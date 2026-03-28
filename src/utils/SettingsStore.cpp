#include "utils/SettingsStore.h"

#include "GlobalConfiguration.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"

namespace Doncon::Utils {

namespace {

uint8_t read_byte(uint32_t offset) {
    return *(reinterpret_cast<uint8_t *>(XIP_BASE + offset)); // NOLINT(performance-no-int-to-ptr)
}

} // namespace

SettingsStore::SettingsStore()
    : m_store_cache({.in_use = m_magic_byte,
                     .usb_mode = Config::Default::usb_mode,
                     .trigger_thresholds = Config::Default::drum_config.trigger_thresholds,
                     .led_brightness = Config::Default::led_config.brightness,
                     .led_enable_player_color = Config::Default::led_config.enable_player_color,
                     .debounce_delay = Config::Default::drum_config.debounce_delay_ms,
                     .double_trigger_mode = Config::Default::drum_config.double_trigger_mode,
                     .double_trigger_thresholds = Config::Default::drum_config.double_trigger_thresholds,
                     .cutoff_thresholds = Config::Default::drum_config.cutoff_thresholds,
                     .don_debounce = Config::Default::drum_config.don_debounce,
                     .kat_debounce = Config::Default::drum_config.kat_debounce,
                     .crosstalk_debounce = Config::Default::drum_config.crosstalk_debounce,
                     .key_timeout_ms = Config::Default::drum_config.key_timeout_ms,
                     .weighted_comparison_mode = Config::Default::drum_config.weighted_comparison_mode,
                     .drum_keys_p1 = Config::Default::drum_keys_p1,
                     .drum_keys_p2 = Config::Default::drum_keys_p2,
                     .controller_keys = Config::Default::controller_keys,
                     .adc_channels = Config::Default::drum_config.adc_channels}) {
    uint32_t current_page = m_flash_offset + m_flash_size - m_store_size;
    bool found_valid = false;
    for (size_t i = 0; i < m_store_pages; ++i) {
        if (read_byte(current_page) == m_magic_byte) {
            found_valid = true;
            break;
        }
        current_page -= m_store_size;
    }

    if (found_valid) {
        m_store_cache = *(reinterpret_cast<Storecache *>(XIP_BASE + current_page)); // NOLINT(performance-no-int-to-ptr)
        m_dirty = false;
    }
}


void SettingsStore::setUsbMode(const usb_mode_t mode) {
    if (mode != m_store_cache.usb_mode) {
        m_store_cache.usb_mode = mode;
        m_dirty = true;

        scheduleReboot();
    }
}

usb_mode_t SettingsStore::getUsbMode() const { return m_store_cache.usb_mode; }

void SettingsStore::setTriggerThresholds(const Peripherals::Drum::Config::Thresholds &thresholds) {
    if (m_store_cache.trigger_thresholds.don_left != thresholds.don_left ||
        m_store_cache.trigger_thresholds.don_right != thresholds.don_right ||
        m_store_cache.trigger_thresholds.ka_left != thresholds.ka_left ||
        m_store_cache.trigger_thresholds.ka_right != thresholds.ka_right) {

        m_store_cache.trigger_thresholds = thresholds;
        m_dirty = true;
    }
}
Peripherals::Drum::Config::Thresholds SettingsStore::getTriggerThresholds() const {
    return m_store_cache.trigger_thresholds;
}

void SettingsStore::setDoubleTriggerMode(const Peripherals::Drum::Config::DoubleTriggerMode &mode) {
    if (m_store_cache.double_trigger_mode != mode) {
        m_store_cache.double_trigger_mode = mode;
        m_dirty = true;
    }
}
Peripherals::Drum::Config::DoubleTriggerMode SettingsStore::getDoubleTriggerMode() const {
    return m_store_cache.double_trigger_mode;
}

void SettingsStore::setDoubleTriggerThresholds(const Peripherals::Drum::Config::Thresholds &thresholds) {
    if (m_store_cache.double_trigger_thresholds.don_left != thresholds.don_left ||
        m_store_cache.double_trigger_thresholds.don_right != thresholds.don_right ||
        m_store_cache.double_trigger_thresholds.ka_left != thresholds.ka_left ||
        m_store_cache.double_trigger_thresholds.ka_right != thresholds.ka_right) {

        m_store_cache.double_trigger_thresholds = thresholds;
        m_dirty = true;
    }
}
Peripherals::Drum::Config::Thresholds SettingsStore::getDoubleTriggerThresholds() const {
    return m_store_cache.double_trigger_thresholds;
}

void SettingsStore::setLedBrightness(const uint8_t brightness) {
    if (m_store_cache.led_brightness != brightness) {
        m_store_cache.led_brightness = brightness;
        m_dirty = true;
    }
}
uint8_t SettingsStore::getLedBrightness() const { return m_store_cache.led_brightness; }

void SettingsStore::setLedEnablePlayerColor(const bool do_enable) {
    if (m_store_cache.led_enable_player_color != do_enable) {
        m_store_cache.led_enable_player_color = do_enable;
        m_dirty = true;
    }
}
bool SettingsStore::getLedEnablePlayerColor() const { return m_store_cache.led_enable_player_color; }

void SettingsStore::setDebounceDelay(const uint16_t delay) {
    if (m_store_cache.debounce_delay != delay) {
        m_store_cache.debounce_delay = delay;
        m_dirty = true;
    }
}
uint16_t SettingsStore::getDebounceDelay() const { return m_store_cache.debounce_delay; }

void SettingsStore::setDonDebounceMs(const uint16_t ms) {
    if (m_store_cache.don_debounce != ms) {
        m_store_cache.don_debounce = ms;
        m_dirty = true;
    }
}
uint16_t SettingsStore::getDonDebounceMs() const { return m_store_cache.don_debounce; }

void SettingsStore::setKatDebounceMs(const uint16_t ms) {
    if (m_store_cache.kat_debounce != ms) {
        m_store_cache.kat_debounce = ms;
        m_dirty = true;
    }
}
uint16_t SettingsStore::getKatDebounceMs() const { return m_store_cache.kat_debounce; }

void SettingsStore::setCrosstalkDebounceMs(const uint16_t ms) {
    if (m_store_cache.crosstalk_debounce != ms) {
        m_store_cache.crosstalk_debounce = ms;
        m_dirty = true;
    }
}
uint16_t SettingsStore::getCrosstalkDebounceMs() const { return m_store_cache.crosstalk_debounce; }

void SettingsStore::setKeyTimeoutMs(const uint16_t ms) {
    if (m_store_cache.key_timeout_ms != ms) {
        m_store_cache.key_timeout_ms = ms;
        m_dirty = true;
    }
}
uint16_t SettingsStore::getKeyTimeoutMs() const { return m_store_cache.key_timeout_ms; }

void SettingsStore::setWeightedComparisonMode(const Peripherals::Drum::Config::WeightedComparisonMode &mode) {
    if (m_store_cache.weighted_comparison_mode != mode) {
        m_store_cache.weighted_comparison_mode = mode;
        m_dirty = true;
    }
}
Peripherals::Drum::Config::WeightedComparisonMode SettingsStore::getWeightedComparisonMode() const {
    return m_store_cache.weighted_comparison_mode;
}

void SettingsStore::setCutoffThresholds(const Peripherals::Drum::Config::Thresholds &thresholds) {
    if (m_store_cache.cutoff_thresholds.don_left != thresholds.don_left ||
        m_store_cache.cutoff_thresholds.don_right != thresholds.don_right ||
        m_store_cache.cutoff_thresholds.ka_left != thresholds.ka_left ||
        m_store_cache.cutoff_thresholds.ka_right != thresholds.ka_right) {
        m_store_cache.cutoff_thresholds = thresholds;
        m_dirty = true;
    }
}

Peripherals::Drum::Config::Thresholds SettingsStore::getCutoffThresholds() const {
    return m_store_cache.cutoff_thresholds;
}

void SettingsStore::setDrumKeysP1(const DrumKeys &keys) {
    if (m_store_cache.drum_keys_p1.don_left != keys.don_left ||
        m_store_cache.drum_keys_p1.don_right != keys.don_right ||
        m_store_cache.drum_keys_p1.ka_left != keys.ka_left ||
        m_store_cache.drum_keys_p1.ka_right != keys.ka_right) {
        m_store_cache.drum_keys_p1 = keys;
        m_dirty = true;
    }
}
DrumKeys SettingsStore::getDrumKeysP1() const { return m_store_cache.drum_keys_p1; }

void SettingsStore::setDrumKeysP2(const DrumKeys &keys) {
    if (m_store_cache.drum_keys_p2.don_left != keys.don_left ||
        m_store_cache.drum_keys_p2.don_right != keys.don_right ||
        m_store_cache.drum_keys_p2.ka_left != keys.ka_left ||
        m_store_cache.drum_keys_p2.ka_right != keys.ka_right) {
        m_store_cache.drum_keys_p2 = keys;
        m_dirty = true;
    }
}
DrumKeys SettingsStore::getDrumKeysP2() const { return m_store_cache.drum_keys_p2; }

void SettingsStore::setControllerKeys(const ControllerKeys &keys) {
    // Simple memcmp equivalent check or just copy and set dirty if different. 
    // Since struct is larger, strict member check is verbose. Using naive assignment for now.
    // Ideally we check if different.
    // For brevity, I'll just check a few or assume always dirty on set (not optimal but safe).
    // Actually, let's just do member-wise check or cast to byte array.
    // Since it's packed, I can use memcmp.
    // But I don't have memcmp included.
    // I'll just set it dirty always for now or do a simple check.
    // Actually, simple assignment is fine, setting dirty is cheap.
    m_store_cache.controller_keys = keys;
    m_dirty = true;
}
ControllerKeys SettingsStore::getControllerKeys() const { return m_store_cache.controller_keys; }

void SettingsStore::setAdcChannels(const Peripherals::Drum::Config::AdcChannels &channels) {
    if (m_store_cache.adc_channels.don_left != channels.don_left ||
        m_store_cache.adc_channels.don_right != channels.don_right ||
        m_store_cache.adc_channels.ka_left != channels.ka_left ||
        m_store_cache.adc_channels.ka_right != channels.ka_right) {
        m_store_cache.adc_channels = channels;
        m_dirty = true;
    }
}
Peripherals::Drum::Config::AdcChannels SettingsStore::getAdcChannels() const { return m_store_cache.adc_channels; }

void SettingsStore::store() {
    if (m_dirty) {
        multicore_lockout_start_blocking();
        const uint32_t interrupts = save_and_disable_interrupts();

        uint32_t current_page = m_flash_offset;
        bool do_erase = true;
        for (size_t i = 0; i < m_store_pages; ++i) {
            if (read_byte(current_page) == 0xFF) {
                do_erase = false;
                break;
            }
            current_page += m_store_size;
        }

        if (do_erase) {
            flash_range_erase(m_flash_offset, m_flash_size);
            current_page = m_flash_offset;
        }

        flash_range_program(current_page, reinterpret_cast<uint8_t *>(&m_store_cache), sizeof(m_store_cache));

        m_dirty = false;

        restore_interrupts_from_disabled(interrupts);
        multicore_lockout_end_blocking();
    }

    switch (m_scheduled_reboot) {
    case RebootType::Normal:
        watchdog_reboot(0, 0, 1);
        break;
    case RebootType::Bootsel:
        sleep_ms(100);
        reset_usb_boot(0, PICO_STDIO_USB_RESET_BOOTSEL_INTERFACE_DISABLE_MASK);
        break;
    case RebootType::None:
        break;
    }
}

void SettingsStore::reset() {
    multicore_lockout_start_blocking();
    const uint32_t interrupts = save_and_disable_interrupts();

    flash_range_erase(m_flash_offset, m_flash_size);

    restore_interrupts(interrupts);
    multicore_lockout_end_blocking();

    m_dirty = false;

    scheduleReboot();
    store();
}

void SettingsStore::scheduleReboot(const bool bootsel) {
    if (m_scheduled_reboot != RebootType::Bootsel) {
        m_scheduled_reboot = (bootsel ? RebootType::Bootsel : RebootType::Normal);
    }
}

} // namespace Doncon::Utils