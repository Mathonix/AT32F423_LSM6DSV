# Screenshots

No synthetic product screenshots are included in this source delivery.

A real screenshot requires the React/Tauri dependencies to be installed and the application to render. Dependency installation timed out in the current isolated build environment, so fabricating a screenshot would violate the project's “not a UI demo / do not fake functionality” rule.

After a successful Windows build, capture at minimum:

1. Overview — connected real UART session.
2. Attitude — quaternion-driven 3D model.
3. Signals — gyro/accel viewer with cursor and channel legend.
4. Diagnostics — parser/session health.
5. Calibration/CAN/Firmware — explicit `Firmware not supported` states.
6. Light theme and dark theme at 125% and 150% Windows scaling.
