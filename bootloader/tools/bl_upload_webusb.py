"""AT32F423 user bootloader uploader over WebUSB (vendor interface 2, pyusb).

Same v1 protocol and framing as bl_upload.py (USB CDC / UART); only the
transport differs: bulk OUT EP 0x03 / bulk IN EP 0x83, 64-byte packets, on
interface 2 of the composite device 2E3C:F401. Bootloader mode is identified
by bcdDevice bit 7 (bootloader 0x0280, application 0x02xx with bit 7 clear).
See docs/bootloader-webusb.md.

  python bootloader/tools/bl_upload_webusb.py --info
  python bootloader/tools/bl_upload_webusb.py app.bin --enter --wait-app
"""
import argparse
import binascii
import importlib.util
import struct
import time
import zlib
from pathlib import Path

_spec = importlib.util.spec_from_file_location("bl_upload", Path(__file__).resolve().with_name("bl_upload.py"))
bl = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bl)

VID, PID = 0x2E3C, 0xF401
INTERFACE = 2
EP_OUT, EP_IN, PACKET = 0x03, 0x83, 64
BOOTLOADER_FLAG = 0x0080
MS_OS_20_VENDOR_CODE, MS_OS_20_DESCRIPTOR_INDEX = 0x01, 0x07
APP_CMD_ENTER_BOOTLOADER = 0x16


def is_bootloader(dev):
    return bool(dev.bcdDevice & BOOTLOADER_FLAG)


def enter_frame(seq=0):
    """Framed application command 0x16 (AA 55 id len seq crc16), as bl_upload.py sends it."""
    body = bytes((APP_CMD_ENTER_BOOTLOADER, 0, seq & 0xFF))
    return b"\xAA\x55" + body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))


def is_timeout(exc):
    return (type(exc).__name__ == "USBTimeoutError" or getattr(exc, "errno", None) in (110, 10060)
            or getattr(exc, "backend_error_code", None) == -7 or "timeout" in str(exc).lower()
            or "timed out" in str(exc).lower())


def default_backend():
    try:
        import libusb_package
        return libusb_package.get_libusb1_backend()
    except ImportError:
        return None


def find_devices(backend=None):
    import usb.core
    return list(usb.core.find(find_all=True, idVendor=VID, idProduct=PID, backend=backend) or [])


def device_serial(dev):
    try:
        return dev.serial_number
    except Exception:
        return None


def select_device(serial=None, want_bootloader=None, backend=None, timeout=0.0):
    """Return exactly one matching board; never guess between several."""
    deadline = time.monotonic() + timeout
    while True:
        devices = [d for d in find_devices(backend)
                   if (serial is None or device_serial(d) == serial)
                   and (want_bootloader is None or is_bootloader(d) == want_bootloader)]
        if len(devices) > 1:
            raise RuntimeError("ambiguous WebUSB device identity; pass --serial to choose a board")
        if devices:
            return devices[0]
        if time.monotonic() >= deadline:
            mode = {None: "", True: "bootloader-mode ", False: "application-mode "}[want_bootloader]
            raise TimeoutError(f"no {mode}2E3C:F401 device" + (f" with serial {serial}" if serial else ""))
        time.sleep(0.1)


def _claim(dev):
    import usb.core
    import usb.util
    try:
        if dev.is_kernel_driver_active(INTERFACE):
            dev.detach_kernel_driver(INTERFACE)
    except (NotImplementedError, usb.core.USBError):
        pass
    usb.util.claim_interface(dev, INTERFACE)


def _release(dev):
    import usb.util
    try:
        usb.util.release_interface(dev, INTERFACE)
    finally:
        usb.util.dispose_resources(dev)


class WebUsbLink:
    """Serial-like byte stream on interface 2 so bl_upload.request/read_ack can be reused.

    Requests may be split into any number of 64-byte OUT packets; the device
    parses a byte stream. Each reply is one 14-byte IN packet (no ZLP).
    """

    def __init__(self, dev, poll_ms=20):
        self.dev = dev
        self.poll_ms = poll_ms
        self._buf = bytearray()
        self._open = False
        _claim(dev)
        self._open = True

    @property
    def in_waiting(self):
        return len(self._buf)

    def _fill(self, timeout_ms):
        try:
            self._buf += bytes(self.dev.read(EP_IN, PACKET, timeout=timeout_ms))
            return True
        except Exception as exc:
            if is_timeout(exc):
                return False
            raise

    def read(self, size):
        if not self._buf:
            self._fill(self.poll_ms)
        out = bytes(self._buf[:size])
        del self._buf[:size]
        return out

    def write(self, data):
        written = self.dev.write(EP_OUT, data, timeout=2000)
        if written != len(data):
            raise IOError(f"short WebUSB write {written}/{len(data)}")
        return written

    def flush(self):
        pass

    def reset_input_buffer(self, max_packets=32):
        """Discard stale replies left in the IN endpoint by an earlier session."""
        self._buf.clear()
        for _ in range(max_packets):
            if not self._fill(10):
                break
        self._buf.clear()

    def close(self):
        if self._open:
            self._open = False
            _release(self.dev)


def hello(link, attempts=5):
    last = None
    for _ in range(attempts):
        try:
            base = bl.request(link, 1, 0, 0, 0, 0, timeout=0.6)
        except TimeoutError as exc:
            last = exc
            continue
        if base != bl.APP_BASE:
            raise RuntimeError(f"unexpected application base 0x{base:08X}")
        return base
    raise last


