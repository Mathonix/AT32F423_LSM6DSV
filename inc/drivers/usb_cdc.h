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
int usb_cdc_read_byte(uint8_t *ch);
int usb_cdc_available(void);
#ifdef __cplusplus
}
#endif
#endif
