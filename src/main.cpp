#include "peripherals/Controller.h"
#include "peripherals/Display.h"
#include "peripherals/Drum.h"
#include "peripherals/StatusLed.h"
#include "usb/device/hid/ps3_driver.h"
#include "usb/device/hid/ps4_auth.h"
#include "usb/device_driver.h"
#include "utils/BootLed.h"
#include "utils/BootMacro.h"
#include "utils/BootModeSelect.h"
#include "utils/InputReport.h"
#include "utils/InputState.h"
#include "utils/MacroStore.h"
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
#include <functional>
#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

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
    ShowMacroRecord,
};

struct ControlMessage {
    ControlCommand command;
    union {
        usb_mode_t usb_mode;
        usb_player_led_t player_led;
        uint8_t led_brightness;
        bool led_enable_player_color;
        uint16_t macro_event_count;
    } data;
};

// Macro record/replay packs every controller input except the drum pads. See
// MacroStore::Button. L/R are recorded, but a 600ms L+R hold ends recording.
uint16_t packMacroButtons(const Utils::InputState::Controller &c) {
    uint16_t b = 0;
    if (c.buttons.north) b |= Utils::MacroStore::BTN_NORTH;
    if (c.buttons.east) b |= Utils::MacroStore::BTN_EAST;
    if (c.buttons.south) b |= Utils::MacroStore::BTN_SOUTH;
    if (c.buttons.west) b |= Utils::MacroStore::BTN_WEST;
    if (c.buttons.start) b |= Utils::MacroStore::BTN_START;
    if (c.buttons.select) b |= Utils::MacroStore::BTN_SELECT;
    if (c.buttons.home) b |= Utils::MacroStore::BTN_HOME;
    if (c.buttons.share) b |= Utils::MacroStore::BTN_SHARE;
    if (c.dpad.up) b |= Utils::MacroStore::BTN_DPAD_UP;
    if (c.dpad.down) b |= Utils::MacroStore::BTN_DPAD_DOWN;
    if (c.dpad.left) b |= Utils::MacroStore::BTN_DPAD_LEFT;
    if (c.dpad.right) b |= Utils::MacroStore::BTN_DPAD_RIGHT;
    if (c.buttons.l) b |= Utils::MacroStore::BTN_L;
    if (c.buttons.r) b |= Utils::MacroStore::BTN_R;
    return b;
}

void applyMacroButtons(uint16_t b, Utils::InputState::Controller &c) {
    c.buttons.north = (b & Utils::MacroStore::BTN_NORTH) != 0;
    c.buttons.east = (b & Utils::MacroStore::BTN_EAST) != 0;
    c.buttons.south = (b & Utils::MacroStore::BTN_SOUTH) != 0;
    c.buttons.west = (b & Utils::MacroStore::BTN_WEST) != 0;
    c.buttons.start = (b & Utils::MacroStore::BTN_START) != 0;
    c.buttons.select = (b & Utils::MacroStore::BTN_SELECT) != 0;
    c.buttons.home = (b & Utils::MacroStore::BTN_HOME) != 0;
    c.buttons.share = (b & Utils::MacroStore::BTN_SHARE) != 0;
    c.dpad.up = (b & Utils::MacroStore::BTN_DPAD_UP) != 0;
    c.dpad.down = (b & Utils::MacroStore::BTN_DPAD_DOWN) != 0;
    c.dpad.left = (b & Utils::MacroStore::BTN_DPAD_LEFT) != 0;
    c.dpad.right = (b & Utils::MacroStore::BTN_DPAD_RIGHT) != 0;
    c.buttons.l = (b & Utils::MacroStore::BTN_L) != 0;
    c.buttons.r = (b & Utils::MacroStore::BTN_R) != 0;
}

