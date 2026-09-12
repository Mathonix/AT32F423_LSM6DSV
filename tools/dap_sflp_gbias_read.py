"""Verify LSM6DSV SFLP gyroscope-bias output against raw gyro data.

FIFO tag 0x16 contains three little-endian signed 16-bit values in the
+/-125 dps format (4.375 mdps/LSB), as handled by ST's
lsm6dsv_sensor_fusion.c example. This is not an IEEE half-float payload.
"""
from __future__ import annotations

import struct
import time
from pyocd.core.helpers import ConnectHelper

from dap_sflp_attitude import (
    DapSpi, enable_sflp, fifo_level,
    REG_FIFO_DATA_OUT_TAG, REG_WHO_AM_I, WHO_AM_I_VAL,
)

GBIAS_X_L = 0x6E
REG_OUTX_L_G = 0x22
REG_STATUS = 0x1E
TAG_GBIAS = 0x16
GYRO_DPS_PER_LSB = 0.070
GBIAS_DPS_PER_LSB = 0.004375


def main() -> None:
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={
            "connect_mode": "under-reset",
            "frequency": 400000,
            "resume_on_disconnect": False,
        },
    )
    if session is None:
        raise SystemExit("no CMSIS-DAP probe")
    session.open()
    target = session.target
    target.reset()
    time.sleep(0.1)
    target.halt()
    spi = DapSpi(target)
    try:
        who = spi.read_reg(REG_WHO_AM_I)
        print(f"WHO_AM_I=0x{who:02X}", flush=True)
        if who != WHO_AM_I_VAL:
            raise SystemExit("chip id mismatch")

        enable_sflp(spi)
        time.sleep(2.0)

        gyro_sum = [0.0, 0.0, 0.0]
        gyro_n = 0
        for _ in range(500):
            if (spi.read_reg(REG_STATUS) & 0x02) == 0:
                time.sleep(0.002)
                continue
            vals = struct.unpack("<3h", bytes(spi.read_bytes(REG_OUTX_L_G, 6)))
            for i, value in enumerate(vals):
                gyro_sum[i] += value * GYRO_DPS_PER_LSB
            gyro_n += 1
            if gyro_n >= 200:
                break
        if gyro_n:
            avg = [value / gyro_n for value in gyro_sum]
            print(
                f"raw gyro avg ({gyro_n} samples) dps = "
                f"({avg[0]:+.6f}, {avg[1]:+.6f}, {avg[2]:+.6f})",
                flush=True,
            )
        else:
            print("raw gyro: no samples", flush=True)

        spi.emb_enter()
        direct = bytes(spi.read_bytes(GBIAS_X_L, 6))
        spi.emb_exit()
        print(f"direct GBIAS registers raw={direct.hex()}", flush=True)

        seen = 0
        for _ in range(300):
            count = fifo_level(spi)
            if count == 0:
                time.sleep(0.02)
                continue
            raw = spi.read_bytes(REG_FIFO_DATA_OUT_TAG, 7)
            if (raw[0] >> 3) != TAG_GBIAS:
                continue
            values = struct.unpack("<3h", bytes(raw[1:7]))
            dps = [value * GBIAS_DPS_PER_LSB for value in values]
            print(f"fifo raw={bytes(raw).hex()} axis={values}", flush=True)
            print("  GBIAS dps = " + ", ".join(f"{x:+.6f}" for x in dps), flush=True)
            seen += 1
            if seen >= 8:
                break
        if seen == 0:
            print("no GBIAS FIFO samples", flush=True)
    finally:
        spi.restore()
        try:
            target.reset()
            target.resume()
        except Exception:
            pass
        session.close()


if __name__ == "__main__":
    main()
