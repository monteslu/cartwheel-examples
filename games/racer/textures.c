/* racer's textures: every surface gets a full PBR set made here at load time (no image files):
 * albedo (sRGB), a tangent-space normal map from a height field, roughness, ambient occlusion
 * and, for the few kinds that glow, an emissive map. All tile without seams (periodic noise),
 * are mipmapped with anisotropy, and are made once, on first use, and shared. Integer hashing
 * and cartwheel/math.h only, so every host makes the same texels (debug field tex_hash).
 *
 * Sizes and what one repeat covers (the mesh's uv, see trackmesh.c):
 *   roads   1024 x 512: u across the road (one repeat), v along it (tile_v metres, about 8). The road is
 *           treated as 16 m wide by 8 m long for noise scale, so cells are round on a typical 18 m road.
 *   walls   512 x 512: u UP the wall (4 m per repeat), v ALONG it (4 m per repeat), bottom at u = 0.
 *   ground  256 x 256: no fixed scale, tiles freely.
 * The generators work in 0..1 texture space; the helpers below turn it into metres where a pattern
 * has a real size (lane lines, bricks, plates). */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "textures.h"

/* ── hashing and noise ──────────────────────────────────────────────── */

static uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xffffff) / 16777216.0f; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static int wrapi(int a, int p) { return ((a % p) + p) % p; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float fractf(float x) { return x - floorf(x); }
static float sstep(float a, float b, float x) { return smooth(clamp01((x - a) / (b - a))); }
/* 1 inside |x - c| < hw, fading over `soft` outside the edge */
static float band(float x, float c, float hw, float soft) { return clamp01((hw - fabsf(x - c)) / soft + 0.5f); }

/* periodic value noise with pu x pv cells across the texture */
static float vnoise(float x, float y, int pu, int pv, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = smooth(x - x0), fy = smooth(y - y0);
  float a = h01(wrapi(x0, pu), wrapi(y0, pv), seed), b = h01(wrapi(x0 + 1, pu), wrapi(y0, pv), seed);
  float c = h01(wrapi(x0, pu), wrapi(y0 + 1, pv), seed), d = h01(wrapi(x0 + 1, pu), wrapi(y0 + 1, pv), seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm2(float u, float v, int pu, int pv, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) {
    s += amp * vnoise(u * pu, v * pv, pu, pv, seed + (uint32_t)o * 101u);
    norm += amp;
    amp *= 0.5f;
    pu *= 2, pv *= 2;
  }
  return s / norm;
}
static float fbm(float u, float v, int p, int oct, uint32_t seed) { return fbm2(u, v, p, p, oct, seed); }
/* road noise: k cells along the 8 m of v, 2k across the 16 m of u, so features are round on the road */
static float rfbm(float u, float v, int k, int oct, uint32_t seed) { return fbm2(u, v, 2 * k, k, oct, seed); }

static void cells2(float u, float v, int pu, int pv, uint32_t seed, float *d1, float *d2, uint32_t *id) {
  float x = u * pu, y = v * pv;
  int cx = (int)floorf(x), cy = (int)floorf(y);
  *d1 = *d2 = 9;
  *id = 0;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      int gx = cx + i, gy = cy + j, wx = wrapi(gx, pu), wy = wrapi(gy, pv);
      float px = gx + h01(wx, wy, seed), py = gy + h01(wx, wy, seed + 7);
      float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
      if (d < *d1) {
        *d2 = *d1;
        *d1 = d;
        *id = hash2(wx, wy, seed + 13);
      } else if (d < *d2) *d2 = d;
    }
}

typedef struct texel { float r, g, b, h, rough, ao, er, eg, eb; } texel;
typedef void (*gen_fn)(float u, float v, texel *t);
static void rgb(texel *t, float r, float g, float b) { t->r = r, t->g = g, t->b = b; }
static void mixc(texel *t, float r, float g, float b, float k) {
  t->r = mixf(t->r, r, k), t->g = mixf(t->g, g, k), t->b = mixf(t->b, b, k);
}
static void emit(texel *t, float r, float g, float b, float k) {
  t->er = fmaxf(t->er, r * k), t->eg = fmaxf(t->eg, g * k), t->eb = fmaxf(t->eb, b * k);
}

/* ── roads: u 0..1 across (16 m), v 0..1 along (8 m) ────────────────── */

/* Lane paint shared by the paved roads: edge lines at u 0.075 / 0.925 (just outside the kerb) and a
 * dashed centre line. `wear` 0..1 is how chipped it is. Returns the cover 0..1. */
static float lane_paint(float u, float v, float wear, uint32_t seed) {
  float edge = band(u, 0.075f, 0.0075f, 0.004f) + band(u, 0.925f, 0.0075f, 0.004f);
  float dash = sstep(0.06f, 0.09f, fractf(v)) * (1 - sstep(0.5f, 0.53f, fractf(v)));
  float centre = band(u, 0.5f, 0.0065f, 0.004f) * dash;
  float chip = rfbm(u, v, 32, 3, seed);
  float cover = clamp01((chip - wear) * 6.0f);
  return clamp01(edge + centre) * cover;
}

/* worn asphalt: grit, two polished wheel lines, hairline cracks, tar patches, chipped paint.
 * wet 0 dry .. 1 soaked: darker, glossy, with mirror puddles */
static void asphalt_core(float u, float v, texel *t, float wet) {
  float n1 = rfbm(u, v, 4, 4, 91), n2 = rfbm(u, v, 24, 3, 92);
  float grit = h01((int)(u * 1024), (int)(v * 512), 93);
  float tracks = clamp01(band(u, 0.3f, 0.07f, 0.06f) + band(u, 0.7f, 0.07f, 0.06f)) * (0.55f + 0.45f * rfbm(u, v, 2, 3, 94));
  float c = 0.2f + 0.06f * n1 + 0.035f * (grit - 0.5f) + 0.03f * (n2 - 0.5f);
  float fleck = grit > 0.94f ? (grit - 0.94f) * 12.0f : 0;
  c += fleck * 0.12f - tracks * 0.045f;
  /* tar patches: big cells, a few of them darker with a sealed seam */
  float d1, d2;
  uint32_t id;
  cells2(u, v, 5, 3, 95, &d1, &d2, &id);
  float patch = (id & 255) < 40 ? 1.0f : 0.0f;
  float seam = patch * (1 - sstep(0.0f, 0.05f, d2 - d1));
  c = c * (1 - 0.3f * patch) - seam * 0.05f;
  /* hairline cracks: narrow where two cells meet, only where a noise mask allows */
  float cd1, cd2;
  uint32_t cid;
  cells2(u, v, 8, 4, 96, &cd1, &cd2, &cid);
  float crack = (1 - sstep(0.0f, 0.035f, cd2 - cd1)) * sstep(0.5f, 0.62f, rfbm(u, v, 3, 3, 97));
  c -= crack * 0.1f;
  float paint = lane_paint(u, v, 0.3f, 98);
  float w = wet > 0 ? sstep(0.52f, 0.64f, rfbm(u, v, 3, 4, 99)) * wet : 0;
  /* wet: dark sheen everywhere, mirror in the puddles */
  c *= 1 - 0.4f * wet;
  rgb(t, c, c, c * 1.06f);
  mixc(t, 0.82f - 0.25f * wet, 0.82f - 0.25f * wet, 0.76f - 0.25f * wet, paint);
  t->h = 0.3f + 0.35f * n2 + 0.25f * grit - crack * 0.5f + paint * 0.08f;
  float rough = 0.86f - 0.12f * tracks - 0.1f * paint;
  rough = mixf(rough, 0.34f, wet);
  rough = mixf(rough, 0.03f, w);
  t->rough = rough;
  t->h = mixf(t->h, 0.5f, w);
  c = c * (1 - 0.35f * w);
  t->ao = 0.86f + 0.14f * n1 - crack * 0.4f;
  if (wet > 0 && w > 0) mixc(t, 0.02f, 0.025f, 0.035f, w * 0.7f);
}
static void g_road_asphalt(float u, float v, texel *t) { asphalt_core(u, v, t, 0); }
static void g_road_wet(float u, float v, texel *t) { asphalt_core(u, v, t, 1); }

/* pale concrete slabs 4 m long, a joint down the middle, expansion joints with tar, cracks, stains */
static void g_road_concrete(float u, float v, texel *t) {
  float x = u * 16, y = v * 8;
  float sy = y / 4, slab = floorf(sy);
  float jd = fminf(fractf(sy), 1 - fractf(sy)) * 4;                     /* metres to a cross joint */
  float jl = fabsf(x - 8);                                              /* metres to the centre joint */
  float joint = 1 - sstep(0.02f, 0.07f, fminf(jd, jl));
  int lane = x < 8 ? 0 : 1;
  float tint = h01((int)slab, lane, 111);
  float n = rfbm(u, v, 6, 5, 112), fine = h01((int)(u * 1024), (int)(v * 512), 113);
  float c = 0.62f + 0.07f * tint + 0.1f * (n - 0.5f) + 0.03f * (fine - 0.5f);
  float cd1, cd2;
  uint32_t cid;
  cells2(u, v, 4, 2, 114 + (uint32_t)lane, &cd1, &cd2, &cid);
  float crack = (1 - sstep(0.0f, 0.03f, cd2 - cd1)) * sstep(0.48f, 0.6f, rfbm(u, v, 3, 3, 115));
  float stain = sstep(0.55f, 0.75f, rfbm(u, v, 3, 4, 116)) * 0.18f;
  float tracks = clamp01(band(u, 0.3f, 0.06f, 0.06f) + band(u, 0.7f, 0.06f, 0.06f)) * 0.08f;
  c -= stain + tracks + crack * 0.2f;
  float paint = lane_paint(u, v, 0.45f, 117);
  rgb(t, c, c * 0.985f, c * 0.95f);
  mixc(t, 0.14f, 0.14f, 0.13f, joint * 0.85f);
  mixc(t, 0.9f, 0.9f, 0.82f, paint);
  t->h = 0.55f + 0.2f * n + 0.1f * fine - joint * 0.6f - crack * 0.4f;
  t->rough = 0.82f - 0.15f * paint + 0.06f * stain;
  t->ao = 0.88f + 0.12f * n - joint * 0.5f - crack * 0.3f;
}

/* packed earth: two deep wheel ruts with a crowned middle, stones, dry crust cracks, tufts at the sides */
static void g_road_dirt(float u, float v, texel *t) {
  float n = rfbm(u, v, 6, 5, 121), clod = rfbm(u, v, 24, 3, 122), grit = h01((int)(u * 1024), (int)(v * 512), 123);
  float rut = clamp01(band(u, 0.3f, 0.055f, 0.07f) + band(u, 0.7f, 0.055f, 0.07f));
  rut *= 0.75f + 0.25f * rfbm(u, v, 3, 3, 124);
  float crown = band(u, 0.5f, 0.07f, 0.06f);
  float side = sstep(0.14f, 0.04f, u) + sstep(0.86f, 0.96f, u);
  float k = 0.82f + 0.3f * n + 0.12f * (clod - 0.5f) + 0.05f * (grit - 0.5f);
  rgb(t, 0.5f * k, 0.355f * k, 0.22f * k);
  mixc(t, 0.34f, 0.24f, 0.15f, rut * 0.65f);           /* compacted, damp */
  mixc(t, 0.46f, 0.43f, 0.2f, side * 0.5f * sstep(0.4f, 0.6f, clod)); /* dry grass at the verges */
  /* pebbles */
  float d1, d2;
  uint32_t id;
  cells2(u, v, 64, 32, 125, &d1, &d2, &id);
  float peb = (id & 255) < 24 ? clamp01(1 - d1 / (0.22f + 0.2f * h01((int)(id & 255), 3, 7))) : 0;
  float pt = 0.45f + 0.3f * h01((int)(id >> 8 & 255), 4, 8);
  mixc(t, pt, pt * 0.95f, pt * 0.85f, peb);
  float cd1, cd2;
  uint32_t cid;
  cells2(u, v, 10, 5, 126, &cd1, &cd2, &cid);
  float crack = (1 - sstep(0.0f, 0.05f, cd2 - cd1)) * (1 - rut) * sstep(0.45f, 0.6f, rfbm(u, v, 3, 2, 127));
  t->r -= crack * 0.18f, t->g -= crack * 0.14f, t->b -= crack * 0.1f;
  t->h = 0.6f + 0.25f * clod + 0.1f * n - rut * 0.45f + crown * 0.1f + peb * 0.3f - crack * 0.3f;
  t->rough = 0.95f - 0.12f * rut;
  t->ao = 0.8f + 0.2f * clod - rut * 0.1f - crack * 0.3f;
}

/* cracked salt crust: raised plates ringed by dark seams, pink-grey tint on the plate rims, faint tyre
 * marks, a chalky dashed orange line. The sparkle is in the roughness. */
static void g_road_salt(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells2(u, v, 14, 7, 131, &d1, &d2, &id);
  float gap = d2 - d1;
  float seam = 1 - sstep(0.0f, 0.1f, gap);
  float rim = 1 - sstep(0.1f, 0.35f, gap);
  float n = rfbm(u, v, 6, 5, 132), fine = h01((int)(u * 1024), (int)(v * 512), 133);
  float tint = (float)(id & 255) / 255.0f;
  float k = 0.9f + 0.06f * (n - 0.5f) + 0.05f * (tint - 0.5f) + 0.03f * (fine - 0.5f);
  rgb(t, 0.93f * k, 0.92f * k, 0.9f * k);
  mixc(t, 0.78f, 0.7f, 0.66f, rim * 0.4f);          /* pinkish dust gathers at the plate rims */
  mixc(t, 0.46f, 0.4f, 0.37f, seam * 0.85f);        /* the cracks */
  /* curled plate edges catch light */
  float streak = fbm2(u, v, 24, 3, 3, 134);
  float tracks = clamp01(band(u, 0.3f, 0.05f, 0.08f) + band(u, 0.7f, 0.05f, 0.08f)) * sstep(0.35f, 0.65f, streak);
  mixc(t, 0.62f, 0.58f, 0.52f, tracks * 0.45f * (1 - seam));
  float dash = sstep(0.06f, 0.09f, fractf(v)) * (1 - sstep(0.5f, 0.53f, fractf(v)));
  float chalk = (band(u, 0.5f, 0.007f, 0.004f) * dash + band(u, 0.075f, 0.008f, 0.004f) + band(u, 0.925f, 0.008f, 0.004f));
  chalk = clamp01(chalk) * clamp01((rfbm(u, v, 24, 3, 135) - 0.3f) * 4);
  mixc(t, 0.95f, 0.5f, 0.2f, chalk * 0.9f);
  t->h = 0.7f - seam * 0.6f + 0.15f * (1 - rim) + 0.1f * n + 0.06f * fine;
  t->rough = (fine > 0.97f ? 0.25f : 0.6f + 0.2f * n) - tracks * 0.1f;
  t->ao = 0.9f + 0.1f * n - seam * 0.6f;
}

/* blue-white ice: deep blue pockets, long scratches, a polished racing line, frost at the edges */
static void g_road_ice(float u, float v, texel *t) {
  float n = rfbm(u, v, 4, 5, 141), deep = sstep(0.45f, 0.7f, rfbm(u, v, 3, 3, 142));
  /* scratches: noise stretched along v, ridged, plus a few diagonal hairlines */
  float sc = fabsf(fbm2(u, v, 96, 3, 2, 143) - 0.5f) * 2;
  float scratch = (1 - sstep(0.0f, 0.06f, sc)) * sstep(0.45f, 0.6f, rfbm(u, v, 6, 2, 144));
  float dg = fabsf(fractf((u * 3 + v * 6 + 0.4f * fbm(u, v, 3, 2, 145)) * 2) - 0.5f) * 2;
  float hair = (1 - sstep(0.0f, 0.04f, dg)) * sstep(0.55f, 0.7f, rfbm(u, v, 4, 3, 146));
  float frost = clamp01(sstep(0.18f, 0.04f, u) + sstep(0.82f, 0.96f, u) + sstep(0.58f, 0.78f, rfbm(u, v, 5, 4, 147)) * 0.7f);
  float d1, d2;
  uint32_t id;
  cells2(u, v, 6, 3, 148, &d1, &d2, &id);
  float crack = (1 - sstep(0.0f, 0.03f, d2 - d1)) * sstep(0.5f, 0.6f, rfbm(u, v, 3, 3, 149));
  float k = 0.88f + 0.12f * n;
  rgb(t, 0.7f * k, 0.86f * k, 0.98f * k);
  mixc(t, 0.4f, 0.62f, 0.9f, deep * 0.55f);
  mixc(t, 0.95f, 0.98f, 1.0f, clamp01(scratch * 0.7f + hair * 0.6f + crack * 0.8f));
  mixc(t, 0.93f, 0.97f, 1.0f, frost * 0.8f);
  float line = clamp01(band(u, 0.3f, 0.05f, 0.06f) + band(u, 0.7f, 0.05f, 0.06f));
  t->h = 0.6f + 0.1f * n + frost * 0.2f * h01((int)(u * 512), (int)(v * 256), 150) - scratch * 0.2f - crack * 0.25f;
  t->rough = mixf(0.07f + 0.05f * n, 0.5f, frost) - line * 0.04f + scratch * 0.2f;
  t->ao = 0.92f + 0.08f * n - crack * 0.25f;
}

/* diamond plate with riveted 2 x 4 m panels, a grating panel every fourth, hazard stripes at both edges */
static void g_road_metal(float u, float v, texel *t) {
  float x = u * 16, y = v * 8;
  float py = y / 4, pan = floorf(py);
  float px2 = x / 2;
  float edge = fminf(fminf(fractf(px2), 1 - fractf(px2)) * 2, fminf(fractf(py), 1 - fractf(py)) * 4); /* metres to a panel joint */
  float seam = 1 - sstep(0.015f, 0.05f, edge);
  float n = rfbm(u, v, 8, 4, 151), fine = h01((int)(u * 1024), (int)(v * 512), 152);
  int grate = ((int)pan & 3) == 2 && x > 2.6f && x < 13.4f;
  float k = 0.46f + 0.08f * n + 0.04f * (h01((int)floorf(px2), (int)pan, 153) - 0.5f);
  /* diamond lugs: a 0.22 m cell, a slanted bar in each, alternate cells turn the other way */
  float cx = x / 0.25f, cy = y / 0.25f;
  int ci = (int)floorf(cx), cj = (int)floorf(cy);
  float fx = fractf(cx) - 0.5f, fy = fractf(cy) - 0.5f;
  float s = ((ci + cj) & 1) ? 1.0f : -1.0f;
  float along = (fx + s * fy) * 0.7071f, across = (fx - s * fy) * 0.7071f;
  float lug = clamp01(1 - sqrtf(across * across * 14.0f + along * along * 1.6f) * 2.2f);
  float h = 0.4f + 0.4f * lug;
  float r = k * 1.0f, g = k * 1.01f, b = k * 1.04f;
  float rough = 0.42f + 0.2f * n - 0.15f * lug;
  float ao = 0.7f + 0.3f * (0.5f + lug);
  if (grate) {
    /* a grating panel: bars every 0.16 m with dark slots between */
    float bar = sstep(0.28f, 0.4f, fabsf(fractf(x / 0.16f) - 0.5f)) * 0 + (fabsf(fractf(x / 0.16f) - 0.5f) < 0.28f ? 1.0f : 0.0f);
    float cross = fabsf(fractf(y / 0.8f) - 0.5f) < 0.06f ? 1.0f : 0.0f;
    float solid = fmaxf(bar, cross);
    h = 0.2f + 0.7f * solid;
    ao = 0.1f + 0.9f * solid;
    r = g = b = (0.34f + 0.06f * n) * (0.35f + 0.65f * solid);
    rough = 0.35f + 0.2f * n;
  }
  /* rust creeping out of the seams, orange speckle */
  float rust = clamp01((rfbm(u, v, 12, 4, 154) - 0.55f) * 4) * 0.7f + seam * 0.4f * sstep(0.4f, 0.6f, n);
  rgb(t, r, g, b);
  mixc(t, 0.5f, 0.27f, 0.12f, rust * 0.7f);
  /* rivets in every panel corner */
  float rd = 9;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      float dx = fractf(px2) - (0.09f + i * 0.82f), dy = (fractf(py) - (0.04f + j * 0.92f)) * 2;
      rd = fminf(rd, sqrtf(dx * dx + dy * dy));
    }
  float rivet = clamp01(1 - rd / 0.045f);
  /* hazard edges: yellow and black diagonal bands within 0.9 m of each side edge */
  float hz = fmaxf(sstep(0.9f, 0.7f, x), sstep(15.1f, 15.3f, x));
  float stripe = fractf((x + y) / 0.8f) < 0.5f ? 1.0f : 0.0f;
  float wear = sstep(0.35f, 0.6f, rfbm(u, v, 20, 3, 155));
  mixc(t, stripe ? 0.9f : 0.08f, stripe ? 0.72f : 0.08f, stripe ? 0.08f : 0.08f, hz * (1 - 0.6f * wear));
  mixc(t, 0.62f, 0.62f, 0.6f, rivet * 0.6f);
  mixc(t, 0.07f, 0.07f, 0.07f, seam * 0.7f);
  t->h = (grate ? h : mixf(h, 0.5f, hz)) + rivet * 0.5f - seam * 0.5f;
  t->rough = rough + rust * 0.4f - (fine > 0.985f ? 0.2f : 0);
  t->ao = ao - seam * 0.5f;
}

