/* chromite textures: procedural metal, made once at load from integer
 * hashing only, so every host makes the same texels (tex.hash).
 *
 *   chrome    near-white polished metal with faint polishing swirls and
 *             fingerprint haze in the roughness
 *   rust      the enemies' chrome, pitted and blotched with rust (rust is
 *             rough and not metal: a metalness map carries it)
 *   deck      the fortress plating: gunmetal panels with seams, rivets,
 *             scratches and grime in the recesses
 *   grate     vent grilles and trench floors: slots over a dark cavity
 *
 * and the environment the chrome reflects: an HDR equirectangular horizon,
 * the classic chrome map (deep blue sky, a dark broken ridge, a pale lit
 * plain below), PMREM filtered by three.c on first use. */
#include <stdlib.h>
#include <string.h>

#include "game.h"

uint32_t tex_hash = CW_HASH32_INIT;

static uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xffffff) / 16777216.0f; }
static int wrapi(int a, int p) { return ((a % p) + p) % p; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static float vnoise(float x, float y, int px, int py, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = smooth(x - x0), fy = smooth(y - y0);
  float a = h01(wrapi(x0, px), wrapi(y0, py), seed), b = h01(wrapi(x0 + 1, px), wrapi(y0, py), seed);
  float c = h01(wrapi(x0, px), wrapi(y0 + 1, py), seed), d = h01(wrapi(x0 + 1, px), wrapi(y0 + 1, py), seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static float fbm(float u, float v, int pu, int pv, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < oct; o++) {
    s += amp * vnoise(u * pu, v * pv, pu, pv, seed + (uint32_t)o * 101u);
    norm += amp;
    amp *= 0.5f;
    pu *= 2;
    pv *= 2;
  }
  return s / norm;
}
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float sstep(float a, float b, float x) { return smooth(clamp01((x - a) / (b - a))); }

typedef struct texel { float r, g, b, h, rough, metal, ao; } texel;

/* rivets: a dome every `step` texels along a panel's border */
static float rivet(float u, float v, float cell, float rad) {
  float x = u / cell, y = v / cell;
  float fx = x - floorf(x) - 0.5f, fy = y - floorf(y) - 0.5f;
  float d = sqrtf(fx * fx + fy * fy) / rad;
  return d < 1 ? sqrtf(1 - d * d) : 0;
}

static void gen(int kind, float u, float v, int size, texel *t) {
  (void)size;
  switch (kind) {
  case MT_CHROME: {
    float swirl = fbm(u, v, 4, 24, 3, 3) * 0.6f + fbm(u, v, 24, 4, 2, 5) * 0.4f;
    float haze = fbm(u, v, 3, 3, 4, 7);
    t->r = 0.97f, t->g = 0.97f, t->b = 0.98f;
    t->h = swirl * 0.15f;
    t->rough = 0.07f + 0.06f * swirl + 0.1f * sstep(0.6f, 0.85f, haze);
    t->metal = 1;
    t->ao = 1;
    break;
  }
  case MT_RUST: {
    /* rust grows in blotches; pits where it is deepest */
    float blot = fbm(u, v, 4, 4, 5, 11);
    float fine = fbm(u, v, 32, 32, 2, 13);
    float rust = sstep(0.56f, 0.7f, blot + 0.12f * (fine - 0.5f));
    float deep = sstep(0.68f, 0.8f, blot);
    float pit = h01((int)(u * 256), (int)(v * 256), 17) > 0.97f ? deep : 0;
    float swirl = fbm(u, v, 4, 24, 3, 3);
    float rr = 0.42f + 0.2f * fine, rg = 0.17f + 0.08f * fine, rb = 0.06f;
    t->r = 0.93f + (rr - 0.93f) * rust;
    t->g = 0.92f + (rg - 0.92f) * rust;
    t->b = 0.92f + (rb - 0.92f) * rust;
    t->h = 0.1f * swirl - 0.25f * rust * fine - 0.4f * pit;
    t->rough = 0.12f + 0.06f * swirl + (0.8f - 0.12f) * rust;
    t->metal = 1 - rust * 0.9f;
    t->ao = 1 - 0.35f * pit;
    break;
  }
  case MT_DECK: {
    /* plating: the tile splits into four big panels, and each of those
     * stays whole or splits in two or in four (a fixed layout from the
     * hash). Each panel gets its own tone and brushing direction, a seam
     * round it, rivets along its long edges, scratches along the grain and
     * grime that gathers toward the seams; one in five is tread plate */
    int qx = u < 0.5f ? 0 : 1, qy = v < 0.5f ? 0 : 1;
    float x0 = qx * 0.5f, y0 = qy * 0.5f, x1 = x0 + 0.5f, y1 = y0 + 0.5f;
    uint32_t split = hash2(qx, qy, 61) % 4, id = (uint32_t)(qx + qy * 2) * 4;
    float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
    if (split == 1 || split == 3) { if (u < mx) x1 = mx; else x0 = mx, id += 1; }
    if (split == 2 || split == 3) { if (v < my) y1 = my; else y0 = my, id += 2; }
    float ex = fminf(u - x0, x1 - u), ey = fminf(v - y0, y1 - v), edge = fminf(ex, ey);
    float seam = 1 - sstep(0.002f, 0.005f, edge);
    float bevel = sstep(0.003f, 0.012f, edge);
    float tone = 0.8f + 0.4f * h01((int)id, 3, 23);
    bool along_u = (x1 - x0) >= (y1 - y0);
    /* rivets in a row 0.02 in from the long edges */
    float rv = 0;
    float ldist = along_u ? ey : ex;
    if (ldist > 0.012f && ldist < 0.03f) rv = rivet(u, v, 1.0f / 48, 0.3f);
    /* brushing: long fibres along the panel */
    float brush = along_u ? fbm(u, v, 4, 160, 2, 29 + id) : fbm(u, v, 160, 4, 2, 29 + id);
    float scratch = sstep(0.66f, 0.72f, along_u ? fbm(u, v, 3, 90, 2, 43 + id) : fbm(u, v, 90, 3, 2, 43 + id)) * 0.6f;
    float tread = 0;
    if (h01((int)id, 7, 71) < 0.12f) {
      float a = (u + v) * 40, b = (u - v) * 40;
      float fa = a - floorf(a) - 0.5f, fb = b - floorf(b) - 0.5f;
      tread = sstep(0.32f, 0.2f, sqrtf(fa * fa * 0.25f + fb * fb * 4)) * 0.8f;
    }
    float grime = fbm(u, v, 6, 6, 4, 31);
    float g = sstep(0.45f, 0.8f, grime) * (0.35f + 0.65f * sstep(0.06f, 0.0f, edge));
    float base = 0.3f * tone * (0.96f + 0.08f * brush) * (1 - 0.5f * g) + 0.08f * scratch + 0.05f * tread;
    t->r = base * 0.97f, t->g = base, t->b = base * 1.03f;
    if (seam > 0) t->r *= 1 - 0.85f * seam, t->g *= 1 - 0.85f * seam, t->b *= 1 - 0.85f * seam;
    t->h = 0.5f * bevel - 0.5f * seam + 0.4f * rv + 0.3f * tread + 0.01f * brush;
    t->rough = 0.42f + 0.3f * g - 0.15f * scratch - 0.08f * brush;
    t->metal = 1 - 0.6f * g;
    t->ao = 1 - 0.6f * seam - 0.15f * (1 - bevel);
    break;
  }
  default: { /* MT_GRATE: 8 slots over a dark cavity, bars between */
    float sv = v * 8;
    float fy = sv - floorf(sv);
    float bar = sstep(0.18f, 0.24f, fabsf(fy - 0.5f) * 2 - 0.55f) ;
    float cav = 1 - bar;
    float frame = 1 - sstep(0.02f, 0.05f, fminf(fminf(u, 1 - u), fminf(v, 1 - v)));
    float solid = fmaxf(bar, frame);
    float grime = fbm(u, v, 8, 8, 3, 41);
    float base = 0.3f * solid + 0.03f * cav;
    t->r = base * (1 - 0.3f * grime);
    t->g = base * (1 - 0.3f * grime);
    t->b = base * (1 - 0.25f * grime) * 1.05f;
    t->h = solid * (0.5f + 0.5f * smooth(clamp01(1 - fabsf(fy - 0.5f) * 2))) ;
    t->rough = 0.4f + 0.3f * grime;
    t->metal = solid;
    t->ao = 0.25f + 0.75f * solid;
    break;
  }
  }
}

typedef struct mat_set { t3_texture *map, *normal, *rough, *metal, *ao; } mat_set;
static mat_set SETS[MT_KINDS];
static const int SIZES[MT_KINDS] = { 256, 256, 512, 256 };

static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }
static t3_texture *finish(const uint8_t *px, int n, bool srgb, float ru, float rv) {
  t3_texture *t = t3_texture_new(n, n, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  t->repeat = (t3_vec2){ ru, rv };
  return t;
}

static const mat_set *mat_get(int kind) {
  mat_set *s = &SETS[kind];
  if (s->map) return s;
  int N = SIZES[kind];
  texel *tx = malloc(sizeof *tx * (size_t)N * N);
  uint8_t *a = malloc((size_t)N * N * 4), *nm = malloc((size_t)N * N * 4), *ro = malloc((size_t)N * N * 4),
          *me = malloc((size_t)N * N * 4), *ao = malloc((size_t)N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) gen(kind, (x + 0.5f) / N, (y + 0.5f) / N, N, &tx[y * N + x]);
  float strength = kind == MT_DECK ? 6.0f : kind == MT_GRATE ? 5.0f : 3.0f;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const texel *t = &tx[y * N + x];
      int i = (y * N + x) * 4;
      /* the linear colour goes out as sRGB bytes */
      float c[3] = { t->r, t->g, t->b };
      for (int k = 0; k < 3; k++) c[k] = c[k] <= 0.0031308f ? c[k] * 12.92f : 1.055f * cw_pow(clamp01(c[k]), 1 / 2.4f) - 0.055f;
      a[i] = b8(c[0]), a[i + 1] = b8(c[1]), a[i + 2] = b8(c[2]), a[i + 3] = 255;
      float hx = tx[y * N + (x + 1) % N].h - tx[y * N + (x + N - 1) % N].h;
      float hy = tx[((y + 1) % N) * N + x].h - tx[((y + N - 1) % N) * N + x].h;
      float nx = -hx * strength, ny = -hy * strength, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      /* r186 reads roughness from G and metalness from B */
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough), ro[i + 3] = 255;
      me[i] = me[i + 1] = me[i + 2] = b8(t->metal), me[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
    }
  tex_hash = cw_hash32(a, (size_t)N * N * 4, tex_hash);
  tex_hash = cw_hash32(nm, (size_t)N * N * 4, tex_hash);
  s->map = finish(a, N, true, 1, 1);
  s->normal = finish(nm, N, false, 1, 1);
  s->rough = finish(ro, N, false, 1, 1);
  s->metal = finish(me, N, false, 1, 1);
  s->ao = finish(ao, N, false, 1, 1);
  free(tx), free(a), free(nm), free(ro), free(me), free(ao);
  return s;
}

void tex_apply(t3_material *m, int kind, float ru, float rv) {
  const mat_set *s = mat_get(kind);
  int N = SIZES[kind];
  t3_texture *src[5] = { s->map, s->normal, s->rough, s->metal, s->ao };
  t3_map_slot slots[5] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_METALNESS_MAP, T3_AO_MAP };
  for (int i = 0; i < 5; i++) {
    t3_texture *t = src[i];
    if (ru != 1 || rv != 1) t = finish(src[i]->pixels, N, i == 0, ru, rv);
    else t3_retain(t);
    t3_material_set_texture(m, slots[i], t);
    t3_release(t);
  }
  m->roughness = 1;
  m->metalness = 1;
  m->normal_scale = (t3_vec2){ 1, 1 };
  m->ao_map_intensity = 1;
}

