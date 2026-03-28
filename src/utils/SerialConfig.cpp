#include "utils/SerialConfig.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tusb.h"

namespace Doncon::Utils {

namespace {
char s_serial_buf[512];
uint32_t s_serial_buf_idx = 0;
uint8_t s_ps4_auth_buf[4096];

uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            const uint32_t mask = (crc & 1U) ? 0xEDB88320U : 0U;
            crc = (crc >> 1U) ^ mask;
        }
    }
    return ~crc;
}
} // namespace

SerialConfig::SerialConfig(SettingsStore &settings_store, SettingsAppliedCallback on_settings_applied)
    : m_settings_store(settings_store), m_on_settings_applied(on_settings_applied), m_write_mode(false),
      m_write_count(0), m_streaming_mode(false), m_input_streaming_mode(false), m_last_stream_time(0), m_don_left_sum(0), m_ka_left_sum(0),
      m_don_right_sum(0), m_ka_right_sum(0), m_sample_count(0) {}

void SerialConfig::processSerial() {
    if (!tud_cdc_connected()) {
        return;
    }

    if (m_ps4_auth_upload_mode) {
        processPS4AuthUpload();
        if (m_ps4_auth_upload_mode) {
            return;
        }
    }

    // Read characters into our buffer
    while (tud_cdc_available() && s_serial_buf_idx < sizeof(s_serial_buf) - 1) {
        char c;
        uint32_t count = tud_cdc_read(&c, 1);
        if (count == 0) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (s_serial_buf_idx > 0) {
                s_serial_buf[s_serial_buf_idx] = '\0'; // Null terminate
                // We have a complete line
                if (m_write_mode) {
                    handleWriteData(s_serial_buf);
                } else {
                    handleCommand(std::atoi(s_serial_buf));
                }
            }
            s_serial_buf_idx = 0; // Reset for next line
        } else {
            s_serial_buf[s_serial_buf_idx++] = c;
        }
    }

    if (s_serial_buf_idx >= sizeof(s_serial_buf) - 1) {
        // Buffer overflow, discard
        s_serial_buf_idx = 0;
    }
}

void SerialConfig::handleCommand(int command_value) {
    switch (static_cast<Command>(command_value)) {
    case Command::ReadAll:
        sendAllSettings();
        break;

    case Command::SaveToFlash:
        m_settings_store.store();
        stdio_flush();
        break;

    case Command::EnterWriteMode:
        m_write_mode = true;
        m_write_count = 0;
        break;

    case Command::ReloadFromFlash:
        // SettingsStore loads from flash on construction, so we just echo current values
        sendAllSettings();
        break;

    case Command::RebootToBootsel:
        m_settings_store.scheduleReboot(true);
        m_settings_store.store();
        break;

    case Command::StartStreaming:
        m_streaming_mode = true;
        m_input_streaming_mode = false;
        m_don_left_sum = 0;
        m_ka_left_sum = 0;
        m_don_right_sum = 0;
        m_ka_right_sum = 0;
        m_sample_count = 0;
        break;

    case Command::StartInputStreaming:
        m_streaming_mode = false;
        m_input_streaming_mode = true;
        break;

    case Command::StopStreaming:
        m_streaming_mode = false;
        m_input_streaming_mode = false;
        break;

    case Command::StartPS4AuthUpload:
        m_ps4_auth_upload_mode = true;
        m_ps4_auth_bytes_received = 0;
        m_ps4_auth_expected_size = 0;
        std::memset(s_ps4_auth_buf, 0, sizeof(s_ps4_auth_buf));
        printf("PS4_AUTH_READY\n");
        stdio_flush();
        break;

    case Command::QueryPS4Auth:
        printf("PS4_AUTH_STATUS:%d\n", m_settings_store.hasPS4AuthCredentials() ? 1 : 0);
        stdio_flush();
        break;

    case Command::ClearPS4Auth:
        m_settings_store.clearPS4AuthCredentials();
        m_settings_store.scheduleReboot(false);
        m_settings_store.store();
        printf("PS4_AUTH_CLEARED\n");
        stdio_flush();
        break;
    }
}

