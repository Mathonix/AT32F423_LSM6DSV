#include "vqf_static_cal.h"
#include "app_config.h"
#include "at32f423_flash.h"

#include <math.h>
#include <string.h>

#define CAL_SECTOR 0x800U
#define CAL_HIST_BINS 256U
#define CAL_HIST_SPAN 1.28f
#define CAL_HIST_STEP (CAL_HIST_SPAN / (float)CAL_HIST_BINS)
#define CAL_BLOCKS 6U
#define CAL_BLOCK_MS 10000U

_Static_assert(sizeof(vqf_static_record_t) == 124U, "static cal record");
_Static_assert((sizeof(vqf_static_record_t) % 4U) == 0U, "static cal word size");
_Static_assert(APP_VQF_STATIC_CAL_MIN_SAMPLES == 108000U, "static cal sample floor");
_Static_assert(APP_VQF_STATIC_FLASH_SLOT0 + CAL_SECTOR == APP_GYR_BIAS_FLASH_SLOT0_ADDR,
               "static cal slot0 meets gyro bias slot0");
_Static_assert(APP_VQF_STATIC_FLASH_SLOT1 + CAL_SECTOR == APP_GYR_BIAS_FLASH_SLOT1_ADDR,
               "static cal slot1 meets gyro bias slot1");
_Static_assert(APP_VQF_STATIC_FLASH_SLOT0 > APP_FUSION_SETTINGS_ADDR, "static cal above settings");

typedef struct {
  uint32_t n;
  double mean;
  double m2;
} welford_t;

typedef struct {
  uint8_t primed;
  double count;
  double sum[3];
  double state[6];
} lp3_t;

enum { SLOT_EMPTY = 0, SLOT_BAD, SLOT_EXPLICIT, SLOT_CAL, SLOT_REJECT };

static uint8_t g_state;
static uint8_t g_error;
static uint8_t g_source;
static uint8_t g_pending;
static uint8_t g_stable;
static uint8_t g_result_error;
static uint32_t g_start_ms;
static uint32_t g_stable_since;
static uint32_t g_collect_since;
static uint32_t g_last_now;
static uint32_t g_frozen_elapsed;
static uint32_t g_frozen_samples;
static uint32_t g_sample_n;
static float g_gyro_rate;
static float g_acc_dev;
static float g_temp;
static double g_temp_sum;
static float g_temp_start;
static float g_temp_end;
static welford_t g_gyr_w[3];
static welford_t g_acc_w[3];
static welford_t g_acc_norm_w;
static double g_block_sum[CAL_BLOCKS][3];
static uint32_t g_block_n[CAL_BLOCKS];
static uint32_t g_gyr_hist[CAL_HIST_BINS];
static uint32_t g_acc_hist[CAL_HIST_BINS];
static lp3_t g_gyr_lp;
static lp3_t g_acc_lp;
static double g_lp_b[3];
static double g_lp_a[2];
static double g_lp_ts;
static vqf_static_params_t g_applied;
static vqf_static_params_t g_candidate;
static float g_saved_gyr_std[3];
static float g_saved_acc_std[3];
static float g_saved_gyr_p95, g_saved_gyr_p99, g_saved_acc_p95, g_saved_acc_p99;

static int in_closed(float v, float lo, float hi);
static float norm3_diff(const float a[3], const float b[3]);

