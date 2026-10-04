"""Install the paired BL/APP with one full backup, verify, preserve calibration."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import struct
from pyocd.core.helpers import ConnectHelper
from dap_flash_and_log import parse_hex, words_from_mem, unlock, erase_sector, program_word, reset_and_run_after_flash
from dap_host_upgrade import release_target

ROOT = Path(__file__).resolve().parents[2]
BASE, APP, RESERVED, END = 0x08000000, 0x08008000, 0x0803C000, 0x08040000

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flash', action='store_true', required=True)
    parser.add_argument('--probe-uid', default='04CF952C7A94199D')
    parser.add_argument('--boot-hex', type=Path, default=ROOT/'bootloader/build/at32f423_bootloader.hex')
    parser.add_argument('--app-hex', type=Path, default=ROOT/'build/shared_boot_startup/release-9axis/shared_boot_startup.hex')
    args = parser.parse_args()
    images = [('app', args.app_hex, APP, RESERVED), ('boot', args.boot_hex, BASE, APP)]
    prepared = []
    for name, path, begin, limit in images:
        memory = parse_hex(path)
        words, start, end = words_from_mem(memory)
        if start != begin or end > limit:
            raise ValueError(f'{name} outside its bounded region')
        sp, pc = struct.unpack('<II', bytes(memory.get(begin+i,255) for i in range(8)))
        if sp != 0x2000BF00 or not begin <= (pc & ~1) < end or not pc & 1:
            raise ValueError(f'{name} has incompatible vectors')
        prepared.append((name, path, begin, limit, memory, words, end))
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d-%H%M%S')
    directory = ROOT/'artifacts/diagnostics'; directory.mkdir(parents=True, exist_ok=True)
    backup = directory/f'shared-startup-before-{stamp}.bin'
    evidence = directory/f'shared-startup-flash-{stamp}.json'
    if backup.exists() or evidence.exists(): raise ValueError('Evidence already exists')
    report = {'time_beijing': stamp, 'probe': args.probe_uid, 'backup': str(backup), 'images': []}
    session = ConnectHelper.session_with_chosen_probe(unique_id=args.probe_uid, target_override='cortex_m',
        options={'connect_mode':'attach','frequency':4000000,'resume_on_disconnect':False,'vector_catch':''})
    if session is None: raise RuntimeError('MicroLink absent')
    with session:
        target = session.target
        complete = False
        try:
            ident = target.read32(0xE0042000)
            if ident != 0x700A3253: raise RuntimeError(f'Unexpected MCU {ident:08X}')
            target.halt()
            before = bytes(target.read_memory_block8(BASE, END-BASE))
            backup.write_bytes(before)
            report['backup_sha256'] = hashlib.sha256(before).hexdigest()
            print(f'Full 256KiB backup: {backup}', flush=True)
            unlock(target)
            for name, path, begin, limit, memory, words, end in prepared:
                for addr in range(begin, limit, 2048): erase_sector(target, addr)
                for i, (addr, value) in enumerate(words):
                    program_word(target, addr, value)
                    if i % 1024 == 0: print(f'{name}: {i}/{len(words)} words', flush=True)
                expected = bytes(memory.get(addr,255) for addr in range(begin,limit))
                actual = bytes(target.read_memory_block8(begin,limit-begin))
                if actual != expected: raise RuntimeError(f'{name}: full-region readback mismatch')
                report['images'].append({'name':name,'hex':str(path),'start':hex(begin),'end':hex(end),
                    'verified_region_bytes':len(actual),'sha256':hashlib.sha256(actual).hexdigest()})
                print(f'{name}: complete region verified', flush=True)
            if bytes(target.read_memory_block8(RESERVED,END-RESERVED)) != before[RESERVED-BASE:]:
                raise RuntimeError('Reserved settings/calibration region changed')
            report['config_calibration_preserved_before_reset'] = True
            target.write32(0x40023C10,0x80)
            reset_and_run_after_flash(target)
            complete = True
            report['complete'] = True
        except Exception as error:
            report['error'] = str(error)
            raise
        finally:
            target.write32(0x40023C10,0x80)
            if complete: release_target(target)
            else: target.halt()
            evidence.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f'Evidence: {evidence}',flush=True)

if __name__ == '__main__': main()
