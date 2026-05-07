#include "usb/device/vendor/usio_driver.h"

#include "device/usbd_pvt.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb/device/vendor/bpreader_diag.h"
#include "usb/device/vendor/bpreader_serial.h"

#include <string.h>

// ----------------------------------------------------------------------------
// Descriptors (mirror rpcs3/Emu/Io/usio.cpp exactly so games fingerprint OK)
// ----------------------------------------------------------------------------

const tusb_desc_device_t usio_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0110,
    .bDeviceClass = 0xFF,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0xFF,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x0B9A,
    .idProduct = 0x0910,
    .bcdDevice = 0x0910,
    .iManufacturer = USBD_STR_MANUFACTURER,
    .iProduct = USBD_STR_PRODUCT,
    .iSerialNumber = USBD_STR_SERIAL,
    .bNumConfigurations = 1,
};

enum {
    USBD_ITF_USIO,
    USBD_ITF_CDC_COMM,
    USBD_ITF_CDC_DATA,
    USBD_ITF_MAX,
};

enum {
    TUD_USIO_EP_OUT = 0x01,
    TUD_USIO_EP_IN = 0x82,
    TUD_USIO_EP_STATUS_IN = 0x83,
    TUD_BPREADER_CDC_EP_NOTIF = 0x84,
    TUD_BPREADER_CDC_EP_OUT = 0x05,
    TUD_BPREADER_CDC_EP_IN = 0x85,
    TUD_USIO_EP_BULK_SIZE = 64,
    TUD_USIO_EP_STATUS_SIZE = 8,
    TUD_USIO_DESC_LEN = 9 + 7 + 7 + 7,
};

// Interface class/sub/proto all 0x00 per reference (device-level is 0xFF).
#define TUD_USIO_DESCRIPTOR(_itfnum, _stridx, _epout, _epin, _epstatus)                                                \
    /* Interface */                                                                                                    \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 3, 0x00, 0x00, 0x00, _stridx,                           /* EP OUT bulk */      \
        7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_BULK, U16_TO_U8S_LE(TUD_USIO_EP_BULK_SIZE), 0, /* EP IN bulk */       \
        7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_BULK, U16_TO_U8S_LE(TUD_USIO_EP_BULK_SIZE),                            \
        0, /* EP IN interrupt (status, never used by rpcs3) */                                                         \
        7, TUSB_DESC_ENDPOINT, _epstatus, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(TUD_USIO_EP_STATUS_SIZE), 16

#define USBD_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_USIO_DESC_LEN + TUD_CDC_DESC_LEN)

const uint8_t usio_desc_cfg[USBD_DESC_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, USBD_ITF_MAX, USBD_STR_LANGUAGE, USBD_DESC_LEN, 0xC0, 100 /* 100mA */),
    TUD_USIO_DESCRIPTOR(USBD_ITF_USIO, 0, TUD_USIO_EP_OUT, TUD_USIO_EP_IN, TUD_USIO_EP_STATUS_IN),
    TUD_CDC_DESCRIPTOR(USBD_ITF_CDC_COMM, 0, TUD_BPREADER_CDC_EP_NOTIF, 8, TUD_BPREADER_CDC_EP_OUT,
                       TUD_BPREADER_CDC_EP_IN, 64),
};

// ----------------------------------------------------------------------------
// Canonical response blobs (verbatim from rpcs3/Emu/Io/usio.cpp)
// ----------------------------------------------------------------------------

