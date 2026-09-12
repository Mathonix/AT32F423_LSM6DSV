from pyocd.core.helpers import ConnectHelper
GPIOA=0x40020000; CFGR=0; IDT=0x10; SCR=0x18
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000});s.open();t=s.target;t.halt();orig=t.read32(GPIOA)
def setmode(p,m):
 c=t.read32(GPIOA);c=(c&~(3<<(2*p)))|(m<<(2*p));t.write32(GPIOA,c)
for p,m in ((4,1),(5,1),(6,0),(7,1)):setmode(p,m)
def hi(p):t.write32(GPIOA+SCR,1<<p)
def lo(p):t.write32(GPIOA+SCR,1<<(p+16))
def bit(p):return (t.read32(GPIOA+IDT)>>p)&1
def send(v):
 for _ in range(8):(hi if v&128 else lo)(7);v=(v<<1)&255;lo(5);hi(5)
def rd3(reg):
 setmode(7,1);hi(4);hi(5);lo(7);lo(4);send(reg|128);setmode(7,0);v=0
 for _ in range(8):lo(5);hi(5);v=(v<<1)|bit(7)
 hi(4);setmode(7,1);return v
print([hex(rd3(0xf)) for _ in range(5)])
t.write32(GPIOA,orig);hi(4);t.reset();t.resume();s.close()
