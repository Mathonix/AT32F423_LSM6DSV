#include "gyro_bias_history.h"
#include "gyro_temp_comp.h"
#include "app_config.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define BIAS_MAGIC       0x42494153U
#define BIAS_VERSION     3U
#define BIAS_VERSION_V2  2U
#define BIAS_V2_MAX      15U
#define BIAS_SLOT0_ADDR  APP_GYR_BIAS_FLASH_SLOT0_ADDR
#define BIAS_SLOT1_ADDR  APP_GYR_BIAS_FLASH_SLOT1_ADDR

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  uint32_t count;
  uint32_t next;
  float bias[GYRO_BIAS_HISTORY_MAX][3];
  float temperature_c[GYRO_BIAS_HISTORY_MAX];
  uint32_t crc;
} gyro_bias_record_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  uint32_t count;
  uint32_t next;
  float bias[BIAS_V2_MAX][3];
  float temperature_c[BIAS_V2_MAX];
  uint32_t crc;
} gyro_bias_record_v2_t;

_Static_assert(sizeof(gyro_bias_record_v2_t) == 264U, "v2 bias record layout");
_Static_assert(sizeof(gyro_bias_record_t) == 824U, "v3 bias record layout");
_Static_assert(sizeof(gyro_bias_record_t) <= 2048U, "bias record exceeds one sector");
_Static_assert((APP_GYR_BIAS_FLASH_SLOT0_ADDR & 0x7FFU) == 0U, "bias slot0 align");
_Static_assert((APP_GYR_BIAS_FLASH_SLOT1_ADDR & 0x7FFU) == 0U, "bias slot1 align");

