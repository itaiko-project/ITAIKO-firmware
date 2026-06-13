#include "peripherals/Drum.h"

#include "hardware/adc.h"
#include "pico/time.h"
#include <mcp3204/Mcp3204Dma.h>

#include <algorithm>
#include <cmath>

namespace Doncon::Peripherals {

Drum::InternalAdc::InternalAdc(const Config::InternalAdc &config) : m_config(config) {
    static const uint adc_base_pin = 26;

    for (uint pin = adc_base_pin; pin < adc_base_pin + 4; ++pin) {
        adc_gpio_init(pin);
    }

    adc_init();
}

std::array<uint16_t, 4> Drum::InternalAdc::read() {
    if (m_config.sample_count == 0) {
        return {};
    }

    std::array<uint16_t, 4> result{};

    //Oversample ADC inputs to get rid of ADC noise
    std::array<uint32_t, 4> values{};

    // for (int i = 0; i < 4; i++) {
    //     adc_select_input(i);
    //     result.at(i) = adc_read();
    // }

    for (uint8_t sample_number = 0; sample_number < m_config.sample_count; ++sample_number) {
        for (size_t idx = 0; idx < values.size(); ++idx) {
            adc_select_input(idx);
            values.at(idx) += adc_read();
        }
    }

    // Take average of all samples
    std::ranges::transform(values, result.begin(), [&](const auto &sample) { return sample / m_config.sample_count;
    });

    return result;
}

Drum::ExternalAdc::ExternalAdc(const Config::ExternalAdc &config) {
    // Enable level shifter
    // gpio_init(config.spi_level_shifter_enable_pin);
    // gpio_set_dir(config.spi_level_shifter_enable_pin, (bool)GPIO_OUT);
    // gpio_put(config.spi_level_shifter_enable_pin, true);

    // Set up SPI
    gpio_set_function(config.spi_miso_pin, GPIO_FUNC_SPI);
    gpio_set_function(config.spi_mosi_pin, GPIO_FUNC_SPI);
    gpio_set_function(config.spi_sclk_pin, GPIO_FUNC_SPI);
    spi_init(config.spi_block, config.spi_speed_hz);

    gpio_init(config.spi_scsn_pin);
    gpio_set_dir(config.spi_scsn_pin, (bool)GPIO_OUT);

    Mcp3204Dma::run(config.spi_block, config.spi_scsn_pin);
}

std::array<uint16_t, 4> Drum::ExternalAdc::read() { return Mcp3204Dma::take_maximums(); }

Drum::Pad::Pad(const uint8_t channel) : m_channel(channel) {}

void Drum::Pad::setState(const bool state, const uint16_t debounce_delay) {
    if (m_active == state) {
        return;
    }

    // Immediately change the input state, but only allow a change every debounce_delay milliseconds.
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (m_last_change + debounce_delay <= now) {
        m_active = state;
        m_last_change = now;
    }
}

void Drum::Pad::trigger() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    // // Respect per-pad debounce
    // if (m_last_change + key_timeout > now) {
    //     return; // Too soon since last state change
    // }
    setLastTrigger(now);

    m_active = true;
    m_last_trigger = now;
    m_last_change = now;
}

void Drum::Pad::updateTimeout(const uint16_t key_timeout) {

    if (!m_active) {
        return; // Not currently pressed
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());

    // Check if key timeout has expired
    if (now - m_last_trigger > key_timeout) {
        m_active = false;
        m_last_change = now;
    }
}

uint32_t Drum::Pad::getTriggerDuration() const {
    if (!m_active) {
        return 0;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    return now - m_last_trigger;
}

uint16_t Drum::Pad::getAnalog() {
    const auto raw_to_uint16 = [](uint16_t raw) { return ((raw << 4) & 0xFFF0) | ((raw >> 8) & 0x000F); };

    return raw_to_uint16(std::ranges::max_element(m_analog_buffer, [](const auto &a, const auto &b) {
                             return a.value < b.value;
                         })->value);
}

void Drum::Pad::setAnalog(uint16_t value, uint16_t debounce_delay) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    // Clear outdated values, i.e. anything older than debounce_delay to allow for convenient configuration.
    while (!m_analog_buffer.empty() && (m_analog_buffer.front().timestamp + debounce_delay) <= now) {
        m_analog_buffer.pop_front();
    }

    m_analog_buffer.push_back({value, now});
}

Drum::RollCounter::RollCounter(uint32_t timeout_ms) : m_timeout_ms(timeout_ms) {};

