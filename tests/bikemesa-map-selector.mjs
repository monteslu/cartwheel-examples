// The map selector scrolls once there are more than twenty tracks: a four-row window with arrows.
// node tests/bikemesa-map-selector.mjs build/cart-wgpu/carts/bikemesa.wasc   (BIKEMESA_GPU=gl for the GL path)
import assert from 'node:assert/strict';import {resolve} from 'node:path';import {mkdir} from 'node:fs/promises';
const out=resolve('out/map-selector');await mkdir(out,{recursive:true});
async function call(t,a){const r=await fetch((process.env.ROMDEV_URL||'http://localhost:7331')+'/tool/'+t,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':'bikemesa-selector-scroll'},body:JSON.stringify(a),signal:AbortSignal.timeout(90000)});let j=await r.json();assert(!j.error,JSON.stringify(j));return j;}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply,step=frames=>call('frame',{op:'step',frames}),hold=b=>call('input',{op:'set',ports:[b]});
const tap=async b=>{await hold(b);await step(2);await hold({});await step(12);};
await call('loadMedia',{platform:'wasmcart',wgpu:process.env.BIKEMESA_GPU==='gl'?false:undefined,path:resolve(process.argv[2]),deterministicStepMs:16,deterministicSeed:42});await step(90);
await cmd('track 1');await step(210);await tap({start:true});for(let i=0;i<4;i++)await tap({down:true});await tap({south:true});
let f=await cmd('fields');assert.equal(f['game.state'],1,'pause menu opens map selector');assert.equal(f['track.count'],21);
await call('frame',{op:'screenshot',path:resolve(out,'more-maps-below.png')});
for(let i=0;i<4;i++)await tap({right:true});for(let i=0;i<4;i++)await tap({down:true});f=await cmd('fields');
assert.equal(f['select.cursor'],20,'down from the fifth column reaches the final row');await call('frame',{op:'screenshot',path:resolve(out,'last-row.png')});
await tap({left:true});assert.equal((await cmd('fields'))['select.cursor'],20,'the last map sits in the first column');
await tap({up:true});assert.equal((await cmd('fields'))['select.cursor'],15);
for(const k of ['cw.gpu.errors','cw.three.missing','render.skipped'])assert.equal((await cmd('fields'))[k],0,k);
console.log('PASS selector down, final row, left and up');
