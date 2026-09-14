#include "usb_cdc.h"
#include "at32f423_conf.h"
#include "at32f423_crm.h"
#include "at32f423_misc.h"
#include "at32f423_acc.h"
#include <string.h>

#define USBX OTG1_GLOBAL
#define USB_CDC_IN_EP  2U
#define USB_CDC_OUT_EP 2U
#define USB_CDC_INT_EP 1U
#define USB_CDC_MPS 64U
#define USB_CDC_RXFIFO_WORDS 128U
#define USB_CDC_TX0_WORDS 32U
#define USB_CDC_TX1_WORDS 16U
#define USB_CDC_TX2_WORDS 64U
#define USB_CDC_RING_SIZE 1024U
/* USB device mode: PA12 = D+, PA11 = D-. */
#define USB_DP_GPIO_TEST 0U

/* AT32F423 OTGFS1 alternate-function mapping: PA12 = D+, PA11 = D-. */
static void usb_gpio_init(void)
{
  gpio_init_type gpio_init_struct;

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_pins = GPIO_PINS_11 | GPIO_PINS_12;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE11, GPIO_MUX_10);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE12, GPIO_MUX_10);
}

#if USB_DP_GPIO_TEST
static void usb_dp_gpio_test(void)
{
  gpio_init_type g;
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&g);
  /* PA12 = USB D+: internal pull-up; PA11 = USB D-: internal pull-up.
   * Input mode avoids actively driving the USB pair during this test. */
  g.gpio_pins = GPIO_PINS_12;
  g.gpio_mode = GPIO_MODE_INPUT;
  g.gpio_pull = GPIO_PULL_UP;
  gpio_init(GPIOA, &g);
  gpio_default_para_init(&g);
  g.gpio_pins = GPIO_PINS_11;
  g.gpio_mode = GPIO_MODE_INPUT;
  g.gpio_pull = GPIO_PULL_UP;
  gpio_init(GPIOA, &g);
}
#endif

typedef struct { uint8_t bmRequestType,bRequest; uint16_t wValue,wIndex,wLength; } setup_t;
static volatile uint8_t configured, in_busy;
#define USB_CDC_RX_RING_SIZE 256U
static uint8_t rx_ring[USB_CDC_RX_RING_SIZE];
static volatile uint16_t rx_ring_r, rx_ring_w;
static uint8_t ring[USB_CDC_RING_SIZE];
static volatile uint16_t ring_r, ring_w;
static setup_t setup;
static uint8_t ep0_buf[USB_CDC_MPS];
static uint8_t ep0_len, ep0_out_len;
static const uint8_t *ep0_src;
static uint16_t ep0_total, ep0_offset;
static uint8_t ep0_zlp;
static uint8_t pending_address;
static uint8_t line_coding[7] = {0x00,0x09,0x3D,0x00,0,0,8};
static uint8_t tx_buf[USB_CDC_MPS];

static const uint8_t dev_desc[] = {
  18,1,0x00,0x02,0xEF,0x02,0x01,64,0x3C,0x2E,0x01,0xF4,1,2,0,0,0,1
};
static const uint8_t cfg_desc[] = {
  9,2,75,0,2,1,0,0x80,50,
  8,11,0,2,2,2,1,0,
  9,4,0,0,1,0x02,0x02,0x01,0,
  5,0x24,0x00,0x10,0x01,
  5,0x24,0x01,0x00,0x01,
  4,0x24,0x02,0x02,
  5,0x24,0x06,0,1,
  7,5,0x81,0x03,8,0,16,
  9,4,1,0,2,0x0A,0,0,0,
  7,5,0x02,0x02,64,0,0,
  7,5,0x82,0x02,64,0,0
};
static const uint8_t lang_desc[] = {4,3,0x09,0x04};
static const uint8_t str_man[] = {18,3,'A',0,'T',0,'3',0,'2',0,'F',0,'4',0,'2',0,'3',0};
static const uint8_t str_prod[] = {34,3,'A',0,'T',0,'3',0,'2',0,' ',0,'L',0,'S',0,'M',0,'6',0,'D',0,'S',0,'V',0,' ',0,'U',0,'S',0,'B',0};

