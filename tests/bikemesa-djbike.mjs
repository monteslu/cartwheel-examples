// Exercise the real carousel and controller inputs through romdev.
// node tests/bikemesa-djbike.mjs build/djbike-cart/carts/bikemesa.wasc webgpu
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdir,writeFile} from 'node:fs/promises';
const cart=resolve(process.argv[2]), backend=process.argv[3]||'webgpu';
const out=resolve('out/djbike',backend);await mkdir(out,{recursive:true});
const session=`bikemesa-djbike-test-${backend}`;
async function call(tool,args) {
 const r=await fetch(`${process.env.ROMDEV_URL||"http://localhost:7331"}/tool/${tool}`,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':session},body:JSON.stringify(args)});
 const result=await r.json();assert(r.ok,JSON.stringify(result));assert(!result.error,JSON.stringify(result));return result;
}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply;
const step=frames=>call('frame',{op:'step',frames});
const press=button=>call('input',{op:'press',button,frames:1});
const hold=ports=>call('input',{op:'set',ports:[ports]});
const shot=name=>call('frame',{op:'screenshot',path:resolve(out,`${name}.png`)});
const fields=()=>cmd('fields');
const checks=[];
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});
await step(90);await press('south');await step(20);await press('south');await step(20);
await press('right');await step(3);await press('right');await step(3);await press('right');await step(3);
let f=await fields();assert.equal(f['bike.model'],3,'carousel reaches DJ cargo');
await shot('selected');checks.push('DJ available through controller carousel');
await press('south');await step(20);await press('south');await step(20);await press('south');await step(20);
// Use the same normal track loader for reproducible handling checks.
await cmd('track 1');await hold({south:true});await step(300);
f=await fields();assert.equal(f['crashes'],0,'stable rolling start');assert(f['bike.pos'][0]>3,'pedals propel loaded bike');
const xStart=f['bike.pos'][0];await step(90);f=await fields();
const travel=f['bike.pos'][0]-xStart;assert(travel>1,'continues accelerating');
await hold({south:true,down:true});await step(12);f=await fields();
assert(f['bike.lane_ticks']>0,'steering in progress');await shot('steering');
await hold({south:true});await step(45);f=await fields();
assert.equal(f['race.lanes'][0],2,'lane change completed');assert.equal(f['bike.lane_ticks'],0);
checks.push('pedalling and slower articulated lane change');
// Brake on the flat start, rather than measuring distance on the downhill
// reached at a timing-sensitive point after the lane change.
await cmd('track 1');await hold({south:true});await step(280);
const brakeStart=(await fields())['bike.velocity'][0];assert(brakeStart>1);
await hold({east:true});await step(90);f=await fields();
assert(Math.abs(f['bike.velocity'][0])<1,'brake arrests forward motion on flat ground');
checks.push('braking');await shot('braked');
await press('select');await step(20);f=await fields();assert(f['resets']>0,'checkpoint respawn');
assert.equal(f['bike.model'],3,'respawn preserves selected model');checks.push('respawn retains DJ bike');
await cmd('track 1');await hold({south:true});await step(280);
await hold({south:true,west:true,right:true});await step(2);await hold({south:true,right:true});
let crashed=false;
for(let n=0;n<50;n++) {
 await step(12);f=await fields();
 if(f['crashes']>0){crashed=true;await shot('crash');break;}
}
assert(crashed,'loaded tricycle can enter a physical crash');
assert(f['hops']>0,'hop input launches cargo bike');
await hold({});await press('select');await step(30);f=await fields();
assert.equal(f['bike.model'],3);assert.equal(f['cw.gpu.errors'],0);assert.equal(f['render.skipped'],0);
checks.push('hop, 3D crash and checkpoint recovery');
// These used to be impassable under the DJ bike's cargo load. Start from
// actual checkpoint respawns so passing does not depend on a long run-up.
await cmd('bike_pick 3 1');await step(2);
for (const test of [
 {name:'downtown-climb',command:'track 3 2',goal:212,finish:false},
 {name:'powder-gap',command:'track 2 3',goal:411,finish:true},
]) {
 await hold({south:true});await cmd(test.command);
 let passed=false,flightShot=false;const samples=[];
 for(let n=0;n<150;n++) {
  await step(12);f=await fields();
  samples.push({position:f['bike.pos'],velocity:f['bike.velocity'],state:f['game.state']});
  assert.equal(f['crashes'],0,`${test.name}: crash before clearing obstacle`);
  if(test.finish&&!flightShot&&f['bike.pos'][0]>373) {
   await shot('powder-flight');flightShot=true;
  }
  if(test.finish?f['finished']>0:f['bike.pos'][0]>=test.goal) {passed=true;break;}
 }
 assert(passed,`${test.name}: bike must clear the course obstacle with pedal held`);
 assert.equal(f['render.skipped'],0);assert.equal(f['cw.gpu.errors'],0);
 await shot(test.name);
 await writeFile(resolve(out,`${test.name}.json`),JSON.stringify(samples,null,2));
 checks.push(test.name);
}
for(let model=0;model<7;model++) {
 await cmd(`bike_pick ${model} 1`);await step(20);f=await fields();
 assert.equal(f['bike.model'],model);assert.equal(f['cw.gpu.errors'],0);
 assert.equal(f['render.skipped'],0,`no invisible geometry for model ${model}`);
 await shot(`model-${model}`);
}
checks.push('all seven models render without skipped geometry or GPU errors');
await writeFile(resolve(out,'report.json'),JSON.stringify({cart,backend,checks,travel,fields:f},null,2));
console.log(JSON.stringify({backend,checks,travel}));
