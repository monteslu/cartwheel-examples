/* bomberdude textures: every surface gets a full PBR set made here at load
 * time (no image files): albedo (sRGB), a tangent-space normal map from a
 * height field, roughness and ambient occlusion. All tile without seams
 * (periodic noise), are mipmapped with 8x anisotropy, and are made once, on
 * first use, and shared. Integer hashing and cartwheel/math.h only, so every
 * host makes the same texels (debug field tex.hash). */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

#define N 256

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

/* periodic value noise: period p cells across the texture */
static float vnoise(float x, float y, int p, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = smooth(x - x0), fy = smooth(y - y0);
  float a = h01(wrapi(x0, p), wrapi(y0, p), seed), b = h01(wrapi(x0 + 1, p), wrapi(y0, p), seed);
  float c = h01(wrapi(x0, p), wrapi(y0 + 1, p), seed), d = h01(wrapi(x0 + 1, p), wrapi(y0 + 1, p), seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float u, float v, int p, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) {
    s += amp * vnoise(u * p, v * p, p, seed + (uint32_t)o * 101u);
    norm += amp;
    amp *= 0.5f;
    p *= 2;
  }
  return s / norm;
}
static void cells(float u, float v, int p, uint32_t seed, float *d1, float *d2, uint32_t *id) {
  float x = u * p, y = v * p;
  int cx = (int)floorf(x), cy = (int)floorf(y);
  *d1 = *d2 = 9;
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      int gx = cx + i, gy = cy + j;
      float px = gx + h01(wrapi(gx, p), wrapi(gy, p), seed), py = gy + h01(wrapi(gx, p), wrapi(gy, p), seed + 7);
      float d = sqrtf((px - x) * (px - x) + (py - y) * (py - y));
      if (d < *d1) {
        *d2 = *d1;
        *d1 = d;
        *id = hash2(wrapi(gx, p), wrapi(gy, p), seed + 13);
      } else if (d < *d2) *d2 = d;
    }
}

typedef struct texel { float r, g, b, h, rough, ao; } texel;
typedef void (*gen_fn)(float u, float v, texel *t);
static void rgb(texel *t, float r, float g, float b) { t->r = r, t->g = g, t->b = b; }

/* bricks of `rows` courses, half offset, mortar `m` wide (uv units of a brick) */
static float brick(float u, float v, int cols, int rows, float m, uint32_t *id, float *bu, float *bv) {
  float y = v * rows;
  int row = (int)floorf(y);
  float x = u * cols + (row & 1) * 0.5f;
  int c = (int)floorf(x);
  *bu = x - c;
  *bv = y - row;
  *id = hash2(wrapi(c, cols * 2), row, 5);
  float ex = fminf(*bu, 1 - *bu) * 2, ey = fminf(*bv, 1 - *bv) * 2; /* 0 at the joint, 1 mid-brick */
  return clamp01(fminf(ex / (m * 2), ey / (m * 2 * cols / rows)));
}

/* ── generators: u, v in 0..1 -> colour (screen sRGB), height, roughness, ao ── */

static void g_grass(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 4, 41), n2 = fbm(u, v, 32, 3, 42), blade = h01((int)(u * N), (int)(v * N), 43);
  float k = 0.78f + 0.35f * n1 + 0.15f * (blade - 0.5f);
  rgb(t, 0.34f * k + 0.08f * n2, 0.6f * k, 0.18f * k);
  if (n1 > 0.66f) {
    float w = clamp01((n1 - 0.66f) * 6);
    t->r = mixf(t->r, 0.6f, w * 0.5f);
    t->g = mixf(t->g, 0.64f, w * 0.3f);
  }
  t->h = 0.4f + 0.4f * blade * n2;
  t->rough = 0.92f;
  t->ao = 0.75f + 0.25f * blade;
}

static void g_brick(float u, float v, texel *t) {
  uint32_t id;
  float bu, bv;
  float e = brick(u, v, 2, 4, 0.06f, &id, &bu, &bv);
  float n = fbm(u, v, 8, 4, 22), tint = (float)(id & 255) / 255.0f, chip = fbm(u, v, 16, 3, 23);
  float c = 0.62f + 0.12f * tint + 0.15f * (n - 0.5f);
  float mortar = 1 - smooth(e);
  rgb(t, mixf(c * 0.98f, 0.72f, mortar), mixf(c * 0.93f, 0.7f, mortar), mixf(c * 0.86f, 0.66f, mortar));
  t->h = smooth(e) * (0.7f + 0.3f * chip);
  t->rough = 0.82f + 0.12f * mortar;
  t->ao = 0.55f + 0.45f * smooth(e);
}

