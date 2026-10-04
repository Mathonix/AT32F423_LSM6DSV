#include "boot_startup.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static const float gyro[3]={.001f,-.002f,.003f}, acc[3]={0,0,9.80665f};
static void feed(boot_startup_t *s, unsigned duration, unsigned period)
{
  for(unsigned ms=0;ms<duration;ms+=period)
    boot_startup_push(s,s->start_ms+ms,gyro,acc,27.25f);
}
static void test_duration(unsigned duration, uint32_t start)
{
  boot_startup_t s; boot_startup_handoff_t out, copy;
  volatile boot_startup_handoff_t mailbox;
  boot_startup_begin(&s,start,duration,1000);
  feed(&s,duration,1);
  boot_startup_finish(&s,start+duration,1,&out);
  assert(out.status==BOOT_STARTUP_FRESH && out.elapsed_ms==duration);
  assert(out.samples==duration && boot_startup_matches(&out,duration,1000));
  for(unsigned i=0;i<3;++i) assert(fabsf(out.bias_rad_s[i]-gyro[i])<1e-7f);
  boot_startup_publish(&mailbox,&out);
  assert(boot_startup_take(&mailbox,&copy));
  assert(memcmp(&out,&copy,sizeof(out))==0 && mailbox.magic==0);
  assert(!boot_startup_take(&mailbox,&copy));
  assert(!boot_startup_matches(&out,duration,500));
  assert(!boot_startup_matches(&out,duration-1,1000));
  boot_startup_publish(&mailbox,&out); mailbox.bias_rad_s[0]+=1;
  assert(!boot_startup_take(&mailbox,&copy) && mailbox.magic==0);
  boot_startup_finish(&s,start+duration,0,&out); /* upgrade / sensor failure */
  assert(out.status==BOOT_STARTUP_HISTORY && boot_startup_matches(&out,duration,1000));
}
static void test_fallback(void)
{
  boot_startup_t s; boot_startup_handoff_t out;
  boot_startup_begin(&s,10,0,1000);
  boot_startup_push(&s,10,gyro,acc,25);
  boot_startup_finish(&s,10,1,&out);
  assert(out.status==BOOT_STARTUP_HISTORY && out.samples==0 && out.elapsed_ms==0);
  assert(boot_startup_matches(&out,0,1000));
  boot_startup_begin(&s,0,2000,1000); feed(&s,2000,2); /* insufficient sample rate */
  boot_startup_finish(&s,2000,1,&out); assert(out.status==BOOT_STARTUP_HISTORY);
  boot_startup_begin(&s,0,2000,1000); feed(&s,1990,1); /* stale final sample */
  boot_startup_finish(&s,2000,1,&out); assert(out.status==BOOT_STARTUP_HISTORY);
  boot_startup_begin(&s,0,2000,1000); feed(&s,1000,1);
  float moving[3]={1,0,0}; boot_startup_push(&s,1000,moving,acc,25);
  for(unsigned ms=1001;ms<2000;++ms) boot_startup_push(&s,ms,gyro,acc,25);
  boot_startup_finish(&s,2000,1,&out);
  assert(out.status==BOOT_STARTUP_HISTORY && out.elapsed_ms==2000 && out.rejected_windows);
  boot_startup_begin(&s,0,2000,1000);
  for(unsigned ms=10;ms<2000;++ms) boot_startup_push(&s,ms,gyro,acc,25);
  boot_startup_finish(&s,2000,1,&out); assert(out.status==BOOT_STARTUP_HISTORY);
  boot_startup_begin(&s,0,2000,1000); feed(&s,2000,1);
  boot_startup_finish(&s,1999,1,&out); assert(out.status==BOOT_STARTUP_HISTORY);
  /* Noise in the partial block must not evade checks at the fixed deadline. */
  boot_startup_begin(&s,0,250,1000);
  for(unsigned ms=0;ms<250;++ms) {
    float noisy[3]={ms>202 ? (ms%2 ? .005f : -.005f) : .001f,0,0};
    boot_startup_push(&s,ms,noisy,acc,25);
  }
  boot_startup_finish(&s,250,1,&out); assert(out.status==BOOT_STARTUP_HISTORY);
  /* A gap remains disqualifying even after a later stationary segment. */
  boot_startup_begin(&s,0,2000,1000);
  for(unsigned ms=0;ms<2000;++ms) if(ms<100 || ms>110) boot_startup_push(&s,ms,gyro,acc,25);
  boot_startup_finish(&s,2000,1,&out); assert(out.status==BOOT_STARTUP_HISTORY);
}
int main(void)
{
  test_duration(2,0); test_duration(2000,0); test_duration(60000,0);
  test_duration(2000,UINT32_MAX-1000U); test_fallback();
  boot_cycle_clock_t clock={0}; uint64_t elapsed=0;
  for(unsigned seconds=1;seconds<=60;++seconds) {
    elapsed+=150000000U;
    assert(boot_cycle_clock_update(&clock,(uint32_t)elapsed)==elapsed);
  }
  assert(clock.total==9000000000ULL); /* two 32-bit wraps in 60 s */
  puts("shared boot startup: 0/2/2000/60000ms, fixed deadline, motion/noise/gap, mailbox and timer wraps: OK");
  return 0;
}
