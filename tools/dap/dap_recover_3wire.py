from pyocd.core.helpers import ConnectHelper
import time
GPIOA=0x40020000; CFGR=0; IDT=0x10; SCR=0x18
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000})
s.open();t=s.target;t.halt();old=t.read32(GPIOA)
def mode(cfg,p,m):return(cfg&~(3<<(p*2)))|(m<<(p*2))
def setmode(p,m):
 global cfg
 cfg=mode(t.read32(GPIOA),p,m);t.write32(GPIOA,cfg)
def hi(p):t.write32(GPIOA+SCR,1<<p)
def lo(p):t.write32(GPIOA+SCR,1<<(p+16))
def rd(p):return(t.read32(GPIOA+IDT)>>p)&1
# CS,SCK,SDA output; SDO input
cfg=old
for p,m in ((4,1),(5,1),(6,0),(7,1)):cfg=mode(cfg,p,m)
t.write32(GPIOA,cfg);hi(4);hi(5);lo(7)
def tx8(x):
 for _ in range(8):
  (hi if x&0x80 else lo)(7);x=(x<<1)&255;lo(5);hi(5)
def rx8_sda():
 r=0
 for _ in range(8):lo(5);hi(5);r=(r<<1)|rd(7)
 return r
def rd3(a):
 setmode(7,1);hi(4);hi(5);lo(7);lo(4);tx8(a|0x80);setmode(7,0);v=rx8_sda();hi(4);return v
def wr3(a,v):
 setmode(7,1);hi(4);hi(5);lo(7);lo(4);tx8(a&0x7f);tx8(v);hi(4)
print('3wire before',*[hex(rd3(a)) for a in (0x0f,0x03,0x12)])
wr3(0x03,0x01);time.sleep(.01)
# restore pin7 output before trying 4wire later
print('wrote IF_CFG=1')
# Try 3wire again (should stop if now 4-wire)
print('3wire after',*[hex(rd3(a)) for a in (0x0f,0x03)])
t.write32(GPIOA,old);hi(4);t.reset();t.resume();s.close()
