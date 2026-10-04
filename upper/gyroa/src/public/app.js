const $ = (id) => document.getElementById(id);

// ==== GYRO-CORE BEGIN ====
// 纯逻辑段（不访问 DOM）。test/ 直接从本文件抽取这一段做单元测试，测的就是线上同一份代码。
// 与固件 Mathonix/AT32F423_LSM6DSV：inc/telemetry/protocol.h、src/drivers/protocol.c、src/app/main.c 对应。
const SYNC_A = 0xaa;
const SYNC_B = 0x55;
const JUST_TAIL = [0x00, 0x00, 0x80, 0x7f];
const MAX_PAYLOAD = 64;            // AHRS_MAX_PAYLOAD_LEN
const DEFAULT_FUSION_HZ = 2000;    // app_config.h APP_FUSION_HZ（未收到 SYSINFO 前的默认值）
const SERIAL_BUFFER_SIZE = 65536;  // port.open bufferSize（默认 255 字节，2 Mbaud 下易溢出丢字节）

// 与固件 inc/telemetry/protocol.h 一一对应
const MSG = {
  ATTITUDE: 0x01,   // roll, pitch, yaw, flags, reserved, ts
  QUAT: 0x02,       // qw, qx, qy, qz, ts
  IMU: 0x03,        // gyr xyz, acc xyz, temp x100, ts
  COMPACT: 0x04,    // roll/pitch/yaw x100, gz x10, flags, reserved, ts
  SYSINFO: 0x05,    // fusion_hz, out_hz, skip_n, temp x100, stream_mode, can_ok, reserved
  SELECTED: 0x06,
  CONFIG: 0x07,
  CAN_CONFIG: 0x08,
  MOTION_BIAS: 0x09,   // bias1：VQF 运动零偏状态 48 B（只读，0x30 查询）
  ACC_CAL: 0x0a,
  ZARU_LIMITS: 0x0f,   // 零角速保持阈值 40 B（zaru-limits.js 解码）
  VQF_STATUS: 0x0d,    // vqfinit1：静置初始化状态 28 B（vqf-init.js 解码）
  VQF_SETTINGS: 0x0e,  // vqfinit1：VQF 静置参数 52 B（vqf-init.js 解码）
  FILTER_CONFIG: 0x0b, // 姿态稳定性 16 B（filter-profile.js 解码）
  FILTER_DIAG: 0x0c,   // 滤波诊断 60 B（filter-profile.js 解码）
  DEVICE_MODEL: 0x36, // four ASCII bytes, no terminator
  FW_VERSION: 0x32,    // fwver1：应用固件版本 16 B（format=1, len, char[14] 以 0 结尾）
  BIAS_HISTORY: 0x34,  // biashist1：启动零偏历史 60 B（bias-history.js 解码，只读）
  ACK: 0x90,        // cmd_id, status, detail
};
// #pragma pack(1) 结构体 sizeof：payload 长度必须严格相等，否则丢弃
const PAYLOAD_LEN = {
  [MSG.ATTITUDE]: 16,  // ahrs_payload_attitude_t    4*3 + 1 + 1 + 2
  [MSG.QUAT]: 18,      // ahrs_payload_quaternion_t  4*4 + 2
  [MSG.IMU]: 28,       // ahrs_payload_imu_t         4*6 + 2 + 2
  [MSG.COMPACT]: 12,   // ahrs_payload_compact_t     2*4 + 1 + 1 + 2
  [MSG.SYSINFO]: 16,   // ahrs_payload_system_info_t 4 + 4 + 2 + 2 + 1 + 1 + 2
  [MSG.ACK]: 4,        // ahrs_payload_ack_t         1 + 1 + 2
  [MSG.CONFIG]: 18,
  [MSG.CAN_CONFIG]: 24,
  [MSG.ACC_CAL]: 60,
  [MSG.DEVICE_MODEL]: 4,
  [MSG.FW_VERSION]: 16, // fwver1：format + len + char[14]
  [MSG.MOTION_BIAS]: 48, // 必须正好 48 B，否则整帧丢弃
};
const CMD = {
  PING: 0x10,
  ZERO: 0x11,
  CAL: 0x12,
  STREAM: 0x13,
  QUERY: 0x14,
  RESET: 0x15,
  ENTER: 0x17,
  EXIT: 0x18,
  MODE: 0x19,
  CAN_ID: 0x1a,
  GYRO_60: 0x1b,
  ACC_6FACE: 0x1c,
  OUTPUT_HZ: 0x1d,
  STARTUP: 0x1e,
  QUERY_CONFIG: 0x1f,
  OUTPUT_CONFIG: 0x20,
  QUERY_CAN: 0x21,
  CAN_CONFIG: 0x22,
  QUERY_DEVICE_MODEL: 0x35,
  QUERY_FW_VERSION: 0x23,  // fwver1：应用固件版本查询（空 payload，成功只回 0x32；旧固件 ACK 0x01）
  QUERY_ACC_CAL: 0x24,
  CANCEL_ACC_CAL: 0x25,
  QUERY_FILTER: 0x26,      // 姿态稳定性查询（空 payload）
  SET_FILTER: 0x27,        // profile, persist（需设置模式）
  QUERY_FILTER_DIAG: 0x28, // 滤波诊断（可选）
  QUERY_ZARU: 0x2e,        // 零角速保持阈值查询（空 payload，成功只回 0x0F）
  SET_ZARU: 0x2f,          // 零角速保持阈值写入 20 B（需设置模式；ACK 后回 0x0F）
  QUERY_MOTION_BIAS: 0x30, // bias1：运动零偏状态查询（空 payload，成功只回 0x09；旧固件 ACK 0x01）
  QUERY_BIAS_HISTORY: 0x33, // biashist1：启动零偏历史（空 / u16 offset，成功只回 0x34；旧固件 ACK 0x01，offset>50 ACK 0x02）
  QUERY_VQF_STATUS: 0x29,  // vqfinit1：静置初始化状态查询 → 0x0D（不回 ACK）
  START_VQF: 0x2a,         // vqfinit1：开始静置初始化（需设置模式）→ ACK + 0x0D
  CANCEL_VQF: 0x2b,        // vqfinit1：取消（不需设置模式）→ ACK + 0x0D
  QUERY_VQF: 0x2c,         // vqfinit1：VQF 静置参数查询 → 0x0E（不回 ACK）
  RESTORE_VQF: 0x2d,       // vqfinit1：恢复默认 VQF 参数（需设置模式）→ ACK + 0x0E
};
const CAN_RATES = [1000000, 500000, 400000, 250000, 200000, 100000, 50000, 25000];
const GYRO_RANGES = [125,250,500,1000,2000,4000];
function canPeriodFromHz(value) {
  const hz = Number(value);
  return Number.isFinite(hz) && hz >= .1 && hz <= 1000 ? Math.round(1000 / hz) : null;
}
function validCanConfig(c) {
  return ['nodeId', 'masterId', 'periodMs', 'baud', 'active', 'mask', 'reserved'].every((k) => Number.isInteger(c[k])) &&
    c.nodeId >= 0 && c.nodeId <= 2047 && c.masterId >= 0 && c.masterId <= 2047 &&
    c.periodMs >= 1 && c.periodMs <= 10000 && c.baud >= 0 && c.baud <= 7 &&
    c.active >= 0 && c.active <= 1 && c.mask >= 0 && c.mask <= 15 && c.reserved === 0 &&
    (!c.active || [0, 1, 2, 3].filter((i) => c.mask & (1 << i)).length * 135000 <= CAN_RATES[c.baud] * .8 * c.periodMs);
}
function encodeCanConfig(c, persist) {
  if (!validCanConfig(c)) return null;
  const bytes = new Uint8Array(11); const v = new DataView(bytes.buffer);
  v.setUint16(0, c.nodeId, true); v.setUint16(2, c.masterId, true); v.setUint16(4, c.periodMs, true);
  bytes.set([c.baud, c.active, c.mask, 0, persist ? 1 : 0], 6);
  return bytes;
}
const ACK_STATUS = ['成功', '未知命令', '参数无效', '执行失败'];
const STREAM_MODES = ['VOFA 3通道', '二进制姿态', '二进制紧凑', '二进制IMU', 'VOFA 6通道'];
const FLAGS = {
  REST: 1 << 0,
  MAG_VALID: 1 << 1,
  MAG_DISTURBED: 1 << 2,
  CALIB_DONE: 1 << 3,
  SENSOR_ERROR: 1 << 4,
};

const FIELD_NAMES = ['yaw', 'pitch', 'roll', 'ax', 'ay', 'az', 'gx', 'gy', 'gz'];
const FIELD_LABELS = ['航向 Yaw', '俯仰 Pitch', '横滚 Roll', '加速度 X', '加速度 Y', '加速度 Z', '角速度 X', '角速度 Y', '角速度 Z'];
function accPlacementReference(face, raw) {
  if (!Array.isArray(raw) || raw.length !== 3 || !raw.every(Number.isFinite)) return null;
  const norm = Math.hypot(...raw);
  if (norm < .2) return null;
  const recognized = Number.isInteger(face) && face >= 1 && face <= 6;
  const axis = recognized ? Math.floor((face - 1) / 2) : raw.reduce((best, x, i) => Math.abs(x) > Math.abs(raw[best]) ? i : best, 0);
  const positive = recognized ? (face % 2 === 0) : raw[axis] >= 0;
  const reference = [0, 0, 0]; reference[axis] = positive ? 1 : -1;
  return { norm, axis, positive, recognized, face: recognized ? face : axis * 2 + (positive ? 2 : 1), reference };
}
function selectedCount(mask) { return FIELD_NAMES.filter((_, i) => mask & (1 << i)).length; }
function selectedFields(values, mask) {
  if (!Number.isInteger(mask) || mask < 1 || mask > 0x1ff || values.length !== selectedCount(mask) || !values.every(Number.isFinite)) return null;
  const result = {}; let offset = 0;
  FIELD_NAMES.forEach((name, i) => { if (mask & (1 << i)) result[name] = values[offset++]; });
  if (['yaw', 'pitch', 'roll'].some((name) => name in result && Math.abs(result[name]) > 100000)) return null;
  return result;
}

const crc16 = (bytes, start = 0, end = bytes.length) => {
  let crc = 0xffff;
  for (let index = start; index < end; index += 1) {
    crc ^= bytes[index] << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) ? (((crc << 1) ^ 0x1021) & 0xffff) : ((crc << 1) & 0xffff);
    }
  }
  return crc;
};
// AA 55 + MSG_ID + LEN + SEQ + payload + CRC16(XMODEM, LE)，CRC 覆盖 MSG_ID..payload（protocol_pack_frame）
const buildFrame = (id, seq, payload = []) => {
  const frame = new Uint8Array(7 + payload.length);
  frame[0] = SYNC_A; frame[1] = SYNC_B; frame[2] = id; frame[3] = payload.length; frame[4] = seq & 0xff;
  frame.set(payload, 5);
  const crc = crc16(frame, 2, 5 + payload.length);
  frame[5 + payload.length] = crc & 0xff;
  frame[6 + payload.length] = crc >> 8;
  return frame;
};

// 固件 stream_mode_t → 上位机解析配置：0=VOFA_3CH，1..3=二进制（姿态/紧凑/IMU），4=VOFA_6CH
function streamParserConfig(mode) {
  if (mode === 0) return { parseMode: 'justfloat', justChannels: 3 };
  if (mode === 4) return { parseMode: 'justfloat', justChannels: 6 };
  if (mode === 1 || mode === 2 || mode === 3) return { parseMode: 'binary', justChannels: null };
  return null;
}

// 固件 AHRS_CMD_SET_OUTPUT_HZ：hz != 0 && hz <= APP_FUSION_HZ && APP_FUSION_HZ % hz == 0
function outputHzDivisors(fusionHz, limit = 8) {
  const list = [];
  for (let hz = fusionHz; hz >= 1 && list.length < limit; hz -= 1) if (fusionHz % hz === 0) list.push(hz);
  return list;
}
function validateOutputHz(text, fusionHz) {
  const raw = String(text ?? '').trim();
  if (!/^\d+$/.test(raw)) return { ok: false, error: `输出频率须为正整数（融合率 ${fusionHz} Hz）` };
  const hz = Number(raw);
  if (!Number.isInteger(hz) || hz < 1 || hz > fusionHz || hz > 0xffff) {
    return { ok: false, error: `输出频率须在 1~${fusionHz} Hz 之间` };
  }
  if (fusionHz % hz !== 0) {
    return { ok: false, error: `输出频率须能整除融合率 ${fusionHz} Hz（可选：${outputHzDivisors(fusionHz).join(' / ')} …）` };
  }
  return { ok: true, hz };
}

// 二进制 payload 解码（严格长度）；未知 MSG 返回 unknown，长度不符返回 badLength
function decodePayload(id, payload) {
  if ((id === MSG.FILTER_CONFIG || id === MSG.FILTER_DIAG) && globalThis.GyroFilter) return globalThis.GyroFilter.decode(id, payload);
  if (id === MSG.ZARU_LIMITS && globalThis.GyroZaru) return globalThis.GyroZaru.decode(id, payload);
  if ((id === MSG.VQF_STATUS || id === MSG.VQF_SETTINGS) && globalThis.GyroVqf) return globalThis.GyroVqf.decode(id, payload);
  if (id === MSG.BIAS_HISTORY && globalThis.GyroBiasHist) return globalThis.GyroBiasHist.decode(id, payload);
  if (id === MSG.SELECTED) {
    const view = new DataView(payload.buffer, payload.byteOffset, payload.length);
    const mask = payload.length >= 4 ? view.getUint16(0, true) : 0;
    const expected = 4 + 4 * selectedCount(mask);
    if (!mask || mask > 0x1ff || payload.length !== expected) return { type: 'badLength', id, length: payload.length, expected };
    const values = Array.from({ length: selectedCount(mask) }, (_, i) => view.getFloat32(4 + 4 * i, true));
    return { type: 'selected', mask, values, ts: view.getUint16(2, true) };
  }
  const expected = id === MSG.CONFIG ? ({1:18,2:22,3:28,4:28}[payload[0]] ?? PAYLOAD_LEN[id]) : PAYLOAD_LEN[id];
  if (expected === undefined) return { type: 'unknown', id, length: payload.length };
  if (payload.length !== expected) return { type: 'badLength', id, length: payload.length, expected };
  const view = new DataView(payload.buffer, payload.byteOffset, payload.length);
  const f32 = (offset) => view.getFloat32(offset, true);
  switch (id) {
    case MSG.DEVICE_MODEL:
      if (![...payload].every(byte => byte >= 0x21 && byte <= 0x7e)) return { type: 'unknown', id, length: payload.length };
      return { type: 'deviceModel', text: String.fromCharCode(...payload) };
    case MSG.FW_VERSION: {
      // fwver1：format=1；len 1..13；ASCII 可打印；第 len 个字节后必须是 0 结尾
      const n = payload[1], chars = Array.from(payload.subarray(2, 2 + n));
      if (payload[0] !== 1 || n < 1 || n > 13 || payload[2 + n] !== 0 || !chars.every((c) => c >= 0x20 && c <= 0x7e)) return { type: 'unknown', id, length: payload.length };
      return { type: 'fwVersion', format: 1, text: String.fromCharCode(...chars) };
    }
    case MSG.ACC_CAL: {
      const bias = [24,28,32].map(f32), scale = [36,40,44].map(f32), raw = [48,52,56].map(f32);
      if (payload[0] !== 1 || payload[1] > 4 || payload[2] > 7 || payload[3] > 6 || payload[4] > 63 ||
          payload[5] > 1 || payload[6] > 1 || payload[7] || view.getUint16(8,true) > 1000 ||
          ![...bias,...scale,...raw].every(Number.isFinite) || scale.some(x=>x<=.5||x>=1.5) ||
          bias.some(x=>Math.abs(x)>.15) || raw.some(x=>Math.abs(x)>16)) return { type:'unknown',id,length:payload.length };
      return {type:'accCalibration',status:payload[1],phase:payload[2],face:payload[3],mask:payload[4],
        enabled:!!payload[5],valid:!!payload[6],progress:view.getUint16(8,true),error:view.getUint16(10,true),
        samples:view.getUint32(12,true),elapsed:view.getUint32(16,true),remaining:view.getUint32(20,true),bias,scale,raw};
    }
    case MSG.MOTION_BIAS: {
      // bias1：version 1 才解码；浮点须有限；标志位只取 0/1（其它值 → 无法识别）。成功判定（三个条件）由 motion-bias.js 控制器做
      const fl = [4, 8, 12, 16, 20, 24, 28, 32, 36, 40].map(f32);
      if (payload[0] !== 1 || [1, 2, 3, 44, 45].some((o) => payload[o] > 1) || !fl.every(Number.isFinite)) return { type: 'unknown', id, length: payload.length };
      return { type: 'motionBias', version: payload[0], motionBiasEnabled: payload[1], restBiasEnabled: payload[2], restDetected: !!payload[3],
        biasSigmaMotion: fl[0], biasVerticalForgettingFactor: fl[1], biasForgettingTime: fl[2], biasClip: fl[3], biasSigmaRest: fl[4],
        tauAcc: fl[5], bias: [fl[6], fl[7], fl[8]], residualNorm: fl[9], zaruHold: !!payload[44], zaruEnabled: !!payload[45], reserved: view.getUint16(46, true) };
    }
    case MSG.CAN_CONFIG: {
      const decode = (offset) => ({ nodeId: view.getUint16(offset, true), masterId: view.getUint16(offset + 2, true),
        periodMs: view.getUint16(offset + 4, true), baud: payload[offset + 6], active: payload[offset + 7], mask: payload[offset + 8], reserved: payload[offset + 9] });
      const active = decode(2), saved = decode(12);
      if (payload[0] !== 1 || payload[1] > 1 || payload[22] > 1 || payload[23] || !validCanConfig(active) || !validCanConfig(saved)) return { type: 'unknown', id, length: payload.length };
      return { type: 'canConfig', ready: !!payload[1], active, saved, busOff: !!payload[22] };
    }
    case MSG.CONFIG:
      if (![1, 2, 3, 4].includes(payload[0]) || payload[1] > 1 || payload[2] > 2 || payload[3] > 2 || payload[4] > 1 || payload[5] > 1 ||
          (payload[0] >= 2 && [18, 20].some((offset) => view.getUint16(offset, true) < (payload[0] >= 4 ? 0 : 100) || view.getUint16(offset, true) > 60000)) ||
          (payload[0] >= 3 && (!GYRO_RANGES.includes(view.getUint16(22,true)) || !GYRO_RANGES.includes(view.getUint16(24,true)) ||
            !validateOutputHz(view.getUint16(26,true),DEFAULT_FUSION_HZ).ok))) return { type: 'unknown', id, length: payload.length };
      return {
        type: 'config', version: payload[0], source: payload[1], activeMode: payload[2], savedMode: payload[3],
        activeFast: payload[4], savedFast: payload[5], capabilities: payload[6], outHz: view.getUint16(8, true),
        outputs: [10, 14].map((offset) => ({ format: payload[offset], legacyMode: payload[offset + 1], mask: view.getUint16(offset + 2, true) })),
        activeInitMs: payload[0] >= 2 ? view.getUint16(18, true) : null,
        savedInitMs: payload[0] >= 2 ? view.getUint16(20, true) : null,
        activeRangeDps: payload[0] >= 3 ? view.getUint16(22,true) : null,
        savedRangeDps: payload[0] >= 3 ? view.getUint16(24,true) : null,
        savedOutputHz: payload[0] >= 3 ? view.getUint16(26,true) : null,
      };
    case MSG.ACK:
      return { type: 'ack', cmd: payload[0], status: payload[1], detail: view.getUint16(2, true) };
    case MSG.SYSINFO:
      return {
        type: 'sysinfo', fusionHz: view.getUint32(0, true), outHz: view.getUint32(4, true), skipN: view.getUint16(8, true),
        temp: view.getInt16(10, true) / 100, streamMode: payload[12], canOk: payload[13],
      };
    case MSG.ATTITUDE:
      return { type: 'attitude', roll: f32(0), pitch: f32(4), yaw: f32(8), flags: payload[12], ts: view.getUint16(14, true) };
    case MSG.COMPACT:
      return {
        type: 'compact', roll: view.getInt16(0, true) / 100, pitch: view.getInt16(2, true) / 100, yaw: view.getInt16(4, true) / 100,
        gz: view.getInt16(6, true) / 10, flags: payload[8], ts: view.getUint16(10, true),
      };
    case MSG.IMU:
      return {
        type: 'imu', gx: f32(0), gy: f32(4), gz: f32(8), ax: f32(12), ay: f32(16), az: f32(20),
        temp: view.getInt16(24, true) / 100, ts: view.getUint16(26, true),
      };
    case MSG.QUAT:
      return { type: 'quat', qw: f32(0), qx: f32(4), qy: f32(8), qz: f32(12), ts: view.getUint16(16, true) };
    default:
      return { type: 'unknown', id, length: payload.length };
  }
}

