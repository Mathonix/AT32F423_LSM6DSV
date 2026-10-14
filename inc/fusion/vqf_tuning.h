#ifndef VQF_TUNING_H
#define VQF_TUNING_H
#include <stdint.h>
#include <math.h>
#include "../app/app_config.h"
/* Version 1 order is shared by Flash and the 0x5B wire pages. */
#define VQF_TUNING_COUNT 14U
typedef struct { float value[VQF_TUNING_COUNT]; } vqf_tuning_t;
enum { VQF_TAU_ACC, VQF_TAU_MAG, VQF_SIGMA_INIT, VQF_SIGMA_REST,
  VQF_SIGMA_MOTION, VQF_BIAS_CLIP, VQF_FORGET_TIME, VQF_REST_TAU,
  VQF_REST_MIN_T, VQF_REST_GYR, VQF_REST_ACC, VQF_MAG_CURRENT_TAU,
  VQF_MAG_NORM_TH, VQF_MAG_DIP_TH };
static inline void vqf_tuning_defaults(vqf_tuning_t *p) {
  const float values[VQF_TUNING_COUNT] = {APP_VQF_TAU_ACC, APP_VQF_TAU_MAG,
    APP_VQF_BIAS_SIGMA_INIT_DPS, APP_VQF_BIAS_SIGMA_REST_DPS,
    APP_VQF_BIAS_SIGMA_MOTION_DPS, APP_VQF_BIAS_CLIP_DPS,
    APP_VQF_BIAS_FORGETTING_TIME_S, APP_VQF_REST_FILTER_TAU_S,
    APP_VQF_REST_MIN_SECONDS, APP_VQF_REST_GYR_DPS, APP_VQF_REST_ACC_MS2,
    0.05f, 0.1f, 10.0f};
  for(unsigned i=0;i<VQF_TUNING_COUNT;++i) p->value[i]=values[i];
}
static inline int vqf_tuning_valid(const vqf_tuning_t *p) {
  const float lo[VQF_TUNING_COUNT]={0.1f,0.2f,0.001f,0.001f,0.001f,0.1f,1,0.01f,0.1f,0.01f,0.01f,0.01f,0.01f,1};
  const float hi[VQF_TUNING_COUNT]={100,100,10,10,10,20,10000,10,60,20,10,10,0.5f,90};
  if(!p) return 0;
  for(unsigned i=0;i<VQF_TUNING_COUNT;++i)
    if(!isfinite(p->value[i]) || p->value[i]<lo[i] || p->value[i]>hi[i]) return 0;
  return p->value[VQF_SIGMA_REST]<=p->value[VQF_SIGMA_INIT] &&
    p->value[VQF_SIGMA_MOTION]<=p->value[VQF_SIGMA_INIT] &&
    p->value[VQF_SIGMA_INIT]<=p->value[VQF_BIAS_CLIP];
}
#endif
