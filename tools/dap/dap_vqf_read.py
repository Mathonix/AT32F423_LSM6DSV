"""Flash (optional) and read an exact number of VQF snapshots via CMSIS-DAP."""
from __future__ import annotations

import argparse
import struct
import sys
import time

from pyocd.core.helpers import ConnectHelper

MAGIC = 0x56465131
SRAM = 0x20000000
SRAM_SIZE = 0xC000
DEFAULT_SWD_FREQUENCY = 2_000_000
FMT = "<IIi" + "I" * 8 + "f" * 13 + "f" * 5 + "I" + "f" * 5 + "i" + "I" * 4 + "f" * 3
SIZE = struct.calcsize(FMT)
NAMES = [
    "magic", "seq", "init_err", "whoami", "clk_hz", "millis",
    "fusion_hz", "out_hz", "fusion_n", "skip_n", "vqf_us",
    "roll", "pitch", "yaw", "qw", "qx", "qy", "qz",
    "gx", "gy", "gz", "ax", "ay", "az",
    "bias_x", "bias_y", "bias_z", "rest_time", "tau_acc",
    "rest_detected",
    "mx", "my", "mz", "mag_norm", "tau_mag",
    "mag_err", "mag_addr", "mag_updates", "mag_ready", "mag_disturbed",
    "temperature_c", "gyr_lpf_z", "corrected_z",
]


def find_live(target) -> int:
    blob = bytes(target.read_memory_block8(SRAM, SRAM_SIZE))
    needle = MAGIC.to_bytes(4, "little")
    off = blob.find(needle)
    if off < 0 or (off % 4):
        raise RuntimeError("vqf_live magic not found in SRAM")
    return SRAM + off


def unpack(raw: bytes) -> dict:
    return dict(zip(NAMES, struct.unpack(FMT, raw[:SIZE])))


def read_live(target, addr: int) -> dict:
    d = unpack(bytes(target.read_memory_block8(addr, SIZE)))
    if d["magic"] != MAGIC:
        raise RuntimeError(f"bad magic 0x{d['magic']:08X} at 0x{addr:08X}")
    return d


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--count", type=int, default=10,
                   help="exact snapshot count (default: 10)")
    p.add_argument("--period", type=float, default=1.0,
                   help="seconds between scheduled snapshots (default: 1.0)")
    p.add_argument("--seconds", type=float, default=None,
                   help="compatibility option; with no --period override, period=seconds/count")
    p.add_argument("--flash", action="store_true")
    p.add_argument("--frequency", type=int, default=DEFAULT_SWD_FREQUENCY,
                   help="SWD clock frequency in Hz")
    args = p.parse_args()
    if args.count <= 0 or args.period < 0:
        p.error("--count must be > 0 and --period must be >= 0")
    if args.seconds is not None and args.period == 1.0:
        if args.seconds <= 0:
            p.error("--seconds must be > 0")
        args.period = args.seconds / args.count

    if args.flash:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from dap_flash_and_log import HEX_PATH, parse_hex, flash_target
        flash_target(parse_hex(HEX_PATH))
        time.sleep(0.4)

    opts = {"connect_mode": "attach", "frequency": args.frequency}
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m", options=opts
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP probe")
    session.open()
    try:
        target = session.target
        try:
            target.halt()
        except Exception:
            pass
        addr = find_live(target)
        try:
            target.resume()
        except Exception:
            pass

        # After flashing, wait for the first valid fused snapshot so sample 1
        # is a real attitude rather than the zero-initialized SRAM structure.
        ready_deadline = time.perf_counter() + 8.0
        while True:
            try:
                target.halt()
            except Exception:
                pass
            ready = read_live(target, addr)
            try:
                target.resume()
            except Exception:
                pass
            if ready["init_err"] != 0:
                raise RuntimeError(
                    f"IMU init failed: err={ready['init_err']} WHO=0x{ready['whoami']:02X}"
                )
            if ready["whoami"] == 0x70 and ready["seq"] > 0:
                break
            if time.perf_counter() >= ready_deadline:
                raise RuntimeError("timed out waiting for the first valid attitude")
            time.sleep(0.05)

        rows = []
        start = time.perf_counter()
        print(f"vqf_live @ 0x{addr:08X}; count={args.count}; period={args.period:.3f}s")
        print("No. Host(s) MCU(ms)   Roll(deg) Pitch(deg)  Yaw(deg)   Gz     BiasZ  LPFZ  CorrZ TempC")
        print("--- ------- ------- ----------- ---------- --------- ------ ------ ------ ------ -----")

        for i in range(args.count):
            deadline = start + i * args.period
            remain = deadline - time.perf_counter()
            if remain > 0:
                time.sleep(remain)
            try:
                target.halt()
            except Exception:
                pass
            d = read_live(target, addr)
            try:
                target.resume()
            except Exception:
                pass
            host_s = time.perf_counter() - start
            rows.append(d)
            print(
                f"{i + 1:3d} {host_s:7.3f} {d['millis']:7d} "
                f"{d['roll']:11.2f} {d['pitch']:10.2f} {d['yaw']:9.2f} "
                f"{d['gz']:+6.2f} {d['bias_z']:+7.3f} {d['gyr_lpf_z']:+6.2f} "
                f"{d['corrected_z']:+6.2f} {d['temperature_c']:+6.2f}"
            )

        last = rows[-1]
        seq_advanced = len(rows) == 1 or rows[-1]["seq"] != rows[0]["seq"]
        print(
            f"STATUS: clk={last['clk_hz']}Hz WHO=0x{last['whoami']:02X} "
            f"init_err={last['init_err']} fusion_hz={last['fusion_hz']} "
            f"out_hz={last['out_hz']} skip_n={last['skip_n']} "
            f"seq={rows[0]['seq']}->{last['seq']}"
        )
        if last["init_err"] != 0 or last["whoami"] != 0x70:
            print("RESULT: IMU initialization failed")
            return 2
        if not seq_advanced:
            print("RESULT: snapshot is not updating")
            return 3
        print("RESULT: 10-second attitude capture OK" if args.count == 10 else "RESULT: attitude capture OK")
        return 0
    finally:
        try:
            session.target.resume()
        except Exception:
            pass
        session.close()


if __name__ == "__main__":
    sys.exit(main())
