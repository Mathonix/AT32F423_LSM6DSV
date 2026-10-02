#include "at32f423_conf.h"
#include "can_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static can_rx_message_type rx;
static can_tx_message_type frames[512];
static unsigned used, mailbox_next, queued;
static int statuses[3], fail_save;
static can_baudrate_type baud;
int can_baudrate_set(int bus, const can_baudrate_type *b) { (void)bus; baud = *b; return SUCCESS; }
int can_flag_get(int bus, int flag) { (void)bus; (void)flag; return 0; }
void can_flag_clear(int bus, int flag) { (void)bus; (void)flag; }
void can_transmit_cancel(int bus, int mailbox) { (void)bus; statuses[mailbox] = CAN_TX_STATUS_FAILED; }
int can_receive_message_pending_get(int bus, int fifo) { (void)bus; (void)fifo; return queued; }
void can_message_receive(int bus, int fifo, can_rx_message_type *m) { (void)bus; (void)fifo; *m = rx; queued = 0; }
uint8_t can_message_transmit(int bus, const can_tx_message_type *m) {
  (void)bus; assert(used < 512); frames[used++] = *m;
  uint8_t n = mailbox_next++ % 3; statuses[n] = CAN_TX_STATUS_PENDING; return n;
}
int can_transmit_status_get(int bus, int mailbox) { (void)bus; return statuses[mailbox]; }
int can_test_save_config(const can_config_t *c) { assert(can_config_valid(c)); return fail_save ? -1 : 0; }
static void ack_all(void) { for(unsigned i = 0; i < 3; ++i) statuses[i] = CAN_TX_STATUS_SUCCESSFUL; }
int main(void)
{
  can_test_init(); assert(can_test_live.init_ok);
  can_config_t c; can_test_get_config(&c); c.output_mask = 15;
  assert(!can_test_set_config(&c));
  used = 0;
  for(uint32_t t = 500; t <= 20000; t += 500) { ack_all(); can_test_task(t); }
  unsigned count[4] = {0};
  for(unsigned i = 0; i < used; ++i) {
    assert(frames[i].standard_id == 1 && frames[i].dlc == 8);
    count[frames[i].data[0] - 1]++;
  }
  for(unsigned i = 0; i < 4; ++i) assert(count[i] == 20); /* every group really 1 kHz */
  c.active = 0; assert(!can_test_set_config(&c)); unsigned before = used;
  can_test_task(21000); assert(used == before);
  rx = (can_rx_message_type){.standard_id = c.node_id, .id_type = CAN_ID_STANDARD, .frame_type = CAN_TFT_DATA, .dlc = 8, .data = {0xCC, 0x0C, 1, 0xDD, 1, 0, 0, 0}};
  queued = 1; can_test_task(22000);
  assert(used == before + 1 && frames[before].data[3] == 0 && can_test_config_busy());
  can_test_get_config(&c); assert(c.baud_index == 0); /* old baud until ACK really transmits */
  ack_all(); can_test_task(23000);
  can_test_get_config(&c); assert(c.baud_index == 1 && baud.baudrate_div == 10 && !can_test_config_busy());
  rx.data[4] = 2; queued = 1; can_test_task(24000);
  assert(can_test_config_busy());
  can_test_task(125000); /* pending, no physical CAN ACK: abort change */
  can_test_get_config(&c); assert(c.baud_index == 1 && !can_test_config_busy());
  rx.data[1] = 0xFE; queued = 1; fail_save = 1;
  before = used; can_test_task(126000); assert(frames[before].data[3] == 3);
  rx.frame_type = CAN_TFT_REMOTE; queued = 1;
  before = used; can_test_task(127000); assert(used == before);
  rx.frame_type = CAN_TFT_DATA; rx.standard_id = 2; queued = 1;
  can_test_task(128000); assert(used == before);
  puts("CAN driver: four-group 1kHz, request-only, delayed baud ACK/timeout, flash failure and RTR/address isolation: OK");
}
