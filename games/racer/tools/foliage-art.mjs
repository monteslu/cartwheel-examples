// games/racer/tools/foliage-art.mjs: the Dam's trees and grass as glTF for TerrainScatter3D.
//
//   node games/racer/tools/foliage-art.mjs <trees dir> <grass dir> [<more trees dir>]
//
// <trees dir> holds the EZ-Tree conifers as glTF, one folder per tree (eztree-<name>/eztree-<name>.glb: EZ-Tree,
// MIT, program output; bark and needle textures CC0 from ambientCG and Poly Haven; each folder's LICENSE.txt says
// which). <more trees dir>: trees generated the same way for this game (the jungle's: eztree-canopy and
// eztree-broadleaf, configs in its tools/eztree/configs). <grass dir> holds Yughues' grass cards (grass_04_rgba.png, grass_05_rgba.png: CC0, OpenGameArt).
//
// Trees: copied to assets/models/trees/<name>.glb with every leaf texture's fully transparent texels filled with
// the average leaf colour, so its smaller mip levels do not darken toward black (the needles went dark at a
// distance otherwise). Grass: assets/models/grass/<name>.glb, three cards crossed at 60 degrees, 1 m tall and wide,
// rooted at y = 0, alpha tested (MASK) and two sided. Licences go to licenses/ beside each.
//
// ImageMagick's convert does the pixel work. Writes only under games/racer/assets/models/.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const [treesDir, grassDir, moreDir] = process.argv.slice(2);
if (!treesDir || !grassDir) {
  console.error('usage: node foliage-art.mjs <trees dir> <grass dir>');
  process.exit(2);
}
const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'models');
const TMP = path.join(ROOT, '.foliage-tmp');
fs.mkdirSync(TMP, { recursive: true });

// ── glb read and write ──────────────────────────────────────────────────────────
function readGlb(file) {
  const b = fs.readFileSync(file);
  if (b.readUInt32LE(0) !== 0x46546c67) throw new Error(`${file}: not a glb`);
  const jl = b.readUInt32LE(12);
  const json = JSON.parse(b.subarray(20, 20 + jl).toString('utf8'));
  const bin = b.subarray(20 + jl + 8, 20 + jl + 8 + b.readUInt32LE(20 + jl));
  return { json, bin };
}
const pad4 = (n) => (n + 3) & ~3;
function writeGlb(file, json, bin) {
  const js = Buffer.from(JSON.stringify(json), 'utf8');
  const jsp = Buffer.concat([js, Buffer.alloc(pad4(js.length) - js.length, 0x20)]);
  const bp = Buffer.concat([bin, Buffer.alloc(pad4(bin.length) - bin.length, 0)]);
  const head = Buffer.alloc(12);
  head.writeUInt32LE(0x46546c67, 0), head.writeUInt32LE(2, 4), head.writeUInt32LE(12 + 8 + jsp.length + 8 + bp.length, 8);
  const c0 = Buffer.alloc(8), c1 = Buffer.alloc(8);
  c0.writeUInt32LE(jsp.length, 0), c0.writeUInt32LE(0x4e4f534a, 4);
  c1.writeUInt32LE(bp.length, 0), c1.writeUInt32LE(0x004e4942, 4);
  fs.writeFileSync(file, Buffer.concat([head, c0, jsp, c1, bp]));
}
// rebuild the binary chunk from per-bufferView buffers (some replaced)
function repack(json, bin, replaced) {
  const parts = [];
  let off = 0;
  json.bufferViews.forEach((bv, i) => {
    const data = replaced.get(i) ?? bin.subarray(bv.byteOffset ?? 0, (bv.byteOffset ?? 0) + bv.byteLength);
    const start = pad4(off);
    if (start > off) parts.push(Buffer.alloc(start - off));
    parts.push(data);
    bv.byteOffset = start, bv.byteLength = data.length;
    off = start + data.length;
  });
  const out = Buffer.concat(parts);
  json.buffers = [{ byteLength: out.length }];
  return out;
}

// fill a PNG's fully transparent texels with its average opaque colour (alpha kept)
function fillTransparent(png) {
  const src = path.join(TMP, 'in.png'), dst = path.join(TMP, 'out.png');
  fs.writeFileSync(src, png);
  const avg = execFileSync('convert', [src, '-channel', 'A', '-threshold', '50%', '+channel', '-scale', '1x1!', '-alpha', 'off', '-format', '#%[hex:u]', 'info:']).toString().trim();
  execFileSync('convert', [src, '-background', avg, '-alpha', 'background', 'PNG32:' + dst]);
  return fs.readFileSync(dst);
}

