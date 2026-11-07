#ifndef CLOCK_QUOTIENT_H
#define CLOCK_QUOTIENT_H
#include <stdint.h>
/* Exact floor(now/divisor) modulo u32. Common short deltas use the M4 u32
 * divider; first use, rate changes and long gaps retain the full u64 path.
 * Caller serializes state and supplies the existing extended DWT epoch. */
typedef struct {uint64_t previous;uint32_t ticks,remainder,divisor;} clock_quotient_t;
static inline uint32_t clock_quotient(clock_quotient_t *s,uint64_t now,uint32_t divisor) {
  if(!divisor)return 0;
  if(s->divisor!=divisor || now<s->previous) {
    s->ticks=(uint32_t)(now/divisor);s->remainder=(uint32_t)(now%divisor);s->divisor=divisor;
  } else {
    uint64_t n=now-s->previous+s->remainder;
    if(n<=UINT32_MAX){uint32_t v=(uint32_t)n;s->ticks+=v/divisor;s->remainder=v%divisor;}
    else{s->ticks+=(uint32_t)(n/divisor);s->remainder=(uint32_t)(n%divisor);}
  }
  s->previous=now;return s->ticks;
}
#endif
