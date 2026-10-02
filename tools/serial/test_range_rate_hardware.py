"""Real UART checks for gyro range and persistent rates; restores user settings."""
import argparse, json, struct, sys, time
from pathlib import Path
import serial
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/usb_host'))
sys.path.insert(0,str(ROOT/'tools/dap'))
from protocol import pack_command,unpack_binary
from dap_host_upgrade import release_target
parser=argparse.ArgumentParser();parser.add_argument('--run-hardware',action='store_true',required=True);parser.add_argument('--uart',default='COM6')
args=parser.parse_args();seq=0
def command(p,cmd,payload=b'',expect=0x90,status=0,timeout=4):
    global seq
    for retry in range(3):
        seq=(seq+1)&255;n=seq;p.write(pack_command(cmd,n,payload));buf=bytearray();end=time.monotonic()+timeout
        while time.monotonic()<end:
            buf.extend(p.read(max(1,min(p.in_waiting,8192))))
            for ident,number,body in unpack_binary(buf):
                if ident==expect and number==n:
                    if expect==0x90: assert body[:2]==bytes([cmd,status]),body.hex()
                    return body
    raise TimeoutError(f'CMD {cmd:02x}')
def config(p):
    b=command(p,0x1f,expect=7);assert len(b)==28 and b[0]==3 and b[6]&96==96,b.hex()
    return dict(mode=b[2],saved_mode=b[3],fast=b[4],saved_fast=b[5],out_hz=int.from_bytes(b[8:10],'little'),
                outputs=[list(struct.unpack_from('<BBH',b,10+4*i)) for i in range(2)],
                init_ms=int.from_bytes(b[18:20],'little'),saved_init_ms=int.from_bytes(b[20:22],'little'),
                range_dps=int.from_bytes(b[22:24],'little'),saved_range_dps=int.from_bytes(b[24:26],'little'),
                saved_output_hz=int.from_bytes(b[26:28],'little'))
with (ROOT/'build/lsm6dsv_spi_test/release-9axis/lsm6dsv_spi_test.elf').open('rb') as f:
    symbols={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
def diagnostic():
    session=ConnectHelper.session_with_chosen_probe(unique_id='349B8F06B96E',target_override='cortex_m',options={
        'connect_mode':'attach','frequency':1000000,'resume_on_disconnect':False,'vector_catch':''})
    assert session
    with session:
        t=session.target
        try:
            # Passive MEM-AP observation. Driver initialization already reads
            # CTRL6 back and rejects a mismatch before sampling can start.
            live=bytes(t.read_memory_block8(symbols['vqf_live'],84))
            magic,sequence,init_error=struct.unpack_from('<IIi',live)
            who=struct.unpack_from('<I',live,12)[0]
            gx,gy,gz=struct.unpack_from('<3f',live,72)
            selected=int.from_bytes(bytes(t.read_memory_block8(symbols['app_gyro_range_dps'],2)),'little')
            return dict(whoami=who,selected_dps=selected,init_error=init_error,sequence=sequence,gyro_dps=[gx,gy,gz],register_readback_verified_by_driver=init_error==0 and sequence>0)
        finally:
            release_target(t)
def reboot(p,original,dps):
    command(p,0x17)
    command(p,0x1e,struct.pack('<BBBHH',original['saved_mode'],original['saved_fast'],1,original['saved_init_ms'],dps))
    time.sleep(1 if original['saved_fast'] else original['saved_init_ms']/1000+1)
    p.reset_input_buffer();return config(p)
report={'cases':[]};initial=None;changed=False;p=serial.Serial(args.uart,2000000,timeout=.03)
try:
    initial=config(p);can=command(p,0x21,expect=8);report['initial']=initial;report['initial_can_hex']=can.hex();print('Initial',initial,flush=True)
    command(p,0x17);command(p,0x1e,struct.pack('<BBBHH',initial['saved_mode'],initial['saved_fast'],0,initial['saved_init_ms'],750),status=2)
    command(p,0x1d,struct.pack('<H',300),status=2);assert config(p)==initial
    # Set serial frequency through the real command, then reboot at each range.
    changed=True;command(p,0x1d,struct.pack('<H',500))
    assert config(p)['saved_output_hz']==500
    for dps,register in [(125,0),(250,1),(500,2),(1000,3),(2000,4),(4000,12)]:
        got=reboot(p,initial,dps);live=diagnostic()
        assert got['range_dps']==dps and got['saved_range_dps']==dps and got['out_hz']==got['saved_output_hz']==500,got
        assert got['outputs']==initial['outputs'] and command(p,0x21,expect=8)[2:22]==can[2:22]
        assert live['whoami']==0x70 and live['register_readback_verified_by_driver'] and live['selected_dps']==dps,live
        sensitivity=dps*.000035
        assert all(abs(x/sensitivity-round(x/sensitivity))<.001 for x in live['gyro_dps']),live
        report['cases'].append({'configuration':got,'diagnostic':live,'expected_ctrl6_fs':register,'dps_per_lsb':sensitivity})
        print(f'PASS {dps} dps (driver verifies FS=0x{register:X}), 500 Hz survives reboot',flush=True)
    command(p,0x17)
    new_can=bytearray(can[2:12]);struct.pack_into('<H',new_can,4,20)
    command(p,0x22,bytes(new_can)+b'\1');command(p,0x15);time.sleep(1 if initial['saved_fast'] else initial['saved_init_ms']/1000+1)
    got_can=command(p,0x21,expect=8);assert got_can[2:12]==got_can[12:22]==new_can
    report['can_50hz_reboot_persisted']=True;print('PASS CAN 50 Hz (20 ms) saved and restored at reboot',flush=True)
finally:
    try:
        if initial and changed:
            command(p,0x17);command(p,0x22,can[12:22]+b'\1')
            command(p,0x1d,struct.pack('<H',initial['saved_output_hz']))
            final=reboot(p,initial,initial['saved_range_dps']);report['restored']=final
            assert final['range_dps']==initial['saved_range_dps'] and final['out_hz']==initial['saved_output_hz']
            assert final['outputs']==initial['outputs'] and command(p,0x21,expect=8)[2:22]==can[2:22]
            print('Restored original startup, range, serial frequency, outputs and CAN',flush=True)
    finally:
        p.close();(ROOT/'artifacts/web-host/range-rate-hardware-20261001.json').write_text(json.dumps(report,indent=2),encoding='utf8')
assert len(report['cases'])==6
print('PASS real UART, six hardware ranges, output frequency and CAN reboot persistence')
