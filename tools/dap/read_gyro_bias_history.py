"""Read gyro history through MicroLink without reset, halt, or Flash programming."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct
import zlib
from datetime import datetime, timedelta, timezone

from pyocd.core.helpers import ConnectHelper
from dap_host_upgrade import release_target

ROOT = Path(__file__).resolve().parents[2]
ADDRESSES = (0x0803E800, 0x0803F800)


def decode_slot(address, raw):
    magic, version, sequence, count, next_index = struct.unpack_from('<5I', raw)
    slot = dict(address=f'0x{address:08X}', magic=f'0x{magic:08X}', version=version,
                sequence=sequence, count=count, next_index=next_index,
                erased=magic == 0xFFFFFFFF, valid=False,
                sha256=hashlib.sha256(raw).hexdigest())
    if magic != 0x42494153 or version not in (2, 3):
        return slot
    capacity = 50 if version == 3 else 15
    crc_offset = 20 + capacity * 16
    expected = struct.unpack_from('<I', raw, crc_offset)[0]
    calculated = (zlib.crc32(raw[:crc_offset]) ^ 0xFFFFFFFF) & 0xFFFFFFFF
    slot.update(capacity=capacity, crc_stored=f'0x{expected:08X}',
                crc_calculated=f'0x{calculated:08X}', crc_valid=expected == calculated)
    if count > capacity or next_index >= capacity or expected != calculated:
        return slot
    entries = []
    for logical in range(count):
        physical = ((next_index if count == capacity else 0) + logical) % capacity
        bias = struct.unpack_from('<3f', raw, 20 + physical * 12)
        temperature = struct.unpack_from('<f', raw, 20 + capacity * 12 + physical * 4)[0]
        if not all(math.isfinite(v) for v in (*bias, temperature)):
            return slot
        entries.append(dict(index_oldest_first=logical + 1, physical_index=physical,
                            temperature_c=temperature, bias_rad_s=list(bias),
                            bias_deg_s=[v * 180.0 / math.pi for v in bias],
                            bias_float32_bits=[f'0x{v:08X}' for v in
                                               struct.unpack_from('<3I', raw, 20 + physical * 12)]))
    slot.update(valid=True, entries=entries)
    return slot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe-uid', default='04CF952C7A94199D')
    parser.add_argument('--frequency', type=int, default=2000000)
    args = parser.parse_args()
    stamp = datetime.now(timezone(timedelta(hours=8)))
    output = ROOT / 'artifacts/diagnostics' / f'gyro-bias-history-{stamp:%Y%m%d-%H%M%S}'
    if output.exists():
        raise RuntimeError('Evidence directory already exists')
    session = ConnectHelper.session_with_chosen_probe(unique_id=args.probe_uid,
        target_override='cortex_m', options={'connect_mode': 'attach', 'frequency': args.frequency,
            'resume_on_disconnect': False, 'vector_catch': ''})
    if session is None:
        raise RuntimeError('Requested MicroLink unavailable')
    blobs = []
    with session:
        target = session.target
        try:
            ident = target.read32(0xE0042000)
            if ident != 0x700A3253:
                raise RuntimeError(f'Unexpected device: 0x{ident:08X}')
            state_before = target.get_state().name
            if state_before != 'RUNNING':
                raise RuntimeError(f'MCU is {state_before}; expected running device')
            for attempt in range(3):
                first = [bytes(target.read_memory_block8(address, 2048)) for address in ADDRESSES]
                second = [bytes(target.read_memory_block8(address, 2048)) for address in ADDRESSES]
                if first == second:
                    blobs = first
                    break
            if not blobs:
                raise RuntimeError('History changed during all three snapshot attempts')
            state_after = target.get_state().name
        finally:
            # Repository's known detach procedure preserves the application's DWT timer.
            # This touches only debug control, never configuration or Flash.
            release_target(target)
    slots = [decode_slot(address, raw) for address, raw in zip(ADDRESSES, blobs)]
    valid = [slot for slot in slots if slot['valid']]
    if not valid:
        raise RuntimeError(f'No valid history: {slots}')
    selected = valid[0]
    if len(valid) == 2:
        delta = (valid[0]['sequence'] - valid[1]['sequence']) & 0xFFFFFFFF
        selected = valid[0] if 0 < delta < 0x80000000 else valid[1]
    entries = selected['entries']
    summary = {}
    if entries:
        for axis, name in enumerate('XYZ'):
            values = [entry['bias_deg_s'][axis] for entry in entries]
            summary[name] = dict(mean_deg_s=statistics.mean(values), min_deg_s=min(values),
                max_deg_s=max(values), range_deg_s=max(values)-min(values),
                sample_std_deg_s=statistics.stdev(values) if len(values) > 1 else None)
    report = dict(read_time=stamp.isoformat(), probe_uid=args.probe_uid, swd_hz=args.frequency,
        device_id=f'0x{ident:08X}', cpu_state_before=state_before, cpu_state_after=state_after,
        double_read_identical=True, slots=[{k:v for k,v in slot.items() if k != 'entries'} for slot in slots],
        selected_address=selected['address'], sequence=selected['sequence'], version=selected['version'],
        count=len(entries), entries=entries, statistics_deg_s=summary,
        note='Flash stores rad/s. No per-entry timestamp, duration, or collection method is stored; records are not proven to all be 2-second startup captures.')
    output.mkdir(parents=True)
    for address, raw in zip(ADDRESSES, blobs):
        (output / f'slot-{address:08X}.bin').write_bytes(raw)
    (output / 'history.json').write_text(json.dumps(report, indent=2, allow_nan=False), encoding='utf-8')
    headers = ['index_oldest_first','physical_index','temperature_c','x_rad_s','y_rad_s','z_rad_s',
               'x_deg_s','y_deg_s','z_deg_s','x_float32_bits','y_float32_bits','z_float32_bits']
    with (output / 'history.csv').open('w', newline='', encoding='utf-8-sig') as stream:
        writer = csv.writer(stream)
        writer.writerow(headers)
        for entry in entries:
            writer.writerow([entry['index_oldest_first'], entry['physical_index'], repr(entry['temperature_c']),
                *[repr(v) for v in entry['bias_rad_s']], *[repr(v) for v in entry['bias_deg_s']],
                *entry['bias_float32_bits']])
    markdown = [f'# 启动零偏历史实板读取\n\n读取时间：{stamp.isoformat()}。MicroLink：{args.probe_uid}。',
        f'\n选中槽位：{selected["address"]}；格式 v{selected["version"]}；序号 {selected["sequence"]}；共 {len(entries)} 条；CRC 校验通过。',
        '\n顺序从旧到新。原始单位 rad/s；以下表格为换算后的 °/s。小数位仅展示数值，不代表测量精度。',
        '\n| 序号 | 温度 °C | X °/s | Y °/s | Z °/s |',
        '| --- | ---: | ---: | ---: | ---: |']
    for entry in entries:
        markdown.append(f'| {entry["index_oldest_first"]} | {entry["temperature_c"]:.6f} | ' +
                        ' | '.join(f'{v:+.9f}' for v in entry['bias_deg_s']) + ' |')
    markdown.extend(['\n原始 rad/s、完整浮点数和 float32 位模式见同目录 CSV / JSON。',
        '\n每条历史没有保存时间戳、采集时长或采集方式，因此不能确认全部来自 2 秒初始化。',
        '\n统计值为这些历史估计值的分布，不能当作零偏校准误差或残余漂移。'])
    (output / 'history.md').write_text('\n'.join(markdown) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2, allow_nan=False))
    print(f'EVIDENCE: {output}')


if __name__ == '__main__':
    main()
