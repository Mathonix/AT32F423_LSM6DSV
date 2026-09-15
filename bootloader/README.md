# AT32F423 User Bootloader

## Archive status

This directory contains the AT32F423KCU7-4 bootloader source and build outputs. GCC cross-build is verified locally. USB CDC, UART, and the full update flow still require board-level validation.

## Flash map

- Bootloader: 0x08000000..0x08007FFF (32 KiB)
- Application: 0x08008000..0x0803BFFF (208 KiB)
- Config/calibration reserve: 0x0803C000..0x0803FFFF (16 KiB)

BL_APP_END is the exclusive address 0x0803C000. The application must be linked with linker/AT32F423_app.ld at 0x08008000.

## Startup behavior

After reset the bootloader initializes USART4 and USB CDC, then waits 3 seconds. The timeout uses DWT->CYCCNT and system_core_clock, not SysTick. During the window, valid update traffic keeps the bootloader active. After the window it jumps to an application with a valid vector table; with no valid application it stays in update mode.

The application-side automatic boot request has not been board-verified. Manual reset followed immediately by the updater is the recommended entry method. The legacy AA 00 00 0D request is only a compatibility attempt and is not guaranteed to enter the bootloader.

## Protocol

Request header is 18 bytes, little endian: magic[2] (42 4C), version[1], command[1], sequence[2], address[4], length[4], crc32[4], followed by payload[length]. CRC32 is IEEE/zlib CRC32. DATA is limited to 256 bytes and must start at 0x08008000 with contiguous addresses.

Reply is 14 bytes: magic[2], version[1], command|0x80[1], status[1], reserved[1], value[4], crc32[4]. Reply CRC covers the first 10 bytes.

## Build

```powershell
cd bootloader
make -B
```

Outputs are bootloader/build/at32f423_bootloader.elf, .hex, and .bin. Build the application from the repository root with make -B.

## Updater

The canonical updater is `bootloader/tools/bl_upload.py`. It does not send an application-entry request by default; use `--enter` only for the unverified legacy request, or `--no-enter` explicitly. `tools/bootloader_update.py` is a legacy compatible tool with a different CLI.

After SWD flashing the bootloader, reset the target and run within the 3-second window:

```powershell
py -3 -m pip install pyserial
py -3 bootloader/tools/bl_upload.py build/lsm6dsv_spi_test.bin --port COM16 --baud 2000000 --no-enter
```

USART4 is PA0 TX / PA1 RX, 8N1, 2 Mbps. USB CDC also appears as a COM port; only one PC port can be opened at a time.

## Validation status

Verified: GCC build/link, partition addresses, frame parsing, contiguous address checks, per-chunk CRC32, whole-image CRC32, and application vector validation. Not yet board-verified: SWD first flash, automatic jump, complete UART update, complete USB update, power-loss recovery, and automatic application restart after update. Updating erases the application region first; do not power off during update.
