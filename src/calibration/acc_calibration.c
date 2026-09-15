#include "acc_calibration.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include <string.h>

#define ACC_CAL_MAGIC 0x41434332U
#define ACC_CAL_VERSION 1U
#define ACC_CAL_ADDR APP_ACC_CAL_FLASH_ADDR

typedef struct {
  uint32_t magic;
  uint32_t version;
  float bias_g[3];
  float scale[3];
  uint32_t crc;
} acc_cal_record_t;

acc_calibration_t acc_calibration_active = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0U};

#if APP_ACC_CAL_ENABLE
static uint32_t crc32(const void *data, uint32_t len)
{
  const uint8_t *p=(const uint8_t *)data; uint32_t c=0xFFFFFFFFU, i, b;
  for(i=0U;i<len;i++){ c^=p[i]; for(b=0U;b<8U;b++) c=(c>>1)^(0xEDB88320U & (0U-(c&1U))); }
  return c;
}
#endif

void acc_calibration_load(void)
{
#if !APP_ACC_CAL_ENABLE
  acc_calibration_active.valid = 0U;
  return;
#else
  const acc_cal_record_t *r=(const acc_cal_record_t *)ACC_CAL_ADDR;
  if(r->magic==ACC_CAL_MAGIC && r->version==ACC_CAL_VERSION &&
     r->scale[0]>0.5f && r->scale[0]<1.5f && r->scale[1]>0.5f && r->scale[1]<1.5f &&
     r->scale[2]>0.5f && r->scale[2]<1.5f &&
     r->crc==crc32(r,sizeof(*r)-sizeof(r->crc)))
  {
    memcpy(acc_calibration_active.bias_g,r->bias_g,sizeof(r->bias_g));
    memcpy(acc_calibration_active.scale,r->scale,sizeof(r->scale));
    acc_calibration_active.valid=1U;
  }

#endif
}

int acc_calibration_save(const acc_calibration_t *cal)
{
#if !APP_ACC_CAL_ENABLE
  (void)cal;
  return -3;
#else
  acc_cal_record_t r; flash_status_type st; uint32_t i, word;
  if(cal==0 || cal->valid==0U) return -1;
  memset(&r,0,sizeof(r)); r.magic=ACC_CAL_MAGIC; r.version=ACC_CAL_VERSION;
  memcpy(r.bias_g,cal->bias_g,sizeof(r.bias_g)); memcpy(r.scale,cal->scale,sizeof(r.scale));
  r.crc=crc32(&r,sizeof(r)-sizeof(r.crc));
  flash_unlock(); st=flash_sector_erase(ACC_CAL_ADDR);
  if(st==FLASH_OPERATE_DONE) for(i=0U;i<sizeof(r)/4U;i++) { memcpy(&word,((const uint8_t *)&r)+4U*i,4U); st=flash_word_program(ACC_CAL_ADDR+4U*i,word); if(st!=FLASH_OPERATE_DONE) break; }
  flash_lock();
  if(st!=FLASH_OPERATE_DONE) return -2;
  memcpy(&acc_calibration_active,cal,sizeof(*cal));
  return 0;
#endif
}


