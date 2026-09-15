# Protocol Matrix

Legend: **Supported** = implemented by audited application firmware; **DAP-only** = available through SWD/DAP tooling rather than product telemetry; **Unsupported** = no audited application implementation.

| Capability | UART | USB | CAN | DAP/SWD | Host behavior |
|---|---|---|---|---|---|
| Roll/Pitch/Yaw | Supported, JustFloat | Unsupported | Unsupported | DAP-only mirror | Live display |
| Quaternion | Supported, JustFloat | Unsupported | Unsupported | DAP-only mirror | Live display / 3D viewer |
| Gyro XYZ | Supported, JustFloat | Unsupported | Unsupported | DAP-only mirror | Signal viewer |
| Accel XYZ | Supported, JustFloat | Unsupported | Unsupported | DAP-only mirror | Signal viewer |
| VQF execution time | Supported, JustFloat | Unsupported | Unsupported | DAP-only mirror | Diagnostics |
| Fusion Hz | Supported, JustFloat | Unsupported | Unsupported | DAP-only mirror | Diagnostics |
| UART late-send counter | Supported, JustFloat | Unsupported | Unsupported | — | Packet health |
| Host-observed output Hz | Derived by host | — | — | firmware has DAP-only `out_hz` | Diagnostics |
| WHO_AM_I | Not in frame | Unsupported | Unsupported | DAP-only | Show as DAP-only capability, not a live value |
| init error | Not in frame | Unsupported | Unsupported | DAP-only | Not fabricated |
| skipped sample count | Not in frame | Unsupported | Unsupported | DAP-only | Not fabricated |
| temperature | Not in frame | Unsupported | Unsupported | Not exposed in live struct | Unsupported |
| IST8310 magnetometer | Unsupported | Unsupported | Unsupported | Unsupported in snapshot | Unsupported |
| command/response | No parser found | Unsupported | Unsupported | DAP scripts only | Disabled command controls |
| orientation zero command | Unsupported | Unsupported | Unsupported | Unsupported | Local camera/view reset only |
| host-triggered calibration | Unsupported | Unsupported | Unsupported | Startup calibration is automatic | Wizard shown as unavailable |
| CAN telemetry | Unsupported | Unsupported | Unsupported | — | CAN page documents gap |
| CAN transmit | Unsupported | Unsupported | Unsupported | — | Disabled |
| firmware update | Unsupported | Unsupported | Unsupported | Development flashing via DAP/J-Link | Firmware page documents gap |
| device firmware version query | Unsupported | Unsupported | Unsupported | — | Display `Unavailable` |
| device serial query | Unsupported | Unsupported | Unsupported | — | Display `Unavailable` |

## UART framing contract

```
64 bytes payload = 16 x float32 little-endian
4 bytes trailer   = 00 00 80 7F
Total             = 68 bytes
```

The host parser searches for the trailer, requires at least 64 preceding bytes, decodes exactly the preceding 64 bytes, rejects non-finite or physically implausible snapshots, and keeps a bounded resynchronization buffer. No CRC exists in this protocol.

## Important consequence

Because JustFloat has no sequence number or CRC, the Host can measure frame rate and observe the firmware `late` counter, but it cannot prove packet integrity at the same level as a framed/CRC protocol. The Diagnostics UI labels this limitation explicitly.
