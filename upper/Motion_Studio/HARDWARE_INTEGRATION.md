# Hardware Integration

## Real device path available now

The audited firmware sends its formal host telemetry through USART4 at **2 Mbps**. Connect a suitable 3.3 V UART interface to the firmware-defined UART pins, select the resulting COM port in AT32 Motion Studio, keep the baud at `2000000`, and connect.

The host does not hard-code a COM number. Serial discovery reports USB VID/PID/product/serial metadata when Windows/driver information provides it, while still allowing manual selection because the target firmware itself does not establish a unique USB CDC identity in this baseline.

## First bring-up checklist

1. Confirm common ground and 3.3 V logic compatibility.
2. Confirm the target is running the intended telemetry build.
3. Select the COM port and 2 Mbps baud.
4. Watch Diagnostics for frame rate, framing errors, discarded resync bytes, last-packet age and UART `late` count.
5. Verify attitude motion and gravity direction before relying on plots.
6. For long runs, verify host memory remains bounded and `late` does not grow unexpectedly.

## What is not a current hardware path

Do not attempt to bring up the disabled CAN/Firmware/USB application pages from this host package as if a wire protocol already exists. The audited firmware snapshot does not contain those application contracts.

## DAP development diagnostics

The firmware's `vqf_live` debug structure exposes more information through debugger memory than the production UART frame, including WHO_AM_I and several initialization/rate fields. AT32 Motion Studio labels these as DAP-only rather than pretending they arrive on UART.

## Enabling a future transport safely

Before enabling any future USB/CAN/bootloader UI:

1. add/identify the firmware-side framing and state machine;
2. document byte order, message IDs, lengths, CRC/checksum, ACK/error behavior and timing;
3. capture real device traffic;
4. add parser regression fixtures and unit tests;
5. update `docs/host/PROTOCOL_MATRIX.md`;
6. implement a new transport/command adapter without changing the proven UART parser;
7. perform physical hardware validation.