// Replays a recorded macro once, keeping the USB device alive throughout so the
// host stays enumerated and registers button holds. Flashes the onboard LED
// green while playing. `interrupted` is polled to allow aborting via a real
// button press. Blocks until done or aborted.
void replayMacro(const std::vector<Utils::MacroStore::Event> &events, Utils::InputReport &input_report, usb_mode_t mode,
                 const Utils::BootLed::Config &led_config, const std::function<bool()> &interrupted) {
    Utils::InputState state;
    Utils::BootLed led(led_config);
    bool led_on = false;
    bool abort = false;

    const auto pump = [&](uint32_t duration_ms) {
        const uint32_t until = to_ms_since_boot(get_absolute_time()) + duration_ms;
        do {
            const uint32_t now = to_ms_since_boot(get_absolute_time());
            const bool on = (now / 250) % 2 == 0; // ~2Hz green flash
            if (on != led_on) {
                led_on = on;
                led.fill(0, on ? 255 : 0, 0);
            }
            usbd_driver_send_report(input_report.getReport(state, mode));
            usbd_driver_task();
            if (interrupted && interrupted()) {
                abort = true;
                return;
            }
            sleep_ms(1);
        } while (to_ms_since_boot(get_absolute_time()) < until);
    };

    // Warmup so USB enumeration completes before the first injected press.
    pump(750);

    for (const auto &ev : events) {
        if (abort) break;
        pump(ev.delta_ms);
        applyMacroButtons(ev.buttons, state.controller);
    }

    if (!abort) {
        pump(100); // hold the final state briefly
    }

    // Always release everything on the way out.
    state.releaseAll();
    usbd_driver_send_report(input_report.getReport(state, mode));
    usbd_driver_task();
}

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
            case ControlCommand::ShowMacroRecord:
                display.showMacroRecord(control_msg.data.macro_event_count);
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

    // Headless macro feature: holding L+R at boot either clears an existing
    // macro or arms recording of a new one. With no hotkey, a stored macro is
    // replayed once before the main loop. Flash writes are deferred until core1
    // has installed its multicore_lockout victim handler.
    Utils::MacroStore macro_store;
    uint32_t macro_arm_press_time = 0;
    const bool macro_hotkey = Utils::BootMacro::check(Config::Default::controller_config, 300, &macro_arm_press_time);
    const bool macro_clear_requested = macro_hotkey && macro_store.hasMacro();
    const bool macro_arm_recording = macro_hotkey && !macro_store.hasMacro();
    const bool macro_replay = !macro_hotkey && macro_store.hasMacro();

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

    Utils::Menu menu(settings_store, [&macro_store]() { macro_store.clear(); });
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

    // Clear the stored macro now that flash writes are safe (core1 lockout ready).
    if (macro_clear_requested) {
        macro_store.clear();
        // Confirm with three quick red blinks on the onboard LED (blocking is
        // fine here, before the main loop / USB traffic).
        Utils::BootLed led(boot_led_config);
        for (int i = 0; i < 3; ++i) {
            led.fill(255, 0, 0);
            sleep_ms(120);
            led.fill(0, 0, 0);
            sleep_ms(120);
        }
    }

    usbd_driver_init(mode);
    usbd_driver_set_player_led_cb([](usb_player_led_t player_led) {
        const auto ctrl_message =
            ControlMessage{.command = ControlCommand::SetPlayerLed, .data = {.player_led = player_led}};
        queue_try_add(&control_queue, &ctrl_message);
    });

    readSettings();

    // Replay a stored macro once at startup (blocks until finished or aborted
    // by a real button press). Reads live controller input for the abort check.
    if (macro_replay) {
        const auto interrupted = [&]() -> bool {
            Utils::InputState s;
            if (controller) {
                controller->updateInputState(s);
            } else {
                queue_try_remove(&controller_state_queue, &s.controller);
            }
            const auto &b = s.controller.buttons;
            const auto &d = s.controller.dpad;
            return b.north || b.east || b.south || b.west || b.start || b.select || b.home || b.share || b.l || b.r ||
                   d.up || d.down || d.left || d.right;
        };
        replayMacro(macro_store.events(), input_report, mode, boot_led_config, interrupted);
        input_state.releaseAll();
    }

    // Live macro recording state. Only armed when L+R were held at boot and no
    // macro existed yet. WaitRelease ensures the boot-hold of L+R is released
    // before capture begins; recording stops on a fresh L+R hold.
    enum class MacroRecState : uint8_t { Idle, WaitRelease, Recording };
    MacroRecState macro_rec = macro_arm_recording ? MacroRecState::WaitRelease : MacroRecState::Idle;
    std::vector<Utils::MacroStore::Event> macro_events;
    uint16_t macro_last_buttons = 0;
    uint32_t macro_last_event_time = 0;
    uint32_t macro_stop_hold_since = 0;
    bool macro_stop_hold_active = false;
    static const uint32_t macro_stop_hold_ms = 600;
    // Onboard-LED "recording" blink (same LED the mode switcher flashes), so
    // OLED-less boards still get feedback. Driven non-blocking from the loop.
    std::unique_ptr<Utils::BootLed> macro_led;
    bool macro_led_on = false;

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

        // Macro recording: capture face-button transitions with timing. Runs on
        // the live controller input (only ever armed for InternalGpio builds).
        if (macro_rec != MacroRecState::Idle) {
            const uint32_t now = to_ms_since_boot(get_absolute_time());
            const bool lr = input_state.controller.buttons.l && input_state.controller.buttons.r;

            if (macro_rec == MacroRecState::WaitRelease) {
                if (!input_state.controller.buttons.l && !input_state.controller.buttons.r) {
                    macro_rec = MacroRecState::Recording;
                    macro_last_buttons = 0;
                    // Anchor the clock at when L+R were first pressed (boot), not
                    // at release, so the first pause includes the hold time.
                    macro_last_event_time = macro_arm_press_time;
                    macro_stop_hold_active = false;
                    macro_led = std::make_unique<Utils::BootLed>(boot_led_config);
                    macro_led_on = false;
                    const ControlMessage msg{.command = ControlCommand::ShowMacroRecord,
                                             .data = {.macro_event_count = 0}};
                    queue_try_add(&control_queue, &msg);
                }
            } else if (lr) { // Recording: L+R held -> candidate stop
                if (!macro_stop_hold_active) {
                    macro_stop_hold_active = true;
                    macro_stop_hold_since = now;
                } else if ((now - macro_stop_hold_since) >= macro_stop_hold_ms) {
                    macro_store.save(macro_events);
                    macro_rec = MacroRecState::Idle;
                    macro_led.reset(); // turn LED off + release the pin
                    const ControlMessage msg{.command = ControlCommand::ExitMenu, .data = {}};
                    queue_add_blocking(&control_queue, &msg);
                }
            } else { // Recording: capture transitions
                macro_stop_hold_active = false;
                const uint16_t buttons = packMacroButtons(input_state.controller);
                if (buttons != macro_last_buttons && macro_events.size() < Utils::MacroStore::MAX_EVENTS) {
                    uint32_t delta = now - macro_last_event_time;
                    // Split gaps longer than a uint16 into no-change filler events.
                    while (delta > 0xFFFF && macro_events.size() < Utils::MacroStore::MAX_EVENTS) {
                        macro_events.push_back({0xFFFF, macro_last_buttons});
                        delta -= 0xFFFF;
                    }
                    macro_events.push_back({static_cast<uint16_t>(delta), buttons});
                    macro_last_buttons = buttons;
                    macro_last_event_time = now;
                    const ControlMessage msg{
                        .command = ControlCommand::ShowMacroRecord,
                        .data = {.macro_event_count = static_cast<uint16_t>(macro_events.size())}};
                    queue_try_add(&control_queue, &msg);
                }
            }

            // Non-blocking "recording" blink on the onboard LED (~1.5Hz).
            if (macro_led && macro_rec == MacroRecState::Recording) {
                const bool on = (now / 330) % 2 == 0;
                if (on != macro_led_on) {
                    macro_led_on = on;
                    macro_led->fill(on ? 255 : 0, 0, 0);
                }
            }

            // Until the boot-time L+R hold is released, keep it out of the USB
            // report. Once recording, L/R pass through (they are recorded too).
            if (macro_rec == MacroRecState::WaitRelease) {
                input_state.controller.buttons.l = false;
                input_state.controller.buttons.r = false;
            }
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

        } else if (macro_rec == MacroRecState::Idle && checkHotkey()) {
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
