#include "can_test.h"
#include "at32f423_conf.h"
#include "app_config.h"
#include <string.h>

volatile can_test_live_t can_test_live = {
  .magic = CAN_TEST_LIVE_MAGIC
};

static uint32_t can_last_tx_us;
static uint32_t can_data_counter;
static uint8_t can_pending_mask;
static uint32_t can_last_status_us;

static float can_att_roll;
static float can_att_pitch;
static float can_att_yaw;
static float can_att_gx_rad;
static float can_att_gy_rad;
static float can_att_gz_rad;
static float can_att_ax_ms2;
static float can_att_ay_ms2;
static float can_att_az_ms2;
static float can_att_qw = 1.0f;
static float can_att_qx = 0.0f;
static float can_att_qy = 0.0f;
static float can_att_qz = 0.0f;
static float can_att_temp = 25.0f;
static uint8_t can_att_flags;
static volatile uint8_t can_cmd_flags;

static uint16_t damiao_can_id = APP_CAN_DEFAULT_CAN_ID;
static uint16_t damiao_mst_id = APP_CAN_DEFAULT_MST_ID;

static inline uint16_t damiao_float_to_uint(float x_float, float x_min, float x_max, int bits)
{
  float span = x_max - x_min;
  float offset = x_min;
  if(x_float < x_min) x_float = x_min;
  if(x_float > x_max) x_float = x_max;
  return (uint16_t)((x_float - offset) * ((float)((1U << bits) - 1U)) / span);
}

void can_test_update_data(float roll, float pitch, float yaw,
                          float gx_dps, float gy_dps, float gz_dps,
                          float ax_g, float ay_g, float az_g,
                          float qw, float qx, float qy, float qz,
                          float temp_c, uint8_t flags)
{
  const float dps_to_rad = 3.14159265f / 180.0f;
  const float g_to_ms2   = 9.80665f;

  can_att_roll   = roll;
  can_att_pitch  = pitch;
  can_att_yaw    = yaw;
  can_att_gx_rad = gx_dps * dps_to_rad;
  can_att_gy_rad = gy_dps * dps_to_rad;
  can_att_gz_rad = gz_dps * dps_to_rad;
  can_att_ax_ms2 = ax_g * g_to_ms2;
  can_att_ay_ms2 = ay_g * g_to_ms2;
  can_att_az_ms2 = az_g * g_to_ms2;
  can_att_qw     = qw;
  can_att_qx     = qx;
  can_att_qy     = qy;
  can_att_qz     = qz;
  can_att_temp   = temp_c;
  can_att_flags  = flags;
}

uint8_t can_test_get_cmd_flag(void)
{
  return can_cmd_flags;
}

void can_test_clear_cmd_flag(uint8_t flag)
{
  can_cmd_flags = (uint8_t)(can_cmd_flags & (uint8_t)~flag);
}

int can_test_set_node_id(uint16_t node_id)
{
  if(node_id > 0x7FFU) return -1;
  damiao_can_id = node_id;
  return 0;
}

uint16_t can_test_get_node_id(void)
{
  return damiao_can_id;
}

static void damiao_pack_euler(uint8_t data[8])
{
  uint16_t pitch_u = damiao_float_to_uint(can_att_pitch, DAMIAO_PITCH_MIN, DAMIAO_PITCH_MAX, 16);
  uint16_t yaw_u   = damiao_float_to_uint(can_att_yaw,   DAMIAO_YAW_MIN,   DAMIAO_YAW_MAX,   16);
  uint16_t roll_u  = damiao_float_to_uint(can_att_roll,  DAMIAO_ROLL_MIN,  DAMIAO_ROLL_MAX,  16);

  data[0] = DAMIAO_CAN_TYPE_EULER; /* 0x03 */
  data[1] = 0x00U;
  data[2] = (uint8_t)(pitch_u & 0xFFU);
  data[3] = (uint8_t)((pitch_u >> 8) & 0xFFU);
  data[4] = (uint8_t)(yaw_u & 0xFFU);
  data[5] = (uint8_t)((yaw_u >> 8) & 0xFFU);
  data[6] = (uint8_t)(roll_u & 0xFFU);
  data[7] = (uint8_t)((roll_u >> 8) & 0xFFU);
}

static void damiao_pack_gyro(uint8_t data[8])
{
  uint16_t gx_u = damiao_float_to_uint(can_att_gx_rad, DAMIAO_GYRO_MIN, DAMIAO_GYRO_MAX, 16);
  uint16_t gy_u = damiao_float_to_uint(can_att_gy_rad, DAMIAO_GYRO_MIN, DAMIAO_GYRO_MAX, 16);
  uint16_t gz_u = damiao_float_to_uint(can_att_gz_rad, DAMIAO_GYRO_MIN, DAMIAO_GYRO_MAX, 16);

  data[0] = DAMIAO_CAN_TYPE_GYRO; /* 0x02 */
  data[1] = 0x00U;
  data[2] = (uint8_t)(gx_u & 0xFFU);
  data[3] = (uint8_t)((gx_u >> 8) & 0xFFU);
  data[4] = (uint8_t)(gy_u & 0xFFU);
  data[5] = (uint8_t)((gy_u >> 8) & 0xFFU);
  data[6] = (uint8_t)(gz_u & 0xFFU);
  data[7] = (uint8_t)((gz_u >> 8) & 0xFFU);
}

