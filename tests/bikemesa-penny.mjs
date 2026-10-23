// Actual controller selection, direct-drive animation and course checks.
// node tests/bikemesa-penny.mjs build/djbike-cart/carts/bikemesa.wasc webgpu
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdir,writeFile} from 'node:fs/promises';
const cart=resolve(process.argv[2]),backend=process.argv[3]||'webgpu';
const out=resolve('out/penny',backend);await mkdir(out,{recursive:true});
async function call(tool,args){
 const r=await fetch(`http://localhost:7331/tool/${tool}`,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':`bikemesa-penny-${backend}`},body:JSON.stringify(args)});
 const j=await r.json();assert(r.ok&&!j.error,JSON.stringify(j));return j;
}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply;
const step=frames=>call('frame',{op:'step',frames});
const hold=buttons=>call('input',{op:'set',ports:[buttons]});
async function tap(button,frames=15){await hold({[button]:true});await step(2);await hold({});await step(frames);}
const fields=()=>cmd('fields');
const shot=name=>call('frame',{op:'screenshot',path:resolve(out,name+'.png')});
const clean=f=>{assert.equal(f['render.skipped'],0);assert.equal(f['cw.gpu.errors'],0);assert.equal(f['cw.three.missing'],0);};
const checks=[];
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});
await step(90);await tap('south');await tap('south');
const defaults=[0,1,7,1,6];
for(let i=0;i<5;i++) {
 const f=await fields();assert.equal(f['bike.model'],i);assert.equal(f['bike.color'],defaults[i]);clean(f);
 await shot(`selection-${i}`);if(i<4)await tap('right',3);
}
checks.push('controller carousel defaults: green, red, white, red, black');
await tap('south');await tap('right');assert.equal((await fields())['bike.color'],7,'custom white paint available');
await tap('east');await tap('left');let f=await fields();assert.equal(f['bike.model'],3);assert.equal(f['bike.color'],1,'changing frame restores its default');
await tap('right');await tap('south');await tap('south');await tap('south');
await cmd('track 1');await hold({south:true});await step(430);
f=await fields();assert.equal(f.crashes,0);assert(f['bike.pos'][0]>15);clean(f);await shot('riding');
await hold({});const before=await fields();await step(5);const after=await fields();
assert(after['bike.pos'][0]>before['bike.pos'][0]);assert(Math.abs(after['bike.crank']-before['bike.crank'])>.05,'fixed front-hub pedals keep turning while coasting');
checks.push('front-hub direct drive and coasting pedal animation');
await hold({south:true,down:true});await step(4);f=await fields();
assert(Math.abs(f['bike.steering'])>.2,'fork visibly steers during lane change');await shot('steering-out');
await hold({south:true});await step(7);f=await fields();assert(f['bike.steering']>0,'countersteer straightens into the next lane');await shot('steering-in');
await step(12);f=await fields();assert.equal(f['bike.steering'],0);assert.equal(f['race.lanes'][0],2);clean(f);
checks.push('fork, wheel, hub pedals and rider articulate through a lane change');

await cmd('track 1');await hold({south:true});await step(290);
await hold({east:true});await step(120);f=await fields();assert.equal(f.crashes,0);assert(f['bike.velocity'][0]<2,'brakes arrest forward travel');
await tap('select');assert((await fields()).resets>0);assert.equal((await fields())['bike.model'],4);
checks.push('brake and checkpoint reset');
await cmd('track 1');await hold({south:true});await step(250);
await hold({south:true,west:true});await step(2);await hold({south:true});await step(8);
f=await fields();assert(f.hops>0);await shot('hop');
await hold({south:true,right:true});let crashed=false;
for(let n=0;n<50;n++){await step(12);f=await fields();if(f.crashes){crashed=true;await shot('crash');break;}}
assert(crashed,'lean can produce a physical crash');clean(f);await tap('select');
checks.push('hop, lean, crash and recovery');
for(const test of [{name:'downtown-climb',track:3,cp:2,goal:212},{name:'powder-gap',track:2,cp:3,goal:411}]) {
 await hold({south:true});await cmd(`track ${test.track} ${test.cp}`);let passed=false;const samples=[];
 for(let n=0;n<150;n++) {
  await step(12);f=await fields();samples.push({position:f['bike.pos'],velocity:f['bike.velocity'],crashes:f.crashes});
  assert.equal(f.crashes,0,test.name);if(f.finished||f['bike.pos'][0]>=test.goal){passed=true;break;}
 }
 assert(passed,test.name);clean(f);await shot(test.name);await writeFile(resolve(out,test.name+'.json'),JSON.stringify(samples,null,2));checks.push(test.name);
}
await writeFile(resolve(out,'report.json'),JSON.stringify({cart,backend,checks,fields:f},null,2));
console.log(JSON.stringify({backend,checks}));
