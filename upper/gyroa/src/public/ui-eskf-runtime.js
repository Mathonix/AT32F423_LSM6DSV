(() => {
  if(!eskfRuntimeControl)return;
  eskfRuntimeView=GyroEskfRuntime.createView(eskfRuntimeControl,eskfTuningContext);
  let last=0,wasConnected=false;
  setInterval(()=>{
    if(!running&&wasConnected)eskfRuntimeControl.reset();
    wasConnected=running;
    const panel=document.getElementById('eskfRuntimePanel');
    // No background query traffic on other pages, hidden tabs or during operations.
    if(running&&!document.hidden&&panel?.getClientRects().length&&Date.now()-last>=1000){
      if(eskfRuntimeControl.query())last=Date.now();
    }
    eskfRuntimeView.render();
  },250);
  eskfRuntimeView.render();
})();
