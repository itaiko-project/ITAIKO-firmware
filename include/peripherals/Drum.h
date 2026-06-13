#ifndef PERIPHERALS_DRUM_H_
#define PERIPHERALS_DRUM_H_

#include "utils/InputState.h"

#include "hardware/spi.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <variant>

namespace Doncon::Peripherals {

class Drum {
  public:
    struct Config {
        struct __attribute((packed, aligned(1))) Thresholds {
            uint16_t don_left;
            uint16_t ka_left;
            uint16_t don_right;
            uint16_t ka_right;
        };

        struct AdcChannels {
            uint8_t don_left;
            uint8_t ka_left;
            uint8_t don_right;
            uint8_t ka_right;
        };

        struct InternalAdc {
            uint8_t sample_count;
        };

        struct ExternalAdc {
            spi_inst_t *spi_block;
            uint spi_speed_hz;
            uint8_t spi_mosi_pin;
            uint8_t spi_miso_pin;
            uint8_t spi_sclk_pin;
            uint8_t spi_scsn_pin;
            //uint8_t spi_level_shifter_enable_pin;
        };

        enum class DoubleTriggerMode : uint8_t {
            Off,
            Threshold,
        };

        enum class WeightedComparisonMode : uint8_t {
            Off,
            On,
        };

        Thresholds trigger_thresholds;

        DoubleTriggerMode double_trigger_mode;
        Thresholds double_trigger_thresholds;

        Thresholds cutoff_thresholds;

        WeightedComparisonMode weighted_comparison_mode;

        uint16_t debounce_delay_ms;
        uint16_t don_debounce;
        uint16_t kat_debounce;
        uint16_t crosstalk_debounce;

        uint16_t key_timeout_ms;

        uint32_t roll_counter_timeout_ms;

        AdcChannels adc_channels;
        std::variant<InternalAdc, ExternalAdc> adc_config;


  };

  private:
    enum class Id : uint8_t {
        DON_LEFT,
        KA_LEFT,
        DON_RIGHT,
        KA_RIGHT,
    };

    class Pad {
      private:
        struct analog_buffer_entry {
            uint16_t value;
            uint32_t timestamp;
        };

        uint8_t m_channel;
        uint32_t m_last_change{0};
        uint32_t m_last_trigger{0};
        int32_t m_last_adc_value{0};

        bool m_active{false};
        // Edge tracker: was the delta above the light threshold last frame? A new strike
        // is only the rising edge (false -> true), so the multi-frame rising slope of one
        // (hard) hit can't be counted as several hits.
        bool m_prev_above{false};
        // One-deep buffered re-hit: a fast same-pad hit blocked only by this pad's own
        // hold/lockout is queued here and replayed once the release gap elapses.
        bool m_pending{false};
        uint32_t m_pending_fire{0};
        std::deque<analog_buffer_entry> m_analog_buffer;

      public:
        Pad(uint8_t channel);

        [[nodiscard]] int32_t getLastAdcValue() const { return m_last_adc_value; };
        [[nodiscard]] uint8_t getChannel() const { return m_channel; };
        [[nodiscard]] bool getState() const { return m_active; };
        [[nodiscard]] uint32_t getLastChange() const { return m_last_change; };
        [[nodiscard]] uint32_t getLastTrigger() const { return m_last_trigger; };
        [[nodiscard]] uint32_t getTriggerDuration() const;

        void setLastTrigger(uint32_t value) { m_last_trigger = value; };
        void setLastAdcValue(int32_t value) { m_last_adc_value = value; };
        void setChannel(uint8_t channel) { m_channel = channel; };
        void setState(bool state, uint16_t debounce_delay);
        void trigger();
        void updateTimeout(uint16_t key_timeout);
        uint16_t getAnalog();
        void setAnalog(uint16_t value, uint16_t debounce_delay);

