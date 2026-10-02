"use strict";
// Web host 协议回归测试：加载完整 app.js（stub 全部 DOM/Web Serial），
// 用固件 protocol.c 相同逻辑编码帧并喂给 feed()，断言解析结果。
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const src = fs.readFileSync(path.join(__dirname, "public", "app.js"), "utf8");

let failed = 0;
const check = (name, cond, detail) => {
  if (cond) console.log(`  ok   ${name}`);
  else { failed++; console.error(`  FAIL ${name}${detail ? " — " + detail : ""}`); }
};

// ---------- 固件侧参考编码（C 逻辑的 JS 翻译） ----------
function fwCrc16(data) {
  let crc = 0xffff;
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}
function fwPack(msgId, seq, payload) {
  const head = [msgId, payload.length, seq];
  const crc = fwCrc16([...head, ...payload]);
  return [0xaa, 0x55, ...head, ...payload, crc & 0xff, (crc >> 8) & 0xff];
}
const f32 = (v) => { const b = new ArrayBuffer(4); new DataView(b).setFloat32(0, v, true); return [...new Uint8Array(b)]; };
const i16 = (v) => [v & 0xff, (v >> 8) & 0xff];
const u32 = (v) => [v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >>> 24) & 0xff];
const u16 = (v) => [v & 0xff, (v >> 8) & 0xff];

// ---------- DOM stub ----------
const elements = {};
function makeEl(id) {
  const listeners = {};
  return {
    id,
    textContent: "",
    value: id === "baud" ? "2000000" : id === "canNodeId" ? "1" : id === "outHz" ? "200" : "",
    disabled: false,
    checked: false,
    className: "",
    scrollTop: 0,
    scrollHeight: 0,
    getContext: () => ({
      clearRect() { }, fillRect() { }, beginPath() { }, moveTo() { }, lineTo() { },
      stroke() { }, fill() { }, closePath() { }, fillText() { },
      set fillStyle(_) { }, set strokeStyle(_) { }, set lineWidth(_) { }, set font(_) { },
    }),
    width: 720, height: 410,
    addEventListener(type, fn) { (listeners[type] ??= []).push(fn); },
    dispatch(type, ev) { (listeners[type] || []).forEach((f) => f(ev)); },
  };
}
const elIds = [
  "log", "message", "linkState", "baud", "parseMode", "justChannels", "refreshBtn",
  "yaw", "pitch", "roll", "temp", "rate", "yaw2", "pitch2", "roll2",
  "modeLabel", "streamLabel", "flagsLabel",
  "gx", "gy", "gz", "ax", "ay", "az", "quat",
  "attitude", "settingsState", "modeField", "applyBtn", "exitBtn", "enterBtn",
  "connectBtn", "disconnectBtn", "clearLog",
  "streamApplyBtn", "rateApplyBtn", "streamMode", "outHz",
  "canIdBtn", "canNodeId", "gyro60Btn", "acc6Btn", "zeroBtn", "calBtn", "pingBtn",
  "restartNow",
];
for (const id of elIds) elements[id] = makeEl(id);

// collect log lines：模拟 textContent += 语义（累积字符串）
const logAcc = { text: "" };
Object.defineProperty(elements.log, "textContent", {
  get() { return logAcc.text; },
  set(v) { logAcc.text += String(v); },
  configurable: true,
});
const logReset = () => { logAcc.text = ""; };
const logHas = (re) => re.test(logAcc.text);
let rafCount = 0;

const sandbox = {
  document: {
    readyState: "complete",
    getElementById: (id) => elements[id] || makeEl(id),
    querySelector: () => ({ value: "1" }),
    querySelectorAll: () => [],
    addEventListener() { },
  },
  window: {},
  navigator: { serial: { addEventListener() { } } },
  performance: { now: () => Date.now() },
  // 同步执行以驱动 draw()；限制次数防 updateRate 无限自递归
  requestAnimationFrame: (fn, ...args) => { if (rafCount++ < 500) fn(...args); },
  confirm: () => false,
  setTimeout, clearTimeout, setInterval, clearInterval,
  console,
  TextDecoder, TextEncoder,
  __capture: null,
};
sandbox.window = sandbox;
vm.runInNewContext(src, sandbox, { filename: "app.js" });

const feed = (bytes) => { rafCount = 0; sandbox.__feed(bytes); };

console.log("[crc16 一致性]");
{
  // app.js 内部 crc16 与固件实现交叉验证：用 packet() 生成的帧与 fwPack 比对
  // packet 未导出，改为通过 DOM stub 无法直接调——用 feed 后的 ACK 日志间接验证即可。
  // 这里直接验证：fwCrc16 已知向量（"123456789" CRC/XMODEM = 0x31C3）
  check("fwCrc16 firmware vector", fwCrc16([...Buffer.from("123456789")]) === 0x29b1);
}

