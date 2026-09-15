#!/usr/bin/env python3
"""Flash AT32F423 via WCH CMSIS-DAP, then read COM4 UART."""

from __future__ import annotations

import sys
import time
import argparse
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

HEX_PATH = Path(r"E:\Desktop\CV_resume\Program\AT32F423_LSM6DSV_SPI_Test\build\lsm6dsv_spi_test.hex")
COM_PORT = None
BAUD = 2000000
DEFAULT_SWD_FREQUENCY = 1_000_000
MIN_SWD_FREQUENCY = 1_000_000
AUTO_RESET_AFTER_FLASH = True

FLASH_BASE = 0x40023C00
FLASH_UNLOCK = FLASH_BASE + 0x04
FLASH_STS = FLASH_BASE + 0x0C
FLASH_CTRL = FLASH_BASE + 0x10
FLASH_ADDR = FLASH_BASE + 0x14
KEY1 = 0x45670123
KEY2 = 0xCDEF89AB
SECTOR = 0x800  # 2 KB

CTRL_FPRGM = 1 << 0
CTRL_SECERS = 1 << 1
CTRL_ERSTR = 1 << 6
CTRL_OPLK = 1 << 7
STS_OBF = 1 << 0
STS_PRGMERR = 1 << 2
STS_EPPERR = 1 << 4


