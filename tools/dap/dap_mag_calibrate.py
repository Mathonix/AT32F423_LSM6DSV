"""Collect IST8310 samples through the running VQF firmware and calculate
axis-aligned hard/soft-iron calibration coefficients.

Rotate the complete assembled board slowly through as many orientations as
possible during capture. Keep away from steel tools, speakers and power wires.
"""
from __future__ import annotations

import argparse
import csv
import math
import sys
import time
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dap_vqf_tune import find_snapshot, read_coherent, wait_snapshot_ready

ROOT = Path(__file__).resolve().parents[2]


def main() -> int:
    p = argparse.ArgumentParser(description="IST8310 hard/soft-iron calibration")
    p.add_argument("--seconds", type=float, default=60.0)
    p.add_argument("--rate", type=float, default=20.0)
    p.add_argument("--frequency", type=int, default=1_000_000)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    if args.seconds < 10.0 or not (1.0 <= args.rate <= 30.0):
        p.error("--seconds must be >=10 and --rate must be within 1..30 Hz")

    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "attach", "frequency": args.frequency},
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP/DAPLink probe is connected")

    rows = []
    session.open()
    try:
        target = session.target
        addr = find_snapshot(target)
        wait_snapshot_ready(target, addr)
        print(f"vqf_tune_live @ 0x{addr:08X}")
        print("ROTATE NOW: slowly rotate/figure-eight the complete board through all axes.", flush=True)
        start = time.perf_counter()
        deadline = start
        next_report = 5.0
        last_seq = None
        while True:
            elapsed = time.perf_counter() - start
            if elapsed >= args.seconds:
                break
            if time.perf_counter() < deadline:
                time.sleep(deadline - time.perf_counter())
            row = read_coherent(target, addr)
            if row["seq"] != last_seq and int(row["mag_err"]) == 0:
                rows.append(row)
                last_seq = row["seq"]
            elapsed = time.perf_counter() - start
            if elapsed >= next_report:
                vals = [[float(r[k]) for r in rows] for k in ("mx", "my", "mz")]
                spans = [max(v)-min(v) if v else 0.0 for v in vals]
                print(f"  {elapsed:4.0f}s samples={len(rows):4d} spans(uT)="
                      f"X:{spans[0]:5.1f} Y:{spans[1]:5.1f} Z:{spans[2]:5.1f}", flush=True)
                next_report += 5.0
            deadline += 1.0 / args.rate
    finally:
        try:
            session.target.resume()
        except Exception:
            pass
        session.close()

    if len(rows) < 30:
        raise RuntimeError("not enough valid magnetometer samples")

    axes = {k: [float(r[k]) for r in rows] for k in ("mx", "my", "mz")}
    mins = [min(axes[k]) for k in ("mx", "my", "mz")]
    maxs = [max(axes[k]) for k in ("mx", "my", "mz")]
    offsets = [(lo+hi)*0.5 for lo,hi in zip(mins,maxs)]
    radii = [(hi-lo)*0.5 for lo,hi in zip(mins,maxs)]
    if min(radii) < 5.0:
        raise RuntimeError(f"insufficient rotation coverage, half-ranges={radii}")
    mean_radius = sum(radii)/3.0
    scales = [mean_radius/r for r in radii]

    corrected_norms = []
    octants = set()
    for r in rows:
        v = [(float(r[k])-offsets[i])*scales[i] for i,k in enumerate(("mx","my","mz"))]
        corrected_norms.append(math.sqrt(sum(x*x for x in v)))
        octants.add(tuple(x >= 0.0 for x in v))
    norm_mean = sum(corrected_norms)/len(corrected_norms)
    norm_rms = math.sqrt(sum((n-norm_mean)**2 for n in corrected_norms)/len(corrected_norms))

    out = args.output or ROOT/"build"/f"mag_cal_{time.strftime('%Y%m%d_%H%M%S')}.csv"
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="", encoding="utf-8") as fp:
        w = csv.DictWriter(fp, fieldnames=["millis","mx","my","mz","mag_norm"])
        w.writeheader()
        for r in rows:
            w.writerow({k:r[k] for k in ("millis","mx","my","mz","mag_norm")})

    print("\n=== IST8310 calibration ===")
    for i,name in enumerate("XYZ"):
        print(f"{name}: min={mins[i]:+8.3f} max={maxs[i]:+8.3f} "
              f"offset={offsets[i]:+8.3f}uT radius={radii[i]:7.3f}uT scale={scales[i]:.6f}")
    print(f"coverage: {len(octants)}/8 octants")
    print(f"corrected field norm: mean={norm_mean:.3f}uT rms_error={norm_rms:.3f}uT")
    print("\nPaste into main.c (values are already in uT):")
    print(f"#define MAG_OFF_X_UT  ({offsets[0]:+.6f}f)")
    print(f"#define MAG_OFF_Y_UT  ({offsets[1]:+.6f}f)")
    print(f"#define MAG_OFF_Z_UT  ({offsets[2]:+.6f}f)")
    print(f"#define MAG_SCALE_X   ({scales[0]:.6f}f)")
    print(f"#define MAG_SCALE_Y   ({scales[1]:.6f}f)")
    print(f"#define MAG_SCALE_Z   ({scales[2]:.6f}f)")
    print(f"CSV: {out}")
    if len(octants) < 7:
        print("WARNING: coverage is incomplete; repeat while rotating through all axes.")
        return 3
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
