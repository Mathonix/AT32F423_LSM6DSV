#include "can_test.h"
#include "at32f423_conf.h"
#include "app_config.h"
#include <string.h>

volatile can_test_live_t can_test_live = {.magic = CAN_TEST_LIVE_MAGIC};
static uint32_t can_last_tx_us, can_data_counter, can_last_status_us;
static uint8_t can_pending_mask, can_cycle;
static volatile uint8_t can_cmd_flags;
static can_config_t can_config, pending_config;
static can_sample_t can_sample;
static uint8_t baud_pending;
static uint8_t baud_mailbox;
static uint32_t baud_deadline_us;

void can_test_update_data(float roll, float pitch, float yaw,
                          float gx, float gy, float gz, float ax, float ay, float az,
                          float qw, float qx, float qy, float qz, float temperature, uint8_t flags)
{
  (void)flags;
  can_sample = (can_sample_t){roll, pitch, yaw, gx * .01745329252f, gy * .01745329252f, gz * .01745329252f,
    ax * 9.80665f, ay * 9.80665f, az * 9.80665f, qw, qx, qy, qz, temperature};
}
uint8_t can_test_get_cmd_flag(void) { return can_cmd_flags; }
void can_test_clear_cmd_flag(uint8_t flag) { can_cmd_flags &= (uint8_t)~flag; }
uint16_t can_test_get_node_id(void) { return can_config.node_id; }
void can_test_get_config(can_config_t *c) { *c = can_config; }
int can_test_config_busy(void) { return baud_pending; }
int can_test_set_config(const can_config_t *c)
{
  if(!can_config_valid(c) || baud_pending || !can_test_live.init_ok) return -1;
  if(c->baud_index != can_config.baud_index) {
    can_baudrate_type b;
    can_baudrate_default_para_init(&b);
    b.baudrate_div = can_baud_divisor(c->baud_index);
    b.rsaw_size = CAN_RSAW_2TQ;
    /* PCLK1=75 MHz. 400k uses 17 TQ/div11 = 401069.5 bit/s (+0.267%).
     * All other choices use 15 TQ and are exact. */
    b.bts1_size = c->baud_index == 2 ? CAN_BTS1_12TQ : CAN_BTS1_10TQ;
    b.bts2_size = CAN_BTS2_4TQ;
    for(unsigned i = 0; i < 3; ++i) can_transmit_cancel(CAN2, (can_tx_mailbox_num_type)i);
    can_pending_mask = 0;
    if(can_baudrate_set(CAN2, &b) != SUCCESS) {
      b.baudrate_div = can_baud_divisor(can_config.baud_index);
      b.bts1_size = can_config.baud_index == 2 ? CAN_BTS1_12TQ : CAN_BTS1_10TQ;
      if(can_baudrate_set(CAN2, &b) != SUCCESS) can_test_live.init_ok = 0;
      return -2;
    }
  }
  can_config = *c; can_cycle = 0; can_last_tx_us = 0;
  return 0;
}
int can_test_set_node_id(uint16_t id)
{
  can_config_t c = can_config; c.node_id = id;
  return can_test_set_config(&c);
}
__attribute__((weak)) int can_test_save_config(const can_config_t *c) { (void)c; return -1; }
static uint8_t transmit(uint16_t id, const uint8_t data[8])
{
  can_tx_message_type message;
  memset(&message, 0, sizeof(message));
  message.standard_id = id; message.id_type = CAN_ID_STANDARD;
  message.frame_type = CAN_TFT_DATA; message.dlc = 8;
  memcpy(message.data, data, 8);
  uint8_t mailbox = can_message_transmit(CAN2, &message);
  can_test_live.tx_count++;
  can_test_live.last_data_counter = can_data_counter++;
  if(mailbox <= CAN_TX_MAILBOX2) {
    can_pending_mask |= 1U << mailbox; can_test_live.last_mailbox = mailbox;
    can_test_live.last_status = CAN_TX_STATUS_PENDING;
  } else {
    can_test_live.tx_no_mailbox_count++; can_test_live.last_mailbox = 0xFFFFFFFF;
    can_test_live.last_status = CAN_TX_STATUS_NO_EMPTY;
  }
  return mailbox;
}
static void can_test_snapshot(uint32_t now_ms)
{
  can_test_live.seq++;
  __DMB();
  can_test_live.millis = now_ms;
  can_test_live.tx_error_counter = can_transmit_error_counter_get(CAN2);
  can_test_live.rx_error_counter = can_receive_error_counter_get(CAN2);
  can_test_live.error_record = (uint32_t)can_error_type_record_get(CAN2);
  can_test_live.bus_off = (can_flag_get(CAN2, CAN_BOF_FLAG) != RESET) ? 1U : 0U;
  can_test_live.error_passive = (can_flag_get(CAN2, CAN_EPF_FLAG) != RESET) ? 1U : 0U;
  __DMB();
  can_test_live.seq++;
}


