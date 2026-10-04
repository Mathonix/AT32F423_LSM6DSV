"""Hardware-free tests for bootloader/tools/bl_upload_webusb.py (no pyusb device needed)."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bl_upload_webusb", ROOT / "bootloader/tools/bl_upload_webusb.py")
webusb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(webusb)
bl = webusb.bl


def ack(cmd, status=0, value=0):
    body = struct.pack("<2sBBBBI", b"BL", 1, cmd | 0x80, status, 0, value)
    return body + struct.pack("<I", zlib.crc32(body))


class USBTimeoutError(Exception):
    pass


class FakeBootloader:
    """Minimal device: parses the request byte stream and queues 14-byte replies."""

    def __init__(self, bcd=0x0280, serial="UID-A", end_status=0, stale=b""):
        self.bcdDevice = bcd
        self.serial_number = serial
        self.end_status = end_status
        self.inq = [stale] if stale else []
        self.rx = bytearray()
        self.written = []
        self.packets = []

    def write(self, ep, data, timeout=None):
        assert ep == webusb.EP_OUT
        data = bytes(data)
        self.written.append(data)
        for i in range(0, len(data), 64):
            self.packets.append(data[i:i + 64])
        self.rx += data
        while len(self.rx) >= 18:
            cmd = self.rx[3]
            length = struct.unpack_from("<I", self.rx, 10)[0] if cmd == 3 else 0
            if len(self.rx) < 18 + length:
                break
            frame = bytes(self.rx[:18 + length])
            del self.rx[:18 + length]
            self.reply(frame)
        return len(data)

    def reply(self, frame):
        cmd = frame[3]
        addr, length = struct.unpack_from("<II", frame, 6)
        if self.bcdDevice & 0x80 == 0:
            return
        value = {1: bl.APP_BASE, 2: 0, 3: addr - bl.APP_BASE + length, 4: length, 6: 0}.get(cmd, 0)
        status = self.end_status if cmd == 4 else 0
        r = ack(cmd, status, value)
        self.inq += [r[:5], r[5:]] if cmd == 1 else [r]

    def read(self, ep, size, timeout=None):
        assert ep == webusb.EP_IN and size == 64
        if not self.inq:
            raise USBTimeoutError("timeout")
        return self.inq.pop(0)


def image(n=600):
    return struct.pack("<II", 0x2000BFF0, 0x08008101) + bytes(range(256)) * 3 + bytes(n)


class WebUsbUploadTests(unittest.TestCase):
    def setUp(self):
        patches = [mock.patch.object(webusb, "_claim"), mock.patch.object(webusb, "_release"),
                   mock.patch.object(webusb.time, "sleep")]
        self.mocks = [p.start() for p in patches]
        for p in patches:
            self.addCleanup(p.stop)

    def run_upload(self, devices, data, **kw):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.bin"
            path.write_bytes(data)
            with mock.patch.object(webusb, "find_devices", side_effect=devices):
                webusb.upload(path, log=lambda *a, **k: None, wait=0, **kw)

    def test_bootloader_flag(self):
        self.assertTrue(webusb.is_bootloader(FakeBootloader(0x0280)))
        self.assertFalse(webusb.is_bootloader(FakeBootloader(0x0201)))

    def test_enter_frame_matches_cdc_uploader(self):
        class Sink:
            def __init__(self): self.written = []
            def write(self, data): self.written.append(data)
            def flush(self): pass
        sink = Sink()
        with mock.patch.object(bl.time, "sleep"):
            bl.enter_bootloader(sink)
        self.assertEqual(webusb.enter_frame(), sink.written[0])

    def test_full_upload_order_endpoints_and_stale_reply(self):
        data = image()
        dev = FakeBootloader(stale=ack(6))
        self.run_upload(lambda backend=None: [dev], data)
        self.assertEqual([w[3] for w in dev.written], [1, 2] + [3] * ((len(data) + 255) // 256) + [4, 6])
        datas = [w for w in dev.written if w[3] == 3]
        self.assertEqual(b"".join(w[18:] for w in datas), data)
        self.assertTrue(all(len(p) <= 64 for p in dev.packets))
        self.assertTrue(any(len(w) > 64 for w in datas))  # requests span several packets
        self.mocks[0].assert_called_once_with(dev)
        self.mocks[1].assert_called_once_with(dev)

    def test_failed_end_never_sends_boot(self):
        dev = FakeBootloader(end_status=3)
        with self.assertRaisesRegex(RuntimeError, "rejected"):
            self.run_upload(lambda backend=None: [dev], image())
        self.assertNotIn(6, [w[3] for w in dev.written])
        self.mocks[1].assert_called_once_with(dev)

    def test_application_mode_is_not_uploaded_without_enter(self):
        app = FakeBootloader(bcd=0x0201)
        with self.assertRaises(TimeoutError):
            self.run_upload(lambda backend=None: [app], image())
        self.assertEqual(app.written, [])

    def test_ambiguous_boards_refused_before_any_write(self):
        a, b = FakeBootloader(serial="UID-A"), FakeBootloader(serial="UID-B")
        with self.assertRaisesRegex(RuntimeError, "ambiguous"):
            self.run_upload(lambda backend=None: [a, b], image())
        self.assertEqual(a.written + b.written, [])
        dev_b_only = FakeBootloader(serial="UID-B")
        self.run_upload(lambda backend=None: [a, dev_b_only], image(), serial="UID-B")
        self.assertEqual(a.written, [])
        self.assertEqual(dev_b_only.written[0][3], 1)

    def test_enter_switches_from_app_to_bootloader_with_same_serial(self):
        app = FakeBootloader(bcd=0x0201, serial="UID-A")
        other_boot = FakeBootloader(serial="UID-B")
        boot = FakeBootloader(serial="UID-A")
        calls = iter([[app, other_boot], [boot, other_boot], [boot, other_boot]])
        self.run_upload(lambda backend=None: next(calls), image(), serial="UID-A", enter=True)
        self.assertEqual(app.written, [webusb.enter_frame()])
        self.assertEqual(other_boot.written, [])
        self.assertEqual(boot.written[0][3], 1)

    def test_wait_app_after_boot(self):
        boot = FakeBootloader(serial="UID-A")
        app = FakeBootloader(bcd=0x0201, serial="UID-A")
        calls = iter([[boot], [app]])
        self.run_upload(lambda backend=None: next(calls), image(), wait_app=True)
        self.assertEqual(boot.written[-1][3], 6)


if __name__ == "__main__":
    unittest.main()
