"""Passive SWD evidence for six-face calibration; never sends a command or writes Flash."""
import argparse
import hashlib
import json
import struct
import time
import zlib
from pathlib import Path

from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
from dap_host_upgrade import release_target

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--elf', type=Path, default=ROOT/'build/acc_six_face_review/release-9axis/acc_six_face_review.elf')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--backup', type=Path)
    args = parser.parse_args()
    with args.elf.open('rb') as stream:
        symbols = {s.name: (s['st_value'], s['st_size']) for s in ELFFile(stream).get_section_by_name('.symtab').iter_symbols()}
    assert symbols['acc_cal_rt'][1] == 188, 'Calibration runtime ABI changed'
    assert symbols['acc_calibration_active'][1] == 28, 'Calibration parameter ABI changed'
    result = {'elf': str(args.elf), 'kind': 'passive SWD; RAM snapshot may straddle samples', 'timestamp_unix': time.time()}
    session = ConnectHelper.session_with_chosen_probe(unique_id='349B8F06B96E', target_override='cortex_m', options={
        'connect_mode': 'attach', 'frequency': 4000000, 'resume_on_disconnect': False, 'vector_catch': ''})
    if session is None:
        raise RuntimeError('Expected probe unavailable')
    with session:
        t = session.target
        try:
            device_id = t.read32(0xE0042000)
            if device_id != 0x700A3253:
                raise RuntimeError(f'Unexpected MCU 0x{device_id:08x}')
            result['device_id'] = hex(device_id)
            result['cpu_state'] = t.get_state().name
            state = bytes(t.read_memory_block8(symbols['acc_cal_rt'][0], 188))
            names = ['active', 'completed_faces', 'face_mask', 'candidate', 'phase', 'status']
            result['calibration'] = dict(zip(names, state[:6]))
            result['calibration'].update(error=hex(struct.unpack_from('<H', state, 6)[0]),
                progress=struct.unpack_from('<H', state, 8)[0], samples=struct.unpack_from('<I', state, 36)[0],
                raw_g=list(struct.unpack_from('<3f', state, 76)),
                face_means_g=[list(struct.unpack_from('<3f', state, 88+12*i)) for i in range(6)],
                fitted_bias_g=list(struct.unpack_from('<3f', state, 160)),
                fitted_scale=list(struct.unpack_from('<3f', state, 172)), fitted_valid=bool(state[184]))
            active = bytes(t.read_memory_block8(symbols['acc_calibration_active'][0], 28))
            result['active_parameters'] = {'bias_g': list(struct.unpack_from('<3f', active)),
                'scale': list(struct.unpack_from('<3f', active, 12)), 'valid': bool(active[24])}
            region = bytes(t.read_memory_block8(0x0803C000, 0x4000))
            result['calibration_sector_sha256'] = hashlib.sha256(region[:2048]).hexdigest()
            records = []
            for offset in range(0, 2048, 64):
                record = region[offset:offset+64]
                if struct.unpack_from('<II', record) != (0x41434332, 2):
                    continue
                crc = (zlib.crc32(record[:56]) ^ 0xffffffff) & 0xffffffff
                complete = struct.unpack_from('<II', record, 56) == (crc, 0x41434F4B)
                records.append({'offset': offset, 'sequence': struct.unpack_from('<I', record, 8)[0],
                    'crc_and_commit_valid': complete, 'bias_g': list(struct.unpack_from('<3f', record, 12)),
                    'scale': list(struct.unpack_from('<3f', record, 24))})
            result['journal_records'] = records
            result['calibration_sector_blank'] = all(b == 255 for b in region[:2048])
            if args.backup:
                before = args.backup.read_bytes()
                assert len(before) == 0x40000
                result['device_settings_identical_to_backup'] = region[0x1000:0x2000] == before[0x3d000:0x3e000]
                result['acc_sector_identical_to_backup'] = region[:2048] == before[0x3c000:0x3c800]
                result['other_reserved_changes'] = [hex(0x0803c000+i) for i in range(0x800, 0x4000)
                    if region[i] != before[0x3c000+i]][:64]
            address = symbols['vqf_live'][0]
            a = bytes(t.read_memory_block8(address, 20))
            time.sleep(.2)
            b = bytes(t.read_memory_block8(address, 20))
            result['fusion_diagnostic'] = {'magic': hex(struct.unpack_from('<I', b)[0]),
                'init_error': struct.unpack_from('<i', b, 8)[0], 'whoami': struct.unpack_from('<I', b, 12)[0],
                'sequence_before': struct.unpack_from('<I', a, 4)[0], 'sequence_after': struct.unpack_from('<I', b, 4)[0]}
        finally:
            release_target(t)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False), encoding='utf-8')
    print(json.dumps(result, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