/* crate: planks across, a frame round the edge, nail heads */
static void g_crate(float u, float v, texel *t) {
  float edge = fminf(fminf(u, 1 - u), fminf(v, 1 - v));
  bool frame = edge < 0.12f;
  float grain_v = frame && (u < 0.12f || u > 0.88f) ? u : v;
  float grain_u = frame && (u < 0.12f || u > 0.88f) ? v : u;
  float plank = frame ? 0 : floorf(v * 4);
  float warp = fbm(grain_u, grain_v, 3, 3, 11 + (uint32_t)plank) * 2;
  float ring = grain_v * 18 + warp;
  float line = cw_pow(1 - fabsf(fractf(ring) - 0.5f) * 2, 5);
  float fine = fbm(u, v, 32, 3, 13);
  float shade = 0.85f + 0.15f * h01((int)plank, 0, 14);
  float seam = frame ? 1 : clamp01(fminf(fractf(v * 4), 1 - fractf(v * 4)) * 40);
  float fseam = clamp01(fabsf(edge - 0.12f) * 60);
  float k = shade * (0.9f + 0.12f * fine) * (frame ? 0.82f : 1);
  rgb(t, mixf(0.86f, 0.6f, line * 0.6f) * k, mixf(0.62f, 0.4f, line * 0.6f) * k, mixf(0.36f, 0.22f, line * 0.6f) * k);
  /* nails at the frame's corners and mid-sides */
  float nd = 9;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      if (i == 1 && j == 1) continue;
      float nx = 0.06f + i * 0.44f, ny = 0.06f + j * 0.44f, dx = u - nx, dy = v - ny;
      nd = fminf(nd, sqrtf(dx * dx + dy * dy));
    }
  float nail = clamp01(1 - nd / 0.025f);
  t->r = mixf(t->r, 0.45f, nail);
  t->g = mixf(t->g, 0.44f, nail);
  t->b = mixf(t->b, 0.42f, nail);
  t->h = 0.5f - line * 0.15f + fine * 0.1f - (1 - seam) * 0.35f - (1 - fseam) * 0.25f + (frame ? 0.12f : 0) + nail * 0.2f;
  t->rough = 0.68f + 0.18f * line - nail * 0.35f;
  t->ao = 1 - (1 - seam) * 0.4f - (1 - fseam) * 0.3f;
}

static void g_ice(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 4, 31, &d1, &d2, &id);
  float crack = 1 - clamp01((d2 - d1) * 14);
  float frost = fbm(u, v, 8, 4, 32), deep = fbm(u, v, 2, 3, 33);
  rgb(t, 0.72f + 0.2f * frost - 0.1f * deep, 0.86f + 0.1f * frost, 0.98f);
  t->r = mixf(t->r, 0.95f, crack * 0.6f);
  t->g = mixf(t->g, 0.98f, crack * 0.6f);
  t->h = 0.5f + 0.1f * frost - 0.3f * crack;
  t->rough = 0.04f + 0.25f * frost * frost + 0.3f * crack;
  t->ao = 1 - 0.15f * crack;
}

static void g_snow(float u, float v, texel *t) {
  float n = fbm(u, v, 4, 5, 34), sp = h01((int)(u * N), (int)(v * N), 35);
  float k = 0.9f + 0.1f * n;
  rgb(t, 0.9f * k, 0.94f * k, 1.0f * k);
  t->h = n;
  t->rough = 0.75f - 0.4f * (sp > 0.985f);
  t->ao = 0.85f + 0.15f * n;
}

/* forge plates: riveted steel panels, heat-tinted */
static void g_metal(float u, float v, texel *t) {
  float pu = fractf(u * 2), pv = fractf(v * 2);
  float edge = fminf(fminf(pu, 1 - pu), fminf(pv, 1 - pv));
  float seam = clamp01(edge * 40);
  float n = fbm(u, v, 8, 4, 51), scratch = cw_pow(fabsf(cw_sin((u * 7 + fbm(u, v, 4, 2, 52) * 3) * 6.2831853f * 4)), 80);
  float rd = 9;
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      float dx = pu - (0.08f + i * 0.84f), dy = pv - (0.08f + j * 0.84f);
      rd = fminf(rd, sqrtf(dx * dx + dy * dy));
    }
  float rivet = clamp01(1 - rd / 0.045f);
  float c = 0.42f + 0.12f * n;
  rgb(t, c * 1.02f + 0.05f * n, c * 0.96f, c * 0.92f);
  t->h = 0.5f + 0.4f * rivet - (1 - seam) * 0.4f + 0.05f * n;
  t->rough = 0.42f + 0.25f * n - 0.2f * scratch + (1 - seam) * 0.3f;
  t->ao = 0.6f + 0.4f * seam;
}

