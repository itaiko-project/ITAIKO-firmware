#ifndef UTILS_MACROSTORE_H_
#define UTILS_MACROSTORE_H_

#include "hardware/flash.h"

#include <cstdint>
#include <vector>

namespace Doncon::Utils {

// Persists a single recorded macro (a timed sequence of button-state
// transitions) in a dedicated flash sector. Used by the boot-time macro
// record/replay feature for unmanned arcade auto-launch.
//
// Flash layout (from end of flash, going backward):
//   [last sector]  SettingsStore main config
//   [prev sector]  SettingsStore PS4 auth
//   [prev sector]  MacroStore macro            <- this class owns this sector
//   [5 sectors]    USIO SRAM region (see usio_driver.c USIO_FLASH_RESERVED_TAIL)
//
// m_flash_offset must stay equal to PICO_FLASH_SIZE_BYTES - 3*FLASH_SECTOR_SIZE
// so it lands between the auth sector and the USIO region without overlap.
class MacroStore {
  public:
    // Every controller input is recorded except the drum pads. A deliberate
    // 600ms simultaneous L+R hold stops recording, so a brief L+R chord can't
    // be captured, but individual L and R presses are. Packing helpers live in
    // main.cpp.
    enum Button : uint16_t {
        BTN_NORTH = 1U << 0U,
        BTN_EAST = 1U << 1U,
        BTN_SOUTH = 1U << 2U,
        BTN_WEST = 1U << 3U,
        BTN_START = 1U << 4U,
        BTN_SELECT = 1U << 5U,
        BTN_HOME = 1U << 6U,
        BTN_SHARE = 1U << 7U,
        BTN_DPAD_UP = 1U << 8U,
        BTN_DPAD_DOWN = 1U << 9U,
        BTN_DPAD_LEFT = 1U << 10U,
        BTN_DPAD_RIGHT = 1U << 11U,
        BTN_L = 1U << 12U,
        BTN_R = 1U << 13U,
    };

    // One input-state transition. delta_ms is the time elapsed since the
    // previous event (or since record-start for the first event). A single gap
    // longer than 65535ms is split across multiple no-change events.
    struct __attribute((packed)) Event {
        uint16_t delta_ms;
        uint16_t buttons; // bitmask of Button
    };
    static_assert(sizeof(Event) == 4);

    static const uint32_t MAX_EVENTS = 1000;

    MacroStore();

    [[nodiscard]] bool hasMacro() const { return !m_events.empty(); }
    [[nodiscard]] const std::vector<Event> &events() const { return m_events; }

    // Persist a freshly recorded sequence to flash. Truncates to MAX_EVENTS.
    // Requires core1's multicore_lockout victim handler to be installed.
    void save(const std::vector<Event> &events);

    // Erase the stored macro. Same multicore_lockout requirement as save().
    void clear();

  private:
    static const uint32_t m_flash_sector_size = FLASH_SECTOR_SIZE;
    static const uint32_t m_flash_offset = PICO_FLASH_SIZE_BYTES - (3 * FLASH_SECTOR_SIZE);
    static const uint32_t m_magic = 0x304D4344; // "DCM0"
    static const uint16_t m_version = 1;

    struct __attribute((packed)) Header {
        uint32_t magic;
        uint16_t version;
        uint16_t count;
        uint32_t crc32; // over the Event array that follows
    };

    std::vector<Event> m_events;

    void load();
};

} // namespace Doncon::Utils

#endif // UTILS_MACROSTORE_H_
