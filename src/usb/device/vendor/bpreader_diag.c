#include "usb/device/vendor/bpreader_diag.h"

#include "hardware/pio.h"
#include "pio_ws2812/ws2812.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    BPREADER_DIAG_LED_PIN = 16,
};

static bool bpreader_diag_ready = false;

static void bpreader_diag_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!bpreader_diag_ready) {
        return;
    }

    const uint32_t pixel = ws2812_rgb_to_u32pixel(r, g, b);
    ws2812_put_pixel(pio1, pixel);
}

void bpreader_diag_init(void) {
    if (bpreader_diag_ready) {
        return;
    }

    ws2812_init(pio1, BPREADER_DIAG_LED_PIN, false);
    bpreader_diag_ready = true;
    bpreader_diag_set_rgb(0, 0, 0);
}

void bpreader_diag_mark(bpreader_diag_event_t event) {
    switch (event) {
    case BPREADER_DIAG_USB_OPEN:
        bpreader_diag_set_rgb(0, 0, 24);
        break;
    case BPREADER_DIAG_USIO_STATUS_READ:
        bpreader_diag_set_rgb(24, 12, 0);
        break;
    case BPREADER_DIAG_USIO_DATA_READ:
        bpreader_diag_set_rgb(0, 24, 24);
        break;
    case BPREADER_DIAG_USIO_DATA_WRITE:
        bpreader_diag_set_rgb(32, 0, 0);
        break;
    case BPREADER_DIAG_CDC_RX:
        bpreader_diag_set_rgb(0, 32, 0);
        break;
    }
}