/* weathered planks laid along the road with butt joints, nails at the joints, silver-grey grain */
static void g_road_wood(float u, float v, texel *t) {
  float x = u * 16, y = v * 8;
  float pw = x / 0.5f; /* 32 planks across */
  int plank = (int)floorf(pw);
  float within = fractf(pw);
  float off = h01(plank, 0, 161) * 8;                  /* each plank's joints sit at their own place */
  float len = 2.0f + 2.0f * h01(plank, 1, 161);        /* but must tile: 8 m divided into whole boards */
  int nb = (int)(8.0f / len + 0.5f);
  len = 8.0f / (float)nb;
  float yy = y + off;
  float by = fmodf(yy, 8.0f);
  int board = (int)floorf(by / len);
  float inb = fractf(by / len);
  float jd = fminf(inb, 1 - inb) * len;                 /* metres to a butt joint */
  float gap = fminf(within, 1 - within) * 0.5f;         /* metres to the plank edge */
  float seam = 1 - sstep(0.008f, 0.03f, fminf(gap, jd));
  float tint = h01(plank, board + 3, 162);
  float warp = fbm(u, v, 3, 3, 163) * 3.0f + tint * 6;
  float grain = 0.5f + 0.5f * cw_sin((within * 5.5f + warp + 0.3f * fbm2(u, v, 32, 8, 3, 164)) * 6.2831853f);
  float fine = h01((int)(u * 1024), (int)(v * 512), 165);
  float k = 0.62f + 0.32f * tint;
  float wr = 0.7f * k * (0.82f + 0.2f * grain), wg = 0.52f * k * (0.82f + 0.2f * grain), wb = 0.34f * k * (0.84f + 0.18f * grain);
  float gray = sstep(0.4f, 0.7f, fbm(u, v, 5, 4, 166));
  rgb(t, mixf(wr, wg * 1.2f, gray * 0.6f), mixf(wg, wg * 1.1f, gray * 0.6f), mixf(wb, wg * 1.0f, gray * 0.6f));
  /* nails: two per board end */
  float nd = 9;
  for (int e = 0; e < 2; e++)
    for (int s2 = 0; s2 < 2; s2++) {
      float dx = (within - (0.25f + 0.5f * s2)) * 0.5f, dy = (inb < 0.5f ? inb : inb - 1) * len - (e ? -1 : 1) * 0.0f;
      dy = (inb < 0.5f ? inb * len : (inb - 1) * len) - (e ? 0 : 0) - 0.12f * (inb < 0.5f ? 1 : -1);
      nd = fminf(nd, sqrtf(dx * dx + dy * dy));
    }
  float nail = clamp01(1 - nd / 0.025f);
  mixc(t, 0.13f, 0.12f, 0.12f, seam * 0.9f);
  mixc(t, 0.35f, 0.33f, 0.3f, nail);
  t->h = 0.6f + 0.15f * grain + 0.04f * fine - seam * 0.5f + nail * 0.2f;
  t->rough = 0.8f + 0.1f * fine - nail * 0.4f;
  t->ao = 0.55f + 0.45f * (1 - seam) * (0.9f + 0.1f * grain);
}

