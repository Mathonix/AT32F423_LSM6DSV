# Hardware Validation Checklist

This checklist is intentionally not marked complete in the source-only build environment.

## UART bring-up

- Confirm target firmware build matches the audited JustFloat baseline.
- Connect USART4 TX/RX/GND through a 3.3 V-compatible interface.
- Enumerate ports without a hard-coded COM number.
- Connect at 2,000,000 baud.
- Confirm stable 68-byte framing and approximately 1 kHz host-observed output rate.
- Move the device and verify roll/pitch/yaw, quaternion and gyro/accel direction/sign conventions.
- Confirm `fusion_hz` remains near the expected firmware operating point and `late` does not continuously increase.

## Hot-plug / reconnect

- Unplug during an active session and confirm transition to Error/Reconnecting rather than a frozen Connected state.
- Reconnect the adapter and verify a single active reader remains.
- Repeat at least 20 cycles and check that frame rate does not multiply (duplicate-listener check).

## Long-run

- Run at least 60 minutes.
- Confirm frontend chart history remains bounded.
- Confirm Rust pending queue remains bounded.
- Record a long session and verify data is streamed to disk rather than accumulated as an unbounded JS array.
- Inspect CPU/GPU usage with the Attitude 3D view and Signals plots active.

## Recording / replay

- Start and stop a Tauri recording.
- Verify `metadata.json`, `session.json`, `telemetry.csv`, `events.jsonl`, and (when enabled) `raw.bin`.
- Load `session.json` in Replay mode and verify it follows the same motion trajectory.
- Disable raw capture in Settings and confirm decoded recording continues while `raw.bin` is omitted.

## Firmware-side physical validation

The firmware repository documents unresolved high-speed SPI signal-integrity risk on the physical board. Complete the repository's SPI sweep and VQF validation before claiming the target itself sustains the intended 2 kHz sensor/fusion operation.
