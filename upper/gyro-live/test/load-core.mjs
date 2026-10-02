// 从 src/public/app.js 抽取 "GYRO-CORE" 段（纯逻辑，无 DOM）实例化：测试的就是线上同一份代码。
// 另提供按固件 protocol.h / protocol.c 独立实现的打包工具（不复用被测代码）。
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

export const root = join(dirname(fileURLToPath(import.meta.url)), "..");

export function extractBlock(src, begin, end) {
  const a = src.indexOf(begin), b = src.indexOf(end);
  if (a < 0 || b < 0 || b < a) throw new Error(`markers not found: ${begin} / ${end}`);
  return src.slice(src.indexOf("\n", a) + 1, b);
}

export const CORE_NAMES = [
  "SYNC_A", "SYNC_B", "JUST_TAIL", "MAX_PAYLOAD", "DEFAULT_FUSION_HZ", "SERIAL_BUFFER_SIZE", "MSG", "PAYLOAD_LEN", "CMD",
  "crc16", "buildFrame", "streamParserConfig", "validateOutputHz", "outputHzDivisors", "decodePayload", "ackDetailText",
  "FIELD_NAMES", "selectedCount", "selectedFields", "accPlacementReference",
  "CAN_RATES", "validCanConfig", "encodeCanConfig", "canPeriodFromHz", "GYRO_RANGES",
  "validJustValues", "justValuesToFields", "createStreamParser", "createWriteQueue",
  "eulerToMatrix", "quatToMatrix", "quatToEuler", "mulVec", "projectBoard", "polygonArea", "convexHull", "BOARD_VERTS",
];

export function loadCore(file = join(root, "src/public/app.js")) {
  const src = readFileSync(file, "utf8");
  const block = extractBlock(src, "// ==== GYRO-CORE BEGIN ====", "// ==== GYRO-CORE END ====");
  return new Function(`"use strict";\n${block}\nreturn { ${CORE_NAMES.join(", ")} };`)();
}

// ---------- 固件协议（独立实现，对照 src/drivers/protocol.c） ----------
export function fwCrc16(data) {
  let crc = 0xffff;
  for (const b of data) {
    crc ^= (b << 8) & 0xffff;
    for (let i = 0; i < 8; i++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}
export function fwPackFrame(msgId, seq, payload) {
  const p = Uint8Array.from(payload);
  const buf = new Uint8Array(7 + p.length);
  buf.set([0xaa, 0x55, msgId, p.length, seq & 0xff]);
  buf.set(p, 5);
  const crc = fwCrc16(buf.subarray(2, 5 + p.length));
  buf[5 + p.length] = crc & 0xff;
  buf[6 + p.length] = crc >> 8;
  return buf;
}
const trunc16 = (x) => { const v = Math.trunc(Math.fround(x)); return ((v % 65536) + 65536) % 65536; }; // (int16_t)(float) 截断
function le(size, fill) { const b = new Uint8Array(size); fill(new DataView(b.buffer)); return b; }
export const fw = {
  attitude: (seq, roll, pitch, yaw, flags, ts) => fwPackFrame(0x01, seq, le(16, (v) => { v.setFloat32(0, roll, true); v.setFloat32(4, pitch, true); v.setFloat32(8, yaw, true); v.setUint8(12, flags); v.setUint8(13, 0); v.setUint16(14, ts, true); })),
  quat: (seq, qw, qx, qy, qz, ts) => fwPackFrame(0x02, seq, le(18, (v) => { [qw, qx, qy, qz].forEach((q, i) => v.setFloat32(i * 4, q, true)); v.setUint16(16, ts, true); })),
  imu: (seq, g, a, tempC, ts) => fwPackFrame(0x03, seq, le(28, (v) => { [...g, ...a].forEach((x, i) => v.setFloat32(i * 4, x, true)); v.setUint16(24, trunc16(Math.fround(tempC) * 100), true); v.setUint16(26, ts, true); })),
  compact: (seq, roll, pitch, yaw, gz, flags, ts) => fwPackFrame(0x04, seq, le(12, (v) => { v.setUint16(0, trunc16(Math.fround(roll) * 100), true); v.setUint16(2, trunc16(Math.fround(pitch) * 100), true); v.setUint16(4, trunc16(Math.fround(yaw) * 100), true); v.setUint16(6, trunc16(Math.fround(gz) * 10), true); v.setUint8(8, flags); v.setUint16(10, ts, true); })),
  sysinfo: (seq, fusionHz, outHz, skipN, tempC, mode, canOk) => fwPackFrame(0x05, seq, le(16, (v) => { v.setUint32(0, fusionHz, true); v.setUint32(4, outHz, true); v.setUint16(8, skipN, true); v.setUint16(10, trunc16(Math.fround(tempC) * 100), true); v.setUint8(12, mode); v.setUint8(13, canOk); v.setUint16(14, 0, true); })),
  ack: (seq, cmd, status, detail) => fwPackFrame(0x90, seq, le(4, (v) => { v.setUint8(0, cmd); v.setUint8(1, status); v.setUint16(2, detail, true); })),
};

// ---------- JustFloat ----------
export const TAIL = [0x00, 0x00, 0x80, 0x7f];
export function justFrame(values) {
  const out = new Uint8Array(values.length * 4 + 4);
  const dv = new DataView(out.buffer);
  values.forEach((x, i) => dv.setFloat32(i * 4, x, true));
  out.set(TAIL, values.length * 4);
  return out;
}
export function concat(parts) {
  const n = parts.reduce((s, p) => s + p.length, 0);
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}
export function rng(seed = 1) {
  let s = seed >>> 0 || 1;
  const next = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  next.int = (a, b) => a + Math.floor(next() * (b - a + 1));
  return next;
}
// 帧内容可校验：ch0 = 序号（姿态范围内），其余由序号确定
export function poseValues(seq, n) {
  const v = [seq % 360 - 180, Math.sin(seq) * 80, Math.cos(seq) * 170];
  if (n === 6) v.push(Math.sin(seq * 0.3) * 250, 9.8 + Math.cos(seq) * 0.1, 30 + (seq % 10) / 10);
  return v.map(Math.fround);
}
export function feedChunked(push, bytes, r, min = 1, max = 64) {
  for (let i = 0; i < bytes.length;) { const n = r.int(min, max); push(bytes.subarray(i, i + n)); i += n; }
}

// ---------- 原线上 app.js（修复前）在 vm 中加载，用于复现旧缺陷 ----------
export function loadLegacyApp(file = join(root, "backup/live-deployed/app.js")) {
  const src = readFileSync(file, "utf8");
  const els = new Map();
  const noop = () => {};
  const ctx2d = new Proxy({}, { get: () => noop, set: () => true });
  const el = (id) => {
    if (!els.has(id)) {
      const store = { id, textContent: "", value: id === "baud" ? "2000000" : "", disabled: false, className: "", scrollTop: 0, width: 720, height: 410 };
      els.set(id, new Proxy(store, {
        get: (t, k) => (k in t ? t[k] : k === "getContext" ? () => ctx2d : k === "classList" ? { toggle: noop, contains: () => false } : k === "addEventListener" ? noop : undefined),
        set: (t, k, v) => { t[k] = v; return true; },
      }));
    }
    return els.get(id);
  };
  const sandbox = {
    document: { getElementById: el, querySelector: () => ({ value: "1" }) },
    performance: { now: () => Date.now() }, requestAnimationFrame: noop, navigator: {}, confirm: () => true,
    setTimeout, clearTimeout, console, Uint8Array, DataView, Math, Number, Array, Promise, Error, JSON, Object, Date,
  };
  sandbox.globalThis = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(src, sandbox);
  return { feed: sandbox.__feed, state: sandbox.__state, el, sandbox };
}