def parse_hex(path: Path) -> dict[int, int]:
    mem: dict[int, int] = {}
    base = 0
    with path.open("r", encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            count = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            rtype = int(line[7:9], 16)
            data = bytes.fromhex(line[9 : 9 + count * 2])
            if rtype == 0x00:
                absaddr = base + addr
                for i, b in enumerate(data):
                    mem[absaddr + i] = b
            elif rtype == 0x04:
                base = int.from_bytes(data, "big") << 16
            elif rtype == 0x01:
                break
    return mem


def wait_idle(target, timeout_s=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        sts = target.read32(FLASH_STS)
        if (sts & STS_OBF) == 0:
            if sts & (STS_PRGMERR | STS_EPPERR):
                raise RuntimeError(f"flash error sts=0x{sts:08X}")
            return
        time.sleep(0.001)
    raise TimeoutError("flash busy timeout")


def unlock(target):
    target.write32(FLASH_UNLOCK, KEY1)
    target.write32(FLASH_UNLOCK, KEY2)
    ctrl = target.read32(FLASH_CTRL)
    if ctrl & CTRL_OPLK:
        raise RuntimeError(f"flash still locked ctrl=0x{ctrl:08X}")


def erase_sector(target, addr):
    wait_idle(target)
    ctrl = target.read32(FLASH_CTRL) & ~CTRL_OPLK
    target.write32(FLASH_CTRL, ctrl | CTRL_SECERS)
    target.write32(FLASH_ADDR, addr)
    target.write32(FLASH_CTRL, (ctrl | CTRL_SECERS | CTRL_ERSTR) & ~CTRL_OPLK)
    wait_idle(target, 2.0)
    target.write32(FLASH_CTRL, ctrl & ~CTRL_SECERS & ~CTRL_ERSTR & ~CTRL_OPLK)


def program_word(target, addr, word):
    wait_idle(target)
    ctrl = target.read32(FLASH_CTRL) & ~CTRL_OPLK
    target.write32(FLASH_CTRL, ctrl | CTRL_FPRGM)
    target.write32(addr, word)
    wait_idle(target, 0.5)
    target.write32(FLASH_CTRL, ctrl & ~CTRL_FPRGM & ~CTRL_OPLK)


def words_from_mem(mem: dict[int, int]) -> list[tuple[int, int]]:
    if not mem:
        raise RuntimeError("empty hex")
    start = min(mem)
    end = max(mem) + 1
    start &= ~3
    if end % 4:
        end = (end + 3) & ~3
    out = []
    for a in range(start, end, 4):
        b0 = mem.get(a, 0xFF)
        b1 = mem.get(a + 1, 0xFF)
        b2 = mem.get(a + 2, 0xFF)
        b3 = mem.get(a + 3, 0xFF)
        word = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)
        if word != 0xFFFFFFFF:
            out.append((a, word))
    return out, start, end


def reset_and_run_after_flash(target) -> None:
    """Reset the MCU after programming and verify that it is running."""
    if not AUTO_RESET_AFTER_FLASH:
        print("auto reset disabled")
        return

    # Do not leave the core halted at the reset vector after programming.
    target.reset_stop_on_reset = False
    try:
        target.reset()
    except Exception as exc:
        raise RuntimeError(f"automatic reset failed: {exc}") from exc

    try:
        target.resume()
    except Exception as exc:
        raise RuntimeError(f"could not release MCU after reset: {exc}") from exc

    # A short settle time avoids reporting the state during the reset handshake.
    time.sleep(0.05)
    try:
        state = target.get_state().name
    except Exception as exc:
        raise RuntimeError(f"could not verify MCU state after reset: {exc}") from exc
    if state != "RUNNING":
        raise RuntimeError(f"MCU is not running after automatic reset (state={state})")

    try:
        pc = target.read_core_register("pc")
        print(f"automatic reset OK; target running pc=0x{pc:08X}")
    except Exception:
        print("automatic reset OK; target running")


def flash_target(mem: dict[int, int], swd_frequency: int = DEFAULT_SWD_FREQUENCY) -> None:
    words, start, end = words_from_mem(mem)
    print(f"image 0x{start:08X}-0x{end:08X}, {len(words)} words")

    opts = {
        "connect_mode": "under-reset",
        "frequency": swd_frequency,
    }
    session = ConnectHelper.session_with_chosen_probe(target_override="cortex_m", options=opts)
    if session is None:
        raise RuntimeError("no CMSIS-DAP probe")
    session.open()
    try:
        target = session.target
        target.halt()
        print(f"halted pc=0x{target.read_core_register('pc'):08X}")
        unlock(target)
        print("flash unlocked")

        first_sec = start & ~(SECTOR - 1)
        last_sec = (end - 1) & ~(SECTOR - 1)
        sec = first_sec
        while sec <= last_sec:
            print(f"erase 0x{sec:08X}")
            erase_sector(target, sec)
            sec += SECTOR

        for i, (addr, word) in enumerate(words):
            program_word(target, addr, word)
            if i % 128 == 0:
                print(f"program {i}/{len(words)} 0x{addr:08X}")
        print("program done")

        # verify a few words
        bad = 0
        for addr, word in words[:: max(1, len(words) // 16)]:
            got = target.read32(addr)
            if got != word:
                print(f"VERIFY FAIL 0x{addr:08X} wrote 0x{word:08X} got 0x{got:08X}")
                bad += 1
        if bad:
            raise RuntimeError("verify failed")
        print("spot verify OK")

        sp = target.read32(0x08000000)
        rst = target.read32(0x08000004)
        print(f"vector SP=0x{sp:08X} Reset=0x{rst:08X}")

        reset_and_run_after_flash(target)
    finally:
        session.close()


def read_uart(port: str, seconds=8.0, baud=BAUD) -> str:
    import serial

    ser = serial.Serial(port, baud, timeout=0.2)
    ser.reset_input_buffer()
    t0 = time.time()
    chunks: list[bytes] = []
    print(f"listening {port} {baud} for {seconds:.0f}s ...")
    while time.time() - t0 < seconds:
        data = ser.read(256)
        if data:
            chunks.append(data)
            sys.stdout.write(data.decode("latin1", errors="replace"))
            sys.stdout.flush()
    ser.close()
    return b"".join(chunks).decode("latin1", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--hex", type=Path, default=HEX_PATH)
    ap.add_argument("--port", help="UART port; omit to skip UART logging")
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--swd-frequency", type=int, default=DEFAULT_SWD_FREQUENCY,
                    help="SWD clock in Hz; minimum 1000000")
    args = ap.parse_args()
    if args.swd_frequency < MIN_SWD_FREQUENCY:
        print(f"SWD frequency must be at least {MIN_SWD_FREQUENCY} Hz")
        return 2
    if not args.hex.exists():
        print("missing hex", args.hex)
        return 1
    mem = parse_hex(args.hex)
    print(f"hex bytes: {len(mem)}")
    flash_target(mem, args.swd_frequency)
    time.sleep(0.4)
    if not args.port:
        print("UART logging skipped; flash completed successfully")
        return 0
    text = read_uart(args.port, args.seconds, args.baud)
    print("`n----- summary -----")
    if "WHO_AM_I=0x70" in text or "OK  WHO_AM_I" in text:
        print("RESULT: LSM6DSV SPI OK (WHO_AM_I=0x70)")
        return 0
    if "FAIL WHO_AM_I" in text:
        print("RESULT: SPI answered but WHO_AM_I mismatch")
        return 2
    if text.strip():
        print("RESULT: got UART but no WHO_AM_I line")
        return 3
    print("RESULT: no UART data (check PA0->DAPLink RX and baud)")
    return 4


if __name__ == "__main__":
    sys.exit(main())





