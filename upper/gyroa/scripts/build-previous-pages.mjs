import {mkdir,readdir,readFile,writeFile,rm,cp} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {dirname,join,resolve,sep,relative} from 'node:path';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {build} from 'esbuild';

// Pin the actual 2026-10-23 Pages production commit. Never read the editable
// current UI or reset the checkout when rebuilding this historical release.
const ref='1ab7765c21781040d57d8f002467bc404a5fbc5d';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const repository=resolve(root,'../..');
const snapshot=join(root,'dist/previous-source'),target=join(root,'dist/pages-previous');
for(const folder of [snapshot,target]) {
  if(!folder.startsWith(join(root,'dist')+sep) || folder===join(root,'dist'))throw new Error('Unsafe historical build target');
  await rm(folder,{recursive:true,force:true});
  await mkdir(folder,{recursive:true});
}
const git=(...args)=>execFileSync('git',args,{cwd:repository,maxBuffer:32*1024*1024});
const paths=git('ls-tree','-rz','--name-only',ref,'--','upper/gyroa/src/').toString().split('\0').filter(Boolean);
for(const path of paths) {
  if(!path.startsWith('upper/gyroa/src/') || path.split('/').includes('..'))throw new Error('Unexpected historical source path');
  const output=resolve(snapshot,path.slice('upper/gyroa/'.length));
  if(!output.startsWith(snapshot+sep))throw new Error('Historical source escaped build folder');
  await mkdir(dirname(output),{recursive:true});
  await writeFile(output,git('show',`${ref}:${path}`));
}
async function files(folder) {
  const result=[];
  for(const entry of await readdir(folder,{withFileTypes:true})) {
    if(entry.isSymbolicLink())throw new Error('Unexpected historical asset symlink');
    const path=join(folder,entry.name);
    if(entry.isDirectory())result.push(...await files(path));else result.push(path);
  }
  return result.sort();
}
for(const path of (await files(join(snapshot,'src'))).filter(p=>p.endsWith('.js')))
  execFileSync(process.execPath,['--check',path],{stdio:'pipe'});
await cp(join(snapshot,'src/public'),target,{recursive:true});
await build({entryPoints:[join(snapshot,'src/index.js')],bundle:true,format:'esm',platform:'browser',target:'es2022',outfile:join(target,'_worker.js')});
await writeFile(join(target,'_routes.json'),JSON.stringify({version:1,include:['/healthz'],exclude:[]},null,2)+'\n');
const hashes={};
for(const path of await files(target))hashes[relative(target,path).split(sep).join('/')]=createHash('sha256').update(await readFile(path)).digest('hex');
const manifest={version:'20261023runtime1',ref,domain:'gyroa-upper-previous.pages.dev',files:hashes};
await writeFile(join(root,'dist/previous-pages-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(`Built ${Object.keys(hashes).length-2} historical assets and Pages Functions from ${ref}; target ${manifest.domain}. No deployment performed.`);
