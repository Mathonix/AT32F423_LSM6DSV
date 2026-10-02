#include "gyro_bias_history.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include "test_memory.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SLOT0 APP_GYR_BIAS_FLASH_SLOT0_ADDR
#define SLOT1 APP_GYR_BIAS_FLASH_SLOT1_ADDR
#define SECTOR 0x800U

static int unlocked, fail_erase, fail_program_after = -1;
static uint32_t erase_count;

void flash_unlock(void) { unlocked = 1; }
void flash_lock(void) { unlocked = 0; }
flash_status_type flash_sector_erase(uint32_t address)
{
  assert(unlocked && address >= 0x08000000U && address < 0x08040000U);
  assert((address & (SECTOR - 1U)) == 0U);
  ++erase_count;
  if(fail_erase) return FLASH_PROGRAM_ERROR;
  memset((void *)(uintptr_t)address, 0xFF, SECTOR);
  return FLASH_OPERATE_DONE;
}
flash_status_type flash_word_program(uint32_t address, uint32_t value)
{
  assert(unlocked && (address & 3U) == 0U);
  if(fail_program_after == 0) return FLASH_PROGRAM_ERROR;
  if(fail_program_after > 0) --fail_program_after;
  *(uint32_t *)(uintptr_t)address &= value;
  return FLASH_OPERATE_DONE;
}

typedef struct {
  uint32_t magic, version, sequence, count, next;
  float bias[15][3];
  float temperature_c[15];
  uint32_t crc;
} v2_record_t;

