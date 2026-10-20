#ifndef GYRO_TEMP_LEARNING_H
#define GYRO_TEMP_LEARNING_H
#include <stdint.h>
#include "protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { TEMP_WAIT, TEMP_REST_CONFIRM, TEMP_CAPTURE, TEMP_CANDIDATE,
       TEMP_VERIFY, TEMP_PENDING_SAVE, TEMP_SAVING, TEMP_PAUSED, TEMP_ERROR };
enum { TEMP_REJECT_NONE, TEMP_REJECT_MOTION, TEMP_REJECT_TEMP_UNSTABLE,
       TEMP_REJECT_NOISE, TEMP_REJECT_GAP, TEMP_REJECT_CONFLICT,
       TEMP_REJECT_COVERAGE, TEMP_REJECT_FLASH };
enum { TEMP_DETAIL_MODE=0x0801, TEMP_DETAIL_BUSY, TEMP_DETAIL_RATE_LIMIT,
       TEMP_DETAIL_NOT_DIRTY, TEMP_DETAIL_DISABLED };
typedef struct { uint8_t id, len, payload[AHRS_MAX_PAYLOAD_LEN]; } temp_learn_reply_t;
void gyro_temp_learning_init(void);
/* Main-loop producer, NOT ISR. Every input is a real, same-sample reading.
 * raw_dps is before ANY compensation/scale; residual_dps is after startup
 * seed, in the VQF input domain. No estimator bias is stored in the model. */
void gyro_temp_learning_sample(uint32_t now_ms, uint32_t sample_us, const float raw_dps[3],
    const float seed_residual_dps[3], const float corrected_residual_dps[3],
    const float acc_ms2[3], float chip_temp_c, uint8_t vqf_rest, uint8_t blocked);
void gyro_temp_learning_gap(void);
void gyro_temp_learning_environment(uint32_t now_ms, uint8_t safe_to_save);
int gyro_temp_learning_save_begin(uint32_t now_ms);
void gyro_temp_learning_save_finish(uint32_t now_ms);
int gyro_temp_learning_saving(void);
int gyro_temp_learning_wants_save(void);
void gyro_temp_learning_status(uint32_t now_ms, ahrs_payload_temp_learn_status_t *status);
/* Pure command handling: control actions only queue work. */
void gyro_temp_learning_command(uint8_t id, uint8_t len, const uint8_t *payload,
    uint8_t seq_unused, uint8_t settings_mode, uint8_t busy,
    uint32_t now_ms, temp_learn_reply_t *reply);
#ifdef __cplusplus
}
#endif
#endif
