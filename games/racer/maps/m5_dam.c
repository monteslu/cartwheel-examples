/* map: Spillway Dam. A colossal concrete dam in a red canyon at bright midday: the start is on the crest with the
 * reservoir on the left, the road then runs down the spillway beside the thundering water, through a flooded
 * tunnel with a corkscrew, over the tailrace on a jump, up the canyon wall and round a loop on the rim.
 *
 * The world is built around the road from anchors found in the track (never from numbers typed in), so the course
 * can be tuned without redoing the scenery: the crest is whatever stretch of road runs along x = 0, the spillway is
 * the first long descent, and so on. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/base.h"
#include "cartwheel/math.h"
#include "../game.h"
#include "../props.h"

static const piece SPEC[] = {
  WALLS(2, 2),
  ITEMS(),
  STRAIGHT(150),
  CHECK(),
  BOOST(30),
  ASTRAIGHT(100),
  BANKED(90, 130, 12),
  STRAIGHT(30),
  SLOPE(80, -16),
  ITEMS(),
  STRAIGHT(100),
  SLOPE(60, 0),
  CHECK(),
  BANKED(90, 130, 12),
  TUNNEL(1),
  WATER(70),
  STRAIGHT(70),
  WIDTH(30, 4.6f),
  CORKSCREW(190, -1),
  WIDTH(30, 10.0f),
  STRAIGHT(40),
  TUNNEL(0),
  BOOST(30),
  STRAIGHT(40),
  SLOPE(36, 20),
  SLOPE(22, 20),
  JUMP(0, 31),
  SLOPE(80, 0),
  ASTRAIGHT(40),
  ITEMS(),
  ABANKED(90, 130, 12),
  SLOPE(100, 16),
  STRAIGHT(100),
  SLOPE(80, 0),
  STRAIGHT(40),
  BOOST(30),
  STRAIGHT(40),
  LOOP(26, 1),
  STRAIGHT(40),
  CHECK(),
  ABANKED(90, 130, 12),
  ASTRAIGHT(100),
  ASLOPE(50, 0),
  ASTRAIGHT(100),
  STRAIGHT(40),
  END(),
};

#define MAP_SPEC (&(track_spec){ .name = "dam", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 10, .circuit = true })

/* ── small helpers ─────────────────────────────────────────────────────────────────────────────────── */

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static float sm(float a, float b, float x) { return cw_smoothstep(a, b, x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float fracf(float x) { return x - floorf(x); }
static uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static float hashf(uint32_t x) { return (float)(hash32(x) & 0xffffff) / 16777215.0f; }
static float hash2f(int a, int b, uint32_t seed) { return hashf((uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ seed * 83492791u); }

/* ── where things are: anchors found in the track ─────────────────────────────────────────────────── */

typedef struct anchors {
  const track *t;
  float zN, zS;          /* the crest runs along x = 0 between these z (north end, south end) */
  int spill0, spill1;    /* the long steep descent: first sample, first level sample after it */
  int tun0, tun1;        /* the tunnel (first and last sample with a roof) */
  int gap0, gap1;        /* the jump: last sample before the gap, first after */
  int cork0, cork1;      /* the corkscrew */
  int loop0, loop1;      /* the vertical loop on the rim */
  int climb0, climb1;    /* the climb back up the canyon wall */
  int pool0, pool1;      /* the bottom turn the spillway's water runs into */
  float xc;              /* the canyon road's x */
  float zsp;             /* the spillway road's z (at its top) */
  float yfloor;          /* the canyon floor */
  float yrim;            /* the rim and the dam crest's level at the finish end */
  float xhead;           /* x of the top of the spillway */
} anchors;
static anchors A;
static cw_node SUN_NODE;

static void find_anchors(const track *t) {
  memset(&A, 0, sizeof A);
  A.t = t;
  int n = t->n;
  /* the crest's north end: the first sample off the x = 0 line */
  int i = 0;
  while (i < n && fabsf(t->s[i].p.x) < 1.0f) i++;
  A.zN = t->s[i > 0 ? i - 1 : 0].p.z;
  /* its south end: walk back from the finish while the road stays on x = 0 */
  int j = n - 1;
  while (j > 0 && fabsf(t->s[j].p.x) < 1.0f) j--;
  A.zS = t->s[j + 1 < n ? j + 1 : n - 1].p.z;
  /* the spillway: pitch below -9 degrees */
  A.spill0 = A.spill1 = -1;
  for (int k = 0; k < n; k++) {
    float py = t->s[k].f.y;
    if (A.spill0 < 0 && py < -0.15f) A.spill0 = k;
    if (A.spill0 >= 0 && A.spill1 < 0 && k > A.spill0 + 20 && py > -0.02f) A.spill1 = k;
  }
  if (A.spill0 < 0) A.spill0 = n / 10;
  if (A.spill1 < 0) A.spill1 = A.spill0 + 250;
  A.tun0 = A.tun1 = -1;
  A.cork0 = A.cork1 = -1;
  A.loop0 = A.loop1 = -1;
  for (int k = 0; k < n; k++) {
    if (t->s[k].flags & TF_TUNNEL) {
      if (A.tun0 < 0) A.tun0 = k;
      A.tun1 = k;
    }
  }
  /* the corkscrew is the loop-flagged run inside the tunnel, the loop the one outside it */
  for (int l = 0; l < t->nloops; l++) {
    int a = t->loop_from[l], b = t->loop_to[l];
    bool in_tunnel = A.tun0 >= 0 && a >= A.tun0 && a <= A.tun1;
    if (in_tunnel) A.cork0 = a, A.cork1 = b;
    else A.loop0 = a, A.loop1 = b;
  }
  if (A.tun0 < 0) A.tun0 = A.tun1 = A.spill1 + 100;
  if (A.cork0 < 0) A.cork0 = A.tun0 + 140, A.cork1 = A.cork0 + 190;
  if (A.loop0 < 0) A.loop0 = n - 600, A.loop1 = A.loop0 + 160;
  A.gap0 = t->ngaps > 0 ? t->gap_from[0] : A.tun1 + 100;
  A.gap1 = t->ngaps > 0 ? t->gap_to[0] : A.gap0 + 31;
  A.xc = t->s[A.tun0].p.x;
  A.zsp = t->s[A.spill0].p.z;
  A.yfloor = t->s[A.tun0].p.y;
  A.yrim = 5.5f;
  A.xhead = t->s[A.spill0].p.x;
  /* the bottom turn: from the end of the descent to the tunnel mouth */
  A.pool0 = A.spill1;
  A.pool1 = A.tun0;
  /* the climb: first sample after the gap that rises */
  A.climb0 = A.climb1 = -1;
  for (int k = A.gap1 + 100; k < n; k++) {
    if (A.climb0 < 0 && t->s[k].f.y > 0.15f) A.climb0 = k;
    if (A.climb0 >= 0 && A.climb1 < 0 && k > A.climb0 + 20 && t->s[k].f.y < 0.02f) A.climb1 = k;
  }
  if (A.climb0 < 0) A.climb0 = A.gap1 + 300;
  if (A.climb1 < 0) A.climb1 = A.climb0 + 200;
}

/* ── the land ───────────────────────────────────────────────────────────────────────────────────────── */

/* distance outside a box on the ground plane, 0 inside */
static float box_dist(float x, float z, float x0, float x1, float z0, float z1) {
  float dx = fmaxf(fmaxf(x0 - x, 0), x - x1), dz = fmaxf(fmaxf(z0 - z, 0), z - z1);
  return sqrtf(dx * dx + dz * dz);
}
/* 1 inside the box, easing to 0 over `edge` metres outside it */
static float box_mask(float x, float z, float x0, float x1, float z0, float z1, float edge) {
  return 1.0f - sm(0, edge, box_dist(x, z, x0, x1, z0, z1));
}

#define LAKE_Y (-3.3f)
#define BED_Y (-26.0f)
enum { KIND_ROCK, KIND_CONCRETE, KIND_CUT, KIND_VERGE, KIND_BED, KIND_TOP };

/* the lake outline (a union of boxes): 1 inside, soft shores */
static float lake_mask(float x, float z, float edge) {
  float m = box_mask(x, z, 8, 235, -330, 300, edge);
  m = fmaxf(m, box_mask(x, z, 8, 150, 250, 372, edge));
  m = fmaxf(m, box_mask(x, z, 8, 125, -548, -250, edge));
  return m;
}

/* the land before the road is cut into it: a rim plateau, a ridge between the lake and the canyon, the downstream
 * valley, the tributary canyon, the lake basin */
static float land0(float x, float z) {
  float yf = A.yfloor - 0.7f;
  float rim = A.yrim - 1.0f;
  float p = rim + ground_noise(x, z, 95, 6.0f, 3, 11) + ground_noise(x, z, 23, 1.6f, 2, 12);
  float ridge = box_mask(x, z, 240, 372, -330, 380, 60);
  p += 34.0f * ridge * (0.85f + 0.3f * ground_noise(x, z, 60, 0.5f, 2, 13) + 0.0f);
  float east = sm(520, 640, x);
  p += 30.0f * east * (0.8f + ground_noise(x, z, 80, 0.4f, 2, 14));
  /* far walls that close the valley in */
  p += 95.0f * sm(-380, -680, x) + 110.0f * sm(620, 900, z) + 90.0f * sm(820, 1200, x) + 70.0f * sm(-650, -900, z);
  float v = fmaxf(box_mask(x, z, -520, -24, -470, 520, 80), box_mask(x, z, -520, 760, 436, 600, 70));
  v = fmaxf(v, box_mask(x, z, 376, 498, -340, 440, 55));
  float floor_n = yf + ground_noise(x, z, 41, 1.3f, 2, 15);
  float h = lerpf(p, floor_n, v);
  float lk = lake_mask(x, z, 28);
  h = lerpf(h, BED_Y + ground_noise(x, z, 50, 2.0f, 2, 16), lk);
  return h;
}

#define SLOPE_K 1.25f /* the cut and fill slopes beside the road: metres of height per metre out */

/* The ground as the road sees it: land0 clamped into a cone about the road (fill below it, cut above it), the
 * spillway's concrete channel beside the descent, a rock mound over the tunnel. kind says what it is made of. */
static float dam_land(float x, float z, int *kind) {
  const track *t = A.t;
  float h0 = land0(x, z);
  int k = KIND_ROCK;
  float ry;
  int idx = 0;
  float g = road_gap_idx(t, x, z, &ry, &idx);
  if (g > 150.0f) {
    if (kind) *kind = h0 > A.yrim + 30 ? KIND_TOP : KIND_ROCK;
    return h0;
  }
  const tsample *sm_ = &t->s[idx];
  float dx = x - sm_->p.x, dz = z - sm_->p.z;
  float lat = dx * sm_->r.x + dz * sm_->r.z;
  float side = lat >= 0 ? 1.0f : -1.0f;
  float ey = sm_->p.y + clampf(lat, -sm_->hw, sm_->hw) * sm_->r.y; /* the road's own height under (or beside) this point */
  float base = ey - 0.4f;
  float gg = fmaxf(g, 0.0f);
  uint32_t fl = sm_->flags;
  float h = h0;
  if (fl & TF_TUNNEL) {
    /* a mound over the tube: tall enough for the corkscrew's roll, slopes off beyond */
    float top = sm_->p.y + 14.0f;
    float lo = top - SLOPE_K * fmaxf(g - 6.0f, 0.0f);
    if (h0 < lo) h = lo;
    if (g < 0) h = fmaxf(h, top);
    if (kind) *kind = KIND_ROCK;
    return h;
  }
  if ((fl & TF_LOOP) || (fl & TF_GAP)) {
    if (kind) *kind = KIND_ROCK;
    return h0;
  }
  float verge = 2.5f;
  float lo = base - SLOPE_K * fmaxf(gg - verge, 0.0f);
  float hi = base + SLOPE_K * fmaxf(gg - verge, 0.0f);
  /* the spillway's channel runs along the left of the descent and the bottom turn */
  bool chute_zone = side < 0 && idx >= A.spill0 - 40 && idx <= A.pool1 - 10;
  if (chute_zone) {
    float bed = ry - 3.6f;
    if (idx > A.spill1) bed = fminf(bed, A.yfloor - 3.6f);
    float c = lerpf(base, bed, sm(1.2f, 5.0f, g));
    if (g > 36.0f) c = lerpf(bed, bed + 9.0f, sm(36.0f, 40.0f, g));
    float up = bed + 9.0f;
    float lo2 = up - 0.3f * fmaxf(g - 40.0f, 0.0f), hi2 = up + SLOPE_K * fmaxf(g - 40.0f, 0.0f);
    if (g <= 40.0f) h = c;
    else h = clampf(h0, lo2, hi2);
    if (g <= 36.0f) k = KIND_CONCRETE;
    else if (g <= 40.0f) k = KIND_CONCRETE;
    if (kind) *kind = k;
    return h;
  }
  h = clampf(h0, lo, hi);
  if (g <= verge) {
    h = base;
    k = KIND_VERGE;
  } else if (h == lo && h0 < lo) {
    /* fill: the dam's own face where the road runs on the crest, an embankment elsewhere */
    k = (idx < A.pool1 + 60 || idx > t->n - 700) ? KIND_CONCRETE : KIND_ROCK;
  } else if (h == hi && h0 > hi) {
    k = KIND_CUT;
  }
  if (kind) *kind = k;
  return h;
}
static float dam_ground_fn(float x, float z, void *ud) {
  (void)ud;
  return dam_land(x, z, NULL);
}

/* ── the terrain mesh: chunked, three resolutions, strata coloured by height ──────────────────────── */

typedef struct tmesh {
  float *p, *n, *uv, *c;
  uint32_t *ix;
  int nv, ni, capv, capi;
} tmesh;
static int tv(tmesh *m, float x, float y, float z, float nx, float ny, float nz, float u, float v, pcol c) {
  if (m->nv == m->capv) {
    m->capv = m->capv ? m->capv * 2 : 1024;
    m->p = realloc(m->p, sizeof(float) * 3 * (size_t)m->capv), m->n = realloc(m->n, sizeof(float) * 3 * (size_t)m->capv);
    m->uv = realloc(m->uv, sizeof(float) * 2 * (size_t)m->capv), m->c = realloc(m->c, sizeof(float) * 3 * (size_t)m->capv);
  }
  int i = m->nv++;
  m->p[i * 3] = x, m->p[i * 3 + 1] = y, m->p[i * 3 + 2] = z;
  m->n[i * 3] = nx, m->n[i * 3 + 1] = ny, m->n[i * 3 + 2] = nz;
  m->uv[i * 2] = u, m->uv[i * 2 + 1] = v;
  m->c[i * 3] = c.r, m->c[i * 3 + 1] = c.g, m->c[i * 3 + 2] = c.b;
  return i;
}
static void tt(tmesh *m, int a, int b, int c) {
  if (m->ni + 3 > m->capi) m->capi = m->capi ? m->capi * 2 : 3072, m->ix = realloc(m->ix, sizeof(uint32_t) * (size_t)m->capi);
  m->ix[m->ni++] = (uint32_t)a, m->ix[m->ni++] = (uint32_t)b, m->ix[m->ni++] = (uint32_t)c;
}
static t3_geometry *tfinish(tmesh *m) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, m->p, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->n, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->uv, m->nv, 2), t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->c, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, m->ix, m->ni, 1), t3_geometry_set_index(g, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(g);
  free(m->p), free(m->n), free(m->uv), free(m->c), free(m->ix);
  memset(m, 0, sizeof *m);
  return g;
}

/* the colour of the land at a point: banded canyon strata on the steep parts, dusty ochre on the flat, concrete
 * on the dam, pale gravel by the road */
static pcol land_colour(float x, float y, float z, float slope, int kind) {
  pcol strata[6] = { rgb(0.80f, 0.42f, 0.22f), rgb(0.66f, 0.26f, 0.16f), rgb(0.88f, 0.62f, 0.40f), rgb(0.52f, 0.28f, 0.19f),
                     rgb(0.92f, 0.74f, 0.52f), rgb(0.74f, 0.36f, 0.20f) };
  float warp = ground_noise(x, z, 38, 3.2f, 2, 21) + ground_noise(x * 0.4f + z * 0.9f, y * 3, 9, 0.5f, 1, 22);
  float yy = (y + warp) / 5.2f;
  int band = (int)floorf(yy);
  float within = yy - floorf(yy);
  pcol c = strata[hash32((uint32_t)(band + 400) * 2654435761u) % 6];
  c = pc_mul(c, within < 0.1f ? 0.72f : 1.0f - 0.18f * within);
  float fine = ground_noise(x * 1.7f, z * 1.7f, 5, 0.5f, 2, 23);
  c = pc_mul(c, 1.0f + 0.35f * fine);
  float steep = sm(0.35f, 1.0f, slope);
  pcol dusty = pc_mix(rgb(0.78f, 0.58f, 0.38f), rgb(0.64f, 0.50f, 0.34f), 0.5f + 0.5f * ground_noise(x, z, 30, 1.0f, 2, 24));
  pcol shrub = rgb(0.46f, 0.47f, 0.24f);
  float patch = sm(0.15f, 0.55f, ground_noise(x, z, 24, 1.0f, 2, 25));
  dusty = pc_mix(dusty, shrub, patch * 0.65f * (1.0f - steep));
  c = pc_mix(dusty, c, steep);
  if (kind == KIND_CONCRETE) {
    pcol conc = pc_mix(rgb(0.70f, 0.69f, 0.65f), rgb(0.56f, 0.55f, 0.52f), 0.5f + 0.5f * fine);
    float lift = fracf(y / 3.0f);
    conc = pc_mul(conc, lift < 0.07f ? 0.8f : 1.0f);
    c = conc;
  } else if (kind == KIND_VERGE) {
    c = pc_mix(rgb(0.74f, 0.68f, 0.58f), c, 0.25f);
  } else if (kind == KIND_CUT) {
    c = pc_mul(c, 0.82f);
  }
  return c;
}

static void terrain_chunk(cw_node group, t3_material *mat, float x0, float z0, float S, int n) {
  float d = S / (float)n;
  int W = n + 3;
  float *hh = malloc(sizeof(float) * (size_t)W * (size_t)W);
  uint8_t *kk = malloc((size_t)W * (size_t)W);
  for (int j = 0; j < W; j++)
    for (int i = 0; i < W; i++) {
      int kd = 0;
      hh[j * W + i] = dam_land(x0 + (float)(i - 1) * d, z0 + (float)(j - 1) * d, &kd);
      kk[j * W + i] = (uint8_t)kd;
    }
  tmesh m = { 0 };
  int base = 0;
  for (int j = 0; j <= n; j++)
    for (int i = 0; i <= n; i++) {
      int k = (j + 1) * W + (i + 1);
      float x = x0 + (float)i * d, z = z0 + (float)j * d;
      float gx = (hh[k + 1] - hh[k - 1]) / (2 * d), gz = (hh[k + W] - hh[k - W]) / (2 * d);
      float l = sqrtf(gx * gx + 1 + gz * gz);
      float slope = sqrtf(gx * gx + gz * gz);
      pcol c = land_colour(x, hh[k], z, slope, kk[k]);
      /* uv: flat ground maps by x, z; steep faces by along-contour distance and height, so cliffs do not smear */
      float u = x / 9.0f, v = z / 9.0f;
      float w = sm(0.8f, 1.8f, slope);
      if (w > 0.001f) {
        float hl = sqrtf(gx * gx + gz * gz) + 1e-4f;
        float along = (x * (-gz) + z * gx) / hl;
        u = lerpf(u, along / 9.0f, w), v = lerpf(v, hh[k] / 9.0f, w);
      }
      int vi = tv(&m, x, hh[k], z, -gx / l, 1 / l, -gz / l, u, v, c);
      if (i == 0 && j == 0) base = vi;
    }
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      int a = base + j * (n + 1) + i, b = a + 1, c = a + n + 1, e = c + 1;
      if (fabsf(hh[(j + 1) * W + (i + 1)] - hh[(j + 2) * W + (i + 2)]) < fabsf(hh[(j + 1) * W + (i + 2)] - hh[(j + 2) * W + (i + 1)])) {
        tt(&m, a, c, e), tt(&m, a, e, b);
      } else {
        tt(&m, a, c, b), tt(&m, b, c, e);
      }
    }
  /* skirts so a seam against a coarser neighbour never shows the sky */
  float drop = 2.0f + S * 0.02f;
  for (int side = 0; side < 4; side++) {
    int prev = -1, prevb = -1;
    for (int s = 0; s <= n; s++) {
      int i = side == 0 ? s : side == 1 ? n : side == 2 ? n - s : 0, j = side == 0 ? 0 : side == 1 ? s : side == 2 ? n : n - s;
      int vtx = base + j * (n + 1) + i;
      int lo = tv(&m, m.p[vtx * 3], m.p[vtx * 3 + 1] - drop, m.p[vtx * 3 + 2], 0, 1, 0, m.uv[vtx * 2], m.uv[vtx * 2 + 1], (pcol){ m.c[vtx * 3] * 0.6f, m.c[vtx * 3 + 1] * 0.6f, m.c[vtx * 3 + 2] * 0.6f });
      if (prev >= 0) tt(&m, prev, prevb, lo), tt(&m, prev, lo, vtx);
      prev = vtx, prevb = lo;
    }
  }
  t3_geometry *g = tfinish(&m);
  t3_mesh *me = t3_mesh_new(g, mat);
  me->base.cast_shadow = false;
  me->base.receive_shadow = true;
  t3_object_add(cw_node3d_object(group), &me->base);
  t3_release(me);
  t3_release(g);
  free(hh), free(kk);
}

