#ifndef GYRO_TEMP_COMP_H
#define GYRO_TEMP_COMP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TEMP_BIN_COUNT 26U
#define TEMP_MODEL_MAGIC 0x42494153U
#define TEMP_MODEL_VERSION 4U
#define TEMP_MODEL_COMMIT 0x54434D34U
#define TEMP_MODEL_FALLBACK_RAW 1U
#define TEMP_MODEL_FALLBACK_LEGACY 2U
enum { TEMP_BIN_EMPTY, TEMP_BIN_CANDIDATE, TEMP_BIN_CONFIRMED, TEMP_BIN_QUARANTINE };
typedef struct {
  float temperature_c, bias_dps[3], sigma_dps[3];
  uint16_t accepted_windows, flags;
} temp_bin_t;
typedef struct {
  uint32_t magic, version, sequence, model_flags;
  float fallback_temp_c, fallback_bias_dps[3];
  temp_bin_t bins[TEMP_BIN_COUNT];
  uint32_t crc32, commit_marker;
} temp_comp_record_v4_t;
typedef struct {
  uint32_t revision, sequence;
  int32_t last_flash_error;
  uint8_t dirty, flash_valid, flash_slot, writes, reboot_required;
} temp_comp_info_t;
/* Flash read/commit API. commit is ONLY called from the main-loop safe-idle
 * service after rest, operation-conflict, command-TX and wear checks. */
uint32_t gyro_temp_comp_crc(const void *data, uint32_t size);
int gyro_temp_comp_record_valid(const temp_comp_record_v4_t *record);
int gyro_temp_comp_has_v4(void);
void gyro_temp_comp_init(float boot_temp_c);
void gyro_temp_comp_update(uint32_t now_ms, float filtered_temp_c);
float gyro_temp_comp_delta(unsigned axis);
int gyro_temp_comp_active(void);
float gyro_temp_comp_boot_temperature(void);
/* Startup seed after a capture that is not fresh. 1 = segment, or a confirmed
 * bin inside APP_GYR_BIAS_TEMP_WINDOW_C. 3 = nearest confirmed bin outside
 * that window when no raw fallback is strictly closer. 2 = raw fallback.
 * 0 = no historical point. Does not write flash.
 * gyro_temp_comp_boot_selected_c() is the temperature of the last choice
 * (the query temperature for code 1). */
int gyro_temp_comp_boot_bias(float temp_c, float raw_bias_dps[3]);
float gyro_temp_comp_boot_selected_c(void);
const temp_comp_record_v4_t *gyro_temp_comp_working(void);
void gyro_temp_comp_info(temp_comp_info_t *info);
void gyro_temp_comp_set_bin(unsigned index, const temp_bin_t *bin);
void gyro_temp_comp_discard_candidates(void);
void gyro_temp_comp_clear(void);
/* Queues a RAW startup fallback in RAM, never writes/erases Flash. */
int gyro_temp_comp_queue_fallback_rad(const float raw_bias_rad_s[3], float temp_c);
int gyro_temp_comp_commit(uint32_t now_ms);
int gyro_temp_comp_save_allowed(uint32_t now_ms, int explicit_request);
int gyro_temp_comp_write_allowed(uint32_t now_ms, int explicit_request);
#ifdef __cplusplus
}
#endif
#endif
