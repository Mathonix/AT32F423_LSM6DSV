"""Real USB/UART configuration test. Does not claim physical CAN bus validation."""
import argparse
import json
from pathlib import Path
import struct
import sys
import time
import serial
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'usb_host'))
from protocol import pack_command, unpack_binary

WIRE = '<HHHBBBB'
NAMES = ['node_id', 'master_id', 'period_ms', 'baud', 'active', 'mask', 'reserved']
seq = 0

def command(port, cmd, payload=b'', expect=0x90, status=0):
    global seq
    seq = (seq + 1) & 255
    port.write(pack_command(cmd, seq, payload))
    buf = bytearray(); until = time.monotonic() + 4
    while time.monotonic() < until:
        buf.extend(port.read(max(1, min(port.in_waiting, 8192))))
        for ident, number, body in unpack_binary(buf):
            if ident == expect and number == seq:
                if expect == 0x90: assert body[0] == cmd and body[1] == status, (cmd, body.hex())
                return body
    raise TimeoutError(f'{port.port} command {cmd:02x}')

def config(port):
    b = command(port, 0x21, expect=8)
    assert len(b) == 24 and b[0] == 1 and b[1] == 1, b.hex()
    return {'active': dict(zip(NAMES, struct.unpack_from(WIRE, b, 2))),
            'saved': dict(zip(NAMES, struct.unpack_from(WIRE, b, 12))), 'bus_off': b[22]}

def apply(port, c, persist=0):
    command(port, 0x22, struct.pack(WIRE, *(c[n] for n in NAMES)) + bytes([persist]))
    got = config(port)
    assert got['active'] == c, got
    if persist: assert got['saved'] == c, got
    return got

def open_port(name):
    p = serial.Serial(name, 2000000, timeout=.025); p.dtr = True; p.rts = True
    return p

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-hardware', required=True, action='store_true')
    parser.add_argument('--uart', default='COM6'); parser.add_argument('--usb', default='COM16')
    args = parser.parse_args()
    ports = []; initial = None; complete = False; records = []
    try:
        ports = [open_port(args.uart), open_port(args.usb)]
        initial = config(ports[0]); startup = command(ports[0], 0x1F, expect=7)
        print('Initial CAN', initial, flush=True)
        command(ports[0], 0x18)
        raw = struct.pack(WIRE, *(initial['active'][n] for n in NAMES)) + b'\0'
        command(ports[0], 0x22, raw, status=3)
        assert config(ports[0])['active'] == initial['active']
        command(ports[0], 0x17)
        # Allow the one setting-mode LED transition before the rapid burst.
        time.sleep(.05)
        for source, p in enumerate(ports):
            for baud in range(8):
                for mask in range(16):
                    for active in [0, 1]:
                        c = dict(zip(NAMES, [2047, 2046, 100, baud, active, mask, 0]))
                        try: got = apply(p, c)
                        except Exception as e: raise RuntimeError(f'Failed case on {p.port}: {c}') from e
                        assert got['saved'] == initial['saved']
                        records.append({'source': source, **c})
            print(f'{p.port}: 256 runtime combinations and saved settings independence OK', flush=True)
        previous = config(ports[0])['active']
        for changes in [{'node_id': 2048}, {'master_id': 2048}, {'period_ms': 0}, {'period_ms': 10001},
                        {'baud': 8}, {'active': 2}, {'mask': 16}, {'reserved': 1}, {'baud': 7, 'active': 1, 'mask': 15, 'period_ms': 1}]:
            c = {**previous, **changes}
            command(ports[0], 0x22, struct.pack(WIRE, *(c[n] for n in NAMES)) + b'\0', status=2)
            assert config(ports[0])['active'] == previous
        command(ports[0], 0x22, b'\0', status=2)
        command(ports[0], 0x22, raw[:-1] + b'\2', status=2)
        persisted = dict(zip(NAMES, [0x123, 0x345, 30, 7, 1, 15, 0]))
        apply(ports[0], persisted, 1)
        command(ports[0], 0x15)
        for p in ports: p.close()
        ports = []; time.sleep(4)
        ports = [open_port(args.uart)]
        after = config(ports[0]); assert after['active'] == persisted and after['saved'] == persisted
        got_startup = command(ports[0], 0x1F, expect=7)
        assert got_startup[2:6] == startup[2:6] and got_startup[10:] == startup[10:]
        command(ports[0], 0x17)
        apply(ports[0], initial['saved'], 1); apply(ports[0], initial['active'])
        command(ports[0], 0x18)
        final = config(ports[0]); complete = True
        out = Path(__file__).resolve().parents[2] / 'artifacts/web-host/hardware-can-test.json'
        out.write_text(json.dumps({'initial': initial, 'persisted_reboot': after, 'final': final,
            'cases': records, 'invalid_cases': 11, 'physical_can_bus_tested': False}, indent=2), encoding='utf-8')
        print(f'PASS: {len(records)} USB/UART combinations, 11 invalid cases, reboot persistence; original settings restored', flush=True)
    finally:
        if initial and not complete:
            try:
                if not ports or not ports[0].is_open: ports = [open_port(args.uart)]
                command(ports[0], 0x17); apply(ports[0], initial['saved'], 1); apply(ports[0], initial['active'])
                command(ports[0], 0x18)
                print('Original CAN config restored after failure', flush=True)
            except Exception as e: print(f'WARNING: restore failed: {e}', flush=True)
        for p in ports: p.close()

if __name__ == '__main__': main()
