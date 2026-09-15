"""Hardware-free regression tests for the Python host application."""
from __future__ import annotations

import queue
import struct
import threading
import unittest
from types import SimpleNamespace
from unittest import mock

import serial

import app as host_app
from protocol import (
    ACK_EXEC_FAILED,
    ACK_SUCCESS,
    CMD_ENTER_SETTINGS,
    CMD_EXIT_SETTINGS,
    CMD_PING,
    CMD_SET_CAN_NODE_ID,
    CMD_SET_FUSION_MODE,
    MODE_6AXIS,
    MSG_ACK,
    MSG_COMPACT,
    MSG_IMU_RAW,
    MSG_QUATERNION,
    JustFloatDecoder,
    MixedStreamDecoder,
    StreamStats,
    decode_binary,
    describe_ack,
    pack_command,
    unpack_binary,
)


class FakeSerial:
    def __init__(self, chunks=()):
        self.chunks = list(chunks)
        self.is_open = True
        self.closed = False

    def read(self, _size):
        if self.chunks:
            return self.chunks.pop(0)
        raise OSError("simulated disconnect")

    def reset_input_buffer(self):
        pass

    def close(self):
        self.closed = True
        self.is_open = False


class ValueSink:
    """Stand-in for a Tk StringVar outside a real Tk instance."""

    def __init__(self):
        self.value = None

    def set(self, value):
        self.value = value


def make_event_sink():
    dummy = SimpleNamespace(stop=threading.Event(), events=queue.Queue())
    dummy._event = host_app.App._event.__get__(dummy)
    return dummy


class AppConnectionTests(unittest.TestCase):
    def test_connect_worker_publishes_connected_serial(self):
        fake = FakeSerial()
        dummy = make_event_sink()
        dummy.connection_generation = 4

        with mock.patch.object(host_app.serial, "Serial", return_value=fake):
            host_app.App._connect_worker(dummy, "COM_TEST", 2000000, 4)

        kind, data = dummy.events.get_nowait()
        self.assertEqual(kind, "connected")
        self.assertEqual(data, (4, fake, "COM_TEST"))

    def test_connect_worker_reports_open_error(self):
        dummy = make_event_sink()
        dummy.connection_generation = 5

        with mock.patch.object(host_app.serial, "Serial",
                               side_effect=serial.SerialException("port busy")):
            host_app.App._connect_worker(dummy, "COM_TEST", 2000000, 5)

        self.assertEqual(dummy.events.get_nowait(), ("connect_error", (5, "port busy")))

    def test_cancelled_connect_closes_late_success(self):
        fake = FakeSerial()
        dummy = make_event_sink()
        dummy.connection_generation = 8
        dummy.stop.set()

        with mock.patch.object(host_app.serial, "Serial", return_value=fake):
            host_app.App._connect_worker(dummy, "COM_TEST", 2000000, 7)

        self.assertTrue(fake.closed)
        self.assertTrue(dummy.events.empty())


class ReaderIntegrationTests(unittest.TestCase):
    def test_auto_reader_decodes_ack_between_justfloat_frames(self):
        first = struct.pack("<fff", 1.0, 2.0, 3.0) + JustFloatDecoder.TAIL
        second = struct.pack("<fff", 4.0, 5.0, 6.0) + JustFloatDecoder.TAIL
        ack = pack_command(MSG_ACK, 9, struct.pack("<BBH", CMD_PING, 0, 0))
        fake = FakeSerial([first + ack + second])
        dummy = make_event_sink()
        dummy.connection_generation = 11
        dummy.parse_mode_value = "auto"
        dummy.ser = fake

        host_app.App.reader(dummy, fake, 11)

        events = []
        while not dummy.events.empty():
            events.append(dummy.events.get_nowait())
        poses = [data for kind, data in events if kind == "pose"]
        binary = [data for kind, data in events if kind == "binary"]
        self.assertEqual(poses, [[1.0, 2.0, 3.0, None, None, None],
                                 [4.0, 5.0, 6.0, None, None, None]])
        self.assertEqual(binary[0]["cmd_id"], CMD_PING)
        self.assertEqual(events[-1][0], "reader_stopped")

    def test_reader_publishes_rate_and_error_counters(self):
        frame = struct.pack("<fff", 1.0, 2.0, 3.0) + JustFloatDecoder.TAIL
        fake = FakeSerial([frame * 4])
        dummy = make_event_sink()
        dummy.connection_generation = 12
        dummy.parse_mode_value = "justfloat"
        dummy.ser = fake
        dummy.connection_generation = 12

        # The reader takes one timestamp before the loop and one per chunk; the
        # second one is two seconds later, which must emit exactly one rate event.
        with mock.patch.object(host_app.time, "monotonic", side_effect=[0.0, 2.0]):
            host_app.App.reader(dummy, fake, 12)

        events = []
        while not dummy.events.empty():
            events.append(dummy.events.get_nowait())
        rates = [data for kind, data in events if kind == "rate"]
        self.assertEqual(len(rates), 1)
        self.assertEqual(rates[0], {"fps": 4, "crc": 0, "resync": 0})


