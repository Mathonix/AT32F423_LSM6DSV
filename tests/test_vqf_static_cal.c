#include "vqf_static_cal.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include "test_memory.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SECTOR 0x800U
#define SLOT0 APP_VQF_STATIC_FLASH_SLOT0
#define SLOT1 APP_VQF_STATIC_FLASH_SLOT1
#define BIAS0 APP_GYR_BIAS_FLASH_SLOT0_ADDR
#define BIAS1 APP_GYR_BIAS_FLASH_SLOT1_ADDR

static int unlocked, fail_erase, fail_program_after = -1;
static uint32_t erase_count;
static vqf_static_record_t good_image;
static int have_good;

void flash_unlock(void) { unlocked = 1; }
void flash_lock(void) { unlocked = 0; }

flash_status_type flash_sector_erase(uint32_t address)
{
  assert(unlocked);
  assert(address == SLOT0 || address == SLOT1);
  ++erase_count;
  if(fail_erase) return FLASH_PROGRAM_ERROR;
  memset((void *)(uintptr_t)address, 0xFF, SECTOR);
  return FLASH_OPERATE_DONE;
}

flash_status_type flash_word_program(uint32_t address, uint32_t value)
{
  assert(unlocked && (address & 3U) == 0U);
  assert((address >= SLOT0 && address < SLOT0 + SECTOR) ||
         (address >= SLOT1 && address < SLOT1 + SECTOR));
  if(fail_program_after == 0) return FLASH_PROGRAM_ERROR;
  if(fail_program_after > 0) --fail_program_after;
  *(uint32_t *)(uintptr_t)address &= value;
  return FLASH_OPERATE_DONE;
}

static const float k_acc[3] = {0.0f, 0.0f, 9.80665f};
static const float k_dt = 0.0005f;

static uint32_t crc_bytes(const void *data, uint32_t n)
{
  const uint8_t *p = data;
  uint32_t c = 0xFFFFFFFFU, i, b;
  for(i = 0U; i < n; ++i) {
    c ^= p[i];
    for(b = 0U; b < 8U; ++b) c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
  }
  return c;
}

static void expect_defaults(const vqf_static_params_t *p)
{
  assert(p->gyro_bias_dps[0] == 0.0f && p->gyro_bias_dps[1] == 0.0f && p->gyro_bias_dps[2] == 0.0f);
  assert(p->bias_sigma_init_dps == APP_VQF_BIAS_SIGMA_INIT_DPS);
  assert(p->bias_sigma_rest_dps == APP_VQF_BIAS_SIGMA_REST_DPS);
  assert(p->rest_th_gyr_dps == APP_VQF_REST_GYR_DPS);
  assert(p->rest_th_acc_ms2 == APP_VQF_REST_ACC_MS2);
  assert(p->calibration_temp_c == 0.0f);
  assert(p->acc_mean_ms2[2] == APP_VQF_STATIC_GRAVITY_MS2);
}

typedef void (*sample_fn)(uint32_t ms, int half, float gyr[3], float acc[3], float *temp, void *user);

static void feed_window(sample_fn fn, void *user)
{
  uint32_t ms;
  assert(vqf_static_cal_start(0U) == 0);
  assert(vqf_static_cal_start(1U) == -1);
  for(ms = 0U; ms <= 65000U; ++ms) {
    int half;
    for(half = 0; half < 2; ++half) {
      float gyr[3], acc[3], temp;
      fn(ms, half, gyr, acc, &temp, user);
      vqf_static_cal_feed(ms, k_dt, gyr, acc, temp);
    }
  }
}

static int finish(vqf_static_cal_status_t *st, vqf_static_params_t *params)
{
  assert(vqf_static_cal_poll(st, params) == 1);
  assert(vqf_static_cal_poll(st, params) == 0);
  return st->error;
}

static void still_fn(uint32_t ms, int half, float gyr[3], float acc[3], float *temp, void *user)
{
  const float *bias = user;
  (void)ms; (void)half;
  gyr[0] = bias[0]; gyr[1] = bias[1]; gyr[2] = bias[2];
  memcpy(acc, k_acc, sizeof(k_acc));
  *temp = 25.0f;
}

static void square_fn(uint32_t ms, int half, float gyr[3], float acc[3], float *temp, void *user)
{
  const float *bias = user;
  float s = ((ms + (uint32_t)half) & 1U) ? 0.05f : -0.05f;
  (void)ms;
  gyr[0] = bias[0] + s;
  gyr[1] = bias[1] + s;
  gyr[2] = bias[2] + s;
  memcpy(acc, k_acc, sizeof(k_acc));
  *temp = 30.0f;
}

