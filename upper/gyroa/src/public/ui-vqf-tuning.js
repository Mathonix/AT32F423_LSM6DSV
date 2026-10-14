(() => {
  if(!globalThis.GyroVqfTuning?.control)return;
  vqfTuningView=GyroVqfTuning.createView(vqfTuningControl,vqfTuningContext);
  let wasConnected=false;
  setInterval(()=>{
    if(!running&&(wasConnected||vqfTuningControl.state.pending))vqfTuningControl.reset();
    wasConnected=running;vqfTuningView.render();
  },150);
  vqfTuningView.render();
})();
