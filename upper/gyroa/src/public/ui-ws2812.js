(() => {
  if(!ws2812Control)return;
  ws2812View=GyroWs2812.createView(ws2812Control,ws2812Context);
  setInterval(()=>{
    if(!running && ws2812Control.state.pending)ws2812Control.reset();
    ws2812View.render();
  },100);
  ws2812View.render();
})();