static void gyro_noise_fn(uint32_t ms, int half, float gyr[3], float acc[3], float *temp, void *user)
{
  float s = ((ms + (uint32_t)half) & 1U) ? 0.40f : -0.40f;
  (void)user;
  gyr[0] = s; gyr[1] = 0.0f; gyr[2] = 0.0f;
  memcpy(acc, k_acc, sizeof(k_acc));
  *temp = 25.0f;
}

static void acc_noise_fn(uint32_t ms, int half, float gyr[3], float acc[3], float *temp, void *user)
{
  float s = ((ms + (uint32_t)half) & 1U) ? 0.25f : -0.25f;
  (void)user;
  gyr[0] = gyr[1] = gyr[2] = 0.0f;
  acc[0] = 0.0f; acc[1] = 0.0f; acc[2] = 9.80665f + s;
  *temp = 25.0f;
}

static void temp_fn(uint32_t ms, int half, float gyr[3], float acc[3], float *temp, void *user)
{
  (void)half; (void)user;
  gyr[0] = gyr[1] = gyr[2] = 0.0f;
  memcpy(acc, k_acc, sizeof(k_acc));
  if(ms < 5000U) *temp = 20.0f;
  else {
    float elapsed = (float)(ms - 5000U);
    if(elapsed > 60000.0f) elapsed = 60000.0f;
    *temp = 20.0f + 3.0f * elapsed / 60000.0f;
  }
}

static void drift_fn(uint32_t ms, int half, float gyr[3], float acc[3], float *temp, void *user)
{
  uint32_t elapsed = ms > 5000U ? ms - 5000U : 0U;
  uint32_t block = elapsed / 10000U;
  (void)half; (void)user;
  if(block > 5U) block = 5U;
  gyr[0] = 0.02f * (float)block;
  gyr[1] = 0.0f;
  gyr[2] = 0.0f;
  memcpy(acc, k_acc, sizeof(k_acc));
  *temp = 25.0f;
}

static void snapshot_good(void)
{
  if(!have_good && *(const uint32_t *)(uintptr_t)SLOT0 == VQF_STATIC_CAL_MAGIC) {
    memcpy(&good_image, (const void *)(uintptr_t)SLOT0, sizeof(good_image));
    have_good = 1;
  }
}

static void test_quiet_pass(void)
{
  const float bias[3] = {0.20f, -0.10f, 0.05f};
  vqf_static_cal_status_t st;
  vqf_static_params_t params, loaded;
  vqf_static_record_t rec;
  uint32_t erases = erase_count;
  feed_window(still_fn, (void *)bias);
  assert(vqf_static_cal_active());
  assert(finish(&st, &params) == VQF_STATIC_CAL_OK);
  assert(st.state == VQF_STATIC_CAL_DONE);
  assert(st.sample_count >= APP_VQF_STATIC_CAL_MIN_SAMPLES);
  assert(erase_count == erases + 1U);
  assert(fabsf(params.gyro_bias_dps[0] - 0.20f) < 1.0e-4f);
  assert(fabsf(params.gyro_bias_dps[1] + 0.10f) < 1.0e-4f);
  assert(fabsf(params.gyro_bias_dps[2] - 0.05f) < 1.0e-4f);
  assert(params.bias_sigma_init_dps == APP_VQF_CAL_MIN_BIAS_SIGMA_INIT);
  assert(params.bias_sigma_rest_dps == APP_VQF_BIAS_SIGMA_REST_DPS);
  assert(params.rest_th_gyr_dps == APP_VQF_REST_GYR_DPS);
  assert(params.rest_th_acc_ms2 == APP_VQF_REST_ACC_MS2);
  assert(fabsf(params.calibration_temp_c - 25.0f) < 1.0e-3f);
  assert(vqf_static_cal_source() == VQF_STATIC_CAL_SOURCE_CAL);
  memcpy(&rec, (const void *)(uintptr_t)SLOT0, sizeof(rec));
  assert(rec.magic == VQF_STATIC_CAL_MAGIC && rec.valid == 1U && rec.version == 1U);
  assert(rec.size == sizeof(rec) && rec.sample_count == st.sample_count);
  assert(rec.gyro_dev_p99_dps < 0.02f);
  assert(rec.gyro_std_dps[0] < 1.0e-4f);
  assert(vqf_static_cal_load(&loaded) == 0);
  assert(fabsf(loaded.gyro_bias_dps[0] - 0.20f) < 1.0e-4f);
  snapshot_good();
  assert(*(const uint8_t *)(uintptr_t)BIAS0 == 0xA5);
  assert(*(const uint8_t *)(uintptr_t)BIAS1 == 0xA5);
}

