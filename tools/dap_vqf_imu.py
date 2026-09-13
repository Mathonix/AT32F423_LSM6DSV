"""Configure LSM6DSV via WCH CMSIS-DAP GPIO SPI, run BasicVQF on the host."""
from __future__ import annotations

import math
import time

from pyocd.core.helpers import ConnectHelper

GPIOA = 0x40020000
CFGR, IDT, SCR = 0x00, 0x10, 0x18

REG_CTRL1, REG_CTRL2, REG_CTRL3 = 0x10, 0x11, 0x12
REG_CTRL6, REG_CTRL8 = 0x15, 0x17
REG_WHO, REG_STATUS, REG_OUTX_L_G = 0x0F, 0x1E, 0x22
REG_HAODR = 0x62
REG_INT1 = 0x0D

WHO_VAL = 0x70
G_DPS = 0.035
A_G = 0.000122
DEG2RAD = math.pi / 180.0
G0 = 9.80665
DT = 0.05


class BasicVQF:
    def __init__(self, dt: float, tau_acc: float = 3.0):
        self.dt = dt
        self.gq = [1.0, 0.0, 0.0, 0.0]
        self.aq = [1.0, 0.0, 0.0, 0.0]
        self.acc_lp = [0.0, 0.0, 0.0]
        self.state = [0.0] * 6
        self.inited = False
        fc = (math.sqrt(2.0) / 2.0) / (math.pi * tau_acc)
        C = math.tan(math.pi * fc * dt)
        D = C * C + math.sqrt(2.0) * C + 1.0
        b0 = (C * C) / D
        self.b = [b0, 2 * b0, b0]
        self.a = [2 * (C * C - 1.0) / D, (1.0 - math.sqrt(2.0) * C + C * C) / D]

    @staticmethod
    def _mul(q1, q2):
        w, x, y, z = q1
        W, X, Y, Z = q2
        return [
            w * W - x * X - y * Y - z * Z,
            w * X + x * W + y * Z - z * Y,
            w * Y - x * Z + y * W + z * X,
            w * Z + x * Y - y * X + z * W,
        ]

    @staticmethod
    def _rot(q, v):
        w, x, y, z = q
        tx = 2 * (y * v[2] - z * v[1])
        ty = 2 * (z * v[0] - x * v[2])
        tz = 2 * (x * v[1] - y * v[0])
        return [
            v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx),
        ]

    def update(self, gyr, acc):
        n = math.sqrt(gyr[0] ** 2 + gyr[1] ** 2 + gyr[2] ** 2)
        if n > 1e-8:
            ang = n * self.dt
            c = math.cos(0.5 * ang)
            s = math.sin(0.5 * ang) / n
            self.gq = self._mul(self.gq, [c, s * gyr[0], s * gyr[1], s * gyr[2]])
            nn = math.sqrt(sum(t * t for t in self.gq)) or 1.0
            self.gq = [t / nn for t in self.gq]

        if acc[0] == acc[1] == acc[2] == 0:
            return
        ae = self._rot(self.gq, acc)
        if not self.inited:
            self.acc_lp = list(ae)
            for i in range(3):
                self.state[2 * i] = ae[i] * (1.0 - self.b[0])
                self.state[2 * i + 1] = ae[i] * (self.b[2] - self.a[1])
            self.inited = True
        else:
            for i in range(3):
                x = ae[i]
                y = self.b[0] * x + self.state[2 * i]
                self.state[2 * i] = self.b[1] * x - self.a[0] * y + self.state[2 * i + 1]
                self.state[2 * i + 1] = self.b[2] * x - self.a[1] * y
                self.acc_lp[i] = y
        n = math.sqrt(sum(t * t for t in self.acc_lp))
        if n < 1e-6:
            return
        ae = [t / n for t in self.acc_lp]
        qw = math.sqrt(max((ae[2] + 1.0) * 0.5, 0.0))
        if qw > 1e-6:
            self.aq = [qw, ae[1] / (2 * qw), -ae[0] / (2 * qw), 0.0]
        else:
            self.aq = [0.0, 1.0, 0.0, 0.0]

    def quat(self):
        return self._mul(self.aq, self.gq)

    def euler_deg(self):
        w, x, y, z = self.quat()
        sinp = 2 * (w * y - z * x)
        sinp = max(-1.0, min(1.0, sinp))
        roll = math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y))
        pitch = math.asin(sinp)
        yaw = math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z))
        r2d = 180.0 / math.pi
        return roll * r2d, pitch * r2d, yaw * r2d


