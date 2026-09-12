"""Flash optional IST8310 test firmware and validate mag_live via CMSIS-DAP."""
from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

MAGIC = 0x49535431
SRAM = 0x20000000
SRAM_SIZE = 0xC000
FMT = "<IIiIIiiiIIIIIII"
SIZE = struct.calcsize(FMT)
NAMES = [
    "magic", "seq", "err", "addr7", "who", "mx", "my", "mz",
    "nack", "millis", "stat1", "drdy", "scl", "sda", "pdcntl",
]
HERE = Path(__file__).resolve().parent
HEX_PATH = HERE.parent / "build" / "ist8310_test.hex"

ERRORS = {
    0: "OK",
    -1: "device/WHO_AM_I not found",
    -2: "I2C bus stuck low",
    -3: "initialization write failed",
    -4: "PDCNTL readback failed",
    -5: "single-measurement trigger failed",
    -6: "data-ready timeout",
    -7: "status/data read failed",
}


def find_live(target) -> int:
    blob = bytes(target.read_memory_block8(SRAM, SRAM_SIZE))
    pattern = MAGIC.to_bytes(4, "little")
    candidates = [i for i in range(0, len(blob) - SIZE + 1, 4) if blob[i:i+4] == pattern]
    if not candidates:
        raise RuntimeError("mag_live magic not found: is ist8310_test.hex running?")
    return SRAM + candidates[0]


def read_live(target, addr: int) -> dict[str, int]:
    target.halt()
    try:
        raw = bytes(target.read_memory_block8(addr, SIZE))
    finally:
        target.resume()
    return dict(zip(NAMES, struct.unpack(FMT, raw)))


def main() -> int:
    p = argparse.ArgumentParser(description="IST8310 DAPLink soldering test")
    p.add_argument("--flash", action="store_true", help="flash build/ist8310_test.hex first")
    p.add_argument("--count", type=int, default=10)
    p.add_argument("--period", type=float, default=0.4)
    args = p.parse_args()

    if args.flash:
        if not HEX_PATH.exists():
            raise RuntimeError(f"firmware not found: {HEX_PATH}; run 'make ist8310' first")
        sys.path.insert(0, str(HERE))
        from dap_flash_and_log import flash_target, parse_hex
        flash_target(parse_hex(HEX_PATH))
        time.sleep(0.5)

    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "attach", "frequency": 400000},
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP probe")

    session.open()
    samples: list[dict[str, int]] = []
    try:
        target = session.target
        addr = find_live(target)
        print(f"mag_live @ 0x{addr:08X}")
        print(
            f"{'No.':>3} {'ms':>8} {'err':>4} {'addr':>6} {'WHO':>6} "
            f"{'mx':>7} {'my':>7} {'mz':>7} {'nack':>6} "
            f"{'ST':>4} {'D':>2} {'CL':>2} {'DA':>2} {'PD':>4}"
        )
        for i in range(max(1, args.count)):
            t0 = time.perf_counter()
            d = read_live(target, addr)
            samples.append(d)
            print(
                f"{i+1:3d} {d['millis']:8d} {d['err']:4d} "
                f"0x{d['addr7']:02X}   0x{d['who']:02X} "
                f"{d['mx']:7d} {d['my']:7d} {d['mz']:7d} {d['nack']:6d} "
                f"0x{d['stat1']:02X} {d['drdy']:2d} {d['scl']:2d} {d['sda']:2d} "
                f"0x{d['pdcntl']:02X}"
            )
            remain = args.period - (time.perf_counter() - t0)
            if remain > 0 and i + 1 < args.count:
                time.sleep(remain)
    finally:
        session.close()

    last = samples[-1]
    reasons: list[str] = []
    if last["magic"] != MAGIC:
        reasons.append("SRAM magic mismatch")
    if last["addr7"] not in range(0x0C, 0x10):
        reasons.append("no legal IST8310 address")
    if last["who"] != 0x10:
        reasons.append(f"WHO_AM_I=0x{last['who']:02X}, expected 0x10")
    if last["err"] != 0:
        reasons.append(ERRORS.get(last["err"], f"firmware error {last['err']}"))
    if last["pdcntl"] != 0xC0:
        reasons.append(f"PDCNTL=0x{last['pdcntl']:02X}, expected 0xC0")
    if not (last["scl"] and last["sda"]):
        reasons.append("SCL/SDA is low while idle")
    if len(samples) > 1 and samples[-1]["seq"] == samples[0]["seq"]:
        reasons.append("sample sequence is not advancing")
    xyz = [(d["mx"], d["my"], d["mz"]) for d in samples if d["err"] == 0]
    if not xyz:
        reasons.append("no valid XYZ sample")
    elif all(v == 0 for v in xyz[-1]):
        reasons.append("XYZ is all zero")
    if len(samples) > 1 and samples[-1]["nack"] > samples[0]["nack"]:
        reasons.append("NACK counter increased during capture")

    if reasons:
        print("RESULT: IST8310 FAIL")
        for reason in dict.fromkeys(reasons):
            print(f"  - {reason}")
        return 1

    print("RESULT: IST8310 PASS")
    print("Move a magnet/rotate the board and confirm that X/Y/Z values change.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(2)