static const uint8_t USIO_KEEPALIVE[64] = {
    0x7E, 0xE4, 0x00, 0x00, 0x74, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t USIO_CARD_READER_1[16] = {
    0x02, 0x03, 0x06, 0x00, 0xFF, 0x0F, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x10, 0x00,
};

// Internal hardware identification blob (Diagnostic)
static const uint8_t USIO_FPGA_VER_IDENT[64] = {
    0x8F, 0x2A, 0x49, 0x54, 0x41, 0x49, 0x4B, 0x4F, 0x00, 0x11, 0x22, 0x33, 0xDE, 0xAD, 0xBE, 0xEF,
    0x7C, 0xA1, 0x4D, 0x93, 0x2B, 0xFE, 0x06, 0x88, 0x55, 0x19, 0x6E, 0xBD, 0x3A, 0xC4, 0x12, 0x7F,
    0x90, 0x0D, 0xE2, 0x33, 0x51, 0x47, 0xA9, 0xBC, 0x0F, 0xD1, 0x78, 0x24, 0x66, 0xAB, 0xC0, 0xD4,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
};

// Firmware ident (registers 0x1800 / 0x1880). "NBGI.;USIO01;Ver1.00;JPN,Multipurpose with PPG."
// This blob MUST be byte-for-byte identical to the reference or the title hangs on boot.
static const uint8_t USIO_FIRMWARE_INFO[0x180] = {
    0x4E, 0x42, 0x47, 0x49, 0x2E, 0x3B, 0x55, 0x53, 0x49, 0x4F, 0x30, 0x31, 0x3B, 0x56, 0x65, 0x72, 0x31, 0x2E, 0x30,
    0x30, 0x3B, 0x4A, 0x50, 0x4E, 0x2C, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x70, 0x75, 0x72, 0x70, 0x6F, 0x73, 0x65, 0x20,
    0x77, 0x69, 0x74, 0x68, 0x20, 0x50, 0x50, 0x47, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x42, 0x47, 0x49, 0x31,
    0x3B, 0x55, 0x53, 0x49, 0x4F, 0x30, 0x31, 0x3B, 0x56, 0x65, 0x72, 0x31, 0x2E, 0x30, 0x30, 0x3B, 0x4A, 0x50, 0x4E,
    0x2C, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x70, 0x75, 0x72, 0x70, 0x6F, 0x73, 0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20,
    0x50, 0x50, 0x47, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x13, 0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
    0x03, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x75, 0x6C, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x42, 0x47, 0x49, 0x32, 0x3B, 0x55, 0x53, 0x49, 0x4F,
    0x30, 0x31, 0x3B, 0x56, 0x65, 0x72, 0x31, 0x2E, 0x30, 0x30, 0x3B, 0x4A, 0x50, 0x4E, 0x2C, 0x4D, 0x75, 0x6C, 0x74,
    0x69, 0x70, 0x75, 0x72, 0x70, 0x6F, 0x73, 0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x50, 0x50, 0x47, 0x2E, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x13, 0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03, 0x02, 0x00, 0x08, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x75, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

// ----------------------------------------------------------------------------
// Command state + input cache
// ----------------------------------------------------------------------------

enum {
    USIO_CMD_WRITE = 0x90,
    USIO_CMD_READ = 0x10,
    USIO_CMD_INIT = 0xA0,
};

// Sized to fit the largest observed game read: 0x1000 bytes from page 0 at addr
// 0x1000 (the 4 KB bookkeeping block). Writes never exceed 0xB8 in the trace
// but match the read buffer for symmetry.
enum { USIO_RESPONSE_BUF_SIZE = 4352 };
enum { USIO_WRITE_BUF_SIZE = 4352 };

// SRAM emulation. rpcs3 exposes 16 pages of 64 KB each, but only pages 0/1 are
// touched by Taiko S111 and the highest accessed offset is 0x1FFF, so 8 KB per
// page is plenty. Kept at file scope so it survives USB resets (which clear
// usio_itf via tu_memclr in usio_reset).
enum {
    USIO_SRAM_PAGE_SIZE = 0x2000,
    USIO_SRAM_PAGE_COUNT = 2,
};
static uint8_t usio_sram[USIO_SRAM_PAGE_COUNT][USIO_SRAM_PAGE_SIZE];

// ----------------------------------------------------------------------------
// Flash-backed persistence for the SRAM contents.
//
// Layout (from end of flash, going backward):
//   [last sector]                        SettingsStore main config (4 KB)
//   [prev sector]                        SettingsStore PS4 auth    (4 KB)
//   [USIO_FLASH_SECTORS sectors before]  USIO SRAM region          (20 KB)
//
// We sit *before* SettingsStore's reserved area so we can never collide with
// existing user settings. The region is one header sector + four data sectors:
// the header sector holds magic/version/CRC; the four data sectors mirror
// usio_sram[] verbatim.
//
// Writes are coalesced: we mark a dirty timestamp on each SRAM write and let
// the commit happen ~USIO_FLASH_DEBOUNCE_MS later from the USB transfer
// callback (which fires at every read poll). This collapses each test-menu
// save burst (2–4 writes within a few ms) into a single flash erase+program.
// ----------------------------------------------------------------------------

enum {
    USIO_FLASH_SECTORS = 5,
    USIO_FLASH_HEADER_SIZE = FLASH_SECTOR_SIZE,
    USIO_FLASH_DATA_SIZE = (USIO_FLASH_SECTORS - 1) * FLASH_SECTOR_SIZE,
    USIO_FLASH_TOTAL_SIZE = USIO_FLASH_SECTORS * FLASH_SECTOR_SIZE,
    USIO_FLASH_DEBOUNCE_MS = 750,
};

// Sit before SettingsStore's two reserved sectors (main + auth = 8 KB).
#define USIO_FLASH_RESERVED_BY_SETTINGS_STORE (2u * FLASH_SECTOR_SIZE)
#define USIO_FLASH_OFFSET                                                                                              \
    (PICO_FLASH_SIZE_BYTES - USIO_FLASH_RESERVED_BY_SETTINGS_STORE - USIO_FLASH_TOTAL_SIZE)

#define USIO_FLASH_MAGIC 0x4F495355u // 'USIO'
#define USIO_FLASH_VERSION 1u

_Static_assert(sizeof(usio_sram) == USIO_FLASH_DATA_SIZE, "usio_sram must fit in the flash data region");

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t payload_size;
    uint32_t crc32;
} usio_flash_header_t;

_Static_assert(sizeof(usio_flash_header_t) <= FLASH_PAGE_SIZE, "header must fit in one flash page");

static bool usio_store_dirty = false;
static uint32_t usio_store_dirty_since_ms = 0;

static uint32_t usio_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            const uint32_t mask = (crc & 1u) ? 0xEDB88320u : 0u;
            crc = (crc >> 1) ^ mask;
        }
    }
    return ~crc;
}

