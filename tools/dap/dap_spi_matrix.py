#!/usr/bin/env python3
"""Build/flash the standalone SPI matrix firmware and print its SRAM results."""

from __future__ import annotations

import argparse
import csv
import struct
import subprocess
import sys
import time
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[2]
HEX_PATH = ROOT / "build" / "spi_matrix.hex"
SRAM = 0x20000000
SRAM_SIZE = 48 * 1024
MAGIC = 0x5350494D
HEADER_FMT = "<16I"
HEADER_SIZE = struct.calcsize(HEADER_FMT)
RESULT_FMT = "<11I"
RESULT_SIZE = struct.calcsize(RESULT_FMT)
DIVISORS = [1024, 512, 256, 128, 64, 32, 16, 8]
TIMINGS = [100, 10, 2]


def build() -> None:
    print("building spi_matrix firmware ...")
    subprocess.run(["make", "-B", "spi-matrix"], cwd=ROOT, check=True)


def flash() -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from dap_flash_and_log import flash_target, parse_hex

    if not HEX_PATH.exists():
        raise FileNotFoundError(HEX_PATH)
    flash_target(parse_hex(HEX_PATH))


def live_read(target, addr: int, size: int) -> bytes:
    """Read through the MEM-AP without stopping an in-flight SPI transaction."""
    return bytes(target.read_memory_block8(addr, size))


def halted_read(target, addr: int, size: int) -> bytes:
    """Use only after the firmware reports done/fatal."""
    try:
        target.halt()
    except Exception:
        pass
    try:
        return bytes(target.read_memory_block8(addr, size))
    finally:
        try:
            target.resume()
        except Exception:
            pass


def find_log(target) -> int:
    raw = live_read(target, SRAM, SRAM_SIZE)
    needle = struct.pack("<I", MAGIC)
    off = raw.find(needle)
    if off < 0:
        raise RuntimeError("SPI matrix magic not found in SRAM")
    return SRAM + off


def read_header(target, addr: int) -> dict:
    vals = struct.unpack(HEADER_FMT, live_read(target, addr, HEADER_SIZE))
    keys = [
        "magic", "version", "state", "count", "result_max", "reads",
        "ref_reads", "core_hz", "apb2_hz", "current_config",
        "ref_fail_groups", "fatal_code", "r0", "r1", "r2", "r3",
    ]
    return dict(zip(keys, vals))


def decode_config(config: int) -> dict:
    mode = config & 3
    frame16 = bool(config & 0x4)
    variant = bool(config & 0x8)
    hwcs = bool(config & 0x10)
    moderate = bool(config & 0x20)
    div_i = (config >> 8) & 0xF
    timing_i = (config >> 12) & 0x3
    return {
        "mode": mode,
        "frame": 16 if frame16 else 8,
        "variant": (
            "0x008F(low-byte-first check)" if variant else "0x8F00(normal)"
        ) if frame16 else ("DT16 access" if variant else "DT8 access"),
        "cs": "hardware" if hwcs else "GPIO",
        "drive": "moderate" if moderate else "stronger",
        "div": DIVISORS[div_i] if div_i < len(DIVISORS) else -1,
        "timing_us": TIMINGS[timing_i] if timing_i < len(TIMINGS) else -1,
    }


def read_results(target, addr: int, count: int) -> list[dict]:
    raw = halted_read(target, addr + HEADER_SIZE, count * RESULT_SIZE)
    rows: list[dict] = []
    for i in range(count):
        words = struct.unpack_from(RESULT_FMT, raw, i * RESULT_SIZE)
        (config, ref_total_ok, ok_ff, zero_other, timeout_first, first_rx,
         ctrl1, ctrl2, sts_or, gpio_cfgr, gpio_muxl) = words
        c = decode_config(config)
        row = {
            "index": i,
            "config": f"0x{config:08X}",
            **c,
            "ref_total": (ref_total_ok >> 16) & 0xFFFF,
            "ref_ok": ref_total_ok & 0xFFFF,
            "ok70": (ok_ff >> 16) & 0xFFFF,
            "ff": ok_ff & 0xFFFF,
            "zero": (zero_other >> 16) & 0xFFFF,
            "other": zero_other & 0xFFFF,
            "timeouts": (timeout_first >> 16) & 0xFFFF,
            "first_other": timeout_first & 0xFF,
            "first_addr_or_word_rx": (first_rx >> 16) & 0xFFFF,
            "first_data_rx": first_rx & 0xFFFF,
            "ctrl1": f"0x{ctrl1:08X}",
            "ctrl2": f"0x{ctrl2:08X}",
            "sts_or": f"0x{sts_or:08X}",
            "gpio_cfgr": f"0x{gpio_cfgr:08X}",
            "gpio_muxl": f"0x{gpio_muxl:08X}",
        }
        rows.append(row)
    return rows


def write_csv(rows: list[dict]) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = ROOT / "build" / f"spi_matrix_results_{stamp}.csv"
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    return path


