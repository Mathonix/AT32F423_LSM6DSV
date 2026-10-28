(() => {
 const $=id=>document.getElementById(id),api=globalThis.GyroFirmwareFiles;
 if(!api||!$('releaseRefresh'))return;
 let releases=[],nextPage=null,loading=false,downloading=false,request=null,generation=0,remoteFile=false;
 const selected=()=>releases.find(r=>String(r.id)===$('releaseVersion').value);
 const asset=()=>selected()?.assets.find(a=>String(a.id)===$('releaseAsset').value);
 const say=text=>{$('releaseMessage').textContent=text;};
 function controls(){
  const locked=api.busy();
  $('releaseRefresh').disabled=loading||downloading||locked;
  $('releaseMore').disabled=loading||downloading||locked;
  $('releaseVersion').disabled=loading||downloading||locked||!releases.length;
  $('releaseAsset').disabled=loading||downloading||locked||!selected()?.assets.length;
  $('releaseDownload').disabled=loading||downloading||locked||!asset();
  $('releaseCancel').hidden=!downloading;$('releaseMore').hidden=!nextPage;
 }
 function cancel(){generation++;request?.abort();request=null;downloading=false;controls();}
 function option(select,value,text){select.add(new Option(text,String(value)));}
 function details(){
  const release=selected(),file=asset();
  $('releaseNotes').textContent=release?.notes||'此版本没有发布说明。';
  $('releaseDetails').hidden=!release;
  $('releasePage').href=release?.url||'https://github.com/Mathonix/AT32F423_LSM6DSV/releases';
  $('releaseMeta').textContent=release?`${release.tag}${release.prerelease?' · 预发布':''}${release.publishedAt?' · '+new Date(release.publishedAt).toLocaleDateString('zh-CN'):''}${file?' · '+(Number.isSafeInteger(file.size)?(file.size/1024).toFixed(1)+' KiB':file.sizeLabel||'应用固件'):''}`:'';
 }
 function clearImage(){if(remoteFile){api.clear();remoteFile=false;}}
 function chooseVersion(){
  cancel();api.clear();remoteFile=false;const select=$('releaseAsset');select.replaceChildren();option(select,'','请选择应用固件');
  const release=selected();release?.assets.forEach(a=>option(select,a.id,a.name));
  if(release?.assets.length===1)select.value=String(release.assets[0].id);
  details();say(!release?'请选择版本，固件将在点击下载后获取。':!release.assets.length?'此版本没有可在线升级的应用 .bin，请选择其他版本。':'已选择版本，点击“下载所选固件”后才会下载。');controls();
 }
 async function getJSON(url,signal){
  const response=await fetch(url,{signal,headers:{Accept:'application/json'}});
  if(!response.headers.get('content-type')?.includes('application/json'))throw new Error('当前站点未提供 Release 服务，请使用正式站点 ag.233688.xyz。');
  const data=await response.json();if(!response.ok)throw new Error(data.error||'读取 Release 失败，请重试。');return data;
 }
 async function load(page=1){
  if(loading||downloading||api.busy())return;
  loading=true;controls();say('正在读取 Release 列表…');
  const timeout=new AbortController(),timer=setTimeout(()=>timeout.abort(),60000);
  try{
   const data=await getJSON('/api/releases?page='+page,timeout.signal);
   if(!Array.isArray(data.releases))throw new Error('Release 列表格式无效。');
   const previous=$('releaseVersion').value;
   releases=page===1?data.releases:[...releases,...data.releases.filter(r=>!releases.some(old=>old.id===r.id))];nextPage=data.nextPage;
   $('releaseVersion').replaceChildren();option($('releaseVersion'),'','请选择 Release 版本');
   releases.forEach(r=>option($('releaseVersion'),r.id,`${r.name||r.tag}${r.prerelease?' · 预发布':''}${r.assets.length?'':' · 无应用固件'}`));
   $('releaseVersion').value=releases.some(r=>String(r.id)===previous)?previous:'';
   if(!$('releaseVersion').value){$('releaseAsset').replaceChildren();option($('releaseAsset'),'','请先选择版本');details();clearImage();}
   say(releases.length?`已读取 ${releases.length} 个版本。请选择版本后下载，当前尚未自动下载固件。`:'仓库尚未发布 Release，可选择本地应用固件。');
  }catch(error){say(error.name==='AbortError'?'读取列表超时，请重试。':error.message);}
  finally{clearTimeout(timer);loading=false;controls();}
 }
 async function download(){
  const file=asset();if(!file||downloading||api.busy())return;
  const current=++generation;request=new AbortController();downloading=true;controls();
  api.clear();remoteFile=false;say('正在下载 '+file.name+'…');
  const controller=request,timer=setTimeout(()=>controller.abort(),60000);
  try{
   const response=await fetch(file.downloadPath||'/api/releases/assets/'+file.id,{signal:request.signal});
   if(!response.ok){const error=await response.json().catch(()=>({}));throw new Error(error.error||'固件下载失败，请重试。');}
   if(Number(response.headers.get('content-length'))>GyroFirmware.APP_SIZE)throw new Error('固件大小超出应用分区。');
   const reader=response.body.getReader(),parts=[];let length=0;
   try{for(;;){const {done,value}=await reader.read();if(done)break;length+=value.length;if(length>GyroFirmware.APP_SIZE){await reader.cancel();throw new Error('固件大小超出应用分区。');}parts.push(value);say(`正在下载 ${file.name} · ${file.size?Math.min(100,Math.round(length/file.size*100))+'%':(length/1024).toFixed(1)+' KiB'}`);}}finally{reader.releaseLock();}
   if(Number.isSafeInteger(file.size)&&length!==file.size)throw new Error('固件下载不完整，请重试。');
   const bytes=new Uint8Array(length);let offset=0;parts.forEach(part=>{bytes.set(part,offset);offset+=part.length;});
   const digest=[...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(b=>b.toString(16).padStart(2,'0')).join('');
   if(file.digest&&file.digest!=='sha256:'+digest)throw new Error('SHA-256 与 Release 附件不一致，请重新下载。');
   GyroFirmware.validateImage(bytes);
   if(current!==generation||api.busy())return;
   const ready=await api.select(new File([bytes],file.name,{type:'application/octet-stream'}));
   if(current!==generation)return;
   globalThis.GyroUI?.pages?.refresh?.();
   remoteFile=!!ready;
   say(ready?`${file.name} 已下载并通过${file.digest?' SHA-256 和':''}应用校验。连接设备后点击“开始升级”。`:'文件检查未通过，请查看下方提示。');
  }catch(error){if(current===generation)say(error.name==='AbortError'?'下载已取消或超时，可重新下载。':error.message);}
  finally{clearTimeout(timer);if(current===generation){request=null;downloading=false;controls();}}
 }
 $('releaseRefresh').onclick=()=>load();$('releaseMore').onclick=()=>load(nextPage);
 $('releaseVersion').onchange=chooseVersion;
 $('releaseAsset').onchange=()=>{cancel();api.clear();remoteFile=false;details();say('已选择固件，点击“下载所选固件”后才会下载。');controls();};
 $('releaseDownload').onclick=download;$('releaseCancel').onclick=()=>{cancel();say('已取消下载。');};
 $('firmwareFile').addEventListener('change',()=>{cancel();remoteFile=false;$('releaseVersion').value='';$('releaseAsset').replaceChildren();option($('releaseAsset'),'','请先选择版本');details();say('当前使用本地固件文件。');controls();});
 new MutationObserver(controls).observe($('firmwareFile'),{attributes:true,attributeFilter:['disabled']});
 let entered=false;function enter(){if(document.body.dataset.view==='upgrade'&&!entered){entered=true;load();}}
 window.addEventListener('hashchange',()=>setTimeout(enter));enter();controls();
})();
