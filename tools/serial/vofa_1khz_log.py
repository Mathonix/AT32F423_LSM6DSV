"""Read 200 Hz VOFA JustFloat frames from USART4 (default 2 Mbps)."""
from __future__ import annotations

import argparse
import struct
import sys
import time

import serial

N_CH = 18
FRAME = N_CH * 4 + 4
TAIL = b"\x00\x00\x80\x7f"


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM8")
    p.add_argument("--baud", type=int, default=2000000)
    p.add_argument("--seconds", type=float, default=3.0)
    args = p.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    time.sleep(0.1)
    ser.reset_input_buffer()
    t0 = time.perf_counter()
    buf = b""
    n = 0
    last = None
    while time.perf_counter() - t0 < args.seconds:
        buf += ser.read(4096)
        while True:
            i = buf.find(TAIL)
            if i < 0 or i < N_CH * 4:
                if len(buf) > 8 * FRAME:
                    buf = buf[-2 * FRAME :]
                break
            start = i - N_CH * 4
            raw = buf[start:i]
            buf = buf[i + 4 :]
            ch = struct.unpack("<" + "f" * N_CH, raw)
            last = ch
            n += 1
    ser.close()
    dt = max(time.perf_counter() - t0, 1e-6)
    print(f"frames={n}  rate={n / dt:.1f} Hz  baud={args.baud}")
    if last is None:
        print("no JustFloat frames")
        return 1
    names = [
        "yaw_kf", "pitch", "roll", "temperature_c",
        "qw", "qx", "qy", "qz",
        "gx", "gy", "gz", "ax", "ay", "az", "vqf_us", "late",
        "gyr_lpf_z", "corrected_z",
    ]
    for name, val in zip(names, last):
        print(f"{name:10s} {val:10.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