void SerialConfig::processPS4AuthUpload() {
    // Bundle format: "PAK1" (4) | key_len u16 LE (2) | pad (2) | serial (16) | signature (256) | key_pem (key_len) | crc32 (4)
    while (tud_cdc_available() && m_ps4_auth_bytes_received < sizeof(s_ps4_auth_buf)) {
        m_ps4_auth_bytes_received +=
            tud_cdc_read(&s_ps4_auth_buf[m_ps4_auth_bytes_received],
                         sizeof(s_ps4_auth_buf) - m_ps4_auth_bytes_received);

        if (m_ps4_auth_expected_size == 0 && m_ps4_auth_bytes_received >= 8) {
            if (std::memcmp(s_ps4_auth_buf, "PAK1", 4) != 0) {
                m_ps4_auth_upload_mode = false;
                printf("PS4_AUTH_ERROR:BAD_MAGIC\n");
                stdio_flush();
                return;
            }
            const uint16_t key_len =
                static_cast<uint16_t>(s_ps4_auth_buf[4] | (static_cast<uint16_t>(s_ps4_auth_buf[5]) << 8));
            if (key_len == 0 || key_len > 3584) {
                m_ps4_auth_upload_mode = false;
                printf("PS4_AUTH_ERROR:BAD_KEY_LEN\n");
                stdio_flush();
                return;
            }
            // 4 magic + 2 key_len + 2 pad + 16 serial + 256 sig + key_len pem + 4 crc
            m_ps4_auth_expected_size = 4 + 2 + 2 + 16 + 256 + key_len + 4;
            if (m_ps4_auth_expected_size > sizeof(s_ps4_auth_buf)) {
                m_ps4_auth_upload_mode = false;
                printf("PS4_AUTH_ERROR:TOO_LARGE\n");
                stdio_flush();
                return;
            }
        }
    }

    if (m_ps4_auth_expected_size == 0 || m_ps4_auth_bytes_received < m_ps4_auth_expected_size) {
        return; // still receiving
    }

    // Validate CRC over everything before the last 4 bytes
    const uint32_t payload_len = m_ps4_auth_expected_size - 4;
    uint32_t expected_crc = 0;
    std::memcpy(&expected_crc, &s_ps4_auth_buf[payload_len], 4);
    const uint32_t actual_crc = crc32(s_ps4_auth_buf, payload_len);
    if (expected_crc != actual_crc) {
        m_ps4_auth_upload_mode = false;
        printf("PS4_AUTH_ERROR:BAD_CRC\n");
        stdio_flush();
        return;
    }

    const uint16_t key_len =
        static_cast<uint16_t>(s_ps4_auth_buf[4] | (static_cast<uint16_t>(s_ps4_auth_buf[5]) << 8));
    const uint8_t *serial    = &s_ps4_auth_buf[8];
    const uint8_t *signature = &s_ps4_auth_buf[8 + 16];
    const char *key_pem      = reinterpret_cast<const char *>(&s_ps4_auth_buf[8 + 16 + 256]);

    if (!m_settings_store.setPS4AuthCredentials(serial, signature, key_pem, key_len)) {
        m_ps4_auth_upload_mode = false;
        printf("PS4_AUTH_ERROR:STORE_FAILED\n");
        stdio_flush();
        return;
    }

    m_ps4_auth_upload_mode = false;
    m_settings_store.scheduleReboot(false);
    m_settings_store.store();
    printf("PS4_AUTH_SAVED\n");
    stdio_flush();
}

