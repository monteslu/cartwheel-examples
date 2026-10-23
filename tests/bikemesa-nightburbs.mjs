// Ride the whole course and verify the playable BMX/alien moon crossing.
// ROMDEV_URL=http://localhost:7341 node tests/bikemesa-nightburbs.mjs cart.wasc webgpu
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
import {mkdir,writeFile} from 'node:fs/promises';
const backend=process.argv[3]||'webgpu',cart=resolve(process.argv[2]),out=resolve('out/nightburbs',backend);await mkdir(out,{recursive:true});
async function call(tool,args){const r=await fetch(`${process.env.ROMDEV_URL||'http://localhost:7331'}/tool/${tool}`,{method:'POST',headers:{'Content-Type':'application/json','x-romdev-session':`bikemesa-nightburbs-${backend}`},body:JSON.stringify(args)});const j=await r.json();assert(r.ok&&!j.error,JSON.stringify(j));return j;}
const cmd=async text=>(await call('wasm',{op:'command',text})).reply,step=frames=>call('frame',{op:'step',frames}),shot=name=>call('frame',{op:'screenshot',path:resolve(out,name+'.png')});
const clean=f=>{for(const k of ['cw.gpu.errors','cw.three.missing','render.skipped'])assert.equal(f[k],0,k);};
const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0),norm=a=>a.map(x=>x/Math.sqrt(dot(a,a)));
function moonOverlap(f){
 const [cx,cy,d]=f['cam.pos'],u=Math.max(0,Math.min(1,(d-13)/5)),tilt=.12-.195*u*u*(3-2*u),eye=[cx-.16*d,cy+tilt*d,d],forward=norm([.16,-tilt,-1]),right=norm([1,0,.16]),up=[right[1]*forward[2]-right[2]*forward[1],right[2]*forward[0]-right[0]*forward[2],right[0]*forward[1]-right[1]*forward[0]];
 const project=p=>{const a=p.map((x,i)=>x-eye[i]),z=dot(a,forward);return[dot(a,right)/z,dot(a,up)/z,z];};
 const moon=project([500,440,-5000]),rider=project([f['bike.pos'][0],f['bike.pos'][1]+.8,f['race.z'][0]]);
 if(f['game.state']===3){assert(Math.abs(moon[1])+800/moon[2]<Math.tan(23*Math.PI/180),'full moon remains vertically in frame');assert(Math.abs(moon[0])+800/moon[2]<Math.tan(23*Math.PI/180)*16/9,'full moon remains horizontally in frame');}
 return Math.hypot(moon[0]-rider[0],moon[1]-rider[1])<800/moon[2]*.85;
}
await call('loadMedia',{platform:'wasmcart',path:cart,deterministicStepMs:16,deterministicSeed:42});await step(90);await cmd('bike_pick 5');await cmd('track 19');await call('input',{op:'set',ports:[{south:true}]});
let f,crossed=false;const samples=[];
for(let i=0;i<230;i++){
 await step(12);f=await cmd('fields');clean(f);assert.equal(f.crashes,0,'BMX clears all three jumps');
 samples.push({pos:f['bike.pos'],crashes:f.crashes,alien:f['alien.gone'],air:f['air.max']});
 const overlap=moonOverlap(f);const [x,y]=f['bike.pos'];if(x>65&&x<95&&y>6&&!f['alien.gone']&&overlap&&!crossed){crossed=true;await shot('moon-crossing');}
 if(i===30)await shot('neighborhood');if(f.finished)break;
}
assert(f.finished,'complete course');assert(crossed,'BMX rider and retained alien cross the moon during normal play');assert(f['air.max']>=150,'at least 2.5 seconds airborne');await shot('finish');
await call('input',{op:'set',ports:[{}]});await cmd('track 19 2');await step(2);f=await cmd('fields');assert.equal(f.checkpoint,2);assert.equal(f['alien.gone'],0);clean(f);
await writeFile(resolve(out,'report.json'),JSON.stringify({cart,backend,checks:['three jumps without crash','BMX and alien moon crossing','long air time','moon stays in frame throughout the ride','checkpoint reload'],samples},null,2));console.log(JSON.stringify({backend,crossed,finished:true}));
