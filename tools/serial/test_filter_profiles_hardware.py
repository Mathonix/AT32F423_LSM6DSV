"""Explicit UART checks and stationary captures. Never erases application Flash.

Run baseline before upgrade, then profiles on the new image. Profile trials
are volatile; the starting saved choice is restored. Capture durations are
reported, never treated as a long-term drift qualification.
"""
import argparse
import binascii
import csv
import json
import math
import statistics
import struct
import time
from pathlib import Path
import serial

class Link:
    def __init__(self, port, baud):
        self.s=serial.Serial(port,baud,timeout=.005,write_timeout=1)
        self.buffer=bytearray(); self.seq=0
    def send(self,cmd,payload=b''):
        self.seq=(self.seq+1)&255
        body=bytes((cmd,len(payload),self.seq))+payload
        self.s.write(b'\xaa\x55'+body+struct.pack('<H',binascii.crc_hqx(body,0xffff)))
        return self.seq
    def read(self):
        self.buffer.extend(self.s.read(min(8192,self.s.in_waiting or 1)))
        frames=[]
        while len(self.buffer)>=7:
            pos=self.buffer.find(b'\xaa\x55')
            if pos<0:
                del self.buffer[:-1];break
            del self.buffer[:pos]
            if len(self.buffer)<7:break
            n=self.buffer[3]
            if n>64:del self.buffer[0];continue
            end=n+7
            if len(self.buffer)<end:break
            if binascii.crc_hqx(self.buffer[2:end-2],0xffff)!=struct.unpack_from('<H',self.buffer,end-2)[0]:
                del self.buffer[0];continue
            frames.append((self.buffer[2],self.buffer[4],bytes(self.buffer[5:end-2])))
            del self.buffer[:end]
        return frames
    def query(self,cmd,msg,payload=b'',timeout=3):
        seq=self.send(cmd,payload);until=time.monotonic()+timeout
        while time.monotonic()<until:
            for id,s,p in self.read():
                if id==0x90 and s==seq and p[0]==cmd and p[1]:raise RuntimeError(f'Command {cmd:#x}: {p.hex()}')
                if id==msg and s==seq:return p
        raise TimeoutError(f'Command {cmd:#x} response')

def summarize(rows):
    out={'samples':len(rows),'duration_s':rows[-1][0]-rows[0][0] if len(rows)>1 else 0}
    if len(rows)<2:return out
    x=[r[0]-rows[0][0] for r in rows];xm=statistics.mean(x)
    for col,name in enumerate(('yaw','pitch','roll'),1):
        y=[]
        for r in rows:
            v=r[col]
            if y:v=y[-1]+(v-y[-1]+180)%360-180
            y.append(v)
        ym=statistics.mean(y)
        slope=sum((a-xm)*(b-ym) for a,b in zip(x,y))/sum((a-xm)**2 for a in x)
        residual=[b-(ym+slope*(a-xm)) for a,b in zip(x,y)]
        out[name]={'drift_deg_per_min':slope*60,'detrended_std_deg':statistics.pstdev(residual),
                   'peak_to_peak_deg':max(y)-min(y)}
    return out

def capture(link,path,seconds,diagnostics):
    rows=[];start=time.monotonic();last_query=0;pose=None;diag=None;last_saved=0;fresh=False
    until=start+seconds
    while time.monotonic()<until:
        now=time.monotonic()
        if diagnostics and now-last_query>=.05:link.send(0x28);last_query=now
        for id,seq,p in link.read():
            if id==6 and len(p)==40 and struct.unpack_from('<H',p)[0]==511:
                fields=struct.unpack_from('<9f',p,4)
                if all(math.isfinite(v) for v in fields):pose=fields;fresh=True
            elif id==12 and len(p)==60:diag=struct.unpack_from('<13f',p,8)
        if pose and fresh and now-last_saved>=.05:
            rows.append([now-start,*pose,*(diag or [float('nan')]*13)])
            last_saved=now;fresh=False
    if len(rows)<seconds*10:raise RuntimeError('Insufficient fresh telemetry')
    with path.open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f);w.writerow(['host_s','yaw','pitch','roll','ax','ay','az','gx','gy','gz',
          'raw_gx','raw_gy','raw_gz','bias_x','bias_y','bias_z','residual_x','residual_y','residual_z',
          'raw_roll','raw_pitch','raw_yaw','bias_sigma_dps']);w.writerows(rows)
    result=summarize(rows);print(json.dumps({'capture':str(path),**result}),flush=True);return result

def main():
    p=argparse.ArgumentParser();p.add_argument('--port',default='COM6');p.add_argument('--baud',type=int,default=2000000)
    p.add_argument('--seconds',type=float,default=60);p.add_argument('--baseline',action='store_true')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise RuntimeError('Refusing to replace evidence')
    a.output.mkdir(parents=True);l=Link(a.port,a.baud);report={'port':a.port,'requested_seconds':a.seconds,'baseline':a.baseline,'captures':{}}
    initial=None
    try:
        config=l.query(0x1f,7);report['initial_config_hex']=config.hex()
        # Require the already-configured custom nine-field UART stream. Do not
        # guess channels or silently change a user's stream to collect data.
        if config[10]!=1 or struct.unpack_from('<H',config,12)[0]!=511:
            raise RuntimeError('UART must already use custom AA55 and all nine fields')
        if a.baseline:
            report['captures']['baseline']=capture(l,a.output/'baseline.csv',a.seconds,False)
        else:
            initial=l.query(0x26,11);report['initial_filter_hex']=initial.hex()
            if len(initial)!=16 or initial[0]!=1:raise RuntimeError('Unsupported filter response')
            l.query(0x17,0x90)
            seq=l.send(0x27,bytes([3,0]));deadline=time.monotonic()+2;rejected=False
            while time.monotonic()<deadline:
                for id,s,payload in l.read():
                    if id==0x90 and s==seq and payload[0]==0x27:
                        rejected=payload[1]==2;deadline=0
            if not rejected:raise RuntimeError('Invalid profile not rejected')
            report['invalid_profile_rejected']=True
            for mode in (0,1,2):
                c=l.query(0x27,11,bytes([mode,0]));assert c[1]==mode and c[2]==initial[2]
                warm=time.monotonic()+5
                while time.monotonic()<warm:l.read()
                report['captures'][str(mode)]=capture(l,a.output/f'profile-{mode}.csv',a.seconds,True)
            restored=l.query(0x27,11,bytes([initial[1],0]));assert restored[1]==initial[1] and restored[2]==initial[2]
            report['active_and_saved_profile_restored']=True
            l.query(0x18,0x90)
        report['success']=True
    finally:
        if initial:
            try:l.query(0x27,11,bytes([initial[1],0]));l.query(0x18,0x90)
            except Exception as e:report['restore_error']=str(e)
        l.s.close();(a.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
if __name__=='__main__':main()
