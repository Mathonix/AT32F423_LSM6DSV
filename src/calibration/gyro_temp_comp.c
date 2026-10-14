#include "gyro_temp_comp.h"
#include "gyro_bias_history.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(temp_bin_t)==32U,"temperature bin layout");
_Static_assert(sizeof(temp_comp_record_v4_t)==872U,"V4 flash layout");
_Static_assert(offsetof(temp_comp_record_v4_t,crc32)==864U,"V4 CRC boundary");
_Static_assert(APP_GYR_BIAS_FLASH_SLOT0_ADDR==0x0803E800U,"temperature slot A");
_Static_assert(APP_GYR_BIAS_FLASH_SLOT1_ADDR==0x0803F800U,"temperature slot B");
static temp_comp_record_v4_t active_model, working_model, write_record;
static temp_comp_info_t info;
static uint32_t last_update_ms, last_write_ms;
static uint8_t initialized, update_seen, segment_valid;
static unsigned segment_lo, segment_hi;
static float boot_temp, boot_bias[3], delta_bias[3];
static uint32_t slot_address(unsigned slot)
{ return slot ? APP_GYR_BIAS_FLASH_SLOT1_ADDR : APP_GYR_BIAS_FLASH_SLOT0_ADDR; }
uint32_t gyro_temp_comp_crc(const void *data, uint32_t size)
{
  const uint8_t *p=data;
  uint32_t crc=0xFFFFFFFFU;
  for(uint32_t i=0;i<size;++i) {
    crc^=p[i];
    for(unsigned b=0;b<8;++b) crc=(crc>>1)^(0xEDB88320U&(0U-(crc&1U)));
  }
  return crc;
}
static int valid_temperature(float t) { return isfinite(t)&&t>=-40.f&&t<=85.f; }
static int raw_bias_valid(const float b[3])
{
  for(unsigned i=0;i<3;++i)
    if(!isfinite(b[i])||fabsf(b[i])>APP_TEMP_LEARN_MAX_BIAS_DPS) return 0;
  return 1;
}
int gyro_temp_comp_record_valid(const temp_comp_record_v4_t *r)
{
  if(r->magic!=TEMP_MODEL_MAGIC||r->version!=4U||r->commit_marker!=TEMP_MODEL_COMMIT||
     (r->model_flags&~3U)||((r->model_flags&TEMP_MODEL_FALLBACK_LEGACY)&&
                          !(r->model_flags&TEMP_MODEL_FALLBACK_RAW))||
     r->crc32!=gyro_temp_comp_crc(r,offsetof(temp_comp_record_v4_t,crc32))) return 0;
  if(!isfinite(r->fallback_temp_c)||!raw_bias_valid(r->fallback_bias_dps)) return 0;
  if((r->model_flags&TEMP_MODEL_FALLBACK_RAW)&&!valid_temperature(r->fallback_temp_c)) return 0;
  for(unsigned i=0;i<TEMP_BIN_COUNT;++i) {
    const temp_bin_t *b=&r->bins[i];
    if(b->flags>TEMP_BIN_QUARANTINE||!isfinite(b->temperature_c)||!raw_bias_valid(b->bias_dps)) return 0;
    for(unsigned a=0;a<3;++a)
      if(!isfinite(b->sigma_dps[a])||b->sigma_dps[a]<0.f||b->sigma_dps[a]>APP_TEMP_LEARN_SIGMA_DPS) return 0;
    if(b->flags==TEMP_BIN_EMPTY) {
      temp_bin_t zero={0};
      if(memcmp(b,&zero,sizeof(zero))) return 0;
    } else if(!b->accepted_windows||!valid_temperature(b->temperature_c)||
              fabsf(b->temperature_c-(-40.f+5.f*i))>2.5f||
              (b->flags==TEMP_BIN_CONFIRMED&&b->accepted_windows<2U)) return 0;
  }
  return 1;
}
static const temp_comp_record_v4_t *valid_slot(unsigned slot)
{
  const temp_comp_record_v4_t *r=(const void *)(uintptr_t)slot_address(slot);
  return gyro_temp_comp_record_valid(r)?r:NULL;
}
int gyro_temp_comp_has_v4(void) { return valid_slot(0)!=NULL||valid_slot(1)!=NULL; }
static int connected(const temp_comp_record_v4_t *r, unsigned a, unsigned b)
{
  float span=r->bins[b].temperature_c-r->bins[a].temperature_c;
  if(!(b>a&&b-a<=2U&&span>0.f&&span<=APP_TEMP_MODEL_MAX_GAP_C)) return 0;
  for(unsigned i=a+1U;i<b;++i)
    if(r->bins[i].flags==TEMP_BIN_CANDIDATE||r->bins[i].flags==TEMP_BIN_QUARANTINE) return 0;
  return 1;
}
/* Locate one continuous segment. A single trusted node cannot define a slope. */
static int find_segment(const temp_comp_record_v4_t *r,float t,unsigned *lo,unsigned *hi)
{
  unsigned first=TEMP_BIN_COUNT, previous=TEMP_BIN_COUNT;
  for(unsigned i=0;i<=TEMP_BIN_COUNT;++i) {
    if(i<TEMP_BIN_COUNT&&r->bins[i].flags!=TEMP_BIN_CONFIRMED) continue;
    if(previous<TEMP_BIN_COUNT&&(i==TEMP_BIN_COUNT||!connected(r,previous,i))) {
      if(first!=previous&&t>=r->bins[first].temperature_c&&t<=r->bins[previous].temperature_c)
        { *lo=first; *hi=previous; return 1; }
      first=TEMP_BIN_COUNT;
    }
    if(i==TEMP_BIN_COUNT) break;
    if(first==TEMP_BIN_COUNT) first=i;
    previous=i;
  }
  return 0;
}
static void interpolate(const temp_comp_record_v4_t *r,unsigned lo,unsigned hi,float t,float out[3])
{
  if(t<=r->bins[lo].temperature_c) { memcpy(out,r->bins[lo].bias_dps,12U); return; }
  if(t>=r->bins[hi].temperature_c) { memcpy(out,r->bins[hi].bias_dps,12U); return; }
  unsigned left=lo;
  for(unsigned right=lo+1U;right<=hi;++right) {
    if(r->bins[right].flags!=TEMP_BIN_CONFIRMED) continue;
    if(t<=r->bins[right].temperature_c) {
      float w=(t-r->bins[left].temperature_c)/(r->bins[right].temperature_c-r->bins[left].temperature_c);
      for(unsigned a=0;a<3;++a) out[a]=r->bins[left].bias_dps[a]+w*(r->bins[right].bias_dps[a]-r->bins[left].bias_dps[a]);
      return;
    }
    left=right;
  }
}
void gyro_temp_comp_init(float boot_temp_c)
{
  const temp_comp_record_v4_t *a=valid_slot(0),*b=valid_slot(1),*best=NULL;
  memset(&info,0,sizeof(info)); info.flash_slot=255U;
  memset(&active_model,0,sizeof(active_model)); memset(&working_model,0,sizeof(working_model));
  memset(delta_bias,0,sizeof(delta_bias)); memset(boot_bias,0,sizeof(boot_bias));
  if(a&&(!b||(int32_t)(a->sequence-b->sequence)>0)) { best=a; info.flash_slot=0U; }
  else if(b) { best=b; info.flash_slot=1U; }
  if(best) { active_model=*best; working_model=*best; info.flash_valid=1U; info.sequence=best->sequence; }
  else {
    float old_bias[3],old_temp; uint32_t sequence; uint8_t slot;
    int legacy=gyro_bias_history_legacy_metadata(old_bias,&old_temp,&sequence,&slot);
    /* Protect even an empty legacy journal, or one with unknown bias domain.
     * Migration always targets the opposite physical slot. */
    if(legacy) { info.flash_slot=slot; info.sequence=sequence; }
    /* Old nonzero linear correction makes legacy bias provenance ambiguous. */
    if(APP_GYR_TEMP_COEFF_X_DPS_PER_C==0.f&&APP_GYR_TEMP_COEFF_Y_DPS_PER_C==0.f&&
       APP_GYR_TEMP_COEFF_Z_DPS_PER_C==0.f&&
       legacy==1) {
      for(unsigned i=0;i<3;++i) working_model.fallback_bias_dps[i]=old_bias[i]/0.017453292519943295f;
      if(valid_temperature(old_temp)&&raw_bias_valid(working_model.fallback_bias_dps)) {
        working_model.fallback_temp_c=old_temp;
        working_model.model_flags=TEMP_MODEL_FALLBACK_RAW|TEMP_MODEL_FALLBACK_LEGACY;
      } else memset(&working_model,0,sizeof(working_model));
    }
  }
  boot_temp=boot_temp_c; segment_valid=0U;
#if APP_GYR_TEMP_COMP_ENABLE
  segment_valid=(uint8_t)(valid_temperature(boot_temp)&&find_segment(&active_model,boot_temp,&segment_lo,&segment_hi));
  if(segment_valid) interpolate(&active_model,segment_lo,segment_hi,boot_temp,boot_bias);
#endif
  initialized=1U; update_seen=0U; last_update_ms=last_write_ms=0U; info.revision=1U;
}
void gyro_temp_comp_update(uint32_t now_ms,float filtered_temp_c)
{
  if(!segment_valid||!isfinite(filtered_temp_c)) return;
  if(update_seen&&(uint32_t)(now_ms-last_update_ms)<1000U/APP_TEMP_COMP_HZ) return;
  float bias[3]; interpolate(&active_model,segment_lo,segment_hi,filtered_temp_c,bias);
  for(unsigned a=0;a<3;++a) delta_bias[a]=bias[a]-boot_bias[a];
  last_update_ms=now_ms; update_seen=1U;
}
float gyro_temp_comp_delta(unsigned axis) { return axis<3U?delta_bias[axis]:0.f; }
int gyro_temp_comp_active(void) { return segment_valid; }
float gyro_temp_comp_boot_temperature(void) { return boot_temp; }
int gyro_temp_comp_boot_bias(float t,float out[3])
{
  unsigned lo,hi;
#if APP_GYR_TEMP_COMP_ENABLE
  if(find_segment(&active_model,t,&lo,&hi)) { interpolate(&active_model,lo,hi,t,out); return 1; }
  for(unsigned i=0;i<TEMP_BIN_COUNT;++i)
    if(active_model.bins[i].flags==TEMP_BIN_CONFIRMED&&fabsf(t-active_model.bins[i].temperature_c)<=APP_GYR_BIAS_TEMP_WINDOW_C)
      { memcpy(out,active_model.bins[i].bias_dps,12U); return 1; }
#else
  (void)lo; (void)hi;
#endif
  if(working_model.model_flags&TEMP_MODEL_FALLBACK_RAW)
    { memcpy(out,working_model.fallback_bias_dps,12U); return 2; }
  return 0;
}
const temp_comp_record_v4_t *gyro_temp_comp_working(void) { return &working_model; }
void gyro_temp_comp_info(temp_comp_info_t *out) { *out=info; }
void gyro_temp_comp_set_bin(unsigned index,const temp_bin_t *bin)
{
  if(index>=TEMP_BIN_COUNT||!memcmp(&working_model.bins[index],bin,sizeof(*bin))) return;
  working_model.bins[index]=*bin; info.revision++; info.dirty=1U;
}
void gyro_temp_comp_discard_candidates(void)
{
  temp_bin_t zero={0};
  for(unsigned i=0;i<TEMP_BIN_COUNT;++i)
    if(working_model.bins[i].flags==TEMP_BIN_CANDIDATE||working_model.bins[i].flags==TEMP_BIN_QUARANTINE)
      gyro_temp_comp_set_bin(i,&zero);
}
void gyro_temp_comp_clear(void)
{
  memset(working_model.bins,0,sizeof(working_model.bins)); info.revision++; info.dirty=1U;
}
int gyro_temp_comp_queue_fallback_rad(const float raw_rad[3],float t)
{
  float raw[3];
  if(!initialized||!raw_rad||!valid_temperature(t)) return -1;
  for(unsigned a=0;a<3;++a) raw[a]=raw_rad[a]/0.017453292519943295f;
  if(!raw_bias_valid(raw)) return -1;
  if((working_model.model_flags&TEMP_MODEL_FALLBACK_RAW)&&
     fabsf(t-working_model.fallback_temp_c)<0.25f) {
    float change=0;
    for(unsigned a=0;a<3;++a) change+=fabsf(raw[a]-working_model.fallback_bias_dps[a]);
    if(change<0.01f) return 0;
  }
  working_model.model_flags=TEMP_MODEL_FALLBACK_RAW;
  working_model.fallback_temp_c=t; memcpy(working_model.fallback_bias_dps,raw,sizeof(raw));
  info.dirty=1U; /* fallback is not part of the paginated table */
  return 0;
}
int gyro_temp_comp_write_allowed(uint32_t now_ms,int explicit_request)
{
  if(info.writes>=APP_TEMP_FLASH_MAX_WRITES) return 0;
  if(info.writes) return (uint32_t)(now_ms-last_write_ms)>=APP_TEMP_FLASH_INTERVAL_MS;
  return explicit_request||now_ms>=APP_TEMP_FLASH_INTERVAL_MS;
}
int gyro_temp_comp_save_allowed(uint32_t now_ms,int explicit_request)
{ return info.dirty&&gyro_temp_comp_write_allowed(now_ms,explicit_request); }
int gyro_temp_comp_commit(uint32_t now_ms)
{
  if(!gyro_temp_comp_save_allowed(now_ms,1)) return -5;
  /* Never allocate an 872-byte record on the sampling/service stack. */
  memset(&write_record,0,sizeof(write_record));
  write_record.magic=TEMP_MODEL_MAGIC; write_record.version=4U; write_record.sequence=info.sequence+1U;
  write_record.model_flags=working_model.model_flags;
  write_record.fallback_temp_c=working_model.fallback_temp_c;
  memcpy(write_record.fallback_bias_dps,working_model.fallback_bias_dps,12U);
  memcpy(write_record.bins,working_model.bins,sizeof(write_record.bins));
  write_record.crc32=gyro_temp_comp_crc(&write_record,offsetof(temp_comp_record_v4_t,crc32));
  write_record.commit_marker=TEMP_MODEL_COMMIT;
  if(!gyro_temp_comp_record_valid(&write_record)) { info.last_flash_error=-1; return -1; }
  unsigned target=info.flash_slot==0U?1U:0U;
  uint32_t address=slot_address(target);
  info.writes++; last_write_ms=now_ms; /* failed attempts also consume wear budget */
  flash_unlock();
  int error=0;
  if(flash_sector_erase(address)!=FLASH_OPERATE_DONE) error=-2;
  for(uint32_t offset=0;!error&&offset<offsetof(temp_comp_record_v4_t,commit_marker);offset+=4U) {
    uint32_t word; memcpy(&word,(const uint8_t *)&write_record+offset,4U);
    if(flash_word_program(address+offset,word)!=FLASH_OPERATE_DONE) error=-3;
  }
  if(!error&&memcmp((const void *)(uintptr_t)address,&write_record,offsetof(temp_comp_record_v4_t,commit_marker))) error=-4;
  if(!error&&flash_word_program(address+offsetof(temp_comp_record_v4_t,commit_marker),TEMP_MODEL_COMMIT)!=FLASH_OPERATE_DONE) error=-3;
  flash_lock();
  if(!error&&!gyro_temp_comp_record_valid((const void *)(uintptr_t)address)) error=-4;
  info.last_flash_error=error;
  if(!error) {
    info.sequence=write_record.sequence; info.flash_slot=(uint8_t)target;
    info.flash_valid=1U; info.dirty=0U;
    info.reboot_required=(uint8_t)(memcmp(active_model.bins,write_record.bins,sizeof(active_model.bins))!=0);
  }
  return error;
}
