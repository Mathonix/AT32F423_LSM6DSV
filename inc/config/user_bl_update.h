#ifndef USER_BL_UPDATE_H
#define USER_BL_UPDATE_H
#include <stdint.h>
#ifdef APP_USER_BL_UPDATE
#define USER_BL_BASE 0x08000000U
#define USER_BL_BYTES 0x8000U
#define USER_BL_SECTOR 0x800U
#define USER_BL_APPLY_MAGIC 0x42554C31U
typedef struct {
  uint8_t version, status; uint16_t reserved;
  uint32_t image_bytes, image_crc, current_crc, last_address, chip_id;
} user_bl_info_t;
/* Status: 0 ready, 1 busy, 2 verified, 3 invalid image, 4 erase,
 * 5 program, 6 readback failure. No resets on failure. */
uint32_t user_bl_crc(const void *data, uint32_t size);
uint8_t user_bl_program(const uint8_t *image, uint32_t size, uint32_t crc);
void user_bl_get_info(user_bl_info_t *info);
int user_bl_request_valid(uint32_t magic, uint32_t size, uint32_t crc);
void user_bl_apply(void);
#endif
#endif
