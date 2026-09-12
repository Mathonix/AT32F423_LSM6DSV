from pyocd.core.helpers import ConnectHelper
GPIOA=0x40020000;CFGR=0;IDT=0x10;SCR=0x18
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000});s.open();t=s.target;t.halt();orig=t.read32(GPIOA)
c=orig
for p,m in ((4,1),(5,1),(6,0),(7,1)):c=(c&~(3<<(p*2)))|(m<<(p*2))
t.write32(GPIOA,c)
def h(p):t.write32(GPIOA+SCR,1<<p)
def l(p):t.write32(GPIOA+SCR,1<<(p+16))
def r():return(t.read32(GPIOA+IDT)>>6)&1
def rd(reg):
 h(4);l(5);l(7);l(4);v=0;tx=(reg|128)<<8
 for i in range(16):
  (h if tx&(1<<(15-i)) else l)(7);h(5);v=(v<<1)|r();l(5)
 h(4);return v&255
print([hex(rd(15)) for _ in range(8)])
t.write32(GPIOA,orig);h(4);t.reset();t.resume();s.close()
