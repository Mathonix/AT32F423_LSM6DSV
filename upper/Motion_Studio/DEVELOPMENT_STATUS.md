# AT32 Motion Studio — Development Status

Date: 2026-09-15

## Phase 1 — Repository audit / protocol matrix / architecture / design system

**Complete for the available 2026-09-12 firmware snapshot.** See `../docs/host/`.

## Phase 2 — Application shell / theme / routing / Mock Device

**Implemented.** Narrow sidebar, compact device bar, status bar, dark/light theme, command palette, deterministic Mock transport.

## Phase 3 — Overview / Attitude / Signals

**Implemented.** Quaternion-driven 3D viewer and batched ECharts telemetry use only fields in the audited 16-channel frame.

## Phase 4 — Real UART / decoder / discovery / reconnect

**Implemented in source.** Serial discovery, Rust reader thread, bounded JustFloat parser, batch snapshots and UART reconnect policy are present. Physical 2 Mbps validation still requires the real board and Windows serial driver.

## Phase 5 — Calibration / CAN / Diagnostics / Recording

- Diagnostics: **implemented** for host-visible metrics.
- Disk-streamed recording/replay/CSV/JSON/events/optional raw capture: **implemented in source**.
- Calibration commands: **Firmware not supported**; current firmware performs startup rest calibration internally.
- CAN: **Firmware not supported** in the audited application snapshot.

## Phase 6 — Bootloader / Firmware Upgrade

**Firmware not supported.** No bootloader protocol is invented. Firmware workspace is present as an explicit unsupported state.

## Phase 7 — Error handling / performance / high DPI / packaging

- Bounded parser, queue and history: implemented.
- Disconnect worker cleanup: implemented.
- Desktop-first/high-DPI CSS: implemented.
- Source validation + TypeScript parse validation: implemented.
- Windows release scripts/Tauri bundle config: implemented.
- Full installer build: requires a Windows machine with Rust/Tauri prerequisites and dependency access.

## Phase 8 — Hardware validation / regression / documentation

- Parser regression unit tests: implemented in Rust source.
- Replay fixture: `fixtures/sample-session.json`.
- Documentation: implemented.
- Physical board regression: **not claimable in this execution environment**.

No application USB/CAN/bootloader feature is marked complete merely because a vendor peripheral driver exists in the MCU SDK.