// ACK detail 解码（main.c protocol_frame_received / acc_cal_finish）
function ackDetailText(cmd, status, detail) {
  if (status === 3 && detail === 0x0703) return '静置初始化进行中，设备不执行该命令（0x0703）';
  if (cmd === CMD.START_VQF && status === 3 && detail === 0x0701) return '已经在静置采集，或六面校准还在进行（0x0701）';
  if (cmd === CMD.START_VQF && status === 3 && detail === 0x0702) return '主循环还没开始（上电采集零偏期间）（0x0702）';
  if (cmd === CMD.ACC_6FACE) {
    const table = {
      0x0000: '六面标定完成并已保存',
      0x0100: '六面标定已开始：任意顺序放置六个面，每面静止约 1.5 秒',
      0x0600: '固件未启用六面加速度计标定（APP_ACC_CAL_ENABLE=0）',
      0x0602: '须先进入设置模式（或参数无效）',
      0x0603: '等待下一面超时（60 秒），原校准参数保留',
      0x0604: '六面标定已在进行中',
      0x0605: '采样不足（< 50 个样本）',
      0x0606: '六面拟合结果超出合理范围（零偏或比例），本次未保存，原参数保留',
      0x0607: '校准保存失败，原参数保留',
      0x0608: '已取消标定，原参数保留',
      0x0609: '校准存储空间已满，原参数保留，请先备份并维护校准扇区',
    };
    if (table[detail]) return table[detail];
  }
  return '';
}

// JustFloat 帧有效性（与原线上 validPose 相同的范围检查）；6 通道：Yaw Pitch Roll Gz Az Temp
function validJustValues(values, channels) {
  if (values.length !== channels || !values.every(Number.isFinite)) return false;
  const angleLimit = 100000;
  if (Math.abs(values[0]) > angleLimit || Math.abs(values[1]) > angleLimit || Math.abs(values[2]) > angleLimit) return false;
  return channels === 3 || Math.abs(values[5]) < 2000;
}
function justValuesToFields(values) {
  const fields = { yaw: values[0], pitch: values[1], roll: values[2] };
  if (values.length === 6) { fields.gz = values[3]; fields.az = values[4]; fields.temp = values[5]; }
  return fields;
}

function concatBytes(a, b) {
  if (!a.length) return b.slice();
  const out = new Uint8Array(a.length + b.length);
  out.set(a, 0); out.set(b, a.length);
  return out;
}
function isTailAt(buffer, index) {
  return buffer[index] === JUST_TAIL[0] && buffer[index + 1] === JUST_TAIL[1] && buffer[index + 2] === JUST_TAIL[2] && buffer[index + 3] === JUST_TAIL[3];
}
function findTail(buffer, from) {
  for (let index = from; index <= buffer.length - 4; index += 1) if (isTailAt(buffer, index)) return index;
  return -1;
}

function newParserStats() {
  return { binFrames: 0, crcSkips: 0, badLengthFrames: 0, unknownFrames: 0, justFrames: 0, justInvalid: 0, justResyncs: 0, droppedBytes: 0 };
}

// 串口字节流解析器：AA55 二进制帧（CRC 校验 + 严格长度）与 JustFloat（严格帧长）共用一条字节流。
// parseMode: 'auto' | 'justfloat' → 非二进制字节交给 JustFloat；'binary' → 丢弃非二进制字节。
// ACK 等二进制帧在 JustFloat 模式下也会被解析（切换数据流需要它）。
function createStreamParser(handlers = {}) {
  let parseMode = 'auto';
  let channels = 3;
  let selectedMask = null;
  let rx = new Uint8Array(0);
  let just = new Uint8Array(0);
  let justSynced = false;
  let scanning = false;
  const stats = newParserStats();

  function parseJust() {
    const dataBytes = channels * 4;
    const frameBytes = dataBytes + 4;
    let pos = 0;
    for (;;) {
      if (!justSynced) {
        // 未同步：找到下一个帧尾，丢弃它及之前的全部字节（不从帧尾往前截取数据）
        const tail = findTail(just, pos);
        if (tail < 0) { const keep = Math.max(pos, just.length - 3); stats.droppedBytes += keep - pos; pos = keep; break; }
        stats.droppedBytes += tail + 4 - pos;
        pos = tail + 4;
        justSynced = true;
        continue;
      }
      if (just.length - pos < frameBytes) break;
      if (!isTailAt(just, pos + dataBytes)) {
        // 帧尾不在 N*4 的精确位置：失步 → 重新搜索下一个帧尾
        stats.justResyncs += 1;
        justSynced = false;
        continue;
      }
      const view = new DataView(just.buffer, just.byteOffset + pos, dataBytes);
      const values = [];
      for (let index = 0; index < channels; index += 1) values.push(view.getFloat32(index * 4, true));
      pos += frameBytes;
      if (selectedMask === null ? validJustValues(values, channels) : selectedFields(values, selectedMask) !== null) {
        stats.justFrames += 1;
        if (handlers.onJust) handlers.onJust(values, channels);
      } else {
        stats.justInvalid += 1;
      }
    }
    just = pos ? just.slice(pos) : just;
  }

  function sinkJust(bytes) {
    if (!bytes.length) return;
    if (parseMode === 'binary') { stats.droppedBytes += bytes.length; return; }
    just = concatBytes(just, bytes);
    parseJust();
  }

  function findSync(from) {
    for (let index = from; index < rx.length; index += 1) {
      if (rx[index] === SYNC_A && (index + 1 >= rx.length || rx[index + 1] === SYNC_B)) return index;
    }
    return -1;
  }

  function scan() {
    scanning = true;
    let pos = 0;
    try {
      while (pos < rx.length) {
        const start = findSync(pos);
        if (start < 0) { sinkJust(rx.subarray(pos)); pos = rx.length; break; }
        if (start > pos) { sinkJust(rx.subarray(pos, start)); pos = start; }
        if (rx.length - pos < 5) break;
        const length = rx[pos + 3];
        if (length > MAX_PAYLOAD) { sinkJust(rx.subarray(pos, pos + 1)); pos += 1; continue; }
        const total = 7 + length;
        if (rx.length - pos < total) break;
        const received = rx[pos + 5 + length] | (rx[pos + 6 + length] << 8);
        if (crc16(rx, pos + 2, pos + 5 + length) !== received) {
          // CRC 不符：只让出 1 个字节，避免吞掉 JustFloat 数据中偶然出现的 AA 55
          stats.crcSkips += 1;
          sinkJust(rx.subarray(pos, pos + 1));
          pos += 1;
          continue;
        }
        const id = rx[pos + 2];
        const seq = rx[pos + 4];
        const payload = rx.slice(pos + 5, pos + 5 + length);
        pos += total;
        stats.binFrames += 1;
        const message = decodePayload(id, payload);
        message.seq = seq;
        if (message.type === 'badLength') stats.badLengthFrames += 1;
        else if (message.type === 'unknown') stats.unknownFrames += 1;
        if (handlers.onMessage) handlers.onMessage(message);
      }
    } finally {
      scanning = false;
    }
    rx = pos ? rx.slice(pos) : rx;
  }

  return {
    push(bytes) {
      if (!bytes || !bytes.length) return;
      rx = concatBytes(rx, bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes));
      scan();
    },
    // 复位缓冲：若在帧回调中调用（例如 STREAM ACK），保留该帧之后尚未扫描的字节（它们属于新数据流）
    reset() {
      just = new Uint8Array(0);
      justSynced = false;
      if (!scanning) rx = new Uint8Array(0);
    },
    configure(mode, count, mask = null) {
      parseMode = mode;
      channels = count;
      selectedMask = mask;
    },
    resetStats() { Object.assign(stats, newParserStats()); },
    get stats() { return stats; },
    get parseMode() { return parseMode; },
    get channels() { return channels; },
    get buffered() { return { rx: rx.length, just: just.length, justSynced }; },
  };
}

// 串口写入队列：前一次写失败不会让后续写入永久失败（每次先吞掉前一次的拒绝）
function createWriteQueue(getWriter) {
  let chain = Promise.resolve();
  return {
    write(frame) {
      chain = chain.catch(() => {}).then(() => {
        const writer = getWriter();
        if (!writer) throw new Error('串口未连接');
        return writer.write(frame);
      });
      return chain;
    },
    reset() { chain = Promise.resolve(); },
    settled() { return chain.catch(() => {}); },
  };
}

// ---------- 3D 姿态 ----------
// 固件 vqf_get_euler_deg()：q=(w,x,y,z)，roll=atan2(2(wx+yz),1-2(x²+y²))，pitch=asin(2(wy-zx))，yaw=atan2(2(wz+xy),1-2(y²+z²))
// 即 ZYX 内旋（yaw→pitch→roll）：R = Rz(yaw)·Ry(pitch)·Rx(roll)，机体→世界（VQF 世界系 ENU，Z 向上）
const DEG = Math.PI / 180;
function eulerToMatrix(yawDeg, pitchDeg, rollDeg) {
  const cy = Math.cos(yawDeg * DEG); const sy = Math.sin(yawDeg * DEG);
  const cp = Math.cos(pitchDeg * DEG); const sp = Math.sin(pitchDeg * DEG);
  const cr = Math.cos(rollDeg * DEG); const sr = Math.sin(rollDeg * DEG);
  return [
    [cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr],
    [sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr],
    [-sp, cp * sr, cp * cr],
  ];
}
function quatToMatrix(qw, qx, qy, qz) {
  const n = Math.hypot(qw, qx, qy, qz) || 1;
  const w = qw / n; const x = qx / n; const y = qy / n; const z = qz / n;
  return [
    [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
    [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
    [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
  ];
}
function quatToEuler(qw, qx, qy, qz) {
  const roll = Math.atan2(2 * (qw * qx + qy * qz), 1 - 2 * (qx * qx + qy * qy)) / DEG;
  const pitch = Math.asin(Math.max(-1, Math.min(1, 2 * (qw * qy - qz * qx)))) / DEG;
  const yaw = Math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz)) / DEG;
  return { yaw, pitch, roll };
}
const mulVec = (m, v) => [
  m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
  m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
  m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
];
// 电路板长方体（机体系：X 前、Y 左、Z 上）
const BOARD_HALF = [1.0, 0.62, 0.09];
const BOARD_VERTS = [];
for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) BOARD_VERTS.push([sx * BOARD_HALF[0], sy * BOARD_HALF[1], sz * BOARD_HALF[2]]);
const vi = (sx, sy, sz) => ((sx > 0 ? 4 : 0) + (sy > 0 ? 2 : 0) + (sz > 0 ? 1 : 0));
const BOARD_FACES = [
  { name: 'top', normal: [0, 0, 1], idx: [vi(-1, -1, 1), vi(1, -1, 1), vi(1, 1, 1), vi(-1, 1, 1)] },
  { name: 'bottom', normal: [0, 0, -1], idx: [vi(-1, -1, -1), vi(-1, 1, -1), vi(1, 1, -1), vi(1, -1, -1)] },
  { name: 'front', normal: [1, 0, 0], idx: [vi(1, -1, -1), vi(1, 1, -1), vi(1, 1, 1), vi(1, -1, 1)] },
  { name: 'back', normal: [-1, 0, 0], idx: [vi(-1, -1, -1), vi(-1, -1, 1), vi(-1, 1, 1), vi(-1, 1, -1)] },
  { name: 'left', normal: [0, 1, 0], idx: [vi(-1, 1, -1), vi(-1, 1, 1), vi(1, 1, 1), vi(1, 1, -1)] },
  { name: 'right', normal: [0, -1, 0], idx: [vi(-1, -1, -1), vi(1, -1, -1), vi(1, -1, 1), vi(-1, -1, 1)] },
];
function makeCamera(width, height) {
  const az = -125 * DEG; const el = 24 * DEG; const dist = 6.2;
  const eye = [dist * Math.cos(el) * Math.cos(az), dist * Math.cos(el) * Math.sin(az), dist * Math.sin(el)];
  const len = Math.hypot(...eye);
  const f = eye.map((c) => -c / len);                         // 视线方向（看向原点）
  let r = [f[1], -f[0], 0];                                    // f × Z
  const rl = Math.hypot(...r); r = r.map((c) => c / rl);
  const u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]; // r × f
  return { eye, f, r, u, cx: width / 2, cy: height / 2, focal: Math.min(width, height) * 1.55 };
}
function projectPoint(cam, p) {
  const v = [p[0] - cam.eye[0], p[1] - cam.eye[1], p[2] - cam.eye[2]];
  const x = v[0] * cam.r[0] + v[1] * cam.r[1] + v[2] * cam.r[2];
  const y = v[0] * cam.u[0] + v[1] * cam.u[1] + v[2] * cam.u[2];
  const z = v[0] * cam.f[0] + v[1] * cam.f[1] + v[2] * cam.f[2];
  const k = cam.focal / Math.max(z, 0.1);
  return [cam.cx + x * k, cam.cy - y * k, z];
}
function polygonArea(points) {
  let area = 0;
  for (let i = 0; i < points.length; i += 1) {
    const a = points[i]; const b = points[(i + 1) % points.length];
    area += a[0] * b[1] - b[0] * a[1];
  }
  return Math.abs(area) / 2;
}
function convexHull(points) {
  const pts = points.map((p) => [p[0], p[1]]).sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  const lower = []; const upper = [];
  for (const p of pts) { while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) lower.pop(); lower.push(p); }
  for (const p of pts.slice().reverse()) { while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) upper.pop(); upper.push(p); }
  return lower.slice(0, -1).concat(upper.slice(0, -1));
}
// 旋转矩阵 → 板子顶点投影 + 可见面（画家算法：远 → 近）
function projectBoard(R, width, height) {
  const cam = makeCamera(width, height);
  const world = BOARD_VERTS.map((v) => mulVec(R, v));
  const screen = world.map((p) => projectPoint(cam, p));
  const faces = [];
  for (const face of BOARD_FACES) {
    const n = mulVec(R, face.normal);
    const center = face.idx.reduce((acc, i) => [acc[0] + world[i][0] / 4, acc[1] + world[i][1] / 4, acc[2] + world[i][2] / 4], [0, 0, 0]);
    const toEye = [cam.eye[0] - center[0], cam.eye[1] - center[1], cam.eye[2] - center[2]];
    const facing = n[0] * toEye[0] + n[1] * toEye[1] + n[2] * toEye[2];
    if (facing <= 0) continue;
    const depth = face.idx.reduce((acc, i) => acc + screen[i][2], 0) / 4;
    faces.push({ name: face.name, points: face.idx.map((i) => screen[i]), depth, light: Math.max(0, n[2] * 0.6 + 0.4 * facing / Math.hypot(...toEye)) });
  }
  faces.sort((a, b) => b.depth - a.depth);
  const hull = convexHull(screen);
  return { cam, world, screen, faces, hull, hullArea: polygonArea(hull) };
}
// ---- temp1：10 Hz 温度轮询（0x14 QUERY_STATUS → 0x05 SYSINFO，temp_c_x100 @ offset 10）----
// 一次只有一个请求在途；300 ms 无回复记一次超时；连续 5 次超时降到 1 Hz；连续 30 次停止并记一行日志；任何有效 SYSINFO 回复恢复 10 Hz；
// 0x14 回 ACK 0x01 → 本次连接停止轮询。轮询帧和它的回复都不写日志、不改解析器/标签（由 app.js 胶水层保证）。
const TEMP_POLL = Object.freeze({ FAST_MS: 100, SLOW_MS: 1000, TIMEOUT_MS: 300, SLOW_AFTER: 5, STOP_AFTER: 30, STALE_MS: 1000, LATE_KEEP: 8, JITTER_MS: 10, TICK_MS: 20 });
// io: { now() → ms, canPoll() → bool, nextSeq() → u8（下一帧将使用的 seq）, send() → Promise<bool>（静默发送 0x14）, onTemp(tempC, atMs), log(text) }
function createTempPoller(io) {
  const s = { mode: 'fast', inFlight: null, late: [], misses: 0, lastSentAt: -Infinity, sent: 0, replies: 0, timeouts: 0, badLogged: false };
  const isPoll = (seq) => (s.inFlight && s.inFlight.seq === seq) || s.late.includes(seq);
  const forget = (seq) => { if (s.inFlight?.seq === seq) s.inFlight = null; s.late = s.late.filter((x) => x !== seq); };
  function good() { const was = s.mode; s.misses = 0; if (was === 'slow' || was === 'stopped') { s.mode = 'fast'; if (was === 'stopped') io.log?.('温度轮询已恢复（10 Hz）'); } }
  function tick() {
    const now = io.now();
    if (s.inFlight && now - s.inFlight.at >= TEMP_POLL.TIMEOUT_MS) {
      s.late = [...s.late.slice(-(TEMP_POLL.LATE_KEEP - 1)), s.inFlight.seq]; s.inFlight = null; s.timeouts++; s.misses++;
      if (s.misses >= TEMP_POLL.STOP_AFTER) { if (s.mode !== 'stopped') { s.mode = 'stopped'; io.log?.(`温度轮询已停止：连续 ${s.misses} 次无回复（刷新状态或重连后恢复）`); } }
      else if (s.misses >= TEMP_POLL.SLOW_AFTER) s.mode = 'slow';
    }
    if (s.mode === 'stopped' || s.mode === 'unsupported' || s.inFlight || !io.canPoll()) return false;
    if (now - s.lastSentAt < (s.mode === 'slow' ? TEMP_POLL.SLOW_MS : TEMP_POLL.FAST_MS) - TEMP_POLL.JITTER_MS) return false; // 20 ms 定时器 → 实际 90~110 ms 一帧
    const req = { seq: io.nextSeq() & 0xff, at: now };
    s.inFlight = req; s.lastSentAt = now; s.sent++;
    Promise.resolve(io.send()).then((ok) => { if (!ok && s.inFlight === req) s.inFlight = null; }, () => { if (s.inFlight === req) s.inFlight = null; });
    return true;
  }
  // 返回 true：这是轮询的回复（调用方只更新温度，不做其余 SYSINFO 处理）
  function onSysinfo(msg) {
    const mine = isPoll(msg.seq);
    if (mine) forget(msg.seq);
    if (Number.isFinite(msg.temp)) { good(); if (mine) { s.replies++; io.onTemp?.(msg.temp, io.now()); } }
    return mine;
  }
  // 返回 true：这是轮询请求的 ACK（静默处理）
  function onAck(cmd, status, seq) {
    if (cmd !== 0x14 || !isPoll(seq)) return false;
    forget(seq);
    if (status === 1) { s.mode = 'unsupported'; io.log?.('温度轮询已停止：固件不支持状态查询（ACK 0x01）'); }
    else if (!s.badLogged) { s.badLogged = true; io.log?.(`温度轮询：状态查询 ACK 0x${status.toString(16).padStart(2, '0')}`); }
    return true;
  }
  function reset() { Object.assign(s, { mode: 'fast', inFlight: null, late: [], misses: 0, lastSentAt: -Infinity, badLogged: false }); }
  return { state: s, tick, onSysinfo, onAck, reset, isPoll };
}
// ==== GYRO-CORE END ====