static uint32_t bias_crc_bytes(const uint8_t *p, uint32_t n)
{
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t i, bit;
  for(i = 0U; i < n; ++i)
  {
    crc ^= p[i];
    for(bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc;
}

static int entries_finite(const float bias[][3], const float *temperature_c, uint32_t count, uint32_t max_count)
{
  uint32_t i;
  if(count > max_count) return 0;
  for(i = 0U; i < count; ++i)
  {
    if(!isfinite(bias[i][0]) || !isfinite(bias[i][1]) ||
       !isfinite(bias[i][2]) || !isfinite(temperature_c[i])) return 0;
  }
  return 1;
}

static int v3_valid(const gyro_bias_record_t *record)
{
  if(record->magic != BIAS_MAGIC || record->version != BIAS_VERSION ||
     record->count > GYRO_BIAS_HISTORY_MAX || record->next >= GYRO_BIAS_HISTORY_MAX ||
     record->crc != bias_crc_bytes((const uint8_t *)record, (uint32_t)(sizeof(*record) - sizeof(record->crc))))
    return 0;
  return entries_finite(record->bias, record->temperature_c, record->count, GYRO_BIAS_HISTORY_MAX);
}

static int v2_valid(const gyro_bias_record_v2_t *record)
{
  if(record->magic != BIAS_MAGIC || record->version != BIAS_VERSION_V2 ||
     record->count > BIAS_V2_MAX || record->next >= BIAS_V2_MAX ||
     record->crc != bias_crc_bytes((const uint8_t *)record, (uint32_t)(sizeof(*record) - sizeof(record->crc))))
    return 0;
  return entries_finite(record->bias, record->temperature_c, record->count, BIAS_V2_MAX);
}

/* Read-only views replace three nested 824-byte copies. All callers run in
 * the foreground; no view escapes a call and no ISR writes these Flash slots.
 * There is no shared scratch buffer or extra BSS allocation. V2 full journals
 * retain the old import's chronological indexing without materializing V3. */
typedef struct {
  const float (*bias)[3];
  const float *temperature_c;
  uint32_t sequence, count, next, capacity, start;
} history_view_t;

static const float *bias_at(const history_view_t *v,uint32_t index)
{ return v->bias[(index+v->start)%v->capacity]; }
static float temperature_at(const history_view_t *v,uint32_t index)
{ return v->temperature_c[(index+v->start)%v->capacity]; }

static int accept_slot(uint32_t address, history_view_t *out, int *valid, int *corrupt,
                       uint8_t *flash_version)
{
  uint32_t magic = 0U, version = 0U;
  *valid = 0;
  *corrupt = 0;
  if(flash_version != NULL) *flash_version = 0U;
  memcpy(&magic, (const void *)(uintptr_t)address, sizeof(magic));
  if(magic == 0xFFFFFFFFU) return 0;
  memcpy(&version, (const void *)(uintptr_t)(address + 4U), sizeof(version));
  if(version == BIAS_VERSION)
  {
    const gyro_bias_record_t *rec=(const void *)(uintptr_t)address;
    if(v3_valid(rec))
    {
      *out=(history_view_t){rec->bias,rec->temperature_c,
                           rec->sequence,rec->count,rec->next,GYRO_BIAS_HISTORY_MAX,0U};
      *valid = 1;
      if(flash_version != NULL) *flash_version = (uint8_t)BIAS_VERSION;
      return 1;
    }
  }
  else if(version == BIAS_VERSION_V2)
  {
    const gyro_bias_record_v2_t *rec=(const void *)(uintptr_t)address;
    if(v2_valid(rec))
    {
      *out=(history_view_t){rec->bias,rec->temperature_c,
                           rec->sequence,rec->count,rec->count,BIAS_V2_MAX,
                           rec->count==BIAS_V2_MAX?rec->next:0U};
      *valid = 1;
      if(flash_version != NULL) *flash_version = (uint8_t)BIAS_VERSION_V2;
      return 1;
    }
  }
  *corrupt = 1;
  return 0;
}

static int newer(const history_view_t *a, const history_view_t *b)
{
  return (int32_t)(a->sequence - b->sequence) > 0;
}

static int load_legacy_records(history_view_t *best, uint8_t *any_corrupt, uint8_t *flash_version, uint32_t *selected_address)
{
  history_view_t a, b;
  int va, vb, ca, cb;
  uint8_t ver_a = 0U, ver_b = 0U;
  accept_slot(BIAS_SLOT0_ADDR, &a, &va, &ca, &ver_a);
  accept_slot(BIAS_SLOT1_ADDR, &b, &vb, &cb, &ver_b);
  if(any_corrupt != NULL) *any_corrupt = (uint8_t)(ca || cb);
  if(!va && !vb)
  {
    if(flash_version != NULL) *flash_version = 0U;
    return 0;
  }
  if(va && (!vb || newer(&a, &b)))
  {
    *best = a;
    if(selected_address) *selected_address=BIAS_SLOT0_ADDR;
    if(flash_version != NULL) *flash_version = ver_a;
  }
  else
  {
    *best = b;
    if(selected_address) *selected_address=BIAS_SLOT1_ADDR;
    if(flash_version != NULL) *flash_version = ver_b;
  }
  return 1;
}

static int load_records(history_view_t *best, uint8_t *corrupt, uint8_t *version, uint32_t *address)
{
  /* Once V4 is committed, legacy telemetry returns a legal empty history.
   * It must not interpret temperature bins as old startup samples. */
  if(gyro_temp_comp_has_v4()) {
    if(corrupt) *corrupt=0U;
    if(version) *version=0U;
    return 0;
  }
  return load_legacy_records(best,corrupt,version,address);
}

static uint32_t chrono_index(const history_view_t *record, uint32_t logical)
{
  uint32_t base = (record->count < GYRO_BIAS_HISTORY_MAX) ? 0U : record->next;
  return (base + logical) % GYRO_BIAS_HISTORY_MAX;
}

static uint32_t latest_index(const history_view_t *record)
{
  return (record->count < GYRO_BIAS_HISTORY_MAX) ? (record->count - 1U) :
         ((record->next + GYRO_BIAS_HISTORY_MAX - 1U) % GYRO_BIAS_HISTORY_MAX);
}

int gyro_bias_history_legacy_metadata(float raw_bias_rad_s[3],float *temp_c,
                                      uint32_t *sequence,uint8_t *slot)
{
  history_view_t record;
  uint32_t address;
  if(!load_legacy_records(&record,NULL,NULL,&address)) return 0;
  *sequence=record.sequence; *slot=(uint8_t)(address==BIAS_SLOT1_ADDR);
  if(!record.count) { memset(raw_bias_rad_s,0,12U); *temp_c=25.f; return 2; }
  uint32_t i=latest_index(&record);
  memcpy(raw_bias_rad_s,bias_at(&record,i),12U);
  *temp_c=temperature_at(&record,i);
  return 1;
}

int gyro_bias_history_load_for_temp(float latest[3], float average[3],
                                    float nearest[3], float current_temp_c,
                                    float temp_window_c, float *nearest_temp_c,
                                    uint8_t *nearest_valid, uint32_t *count,
                                    uint8_t *corrupt)
{
  history_view_t record;
  uint32_t i, j, n, li, near_n = 0U;
  float temp_sum = 0.0f;
  if(latest != NULL) memset(latest, 0, sizeof(float) * 3U);
  if(average != NULL) memset(average, 0, sizeof(float) * 3U);
  if(nearest != NULL) memset(nearest, 0, sizeof(float) * 3U);
  if(nearest_temp_c != NULL) *nearest_temp_c = 0.0f;
  if(nearest_valid != NULL) *nearest_valid = 0U;
  if(count != NULL) *count = 0U;
  if(corrupt != NULL) *corrupt = 0U;
  if(!load_records(&record, corrupt, NULL, NULL) || record.count == 0U) return 0;

  n = record.count;
  li = latest_index(&record);
  for(i = 0U; i < 3U; ++i)
  {
    if(latest != NULL) latest[i] = bias_at(&record,li)[i];
    if(average != NULL)
      for(j = 0U; j < n; ++j) average[i] += bias_at(&record,j)[i] / (float)n;
  }
  for(j = 0U; j < n; ++j)
  {
    if(fabsf(temperature_at(&record,j) - current_temp_c) <= temp_window_c)
    {
      near_n++;
      temp_sum += temperature_at(&record,j);
      if(nearest != NULL)
        for(i = 0U; i < 3U; ++i) nearest[i] += bias_at(&record,j)[i];
    }
  }
  if(near_n != 0U)
  {
    if(nearest != NULL)
      for(i = 0U; i < 3U; ++i) nearest[i] /= (float)near_n;
    if(nearest_temp_c != NULL) *nearest_temp_c = temp_sum / (float)near_n;
    if(nearest_valid != NULL) *nearest_valid = 1U;
  }
  if(count != NULL) *count = n;
  return 1;
}

int gyro_bias_history_load(float latest[3], float average[3], uint32_t *count, uint8_t *corrupt)
{
  float nearest[3], nearest_temp;
  uint8_t nearest_valid;
  return gyro_bias_history_load_for_temp(latest, average, nearest,
                                          APP_GYR_TEMP_REF_C, 0.0f,
                                          &nearest_temp, &nearest_valid,
                                          count, corrupt);
}

int gyro_bias_history_load_recent_for_temp(float latest[3], float average[3],
    float selected[3], float current_temp_c, float temp_window_c,
    float *selected_temp_c, uint8_t *selected_valid, uint32_t *count,
    uint8_t *corrupt)
{
  history_view_t record;
  if(selected != NULL) memset(selected, 0, sizeof(float) * 3U);
  if(selected_temp_c != NULL) *selected_temp_c = 0.0f;
  if(selected_valid != NULL) *selected_valid = 0U;
  int valid = gyro_bias_history_load(latest, average, count, corrupt);
  if(!valid || !isfinite(current_temp_c) || !isfinite(temp_window_c) ||
     temp_window_c < 0.0f || !load_records(&record, NULL, NULL, NULL)) return valid;
  for(uint32_t logical = record.count; logical > 0U; --logical) {
    uint32_t physical = chrono_index(&record, logical - 1U);
    if(fabsf(temperature_at(&record,physical) - current_temp_c) <= temp_window_c) {
      if(selected != NULL) memcpy(selected, bias_at(&record,physical), sizeof(float) * 3U);
      if(selected_temp_c != NULL) *selected_temp_c = temperature_at(&record,physical);
      if(selected_valid != NULL) *selected_valid = 1U;
      break;
    }
  }
  return valid;
}

int gyro_bias_history_load_nearest(float bias[3], float temperature_c, float *matched_temperature_c)
{
  history_view_t record;
  float best_distance=INFINITY;
  uint32_t best=0U;
  if(!bias || !isfinite(temperature_c) || !load_records(&record,NULL,NULL,NULL) || !record.count) return 0;
  for(uint32_t logical=0;logical<record.count;++logical) {
    uint32_t physical=chrono_index(&record,logical);
    float distance=fabsf(temperature_at(&record,physical)-temperature_c);
    if(distance<=best_distance) { best_distance=distance; best=physical; }
  }
  memcpy(bias,bias_at(&record,best),sizeof(float)*3U);
  if(matched_temperature_c) *matched_temperature_c=temperature_at(&record,best);
  return 1;
}

int gyro_bias_history_save_at_temp(const float bias[3], float temperature_c)
{
  /* The sole writer is the queued V4 maintenance service. This compatibility
   * API accepts nominal RAW rad/s and never writes flash in a callback. */
  return gyro_temp_comp_queue_fallback_rad(bias,temperature_c);
}

int gyro_bias_history_save(const float bias[3])
{
  return gyro_bias_history_save_at_temp(bias, APP_GYR_TEMP_REF_C);
}

int gyro_bias_history_read(uint32_t offset, uint32_t limit,
                           float bias[][3], float *temperature_c,
                           uint32_t *copied, uint32_t *count,
                           uint32_t *sequence, uint8_t *record_version,
                           uint8_t *corrupt)
{
  history_view_t record;
  uint8_t version = 0U;
  uint32_t n = 0U;
  uint32_t i;
  if(copied != NULL) *copied = 0U;
  if(count != NULL) *count = 0U;
  if(sequence != NULL) *sequence = 0U;
  if(record_version != NULL) *record_version = 0U;
  if(corrupt != NULL) *corrupt = 0U;
  if(!load_records(&record, corrupt, &version, NULL) || record.count == 0U) return 0;
  if(count != NULL) *count = record.count;
  if(sequence != NULL) *sequence = record.sequence;
  if(record_version != NULL) *record_version = version;
  if(offset >= record.count || limit == 0U || bias == NULL || temperature_c == NULL) return 0;
  n = record.count - offset;
  if(n > limit) n = limit;
  for(i = 0U; i < n; ++i)
  {
    uint32_t src = chrono_index(&record, offset + i);
    memcpy(bias[i], bias_at(&record,src), sizeof(float) * 3U);
    temperature_c[i] = temperature_at(&record,src);
  }
  if(copied != NULL) *copied = n;
  return 0;
}
