/* UI-only workspace routing and task tabs; device commands remain in existing controllers. */
(() => {
 const groups={status:'observe',tuning:'configure',calib:'configure',output:'configure',test:'experiment',capture:'experiment',settings:'device',upgrade:'device'};
 const remembered={observe:'status',configure:'tuning',experiment:'test',device:'settings'};
 let lastRoute;
 function sync(){
  const route=document.body.dataset.view||'status',group=groups[route]||'observe';remembered[group]=route;
  if(lastRoute && lastRoute!==route){
   const view=document.querySelector(`.view[data-view="${route}"]`);
   view?.querySelectorAll('.task-content,.record-workspace,.form-card,.firmware-workspace').forEach(el=>{el.scrollTop=0});
  }
  lastRoute=route;
  document.querySelectorAll('.workspace-link').forEach(a=>{const on=a.dataset.workspace===group;a.classList.toggle('active',on);if(on)a.setAttribute('aria-current','true');else a.removeAttribute('aria-current');a.href='#/'+remembered[a.dataset.workspace]});
  document.querySelectorAll('.nav-item').forEach(a=>{a.hidden=a.dataset.group!==group});
 }
 window.addEventListener('hashchange',sync);sync();
 document.querySelectorAll('.task-tabs').forEach(list=>{
  const tabs=[...list.querySelectorAll('[role=tab]')];
  function select(tab){tabs.forEach(t=>{const on=t===tab;t.setAttribute('aria-selected',String(on));t.tabIndex=on?0:-1;document.getElementById(t.dataset.panel).hidden=!on});window.dispatchEvent(new Event('resize'))}
  list.addEventListener('click',e=>{const t=e.target.closest('[role=tab]');if(t)select(t)});
  list.addEventListener('keydown',e=>{const i=tabs.indexOf(document.activeElement);if(i<0)return;let next;if(['ArrowRight','ArrowDown'].includes(e.key))next=(i+1)%tabs.length;else if(['ArrowLeft','ArrowUp'].includes(e.key))next=(i+tabs.length-1)%tabs.length;else if(e.key==='Home')next=0;else if(e.key==='End')next=tabs.length-1;else return;e.preventDefault();select(tabs[next]);tabs[next].focus()});
 });
 const footer=document.querySelector('.view-settings>.page-foot');if(footer)document.getElementById('startupControls').append(footer);
 const lights=document.getElementById('ws2812Panel');
 if(lights){const card=document.createElement('section');card.className='card';card.setAttribute('data-fw-inert','');card.innerHTML='<div class="card-h"><h2>状态指示灯</h2></div>';card.append(lights);document.querySelector('.device-grid').append(card);}
 // Mobile switches between two readable instruments instead of shrinking both onto one screen.
 const home=document.querySelector('.home-grid'),charts=document.getElementById('homeCharts');
 const displayTabs=[...document.querySelectorAll('#statusDisplay [role=tab]')];
 function display(name){
  if(name==='waves'&&charts.hidden)return;
  home.dataset.display=name;
  displayTabs.forEach(tab=>{const on=tab.dataset.display===name;tab.setAttribute('aria-selected',String(on));tab.tabIndex=on?0:-1});
  window.dispatchEvent(new Event('resize'));
 }
 function chartAvailability(){
  const waves=document.getElementById('statusWavesTab');waves.disabled=charts.hidden;
  if(charts.hidden&&home.dataset.display==='waves')display('attitude');
 }
 displayTabs.forEach(tab=>tab.addEventListener('click',()=>display(tab.dataset.display)));
 document.getElementById('statusDisplay').addEventListener('keydown',event=>{
  if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
  const available=displayTabs.filter(tab=>!tab.disabled);
  const current=available.indexOf(document.activeElement);
  const next=event.key==='Home'?0:event.key==='End'?available.length-1:(current+1)%available.length;
  event.preventDefault();display(available[next].dataset.display);available[next].focus();
 });
 new MutationObserver(chartAvailability).observe(charts,{attributes:true,attributeFilter:['hidden']});chartAvailability();
 const phone=matchMedia('(max-width:759px)'),quick=document.getElementById('quickSet');
 const quickLayout=()=>{quick.open=!phone.matches};quickLayout();phone.addEventListener('change',quickLayout);
 const chip=document.getElementById('connChip'),offline=document.getElementById('studioOffline');
 function connection(){offline.hidden=chip.dataset.state==='on'}
 new MutationObserver(connection).observe(chip,{attributes:true,attributeFilter:['data-state']});connection();
})();
