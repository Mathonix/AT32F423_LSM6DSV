#include "cdc_class.h"
#include "cdc_desc.h"
#include "usbd_int.h"
#include "usbd_sdr.h"
#include "test_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef USB_TEST_BOOTLOADER
/* Same sources built with bootloader/inc first (bootloader USB identity). */
#define EXPECT_BCD_DEVICE 0x0280U
#define EXPECT_PRODUCT    "LSM6DSV Bootloader"
#define EXPECT_INTERFACE  "LSM6DSV Bootloader WebUSB"
#else
#define EXPECT_BCD_DEVICE 0x0201U
#define EXPECT_PRODUCT    "LSM6DSV USB CDC"
#define EXPECT_INTERFACE  "LSM6DSV WebUSB"
#endif

static unsigned stalls, sends, receives;
static uint16_t transfer_len;
static uint8_t *transfer_buffer;
static uint8_t last_ep, opened[16], open_count, close_count;
void usbd_ept_open(usbd_core_type *u, uint8_t ep, uint8_t type, uint16_t size)
{ (void)u; (void)type; assert(size <= 64U); if(open_count < 16U) opened[open_count] = ep; ++open_count; }
void usbd_ept_close(usbd_core_type *u, uint8_t ep) { (void)u; (void)ep; ++close_count; }
void usbd_ept_send(usbd_core_type *u, uint8_t ep, uint8_t *p, uint16_t n)
{ (void)u; last_ep = ep; transfer_buffer = p; transfer_len = n; ++sends; }
void usbd_ept_recv(usbd_core_type *u, uint8_t ep, uint8_t *p, uint16_t n)
{ (void)u; last_ep = ep; transfer_buffer = p; transfer_len = n; ++receives; }
void usbd_ctrl_send(usbd_core_type *u, uint8_t *p, uint16_t n) { usbd_ept_send(u, 0, p, n); }
void usbd_ctrl_recv(usbd_core_type *u, uint8_t *p, uint16_t n) { usbd_ept_recv(u, 0, p, n); }
void usbd_ctrl_unsupport(usbd_core_type *u) { (void)u; ++stalls; }
void usbd_set_stall(usbd_core_type *u, uint8_t ep) { (void)u; (void)ep; ++stalls; }
void usbd_clear_stall(usbd_core_type *u, uint8_t ep) { (void)u; (void)ep; }
void usbd_ctrl_send_status(usbd_core_type *u) { (void)u; ++sends; }
void usbd_set_device_addr(usbd_core_type *u, uint8_t addr) { (void)u; (void)addr; }
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

static int str_desc_eq(const usbd_desc_t *s, const char *str)
{
  const size_t n = strlen(str);
  if(s->length != 2U + 2U * n || s->descriptor[0] != s->length || s->descriptor[1] != 3U) return 0;
  for(size_t i = 0; i < n; ++i)
    if(s->descriptor[2U + 2U * i] != (uint8_t)str[i] || s->descriptor[3U + 2U * i] != 0U) return 0;
  return 1;
}
static void test_descriptors(void)
{
  usbd_desc_t *dd = cdc_desc_handler.get_device_descriptor();
  const uint8_t *d = dd->descriptor;
  assert(dd->length == 18U && d[0] == 18U && d[1] == 1U);
  assert(d[4] == 0xEFU && d[5] == 0x02U && d[6] == 0x01U);         /* IAD composite */
  assert((d[8] | (d[9] << 8)) == 0x2E3CU && (d[10] | (d[11] << 8)) == 0xF401U);
  assert((d[12] | (d[13] << 8)) == EXPECT_BCD_DEVICE);
  assert(str_desc_eq(cdc_desc_handler.get_device_product_string(), EXPECT_PRODUCT));
  assert(str_desc_eq(cdc_desc_handler.get_device_interface_string(), EXPECT_INTERFACE));

  usbd_desc_t *cd = cdc_desc_handler.get_device_configuration();
  const uint8_t *c = cd->descriptor;
  const uint16_t total = (uint16_t)(c[2] | (c[3] << 8));
  assert(USBD_CDC_CONFIG_DESC_SIZE == 98U);
  assert(cd->length == total && total == USBD_CDC_CONFIG_DESC_SIZE);
  assert(c[4] == 3U && c[7] == 0xC0U);                                 /* 3 IFs, attrs unchanged */

  unsigned off = 0, ifaces = 0, eps = 0, iads = 0;
  uint8_t ep_seen[256] = {0}, cur_if = 0xFF, if2_eps = 0;
  int if_nep = -1;
  while(off < total)
  {
    const uint8_t len = c[off], type = c[off + 1];
    assert(len >= 2U && off + len <= total);
    if(type == 0x0BU) { assert(len == 8U && ifaces == 0U && c[off + 2] == 0U && c[off + 3] == 2U && c[off + 4] == 0x02U); ++iads; }
    if(type == 0x04U)
    {
      assert(len == 9U && c[off + 2] == ifaces && c[off + 3] == 0U);
      if(if_nep >= 0) assert(if_nep == 0);
      cur_if = c[off + 2]; if_nep = c[off + 4]; ++ifaces;
      if(cur_if == 2U) assert(c[off + 5] == 0xFFU && c[off + 6] == 0U && c[off + 7] == 0U && c[off + 4] == 2U);
    }
    if(type == 0x05U)
    {
      assert(len == 7U && if_nep > 0); --if_nep; ++eps;
      assert(ep_seen[c[off + 2]] == 0U); ep_seen[c[off + 2]] = 1U;   /* unique addresses */
      assert((unsigned)(c[off + 4] | (c[off + 5] << 8)) <= 64U);
      if(cur_if == 2U) { assert(c[off + 3] == 0x02U && (c[off + 4] | (c[off + 5] << 8)) == 64U); ++if2_eps; }
    }
    off += len;
  }
  assert(off == total && ifaces == 3U && iads == 1U && eps == 5U && if_nep == 0);
  assert(ep_seen[0x82] && ep_seen[0x81] && ep_seen[0x01] && ep_seen[0x83] && ep_seen[0x03] && if2_eps == 2U);
}

