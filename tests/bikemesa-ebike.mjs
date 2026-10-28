// Low-speed electric assist, using the normal pedal/brake inputs.
// node tests/bikemesa-ebike.mjs build/djbike-cart/carts/bikemesa.wasc webgpu
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdir,writeFile} from 'node:fs/promises';
const cart=resolve(process.argv[2]),backend=process.argv[3]||'webgpu';
async function call(tool,args){
 const r=await fetch(`http://localhost:7331/tool/${tool}`,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':`bikemesa-ebike-${backend}`},body:JSON.stringify(args)});
 const j=await r.json();assert(r.ok&&!j.error,JSON.stringify(j));return j;
}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply;
const step=frames=>call('frame',{op:'step',frames});
const hold=buttons=>call('input',{op:'set',ports:[buttons]});
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});
await step(90);await cmd('bike_pick 2');await cmd('track 1');await hold({});await step(200);
let f=await cmd('fields');assert.equal(f['bike.model'],2);assert(Math.abs(f['bike.velocity'][0])<.1);
const samples=[f];await hold({south:true});
for(let n=0;n<10;n++){
 await step(12);f=await cmd('fields');samples.push(f);
 assert.equal(f.crashes,0);assert(Math.abs(f['bike.angle'])<.2,'launch stays stable');
 assert.equal(f['cw.gpu.errors'],0);assert.equal(f['cw.three.missing'],0);assert.equal(f['render.skipped'],0);
}
assert(samples[5]['bike.velocity'][0]>4,'strong launch in the first second');
assert(f['bike.velocity'][0]>6,'assist transitions smoothly into normal riding');
const launchSpeed=f['bike.velocity'][0];
await hold({east:true});await step(24);f=await cmd('fields');
assert(f['bike.velocity'][0]<launchSpeed-.5,'braking overrides electric assist');assert.equal(f.crashes,0);
const out=resolve('out/ebike',backend);await mkdir(out,{recursive:true});
await writeFile(resolve(out,'launch.json'),JSON.stringify({cart,samples,braking:f},null,2));
console.log(`${backend}: e-bike launch, stability, braking and renderer checks passed`);