void Drum::RollCounter::update(Utils::InputState &input_state) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((now - m_last_hit_time) > m_timeout_ms) {
        if (m_current_roll > 1) {
            m_previous_roll = m_current_roll;
        }
        m_current_roll = 0;
    }

    if (input_state.drum.don_left.triggered && (m_previous_pad_state.don_left != input_state.drum.don_left.triggered)) {
        m_last_hit_time = now;
        m_current_roll++;
    }
    if (input_state.drum.don_right.triggered &&
        (m_previous_pad_state.don_right != input_state.drum.don_right.triggered)) {
        m_last_hit_time = now;
        m_current_roll++;
    }
    if (input_state.drum.ka_right.triggered && (m_previous_pad_state.ka_right != input_state.drum.ka_right.triggered)) {
        m_last_hit_time = now;
        m_current_roll++;
    }
    if (input_state.drum.ka_left.triggered && (m_previous_pad_state.ka_left != input_state.drum.ka_left.triggered)) {
        m_last_hit_time = now;
        m_current_roll++;
    }

    m_previous_pad_state.don_left = input_state.drum.don_left.triggered;
    m_previous_pad_state.don_right = input_state.drum.don_right.triggered;
    m_previous_pad_state.ka_left = input_state.drum.ka_left.triggered;
    m_previous_pad_state.ka_right = input_state.drum.ka_right.triggered;

    input_state.drum.current_roll = m_current_roll;
    input_state.drum.previous_roll = m_previous_roll;
}

Drum::Drum(const Config &config) : m_config(config), m_roll_counter(config.roll_counter_timeout_ms) {

    std::visit(
        [this](auto &&config) {
            using T = std::decay_t<decltype(config)>;

            if constexpr (std::is_same_v<T, Config::InternalAdc>) {
                m_adc = std::make_unique<InternalAdc>(config);
            } else if constexpr (std::is_same_v<T, Config::ExternalAdc>) {
                m_adc = std::make_unique<ExternalAdc>(config);
            } else {
                static_assert(!sizeof(T), "Unknown ADC type!");
            }
        },
        m_config.adc_config);

    m_pads.emplace(Id::DON_RIGHT, config.adc_channels.don_right);
    m_pads.emplace(Id::DON_LEFT, config.adc_channels.don_left);
    m_pads.emplace(Id::KA_LEFT, config.adc_channels.ka_left);
    m_pads.emplace(Id::KA_RIGHT, config.adc_channels.ka_right);
}

std::map<Drum::Id, int32_t> Drum::readInputs() {
    std::map<Id, int32_t> result;

    const auto adc_values = m_adc->read();

    for (const auto &[id, pad] : m_pads) {
        result[id] = static_cast<int32_t>(adc_values.at(pad.getChannel()));
    }

    return result;
}

bool Drum::isGlobalDebounceElapsed() const {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    return (now - m_global_debounce_time) >= m_config.don_debounce;
}

void Drum::updateGlobalDebounce() { m_global_debounce_time = to_ms_since_boot(get_absolute_time()); }

uint16_t Drum::getThreshold(const Id pad_id, const Config::Thresholds &thresholds) const {
    switch (pad_id) {
    case Id::DON_LEFT:
        return thresholds.don_left;
    case Id::DON_RIGHT:
        return thresholds.don_right;
    case Id::KA_LEFT:
        return thresholds.ka_left;
    case Id::KA_RIGHT:
        return thresholds.ka_right;
    }
    assert(false);
    return 0;
}

// Gap (ms) inserted after a buffered re-hit's hold/lockout before it is replayed,
// so the host sees a clean release edge between paced hits.
static constexpr uint16_t kBufferDelayMs = 5;

