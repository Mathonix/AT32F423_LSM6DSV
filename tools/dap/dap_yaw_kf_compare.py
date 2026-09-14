#!/usr/bin/env python3
"""Synchronously log VQF/KF yaw and motion diagnostics through CMSIS-DAP."""
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
SYNC_MAGIC = 0x594B4631
VQF_FMT = "<IIi" + "I" * 8 + "f" * 18 + "I" + "f" * 5 + "i" + "I" * 4 + "f" * 3
VQF_SIZE = struct.calcsize(VQF_FMT)
VQF_NAMES = (
    "magic seq init_err whoami clk_hz millis fusion_hz out_hz fusion_n "
    "skip_n vqf_us roll pitch yaw qw qx qy qz gx gy gz ax ay az "
    "bias_x bias_y bias_z rest_time tau_acc rest_detected "
    "mx my mz mag_norm tau_mag mag_err mag_addr mag_updates mag_ready mag_disturbed "
    "temperature_c gyr_lpf_z corrected_z"
).split()
POSE_FMT = "<IIIffff"
POSE_SIZE = struct.calcsize(POSE_FMT)
POSE_NAMES = "magic seq millis yaw pitch roll temperature_c".split()
SYNC_FMT = "<IIIffffIIfff"
SYNC_SIZE = struct.calcsize(SYNC_FMT)
SYNC_NAMES = ("magic seq millis vqf_yaw_deg kf_yaw_deg gz_dps bias_z_dps "
              "rest_detected mag_updates temperature_c gyr_lpf_z corrected_z").split()


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


def unpack_vqf(raw: bytes) -> dict:
    return dict(zip(VQF_NAMES, struct.unpack(VQF_FMT, raw[:VQF_SIZE])))


def unpack_pose(raw: bytes) -> dict:
    return dict(zip(POSE_NAMES, struct.unpack(POSE_FMT, raw[:POSE_SIZE])))


def unpack_sync(raw: bytes) -> dict:
    return dict(zip(SYNC_NAMES, struct.unpack(SYNC_FMT, raw[:SYNC_SIZE])))


def read_stable_block(target, addr: int, size: int, magic: int, unpack):
    """Read a producer snapshot without halting the MCU.

    The firmware increments seq to an odd value before writing and to an even
    value afterwards. Reading the block twice around the transfer detects a
    write that overlapped the SWD transaction.
    """
    for _ in range(8):
        raw = bytes(target.read_memory_block8(addr, size))
        row = unpack(raw)
        seq_before = int(row["seq"])
        seq_after = read_u32(target, addr + 4)
        if (int(row["magic"]) == magic and seq_before != 0
                and not (seq_before & 1) and seq_before == seq_after):
            return row
    raise RuntimeError(f"unstable snapshot at 0x{addr:08X}")


def read_yaw_snapshot(target, sync_addr: int) -> dict:
    """Read the compact snapshot containing one synchronized 200 Hz sample."""
    sync = read_stable_block(target, sync_addr, SYNC_SIZE, SYNC_MAGIC, unpack_sync)
    return {
        "sync_seq": sync["seq"],
        "mcu_ms": sync["millis"],
        "vqf_yaw_deg": sync["vqf_yaw_deg"],
        "kf_yaw_deg": sync["kf_yaw_deg"],
        "gz_dps": sync["gz_dps"],
        "bias_z_dps": sync["bias_z_dps"],
        "rest_detected": sync["rest_detected"],
        "mag_updates": sync["mag_updates"],
        "temperature_c": sync["temperature_c"],
        "gyr_lpf_z_dps": sync["gyr_lpf_z"],
        "corrected_z_dps": sync["corrected_z"],
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
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--hz", type=float, default=200.0)
    ap.add_argument("--output-dir", type=Path, default=None,
                    help="directory for CSV/JSON output (default: build/logs)")
    ap.add_argument("--probe", default=None)
    ap.add_argument("--frequency", type=int, default=2_000_000)
    ap.add_argument("--startup-wait", type=float, default=3.0,
                    help="seconds to wait after resetting the target")
    args = ap.parse_args()
    if args.seconds <= 0 or args.hz <= 0:
        ap.error("--seconds and --hz must be positive")

    folder = args.output_dir or (Path(__file__).resolve().parents[2] / "build" / "logs")
    folder.mkdir(parents=True, exist_ok=True)
    stem = folder / ("yaw_kf_sync_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
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
        sync_addr = locate(target, SYNC_MAGIC)
        print(f"yaw_kf_sync_live @ 0x{sync_addr:08X}; sample target={args.hz:.1f}Hz", flush=True)
        print("Keep the board stationary, or perform the requested motion during capture.", flush=True)
        period = 1.0 / args.hz
        start = time.perf_counter()
        deadline = start
        n = int(math.floor(args.seconds * args.hz)) + 1
        for i in range(n):
            deadline = start + i * period
            delay = deadline - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            sample = read_yaw_snapshot(target, sync_addr)
            now = time.perf_counter()
            row = {
                "host_s": now - start,
                **sample,
            }
            rows.append(row)
            if i % max(1, int(args.hz)) == 0:
                print(
                    f"t={row['host_s']:6.1f}s raw={row['vqf_yaw_deg']:+10.5f} "
                    f"kf={row['kf_yaw_deg']:+10.5f} "
                    f"gz={row['gz_dps']:+8.3f} bias_z={row['bias_z_dps']:+8.4f} "
                    f"lpf_z={row['gyr_lpf_z_dps']:+8.3f} corr_z={row['corrected_z_dps']:+8.3f} "
                    f"rest={int(row['rest_detected'])} mag_n={int(row['mag_updates'])} "
                    f"seq={int(row['sync_seq'])} mcu_ms={int(row['mcu_ms'])}", flush=True
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
    elapsed = ts[-1] - ts[0]
    mcu_span = rows[-1]["mcu_ms"] - rows[0]["mcu_ms"]
    rest_ratio = mean([float(r["rest_detected"]) for r in rows])
    mag_delta = rows[-1]["mag_updates"] - rows[0]["mag_updates"]
    summary = {
        "samples": len(rows),
        "elapsed_s": elapsed,
        "sample_hz": (len(rows) - 1) / max(elapsed, 1e-9),
        "requested_hz": args.hz,
        "mcu_sample_hz": (len(rows) - 1) / max(mcu_span / 1000.0, 1e-9),
        "mcu_span_ms": mcu_span,
        "rest_ratio": rest_ratio,
        "mag_updates_delta": mag_delta,
        "vqf_raw": metrics(ts, raw),
        "yaw_kf": metrics(ts, kf),
        "kf_minus_vqf": {
            "mean_deg": mean(diff),
            "std_deg": std(diff),
            "p2p_deg": p2p(diff),
        },
        "sync_seq_delta": rows[-1]["sync_seq"] - rows[0]["sync_seq"],
        "sync_addr": f"0x{sync_addr:08X}",
        "csv": str(stem.with_suffix(".csv")),
    }
    stem.with_suffix(".json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print("----- summary -----", flush=True)
    print(json.dumps(summary, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

