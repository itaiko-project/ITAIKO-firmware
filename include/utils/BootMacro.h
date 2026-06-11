#ifndef UTILS_BOOTMACRO_H_
#define UTILS_BOOTMACRO_H_

#include "peripherals/Controller.h"

namespace Doncon::Utils::BootMacro {

// Polls the controller at startup for the macro hotkey (L + R held together).
// Mirrors BootModeSelect: only supported with InternalGpio controllers, since
// it runs before core1 (and thus any I2C GPIO expander) is available.
//
// Returns true if L+R were held for `hold_ms`. The caller decides what that
// means based on whether a macro already exists:
//   - macro exists  -> clear it
//   - no macro yet  -> arm live recording
bool check(const Peripherals::Controller::Config &controller_config, unsigned hold_ms = 300);

} // namespace Doncon::Utils::BootMacro

#endif // UTILS_BOOTMACRO_H_
