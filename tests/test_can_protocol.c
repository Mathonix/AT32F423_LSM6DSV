#include "can_protocol.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
static uint16_t u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
int main(void)
{
  can_config_t c, next; can_config_defaults(&c);
  assert(sizeof(c) == 10 && can_config_valid(&c));
  assert(c.baud_index == 0 && c.output_mask == 4 && c.period_ms == 1);
  for(unsigned b = 0; b < 8; ++b) for(unsigned mask = 0; mask < 16; ++mask) {
    c.baud_index = b; c.output_mask = mask; c.period_ms = 100;
    assert(can_config_valid(&c));
  }
  c.period_ms = 1; assert(!can_config_valid(&c));
  c.active = 0; assert(can_config_valid(&c));
  c.period_ms = 10000; c.active = 1; c.baud_index = 0; assert(can_config_valid(&c));
  c.reserved = 1; assert(!can_config_valid(&c));
  can_config_defaults(&c);
  can_sample_t s = {.pitch = -90, .yaw = 180, .roll = 0, .temperature = 30,
    .ax_ms2 = -235.2f, .ay_ms2 = 0, .az_ms2 = 235.2f, .gx_rad = -34.88f, .gz_rad = 34.88f,
    .qw = 1, .qx = -1, .qy = 0, .qz = .5f};
  uint8_t d[8], reply[8], actions; uint16_t id;
  assert(!can_damiao_pack(3, &s, d));
  assert(d[0] == 3 && d[1] == 0 && u16(d+2) == 0 && u16(d+4) == 65535 && u16(d+6) == 32767);
  assert(!can_damiao_pack(1, &s, d));
  assert(d[1] == 30 && u16(d+2) == 0 && u16(d+4) == 32767 && u16(d+6) == 65535);
  assert(!can_damiao_pack(2, &s, d));
  assert(u16(d+2) == 0 && u16(d+4) == 32767 && u16(d+6) == 65535);
  assert(!can_damiao_pack(4, &s, d));
  assert(((d[1]<<6) | (d[2]>>2)) == 16383);
  assert((((d[2]&3)<<12) | (d[3]<<4) | (d[4]>>4)) == 0);
  assert((((d[4]&15)<<10) | (d[5]<<2) | (d[6]>>6)) == 8191);
  assert((((d[6]&63)<<8) | d[7]) == 12287);
  s.pitch = INFINITY; assert(!can_damiao_pack(3, &s, d) && u16(d+2) == 32767);
  uint8_t request[8] = {0xCC, 0x0D, 1, 0xDD, 0x23, 0x01, 0, 0};
  assert(!can_damiao_request(2, 8, request, &c, &s, &id, reply, &next, &actions));
  assert(can_damiao_request(c.node_id, 8, request, &c, &s, &id, reply, &next, &actions));
  assert(id == c.master_id && !reply[3] && actions == CAN_ACTION_CONFIG && next.node_id == 0x123);
  request[5] = 8;
  assert(can_damiao_request(c.node_id, 8, request, &c, &s, &id, reply, &next, &actions));
  assert(reply[3] == 2 && !actions && !memcmp(&next, &c, sizeof(c)));
  request[1] = 0xFE; request[2] = 0;
  can_damiao_request(c.node_id, 8, request, &c, &s, &id, reply, &next, &actions);
  assert(reply[3] == 2 && !actions);
  request[2] = 1;
  can_damiao_request(c.node_id, 8, request, &c, &s, &id, reply, &next, &actions);
  assert(!reply[3] && actions == CAN_ACTION_SAVE);
  request[1] = 7;
  can_damiao_request(c.node_id, 8, request, &c, &s, &id, reply, &next, &actions);
  assert(reply[3] == 3 && !actions);
  request[1] = 3; request[2] = 0;
  can_damiao_request(c.node_id, 8, request, &c, &s, &id, reply, &next, &actions);
  assert(reply[0] == 3); /* sensor response is a data frame, not register ACK */
  request[2] = 1;
  can_damiao_request(c.node_id, 8, request, &c, &s, &id, reply, &next, &actions);
  assert(reply[3] == 2);
  uint8_t fast[8] = {1, 0, 4, 0xCC};
  assert(can_damiao_request(c.master_id, 4, fast, &c, &s, &id, reply, &next, &actions) && reply[0] == 4);
  fast[0] = 0; assert(!can_damiao_request(c.master_id, 4, fast, &c, &s, &id, reply, &next, &actions));
  puts("CAN mapping, four groups, routing, register widths/permissions and load limits: OK");
}