const RECOVERABLE_READ_ERRORS = new Set(['BufferOverrunError', 'BreakError', 'FramingError', 'ParityError']);
const DATA_TIMEOUT_MS = 500;   // 超过 500 ms 未收到数据 → 数据超时
const POSE_STALE_MS = 2000;    // 超过 2 s 未收到姿态 → Yaw/Pitch/Roll 置灰
const QUAT_PREFER_MS = 500;    // 最近 500 ms 内收到过 QUAT 帧 → 3D 优先用四元数

let port = null;
let reader = null;
let writer = null;
let running = false;
let connecting = false;
let rememberedConnection = null, reconnectPlan = null, reconnectTimer = null, connectionGeneration = 0;
let readLoopDone = Promise.resolve();
let portClosing = Promise.resolve();
let seq = 0;
let setting = false;
let lastFrames = 0;
let lastRateAt = performance.now();
let frameRate = 0;
let justChannels = 3;
let parseModeValue = 'auto';
let pose = { yaw: null, pitch: null, roll: null, temp: null };
let tempAt = 0;                   // temp1：pose.temp 最近一次更新时间（performance.now）；超过 TEMP_POLL.STALE_MS 显示 --
let imu = { gx: null, gy: null, gz: null, ax: null, ay: null, az: null };
let quat = null;
let lastQuatAt = 0;
let drawPending = false;
let pendingImmediateReset = false;
let startupFormDirty = false, startupFormRevision = 0, pendingStartup = null, startupAckTimer = null;
let pendingExitAfterMode = false;
let lastPoseAt = 0;
const poseTimes = { yaw: 0, pitch: 0, roll: 0 };
let lastImuAt = 0;
// app2 UI glue: per-channel arrival time of IMU values (read by ui-chart.js; parsing unchanged)
const imuStamp = { gx: 0, gy: 0, gz: 0, ax: 0, ay: 0, az: 0 };
const stampImu = (keys) => { const now = performance.now(); for (const k of keys) imuStamp[k] = now; };
let connectedAt = 0;
let streamMode = null;
let canOk = 0;
let fusionHz = null;              // 来自 SYSINFO.fusion_hz；未收到前按 DEFAULT_FUSION_HZ 校验
let fusionPendingRestart = false; // 本会话保存过融合模式且设备尚未重启
let dirtyUnknown = false;         // 设备报告有待应用配置，但本页不知道其中是否含融合模式
let probingDirty = false;         // 进入设置模式前发送 EXIT 探测 app_settings_dirty
let dirtyCanOnly = false;         // 本会话已知：设备待应用的配置只来自 CAN ID（不含融合模式）
let dataTimedOut = false;
let firstDataSeen = false;
let lastProjection = null;
let badLengthLogged = 0;
let deviceConfig = null;
let canConfig = null, pendingCan = null, canAckTimer = null;
let canFormDirty = false;
let canFormRevision = 0;
let selectionMask = null;
let pendingOutput = null;
let outputAckTimer = null;
let rateFormDirty = false, rateFormRevision = 0, pendingRate = null, rateAckTimer = null;
let accCalibration = null, accStartPending = false, accAckTimer = null, accStatusAt = 0, accLastQuery = 0;
const firmware = globalThis.GyroFirmware;
let firmwareImage = null, firmwareFileGeneration = 0, firmwareBusy = false, firmwareRestoring = false, firmwareChoosingPort = false;
let firmwareRecoveryPort = null, firmwareStopRequested = false, firmwarePhase = 'idle', firmwarePingAt = 0;
// 固件升级链路：只有板载 USB CDC（2E3C:F401，应用与 Bootloader 共用 cdc_desc.h）走 USB 重新枚举路径；
// 其余端口（WCH 1A86:8012、测试 mock 2E3C:5740 等 USB-UART 适配器）都按 UART 处理。连接后自动判断，没有手动开关。
const USB_CDC_VID = 0x2e3c, USB_CDC_PID = 0xf401;
function firmwareTransport(info) {
  return info?.usbVendorId === USB_CDC_VID && info?.usbProductId === USB_CDC_PID ? 'usb' : 'uart';
}
function portTransport(p) { try { return p ? firmwareTransport(p.getInfo()) : null; } catch { return 'uart'; } }
const FW_UART_TIMEOUT_TEXT = '原串口未响应 Bootloader。请确认板上已安装配套 Bootloader；USB 端口变化时可手动选择恢复端口。';
const FW_USB_TIMEOUT_TEXT = 'USB 重新枚举后未连上同一块板子。应用尚未擦除；请只保留这块板子，或勾选恢复模式并手动选择新出现的 USB 串口。';
const FW_USB_MULTI_TEXT = '检测到多块相同的 USB 设备，已停止。请只连接当前板子后再升级。';
const FW_UNCONFIRMED_TEXT = '固件已完整校验，但尚未确认应用启动。请保持供电并重新连接检查；上传完成不代表应用已运行。固件已写完，不要断电，不要再点升级；刷新后重连即可。';
const FW_RECONNECT_OPEN_MS = 2000, FW_RESTORE_MS = 90000;
// restoreAfterFirmware 期间由 navigator.serial 'connect' 事件采纳的重新枚举端口；有界打开每 50 ms 检查它
let firmwareReconnectPort = null, firmwareOpenBoundedCalls = 0, firmwareLastBoundedAbort = null;
// 只用于升级后的重连（connect({firmware:true})）。open() 与超时赛跑：超时或发现新端口时立即失败，
// 迟到成功的 open 立刻 close。普通用户点击连接仍直接 port.open()，不加超时。
function openPortBounded(p, options, ms = FW_RECONNECT_OPEN_MS) {
  firmwareOpenBoundedCalls++;
  return new Promise((resolve, reject) => {
    let settled = false, timer = null, watch = null;
    const finish = (error) => {
      if (settled) return;
      settled = true; clearTimeout(timer); clearInterval(watch);
      if (error) { firmwareLastBoundedAbort = { reason: error.message, t: performance.now() }; reject(error); } else resolve();
    };
    Promise.resolve().then(() => p.open(options)).then(
      () => { if (settled) { Promise.resolve().then(() => p.close()).catch(() => {}); log('迟到打开的串口已关闭'); } else finish(); },
      (error) => finish(error));
    timer = setTimeout(() => finish(new Error(`打开串口超时（${ms} ms）`)), ms);
    watch = setInterval(() => {
      if (firmwareReconnectPort && firmwareReconnectPort !== p) finish(new Error('已发现重新枚举的 USB 端口'));
    }, 50);
  });
}

const parser = createStreamParser({ onMessage: handleMessage, onJust: handleJust });
const writes = createWriteQueue(() => writer);
// 姿态稳定性（filter-profile.js）：ACK 状态为准，再以 0x26 回读核对；只发送 0x26 / 0x27 / 0x28，不发重启或融合模式命令
let filterView = null;
const filterProfile = globalThis.GyroFilter ? globalThis.GyroFilter.createController({
  send: async (cmd, payload) => { const frameSeq = seq & 0xff; return (await send(cmd, payload)) ? frameSeq : null; },
  say: (text) => say(text), log: (text) => log(text), changed: () => filterView?.render(),
}) : null;
// limits1：零角速保持阈值（zaru-limits.js）：0x2E 查询 / 0x2F 写入（需设置模式）→ ACK 0 + 0x0F 回读一致才算成功；不切换档位
let zaruView = null;
const zaruLimits = globalThis.GyroZaru ? globalThis.GyroZaru.createController({
  send: async (cmd, payload) => { const frameSeq = seq & 0xff; return (await send(cmd, payload)) ? frameSeq : null; },
  editable: () => settingEditable(), say: (text) => say(text), log: (text) => log(text), changed: () => zaruView?.render(),
}) : null;

// bias1：VQF 运动零偏状态（motion-bias.js，只读）：0x30 → 0x09；ACK 0x01 → 静默隐藏；展开时可 2 Hz 静默刷新（与温度轮询同一门控）
let biasView = null;
const motionBias = globalThis.GyroBias ? globalThis.GyroBias.createController({
  send: (cmd, payload, { quiet = false } = {}) => (quiet ? sendQuiet(cmd, payload) : send(cmd, payload)),
  canAuto: () => tempPollAllowed(), log: (text) => log(text), changed: () => biasView?.render(),
}) : null;

// vqfinit1：静置初始化 VQF（vqf-init.js）：连接后 0x2C + 0x29；开始 / 恢复默认由 ui-shell.js 自动设置模式门包装；ACK 0x01 或 3 s 无数据 → 隐藏
let vqfView = null;
const vqfInit = globalThis.GyroVqf ? globalThis.GyroVqf.createController({
  send: (cmd, payload, { quiet = false } = {}) => (quiet ? sendQuiet(cmd, payload) : send(cmd, payload)),
  editable: () => settingEditable(), say: (text) => say(text), log: (text) => log(text), changed: () => { vqfView?.render(); globalThis.GyroUI?.vqfChanged?.(); },
}) : null;

// biashist1：启动零偏历史（bias-history.js，只读）：连上后自动整表读一次（静默翻页）；ACK 0x01 / 3 s 无 0x34 → 隐藏卡片
let biasHistView = null;
const biasHist = globalThis.GyroBiasHist ? globalThis.GyroBiasHist.createController({
  send: (cmd, payload) => sendQuiet(cmd, payload), log: (text) => log(text), changed: () => biasHistView?.render(),
}) : null;

// fwver1：应用固件版本（0x23 → 0x32）。连上后查一次；固件升级完成后再查一次；旧固件 ACK 0x01 / 3 s 无回复 → 「当前版本」保持 --
let fwVersion = { text: null, supported: null }, fwVersionTimer = null, fwVersionRequery = false;
function renderFwVersion() {
  const node = $('fwVersion'); if (!node) return;
  const text = fwVersion.text || '--';
  const title = fwVersion.text ? '应用固件版本（0x23 回读）' : fwVersion.supported === false ? '当前固件不上报版本（旧固件，ACK 0x01）' : '未读到应用版本';
  if (node.textContent !== text) node.textContent = text;
  if (node.title !== title) node.title = title;
}
let deviceModel = { text: null, supported: null };
function renderDeviceModel() {
  const node = $('deviceModel');
  if (node) {
    node.textContent = deviceModel.text || '--';
    node.title = deviceModel.text ? '设备型号（0x35 回读）' : deviceModel.supported === false ? '当前固件不上报型号' : '未读到设备型号';
  }
}
async function queryFwVersion() {
  clearTimeout(fwVersionTimer);
  fwVersionTimer = setTimeout(() => { fwVersionTimer = null; if (!fwVersion.text) log('未收到应用版本（0x32），当前版本保持 --'); }, 3000);
  const sent = await send(CMD.QUERY_FW_VERSION);
  if (sent) await send(CMD.QUERY_DEVICE_MODEL);
  return sent;
}

const log = (message) => {
  const node = $('log');
  if (!node) return;
  const now = new Date().toLocaleTimeString();
  node.textContent += `[${now}] ${message}\n`;
  const lines = node.textContent.split('\n');
  if (lines.length > 300) node.textContent = lines.slice(-300).join('\n');
  node.scrollTop = node.scrollHeight;
};
const say = (message) => {
  $('message').textContent = message;
  log(message);
};
const hex = (value, width = 2) => value.toString(16).padStart(width, '0');

async function send(cmd, payload = []) {
  if (firmwareBusy && !firmwareRestoring) return false;
  if (!writer) {
    say('请先连接串口');
    return false;
  }
  const frame = buildFrame(cmd, seq++ & 0xff, payload);
  try {
    await writes.write(frame);
    log(`发送 CMD 0x${hex(cmd)} ${payload.map((v) => hex(v)).join(' ')}`);
    return true;
  } catch (error) {
    say(`发送失败：${error.message}`);
    return false;
  }
}

// app1 UI glue: ui-shell.js enters settings mode automatically right before a write and exits afterwards,
// so settings-gated controls stay editable whenever the device is connected (protocol unchanged).
let autoSettingsMode = true;
function settingEditable() { return setting || (autoSettingsMode && running && !firmwareBusy); }

// temp1：温度轮询专用静默发送（不写日志、失败不提示）；固件升级期间一律不发
async function sendQuiet(cmd, payload = []) {
  if (firmwareBusy || !writer) return false;
  const frame = buildFrame(cmd, seq++ & 0xff, payload);
  try { await writes.write(frame); return true; } catch { return false; }
}
// 轮询门控：已连接且空闲。连接中 / 升级（含选端口、升级后恢复）/ 重启重连 / 任何“应用 + 回读核对”进行中 / gyro3 自动设置模式门 / 页面不可见 → 暂停
function tempPollAllowed() {
  return running && !!writer && !connecting && !firmwareBusy && !firmwareRestoring && !firmwareChoosingPort && !reconnectPlan &&
    !pendingStartup && !pendingOutput && !pendingCan && !pendingRate && !pendingImmediateReset && !accStartPending &&
    !filterProfile?.state.pending && !zaruLimits?.state.pending && !vqfInit?.state.pending && !biasHist?.state.reading && !globalThis.GyroUI?.gate?.busy && document.visibilityState === 'visible';
}
const tempPoller = createTempPoller({
  now: () => performance.now(), canPoll: tempPollAllowed, nextSeq: () => seq & 0xff, send: () => sendQuiet(CMD.QUERY),
  onTemp: (temp, at) => { pose.temp = temp; tempAt = at; renderTemp(); }, log: (text) => log(text),
});
function renderTemp() {
  const node = $('temp'); if (!node) return;
  const text = Number.isFinite(pose.temp) && performance.now() - tempAt <= TEMP_POLL.STALE_MS ? pose.temp.toFixed(2) : '--';
  if (node.textContent !== text) node.textContent = text;
}
setInterval(() => { tempPoller.tick(); renderTemp(); }, TEMP_POLL.TICK_MS);
setInterval(() => { motionBias?.tick(); }, 100); // bias1：仅 #biasPanel 展开时生效
setInterval(() => { if (running && !firmwareBusy) vqfInit?.tick(); }, 250); // vqfinit1：采集中 1 s 无推送 → 静默补查 0x29

function setConnected(connected) {
  if (!connected) { clearTimeout(accAckTimer); accStartPending = false; }
  $('connectBtn').disabled = firmwareBusy || connected || connecting || !!reconnectPlan;
  $('disconnectBtn').disabled = firmwareBusy || (!connected && !reconnectPlan);
  $('linkState').textContent = connected ? '串口已连接' : reconnectPlan ? '等待原串口重连' : '未连接';
  $('linkState').className = `badge ${connected ? 'on' : reconnectPlan ? '' : 'off'}`;
  updateOutputControls();
  updateAccCalibrationUI();
  updateLiveness();
  updateSettingUI(); // app1: settings-gated controls follow the connection state
}

