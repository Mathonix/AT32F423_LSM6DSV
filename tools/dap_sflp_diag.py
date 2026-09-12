"""Diagnose LSM6DSV SFLP FIFO output over CMSIS-DAP bit-banged SPI.

Connects under reset, releases the MCU reset, halts the core, then configures the
sensor directly. This avoids attaching to the firmware's already-active SPI state.
"""
from __future__ import annotations

import time
from pyocd.core.helpers import ConnectHelper

from dap_sflp_attitude import (
    DapSpi, enable_sflp, fifo_level, REG_FIFO_CTRL4, REG_FIFO_DATA_OUT_TAG,
    REG_WHO_AM_I, REG_CTRL1, REG_CTRL2, REG_CTRL3, WHO_AM_I_VAL,
)


def main() -> None:
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={
            "connect_mode": "under-reset",
            "frequency": 100000,
            "resume_on_disconnect": False,
        },
    )
    if session is None:
        raise SystemExit("no CMSIS-DAP probe")
    session.open()
    target = session.target
    # Release reset and immediately halt after the core has left reset.
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
        print(
            "CTRL1=0x%02X CTRL2=0x%02X CTRL3=0x%02X FIFO_CTRL4=0x%02X"
            % (
                spi.read_reg(REG_CTRL1),
                spi.read_reg(REG_CTRL2),
                spi.read_reg(REG_CTRL3),
                spi.read_reg(REG_FIFO_CTRL4),
            ),
            flush=True,
        )
        for i in range(12):
            time.sleep(0.5)
            count = fifo_level(spi)
            print(f"t={(i + 1) * 0.5:4.1f}s level={count}", flush=True)
            if count:
                raw = spi.read_bytes(REG_FIFO_DATA_OUT_TAG, 7)
                print(f"  tag=0x{raw[0] >> 3:02X} raw={bytes(raw).hex()}", flush=True)
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
