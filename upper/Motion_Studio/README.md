# AT32 Motion Studio

**AT32 Motion Studio — Professional Motion & Embedded Device Studio** is a Tauri 2 desktop host for the audited `AT32F423KCU7 + LSM6DSV` firmware in the parent repository.

This host intentionally follows the firmware rather than inventing a desktop-side protocol. The audited 2026-09-12 firmware baseline exposes a production telemetry path over **USART4 at 2,000,000 baud** using **VOFA+ JustFloat: 16 little-endian float32 values followed by `00 00 80 7F`**. USB application transport, application CAN, magnetometer telemetry, host calibration commands, and a product bootloader protocol are **not present in that snapshot**, so their workspaces are visibly marked `Firmware not supported`.

## What is implemented

- Tauri 2 + Rust desktop shell.
- React + strict TypeScript + Vite + Tailwind CSS.
- Clean dense dark/light engineering UI with a narrow sidebar, compact device bar, status bar, command palette and desktop-first sizing.
- UART port discovery and 2 Mbps serial transport.
- Dedicated Rust telemetry thread, bounded parser buffer, bounded pending queue, batched IPC snapshots, and bounded frontend history.
- Exact current-firmware JustFloat decoder with resynchronization and parser regression tests.
- Overview, professional 3D attitude viewer, gyro/accelerometer signal viewer, Diagnostics, Settings.
- Calibration/CAN/Firmware pages that accurately communicate current firmware capability instead of exposing dead controls.
- Deterministic Mock transport that emits the **same 68-byte wire format** as the firmware.
- Recorded-session JSON replay that re-encodes samples and passes them back through the same Rust parser.
- Disk-streamed sessions: replayable JSON, decoded CSV, event log, metadata and optional raw binary wire capture.
- Auto-reconnect policy for UART transport.

## Audited wire format

```text
16 × float32 little-endian + 00 00 80 7F
```

| Ch | Field | Unit |
|---:|---|---|
| 0 | Roll | deg |
| 1 | Pitch | deg |
| 2 | Yaw | deg |
| 3..6 | Quaternion `qw qx qy qz` | — |
| 7..9 | Gyroscope `gx gy gz` | dps |
| 10..12 | Accelerometer `ax ay az` | g |
| 13 | VQF execution time | µs |
| 14 | Fusion rate | Hz |
| 15 | UART late/busy counter | count |

See [`../docs/host/PROTOCOL_MATRIX.md`](../docs/host/PROTOCOL_MATRIX.md) and [`PROTOCOL.md`](PROTOCOL.md).

## Development

Prerequisites on Windows:

- Node.js 20+ and npm 10+
- Rust stable (`rustup`)
- Visual Studio 2022 Build Tools with Desktop development with C++
- Microsoft WebView2 Runtime
- Tauri 2 platform prerequisites

```powershell
npm install
npm run check
npm run lint
npm run test:rust
npm run build
npm run dev:desktop
```

For a release bundle:

```powershell
.\build_windows.ps1
```

The Tauri configuration targets NSIS and MSI bundles. Installer generation requires Windows.

## Repository layout

```text
at32-motion-studio/
  src/                    React/TypeScript UI
  src-tauri/src/          Rust transport/parser/service/commands
  fixtures/               Replay/regression session fixture
  scripts/                Source validation
  PROTOCOL.md             Exact host-visible protocol baseline
  HARDWARE_INTEGRATION.md Bring-up guide
../docs/host/              Repository audit, matrix, architecture, design system, plan
```

## Product boundary

The current host can be used with the real UART telemetry today. CAN, USB application communication, magnetic telemetry, persistent calibration, orientation-zero device commands and firmware update are deliberately **not implemented as pretend desktop protocols**. When those capabilities are added to firmware, update the protocol audit first, add regression captures, then enable the corresponding adapters/workspaces.