const ACC_FACES = ['−X', '+X', '−Y', '+Y', '−Z', '+Z'];
function updateAccCalibrationUI() {
  const s = accCalibration, busy = accStartPending || s?.status === 1;
  $('acc6Btn').disabled = !running || !settingEditable() || busy || s?.enabled === false;
  $('accCancelBtn').disabled = !running || !busy;
  $('accRefreshBtn').disabled = !running;
  $('accFaces').querySelectorAll('[data-face]').forEach(e => {
    const face = Number(e.dataset.face), done = !!(s?.mask & (1 << face));
    e.className = `acc-face${done?' done':''}${s?.status===1 && s.face===face+1?' current':''}`;
    e.textContent = `${done?'✓ ':''}${ACC_FACES[face]}`;
  });
  $('accFaceProgress').value = busy ? (s?.progress || 0) : s?.status === 2 ? 1000 : 0;
  $('accFaceProgress').hidden = !busy;
  const count = s ? ACC_FACES.filter((_,i)=>s.mask & (1<<i)).length : 0;
  if (!running) $('accStatus').textContent = '连接设备后可读取校准状态；标定中断开时，设备会继续等待或超时。';
  else if (accStartPending) $('accStatus').textContent = '正在请求开始，等待设备确认…';
  else if (s?.enabled === false) $('accStatus').textContent = '当前固件未启用自动六面校准。';
  else if (!s) $('accStatus').textContent = '先进入设置模式，再开始六面校准。旧固件请更新后使用。';
  else if (s.status === 1) {
    const phase = ['放置下一个未完成的面', '保持静止，正在确认稳定', '保持静止，正在采样', '六面完成，正在验证并保存', '',
      '检测到运动或摆放倾斜，请摆正并保持静止', '这一面已完成，请翻到另一面'][s.phase] || '等待设备';
    const stale = performance.now()-accStatusAt > 3000 ? '状态更新中断，正在重新读取；请保持连接。' : phase;
    $('accStatus').textContent = `${count}/6 面 · ${s.face ? ACC_FACES[s.face-1]+' · ' : ''}${stale}${s.remaining ? ' · 等待余时 '+Math.ceil(s.remaining/1000)+' 秒' : ''}`;
  } else if (s.status === 2) $('accStatus').textContent = '6/6 面 · 校准完成，已保存，重启后继续生效。';
  else if (s.status === 3 || s.status === 4) $('accStatus').textContent = ackDetailText(CMD.ACC_6FACE,3,s.error) || '校准未完成，原参数保留。';
  else $('accStatus').textContent = s.valid ? '已读取保存的校准参数。可重新进行六面校准。' : '尚无校准参数；请在平稳支撑面上完成六个方向。';
  $('accParams').textContent = s?.valid ? `零偏 g：${s.bias.map(x=>x.toFixed(5)).join(' / ')} · 比例：${s.scale.map(x=>x.toFixed(5)).join(' / ')}` : '';
  updateAccPlacementUI(s);
}

function updateAccPlacementUI(s) {
  const reference = accPlacementReference(s?.face, s?.raw);
  const fresh = running && s?.status === 1 && performance.now()-accStatusAt <= 3000;
  const live = fresh && !!reference;
  $('accRawLabel').textContent = live ? '当前原始加速度（g）' : reference ? '末次校准读数（非实时）' : '原始加速度（等待校准采样）';
  $('accRaw').classList.toggle('stale', !live);
  ['X','Y','Z'].forEach((axis,i) => {
    $('accRaw'+axis).textContent = reference ? `${s.raw[i] >= 0 ? '+' : ''}${s.raw[i].toFixed(4)}` : '—';
  });
  $('accRawNorm').textContent = reference ? `模长 ${reference.norm.toFixed(4)} g` : '';
  if (!reference) $('accPlacement').textContent = '每面让一个传感器轴竖直，其余两轴水平；把板卡固定在平整支撑上。';
  else {
    const axis = ['X','Y','Z'][reference.axis];
    const label = ACC_FACES[reference.face-1];
    const direction = reference.recognized ? `${live ? '设备识别' : '末次识别'} ${label}` : `${live ? '参考方向' : '末次参考方向'} ${label}（按 g 值估计）`;
    const targets = reference.reference.map((x,i) => `${['X','Y','Z'][i]} ${x > 0 ? '+' : ''}${x} g`).join(' / ');
    $('accPlacement').textContent = `${direction}：${axis} 轴正方向朝${reference.positive ? '上' : '下'}。理想摆正参考：${targets}。`;
  }
  let advice = '';
  if (s?.status === 3 && s.error === 0x0606) {
    advice = '六面已采齐，但拟合出的零偏超过 ±0.15 g 或比例超出 0.85~1.15，本次未保存；当前协议没有报告具体失败面。轻微倾斜已自动补偿，不是失败原因。请确认每面采样时板卡静止、未受外力，再重新开始；反复出现时请检查传感器量程与灵敏度配置。';
  } else if (s?.status === 3 && s.error === 0x0603) {
    advice = '等待下一个未完成方向超过 60 秒，本次已结束。请准备好支撑和翻面空间，再重新开始完整六面校准。';
  } else if (s?.status === 1 && s.phase === 5) {
    advice = '先固定板卡，再微调摆放，使其他两轴尽量接近 0 g。若数值接近参考仍未开始采样，请保持静止；这条状态也可能由晃动或角速度触发。';
  } else if (s?.status === 1 && s.phase === 6) {
    advice = '这一方向已有勾选，请翻到尚未完成的方向。';
  } else if (s?.status === 1) {
    advice = '理想值用于辅助摆正，原始值可能带少量零偏。请用平整支撑固定，等待本面勾选后再翻面。';
  }
  $('accAdvice').textContent = advice; $('accAdvice').hidden = !advice;
}

function configureAccCalibration(message) {
  accCalibration = message; accStatusAt = performance.now();
  clearTimeout(accAckTimer); accStartPending = false; updateAccCalibrationUI();
}

async function startAccCalibration() {
  if (!running || !setting) { say('请先连接并进入设置模式'); return; }
  accStartPending = true; updateAccCalibrationUI();
  accAckTimer = setTimeout(() => {
    accStartPending = false; updateAccCalibrationUI();
    say('未收到校准开始确认，正在读取设备状态，请勿重复启动'); void send(CMD.QUERY_ACC_CAL);
  },3000);
  if (!await send(CMD.ACC_6FACE)) { clearTimeout(accAckTimer); accStartPending = false; updateAccCalibrationUI(); }
}

function pollAccCalibration() {
  if (!running || accCalibration?.status !== 1) return;
  updateAccCalibrationUI();
  const now = performance.now();
  if (now-accStatusAt > 1500 && now-accLastQuery > 1500) { accLastQuery = now; void send(CMD.QUERY_ACC_CAL); }
}

// 数据流模式名（固件 stream_mode_t）
function streamModeName(mode) {
  return STREAM_MODES[mode] || `未知(${mode})`;
}
// 融合模式名（固件 fusion_mode_t：0 六轴 / 1 九轴 / 2 九轴相对角）
function fusionModeName(mode) {
  return ['六轴', '九轴', '九轴相对角'][mode] || '未知';
}

function flagsText(flags) {
  const parts = [];
  if (flags & FLAGS.REST) parts.push('静止');
  if (flags & FLAGS.MAG_VALID) parts.push('磁有效');
  if (flags & FLAGS.MAG_DISTURBED) parts.push('磁扰动');
  if (flags & FLAGS.CALIB_DONE) parts.push('已标定');
  if (flags & FLAGS.SENSOR_ERROR) parts.push('传感器异常');
  return parts.length ? parts.join('/') : '-';
}

function scheduleDraw() {
  if (!drawPending) {
    drawPending = true;
    requestAnimationFrame(() => { drawPending = false; draw(); });
  }
}

function updatePose(yaw, pitch, roll) {
  if (![yaw, pitch, roll].every(Number.isFinite)) return false;
  pose.yaw = yaw;
  pose.pitch = pitch;
  pose.roll = roll;
  lastFrames += 1;
  lastPoseAt = performance.now();
  for (const key of ['yaw', 'pitch', 'roll']) poseTimes[key] = lastPoseAt;
  scheduleDraw();
  return true;
}

function updateImu(values) {
  if (!values.every(Number.isFinite)) return;
  [imu.gx, imu.gy, imu.gz, imu.ax, imu.ay, imu.az] = values;
  stampImu(['gx', 'gy', 'gz', 'ax', 'ay', 'az']);
  lastFrames += 1;
  lastImuAt = performance.now();
  scheduleDraw();
}

// JustFloat：CH0 Yaw、CH1 Pitch、CH2 Roll（6 通道另有 CH3 Gz、CH4 Az、CH5 Temp）
function handleJust(values) {
  if (selectionMask !== null) {
    handleSelected(values, selectionMask);
    return;
  }
  const fields = justValuesToFields(values);
  if (values.length === 6) {
    imu.gz = fields.gz;
    imu.az = fields.az;
    stampImu(['gz', 'az']);
    pose.temp = fields.temp; if (Number.isFinite(fields.temp)) tempAt = performance.now();
  }
  updatePose(fields.yaw, fields.pitch, fields.roll);
}

function resetParser() {
  parser.configure(parseModeValue, justChannels, selectionMask);
  parser.reset();
}

// 让解析器与设备数据流一致（STREAM ACK 成功 / SYSINFO.stream_mode）
function syncParserToStream(mode, { force = true } = {}) {
  const config = streamParserConfig(mode);
  if (!config) return false;
  streamMode = mode;
  const channels = config.justChannels ?? justChannels;
  const changed = config.parseMode !== parseModeValue || channels !== justChannels || selectionMask !== null;
  selectionMask = null;
  parseModeValue = config.parseMode;
  justChannels = channels;
  if (force || changed) resetParser();
  $('parseMode').value = parseModeValue;
  $('justChannels').value = String(justChannels);
  $('streamMode').value = String(mode);
  $('streamLabel').textContent = `数据流：${streamModeName(mode)}${canOk ? ' · CAN 就绪' : ''}`;
  if (changed) log(`解析器已同步：${parseModeValue === 'binary' ? '二进制 AA55' : `JustFloat ${justChannels} 通道`}`);
  return true;
}

function handleSelected(values, mask) {
  const fields = selectedFields(values, mask);
  if (!fields) return;
  for (const key of FIELD_NAMES) {
    if (['yaw', 'pitch', 'roll'].includes(key)) pose[key] = fields[key] ?? null;
    else imu[key] = fields[key] ?? null;
  }
  stampImu(Object.keys(fields).filter((key) => key in imuStamp));
  quat = null; lastQuatAt = 0;
  lastFrames += 1;
  // Data may contain only a subset of Euler angles. The 3D view requires all three.
  const now = performance.now();
  for (const key of ['yaw', 'pitch', 'roll']) poseTimes[key] = key in fields ? now : 0;
  lastPoseAt = (mask & 7) === 7 ? now : 0;
  lastImuAt = now;
  scheduleDraw();
}

function outputMask(portIndex) {
  return [...document.querySelectorAll(`#outputFields${portIndex} input`)]
    .reduce((mask, node) => mask | (node.checked ? 1 << Number(node.value) : 0), 0);
}

function updateOutputControls() {
  for (let i = 0; i < 2; i++) $('outputPanel' + i).disabled = !running || !deviceConfig || !!pendingOutput;
  $('fastStart').disabled = !settingEditable() || !deviceConfig || !(deviceConfig.capabilities & 2) || !!pendingStartup;
  $('gyroInitSeconds').disabled = !settingEditable() || !(deviceConfig?.version >= 2) || !(deviceConfig.capabilities & 16) || (deviceConfig.version < 4 && $('fastStart').checked) || !!pendingStartup;
  $('gyroRange').disabled = !settingEditable() || !(deviceConfig?.version >= 3) || !(deviceConfig.capabilities & 32) || !!pendingStartup;
  $('rateApplyBtn').disabled = !running || !!pendingRate;
  $('canPanel').disabled = !running || !settingEditable() || !(deviceConfig?.capabilities & 8) || !canConfig || !canConfig.ready || !!pendingCan;
}

function fillStartupForm(message) {
  $('fastStart').checked = message.version >= 4 ? message.savedInitMs === 0 : !!message.savedFast;
  $('gyroInitSeconds').min = message.version >= 4 ? '0' : '0.1';
  $('gyroInitSeconds').value = (message.savedInitMs ?? 2000) / 1000;
  $('gyroRange').value = String(message.savedRangeDps ?? 1000);
  const radio = document.querySelector(`input[name="fusion"][value="${message.savedMode}"]`);
  if (radio) radio.checked = true;
}
function updateStartupDraftUI() {
  const combined = deviceConfig?.version >= 4;
  $('fastStartRow').hidden = !!combined;
  $('gyroInitRow').hidden = !combined && $('fastStart').checked;
  $('gyroInitLabel').textContent = combined ? '启动窗口' : '零偏时间';
  $('startupDraftHint').textContent = startupFormDirty ? '有未保存的启动修改。保存后在设备重启时生效。' : '';
  $('gyroInitHint').textContent = deviceConfig && (deviceConfig.version < 2 || !(deviceConfig.capabilities & 16))
    ? '当前固件不支持初始化零偏时长，请升级配套固件。'
    : combined ? '0～60 秒，默认 2 秒；升级等待和零偏采集同时进行。到期不合格使用历史零偏，0 秒直接使用历史启动。'
      : '重启后保持设备静止，完成此时长的零偏采样后开始输出姿态。';
  $('startupDiscard').disabled = !startupFormDirty || !deviceConfig || !!pendingStartup;
  $('gyroRangeHint').textContent = deviceConfig?.version >= 3 && (deviceConfig.capabilities & 32)
    ? `当前 ±${deviceConfig.activeRangeDps} dps · 已保存 ±${deviceConfig.savedRangeDps} dps；量程在重启时生效。`
    : deviceConfig ? '当前固件固定量程，需升级配套固件才能调节。' : '量程保存后在设备重启时生效。';
}
function startupSummary(mode, fast, duration) {
  if (deviceConfig?.version >= 4) return `${fusionModeName(mode)} / 启动窗口 ${(duration / 1000).toFixed(1)} 秒${duration === 0 ? '（历史零偏）' : ''}`;
  return `${fusionModeName(mode)} / ${fast ? '快速启动开' : `快速启动关${duration === null ? '（旧固件未提供零偏时长）' : ` / 零偏 ${(duration / 1000).toFixed(1)} 秒`}`}`;
}
function configureDevice(message) {
  if (message.outputs.some((output) => output.format > 2 || output.legacyMode > 4 || output.mask > 0x1ff)) return;
  deviceConfig = message;
  filterProfile?.setFusion(message.activeMode); // 第 4 档说明按 0x07 运行融合模式（不看 LED）
  zaruLimits?.setFusion(message.activeMode);    // 阈值说明：九轴不锁磁力计航向
  $('configState').textContent = `已读取 · 当前连接 ${message.source ? 'USB' : 'UART'}`;
  $('configState').className = 'badge on';
  $('startupState').textContent = `当前：${startupSummary(message.activeMode, message.activeFast, message.activeInitMs)} · 已保存：${startupSummary(message.savedMode, message.savedFast, message.savedInitMs)}`;
  if (pendingStartup?.acked && message.savedMode === pendingStartup.mode && message.savedFast === pendingStartup.fast &&
      (pendingStartup.initMs === null || message.savedInitMs === pendingStartup.initMs) &&
      (pendingStartup.rangeDps === null || message.savedRangeDps === pendingStartup.rangeDps)) {
    const request = pendingStartup;
    clearTimeout(startupAckTimer); pendingStartup = null;
    if (startupFormRevision === request.revision) startupFormDirty = false;
    say('启动设置已保存，将在下次设备重启后生效');
    if (pendingExitAfterMode) { pendingExitAfterMode = false; void send(CMD.EXIT); }
  }
  if (!startupFormDirty && !pendingStartup) fillStartupForm(message);
  document.querySelectorAll('input[name="fusion"]').forEach((node) => { node.disabled = !(message.capabilities & 1) && node.value !== '0'; });
  fusionPendingRestart = message.activeMode !== message.savedMode || message.activeFast !== message.savedFast || message.activeInitMs !== message.savedInitMs || message.activeRangeDps !== message.savedRangeDps;
  $('rateState').textContent = message.version >= 3 && (message.capabilities & 64)
    ? `USB / UART：当前 ${message.outHz} Hz · 断电恢复 ${message.savedOutputHz} Hz（设置后自动保存）`
    : '旧固件输出频率仅本次运行有效；升级配套固件后支持重启保留。';
  if (pendingRate?.acked && message.outHz === pendingRate.hz && message.savedOutputHz === pendingRate.hz) {
    if (rateFormRevision === pendingRate.revision) rateFormDirty = false;
    clearTimeout(rateAckTimer); pendingRate = null;
    say(`输出频率 ${message.outHz} Hz 已应用并保存，重启后保留`);
  }
  if (!rateFormDirty && !pendingRate) $('outHz').value = message.outHz;
  dirtyUnknown = false;
  for (let i = 0; i < 2; i++) {
    const output = message.outputs[i];
    $('outputFormat' + i).value = String(output.format === 2 ? (output.legacyMode >= 1 && output.legacyMode <= 3 ? 1 : 0) : output.format);
    document.querySelectorAll(`#outputFields${i} input`).forEach((node) => { node.checked = !!(output.mask & (1 << Number(node.value))); });
    $('outputSummary' + i).textContent = output.format === 2 ? `设备当前：兼容预设 · ${streamModeName(output.legacyMode)}` : output.mask === 0 ? '设备当前：数据输出已关闭' : `设备当前：${output.format ? 'AA55 自定义协议' : 'JustFloat'} · ${FIELD_LABELS.filter((_, bit) => output.mask & (1 << bit)).join('、')}`;
  }
  const current = message.outputs[message.source];
  if (current.format === 2) syncParserToStream(current.legacyMode, { force: false });
  else {
    const mode = current.format || !current.mask ? 'binary' : 'justfloat';
    const channels = selectedCount(current.mask);
    const changed = selectionMask !== current.mask || parseModeValue !== mode || justChannels !== channels;
    selectionMask = current.mask; parseModeValue = mode; justChannels = channels;
    if (changed) {
      resetParser();
      for (const key of FIELD_NAMES) { if (key in pose) pose[key] = null; else imu[key] = null; }
      for (const key of ['yaw', 'pitch', 'roll']) poseTimes[key] = 0;
      lastPoseAt = 0; lastImuAt = 0; quat = null; pose.temp = null;
    }
    $('parseMode').value = mode;
    let option = $('selectedChannels');
    if (!option) { option = document.createElement('option'); option.id = 'selectedChannels'; $('justChannels').append(option); }
    option.value = `selected:${channels}`; option.textContent = `${channels}（设备自选通道）`;
    $('justChannels').value = option.value;
    $('streamLabel').textContent = `${message.source ? 'USB' : 'UART'}：${current.mask ? (current.format ? '自定义协议' : 'JustFloat') : '输出已关闭'}`;
  }
  $('parseMode').disabled = selectionMask !== null;
  $('justChannels').disabled = selectionMask !== null;
  updateStartupDraftUI(); updateSettingUI(); scheduleDraw();
  if ((message.capabilities & 8) && !canConfig) void send(CMD.QUERY_CAN);
  if (!(message.capabilities & 8)) $('canConfigState').textContent = '旧固件：需升级以配置 CAN';
}