static void usio_store_load(void) {
    const usio_flash_header_t *hdr = (const usio_flash_header_t *)(XIP_BASE + USIO_FLASH_OFFSET);
    if (hdr->magic != USIO_FLASH_MAGIC || hdr->version != USIO_FLASH_VERSION ||
        hdr->payload_size != USIO_FLASH_DATA_SIZE) {
        memset(usio_sram, 0, sizeof(usio_sram));
        return;
    }
    const uint8_t *payload = (const uint8_t *)(XIP_BASE + USIO_FLASH_OFFSET + USIO_FLASH_HEADER_SIZE);
    if (usio_crc32(payload, USIO_FLASH_DATA_SIZE) != hdr->crc32) {
        memset(usio_sram, 0, sizeof(usio_sram));
        return;
    }
    memcpy(usio_sram, payload, sizeof(usio_sram));
}

static void usio_store_commit(void) {
    // One header page (256 B) padded with 0xFF, followed by the SRAM payload.
    static uint8_t header_page[FLASH_PAGE_SIZE];
    memset(header_page, 0xFF, sizeof(header_page));
    usio_flash_header_t hdr = {
        .magic = USIO_FLASH_MAGIC,
        .version = USIO_FLASH_VERSION,
        .reserved = 0,
        .payload_size = USIO_FLASH_DATA_SIZE,
        .crc32 = usio_crc32((const uint8_t *)usio_sram, sizeof(usio_sram)),
    };
    memcpy(header_page, &hdr, sizeof(hdr));

    multicore_lockout_start_blocking();
    const uint32_t interrupts = save_and_disable_interrupts();

    flash_range_erase(USIO_FLASH_OFFSET, USIO_FLASH_TOTAL_SIZE);
    flash_range_program(USIO_FLASH_OFFSET, header_page, sizeof(header_page));
    flash_range_program(USIO_FLASH_OFFSET + USIO_FLASH_HEADER_SIZE, (const uint8_t *)usio_sram, sizeof(usio_sram));

    restore_interrupts_from_disabled(interrupts);
    multicore_lockout_end_blocking();

    usio_store_dirty = false;
}

static void usio_store_mark_dirty(void) {
    usio_store_dirty = true;
    usio_store_dirty_since_ms = to_ms_since_boot(get_absolute_time());
}

static void usio_store_tick(void) {
    if (!usio_store_dirty)
        return;
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now_ms - usio_store_dirty_since_ms) < USIO_FLASH_DEBOUNCE_MS)
        return;
    usio_store_commit();
}

void usio_clear_nvram(void) {
    memset(usio_sram, 0, sizeof(usio_sram));

    multicore_lockout_start_blocking();
    const uint32_t interrupts = save_and_disable_interrupts();

    flash_range_erase(USIO_FLASH_OFFSET, USIO_FLASH_TOTAL_SIZE);

    restore_interrupts_from_disabled(interrupts);
    multicore_lockout_end_blocking();

    usio_store_dirty = false;
}

typedef struct {
    uint8_t itf_num;
    uint8_t ep_out;
    uint8_t ep_in;
    uint8_t ep_status_in;

    CFG_TUSB_MEM_ALIGN uint8_t epout_buf[TUD_USIO_EP_BULK_SIZE];
    CFG_TUSB_MEM_ALIGN uint8_t epin_buf[TUD_USIO_EP_BULK_SIZE];
    CFG_TUSB_MEM_ALIGN uint8_t epstatus_buf[TUD_USIO_EP_STATUS_SIZE];

    // Command state machine
    bool expecting_data;
    uint8_t usio_channel;
    uint16_t usio_register;
    uint16_t usio_length_remaining;

    // Write accumulation
    uint8_t write_buf[USIO_WRITE_BUF_SIZE];
    uint16_t write_total;

    // Response staging
    uint8_t response[USIO_RESPONSE_BUF_SIZE];
    uint16_t response_size;
    uint16_t response_seek;

    // Persistent I/O state
    uint16_t coin_counter;
    bool test_on;
    bool prev_coin_raw;
    bool prev_test_raw;

    // Latest input snapshot (from core 0 via send_report hook)
    usio_input_t cached_input;

    // Previous trigger flags for edge detection. Order: side_left,
    // center_left, center_right, side_right.
    bool prev_hit_triggered[4];

    // Latched hit flags for the next Taiko input read. Order: side_left,
    // center_left, center_right, side_right.
    bool pending_hit[4];
    uint8_t last_taiko_frame[0x60];
    bool last_taiko_frame_valid;

    // Track whether a response transfer is in progress on EP 0x82.
    // Set false when all chunks have been sent AND the transfer chain is closed.
    bool response_pending;

    // Some USIO hosts issue an initial IN drain before sending the first command.
    // Arm a single idle ZLP after configuration so that pre-command drain can
    // complete, but do not keep idle ZLPs queued during normal command traffic.
    bool pre_command_idle_zlp_armed;
    bool command_out_seen;
} usio_interface_t;

