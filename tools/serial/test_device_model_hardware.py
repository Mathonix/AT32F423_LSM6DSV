"""Read model/version and check invalid-query handling; never change device settings."""
import argparse
import binascii
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import time

import serial
from serial.tools import list_ports


def frame(command, sequence, payload=b''):
    body = bytes((command, len(payload), sequence)) + payload
    return b'\xaa\x55' + body + binascii.crc_hqx(body, 0xFFFF).to_bytes(2, 'little')


def receive(connection, sequence, timeout=3):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(connection.read(min(max(connection.in_waiting, 1), 4096)))
        while len(data) >= 7:
            start = data.find(b'\xaa\x55')
            if start < 0:
                data[:] = data[-1:]
                break
            del data[:start]
            if len(data) < 5:
                break
            length = data[3]
            if length > 64:
                del data[0]
                continue
            size = 7 + length
            if len(data) < size:
                break
            packet = bytes(data[:size])
            if binascii.crc_hqx(packet[2:-2], 0xFFFF) != int.from_bytes(packet[-2:], 'little'):
                del data[0]
                continue
            del data[:size]
            if packet[4] == sequence:
                return dict(message=packet[2], sequence=packet[4], payload_hex=packet[5:-2].hex(' ').upper(),
                            frame_hex=packet.hex(' ').upper(), crc_valid=True), packet[5:-2]
    raise TimeoutError(f'No valid response for sequence {sequence}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    candidates = [p for p in list_ports.comports()
                  if p.vid == 0x2E3C and p.pid == 0xF401 and p.serial_number == '22EC987C8068']
    if len(candidates) != 1:
        raise RuntimeError('Exactly one expected application USB CDC device is required')
    selected = candidates[0]
    result = dict(read_time=datetime.now(timezone(timedelta(hours=8))).isoformat(),
                  port=selected.device, serial_number=selected.serial_number, baud=2000000,
                  actions='Model and version queries only, plus nonempty model-query rejection', events=[])
    with serial.Serial(selected.device, 2000000, timeout=0.1, write_timeout=2) as connection:
        # Drain startup/telemetry for the established device settling period.
        deadline = time.monotonic() + 16
        while time.monotonic() < deadline:
            connection.read(min(max(connection.in_waiting, 1), 4096))
        for command, sequence, payload in ((0x35, 1, b''), (0x23, 2, b''),
                                            (0x35, 3, b'\x00'), (0x35, 4, b'')):
            request = frame(command, sequence, payload)
            connection.write(request)
            connection.flush()
            event, reply = receive(connection, sequence)
            event['request_hex'] = request.hex(' ').upper()
            if command == 0x23:
                assert event['message'] == 0x32 and len(reply) == 16
                assert reply[0] == 1 and reply[1] == 9
                event['version'] = reply[2:2 + reply[1]].decode('ascii')
                assert event['version'] == '20261003d', event
                result['firmware_version'] = event['version']
            elif payload:
                assert event['message'] == 0x90 and reply == b'\x35\x02\x00\x00', event
                event['invalid_parameter_ack'] = True
            else:
                assert event['message'] == 0x36 and reply == b'AT32', event
                event['model'] = reply.decode('ascii')
                result['model'] = event['model']
            result['events'].append(event)
    result['complete'] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
