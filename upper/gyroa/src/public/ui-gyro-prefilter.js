(() => {
  if (!gyroPrefilterControl) return;
  gyroPrefilterView = GyroPrefilter.createView(gyroPrefilterControl, gyroPrefilterContext);
  setInterval(() => {
    if (!running && gyroPrefilterControl.state.pending) gyroPrefilterControl.reset();
    gyroPrefilterView.render();
  }, 100);
  gyroPrefilterView.render();
})();
