"""Read-only bounded CDC DMA/runtime/DWT capture. No setters/reset/Flash."""
import argparse,json,os,struct,time,binascii
from pathlib import Path
import serial
from serial.tools import list_ports
from winusb_link import AppLink,WinUSB

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--seconds',type=float,default=5);p.add_argument('--out',type=Path,required=True);p.add_argument('--transport',choices=['cdc','winusb'],default='winusb');a=p.parse_args()
 assert 1<=a.seconds<=240;a.out.mkdir(parents=True,exist_ok=False)
 devices=[p for p in list_ports.comports() if p.device=='COM29' and p.vid==0x2e3c and p.pid==0xf401 and p.serial_number=='22EC987C8068']
 assert len(devices)==1,'wrong board identity'
 class IO:
  def __init__(self):self.s=serial.Serial('COM29',2000000,timeout=.002,write_timeout=1);self.tx=bytearray()
  def write(self,b):self.tx+=b;return self.s.write(b)
  def read(self):return self.s.read(min(8192,self.s.in_waiting or 1))
 io=IO() if a.transport=='cdc' else WinUSB()
 if a.transport=='winusb':
  io.tx=bytearray();write=io.write
  def tracked_write(b):
   n=write(b);io.tx.extend(bytes(b)[:n]);return n
  io.write=tracked_write
 l=AppLink(io);r={'capture_finished':False,'acceptance':'NOT_VERIFIED; run independent verifier','transport':a.transport,'scope':'Read-only short-window; no USB endpoint ledger/physical SOURCE proof'}
 def words(c,m,page=b''):return list(struct.unpack('<'+'I'*(16 if c in [0x82,0x78] else 15),l.query(c,m,page)))
 def check(d):
  assert d[0:2]==[1,1]
  assert (d[4]+d[7]+d[14])&0xffffffff==d[3],'DRDY accounting'
  assert (d[5]+d[8]+d[9]+d[13]+d[2])&0xffffffff==d[4],'DMA accounting'
  assert (d[6]+d[10])&0xffffffff==d[5],'ring accounting'
  assert all(d[i]==0 for i in [7,8,9,10,13,14]),'DMA capture error'
 try:
  f=l.query(0x23,0x32);assert f[2:2+f[1]]==b'20261023E'
  r['firmware']='20261023E';r['algorithm']=l.query(0x50,0x52).hex();assert bytes.fromhex(r['algorithm'])[1:3]==bytes([2,2])
  # Host drain ready before measurement starts.
  end=time.monotonic()+1
  while time.monotonic()<end:l.read()
  r['start_dma']=words(0x82,0x83);check(r['start_dma'])
  r['start_runtime']=words(0x74,0x75)
  r['start_timing']=words(0x76,0x77)
  r['start_perf']=[words(0x78,0x79,bytes([i])) for i in [0,1,2,3,4,5,15]]
  r['guards']=[];end=time.monotonic()+a.seconds;next_guard=0
  while time.monotonic()<end:
   l.read()
   if time.monotonic()>=next_guard:
    d=words(0x82,0x83);v=words(0x74,0x75);tm=words(0x76,0x77);check(d)
    assert v[4:6]==r['start_runtime'][4:6] and v[6]==v[7] and v[10]==v[11],'runtime failure'
    assert tm[3]==r['start_timing'][3],'new pose age >1ms'
    r['guards'].append({'dma':d,'runtime':v,'timing':tm});next_guard=time.monotonic()+.5
  r['end_dma']=words(0x82,0x83);check(r['end_dma']);r['end_runtime']=words(0x74,0x75)
  r['end_timing']=words(0x76,0x77);assert r['end_timing'][3]==r['start_timing'][3]
  r['end_perf']=[words(0x78,0x79,bytes([i])) for i in [0,1,2,3,4,5,15]]
  u64=lambda v,i:v[i]+(v[i+1]<<32)
  s,e=r['start_perf'][0],r['end_perf'][0];wall=u64(e,3)-u64(s,3);idle=u64(e,14)-u64(s,14)
  r['DWT_s']=wall/s[2];r['CPU_percent']=100*(1-idle/wall)
  assert s[13]==e[13]==0 and r['CPU_percent']<85
  r['buckets']=[]
  for s,e in zip(r['start_perf'],r['end_perf']):
   calls=(e[5]-s[5])&0xffffffff;cycles=u64(e,6)-u64(s,6);w=u64(e,3)-u64(s,3)
   r['buckets'].append({'page':s[1],'calls':calls,'mean_cycles':cycles/calls if calls else None,'wall_percent':100*cycles/w})
  assert l.crc_errors==0,'CRC errors'
  r['crc_errors']=l.crc_errors;r['capture_finished']=True
 except BaseException as e:r['error']=repr(e);raise
 finally:
  io.s.close() if a.transport=='cdc' else io.close()
  for name,data in [('rx.private.bin',l.raw),('tx.bin',io.tx),('result.json',json.dumps(r,indent=2).encode())]:
   with (a.out/name).open('wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
  print(json.dumps({k:v for k,v in r.items() if k not in ['guards','start_perf','end_perf']},indent=2))
if __name__=='__main__':main()
