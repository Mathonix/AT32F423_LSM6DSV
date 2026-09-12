#!/usr/bin/env python3
"""Compare raw VQF yaw and output-only Kalman yaw over a stationary interval."""
from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import time
from datetime import datetime
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

SRAM = 0x20000000
SRAM_SIZE = 0xC000
VQF_MAGIC = 0x56465131
POSE_MAGIC = 0x56504F53
VQF_FMT = "<IIiIIIIIIIIfffffffffffff"
VQF_SIZE = struct.calcsize(VQF_FMT)
VQF_NAMES = (
    "magic seq init_err whoami clk_hz millis fusion_hz out_hz fusion_n "
    "skip_n vqf_us roll pitch yaw qw qx qy qz gx gy gz ax ay az"
).split()
POSE_FMT = "<IIIffff"
POSE_SIZE = struct.calcsize(POSE_FMT)
POSE_NAMES = "magic seq millis yaw pitch roll temperature_c".split()


def locate(target, magic: int) -> int:
    blob = bytes(target.read_memory_block8(SRAM, SRAM_SIZE))
    needle = struct.pack("<I", magic)
    candidates = []
    start = 0
    while True:
        off = blob.find(needle, start)
        if off < 0:
            break
        if off % 4 == 0:
            candidates.append(SRAM + off)
        start = off + 1
    if not candidates:
        raise RuntimeError(f"magic 0x{magic:08X} not found in SRAM")
    return candidates[0]


def read_u32(target, addr: int) -> int:
    return target.read32(addr)


def read_float32(target, addr: int) -> float:
    return struct.unpack("<f", struct.pack("<I", target.read32(addr)))[0]


def read_yaw_snapshot(target, vqf_addr: int, pose_addr: int) -> dict:
    """Read the individual 32-bit yaw words atomically.

    A full snapshot cannot be sequence-locked over a 1 ms producer period at
    the available SWD speed. Each float is one atomic 32-bit SWD read; the two
    values are close enough for a stationary noise comparison.
    """
    return {
        "vqf_seq": read_u32(target, vqf_addr + 4),
        "pose_seq": read_u32(target, pose_addr + 4),
        "mcu_ms": read_u32(target, vqf_addr + 20),
        "vqf_yaw_deg": read_float32(target, vqf_addr + 52),
        "kf_yaw_deg": read_float32(target, pose_addr + 12),
        "bias_x_dps": read_float32(target, vqf_addr + 96),
        "bias_y_dps": read_float32(target, vqf_addr + 100),
        "bias_z_dps": read_float32(target, vqf_addr + 104),
        "fusion_hz": read_u32(target, vqf_addr + 24),
        "skip_n": read_u32(target, vqf_addr + 36),
    }


def unwrap(values: list[float]) -> list[float]:
    if not values:
        return []
    out = [values[0]]
    for x in values[1:]:
        d = x - out[-1]
        while d > 180.0:
            x -= 360.0
            d -= 360.0
        while d < -180.0:
            x += 360.0
            d += 360.0
        out.append(x)
    return out


def mean(xs):
    return sum(xs) / len(xs) if xs else float("nan")


def std(xs):
    if not xs:
        return float("nan")
    m = mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))


def p2p(xs):
    return max(xs) - min(xs) if xs else float("nan")


def detrended_std(ts, xs):
    if len(xs) < 2:
        return float("nan"), float("nan")
    t0 = ts[0]
    t = [x - t0 for x in ts]
    mt, mx = mean(t), mean(xs)
    den = sum((u - mt) ** 2 for u in t)
    slope = sum((u - mt) * (x - mx) for u, x in zip(t, xs)) / den if den else 0.0
    intercept = mx - slope * mt
    residual = [x - (intercept + slope * u) for u, x in zip(t, xs)]
    return std(residual), slope


