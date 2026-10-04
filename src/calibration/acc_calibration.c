#include "acc_calibration.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define ACC_CAL_MAGIC 0x41434332U
#define ACC_CAL_COMMIT 0x41434F4BU
#define ACC_CAL_ADDR APP_ACC_CAL_FLASH_ADDR
#define ACC_CAL_SECTOR_BYTES 2048U

/* Version 1 is the existing 36-byte record at the sector start. */
typedef struct {
  uint32_t magic, version;
  float bias_g[3], scale[3];
  uint32_t crc;
} acc_cal_legacy_t;

/* Append-only slots retain the previous calibration through interrupted
 * writes. Commit is programmed last. Full returns -4; no sector is erased. */
typedef struct {
  uint32_t magic, version, sequence;
  float bias_g[3], scale[3];
  uint32_t reserved[5], crc, commit;
} acc_cal_record_t;
_Static_assert(sizeof(acc_cal_record_t) == 64U, "acc calibration journal slot");

acc_calibration_t acc_calibration_active = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0U};

#if APP_ACC_CAL_ENABLE
static uint32_t crc32(const void *data, uint32_t len)
{
  const uint8_t *p = (const uint8_t *)data; uint32_t c = 0xFFFFFFFFU, i, b;
  /* Preserve the existing record's uncomplemented CRC32 convention. */
  for(i=0U; i<len; i++) { c^=p[i]; for(b=0U; b<8U; b++) c=(c>>1)^(0xEDB88320U & (0U-(c&1U))); }
  return c;
}

static int parameters_valid(const float bias[3], const float scale[3])
{
  unsigned i;
  for(i=0; i<3; i++) if(!isfinite(bias[i]) || !isfinite(scale[i]) ||
     fabsf(bias[i]) > APP_ACC_CAL_MAX_BIAS_G || scale[i] <= 0.5f || scale[i] >= 1.5f) return 0;
  return 1;
}

static int record_valid(const acc_cal_record_t *r)
{
  return r->magic == ACC_CAL_MAGIC && r->version == 2U && r->commit == ACC_CAL_COMMIT &&
    parameters_valid(r->bias_g, r->scale) && r->crc == crc32(r, offsetof(acc_cal_record_t, crc));
}

static const acc_cal_record_t *latest_record(void)
{
  const acc_cal_record_t *best = 0; uint32_t offset;
  for(offset=0; offset<ACC_CAL_SECTOR_BYTES; offset+=sizeof(acc_cal_record_t)) {
    const acc_cal_record_t *r = (const acc_cal_record_t *)(uintptr_t)(ACC_CAL_ADDR+offset);
    if(record_valid(r) && (!best || (int32_t)(r->sequence-best->sequence)>0)) best = r;
  }
  return best;
}
#endif

void acc_calibration_load(void)
{
  unsigned i;
  memset(&acc_calibration_active, 0, sizeof(acc_calibration_active));
  for(i=0; i<3; i++) acc_calibration_active.scale[i] = 1.0f;
#if APP_ACC_CAL_ENABLE
  const acc_cal_record_t *r = latest_record();
  const acc_cal_legacy_t *old = (const acc_cal_legacy_t *)(uintptr_t)ACC_CAL_ADDR;
  if(r) {
    memcpy(acc_calibration_active.bias_g, r->bias_g, sizeof(r->bias_g));
    memcpy(acc_calibration_active.scale, r->scale, sizeof(r->scale));
    acc_calibration_active.valid = 1U;
  } else if(old->magic == ACC_CAL_MAGIC && old->version == 1U &&
            parameters_valid(old->bias_g, old->scale) && old->crc == crc32(old, offsetof(acc_cal_legacy_t, crc))) {
    memcpy(acc_calibration_active.bias_g, old->bias_g, sizeof(old->bias_g));
    memcpy(acc_calibration_active.scale, old->scale, sizeof(old->scale));
    acc_calibration_active.valid = 1U;
  }
#endif
}

int acc_calibration_save(const acc_calibration_t *cal)
{
#if !APP_ACC_CAL_ENABLE
  (void)cal; return -3;
#else
  acc_cal_record_t r; const acc_cal_record_t *latest;
  flash_status_type st = FLASH_OPERATE_DONE;
  uint32_t offset, i, word, target = 0U;
  if(!cal || !cal->valid || !parameters_valid(cal->bias_g, cal->scale)) return -1;
  if(acc_calibration_active.valid &&
     !memcmp(cal->bias_g, acc_calibration_active.bias_g, sizeof(cal->bias_g)) &&
     !memcmp(cal->scale, acc_calibration_active.scale, sizeof(cal->scale))) return 0;
  for(offset=0; offset<ACC_CAL_SECTOR_BYTES; offset+=sizeof(r)) {
    const uint32_t *slot = (const uint32_t *)(uintptr_t)(ACC_CAL_ADDR+offset);
    for(i=0; i<sizeof(r)/4U && slot[i] == 0xFFFFFFFFU; i++) {}
    if(i == sizeof(r)/4U) { target = ACC_CAL_ADDR+offset; break; }
  }
  if(!target) return -4;
  latest = latest_record(); memset(&r, 0, sizeof(r));
  r.magic = ACC_CAL_MAGIC; r.version = 2U; r.sequence = latest ? latest->sequence+1U : 1U;
  memcpy(r.bias_g, cal->bias_g, sizeof(r.bias_g)); memcpy(r.scale, cal->scale, sizeof(r.scale));
  r.crc = crc32(&r, offsetof(acc_cal_record_t, crc)); r.commit = ACC_CAL_COMMIT;
  flash_unlock();
  for(i=0; i<sizeof(r)/4U; i++) {
    memcpy(&word, (const uint8_t *)&r+4U*i, 4U);
    st = flash_word_program(target+4U*i, word); if(st != FLASH_OPERATE_DONE) break;
  }
  flash_lock();
  if(st != FLASH_OPERATE_DONE || !record_valid((const acc_cal_record_t *)(uintptr_t)target) ||
     memcmp((const void *)(uintptr_t)target, &r, sizeof(r))) return -2;
  acc_calibration_active = *cal; return 0;
#endif
}
