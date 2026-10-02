"""Real-board output/flash/restart checks; explicitly requires --run-hardware."""
import argparse
import json
import math
from pathlib import Path
import struct
import sys
import time
import serial
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'usb_host'))
from protocol import pack_command, unpack_binary

parser = argparse.ArgumentParser()
parser.add_argument('--run-hardware', action='store_true', required=True)
parser.add_argument('--uart', default='COM6'); parser.add_argument('--usb', default='COM16')
args = parser.parse_args()
records = []
seq = 0
def command(port, cmd, payload=b'', expect=0x90, timeout=3):
    global seq
    seq = (seq + 1) & 255; current = seq
    port.write(pack_command(cmd, current, payload)); buf = bytearray(); until = time.monotonic() + timeout
    while time.monotonic() < until:
        buf.extend(port.read(max(1, min(port.in_waiting, 8192))))
        for ident, number, body in unpack_binary(buf):
            if ident == expect and number == current:
                if expect == 0x90:
                    assert body[0] == cmd and body[1] == 0, (cmd, body.hex())
                return body
    raise TimeoutError(f'{port.port} CMD {cmd:02x}')
def config(port):
    body = command(port, 0x1f, expect=7)
    assert len(body) == 18 and body[0] == 1
    return {'source': body[1], 'active_mode': body[2], 'saved_mode': body[3], 'active_fast': body[4], 'saved_fast': body[5],
            'capabilities': body[6], 'outputs': [list(struct.unpack_from('<BBH', body, 10 + 4 * i)) for i in range(2)]}
def output(port, target, fmt, mask, persist=0):
    command(port, 0x20, struct.pack('<BBHB', target, fmt, mask, persist))
    got = config(port); assert got['outputs'][target] == [fmt, 0, mask], got
    return got
def sample(port, fmt, mask):
    port.reset_input_buffer(); buf = bytearray(); count = mask.bit_count(); until = time.monotonic() + 2
    while time.monotonic() < until:
        buf.extend(port.read(max(1, min(port.in_waiting, 8192))))
        if fmt == 1:
            for ident, _, body in unpack_binary(buf):
                if ident == 6 and int.from_bytes(body[:2], 'little') == mask:
                    assert len(body) == 4 + 4 * count
                    values = struct.unpack_from('<' + 'f' * count, body, 4)
                    assert all(math.isfinite(v) for v in values); return list(values)
        else:
            tail = b'\x00\x00\x80\x7f'; start = buf.find(tail)
            if start >= 0:
                end = start + 4; size = count * 4 + 4
                if len(buf) >= end + size and buf[end + size - 4:end + size] == tail:
                    values = struct.unpack_from('<' + 'f' * count, buf, end)
                    assert all(math.isfinite(v) for v in values); return list(values)
                if end > 65536: del buf[:end]
    raise TimeoutError(f'{port.port} telemetry format={fmt} mask={mask}')
def open_port(name):
    p = serial.Serial(name, 2000000, timeout=.03); p.dtr = True; p.rts = True; return p

uart = open_port(args.uart); usb = open_port(args.usb)
try:
    initial = config(uart); print('Initial', initial, flush=True)
    assert config(usb)['source'] == 1 and initial['source'] == 0
    # Temporarily reduce output while exercising control/packet correctness.
    command(uart, 0x1d, struct.pack('<H', 200))
    masks = [1 << i for i in range(9)] + [65, 56, 448, 7, 511]
    for source, port in enumerate([uart, usb]):
        for fmt in range(2):
            for mask in masks:
                previous = config(port)['outputs'][1 - source]
                got = output(port, source, fmt, mask)
                assert got['outputs'][1 - source] == previous
                values = sample(port, fmt, mask)
                records.append({'port': port.port, 'format': fmt, 'mask': mask, 'values': values})
            print(f'{port.port} format {fmt}: {len(masks)} selections OK', flush=True)
    output(usb, 1, 1, 0); command(usb, 0x10)
    print('Disabled USB telemetry: PING and config still work', flush=True)
    # Exercise two startup configurations and full reboot persistence. Restore original startup afterwards.
    for mode, fast in [(0, 1), (1, 0)]:
        command(uart, 0x17)
        command(uart, 0x1e, bytes([mode, fast, 0]))
        got = config(uart); assert got['saved_mode'] == mode and got['saved_fast'] == fast
        output(uart, 0, 0, 65, 1); output(uart, 1, 1, 56, 1)
        command(uart, 0x15); usb.close(); uart.close()
        time.sleep(4)
        uart = open_port(args.uart)
        deadline = time.monotonic() + 8
        while True:
            try: usb = open_port(args.usb); break
            except serial.SerialException:
                if time.monotonic() > deadline: raise
                time.sleep(.2)
        got = config(uart)
        assert got['active_mode'] == mode and got['active_fast'] == fast, got
        assert got['outputs'] == [[0, 0, 65], [1, 0, 56]], got
        sample(uart, 0, 65); sample(usb, 1, 56)
        print(f'Reboot applied mode={mode}, fast={fast}, independent saved outputs', flush=True)
    command(uart, 0x17)
    command(uart, 0x1e, bytes([initial['saved_mode'], initial['saved_fast'], 0])); command(uart, 0x18)
    # Leave a useful default selected format for the browser; both interfaces Yaw/Pitch/Roll.
    output(uart, 0, 0, 7, 1); output(uart, 1, 0, 7, 1)
    command(uart, 0x1d, struct.pack('<H', 1000))
    final = config(uart)
    out = Path(__file__).resolve().parents[2] / 'artifacts/web-host/hardware-output-test.json'
    out.write_text(json.dumps({'initial': initial, 'final': final, 'selections_checked': len(records), 'samples': records}, indent=2), encoding='utf-8')
    print(f'PASS: {len(records)} real-port selections, disable/control, startup reboot and flash persistence; {out}')
finally:
    uart.close(); usb.close()
