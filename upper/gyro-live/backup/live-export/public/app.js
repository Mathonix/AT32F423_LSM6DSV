const $ = (id) => document.getElementById(id);
const SYNC_A = 0xaa;
const SYNC_B = 0x55;
const JUST_TAIL = [0x00, 0x00, 0x80, 0x7f];

// 与固件 inc/telemetry/protocol.h 一一对应
const MSG = {
  ATTITUDE: 0x01,   // roll, pitch, yaw, flags, reserved, ts
  QUAT: 0x02,       // qw, qx, qy, qz, ts
  IMU: 0x03,        // gyr xyz, acc xyz, temp x100, ts
  COMPACT: 0x04,    // roll/pitch/yaw x100, gz x10, flags, ts
  SYSINFO: 0x05,    // fusion_hz, out_hz, skip_n, temp x100, stream_mode, can_ok
  ACK: 0x90,
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
};
const ACK_STATUS = ['成功', '未知命令', '参数无效', '执行失败'];
const STREAM_MODES = ['VOFA 3通道', '二进制姿态', '二进制紧凑', '二进制IMU', 'VOFA 6通道'];
const FLAGS = {
  REST: 1 << 0,
  MAG_VALID: 1 << 1,
  MAG_DISTURBED: 1 << 2,
  CALIB_DONE: 1 << 3,
  SENSOR_ERROR: 1 << 4,
};

let port = null;
let reader = null;
let writer = null;
let running = false;
let seq = 0;
let rx = [];
let justBuffer = [];
let setting = false;
let lastFrames = 0;
let lastRateAt = performance.now();
let frameRate = 0;
let justChannels = 3;
let parseModeValue = 'auto';
let pose = { yaw: 0, pitch: 0, roll: 0, temp: null };
let imu = { gx: null, gy: null, gz: null, ax: null, ay: null, az: null };
let quat = null;
let drawPending = false;
let writeChain = Promise.resolve();
let pendingImmediateReset = false;
let pendingExitAfterMode = false;
let lastPoseAt = 0;
let streamMode = null;

const log = (message) => {
  const node = $('log');
  if (!node) return;
  const now = new Date().toLocaleTimeString();
  node.textContent += `[${now}] ${message}\n`;
  node.scrollTop = node.scrollHeight;
};
const say = (message) => {
  $('message').textContent = message;
  log(message);
  const lines = $('log').textContent.split('\n');
  if (lines.length > 300) $('log').textContent = lines.slice(-300).join('\n');
};
const crc16 = (bytes) => {
  let crc = 0xffff;
  for (const value of bytes) {
    crc ^= value << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) ? (((crc << 1) ^ 0x1021) & 0xffff) : ((crc << 1) & 0xffff);
    }
  }
  return crc;
};
const packet = (cmd, payload = []) => {
  const body = [cmd, payload.length, seq++ & 0xff, ...payload];
  const crc = crc16(body);
  return new Uint8Array([SYNC_A, SYNC_B, ...body, crc & 0xff, crc >> 8]);
};

async function send(cmd, payload = []) {
  if (!writer) {
    say('请先连接串口');
    return false;
  }
  try {
    const frame = packet(cmd, payload);
    writeChain = writeChain.then(() => writer.write(frame));
    await writeChain;
    log(`发送 CMD 0x${cmd.toString(16).padStart(2, '0')} ${payload.map((v) => v.toString(16).padStart(2, '0')).join(' ')}`);
    return true;
  } catch (error) {
    say(`发送失败：${error.message}`);
    return false;
  }
}

