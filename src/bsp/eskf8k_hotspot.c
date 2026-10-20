/* Diagnostic-only sampled scopes. Estimator and scheduling are unchanged.
 * Exclusive CPU clocks exclude all nested IRQ time. A scope may contain a
 * named child scope (PACK/CRC, COMMANDS/CRC); host reports mark those overlaps.
 * Calls are counted in full; duration sampling uses coprime /17 to avoid
 * aliasing the 8/40/800 sample schedules. Rare accel/mag calls sampled fully.
 */
#include "eskf8k_hotspot.h"
#include "bsp.h"
#include <string.h>
typedef struct { uint32_t calls,n,min,max; uint64_t sum; uint32_t hist[64]; } hot_stat_t;
volatile hot_stat_t hot_stats[H_COUNT];
volatile hot_stat_t hot_acc_split[2]; /* API housekeeping / accepted observation */
volatile uint32_t hot_request,hot_enabled;
volatile uint32_t hot_abi[10]={0x484f5431,H_COUNT,sizeof(hot_stat_t),17,0,0,APP_ESKF8K_HOT_GROUP};
volatile uint64_t hot_wall_start,hot_wall_end;
const uint32_t hot_width[H_COUNT]={
32,32,32,32,32, 32,32,32,32,32,32,32,
64,64,128,32,256,512,512,128,256,1024,256,256,512,128,32,
64,32,256,128,256,128,64,128,32};

uint64_t hot_begin_impl(unsigned id) {
 if(!hot_enabled)return 0;
 volatile hot_stat_t *s=&hot_stats[id];uint32_t n=++s->calls;
 if(id!=H_ACCEL && id!=H_MAG && id!=H_EMPTY && n%17U)return 0;
 return eskf8k_cpu_exclusive_now()+1U;
}
static void record(volatile hot_stat_t *s,uint32_t d,uint32_t width) {
 if(!s->n||d<s->min)s->min=d;
 if(d>s->max)s->max=d;
 s->n++;s->sum+=d;unsigned b=d/width;if(b>63)b=63;s->hist[b]++;
}
void hot_end_impl(unsigned id,uint64_t start) {
 if(!start)return;
 uint32_t d=(uint32_t)(eskf8k_cpu_exclusive_now()+1U-start);
 record(&hot_stats[id],d,hot_width[id]);
}
void hot_end_acc(uint64_t start,unsigned observation_accepted) {
 if(!start)return;
 uint32_t d=(uint32_t)(eskf8k_cpu_exclusive_now()+1U-start);
 record(&hot_stats[H_ACCEL],d,hot_width[H_ACCEL]);
 volatile hot_stat_t *s=&hot_acc_split[!!observation_accepted];s->calls++;
 record(s,d,hot_width[H_ACCEL]);
}
void hot_prepare(void) {
 hot_enabled=1;uint32_t start=dwt_cycles();
 for(unsigned i=0;i<1000;i++){uint64_t s=hot_begin(H_EMPTY);hot_end(H_EMPTY,s);}
 hot_abi[4]=(dwt_cycles()-start)/1000U;
 hot_abi[5]=hot_stats[H_EMPTY].sum/1000U;
 hot_enabled=0;memset((void*)hot_stats,0,sizeof(hot_stats));memset((void*)hot_acc_split,0,sizeof(hot_acc_split));
}
void hot_sync(void) {
 if(hot_request==1U){
  hot_enabled=0;memset((void*)hot_stats,0,sizeof(hot_stats));memset((void*)hot_acc_split,0,sizeof(hot_acc_split));
  hot_wall_start=eskf8k_cpu_total_now();hot_wall_end=0;hot_enabled=1;hot_request=0;
 } else if(hot_request==2U){
  hot_enabled=0;hot_wall_end=eskf8k_cpu_total_now();__DMB();hot_request=0;
 }
}
