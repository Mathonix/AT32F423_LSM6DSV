# Bootloader WebUSB host integration (v1 protocol)

Audience: web front-end (WebUSB) team. Firmware side: `bootloader/src/bl_io.c`,
`bootloader/src/bl_protocol.c`, `bootloader/inc/usb_conf.h`,
`middleware/usbd_class/cdc/cdc_desc.c`. Reference host: `bootloader/tools/bl_upload_webusb.py`.

**Status (2026-10-03):** implemented, built (15,612 B of 32 KiB) and unit-tested only.
The board still runs the older CDC-only bootloader until the new one is installed over SWD.
Everything marked *unverified* must be confirmed on hardware.

## 1. Detecting bootloader vs application

Both modes are the same composite USB device. VID, PID and serial number do not change.

| Field | Application (20261003x) | Bootloader |
| --- | --- | --- |
| VID:PID | `2E3C:F401` | `2E3C:F401` |
| serialNumber | chip UID, e.g. `22EC987C8068` | same |
| bcdUSB | `0x0210` | `0x0210` |
| **bcdDevice** | **`0x0201`** | **`0x0280`** |
| WebUSB `deviceVersionMajor/Minor/Subminor` | 2 / 0 / 1 | 2 / 8 / 0 |
| productName | `LSM6DSV USB CDC` | `LSM6DSV Bootloader` |
| interface 2 name | `LSM6DSV WebUSB` | `LSM6DSV Bootloader WebUSB` |
| Protocol on interface 2 | AA 55 application frames | `BL` bootloader frames (below) |

Rule: **bootloader ⇔ bcdDevice bit 7 is set**. In WebUSB terms this is
`(device.deviceVersionMinor & 0x8) !== 0`. Application releases keep bit 7 clear (0x0201, 0x0202, ...);
later bootloader revisions use 0x0281, 0x0282, ... Use the strings only for display.
Positive check: send HELLO and expect `value == 0x08008000`.

## 2. Interface and endpoints

| Interface | Class | Endpoints | Owner |
| --- | --- | --- | --- |
| 0 + 1 (IAD) | CDC ACM | 0x82 interrupt IN 8 B; 0x81 bulk IN / 0x01 bulk OUT, 64 B | OS serial driver (Web Serial) – do not claim |
| **2** | **vendor 0xFF/0x00/0x00**, alt 0 only | **0x83 bulk IN, 0x03 bulk OUT, 64 B packets** | WebUSB |

- Configuration value 1. `selectConfiguration(1)` only if `device.configuration` is null.
- `claimInterface(2)`, `transferOut(3, ...)`, `transferIn(3, 64)`.
- Windows: the BOS + MS OS 2.0 descriptor set (vendor code 0x01, 178 B) binds WinUSB to interface 2 only,
  DeviceInterfaceGUID `{7B926486-7EEE-499C-BFFC-1CA59C7DD9ED}` (the same as the application). The CDC
  function keeps usbser, normally with the same COM port number as the application.
- Linux: needs a udev rule granting access to 2E3C:F401. Android Chrome: `claimInterface(2)` directly.

## 3. Framing

All fields are little endian. CRC32 is IEEE 802.3 / zlib (`crc32`, init 0xFFFFFFFF, final xor).

Request: 18-byte header plus a payload for DATA only.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | magic `42 4C` ("BL") |
| 2 | 1 | version `0x01` |
| 3 | 1 | command |
| 4 | 2 | sequence (free; not echoed) |
| 6 | 4 | address |
| 10 | 4 | length |
| 14 | 4 | crc32 |
| 18 | length | payload (DATA only, 1..256 bytes) |

| Cmd | Name | address | length | crc32 | Reply value on OK |
| --- | --- | --- | --- | --- | --- |
| 0x01 | HELLO | 0 | 0 | 0 | `0x08008000` (application base) |
| 0x02 | BEGIN | `0x08008000` | image size (8..212,992) | CRC32 of whole image | 0 |
| 0x03 | DATA | `0x08008000 + offset` | chunk size ≤ 256 | CRC32 of this chunk | next offset (bytes written so far) |
| 0x04 | END | `0x08008000` | image size | CRC32 of whole image | image size |
| 0x05 | ABORT | 0 | 0 | 0 | 0 |
| 0x06 | BOOT | 0 | 0 | 0 | 0 |

DATA rules: contiguous from offset 0, address word-aligned; every non-final chunk length a multiple of 4
(use 256). BEGIN and END have no payload; their `length` is the image size.

Reply: always exactly 14 bytes.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | `42 4C` |
| 2 | 1 | `0x01` |
| 3 | 1 | command \| 0x80 |
| 4 | 1 | status |
| 5 | 1 | reserved 0 |
| 6 | 4 | value |
| 10 | 4 | CRC32 of bytes 0..9 |

Status: 0 OK, 1 BAD_FRAME (unknown command), 2 BAD_PARAM (value = expected next offset for DATA/END),
3 CRC, 4 FLASH, 5 NO_APP, 6 BUSY (another transport owns the transfer, or BOOT during a transfer).

