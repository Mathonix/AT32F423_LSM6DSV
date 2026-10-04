#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Central application configuration. Keep all board-level tuning here. */

/* Application label: YYYYMMDD and one letter. The first build of a day is
 * "a". Each later build on that same day uses the next letter. */
#ifdef APP_USER_BL_UPDATE
#define APP_FIRMWARE_VERSION "20261005f"
#else
#define APP_FIRMWARE_VERSION "20261005e"
#endif

#define APP_FUSION_HZ             2000U
#define APP_GYR_LPF_CUTOFF_HZ    30.0f

/* LSM6DSV temperature telemetry is always published. */
#define APP_GYR_TEMP_COMP_ENABLE       1U
#define APP_GYR_TEMP_REF_C             25.0f
/* Maximum temperature difference for selecting a temperature-matched
 * historical gyro bias during startup fallback. */
#define APP_GYR_BIAS_TEMP_WINDOW_C     1.0f

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
#define APP_GYR_INIT_MIN_MS       0U
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
/* Startup rest is decided from 100-ms blocks. Raw samples can exceed the
 * mean gate because of noise; only gross excursions reject immediately. */
#define APP_STARTUP_GYR_GROSS_DPS 5.0f
#define APP_STARTUP_ACC_GROSS_MS2 2.4f
#define APP_STARTUP_BLOCK_MS     100U
/* Measured stationary Y-axis noise is ~0.177 dps at +/-4000 dps.
 * This is a motion rejection threshold, not the accuracy of the mean. */
#define APP_STARTUP_GYR_STD_DPS   0.30f
#define APP_STARTUP_ACC_STD_MS2   0.15f
/* Persistent gyro-bias history, up to 50 samples per 2 KB slot. The slots
 * sit outside the application image. Version 2 records remain readable. */
#define APP_GYR_BIAS_FLASH_SLOT0_ADDR 0x0803E800U
#define APP_GYR_BIAS_FLASH_SLOT1_ADDR 0x0803F800U
#define APP_FUSION_SETTINGS_ADDR       0x0803D800U
#define APP_GYR_DEFAULT_BIAS_X_DPS     0.0f
#define APP_GYR_DEFAULT_BIAS_Y_DPS     0.0f
#define APP_GYR_DEFAULT_BIAS_Z_DPS     0.0f

/* Runtime VQF bias estimators. Startup averaging remains independent of
 * these switches; rest detection remains available for status and ZARU. */
#define APP_VQF_MOTION_BIAS_ENABLE 1U
#define APP_VQF_REST_BIAS_ENABLE   1U
/* Balanced-gear fallback used only before a profile is applied. */
#define APP_VQF_TAU_ACC           2.5f
#define APP_VQF_TAU_MAG           4.0f

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

/* Full VQF bias estimator shared by every gear. Motion bias uses the
 * 0.1 °/s sigma below; vertical forgetting stays at the VQF default.
 * Rest trial: 0.6 °/s, 0.15 m/s², confirm 1.0 s, sigma 0.035 °/s.
 * Every profile row copies the same two gates; apply_profile overwrites them. */
#define APP_VQF_BIAS_SIGMA_REST_DPS       0.035f
/* Constructor value already used when vqf_init does not assign it. */
#define APP_VQF_BIAS_SIGMA_INIT_DPS       0.5f
#define APP_VQF_BIAS_SIGMA_MOTION_DPS     0.1f
#define APP_VQF_BIAS_CLIP_DPS             2.0f
#define APP_VQF_BIAS_FORGETTING_TIME_S  100.0f
#define APP_VQF_REST_FILTER_TAU_S   0.5f
#define APP_VQF_REST_GYR_DPS      0.6f
#define APP_VQF_REST_ACC_MS2      0.15f
#define APP_VQF_REST_MIN_SECONDS   1.0f
/* Static initialization may only raise rest gates and rest sigma up to these
 * ceilings. The floors are the current firmware values, so a quiet capture
 * cannot replace the running tune with a smaller number. */
#define APP_VQF_CAL_MIN_REST_GYR_DPS      APP_VQF_REST_GYR_DPS
#define APP_VQF_CAL_MAX_REST_GYR_DPS      1.20f
#define APP_VQF_CAL_MIN_REST_ACC_MS2      APP_VQF_REST_ACC_MS2
#define APP_VQF_CAL_MAX_REST_ACC_MS2      0.40f
#define APP_VQF_CAL_MIN_BIAS_SIGMA_INIT   0.10f
#define APP_VQF_CAL_MAX_BIAS_SIGMA_INIT   1.00f
#define APP_VQF_CAL_MIN_BIAS_SIGMA_REST   APP_VQF_BIAS_SIGMA_REST_DPS
#define APP_VQF_CAL_MAX_BIAS_SIGMA_REST   0.06f
#define APP_VQF_CAL_MAX_GYRO_STD_DPS      0.15f
#define APP_VQF_CAL_MAX_ACC_STD_MS2       0.10f
#define APP_VQF_CAL_MAX_TEMP_SPAN_C       2.0f
#define APP_VQF_CAL_MAX_BIAS_DPS          2.0f
#define APP_VQF_CAL_MAX_BIAS_DRIFT_DPS    0.05f
#define APP_VQF_CAL_REST_GYR_SCALE        1.5f
#define APP_VQF_CAL_REST_ACC_SCALE        2.0f
#define APP_VQF_CAL_SIGMA_INIT_SCALE      3.0f
#define APP_VQF_STATIC_CAL_PREPARE_MS     5000U
#define APP_VQF_STATIC_CAL_COLLECT_MS     60000U
#define APP_VQF_STATIC_CAL_PREPARE_CAP_MS 120000U
#define APP_VQF_STATIC_CAL_MOVE_GYR_DPS   2.0f
#define APP_VQF_STATIC_CAL_MOVE_ACC_MS2   0.8f
#define APP_VQF_STATIC_GRAVITY_MS2        9.80665f
#define APP_VQF_STATIC_FLASH_SLOT0        0x0803E000U
#define APP_VQF_STATIC_FLASH_SLOT1        0x0803F000U
/* 60 s at 2 kHz, with a 10 percent drop budget. */
#define APP_VQF_STATIC_CAL_MIN_SAMPLES \
  ((APP_VQF_STATIC_CAL_COLLECT_MS * APP_FUSION_HZ * 9U) / 10000U)
/* Output heading hold for FUSION_PROFILE_ZARU. Degrees/s are bias-corrected
 * gyro residual, not raw gyro and not the VQF rest gate. Enter is the 10 ms
 * RMS; exit is the unfiltered norm. These macros are the compiled defaults;
 * a saved host setting overrides them at boot. 0.30–0.70 °/s is hysteresis.
 * Enter 0.20 strict / 0.30 default / 0.40 locks sooner and swallows slow turns.
 * Exit 0.50 tight / 0.70 default / 1.00 very quiet but swallows more.
 * Enter filter 5 ms fast, 10 ms default, 50 ms starts to lag.
 * Enter confirm 20 ms eager, 50 ms default, 100 ms slow.
 * Exit confirm 1 ms sharp, 3 ms default, 10 ms still acceptable. */
#define APP_ZARU_ENABLE               1U
#define APP_ZARU_ENTER_DPS            0.30f
#define APP_ZARU_EXIT_DPS             0.70f
#define APP_ZARU_ENTER_FILTER_MS      10U
#define APP_ZARU_ENTER_CONFIRM_MS     50U
#define APP_ZARU_EXIT_CONFIRM_MS      3U
#define APP_ZARU_ACC_DEV_MS2          0.15f
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