def transfer(link, data, log=print):
    """HELLO, BEGIN, DATA..., END, BOOT on an open link. BEGIN is never retried."""
    image_crc = zlib.crc32(data) & 0xFFFFFFFF
    link.reset_input_buffer()
    hello(link)
    bl.request(link, 2, 1, bl.APP_BASE, len(data), image_crc, timeout=30.0)
    for off in range(0, len(data), bl.MAX_CHUNK):
        chunk = data[off:off + bl.MAX_CHUNK]
        next_off = bl.request(link, 3, off // bl.MAX_CHUNK, bl.APP_BASE + off, len(chunk),
                              zlib.crc32(chunk) & 0xFFFFFFFF, chunk)
        if next_off != off + len(chunk):
            raise RuntimeError(f"offset mismatch: expected {off + len(chunk)}, got {next_off}")
        log(f"\r{next_off}/{len(data)} ({next_off * 100 / len(data):5.1f}%)", end="", flush=True)
    if bl.request(link, 4, 0, bl.APP_BASE, len(data), image_crc, timeout=10.0) != len(data):
        raise RuntimeError("END confirmed an unexpected image length")
    bl.request(link, 6, 0, 0, 0, 0)
    log("\nUpload complete; application startup requested.")


def enter_bootloader(dev):
    """Send app command 0x16 over the application's WebUSB interface, then release it."""
    link = WebUsbLink(dev)
    try:
        link.reset_input_buffer()
        link.write(enter_frame())
        time.sleep(0.05)
    finally:
        link.close()


def upload(path, serial=None, enter=False, wait_app=False, wait=3.0, backend=None, log=print):
    data = Path(path).read_bytes()
    bl.validate_image(data)
    if enter:
        dev = select_device(serial, None, backend, timeout=wait)
        serial = serial or device_serial(dev)
        if not serial:
            raise RuntimeError("device has no readable serial; cannot safely re-find it after reset")
        if not is_bootloader(dev):
            log(f"Application mode (bcdDevice 0x{dev.bcdDevice:04X}); requesting maintenance mode")
            enter_bootloader(dev)
        dev = select_device(serial, True, backend, timeout=15.0)
    else:
        dev = select_device(serial, True, backend, timeout=wait)
        serial = serial or device_serial(dev)
    log(f"Bootloader bcdDevice 0x{dev.bcdDevice:04X}, serial {serial}")
    link = WebUsbLink(dev)
    try:
        transfer(link, data, log)
    finally:
        link.close()
    if wait_app:
        time.sleep(0.5)
        app = select_device(serial, False, backend, timeout=15.0)
        log(f"Application re-enumerated: bcdDevice 0x{app.bcdDevice:04X}, serial {serial}")


def info(backend=None, log=print):
    """Read-only descriptor dump (no interface claim, no data transfer)."""
    import usb.util
    devices = find_devices(backend)
    if not devices:
        log("no 2E3C:F401 device")
    for dev in devices:
        mode = "BOOTLOADER" if is_bootloader(dev) else "application"
        log(f"2E3C:F401 bcdUSB 0x{dev.bcdUSB:04X} bcdDevice 0x{dev.bcdDevice:04X} ({mode}) serial {device_serial(dev)}")
        try:
            log(f"  product: {usb.util.get_string(dev, dev.iProduct)!r}")
            for intf in dev.get_active_configuration():
                name = usb.util.get_string(dev, intf.iInterface) if intf.iInterface else ""
                eps = " ".join(f"0x{ep.bEndpointAddress:02X}/{ep.wMaxPacketSize}" for ep in intf)
                log(f"  IF{intf.bInterfaceNumber} class 0x{intf.bInterfaceClass:02X} {name!r} {eps}")
            bos = bytes(dev.ctrl_transfer(0x80, 6, 0x0F00, 0, 255))
            ms = bytes(dev.ctrl_transfer(0xC0, MS_OS_20_VENDOR_CODE, 0, MS_OS_20_DESCRIPTOR_INDEX, 255))
            log(f"  BOS {len(bos)} B, MS OS 2.0 set {len(ms)} B, WINUSB={b'WINUSB' in ms}")
        except Exception as exc:
            log(f"  descriptor read failed: {exc}")


def main():
    ap = argparse.ArgumentParser(description="AT32F423 bootloader uploader over WebUSB (interface 2, pyusb)")
    ap.add_argument("image", nargs="?", help="application .bin linked at 0x08008000")
    ap.add_argument("--serial", help="USB serial number (chip UID) of the board to use")
    ap.add_argument("--enter", action="store_true",
                    help="if the board is in application mode, send command 0x16 and wait for the bootloader")
    ap.add_argument("--wait", type=float, default=3.0, help="seconds to wait for the device to appear (default 3)")
    ap.add_argument("--wait-app", action="store_true", help="after BOOT, wait for the application to re-enumerate")
    ap.add_argument("--info", action="store_true", help="list devices and descriptors only (read-only)")
    args = ap.parse_args()
    backend = default_backend()
    if args.info:
        info(backend)
        return
    if not args.image:
        ap.error("image is required unless --info is given")
    upload(args.image, args.serial, args.enter, args.wait_app, args.wait, backend)


if __name__ == "__main__":
    main()
