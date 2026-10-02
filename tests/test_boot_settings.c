/* Compile real bootloader/settings code with an in-process mock Flash. */
#include "boot_config.h"
#include "bl_protocol.h"
#include "fusion_settings.h"
#include "gyro_range.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include "test_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t erase_count, program_count, reply_count;
static int unlocked, fail_erase, fail_program_after = -1;
static uint8_t last_status;
static uint32_t last_value;
static bl_io_port_t last_port;

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
int bl_io_write_to(bl_io_port_t destination, const uint8_t *p, uint16_t n)
{
  uint32_t crc;
  assert(n == 14U && p[0] == 'B' && p[1] == 'L' && (p[3] & BL_CMD_ACK));
  memcpy(&crc, p + 10, 4);
  assert(bl_crc32(p, 10) == crc);
  last_status = p[4];
  last_port = destination;
  memcpy(&last_value, p + 6, 4);
  ++reply_count;
  return 0;
}
static void send_frame_from(bl_io_port_t source, uint8_t cmd, uint32_t addr,
                            uint32_t len, uint32_t crc, const uint8_t *payload)
{
  uint8_t frame[18U + BL_MAX_CHUNK] = {'B', 'L', 1U, 0U, 0U, 0U};
  uint32_t before = reply_count;
  frame[3] = cmd;
  memcpy(frame + 6, &addr, 4); memcpy(frame + 10, &len, 4); memcpy(frame + 14, &crc, 4);
  uint32_t n = 18;
  if(cmd == BL_CMD_DATA) { assert(len <= BL_MAX_CHUNK); memcpy(frame + n, payload, len); n += len; }
  for(uint32_t i = 0; i < n; ++i) bl_protocol_feed_from(source, frame[i]);
  assert(reply_count == before + 1);
  assert(last_port == source);
  assert(!unlocked);
}
static void send_frame(uint8_t cmd, uint32_t addr, uint32_t len,
                       uint32_t crc, const uint8_t *payload)
{
  send_frame_from(BL_IO_UART, cmd, addr, len, crc, payload);
}

