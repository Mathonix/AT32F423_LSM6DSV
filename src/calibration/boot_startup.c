#include "boot_startup.h"
#include "gyro_range.h"
#include <math.h>
#include <string.h>
#include <stddef.h>
_Static_assert(sizeof(boot_startup_handoff_t) == 64U, "boot startup mailbox ABI");
static uint32_t checksum(const boot_startup_handoff_t *data)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFU;
  for(unsigned i=0; i<offsetof(boot_startup_handoff_t, crc); ++i) {
    crc ^= p[i];
    for(unsigned b=0; b<8; ++b) crc=(crc>>1) ^ (0xEDB88320U & (0U-(crc&1U)));
  }
  return crc;
}
uint64_t boot_cycle_clock_update(boot_cycle_clock_t *clock, uint32_t cycles)
{
  clock->total += (uint32_t)(cycles-clock->last);
  clock->last=cycles;
  return clock->total;
}
void boot_startup_begin(boot_startup_t *s, uint32_t now, uint16_t duration, uint16_t range)
{
  memset(s,0,sizeof(*s));
  s->start_ms=now; s->range_dps=range; s->temperature_c=25.0f;
  gyro_startup_calibration_init(&s->calibration,duration);
}
void boot_startup_push(boot_startup_t *s, uint32_t now, const float gyro[3], const float acc[3], float temp)
{
  if(s->calibration.duration_ms && (uint32_t)(now-s->start_ms)<s->calibration.duration_ms) {
    s->temperature_c=temp;
    (void)gyro_startup_calibration_push(&s->calibration,now,gyro,acc);
  }
}
void boot_startup_finish(const boot_startup_t *s, uint32_t now, int allow_fresh, boot_startup_handoff_t *out)
{
  boot_startup_t qualified=*s;
  gyro_startup_calibration_finalize(&qualified.calibration);
  s=&qualified;
  memset(out,0,sizeof(*out));
  out->magic=BOOT_STARTUP_MAGIC; out->version=BOOT_STARTUP_VERSION;
  out->duration_ms=s->calibration.duration_ms; out->range_dps=s->range_dps;
  out->elapsed_ms=(uint32_t)(now-s->start_ms); out->samples=s->calibration.samples;
  out->rejected_windows=s->calibration.rejected_windows;
  out->rejection_reason=s->calibration.rejection_reason;
  out->temperature_c=s->temperature_c;
  if(allow_fresh && gyro_startup_calibration_finish_window(&s->calibration, now, s->start_ms,
                                                        out->bias_rad_s,out->gravity_ms2))
    { out->status=BOOT_STARTUP_FRESH; out->rejection_reason=0; }
  else if(out->duration_ms) out->rejection_reason = allow_fresh ?
      gyro_startup_calibration_window_reason(&s->calibration,now,s->start_ms) : 14U;
  out->crc=checksum(out);
}
void boot_startup_publish(volatile boot_startup_handoff_t *mailbox, const boot_startup_handoff_t *data)
{
  volatile uint8_t *destination=(volatile uint8_t *)mailbox;
  const uint8_t *source=(const uint8_t *)data;
  mailbox->magic=0U;
  for(unsigned i=4; i<sizeof(*data); ++i) destination[i]=source[i];
  mailbox->magic=data->magic;
}
int boot_startup_take(volatile boot_startup_handoff_t *mailbox, boot_startup_handoff_t *out)
{
  const volatile uint8_t *source=(const volatile uint8_t *)mailbox;
  uint8_t *destination=(uint8_t *)out;
  for(unsigned i=0; i<sizeof(*out); ++i) destination[i]=source[i];
  mailbox->magic=0U; /* consume once, including corrupt or incompatible results */
  return out->magic==BOOT_STARTUP_MAGIC && out->version==BOOT_STARTUP_VERSION &&
         out->crc==checksum(out);
}
int boot_startup_matches(const boot_startup_handoff_t *data, uint16_t duration, uint16_t range)
{
  if(data->magic!=BOOT_STARTUP_MAGIC || data->version!=BOOT_STARTUP_VERSION ||
     data->crc!=checksum(data) || data->duration_ms!=duration || duration>60000U ||
     data->range_dps!=range || !gyro_range_valid(range) || data->status>BOOT_STARTUP_FRESH) return 0;
  if(data->status==BOOT_STARTUP_HISTORY) return 1;
  if(!duration || data->samples<duration || data->elapsed_ms<duration || data->rejection_reason ||
     !isfinite(data->temperature_c)) return 0;
  for(unsigned i=0;i<3;++i)
    if(!isfinite(data->bias_rad_s[i]) || fabsf(data->bias_rad_s[i])>0.017453293f ||
       !isfinite(data->gravity_ms2[i])) return 0;
  return 1;
}