enum {
    USIO_HIT_PEAK = 0xFFFF, // fixed peak for all hits
};

CFG_TUSB_MEM_SECTION static usio_interface_t usio_itf;

static void stage_response(const uint8_t *src, uint16_t src_size, uint16_t requested);

enum {
    BPREADER_CDC_RX_BUF_SIZE = 128,
    BPREADER_CDC_TX_BUF_SIZE = 128,
    BPREADER_FRAME_WAIT = 0xFFFF,
};

static uint8_t bpreader_cdc_rx_buf[BPREADER_CDC_RX_BUF_SIZE];
static uint16_t bpreader_cdc_rx_len = 0;

static uint8_t bpreader_usio_rx_buf[BPREADER_CDC_RX_BUF_SIZE];
static uint16_t bpreader_usio_rx_len = 0;
static uint8_t bpreader_usio_tx_buf[USIO_RESPONSE_BUF_SIZE];
static uint16_t bpreader_usio_tx_len = 0;

static uint16_t bpreader_frame_len(const uint8_t *buf, uint16_t len) {
    if (len == 0) {
        return 0;
    }

    if (buf[0] == 0x55) {
        return 1;
    }

    if (buf[0] != 0x00) {
        return 0;
    }
    if (len == 1) {
        return BPREADER_FRAME_WAIT;
    }

    if (buf[1] != 0x00) {
        return 0;
    }
    if (len == 2) {
        return BPREADER_FRAME_WAIT;
    }

    if (buf[2] != 0xFF) {
        return 0;
    }
    if (len < 5) {
        return BPREADER_FRAME_WAIT;
    }

    if (buf[3] == 0x00 && buf[4] == 0xFF) {
        if (len < 6) {
            return BPREADER_FRAME_WAIT;
        }
        return buf[5] == 0x00 ? 6 : 0;
    }

    return (uint16_t)buf[3] + 7;
}

static void bpreader_cdc_pump(void) {
    uint8_t tx[BPREADER_CDC_TX_BUF_SIZE];

    while (bpreader_cdc_rx_len > 0) {
        const uint16_t frame_len = bpreader_frame_len(bpreader_cdc_rx_buf, bpreader_cdc_rx_len);
        if (frame_len == BPREADER_FRAME_WAIT) {
            return;
        }
        if (frame_len == 0) {
            memmove(bpreader_cdc_rx_buf, &bpreader_cdc_rx_buf[1], --bpreader_cdc_rx_len);
            continue;
        }
        if (bpreader_cdc_rx_len < frame_len) {
            return;
        }

        const size_t tx_len = bpreader_serial_process(bpreader_cdc_rx_buf, frame_len, tx, sizeof(tx));
        if (tx_len > 0) {
            tud_cdc_n_write(0, tx, (uint32_t)tx_len);
            tud_cdc_n_write_flush(0);
        }

        bpreader_cdc_rx_len = (uint16_t)(bpreader_cdc_rx_len - frame_len);
        if (bpreader_cdc_rx_len > 0) {
            memmove(bpreader_cdc_rx_buf, &bpreader_cdc_rx_buf[frame_len], bpreader_cdc_rx_len);
        }
    }
}

static void bpreader_usio_queue_tx(const uint8_t *tx, size_t tx_len) {
    if (!tx || tx_len == 0) {
        return;
    }

    const size_t cap = sizeof(bpreader_usio_tx_buf) - bpreader_usio_tx_len;
    if (tx_len > cap) {
        bpreader_usio_tx_len = 0;
    }

    if (tx_len <= sizeof(bpreader_usio_tx_buf)) {
        memcpy(&bpreader_usio_tx_buf[bpreader_usio_tx_len], tx, tx_len);
        bpreader_usio_tx_len = (uint16_t)(bpreader_usio_tx_len + tx_len);
    }
}

static void bpreader_usio_feed(const uint8_t *rx, uint16_t rx_len) {
    uint8_t tx[BPREADER_CDC_TX_BUF_SIZE];

    if (!rx || rx_len == 0) {
        return;
    }

    if ((uint32_t)bpreader_usio_rx_len + rx_len > sizeof(bpreader_usio_rx_buf)) {
        bpreader_usio_rx_len = 0;
    }

    memcpy(&bpreader_usio_rx_buf[bpreader_usio_rx_len], rx, rx_len);
    bpreader_usio_rx_len = (uint16_t)(bpreader_usio_rx_len + rx_len);

    while (bpreader_usio_rx_len > 0) {
        const uint16_t frame_len = bpreader_frame_len(bpreader_usio_rx_buf, bpreader_usio_rx_len);
        if (frame_len == BPREADER_FRAME_WAIT) {
            return;
        }
        if (frame_len == 0) {
            memmove(bpreader_usio_rx_buf, &bpreader_usio_rx_buf[1], --bpreader_usio_rx_len);
            continue;
        }
        if (bpreader_usio_rx_len < frame_len) {
            return;
        }

        const size_t tx_len = bpreader_serial_process(bpreader_usio_rx_buf, frame_len, tx, sizeof(tx));
        bpreader_usio_queue_tx(tx, tx_len);

        bpreader_usio_rx_len = (uint16_t)(bpreader_usio_rx_len - frame_len);
        if (bpreader_usio_rx_len > 0) {
            memmove(bpreader_usio_rx_buf, &bpreader_usio_rx_buf[frame_len], bpreader_usio_rx_len);
        }
    }
}