/* temple flagstones: irregular slabs, a worn crown, moss in the joints and on the damp side, cracks */
static void g_road_stone(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  /* warp the lookup a little so the joints wander */
  float wu = u + 0.012f * (fbm(u, v, 6, 2, 171) - 0.5f), wv = v + 0.024f * (fbm(u, v, 6, 2, 172) - 0.5f);
  cells2(wu, wv, 6, 3, 173, &d1, &d2, &id);
  float gap = d2 - d1;
  float joint = 1 - sstep(0.0f, 0.12f, gap);
  float dome = sstep(0.0f, 0.5f, gap);
  float n = rfbm(u, v, 8, 5, 174), fine = h01((int)(u * 1024), (int)(v * 512), 175);
  float tint = (float)(id & 255) / 255.0f;
  float k = 0.58f + 0.14f * tint + 0.12f * (n - 0.5f) + 0.04f * (fine - 0.5f);
  rgb(t, k * 1.0f, k * 0.96f, k * 0.86f);
  float moss = clamp01(joint * 0.9f + sstep(0.58f, 0.72f, rfbm(u, v, 3, 4, 176)) * 0.7f + sstep(0.2f, 0.0f, u) * 0.5f + sstep(0.8f, 1.0f, u) * 0.5f);
  moss *= 0.5f + 0.5f * sstep(0.3f, 0.6f, rfbm(u, v, 20, 3, 177));
  mixc(t, 0.2f + 0.08f * n, 0.38f + 0.1f * n, 0.12f, moss * 0.75f);
  mixc(t, 0.14f, 0.12f, 0.09f, joint * (1 - moss) * 0.7f);
  float polish = band(u, 0.5f, 0.2f, 0.2f);
  t->h = 0.5f + 0.25f * dome + 0.12f * n - joint * 0.6f + 0.05f * fine;
  t->rough = 0.88f - 0.1f * polish * dome - moss * 0.05f;
  t->ao = 0.8f + 0.2f * dome - joint * 0.5f;
}

/* futuristic roadway: near-black panels, glowing edge rails (cyan), a dashed centre (magenta), lane
 * studs and panel seams that pulse along the road */
static void g_road_tech(float u, float v, texel *t) {
  float x = u * 16, y = v * 8;
  float n = rfbm(u, v, 8, 4, 181), fine = h01((int)(u * 1024), (int)(v * 512), 182);
  float py = y / 4;
  float seam = 1 - sstep(0.012f, 0.04f, fminf(fractf(py), 1 - fractf(py)) * 4);
  float seam2 = 1 - sstep(0.012f, 0.04f, fabsf(x - 8));
  float c = 0.07f + 0.025f * n + 0.012f * fine;
  rgb(t, c * 0.9f, c, c * 1.25f);
  /* faint hex mesh in the panels */
  float hx = fabsf(fractf(x * 2.0f) - 0.5f), hy = fabsf(fractf(y * 1.1547f + 0.5f * floorf(x * 2.0f)) - 0.5f);
  float mesh = sstep(0.43f, 0.5f, fmaxf(hx, hy * 0.9f));
  t->r += mesh * 0.015f, t->g += mesh * 0.03f, t->b += mesh * 0.045f;
  float rail = band(u, 0.075f, 0.0075f, 0.003f) + band(u, 0.925f, 0.0075f, 0.003f);
  float rail_halo = band(u, 0.075f, 0.02f, 0.02f) + band(u, 0.925f, 0.02f, 0.02f);
  float dash = sstep(0.06f, 0.09f, fractf(v)) * (1 - sstep(0.5f, 0.53f, fractf(v)));
  float cen = band(u, 0.5f, 0.007f, 0.003f) * dash;
  float stud = 0;
  for (int i = 1; i <= 3; i += 2) {
    float dx = (u - 0.25f * i) * 16, dy = (fractf(v * 4) - 0.5f) * 2;
    stud = fmaxf(stud, clamp01(1 - sqrtf(dx * dx + dy * dy) / 0.14f));
  }
  mixc(t, 0.1f, 0.85f, 1.0f, clamp01(rail));
  mixc(t, 0.95f, 0.2f, 0.85f, clamp01(cen));
  mixc(t, 0.2f, 0.75f, 1.0f, stud * 0.9f);
  emit(t, 0.1f, 0.85f, 1.0f, clamp01(rail) * 1.0f + clamp01(rail_halo) * 0.12f);
  emit(t, 0.95f, 0.2f, 0.85f, clamp01(cen) * 1.0f);
  emit(t, 0.15f, 0.55f, 1.0f, stud * 0.8f + (seam + seam2) * 0.1f * (0.4f + 0.6f * n));
  mixc(t, 0.02f, 0.04f, 0.07f, (seam + seam2) * 0.5f);
  t->h = 0.6f + 0.1f * n - (seam + seam2) * 0.5f + clamp01(rail) * 0.1f;
  t->rough = 0.3f + 0.2f * n + 0.15f * fine - clamp01(rail) * 0.15f;
  t->ao = 0.85f + 0.15f * n - (seam + seam2) * 0.4f;
}

/* wind-rippled sand: crests that wander along the road, flattened and darker wheel tracks, a few pebbles */
static void g_road_sand(float u, float v, texel *t) {
  float wob = rfbm(u, v, 3, 4, 191), tracks = clamp01(band(u, 0.3f, 0.06f, 0.07f) + band(u, 0.7f, 0.06f, 0.07f));
  float x = u * 16, y = v * 8;
  /* crests run roughly along v, 0.35 m apart, bending with the noise */
  float crest = 0.5f + 0.5f * cw_sin((x / 0.35f + wob * 14.0f) * 6.2831853f / 1.0f + 6.2831853f * 0.0f);
  crest = crest * (1 - 0.8f * tracks);
  float cross = 0.5f + 0.5f * cw_sin((y * 2.0f + wob * 5.0f) * 3.14159f);
  float n = rfbm(u, v, 12, 4, 192), grain = h01((int)(u * 1024), (int)(v * 512), 193);
  float k = 0.9f + 0.1f * n + 0.04f * (grain - 0.5f) - 0.12f * tracks;
  float shade = 0.9f + 0.1f * crest;
  rgb(t, 0.9f * k * shade, 0.74f * k * shade, 0.5f * k * shade);
  float d1, d2;
  uint32_t id;
  cells2(u, v, 40, 20, 194, &d1, &d2, &id);
  float peb = (id & 255) < 12 ? clamp01(1 - d1 / 0.3f) : 0;
  mixc(t, 0.5f, 0.42f, 0.34f, peb);
  t->h = 0.35f + 0.4f * crest * (0.8f + 0.2f * cross) + 0.1f * n + 0.04f * grain + peb * 0.3f;
  t->rough = 0.93f;
  t->ao = 0.82f + 0.18f * crest;
}