static void build_terrain(cw_node parent) {
  const track *t = A.t;
  cw_node group = cw_node_new("Node3D", "Terrain");
  cw_node_add_child(parent, group);
  t3_material *mat = phys((t3_color){ 1, 1, 1 }, 0.95f, 0);
  mat->vertex_colors = true;
  tex_apply(mat, TEX_GROUND_ROCK, 1, 1, 0.9f, 0.8f);
  /* the texture multiplies the vertex colours (strata, dust) and lends the relief; its albedo is dark, so lift it */
  if (mat->map) mat->color = (t3_color){ 2.6f, 2.6f, 2.6f };
  const float CH = 128;
  float X0 = -1280, X1 = 1408, Z0 = -1152, Z1 = 1152;
  int chunks = 0;
  for (float z = Z0; z < Z1; z += CH)
    for (float x = X0; x < X1; x += CH) {
      float dist;
      road_nearest(t, x + CH * 0.5f, z + CH * 0.5f, &dist);
      dist -= CH * 0.71f;
      int n = dist < 90 ? 32 : dist < 330 ? 16 : 8;
      terrain_chunk(group, mat, x, z, CH, n);
      chunks++;
    }
  t3_release(mat);
  cw_info("dam: terrain %d chunks", chunks);
}

/* ── the lake ───────────────────────────────────────────────────────────────────────────────────────── */

