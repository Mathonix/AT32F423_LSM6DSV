from pyocd.core.helpers import ConnectHelper
import time
GPIOA=0x40020000; CFGR=0; IDT=0x10; SCR=0x18
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000})
s.open();t=s.target;t.halt();old=t.read32(GPIOA)
def set_mode(p,m):
 c=t.read32(GPIOA);c=(c&~(3<<(p*2)))|(m<<(p*2));t.write32(GPIOA,c)
def low(p):
 t.write32(GPIOA+SCR,1<<(p+16));set_mode(p,1)
def release(p):set_mode(p,0)
def rd(p):return(t.read32(GPIOA+IDT)>>p)&1
def tick():pass
# CS high output
set_mode(4,1);t.write32(GPIOA+SCR,1<<4)
release(5);release(7)
def start():
 release(7);release(5);low(7);low(5)
def stop():
 low(7);release(5);release(7)
def wb(x):
 for i in range(8):
  low(5); (release if x&0x80 else low)(7); release(5); x=(x<<1)&255
 low(5);release(7);release(5);a=rd(7)==0;low(5);return a
def rb(ack):
 r=0;release(7)
 for _ in range(8):low(5);release(5);r=(r<<1)|rd(7)
 low(5);(low if ack else release)(7);release(5);low(5);release(7);return r
def readreg(addr,reg):
 start();a=wb(addr<<1);b=wb(reg);start();c=wb((addr<<1)|1);v=rb(False);stop();return a,b,c,v
def writereg(addr,reg,v):
 start();r=(wb(addr<<1),wb(reg),wb(v));stop();return r
print('idle scl/sda/sa0',rd(5),rd(7),rd(6))
for a in (0x6a,0x6b):print(hex(a),readreg(a,0x0f),readreg(a,0x03),readreg(a,0x12))
for a in (0x6a,0x6b):
 print('write reset defaults',hex(a),writereg(a,0x03,0),writereg(a,0x12,1))
time.sleep(.1)
for a in (0x6a,0x6b):print('after',hex(a),readreg(a,0x0f),readreg(a,0x03),readreg(a,0x12))
t.write32(GPIOA,old);t.write32(GPIOA+SCR,1<<4);t.reset();t.resume();s.close()
