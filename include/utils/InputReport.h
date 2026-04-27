#ifndef UTILS_INPUTREPORT_H_
#define UTILS_INPUTREPORT_H_

#include "utils/InputState.h"

#include "usb/device/hid/keyboard_driver.h"
#include "usb/device/hid/ps4_driver.h"
#include "usb/device/hid/switch_driver.h"
#include "usb/device/midi_driver.h"
#include "usb/device/vendor/usio_driver.h"
#include "usb/device/vendor/xinput_driver.h"
#include "usb/device_driver.h"

#include <cstdint>
#include <memory>

namespace Doncon::Utils {

class SettingsStore;

struct InputReport {
  private:
    enum class Player : uint8_t {
        One,
        Two,
    };
    
    std::shared_ptr<SettingsStore> m_settings_store;

    hid_switch_report_t m_switch_report{
        .buttons = 0x00,
        .hat = 0x08,
        .lx = 0x80,
        .ly = 0x80,
        .rx = 0x80,
        .ry = 0x80,
        .vendor = 0x00,
    };
    hid_ps4_report_t m_ps4_report{
        .report_id = 0x01,
        .lx = 0x80,
        .ly = 0x80,
        .rx = 0x80,
        .ry = 0x80,
        .buttons1 = 0x08,
        .buttons2 = 0x00,
        .buttons3 = 0x00,
        .lt = 0x00,
        .rt = 0x00,
        .sensor_timestamp = 0x0000,
        .sensor_temperature = 0x00,
        .gyrox = 0x0000,
        .gyroy = 0x0000,
        .gyroz = 0x0000,
        .accelx = 0x0000,
        .accely = 0x0000,
        .accelz = 0x0000,
        ._reserved1 = {},
        .battery = 0 | (1 << 4) | 11, // Cable connected and fully charged
        .peripheral = 0x01,
        ._reserved2 = 0x00,
        .touch_report_count = 0x00,
        .touch_report1 = {},
        .touch_report2 = {},
        .touch_report3 = {},
        ._reserved3 = {},
    };
    hid_nkro_keyboard_report_t m_keyboard_report{
        .keycodes = {},
    };
    xinput_report_t m_xinput_report{
        .report_id = 0x00,
        .report_size = sizeof(xinput_report_t),
        .buttons1 = 0x08,
        .buttons2 = 0x00,
        .lt = 0x00,
        .rt = 0x00,
        .lx = 0x0000,
        .ly = 0x0000,
        .rx = 0x0000,
        .ry = 0x0000,
        ._reserved = {},
    };
    midi_report_t m_midi_report{
        .status = {},
        .velocity = {},
    };
    usio_input_t m_usio_input{};

    uint8_t m_ps4_report_counter = 0;

    usb_report_t getSwitchReport(const InputState &state);
    usb_report_t getPS4Report(const InputState &state);
    usb_report_t getKeyboardReport(const InputState &state, Player player);
    usb_report_t getXinputBaseReport(const InputState &state);
    usb_report_t getXinputDigitalReport(const InputState &state);
    usb_report_t getMidiReport(const InputState &state);
    usb_report_t getUsioReport(const InputState &state);

  public:
    InputReport(std::shared_ptr<SettingsStore> settings_store);

    usb_report_t getReport(const InputState &state, usb_mode_t mode);
};

} // namespace Doncon::Utils

#endif // UTILS_INPUTREPORT_H_