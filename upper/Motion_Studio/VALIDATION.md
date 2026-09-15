# AT32 Motion Studio — Validation Report

Date: 2026-09-15

This report separates checks actually executed in this environment from checks that require Windows/Rust dependencies or physical hardware.

## Executed successfully here

- `node scripts/audit-firmware.mjs` — **PASS**
  - firmware `VOFA_N_CH = 16`;
  - trailer is `00 00 80 7F`;
  - channels 13/14/15 are `vqf_us / fusion_hz / late`;
  - host UART is USART4 at 2,000,000 baud;
  - `tools/vofa_1khz_log.py` agrees on the 2 Mbps baseline.
- `node scripts/validate-source.mjs` — **PASS**; required Host artifacts exist and the formal protocol document contains no old fabricated AA55/A55A/CAN-bridge protocol.
- `tsc -b --noCheck --pretty false` — **PASS**; TypeScript/TSX project source parses and its build graph is valid.
- Custom local `@/` import resolution scan — **PASS**.
- Firmware Python tools: `python3 -m compileall -q tools` — **PASS**.
- Firmware Makefile: `make -n` — **PASS** (expected ARM GCC compile/link plan generated).

## Dependency installation attempt

`npm install --no-audit --no-fund` was attempted in this environment but dependency retrieval did not complete before the environment timeout. No production dependency tree is therefore bundled into the source package.

Consequences in this environment:

- `npm run check` with real React/ECharts/Three/Tauri type packages cannot be truthfully reported as completed;
- `npm run lint` cannot run because project ESLint dependencies are not installed;
- Vite production output cannot be rendered here, so no fabricated screenshot is supplied.

The Windows build script explicitly runs these checks after dependency installation.

## Rust test/build boundary

This container does not have `rustc` or `cargo`. Therefore the Rust unit tests and Tauri native build cannot be reported as executed here.

The source includes parser regression tests for:

- exact 16-float firmware layout;
- fragmented frames;
- garbage-byte resynchronization;
- NaN rejection.

Run on a Rust-enabled Windows development machine:

```powershell
npm install
npm run audit:firmware
npm run validate:source
npm run check
npm run lint
npm run test:rust
npm run build
npm run tauri build
```

or use `build_windows.ps1`, which performs the same quality gates before bundling MSI/NSIS installers.

## Physical hardware boundary

Implemented and ready for real-device validation:

- serial enumeration and manual port selection;
- 2 Mbps UART transport;
- exact current-firmware JustFloat parser;
- bounded parser queue and batched UI snapshots;
- quaternion 3D attitude, signals, diagnostics;
- reconnect lifecycle;
- disk-streamed session recording and replay.

Not claimed as physically validated here:

- sustained 2 Mbps reception on the actual UART bridge;
- one-hour Windows CPU/memory test;
- repeated physical unplug/replug behavior;
- firmware USB/CAN/bootloader functions, because those application protocols are absent from the audited firmware snapshot.

See `../docs/host/HARDWARE_VALIDATION.md` in the full project package.
