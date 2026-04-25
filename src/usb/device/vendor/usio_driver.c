#include "usb/device/vendor/usio_driver.h"

#include "device/usbd_pvt.h"
#include "tusb.h"

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
    USBD_ITF_MAX,
};

enum {
    TUD_USIO_EP_OUT = 0x01,
    TUD_USIO_EP_IN = 0x82,
    TUD_USIO_EP_STATUS_IN = 0x83,
    TUD_USIO_EP_BULK_SIZE = 64,
    TUD_USIO_EP_STATUS_SIZE = 8,
    TUD_USIO_DESC_LEN = 9 + 7 + 7 + 7,
};

// Interface class/sub/proto all 0x00 per reference (device-level is 0xFF).
#define TUD_USIO_DESCRIPTOR(_itfnum, _stridx, _epout, _epin, _epstatus)                                                \
    /* Interface */                                                                                                    \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 3, 0x00, 0x00, 0x00, _stridx,                                                  \
        /* EP OUT bulk */                                                                                              \
        7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_BULK, U16_TO_U8S_LE(TUD_USIO_EP_BULK_SIZE), 0,                        \
        /* EP IN bulk */                                                                                               \
        7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_BULK, U16_TO_U8S_LE(TUD_USIO_EP_BULK_SIZE), 0,                         \
        /* EP IN interrupt (status, never used by rpcs3) */                                                            \
        7, TUSB_DESC_ENDPOINT, _epstatus, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(TUD_USIO_EP_STATUS_SIZE), 16

#define USBD_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_USIO_DESC_LEN)

const uint8_t usio_desc_cfg[USBD_DESC_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, USBD_ITF_MAX, USBD_STR_LANGUAGE, USBD_DESC_LEN, 0xC0, 100 /* 100mA */),
    TUD_USIO_DESCRIPTOR(USBD_ITF_USIO, 0, TUD_USIO_EP_OUT, TUD_USIO_EP_IN, TUD_USIO_EP_STATUS_IN),
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

