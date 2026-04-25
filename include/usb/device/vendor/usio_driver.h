#ifndef USB_DEVICE_VENDOR_USIO_DRIVER_H_
#define USB_DEVICE_VENDOR_USIO_DRIVER_H_

#include "usb/device_driver.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool hit_side_left;
    bool hit_center_left;
    bool hit_center_right;
    bool hit_side_right;

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
