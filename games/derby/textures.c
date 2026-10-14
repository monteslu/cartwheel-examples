/* derby textures: every surface gets a full PBR set made here at load time
 * (no image files): albedo (sRGB), a tangent-space normal map from a height
 * field, roughness and ambient occlusion. All tile without seams (periodic
 * noise), are mipmapped with 8x anisotropy, and are made once, on first
 * use, and shared. Integer hashing and cartwheel/math.h only, so every host
 * makes the same texels (debug field tex.hash).
 *
 * The look is grime: churned dirt with tyre ruts and puddles, stained and
 * chipped concrete, worn tread, paint with scuffs and grime that the dents
 * strip back to rust, galvanised steel, weathered planks. */
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

typedef struct texel { float r, g, b, a, h, rough, ao; } texel;
typedef void (*gen_fn)(float u, float v, texel *t);
static void rgb(texel *t, float r, float g, float b) { t->r = r, t->g = g, t->b = b; }

/* ── generators: u, v in 0..1 -> colour (screen sRGB), height, roughness, ao ── */

/* the arena floor: churned mud, tyre tracks pressed in it (soft-edged,
 * crossing), clods and stones, wet patches that go dark and glossy */
static void g_dirt(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 5, 11), n2 = fbm(u, v, 24, 4, 12), n3 = fbm(u, v, 64, 2, 16);
  float wet = clamp01((fbm(u, v, 2, 3, 13) - 0.52f) * 4);
  /* tracks: a few wide, soft furrows wandering in two directions */
  float rut = 0;
  for (int k = 0; k < 4; k++) {
    float w = fbm(u, v, 2, 2, 14 + (uint32_t)k) * 0.9f;
    float c = fractf((k & 1 ? v : u) * 2 + w + k * 0.31f);
    float d = fabsf(c - 0.5f);
    rut += clamp01(1 - d * 7) * clamp01(1 - d * 7) * (0.35f + 0.65f * fbm(u, v, 8, 2, 40 + (uint32_t)k));
  }
  rut = clamp01(rut) * 0.8f;
  float d1, d2;
  uint32_t id;
  cells(u, v, 48, 15, &d1, &d2, &id);
  float pebble = (id & 15) == 0 ? clamp01(1 - d1 * 3.2f) : 0;
  float clod = clamp01((n2 - 0.62f) * 5) * (1 - wet);
  float k = 0.72f + 0.28f * n1 + 0.1f * (n3 - 0.5f) - 0.1f * rut + 0.08f * clod;
  rgb(t, 0.36f * k, 0.27f * k, 0.2f * k);
  t->r = mixf(t->r, t->r * 0.5f, wet), t->g = mixf(t->g, t->g * 0.52f, wet), t->b = mixf(t->b, t->b * 0.58f, wet);
  if (pebble > 0) {
    float pc = 0.4f + 0.18f * (float)((id >> 4) & 255) / 255.0f;
    t->r = mixf(t->r, pc, pebble), t->g = mixf(t->g, pc * 0.95f, pebble), t->b = mixf(t->b, pc * 0.9f, pebble);
  }
  t->h = 0.5f + 0.3f * n2 + 0.1f * n3 - 0.3f * rut + 0.35f * pebble + 0.2f * clod;
  t->rough = mixf(0.94f, 0.18f, wet) - 0.15f * pebble;
  t->ao = 0.75f + 0.25f * (1 - rut * 0.6f) - 0.1f * pebble;
}

/* jersey barriers: cast concrete, stained, chipped, a little moss low down */
static void g_concrete(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 21), pit = h01((int)(u * N), (int)(v * N), 22) > 0.982f;
  float stain = clamp01((fbm(u, v, 3, 3, 23) - 0.5f) * 3);
  float streak = clamp01((fbm(u * 0.25f, v * 4, 4, 3, 24) - 0.55f) * 4) * (1 - v); /* rain runs down */
  float d1, d2;
  uint32_t id;
  cells(u, v, 6, 25, &d1, &d2, &id);
  float chip = (id & 7) == 0 ? clamp01(1 - d1 * 2.6f) : 0;
  float c = 0.6f + 0.12f * n - 0.16f * stain - 0.12f * streak;
  rgb(t, c, c * 0.98f, c * 0.94f);
  t->r -= 0.05f * chip, t->g -= 0.05f * chip;
  t->h = 0.5f + 0.25f * n - 0.3f * pit - 0.35f * chip;
  t->rough = 0.9f;
  t->ao = 0.85f + 0.15f * n - 0.3f * pit - 0.2f * chip;
}

