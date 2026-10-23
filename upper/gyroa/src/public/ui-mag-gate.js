(() => {
  if (!magGateControl) return;
  magGateView = GyroMagGate.createView(magGateControl, magGateContext);
  setInterval(() => {
    if (!running && magGateControl.state.pending) magGateControl.reset();
    magGateView.render();
  }, 100);
  magGateView.render();
})();
