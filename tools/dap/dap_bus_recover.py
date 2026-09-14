from pyocd.core.helpers import ConnectHelper
import time
GPIOA=0x40020000; IDT=0x10; SCR=0x18
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000})
s.open();t=s.target;t.halt();old=t.read32(GPIOA)
def mode(p,m):
 c=t.read32(GPIOA);c=(c&~(3<<(2*p)))|(m<<(2*p));t.write32(GPIOA,c)
def hi(p):t.write32(GPIOA+SCR,1<<p)
def lo(p):t.write32(GPIOA+SCR,1<<(p+16))
def inp(p):mode(p,0)
def out(p,v):
 (hi if v else lo)(p);mode(p,1)
def rd(p):return(t.read32(GPIOA+IDT)>>p)&1
out(4,1);inp(7);out(5,1)
print('start',rd(4),rd(5),rd(7),rd(6))
for i in range(32):
 out(5,0);out(5,1)
 if i%8==7:print(i+1,rd(7))
# I2C stop: SDA low, SCL high, release SDA
out(7,0);out(5,1);inp(7)
print('stop',rd(4),rd(5),rd(7),rd(6))
t.write32(GPIOA,old);hi(4);t.reset();t.resume();s.close()
