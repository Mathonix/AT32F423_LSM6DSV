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
/* The three entries below only feed the residual-bias learner that lives inside
 * `#if APP_ESKF_SINGLE_QR < 2` in accel(). With the shipped SINGLE_QR = 4 the
 * reference_rest tracker overwrites residual_bias on every gyro sample, so they
 * have no effect on the running build: change `rest_tau_s` (runtime tuning,
 * 0x68) instead. Kept so the single-QR-disabled build still compiles. */
#define APP_ESKF_REST_BIAS_LEARN_YAW 1
#define APP_ESKF_REST_YAW_CLAMP_DPS  0.2f
#define APP_ESKF_REST_YAW_TAU_S      12.0f
#define APP_ESKF_BIAS_OBSERVABLE     1
#define APP_ESKF_BIAS_PERSIST        0
#define APP_ESKF_HEADING_LEARN       0
#define APP_ESKF_RAM_RECAL           0
#define APP_ESKF_Q_ANGLE             0
#define APP_ESKF_MAG_LAYERED         1 /* heading-only while gravity is trusted */

/* Accelerometer unit-vector observation noise. Measured on this board, still,
 * 1 kHz for 15 s: direction RMS 0.167 deg, p95 0.289 deg, max 0.481 deg. That
 * figure is per input sample; the observation is now the mean of
 * APP_ESKF_ACC_OBS_DIV of them, so its direction noise is 0.0835 deg (1.46e-3
 * rad) and 0.0045 rad (0.258 deg) keeps a 2.7x margin. The previous 0.035 rad
 * (2.0 deg) was an unmeasured placeholder and cost about 34x in observation
 * variance. Do not go below the measured mean: the filter would start to trust
 * single clean samples over the gyro, which is exactly what the magnitude gate
 * exists to prevent. */
#ifndef APP_ESKF_ACC_SIGMA_RAD
#define APP_ESKF_ACC_SIGMA_RAD 0.0045f
#endif

/* Rotational-rate gate on the gravity observation, six-axis.
 *
 * The magnitude gates alone cannot see horizontal linear acceleration: for a
 * horizontal acceleration of a_h the measured |a| is only g*sqrt(1+a_h^2), so
 * the ||a|-g| < 0.25 m/s^2 gate does not trip until a_h reaches 0.23 g - and
 * by then the acceleration direction is already atan(0.23) = 12.9 deg away
 * from gravity. A hand-held rotation is almost always accompanied by exactly
 * that kind of translation, so the gyro magnitude is used as the cheaper,
 * earlier indicator. Corrected rate (bias removed) is low-passed first. */
#ifndef APP_ESKF_ACC_RATE_GATE_DPS
#define APP_ESKF_ACC_RATE_GATE_DPS 20.0f
#endif
#ifndef APP_ESKF_ACC_RATE_LPF_MS
#define APP_ESKF_ACC_RATE_LPF_MS 20.0f
#endif

/* Tilt variance inflation while the gravity observation is unavailable.
 *
 * Without it a rejected observation leaves P untouched, so after a long
 * manoeuvre the gain is still the resting one and the tilt crawls back. The
 * kernel adds extra process noise once the gap passes START, for SPAN seconds,
 * sized so SPAN seconds of closure add DEG of tilt uncertainty in quadrature.
 * Applies to six-axis only: the nine-axis path has the magnetic observation and
 * was validated without this term. */
#ifndef APP_ESKF_TILT_INFLATE_DEG
#define APP_ESKF_TILT_INFLATE_DEG 3.0f
#endif
#ifndef APP_ESKF_TILT_INFLATE_SPAN_S
#define APP_ESKF_TILT_INFLATE_SPAN_S 20.0f
#endif
#ifndef APP_ESKF_TILT_INFLATE_START_S
#define APP_ESKF_TILT_INFLATE_START_S 0.05f
#endif

/* Accelerometer observation rate. The engine is fed at 2 kHz and averages
 * APP_ESKF_ACC_OBS_DIV of those inputs into one gravity observation, so this
 * value sets the rate: 10 -> 200 Hz, 4 -> 500 Hz.
 *
 * The average replaces the single sample the old code kept while discarding
 * the other three, so the extra inputs are no longer wasted: the direction and
 * magnitude noise of the observation drop by sqrt(APP_ESKF_ACC_OBS_DIV).
 *
 * The closed-loop time constant of a square-root filter scales linearly with
 * the observation sigma, because the steady-state gain grows with sqrt(dt)
 * while R stays fixed. Measured in tests/test_eskf8k_sim.cpp, 3 deg step from
 * steady state: 200 Hz -> 5.52 s to 0.10 deg, 500 Hz -> 3.54 s, and the same
 * 500 Hz with half the sigma -> about 1.8 s. The cost is one extra 3x3 QR per
 * extra observation. */
