# AT32 Motion Studio — Repository Audit

Audit baseline: `AT32F423_LSM6DSV-fixed-20260912`.

## Executive conclusion

The audited firmware is a focused AT32F423KCU7 + LSM6DSV six-axis motion firmware. It is **not** yet a general USB/CAN/bootloader device firmware. The only production host telemetry path present in the application code is USART4 TX at 2,000,000 baud using VOFA+ JustFloat frames. DAP/SWD tools can inspect a richer `vqf_live` RAM structure for validation and diagnostics.

Host software must therefore treat UART motion telemetry as supported, while USB application protocol, CAN application protocol, host-driven calibration, magnetometer telemetry, device configuration commands, and firmware upgrade are unsupported by this baseline.

## Audited paths

- `README.md`, `FIXES.md`, `problem.md`
- `src/`, `inc/`
- `tools/`
- project `Makefile`
- vendor library presence was checked only to distinguish peripheral driver availability from application-level implementation

The requested paths `bootloader/`, `tools/usb_host/`, and `tools/electron_host/` do not exist in this snapshot. Their absence is treated as an explicit capability gap, not as evidence of hidden support.

## Real firmware data path

`src/main.c` performs:

1. AT32 clock and BSP initialization.
2. LSM6DSV SPI1 initialization.
3. LSM6DSV 2 kHz configuration.
4. One-second contiguous-rest startup calibration.
5. BasicVQF update at a nominal 2 kHz.
6. A 1 kHz snapshot every second fused sample.
7. USART4 DMA transmission of a fixed JustFloat frame.

The USART4 configuration is defined by `inc/bsp.h` and `src/bsp.c`:

- TX: PA0 / USART4_TX / MUX8
- RX: PA1 / USART4_RX / MUX8
- baud: 2,000,000
- 8 data bits, 1 stop bit
- TX uses DMA1 channel 1

The current application sends telemetry only; no host command parser is present in the audited application.

## UART JustFloat frame

`src/main.c::vofa_send_justfloat()` sends 16 little-endian IEEE-754 `float32` values followed by the 4-byte VOFA JustFloat trailer `00 00 80 7F`.

| Index | Field | Unit / meaning |
|---:|---|---|
| 0 | roll | deg |
| 1 | pitch | deg |
| 2 | yaw | deg |
| 3 | qw | quaternion |
| 4 | qx | quaternion |
| 5 | qy | quaternion |
| 6 | qz | quaternion |
| 7 | gx | dps |
| 8 | gy | dps |
| 9 | gz | dps |
| 10 | ax | g |
| 11 | ay | g |
| 12 | az | g |
| 13 | vqf_us | VQF execution time in microseconds, encoded as float |
| 14 | fusion_hz | measured fusion rate, encoded as float |
| 15 | late | cumulative failed/busy UART DMA send attempts, encoded as float |

Frame size: `16 * 4 + 4 = 68 bytes`.

`tools/vofa_1khz_log.py` independently expects exactly the same 16-channel frame at 2 Mbps and is the primary host-side parser reference.

## DAP/SWD-only diagnostics

`inc/vqf_live.h` exposes a RAM structure used by the DAP tools. Fields include:

- magic / sequence
- init error
- WHO_AM_I
- system clock
- milliseconds
- fusion Hz / output Hz
- fusion count / skipped sample count
- VQF execution time
- roll / pitch / yaw
- quaternion
- gyro XYZ
- acceleration XYZ

Several of these values are **not** in the UART frame: WHO_AM_I, init error, MCU milliseconds, output Hz, fusion count and skipped sample count. AT32 Motion Studio must not display them as live UART values.

## Sensor and fusion behavior

- LSM6DSV WHO_AM_I expected value: `0x70`.
- Current driver uses SPI1 Mode 3, 16-bit frame mode and approximately 1.171875 MHz SCK in the repaired baseline.
- Gyroscope full scale: ±2000 dps.
- Accelerometer full scale: ±4 g.
- Nominal sensor/fusion rate: 2 kHz.
- Nominal UART snapshot rate: approximately 1 kHz.
- The repository documents unresolved board-level high-speed SPI signal-integrity risk. Host UI must not present 2 kHz hardware validation as guaranteed merely because the software target is 2 kHz.

## Calibration reality

The firmware performs an automatic startup rest calibration. It requires a contiguous rest interval and restarts accumulation if motion exceeds the thresholds. There is no audited host command for:

- starting gyro calibration,
- six-position accelerometer calibration,
- magnetometer calibration,
- writing calibration matrices/biases,
- saving calibration to flash,
- resetting orientation remotely.

Those controls are disabled in the Host.

## USB reality

The AT32 vendor library contains USB peripheral drivers, but the project application does not contain an audited USB device stack or application protocol. Vendor-driver availability is not considered product support.

Status: **Firmware not supported** in this baseline.

## CAN reality

The AT32 vendor library contains CAN peripheral drivers, but there is no project-level CAN initialization, application frame definition, decoder, sender, or transport protocol in the audited application.

Status: **Firmware not supported** in this baseline.

## Bootloader / firmware update reality

No `bootloader/` directory or application upgrade protocol is present in this snapshot. There is a development-side J-Link script and DAP flash helper, but those are not an end-user bootloader protocol.

Status: **Firmware not supported** in this baseline.

## Magnetometer / temperature reality

No IST8310 application driver or magnetometer data path is present in the audited snapshot. LSM6DSV temperature is also not included in the 16-channel UART frame.

Status: **Firmware not supported** for live Host display.

## Reusable proven code

The following existing logic is reused conceptually or directly where appropriate:

- exact 16-channel JustFloat layout from `src/main.c`;
- robust trailer-oriented parsing approach from `tools/vofa_1khz_log.py`;
- 2 Mbps UART default from firmware/BSP/tools;
- fusion/output expectations and SPI caveats from `README.md` / `FIXES.md`;
- DAP-only field classification from `inc/vqf_live.h` and DAP tooling.

No speculative `AA55` command protocol, CAN protocol, USB bulk protocol, or bootloader protocol is introduced.
