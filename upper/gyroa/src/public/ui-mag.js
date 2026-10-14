(() => {
  const poller = GyroMag.createPoller(GyroMagIO);
  Object.assign(GyroMag, poller);
  setInterval(poller.tick, 50);
})();