/* a tyre, laid out as its lathe's uv: u round the tyre (six repeats), v
 * across the cross section: sidewall, tread blocks in the middle, sidewall.
 * The sidewalls carry raised lettering-like blocks (no words, no brands);
 * mud caked into the tread and up the walls */
static void g_rubber(float u, float v, texel *t) {
  float n = fbm(u, v, 16, 3, 31), mud = clamp01((fbm(u, v, 4, 3, 32) - 0.4f) * 2.2f);
  float h = 0.5f, ao = 1, rough = 0.9f;
  if (v > 0.3f && v < 0.7f) {
    /* the tread: staggered blocks with sipes */
    float tv = (v - 0.3f) / 0.4f;
    float bu = fractf(u * 10 + (tv > 0.5f ? 0.5f : 0)), bv = fractf(tv * 3);
    float groove = fminf(clamp01(fminf(bu, 1 - bu) * 12), clamp01(fminf(bv, 1 - bv) * 9));
    float sipe = fabsf(fractf(u * 30) - 0.5f) < 0.04f ? 0.5f : 1;
    h = groove * sipe;
    ao = 0.4f + 0.6f * groove;
    mud *= 1.3f - groove * 0.6f; /* packed into the grooves */
  } else {
    /* sidewall: a ring of raised glyph blocks, then smooth rubber */
    float sv = v < 0.3f ? v / 0.3f : (1 - v) / 0.3f;
    float band = sv > 0.35f && sv < 0.6f ? 1 : 0;
    float gu = fractf(u * 28);
    uint32_t id = (uint32_t)floorf(u * 28) * 7u + 3u;
    float on = (h01((int)id, 0, 33) > 0.25f && gu > 0.15f && gu < 0.85f) ? 1 : 0;
    float row = h01((int)id, 1, 34);
    if (row > 0.5f && sv > 0.46f && sv < 0.5f) on = 0; /* a stroke gap: letters, not bars */
    h = 0.5f + 0.25f * band * on + 0.05f * n;
    ao = 0.85f + 0.15f * n;
    mud *= 0.5f + 0.5f * (1 - sv);
  }
  mud = clamp01(mud);
  float c = 0.065f + 0.03f * n;
  rgb(t, mixf(c, 0.26f, mud), mixf(c, 0.2f, mud), mixf(c, 0.14f, mud));
  t->h = h;
  t->rough = mixf(rough, 0.97f, mud);
  t->ao = ao;
}

/* car paint (near white: the instance / vertex colour is the paint): orange
 * peel, scuffs, grime from the sills up */
static void g_paint(float u, float v, texel *t) {
  float n = fbm(u, v, 32, 2, 41), grime = clamp01((fbm(u, v, 10, 4, 42) - 0.55f) * 2.0f) * 0.7f; /* fine grime, not blotches */
  float scratch = 0;
  for (int k = 0; k < 4; k++) {
    float a = h01(k, 0, 43) * 3.14f, s, c;
    cw_sincos(a, &s, &c);
    float l = fractf((u * c + v * s) * (5 + k * 2) + fbm(u, v, 4, 2, 44 + (uint32_t)k) * 0.3f);
    scratch += 0.5f * cw_pow(1 - clamp01(fabsf(l - 0.5f) * 60), 3) * (fbm(u, v, 6, 2, 50 + (uint32_t)k) > 0.68f);
  }
  scratch = clamp01(scratch);
  float k = 0.9f + 0.06f * n - 0.3f * grime;
  float mott = fbm(u, v, 24, 3, 45); /* oxidised paint, finely mottled */
  /* swirl marks: fine concentric scratches from a buffer, in a few patches */
  float swirl = 0;
  for (int k = 0; k < 4; k++) {
    float cx = h01(k, 5, 46), cy = h01(k, 6, 46);
    float dx = fractf(u - cx + 0.5f) - 0.5f, dy = fractf(v - cy + 0.5f) - 0.5f, d = sqrtf(dx * dx + dy * dy);
    if (d < 0.22f) swirl += cw_pow(1 - clamp01(fabsf(fractf(d * 140 + fbm(u, v, 8, 1, 47 + (uint32_t)k)) - 0.5f) * 5), 6) * (1 - d / 0.22f);
  }
  scratch = clamp01(scratch + swirl * 0.4f);
  k *= 0.95f + 0.06f * mott;
  rgb(t, mixf(k, 0.62f, scratch), mixf(k * 0.98f, 0.6f, scratch), mixf(k * 0.95f, 0.58f, scratch));
  t->h = 0.5f + 0.05f * n - 0.2f * scratch + 0.04f * mott;
  t->rough = 0.42f + 0.3f * grime + 0.25f * scratch + 0.15f * mott;
  t->ao = 0.9f + 0.1f * n;
}

