#include "bl_protocol.h"
#include "bl_io.h"
#include "boot_config.h"
#include "at32f423_flash.h"
#include <string.h>

#define BL_HEADER_SIZE 18U
#define BL_FRAME_SIZE (BL_HEADER_SIZE + BL_MAX_CHUNK)

static uint8_t rx[BL_FRAME_SIZE];
static uint16_t rxn;
static uint32_t image_len, image_crc, image_next;
static uint8_t active;

uint32_t bl_crc32(const uint8_t *p, uint32_t n)
{
  uint32_t c = 0xFFFFFFFFU;
  while(n--)
  {
    c ^= *p++;
    for(uint8_t i = 0U; i < 8U; ++i)
      c = (c >> 1) ^ ((c & 1U) ? 0xEDB88320U : 0U);
  }
  return ~c;
}

static void reply(uint8_t cmd, uint8_t status, uint32_t value)
{
  uint8_t b[14];
  uint32_t c;
  b[0] = BL_MAGIC0; b[1] = BL_MAGIC1; b[2] = BL_PROTOCOL_VERSION;
  b[3] = (uint8_t)(cmd | BL_CMD_ACK); b[4] = status; b[5] = 0U;
  b[6] = (uint8_t)value; b[7] = (uint8_t)(value >> 8);
  b[8] = (uint8_t)(value >> 16); b[9] = (uint8_t)(value >> 24);
  c = bl_crc32(b, 10U);
  memcpy(&b[10], &c, sizeof(c));
  (void)bl_io_write(b, sizeof(b));
}

static int valid_app(void)
{
  uint32_t sp = *(const uint32_t *)BL_APP_BASE;
  uint32_t pc = *(const uint32_t *)(BL_APP_BASE + 4U);
  return ((sp & 0x2FFE0000U) == 0x20000000U &&
          pc >= BL_APP_BASE && pc < BL_APP_END && (pc & 1U));
}

static int erase_app(void)
{
  uint32_t a;
  flash_unlock();
  for(a = BL_APP_BASE; a < BL_APP_END; a += BL_SECTOR_SIZE)
  {
    if(flash_sector_erase(a) != FLASH_OPERATE_DONE)
    {
      flash_lock();
      return 0;
    }
  }
  flash_lock();
  return 1;
}

static int program_words(uint32_t addr, const uint8_t *p, uint32_t n)
{
  flash_status_type st = FLASH_OPERATE_DONE;
  flash_unlock();
  for(uint32_t i = 0U; i < n; i += 4U)
  {
    uint32_t word = 0xFFFFFFFFU;
    uint32_t count = (n - i >= 4U) ? 4U : (n - i);
    memcpy(&word, p + i, count);
    st = flash_word_program(addr + i, word);
    if(st != FLASH_OPERATE_DONE) break;
  }
  flash_lock();
  return st == FLASH_OPERATE_DONE;
}

void bl_protocol_reset(void)
{
  rxn = 0U;
  active = 0U;
  image_len = 0U;
  image_crc = 0U;
  image_next = 0U;
}

static void handle_frame(const uint8_t *p, uint16_t n)
{
  uint8_t cmd = p[3];
  uint32_t addr, len, crc;
  memcpy(&addr, p + 6U, sizeof(addr));
  memcpy(&len, p + 10U, sizeof(len));
  memcpy(&crc, p + 14U, sizeof(crc));

  if(cmd == BL_CMD_HELLO)
  {
    reply(cmd, BL_ST_OK, BL_APP_BASE);
  }
  else if(cmd == BL_CMD_BEGIN)
  {
    if(addr != BL_APP_BASE || len == 0U || len > (BL_APP_END - BL_APP_BASE))
      reply(cmd, BL_ST_BAD_PARAM, 0U);
    else if(!erase_app())
      reply(cmd, BL_ST_FLASH, 0U);
    else
    {
      image_len = len; image_crc = crc; image_next = 0U; active = 1U;
      reply(cmd, BL_ST_OK, 0U);
    }
  }
  else if(cmd == BL_CMD_DATA)
  {
    if(!active || addr != BL_APP_BASE + image_next || len == 0U ||
       len > BL_MAX_CHUNK || n != BL_HEADER_SIZE + len ||
       image_next + len > image_len)
      reply(cmd, BL_ST_BAD_PARAM, image_next);
    else if(bl_crc32(p + BL_HEADER_SIZE, len) != crc)
      reply(cmd, BL_ST_CRC, image_next);
    else if(!program_words(addr, p + BL_HEADER_SIZE, len))
      reply(cmd, BL_ST_FLASH, image_next);
    else
    {
      image_next += len;
      reply(cmd, BL_ST_OK, image_next);
    }
  }
  else if(cmd == BL_CMD_END)
  {
    if(!active || image_next != image_len)
      reply(cmd, BL_ST_BAD_PARAM, image_next);
    else if(bl_crc32((const uint8_t *)BL_APP_BASE, image_len) != image_crc)
      reply(cmd, BL_ST_CRC, image_next);
    else if(!valid_app())
      reply(cmd, BL_ST_NO_APP, image_next);
    else
    {
      active = 0U;
      reply(cmd, BL_ST_OK, image_next);
    }
  }
  else if(cmd == BL_CMD_ABORT)
  {
    bl_protocol_reset();
    reply(cmd, BL_ST_OK, 0U);
  }
  else if(cmd == BL_CMD_BOOT)
  {
    active = 0U;
    reply(cmd, BL_ST_OK, 0U);
  }
  else
  {
    reply(cmd, BL_ST_BAD_FRAME, 0U);
  }
}

void bl_protocol_feed(uint8_t b)
{
  rx[rxn++] = b;
  for(;;)
  {
    uint32_t len;
    if(rxn < BL_HEADER_SIZE) return;
    if(rx[0] != BL_MAGIC0 || rx[1] != BL_MAGIC1 ||
       rx[2] != BL_PROTOCOL_VERSION)
    {
      memmove(rx, rx + 1U, --rxn);
      continue;
    }
    memcpy(&len, rx + 10U, sizeof(len));
    if(len > BL_MAX_CHUNK)
    {
      memmove(rx, rx + 1U, --rxn);
      continue;
    }
    if(rxn < BL_HEADER_SIZE + len) return;
    if(rxn > BL_HEADER_SIZE + len)
    {
      /* A valid frame is always consumed first; the remaining bytes are
       * retained so back-to-back USB/UART frames are not discarded. */
      uint16_t frame = (uint16_t)(BL_HEADER_SIZE + len);
      handle_frame(rx, frame);
      memmove(rx, rx + frame, rxn - frame);
      rxn = (uint16_t)(rxn - frame);
      continue;
    }
    handle_frame(rx, rxn);
    rxn = 0U;
    return;
  }
}
