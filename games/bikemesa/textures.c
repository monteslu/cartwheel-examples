/* BikeMesa textures: every surface gets a full PBR set made here at load time
 * (no image files): albedo (sRGB), a tangent-space normal map from a height
 * field, roughness and ambient occlusion. All tile without seams (periodic
 * noise), are mipmapped with 8x anisotropy, and are made once and shared.
 * Deterministic integer hashing only, so every host makes the same texels
 * (debug field tex.hash). */
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define N 256

static uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xffffff) / 16777216.0f; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static int wrapi(int a, int p) { return ((a % p) + p) % p; }

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
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static void rgb(texel *t, float r, float g, float b) { t->r = r, t->g = g, t->b = b; }

/* ── generators: u, v in 0..1 → colour (sRGB), height, roughness, ao ── */

/* weathered rock: multi-octave fbm relief, a few fine ridged cracks with dirt in
 * them, near white (the material tints it) */
static float ridged(float u, float v, int p, uint32_t seed) {
  float n = 1 - fabsf(fbm(u, v, p, 4, seed) * 2 - 1);
  return n * n * n * n;
}
static void g_rock(float u, float v, texel *t) {
  float n = fbm(u, v, 4, 6, 12), fine = fbm(u, v, 32, 3, 13), crack = ridged(u, v, 3, 14) * 0.7f + ridged(u, v, 7, 15) * 0.3f;
  float k = 0.6f + 0.3f * n + 0.08f * fine - 0.25f * crack;
  rgb(t, k, k * 0.98f, k * 0.95f);
  t->h = n * 0.7f + fine * 0.2f - crack * 0.4f;
  t->rough = 0.8f + 0.12f * crack;
  t->ao = 0.6f + 0.4f * (1 - crack) * (0.7f + 0.3f * n);
}

/* granite: speckled feldspar, quartz and mica, blocky relief */
static void g_granite(float u, float v, texel *t) {
  float n = fbm(u, v, 4, 5, 161), sp = h01((int)(u * N), (int)(v * N), 162), crack = ridged(u, v, 4, 163);
  float k = 0.62f + 0.18f * n;
  float r = k, g = k * 0.97f, b = k * 0.95f;
  if (sp > 0.82f) r = g = b = 0.9f;             /* quartz */
  else if (sp < 0.12f) r = g = b = 0.15f;       /* mica */
  else if (sp > 0.7f) r *= 1.08f, g *= 0.94f;   /* pink feldspar */
  rgb(t, r * (1 - 0.3f * crack), g * (1 - 0.3f * crack), b * (1 - 0.3f * crack));
  t->h = n * 0.6f + sp * 0.15f - crack * 0.4f;
  t->rough = 0.6f + 0.25f * n;
  t->ao = 0.65f + 0.35f * (1 - crack);
}

/* basalt: dark, fine-grained, vesicles (little holes) */
static void g_basalt(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 171), fine = fbm(u, v, 48, 2, 172);
  float d1, d2;
  uint32_t id;
  cells(u, v, 40, 173, &d1, &d2, &id);
  float hole = (id & 7) == 0 ? clamp01(1 - d1 * 4) : 0;
  float k = 0.3f + 0.15f * n + 0.05f * fine - 0.15f * hole;
  rgb(t, k, k * 0.98f, k * 1.02f);
  t->h = n * 0.5f + fine * 0.3f - hole * 0.6f;
  t->rough = 0.75f + 0.15f * fine;
  t->ao = 0.7f + 0.3f * (1 - hole);
}

/* mossy rock: rock with moss filling the hollows */
static void g_mossrock(float u, float v, texel *t) {
  g_rock(u, v, t);
  float moss = smooth(clamp01((fbm(u, v, 3, 4, 181) - 0.45f) * 4)) * (0.6f + 0.4f * (1 - t->h));
  float f = fbm(u, v, 40, 2, 182);
  t->r = mixf(t->r, 0.25f + 0.08f * f, moss);
  t->g = mixf(t->g, 0.42f + 0.1f * f, moss);
  t->b = mixf(t->b, 0.14f, moss);
  t->h = mixf(t->h, 0.55f + 0.2f * f, moss * 0.6f);
  t->rough = mixf(t->rough, 0.95f, moss);
}