/* bare steel gone to rust: blisters of bubbled paint lifting at the edge
 * of the rot, flaking scale, orange blooms, pits */
static void g_rust(float u, float v, texel *t) {
  float n = fbm(u, v, 8, 5, 61), bloom = clamp01((fbm(u, v, 4, 4, 62) - 0.4f) * 2.5f);
  float d1, d2;
  uint32_t id;
  cells(u, v, 20, 63, &d1, &d2, &id);
  float flake = clamp01((d2 - d1) * 6);
  /* blisters: round domes on a cell grid */
  float b1, b2;
  uint32_t bid;
  cells(u, v, 36, 64, &b1, &b2, &bid);
  float blister = (bid & 3) == 0 ? clamp01(1 - b1 * 2.4f) : 0;
  blister = blister * blister * (3 - 2 * blister);
  float pit = h01((int)(u * N), (int)(v * N), 65) > 0.97f ? 1 : 0;
  rgb(t, mixf(0.36f, 0.5f, bloom) + 0.08f * n, mixf(0.33f, 0.24f, bloom) + 0.04f * n, mixf(0.31f, 0.12f, bloom));
  t->h = 0.45f + 0.25f * n + 0.2f * flake * bloom + 0.35f * blister - 0.25f * pit;
  t->rough = mixf(0.6f, 0.94f, bloom) - 0.1f * blister;
  t->ao = 0.65f + 0.35f * flake - 0.3f * pit;
}

/* primer: flat, sanded in swirls, a few runs */
static void g_primer(float u, float v, texel *t) {
  float n = fbm(u, v, 12, 4, 131);
  float sand = 0;
  for (int k = 0; k < 6; k++) {
    float cx = h01(k, 0, 132), cy = h01(k, 1, 132), r = 0.08f + 0.1f * h01(k, 2, 132);
    float dx = fractf(u - cx + 0.5f) - 0.5f, dy = fractf(v - cy + 0.5f) - 0.5f, d = sqrtf(dx * dx + dy * dy);
    sand += cw_pow(1 - clamp01(fabsf(fractf(d * 90) - 0.5f) * 4), 4) * (d < r ? 1 : 0) * 0.5f;
  }
  float c = 0.86f + 0.08f * n - 0.05f * sand;
  rgb(t, c, c, c * 0.98f);
  t->h = 0.5f + 0.06f * n - 0.04f * sand;
  t->rough = 0.86f + 0.08f * n;
  t->ao = 0.95f;
}

/* galvanised steel: spangle, scratches, a little rust at the seams */
static void g_steel(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 12, 71, &d1, &d2, &id);
  float sp = (float)(id & 255) / 255.0f, n = fbm(u, v, 8, 4, 72);
  float rust = clamp01((fbm(u, v, 3, 3, 73) - 0.62f) * 4);
  float c = 0.55f + 0.12f * sp + 0.08f * n;
  rgb(t, mixf(c, 0.45f, rust), mixf(c, 0.28f, rust), mixf(c * 1.02f, 0.15f, rust));
  t->h = 0.5f + 0.1f * sp + 0.2f * rust * n;
  t->rough = mixf(0.38f + 0.2f * n, 0.9f, rust);
  t->ao = 0.85f + 0.15f * n;
}

