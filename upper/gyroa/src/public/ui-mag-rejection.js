(() => {
  if(!magRejectionControl)return;
  magRejectionView=GyroMagRejection.createView(magRejectionControl,magRejectionContext);
  setInterval(()=>{
    if(!running && magRejectionControl.state.pending)magRejectionControl.reset();
    magRejectionView.render();
  },100);
  magRejectionView.render();
})();
