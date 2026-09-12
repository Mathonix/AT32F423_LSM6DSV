"""Read-only, non-halting magnetic validation capture. No target writes/reset."""
import argparse, csv, json, struct, subprocess, time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper
from dap_stationary_30min import NAMES, FMT, SIZE, MAGIC

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--seconds',type=int,default=120)
    p.add_argument('--frequency',type=int,default=100000)
    a=p.parse_args()
    if a.seconds<1: p.error('seconds must be positive')
    root=Path(__file__).resolve().parents[1]
    nm=Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-nm.exe'
    syms=subprocess.check_output([str(nm),str(root/'build/lsm6dsv_spi_test.elf')],text=True)
    def symbol(name): return next(int(s.split()[0],16) for s in syms.splitlines() if s.endswith(' '+name))
    addr=symbol('vqf_tune_live'); rawaddr=symbol('mag_raw_live')
    out=root/'build/logs'/('mag_validate_'+time.strftime('%Y%m%d_%H%M%S')+'.csv')
    out.parent.mkdir(parents=True,exist_ok=True)
    meta={'csv':str(out),'samples':0,'complete':False,'error':None,'read_failures':0,'duplicate_reads':0,'frequency':a.frequency}
    session=ConnectHelper.session_with_chosen_probe(target_override='cortex_m',options={'connect_mode':'attach','frequency':a.frequency,'resume_on_disconnect':False})
    try:
        if session is None: raise RuntimeError('No DAPLink probe')
        with session:
            t=session.target
            if t.get_state().name!='RUNNING': raise RuntimeError('MCU not running; no resume/reset performed')
            def coherent(address,fmt,magic):
                for _ in range(30):
                    before=t.read32(address+4)
                    if before&1: continue
                    vals=struct.unpack(fmt,bytes(t.read_memory_block8(address,struct.calcsize(fmt))))
                    after=t.read32(address+4)
                    if before==vals[1]==after and not after&1:
                        if vals[0]!=magic: raise RuntimeError('ELF/ABI mismatch')
                        return vals
                raise RuntimeError('Cannot read coherent snapshot')
            raw=coherent(rawaddr,'<5I3i',0x4D524157)
            if raw[4]!=0 or raw[3]==0: raise RuntimeError('Need fusion OFF and active magnetometer')
            meta['mag_fusion_enabled']=False
            print('ROTATE NOW: slow three-axis rotations, flips and 2-3 second rests. '+str(out),flush=True)
            start=time.monotonic(); last=None; last_progress=start; report=0
            with out.open('w',newline='',encoding='utf-8') as f:
                w=csv.DictWriter(f,fieldnames=['host_s']+NAMES); w.writeheader()
                while time.monotonic()-start<a.seconds:
                    d=dict(zip(NAMES,coherent(addr,FMT,MAGIC)))
                    now=time.monotonic()
                    if d['seq']!=last:
                        if last is not None and d['millis']<=previous_ms: raise RuntimeError('MCU timestamp reset/nonmonotonic')
                        if d['mag_updates']!=0: raise RuntimeError('Unexpected magnetic fusion updates')
                        w.writerow(dict(host_s=now-start,**d)); f.flush()
                        last=d['seq']; previous_ms=d['millis']; last_progress=now; meta['samples']+=1
                    else: meta['duplicate_reads']+=1
                    if now-last_progress>2: raise RuntimeError('Telemetry stale >2 seconds')
                    if now-start>=report:
                        raw=coherent(rawaddr,'<5I3i',0x4D524157)
                        if raw[4]!=0: raise RuntimeError('Mag fusion enabled during capture')
                        if report and raw[3]==last_raw_n: raise RuntimeError('Magnetometer samples stale')
                        last_raw_n=raw[3]
                        print(f"{now-start:.0f}s n={meta['samples']} norm={d['mag_norm']:.3f}uT hz={d['fusion_hz']} mag_err={d['mag_err']}",flush=True); report+=15
                    time.sleep(0.006)
            meta['complete']=True
    except Exception as e:
        meta['error']=repr(e)
        raise
    finally:
        out.with_suffix('.json').write_text(json.dumps(meta,indent=2),encoding='utf-8')
        print(json.dumps(meta,indent=2),flush=True)

if __name__=='__main__': main()
