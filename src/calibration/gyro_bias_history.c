#include "gyro_bias_history.h"
#include "app_config.h"
#include "at32f423_flash.h"
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

static void import_v2(const gyro_bias_record_v2_t *src, gyro_bias_record_t *dst)
{
  uint32_t i, n, src_i;
  memset(dst, 0, sizeof(*dst));
  dst->magic = BIAS_MAGIC;
  dst->version = BIAS_VERSION;
  dst->sequence = src->sequence;
  n = src->count;
  dst->count = n;
  if(n == BIAS_V2_MAX)
  {
    for(i = 0U; i < n; ++i)
    {
      src_i = (src->next + i) % BIAS_V2_MAX;
      memcpy(dst->bias[i], src->bias[src_i], sizeof(float) * 3U);
      dst->temperature_c[i] = src->temperature_c[src_i];
    }
    dst->next = n;
  }
  else
  {
    for(i = 0U; i < n; ++i)
    {
      memcpy(dst->bias[i], src->bias[i], sizeof(float) * 3U);
      dst->temperature_c[i] = src->temperature_c[i];
    }
    dst->next = n;
  }
}

static int accept_slot(uint32_t address, gyro_bias_record_t *out, int *valid, int *corrupt,
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
    gyro_bias_record_t rec;
    memcpy(&rec, (const void *)(uintptr_t)address, sizeof(rec));
    if(v3_valid(&rec))
    {
      *out = rec;
      *valid = 1;
      if(flash_version != NULL) *flash_version = (uint8_t)BIAS_VERSION;
      return 1;
    }
  }
  else if(version == BIAS_VERSION_V2)
  {
    gyro_bias_record_v2_t rec;
    memcpy(&rec, (const void *)(uintptr_t)address, sizeof(rec));
    if(v2_valid(&rec))
    {
      import_v2(&rec, out);
      *valid = 1;
      if(flash_version != NULL) *flash_version = (uint8_t)BIAS_VERSION_V2;
      return 1;
    }
  }
  *corrupt = 1;
  return 0;
}

static int newer(const gyro_bias_record_t *a, const gyro_bias_record_t *b)
{
  return (int32_t)(a->sequence - b->sequence) > 0;
}

static int load_records(gyro_bias_record_t *best, uint8_t *any_corrupt, uint8_t *flash_version)
{
  gyro_bias_record_t a, b;
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
    if(flash_version != NULL) *flash_version = ver_a;
  }
  else
  {
    *best = b;
    if(flash_version != NULL) *flash_version = ver_b;
  }
  return 1;
}

static uint32_t chrono_index(const gyro_bias_record_t *record, uint32_t logical)
{
  uint32_t base = (record->count < GYRO_BIAS_HISTORY_MAX) ? 0U : record->next;
  return (base + logical) % GYRO_BIAS_HISTORY_MAX;
}

static uint32_t latest_index(const gyro_bias_record_t *record)
{
  return (record->count < GYRO_BIAS_HISTORY_MAX) ? (record->count - 1U) :
         ((record->next + GYRO_BIAS_HISTORY_MAX - 1U) % GYRO_BIAS_HISTORY_MAX);
}

