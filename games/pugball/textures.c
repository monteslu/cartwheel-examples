/* pugball textures: every surface gets a full PBR set made here at load
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

static void g_wood(float u, float v, texel *t) {
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


static void g_sand(float u, float v, texel *t) {
  float ripple = cw_sin((v * 14 + fbm(u, v, 3, 3, 111) * 2.5f) * 6.2831853f) * 0.5f + 0.5f;
  float n = fbm(u, v, 16, 3, 112), grain = h01((int)(u * N), (int)(v * N), 113);
  float k = 0.85f + 0.12f * n + 0.05f * grain;
  rgb(t, 0.93f * k, 0.78f * k, 0.55f * k);
  t->h = 0.6f * ripple + 0.2f * n;
  t->rough = 0.9f;
  t->ao = 0.8f + 0.2f * ripple;
}


static void g_fur(float u, float v, texel *t) {
  float strand = fbm(u, v, 64, 2, 131) * 0.6f + fbm(u, v, 8, 3, 132) * 0.4f;
  float k = 0.8f + 0.28f * strand;
  rgb(t, k, k, k);
  t->h = strand;
  t->rough = 0.85f;
  t->ao = 0.75f + 0.25f * strand;
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

/* the boards round the pitch: quilted vinyl padding, diamond stitching */
static void g_pad(float u, float v, texel *t) {
  float a = fractf((u + v) * 4), b = fractf((u - v) * 4);
  float seam = fminf(fminf(a, 1 - a), fminf(b, 1 - b));
  float puff = clamp01(seam * 9);
  float n = fbm(u, v, 16, 3, 171);
  float k = 0.82f + 0.12f * puff + 0.05f * n;
  rgb(t, k, k, k);
  t->h = smooth(puff) * 0.8f + n * 0.05f;
  t->rough = 0.42f - 0.12f * puff + 0.1f * n;
  t->ao = 0.55f + 0.45f * puff;
}

/* tabby: bands wrapping the cat (u runs round its middle), a cream belly
 * and chin toward v = 1, ticked fur all over (the material tints it) */
static void g_tabby(float u, float v, texel *t) {
  float warp = fbm(u, v, 4, 3, 181) * 0.9f;
  float band = cw_sin((u * 9 + warp + 0.4f * cw_sin(v * 6.2831853f)) * 6.2831853f);
  float stripe = smooth(clamp01(band * 1.6f - 0.25f)) * clamp01(1.4f - v * 1.5f);
  float hair = fbm(u, v, 64, 2, 182);
  float belly = smooth(clamp01((v - 0.62f) * 4));
  float k = 0.9f + 0.18f * hair;
  rgb(t, mixf(mixf(1.0f, 0.62f, stripe), 1.0f, belly) * k, mixf(mixf(0.62f, 0.3f, stripe), 0.92f, belly) * k,
      mixf(mixf(0.3f, 0.12f, stripe), 0.8f, belly) * k);
  t->h = hair;
  t->rough = 0.8f;
  t->ao = 0.8f + 0.2f * hair;
}

static void g_concrete(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 191), pit = h01((int)(u * N), (int)(v * N), 192) > 0.985f;
  float stain = clamp01((fbm(u, v, 3, 3, 193) - 0.55f) * 3);
  float c = 0.62f + 0.12f * n - 0.1f * stain;
  rgb(t, c, c * 0.99f, c * 0.96f);
  t->h = 0.5f + 0.3f * n - 0.3f * pit;
  t->rough = 0.88f;
  t->ao = 0.85f + 0.15f * n - 0.3f * pit;
}

/* stadium seats: moulded plastic backs in rows (the instance colour tints) */
static void g_seat(float u, float v, texel *t) {
  float su = fractf(u * 4), sv = fractf(v * 2);
  float edge = fminf(fminf(su, 1 - su), fminf(sv, 1 - sv));
  float shell = clamp01(edge * 16);
  float n = fbm(u, v, 16, 2, 201);
  float k = 0.86f + 0.1f * shell;
  rgb(t, k, k, k);
  t->h = smooth(shell) * 0.7f + 0.05f * n;
  t->rough = 0.35f + 0.1f * n;
  t->ao = 0.5f + 0.5f * shell;
}

/* clipped hedge: dense small leaves */
static void g_hedge(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 24, 211, &d1, &d2, &id);
  float leaf = clamp01(1 - d1 * 1.8f), n = fbm(u, v, 4, 3, 212), tint = (float)(id & 255) / 255.0f;
  float k = 0.55f + 0.5f * leaf + 0.2f * n;
  rgb(t, (0.16f + 0.08f * tint) * k, (0.42f + 0.08f * tint) * k, 0.14f * k);
  t->h = leaf * 0.8f + n * 0.2f;
  t->rough = 0.7f;
  t->ao = 0.35f + 0.65f * leaf;
}

static const gen_fn GEN[TEX_KINDS] = { g_grass, g_brick, g_snow, g_metal, g_asphalt, g_panel, g_wood, g_sand, g_fur, g_pad, g_leaf, g_bark,
                                       g_tabby, g_concrete, g_seat, g_hedge };
static const float NORMAL_STRENGTH[TEX_KINDS] = { 2, 4, 1.5f, 3, 2, 3, 3, 2.5f, 2, 3, 3, 5, 1.5f, 2, 2, 3 };

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
  tex_apply_uv(m, kind, repeat, repeat, normal_scale, ao_intensity);
}

/* a set at another repeat: three.js keeps the repeat on the texture, so it
 * is a copy, made once per (kind, repeat) and shared by every material */
typedef struct rep_set { int kind; float ru, rv; t3_texture *t[4]; } rep_set;
static rep_set REP[48];
static int nrep;