### Packetization

- Send each request with **one** `transferOut(3, frame)`. The browser splits it into 64-byte packets
  (a 274-byte DATA frame = 4 × 64 + 18). The device parses a byte stream, so packet boundaries do not
  matter and no zero-length packet is needed, also when the frame length is a multiple of 64.
- The device never sends a reply larger than 14 bytes; each reply is a single short IN packet, never a ZLP.
  Keep one `transferIn(3, 64)` pending while waiting. Append received bytes to a buffer, search for `42 4C`,
  check the CRC and that byte 3 equals `cmd | 0x80` for the request you sent. Discard anything else
  (for example a stale reply left by an earlier page session).
- Exactly one request in flight. Wait for its reply before sending the next request.
- Replies go only to the transport that sent the request. Web Serial (CDC) and WebUSB can be open at the same
  time, but BEGIN binds the transfer to one transport. Until ABORT or reset, every other transport gets BUSY for
  everything except HELLO.

## 4. Recommended flow

```js
const VID = 0x2E3C, PID = 0xF401;
const isBoot = d => (d.deviceVersionMinor & 0x8) !== 0;

// 0. App mode: claim interface 2 and send app command 0x16 (AA 55 16 00 seq crc16).
//    crc16 = CRC-16/CCITT-FALSE over [0x16, 0x00, seq]. The app ACKs, then resets.
//    The bootloader then stays in maintenance mode with no timeout.
const serial = appDevice.serialNumber;
await appDevice.transferOut(3, enterFrame);
// 1. Expect 'disconnect' for the app device, then 'connect' for the bootloader (typically within ~1-2 s;
//    allow 15 s). Match on VID/PID/serial plus isBoot(), never on "first device".
const boot = await waitFor(async () => (await navigator.usb.getDevices())
  .filter(d => d.vendorId === VID && d.productId === PID && d.serialNumber === serial && isBoot(d)));
// If getDevices() never returns it, show a button that calls
// navigator.usb.requestDevice({filters: [{vendorId: VID, productId: PID, serialNumber: serial}]}).
await boot.open();
if (!boot.configuration) await boot.selectConfiguration(1);
await boot.claimInterface(2);
await req(HELLO);                           // value must be 0x08008000; retry HELLO only (safe)
await req(BEGIN, base, size, crcImage);     // erases the whole application region
for (let off = 0; off < size; off += 256)   // value must equal off + chunk.length
  await req(DATA, base + off, chunk.length, crc32(chunk), chunk);
await req(END, base, size, crcImage);       // value must equal size
await req(BOOT);                            // send only after END returned OK
// 2. The bootloader disconnects and jumps to the application. Wait for a
//    non-boot device with the same serial (bcdDevice 0x02xx with bit 7 clear), then reopen it.
```

Never resend BEGIN automatically. A repeated BEGIN erases the image again. On a DATA error or timeout, send ABORT
and restart from BEGIN. v1 has no resume.

## 5. Timeouts

| Step | Recommended timeout | Basis |
| --- | --- | --- |
| HELLO | 1 s, up to 5 tries | trivial |
| **BEGIN** | **10 s** (`bl_upload.py` uses 30 s) | erases all 104 × 2 KiB sectors 0x08008000–0x0803BFFF, whatever the image size. Measured on USB CDC (2026-10-01): BEGIN ACK ≤ 1.44 s after the port opened. |
| DATA | 2 s | measured ≈ 3.6 ms per 256-byte round trip (78,352 B in 1.11 s) |
| END | 5 s | whole-image bitwise CRC: 42 ms for 78 KB, about 0.12 s at 208 KiB |
| BOOT | 2 s | reply sent before USB disconnect |
| Re-enumeration (app→boot, boot→app) | 15 s | 20 ms disconnect, then clock and USB init. Windows may install WinUSB once for the new bcdDevice. |

During a Flash erase the CPU stalls on Flash access. The USB core simply NAKs, so a long BEGIN is not an error.
WebUSB timings are not measured yet. They are expected to be similar to CDC.

## 6. Permissions (unverified)

Chrome keys stored WebUSB permissions by VID, PID and serial number. The bootloader keeps all three, so
`getDevices()` should normally return it without a new chooser. This is **unverified**: the product string and
bcdDevice differ, and Android grants USB permission per attached device. Implement the `requestDevice` fallback
(a user gesture is needed) with a `serialNumber` filter. Re-enumeration always creates a new `USBDevice` object,
so call `open()` and `claimInterface(2)` again.

## 7. Safety notes

- Updating erases the application first. If power is lost before END, an image whose first chunk was already
  written may have valid vectors and be started by a later normal reset. Prefer re-entering via 0x16 and
  re-uploading after any interrupted update. There is no persistent image-valid marker in v1.
- CRC32 detects corruption only. There is no signature check.
- After a normal reset the bootloader listens for 3 s and then starts a valid application. The 0x16 entry keeps it
  in maintenance mode until BOOT.
