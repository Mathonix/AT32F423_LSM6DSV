"""Back up/verify a board, or explicitly install only the bounded Boot region."""
import argparse
import hashlib
import json
import struct
import time
import zlib
from pathlib import Path

from pyocd.core.helpers import ConnectHelper
from dap_flash_and_log import parse_hex, words_from_mem, unlock, erase_sector, program_word, reset_and_run_after_flash
from dap_host_upgrade import release_target

BASE = 0x08000000
APP = 0x08008000
END = 0x0803c000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--snapshot', type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--verify-image', type=Path)
    parser.add_argument('--verify-boot-hex', type=Path)
    parser.add_argument('--install-boot-hex', type=Path)
    parser.add_argument('--reset', action='store_true')
    args = parser.parse_args()
    if args.output.exists() or (args.snapshot and args.snapshot.exists()):
        raise RuntimeError('Refusing to replace existing evidence/backup')
    if args.install_boot_hex and not args.snapshot:
        raise ValueError('Boot installation requires a new complete Flash backup')
    mem = parse_hex(args.install_boot_hex) if args.install_boot_hex else None
    if mem and (min(mem) != BASE or max(mem) >= APP):
        raise ValueError('Boot image outside 32 KiB Boot region')
    verify_boot = parse_hex(args.verify_boot_hex) if args.verify_boot_hex else None
    if verify_boot and (min(verify_boot) != BASE or max(verify_boot) >= APP):
        raise ValueError('Boot comparison image outside 32 KiB Boot region')
    report = {'started_unix': time.time(), 'device_id_expected': hex(0x700a3253)}
    session = ConnectHelper.session_with_chosen_probe(unique_id='349B8F06B96E', target_override='cortex_m',
        options={'connect_mode': 'attach', 'frequency': 4000000, 'resume_on_disconnect': False, 'vector_catch': ''})
    if session is None:
        raise RuntimeError('Expected SWD probe absent')
    with session:
        target = session.target
        try:
            ident = target.read32(0xE0042000)
            if ident != 0x700a3253:
                raise RuntimeError('Unexpected MCU; refusing access')
            report['device_id'] = hex(ident)
            report['cpu_state'] = target.get_state().name
            flash = bytes(target.read_memory_block8(BASE, 0x40000))
            report['flash_sha256'] = hashlib.sha256(flash).hexdigest()
            report['boot_vectors'] = [hex(v) for v in struct.unpack_from('<II', flash)]
            report['app_vectors'] = [hex(v) for v in struct.unpack_from('<II', flash, 0x8000)]
            report['boot_cookie'] = hex(target.read32(0x2000bff0))
            report['vtor'] = hex(target.read32(0xe000ed08))
            if verify_boot:
                report['boot_matches_expected_build'] = flash[:0x8000] == bytes(
                    verify_boot.get(a, 255) for a in range(BASE, APP))
            report['gyro_history_records'] = []
            for offset in (0x3e800, 0x3f800):
                record = flash[offset:offset+264]
                magic, version, sequence, count, next_index = struct.unpack_from('<5I', record)
                report['gyro_history_records'].append({'address': hex(BASE+offset), 'sequence': sequence,
                    'count': count, 'next': next_index, 'crc_valid': magic == 0x42494153 and version == 2
                    and count <= 15 and next_index < 15
                    and (zlib.crc32(record[:260]) ^ 0xffffffff) == struct.unpack_from('<I', record, 260)[0]})
            if args.snapshot:
                args.snapshot.parent.mkdir(parents=True, exist_ok=True)
                args.snapshot.write_bytes(flash)
                report['snapshot'] = str(args.snapshot)
            if args.compare:
                earlier = args.compare.read_bytes()
                if len(earlier) != 0x40000:
                    raise ValueError('Comparison requires a complete 256 KiB snapshot')
                report['boot_preserved'] = flash[:0x8000] == earlier[:0x8000]
                report['device_settings_preserved'] = flash[0x3d000:0x3e000] == earlier[0x3d000:0x3e000]
                report['acc_calibration_preserved'] = flash[0x3c000:0x3c800] == earlier[0x3c000:0x3c800]
                report['non_gyro_reserved_preserved'] = all(flash[i] == earlier[i]
                    for i in range(0x3c000, 0x40000) if not (0x3e800 <= i < 0x3f000 or 0x3f800 <= i < 0x40000))
                report['reserved_changed_bytes'] = sum(a != b for a, b in zip(flash[0x3c000:], earlier[0x3c000:]))
                report['reserved_changes_first64'] = [hex(BASE+i) for i in range(0x3c000, 0x40000)
                                                       if flash[i] != earlier[i]][:64]
            if args.verify_image:
                expected = args.verify_image.read_bytes()
                if not 8 <= len(expected) <= END-APP:
                    raise ValueError('Not an application image')
                actual = flash[0x8000:0x8000+len(expected)]
                report['application_bytes'] = len(expected)
                report['application_sha256'] = hashlib.sha256(actual).hexdigest()
                report['application_exact_match'] = actual == expected
                report['application_tail_erased'] = all(b == 255 for b in flash[0x8000+len(expected):0x3c000])
            if mem:
                words, start, end = words_from_mem(mem)
                target.halt()
                unlock(target)
                for address in range(BASE, APP, 2048):
                    erase_sector(target, address)
                for i, (address, value) in enumerate(words):
                    program_word(target, address, value)
                    if i % 1024 == 0:
                        print(f'Boot write {i}/{len(words)} words', flush=True)
                expected = bytes(mem.get(a, 255) for a in range(BASE, APP))
                actual = bytes(target.read_memory_block8(BASE, APP-BASE))
                if actual != expected:
                    raise RuntimeError('Boot readback mismatch')
                if bytes(target.read_memory_block8(APP, 0x38000)) != flash[0x8000:]:
                    raise RuntimeError('Application or reserved data changed during Boot installation')
                report['boot_install'] = {'hex': str(args.install_boot_hex), 'verified_region_bytes': len(actual),
                                        'sha256': hashlib.sha256(actual).hexdigest(),
                                        'application_and_reserved_preserved': True}
                target.write32(0x40023c10, 0x80)
            # Short diagnostic halt, resumed before leaving; no application state writes.
            target.halt()
            report['pc'] = hex(target.read_core_register('pc'))
            report['primask'] = target.read_core_register('primask')
            if args.reset or mem:
                reset_and_run_after_flash(target)
                report['reset_requested'] = True
        finally:
            if mem:
                target.write32(0x40023c10, 0x80)
            release_target(target)
    report['finished_unix'] = time.time()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