/* glass road plate: a faint grid of frames every 2 x 4 m, ceramic dots near the edges. The material makes it clear. */
static void g_road_glass(float u, float v, texel *t) {
  float x = u * 16, y = v * 8;
  float ex = fminf(fractf(x / 2.0f), 1 - fractf(x / 2.0f)) * 2, ey = fminf(fractf(y / 4.0f), 1 - fractf(y / 4.0f)) * 4;
  float frame = 1 - sstep(0.03f, 0.09f, fminf(ex, ey));
  float n = fbm(u, v, 8, 3, 201), fine = h01((int)(u * 1024), (int)(v * 512), 202);
  /* frit dots: a 0.12 m grid, bigger toward the panel edges */
  float dx = fractf(x / 0.12f) - 0.5f, dy = fractf(y / 0.12f) - 0.5f;
  float r = 0.1f + 0.3f * (1 - sstep(0.0f, 0.5f, fminf(ex, ey)));
  float dot = clamp01((r - sqrtf(dx * dx + dy * dy)) / 0.05f) * (r > 0.12f ? 1.0f : 0.0f);
  float scr = (1 - sstep(0.0f, 0.05f, fabsf(fbm2(u, v, 80, 4, 2, 203) - 0.5f) * 2)) * sstep(0.5f, 0.65f, fbm(u, v, 4, 2, 204));
  rgb(t, 0.5f + 0.04f * n, 0.66f + 0.04f * n, 0.7f + 0.04f * n);
  mixc(t, 0.9f, 0.97f, 1.0f, frame * 0.8f + scr * 0.5f);
  mixc(t, 0.25f, 0.34f, 0.38f, dot * 0.7f);
  t->h = 0.5f + frame * 0.2f - dot * 0.1f;
  t->rough = 0.04f + 0.1f * frame + 0.25f * dot + 0.25f * scr + 0.02f * fine;
  t->ao = 0.95f - frame * 0.1f;
}

/* ── walls: u up (4 m), v along (4 m); the bottom edge is u = 0 ────── */

/* stained jersey concrete: 2 m pour panels, form-tie holes, shuttering lines, rain streaks, a dirty foot, tyre scuffs */
static void g_wall_concrete(float u, float v, texel *t) {
  float up = u * 4, al = v * 4;
  float n = fbm(u, v, 8, 5, 211), fine = h01((int)(u * 512), (int)(v * 512), 212);
  float pit = fine > 0.985f ? 1.0f : 0.0f;
  float jd = fminf(fractf(al * 0.5f), 1 - fractf(al * 0.5f)) * 2;
  float joint = 1 - sstep(0.012f, 0.04f, jd);
  float gd = fminf(fractf(up), 1 - fractf(up));
  float groove = (1 - sstep(0.004f, 0.016f, gd)) * 0.7f;
  float ties = 0;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 4; j++) {
      float dx = fractf(al * 0.5f) * 2 - (0.5f + i), dy = up - (0.55f + j);
      ties = fmaxf(ties, clamp01(1 - sqrtf(dx * dx + dy * dy) / 0.045f));
    }
  float streak = fbm2(u, v, 2, 40, 3, 213), rain = sstep(0.52f, 0.72f, streak);
  float foot = 1 - sstep(0.0f, 0.7f, up);
  float scuffn = fbm2(u, v, 2, 12, 3, 214), scuff = sstep(0.58f, 0.78f, scuffn) * (1 - sstep(0.25f, 0.75f, up));
  float c = 0.66f + 0.1f * (n - 0.5f) + 0.025f * (fine - 0.5f) - 0.2f * rain * (0.5f + 0.5f * n) - 0.14f * foot * (0.6f + 0.4f * n);
  rgb(t, c, c * 0.985f, c * 0.95f);
  mixc(t, 0.14f, 0.13f, 0.12f, scuff * 0.7f);
  mixc(t, 0.22f, 0.2f, 0.16f, joint * 0.5f + ties * 0.8f + groove * 0.2f);
  t->h = 0.55f + 0.3f * n + 0.05f * fine - pit * 0.3f - joint * 0.5f - groove * 0.4f - ties * 0.6f;
  t->rough = 0.9f - 0.2f * scuff + 0.05f * rain;
  t->ao = 0.9f + 0.1f * n - joint * 0.5f - ties * 0.6f - pit * 0.3f - groove * 0.2f;
}

/* yellow and black hazard stripes on painted steel, scuffed and dented */
static void g_wall_hazard(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 221), fine = h01((int)(u * 512), (int)(v * 512), 222);
  float diag = fractf((u + v) * 8 + 0.02f * (fbm(u, v, 4, 2, 223) - 0.5f));
  float yel = sstep(0.46f, 0.5f, diag) * (1 - sstep(0.96f, 1.0f, diag)) + (1 - sstep(0.0f, 0.04f, diag)) * 0;
  yel = diag < 0.5f ? sstep(0.0f, 0.02f, diag) * (1 - sstep(0.48f, 0.5f, diag)) : 0.0f;
  float scuff = sstep(0.55f, 0.75f, fbm2(u, v, 3, 14, 4, 224)) * (1 - sstep(0.3f, 0.8f, u * 4));
  float chip = sstep(0.6f, 0.72f, fbm(u, v, 24, 3, 225));
  float dirt = (1 - sstep(0.0f, 0.5f, u * 4)) * 0.4f + 0.2f * sstep(0.5f, 0.75f, fbm2(u, v, 2, 30, 3, 226));
  float yr = 0.95f, yg = 0.74f, yb = 0.08f;
  rgb(t, mixf(0.07f, yr, yel), mixf(0.07f, yg, yel), mixf(0.075f, yb, yel));
  mixc(t, 0.5f, 0.5f, 0.5f, scuff * 0.55f);
  mixc(t, 0.42f, 0.44f, 0.46f, chip * 0.8f); /* bare steel where the paint chipped */
  mixc(t, 0.2f, 0.17f, 0.12f, dirt);
  /* a rolled lip along the top of each 1 m panel and a bolt row */
  float pd = fminf(fractf(u * 4), 1 - fractf(u * 4)) * 0.25f * 4;
  float lip = 1 - sstep(0.01f, 0.03f, pd * 0.25f);
  float bolt = 0;
  for (int i = 0; i < 4; i++) {
    float dx = (fractf(v * 2) - 0.5f) * 2 - ((i & 1) ? 0.8f : -0.8f), dy = (fractf(u * 4) - 0.5f) * 1 - ((i >> 1) ? 0.4f : -0.4f);
    bolt = fmaxf(bolt, clamp01(1 - sqrtf(dx * dx * 0.25f + dy * dy) / 0.07f));
  }
  mixc(t, 0.5f, 0.5f, 0.5f, bolt * 0.6f);
  t->h = 0.55f + 0.1f * n + 0.04f * fine - chip * 0.2f - lip * 0.3f + bolt * 0.35f;
  t->rough = 0.5f + 0.2f * n + 0.3f * chip - 0.15f * yel;
  t->ao = 0.88f + 0.12f * n - lip * 0.3f - bolt * 0.1f;
}

/* corrugated sheet metal: 0.25 m ribs, faded teal paint worn through to orange rust, drip streaks, rivet rows */
static void g_wall_rust(float u, float v, texel *t) {
  float up = u * 4, al = v * 4;
  float rib = 0.5f + 0.5f * cw_sin(al * 16 * 6.2831853f / 4.0f * 1.0f * 1.0f);
  rib = 0.5f + 0.5f * cw_sin(v * 16.0f * 6.2831853f);
  float n = fbm(u, v, 6, 5, 231), fine = h01((int)(u * 512), (int)(v * 512), 232);
  float streak = fbm2(u, v, 3, 36, 3, 233);
  float rustm = clamp01(sstep(0.5f, 0.68f, n) * 0.9f + sstep(0.5f, 0.7f, streak) * 0.6f + (1 - sstep(0.0f, 1.0f, up)) * 0.4f);
  rustm = clamp01(rustm + (rib > 0.8f ? 0.3f * sstep(0.4f, 0.6f, n) : 0)); /* crests wear first */
  float sheet = fminf(fractf(up), 1 - fractf(up)) * 1;                       /* 1 m sheets stacked, overlap seam at each metre */
  float lap = 1 - sstep(0.005f, 0.03f, sheet);
  float rv = 0;
  for (int r = 0; r < 2; r++)
    for (int c = 0; c < 8; c++) {
      float dx = (fractf(v * 8) - 0.5f) * 0.5f, dy = fractf(up) - (r ? 0.9f : 0.1f);
      rv = fmaxf(rv, clamp01(1 - sqrtf(dx * dx + dy * dy) / 0.03f)) * (c < 1 ? 1.0f : 1.0f);
    }
  float pr = 0.26f, pg = 0.46f, pb = 0.5f;
  float k = 0.8f + 0.3f * rib;
  rgb(t, pr * k, pg * k, pb * k);
  float rr = 0.52f + 0.2f * n, rg = 0.26f + 0.1f * n, rb = 0.1f;
  mixc(t, rr, rg, rb, rustm);
  mixc(t, 0.12f, 0.09f, 0.07f, lap * 0.6f);
  mixc(t, 0.55f, 0.5f, 0.45f, rv * 0.5f);
  t->h = 0.2f + 0.6f * rib + 0.05f * fine + rustm * 0.1f * fine - lap * 0.3f + rv * 0.2f;
  t->rough = mixf(0.5f, 0.92f, rustm) + 0.05f * fine;
  t->ao = 0.55f + 0.45f * rib - lap * 0.3f;
}

