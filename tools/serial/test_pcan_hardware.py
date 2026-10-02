"""Verify the real CAN wire through PCAN; restore all temporary registers.

Does not write Flash, reset the board, or change the CAN bitrate.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import time

import can


REGISTERS = {'period_ms': 0x0A, 'active': 0x0B, 'baud': 0x0C,
             'node_id': 0x0D, 'master_id': 0x0E, 'mask': 0x0F}


class Board:
    def __init__(self, bus, node_id, master_id):
        self.bus, self.node_id, self.master_id = bus, node_id, master_id

    def drain(self):
        for _ in range(4000):
            if self.bus.recv(0) is None:
                break

    def exchange(self, data, match, arbitration_id=None, timeout=1):
        self.drain()
        self.bus.send(can.Message(arbitration_id=self.node_id if arbitration_id is None else arbitration_id,
                                  is_extended_id=False, data=data), timeout=.5)
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            message = self.bus.recv(min(.05, max(0, until - time.monotonic())))
            if message and message.arbitration_id == self.master_id and not message.is_extended_id and message.dlc == 8 and match(message.data):
                return bytes(message.data)
        raise TimeoutError(f'No CAN reply: {bytes(data).hex()}')

    def register(self, name, value=None, status=0):
        rid = REGISTERS[name]
        data = bytes([0xCC, rid, int(value is not None), 0xDD]) + struct.pack('<I', value or 0)
        reply = self.exchange(data, lambda d: d[:3] == bytes([0xCC, rid, 0xDD]))
        assert reply[3] == status, (name, value, reply.hex())
        got = struct.unpack_from('<I', reply, 4)[0]
        if value is not None and status == 0:
            assert got == value, (name, value, got)
            if name == 'node_id':
                self.node_id = value
            elif name == 'master_id':
                self.master_id = value
        return got

    def config(self):
        return {name: self.register(name) for name in REGISTERS}

    def sensor(self, group, fast=False):
        data = struct.pack('<HBB', self.node_id, group, 0xCC) if fast else bytes([0xCC, group, 0, 0xDD, 0, 0, 0, 0])
        raw = self.exchange(data, lambda d: d[0] == group,
                            arbitration_id=self.master_id if fast else self.node_id)
        if group == 4:
            w = raw[1] << 6 | raw[2] >> 2
            x = (raw[2] & 3) << 12 | raw[3] << 4 | raw[4] >> 4
            y = (raw[4] & 15) << 10 | raw[5] << 2 | raw[6] >> 6
            z = (raw[6] & 63) << 8 | raw[7]
            decoded = [u * 2 / 16383 - 1 for u in [w, x, y, z]]
            assert abs(math.sqrt(sum(x*x for x in decoded)) - 1) < .01, decoded
        else:
            ranges = {1: [235.2]*3, 2: [34.88]*3, 3: [90, 180, 180]}[group]
            decoded = [u * 2 * r / 65535 - r for u, r in zip(struct.unpack_from('<HHH', raw, 2), ranges)]
        return {'group': group, 'fast': fast, 'raw': raw.hex(), 'decoded': decoded}

    def capture(self, seconds):
        self.drain()
        until = time.monotonic() + seconds
        counts = Counter()
        invalid = []
        while time.monotonic() < until:
            m = self.bus.recv(min(.02, max(0, until - time.monotonic())))
            if m and m.arbitration_id == self.node_id:
                if m.is_extended_id or m.is_remote_frame or m.is_error_frame or m.dlc != 8 or m.data[0] not in range(1, 5):
                    invalid.append(str(m))
                else:
                    counts[m.data[0]] += 1
        assert not invalid, invalid[:5]
        return dict(sorted(counts.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-hardware', required=True, action='store_true')
    parser.add_argument('--channel', default='PCAN_USBBUS1')
    parser.add_argument('--bitrate', default=1000000, type=int)
    parser.add_argument('--node-id', default=1, type=lambda s: int(s, 0))
    parser.add_argument('--master-id', default=0x6FF, type=lambda s: int(s, 0))
    parser.add_argument('--result', default='artifacts/web-host/pcan-hardware-20261001.json')
    args = parser.parse_args()
    report = {'time_utc': datetime.now(timezone.utc).isoformat(), 'channel': args.channel,
              'bitrate': args.bitrate, 'physical_can_bus_tested': True,
              'flash_written': False, 'bitrate_changed': False, 'tests': []}
    initial = None
    bus = can.Bus(interface='pcan', channel=args.channel, bitrate=args.bitrate)
    board = Board(bus, args.node_id, args.master_id)
    try:
        initial = board.config()
        report['initial'] = initial
        report['initial_active_capture'] = board.capture(1)
        print('Initial CAN:', initial, flush=True)
        board.register('active', 0)
        assert not board.capture(.08), 'Request mode still emits telemetry'
        report['sensor_replies'] = [board.sensor(g, fast) for fast in [False, True] for g in range(1, 5)]
        report['tests'].append('All four sensor groups, standard and fast requests; quaternion norm')
        board.register('period_ms', 20)
        for mask in range(16):
            board.register('active', 0)
            board.register('mask', mask)
            board.register('active', 1)
            counts = board.capture(.18)
            expected = {g for g in range(1, 5) if mask & (1 << (g-1))}
            assert set(counts) == expected and all(v >= 3 for v in counts.values()), (mask, counts)
            report['tests'].append({'active_mask': mask, 'period_ms': 20, 'counts': counts})
        board.register('active', 0)
        before = board.config()
        for name, value in [('period_ms', 0), ('period_ms', 10001), ('active', 2),
                            ('baud', 8), ('node_id', 2048), ('master_id', 2048), ('mask', 16)]:
            board.register(name, value, status=2)
            assert board.config() == before
        report['tests'].append('Seven invalid writes rejected without changing registers')
        board.register('node_id', 0x123)
        board.register('master_id', 0x345)
        assert board.config()['node_id'] == 0x123 and board.config()['master_id'] == 0x345
        report['changed_id_reply'] = board.sensor(3, True)
        report['tests'].append('Temporary request/reply ID changes and fast addressed read')
        assert bus.status() == 0, bus.status()
        report['passed'] = True
    except Exception as error:
        report['passed'] = False
        report['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        try:
            if initial is not None:
                board.register('active', 0)
                board.register('period_ms', max(initial['period_ms'], 100))
                board.register('mask', initial['mask'])
                board.register('node_id', initial['node_id'])
                board.register('master_id', initial['master_id'])
                board.register('period_ms', initial['period_ms'])
                board.register('active', initial['active'])
                report['final'] = board.config()
                report['restored'] = report['final'] == initial
                assert report['restored'], report
                report['final_bus_status'] = bus.status()
                print('Original CAN configuration restored', flush=True)
        except Exception as error:
            report['restored'] = False
            report['restore_error'] = f'{type(error).__name__}: {error}'
            report['passed'] = False
            raise
        finally:
            bus.shutdown()
            out = Path(args.result)
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('PASS: real PCAN receive/transmit, 16 masks, 8 data requests, invalid writes, ID changes', flush=True)


if __name__ == '__main__':
    main()