static void test_webusb(usbd_core_type *dev)
{
  cdc_struct_type *cdc = (cdc_struct_type *)dev->class_handler->pdata;
  uint8_t out[64], pkt[64];
  open_count = 0; close_count = 0;
  dev->class_handler->init_handler(dev);
  assert(open_count == 5U && opened[3] == 0x83U && opened[4] == 0x03U);
  assert(last_ep == USBD_WEBUSB_BULK_OUT_EPT && transfer_len == 64U && transfer_buffer == cdc->g_webusb_rx_buff);
  assert(usb_webusb_get_rxdata(dev, out) == 0U);

  /* CDC OUT must not touch WebUSB state and vice versa. */
  dev->ept_out[1].trans_len = 5; dev->class_handler->out_handler(dev, 1);
  assert(cdc->g_rx_completed == 1U && cdc->g_webusb_rx_completed == 0U);
  memcpy(cdc->g_webusb_rx_buff, "\xAA\x55\x01\x02", 4);
  dev->ept_out[3].trans_len = 4; dev->class_handler->out_handler(dev, 3);
  assert(cdc->g_webusb_rx_completed == 1U && cdc->g_rxlen == 5U);
  unsigned before = receives;
  assert(usb_webusb_get_rxdata(dev, out) == 4U && out[0] == 0xAAU && out[3] == 0x02U);
  assert(receives == before + 1U && last_ep == USBD_WEBUSB_BULK_OUT_EPT);   /* re-armed */
  assert(usb_webusb_get_rxdata(dev, out) == 0U && receives == before + 1U);

  /* TX busy tracking per endpoint. */
  memset(pkt, 0x5A, sizeof(pkt));
  assert(usb_webusb_send_data(dev, pkt, 64) == SUCCESS && last_ep == USBD_WEBUSB_BULK_IN_EPT);
  assert(usb_webusb_send_data(dev, pkt, 1) == ERROR);
  assert(usb_vcp_send_data(dev, pkt, 8) == SUCCESS && last_ep == USBD_CDC_BULK_IN_EPT);
  dev->class_handler->in_handler(dev, 1);
  assert(cdc->g_tx_completed == 1U && cdc->g_webusb_tx_completed == 0U);
  dev->class_handler->in_handler(dev, 2);                              /* notification EP ignored */
  assert(cdc->g_webusb_tx_completed == 0U);
  dev->class_handler->in_handler(dev, 3);
  assert(cdc->g_webusb_tx_completed == 1U);
  assert(usb_webusb_send_data(dev, pkt, 65) == ERROR);

  /* Interface 2 has only alt 0; class requests stay on interface 0. */
  usb_setup_type setup = {0};
  setup.bmRequestType = 0x01; setup.bRequest = USB_STD_REQ_SET_INTERFACE; setup.wIndex = 2; setup.wValue = 0;
  before = stalls; dev->class_handler->setup_handler(dev, &setup); assert(stalls == before);
  setup.wValue = 1; dev->class_handler->setup_handler(dev, &setup); assert(stalls == before + 1U);
  setup.wValue = 0; setup.wIndex = 3; dev->class_handler->setup_handler(dev, &setup); assert(stalls == before + 2U);
  setup.bmRequestType = 0x21; setup.bRequest = 0x22; setup.wIndex = 2;
  assert(dev->class_handler->setup_handler(dev, &setup) == USB_FAIL);

  /* Reset/deconfigure: clear closes all 5 endpoints, re-init restores idle state. */
  usb_webusb_send_data(dev, pkt, 4);
  dev->ept_out[3].trans_len = 4; dev->class_handler->out_handler(dev, 3);
  dev->class_handler->clear_handler(dev);
  assert(close_count == 5U);
  dev->class_handler->init_handler(dev);
  assert(cdc->g_webusb_tx_completed == 1U && cdc->g_webusb_rx_completed == 0U);
}
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int utf16_eq(const uint8_t *p, const char *s)
{
  for(; *s; ++s, p += 2) if(p[0] != (uint8_t)*s || p[1] != 0U) return 0;
  return p[0] == 0U && p[1] == 0U;
}

