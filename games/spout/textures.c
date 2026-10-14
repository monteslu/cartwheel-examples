/* spout textures: procedural paper. Every sheet is the same pressed-fibre
 * stock in a different colour (or print): long fibres laid in one direction,
 * fine speckle, a soft tooth in the normal map, a matte roughness. Albedo,
 * normal, roughness and AO tile without seams and are made once at load, from
 * integer hashing only, so every host makes the same texels (tex.hash). */
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
static int wrapi(int a, int p) { return ((a % p) + p) % p; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static float vnoise(float x, float y, int px, int py, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = smooth(x - x0), fy = smooth(y - y0);
  float a = h01(wrapi(x0, px), wrapi(y0, py), seed), b = h01(wrapi(x0 + 1, px), wrapi(y0, py), seed);
  float c = h01(wrapi(x0, px), wrapi(y0 + 1, py), seed), d = h01(wrapi(x0 + 1, px), wrapi(y0 + 1, py), seed);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
/* fractal noise with separate periods across and along (fibres: long along u) */
static float fbm2(float u, float v, int pu, int pv, int oct, uint32_t seed) {
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

typedef struct texel { float r, g, b, h, rough, ao; } texel;

/* the stock every sheet shares: fibres (long along u), a little cloud, speckle */
static float fibre(float u, float v, uint32_t seed) {
  /* real-scale stock: fine fibres and tooth dominate, only a faint mottle */
  float long_f = fbm2(u, v, 16, 128, 3, seed);
  float tooth = fbm2(u, v, 64, 64, 2, seed + 13);
  float cloud = fbm2(u, v, 3, 3, 3, seed + 31);
  float speck = h01((int)(u * N), (int)(v * N), seed + 77);
  return 0.45f * long_f + 0.3f * tooth + 0.12f * cloud + 0.13f * speck;
}

static void gen(int kind, float u, float v, texel *t) {
  static const float BASE[PAPER_KINDS][3] = {
    { 0.95f, 0.93f, 0.88f }, /* white cardstock */
    { 0.74f, 0.56f, 0.38f }, /* kraft */
    { 0.60f, 0.68f, 0.74f }, /* the pipe: grey-blue */
    { 0.74f, 0.33f, 0.24f }, /* brick */
    { 0.47f, 0.70f, 0.33f }, /* grass green */
    { 0.62f, 0.80f, 0.97f }, /* sky blue */
    { 0.34f, 0.24f, 0.46f }, /* the spider: a deep plum card */
    { 0.80f, 0.64f, 0.42f }, /* wood-print */
    { 0.93f, 0.90f, 0.84f }, /* the backdrop sheet: tinted by its gradient */
    { 0.92f, 0.90f, 0.86f }, /* plain card (bricks, tinted by material) */
  };
  float f = fibre(u, v, 11u + (uint32_t)kind * 7u);
  float shade = 0.94f + 0.1f * (f - 0.5f);
  float r = BASE[kind][0], g = BASE[kind][1], b = BASE[kind][2];
  float h = f, ao = 1;
  if (kind == PAPER_BRICK) {
    /* printed bricks: 8 courses, offset each row, cream mortar lines */
    float y = v * 8, x = u * 4 + (((int)y & 1) ? 0.5f : 0);
    float fy = y - floorf(y), fx = x - floorf(x);
    float mortar = (fy < 0.08f || fx < 0.04f) ? 1 : 0;
    float tint = 0.85f + 0.3f * h01((int)floorf(x), (int)floorf(y), 5);
    r = mortar ? 0.86f : r * tint;
    g = mortar ? 0.8f : g * tint;
    b = mortar ? 0.7f : b * tint;
    h -= mortar * 0.35f;
    ao = mortar ? 0.75f : 1;
  } else if (kind == PAPER_GRASS) {
    /* crayon strokes */
    float st = fbm2(u, v, 32, 3, 2, 9);
    r *= 0.85f + 0.3f * st;
    g *= 0.85f + 0.3f * st;
    b *= 0.8f + 0.3f * st;
  } else if (kind == PAPER_WOOD) {
    float ring = v * 10 + fbm2(u, v, 3, 3, 3, 4) * 2;
    float line = cw_pow(1 - fabsf(ring - floorf(ring) - 0.5f) * 2, 8);
    r -= line * 0.2f;
    g -= line * 0.18f;
    b -= line * 0.12f;
    h -= line * 0.2f;
  } else if (kind == PAPER_BACKDROP) {
    /* a painted wash on the backdrop sheet: warm cream low, pale blue high,
     * soft blotches of watercolour; low contrast fibre (it is far away) */
    float wash = fbm2(u, v, 3, 2, 3, 41);
    r = 0.96f - 0.22f * v + 0.04f * wash;
    g = 0.92f - 0.08f * v + 0.03f * wash;
    b = 0.84f + 0.12f * v + 0.03f * wash;
    h = 0.5f + 0.2f * (f - 0.5f);
  } else if (kind == PAPER_PIPE) {
    /* faint printed seams every quarter */
    float fy = v * 4 - floorf(v * 4);
    if (fy < 0.015f) r *= 0.82f, g *= 0.82f, b *= 0.85f, h -= 0.2f;
  }
  t->r = r * shade;
  t->g = g * shade;
  t->b = b * shade;
  t->h = h;
  t->rough = 0.84f + 0.12f * f;
  t->ao = ao;
}

typedef struct paper_set { t3_texture *map, *normal, *rough, *ao; } paper_set;
static paper_set SETS[PAPER_KINDS];
uint32_t tex_hash = CW_HASH32_INIT;

static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }
static t3_texture *finish(const uint8_t *px, bool srgb, float ru, float rv) {
  t3_texture *t = t3_texture_new(N, N, px);
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

static const paper_set *paper_get(int kind) {
  paper_set *s = &SETS[kind];
  if (s->map) return s;
  texel *tx = malloc(sizeof *tx * N * N);
  uint8_t *a = malloc(N * N * 4), *nm = malloc(N * N * 4), *ro = malloc(N * N * 4), *ao = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) gen(kind, (x + 0.5f) / N, (y + 0.5f) / N, &tx[y * N + x]);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const texel *t = &tx[y * N + x];
      int i = (y * N + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = 255;
      float hx = tx[y * N + (x + 1) % N].h - tx[y * N + (x + N - 1) % N].h;
      float hy = tx[((y + 1) % N) * N + x].h - tx[((y + N - 1) % N) * N + x].h;
      float nx = -hx * 2.5f, ny = -hy * 2.5f, l = sqrtf(nx * nx + ny * ny + 1);
      nm[i] = b8(nx / l * 0.5f + 0.5f), nm[i + 1] = b8(ny / l * 0.5f + 0.5f), nm[i + 2] = b8(1 / l * 0.5f + 0.5f), nm[i + 3] = 255;
      ro[i] = ro[i + 1] = ro[i + 2] = b8(t->rough), ro[i + 3] = 255;
      ao[i] = ao[i + 1] = ao[i + 2] = b8(t->ao), ao[i + 3] = 255;
    }
  tex_hash = cw_hash32(a, N * N * 4, tex_hash);
  tex_hash = cw_hash32(nm, N * N * 4, tex_hash);
  s->map = finish(a, true, 1, 1);
  s->normal = finish(nm, false, 1, 1);
  s->rough = finish(ro, false, 1, 1);
  s->ao = finish(ao, false, 1, 1);
  free(tx);
  free(a);
  free(nm);
  free(ro);
  free(ao);
  return s;
}

/* the set on a material, with its own repeat (three.js keeps repeat per texture) */
void paper_apply(t3_material *m, int kind, float ru, float rv) {
  const paper_set *s = paper_get(kind);
  t3_texture *src[4] = { s->map, s->normal, s->rough, s->ao };
  t3_map_slot slots[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
  for (int i = 0; i < 4; i++) {
    t3_texture *t = src[i];
    if (ru != 1 || rv != 1) t = finish(src[i]->pixels, i == 0, ru, rv);
    else t3_retain(t);
    t3_material_set_texture(m, slots[i], t);
    t3_release(t);
  }
  m->normal_scale = (t3_vec2){ 0.6f, 0.6f };
  m->ao_map_intensity = 1;
}

void paper_free_all(void) {
  for (int k = 0; k < PAPER_KINDS; k++) {
    paper_set *s = &SETS[k];
    if (!s->map) continue;
    t3_release(s->map);
    t3_release(s->normal);
    t3_release(s->rough);
    t3_release(s->ao);
    memset(s, 0, sizeof *s);
  }
}
