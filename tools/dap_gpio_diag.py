from pyocd.core.helpers import ConnectHelper
GPIOA=0x40020000
REG={'CFGR':0,'OMODE':4,'ODRVR':8,'PULL':12,'IDT':16,'ODT':20,'SCR':24,'MUXL':32,'CLR':40}
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'halt','frequency':400000})
s.open();t=s.target;t.halt()
print('pc',hex(t.read_core_register('pc')))
for n,o in REG.items():print(n,hex(t.read32(GPIOA+o)))
print('SPI',*[hex(t.read32(0x40013000+o)) for o in (0,4,8,12,28,32)])
# force known GPIO state and inspect levels
cf=t.read32(GPIOA)
for p,m in ((4,1),(5,1),(6,0),(7,1)):
 cf=(cf&~(3<<(p*2)))|(m<<(p*2))
t.write32(GPIOA,cf)
t.write32(GPIOA+24,(1<<4)|(1<<5)|(1<<(7+16)))
print('forced IDT',bin(t.read32(GPIOA+16)&0xff))
# CS low then read IDT, toggle clocks
for label,word in [('CSL',1<<(4+16)),('SCKL',1<<(5+16)),('SCKH',1<<5),('CSH',1<<4)]:
 t.write32(GPIOA+24,word);print(label,bin(t.read32(GPIOA+16)&0xff))
t.resume();s.close()