static void test_sigma_band(void)
{
  const float bias[3] = {0.20f, 0.0f, -0.10f};
  vqf_static_cal_status_t st;
  vqf_static_params_t params;
  vqf_static_record_t rec;
  uint32_t erases = erase_count;
  feed_window(square_fn, (void *)bias);
  assert(finish(&st, &params) == VQF_STATIC_CAL_OK);
  assert(erase_count == erases + 1U);
  assert(fabsf(params.bias_sigma_rest_dps - 0.05f) < 2.0e-3f);
  assert(params.bias_sigma_init_dps == APP_VQF_CAL_MIN_BIAS_SIGMA_INIT);
  assert(params.rest_th_gyr_dps == APP_VQF_REST_GYR_DPS);
  assert(params.rest_th_acc_ms2 == APP_VQF_REST_ACC_MS2);
  assert(fabsf(params.gyro_bias_dps[0] - 0.20f) < 1.0e-3f);
  memcpy(&rec, (const void *)(uintptr_t)SLOT1, sizeof(rec));
  assert(rec.valid == 1U && rec.sequence == 2U);
  assert(rec.gyro_std_dps[0] > 0.049f && rec.gyro_std_dps[0] < 0.052f);
  assert(rec.gyro_dev_p99_dps > 0.08f && rec.gyro_dev_p99_dps < 0.12f);
}

static void expect_reject(sample_fn fn, uint8_t error)
{
  vqf_static_cal_status_t st;
  vqf_static_params_t loaded;
  uint32_t erases = erase_count;
  float kept;
  assert(vqf_static_cal_load(&loaded) == 0);
  kept = loaded.gyro_bias_dps[0];
  feed_window(fn, NULL);
  assert(finish(&st, NULL) == error);
  assert(st.state == VQF_STATIC_CAL_FAILED);
  assert(erase_count == erases);
  assert(vqf_static_cal_load(&loaded) == 0);
  assert(fabsf(loaded.gyro_bias_dps[0] - kept) < 1.0e-4f);
}

static void test_motion_and_short(void)
{
  vqf_static_cal_status_t st;
  vqf_static_params_t loaded;
  float gyr[3] = {0.0f, 0.0f, 0.0f};
  float moved[3] = {5.0f, 0.0f, 0.0f};
  uint32_t ms, erases;
  assert(vqf_static_cal_load(&loaded) == 0);
  erases = erase_count;
  assert(vqf_static_cal_start(1000U) == 0);
  for(ms = 1000U; ms <= 7000U; ++ms) {
    vqf_static_cal_feed(ms, k_dt, gyr, k_acc, 25.0f);
    vqf_static_cal_feed(ms, k_dt, gyr, k_acc, 25.0f);
  }
  vqf_static_cal_get_status(&st);
  assert(st.state == VQF_STATIC_CAL_COLLECTING);
  vqf_static_cal_feed(7001U, k_dt, moved, k_acc, 25.0f);
  assert(finish(&st, NULL) == VQF_STATIC_CAL_ERR_MOVED);
  assert(erase_count == erases);

  assert(vqf_static_cal_start(0U) == 0);
  for(ms = 0U; ms <= 5000U; ++ms) {
    vqf_static_cal_feed(ms, k_dt, gyr, k_acc, 25.0f);
    vqf_static_cal_feed(ms, k_dt, gyr, k_acc, 25.0f);
  }
  vqf_static_cal_feed(65000U, k_dt, gyr, k_acc, 25.0f);
  assert(finish(&st, NULL) == VQF_STATIC_CAL_ERR_SAMPLE_COUNT);
  assert(erase_count == erases);
  assert(vqf_static_cal_load(&loaded) == 0);
}

static void test_prepare_restart_and_cancel(void)
{
  vqf_static_cal_status_t st;
  float gyr[3] = {0.0f, 0.0f, 0.0f};
  float moved[3] = {4.0f, 0.0f, 0.0f};
  uint32_t ms, erases = erase_count;
  assert(vqf_static_cal_start(0U) == 0);
  for(ms = 0U; ms < 3000U; ++ms) vqf_static_cal_feed(ms, k_dt, moved, k_acc, 25.0f);
  vqf_static_cal_get_status(&st);
  assert(st.state == VQF_STATIC_CAL_PRECHECK && st.error == VQF_STATIC_CAL_OK);
  for(ms = 3000U; ms < 7000U; ++ms) vqf_static_cal_feed(ms, k_dt, gyr, k_acc, 25.0f);
  vqf_static_cal_get_status(&st);
  assert(st.state == VQF_STATIC_CAL_PRE_STABLE);
  vqf_static_cal_cancel();
  vqf_static_cal_get_status(&st);
  assert(st.state == VQF_STATIC_CAL_IDLE);
  assert(!vqf_static_cal_active());
  assert(erase_count == erases);
  assert(vqf_static_cal_source() == VQF_STATIC_CAL_SOURCE_CAL);
}