/* ── the environment ─────────────────────────────────────────────── */

#define ENV_W 512
#define ENV_H 256
static t3_texture *ENV;

static uint16_t half(float f) {
  uint32_t x;
  memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000u;
  int32_t e = (int32_t)((x >> 23) & 0xff) - 127 + 15;
  uint32_t m = x & 0x7fffffu;
  if (e <= 0) return (uint16_t)sign;
  if (e >= 31) return (uint16_t)(sign | 0x7c00u);
  return (uint16_t)(sign | (uint32_t)e << 10 | (m + 0x1000u) >> 13);
}

/* direction (equirect u, v) -> linear HDR radiance. v = 0 is straight up. */
static void env_texel(float u, float v, float *c) {
  float el = (0.5f - v) * 3.14159265f; /* elevation, radians: + up */
  float az = u * 6.2831853f;
  /* the broken ridge: a skyline of mesas and spires round the horizon */
  float ridge = 0.04f + 0.09f * fbm(u, 0.3f, 6, 1, 4, 51) + 0.06f * sstep(0.7f, 0.75f, fbm(u, 0.7f, 24, 1, 2, 53));
  float s = cw_sin(el);
  if (el > ridge) {
    /* sky: deep ultramarine overhead to a bright cyan glow at the ridge */
    float k = clamp01((s - ridge) / (1 - ridge));
    float glow = cw_exp(-k * 7);
    c[0] = 0.55f - 0.35f * (1 - k) + 0.2f * glow;
    c[1] = 0.68f - 0.3f * (1 - k) + 0.8f * glow;
    c[2] = 0.95f - 0.1f * (1 - k) + 1.3f * glow;
    /* two long studio strips high up: the chrome's bright streaks */
    float strip1 = sstep(0.06f, 0.0f, fabsf(el - 1.0f)) * sstep(0.35f, 0.3f, fabsf(cw_sin(az * 0.5f - 0.6f)));
    float strip2 = sstep(0.04f, 0.0f, fabsf(el - 0.55f)) * sstep(0.5f, 0.45f, fabsf(cw_sin(az * 0.5f + 1.2f)));
    c[0] += 6 * strip1 + 3 * strip2;
    c[1] += 6.2f * strip1 + 3.2f * strip2;
    c[2] += 6.6f * strip1 + 3.6f * strip2;
  } else if (el > -0.02f) {
    /* the ridge itself: near black, a little blue on its face */
    c[0] = 0.006f, c[1] = 0.008f, c[2] = 0.02f;
  } else {
    /* the plain: bright, warm pinkish white, fading pale far off */
    float k = clamp01(-s);
    float haze = fbm(u, v, 8, 4, 3, 57);
    c[0] = 1.5f + 0.4f * k + 0.12f * haze;
    c[1] = 1.3f + 0.3f * k + 0.1f * haze;
    c[2] = 1.35f + 0.25f * k + 0.1f * haze;
    /* soft horizon line just below the ridge */
    float line = cw_exp((el + 0.02f) * 20);
    c[0] *= 1 - 0.5f * line, c[1] *= 1 - 0.5f * line, c[2] *= 1 - 0.4f * line;
  }
}

