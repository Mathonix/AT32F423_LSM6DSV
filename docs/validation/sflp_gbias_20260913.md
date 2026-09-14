# LSM6DSV SFLP GBIAS integration and A/B test

Date: 2026-09-13

## Correct FIFO format

The SFLP gyroscope-bias FIFO frame uses tag `0x16` and a 7-byte frame:

```text
tag + X(int16 LE) + Y(int16 LE) + Z(int16 LE)
```

It is not an IEEE half-float payload. The values use the +/-125 dps
sensitivity:

```text
1 LSB = 4.375 mdps = 0.004375 dps
```

This matches ST's `lsm6dsv_sensor_fusion.c` example, which reads the payload as
`int16_t` and converts it with `lsm6dsv_from_fs125_to_mdps()`.

## Hardware verification

With the board stationary, the raw gyro average and SFLP GBIAS were compared:

| Source | X (dps) | Y (dps) | Z (dps) |
|---|---:|---:|---:|
| Raw gyro average, 200 samples | +0.285600 | +0.066850 | -0.350000 |
| SFLP GBIAS FIFO | +0.284375 | +0.065625 | -0.345625 |

The difference is approximately one GBIAS LSB, confirming both the byte order
and the scale factor.

## Firmware integration

New startup path:

1. Configure the LSM6DSV at 30 Hz and enable SFLP game + GBIAS FIFO output.
2. Wait 1000 ms for the SFLP bias estimate to settle.
3. Average up to 32 GBIAS frames.
4. Stop the sensor and restore the normal HAODR 2 kHz configuration.
5. Use the averaged SFLP bias as the initial VQF bias when all axes are below
   5 dps; otherwise fall back to the MCU stationary average.

Relevant code:

```text
inc/app/app_config.h
inc/drivers/lsm6dsv.h
src/lsm6dsv.c: lsm6dsv_read_sflp_gbias()
src/app/main.c: startup acquisition and VQF initialization
```

## 60 s stationary A/B result

Both builds used HAODR, 2 kHz fusion, 30 Hz gyro LPF, and six-axis VQF.

| Initial bias source | Yaw std (deg) | Yaw p2p (deg) | Linear drift (deg/min) | Fusion rate |
|---|---:|---:|---:|---:|
| SFLP GBIAS | 0.008064 | 0.031739 | -0.02566 | 1994-1995 Hz |
| MCU 1 s stationary average | 0.010846 | 0.036332 | -0.03530 | 1994-1995 Hz |

The SFLP initialization produced about 25% lower short-term yaw standard
deviation and about 27% lower linear drift in this run. The two bias estimates
were very close, so the main benefit is that SFLP provides an independent,
sensor-side bias estimate while preserving the MCU fallback.

## Diagnostic tools

```text
tools/dap/dap_sflp_attitude.py     10 x 1 s spot attitude + GBIAS samples
tools/dap/dap_sflp_gbias_read.py   raw gyro vs decoded SFLP GBIAS
tools/dap/dap_sflp_diag.py         FIFO tag/level diagnostic
tools/dap/dap_yaw_kf_compare.py     live VQF/POSE telemetry and 60 s drift report
```
