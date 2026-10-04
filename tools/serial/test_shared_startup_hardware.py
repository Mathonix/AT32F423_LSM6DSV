"""Real-board shared startup window test; temporarily changes T and restores it."""
import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import struct
import sys
import time
import serial
from serial.tools import list_ports
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/usb_host'),str(ROOT/'tools/dap'),str(ROOT/'tools')]
from protocol import pack_command,unpack_binary
from dap_host_upgrade import release_target
from read_gyro_bias_history import decode_slot
from bootloader_update import command as bl_command, CMD_HELLO, CMD_BOOT, CMD_BEGIN, CMD_DATA, CMD_END, crc32

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-hardware',action='store_true',required=True)
    parser.add_argument('--durations',nargs='+',type=int,default=[0,2000,60000])
    parser.add_argument('--probe-uid',default='04CF952C7A94199D')
    parser.add_argument('--expect-motion',action='store_true')
    parser.add_argument('--maintenance-only',action='store_true',help='T=0 forced BL and same-image USB upgrade')
    args=parser.parse_args()
    stamp=datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d-%H%M%S')
    output=ROOT/'artifacts/diagnostics'/f'shared-startup-hardware-{stamp}.json'
    report={'time_beijing':stamp,'cases':[]}
    with (ROOT/'build/shared_boot_startup/release-9axis/shared_boot_startup.elf').open('rb') as f:
        symbols={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    seq=0
    def connect(timeout=15):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            ports=[p for p in list_ports.comports() if p.vid==0x2E3C and p.pid==0xF401 and p.serial_number=='22EC987C8068']
            for p in ports:
                try: return serial.Serial(p.device,2000000,timeout=.03,write_timeout=2)
                except serial.SerialException: pass
            time.sleep(.1)
        raise RuntimeError('Expected USB CDC not available')
    def command(port,cmd,payload=b'',expect=0x90,status=0):
        nonlocal seq
        for retry in range(3):
            seq=(seq+1)&255; current=seq
            port.write(pack_command(cmd,current,payload));port.flush()
            buf=bytearray();end=time.monotonic()+3
            while time.monotonic()<end:
                buf.extend(port.read(max(1,min(port.in_waiting,8192))))
                for ident,number,body in unpack_binary(buf):
                    if ident==expect and number==current:
                        if expect==0x90: assert body[:2]==bytes([cmd,status]),body.hex()
                        return body
        raise TimeoutError(f'CMD {cmd:02X} on {port.port}')
    def config(port):
        b=command(port,0x1f,expect=7)
        assert len(b)==28 and b[0]==4,b.hex()
        return dict(version=b[0],mode=b[2],saved_mode=b[3],fast=b[4],saved_fast=b[5],
            out_hz=int.from_bytes(b[8:10],'little'),outputs=b[10:18].hex(),
            init_ms=int.from_bytes(b[18:20],'little'),saved_init_ms=int.from_bytes(b[20:22],'little'),
            range_dps=int.from_bytes(b[22:24],'little'),saved_range_dps=int.from_bytes(b[24:26],'little'),
            saved_output_hz=int.from_bytes(b[26:28],'little'))
    session=ConnectHelper.session_with_chosen_probe(unique_id=args.probe_uid,target_override='cortex_m',
        options={'connect_mode':'attach','frequency':4000000,'resume_on_disconnect':False,'vector_catch':''})
    if not session: raise RuntimeError('MicroLink absent')
    initial=None; changed=False; port=None
    with session:
        target=session.target
        def live():
            v=struct.unpack('<5I3f',bytes(target.read_memory_block8(symbols['gyro_startup_live'],32)))
            assert v[0]==0x47535443,v
            mailbox=bytes(target.read_memory_block8(0x2000bf00,64))
            fields=struct.unpack('<IIHH5I7fI',mailbox)
            return {'status':v[1],'duration_ms':v[2],'elapsed_ms':v[3],'samples':v[4],
                'bias_dps':list(v[5:]),'rejected_windows':fields[7],'rejection_reason':fields[8],
                'temperature_c':fields[9],'gravity_ms2':list(fields[13:16]),
                'app_ready':target.read8(symbols['app_fusion_ready'])}
        def history():
            slots=[decode_slot(a,bytes(target.read_memory_block8(a,2048))) for a in [0x0803e800,0x0803f800]]
            valid=[s for s in slots if s['valid']]
            return max(valid,key=lambda s:s['sequence']) if valid else {'sequence':0,'count':0,'entries':[]}
        def save(duration):
            nonlocal changed
            command(port,0x17)
            command(port,0x1e,struct.pack('<BBBHH',initial['saved_mode'],+(duration==0),0,duration,initial['saved_range_dps']))
            changed=True
            got=config(port)
            assert got['saved_init_ms']==duration and got['saved_fast']==+(duration==0),got
            command(port,0x18)
        def restart(duration):
            nonlocal port
            begin=time.monotonic(); command(port,0x15); port.close();port=None
            saw_boot=False;transition=None;first_boot=None
            deadline=begin+duration/1000+15; last_progress=begin
            while time.monotonic()<deadline:
                now=time.monotonic()
                vtor=target.read32(0xe000ed08)
                if vtor!=0x08008000:
                    if not saw_boot: first_boot=now
                    saw_boot=True
                if vtor==0x08008000 and (saw_boot or now-begin>.3):
                    if transition is None: transition=now
                    value=live()
                    if value['app_ready'] and value['duration_ms']==duration and value['status'] in (2,3):
                        value['reset_to_ready_s']=round(now-begin,4)
                        value['reset_to_app_vector_s']=round(transition-begin,4)
                        value['observed_boot_s']=round(transition-first_boot,4) if first_boot is not None else None
                        value['saw_boot']=saw_boot
                        return value
                if now-last_progress>10:
                    print(f'Waiting {duration}ms: {now-begin:.1f}s, VTOR={vtor:08X}',flush=True);last_progress=now
                time.sleep(.005)
            raise TimeoutError(f'Application startup not ready for T={duration}')
        try:
            assert target.read32(0xe000ed08)==0x08008000
            port=connect();initial=config(port);can=command(port,0x21,expect=8)
            report['initial']=initial;report['initial_diagnostic']=live();report['can_before']=can.hex()
            version=command(port,0x23,expect=0x32);model=command(port,0x35,expect=0x36)
            assert version[2:11]==b'20261003e' and model==b'AT32',(version,model)
            report['firmware_version']=version[2:11].decode('ascii')
            report['model']=model.decode('ascii')
            print('Initial',initial,'diagnostic',report['initial_diagnostic'],flush=True)
            command(port,0x17)
            command(port,0x1e,struct.pack('<BBBHH',initial['saved_mode'],0,0,60001,initial['saved_range_dps']),status=2)
            command(port,0x18)
            if args.maintenance_only:
                save(0);zero=restart(0);port=connect()
                assert zero['status']==3 and zero['samples']==0,zero
                before=history()
                command(port,0x16);port.close();port=None
                time.sleep(3.5)
                assert target.read32(0xe000ed08)==0x08000000,'T=0 must retain explicit maintenance mode'
                port=connect()
                hello=bl_command(port,CMD_HELLO)
                assert hello==0x08008000,hello
                print('PASS T=0: explicit BL entry held for 3.5s; USB HELLO replied',flush=True)
                image=(ROOT/'build/shared_boot_startup/release-9axis/shared_boot_startup.bin').read_bytes()
                bl_command(port,CMD_BEGIN,0x08008000,value_crc=crc32(image),length=len(image))
                for offset in range(0,len(image),256):
                    part=image[offset:offset+256]
                    assert bl_command(port,CMD_DATA,0x08008000+offset,part)==offset+len(part)
                    if offset%16384==0: print(f'USB upgrade: {offset}/{len(image)} bytes',flush=True)
                bl_command(port,CMD_END,0x08008000,value_crc=crc32(image),length=len(image))
                bl_command(port,CMD_BOOT);port.close();port=None
                deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    if target.read32(0xe000ed08)==0x08008000 and live()['app_ready']:break
                    time.sleep(.01)
                port=connect();got=config(port)
                assert got['init_ms']==0 and live()['status']==3 and live()['samples']==0
                assert bytes(target.read_memory_block8(0x08008000,len(image)))==image
                assert history()['sequence']==before['sequence']
                report['maintenance']={'duration_ms':0,'held_seconds':3.5,'usb_hello_base':hex(hello),
                    'same_image_usb_upgrade_bytes':len(image),'crc_verified_by_bl':True,'full_swd_readback_match':True,
                    'history_sequence_unchanged':True,'configuration':got,'diagnostic':live()}
                print('PASS same-image USB upgrade, full readback, BOOT with historical bias at T=0',flush=True)
            for duration in ([] if args.maintenance_only else args.durations):
                before=history();save(duration)
                print(f'Restarting T={duration}ms'+(' -- MOVE BOARD NOW' if args.expect_motion else ' -- stationary'),flush=True)
                value=restart(duration);after=history()
                expected=3 if duration==0 or args.expect_motion else 2
                assert value['status']==expected,value
                assert value['elapsed_ms']==duration,value
                if expected==2:
                    assert value['samples']>=duration and not value['rejected_windows'],value
                    assert after['sequence']==before['sequence']+1,(before['sequence'],after['sequence'])
                else: assert after['sequence']==before['sequence'],(before['sequence'],after['sequence'])
                assert value['reset_to_ready_s']<duration/1000+1,value
                if duration==0: assert value['samples']==0,value
                port=connect();got=config(port)
                assert got['init_ms']==got['saved_init_ms']==duration and got['fast']==+(duration==0),got
                assert got['outputs']==initial['outputs'] and got['range_dps']==initial['saved_range_dps'],got
                assert got['out_hz']==initial['out_hz'] and command(port,0x21,expect=8)[2:22]==can[2:22]
                case={'configuration':got,'diagnostic':value,'history_sequence_before':before['sequence'],'history_sequence_after':after['sequence']}
                report['cases'].append(case);print('PASS',json.dumps(case),flush=True)
            report['complete']=True
        except Exception as error:
            report['error']=str(error);raise
        finally:
            try:
                if initial and changed:
                    if port is None: port=connect(80)
                    save(initial['saved_init_ms']);restored=restart(initial['saved_init_ms']);port=connect()
                    final=config(port);assert final==initial,(final,initial)
                    report['restored']=final;report['restored_diagnostic']=restored
                    print('Original settings restored',flush=True)
            finally:
                if port is not None: port.close()
                release_target(target)
                output.parent.mkdir(parents=True,exist_ok=True)
                output.write_text(json.dumps(report,indent=2),encoding='utf-8')
                print(f'Evidence: {output}',flush=True)

if __name__=='__main__': main()
