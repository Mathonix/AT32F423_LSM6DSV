# AT32 Motion Studio — Host Protocol Baseline

Source of truth: the audited 2026-09-12 `AT32F423_LSM6DSV` firmware snapshot in the parent repository. This document describes only behavior visible in source. It does not reserve or invent future command IDs.

## 1. Production telemetry transport

- Peripheral: USART4
- TX: PA0 / USART4_TX / MUX8
- RX: PA1 / USART4_RX / MUX8
- Baud: 2,000,000
- Current application behavior: device-to-host telemetry stream
- Framing: VOFA+ JustFloat compatible

### Frame

```text
offset 0   64 bytes  = 16 × IEEE-754 float32, little-endian
offset 64   4 bytes  = 00 00 80 7F
frame size           = 68 bytes
```

There is no CRC field in this frame. Parser health is therefore expressed as framing/resynchronization errors, not fabricated CRC errors.

### Channels

| Ch | Field | Unit / meaning |
|---:|---|---|
| 0 | roll | degree |
| 1 | pitch | degree |
| 2 | yaw | degree |
| 3 | qw | quaternion scalar |
| 4 | qx | quaternion X |
| 5 | qy | quaternion Y |
| 6 | qz | quaternion Z |
| 7 | gx | deg/s |
| 8 | gy | deg/s |
| 9 | gz | deg/s |
| 10 | ax | g |
| 11 | ay | g |
| 12 | az | g |
| 13 | vqf_us | VQF execution time, µs |
| 14 | fusion_hz | measured fusion rate, Hz |
| 15 | late | cumulative UART DMA busy/fail counter |

The bundled `tools/vofa_1khz_log.py` uses the same 16-channel/68-byte interpretation.

## 2. DAP-only diagnostic data

`vqf_live` exposes additional RAM diagnostics to a debugger/DAP workflow, including fields such as WHO_AM_I, initialization error, clock, millis, output rate/counters and the live pose/sensor values. Those fields are **not silently treated as UART fields** by this host.

The expected LSM6DSV WHO_AM_I value in the audited source is `0x70`.

## 3. Host commands

No verified host-command request/response protocol is present in this audited application snapshot. The desktop therefore does not send reset-zero, calibration-save, configuration, ping, system-info or similar made-up commands.

## 4. USB, CAN and bootloader

Vendor peripheral drivers in an MCU SDK are not evidence of an application protocol. In the audited snapshot there is no verified:

- USB application transport protocol;
- application-level CAN telemetry/control protocol;
- resident product bootloader update protocol;
- USB/UART/CAN firmware-update framing.

The corresponding desktop pages remain disabled/unsupported until firmware source and hardware captures establish those contracts.

## 5. Parser policy

The Rust parser:

1. looks for the exact four-byte JustFloat tail;
2. requires exactly 64 payload bytes immediately before the tail;
3. decodes 16 little-endian float32 values;
4. rejects non-finite or implausibly corrupt values;
5. accounts for discarded bytes as resynchronization bytes;
6. keeps its buffer bounded.

Unit tests cover exact layout, fragmented input, garbage resynchronization and NaN rejection.
