#define FLASH_SRAM_IMPLEMENTATION
#include "flash_sram.h"
#include "flash_gate.h"
/* Only the two existing settings sectors are accepted by this entry point.
 * CTRL/ADDR/STS are the only peripheral registers it can access. No unlock,
 * protection, option-byte or mass-erase operation exists in this function.
 * .data is copied by the existing startup before any caller can execute it.
 */
#if APP_SRAM_CAPTURE
RAM_CODE
#else
__attribute__((noinline,section(".data.flash_erase_wait_sram")))
#endif
uint32_t flash_erase_wait_sram(uint32_t address)
{
    volatile uint32_t *const ctrl=(volatile uint32_t *)0x40023c10U;
    volatile uint32_t *const addr=(volatile uint32_t *)0x40023c14U;
    volatile const uint32_t *const sts=(volatile const uint32_t *)0x40023c0cU;
#if APP_SRAM_CAPTURE
    /* Settings erase is reachable only while maintenance holds the settings
     * window. Return before CTRL is written so a denied call cannot stall. */
    if(flash_gate_lo != 0x0803d000U || flash_gate_hi != 0x0803e000U) return UINT32_MAX;
#endif
    if(address!=0x0803d000U && address!=0x0803d800U)return UINT32_MAX;
    uint32_t spins=0;
    *ctrl=2U;*addr=address;*ctrl=0x42U;
    while(*sts&1U)spins++;
    *ctrl=0U;
    return spins;
}
#if APP_SRAM_CAPTURE
/* noinline: -O3 otherwise pastes this window check into both callers and
 * the RAM copy no longer fits beside the 3 KiB stack reserve. */
static RAM_CODE __attribute__((noinline)) int admissible(uint32_t address)
{
 /* lo/hi are 0 until maintenance enters a window. Nothing below 0x0803C000
  * or at/above 0x08040000 can be named by flash_gate_enter. */
 if(address<flash_gate_lo || address>=flash_gate_hi){
  sram_live[SR_PROTECTED_REJECTS]++;return 0;
 }
 if(!sram_flash_active() && !sram_flash_ready()){
  sram_live[SR_SAVE_DEFERRED]++;return 0;
 }
 return 1;
}
static RAM_CODE __attribute__((noinline)) flash_status_type status(void)
{
 uint32_t s=*(volatile uint32_t *)0x40023c0cU;
 if(s&0x10U)return FLASH_EPP_ERROR;
 if(s&4U)return FLASH_PROGRAM_ERROR;
 return FLASH_OPERATE_DONE;
}
RAM_CODE flash_status_type capture_flash_sector_erase(uint32_t address)
{
 if((address&0x7ffU)||!admissible(address))return FLASH_EPP_ERROR;
 sram_flash_begin();
 volatile uint32_t *ctrl=(volatile uint32_t *)0x40023c10U;
 *ctrl=2U;*(volatile uint32_t *)0x40023c14U=address;*ctrl=0x42U;
 while(*(volatile uint32_t *)0x40023c0cU&1U){}
 *ctrl=0U;flash_status_type st=status();sram_flash_end();return st;
}
RAM_CODE flash_status_type capture_flash_word_program(uint32_t address,uint32_t data)
{
 if((address&3U)||!admissible(address))return FLASH_EPP_ERROR;
 sram_flash_begin();
 volatile uint32_t *ctrl=(volatile uint32_t *)0x40023c10U;
 *ctrl=1U;*(volatile uint32_t *)address=data;
 while(*(volatile uint32_t *)0x40023c0cU&1U){}
 *ctrl=0U;flash_status_type st=status();sram_flash_end();return st;
}
#endif
