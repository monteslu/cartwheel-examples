/* otium textures, made here at load time (no image files):
 *   the board   an antique map of Italy on parchment: the sea in a faded
 *               verdigris with ripple lines round every coast, the land in
 *               warm vellum washed by region, an inked coastline, dotted
 *               region borders, a double-ruled frame, foxing and stains
 *   the table   dark walnut, planks and grain
 *   stone       pale travertine for the city plinths
 * Integer hashing and + - * / sqrt only, so every host makes the same
 * texels (debug field tex.hash). The board's coast distance and regions are
 * kept for world.c's relief (board_field). */
#include <stdlib.h>
#include <string.h>

#include "game.h"

uint32_t tex_hash = 2166136261u;

static uint32_t hash2(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(int x, int y, uint32_t seed) { return (float)(hash2(x, y, seed) & 0xffffff) / 16777216.0f; }
static float smooth(float t) { return t * t * (3 - 2 * t); }
static int wrapi(int a, int p) { return ((a % p) + p) % p; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
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
static uint8_t b8(float x) { return (uint8_t)(clamp01(x) * 255.0f + 0.5f); }
static uint32_t fnv(const uint8_t *p, size_t n, uint32_t h) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

static t3_texture *finish(int w, int h, const uint8_t *px, bool srgb, bool repeat) {
  t3_texture *t = t3_texture_new(w, h, px);
  t->wrap_s = t->wrap_t = repeat ? T3_REPEAT : T3_CLAMP_TO_EDGE;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  tex_hash = fnv(px, (size_t)w * h * 4, tex_hash);
  return t;
}

/* ── the board's field: land, distance to the coast (degrees), region ── */

#define FN 512 /* the field's resolution (the texture samples it, bilinear) */
static float *F_dist;   /* signed: + on land, - at sea */
static int8_t *F_region;

static void field_lonlat(int x, int y, float *lon, float *lat) {
  *lon = MAP_LON0 + (MAP_LON1 - MAP_LON0) * (x + 0.5f) / FN;
  *lat = MAP_LAT1 - (MAP_LAT1 - MAP_LAT0) * (y + 0.5f) / FN;
}
static void field_build(void) {
  if (F_dist) return;
  F_dist = malloc(sizeof(float) * FN * FN);
  F_region = malloc(FN * FN);
  uint8_t *land = malloc(FN * FN);
  for (int y = 0; y < FN; y++)
    for (int x = 0; x < FN; x++) {
      float lon, lat;
      field_lonlat(x, y, &lon, &lat);
      land[y * FN + x] = map_on_land(lon, lat);
    }
  /* chamfer distance (in cells) to the nearest cell of the other kind, both ways */
  const float BIG = 1e6f, D1 = 1, D2 = 1.41421356f;
  float *d = F_dist;
  for (int pass = 0; pass < 2; pass++) {
    float *t = malloc(sizeof(float) * FN * FN);
    for (int i = 0; i < FN * FN; i++) t[i] = land[i] == (pass == 0) ? BIG : 0;
    for (int y = 0; y < FN; y++)
      for (int x = 0; x < FN; x++) {
        float *c = &t[y * FN + x];
        if (x > 0) *c = fminf(*c, t[y * FN + x - 1] + D1);
        if (y > 0) {
          *c = fminf(*c, t[(y - 1) * FN + x] + D1);
          if (x > 0) *c = fminf(*c, t[(y - 1) * FN + x - 1] + D2);
          if (x < FN - 1) *c = fminf(*c, t[(y - 1) * FN + x + 1] + D2);
        }
      }
    for (int y = FN - 1; y >= 0; y--)
      for (int x = FN - 1; x >= 0; x--) {
        float *c = &t[y * FN + x];
        if (x < FN - 1) *c = fminf(*c, t[y * FN + x + 1] + D1);
        if (y < FN - 1) {
          *c = fminf(*c, t[(y + 1) * FN + x] + D1);
          if (x < FN - 1) *c = fminf(*c, t[(y + 1) * FN + x + 1] + D2);
          if (x > 0) *c = fminf(*c, t[(y + 1) * FN + x - 1] + D2);
        }
      }
    float cell = (MAP_LON1 - MAP_LON0) / FN;
    for (int i = 0; i < FN * FN; i++)
      if (land[i] == (pass == 0)) d[i] = (pass == 0 ? 1 : -1) * (t[i] - 0.5f) * cell;
    free(t);
  }
  /* regions: the nearest city's, on land (lat squashed like the board) */
  for (int y = 0; y < FN; y++)
    for (int x = 0; x < FN; x++) {
      float lon, lat;
      field_lonlat(x, y, &lon, &lat);
      int best = -1;
      float bd = 1e9f;
      for (int c = 1; c < N_CITIES; c++) {
        float dx = lon - CITIES[c].lon, dy = (lat - CITIES[c].lat) * 1.3f;
        float dd = dx * dx + dy * dy;
        if (dd < bd) bd = dd, best = c;
      }
      F_region[y * FN + x] = land[y * FN + x] && bd < 2.2f ? (int8_t)CITIES[best].region : -1;
    }
  free(land);
}

/* the coast distance at a point (degrees, + on land), bilinear */
float board_coast_dist(float lon, float lat) {
  field_build();
  float fx = (lon - MAP_LON0) / (MAP_LON1 - MAP_LON0) * FN - 0.5f, fy = (MAP_LAT1 - lat) / (MAP_LAT1 - MAP_LAT0) * FN - 0.5f;
  int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
  float tx = fx - x0, ty = fy - y0;
  int xa = x0 < 0 ? 0 : x0 >= FN ? FN - 1 : x0, xb = x0 + 1 < 0 ? 0 : x0 + 1 >= FN ? FN - 1 : x0 + 1;
  int ya = y0 < 0 ? 0 : y0 >= FN ? FN - 1 : y0, yb = y0 + 1 < 0 ? 0 : y0 + 1 >= FN ? FN - 1 : y0 + 1;
  float a = F_dist[ya * FN + xa], b = F_dist[ya * FN + xb], c = F_dist[yb * FN + xa], d = F_dist[yb * FN + xb];
  return mixf(mixf(a, b, tx), mixf(c, d, tx), ty);
}
static int board_region(float lon, float lat) {
  int x = (int)((lon - MAP_LON0) / (MAP_LON1 - MAP_LON0) * FN), y = (int)((MAP_LAT1 - lat) / (MAP_LAT1 - MAP_LAT0) * FN);
  if (x < 0 || y < 0 || x >= FN || y >= FN) return -1;
  return F_region[y * FN + x];
}

/* muted washes, one per region */
static const float REGION_WASH[MAX_REGIONS][3] = {
  { 0.80f, 0.62f, 0.42f }, { 0.62f, 0.70f, 0.48f }, { 0.78f, 0.58f, 0.56f }, { 0.70f, 0.66f, 0.40f },
  { 0.62f, 0.64f, 0.72f }, { 0.80f, 0.54f, 0.40f }, { 0.70f, 0.60f, 0.72f }, { 0.52f, 0.68f, 0.64f },
};

#define BN 2048
t3_texture *tex_board(uint32_t *hash) {
  field_build();
  uint8_t *px = malloc(BN * BN * 4);
  float cell = (MAP_LON1 - MAP_LON0) / BN;
  for (int y = 0; y < BN; y++)
    for (int x = 0; x < BN; x++) {
      float u = (x + 0.5f) / BN, v = (y + 0.5f) / BN;
      float lon = MAP_LON0 + (MAP_LON1 - MAP_LON0) * u, lat = MAP_LAT1 - (MAP_LAT1 - MAP_LAT0) * v;
      float d = board_coast_dist(lon, lat);
      float paper = fbm(u, v, 8, 5, 11), fibre = fbm(u * 3, v * 7, 16, 3, 12);
      float r, g, b;
      if (d > 0) { /* land: vellum, washed by region near its cities */
        r = 0.84f, g = 0.74f, b = 0.55f;
        int reg = board_region(lon, lat);
        if (reg >= 0) {
          float k = 0.38f;
          /* the wash pools darker along the region's edges, like a brush stroke drying */
          int rx = board_region(lon + cell * 6, lat), ry = board_region(lon, lat + cell * 6);
          int lx = board_region(lon - cell * 6, lat), ly = board_region(lon, lat - cell * 6);
          if (rx != reg || ry != reg || lx != reg || ly != reg) k = 0.62f;
          r = mixf(r, REGION_WASH[reg][0], k), g = mixf(g, REGION_WASH[reg][1], k), b = mixf(b, REGION_WASH[reg][2], k);
        }
        /* hatched hills inland */
        float inland = clamp01((d - 0.25f) / 0.8f);
        float hatch = fabsf((u * 220 + v * 140) - floorf(u * 220 + v * 140) - 0.5f);
        float hills = fbm(u, v, 12, 3, 31);
        if (hills > 0.58f && hatch < 0.12f) r *= 1 - 0.14f * inland, g *= 1 - 0.14f * inland, b *= 1 - 0.14f * inland;
        /* region borders: dotted sepia */
        int reg2 = board_region(lon + cell * 2, lat), reg3 = board_region(lon, lat + cell * 2);
        if (reg >= 0 && ((reg2 >= 0 && reg2 != reg) || (reg3 >= 0 && reg3 != reg))) {
          float dot = fmodf(u * 400 + v * 400, 1.0f);
          if (dot < 0.55f) r = 0.42f, g = 0.28f, b = 0.18f;
        }
      } else { /* sea: faded verdigris, deeper away from shore, ripple lines round the coasts */
        float deep = clamp01(-d / 1.6f);
        r = mixf(0.60f, 0.40f, deep), g = mixf(0.76f, 0.60f, deep), b = mixf(0.74f, 0.66f, deep);
        for (int k = 1; k <= 4; k++) {
          float ring = fabsf(-d - k * 0.085f);
          if (ring < 0.010f) {
            float a = 0.45f - k * 0.08f;
            r = mixf(r, 0.30f, a), g = mixf(g, 0.40f, a), b = mixf(b, 0.38f, a);
          }
        }
        /* faint wave strokes out at sea */
        float wv = fbm(u, v, 24, 2, 41);
        float stroke = fabsf(fmodf(v * 160 + wv * 6, 1.0f) - 0.5f);
        if (deep > 0.5f && stroke < 0.05f && fbm(u, v, 10, 2, 42) > 0.6f) r *= 0.92f, g *= 0.94f, b *= 0.94f;
      }
      /* the coastline in ink */
      float ink = clamp01(1 - fabsf(d) / (cell * 2.4f));
      r = mixf(r, 0.24f, ink * 0.9f), g = mixf(g, 0.16f, ink * 0.9f), b = mixf(b, 0.10f, ink * 0.9f);
      /* parchment: fibres, foxing, a darker rim */
      float k = 0.9f + 0.12f * paper + 0.04f * (fibre - 0.5f);
      float fox = fbm(u, v, 6, 4, 77);
      if (fox > 0.7f) k *= 1 - (fox - 0.7f) * 0.9f;
      float ex = fminf(u, 1 - u), ey = fminf(v, 1 - v), edge = fminf(ex, ey);
      k *= 0.72f + 0.28f * clamp01(edge / 0.06f);
      r *= k, g *= k * 0.985f, b *= k * 0.95f;
      /* the frame: two ruled lines inside the margin */
      float fr = edge;
      if ((fr > 0.012f && fr < 0.0145f) || (fr > 0.019f && fr < 0.0205f)) r = 0.3f, g = 0.2f, b = 0.12f;
      if (fr < 0.012f) r *= 0.9f, g *= 0.85f, b *= 0.78f;
      uint8_t *o = &px[(y * BN + x) * 4];
      o[0] = b8(r), o[1] = b8(g), o[2] = b8(b), o[3] = 255;
    }
  t3_texture *t = finish(BN, BN, px, true, false);
  if (hash) *hash = fnv(px, BN * BN * 4, 2166136261u);
  free(px);
  return t;
}

/* the parchment's fibres as a normal map (tiled) */
t3_texture *tex_normal_board(void) {
  enum { N = 256 };
  float *h = malloc(sizeof(float) * N * N);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) h[y * N + x] = fbm((x + 0.5f) / N, (y + 0.5f) / N, 16, 4, 91) * 0.7f + fbm((x + 0.5f) / N * 2, (y + 0.5f) / N * 9, 8, 2, 92) * 0.3f;
  uint8_t *px = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float dx = h[y * N + (x + 1) % N] - h[y * N + (x + N - 1) % N], dy = h[((y + 1) % N) * N + x] - h[((y + N - 1) % N) * N + x];
      float nx = -dx * 3, ny = -dy * 3, nz = 1, l = sqrtf(nx * nx + ny * ny + nz * nz);
      uint8_t *o = &px[(y * N + x) * 4];
      o[0] = b8(nx / l * 0.5f + 0.5f), o[1] = b8(ny / l * 0.5f + 0.5f), o[2] = b8(nz / l * 0.5f + 0.5f), o[3] = 255;
    }
  t3_texture *t = finish(N, N, px, false, true);
  free(h);
  free(px);
  return t;
}

