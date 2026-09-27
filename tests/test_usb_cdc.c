#include "cdc_class.h"
#include "usbd_int.h"
#include "usbd_sdr.h"
#include "test_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned stalls, sends, receives;
static uint16_t transfer_len;
static uint8_t *transfer_buffer;
void usbd_ept_open(usbd_core_type *u, uint8_t ep, uint8_t type, uint16_t size)
{ (void)u; (void)ep; (void)type; (void)size; }
void usbd_ept_close(usbd_core_type *u, uint8_t ep) { (void)u; (void)ep; }
void usbd_ept_send(usbd_core_type *u, uint8_t ep, uint8_t *p, uint16_t n)
{ (void)u; (void)ep; transfer_buffer = p; transfer_len = n; ++sends; }
void usbd_ept_recv(usbd_core_type *u, uint8_t ep, uint8_t *p, uint16_t n)
{ (void)u; (void)ep; transfer_buffer = p; transfer_len = n; ++receives; }
void usbd_ctrl_send(usbd_core_type *u, uint8_t *p, uint16_t n) { usbd_ept_send(u, 0, p, n); }
void usbd_ctrl_recv(usbd_core_type *u, uint8_t *p, uint16_t n) { usbd_ept_recv(u, 0, p, n); }
void usbd_ctrl_unsupport(usbd_core_type *u) { (void)u; ++stalls; }
void usbd_set_stall(usbd_core_type *u, uint8_t ep) { (void)u; (void)ep; ++stalls; }
void usbd_clear_stall(usbd_core_type *u, uint8_t ep) { (void)u; (void)ep; }
void usbd_ctrl_send_status(usbd_core_type *u) { (void)u; ++sends; }
uint32_t usbd_get_recv_len(usbd_core_type *u, uint8_t ep) { return u->ept_out[ep].trans_len; }
void usbd_flush_tx_fifo(usbd_core_type *u, uint8_t ep)
{ (void)u; (void)ep; }
void usb_global_interrupt_enable(otg_global_type *u, uint16_t interrupt, confirm_state state)
{ (void)u; (void)interrupt; (void)state; }