function canSummary(c) {
  const names = ['加速度', '角速度', '欧拉角', '四元数'].filter((_, i) => c.mask & (1 << i));
  return `CAN_ID 0x${hex(c.nodeId, 3)} · MST_ID 0x${hex(c.masterId, 3)} · ${CAN_RATES[c.baud] / 1000} Kbps · ${c.active ? `主动 / 每类 ${Number((1000/c.periodMs).toFixed(3))} Hz（${c.periodMs} ms）/ ${names.join('、') || '不输出'}` : '请求应答'}`;
}
function fillCanForm(c) {
  $('canRequestId').value = `0x${hex(c.nodeId, 3)}`;
  $('canMasterId').value = `0x${hex(c.masterId, 3)}`;
  $('canPeriod').value = c.periodMs;
  $('canFrequency').value = Number((1000/c.periodMs).toFixed(6));
  $('canBaud').value = c.baud; $('canActive').value = c.active;
  document.querySelectorAll('#canOutputs input').forEach((n) => { n.checked = !!(c.mask & (1 << Number(n.value))); });
}
function updateCanDraftUI() {
  const period = Number($('canPeriod').value);
  $('canFrequencyHint').textContent = $('canActive').value === '0' ? '请求 / 应答模式按请求发送；频率设置用于主动输出。'
    : Number.isInteger(period) && period >= 1 && period <= 10000
      ? `每类实际输出 ${Number((1000/period).toFixed(3))} Hz · ${period} ms；多个报文类型分别按此频率发送。`
      : '输出频率范围 0.1～1000 Hz；按设备的整毫秒间隔取最接近值。';
  $('canDraftHint').textContent = canFormDirty ? '有未应用的修改。读取设备配置会更新下方状态，并保留你正在编辑的参数。' : '';
  $('canDiscard').disabled = !canFormDirty || !canConfig || !!pendingCan;
}
function configureCan(message) {
  canConfig = message;
  const c = message.active;
  $('canNodeId').value = c.nodeId;
  if (pendingCan?.acked && Object.keys(pendingCan.config).every((key) => c[key] === pendingCan.config[key]) &&
      (!pendingCan.persist || Object.keys(pendingCan.config).every((key) => message.saved[key] === pendingCan.config[key]))) {
    const request = pendingCan;
    clearTimeout(canAckTimer); pendingCan = null;
    if (canFormRevision === request.revision) canFormDirty = false;
    say(`CAN 配置已应用${request.persist ? '并保存到设备' : '（仅本次运行）'}`);
  }
  if (!canFormDirty && !pendingCan) fillCanForm(c);
  $('canConfigState').textContent = !message.ready ? 'CAN 初始化失败' : message.busOff ? 'CAN 总线离线 / bus-off' : 'CAN 已读取';
  $('canConfigState').className = `badge ${message.ready && !message.busOff ? 'on' : ''}`;
  $('canConfigSummary').textContent = `当前：${canSummary(c)}`;
  $('canSavedSummary').textContent = `断电恢复：${canSummary(message.saved)}`;
  updateCanDraftUI();
  updateSettingUI();
}
async function applyCan() {
  if (!setting || !canConfig || pendingCan) return;
  const id = (name) => {
    const text = $(name).value.trim();
    return /^(?:\d+|0x[0-9a-f]+)$/i.test(text) ? Number(text) : NaN;
  };
  const c = { nodeId: id('canRequestId'), masterId: id('canMasterId'), periodMs: Number($('canPeriod').value),
    baud: Number($('canBaud').value), active: Number($('canActive').value), reserved: 0,
    mask: [...document.querySelectorAll('#canOutputs input')].reduce((mask, n) => mask | (n.checked ? 1 << Number(n.value) : 0), 0) };
  const persist = $('canPersist').checked;
  const payload = encodeCanConfig(c, persist);
  if (!payload) { say('CAN 参数无效：ID 为 0~2047，间隔为 1~10000 ms；发送量须低于总线带宽的 80%，请增加间隔'); return; }
  canFormDirty = true;
  pendingCan = { persist, config: c, revision: canFormRevision, acked: false }; updateCanDraftUI(); updateSettingUI();
  canAckTimer = setTimeout(() => { pendingCan = null; updateCanDraftUI(); updateSettingUI(); say('未确认 CAN 配置回读，请读取设备配置核对实际状态'); }, 3000);
  if (!(await send(CMD.CAN_CONFIG, payload))) { clearTimeout(canAckTimer); pendingCan = null; updateCanDraftUI(); updateSettingUI(); }
}

async function applyOutput(portIndex) {
  if (!deviceConfig || pendingOutput) return;
  const format = Number($('outputFormat' + portIndex).value);
  const mask = outputMask(portIndex);
  const persist = $('outputPersist' + portIndex).checked;
  pendingOutput = { port: portIndex, persist };
  updateOutputControls();
  outputAckTimer = setTimeout(() => {
    pendingOutput = null; updateOutputControls();
    say('未收到输出配置应答，请刷新状态确认设备实际配置');
  }, 3000);
  if (!(await send(CMD.OUTPUT_CONFIG, [portIndex, format, mask & 255, mask >> 8, persist ? 1 : 0]))) {
    clearTimeout(outputAckTimer); pendingOutput = null; updateOutputControls();
  }
}

function handleAck({ cmd: command, status, detail, seq: ackSeq }) {
  if (tempPoller.onAck(command, status, ackSeq)) return; // 轮询 0x14 的 ACK：静默处理（0x01 → 停止轮询）
  const statusText = ACK_STATUS[status] || `0x${status.toString(16)}`;
  const reason = ackDetailText(command, status, detail);
  log(`ACK CMD 0x${hex(command)} ${statusText} detail=0x${hex(detail, 4)}${reason ? `（${reason}）` : ''}`);
  if (zaruLimits?.onAck(command, status, detail, ackSeq)) return;
  if (vqfInit?.onAck(command, status, detail)) return; // 0x29–0x2D
  if (motionBias?.onAck(command, status)) return; // 0x01 → 隐藏（不提示通信故障）
  if (biasHist?.onAck(command, status)) return; // biashist1：0x01 → 隐藏；0x02 → offset 无效
  if (filterProfile?.onAck(command, status, detail, ackSeq)) return;
  if (command === CMD.QUERY_CAN && status !== 0) {
    canConfig = null;
    $('canConfigState').textContent = status === 1 ? '旧固件：不支持 CAN 配置' : 'CAN 查询失败';
    updateSettingUI();
  } else if (command === CMD.CAN_CONFIG) {
    if (status === 0 && pendingCan) {
      pendingCan.acked = true;
      say('设备已接受 CAN 配置，等待读回确认');
    } else {
      clearTimeout(canAckTimer); pendingCan = null;
      updateCanDraftUI(); updateSettingUI();
      say(status === 0 ? '设备报告 CAN 配置成功，请读取设备配置确认' : `CAN 配置失败（${statusText}，请确认设置模式、参数和设备状态）`);
    }
    void send(CMD.QUERY_CAN);
  } else if (command === CMD.ENTER) {
    setting = status === 0;
    updateSettingUI();
    say(status === 0 ? '已进入设置模式' : `进入设置模式失败（${statusText}）`);
  } else if (command === CMD.EXIT) {
    if (probingDirty) {
      // 进入设置模式前的探测：EXIT 在非设置模式下无副作用，detail = app_settings_dirty
      probingDirty = false;
      if (status === 0 && detail === 0) {
        if (fusionPendingRestart || dirtyUnknown) say('设备无待应用的配置（已重启），CAN ID 设置已恢复');
        fusionPendingRestart = false;
        dirtyUnknown = false;
        dirtyCanOnly = false;
      } else if (status === 0 && !fusionPendingRestart && !dirtyCanOnly) {
        dirtyUnknown = true;
        log('设备存在已保存但尚未应用的配置（可能包含融合模式）');
      }
      updateSettingUI();
      return;
    }
    if (status === 0) {
      pendingExitAfterMode = false;
      setting = false;
      updateSettingUI();
      say(detail ? '已退出设置模式，存在已保存但尚未应用的配置，请重启设备' : '已退出设置模式');
    } else {
      say(`退出设置模式失败（${statusText}）`);
    }
  } else if (command === CMD.STARTUP) {
    if (status === 0 && pendingStartup) {
      pendingStartup.acked = true;
      if (pendingImmediateReset) {
        clearTimeout(startupAckTimer); pendingStartup = null;
        startupFormDirty = false;
        pendingImmediateReset = false; pendingExitAfterMode = false;
        say('启动设置已保存，设备重启中；请保持静止，将自动重连原串口');
        beginRestartReconnect();
      } else {
        say('设备已接受启动设置，等待读回确认');
        void send(CMD.QUERY_CONFIG);
      }
    } else {
      clearTimeout(startupAckTimer); pendingStartup = null;
      pendingImmediateReset = false; pendingExitAfterMode = false;
      cancelRestartReconnect();
      say(status === 0 ? '设备报告启动设置已保存，请读取配置确认' : `启动设置保存失败（${statusText}${status === 3 ? '：未进入设置模式或 Flash 写入失败' : ''}）`);
    }
    updateStartupDraftUI(); updateSettingUI();
  } else if (command === CMD.MODE) {
    if (status === 0) {
      if (pendingImmediateReset) {
        pendingImmediateReset = false;
        pendingExitAfterMode = false;
        fusionPendingRestart = false; // 设备立即重启，新模式生效
        dirtyUnknown = false;
        say(`启动配置已保存（${fusionModeName(detail)}），设备重启中，将自动重连原串口`);
        beginRestartReconnect();
      } else {
        // 固件仅在 apply_now(payload[1])!=0 时复位；EXIT_SETTINGS 不会重启
        fusionPendingRestart = true;
        dirtyCanOnly = false;
        say(`融合模式已保存（${fusionModeName(detail)}），将在下次设备重启后生效`);
        if (pendingExitAfterMode) {
          pendingExitAfterMode = false;
          void send(CMD.EXIT);
        }
      }
      updateSettingUI();
    } else {
      pendingImmediateReset = false;
      pendingExitAfterMode = false;
      cancelRestartReconnect();
      say(`融合模式保存失败（${statusText}${status === 3 ? '：未进入设置模式或 Flash 写入失败' : ''}）`);
    }
  } else if (command === CMD.QUERY_CONFIG && status !== 0) {
    if (status === 1) {
      deviceConfig = null;
      $('configState').textContent = '旧固件：需升级以使用独立输出和快速启动配置';
      updateOutputControls();
    } else say(`设备配置查询失败（${statusText}）`);
  } else if (command === CMD.OUTPUT_CONFIG) {
    clearTimeout(outputAckTimer);
    const request = pendingOutput; pendingOutput = null;
    updateOutputControls();
    say(status === 0 ? `${detail ? 'USB' : 'UART'} 输出已应用${request?.persist ? '并保存到设备' : '（仅本次运行）'}` : `输出配置失败（${statusText}）`);
    if (status === 0) void send(CMD.QUERY_CONFIG);
  } else if (command === CMD.CAN_ID) {
    if (status === 0 && !fusionPendingRestart && !dirtyUnknown) dirtyCanOnly = true;
    say(status === 0 ? `CAN 节点 ID 已保存：0x${hex(detail, 3)}` : `CAN ID 保存失败（${statusText}，当前 0x${hex(detail, 3)}）`);
    if (deviceConfig?.capabilities & 8) void send(CMD.QUERY_CAN);
  } else if (command === CMD.STREAM) {
    if (status === 0) {
      syncParserToStream(detail & 0xff, { force: true });
      if (deviceConfig) void send(CMD.QUERY_CONFIG);
      say(`数据流已切换：${streamModeName(streamMode)}`);
    } else {
      say(`数据流切换失败（${statusText}，设备当前：${streamModeName(detail & 0xff)}）`);
    }
  } else if (command === CMD.OUTPUT_HZ) {
    const hz = fusionHz ?? DEFAULT_FUSION_HZ;
    if (status === 0 && pendingRate) {
      pendingRate.acked=true; say('设备已接受输出频率，等待读回保存结果'); void send(CMD.QUERY_CONFIG);
    } else {
      clearTimeout(rateAckTimer);pendingRate=null;updateOutputControls();
      say(status === 0 ? (deviceConfig?.capabilities & 64 ? `设备报告输出频率 ${detail} Hz，刷新状态核对保存结果` : `输出频率已设为 ${detail} Hz（旧固件仅本次运行有效）`) :
        status === 3 ? `输出频率保存失败，当前 ${detail} Hz；修改已保留，可重试` :
        `输出频率设置失败（${statusText}，须能整除融合率 ${hz} Hz；当前 ${detail} Hz）`);
    }
    void send(CMD.QUERY);
  } else if (command === CMD.QUERY_DEVICE_MODEL) {
    deviceModel = { text: null, supported: status === 1 ? false : null };
    renderDeviceModel();
  } else if (command === CMD.QUERY_FW_VERSION) {
    // fwver1：成功不回 ACK；0x01 = 旧固件（继续显示 --），0x02 = 载荷非空
    clearTimeout(fwVersionTimer); fwVersionTimer = null;
    fwVersion = { text: null, supported: status === 1 ? false : fwVersion.supported }; renderFwVersion();
  } else if (command === CMD.ACC_6FACE) {
    if (status !== 0 || detail === 0) { clearTimeout(accAckTimer); accStartPending = false; }
    if (status === 0) say(reason || `六面标定 ACK（detail=0x${hex(detail, 4)}）`);
    else say(`六面加速度计标定失败（${statusText}，detail=0x${hex(detail, 4)}${reason ? `：${reason}` : ''}）`);
    updateAccCalibrationUI(); void send(CMD.QUERY_ACC_CAL);
  } else if (command === CMD.QUERY_ACC_CAL && status !== 0) {
    clearTimeout(accAckTimer); accStartPending = false;
    accCalibration = {enabled:false,status:0,mask:0,valid:false}; updateAccCalibrationUI();
    $('accStatus').textContent = status === 1 ? '当前固件不支持校准进度，请升级固件后使用。' : '校准状态读取失败，请重试。';
  } else if (command === CMD.CANCEL_ACC_CAL) {
    if (status === 0) say('设备已接受取消请求，正在读取校准状态');
    else say(`取消校准失败（${statusText}），请读取设备状态`);
    void send(CMD.QUERY_ACC_CAL);
  } else if (command === CMD.PING) {
    if (firmwareRestoring && status === 0) firmwarePingAt = performance.now();
    say('PONG：设备在线');
  } else if (command === CMD.QUERY && status !== 0) {
    say(`状态查询失败（${statusText}）`);
  }
}

