#ifndef TEST_FLASH_H
#define TEST_FLASH_H
#include <stdint.h>
typedef enum { FLASH_OPERATE_DONE, FLASH_PROGRAM_ERROR } flash_status_type;
void flash_unlock(void);
void flash_lock(void);
flash_status_type flash_sector_erase(uint32_t address);
flash_status_type flash_word_program(uint32_t address, uint32_t value);
#endif
