#include "utils/Edition.h"

#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "pico/time.h"

#if PICO_RP2040
#include "hardware/structs/io_qspi.h"
#include "hardware/structs/pads_qspi.h"
#include "hardware/structs/ssi.h"
#include "hardware/structs/timer.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#endif

namespace Doncon::Utils {

namespace {
constexpr uint EDITION_DETECT_PIN = 25;
bool override_loaded = false;
bool zhongtaiko_override = false;

enum class FlashOperation { Read, Enable, Clear };
enum class FlashResult { Empty, Enabled, Unsupported, Occupied, VerifyFailed, CoreBusy };

#if PICO_RP2040
constexpr uint32_t W25Q16_JEDEC_ID = 0xEF4015;
constexpr uint32_t PUYA_P25Q16_JEDEC_ID = 0x856015;
constexpr size_t WINBOND_REGISTER_SIZE = 256;
constexpr size_t PUYA_MAX_REGISTER_SIZE = 1024;
// Reserve security register 3 exclusively for the edition marker.
constexpr uint8_t EDITION_REGISTER_ADDRESS_MID = 0x30;
constexpr uint32_t ZHONGTAIKO_MARKER = 0x31545A49; // "IZT1" in little-endian order
// Keep command buffers in SRAM as well as the code that uses them.
constexpr uint8_t read_register[] __not_in_flash("edition_spi") = {0x48, 0, EDITION_REGISTER_ADDRESS_MID, 0, 0};
constexpr uint8_t erase_register[] __not_in_flash("edition_spi") = {0x44, 0, EDITION_REGISTER_ADDRESS_MID, 0};
constexpr uint8_t program_register[] __not_in_flash("edition_spi") = {
    0x42, 0, EDITION_REGISTER_ADDRESS_MID, 0,
    static_cast<uint8_t>(ZHONGTAIKO_MARKER),
    static_cast<uint8_t>(ZHONGTAIKO_MARKER >> 8),
    static_cast<uint8_t>(ZHONGTAIKO_MARKER >> 16),
    static_cast<uint8_t>(ZHONGTAIKO_MARKER >> 24)};

// These helpers run from SRAM with both cores and interrupts excluded from XIP.
void __no_inline_not_in_flash_func(transferFlashCommand)(const uint8_t *command, size_t command_size,
                                                       uint8_t *response, size_t response_size) {
    constexpr uint32_t cs_mask = IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS;
    constexpr uint32_t cs_shift = IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB;
    hw_write_masked(&io_qspi_hw->io[1].ctrl, GPIO_OVERRIDE_LOW << cs_shift, cs_mask);
    for (size_t i = 0; i < command_size + response_size; ++i) {
        ssi_hw->dr0 = i < command_size ? command[i] : 0;
        while (!(ssi_hw->sr & SSI_SR_RFNE_BITS)) {
            tight_loop_contents();
        }
        const uint8_t value = static_cast<uint8_t>(ssi_hw->dr0);
        if (i >= command_size) {
            response[i - command_size] = value;
        }
    }
    while (ssi_hw->sr & SSI_SR_BUSY_BITS) {
        tight_loop_contents();
    }
    hw_write_masked(&io_qspi_hw->io[1].ctrl, GPIO_OVERRIDE_HIGH << cs_shift, cs_mask);
    const uint32_t deselected = timer_hw->timerawl;
    while (timer_hw->timerawl - deselected < 2u) {
        tight_loop_contents();
    }
}

FlashResult __no_inline_not_in_flash_func(readEditionRegister)(size_t read_size) {
    static uint8_t data[PUYA_MAX_REGISTER_SIZE];
    transferFlashCommand(read_register, sizeof(read_register), data, read_size);
    const uint32_t marker = uint32_t{data[0]} | (uint32_t{data[1]} << 8) |
                            (uint32_t{data[2]} << 16) | (uint32_t{data[3]} << 24);
    if (marker != ZHONGTAIKO_MARKER && marker != UINT32_MAX) {
        return FlashResult::Occupied;
    }

    // The Puya parts share a JEDEC ID and the read wraps at the end of the register.
    // Check the full read for an erased register or just our marker, including any wrap.
    bool matches_512 = true;
    bool matches_full = true;
    for (size_t i = sizeof(marker); i < read_size; ++i) {
        const size_t wrapped = i & 511u;
        const uint8_t expected_wrapped = wrapped < sizeof(marker) ? data[wrapped] : 0xFF;
        matches_512 &= data[i] == expected_wrapped;
        matches_full &= data[i] == 0xFF;
    }
    if (!matches_full && (read_size != PUYA_MAX_REGISTER_SIZE || !matches_512)) {
        return FlashResult::Occupied;
    }

    return marker == UINT32_MAX ? FlashResult::Empty : FlashResult::Enabled;
}

FlashResult __no_inline_not_in_flash_func(accessEditionRegister)(FlashOperation operation, uint32_t &flash_id) {
    const uint8_t read_id = 0x9F;
    uint8_t id[3];
    transferFlashCommand(&read_id, 1, id, sizeof(id));
    flash_id = (uint32_t{id[0]} << 16) | (uint32_t{id[1]} << 8) | id[2];
    size_t register_read_size;
    if (flash_id == W25Q16_JEDEC_ID) {
        register_read_size = WINBOND_REGISTER_SIZE;
    } else if (flash_id == PUYA_P25Q16_JEDEC_ID) {
        register_read_size = PUYA_MAX_REGISTER_SIZE;
    } else {
        return FlashResult::Unsupported;
    }

    const FlashResult current = readEditionRegister(register_read_size);
    const FlashResult desired = operation == FlashOperation::Enable ? FlashResult::Enabled : FlashResult::Empty;
    if (operation == FlashOperation::Read || current == desired || current == FlashResult::Occupied) {
        return current;
    }

    const uint8_t write_enable = 0x06;
    transferFlashCommand(&write_enable, 1, nullptr, 0);
    if (operation == FlashOperation::Enable) {
        transferFlashCommand(program_register, sizeof(program_register), nullptr, 0);
    } else {
        transferFlashCommand(erase_register, sizeof(erase_register), nullptr, 0);
    }

    // Wait until flash can serve XIP reads again before restoring boot2.
    const uint8_t read_status = 0x05;
    uint8_t status;
    do {
        transferFlashCommand(&read_status, 1, &status, 1);
    } while (status & 1u);
    const uint8_t write_disable = 0x04;
    transferFlashCommand(&write_disable, 1, nullptr, 0);
    return readEditionRegister(register_read_size) == desired ? desired : FlashResult::VerifyFailed;
}

FlashResult __no_inline_not_in_flash_func(runEditionTransaction)(FlashOperation operation, uint32_t &flash_id,
                                                                const uint32_t *boot2) {
    uint32_t pad_config[6];
    for (size_t i = 0; i < count_of(pad_config); ++i) {
        pad_config[i] = pads_qspi_hw->io[i];
    }
    rom_connect_internal_flash();
    rom_flash_exit_xip();
    const FlashResult result = accessEditionRegister(operation, flash_id);
    rom_flash_flush_cache();
    reinterpret_cast<void (*)()>(reinterpret_cast<uintptr_t>(boot2) + 1)();
    for (size_t i = 0; i < count_of(pad_config); ++i) {
        pads_qspi_hw->io[i] = pad_config[i];
    }
    return result;
}
#endif

FlashResult editionFlash(FlashOperation operation, uint32_t &flash_id) {
#if PICO_RP2040
    // Copy the board's second-stage loader before leaving XIP, as the SDK does.
    uint32_t boot2[64];
    const auto *boot2_in_flash = reinterpret_cast<const volatile uint32_t *>(XIP_BASE);
    for (size_t i = 0; i < count_of(boot2); ++i) {
        boot2[i] = boot2_in_flash[i];
    }
    if (!multicore_lockout_start_timeout_us(100'000)) {
        return FlashResult::CoreBusy;
    }
    const uint32_t interrupts = save_and_disable_interrupts();
    const FlashResult result = runEditionTransaction(operation, flash_id, boot2);
    restore_interrupts_from_disabled(interrupts);
    multicore_lockout_end_blocking();
    return result;
#else
    (void)operation;
    (void)flash_id;
    return FlashResult::Unsupported;
#endif
}

bool gpioSelectsZhongTaiko() {
    gpio_init(EDITION_DETECT_PIN);
    gpio_set_dir(EDITION_DETECT_PIN, GPIO_IN);
    gpio_pull_up(EDITION_DETECT_PIN);
    busy_wait_us(50);
    return !gpio_get(EDITION_DETECT_PIN);
}
} // namespace

const char *getFirmwareEdition() {
    static const bool strapped = gpioSelectsZhongTaiko();
    if (!override_loaded) {
        uint32_t flash_id = 0;
        const FlashResult result = editionFlash(FlashOperation::Read, flash_id);
        zhongtaiko_override = result == FlashResult::Enabled;
        override_loaded = result != FlashResult::CoreBusy;
    }
    return (strapped || zhongtaiko_override) ? "ZhongTaiko" : "iTAIKO";
}

const char *setZhongTaikoOverride(bool enabled, uint32_t &flash_id) {
    switch (editionFlash(enabled ? FlashOperation::Enable : FlashOperation::Clear, flash_id)) {
    case FlashResult::Empty:
    case FlashResult::Enabled:
        zhongtaiko_override = enabled;
        override_loaded = true;
        return nullptr;
    case FlashResult::Unsupported:
        return "UNSUPPORTED_FLASH";
    case FlashResult::Occupied:
        return "REGISTER_OCCUPIED";
    case FlashResult::VerifyFailed:
        override_loaded = false;
        return "VERIFY_FAILED";
    case FlashResult::CoreBusy:
        return "CORE_BUSY";
    }
    return "VERIFY_FAILED";
}

} // namespace Doncon::Utils
