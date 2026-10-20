#include "heap_guard.h"
#include "at32f423.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
extern uint8_t __heap_start, __heap_limit;
static uintptr_t heap_break;
volatile uint32_t heap_guard_live[8];

void heap_guard_init(void)
{
  heap_break=(uintptr_t)&__heap_start;
  heap_guard_live[0]=0x48454131U;
  heap_guard_live[1]=(uint32_t)heap_break;
  heap_guard_live[7]=(uintptr_t)&__heap_limit-heap_break;
}

static void *reject(int reason)
{
  errno=reason;heap_guard_live[4]++;heap_guard_live[5]=(uint32_t)reason;
  return (void *)-1;
}

/* The app owns one fixed linker-reserved heap. No ISR is allowed to extend
 * it. Checks use differences so neither signed negation nor pointer addition
 * can wrap. Shrinking below its initial break is an explicit failure. */
void *_sbrk(ptrdiff_t increment)
{
  const uintptr_t start=(uintptr_t)&__heap_start,limit=(uintptr_t)&__heap_limit;
  if(__get_IPSR()) { heap_guard_live[6]++;return reject(EPERM); }
  if(!heap_break)heap_guard_init();
  uintptr_t old=heap_break,next;
  if(old<start||old>limit)return reject(ENOMEM);
  if(increment>=0) {
    uintptr_t grow=(uintptr_t)increment;
    if(grow>limit-old)return reject(ENOMEM);
    next=old+grow;
  } else {
    uintptr_t shrink=(uintptr_t)(-(increment+1))+1U;
    if(shrink>old-start)return reject(EINVAL);
    next=old-shrink;
  }
  /* Fixed heap bounds reserve the complete stack. Also reject an already
   * dangerously low current MSP instead of relying on the linker alone. */
  uintptr_t sp=__get_MSP();
  if(sp<256U||next>sp-256U)return reject(ENOMEM);
  heap_break=next;heap_guard_live[1]=(uint32_t)next;heap_guard_live[3]++;
  uint32_t used=(uint32_t)(next-start);
  if(used>heap_guard_live[2])heap_guard_live[2]=used;
  return (void *)old;
}