static void bpreader_usio_stage_read(uint16_t length) {
    const uint16_t n = (bpreader_usio_tx_len < length) ? bpreader_usio_tx_len : length;
    stage_response(bpreader_usio_tx_buf, n, length);

    bpreader_usio_tx_len = (uint16_t)(bpreader_usio_tx_len - n);
    if (bpreader_usio_tx_len > 0) {
        memmove(bpreader_usio_tx_buf, &bpreader_usio_tx_buf[n], bpreader_usio_tx_len);
    }
}

// ----------------------------------------------------------------------------
// Taiko input frame (registers 0x1080 / 0x1100, 0x60 bytes)
// ----------------------------------------------------------------------------

static void build_taiko_frame(uint8_t out[0x60], bool advance_input) {
    memset(out, 0, 0x60);

    const bool coin_raw = usio_itf.cached_input.btn_coin_raw;
    if (advance_input && coin_raw && !usio_itf.prev_coin_raw) {
        usio_itf.coin_counter++;
    }
    if (advance_input) {
        usio_itf.prev_coin_raw = coin_raw;
    }

    const bool test_raw = usio_itf.cached_input.btn_test_raw;
    if (advance_input && test_raw && !usio_itf.prev_test_raw) {
        usio_itf.test_on = !usio_itf.test_on;
    }
    if (advance_input) {
        usio_itf.prev_test_raw = test_raw;
    }

    uint16_t digital = 0;
    if (usio_itf.test_on)
        digital |= 0x0080;
    if (usio_itf.cached_input.btn_enter)
        digital |= 0x0200;
    if (usio_itf.cached_input.btn_down)
        digital |= 0x1000;
    if (usio_itf.cached_input.btn_up)
        digital |= 0x2000;
    if (usio_itf.cached_input.btn_service)
        digital |= 0x4000;

    out[0] = (uint8_t)(digital & 0xFF);
    out[1] = (uint8_t)(digital >> 8);

    out[16] = (uint8_t)(usio_itf.coin_counter & 0xFF);
    out[17] = (uint8_t)(usio_itf.coin_counter >> 8);

    uint16_t sample_values[4] = {0, 0, 0, 0};

    if (advance_input) {
        for (uint8_t input = 0; input < 4; input++) {
            if (usio_itf.pending_hit[input]) {
                sample_values[input] = USIO_HIT_PEAK;
                usio_itf.pending_hit[input] = false;
            }
        }
    }

    for (uint8_t input = 0; input < 4; input++) {
        const uint16_t v = sample_values[input];
        const uint8_t off = (uint8_t)(32 + input * 2);
        out[off] = (uint8_t)(v & 0xFF);
        out[off + 1] = (uint8_t)(v >> 8);
    }

    if (advance_input) {
        memcpy(usio_itf.last_taiko_frame, out, sizeof(usio_itf.last_taiko_frame));
        usio_itf.last_taiko_frame_valid = true;
    }
}

// ----------------------------------------------------------------------------
// Read / write / init command handlers
// ----------------------------------------------------------------------------

static void stage_response(const uint8_t *src, uint16_t src_size, uint16_t requested) {
    const uint16_t cap = (requested <= USIO_RESPONSE_BUF_SIZE) ? requested : USIO_RESPONSE_BUF_SIZE;
    memset(usio_itf.response, 0, cap);
    if (src && src_size) {
        const uint16_t n = (src_size < cap) ? src_size : cap;
        memcpy(usio_itf.response, src, n);
    }
    usio_itf.response_size = cap;
    usio_itf.response_seek = 0;
    usio_itf.response_pending = true;
}

