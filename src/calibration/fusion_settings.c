#include "fusion_settings.h"
#include "app_config.h"
#include "gyro_range.h"
#include "fusion_profile.h"
#include "at32f423_flash.h"
#include <string.h>

#define SETTINGS_MAGIC 0x4655534EU
#define SETTINGS_VERSION 7U
#define SETTINGS_SECTOR_SIZE 0x800U
#define SETTINGS_SLOT0 (APP_FUSION_SETTINGS_ADDR - SETTINGS_SECTOR_SIZE)
#define SETTINGS_SLOT1 APP_FUSION_SETTINGS_ADDR
#if (SETTINGS_SLOT0 != 0x0803D000U) || (SETTINGS_SLOT1 != 0x0803D800U) || \
    ((SETTINGS_SLOT0 % SETTINGS_SECTOR_SIZE) != 0U) || ((SETTINGS_SLOT1 % SETTINGS_SECTOR_SIZE) != 0U)
#error "Settings slots must be independent reserved 2-KiB Flash sectors"
#endif

typedef struct {
  uint32_t magic, version, sequence;
  uint8_t mode, fast_start;
  uint16_t can_node_id;
  output_config_t outputs[2];
  can_config_t can;
  uint16_t gyro_init_ms;
  uint16_t gyro_range_dps, output_hz;
  uint32_t filter_profile;
  uint32_t crc;
} settings_record_t;

typedef struct {
  uint32_t magic, version, sequence;
  uint8_t mode, fast_start;
  uint16_t can_node_id;
  output_config_t outputs[2];
  can_config_t can;
  uint16_t gyro_init_ms, gyro_range_dps, output_hz;
  uint32_t crc;
} v6_record_t;
typedef struct {
  uint32_t magic, version, sequence;
  uint8_t mode, fast_start;
  uint16_t can_node_id;
  output_config_t outputs[2];
  can_config_t can;
  uint16_t gyro_init_ms;
  uint32_t crc;
} v5_record_t;
typedef struct {
  uint32_t magic, version, sequence;
  uint8_t mode, fast_start;
  uint16_t can_node_id;
  output_config_t outputs[2];
  uint32_t crc;
} v3_record_t;
typedef struct {
  uint32_t magic, version, sequence;
  uint8_t mode, reserved;
  uint16_t can_node_id;
  uint32_t crc;
} legacy_record_t;
_Static_assert(sizeof(settings_record_t) == 48U, "Settings wire layout changed");
_Static_assert(sizeof(v6_record_t) == 44U, "V6 migration layout changed");
_Static_assert(sizeof(v5_record_t) == 40U, "V4/V5 migration layout changed");
_Static_assert(sizeof(v3_record_t) == 28U, "V3 migration layout changed");

