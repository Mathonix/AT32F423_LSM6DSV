#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H
#include <stdint.h>
#define BL_FLASH_BASE 0x08000000U
#define BL_FLASH_END  0x08008000U
#define BL_APP_BASE   0x08008000U
#define BL_APP_END    0x0803C000U
#define BL_CONFIG_BASE 0x0803C000U
#define BL_FLASH_SIZE 0x00040000U
/* 256-KB AT32F423: 128 physical Flash sectors of 2 KiB (RM v2.03, table 2-1).
 * flash_sector_erase() erases exactly one sector per call. */
#define BL_SECTOR_SIZE 0x800U
#if ((BL_APP_BASE % BL_SECTOR_SIZE) != 0U) || ((BL_APP_END % BL_SECTOR_SIZE) != 0U)
#error "Bootloader application erase range must be sector-aligned"
#endif
#define BL_UART_BAUD 2000000U
/* Normal startup timeout is settings.gyro_init_ms (0..60000), default 2000.
 * An explicit upgrade request or invalid application stays in recovery. */
#define BL_PROTOCOL_VERSION 1U
#define BL_MAX_CHUNK 256U
#define BL_MAGIC0 0x42U
#define BL_MAGIC1 0x4CU

/* Used for both normal startup and final upload validation. Accept legacy
 * images with an initial SP at the physical SRAM end, but never an empty,
 * misaligned or out-of-range stack, or a non-Thumb reset vector. */
static inline int bl_app_vectors_valid(uint32_t sp, uint32_t pc)
{
  return sp > 0x20000000U && sp <= 0x2000C000U && (sp & 7U) == 0U &&
         (pc & 1U) != 0U && (pc & ~1U) >= BL_APP_BASE &&
         (pc & ~1U) < BL_APP_END;
}
#endif