static void g_basalt(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 6, 61, &d1, &d2, &id);
  float e = clamp01((d2 - d1) * 6), n = fbm(u, v, 8, 4, 62);
  float c = 0.2f + 0.08f * (float)(id & 255) / 255.0f + 0.06f * n;
  rgb(t, c * 1.05f, c * 0.95f, c * 0.9f);
  /* the cracks glow: lava under the stones (emissive takes this map too) */
  float lava = 1 - smooth(e);
  t->r = mixf(t->r, 1.0f, lava * 0.9f);
  t->g = mixf(t->g, 0.35f, lava * 0.9f);
  t->b = mixf(t->b, 0.05f, lava * 0.9f);
  t->h = smooth(e) * (0.6f + 0.4f * n);
  t->rough = 0.85f - 0.3f * lava;
  t->ao = 0.6f + 0.4f * smooth(e);
}

/* temple floor: big worn flagstones, moss in the joints */
static void g_moss(float u, float v, texel *t) {
  uint32_t id;
  float bu, bv;
  float e = brick(u, v, 2, 2, 0.04f, &id, &bu, &bv);
  float n = fbm(u, v, 6, 5, 71), moss = fbm(u, v, 12, 3, 72);
  float c = 0.55f + 0.12f * (float)(id & 255) / 255.0f + 0.12f * (n - 0.5f);
  rgb(t, c * 0.95f, c * 0.97f, c * 0.86f);
  float m = clamp01((1 - smooth(e)) * 1.5f + (moss - 0.62f) * 4);
  t->r = mixf(t->r, 0.26f, m);
  t->g = mixf(t->g, 0.42f, m);
  t->b = mixf(t->b, 0.14f, m);
  t->h = smooth(e) * (0.6f + 0.4f * n) + m * 0.1f;
  t->rough = 0.88f;
  t->ao = 0.55f + 0.45f * smooth(e);
}

/* temple walls: carved blocks with a glyph band */
static void g_jungle(float u, float v, texel *t) {
  uint32_t id;
  float bu, bv;
  float e = brick(u, v, 1, 3, 0.05f, &id, &bu, &bv);
  float n = fbm(u, v, 8, 4, 73);
  float glyph = 0;
  if (((id >> 3) & 3) == 0) { /* a carved spiral on some blocks */
    float dx = bu - 0.5f, dy = (bv - 0.5f) * 0.4f, r = sqrtf(dx * dx + dy * dy), a = cw_atan2(dy, dx);
    glyph = clamp01(1 - fabsf(fractf(r * 9 - a / 6.2831853f) - 0.5f) * 6) * (r < 0.3f);
  }
  float c = 0.58f + 0.1f * (float)(id & 255) / 255.0f + 0.14f * (n - 0.5f);
  rgb(t, c * 0.92f, c * 0.95f, c * 0.8f);
  float moss = clamp01((fbm(u, v, 6, 3, 74) - 0.55f) * 3 + (1 - bv) * 0.3f);
  t->r = mixf(t->r, 0.3f, moss * 0.8f);
  t->g = mixf(t->g, 0.48f, moss * 0.8f);
  t->b = mixf(t->b, 0.18f, moss * 0.8f);
  t->h = smooth(e) * 0.8f - glyph * 0.25f + n * 0.1f;
  t->rough = 0.85f;
  t->ao = 0.5f + 0.5f * smooth(e) - glyph * 0.2f;
}

/* candy: a pastel swirl with sugar sparkle (the material tints it) */
static void g_candy(float u, float v, texel *t) {
  float s = fractf((u + v) * 3 + fbm(u, v, 2, 2, 81) * 0.3f);
  float stripe = smooth(clamp01(fabsf(s - 0.5f) * 4 - 0.6f));
  float sugar = h01((int)(u * N), (int)(v * N), 82) > 0.97f;
  rgb(t, mixf(1, 0.98f, stripe), mixf(1, 0.55f, stripe), mixf(1, 0.72f, stripe));
  t->h = 0.5f + 0.2f * stripe + 0.1f * sugar;
  t->rough = 0.3f - 0.15f * sugar;
  t->ao = 1;
}

