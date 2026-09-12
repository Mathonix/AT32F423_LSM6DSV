"""SPI1 peripheral isolation diagnostic (no reflash needed).

Halts the running firmware and drives SPI1 / GPIOA directly through DAP so the
peripheral is tested independently of the sensor and of the firmware build.

Tests
  A  bit-bang reference (same edges as dap_whoami.py)  -> proves sensor + wiring
  B  SPI1 peripheral, 8-bit DT write + word poll       -> the firmware's path
  C  SPI1 peripheral, 16-bit DT read                   -> rules out DT access width
  D  SPI1 internal loopback (slbsel/slbtd/ora)         -> proves shift register+baud
  E  MISO forced low by GPIO                           -> proves SPI MISO input path

Usage:  python tools/dap_spi_diag.py
"""

from pyocd.core.helpers import ConnectHelper

GPIOA = 0x40020000
GPIOA_CFGR = 0x00
GPIOA_IDT = 0x10
GPIOA_SCR = 0x18
GPIOA_MUXL = 0x20

SPI1 = 0x40013000
SPI_CTRL1 = 0x00
SPI_CTRL2 = 0x04
SPI_STS = 0x08
SPI_DT = 0x0C
SPI_I2SCTL = 0x1C
SPI_I2SCLKP = 0x20

CRM = 0x40023800
CRM_APB2EN = 0x44

# CTRL1 bits
CLKPHA, CLKPOL, MSTEN, SPIEN, LTF = 1 << 0, 1 << 1, 1 << 2, 1 << 6, 1 << 7
SWCSIL, SWCSEN, ORA, FBN = 1 << 8, 1 << 9, 1 << 10, 1 << 11
SLBTD, SLBEN = 1 << 14, 1 << 15

WHO_AM_I = 0x0F
WHO_VAL = 0x70


def bits(v):
    return " ".join(
        "%s=%d" % (n, (v >> b) & 1)
        for n, b in (
            ("clkpha", 0), ("clkpol", 1), ("msten", 2), ("mdiv", 5),
            ("spien", 6), ("ltf", 7), ("swcsil", 8), ("swcsen", 9),
            ("ora", 10), ("fbn", 11), ("slbtd", 14), ("slben", 15),
        )
    )


class Bench(object):
    def __init__(self, t):
        self.t = t
        self.save = {}

    def stash(self, addr):
        if addr not in self.save:
            self.save[addr] = self.t.read32(addr)

    def w(self, addr, val):
        self.stash(addr)
        self.t.write32(addr, val)

    def restore(self):
        for addr, val in self.save.items():
            self.t.write32(addr, val)
        self.save = {}

    # ---- GPIO helpers (SCR is BSRR: bit n sets, bit 16+n resets) ----
    def pin_set(self, p):
        self.w(GPIOA_SCR, 1 << p)

    def pin_clr(self, p):
        self.w(GPIOA_SCR, 1 << (p + 16))

    def pin_in(self, p):
        return (self.t.read32(GPIOA_IDT) >> p) & 1

    def gpio_mode(self, p, mode):
        cfgr = self.t.read32(GPIOA_CFGR)
        self.w(GPIOA_CFGR, (cfgr & ~(0x3 << (p * 2))) | (mode << (p * 2)))

    def mux(self, p, af):
        muxl = self.t.read32(GPIOA_MUXL)
        self.w(GPIOA_MUXL, (muxl & ~(0xF << (p * 4))) | (af << (p * 4)))

    def spi_off(self):
        self.w(SPI1 + SPI_CTRL1, 0)
        self.w(SPI1 + SPI_CTRL2, 0)

    # ---- Test A: bit-bang reference -------------------------------------
    def bitbang_who(self):
        for p, m in ((4, 1), (5, 1), (6, 0), (7, 1)):
            self.gpio_mode(p, m)
        self.pin_set(4)
        self.pin_set(5)
        self.pin_clr(7)
        self.pin_clr(4)
        tx = (WHO_AM_I | 0x80) << 8
        miso = 0
        for i in range(16):
            if tx & (1 << (15 - i)):
                self.pin_set(7)
            else:
                self.pin_clr(7)
            self.pin_clr(5)
            self.pin_set(5)
            miso = (miso << 1) | self.pin_in(6)
        self.pin_set(4)
        return miso & 0xFF

    # ---- SPI pin / peripheral setup -------------------------------------
    def spi_pins(self, cs_gpio=True):
        self.w(CRM + CRM_APB2EN, self.t.read32(CRM + CRM_APB2EN) | (1 << 12))
        if cs_gpio:
            self.gpio_mode(4, 1)
            self.pin_set(4)
        for p in (5, 6, 7):
            self.gpio_mode(p, 2)      # MUX
            self.mux(p, 5)            # AF5
        self.w(SPI1 + SPI_I2SCTL, 0)
        self.w(SPI1 + SPI_I2SCLKP, 2)

    def ctrl1(self, mdiv=0x04, extra=0, mode3=True):
        v = MSTEN | SPIEN | SWCSEN | SWCSIL
        if mode3:
            v |= CLKPOL | CLKPHA
        return v | (mdiv << 3) | extra

    def xfer(self, tx, fbn16=False):
        """Full-duplex one frame. Returns (rx, status_before, status_after)."""
        t = self.t
        g = 1000
        while not (t.read32(SPI1 + SPI_STS) & 0x2) and g:
            g -= 1
        st_before = t.read32(SPI1 + SPI_STS)
        t.write32(SPI1 + SPI_DT, tx)
        g = 1000
        while not (t.read32(SPI1 + SPI_STS) & 0x1) and g:
            g -= 1
        rx = t.read32(SPI1 + SPI_DT)
        st_after = t.read32(SPI1 + SPI_STS)
        if fbn16:
            return rx & 0xFFFF, st_before, st_after
        return rx & 0xFF, st_before, st_after

    def hw_read_who(self, fbn16=False, mdiv=0x04):
        self.spi_off()
        self.spi_pins()
        self.w(SPI1 + SPI_CTRL1, self.ctrl1(mdiv=mdiv, extra=FBN if fbn16 else 0))
        self.pin_clr(4)
        a, _, _ = self.xfer((WHO_AM_I | 0x80) << (8 if fbn16 else 0), fbn16)
        b, s1, s2 = self.xfer(0, fbn16)
        self.pin_set(4)
        return (a, b, s1, s2)


