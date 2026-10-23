#include "fusion_settings.h"
#include "gyro_prefilter.h"
#include "app_config.h"
#include "gyro_range.h"
#include "acc_range.h"
#include "fusion_profile.h"
#include "at32f423_flash.h"
#include <string.h>

#define SETTINGS_MAGIC 0x4655534EU
#define SETTINGS_VERSION 8U
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
  float zaru_enter_dps, zaru_exit_dps, zaru_acc_dev_ms2;
  uint16_t zaru_enter_filter_ms, zaru_enter_confirm_ms, zaru_exit_confirm_ms, zaru_reserved;
  uint32_t crc;
} settings_record_t;

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
} v7_record_t;

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
_Static_assert(sizeof(settings_record_t) == 68U, "Settings wire layout changed");
/* Keep the v8 record byte-for-byte compatible with installed older BLs.
 * This optional extension is in the same journal sector and commits before
 * the main CRC, so a failed write cannot select a half-written new setting. */
#define WS_SETTINGS_MAGIC 0x57534C45U
typedef struct { uint32_t magic, sequence, enabled, crc; } ws_settings_t;
#define MAG_SETTINGS_MAGIC 0x4D43414CU
typedef struct { uint32_t magic, sequence; mag_cal_parameters_t parameters; uint32_t crc; } mag_settings_t;
_Static_assert(sizeof(mag_settings_t)==64U,"mag settings extension");
#define MAG_REJECT_MAGIC 0x4D52454AU
typedef struct { uint32_t magic, sequence, enabled, crc; } mag_reject_settings_t;
#define ALGORITHM_SETTINGS_MAGIC 0x414C474FU
typedef struct { uint32_t magic, sequence, algorithm, crc; } algorithm_settings_t;
_Static_assert(sizeof(settings_record_t)+sizeof(ws_settings_t)+sizeof(mag_settings_t)+sizeof(mag_reject_settings_t)==164U,"algorithm extension offset");
#define REST_BIAS_SETTINGS_MAGIC 0x52424941U
typedef struct { uint32_t magic, sequence, enabled, crc; } rest_bias_settings_t;
_Static_assert(164U+sizeof(algorithm_settings_t)==180U,"rest bias extension offset");
#define VQF_TUNING_MAGIC 0x56515431U
typedef struct { uint32_t magic, sequence, enabled; vqf_tuning_t parameters; uint32_t crc; } tuning_settings_t;
_Static_assert(sizeof(tuning_settings_t)==72U,"VQF tuning extension");
_Static_assert(180U+sizeof(rest_bias_settings_t)==196U,"VQF tuning extension offset");
#define GYRO_SCALE_MAGIC 0x47534331U
typedef struct { uint32_t magic, sequence; gyro_scale_t parameters; uint32_t crc; } scale_settings_t;
_Static_assert(sizeof(scale_settings_t)==24U,"gyro scale extension");
_Static_assert(196U+sizeof(tuning_settings_t)==268U,"gyro scale extension offset");
#define ACC_RANGE_MAGIC 0x41435231U
typedef struct { uint32_t magic, sequence, range_g, crc; } acc_range_settings_t;
_Static_assert(268U+sizeof(scale_settings_t)==292U,"acc range extension offset");
_Static_assert(292U+sizeof(acc_range_settings_t)==308U,"gyro prefilter extension offset");
#define GYRO_PREFILTER_MAGIC 0x47504631U
typedef struct { uint32_t magic, sequence, enabled; float cutoff_hz, q; uint32_t crc; } gyro_prefilter_settings_t;
_Static_assert(sizeof(gyro_prefilter_settings_t)==24U,"gyro prefilter extension");
_Static_assert(308U+sizeof(gyro_prefilter_settings_t)==332U,"ESKF tuning extension offset");
#define ESKF_TUNE_MAGIC 0x45534B31U
typedef struct { uint32_t magic, sequence, custom; eskf_tuning_t parameters; uint32_t crc; } eskf_tune_settings_t;
_Static_assert(sizeof(eskf_tune_settings_t)==72U,"ESKF tuning extension");
_Static_assert(332U+sizeof(eskf_tune_settings_t)<=SETTINGS_SECTOR_SIZE,"settings extensions fit journal");
#define REST_INDICATOR_MAGIC 0x52494E31U
typedef struct { uint32_t magic, sequence, enabled, crc; } rest_indicator_settings_t;
_Static_assert(332U+sizeof(eskf_tune_settings_t)==404U,"rest indicator extension offset");
_Static_assert(404U+sizeof(rest_indicator_settings_t)<=SETTINGS_SECTOR_SIZE,"rest indicator fits journal");
#define MAG_VQF_GATE_MAGIC 0x4D564731U
typedef struct { uint32_t magic, sequence, enabled, crc; } mag_vqf_gate_settings_t;
_Static_assert(404U+sizeof(rest_indicator_settings_t)==420U,"magnetometer gate extension offset");
_Static_assert(420U+sizeof(mag_vqf_gate_settings_t)==436U,"magnetometer health extension offset");
/* Magnitude the loaded calibration was fitted to. Stored next to the
 * coefficients so a unit whose hard-iron offset moved can be told apart from
 * one that is merely looking at a different part of the sky. Zero = unknown. */
