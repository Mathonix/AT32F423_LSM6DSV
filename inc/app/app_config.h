#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Central application configuration. Keep all board-level tuning here. */
#define APP_FUSION_HZ             2000U
#define APP_GYR_LPF_CUTOFF_HZ    30.0f

/* LSM6DSV temperature telemetry is always published. */
#define APP_GYR_TEMP_COMP_ENABLE       1U
#define APP_GYR_TEMP_REF_C             25.0f
#define APP_GYR_TEMP_LPF_HZ            1.0f
#define APP_GYR_TEMP_COEFF_X_DPS_PER_C 0.0f
#define APP_GYR_TEMP_COEFF_Y_DPS_PER_C 0.0f
#define APP_GYR_TEMP_COEFF_Z_DPS_PER_C 0.0f

/* At boot discard the first second, then average 3 seconds of rest data. */
#define APP_CAL_REST_SECONDS      3.0f
#define APP_CAL_DROP_MS           1000U
#define APP_CAL_BLOCK_SAMPLES     32U
#define APP_CAL_BLOCK_MAX         256U
#define APP_CAL_TRIM_PERCENT      10U
#define APP_SFLP_BIAS_ENABLE      0U
#define APP_SFLP_BIAS_SETTLE_MS   1000U
#define APP_SFLP_BIAS_MAX_DPS     5.0f
#define APP_CAL_GYR_REST_DPS      1.0f
#define APP_CAL_ACC_REST_MS2      0.8f

/* Full VQF parameters */
#define APP_VQF_MOTION_BIAS_ENABLE 0U
#define APP_VQF_TAU_ACC           3.0f
#define APP_VQF_TAU_MAG           4.0f

/* Enable calibrated IST8310 updates in Full VQF for absolute yaw. */
#define APP_MAG_FUSION_ENABLE     1U
#define APP_MAG_VQF_UPDATE_DIV    5U

/* Full VQF stationary gyro-bias estimator tuning. */
#define APP_VQF_BIAS_SIGMA_REST_DPS       0.05f
#define APP_VQF_BIAS_FORGETTING_TIME_S  200.0f
#define APP_VQF_REST_GYR_DPS      1.2f
#define APP_VQF_REST_ACC_MS2      0.4f
#define APP_VQF_REST_MIN_SECONDS   1.5f
#define APP_VQF_PRIME_MAX_SAMPLES 1000U
#define APP_VOFA_OUTPUT_HZ        1000U

/* Hold the output attitude at the last value after VQF confirms rest. */
#define APP_VOFA_REST_HOLD_ENABLE 0U

/* Output-only adaptive yaw Kalman filter. Q is deg^2/s, R is deg^2. */
#define APP_VOFA_YAW_KF_Q_REST_DEG2_PER_S  0.0003f
#define APP_VOFA_YAW_KF_R_REST_DEG2        0.0049f
#define APP_VOFA_YAW_KF_Q_MOVE_DEG2_PER_S  0.50f
#define APP_VOFA_YAW_KF_R_MOVE_DEG2        0.0009f
#define APP_VOFA_YAW_KF_MOTION_START_DPS   0.5f
#define APP_VOFA_YAW_KF_MOTION_FULL_DPS    8.0f

/* Robustness settings for the output-only adaptive yaw KF. */
#define APP_VOFA_YAW_KF_RATE_LPF_HZ        25.0f
#define APP_VOFA_YAW_KF_MOTION_STOP_DPS    0.30f
#define APP_VOFA_YAW_KF_MOTION_CONFIRM_MS  20U
#define APP_VOFA_YAW_KF_INNOVATION_GATE_SIGMA 5.0f
#define APP_VOFA_YAW_KF_INNOVATION_GATE_MIN_DEG 0.25f
#define APP_VOFA_YAW_KF_R_ADAPT_TAU_S      0.50f
#define APP_VOFA_YAW_KF_R_ADAPT_MAX_SCALE  16.0f
#define APP_VOFA_YAW_KF_ACC_NORM_TOL_G     0.08f
#define APP_VOFA_YAW_KF_R_ACCEL_DEG2       0.0100f

/* CAN2 Settings: PA2=CAN2_RX, PA3=CAN2_TX, GPIO mux 9. */
#define APP_CAN_ENABLE             1U
#define APP_CAN_BAUDRATE_DIV       5U /* PCLK1=75 MHz, 15 TQ -> 1 Mbit/s */
#define APP_CAN_RSAW              CAN_RSAW_2TQ
#define APP_CAN_BTS1              CAN_BTS1_10TQ
#define APP_CAN_BTS2              CAN_BTS2_4TQ
#define APP_CAN_TX_ENABLE          1U
#define APP_CAN_TX_PERIOD_US     1000U /* 1 ms = 1 kHz output; requires a high-resolution scheduler */
#define APP_CAN_RX_ENABLE          1U

/* Damiao DM-IMU-L1 CAN Node Settings */
#define APP_CAN_DEFAULT_CAN_ID    0x01U
#define APP_CAN_DEFAULT_MST_ID    0x6FFU
#define APP_CAN_TX_STANDARD_ID    0x6FFU

/* Damiao Broadcast Mode:
 * 0 = Euler angle frame only (0x03: Pitch, Yaw, Roll)
 * 1 = Interleave Euler (0x03) and Gyro (0x02)
 * 2 = Cycle all 4 frames (Euler 0x03 -> Gyro 0x02 -> Accel 0x01 -> Quat 0x04) */
#define APP_CAN_DAMIAO_MODE       0U

/* UART & USB Streaming: VOFA+ JustFloat 3 channels (Yaw, Pitch, Roll) */
#define APP_UART_BAUD             2000000U
#define APP_UART_ENABLE           1U
#define APP_STREAM_DEFAULT_MODE   0U

#endif
