import {cp,mkdir,readdir,readFile,writeFile,rm} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {dirname,join,resolve,sep,relative} from 'node:path';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {build} from 'esbuild';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const pages=process.argv.includes('--pages');
const domain=process.argv.find(arg=>arg.startsWith('--domain='))?.slice('--domain='.length)||(pages?'gyroa-upper.pages.dev':'gyroa.233688.xyz');
const output=pages?'pages':'public';
const source=join(root,'src/public'),target=resolve(root,'dist',output);
// Only remove the selected generated output inside this project's dist folder.
if(target!==join(root,'dist',output) || !target.startsWith(join(root,'dist')+sep) || target===source) throw new Error('Unsafe build target');
async function files(dir) {
  const result=[];
  for(const item of await readdir(dir,{withFileTypes:true})) {
    const path=join(dir,item.name);
    if(item.isSymbolicLink()) throw new Error('Unexpected symlink in assets: '+path);
    if(item.isDirectory()) result.push(...await files(path));else result.push(path);
  }
  return result.sort();
}
const assets=await files(source);
for(const path of assets.filter(p=>p.endsWith('.js'))) execFileSync(process.execPath,['--check',path],{stdio:'pipe'});
execFileSync(process.execPath,['--check',join(root,'src/index.js')],{stdio:'pipe'});
await mkdir(join(root,'dist'),{recursive:true});
await rm(target,{recursive:true,force:true});
await cp(source,target,{recursive:true});
await build({entryPoints:[join(root,'src/index.js')],bundle:true,format:'esm',platform:'browser',target:'es2022',outfile:pages?join(target,'_worker.js'):join(root,'dist/worker.js')});
if(pages) {
  // Pages Advanced Mode reuses the existing API Worker. All other requests
  // go directly to Pages static hosting, including the hash-routed UI.
  await writeFile(join(target,'_routes.json'),JSON.stringify({version:1,include:['/healthz','/api/releases','/api/releases/*'],exclude:[]},null,2)+'\n');
}
const hashes={};
for(const path of await files(target)) hashes[relative(target,path).split(sep).join('/')]=createHash('sha256').update(await readFile(path)).digest('hex');
const manifest={version:'20261025webusb1',domain,firmware:'20261023q',firmwareSha256:'86466a07f7ce9a9d8d4d667abe62cda857a26850c6facf919af0fddf7ebf9b01',eskfRuntimeAbi:1,configVersions:[1,2,3,4,5],files:hashes};
await writeFile(join(root,'dist',pages?'pages-manifest.json':'asset-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(`Built ${assets.length} static assets and ${pages?'Pages Functions':'worker'}; target ${manifest.domain}. No deployment performed.`);
