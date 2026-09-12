from pyocd.core.helpers import ConnectHelper
import time
GPIOA=0x40020000; CFGR=0; OM=4; PULL=0x0c; IDT=0x10; ODT=0x14; SCR=0x18
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m', options={'connect_mode':'halt','frequency':400000});s.open();t=s.target;t.halt()
orig=[t.read32(GPIOA+x) for x in (CFGR,OM,PULL)]
c=orig[0]
for p,m in ((4,1),(5,1),(7,1)): c=(c&~(3<<(2*p)))|(m<<(2*p))
t.write32(GPIOA+CFGR,c)
# PA5/7 open drain, PA4 push pull
om=orig[1] | (1<<5)|(1<<7); om &= ~(1<<4);t.write32(GPIOA+OM,om)
p=orig[2]
for pin in (5,7): p=(p&~(3<<(2*pin)))|(1<<(2*pin))
t.write32(GPIOA+PULL,p)
def hi(pin):t.write32(GPIOA+SCR,1<<pin)
def lo(pin):t.write32(GPIOA+SCR,1<<(pin+16))
def rd(pin):return(t.read32(GPIOA+IDT)>>pin)&1
def d():time.sleep(0.0002)
def start():hi(7);hi(5);d();lo(7);d();lo(5);d()
def stop():lo(7);d();hi(5);d();hi(7);d()
def wr(v):
 for _ in range(8):
  (hi if v&128 else lo)(7);d();hi(5);d();lo(5);d();v=(v<<1)&255
 hi(7);d();hi(5);d();ack=not rd(7);lo(5);d();return ack
def read(ack):
 hi(7);v=0
 for _ in range(8):hi(5);d();v=(v<<1)|rd(7);lo(5);d()
 (lo if ack else hi)(7);d();hi(5);d();lo(5);hi(7);d();return v
def regread(addr,reg):
 start();a1=wr(addr<<1);a2=wr(reg);start();a3=wr((addr<<1)|1);v=read(False);stop();return a1,a2,a3,v
hi(4);hi(5);hi(7);d()
print('levels',hex(t.read32(GPIOA+IDT)))
for a in (0x6a,0x6b):
 try: print(hex(a),regread(a,0x0f))
 except Exception as e:print(e)
# restore
t.write32(GPIOA+CFGR,orig[0]);t.write32(GPIOA+OM,orig[1]);t.write32(GPIOA+PULL,orig[2]);hi(4);t.reset();t.resume();s.close()
