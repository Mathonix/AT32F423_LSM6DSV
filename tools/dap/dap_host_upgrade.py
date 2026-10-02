"""Scoped SWD application upgrade: back up flash, preserve boot/config, verify every byte."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from pyocd.core.helpers import ConnectHelper
from dap_flash_and_log import parse_hex, words_from_mem, unlock, erase_sector, program_word, reset_and_run_after_flash

ROOT = Path(__file__).resolve().parents[2]
PROBE = '349B8F06B96E'
APP = 0x08008000
END = 0x0803C000

def release_target(t):
    # Leave the application's cycle timer enabled but detach halt control.
    ap = t.aps[0]
    t.set_vector_catch(0)
    ap.write_memory(0xE000EDFC, 0x01000000, 32)
    ap.write_memory(0xE0001000, ap.read_memory(0xE0001000, 32) | 1, 32)
    t.resume()
    ap.write_memory(0xE000EDF0, 0xA05F0000, 32)
    t.flush()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--flash', action='store_true')
    parser.add_argument('--hex', type=Path, default=ROOT / 'build/lsm6dsv_spi_test/release-9axis/lsm6dsv_spi_test.hex')
    parser.add_argument('--result-path', type=Path, default=ROOT / 'artifacts/web-host/swd-upgrade.json')
    args = parser.parse_args()
    mem = parse_hex(args.hex)
    words, start, end = words_from_mem(mem)
    if start != APP or end > END:
        raise RuntimeError('Image is not confined to the application region')
    session = ConnectHelper.session_with_chosen_probe(unique_id=PROBE, target_override='cortex_m', options={
        # The firmware uses DWT as its timer. pyOCD's normal disconnect clears
        # DEMCR/TRCENA and freezes that timer; resume manually without disabling it.
        'connect_mode': 'attach', 'frequency': 1000000, 'resume_on_disconnect': False, 'vector_catch': '',
    })
    if session is None:
        raise RuntimeError('Expected WCH-Link probe not found')
    with session:
        t = session.target
        ident = t.read32(0xE0042000)
        if ident != 0x700A3253:
            raise RuntimeError(f'Unexpected device ID 0x{ident:08X}; refusing upgrade')
        print(f'Device ID 0x{ident:08X}, CPUID 0x{t.read32(0xE000ED00):08X}, state {t.get_state().name}')
        before = bytes(t.read_memory_block8(0x08000000, 0x40000))
        sp = int.from_bytes(before[:4], 'little'); reset = int.from_bytes(before[4:8], 'little')
        print(f'Boot vectors SP=0x{sp:08X} Reset=0x{reset:08X}; app 0x{start:08X}-0x{end:08X}')
        if not (0x20000000 <= sp <= 0x2000C000 and 0x08000000 <= (reset & ~1) < APP):
            raise RuntimeError('Bootloader vectors invalid; refusing application-only upgrade')
        if not args.flash:
            release_target(t)
            print('Read-only preflight OK; no flash changes'); return
        out = ROOT / 'artifacts/web-host'; out.mkdir(parents=True, exist_ok=True)
        backup = out / ('board-before-host-upgrade-' + time.strftime('%Y%m%d-%H%M%S') + '.bin')
        if backup.exists():
            raise RuntimeError('Backup already exists; refusing to replace the original backup')
        backup.write_bytes(before)
        t.halt()
        try:
            unlock(t)
            for addr in range(start, (end + 2047) & ~2047, 2048): erase_sector(t, addr)
            for i, (addr, value) in enumerate(words):
                program_word(t, addr, value)
                if i % 1024 == 0: print(f'Program {i}/{len(words)} words', flush=True)
            expected = bytes(mem.get(addr, 255) for addr in range(start, end))
            actual = bytes(t.read_memory_block8(start, len(expected)))
            if actual != expected: raise RuntimeError('Full application readback mismatch')
            if bytes(t.read_memory_block8(0x08000000, APP - 0x08000000)) != before[:0x8000]:
                raise RuntimeError('Bootloader changed unexpectedly')
            if bytes(t.read_memory_block8(END, 0x4000)) != before[0x3C000:]:
                raise RuntimeError('Configuration/calibration changed unexpectedly')
            result = {'probe': PROBE, 'device_id': hex(ident), 'start': hex(start), 'verified_bytes': len(expected),
                      'sha256': hashlib.sha256(actual).hexdigest(), 'bootloader_preserved': True,
                      'config_calibration_preserved': True, 'backup': str(backup)}
            args.result_path.write_text(json.dumps(result, indent=2), encoding='utf-8')
            print(json.dumps(result, indent=2)); reset_and_run_after_flash(t)
        finally:
            t.write32(0x40023C10, 0x80)
            release_target(t)

if __name__ == '__main__': main()