static void build_lake(cw_node parent) {
  water_opts w = water_defaults();
  w.color = rgb(0.10f, 0.62f, 0.66f);
  w.deep = rgb(0.01f, 0.20f, 0.30f);
  w.clarity = 0.45f;
  w.wave_height = 0.18f;
  w.wave_length = 26;
  w.choppy = 0.3f;
  w.foam = 0.25f;
  w.reflect = 0.7f;
  w.refract = false;
  w.grid = 80;
  /* two sheets, so no water lies over the spillway channel that runs below the lake level beside the head of the descent */
  water_surface(parent, 79.0f, -88.0f, 142.0f, 920.0f, LAKE_Y, &w);
  water_surface(parent, 193.0f, -15.0f, 86.0f, 630.0f, LAKE_Y, &w);
}

/* RACER_DAM_CHECK=1: report every place the land stands above the road surface (a development check) */
static void check_land(const track *t) {
  int bad = 0;
  for (int i = 0; i < t->n; i += 3) {
    const tsample *s = &t->s[i];
    if (s->flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
    for (int k = -2; k <= 2; k++) {
      float lat = (float)k * 0.5f * s->hw;
      t3_vec3 p = v3add(s->p, v3mul(s->r, lat));
      float h = dam_land(p.x, p.z, NULL), ey = s->p.y + lat * s->r.y;
      if (h > ey - 0.1f) {
        if (bad < 12) cw_info("dam check: s=%.0f lat=%.1f land %.2f road %.2f", s->s, lat, h, ey);
        bad++;
      }
    }
  }
  cw_info("dam check: %d road points under the land", bad);
}

static void dam_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  find_anchors(t);
  cw_info("dam anchors: crest z %.0f..%.0f, spill %d..%d, tunnel %d..%d, cork %d..%d, gap %d..%d, climb %d..%d, loop %d..%d, xc %.0f floor %.1f rim %.1f",
          A.zS, A.zN, A.spill0, A.spill1, A.tun0, A.tun1, A.cork0, A.cork1, A.gap0, A.gap1, A.climb0, A.climb1, A.loop0, A.loop1, A.xc, A.yfloor, A.yrim);
  SUN_NODE = cw_node_get(scene, "Sun");
  cw_node root = cw_node_new("Node3D", "Dam");
  cw_node_add_child(scene, root);
  if (getenv("RACER_DAM_CHECK")) check_land(t);
  build_terrain(root);
  build_lake(root);
}

static void dam_frame(float alpha, double time) {
  (void)alpha;
  static bool said;
  if (!said && time > 1.0 && cw_node_valid(SUN_NODE)) {
    said = true;
    const t3_mat4 *m = cw_node3d_global_transform(SUN_NODE);
    cw_info("dam sun travels toward (%.2f %.2f %.2f)", -m->e[8], -m->e[9], -m->e[10]);
  }
}

const map_def map_dam = {
  .name = "Spillway Dam",
  .blurb = "Down the dam's spillway, through its water tunnel, over the canyon.",
  .scene = "scenes/dam.cwscene",
  .spec = &(track_spec){ .name = "dam", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 10, .circuit = true },
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.74f, 0.73f, 0.70f }, .kerb_a = { 0.95f, 0.78f, 0.1f }, .kerb_b = { 0.08f, 0.08f, 0.09f }, .wall = { 0.88f, 0.86f, 0.82f },
            .wall_top = { 0.95f, 0.93f, 0.88f }, .road_tex = TEX_ROAD_CONCRETE, .wall_tex = TEX_WALL_CONCRETE, .road_rough = 0.8f },
  .laps = 3,
  .par_lap = 60,
  .scenery = dam_scenery,
  .scenery_frame = dam_frame,
};
