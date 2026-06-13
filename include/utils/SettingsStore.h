#ifndef UTILS_SETTINGSSTORE_H_
#define UTILS_SETTINGSSTORE_H_

#include "peripherals/Drum.h"
#include "usb/device_driver.h"
#include "utils/KeyboardMappings.h"

#include "hardware/flash.h"

#include <array>
#include <string>

namespace Doncon::Utils {

class SettingsStore {
  private:
    const static uint32_t m_flash_size = FLASH_SECTOR_SIZE;
    const static uint32_t m_flash_offset = PICO_FLASH_SIZE_BYTES - m_flash_size;
    const static uint32_t m_store_size = FLASH_PAGE_SIZE;
    const static uint32_t m_store_pages = m_flash_size / m_store_size;
    const static uint8_t m_magic_byte = 0x39;
    const static uint32_t m_auth_flash_size = FLASH_SECTOR_SIZE;
    const static uint32_t m_auth_flash_offset = m_flash_offset - m_auth_flash_size;
    const static uint32_t m_auth_key_max_size = 3584;
    const static uint32_t m_auth_magic = 0x53345041; // "AP4S"
    const static uint16_t m_auth_version = 1;

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
                uint8_t ps3_mac[6];
                uint8_t roll_boost_ms;  // 連打增速 window (ms), 0 = off. Appended -> old saves read 0.
                uint8_t buffered_input; // 1 = buffer & replay fast same-pad re-hits (roll recovery)
            };
            uint8_t raw[m_store_size];
        };
    };
    static_assert(sizeof(Storecache) == m_store_size);

    struct __attribute((packed, aligned(1))) AuthStorecache {
        union {
            struct __attribute((packed)) {
                uint32_t magic;
                uint16_t version;
                uint16_t key_len;
                uint8_t serial[16];
                uint8_t signature[256];
                char key_pem[m_auth_key_max_size];
                uint32_t crc32;
                uint8_t reserved[m_auth_flash_size - (4 + 2 + 2 + 16 + 256 + m_auth_key_max_size + 4)];
            };
            uint8_t raw[m_auth_flash_size];
        };
    };
    static_assert(sizeof(AuthStorecache) == m_auth_flash_size);

    enum class RebootType : uint8_t {
        None,
        Normal,
        Bootsel,
    };

    Storecache m_store_cache;
    AuthStorecache m_auth_store_cache{};
    bool m_dirty{true};
    bool m_auth_dirty{false};
    RebootType m_scheduled_reboot{RebootType::None};

    Storecache read();
    static bool isAuthValid(const AuthStorecache &cache);

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

    void setPs3Mac(const uint8_t mac[6]);
    void getPs3Mac(uint8_t mac[6]) const;
    [[nodiscard]] bool hasPs3Mac() const;

    void setRollBoostMs(uint16_t ms);
    [[nodiscard]] uint16_t getRollBoostMs() const;

    void setBufferedInput(bool enabled);
    [[nodiscard]] bool getBufferedInput() const;

    void scheduleReboot(bool bootsel = false);

    bool setPS4AuthCredentials(const uint8_t serial[16], const uint8_t signature[256], const char *key_pem,
                               size_t key_pem_len);
    bool getPS4AuthCredentials(std::array<uint8_t, 16> &serial, std::array<uint8_t, 256> &signature,
                               std::string &key_pem) const;
    [[nodiscard]] bool hasPS4AuthCredentials() const;
    void clearPS4AuthCredentials();

    void store();
    void reset();
};
} // namespace Doncon::Utils

#endif // UTILS_SETTINGSSTORE_H_