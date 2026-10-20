#ifndef __USB_CONF_H
#define __USB_CONF_H

#include "at32f423_usb.h"
#include "at32f423.h"

#define USE_OTG_DEVICE_MODE
#define USB_ID 0
#define OTG_CLOCK CRM_OTGFS1_PERIPH_CLOCK
#define OTG_IRQ OTGFS1_IRQn
#define OTG_IRQ_HANDLER OTGFS1_IRQHandler
#define OTG_PIN_GPIO GPIOA
#define OTG_PIN_GPIO_CLOCK CRM_GPIOA_PERIPH_CLOCK
#define OTG_PIN_DP GPIO_PINS_12
#define OTG_PIN_DP_SOURCE GPIO_PINS_SOURCE12
#define OTG_PIN_DM GPIO_PINS_11
#define OTG_PIN_DM_SOURCE GPIO_PINS_SOURCE11
#define OTG_PIN_MUX GPIO_MUX_10
#define USB_VBUS_IGNORE
#define USB_EPT_MAX_NUM 8
#define USBD_RX_SIZE 128
#define USBD_EP0_TX_SIZE 24
#define USBD_EP1_TX_SIZE 20
#if APP_USB_FIFO_FIX
/* EP2 is the <=8-byte CDC notification endpoint. The inherited 80-word
 * allocation made all FIFOs total 352 words, above this controller's 320.
 * 32 words retain ample notification capacity and keep the total at 304. */
#define USBD_EP2_TX_SIZE 32
#else
#define USBD_EP2_TX_SIZE 80
#endif
#define USBD_EP3_TX_SIZE 20
#define USBD_EP4_TX_SIZE 20
#define USBD_EP5_TX_SIZE 20
#define USBD_EP6_TX_SIZE 20
#define USBD_EP7_TX_SIZE 20
#if APP_USB_FIFO_FIX && (USBD_RX_SIZE+USBD_EP0_TX_SIZE+USBD_EP1_TX_SIZE+USBD_EP2_TX_SIZE+USBD_EP3_TX_SIZE+USBD_EP4_TX_SIZE+USBD_EP5_TX_SIZE+USBD_EP6_TX_SIZE+USBD_EP7_TX_SIZE > OTG_FIFO_SIZE)
#error USB FIFO allocations exceed controller word capacity
#endif

void usb_delay_ms(uint32_t ms);
void usb_delay_us(uint32_t us);

#endif
