#include "perf_meter.h"
#include <string.h>
#ifndef PERF_NATIVE_TEST
#include "bsp.h"
static uint32_t cycle(void){return DWT->CYCCNT;}
static uint32_t lock(void){uint32_t p=__get_PRIMASK();__disable_irq();return p;}
static void unlock(uint32_t p){__set_PRIMASK(p);}
#else
extern uint32_t test_cycle,test_clock;
#define system_core_clock test_clock
static uint32_t cycle(void){return test_cycle;}
static uint32_t lock(void){return 0;}
static void unlock(uint32_t p){(void)p;}
#endif
typedef struct {uint64_t cycles;uint32_t calls,max;} bucket_t;
static bucket_t data[PERF_BUCKETS];
static uint64_t irq_union,wall;
static uint32_t outer_start,depth,last_wall,enabled,open,anomalies;
static perf_mark_t main_start;
static perf_mark_t mark_locked(void){perf_mark_t r={cycle(),(uint32_t)irq_union};return r;}
static void add(unsigned n,uint32_t duration){
  if(n>=PERF_BUCKETS){anomalies++;return;}
  data[n].cycles+=duration;data[n].calls++;
  if(duration>data[n].max)data[n].max=duration;
}
void perf_begin(void){
  uint32_t p=lock();memset(data,0,sizeof(data));irq_union=wall=0;
  depth=open=anomalies=0;last_wall=cycle();enabled=1;unlock(p);
}
perf_mark_t perf_mark(void){uint32_t p=lock();perf_mark_t r=mark_locked();unlock(p);return r;}
void perf_record_cycles(unsigned n,uint32_t cycles){if(enabled)add(n,cycles);}
uint32_t perf_record(unsigned n,perf_mark_t start){
  if(!enabled)return 0;
  perf_mark_t end=perf_mark();uint32_t elapsed=end.cycle-start.cycle,irq=end.irq-start.irq;
  if(irq>elapsed){anomalies++;return 0;}
  add(n,elapsed-irq);
  return elapsed-irq;
}
void perf_loop_begin(void){if(enabled){main_start=perf_mark();open=1;}}
void perf_loop_end(void){
  if(!enabled||!open)return;
  perf_mark_t end=perf_mark();uint32_t elapsed=end.cycle-main_start.cycle,irq=end.irq-main_start.irq;
  if(irq<=elapsed)add(PERF_MAIN,elapsed-irq);else anomalies++;
  wall+=(uint32_t)(end.cycle-last_wall);last_wall=end.cycle;open=0;
}
uint32_t perf_irq_enter(void){
  uint32_t p=lock(),now=cycle();
  if(enabled){if(depth++==0)outer_start=now;}
  unlock(p);return now;
}
void perf_irq_exit(unsigned n,uint32_t start){
  uint32_t p=lock(),now=cycle();
  if(enabled){
    add(n,now-start); /* Per-IRQ duration is INCLUSIVE of higher-priority IRQs. */
    if(!depth)anomalies++;
    else if(--depth==0)irq_union+=(uint32_t)(now-outer_start);
  }
  unlock(p);
}
void perf_snapshot(unsigned page,uint32_t out[16]){
  uint32_t p=lock(),now=cycle();uint64_t w=wall+(uint32_t)(now-last_wall);
  bucket_t b={0};if(page<PERF_BUCKETS)b=data[page];
  if(page==PERF_MAIN&&open){
    uint32_t elapsed=now-main_start.cycle,irq=(uint32_t)irq_union-main_start.irq;
    if(irq<=elapsed){b.cycles+=elapsed-irq;b.calls++;if(elapsed-irq>b.max)b.max=elapsed-irq;}
  }
  const uint64_t idle=data[PERF_IDLE].cycles;
  const uint32_t fields[16]={2,page,system_core_clock,(uint32_t)w,(uint32_t)(w>>32),
      b.calls,(uint32_t)b.cycles,(uint32_t)(b.cycles>>32),b.max,
      (uint32_t)irq_union,(uint32_t)(irq_union>>32),depth,enabled,anomalies,(uint32_t)idle,(uint32_t)(idle>>32)};
  memcpy(out,fields,sizeof(fields));unlock(p);
}
