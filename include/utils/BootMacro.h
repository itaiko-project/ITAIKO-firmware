#ifndef UTILS_BOOTMACRO_H_
#define UTILS_BOOTMACRO_H_

#include "peripherals/Controller.h"

#include <cstdint>

namespace Doncon::Utils::BootMacro {

// Polls the controller at startup for the macro hotkey (L + R held together).
// Mirrors BootModeSelect: only supported with InternalGpio controllers, since
// it runs before core1 (and thus any I2C GPIO expander) is available.
//
// Returns true if L+R were held for `hold_ms`. The caller decides what that
// means based on whether a macro already exists:
//   - macro exists  -> clear it
//   - no macro yet  -> arm live recording
//
// On success, if `press_start_ms` is non-null it is set to the ms-since-boot of
// the moment L+R were first seen (the start of the hold). Recording uses this
// as its clock origin so the first pause is counted from when the buttons were
// pressed, not from when they were released.
bool check(const Peripherals::Controller::Config &controller_config, unsigned hold_ms = 300,
           uint32_t *press_start_ms = nullptr);

} // namespace Doncon::Utils::BootMacro

#endif // UTILS_BOOTMACRO_H_
