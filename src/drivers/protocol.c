#include "protocol.h"
#include <string.h>

uint16_t protocol_crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  uint16_t i;
  while(len--)
  {
    crc ^= (uint16_t)((uint16_t)(*data++) << 8);
    for(i = 0U; i < 8U; ++i)
    {
      if((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

void protocol_parser_init(protocol_parser_t *parser, protocol_frame_cb_t cb, void *user_data)
{
  if(parser == NULL)
  {
    return;
  }
  memset(parser, 0, sizeof(*parser));
  parser->state = PARSE_STATE_SYNC1;
  parser->frame_cb = cb;
  parser->user_data = user_data;
}

void protocol_parser_feed_byte(protocol_parser_t *parser, uint8_t byte)
{
  if(parser == NULL)
  {
    return;
  }

  switch(parser->state)
  {
    case PARSE_STATE_SYNC1:
      if(byte == AHRS_SYNC1)
      {
        parser->state = PARSE_STATE_SYNC2;
      }
      break;

    case PARSE_STATE_SYNC2:
      if(byte == AHRS_SYNC2)
      {
        parser->state = PARSE_STATE_MSG_ID;
      }
      else if(byte == AHRS_SYNC1)
      {
        parser->state = PARSE_STATE_SYNC2;
      }
      else
      {
        parser->state = PARSE_STATE_SYNC1;
      }
      break;

    case PARSE_STATE_MSG_ID:
      parser->msg_id = byte;
      parser->state = PARSE_STATE_LEN;
      break;

    case PARSE_STATE_LEN:
      if(byte <= AHRS_MAX_PAYLOAD_LEN)
      {
        parser->payload_len = byte;
        parser->state = PARSE_STATE_SEQ;
      }
      else
      {
        parser->state = PARSE_STATE_SYNC1;
      }
      break;

    case PARSE_STATE_SEQ:
      parser->seq = byte;
      parser->payload_idx = 0U;
      if(parser->payload_len == 0U)
      {
        parser->state = PARSE_STATE_CRC_L;
      }
      else
      {
        parser->state = PARSE_STATE_PAYLOAD;
      }
      break;

    case PARSE_STATE_PAYLOAD:
      parser->payload[parser->payload_idx++] = byte;
      if(parser->payload_idx >= parser->payload_len)
      {
        parser->state = PARSE_STATE_CRC_L;
      }
      break;

    case PARSE_STATE_CRC_L:
      parser->rx_crc = (uint16_t)byte;
      parser->state = PARSE_STATE_CRC_H;
      break;

    case PARSE_STATE_CRC_H:
      parser->rx_crc |= (uint16_t)((uint16_t)byte << 8);
      {
        uint8_t hdr[3];
        uint16_t calc_crc;
        hdr[0] = parser->msg_id;
        hdr[1] = parser->payload_len;
        hdr[2] = parser->seq;

        calc_crc = protocol_crc16(hdr, 3U);

        /* Compute continuous CRC over hdr[3] + payload */
        {
          uint8_t check_buf[3 + AHRS_MAX_PAYLOAD_LEN];
          check_buf[0] = parser->msg_id;
          check_buf[1] = (uint8_t)parser->payload_idx;
          check_buf[2] = parser->seq;
          if(parser->payload_idx > 0U)
          {
            memcpy(&check_buf[3], parser->payload, parser->payload_idx);
          }
          calc_crc = protocol_crc16(check_buf, (uint16_t)(3U + parser->payload_idx));
        }

        if(calc_crc == parser->rx_crc)
        {
          parser->parsed_frames++;
          if(parser->frame_cb != NULL)
          {
            parser->frame_cb(parser->msg_id, parser->seq, parser->payload, parser->payload_idx, parser->user_data);
          }
        }
        else
        {
          parser->crc_errors++;
        }
      }
      parser->state = PARSE_STATE_SYNC1;
      break;

    default:
      parser->state = PARSE_STATE_SYNC1;
      break;
  }
}

uint16_t protocol_pack_frame(uint8_t *buf, uint16_t capacity, uint8_t msg_id, uint8_t seq, const void *payload, uint8_t len)
{
  uint16_t crc;
  if((buf == NULL) || (len > AHRS_MAX_PAYLOAD_LEN) ||
     ((uint16_t)(AHRS_FRAME_OVERHEAD + len) > capacity) ||
     ((len != 0U) && (payload == NULL)))
  {
    return 0U;
  }
  buf[0] = AHRS_SYNC1;
  buf[1] = AHRS_SYNC2;
  buf[2] = msg_id;
  buf[3] = len;
  buf[4] = seq;
  if((payload != NULL) && (len > 0U))
  {
    memcpy(&buf[AHRS_HEADER_LEN], payload, len);
  }
  crc = protocol_crc16(&buf[2], (uint16_t)(3U + len));
  buf[AHRS_HEADER_LEN + len] = (uint8_t)(crc & 0xFFU);
  buf[AHRS_HEADER_LEN + len + 1U] = (uint8_t)((crc >> 8) & 0xFFU);
  return (uint16_t)(AHRS_FRAME_OVERHEAD + len);
}

uint16_t protocol_pack_attitude(uint8_t *buf, uint16_t capacity, uint8_t seq, float roll, float pitch, float yaw, uint8_t flags, uint16_t timestamp_ms)
{
  ahrs_payload_attitude_t payload;
  payload.roll = roll;
  payload.pitch = pitch;
  payload.yaw = yaw;
  payload.flags = flags;
  payload.reserved = 0U;
  payload.timestamp_ms = timestamp_ms;
  return protocol_pack_frame(buf, capacity, AHRS_MSG_ATTITUDE_EULER, seq, &payload, (uint8_t)sizeof(payload));
}

uint16_t protocol_pack_quaternion(uint8_t *buf, uint16_t capacity, uint8_t seq, float qw, float qx, float qy, float qz, uint16_t timestamp_ms)
{
  ahrs_payload_quaternion_t payload;
  payload.qw = qw;
  payload.qx = qx;
  payload.qy = qy;
  payload.qz = qz;
  payload.timestamp_ms = timestamp_ms;
  return protocol_pack_frame(buf, capacity, AHRS_MSG_QUATERNION, seq, &payload, (uint8_t)sizeof(payload));
}

uint16_t protocol_pack_compact(uint8_t *buf, uint16_t capacity, uint8_t seq, float roll, float pitch, float yaw, float gz, uint8_t flags, uint16_t timestamp_ms)
{
  ahrs_payload_compact_t payload;
  payload.roll_x100 = (int16_t)(roll * 100.0f);
  payload.pitch_x100 = (int16_t)(pitch * 100.0f);
  payload.yaw_x100 = (int16_t)(yaw * 100.0f);
  payload.gz_x10 = (int16_t)(gz * 10.0f);
  payload.flags = flags;
  payload.reserved = 0U;
  payload.timestamp_ms = timestamp_ms;
  return protocol_pack_frame(buf, capacity, AHRS_MSG_COMPACT, seq, &payload, (uint8_t)sizeof(payload));
}

uint16_t protocol_pack_imu(uint8_t *buf, uint16_t capacity, uint8_t seq, float gx, float gy, float gz, float ax, float ay, float az, float temp_c, uint16_t timestamp_ms)
{
  ahrs_payload_imu_t payload;
  payload.gx = gx;
  payload.gy = gy;
  payload.gz = gz;
  payload.ax = ax;
  payload.ay = ay;
  payload.az = az;
  payload.temp_c_x100 = (int16_t)(temp_c * 100.0f);
  payload.timestamp_ms = timestamp_ms;
  return protocol_pack_frame(buf, capacity, AHRS_MSG_IMU_RAW, seq, &payload, (uint8_t)sizeof(payload));
}

uint16_t protocol_pack_system_info(uint8_t *buf, uint16_t capacity, uint8_t seq, uint32_t fusion_hz, uint32_t out_hz, uint16_t skip_n, float temp_c, uint8_t stream_mode, uint8_t can_ok)
{
  ahrs_payload_system_info_t payload;
  payload.fusion_hz = fusion_hz;
  payload.out_hz = out_hz;
  payload.skip_n = skip_n;
  payload.temp_c_x100 = (int16_t)(temp_c * 100.0f);
  payload.stream_mode = stream_mode;
  payload.can_ok = can_ok;
  payload.reserved = 0U;
  return protocol_pack_frame(buf, capacity, AHRS_MSG_SYSTEM_INFO, seq, &payload, (uint8_t)sizeof(payload));
}

uint16_t protocol_pack_ack(uint8_t *buf, uint16_t capacity, uint8_t seq, uint8_t cmd_id, uint8_t status, uint16_t detail)
{
  ahrs_payload_ack_t payload;
  payload.cmd_id = cmd_id;
  payload.status = status;
  payload.detail = detail;
  return protocol_pack_frame(buf, capacity, AHRS_MSG_ACK, seq, &payload, (uint8_t)sizeof(payload));
}