#ifndef APP_ESKF_ACC_OBS_DIV
#define APP_ESKF_ACC_OBS_DIV 4
#endif

/* Magnitude gate on the averaged gravity observation, in m/s^2.
 *
 * The rate gate cannot see horizontal translation and the magnitude test is
 * the only other defence: 0.2 g of translation moves the measured direction
 * 11.3 deg while ||a|-g| is just 0.20 m/s^2. The old single-sample gate of
 * 0.25 m/s^2 therefore accepted that shape whole and produced an 11.3 deg
 * false tilt. Averaging APP_ESKF_ACC_OBS_DIV samples lowers the magnitude
 * noise by sqrt(N), so the gate can be tightened to 0.10 m/s^2 (about 13 sigma
 * over the measured 0.03 m/s^2 per-sample noise) without losing good samples.
 *
 * A unit whose accelerometer reads 1-2 % high or low would be rejected by the
 * tight gate forever and would lose gravity completely, so a sustained
 * rejection falls back to the previous 0.25 m/s^2 gate after RELAX_S and stays
 * there for HOLD_S before retrying the tight one. Set the tight value from the
 * measured residual (static ||a|-g| over several orientations), not lower than
 * 3 sigma of that residual. */
#ifndef APP_ESKF_ACC_MAG_GATE
#define APP_ESKF_ACC_MAG_GATE 0.10f
#endif
#ifndef APP_ESKF_ACC_MAG_GATE_LOOSE
#define APP_ESKF_ACC_MAG_GATE_LOOSE 0.25f
#endif
#ifndef APP_ESKF_ACC_MAG_GATE_RELAX_S
#define APP_ESKF_ACC_MAG_GATE_RELAX_S 5.0f
#endif
#ifndef APP_ESKF_ACC_MAG_GATE_LOOSE_HOLD_S
#define APP_ESKF_ACC_MAG_GATE_LOOSE_HOLD_S 2.0f
#endif

/* Covariance propagation interval, counted in gyro samples. The quaternion
 * keeps integrating at 8 kHz; the 3x3 covariance only has to keep up with the
 * gravity observations (APP_ESKF_ACC_OBS_DIV apart) and the process model is
 * linear over half a millisecond, so one QR covers the whole interval.
 *
 * Cost per propagation is 12 Givens rotations plus the P rebuild, ~600 cycles.
 * At 8 kHz that is about 4.8 M cycles/s (~3.2 % of 150 MHz); 4 brings it to
 * 1.2 M. Measured in tests/test_eskf8k_sim.cpp: a 15 s 200 dps shake then rest
 * gives the same 3.57 deg worst error, 0.028 deg rest p2p and 3.5 s step
 * response for 1, 2 and 4. */
#ifndef APP_ESKF_COV_DIV
#define APP_ESKF_COV_DIV 4
#endif

/* Rest-tracker guard rails.
 *
 * The stationary test reads the gyro only, so a constant rotation below the
 * threshold (~0.32 dps, 0.11 dps once enough evidence accumulated) is
 * indistinguishable from bias and gets learned as bias. Gravity separates the
 * two for tilt: a slow tilt moves the measured gravity direction, a bias does
 * not. APP_ESKF_REST_ACCEL_GUARD adds that check; a drift past
 * APP_ESKF_REST_DIR_GUARD_RAD leaves rest again and the existing history path
 * rolls the bias back.
 *
 * Off by default: it pauses bias learning whenever the board is slowly tipped,
 * which is correct for tilt but changes the validated zero-drift behaviour on
 * a vibrating platform. Enable it after re-running the zero-drift acceptance
 * with the board on its normal mount.
 *
 * APP_ESKF_REST_BIAS_CLAMP_RAD saturates the learned bias (2 dps). The
 * single-state path already discards a larger estimate; saturating keeps the
 * state continuous instead of stepping it to zero. */
#ifndef APP_ESKF_REST_ACCEL_GUARD
#define APP_ESKF_REST_ACCEL_GUARD 0
#endif
#ifndef APP_ESKF_REST_DIR_GUARD_RAD
#define APP_ESKF_REST_DIR_GUARD_RAD 0.005f
#endif
#ifndef APP_ESKF_REST_DIR_TAU_S
#define APP_ESKF_REST_DIR_TAU_S 0.50f
#endif
#ifndef APP_ESKF_REST_DIR_SLOW_S
#define APP_ESKF_REST_DIR_SLOW_S 4.0f
#endif
#ifndef APP_ESKF_REST_BIAS_CLAMP_RAD
#define APP_ESKF_REST_BIAS_CLAMP_RAD 0.0349f
#endif

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
/* Published attitude no longer waits out hold_s after a stop. Rest still
 * blends with the gear's rest_tau. ZARU heading lock is separate. */