// Firmware ident (registers 0x1800 / 0x1880). "NBGI.;USIO01;Ver1.00;JPN,Multipurpose with PPG."
// This blob MUST be byte-for-byte identical to the reference or the title hangs on boot.
static const uint8_t USIO_FIRMWARE_INFO[0x180] = {
    0x4E, 0x42, 0x47, 0x49, 0x2E, 0x3B, 0x55, 0x53, 0x49, 0x4F, 0x30, 0x31, 0x3B, 0x56, 0x65, 0x72,
    0x31, 0x2E, 0x30, 0x30, 0x3B, 0x4A, 0x50, 0x4E, 0x2C, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x70, 0x75,
    0x72, 0x70, 0x6F, 0x73, 0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x50, 0x50, 0x47, 0x2E, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4E, 0x42, 0x47, 0x49, 0x31, 0x3B, 0x55, 0x53, 0x49, 0x4F, 0x30, 0x31, 0x3B, 0x56, 0x65, 0x72,
    0x31, 0x2E, 0x30, 0x30, 0x3B, 0x4A, 0x50, 0x4E, 0x2C, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x70, 0x75,
    0x72, 0x70, 0x6F, 0x73, 0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x50, 0x50, 0x47, 0x2E, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x13, 0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x03, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x75, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4E, 0x42, 0x47, 0x49, 0x32, 0x3B, 0x55, 0x53, 0x49, 0x4F, 0x30, 0x31, 0x3B, 0x56, 0x65, 0x72,
    0x31, 0x2E, 0x30, 0x30, 0x3B, 0x4A, 0x50, 0x4E, 0x2C, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x70, 0x75,
    0x72, 0x70, 0x6F, 0x73, 0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x50, 0x50, 0x47, 0x2E, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x13, 0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x03, 0x02, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
    0x75, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// ----------------------------------------------------------------------------
// Command state + input cache
// ----------------------------------------------------------------------------

enum {
    USIO_CMD_WRITE = 0x90,
    USIO_CMD_READ = 0x10,
    USIO_CMD_INIT = 0xA0,
};

enum { USIO_RESPONSE_BUF_SIZE = 512 };
enum { USIO_WRITE_BUF_SIZE = 512 };

typedef struct {
    uint8_t itf_num;
    uint8_t ep_out;
    uint8_t ep_in;
    uint8_t ep_status_in;

    CFG_TUSB_MEM_ALIGN uint8_t epout_buf[TUD_USIO_EP_BULK_SIZE];
    CFG_TUSB_MEM_ALIGN uint8_t epin_buf[TUD_USIO_EP_BULK_SIZE];

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

    // Track whether a response transfer is in progress on EP 0x82.
    // Set false when all chunks have been sent AND the transfer chain is closed.
    bool response_pending;
} usio_interface_t;

CFG_TUSB_MEM_SECTION static usio_interface_t usio_itf;

// ----------------------------------------------------------------------------
// Taiko input frame (register 0x1080, 0x60 bytes)
// ----------------------------------------------------------------------------

static void build_taiko_frame(uint8_t out[0x60]) {
    memset(out, 0, 0x60);

    const bool coin_raw = usio_itf.cached_input.btn_coin_raw;
    if (coin_raw && !usio_itf.prev_coin_raw) {
        usio_itf.coin_counter++;
    }
    usio_itf.prev_coin_raw = coin_raw;

    const bool test_raw = usio_itf.cached_input.btn_test_raw;
    if (test_raw && !usio_itf.prev_test_raw) {
        usio_itf.test_on = !usio_itf.test_on;
    }
    usio_itf.prev_test_raw = test_raw;

    uint16_t digital = 0;
    if (usio_itf.test_on) digital |= 0x0080;
    if (usio_itf.cached_input.btn_enter) digital |= 0x0200;
    if (usio_itf.cached_input.btn_down) digital |= 0x1000;
    if (usio_itf.cached_input.btn_up) digital |= 0x2000;
    if (usio_itf.cached_input.btn_service) digital |= 0x4000;

    out[0] = (uint8_t)(digital & 0xFF);
    out[1] = (uint8_t)(digital >> 8);

    out[16] = (uint8_t)(usio_itf.coin_counter & 0xFF);
    out[17] = (uint8_t)(usio_itf.coin_counter >> 8);

    const uint16_t HIT = 0x1800;
    if (usio_itf.cached_input.hit_side_left) {
        out[32] = (uint8_t)(HIT & 0xFF);
        out[33] = (uint8_t)(HIT >> 8);
    }
    if (usio_itf.cached_input.hit_center_left) {
        out[34] = (uint8_t)(HIT & 0xFF);
        out[35] = (uint8_t)(HIT >> 8);
    }
    if (usio_itf.cached_input.hit_center_right) {
        out[36] = (uint8_t)(HIT & 0xFF);
        out[37] = (uint8_t)(HIT >> 8);
    }
    if (usio_itf.cached_input.hit_side_right) {
        out[38] = (uint8_t)(HIT & 0xFF);
        out[39] = (uint8_t)(HIT >> 8);
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
        case 0x0080:
            stage_response(USIO_CARD_READER_1, sizeof(USIO_CARD_READER_1), length);
            return;
        case 0x7000:
            stage_response(NULL, 0, length);
            return;
        case 0x1080: {
            uint8_t frame[0x60];
            build_taiko_frame(frame);
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
    stage_response(NULL, 0, length);
}

static void handle_write(uint8_t channel, uint16_t reg, const uint8_t *data, uint16_t size) {
    (void)channel;
    (void)reg;
    (void)data;
    (void)size;
}

static void handle_init(uint8_t channel, uint16_t reg, uint16_t size) {
    (void)channel;
    (void)reg;
    (void)size;
}

static void pump_response_in(uint8_t rhport) {
    if (usbd_edpt_busy(rhport, usio_itf.ep_in)) return;

    if (usio_itf.response_seek >= usio_itf.response_size) {
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

// ----------------------------------------------------------------------------
// TinyUSB class-driver callbacks
// ----------------------------------------------------------------------------

static void usio_reset(uint8_t rhport) {
    (void)rhport;
    tu_memclr(&usio_itf, sizeof(usio_itf));
}

static void usio_init_cb(void) { usio_reset(0); }

static uint16_t usio_open(uint8_t rhport, tusb_desc_interface_t const *desc_itf, uint16_t max_len) {
    TU_VERIFY(desc_itf->bInterfaceClass == 0x00 && desc_itf->bInterfaceSubClass == 0x00 &&
                  desc_itf->bInterfaceProtocol == 0x00,
              0);
    TU_VERIFY(desc_itf->bNumEndpoints == 3, 0);

    const uint16_t drv_len =
        (uint16_t)(sizeof(tusb_desc_interface_t) + desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
    TU_ASSERT(max_len >= drv_len, 0);

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

    if (ep_addr == usio_itf.ep_out) {
        const uint8_t *buf = usio_itf.epout_buf;
        uint32_t size = xferred_bytes;

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
        if (usio_itf.response_pending) {
            pump_response_in(rhport);
        }
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
    // Not a "report" in the HID sense: we just cache the latest input snapshot
    // and build the Taiko frame on demand inside the command handler.
    if (report.data && report.size == sizeof(usio_input_t)) {
        memcpy(&usio_itf.cached_input, report.data, sizeof(usio_input_t));
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
