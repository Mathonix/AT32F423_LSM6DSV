"""Explicit v1 USB/UART board test. Handshake mode never erases or programs Flash."""
import argparse
import binascii
import hashlib
import importlib.util
import json
import struct
import time
import zlib
from pathlib import Path

import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bl_upload', ROOT/'bootloader/tools/bl_upload.py')
bl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bl)


def app_frame(command, sequence=71):
    body = bytes((command, 0, sequence))
    return b'\xaa\x55' + body + struct.pack('<H', binascii.crc_hqx(body, 0xffff))


def app_frames(data):
    frames = []
    while len(data) >= 7:
        pos = data.find(b'\xaa\x55')
        if pos < 0:
            break
        data = data[pos:]
        if len(data) < 7:
            break
        length = data[3]
        if length > 64:
            data = data[1:]
            continue
        end = 7+length
        if len(data) < end:
            break
        if binascii.crc_hqx(data[2:end-2], 0xffff) == struct.unpack_from('<H', data, end-2)[0]:
            frames.append({'id': data[2], 'sequence': data[4], 'payload_hex': data[5:end-2].hex()})
            data = data[end:]
        else:
            data = data[1:]
    return frames


def identity(port):
    matches = [p for p in list_ports.comports() if p.device.upper() == port.upper()]
    if len(matches) != 1:
        raise RuntimeError(f'Expected port {port} unavailable')
    p = matches[0]
    return {'port': p.device, 'vid': p.vid, 'pid': p.pid, 'serial_number': p.serial_number}


def matching_ports(wanted):
    return [p.device for p in list_ports.comports()
            if p.vid == wanted['vid'] and p.pid == wanted['pid']
            and p.serial_number == wanted['serial_number']]


def wait_hello(wanted, baud, report, seconds=15):
    deadline = time.monotonic()+seconds
    failures = []
    while time.monotonic() < deadline:
        ports = matching_ports(wanted)
        if len(ports) > 1:
            raise RuntimeError('Ambiguous USB identity; refusing to choose another board')
        for port in ports:
            connection = None
            try:
                connection = serial.Serial(port, baud, timeout=.03, write_timeout=1)
                connection.reset_input_buffer()
                connection.write(bl.packet(1, 1, 0, 0, 0))
                connection.flush()
                value = bl.read_ack(connection, 1, timeout=.6)
                if value != bl.APP_BASE:
                    raise RuntimeError(f'Unexpected application base {value:#x}')
                report['hello'] = {'port': port, 'application_base': hex(value), 'ack_crc_verified': True}
                print(f'Boot HELLO verified on {port}', flush=True)
                return connection
            except (serial.SerialException, TimeoutError, OSError) as error:
                failures.append(str(error))
                if connection:
                    connection.close()
            except Exception:
                if connection:
                    connection.close()
                raise
        time.sleep(.1)
    report['hello_attempt_errors'] = failures[-8:]
    raise TimeoutError('No valid Boot HELLO after port re-enumeration')


