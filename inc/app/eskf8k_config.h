#ifndef ESKF8K_CONFIG_H
#define ESKF8K_CONFIG_H

/* Build configuration of the 8 kHz ESKF engine, ported from the
 * codex/eskf8k-* branch on 2026-10-22 so the engine can coexist with the
 * shipped VQF path in one image. The values below are the shipped branch
 * configuration (reference port, single QR state, retained low-part
 * arithmetic). Persistence, heading learning, RAM recalibration and the
 * rest/LED output variants stay off until the host side of the port exists. */

#define APP_EXPERIMENTAL_ESKF8K      1
#define APP_ESKF_REFERENCE           1
#define APP_ESKF_SINGLE_QR           4
#define APP_ESKF_Q_COMP              2
#define APP_ESKF_Q_COMP_FAST         2
#define APP_REFERENCE_FAST           1
#define APP_REFERENCE_HISTORY_DEPTH  25
#define APP_ESKF_REST_CONTROL        1
#define APP_ESKF_REST_RELAX          1
#define APP_ESKF_REST_BIAS_LEARN_YAW 1
#define APP_ESKF_REST_YAW_CLAMP_DPS  0.2f
#define APP_ESKF_REST_YAW_TAU_S      12.0f
#define APP_ESKF_BIAS_OBSERVABLE     1
#define APP_ESKF_BIAS_PERSIST        0
#define APP_ESKF_HEADING_LEARN       0
#define APP_ESKF_RAM_RECAL           0
#define APP_ESKF_Q_ANGLE             0
#define APP_ESKF_MAG_LAYERED         0

/* Defaults for every switch the engine knows about; the values above win. */
#ifndef APP_EXPERIMENTAL_ESKF8K
#define APP_EXPERIMENTAL_ESKF8K 0
#endif
#ifndef APP_STATIC6_DIAG
#define APP_STATIC6_DIAG 0
#endif
#ifndef APP_ESKF_REST_CONTROL
#define APP_ESKF_REST_CONTROL 0
#endif
#ifndef APP_USB_RX_SNAPSHOT_FIX
#define APP_USB_RX_SNAPSHOT_FIX 0
#endif
#ifndef APP_REST_CONTROL_SINGLE_POLL
#define APP_REST_CONTROL_SINGLE_POLL 0
#endif
#ifndef APP_ESKF_SIX_CAL_ONLY
#define APP_ESKF_SIX_CAL_ONLY 0
#endif
#ifndef APP_OUTPUT_NO_REST_HOLD
#define APP_OUTPUT_NO_REST_HOLD 0
#endif
#ifndef APP_REST_VISUAL_CUE
#define APP_REST_VISUAL_CUE 0
#endif
#ifndef APP_REST_INDICATOR
#define APP_REST_INDICATOR 0
#endif
#ifndef APP_ESKF_REFERENCE
#define APP_ESKF_REFERENCE 0
#endif
#ifndef APP_ESKF_SINGLE_QR
#define APP_ESKF_SINGLE_QR 0
#endif
#if APP_REST_INDICATOR >= 2
#define APP_REST_INDICATOR_RATE 4U
#else
#define APP_REST_INDICATOR_RATE 2U
#endif
#ifndef APP_UART_BATCH_EXPERIMENT
#define APP_UART_BATCH_EXPERIMENT 0
#endif
#ifndef APP_ESKF_BIAS_OBSERVABLE
#define APP_ESKF_BIAS_OBSERVABLE 0
#endif
#ifndef APP_ESKF_RAM_RECAL
#define APP_ESKF_RAM_RECAL 0
#endif
#ifndef APP_ESKF_BIAS_PERSIST
#define APP_ESKF_BIAS_PERSIST 0
#endif
#ifndef APP_ESKF_HEADING_LEARN
#define APP_ESKF_HEADING_LEARN 0
#endif
/* Exact quaternion increment angle. Off keeps the shipped {1,w*dt/2} form
 * byte-identical; on scales the vector part by (1+theta^2/12) so the
 * normalised increment rotates by exactly theta instead of 2*atan(theta/2). */
#ifndef APP_ESKF_Q_ANGLE
#define APP_ESKF_Q_ANGLE 0
#endif
#ifndef APP_ESKF_Q_COMP
#define APP_ESKF_Q_COMP 0
#endif
#ifndef APP_ESKF_Q_COMP_FAST
#define APP_ESKF_Q_COMP_FAST 0
#endif
/* Layered magnetic update. Gravity observes tilt but carries no heading
 * information; the field vector observes heading but its tilt component is
 * noisier than gravity and, in a distorted field, biased. When gravity is
 * trusted the magnetic update is therefore restricted to the heading
 * subspace -- a rotation about the world vertical -- so it cannot drag tilt.
 * When gravity is lost the full 3-D vector update returns, because the field
 * is then the only absolute reference. Off keeps the shipped path byte
 * identical. */
#ifndef APP_ESKF_MAG_LAYERED
#define APP_ESKF_MAG_LAYERED 0
#endif
/* Tilt sigma below which gravity counts as trusted for the heading-only path
 * (variance of the two error axes orthogonal to the body vertical). */
#ifndef APP_ESKF_MAG_LAYERED_TILT_RAD
#define APP_ESKF_MAG_LAYERED_TILT_RAD 0.05f
#endif
/* Age in 8 kHz gyro steps since the last accepted accelerometer update, after
 * which gravity is treated as lost (240 = 30 ms). */
#ifndef APP_ESKF_MAG_LAYERED_ACCEL_AGE
#define APP_ESKF_MAG_LAYERED_ACCEL_AGE 240u
#endif
#ifndef APP_VOFA_6AXIS
#define APP_VOFA_6AXIS 0
#endif
#ifndef APP_ESKF_REST_RELAX
#define APP_ESKF_REST_RELAX 0
#endif
/* Fresh startup bias always seeds the running filter. Persistence is a
 * separate policy: the ESKF RAM-only profile must not dirty the V4 model. */
#ifndef APP_GYR_STARTUP_BIAS_SAVE_ENABLE
#define APP_GYR_STARTUP_BIAS_SAVE_ENABLE 1U
#endif
/* ESKF uses instantaneous input after startup seed, before residual bias.
 * These gates are independent of VQF tuning and the output rate guard. */
#if APP_ESKF_REST_RELAX
#define APP_ESKF_REST_GYR_DPS 0.8f
#define APP_ESKF_REST_ACC_MS2 0.20f
#else
#define APP_ESKF_REST_GYR_DPS 0.6f
#define APP_ESKF_REST_ACC_MS2 0.15f
#endif


#endif /* ESKF8K_CONFIG_H */