/* concrete rubble: aggregate in grey cement, stains */
static void g_concrete(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 4, 191), agg = h01((int)(u * N), (int)(v * N), 192), stain = fbm(u, v, 2, 3, 193);
  float k = 0.58f + 0.12f * n - 0.12f * smooth(clamp01((stain - 0.55f) * 4)) + (agg > 0.85f ? 0.08f : 0);
  rgb(t, k, k * 0.99f, k * 0.97f);
  t->h = n * 0.4f + (agg > 0.85f ? 0.3f : 0);
  t->rough = 0.88f;
  t->ao = 0.75f + 0.25f * n;
}

static void g_dirt(float u, float v, texel *t) {
  float n = fbm(u, v, 6, 5, 21);
  float d1, d2;
  uint32_t id;
  cells(u, v, 24, 22, &d1, &d2, &id);
  float pebble = clamp01(1 - d1 * 3.2f) * ((id & 3) == 0);
  float k = 0.7f + 0.4f * n;
  rgb(t, mixf(0.6f * k, 0.72f, pebble), mixf(0.46f * k, 0.68f, pebble), mixf(0.32f * k, 0.62f, pebble));
  /* tyre-packed ruts along u */
  float rut = cw_pow(fabsf(cw_sin(v * 6.2831853f * 3 + fbm(u, v, 3, 2, 23) * 2)), 8);
  t->h = 0.35f * n + 0.6f * pebble - 0.15f * rut;
  t->rough = 0.95f - 0.25f * pebble - 0.1f * rut;
  t->ao = 0.7f + 0.3f * n;
}

static void g_grass(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 4, 31), n2 = fbm(u, v, 32, 3, 32), blade = h01((int)(u * N), (int)(v * N), 33);
  float k = 0.75f + 0.35f * n1 + 0.15f * (blade - 0.5f);
  rgb(t, 0.33f * k + 0.08f * n2, 0.55f * k, 0.18f * k);
  t->h = 0.4f + 0.4f * blade * n2;
  t->rough = 0.9f;
  t->ao = 0.75f + 0.25f * blade;
}

/* layered sandstone: strata bands along v, warped, with cross bedding */
static void g_sandstone(float u, float v, texel *t) {
  float warp = fbm(u, v, 2, 3, 41) * 0.6f;
  float band = v * 9 + warp;
  float f = band - floorf(band);
  int layer = (int)floorf(band);
  float tint = h01(wrapi(layer, 9), 0, 42);
  float n = fbm(u, v, 8, 4, 43), grain = fbm(u, v, 64, 2, 44);
  float edge = smooth(clamp01(f * 6)) * smooth(clamp01((1 - f) * 10));
  float k = 0.65f + 0.2f * tint + 0.12f * n + 0.05f * grain;
  rgb(t, k * 1.02f, k * (0.84f + 0.06f * tint), k * (0.7f + 0.08f * tint));
  t->h = edge * 0.6f + 0.25f * n + 0.1f * grain;
  t->rough = 0.88f;
  t->ao = 0.55f + 0.45f * edge;
}

static void g_snow(float u, float v, texel *t) {
  float n = fbm(u, v, 4, 5, 51), sparkle = h01((int)(u * N), (int)(v * N), 52) > 0.985f ? 1.0f : 0.0f;
  float drift = fbm(u, v, 2, 3, 53);
  float k = 0.9f + 0.08f * n;
  rgb(t, k * 0.97f, k * 0.985f, k);
  t->h = 0.5f * drift + 0.3f * n;
  t->rough = 0.55f - 0.35f * sparkle;
  t->ao = 0.85f + 0.15f * drift;
}

static void g_asphalt(float u, float v, texel *t) {
  float n = fbm(u, v, 16, 4, 61), agg = h01((int)(u * N), (int)(v * N), 62), patch = fbm(u, v, 2, 3, 63);
  float k = 0.22f + 0.1f * n + 0.08f * (agg > 0.8f) + 0.06f * patch;
  rgb(t, k, k * 1.01f, k * 1.04f);
  t->h = 0.3f * n + 0.5f * (agg > 0.8f);
  t->rough = 0.75f + 0.15f * n;
  t->ao = 0.8f + 0.2f * n;
}