/* walnut planks along u */
t3_texture *tex_wood(uint32_t *hash) {
  enum { N = 512 };
  uint8_t *px = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
      int plank = (int)floorf(v * 4);
      float pv = v * 4 - plank;
      float off = h01(plank, 0, 5);
      float grain = fbm(u * 0.5f + off, pv * 0.25f, 4, 4, 51 + (uint32_t)plank);
      float rings = fabsf(fmodf(grain * 18 + pv * 2.5f, 1.0f) - 0.5f) * 2;
      float k = 0.75f + 0.25f * rings + 0.1f * (fbm(u, v, 32, 2, 52) - 0.5f);
      float r = 0.30f * k, g = 0.18f * k, b = 0.10f * k;
      if (pv < 0.012f || pv > 0.988f) r *= 0.45f, g *= 0.45f, b *= 0.45f; /* the seams */
      uint8_t *o = &px[(y * N + x) * 4];
      o[0] = b8(r), o[1] = b8(g), o[2] = b8(b), o[3] = 255;
    }
  t3_texture *t = finish(N, N, px, true, true);
  if (hash) *hash = fnv(px, N * N * 4, 2166136261u);
  free(px);
  return t;
}

t3_texture *tex_stone(uint32_t *hash) {
  enum { N = 256 };
  uint8_t *px = malloc(N * N * 4);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
      float n = fbm(u, v, 8, 5, 61), pits = fbm(u, v, 32, 2, 62);
      float band = fabsf(fmodf(v * 6 + n * 1.5f, 1.0f) - 0.5f);
      float k = 0.82f + 0.12f * n - (pits > 0.72f ? 0.12f : 0) - (band < 0.04f ? 0.06f : 0);
      uint8_t *o = &px[(y * N + x) * 4];
      o[0] = b8(0.90f * k), o[1] = b8(0.85f * k), o[2] = b8(0.76f * k), o[3] = 255;
    }
  t3_texture *t = finish(N, N, px, true, true);
  if (hash) *hash = fnv(px, N * N * 4, 2166136261u);
  free(px);
  return t;
}