static void g_frosting(float u, float v, texel *t) {
  float n = fbm(u, v, 4, 4, 83), drip = fbm(u, v, 8, 3, 84);
  float sp = h01((int)(u * N), (int)(v * N), 85);
  rgb(t, 1, 0.96f, 0.98f);
  /* sprinkles */
  if (sp > 0.992f) {
    int k = (int)(sp * 10000) % 5;
    static const float S[5][3] = { { 1, 0.3f, 0.4f }, { 0.3f, 0.7f, 1 }, { 1, 0.9f, 0.2f }, { 0.4f, 0.9f, 0.4f }, { 0.8f, 0.4f, 1 } };
    rgb(t, S[k][0], S[k][1], S[k][2]);
  }
  t->h = 0.5f * n + 0.3f * drip + (sp > 0.992f ? 0.3f : 0);
  t->rough = 0.45f;
  t->ao = 0.85f + 0.15f * n;
}

static void g_asphalt(float u, float v, texel *t) {
  float n = fbm(u, v, 16, 4, 91), grit = h01((int)(u * N), (int)(v * N), 92);
  float wet = clamp01((fbm(u, v, 3, 3, 93) - 0.5f) * 4);
  float c = 0.12f + 0.06f * n + 0.05f * (grit - 0.5f);
  rgb(t, c, c, c * 1.08f);
  t->h = 0.4f * n + 0.3f * grit;
  t->rough = mixf(0.85f, 0.12f, wet); /* puddles: the neon reflects in them */
  t->ao = 0.8f + 0.2f * n;
}

/* neon city walls: sci-fi panels with vents */
static void g_panel(float u, float v, texel *t) {
  float pu = fractf(u * 2), pv = fractf(v * 3);
  float edge = fminf(fminf(pu, 1 - pu), fminf(pv, 1 - pv) * 1.5f);
  float seam = clamp01(edge * 30);
  float vent = (pv > 0.3f && pv < 0.7f && pu > 0.25f && pu < 0.75f) ? clamp01(fabsf(fractf(pv * 10) - 0.5f) * 6 - 1) : 0;
  float n = fbm(u, v, 8, 3, 94);
  float c = 0.2f + 0.05f * n;
  rgb(t, c * 0.95f, c, c * 1.15f);
  t->h = 0.5f - (1 - seam) * 0.4f - vent * 0.3f;
  t->rough = 0.35f + 0.2f * n;
  t->ao = 0.55f + 0.45f * seam - vent * 0.3f;
}

static void g_deck(float u, float v, texel *t) {
  float row = floorf(v * 5), x = u * 1 + h01((int)row, 0, 101) * 3;
  float seg = fractf(x);
  float warp = fbm(u, v, 4, 3, 102 + (uint32_t)row) * 2;
  float line = cw_pow(1 - fabsf(fractf(u * 6 + warp) - 0.5f) * 2, 6);
  float seam = clamp01(fminf(fractf(v * 5), 1 - fractf(v * 5)) * 50) * clamp01(fminf(seg, 1 - seg) * 200);
  float k = 0.8f + 0.25f * h01((int)row, (int)floorf(x), 103) + 0.1f * fbm(u, v, 32, 2, 104);
  rgb(t, mixf(0.66f, 0.48f, line) * k, mixf(0.48f, 0.33f, line) * k, mixf(0.32f, 0.2f, line) * k);
  t->h = 0.55f - line * 0.1f - (1 - seam) * 0.45f;
  t->rough = 0.72f;
  t->ao = 0.55f + 0.45f * seam;
}

static void g_hull(float u, float v, texel *t) {
  float rows = floorf(v * 6), seam = clamp01(fminf(fractf(v * 6), 1 - fractf(v * 6)) * 30);
  float n = fbm(u, v, 8, 3, 105 + (uint32_t)rows);
  float k = 0.7f + 0.2f * n;
  rgb(t, 0.42f * k, 0.25f * k, 0.15f * k);
  t->h = 0.5f + 0.1f * n - (1 - seam) * 0.4f;
  t->rough = 0.6f;
  t->ao = 0.6f + 0.4f * seam;
}

