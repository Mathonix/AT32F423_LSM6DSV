"""Sample vqf_live roll/pitch/yaw via MicroLink SWD and emit JustFloat.

MKLink COM14 is a probe REPL, not a data pipe. This tool uses the same
MicroLink CMSIS-DAP to read RAM (SuperWatch-style) and streams JustFloat
to TCP so VOFA+ can plot without MCU UART.

VOFA+: add TCP client, host 127.0.0.1, port 1347, protocol JustFloat.
Channels: 0=roll 1=pitch 2=yaw (deg).
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import time

from pyocd.core.helpers import ConnectHelper

MAGIC = 0x56465131
SRAM = 0x20000000
SRAM_SIZE = 0xC000
LIVE_FMT = "<IIiIIIIIIIIfffffffffffff"
LIVE_SIZE = struct.calcsize(LIVE_FMT)
RPY_OFF = 44
TAIL = b"\x00\x00\x80\x7f"


def find_live(target) -> int:
    blob = bytes(target.read_memory_block8(SRAM, SRAM_SIZE))
    off = blob.find(MAGIC.to_bytes(4, "little"))
    if off < 0 or (off % 4):
        raise RuntimeError("vqf_live magic not found")
    return SRAM + off


def read_rpy(target, addr: int) -> tuple[float, float, float, int]:
    raw = bytes(target.read_memory_block8(addr, LIVE_SIZE))
    mag, seq = struct.unpack_from("<II", raw, 0)
    if mag != MAGIC:
        raise RuntimeError(f"bad magic 0x{mag:08X}")
    roll, pitch, yaw = struct.unpack_from("<fff", raw, RPY_OFF)
    return roll, pitch, yaw, seq


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--hz", type=float, default=100.0)
    p.add_argument("--seconds", type=float, default=0.0, help="0 = until Ctrl+C")
    p.add_argument("--tcp", type=int, default=1347, help="JustFloat TCP port; 0 disables")
    p.add_argument("--print-every", type=float, default=0.5)
    args = p.parse_args()
    period = 1.0 / max(args.hz, 1.0)

    opts = {"connect_mode": "attach", "frequency": 2_000_000}
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m", options=opts
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP probe")
    session.open()
    target = session.target
    target.resume()
    addr = find_live(target)
    print(f"vqf_live @ 0x{addr:08X}  RPY @ 0x{addr + RPY_OFF:08X}")
    print("channels: roll pitch yaw  (deg)")

    server = None
    clients: list[socket.socket] = []
    if args.tcp:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", args.tcp))
        server.listen(4)
        server.setblocking(False)
        print(f"VOFA+ TCP JustFloat: 127.0.0.1:{args.tcp}")

    t0 = time.perf_counter()
    last_print = t0
    n = 0
    last_seq = -1
    try:
        while True:
            now = time.perf_counter()
            if args.seconds > 0 and (now - t0) >= args.seconds:
                break
            if server is not None:
                try:
                    conn, _ = server.accept()
                    conn.setblocking(False)
                    clients.append(conn)
                    print("VOFA client connected")
                except BlockingIOError:
                    pass
            try:
                target.halt()
            except Exception:
                pass
            try:
                roll, pitch, yaw, seq = read_rpy(target, addr)
            finally:
                try:
                    target.resume()
                except Exception:
                    pass
            frame = struct.pack("<fff", roll, pitch, yaw) + TAIL
            alive = []
            for c in clients:
                try:
                    c.sendall(frame)
                    alive.append(c)
                except OSError:
                    try:
                        c.close()
                    except OSError:
                        pass
            clients = alive
            n += 1
            if (now - last_print) >= args.print_every:
                hz = n / max(now - t0, 1e-6)
                print(
                    f"t={now - t0:6.1f}s  seq={seq:8d}  "
                    f"R={roll:8.2f}  P={pitch:8.2f}  Y={yaw:8.2f}  "
                    f"swd={hz:.0f}Hz"
                )
                last_print = now
                last_seq = seq
            elapsed = time.perf_counter() - now
            sleep = period - elapsed
            if sleep > 0:
                time.sleep(sleep)
    except KeyboardInterrupt:
        print("stop")
    finally:
        for c in clients:
            try:
                c.close()
            except OSError:
                pass
        if server is not None:
            server.close()
        try:
            session.target.resume()
        except Exception:
            pass
        session.close()
    dt = max(time.perf_counter() - t0, 1e-6)
    print(f"frames={n}  rate={n / dt:.1f} Hz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
