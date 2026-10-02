# AT32F423 User Bootloader

## Archive status

This directory contains the AT32F423KCU7-4 bootloader source and build outputs. GCC cross-build and complete USB CDC and UART application uploads are board-verified as of 2026-10-01. See [hardware validation](../docs/firmware-upgrade-hardware-validation.md).

## Flash map

- Bootloader: 0x08000000..0x08007FFF (32 KiB)
- Application: 0x08008000..0x0803BFFF (208 KiB)
- Config/calibration reserve: 0x0803C000..0x0803FFFF (16 KiB)

BL_APP_END is the exclusive address 0x0803C000. The application must be linked with linker/AT32F423_app.ld at 0x08008000.

## Startup behavior

After reset the bootloader initializes USART4 and USB CDC and provides a 3-second update window. If the application vectors are valid and no boot request cookie is present, it then jumps to the application. Framed application command 0x16 stores a RAM cookie and resets into maintenance mode; the legacy AA 00 00 0D command only resets the application. In maintenance mode the host must complete END validation and send BOOT (0x06) to start the application. An active or failed update cannot be booted by timeout or BOOT in the same session. If no valid application exists, the bootloader remains available for recovery.

The cookie is at 0x2000BFF0. Both GCC images reserve the top 16 bytes of SRAM and place their initial stack below it. Build and deploy the updated bootloader and application together; this mailbox reservation does not apply to the archived Keil project.

## Protocol

Request header is 18 bytes, little endian: magic[2] (42 4C), version[1], command[1], sequence[2], address[4], length[4], crc32[4]. **Only DATA has a payload** of `length` bytes. BEGIN/END use `length` for the whole image and have no payload. CRC32 is IEEE/zlib CRC32. DATA is limited to 256 bytes and must start at 0x08008000 with contiguous, word-aligned addresses. Nonfinal chunks must have a multiple-of-four length; a final partial word is padded with 0xFF in Flash, not included in image CRC.

Reply is 14 bytes: magic[2], version[1], command|0x80[1], status[1], reserved[1], value[4], crc32[4]. Reply CRC covers the first 10 bytes.

Each transport has its own parser and receives only its own replies. UART ACK transmission never waits for the USB IN endpoint. BEGIN binds the transfer to its physical port until that port sends ABORT or the MCU restarts. HELLO on the other port remains read-only; BEGIN/DATA/END/ABORT/BOOT from that other port return BUSY. To change transport during a transfer, send ABORT on the original port, then start a complete upload on the new one. This port ownership is volatile and does not replace the planned v2 persistent image state or session identifiers.

## Build

```powershell
cd bootloader
make -B
```

Outputs are bootloader/build/at32f423_bootloader.elf, .hex, and .bin. Build the application from the repository root with make -B.

## Updater

The canonical updater is `bootloader/tools/bl_upload.py`. It does not send an application-entry request by default; use `--enter` to request maintenance mode from a compatible application, or `--no-enter` when already in maintenance mode. With `--enter`, it closes the old native serial handle and waits for the same VID/PID/serial identity to return, including a changed COM number. Adapters without a serial descriptor must return on the original COM name with the same VID/PID. Ambiguous identities or a wrong application base are rejected before BEGIN. The old board Bootloader failed this entry test; the current source Bootloader was installed once over SWD and its full Boot region was read back before USB testing. `tools/bootloader_update.py` retains the older manual-entry CLI. Both tools send BOOT only after a successful END.

After SWD flashing the bootloader, reset the target. A normal reset gives the host a 3-second window; send the application bootloader-entry command for an unlimited maintenance window:

```powershell
py -3 -m pip install pyserial
py -3 bootloader/tools/bl_upload.py build/lsm6dsv_spi_test/release-9axis/lsm6dsv_spi_test.bin --port COM16 --baud 2000000 --no-enter
```

USART4 is PA0 TX / PA1 RX, 8N1, 2 Mbps. USB CDC also appears as a COM port; only one PC port can be opened at a time.

## Validation status

Verified: GCC build/link, partition addresses, frame parsing, contiguous address checks, per-chunk CRC32, whole-image CRC32, and application vector validation. Board-verified on 2026-10-01: bounded SWD Boot installation, automatic jump, application-command entry, complete USB CDC and UART (COM6, 2 Mbps) uploads, BOOT startup, resumed PING/telemetry, exact full application readback, and retained device settings/accelerometer calibration. UART was also tested while the USB cable remained connected. Real power-loss recovery remains unverified. Updating erases the application region first; do not power off during update.

Hardware-free regressions: `python -B tests/run_native.py` and `python -B -m unittest discover -s tests -p 'test_*.py' -v` from the repository root. CRC detects corruption, not a malicious firmware image. There is no signed firmware verification or persistent image-commit marker; after power loss startup validates vectors only. Do not treat this bootloader as secure boot or power-fail-atomic firmware storage.
