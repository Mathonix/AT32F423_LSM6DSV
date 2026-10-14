#ifndef GYRO_SCALE_H
#define GYRO_SCALE_H
#include <math.h>
/* Multipliers on nominal gyro sensitivity, before either fusion algorithm.
 * Bias history and the installed bootloader mailbox stay in nominal units. */
typedef struct { float axis[3]; } gyro_scale_t;
static inline void gyro_scale_defaults(gyro_scale_t *s)
{ for(unsigned i=0;i<3;i++) s->axis[i]=1.0f; }
static inline int gyro_scale_valid(const gyro_scale_t *s)
{
  if(!s)return 0;
  for(unsigned i=0;i<3;i++)
    if(!isfinite(s->axis[i]) || s->axis[i]<0.95f || s->axis[i]>1.05f)return 0;
  return 1;
}
static inline float gyro_scale_apply(const gyro_scale_t *s,unsigned axis,float rate)
{ return axis<3U ? rate*s->axis[axis] : rate; }
#endif
