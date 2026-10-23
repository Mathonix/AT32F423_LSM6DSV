(() => {
  if(!globalThis.GyroEskfTuning?.control)return;
  eskfTuningView=GyroEskfTuning.createView(eskfTuningControl,eskfTuningContext);
  let wasConnected=false;
  setInterval(()=>{
    if(!running&&(wasConnected||eskfTuningControl.state.pending))eskfTuningControl.reset();
    wasConnected=running;eskfTuningView.render();
  },150);
  eskfTuningView.render();
})();