/* old planks (bleachers): grain, grey weathering, gaps */
static void g_wood(float u, float v, texel *t) {
  float row = floorf(v * 5), x = u * 1 + h01((int)row, 0, 81) * 3;
  float seg = fractf(x);
  float warp = fbm(u, v, 4, 3, 82 + (uint32_t)row) * 2;
  float line = cw_pow(1 - fabsf(fractf(u * 6 + warp) - 0.5f) * 2, 6);
  float seam = clamp01(fminf(fractf(v * 5), 1 - fractf(v * 5)) * 50) * clamp01(fminf(seg, 1 - seg) * 200);
  float grey = 0.55f + 0.3f * fbm(u, v, 8, 2, 83);
  float k = 0.8f + 0.25f * h01((int)row, (int)floorf(x), 84);
  float r = mixf(0.55f, 0.4f, line) * k, g = mixf(0.42f, 0.3f, line) * k, b = mixf(0.3f, 0.2f, line) * k;
  float l = (r + g + b) / 3;
  rgb(t, mixf(r, l, grey), mixf(g, l, grey), mixf(b, l * 1.03f, grey));
  t->h = 0.55f - line * 0.1f - (1 - seam) * 0.45f;
  t->rough = 0.85f;
  t->ao = 0.55f + 0.45f * seam;
}

/* the lot outside: gravel and tar patches */
static void g_gravel(float u, float v, texel *t) {
  float d1, d2;
  uint32_t id;
  cells(u, v, 64, 91, &d1, &d2, &id);
  float stone = clamp01((d2 - d1) * 5), tone = (float)(id & 255) / 255.0f;
  float tar = clamp01((fbm(u, v, 3, 3, 92) - 0.6f) * 5);
  float c = 0.32f + 0.18f * tone;
  rgb(t, mixf(c * 1.02f, 0.08f, tar), mixf(c, 0.08f, tar), mixf(c * 0.95f, 0.085f, tar));
  t->h = mixf(stone * 0.8f, 0.3f, tar);
  t->rough = mixf(0.9f, 0.55f, tar);
  t->ao = mixf(0.5f + 0.5f * stone, 0.9f, tar);
}

/* late-summer grass: dry, patchy */
static void g_grass(float u, float v, texel *t) {
  float n1 = fbm(u, v, 4, 4, 101), n2 = fbm(u, v, 32, 3, 102), blade = h01((int)(u * N), (int)(v * N), 103);
  float dry = clamp01((fbm(u, v, 3, 3, 104) - 0.35f) * 2);
  float k = 0.75f + 0.3f * n1 + 0.15f * (blade - 0.5f);
  rgb(t, mixf(0.3f, 0.55f, dry) * k, mixf(0.42f, 0.46f, dry) * k, mixf(0.16f, 0.22f, dry) * k);
  t->h = 0.4f + 0.4f * blade * n2;
  t->rough = 0.92f;
  t->ao = 0.75f + 0.25f * blade;
}

/* chain link: a diamond mesh of wire (alpha cut out), galvanised */
static void g_chain(float u, float v, texel *t) {
  float a = fractf((u + v) * 8), b = fractf((u - v) * 8);
  float wa = 1 - clamp01((fabsf(a - 0.5f) - 0.42f) * 40), wb = 1 - clamp01((fabsf(b - 0.5f) - 0.42f) * 40);
  float wire = clamp01(fmaxf(1 - wa, 1 - wb));
  float n = fbm(u, v, 8, 2, 111);
  float c = 0.55f + 0.15f * n;
  rgb(t, c, c, c * 1.02f);
  t->a = wire;
  t->h = wire;
  t->rough = 0.5f;
  t->ao = 1;
}

/* window glass: grime and smears (the material is mostly see-through) */
static void g_glass(float u, float v, texel *t) {
  float smear = clamp01((fbm(u, v, 4, 4, 121) - 0.4f) * 2), dirt = clamp01((fbm(u, v, 2, 3, 122) - 0.5f) * 3) * (1 - v);
  float c = 0.85f - 0.4f * dirt;
  rgb(t, c, c * 0.98f, c * 0.95f);
  t->h = 0.5f;
  t->rough = 0.05f + 0.35f * smear + 0.4f * dirt;
  t->ao = 1;
}

static const gen_fn GEN[TEX_KINDS] = { g_dirt, g_concrete, g_rubber, g_paint, g_rust, g_steel, g_wood, g_gravel, g_grass, g_chain, g_glass, g_primer };
static const float NORMAL_STRENGTH[TEX_KINDS] = { 3, 2.5f, 4, 1, 5, 1.5f, 3, 3, 2, 2, 0.5f, 1.5f };

