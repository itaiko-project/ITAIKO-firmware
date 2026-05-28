#ifndef UTILS_BOOTMODESELECT_H_
#define UTILS_BOOTMODESELECT_H_

#include "peripherals/Controller.h"
#include "utils/SettingsStore.h"

#include <cstdint>

namespace Doncon::Utils::BootModeSelect {

struct LedConfig {
    uint8_t pin;        // GPIO driving WS2812 data line
    uint16_t count;     // number of LEDs in the chain
    uint8_t brightness; // 0-255 applied to all colors during boot pattern
    bool is_rgbw;       // true for SK6812 RGBW, false for standard WS2812
    bool reversed;      // reverse LED ordering (matches StatusLed::Config)
};

// Polls controller buttons at startup. If a recognized button is held for
// `hold_ms`, switches the persisted USB mode and plays a confirmation pattern
// on the WS2812 LED(s). Caller is responsible for calling SettingsStore::store()
// after core1 has installed its multicore_lockout victim handler.
//
// Mapping:
//   DPad Up    -> USB_MODE_SWITCH_TATACON
//   DPad Down  -> USB_MODE_PS4_TATACON
//   DPad Left  -> USB_MODE_KEYBOARD_P1
//   DPad Right -> USB_MODE_KEYBOARD_P2
//   Face North -> USB_MODE_DUALSHOCK3
//   Face South -> USB_MODE_USIO_TAIKO
//   Face West  -> USB_MODE_MIDI
//   Face East  -> USB_MODE_XBOX360
//
// Returns true if a mode change was applied.
bool run(SettingsStore &settings_store,
         const Peripherals::Controller::Config &controller_config,
         const LedConfig &led_config,
         unsigned hold_ms = 300);

} // namespace Doncon::Utils::BootModeSelect

#endif // UTILS_BOOTMODESELECT_H_