static void handle_read(uint8_t channel, uint16_t reg, uint16_t length) {
    if (channel == 0) {
        switch (reg) {
        case 0x0000:
            stage_response(USIO_KEEPALIVE, sizeof(USIO_KEEPALIVE), length);
            return;
        case 0x0080: {
            bpreader_diag_mark(BPREADER_DIAG_USIO_STATUS_READ);
            uint8_t status[sizeof(USIO_CARD_READER_1)];
            memcpy(status, USIO_CARD_READER_1, sizeof(status));
            status[2] = (uint8_t)((bpreader_usio_tx_len > 0xFF) ? 0xFF : bpreader_usio_tx_len);
            stage_response(status, sizeof(status), length);
            return;
        }
        case 0x7000:
            bpreader_diag_mark(BPREADER_DIAG_USIO_DATA_READ);
            bpreader_usio_stage_read(length);
            return;
        case 0x4954: // HW Ident Check
            stage_response(USIO_FPGA_VER_IDENT, sizeof(USIO_FPGA_VER_IDENT), length);
            return;
        case 0x1080: {
            uint8_t frame[0x60];
            build_taiko_frame(frame, true);
            stage_response(frame, sizeof(frame), length);
            return;
        }
        case 0x1100: {
            uint8_t frame[0x60];
            if (usio_itf.last_taiko_frame_valid) {
                memcpy(frame, usio_itf.last_taiko_frame, sizeof(frame));
            } else {
                build_taiko_frame(frame, false);
            }
            stage_response(frame, sizeof(frame), length);
            return;
        }
        case 0x1800:
        case 0x1880: {
            const uint16_t off = reg - 0x1800;
            if (off < sizeof(USIO_FIRMWARE_INFO)) {
                stage_response(&USIO_FIRMWARE_INFO[off], (uint16_t)(sizeof(USIO_FIRMWARE_INFO) - off), length);
            } else {
                stage_response(NULL, 0, length);
            }
            return;
        }
        default:
            stage_response(NULL, 0, length);
            return;
        }
    }

    // Channels >= 2 map to SRAM pages. Mirrors usb_device_usio::usio_read in
    // rpcs3/Emu/Io/usio.cpp.
    if (channel >= 2) {
        const uint8_t page = (uint8_t)(channel - 2);
        const uint32_t end = (uint32_t)reg + length;
        if (length > 0 && page < USIO_SRAM_PAGE_COUNT && end <= USIO_SRAM_PAGE_SIZE) {
            stage_response(&usio_sram[page][reg], length, length);
            return;
        }
    }

    // Channel 1 (firmware-update endpoint) and out-of-range SRAM accesses both
    // fall through to an empty response.
    stage_response(NULL, 0, length);
}

static void handle_write(uint8_t channel, uint16_t reg, const uint8_t *data, uint16_t size) {
    if (channel == 0 && (reg == 0x7000 || reg == 0x7400)) {
        bpreader_diag_mark(BPREADER_DIAG_USIO_DATA_WRITE);
        bpreader_usio_feed(data, size);
    }

    if (channel >= 2 && size > 0 && data != NULL) {
        const uint8_t page = (uint8_t)(channel - 2);
        const uint32_t end = (uint32_t)reg + size;
        if (page < USIO_SRAM_PAGE_COUNT && end <= USIO_SRAM_PAGE_SIZE) {
            if (memcmp(&usio_sram[page][reg], data, size) != 0) {
                memcpy(&usio_sram[page][reg], data, size);
                usio_store_mark_dirty();
            }
        }
    }
    // Channel 0 register writes (lamps, hopper, card reader) and channel 1
    // (firmware update) are intentionally dropped — rpcs3 only traces them.
}

static void handle_init(uint8_t channel, uint16_t reg, uint16_t size) {
    (void)size;
    if (channel == 0 && reg == 0x000A) {
        // ClearSram: wipe all backing pages. Matches usio_init in rpcs3.
        memset(usio_sram, 0, sizeof(usio_sram));
        usio_store_mark_dirty();
    }
    // reg 0x0008 (USIO Reset) and other init sub-commands are no-ops; the USB
    // stack handles bus-level reset separately.
}

static void pump_response_in(uint8_t rhport) {
    if (usbd_edpt_busy(rhport, usio_itf.ep_in))
        return;

    if (usio_itf.response_seek >= usio_itf.response_size) {
        if (usio_itf.response_pending && usio_itf.response_size == 0) {
            usbd_edpt_xfer(rhport, usio_itf.ep_in, NULL, 0);
            usio_itf.response_pending = false;
            return;
        }

        if (usio_itf.response_pending && usio_itf.response_size > 0 &&
            (usio_itf.response_size % TUD_USIO_EP_BULK_SIZE) == 0) {
            usbd_edpt_xfer(rhport, usio_itf.ep_in, NULL, 0);
            usio_itf.response_pending = false;
            return;
        }
        usio_itf.response_pending = false;
        return;
    }

    const uint16_t remain = usio_itf.response_size - usio_itf.response_seek;
    const uint16_t chunk = (remain > TUD_USIO_EP_BULK_SIZE) ? TUD_USIO_EP_BULK_SIZE : remain;
    memcpy(usio_itf.epin_buf, &usio_itf.response[usio_itf.response_seek], chunk);
    usbd_edpt_xfer(rhport, usio_itf.ep_in, usio_itf.epin_buf, chunk);
    usio_itf.response_seek += chunk;
}

static void arm_pre_command_idle_zlp(uint8_t rhport) {
    if (usio_itf.command_out_seen || usio_itf.pre_command_idle_zlp_armed || usbd_edpt_busy(rhport, usio_itf.ep_in))
        return;

    usio_itf.pre_command_idle_zlp_armed = usbd_edpt_xfer(rhport, usio_itf.ep_in, NULL, 0);
}

// Real PS3 (and unpatched RPCS3) submits a zero-length IN URB to drain the read
// endpoint after a CMD_WRITE finishes. libusb cannot complete that URB until
// the device sends a ZLP, so the host hangs forever if we stay silent. Queue a
// one-shot ZLP on EP_IN whenever a write completes and nothing else is pending.
static void arm_post_write_idle_zlp(uint8_t rhport) {
    if (usio_itf.response_pending || usio_itf.pre_command_idle_zlp_armed ||
        usbd_edpt_busy(rhport, usio_itf.ep_in))
        return;

    usio_itf.pre_command_idle_zlp_armed = usbd_edpt_xfer(rhport, usio_itf.ep_in, NULL, 0);
}

