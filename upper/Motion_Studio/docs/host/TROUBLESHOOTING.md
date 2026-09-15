# Troubleshooting

## No COM port

1. Confirm the board is physically connected to the intended UART/USB-UART path.
2. Refresh ports in the Device Bar.
3. Verify Windows Device Manager sees the adapter.
4. Do not assume a fixed COM number; the application intentionally does not hard-code one.

## Connected but no telemetry

- Default baud is 2,000,000.
- Confirm firmware is the audited JustFloat build.
- Confirm PA0 USART4_TX reaches the host adapter RX and grounds are common.
- The current firmware output is binary, not text.
- Use the existing `tools/vofa_1khz_log.py` as an independent parser check.

## Low output rate / rising late counter

The firmware increments the `late` field when UART DMA is still busy or a send cannot start. A rising value is a real transport-health signal.

## Fusion rate low or unstable

The repository documents an unresolved physical high-speed SPI reliability risk on the board. A low fusion rate can originate below the Host layer. Use the existing DAP SPI sweep and 10-second VQF validation tools before changing Host parsing.

## CAN / firmware / calibration controls disabled

This is intentional for the audited firmware snapshot. The application does not invent commands the firmware does not implement.
