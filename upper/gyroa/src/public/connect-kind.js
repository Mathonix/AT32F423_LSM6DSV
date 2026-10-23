// Default transport from the browser UA. A phone prefers WebUSB. A computer,
// a tablet, or any other UA prefers Web Serial. The arrow beside Connect
// switches the same #connKind select used by the settings page.
(function () {
  function kindFromUA(ua) {
    const s = String(ua || '');
    if (/iPad|Tablet|PlayBook|Kindle|Silk\/|SM-T\d|GT-P\d/i.test(s)) return 'serial';
    if (/iPhone|iPod|Windows Phone|IEMobile|BlackBerry|BB10|Opera Mini|Opera Mobi|KaiOS/i.test(s)) return 'webusb';
    if (/Android/i.test(s) && /Mobile/i.test(s)) return 'webusb';
    return 'serial';
  }
  const NAMES = { serial: 'Web Serial', webusb: 'WebUSB' };

  function kindSelect() { return document.getElementById('connKind'); }
  function current() { return kindSelect()?.value === 'webusb' ? 'webusb' : 'serial'; }
  function closeMenu() {
    const menu = document.getElementById('connectMenu');
    const caret = document.getElementById('connectMenuBtn');
    if (menu) menu.hidden = true;
    if (caret) caret.setAttribute('aria-expanded', 'false');
  }
  function syncMarks() {
    const kind = current();
    document.querySelectorAll('[data-connect-kind]').forEach((button) => {
      const on = button.getAttribute('data-connect-kind') === kind;
      button.setAttribute('aria-checked', on ? 'true' : 'false');
      button.classList.toggle('on', on);
    });
    const connect = document.getElementById('connectBtn');
    const caret = document.getElementById('connectMenuBtn');
    const name = NAMES[kind];
    if (connect) connect.title = `使用 ${name} 连接`;
    if (caret) caret.title = `选择连接方式，当前为 ${name}`;
    const baud = document.getElementById('baud');
    if (baud) baud.disabled = kind === 'webusb';
  }
  function locked() {
    const connect = document.getElementById('connectBtn');
    const kind = kindSelect();
    return !connect || connect.hidden || connect.disabled || !!kind?.disabled;
  }
  function syncLock() {
    const connect = document.getElementById('connectBtn');
    const caret = document.getElementById('connectMenuBtn');
    if (!connect || !caret) return;
    caret.hidden = connect.hidden;
    caret.disabled = locked();
    if (caret.disabled) closeMenu();
  }
  function apply(kind) {
    const sel = kindSelect();
    if (!sel || (kind !== 'serial' && kind !== 'webusb')) return false;
    if (sel.disabled && sel.value !== kind) return false;
    if (sel.value !== kind) sel.value = kind;
    syncMarks();
    return true;
  }
  function boot() {
    apply(kindFromUA(navigator.userAgent));
    const sel = kindSelect();
    const caret = document.getElementById('connectMenuBtn');
    const menu = document.getElementById('connectMenu');
    sel?.addEventListener('change', syncMarks);
    caret?.addEventListener('click', (event) => {
      event.stopPropagation();
      if (!menu || caret.disabled) return;
      const open = menu.hidden;
      if (!open) { closeMenu(); return; }
      menu.hidden = false;
      caret.setAttribute('aria-expanded', 'true');
    });
    menu?.addEventListener('click', (event) => {
      const item = event.target.closest?.('[data-connect-kind]');
      if (!item || locked()) return;
      apply(item.getAttribute('data-connect-kind'));
      closeMenu();
    });
    document.addEventListener('click', closeMenu);
    document.addEventListener('keydown', (event) => { if (event.key === 'Escape') closeMenu(); });
    syncLock();
  }
  if (typeof document !== 'undefined') boot();
  globalThis.GyroConnection = { kindFromUA, apply, current, syncLock, closeMenu };
})();