class PoseMappingTests(unittest.TestCase):
    def test_three_channel_frame(self):
        self.assertEqual(host_app._pose_from_justfloat((1.0, 2.0, 3.0)),
                         [1.0, 2.0, 3.0, None, None, None])

    def test_four_channel_frame_carries_temperature(self):
        self.assertEqual(host_app._pose_from_justfloat((1.0, 2.0, 3.0, 25.5)),
                         [1.0, 2.0, 3.0, 25.5, None, None])

    def test_six_channel_frame_orders_gz_az_temperature(self):
        # Documented layout: yaw, pitch, roll, gz, az, temperature.
        self.assertEqual(host_app._pose_from_justfloat((1.0, 2.0, 3.0, 4.0, 5.0, 6.0)),
                         [1.0, 2.0, 3.0, 6.0, 4.0, 5.0])

    def test_short_frame_is_ignored(self):
        self.assertEqual(host_app._pose_from_justfloat((1.0, 2.0)),
                         [None] * 6)


class AckHandlingTests(unittest.TestCase):
    @staticmethod
    def ack(cmd, status=ACK_SUCCESS, detail=0):
        """Build the same dictionary the reader publishes for an ACK frame."""
        return decode_binary(MSG_ACK, struct.pack("<BBH", cmd, status, detail))

    def make_sink(self, immediate=True):
        dummy = SimpleNamespace(
            settings_mode=False,
            pending_restart=False,
            pending_exit_after_mode=False,
            immediate_restart=SimpleNamespace(get=lambda: immediate),
            exited_settings=False,
            logged=[],
        )
        dummy.settings_text = ValueSink()
        dummy.can_id = ValueSink()
        dummy.log_line = dummy.logged.append
        dummy._update_settings_controls = lambda: None
        dummy.exit_settings = lambda: setattr(dummy, "exited_settings", True)
        dummy._handle_ack = host_app.App._handle_ack.__get__(dummy)
        return dummy

    def test_enter_settings_enables_controls(self):
        dummy = self.make_sink()
        dummy._handle_ack(self.ack(CMD_ENTER_SETTINGS))
        self.assertTrue(dummy.settings_mode)

    def test_exit_settings_disables_controls(self):
        dummy = self.make_sink()
        dummy.settings_mode = True
        dummy._handle_ack(self.ack(CMD_EXIT_SETTINGS, detail=1))
        self.assertFalse(dummy.settings_mode)
        self.assertIn("参数已变更", dummy.settings_text.value)

    def test_deferred_fusion_mode_leaves_settings_mode(self):
        dummy = self.make_sink(immediate=False)
        dummy.settings_mode = True
        dummy.pending_exit_after_mode = True
        dummy._handle_ack(self.ack(CMD_SET_FUSION_MODE, detail=MODE_6AXIS))
        self.assertTrue(dummy.pending_restart)
        self.assertTrue(dummy.exited_settings)
        self.assertIn("六轴", dummy.settings_text.value)

    def test_failed_ack_clears_pending_state(self):
        dummy = self.make_sink()
        dummy.pending_restart = True
        dummy.pending_exit_after_mode = True
        dummy._handle_ack(self.ack(CMD_SET_FUSION_MODE, ACK_EXEC_FAILED, 0x0602))
        self.assertFalse(dummy.pending_restart)
        self.assertFalse(dummy.pending_exit_after_mode)
        self.assertIn("设置模式", dummy.settings_text.value)

    def test_can_node_id_is_reflected_in_entry(self):
        dummy = self.make_sink()
        dummy.settings_mode = True
        dummy._handle_ack(self.ack(CMD_SET_CAN_NODE_ID, detail=0x123))
        self.assertEqual(dummy.can_id.value, "0x123")