#define APP_OUTPUT_NO_REST_HOLD 1
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



/* Magnetic validation fused from the VQF implementation: inside the soft band
 * the reference tracker follows slowly, and a hard excursion that persists for
 * REJECT_LIMIT observations is accepted with that slow tracker instead of being
 * rejected forever (which used to lock the engine out after the board moved). */
#ifndef APP_ESKF_MAG_SOFT_BAND
#define APP_ESKF_MAG_SOFT_BAND  0.10f
#endif
#ifndef APP_ESKF_MAG_HARD_BAND
#define APP_ESKF_MAG_HARD_BAND  0.20f
#endif
#ifndef APP_ESKF_MAG_REJECT_LIMIT
#define APP_ESKF_MAG_REJECT_LIMIT 500U
#endif
#ifndef APP_ESKF_MAG_ALPHA_SOFT
#define APP_ESKF_MAG_ALPHA_SOFT 0.001f
#endif
#ifndef APP_ESKF_MAG_SOFT_DIP
#define APP_ESKF_MAG_SOFT_DIP   0.087f /* 5 deg */
#endif
#ifndef APP_ESKF_MAG_HARD_DIP
#define APP_ESKF_MAG_HARD_DIP   0.175f /* 10 deg */
#endif
/* A hard-band excursion is rejected outright, VQF-style: nothing is observed
 * and the magnitude tracker is frozen, so fusion resumes by itself once the
 * field returns. REJECT_LIMIT only latches the "paused this long" flag that
 * the status LED and the diagnostic bit report. */
/* Legacy rejection strategy: after REJECT_LIMIT rejections the field is
 * accepted with this sigma multiplier instead of waiting for recovery. Only
 * used when the VQF-style gate is switched off. */
#ifndef APP_ESKF_MAG_DEGRADED_SIGMA
#define APP_ESKF_MAG_DEGRADED_SIGMA 4.0f
#endif
/* Plausibility window for the field used to lock the heading reference.
 * Earth's field here is about 45 uT; a reference captured inside a local
 * source would define that source as north for the whole session and no
 * later check could detect it. Outside this window the engine keeps waiting
 * instead of locking. */
#ifndef APP_ESKF_MAG_LOCK_MIN_UT
#define APP_ESKF_MAG_LOCK_MIN_UT 20.0f
#endif
#ifndef APP_ESKF_MAG_LOCK_MAX_UT
#define APP_ESKF_MAG_LOCK_MAX_UT 80.0f
#endif

/* Calibration health. The magnitude a correct hard-iron removal produces does
 * not depend on attitude, so the corrected |B| at rest is a fiducial: store the
 * value the calibration was fitted to and watch it. A changed hard-iron offset
 * (this board picks up ~69 uT when a magnet comes close) moves it by tens of
 * percent, while the pose-dependent residual of the fit stays within a few
 * percent. That drift is what makes the absolute heading wrong without any
 * other symptom.
 *
 * The deviation is measured on a 1 s tracker of the incoming magnitude, not on
 * the accept-path reference: the reference freezes while the field is being
 * rejected, which is exactly the case this is meant to catch.
 *
 * 15 % entry / 8 % exit sits above the residual of the current fit (5.9 % rms)
 * and below the observed failure (41.5 -> 32.9 uT is -21 %). The verdict needs
 * 8 s of continuous rest so a magnet passing by does not raise it. Counts are
 * observations at 100 Hz. */
#ifndef APP_ESKF_MAG_HEALTH_ENTER
#define APP_ESKF_MAG_HEALTH_ENTER 0.15f
#endif
#ifndef APP_ESKF_MAG_HEALTH_EXIT
#define APP_ESKF_MAG_HEALTH_EXIT  0.08f
#endif
#ifndef APP_ESKF_MAG_HEALTH_HOLD_N
#define APP_ESKF_MAG_HEALTH_HOLD_N 800U  /* 8 s at 100 Hz */
#endif
#ifndef APP_ESKF_MAG_HEALTH_CLEAR_N
#define APP_ESKF_MAG_HEALTH_CLEAR_N 200U /* 2 s at 100 Hz */
#endif
#endif /* ESKF8K_CONFIG_H */
