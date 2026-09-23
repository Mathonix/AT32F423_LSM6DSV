/* Host regression: gcc -std=c11 -Wall -Wextra -Iinc/telemetry tests/test_protocol.c src/drivers/protocol.c -o test_protocol */
#include "protocol.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(ahrs_payload_imu_t) == 28, "Unexpected IMU wire layout");
_Static_assert(AHRS_MAX_FRAME_LEN >= AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_imu_t),
               "IMU frame will not fit the DMA buffer");

static uint32_t callbacks;
static void on_frame(uint8_t id, uint8_t seq, const uint8_t *data, uint8_t len, void *ctx)
{
  (void)data; (void)len; (void)ctx;
  assert(id == AHRS_MSG_IMU_RAW && seq == 42);
  callbacks++;
}

int main(void)
{
  struct { uint8_t frame[AHRS_MAX_FRAME_LEN]; uint8_t guard[8]; } out;
  protocol_parser_t parser;
  uint8_t payload[AHRS_MAX_PAYLOAD_LEN] = {0};
  uint16_t len, i;

  memset(&out, 0xA5, sizeof(out));
  assert(protocol_pack_frame(out.frame, 6, 1, 0, NULL, 0) == 0);
  assert(protocol_pack_frame(out.frame, sizeof(out.frame), 1, 0, NULL, 1) == 0);
  assert(protocol_pack_frame(out.frame, sizeof(out.frame), 1, 0, payload, AHRS_MAX_PAYLOAD_LEN + 1) == 0);
  for(i = 0; i < sizeof(out); ++i) assert(((const uint8_t *)&out)[i] == 0xA5);

  assert(protocol_pack_imu(out.frame, 34, 42, 1, 2, 3, 4, 5, 6, 25, 123) == 0);
  assert(out.frame[34] == 0xA5);
  len = protocol_pack_imu(out.frame, sizeof(out.frame), 42, 1, 2, 3, 4, 5, 6, 25, 123);
  assert(len == 35 && out.frame[34] != 0xA5);
  for(i = 0; i < sizeof(out.guard); ++i) assert(out.guard[i] == 0xA5);
  protocol_parser_init(&parser, on_frame, NULL);
  for(i = 0; i < len; ++i) protocol_parser_feed_byte(&parser, out.frame[i]);
  assert(callbacks == 1 && parser.crc_errors == 0);
  out.frame[6] ^= 1;
  for(i = 0; i < len; ++i) protocol_parser_feed_byte(&parser, out.frame[i]);
  assert(callbacks == 1 && parser.crc_errors == 1);

  assert(protocol_pack_attitude(out.frame, sizeof(out.frame), 1, 1, 2, 3, 0, 0) ==
         AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_attitude_t));
  assert(protocol_pack_quaternion(out.frame, sizeof(out.frame), 1, 1, 0, 0, 0, 0) ==
         AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_quaternion_t));
  assert(protocol_pack_compact(out.frame, sizeof(out.frame), 1, 1, 2, 3, 4, 0, 0) ==
         AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_compact_t));
  assert(protocol_pack_system_info(out.frame, sizeof(out.frame), 1, 2000, 100, 0, 25, 0, 0) ==
         AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_system_info_t));
  assert(protocol_pack_ack(out.frame, sizeof(out.frame), 1, AHRS_CMD_PING, 0, 0) ==
         AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_ack_t));
  for(i = 0; i < sizeof(out.guard); ++i) assert(out.guard[i] == 0xA5);
  puts("protocol packing, capacity, guard, CRC and parser: OK");
  return 0;
}
