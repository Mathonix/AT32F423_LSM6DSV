/* Compile real bootloader/settings code with an in-process mock Flash. */
#include "boot_config.h"
#include "bl_protocol.h"
#include "fusion_settings.h"
#include "at32f423_flash.h"
#include "test_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t erase_count, program_count, reply_count;
static int unlocked, fail_erase, fail_program_after = -1;
static uint8_t last_status;
static uint32_t last_value;

void flash_unlock(void) { unlocked = 1; }
void flash_lock(void) { unlocked = 0; }
flash_status_type flash_sector_erase(uint32_t address)
{
  assert(unlocked && address >= BL_APP_BASE && address < 0x08040000U);
  assert((address & (BL_SECTOR_SIZE - 1U)) == 0U);
  ++erase_count;
  if(fail_erase) return FLASH_PROGRAM_ERROR;
  memset((void *)(uintptr_t)address, 0xFF, BL_SECTOR_SIZE);
  return FLASH_OPERATE_DONE;
}
flash_status_type flash_word_program(uint32_t address, uint32_t value)
{
  assert(unlocked && address >= BL_APP_BASE && address <= 0x0803FFFCU);
  assert((address & 3U) == 0U);
  if(fail_program_after == 0) return FLASH_PROGRAM_ERROR;
  if(fail_program_after > 0) --fail_program_after;
  ++program_count;
  *(uint32_t *)(uintptr_t)address &= value;
  return FLASH_OPERATE_DONE;
}
int bl_io_write(const uint8_t *p, uint16_t n)
{
  uint32_t crc;
  assert(n == 14U && p[0] == 'B' && p[1] == 'L' && (p[3] & BL_CMD_ACK));
  memcpy(&crc, p + 10, 4);
  assert(bl_crc32(p, 10) == crc);
  last_status = p[4];
  memcpy(&last_value, p + 6, 4);
  ++reply_count;
  return 0;
}
static void send_frame(uint8_t cmd, uint32_t addr, uint32_t len,
                       uint32_t crc, const uint8_t *payload)
{
  uint8_t frame[18U + BL_MAX_CHUNK] = {'B', 'L', 1U, 0U, 0U, 0U};
  uint32_t before = reply_count;
  frame[3] = cmd;
  memcpy(frame + 6, &addr, 4); memcpy(frame + 10, &len, 4); memcpy(frame + 14, &crc, 4);
  uint32_t n = 18;
  if(cmd == BL_CMD_DATA) { assert(len <= BL_MAX_CHUNK); memcpy(frame + n, payload, len); n += len; }
  for(uint32_t i = 0; i < n; ++i) bl_protocol_feed(frame[i]);
  assert(reply_count == before + 1);
  assert(!unlocked);
}
static void test_vectors(void)
{
  const uint32_t pc = BL_APP_BASE + 0x101;
  assert(bl_app_vectors_valid(0x2000BFF0U, pc));
  assert(bl_app_vectors_valid(0x2000C000U, pc));
  assert(!bl_app_vectors_valid(0x20000000U, pc));
  assert(!bl_app_vectors_valid(0x2000C008U, pc));
  assert(!bl_app_vectors_valid(0x20010000U, pc));
  assert(!bl_app_vectors_valid(0x2000BFF1U, pc));
  assert(!bl_app_vectors_valid(0x2000BFF0U, pc & ~1U));
  assert(!bl_app_vectors_valid(0x2000BFF0U, BL_APP_END | 1U));
}
static void test_upgrade(void)
{
  uint8_t image[601];
  uint32_t vectors[2] = {0x2000BFF0U, BL_APP_BASE + 0x101U};
  memset(image, 0x5A, sizeof(image)); memcpy(image, vectors, sizeof(vectors));
  uint32_t crc = bl_crc32(image, sizeof(image));
  bl_protocol_reset();
  assert(!bl_protocol_can_boot());
  send_frame(BL_CMD_BEGIN, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_OK); /* length > 256, no BEGIN payload */
  send_frame(BL_CMD_DATA, BL_APP_BASE, 3, bl_crc32(image, 3), image);
  assert(last_status == BL_ST_BAD_PARAM && last_value == 0);
  send_frame(BL_CMD_DATA, BL_APP_BASE, 256, 0, image);
  assert(last_status == BL_ST_CRC);
  for(uint32_t off = 0; off < sizeof(image); off += BL_MAX_CHUNK)
  {
    uint32_t n = sizeof(image) - off;
    if(n > BL_MAX_CHUNK) n = BL_MAX_CHUNK;
    send_frame(BL_CMD_DATA, BL_APP_BASE + off, n, bl_crc32(image + off, n), image + off);
    assert(last_status == BL_ST_OK && last_value == off + n);
    assert(!bl_protocol_can_boot());
  }
  send_frame(BL_CMD_BOOT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_BUSY && !bl_protocol_boot_requested());
  *(uint8_t *)(uintptr_t)(BL_APP_BASE + 16U) ^= 1U;
  send_frame(BL_CMD_END, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_CRC && !bl_protocol_can_boot());
  *(uint8_t *)(uintptr_t)(BL_APP_BASE + 16U) ^= 1U;
  send_frame(BL_CMD_END, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_OK && bl_protocol_can_boot());
  assert(memcmp((void *)(uintptr_t)BL_APP_BASE, image, sizeof(image)) == 0);
  assert(*(uint8_t *)(uintptr_t)(BL_APP_BASE + sizeof(image)) == 0xFF);
  send_frame(BL_CMD_BOOT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_OK && bl_protocol_boot_requested());

  fail_erase = 1;
  send_frame(BL_CMD_BEGIN, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_FLASH && !bl_protocol_can_boot());
  fail_erase = 0;
  send_frame(BL_CMD_BEGIN, BL_APP_BASE, sizeof(image), crc, NULL);
  send_frame(BL_CMD_DATA, BL_APP_BASE, 256, bl_crc32(image, 256), image);
  send_frame(BL_CMD_ABORT, 0, 0, 0, NULL);
  assert(!bl_protocol_can_boot());
  send_frame(BL_CMD_BOOT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_NO_APP);

  /* A valid SRAM/Thumb vector is insufficient if its code was not uploaded. */
  vectors[1] = BL_APP_BASE + 0x1001U;
  memcpy(image, vectors, sizeof(vectors));
  crc = bl_crc32(image, 16);
  send_frame(BL_CMD_BEGIN, BL_APP_BASE, 16, crc, NULL);
  send_frame(BL_CMD_DATA, BL_APP_BASE, 16, crc, image);
  send_frame(BL_CMD_END, BL_APP_BASE, 16, crc, NULL);
  assert(last_status == BL_ST_NO_APP && !bl_protocol_can_boot());
}
static void test_settings(void)
{
  fusion_mode_t mode;
  uint16_t id;
  assert(fusion_settings_save_ex(FUSION_MODE_6AXIS, 0x123) == 0);
  uint32_t before = erase_count, programs = program_count;
  for(int i = 0; i < 100; ++i)
    assert(fusion_settings_save_ex(FUSION_MODE_6AXIS, 0x123) == 0);
  assert(erase_count == before && program_count == programs);
  assert(fusion_settings_save_ex(FUSION_MODE_9AXIS, 0x456) == 0);
  assert(erase_count == before + 1);
  fail_program_after = 2;
  assert(fusion_settings_save_ex(FUSION_MODE_6AXIS, 0x111) != 0);
  assert(fusion_settings_load_ex(&mode, &id) == 0);
  assert(mode == FUSION_MODE_9AXIS && id == 0x456);
  fail_program_after = -1;
  assert(fusion_settings_save_ex(FUSION_MODE_6AXIS, 0x111) == 0);
  assert(fusion_settings_load_ex(&mode, &id) == 0);
  assert(mode == FUSION_MODE_6AXIS && id == 0x111);
  assert(fusion_settings_save_ex((fusion_mode_t)-1, 1) != 0);
  assert(fusion_settings_save_ex(FUSION_MODE_6AXIS, 0x800) != 0);
}
int main(void)
{
  void *flash = test_map_memory(0x08000000U, 0x40000U);
  memset(flash, 0xFF, 0x40000U);
  test_vectors(); test_upgrade(); test_settings();
  test_unmap_memory(flash, 0x40000U);
  puts("boot vectors, upgrade framing/state, flash idempotence/power-loss fallback: OK");
  return 0;
}
