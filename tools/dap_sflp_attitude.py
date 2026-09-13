"""Enable LSM6DSV SFLP via WCH CMSIS-DAP SWD bitbang SPI.
Record ten 1 s spot attitude samples, then print a table.
"""
from __future__ import annotations

import math
import struct
import time
from pyocd.core.helpers import ConnectHelper

GPIOA = 0x40020000
CFGR, IDT, SCR = 0x00, 0x10, 0x18

REG_FUNC_CFG_ACCESS = 0x01
REG_IF_CFG = 0x03
REG_FIFO_CTRL4 = 0x0A
REG_WHO_AM_I = 0x0F
REG_CTRL1 = 0x10
REG_CTRL2 = 0x11
REG_CTRL3 = 0x12
REG_CTRL6 = 0x15
REG_CTRL8 = 0x17
REG_FIFO_STATUS1 = 0x1B
REG_FIFO_STATUS2 = 0x1C
REG_FIFO_DATA_OUT_TAG = 0x78

EMB_FUNC_EN_A = 0x04
EMB_FUNC_FIFO_EN_A = 0x44
SFLP_ODR = 0x5E

TAG_SFLP_GAME = 0x13  # FIFO tag >> 3
TAG_SFLP_GBIAS = 0x16  # FIFO tag >> 3

WHO_AM_I_VAL = 0x70


def f16_to_f32(h: int) -> float:
    h &= 0xFFFF
    s = (h >> 15) & 1
    e = (h >> 10) & 0x1F
    f = h & 0x3FF
    sign = -1.0 if s else 1.0
    if e == 0:
        if f == 0:
            return -0.0 if s else 0.0
        return sign * (f / 1024.0) * (2.0 ** -14)
    if e == 31:
        return float("nan") if f else (float("-inf") if s else float("inf"))
    return sign * (1.0 + f / 1024.0) * (2.0 ** (e - 15))


def sflp_to_quat(s0: int, s1: int, s2: int) -> tuple[float, float, float, float]:
    qx = f16_to_f32(s0)
    qy = f16_to_f32(s1)
    qz = f16_to_f32(s2)
    sumsq = qx * qx + qy * qy + qz * qz
    if sumsq > 1.0:
        sumsq = 1.0
    qw = math.sqrt(1.0 - sumsq)
    return qx, qy, qz, qw


def quat_to_euler(qx: float, qy: float, qz: float, qw: float) -> tuple[float, float, float]:
    # ZYX: yaw (Z), pitch (Y), roll (X), degrees
    sinr_cosp = 2.0 * (qw * qx + qy * qz)
    cosr_cosp = 1.0 - 2.0 * (qx * qx + qy * qy)
    roll = math.degrees(math.atan2(sinr_cosp, cosr_cosp))

    sinp = 2.0 * (qw * qy - qz * qx)
    sinp = max(-1.0, min(1.0, sinp))
    pitch = math.degrees(math.asin(sinp))

    siny_cosp = 2.0 * (qw * qz + qx * qy)
    cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz)
    yaw = math.degrees(math.atan2(siny_cosp, cosy_cosp))
    return roll, pitch, yaw


