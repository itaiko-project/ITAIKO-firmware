#include "utils/MacroStore.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include <cstring>

namespace Doncon::Utils {

namespace {

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

const uint8_t *flash_ptr(uint32_t offset) {
    return reinterpret_cast<const uint8_t *>(XIP_BASE + offset); // NOLINT(performance-no-int-to-ptr)
}

} // namespace

MacroStore::MacroStore() { load(); }

void MacroStore::load() {
    m_events.clear();

    Header header{};
    std::memcpy(&header, flash_ptr(m_flash_offset), sizeof(header));

    if (header.magic != m_magic || header.version != m_version || header.count == 0 || header.count > MAX_EVENTS) {
        return;
    }

    const size_t events_bytes = static_cast<size_t>(header.count) * sizeof(Event);
    if (sizeof(Header) + events_bytes > m_flash_sector_size) {
        return;
    }

    const uint8_t *events_ptr = flash_ptr(m_flash_offset + sizeof(Header));
    if (crc32(events_ptr, events_bytes) != header.crc32) {
        return;
    }

    m_events.resize(header.count);
    std::memcpy(m_events.data(), events_ptr, events_bytes);
}

void MacroStore::save(const std::vector<Event> &events) {
    std::vector<Event> trimmed = events;
    if (trimmed.size() > MAX_EVENTS) {
        trimmed.resize(MAX_EVENTS);
    }

    // Assemble the full sector image in RAM, then erase+program in one shot.
    std::vector<uint8_t> buffer(m_flash_sector_size, 0xFF);

    const size_t events_bytes = trimmed.size() * sizeof(Event);
    Header header{
        .magic = m_magic,
        .version = m_version,
        .count = static_cast<uint16_t>(trimmed.size()),
        .crc32 = crc32(reinterpret_cast<const uint8_t *>(trimmed.data()), events_bytes),
    };

    std::memcpy(buffer.data(), &header, sizeof(header));
    std::memcpy(buffer.data() + sizeof(Header), trimmed.data(), events_bytes);

    multicore_lockout_start_blocking();
    const uint32_t interrupts = save_and_disable_interrupts();

    flash_range_erase(m_flash_offset, m_flash_sector_size);
    flash_range_program(m_flash_offset, buffer.data(), buffer.size());

    restore_interrupts(interrupts);
    multicore_lockout_end_blocking();

    m_events = std::move(trimmed);
}

void MacroStore::clear() {
    multicore_lockout_start_blocking();
    const uint32_t interrupts = save_and_disable_interrupts();

    flash_range_erase(m_flash_offset, m_flash_sector_size);

    restore_interrupts(interrupts);
    multicore_lockout_end_blocking();

    m_events.clear();
}

} // namespace Doncon::Utils
