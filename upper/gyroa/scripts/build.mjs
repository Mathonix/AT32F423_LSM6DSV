import {cp,mkdir,readdir,readFile,writeFile,rm} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {dirname,join,resolve,sep,relative} from 'node:path';
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {build} from 'esbuild';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const source=join(root,'src/public'),target=resolve(root,'dist/public');
// Only remove this project's generated public directory; never touch the editable source.
if(target!==join(root,'dist/public') || !target.startsWith(root+sep) || target===source) throw new Error('Unsafe build target');
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
await build({entryPoints:[join(root,'src/index.js')],bundle:true,format:'esm',platform:'browser',target:'es2022',outfile:join(root,'dist/worker.js')});
const hashes={};
for(const path of await files(target)) hashes[relative(target,path).split(sep).join('/')]=createHash('sha256').update(await readFile(path)).digest('hex');
const manifest={version:'20261004biasfix1',domain:'gyroa.233688.xyz',firmware:'20261004d',configVersions:[1,2,3,4],files:hashes};
await writeFile(join(root,'dist/asset-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(`Built ${assets.length} static assets and worker; target ${manifest.domain}. No deployment performed.`);