def set_mode(cfg, pin, mode):
    cfg &= ~(0x3 << (pin * 2))
    cfg |= mode << (pin * 2)
    return cfg


def main() -> int:
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "halt", "frequency": 1000000},
    )
    if session is None:
        raise RuntimeError("no CMSIS-DAP")
    session.open()
    t = session.target
    t.halt()
    cfgr0 = t.read32(GPIOA + CFGR)
    cfg = cfgr0
    for p, m in ((4, 1), (5, 1), (6, 0), (7, 1)):
        cfg = set_mode(cfg, p, m)
    t.write32(GPIOA + CFGR, cfg)

    def seth(p):
        t.write32(GPIOA + SCR, 1 << p)

    def clrh(p):
        t.write32(GPIOA + SCR, 1 << (p + 16))

    def rd6():
        return (t.read32(GPIOA + IDT) >> 6) & 1

    def xfer(byte: int) -> int:
        miso = 0
        for i in range(8):
            (seth if (byte & 0x80) else clrh)(7)
            byte = (byte << 1) & 0xFF
            clrh(5)
            seth(5)
            miso = (miso << 1) | rd6()
        return miso

    def read_reg(reg: int) -> int:
        seth(4)
        seth(5)
        clrh(4)
        xfer(reg | 0x80)
        v = xfer(0)
        seth(4)
        return v

    def write_reg(reg: int, val: int) -> None:
        seth(4)
        seth(5)
        clrh(4)
        xfer(reg & 0x7F)
        xfer(val)
        seth(4)

    def read_burst(reg: int, n: int) -> bytes:
        seth(4)
        seth(5)
        clrh(4)
        xfer(reg | 0x80)
        data = bytes(xfer(0) for _ in range(n))
        seth(4)
        return data

    try:
        who = read_reg(REG_WHO)
        print(f"WHO_AM_I=0x{who:02X}")
        if who != WHO_VAL:
            print("RESULT: IMU SPI fail via DAP")
            return 2

        write_reg(REG_CTRL3, 0x01)
        for _ in range(20):
            if (read_reg(REG_CTRL3) & 1) == 0:
                break
            time.sleep(0.01)
        write_reg(REG_CTRL3, 0x44)
        write_reg(REG_CTRL6, 0x03)
        write_reg(REG_CTRL8, 0x01)
        write_reg(REG_HAODR, 0x01)
        write_reg(REG_INT1, 0x02)
        write_reg(REG_CTRL1, 0x1A)
        write_reg(REG_CTRL2, 0x1A)
        print("IMU HA01 2000 Hz configured (host samples ~20 Hz via DAP)")

        vqf = BasicVQF(DT)
        print(f"{'#':>3} {'R':>8} {'P':>8} {'Y':>8}  qw qx qy qz")
        for i in range(12):
            raw = read_burst(REG_OUTX_L_G, 12)
            gx = int.from_bytes(raw[0:2], "little", signed=True) * G_DPS * DEG2RAD
            gy = int.from_bytes(raw[2:4], "little", signed=True) * G_DPS * DEG2RAD
            gz = int.from_bytes(raw[4:6], "little", signed=True) * G_DPS * DEG2RAD
            ax = int.from_bytes(raw[6:8], "little", signed=True) * A_G * G0
            ay = int.from_bytes(raw[8:10], "little", signed=True) * A_G * G0
            az = int.from_bytes(raw[10:12], "little", signed=True) * A_G * G0
            vqf.update((gx, gy, gz), (ax, ay, az))
            r, p, y = vqf.euler_deg()
            q = vqf.quat()
            print(
                f"{i+1:3d} {r:8.2f} {p:8.2f} {y:8.2f}  "
                f"{q[0]:+.3f} {q[1]:+.3f} {q[2]:+.3f} {q[3]:+.3f}  "
                f"g=({gx/DEG2RAD:+6.1f},{gy/DEG2RAD:+6.1f},{gz/DEG2RAD:+6.1f}) "
                f"a=({ax/G0:+5.2f},{ay/G0:+5.2f},{az/G0:+5.2f})"
            )
        print("RESULT: DAP VQF samples OK")
        return 0
    finally:
        t.write32(GPIOA + CFGR, cfgr0)
        t.write32(GPIOA + SCR, 1 << 4)
        try:
            t.reset()
            t.resume()
        except Exception:
            pass
        session.close()


if __name__ == "__main__":
    raise SystemExit(main())