def metrics(ts, values):
    vals = unwrap(values)
    ds, slope = detrended_std(ts, vals)
    return {
        "mean_deg": mean(vals),
        "std_deg": std(vals),
        "p2p_deg": p2p(vals),
        "detrended_std_deg": ds,
        "linear_drift_deg_s": slope,
        "endpoint_change_deg": vals[-1] - vals[0] if vals else float("nan"),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--hz", type=float, default=20.0)
    ap.add_argument("--probe", default=None)
    ap.add_argument("--frequency", type=int, default=400000)
    ap.add_argument("--startup-wait", type=float, default=3.0,
                    help="seconds to wait after resetting the target")
    args = ap.parse_args()
    if args.seconds <= 0 or args.hz <= 0:
        ap.error("--seconds and --hz must be positive")

    folder = Path(__file__).resolve().parents[1] / "build" / "logs"
    folder.mkdir(parents=True, exist_ok=True)
    stem = folder / ("yaw_kf_compare_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    opts = {
        "connect_mode": "under-reset",
        "frequency": args.frequency,
        "resume_on_disconnect": False,
    }
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m", unique_id=args.probe, options=opts
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP probe")

    rows = []
    with session:
        target = session.target
        target.reset()
        target.resume()
        if args.startup_wait > 0:
            time.sleep(args.startup_wait)
        if target.get_state().name != "RUNNING":
            raise RuntimeError("MCU is not running; resume it before capture")
        vqf_addr = locate(target, VQF_MAGIC)
        pose_addr = locate(target, POSE_MAGIC)
        print(f"vqf_live @ 0x{vqf_addr:08X}; vofa_pose_live @ 0x{pose_addr:08X}", flush=True)
        print("Keep the board completely stationary during capture.", flush=True)
        period = 1.0 / args.hz
        start = time.perf_counter()
        deadline = start
        n = int(math.floor(args.seconds * args.hz)) + 1
        for i in range(n):
            deadline = start + i * period
            delay = deadline - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            sample = read_yaw_snapshot(target, vqf_addr, pose_addr)
            now = time.perf_counter()
            row = {
                "host_s": now - start,
                **sample,
            }
            rows.append(row)
            if i % max(1, int(args.hz * 5)) == 0:
                print(
                    f"t={row['host_s']:6.1f}s raw={row['vqf_yaw_deg']:+10.5f} "
                    f"kf={row['kf_yaw_deg']:+10.5f} "
                    f"diff={row['kf_yaw_deg']-row['vqf_yaw_deg']:+.5f} "
                    f"bias=({row['bias_x_dps']:+.4f},{row['bias_y_dps']:+.4f},{row['bias_z_dps']:+.4f})dps "
                    f"fusion={row['fusion_hz']} skip={row['skip_n']}", flush=True
                )

    fields = list(rows[0].keys())
    with stem.with_suffix(".csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    ts = [r["host_s"] for r in rows]
    raw = [r["vqf_yaw_deg"] for r in rows]
    kf = [r["kf_yaw_deg"] for r in rows]
    diff = [a - b for a, b in zip(unwrap(kf), unwrap(raw))]
    summary = {
        "samples": len(rows),
        "elapsed_s": ts[-1] - ts[0],
        "sample_hz": (len(rows) - 1) / max(ts[-1] - ts[0], 1e-9),
        "vqf_raw": metrics(ts, raw),
        "yaw_kf": metrics(ts, kf),
        "kf_minus_vqf": {
            "mean_deg": mean(diff),
            "std_deg": std(diff),
            "p2p_deg": p2p(diff),
        },
        "fusion_hz_min": min(r["fusion_hz"] for r in rows),
        "fusion_hz_max": max(r["fusion_hz"] for r in rows),
        "skip_delta": rows[-1]["skip_n"] - rows[0]["skip_n"],
        "vqf_addr": f"0x{vqf_addr:08X}",
        "pose_addr": f"0x{pose_addr:08X}",
        "csv": str(stem.with_suffix(".csv")),
    }
    stem.with_suffix(".json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print("----- summary -----", flush=True)
    print(json.dumps(summary, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