static void can_test_poll_receive(uint32_t now_us)
{
  uint32_t now_ms = now_us / 1000U;
  for(unsigned processed = 0; processed < 8 && can_receive_message_pending_get(CAN2, CAN_RX_FIFO0); ++processed) {
    can_rx_message_type rx;
    uint8_t reply[8], actions; uint16_t id; can_config_t next;
    can_message_receive(CAN2, CAN_RX_FIFO0, &rx);
    can_test_live.rx_count++;
    if(rx.id_type == CAN_ID_STANDARD) can_test_live.rx_standard_count++;
    else can_test_live.rx_extended_count++;
    can_test_live.rx_last_id = rx.id_type == CAN_ID_STANDARD ? rx.standard_id : rx.extended_id;
    can_test_live.rx_last_dlc = rx.dlc; can_test_live.rx_last_frame_type = rx.frame_type;
    can_test_live.rx_last_millis = now_ms;
    for(unsigned i = 0; i < 8; ++i) can_test_live.rx_last_data[i] = i < rx.dlc ? rx.data[i] : 0;
    if(rx.id_type != CAN_ID_STANDARD || rx.frame_type != CAN_TFT_DATA ||
       !can_damiao_request(rx.standard_id, rx.dlc, rx.data, &can_config, &can_sample, &id, reply, &next, &actions)) continue;
    if(baud_pending && actions) { reply[3] = DAMIAO_ACK_FAIL; actions = 0; }
    if(actions & CAN_ACTION_SAVE) {
      if(can_test_save_config(&can_config)) reply[3] = DAMIAO_ACK_FAIL;
    }
    if((actions & CAN_ACTION_CONFIG) && next.baud_index == can_config.baud_index) {
      if(can_test_set_config(&next)) reply[3] = DAMIAO_ACK_FAIL;
    }
    uint8_t mailbox = transmit(id, reply);
    if(mailbox > CAN_TX_MAILBOX2 || reply[0] != 0xCC || reply[3]) continue;
    if((actions & CAN_ACTION_CONFIG) && next.baud_index != can_config.baud_index) {
      /* Send the acknowledgement at the OLD baud. Commit only on actual TX
       * success; a missing peer/ACK or occupied mailbox never changes baud. */
      pending_config = next; baud_mailbox = mailbox; baud_pending = 1;
      baud_deadline_us = now_us + 100000U;
    }
    if(actions & CAN_ACTION_ZERO) can_cmd_flags |= CAN_CMD_FLAG_ZERO_YAW;
    if(actions & CAN_ACTION_REBOOT) can_cmd_flags |= CAN_CMD_FLAG_REBOOT;
  }
  can_test_live.rx_pending = can_receive_message_pending_get(CAN2, CAN_RX_FIFO0);
  if(can_flag_get(CAN2, CAN_RF0OF_FLAG) != RESET) {
    can_test_live.rx_overrun_count++; can_flag_clear(CAN2, CAN_RF0OF_FLAG);
  }
}
static void can_test_poll_mailboxes(void)
{
  uint8_t bit;
  for(bit = 0U; bit < 3U; ++bit)
  {
    const uint8_t mask = (uint8_t)(1U << bit);
    can_transmit_status_type status;
    if((can_pending_mask & mask) == 0U)
    {
      continue;
    }
    status = can_transmit_status_get(CAN2, (can_tx_mailbox_num_type)bit);
    if(status == CAN_TX_STATUS_SUCCESSFUL)
    {
      can_test_live.tx_success_count++;
      can_test_live.last_status = (uint32_t)status;
      can_flag_clear(CAN2, CAN_TM0TCF_FLAG + bit);
      can_pending_mask = (uint8_t)(can_pending_mask & (uint8_t)~mask);
    }
    else if(status == CAN_TX_STATUS_FAILED)
    {
      can_test_live.tx_failed_count++;
      can_test_live.last_status = (uint32_t)status;
      can_flag_clear(CAN2, CAN_TM0TCF_FLAG + bit);
      can_pending_mask = (uint8_t)(can_pending_mask & (uint8_t)~mask);
    }
    else
    {
      can_test_live.tx_pending_count++;
    }
  }
}


