"""Read coherent 3-axis IMU/VQF tuning telemetry through a CMSIS-DAP probe.

The firmware publishes a low-rate ``vqf_tune_live`` snapshot in SRAM. This tool
briefly halts the core for each SRAM read so the complete structure is coherent,
stores a CSV file, and prints stationary noise, bias and attitude-drift statistics.
"""
from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import sys
import time

from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ELF = ROOT / "build" / "lsm6dsv_spi_test.elf"
MAGIC = 0x56514654  # VQF_TUNE_MAGIC
FMT = "<6I19fi4I3f"
SIZE = struct.calcsize(FMT)
NAMES = [
    "magic", "seq", "millis", "fusion_hz", "skip_n", "rest_detected",
    "roll", "pitch", "yaw",
    "gx", "gy", "gz", "ax", "ay", "az",
    "bias_x", "bias_y", "bias_z", "rest_time", "tau_acc",
    "mx", "my", "mz", "mag_norm", "tau_mag",
    "mag_err", "mag_addr", "mag_updates", "mag_ready", "mag_disturbed",
    "temperature_c", "gyr_lpf_z", "corrected_z",
]


def elf_symbol_address(elf: Path, symbol: str) -> int:
    """Resolve a global from the exact ELF that describes the target image."""
    candidates = [
        shutil.which("arm-none-eabi-nm"),
        Path.home() / ".platformio" / "packages" /
        "toolchain-gccarmnoneeabi" / "bin" / "arm-none-eabi-nm.exe",
    ]
    nm = next((str(path) for path in candidates if path and Path(path).is_file()), None)
    if nm is None:
        raise RuntimeError("arm-none-eabi-nm was not found in PATH or PlatformIO")
    if not elf.is_file():
        raise RuntimeError(f"ELF not found: {elf}")

    output = subprocess.check_output(
        [nm, "--defined-only", str(elf)], text=True, errors="replace"
    )
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-1] == symbol:
            return int(fields[0], 16)
    raise RuntimeError(f"symbol {symbol!r} not found in {elf}")


def validate_snapshot_address(target, addr: int) -> None:
    """Reject an ELF/firmware mismatch before collecting invalid telemetry."""
    target.halt()
    magic = target.read32(addr)
    if magic != MAGIC:
        raise RuntimeError(
            f"ELF/firmware mismatch: vqf_tune_live at 0x{addr:08X} "
            f"contains 0x{magic:08X}, expected 0x{MAGIC:08X}; "
            "flash the matching build"
        )


def unpack_snapshot(raw: bytes) -> dict[str, int | float]:
    return dict(zip(NAMES, struct.unpack(FMT, raw[:SIZE])))


def find_snapshot(target, elf: Path = DEFAULT_ELF) -> int:
    """Compatibility entry point for the magnetic calibration tool."""
    addr = elf_symbol_address(elf, "vqf_tune_live")
    validate_snapshot_address(target, addr)
    return addr


def wait_snapshot_ready(target, addr: int, timeout: float = 5.0) -> None:
    """Let firmware finish reset/calibration before halted sampling starts."""
    target.resume()
    deadline = time.perf_counter() + timeout
    while time.perf_counter() < deadline:
        seq = target.read32(addr + 4)
        if seq != 0 and not (seq & 1):
            return
        time.sleep(0.05)
    raise RuntimeError("firmware snapshot did not start within 5 s")


def read_coherent(target, addr: int, retries: int = 20) -> dict[str, int | float]:
    """Briefly halt the core and read one atomic telemetry snapshot.

    A 120-byte SWD transfer can overlap a firmware update even at only 20 Hz.
    Halting avoids that tearing. If the core happened to stop in the tiny odd-seq
    update window, resume it briefly and retry.
    """
    for _ in range(retries):
        target.halt()
        try:
            raw = bytes(target.read_memory_block8(addr, SIZE))
            row = unpack_snapshot(raw)
            if (
                row["magic"] == MAGIC
                and int(row["seq"]) != 0
                and not (int(row["seq"]) & 1)
            ):
                return row
        finally:
            target.resume()
        time.sleep(0.002)
    raise RuntimeError("could not obtain a coherent halted DAP snapshot")


def unwrap_degrees(values: list[float]) -> list[float]:
    if not values:
        return []
    out = [values[0]]
    for value in values[1:]:
        delta = value - values[len(out) - 1]
        while delta > 180.0:
            delta -= 360.0
        while delta < -180.0:
            delta += 360.0
        out.append(out[-1] + delta)
    return out


def linear_slope(values: list[float], dt: float) -> float:
    if len(values) < 2 or dt <= 0.0:
        return 0.0
    n = len(values)
    mean_x = (n - 1) * dt * 0.5
    mean_y = statistics.fmean(values)
    den = 0.0
    num = 0.0
    for i, value in enumerate(values):
        x = i * dt
        dx = x - mean_x
        den += dx * dx
        num += dx * (value - mean_y)
    return num / den if den else 0.0


def stats(values: list[float]) -> tuple[float, float, float]:
    mean = statistics.fmean(values)
    std = statistics.pstdev(values) if len(values) > 1 else 0.0
    pp = max(values) - min(values)
    return mean, std, pp


