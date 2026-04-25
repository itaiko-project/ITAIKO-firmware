#include "peripherals/Display.h"

#include "hardware/gpio.h"
#include "pico/time.h"

#ifndef NO_SCREEN
#include "bitmaps/MenuScreens.h"
#endif

#include <list>
#include <numeric>
#include <string>

namespace Doncon::Peripherals {

namespace {

#ifndef NO_SCREEN
std::string modeToString(usb_mode_t mode) {
    switch (mode) {
    case USB_MODE_SWITCH_TATACON:
        return "Switch Tatacon";
    case USB_MODE_SWITCH_HORIPAD:
        return "Switch Horipad";
    case USB_MODE_DUALSHOCK3:
        return "Dualshock 3";
    case USB_MODE_PS4_TATACON:
        return "PS4 Tatacon";
    case USB_MODE_DUALSHOCK4:
        return "Dualshock 4";
    case USB_MODE_KEYBOARD_P1:
        return "Keyboard P1";
    case USB_MODE_KEYBOARD_P2:
        return "Keyboard P2";
    case USB_MODE_XBOX360:
        return "Xbox 360";
    case USB_MODE_XBOX360_ANALOG_P1:
        return "Analog P1";
    case USB_MODE_XBOX360_ANALOG_P2:
        return "Analog P2";
    case USB_MODE_MIDI:
        return "MIDI";
    case USB_MODE_DEBUG:
        return "Debug";
    case USB_MODE_USIO_TAIKO:
        return "USIO Taiko";
    }
    return "?";
}
#endif

} // namespace

Display::Display(const Config &config, std::shared_ptr<Utils::SettingsStore> settings_store)
    : m_config(config), m_settings_store(settings_store) {
#ifndef NO_SCREEN
    m_display.external_vcc = false;
    ssd1306_init(&m_display, 128, 64, m_config.i2c_address, m_config.i2c_block);
    ssd1306_clear(&m_display);
    m_last_activity_time = to_ms_since_boot(get_absolute_time());
#endif
}

void Display::setInputState(const Utils::InputState &state) {
#ifndef NO_SCREEN
    // Check for any new activity to reset the screen timeout
    if (hasActivity(state)) {
        m_last_activity_time = to_ms_since_boot(get_absolute_time());

        // Wake up screen if it was off
        if (m_screen_off) {
            m_screen_off = false;
            ssd1306_poweron(&m_display);
        }
    }

    m_last_input_state = m_input_state;
    m_input_state = state;
#else
    (void)state;
#endif
}

bool Display::hasActivity(const Utils::InputState &state) {
    // Check for any drum hit (new trigger that wasn't triggered before)
    if ((state.drum.don_left.triggered && !m_last_input_state.drum.don_left.triggered) ||
        (state.drum.don_right.triggered && !m_last_input_state.drum.don_right.triggered) ||
        (state.drum.ka_left.triggered && !m_last_input_state.drum.ka_left.triggered) ||
        (state.drum.ka_right.triggered && !m_last_input_state.drum.ka_right.triggered)) {
        return true;
    }

    // Check for any button press (new press that wasn't pressed before)
    const auto &btn = state.controller.buttons;
    const auto &last_btn = m_last_input_state.controller.buttons;
    if ((btn.north && !last_btn.north) || (btn.south && !last_btn.south) || (btn.east && !last_btn.east) ||
        (btn.west && !last_btn.west) || (btn.l && !last_btn.l) || (btn.r && !last_btn.r) ||
        (btn.start && !last_btn.start) || (btn.select && !last_btn.select) || (btn.home && !last_btn.home) ||
        (btn.share && !last_btn.share)) {
        return true;
    }

    // Check for any dpad press
    const auto &dpad = state.controller.dpad;
    const auto &last_dpad = m_last_input_state.controller.dpad;
    if ((dpad.up && !last_dpad.up) || (dpad.down && !last_dpad.down) || (dpad.left && !last_dpad.left) ||
        (dpad.right && !last_dpad.right)) {
        return true;
    }

    return false;
}

void Display::setUsbMode(usb_mode_t mode) { m_usb_mode = mode; };
void Display::setPlayerId(uint8_t player_id) { m_player_id = player_id; };
void Display::setSigningActive(bool signing_active) { m_signing_active = signing_active; };

void Display::setMenuState(const Utils::Menu::State &menu_state) { m_menu_state = menu_state; }

void Display::showIdle() { m_state = State::Idle; }
void Display::showMenu() { m_state = State::Menu; }

void Display::drawIdleScreen() {
#ifndef NO_SCREEN
    // Header
    const std::string mode_string = modeToString(m_usb_mode) + " mode";
    ssd1306_draw_string(&m_display, 0, 0, 1, mode_string.c_str());
    ssd1306_draw_line(&m_display, 0, 10, 128, 10);

    // Center text area: roll counter (default) or PS4 signing status
    if (m_signing_active) {
        const std::string signing = "Signing...";
        ssd1306_draw_string(&m_display, (127 - (signing.length() * 12)) / 2, 24, 2, signing.c_str());
    } else {
        auto roll_str = std::to_string(m_input_state.drum.current_roll) + " Roll";
        auto prev_roll_str = "Last " + std::to_string(m_input_state.drum.previous_roll);
        ssd1306_draw_string(&m_display, (127 - (roll_str.length() * 12)) / 2, 20, 2, roll_str.c_str());
        ssd1306_draw_string(&m_display, (127 - (prev_roll_str.length() * 6)) / 2, 40, 1, prev_roll_str.c_str());
    }

    // Player "LEDs"
    if (m_player_id != 0) {
        for (uint8_t i = 0; i < 4; ++i) {
            if ((m_player_id & (1 << i)) == 0) {
                ssd1306_draw_square(&m_display, (127) - ((4 - i) * 6), 3, 2, 2);
            } else {
                ssd1306_draw_square(&m_display, ((127) - ((4 - i) * 6)) - 1, 2, 4, 4);
            }
        }
    }

    // Menu hint
    ssd1306_draw_line(&m_display, 0, 54, 128, 54);
    ssd1306_draw_string(&m_display, 0, 56, 1, "Hold + and - for Menu");
#endif
}


void Display::drawSplashScreen() {
#ifndef NO_SCREEN
    // Non-blocking splash screen - just draw it once and record the time
    ssd1306_clear(&m_display);

    ssd1306_bmp_show_image(&m_display, splash_screen.data(), splash_screen.size());

    ssd1306_show(&m_display);
    m_splash_start_time = to_ms_since_boot(get_absolute_time());
    m_state = State::Splash;
#endif
}

void Display::drawMenuScreen() {
#ifndef NO_SCREEN
    auto descriptor_it = Utils::Menu::descriptors.find(m_menu_state.page);
    if (descriptor_it == Utils::Menu::descriptors.end()) {
        return;
    }

    // Background
    switch (descriptor_it->second.type) {
    case Utils::Menu::Descriptor::Type::Menu:
        if (m_menu_state.page == Utils::Menu::Page::Main) {
            ssd1306_bmp_show_image(&m_display, menu_screen_top.data(), menu_screen_top.size());
        } else {
            ssd1306_bmp_show_image(&m_display, menu_screen_sub.data(), menu_screen_sub.size());
        }
        break;
    case Utils::Menu::Descriptor::Type::Value:
        ssd1306_bmp_show_image(&m_display, menu_screen_value.data(), menu_screen_value.size());
        break;
    case Utils::Menu::Descriptor::Type::Selection:
    case Utils::Menu::Descriptor::Type::Toggle:
    case Utils::Menu::Descriptor::Type::Info:
        ssd1306_bmp_show_image(&m_display, menu_screen_sub.data(), menu_screen_sub.size());
        break;
    case Utils::Menu::Descriptor::Type::RebootInfo:
        break;
    }

    // Heading
    ssd1306_draw_string(&m_display, 0, 0, 1, descriptor_it->second.name.c_str());

    // Current Selection
    std::string selection;
    switch (descriptor_it->second.type) {
    case Utils::Menu::Descriptor::Type::Menu:
    case Utils::Menu::Descriptor::Type::Selection:
    case Utils::Menu::Descriptor::Type::RebootInfo:
    case Utils::Menu::Descriptor::Type::Info:
        selection = descriptor_it->second.items.at(m_menu_state.selected_value).first;
        break;
    case Utils::Menu::Descriptor::Type::Value:
        selection = std::to_string(m_menu_state.selected_value);
        break;
    case Utils::Menu::Descriptor::Type::Toggle:
        selection = m_menu_state.selected_value != 0 ? "On" : "Off";
        break;
    }
    ssd1306_draw_string(&m_display, (127 - (selection.length() * 12)) / 2, 15, 2, selection.c_str());

    // Breadcrumbs
    switch (descriptor_it->second.type) {
    case Utils::Menu::Descriptor::Type::Menu:
    case Utils::Menu::Descriptor::Type::Selection: {
        auto selection_count = descriptor_it->second.items.size();
        for (size_t i = 0; i < selection_count; ++i) {
            if (i == m_menu_state.selected_value) {
                ssd1306_draw_square(&m_display, ((127) - ((selection_count - i) * 6)) - 1, 2, 4, 4);
            } else {
                ssd1306_draw_square(&m_display, (127) - ((selection_count - i) * 6), 3, 2, 2);
            }
        }
    } break;
    case Utils::Menu::Descriptor::Type::RebootInfo:
    case Utils::Menu::Descriptor::Type::Info:
    case Utils::Menu::Descriptor::Type::Value:
    case Utils::Menu::Descriptor::Type::Toggle:
        break;
    }
#endif
}

void Display::update() {
#ifndef NO_SCREEN
    static const uint32_t interval_ms = 17;            // Limit to ~60fps
    static const uint32_t splash_duration_ms = 2000;   // Show splash for 2 seconds
    static const uint32_t screen_off_timeout_ms = 60000; // Turn off after 60 seconds of inactivity

    // Handle splash screen timeout
    if (m_state == State::Splash) {
        if (to_ms_since_boot(get_absolute_time()) - m_splash_start_time >= splash_duration_ms) {
            m_state = State::Idle; // Transition to idle after splash duration
            m_last_activity_time = to_ms_since_boot(get_absolute_time()); // Reset activity timer
        } else {
            return; // Keep showing splash screen, don't update
        }
    }

    // Check for inactivity timeout (only in Idle state, not in Menu)
    if (m_state == State::Idle && !m_screen_off) {
        if (to_ms_since_boot(get_absolute_time()) - m_last_activity_time >= screen_off_timeout_ms) {
            m_screen_off = true;
            ssd1306_poweroff(&m_display);
            return;
        }
    }

    // Don't update display if screen is off
    if (m_screen_off) {
        return;
    }

    if (to_ms_since_boot(get_absolute_time()) - m_next_frame_time < interval_ms) {
        return;
    }
    m_next_frame_time += interval_ms;

    ssd1306_clear(&m_display);

    switch (m_state) {
    case State::Splash:
        // Should never reach here due to check above, but handle gracefully
        break;
    case State::Idle:
        drawIdleScreen();
        break;
    case State::Menu:
        drawMenuScreen();
        break;
    }

    ssd1306_show(&m_display);
#endif
};

} // namespace Doncon::Peripherals