static void g_sand(float u, float v, texel *t) {
  float ripple = cw_sin((v * 14 + fbm(u, v, 3, 3, 111) * 2.5f) * 6.2831853f) * 0.5f + 0.5f;
  float n = fbm(u, v, 16, 3, 112), grain = h01((int)(u * N), (int)(v * N), 113);
  float k = 0.85f + 0.12f * n + 0.05f * grain;
  rgb(t, 0.93f * k, 0.78f * k, 0.55f * k);
  t->h = 0.6f * ripple + 0.2f * n;
  t->rough = 0.9f;
  t->ao = 0.8f + 0.2f * ripple;
}

static void g_sandstone(float u, float v, texel *t) {
  uint32_t id;
  float bu, bv;
  float e = brick(u, v, 2, 3, 0.05f, &id, &bu, &bv);
  float strata = cw_sin((v * 30 + fbm(u, v, 4, 3, 121) * 3) * 6.2831853f) * 0.5f + 0.5f;
  float n = fbm(u, v, 8, 4, 122);
  float c = 0.75f + 0.1f * (float)(id & 255) / 255.0f + 0.1f * (n - 0.5f);
  rgb(t, c * 0.98f, c * 0.78f + 0.04f * strata, c * 0.55f);
  float worn = clamp01((fbm(u, v, 4, 3, 123) - 0.5f) * 3);
  t->h = smooth(e) * (0.7f - 0.3f * worn) + 0.1f * strata;
  t->rough = 0.88f;
  t->ao = 0.55f + 0.45f * smooth(e);
}

static void g_fur(float u, float v, texel *t) {
  float strand = fbm(u, v, 64, 2, 131) * 0.6f + fbm(u, v, 8, 3, 132) * 0.4f;
  float k = 0.8f + 0.28f * strand;
  rgb(t, k, k, k);
  t->h = strand;
  t->rough = 0.85f;
  t->ao = 0.75f + 0.25f * strand;
}

static void g_rubber(float u, float v, texel *t) {
  /* a belt: chevrons along v (the belt's travel; the material scrolls them) */
  float c = fractf(v * 4 + fabsf(u - 0.5f) * 1.2f);
  float chev = smooth(clamp01((0.25f - fabsf(c - 0.5f)) * 12));
  float edge = clamp01(fminf(u, 1 - u) * 14);
  float n = fbm(u, v, 16, 3, 141);
  float k = 0.1f + 0.04f * n;
  rgb(t, mixf(k, 0.95f, chev * 0.75f * edge), mixf(k, 0.8f, chev * 0.75f * edge), mixf(k * 1.1f, 0.2f, chev * 0.75f * edge));
  t->h = 0.4f + 0.3f * chev - (1 - edge) * 0.3f;
  t->rough = 0.7f - 0.3f * chev;
  t->ao = 0.7f + 0.3f * edge;
}

static void g_leaf(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 10, 151, &d1, &d2, &id);
  float leaf = clamp01(1 - d1 * 1.6f), n = fbm(u, v, 4, 3, 152), tint = (float)(id & 255) / 255.0f;
  float k = 0.6f + 0.5f * leaf + 0.2f * n;
  rgb(t, (0.2f + 0.12f * tint) * k, (0.5f + 0.1f * tint) * k, 0.16f * k);
  t->h = leaf;
  t->rough = 0.65f;
  t->ao = 0.45f + 0.55f * leaf;
}

static void g_bark(float u, float v, texel *t) {
  float ridge = fbm(u, v, 6, 3, 161);
  float s = u * 9 + fbm(u, v, 4, 3, 162) * 2;
  float fl = fabsf(s - floorf(s) - 0.5f) * 2;
  float k = 0.6f + 0.4f * ridge;
  rgb(t, 0.42f * k, 0.3f * k, 0.2f * k);
  t->h = fl * 0.7f + ridge * 0.3f;
  t->rough = 0.9f;
  t->ao = 0.5f + 0.5f * fl;
}

static const gen_fn GEN[TEX_KINDS] = { g_grass, g_brick, g_crate, g_ice, g_snow, g_metal, g_basalt, g_moss, g_jungle, g_candy, g_frosting,
                                       g_asphalt, g_panel, g_deck, g_hull, g_sand, g_sandstone, g_fur, g_rubber, g_leaf, g_bark };
static const float NORMAL_STRENGTH[TEX_KINDS] = { 2, 4, 3, 1.2f, 1.5f, 3, 4, 3.5f, 4, 1.5f, 2, 2, 3, 3, 3, 2.5f, 4, 2, 3, 3, 5 };