void SerialConfig::handleWriteData(const char *data) {
    char data_copy[512];
    strncpy(data_copy, data, sizeof(data_copy) - 1);
    data_copy[sizeof(data_copy) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(data_copy, " \n\r", &saveptr);

    while (token != NULL) {
        // Split on ':'
        char *colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            int key = atoi(token);
            int value = atoi(colon + 1);

            setSettingByKey(key, value);

            m_write_count++;
        }
        token = strtok_r(NULL, " \n\r", &saveptr);
    }

    // Exit write mode after receiving at least one value.
    // The python script sends all values in one line.
    if (m_write_count > 0) {
        m_write_mode = false;
        m_write_count = 0;

        // Apply settings to Drum object by calling callback
        if (m_on_settings_applied) {
            m_on_settings_applied();
        }
    }
}

void SerialConfig::sendAllSettings() {
    // Send 46 values: 9 hidtaiko-compatible + 5 extended (double trigger) + 4 extended (cutoff thresholds) + 24 extended (key mappings) + 4 extended (ADC channels)
    for (int i = 0; i < 46; i++) {
        uint16_t value = getSettingByKey(i);
        printf("%d:%d\n", i, value);
        stdio_flush();
        //sleep_us(5000); // Small delay between values
    }
    printf("Version:%s\n", FIRMWARE_VERSION);
    stdio_flush();
}

uint16_t SerialConfig::getSettingByKey(int key) {
    auto thresholds = m_settings_store.getTriggerThresholds();

    // HIDtaiko-compatible mapping (web page order, not kando array order!)
    switch (key) {
    case 0: // Don Left (face left sensitivity) - swapped!
        return thresholds.don_left;
    case 1: // Ka Left (rim left sensitivity) - swapped!
        return thresholds.ka_left;
    case 2: // Don Right (face right sensitivity)
        return thresholds.don_right;
    case 3: // Ka Right (rim right sensitivity)
        return thresholds.ka_right;
    case 4: // Don Debounce (B delay - next input time after face)
        return m_settings_store.getDonDebounceMs();
    case 5: // Kat Debounce (C delay - rim input acceptance time)
        return m_settings_store.getKatDebounceMs();
    case 6: // Crosstalk Debounce (D delay - time to ignore rim after face)
        return m_settings_store.getCrosstalkDebounceMs();
    case 7: // Key Timeout (H delay - input limit for simulators)
        return m_settings_store.getKeyTimeoutMs();
    case 8: // Debounce Delay (A delay - single hit acceptance time)
        return m_settings_store.getDebounceDelay();
    case 9: // Double Trigger Mode (0=Off, 1=Threshold)
        return static_cast<uint16_t>(m_settings_store.getDoubleTriggerMode());
    case 10: // Double Trigger Don Left Threshold
        return m_settings_store.getDoubleTriggerThresholds().don_left;
    case 11: // Double Trigger Ka Left Threshold
        return m_settings_store.getDoubleTriggerThresholds().ka_left;
    case 12: // Double Trigger Don Right Threshold
        return m_settings_store.getDoubleTriggerThresholds().don_right;
    case 13: // Double Trigger Ka Right Threshold
        return m_settings_store.getDoubleTriggerThresholds().ka_right;
    case 14: // Cutoff Don Left
        return m_settings_store.getCutoffThresholds().don_left;
    case 15: // Cutoff Ka Left
        return m_settings_store.getCutoffThresholds().ka_left;
    case 16: // Cutoff Don Right
        return m_settings_store.getCutoffThresholds().don_right;
    case 17: // Cutoff Ka Right
        return m_settings_store.getCutoffThresholds().ka_right;

    // Drum Keys P1 (Keys 18-21)
    case 18: // Drum P1 Ka Left
        return m_settings_store.getDrumKeysP1().ka_left;
    case 19: // Drum P1 Don Left
        return m_settings_store.getDrumKeysP1().don_left;
    case 20: // Drum P1 Don Right
        return m_settings_store.getDrumKeysP1().don_right;
    case 21: // Drum P1 Ka Right
        return m_settings_store.getDrumKeysP1().ka_right;

    // Drum Keys P2 (Keys 22-25)
    case 22: // Drum P2 Ka Left
        return m_settings_store.getDrumKeysP2().ka_left;
    case 23: // Drum P2 Don Left
        return m_settings_store.getDrumKeysP2().don_left;
    case 24: // Drum P2 Don Right
        return m_settings_store.getDrumKeysP2().don_right;
    case 25: // Drum P2 Ka Right
        return m_settings_store.getDrumKeysP2().ka_right;

    // Controller Keys (Keys 26-41)
    case 26: // Controller Up
        return m_settings_store.getControllerKeys().up;
    case 27: // Controller Down
        return m_settings_store.getControllerKeys().down;
    case 28: // Controller Left
        return m_settings_store.getControllerKeys().left;
    case 29: // Controller Right
        return m_settings_store.getControllerKeys().right;
    case 30: // Controller North
        return m_settings_store.getControllerKeys().north;
    case 31: // Controller East
        return m_settings_store.getControllerKeys().east;
    case 32: // Controller South
        return m_settings_store.getControllerKeys().south;
    case 33: // Controller West
        return m_settings_store.getControllerKeys().west;
    case 34: // Controller L
        return m_settings_store.getControllerKeys().l;
    case 35: // Controller R
        return m_settings_store.getControllerKeys().r;
    case 36: // Controller Start
        return m_settings_store.getControllerKeys().start;
    case 37: // Controller Select
        return m_settings_store.getControllerKeys().select;
    case 38: // Controller Home
        return m_settings_store.getControllerKeys().home;
    case 39: // Controller Share
        return m_settings_store.getControllerKeys().share;
    case 40: // Controller L3
        return m_settings_store.getControllerKeys().l3;
    case 41: // Controller R3
        return m_settings_store.getControllerKeys().r3;

    // ADC Channel Mappings (Keys 42-45)
    case 42: // ADC Channel Don Left
        return m_settings_store.getAdcChannels().don_left;
    case 43: // ADC Channel Ka Left
        return m_settings_store.getAdcChannels().ka_left;
    case 44: // ADC Channel Don Right
        return m_settings_store.getAdcChannels().don_right;
    case 45: // ADC Channel Ka Right
        return m_settings_store.getAdcChannels().ka_right;

    default:
        return 0;
    }
}

