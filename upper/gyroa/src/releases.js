const REPO='Mathonix/AT32F423_LSM6DSV';
const API=`https://api.github.com/repos/${REPO}`;
const APP_SIZE=0x34000;
const json=(data,status=200)=>new Response(JSON.stringify(data),{status,headers:{'content-type':'application/json; charset=utf-8','cache-control':status===200?'public, max-age=120':'no-store'}});
function applicationName(name){return typeof name==='string'&&/\.bin$/i.test(name)&&!/(bootloader|boot[-_. ]|full[-_ ]?(chip|flash)?|combined|merged|factory|(?:^|[-_])bl(?:[-_.]|$))/i.test(name);}
export function applicationAsset(asset){
  return Number.isSafeInteger(asset?.id)&&asset.id>0&&asset.state==='uploaded'&&applicationName(asset.name)&&
    Number.isSafeInteger(asset.size)&&asset.size>=8&&asset.size<=APP_SIZE;
}
function downloadURL(value){
  try{const url=new URL(value);return url.protocol==='https:'&&url.hostname==='github.com'&&url.pathname.startsWith(`/${REPO}/releases/download/`)?url.href:null;}catch{return null;}
}
async function limitedBytes(response,max){
  if(Number(response.headers.get('content-length'))>max)throw new Error('文件超过大小限制');
  const reader=response.body?.getReader();if(!reader)throw new Error('服务器返回空内容');
  const parts=[];let length=0;
  try{for(;;){const {done,value}=await reader.read();if(done)break;length+=value.length;if(length>max){await reader.cancel();throw new Error('文件超过大小限制');}parts.push(value);}}finally{reader.releaseLock();}
  const bytes=new Uint8Array(length);let offset=0;for(const part of parts){bytes.set(part,offset);offset+=part.length;}return bytes;
}
async function github(path,env){
  const headers={'User-Agent':'AT32-AHRS-Web-Host','Accept':'application/vnd.github+json','X-GitHub-Api-Version':'2026-03-10'};
  if(env.GITHUB_TOKEN)headers.Authorization=`Bearer ${env.GITHUB_TOKEN}`;
  const response=await fetch(API+path,{headers,signal:AbortSignal.timeout(15000)});
  if(!response.ok){await response.body?.cancel();throw new Error(response.status===403||response.status===429?'GitHub 请求暂时受限，请稍后重试。':response.status===404?'Release 或固件文件不存在。':'GitHub 暂时无法访问，请稍后重试。');}
  const bytes=await limitedBytes(response,2*1024*1024);
  return {data:JSON.parse(new TextDecoder().decode(bytes)),more:/rel="next"/.test(response.headers.get('link')||'')};
}
const webDownloadPath=(tag,name)=>`/api/releases/download?tag=${encodeURIComponent(tag)}&file=${encodeURIComponent(name)}`;
function unescape(value){return value.replace(/&(#x[0-9a-f]+|#\d+|amp|lt|gt|quot|apos);/gi,(_,code)=>{if(code[0]==='#'){const n=code[1]?.toLowerCase()==='x'?parseInt(code.slice(2),16):Number(code.slice(1));return n>=0&&n<=0x10ffff?String.fromCodePoint(n):'';}return {amp:'&',lt:'<',gt:'>',quot:'"',apos:"'"}[code.toLowerCase()];});}
async function webText(path,max=512*1024){
  const response=await fetch(`https://github.com/${REPO}/${path}`,{headers:{'User-Agent':'AT32-AHRS-Web-Host'},signal:AbortSignal.timeout(15000)});
  if(!response.ok){await response.body?.cancel();throw new Error('Release 页面暂时无法读取，请重试。');}
  return new TextDecoder().decode(await limitedBytes(response,max));
}
export function parseWebAssets(html,tag){
  return (html.match(/<li\b[\s\S]*?<\/li>/g)||[]).flatMap(item=>{
    const href=unescape(item.match(/<a\b[^>]*href="([^"]+)"/)?.[1]||''),source=downloadURL('https://github.com'+href);
    if(!source)return [];
    const path=new URL(source).pathname,prefix=`/${REPO}/releases/download/`,parts=path.slice(prefix.length).split('/');
    let name,assetTag;try{name=decodeURIComponent(parts.pop());assetTag=decodeURIComponent(parts.join('/'));}catch{return [];}
    if(assetTag!==tag||!applicationName(name)||/[\\/\x00-\x1f]/.test(name))return [];
    return [{id:tag+'/'+name,name,size:null,sizeLabel:item.match(/>([\d.]+\s*(?:Bytes|KB|MB))<\/span>/)?.[1]||'',digest:item.match(/sha256:[a-f0-9]{64}/i)?.[0].toLowerCase()||null,downloadPath:webDownloadPath(tag,name)}];
  });
}
async function webAssets(tag){return parseWebAssets(await webText(`releases/expanded_assets/${encodeURIComponent(tag)}`),tag);}
async function feedReleases(){
  const feed=await webText('releases.atom',2*1024*1024),entries=(feed.match(/<entry>[\s\S]*?<\/entry>/g)||[]).slice(0,20);
  const releases=await Promise.all(entries.map(async entry=>{
    const link=unescape(entry.match(/<link\b[^>]*href="([^"]+)"/)?.[1]||''),prefix=`https://github.com/${REPO}/releases/tag/`;
    if(!link.startsWith(prefix))return null;
    let tag;try{tag=decodeURIComponent(link.slice(prefix.length));}catch{return null;}
    try{return {id:'tag:'+tag,tag,name:unescape(entry.match(/<title>([\s\S]*?)<\/title>/)?.[1]||tag),publishedAt:entry.match(/<updated>([^<]+)<\/updated>/)?.[1]||null,prerelease:false,notes:unescape(unescape(entry.match(/<content\b[^>]*>([\s\S]*?)<\/content>/)?.[1]||'').replace(/<[^>]*>/g,' ')).slice(0,6000),url:link,assets:await webAssets(tag)};}catch{return null;}
  }));
  if(!entries.length||!releases.some(Boolean))throw new Error('Release 列表暂时无法读取，请稍后重试。');
  return releases.filter(Boolean);
}
async function binaryResponse(source,expectedSize=null){
  const response=await fetch(source,{signal:AbortSignal.timeout(30000),redirect:'follow'});
  if(!response.ok){await response.body?.cancel();throw new Error('固件下载失败，请重试。');}
  const bytes=await limitedBytes(response,APP_SIZE);if(bytes.length<8||(expectedSize!==null&&bytes.length!==expectedSize))throw new Error('固件下载不完整，请重试。');
  return new Response(bytes,{headers:{'content-type':'application/octet-stream','content-length':String(bytes.length),'cache-control':'no-store','x-content-type-options':'nosniff'}});
}
export async function releaseResponse(request,env={},ctx={}){
  const url=new URL(request.url);if(!url.pathname.startsWith('/api/releases'))return null;
  if(request.method!=='GET')return json({error:'仅支持读取 Release。'},405);
  try{
    if(url.pathname==='/api/releases'){
      const raw=url.searchParams.get('page')||'1';if(!/^[1-9]\d{0,2}$/.test(raw))return json({error:'页码无效。'},400);
      const page=Number(raw),key=new Request(`${url.origin}/api/releases?page=${page}&format=2`),cache=globalThis.caches?.default;
      const cached=await cache?.match(key);if(cached)return cached;
      let data,more;try{({data,more}=await github(`/releases?per_page=30&page=${page}`,env));}catch(error){if(page!==1)throw error;const response=json({repository:REPO,releases:await feedReleases(),nextPage:null});if(cache&&ctx.waitUntil)ctx.waitUntil(cache.put(key,response.clone()));return response;}
      if(!Array.isArray(data))throw new Error('Release 列表格式无效。');
      const releases=data.filter(r=>!r.draft).map(r=>({id:r.id,tag:String(r.tag_name||''),name:String(r.name||r.tag_name||''),publishedAt:r.published_at,prerelease:!!r.prerelease,notes:String(r.body||'').slice(0,6000),url:`https://github.com/${REPO}/releases/tag/${encodeURIComponent(r.tag_name)}`,assets:(r.assets||[]).filter(applicationAsset).filter(a=>downloadURL(a.browser_download_url)).map(a=>({id:a.id,name:a.name,size:a.size,digest:/^sha256:[a-f0-9]{64}$/i.test(a.digest||'')?a.digest.toLowerCase():null,downloadPath:webDownloadPath(r.tag_name,a.name)}))}));
      const response=json({repository:REPO,releases,nextPage:more?page+1:null});
      if(cache&&ctx.waitUntil)ctx.waitUntil(cache.put(key,response.clone()));return response;
    }
    if(url.pathname==='/api/releases/download'){
      const tag=url.searchParams.get('tag'),name=url.searchParams.get('file');
      if(!tag||tag.length>200||tag==='.'||tag==='..'||/[\x00-\x1f]/.test(tag)||!name||name.length>200||/[\\/\x00-\x1f]/.test(name)||!applicationName(name))return json({error:'固件地址无效。'},400);
      const assets=await webAssets(tag);if(!assets.some(a=>a.name===name))return json({error:'Release 中不存在此应用固件。'},404);
      return await binaryResponse(`https://github.com/${REPO}/releases/download/${encodeURIComponent(tag)}/${encodeURIComponent(name)}`);
    }
    const match=url.pathname.match(/^\/api\/releases\/assets\/([1-9]\d{0,15})$/);
    if(!match)return json({error:'Release 地址无效。'},404);
    const id=Number(match[1]);if(!Number.isSafeInteger(id))return json({error:'固件编号无效。'},400);
    const {data:asset}=await github(`/releases/assets/${id}`,env),source=downloadURL(asset.browser_download_url);
    if(!applicationAsset(asset)||!source)return json({error:'此附件不是可在线升级的应用固件。'},400);
    return await binaryResponse(source,asset.size);
  }catch(error){return json({error:error.name==='TimeoutError'?'GitHub 请求超时，请重试。':error.message||'暂时无法读取 Release。'},502);}
}