// ── trees ────────────────────────────────────────────────────────────────────────
const TREES = ['ponderosa', 'ponderosa-2', 'pinyon', 'juniper', 'scruboak', 'snag', 'fir', ...(moreDir ? ['canopy', 'broadleaf'] : [])];
fs.mkdirSync(path.join(ROOT, 'trees', 'licenses'), { recursive: true });
for (const name of TREES) {
  const dir = fs.existsSync(path.join(treesDir, `eztree-${name}`)) ? path.join(treesDir, `eztree-${name}`) : path.join(moreDir, `eztree-${name}`);
  const { json, bin } = readGlb(path.join(dir, `eztree-${name}.glb`));
  const replaced = new Map();
  // the images a MASK material's base colour uses
  const leafImages = new Set();
  for (const m of json.materials ?? []) {
    if (m.alphaMode !== 'MASK') continue;
    const t = m.pbrMetallicRoughness?.baseColorTexture;
    if (t) leafImages.add(json.textures[t.index].source);
  }
  for (const ii of leafImages) {
    const img = json.images[ii];
    if (img.mimeType !== 'image/png' || img.bufferView === undefined) continue;
    const bv = json.bufferViews[img.bufferView];
    replaced.set(img.bufferView, fillTransparent(bin.subarray(bv.byteOffset ?? 0, (bv.byteOffset ?? 0) + bv.byteLength)));
  }
  writeGlb(path.join(ROOT, 'trees', `${name}.glb`), json, repack(json, bin, replaced));
  fs.copyFileSync(path.join(dir, 'LICENSE.txt'), path.join(ROOT, 'trees', 'licenses', `${name}.txt`));
  console.log(`tree ${name}: ${leafImages.size} leaf texture(s) filled`);
}

// ── grass cards ──────────────────────────────────────────────────────────────────
// name, source, ImageMagick grade
const GRASS = [
  ['green', 'grass_04_rgba.png', ['-modulate', '125,135,100']],
  ['tall', 'grass_05_rgba.png', ['-modulate', '125,120,100']],
  ['dry', 'grass_04_rgba.png', ['-channel', 'RGB', '-colorspace', 'gray', '-level', '0,55%', '-fill', '#d2b072', '-tint', '100', '+channel']],
];
function cardGlb(png) {
  // three vertical quads through the y axis at 0, 60 and 120 degrees: 12 vertices, 6 triangles
  const P = [], N = [], T = [], I = [];
  for (let k = 0; k < 3; k++) {
    const a = (k * Math.PI) / 3, c = Math.cos(a) * 0.5, s = Math.sin(a) * 0.5;
    const b = P.length / 3;
    P.push(-c, 0, -s, c, 0, s, c, 1, s, -c, 1, -s);
    // normals straight up (the usual grass trick): lit like the ground under them, never dark side-on to the sun
    for (let v = 0; v < 4; v++) N.push(0, 1, 0);
    T.push(0, 1, 1, 1, 1, 0, 0, 0);
    I.push(b, b + 1, b + 2, b, b + 2, b + 3);
  }
  const f32 = (a) => Buffer.from(new Float32Array(a).buffer), u16 = (a) => Buffer.from(new Uint16Array(a).buffer);
  const views = [f32(P), f32(N), f32(T), u16(I), png];
  const json = {
    asset: { version: '2.0', generator: 'racer foliage-art.mjs' },
    scene: 0, scenes: [{ nodes: [0] }], nodes: [{ mesh: 0, name: 'grass' }],
    meshes: [{ name: 'grass', primitives: [{ attributes: { POSITION: 0, NORMAL: 1, TEXCOORD_0: 2 }, indices: 3, material: 0 }] }],
    materials: [{ name: 'grass', alphaMode: 'MASK', alphaCutoff: 0.5, doubleSided: true,
      pbrMetallicRoughness: { baseColorTexture: { index: 0 }, metallicFactor: 0, roughnessFactor: 0.85 } }],
    textures: [{ source: 0, sampler: 0 }], samplers: [{ magFilter: 9729, minFilter: 9987, wrapS: 33071, wrapT: 33071 }],
    images: [{ bufferView: 4, mimeType: 'image/png' }],
    accessors: [
      { bufferView: 0, componentType: 5126, count: 12, type: 'VEC3', min: [-0.5, 0, -0.5], max: [0.5, 1, 0.5] },
      { bufferView: 1, componentType: 5126, count: 12, type: 'VEC3' },
      { bufferView: 2, componentType: 5126, count: 12, type: 'VEC2' },
      { bufferView: 3, componentType: 5123, count: 18, type: 'SCALAR' },
    ],
    bufferViews: views.map(() => ({ buffer: 0, byteOffset: 0, byteLength: 0 })),
  };
  json.bufferViews[0].target = json.bufferViews[1].target = json.bufferViews[2].target = 34962;
  json.bufferViews[3].target = 34963;
  return { json, bin: repack(json, Buffer.alloc(0), new Map(views.map((v, i) => [i, v]))) };
}
fs.mkdirSync(path.join(ROOT, 'grass', 'licenses'), { recursive: true });
for (const [name, src, grade] of GRASS) {
  const graded = path.join(TMP, 'graded.png');
  execFileSync('convert', [path.join(grassDir, src), ...grade, 'PNG32:' + graded]);
  const png = fillTransparent(fs.readFileSync(graded));
  const { json, bin } = cardGlb(png);
  writeGlb(path.join(ROOT, 'grass', `${name}.glb`), json, bin);
  console.log(`grass ${name}: ${src}`);
}
fs.copyFileSync(path.join(grassDir, 'LICENSE.txt'), path.join(ROOT, 'grass', 'licenses', 'yughues-grass.txt'));
fs.rmSync(TMP, { recursive: true, force: true });
