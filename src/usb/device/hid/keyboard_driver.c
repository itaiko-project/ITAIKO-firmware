#include "usb/device/hid/keyboard_driver.h"

#include "usb/device/hid/common.h"

#include "tusb.h"

const tusb_desc_device_t keyboard_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x1209,
    .idProduct = 0x3901,
    .bcdDevice = 0x0100,
    .iManufacturer = USBD_STR_MANUFACTURER,
    .iProduct = USBD_STR_PRODUCT,
    .iSerialNumber = 0,
    .bNumConfigurations = 0x01,
};

enum {
    USBD_ITF_HID_BOOT,
    USBD_ITF_HID_CONSUMER,
    USBD_ITF_CDC,
    USBD_ITF_CDC_DATA,
    USBD_ITF_MAX,
};

enum {
    USBD_HID_BOOT_EP_IN = 0x81,
    USBD_HID_CONSUMER_EP_IN = 0x82,
    USBD_CDC_EP_CMD = 0x83,
    USBD_CDC_EP_OUT = 0x04,
    USBD_CDC_EP_IN = 0x84,
    USBD_CDC_CMD_MAX_SIZE = 8,
    USBD_CDC_IN_OUT_MAX_SIZE = 64,
};

// Boot keyboard report descriptor (matches Lenovo Calliope IF0, 65 bytes)
const uint8_t keyboard_desc_hid_report[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Keyboard)
    0x19, 0xE0,       //   Usage Minimum (224)
    0x29, 0xE7,       //   Usage Maximum (231)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Cnst,Ary,Abs)
    0x95, 0x03,       //   Report Count (3)
    0x75, 0x01,       //   Report Size (1)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (1)
    0x29, 0x03,       //   Usage Maximum (3)
    0x91, 0x02,       //   Output (Data,Var,Abs)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x05,       //   Report Size (5)
    0x91, 0x01,       //   Output (Cnst,Ary,Abs)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x05, 0x07,       //   Usage Page (Keyboard)
    0x19, 0x00,       //   Usage Minimum (0)
    0x2A, 0xFF, 0x00, //   Usage Maximum (255)
    0x81, 0x00,       //   Input (Data,Ary,Abs)
    0xC0,             // End Collection
};

// Consumer / system control report descriptor (matches Lenovo Calliope IF1, 107 bytes)
const uint8_t keyboard_desc_hid_consumer_report[] = {
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, 0x01, 0x19, 0x81, 0x29, 0x83, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x01, 0xC0, 0x06, 0x0C, 0x00, 0x09, 0x01,
    0xA1, 0x01, 0x85, 0x02, 0x25, 0x01, 0x15, 0x00, 0x75, 0x01, 0x0A, 0xE2, 0x00, 0x0A, 0xEA, 0x00,
    0x0A, 0xE9, 0x00, 0x0B, 0x11, 0x00, 0x09, 0x00, 0x0A, 0xCD, 0x00, 0x0A, 0xB7, 0x00, 0x0A, 0xB6,
    0x00, 0x0A, 0xB5, 0x00, 0x95, 0x08, 0x81, 0x02, 0x0B, 0x26, 0x00, 0x09, 0x00, 0x0B, 0x27, 0x00,
    0x09, 0x00, 0x0B, 0x21, 0x00, 0x09, 0x00, 0x0B, 0x25, 0x00, 0x09, 0x00, 0x0A, 0x94, 0x01, 0x0A,
    0x92, 0x01, 0x95, 0x06, 0x81, 0x02, 0x95, 0x02, 0x81, 0x01, 0xC0,
};

#define USBD_KEYBOARD_DESC_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
uint8_t const keyboard_desc_cfg[] = {
    TUD_CONFIG_DESCRIPTOR(0x01, USBD_ITF_MAX, USBD_STR_LANGUAGE, USBD_KEYBOARD_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, USBD_MAX_POWER_MAX),
    TUD_HID_DESCRIPTOR(USBD_ITF_HID_BOOT, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(keyboard_desc_hid_report),
                       USBD_HID_BOOT_EP_IN, 8, 24),
    TUD_HID_DESCRIPTOR(USBD_ITF_HID_CONSUMER, 0, HID_ITF_PROTOCOL_NONE, sizeof(keyboard_desc_hid_consumer_report),
                       USBD_HID_CONSUMER_EP_IN, 8, 48),
    TUD_CDC_DESCRIPTOR(USBD_ITF_CDC, 0, USBD_CDC_EP_CMD, USBD_CDC_CMD_MAX_SIZE, USBD_CDC_EP_OUT, USBD_CDC_EP_IN,
                       USBD_CDC_IN_OUT_MAX_SIZE),
};

static hid_boot_keyboard_report_t last_report = {};

// Translate NKRO bitmap input (from InputReport) to boot keyboard 6KRO report.
// Input format: 32-byte bitmap, bit n == keycode n.
static void nkro_to_boot(const uint8_t *bitmap, uint16_t size, hid_boot_keyboard_report_t *out) {
    out->modifier = 0;
    out->reserved = 0;
    for (uint8_t i = 0; i < 6; i++) {
        out->keycodes[i] = 0;
    }

    uint8_t slot = 0;
    const uint16_t max_bytes = size < 32 ? size : 32;
    for (uint16_t byte = 0; byte < max_bytes; byte++) {
        uint8_t b = bitmap[byte];
        while (b) {
            uint8_t bit = __builtin_ctz(b);
            b &= (uint8_t)(b - 1);
            uint16_t keycode = byte * 8 + bit;
            if (keycode >= 0xE0 && keycode <= 0xE7) {
                out->modifier |= (uint8_t)(1 << (keycode - 0xE0));
            } else if (slot < 6 && keycode != 0) {
                out->keycodes[slot++] = (uint8_t)keycode;
            }
        }
    }
}

bool send_hid_keyboard_report(usb_report_t report) {
    bool result = false;

    hid_boot_keyboard_report_t boot_report;
    nkro_to_boot(report.data, report.size, &boot_report);

    if (tud_hid_n_ready(USBD_ITF_HID_BOOT)) {
        result = tud_hid_n_report(USBD_ITF_HID_BOOT, 0, &boot_report, sizeof(boot_report));
    }

    memcpy(&last_report, &boot_report, sizeof(boot_report));

    return result;
}

uint16_t hid_keyboard_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                                    uint16_t reqlen) {
    (void)itf;
    (void)report_id;
    (void)reqlen;

    if (report_type == HID_REPORT_TYPE_INPUT) {
        memcpy(buffer, &last_report, sizeof(hid_boot_keyboard_report_t));
        return sizeof(hid_boot_keyboard_report_t);
    }
    return 0;
}

void hid_keyboard_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                                uint16_t bufsize) {
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

const uint8_t *get_keyboard_consumer_report_desc(void) { return keyboard_desc_hid_consumer_report; }

const usbd_driver_t *get_hid_keyboard_device_driver() {
    static const usbd_driver_t hid_keyboard_device_driver = {
        .name = "Keyboard",
        .app_driver = &hid_app_driver,
        .desc_device = &keyboard_desc_device,
        .desc_cfg = keyboard_desc_cfg,
        .desc_bos = NULL,
        .send_report = send_hid_keyboard_report,
    };
    return &hid_keyboard_device_driver;
}
