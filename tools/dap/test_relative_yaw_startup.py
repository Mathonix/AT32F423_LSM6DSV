"""Observe real startup through SWD while leaving the user's UART owner alone."""
import argparse
import json
import struct
import time
from pathlib import Path
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
from dap_host_upgrade import release_target

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--run-hardware', action='store_true', required=True)
parser.add_argument('--restart', action='store_true')
args = parser.parse_args()
with (ROOT/'build/lsm6dsv_spi_test/release-9axis/lsm6dsv_spi_test.elf').open('rb') as stream:
    table = list(ELFFile(stream).get_section_by_name('.symtab').iter_symbols())
    symbols = {s.name:s['st_value'] for s in table}
    sizes = {s.name:s['st_size'] for s in table}

session = ConnectHelper.session_with_chosen_probe(unique_id='349B8F06B96E', target_override='cortex_m', options={
    'connect_mode':'attach', 'frequency':4000000, 'resume_on_disconnect':False, 'vector_catch':''})
assert session is not None
report = {'samples':[]}
with session:
    target = session.target
    def memory(name, count):
        if count % 4 == 0:
            return struct.pack('<'+'I'*(count//4), *target.read_memory_block32(symbols[name],count//4))
        return bytes(target.read_memory_block8(symbols[name], count))
    def configuration():
        return {'active_mode':int.from_bytes(memory('app_fusion_mode',sizes['app_fusion_mode']),'little'),
                'saved_mode':int.from_bytes(memory('app_saved_settings',sizes['app_fusion_mode']),'little'),
                'fast_start':int.from_bytes(memory('app_fast_start',1),'little'),
                'init_ms':int.from_bytes(memory('app_gyro_init_ms',2),'little'),
                'range_dps':int.from_bytes(memory('app_gyro_range_dps',2),'little'),
                'output_hz':int.from_bytes(memory('app_output_hz',2),'little')}
    def snapshot():
        for _ in range(8):
            body = memory('vofa_pose_live',24)
            after = target.read32(symbols['vofa_pose_live']+4)
            magic, seq, ms, yaw, pitch, roll = struct.unpack('<III3f',body)
            if seq == after and not (seq&1) and magic == 0x56504f53:
                live = memory('vqf_live',172)
                return {'millis':ms,'yaw':yaw,'pitch':pitch,'roll':roll,'absolute_yaw':struct.unpack_from('<f',live,52)[0],
                        'absolute_pitch':struct.unpack_from('<f',live,48)[0],
                        'absolute_roll':struct.unpack_from('<f',live,44)[0],
                        'mag_ready':int.from_bytes(live[152:156],'little'),
                        'mag_updates':int.from_bytes(live[148:152],'little')}
        return None
    try:
        report['initial'] = configuration()
        print('Initial:',report['initial'],flush=True)
        if args.restart:
            assert report['initial']['active_mode'] == report['initial']['saved_mode'] == 2, 'Restart check requires saved relative mode; no configuration was changed'
            target.reset_stop_on_reset = False
            target.reset()
            target.resume()
        end = time.monotonic()+9
        while time.monotonic() < end:
            sample = snapshot()
            if sample and sample['millis']:
                report['samples'].append(sample)
            time.sleep(.025)
        report['final'] = configuration()
        assert report['initial'] == report['final'], report['final']
        report['parameters_preserved'] = True
        if report['initial']['active_mode'] == 2:
            samples = report['samples']
            assert len(samples)>30 and samples[-1]['mag_ready'], 'No nine-axis output'
            report['maximum_abs_yaw_deg'] = max(abs(s['yaw']) for s in samples)
            report['maximum_pitch_difference_deg'] = max(abs(s['pitch']-s['absolute_pitch']) for s in samples)
            report['maximum_roll_difference_deg'] = max(abs(s['roll']-s['absolute_roll']) for s in samples)
            assert report['maximum_abs_yaw_deg'] < 1.0, report['maximum_abs_yaw_deg']
            assert report['maximum_pitch_difference_deg'] < .1 and report['maximum_roll_difference_deg'] < .1
            report['passed'] = True
        else:
            report['passed'] = False
            report['reason'] = 'Device is not running relative mode; only passive configuration observation completed'
    finally:
        release_target(target)
        (ROOT/'artifacts/web-host/relative-yaw-hardware-20261001.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print({k:v for k,v in report.items() if k!='samples'})