/* terracotta brick in half-bond courses, soot at the foot, chipped corners */
static void g_wall_brick(float u, float v, texel *t) {
  float ry = u * 32, row = floorf(ry), fy = ry - row;
  float cx = v * 10 + (((int)row & 1) ? 0.5f : 0.0f), col = floorf(cx), fx = cx - col;
  uint32_t id = hash2(wrapi((int)col, 10), (int)row, 241);
  float dy = fminf(fy, 1 - fy) * 0.125f, dx = fminf(fx, 1 - fx) * 0.4f, d = fminf(dx, dy);
  float body = sstep(0.006f, 0.018f, d);
  float n = fbm(u, v, 8, 4, 242), fine = h01((int)(u * 512), (int)(v * 512), 243);
  float tint = (float)(id & 255) / 255.0f;
  float burnt = (id >> 8 & 15) < 2 ? 0.55f : 1.0f;
  float chip = sstep(0.5f, 0.7f, fbm(u, v, 30, 3, 244)) * (1 - sstep(0.0f, 0.03f, d));
  float k = (0.78f + 0.4f * tint + 0.12f * (n - 0.5f) + 0.06f * (fine - 0.5f)) * burnt;
  float soot = 1 - sstep(0.0f, 0.5f, u * 4);
  soot = soot * (0.5f + 0.5f * n) * 0.5f;
  rgb(t, 0.64f * k, 0.27f * k, 0.19f * k);
  mixc(t, 0.6f, 0.58f, 0.52f, (1 - body) * 0.95f);
  mixc(t, 0.12f, 0.1f, 0.09f, soot);
  mixc(t, 0.74f, 0.55f, 0.45f, chip * 0.6f);
  t->h = body * (0.75f + 0.15f * n + 0.1f * fine) - chip * 0.2f;
  t->rough = 0.85f + 0.1f * (1 - body);
  t->ao = 0.45f + 0.55f * body;
}

/* carved stone blocks, 0.5 m courses of two blocks, chamfered edges, a band of stepped glyph rings, moss at the foot */
static void g_wall_stone(float u, float v, texel *t) {
  float up = u * 4, al = v * 4;
  int row = (int)floorf(up / 0.5f);
  float cut = 0.8f + 2.4f * h01(row, 5, 251);                /* the second block starts here (0.8 .. 3.2 m) */
  float cutdist = fabsf(al - cut);
  float wrapd = fminf(al, 4 - al);
  float dv = fminf(cutdist, wrapd);
  float dh = fminf(fractf(up / 0.5f), 1 - fractf(up / 0.5f)) * 0.5f;
  float d = fminf(dv, dh);
  float body = sstep(0.0f, 0.035f, d), cham = sstep(0.0f, 0.06f, d);
  int blk = (al > cut) ? 1 : 0;
  uint32_t id = hash2(row, blk, 252);
  float n = fbm(u, v, 8, 5, 253), fine = h01((int)(u * 512), (int)(v * 512), 254);
  float tint = (float)(id & 255) / 255.0f;
  float k = 0.58f + 0.14f * tint + 0.14f * (n - 0.5f) + 0.04f * (fine - 0.5f);
  rgb(t, k * 1.0f, k * 0.95f, k * 0.85f);
  /* glyph band in row 3: nested squares per 0.5 m cell, carved in */
  float carve = 0;
  if (row == 3) {
    float bx = fractf(al / 0.5f) - 0.5f, by = fractf(up / 0.5f) - 0.5f;
    float ring = fmaxf(fabsf(bx), fabsf(by)) * 2;
    float step = fractf(ring * 2.5f);
    carve = (ring < 0.9f && step < 0.45f) ? 1.0f : 0.0f;
    if (((int)floorf(al / 0.5f) & 1) == 1) carve = (fabsf(bx) < 0.12f || fabsf(by) < 0.12f) && ring < 0.85f ? 1.0f : 0.0f;
  }
  float moss = clamp01((1 - sstep(0.0f, 0.9f, up)) * 1.1f * sstep(0.35f, 0.6f, n) + sstep(0.66f, 0.8f, fbm(u, v, 5, 4, 255)) * 0.8f + (1 - cham) * 0.15f);
  mixc(t, 0.15f + 0.05f * n, 0.34f + 0.1f * n, 0.1f, moss * 0.7f);
  mixc(t, 0.1f, 0.09f, 0.07f, (1 - body) * 0.6f + carve * 0.35f);
  t->h = 0.4f + 0.4f * cham + 0.12f * n + 0.04f * fine - carve * 0.35f;
  t->rough = 0.9f - 0.1f * (1 - moss);
  t->ao = 0.5f + 0.5f * cham - carve * 0.25f;
}

/* ice wall: pale blue, internal cracks, trapped bubbles, vertical drip streaks, frost toward the foot */
static void g_wall_ice(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 261), fine = h01((int)(u * 512), (int)(v * 512), 262);
  float d1, d2;
  uint32_t id;
  cells2(u, v, 5, 5, 263, &d1, &d2, &id);
  float crack = (1 - sstep(0.0f, 0.04f, d2 - d1)) * sstep(0.35f, 0.55f, fbm(u, v, 3, 3, 264));
  float bd1, bd2;
  uint32_t bid;
  cells2(u, v, 22, 22, 265, &bd1, &bd2, &bid);
  float rad = 0.12f + 0.14f * ((bid & 255) / 255.0f);
  float bubble = (bid & 7) < 2 ? clamp01((rad - bd1) / 0.06f) : 0;
  float drip = sstep(0.5f, 0.75f, fbm2(u, v, 2, 40, 3, 266));
  float frost = clamp01((1 - sstep(0.0f, 0.8f, u * 4)) * 0.9f + sstep(0.62f, 0.78f, n) * 0.6f);
  float deep = sstep(0.35f, 0.7f, fbm(u, v, 3, 3, 267));
  rgb(t, 0.62f + 0.12f * n, 0.82f + 0.08f * n, 0.95f);
  mixc(t, 0.3f, 0.56f, 0.86f, deep * 0.5f);
  mixc(t, 0.93f, 0.97f, 1.0f, clamp01(crack * 0.85f + bubble * 0.5f + drip * 0.3f));
  mixc(t, 0.95f, 0.98f, 1.0f, frost * 0.7f);
  t->h = 0.55f + 0.2f * n - crack * 0.3f + drip * 0.05f + frost * 0.1f * fine + bubble * 0.1f;
  t->rough = mixf(0.1f + 0.1f * n, 0.6f, frost) + crack * 0.2f;
  t->ao = 0.9f + 0.1f * n - crack * 0.3f;
}

/* riveted tech panels, 2 m x 1 m: plain, vented, hatch and light variants, with glowing seams low on the wall */
static void g_wall_tech(float u, float v, texel *t) {
  float up = u * 4, al = v * 4;
  float px = al * 0.5f, py = up;
  int col = (int)floorf(px), row = (int)floorf(py);
  float fx = fractf(px), fy = fractf(py);
  uint32_t id = hash2(wrapi(col, 2), wrapi(row, 4), 271);
  int kind = (int)(id & 7);
  float dx = fminf(fx, 1 - fx) * 2, dy = fminf(fy, 1 - fy);
  float d = fminf(dx, dy);
  float bev = sstep(0.0f, 0.05f, d), seam = 1 - sstep(0.008f, 0.03f, d);
  float n = fbm(u, v, 8, 4, 272), fine = h01((int)(u * 512), (int)(v * 512), 273);
  float scr = (1 - sstep(0.0f, 0.04f, fabsf(fbm2(u, v, 3, 90, 2, 274) - 0.5f) * 2)) * sstep(0.5f, 0.6f, fbm(u, v, 5, 2, 275));
  float c = 0.27f + 0.06f * n + 0.04f * (float)((id >> 4) & 3) / 3.0f;
  rgb(t, c * 0.9f, c, c * 1.18f);
  float h = 0.55f + 0.3f * bev;
  float ao = 0.6f + 0.4f * bev;
  float rough = 0.38f + 0.2f * n;
  /* rivets 0.1 m in from the corners */
  float rd = 9;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      float rx = (fx - (0.06f + i * 0.88f)) * 2, ry = fy - (0.1f + j * 0.8f);
      rd = fminf(rd, sqrtf(rx * rx + ry * ry));
    }
  float rivet = clamp01(1 - rd / 0.035f);
  if (kind == 0 || kind == 1) { /* vent: slats in the middle */
    float inx = sstep(0.2f, 0.25f, fx) * (1 - sstep(0.75f, 0.8f, fx)), iny = sstep(0.2f, 0.25f, fy) * (1 - sstep(0.75f, 0.8f, fy));
    float slat = sstep(0.3f, 0.5f, fabsf(fractf(fy * 7) - 0.5f) * 1.0f + 0.0f);
    float m = inx * iny;
    mixc(t, 0.02f, 0.025f, 0.035f, m * (1 - slat) * 0.9f);
    h -= m * (1 - slat) * 0.4f;
    ao -= m * (1 - slat) * 0.5f;
  } else if (kind == 2) { /* hatch: a recessed door with a handle */
    float inx = sstep(0.18f, 0.2f, fx) * (1 - sstep(0.8f, 0.82f, fx)), iny = sstep(0.12f, 0.15f, fy) * (1 - sstep(0.85f, 0.88f, fy));
    float m = inx * iny;
    mixc(t, 0.34f, 0.37f, 0.42f, m * 0.5f);
    h -= m * 0.15f;
    float hdl = clamp01(1 - sqrtf(((fx - 0.7f) * 2) * ((fx - 0.7f) * 2) + (fy - 0.5f) * (fy - 0.5f)) / 0.09f);
    mixc(t, 0.7f, 0.72f, 0.75f, hdl);
    h += hdl * 0.3f;
  } else if (kind == 3) { /* warning chevrons panel */
    float hz = fractf((fx * 2 + fy) * 3) < 0.5f ? 1.0f : 0.0f;
    float inn = sstep(0.1f, 0.15f, fy) * (1 - sstep(0.3f, 0.35f, fy));
    mixc(t, hz ? 0.9f : 0.06f, hz ? 0.55f : 0.06f, hz ? 0.08f : 0.06f, inn * 0.9f);
  }
  /* glowing seam strips (the material's emissive colours the white): low on the wall and again at 2.5 m */
  float glow = band(up, 0.45f, 0.035f, 0.02f) + 0.6f * band(up, 2.55f, 0.03f, 0.02f);
  glow = clamp01(glow) * (0.5f + 0.5f * sstep(0.2f, 0.4f, fbm(u, v, 12, 2, 276)) + 0.0f);
  float halo = clamp01(band(up, 0.45f, 0.1f, 0.1f) * 0.2f);
  mixc(t, 0.9f, 0.95f, 1.0f, clamp01(glow));
  emit(t, 1, 1, 1, clamp01(glow) + halo);
  mixc(t, 0.7f, 0.72f, 0.75f, rivet * 0.5f);
  mixc(t, 0.03f, 0.035f, 0.05f, seam * 0.8f);
  mixc(t, 0.45f, 0.48f, 0.52f, scr * 0.5f);
  t->h = h + rivet * 0.4f - seam * 0.4f + 0.02f * fine;
  t->rough = rough - rivet * 0.15f - scr * 0.1f;
  t->ao = ao - seam * 0.3f;
}

