// Writes derby's decal sheets. assets/decals/scorch.png: four burn scars
// in a 2 x 2 sheet of 256 px cells, drawn as MULTIPLY decals on the dirt
// under a wreck. Each is a charred core with a torn, irregular edge, soot
// streaks blown out from it, a browned ring where the heat cooked the dirt
// and unburnt clods left in it. RGB is what the dirt is multiplied by (sRGB),
// alpha how much. assets/decals/body.png: what a hit and a fire leave on a
// car's panels, also 2 x 2 cells of 256 px: two scrapes (top row: gouges
// through the paint to bare steel and primer, smeared rubber; drawn MIX, lit
// like the paint) and two soot blooms (bottom row: drawn MULTIPLY, scorched
// paint round a fire).
//
//   node games/derby/tools/make-decals.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';

const D = join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'decals');
mkdirSync(D, { recursive: true });

const N = 512, C = 256;
const px = new Uint8Array(N * N * 4);

// integer hash noise: the same bytes everywhere
const hash = (x, y, s) => {
  let h = (Math.imul(x, 374761393) + Math.imul(y, 668265263) + Math.imul(s, 2147483647)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
};
const smooth = (t) => t * t * (3 - 2 * t);
const vnoise = (x, y, s) => {
  const xi = Math.floor(x), yi = Math.floor(y), fx = smooth(x - xi), fy = smooth(y - yi);
  const a = hash(xi, yi, s), b = hash(xi + 1, yi, s), c = hash(xi, yi + 1, s), d = hash(xi + 1, yi + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
};
const fbm = (x, y, s, oct = 5) => {
  let v = 0, amp = 0.5, f = 1, tot = 0;
  for (let o = 0; o < oct; o++) v += amp * vnoise(x * f, y * f, s + o * 31), tot += amp, amp *= 0.5, f *= 2.03;
  return v / tot;
};
// noise round a circle (no seam at the wrap): sample on a ring
const ring = (ang, freq, s, oct = 4) => fbm(Math.cos(ang) * freq + 50, Math.sin(ang) * freq + 50, s, oct);
const clamp = (x) => Math.max(0, Math.min(1, x));
const mix = (a, b, t) => a + (b - a) * t;

for (let cell = 0; cell < 4; cell++) {
  const ox = (cell & 1) * C, oy = (cell >> 1) * C, seed = 11 + cell * 97;
  const elong = 1 + 0.25 * hash(cell, 1, 5); // a burn spreads along the car
  for (let y = 0; y < C; y++)
    for (let x = 0; x < C; x++) {
      const u = (x + 0.5) / C * 2 - 1, v = ((y + 0.5) / C * 2 - 1) * elong;
      const r = Math.hypot(u, v), ang = Math.atan2(v, u);
      // the charred core's torn edge
      const edge = 0.42 + 0.2 * (ring(ang, 1.6, seed) - 0.5) * 2 + 0.08 * (ring(ang, 5, seed + 1) - 0.5) * 2;
      // soot streaks blown outward: noise in angle, stretched along r
      const streak = Math.pow(ring(ang, 4.5, seed + 2, 4), 1.6) * 1.4 * (0.6 + 0.4 * fbm(u * 3, v * 3, seed + 5, 3));
      const reach = edge + 0.1 + 0.3 * streak;
      const grain = fbm(u * 9, v * 9, seed + 3, 4);
      const fine = fbm(u * 28, v * 28, seed + 4, 3);
      // 1 in the core, 0 past the soot's reach
      const core = clamp((edge - r) / 0.06 + 0.5);
      const soot = Math.pow(clamp((reach - r) / Math.max(0.05, reach - edge)), 1.5) * (0.45 + 0.55 * grain);
      const cook = clamp(1 - Math.abs(r - edge - 0.05) / 0.14) * 0.6; // the browned ring
      let a = Math.max(core * (0.86 + 0.14 * grain), soot * 0.7, cook * 0.5);
      // clods and bare patches that did not burn
      if (fine > 0.8) a *= 0.5;
      if (hash(x, y, seed + 9) > 0.985) a *= 0.2;
      a *= clamp((0.98 - r) / 0.08); // never to the cell's edge
      // colour: black char in the core, cooked umber at the ring
      const t = clamp((r - edge * 0.6) / (edge * 0.6));
      const ch = mix(0.05, 0.11, grain) * (0.85 + 0.3 * fine);
      const R = mix(ch, 0.34 + 0.1 * grain, t * (1 - core * 0.6));
      const G = mix(ch * 0.92, 0.25 + 0.07 * grain, t * (1 - core * 0.6));
      const B = mix(ch * 0.85, 0.17 + 0.05 * grain, t * (1 - core * 0.6));
      const i = ((oy + y) * N + ox + x) * 4;
      px[i] = Math.round(clamp(R) * 255);
      px[i + 1] = Math.round(clamp(G) * 255);
      px[i + 2] = Math.round(clamp(B) * 255);
      px[i + 3] = Math.round(clamp(a) * 255);
    }
}

// PNG: RGBA8, filter 0 per row
const crcT = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
const crc = (b) => {
  let c = 0xffffffff;
  for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
};
const chunk = (type, data) => {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
};
const writePng = (name, px) => {
const raw = Buffer.alloc(N * (N * 4 + 1));
for (let y = 0; y < N; y++) Buffer.from(px.buffer, y * N * 4, N * 4).copy(raw, y * (N * 4 + 1) + 1);
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(N, 0);
ihdr.writeUInt32BE(N, 4);
ihdr[8] = 8, ihdr[9] = 6;
const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
  chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
writeFileSync(join(D, name), png);
console.log('wrote', join(D, name), png.length, 'bytes');
};
writePng('scorch.png', px);

// ── body.png ────────────────────────────────────────────────────────
const bp = new Uint8Array(N * N * 4);
const put = (ox, oy, x, y, R, G, B, A) => {
  const i = ((oy + y) * N + ox + x) * 4;
  bp[i] = Math.round(clamp(R) * 255), bp[i + 1] = Math.round(clamp(G) * 255);
  bp[i + 2] = Math.round(clamp(B) * 255), bp[i + 3] = Math.round(clamp(A) * 255);
};
// scrapes: gouges along the cell's x, the direction of the slide
for (let cell = 0; cell < 2; cell++) {
  const ox = cell * C, seed = 301 + cell * 53;
  for (let y = 0; y < C; y++)
    for (let x = 0; x < C; x++) {
      const u = (x + 0.5) / C * 2 - 1, v = (y + 0.5) / C * 2 - 1;
      // the scraped band: wide in the middle, tapering to the ends, a torn edge
      const half = 0.42 * (1 - Math.pow(Math.abs(u), 2.2)) * (0.75 + 0.5 * fbm(u * 2.5, 3, seed, 3));
      const band = clamp((half - Math.abs(v)) / 0.06 + 0.5);
      // gouges: thin lines along the slide, each its own depth
      const line = fbm(v * 70, u * 0.25, seed + 1, 2);
      const gouge = clamp((line - 0.5) / 0.07);
      const grain = fbm(u * 14, v * 14, seed + 2, 4);
      // through the paint: primer where shallow, bright steel in the gouges
      const steel = 0.62 + 0.18 * grain, primer = 0.36 + 0.08 * grain;
      const t = clamp(gouge * 1.2);
      let R = mix(primer * 1.05, steel, t), G = mix(primer, steel, t), B = mix(primer * 0.92, steel * 1.02, t);
      // black rubber smeared off the other car, in streaks
      const rub = clamp((fbm(u * 2, v * 12, seed + 3, 4) - 0.64) / 0.12) * 0.7;
      R = mix(R, 0.05, rub), G = mix(G, 0.05, rub), B = mix(B, 0.05, rub);
      let a = band * (0.55 + 0.45 * Math.max(t, rub)) * (0.7 + 0.3 * grain);
      if (hash(x, y, seed + 9) > 0.97) a *= 0.3;
      a *= clamp((0.97 - Math.abs(u)) / 0.1) * clamp((0.97 - Math.abs(v)) / 0.1);
      put(ox, 0, x, y, R, G, B, a);
    }
}
// soot: a scorched bloom, darkest at its heart, licked upward (the cell's -y)
for (let cell = 0; cell < 2; cell++) {
  const ox = cell * C, seed = 509 + cell * 71;
  for (let y = 0; y < C; y++)
    for (let x = 0; x < C; x++) {
      const u = (x + 0.5) / C * 2 - 1, v = (y + 0.5) / C * 2 - 1;
      const vv = v + 0.25 * (1 - u * u); // flames lick up the panel
      const r = Math.hypot(u * 1.1, vv), ang = Math.atan2(vv, u);
      const edge = 0.55 + 0.25 * (ring(ang, 2, seed) - 0.5) * 2;
      const grain = fbm(u * 8, v * 8, seed + 2, 4), fine = fbm(u * 30, v * 30, seed + 3, 3);
      let a = Math.pow(clamp((edge - r) / edge), 0.8) * (0.6 + 0.4 * grain);
      if (fine > 0.78) a *= 0.6; // blistered flecks
      a *= clamp((0.97 - Math.abs(u)) / 0.1) * clamp((0.97 - Math.abs(v)) / 0.1);
      // MULTIPLY: what the paint is multiplied by; cooked brown at the rim
      const t = clamp(r / edge);
      const ch = mix(0.06, 0.14, grain);
      put(ox, C, x, y, mix(ch, 0.45, t * t), mix(ch * 0.95, 0.36, t * t), mix(ch * 0.9, 0.28, t * t), a);
    }
}
writePng('body.png', bp);
