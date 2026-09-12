from pyocd.core.helpers import ConnectHelper
import time
GPIOA=0x40020000; CFGR=0; IDT=0x10; SCR=0x18
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000})
s.open(); t=s.target; t.halt(); old=t.read32(GPIOA)
def mode(cfg,p,m): return (cfg & ~(3<<(p*2))) | (m<<(p*2))
cfg=old
for p,m in ((4,1),(5,1),(6,0),(7,1)): cfg=mode(cfg,p,m)
t.write32(GPIOA,cfg)
def hi(p):t.write32(GPIOA+SCR,1<<p)
def lo(p):t.write32(GPIOA+SCR,1<<(p+16))
def bit():return (t.read32(GPIOA+IDT)>>6)&1
def xfer(x):
 r=0
 for i in range(8):
  (hi if x&0x80 else lo)(7); x=(x<<1)&255; lo(5); hi(5); r=(r<<1)|bit()
 return r
def rd(a):
 hi(4);hi(5);lo(7);lo(4);xfer(a|0x80);v=xfer(0);hi(4); return v
def wr(a,v):
 hi(4);hi(5);lo(7);lo(4);xfer(a&0x7f);xfer(v);hi(4)
print('before',*[hex(rd(a)) for a in (0x0f,0x03,0x12)])
wr(0x03,0x00); time.sleep(.01)
print('after ifcfg0',*[hex(rd(a)) for a in (0x0f,0x03,0x12)])
wr(0x12,0x01); time.sleep(.1)
print('after reset',*[hex(rd(a)) for a in (0x0f,0x03,0x12)])
t.write32(GPIOA,old);hi(4);t.reset();t.resume();s.close()