/* painted boards: 0.2 m horizontal planks, nailed to posts every metre, paint chipped back to grey wood, drip stains */
static void g_wall_wood(float u, float v, texel *t) {
  float up = u * 4, al = v * 4;
  float rowf = up / 0.2f;
  int row = (int)floorf(rowf);
  float inrow = fractf(rowf);
  float gd = fminf(inrow, 1 - inrow) * 0.2f;
  float seam = 1 - sstep(0.004f, 0.014f, gd);
  float tint = h01(row, 0, 281);
  float shift = h01(row, 1, 282) * 4;
  float jx = fabsf(fractf((al + shift) / 2.0f) - 0.5f) * 2;
  float butt = 1 - sstep(0.99f, 1.0f, jx);
  float fine = h01((int)(u * 512), (int)(v * 512), 283);
  float warp = fbm2(u, v, 4, 6, 3, 284 + (uint32_t)row) * 3;
  float grain = 0.5f + 0.5f * cw_sin((inrow * 6 + warp + fbm2(u, v, 4, 60, 3, 285) * 0.4f) * 6.2831853f);
  float chipn = fbm(u, v, 14, 4, 286);
  float edgechip = (1 - sstep(0.0f, 0.04f, gd)) * 0.5f;
  float chip = clamp01(sstep(0.58f, 0.7f, chipn) + edgechip * sstep(0.4f, 0.6f, chipn));
  float drip = sstep(0.55f, 0.75f, fbm2(u, v, 3, 40, 3, 287)) * 0.25f;
  float k = 0.92f + 0.08f * (tint - 0.5f) * 2.0f;
  rgb(t, 0.9f * k, 0.88f * k, 0.8f * k);
  mixc(t, 0.34f * (0.8f + 0.3f * grain), 0.28f * (0.8f + 0.3f * grain), 0.22f * (0.8f + 0.3f * grain), chip);
  mixc(t, 0.5f, 0.4f, 0.28f, drip * (1 - chip));
  /* nails along posts every metre */
  float nd = 9;
  float nx = (fractf(al) - 0.5f), ny = (inrow - 0.5f) * 0.2f;
  nd = sqrtf(nx * nx + ny * ny * 4);
  float nail = (0.2f > 0) ? clamp01(1 - fabsf(nx + 0.0f) / 0.02f) * clamp01(1 - fabsf(ny) / 0.02f) : 0;
  (void)nd;
  mixc(t, 0.3f, 0.3f, 0.32f, nail);
  mixc(t, 0.1f, 0.08f, 0.06f, seam * 0.9f + butt * 0.4f);
  t->h = 0.6f + 0.1f * grain * chip + 0.06f * fine - seam * 0.5f - butt * 0.3f - chip * 0.1f;
  t->rough = mixf(0.45f, 0.85f, chip) + 0.05f * fine;
  t->ao = 0.55f + 0.45f * (1 - seam) - butt * 0.2f;
}

/* tunnel lining: pale ribbed concrete, a lamp strip down the crown (u 0.5), reflector studs low on each side.
 * u runs round the arch (8 m per repeat), v along it (4 m per repeat). */
static void g_tunnel(float u, float v, texel *t) {
  float rib = fminf(fractf(v), 1 - fractf(v)) * 4;              /* metres to the nearest ring joint */
  float ringj = 1 - sstep(0.03f, 0.1f, rib);
  float n = fbm2(u, v, 8, 4, 5, 291), fine = h01((int)(u * 1024), (int)(v * 512), 292);
  float soot = sstep(0.4f, 0.8f, fbm2(u, v, 3, 6, 3, 293));
  float c = 0.5f + 0.09f * (n - 0.5f) + 0.03f * (fine - 0.5f) - 0.14f * soot;
  rgb(t, c, c * 0.975f, c * 0.92f);
  /* lamp strip: a 0.28 m wide slot in dashes of 2.8 m every 4 m */
  float dash = sstep(0.06f, 0.1f, fractf(v)) * (1 - sstep(0.76f, 0.8f, fractf(v)));
  float lamp = band(u, 0.5f, 0.018f, 0.01f) * dash;
  float stud = clamp01(1 - sqrtf(((u < 0.5f ? u : 1 - u) - 0.1f) * ((u < 0.5f ? u : 1 - u) - 0.1f) * 64.0f * 64.0f / 100.0f + (fractf(v * 4) - 0.5f) * (fractf(v * 4) - 0.5f) * 16.0f) / 0.12f);
  mixc(t, 1.0f, 0.95f, 0.8f, lamp);
  mixc(t, 0.95f, 0.5f, 0.1f, stud);
  mixc(t, 0.1f, 0.1f, 0.1f, ringj * 0.5f);
  mixc(t, 0.88f, 0.86f, 0.8f, band(u, 0.5f, 0.05f, 0.05f) * (1 - dash) * 0.12f);
  emit(t, 1.0f, 0.95f, 0.8f, lamp + band(u, 0.5f, 0.05f, 0.05f) * dash * 0.12f);
  emit(t, 1.0f, 0.5f, 0.1f, stud * 0.5f);
  t->h = 0.55f + 0.3f * n + 0.05f * fine - ringj * 0.5f + lamp * 0.2f;
  t->rough = 0.88f - 0.5f * lamp;
  t->ao = 0.9f + 0.1f * n - ringj * 0.4f;
}

/* ── ground and general: 256, tile freely ───────────────────────────── */

static void g_ground_sand(float u, float v, texel *t) {
  float wob = fbm(u, v, 3, 3, 301);
  float ripple = 0.5f + 0.5f * cw_sin((u * 12 + wob * 9 + 0.3f * fbm(u, v, 8, 2, 302)) * 6.2831853f);
  float n = fbm(u, v, 12, 4, 303), grain = h01((int)(u * 256), (int)(v * 256), 304);
  float dune = fbm(u, v, 2, 3, 305);
  float k = 0.84f + 0.14f * n + 0.04f * grain + 0.1f * (dune - 0.5f);
  float sh = 0.92f + 0.08f * ripple;
  rgb(t, 0.92f * k * sh, 0.76f * k * sh, 0.52f * k * sh);
  t->h = 0.55f * ripple + 0.2f * n + 0.05f * grain;
  t->rough = 0.93f;
  t->ao = 0.84f + 0.16f * ripple;
}

/* layered rock: strata bands, angular fractures, lichen specks */
static void g_ground_rock(float u, float v, texel *t) {
  float warp = fbm(u, v, 3, 3, 311);
  float strata = 0.5f + 0.5f * cw_sin((v * 8 + warp * 2.0f) * 6.2831853f);
  float d1, d2;
  uint32_t id;
  cells2(u + 0.02f * warp, v, 6, 6, 312, &d1, &d2, &id);
  float crack = 1 - sstep(0.0f, 0.07f, d2 - d1);
  float n = fbm(u, v, 10, 5, 313), fine = h01((int)(u * 256), (int)(v * 256), 314);
  float tint = (float)(id & 255) / 255.0f;
  float k = 0.42f + 0.12f * n + 0.08f * (tint - 0.5f) + 0.05f * strata;
  rgb(t, k * 1.02f, k * 0.97f, k * 0.9f);
  float lich = sstep(0.68f, 0.78f, fbm(u, v, 12, 3, 315)) * 0.6f;
  mixc(t, 0.55f, 0.56f, 0.28f, lich);
  mixc(t, 0.08f, 0.07f, 0.06f, crack * 0.7f);
  t->h = 0.5f + 0.25f * n + 0.15f * (1 - crack) + 0.05f * fine - crack * 0.4f;
  t->rough = 0.9f;
  t->ao = 0.85f - crack * 0.5f + 0.15f * n;
}

static void g_ground_grass(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 4, 321), n2 = fbm(u, v, 32, 3, 322), blade = h01((int)(u * 256), (int)(v * 256), 323);
  float clump = fbm(u, v, 16, 3, 324);
  float k = 0.74f + 0.38f * n1 + 0.16f * (blade - 0.5f) + 0.12f * (clump - 0.5f);
  rgb(t, 0.3f * k + 0.08f * n2, 0.58f * k, 0.15f * k);
  if (n1 > 0.64f) {
    float w = clamp01((n1 - 0.64f) * 6);
    mixc(t, 0.5f, 0.45f, 0.22f, w * 0.45f); /* dry patches */
  }
  float fl = h01((int)(u * 128), (int)(v * 128), 325);
  if (fl > 0.992f) mixc(t, 0.95f, 0.9f, 0.4f, 0.8f);
  t->h = 0.4f + 0.4f * blade * n2 + 0.2f * clump;
  t->rough = 0.92f;
  t->ao = 0.72f + 0.28f * blade;
}

static void g_ground_snow(float u, float v, texel *t) {
  float n = fbm(u, v, 4, 5, 331), sp = h01((int)(u * 256), (int)(v * 256), 332);
  float drift = 0.5f + 0.5f * cw_sin((v * 5 + fbm(u, v, 3, 3, 333) * 4) * 6.2831853f);
  float k = 0.9f + 0.1f * n - 0.04f * (1 - drift);
  rgb(t, 0.92f * k, 0.95f * k, 1.0f * k);
  if (sp > 0.992f) mixc(t, 1, 1, 1, 1);
  t->h = n * 0.6f + drift * 0.3f;
  t->rough = sp > 0.992f ? 0.15f : 0.72f;
  t->ao = 0.88f + 0.12f * n;
}

static void g_ground_mud(float u, float v, texel *t) {
  float n = fbm(u, v, 5, 5, 341), grit = h01((int)(u * 256), (int)(v * 256), 342);
  float pud = sstep(0.56f, 0.64f, fbm(u, v, 3, 4, 343));
  float d1, d2;
  uint32_t id;
  cells2(u, v, 8, 8, 344, &d1, &d2, &id);
  float crack = (1 - sstep(0.0f, 0.05f, d2 - d1)) * (1 - pud) * sstep(0.45f, 0.6f, fbm(u, v, 2, 2, 345));
  float k = 0.8f + 0.3f * n + 0.06f * grit;
  rgb(t, 0.34f * k, 0.24f * k, 0.15f * k);
  mixc(t, 0.16f, 0.12f, 0.09f, pud * 0.8f + crack * 0.6f);
  t->h = 0.55f + 0.3f * n - pud * 0.3f - crack * 0.3f;
  t->rough = mixf(0.82f, 0.08f, pud);
  t->ao = 0.8f + 0.2f * n - crack * 0.3f;
}