static tex_set SETS[TEX_KINDS];
uint32_t tex_hash = 2166136261u;
uint32_t tex_each[TEX_KINDS + 2]; /* per texture (the last two: numbers, crack), to find a host that differs */

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

/* a texture set in the making: its texels, filled a band of rows per call */
static texel *WARM_TX;
static int warm_kind = -1, warm_row;
#define WARM_ROWS 48

bool tex_warm(int kind) {
  tex_set *s = &SETS[kind];
  if (s->map) return true;
  if (warm_kind != kind) {
    if (!WARM_TX) WARM_TX = malloc(sizeof *WARM_TX * N * N);
    warm_kind = kind, warm_row = 0;
  }
  texel *tx = WARM_TX;
  int y1 = warm_row + WARM_ROWS < N ? warm_row + WARM_ROWS : N;
  for (int y = warm_row; y < y1; y++)
    for (int x = 0; x < N; x++) {
      texel *t = &tx[y * N + x];
      memset(t, 0, sizeof *t);
      t->a = 1;
      GEN[kind]((x + 0.5f) / N, (y + 0.5f) / N, t);
    }
  warm_row = y1;
  if (warm_row < N) return false;
  warm_kind = -1;
  uint8_t *a = malloc(N * N * 4), *nm = malloc(N * N * 4), *ro = malloc(N * N * 4), *ao = malloc(N * N * 4);
  float st = NORMAL_STRENGTH[kind];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      const texel *t = &tx[y * N + x];
      int i = (y * N + x) * 4;
      a[i] = b8(t->r), a[i + 1] = b8(t->g), a[i + 2] = b8(t->b), a[i + 3] = b8(t->a);
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
  tex_each[kind] = fnv(ro, N * N * 4, fnv(nm, N * N * 4, fnv(a, N * N * 4, 2166136261u)));
  s->map = finish(a, true);
  s->normal = finish(nm, false);
  s->rough = finish(ro, false);
  s->ao = finish(ao, false);
  free(a);
  free(nm);
  free(ro);
  free(ao);
  return true;
}

const tex_set *tex_get(int kind) {
  while (!tex_warm(kind)) {}
  return &SETS[kind];
}

void tex_apply(t3_material *m, int kind, float repeat, float normal_scale, float ao_intensity) {
  tex_apply_uv(m, kind, repeat, repeat, normal_scale, ao_intensity);
}

