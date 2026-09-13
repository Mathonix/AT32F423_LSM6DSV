"""Non-halting stationary capture; persist each sample and summarize measured drift."""
import argparse
import csv
import json
import struct
import time
from pathlib import Path
from datetime import datetime
from pyocd.core.helpers import ConnectHelper
from dap_vqf_tune import DEFAULT_ELF, elf_symbol_address

NAMES = ('magic seq millis fusion_hz skip_n rest_detected roll pitch yaw gx gy gz ax ay az bias_x bias_y bias_z rest_time tau_acc mx my mz mag_norm tau_mag mag_err mag_addr mag_updates mag_ready mag_disturbed temperature_c gyr_lpf_z corrected_z').split()
FMT = '<6I19fi4I3f'
SIZE = struct.calcsize(FMT)
MAGIC = 0x56514654

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=1800)
    parser.add_argument('--probe')
    parser.add_argument('--frequency',type=int,default=2000000)
    args = parser.parse_args()
    if args.duration < 1:
        parser.error("duration must be positive")
    folder = Path(__file__).resolve().parents[1] / 'build' / 'logs'
    folder.mkdir(parents=True, exist_ok=True)
    stem = folder / (f'stationary_{args.duration}s_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
    session = ConnectHelper.session_with_chosen_probe(target_override='cortex_m', unique_id=args.probe, options={'connect_mode': 'attach', 'frequency': args.frequency, 'resume_on_disconnect': False})
    if session is None:
        raise RuntimeError('No DAPLink probe')
    rows = []
    with session:
        target = session.target
        if target.get_state().name != 'RUNNING':
            raise RuntimeError('MCU is not running; capture will not reset/resume it')
        addr = elf_symbol_address(DEFAULT_ELF, 'vqf_tune_live')
        if target.read32(addr) != MAGIC:
            raise RuntimeError('ELF/firmware telemetry mismatch')
        print(f'Non-halting capture at 0x{addr:08X}; CSV: {stem}.csv', flush=True)
        start = time.monotonic()
        with stem.with_suffix('.csv').open('w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=['host_s', 'retries'] + NAMES)
            writer.writeheader()
            for i in range(args.duration + 1):
                time.sleep(max(0, start + i - time.monotonic()))
                for retry in range(200):
                    before = target.read32(addr + 4)
                    if before & 1:
                        continue
                    values = struct.unpack(FMT, bytes(target.read_memory_block8(addr, SIZE)))
                    after = target.read32(addr + 4)
                    if before == values[1] == after and not after & 1 and values[0] == MAGIC:
                        break
                else:
                    raise RuntimeError('No coherent telemetry snapshot')
                d = dict(zip(NAMES, values))
                d.update(host_s=time.monotonic() - start, retries=retry)
                writer.writerow(d)
                f.flush()
                rows.append(d)
                if i % 60 == 0:
                    print(f'{i:4d}s yaw={d["yaw"]:.7f} bias_z={d["bias_z"]:.7f} dps rest={d["rest_detected"]} hz={d["fusion_hz"]} skip={d["skip_n"]}', flush=True)
    elapsed = rows[-1]['host_s'] - rows[0]['host_s']
    delta = rows[-1]['yaw'] - rows[0]['yaw']
    summary = {'samples': len(rows), 'elapsed_s': elapsed, 'yaw_start_deg': rows[0]['yaw'], 'yaw_end_deg': rows[-1]['yaw'], 'yaw_delta_deg': delta, 'endpoint_drift_deg_min': delta / elapsed * 60, 'yaw_min_deg': min(r['yaw'] for r in rows), 'yaw_max_deg': max(r['yaw'] for r in rows), 'bias_z_start_dps': rows[0]['bias_z'], 'bias_z_end_dps': rows[-1]['bias_z'], 'rest_fraction': sum(r['rest_detected'] != 0 for r in rows)/len(rows), 'skip_delta': rows[-1]['skip_n']-rows[0]['skip_n']}
    stem.with_suffix('.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2), flush=True)

if __name__ == '__main__':
    main()
