"""Read true pre-calibration IST8310 integer samples without halting MCU."""
import argparse
import csv
import json
import struct
import time
import subprocess
from pathlib import Path
from pyocd.core.helpers import ConnectHelper

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=int, default=180)
parser.add_argument('--frequency', type=int, default=400000)
args = parser.parse_args()
if args.seconds < 1: parser.error('seconds must be positive')
root = Path(__file__).resolve().parents[2]
nm = Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
symbols = subprocess.check_output([str(nm), str(root/'build/lsm6dsv_spi_test.elf')], text=True)
addr = next(int(line.split()[0],16) for line in symbols.splitlines() if line.endswith(' mag_raw_live'))
out = root/'build/logs'/('mag_raw_'+time.strftime('%Y%m%d_%H%M%S')+'.csv')
out.parent.mkdir(parents=True, exist_ok=True)
session = ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'attach','frequency':args.frequency,'resume_on_disconnect':False})
if session is None: raise RuntimeError('No probe')
rows=[]
with session:
    t=session.target
    if t.get_state().name != 'RUNNING': raise RuntimeError('Target not running')
    def read():
        for _ in range(100):
            a=t.read32(addr+4)
            if a&1: continue
            d=struct.unpack('<5I3i',bytes(t.read_memory_block8(addr,32)))
            b=t.read32(addr+4)
            if a==d[1]==b and not b&1:
                if d[0]!=0x4D524157: raise RuntimeError('Wrong telemetry ABI/ELF')
                if d[4]!=0: raise RuntimeError('Magnetic fusion must be OFF')
                return d
        raise RuntimeError('Cannot read coherent raw sample')
    first=read()
    if not first[3]: raise RuntimeError('No magnetometer samples; check sensor')
    print(f'RAW capture {args.seconds} s; address=0x{addr:08X}; output={out}',flush=True)
    print('ROTATE NOW: slow full 3-axis rotations and flips.',flush=True)
    start=time.monotonic(); last=None; report=15; missed=0
    with out.open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f);w.writerow(['host_s','mcu_ms','sample_n','raw_x','raw_y','raw_z','uncal_x_uT','uncal_y_uT','uncal_z_uT'])
        while time.monotonic()-start<args.seconds:
            d=read();elapsed=time.monotonic()-start
            if d[3]!=last:
                if last is not None: missed+=max(0,d[3]-last-1)
                last=d[3];row=[elapsed,d[2],d[3],*d[5:],*[x*0.3 for x in d[5:]]]
                w.writerow(row);f.flush();rows.append(row)
            if elapsed>=report:
                print(f'{elapsed:.0f}s samples={len(rows)} missing_octant_hits={sum(r[6]>-0.3 and r[7]>-3.6 and r[8]>4.95 for r in rows)} raw spans='+str([max(r[k] for r in rows)-min(r[k] for r in rows) for k in (3,4,5)]),flush=True);report+=15
            time.sleep(0.005)
summary={'samples':len(rows),'host_missed_samples':missed,'elapsed_s':rows[-1][0]-rows[0][0],'raw_min':[min(r[k] for r in rows) for k in (3,4,5)],'raw_max':[max(r[k] for r in rows) for k in (3,4,5)],'mag_fusion_enabled':False,'calibration_applied':False,'csv':str(out)}
out.with_suffix('.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2),flush=True)
