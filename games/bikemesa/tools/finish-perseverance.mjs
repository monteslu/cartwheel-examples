// Bake a parked rover pose, convert WebP, and join compatible primitives.
// ROVER_NODE_PACKAGE may point to the package.json containing glTF Transform/sharp.
import {createRequire} from 'node:module';
import {resolve} from 'node:path';
const require=createRequire(resolve(process.env.ROVER_NODE_PACKAGE || 'package.json'));
const {NodeIO}=await import(require.resolve('@gltf-transform/core'));
const {ALL_EXTENSIONS}=await import(require.resolve('@gltf-transform/extensions'));
const {flatten,join,prune,dedup}=await import(require.resolve('@gltf-transform/functions'));
const {default:sharp}=await import(require.resolve('sharp'));
const io=new NodeIO().registerExtensions(ALL_EXTENSIONS);
const d=await io.read(process.argv[2]);
for(const a of d.getRoot().listAnimations()){for(const c of a.listChannels())c.dispose();for(const s of a.listSamplers())s.dispose();a.dispose();}
// Bake the source pose before removing its skeletal hierarchy.
const mul=(a,b)=>Array.from({length:16},(_,i)=>{let v=0;for(let k=0;k<4;k++)v+=a[(i%4)+4*k]*b[k+4*Math.floor(i/4)];return v;});
function inv(m){
 const rows=Array.from({length:4},(_,r)=>Array.from({length:8},(_,c)=>c<4?m[r+c*4]:+(c-4===r)));
 for(let c=0;c<4;c++){
  let p=c;for(let r=c+1;r<4;r++)if(Math.abs(rows[r][c])>Math.abs(rows[p][c]))p=r;
  if(Math.abs(rows[p][c])<1e-12)throw Error('singular skin matrix');
  [rows[c],rows[p]]=[rows[p],rows[c]];
  const v=rows[c][c];rows[c]=rows[c].map(x=>x/v);
  for(let r=0;r<4;r++)if(r!==c){const f=rows[r][c];rows[r]=rows[r].map((x,k)=>x-f*rows[c][k]);}
 }
 return Array.from({length:16},(_,i)=>rows[i%4][4+Math.floor(i/4)]);
}
for(const n of d.getRoot().listNodes())if(n.getSkin()){
 const skin=n.getSkin(),ib=skin.getInverseBindMatrices(),inverse=inv(n.getWorldMatrix());
 const mats=skin.listJoints().map((j,k)=>mul(mul(inverse,j.getWorldMatrix()),ib.getElement(k,[])));
 for(const p of n.getMesh().listPrimitives()){
  const pos=p.getAttribute('POSITION'),norm=p.getAttribute('NORMAL'),j=p.getAttribute('JOINTS_0'),w=p.getAttribute('WEIGHTS_0');
  if(!j||!w)throw Error('missing skin attributes');
  for(let v=0;v<pos.getCount();v++){
   const js=j.getElement(v,[]),ws=w.getElement(v,[]),src=pos.getElement(v,[]),ns=norm?.getElement(v,[]);
   const m=Array(16).fill(0);for(let k=0;k<4;k++)for(let a=0;a<16;a++)m[a]+=mats[js[k]][a]*ws[k];
   pos.setElement(v,[0,1,2].map(r=>m[r]*src[0]+m[r+4]*src[1]+m[r+8]*src[2]+m[r+12]));
   if(ns){const im=inv(m);const nv=[0,1,2].map(r=>im[r*4]*ns[0]+im[r*4+1]*ns[1]+im[r*4+2]*ns[2]);const len=Math.hypot(...nv);norm.setElement(v,nv.map(x=>x/len));}
  }
  p.setAttribute('JOINTS_0',null).setAttribute('WEIGHTS_0',null);
 }
 n.setSkin(null);
}
for(const skin of d.getRoot().listSkins())skin.dispose();
for(const t of d.getRoot().listTextures())if(t.getMimeType()==='image/webp')t.setImage(await sharp(t.getImage()).png().toBuffer()).setMimeType('image/png');
for(const e of d.getRoot().listExtensionsUsed())if(e.extensionName==='EXT_texture_webp')e.dispose();
await d.transform(flatten(),dedup(),join({keepNamed:false}),prune());
await io.write(process.argv[3],d);
const p=d.getRoot().listMeshes().flatMap(m=>m.listPrimitives());console.log({meshes:d.getRoot().listMeshes().length,parts:p.length,triangles:p.reduce((n,p)=>n+p.getIndices().getCount()/3,0)});
