#ifndef EDITION_H
#define EDITION_H

#include <cstdint>

namespace Doncon::Utils {

// A stored override selects ZhongTaiko. Otherwise GPIO25 selects the edition.
const char *getFirmwareEdition();

// Returns nullptr on success or a serial error token on failure.
const char *setZhongTaikoOverride(bool enabled, uint32_t &flash_id);

} // namespace Doncon::Utils

#endif // EDITION_H
