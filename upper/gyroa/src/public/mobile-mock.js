// mobile-mock.js（20261003 mobile1）：
// 1) 记录浏览器是否原生支持 Web Serial（手机浏览器没有）→ 显示提示条；
// 2) ?mock=1 时同步加载 /mock-serial.js（模拟 navigator.serial + AT32 设备应答，NOT real hardware）。
(function () {
  var native = 'serial' in navigator;
  var mock = /(?:^|[?&])mock=1(?:&|$)/.test(location.search.slice(1));
  window.__gyroaEnv = { nativeSerial: native, mock: mock };
  if (mock) document.write('<script src="/mock-serial.js?v=20261004startup1"><\/script>');
  document.addEventListener('DOMContentLoaded', function () {
    var n = document.getElementById('serialNotice'); if (!n) return;
    if (!native) n.hidden = false;
    if (mock) { n.hidden = false; n.classList.add('is-mock'); document.getElementById('serialNoticeText').textContent = native ? 'Mock 设备模式（?mock=1）— 不是实板' : '手机浏览器不支持 Web Serial，当前为 Mock 演示（?mock=1，不是实板）；实板请在电脑 Chrome/Edge 中连接'; document.getElementById('serialNoticeMock').hidden = true; }
  });
})();