static uint16_t ring_used(void) { return (uint16_t)((ring_w-ring_r)&(USB_CDC_RING_SIZE-1U)); }
/* Re-arm CDC bulk OUT after each received packet. */
static void bulk_out_arm(void)
{
  USB_OUTEPT(USBX, USB_CDC_OUT_EP)->doeptsiz = 0;
  USB_OUTEPT(USBX, USB_CDC_OUT_EP)->doeptsiz_bit.xfersize = USB_CDC_MPS;
  USB_OUTEPT(USBX, USB_CDC_OUT_EP)->doeptsiz_bit.pktcnt = 1;
  USB_OUTEPT(USBX, USB_CDC_OUT_EP)->doepctl_bit.cnak = TRUE;
  USB_OUTEPT(USBX, USB_CDC_OUT_EP)->doepctl_bit.eptena = TRUE;
}
static void ep0_arm_setup(void)
{
  USB_OUTEPT(USBX, 0)->doeptsiz = 0;
  USB_OUTEPT(USBX, 0)->doeptsiz_bit.pktcnt = 1;
  USB_OUTEPT(USBX, 0)->doeptsiz_bit.xfersize = 24;
  USB_OUTEPT(USBX, 0)->doeptsiz_bit.rxdpid_setupcnt = 3;
  USB_OUTEPT(USBX, 0)->doepctl_bit.cnak = TRUE;
  USB_OUTEPT(USBX, 0)->doepctl_bit.eptena = TRUE;
}
static void ep0_prime_out(uint16_t n) {
  USB_OUTEPT(USBX,0)->doeptsiz = 0;
  USB_OUTEPT(USBX,0)->doeptsiz_bit.xfersize = n;
  USB_OUTEPT(USBX,0)->doeptsiz_bit.pktcnt = 1;
  USB_OUTEPT(USBX,0)->doepctl_bit.cnak = TRUE;
  USB_OUTEPT(USBX,0)->doepctl_bit.eptena = TRUE;
}
static void ep0_in_start(const uint8_t *p, uint16_t n)
{
  uint16_t chunk = (n > USB_CDC_MPS) ? USB_CDC_MPS : n;
  ep0_src = p;
  ep0_total = n;
  ep0_offset = 0U;
  ep0_zlp = (uint8_t)((n != 0U) && ((n % USB_CDC_MPS) == 0U));
  if(chunk != 0U)
  {
    memcpy(ep0_buf, p, chunk);
    ep0_offset = chunk;
  }
  ep0_len = (uint8_t)chunk;
  USB_INEPT(USBX,0)->dieptsiz = 0;
  USB_INEPT(USBX,0)->dieptsiz_bit.xfersize = chunk;
  USB_INEPT(USBX,0)->dieptsiz_bit.pktcnt = 1;
  USB_INEPT(USBX,0)->diepctl_bit.cnak = TRUE;
  USB_INEPT(USBX,0)->diepctl_bit.eptena = TRUE;
  if(chunk != 0U) usb_write_packet(USBX, ep0_buf, 0, chunk);
}
static void ep0_in_next(void)
{
  uint16_t remain = (ep0_offset < ep0_total) ? (uint16_t)(ep0_total - ep0_offset) : 0U;
  uint16_t chunk = (remain > USB_CDC_MPS) ? USB_CDC_MPS : remain;
  if((chunk == 0U) && (ep0_zlp == 0U)) return;
  if(chunk != 0U)
  {
    memcpy(ep0_buf, ep0_src + ep0_offset, chunk);
    ep0_offset = (uint16_t)(ep0_offset + chunk);
  }
  else
  {
    ep0_zlp = 0U;
  }
  ep0_len = (uint8_t)chunk;
  USB_INEPT(USBX,0)->dieptsiz = 0;
  USB_INEPT(USBX,0)->dieptsiz_bit.xfersize = chunk;
  USB_INEPT(USBX,0)->dieptsiz_bit.pktcnt = 1;
  USB_INEPT(USBX,0)->diepctl_bit.cnak = TRUE;
  USB_INEPT(USBX,0)->diepctl_bit.eptena = TRUE;
  if(chunk != 0U) usb_write_packet(USBX, ep0_buf, 0, chunk);
}
static void ep0_status_in(void) { ep0_in_start(NULL,0); }
static void ep0_stall(void) { USB_INEPT(USBX,0)->diepctl_bit.stall=TRUE; USB_OUTEPT(USBX,0)->doepctl_bit.stall=TRUE; }
static void open_data_eps(void) {
  usb_ept_info e={0};
  /* Open EP0 in both directions so SETUP and control OUT packets are accepted. */
  e.eptn=0; e.maxpacket=64; e.inout=EPT_DIR_IN; e.trans_type=EPT_CONTROL_TYPE; usb_ept_open(USBX,&e);
  e.eptn=0; e.maxpacket=64; e.inout=EPT_DIR_OUT; e.trans_type=EPT_CONTROL_TYPE; usb_ept_open(USBX,&e);
  e.eptn=1; e.maxpacket=8; e.inout=EPT_DIR_IN; e.trans_type=EPT_INT_TYPE; usb_ept_open(USBX,&e);
  e.eptn=2; e.maxpacket=64; e.inout=EPT_DIR_IN; e.trans_type=EPT_BULK_TYPE; usb_ept_open(USBX,&e);
  e.eptn=2; e.maxpacket=64; e.inout=EPT_DIR_OUT; e.trans_type=EPT_BULK_TYPE; usb_ept_open(USBX,&e);
  ep0_arm_setup();
}
static void usb_reset(void) {
  configured=0;
  in_busy=0;
  ring_r=ring_w=0;
  rx_ring_r=rx_ring_w=0;
  pending_address=0;
  ep0_out_len=0;
  ep0_total=0;
  ep0_offset=0;
  ep0_zlp=0;
  usb_flush_tx_fifo(USBX,16); usb_flush_rx_fifo(USBX);
  OTG_DEVICE(USBX)->dcfg=0; OTG_DEVICE(USBX)->dcfg_bit.devspd=3; /* FS */
  OTG_DEVICE(USBX)->dcfg_bit.nzstsouthshk=1;
  OTG_DEVICE(USBX)->diepmsk=USB_OTG_DIEPINT_XFERC_FLAG;
  OTG_DEVICE(USBX)->doepmsk=USB_OTG_DOEPINT_XFERC_FLAG|USB_OTG_DOEPINT_SETUP_FLAG;
  OTG_DEVICE(USBX)->daintmsk =
      (1UL << 0) | (1UL << USB_CDC_IN_EP) |
      (1UL << (16U + 0U)) | (1UL << (16U + USB_CDC_OUT_EP));
  open_data_eps();
}
static void handle_setup(void) {
  uint16_t value=setup.wValue, len=setup.wLength;
  if(setup.bRequest==6 && (setup.bmRequestType&0x60)==0) {
    const uint8_t *p=NULL; uint16_t n=0;
    switch((uint8_t)(value>>8)) { case 1:p=dev_desc;n=sizeof(dev_desc);break; case 2:p=cfg_desc;n=sizeof(cfg_desc);break; case 3: if((uint8_t)value==0)p=lang_desc,n=sizeof(lang_desc); else if((uint8_t)value==1)p=str_man,n=sizeof(str_man); else if((uint8_t)value==2)p=str_prod,n=sizeof(str_prod); break; }
    if(!p){ep0_stall();return;} if(n>len)n=len; ep0_in_start(p,n); return;
  }
  if(setup.bRequest==5 && setup.bmRequestType==0) { pending_address=(uint8_t)value; ep0_status_in(); return; }
  if(setup.bRequest==9 && setup.bmRequestType==0)
  {
    configured = ((uint8_t)value == 1U) ? 1U : 0U;
    if(configured != 0U)
    {
      /* Endpoints were initialized at bus reset; only mark the CDC
       * interface configured here. Re-opening them on every SET_CONFIGURATION
       * can reset data-toggle/state while the host is enumerating. */
      bulk_out_arm();
    }
    ep0_status_in();
    return;
  }
  if(setup.bRequest==8 && setup.bmRequestType==0) { ep0_buf[0]=configured; ep0_in_start(ep0_buf,1); return; }
  if(setup.bRequest==0 && (setup.bmRequestType&0x80)) { ep0_buf[0]=0;ep0_buf[1]=0;ep0_in_start(ep0_buf,2);return; }
  if(setup.bRequest==0x20 && setup.bmRequestType==0x21) { ep0_out_len=7;ep0_prime_out(7);return; }
  if(setup.bRequest==0x21 && setup.bmRequestType==0xA1) { ep0_in_start(line_coding,7);return; }
  if(setup.bRequest==0x22 && setup.bmRequestType==0x21) { ep0_status_in();return; }
  /* Standard interface requests. SET_INTERFACE is host-to-device and must
   * return a zero-length status packet. Answering it with one byte of IN
   * data makes some Windows CDC enumerators abort configuration. */
  if(setup.bRequest==11 && setup.bmRequestType==0x01) { ep0_status_in();return; }
  if(setup.bRequest==10 && setup.bmRequestType==0x81) { ep0_buf[0]=0;ep0_in_start(ep0_buf,1);return; }
  ep0_stall();
}
static void rx_fifo(void) {
  uint32_t s=USBX->grxstsp; uint8_t ep=(uint8_t)(s&15U); uint16_t n=(uint16_t)((s>>4)&0x7FFU); uint8_t st=(uint8_t)((s>>17)&15U);
  if(st==USB_SETUP_STS_DATA) { uint8_t b[8]; usb_read_packet(USBX,b,0,8); setup.bmRequestType=b[0];setup.bRequest=b[1];setup.wValue=(uint16_t)b[2]|((uint16_t)b[3]<<8);setup.wIndex=(uint16_t)b[4]|((uint16_t)b[5]<<8);setup.wLength=(uint16_t)b[6]|((uint16_t)b[7]<<8); handle_setup(); }
  else if(st==USB_OUT_STS_DATA) { if(ep==0 && n) { if(n>64)n=64; usb_read_packet(USBX,ep0_buf,0,n); if(ep0_out_len==7){memcpy(line_coding,ep0_buf,7);ep0_out_len=0;ep0_status_in();} } else if(ep==2) { uint8_t dump[64]; if(n>64)n=64; if(n) { uint16_t j; usb_read_packet(USBX,dump,2,n); for(j=0;j<n;j++) { uint16_t next=(uint16_t)((rx_ring_w+1U)&(USB_CDC_RX_RING_SIZE-1U)); if(next!=rx_ring_r) { rx_ring[rx_ring_w]=dump[j]; rx_ring_w=next; } } } bulk_out_arm(); } }
}
static void start_bulk(void) { uint16_t n,i; if(!configured||in_busy)return; n=ring_used();if(!n)return;if(n>64)n=64;for(i=0;i<n;i++)tx_buf[i]=ring[(ring_r+i)&(USB_CDC_RING_SIZE-1U)]; ring_r=(uint16_t)(ring_r+n); in_busy=1; USB_INEPT(USBX,2)->dieptsiz=0;USB_INEPT(USBX,2)->dieptsiz_bit.xfersize=n;USB_INEPT(USBX,2)->dieptsiz_bit.pktcnt=1;USB_INEPT(USBX,2)->diepctl_bit.cnak=TRUE;USB_INEPT(USBX,2)->diepctl_bit.eptena=TRUE;usb_write_packet(USBX,tx_buf,2,n); }
void usb_cdc_init(void)
{
  uint32_t i;

  /* The USB pins must be in OTGFS1 MUX10 before enabling the core. */
  usb_gpio_init();

#if USB_DP_GPIO_TEST
  usb_dp_gpio_test();
  configured = 0U;
  return;
#endif
  /* Select HICK as the 48 MHz USB source and enable the crystal-less
   * automatic trim loop recommended by the AT32F423 USB example. */
  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);
  crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, TRUE);
  acc_write_c1(7980U);
  acc_write_c2(8000U);
  acc_write_c3(8020U);
  acc_calibration_mode_enable(ACC_CAL_HICKTRIM, TRUE);
  crm_periph_clock_enable(CRM_OTGFS1_PERIPH_CLOCK, TRUE);
  usb_disconnect(USBX);
  usb_global_set_mode(USBX, OTG_DEVICE_MODE);
  usb_global_init(USBX);
  usb_global_power_on(USBX);
  USBX->gusbcfg_bit.usbtrdtim = 9U;
  usb_set_rx_fifo(USBX, USB_CDC_RXFIFO_WORDS);
  usb_set_tx_fifo(USBX, 0U, USB_CDC_TX0_WORDS);
  usb_set_tx_fifo(USBX, 1U, USB_CDC_TX1_WORDS);
  usb_set_tx_fifo(USBX, 2U, USB_CDC_TX2_WORDS);
  USBX->gintmsk = USB_OTG_USBRST_INT | USB_OTG_ENUMDONE_INT |
                  USB_OTG_RXFLVL_INT | USB_OTG_IEPT_INT | USB_OTG_OEPT_INT;
  usb_interrupt_enable(USBX);
  nvic_irq_enable(OTGFS1_IRQn, 3, 0);
  /* Ensure the host sees a fresh attach after clocks/core are ready. */
  for(i = 0U; i < 300000U; ++i) __NOP();
  usb_connect(USBX);
}
void usb_cdc_task(void) { start_bulk(); }
int usb_cdc_configured(void) { return configured!=0; }
int usb_cdc_available(void) { return (int)((rx_ring_w - rx_ring_r) & (USB_CDC_RX_RING_SIZE - 1U)); }
int usb_cdc_read_byte(uint8_t *ch) { if(rx_ring_r == rx_ring_w) return 0; if(ch) *ch = rx_ring[rx_ring_r]; rx_ring_r = (uint16_t)((rx_ring_r + 1U) & (USB_CDC_RX_RING_SIZE - 1U)); return 1; }
int usb_cdc_write(const uint8_t *data,uint16_t len) { uint16_t free=(uint16_t)(USB_CDC_RING_SIZE-1U-ring_used()),i; if(!data||!len||len>free)return -1; for(i=0;i<len;i++)ring[(ring_w+i)&(USB_CDC_RING_SIZE-1U)]=data[i]; __DMB();ring_w=(uint16_t)(ring_w+len);return 0; }
void usb_cdc_isr(void)
{
  uint32_t f = USBX->gintsts & USBX->gintmsk;
  if(f & USB_OTG_USBRST_FLAG)
  {
    USBX->gintsts = USB_OTG_USBRST_FLAG;
    usb_reset();
  }
  if(f & USB_OTG_ENUMDONE_FLAG)
  {
    USBX->gintsts = USB_OTG_ENUMDONE_FLAG;
  }
  if(f & USB_OTG_RXFLVL_FLAG)
  {
    USBX->gintmsk &= ~USB_OTG_RXFLVL_INT;
    while(USBX->gintsts & USB_OTG_RXFLVL_FLAG) rx_fifo();
    USBX->gintmsk |= USB_OTG_RXFLVL_INT;
  }
  if(f & USB_OTG_IEPT_FLAG)
  {
    uint32_t m = usb_get_all_in_interrupt(USBX);
    if(m & (1U << USB_CDC_IN_EP))
    {
      usb_ept_in_clear(USBX, USB_CDC_IN_EP, USB_OTG_DIEPINT_XFERC_FLAG);
      in_busy = 0U;
    }
    if(m & 1U)
    {
      usb_ept_in_clear(USBX, 0, USB_OTG_DIEPINT_XFERC_FLAG);
      if(ep0_offset < ep0_total || ep0_zlp)
      {
        ep0_in_next();
      }
      else
      {
        if(pending_address != 0U)
        {
          usb_set_address(USBX, pending_address);
          pending_address = 0U;
        }
        ep0_arm_setup();
      }
    }
  }
  if(f & USB_OTG_OEPT_FLAG)
  {
    uint32_t m = usb_get_all_out_interrupt(USBX);
    if(m & 1U) usb_ept_out_clear(USBX, 0, USB_OTG_DOEPINT_XFERC_FLAG | USB_OTG_DOEPINT_SETUP_FLAG);
    if(m & (1U << USB_CDC_OUT_EP)) usb_ept_out_clear(USBX, USB_CDC_OUT_EP, USB_OTG_DOEPINT_XFERC_FLAG);
  }
}
