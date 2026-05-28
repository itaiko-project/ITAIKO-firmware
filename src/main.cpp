#include "peripherals/Controller.h"
#include "peripherals/Display.h"
#include "peripherals/Drum.h"
#include "peripherals/StatusLed.h"
#include "usb/device/hid/ps3_driver.h"
#include "usb/device/hid/ps4_auth.h"
#include "usb/device_driver.h"
#include "utils/BootModeSelect.h"
#include "utils/InputReport.h"
#include "utils/InputState.h"
#include "utils/Menu.h"
#include "utils/PS4AuthProvider.h"
#include "utils/SerialConfig.h"
#include "utils/SettingsStore.h"

#include "GlobalConfiguration.h"

#include "hardware/adc.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/util/queue.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <variant>

using namespace Doncon;

namespace {

queue_t control_queue;
queue_t menu_display_queue;
queue_t drum_input_queue;
queue_t controller_state_queue;

queue_t auth_challenge_queue;
queue_t auth_signed_challenge_queue;

std::shared_ptr<Utils::SettingsStore> g_settings_store;
std::string g_ps4_auth_key_pem;
std::array<uint8_t, Utils::PS4AuthProvider::SERIAL_LENGTH> g_ps4_auth_serial{};
std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH> g_ps4_auth_signature{};
std::atomic_bool g_led_is_active{false};
const bool kControllerOnCore0 =
    std::holds_alternative<Peripherals::Controller::Config::InternalGpio>(Config::Default::controller_config.gpio_config);

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

    std::unique_ptr<Peripherals::Controller> controller;
    if (!kControllerOnCore0) {
        controller = std::make_unique<Peripherals::Controller>(Config::Default::controller_config);
    }

    Peripherals::StatusLed led(Config::Default::led_config);
    Peripherals::Display display(Config::Default::display_config, g_settings_store);

    Utils::PS4AuthProvider ps4authprovider(g_ps4_auth_key_pem);
    std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH> pending_auth_challenge{};
    bool has_pending_auth_challenge = false;
    bool signing_active = false;

    Utils::InputState input_state;
    Utils::Menu::State menu_display_msg{};
    ControlMessage control_msg{};

    display.drawSplashScreen();

    while (true) {
        const bool led_active = led.isActive();
        g_led_is_active.store(led_active, std::memory_order_relaxed);

        if (controller) {
            controller->updateInputState(input_state);

            // Mask buttons on pins shared with LED data line while LEDs are driving
            if (led_active) {
                input_state.controller.buttons.home = false;
                input_state.controller.buttons.share = false;
            }

            queue_try_add(&controller_state_queue, &input_state.controller);
        } else {
            queue_try_remove(&controller_state_queue, &input_state.controller);
        }

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
        if (!has_pending_auth_challenge && queue_try_remove(&auth_challenge_queue, pending_auth_challenge.data())) {
            has_pending_auth_challenge = true;
            signing_active = true;
        }

        display.setSigningActive(signing_active);
        led.setInputState(input_state);
        display.setInputState(input_state);

        led.update();
        display.update();

        if (has_pending_auth_challenge) {
            const auto signed_challenge = ps4authprovider.sign(pending_auth_challenge);
            if (signed_challenge) {
                queue_try_remove(&auth_signed_challenge_queue, nullptr); // clear stale response
                queue_try_add(&auth_signed_challenge_queue, signed_challenge->data());
            }
            has_pending_auth_challenge = false;
            signing_active = false;
        }
    }
}

} // namespace