void can_test_init(void)
{
  gpio_init_type gpio_init_struct;
  can_base_type can_base_struct;
  can_baudrate_type can_baudrate_struct;

  can_config_defaults(&can_config);
  can_sample.qw = 1; can_sample.temperature = 25;
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_CAN2_PERIPH_CLOCK, TRUE);

  /* AT32 CAN RX must also be configured in alternate-function mode.
   * Leaving PA2 as GPIO input disconnects CAN2_RX from the bus, so the
   * controller cannot monitor its own dominant bits and immediately enters
   * error-passive/bus-off. Match the official AT32 CAN example exactly. */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_2 | GPIO_PINS_3;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE2, GPIO_MUX_9);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE3, GPIO_MUX_9);

  can_reset(CAN2);
  can_default_para_init(&can_base_struct);
  can_base_struct.mode_selection = CAN_MODE_COMMUNICATE;
  can_base_struct.ttc_enable = FALSE;
  can_base_struct.aebo_enable = TRUE;
  can_base_struct.aed_enable = TRUE;
  can_base_struct.prsf_enable = FALSE;
  can_base_struct.mdrsel_selection = CAN_DISCARDING_FIRST_RECEIVED;
  can_base_struct.mmssr_selection = CAN_SENDING_BY_ID;
  if(can_base_init(CAN2, &can_base_struct) != SUCCESS)
  {
    can_test_live.init_ok = 0U;
    return;
  }

  can_baudrate_default_para_init(&can_baudrate_struct);
  can_baudrate_struct.baudrate_div = APP_CAN_BAUDRATE_DIV;
  can_baudrate_struct.rsaw_size = APP_CAN_RSAW;
  can_baudrate_struct.bts1_size = APP_CAN_BTS1;
  can_baudrate_struct.bts2_size = APP_CAN_BTS2;
  if(can_baudrate_set(CAN2, &can_baudrate_struct) != SUCCESS)
  {
    can_test_live.init_ok = 0U;
    return;
  }

#if APP_CAN_RX_ENABLE
  {
    can_filter_init_type filter_init;
    can_filter_default_para_init(&filter_init);
    filter_init.filter_activate_enable = TRUE;
    filter_init.filter_mode = CAN_FILTER_MODE_ID_MASK;
    filter_init.filter_fifo = CAN_FILTER_FIFO0;
    filter_init.filter_number = 0;
    filter_init.filter_bit = CAN_FILTER_32BIT;
    filter_init.filter_id_high = 0U;
    filter_init.filter_id_low = 0U;
    filter_init.filter_mask_high = 0U;
    filter_init.filter_mask_low = 0U;
    can_filter_init(CAN2, &filter_init);
  }
#endif

  can_pending_mask = 0U;
  can_data_counter = 0U;
  can_last_tx_us = 0U;
  can_last_status_us = 0U;
  can_cmd_flags = 0U;
  can_test_live.init_ok = 1U;
  can_test_snapshot(0U);
}


void can_test_task(uint32_t now_us)
{
  if(!can_test_live.init_ok) return;
  if((uint32_t)(now_us - can_last_status_us) >= 1000U) {
    if(baud_pending) {
      can_transmit_status_type status = can_transmit_status_get(CAN2, (can_tx_mailbox_num_type)baud_mailbox);
      if(status == CAN_TX_STATUS_SUCCESSFUL) {
        baud_pending = 0;
        (void)can_test_set_config(&pending_config);
      } else if(status == CAN_TX_STATUS_FAILED || (int32_t)(now_us - baud_deadline_us) >= 0) {
        can_transmit_cancel(CAN2, (can_tx_mailbox_num_type)baud_mailbox); baud_pending = 0;
      }
    }
    can_test_poll_mailboxes();
#if APP_CAN_RX_ENABLE
    can_test_poll_receive(now_us);
#endif
    can_last_status_us = now_us; can_test_snapshot(now_us / 1000U);
  }
#if APP_CAN_TX_ENABLE
  unsigned count = can_output_count(can_config.output_mask);
  if(baud_pending || !can_config.active || !count) return;
  /* Spread groups evenly through the requested PER-GROUP period. No burst,
   * no reduction to quarter-rate when all four groups are selected. */
  uint32_t slot_us = (uint32_t)can_config.period_ms * 1000U / count;
  unsigned due = (uint32_t)(now_us - can_last_tx_us) / slot_us;
  if(!due) return;
  /* Main loop is 2 kHz: all four groups at 1 kHz require two frames per
   * iteration. Bound work to three mailboxes and discard a stale backlog. */
  if(due > 3) { due = 1; can_last_tx_us = now_us - slot_us; }
  if(can_test_live.bus_off) { can_test_live.tx_no_mailbox_count++; return; }
  for(unsigned slot = 0; slot < due; ++slot) {
    uint8_t data[8]; can_last_tx_us += slot_us;
    for(unsigned i = 0; i < 4; ++i) {
      uint8_t type = can_cycle + 1; can_cycle = (can_cycle + 1) & 3U;
      if(!(can_config.output_mask & (1U << (type - 1)))) continue;
      can_damiao_pack(type, &can_sample, data);
      /* Manual specifies MST_ID for responses but no active-frame ID.
       * Preserve this project's existing active output on CAN_ID. */
      (void)transmit(can_config.node_id, data); break;
    }
  }
#endif
}
