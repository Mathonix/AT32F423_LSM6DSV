#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Central application configuration. Keep all board-level tuning here. */
#define APP_FUSION_HZ             2000U
#define APP_GYR_LPF_CUTOFF_HZ    30.0f

/* LSM6DSV temperature telemetry is always published. */
#define APP_GYR_TEMP_COMP_ENABLE       1U
#define APP_GYR_TEMP_REF_C             25.0f
/* Maximum temperature difference for selecting a temperature-matched
 * historical gyro bias during startup fallback. */
#define APP_GYR_BIAS_TEMP_WINDOW_C     5.0f

/* Fast startup: use a valid temperature-matched history record, then refine
 * it only after confirmed rest. A save is attempted at most once per boot. */
#ifndef APP_GYR_FAST_START_ENABLE
/* Default for erased/legacy settings only. A saved host startup setting
 * overrides this value; both startup paths are present in the firmware. */
#define APP_GYR_FAST_START_ENABLE      0U
#endif
#define APP_GYR_FAST_START_REST_MS     1000U
#define APP_GYR_FAST_START_SAVE_MS     5000U
#define APP_GYR_FAST_START_BLEND       0.005f
#define APP_GYR_FAST_START_SAVE_DELTA_DPS 0.02f
#define APP_GYR_FAST_START_SAVE_TEMP_C 2.0f
#define APP_GYR_TEMP_LPF_HZ            1.0f
#define APP_GYR_TEMP_COEFF_X_DPS_PER_C 0.0f
#define APP_GYR_TEMP_COEFF_Y_DPS_PER_C 0.0f
#define APP_GYR_TEMP_COEFF_Z_DPS_PER_C 0.0f

/* Normal boot averages a configurable stationary window (default 2 seconds). */
#define APP_GYR_INIT_DEFAULT_MS   2000U
#define APP_GYR_INIT_MIN_MS       100U
#define APP_GYR_INIT_MAX_MS       60000U
#define APP_CAL_REST_SECONDS      2.0f
#define APP_CAL_DROP_MS           1000U
#define APP_CAL_BLOCK_SAMPLES     32U
#define APP_CAL_BLOCK_MAX         256U
#define APP_CAL_TRIM_PERCENT      10U
#define APP_SFLP_BIAS_ENABLE      0U
#define APP_SFLP_BIAS_SETTLE_MS   1000U
#define APP_SFLP_BIAS_MAX_DPS     5.0f
#define APP_CAL_GYR_REST_DPS      1.0f
#define APP_CAL_ACC_REST_MS2      0.8f
/* Persistent gyro-bias history. The final two 2-KB sectors are reserved for
 * calibration data and are outside the application image. */
#define APP_GYR_BIAS_FLASH_SLOT0_ADDR 0x0803E800U
#define APP_GYR_BIAS_FLASH_SLOT1_ADDR 0x0803F800U
#define APP_FUSION_SETTINGS_ADDR       0x0803D800U
#define APP_GYR_DEFAULT_BIAS_X_DPS     0.0f
#define APP_GYR_DEFAULT_BIAS_Y_DPS     0.0f
#define APP_GYR_DEFAULT_BIAS_Z_DPS     0.0f

/* Full VQF parameters */
#define APP_VQF_MOTION_BIAS_ENABLE 0U
#define APP_VQF_TAU_ACC           3.0f
#define APP_VQF_TAU_MAG           2.0f

/* Enable calibrated IST8310 updates in Full VQF for 9-axis yaw stabilization. */
#ifndef APP_MAG_FUSION_ENABLE
#define APP_MAG_FUSION_ENABLE     1U
#endif
#define APP_MAG_VQF_UPDATE_DIV    5U
/* Relative-yaw output reference: 0 = VQF/KF yaw, 1 = yaw relative to boot.
 * This changes only the published yaw reference; VQF remains 9-axis. */
#define APP_RELATIVE_YAW_ENABLE     0U
/* When VQF rejects magnetometer updates, keep the normal mode color longer
 * than the green (effective 6-axis) warning color. */
#define APP_WS2812_MAG_REJECT_BASE_MS   750U
#define APP_WS2812_MAG_REJECT_GREEN_MS  250U

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

/* Non-blocking six-face calibration; saved parameters apply before fusion. */
#ifndef APP_ACC_CAL_ENABLE
#define APP_ACC_CAL_ENABLE          1U
#endif

/* Reserved calibration sector immediately after the application image. */
#define APP_ACC_CAL_FLASH_ADDR       0x0803C000U
#define APP_ACC_CAL_FACE_SECONDS     1.0f
#define APP_ACC_CAL_FACE_TIMEOUT_S   60.0f
#define APP_ACC_CAL_GYR_REST_DPS     2.0f
#define APP_ACC_CAL_NORM_TOL_G       0.15f
#define APP_ACC_CAL_DOMINANT_MIN_G   0.75f
#define APP_ACC_CAL_OTHER_MAX_G      0.20f
#define APP_ACC_CAL_STABLE_MS        500U
#define APP_ACC_CAL_SAMPLE_MS        500U
#define APP_ACC_CAL_MAX_SAMPLE_GAP_MS 20U
#define APP_ACC_CAL_MAX_STDDEV_G     0.015f
#define APP_ACC_CAL_MAX_STEP_G       0.025f
#define APP_ACC_CAL_MAX_BIAS_G       0.15f
#define APP_ACC_CAL_MIN_SCALE        0.85f
#define APP_ACC_CAL_MAX_SCALE        1.15f
/* Fit tolerates placement tilt (folded into the unit-norm model); the tilt
 * limit is only a numerical guard, the norm error is a convergence check. */
#define APP_ACC_CAL_MAX_TILT_G       0.50f
#define APP_ACC_CAL_MAX_NORM_ERR_G   0.001f


#endif
