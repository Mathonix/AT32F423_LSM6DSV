#ifndef GYRO_BIAS_HISTORY_H
#define GYRO_BIAS_HISTORY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Version 3 holds 50 samples in each existing 2 KB slot. Version 2 records
 * of 15 samples stay readable; the next save keeps them and appends. */
#define GYRO_BIAS_HISTORY_MAX 50U

/* Newest record inside the temperature window; otherwise the caller may
 * fall back to load_nearest. Bias arrays are always in rad/s. */
int gyro_bias_history_load_recent_for_temp(float latest[3], float average[3],
    float selected[3], float current_temp_c, float temp_window_c,
    float *selected_temp_c, uint8_t *selected_valid, uint32_t *count,
    uint8_t *corrupt);

/* Load the latest record and the average of all valid history entries. */
int gyro_bias_history_load(float latest[3], float average[3], uint32_t *count, uint8_t *corrupt);
/* Temperature-aware load. nearest_valid is set when at least one entry is
 * within temp_window_c of current_temp_c; nearest is the average of those
 * entries and nearest_temp_c is their mean temperature. */
int gyro_bias_history_load_for_temp(float latest[3], float average[3],
                                    float nearest[3], float current_temp_c,
                                    float temp_window_c, float *nearest_temp_c,
                                    uint8_t *nearest_valid, uint32_t *count,
                                    uint8_t *corrupt);
int gyro_bias_history_save(const float bias[3]);
/* Closest valid historical temperature, even outside the preferred window. */
int gyro_bias_history_load_nearest(float bias[3], float temperature_c, float *matched_temperature_c);
int gyro_bias_history_save_at_temp(const float bias[3], float temperature_c);
/* Copy samples in oldest-first order. offset 0 is the oldest. record_version
 * is the flash version of the accepted slot (2 or 3), or 0 when none.
 * corrupt is set when a slot was present but unreadable. This does not
 * write flash. Returns 0. */
int gyro_bias_history_read(uint32_t offset, uint32_t limit,
                           float bias[][3], float *temperature_c,
                           uint32_t *copied, uint32_t *count,
                           uint32_t *sequence, uint8_t *record_version,
                           uint8_t *corrupt);
#ifdef __cplusplus
}
#endif
#endif