void SerialConfig::setSettingByKey(int key, uint16_t value) {
    auto thresholds = m_settings_store.getTriggerThresholds();
    auto double_thresholds = m_settings_store.getDoubleTriggerThresholds();

    // HIDtaiko-compatible mapping (web page order, not kando array order!)
    switch (key) {
    case 0: // Don Left (face left sensitivity) - swapped!
        thresholds.don_left = value;
        m_settings_store.setTriggerThresholds(thresholds);
        break;
    case 1: // Ka Left (rim left sensitivity) - swapped!
        thresholds.ka_left = value;
        m_settings_store.setTriggerThresholds(thresholds);
        break;
    case 2: // Don Right (face right sensitivity)
        thresholds.don_right = value;
        m_settings_store.setTriggerThresholds(thresholds);
        break;
    case 3: // Ka Right (rim right sensitivity)
        thresholds.ka_right = value;
        m_settings_store.setTriggerThresholds(thresholds);
        break;
    case 4: // Don Debounce (B delay)
        m_settings_store.setDonDebounceMs(value);
        break;
    case 5: // Kat Debounce (C delay)
        m_settings_store.setKatDebounceMs(value);
        break;
    case 6: // Crosstalk Debounce (D delay)
        m_settings_store.setCrosstalkDebounceMs(value);
        break;
    case 7: // Key Timeout (H delay)
        m_settings_store.setKeyTimeoutMs(value);
        break;
    case 8: // Debounce Delay (A delay)
        m_settings_store.setDebounceDelay(value);
        break;
    case 9: // Double Trigger Mode
        m_settings_store.setDoubleTriggerMode(static_cast<Peripherals::Drum::Config::DoubleTriggerMode>(value));
        break;
    case 10: // Double Trigger Don Left
        double_thresholds.don_left = value;
        m_settings_store.setDoubleTriggerThresholds(double_thresholds);
        break;
    case 11: // Double Trigger Ka Left
        double_thresholds.ka_left = value;
        m_settings_store.setDoubleTriggerThresholds(double_thresholds);
        break;
    case 12: // Double Trigger Don Right
        double_thresholds.don_right = value;
        m_settings_store.setDoubleTriggerThresholds(double_thresholds);
        break;
    case 13: // Double Trigger Ka Right
        double_thresholds.ka_right = value;
        m_settings_store.setDoubleTriggerThresholds(double_thresholds);
        break;
    case 14: // Cutoff Don Left
        double_thresholds = m_settings_store.getCutoffThresholds();
        double_thresholds.don_left = value;
        m_settings_store.setCutoffThresholds(double_thresholds);
        break;
    case 15: // Cutoff Ka Left
        double_thresholds = m_settings_store.getCutoffThresholds();
        double_thresholds.ka_left = value;
        m_settings_store.setCutoffThresholds(double_thresholds);
        break;
    case 16: // Cutoff Don Right
        double_thresholds = m_settings_store.getCutoffThresholds();
        double_thresholds.don_right = value;
        m_settings_store.setCutoffThresholds(double_thresholds);
        break;
    case 17: // Cutoff Ka Right
        double_thresholds = m_settings_store.getCutoffThresholds();
        double_thresholds.ka_right = value;
        m_settings_store.setCutoffThresholds(double_thresholds);
        break;

    // Drum Keys P1 (Keys 18-21)
    case 18: { // Drum P1 Ka Left
        auto drum_keys = m_settings_store.getDrumKeysP1();
        drum_keys.ka_left = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP1(drum_keys);
        break;
    }
    case 19: { // Drum P1 Don Left
        auto drum_keys = m_settings_store.getDrumKeysP1();
        drum_keys.don_left = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP1(drum_keys);
        break;
    }
    case 20: { // Drum P1 Don Right
        auto drum_keys = m_settings_store.getDrumKeysP1();
        drum_keys.don_right = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP1(drum_keys);
        break;
    }
    case 21: { // Drum P1 Ka Right
        auto drum_keys = m_settings_store.getDrumKeysP1();
        drum_keys.ka_right = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP1(drum_keys);
        break;
    }

    // Drum Keys P2 (Keys 22-25)
    case 22: { // Drum P2 Ka Left
        auto drum_keys = m_settings_store.getDrumKeysP2();
        drum_keys.ka_left = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP2(drum_keys);
        break;
    }
    case 23: { // Drum P2 Don Left
        auto drum_keys = m_settings_store.getDrumKeysP2();
        drum_keys.don_left = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP2(drum_keys);
        break;
    }
    case 24: { // Drum P2 Don Right
        auto drum_keys = m_settings_store.getDrumKeysP2();
        drum_keys.don_right = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP2(drum_keys);
        break;
    }
    case 25: { // Drum P2 Ka Right
        auto drum_keys = m_settings_store.getDrumKeysP2();
        drum_keys.ka_right = static_cast<uint8_t>(value);
        m_settings_store.setDrumKeysP2(drum_keys);
        break;
    }

    // Controller Keys (Keys 26-41)
    case 26: { // Controller Up
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.up = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 27: { // Controller Down
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.down = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 28: { // Controller Left
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.left = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 29: { // Controller Right
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.right = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 30: { // Controller North
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.north = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 31: { // Controller East
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.east = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 32: { // Controller South
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.south = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 33: { // Controller West
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.west = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 34: { // Controller L
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.l = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 35: { // Controller R
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.r = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 36: { // Controller Start
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.start = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 37: { // Controller Select
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.select = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 38: { // Controller Home
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.home = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 39: { // Controller Share
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.share = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 40: { // Controller L3
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.l3 = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }
    case 41: { // Controller R3
        auto controller_keys = m_settings_store.getControllerKeys();
        controller_keys.r3 = static_cast<uint8_t>(value);
        m_settings_store.setControllerKeys(controller_keys);
        break;
    }

    // ADC Channel Mappings (Keys 42-45)
    case 42: { // ADC Channel Don Left
        auto adc_channels = m_settings_store.getAdcChannels();
        adc_channels.don_left = static_cast<uint8_t>(value);
        m_settings_store.setAdcChannels(adc_channels);
        break;
    }
    case 43: { // ADC Channel Ka Left
        auto adc_channels = m_settings_store.getAdcChannels();
        adc_channels.ka_left = static_cast<uint8_t>(value);
        m_settings_store.setAdcChannels(adc_channels);
        break;
    }
    case 44: { // ADC Channel Don Right
        auto adc_channels = m_settings_store.getAdcChannels();
        adc_channels.don_right = static_cast<uint8_t>(value);
        m_settings_store.setAdcChannels(adc_channels);
        break;
    }
    case 45: { // ADC Channel Ka Right
        auto adc_channels = m_settings_store.getAdcChannels();
        adc_channels.ka_right = static_cast<uint8_t>(value);
        m_settings_store.setAdcChannels(adc_channels);
        break;
    }

    default:
        break;
    }
}

void SerialConfig::sendSensorData(uint16_t ka_l, uint16_t don_l, uint16_t don_r,
                                      uint16_t ka_r) {
    // Pack 4x 16-bit values into one 64-bit integer
    // Order: Ka Left (MSB), Don Left, Don Right, Ka Right (LSB)
    uint64_t packet = ((uint64_t)ka_l << 48) |
                      ((uint64_t)don_l << 32) |
                      ((uint64_t)don_r << 16) |
                      ((uint64_t)ka_r);

    printf("%016llX\n", packet);
    stdio_flush();
}

void SerialConfig::sendSensorDataIfStreaming(const InputState &input_state) {
    if ((!m_streaming_mode && !m_input_streaming_mode) || !tud_cdc_connected()) {
        return;
    }

    if (m_input_streaming_mode) {
        sendInputData(input_state);
        return;
    }

    // Accumulate sensor data
    m_don_left_sum += input_state.drum.don_left.raw;
    m_ka_left_sum += input_state.drum.ka_left.raw;
    m_don_right_sum += input_state.drum.don_right.raw;
    m_ka_right_sum += input_state.drum.ka_right.raw;
    m_sample_count++;

    // // Rate limit to ~1000Hz (1ms between sends)
    // const uint64_t current_time = time_us_64();
    // if (current_time - m_last_stream_time < 1000) {
    //     return;
    // }

   // m_last_stream_time = current_time;

    if (m_sample_count > 0) {
        const uint16_t don_left_avg = m_don_left_sum / m_sample_count;
        const uint16_t ka_left_avg = m_ka_left_sum / m_sample_count;
        const uint16_t don_right_avg = m_don_right_sum / m_sample_count;
        const uint16_t ka_right_avg = m_ka_right_sum / m_sample_count;

        sendSensorData(ka_left_avg, don_left_avg, don_right_avg, ka_right_avg);

        // Reset accumulators
        m_don_left_sum = 0;
        m_ka_left_sum = 0;
        m_don_right_sum = 0;
        m_ka_right_sum = 0;
        m_sample_count = 0;
    }
}

void SerialConfig::sendInputData(const InputState &input_state) {
    const auto &drum = input_state.drum;
    uint8_t mask = 0;

    if (drum.ka_left.triggered) mask |= (1 << 0);
    if (drum.don_left.triggered) mask |= (1 << 1);
    if (drum.don_right.triggered) mask |= (1 << 2);
    if (drum.ka_right.triggered) mask |= (1 << 3);

    printf("%X\n", mask);
    stdio_flush();
}

} // namespace Doncon::Utils
