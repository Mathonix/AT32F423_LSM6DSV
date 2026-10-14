// mobile-mock.js（20261003 mobile1）：
// 1) 记录浏览器是否原生支持 Web Serial（手机浏览器没有）→ 显示提示条；
// 2) ?mock=1 时同步加载 /mock-serial.js（模拟 navigator.serial + AT32 设备应答，NOT real hardware）。
(function () {
  var native = 'serial' in navigator;
  var mock = /(?:^|[?&])mock=1(?:&|$)/.test(location.search.slice(1));
  window.__gyroaEnv = { nativeSerial: native, mock: mock };
  if (mock) {
    document.write('<script src="/mock-serial.js?v=20261008rest1"><\/script>');
    // Optional local magnetometer demonstration. Does not touch a real device.
    var magDemo = new URLSearchParams(location.search).get('magdemo');
    document.addEventListener('DOMContentLoaded', function () {
      if (magDemo && window.__mock) Object.assign(window.__mock.dev, {
        extended: true, configVersion: 4, capabilities: 255,
        activeMode: magDemo === '6axis' ? 0 : magDemo === 'relative' ? 2 : 1,
        magUnsupported: magDemo === 'legacy', magInvalid: magDemo === 'invalid', magDisturbed: magDemo === 'disturbed',
      });
      var wsDemo=new URLSearchParams(location.search).get('wsdemo');
      if(wsDemo && window.__mock) Object.assign(window.__mock.dev,{wsUnsupported:wsDemo==='legacy',failSave:wsDemo==='fail'});
      var algorithmDemo=new URLSearchParams(location.search).get('algodemo');
      if(algorithmDemo && window.__mock) Object.assign(window.__mock.dev,{algorithmUnsupported:algorithmDemo==='legacy',failSave:algorithmDemo==='fail'});
      if(algorithmDemo==='usb' && window.__mock)window.__mock.setup({transport:'usb'});
      if(algorithmDemo==='uart' && window.__mock)window.__mock.dev.source=0;
    });
  }
  document.addEventListener('DOMContentLoaded', function () {
    var n = document.getElementById('serialNotice'); if (!n) return;
    if (!native && !('usb' in navigator)) n.hidden = false;
    var kind=document.getElementById('connKind');
    if(kind){if(!native && !mock && 'usb' in navigator)kind.value='webusb';kind.addEventListener('change',function(){document.getElementById('baud').disabled=kind.value==='webusb';});document.getElementById('baud').disabled=kind.value==='webusb';}
    if (mock) { n.hidden = false; n.classList.add('is-mock'); document.getElementById('serialNoticeText').textContent = native ? 'Mock 设备模式（?mock=1）— 不是实板' : '手机浏览器不支持 Web Serial，当前为 Mock 演示（?mock=1，不是实板）；实板请在电脑 Chrome/Edge 中连接'; document.getElementById('serialNoticeMock').hidden = true; }
  });
})();
