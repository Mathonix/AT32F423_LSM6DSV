#ifndef USB_TX_CONTROL_GUARD_H
#define USB_TX_CONTROL_GUARD_H
#include <stdint.h>

/* Main-context bookkeeping for a byte ring with uint16_t monotonic positions.
 * Unread telemetry may stay queued during Flash work. Command replies must
 * have reached the host before the save commits; never discard either queue. */
typedef struct {
  uint16_t end;
  uint8_t queued, in_packet;
} usb_tx_control_guard_t;

static inline void usb_tx_control_queued(usb_tx_control_guard_t *g,uint16_t end)
{
  g->end=end; g->queued=1U;
}
/* Call only after a new packet was accepted by the endpoint. That acceptance
 * also proves any preceding in_packet transfer has completed. */
static inline void usb_tx_control_packet(usb_tx_control_guard_t *g,uint16_t read,uint16_t len)
{
  uint16_t remaining=(uint16_t)(g->end-read);
  g->in_packet=(uint8_t)(g->queued&&remaining!=0U&&remaining<=len);
  if(g->in_packet) g->queued=0U;
}
static inline int usb_tx_control_idle(const usb_tx_control_guard_t *g,int packet_completed)
{
  return !g->queued&&(!g->in_packet||packet_completed);
}
#endif
