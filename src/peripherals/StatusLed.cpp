#include "peripherals/StatusLed.h"

#include "hardware/gpio.h"
#include "pio_ws2812/ws2812.h"

#include <algorithm>
#include <cmath>

namespace Doncon::Peripherals {

StatusLed::StatusLed(const Config &config) : m_config(config) {
    // Initialize PIO state machine for WS2812 (this claims the pin for PIO)
    ws2812_init(pio0, config.led_pin, m_config.is_rgbw);

    // Immediately release pin back to SIO for button use (start in idle state)
    gpio_set_function(m_config.led_pin, GPIO_FUNC_SIO);
    gpio_set_dir(m_config.led_pin, GPIO_IN);
    gpio_pull_up(m_config.led_pin);

    m_leds.resize(m_config.led_count, 0);
    m_led_states.resize(m_config.led_count, {0.0f, 0.0f, 0.0f});
    m_last_update_time = to_ms_since_boot(get_absolute_time());
}

void StatusLed::setBrightness(const uint8_t brightness) { m_config.brightness = brightness; }
void StatusLed::setEnablePlayerColor(const bool do_enable) { m_config.enable_player_color = do_enable; }

void StatusLed::setInputState(const Utils::InputState &input_state) { m_input_state = input_state; }
void StatusLed::setPlayerColor(const Config::Color &color) { m_player_color = color; }

bool StatusLed::isActive() const { return m_active; }

void StatusLed::update() {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    float dt = (now - m_last_update_time) / 1000.0f;
    m_last_update_time = now;

    // Prevent huge jumps if update is paused
    if (dt > 0.1f) dt = 0.1f;

    const float brightness_factor = (float)m_config.brightness / (float)UINT8_MAX;
    
    // 1. Clear State (Ripples are the source of truth)
    std::fill(m_led_states.begin(), m_led_states.end(), ColorFloat{0.0f, 0.0f, 0.0f});

    // 2. Spawn Ripples
    auto spawn_ripple = [&](const Config::Color &c, float origin) {
        if (m_config.led_count < 2) {
            // Fallback for single LED: just set state directly
            m_led_states[0].r += c.r;
            m_led_states[0].g += c.g;
            m_led_states[0].b += c.b;
        } else {
            m_ripples.push_back({origin, 0.0f, c});
        }
    };

    float left_origin = (float)m_config.led_count * 0.25f;
    float right_origin = (float)m_config.led_count * 0.75f;

    if (m_input_state.drum.don_left.triggered && !m_previous_input_state.drum.don_left.triggered) {
        spawn_ripple(m_config.don_left_color, left_origin);
    } else if (m_input_state.drum.ka_left.triggered && !m_previous_input_state.drum.ka_left.triggered) {
        spawn_ripple(m_config.ka_left_color, left_origin);
    }

    if (m_input_state.drum.don_right.triggered && !m_previous_input_state.drum.don_right.triggered) {
        spawn_ripple(m_config.don_right_color, right_origin);
    } else if (m_input_state.drum.ka_right.triggered && !m_previous_input_state.drum.ka_right.triggered) {
        spawn_ripple(m_config.ka_right_color, right_origin);
    }

    m_previous_input_state = m_input_state;

    // 3. Update & Render Ripples
    const float MAX_DIST = (float)m_config.led_count / 2.0f;
    const float RIPPLE_RADIUS = 5.0f;     // Radius of the light point in LEDs
    const float MIN_SPEED = 40.0f;        // Minimum speed to ensure it finishes
    const float SPEED_DECAY_FACTOR = 6.0f; // Speed = Remaining_Dist * Factor

    for (auto it = m_ripples.begin(); it != m_ripples.end();) {
        // Ease-Out Physics: Move faster when further from destination
        float remaining = MAX_DIST - it->distance;
        float speed = std::max(MIN_SPEED, remaining * SPEED_DECAY_FACTOR);
        
        it->distance += speed * dt;

        if (it->distance >= MAX_DIST) {
            it = m_ripples.erase(it);
            continue;
        }

        // Render Particle Function (Soft Glow)
        auto render_particle = [&](float center, const Config::Color &col) {
             // Iterate over the integer footprint of the particle
             int start_idx = (int)floor(center - RIPPLE_RADIUS);
             int end_idx = (int)ceil(center + RIPPLE_RADIUS);

             for (int i = start_idx; i <= end_idx; ++i) {
                 float dist = std::abs((float)i - center);
                 if (dist >= RIPPLE_RADIUS) continue;

                 // Quadratic Falloff: (1 - d/r)^2 for smooth edges
                 float intensity = 1.0f - (dist / RIPPLE_RADIUS);
                 intensity *= intensity; 

                 // Handle wrapping
                 int led_idx = i;
                 while (led_idx < 0) led_idx += m_config.led_count;
                 while (led_idx >= (int)m_config.led_count) led_idx -= m_config.led_count;

                 m_led_states[led_idx].r += col.r * intensity;
                 m_led_states[led_idx].g += col.g * intensity;
                 m_led_states[led_idx].b += col.b * intensity;
             }
        };

        // Render two heads moving in opposite directions
        render_particle(it->origin + it->distance, it->color);
        render_particle(it->origin - it->distance, it->color);

        ++it;
    }

    // 4. Composite Output
    Config::Color idle = m_config.enable_player_color ? m_player_color.value_or(m_config.idle_color) : m_config.idle_color;
    std::vector<Config::Color> frame_colors(m_config.led_count);

    for (size_t i = 0; i < m_config.led_count; ++i) {
        const auto &state = m_led_states[i];
        
        // Calculate intensity of the active effect to fade out the idle color
        // Using max component as a rough brightness estimate for saturation
        float state_max = std::max({state.r, state.g, state.b});
        float active_intensity = std::min(1.0f, state_max / 255.0f);
        float idle_weight = 1.0f - active_intensity;

        // Composite: State + (Idle * Weight)
        // We allow state to go > 255 internally (HDR-ish), but clamp at output.
        float r = state.r + (float)idle.r * idle_weight;
        float g = state.g + (float)idle.g * idle_weight;
        float b = state.b + (float)idle.b * idle_weight;

        // Apply global brightness
        frame_colors[i].r = static_cast<uint8_t>(std::min(255.0f, r * brightness_factor));
        frame_colors[i].g = static_cast<uint8_t>(std::min(255.0f, g * brightness_factor));
        frame_colors[i].b = static_cast<uint8_t>(std::min(255.0f, b * brightness_factor));
    }

    // 5. Current Limiting
    uint32_t total_ma = 0;
    // Estimated: 60mA per LED at full white (255, 255, 255)
    // Formula: sum((r+g+b) / 765 * 60)
    for (const auto &c : frame_colors) {
        total_ma += (c.r + c.g + c.b) * 60 / 765; 
    }

    float scale = 1.0f;
    if (total_ma > m_config.max_current_ma && total_ma > 0) {
        scale = (float)m_config.max_current_ma / (float)total_ma;
    }

    // 6. Convert to WS2812 pixel format
    for (size_t i = 0; i < m_config.led_count; ++i) {
        uint8_t r = static_cast<uint8_t>((float)frame_colors[i].r * scale);
        uint8_t g = static_cast<uint8_t>((float)frame_colors[i].g * scale);
        uint8_t b = static_cast<uint8_t>((float)frame_colors[i].b * scale);

        size_t index = m_config.reversed ? (m_config.led_count - 1 - i) : i;
        m_leds[index] = ws2812_rgb_to_gamma_corrected_u32pixel(r, g, b);
    }

    // 7. Pin-sharing state machine: switch LED data pin between PIO (driving LEDs)
    //    and SIO (button input) based on whether ripples are active.
    bool has_ripples = !m_ripples.empty();

    if (has_ripples && !m_active) {
        // Transition IDLE -> ACTIVE: claim pin for PIO
        pio_gpio_init(pio0, m_config.led_pin);
        m_active = true;
    }

    if (m_active) {
        // Send frame while active
        ws2812_put_frame(pio0, m_leds.data(), m_leds.size());

        if (!has_ripples) {
            // Transition ACTIVE -> IDLE: ripples finished, final "off" frame was just sent.
            // Wait for PIO transmission to complete before releasing the pin.
            while (!pio_sm_is_tx_fifo_empty(pio0, 0)) {
            }
            sleep_us(350); // Last pixel shift-out (~30us) + WS2812 reset/latch (~300us)

            // Release pin back to SIO for button use
            gpio_set_function(m_config.led_pin, GPIO_FUNC_SIO);
            gpio_set_dir(m_config.led_pin, GPIO_IN);
            gpio_pull_up(m_config.led_pin);
            m_active = false;
        }
    }
}

} // namespace Doncon::Peripherals