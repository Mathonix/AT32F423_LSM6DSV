# Accelerometer six-face calibration ? 2026-09-12

## Applied provisionally
- Source: `build/logs/acc_six_faces_20260912_231659_recheck_z_234958_fit.json`.
- Original -Z dataset preserved; replacement capture passed all fit checks.
- `inc/calibration/acc_calibration.h`: corrected_g = (nominal_g - bias_g) * gain.
- Applied exactly once at both startup and runtime conversion sites in `src/app/main.c`, before g -> m/s^2 conversion.
- Gyro calibration, VQF tuning, magnetic calibration and fusion-OFF mode unchanged.
- Existing vqf_live / vqf_tune_live / serial VOFA ax,ay,az remain UNCORRECTED nominal g. Existing ABI layouts unchanged (ELF symbol addresses may move after rebuild).
- New `acc_cal_live`: 36 bytes, `<3I6f>`; magic 0x4143434C, odd/even seq, millis, raw_g[3], corrected_g[3]. Updated at 20 Hz with memory barriers. Corrected telemetry comes from actual VQF acceleration input converted back to g.

## Build / hardware verification
- Forced full build (`make -B -j2`): successful, no compiler warnings/errors found.
- DAPLink flash successful; non-halting full BIN readback matches all 44848 bytes.
- `tools/dap/dap_verify_acc_calibration.py`: 30s, 572 fresh coherent samples. Calibration arithmetic max error 1.172e-7 g.
- Fusion 1995?1996 Hz; skip delta 0; rest 100%; magnetic fusion disabled.
- Raw mean [0.02983924, 0.01899190, -0.99886688] g.
- Corrected mean [0.03192684, 0.01804010, -1.00031316] g.
- Mean sample norm raw 0.99949522 g; corrected 1.00098741 g.
- This pose's norm error increased, despite successful arithmetic verification. Do not claim accuracy improved globally.
- Verification: `build/logs/acc_cal_verified_20260912_235429.json`.

## Remaining validation
This is a new time window at the same -Z pose, NOT independent multi-pose validation. Acquire new static poses (including oblique poses) and compare raw/corrected norm errors before claiming improvement. Diagonal six-face fitting assumes nominal 1g and axis-aligned placement; residual placement tilt affects scale. No nonorthogonality, mounting or temperature calibration was performed. Refit or disable the provisional correction if independent validation does not support it. Does not establish reduced yaw drift.