static void test_prepare_cap_and_nan(void)
{
  vqf_static_cal_status_t st;
  float bad[3] = {0.0f, NAN, 0.0f};
  uint32_t erases = erase_count;
  assert(vqf_static_cal_start(50U) == 0);
  vqf_static_cal_tick(50U + 119999U);
  assert(vqf_static_cal_active());
  vqf_static_cal_tick(50U + 120000U);
  assert(finish(&st, NULL) == VQF_STATIC_CAL_ERR_MOVED);
  assert(erase_count == erases);

  assert(vqf_static_cal_start(0U) == 0);
  vqf_static_cal_feed(0U, k_dt, bad, k_acc, 25.0f);
  assert(finish(&st, NULL) == VQF_STATIC_CAL_ERR_INVALID_NUMBER);
  assert(erase_count == erases);
}

static void test_flash_failure(sample_fn fn, int program_fail)
{
  vqf_static_cal_status_t st;
  vqf_static_params_t loaded;
  float kept;
  assert(vqf_static_cal_load(&loaded) == 0);
  kept = loaded.gyro_bias_dps[0];
  feed_window(fn, (void *)loaded.gyro_bias_dps);
  vqf_static_cal_get_status(&st);
  assert(st.state == VQF_STATIC_CAL_VALIDATING);
  if(program_fail) fail_program_after = 0;
  else fail_erase = 1;
  assert(finish(&st, NULL) == VQF_STATIC_CAL_ERR_FLASH_WRITE);
  fail_erase = 0;
  fail_program_after = -1;
  assert(vqf_static_cal_load(&loaded) == 0);
  assert(fabsf(loaded.gyro_bias_dps[0] - kept) < 1.0e-3f);
  assert(vqf_static_cal_source() == VQF_STATIC_CAL_SOURCE_CAL);
}

static void test_restore(void)
{
  vqf_static_params_t params, loaded;
  vqf_static_record_t newest;
  uint32_t addr;
  assert(vqf_static_cal_start(0U) == 0);
  assert(vqf_static_cal_restore_defaults() == -1);
  vqf_static_cal_cancel();
  assert(vqf_static_cal_restore_defaults() == 0);
  vqf_static_cal_get_applied(&params);
  expect_defaults(&params);
  assert(vqf_static_cal_source() == VQF_STATIC_CAL_SOURCE_DEFAULT);
  assert(vqf_static_cal_load(&loaded) == -1);
  expect_defaults(&loaded);
  addr = *(const uint32_t *)(uintptr_t)SLOT0 == VQF_STATIC_CAL_MAGIC &&
         ((const vqf_static_record_t *)(uintptr_t)SLOT0)->sequence >
         ((const vqf_static_record_t *)(uintptr_t)SLOT1)->sequence ? SLOT0 : SLOT1;
  memcpy(&newest, (const void *)(uintptr_t)addr, sizeof(newest));
  assert(newest.valid == 0U);
  assert(newest.crc32 == crc_bytes(&newest, sizeof(newest) - 4U));
  fail_erase = 1;
  assert(vqf_static_cal_restore_defaults() == -2);
  fail_erase = 0;
  vqf_static_cal_get_applied(&params);
  expect_defaults(&params);
}

static void put_record(uint32_t address, vqf_static_record_t *rec)
{
  rec->magic = VQF_STATIC_CAL_MAGIC;
  rec->version = rec->version ? rec->version : VQF_STATIC_CAL_VERSION;
  rec->size = rec->size ? rec->size : (uint16_t)sizeof(*rec);
  rec->crc32 = crc_bytes(rec, sizeof(*rec) - 4U);
  memcpy((void *)(uintptr_t)address, rec, sizeof(*rec));
}

