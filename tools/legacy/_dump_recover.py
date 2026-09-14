from pyocd.core.helpers import ConnectHelper
import time
GPIOA=0x40020000; CFGR=0; IDT=0x10; SCR=0x18; MUXL=0x20
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000}); s.open(); t=s.target; t.halt()
orig=(t.read32(GPIOA+CFGR),t.read32(GPIOA+MUXL))
def mode(cfg,p,m): return (cfg & ~(3<<(p*2))) | (m<<(p*2))
cfg=orig[0]
for p,m in ((4,1),(5,1),(6,0),(7,1)): cfg=mode(cfg,p,m)
t.write32(GPIOA+CFGR,cfg)
def hi(p): t.write32(GPIOA+SCR,1<<p)
def lo(p): t.write32(GPIOA+SCR,1<<(p+16))
def rd(p): return (t.read32(GPIOA+IDT)>>p)&1
def txbit(v):
  for i in range(8):
    (hi if v&0x80 else lo)(7); v=(v<<1)&255; lo(5); hi(5)
def wr(reg,val):
  hi(4);hi(5);lo(7);lo(4);txbit(reg&0x7f);txbit(val);hi(4)
def rdreg(reg):
  hi(4);hi(5);lo(7);lo(4);txbit(reg|0x80); v=0
  for _ in range(8): lo(7);lo(5);hi(5);v=(v<<1)|rd(6)
  hi(4);return v
print('before dump')
for base in range(0, 0x70, 16):
  vals=[rdreg(base+i) for i in range(16)]
  print(f'{base:02X}: '+' '.join(f'{v:02X}' for v in vals))
wr(0x01,0x00); wr(0x03,0x00)
print('after bank/ifcfg',hex(rdreg(0xf)), 'ifcfg',hex(rdreg(3)))
# reset then wait using DAP host
wr(0x12,0x01); time.sleep(.05); wr(0x01,0x00); wr(0x03,0x00)
print('after reset',hex(rdreg(0xf)), 'ctrl3',hex(rdreg(0x12)), 'ifcfg',hex(rdreg(3)))
t.write32(GPIOA+CFGR,orig[0]); t.write32(GPIOA+MUXL,orig[1]); hi(4); t.reset();t.resume();s.close()