/* ── relief: the board's height at a point, for world.c ────────────── */

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
  t = clamp01(t);
  float ex = px - (ax + dx * t), ey = py - (ay + dy * t);
  return sqrtf(ex * ex + ey * ey);
}
/* the Apennines down the peninsula, the Alps across the top (lon, lat) */
static const float SPINE[] = { 8.6f, 44.45f, 10.2f, 44.25f, 11.6f, 43.85f, 12.9f, 42.9f, 13.7f, 42.2f, 14.6f, 41.5f, 15.5f, 40.6f, 16.0f, 39.6f, 16.2f, 38.6f };
static const float ALPS[] = { 6.6f, 45.0f, 7.0f, 45.9f, 8.2f, 46.3f, 10.0f, 46.4f, 11.8f, 46.5f, 13.4f, 46.4f };
static float ridge(const float *p, int n, float lon, float lat) {
  float best = 9;
  for (int i = 0; i + 1 < n; i++) best = fminf(best, seg_dist(lon, lat * 1.3f, p[i * 2], p[i * 2 + 1] * 1.3f, p[i * 2 + 2], p[i * 2 + 3] * 1.3f));
  return best;
}
float board_height(float lon, float lat, float *mountain) {
  float d = board_coast_dist(lon, lat);
  if (mountain) *mountain = 0;
  if (d <= 0) return -0.02f * clamp01(-d / 0.3f);
  float u = (lon - MAP_LON0) / (MAP_LON1 - MAP_LON0), v = (MAP_LAT1 - lat) / (MAP_LAT1 - MAP_LAT0);
  float shore = smooth(clamp01(d / 0.35f));
  float hills = fbm(u, v, 10, 4, 33);
  float h = 0.03f + shore * (0.035f + 0.07f * hills);
  float sp = ridge(SPINE, sizeof SPINE / sizeof(float) / 2, lon, lat), al = ridge(ALPS, sizeof ALPS / sizeof(float) / 2, lon, lat);
  float m = fmaxf(clamp01(1 - sp / 0.55f) * 0.8f, clamp01(1 - al / 0.7f));
  m *= 0.6f + 0.6f * fbm(u, v, 20, 3, 34);
  h += shore * m * 0.16f;
  if (mountain) *mountain = m * shore;
  return h;
}
