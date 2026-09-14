#ifndef CAN_TEST_H
#define CAN_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_TEST_LIVE_MAGIC 0x43414E31u /* "CAN1" */

typedef struct
{
  uint32_t magic;
  uint32_t seq;
  uint32_t millis;
  uint32_t init_ok;
  uint32_t tx_count;
  uint32_t tx_success_count;
  uint32_t tx_failed_count;
  uint32_t tx_no_mailbox_count;
  uint32_t tx_pending_count;
  uint32_t tx_error_counter;
  uint32_t rx_error_counter;
  uint32_t error_record;
  uint32_t bus_off;
  uint32_t error_passive;
  uint32_t last_mailbox;
  uint32_t last_status;
  uint32_t last_data_counter;
  uint32_t rx_count;
  uint32_t rx_standard_count;
  uint32_t rx_extended_count;
  uint32_t rx_overrun_count;
  uint32_t rx_pending;
  uint32_t rx_last_id;
  uint32_t rx_last_dlc;
  uint32_t rx_last_frame_type;
  uint32_t rx_last_millis;
  uint32_t rx_last_data[8];
} can_test_live_t;

extern volatile can_test_live_t can_test_live;

void can_test_init(void);
void can_test_task(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif