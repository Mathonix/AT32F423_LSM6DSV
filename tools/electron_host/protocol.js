const SYNC = [0xaa, 0x55];
const TAIL = [0, 0, 0x80, 0x7f];
const MAX_PAYLOAD = 64;
const CMD = { PING: 0x10, ZERO: 0x11, CAL: 0x12, STREAM: 0x13, QUERY: 0x14, RESET: 0x15, BOOTLOADER: 0x16, ENTER: 0x17, EXIT: 0x18, MODE: 0x19, CAN_ID: 0x1a, GYRO_60: 0x1b, ACC_6FACE: 0x1c };
const MODE = { SIX: 0, NINE: 1, RELATIVE: 2 };
const ACK = { SUCCESS: 0, UNKNOWN: 1, INVALID: 2, FAILED: 3 };
function crc16(bytes) { let crc = 0xffff; for (const b of bytes) { crc ^= b << 8; for (let i = 0; i < 8; i++) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff; } return crc; }
function packet(cmd, seq, payload = []) { const body = [cmd, payload.length, seq & 255, ...payload]; const crc = crc16(body); return Uint8Array.from([...SYNC, ...body, crc & 255, crc >> 8]); }
function parseBinary(buffer, callback) {
  while (buffer.length >= 2) {
    let start = buffer.findIndex((v, i) => v === 0xaa && buffer[i + 1] === 0x55);
    if (start < 0) { if (buffer.at(-1) === 0xaa) buffer.splice(0, buffer.length - 1); else buffer.length = 0; return; }
    if (start) buffer.splice(0, start);
    if (buffer.length < 7) return;
    const len = buffer[3]; if (len > MAX_PAYLOAD) { buffer.shift(); continue; }
    const total = 7 + len; if (buffer.length < total) return;
    const raw = buffer.slice(0, total); buffer.splice(0, total);
    if (crc16(raw.slice(2, -2)) !== (raw.at(-2) | raw.at(-1) << 8)) continue;
    callback({ type: 'binary', id: raw[2], seq: raw[4], payload: raw.slice(5, 5 + len) });
  }
}
function decodeBinary(frame) {
  const p = frame.payload;
  if (frame.id === 0x90 && p.length >= 4) return { kind: 'ack', cmd: p[0], status: p[1], detail: p[2] | p[3] << 8 };
  if (frame.id === 0x01 && p.length >= 16) { const v = new DataView(Uint8Array.from(p).buffer); return { kind: 'pose', yaw: v.getFloat32(8, true), pitch: v.getFloat32(4, true), roll: v.getFloat32(0, true), flags: v.getUint8(12) }; }
  if (frame.id === 0x05 && p.length >= 16) { const v = new DataView(Uint8Array.from(p).buffer); return { kind: 'system', fusion: v.getUint32(0, true), output: v.getUint32(4, true), temp: v.getInt16(10, true) / 100, stream: v.getUint8(12), can: v.getUint8(13) }; }
  return { kind: 'message', id: frame.id, payload: p };
}
function parseJustFloat(buffer, callback, channels = 0) {
  for (;;) {
    let tail = -1; for (let i = 0; i <= buffer.length - 4; i++) if (TAIL.every((v, k) => buffer[i + k] === v)) { tail = i; break; }
    if (tail < 0) { if (buffer.length > 27) buffer.splice(0, buffer.length - 27); return; }
    const choices = channels ? [channels] : [4, 3]; let found = null;
    for (const ch of choices) { const start = tail - ch * 4; if (start >= 0) { const view = new DataView(Uint8Array.from(buffer.slice(start, tail)).buffer); const values = Array.from({ length: ch }, (_, i) => view.getFloat32(i * 4, true)); if (values.every(Number.isFinite) && values.every((x) => Math.abs(x) < 1e7)) { found = { start, values }; break; } } }
    if (!found) { buffer.splice(0, tail + 4); continue; }
    buffer.splice(0, tail + 4); callback({ kind: 'pose', yaw: found.values[0], pitch: found.values[1], roll: found.values[2], temp: found.values[3] });
  }
}
module.exports = { CMD, MODE, ACK, packet, crc16, parseBinary, decodeBinary, parseJustFloat };
