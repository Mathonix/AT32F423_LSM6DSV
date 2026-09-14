#ifndef __USB_CDC_H
#define __USB_CDC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void usb_cdc_init(void);
void usb_cdc_task(void);
void usb_cdc_isr(void);
int usb_cdc_configured(void);
int usb_cdc_write(const uint8_t *data, uint16_t len);
#ifdef __cplusplus
}
#endif
#endif