t3_texture *env_texture(void) {
  if (ENV) return ENV;
  uint16_t *px = malloc(sizeof(uint16_t) * 4 * ENV_W * ENV_H);
  for (int y = 0; y < ENV_H; y++)
    for (int x = 0; x < ENV_W; x++) {
      float c[3];
      /* a DataTexture is not flipped: row 0 is the bottom of the map (straight down) */
      env_texel((x + 0.5f) / ENV_W, 1 - (y + 0.5f) / ENV_H, c);
      uint16_t *p = px + (y * ENV_W + x) * 4;
      p[0] = half(c[0]), p[1] = half(c[1]), p[2] = half(c[2]), p[3] = half(1);
    }
  tex_hash = cw_hash32(px, sizeof(uint16_t) * 4 * ENV_W * ENV_H, tex_hash);
  ENV = t3_data_texture_new_typed(ENV_W, ENV_H, px, T3_HALF_FLOAT_TYPE);
  free(px);
  ENV->mapping = T3_EQUIRECTANGULAR_REFLECTION_MAPPING;
  ENV->wrap_s = T3_REPEAT;
  ENV->mag_filter = T3_LINEAR;
  ENV->min_filter = T3_LINEAR;
  ENV->color_space = T3_LINEAR_SRGB_COLOR_SPACE;
  return ENV;
}

void tex_free_all(void) {
  for (int k = 0; k < MT_KINDS; k++) {
    mat_set *s = &SETS[k];
    if (!s->map) continue;
    t3_release(s->map), t3_release(s->normal), t3_release(s->rough), t3_release(s->metal), t3_release(s->ao);
    memset(s, 0, sizeof *s);
  }
  if (ENV) t3_release(ENV), ENV = NULL;
}
