# Architecture Decisions

## ADR-001 — Do not reuse speculative WebPCAN protocol assumptions

Previous host experiments contained generic `AA 55` binary/CAN/bootloader concepts. The audited firmware snapshot does not implement those protocols. AT32 Motion Studio therefore removes them from the active protocol layer.

## ADR-002 — Treat 16-channel JustFloat as the production baseline

It is directly implemented in `src/main.c` and independently consumed by `tools/vofa_1khz_log.py`.

## ADR-003 — High-rate parsing belongs in Rust

The device can emit about 1,000 telemetry frames per second. Backend batching protects React from per-packet renders.

## ADR-004 — Unsupported workspaces remain visible

CAN, firmware upgrade and host-controlled calibration are product roadmap areas. Keeping the pages visible with explicit capability status is more useful than hiding them, but no interactive command is enabled until firmware support exists.

## ADR-005 — Host-derived metrics are labeled as derived

Output rate is measured by the host because firmware `out_hz` is DAP-only and absent from UART. WHO_AM_I and init errors are not shown as live UART values.
