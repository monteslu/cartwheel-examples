// WASMCART_MODULE=/path/to/wasmcart/index.js node tests/bikemesa-save-cart.mjs cart.wasc
const {CartHost}=await import(process.env.WASMCART_MODULE||'wasmcart');
import assert from 'node:assert/strict';
const sum=b=>{let h=2166136261;for(const x of b.subarray(12))h=Math.imul(h^x,16777619)>>>0;return(h^(b.readUInt16LE(4)|b.length<<16))>>>0;};
for(const version of [8,9,10]){
 const size={8:208,9:220,10:232}[version],b=Buffer.alloc(size);b.writeUInt32LE(0x534d4b42);b.writeUInt16LE(version,4);b.writeUInt16LE(size,6);b.writeInt32LE(1234,12);b.writeInt32LE(9000,16);b[20]=3;b[21]=1;b[23]=3;b.writeUInt32LE(2,size-4);if(version===10){b[12+17*12+9]=1;b.writeInt32LE(4567,12+17*12);}b.writeUInt32LE(sum(b),8);
 const host=new CartHost();await host.load(process.argv[2],{saveData:b});host.setFixedStep(16);for(let i=0;i<90;i++)host.runFrame([]);const out=Buffer.from(host.getSaveData());assert.equal(out.length,244);assert.equal(out.readUInt16LE(4),11);assert.equal(out.readInt32LE(12),1234);assert.equal(out.readInt32LE(16),9000);assert.equal(out.readUInt32LE(240),2);assert.deepEqual(out.subarray(228,240),Buffer.alloc(12));assert.equal(out.readUInt32LE(8),sum(out));if(version===10){assert.equal(out[225],1);assert.equal(out.readInt32LE(216),4567);}host.destroy();console.log('PASS save v'+version);
}