int gyro_bias_history_load_for_temp(float latest[3], float average[3],
                                    float nearest[3], float current_temp_c,
                                    float temp_window_c, float *nearest_temp_c,
                                    uint8_t *nearest_valid, uint32_t *count,
                                    uint8_t *corrupt)
{
  gyro_bias_record_t record;
  uint32_t i, j, n, li, near_n = 0U;
  float temp_sum = 0.0f;
  if(latest != NULL) memset(latest, 0, sizeof(float) * 3U);
  if(average != NULL) memset(average, 0, sizeof(float) * 3U);
  if(nearest != NULL) memset(nearest, 0, sizeof(float) * 3U);
  if(nearest_temp_c != NULL) *nearest_temp_c = 0.0f;
  if(nearest_valid != NULL) *nearest_valid = 0U;
  if(count != NULL) *count = 0U;
  if(corrupt != NULL) *corrupt = 0U;
  if(!load_records(&record, corrupt, NULL) || record.count == 0U) return 0;

  n = record.count;
  li = latest_index(&record);
  for(i = 0U; i < 3U; ++i)
  {
    if(latest != NULL) latest[i] = record.bias[li][i];
    if(average != NULL)
      for(j = 0U; j < n; ++j) average[i] += record.bias[j][i] / (float)n;
  }
  for(j = 0U; j < n; ++j)
  {
    if(fabsf(record.temperature_c[j] - current_temp_c) <= temp_window_c)
    {
      near_n++;
      temp_sum += record.temperature_c[j];
      if(nearest != NULL)
        for(i = 0U; i < 3U; ++i) nearest[i] += record.bias[j][i];
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

int gyro_bias_history_save_at_temp(const float bias[3], float temperature_c)
{
  gyro_bias_record_t old_record, next_record, verify_record;
  uint32_t old_addr, new_addr, i;
  flash_status_type status;
  if(bias == NULL || !isfinite(bias[0]) || !isfinite(bias[1]) ||
     !isfinite(bias[2]) || !isfinite(temperature_c)) return -1;

  if(load_records(&old_record, NULL, NULL))
  {
    old_addr = (old_record.sequence & 1U) ? BIAS_SLOT1_ADDR : BIAS_SLOT0_ADDR;
    new_addr = (old_addr == BIAS_SLOT0_ADDR) ? BIAS_SLOT1_ADDR : BIAS_SLOT0_ADDR;
    next_record = old_record;
    next_record.sequence++;
    if(next_record.count < GYRO_BIAS_HISTORY_MAX)
    {
      next_record.bias[next_record.count][0] = bias[0];
      next_record.bias[next_record.count][1] = bias[1];
      next_record.bias[next_record.count][2] = bias[2];
      next_record.temperature_c[next_record.count] = temperature_c;
      next_record.count++;
      next_record.next = next_record.count % GYRO_BIAS_HISTORY_MAX;
    }
    else
    {
      next_record.bias[next_record.next][0] = bias[0];
      next_record.bias[next_record.next][1] = bias[1];
      next_record.bias[next_record.next][2] = bias[2];
      next_record.temperature_c[next_record.next] = temperature_c;
      next_record.next = (next_record.next + 1U) % GYRO_BIAS_HISTORY_MAX;
    }
  }
  else
  {
    old_addr = BIAS_SLOT0_ADDR;
    new_addr = BIAS_SLOT0_ADDR;
    memset(&next_record, 0, sizeof(next_record));
    next_record.magic = BIAS_MAGIC;
    next_record.version = BIAS_VERSION;
    next_record.sequence = 1U;
    next_record.count = 1U;
    next_record.next = 1U;
    memcpy(next_record.bias[0], bias, sizeof(float) * 3U);
    next_record.temperature_c[0] = temperature_c;
  }
  (void)old_addr;
  next_record.magic = BIAS_MAGIC;
  next_record.version = BIAS_VERSION;
  next_record.crc = bias_crc_bytes((const uint8_t *)&next_record,
                                   (uint32_t)(sizeof(next_record) - sizeof(next_record.crc)));

  flash_unlock();
  status = flash_sector_erase(new_addr);
  if(status != FLASH_OPERATE_DONE) { flash_lock(); return -2; }
  for(i = 0U; i < (uint32_t)(sizeof(next_record) / 4U); ++i)
  {
    uint32_t word;
    memcpy(&word, ((const uint8_t *)&next_record) + i * 4U, sizeof(word));
    status = flash_word_program(new_addr + i * 4U, word);
    if(status != FLASH_OPERATE_DONE) { flash_lock(); return -3; }
  }
  flash_lock();
  memcpy(&verify_record, (const void *)(uintptr_t)new_addr, sizeof(verify_record));
  return v3_valid(&verify_record) ? 0 : -4;
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
  gyro_bias_record_t record;
  uint8_t version = 0U;
  uint32_t n = 0U;
  uint32_t i;
  if(copied != NULL) *copied = 0U;
  if(count != NULL) *count = 0U;
  if(sequence != NULL) *sequence = 0U;
  if(record_version != NULL) *record_version = 0U;
  if(corrupt != NULL) *corrupt = 0U;
  if(!load_records(&record, corrupt, &version) || record.count == 0U) return 0;
  if(count != NULL) *count = record.count;
  if(sequence != NULL) *sequence = record.sequence;
  if(record_version != NULL) *record_version = version;
  if(offset >= record.count || limit == 0U || bias == NULL || temperature_c == NULL) return 0;
  n = record.count - offset;
  if(n > limit) n = limit;
  for(i = 0U; i < n; ++i)
  {
    uint32_t src = chrono_index(&record, offset + i);
    memcpy(bias[i], record.bias[src], sizeof(float) * 3U);
    temperature_c[i] = record.temperature_c[src];
  }
  if(copied != NULL) *copied = n;
  return 0;
}
