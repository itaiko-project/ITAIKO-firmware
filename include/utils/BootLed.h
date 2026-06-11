#ifndef UTILS_BOOTLED_H_
#define UTILS_BOOTLED_H_

#include <cstdint>
#include <vector>

namespace Doncon::Utils {

// Minimal WS2812 driver for headless boot/setup feedback on the onboard LED
// (GPIO16 on the Waveshare RP2040-Zero). Claims pio0 while alive and releases
// the data pin back to SIO input on destruction, so it can share the bus with
// other transient users (BootModeSelect's mode confirmation, the macro
// recording indicator). Not for the main effects strip (see StatusLed).
//
// Only safe to use when nothing else holds pio0 (i.e. LED_LIGHTS disabled, or
// before core1 launches).
class BootLed {
  public:
    struct Config {
        uint8_t pin;        // GPIO driving WS2812 data line
        uint16_t count;     // number of LEDs in the chain
        uint8_t brightness; // 0-255 applied to all colors
        bool is_rgbw;       // true for SK6812 RGBW, false for standard WS2812
        bool reversed;      // reverse LED ordering
    };

    explicit BootLed(const Config &config);
    ~BootLed();

    BootLed(const BootLed &) = delete;
    BootLed &operator=(const BootLed &) = delete;

    void fill(uint8_t r, uint8_t g, uint8_t b);
    void rainbowFrame(float phase); // phase in degrees

  private:
    [[nodiscard]] uint8_t scale(uint8_t v) const;

    Config m_config;
    std::vector<uint32_t> m_frame;
};

} // namespace Doncon::Utils

#endif // UTILS_BOOTLED_H_
