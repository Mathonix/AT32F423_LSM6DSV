#include "can_test.h"
#include "at32f423_conf.h"
#include "app_config.h"

volatile can_test_live_t can_test_live = {
  .magic = CAN_TEST_LIVE_MAGIC
};

static uint32_t can_last_tx_ms;
static uint32_t can_data_counter;
static uint8_t can_pending_mask;
static uint32_t can_last_status_ms;

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

static void can_test_poll_receive(uint32_t now_ms)
{
  uint8_t processed = 0U;

  while((can_receive_message_pending_get(CAN2, CAN_RX_FIFO0) != 0U) &&
        (processed < 8U))
  {
    can_rx_message_type rx_message;
    uint8_t i;

    can_message_receive(CAN2, CAN_RX_FIFO0, &rx_message);
    can_test_live.rx_count++;
    if(rx_message.id_type == CAN_ID_STANDARD)
    {
      can_test_live.rx_standard_count++;
      can_test_live.rx_last_id = rx_message.standard_id;
    }
    else
    {
      can_test_live.rx_extended_count++;
      can_test_live.rx_last_id = rx_message.extended_id;
    }
    can_test_live.rx_last_dlc = rx_message.dlc;
    can_test_live.rx_last_frame_type = (uint32_t)rx_message.frame_type;
    can_test_live.rx_last_millis = now_ms;
    for(i = 0U; i < 8U; ++i)
    {
      can_test_live.rx_last_data[i] = (i < rx_message.dlc) ? rx_message.data[i] : 0U;
    }
    processed++;
  }

  can_test_live.rx_pending = can_receive_message_pending_get(CAN2, CAN_RX_FIFO0);
  if(can_flag_get(CAN2, CAN_RF0OF_FLAG) != RESET)
  {
    can_test_live.rx_overrun_count++;
    can_flag_clear(CAN2, CAN_RF0OF_FLAG);
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

  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_CAN2_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_pins = GPIO_PINS_2 | GPIO_PINS_3;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOA, &gpio_init_struct);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE2, GPIO_MUX_9);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE3, GPIO_MUX_9);

  can_reset(CAN2);
  can_default_para_init(&can_base_struct);
  can_base_struct.mode_selection = CAN_MODE_COMMUNICATE;
  can_base_struct.aebo_enable = TRUE;
  can_base_struct.aed_enable = FALSE;
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
    filter_init.filter_number = 0U;
    filter_init.filter_bit = CAN_FILTER_32BIT;
    /* Zero ID and zero mask accepts every standard and extended frame. */
    filter_init.filter_id_high = 0U;
    filter_init.filter_id_low = 0U;
    filter_init.filter_mask_high = 0U;
    filter_init.filter_mask_low = 0U;
    can_filter_init(CAN2, &filter_init);
  }
#endif

  can_pending_mask = 0U;
  can_data_counter = 0U;
  can_last_tx_ms = 0U;
  can_last_status_ms = 0U;
  can_test_live.init_ok = 1U;
  can_test_snapshot(0U);
}

void can_test_task(uint32_t now_ms)
{
#if APP_CAN_TX_ENABLE
  can_tx_message_type tx_message;
  uint8_t mailbox;
#endif

  if(can_test_live.init_ok == 0U)
  {
    return;
  }

  /* Poll completion/error state at 100 Hz rather than adding register traffic
   * to every 2 kHz IMU iteration. The transmit scheduler remains independent. */
  if((uint32_t)(now_ms - can_last_status_ms) >= 10U)
  {
    can_test_poll_mailboxes();
#if APP_CAN_RX_ENABLE
    can_test_poll_receive(now_ms);
#endif
    can_last_status_ms = now_ms;
    can_test_snapshot(now_ms);
  }
#if APP_CAN_TX_ENABLE
  if((uint32_t)(now_ms - can_last_tx_ms) < APP_CAN_TX_PERIOD_MS)
  {
    return;
  }
  can_last_tx_ms = now_ms;

  tx_message.standard_id = APP_CAN_TX_STANDARD_ID;
  tx_message.extended_id = 0U;
  tx_message.id_type = CAN_ID_STANDARD;
  tx_message.frame_type = CAN_TFT_DATA;
  tx_message.dlc = 8U;
  tx_message.data[0] = (uint8_t)(can_data_counter >> 24);
  tx_message.data[1] = (uint8_t)(can_data_counter >> 16);
  tx_message.data[2] = (uint8_t)(can_data_counter >> 8);
  tx_message.data[3] = (uint8_t)can_data_counter;
  tx_message.data[4] = 0xA5U;
  tx_message.data[5] = 0x5AU;
  tx_message.data[6] = 0xC3U;
  tx_message.data[7] = 0x3CU;

  mailbox = can_message_transmit(CAN2, &tx_message);
  can_test_live.tx_count++;
  can_test_live.last_data_counter = can_data_counter++;
  if(mailbox <= CAN_TX_MAILBOX2)
  {
    can_pending_mask = (uint8_t)(can_pending_mask | (uint8_t)(1U << mailbox));
    can_test_live.last_mailbox = mailbox;
    can_test_live.last_status = (uint32_t)CAN_TX_STATUS_PENDING;
  }
  else
  {
    can_test_live.tx_no_mailbox_count++;
    can_test_live.last_mailbox = 0xFFFFFFFFU;
    can_test_live.last_status = (uint32_t)CAN_TX_STATUS_NO_EMPTY;
  }
  can_test_snapshot(now_ms);
#endif
}
