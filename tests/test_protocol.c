/* Host regression: gcc -std=c11 -Wall -Wextra -Iinc/telemetry tests/test_protocol.c src/drivers/protocol.c -o test_protocol */
#include "protocol.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(ahrs_payload_imu_t) == 28, "Unexpected IMU wire layout");
_Static_assert(sizeof(ahrs_payload_filter_config_t) == 16, "Filter config wire layout");
_Static_assert(sizeof(ahrs_payload_fusion_diagnostic_t) == 60, "Fusion diagnostic wire layout");
_Static_assert(AHRS_MAX_FRAME_LEN >= AHRS_FRAME_OVERHEAD + sizeof(ahrs_payload_imu_t),
               "IMU frame will not fit the DMA buffer");

static uint32_t callbacks;
static uint32_t text_callbacks[2], text_frames;

static void on_vofa(void *ctx)
{
  unsigned port = (unsigned)(uintptr_t)ctx;
  assert(port < 2);
  ++text_callbacks[port];
}

static void on_text_frame(uint8_t id, uint8_t seq, const uint8_t *data, uint8_t len, void *ctx)
{
  (void)ctx;
  assert(id == 0x7e && seq == 42 && len == 8);
  assert(memcmp(data, "vofaVOFA", len) == 0);
  ++text_frames;
}

static void feed_text(protocol_parser_t *parser, const char *text)
{
  while(*text) protocol_parser_feed_byte(parser, (uint8_t)*text++);
}

static void test_vofa_text_command(void)
{
  protocol_parser_t uart, usb;
  uint8_t frame[AHRS_MAX_FRAME_LEN];
  protocol_parser_init(&uart, on_text_frame, (void *)(uintptr_t)0);
  protocol_parser_init(&usb, on_text_frame, (void *)(uintptr_t)1);
  feed_text(&uart, "vofa"); /* Opt-in only. */
  assert(text_callbacks[0] == 0);
  protocol_parser_set_vofa_callback(&uart, on_vofa);
  protocol_parser_set_vofa_callback(&usb, on_vofa);
  feed_text(&uart, "vo");
  feed_text(&usb, "fa");
  assert(text_callbacks[0] == 0 && text_callbacks[1] == 0);
  feed_text(&uart, "fa");
  assert(text_callbacks[0] == 1 && text_callbacks[1] == 0);
  feed_text(&usb, "VoFa\r\nVOFA\n");
  assert(text_callbacks[1] == 2);
  feed_text(&uart, "\x76\x6f\x66\x61\x20");
  assert(text_callbacks[0] == 2);
  feed_text(&uart, "vo\rfa vvofa vovofa vofxfa");
  assert(text_callbacks[0] == 4);
  /* Payload and CRC bytes are never interpreted as ASCII commands, even
   * when the binary frame is split across reads or has a bad checksum. */
  uint16_t n = protocol_pack_frame(frame, sizeof(frame), 0x7e, 42, "vofaVOFA", 8);
  feed_text(&uart, "vo");
  for(unsigned i = 0; i < n; ++i) protocol_parser_feed_byte(&uart, frame[i]);
  feed_text(&uart, "fa");
  assert(text_frames == 1 && text_callbacks[0] == 4);
  frame[n - 1] ^= 1;
  for(unsigned i = 0; i < n; ++i) protocol_parser_feed_byte(&usb, frame[i]);
  assert(text_frames == 1 && usb.crc_errors == 1 && text_callbacks[1] == 2);
  /* Recover immediately after binary traffic and a stray AA sync byte. */
  feed_text(&usb, "vofa");
  protocol_parser_feed_byte(&usb, AHRS_SYNC1);
  feed_text(&usb, "vofa");
  assert(text_callbacks[1] == 4);
  protocol_parser_set_vofa_callback(&usb, NULL);
  feed_text(&usb, "vofa");
  assert(text_callbacks[1] == 4);
}

static void test_selected_output(void)
{
  const float fields[9] = {10, 20, 30, 1, 2, 3, 40, 50, 60};
  struct { uint8_t frame[AHRS_MAX_FRAME_LEN]; uint8_t guard[8]; } out;
  for(unsigned mask = 0; mask <= AHRS_FIELDS_ALL; ++mask) {
    unsigned count = 0;
    for(unsigned i = 0; i < 9; ++i) count += !!(mask & (1U << i));
    for(unsigned format = 0; format <= 1; ++format) {
      output_config_t config = {format, 0, mask};
      memset(&out, 0xA5, sizeof(out));
      uint16_t n = protocol_pack_output(out.frame, sizeof(out.frame), 42, &config, fields, 25, 1, 1234);
      if(mask == 0) { assert(n == 0); continue; }
      assert(n == 4U * count + (format ? 11U : 4U));
      unsigned offset = format ? 9U : 0U;
      if(format) {
        assert(out.frame[2] == AHRS_MSG_SELECTED_DATA && out.frame[3] == 4 + count * 4);
        assert((unsigned)(out.frame[5] | out.frame[6] << 8) == mask);
        assert((out.frame[7] | out.frame[8] << 8) == 1234);
        assert(protocol_crc16(out.frame + 2, n - 4) == (out.frame[n - 2] | out.frame[n - 1] << 8));
      } else assert(memcmp(out.frame + n - 4, "\x00\x00\x80\x7F", 4) == 0);
      for(unsigned i = 0; i < 9; ++i) if(mask & (1U << i)) {
        float value; memcpy(&value, out.frame + offset, 4); offset += 4;
        assert(value == fields[i]);
      }
      for(unsigned i = n; i < sizeof(out); ++i) assert(((uint8_t *)&out)[i] == 0xA5);
      memset(&out, 0xA5, sizeof(out));
      assert(protocol_pack_output(out.frame, n - 1, 42, &config, fields, 25, 1, 1234) == 0);
      for(unsigned i = 0; i < sizeof(out); ++i) assert(((uint8_t *)&out)[i] == 0xA5);
    }
  }
  output_config_t invalid = {0, 0, 0x200};
  assert(!protocol_output_config_valid(&invalid));
  for(unsigned mode = 0; mode <= 4; ++mode) {
    output_config_t config = {OUTPUT_FORMAT_LEGACY, mode, 7};
    const unsigned expected[] = {16, 23, 19, 35, 28};
    assert(protocol_pack_output(out.frame, sizeof(out.frame), 42, &config, fields, 25, 1, 1234) == expected[mode]);
  }
}
static void on_frame(uint8_t id, uint8_t seq, const uint8_t *data, uint8_t len, void *ctx)
{
  (void)data; (void)len; (void)ctx;
  assert(id == AHRS_MSG_IMU_RAW && seq == 42);
  callbacks++;
}

int main(void)
{
  test_selected_output();
  test_vofa_text_command();
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
  puts("protocol packing, capacity, guard, CRC, parser and VOFA text command: OK");
  return 0;
}
