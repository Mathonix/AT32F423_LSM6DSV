#ifdef APP_USER_BL_UPDATE
#include "user_bl_update.h"
#include "at32f423.h"
#include "at32f423_flash.h"
#include <string.h>
#ifndef USER_BL_UPDATE_NATIVE
#include "bl_image.h"
#endif
static uint8_t update_status;
static uint32_t last_address;
uint32_t user_bl_crc(const void *data, uint32_t size)
{
  const uint8_t *p=data; uint32_t crc=0xFFFFFFFFU;
  for(uint32_t i=0;i<size;++i) {
    crc^=p[i]; for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xEDB88320U & (0U-(crc&1U)));
  }
  return ~crc;
}
uint8_t user_bl_program(const uint8_t *image, uint32_t size, uint32_t crc)
{
  uint32_t sp,pc, primask;
  uintptr_t begin=(uintptr_t)image;
  update_status=3; last_address=0;
  /* Embedded source must be wholly in the still-running APP partition. */
  if(size!=USER_BL_BYTES || begin<0x08008000U || begin>0x0803C000U-size ||
     *(const volatile uint32_t *)0xE0042000U!=0x700A3253U || user_bl_crc(image,size)!=crc) return update_status;
  memcpy(&sp,image,4); memcpy(&pc,image+4,4);
  if(sp!=0x2000BF00U || !(pc&1U) || (pc&~1U)<USER_BL_BASE || (pc&~1U)>=USER_BL_BASE+size) return update_status;
  if(!memcmp((const void *)USER_BL_BASE,image,size)) { update_status=2; return 2; }
  primask=__get_PRIMASK(); __disable_irq(); update_status=1;
  flash_unlock(); flash_flag_clear(FLASH_PRGMERR_FLAG|FLASH_EPPERR_FLAG);
  /* Sector zero and its vectors commit last. APP code/data never overlap BL.
   * Host sends no more traffic until the synchronous update finishes. */
  for(unsigned step=0;step<USER_BL_BYTES/USER_BL_SECTOR;++step) {
    unsigned sector=(step+1U)%(USER_BL_BYTES/USER_BL_SECTOR);
    uint32_t off=sector*USER_BL_SECTOR; last_address=USER_BL_BASE+off;
    if(flash_sector_erase(last_address)!=FLASH_OPERATE_DONE) { update_status=4; goto done; }
    for(unsigned k=sector==0 ? 8U : 0U;k<USER_BL_SECTOR;k+=4) {
      uint32_t value;memcpy(&value,image+off+k,4);last_address=USER_BL_BASE+off+k;
      if(value!=0xFFFFFFFFU && flash_word_program(last_address,value)!=FLASH_OPERATE_DONE) { update_status=5; goto done; }
      if(*(const volatile uint32_t *)(uintptr_t)last_address!=value) { update_status=6; goto done; }
    }
    if(sector==0) {
      for(unsigned k=0;k<8;k+=4) {
        uint32_t value; memcpy(&value,image+k,4);last_address=USER_BL_BASE+k;
        if(flash_word_program(last_address,value)!=FLASH_OPERATE_DONE) { update_status=5; goto done; }
      }
    }
  }
  update_status=memcmp((const void *)USER_BL_BASE,image,size) || user_bl_crc((const void *)USER_BL_BASE,size)!=crc ? 6 : 2;
done:
  flash_lock(); __DSB(); __ISB(); __set_PRIMASK(primask);
  return update_status;
}
#ifndef USER_BL_UPDATE_NATIVE
void user_bl_get_info(user_bl_info_t *info)
{
  *info=(user_bl_info_t){1,update_status,0,BL_IMAGE_BYTES,BL_IMAGE_CRC,
    user_bl_crc((const void *)USER_BL_BASE,USER_BL_BYTES),last_address,*(const volatile uint32_t *)0xE0042000U};
}
int user_bl_request_valid(uint32_t magic, uint32_t size, uint32_t crc)
{return magic==USER_BL_APPLY_MAGIC && size==BL_IMAGE_BYTES && crc==BL_IMAGE_CRC;}
void user_bl_apply(void) { (void)user_bl_program(bl_image,BL_IMAGE_BYTES,BL_IMAGE_CRC); }
#endif
#endif