static void test_ports(void)
{
  uint8_t hello[18] = {'B', 'L', 1U, BL_CMD_HELLO};
  bl_protocol_reset();
  uint32_t before = reply_count;
  /* Neither half of an interleaved pair can become a frame on the other port. */
  for(uint32_t i = 0; i < 9; ++i)
  {
    bl_protocol_feed_from(BL_IO_UART, hello[i]);
    bl_protocol_feed_from(BL_IO_USB, hello[i]);
  }
  assert(reply_count == before);
  for(uint32_t i = 9; i < sizeof(hello); ++i) bl_protocol_feed_from(BL_IO_UART, hello[i]);
  assert(reply_count == before + 1 && last_port == BL_IO_UART);
  for(uint32_t i = 9; i < sizeof(hello); ++i) bl_protocol_feed_from(BL_IO_USB, hello[i]);
  assert(reply_count == before + 2 && last_port == BL_IO_USB);
  bl_protocol_feed_from((bl_io_port_t)255, 'B');

  uint8_t image[20] = {0};
  uint32_t vectors[2] = {0x2000BFF0U, BL_APP_BASE + 9U};
  memcpy(image, vectors, sizeof(vectors));
  uint32_t crc = bl_crc32(image, sizeof(image));
  send_frame_from(BL_IO_USB, BL_CMD_BEGIN, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_OK);
  uint32_t erased = erase_count, programmed = program_count;
  send_frame(BL_CMD_HELLO, 0, 0, 0, NULL);
  assert(last_status == BL_ST_OK && last_value == BL_APP_BASE);
  send_frame(BL_CMD_BEGIN, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_BUSY && erase_count == erased);
  send_frame(BL_CMD_DATA, BL_APP_BASE, sizeof(image), crc, image);
  assert(last_status == BL_ST_BUSY && program_count == programmed);
  send_frame(BL_CMD_END, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_BUSY);
  send_frame(BL_CMD_ABORT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_BUSY);
  send_frame(BL_CMD_BOOT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_BUSY && !bl_protocol_boot_requested());

  send_frame_from(BL_IO_USB, BL_CMD_DATA, BL_APP_BASE, sizeof(image), crc, image);
  assert(last_status == BL_ST_OK && last_value == sizeof(image));
  send_frame_from(BL_IO_USB, BL_CMD_END, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_OK && bl_protocol_can_boot());
  send_frame(BL_CMD_BOOT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_BUSY && !bl_protocol_boot_requested());
  send_frame_from(BL_IO_USB, BL_CMD_ABORT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_OK);
  send_frame(BL_CMD_BEGIN, BL_APP_BASE, sizeof(image), crc, NULL);
  assert(last_status == BL_ST_OK && erase_count > erased);
  send_frame(BL_CMD_ABORT, 0, 0, 0, NULL);
  assert(last_status == BL_ST_OK && !bl_protocol_can_boot());
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
  test_vectors(); test_upgrade(); test_ports(); test_settings();
  device_settings_t saved, loaded;
  assert(device_settings_load(&saved) == 0);
  saved.fast_start = 1;
  saved.outputs[0] = (output_config_t){OUTPUT_FORMAT_JUSTFLOAT, 0, 1};
  saved.outputs[1] = (output_config_t){OUTPUT_FORMAT_CUSTOM, 0, 0x1FF};
  assert(device_settings_save(&saved) == 0);
  assert(device_settings_load(&loaded) == 0);
  assert(loaded.fast_start == 1 && loaded.outputs[0].field_mask == 1 && loaded.outputs[1].field_mask == 0x1FF);
  assert(fusion_settings_save_ex(FUSION_MODE_9AXIS, 0x234) == 0);
  assert(device_settings_load(&loaded) == 0 && loaded.fast_start == 1 && loaded.outputs[1].format == OUTPUT_FORMAT_CUSTOM);
  uint32_t before = erase_count;
  assert(device_settings_save(&loaded) == 0 && erase_count == before);
  saved = loaded; saved.fast_start = 0;
  fail_program_after = 4;
  assert(device_settings_save(&saved) != 0);
  assert(device_settings_load(&loaded) == 0 && loaded.fast_start == 1);
  fail_program_after = -1;
  /* A real V2 record migrates without losing mode/CAN ID; a failed V3 write
   * leaves the V2 record usable in its original slot. */
  memset((void *)(uintptr_t)0x0803D000U, 0xFF, 0x1000);
  uint32_t v2[5] = {0x4655534E, 2, 99, (0x321U << 16) | FUSION_MODE_9AXIS_RELATIVE, 0};
  v2[4] = ~bl_crc32((uint8_t *)v2, 16);
  memcpy((void *)(uintptr_t)0x0803D800U, v2, sizeof(v2));
  assert(device_settings_load(&saved) == 0 && saved.mode == FUSION_MODE_9AXIS_RELATIVE && saved.can_node_id == 0x321);
  assert(saved.outputs[0].format == OUTPUT_FORMAT_LEGACY);
  saved.fast_start = 1;
  saved.outputs[1] = (output_config_t){OUTPUT_FORMAT_CUSTOM, 0, 0x1FF};
  fail_program_after = 4;
  assert(device_settings_save(&saved) != 0);
  assert(device_settings_load(&loaded) == 0 && loaded.outputs[1].format == OUTPUT_FORMAT_LEGACY);
  fail_program_after = -1;
  assert(device_settings_save(&saved) == 0);
  assert(device_settings_load(&loaded) == 0 && loaded.mode == saved.mode && loaded.outputs[1].field_mask == 0x1FF);
  /* V3 keeps startup and serial selection when CAN settings are introduced. */
  memset((void *)(uintptr_t)0x0803D000U, 0xFF, 0x1000);
  uint8_t v3[28] = {0};
  uint32_t header[3] = {0x4655534E, 3, 101};
  memcpy(v3, header, 12); v3[12] = 0; v3[13] = 1;
  uint16_t old_id = 0x456; memcpy(v3 + 14, &old_id, 2);
  output_config_t outputs[2] = {{0, 0, 65}, {1, 0, 511}};
  memcpy(v3 + 16, outputs, 8);
  uint32_t old_crc = ~bl_crc32(v3, 24); memcpy(v3 + 24, &old_crc, 4);
  memcpy((void *)(uintptr_t)0x0803D800U, v3, 28);
  assert(device_settings_load(&saved) == 0);
  assert(saved.mode == 0 && saved.fast_start == 1 && saved.can.node_id == 0x456 && saved.outputs[0].field_mask == 65);
  saved.can.master_id = 0x123; saved.can.period_ms = 30; saved.can.baud_index = 7; saved.can.output_mask = 15;
  fail_program_after = 8;
  assert(device_settings_save(&saved) != 0);
  assert(device_settings_load(&loaded) == 0 && loaded.can.master_id == 0x6FF && loaded.outputs[1].field_mask == 511);
  fail_program_after = -1;
  assert(device_settings_save(&saved) == 0 && device_settings_load(&loaded) == 0);
  assert(loaded.can.master_id == 0x123 && loaded.can.baud_index == 7 && loaded.can.output_mask == 15);
  before = erase_count; assert(device_settings_save(&loaded) == 0 && erase_count == before);
  assert(fusion_settings_save_ex(FUSION_MODE_9AXIS, 0x345) == 0);
  assert(device_settings_load(&loaded) == 0 && loaded.can.node_id == 0x345 && loaded.can.master_id == 0x123);
  /* V4's two padding bytes are not a duration. Migrate to a 2-second default
   * without losing startup, CAN or independent serial output settings. */
  uint8_t v4[40] = {0};
  uint32_t v4_header[3] = {0x4655534E, 4, 200};
  memcpy(v4, v4_header, 12); v4[12] = loaded.mode; v4[13] = loaded.fast_start;
  memcpy(v4 + 14, &loaded.can_node_id, 2);
  memcpy(v4 + 16, loaded.outputs, 8); memcpy(v4 + 24, &loaded.can, 10);
  v4[34] = 0xFF; v4[35] = 0xFF;
  uint32_t v4_crc = ~bl_crc32(v4, 36); memcpy(v4 + 36, &v4_crc, 4);
  memset((void *)(uintptr_t)0x0803D000U, 0xFF, 0x1000);
  memcpy((void *)(uintptr_t)0x0803D800U, v4, 40);
  assert(device_settings_load(&saved) == 0 && saved.gyro_init_ms == 2000);
  assert(saved.can.master_id == loaded.can.master_id && saved.outputs[1].field_mask == loaded.outputs[1].field_mask);
  saved.fast_start = 0; saved.gyro_init_ms = 2500;
  fail_program_after = 9; assert(device_settings_save(&saved) != 0);
  assert(device_settings_load(&loaded) == 0 && loaded.gyro_init_ms == 2000);
  fail_program_after = -1;
  assert(device_settings_save(&saved) == 0 && device_settings_load(&loaded) == 0);
  assert(!loaded.fast_start && loaded.gyro_init_ms == 2500);
  before = erase_count; assert(device_settings_save(&loaded) == 0 && erase_count == before);
  saved = loaded; saved.gyro_init_ms = 0;
  assert(device_settings_save(&saved) != 0 && erase_count == before);
  saved.gyro_init_ms = 60001; assert(device_settings_save(&saved) != 0);
  assert(fusion_settings_save_ex(FUSION_MODE_6AXIS, 0x346) == 0);
  assert(device_settings_load(&loaded) == 0 && loaded.gyro_init_ms == 2500 && loaded.can.master_id == 0x123);
  /* V5 migration keeps existing choices; interrupted V6 writes fall back. */
  uint8_t v5[40]; memcpy(v5,v4,40);
  uint32_t v5_version=5, v5_seq=300; uint16_t duration=3500;
  memcpy(v5+4,&v5_version,4);memcpy(v5+8,&v5_seq,4);memcpy(v5+34,&duration,2);
  uint32_t v5_crc=~bl_crc32(v5,36);memcpy(v5+36,&v5_crc,4);
  memset((void *)(uintptr_t)0x0803D000U,0xFF,0x1000);memcpy((void *)(uintptr_t)0x0803D800U,v5,40);
  assert(device_settings_load(&saved)==0 && saved.gyro_init_ms==3500 && saved.gyro_range_dps==1000 && saved.output_hz==APP_VOFA_OUTPUT_HZ);
  uint16_t ranges[]={125,250,500,1000,2000,4000};
  uint8_t registers[]={0,1,2,3,4,12};
  for(unsigned i=0;i<6;i++) {
    assert(gyro_range_register(ranges[i])==registers[i]);
    assert(gyro_range_dps_per_lsb(ranges[i])>0);
    saved.gyro_range_dps=ranges[i];saved.output_hz=500;
    assert(device_settings_save(&saved)==0 && device_settings_load(&loaded)==0);
    assert(loaded.gyro_range_dps==ranges[i] && loaded.output_hz==500 && loaded.gyro_init_ms==3500);
    assert(loaded.can.master_id==saved.can.master_id && loaded.outputs[1].field_mask==saved.outputs[1].field_mask);
  }
  saved.output_hz=250;fail_program_after=10;
  assert(device_settings_save(&saved)!=0 && device_settings_load(&loaded)==0 && loaded.output_hz==500);
  fail_program_after=-1;assert(device_settings_save(&saved)==0);
  before=erase_count;assert(device_settings_save(&saved)==0 && erase_count==before);
  saved.gyro_range_dps=750;assert(device_settings_save(&saved)!=0);
  saved.gyro_range_dps=1000;saved.output_hz=300;assert(device_settings_save(&saved)!=0);
  saved.output_hz=0;assert(device_settings_save(&saved)!=0);
  assert(device_settings_load(&loaded)==0 && loaded.gyro_range_dps==4000 && loaded.output_hz==250);
  /* Real V6 bytes migrate without interpreting its CRC as a profile. */
  uint8_t v6[44]={0};uint32_t v6_header[3]={0x4655534E,6,400};
  memcpy(v6,v6_header,12);v6[12]=loaded.mode;v6[13]=loaded.fast_start;
  memcpy(v6+14,&loaded.can_node_id,2);memcpy(v6+16,loaded.outputs,8);memcpy(v6+24,&loaded.can,10);
  memcpy(v6+34,&loaded.gyro_init_ms,2);memcpy(v6+36,&loaded.gyro_range_dps,2);memcpy(v6+38,&loaded.output_hz,2);
  uint32_t v6_crc=~bl_crc32(v6,40);memcpy(v6+40,&v6_crc,4);
  memset((void *)(uintptr_t)0x0803D000U,0xFF,0x1000);memcpy((void *)(uintptr_t)0x0803D800U,v6,44);
  assert(device_settings_load(&saved)==0 && saved.filter_profile==1 && saved.output_hz==250 && saved.gyro_range_dps==4000);
  assert(saved.zaru.enter_dps==APP_ZARU_ENTER_DPS && saved.zaru.exit_dps==APP_ZARU_EXIT_DPS);
  assert(saved.zaru.acc_dev_ms2==APP_ZARU_ACC_DEV_MS2 && saved.zaru.enter_filter_ms==APP_ZARU_ENTER_FILTER_MS);
  assert(saved.zaru.enter_confirm_ms==APP_ZARU_ENTER_CONFIRM_MS && saved.zaru.exit_confirm_ms==APP_ZARU_EXIT_CONFIRM_MS);
  {
    uint8_t v7[48]={0}; uint32_t v7_header[3]={0x4655534E,7,401}; uint32_t v7_profile=1;
    memcpy(v7,v7_header,12); v7[12]=saved.mode; v7[13]=saved.fast_start;
    memcpy(v7+14,&saved.can_node_id,2); memcpy(v7+16,saved.outputs,8); memcpy(v7+24,&saved.can,10);
    memcpy(v7+34,&saved.gyro_init_ms,2); memcpy(v7+36,&saved.gyro_range_dps,2); memcpy(v7+38,&saved.output_hz,2);
    memcpy(v7+40,&v7_profile,4);
    uint32_t v7_crc=~bl_crc32(v7,44); memcpy(v7+44,&v7_crc,4);
    memset((void *)(uintptr_t)0x0803D000U,0xFF,0x1000); memcpy((void *)(uintptr_t)0x0803D800U,v7,48);
  }
  assert(device_settings_load(&saved)==0 && saved.filter_profile==1 && saved.gyro_range_dps==4000 && saved.output_hz==250);
  assert(saved.zaru.enter_dps==APP_ZARU_ENTER_DPS && saved.zaru.exit_confirm_ms==APP_ZARU_EXIT_CONFIRM_MS);
  saved.filter_profile=2;fail_program_after=11;
  assert(device_settings_save(&saved)!=0 && device_settings_load(&loaded)==0 && loaded.filter_profile==1);
  fail_program_after=-1;assert(device_settings_save(&saved)==0 && device_settings_load(&loaded)==0 && loaded.filter_profile==2);
  before=erase_count;assert(device_settings_save(&saved)==0 && erase_count==before);
  saved.filter_profile=3;assert(device_settings_save(&saved)==0 && device_settings_load(&loaded)==0 && loaded.filter_profile==3);
  before=erase_count;saved.filter_profile=4;assert(device_settings_save(&saved)!=0 && erase_count==before);
  assert(device_settings_load(&loaded)==0 && loaded.filter_profile==3);
  loaded.zaru.enter_dps=0.25f; loaded.zaru.exit_dps=0.90f; loaded.zaru.acc_dev_ms2=0.20f;
  loaded.zaru.enter_filter_ms=20; loaded.zaru.enter_confirm_ms=80; loaded.zaru.exit_confirm_ms=5;
  assert(device_settings_save(&loaded)==0 && device_settings_load(&saved)==0);
  assert(saved.filter_profile==3 && saved.gyro_range_dps==4000 && saved.output_hz==250);
  assert(saved.zaru.enter_dps==0.25f && saved.zaru.exit_dps==0.90f && saved.zaru.acc_dev_ms2==0.20f);
  assert(saved.zaru.enter_filter_ms==20 && saved.zaru.enter_confirm_ms==80 && saved.zaru.exit_confirm_ms==5);
  before=erase_count; assert(device_settings_save(&saved)==0 && erase_count==before);
  saved.zaru.exit_dps=0.20f; assert(device_settings_save(&saved)!=0 && erase_count==before);
  assert(device_settings_load(&loaded)==0 && loaded.zaru.enter_dps==0.25f && loaded.zaru.exit_dps==0.90f);
  test_unmap_memory(flash, 0x40000U);
  puts("boot vectors, upgrade framing/state, per-port ACK/parser/owner, flash idempotence/power-loss fallback: OK");
  return 0;
}
