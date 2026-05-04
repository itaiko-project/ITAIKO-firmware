#include "utils/InputReport.h"
#include "utils/SettingsStore.h"

namespace Doncon::Utils {

namespace {

uint8_t getHidHat(const InputState::Controller::DPad dpad) {
    if (dpad.up && dpad.right) {
        return 0x01;
    }
    if (dpad.down && dpad.right) {
        return 0x03;
    }
    if (dpad.down && dpad.left) {
        return 0x05;
    }
    if (dpad.up && dpad.left) {
        return 0x07;
    }
    if (dpad.up) {
        return 0x00;
    }
    if (dpad.right) {
        return 0x02;
    }
    if (dpad.down) {
        return 0x04;
    }
    if (dpad.left) {
        return 0x06;
    }

    return 0x08;
}

} // namespace

InputReport::InputReport(std::shared_ptr<SettingsStore> settings_store) : m_settings_store(std::move(settings_store)) {}

usb_report_t InputReport::getSwitchReport(const InputState &state) {
    const auto &controller = state.controller;
    const auto &drum = state.drum;

    m_switch_report.buttons = 0                                             //
                              | (controller.buttons.west ? (1 << 0) : 0)    // Y
                              | (controller.buttons.south ? (1 << 1) : 0)   // B
                              | (controller.buttons.east ? (1 << 2) : 0)    // A
                              | (controller.buttons.north ? (1 << 3) : 0)   // X
                              | (controller.buttons.l ? (1 << 4) : 0)       // L
                              | (controller.buttons.r ? (1 << 5) : 0)       // R
                              | (drum.ka_left.triggered ? (1 << 6) : 0)     // ZL
                              | (drum.ka_right.triggered ? (1 << 7) : 0)    // ZR
                              | (controller.buttons.select ? (1 << 8) : 0)  // -
                              | (controller.buttons.start ? (1 << 9) : 0)   // +
                              | (drum.don_left.triggered ? (1 << 10) : 0)   // LS
                              | (drum.don_right.triggered ? (1 << 11) : 0)  // RS
                              | (controller.buttons.home ? (1 << 12) : 0)   // Home
                              | (controller.buttons.share ? (1 << 13) : 0); // Capture

    m_switch_report.hat = getHidHat(controller.dpad);

    return {reinterpret_cast<uint8_t *>(&m_switch_report), sizeof(hid_switch_report_t)};
}

usb_report_t InputReport::getPS4Report(const InputState &state) {
    const auto &controller = state.controller;
    const auto &drum = state.drum;

    m_ps4_report.buttons1 = getHidHat(controller.dpad)                    //
                            | (controller.buttons.west ? (1 << 4) : 0)    // Square
                            | (controller.buttons.south ? (1 << 5) : 0)   // Cross
                            | (controller.buttons.east ? (1 << 6) : 0)    // Circle
                            | (controller.buttons.north ? (1 << 7) : 0);  // Triangle
    m_ps4_report.buttons2 = 0                                             //
                            | (controller.buttons.l ? (1 << 0) : 0)       // L1
                            | (controller.buttons.r ? (1 << 1) : 0)       // R1
                            | (drum.ka_left.triggered ? (1 << 2) : 0)     // L2
                            | (drum.ka_right.triggered ? (1 << 3) : 0)    // R2
                            | (controller.buttons.share ? (1 << 4) : 0)   // Share
                            | (controller.buttons.start ? (1 << 5) : 0)   // Option
                            | (drum.don_left.triggered ? (1 << 6) : 0)    // L3
                            | (drum.don_right.triggered ? (1 << 7) : 0);  // R3
    m_ps4_report.buttons3 = (m_ps4_report_counter << 2)                   //
                            | (controller.buttons.home ? (1 << 0) : 0)    // PS
                            | (controller.buttons.select ? (1 << 1) : 0); // T-Pad

    m_ps4_report.lt = (drum.ka_left.triggered ? 0xFF : 0);
    m_ps4_report.rt = (drum.ka_right.triggered ? 0xFF : 0);

    // This method actually gets called more often than the report is sent,
    // so counters are not consecutive ... let's see if this turns out to
    // be a problem.
    if (++m_ps4_report_counter > (UINT8_MAX >> 2)) {
        m_ps4_report_counter = 0;
    }

    return {reinterpret_cast<uint8_t *>(&m_ps4_report), sizeof(hid_ps4_report_t)};
}

usb_report_t InputReport::getKeyboardReport(const InputState &state, InputReport::Player player) {
    const auto &controller = state.controller;
    const auto &drum = state.drum;

    m_keyboard_report = {};

    auto set_key = [&](const bool input, const uint8_t keycode) {
        if (input) {
            m_keyboard_report.keycodes[keycode / 8] |= 1 << (keycode % 8);
        }
    };
    
    DrumKeys drum_keys;
    if (player == Player::One) {
        drum_keys = m_settings_store->getDrumKeysP1();
    } else {
        drum_keys = m_settings_store->getDrumKeysP2();
    }
    const ControllerKeys ctrl_keys = m_settings_store->getControllerKeys();

    set_key(drum.ka_left.triggered, drum_keys.ka_left);
    set_key(drum.don_left.triggered, drum_keys.don_left);
    set_key(drum.don_right.triggered, drum_keys.don_right);
    set_key(drum.ka_right.triggered, drum_keys.ka_right);

    set_key(controller.dpad.up, ctrl_keys.up);
    set_key(controller.dpad.down, ctrl_keys.down);
    set_key(controller.dpad.left, ctrl_keys.left);
    set_key(controller.dpad.right, ctrl_keys.right);

    set_key(controller.buttons.north, ctrl_keys.north);
    set_key(controller.buttons.east, ctrl_keys.east);
    set_key(controller.buttons.south, ctrl_keys.south);
    set_key(controller.buttons.west, ctrl_keys.west);

    set_key(controller.buttons.l, ctrl_keys.l);
    set_key(controller.buttons.r, ctrl_keys.r);

    set_key(controller.buttons.start, ctrl_keys.start);
    set_key(controller.buttons.select, ctrl_keys.select);
    set_key(controller.buttons.home, ctrl_keys.home);
    set_key(controller.buttons.share, ctrl_keys.share);
    
    // L3/R3 if we have input state for them (controller struct might not have explicit L3/R3 bools, let's check InputState)
    // InputState::Controller has buttons which seem to map to physical pins.
    // If controller has no L3/R3 pins, we can't map them.
    // Let's assume standard controller buttons for now.
    // The previous code didn't map L3/R3 for keyboard.

    return {reinterpret_cast<uint8_t *>(&m_keyboard_report), sizeof(hid_nkro_keyboard_report_t)};
}

usb_report_t InputReport::getXinputBaseReport(const InputState &state) {
    const auto &controller = state.controller;

    m_xinput_report.buttons1 = 0                                            //
                               | (controller.dpad.up ? (1 << 0) : 0)        // Dpad Up
                               | (controller.dpad.down ? (1 << 1) : 0)      // Dpad Down
                               | (controller.dpad.left ? (1 << 2) : 0)      // Dpad Left
                               | (controller.dpad.right ? (1 << 3) : 0)     // Dpad Right
                               | (controller.buttons.start ? (1 << 4) : 0)  // Start
                               | (controller.buttons.select ? (1 << 5) : 0) // Select
                               | (false ? (1 << 6) : 0)                     // L3
                               | (false ? (1 << 7) : 0);                    // R3
    m_xinput_report.buttons2 = 0                                            //
                               | (controller.buttons.l ? (1 << 0) : 0)      // L1
                               | (controller.buttons.r ? (1 << 1) : 0)      // R1
                               | (controller.buttons.home ? (1 << 2) : 0)   // Guide
                               | (controller.buttons.south ? (1 << 4) : 0)  // A
                               | (controller.buttons.east ? (1 << 5) : 0)   // B
                               | (controller.buttons.west ? (1 << 6) : 0)   // X
                               | (controller.buttons.north ? (1 << 7) : 0); // Y

    return {reinterpret_cast<uint8_t *>(&m_xinput_report), sizeof(xinput_report_t)};
}

usb_report_t InputReport::getXinputDigitalReport(const InputState &state) {
    const auto &drum = state.drum;

    getXinputBaseReport(state);

    m_xinput_report.buttons1 |= (drum.don_left.triggered ? (1 << 1) : 0)   // Dpad Down
                                | (drum.ka_left.triggered ? (1 << 2) : 0); // Dpad Left

    m_xinput_report.buttons2 |= (drum.don_right.triggered ? (1 << 4) : 0)   // A
                                | (drum.ka_right.triggered ? (1 << 5) : 0); // B

    return {reinterpret_cast<uint8_t *>(&m_xinput_report), sizeof(xinput_report_t)};
}

usb_report_t InputReport::getMidiReport(const InputState &state) {
    const auto &drum = state.drum;

    m_midi_report.status.acoustic_bass_drum = drum.don_left.triggered;
    m_midi_report.status.electric_bass_drum = drum.don_right.triggered;
    m_midi_report.status.drumsticks = drum.ka_left.triggered;
    m_midi_report.status.side_stick = drum.ka_right.triggered;

    auto convert_range = [](uint16_t in) {
        const uint16_t out = in / 256;
        return uint8_t(out > 127 ? 127 : out);
    };

    m_midi_report.velocity.acoustic_bass_drum = convert_range(drum.don_left.analog);
    m_midi_report.velocity.electric_bass_drum = convert_range(drum.don_right.analog);
    m_midi_report.velocity.drumsticks = convert_range(drum.ka_left.analog);
    m_midi_report.velocity.side_stick = convert_range(drum.ka_right.analog);

    return {reinterpret_cast<uint8_t *>(&m_midi_report), sizeof(midi_report_t)};
}

usb_report_t InputReport::getUsioReport(const InputState &state) {
    const auto &drum = state.drum;
    const auto &ctrl = state.controller;

    m_usio_input = usio_input_t{
        // Re-use the firmware's debounce / crosstalk pipeline for hit detection;
<<<<<<< HEAD
        // the USIO driver queues a peak/zero pulse for each rising edge, so the
        // game sees a complete hit when it drains the USIO input frame. `analog`
        // still carries the captured peak for future tuning.
=======
        // the USIO driver uses each rising edge to start a synthesized C3-style
        // exponential decay envelope with a fixed peak, so the game sees a real
        // piezo-like pulse rather than a held value. `analog` still carries the
        // captured peak but it is ignored by the driver for the envelope scaling.
>>>>>>> a4d12074968877d96b4cf116811aa08766dafe81
        .hit_side_left_triggered = drum.ka_left.triggered,
        .hit_center_left_triggered = drum.don_left.triggered,
        .hit_center_right_triggered = drum.don_right.triggered,
        .hit_side_right_triggered = drum.ka_right.triggered,
        .hit_side_left_peak = drum.ka_left.analog,
        .hit_center_left_peak = drum.don_left.analog,
        .hit_center_right_peak = drum.don_right.analog,
        .hit_side_right_peak = drum.ka_right.analog,
        .btn_enter = ctrl.buttons.start,
        .btn_service = ctrl.buttons.select,
        .btn_up = ctrl.dpad.up,
        .btn_down = ctrl.dpad.down,
        .btn_coin_raw = ctrl.buttons.share,
        .btn_test_raw = ctrl.buttons.home,
    };

    return {reinterpret_cast<uint8_t *>(&m_usio_input), sizeof(usio_input_t)};
}

usb_report_t InputReport::getReport(const InputState &state, usb_mode_t mode) {
    switch (mode) {
    case USB_MODE_SWITCH_TATACON:
        return getSwitchReport(state);
    case USB_MODE_PS4_TATACON:
        return getPS4Report(state);
    case USB_MODE_KEYBOARD_P1:
        return getKeyboardReport(state, Player::One);
    case USB_MODE_KEYBOARD_P2:
        return getKeyboardReport(state, Player::Two);
    case USB_MODE_XBOX360:
        return getXinputDigitalReport(state);
    case USB_MODE_MIDI:
        return getMidiReport(state);
    case USB_MODE_USIO_TAIKO:
        return getUsioReport(state);
    }

    return getUsioReport(state);
}

} // namespace Doncon::Utils
