#ifndef APP_CONFIG_H
#define APP_CONFIG_H
/* Central application configuration. Keep all board-level tuning here. */
#define APP_FUSION_HZ             2000U
#define APP_GYR_LPF_CUTOFF_HZ    30.0f
#define APP_CAL_REST_SECONDS      1U
#define APP_CAL_DROP_MS           200U
#define APP_CAL_GYR_REST_DPS      1.0f
#define APP_CAL_ACC_REST_MS2      0.8f
// Trial B: isolate motion-bias updates during false non-rest intervals.
#define APP_VQF_MOTION_BIAS_ENABLE 0U
#define APP_VQF_TAU_ACC           3.0f
#define APP_VQF_TAU_MAG           4.0f
#define APP_VQF_REST_GYR_DPS      1.2f
#define APP_VQF_REST_ACC_MS2      0.4f
#define APP_VQF_PRIME_MAX_SAMPLES 1000U
#define APP_VOFA_OUTPUT_HZ        200U
#define APP_UART_BAUD             2000000U
#endif