static tex_set SETS[TEX_KINDS];
uint32_t tex_hash = 2166136261u;

static t3_texture *finish(const uint8_t *px, bool srgb) {
  t3_texture *t = t3_texture_new(N, N, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }
static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

const tex_set *tex_get(int kind) {
  tex_set *s = &SETS[kind];
  if (s->map) return s;
  texel *tx = malloc(sizeof *tx * N * N);
  uint8_t *a = malloc(N * N * 4), *nm = malloc(N * N * 4), *ro = malloc(N * N * 4), *ao = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      texel *t = &tx[y * N + x];
      memset(t, 0, sizeof *t);
      GEN[kind]((x + 0.5f) / N, (y + 0.5f) / N, t);
    }
  float st = NORMAL_STRENGTH[kind];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const texel *t = &tx[y * N + x];
      int i = (y * N + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = 255;
      float hx = tx[y * N + (x + 1) % N].h - tx[y * N + (x + N - 1) % N].h;
      float hy = tx[((y + 1) % N) * N + x].h - tx[((y + N - 1) % N) * N + x].h;
      float nx = -hx * st, ny = -hy * st, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough), ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
    }
  tex_hash = fnv(a, N * N * 4, tex_hash);
  tex_hash = fnv(nm, N * N * 4, tex_hash);
  tex_hash = fnv(ro, N * N * 4, tex_hash);
  s->map = finish(a, true);
  s->normal = finish(nm, false);
  s->rough = finish(ro, false);
  s->ao = finish(ao, false);
  free(tx);
  free(a);
  free(nm);
  free(ro);
  free(ao);
  return s;
}

void tex_apply(t3_material *m, int kind, float repeat, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_get(kind);
  if (repeat != 1) { /* three.js keeps the repeat on the texture: a repeated copy */
    t3_texture *ts[4] = { s->map, s->normal, s->rough, s->ao };
    t3_map_slot slots[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
    for (int i = 0; i < 4; i++) {
      t3_texture *c = finish(ts[i]->pixels, i == 0);
      c->repeat = (t3_vec2){ repeat, repeat };
      t3_material_set_texture(m, slots[i], c);
      t3_release(c);
    }
  } else {
    t3_material_set_texture(m, T3_MAP, s->map);
    t3_material_set_texture(m, T3_NORMAL_MAP, s->normal);
    t3_material_set_texture(m, T3_ROUGHNESS_MAP, s->rough);
    t3_material_set_texture(m, T3_AO_MAP, s->ao);
  }
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->ao_map_intensity = ao_intensity;
}

void tex_free_all(void) {
  for (int k = 0; k < TEX_KINDS; k++) {
    tex_set *s = &SETS[k];
    if (!s->map) continue;
    t3_release(s->map);
    t3_release(s->normal);
    t3_release(s->rough);
    t3_release(s->ao);
    memset(s, 0, sizeof *s);
  }
}

t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

/* RoundedBoxGeometry's construction: a segmented box whose vertices are
 * pulled onto a cuboid with rounded edges (radius r), normals away from the
 * inner box so the bevels shade smoothly; the box's per-face uvs stay */
t3_geometry *geo_rounded_box(float w, float h, float d, float r, int seg) {
  t3_geometry *g = t3_box_geometry_new(w, h, d, seg, seg, seg);
  t3_attribute *pos = g->attributes[T3_ATTR_POSITION], *nor = g->attributes[T3_ATTR_NORMAL];
  float *p = pos->array, *n = nor->array;
  float hx = w * 0.5f - r, hy = h * 0.5f - r, hz = d * 0.5f - r;
  for (int i = 0; i < pos->count; i++) {
    float x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
    float ix = cw_clampf(x, -hx, hx), iy = cw_clampf(y, -hy, hy), iz = cw_clampf(z, -hz, hz);
    float dx = x - ix, dy = y - iy, dz = z - iz, l = sqrtf(dx * dx + dy * dy + dz * dz);
    if (l < 1e-6f) continue;
    dx /= l, dy /= l, dz /= l;
    p[i * 3] = ix + dx * r, p[i * 3 + 1] = iy + dy * r, p[i * 3 + 2] = iz + dz * r;
    n[i * 3] = dx, n[i * 3 + 1] = dy, n[i * 3 + 2] = dz;
  }
  pos->version++;
  nor->version++;
  g->has_bounding_sphere = g->has_bounding_box = false;
  return g;
}
