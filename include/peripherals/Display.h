#ifndef PERIPHERALS_DISPLAY_H_
#define PERIPHERALS_DISPLAY_H_

// Uncomment to disable display support
//#define NO_SCREEN

#include "usb/device_driver.h"
#include "utils/InputState.h"
#include "utils/Menu.h"
#include "utils/SettingsStore.h"

#include "hardware/i2c.h"

#ifndef NO_SCREEN
#include <ssd1306/ssd1306.h>
#endif

#include <array>
#include <cstdint>
#include <memory>

namespace Doncon::Peripherals {

class Display {
  public:
    struct Config {
        i2c_inst_t *i2c_block;
        uint8_t i2c_address;
    };

  private:
    enum class State : uint8_t {
        Splash,
        Idle,
        Menu,
    };

    Config m_config;
    State m_state{State::Splash};

    Utils::InputState m_input_state;
    usb_mode_t m_usb_mode{USB_MODE_SWITCH_TATACON};
    uint8_t m_player_id{0};
    bool m_signing_active{false};

    Utils::Menu::State m_menu_state{};

#ifndef NO_SCREEN
    ssd1306_t m_display{};
#endif
    uint32_t m_next_frame_time{0};
    uint32_t m_splash_start_time{0};

    // Inactivity screen off
    uint32_t m_last_activity_time{0};
    bool m_screen_off{false};
    Utils::InputState m_last_input_state{};

    std::shared_ptr<Utils::SettingsStore> m_settings_store;

    bool hasActivity(const Utils::InputState &state);
    void drawIdleScreen();
    void drawMenuScreen();

  public:
    Display(const Config &config, std::shared_ptr<Utils::SettingsStore> settings_store);

    void setInputState(const Utils::InputState &state);
    void setUsbMode(usb_mode_t mode);
    void setPlayerId(uint8_t player_id);
    void setSigningActive(bool signing_active);

    void setMenuState(const Utils::Menu::State &menu_state);

    void showIdle();
    void showMenu();

    void update();
    void drawSplashScreen();
};

} // namespace Doncon::Peripherals

#endif // PERIPHERALS_DISPLAY_H_
