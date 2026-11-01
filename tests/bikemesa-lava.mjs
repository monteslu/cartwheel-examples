import assert from 'node:assert/strict';import {resolve} from 'node:path';import {mkdir,writeFile} from 'node:fs/promises';
const cart=resolve(process.argv[2]),be=process.argv[3]||'webgpu',out=resolve('out/lava-contact',be);await mkdir(out,{recursive:true});
async function call(t,a){const r=await fetch((process.env.ROMDEV_URL||'http://localhost:7331')+'/tool/'+t,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':'bikemesa-lava-contact-'+be},body:JSON.stringify(a)});const j=await r.json();assert(!j.error,JSON.stringify(j));return j;}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply,step=frames=>call('frame',{op:'step',frames}),shot=n=>call('frame',{op:'screenshot',path:resolve(out,n+'.png')});
const clean=f=>{for(const k of ['cw.gpu.errors','cw.three.missing','render.skipped'])assert.equal(f[k],0,k);};
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});await step(90);await cmd('bike_pick 7');await cmd('track 20');await cmd('hellfire pit 1');let f;
for(let i=0;i<30;i++){await step(6);f=await cmd('fields');clean(f);if(f['hellfire.lava_hits']>0)break;}
assert(f['hellfire.lava_hits']>0,'falling into the actual pool ignites rider');assert(f['hellfire.burning']>0);assert(f.crashes>0);await step(12);await shot('burning-in-pit');f=await cmd('fields');assert.equal(f['hellfire.scorched'][0],1);clean(f);
await cmd('hellfire 1 3');await step(15);f=await cmd('fields');assert.equal(f['hellfire.burning'],0);assert.equal(f['hellfire.scorched'][0],1,'charcoal remains after respawn');const [x,y]=f['bike.pos'];await cmd(`inspect ${x+2} ${y+1.5} 3 ${x} ${y+.8} 0`);await step(1);await shot('scorched-after-respawn');clean(await cmd('fields'));
await cmd('inspect off');await cmd('track 1');await step(10);f=await cmd('fields');assert.equal(f['hellfire.scorched'][0],0,'next level clears charcoal');clean(f);
await writeFile(resolve(out,'report.json'),JSON.stringify({cart,be,passed:true,checks:['actual lava contact causes burning wipeout','charcoal survives respawn','next level clears charcoal','render clean']},null,2));console.log(JSON.stringify({be,passed:true}));