static void test_corrupt_flash(void)
{
  vqf_static_params_t loaded;
  vqf_static_record_t rec;
  assert(have_good);
  memset((void *)(uintptr_t)SLOT0, 0xFF, SECTOR);
  memset((void *)(uintptr_t)SLOT1, 0xFF, SECTOR);
  assert(vqf_static_cal_load(&loaded) == -1);
  expect_defaults(&loaded);

  rec = good_image;
  rec.sequence = 1U;
  rec.crc32 ^= 0x00FFU;
  rec.version = VQF_STATIC_CAL_VERSION;
  rec.size = (uint16_t)sizeof(rec);
  memcpy((void *)(uintptr_t)SLOT0, &rec, sizeof(rec));
  assert(vqf_static_cal_load(&loaded) == -1);

  rec = good_image;
  rec.sequence = 2U;
  rec.version = 99U;
  put_record(SLOT0, &rec);
  assert(vqf_static_cal_load(&loaded) == -1);

  rec = good_image;
  rec.sequence = 3U;
  rec.version = VQF_STATIC_CAL_VERSION;
  rec.size = 80U;
  put_record(SLOT0, &rec);
  assert(vqf_static_cal_load(&loaded) == -1);

  rec = good_image;
  rec.sequence = 4U;
  rec.version = VQF_STATIC_CAL_VERSION;
  rec.size = (uint16_t)sizeof(rec);
  rec.gyro_bias_dps[0] = NAN;
  put_record(SLOT1, &rec);
  rec = good_image;
  rec.sequence = 1U;
  rec.version = VQF_STATIC_CAL_VERSION;
  rec.size = (uint16_t)sizeof(rec);
  put_record(SLOT0, &rec);
  assert(vqf_static_cal_load(&loaded) == -1);
  expect_defaults(&loaded);

  rec = good_image;
  rec.sequence = 8U;
  rec.version = VQF_STATIC_CAL_VERSION;
  rec.size = (uint16_t)sizeof(rec);
  rec.rest_th_gyr_dps = 5.0f;
  rec.gyro_bias_dps[0] = good_image.gyro_bias_dps[0];
  put_record(SLOT1, &rec);
  assert(vqf_static_cal_load(&loaded) == -1);

  rec = good_image;
  rec.sequence = 9U;
  rec.valid = 0U;
  rec.version = VQF_STATIC_CAL_VERSION;
  rec.size = (uint16_t)sizeof(rec);
  rec.gyro_bias_dps[0] = 1.5f;
  put_record(SLOT0, &rec);
  assert(vqf_static_cal_load(&loaded) == -1);
  expect_defaults(&loaded);

  rec = good_image;
  rec.sequence = 3U;
  rec.valid = 1U;
  rec.version = VQF_STATIC_CAL_VERSION;
  rec.size = (uint16_t)sizeof(rec);
  memset((void *)(uintptr_t)SLOT0, 0xFF, SECTOR);
  memset((void *)(uintptr_t)SLOT1, 0xFF, SECTOR);
  put_record(SLOT1, &rec);
  assert(vqf_static_cal_load(&loaded) == 0);
  assert(fabsf(loaded.gyro_bias_dps[0] - good_image.gyro_bias_dps[0]) < 1.0e-4f);
}

int main(void)
{
  vqf_static_params_t defaults;
  const float quiet_bias[3] = {0.20f, -0.10f, 0.05f};
  test_map_memory(0x08000000U, 0x40000U);
  memset((void *)(uintptr_t)BIAS0, 0xA5, 16);
  memset((void *)(uintptr_t)BIAS1, 0xA5, 16);
  vqf_static_cal_defaults(&defaults);
  expect_defaults(&defaults);
  assert(vqf_static_cal_load(&defaults) == -1);
  expect_defaults(&defaults);
  assert(APP_VQF_BIAS_SIGMA_INIT_DPS == 0.5f);
  assert(APP_VQF_BIAS_SIGMA_REST_DPS == 0.035f);
  assert(APP_VQF_REST_GYR_DPS == 0.6f);
  assert(APP_VQF_REST_ACC_MS2 == 0.15f);

  test_quiet_pass();
  test_sigma_band();
  expect_reject(gyro_noise_fn, VQF_STATIC_CAL_ERR_GYRO_NOISE);
  expect_reject(acc_noise_fn, VQF_STATIC_CAL_ERR_ACC_NOISE);
  expect_reject(temp_fn, VQF_STATIC_CAL_ERR_TEMP_DRIFT);
  expect_reject(drift_fn, VQF_STATIC_CAL_ERR_BIAS_DRIFT);
  test_motion_and_short();
  test_prepare_restart_and_cancel();
  test_prepare_cap_and_nan();
  test_flash_failure(still_fn, 0);
  test_flash_failure(still_fn, 1);
  (void)quiet_bias;
  test_restore();
  test_corrupt_flash();
  assert(*(const uint8_t *)(uintptr_t)BIAS0 == 0xA5);
  assert(*(const uint8_t *)(uintptr_t)BIAS1 == 0xA5);
  puts("vqf static cal: defaults, pass, reject, flash, restore: OK");
  return 0;
}
