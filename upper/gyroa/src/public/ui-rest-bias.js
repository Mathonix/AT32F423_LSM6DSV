(() => {
  if(!restBiasControl)return;
  restBiasView=GyroRestBias.createView(restBiasControl,restBiasContext);
  setInterval(()=>{
    if(!running && restBiasControl.state.pending)restBiasControl.reset();
    restBiasView.render();
  },100);
  restBiasView.render();
})();
