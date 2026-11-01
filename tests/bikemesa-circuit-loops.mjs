// Black Circuit's two mirrored loops: the folding bike goes round both from either entry lane and comes
// back out on the lane it entered. BIKEMESA_GPU=gl selects the host's GL path.
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
async function call(t,a){let r=await fetch((process.env.ROMDEV_URL||'http://localhost:7331')+'/tool/'+t,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':'bikemesa-circuit-loops'},body:JSON.stringify(a),signal:AbortSignal.timeout(90000)});let j=await r.json();assert(!j.error,JSON.stringify(j));return j;}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply;
await call('loadMedia',{platform:'wasmcart',path:resolve(process.argv[2]),wgpu:process.env.BIKEMESA_GPU==='gl'?false:undefined,deterministicStepMs:16,deterministicSeed:42});await call('frame',{op:'step',frames:90});
for(const bike of [0])for(let lane=0;lane<2;lane++){
await call('input',{op:'set',ports:[{}]});await cmd('bike_pick '+bike);await cmd('track 21');await call('frame',{op:'step',frames:2});await cmd('place 440 '+lane);await call('input',{op:'set',ports:[{south:true}]});let f,prev;
for(let i=0;i<250;i++){await call('frame',{op:'step',frames:6});f=await cmd('fields');if(f.crashes||f['bike.pos'][0]>535)break;prev={pos:f['bike.pos'],speed:f['bike.speed'],loops:f.loops,boost:f['boost.ticks'],lane:f['race.lanes']};}
for(const k of ['cw.gpu.errors','cw.three.missing','render.skipped'])assert.equal(f[k],0,k);assert.equal(f.crashes,0,'the folding bike clears both loops');assert.equal(f.loops,2);assert.equal(f['race.lanes'][0],lane,'returns to the entry lane');console.log(JSON.stringify({bike,lane,pos:f['bike.pos'],crashes:f.crashes,loops:f.loops,prev}));}
