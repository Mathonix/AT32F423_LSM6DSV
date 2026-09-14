#include "usb_cdc.h"
#include "usb_conf.h"
#include "usb_core.h"
#include "usbd_int.h"
#include "cdc_class.h"
#include "cdc_desc.h"
#include "at32f423_conf.h"
#include "at32f423_crm.h"
#include "at32f423_acc.h"
#include "at32f423_misc.h"
#include <string.h>

/* Official AT32F423 USB device CDC wrapper.
 * USB_VBUS_IGNORE and the CDC control/data state machine are provided by the
 * Artery middleware; this file only adapts it to the existing application API.
 */
static otg_core_type otg_core;
#define USB_TX_RING_SIZE 2048U
#define USB_RX_RING_SIZE 512U
static uint8_t tx_ring[USB_TX_RING_SIZE];
static volatile uint16_t tx_r, tx_w;
static uint8_t rx_packet[USBD_CDC_OUT_MAXPACKET_SIZE];
static uint8_t rx_ring[USB_RX_RING_SIZE];
static volatile uint16_t rx_r, rx_w;
static uint8_t usb_tx_packet[USBD_CDC_IN_MAXPACKET_SIZE];

static uint16_t ring_used(volatile uint16_t r, volatile uint16_t w, uint16_t size)
{
  return (uint16_t)((w - r) & (size - 1U));
}

static void usb_gpio_config_local(void)
{
  gpio_init_type g;
  crm_periph_clock_enable(OTG_PIN_GPIO_CLOCK, TRUE);
  gpio_default_para_init(&g);
  g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  g.gpio_mode = GPIO_MODE_MUX;
  g.gpio_pull = GPIO_PULL_NONE;
  g.gpio_pins = OTG_PIN_DP | OTG_PIN_DM;
  gpio_init(OTG_PIN_GPIO, &g);
  gpio_pin_mux_config(OTG_PIN_GPIO, OTG_PIN_DP_SOURCE, OTG_PIN_MUX);
  gpio_pin_mux_config(OTG_PIN_GPIO, OTG_PIN_DM_SOURCE, OTG_PIN_MUX);
  /* VBUS is intentionally not configured: USB_VBUS_IGNORE is enabled. */
}

static void usb_clock48m_select_local(void)
{
  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);
  crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, TRUE);
  acc_write_c1(7980U);
  acc_write_c2(8000U);
  acc_write_c3(8020U);
  acc_calibration_mode_enable(ACC_CAL_HICKTRIM, TRUE);
}

void usb_cdc_init(void)
{
  usb_gpio_config_local();
  crm_periph_clock_enable(OTG_CLOCK, TRUE);
  usb_clock48m_select_local();
  nvic_irq_enable(OTG_IRQ, 3, 0);
  (void)usbd_init(&otg_core, USB_FULL_SPEED_CORE_ID, USB_ID,
                  &cdc_class_handler, &cdc_desc_handler);
  tx_r = tx_w = rx_r = rx_w = 0U;
}

void usb_cdc_isr(void)
{
  usbd_irq_handler(&otg_core);
}

int usb_cdc_configured(void)
{
  return (otg_core.dev.dev_config != 0U) ? 1 : 0;
}

void usb_cdc_task(void)
{
  uint16_t n, i;
  uint16_t used = ring_used(tx_r, tx_w, USB_TX_RING_SIZE);
  if(used != 0U)
  {
    n = (used > USBD_CDC_IN_MAXPACKET_SIZE) ? USBD_CDC_IN_MAXPACKET_SIZE : used;
    for(i = 0U; i < n; ++i)
      usb_tx_packet[i] = tx_ring[(tx_r + i) & (USB_TX_RING_SIZE - 1U)];
    if(usb_vcp_send_data(&otg_core.dev, usb_tx_packet, n) == SUCCESS)
      tx_r = (uint16_t)(tx_r + n);
  }

  /* Official CDC keeps one OUT packet armed. Move it to the application ring
   * only from the main context, then usb_vcp_get_rxdata re-arms the endpoint. */
  n = usb_vcp_get_rxdata(&otg_core.dev, rx_packet);
  if(n != 0U)
  {
    for(i = 0U; i < n; ++i)
    {
      uint16_t next = (uint16_t)((rx_w + 1U) & (USB_RX_RING_SIZE - 1U));
      if(next == rx_r) break;
      rx_ring[rx_w] = rx_packet[i];
      rx_w = next;
    }
  }
}

int usb_cdc_available(void)
{
  return (int)ring_used(rx_r, rx_w, USB_RX_RING_SIZE);
}

int usb_cdc_read_byte(uint8_t *ch)
{
  if(rx_r == rx_w) return 0;
  if(ch != NULL) *ch = rx_ring[rx_r];
  rx_r = (uint16_t)((rx_r + 1U) & (USB_RX_RING_SIZE - 1U));
  return 1;
}

int usb_cdc_write(const uint8_t *data, uint16_t len)
{
  uint16_t free_n, i;
  if(data == NULL || len == 0U) return 0;
  free_n = (uint16_t)((USB_TX_RING_SIZE - 1U) - ring_used(tx_r, tx_w, USB_TX_RING_SIZE));
  if(len > free_n) return -1;
  for(i = 0U; i < len; ++i)
    tx_ring[(tx_w + i) & (USB_TX_RING_SIZE - 1U)] = data[i];
  __DMB();
  tx_w = (uint16_t)(tx_w + len);
  return 0;
}
#include "bsp.h"
void usb_delay_ms(uint32_t ms) { delay_ms(ms); }
void usb_delay_us(uint32_t us) { delay_us(us); }
