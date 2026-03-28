#include "peripherals/Controller.h"
#include "peripherals/Display.h"
#include "peripherals/Drum.h"
#include "peripherals/StatusLed.h"
#include "usb/device/hid/ps4_auth.h"
#include "usb/device_driver.h"
#include "utils/InputReport.h"
#include "utils/InputState.h"
#include "utils/Menu.h"
#include "utils/PS4AuthProvider.h"
#include "utils/SerialConfig.h"
#include "utils/SettingsStore.h"

#include "GlobalConfiguration.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/util/queue.h"

#include <array>
#include <cstdio>
#include <string>

using namespace Doncon;

namespace {

queue_t control_queue;
queue_t menu_display_queue;
queue_t drum_input_queue;
queue_t controller_input_queue;

queue_t auth_challenge_queue;
queue_t auth_signed_challenge_queue;

std::shared_ptr<Utils::SettingsStore> g_settings_store;
std::string g_ps4_auth_key_pem;
std::array<uint8_t, Utils::PS4AuthProvider::SERIAL_LENGTH> g_ps4_auth_serial{};
std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH> g_ps4_auth_signature{};

enum class ControlCommand : uint8_t {
    SetUsbMode,
    SetPlayerLed,
    SetLedBrightness,
    SetLedEnablePlayerColor,
    EnterMenu,
    ExitMenu,
};

struct ControlMessage {
    ControlCommand command;
    union {
        usb_mode_t usb_mode;
        usb_player_led_t player_led;
        uint8_t led_brightness;
        bool led_enable_player_color;
    } data;
};

void core1_task() {
    multicore_lockout_victim_init();

    // Init i2c port here because Controller and Display share it and
    // therefore can't init it themself.
    gpio_set_function(Config::Default::i2c_config.sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(Config::Default::i2c_config.scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(Config::Default::i2c_config.sda_pin);
    gpio_pull_up(Config::Default::i2c_config.scl_pin);
    i2c_init(Config::Default::i2c_config.block, Config::Default::i2c_config.speed_hz);

    Peripherals::Controller controller(Config::Default::controller_config);
    Peripherals::StatusLed led(Config::Default::led_config);
    Peripherals::Display display(Config::Default::display_config, g_settings_store);

    Utils::PS4AuthProvider ps4authprovider(g_ps4_auth_key_pem);
    std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH> auth_challenge{};

    Utils::InputState input_state;
    Utils::Menu::State menu_display_msg{};
    ControlMessage control_msg{};

    display.drawSplashScreen();

    while (true) {
        controller.updateInputState(input_state);

        // Mask buttons on pins shared with LED data line while LEDs are driving
        if (led.isActive()) {
            input_state.controller.buttons.home = false;
            input_state.controller.buttons.share = false;
        }

        queue_try_add(&controller_input_queue, &input_state.controller);
        queue_try_remove(&drum_input_queue, &input_state.drum);

        if (queue_try_remove(&control_queue, &control_msg)) {
            switch (control_msg.command) {
            case ControlCommand::SetUsbMode:
                display.setUsbMode(control_msg.data.usb_mode);
                break;
            case ControlCommand::SetPlayerLed:
                switch (control_msg.data.player_led.type) {
                case USB_PLAYER_LED_ID:
                    display.setPlayerId(control_msg.data.player_led.id);
                    break;
                case USB_PLAYER_LED_COLOR:
                    break;
                }
                break;
            case ControlCommand::SetLedBrightness:
                led.setBrightness(control_msg.data.led_brightness);
                break;
            case ControlCommand::SetLedEnablePlayerColor:
                led.setEnablePlayerColor(control_msg.data.led_enable_player_color);
                break;
            case ControlCommand::EnterMenu:
                display.showMenu();
                break;
            case ControlCommand::ExitMenu:
                display.showIdle();
                break;
            }
        }
        if (queue_try_remove(&menu_display_queue, &menu_display_msg)) {
            display.setMenuState(menu_display_msg);
        }
        if (queue_try_remove(&auth_challenge_queue, auth_challenge.data())) {
            const auto signed_challenge = ps4authprovider.sign(auth_challenge);
            if (signed_challenge) {
                queue_try_remove(&auth_signed_challenge_queue, nullptr); // clear stale response
                queue_try_add(&auth_signed_challenge_queue, signed_challenge->data());
            }
        }

        led.setInputState(input_state);
        display.setInputState(input_state);

        led.update();
        display.update();
    }
}

} // namespace

int main() {
    queue_init(&control_queue, sizeof(ControlMessage), 1);
    queue_init(&menu_display_queue, sizeof(Utils::Menu::State), 1);
    queue_init(&drum_input_queue, sizeof(Utils::InputState::Drum), 1);
    queue_init(&controller_input_queue, sizeof(Utils::InputState::Controller), 1);
    queue_init(&auth_challenge_queue, sizeof(std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH>), 1);
    queue_init(&auth_signed_challenge_queue, sizeof(std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH>), 1);

    stdio_init_all();

    // Initialize SettingsStore first to get saved ADC channel configuration
    g_settings_store = std::make_shared<Utils::SettingsStore>();
    auto settings_store = g_settings_store;

    // Create drum config with ADC channels from settings
    auto drum_config = Config::Default::drum_config;
    drum_config.adc_channels = settings_store->getAdcChannels();
    Peripherals::Drum drum(drum_config);

    Utils::InputState input_state;
    // True while a start+select combo is in progress; suppresses those buttons
    // in the USB report without touching input_state itself (so stale queue
    // frames can't reset the hold timer by replaying suppressed false values).
    bool suppress_menu_buttons = false;
    const auto checkHotkey = [&input_state, &suppress_menu_buttons]() {
        static const uint32_t hold_timeout = 2000;
        static uint32_t hold_since = 0;
        static bool hold_active = false;
        static bool combo_in_progress = false;

        const bool start = input_state.controller.buttons.start;
        const bool select = input_state.controller.buttons.select;

        if (start && select) {
            combo_in_progress = true;
            suppress_menu_buttons = true;

            const uint32_t now = to_ms_since_boot(get_absolute_time());
            if (!hold_active) {
                hold_active = true;
                hold_since = now;
            } else if ((now - hold_since) > hold_timeout) {
                hold_active = false;
                combo_in_progress = false;
                return true;
            }
        } else {
            hold_active = false;
            if (!start && !select) {
                combo_in_progress = false;
            }
            // Keep suppressing until both are fully released so that releasing
            // one button before the other doesn't send a stray key press
            suppress_menu_buttons = combo_in_progress;
        }

        return false;
    };

    Utils::InputReport input_report(settings_store);

    const auto mode = settings_store->getUsbMode();
    const auto readSettings = [&]() {
        const auto sendCtrlMessage = [&](const ControlMessage &msg) { queue_add_blocking(&control_queue, &msg); };

        sendCtrlMessage({.command = ControlCommand::SetUsbMode, .data = {.usb_mode = mode}});
        sendCtrlMessage({.command = ControlCommand::SetLedBrightness,
                         .data = {.led_brightness = settings_store->getLedBrightness()}});
        sendCtrlMessage({.command = ControlCommand::SetLedEnablePlayerColor,
                         .data = {.led_enable_player_color = settings_store->getLedEnablePlayerColor()}});

        drum.setDebounceDelay(settings_store->getDebounceDelay());
        drum.setDonDebounceMs(settings_store->getDonDebounceMs());
        drum.setKatDebounceMs(settings_store->getKatDebounceMs());

        drum.setCrosstalkDebounceMs(settings_store->getCrosstalkDebounceMs());
        drum.setKeyTimeoutMs(settings_store->getKeyTimeoutMs());
        drum.setTriggerThresholds(settings_store->getTriggerThresholds());
        drum.setDoubleTriggerMode(settings_store->getDoubleTriggerMode());
        drum.setDoubleThresholds(settings_store->getDoubleTriggerThresholds());
        drum.setCutoffThresholds(settings_store->getCutoffThresholds());
        drum.setAdcChannels(settings_store->getAdcChannels());
    };

    Utils::Menu menu(settings_store);
    Utils::SerialConfig serial_config(*settings_store, readSettings);

    std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH> auth_challenge_response{};
    if (settings_store->getPS4AuthCredentials(g_ps4_auth_serial, g_ps4_auth_signature, g_ps4_auth_key_pem)) {
        ps4_auth_init(g_ps4_auth_key_pem.c_str(), g_ps4_auth_key_pem.size() + 1, g_ps4_auth_serial.data(),
                      g_ps4_auth_signature.data(),
                      [](const uint8_t *challenge) { queue_try_add(&auth_challenge_queue, challenge); });
    }

    multicore_launch_core1(core1_task);

    usbd_driver_init(mode);
    usbd_driver_set_player_led_cb([](usb_player_led_t player_led) {
        const auto ctrl_message =
            ControlMessage{.command = ControlCommand::SetPlayerLed, .data = {.player_led = player_led}};
        queue_try_add(&control_queue, &ctrl_message);
    });

    readSettings();

    while (true) {
        drum.updateInputState(input_state);
        queue_try_remove(&controller_input_queue, &input_state.controller);

        const auto drum_message = input_state.drum;

        if (menu.active()) {
            menu.update(input_state.controller);
            if (menu.active()) {
                const auto display_msg = menu.getState();
                queue_add_blocking(&menu_display_queue, &display_msg);
            } else {
                settings_store->store();

                ControlMessage ctrl_message = {.command = ControlCommand::ExitMenu, .data = {}};
                queue_add_blocking(&control_queue, &ctrl_message);
            }

            readSettings();
            input_state.releaseAll();

        } else if (checkHotkey()) {
            menu.activate();

            ControlMessage ctrl_message{.command = ControlCommand::EnterMenu, .data = {}};
            queue_add_blocking(&control_queue, &ctrl_message);
        }

        usbd_driver_send_report(input_report.getReport(input_state, mode));
        usbd_driver_task();

        // Process serial configuration commands
        serial_config.processSerial();

        // Send sensor data if streaming is active
        serial_config.sendSensorDataIfStreaming(input_state);

        queue_try_add(&drum_input_queue, &drum_message);

        if (queue_try_remove(&auth_signed_challenge_queue, auth_challenge_response.data())) {
            ps4_auth_set_signed_challenge(auth_challenge_response.data());
        }
    }

    return 0;
}