/* planks along u, eight to a tile, with grain and dark joints */
static void g_wood(float u, float v, texel *t) {
  float rows = 8, y = v * rows;
  int row = (int)floorf(y);
  float fy = y - row;
  float off = h01(wrapi(row, 8), 0, 71);
  float x = u * 2 + off, fx = x - floorf(x);
  float joint = smooth(clamp01(fy * 14)) * smooth(clamp01((1 - fy) * 14)) * smooth(clamp01(fx * 60)) * smooth(clamp01((1 - fx) * 60));
  float grain = fbm(u * 1, v * 1, 4, 3, 72 + (uint32_t)row) * 2.5f;
  float ring = fy * 3 + grain, rf = ring - floorf(ring);
  float line = cw_pow(1 - fabsf(rf - 0.5f) * 2, 5);
  float tint = h01(wrapi(row, 8), 1, 73);
  float base = 0.75f + 0.2f * tint;
  rgb(t, mixf(0.8f, 0.58f, line * 0.6f) * base, mixf(0.58f, 0.4f, line * 0.6f) * base, mixf(0.36f, 0.24f, line * 0.6f) * base);
  t->r *= 0.35f + 0.65f * joint;
  t->g *= 0.35f + 0.65f * joint;
  t->b *= 0.35f + 0.65f * joint;
  t->h = joint * (0.7f + 0.2f * line);
  t->rough = 0.7f + 0.15f * line;
  t->ao = 0.45f + 0.55f * joint;
}

static void g_bark(float u, float v, texel *t) {
  float ridge = fbm(u, v, 6, 3, 81);
  float s = u * 9 + fbm(u, v, 4, 3, 82) * 2;
  float f = fabsf(s - floorf(s) - 0.5f) * 2;
  float k = 0.6f + 0.4f * ridge;
  rgb(t, 0.42f * k, 0.3f * k, 0.2f * k);
  t->h = f * 0.7f + ridge * 0.3f;
  t->rough = 0.9f;
  t->ao = 0.5f + 0.5f * f;
}

static void g_leaves(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 12, 91, &d1, &d2, &id);
  float leaf = clamp01(1 - d1 * 1.6f), n = fbm(u, v, 4, 3, 92);
  float k = 0.6f + 0.5f * leaf + 0.2f * n, tint = (float)(id & 255) / 255.0f;
  /* near white: the material colour sets the season */
  rgb(t, (0.75f + 0.2f * tint) * k, (0.8f + 0.1f * tint) * k, 0.7f * k);
  t->h = leaf;
  t->rough = 0.7f;
  t->ao = 0.45f + 0.55f * leaf;
}

static void g_sand(float u, float v, texel *t) {
  float ripple = cw_sin((v * 14 + fbm(u, v, 3, 3, 101) * 3) * 6.2831853f) * 0.5f + 0.5f;
  float n = fbm(u, v, 8, 4, 102), grain = h01((int)(u * N), (int)(v * N), 103);
  float k = 0.85f + 0.1f * n + 0.05f * (grain - 0.5f);
  rgb(t, k * 0.98f, k * 0.86f, k * 0.66f);
  t->h = ripple * 0.5f + 0.2f * n;
  t->rough = 0.92f;
  t->ao = 0.8f + 0.2f * ripple;
}

static void g_moss(float u, float v, texel *t) {
  /* moss over stony ground: no tile grid, just lumps and hollows */
  float lump = fbm(u, v, 5, 5, 111), moss = fbm(u, v, 4, 4, 112), fine = fbm(u, v, 32, 2, 113);
  float m = smooth(clamp01((moss - 0.4f) * 3));
  float c = 0.42f + 0.25f * lump;
  rgb(t, mixf(c, 0.22f + 0.1f * fine, m), mixf(c * 0.98f, 0.4f + 0.12f * fine, m), mixf(c * 0.92f, 0.12f, m));
  t->h = lump * 0.6f + m * 0.2f + fine * 0.15f;
  t->rough = mixf(0.8f, 0.95f, m);
  t->ao = 0.6f + 0.4f * lump;
}

/* brushed metal, near white */
static void g_metal(float u, float v, texel *t) {
  float brush = fbm(u * 1, v * 1, 4, 2, 121) * 0.3f + h01((int)(u * 4), (int)(v * N), 122) * 0.7f;
  float k = 0.86f + 0.1f * brush;
  rgb(t, k, k, k);
  t->h = brush * 0.3f;
  t->rough = 0.3f + 0.2f * brush;
  t->ao = 1;
}

/* a jersey's knit, near white (the material colour dyes it) */
static void g_fabric(float u, float v, texel *t) {
  float x = u * 96, y = v * 96;
  float kx = cw_sin(x * 6.2831853f) * 0.5f + 0.5f, ky = cw_sin((y + (((int)floorf(x)) & 1) * 0.5f) * 6.2831853f) * 0.5f + 0.5f;
  float knit = kx * ky, n = fbm(u, v, 8, 2, 131);
  float k = 0.86f + 0.1f * knit + 0.04f * n;
  rgb(t, k, k, k);
  t->h = knit;
  t->rough = 0.8f;
  t->ao = 0.85f + 0.15f * knit;
}