static void test_ms_os_20(usbd_core_type *dev)
{
  static const uint8_t ms_os_20_uuid[16] = {0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
                                            0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F};
  const uint8_t *d = cdc_desc_handler.get_device_descriptor()->descriptor;
  assert(le16(d + 2) == 0x0210U && le16(d + 12) == EXPECT_BCD_DEVICE);       /* bcdUSB 2.10, bcdDevice bumped */

  /* BOS through the core: standard GET_DESCRIPTOR(0x0F) on the device. */
  memset(&dev->setup, 0, sizeof(dev->setup));
  dev->setup.bmRequestType = 0x80; dev->setup.bRequest = USB_STD_REQ_GET_DESCRIPTOR;
  dev->setup.wValue = 0x0F00; dev->setup.wLength = 5;
  unsigned before_stall = stalls, before_send = sends;
  usbd_device_request(dev);
  assert(stalls == before_stall && sends == before_send + 1U && last_ep == 0U && transfer_len == 5U);
  assert(le16(transfer_buffer + 2) == USBD_BOS_DESC_SIZE);
  dev->setup.wLength = 0xFF;
  usbd_device_request(dev);
  assert(stalls == before_stall && transfer_len == USBD_BOS_DESC_SIZE && USBD_BOS_DESC_SIZE == 33U);
  const uint8_t *b = transfer_buffer;
  assert(b[0] == 5U && b[1] == 0x0FU && le16(b + 2) == 33U && b[4] == 1U);
  const uint8_t *cap = b + 5;
  assert(cap[0] == 28U && cap[1] == 0x10U && cap[2] == 0x05U && cap[3] == 0U);
  assert(memcmp(cap + 4, ms_os_20_uuid, 16) == 0);
  assert((cap[20] | (cap[21] << 8) | (cap[22] << 16) | ((uint32_t)cap[23] << 24)) == 0x06030000UL);
  const uint16_t set_total = le16(cap + 24);
  const uint8_t vendor_code = cap[26];
  assert(set_total == USBD_MS_OS_20_DESC_SET_SIZE && set_total == 178U);
  assert(vendor_code == USBD_MS_OS_20_VENDOR_CODE && cap[27] == 0U);

  /* MS OS 2.0 descriptor set via vendor IN request (device recipient, wIndex 7). */
  memset(&dev->setup, 0, sizeof(dev->setup));
  dev->setup.bmRequestType = 0xC0; dev->setup.bRequest = vendor_code;
  dev->setup.wIndex = 7; dev->setup.wLength = set_total;
  before_send = sends;
  usbd_device_request(dev);
  assert(stalls == before_stall && sends == before_send + 1U && last_ep == 0U && transfer_len == set_total);
  const uint8_t *s = transfer_buffer;
  assert(le16(s) == 10U && le16(s + 2) == 0x00U && le16(s + 8) == set_total);
  assert((s[4] | (s[5] << 8) | (s[6] << 16) | ((uint32_t)s[7] << 24)) == 0x06030000UL);
  const uint8_t *cfg = s + 10;
  assert(le16(cfg) == 8U && le16(cfg + 2) == 0x01U && cfg[4] == 0U && le16(cfg + 6) == set_total - 10U);
  const uint8_t *fn = cfg + 8;
  assert(le16(fn) == 8U && le16(fn + 2) == 0x02U && fn[4] == 2U && le16(fn + 6) == set_total - 18U);
  const uint8_t *cid = fn + 8;
  assert(le16(cid) == 20U && le16(cid + 2) == 0x03U && memcmp(cid + 4, "WINUSB\0\0", 8) == 0);
  for(unsigned i = 12; i < 20U; ++i) assert(cid[i] == 0U);
  const uint8_t *reg = cid + 20;
  const uint16_t name_len = le16(reg + 6);
  assert(le16(reg + 2) == 0x04U && le16(reg + 4) == 7U && name_len == 42U);
  assert(utf16_eq(reg + 8, "DeviceInterfaceGUIDs"));
  const uint16_t data_len = le16(reg + 8 + name_len);
  const uint8_t *data = reg + 10 + name_len;
  assert(data_len == 80U && le16(reg) == 10U + name_len + data_len);
  assert(utf16_eq(data, "{7B926486-7EEE-499C-BFFC-1CA59C7DD9ED}"));
  assert(data[data_len - 2] == 0U && data[data_len - 1] == 0U && data[data_len - 4] == 0U); /* MULTI_SZ */
  assert((unsigned)(reg + le16(reg) - s) == set_total);
  dev->setup.wLength = 10;                                           /* short read honoured */
  usbd_device_request(dev);
  assert(stalls == before_stall && transfer_len == 10U);

  /* Anything else vendor-typed stalls without sending. */
  const struct { uint8_t type, req; uint16_t value, index, len; } bad[] = {
    {0xC0, 0x02, 0, 7, 178}, {0xC0, 0x01, 0, 4, 178}, {0xC0, 0x01, 1, 7, 178},
    {0xC0, 0x01, 0, 7, 0},   {0x40, 0x01, 0, 7, 0},   {0xC1, 0x01, 0, 7, 178},
  };
  for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
  {
    usb_setup_type v = {0};
    v.bmRequestType = bad[i].type; v.bRequest = bad[i].req;
    v.wValue = bad[i].value; v.wIndex = bad[i].index; v.wLength = bad[i].len;
    before_stall = stalls; before_send = sends;
    assert(dev->class_handler->setup_handler(dev, &v) == USB_FAIL);
    assert(stalls == before_stall + 1U && sends == before_send);
  }
  /* GET_DESCRIPTOR for types the class does not own still stalls. */
  usb_setup_type g = {0};
  g.bmRequestType = 0x80; g.bRequest = USB_STD_REQ_GET_DESCRIPTOR; g.wValue = 0x0F01; g.wLength = 5;
  before_stall = stalls; dev->class_handler->setup_handler(dev, &g); assert(stalls == before_stall + 1U);
  g.bmRequestType = 0x81; g.wValue = 0x0F00;
  dev->class_handler->setup_handler(dev, &g); assert(stalls == before_stall + 2U);
}

