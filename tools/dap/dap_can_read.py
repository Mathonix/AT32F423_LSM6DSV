"""Read continuous CAN2 status and parse Damiao format frames over CMSIS-DAP."""
from __future__ import annotations

import argparse
import struct
import time
from pyocd.core.helpers import ConnectHelper

MAGIC = 0x43414E31
SRAM = 0x20000000
SRAM_SIZE = 0xC000
FMT = "<34I"
SIZE = struct.calcsize(FMT)
NAMES = [
    "magic", "seq", "millis", "init_ok", "tx_count",
    "tx_success_count", "tx_failed_count", "tx_no_mailbox_count",
    "tx_pending_count", "tx_error_counter", "rx_error_counter",
    "error_record", "bus_off", "error_passive", "last_mailbox",
    "last_status", "last_data_counter", "rx_count", "rx_standard_count",
    "rx_extended_count", "rx_overrun_count", "rx_pending", "rx_last_id",
    "rx_last_dlc", "rx_last_frame_type", "rx_last_millis",
    "rx_data0", "rx_data1", "rx_data2", "rx_data3", "rx_data4",
    "rx_data5", "rx_data6", "rx_data7",
]


def uint_to_float(x_int: int, x_min: float, x_max: float, bits: int) -> float:
    span = x_max - x_min
    return float(x_int) * span / float((1 << bits) - 1) + x_min


def decode_damiao_can_frame(data: bytes) -> str:
    if len(data) < 8:
        return f"Raw({data.hex()})"
    ftype = data[0]
    if ftype == 0x03:  # Euler
        pitch_u = data[2] | (data[3] << 8)
        yaw_u = data[4] | (data[5] << 8)
        roll_u = data[6] | (data[7] << 8)
        pitch = uint_to_float(pitch_u, -90.0, 90.0, 16)
        yaw = uint_to_float(yaw_u, -180.0, 180.0, 16)
        roll = uint_to_float(roll_u, -180.0, 180.0, 16)
        return f"Damiao Euler: Pitch={pitch:7.2f}°, Yaw={yaw:7.2f}°, Roll={roll:7.2f}°"
    elif ftype == 0x02:  # Gyro
        gx_u = data[2] | (data[3] << 8)
        gy_u = data[4] | (data[5] << 8)
        gz_u = data[6] | (data[7] << 8)
        gx = uint_to_float(gx_u, -34.88, 34.88, 16) * 57.29578
        gy = uint_to_float(gy_u, -34.88, 34.88, 16) * 57.29578
        gz = uint_to_float(gz_u, -34.88, 34.88, 16) * 57.29578
        return f"Damiao Gyro: Gx={gx:7.1f} dps, Gy={gy:7.1f} dps, Gz={gz:7.1f} dps"
    elif ftype == 0x01:  # Accel
        temp = data[1]
        ax_u = data[2] | (data[3] << 8)
        ay_u = data[4] | (data[5] << 8)
        az_u = data[6] | (data[7] << 8)
        ax = uint_to_float(ax_u, -235.2, 235.2, 16) / 9.80665
        ay = uint_to_float(ay_u, -235.2, 235.2, 16) / 9.80665
        az = uint_to_float(az_u, -235.2, 235.2, 16) / 9.80665
        return f"Damiao Accel: Ax={ax:6.2f}g, Ay={ay:6.2f}g, Az={az:6.2f}g, Temp={temp}°C"
    elif ftype == 0x04:  # Quaternion
        w = (data[1] << 6) | ((data[2] & 0xFC) >> 2)
        x = ((data[2] & 0x03) << 12) | (data[3] << 4) | ((data[4] & 0xF0) >> 4)
        y = ((data[4] & 0x0F) << 10) | (data[5] << 2) | ((data[6] & 0xC0) >> 6)
        z = ((data[6] & 0x3F) << 8) | data[7]
        qw = uint_to_float(w, -1.0, 1.0, 14)
        qx = uint_to_float(x, -1.0, 1.0, 14)
        qy = uint_to_float(y, -1.0, 1.0, 14)
        qz = uint_to_float(z, -1.0, 1.0, 14)
        return f"Damiao Quat: Qw={qw:.4f}, Qx={qx:.4f}, Qy={qy:.4f}, Qz={qz:.4f}"
    return f"Damiao Type 0x{ftype:02X} Raw: {data.hex()}"


def find_live(target: object) -> int:
    blob = bytes(target.read_memory_block8(SRAM, SRAM_SIZE))
    needle = MAGIC.to_bytes(4, "little")
    off = blob.find(needle)
    if off < 0 or off % 4:
        raise RuntimeError("CAN test live magic not found in SRAM")
    return SRAM + off


def read_live(target: object, addr: int) -> dict[str, int]:
    raw = bytes(target.read_memory_block8(addr, SIZE))
    return dict(zip(NAMES, struct.unpack(FMT, raw)))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--period", type=float, default=1.0)
    ap.add_argument("--frequency", type=int, default=1_000_000)
    args = ap.parse_args()

    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "attach", "frequency": args.frequency},
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP probe")
    session.open()
    try:
        target = session.target
        target.halt()
        addr = find_live(target)
        target.resume()
        print(f"can_test_live @ 0x{addr:08X}; duration={args.seconds:.1f}s")
        print("host_s MCU_ms init tx tx_ok tx_fail no_mb tec rec err busoff rx rx_std rx_ext rx_ovr last_rx_id last_rx_dlc")
        t0 = time.perf_counter()
        next_sample = t0
        last = None
        while time.perf_counter() - t0 < args.seconds:
            if time.perf_counter() < next_sample:
                time.sleep(max(0.001, next_sample - time.perf_counter()))
            next_sample += args.period
            target.halt()
            d = read_live(target, addr)
            target.resume()
            last = d
            print(
                f"{time.perf_counter()-t0:7.3f} {d['millis']:6d} "
                f"{d['init_ok']:4d} {d['tx_count']:5d} "
                f"{d['tx_success_count']:5d} {d['tx_failed_count']:7d} "
                f"{d['tx_no_mailbox_count']:5d} {d['tx_error_counter']:3d} "
                f"{d['rx_error_counter']:3d} {d['error_record']:3d} "
                f"{d['bus_off']:6d} {d['rx_count']:3d} "
                f"{d['rx_standard_count']:6d} {d['rx_extended_count']:6d} "
                f"{d['rx_overrun_count']:6d} {d['rx_last_id']:11d} "
                f"{d['rx_last_dlc']:11d}"
            )
        if last is None:
            return 1
        if last["init_ok"] != 1:
            print("RESULT: CAN2 initialization failed")
            return 2
        if last["rx_count"]:
            raw_bytes = bytes(last[f"rx_data{i}"] for i in range(last["rx_last_dlc"]))
            decoded = decode_damiao_can_frame(raw_bytes)
            print(
                "RESULT: received CAN2 frame(s); "
                f"last_id=0x{last['rx_last_id']:X} "
                f"dlc={last['rx_last_dlc']} -> {decoded}"
            )
            return 0
        if last["tx_success_count"]:
            print("RESULT: CAN2 frames acknowledged/transmitted, no RX frame captured")
            return 0
        if last["tx_count"]:
            print("RESULT: transmit requests ran, but no ACK/RX frame was observed")
        else:
            print("RESULT: CAN2 initialized, but no RX frame was observed")
        return 4
    finally:
        try:
            session.target.resume()
        except Exception:
            pass
        session.close()


if __name__ == "__main__":
    raise SystemExit(main())
