#ifndef EDITION_H
#define EDITION_H

namespace Doncon::Utils {

// Firmware edition / branding string reported to the web configurator over serial
// so it can show the matching co-branding (e.g. "ZhongTaiko x iTAIKO").
//
// The edition is detected at runtime instead of being baked in at build time:
// bridge GPIO25 to GND on the RP2040 to flag the board as the ZhongTaiko edition.
// When the pin reads high (internal pull-up, no bridge) the stock "iTAIKO" edition
// is reported.
//
// The pin is initialised and read once on the first call; the result is cached.
const char *getFirmwareEdition();

} // namespace Doncon::Utils

#endif // EDITION_H