def observe_application(wanted, baud, seconds=12):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        ports = matching_ports(wanted)
        if len(ports) > 1:
            raise RuntimeError('Ambiguous application port')
        for port in ports:
            try:
                with serial.Serial(port, baud, timeout=.03, write_timeout=1) as connection:
                    connection.reset_input_buffer()
                    connection.write(app_frame(0x10, 72))
                    connection.write(app_frame(0x1f, 73))
                    connection.write(app_frame(0x24, 74))
                    connection.flush()
                    received = bytearray()
                    until = min(deadline, time.monotonic()+1.5)
                    while time.monotonic() < until:
                        received.extend(connection.read(min(4096, connection.in_waiting or 1)))
                    frames = app_frames(bytes(received))
                    pong = [f for f in frames if f['id'] == 0x90 and f['sequence'] == 72
                            and bytes.fromhex(f['payload_hex'])[:2] == b'\x10\x00']
                    telemetry = sum(f['id'] in (1, 2, 3, 4, 6) for f in frames)
                    if pong and telemetry:
                        return {'port': port, 'ping_ack': pong[0],
                                'config': next((f for f in frames if f['id'] == 7), None),
                                'calibration': next((f for f in frames if f['id'] == 10), None),
                                'telemetry_frames': telemetry}
            except (serial.SerialException, OSError):
                pass
        time.sleep(.1)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=2000000)
    parser.add_argument('--mode', choices=('hello', 'handshake', 'upload', 'observe'), default='handshake')
    parser.add_argument('--image', type=Path)
    parser.add_argument('--enter', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise RuntimeError('Evidence output already exists; choose a new filename')
    report = {'mode': args.mode, 'started_unix': time.time(), 'success': False,
              'flash_transport': 'serial; no SWD programming in this script', 'events': []}
    connection = None
    try:
        wanted = identity(args.port)
        if wanted['vid'] is None or wanted['serial_number'] is None:
            raise RuntimeError('This test requires an unambiguous USB adapter identity')
        report['transport'] = wanted
        data = None
        if args.mode == 'upload':
            if args.image is None:
                raise ValueError('Upload requires an explicit application image')
            data = args.image.read_bytes()
            bl.validate_image(data)
            report['image'] = {'path': str(args.image), 'bytes': len(data),
                               'sha256': hashlib.sha256(data).hexdigest(), 'crc32': hex(zlib.crc32(data))}
        if args.mode == 'observe':
            report['application_after_boot'] = observe_application(wanted, args.baud)
            if not report['application_after_boot']:
                raise TimeoutError('No application PING reply with resumed telemetry')
            report['success'] = True
            return
        if args.enter:
            with serial.Serial(args.port, args.baud, timeout=.02, write_timeout=1) as application:
                application.reset_input_buffer()
                application.write(app_frame(0x16))
                application.flush()
                received = bytearray()
                until = time.monotonic()+.15
                while time.monotonic() < until:
                    try:
                        received.extend(application.read(min(4096, application.in_waiting or 1)))
                    except serial.SerialException:
                        break  # USB reset invalidates the old native handle.
                report['entry_replies'] = [f for f in app_frames(bytes(received)) if f['id'] == 0x90]
            print('Application entry command sent; reopening matching port', flush=True)
            time.sleep(.15)
        connection = wait_hello(wanted, args.baud, report)
        if args.mode == 'hello':
            report['success'] = True
            return
        if data is not None:
            crc = zlib.crc32(data) & 0xffffffff
            def request(command, sequence, address=0, length=0, check=0, payload=b'', timeout=3):
                connection.write(bl.packet(command, sequence, address, length, check, payload))
                connection.flush()
                value = bl.read_ack(connection, command, timeout=timeout)
                report['events'].append({'cmd': command, 'value': value, 'time_unix': time.time()})
                return value
            # v1 BEGIN is never automatically retried: a repeat would erase the app again.
            request(2, 2, bl.APP_BASE, len(data), crc, timeout=30)
            print('Application erase acknowledged', flush=True)
            for off in range(0, len(data), bl.MAX_CHUNK):
                chunk = data[off:off+bl.MAX_CHUNK]
                value = request(3, 3+off//bl.MAX_CHUNK, bl.APP_BASE+off, len(chunk),
                                zlib.crc32(chunk) & 0xffffffff, chunk)
                if value != off+len(chunk):
                    raise RuntimeError(f'Wrong confirmed offset: {value}, expected {off+len(chunk)}')
                if off % 8192 == 0:
                    print(f'Upload {value}/{len(data)} bytes', flush=True)
            value = request(4, 4+len(data)//bl.MAX_CHUNK, bl.APP_BASE, len(data), crc, timeout=10)
            if value != len(data):
                raise RuntimeError('END confirmed wrong image length')
            report['end_image_crc_verified'] = True
            print('Whole-image END CRC verified', flush=True)
        connection.write(bl.packet(6, 400, 0, 0, 0))
        connection.flush()
        report['boot_reply'] = bl.read_ack(connection, 6)
        connection.close()
        connection = None
        report['application_after_boot'] = observe_application(wanted, args.baud)
        if not report['application_after_boot']:
            raise TimeoutError('BOOT acknowledged, but application did not answer PING with resumed telemetry')
        report['success'] = True
        print('Application PING and telemetry verified after BOOT', flush=True)
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        if connection:
            connection.close()
        report['finished_unix'] = time.time()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps({k: v for k, v in report.items() if k not in ('events',)}, indent=2), flush=True)


if __name__ == '__main__':
    main()
