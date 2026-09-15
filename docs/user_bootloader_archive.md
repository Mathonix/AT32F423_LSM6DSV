# User Bootloader archive audit

Archive date: 2026-09-14

## Canonical layout

- Bootloader: 0x08000000..0x08007FFF (32 KiB)
- Application: 0x08008000..0x0803BFFF; end is exclusive 0x0803C000
- Config/calibration reserve: 0x0803C000..0x0803FFFF
- USART4: PA0 TX / PA1 RX, 2 Mbps
- USB: CDC on PA11/PA12

The application linker file is linker/AT32F423_app.ld. An application linked at 0x08000000 is not compatible with this bootloader.

## Source of truth

- Bootloader source: bootloader/src, bootloader/inc, bootloader/linker
- Build: bootloader/Makefile
- Canonical updater: bootloader/tools/bl_upload.py
- Legacy updater: tools/bootloader_update.py; same 18-byte request and 14-byte reply format, but a different CLI
- Build artifacts: bootloader/build/at32f423_bootloader.{elf,hex,bin}

## Corrected claims

The startup timeout is implemented with DWT->CYCCNT and system_core_clock. It is not a SysTick timer. The 3-second window and application jump have been verified by code/build inspection only, not by board test. USB CDC and UART full update, first SWD flash, power-loss recovery, and post-update automatic restart are still unverified.

The canonical updater defaults to no application-entry request. Use `--enter` only as a best-effort compatibility option; it must not be treated as a guaranteed way to force the running application into the bootloader. The application-side boot request path is not board-verified. Recommended procedure: reset the target manually, then run the updater within the 3-second window with --no-enter. The legacy AA 00 00 0D request remains only a best-effort compatibility option.

## Protocol summary

Request: magic BL, version 1, command, uint16 sequence, uint32 address, uint32 length, uint32 IEEE CRC32, optional payload. Header size is 18 bytes; DATA payload is at most 256 bytes and addresses must be contiguous from 0x08008000. Reply size is 14 bytes and its CRC covers the first 10 bytes.

## Verification performed during this audit

- Inspected bootloader source, linker map, configuration, both updater scripts, and application linker file.
- Confirmed bootloader build succeeds with make -C bootloader -B.
- Confirmed bootloader output exists in bootloader/build.
- Confirmed Python syntax checks for the updater.

No physical device or COM-port update was performed in this audit.
