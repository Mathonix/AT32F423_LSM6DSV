from pyocd.core.helpers import ConnectHelper

GPIOA = 0x40020000
CFGR, IDT, SCR = 0x00, 0x10, 0x18

session = ConnectHelper.session_with_chosen_probe(
    target_override="cortex_m",
    options={"connect_mode": "halt", "frequency": 1000000},
)
session.open()
t = session.target
t.halt()
cfgr = t.read32(GPIOA + CFGR)


def set_mode(cfg, pin, mode):
    cfg &= ~(0x3 << (pin * 2))
    cfg |= mode << (pin * 2)
    return cfg


cfg = cfgr
for p, m in ((4, 1), (5, 1), (6, 0), (7, 1)):
    cfg = set_mode(cfg, p, m)
t.write32(GPIOA + CFGR, cfg)


def seth(p):
    t.write32(GPIOA + SCR, 1 << p)


def clrh(p):
    t.write32(GPIOA + SCR, 1 << (p + 16))


def rd6():
    return (t.read32(GPIOA + IDT) >> 6) & 1


def read_who():
    seth(4)
    seth(5)
    clrh(7)
    clrh(4)
    tx = ((0x0F | 0x80) << 8) | 0x00
    miso = 0
    for i in range(16):
        bit = 1 if (tx & (1 << (15 - i))) else 0
        (seth if bit else clrh)(7)
        clrh(5)
        seth(5)
        miso = (miso << 1) | rd6()
    seth(4)
    return miso & 0xFF


vals = [read_who() for _ in range(3)]
print("WHO_AM_I", " ".join(hex(v) for v in vals))
ok = all(v == 0x70 for v in vals)
print("RESULT", "OK" if ok else "FAIL")

t.write32(GPIOA + CFGR, cfgr)
t.write32(GPIOA + SCR, 1 << 4)
t.reset()
t.resume()
session.close()
print("mcu resumed")
