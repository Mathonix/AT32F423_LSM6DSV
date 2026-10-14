// Drift acquisition consumes the existing parser; it never opens another serial reader.
(function (root) {
  'use strict';
  const DURATIONS = [60, 300, 600, 1800, 3600];
  const AXES = ['yaw', 'pitch', 'roll'];
  const GAP_MS = 2000;
  const copy = value => JSON.parse(JSON.stringify(value));
  const wrapDelta = value => ((value + 180) % 360 + 360) % 360 - 180;
  function fingerprint(m) {
    return JSON.stringify([m.firmware, m.model, m.algorithm, m.mode, m.rangeDps, m.outputHz, m.profile,
      m.restBiasEnabled, m.motionBiasEnabled, m.outputs, m.vqfSettings, m.zaruSettings]);
  }
  function createSession({ now = () => performance.now(), wall = () => new Date().toISOString() } = {}) {
    let s = { status: 'idle', rows: [] }, previous = null, angles = null, deviceTime = null;
    function elapsed(at = now()) { return s.startMs == null ? 0 : Math.max(0, Math.min(s.durationMs, (s.endMs ?? at) - s.startMs)); }
    function stop(status, reason, at = now()) {
      if (!['waiting', 'running'].includes(s.status)) return;
      s.endMs = s.startMs == null ? at : Math.min(at, s.startMs + s.durationMs);
      s.status = status; s.reason = reason; s.finishedAt = wall();
    }
    function start(seconds, metadata = {}) {
      if (!DURATIONS.includes(seconds) || ['waiting', 'running'].includes(s.status)) return false;
      s = { status: 'waiting', durationMs: seconds * 1000, requestedAt: wall(), waitMs: now(), startMs: null,
        endMs: null, metadata: copy(metadata), fingerprint: fingerprint(metadata), reason: '', rows: [],
        frames: 0, maxGapMs: 0, first: null, last: null, lastMs: null,
        min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity], diagnosticSamples: 0, restSamples: 0 };
      previous = angles = deviceTime = null;
      return true;
    }
    function tick({ connected = true, busy = false, metadata } = {}, at = now()) {
      if (!['waiting', 'running'].includes(s.status)) return;
      if (!connected) { stop('interrupted', '设备连接中断', at); return; }
      if (busy) { stop('interrupted', '设备进入设置、校准或升级状态', at); return; }
      if (metadata && fingerprint(metadata) !== s.fingerprint) { stop('interrupted', '设备配置发生变化', at); return; }
      if (s.startMs == null) { if (at - s.waitMs > GAP_MS) stop('interrupted', '未收到完整的 Yaw / Pitch / Roll 数据', at); return; }
      // Check coverage BEFORE completion: a suspended browser must not report a passed hour.
      if (at - s.lastMs > GAP_MS) { stop('interrupted', '姿态数据中断超过 2 秒', at); return; }
      if (at >= s.startMs + s.durationMs) {
        const hz=Number(s.metadata.outputHz);
        const coverageMs=Number.isFinite(hz) && hz>0 ? Math.max(500,Math.min(GAP_MS,1000/hz+100)) : 500;
        if (s.lastMs < s.startMs + s.durationMs - coverageMs) stop('interrupted', '测试结束时数据覆盖不足', at);
        else stop('complete', '', s.startMs + s.durationMs);
      }
    }
    function ingest(sample, at = now()) {
      if (!['waiting', 'running'].includes(s.status)) return false;
      const raw = AXES.map(k => sample[k]);
      if (!raw.every(Number.isFinite)) return false;
      tick({}, at);
      if (!['waiting', 'running'].includes(s.status)) return false;
      if (s.startMs == null) { s.startMs = at; s.startedAt = wall(); s.status = 'running'; }
      if (at < s.startMs || (s.lastMs != null && at < s.lastMs)) return false;
      const gap = s.lastMs == null ? 0 : at - s.lastMs;
      s.maxGapMs = Math.max(s.maxGapMs, gap);
      angles = previous ? raw.map((v, i) => angles[i] + wrapDelta(v - previous[i])) : raw.slice();
      previous = raw;
      s.first ??= angles.slice(); s.last = angles.slice(); s.lastMs = at; s.frames++;
      const second = Math.floor((at - s.startMs) / 1000);
      let row = s.rows[second];
      if (!row) row = s.rows[second] = { second, n: 0, sum: [0, 0, 0], min: [Infinity, Infinity, Infinity],
        max: [-Infinity, -Infinity, -Infinity], tempSum: 0, tempN: 0, tempMin: Infinity, tempMax: -Infinity,
        gyroSum: [0, 0, 0], gyroN: 0, biasSum: [0, 0, 0], biasN: 0 };
      row.n++;
      angles.forEach((v, i) => { row.sum[i] += v; row.min[i] = Math.min(row.min[i], v); row.max[i] = Math.max(row.max[i], v);
        s.min[i] = Math.min(s.min[i], v); s.max[i] = Math.max(s.max[i], v); });
      if (Number.isFinite(sample.temp)) { row.tempSum += sample.temp; row.tempN++; row.tempMin = Math.min(row.tempMin, sample.temp); row.tempMax = Math.max(row.tempMax, sample.temp); }
      if (sample.gyro?.length === 3 && sample.gyro.every(Number.isFinite)) { row.gyroN++; sample.gyro.forEach((v, i) => row.gyroSum[i] += v); }
      if (sample.bias?.length === 3 && sample.bias.every(Number.isFinite)) { row.biasN++; sample.bias.forEach((v, i) => row.biasSum[i] += v); }
      return true;
    }
    function diagnostic(d) {
      if (s.status !== 'running') return;
      if (Number.isInteger(d.timestampMs)) {
        if (deviceTime != null && ((d.timestampMs - deviceTime) >>> 0) > 0x80000000) {
          stop('interrupted', '设备计时倒退，可能已重启'); return;
        }
        deviceTime = d.timestampMs;
      }
      s.diagnosticSamples++; if (d.rest) s.restSamples++;
    }
    function summary() {
      const rows = s.rows.filter(Boolean), endSecond = rows.at(-1)?.second ?? 0;
      const firstRows = rows.filter(r => r.second < 10), lastRows = rows.filter(r => r.second >= Math.max(0, endSecond - 9));
      const mean = (rs, i) => { const n = rs.reduce((n, r) => n + r.n, 0); return n ? rs.reduce((a, r) => a + r.sum[i], 0) / n : null; };
      const baseline = AXES.map((_, i) => mean(firstRows, i));
      const axes = Object.fromEntries(AXES.map((key, i) => {
        if (!s.frames) return [key, null];
        const points = rows.map(r => [r.second + .5, r.sum[i] / r.n]);
        const x0 = points.reduce((a, p) => a + p[0], 0) / points.length;
        const y0 = points.reduce((a, p) => a + p[1], 0) / points.length;
        const variance = points.reduce((a, p) => a + (p[0] - x0) ** 2, 0);
        const slope = variance ? points.reduce((a, p) => a + (p[0] - x0) * (p[1] - y0), 0) / variance * 60 : null;
        return [key, { netDeg: s.last[i] - s.first[i], meanChangeDeg: mean(lastRows, i) - baseline[i],
          peakToPeakDeg: s.max[i] - s.min[i], maxDeviationDeg: Math.max(Math.abs(s.max[i] - baseline[i]), Math.abs(s.min[i] - baseline[i])),
          slopeDegPerMin: slope, baselineDeg: baseline[i], firstMeanDeg: baseline[i], lastMeanDeg: mean(lastRows, i) }];
      }));
      const temps = rows.filter(r => r.tempN);
      return { schemaVersion: 1, status: s.status, reason: s.reason || '', requestedSeconds: (s.durationMs || 0) / 1000,
        elapsedSeconds: elapsed() / 1000, dataSeconds: s.frames ? (s.lastMs - s.startMs) / 1000 : 0,
        frames: s.frames || 0, secondsWithData: rows.length, maxGapMs: s.maxGapMs || 0,
        startedAt: s.startedAt || null, finishedAt: s.finishedAt || null, metadata: s.metadata || {}, axes,
        temperature: temps.length ? { min: Math.min(...temps.map(r => r.tempMin)), max: Math.max(...temps.map(r => r.tempMax)),
          first: temps[0].tempSum / temps[0].tempN, last: temps.at(-1).tempSum / temps.at(-1).tempN } : null,
        restFraction: s.diagnosticSamples ? s.restSamples / s.diagnosticSamples : null,
        method: '全部接收的姿态帧统计；角度跨 ±180° 连续展开；首尾 10 秒按帧均值；每秒均值线性拟合；浏览器单调时钟计时。',
        staticVerified: false };
    }
    function rows() {
      return s.rows.filter(Boolean).map(r => ({ second: r.second, frames: r.n,
        angles: r.sum.map(v => v / r.n), min: r.min, max: r.max,
        temperature: r.tempN ? r.tempSum / r.tempN : null,
        gyro: r.gyroN ? r.gyroSum.map(v => v / r.gyroN) : [null, null, null],
        bias: r.biasN ? r.biasSum.map(v => v / r.biasN) : [null, null, null] }));
    }
    function csv() {
      const columns = ['second', 'frames', ...AXES.map(k => `${k}_mean_deg`), ...AXES.map(k => `${k}_delta_deg`),
        ...AXES.map(k => `${k}_min_deg`), ...AXES.map(k => `${k}_max_deg`), 'temperature_C', 'gx_dps', 'gy_dps', 'gz_dps', 'bias_x_dps', 'bias_y_dps', 'bias_z_dps'];
      const base = AXES.map(k => summary().axes[k]?.baselineDeg ?? 0);
      return '\uFEFF' + [columns.join(','), ...rows().map(r => [r.second, r.frames, ...r.angles,
        ...r.angles.map((v, i) => v - base[i]), ...r.min, ...r.max, r.temperature, ...r.gyro, ...r.bias].join(','))].join('\r\n');
    }
    return { start, ingest, diagnostic, tick, stop: () => stop('cancelled', '用户停止测试'), summary, rows, csv,
      elapsed, get state() { return s; }, get active() { return ['waiting', 'running'].includes(s.status); } };
  }
  root.GyroDrift = { DURATIONS, AXES, GAP_MS, createSession, fingerprint };
})(globalThis);
