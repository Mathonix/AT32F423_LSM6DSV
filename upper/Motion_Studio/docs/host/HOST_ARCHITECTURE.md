# Host Architecture

## Product boundary

AT32 Motion Studio is a Tauri 2 desktop application. The frontend is React/TypeScript; hardware I/O and high-rate parsing run in Rust. The architecture intentionally prevents a 1 kHz device stream from causing 1,000 React renders per second.

## Layers

```
Serial / Mock / Replay transport
          ↓
   Rust reader worker
          ↓
 JustFloat parser + validation
          ↓
 bounded telemetry ring + counters
          ↓
  20–30 Hz snapshot command
          ↓
 frontend telemetry store
          ↓
 numeric UI / ECharts / R3F viewer
```

### Transport layer

A common transport contract is used for UART, Mock and Replay. USB/CAN classes are not active transports in this baseline because the firmware does not support them.

### Reader worker

Real serial reading and parsing execute outside React. The worker owns the transport and pushes decoded samples into bounded queues. Disconnect stops the worker before reopening another transport, avoiding duplicate listeners.

### Parser

The production parser implements only the audited 68-byte VOFA JustFloat frame. It performs trailer resynchronization, float decoding, finite-value checks and buffer limits. Parser tests use deterministic byte fixtures including fragmented input and injected garbage.

### Telemetry store

The backend stores only a bounded pending queue for UI delivery. Frontend snapshots contain a batch of new samples rather than one callback per sample. The frontend keeps a separately bounded chart history, so an hour-long connection does not imply an hour of React objects.

### Recording and replay

Desktop recording streams to disk in the Rust worker instead of accumulating a one-hour session in browser memory. Each session contains replay-compatible `session.json`, decoded `telemetry.csv`, `events.jsonl`, `metadata.json`, and optionally the original `raw.bin` wire stream. Raw-byte recording is user-configurable. Replay re-encodes recorded samples to the audited 68-byte frame and sends them through the same Rust parser used by UART and Mock transports.

## State separation

Frontend state is split conceptually into:

- connection state,
- telemetry/history state,
- recording/replay state,
- UI/theme/preferences.

High-frequency raw samples never become one global object that causes unrelated UI to rerender.

## Connection state machine

```
Disconnected → Discovering → Connecting → Connected
       ↑                         ↓           ↓
       └──────── Error ←─────────┘     Reconnecting
```

The current implementation exposes UART, Mock and Replay. USB/CAN/Bootloader remain capability entries rather than fake transports.

## Performance targets

- UART read/parsing is backend work.
- React updates occur in batches around 20 Hz by default.
- chart history has a hard maximum.
- diagnostic event history is bounded.
- recording size is user-visible and intentionally bounded in memory before export.
- 3D rendering is isolated from parsing.
