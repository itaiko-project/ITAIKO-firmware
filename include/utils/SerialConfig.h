#ifndef UTILS_SERIALCONFIG_H_
#define UTILS_SERIALCONFIG_H_

#include "utils/InputState.h"
#include "utils/SettingsStore.h"

#include "pico/stdlib.h"
#include "tusb.h"

#include <functional>

namespace Doncon::Utils {

/**
 * @brief Serial configuration interface for runtime parameter adjustment
 *
 * Provides a USB CDC serial protocol for reading and writing settings.
 *
 * Protocol:
 * - Send "1000" to read all settings (returns "key:value" pairs)
 * - Send "1001" to save settings to flash
 * - Send "1002" to enter write mode
 * - Send "1003" to reload settings from flash
 * - Send "1004" to reboot to BOOTSEL mode
 * - Send "2000" to start streaming sensor data (CSV format)
 * - Send "2001" to stop streaming sensor data
 * - Send "4000" to start PS4 auth bundle upload (binary mode, see protocol docs)
 * - Send "4001" to query PS4 auth presence (returns PS4_AUTH_STATUS:0 or :1)
 * - Send "4002" to clear stored PS4 auth credentials
 * - In write mode, send "key:value" pairs (e.g., "0:800")
 *
 * Configuration Keys (46 total, keys 0-45):
 *
 * Trigger Thresholds:
 * 0: Don Left Threshold (Left face sensitivity)
 * 1: Ka Left Threshold (Left rim sensitivity)
 * 2: Don Right Threshold (Right face sensitivity)
 * 3: Ka Right Threshold (Right rim sensitivity)
 *
 * Debounce Settings:
 * 4: Don Debounce (Lockout time between don hits left/right, ms)
 * 5: Kat Debounce (Lockout time between ka hits left/right, ms)
 * 6: Crosstalk Debounce (Time to ignore ka after don hit, ms)
 * 7: Debounce Delay (Same-pad lockout time, can't hit same pad twice, ms)
 * 8: Key Timeout (How long button appears pressed to OS, ms)
 *
 * Double Trigger Settings:
 * 9: Double Trigger Mode (0=Off, 1=Threshold)
 * 10: Double Trigger Don Left Threshold
 * 11: Double Trigger Ka Left Threshold
 * 12: Double Trigger Don Right Threshold
 * 13: Double Trigger Ka Right Threshold
 *
 * Cutoff Thresholds:
 * 14: Cutoff Don Left
 * 15: Cutoff Ka Left
 * 16: Cutoff Don Right
 * 17: Cutoff Ka Right
 *
 * Keyboard Mappings - Drum P1:
 * 18: Drum P1 Ka Left (HID keycode)
 * 19: Drum P1 Don Left (HID keycode)
 * 20: Drum P1 Don Right (HID keycode)
 * 21: Drum P1 Ka Right (HID keycode)
 *
 * Keyboard Mappings - Drum P2:
 * 22: Drum P2 Ka Left (HID keycode)
 * 23: Drum P2 Don Left (HID keycode)
 * 24: Drum P2 Don Right (HID keycode)
 * 25: Drum P2 Ka Right (HID keycode)
 *
 * Keyboard Mappings - Controller:
 * 26: Controller Up (HID keycode)
 * 27: Controller Down (HID keycode)
 * 28: Controller Left (HID keycode)
 * 29: Controller Right (HID keycode)
 * 30: Controller North (HID keycode)
 * 31: Controller East (HID keycode)
 * 32: Controller South (HID keycode)
 * 33: Controller West (HID keycode)
 * 34: Controller L (HID keycode)
 * 35: Controller R (HID keycode)
 * 36: Controller Start (HID keycode)
 * 37: Controller Select (HID keycode)
 * 38: Controller Home (HID keycode)
 * 39: Controller Share (HID keycode)
 * 40: Controller L3 (HID keycode)
 * 41: Controller R3 (HID keycode)
 *
 * ADC Channel Mappings:
 * 42: ADC Channel Don Left (0-3)
 * 43: ADC Channel Ka Left (0-3)
 * 44: ADC Channel Don Right (0-3)
 * 45: ADC Channel Ka Right (0-3)
 *
 * Special Output:
 * Version:Firmware Version String (e.g. "Version:0.0.0")
 */
class SerialConfig {
  public:
    using SettingsAppliedCallback = std::function<void()>;

    explicit SerialConfig(SettingsStore &settings_store, SettingsAppliedCallback on_settings_applied = nullptr);

    /**
     * @brief Process incoming serial data
     *
     * Call this from main loop when CDC data is available.
     * Non-blocking, processes one command per call.
     */
    void processSerial();

    /**
     * @brief Send sensor data if streaming is active
     *
     * Call this from main loop after processSerial().
     * Sends CSV-formatted sensor data when streaming mode is enabled.
     *
     * @param input_state Current input state containing sensor data
     */
    void sendSensorDataIfStreaming(const InputState &input_state);

  private:
    SettingsStore &m_settings_store;
    SettingsAppliedCallback m_on_settings_applied;
    bool m_write_mode;
    int m_write_count;
    bool m_streaming_mode;
    bool m_input_streaming_mode;
    uint64_t m_last_stream_time;

    // ADC streaming average data
    uint32_t m_don_left_sum;
    uint32_t m_ka_left_sum;
    uint32_t m_don_right_sum;
    uint32_t m_ka_right_sum;
    uint32_t m_sample_count;

    // PS4 auth upload state
    bool m_ps4_auth_upload_mode{false};
    uint32_t m_ps4_auth_bytes_received{0};
    uint32_t m_ps4_auth_expected_size{0};

    enum class Command : int {
        ReadAll = 1000,
        SaveToFlash = 1001,
        EnterWriteMode = 1002,
        ReloadFromFlash = 1003,
        RebootToBootsel = 1004,
        StartStreaming = 2000,
        StopStreaming = 2001,
        StartInputStreaming = 2002,
        StartPS4AuthUpload = 4000,
        QueryPS4Auth = 4001,
        ClearPS4Auth = 4002,
    };

    void handleCommand(int command_value);
    void handleWriteData(const char *data);
    void processPS4AuthUpload();
    void sendAllSettings();
    void sendSensorData(uint16_t ka_l, uint16_t don_l, uint16_t don_r, uint16_t ka_r);
    void sendInputData(const InputState &input_state);
    uint16_t getSettingByKey(int key);
    void setSettingByKey(int key, uint16_t value);
};

} // namespace Doncon::Utils

#endif // UTILS_SERIALCONFIG_H_