void Drum::updateDigitalInputState(Utils::InputState &input_state, const std::map<Drum::Id, int32_t> &raw_values) {
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    // PHASE 0: Maintain existing button states (key timeout logic). Snapshot the
    // pressed state BEFORE the timeout so PHASE 0b can tell whether a pad was already
    // released coming into this frame.
    std::array<bool, 4> was_pressed{};
    for (auto &[id, pad] : m_pads) {
        was_pressed.at(static_cast<size_t>(id)) = pad.getState();
        pad.updateTimeout(m_config.debounce_delay_ms);
    }

    // PHASE 0b: Replay buffered re-hits whose release gap has elapsed.
    // A fast same-pad hit that arrived while the pad was still held / within its own
    // re-trigger lockout was queued (instead of dropped) so the game still registers
    // every hit during a roll. Cross-key / same-type echoes are never buffered.
    //
    // Only fire in a frame that STARTED with the pad released (was_pressed == false):
    // that means the previous frame's report already carried the release, so the host
    // sees a distinct press. Releasing and re-pressing within one frame would hide the
    // release and merge consecutive hits into one long hold (slow-loop frames).
    for (auto &[id, pad] : m_pads) {
        if (pad.hasPending() && now >= pad.getPendingFire() && !was_pressed.at(static_cast<size_t>(id))) {
            pad.trigger();
            pad.clearPending();
            if (id == Id::DON_LEFT || id == Id::DON_RIGHT) {
                last_don_time = now;
            } else {
                last_kat_time = now;
            }
        }
    }

    // Initialize candidate tracking
    WeightedComparisonState wc_state{};
    bool don_left_hard = false;
    bool don_right_hard = false;
    bool ka_left_hard = false;
    bool ka_right_hard = false;

    // PHASE 1: Collect all candidates
    for (const auto &id : {Id::DON_LEFT, Id::DON_RIGHT, Id::KA_LEFT, Id::KA_RIGHT}) {
        auto &pad = m_pads.at(id);

        const int32_t adc_value = raw_values.at(id);
        const int32_t light_threshold = static_cast<int32_t>(getThreshold(id, m_config.trigger_thresholds));
        const int32_t cutoff_threshold = static_cast<int32_t>(getThreshold(id, m_config.cutoff_thresholds));
        const int32_t last_adc_value = pad.getLastAdcValue();
        const int32_t delta = adc_value - last_adc_value;

        pad.setLastAdcValue(adc_value);

        // Cutoff check: if raw value exceeds cutoff, ignore but block aftershocks
        if (adc_value > cutoff_threshold) {
            pad.setLastTrigger(now);
            pad.setWasAbove(false);
            continue;
        }

        // A strike is the RISING EDGE of the threshold crossing (was below, now above).
        // The continuation frames of one spike (its multi-sample rising slope) stay
        // "above" and are ignored, so a single hard hit can't register as several hits.
        const bool above = (delta > light_threshold);
        const bool rising_edge = above && !pad.wasAbove();
        pad.setWasAbove(above);

        if (!rising_edge) {
            continue;
        }

        // Determine if it's a hard hit
        const bool is_hard_hit =
            m_config.double_trigger_mode != Config::DoubleTriggerMode::Off &&
            delta > static_cast<int32_t>(getThreshold(id, m_config.double_trigger_thresholds));

        // Type-specific debounce and crosstalk checks. These reject ghost / echo hits
        // and are NEVER buffered (replaying crosstalk would create phantom notes).
        const bool is_don = (id == Id::DON_LEFT || id == Id::DON_RIGHT);
        if (is_don) {
            // Crosstalk debounce (never bypassed)
            if (now - last_kat_time <= m_config.crosstalk_debounce) {
                continue;
            }
            // Same-type debounce (bypassed by hard hit)
            if (now - last_don_time < m_config.don_debounce && !is_hard_hit) {
                continue;
            }
        } else {
            // Crosstalk debounce (never bypassed)
            if (now - last_don_time <= m_config.crosstalk_debounce) {
                continue;
            }
            // Same-type debounce (bypassed by hard hit)
            if (now - last_kat_time < m_config.kat_debounce && !is_hard_hit) {
                continue;
            }
        }

        // Self gate: pad still held, within its own re-trigger lockout, or only just
        // released THIS frame. The last case matters because triggering now would set
        // `triggered` back to true in the same loop iteration that released it, so the
        // release report would never be sent and consecutive hits would merge into one
        // long hold. Deferring keeps an OFF report between presses. Previously these hits
        // were dropped (lost roll inputs); with Buffered Input on they are queued and
        // replayed once the release has been reported, otherwise dropped as before.
        const uint32_t time_since_trigger = now - pad.getLastTrigger();
        const bool self_blocked = pad.getState() || was_pressed.at(static_cast<size_t>(id)) ||
                                  (time_since_trigger <= m_config.key_timeout_ms);
        if (self_blocked) {
            if (m_buffered_input && !pad.hasPending()) {
                const uint16_t gap =
                    m_config.debounce_delay_ms > m_config.key_timeout_ms ? m_config.debounce_delay_ms : m_config.key_timeout_ms;
                pad.setPending(pad.getLastTrigger() + gap + kBufferDelayMs);
            }
            continue;
        }

        // Calculate ratio and mark as candidate
        const float ratio = calculateTriggerRatio(delta, light_threshold);

        switch (id) {
        case Id::DON_LEFT:
            wc_state.don_left_candidate = true;
            wc_state.don_left_ratio = ratio;
            don_left_hard = is_hard_hit;
            break;
        case Id::DON_RIGHT:
            wc_state.don_right_candidate = true;
            wc_state.don_right_ratio = ratio;
            don_right_hard = is_hard_hit;
            break;
        case Id::KA_LEFT:
            wc_state.ka_left_candidate = true;
            wc_state.ka_left_ratio = ratio;
            ka_left_hard = is_hard_hit;
            break;
        case Id::KA_RIGHT:
            wc_state.ka_right_candidate = true;
            wc_state.ka_right_ratio = ratio;
            ka_right_hard = is_hard_hit;
            break;
        }
    }

    // PHASE 2: Determine winning type based on highest ratio
    float overall_max = 0.0f;
    bool winning_type_is_don = true;

    if (wc_state.don_left_candidate && wc_state.don_left_ratio > overall_max) {
        overall_max = wc_state.don_left_ratio;
        winning_type_is_don = true;
    }
    if (wc_state.don_right_candidate && wc_state.don_right_ratio > overall_max) {
        overall_max = wc_state.don_right_ratio;
        winning_type_is_don = true;
    }
    if (wc_state.ka_left_candidate && wc_state.ka_left_ratio > overall_max) {
        overall_max = wc_state.ka_left_ratio;
        winning_type_is_don = false;
    }
    if (wc_state.ka_right_candidate && wc_state.ka_right_ratio > overall_max) {
        overall_max = wc_state.ka_right_ratio;
        winning_type_is_don = false;
    }

    // Discard candidates of losing type
    if (winning_type_is_don) {
        wc_state.ka_left_candidate = false;
        wc_state.ka_right_candidate = false;
    } else {
        wc_state.don_left_candidate = false;
        wc_state.don_right_candidate = false;
    }

    // PHASE 3: Weighted comparison within winning type (with double-trigger support)
    if (wc_state.don_left_candidate && wc_state.don_right_candidate) {
        if (!(don_left_hard && don_right_hard)) {
            // Not both hard hits - higher ratio wins
            if (wc_state.don_left_ratio > wc_state.don_right_ratio) {
                wc_state.don_right_candidate = false;
            } else {
                wc_state.don_left_candidate = false;
            }
        }
        // Both hard hits: keep both candidates for double trigger
    }

    if (wc_state.ka_left_candidate && wc_state.ka_right_candidate) {
        if (!(ka_left_hard && ka_right_hard)) {
            // Not both hard hits - higher ratio wins
            if (wc_state.ka_left_ratio > wc_state.ka_right_ratio) {
                wc_state.ka_right_candidate = false;
            } else {
                wc_state.ka_left_candidate = false;
            }
        }
        // Both hard hits: keep both candidates for double trigger
    }

    // PHASE 4: Trigger surviving candidates
    if (wc_state.don_left_candidate) {
        m_pads.at(Id::DON_LEFT).trigger();
        last_don_time = now;
    }
    if (wc_state.don_right_candidate) {
        m_pads.at(Id::DON_RIGHT).trigger();
        last_don_time = now;
    }
    if (wc_state.ka_left_candidate) {
        m_pads.at(Id::KA_LEFT).trigger();
        last_kat_time = now;
    }
    if (wc_state.ka_right_candidate) {
        m_pads.at(Id::KA_RIGHT).trigger();
        last_kat_time = now;
    }

    // PHASE 5: Output to InputState
    input_state.drum.don_left.triggered = m_pads.at(Id::DON_LEFT).getState();
    input_state.drum.don_left.duration_ms = m_pads.at(Id::DON_LEFT).getTriggerDuration();
    input_state.drum.ka_left.triggered = m_pads.at(Id::KA_LEFT).getState();
    input_state.drum.ka_left.duration_ms = m_pads.at(Id::KA_LEFT).getTriggerDuration();
    input_state.drum.don_right.triggered = m_pads.at(Id::DON_RIGHT).getState();
    input_state.drum.don_right.duration_ms = m_pads.at(Id::DON_RIGHT).getTriggerDuration();
    input_state.drum.ka_right.triggered = m_pads.at(Id::KA_RIGHT).getState();
    input_state.drum.ka_right.duration_ms = m_pads.at(Id::KA_RIGHT).getTriggerDuration();

    // PHASE 6: Update roll counter
    m_roll_counter.update(input_state);
}

