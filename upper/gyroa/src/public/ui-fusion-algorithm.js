(() => {
  if(!fusionAlgorithmControl)return;
  fusionAlgorithmView=GyroFusionAlgorithm.createView(fusionAlgorithmControl,fusionAlgorithmContext);
  setInterval(()=>{
    if(!running&&fusionAlgorithmControl.state.pending)fusionAlgorithmControl.reset();
    fusionAlgorithmView.render();
  },100);
  fusionAlgorithmView.render();
})();
