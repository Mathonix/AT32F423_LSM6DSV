#ifndef TIMEBASE_FAST_H
#define TIMEBASE_FAST_H
#include <stdint.h>
/* Foreground-only, like the original dwt_cycles64 caller. Keep that original
 * wrap tracker; replace repeated 64-bit division with exact quotient/remainder
 * updates between calls. A changed clock divisor takes the original slow path.
 * Calls must remain less than one CYCCNT wrap apart, as in the original clock.
 */
typedef struct {
    uint32_t divisor, last, ms, remainder, initialized;
} timebase_fast_t;
static inline uint32_t timebase_fast_ms(timebase_fast_t *s,uint64_t cycles,uint32_t divisor)
{
    if(!s->initialized || s->divisor!=divisor) {
        s->ms=(uint32_t)(cycles/divisor);
        s->remainder=(uint32_t)(cycles%divisor);
        s->divisor=divisor;s->initialized=1;
    } else {
        uint32_t delta=(uint32_t)cycles-s->last;
        uint32_t q=delta/divisor;
        uint32_t rem=s->remainder+(delta-q*divisor);
        s->ms+=q;
        if(rem>=divisor){rem-=divisor;s->ms++;}
        s->remainder=rem;
    }
    s->last=(uint32_t)cycles;return s->ms;
}
static inline uint32_t timebase_fast_us(const timebase_fast_t *s,uint64_t cycles,uint32_t divisor_us)
{
    if(s->divisor==divisor_us*1000U)
        return s->ms*1000U+s->remainder/divisor_us;
    return (uint32_t)(cycles/divisor_us);
}
#endif
