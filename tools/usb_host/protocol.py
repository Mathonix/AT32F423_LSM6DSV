"""Host-side protocol helpers for the AT32 AHRS USB/UART link."""
from __future__ import annotations
import struct
import math
from dataclasses import dataclass

SYNC = b"\xAA\x55"
MAX_PAYLOAD = 64

MSG_ATTITUDE = 0x01
MSG_QUATERNION = 0x02
MSG_IMU_RAW = 0x03
MSG_COMPACT = 0x04
MSG_SYSTEM_INFO = 0x05
MSG_ACK = 0x90
ACK_SUCCESS = 0x00
ACK_UNKNOWN_CMD = 0x01
ACK_INVALID_PARAM = 0x02
ACK_EXEC_FAILED = 0x03

# Existing firmware command IDs.
CMD_PING = 0x10
CMD_ZERO_YAW = 0x11
CMD_RECALIBRATE_GYRO = 0x12
CMD_SET_STREAM_MODE = 0x13
CMD_QUERY_STATUS = 0x14
CMD_SYSTEM_RESET = 0x15
CMD_ENTER_BOOTLOADER = 0x16

# Settings commands implemented by the firmware. Mode is staged in flash and
# becomes active after reboot. SET_FUSION_MODE payload is [mode, apply_now].
CMD_ENTER_SETTINGS = 0x17
CMD_EXIT_SETTINGS = 0x18
CMD_SET_FUSION_MODE = 0x19
CMD_SET_CAN_NODE_ID = 0x1A       # payload: uint16 little-endian node ID
CMD_START_GYRO_CAL_60S = 0x1B    # payload: empty; MCU owns timing/LED/fallback
CMD_START_ACC_6FACE_CAL = 0x1C   # payload: empty; MCU owns face prompts/LED
CMD_SET_OUTPUT_HZ = 0x1D
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


@dataclass
class StreamStats:
    frames: int = 0
    crc_errors: int = 0
    resync_bytes: int = 0


def unpack_binary(buffer: bytearray, stats=None):
    stats = stats if stats is not None else StreamStats()
    frames = []
    while True:
        pos = buffer.find(SYNC)
        if pos < 0:
            discard = len(buffer) - int(buffer[-1:] == SYNC[:1])
            stats.resync_bytes += discard
            del buffer[:discard]
            break
        if pos:
            stats.resync_bytes += pos
            del buffer[:pos]
        if len(buffer) < 7: break
        msg_id, length, seq = buffer[2], buffer[3], buffer[4]
        if length > MAX_PAYLOAD:
            stats.resync_bytes += 1
            del buffer[0]; continue
        total = 2 + 3 + length + 2
        if len(buffer) < total: break
        raw = bytes(buffer[:total])
        if crc16_ccitt(raw[2:total - 2]) != struct.unpack_from("<H", raw, total - 2)[0]:
            # A corrupt length must not swallow a following valid frame.
            stats.crc_errors += 1
            stats.resync_bytes += 1
            del buffer[0]
            continue
        del buffer[:total]
        stats.frames += 1
        frames.append((msg_id, seq, raw[5:5 + length]))
    return frames


def decode_binary(msg_id: int, payload: bytes) -> dict:
    if msg_id == MSG_ATTITUDE and len(payload) >= 16:
        roll, pitch, yaw, flags, _reserved, timestamp = struct.unpack_from("<fffBBH", payload)
        if all(math.isfinite(v) for v in (roll, pitch, yaw)):
            return {"kind": "attitude", "roll": roll, "pitch": pitch, "yaw": yaw,
                    "flags": flags, "flags_text": flags_text(flags), "timestamp_ms": timestamp}
    if msg_id == MSG_COMPACT and len(payload) >= 12:
        roll, pitch, yaw, gz, flags, _, timestamp = struct.unpack_from("<hhhhBBH", payload)
        return {"kind": "attitude", "roll": roll / 100, "pitch": pitch / 100,
                "yaw": yaw / 100, "gz": gz / 10, "flags": flags,
                "flags_text": flags_text(flags), "timestamp_ms": timestamp}
    if msg_id == MSG_QUATERNION and len(payload) >= 18:
        qw, qx, qy, qz, timestamp = struct.unpack_from("<ffffH", payload)
        if all(math.isfinite(v) for v in (qw, qx, qy, qz)):
            return {"kind": "quaternion", "qw": qw, "qx": qx, "qy": qy, "qz": qz,
                    "timestamp_ms": timestamp}
    if msg_id == MSG_IMU_RAW and len(payload) >= 28:
        gx, gy, gz, ax, ay, az, temp, timestamp = struct.unpack_from("<ffffffhH", payload)
        if all(math.isfinite(v) for v in (gx, gy, gz, ax, ay, az)):
            return {"kind": "imu", "gx": gx, "gy": gy, "gz": gz,
                    "ax": ax, "ay": ay, "az": az, "temperature_c": temp / 100,
                    "timestamp_ms": timestamp}
    if msg_id == MSG_ACK and len(payload) >= 4:
        cmd, status, detail = struct.unpack_from("<BBH", payload)
        return {"kind": "ack", "cmd_id": cmd, "status": status, "detail": detail}
    if msg_id == MSG_SYSTEM_INFO and len(payload) >= 16:
        fusion, output, skip, temp_x100, mode, can_ok, _ = struct.unpack_from("<IIHhBBH", payload)
        return {"kind": "system", "fusion_hz": fusion, "out_hz": output, "skip_n": skip,
                "temperature_c": temp_x100 / 100.0, "stream_mode": mode, "can_ok": can_ok}
    return {"kind": "binary", "msg_id": msg_id, "payload": payload}


