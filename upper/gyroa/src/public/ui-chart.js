// AT32 AHRS · app2 home waveforms: 3-axis acceleration (g) and angular rate (dps).
// Samples the values app.js already parsed (imu + imuStamp) into a ring buffer; parsing/protocol untouched.
// restore1：放大视图（#chartZoom 覆盖层）：同一缓冲实时更新，显示当前窗口内各轴最大 / 最小 / 当前值，
// 悬停显示十字线 + 提示（相对时间 · 样本号 · X/Y/Z）。vqfinit1：入口改为标题栏「放大」按钮（#accZoomBtn / #gyroZoomBtn），
// 不再双击小图打开，也不再双击关闭；关闭：「关闭」按钮 / Esc / 点背景。
// vqfinit1：放大视图「暂停 / 继续」（#czPause）：暂停 = 冻结放大图显示的数据（快照），最大 / 最小与悬停都基于冻结数据；
// 后台采样照常（小图继续更新）；「继续」恢复实时；关闭放大视图即回到实时。
(() => {
  const $ = (id) => document.getElementById(id);
  const CAP = 640, SAMPLE_MS = 20, WINDOW_MS = 10000, FRESH_MS = 600, HIDE_MS = 800, FRAME_MS = 33;
  const COLORS = ['#635BFF', '#22A06B', '#F79009'];
  const G = 9.80665; // app.js / firmware report acceleration in m/s²
  const groups = {
    acc: { keys: ['ax', 'ay', 'az'], scale: 1 / G, unit: 'g', digits: 3, minSpan: 0.2, el: $('accChart') },
    gyro: { keys: ['gx', 'gy', 'gz'], scale: 1, unit: 'dps', digits: 2, minSpan: 2, el: $('gyroChart') },
  };
  for (const g of Object.values(groups)) {
    Object.assign(g, { t: new Float64Array(CAP), n: new Float64Array(CAP), seq: 0, v: g.keys.map(() => new Float32Array(CAP)), head: 0, count: 0, lastActive: 0, visible: false,
      canvas: g.el.querySelector('canvas'), legend: g.keys.map((k) => $(`${k}Live`)), draws: 0 });
    new ResizeObserver(() => fit(g)).observe(g.canvas);
  }
  let lastFrame = 0, frames = 0;

  function fit(g) {
    const dpr = Math.min(window.devicePixelRatio || 1, 3), w = Math.round(g.canvas.clientWidth * dpr), h = Math.round(g.canvas.clientHeight * dpr);
    if (w && h && (g.canvas.width !== w || g.canvas.height !== h)) { g.canvas.width = w; g.canvas.height = h; g.dirty = true; }
  }

  function sample() {
    const now = performance.now();
    let changed = false;
    for (const g of Object.values(groups)) {
      const fresh = g.keys.map((k) => running && now - imuStamp[k] < FRESH_MS && Number.isFinite(imu[k]));
      if (fresh.some(Boolean)) {
        g.lastActive = now;
        const i = g.head; g.t[i] = now; g.n[i] = ++g.seq;
        g.keys.forEach((k, a) => { g.v[a][i] = fresh[a] ? imu[k] * g.scale : NaN; });
        g.head = (i + 1) % CAP; g.count = Math.min(CAP, g.count + 1);
        g.fresh = fresh;
      }
      const show = g.lastActive > 0 && now - g.lastActive < HIDE_MS;
      if (show !== g.visible) {
        g.visible = show; g.el.hidden = !show; changed = true;
        if (!show) { g.count = 0; g.head = 0; g.lastActive = 0; if (zoom.g === g) closeZoom(); }
      }
    }
    if (changed) {
      const any = Object.values(groups).some((g) => g.visible);
      $('homeCharts').hidden = !any;
      document.querySelector('.home-grid').classList.toggle('has-charts', any);
      for (const g of Object.values(groups)) fit(g);
    }
  }

  const fmt = (x, d) => (Number.isFinite(x) ? (x >= 0 ? ' ' : '') + x.toFixed(d) : '--');
  function niceStep(span) {
    const raw = span / 4, p = 10 ** Math.floor(Math.log10(raw)), m = raw / p;
    return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
  }
  // 通用绘制：小图与放大图共用。返回本帧使用的样本（窗口内）与坐标映射，供统计 / 悬停
  function render(g, c, now, hover = null) {
    const ctx = c.getContext('2d'), W = c.width, H = c.height;
    if (!W || !H) return null;
    const dpr = W / Math.max(1, c.clientWidth);
    ctx.clearRect(0, 0, W, H);
    // visible window values → auto scale
    let lo = Infinity, hi = -Infinity;
    const idx = [];
    for (let n = 0; n < g.count; n++) {
      const i = (g.head - g.count + n + CAP) % CAP;
      if (now - g.t[i] > WINDOW_MS) continue;
      idx.push(i);
      for (const arr of g.v) { const x = arr[i]; if (Number.isFinite(x)) { if (x < lo) lo = x; if (x > hi) hi = x; } }
    }
    if (!Number.isFinite(lo)) { lo = -g.minSpan / 2; hi = g.minSpan / 2; }
    if (hi - lo < g.minSpan) { const mid = (hi + lo) / 2; lo = mid - g.minSpan / 2; hi = mid + g.minSpan / 2; }
    const step = niceStep(hi - lo); lo = Math.floor(lo / step) * step; hi = Math.ceil(hi / step) * step;
    const left = 46 * dpr, right = 8 * dpr, top = 8 * dpr, bottom = 8 * dpr, pw = W - left - right, ph = H - top - bottom;
    const y = (v) => top + (hi - v) / (hi - lo) * ph, x = (t) => left + (1 - (now - t) / WINDOW_MS) * pw;
    ctx.font = `${12 * dpr}px Inter,system-ui,sans-serif`; ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
    const dec = step >= 1 ? 0 : Math.min(3, Math.ceil(-Math.log10(step)));
    for (let v = lo; v <= hi + step / 2; v += step) {
      const yy = Math.round(y(v)) + .5;
      ctx.strokeStyle = Math.abs(v) < step / 2 ? '#D0D5DD' : '#EEF0F3'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(left, yy); ctx.lineTo(W - right, yy); ctx.stroke();
      ctx.fillStyle = '#98A2B3'; ctx.fillText(v.toFixed(dec), left - 8 * dpr, yy);
    }
    ctx.lineWidth = 1.6 * dpr; ctx.lineJoin = 'round';
    g.v.forEach((arr, a) => {
      ctx.strokeStyle = COLORS[a]; ctx.beginPath(); let pen = false;
      for (const i of idx) {
        const val = arr[i];
        if (!Number.isFinite(val)) { pen = false; continue; }
        const px = x(g.t[i]), py = y(val);
        if (pen) ctx.lineTo(px, py); else { ctx.moveTo(px, py); pen = true; }
      }
      ctx.stroke();
    });
    // 悬停：按鼠标 x（CSS px）找最近样本，画竖线 + 各轴圆点
    let hit = null;
    if (hover && idx.length) {
      const hx = hover.x * dpr;
      if (hx >= left && hx <= W - right) {
        let best = -1, bd = Infinity;
        for (const i of idx) { const d = Math.abs(x(g.t[i]) - hx); if (d < bd) { bd = d; best = i; } }
        if (best >= 0) {
          const px = Math.round(x(g.t[best])) + .5;
          ctx.strokeStyle = '#667085'; ctx.lineWidth = 1; ctx.setLineDash([4 * dpr, 3 * dpr]);
          ctx.beginPath(); ctx.moveTo(px, top); ctx.lineTo(px, H - bottom); ctx.stroke(); ctx.setLineDash([]);
          if (hover.y != null) { const hy = Math.round(hover.y * dpr) + .5; if (hy >= top && hy <= H - bottom) { ctx.strokeStyle = '#D0D5DD'; ctx.beginPath(); ctx.moveTo(left, hy); ctx.lineTo(W - right, hy); ctx.stroke(); } }
          g.v.forEach((arr, a) => { const val = arr[best]; if (!Number.isFinite(val)) return; ctx.fillStyle = COLORS[a]; ctx.beginPath(); ctx.arc(px, y(val), 3.5 * dpr, 0, Math.PI * 2); ctx.fill(); });
          hit = { i: best, seq: g.n[best], age: now - g.t[best], values: g.v.map((arr) => arr[best]), px: px / dpr };
        }
      }
    }
    return { idx, hit };
  }
  function draw(g, now) {
    if (!render(g, g.canvas, now)) return;
    const last = (g.head - 1 + CAP) % CAP;
    g.legend.forEach((node, a) => {
      const text = g.count && g.fresh?.[a] ? fmt(g.v[a][last], g.digits) : '--';
      if (node.textContent !== text) node.textContent = text;
    });
    g.draws++;
  }

  // ---------------- 放大视图 ----------------
  const zoom = { g: null, el: null, canvas: null, tip: null, stats: null, hover: null, last: null, draws: 0, opener: null, frozen: null };
  const AX = ['X', 'Y', 'Z'];
  function buildZoom() {
    const el = document.createElement('div');
    el.className = 'chart-zoom'; el.id = 'chartZoom'; el.hidden = true;
    el.innerHTML = `<div class="cz-panel card" role="dialog" aria-modal="true" aria-labelledby="czTitle">
      <div class="cz-h"><span class="chart-t" id="czTitle"></span><span class="cz-hint">悬停查看数值 · Esc 关闭 · 最近 ${WINDOW_MS / 1000} s</span><button type="button" id="czPause" class="sm" aria-pressed="false" title="暂停：冻结放大图（后台继续采集）">暂停</button><button type="button" id="czClose" class="sm">关闭</button></div>
      <div class="cz-stats" id="czStats">${AX.map((n, a) => `<div class="cz-ax cz-${n.toLowerCase()}" data-axis="${n}"><i>${n}</i><span>最大<b data-k="max">--</b></span><span>最小<b data-k="min">--</b></span><span>当前<b data-k="cur">--</b></span></div>`).join('')}</div>
      <div class="plot cz-plot"><canvas id="czCanvas"></canvas><div class="cz-tip" id="czTip" hidden></div></div>
    </div>`;
    document.body.appendChild(el);
    Object.assign(zoom, { el, canvas: el.querySelector('canvas'), tip: el.querySelector('#czTip'), stats: el.querySelector('#czStats') });
    new ResizeObserver(() => { const c = zoom.canvas, dpr = Math.min(window.devicePixelRatio || 1, 3), w = Math.round(c.clientWidth * dpr), h = Math.round(c.clientHeight * dpr); if (w && h && (c.width !== w || c.height !== h)) { c.width = w; c.height = h; } }).observe(zoom.canvas);
    el.querySelector('#czClose').addEventListener('click', closeZoom);
    el.querySelector('#czPause').addEventListener('click', () => setPaused(!zoom.frozen));
    el.addEventListener('click', (e) => { if (e.target === el) closeZoom(); });     // 点背景
    const plot = el.querySelector('.cz-plot');
    plot.addEventListener('mousemove', (e) => { const r = zoom.canvas.getBoundingClientRect(); zoom.hover = { x: e.clientX - r.left, y: e.clientY - r.top }; });
    plot.addEventListener('mouseleave', () => { zoom.hover = null; zoom.tip.hidden = true; });
  }
  // 冻结：复制环形缓冲（CAP 个样本）与冻结时刻；渲染 / 统计 / 悬停都用这份副本
  function freeze(g, now) {
    return { keys: g.keys, unit: g.unit, digits: g.digits, minSpan: g.minSpan, head: g.head, count: g.count,
      t: g.t.slice(), n: g.n.slice(), v: g.v.map((a) => a.slice()), at: now };
  }
  function setPaused(on) {
    if (!zoom.g) return;
    zoom.frozen = on ? freeze(zoom.g, performance.now()) : null;
    const b = zoom.el.querySelector('#czPause');
    b.textContent = on ? '继续' : '暂停'; b.setAttribute('aria-pressed', String(on));
    b.title = on ? '继续：恢复实时显示' : '暂停：冻结放大图（后台继续采集）';
    zoom.el.dataset.paused = String(on);
    zoom.el.querySelector('.cz-hint').textContent = on ? `已暂停 · 显示冻结数据（后台继续采集）· 悬停查看数值 · Esc 关闭` : `悬停查看数值 · Esc 关闭 · 最近 ${WINDOW_MS / 1000} s`;
    drawZoom(performance.now());
  }
  function openZoom(g) {
    if (!g.visible) return false;
    if (!zoom.el) buildZoom();
    zoom.g = g; zoom.hover = null; zoom.last = null; zoom.tip.hidden = true; zoom.opener = document.activeElement;
    setPaused(false);
    zoom.el.querySelector('#czTitle').innerHTML = `${g === groups.acc ? '加速度' : '角速度'}<em>${g.unit}</em>`;
    zoom.el.dataset.group = g === groups.acc ? 'acc' : 'gyro';
    zoom.el.hidden = false; document.body.classList.add('zoom-open');
    zoom.el.querySelector('#czClose').focus({ preventScroll: true });
    drawZoom(performance.now());
    return true;
  }
  function closeZoom() {
    if (!zoom.el || zoom.el.hidden) return;
    setPaused(false); // 关闭即回到实时
    zoom.el.hidden = true; zoom.g = null; zoom.hover = null; zoom.last = null; document.body.classList.remove('zoom-open');
    if (zoom.opener && document.contains(zoom.opener)) zoom.opener.focus?.({ preventScroll: true });
  }
  function drawZoom(now) {
    if (!zoom.g) return;
    const g = zoom.frozen || zoom.g; if (zoom.frozen) now = zoom.frozen.at;
    const c = zoom.canvas;
    if (!c.width) { const dpr = Math.min(window.devicePixelRatio || 1, 3); c.width = Math.round(c.clientWidth * dpr); c.height = Math.round(c.clientHeight * dpr); }
    const r = render(g, c, now, zoom.hover); if (!r) return;
    // 统计：本帧窗口内样本（与曲线同一批）各轴最大 / 最小；当前 = 最新样本
    const stats = g.v.map((arr) => { let mx = -Infinity, mn = Infinity; for (const i of r.idx) { const x = arr[i]; if (Number.isFinite(x)) { if (x > mx) mx = x; if (x < mn) mn = x; } } return { max: Number.isFinite(mx) ? mx : null, min: Number.isFinite(mn) ? mn : null }; });
    const last = r.idx.length ? r.idx[r.idx.length - 1] : -1;
    zoom.stats.querySelectorAll('.cz-ax').forEach((row, a) => {
      const put = (k, v) => { const b = row.querySelector(`b[data-k="${k}"]`), t = v == null || !Number.isFinite(v) ? '--' : fmt(v, g.digits).trim(); if (b.textContent !== t) b.textContent = t; };
      put('max', stats[a].max); put('min', stats[a].min); put('cur', last >= 0 ? g.v[a][last] : null);
    });
    if (r.hit) {
      const h = r.hit, tip = zoom.tip;
      const text = `<div class="cz-tt">−${(h.age / 1000).toFixed(2)} s · 样本 #${h.seq}</div>` + h.values.map((v, a) => `<div class="cz-tv cz-${AX[a].toLowerCase()}"><i>${AX[a]}</i><b>${Number.isFinite(v) ? fmt(v, g.digits).trim() : '--'}</b><em>${g.unit}</em></div>`).join('');
      if (tip.innerHTML !== text) tip.innerHTML = text;
      tip.hidden = false;
      const pw = c.clientWidth, tw = tip.offsetWidth || 140, th = tip.offsetHeight || 80;
      let lx = h.px + 12; if (lx + tw > pw - 4) lx = h.px - tw - 12;
      let ly = (zoom.hover.y ?? 0) - th / 2; ly = Math.max(4, Math.min(ly, c.clientHeight - th - 4));
      tip.style.left = `${Math.round(lx)}px`; tip.style.top = `${Math.round(ly)}px`;
    } else zoom.tip.hidden = true;
    zoom.last = { stats, idx: r.idx.slice(), hit: r.hit && { seq: r.hit.seq, values: r.hit.values.slice() } };
    zoom.draws++;
  }
  for (const g of Object.values(groups)) {
    g.zoomBtn = $(g === groups.acc ? 'accZoomBtn' : 'gyroZoomBtn');
    g.zoomBtn?.addEventListener('click', () => openZoom(g));
  }
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && zoom.g) { e.preventDefault(); e.stopPropagation(); closeZoom(); } }, true);

  function loop(now) {
    requestAnimationFrame(loop);
    if (zoom.g && window.GyroUI?.view !== 'status') closeZoom(); // 离开首页即关闭放大视图
    if (document.hidden || window.GyroUI?.view !== 'status' || now - lastFrame < FRAME_MS) return; // paused off-page
    lastFrame = now; frames++;
    const t = performance.now();
    for (const g of Object.values(groups)) if (g.visible) draw(g, t);
    if (zoom.g) drawZoom(t);
  }
  setInterval(sample, SAMPLE_MS);
  requestAnimationFrame(loop);

  window.GyroUI.charts = {
    state: () => Object.fromEntries(Object.entries(groups).map(([k, g]) => [k, { visible: g.visible, count: g.count, draws: g.draws, w: g.canvas.width, h: g.canvas.height }])),
    get frames() { return frames; },
    // 测试 / 诊断：放大视图状态，snapshot = 最近一帧放大图使用的样本（与显示的最大 / 最小同一批）
    zoom: {
      open: (name) => openZoom(groups[name]), close: closeZoom,
      state: () => {
        const g = zoom.g, L = zoom.last;
        const d = zoom.frozen || g;
        return { open: !!g, group: g ? (g === groups.acc ? 'acc' : 'gyro') : null, draws: zoom.draws, digits: g?.digits ?? null, paused: !!zoom.frozen,
          stats: L?.stats ?? null, hit: L?.hit ?? null,
          snapshot: g && L ? { seq: L.idx.map((i) => d.n[i]), v: d.v.map((arr) => L.idx.map((i) => arr[i])) } : null };
      },
    },
  };
})();