function setConnected(connected) {
  $('connectBtn').disabled = connected;
  $('disconnectBtn').disabled = !connected;
  $('linkState').textContent = connected ? '已连接' : '未连接';
  $('linkState').className = `badge ${connected ? 'on' : 'off'}`;
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

function validPose(values, channels) {
  if (!values.every(Number.isFinite)) return false;
  const angleLimit = 100000;
  if (Math.abs(values[0]) > angleLimit || Math.abs(values[1]) > angleLimit || Math.abs(values[2]) > angleLimit) return false;
  return channels === 3 || Math.abs(values[3]) < 2000;
}

function updatePose(values, channels) {
  if (!validPose(values, channels)) return false;
  pose.yaw = values[0];
  pose.pitch = values[1];
  pose.roll = values[2];
  if (channels === 4) pose.temp = values[3];
  lastFrames += 1;
  lastPoseAt = performance.now();
  if (!drawPending) {
    drawPending = true;
    requestAnimationFrame(() => { drawPending = false; draw(); });
  }
  return true;
}

function updateImu(values) {
  if (!values.every(Number.isFinite)) return;
  [imu.gx, imu.gy, imu.gz, imu.ax, imu.ay, imu.az] = values;
  lastFrames += 1;
  lastPoseAt = performance.now();
  if (!drawPending) {
    drawPending = true;
    requestAnimationFrame(() => { drawPending = false; draw(); });
  }
}

// ---------- 二进制帧解析（对齐固件 protocol.c） ----------
function parseBinaryFrame(raw) {
  const body = raw.slice(2, -2);
  const received = raw[raw.length - 2] | (raw[raw.length - 1] << 8);
  if (crc16(body) !== received) return false;
  const id = raw[2];
  const length = raw[3];
  const payload = raw.slice(5, 5 + length);
  const view = new DataView(new Uint8Array(payload).buffer, 0, payload.length);

  if (id === MSG.ACK && payload.length >= 4) {
    const command = payload[0];
    const status = payload[1];
    const detail = payload[2] | (payload[3] << 8);
    const statusText = ACK_STATUS[status] || `0x${status.toString(16)}`;
    log(`ACK CMD 0x${command.toString(16).padStart(2, '0')} ${statusText} detail=0x${detail.toString(16).padStart(4, '0')}`);
    if (command === CMD.ENTER) {
      setting = status === 0;
      updateSettingUI();
      say(status === 0 ? '已进入设置模式' : `进入设置模式失败（${statusText}）`);
    } else if (command === CMD.EXIT && status === 0) {
      pendingExitAfterMode = false;
      setting = false;
      updateSettingUI();
      say(detail ? '已退出设置模式（有未保存改动将重启应用）' : '已退出设置模式');
    } else if (command === CMD.MODE) {
      if (status === 0) {
        say(pendingImmediateReset ? '融合模式已保存，设备重启中' : '融合模式已保存，退出设置模式生效');
        if (pendingImmediateReset) {
          pendingImmediateReset = false;
          setTimeout(() => { if (running) void disconnect(false); }, 150);
        } else if (pendingExitAfterMode) {
          pendingExitAfterMode = false;
          void send(CMD.EXIT);
        }
      } else {
        pendingImmediateReset = false;
        pendingExitAfterMode = false;
        say(`融合模式保存失败（${statusText}）`);
      }
    } else if (command === CMD.CAN_ID) {
      say(status === 0 ? `CAN 节点 ID 已保存：0x${detail.toString(16).padStart(3, '0')}` : `CAN ID 保存失败（${statusText}）`);
    } else if (command === CMD.STREAM) {
      if (status === 0) {
        streamMode = detail & 0xff;
        $('streamLabel').textContent = `数据流：${streamModeName(streamMode)}`;
        say(`数据流已切换：${streamModeName(streamMode)}`);
      } else {
        say(`数据流切换失败（${statusText}）`);
      }
    } else if (command === CMD.OUTPUT_HZ) {
      say(status === 0 ? `输出频率已设为 ${detail} Hz` : `输出频率设置失败（${statusText}，须为融合率的整数因子）`);
    } else if (command === CMD.GYRO_60 || command === CMD.ACC_6FACE) {
      say(status === 0 ? '标定完成' : `标定失败（${statusText}，detail=0x${detail.toString(16).padStart(4, '0')}）`);
    } else if (command === CMD.ZERO) {
      say(status === 0 ? 'Yaw 已置零' : `Yaw 置零失败（${statusText}）`);
    } else if (command === CMD.PING) {
      say('PONG：设备在线');
    }
  } else if (id === MSG.SYSINFO && payload.length >= 16) {
    // 固件 ahrs_payload_system_info_t: fusion_hz(4) out_hz(4) skip_n(2) temp(2) mode(1) can_ok(1) reserved(2) = 16 字节
    const fusionHz = view.getUint32(0, true);
    const outHz = view.getUint32(4, true);
    const skipN = view.getUint16(8, true);
    const temp = view.getInt16(10, true) / 100;
    const mode = payload[12];
    const canOk = payload[13];
    streamMode = mode;
    $('modeLabel').textContent = `融合：${fusionModeName(0)} ${fusionHz}Hz · 输出 ${outHz}Hz`;
    $('streamLabel').textContent = `数据流：${streamModeName(mode)}${canOk ? ' · CAN 就绪' : ''}`;
    pose.temp = temp;
    log(`状态：融合 ${fusionHz}Hz，输出 ${outHz}Hz，skip ${skipN}，温度 ${temp.toFixed(2)}°C，流 ${streamModeName(mode)}${canOk ? '，CAN 就绪' : ''}`);
    if (!drawPending) {
      drawPending = true;
      requestAnimationFrame(() => { drawPending = false; draw(); });
    }
  } else if (id === MSG.ATTITUDE && payload.length >= 12) {
    // 固件 ahrs_payload_attitude_t: roll, pitch, yaw, flags, reserved, ts
    const roll = view.getFloat32(0, true);
    const pitch = view.getFloat32(4, true);
    const yaw = view.getFloat32(8, true);
    const flags = payload[12];
    updatePose([yaw, pitch, roll], 3);
    $('flagsLabel').textContent = flagsText(flags);
  } else if (id === MSG.COMPACT && payload.length >= 10) {
    // 固件 ahrs_payload_compact_t: roll/pitch/yaw x100, gz x10, flags
    const roll = view.getInt16(0, true) / 100;
    const pitch = view.getInt16(2, true) / 100;
    const yaw = view.getInt16(4, true) / 100;
    const gz = view.getInt16(6, true) / 10;
    const flags = payload[8];
    imu.gz = gz;
    updatePose([yaw, pitch, roll], 3);
    $('flagsLabel').textContent = flagsText(flags);
  } else if (id === MSG.IMU && payload.length >= 24) {
    // 固件 ahrs_payload_imu_t: gx..az, temp x100, ts
    const values = [0, 4, 8, 12, 16, 20].map((off) => view.getFloat32(off, true));
    updateImu(values);
  } else if (id === MSG.QUAT && payload.length >= 18) {
    // 固件 ahrs_payload_quaternion_t: qw, qx, qy, qz, ts (18 字节)
    quat = [0, 4, 8, 12].map((off) => view.getFloat32(off, true));
    // 四元数 → 欧拉角（Z-Y-X 顺序，与固件一致）
    const [qw, qx, qy, qz] = quat;
    const roll = Math.atan2(2 * (qw * qx + qy * qz), 1 - 2 * (qx * qx + qy * qy)) * 180 / Math.PI;
    const pitch = Math.asin(Math.max(-1, Math.min(1, 2 * (qw * qy - qz * qx)))) * 180 / Math.PI;
    const yaw = Math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz)) * 180 / Math.PI;
    updatePose([yaw, pitch, roll], 3);
  }
  return true;
}

