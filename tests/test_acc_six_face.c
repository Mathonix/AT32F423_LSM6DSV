#include "acc_six_face.h"
#include "app_config.h"
#include "at32f423_flash.h"
#include "test_memory.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int writes, fail_word = -1, unlocked;
void flash_unlock(void) { unlocked = 1; }
void flash_lock(void) { unlocked = 0; }
flash_status_type flash_sector_erase(uint32_t address) { (void)address; assert(!"calibration must never erase sector"); return FLASH_PROGRAM_ERROR; }
flash_status_type flash_word_program(uint32_t address, uint32_t word)
{
  uint32_t *p = (uint32_t *)(uintptr_t)address;
  assert(unlocked && address >= APP_ACC_CAL_FLASH_ADDR && address < APP_ACC_CAL_FLASH_ADDR+2048U && !(address&3U));
  if(writes++ == fail_word) return FLASH_PROGRAM_ERROR;
  assert((*p&word) == word); *p &= word; return FLASH_OPERATE_DONE;
}
static uint32_t crc(const void *data, unsigned len)
{
  uint32_t c=0xffffffffU; const uint8_t *b=data; unsigned i,k;
  for(i=0;i<len;i++) { c^=b[i]; for(k=0;k<8;k++) c=(c>>1)^(0xedb88320U & (0U-(c&1U))); } return c;
}
static void clean_flash(void)
{
  memset((void *)(uintptr_t)0x08000000U, 0xff, 0x40000U);
  fail_word=-1; writes=0; unlocked=0; acc_calibration_load();
}
static acc_calibration_t truth = {{0.018f,-0.025f,0.009f},{1.015f,0.985f,1.008f},1U};
static void vector(unsigned face, float a[3])
{
  unsigned k; for(k=0;k<3;k++) a[k]=truth.bias_g[k];
  a[face/2] += ((face&1U) ? 1.0f : -1.0f)/truth.scale[face/2];
}
/* Hand-placed face: gravity tilted off the nominal axis by off[] (true g on
 * the two other axes, in X,Y,Z order), magnitude still exactly 1 g. */
static void tilted_vector(unsigned face, const float off[2], float a[3])
{
  unsigned k, j=0, axis=face/2; float g[3], off2=0.0f;
  for(k=0;k<3;k++) if(k!=axis) { g[k]=off[j++]; off2+=g[k]*g[k]; }
  g[axis]=((face&1U) ? 1.0f : -1.0f)*sqrtf(1.0f-off2);
  for(k=0;k<3;k++) a[k]=truth.bias_g[k]+g[k]/truth.scale[k];
}
static uint32_t pose_at(acc_six_face_t *s, uint32_t at, const float a[3], unsigned duration)
{
  const float g[3]={0}; unsigned n;
  for(n=0;n<=duration;n+=2) (void)acc_six_face_push(s,at+n,a,g);
  return at+duration+2;
}
static uint32_t pose(acc_six_face_t *s, uint32_t at, unsigned face, unsigned duration)
{
  float a[3]; vector(face,a); return pose_at(s,at,a,duration);
}
static void near(float a,float b) { assert(fabsf(a-b)<0.00005f); }
static void successful_fit(void)
{
  acc_six_face_t s; unsigned order[6]={5,0,3,1,4,2},i; uint32_t t=0;
  acc_six_face_start(&s,t);
  for(i=0;i<6;i++) {
    t=pose(&s,t,order[i],1500);
    assert(s.face==i+1 && (s.face_mask&(1U<<order[i])));
    if(i<5) { unsigned count=s.face; t=pose(&s,t,order[i],1800); assert(s.face==count && s.phase==ACC_CAL_PHASE_DUPLICATE); }
  }
  assert(s.phase==ACC_CAL_PHASE_SAVING && s.active && s.face_mask==63 && s.result.valid);
  for(i=0;i<3;i++) { near(s.result.bias_g[i],truth.bias_g[i]); near(s.result.scale[i],truth.scale[i]); }
  assert(acc_calibration_save(&s.result)==0); acc_six_face_saved(&s,0);
  assert(s.status==ACC_CAL_STATUS_DONE && !s.active);
  acc_calibration_load(); assert(acc_calibration_active.valid);
  for(i=0;i<3;i++) near(acc_calibration_active.scale[i],truth.scale[i]);
}
/* Regression for hardware 0x0606: 5..9 degree hand-placement tilt on every
 * face passes detection (other axes < 0.20 g) and must fit the exact truth,
 * not be rejected as a cross-axis residual. */