def print_report(rows: list[dict[str, int | float]], elapsed: float) -> None:
    print("\n=== DAPLink three-axis report ===")
    print(f"samples={len(rows)} elapsed={elapsed:.3f}s rate={len(rows) / elapsed:.1f}Hz")
    print(f"fusion_hz={int(rows[-1]['fusion_hz'])} skip_n={int(rows[-1]['skip_n'])} "
          f"capture_skip_delta={int(rows[-1]['skip_n']) - int(rows[0]['skip_n'])}")
    print(f"tau_acc={float(rows[-1]['tau_acc']):.3f}s tau_mag={float(rows[-1]['tau_mag']):.3f}s")
    print(f"IST8310 addr=0x{int(rows[-1]['mag_addr']):02X} err={int(rows[-1]['mag_err'])} updates={int(rows[-1]['mag_updates'])} ready={int(rows[-1]['mag_ready'])} disturbed={int(rows[-1]['mag_disturbed'])}")
    print(f"Mag (uT): x={float(rows[-1]['mx']):+.2f} y={float(rows[-1]['my']):+.2f} z={float(rows[-1]['mz']):+.2f} norm={float(rows[-1]['mag_norm']):.2f}")

    print("\nGyroscope (dps):")
    for axis in ("gx", "gy", "gz"):
        values = [float(r[axis]) for r in rows]
        mean, std, pp = stats(values)
        print(f"  {axis}: mean={mean:+.5f}  std={std:.5f}  p-p={pp:.5f}")

    print("Gyro bias estimate (dps):")
    for axis in ("bias_x", "bias_y", "bias_z"):
        values = [float(r[axis]) for r in rows]
        mean, std, pp = stats(values)
        print(f"  {axis}: mean={mean:+.5f}  std={std:.5f}  p-p={pp:.5f}")

    print("Acceleration (g):")
    for axis in ("ax", "ay", "az"):
        values = [float(r[axis]) for r in rows]
        mean, std, pp = stats(values)
        print(f"  {axis}: mean={mean:+.5f}  std={std:.5f}  p-p={pp:.5f}")
    acc_norm = [
        math.sqrt(float(r["ax"]) ** 2 + float(r["ay"]) ** 2 + float(r["az"]) ** 2)
        for r in rows
    ]
    mean, std, pp = stats(acc_norm)
    print(f"  |a|: mean={mean:.5f}  std={std:.5f}  p-p={pp:.5f}")

    dt = elapsed / max(len(rows) - 1, 1)
    print("Attitude drift (deg/s, linear fit):")
    for axis in ("roll", "pitch", "yaw"):
        values = unwrap_degrees([float(r[axis]) for r in rows])
        print(f"  {axis}: {linear_slope(values, dt):+.6f}")

    rest_ratio = statistics.fmean(float(r["rest_detected"]) for r in rows)
    print(f"rest_detected={rest_ratio * 100.0:.1f}%  rest_time={float(rows[-1]['rest_time']):.3f}s")
    if rest_ratio < 0.8:
        print("NOTE: rest detection was not stable; keep the board still or review thresholds.")
    if abs(linear_slope(unwrap_degrees([float(r["yaw"]) for r in rows]), dt)) > 0.05:
        print("NOTE: yaw drift is still significant; verify IST8310 axis mapping and magnetic calibration.")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--rate", type=float, default=10.0,
                        help="DAP sample rate, Hz (10 Hz recommended for halted reads)")
    parser.add_argument("--warmup", type=float, default=6.0,
                        help="seconds to let Full VQF settle before capture")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--elf", type=Path, default=DEFAULT_ELF,
                        help="ELF used to resolve vqf_tune_live")
    parser.add_argument("--frequency", type=int, default=2_000_000,
                        help="SWD frequency in Hz")
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    if args.seconds <= 0.0 or args.warmup < 0.0 or not (0.1 <= args.rate <= 100.0):
        parser.error("--seconds must be >0, --warmup >=0, and --rate within 0.1..100 Hz")

    probes = ConnectHelper.get_all_connected_probes(blocking=False)
    if not probes:
        raise RuntimeError("no CMSIS-DAP/DAPLink probe is connected")

    if args.flash:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from dap_flash_and_log import HEX_PATH, flash_target, parse_hex
        flash_target(parse_hex(HEX_PATH))
        time.sleep(0.5)

    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "attach", "frequency": args.frequency},
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP/DAPLink probe is connected")

    session.open()
    rows: list[dict[str, int | float]] = []
    try:
        target = session.target
        addr = elf_symbol_address(args.elf.resolve(), "vqf_tune_live")
        validate_snapshot_address(target, addr)
        wait_snapshot_ready(target, addr)
        print(f"vqf_tune_live @ 0x{addr:08X}, size={SIZE}, target rate={args.rate:.1f}Hz")
        print("Keep the board completely still during a stationary tuning capture.")
        if args.warmup > 0.0:
            print(f"Full VQF warm-up: {args.warmup:.1f}s")
            time.sleep(args.warmup)

        period = 1.0 / args.rate
        start = time.perf_counter()
        deadline = start
        last_seq = None
        while True:
            now = time.perf_counter()
            if now - start >= args.seconds:
                break
            if now < deadline:
                time.sleep(deadline - now)
            row = read_coherent(target, addr)
            if row["seq"] != last_seq:
                rows.append(row)
                last_seq = row["seq"]
            deadline += period
        elapsed = time.perf_counter() - start
    finally:
        try:
            session.target.resume()
        except Exception:
            pass
        session.close()

    if len(rows) < 2:
        raise RuntimeError("not enough changing snapshots were captured")

    output = args.output
    if output is None:
        stamp = time.strftime("%Y%m%d_%H%M%S")
        output = ROOT / "build" / f"vqf_tune_{stamp}.csv"
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=NAMES)
        writer.writeheader()
        writer.writerows(rows)
    print_report(rows, elapsed)
    print(f"CSV: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