        [[nodiscard]] bool wasAbove() const { return m_prev_above; };
        void setWasAbove(bool above) { m_prev_above = above; };

        [[nodiscard]] bool hasPending() const { return m_pending; };
        [[nodiscard]] uint32_t getPendingFire() const { return m_pending_fire; };
        void setPending(uint32_t fire_time) {
            m_pending = true;
            m_pending_fire = fire_time;
        };
        void clearPending() { m_pending = false; };
    };

    class RollCounter {
      private:
        uint32_t m_timeout_ms;

        uint32_t m_last_hit_time{0};
        uint16_t m_current_roll{0};
        uint16_t m_previous_roll{0};

        struct {
            bool don_left;
            bool ka_left;
            bool don_right;
            bool ka_right;
        } m_previous_pad_state{};

      public:
        RollCounter(uint32_t timeout_ms);
        void update(Utils::InputState &input_state);

        [[nodiscard]] uint16_t getCurrentRoll() const { return m_current_roll; };
        [[nodiscard]] uint16_t getPreviousRoll() const { return m_previous_roll; };
    };

    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): Class has no members
    class AdcInterface {
      public:
        virtual ~AdcInterface() = default;

        // Those are expected to be 12bit values
        virtual std::array<uint16_t, 4> read() = 0;
    };

    class InternalAdc : public AdcInterface {
      private:
        Config::InternalAdc m_config;

      public:
        InternalAdc(const Config::InternalAdc &config);
        std::array<uint16_t, 4> read() final;
    };

    class ExternalAdc : public AdcInterface {
      public:
        ExternalAdc(const Config::ExternalAdc &config);
        std::array<uint16_t, 4> read() final;
    };

    Config m_config;
    std::unique_ptr<AdcInterface> m_adc;
    std::map<Id, Pad> m_pads;
    RollCounter m_roll_counter;
    uint32_t m_global_debounce_time{0};
    bool m_buffered_input{false}; // 連打 buffered re-emission (fast-roll recovery)

    uint32_t last_don_time;
    uint32_t last_kat_time;

    struct WeightedComparisonState {
        bool don_left_candidate = false;
        bool don_right_candidate = false;
        bool ka_left_candidate = false;
        bool ka_right_candidate = false;

        float don_left_ratio = 0.0f;
        float don_right_ratio = 0.0f;
        float ka_left_ratio = 0.0f;
        float ka_right_ratio = 0.0f;
    };

    void updateDigitalInputState(Utils::InputState &input_state, const std::map<Id, int32_t> &raw_values);
    void updateAnalogInputState(Utils::InputState &input_state, const std::map<Id, int32_t> &raw_values);
    std::map<Id, int32_t> readInputs();
    bool isGlobalDebounceElapsed() const;
    void updateGlobalDebounce();
    uint16_t getThreshold(Id pad_id, const Config::Thresholds &thresholds) const;
    float calculateTriggerRatio(int32_t delta, uint16_t threshold) const;
    void applyWeightedComparison(WeightedComparisonState& state);

  public:
    Drum(const Config &config);

    void updateInputState(Utils::InputState &input_state);

    void setDebounceDelay(uint16_t delay);
    void setDonDebounceMs(uint16_t ms);
    void setKatDebounceMs(uint16_t ms);
    void setCrosstalkDebounceMs(uint16_t ms);
    void setKeyTimeoutMs(uint16_t ms);
    void setBufferedInput(bool enabled);
    void setTriggerThresholds(const Config::Thresholds &thresholds);
    void setDoubleTriggerMode(Config::DoubleTriggerMode mode);
    void setDoubleThresholds(const Config::Thresholds &thresholds);
    void setCutoffThresholds(const Config::Thresholds &thresholds);
    void setWeightedComparisonMode(Config::WeightedComparisonMode mode);
    void setAdcChannels(const Config::AdcChannels &channels);
};

} // namespace Doncon::Peripherals

#endif // PERIPHERALS_DRUM_H_