int main(void)
{
  usbd_core_type dev = {0};
  dev.class_handler = &cdc_class_handler;
  test_descriptors();
  test_setup(&dev); test_rx(&dev); test_webusb(&dev); test_ms_os_20(&dev);
  const uint16_t invalid_endpoints[] = {8, 15, 0x88, 0xFF, 0x0101, 0xFFFF};
  dev.conn_state = USB_CONN_STATE_CONFIGURED;
  dev.setup.bRequest = USB_STD_REQ_GET_STATUS;
  for(unsigned i = 0; i < sizeof(invalid_endpoints) / sizeof(invalid_endpoints[0]); ++i)
  {
    dev.setup.wIndex = invalid_endpoints[i];
    assert(usbd_endpoint_request(&dev) == USB_FAIL);
  }
  puts("USB control bounds/direction, DTR/RTS, pending requests and RX canaries: OK");
  puts("USB composite descriptor (98 B, 3 IF, IAD, unique EPs) and WebUSB EP3 rx/tx/reset: OK");
  puts("USB 2.10 BOS (33 B) + MS OS 2.0 set (178 B, IF2 WINUSB + GUID) and vendor-request stalls: OK");
  printf("USB identity bcdDevice 0x%04X, product \"%s\", interface \"%s\": OK\n",
         (unsigned)EXPECT_BCD_DEVICE, EXPECT_PRODUCT, EXPECT_INTERFACE);
  return 0;
}