void tex_apply_uv(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_get(kind);
  t3_texture *ts[4] = { s->map, s->normal, s->rough, s->ao };
  if (ru != 1 || rv != 1) {
    rep_set *r = NULL;
    for (int k = 0; k < nrep && !r; k++)
      if (REP[k].kind == kind && REP[k].ru == ru && REP[k].rv == rv) r = &REP[k];
    if (!r && nrep < (int)(sizeof REP / sizeof REP[0])) {
      r = &REP[nrep++];
      *r = (rep_set){ kind, ru, rv, { 0 } };
      for (int i = 0; i < 4; i++) {
        r->t[i] = finish(ts[i]->pixels, i == 0);
        r->t[i]->repeat = (t3_vec2){ ru, rv };
      }
    }
    if (r)
      for (int i = 0; i < 4; i++) ts[i] = r->t[i];
    else cw_error("pugball: more than %d repeated texture sets", (int)(sizeof REP / sizeof REP[0]));
  }
  static const t3_map_slot SLOTS[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
  for (int i = 0; i < 4; i++) t3_material_set_texture(m, SLOTS[i], ts[i]);
  m->normal_scale = (t3_vec2){ normal_scale, normal_scale };
  m->ao_map_intensity = ao_intensity;
}

void tex_free_all(void) {
  for (int k = 0; k < nrep; k++)
    for (int i = 0; i < 4; i++) t3_release(REP[k].t[i]);
  nrep = 0;
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

/* ── pug coats: a head map with the mask as a charcoal gradient that follows
 * the face (darkest at the nose, fading into the coat round the eyes, a
 * darker crown), and a body map (lighter chest and legs, a darker back),
 * per coat, with fine fur variation. The head map is in a SphereGeometry's
 * uv (the skull is a deformed sphere facing +z); the body map runs along v
 * from belly (0) to back (1). Integer hashing only (tex.hash). */
static t3_texture *PUG_HEAD[8], *PUG_BODY[8];
static float smooth01(float e0, float e1, float x) { return smooth(clamp01((x - e0) / (e1 - e0))); }
t3_texture *tex_pug_head(int coat, const float rgb[3], bool black) {
  if (PUG_HEAD[coat]) return PUG_HEAD[coat];
  uint8_t *px = malloc(N * N * 4);
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      float u = (c + 0.5f) / N, v = (r + 0.5f) / N;
      float phi = 4.712389f + u * 6.2831853f, th = (1 - v) * 3.1415927f; /* the skull's seam is at the back */
      float st = cw_sin(th), x = -cw_cos(phi) * st, y = cw_cos(th), z = cw_sin(phi) * st;
      float d0 = sqrtf(x * x + (y + 0.38f) * (y + 0.38f) + (z - 0.9f) * (z - 0.9f));
      float de = fminf(sqrtf((x - 0.42f) * (x - 0.42f) + (y - 0.1f) * (y - 0.1f) + (z - 0.86f) * (z - 0.86f)),
                       sqrtf((x + 0.42f) * (x + 0.42f) + (y - 0.1f) * (y - 0.1f) + (z - 0.86f) * (z - 0.86f)));
      float cr0 = pug_crease(x, y, z);
      float dark = fmaxf(1 - smooth01(0.25f, 0.75f, d0), 0.72f * (1 - smooth01(0.1f, 0.42f, de)));
      dark *= smooth01(0.0f, 0.35f, z);
      float crown = 0.88f - 0.1f * clamp01(y) * (1 - clamp01(z));
      float n = 0.94f + 0.12f * h01(c, r, 301) * 0.5f + 0.06f * fbm(u, v, 16, 3, 302);
      float k = crown * n * (1 - 0.55f * cr0);
      float cr = rgb[0] * k, cg = rgb[1] * k, cb = rgb[2] * k;
      if (black) cr = 0.15f * crown * n, cg = 0.14f * crown * n, cb = 0.14f * crown * n;
      float ch = (0.12f + 0.08f * (1 - dark)) * (1 - 0.5f * cr0); /* charcoal, darkest at the nose */
      int i = (r * N + c) * 4;
      px[i] = b8(mixf(cr, ch, dark)), px[i + 1] = b8(mixf(cg, ch * 0.95f, dark)), px[i + 2] = b8(mixf(cb, ch * 0.95f, dark)), px[i + 3] = 255;
    }
  tex_hash = fnv(px, N * N * 4, tex_hash);
  PUG_HEAD[coat] = finish(px, true);
  free(px);
  return PUG_HEAD[coat];
}
t3_texture *tex_pug_body(int coat, const float rgb[3], const float belly[3], bool black) {
  if (PUG_BODY[coat]) return PUG_BODY[coat];
  uint8_t *px = malloc(N * N * 4);
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) {
      float u = (c + 0.5f) / N, v = (r + 0.5f) / N;
      float up = smooth01(0.2f, 0.65f, v), back = smooth01(0.72f, 1.0f, v);
      float n = 0.93f + 0.07f * h01(c, r, 311) + 0.06f * fbm(u, v, 16, 3, 312);
      float k = (1 - 0.2f * back) * n;
      float cr = mixf(belly[0], rgb[0], up) * k, cg = mixf(belly[1], rgb[1], up) * k, cb = mixf(belly[2], rgb[2], up) * k;
      if (black) cr = (0.16f - 0.03f * back) * n, cg = (0.15f - 0.03f * back) * n, cb = (0.15f - 0.03f * back) * n;
      int i = (r * N + c) * 4;
      px[i] = b8(cr), px[i + 1] = b8(cg), px[i + 2] = b8(cb), px[i + 3] = 255;
    }
  tex_hash = fnv(px, N * N * 4, tex_hash);
  PUG_BODY[coat] = finish(px, true);
  free(px);
  return PUG_BODY[coat];
}