function handleMessage(message) {
  switch (message.type) {
    case 'accCalibration':
      configureAccCalibration(message);
      break;
    case 'canConfig':
      configureCan(message);
      break;
    case 'ack':
      handleAck(message);
      break;
    case 'filterConfig':
      log(`姿态稳定性：当前 ${GyroFilter.profileName(message.active)}，已保存 ${GyroFilter.profileName(message.saved)}，内部 ${message.estimatorHz} Hz，tau_mag ${message.tauMag.toFixed(2)} s，rest_tau ${message.restTau.toFixed(2)} s`);
      filterProfile?.onConfig(message);
      break;
    case 'filterDiag':
      filterProfile?.onDiag(message);
      break;
    case 'motionBias':
      motionBias?.onMessage(message); // 不写日志（可能是 2 Hz 刷新）
      break;
    case 'biasHistory':
      biasHist?.onMessage(message); // 翻页帧不写日志
      break;
    case 'deviceModel':
      deviceModel = { text: message.text, supported: true }; renderDeviceModel();
      break;
    case 'fwVersion':
      clearTimeout(fwVersionTimer); fwVersionTimer = null;
      fwVersion = { text: message.text, supported: true }; renderFwVersion(); log(`应用固件版本：${message.text}`);
      break;
    case 'vqfStatus':
      vqfInit?.onStatus(message); // 采集中约 5 Hz 推送：不写日志
      break;
    case 'vqfSettings':
      log(`VQF 静置参数：source=${message.source} σ0=${message.current.sigmaInit.toFixed(3)} σr=${message.current.sigmaRest.toFixed(3)} 门限 ${message.current.restGyr.toFixed(2)} °/s / ${message.current.restAcc.toFixed(2)} m/s²`);
      vqfInit?.onSettings(message);
      break;
    case 'zaruLimits':
      if (message.supported) log(`零角速保持阈值：当前 ${GyroZaru.summary(message.runtime)}；已保存 ${GyroZaru.summary(message.saved)}`);
      else log('零角速保持阈值：固件报告 supported=0（未启用）');
      zaruLimits?.onLimits(message);
      break;
    case 'config':
      configureDevice(message);
      break;
    case 'selected':
      handleSelected(message.values, message.mask);
      break;
    case 'sysinfo': {
      if (tempPoller.onSysinfo(message)) break; // 10 Hz 轮询回复：只更新温度（onTemp），不写日志、不同步解析器、不改标签
      // SYSINFO 不含融合模式字段：只显示融合率 / 输出频率
      fusionHz = message.fusionHz || null;
      canOk = message.canOk;
      $('modeLabel').textContent = `融合率：${message.fusionHz} Hz · 输出：${message.outHz} Hz`;
      pose.temp = message.temp; tempAt = performance.now();
      if (!syncParserToStream(message.streamMode, { force: false })) {
        $('streamLabel').textContent = `数据流：${streamModeName(message.streamMode)}${canOk ? ' · CAN 就绪' : ''}`;
      }
      log(`状态：融合 ${message.fusionHz}Hz，输出 ${message.outHz}Hz，skip ${message.skipN}，温度 ${message.temp.toFixed(2)}°C，流 ${streamModeName(message.streamMode)}${canOk ? '，CAN 就绪' : ''}`);
      scheduleDraw();
      break;
    }
    case 'attitude':
      updatePose(message.yaw, message.pitch, message.roll);
      $('flagsLabel').textContent = `状态位：${flagsText(message.flags)}`;
      break;
    case 'compact':
      imu.gz = message.gz; stampImu(['gz']);
      updatePose(message.yaw, message.pitch, message.roll);
      $('flagsLabel').textContent = `状态位：${flagsText(message.flags)}`;
      break;
    case 'imu':
      if (Number.isFinite(message.temp)) { pose.temp = message.temp; tempAt = performance.now(); }
      updateImu([message.gx, message.gy, message.gz, message.ax, message.ay, message.az]);
      break;
    case 'quat': {
      if (![message.qw, message.qx, message.qy, message.qz].every(Number.isFinite)) break;
      quat = [message.qw, message.qx, message.qy, message.qz];
      lastQuatAt = performance.now();
      const euler = quatToEuler(...quat);
      updatePose(euler.yaw, euler.pitch, euler.roll);
      break;
    }
    case 'unknown':
      if (message.id === MSG.ZARU_LIMITS) zaruLimits?.onDropped(message); // 版本 / 范围 / 非有限数 → 整帧丢弃
      if (message.id === MSG.MOTION_BIAS) motionBias?.onDropped(message);
      if (message.id === MSG.VQF_STATUS || message.id === MSG.VQF_SETTINGS) vqfInit?.onDropped(message);
      if (message.id === MSG.BIAS_HISTORY) biasHist?.onDropped(message);
      break;
    case 'badLength':
      if (message.id === MSG.ZARU_LIMITS) zaruLimits?.onDropped(message);
      if (message.id === MSG.MOTION_BIAS) motionBias?.onDropped(message);
      if (message.id === MSG.VQF_STATUS || message.id === MSG.VQF_SETTINGS) vqfInit?.onDropped(message);
      if (message.id === MSG.BIAS_HISTORY) biasHist?.onDropped(message);
      badLengthLogged += 1;
      if (badLengthLogged <= 5 || badLengthLogged % 100 === 0) {
        log(`丢弃长度错误的帧 MSG 0x${hex(message.id)}：len=${message.length}，应为 ${message.expected}（累计 ${parser.stats.badLengthFrames}）`);
      }
      break;
    default:
      break;
  }
  updateStats();
}

function feed(bytes) {
  parser.push(bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes));
}

// 读循环：reader 为局部变量并由循环自己 releaseLock；外部只通过 reader.cancel() 请求退出
async function readLoop(p) {
  while (running && port === p && p.readable) {
    let r;
    try {
      r = p.readable.getReader();
    } catch (error) {
      if (running && port === p) handlePortGone(`读取失败：${error.message}`);
      return;
    }
    reader = r;
    try {
      for (;;) {
        const { value, done } = await r.read();
        if (done || !running || port !== p) break;
        if (value && value.length) feed(value);
      }
    } catch (error) {
      if (running && port === p && RECOVERABLE_READ_ERRORS.has(error?.name)) {
        log(`串口读取可恢复错误：${error.name}`);
        continue; // finally 释放旧 reader，下一轮重新取 reader
      }
      if (running && port === p) handlePortGone(`读取中断：${error.message}`);
      return;
    } finally {
      try { r.releaseLock(); } catch { /* 已释放 */ }
      if (reader === r) reader = null;
    }
    if (running && port === p) handlePortGone('串口数据流已结束');
    return;
  }
}

// 关闭顺序：取消读取 → 等读循环释放 reader → 等待/中止写入并释放 writer → 关闭端口
// closeBudgetMs > 0（USB 升级）：cancel、读循环、abort、close 每一步都有时间上限，旧 CDC 句柄不会无限占住端口
async function shutdownPort(p, r, w, pendingWrites, closeBudgetMs = 0) {
  const within = (promise) => (closeBudgetMs > 0
    ? Promise.race([promise, new Promise((resolve) => setTimeout(resolve, closeBudgetMs))]) : promise);
  try { if (r) await within(r.cancel()); } catch (error) { log(`关闭读取器：${error.message}`); }
  try { await within(readLoopDone); } catch { /* 读循环异常已处理 */ }
  await Promise.race([pendingWrites, new Promise((resolve) => setTimeout(resolve, closeBudgetMs > 0 ? Math.min(300, closeBudgetMs) : 300))]);
  try { if (w) await within(w.abort()); } catch { /* 已关闭 */ }
  try { if (w) w.releaseLock(); } catch (error) { log(`释放写入器：${error.message}`); }
  try { await within(p.close()); } catch (error) { log(`关闭串口：${error.message}`); }
}

function teardown(closeBudgetMs = 0) {
  const p = port;
  if (!p) return portClosing;
  running = false;
  tempPoller.reset(); tempAt = 0;
  const r = reader;
  const w = writer;
  const pendingWrites = writes.settled();
  port = null;
  writer = null;
  writes.reset();
  const closing = shutdownPort(p, r, w, pendingWrites, closeBudgetMs);
  portClosing = closing.catch(() => {});
  setting = false;
  probingDirty = false;
  pendingImmediateReset = false;
  pendingExitAfterMode = false;
  clearTimeout(outputAckTimer); pendingOutput = null;
  clearTimeout(rateAckTimer); pendingRate = null; rateFormDirty = false;
  clearTimeout(startupAckTimer); pendingStartup = null; startupFormDirty = false; startupFormRevision++;
  deviceConfig = null; selectionMask = null;
  clearTimeout(canAckTimer); pendingCan = null; canConfig = null;
  canFormDirty = false; canFormRevision = 0; updateCanDraftUI();
  $('canConfigState').textContent = '等待设备读取';
  $('canConfigSummary').textContent = '尚未读取 CAN 配置；旧固件需升级才能使用。';
  $('canSavedSummary').textContent = '';
  resetParser();
  $('parseMode').disabled = false; $('justChannels').disabled = false;
  $('configState').textContent = '等待配置查询';
  updateSettingUI();
  setConnected(false);
  return portClosing;
}

// 拔出 / 致命读错误：UI 立即复位，端口在后台按顺序关闭（重连会等待关闭完成）
function handlePortGone(reason) {
  if (!port) return;
  if (firmwareBusy) { void teardown(portTransport(port) === 'usb' ? 700 : 0); return; }
  if (!reconnectPlan && rememberedConnection) armRestartReconnect();
  void teardown();
  if (reconnectPlan) {
    if (reconnectPlan.phase === 'armed') beginRestartReconnect();
    else { reconnectPlan.phase = 'waiting'; queueRestartReconnect(reconnectPlan, 500); }
    say(`串口已断开：${reason}；等待原串口自动重连`);
  } else say(`串口已断开：${reason}`);
}

function cancelRestartReconnect() {
  ++connectionGeneration; clearTimeout(reconnectTimer); reconnectTimer = null; reconnectPlan = null;
}
function armRestartReconnect() {
  cancelRestartReconnect();
  if (!rememberedConnection) return;
  reconnectPlan = { ...rememberedConnection, parseMode: parseModeValue, justChannels,
    generation: connectionGeneration, phase: 'armed', deadline: performance.now() + 90000 };
}
function queueRestartReconnect(plan, delay) {
  if (reconnectPlan !== plan) return;
  clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(() => void retryRestartConnection(plan), delay);
}
function beginRestartReconnect() {
  const plan = reconnectPlan;
  if (!plan || plan.phase !== 'armed') return;
  plan.phase = 'waiting';
  clearTimeout(reconnectTimer);
  reconnectTimer = setTimeout(async () => {
    if (reconnectPlan !== plan) return;
    await teardown();
    if (reconnectPlan === plan) queueRestartReconnect(plan, 500);
  }, 150);
}
async function retryRestartConnection(plan) {
  if (reconnectPlan !== plan) return;
  if (performance.now() >= plan.deadline) {
    cancelRestartReconnect(); setConnected(!!port);
    say(port ? '原串口已重连，但设备未响应，请检查设备启动状态' : '原串口重连超时，请检查设备后重新连接');
    return;
  }
  if (connecting) { queueRestartReconnect(plan, 500); return; }
  if (!port) {
    await connect(plan);
    if (reconnectPlan !== plan) return;
    if (!port) { queueRestartReconnect(plan, 700); return; }
    plan.phase = 'confirming';
  }
  if (deviceConfig || fusionHz !== null) {
    cancelRestartReconnect(); setConnected(true);
    say('已自动重连原串口，并重新读取设备状态');
    return;
  }
  await send(CMD.QUERY_CONFIG); await send(CMD.QUERY);
  if (reconnectPlan === plan) queueRestartReconnect(plan, 700);
}

