#include "can_protocol.h"
#include "app_config.h"
#include <string.h>
#include <math.h>

static const uint32_t rates[8] = {1000000, 500000, 400000, 250000, 200000, 100000, 50000, 25000};
static const uint16_t divisors[8] = {5, 10, 11, 20, 25, 50, 100, 200};
uint32_t can_baudrate(unsigned i) { return i < 8 ? rates[i] : 0; }
uint16_t can_baud_divisor(unsigned i) { return i < 8 ? divisors[i] : 0; }
unsigned can_output_count(uint8_t mask)
{
  unsigned n = 0;
  for(unsigned i = 0; i < 4; ++i) n += (mask >> i) & 1U;
  return n;
}
void can_config_defaults(can_config_t *c)
{
  memset(c, 0, sizeof(*c));
  c->node_id = APP_CAN_DEFAULT_CAN_ID; c->master_id = APP_CAN_DEFAULT_MST_ID;
  c->period_ms = APP_CAN_TX_PERIOD_US / 1000U;
  c->active = 1;
  c->output_mask = APP_CAN_DAMIAO_MODE == 0 ? 4 : APP_CAN_DAMIAO_MODE == 1 ? 6 : 15;
}
int can_config_valid(const can_config_t *c)
{
  if(!c || c->node_id > 0x7FF || c->master_id > 0x7FF || !c->period_ms || c->period_ms > 10000 ||
     c->baud_index > 7 || c->active > 1 || c->output_mask > 15 || c->reserved) return 0;
  /* 135 bits covers a worst-case stuffed standard DLC8 frame plus IFS.
   * Reserve at least 20% for commands/other nodes; reject overload, not silently drop groups. */
  return !c->active || can_output_count(c->output_mask) * 135000U <=
         (uint64_t)(can_baudrate(c->baud_index) / 5U) * 4U * c->period_ms;
}
static uint16_t mapped(float value, float minimum, float maximum, unsigned bits)
{
  if(!isfinite(value)) value = 0;
  if(value <= minimum) return 0;
  if(value >= maximum) return (1U << bits) - 1U;
  return (uint16_t)((value - minimum) * (float)((1U << bits) - 1U) / (maximum - minimum));
}
int can_damiao_pack(uint8_t type, const can_sample_t *s, uint8_t data[8])
{
  float v[3], limit;
  if(!s || !data || type < 1 || type > 4) return -1;
  memset(data, 0, 8); data[0] = type;
  if(type == 4) {
    uint16_t w = mapped(s->qw, -1, 1, 14), x = mapped(s->qx, -1, 1, 14);
    uint16_t y = mapped(s->qy, -1, 1, 14), z = mapped(s->qz, -1, 1, 14);
    data[1] = w >> 6; data[2] = ((w & 63) << 2) | (x >> 12);
    data[3] = x >> 4; data[4] = ((x & 15) << 4) | (y >> 10);
    data[5] = y >> 2; data[6] = ((y & 3) << 6) | (z >> 8); data[7] = z;
    return 0;
  }
  if(type == 1) {
    float t = isfinite(s->temperature) ? s->temperature : 0;
    data[1] = t < 0 ? 0 : t > 255 ? 255 : (uint8_t)(t + .5f);
    v[0] = s->ax_ms2; v[1] = s->ay_ms2; v[2] = s->az_ms2; limit = 235.2f;
  } else if(type == 2) {
    v[0] = s->gx_rad; v[1] = s->gy_rad; v[2] = s->gz_rad; limit = 34.88f;
  } else {
    v[0] = s->pitch; v[1] = s->yaw; v[2] = s->roll; limit = 180;
  }
  for(unsigned i = 0; i < 3; ++i) {
    float range = type == 3 && i == 0 ? 90 : limit;
    uint16_t u = mapped(v[i], -range, range, 16);
    data[2 + 2*i] = u; data[3 + 2*i] = u >> 8;
  }
  return 0;
}
static void put32(uint8_t *p, uint32_t v)
{
  for(unsigned i = 0; i < 4; ++i) p[i] = v >> (8*i);
}
int can_damiao_request(uint16_t id, uint8_t dlc, const uint8_t d[8],
                       const can_config_t *c, const can_sample_t *s,
                       uint16_t *reply_id, uint8_t reply[8], can_config_t *next, uint8_t *actions)
{
  *actions = 0; *next = *c; *reply_id = c->master_id;
  /* Appendix's fast request uses MST_ID and an explicitly addressed node.
   * Do NOT let another node's command, RTR, or a broadcast reboot affect us. */
  if(dlc == 4 && id == c->master_id && d[3] == 0xCC &&
     (uint16_t)(d[0] | ((uint16_t)d[1] << 8)) == c->node_id)
    return can_damiao_pack(d[2], s, reply) == 0;
  if(id != c->node_id || dlc != 8 || d[0] != 0xCC || d[3] != 0xDD) return 0;
  uint8_t rid = d[1], rw = d[2];
  memset(reply, 0, 8); reply[0] = 0xCC; reply[1] = rid; reply[2] = 0xDD;
  if(rw > 1) { reply[3] = 2; return 1; }
  if(rid >= 1 && rid <= 4) {
    if(rw) reply[3] = 2;
    else can_damiao_pack(rid, s, reply);
    return 1;
  }
  uint32_t value = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
  if(rid == 0 || rid == 5 || rid == 0xFE) {
    if(!rw) reply[3] = 2;
    else *actions = rid == 0 ? CAN_ACTION_REBOOT : rid == 5 ? CAN_ACTION_ZERO : CAN_ACTION_SAVE;
    return 1;
  }
  if(rid == 6 || rid == 7 || rid == 8 || rid == 0xFF) {
    reply[3] = rw ? 3 : 2; /* Unsupported calibration/factory erase never fakes success. */
    return 1;
  }
  switch(rid) {
    case 9: if(rw && value != 2) reply[3] = 2; value = 2; break; /* independent CAN transport */
    case 0x0A: if(rw) { if(value < 1 || value > 10000) reply[3] = 2; else next->period_ms = value; } value = next->period_ms; break;
    case 0x0B: if(rw) { if(value > 1) reply[3] = 2; else next->active = value; } value = next->active; break;
    case 0x0C: if(rw) { if(value > 7) reply[3] = 2; else next->baud_index = value; } value = next->baud_index; break;
    case 0x0D: if(rw) { if(value > 0x7FF) reply[3] = 2; else next->node_id = value; } value = next->node_id; break;
    case 0x0E: if(rw) { if(value > 0x7FF) reply[3] = 2; else next->master_id = value; } value = next->master_id; break;
    case 0x0F: if(rw) { if(value > 15) reply[3] = 2; else next->output_mask = value; } value = next->output_mask; break;
    default: reply[3] = 1; return 1;
  }
  if(!can_config_valid(next)) reply[3] = 2;
  if(reply[3]) *next = *c;
  else if(rw && rid != 9) *actions = CAN_ACTION_CONFIG;
  put32(reply + 4, value);
  return 1;
}