static uint32_t crc_bytes(const void *data, uint32_t n)
{
  const uint8_t *p = data;
  uint32_t c = 0xFFFFFFFFU, i, b;
  for(i = 0; i < n; ++i) {
    c ^= p[i];
    for(b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
  }
  return c;
}

static uint32_t slot_version(uint32_t address)
{
  uint32_t version;
  memcpy(&version, (const void *)(uintptr_t)(address + 4U), 4);
  return version;
}

static void put_v2(uint32_t address, uint32_t sequence, uint32_t count, uint32_t next)
{
  v2_record_t rec;
  uint32_t i;
  memset(&rec, 0, sizeof(rec));
  rec.magic = 0x42494153U;
  rec.version = 2U;
  rec.sequence = sequence;
  rec.count = count;
  rec.next = next;
  for(i = 0; i < count; ++i) {
    rec.bias[i][0] = (float)i;
    rec.bias[i][1] = 0.25f;
    rec.bias[i][2] = -0.5f;
    rec.temperature_c[i] = 20.0f + (float)i;
  }
  rec.crc = crc_bytes(&rec, sizeof(rec) - 4U);
  memcpy((void *)(uintptr_t)address, &rec, sizeof(rec));
}

static int near(float a, float b) { return fabsf(a - b) < 1e-4f; }

static void clear_flash(void)
{
  memset((void *)(uintptr_t)0x08000000U, 0xFF, 0x40000U);
  fail_erase = 0;
  fail_program_after = -1;
}

static void test_empty(void)
{
  float latest[3], average[3];
  uint32_t count = 9U;
  uint8_t corrupt = 1U;
  clear_flash();
  assert(gyro_bias_history_load(latest, average, &count, &corrupt) == 0);
  assert(count == 0U && corrupt == 0U);
}

static void test_capacity_and_drop(void)
{
  float bias[3] = {0.0f, 1.0f, -1.0f};
  float latest[3], nearest[3], nearest_temp;
  uint32_t count = 0U, i;
  uint8_t nearest_valid = 0U, corrupt = 1U;
  clear_flash();
  for(i = 0; i < GYRO_BIAS_HISTORY_MAX; ++i) {
    bias[0] = (float)i;
    assert(gyro_bias_history_save_at_temp(bias, (float)i) == 0);
  }
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 0.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(count == 50U && corrupt == 0U && nearest_valid == 1U);
  assert(near(latest[0], 49.0f) && near(nearest[0], 0.0f) && near(nearest_temp, 0.0f));
  bias[0] = 50.0f;
  assert(gyro_bias_history_save_at_temp(bias, 50.0f) == 0);
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 0.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(count == 50U && near(latest[0], 50.0f) && nearest_valid == 0U);
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 1.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(nearest_valid == 1U && near(nearest[0], 1.0f));
  assert(slot_version(SLOT0) == 3U || slot_version(SLOT1) == 3U);
}

static void test_v2_ring_migrates(void)
{
  float bias[3] = {100.0f, 0.0f, 0.0f};
  float latest[3], nearest[3], nearest_temp;
  uint32_t count = 0U;
  uint8_t nearest_valid = 0U, corrupt = 1U;
  uint32_t erases;
  clear_flash();
  put_v2(SLOT1, 107U, 15U, 2U);
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 21.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(count == 15U && corrupt == 0U);
  assert(near(latest[0], 1.0f) && near(latest[1], 0.25f) && near(latest[2], -0.5f));
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 22.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(nearest_valid && near(nearest[0], 2.0f) && near(nearest_temp, 22.0f));
  erases = erase_count;
  assert(gyro_bias_history_save_at_temp(bias, 90.0f) == 0);
  assert(erase_count == erases + 1U);
  assert(slot_version(SLOT0) == 3U && slot_version(SLOT1) == 2U);
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 90.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(count == 16U && near(latest[0], 100.0f) && nearest_valid);
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 22.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(nearest_valid && near(nearest[0], 2.0f));
  memset((void *)(uintptr_t)SLOT0, 0xFF, SECTOR);
  assert(gyro_bias_history_load(latest, NULL, &count, &corrupt) == 1);
  assert(count == 15U && near(latest[0], 1.0f) && corrupt == 0U);
}

static void test_v2_partial_and_failure(void)
{
  float bias[3] = {7.0f, 0.0f, 0.0f};
  float nan_bias[3] = {0.0f, 0.0f, 0.0f};
  float latest[3], nearest[3], nearest_temp;
  uint32_t count = 0U, erases;
  uint8_t nearest_valid = 0U, corrupt = 0U;
  clear_flash();
  put_v2(SLOT0, 4U, 3U, 3U);
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 22.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(count == 3U && near(latest[0], 2.0f));
  erases = erase_count;
  nan_bias[0] = nanf("");
  assert(gyro_bias_history_save_at_temp(nan_bias, 30.0f) == -1);
  assert(gyro_bias_history_save_at_temp(bias, nanf("")) == -1);
  assert(erase_count == erases);
  fail_erase = 1;
  assert(gyro_bias_history_save_at_temp(bias, 30.0f) == -2);
  fail_erase = 0;
  assert(gyro_bias_history_load(latest, NULL, &count, &corrupt) == 1);
  assert(count == 3U && near(latest[0], 2.0f));
  assert(gyro_bias_history_save_at_temp(bias, 30.0f) == 0);
  assert(gyro_bias_history_load_for_temp(latest, NULL, nearest, 22.0f, 0.0f,
                                          &nearest_temp, &nearest_valid, &count, &corrupt) == 1);
  assert(count == 4U && near(latest[0], 7.0f) && nearest_valid && near(nearest[0], 2.0f));
  fail_program_after = 0;
  assert(gyro_bias_history_save_at_temp(bias, 31.0f) == -3);
  fail_program_after = -1;
  assert(gyro_bias_history_load(latest, NULL, &count, &corrupt) == 1);
  assert(count == 4U && near(latest[0], 7.0f));
}

static void test_corrupt_and_sequence(void)
{
  float latest[3] = {0}, bias[3] = {9.0f, 0.0f, 0.0f};
  uint32_t count = 0U;
  uint8_t corrupt = 0U;
  v2_record_t bad;
  clear_flash();
  memset(&bad, 0, sizeof(bad));
  bad.magic = 0x42494153U;
  bad.version = 2U;
  bad.count = 1U;
  bad.crc = 0U;
  memcpy((void *)(uintptr_t)SLOT0, &bad, sizeof(bad));
  assert(gyro_bias_history_load(latest, NULL, &count, &corrupt) == 0);
  assert(corrupt == 1U && count == 0U);
  put_v2(SLOT1, 10U, 1U, 1U);
  assert(gyro_bias_history_load(latest, NULL, &count, &corrupt) == 1);
  assert(count == 1U && near(latest[0], 0.0f) && corrupt == 1U);
  assert(gyro_bias_history_save_at_temp(bias, 40.0f) == 0);
  assert(gyro_bias_history_load(latest, NULL, &count, &corrupt) == 1);
  assert(count == 2U && near(latest[0], 9.0f));
}

static void read_page(uint32_t offset, uint32_t *copied, uint32_t *count,
                      uint32_t *sequence, uint8_t *version, uint8_t *corrupt,
                      float bias[][3], float *temperature)
{
  assert(gyro_bias_history_read(offset, 3U, bias, temperature, copied, count,
                                sequence, version, corrupt) == 0);
}

static void test_history_pages(void)
{
  float bias[3] = {0.0f, 0.1f, -0.2f};
  float page[3][3], temperature[3];
  uint32_t copied = 9U, count = 9U, sequence = 9U, i, erases;
  uint8_t version = 9U, corrupt = 9U;
  clear_flash();
  read_page(0U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(copied == 0U && count == 0U && sequence == 0U && version == 0U && corrupt == 0U);

  for(i = 0U; i < 4U; ++i) {
    bias[0] = (float)i;
    assert(gyro_bias_history_save_at_temp(bias, 20.0f + (float)i) == 0);
  }
  erases = erase_count;
  read_page(0U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(erase_count == erases);
  assert(copied == 3U && count == 4U && sequence == 4U && version == 3U && corrupt == 0U);
  assert(near(page[0][0], 0.0f) && near(temperature[0], 20.0f));
  assert(near(page[1][0], 1.0f) && near(page[2][0], 2.0f) && near(page[2][1], 0.1f));
  read_page(3U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(copied == 1U && near(page[0][0], 3.0f) && near(temperature[0], 23.0f));
  read_page(4U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(copied == 0U && count == 4U);

  clear_flash();
  for(i = 0U; i < GYRO_BIAS_HISTORY_MAX + 1U; ++i) {
    bias[0] = (float)i;
    assert(gyro_bias_history_save_at_temp(bias, (float)i) == 0);
  }
  read_page(0U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(count == 50U && sequence == 51U && version == 3U && copied == 3U);
  assert(near(page[0][0], 1.0f) && near(page[1][0], 2.0f) && near(page[2][0], 3.0f));
  read_page(48U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(copied == 2U && near(page[0][0], 49.0f) && near(page[1][0], 50.0f));
  assert(near(temperature[1], 50.0f));

  clear_flash();
  erases = erase_count;
  put_v2(SLOT1, 107U, 15U, 2U);
  read_page(0U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(count == 15U && sequence == 107U && version == 2U && corrupt == 0U && copied == 3U);
  assert(near(page[0][0], 2.0f) && near(temperature[0], 22.0f));
  assert(near(page[1][0], 3.0f) && near(page[2][0], 4.0f));
  read_page(12U, &copied, &count, &sequence, &version, &corrupt, page, temperature);
  assert(copied == 3U && near(page[0][0], 14.0f) && near(page[1][0], 0.0f) && near(page[2][0], 1.0f));
  assert(erase_count == erases && slot_version(SLOT1) == 2U);
}

int main(void)
{
  void *flash = test_map_memory(0x08000000U, 0x40000U);
  assert(GYRO_BIAS_HISTORY_MAX == 50U);
  assert(sizeof(v2_record_t) == 264U);
  test_empty();
  test_capacity_and_drop();
  test_v2_ring_migrates();
  test_v2_partial_and_failure();
  test_corrupt_and_sequence();
  test_history_pages();
  test_unmap_memory(flash, 0x40000U);
  puts("gyro bias history: 50-entry ring, v2 migration, failure fallback: OK");
  return 0;
}
