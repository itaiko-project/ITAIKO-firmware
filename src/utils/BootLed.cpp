#include "utils/BootLed.h"

#include "hardware/gpio.h"
#include "pico/time.h"
#include "pio_ws2812/ws2812.h"

#include <algorithm>
#include <cmath>

namespace Doncon::Utils {

namespace {

struct Rgb {
    uint8_t r, g, b;
};

// HSV -> RGB (h in [0,360), s/v in [0,1])
Rgb hsv(float h, float s, float v) {
    float c = v * s;
    float hh = h / 60.0f;
    float x = c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;
    if (hh < 1) {
        r1 = c;
        g1 = x;
    } else if (hh < 2) {
        r1 = x;
        g1 = c;
    } else if (hh < 3) {
        g1 = c;
        b1 = x;
    } else if (hh < 4) {
        g1 = x;
        b1 = c;
    } else if (hh < 5) {
        r1 = x;
        b1 = c;
    } else {
        r1 = c;
        b1 = x;
    }
    float m = v - c;
    return Rgb{
        static_cast<uint8_t>((r1 + m) * 255.0f),
        static_cast<uint8_t>((g1 + m) * 255.0f),
        static_cast<uint8_t>((b1 + m) * 255.0f),
    };
}

} // namespace

BootLed::BootLed(const Config &config) : m_config(config) {
    ws2812_init(pio0, m_config.pin, m_config.is_rgbw);
    // ws2812_init -> ws2812_program_init already calls pio_gpio_init and sets
    // pin direction. Nothing else needed before sending frames.
    m_frame.assign(m_config.count, 0);
    // Latch reset before first frame (WS2812 needs ~50us low to start a packet).
    sleep_us(300);
}

BootLed::~BootLed() {
    std::fill(m_frame.begin(), m_frame.end(), 0u);
    ws2812_put_frame(pio0, m_frame.data(), m_frame.size());
    while (!pio_sm_is_tx_fifo_empty(pio0, 0)) {
    }
    sleep_us(350);
    gpio_set_function(m_config.pin, GPIO_FUNC_SIO);
    gpio_set_dir(m_config.pin, GPIO_IN);
    gpio_pull_up(m_config.pin);
}

uint8_t BootLed::scale(uint8_t v) const {
    return static_cast<uint8_t>((static_cast<uint32_t>(v) * m_config.brightness) / 255u);
}

void BootLed::fill(uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t px = ws2812_rgb_to_gamma_corrected_u32pixel(scale(r), scale(g), scale(b));
    std::fill(m_frame.begin(), m_frame.end(), px);
    ws2812_put_frame(pio0, m_frame.data(), m_frame.size());
}

void BootLed::rainbowFrame(float phase) {
    for (size_t i = 0; i < m_config.count; ++i) {
        const float h = std::fmod(phase + 360.0f * static_cast<float>(i) / static_cast<float>(m_config.count), 360.0f);
        const Rgb c = hsv(h, 1.0f, 1.0f);
        const size_t idx = m_config.reversed ? (m_config.count - 1 - i) : i;
        m_frame[idx] = ws2812_rgb_to_gamma_corrected_u32pixel(scale(c.r), scale(c.g), scale(c.b));
    }
    ws2812_put_frame(pio0, m_frame.data(), m_frame.size());
}

} // namespace Doncon::Utils