static uint32_t crc32(const void *data, uint32_t length)
{
  const uint8_t *p = data;
  uint32_t c = 0xFFFFFFFFU, i, b;
  for(i = 0U; i < length; ++i) {
    c ^= p[i];
    for(b = 0U; b < 8U; ++b) c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
  }
  return c;
}
static int valid(const settings_record_t *r)
{
  if(r->magic != SETTINGS_MAGIC || r->mode > FUSION_MODE_9AXIS_RELATIVE || r->can_node_id > 0x7FFU) return 0;
  if(r->version == 2U) {
    const legacy_record_t *old = (const legacy_record_t *)r;
    return old->crc == crc32(old, sizeof(*old) - 4U);
  }
  if(r->fast_start > 1U || !protocol_output_config_valid(&r->outputs[0]) || !protocol_output_config_valid(&r->outputs[1])) return 0;
  if(r->version == 3U) {
    const v3_record_t *old = (const v3_record_t *)r;
    return old->crc == crc32(old, sizeof(*old) - 4U);
  }
  if(!can_config_valid(&r->can) || r->can.node_id != r->can_node_id) return 0;
  if(r->version == 4U || r->version == 5U) {
    const v5_record_t *old = (const v5_record_t *)r;
    return (r->version == 4U || (r->gyro_init_ms >= APP_GYR_INIT_MIN_MS && r->gyro_init_ms <= APP_GYR_INIT_MAX_MS)) &&
           old->crc == crc32(old, sizeof(*old) - 4U);
  }
  if(r->version != 6U && r->version != SETTINGS_VERSION) return 0;
  if(!gyro_range_valid(r->gyro_range_dps) || !r->output_hz || r->output_hz > APP_FUSION_HZ ||
     APP_FUSION_HZ % r->output_hz || r->gyro_init_ms < APP_GYR_INIT_MIN_MS ||
     r->gyro_init_ms > APP_GYR_INIT_MAX_MS) return 0;
  if(r->version == 6U) {
    const v6_record_t *old = (const v6_record_t *)r;
    return old->crc == crc32(old, sizeof(*old)-4U);
  }
  return r->filter_profile < FUSION_PROFILE_COUNT && gyro_range_valid(r->gyro_range_dps) &&
         r->output_hz && r->output_hz <= APP_FUSION_HZ && APP_FUSION_HZ % r->output_hz == 0U &&
         r->gyro_init_ms >= APP_GYR_INIT_MIN_MS && r->gyro_init_ms <= APP_GYR_INIT_MAX_MS &&
         r->crc == crc32(r, sizeof(*r) - 4U);
}
static const settings_record_t *latest_record(uint32_t *address)
{
  const settings_record_t *a = (const settings_record_t *)SETTINGS_SLOT0;
  const settings_record_t *b = (const settings_record_t *)SETTINGS_SLOT1;
  int va = valid(a), vb = valid(b);
  if(!va && !vb) return NULL;
  if(va && (!vb || (int32_t)(a->sequence - b->sequence) > 0)) {
    if(address) *address = SETTINGS_SLOT0;
    return a;
  }
  if(address) *address = SETTINGS_SLOT1;
  return b;
}
void device_settings_defaults(device_settings_t *s)
{
  memset(s, 0, sizeof(*s));
  s->mode = FUSION_MODE_9AXIS;
  s->can_node_id = APP_CAN_DEFAULT_CAN_ID;
  s->fast_start = APP_GYR_FAST_START_ENABLE ? 1U : 0U;
  s->gyro_init_ms = APP_GYR_INIT_DEFAULT_MS;
  s->gyro_range_dps = GYRO_RANGE_DEFAULT_DPS;
  s->output_hz = APP_VOFA_OUTPUT_HZ;
  s->filter_profile = FUSION_PROFILE_DEFAULT;
  can_config_defaults(&s->can);
  for(unsigned i = 0; i < 2; ++i) {
    s->outputs[i].format = OUTPUT_FORMAT_LEGACY;
    s->outputs[i].legacy_mode = STREAM_MODE_VOFA_3CH;
    s->outputs[i].field_mask = AHRS_FIELDS_ATTITUDE;
  }
}
static void decode_record(const settings_record_t *r, device_settings_t *s)
{
  device_settings_defaults(s);
  s->mode = (fusion_mode_t)r->mode;
  s->can_node_id = r->can_node_id;
  s->can.node_id = r->can_node_id;
  if(r->version >= 3U) {
    s->fast_start = r->fast_start;
    memcpy(s->outputs, r->outputs, sizeof(s->outputs));
  }
  if(r->version >= 4U) s->can = r->can;
  if(r->version >= 5U) s->gyro_init_ms = r->gyro_init_ms;
  if(r->version >= 6U) { s->gyro_range_dps = r->gyro_range_dps; s->output_hz = r->output_hz; }
  if(r->version == SETTINGS_VERSION) s->filter_profile = (uint8_t)r->filter_profile;
}
int device_settings_load(device_settings_t *s)
{
  const settings_record_t *r = latest_record(NULL);
  if(!s) return -1;
  if(!r) { device_settings_defaults(s); return -1; }
  decode_record(r, s);
  return 0;
}
int device_settings_save(const device_settings_t *s)
{
  settings_record_t r;
  device_settings_t previous;
  const settings_record_t *old;
  flash_status_type st;
  uint32_t old_addr = SETTINGS_SLOT1, new_addr, i, word;
  if(!s || s->filter_profile >= FUSION_PROFILE_COUNT || (unsigned)s->mode > FUSION_MODE_9AXIS_RELATIVE || s->can_node_id > 0x7FFU || s->fast_start > 1U ||
     s->gyro_init_ms < APP_GYR_INIT_MIN_MS || s->gyro_init_ms > APP_GYR_INIT_MAX_MS ||
     !gyro_range_valid(s->gyro_range_dps) || !s->output_hz || s->output_hz > APP_FUSION_HZ || APP_FUSION_HZ % s->output_hz != 0U ||
     !protocol_output_config_valid(&s->outputs[0]) || !protocol_output_config_valid(&s->outputs[1]) ||
     !can_config_valid(&s->can) || s->can.node_id != s->can_node_id) return -1;
  old = latest_record(&old_addr);
  if(old) {
    decode_record(old, &previous);
    if(old->version == SETTINGS_VERSION && previous.mode == s->mode && previous.can_node_id == s->can_node_id && previous.fast_start == s->fast_start && previous.gyro_init_ms == s->gyro_init_ms &&
       previous.gyro_range_dps == s->gyro_range_dps && previous.output_hz == s->output_hz &&
       previous.filter_profile == s->filter_profile &&
       memcmp(previous.outputs, s->outputs, sizeof(s->outputs)) == 0 &&
       memcmp(&previous.can, &s->can, sizeof(s->can)) == 0) return 0;
  }
  new_addr = old_addr == SETTINGS_SLOT1 ? SETTINGS_SLOT0 : SETTINGS_SLOT1;
  memset(&r, 0, sizeof(r));
  r.magic = SETTINGS_MAGIC; r.version = SETTINGS_VERSION;
  r.sequence = old ? old->sequence + 1U : 1U;
  r.mode = (uint8_t)s->mode; r.fast_start = s->fast_start; r.can_node_id = s->can_node_id;
  memcpy(r.outputs, s->outputs, sizeof(r.outputs));
  r.can = s->can;
  r.gyro_init_ms = s->gyro_init_ms;
  r.gyro_range_dps = s->gyro_range_dps; r.output_hz = s->output_hz;
  r.filter_profile = s->filter_profile;
  r.crc = crc32(&r, sizeof(r) - 4U);
  flash_unlock();
  st = flash_sector_erase(new_addr);
  if(st == FLASH_OPERATE_DONE) {
    for(i = 0; i < sizeof(r) / 4U; ++i) {
      memcpy(&word, (const uint8_t *)&r + 4U * i, 4U);
      st = flash_word_program(new_addr + 4U * i, word);
      if(st != FLASH_OPERATE_DONE) break;
    }
  }
  flash_lock();
  if(st != FLASH_OPERATE_DONE) return -2;
  return valid((const settings_record_t *)(uintptr_t)new_addr) ? 0 : -3;
}
int fusion_settings_load_ex(fusion_mode_t *mode, uint16_t *can_node_id)
{
  device_settings_t s;
  int result = device_settings_load(&s);
  if(mode) *mode = s.mode;
  if(can_node_id) *can_node_id = s.can_node_id;
  return result;
}
int fusion_settings_save_ex(fusion_mode_t mode, uint16_t can_node_id)
{
  device_settings_t s;
  (void)device_settings_load(&s);
  s.mode = mode; s.can_node_id = can_node_id; s.can.node_id = can_node_id;
  return device_settings_save(&s);
}
int fusion_settings_load(fusion_mode_t *mode) { return fusion_settings_load_ex(mode, NULL); }
int fusion_settings_save(fusion_mode_t mode)
{
  device_settings_t s;
  (void)device_settings_load(&s);
  s.mode = mode;
  return device_settings_save(&s);
}