static void arm_status_in(uint8_t rhport) {
    if (!usio_itf.ep_status_in || usbd_edpt_busy(rhport, usio_itf.ep_status_in))
        return;

    memset(usio_itf.epstatus_buf, 0, sizeof(usio_itf.epstatus_buf));
    (void)usbd_edpt_xfer(rhport, usio_itf.ep_status_in, usio_itf.epstatus_buf, sizeof(usio_itf.epstatus_buf));
}

// ----------------------------------------------------------------------------
// TinyUSB class-driver callbacks
// ----------------------------------------------------------------------------

static void usio_reset(uint8_t rhport) {
    (void)rhport;
    tu_memclr(&usio_itf, sizeof(usio_itf));
}

static void usio_init_cb(void) {
    usio_reset(0);
    bpreader_diag_init();
    usio_store_load();
    bpreader_serial_init();
}

static uint16_t usio_open(uint8_t rhport, tusb_desc_interface_t const *desc_itf, uint16_t max_len) {
    TU_VERIFY(desc_itf->bInterfaceClass == 0x00 && desc_itf->bInterfaceSubClass == 0x00 &&
                  desc_itf->bInterfaceProtocol == 0x00,
              0);
    TU_VERIFY(desc_itf->bNumEndpoints == 3, 0);

    const uint16_t drv_len =
        (uint16_t)(sizeof(tusb_desc_interface_t) + desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
    TU_ASSERT(max_len >= drv_len, 0);

    // Clear command/response state. usio_reset() (the TinyUSB bus-reset hook)
    // already does this, but rpcs3 closing without a physical unplug leaves the
    // bus electrically attached: no reset fires, yet the host re-enumerates on
    // the next launch via SET_CONFIGURATION → usio_open. Without this wipe the
    // first command after reattach gets misparsed as the tail of a write
    // payload that was in-flight when rpcs3 disappeared, and the game's
    // boot-time USIO check times out.
    //
    // SRAM contents (usio_sram, file scope) and latched input state are kept
    // intentionally — those represent persistent device state, not the
    // transient command pipeline.
    usio_itf.expecting_data = false;
    usio_itf.usio_length_remaining = 0;
    usio_itf.write_total = 0;
    usio_itf.response_size = 0;
    usio_itf.response_seek = 0;
    usio_itf.response_pending = false;
    usio_itf.pre_command_idle_zlp_armed = false;
    usio_itf.command_out_seen = false;

    usio_itf.itf_num = desc_itf->bInterfaceNumber;

    uint8_t const *p_desc = tu_desc_next(desc_itf);
    for (uint8_t i = 0; i < desc_itf->bNumEndpoints; ++i) {
        TU_ASSERT(p_desc[1] == TUSB_DESC_ENDPOINT, 0);
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p_desc;
        TU_ASSERT(usbd_edpt_open(rhport, ep), 0);

        if (ep->bEndpointAddress == TUD_USIO_EP_OUT) {
            usio_itf.ep_out = ep->bEndpointAddress;
        } else if (ep->bEndpointAddress == TUD_USIO_EP_IN) {
            usio_itf.ep_in = ep->bEndpointAddress;
        } else if (ep->bEndpointAddress == TUD_USIO_EP_STATUS_IN) {
            usio_itf.ep_status_in = ep->bEndpointAddress;
        }
        p_desc = tu_desc_next(p_desc);
    }

    // Start listening on the OUT endpoint.
    TU_ASSERT(usbd_edpt_xfer(rhport, usio_itf.ep_out, usio_itf.epout_buf, sizeof(usio_itf.epout_buf)), 0);
    bpreader_diag_mark(BPREADER_DIAG_USB_OPEN);
    arm_status_in(rhport);
    arm_pre_command_idle_zlp(rhport);
    return drv_len;
}

static bool usio_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    (void)rhport;
    (void)stage;
    (void)request;
    // No class-specific control transfers; let the stack STALL / default-handle.
    return false;
}

