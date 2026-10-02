"""Hardware-free coverage of both supported updater command formats."""
import binascii
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace
import zlib

ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


canonical = load_module("bl_upload", "bootloader/tools/bl_upload.py")
legacy = load_module("bootloader_update", "tools/bootloader_update.py")


def ack(cmd, status=0, value=0):
    body = struct.pack("<2sBBBBI", b"BL", 1, cmd | 0x80, status, 0, value)
    return body + struct.pack("<I", zlib.crc32(body))


class FakeSerial:
    def __init__(self, data=b"", chunk=1):
        self.data = bytearray(data)
        self.chunk = chunk
        self.written = []
        self.closed = False

    @property
    def in_waiting(self):
        return len(self.data)

    def read(self, size):
        size = min(size, self.chunk)
        result = bytes(self.data[:size])
        del self.data[:size]
        return result

    def write(self, data):
        self.written.append(data)
        return len(data)

    def reset_input_buffer(self):
        pass

    def flush(self):
        pass

    def close(self):
        self.closed = True


class UploadTests(unittest.TestCase):
    def test_begin_and_end_have_image_length_without_payload(self):
        for cmd in (2, 4):
            c = canonical.packet(cmd, 0, 0x08008000, 60000, 123)
            l = legacy.frame(cmd, 0x08008000, value_crc=123, length=60000)
            self.assertEqual(c, l)
            self.assertEqual(len(c), 18)
            self.assertEqual(struct.unpack_from("<I", c, 10)[0], 60000)

    def test_data_lengths_and_payload_match(self):
        data = bytes(range(256))
        self.assertEqual(canonical.packet(3, 0, 0x08008000, 256, zlib.crc32(data), data),
                         legacy.frame(3, 0x08008000, data))
        with self.assertRaises(ValueError):
            legacy.frame(3, payload=data, length=1)
        with self.assertRaises(ValueError):
            legacy.frame(2, payload=data)

    def test_ack_resync_preserves_split_magic(self):
        for size in range(1, 18):
            serial = FakeSerial(b"x" * 13 + ack(1, value=42), size)
            self.assertEqual(canonical.read_ack(serial, 1, timeout=.1), 42)
            serial = FakeSerial(b"x" * 13 + ack(1, value=42), size)
            self.assertEqual(legacy.read_reply(serial, timeout=.1), (1, 0, 42))

    def test_corrupt_ack_does_not_consume_valid_ack(self):
        data = b"BL\x01\x81\x00\x00" + ack(1, value=42)
        self.assertEqual(canonical.read_ack(FakeSerial(data, 64), 1, .1), 42)
        self.assertEqual(legacy.read_reply(FakeSerial(data, 64), .1), (1, 0, 42))

    def test_enter_sends_framed_bootloader_command_not_application_reset(self):
        serial = FakeSerial()
        with mock.patch.object(canonical.time, "sleep"):
            canonical.enter_bootloader(serial)
        command = serial.written[0]
        self.assertEqual(command[:5], b"\xAA\x55\x16\x00\x00")
        self.assertEqual(struct.unpack_from("<H", command, 5)[0],
                         binascii.crc_hqx(command[2:5], 0xFFFF))

    def test_invalid_vectors_rejected_before_serial_open(self):
        for vectors in ((0x20010000, 0x08008101), (0x2000BFF0, 0x08008100),
                        (0x20000000, 0x08008101), (0x2000BFF1, 0x08008101),
                        (0x2000BFF0, 0x08030001)):
            with self.assertRaises(ValueError):
                canonical.validate_image(struct.pack("<II", *vectors) + bytes(592))

    def test_full_upload_sends_boot_only_after_end(self):
        data = struct.pack("<II", 0x2000BFF0, 0x08008101) + bytes(593)
        replies = ack(1, value=0x08008000) + ack(2)
        replies += b"".join(ack(3, value=min(off + 256, len(data)))
                            for off in range(0, len(data), 256))
        replies += ack(4, value=len(data)) + ack(6)
        serial = FakeSerial(replies)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.bin"
            path.write_bytes(data)
            with mock.patch.object(canonical.serial, "Serial", return_value=serial):
                canonical.upload("TEST", 2000000, path, False)
        self.assertEqual([p[3] for p in serial.written], [1, 2, 3, 3, 3, 4, 6])
        self.assertTrue(serial.closed)

    def test_reopens_usb_device_when_com_number_changes(self):
        adapter = SimpleNamespace(device='COM17', vid=0x2e3c, pid=0xf401, serial_number='board-a')
        other = SimpleNamespace(device='COM16', vid=0x2e3c, pid=0xf401, serial_number='board-b')
        connection = FakeSerial(ack(1, value=canonical.APP_BASE))
        with mock.patch.object(canonical.list_ports, 'comports', return_value=[other, adapter]), \
             mock.patch.object(canonical.serial, 'Serial', return_value=connection) as opener:
            result = canonical.reopen_bootloader((0x2e3c, 0xf401, 'board-a', 'COM16'), 2000000)
        self.assertIs(result, connection)
        self.assertEqual(opener.call_args.args[0], 'COM17')
        self.assertEqual([p[3] for p in connection.written], [1])

    def test_ambiguous_device_identity_never_opens_or_erases(self):
        ports = [SimpleNamespace(device=name, vid=1, pid=2, serial_number='duplicate')
                 for name in ('COM16', 'COM17')]
        with mock.patch.object(canonical.list_ports, 'comports', return_value=ports), \
             mock.patch.object(canonical.serial, 'Serial') as opener:
            with self.assertRaisesRegex(RuntimeError, 'ambiguous'):
                canonical.reopen_bootloader((1, 2, 'duplicate', 'COM16'), 2000000)
        opener.assert_not_called()

    def test_wrong_boot_partition_closes_handle_without_begin(self):
        port = SimpleNamespace(device='COM16', vid=1, pid=2, serial_number='board-a')
        connection = FakeSerial(ack(1, value=0x08000000))
        with mock.patch.object(canonical.list_ports, 'comports', return_value=[port]), \
             mock.patch.object(canonical.serial, 'Serial', return_value=connection):
            with self.assertRaisesRegex(RuntimeError, 'application base'):
                canonical.reopen_bootloader((1, 2, 'board-a', 'COM16'), 2000000)
        self.assertTrue(connection.closed)
        self.assertEqual([p[3] for p in connection.written], [1])

    def test_entry_closes_old_handle_and_uploads_only_on_reopened_handle(self):
        data = struct.pack('<II', 0x2000bff0, 0x08008009) + bytes(17)
        original = FakeSerial()
        reopened = FakeSerial(ack(2) + ack(3, value=len(data)) + ack(4, value=len(data)) + ack(6))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'app.bin'
            path.write_bytes(data)
            with mock.patch.object(canonical, 'port_identity', return_value=(1, 2, 'board-a', 'COM16')), \
                 mock.patch.object(canonical.serial, 'Serial', return_value=original), \
                 mock.patch.object(canonical, 'reopen_bootloader', return_value=reopened) as reopen, \
                 mock.patch.object(canonical.time, 'sleep'):
                canonical.upload('COM16', 2000000, path, True)
        self.assertTrue(original.closed)
        self.assertTrue(reopened.closed)
        reopen.assert_called_once_with((1, 2, 'board-a', 'COM16'), 2000000)
        self.assertEqual([p[2] for p in original.written], [0x16])
        self.assertEqual([p[3] for p in reopened.written], [2, 3, 4, 6])

    def test_failed_end_does_not_send_boot(self):
        data = struct.pack('<II', 0x2000bff0, 0x08008009) + bytes(17)
        connection = FakeSerial(ack(1, value=canonical.APP_BASE) + ack(2) + ack(3, value=len(data)) + ack(4, status=3))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'app.bin'
            path.write_bytes(data)
            with mock.patch.object(canonical.serial, 'Serial', return_value=connection):
                with self.assertRaisesRegex(RuntimeError, 'rejected'):
                    canonical.upload('TEST', 2000000, path, False)
        self.assertTrue(connection.closed)
        self.assertNotIn(6, [p[3] for p in connection.written])


if __name__ == "__main__":
    unittest.main()
