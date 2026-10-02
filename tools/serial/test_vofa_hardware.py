"""Send real ASCII VOFA commands; verify both outputs and restore configuration."""
import argparse
import json
import math
from pathlib import Path
import struct
import sys
import time

import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/usb_host'))
from protocol import pack_command, unpack_binary

parser = argparse.ArgumentParser()
parser.add_argument('--run-hardware', action='store_true', required=True)
parser.add_argument('--uart', default='COM6')
parser.add_argument('--usb', help='Actual AT32 USB CDC port, if connected')
args = parser.parse_args()
seq = 0


def command(port, cmd, payload=b'', expect=0x90):
    global seq
    seq = (seq + 1) & 255
    current = seq
    port.write(pack_command(cmd, current, payload))
    buf = bytearray()
    until = time.monotonic() + 3
    while time.monotonic() < until:
        buf.extend(port.read(max(1, min(port.in_waiting, 8192))))
        for ident, number, body in unpack_binary(buf):
            if ident == expect and number == current:
                if expect == 0x90:
                    assert body[:2] == bytes([cmd, 0]), (cmd, body.hex())
                return body
    raise TimeoutError(f'{port.port} CMD {cmd:02x}')


def config(port):
    body = command(port, 0x1f, expect=7)
    assert len(body) == 22 and body[0] == 2
    return {'startupHex': body[2:10].hex() + body[18:22].hex(),
            'outputs': [list(struct.unpack_from('<BBH', body, 10 + 4 * i)) for i in range(2)]}


def wait_config(port):
    """Allow sensor initialization and USB/serial delivery after a restart."""
    until = time.monotonic() + 12
    while True:
        try:
            return config(port)
        except TimeoutError:
            if time.monotonic() >= until:
                raise


def set_custom(port, baseline):
    for target, output in enumerate(baseline['outputs']):
        command(port, 0x20, struct.pack('<BBHB', target, 1, output[2], 0))
    got = config(port)
    assert [o[0] for o in got['outputs']] == [1, 1], got


def justfloat_sample(port, mask):
    port.reset_input_buffer()
    buf = bytearray()
    tail = b'\x00\x00\x80\x7f'
    count = mask.bit_count()
    assert count
    size = count * 4 + 4
    until = time.monotonic() + 2
    while time.monotonic() < until:
        buf.extend(port.read(max(1, min(port.in_waiting, 8192))))
        end = buf.find(tail) + 4
        if end >= 4 and len(buf) >= end + size and buf[end + size - 4:end + size] == tail:
            values = list(struct.unpack_from('<' + 'f' * count, buf, end))
            assert all(math.isfinite(v) for v in values)
            return values
    raise TimeoutError(f'{port.port}: JustFloat frame unavailable')


ports = [serial.Serial(args.uart, 2000000, timeout=.03)]
baseline = None
report = {'cases': [], 'usbPhysicalTested': False}
try:
    uart = ports[0]
    # Runtime output formats may differ from flash after an earlier vofa command.
    # Establish the saved defaults before testing reboot persistence.
    command(uart, 0x15)
    time.sleep(.5)
    uart.reset_input_buffer()
    baseline = wait_config(uart)
    candidates = [p.device for p in list_ports.comports() if p.vid == 0x2e3c and p.pid == 0xf401]
    usb = args.usb or (candidates[0] if len(candidates) == 1 else None)
    if usb:
        ports.append(serial.Serial(usb, 2000000, timeout=.03))
        ports[-1].dtr = True
        ports[-1].rts = True
        report['usbPhysicalTested'] = True
    can_before = command(uart, 0x21, expect=8).hex()
    report['initial'] = baseline
    report['initialCanHex'] = can_before
    print('Initial', baseline, flush=True)
    for port in ports:
        for chunks in [[b'vofa'], [b'v', b'o', b'f', b'a'], [b'VOFA\r\n']]:
            set_custom(uart, baseline)
            for chunk in chunks:
                port.write(chunk)
                time.sleep(.01)
            got = config(uart)
            assert got['outputs'] == [[0, 0, o[2]] for o in baseline['outputs']], got
            assert got['startupHex'] == baseline['startupHex']
            mask = baseline['outputs'][0][2]
            values = justfloat_sample(uart, mask) if mask else []
            report['cases'].append({'port': port.port, 'chunksHex': [x.hex() for x in chunks], 'config': got, 'uartValues': values})
            print(f'PASS {port.port}: {[x.hex() for x in chunks]} switches UART+USB to JustFloat', flush=True)
        set_custom(uart, baseline)
        command(port, 0x10, b'vofa')
        assert [o[0] for o in config(uart)['outputs']] == [1, 1]
        corrupt = bytearray(pack_command(0x10, 201, b'vofa'))
        corrupt[-1] ^= 1
        port.write(corrupt)
        assert [o[0] for o in config(uart)['outputs']] == [1, 1]
    report['binaryPayloadDoesNotSwitch'] = True
    assert command(uart, 0x21, expect=8).hex() == can_before
    # Restart proves the ASCII command did not replace the saved output defaults.
    command(uart, 0x15)
    for port in ports[1:]:
        port.close()
    time.sleep(2)
    uart.reset_input_buffer()
    final = wait_config(uart)
    assert final == baseline, (final, baseline)
    assert command(uart, 0x21, expect=8).hex() == can_before
    report['afterReboot'] = final
    report['savedSettingsPreserved'] = True
    report['passed'] = True
finally:
    try:
        if baseline is not None:
            # Initial state was loaded from flash after the firmware upgrade.
            # Reboot also restores independent legacy presets after a failed test.
            if wait_config(ports[0]) != baseline:
                command(ports[0], 0x15)
                time.sleep(2)
                ports[0].reset_input_buffer()
            report['restored'] = wait_config(ports[0])
            assert report['restored'] == baseline
    finally:
        for port in ports:
            port.close()
        out = ROOT / 'artifacts/web-host/vofa-serial-hardware-20261001.json'
        out.write_text(json.dumps(report, indent=2), encoding='utf-8')
print('PASS VOFA text switching, binary isolation, JustFloat telemetry and reboot persistence')
if not report['usbPhysicalTested']:
    print('AT32 USB CDC not connected: USB parser covered by native regression; real ingress tested on UART only')
