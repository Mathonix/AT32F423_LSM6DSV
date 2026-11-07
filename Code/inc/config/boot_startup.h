#ifndef BOOT_STARTUP_H
#define BOOT_STARTUP_H
#include <stdint.h>
#include "gyro_startup_calibration.h"
#define BOOT_STARTUP_ADDR 0x2000BF00U
#define BOOT_STARTUP_MAGIC 0x42534331U
#define BOOT_STARTUP_VERSION 1U
#define BOOT_STARTUP_HISTORY 0U
#define BOOT_STARTUP_FRESH 1U
/* Both linkers reserve the top 256 bytes; the existing request at BFF0 stays. */
typedef struct {
  uint32_t magic, version;
  uint16_t duration_ms, range_dps;
  uint32_t status, elapsed_ms, samples, rejected_windows, rejection_reason;
  float temperature_c, bias_rad_s[3], gravity_ms2[3];
  uint32_t crc;
} boot_startup_handoff_t;
typedef struct {
  gyro_startup_calibration_t calibration;
  uint32_t start_ms;
  uint16_t range_dps;
  float temperature_c;
} boot_startup_t;
typedef struct { uint32_t last; uint64_t total; } boot_cycle_clock_t;
uint64_t boot_cycle_clock_update(boot_cycle_clock_t *clock, uint32_t cycles);
void boot_startup_begin(boot_startup_t *s, uint32_t now, uint16_t duration, uint16_t range);
void boot_startup_push(boot_startup_t *s, uint32_t now, const float gyro[3], const float acc[3], float temp);
void boot_startup_finish(const boot_startup_t *s, uint32_t now, int allow_fresh, boot_startup_handoff_t *out);
void boot_startup_publish(volatile boot_startup_handoff_t *mailbox, const boot_startup_handoff_t *data);
int boot_startup_take(volatile boot_startup_handoff_t *mailbox, boot_startup_handoff_t *out);
int boot_startup_matches(const boot_startup_handoff_t *data, uint16_t duration, uint16_t range);
#endif