void Drum::updateAnalogInputState(Utils::InputState &input_state, const std::map<Drum::Id, int32_t> &raw_values) {
    for (const auto &[id, raw] : raw_values) {
        m_pads.at(id).setAnalog(static_cast<uint16_t>(raw), m_config.debounce_delay_ms);

        switch (id) {
        case Id::DON_LEFT:
            input_state.drum.don_left.analog = m_pads.at(id).getAnalog();
            break;
        case Id::DON_RIGHT:
            input_state.drum.don_right.analog = m_pads.at(id).getAnalog();
            break;
        case Id::KA_LEFT:
            input_state.drum.ka_left.analog = m_pads.at(id).getAnalog();
            break;
        case Id::KA_RIGHT:
            input_state.drum.ka_right.analog = m_pads.at(id).getAnalog();
            break;
        }
    };
}

void Drum::updateInputState(Utils::InputState &input_state) {
    const auto raw_values = readInputs();

    input_state.drum.don_left.raw = static_cast<uint16_t>(raw_values.at(Id::DON_LEFT));
    input_state.drum.don_right.raw = static_cast<uint16_t>(raw_values.at(Id::DON_RIGHT));
    input_state.drum.ka_left.raw = static_cast<uint16_t>(raw_values.at(Id::KA_LEFT));
    input_state.drum.ka_right.raw = static_cast<uint16_t>(raw_values.at(Id::KA_RIGHT));

    updateDigitalInputState(input_state, raw_values);
    updateAnalogInputState(input_state, raw_values);
}

