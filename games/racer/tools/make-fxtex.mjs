// racer: the decal sheet for effects (assets/fx/decals.png), generated here so nothing is borrowed.
//   region 0 0 256 256   a shockwave ring: bright thin edge, soft glow inside, nothing outside
//   region 256 0 128 256 a tyre mark: a stripe with grain, fading at both ends so marks chain without seams
// Run: node games/racer/tools/make-fxtex.mjs
import { deflateSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const W = 512, H = 256;
const px = new Uint8Array(W * H * 4);
const hash = (x, y, s) => {
  let h = (x * 374761393 + y * 668265263 + s * 2147483647) >>> 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
};
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const put = (x, y, r, g, b, a) => {
  const i = (y * W + x) * 4;
  px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = Math.round(Math.min(1, Math.max(0, a)) * 255);
};
// the ring
for (let y = 0; y < 256; y++) for (let x = 0; x < 256; x++) {
  const dx = (x + 0.5 - 128) / 128, dy = (y + 0.5 - 128) / 128;
  const r = Math.hypot(dx, dy);
  const edge = Math.exp(-Math.pow((r - 0.86) / 0.05, 2));        // the bright front
  const inner = smooth(0.2, 0.86, r) * 0.18 * (1 - smooth(0.86, 0.9, r)); // a dim wash behind it
  const wob = 0.85 + 0.15 * hash(Math.floor(Math.atan2(dy, dx) * 40), 0, 3); // a little ragged
  put(x, y, 255, 255, 255, (edge * wob + inner) * (1 - smooth(0.93, 1.0, r)));
}
// the tyre mark
for (let y = 0; y < 256; y++) for (let x = 0; x < 128; x++) {
  const u = (x + 0.5) / 128 * 2 - 1, v = (y + 0.5) / 256;
  const across = 1 - smooth(0.55, 0.95, Math.abs(u));
  const along = smooth(0, 0.14, v) * (1 - smooth(0.86, 1, v));
  const grain = 0.65 + 0.35 * hash(x >> 1, y >> 1, 7);
  const tread = 0.85 + 0.15 * Math.sin(y * 0.9);
  put(256 + x, y, 255, 255, 255, across * along * grain * tread);
}
// PNG
const crcT = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = (b) => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (t, d) => {
  const b = Buffer.alloc(8 + d.length + 4);
  b.writeUInt32BE(d.length, 0); b.write(t, 4, 'ascii'); Buffer.from(d).copy(b, 8);
  b.writeUInt32BE(crc(b.subarray(4, 8 + d.length)), 8 + d.length);
  return b;
};
const raw = Buffer.alloc((W * 4 + 1) * H);
for (let y = 0; y < H; y++) { raw[y * (W * 4 + 1)] = 0; Buffer.from(px.subarray(y * W * 4, (y + 1) * W * 4)).copy(raw, y * (W * 4 + 1) + 1); }
const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 6;
const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
const out = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'fx');
mkdirSync(out, { recursive: true });
writeFileSync(join(out, 'decals.png'), png);
console.log('wrote', join(out, 'decals.png'), png.length, 'bytes');