def summarize(header: dict, rows: list[dict]) -> None:
    reads = header["reads"]
    clean = [r for r in rows if r["ref_ok"] == r["ref_total"]]
    success = [r for r in clean if r["ok70"] == reads and r["timeouts"] == 0]
    partial = [r for r in clean if 0 < r["ok70"] < reads]
    failed = [r for r in clean if r["ok70"] == 0]

    print("\n=== MATRIX SUMMARY ===")
    print(f"rows={len(rows)}/{header['result_max']}  WHO reads/row={reads}")
    print(f"GPIO reference failed groups={header['ref_fail_groups']}")
    print(f"100% passing rows={len(success)}, partial rows={len(partial)}, zero-pass rows={len(failed)}")

    dist = Counter()
    for r in clean:
        dominant = max(("70", r["ok70"]), ("FF", r["ff"]),
                       ("00", r["zero"]), ("other", r["other"]),
                       key=lambda x: x[1])[0]
        dist[dominant] += 1
    print("dominant result by row:", ", ".join(f"{k}={v}" for k, v in dist.items()))

    if success:
        print("\nPassing configurations (first 40):")
        for r in success[:40]:
            sck = header["apb2_hz"] / r["div"] if r["div"] > 0 else 0
            print(
                f"  {r['cs']:8s} mode{r['mode']} {r['frame']:2d}b "
                f"{r['variant']:26s} {r['drive']:8s} /{r['div']:<4d} "
                f"SCK={sck:9.1f}Hz t={r['timing_us']:3d}us "
                f"OK={r['ok70']}/{reads}"
            )
        if len(success) > 40:
            print(f"  ... {len(success) - 40} more in CSV")
    else:
        print("\nNo hardware-SPI configuration passed 100/100.")

    best = sorted(clean, key=lambda r: (r["ok70"], -r["timeouts"], -r["other"]), reverse=True)[:20]
    print("\nBest 20 rows:")
    for r in best:
        sck = header["apb2_hz"] / r["div"] if r["div"] > 0 else 0
        print(
            f"  {r['cs']:8s} m{r['mode']} {r['frame']:2d}b {r['variant']:26s} "
            f"{r['drive']:8s} /{r['div']:<4d} {sck:9.1f}Hz t={r['timing_us']:3d}us "
            f"70={r['ok70']:3d} FF={r['ff']:3d} 00={r['zero']:3d} "
            f"other={r['other']:3d} timeout={r['timeouts']:3d} "
            f"rx0=0x{r['first_addr_or_word_rx']:04X} rx1=0x{r['first_data_rx']:04X}"
        )

    groups = defaultdict(lambda: [0, 0, 0])
    for r in clean:
        key = (r["cs"], r["mode"], r["frame"], r["variant"])
        groups[key][0] += r["ok70"]
        groups[key][1] += reads
        groups[key][2] += 1
    print("\nAggregate by transaction type:")
    for key, (ok, total, n) in groups.items():
        print(f"  {key[0]:8s} mode{key[1]} {key[2]:2d}b {key[3]:26s}: {ok}/{total} ({n} rows)")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--no-build", action="store_true")
    p.add_argument("--no-flash", action="store_true")
    p.add_argument("--timeout", type=float, default=180.0)
    args = p.parse_args()

    if not args.no_build:
        build()
    if not args.no_flash:
        flash()
        time.sleep(0.3)

    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "attach", "frequency": 400000},
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP probe")
    session.open()
    try:
        target = session.target
        deadline = time.time() + args.timeout
        addr = None
        while addr is None:
            try:
                addr = find_log(target)
            except RuntimeError:
                if time.time() >= deadline:
                    raise
                time.sleep(0.2)
        print(f"spi_matrix_log @ 0x{addr:08X}")

        last_count = -1
        while True:
            h = read_header(target, addr)
            if h["count"] != last_count:
                print(
                    f"progress {h['count']}/{h['result_max']} state={h['state']} "
                    f"cfg=0x{h['current_config']:08X} ref_fail={h['ref_fail_groups']}"
                )
                last_count = h["count"]
            if h["state"] in (2, 3):
                break
            if time.time() >= deadline:
                raise TimeoutError(f"matrix did not finish, count={h['count']}")
            time.sleep(1.0)

        rows = read_results(target, addr, h["count"])
        if not rows:
            print(
                f"No matrix rows: state={h['state']} fatal=0x{h['fatal_code']:X}, "
                f"reference_fail_groups={h['ref_fail_groups']}"
            )
            if h["fatal_code"] == 0x10:
                raw = " ".join(f"0x{h[f'r{i}'] & 0xFF:02X}" for i in range(4))
                print(f"GPIO SPI reference is not 0x70. Raw first reads: {raw}")
                print("Power-cycle the whole board and rerun.")
            return 2
        path = write_csv(rows)
        summarize(h, rows)
        print(f"\nCSV: {path}")
        if h["state"] == 3:
            print(f"FATAL: code={h['fatal_code']}")
            return 2
        return 0
    finally:
        try:
            session.target.resume()
        except Exception:
            pass
        session.close()


if __name__ == "__main__":
    sys.exit(main())

