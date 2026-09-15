#include "fusion_settings.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include <string.h>

#define SETTINGS_MAGIC   0x4655534EU
#define SETTINGS_VERSION 2U

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  uint8_t mode;
  uint8_t reserved;
  uint16_t can_node_id;
  uint32_t crc;
} settings_record_t;

static uint32_t crc32(const settings_record_t *r)
{
  const uint8_t *p = (const uint8_t *)r;
  uint32_t c = 0xFFFFFFFFU;
  uint32_t i, b;
  for(i = 0U; i < (uint32_t)(sizeof(*r) - sizeof(r->crc)); ++i)
  {
    c ^= p[i];
    for(b = 0U; b < 8U; ++b)
      c = (c >> 1) ^ (0xEDB88320U & (0U - (c & 1U)));
  }
  return c;
}

static int valid(const settings_record_t *r)
{
  return (r->magic == SETTINGS_MAGIC) &&
         (r->version == SETTINGS_VERSION) &&
         (r->mode <= FUSION_MODE_9AXIS_RELATIVE) &&
         (r->can_node_id <= 0x7FFU) &&
         (r->crc == crc32(r));
}

int fusion_settings_load_ex(fusion_mode_t *mode, uint16_t *can_node_id)
{
  const settings_record_t *r = (const settings_record_t *)APP_FUSION_SETTINGS_ADDR;
  if(!valid(r))
  {
    if(mode != NULL) *mode = FUSION_MODE_9AXIS;
    if(can_node_id != NULL) *can_node_id = APP_CAN_DEFAULT_CAN_ID;
    return -1;
  }
  if(mode != NULL) *mode = (fusion_mode_t)r->mode;
  if(can_node_id != NULL) *can_node_id = r->can_node_id;
  return 0;
}

int fusion_settings_save_ex(fusion_mode_t mode, uint16_t can_node_id)
{
  settings_record_t r;
  flash_status_type st;
  uint32_t i, word;
  if(mode > FUSION_MODE_9AXIS_RELATIVE || can_node_id > 0x7FFU) return -1;
  memset(&r, 0, sizeof(r));
  r.magic = SETTINGS_MAGIC;
  r.version = SETTINGS_VERSION;
  r.sequence = 1U;
  r.mode = (uint8_t)mode;
  r.can_node_id = can_node_id;
  r.crc = crc32(&r);

  flash_unlock();
  st = flash_sector_erase(APP_FUSION_SETTINGS_ADDR);
  if(st == FLASH_OPERATE_DONE)
  {
    for(i = 0U; i < (uint32_t)(sizeof(r) / 4U); ++i)
    {
      memcpy(&word, ((const uint8_t *)&r) + 4U * i, sizeof(word));
      st = flash_word_program(APP_FUSION_SETTINGS_ADDR + 4U * i, word);
      if(st != FLASH_OPERATE_DONE) break;
    }
  }
  flash_lock();
  if(st != FLASH_OPERATE_DONE) return -2;
  return valid((const settings_record_t *)APP_FUSION_SETTINGS_ADDR) ? 0 : -3;
}

int fusion_settings_load(fusion_mode_t *mode)
{
  return fusion_settings_load_ex(mode, NULL);
}

int fusion_settings_save(fusion_mode_t mode)
{
  fusion_mode_t stored_mode;
  uint16_t can_node_id;
  (void)fusion_settings_load_ex(&stored_mode, &can_node_id);
  return fusion_settings_save_ex(mode, can_node_id);
}