async function connect(autoConnection = null) {
  if (firmwareBusy && !autoConnection?.firmware) return false;
  if (!('serial' in navigator)) {
    say('当前浏览器不支持 Web Serial，请使用最新版 Chrome/Edge');
    return;
  }
  if (port || connecting) return false;
  if (!autoConnection) cancelRestartReconnect();
  const generation = connectionGeneration;
  connecting = true;
  setConnected(false);
  let picked = null;
  try {
    await portClosing; // 上一次断开/拔出的关闭流程完成后才能重开
    if (generation !== connectionGeneration) return false;
    // Reuse the exact granted SerialPort object; never select by VID/PID or
    // choose another device, and never reopen the picker during a restart.
    picked = autoConnection?.port || await navigator.serial.requestPort();
    const fwReconnect = !!autoConnection?.firmware;
    if (picked.readable || picked.writable) {
      const closing = (async () => { try { await picked.close(); } catch { /* 未打开 */ } })();
      await (fwReconnect ? Promise.race([closing, new Promise((resolve) => setTimeout(resolve, 700))]) : closing);
    }
    const baudRate = autoConnection?.baudRate || Number($('baud').value);
    const openOptions = { baudRate, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none', bufferSize: SERIAL_BUFFER_SIZE };
    if (fwReconnect) await openPortBounded(picked, openOptions, FW_RECONNECT_OPEN_MS);
    else await picked.open(openOptions);
    if (generation !== connectionGeneration) { await picked.close(); return false; }
    if (autoConnection) {
      parseModeValue = autoConnection.parseMode; justChannels = autoConnection.justChannels;
      $('baud').value = String(baudRate); $('parseMode').value = parseModeValue; $('justChannels').value = String(justChannels);
    }
    rememberedConnection = { port:picked, baudRate, parseMode:parseModeValue, justChannels };
    port = picked;
    writes.reset();
    resetParser();
    parser.resetStats();
    badLengthLogged = 0;
    lastFrames = 0;
    lastRateAt = performance.now();
    lastPoseAt = 0;
    for (const key of ['yaw', 'pitch', 'roll']) poseTimes[key] = 0;
    lastImuAt = 0;
    lastQuatAt = 0;
    quat = null;
    dataTimedOut = false;
    firstDataSeen = false;
    connectedAt = performance.now();
    fusionHz = null;
    filterProfile?.reset();
    zaruLimits?.reset();
    motionBias?.reset();
    vqfInit?.reset();
    biasHist?.reset();
    clearTimeout(fwVersionTimer); fwVersionTimer = null; fwVersion = { text: null, supported: null }; renderFwVersion();
    deviceModel = { text: null, supported: null }; renderDeviceModel();
    tempPoller.reset(); tempAt = 0;
    writer = picked.writable.getWriter();
    running = true;
    connecting = false;
    setConnected(true);
    say(autoConnection ? '原串口已自动重连，等待设备启动和数据' : '串口已连接，等待数据');
    readLoopDone = readLoop(picked);
    await send(CMD.QUERY_CONFIG);
    await send(CMD.QUERY);
    await send(CMD.QUERY_ACC_CAL);
    await filterProfile?.query();
    await zaruLimits?.query(); // 连接后与姿态稳定性一起查询阈值
    await motionBias?.query();
    await vqfInit?.query(); // vqfinit1：0x2C + 0x29（3 s 无数据帧 → 隐藏）
    await queryFwVersion(); // fwver1：0x23 → 0x32
    await biasHist?.read(); // biashist1：0x33 → 0x34 整表
  } catch (error) {
    connecting = false;
    if (autoConnection?.firmware) log(`升级重连暂不可用，继续等待：${error.message}`);
    else if (autoConnection) {
      if (reconnectPlan === autoConnection && autoConnection.lastError !== error.message) {
        autoConnection.lastError = error.message; log(`原串口暂不可用，继续等待：${error.message}`);
      }
    } else say(`连接失败：${error.message}`);
    if (port) await teardown();
    else if (picked && (picked.readable || picked.writable)) { try { await picked.close(); } catch { /* ignore */ } }
  } finally {
    connecting = false;
    setConnected(!!port);
  }
  return !!picked && port === picked;
}

async function disconnect(showMessage = true) {
  if (firmwareBusy) return;
  cancelRestartReconnect();
  const closing = teardown();
  setConnected(false);
  if (showMessage) say('串口已断开');
  await closing;
}

function updateFirmwareLink() {
  if (firmwareBusy) return; // 升级过程中不改写，保持开始时判断的链路
  const node = $('firmwareLink');
  if (!node) return;
  const t = portTransport(port);
  const text = t ? `当前链路：${t === 'usb' ? 'USB' : 'UART'}` : '当前链路：未连接';
  if (node.textContent !== text) node.textContent = text;
  if (node.dataset.transport !== (t || '')) node.dataset.transport = t || '';
}
function updateFirmwareUI() {
  updateFirmwareLink();
  $('firmwareFile').disabled = firmwareBusy || firmwareChoosingPort;
  $('firmwareRecovery').disabled = firmwareBusy || firmwareChoosingPort;
  $('firmwarePortBtn').hidden = !$('firmwareRecovery').checked;
  $('firmwarePortBtn').disabled = firmwareBusy || firmwareChoosingPort || !!port || connecting || !!reconnectPlan;
  $('firmwareStartBtn').disabled = firmwareBusy || firmwareChoosingPort || !firmwareImage || connecting || !!reconnectPlan ||
    (!running && !$('firmwareRecovery').checked) || accStartPending || accCalibration?.status === 1 ||
    !!pendingStartup || !!pendingOutput || !!pendingCan || !!pendingRate;
  $('firmwareStopBtn').disabled = !firmwareBusy || firmwareStopRequested || !['enter','hello','erase','write'].includes(firmwarePhase);
  // Inert blocks both pointer and keyboard edits; periodic configuration callbacks may still update controls.
  document.querySelectorAll('[data-fw-inert], #canPanel').forEach(node=>{node.inert=firmwareBusy;});
}
function firmwareStatus(text, phase=firmwarePhase) {
  firmwarePhase=phase; $('firmwareStatus').textContent=text; updateFirmwareUI();
}
async function selectFirmwareFile() {
  const generation=++firmwareFileGeneration, file=$('firmwareFile').files[0];
  firmwareImage=null;$('firmwareProgress').value=0;updateFirmwareUI();
  if(!file) {$('firmwareInfo').textContent='尚未选择固件。';return;}
  try {
    if(!/\.bin$/i.test(file.name) || file.size<8 || file.size>firmware.APP_SIZE)
      throw new Error('请选择本板应用 .bin，最大 208 KiB；不接受 Bootloader、整片 Flash 或压缩包。');
    const data=new Uint8Array(await file.arrayBuffer()), info=firmware.validateImage(data);
    const digest=await crypto.subtle.digest('SHA-256',data);
    const sha256=[...new Uint8Array(digest)].map(b=>hex(b)).join('');
    if(generation!==firmwareFileGeneration || firmwareBusy) return;
    firmwareImage={name:file.name,data,...info,sha256};
    $('firmwareInfo').textContent=`${file.name} · ${info.bytes.toLocaleString()} 字节\nCRC32 ${hex(info.crc32,8)}\nSHA256 ${sha256}`;
    firmwareStatus('固件文件检查通过。确认文件适用于当前板卡后点击开始升级。','ready');
  } catch(error) {
    if(generation!==firmwareFileGeneration) return;
    $('firmwareInfo').textContent='固件文件检查失败。';firmwareStatus(error.message,'invalid');
  }
}
const firmwareDelay = ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function firmwareBounded(promise, ms, text) {
  let timer;
  try {return await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(text)),ms);})]);}
  finally {clearTimeout(timer);}
}
// USB 重新枚举后找回同一块板子：只认点击“开始升级”时尚未授权（对象引用不在 seen 中）的新 SerialPort。
// 不按 VID/PID 退回 same[0]，也不使用点击前已授权的另一块相同 VID/PID 的板子；getPorts 失败时继续用当前口。
async function usbUpgradePort(current, info, seen, origin = null) {
  let ports;
  try { ports = await navigator.serial.getPorts(); } catch (error) { return current; }
  const same = ports.filter((p) => {
    try { const i = p.getInfo(); return i.usbVendorId === info.usbVendorId && i.usbProductId === info.usbProductId; }
    catch { return false; }
  });
  const fresh = same.filter((p) => !seen.includes(p));
  if (fresh.length >= 2) throw new Error(FW_USB_MULTI_TEXT);
  if (fresh.length === 1) return fresh[0];
  if (same.includes(current)) return current;
  if (origin && same.includes(origin)) return origin;
  return current;
}
// UART（usb=null）：只重开原 SerialPort 对象 15 秒，不调用 getPorts。
// USB（usb={info,seen,origin}）：每轮经 usbUpgradePort 选口，最多 25 秒。
async function openFirmwarePort(selected, usb = null) {
  const info=selected.getInfo(), deadline=performance.now()+(usb?25000:15000);
  let candidate=selected;
  while(performance.now()<deadline) {
    if(firmwareStopRequested) throw new Error('已停止进入升级模式；应用尚未擦除。');
    if(usb) {
      candidate=await usbUpgradePort(candidate,usb.info,usb.seen,usb.origin); // 多块新设备：直接抛出，不重试
      if(!usb.seen.includes(candidate)) usb.lastFresh=candidate;
    } else {
      const current=selected.getInfo();
      if(current.usbVendorId!==info.usbVendorId || current.usbProductId!==info.usbProductId)
        throw new Error('原串口身份发生变化，已停止升级。');
    }
    const target=candidate;
    let client;
    try {
      await target.open({baudRate:2000000,dataBits:8,stopBits:1,parity:'none',flowControl:'none',bufferSize:SERIAL_BUFFER_SIZE});
      client=new firmware.BootClient(target).start();
      const base=await client.request(firmware.COMMAND.HELLO,0,0,0,0,new Uint8Array(),600);
      if(base!==firmware.APP_BASE) throw new Error('Bootloader 应用分区不匹配，已停止；没有擦除固件。');
      return client;
    } catch(error) {
      const release=async()=>{
        if(client) await client.close();
        else if(target.readable || target.writable) await target.close();
      };
      if(usb) {try {await release();} catch(closeError) {log(closeError.message);}}
      else await release();
      if(error.message.includes('分区不匹配') || error.message.includes('Bootloader 拒绝') || error.message===FW_USB_MULTI_TEXT) throw error;
      await firmwareDelay(200);
    }
  }
  throw new Error(usb ? FW_USB_TIMEOUT_TEXT : FW_UART_TIMEOUT_TEXT);
}
async function restoreAfterFirmware(plan, usb = null) {
  firmwareRestoring=true;firmwarePingAt=0;firmwareReconnectPort=null;
  firmwareStatus(usb?'固件已校验，等待应用启动并重新连接 USB…':'固件已校验，等待应用启动并重新连接原串口…','restart');
  const restoreMs=Number(globalThis.__gyroTest?.restoreMs)||FW_RESTORE_MS; // 测试钩子；生产为 90 秒
  const deadline=performance.now()+restoreMs;
  let adopted=null, ambiguous=false;
  const eventPorts=[];
  // USB：应用 CDC 重新枚举时 Chrome 发 connect 事件；getPorts() 可能仍只给出已失效的 Bootloader 对象。
  // 只采纳同 VID/PID 的口；点击前已授权（usbPortsBefore）且不是 origin 的口是另一块板子，忽略。
  const onConnect=(event)=>{
    const p=event?.port || event?.target;
    if(!p || typeof p.getInfo!=='function') return;
    let i; try {i=p.getInfo();} catch {return;}
    if(i?.usbVendorId!==plan.info.usbVendorId || i?.usbProductId!==plan.info.usbProductId) return;
    if(usb.before.includes(p) && p!==usb.origin) return;
    // 校验完成后同时冒出多块新的同 VID/PID 口：不再采纳任何一个，只保持当前口，不抛掉整个升级
    if(!eventPorts.includes(p)) eventPorts.push(p);
    if(eventPorts.length>1) {
      if(!ambiguous) {ambiguous=true;log(`${FW_USB_MULTI_TEXT}（镜像已写完，保持当前 USB 端口）`);}
      adopted=null;firmwareReconnectPort=null;return;
    }
    adopted=p;firmwareReconnectPort=p;
    if(!usb.seen.includes(p)) usb.seen.push(p);
    log('升级重连：已发现重新枚举的 USB 端口');
  };
  if(usb) navigator.serial.addEventListener('connect',onConnect);
  try {
    while(performance.now()<deadline) {
      if(usb && adopted && port && port!==adopted) {
        // 已采纳新端口，但当前打开的是旧对象：放掉旧对象，下一轮打开 adopted
        teardown(700);await Promise.race([portClosing,firmwareDelay(800)]);
      }
      if(usb && !port && !connecting) {
        if(adopted) plan.port=adopted; // 一旦采纳，不再换回 usbSeen 里的旧口
        else if(!ambiguous) {
          // 还没有 connect 事件：只允许换成尚未在 usbSeen 中的新口；镜像已写完，多设备错误只记录并保持当前口
          try {
            const next=await usbUpgradePort(plan.port,plan.info,usb.seen,usb.origin);
            if(next!==plan.port && !usb.seen.includes(next)) {usb.seen.push(next);plan.port=next;}
          } catch(error) {
            if(!usb.multiLogged) {usb.multiLogged=true;log(`${error.message}（镜像已写完，保持在最后确认的 USB 端口）`);}
          }
        }
      }
      // connect() 在调用时复制 plan.port；挂起的 open 由 openPortBounded（2 秒 / 新端口）结束，下一轮再打开 adopted
      if(!port && !connecting) await connect({...plan,firmware:true});
      if(port) {
        await send(CMD.PING);await send(CMD.QUERY_CONFIG);await send(CMD.QUERY);
        await firmwareDelay(300);
        if(firmwarePingAt>0) return true;
      } else await firmwareDelay(200);
    }
    return false;
  } finally {
    firmwareRestoring=false;
    if(usb) navigator.serial.removeEventListener('connect',onConnect);
    firmwareReconnectPort=null;
  }
}
async function startFirmwareUpgrade() {
  if(firmwareBusy || firmwareChoosingPort || !firmwareImage || connecting || reconnectPlan) return;
  if(accStartPending || accCalibration?.status===1) {firmwareStatus('请先取消正在进行的六面校准。');return;}
  if(pendingStartup || pendingOutput || pendingCan || pendingRate) {firmwareStatus('请等待当前设备操作完成。');return;}
  if(startupFormDirty || canFormDirty || rateFormDirty) {firmwareStatus('请先保存或撤销尚未应用的配置修改。');return;}
  const recovery=$('firmwareRecovery').checked, image=firmwareImage;
  let selected=port || (recovery ? firmwareRecoveryPort : null), client=null, eraseSent=false, entered=false;
  if(!selected && !recovery) {firmwareStatus('请先连接设备。');return;}
  // Only an explicit recovery click can open a picker. Re-enumeration never chooses a similar device.
  if(!selected) {
    firmwareChoosingPort=true;updateFirmwareUI();
    try {selected=await navigator.serial.requestPort();} catch(error) {firmwareStatus(`未选择恢复端口：${error.message}`);return;}
    finally {firmwareChoosingPort=false;updateFirmwareUI();}
    if(firmwareBusy) return;
  }
  const info=selected.getInfo(), transport=firmwareTransport(info);
  const plan={port:selected,info,baudRate:rememberedConnection?.port===selected?rememberedConnection.baudRate:Number($('baud').value),parseMode:parseModeValue,justChannels};
  let usb=null;
  firmwareBusy=true;firmwareStopRequested=false;firmwareRecoveryPort=selected;cancelRestartReconnect();
  $('firmwareProgress').value=0;firmwareStatus('正在进入升级模式…','enter');setConnected(!!port);
  log(`固件升级（${transport==='usb'?'USB':'UART'}）：${image.name}，${image.bytes} 字节，CRC32 ${hex(image.crc32,8)}`);
  try {
    if(transport==='usb') {
      // 发 0x16 之前按对象引用记下已授权端口：之后新出现的那个才是同一块板子重新枚举出来的口。
      let seen;
      try {seen=[...await navigator.serial.getPorts()];} catch {seen=[selected];}
      if(!seen.includes(selected)) seen.push(selected);
      usb={info,seen,before:[...seen],origin:selected,lastFresh:null,multiLogged:false};
    }
    if(!recovery) {
      if(!writer || port!==selected) throw new Error('应用串口已断开，请重新连接后升级。');
      await firmwareBounded(writes.write(buildFrame(0x16,seq++ & 255,[])),3000,'发送升级入口命令超时。');
      entered=true;
      if(!usb) await firmwareDelay(400);
    } else entered=true;
    if(usb) {
      // 立刻释放旧 CDC 句柄（每步 700 ms 上限），否则 Windows 可能不重新枚举；最多再等 800 ms。
      teardown(700);
      await Promise.race([portClosing,firmwareDelay(800)]);
    } else {await teardown();await portClosing;}
    if(firmwareStopRequested) throw new Error('已停止进入升级模式；应用尚未擦除。');
    firmwareStatus(usb?'等待 USB 重新枚举并核对 Bootloader…':'等待原串口返回并核对 Bootloader…','hello');
    client=await openFirmwarePort(selected,usb);
    if(usb) {
      plan.port=client.port;firmwareRecoveryPort=client.port;
      if(!usb.seen.includes(client.port)) usb.seen.push(client.port);
    }
    await firmware.transferImage(client,image.data,{
      shouldStop:()=>firmwareStopRequested,
      onProgress:progress=>{
        if(progress.phase==='erase') {eraseSent=true;firmwareStatus('正在擦除应用区域，请保持供电…','erase');}
        else if(progress.phase==='write') {
          $('firmwareProgress').value=Math.floor(progress.offset*90/progress.total);
          firmwareStatus(`已确认 ${progress.offset.toLocaleString()} / ${progress.total.toLocaleString()} 字节，正在上传…`,'write');
        } else firmwareStatus('正在校验完整固件…','verify');
      },
    });
    $('firmwareProgress').value=95;firmwareStatus('整镜像校验通过，正在启动应用…','restart');
    try {await client.request(firmware.COMMAND.BOOT);}
    catch(error) {log(`启动应答未确认，改为检查应用响应：${error.message}`);}
    await client.close();client=null;
    if(await restoreAfterFirmware(plan,usb)) {
      $('firmwareProgress').value=100;$('firmwareRecovery').checked=false;firmwareRecoveryPort=null;
      firmwareStatus(usb?'升级完成：整镜像校验通过，应用已响应，USB 已重新连接。':'升级完成：整镜像校验通过，应用已响应，原串口已重新连接。','complete');
      say(usb?'固件升级完成，USB 已重新连接':'固件升级完成，已重新连接原串口');
      fwVersionRequery = true; // fwver1：升级后的新应用版本
    } else {
      await teardown();
      firmwareStatus(FW_UNCONFIRMED_TEXT,'unconfirmed'); // 进度保持 95%
    }
  } catch(error) {
    if(client) {try {await client.close();} catch(closeError) {log(closeError.message);} client=null;}
    await teardown();
    if(entered) $('firmwareRecovery').checked=true;
    // USB 且尚未擦除：恢复上传优先用重新枚举后出现的那个端口（旧对象已失效）。
    if(usb && !eraseSent && usb.lastFresh) firmwareRecoveryPort=usb.lastFresh;
    const notErased=error.message.includes('应用尚未擦除') && usb;
    firmwareStatus(`${error.message}${eraseSent?' 上传未完成，未发送应用启动命令；请保持供电，在恢复模式重新完整上传。':entered&&!notErased?' 应用尚未擦除；设备可能已在升级模式，可使用恢复上传。':''}`,'failed');
    log(`固件升级停止：${error.message}`);
  } finally {
    firmwareBusy=false;firmwareRestoring=false;setConnected(!!port);updateFirmwareUI();
  }
}

function updateSettingUI() {
  $('settingsState').textContent = setting ? '设置模式已进入' : '未进入设置模式';
  $('settingsState').className = `badge ${setting ? 'on' : ''}`;
  $('modeField').disabled = !settingEditable() || !!pendingStartup;
  $('applyBtn').disabled = !settingEditable() || !!pendingStartup;
  $('exitBtn').disabled = !setting;
  $('enterBtn').disabled = setting;
  updateOutputControls();
  updateAccCalibrationUI();
  updateStartupDraftUI();
  // 固件 AHRS_CMD_SET_CAN_NODE_ID 用"当前运行的"融合模式重写设置记录，会覆盖已保存但未重启生效的新融合模式
  const canBlocked = (!deviceConfig && (fusionPendingRestart || dirtyUnknown)) || !!pendingCan;
  $('canIdBtn').disabled = canBlocked;
  $('canNodeId').disabled = canBlocked;
  $('canHint').textContent = !deviceConfig && fusionPendingRestart
    ? '融合模式已保存，请先重启设备后再修改 CAN ID'
    : (!deviceConfig && dirtyUnknown ? '设备存在已保存但尚未应用的配置，请先重启设备后再修改 CAN ID' : '');
  $('restartHint').textContent = fusionPendingRestart ? (deviceConfig ? '启动配置已保存，将在下次设备重启后生效' : '融合模式已保存，将在下次设备重启后生效') : '';
  filterView?.render();
  zaruView?.render();
  biasView?.render();
  vqfView?.render();
  biasHistView?.render();
  updateFirmwareUI();
}

function selectedMode() {
  return Number(document.querySelector('input[name="fusion"]:checked').value);
}

function startupValidationError() {
  if (!deviceConfig) return '';
  const combined = deviceConfig.version >= 4, fast = $('fastStart').checked;
  const supportsDuration = deviceConfig.version >= 2 && !!(deviceConfig.capabilities & 16);
  if (deviceConfig.version >= 3 && (deviceConfig.capabilities & 32) && !GYRO_RANGES.includes(Number($('gyroRange').value))) return '请选择有效的陀螺仪量程';
  if (!fast && !supportsDuration) return '当前固件不支持普通启动零偏采样，请先升级配套固件';
  if (supportsDuration && (combined || !fast)) {
    const text = $('gyroInitSeconds').value.trim(), seconds = Number(text), ms = Math.round(seconds * 1000);
    if (!text || !Number.isFinite(seconds) || seconds < 0 || seconds > 60 || ms < (combined ? 0 : 100) || ms > 60000)
      return combined ? '启动窗口须为 0～60 秒（默认 2 秒）' : '初始化零偏时长须为 0.1～60 秒（默认 2 秒）';
  }
  return '';
}
async function applyMode() {
  const validationError = startupValidationError();
  if (validationError) { say(validationError); return; }
  if (!setting) {
    say('请先进入设置模式');
    return;
  }
  if (pendingStartup) return;
  const immediate = $('restartNow').checked;
  const combined = deviceConfig?.version >= 4;
  let fast = $('fastStart').checked ? 1 : 0;
  const supportsDuration = deviceConfig?.version >= 2 && !!(deviceConfig.capabilities & 16);
  const rangeDps = deviceConfig?.version >= 3 && (deviceConfig.capabilities & 32) ? Number($('gyroRange').value) : null;
  if (rangeDps !== null && !GYRO_RANGES.includes(rangeDps)) { say('请选择有效的陀螺仪量程'); return; }
  let initMs = null;
  if (deviceConfig) {
    if (!fast && !supportsDuration) { say('当前固件不支持普通启动零偏采样，请先升级配套固件'); return; }
    if (supportsDuration) {
      const durationText = $('gyroInitSeconds').value.trim();
      const seconds = Number(durationText);
      initMs = Math.round(seconds * 1000);
      if (!durationText || !Number.isFinite(seconds) || seconds < 0 || seconds > 60 || initMs < (combined ? 0 : 100) || initMs > 60000) {
        if (combined || !fast) { say(combined ? '启动窗口须为 0～60 秒（默认 2 秒）' : '初始化零偏时长须为 0.1～60 秒（默认 2 秒）'); return; }
        initMs = deviceConfig.savedInitMs;
      }
    }
    if (combined) fast = initMs === 0 ? 1 : 0;
    startupFormDirty = true;
    pendingStartup = { mode: selectedMode(), fast, initMs, rangeDps, revision: startupFormRevision, acked: false };
    startupAckTimer = setTimeout(() => {
      pendingStartup = null; pendingImmediateReset = false; pendingExitAfterMode = false;
      updateSettingUI(); say('未确认启动设置回读，修改已保留，请读取设备配置核对');
    }, 3000);
    updateSettingUI();
  }
  pendingImmediateReset = immediate;
  if (immediate) armRestartReconnect();
  pendingExitAfterMode = !immediate;
  const payload = [selectedMode(), fast, immediate ? 1 : 0];
  if (initMs !== null) payload.push(initMs & 0xff, initMs >> 8);
  if (rangeDps !== null) payload.push(rangeDps & 0xff, rangeDps >> 8);
  const sent = deviceConfig
    ? await send(CMD.STARTUP, payload)
    : await send(CMD.MODE, [selectedMode(), immediate ? 1 : 0]);
  if (!sent) { cancelRestartReconnect(); clearTimeout(startupAckTimer); pendingStartup = null; pendingImmediateReset = false; pendingExitAfterMode = false; updateSettingUI(); }
  else {
    if (immediate && reconnectPlan?.phase === 'armed') {
      const plan = reconnectPlan;
      reconnectTimer = setTimeout(() => { if (reconnectPlan === plan) beginRestartReconnect(); }, 1000);
    }
    log(immediate ? '等待保存 ACK（随后设备重启并自动重连）' : '等待保存 ACK');
  }
}

async function enterSettings() {
  // 先用 EXIT 探测设备是否仍有待应用配置（非设置模式下 EXIT 无副作用，ACK detail = app_settings_dirty）
  probingDirty = true;
  if (!(await send(CMD.EXIT))) { probingDirty = false; return; }
  await send(CMD.ENTER);
}

async function applyStream() {
  const mode = Number($('streamMode').value);
  const sent = await send(CMD.STREAM, [mode]);
  if (sent) log(`等待数据流切换 ACK（${streamModeName(mode)}）`);
}

async function applyRate() {
  if (pendingRate) return;
  const hz = fusionHz ?? DEFAULT_FUSION_HZ;
  const check = validateOutputHz($('outHz').value, hz);
  if (!check.ok) {
    say(`${check.error}${fusionHz ? '' : `（尚未收到设备状态，按固件默认融合率 APP_FUSION_HZ=${DEFAULT_FUSION_HZ} Hz 校验）`}`);
    return;
  }
  if (deviceConfig?.version >= 3 && (deviceConfig.capabilities & 64)) {
    pendingRate = {hz:check.hz,revision:rateFormRevision,acked:false}; updateOutputControls();
    rateAckTimer = setTimeout(() => {pendingRate=null;updateOutputControls();say('未确认输出频率保存，请刷新状态核对设备');},3000);
  }
  const sent = await send(CMD.OUTPUT_HZ, [check.hz & 0xff, (check.hz >> 8) & 0xff]);
  if (!sent) { clearTimeout(rateAckTimer);pendingRate=null;updateOutputControls(); }
  if (sent) log(`等待输出频率 ACK（${check.hz}Hz）`);
}

function updateRate() {
  const now = performance.now();
  if (now - lastRateAt >= 1000) {
    frameRate = Math.round(lastFrames * 1000 / (now - lastRateAt));
    $('rate').textContent = String(frameRate);
    lastFrames = 0;
    lastRateAt = now;
  }
  requestAnimationFrame(updateRate);
}