class DapSpi:
    def __init__(self, target):
        self.t = target
        self._cfgr_save = target.read32(GPIOA + CFGR)
        cfg = self._cfgr_save
        for pin, mode in ((4, 1), (5, 1), (6, 0), (7, 1)):
            cfg &= ~(0x3 << (pin * 2))
            cfg |= mode << (pin * 2)
        target.write32(GPIOA + CFGR, cfg)
        self.seth(4)
        self.seth(5)
        self.clrh(7)

    def restore(self):
        self.seth(4)
        self.t.write32(GPIOA + CFGR, self._cfgr_save)

    def seth(self, pin: int) -> None:
        self.t.write32(GPIOA + SCR, 1 << pin)

    def clrh(self, pin: int) -> None:
        self.t.write32(GPIOA + SCR, 1 << (pin + 16))

    def rd_miso(self) -> int:
        return (self.t.read32(GPIOA + IDT) >> 6) & 1

    def xfer_byte(self, tx: int) -> int:
        rx = 0
        for i in range(7, -1, -1):
            if tx & (1 << i):
                self.seth(7)
            else:
                self.clrh(7)
            self.clrh(5)
            self.seth(5)
            rx = (rx << 1) | self.rd_miso()
        return rx

    def write_reg(self, reg: int, val: int) -> None:
        self.clrh(4)
        self.xfer_byte(reg & 0x7F)
        self.xfer_byte(val & 0xFF)
        self.seth(4)

    def read_reg(self, reg: int) -> int:
        self.clrh(4)
        self.xfer_byte(reg | 0x80)
        val = self.xfer_byte(0x00)
        self.seth(4)
        return val

    def read_bytes(self, reg: int, n: int) -> list[int]:
        self.clrh(4)
        self.xfer_byte(reg | 0x80)
        data = [self.xfer_byte(0x00) for _ in range(n)]
        self.seth(4)
        return data

    def emb_enter(self) -> None:
        self.write_reg(REG_FUNC_CFG_ACCESS, 0x80)

    def emb_exit(self) -> None:
        self.write_reg(REG_FUNC_CFG_ACCESS, 0x00)


def fifo_level(spi: DapSpi) -> int:
    s1 = spi.read_reg(REG_FIFO_STATUS1)
    s2 = spi.read_reg(REG_FIFO_STATUS2)
    return s1 | ((s2 & 0x01) << 8)


def read_latest_sflp(spi: DapSpi):
    """Return the latest game quaternion and GBIAS from fresh FIFO samples.

    GBIAS payload is three little-endian int16 values in +/-125 dps format
    (4.375 mdps/LSB), matching ST's lsm6dsv_sensor_fusion example.
    """
    latest_game = None
    latest_gbias = None
    deadline = time.time() + 1.5
    while time.time() < deadline:
        n = fifo_level(spi)
        for _ in range(min(n, 64)):
            raw = spi.read_bytes(REG_FIFO_DATA_OUT_TAG, 7)
            tag = raw[0] >> 3
            if tag == TAG_SFLP_GAME:
                latest_game = (raw[1] | (raw[2] << 8),
                               raw[3] | (raw[4] << 8),
                               raw[5] | (raw[6] << 8))
            elif tag == TAG_SFLP_GBIAS:
                x, y, z = struct.unpack("<3h", bytes(raw[1:7]))
                latest_gbias = (x * 0.004375, y * 0.004375, z * 0.004375)
        if latest_game is not None:
            return latest_game, latest_gbias
        time.sleep(0.05)
    return latest_game, latest_gbias

def enable_sflp(spi: DapSpi) -> None:
    spi.write_reg(REG_CTRL3, 0x01)  # SW_RESET
    time.sleep(0.05)
    for _ in range(40):
        if (spi.read_reg(REG_CTRL3) & 0x01) == 0:
            break
        time.sleep(0.01)
    time.sleep(0.03)

    spi.write_reg(REG_IF_CFG, 0x01)  # disable I2C
    spi.write_reg(REG_CTRL3, 0x44)  # BDU + IF_INC
    spi.write_reg(REG_CTRL8, 0x00)  # +/-2 g
    spi.write_reg(REG_CTRL6, 0x03)  # +/-1000 dps

    # FIFO bypass then stream, only SFLP game rotation
    spi.write_reg(REG_FIFO_CTRL4, 0x00)

    spi.emb_enter()
    spi.write_reg(EMB_FUNC_EN_A, 0x02)       # SFLP_GAME_EN (GBIAS is FIFO-select only)
    spi.write_reg(EMB_FUNC_FIFO_EN_A, 0x22)  # SFLP_GAME_FIFO_EN(0x02) + SFLP_GBIAS_FIFO_EN(0x20)
    spi.write_reg(SFLP_ODR, 0x4B)            # 30 Hz, required bits
    spi.emb_exit()

    spi.write_reg(REG_CTRL1, 0x04)  # XL 30 Hz HP
    spi.write_reg(REG_CTRL2, 0x04)  # G  30 Hz HP
    spi.write_reg(REG_FIFO_CTRL4, 0x06)  # continuous

    time.sleep(1.5)  # gyro/SFLP settle


