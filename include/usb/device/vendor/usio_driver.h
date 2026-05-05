#ifndef USB_DEVICE_VENDOR_USIO_DRIVER_H_
#define USB_DEVICE_VENDOR_USIO_DRIVER_H_

#include "usb/device_driver.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Trigger flags come from the firmware's own debounce / crosstalk pipeline
    // (the same path used by Switch / PS4 / etc), so all the existing user
    // settings still apply. The driver queues a peak sample and a zero sample
    // on each rising edge so the game can drain complete pulses once per frame.
    bool hit_side_left_triggered;
    bool hit_center_left_triggered;
    bool hit_center_right_triggered;
    bool hit_side_right_triggered;

    // Peak amplitude captured at the moment the trigger fired. Currently the
    // USIO driver uses a fixed peak so soft hits still clear the game's
    // velocity thresholds.
    uint16_t hit_side_left_peak;
    uint16_t hit_center_left_peak;
    uint16_t hit_center_right_peak;
    uint16_t hit_side_right_peak;

    bool btn_enter;   // Start
    bool btn_service; // Select
    bool btn_up;
    bool btn_down;

    bool btn_coin_raw; // Share; driver detects rising edge
    bool btn_test_raw; // Home;  driver toggles latched state on rising edge

    bool bpreader_card_present; // South/A; card is present while held
} usio_input_t;

const usbd_driver_t *get_usio_device_driver(void);

#ifdef __cplusplus
}
#endif

#endif // USB_DEVICE_VENDOR_USIO_DRIVER_H_