def flags_text(flags):
    names = ((1, "静止"), (2, "磁场有效"), (4, "磁场干扰"),
             (8, "标定完成"), (16, "传感器异常"))
    return "、".join(text for mask, text in names if flags & mask) or "正常"


def describe_ack(cmd, status, detail):
    label = {ACK_SUCCESS: "成功", ACK_UNKNOWN_CMD: "未知命令",
             ACK_INVALID_PARAM: "参数错误", ACK_EXEC_FAILED: "执行失败"}.get(status, "未知状态")
    details = {
        0x0600: "固件未启用 APP_ACC_CAL_ENABLE",
        0x0601: "当前固件不支持运行时陀螺校准",
        0x0602: "请先进入设置模式并检查参数",
        0x0603: "六面校准等待超时",
        0x0604: "校准正在进行",
        0x0605: "校准样本不足",
        0x0606: "加速度计校准比例超出范围",
        0x0607: "校准结果写入 Flash 失败",
    }
    explanation = details.get(detail) if status != ACK_SUCCESS else None
    return f"{label} cmd=0x{cmd:02X} detail=0x{detail:04X}" + (f"：{explanation}" if explanation else "")


class JustFloatDecoder:
    """Incremental VOFA+ JustFloat decoder.

    A JustFloat frame is N little-endian IEEE754 values followed by
    ``00 00 80 7f``.  The default firmware sends three values
    (yaw, pitch, roll); some builds send four (the fourth is temperature).
    Six-channel builds send yaw, pitch, roll, gz, az, temperature.
    The decoder locks to the first complete, plausibly aligned 3/4/6-channel
    frame in auto mode and keeps binary command/ACK bytes from corrupting the
    float stream.
    """

    TAIL = b"\x00\x00\x80\x7f"

    def __init__(self, channels=None):
        if channels not in (None, 0, 3, 4, 6):
            raise ValueError("channels must be 3, 4, 6, or None/0 for auto")
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
                    for ch in (3, 4, 6):
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
                    if len(buffer) > 27:
                        del buffer[:-27]
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
    """Decode complete 3/4/6-channel JustFloat frames in-place.

    ``channels=3``, ``4`` or ``6`` selects a fixed stream.  ``channels=0`` (the
    default) auto-detects and locks to the first aligned frame.  The function
    is kept for compatibility; applications receiving a long-lived stream
    should use :class:`JustFloatDecoder` so the channel lock persists between
    serial reads.
    """
    decoder = JustFloatDecoder(None if channels in (None, 0) else channels)
    return decoder.feed(buffer)


class MixedStreamDecoder:
    """Consume each byte once: binary ACKs must not become float channels.

    JustFloat has no length/checksum, so auto channel detection is inherently
    ambiguous after noise. Select a fixed channel count for noisy links.
    Binary frame payloads take precedence over embedded sync/tail patterns.
    """

    _LENGTHS = {MSG_ATTITUDE: 16, MSG_QUATERNION: 18, MSG_IMU_RAW: 28,
                MSG_COMPACT: 12, MSG_SYSTEM_INFO: 16, MSG_ACK: 4}

    def __init__(self, channels=None):
        self.just = JustFloatDecoder(channels)
        self.buffer = bytearray()
        self.stats = StreamStats()

    def _discard(self, count):
        self.stats.resync_bytes += count
        del self.buffer[:count]

    def feed(self, data):
        items = []
        # Limit retained input without dropping complete frames in large reads.
        for offset in range(0, len(data), 4096):
            self.buffer.extend(data[offset:offset + 4096])
            self._drain(items)
        return items

    def _drain(self, items):
        buf = self.buffer
        while buf:
            binary = None
            pos = buf.find(SYNC)
            while pos >= 0 and pos + 5 <= len(buf):
                length = buf[pos + 3]
                total = length + 7
                if length <= MAX_PAYLOAD and pos + total <= len(buf):
                    body = buf[pos + 2:pos + total - 2]
                    if crc16_ccitt(body) == struct.unpack_from("<H", buf, pos + total - 2)[0]:
                        binary = (pos, total)
                        break
                    if pos == 0:
                        self.stats.crc_errors += 1
                        self._discard(1)
                        break
                elif pos == 0 and length == self._LENGTHS.get(buf[2]):
                    # Do not mistake tails or nested frames in a fragmented
                    # telemetry payload for a separate message.
                    return
                pos = buf.find(SYNC, pos + 1)
            else:
                pos = -1
            if binary is None and pos == 0:
                continue  # discarded one CRC-corrupt sync byte above

            limit = binary[0] if binary is not None else len(buf)
            candidate = None
            tail = buf.find(JustFloatDecoder.TAIL, 0, limit)
            while tail >= 0:
                channels = (self.just.channels,) if self.just.channels else (6, 4, 3)
                for ch in channels:
                    start = tail - ch * 4
                    if start < 0:
                        continue
                    values = self.just._candidate(buf, start, ch)
                    if values is not None:
                        candidate = (start, tail + 4, ch, values)
                        break
                if candidate is not None:
                    break
                tail = buf.find(JustFloatDecoder.TAIL, tail + 1, limit)

            if candidate is not None:
                start, end, ch, values = candidate
                self.stats.resync_bytes += start
                del buf[:end]
                self.just.channels = ch
                self.stats.frames += 1
                items.append(("justfloat", values))
            elif binary is not None:
                start, total = binary
                msg, seq = buf[start + 2], buf[start + 4]
                payload = bytes(buf[start + 5:start + total - 2])
                self.stats.resync_bytes += start
                del buf[:start + total]
                self.stats.frames += 1
                items.append(("binary", msg, seq, payload))
            else:
                # At most one incomplete binary frame plus a float suffix.
                if len(buf) > MAX_PAYLOAD + 6:
                    self._discard(len(buf) - (MAX_PAYLOAD + 6))
                return

