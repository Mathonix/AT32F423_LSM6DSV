"""Explicit real-board startup-duration checks, restoring original settings."""
import argparse
import json
from pathlib import Path
import struct
import sys
import time

import serial
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/usb_host'))
sys.path.insert(0, str(ROOT / 'tools/dap'))
from protocol import pack_command, unpack_binary
from dap_host_upgrade import release_target

parser = argparse.ArgumentParser()
parser.add_argument('--run-hardware', action='store_true', required=True)
parser.add_argument('--uart', default='COM6')
args = parser.parse_args()
seq = 0


def command(port, cmd, payload=b'', expect=0x90, status=0, timeout=3):
    global seq
    seq = (seq + 1) & 255
    current = seq
    port.write(pack_command(cmd, current, payload))
    buf = bytearray()
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        buf.extend(port.read(max(1, min(port.in_waiting, 8192))))
        for ident, number, body in unpack_binary(buf):
            if ident == expect and number == current:
                if expect == 0x90:
                    assert body[:2] == bytes([cmd, status]), (cmd, body.hex())
                return body
    raise TimeoutError(f'{port.port} CMD {cmd:02x}')


def config(port):
    body = command(port, 0x1f, expect=7)
    assert len(body) == 22 and body[0] == 2 and body[6] & 16, body.hex()
    return {'source': body[1], 'active_mode': body[2], 'saved_mode': body[3],
            'active_fast': body[4], 'saved_fast': body[5], 'capabilities': body[6],
            'outputs': [list(struct.unpack_from('<BBH', body, 10 + 4 * i)) for i in range(2)],
            'active_init_ms': int.from_bytes(body[18:20], 'little'),
            'saved_init_ms': int.from_bytes(body[20:22], 'little')}


def diagnostic():
    with (ROOT / 'build/lsm6dsv_spi_test/release-9axis/lsm6dsv_spi_test.elf').open('rb') as stream:
        symbols = {s.name: s['st_value'] for s in ELFFile(stream).get_section_by_name('.symtab').iter_symbols()}
    session = ConnectHelper.session_with_chosen_probe(unique_id='349B8F06B96E', target_override='cortex_m', options={
        'connect_mode': 'attach', 'frequency': 1000000, 'resume_on_disconnect': False, 'vector_catch': ''})
    assert session is not None
    with session:
        try:
            values = struct.unpack('<5I3f', bytes(session.target.read_memory_block8(symbols['gyro_startup_live'], 32)))
            result = dict(zip(['magic', 'status', 'durationMs', 'elapsedMs', 'samples', 'biasX', 'biasY', 'biasZ'], values))
            assert result['magic'] == 0x47535443, result
            return result
        finally:
            release_target(session.target)


def restart(port, mode, fast, duration):
    command(port, 0x17)
    command(port, 0x1e, struct.pack('<BBBH', mode, fast, 1, duration))
    time.sleep(duration / 1000 + 1 if not fast else 1)
    port.reset_input_buffer()
    got = config(port)
    assert (got['active_mode'], got['active_fast'], got['active_init_ms']) == (mode, fast, duration), got
    return got


report = {'cases': []}
port = serial.Serial(args.uart, 2000000, timeout=.03)
initial = None
changed = False
try:
    initial = config(port)
    initial_can = command(port, 0x21, expect=8).hex()
    report['initial'] = initial
    report['initialCanHex'] = initial_can
    print('Initial', initial, flush=True)
    command(port, 0x17)
    for invalid in [0, 99, 60001]:
        command(port, 0x1e, struct.pack('<BBBH', initial['saved_mode'], 0, 0, invalid), status=2)
    assert config(port) == initial
    command(port, 0x18)
    report['invalidDurationsRejectedMs'] = [0, 99, 60001]
    for duration in [2000, 2500]:
        changed = True
        got = restart(port, initial['saved_mode'], 0, duration)
        live = diagnostic()
        assert live['status'] == 2 and live['durationMs'] == duration, live
        assert duration <= live['elapsedMs'] <= duration + 5 and live['samples'] >= duration, live
        assert got['outputs'] == initial['outputs']
        assert command(port, 0x21, expect=8).hex() == initial_can
        report['cases'].append({'configuration': got, 'diagnostic': live})
        print(f"PASS normal {duration}ms: {live['elapsedMs']}ms, {live['samples']} samples", flush=True)
    # Old three-byte requests preserve the saved duration.
    command(port, 0x17)
    command(port, 0x1e, bytes([initial['saved_mode'], 0, 0]))
    assert config(port)['saved_init_ms'] == 2500
    command(port, 0x18)
    report['legacyRequestPreservesDuration'] = True
finally:
    try:
        if initial is not None and changed:
            final = restart(port, initial['saved_mode'], initial['saved_fast'], initial['saved_init_ms'])
            assert final == initial, (final, initial)
            assert command(port, 0x21, expect=8).hex() == initial_can
            live = diagnostic()
            if initial['saved_fast']:
                assert live['status'] == 0 and live['samples'] == 0, live
            report['restored'] = final
            report['restoredDiagnostic'] = live
            print('Restored original startup, outputs and CAN configuration', flush=True)
    finally:
        port.close()
        out = ROOT / 'artifacts/web-host/startup-serial-hardware-20261001.json'
        out.write_text(json.dumps(report, indent=2), encoding='utf-8')

assert len(report['cases']) == 2
print('PASS real 2s/2.5s startup calibration, invalid-duration rejection, legacy compatibility and restore')
