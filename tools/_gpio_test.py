from pyocd.core.helpers import ConnectHelper
GPIOA=0x40020000; CFGR=0; IDT=0x10; ODT=0x14; SCR=0x18; MUXL=0x20
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000}); s.open();t=s.target;t.halt()
cfg=t.read32(GPIOA); orig=cfg
for p,m in ((4,1),(5,1),(6,0),(7,1)): cfg=(cfg&~(3<<(2*p)))|(m<<(2*p))
t.write32(GPIOA,cfg)
def state(tag):print(tag,hex(t.read32(GPIOA+IDT)),hex(t.read32(GPIOA+ODT)),hex(t.read32(GPIOA+CFGR)),hex(t.read32(GPIOA+MUXL)))
def hi(p):t.write32(GPIOA+SCR,1<<p)
def lo(p):t.write32(GPIOA+SCR,1<<(p+16))
state('start')
for p in (4,5,7):
 lo(p);state(f'p{p} lo');hi(p);state(f'p{p} hi')
t.write32(GPIOA,orig);hi(4);t.reset();t.resume();s.close()