int main() {
    queue_init(&control_queue, sizeof(ControlMessage), 1);
    queue_init(&menu_display_queue, sizeof(Utils::Menu::State), 1);
    queue_init(&drum_input_queue, sizeof(Utils::InputState::Drum), 1);
    queue_init(&controller_state_queue, sizeof(Utils::InputState::Controller), 1);
    queue_init(&auth_challenge_queue, sizeof(std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH>), 1);
    queue_init(&auth_signed_challenge_queue, sizeof(std::array<uint8_t, Utils::PS4AuthProvider::SIGNATURE_LENGTH>), 1);

    stdio_init_all();

    // Initialize SettingsStore first to get saved ADC channel configuration
    g_settings_store = std::make_shared<Utils::SettingsStore>();
    auto settings_store = g_settings_store;

    // Headless boot mode select: if a recognized button is held during boot,
    // switch USB mode and confirm via LED pattern. Persisted to flash later,
    // once core1 has installed the multicore_lockout victim handler.
    //
    // Drives the Waveshare RP2040-Zero onboard WS2812 (GPIO 16, single LED).
    // This is independent of the main LED strip config (which targets an
    // external strip on GPIO 15 for boards that have one).
    const Utils::BootModeSelect::LedConfig boot_led_config = {
        .pin = 16,
        .count = 1,
        .brightness = 64,
        .is_rgbw = false,
        .reversed = false,
    };
    const bool boot_mode_changed =
        Utils::BootModeSelect::run(*settings_store, Config::Default::controller_config, boot_led_config);

    // Create drum config with ADC channels from settings
    auto drum_config = Config::Default::drum_config;
    drum_config.adc_channels = settings_store->getAdcChannels();
    Peripherals::Drum drum(drum_config);
    std::unique_ptr<Peripherals::Controller> controller;
    if (kControllerOnCore0) {
        controller = std::make_unique<Peripherals::Controller>(Config::Default::controller_config);
    }

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
    // Give core1 a moment to install its multicore_lockout victim handler before
    // we trigger a flash write (which uses multicore_lockout_start_blocking).
    sleep_ms(50);

    uint8_t ps3_mac[6];
    if (!settings_store->hasPs3Mac()) {
        // Mix ADC noise from the internal temperature sensor into a 6-byte MAC.
        // The temp sensor is always present and its LSBs jitter independently of
        // anything connected to the drum inputs.
        adc_init();
        adc_set_temp_sensor_enabled(true);
        adc_select_input(4);
        std::memset(ps3_mac, 0, sizeof(ps3_mac));
        for (int i = 0; i < 50; ++i) {
            const uint16_t raw = adc_read();
            ps3_mac[i % 6] ^= static_cast<uint8_t>(raw & 0xFF);
            ps3_mac[(i + 3) % 6] ^= static_cast<uint8_t>((raw >> 4) & 0xFF);
            sleep_us(200);
        }
        adc_set_temp_sensor_enabled(false);
        // Force locally-administered unicast MAC.
        ps3_mac[0] = (ps3_mac[0] & 0xFC) | 0x02;
        settings_store->setPs3Mac(ps3_mac);
        settings_store->store();
    } else {
        settings_store->getPs3Mac(ps3_mac);
    }
    hid_ps3_set_mac(ps3_mac);

    // Persist any boot-time mode change now that core1's lockout victim is ready.
    if (boot_mode_changed) {
        settings_store->store();
    }

    usbd_driver_init(mode);
    usbd_driver_set_player_led_cb([](usb_player_led_t player_led) {
        const auto ctrl_message =
            ControlMessage{.command = ControlCommand::SetPlayerLed, .data = {.player_led = player_led}};
        queue_try_add(&control_queue, &ctrl_message);
    });

    readSettings();

    while (true) {
        drum.updateInputState(input_state);
        if (controller) {
            controller->updateInputState(input_state);

            // Keep behavior consistent with core1-side masking for shared LED pins.
            if (g_led_is_active.load(std::memory_order_relaxed)) {
                input_state.controller.buttons.home = false;
                input_state.controller.buttons.share = false;
            }

            queue_try_add(&controller_state_queue, &input_state.controller);
        } else {
            queue_try_remove(&controller_state_queue, &input_state.controller);
        }

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