static void g_ground_ash(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 351), grit = h01((int)(u * 256), (int)(v * 256), 352);
  float k = 0.22f + 0.1f * n + 0.05f * (grit - 0.5f);
  rgb(t, k, k * 0.98f, k * 0.96f);
  if (grit > 0.985f) mixc(t, 0.7f, 0.68f, 0.64f, 0.8f);
  float ember = sstep(0.8f, 0.9f, fbm(u, v, 14, 3, 353));
  mixc(t, 0.4f, 0.16f, 0.07f, ember * 0.35f);
  t->h = 0.5f + 0.3f * n + 0.1f * grit;
  t->rough = 0.95f;
  t->ao = 0.8f + 0.2f * n;
}

/* worn steel plate: 1 m panels, rivets, brushed scratches, grime in the seams, orange rust blooms */
static void g_metal(float u, float v, texel *t) {
  float pu = fractf(u * 2), pv = fractf(v * 2);
  float edge = fminf(fminf(pu, 1 - pu), fminf(pv, 1 - pv));
  float seam = 1 - sstep(0.004f, 0.02f, edge);
  float n = fbm(u, v, 8, 4, 361);
  float brushed = fbm2(u, v, 3, 128, 3, 362);
  float scratch = (1 - sstep(0.0f, 0.04f, fabsf(fbm2(u, v, 64, 3, 2, 363) - 0.5f) * 2)) * 0.6f;
  float rd = 9;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      float dx = pu - (0.07f + i * 0.86f), dy = pv - (0.07f + j * 0.86f);
      rd = fminf(rd, sqrtf(dx * dx + dy * dy));
    }
  float rivet = clamp01(1 - rd / 0.035f);
  float rust = sstep(0.55f, 0.7f, fbm(u, v, 6, 4, 364)) * 0.8f + seam * 0.4f;
  float c = 0.46f + 0.1f * n + 0.04f * (brushed - 0.5f);
  rgb(t, c * 1.0f, c * 1.01f, c * 1.04f);
  mixc(t, 0.52f, 0.27f, 0.11f, clamp01(rust) * 0.65f);
  mixc(t, 0.72f, 0.74f, 0.78f, scratch * 0.25f + rivet * 0.5f);
  mixc(t, 0.08f, 0.08f, 0.08f, seam * 0.7f);
  t->h = 0.5f + 0.15f * brushed + 0.4f * rivet - seam * 0.4f + 0.05f * n;
  t->rough = 0.4f + 0.25f * n + 0.35f * clamp01(rust) - 0.2f * scratch;
  t->ao = 0.7f + 0.3f * (1 - seam) - 0.1f * rust;
}

static void g_rust(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 371), streak = fbm2(u, v, 3, 40, 3, 372), pit = h01((int)(u * 256), (int)(v * 256), 373);
  float k = 0.7f + 0.4f * n;
  rgb(t, 0.5f * k, 0.25f * k, 0.1f * k);
  mixc(t, 0.3f, 0.14f, 0.06f, sstep(0.5f, 0.75f, streak) * 0.6f);
  mixc(t, 0.7f, 0.42f, 0.18f, sstep(0.65f, 0.85f, n) * 0.5f);
  if (pit > 0.97f) mixc(t, 0.12f, 0.06f, 0.03f, 0.7f);
  t->h = 0.4f + 0.4f * n + 0.15f * pit;
  t->rough = 0.88f;
  t->ao = 0.7f + 0.3f * n;
}

/* kerb stripes: a stripe of colour A, then B, every metre of its 2 m repeat. The colours are the map's;
 * tex_kerb sets them, the generic kind below uses red and white. Edges are scuffed and chipped. */
static float KA[3] = { 0.9f, 0.2f, 0.15f }, KB[3] = { 0.95f, 0.95f, 0.95f };
static void g_stripes(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 4, 381), fine = h01((int)(u * 256), (int)(v * 256), 382);
  float s = fractf(v * 2 + 0.0f);
  float which = fractf(v) < 0.5f ? 0.0f : 1.0f;
  float edge = fminf(fractf(v * 2), 1 - fractf(v * 2)) * 1.0f; /* 0..0.5 of a stripe */
  float paintsep = sstep(0.0f, 0.012f, fminf(fractf(v), 1 - fractf(v)) * 0 + fminf(fabsf(fractf(v) - 0.5f), fminf(fractf(v), 1 - fractf(v))));
  (void)s;
  float a = which ? KB[0] : KA[0], b = which ? KB[1] : KA[1], c = which ? KB[2] : KA[2];
  float grime = 0.78f + 0.22f * n;
  rgb(t, a * grime, b * grime, c * grime);
  /* tyre scuffs: black smears on the raised top and at the stripe seams, chips showing concrete */
  float scuff = sstep(0.55f, 0.75f, fbm2(u, v, 3, 10, 3, 383));
  float chip = sstep(0.62f, 0.74f, fbm(u, v, 20, 3, 384));
  mixc(t, 0.1f, 0.1f, 0.1f, scuff * 0.45f);
  mixc(t, 0.6f, 0.58f, 0.54f, chip * 0.75f);
  float seam = (1 - paintsep);
  mixc(t, 0.5f, 0.48f, 0.45f, seam * 0.35f);
  /* a rounded profile: the top (u 0.5..1 on the left kerb, 0..0.5 on the right) is the surface; the face is shaded darker by light */
  float rounded = 0.5f + 0.5f * cw_sin(u * 6.2831853f * 1.0f);
  (void)edge;
  t->h = 0.5f + 0.25f * rounded + 0.08f * n + 0.04f * fine - chip * 0.3f;
  t->rough = 0.55f + 0.2f * n + 0.3f * chip - 0.15f * scuff;
  t->ao = 0.85f + 0.15f * n - seam * 0.1f;
}

/* boost chevrons: bright arrows pointing along +v on a dark navy ground, two per 6 m repeat, a border rail
 * each side. u 0..1 across the pad (about 11 m), v along (6 m). The arrows glow (emissive map). */
static void g_arrows(float u, float v, texel *t) {
  float xm = (u - 0.5f) * 11.0f;
  float k = 0.62f;                                 /* how far the arms sweep back, metres per metre */
  float tt = (v * 6.0f + k * fabsf(xm)) / 3.0f;    /* chevron coordinate, one per 3 m */
  float f = fractf(tt);
  float arm = sstep(0.0f, 0.04f, f) * (1 - sstep(0.4f, 0.44f, f));  /* arm 1.2 m thick along v */
  float inner = sstep(0.1f, 0.14f, f) * (1 - sstep(0.3f, 0.34f, f));
  float fade = 1 - sstep(4.6f, 5.2f, fabsf(xm));   /* tapers off before the rails */
  arm *= fade;
  float rail = band(u, 0.035f, 0.012f, 0.006f) + band(u, 0.965f, 0.012f, 0.006f);
  rail = clamp01(rail) * (0.5f + 0.5f * sstep(0.3f, 0.5f, fractf(v * 6.0f * 2.0f)));
  float n = fbm(u, v, 8, 3, 391), fine = h01((int)(u * 512), (int)(v * 512), 392);
  float hexx = fabsf(fractf(xm * 1.0f) - 0.5f), hexy = fabsf(fractf(v * 6.0f * 1.1547f + 0.5f * floorf(xm)) - 0.5f);
  float mesh = sstep(0.42f, 0.5f, fmaxf(hexx, hexy)) * 0.6f;
  rgb(t, 0.03f + 0.01f * n, 0.06f + 0.02f * n, 0.14f + 0.04f * n);
  t->b += mesh * 0.04f, t->g += mesh * 0.02f;
  mixc(t, 0.15f, 0.85f, 1.0f, arm);
  mixc(t, 0.9f, 1.0f, 1.0f, arm * inner);
  mixc(t, 0.15f, 0.85f, 1.0f, rail);
  /* arrow glow: strong on the arm, white hot at the core, a soft halo around the arm */
  float halo = fade * (sstep(-0.08f, 0.0f, f) * (1 - sstep(0.0f, 0.04f, f)) + sstep(0.4f, 0.44f, f) * (1 - sstep(0.44f, 0.54f, f))) * 0.25f;
  emit(t, 0.15f, 0.85f, 1.0f, arm * 0.85f + rail * 0.8f + halo);
  emit(t, 0.9f, 1.0f, 1.0f, arm * inner * 0.6f);
  t->h = 0.5f + arm * 0.1f + 0.05f * fine - mesh * 0.1f;
  t->rough = 0.35f - 0.2f * arm + 0.1f * n;
  t->ao = 0.95f;
}

static void g_noise(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 401), f = h01((int)(u * 256), (int)(v * 256), 402);
  float k = 0.6f + 0.3f * n + 0.08f * (f - 0.5f);
  rgb(t, k, k, k);
  t->h = n;
  t->rough = 0.8f;
  t->ao = 0.9f + 0.1f * n;
}

/* ── the table ──────────────────────────────────────────────────────── */

typedef struct kind_info {
  gen_fn gen;
  int w, h;
  float normal;     /* height to slope strength */
  const char *name;
} kind_info;