function updateStats() {
  const s = parser.stats;
  $('statsLabel').textContent = `JustFloat ${s.justFrames} · 失步 ${s.justResyncs} · 二进制 ${s.binFrames} · 长度错误 ${s.badLengthFrames}`;
}

const POSE_IDS = ['yaw', 'pitch', 'roll', 'yaw2', 'pitch2', 'roll2'];
// 串口连接状态与数据是否在线分开显示
function updateLiveness() {
  const now = performance.now();
  const poseAge = lastPoseAt ? now - lastPoseAt : Infinity;
  const dataAt = Math.max(lastPoseAt, lastImuAt);
  const dataAge = dataAt ? now - dataAt : Infinity;
  const badge = $('dataState');
  let text = '无数据';
  let cls = 'badge';
  if (running && selectionMask === 0) {
    text = '输出已关闭'; cls = 'badge'; dataTimedOut = false;
  } else if (running) {
    if (!dataAt) { text = '等待数据'; cls = 'badge warn'; }
    else if (dataAge > DATA_TIMEOUT_MS) { text = '数据超时'; cls = 'badge off'; }
    else if (poseAge <= DATA_TIMEOUT_MS) { text = '数据正常'; cls = 'badge on'; }
    else { text = selectionMask !== null ? '自选通道数据' : 'IMU 数据（无姿态）'; cls = 'badge on'; }
    if (dataAt && !firstDataSeen) { firstDataSeen = true; say(lastPoseAt ? '串口已连接，姿态数据正常' : '串口已连接，已收到数据'); }
    const timedOut = dataAt ? dataAge > DATA_TIMEOUT_MS : now - connectedAt > DATA_TIMEOUT_MS * 4;
    if (timedOut && !dataTimedOut) say(dataAt ? `数据超时：已超过 ${DATA_TIMEOUT_MS} ms 未收到数据（串口仍连接）` : '串口已连接，但尚未收到数据');
    else if (!timedOut && dataTimedOut && dataAt) say('数据已恢复');
    dataTimedOut = timedOut;
  } else {
    dataTimedOut = false;
  }
  if (badge.textContent !== text) badge.textContent = text;
  if (badge.className !== cls) badge.className = cls;
  const stale = poseAge > POSE_STALE_MS;
  for (const id of POSE_IDS) {
    const stamp = poseTimes[id.replace('2', '')];
    $(id).classList.toggle('stale', !stamp || now - stamp > POSE_STALE_MS);
  }
  const canvas = $('attitude');
  if (canvas.classList.contains('stale') !== stale) { canvas.classList.toggle('stale', stale); scheduleDraw(); }
}

function val(id, value) {
  $(id).textContent = Number.isFinite(value) ? Number(value).toFixed(4) : '--';
}

function valDeg(id, value) { // quick1：首页 Yaw/Pitch/Roll 三位小数
  $(id).textContent = Number.isFinite(value) ? Number(value).toFixed(3) : '--';
}

function currentRotation() {
  if (quat && performance.now() - lastQuatAt <= QUAT_PREFER_MS) return { R: quatToMatrix(...quat), source: '四元数' };
  if (selectionMask !== null && (selectionMask & 7) !== 7) return { R: eulerToMatrix(0, 0, 0), source: '请选齐 Yaw/Pitch/Roll 查看姿态' };
  return { R: eulerToMatrix(pose.yaw || 0, pose.pitch || 0, pose.roll || 0), source: '欧拉角 ZYX' };
}

function drawArrow(context, from, to, color, label) {
  context.strokeStyle = color; context.fillStyle = color; context.lineWidth = 3;
  context.beginPath(); context.moveTo(from[0], from[1]); context.lineTo(to[0], to[1]); context.stroke();
  const angle = Math.atan2(to[1] - from[1], to[0] - from[0]);
  context.beginPath();
  context.moveTo(to[0], to[1]);
  context.lineTo(to[0] - 11 * Math.cos(angle - 0.4), to[1] - 11 * Math.sin(angle - 0.4));
  context.lineTo(to[0] - 11 * Math.cos(angle + 0.4), to[1] - 11 * Math.sin(angle + 0.4));
  context.closePath(); context.fill();
  context.font = 'bold 14px ui-monospace,monospace';
  context.fillText(label, to[0] + 6 * Math.cos(angle) - 4, to[1] + 6 * Math.sin(angle) + 5);
}

// 真 3D：欧拉角（ZYX）/ 四元数 → 旋转矩阵 → 板子顶点 → 透视投影 → Canvas
let pcbView = null;
function render3D() {
  const canvas = $('attitude');
  const context = canvas.getContext('2d');
  const { width, height } = canvas;
  context.clearRect(0, 0, width, height);
  const realModel = pcbView?.ready;
  if (!realModel) { context.fillStyle = '#0d1623'; context.fillRect(0, 0, width, height); }
  const { R, source } = currentRotation();
  const scene = projectBoard(R, width, height);
  const { cam } = scene;
  // 地面网格（世界系 Z = -1.1 平面）
  context.strokeStyle = '#1f3350'; context.lineWidth = 1;
  for (let k = -3; !realModel && k <= 3; k += 1) {
    const a = projectPoint(cam, [k * 0.5, -1.5, -1.1]); const b = projectPoint(cam, [k * 0.5, 1.5, -1.1]);
    const c = projectPoint(cam, [-1.5, k * 0.5, -1.1]); const d = projectPoint(cam, [1.5, k * 0.5, -1.1]);
    context.beginPath(); context.moveTo(a[0], a[1]); context.lineTo(b[0], b[1]); context.moveTo(c[0], c[1]); context.lineTo(d[0], d[1]); context.stroke();
  }
  const stale = canvas.classList.contains('stale');
  context.globalAlpha = stale && running ? 0.35 : 1;
  const colors = { top: [27, 119, 167], bottom: [70, 52, 110], front: [243, 168, 60], back: [60, 80, 110], left: [45, 150, 95], right: [170, 70, 80] };
  for (const face of realModel ? [] : scene.faces) {
    const base = colors[face.name];
    const shade = 0.55 + 0.45 * face.light;
    context.fillStyle = `rgb(${base.map((c) => Math.round(c * shade)).join(',')})`;
    context.strokeStyle = '#9fdcff'; context.lineWidth = 1.5;
    context.beginPath();
    face.points.forEach((point, index) => (index ? context.lineTo(point[0], point[1]) : context.moveTo(point[0], point[1])));
    context.closePath(); context.fill(); context.stroke();
    if (face.name === 'top') {
      // 顶面：芯片 + 指向 +X 的箭头
      const chip = [[-0.25, -0.25], [0.25, -0.25], [0.25, 0.25], [-0.25, 0.25]].map(([x, y]) => projectPoint(cam, mulVec(R, [x - 0.35, y, BOARD_HALF[2]])));
      context.fillStyle = '#0c1a28'; context.beginPath();
      chip.forEach((point, index) => (index ? context.lineTo(point[0], point[1]) : context.moveTo(point[0], point[1])));
      context.closePath(); context.fill();
      const tip = projectPoint(cam, mulVec(R, [0.85, 0, BOARD_HALF[2]]));
      const tail = projectPoint(cam, mulVec(R, [0.1, 0, BOARD_HALF[2]]));
      drawArrow(context, tail, tip, '#ffffff', '');
    }
  }
  // 机体坐标轴
  const screen = realModel ? pcbView.render(R, stale && running) : scene.screen;
  const project = point => realModel ? pcbView.project(point) : projectPoint(cam, point);
  const origin = project([0, 0, 0]);
  const axes = [[[1.65, 0, 0], '#ff6b6b', 'X'], [[0, 1.4, 0], '#4be08a', 'Y'], [[0, 0, 1.0], '#5aa9ff', 'Z']];
  for (const [axis, color, label] of axes) drawArrow(context, origin, project(mulVec(R, axis)), color, label);
  context.globalAlpha = 1;
  context.fillStyle = '#8fa4bf'; context.font = '13px ui-monospace,monospace';
  context.fillText(`${realModel ? '真实板卡' : '3D'} · ${source}${stale && running ? ' · 数据超时' : ''}`, 14, 22);
  const hull = convexHull(screen);
  lastProjection = { hullArea: polygonArea(hull), hull, screen: screen.map((p) => [p[0], p[1]]), faces: scene.faces.map((f) => f.name), source, R, model: realModel ? pcbView.state() : null };
}

function draw() {
  valDeg('yaw', pose.yaw); valDeg('pitch', pose.pitch); valDeg('roll', pose.roll);
  renderTemp();
  val('yaw2', pose.yaw); val('pitch2', pose.pitch); val('roll2', pose.roll);
  if ($('gx')) {
    val('gx', imu.gx); val('gy', imu.gy); val('gz', imu.gz);
    val('ax', imu.ax); val('ay', imu.ay); val('az', imu.az);
  }
  if ($('quat') && quat) {
    $('quat').textContent = quat.map((v) => v.toFixed(3)).join(', ');
  }
  updateStats();
  render3D();
}

for (let portIndex = 0; portIndex < 2; portIndex++) {
  const shortLabels = ['Yaw', 'Pitch', 'Roll', 'Ax', 'Ay', 'Az', 'Gx', 'Gy', 'Gz'];
  $('outputFields' + portIndex).innerHTML = FIELD_LABELS.map((label, bit) => `<label title="${label}"><input type="checkbox" aria-label="${label}" value="${bit}" ${bit < 3 ? 'checked' : ''}> ${shortLabels[bit]}</label>`).join('');
  $('allFields' + portIndex).onclick = () => document.querySelectorAll(`#outputFields${portIndex} input`).forEach((node) => { node.checked = true; });
  $('noFields' + portIndex).onclick = () => document.querySelectorAll(`#outputFields${portIndex} input`).forEach((node) => { node.checked = false; });
  $('outputApply' + portIndex).onclick = () => applyOutput(portIndex);
}
$('connectBtn').onclick = () => connect();
$('disconnectBtn').onclick = () => disconnect();
$('refreshBtn').onclick = async () => { await send(CMD.QUERY_CONFIG); if (deviceConfig?.capabilities & 8) await send(CMD.QUERY_CAN); await send(CMD.QUERY); await filterProfile?.query(); await zaruLimits?.query(); await motionBias?.query(); await vqfInit?.query(); };
$('canApply').onclick = applyCan;
$('canRefresh').onclick = () => send(CMD.QUERY_CAN);
for (const event of ['input', 'change']) $('canPanel').addEventListener(event, (e) => {
  if (!e.target.matches('input, select') || !running || !canConfig) return;
  canFormDirty = true; canFormRevision++; updateCanDraftUI();
});
$('canDiscard').onclick = () => {
  if (!canConfig || pendingCan) return;
  canFormDirty = false; canFormRevision++; fillCanForm(canConfig.active); updateCanDraftUI();
};
$('enterBtn').onclick = enterSettings;
$('exitBtn').onclick = () => send(CMD.EXIT);
$('applyBtn').onclick = applyMode;
for (const event of ['input', 'change']) $('startupControls').addEventListener(event, (e) => {
  if (!e.target.matches('#fastStart, #gyroInitSeconds, #gyroRange, input[name="fusion"]') || !deviceConfig || pendingStartup) return;
  startupFormDirty = true; startupFormRevision++; updateSettingUI();
});
$('startupDiscard').onclick = () => {
  if (!deviceConfig || pendingStartup) return;
  startupFormDirty = false; startupFormRevision++; fillStartupForm(deviceConfig); updateSettingUI();
};
$('streamApplyBtn').onclick = applyStream;
$('rateApplyBtn').onclick = applyRate;
$('outHz').addEventListener('input', () => {rateFormDirty=true;rateFormRevision++;});
$('canFrequency').addEventListener('input', () => { const ms=canPeriodFromHz($('canFrequency').value);$('canPeriod').value=ms??'';updateCanDraftUI(); });
$('canPeriod').addEventListener('input', () => { const ms=Number($('canPeriod').value);$('canFrequency').value=ms>0?Number((1000/ms).toFixed(6)):'';updateCanDraftUI(); });
$('canIdBtn').onclick = async () => {
  if (!setting) { say('请先进入设置模式'); return; }
  if (!deviceConfig && (fusionPendingRestart || dirtyUnknown)) { say($('canHint').textContent); return; }
  const value = Number($('canNodeId').value);
  if (!Number.isInteger(value) || value < 0 || value > 0x7ff) { say('CAN 节点 ID 范围为 0~2047'); return; }
  await send(CMD.CAN_ID, [value & 0xff, (value >> 8) & 0xff]);
};
$('acc6Btn').onclick = startAccCalibration;
$('accCancelBtn').onclick = () => send(CMD.CANCEL_ACC_CAL);
$('accRefreshBtn').onclick = () => send(CMD.QUERY_ACC_CAL);
$('pingBtn').onclick = () => send(CMD.PING);
$('clearLog').onclick = () => { $('log').textContent = ''; };
$('firmwareFile').onchange = selectFirmwareFile;
$('firmwareRecovery').onchange = updateFirmwareUI;
$('firmwareStartBtn').onclick = startFirmwareUpgrade;
$('firmwareStopBtn').onclick = () => {
  firmwareStopRequested=true;
  firmwareStatus('正在完成当前步骤后停止上传；设备将保留在升级模式。');
};
$('firmwarePortBtn').onclick = async () => {
  if(firmwareBusy || firmwareChoosingPort || port || connecting || reconnectPlan) return;
  firmwareChoosingPort=true;updateFirmwareUI();
  try {firmwareRecoveryPort=await navigator.serial.requestPort();firmwareStatus('已选择恢复端口，可开始完整上传。');}
  catch(error) {firmwareStatus(`未选择恢复端口：${error.message}`);}
  finally {firmwareChoosingPort=false;updateFirmwareUI();}
};
window.addEventListener('beforeunload',event=>{if(firmwareBusy){event.preventDefault();event.returnValue='';}});
$('parseMode').onchange = (event) => { parseModeValue = event.target.value; resetParser(); };
$('justChannels').onchange = (event) => { justChannels = Number(event.target.value); resetParser(); };
if (filterProfile) filterView = GyroFilter.bindDom(filterProfile, { editable: () => settingEditable(), connected: () => running && !firmwareBusy, note: false });
// bias1：零角速保持阈值（保存由 ui-shell.js 自动设置模式门包装：0x17 → 0x2F → ACK + 0x0F 回读 → 0x18）
if (zaruLimits) {
  zaruView = GyroZaru.bindDom(zaruLimits, { editable: () => settingEditable(), connected: () => running && !firmwareBusy });
  // 读取姿态稳定性时一起读取阈值与运动零偏（被动回读，不覆盖未保存的阈值修改）
  const readFilter = $('filterRead').onclick;
  $('filterRead').onclick = () => { readFilter?.(); void zaruLimits.query(); void motionBias?.query(); };
  zaruView.render();
}
if (motionBias) { biasView = GyroBias.bindDom(motionBias, { connected: () => running && !firmwareBusy }); biasView.render(); }
if (biasHist) { biasHistView = GyroBiasHist.bindDom(biasHist, { connected: () => running && !firmwareBusy }); biasHistView.render(); }
if (vqfInit) { vqfView = GyroVqf.bindDom(vqfInit, { editable: () => settingEditable(), connected: () => running && !firmwareBusy }); vqfView.render(); }
resetParser(); updateSettingUI(); draw(); updateRate();
import('/pcb-view.js?v=20261001fw1-app2').then(async ({ createPCBView }) => {
  const availability = ready => {
    $('modelStatus').textContent = ready ? '真实板卡 · 拖动旋转视角，滚轮 / 双指缩放' : '真实模型暂不可用，显示简化姿态模型';
    $('modelReset').disabled = !ready;
    scheduleDraw();
  };
  pcbView = await createPCBView($('attitude').parentElement, scheduleDraw, availability);
  $('attitude').parentElement.classList.add('model-ready');
  availability(true);
  $('modelReset').onclick = () => pcbView.reset();
  scheduleDraw();
}).catch(() => {
  $('modelStatus').textContent = '真实模型暂不可用，显示简化姿态模型';
  scheduleDraw();
});
setInterval(updateLiveness, 100);
setInterval(() => { if (fwVersionRequery && running && writer && !firmwareBusy && !firmwareRestoring) { fwVersionRequery = false; void queryFwVersion(); } }, 300);
setInterval(pollAccCalibration, 500);
if ('serial' in navigator) navigator.serial.addEventListener('disconnect', (event) => { if (port && event.target === port) handlePortGone('设备已拔出'); });

// 测试钩子（无头浏览器检查用；页面逻辑不依赖它）
if (typeof globalThis !== 'undefined') {
  globalThis.__feed = feed;
  globalThis.__state = () => ({ pose, imu, quat, streamMode, setting });
  globalThis.__gyro = {
    feed, filter: filterProfile, zaru: zaruLimits, bias: motionBias, vqf: vqfInit, biasHist, fwVersion: () => ({ ...fwVersion }), deviceModel: () => ({ ...deviceModel }), firmwareTransport, tempPoller, tempPollAllowed, tempAge: () => (tempAt ? performance.now() - tempAt : Infinity),
    state: () => ({
      pose: { ...pose }, imu: { ...imu }, quat, streamMode, setting, parseMode: parseModeValue, justChannels, fusionHz,
      fusionPendingRestart, dirtyUnknown, running, deviceConfig, startupFormDirty, pendingStartup, canConfig, pendingCan, canFormDirty, selectionMask, accCalibration, accStartPending, stats: { ...parser.stats }, buffered: parser.buffered, lastPoseAt, lastImuAt,
      reconnecting: !!reconnectPlan, reconnectPhase: reconnectPlan?.phase || null,
      firmware: {busy:firmwareBusy,phase:firmwarePhase,restoring:firmwareRestoring,imageBytes:firmwareImage?.bytes||0,transport:portTransport(port),recoveryPortSet:!!firmwareRecoveryPort,openBoundedCalls:firmwareOpenBoundedCalls,lastBoundedAbort:firmwareLastBoundedAbort,reconnectPortSet:!!firmwareReconnectPort,progress:Number($('firmwareProgress').value)},
    }),
    projection: () => lastProjection,
    model: () => pcbView?.state() || null,
  };
}