function queueJust(bytes) {
  if (parseModeValue !== 'binary' && bytes.length) justBuffer.push(...bytes);
}

function parseBinaryFromRx() {
  while (rx.length >= 2) {
    const start = rx.findIndex((value, index) => value === SYNC_A && rx[index + 1] === SYNC_B);
    if (start < 0) {
      queueJust(rx);
      rx = [];
      if (parseModeValue !== 'binary') parseJust();
      return;
    }
    if (start > 0) {
      queueJust(rx.splice(0, start));
      if (parseModeValue !== 'binary') parseJust();
    }
    if (rx.length < 6) return;
    const length = rx[3];
    if (length > 64) {
      queueJust([rx.shift()]);
      continue;
    }
    const total = 2 + 3 + length + 2;
    if (rx.length < total) return;
    const candidate = rx.slice(0, total);
    if (!parseBinaryFrame(candidate)) {
      // Bad CRC: discard only one byte so a valid JustFloat frame is not lost.
      queueJust([rx.shift()]);
      continue;
    }
    rx.splice(0, total);
  }
}

function isTailAt(buffer, index) {
  return buffer[index] === JUST_TAIL[0] && buffer[index + 1] === JUST_TAIL[1] && buffer[index + 2] === JUST_TAIL[2] && buffer[index + 3] === JUST_TAIL[3];
}

function parseJust() {
  const bytesPerFrame = justChannels * 4 + 4;
  while (justBuffer.length >= bytesPerFrame) {
    let tailIndex = -1;
    for (let index = 0; index <= justBuffer.length - 4; index += 1) {
      if (isTailAt(justBuffer, index)) {
        tailIndex = index;
        break;
      }
    }
    if (tailIndex < 0) {
      justBuffer = justBuffer.slice(-(bytesPerFrame - 1));
      return;
    }
    const start = tailIndex - justChannels * 4;
    if (start < 0) {
      justBuffer.splice(0, tailIndex + 4);
      continue;
    }
    const frameBytes = justBuffer.slice(start, tailIndex + 4);
    const view = new DataView(new Uint8Array(frameBytes).buffer);
    const values = Array.from({ length: justChannels }, (_, index) => view.getFloat32(index * 4, true));
    justBuffer.splice(0, tailIndex + 4);
    if (validPose(values, justChannels)) updatePose(values, justChannels);
  }
}

