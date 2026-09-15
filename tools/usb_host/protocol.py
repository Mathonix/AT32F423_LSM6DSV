"""Host-side protocol helpers for the AT32 AHRS USB/UART link."""
from __future__ import annotations
import struct
import math

SYNC = b"\xAA\x55"
MAX_PAYLOAD = 64

MSG_ATTITUDE = 0x01
MSG_SYSTEM_INFO = 0x05
MSG_ACK = 0x90

# Existing firmware command IDs.
CMD_PING = 0x10
CMD_ZERO_YAW = 0x11
CMD_RECALIBRATE_GYRO = 0x12
CMD_SET_STREAM_MODE = 0x13
CMD_QUERY_STATUS = 0x14
CMD_SYSTEM_RESET = 0x15

# Settings commands implemented by the firmware. Mode is staged in flash and
# becomes active after reboot. SET_FUSION_MODE payload is [mode, apply_now].
CMD_ENTER_SETTINGS = 0x17
CMD_EXIT_SETTINGS = 0x18
CMD_SET_FUSION_MODE = 0x19
CMD_SET_CAN_NODE_ID = 0x1A       # payload: uint16 little-endian node ID
CMD_START_GYRO_CAL_60S = 0x1B    # payload: empty; MCU owns timing/LED/fallback
CMD_START_ACC_6FACE_CAL = 0x1C   # payload: empty; MCU owns face prompts/LED
# Compatibility alias for older UI code; it must not be sent separately.
CMD_SET_ATTITUDE_MODE = CMD_SET_FUSION_MODE

MODE_6AXIS = 0
MODE_9AXIS = 1
MODE_9AXIS_RELATIVE = 2


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def pack_command(cmd_id: int, seq: int = 0, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    body = bytes((cmd_id & 0xFF, len(payload), seq & 0xFF)) + payload
    return SYNC + body + struct.pack("<H", crc16_ccitt(body))


def unpack_binary(buffer: bytearray):
    frames = []
    while True:
        pos = buffer.find(SYNC)
        if pos < 0:
            if buffer[-1:] == SYNC[:1]: del buffer[:-1]
            else: buffer.clear()
            break
        if pos: del buffer[:pos]
        if len(buffer) < 7: break
        msg_id, length, seq = buffer[2], buffer[3], buffer[4]
        if length > MAX_PAYLOAD:
            del buffer[0]; continue
        total = 2 + 3 + length + 2
        if len(buffer) < total: break
        raw = bytes(buffer[:total]); del buffer[:total]
        if crc16_ccitt(raw[2:total - 2]) != struct.unpack_from("<H", raw, total - 2)[0]:
            continue
        frames.append((msg_id, seq, raw[5:5 + length]))
    return frames


def decode_binary(msg_id: int, payload: bytes) -> dict:
    if msg_id == MSG_ATTITUDE and len(payload) >= 16:
        roll, pitch, yaw, flags, _reserved, timestamp = struct.unpack_from("<fffBBH", payload)
        return {"kind": "attitude", "roll": roll, "pitch": pitch, "yaw": yaw, "flags": flags, "timestamp_ms": timestamp}
    if msg_id == MSG_ACK and len(payload) >= 4:
        cmd, status, detail = struct.unpack_from("<BBH", payload)
        return {"kind": "ack", "cmd_id": cmd, "status": status, "detail": detail}
    if msg_id == MSG_SYSTEM_INFO and len(payload) >= 16:
        fusion, output, skip, temp_x100, mode, can_ok, _ = struct.unpack_from("<IIHhBBH", payload)
        return {"kind": "system", "fusion_hz": fusion, "out_hz": output, "skip_n": skip,
                "temperature_c": temp_x100 / 100.0, "stream_mode": mode, "can_ok": can_ok}
    return {"kind": "binary", "msg_id": msg_id, "payload": payload}


class JustFloatDecoder:
    """Incremental VOFA+ JustFloat decoder.

    A JustFloat frame is N little-endian IEEE754 values followed by
    ``00 00 80 7f``.  The default firmware sends three values
    (yaw, pitch, roll); some builds send four (the fourth is temperature).
    The decoder locks to the first complete, plausibly aligned 3/4-channel
    frame in auto mode and keeps binary command/ACK bytes from corrupting the
    float stream.
    """

    TAIL = b"\x00\x00\x80\x7f"

    def __init__(self, channels=None):
        if channels not in (None, 0, 3, 4):
            raise ValueError("channels must be 3, 4, or None/0 for auto")
        self.channels = None if channels in (None, 0) else int(channels)

    @staticmethod
    def _valid(values):
        # The limits reject most accidental interpretations of command bytes
        # while allowing full angle/temperature operating ranges.
        return all(math.isfinite(v) and abs(v) < 1.0e7 for v in values)

    def _candidate(self, data, start, channels):
        end = start + channels * 4
        if end + 4 > len(data) or data[end:end + 4] != self.TAIL:
            return None
        values = struct.unpack_from("<" + "f" * channels, data, start)
        return values if self._valid(values) else None

    def feed(self, buffer: bytearray):
        result = []
        # A malformed/very long buffer should not grow forever if a device
        # sends non-VOFA diagnostic text on the same port.
        if len(buffer) > 4096:
            del buffer[:-4096]

        while True:
            if self.channels is None:
                # Pick the earliest complete frame start.  For a clean 3ch
                # stream it is offset 0; for a clean 4ch stream the 4ch
                # candidate starts earlier than the overlapping 3ch candidate.
                best = None
                for tail_pos in range(0, len(buffer) - 3):
                    if buffer[tail_pos:tail_pos + 4] != self.TAIL:
                        continue
                    for ch in (3, 4):
                        start = tail_pos - ch * 4
                        if start < 0:
                            continue
                        values = self._candidate(buffer, start, ch)
                        if values is not None and (best is None or start < best[0]):
                            best = (start, ch, values, tail_pos + 4)
                    # A tail near the front is the only useful one; don't
                    # wait for arbitrary later data once a candidate exists.
                    if best is not None and tail_pos > best[0] + 16:
                        break
                if best is None:
                    # Retain enough bytes for a partial frame and tail.
                    if len(buffer) > 24:
                        del buffer[:-24]
                    break
                start, self.channels, values, consume_end = best
                if start:
                    del buffer[:start]
                del buffer[:consume_end - start]
                result.append(values)
                continue

            ch = self.channels
            tail_pos = buffer.find(self.TAIL, ch * 4)
            if tail_pos < 0:
                # Keep a possible partial frame, not the whole stale stream.
                keep = ch * 4 + 3
                if len(buffer) > keep:
                    del buffer[:-keep]
                break
            start = tail_pos - ch * 4
            if start < 0:
                del buffer[:tail_pos + 4]
                continue
            values = self._candidate(buffer, start, ch)
            if values is None:
                # Discard through this tail and resynchronise at the next one.
                del buffer[:tail_pos + 4]
                continue
            if start:
                del buffer[:start]
            del buffer[:ch * 4 + 4]
            result.append(values)
        return result


def decode_justfloat(buffer: bytearray, channels: int = 0):
    """Decode complete 3/4-channel JustFloat frames in-place.

    ``channels=3`` or ``4`` selects a fixed stream.  ``channels=0`` (the
    default) auto-detects and locks to the first aligned frame.  The function
    is kept for compatibility; applications receiving a long-lived stream
    should use :class:`JustFloatDecoder` so the channel lock persists between
    serial reads.
    """
    decoder = JustFloatDecoder(None if channels in (None, 0) else channels)
    return decoder.feed(buffer)