#define RD 1024, 512
#define WL 512, 512
#define GR 256, 256
static const kind_info INFO[TEX_KINDS] = {
  [TEX_ROAD_ASPHALT] = { g_road_asphalt, RD, 2.2f, "asphalt" },
  [TEX_ROAD_CONCRETE] = { g_road_concrete, RD, 2.4f, "concrete road" },
  [TEX_ROAD_DIRT] = { g_road_dirt, RD, 3.0f, "dirt" },
  [TEX_ROAD_SALT] = { g_road_salt, RD, 3.0f, "salt" },
  [TEX_ROAD_ICE] = { g_road_ice, RD, 1.6f, "ice" },
  [TEX_ROAD_METAL] = { g_road_metal, RD, 3.5f, "metal" },
  [TEX_ROAD_WOOD] = { g_road_wood, RD, 3.0f, "wood" },
  [TEX_ROAD_STONE] = { g_road_stone, RD, 3.5f, "stone road" },
  [TEX_ROAD_TECH] = { g_road_tech, RD, 2.5f, "tech road" },
  [TEX_ROAD_SAND] = { g_road_sand, RD, 2.5f, "sand road" },
  [TEX_ROAD_WET] = { g_road_wet, RD, 2.2f, "wet asphalt" },
  [TEX_ROAD_GLASS] = { g_road_glass, RD, 1.2f, "glass" },
  [TEX_WALL_CONCRETE] = { g_wall_concrete, WL, 3.0f, "concrete wall" },
  [TEX_WALL_HAZARD] = { g_wall_hazard, WL, 2.5f, "hazard wall" },
  [TEX_WALL_RUST] = { g_wall_rust, WL, 4.0f, "rust wall" },
  [TEX_WALL_BRICK] = { g_wall_brick, WL, 4.0f, "brick" },
  [TEX_WALL_STONE] = { g_wall_stone, WL, 4.0f, "stone wall" },
  [TEX_WALL_ICE] = { g_wall_ice, WL, 2.0f, "ice wall" },
  [TEX_WALL_TECH] = { g_wall_tech, WL, 3.5f, "tech wall" },
  [TEX_WALL_WOOD] = { g_wall_wood, WL, 3.0f, "wood wall" },
  [TEX_GROUND_SAND] = { g_ground_sand, GR, 2.5f, "sand" },
  [TEX_GROUND_ROCK] = { g_ground_rock, GR, 3.5f, "rock" },
  [TEX_GROUND_GRASS] = { g_ground_grass, GR, 2.0f, "grass" },
  [TEX_GROUND_SNOW] = { g_ground_snow, GR, 1.5f, "snow" },
  [TEX_GROUND_MUD] = { g_ground_mud, GR, 2.5f, "mud" },
  [TEX_GROUND_ASH] = { g_ground_ash, GR, 2.0f, "ash" },
  [TEX_METAL] = { g_metal, WL, 3.0f, "steel plate" },
  [TEX_RUST] = { g_rust, GR, 3.0f, "rust" },
  [TEX_PAINT_STRIPES] = { g_stripes, 256, 512, 2.5f, "kerb stripes" },
  [TEX_ARROWS] = { g_arrows, WL, 2.0f, "arrows" },
  [TEX_NOISE] = { g_noise, GR, 2.0f, "noise" },
  [TEX_TUNNEL] = { g_tunnel, RD, 2.5f, "tunnel" },
};

/* ── building: a band of rows per call, finished by one call ────────── */

typedef struct build { texel *tx; int row; } build;
static build BLD[TEX_KINDS];
static tex_set SETS[TEX_KINDS];
uint32_t tex_hash = 2166136261u;

static t3_texture *finish(int w, int h, const uint8_t *px, bool srgb) {
  t3_texture *t = t3_texture_new(w, h, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 16;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }
static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

/* texels -> the maps, into a set */
static void bake(tex_set *s, const texel *tx, int w, int h, float st) {
  size_t n = (size_t)w * h;
  uint8_t *a = malloc(n * 4), *nm = malloc(n * 4), *ro = malloc(n * 4), *ao = malloc(n * 4), *em = malloc(n * 4);
  bool glow = false;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const texel *t = &tx[y * w + x];
      size_t i = ((size_t)y * w + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = 255;
      float hx = tx[y * w + (x + 1) % w].h - tx[y * w + (x + w - 1) % w].h;
      float hy = tx[((y + 1) % h) * w + x].h - tx[((y + h - 1) % h) * w + x].h;
      float nx = -hx * st, ny = -hy * st, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough), ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
      em[i] = b8(t->er), em[i + 1] = b8(t->eg), em[i + 2] = b8(t->eb), em[i + 3] = 255;
      if (t->er + t->eg + t->eb > 0.02f) glow = true;
    }
  tex_hash = fnv(a, n * 4, tex_hash);
  tex_hash = fnv(nm, n * 4, tex_hash);
  tex_hash = fnv(ro, n * 4, tex_hash);
  s->map = finish(w, h, a, true);
  s->normal = finish(w, h, nm, false);
  s->rough = finish(w, h, ro, false);
  s->ao = finish(w, h, ao, false);
  s->emit = glow ? finish(w, h, em, true) : NULL;
  free(a), free(nm), free(ro), free(ao), free(em);
}

bool tex_warm(int kind) {
  if (kind < 0 || kind >= TEX_KINDS) return true;
  tex_set *s = &SETS[kind];
  if (s->map) return true;
  const kind_info *k = &INFO[kind];
  build *b = &BLD[kind];
  if (!b->tx) b->tx = calloc((size_t)k->w * k->h, sizeof(texel));
  if (b->row < k->h) {
    /* about 24k texels a call: a few milliseconds */
    int rows = 24576 / k->w;
    if (rows < 1) rows = 1;
    for (int r = 0; r < rows && b->row < k->h; r++, b->row++)
      for (int x = 0; x < k->w; x++) k->gen((x + 0.5f) / k->w, (b->row + 0.5f) / k->h, &b->tx[b->row * k->w + x]);
    return false;
  }
  bake(s, b->tx, k->w, k->h, k->normal);
  free(b->tx);
  *b = (build){ 0 };
  return true;
}

const tex_set *tex_get(int kind) {
  while (!tex_warm(kind)) {}
  return &SETS[kind];
}

/* The kerb set for a colour pair, built once per pair (up to 8 pairs). */
static struct { float a[3], b[3]; tex_set set; } KERBS[8];
static int nkerb;
const tex_set *tex_kerb(const float a[3], const float b[3]) {
  for (int i = 0; i < nkerb; i++)
    if (!memcmp(KERBS[i].a, a, sizeof KERBS[i].a) && !memcmp(KERBS[i].b, b, sizeof KERBS[i].b)) return &KERBS[i].set;
  if (nkerb == 8) return tex_get(TEX_PAINT_STRIPES);
  const kind_info *k = &INFO[TEX_PAINT_STRIPES];
  memcpy(KA, a, sizeof KA), memcpy(KB, b, sizeof KB);
  texel *tx = calloc((size_t)k->w * k->h, sizeof(texel));
  for (int y = 0; y < k->h; y++)
    for (int x = 0; x < k->w; x++) k->gen((x + 0.5f) / k->w, (y + 0.5f) / k->h, &tx[y * k->w + x]);
  memcpy(KERBS[nkerb].a, a, sizeof KERBS[0].a), memcpy(KERBS[nkerb].b, b, sizeof KERBS[0].b);
  bake(&KERBS[nkerb].set, tx, k->w, k->h, k->normal);
  free(tx);
  KA[0] = 0.9f, KA[1] = 0.2f, KA[2] = 0.15f, KB[0] = KB[1] = KB[2] = 0.95f;
  return &KERBS[nkerb++].set;
}

/* a set at another repeat: the texture keeps the repeat, so it is a copy, made once per (kind, repeat) and shared */
typedef struct rep_set { int kind; float ru, rv; t3_texture *t[5]; } rep_set;
static rep_set REP[64];
static int nrep;

static void dress(t3_material *m, const tex_set *s, float ru, float rv, float normal_scale, float ao_intensity, int key) {
  t3_texture *ts[5] = { s->map, s->normal, s->rough, s->ao, s->emit };
  if (ru != 1 || rv != 1) {
    rep_set *r = NULL;
    for (int k = 0; k < nrep && !r; k++)
      if (REP[k].kind == key && REP[k].ru == ru && REP[k].rv == rv) r = &REP[k];
    if (!r && nrep < (int)(sizeof REP / sizeof REP[0])) {
      r = &REP[nrep++];
      *r = (rep_set){ key, ru, rv, { 0 } };
      for (int i = 0; i < 5; i++) {
        if (!ts[i]) continue;
        r->t[i] = finish(ts[i]->width, ts[i]->height, ts[i]->pixels, i == 0 || i == 4);
        r->t[i]->repeat = (t3_vec2){ ru, rv };
      }
    }
    if (r)
      for (int i = 0; i < 5; i++) ts[i] = r->t[i];
  }
  static const t3_map_slot SLOTS[5] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP, T3_EMISSIVE_MAP };
  for (int i = 0; i < 5; i++)
    if (ts[i]) t3_material_set_texture(m, SLOTS[i], ts[i]);
  if (ts[4]) m->emissive = (t3_color){ 1, 1, 1 }, m->emissive_intensity = 1;
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->ao_map_intensity = ao_intensity;
}

void tex_apply(t3_material *m, int kind, float repeat_u, float repeat_v, float normal_scale, float ao_intensity) {
  dress(m, tex_get(kind), repeat_u, repeat_v, normal_scale, ao_intensity, kind);
}
void tex_apply_kerb(t3_material *m, const float a[3], const float b[3], float repeat_u, float repeat_v, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_kerb(a, b);
  int key = (int)(s - &KERBS[0].set) / 1; /* unique per pair: the address difference over the element size */
  key = TEX_KINDS + (int)(((const char *)s - (const char *)&KERBS[0].set) / (long)sizeof KERBS[0]);
  dress(m, s, repeat_u, repeat_v, normal_scale, ao_intensity, key);
}

void tex_scroll(int kind, float du, float dv) {
  const tex_set *s = &SETS[kind];
  t3_texture *ts[5] = { s->map, s->normal, s->rough, s->ao, s->emit };
  for (int i = 0; i < 5; i++)
    if (ts[i]) ts[i]->offset = (t3_vec2){ du, dv };
}

void tex_free_all(void) {
  for (int k = 0; k < nrep; k++)
    for (int i = 0; i < 5; i++)
      if (REP[k].t[i]) t3_release(REP[k].t[i]);
  nrep = 0;
  for (int k = 0; k < TEX_KINDS; k++) {
    tex_set *s = &SETS[k];
    if (s->map) {
      t3_release(s->map), t3_release(s->normal), t3_release(s->rough), t3_release(s->ao);
      if (s->emit) t3_release(s->emit);
    }
    memset(s, 0, sizeof *s);
    free(BLD[k].tx);
    BLD[k] = (build){ 0 };
  }
  for (int i = 0; i < nkerb; i++) {
    tex_set *s = &KERBS[i].set;
    t3_release(s->map), t3_release(s->normal), t3_release(s->rough), t3_release(s->ao);
    if (s->emit) t3_release(s->emit);
  }
  nkerb = 0;
  memset(KERBS, 0, sizeof KERBS);
}