static bool usio_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    TU_ASSERT(result == XFER_RESULT_SUCCESS);

    // Opportunistic deferred flush. The xfer callback fires on every USIO poll
    // cycle (~30 Hz from rpcs3 trace), so dirty SRAM gets committed within one
    // debounce window after the last write.
    usio_store_tick();

    if (ep_addr == usio_itf.ep_out) {
        const uint8_t *buf = usio_itf.epout_buf;
        uint32_t size = xferred_bytes;
        usio_itf.command_out_seen = true;

        if (usio_itf.expecting_data) {
            // Accumulate write payload.
            uint16_t take = (size < usio_itf.usio_length_remaining) ? (uint16_t)size : usio_itf.usio_length_remaining;
            if ((uint32_t)usio_itf.write_total + take > sizeof(usio_itf.write_buf)) {
                take = (uint16_t)(sizeof(usio_itf.write_buf) - usio_itf.write_total);
            }
            memcpy(&usio_itf.write_buf[usio_itf.write_total], buf, take);
            usio_itf.write_total += take;
            usio_itf.usio_length_remaining -= take;

            if (usio_itf.usio_length_remaining == 0) {
                usio_itf.expecting_data = false;
                handle_write(usio_itf.usio_channel, usio_itf.usio_register, usio_itf.write_buf, usio_itf.write_total);
                arm_post_write_idle_zlp(rhport);
            }
        } else if (size == 6) {
            // Command frame.
            const uint8_t cmd_byte = buf[0];
            usio_itf.usio_channel = cmd_byte & 0x0F;
            usio_itf.usio_register = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
            const uint16_t length = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
            usio_itf.usio_length_remaining = length;

            if ((cmd_byte & USIO_CMD_WRITE) == USIO_CMD_WRITE) {
                const uint8_t expected_check = (uint8_t)((~(usio_itf.usio_register >> 8)) & 0xF0);
                if (buf[1] == expected_check) {
                    usio_itf.expecting_data = (length > 0);
                    usio_itf.write_total = 0;
                    if (!usio_itf.expecting_data) {
                        handle_write(usio_itf.usio_channel, usio_itf.usio_register, NULL, 0);
                        arm_post_write_idle_zlp(rhport);
                    }
                }
                // Bad checksum: silently drop (matches rpcs3 behaviour).
            } else if ((cmd_byte & USIO_CMD_READ) == USIO_CMD_READ) {
                handle_read(usio_itf.usio_channel, usio_itf.usio_register, length);
                pump_response_in(rhport);
            } else if ((cmd_byte & USIO_CMD_INIT) == USIO_CMD_INIT) {
                handle_init(usio_itf.usio_channel, usio_itf.usio_register, length);
            }
        }

        // Re-arm OUT endpoint.
        TU_ASSERT(usbd_edpt_xfer(rhport, usio_itf.ep_out, usio_itf.epout_buf, sizeof(usio_itf.epout_buf)));
    } else if (ep_addr == usio_itf.ep_in) {
        // Previous IN chunk delivered.
        if (usio_itf.pre_command_idle_zlp_armed) {
            usio_itf.pre_command_idle_zlp_armed = false;
        }
        if (usio_itf.response_pending) {
            pump_response_in(rhport);
        }
    } else if (ep_addr == usio_itf.ep_status_in) {
        arm_status_in(rhport);
    }

    return true;
}

static const usbd_class_driver_t usio_app_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "USIO",
#endif
    .init = usio_init_cb,
    .reset = usio_reset,
    .open = usio_open,
    .control_xfer_cb = usio_control_xfer_cb,
    .xfer_cb = usio_xfer_cb,
    .sof = NULL,
};

// ----------------------------------------------------------------------------
// Public driver hook
// ----------------------------------------------------------------------------

static bool send_usio_report(usb_report_t report) {
    // Not a "report" in the HID sense: we cache the latest input snapshot here
    // and build the Taiko frame on demand inside the command handler. We also
    // edge-detect each pad's trigger flag so the next 0x1080 read can expose a
    // single full-scale sample.
    if (!report.data || report.size != sizeof(usio_input_t)) {
        return true;
    }
    memcpy(&usio_itf.cached_input, report.data, sizeof(usio_input_t));
    bpreader_serial_set_card_present(usio_itf.cached_input.bpreader_card_present);

    const bool triggered[4] = {
        usio_itf.cached_input.hit_side_left_triggered,
        usio_itf.cached_input.hit_center_left_triggered,
        usio_itf.cached_input.hit_center_right_triggered,
        usio_itf.cached_input.hit_side_right_triggered,
    };
    for (int i = 0; i < 4; i++) {
        if (triggered[i] && !usio_itf.prev_hit_triggered[i]) {
            usio_itf.pending_hit[i] = true;
        }
        usio_itf.prev_hit_triggered[i] = triggered[i];
    }

    return true;
}

const usbd_driver_t *get_usio_device_driver(void) {
    static const usbd_driver_t usio_device_driver = {
        .name = "USIO Taiko",
        .app_driver = &usio_app_driver,
        .desc_device = &usio_desc_device,
        .desc_cfg = usio_desc_cfg,
        .desc_bos = NULL,
        .send_report = send_usio_report,
    };
    return &usio_device_driver;
}

void tud_cdc_rx_cb(uint8_t itf) {
    if (itf != 0) {
        return;
    }

    bpreader_diag_mark(BPREADER_DIAG_CDC_RX);

    while (tud_cdc_n_available(0)) {
        const uint32_t space = sizeof(bpreader_cdc_rx_buf) - bpreader_cdc_rx_len;
        if (space == 0) {
            bpreader_cdc_rx_len = 0;
            return;
        }

        const uint32_t count = tud_cdc_n_read(0, &bpreader_cdc_rx_buf[bpreader_cdc_rx_len], space);
        bpreader_cdc_rx_len = (uint16_t)(bpreader_cdc_rx_len + count);
        bpreader_cdc_pump();
    }
}
