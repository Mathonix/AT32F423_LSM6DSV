#!/usr/bin/env python3
from pathlib import Path
import struct,time,sys,subprocess
from pyocd.core.helpers import ConnectHelper
ROOT=Path(__file__).resolve().parents[1]; HEX=ROOT/'build'/'spi_observe.hex'; MAGIC=0x424F5053
sys.path.insert(0,str(ROOT/'tools'))
from dap_flash_and_log import parse_hex,flash_target
subprocess.run(['make','-B','spi-observe'],cwd=ROOT,check=True)
flash_target(parse_hex(HEX)); time.sleep(.5)
s=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'attach','frequency':400000}); s.open()
try:
 t=s.target; off=-1
 for _ in range(40):
  raw=bytes(t.read_memory_block8(0x20000000,256)); off=raw.find(struct.pack('<I',MAGIC))
  if off>=0:
   vals=struct.unpack_from('<22I',raw,off)
   if vals[1] in (2,3,4,5): break
  time.sleep(.25)
 if off<0: raise RuntimeError('SPOB not found')
 vals=struct.unpack_from('<22I',raw,off); names=['magic','state','apb2','ref_before','ref_after_m3','ref_after_m0','m3_ctrl1','m3_ctrl2','m3_addr_edges','m3_addr_flags','m3_addr_rx','m3_data_edges','m3_data_flags','m3_data_rx','m0_ctrl1','m0_ctrl2','m0_addr_edges','m0_addr_flags','m0_addr_rx','m0_data_edges','m0_data_flags','m0_data_rx']; d=dict(zip(names,vals))
 print(f"state={d['state']} refs={d['ref_before']}/{d['ref_after_m3']}/{d['ref_after_m0']} APB2={d['apb2']}")
 for m in ('m3','m0'):
  print(f"{m}: CTRL1=0x{d[m+'_ctrl1']:08X} CTRL2=0x{d[m+'_ctrl2']:08X}")
  print(f"  addr: edges={d[m+'_addr_edges']} samples=0x{d[m+'_addr_flags']:08X} rx=0x{d[m+'_addr_rx']:02X}")
  print(f"  data: edges={d[m+'_data_edges']} samples=0x{d[m+'_data_flags']:08X} rx=0x{d[m+'_data_rx']:02X}")
 print('samples: [MISO@rise, MISO@fall, MOSI@rise, MOSI@fall]')
finally:
 try:s.target.resume()
 except:pass
 s.close()