class ProtocolResyncTests(unittest.TestCase):
    def test_mixed_decoder_skips_incomplete_false_sync(self):
        frame = pack_command(CMD_PING, 12)
        false_prefix = b"\xAA\x55\x00\x3F\x00"
        decoder = MixedStreamDecoder()
        items = decoder.feed(false_prefix + frame)
        self.assertEqual(items, [("binary", CMD_PING, 12, b"")])

    def test_split_ack_and_float_frames(self):
        stream = (struct.pack("<fff", 7.0, 8.0, 9.0) + JustFloatDecoder.TAIL +
                  pack_command(CMD_PING, 13))
        decoder = MixedStreamDecoder()
        items = []
        for index in range(0, len(stream), 3):
            items.extend(decoder.feed(stream[index:index + 3]))
        self.assertIn(("justfloat", (7.0, 8.0, 9.0)), items)
        self.assertIn(("binary", CMD_PING, 13, b""), items)

    def test_six_channel_stream_keeps_channel_lock(self):
        frame = struct.pack("<ffffff", 1.0, 2.0, 3.0, 4.0, 5.0, 6.0) + JustFloatDecoder.TAIL
        decoder = MixedStreamDecoder()
        items = []
        for index in range(0, len(frame) * 2, 5):
            items.extend(decoder.feed((frame * 2)[index:index + 5]))
        self.assertEqual([item[1] for item in items],
                         [(1.0, 2.0, 3.0, 4.0, 5.0, 6.0)] * 2)

    def test_stats_separate_crc_errors_and_resync_bytes(self):
        stats = StreamStats()
        corrupted = bytearray(pack_command(CMD_PING, 14))
        corrupted[-1] ^= 0xFF
        buffer = bytearray(b"\x5A\x5B" + corrupted + pack_command(CMD_PING, 15))
        self.assertEqual(unpack_binary(buffer, stats), [(CMD_PING, 15, b"")])
        self.assertEqual(stats.frames, 1)
        self.assertEqual(stats.crc_errors, 1)
        self.assertGreaterEqual(stats.resync_bytes, 2)

    def test_invalid_channel_count_is_rejected(self):
        with self.assertRaises(ValueError):
            JustFloatDecoder(5)
        with self.assertRaises(ValueError):
            MixedStreamDecoder(2)


class DecodeTests(unittest.TestCase):
    def test_compact_frame_reports_attitude(self):
        payload = struct.pack("<hhhhBBH", 120, -450, 900, -31, 0x08, 0, 42)
        data = decode_binary(MSG_COMPACT, payload)
        self.assertEqual(data["kind"], "attitude")
        self.assertAlmostEqual(data["roll"], 1.2)
        self.assertAlmostEqual(data["pitch"], -4.5)
        self.assertAlmostEqual(data["yaw"], 9.0)
        self.assertAlmostEqual(data["gz"], -3.1)
        self.assertEqual(data["flags_text"], "标定完成")

    def test_quaternion_and_imu_frames(self):
        quaternion = decode_binary(MSG_QUATERNION, struct.pack("<ffffH", 1.0, 0.0, 0.0, 0.0, 5))
        self.assertEqual(quaternion["kind"], "quaternion")
        self.assertEqual(quaternion["qw"], 1.0)
        imu = decode_binary(MSG_IMU_RAW,
                            struct.pack("<ffffffhH", 1, 2, 3, 4, 5, 6, 2550, 7))
        self.assertEqual(imu["kind"], "imu")
        self.assertEqual(imu["az"], 6.0)
        self.assertAlmostEqual(imu["temperature_c"], 25.5)

    def test_unknown_message_is_preserved(self):
        data = decode_binary(0x7A, b"\x01\x02")
        self.assertEqual(data["kind"], "binary")
        self.assertEqual(data["payload"], b"\x01\x02")

    def test_ack_descriptions_cover_firmware_detail_codes(self):
        self.assertIn("成功", describe_ack(CMD_PING, ACK_SUCCESS, 0))
        self.assertIn("APP_ACC_CAL_ENABLE", describe_ack(0x1C, ACK_EXEC_FAILED, 0x0600))
        self.assertIn("陀螺校准", describe_ack(0x1B, ACK_EXEC_FAILED, 0x0601))
        self.assertIn("执行失败", describe_ack(CMD_ENTER_SETTINGS, ACK_EXEC_FAILED, 0))


class CommandEncodingTests(unittest.TestCase):
    def test_can_node_id_is_little_endian(self):
        frame = pack_command(CMD_SET_CAN_NODE_ID, 1, (0x123).to_bytes(2, "little"))
        self.assertEqual(frame[-4:-2], b"\x23\x01")


if __name__ == "__main__":
    unittest.main(verbosity=2)