function feed(bytes) {
  rx.push(...bytes);
  parseBinaryFromRx();
  if (parseModeValue !== 'binary') parseJust();
}

async function readLoop() {
  while (running) {
    try {
      const { value, done } = await reader.read();
      if (done) break;
      if (value && value.length) feed(Array.from(value));
    } catch (error) {
      if (running) say(`读取失败：${error.message}`);
      break;
    }
  }
}

async function connect() {
  if (!('serial' in navigator)) {
    say('当前浏览器不支持 Web Serial，请使用最新版 Chrome/Edge');
    return;
  }
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: Number($('baud').value), dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none' });
    rx = [];
    justBuffer = [];
    lastFrames = 0;
    lastRateAt = performance.now();
    reader = port.readable.getReader();
    writer = port.writable.getWriter();
    running = true;
    setConnected(true);
    say('串口已连接，等待姿态数据');
    void readLoop();
    await send(CMD.QUERY);
  } catch (error) {
    say(`连接失败：${error.message}`);
    await disconnect(false);
  }
}

async function disconnect(showMessage = true) {
  running = false;
  try { if (reader) await reader.cancel(); } catch (error) { log(`关闭读取器：${error.message}`); }
  try { if (reader) await reader.releaseLock(); } catch (error) { log(`释放读取器：${error.message}`); }
  try { if (writer) await writer.releaseLock(); } catch (error) { log(`释放写入器：${error.message}`); }
  try { if (port) await port.close(); } catch (error) { log(`关闭串口：${error.message}`); }
  reader = null;
  writer = null;
  port = null;
  rx = [];
  justBuffer = [];
  setting = false;
  pendingImmediateReset = false;
  pendingExitAfterMode = false;
  updateSettingUI();
  setConnected(false);
  if (showMessage) say('串口已断开');
}

function updateSettingUI() {
  $('settingsState').textContent = setting ? '设置模式已进入' : '未进入设置模式';
  $('settingsState').className = `badge ${setting ? 'on' : ''}`;
  $('modeField').disabled = !setting;
  $('applyBtn').disabled = !setting;
  $('exitBtn').disabled = !setting;
  $('enterBtn').disabled = setting;
}

function selectedMode() {
  return Number(document.querySelector('input[name="fusion"]:checked').value);
}

async function applyMode() {
  if (!setting) {
    say('请先进入设置模式');
    return;
  }
  const immediate = $('restartNow').checked;
  pendingImmediateReset = immediate;
  pendingExitAfterMode = !immediate;
  const sent = await send(CMD.MODE, [selectedMode(), immediate ? 1 : 0]);
  if (!sent) { pendingImmediateReset = false; pendingExitAfterMode = false; }
  else log(immediate ? '等待设备重启 ACK' : '等待保存 ACK');
}

async function applyStream() {
  const mode = Number($('streamMode').value);
  const sent = await send(CMD.STREAM, [mode]);
  if (sent) log(`等待数据流切换 ACK（${streamModeName(mode)}）`);
}