static void tilted_fit(void)
{
  static const float off[6][2]={{0.09f,-0.11f},{-0.12f,0.07f},{0.10f,0.12f},
                                {-0.08f,-0.13f},{0.14f,0.05f},{-0.06f,0.10f}};
  acc_six_face_t s; unsigned order[6]={2,5,1,4,0,3},i; uint32_t t=0; float a[3];
  acc_six_face_start(&s,t);
  for(i=0;i<6;i++) {
    tilted_vector(order[i],off[order[i]],a); t=pose_at(&s,t,a,1500);
    assert(s.face==i+1 && (s.face_mask&(1U<<order[i])));
  }
  assert(s.phase==ACC_CAL_PHASE_SAVING && s.active && s.result.valid);
  for(i=0;i<3;i++) { near(s.result.bias_g[i],truth.bias_g[i]); near(s.result.scale[i],truth.scale[i]); }
  acc_six_face_cancel(&s);
}
static void motion_and_gap(void)
{
  acc_six_face_t s; uint32_t t; float a[3],g[3]={0};
  acc_six_face_start(&s,0); t=pose(&s,0,1,800); assert(s.samples>0);
  vector(1,a); g[0]=5; acc_six_face_push(&s,t,a,g);
  assert(!s.candidate && !s.samples && !s.mean[0]); g[0]=0;
  t=pose(&s,t+2,1,498); assert(!s.samples && !s.face);
  t=pose(&s,t,1,500); assert(s.samples>0 && !s.face);
  acc_six_face_push(&s,t+100,a,g); assert(!s.samples && s.phase==ACC_CAL_PHASE_STABLE);
  t=pose(&s,t+102,1,1500); assert(s.face==1);
  /* NaN and fast linear acceleration must not become static candidates. */
  vector(3,a); a[0]=NAN; acc_six_face_push(&s,t,a,g); assert(!s.candidate && !s.samples);
  vector(3,a); acc_six_face_push(&s,t+2,a,g); a[0]+=0.1f;
  acc_six_face_push(&s,t+4,a,g); assert(!s.samples && s.phase==ACC_CAL_PHASE_STABLE);
}
static void invalid_fits_and_variance(void)
{
  acc_six_face_t s; uint32_t t=0; unsigned face,n; float a[3],g[3]={0};
  acc_calibration_t saved_truth=truth;
  /* Y sensitivity 14% low: each face still passes the 0.85 g norm gate, but
   * the fitted scale 1.16 exceeds the plausible range and must not save. */
  truth.bias_g[1]=0.0f; truth.scale[1]=1.16f;
  acc_six_face_start(&s,t);
  for(face=0;face<6;face++) t=pose(&s,t,face,1500);
  truth=saved_truth;
  assert(s.face==6 && s.status==ACC_CAL_STATUS_FAILED && s.error==0x0606 && !s.result.valid);
  acc_six_face_start(&s,0); vector(1,a);
  for(n=0;n<=1500;n+=2) {
    float jitter=0.024f*sinf((float)n*0.04f); a[1]=truth.bias_g[1]+jitter;
    acc_six_face_push(&s,n,a,g);
  }
  assert(s.face==0 && s.phase==ACC_CAL_PHASE_MOVING);
}
static void timeout_cancel_and_wrap(void)
{
  acc_six_face_t s; acc_calibration_t old=acc_calibration_active;
  acc_six_face_start(&s,0); acc_six_face_tick(&s,60000);
  assert(s.status==ACC_CAL_STATUS_FAILED && s.error==0x0603 && !s.active);
  assert(!memcmp(&old,&acc_calibration_active,sizeof(old)));
  acc_six_face_start(&s,100); pose(&s,100,0,1500); acc_six_face_cancel(&s);
  assert(s.status==ACC_CAL_STATUS_CANCELLED && !s.active);
  assert(!memcmp(&old,&acc_calibration_active,sizeof(old)));
  acc_six_face_start(&s,0xffffff00U); pose(&s,0xffffff00U,4,1500);
  assert(s.face==1 && s.active);
  acc_six_face_saved(&s,-4); assert(s.error==0x0609 && s.status==ACC_CAL_STATUS_FAILED);
}
static void legacy_record(void)
{
  struct { uint32_t magic,version; float bias[3],scale[3]; uint32_t crc; } old;
  old.magic=0x41434332; old.version=1;
  memcpy(old.bias,truth.bias_g,12); memcpy(old.scale,truth.scale,12); old.crc=crc(&old,32);
  memcpy((void *)(uintptr_t)APP_ACC_CAL_FLASH_ADDR,&old,sizeof(old));
  acc_calibration_load(); assert(acc_calibration_active.valid); near(acc_calibration_active.bias_g[0],truth.bias_g[0]);
}
static void journal_faults(void)
{
  unsigned point,slot; acc_calibration_t next=truth,old;
  next.bias_g[0]+=0.01f;
  for(point=0;point<16;point++) {
    clean_flash(); legacy_record(); old=acc_calibration_active;
    writes=0; fail_word=(int)point; assert(acc_calibration_save(&next)==-2); assert(!unlocked);
    acc_calibration_load(); assert(!memcmp(&old,&acc_calibration_active,sizeof(old)));
    fail_word=-1; writes=0; assert(acc_calibration_save(&next)==0);
    assert(!memcmp((void *)(uintptr_t)(APP_ACC_CAL_FLASH_ADDR+12U),truth.bias_g+1,8));
    acc_calibration_load(); near(acc_calibration_active.bias_g[0],next.bias_g[0]);
    /* Corrupt newest committed record: CRC rejects it, legacy is retained. */
    ((uint8_t *)(uintptr_t)(APP_ACC_CAL_FLASH_ADDR+(point ? 128U : 64U)))[12]^=1;
    acc_calibration_load(); near(acc_calibration_active.bias_g[0],truth.bias_g[0]);
  }
  clean_flash(); legacy_record();
  for(slot=0;slot<31;slot++) { next.bias_g[0]=0.018f+(float)(slot+1)*0.001f; assert(acc_calibration_save(&next)==0); }
  old=acc_calibration_active; next.bias_g[0]+=0.001f;
  assert(acc_calibration_save(&next)==-4); acc_calibration_load(); assert(!memcmp(&old,&acc_calibration_active,sizeof(old)));
  /* Invalid nonfinite parameters and bad CRC never activate a calibration. */
  next.scale[0]=NAN; assert(acc_calibration_save(&next)==-1);
  clean_flash(); legacy_record(); ((uint32_t *)(uintptr_t)APP_ACC_CAL_FLASH_ADDR)[8]^=1;
  acc_calibration_load(); assert(!acc_calibration_active.valid && acc_calibration_active.scale[0]==1.0f);
}
int main(void)
{
  void *mapped=test_map_memory(0x08000000U,0x40000U);
  clean_flash(); successful_fit(); tilted_fit(); motion_and_gap(); invalid_fits_and_variance(); timeout_cancel_and_wrap(); journal_faults();
  test_unmap_memory(mapped,0x40000U);
  puts("PASS six-face: unordered poses, duplicates, tilted placement, motion/gaps, variance/scale range, timeout/cancel/wrap, legacy/journal/power-loss/full");
  return 0;
}
