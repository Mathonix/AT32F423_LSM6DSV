#include "protocol.h"
#include <string.h>
#include <math.h>

/* CRC-CCITT-FALSE, polynomial 0x1021, init 0xffff, no reflection.
 * Immutable Flash table; the wire format and parser checks are unchanged. */
static const uint16_t crc16_table[256] = {
  0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
  0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
  0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U,
  0x9339U, 0x8318U, 0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU,
  0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U, 0x5485U,
  0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU,
  0x3653U, 0x2672U, 0x1611U, 0x0630U, 0x76D7U, 0x66F6U, 0x5695U, 0x46B4U,
  0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU,
  0x48C4U, 0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U,
  0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U, 0xA90AU, 0xB92BU,
  0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U,
  0xDBFDU, 0xCBDCU, 0xFBBFU, 0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU,
  0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
  0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U,
  0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U, 0x2E32U, 0x1E51U, 0x0E70U,
  0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U,
  0x9188U, 0x81A9U, 0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU,
  0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U, 0x6067U,
  0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU,
  0x02B1U, 0x1290U, 0x22F3U, 0x32D2U, 0x4235U, 0x5214U, 0x6277U, 0x7256U,
  0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU,
  0x34E2U, 0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U,
  0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU, 0xC71DU, 0xD73CU,
  0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U,
  0xD94CU, 0xC96DU, 0xF90EU, 0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU,
  0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
  0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBBU, 0xBB9AU,
  0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U, 0x1AD0U, 0x2AB3U, 0x3A92U,
  0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U,
  0x7C26U, 0x6C07U, 0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U,
  0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U, 0x9FF8U,
  0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U,
};

