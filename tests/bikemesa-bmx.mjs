// Real controller selection, landing ejection and independent Box3D lifecycle.
// node tests/bikemesa-bmx.mjs build/djbike-cart/carts/bikemesa.wasc webgpu
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdir,writeFile} from 'node:fs/promises';
const cart=resolve(process.argv[2]),backend=process.argv[3]||'webgpu';
const out=resolve('out/bmx',backend);await mkdir(out,{recursive:true});
async function call(tool,args){
 const r=await fetch(`${process.env.ROMDEV_URL||"http://localhost:7331"}/tool/${tool}`,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':`bikemesa-bmx-${backend}`},body:JSON.stringify(args)});
 const j=await r.json();assert(r.ok&&!j.error,JSON.stringify(j));return j;
}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply;
const step=frames=>call('frame',{op:'step',frames});
const hold=buttons=>call('input',{op:'set',ports:[buttons]});
const fields=()=>cmd('fields');
async function tap(button){await hold({[button]:true});await step(2);await hold({});await step(15);}
const shot=name=>call('frame',{op:'screenshot',path:resolve(out,name+'.png')});
const clean=f=>{for(const k of ['cw.gpu.errors','cw.three.missing','render.skipped'])assert.equal(f[k],0,k);};
const checks=[];
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});
await step(90);await tap('south');await tap('south');await tap('left');await tap('left');
let f=await fields();assert.equal(f['bike.model'],5);assert.equal(f['bike.color'],2);clean(f);await shot('selection');
checks.push('sixth selectable bike defaults to blue');
await cmd('track 1');await hold({south:true});await step(280);
f=await fields();assert.equal(f.crashes,0);assert.equal(f['alien.gone'],0);assert(f['bike.pos'][0]>4);await shot('riding');
await hold({south:true,west:true});await step(2);await hold({south:true});
// The original normal hop used to spill him: protect that regression.
for(let n=0;n<20;n++) {
 await step(6);f=await fields();assert.equal(f.crashes,0);
 assert.equal(f['alien.gone'],0,'ordinary hop and landing keep passenger seated');clean(f);
}
assert(f.hops>0);checks.push('ordinary bunny hop retains the passenger');
await cmd('track 1');await hold({south:true});await step(280);
await hold({south:true,west:true,right:true});await step(2);await hold({south:true,right:true});
let tossed=false;const samples=[];
for(let n=0;n<60;n++) {
 await step(6);f=await fields();samples.push(f);
 assert.equal(f.crashes,0,'ejection does not require a rider crash');clean(f);
 if(f['alien.gone']){tossed=true;break;}
}
assert(tossed,'substantial airborne inversion ejects the passenger');
await hold({south:true,left:true});assert(f.hops>0);await shot('ejected');
const first=f['alien.parts'];assert.equal(first.length,56);assert(first.every(Number.isFinite));
await step(24);f=await fields();const second=f['alien.parts'];await shot('flailing');clean(f);
assert(Math.hypot(second[0]-first[0],second[1]-first[1],second[2]-first[2])>.3,'body moves independently');
const angleChange=Math.abs(second[17]-first[17])+Math.abs(second[18]-first[18])+Math.abs(second[19]-first[19]);
assert(angleChange>.05,'articulated arm rotates in Box3D');
await hold({south:true});await step(30);f=await fields();clean(f);
checks.push('airborne inversion ejection, independent moving body and flailing arm');
for(let n=0;n<20&&f['game.state']!==3;n++){await step(20);await tap('south');f=await fields();}
assert.equal(f['game.state'],3,'rider recovers from the inversion');
await tap('select');f=await fields();assert.equal(f['alien.gone'],1,'checkpoint reset does not replace passenger');assert(f.resets>0);clean(f);await shot('checkpoint-empty');
checks.push('passenger stays behind through checkpoint respawn');
// Stop short of the passenger and let him settle before riding back over him.
const hitsBefore=f['alien.bike_hits'];
await hold({east:true});await step(90);await hold({});await step(60);
f=await fields();const settled=f['alien.parts'].slice(0,3);
let contacted=false;
for(let n=0;n<100;n++) {
 const target=Math.max(0,Math.min(3,Math.round(f['alien.parts'][2]/.9+1.5)));
 const lane=f['race.lanes'][0];
 await hold({south:true,...(target>lane?{down:true}:target<lane?{up:true}:{})});
 await step(6);f=await fields();clean(f);
 if(f['alien.bike_hits']>hitsBefore){contacted=true;break;}
}
assert(contacted,'returning bike physically contacts the fallen alien after checkpoint');
await step(12);f=await fields();clean(f);
assert(Math.hypot(...f['alien.parts'].slice(0,3).map((v,i)=>v-settled[i]))>.1,'wheel pushes the fallen alien');
await shot('return-collision');checks.push('returning bike knocks the fallen alien after checkpoint');

await hold({});await cmd('track 2');await step(220);f=await fields();assert.equal(f['alien.gone'],0,'next level restores passenger');assert.equal(f['bike.model'],5);clean(f);await shot('next-level');
checks.push('next level restores passenger in crate');
await cmd('track 1');await hold({south:true});await step(280);
await hold({south:true,west:true,right:true});await step(2);await hold({south:true,right:true});
let crashed=false;
for(let n=0;n<60;n++){await step(12);f=await fields();if(f.crashes){crashed=true;break;}}
assert(crashed,'BMX can crash with the crate attached');assert.equal(f['alien.gone'],1);
await step(30);f=await fields();assert(f['alien.parts'].every(Number.isFinite));clean(f);await shot('crash');
await tap('select');f=await fields();assert.equal(f['alien.gone'],1);clean(f);
checks.push('3D bike/crate crash and empty-crate recovery');

await writeFile(resolve(out,'report.json'),JSON.stringify({cart,backend,checks,samples,final:f},null,2));
console.log(JSON.stringify({backend,checks}));
