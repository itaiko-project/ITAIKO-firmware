#ifndef UTILS_SETTINGSSTORE_H_
#define UTILS_SETTINGSSTORE_H_

#include "peripherals/Drum.h"
#include "usb/device_driver.h"
#include "utils/KeyboardMappings.h"

#include "hardware/flash.h"

#include <array>

namespace Doncon::Utils {

class SettingsStore {
  private:
    const static uint32_t m_flash_size = FLASH_SECTOR_SIZE;
    const static uint32_t m_flash_offset = PICO_FLASH_SIZE_BYTES - m_flash_size;
    const static uint32_t m_store_size = FLASH_PAGE_SIZE;
    const static uint32_t m_store_pages = m_flash_size / m_store_size;
    const static uint8_t m_magic_byte = 0x39;

    struct __attribute((packed, aligned(1))) Storecache {
        union {
            struct __attribute((packed)) {
                uint8_t in_use;
                usb_mode_t usb_mode;
                Peripherals::Drum::Config::Thresholds trigger_thresholds;
                uint8_t led_brightness;
                bool led_enable_player_color;
                uint16_t debounce_delay;
                Peripherals::Drum::Config::DoubleTriggerMode double_trigger_mode;
                Peripherals::Drum::Config::Thresholds double_trigger_thresholds;
                Peripherals::Drum::Config::Thresholds cutoff_thresholds;
                uint16_t don_debounce;
                uint16_t kat_debounce;
                uint16_t crosstalk_debounce;
                uint16_t key_timeout_ms;
                Peripherals::Drum::Config::WeightedComparisonMode weighted_comparison_mode;
                DrumKeys drum_keys_p1;
                DrumKeys drum_keys_p2;
                ControllerKeys controller_keys;
                Peripherals::Drum::Config::AdcChannels adc_channels;
            };
            uint8_t raw[m_store_size];
        };
    };
    static_assert(sizeof(Storecache) == m_store_size);

    enum class RebootType : uint8_t {
        None,
        Normal,
        Bootsel,
    };

    Storecache m_store_cache;
    bool m_dirty{true};
    RebootType m_scheduled_reboot{RebootType::None};

    Storecache read();

  public:
    SettingsStore();

    void setUsbMode(usb_mode_t mode);
    [[nodiscard]] usb_mode_t getUsbMode() const;

    void setTriggerThresholds(const Peripherals::Drum::Config::Thresholds &thresholds);
    [[nodiscard]] Peripherals::Drum::Config::Thresholds getTriggerThresholds() const;

    void setDoubleTriggerMode(const Peripherals::Drum::Config::DoubleTriggerMode &mode);
    [[nodiscard]] Peripherals::Drum::Config::DoubleTriggerMode getDoubleTriggerMode() const;

    void setDoubleTriggerThresholds(const Peripherals::Drum::Config::Thresholds &thresholds);
    [[nodiscard]] Peripherals::Drum::Config::Thresholds getDoubleTriggerThresholds() const;

    void setCutoffThresholds(const Peripherals::Drum::Config::Thresholds &thresholds);
    [[nodiscard]] Peripherals::Drum::Config::Thresholds getCutoffThresholds() const;

    void setLedBrightness(uint8_t brightness);
    [[nodiscard]] uint8_t getLedBrightness() const;

    void setLedEnablePlayerColor(bool do_enable);
    [[nodiscard]] bool getLedEnablePlayerColor() const;

    void setDebounceDelay(uint16_t delay);
    [[nodiscard]] uint16_t getDebounceDelay() const;

    void setDonDebounceMs(uint16_t ms);
    [[nodiscard]] uint16_t getDonDebounceMs() const;

    void setKatDebounceMs(uint16_t ms);
    [[nodiscard]] uint16_t getKatDebounceMs() const;

    void setCrosstalkDebounceMs(uint16_t ms);
    [[nodiscard]] uint16_t getCrosstalkDebounceMs() const;

    void setKeyTimeoutMs(uint16_t ms);
    [[nodiscard]] uint16_t getKeyTimeoutMs() const;

    void setWeightedComparisonMode(const Peripherals::Drum::Config::WeightedComparisonMode &mode);
    [[nodiscard]] Peripherals::Drum::Config::WeightedComparisonMode getWeightedComparisonMode() const;

    void setDrumKeysP1(const DrumKeys &keys);
    [[nodiscard]] DrumKeys getDrumKeysP1() const;

    void setDrumKeysP2(const DrumKeys &keys);
    [[nodiscard]] DrumKeys getDrumKeysP2() const;

    void setControllerKeys(const ControllerKeys &keys);
    [[nodiscard]] ControllerKeys getControllerKeys() const;

    void setAdcChannels(const Peripherals::Drum::Config::AdcChannels &channels);
    [[nodiscard]] Peripherals::Drum::Config::AdcChannels getAdcChannels() const;

    void scheduleReboot(bool bootsel = false);

    void store();
    void reset();
};
} // namespace Doncon::Utils

#endif // UTILS_SETTINGSSTORE_H_