def main():
    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m",
        options={"connect_mode": "halt", "frequency": 400000},
    )
    session.open()
    t = session.target
    t.halt()
    b = Bench(t)

    print("=== live register state (from running firmware) ===")
    print("APB2EN     = 0x%08X  (SPI1EN bit12=%d)" % (
        t.read32(CRM + CRM_APB2EN), (t.read32(CRM + CRM_APB2EN) >> 12) & 1))
    c1 = t.read32(SPI1 + SPI_CTRL1)
    print("SPI1 CTRL1 = 0x%08X  %s" % (c1, bits(c1)))
    print("SPI1 CTRL2 = 0x%08X" % t.read32(SPI1 + SPI_CTRL2))
    print("SPI1 STS   = 0x%08X" % t.read32(SPI1 + SPI_STS))
    print("SPI1 I2SCTL= 0x%08X   I2SCLKP=0x%08X" % (
        t.read32(SPI1 + SPI_I2SCTL), t.read32(SPI1 + SPI_I2SCLKP)))
    print("GPIOA CFGR = 0x%08X  MUXL=0x%08X  IDT=0x%04X" % (
        t.read32(GPIOA_CFGR), t.read32(GPIOA_MUXL), t.read32(GPIOA_IDT) & 0xFFFF))

    print()
    print("=== A: bit-bang reference (sensor sanity) ===")
    b.spi_off()
    vals = [b.bitbang_who() for _ in range(3)]
    print("WHO = %s  -> %s" % (
        " ".join(hex(v) for v in vals),
        "SENSOR OK" if all(v == WHO_VAL for v in vals) else "SENSOR/WIRING PROBLEM"))

    print()
    print("=== B: SPI1 peripheral, 8-bit DT (firmware path) ===")
    for mdiv, name in ((0x04, "PCLK/32"), (0x05, "PCLK/64"), (0x07, "PCLK/256")):
        a, r, s1, s2 = b.hw_read_who(fbn16=False, mdiv=mdiv)
        print("  Mode3 %-9s addr_rx=0x%02X data_rx=0x%02X sts=%02X/%02X" % (name, a, r, s1, s2))

    print()
    print("=== C: SPI1 peripheral, 16-bit DT ===")
    a, r, s1, s2 = b.hw_read_who(fbn16=True, mdiv=0x07)
    print("  Mode3 word    addr_rx=0x%04X data_rx=0x%04X sts=%02X/%02X" % (a, r, s1, s2))

    print()
    print("=== D: SPI1 internal loopback (proves shift register + baud) ===")
    b.spi_off()
    b.spi_pins()
    b.w(SPI1 + SPI_CTRL1, b.ctrl1(mdiv=0x07, extra=SLBEN | SLBTD | ORA))
    for pat in (0xA5, 0x5A, 0x3C):
        rx, _, _ = b.xfer(pat)
        print("  tx=0x%02X rx=0x%02X %s" % (pat, rx, "OK" if rx == pat else "MISMATCH"))

    print()
    print("=== E: MISO forced low by GPIO during SPI read ===")
    b.spi_off()
    b.spi_pins()
    b.w(SPI1 + SPI_CTRL1, b.ctrl1(mdiv=0x07))
    # PA6 becomes plain push-pull output driven low
    b.gpio_mode(6, 1)
    b.pin_clr(6)
    b.pin_set(4)
    b.pin_clr(4)
    b.xfer((WHO_AM_I | 0x80))
    r, _, _ = b.xfer(0x00)
    b.pin_set(4)
    print("  PA6 driven low -> SPI1 rx=0x%02X  %s" % (
        r, "MISO PATH OK (reads 0)" if r == 0x00 else "MISO INPUT PATH BROKEN"))

    b.restore()
    print()
    print("state restored; resuming MCU")
    t.resume()
    session.close()


if __name__ == "__main__":
    main()


