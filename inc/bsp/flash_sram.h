#ifndef FLASH_SRAM_H
#define FLASH_SRAM_H
#include <stdint.h>
#include "sram_capture.h"
#include "at32f423_flash.h"
/* Separate translation unit, no LTO or naked assembly: normal AAPCS call. */
uint32_t flash_erase_wait_sram(uint32_t address);
#if APP_SRAM_CAPTURE
flash_status_type capture_flash_sector_erase(uint32_t address);
flash_status_type capture_flash_word_program(uint32_t address,uint32_t data);
#ifndef FLASH_SRAM_IMPLEMENTATION
#define flash_sector_erase capture_flash_sector_erase
#define flash_word_program capture_flash_word_program
#endif
#endif
#endif