static void damiao_pack_accel(uint8_t data[8])
{
  uint16_t ax_u = damiao_float_to_uint(can_att_ax_ms2, DAMIAO_ACCEL_MIN, DAMIAO_ACCEL_MAX, 16);
  uint16_t ay_u = damiao_float_to_uint(can_att_ay_ms2, DAMIAO_ACCEL_MIN, DAMIAO_ACCEL_MAX, 16);
  uint16_t az_u = damiao_float_to_uint(can_att_az_ms2, DAMIAO_ACCEL_MIN, DAMIAO_ACCEL_MAX, 16);
  int16_t t_i   = (int16_t)(can_att_temp + 0.5f);
  if(t_i < 0) t_i = 0;
  if(t_i > 255) t_i = 255;

  data[0] = DAMIAO_CAN_TYPE_ACCEL; /* 0x01 */
  data[1] = (uint8_t)t_i;
  data[2] = (uint8_t)(ax_u & 0xFFU);
  data[3] = (uint8_t)((ax_u >> 8) & 0xFFU);
  data[4] = (uint8_t)(ay_u & 0xFFU);
  data[5] = (uint8_t)((ay_u >> 8) & 0xFFU);
  data[6] = (uint8_t)(az_u & 0xFFU);
  data[7] = (uint8_t)((az_u >> 8) & 0xFFU);
}

