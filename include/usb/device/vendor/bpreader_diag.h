#ifndef USB_DEVICE_VENDOR_BPREADER_DIAG_H_
#define USB_DEVICE_VENDOR_BPREADER_DIAG_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BPREADER_DIAG_USB_OPEN,
    BPREADER_DIAG_USIO_STATUS_READ,
    BPREADER_DIAG_USIO_DATA_READ,
    BPREADER_DIAG_USIO_DATA_WRITE,
    BPREADER_DIAG_CDC_RX,
} bpreader_diag_event_t;

void bpreader_diag_init(void);
void bpreader_diag_mark(bpreader_diag_event_t event);

#ifdef __cplusplus
}
#endif

#endif // USB_DEVICE_VENDOR_BPREADER_DIAG_H_
