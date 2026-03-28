#ifndef PERIPHERALS_STATUSLED_H_
#define PERIPHERALS_STATUSLED_H_

#include "utils/InputState.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Doncon::Peripherals {

class StatusLed {
  public:
    struct Config {
        struct Color {
            uint8_t r;
            uint8_t g;
            uint8_t b;
        };

        Color idle_color;
        Color don_left_color;
        Color ka_left_color;
        Color don_right_color;
        Color ka_right_color;

        uint8_t led_pin;
        uint16_t led_count;
        bool is_rgbw;
        bool reversed;
        uint16_t max_current_ma;

        uint8_t brightness;
        bool enable_player_color;
    };

  private:
    struct ColorFloat {
        float r;
        float g;
        float b;
    };

    struct Ripple {
        float origin;
        float distance;
        Config::Color color;
    };

    Config m_config;

    Utils::InputState m_input_state;
    Utils::InputState m_previous_input_state;
    std::optional<Config::Color> m_player_color;

    std::vector<ColorFloat> m_led_states;
    std::vector<Ripple> m_ripples;
    uint32_t m_last_update_time = 0;
    std::vector<uint32_t> m_leds;
    bool m_active = false;

  public:
    StatusLed(const Config &config);

    void setBrightness(uint8_t brightness);
    void setEnablePlayerColor(bool do_enable);

    void setInputState(const Utils::InputState &input_state);
    void setPlayerColor(const Config::Color &color);

    bool isActive() const;

    void update();
};

} // namespace Doncon::Peripherals

#endif // PERIPHERALS_STATUSLED_H_