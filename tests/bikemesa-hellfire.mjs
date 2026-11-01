// Real cart: unicycle course completion, timed fire collisions, lanes and recovery.
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdir,writeFile} from 'node:fs/promises';
const cart=resolve(process.argv[2]),backend=process.argv[3]||'webgpu',out=resolve('out/hellfire',backend);await mkdir(out,{recursive:true});
async function call(t,a){const r=await fetch(`${process.env.ROMDEV_URL||'http://localhost:7331'}/tool/${t}`,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':`bikemesa-hellfire-${backend}`},body:JSON.stringify(a)});const j=await r.json();assert(r.ok&&!j.error,JSON.stringify(j));return j;}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply,step=frames=>call('frame',{op:'step',frames}),hold=b=>call('input',{op:'set',ports:[b]}),shot=n=>call('frame',{op:'screenshot',path:resolve(out,n+'.png')});
function clean(f){for(const k of ['cw.gpu.errors','cw.three.missing','render.skipped'])assert.equal(f[k],0,k);}
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});await step(90);await cmd('bike_pick 7');await cmd('track 20');await hold({south:true});
let f,devil=false;const samples=[];
for(let n=0;n<700;n++){await step(12);f=await cmd('fields');clean(f);assert.equal(f.crashes,0,'unicycle clears Hellfire');samples.push({x:f['bike.pos'],air:f['air.max']});if(n===25)await shot('lava-cliffs');if(f['bike.pos'][0]>618&&!devil){devil=true;await shot('friendly-host');}if(f.finished)break;}
assert(f.finished,'unicycle completes Hellfire');assert(f['ride.ticks']>=2400,'course takes at least forty seconds');assert(devil);assert.equal(f['bike.model'],7);await shot('finish');
// Stationary in the real eruption lane: immunity expires before an eruption.
await hold({});await cmd('track 20');await cmd('hellfire 1');let burned=false;
for(let n=0;n<60;n++){await step(6);f=await cmd('fields');clean(f);if(f['hellfire.immunity']>0)assert.equal(f['hellfire.hits'],0,'spawn fire protection');if(f['hellfire.burning']>0){burned=true;assert(f.crashes>0,'fire causes a scored wipeout');await step(18);await shot('on-fire');break;}}
assert(burned,'real fireball ignites rider');
await cmd('hellfire 1');const hits=f['hellfire.hits'];await step(90);f=await cmd('fields');assert.equal(f['hellfire.hits'],hits,'respawn cannot immediately burn again');assert.equal(f['hellfire.burning'],0);
// Neighboring lane is safe for longer than a complete eruption cycle.
await cmd('track 20');await cmd('hellfire 1 3');await step(360);f=await cmd('fields');assert.equal(f['hellfire.hits'],0,'safe neighboring lane');clean(f);
// Wave is visibly animated and retains recognizable face, cape, staff and horns.
await cmd('pause 0');await cmd('inspect 625 -10 3 624 -14 -10');await step(1);await shot('host-wave-a');await step(35);await shot('host-wave-b');await cmd('inspect off');
await writeFile(resolve(out,'report.json'),JSON.stringify({cart,backend,checks:['unicycle finishes Hellfire','render clean','fireball ignites rider and causes wipeout','respawn protection','safe adjacent lane'],samples},null,2));console.log(JSON.stringify({backend,passed:true}));