async function applyRate() {
  const hz = Number($('outHz').value);
  if (!Number.isInteger(hz) || hz <= 0 || hz > 2000) {
    say('输出频率须为 1~2000 的整数，且是融合率（200Hz）的整数因子');
    return;
  }
  const sent = await send(CMD.OUTPUT_HZ, [hz & 0xff, (hz >> 8) & 0xff]);
  if (sent) log(`等待输出频率 ACK（${hz}Hz）`);
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

function val(id, value) {
  $(id).textContent = Number.isFinite(value) ? Number(value).toFixed(4) : '--';
}

function draw() {
  val('yaw', pose.yaw); val('pitch', pose.pitch); val('roll', pose.roll);
  $('temp').textContent = Number.isFinite(pose.temp) ? pose.temp.toFixed(2) : '--';
  val('yaw2', pose.yaw); val('pitch2', pose.pitch); val('roll2', pose.roll);
  if ($('gx')) {
    val('gx', imu.gx); val('gy', imu.gy); val('gz', imu.gz);
    val('ax', imu.ax); val('ay', imu.ay); val('az', imu.az);
  }
  if ($('quat') && quat) {
    $('quat').textContent = quat.map((v) => v.toFixed(3)).join(', ');
  }
  const canvas = $('attitude');
  const context = canvas.getContext('2d');
  const { width, height } = canvas;
  context.clearRect(0, 0, width, height);
  context.fillStyle = '#0d1623'; context.fillRect(0, 0, width, height);
  context.strokeStyle = '#263d59'; context.lineWidth = 1;
  for (let index = 0; index < 10; index += 1) {
    context.beginPath(); context.moveTo(index * width / 10, 0); context.lineTo(index * width / 10, height);
    context.moveTo(0, index * height / 10); context.lineTo(width, index * height / 10); context.stroke();
  }
  const cx = width / 2; const cy = height / 2; const scale = Math.min(width, height) * 0.27;
  const yaw = pose.yaw * Math.PI / 180; const pitch = pose.pitch * Math.PI / 180; const roll = pose.roll * Math.PI / 180;
  const points = [[-1, -0.55], [1, -0.55], [1, 0.55], [-1, 0.55]];
  const rotate = ([px, py]) => {
    let x = px * scale; let y = py * scale * Math.cos(pitch); x *= Math.cos(roll); y *= Math.cos(roll);
    return [cx + x * Math.cos(yaw) - y * Math.sin(yaw), cy + x * Math.sin(yaw) + y * Math.cos(yaw) - Math.sin(pitch) * scale * 0.5];
  };
  const projected = points.map(rotate);
  context.fillStyle = '#1b77a7'; context.strokeStyle = '#75d9ff'; context.lineWidth = 3;
  context.beginPath(); projected.forEach((point, index) => index ? context.lineTo(...point) : context.moveTo(...point)); context.closePath(); context.fill(); context.stroke();
  context.fillStyle = '#fff'; context.font = 'bold 17px sans-serif'; context.fillText('AT32 AHRS', cx - 48, cy + 6);
  context.strokeStyle = '#f3bf54'; context.lineWidth = 5; context.beginPath(); context.moveTo(cx - 100, cy + 100); context.lineTo(cx + 100, cy + 100); context.stroke();
}

$('connectBtn').onclick = connect;
$('disconnectBtn').onclick = () => disconnect();
$('refreshBtn').onclick = () => send(CMD.QUERY);
$('enterBtn').onclick = () => send(CMD.ENTER);
$('exitBtn').onclick = () => send(CMD.EXIT);
$('applyBtn').onclick = applyMode;
$('streamApplyBtn').onclick = applyStream;
$('rateApplyBtn').onclick = applyRate;
$('canIdBtn').onclick = async () => {
  if (!setting) { say('请先进入设置模式'); return; }
  const value = Number($('canNodeId').value);
  if (!Number.isInteger(value) || value < 0 || value > 0x7ff) { say('CAN 节点 ID 范围为 0~2047'); return; }
  await send(CMD.CAN_ID, [value & 0xff, (value >> 8) & 0xff]);
};
$('gyro60Btn').onclick = async () => {
  if (confirm('启动 60 秒陀螺仪标定？期间请保持静止')) { await send(CMD.GYRO_60); say('等待 MCU ACK'); }
};
$('acc6Btn').onclick = async () => {
  if (!setting) { say('请先进入设置模式'); return; }
  if (confirm('启动六面加速度计标定？按提示逐面放置')) { await send(CMD.ACC_6FACE); say('等待 MCU ACK'); }
};
$('zeroBtn').onclick = () => send(CMD.ZERO);
$('calBtn').onclick = () => send(CMD.CAL);
$('pingBtn').onclick = () => send(CMD.PING);
$('clearLog').onclick = () => { $('log').textContent = ''; };
$('parseMode').onchange = (event) => { parseModeValue = event.target.value; };
if ($('justChannels')) $('justChannels').onchange = (event) => { justChannels = Number(event.target.value); justBuffer = []; };
updateSettingUI(); draw(); updateRate();
if ('serial' in navigator) navigator.serial.addEventListener('disconnect', (event) => { if (event.target === port) void disconnect(); });

// 测试钩子（Node vm 回归测试用；浏览器中 __feed 未被引用，无副作用）
if (typeof globalThis !== 'undefined') {
  globalThis.__feed = feed;
  globalThis.__state = () => ({ pose, imu, quat, streamMode, setting });
}
