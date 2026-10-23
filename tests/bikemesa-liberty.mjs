// Visual/renderer regression for the Liberty Leap arena on both cart backends.
// node tests/bikemesa-liberty.mjs build/djbike-cart/carts/bikemesa.wasc webgpu
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdir,writeFile} from 'node:fs/promises';
const cart=resolve(process.argv[2]),backend=process.argv[3]||'webgpu';
const out=resolve('out/liberty',backend);await mkdir(out,{recursive:true});
async function call(tool,args){
 const r=await fetch(`http://localhost:7331/tool/${tool}`,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':`bikemesa-liberty-${backend}`},body:JSON.stringify(args)});
 const j=await r.json();assert(r.ok&&!j.error,JSON.stringify(j));return j;
}
const cmd=async text=>{const j=(await call('wasm',{op:'command',text})).reply;assert(!j.error,JSON.stringify(j));return j;};
const step=frames=>call('frame',{op:'step',frames});
const shot=name=>call('frame',{op:'screenshot',path:resolve(out,name+'.png')});
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});
await step(45);await cmd('bike_pick 3 1');await step(2);await cmd('track 18');await step(240);
await shot('start');
// Freeze simulation to inspect the actual world from consistent camera angles.
await cmd('pause 1');
async function inspect(name,eye,target){
 await cmd(`inspect ${eye.join(' ')} ${target.join(' ')}`);
 await step(2);await shot(name);
}
await inspect('flags',[27,2.6,0],[10,1.5,-8]);
await inspect('first-jump',[86,8,28],[78,-1,0]);
await inspect('fire-ring',[205,11,24],[193,1,0]);
await inspect('finish',[328,-12,26],[315,-18,-2]);
const tree=await cmd('tree');const nodes=tree.children[0].children;
for(const name of ['Liberty ring fire','Liberty ring sparks','Liberty ring smoke']) {
 const node=nodes.find(n=>n.name===name);assert(node,`${name} exists`);
 const props=await cmd(`get #${node.uid} rotation alive`);
 assert(props.values.rotation.value.includes('1.570'),`${name} lies in YZ`);
 assert(Number(props.values.alive.value)>0,`${name} has live particles`);
}
await cmd('inspect off');await cmd('pause 0');await cmd('track 18 2');
await call('input',{op:'set',ports:[{south:true}]});
// Observe the real ride through the arena as well as stationary inspection.
await step(60);const underrunsBefore=(await cmd('fields'))['audio.underruns'];
await step(340);await shot('riding-ring');
let f=await cmd('fields');assert.equal(f['render.skipped'],0);assert.equal(f['cw.gpu.errors'],0);assert.equal(f['cw.three.missing'],0);
assert.equal(f['audio.underruns'],underrunsBefore,'no stream starvation during normal riding after debugger pause');assert(f['audio.voices']>0);
await cmd('track 1');await step(30);await cmd('track 18 3');await step(30);
f=await cmd('fields');assert.equal(f['render.skipped'],0,'animated flag lifetimes survive scene reload');assert.equal(f['cw.gpu.errors'],0);
await writeFile(resolve(out,'report.json'),JSON.stringify({cart,backend,fields:f,checks:['arena and close-up captures','ring particle plane and live emission','no missing shaders or skipped geometry','audio active without additional underruns during riding','scene reload']},null,2));
console.log(JSON.stringify({backend,skipped:f['render.skipped'],gpuErrors:f['cw.gpu.errors']}));