/* metallic flake: tiny facets at random tilts (normal only; albedo near white) */
static void g_flake(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 96, 141, &d1, &d2, &id);
  float tilt = (float)(id & 255) / 255.0f, edge = clamp01((d2 - d1) * 8);
  rgb(t, 0.96f + 0.04f * tilt, 0.96f + 0.04f * tilt, 0.96f + 0.04f * tilt);
  t->h = tilt * 0.6f * smooth(edge);
  t->rough = 0.25f + 0.1f * tilt;
  t->ao = 1;
}

/* cotton jersey knit with soft folds, near white */
static void g_cotton(float u, float v, texel *t) {
  float knit = cw_sin(u * 6.2831853f * 128) * 0.5f + 0.5f, fold = fbm(u, v, 3, 3, 151), crease = fbm(u * 1, v * 1, 6, 2, 152);
  float k = 0.86f + 0.06f * knit + 0.08f * fold;
  rgb(t, k, k, k);
  t->h = 0.15f * knit + 0.7f * fold + 0.25f * crease;
  t->rough = 0.85f;
  t->ao = 0.75f + 0.25f * fold;
}

static const gen_fn GEN[TEX_KINDS] = { g_rock, g_dirt, g_grass, g_sandstone, g_snow, g_asphalt, g_wood, g_bark, g_leaves,
                                       g_sand, g_moss, g_metal, g_fabric, g_flake, g_cotton, g_granite, g_basalt,
                                       g_mossrock, g_concrete };
static const float NORMAL_STRENGTH[TEX_KINDS] = { 4, 3, 2, 3.5f, 1.2f, 1.5f, 3, 5, 3, 1.5f, 3.5f, 0.6f, 1.5f, 2.5f, 3, 3, 3, 3.5f, 2.5f };

static tex_set SETS[TEX_KINDS];

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

uint32_t tex_hash = CW_HASH32_INIT;
static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }

const tex_set *tex_get(int kind) {
  tex_set *s = &SETS[kind];
  if (s->map) return s;
  texel *tx = malloc(sizeof *tx * N * N);
  uint8_t *a = malloc(N * N * 4), *nm = malloc(N * N * 4), *ro = malloc(N * N * 4), *ao = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) GEN[kind]((x + 0.5f) / N, (y + 0.5f) / N, &tx[y * N + x]);
  float st = NORMAL_STRENGTH[kind];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const texel *t = &tx[y * N + x];
      int i = (y * N + x) * 4;
      a[i] = b8(t->r);
      a[i + 1] = b8(t->g);
      a[i + 2] = b8(t->b);
      a[i + 3] = 255;
      float hx = tx[y * N + (x + 1) % N].h - tx[y * N + (x + N - 1) % N].h;
      float hy = tx[((y + 1) % N) * N + x].h - tx[((y + N - 1) % N) * N + x].h;
      float nx = -hx * st, ny = -hy * st, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f);
      nm[i + 1] = b8(ny / l * 0.5f + 0.5f);
      nm[i + 2] = b8(1 / l * 0.5f + 0.5f);
      nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough);
      ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao);
      ao[i + 3] = 255;
    }
  tex_hash = cw_hash32(a, N * N * 4, tex_hash);
  tex_hash = cw_hash32(nm, N * N * 4, tex_hash);
  tex_hash = cw_hash32(ro, N * N * 4, tex_hash);
  tex_hash = cw_hash32(ao, N * N * 4, tex_hash);
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

/* the set on a material; rx / ry repeat it (a repeated copy: three.js keeps
 * repeat on the texture) */
void tex_apply(t3_material *m, int kind, float rx, float ry, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_get(kind);
  t3_texture *ts[4] = { s->map, s->normal, s->rough, s->ao };
  t3_map_slot slots[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
  for (int i = 0; i < 4; i++) {
    if (rx == 1 && ry == 1) {
      t3_material_set_texture(m, slots[i], ts[i]);
      continue;
    }
    t3_texture *c = finish(ts[i]->pixels, i == 0);
    c->repeat = (t3_vec2){ rx, ry };
    t3_material_set_texture(m, slots[i], c);
    t3_release(c);
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
