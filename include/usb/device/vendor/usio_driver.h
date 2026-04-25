#ifndef USB_DEVICE_VENDOR_USIO_DRIVER_H_
#define USB_DEVICE_VENDOR_USIO_DRIVER_H_

#include "usb/device_driver.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Raw piezo amplitudes streamed straight from the ADC. The game runs its own
    // peak / debounce / velocity logic on these, the same way it would against a
    // real Namco 357 IO board, so the firmware does no thresholding here.
    uint16_t hit_side_left;
    uint16_t hit_center_left;
    uint16_t hit_center_right;
    uint16_t hit_side_right;

    bool btn_enter;   // Start
    bool btn_service; // Select
    bool btn_up;
    bool btn_down;

    bool btn_coin_raw; // Share; driver detects rising edge
    bool btn_test_raw; // Home;  driver toggles latched state on rising edge
} usio_input_t;

const usbd_driver_t *get_usio_device_driver(void);

#ifdef __cplusplus
}
#endif

#endif // USB_DEVICE_VENDOR_USIO_DRIVER_H_
