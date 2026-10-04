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
int usb_cdc_tx_idle(void);
int usb_cdc_write(const uint8_t *data, uint16_t len);
int usb_cdc_read_byte(uint8_t *ch);
int usb_cdc_available(void);

/* WebUSB vendor interface 2 (bulk IN 0x83 / OUT 0x03, 64 B packets).
 * webusb_task() is also called from usb_cdc_task(). */
void webusb_task(void);
int webusb_configured(void);
int webusb_tx_idle(void);
int webusb_write(const uint8_t *data, uint16_t len);
int webusb_read_byte(uint8_t *ch);
int webusb_available(void);
#ifdef __cplusplus
}
#endif
#endif