#define MAG_HEALTH_MAGIC 0x4D484C31U
typedef struct { uint32_t magic, sequence; float expected_ut; uint32_t crc; } mag_health_settings_t;
_Static_assert(sizeof(mag_health_settings_t)==16U,"magnetometer health extension");
_Static_assert(436U+sizeof(mag_health_settings_t)<=SETTINGS_SECTOR_SIZE,"magnetometer health fits journal");
static void load_scale(const settings_record_t *r, device_settings_t *s);
static void load_tuning(const settings_record_t *r, device_settings_t *s);
static uint8_t load_ws_enabled(const settings_record_t *r);
_Static_assert(sizeof(v7_record_t) == 48U, "V7 migration layout changed");
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
static uint8_t load_ws_enabled(const settings_record_t *r)
{
  const ws_settings_t *w=(const ws_settings_t *)((const uint8_t *)r+sizeof(*r));
  if(w->magic!=WS_SETTINGS_MAGIC || w->sequence!=r->sequence || w->enabled>1U ||
     w->crc!=crc32(w,sizeof(*w)-4U)) return 1U;
  return (uint8_t)w->enabled;
}
static float load_mag_health(const settings_record_t *r)
{
  const mag_health_settings_t *x=(const mag_health_settings_t *)((const uint8_t *)r+436U);
  if(x->magic!=MAG_HEALTH_MAGIC || x->sequence!=r->sequence ||
     !(x->expected_ut>=20.f && x->expected_ut<=80.f) || x->crc!=crc32(x,sizeof(*x)-4U)) return 0.f;
  return x->expected_ut;
}
static uint8_t load_mag_vqf_gate(const settings_record_t *r)
{
  const mag_vqf_gate_settings_t *x=(const mag_vqf_gate_settings_t *)((const uint8_t *)r+420U);
  if(x->magic!=MAG_VQF_GATE_MAGIC || x->sequence!=r->sequence || x->enabled>1U ||
     x->crc!=crc32(x,sizeof(*x)-4U)) return 1U;
  return (uint8_t)x->enabled;
}
static uint8_t load_rest_indicator(const settings_record_t *r)
{
  const rest_indicator_settings_t *x=(const rest_indicator_settings_t *)((const uint8_t *)r+404U);
  if(x->magic!=REST_INDICATOR_MAGIC || x->sequence!=r->sequence || x->enabled>1U ||
     x->crc!=crc32(x,sizeof(*x)-4U)) return 1U;
  return (uint8_t)x->enabled;
}
static void load_mag(const settings_record_t *r, mag_cal_parameters_t *p)
{
  const mag_settings_t *m=(const mag_settings_t *)((const uint8_t *)r+sizeof(*r)+sizeof(ws_settings_t));
  mag_cal_defaults(p);
  if(m->magic==MAG_SETTINGS_MAGIC && m->sequence==r->sequence &&
     m->crc==crc32(m,sizeof(*m)-4U) && mag_cal_valid(&m->parameters)) *p=m->parameters;
}
static uint8_t load_mag_rejection(const settings_record_t *r)
{
  const mag_reject_settings_t *m=(const mag_reject_settings_t *)((const uint8_t *)r+sizeof(*r)+sizeof(ws_settings_t)+sizeof(mag_settings_t));
  if(m->magic!=MAG_REJECT_MAGIC || m->sequence!=r->sequence || m->enabled>1U || m->crc!=crc32(m,sizeof(*m)-4U)) return 1U;
  return (uint8_t)m->enabled;
}
static uint8_t load_algorithm(const settings_record_t *r)
{
  const algorithm_settings_t *a=(const algorithm_settings_t *)((const uint8_t *)r+164U);
  if(a->magic!=ALGORITHM_SETTINGS_MAGIC || a->sequence!=r->sequence || a->algorithm>=FUSION_ALGORITHM_COUNT ||
     a->crc!=crc32(a,sizeof(*a)-4U)) return FUSION_ALGORITHM_VQF;
  return (uint8_t)a->algorithm;
}
static uint8_t load_rest_bias(const settings_record_t *r)
{
  const rest_bias_settings_t *b=(const rest_bias_settings_t *)((const uint8_t *)r+180U);
  if(b->magic!=REST_BIAS_SETTINGS_MAGIC || b->sequence!=r->sequence || b->enabled>1U ||
     b->crc!=crc32(b,sizeof(*b)-4U)) return APP_VQF_REST_BIAS_ENABLE ? 1U : 0U;
  return (uint8_t)b->enabled;
}
static void load_tuning(const settings_record_t *r, device_settings_t *s)
{
  const tuning_settings_t *t=(const tuning_settings_t *)((const uint8_t *)r+196U);
  s->vqf_tuning_enabled=0U;
  vqf_tuning_defaults(&s->vqf_tuning);
  if(t->magic==VQF_TUNING_MAGIC && t->sequence==r->sequence && t->enabled<=1U &&
     t->crc==crc32(t,sizeof(*t)-4U) && vqf_tuning_valid(&t->parameters)) {
    s->vqf_tuning_enabled=(uint8_t)t->enabled; s->vqf_tuning=t->parameters;
  }
}
static void load_scale(const settings_record_t *r, device_settings_t *s)
{
  const scale_settings_t *t=(const scale_settings_t *)((const uint8_t *)r+268U);
  gyro_scale_defaults(&s->gyro_scale);
  if(t->magic==GYRO_SCALE_MAGIC && t->sequence==r->sequence &&
     t->crc==crc32(t,sizeof(*t)-4U) && gyro_scale_valid(&t->parameters)) s->gyro_scale=t->parameters;
}
static void load_prefilter(const settings_record_t *r, device_settings_t *s)
{
  const gyro_prefilter_settings_t *p=(const gyro_prefilter_settings_t *)((const uint8_t *)r+308U);
  if(p->magic!=GYRO_PREFILTER_MAGIC || p->sequence!=r->sequence || p->enabled>1U ||
     !gyro_prefilter_params_ok(1, p->cutoff_hz, p->q) || p->crc!=crc32(p,sizeof(*p)-4U)) return;
  s->gyro_prefilter_enabled=(uint8_t)p->enabled;
  s->gyro_prefilter_hz=p->cutoff_hz;
  s->gyro_prefilter_q=p->q;
}
static void load_eskf_tuning(const settings_record_t *r, device_settings_t *s)
{
  const eskf_tune_settings_t *t=(const eskf_tune_settings_t *)((const uint8_t *)r+332U);
  if(t->magic!=ESKF_TUNE_MAGIC || t->sequence!=r->sequence || t->custom>1U ||
     !eskf_tuning_valid(&t->parameters) || t->crc!=crc32(t,sizeof(*t)-4U)) return;
  s->eskf_tuning_custom=(uint8_t)t->custom;
  s->eskf_tuning=t->parameters;
  if(!s->eskf_tuning_custom || eskf_tuning_is_default(&s->eskf_tuning)) {
    eskf_tuning_defaults(&s->eskf_tuning);
    s->eskf_tuning_custom=0U;
  }
}
static uint8_t load_acc_range(const settings_record_t *r)
{
  const acc_range_settings_t *a=(const acc_range_settings_t *)((const uint8_t *)r+292U);
  if(a->magic!=ACC_RANGE_MAGIC || a->sequence!=r->sequence || a->range_g>UINT8_MAX ||
     !acc_range_valid((uint8_t)a->range_g) || a->crc!=crc32(a,sizeof(*a)-4U)) return ACC_RANGE_DEFAULT_G;
  return (uint8_t)a->range_g;
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
    return (r->version == 4U || r->gyro_init_ms <= APP_GYR_INIT_MAX_MS) &&
           old->crc == crc32(old, sizeof(*old) - 4U);
  }
  if(r->version != 6U && r->version != 7U && r->version != SETTINGS_VERSION) return 0;
  if(!gyro_range_valid(r->gyro_range_dps) || !r->output_hz || r->output_hz > APP_FUSION_HZ ||
     APP_FUSION_HZ % r->output_hz ||
     r->gyro_init_ms > APP_GYR_INIT_MAX_MS) return 0;
  if(r->version == 6U) {
    const v6_record_t *old = (const v6_record_t *)r;
    return old->crc == crc32(old, sizeof(*old)-4U);
  }
  if(r->filter_profile >= FUSION_PROFILE_COUNT) return 0;
  if(r->version == 7U) {
    const v7_record_t *old = (const v7_record_t *)r;
    return old->crc == crc32(old, sizeof(*old) - 4U);
  }
  {
    zaru_limits_t zaru = {r->zaru_enter_dps, r->zaru_exit_dps, r->zaru_acc_dev_ms2,
      r->zaru_enter_filter_ms, r->zaru_enter_confirm_ms, r->zaru_exit_confirm_ms,
      r->zaru_reserved};
    return zaru_limits_valid(&zaru) && r->crc == crc32(r, sizeof(*r) - 4U);
  }
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
  s->ws2812_enabled = 1U;
  s->mag_rejection_enabled = 1U;
  s->fusion_algorithm = FUSION_ALGORITHM_VQF;
  s->rest_bias_enabled = APP_VQF_REST_BIAS_ENABLE ? 1U : 0U;
  mag_cal_defaults(&s->mag_cal);
  vqf_tuning_defaults(&s->vqf_tuning);
  gyro_scale_defaults(&s->gyro_scale);
  s->gyro_init_ms = APP_GYR_INIT_DEFAULT_MS;
  s->gyro_range_dps = GYRO_RANGE_DEFAULT_DPS;
  s->acc_range_g = ACC_RANGE_DEFAULT_G;
  s->gyro_prefilter_enabled = 0U;
  s->gyro_prefilter_hz = GYRO_PREFILTER_DEFAULT_HZ;
  s->gyro_prefilter_q = GYRO_PREFILTER_DEFAULT_Q;
  s->eskf_tuning_custom = 0U;
  eskf_tuning_defaults(&s->eskf_tuning);
  s->rest_indicator_enabled = 1U;
  s->mag_vqf_gate_enabled = 1U;
  s->mag_expected_ut = 0.f;
  s->output_hz = APP_VOFA_OUTPUT_HZ;
  s->filter_profile = FUSION_PROFILE_DEFAULT;
  zaru_limits_default(&s->zaru);
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
  if(r->version==SETTINGS_VERSION) { s->ws2812_enabled=load_ws_enabled(r); s->mag_rejection_enabled=load_mag_rejection(r); s->fusion_algorithm=load_algorithm(r); s->rest_bias_enabled=load_rest_bias(r); load_mag(r,&s->mag_cal); load_tuning(r,s); load_scale(r,s); s->acc_range_g=load_acc_range(r); load_prefilter(r,s); load_eskf_tuning(r,s); s->rest_indicator_enabled=load_rest_indicator(r); s->mag_vqf_gate_enabled=load_mag_vqf_gate(r); s->mag_expected_ut=load_mag_health(r); }
  if(r->version >= 3U) {
    s->fast_start = r->fast_start;
    memcpy(s->outputs, r->outputs, sizeof(s->outputs));
  }
  if(r->version >= 4U) s->can = r->can;
  if(r->version >= 5U) s->gyro_init_ms = r->gyro_init_ms;
  if(r->version >= 6U) { s->gyro_range_dps = r->gyro_range_dps; s->output_hz = r->output_hz; }
  if(r->version >= 7U) s->filter_profile = (uint8_t)r->filter_profile;
  if(r->version >= 8U) {
    s->zaru.enter_dps = r->zaru_enter_dps;
    s->zaru.exit_dps = r->zaru_exit_dps;
    s->zaru.acc_dev_ms2 = r->zaru_acc_dev_ms2;
    s->zaru.enter_filter_ms = r->zaru_enter_filter_ms;
    s->zaru.enter_confirm_ms = r->zaru_enter_confirm_ms;
    s->zaru.exit_confirm_ms = r->zaru_exit_confirm_ms;
    s->zaru.reserved = r->zaru_reserved;
  }
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
  ws_settings_t ws;
  mag_settings_t mag; mag_reject_settings_t rejection;
  algorithm_settings_t algorithm;
  rest_bias_settings_t rest_bias;
  tuning_settings_t tuning;
  scale_settings_t scale;
  acc_range_settings_t acc_range;
  gyro_prefilter_settings_t prefilter;
  eskf_tune_settings_t eskf;
  rest_indicator_settings_t rest_ind;
  mag_vqf_gate_settings_t mag_gate;
  mag_health_settings_t mag_health;
  device_settings_t previous;
  const settings_record_t *old;
  flash_status_type st;
  uint32_t old_addr = SETTINGS_SLOT1, new_addr, i, word;
  if(!s || !gyro_scale_valid(&s->gyro_scale) || !gyro_prefilter_params_ok(s->gyro_prefilter_enabled, s->gyro_prefilter_hz, s->gyro_prefilter_q) || s->eskf_tuning_custom>1U || !eskf_tuning_valid(&s->eskf_tuning) || s->vqf_tuning_enabled>1U || !vqf_tuning_valid(&s->vqf_tuning) || s->rest_bias_enabled>1U || s->fusion_algorithm>=FUSION_ALGORITHM_COUNT || !mag_cal_valid(&s->mag_cal) || s->mag_rejection_enabled > 1U || s->ws2812_enabled > 1U || s->rest_indicator_enabled > 1U || s->mag_vqf_gate_enabled > 1U ||
     !(s->mag_expected_ut == 0.f || (s->mag_expected_ut >= 20.f && s->mag_expected_ut <= 80.f)) ||
     s->filter_profile >= FUSION_PROFILE_COUNT || (unsigned)s->mode > FUSION_MODE_9AXIS_RELATIVE || s->can_node_id > 0x7FFU || s->fast_start > 1U ||
     s->gyro_init_ms > APP_GYR_INIT_MAX_MS ||
     !gyro_range_valid(s->gyro_range_dps) || !acc_range_valid(s->acc_range_g) || !s->output_hz || s->output_hz > APP_FUSION_HZ || APP_FUSION_HZ % s->output_hz != 0U ||
     !protocol_output_config_valid(&s->outputs[0]) || !protocol_output_config_valid(&s->outputs[1]) ||
     !can_config_valid(&s->can) || s->can.node_id != s->can_node_id ||
     !zaru_limits_valid(&s->zaru)) return -1;
  old = latest_record(&old_addr);
  if(old) {
    decode_record(old, &previous);
    if(old->version == SETTINGS_VERSION && previous.mode == s->mode && previous.can_node_id == s->can_node_id && previous.fast_start == s->fast_start && previous.gyro_init_ms == s->gyro_init_ms &&
       previous.gyro_range_dps == s->gyro_range_dps && previous.output_hz == s->output_hz &&
       previous.acc_range_g == s->acc_range_g &&
       previous.filter_profile == s->filter_profile &&
       previous.ws2812_enabled == s->ws2812_enabled &&
       previous.rest_indicator_enabled == s->rest_indicator_enabled &&
       previous.mag_vqf_gate_enabled == s->mag_vqf_gate_enabled &&
       previous.mag_expected_ut == s->mag_expected_ut &&
       previous.mag_rejection_enabled == s->mag_rejection_enabled &&
       previous.fusion_algorithm == s->fusion_algorithm &&
       previous.rest_bias_enabled == s->rest_bias_enabled &&
       memcmp(&previous.gyro_scale,&s->gyro_scale,sizeof(s->gyro_scale))==0 &&
       previous.gyro_prefilter_enabled == s->gyro_prefilter_enabled &&
       previous.gyro_prefilter_hz == s->gyro_prefilter_hz &&
       previous.gyro_prefilter_q == s->gyro_prefilter_q &&
       previous.eskf_tuning_custom == s->eskf_tuning_custom &&
       memcmp(&previous.eskf_tuning,&s->eskf_tuning,sizeof(s->eskf_tuning))==0 &&
       previous.vqf_tuning_enabled == s->vqf_tuning_enabled &&
       memcmp(&previous.vqf_tuning,&s->vqf_tuning,sizeof(s->vqf_tuning))==0 &&
       memcmp(&previous.mag_cal,&s->mag_cal,sizeof(s->mag_cal))==0 &&
       previous.zaru.enter_dps == s->zaru.enter_dps && previous.zaru.exit_dps == s->zaru.exit_dps &&
       previous.zaru.acc_dev_ms2 == s->zaru.acc_dev_ms2 &&
       previous.zaru.enter_filter_ms == s->zaru.enter_filter_ms &&
       previous.zaru.enter_confirm_ms == s->zaru.enter_confirm_ms &&
       previous.zaru.exit_confirm_ms == s->zaru.exit_confirm_ms &&
       previous.zaru.reserved == s->zaru.reserved &&
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
  r.zaru_enter_dps = s->zaru.enter_dps;
  r.zaru_exit_dps = s->zaru.exit_dps;
  r.zaru_acc_dev_ms2 = s->zaru.acc_dev_ms2;
  r.zaru_enter_filter_ms = s->zaru.enter_filter_ms;
  r.zaru_enter_confirm_ms = s->zaru.enter_confirm_ms;
  r.zaru_exit_confirm_ms = s->zaru.exit_confirm_ms;
  r.zaru_reserved = s->zaru.reserved;
  r.crc = crc32(&r, sizeof(r) - 4U);
  ws.magic=WS_SETTINGS_MAGIC; ws.sequence=r.sequence; ws.enabled=s->ws2812_enabled;
  ws.crc=crc32(&ws,sizeof(ws)-4U);
  mag.magic=MAG_SETTINGS_MAGIC; mag.sequence=r.sequence; mag.parameters=s->mag_cal;
  mag.crc=crc32(&mag,sizeof(mag)-4U);
  rejection.magic=MAG_REJECT_MAGIC; rejection.sequence=r.sequence; rejection.enabled=s->mag_rejection_enabled;
  rejection.crc=crc32(&rejection,sizeof(rejection)-4U);
  algorithm.magic=ALGORITHM_SETTINGS_MAGIC;algorithm.sequence=r.sequence;algorithm.algorithm=s->fusion_algorithm;
  algorithm.crc=crc32(&algorithm,sizeof(algorithm)-4U);
  rest_bias.magic=REST_BIAS_SETTINGS_MAGIC;rest_bias.sequence=r.sequence;rest_bias.enabled=s->rest_bias_enabled;
  rest_bias.crc=crc32(&rest_bias,sizeof(rest_bias)-4U);
  tuning.magic=VQF_TUNING_MAGIC;tuning.sequence=r.sequence;tuning.enabled=s->vqf_tuning_enabled;
  tuning.parameters=s->vqf_tuning;tuning.crc=crc32(&tuning,sizeof(tuning)-4U);
  scale.magic=GYRO_SCALE_MAGIC;scale.sequence=r.sequence;scale.parameters=s->gyro_scale;
  scale.crc=crc32(&scale,sizeof(scale)-4U);
  acc_range.magic=ACC_RANGE_MAGIC;acc_range.sequence=r.sequence;acc_range.range_g=s->acc_range_g;
  acc_range.crc=crc32(&acc_range,sizeof(acc_range)-4U);
  prefilter.magic=GYRO_PREFILTER_MAGIC;prefilter.sequence=r.sequence;prefilter.enabled=s->gyro_prefilter_enabled;
  prefilter.cutoff_hz=s->gyro_prefilter_hz;prefilter.q=s->gyro_prefilter_q;
  prefilter.crc=crc32(&prefilter,sizeof(prefilter)-4U);
  eskf.magic=ESKF_TUNE_MAGIC;eskf.sequence=r.sequence;eskf.custom=s->eskf_tuning_custom;
  eskf.parameters=s->eskf_tuning;eskf.crc=crc32(&eskf,sizeof(eskf)-4U);
  rest_ind.magic=REST_INDICATOR_MAGIC;rest_ind.sequence=r.sequence;rest_ind.enabled=s->rest_indicator_enabled;
  rest_ind.crc=crc32(&rest_ind,sizeof(rest_ind)-4U);
  mag_gate.magic=MAG_VQF_GATE_MAGIC;mag_gate.sequence=r.sequence;mag_gate.enabled=s->mag_vqf_gate_enabled;
  mag_gate.crc=crc32(&mag_gate,sizeof(mag_gate)-4U);
  mag_health.magic=MAG_HEALTH_MAGIC;mag_health.sequence=r.sequence;mag_health.expected_ut=s->mag_expected_ut;
  mag_health.crc=crc32(&mag_health,sizeof(mag_health)-4U);
  flash_unlock();
  st = flash_sector_erase(new_addr);
  if(st == FLASH_OPERATE_DONE) {
    for(i = 0; i < (sizeof(r)-4U) / 4U; ++i) {
      memcpy(&word, (const uint8_t *)&r + 4U * i, 4U);
      st = flash_word_program(new_addr + 4U * i, word);
      if(st != FLASH_OPERATE_DONE) break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0; i<sizeof(ws)/4U; ++i) {
      memcpy(&word,(const uint8_t *)&ws+4U*i,4U);
      st=flash_word_program(new_addr+sizeof(r)+4U*i,word);
      if(st!=FLASH_OPERATE_DONE) break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0; i<sizeof(mag)/4U; ++i) {
      memcpy(&word,(const uint8_t *)&mag+4U*i,4U);
      st=flash_word_program(new_addr+sizeof(r)+sizeof(ws)+4U*i,word);
      if(st!=FLASH_OPERATE_DONE) break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0; i<sizeof(rejection)/4U; ++i) {
      memcpy(&word,(const uint8_t *)&rejection+4U*i,4U);
      st=flash_word_program(new_addr+sizeof(r)+sizeof(ws)+sizeof(mag)+4U*i,word);
      if(st!=FLASH_OPERATE_DONE) break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(algorithm)/4U;++i) {
      memcpy(&word,(const uint8_t *)&algorithm+4U*i,4U);
      st=flash_word_program(new_addr+164U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(rest_bias)/4U;++i) {
      memcpy(&word,(const uint8_t *)&rest_bias+4U*i,4U);
      st=flash_word_program(new_addr+180U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(tuning)/4U;++i) {
      memcpy(&word,(const uint8_t *)&tuning+4U*i,4U);
      st=flash_word_program(new_addr+196U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(scale)/4U;++i) {
      memcpy(&word,(const uint8_t *)&scale+4U*i,4U);
      st=flash_word_program(new_addr+268U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(acc_range)/4U;++i) {
      memcpy(&word,(const uint8_t *)&acc_range+4U*i,4U);
      st=flash_word_program(new_addr+292U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(prefilter)/4U;++i) {
      memcpy(&word,(const uint8_t *)&prefilter+4U*i,4U);
      st=flash_word_program(new_addr+308U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(eskf)/4U;++i) {
      memcpy(&word,(const uint8_t *)&eskf+4U*i,4U);
      st=flash_word_program(new_addr+332U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(rest_ind)/4U;++i) {
      memcpy(&word,(const uint8_t *)&rest_ind+4U*i,4U);
      st=flash_word_program(new_addr+404U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(mag_gate)/4U;++i) {
      memcpy(&word,(const uint8_t *)&mag_gate+4U*i,4U);
      st=flash_word_program(new_addr+420U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) for(i=0;i<sizeof(mag_health)/4U;++i) {
      memcpy(&word,(const uint8_t *)&mag_health+4U*i,4U);
      st=flash_word_program(new_addr+436U+4U*i,word);
      if(st!=FLASH_OPERATE_DONE)break;
    }
    if(st == FLASH_OPERATE_DONE) st=flash_word_program(new_addr+sizeof(r)-4U,r.crc);
  }
  flash_lock();
  if(st != FLASH_OPERATE_DONE) return -2;
  load_mag((const settings_record_t *)(uintptr_t)new_addr,&previous.mag_cal);
  load_tuning((const settings_record_t *)(uintptr_t)new_addr,&previous);
  load_scale((const settings_record_t *)(uintptr_t)new_addr,&previous);
  load_prefilter((const settings_record_t *)(uintptr_t)new_addr,&previous);
  load_eskf_tuning((const settings_record_t *)(uintptr_t)new_addr,&previous);
  return load_acc_range((const settings_record_t *)(uintptr_t)new_addr)==s->acc_range_g &&
      previous.eskf_tuning_custom==s->eskf_tuning_custom &&
      memcmp(&previous.eskf_tuning,&s->eskf_tuning,sizeof(s->eskf_tuning))==0 &&
      memcmp(&previous.gyro_scale,&s->gyro_scale,sizeof(s->gyro_scale))==0 &&
      previous.gyro_prefilter_enabled==s->gyro_prefilter_enabled &&
      previous.gyro_prefilter_hz==s->gyro_prefilter_hz &&
      previous.gyro_prefilter_q==s->gyro_prefilter_q &&
      previous.vqf_tuning_enabled==s->vqf_tuning_enabled &&
      memcmp(&previous.vqf_tuning,&s->vqf_tuning,sizeof(s->vqf_tuning))==0 &&
      memcmp(&previous.mag_cal,&s->mag_cal,sizeof(s->mag_cal))==0 &&
      valid((const settings_record_t *)(uintptr_t)new_addr) &&
      load_ws_enabled((const settings_record_t *)(uintptr_t)new_addr)==s->ws2812_enabled &&
      load_algorithm((const settings_record_t *)(uintptr_t)new_addr)==s->fusion_algorithm &&
      load_rest_bias((const settings_record_t *)(uintptr_t)new_addr)==s->rest_bias_enabled &&
      load_rest_indicator((const settings_record_t *)(uintptr_t)new_addr)==s->rest_indicator_enabled &&
      load_mag_vqf_gate((const settings_record_t *)(uintptr_t)new_addr)==s->mag_vqf_gate_enabled &&
      load_mag_health((const settings_record_t *)(uintptr_t)new_addr)==s->mag_expected_ut &&
      load_mag_rejection((const settings_record_t *)(uintptr_t)new_addr)==s->mag_rejection_enabled ? 0 : -3;
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