void tex_apply_uv(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity) {
  const tex_set *s = tex_get(kind);
  if (ru != 1 || rv != 1) { /* three.js keeps the repeat on the texture: a repeated copy */
    t3_texture *ts[4] = { s->map, s->normal, s->rough, s->ao };
    t3_map_slot slots[4] = { T3_MAP, T3_NORMAL_MAP, T3_ROUGHNESS_MAP, T3_AO_MAP };
    for (int i = 0; i < 4; i++) {
      t3_texture *c = finish(ts[i]->pixels, i == 0);
      c->repeat = (t3_vec2){ ru, rv };
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

/* ── race numbers: sprayed on by hand ──────────────────────────────── */

#define NT 128
static t3_texture *NUMS[100];
static t3_texture *CRACK;

/* seven segments: a b c d e f g (top, top right, bottom right, bottom,
 * bottom left, top left, middle) */
static const uint8_t SEG7[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
/* distance from p to segment ab */
static float seg_d(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  float qx = ax + dx * t - px, qy = ay + dy * t - py;
  return sqrtf(qx * qx + qy * qy);
}

/* a number sprayed with a can: wobbly strokes of uneven width, overspray
 * speckle round them, a drip or two running down. White or yellow paint
 * (whichever reads on the car), the rest transparent */
t3_texture *tex_number(int n, const float paint[3]) {
  n = ((n % 100) + 100) % 100;
  if (NUMS[n]) return NUMS[n];
  bool dark = paint[0] * 0.3f + paint[1] * 0.6f + paint[2] * 0.1f < 0.5f;
  uint8_t *px = malloc(NT * NT * 4);
  int digits = n >= 10 ? 2 : 1;
  uint32_t seed = 400u + (uint32_t)n;
  float cr = dark ? 0.92f : 0.06f, cg = dark ? 0.9f : 0.06f, cb = dark ? 0.85f : 0.055f; /* white on dark paint, black on light */
  for (int y = 0; y < NT; y++)
    for (int x = 0; x < NT; x++) {
      float u = (x + 0.5f) / NT, v = (y + 0.5f) / NT;
      float cw = digits == 2 ? 0.36f : 0.5f, ch = 0.78f, gap = 0.06f, total = digits * cw + (digits - 1) * gap;
      float x0 = 0.5f - total * 0.5f, y0 = 0.5f - ch * 0.5f;
      float dmin = 9;
      for (int d = 0; d < digits; d++) {
        int dig = digits == 2 ? (d == 0 ? n / 10 : n % 10) : n;
        float cx = x0 + d * (cw + gap);
        /* the hand: the cell skews and the stroke wobbles */
        float lx = (u - cx) / cw + (v - 0.5f) * 0.12f, ly = (v - y0) / ch;
        lx += (fbm(u, v, 4, 2, seed + 1) - 0.5f) * 0.12f;
        ly += (fbm(u, v, 4, 2, seed + 2) - 0.5f) * 0.08f;
        static const float SG[7][4] = { { 0.1f, 0.05f, 0.9f, 0.05f }, { 0.9f, 0.05f, 0.9f, 0.5f }, { 0.9f, 0.5f, 0.9f, 0.95f }, { 0.1f, 0.95f, 0.9f, 0.95f },
                                        { 0.1f, 0.5f, 0.1f, 0.95f }, { 0.1f, 0.05f, 0.1f, 0.5f }, { 0.1f, 0.5f, 0.9f, 0.5f } };
        for (int s2 = 0; s2 < 7; s2++)
          if (SEG7[dig] & (1 << s2)) dmin = fminf(dmin, seg_d(lx, ly, SG[s2][0], SG[s2][1], SG[s2][2], SG[s2][3]));
      }
      float width = 0.085f + 0.035f * fbm(u, v, 6, 2, seed + 3); /* in cell units */
      bool on = dmin < width;
      /* overspray: speckle just outside the stroke */
      if (!on && dmin < width * 1.9f && h01(x, y, seed + 4) > 0.55f + (dmin - width) / width * 0.6f) on = true;
      /* drips: a few columns running down from the strokes */
      uint32_t col = (uint32_t)(x / 3);
      if (!on && h01((int)col, 0, seed + 5) > 0.93f) {
        float start = 0.3f + 0.4f * h01((int)col, 1, seed + 5), len = 0.08f + 0.15f * h01((int)col, 2, seed + 5);
        if (v > start && v < start + len && x % 3 != 2) on = true;
      }
      int i = (y * NT + x) * 4;
      float g = 0.85f + 0.15f * h01(x, y, seed + 6);
      px[i] = b8(cr * g), px[i + 1] = b8(cg * g), px[i + 2] = b8(cb * g), px[i + 3] = on ? 255 : 0;
    }
  t3_texture *t = t3_texture_new(NT, NT, px);
  t->wrap_s = t->wrap_t = T3_CLAMP_TO_EDGE;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->color_space = T3_SRGB_COLOR_SPACE;
  tex_hash = fnv(px, NT * NT * 4, tex_hash);
  tex_each[TEX_KINDS] ^= fnv(px, NT * NT * 4, 2166136261u);
  free(px);
  NUMS[n] = t;
  return t;
}

/* a cracked windscreen: a star of cracks from a hit low on the driver's
 * side, rings round it, a few long runs across; light lines on clear glass */
/* the crack is made a band of rows per call too (the warm-up) */
static uint8_t *CRACK_PX;
static int crack_row;
bool tex_crack_warm(void) {
  if (CRACK) return true;
  const int S = 256;
  if (!CRACK_PX) CRACK_PX = malloc((size_t)S * S * 4), crack_row = 0;
  uint8_t *px = CRACK_PX;
  float cx = 0.7f, cy = 0.62f;
  int y1 = crack_row + 32 < S ? crack_row + 32 : S;
  for (int y = crack_row; y < y1; y++)
    for (int x = 0; x < S; x++) {
      float u = (x + 0.5f) / S, v = (y + 0.5f) / S, dx = u - cx, dy = v - cy, r = sqrtf(dx * dx + dy * dy);
      float a = cw_atan2(dy, dx), line = 0;
      /* radial cracks: 11 wobbly spokes */
      for (int k = 0; k < 11; k++) {
        float ak = -3.1415927f + 6.2831853f * k / 11 + (h01(k, 0, 501) - 0.5f) * 0.4f;
        float da = fabsf(cw_wrap_angle(a - ak - (fbm(u, v, 8, 2, 502 + (uint32_t)k) - 0.5f) * 0.3f));
        float reach = 0.25f + 0.6f * h01(k, 1, 501);
        if (r < reach) line = fmaxf(line, clamp01(1 - da * r * 260));
      }
      /* rings, written out: wasi-sdk clang -O3 -msimd128 vectorises this
       * 3-trip fmaxf loop wrongly (the third ring drops out in a cart) */
      if (r < 0.2f) {
        float r1 = 0.035f + 0.01f * fbm(u, v, 6, 2, 511), r2 = 0.07f + 0.01f * fbm(u, v, 6, 2, 512), r3 = 0.105f + 0.01f * fbm(u, v, 6, 2, 513);
        line = fmaxf(line, clamp01(1 - fabsf(r - r1) * 260));
        line = fmaxf(line, clamp01(1 - fabsf(r - r2) * 260));
        line = fmaxf(line, clamp01(1 - fabsf(r - r3) * 260));
      }
      float smear = fbm(u, v, 3, 3, 520);
      int i = (y * S + x) * 4;
      float c = 0.6f + 0.4f * line;
      px[i] = b8(c * (0.85f + 0.15f * smear)), px[i + 1] = b8(c * (0.87f + 0.13f * smear)), px[i + 2] = b8(c * 0.86f), px[i + 3] = 255;
    }
  crack_row = y1;
  if (crack_row < S) return false;
  CRACK = t3_texture_new(S, S, px);
  CRACK->wrap_s = CRACK->wrap_t = T3_CLAMP_TO_EDGE;
  CRACK->flip_y = false;
  CRACK->generate_mipmaps = true;
  CRACK->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  CRACK->mag_filter = T3_LINEAR;
  CRACK->color_space = T3_SRGB_COLOR_SPACE;
  tex_hash = fnv(px, (size_t)S * S * 4, tex_hash);
  tex_each[TEX_KINDS + 1] = fnv(px, (size_t)S * S * 4, 2166136261u);
  free(px);
  CRACK_PX = NULL;
  return true;
}

/* side glass: the windscreen's grime without the crack (sampled like the
 * crack, so the side windows share the windscreen's program) */
static t3_texture *GRIME;
t3_texture *tex_crack_clean(void) {
  if (GRIME) return GRIME;
  const int S = 64;
  uint8_t *px = malloc((size_t)S * S * 4);
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      float u = (x + 0.5f) / S, v = (y + 0.5f) / S, smear = fbm(u, v, 3, 3, 520), dirt = clamp01((v - 0.6f) * 2) * fbm(u, v, 8, 2, 530);
      int i = (y * S + x) * 4;
      float c = 0.6f * (1 - 0.35f * dirt);
      px[i] = b8(c * (0.85f + 0.15f * smear)), px[i + 1] = b8(c * (0.87f + 0.13f * smear)), px[i + 2] = b8(c * 0.86f), px[i + 3] = 255;
    }
  GRIME = t3_texture_new(S, S, px);
  GRIME->wrap_s = GRIME->wrap_t = T3_CLAMP_TO_EDGE;
  GRIME->flip_y = false;
  GRIME->generate_mipmaps = true;
  GRIME->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  GRIME->mag_filter = T3_LINEAR;
  GRIME->color_space = T3_SRGB_COLOR_SPACE;
  tex_hash = fnv(px, (size_t)S * S * 4, tex_hash);
  free(px);
  return GRIME;
}

t3_texture *tex_crack(void) {
  while (!tex_crack_warm()) {}
  return CRACK;
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
  for (int i = 0; i < 100; i++)
    if (NUMS[i]) t3_release(NUMS[i]), NUMS[i] = NULL;
  if (CRACK) t3_release(CRACK), CRACK = NULL;
  if (GRIME) t3_release(GRIME), GRIME = NULL;
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