static void damiao_pack_quat(uint8_t data[8])
{
  uint16_t w_u = damiao_float_to_uint(can_att_qw, DAMIAO_QUAT_MIN, DAMIAO_QUAT_MAX, 14);
  uint16_t x_u = damiao_float_to_uint(can_att_qx, DAMIAO_QUAT_MIN, DAMIAO_QUAT_MAX, 14);
  uint16_t y_u = damiao_float_to_uint(can_att_qy, DAMIAO_QUAT_MIN, DAMIAO_QUAT_MAX, 14);
  uint16_t z_u = damiao_float_to_uint(can_att_qz, DAMIAO_QUAT_MIN, DAMIAO_QUAT_MAX, 14);

  data[0] = DAMIAO_CAN_TYPE_QUAT; /* 0x04 */
  data[1] = (uint8_t)((w_u >> 6) & 0xFFU);
  data[2] = (uint8_t)(((w_u & 0x3FU) << 2) | ((x_u >> 12) & 0x03U));
  data[3] = (uint8_t)((x_u >> 4) & 0xFFU);
  data[4] = (uint8_t)(((x_u & 0x0FU) << 4) | ((y_u >> 10) & 0x0FU));
  data[5] = (uint8_t)((y_u >> 2) & 0xFFU);
  data[6] = (uint8_t)(((y_u & 0x03U) << 6) | ((z_u >> 8) & 0x3FU));
  data[7] = (uint8_t)(z_u & 0xFFU);
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

    /* 1. Damiao Fast Request: [can_id_L, can_id_H, reg, 0xCC] */
    if((rx_message.id_type == CAN_ID_STANDARD) &&
       (rx_message.dlc == 4U) &&
       (rx_message.data[3] == DAMIAO_REQ_HEADER))
    {
      uint16_t req_id = (uint16_t)rx_message.data[0] | ((uint16_t)rx_message.data[1] << 8);
      uint8_t reg = rx_message.data[2];

      if((req_id == damiao_can_id) || (req_id == 0U) || (req_id == 0x6FFU))
      {
        can_tx_message_type reply;
        reply.standard_id = damiao_mst_id;
        reply.extended_id = 0U;
        reply.id_type = CAN_ID_STANDARD;
        reply.frame_type = CAN_TFT_DATA;
        reply.dlc = 8U;

        if(reg == DAMIAO_REG_ACCEL)
        {
          damiao_pack_accel(reply.data);
          (void)can_message_transmit(CAN2, &reply);
        }
        else if(reg == DAMIAO_REG_GYRO)
        {
          damiao_pack_gyro(reply.data);
          (void)can_message_transmit(CAN2, &reply);
        }
        else if(reg == DAMIAO_REG_EULER)
        {
          damiao_pack_euler(reply.data);
          (void)can_message_transmit(CAN2, &reply);
        }
        else if(reg == DAMIAO_REG_QUAT)
        {
          damiao_pack_quat(reply.data);
          (void)can_message_transmit(CAN2, &reply);
        }
      }
    }
    /* 2. Damiao Register Request: [0xCC, RID, R/W, 0xDD, DATA[4..7]] */
    else if((rx_message.id_type == CAN_ID_STANDARD) &&
            (rx_message.dlc == 8U) &&
            (rx_message.data[0] == DAMIAO_REQ_HEADER) &&
            (rx_message.data[3] == DAMIAO_REQ_TAIL))
    {
      uint8_t rid = rx_message.data[1];
      uint8_t rw  = rx_message.data[2];
      can_tx_message_type reply;

      reply.standard_id = damiao_mst_id;
      reply.extended_id = 0U;
      reply.id_type = CAN_ID_STANDARD;
      reply.frame_type = CAN_TFT_DATA;
      reply.dlc = 8U;
      reply.data[0] = DAMIAO_REQ_HEADER;
      reply.data[1] = rid;
      reply.data[2] = DAMIAO_REQ_TAIL;
      reply.data[3] = DAMIAO_ACK_SUCCESS;
      memset(&reply.data[4], 0, 4U);

      if(rid == DAMIAO_REG_REBOOT)
      {
        can_cmd_flags |= CAN_CMD_FLAG_REBOOT;
      }
      else if(rid == DAMIAO_REG_ZERO_YAW)
      {
        can_cmd_flags |= CAN_CMD_FLAG_ZERO_YAW;
      }
      else if(rid == DAMIAO_REG_CALIB_GYRO)
      {
        can_cmd_flags |= CAN_CMD_FLAG_RECAL;
      }
      else if(rid == DAMIAO_REG_CAN_ID)
      {
        if(rw == 1U) damiao_can_id = rx_message.data[4];
        reply.data[4] = (uint8_t)damiao_can_id;
      }
      else if(rid == DAMIAO_REG_MST_ID)
      {
        if(rw == 1U) damiao_mst_id = (uint16_t)rx_message.data[4] | ((uint16_t)rx_message.data[5] << 8);
        reply.data[4] = (uint8_t)damiao_mst_id;
        reply.data[5] = (uint8_t)(damiao_mst_id >> 8);
      }
      else if(rid == DAMIAO_REG_EULER)
      {
        damiao_pack_euler(reply.data);
      }
      else if(rid == DAMIAO_REG_GYRO)
      {
        damiao_pack_gyro(reply.data);
      }
      else if(rid == DAMIAO_REG_ACCEL)
      {
        damiao_pack_accel(reply.data);
      }
      else if(rid == DAMIAO_REG_QUAT)
      {
        damiao_pack_quat(reply.data);
      }
      else
      {
        reply.data[3] = DAMIAO_ACK_NO_REG;
      }

      (void)can_message_transmit(CAN2, &reply);
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
#if APP_CAN_TX_ENABLE
  can_tx_message_type tx_message;
  uint8_t mailbox;
#endif

  if(can_test_live.init_ok == 0U)
  {
    return;
  }

  if((uint32_t)(now_us - can_last_status_us) >= 1000U)
  {
    can_test_poll_mailboxes();
#if APP_CAN_RX_ENABLE
    can_test_poll_receive(now_us / 1000U);
#endif
    can_last_status_us = now_us;
    can_test_snapshot(now_us / 1000U);
  }

#if APP_CAN_TX_ENABLE
  if((uint32_t)(now_us - can_last_tx_us) < APP_CAN_TX_PERIOD_US)
  {
    return;
  }
  /* Preserve the 1 kHz phase instead of accumulating main-loop jitter.
   * If execution was delayed by more than one period, skip the backlog rather
   * than emitting a burst of stale attitude frames. */
  can_last_tx_us += APP_CAN_TX_PERIOD_US;
  if((uint32_t)(now_us - can_last_tx_us) >= APP_CAN_TX_PERIOD_US)
  {
    can_last_tx_us = now_us;
  }

  if(can_test_live.bus_off != 0U)
  {
    can_test_live.tx_no_mailbox_count++;
    return;
  }

  tx_message.standard_id = damiao_can_id;
  tx_message.extended_id = 0U;
  tx_message.id_type = CAN_ID_STANDARD;
  tx_message.frame_type = CAN_TFT_DATA;
  tx_message.dlc = 8U;

#if (APP_CAN_DAMIAO_MODE == 0U)
  /* Mode 0: Euler angle frame (0x03: Pitch, Yaw, Roll) */
  damiao_pack_euler(tx_message.data);
#elif (APP_CAN_DAMIAO_MODE == 1U)
  /* Mode 1: Interleave Euler (0x03) and Gyro (0x02) */
  {
    static uint8_t can_toggle = 0U;
    can_toggle ^= 1U;
    if(can_toggle != 0U)
    {
      damiao_pack_euler(tx_message.data);
    }
    else
    {
      damiao_pack_gyro(tx_message.data);
    }
  }
#else
  /* Mode 2: Cycle all 4 frames: Euler -> Gyro -> Accel -> Quat */
  {
    static uint8_t can_cycle = 0U;
    if(can_cycle == 0U)
    {
      damiao_pack_euler(tx_message.data);
    }
    else if(can_cycle == 1U)
    {
      damiao_pack_gyro(tx_message.data);
    }
    else if(can_cycle == 2U)
    {
      damiao_pack_accel(tx_message.data);
    }
    else
    {
      damiao_pack_quat(tx_message.data);
    }
    can_cycle = (uint8_t)((can_cycle + 1U) & 3U);
  }
#endif

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
  can_test_snapshot(now_us / 1000U);
#endif
}