uint16_t protocol_crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  while(len--)
  {
    const uint8_t index = (uint8_t)((crc >> 8) ^ *data++);
    crc = (uint16_t)((crc << 8) ^ crc16_table[index]);
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

void protocol_parser_set_vofa_callback(protocol_parser_t *parser, protocol_vofa_cb_t cb)
{
  if(parser == NULL) return;
  parser->vofa_cb = cb;
  parser->vofa_match = 0U;
}

static void protocol_feed_text(protocol_parser_t *parser, uint8_t byte)
{
  static const uint8_t command[] = "vofa";
  if(parser->vofa_cb == NULL) return;
  if(byte >= 'A' && byte <= 'Z') byte = (uint8_t)(byte + ('a' - 'A'));
  if(byte == command[parser->vofa_match]) ++parser->vofa_match;
  else parser->vofa_match = byte == command[0] ? 1U : 0U;
  if(parser->vofa_match == sizeof(command) - 1U) {
    parser->vofa_match = 0U;
    parser->vofa_cb(parser->user_data);
  }
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
        parser->vofa_match = 0U;
        parser->state = PARSE_STATE_SYNC2;
      }
      else protocol_feed_text(parser, byte);
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
        protocol_feed_text(parser, byte);
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

uint16_t protocol_pack_device_model(uint8_t *buf, uint16_t capacity, uint8_t seq)
{
  static const char model[] = "AT32";
  return protocol_pack_frame(buf, capacity, AHRS_MSG_DEVICE_MODEL, seq,
                             model, (uint8_t)(sizeof(model) - 1U));
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

static int16_t compact_scaled(float value, float scale, uint8_t *flags)
{
  float scaled=value*scale;
  if(!isfinite(scaled)) {
    *flags |= AHRS_FLAG_VALUE_CLIPPED | AHRS_FLAG_SENSOR_ERROR;
    return 0;
  }
  if(scaled>32767.0f) { *flags |= AHRS_FLAG_VALUE_CLIPPED; return INT16_MAX; }
  if(scaled< -32768.0f) { *flags |= AHRS_FLAG_VALUE_CLIPPED; return INT16_MIN; }
  return (int16_t)scaled;
}

uint16_t protocol_pack_compact(uint8_t *buf, uint16_t capacity, uint8_t seq, float roll, float pitch, float yaw, float gz, uint8_t flags, uint16_t timestamp_ms)
{
  ahrs_payload_compact_t payload;
  payload.roll_x100 = compact_scaled(roll,100.0f,&flags);
  payload.pitch_x100 = compact_scaled(pitch,100.0f,&flags);
  payload.yaw_x100 = compact_scaled(yaw,100.0f,&flags);
  payload.gz_x10 = compact_scaled(gz,10.0f,&flags);
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

uint16_t protocol_pack_system_info(uint8_t *buf, uint16_t capacity, uint8_t seq, uint32_t fusion_hz, uint32_t out_hz, uint32_t skip_n, float temp_c, uint8_t stream_mode, uint8_t can_ok)
{
  ahrs_payload_system_info_t payload;
  payload.fusion_hz = fusion_hz;
  payload.out_hz = out_hz;
  payload.skip_n = skip_n>UINT16_MAX ? UINT16_MAX : (uint16_t)skip_n;
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

int protocol_output_config_valid(const output_config_t *config)
{
  return config != NULL && config->format <= OUTPUT_FORMAT_LEGACY &&
         config->legacy_mode <= STREAM_MODE_VOFA_6CH &&
         (config->field_mask & ~AHRS_FIELDS_ALL) == 0U;
}

uint16_t protocol_pack_output(uint8_t *buf, uint16_t capacity, uint8_t seq,
                              const output_config_t *config, const float fields[AHRS_FIELD_COUNT],
                              float temperature, uint8_t flags, uint16_t timestamp_ms)
{
  uint8_t payload[4U + 4U * AHRS_FIELD_COUNT];
  uint16_t used = 0U, i;
  static const uint8_t tail[4] = {0U, 0U, 0x80U, 0x7FU};
  if(buf == NULL || fields == NULL || !protocol_output_config_valid(config)) return 0U;
  if(config->format == OUTPUT_FORMAT_LEGACY)
  {
    switch(config->legacy_mode)
    {
      case STREAM_MODE_BIN_ATT:
        return protocol_pack_attitude(buf, capacity, seq, fields[2], fields[1], fields[0], flags, timestamp_ms);
      case STREAM_MODE_BIN_COMPACT:
        return protocol_pack_compact(buf, capacity, seq, fields[2], fields[1], fields[0], fields[8], flags, timestamp_ms);
      case STREAM_MODE_BIN_IMU:
        return protocol_pack_imu(buf, capacity, seq, fields[6], fields[7], fields[8], fields[3], fields[4], fields[5], temperature, timestamp_ms);
      default:
        memcpy(payload, fields, 12U);
        used = 12U;
        if(config->legacy_mode == STREAM_MODE_VOFA_6CH)
        {
          memcpy(payload + used, &fields[8], 4U); used += 4U;
          memcpy(payload + used, &fields[5], 4U); used += 4U;
          memcpy(payload + used, &temperature, 4U); used += 4U;
        }
        break;
    }
  }
  else
  {
    if(config->field_mask == 0U) return 0U;
    if(config->format == OUTPUT_FORMAT_CUSTOM)
    {
      payload[0] = (uint8_t)config->field_mask;
      payload[1] = (uint8_t)(config->field_mask >> 8);
      payload[2] = (uint8_t)timestamp_ms;
      payload[3] = (uint8_t)(timestamp_ms >> 8);
      used = 4U;
    }
    for(i = 0U; i < AHRS_FIELD_COUNT; ++i)
      if((config->field_mask & (1U << i)) != 0U)
      {
        memcpy(payload + used, &fields[i], 4U);
        used += 4U;
      }
    if(config->format == OUTPUT_FORMAT_CUSTOM)
      return protocol_pack_frame(buf, capacity, AHRS_MSG_SELECTED_DATA, seq, payload, (uint8_t)used);
  }
  if(capacity < used + 4U) return 0U;
  memcpy(buf, payload, used);
  memcpy(buf + used, tail, 4U);
  return used + 4U;
}