static void test_setup(usbd_core_type *dev)
{
  cdc_struct_type *cdc = (cdc_struct_type *)dev->class_handler->pdata;
  dev->class_handler->init_handler(dev);
  usb_setup_type setup = {0};
  setup.bmRequestType = 0x21; setup.bRequest = SET_LINE_CODING; setup.wLength = 7;
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_OK);
  assert(transfer_len == 7 && transfer_buffer == cdc->g_cmd);
  const uint8_t coding[7] = {0x80, 0x84, 0x1E, 0, 0, 0, 8}; /* 2 Mbps */
  memcpy(transfer_buffer, coding, 7);
  dev->ept_out[0].trans_len = 7;
  dev->class_handler->ept0_rx_handler(dev);
  assert(cdc->linecoding.bitrate == 2000000 && cdc->g_req == 0);
  setup.bmRequestType = 0xA1; setup.bRequest = GET_LINE_CODING;
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_OK);
  assert(transfer_len == 7 && memcmp(transfer_buffer, coding, 7) == 0);

  const uint16_t lengths[] = {0, 1, 6, 8, 64, 255, 65535};
  for(unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
  {
    unsigned before_send = sends, before_recv = receives;
    setup.wLength = lengths[i];
    assert(dev->class_handler->setup_handler(dev, &setup) == USB_FAIL);
    setup.bmRequestType = 0x21; setup.bRequest = SET_LINE_CODING;
    assert(dev->class_handler->setup_handler(dev, &setup) == USB_FAIL);
    assert(sends == before_send && receives == before_recv);
    setup.bmRequestType = 0xA1; setup.bRequest = GET_LINE_CODING;
  }
  setup.wLength = 7; setup.bRequest = SET_LINE_CODING; /* wrong IN direction */
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_FAIL);
  setup.bmRequestType = 0x21; setup.bRequest = GET_LINE_CODING;
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_FAIL);
  setup.bRequest = SET_LINE_CODING;
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_OK);
  dev->ept_out[0].trans_len = 6;
  memset(cdc->g_cmd, 0, sizeof(cdc->g_cmd));
  dev->class_handler->ept0_rx_handler(dev);
  assert(cdc->linecoding.bitrate == 2000000 && cdc->g_req == 0);
  setup.bRequest = 0x22; setup.wLength = 0; setup.wValue = 3;
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_OK);
  setup.wIndex = 1;
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_FAIL);
}
static void test_rx(usbd_core_type *dev)
{
  dev->usb_reg = test_map_memory(0x10000000U, 0x10000U);
  *(volatile uint32_t *)(uintptr_t)(0x10000000U + 0x1000U) = 0x44332211U;
  uint8_t buffer[12];
  memset(buffer, 0xA5, sizeof(buffer));
  dev->ept_out[0].trans_buf = buffer + 1;
  dev->ept_out[0].total_len = 7;
  dev->ept_out[0].trans_len = 0;
  dev->usb_reg->grxstsp = (USB_OUT_STS_DATA << 17) | (7U << 4);
  usbd_rxflvl_handler(dev);
  assert(buffer[0] == 0xA5 && buffer[8] == 0xA5);
  assert(buffer[1] == 0x11 && buffer[7] == 0x33);
  assert(dev->ept_out[0].trans_len == 7);

  memset(buffer, 0xA5, sizeof(buffer));
  dev->ept_out[0].trans_buf = buffer + 1;
  dev->ept_out[0].trans_len = 0;
  unsigned before = stalls;
  dev->usb_reg->grxstsp = (USB_OUT_STS_DATA << 17) | (64U << 4);
  usbd_rxflvl_handler(dev);
  assert(stalls == before + 1 && dev->ept0_sts == USB_EPT0_STALL);
  for(unsigned i = 0; i < sizeof(buffer); ++i) assert(buffer[i] == 0xA5);
  assert(dev->ept_out[0].trans_len == 0);

  dev->ept_out[0].trans_buf = NULL; dev->ept_out[0].total_len = 0;
  dev->usb_reg->grxstsp = (USB_OUT_STS_DATA << 17) | (4U << 4);
  usbd_rxflvl_handler(dev); /* data in a status-only transfer must not dereference NULL */
  dev->usb_reg->grxstsp = (USB_OUT_STS_DATA << 17) | (4U << 4) | 15U;
  usbd_rxflvl_handler(dev); /* no endpoint-array access for invalid endpoint */
  memset(dev->setup_buffer, 0xA5, sizeof(dev->setup_buffer));
  dev->usb_reg->grxstsp = (USB_SETUP_STS_DATA << 17) | (64U << 4);
  usbd_rxflvl_handler(dev);
  for(unsigned i = 0; i < sizeof(dev->setup_buffer); ++i) assert(dev->setup_buffer[i] == 0xA5);
  test_unmap_memory(dev->usb_reg, 0x10000U);
}
int main(void)
{
  usbd_core_type dev = {0};
  dev.class_handler = &cdc_class_handler;
  test_setup(&dev); test_rx(&dev);
  const uint16_t invalid_endpoints[] = {8, 15, 0x88, 0xFF, 0x0101, 0xFFFF};
  dev.conn_state = USB_CONN_STATE_CONFIGURED;
  dev.setup.bRequest = USB_STD_REQ_GET_STATUS;
  for(unsigned i = 0; i < sizeof(invalid_endpoints) / sizeof(invalid_endpoints[0]); ++i)
  {
    dev.setup.wIndex = invalid_endpoints[i];
    assert(usbd_endpoint_request(&dev) == USB_FAIL);
  }
  puts("USB control bounds/direction, DTR/RTS, pending requests and RX canaries: OK");
  return 0;
}
