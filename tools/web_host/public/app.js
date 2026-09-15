const $ = (id) => document.getElementById(id);
const SYNC_A = 0xaa;
const SYNC_B = 0x55;
const JUST_TAIL = [0x00, 0x00, 0x80, 0x7f];
const CMD = {
  PING: 0x10,
  ZERO: 0x11,
  CAL: 0x12,
  QUERY: 0x14,
  RESET: 0x15,
  ENTER: 0x17,
  EXIT: 0x18,
  MODE: 0x19,
  CAN_ID: 0x1a,
  GYRO_60: 0x1b,
  ACC_6FACE: 0x1c,
  STREAM: 0x13
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
let drawPending = false;
let writeChain = Promise.resolve();
let pendingImmediateReset = false;
let pendingExitAfterMode = false;
let lastPoseAt = 0;

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

function modeName(mode) {
  return ['六轴', '九轴', '九轴相对角'][mode] || '未知';
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

function parseBinaryFrame(raw) {
  const body = raw.slice(2, -2);
  const received = raw[raw.length - 2] | (raw[raw.length - 1] << 8);
  if (crc16(body) !== received) return false;
  const id = raw[2];
  const length = raw[3];
  const payload = raw.slice(5, 5 + length);
  if (id === 0x90 && payload.length >= 4) {
    const command = payload[0];
    const status = payload[1];
    const detail = payload[2] | (payload[3] << 8);
    log(`ACK CMD 0x${command.toString(16).padStart(2, '0')} 状态=${status} detail=${detail}`);
    if (command === CMD.ENTER) {
      setting = status === 0;
      updateSettingUI();
      say(status === 0 ? '????' : `?????${status}`);
    } else if (command === CMD.EXIT && status === 0) {
      pendingExitAfterMode = false;
      setting = false;
      updateSettingUI();
    } else if (command === CMD.MODE) {
      if (status === 0) {
        say(pendingImmediateReset ? '??????' : '??????????');
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
        say(`?????${status}`);
      }
    } else if (command === CMD.CAN_ID) {
      say(status === 0 ? `CAN ID ???? 0x${detail.toString(16).padStart(3, '0')}` : `CAN ID ?????${status}`);
    } else if (command === CMD.GYRO_60 || command === CMD.ACC_6FACE) {
      say(status === 0 ? '????' : `?????${status}?detail=${detail}`);
    }
  } else if (id === 0x05 && payload.length >= 13) {
    const view = new DataView(new Uint8Array(payload).buffer);
    const mode = payload[12];
    $('modeLabel').textContent = `模式：${modeName(mode)}`;
    log(`状态：融合 ${view.getUint32(0, true)}Hz，输出 ${view.getUint32(4, true)}Hz，温度 ${view.getInt16(10, true) / 100}°C`);
  } else if (id === 0x01 && payload.length >= 12) {
    const view = new DataView(new Uint8Array(payload).buffer);
    updatePose([view.getFloat32(0, true), view.getFloat32(4, true), view.getFloat32(8, true)], 3);
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
    // Remove everything through the tail. This prevents ACK bytes from being
    // reused as a second JustFloat frame and preserves the next partial frame.
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
  try { if (reader) reader.releaseLock(); } catch (error) { log(`释放读取器：${error.message}`); }
  try { if (writer) writer.releaseLock(); } catch (error) { log(`释放写入器：${error.message}`); }
  try { if (port) await port.close(); } catch (error) { log(`关闭串口：${error.message}`); }
  reader = null;
  writer = null;
  port = null;
  rx = [];
  justBuffer = [];
  writeChain = Promise.resolve();
  setting = false; pendingImmediateReset = false; pendingExitAfterMode = false; updateSettingUI();
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
  else say(immediate ? '?????? ACK' : '?????? ACK');
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
$('canIdBtn').onclick = async () => {
  if (!setting) { say('????????'); return; }
  const value = Number($('canNodeId').value);
  if (!Number.isInteger(value) || value < 0 || value > 0x7ff) { say('CAN ?? ID ??? 0~2047'); return; }
  await send(CMD.CAN_ID, [value & 0xff, (value >> 8) & 0xff]);
};
$('gyro60Btn').onclick = async () => {
  if (!setting) { say('????????'); return; }
  if (confirm('???? 60 ??????????')) { await send(CMD.GYRO_60); say('?? MCU ACK'); }
};
$('acc6Btn').onclick = async () => {
  if (!setting) { say('????????'); return; }
  if (confirm('?????????????')) { await send(CMD.ACC_6FACE); say('?? MCU ACK'); }
};
$('zeroBtn').onclick = () => send(CMD.ZERO);
$('calBtn').onclick = () => send(CMD.CAL);
$('pingBtn').onclick = () => send(CMD.PING);
$('clearLog').onclick = () => { $('log').textContent = ''; };
$('parseMode').onchange = (event) => { parseModeValue = event.target.value; };
if ($('justChannels')) $('justChannels').onchange = (event) => { justChannels = Number(event.target.value); justBuffer = []; };
updateSettingUI(); draw(); updateRate();
if ('serial' in navigator) navigator.serial.addEventListener('disconnect', (event) => { if (event.target === port) void disconnect(); });