console.log("[ATT 0x01]");
{
  const payload = [...f32(10.5), ...f32(-20.25), ...f32(170.125), 0x01, 0x00, ...u16(1234)];
  feed(fwPack(0x01, 1, payload));
  check("yaw=170.125", elements.yaw.textContent === "170.1250", elements.yaw.textContent);
  check("pitch=-20.25", elements.pitch.textContent === "-20.2500", elements.pitch.textContent);
  check("roll=10.5", elements.roll.textContent === "10.5000", elements.roll.textContent);
  check("flags 显示静止", /静止/.test(elements.flagsLabel.textContent), elements.flagsLabel.textContent);
}

console.log("[COMPACT 0x04]");
{
  feed(fwPack(0x04, 2, [...i16(1050), ...i16(-2025), ...i16(17012), ...i16(123), 0x00, 0x00, ...u16(99)]));
  check("yaw=170.12", elements.yaw.textContent === "170.1200", elements.yaw.textContent);
  check("pitch=-20.25", elements.pitch.textContent === "-20.2500", elements.pitch.textContent);
  check("gz=12.3", elements.gz.textContent === "12.3000", elements.gz.textContent);
}

console.log("[IMU 0x03]");
{
  feed(fwPack(0x03, 3, [...f32(1.5), ...f32(-2.5), ...f32(30.25), ...f32(0.1), ...f32(0.2), ...f32(9.81), ...i16(2534), ...u16(7)]));
  check("gz=30.25", elements.gz.textContent === "30.2500", elements.gz.textContent);
  check("az=9.81", elements.az.textContent === "9.8100", elements.az.textContent);
}

console.log("[QUAT 0x02]");
{
  feed(fwPack(0x02, 4, [...f32(1), ...f32(0), ...f32(0), ...f32(0), ...u16(5)]));
  check("identity quat → 0°", Math.abs(Number(elements.yaw.textContent)) < 1e-6, elements.yaw.textContent);
}

console.log("[SYSINFO 0x05]");
{
  logReset();
  feed(fwPack(0x05, 5, [...u32(2000), ...u32(200), ...u16(10), ...i16(2534), 0x01, 0x01, ...u16(0)]));
  check("modeLabel 融合 2000Hz", /2000Hz/.test(elements.modeLabel.textContent), elements.modeLabel.textContent);
  check("streamLabel 二进制姿态", /二进制姿态/.test(elements.streamLabel.textContent), elements.streamLabel.textContent);
  check("温度 25.34 记录", logHas(/25\.34/), logAcc.text);
  check("温度显示", elements.temp.textContent === "25.34", elements.temp.textContent);
}

console.log("[ACK 0x90]");
{
  logReset();
  feed(fwPack(0x90, 6, [0x13, 0x00, 0x02, 0x00]));
  check("ACK 中文状态", logHas(/ACK CMD 0x13 成功/), logAcc.text);
  // detail=0x0002 → 设备确认切到流 2（二进制紧凑）
  check("streamMode 更新", /二进制紧凑/.test(elements.streamLabel.textContent), elements.streamLabel.textContent);
  feed(fwPack(0x90, 7, [0x11, 0x03, 0x00, 0x00]));
  check("ZERO 失败显示执行失败", logHas(/Yaw 置零失败（执行失败）/), logAcc.text);
}

console.log("[坏 CRC 拒收]");
{
  const yawBefore = elements.yaw.textContent;
  const frame = fwPack(0x01, 8, [...f32(77), ...f32(1), ...f32(2), 0, 0, ...u16(0)]);
  frame[frame.length - 1] ^= 0xff;
  feed(frame);
  check("坏帧不更新姿态", elements.yaw.textContent === yawBefore, elements.yaw.textContent);
}

console.log("[JustFloat 兼容]");
{
  // 切回 auto 模式已默认；喂 3ch JustFloat 帧
  elements.parseMode.value = "justfloat";
  elements.parseMode.dispatch("change", { target: elements.parseMode });
  const ch = [45.5, -3.25, 12.125];
  const bytes = [];
  for (const v of ch) { const b = new ArrayBuffer(4); new DataView(b).setFloat32(0, v, true); bytes.push(...new Uint8Array(b)); }
  feed([...bytes, 0x00, 0x00, 0x80, 0x7f]);
  check("just yaw=45.5", elements.yaw.textContent === "45.5000", elements.yaw.textContent);
  check("just pitch=-3.25", elements.pitch.textContent === "-3.2500", elements.pitch.textContent);
  check("just roll=12.125", elements.roll.textContent === "12.1250", elements.roll.textContent);
}

console.log(failed ? `\n${failed} test(s) FAILED` : "\nall tests passed");
process.exit(failed ? 1 : 0);