static uint32_t crc_bytes(const uint8_t *p, uint32_t n)
{
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t i, bit;
  for(i = 0U; i < n; ++i) {
    crc ^= p[i];
    for(bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

static float clampf(float v, float lo, float hi)
{
  if(v < lo) return lo;
  if(v > hi) return hi;
  return v;
}

static float norm3(const float v[3])
{
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void welford_add(welford_t *w, float x)
{
  double d, d2;
  w->n++;
  d = (double)x - w->mean;
  w->mean += d / (double)w->n;
  d2 = (double)x - w->mean;
  w->m2 += d * d2;
}

static float welford_mean(const welford_t *w)
{
  return (float)w->mean;
}

static float welford_std(const welford_t *w)
{
  if(w->n < 2U) return 0.0f;
  return (float)sqrt(w->m2 / (double)(w->n - 1U));
}

static void lp_coeffs(double tau, double ts)
{
  const double pi = 3.14159265358979323846;
  const double sqrt2 = 1.41421356237309504880;
  double fc, c, d, b0;
  if(tau < ts * 0.5) {
    g_lp_b[0] = 1.0;
    g_lp_b[1] = 0.0;
    g_lp_b[2] = 0.0;
    g_lp_a[0] = 0.0;
    g_lp_a[1] = 0.0;
    return;
  }
  fc = (sqrt2 / (2.0 * pi)) / tau;
  c = tan(pi * fc * ts);
  d = c * c + sqrt2 * c + 1.0;
  b0 = c * c / d;
  g_lp_b[0] = b0;
  g_lp_b[1] = 2.0 * b0;
  g_lp_b[2] = b0;
  g_lp_a[0] = 2.0 * (c * c - 1.0) / d;
  g_lp_a[1] = (1.0 - sqrt2 * c + c * c) / d;
}

static void lp_reset(lp3_t *lp, float dt)
{
  memset(lp, 0, sizeof(*lp));
  g_lp_ts = (double)dt;
  lp_coeffs((double)APP_VQF_REST_FILTER_TAU_S, g_lp_ts);
}

static void lp_prime(lp3_t *lp, const float y[3])
{
  int i;
  for(i = 0; i < 3; ++i) {
    double x0 = (double)y[i];
    lp->state[2 * i] = x0 * (1.0 - g_lp_b[0]);
    lp->state[2 * i + 1] = x0 * (g_lp_b[2] - g_lp_a[1]);
  }
  lp->primed = 1U;
}

static double lp_step(double x, double *st)
{
  double y = g_lp_b[0] * x + st[0];
  st[0] = g_lp_b[1] * x - g_lp_a[0] * y + st[1];
  st[1] = g_lp_b[2] * x - g_lp_a[1] * y;
  return y;
}

static void lp_apply(lp3_t *lp, const float x[3], float out[3])
{
  int i;
  if(!lp->primed) {
    lp->count += 1.0;
    for(i = 0; i < 3; ++i) {
      lp->sum[i] += (double)x[i];
      out[i] = (float)(lp->sum[i] / lp->count);
    }
    if(lp->count * g_lp_ts >= (double)APP_VQF_REST_FILTER_TAU_S) lp_prime(lp, out);
    return;
  }
  for(i = 0; i < 3; ++i) out[i] = (float)lp_step((double)x[i], lp->state + 2 * i);
}

static void hist_add(uint32_t *hist, float dev)
{
  int bin = 0;
  if(dev > 0.0f) {
    bin = (int)(dev / CAL_HIST_STEP);
    if(bin > (int)CAL_HIST_BINS - 1) bin = (int)CAL_HIST_BINS - 1;
    if(bin < 0) bin = 0;
  }
  hist[bin]++;
}

static float hist_percentile(const uint32_t *hist, uint32_t n, float fraction)
{
  double need;
  uint32_t threshold, acc = 0U, i;
  if(n == 0U) return 0.0f;
  need = (double)fraction * (double)n;
  threshold = (uint32_t)need;
  if((double)threshold < need) threshold++;
  if(threshold < 1U) threshold = 1U;
  if(threshold > n) threshold = n;
  for(i = 0U; i < CAL_HIST_BINS; ++i) {
    acc += hist[i];
    if(acc >= threshold) return ((float)i + 1.0f) * CAL_HIST_STEP;
  }
  return CAL_HIST_SPAN;
}

static int record_floats_finite(const vqf_static_record_t *rec)
{
  const float *p = rec->gyro_bias_dps;
  const float *end = &rec->temp_mean_c + 1;
  for(; p < end; ++p) if(!isfinite(*p)) return 0;
  return 1;
}

static int record_ranges_ok(const vqf_static_record_t *rec)
{
  float acc_norm;
  int i;
  if(rec->sample_count < APP_VQF_STATIC_CAL_MIN_SAMPLES) return 0;
  if(!in_closed(rec->bias_sigma_init_dps, APP_VQF_CAL_MIN_BIAS_SIGMA_INIT, APP_VQF_CAL_MAX_BIAS_SIGMA_INIT)) return 0;
  if(!in_closed(rec->bias_sigma_rest_dps, APP_VQF_CAL_MIN_BIAS_SIGMA_REST, APP_VQF_CAL_MAX_BIAS_SIGMA_REST)) return 0;
  if(!in_closed(rec->rest_th_gyr_dps, APP_VQF_CAL_MIN_REST_GYR_DPS, APP_VQF_CAL_MAX_REST_GYR_DPS)) return 0;
  if(!in_closed(rec->rest_th_acc_ms2, APP_VQF_CAL_MIN_REST_ACC_MS2, APP_VQF_CAL_MAX_REST_ACC_MS2)) return 0;
  for(i = 0; i < 3; ++i) {
    if(fabsf(rec->gyro_bias_dps[i]) > APP_VQF_CAL_MAX_BIAS_DPS) return 0;
    if(!in_closed(rec->gyro_std_dps[i], 0.0f, APP_VQF_CAL_MAX_GYRO_STD_DPS)) return 0;
    if(!in_closed(rec->acc_std_ms2[i], 0.0f, APP_VQF_CAL_MAX_ACC_STD_MS2)) return 0;
  }
  if(!in_closed(rec->gyro_dev_p95_dps, 0.0f, 2.0f) || !in_closed(rec->gyro_dev_p99_dps, 0.0f, 2.0f)) return 0;
  if(!in_closed(rec->acc_dev_p95_ms2, 0.0f, 2.0f) || !in_closed(rec->acc_dev_p99_ms2, 0.0f, 2.0f)) return 0;
  acc_norm = norm3(rec->acc_mean_ms2);
  if(fabsf(acc_norm - APP_VQF_STATIC_GRAVITY_MS2) > 1.0f) return 0;
  return 1;
}

static int in_closed(float v, float lo, float hi)
{
  return isfinite(v) && v >= lo && v <= hi;
}

static void params_from_record(const vqf_static_record_t *rec, vqf_static_params_t *out)
{
  memcpy(out->gyro_bias_dps, rec->gyro_bias_dps, sizeof(out->gyro_bias_dps));
  memcpy(out->acc_mean_ms2, rec->acc_mean_ms2, sizeof(out->acc_mean_ms2));
  out->bias_sigma_init_dps = rec->bias_sigma_init_dps;
  out->bias_sigma_rest_dps = rec->bias_sigma_rest_dps;
  out->rest_th_gyr_dps = rec->rest_th_gyr_dps;
  out->rest_th_acc_ms2 = rec->rest_th_acc_ms2;
  out->calibration_temp_c = rec->calibration_temp_c;
}

static int classify_slot(uint32_t address, vqf_static_record_t *out, uint32_t *sequence)
{
  uint32_t magic = 0xFFFFFFFFU;
  memcpy(&magic, (const void *)(uintptr_t)address, sizeof(magic));
  if(magic == 0xFFFFFFFFU) return SLOT_EMPTY;
  memcpy(out, (const void *)(uintptr_t)address, sizeof(*out));
  if(out->magic != VQF_STATIC_CAL_MAGIC || out->version != VQF_STATIC_CAL_VERSION ||
     out->size != sizeof(*out) ||
     out->crc32 != crc_bytes((const uint8_t *)out, (uint32_t)(sizeof(*out) - sizeof(out->crc32))))
    return SLOT_BAD;
  *sequence = out->sequence;
  if(!record_floats_finite(out)) return SLOT_REJECT;
  if(out->valid == 0U) return SLOT_EXPLICIT;
  if(out->valid == 1U && record_ranges_ok(out)) return SLOT_CAL;
  return SLOT_REJECT;
}

static int newest_slot(vqf_static_record_t *best, int *kind, uint32_t *address)
{
  static const uint32_t slots[2] = {
    APP_VQF_STATIC_FLASH_SLOT0, APP_VQF_STATIC_FLASH_SLOT1
  };
  int have = 0;
  uint32_t best_seq = 0U;
  unsigned s;
  *kind = SLOT_EMPTY;
  *address = 0U;
  for(s = 0U; s < 2U; ++s) {
    vqf_static_record_t rec;
    uint32_t seq = 0U;
    int got = classify_slot(slots[s], &rec, &seq);
    if(got == SLOT_EMPTY || got == SLOT_BAD) continue;
    if(!have || (int32_t)(seq - best_seq) > 0) {
      have = 1;
      best_seq = seq;
      *kind = got;
      *best = rec;
      *address = slots[s];
    }
  }
  return have;
}

static int program_record(uint32_t address, const vqf_static_record_t *rec)
{
  vqf_static_record_t verify;
  flash_status_type status;
  uint32_t i;
  flash_unlock();
  status = flash_sector_erase(address);
  if(status != FLASH_OPERATE_DONE) { flash_lock(); return -1; }
  for(i = 0U; i < (uint32_t)(sizeof(*rec) / 4U); ++i) {
    uint32_t word;
    memcpy(&word, ((const uint8_t *)rec) + i * 4U, sizeof(word));
    status = flash_word_program(address + i * 4U, word);
    if(status != FLASH_OPERATE_DONE) { flash_lock(); return -1; }
  }
  flash_lock();
  memcpy(&verify, (const void *)(uintptr_t)address, sizeof(verify));
  return memcmp(&verify, rec, sizeof(verify)) == 0 ? 0 : -1;
}

static void seal_record(vqf_static_record_t *rec)
{
  rec->magic = VQF_STATIC_CAL_MAGIC;
  rec->version = VQF_STATIC_CAL_VERSION;
  rec->size = (uint16_t)sizeof(*rec);
  rec->crc32 = crc_bytes((const uint8_t *)rec, (uint32_t)(sizeof(*rec) - sizeof(rec->crc32)));
}

static int commit_record(const vqf_static_record_t *filled)
{
  vqf_static_record_t latest, rec;
  uint32_t latest_addr = 0U;
  uint32_t target;
  int kind = SLOT_EMPTY;
  int have = newest_slot(&latest, &kind, &latest_addr);
  memset(&rec, 0, sizeof(rec));
  rec = *filled;
  rec.sequence = have ? latest.sequence + 1U : 1U;
  seal_record(&rec);
  if(!have || latest_addr == APP_VQF_STATIC_FLASH_SLOT1) target = APP_VQF_STATIC_FLASH_SLOT0;
  else target = APP_VQF_STATIC_FLASH_SLOT1;
  return program_record(target, &rec);
}

static void freeze_elapsed(void)
{
  if(g_state == VQF_STATIC_CAL_COLLECTING || g_state == VQF_STATIC_CAL_VALIDATING)
    g_frozen_elapsed = g_last_now - g_collect_since;
  else g_frozen_elapsed = g_last_now - g_start_ms;
  g_frozen_samples = (g_state == VQF_STATIC_CAL_COLLECTING ||
                      g_state == VQF_STATIC_CAL_VALIDATING) ? g_sample_n : 0U;
}

static void fail_run(uint8_t err)
{
  freeze_elapsed();
  g_state = VQF_STATIC_CAL_FAILED;
  g_error = err;
  g_pending = 1U;
}

static void reset_stats(float dt)
{
  memset(g_gyr_w, 0, sizeof(g_gyr_w));
  memset(g_acc_w, 0, sizeof(g_acc_w));
  memset(&g_acc_norm_w, 0, sizeof(g_acc_norm_w));
  memset(g_block_sum, 0, sizeof(g_block_sum));
  memset(g_block_n, 0, sizeof(g_block_n));
  memset(g_gyr_hist, 0, sizeof(g_gyr_hist));
  memset(g_acc_hist, 0, sizeof(g_acc_hist));
  g_sample_n = 0U;
  g_temp_sum = 0.0;
  g_temp_start = 0.0f;
  g_temp_end = 0.0f;
  lp_reset(&g_gyr_lp, dt);
  lp_reset(&g_acc_lp, dt);
}

static void add_sample(uint32_t now, const float gyr[3], const float acc[3], float temp)
{
  float gref[3], aref[3];
  uint32_t block;
  int i;
  block = (now - g_collect_since) / CAL_BLOCK_MS;
  if(block >= CAL_BLOCKS) block = CAL_BLOCKS - 1U;
  if(g_sample_n == 0U) g_temp_start = temp;
  g_temp_end = temp;
  g_temp_sum += (double)temp;
  for(i = 0; i < 3; ++i) {
    welford_add(&g_gyr_w[i], gyr[i]);
    welford_add(&g_acc_w[i], acc[i]);
    g_block_sum[block][i] += (double)gyr[i];
  }
  g_block_n[block]++;
  welford_add(&g_acc_norm_w, norm3(acc));
  lp_apply(&g_gyr_lp, gyr, gref);
  lp_apply(&g_acc_lp, acc, aref);
  hist_add(g_gyr_hist, norm3_diff(gyr, gref));
  hist_add(g_acc_hist, norm3_diff(acc, aref));
  g_sample_n++;
}

static float norm3_diff(const float a[3], const float b[3])
{
  float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return norm3(d);
}

static float std_of_block_means(int axis)
{
  float mean = 0.0f, m2 = 0.0f;
  float v[CAL_BLOCKS];
  unsigned i;
  for(i = 0U; i < CAL_BLOCKS; ++i) v[i] = (float)(g_block_sum[i][axis] / (double)g_block_n[i]);
  for(i = 0U; i < CAL_BLOCKS; ++i) mean += v[i];
  mean /= (float)CAL_BLOCKS;
  for(i = 0U; i < CAL_BLOCKS; ++i) {
    float d = v[i] - mean;
    m2 += d * d;
  }
  return sqrtf(m2 / (float)(CAL_BLOCKS - 1U));
}

static float block_range(int axis)
{
  float lo, hi;
  unsigned i;
  lo = hi = (float)(g_block_sum[0][axis] / (double)g_block_n[0]);
  for(i = 1U; i < CAL_BLOCKS; ++i) {
    float v = (float)(g_block_sum[i][axis] / (double)g_block_n[i]);
    if(v < lo) lo = v;
    if(v > hi) hi = v;
  }
  return hi - lo;
}

static uint8_t evaluate(void)
{
  float gstd[3], astd[3], bias[3], acc_mean[3];
  float rms, max_block_std, gyr_p99, acc_p99;
  int i;
  unsigned b;
  if(g_sample_n < APP_VQF_STATIC_CAL_MIN_SAMPLES) return VQF_STATIC_CAL_ERR_SAMPLE_COUNT;
  for(b = 0U; b < CAL_BLOCKS; ++b) if(g_block_n[b] == 0U) return VQF_STATIC_CAL_ERR_SAMPLE_COUNT;
  for(i = 0; i < 3; ++i) {
    bias[i] = welford_mean(&g_gyr_w[i]);
    acc_mean[i] = welford_mean(&g_acc_w[i]);
    gstd[i] = welford_std(&g_gyr_w[i]);
    astd[i] = welford_std(&g_acc_w[i]);
    if(!isfinite(bias[i]) || !isfinite(acc_mean[i]) || !isfinite(gstd[i]) || !isfinite(astd[i]))
      return VQF_STATIC_CAL_ERR_INVALID_NUMBER;
  }
  if(!isfinite(g_temp_start) || !isfinite(g_temp_end) || !isfinite((float)g_temp_sum))
    return VQF_STATIC_CAL_ERR_INVALID_NUMBER;
  for(i = 0; i < 3; ++i) if(fabsf(bias[i]) > APP_VQF_CAL_MAX_BIAS_DPS) return VQF_STATIC_CAL_ERR_BIAS_RANGE;
  for(i = 0; i < 3; ++i) if(gstd[i] > APP_VQF_CAL_MAX_GYRO_STD_DPS) return VQF_STATIC_CAL_ERR_GYRO_NOISE;
  for(i = 0; i < 3; ++i) if(astd[i] > APP_VQF_CAL_MAX_ACC_STD_MS2) return VQF_STATIC_CAL_ERR_ACC_NOISE;
  if(welford_std(&g_acc_norm_w) > APP_VQF_CAL_MAX_ACC_STD_MS2) return VQF_STATIC_CAL_ERR_ACC_NOISE;
  if(fabsf(g_temp_end - g_temp_start) >= APP_VQF_CAL_MAX_TEMP_SPAN_C) return VQF_STATIC_CAL_ERR_TEMP_DRIFT;
  for(i = 0; i < 3; ++i) if(block_range(i) > APP_VQF_CAL_MAX_BIAS_DRIFT_DPS) return VQF_STATIC_CAL_ERR_BIAS_DRIFT;

  rms = sqrtf((gstd[0] * gstd[0] + gstd[1] * gstd[1] + gstd[2] * gstd[2]) / 3.0f);
  max_block_std = 0.0f;
  for(i = 0; i < 3; ++i) {
    float s = std_of_block_means(i);
    if(!isfinite(s)) return VQF_STATIC_CAL_ERR_INVALID_NUMBER;
    if(s > max_block_std) max_block_std = s;
  }
  gyr_p99 = hist_percentile(g_gyr_hist, g_sample_n, 0.99f);
  acc_p99 = hist_percentile(g_acc_hist, g_sample_n, 0.99f);
  memset(&g_candidate, 0, sizeof(g_candidate));
  memcpy(g_candidate.gyro_bias_dps, bias, sizeof(bias));
  memcpy(g_candidate.acc_mean_ms2, acc_mean, sizeof(acc_mean));
  g_candidate.bias_sigma_init_dps = clampf(APP_VQF_CAL_SIGMA_INIT_SCALE * max_block_std,
                                           APP_VQF_CAL_MIN_BIAS_SIGMA_INIT, APP_VQF_CAL_MAX_BIAS_SIGMA_INIT);
  g_candidate.bias_sigma_rest_dps = clampf(rms, APP_VQF_CAL_MIN_BIAS_SIGMA_REST, APP_VQF_CAL_MAX_BIAS_SIGMA_REST);
  g_candidate.rest_th_gyr_dps = clampf(gyr_p99 * APP_VQF_CAL_REST_GYR_SCALE,
                                       APP_VQF_CAL_MIN_REST_GYR_DPS, APP_VQF_CAL_MAX_REST_GYR_DPS);
  g_candidate.rest_th_acc_ms2 = clampf(acc_p99 * APP_VQF_CAL_REST_ACC_SCALE,
                                       APP_VQF_CAL_MIN_REST_ACC_MS2, APP_VQF_CAL_MAX_REST_ACC_MS2);
  g_candidate.calibration_temp_c = (float)(g_temp_sum / (double)g_sample_n);
  if(!isfinite(g_candidate.bias_sigma_init_dps) || !isfinite(g_candidate.bias_sigma_rest_dps) ||
     !isfinite(g_candidate.rest_th_gyr_dps) || !isfinite(g_candidate.rest_th_acc_ms2) ||
     !isfinite(g_candidate.calibration_temp_c) || !isfinite(gyr_p99) || !isfinite(acc_p99))
    return VQF_STATIC_CAL_ERR_INVALID_NUMBER;
  memcpy(g_saved_gyr_std, gstd, sizeof(gstd));
  memcpy(g_saved_acc_std, astd, sizeof(astd));
  g_saved_gyr_p95 = hist_percentile(g_gyr_hist, g_sample_n, 0.95f);
  g_saved_gyr_p99 = gyr_p99;
  g_saved_acc_p95 = hist_percentile(g_acc_hist, g_sample_n, 0.95f);
  g_saved_acc_p99 = acc_p99;
  return VQF_STATIC_CAL_OK;
}

static void begin_validate(void)
{
  if(g_state != VQF_STATIC_CAL_COLLECTING) return;
  g_result_error = evaluate();
  g_state = VQF_STATIC_CAL_VALIDATING;
}

static int save_candidate(void)
{
  vqf_static_record_t rec;
  memset(&rec, 0, sizeof(rec));
  rec.valid = 1U;
  memcpy(rec.gyro_bias_dps, g_candidate.gyro_bias_dps, sizeof(rec.gyro_bias_dps));
  memcpy(rec.acc_mean_ms2, g_candidate.acc_mean_ms2, sizeof(rec.acc_mean_ms2));
  rec.bias_sigma_init_dps = g_candidate.bias_sigma_init_dps;
  rec.bias_sigma_rest_dps = g_candidate.bias_sigma_rest_dps;
  rec.rest_th_gyr_dps = g_candidate.rest_th_gyr_dps;
  rec.rest_th_acc_ms2 = g_candidate.rest_th_acc_ms2;
  rec.calibration_temp_c = g_candidate.calibration_temp_c;
  memcpy(rec.gyro_std_dps, g_saved_gyr_std, sizeof(rec.gyro_std_dps));
  memcpy(rec.acc_std_ms2, g_saved_acc_std, sizeof(rec.acc_std_ms2));
  rec.gyro_dev_p95_dps = g_saved_gyr_p95;
  rec.gyro_dev_p99_dps = g_saved_gyr_p99;
  rec.acc_dev_p95_ms2 = g_saved_acc_p95;
  rec.acc_dev_p99_ms2 = g_saved_acc_p99;
  rec.temp_start_c = g_temp_start;
  rec.temp_end_c = g_temp_end;
  rec.temp_mean_c = g_candidate.calibration_temp_c;
  rec.sample_count = g_sample_n;
  rec.flags = 0U;
  return commit_record(&rec);
}

static int moving_sample(float gyro_norm, float acc_off_gravity)
{
  return gyro_norm > APP_VQF_STATIC_CAL_MOVE_GYR_DPS ||
         acc_off_gravity > APP_VQF_STATIC_CAL_MOVE_ACC_MS2;
}

void vqf_static_cal_defaults(vqf_static_params_t *out)
{
  if(out == NULL) return;
  memset(out, 0, sizeof(*out));
  out->bias_sigma_init_dps = APP_VQF_BIAS_SIGMA_INIT_DPS;
  out->bias_sigma_rest_dps = APP_VQF_BIAS_SIGMA_REST_DPS;
  out->rest_th_gyr_dps = APP_VQF_REST_GYR_DPS;
  out->rest_th_acc_ms2 = APP_VQF_REST_ACC_MS2;
  out->acc_mean_ms2[2] = APP_VQF_STATIC_GRAVITY_MS2;
}

int vqf_static_cal_load(vqf_static_params_t *out)
{
  vqf_static_record_t best;
  uint32_t addr = 0U;
  int kind = SLOT_EMPTY;
  vqf_static_params_t defaults;
  vqf_static_cal_defaults(&defaults);
  if(newest_slot(&best, &kind, &addr) && kind == SLOT_CAL) {
    params_from_record(&best, &g_applied);
    g_source = VQF_STATIC_CAL_SOURCE_CAL;
  } else {
    g_applied = defaults;
    g_source = VQF_STATIC_CAL_SOURCE_DEFAULT;
  }
  if(out != NULL) *out = g_applied;
  return g_source == VQF_STATIC_CAL_SOURCE_CAL ? 0 : -1;
}

void vqf_static_cal_get_applied(vqf_static_params_t *out)
{
  if(out == NULL) return;
  if(g_source == VQF_STATIC_CAL_SOURCE_DEFAULT &&
     g_applied.bias_sigma_init_dps == 0.0f) vqf_static_cal_defaults(&g_applied);
  *out = g_applied;
}

uint8_t vqf_static_cal_source(void)
{
  return g_source;
}

int vqf_static_cal_active(void)
{
  return g_pending ||
         g_state == VQF_STATIC_CAL_PRECHECK ||
         g_state == VQF_STATIC_CAL_PRE_STABLE ||
         g_state == VQF_STATIC_CAL_COLLECTING ||
         g_state == VQF_STATIC_CAL_VALIDATING;
}

int vqf_static_cal_start(uint32_t now_ms)
{
  if(vqf_static_cal_active()) return -1;
  g_state = VQF_STATIC_CAL_PRECHECK;
  g_error = VQF_STATIC_CAL_OK;
  g_pending = 0U;
  g_stable = 0U;
  g_result_error = VQF_STATIC_CAL_OK;
  g_start_ms = now_ms;
  g_last_now = now_ms;
  g_stable_since = 0U;
  g_collect_since = 0U;
  g_sample_n = 0U;
  g_gyro_rate = 0.0f;
  g_acc_dev = 0.0f;
  g_temp = 0.0f;
  g_frozen_elapsed = 0U;
  g_frozen_samples = 0U;
  return 0;
}

void vqf_static_cal_cancel(void)
{
  g_state = VQF_STATIC_CAL_IDLE;
  g_error = VQF_STATIC_CAL_OK;
  g_pending = 0U;
  g_stable = 0U;
  g_sample_n = 0U;
  g_gyro_rate = 0.0f;
  g_acc_dev = 0.0f;
  g_frozen_elapsed = 0U;
  g_frozen_samples = 0U;
}

int vqf_static_cal_restore_defaults(void)
{
  vqf_static_record_t rec;
  vqf_static_params_t defaults;
  if(vqf_static_cal_active()) return -1;
  vqf_static_cal_defaults(&defaults);
  memset(&rec, 0, sizeof(rec));
  rec.valid = 0U;
  memcpy(rec.gyro_bias_dps, defaults.gyro_bias_dps, sizeof(rec.gyro_bias_dps));
  memcpy(rec.acc_mean_ms2, defaults.acc_mean_ms2, sizeof(rec.acc_mean_ms2));
  rec.bias_sigma_init_dps = defaults.bias_sigma_init_dps;
  rec.bias_sigma_rest_dps = defaults.bias_sigma_rest_dps;
  rec.rest_th_gyr_dps = defaults.rest_th_gyr_dps;
  rec.rest_th_acc_ms2 = defaults.rest_th_acc_ms2;
  rec.calibration_temp_c = 0.0f;
  if(commit_record(&rec) != 0) return -2;
  g_applied = defaults;
  g_applied.calibration_temp_c = 0.0f;
  g_source = VQF_STATIC_CAL_SOURCE_DEFAULT;
  g_state = VQF_STATIC_CAL_IDLE;
  g_error = VQF_STATIC_CAL_OK;
  g_pending = 0U;
  g_sample_n = 0U;
  return 0;
}

static void note_sample(const float gyr[3], const float acc[3], float temp)
{
  float anorm = norm3(acc);
  g_gyro_rate = norm3(gyr);
  g_acc_dev = fabsf(anorm - APP_VQF_STATIC_GRAVITY_MS2);
  g_temp = temp;
}

void vqf_static_cal_feed(uint32_t now_ms, float dt_s,
                         const float gyr_dps[3], const float acc_ms2[3], float temp_c)
{
  int i, moving;
  if(g_state != VQF_STATIC_CAL_PRECHECK && g_state != VQF_STATIC_CAL_PRE_STABLE &&
     g_state != VQF_STATIC_CAL_COLLECTING) return;
  g_last_now = now_ms;
  if(!isfinite(dt_s) || dt_s <= 0.0f || dt_s > 0.05f || !isfinite(temp_c)) {
    fail_run(VQF_STATIC_CAL_ERR_INVALID_NUMBER);
    return;
  }
  for(i = 0; i < 3; ++i) {
    if(!isfinite(gyr_dps[i]) || !isfinite(acc_ms2[i])) {
      fail_run(VQF_STATIC_CAL_ERR_INVALID_NUMBER);
      return;
    }
  }
  note_sample(gyr_dps, acc_ms2, temp_c);
  moving = moving_sample(g_gyro_rate, g_acc_dev);
  if(g_state == VQF_STATIC_CAL_PRECHECK || g_state == VQF_STATIC_CAL_PRE_STABLE) {
    if(moving) {
      g_stable = 0U;
      g_state = VQF_STATIC_CAL_PRECHECK;
    } else if(!g_stable) {
      g_stable = 1U;
      g_stable_since = now_ms;
      g_state = VQF_STATIC_CAL_PRE_STABLE;
    }
    if(g_stable && (uint32_t)(now_ms - g_stable_since) >= APP_VQF_STATIC_CAL_PREPARE_MS) {
      reset_stats(dt_s);
      g_collect_since = now_ms;
      g_state = VQF_STATIC_CAL_COLLECTING;
      add_sample(now_ms, gyr_dps, acc_ms2, temp_c);
      if((uint32_t)(now_ms - g_collect_since) >= APP_VQF_STATIC_CAL_COLLECT_MS) begin_validate();
    } else if((uint32_t)(now_ms - g_start_ms) >= APP_VQF_STATIC_CAL_PREPARE_CAP_MS) {
      fail_run(VQF_STATIC_CAL_ERR_MOVED);
    }
    return;
  }
  if(moving) {
    fail_run(VQF_STATIC_CAL_ERR_MOVED);
    return;
  }
  add_sample(now_ms, gyr_dps, acc_ms2, temp_c);
  if((uint32_t)(now_ms - g_collect_since) >= APP_VQF_STATIC_CAL_COLLECT_MS) begin_validate();
}

void vqf_static_cal_tick(uint32_t now_ms)
{
  g_last_now = now_ms;
  if(g_state == VQF_STATIC_CAL_PRECHECK || g_state == VQF_STATIC_CAL_PRE_STABLE) {
    if((uint32_t)(now_ms - g_start_ms) >= APP_VQF_STATIC_CAL_PREPARE_CAP_MS)
      fail_run(VQF_STATIC_CAL_ERR_MOVED);
  } else if(g_state == VQF_STATIC_CAL_COLLECTING) {
    if((uint32_t)(now_ms - g_collect_since) >= APP_VQF_STATIC_CAL_COLLECT_MS) begin_validate();
  }
}

void vqf_static_cal_get_status(vqf_static_cal_status_t *out)
{
  uint32_t elapsed = 0U, remaining = 0U, samples = 0U;
  if(out == NULL) return;
  if(g_state == VQF_STATIC_CAL_DONE || g_state == VQF_STATIC_CAL_FAILED) {
    elapsed = g_frozen_elapsed;
    samples = g_frozen_samples;
  } else if(g_state == VQF_STATIC_CAL_COLLECTING || g_state == VQF_STATIC_CAL_VALIDATING) {
    elapsed = g_last_now - g_collect_since;
    remaining = elapsed >= APP_VQF_STATIC_CAL_COLLECT_MS ? 0U : APP_VQF_STATIC_CAL_COLLECT_MS - elapsed;
    samples = g_sample_n;
  } else if(g_state == VQF_STATIC_CAL_PRE_STABLE) {
    uint32_t stable_elapsed = g_last_now - g_stable_since;
    uint32_t prep_left = stable_elapsed >= APP_VQF_STATIC_CAL_PREPARE_MS ? 0U :
                         APP_VQF_STATIC_CAL_PREPARE_MS - stable_elapsed;
    elapsed = stable_elapsed;
    remaining = prep_left + APP_VQF_STATIC_CAL_COLLECT_MS;
  } else if(g_state == VQF_STATIC_CAL_PRECHECK) {
    elapsed = g_last_now - g_start_ms;
    remaining = APP_VQF_STATIC_CAL_PREPARE_MS + APP_VQF_STATIC_CAL_COLLECT_MS;
  }
  memset(out, 0, sizeof(*out));
  out->state = g_state;
  out->error = g_error;
  out->source = g_source;
  out->elapsed_ms = elapsed;
  out->remaining_ms = remaining;
  out->sample_count = samples;
  out->gyro_rate_dps = g_gyro_rate;
  out->acc_deviation_ms2 = g_acc_dev;
  out->temperature_c = g_temp;
}

int vqf_static_cal_poll(vqf_static_cal_status_t *status, vqf_static_params_t *params)
{
  if(g_state == VQF_STATIC_CAL_VALIDATING) {
    uint8_t err = g_result_error;
    g_frozen_elapsed = g_last_now - g_collect_since;
    g_frozen_samples = g_sample_n;
    if(err == VQF_STATIC_CAL_OK && save_candidate() != 0) err = VQF_STATIC_CAL_ERR_FLASH_WRITE;
    if(err == VQF_STATIC_CAL_OK) {
      g_applied = g_candidate;
      g_source = VQF_STATIC_CAL_SOURCE_CAL;
      g_state = VQF_STATIC_CAL_DONE;
    } else g_state = VQF_STATIC_CAL_FAILED;
    g_error = err;
    g_pending = 1U;
  }
  if(!g_pending) return 0;
  g_pending = 0U;
  if(status != NULL) vqf_static_cal_get_status(status);
  if(params != NULL && g_state == VQF_STATIC_CAL_DONE) *params = g_applied;
  return 1;
}