void Drum::setDebounceDelay(const uint16_t delay) { m_config.debounce_delay_ms = delay; }

void Drum::setDonDebounceMs(const uint16_t ms) { m_config.don_debounce = ms; }

void Drum::setKatDebounceMs(const uint16_t ms) { m_config.kat_debounce = ms; }

void Drum::setCrosstalkDebounceMs(const uint16_t ms) { m_config.crosstalk_debounce = ms; }

void Drum::setKeyTimeoutMs(const uint16_t ms) { m_config.key_timeout_ms = ms; }

void Drum::setBufferedInput(const bool enabled) {
    m_buffered_input = enabled;
    if (!enabled) {
        for (auto &[id, pad] : m_pads) {
            pad.clearPending();
        }
    }
}

void Drum::setTriggerThresholds(const Config::Thresholds &thresholds) { m_config.trigger_thresholds = thresholds; }

void Drum::setDoubleTriggerMode(const Config::DoubleTriggerMode mode) { m_config.double_trigger_mode = mode; }

void Drum::setDoubleThresholds(const Config::Thresholds &thresholds) {
    m_config.double_trigger_thresholds = thresholds;
}

void Drum::setCutoffThresholds(const Config::Thresholds &thresholds) { m_config.cutoff_thresholds = thresholds; }

void Drum::setWeightedComparisonMode(const Config::WeightedComparisonMode mode) {
    m_config.weighted_comparison_mode = mode;
}

void Drum::setAdcChannels(const Config::AdcChannels &channels) {
    m_config.adc_channels = channels;
    m_pads.at(Id::DON_LEFT).setChannel(channels.don_left);
    m_pads.at(Id::DON_RIGHT).setChannel(channels.don_right);
    m_pads.at(Id::KA_LEFT).setChannel(channels.ka_left);
    m_pads.at(Id::KA_RIGHT).setChannel(channels.ka_right);
}

float Drum::calculateTriggerRatio(const int32_t delta, const uint16_t threshold) const {
    if (threshold == 0) {
        return 0.0f;
    }
    return static_cast<float>(delta) / static_cast<float>(threshold);
}

void Drum::applyWeightedComparison(WeightedComparisonState &state) {
    // constexpr float EPSILON = 0.01f; // 1% tie threshold

    // Compare Don pair (only if BOTH candidates)
    if (state.don_left_candidate && state.don_right_candidate) {
        // if (std::abs(state.don_left_ratio - state.don_right_ratio) > EPSILON) {
        if (state.don_left_ratio > state.don_right_ratio) {
            state.don_right_candidate = false;
        } else {
            state.don_left_candidate = false;
        }
        //}
        // If within epsilon, both remain candidates
    }

    // Compare Ka pair (only if BOTH candidates)
    if (state.ka_left_candidate && state.ka_right_candidate) {
        // if (std::abs(state.ka_left_ratio - state.ka_right_ratio) > EPSILON) {
        if (state.ka_left_ratio > state.ka_right_ratio) {
            state.ka_right_candidate = false;
        } else {
            state.ka_left_candidate = false;
        }
        //}
        // If within epsilon, both remain candidates
    }
}

} // namespace Doncon::Peripherals