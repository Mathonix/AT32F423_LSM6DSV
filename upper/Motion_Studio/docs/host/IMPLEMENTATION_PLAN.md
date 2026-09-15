# Implementation Plan

## Phase 1 — Repository audit

Status: complete for the supplied 2026-09-12 fixed snapshot.

Outputs: `REPOSITORY_AUDIT.md`, `PROTOCOL_MATRIX.md`, `HOST_ARCHITECTURE.md`, `UI_DESIGN_SYSTEM.md`, this plan.

## Phase 2 — Application shell

- Tauri 2 / React / TypeScript / Vite shell
- dark/light theme
- compact sidebar/device bar/status bar
- command palette
- Mock transport through common session interface

## Phase 3 — Core motion workspaces

- Overview
- Attitude with React Three Fiber viewer
- Signals with ECharts and bounded windows
- deterministic mock data

## Phase 4 — Real UART adapter

- serial discovery
- 2 Mbps default
- background reader
- exact 16-channel JustFloat parser
- reconnect-safe session lifecycle
- parser regression tests

USB remains disabled until application firmware support exists.

## Phase 5 — Calibration / CAN / Diagnostics / Recording

- Calibration Center documents automatic startup rest calibration and disables unsupported host commands.
- CAN page is an explicit unsupported capability surface.
- Diagnostics exposes host-observed packet rate, bytes, parser resync/error counters, firmware `fusion_hz`, `vqf_us` and `late`.
- Session recording streams replayable JSON, decoded CSV, events, metadata and optional raw wire bytes to disk.
- Replay reuses the same transport/parser path rather than bypassing the decoder.

## Phase 6 — Firmware

The UI workspace is implemented as an unsupported state only. No bootloader packet format is invented. A real upgrade implementation is deferred until a bootloader/application protocol appears in the firmware repository.

## Phase 7 — Quality / packaging

- strict TypeScript
- Rust parser tests
- source validation script
- Windows development/build scripts
- Tauri installer configuration
- high-DPI-safe CSS
- troubleshooting docs

## Phase 8 — Hardware validation

Requires Windows, the physical AT32F423 board, correct UART adapter/wiring, and the actual firmware. Acceptance checklist is documented separately. Hardware validation cannot be truthfully completed in an environment without the board.
