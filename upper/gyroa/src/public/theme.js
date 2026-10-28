// gyroa · 20261009style1 — theme switch (light default, dark optional), persisted in localStorage('gyroa-theme').
// New visitors start in light mode; an explicit saved theme is always respected.
// GyroTheme.color('--chart-grid', fallback) lets canvas code read the active palette (cached until the theme changes).
// UI only: no device, protocol or storage-of-config logic here.
// 20261009theme2: a click on #themeToggle reveals the new theme as a circle growing from the button (View Transitions API).
// Instant switch when the API is missing, prefers-reduced-motion is set, the tab is hidden, or the change follows the system.
// 20261009theme3: a second toggle while a transition runs skips it (skipTransition) and switches instantly; any text selection
// is cleared before a transition starts and html.theme-switching blocks selection until it ends (fixes a freeze on double Enter).
(function () {
  var KEY = 'gyroa-theme', root = document.documentElement, cache = {};
  var DURATION = 600, EASING = 'cubic-bezier(.65,0,.35,1)', pending = null, active = null, lastPath = '', gen = 0; // gen: a skipped transition's late update callback must not re-apply its theme
  function saved() { try { var v = localStorage.getItem(KEY); return v === 'light' || v === 'dark' ? v : null; } catch (e) { return null; } }
  function system() { return 'light'; }
  function current() { return saved() || system(); }
  function sync(theme) {
    var meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute('content', theme === 'light' ? '#f4f3ef' : '#191c18');
    var b = document.getElementById('themeToggle');
    if (b) {
      var next = theme === 'light' ? '深色' : '浅色';
      b.setAttribute('aria-label', '切换到' + next + '主题'); b.title = '切换到' + next + '主题';
      b.setAttribute('aria-pressed', theme === 'light' ? 'true' : 'false');
    }
  }
  function apply(theme) {
    root.setAttribute('data-theme', theme); cache = {}; sync(theme);
    try { window.dispatchEvent(new CustomEvent('gyroa-theme', { detail: theme })); } catch (e) {}
  }
  apply(current());
  window.GyroTheme = {
    get: current,
    set: function (theme) { try { localStorage.setItem(KEY, theme); } catch (e) {} apply(theme); },
    toggle: function () { this.set(current() === 'dark' ? 'light' : 'dark'); },
    // Animated toggle from a button: circular reveal of ::view-transition-new(root). Commit (and the 'gyroa-theme' event that
    // makes ui-chart.js / ui-test.js repaint their canvases synchronously) runs inside the update callback.
    toggleFrom: function (btn) {
      var next = (pending || current()) === 'dark' ? 'light' : 'dark', self = this;
      var reduce = !!(window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches);
      if (active) { gen++; try { if (active.skipTransition) active.skipTransition(); } catch (e) {} active = null; pending = null; root.classList.remove('theme-switching'); lastPath = 'instant'; self.set(next); return; }
      if (typeof document.startViewTransition !== 'function' || reduce || document.hidden || !btn) { lastPath = 'instant'; pending = null; self.set(next); return; }
      try { var sel = window.getSelection && window.getSelection(); if (sel) sel.removeAllRanges(); } catch (e) {}
      var r = btn.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2;
      var radius = Math.hypot(Math.max(x, innerWidth - x), Math.max(y, innerHeight - y));
      pending = next; root.classList.add('theme-switching');
      var vt, my = ++gen;
      try { vt = document.startViewTransition(function () { if (my === gen) self.set(next); }); }
      catch (e) { lastPath = 'error'; pending = null; root.classList.remove('theme-switching'); self.set(next); return; }
      lastPath = 'transition'; active = vt;
      vt.ready.then(function () {
        root.animate({ clipPath: ['circle(0px at ' + x + 'px ' + y + 'px)', 'circle(' + radius + 'px at ' + x + 'px ' + y + 'px)'] },
          { duration: DURATION, easing: EASING, pseudoElement: '::view-transition-new(root)' });
      }).catch(function () {});
      vt.finished.catch(function () {}).then(function () { if (active === vt) { active = null; pending = null; root.classList.remove('theme-switching'); } });
    },
    get lastPath() { return lastPath; },
    get active() { return active; },
    DURATION: DURATION,
    color: function (name, fallback) {
      if (!cache[name]) { var v = getComputedStyle(root).getPropertyValue(name).trim(); if (v) cache[name] = v; }
      return cache[name] || fallback;
    },
  };
  document.addEventListener('DOMContentLoaded', function () {
    var b = document.getElementById('themeToggle');
    if (b) b.addEventListener('click', function () { window.GyroTheme.toggleFrom(b); });
    cache = {}; sync(current());
    window.addEventListener('gyroa-theme', function () { window.dispatchEvent(new Event('resize')); });
  });
})();
