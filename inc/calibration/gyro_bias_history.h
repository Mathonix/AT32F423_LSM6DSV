#ifndef GYRO_BIAS_HISTORY_H
#define GYRO_BIAS_HISTORY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GYRO_BIAS_HISTORY_MAX 15U

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
int gyro_bias_history_save_at_temp(const float bias[3], float temperature_c);
#ifdef __cplusplus
}
#endif
#endif
