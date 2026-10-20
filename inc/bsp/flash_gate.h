#ifndef FLASH_GATE_H
#define FLASH_GATE_H
#include <stdint.h>
/* Authorization for the SRAM Flash wrappers. Boot is deny (both bounds 0).
 * Streaming and SAVE_PENDING never enter a kind. MAINTENANCE enters one
 * kind around one persistence call and always leaves it. No kind can name
 * the bootloader, the application image, or a mass erase.
 * The admissible window is the only RAM state. Callers in flash compare it. */
enum {
  FLASH_GATE_DENY = 0,
  FLASH_GATE_SETTINGS = 1, /* 0x0803D000 .. 0x0803E000 */
  FLASH_GATE_RESERVED = 2  /* 0x0803C000 .. 0x08040000 */
};
extern volatile uint32_t flash_gate_lo; /* inclusive */
extern volatile uint32_t flash_gate_hi; /* exclusive */
void flash_gate_enter(uint32_t kind);
void flash_gate_leave(void);
static inline int flash_gate_settings_held(void)
{
  return flash_gate_lo == 0x0803D000U && flash_gate_hi == 0x0803E000U;
}
#endif
