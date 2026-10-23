(() => {
  if (!restIndicatorControl) return;
  restIndicatorView = GyroRestIndicator.createView(restIndicatorControl, restIndicatorContext);
  setInterval(() => {
    if (!running && restIndicatorControl.state.pending) restIndicatorControl.reset();
    restIndicatorView.render();
  }, 100);
  restIndicatorView.render();
})();
