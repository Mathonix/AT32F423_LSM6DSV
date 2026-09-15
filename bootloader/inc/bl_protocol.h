#ifndef BL_PROTOCOL_H
#define BL_PROTOCOL_H
#include <stdint.h>
#define BL_CMD_HELLO 0x01U
#define BL_CMD_BEGIN 0x02U
#define BL_CMD_DATA 0x03U
#define BL_CMD_END 0x04U
#define BL_CMD_ABORT 0x05U
#define BL_CMD_BOOT 0x06U
#define BL_CMD_ACK 0x80U
#define BL_ST_OK 0U
#define BL_ST_BAD_FRAME 1U
#define BL_ST_BAD_PARAM 2U
#define BL_ST_CRC 3U
#define BL_ST_FLASH 4U
#define BL_ST_NO_APP 5U
#define BL_ST_BUSY 6U
uint32_t bl_crc32(const uint8_t *p, uint32_t n);
void bl_protocol_reset(void);
void bl_protocol_feed(uint8_t b);
#endif