def main() -> None:
    print("connecting WCH CMSIS-DAP ...")
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "under-reset", "frequency": 400000, "resume_on_disconnect": False},
    )
    if session is None:
        raise SystemExit("no CMSIS-DAP probe")
    session.open()
    t = session.target
    t.reset()
    time.sleep(0.1)
    t.halt()
    spi = DapSpi(t)
    try:
        who = spi.read_reg(REG_WHO_AM_I)
        print(f"WHO_AM_I = 0x{who:02X}")
        if who != WHO_AM_I_VAL:
            raise SystemExit("chip id mismatch, abort")

        print("enable SFLP game rotation (30 Hz) ...")
        enable_sflp(spi)

        spi.emb_enter()
        en_a = spi.read_reg(EMB_FUNC_EN_A)
        fifo_en = spi.read_reg(EMB_FUNC_FIFO_EN_A)
        odr = spi.read_reg(SFLP_ODR)
        spi.emb_exit()
        print(f"EMB_FUNC_EN_A=0x{en_a:02X}  FIFO_EN_A=0x{fifo_en:02X}  SFLP_ODR=0x{odr:02X}")
        if (en_a & 0x02) == 0:
            raise SystemExit("SFLP_GAME_EN did not stick")

        rows = []
        print("recording 10 samples, interval 1 s (keep the board still or move slowly)\n")
        t0 = time.time()
        for i in range(1, 11):
            deadline = t0 + 1.0 * i
            remain = deadline - time.time()
            if remain > 0:
                time.sleep(remain)
            # Drop the accumulated batch and take a short fresh spot sample.
            # CMSIS-DAP bit-banged SPI is much slower than 30 Hz, so draining
            # a continuously growing FIFO would make timestamps drift.
            spi.write_reg(REG_FIFO_CTRL4, 0x00)
            spi.write_reg(REG_FIFO_CTRL4, 0x06)
            time.sleep(0.1)
            raw, gbias = read_latest_sflp(spi)
            ts = time.time() - t0
            if raw is None:
                rows.append((i, ts, None))
                print(f"[{i:02d}] t={ts:5.1f}s  no SFLP sample in FIFO")
                continue
            qx, qy, qz, qw = sflp_to_quat(*raw)
            roll, pitch, yaw = quat_to_euler(qx, qy, qz, qw)
            rows.append((i, ts, (qx, qy, qz, qw, roll, pitch, yaw)))
            print(
                f"[{i:02d}] t={ts:5.1f}s  "
                f"R={roll:7.2f}  P={pitch:7.2f}  Y={yaw:7.2f}  "
                f"q=({qx:+.4f}, {qy:+.4f}, {qz:+.4f}, {qw:+.4f})  "
                f"GB(dps)={gbias if gbias is not None else None}"
            )
    finally:
        spi.restore()
        try:
            t.reset()
            t.resume()
        except Exception:
            pass
        session.close()

    print("\n========== SFLP attitude (10 x 1s spot samples) ==========")
    print(f"{'#':>2}  {'t(s)':>6}  {'roll':>8}  {'pitch':>8}  {'yaw':>8}  {'qx':>8}  {'qy':>8}  {'qz':>8}  {'qw':>8}")
    print("-" * 82)
    for i, ts, att in rows:
        if att is None:
            print(f"{i:2d}  {ts:6.1f}  {'-- no data --':^64}")
        else:
            qx, qy, qz, qw, roll, pitch, yaw = att
            print(
                f"{i:2d}  {ts:6.1f}  {roll:8.2f}  {pitch:8.2f}  {yaw:8.2f}  "
                f"{qx:8.4f}  {qy:8.4f}  {qz:8.4f}  {qw:8.4f}"
            )
    print("==============================================")
    print("angles in degrees, quaternion is SFLP game rotation (qx qy qz qw)")


if __name__ == "__main__":
    main()









