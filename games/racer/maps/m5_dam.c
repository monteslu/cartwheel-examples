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
  int gate;              /* the sample at the spillway's gates (where the lake ends and the white water begins) */
} anchors;
static anchors A;
static cw_node SUN_NODE, SCENE_NODE;

static void build_river(const track *t);
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
  A.gate = A.spill0;
  for (int k = A.spill0 - 80; k < A.spill0 + 60; k++)
    if (t->s[k].p.x >= 148.0f) {
      A.gate = k;
      break;
    }
  /* the bottom turn: from the end of the descent to the tunnel mouth */
  A.pool0 = A.spill1;
  A.pool1 = A.tun0;
  build_river(t);
  /* the climb: first sample after the gap that rises */
  A.climb0 = A.climb1 = -1;
  for (int k = A.gap1 + 100; k < n; k++) {
    if (A.climb0 < 0 && t->s[k].f.y > 0.15f) A.climb0 = k;
    if (A.climb0 >= 0 && A.climb1 < 0 && k > A.climb0 + 20 && t->s[k].f.y < 0.02f) A.climb1 = k;
  }
  if (A.climb0 < 0) A.climb0 = A.gap1 + 300;
  if (A.climb1 < 0) A.climb1 = A.climb0 + 200;
}


/* ── the river: the spillway's water runs on down the canyon beside the road, then crosses under the jump ── */

#define MAXRV 480
typedef struct rv_pt {
  t3_vec3 p;   /* the water's surface */
  float hw;    /* half width of the flowing water */
  float cum;   /* metres along the river */
} rv_pt;
static rv_pt RV[MAXRV];
static int NRV;
static float RV_BED = 0.3f; /* the bed lies this far below the water's surface */

static t3_vec3 flat_dir(t3_vec3 v) {
  v.y = 0;
  float l = v3len(v);
  return l > 1e-6f ? v3mul(v, 1.0f / l) : v3(0, 0, 1);
}
static t3_vec3 flat_right(const tsample *s) {
  t3_vec3 f = flat_dir(s->f);
  return v3(-f.z, 0, f.x); /* f x up */
}
static void river_add(t3_vec3 p, float hw) {
  if (NRV >= MAXRV) return;
  rv_pt *r = &RV[NRV];
  r->p = p, r->hw = hw;
  r->cum = NRV ? RV[NRV - 1].cum + v3len(v3sub(p, RV[NRV - 1].p)) : 0;
  NRV++;
}
static t3_vec3 catmull(t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, float t) {
  float t2 = t * t, t3_ = t2 * t;
  t3_vec3 r;
  r.x = 0.5f * ((2 * b.x) + (-a.x + c.x) * t + (2 * a.x - 5 * b.x + 4 * c.x - d.x) * t2 + (-a.x + 3 * b.x - 3 * c.x + d.x) * t3_);
  r.y = 0.5f * ((2 * b.y) + (-a.y + c.y) * t + (2 * a.y - 5 * b.y + 4 * c.y - d.y) * t2 + (-a.y + 3 * b.y - 3 * c.y + d.y) * t3_);
  r.z = 0.5f * ((2 * b.z) + (-a.z + c.z) * t + (2 * a.z - 5 * b.z + 4 * c.z - d.z) * t2 + (-a.z + 3 * b.z - 3 * c.z + d.z) * t3_);
  return r;
}

static void build_river(const track *t) {
  NRV = 0;
  float wy = A.yfloor - 3.3f;
  int gc = (A.gap0 + A.gap1) / 2;
  int end1 = gc - 100;
  if (end1 < A.spill0 + 100) end1 = A.spill0 + 100;
  /* along the left of the road from the gates to a hundred metres short of the jump */
  for (int i = A.gate; i <= end1; i += 5) {
    const tsample *s = &t->s[i];
    t3_vec3 p = v3add(s->p, v3mul(flat_right(s), -36.0f));
    p.y = (i < A.spill1 ? s->p.y : A.yfloor) - 3.3f;
    river_add(p, 15.5f);
  }
  /* then the bend across the road under the jump, in the jump's own frame (d along the road, lat to its right) */
  const tsample *g = &t->s[gc];
  t3_vec3 f = flat_dir(g->f), r = v3(-f.z, 0, f.x);
  static const float L[][3] = { { -60, -36, 15.5f }, { -30, -35, 15.5f }, { -12, -29, 15.5f }, { -3, -15, 15.5f }, { 0, 0, 16 }, { 3, 18, 16.5f },
                                { 10, 45, 17 }, { 28, 80, 18 }, { 60, 115, 19 }, { 110, 145, 21 }, { 180, 165, 24 }, { 270, 175, 28 } };
  int nl = (int)(sizeof L / sizeof L[0]);
  t3_vec3 P[16];
  float HW[16];
  for (int i = 0; i < nl; i++) {
    P[i] = v3add(v3add(g->p, v3mul(f, L[i][0])), v3mul(r, L[i][1]));
    P[i].y = wy - 1.7f * sm(0, 200, L[i][0] + 60.0f);
    HW[i] = L[i][2];
  }
  /* the last point of the first stretch is the first control point's neighbour */
  t3_vec3 pre = NRV > 0 ? RV[NRV - 1].p : P[0];
  for (int i = 0; i < nl - 1; i++) {
    t3_vec3 a = i == 0 ? pre : P[i - 1], b = P[i], c = P[i + 1], d = i + 2 < nl ? P[i + 2] : v3add(P[i + 1], v3sub(P[i + 1], P[i]));
    for (int k = 0; k < 4; k++) {
      float u = (float)k / 4.0f;
      river_add(catmull(a, b, c, d, u), lerpf(HW[i], HW[i + 1], u));
    }
  }
  river_add(P[nl - 1], HW[nl - 1]);
}

/* squared-free: distance on the ground plane from (x, z) to the river's centre line; also the surface height and half width there */
static float river_dist(float x, float z, float *wy, float *hw) {
  float best = 1e18f, by = 0, bw = 0;
  for (int i = 0; i + 1 < NRV; i++) {
    const rv_pt *a = &RV[i], *b = &RV[i + 1];
    float ex = b->p.x - a->p.x, ez = b->p.z - a->p.z;
    float l2 = ex * ex + ez * ez + 1e-6f;
    float u = clampf(((x - a->p.x) * ex + (z - a->p.z) * ez) / l2, 0, 1);
    float px = a->p.x + ex * u - x, pz = a->p.z + ez * u - z, d = px * px + pz * pz;
    if (d < best) best = d, by = lerpf(a->p.y, b->p.y, u), bw = lerpf(a->hw, b->hw, u);
  }
  if (wy) *wy = by;
  if (hw) *hw = bw;
  return sqrtf(best);
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
  m = fmaxf(m, box_mask(x, z, 8, 125, -430, -250, edge));
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
  float v = fmaxf(box_mask(x, z, -520, -24, -640, 520, 80), box_mask(x, z, -520, 760, 436, 600, 70));
  v = fmaxf(v, box_mask(x, z, 376, 498, -340, 440, 55));
  float floor_n = yf + ground_noise(x, z, 41, 1.3f, 2, 15);
  float h = lerpf(p, floor_n, v);
  /* terraces: the walls and mesas step in ledges, as layered red rock does (treads stay flat, risers are steep) */
  if (h > yf + 1.5f) {
    float S = 7.5f + 2.0f * ground_noise(x, z, 130, 1.0f, 1, 17), off = ground_noise(x, z, 55, 0.9f, 2, 18);
    float q = (h - yf) / S + off;
    float fl = floorf(q), fr = q - fl;
    h = yf + (fl + sm(0.55f, 0.92f, fr) - off) * S;
  }
  float lk = lake_mask(x, z, 28);
  h = lerpf(h, BED_Y + ground_noise(x, z, 50, 2.0f, 2, 16), lk);
  return h;
}

#define SLOPE_K 1.25f /* the cut and fill slopes beside the road: metres of height per metre out */

/* The ground as the road sees it: land0 clamped into a cone about the road (fill below it, cut above it), the
 * spillway's concrete channel beside the descent, a rock mound over the tunnel. kind says what it is made of. */
static float land_road(float x, float z, int *kind) {
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
    float c = lerpf(base, bed, sm(1.2f, 9.0f, g));
    if (g > 40.0f) c = lerpf(bed, bed + 9.0f, sm(40.0f, 44.0f, g));
    float up = bed + 9.0f;
    float lo2 = up - 0.3f * fmaxf(g - 44.0f, 0.0f), hi2 = up + SLOPE_K * fmaxf(g - 44.0f, 0.0f);
    if (g <= 44.0f) h = c;
    else h = clampf(h0, lo2, hi2);
    if (g <= 44.0f) k = KIND_CONCRETE;
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
/* the land with the river carved into it */
static float dam_land(float x, float z, int *kind) {
  int k = KIND_ROCK;
  float h = land_road(x, z, &k);
  float wy, hw;
  float d = river_dist(x, z, &wy, &hw);
  if (d < hw + 5.0f) {
    float bed = wy - RV_BED, tb = sm(hw, hw + 5.0f, d);
    float rh = lerpf(bed, h, tb);
    if (rh < h) h = rh, k = tb < 0.5f ? KIND_BED : k;
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
      float u = x / 14.0f, v = z / 14.0f;
      float w = sm(0.8f, 1.8f, slope);
      if (w > 0.001f) {
        float hl = sqrtf(gx * gx + gz * gz) + 1e-4f;
        float along = (x * (-gz) + z * gx) / hl;
        u = lerpf(u, along / 14.0f, w), v = lerpf(v, hh[k] / 14.0f, w);
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

static t3_material *TMAT;
static void build_terrain(cw_node parent) {
  const track *t = A.t;
  cw_node group = cw_node_new("Node3D", "Terrain");
  cw_node_add_child(parent, group);
  t3_material *mat = phys((t3_color){ 1, 1, 1 }, 0.95f, 0);
  mat->vertex_colors = true;
  tex_apply(mat, TEX_GROUND_ROCK, 1, 1, 0.5f, 0.8f);
  TMAT = t3_retain(mat);
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


/* ── white water: a ribbon along the river, a scrolling foam texture, standing waves ─────────────────── */

/* value noise on a lattice that wraps every px, py cells, so the texture tiles along the flow */
static float tvn(float x, float y, int px, int py, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - (float)x0, fy = y - (float)y0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  int xa = ((x0 % px) + px) % px, xb = (xa + 1) % px, ya = ((y0 % py) + py) % py, yb = (ya + 1) % py;
  float a = hash2f(xa, ya, seed), b = hash2f(xb, ya, seed), c = hash2f(xa, yb, seed), d = hash2f(xb, yb, seed);
  return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}

/* kind 0: streaks stretched along the flow (chutes and rapids); kind 1: churned clumps (the plunge pool) */
static t3_texture *make_foam(int kind, uint32_t seed) {
  const int W = 256, H = 512;
  uint8_t *px = malloc((size_t)W * (size_t)H * 4);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float u = ((float)x + 0.5f) / (float)W, v = ((float)y + 0.5f) / (float)H;
      float f;
      if (kind == 0) {
        f = 0.45f * tvn(u * 14, v * 4, 14, 4, seed) + 0.3f * tvn(u * 36, v * 10, 36, 10, seed + 1) + 0.25f * tvn(u * 80, v * 26, 80, 26, seed + 2);
      } else {
        f = 0.45f * tvn(u * 8, v * 16, 8, 16, seed) + 0.3f * tvn(u * 20, v * 40, 20, 40, seed + 1) + 0.25f * tvn(u * 48, v * 96, 48, 96, seed + 2);
      }
      float foam = sm(0.40f, 0.60f, f);
      float core = sm(0.56f, 0.72f, f);
      float edge = sm(0.0f, 0.16f, u) * sm(0.0f, 0.16f, 1.0f - u);
      float rim = 1.0f - sm(0.0f, 0.32f, fminf(u, 1.0f - u)); /* foam gathers at the banks */
      foam = clampf(foam + rim * 0.5f, 0, 1);
      float spark = hash2f(x, y, seed + 9) > 0.992f ? 1.0f : 0.0f;
      float r = lerpf(0.10f, 1.0f, foam), g = lerpf(0.52f, 1.0f, foam), b = lerpf(0.58f, 1.0f, foam);
      r = lerpf(r, 1, core * 0.5f), g = lerpf(g, 1, core * 0.5f), b = lerpf(b, 1, core * 0.5f);
      float a = (0.58f + 0.42f * foam) * edge;
      a = fmaxf(a, spark * edge);
      uint8_t *o = px + ((size_t)y * (size_t)W + (size_t)x) * 4;
      o[0] = (uint8_t)(clampf(r, 0, 1) * 255), o[1] = (uint8_t)(clampf(g, 0, 1) * 255), o[2] = (uint8_t)(clampf(b, 0, 1) * 255), o[3] = (uint8_t)(clampf(a, 0, 1) * 255);
    }
  t3_texture *t = t3_texture_new(W, H, px);
  free(px);
  t->wrap_s = T3_REPEAT;
  t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}

#define MAX_SCROLL 4
static struct {
  t3_texture *tex[MAX_SCROLL];
  float rate[MAX_SCROLL]; /* texture repeats per second */
  int n;
} SC;

static t3_material *foam_material(t3_texture *tex, float bright) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_map(m, tex);
  m->vertex_colors = true;
  m->transparent = true;
  m->depth_write = false;
  m->side = T3_DOUBLE_SIDE;
  m->color = (t3_color){ bright, bright, bright };
  return m;
}

/* A ribbon over pts[0..n): half widths hw[i], `cols` quads across, standing waves of amplitude amp and wavelength wl,
 * the foam texture laid along the flow (vscale metres per repeat at ordinary places, stretched by vstretch[i]).
 * Colours carry a fake light: the faces that look at the sun are brighter. */
static void flow_ribbon(cw_node parent, const char *name, const t3_vec3 *pts, const float *hw, const float *vst, int n, int cols, float amp, float wl,
                        t3_material *mat, t3_vec3 sun_to) {
  tmesh m = { 0 };
  float cum = 0, v = 0;
  for (int i = 0; i < n; i++) {
    t3_vec3 tg = v3norm(v3sub(pts[i + 1 < n ? i + 1 : i], pts[i > 0 ? i - 1 : i]));
    t3_vec3 side = v3norm(v3cross(v3(0, 1, 0), tg));
    t3_vec3 nrm = v3norm(v3cross(tg, side));
    if (nrm.y < 0) nrm = v3mul(nrm, -1);
    if (i > 0) {
      float l = v3len(v3sub(pts[i], pts[i - 1]));
      cum += l;
      v += l / vst[i];
    }
    for (int c = 0; c <= cols; c++) {
      float fr = (float)c / (float)cols, lat = (fr - 0.5f) * 2.0f * hw[i];
      float edge = 1.0f - sm(0.0f, 0.16f, fminf(fr, 1.0f - fr));
      float ph = cum / wl * 6.2831853f + 1.7f * sinf(fr * 9.0f + cum * 0.05f);
      float wave = amp * (0.55f * sinf(ph) + 0.45f * sinf(ph * 1.9f + fr * 5.0f)) * (1.0f - 0.7f * edge);
      float dw = amp * (0.55f * cosf(ph) + 0.85f * cosf(ph * 1.9f + fr * 5.0f)) * 6.2831853f / wl; /* slope along the flow */
      t3_vec3 p = v3add(v3add(pts[i], v3mul(side, lat)), v3mul(nrm, wave));
      t3_vec3 nn = v3norm(v3sub(nrm, v3mul(tg, dw)));
      float lit = clampf(0.78f + 0.45f * (-v3dot(nn, sun_to)), 0.55f, 1.25f);
      pcol col = { lit, lit, lit };
      tv(&m, p.x, p.y, p.z, nn.x, nn.y, nn.z, fr, v, col);
    }
  }
  for (int i = 0; i + 1 < n; i++)
    for (int c = 0; c < cols; c++) {
      int a = i * (cols + 1) + c, b = a + 1, d = a + cols + 1, e = d + 1;
      tt(&m, a, d, e), tt(&m, a, e, b);
    }
  t3_geometry *g = tfinish(&m);
  t3_mesh *me = t3_mesh_new(g, mat);
  me->base.cast_shadow = false;
  me->base.receive_shadow = false;
  hang(parent, name, &me->base);
  t3_release(me);
  t3_release(g);
}

/* the river as foam: cut into pieces so a view only draws what it sees. The apparent speed is the scroll rate
 * times the metres a repeat covers: fast and stretched on the descent, slower and busier in the canyon. */
static void build_white_water(cw_node parent, t3_vec3 sun_to) {
  t3_texture *streak = make_foam(0, 77), *churn = make_foam(1, 91);
  t3_material *m_streak = foam_material(streak, 0.95f), *m_churn = foam_material(churn, 0.95f);
  SC.tex[0] = streak, SC.rate[0] = 1.9f;
  SC.tex[1] = churn, SC.rate[1] = 0.55f;
  SC.n = 2;
  /* where the descent ends the water slows and churns: the pool and the river */
  int i_pool = 0;
  for (int i = 0; i < NRV; i++)
    if (RV[i].cum > 0 && RV[i].p.y < A.yfloor - 3.0f) {
      i_pool = i;
      break;
    }
  const int PIECE = 28;
  for (int a = 0; a + 1 < NRV; a += PIECE - 1) {
    int b = a + PIECE < NRV ? a + PIECE : NRV;
    int n = b - a;
    t3_vec3 pts[64];
    float hw[64], vst[64];
    for (int i = 0; i < n; i++) {
      pts[i] = RV[a + i].p;
      hw[i] = RV[a + i].hw;
      bool chute = a + i < i_pool;
      vst[i] = chute ? 16.0f : 11.0f;
      /* the sheet sits a little over its bed so the waves ride on it */
      pts[i].y += 0.05f;
    }
    bool chute = a < i_pool;
    char nm[32];
    snprintf(nm, sizeof nm, "Flow%d", a);
    flow_ribbon(parent, nm, pts, hw, vst, n, 10, chute ? 0.55f : 0.35f, chute ? 11.0f : 7.0f, chute ? m_streak : m_churn, sun_to);
  }
  t3_release(m_streak);
  t3_release(m_churn);
}

static void water_scroll(double time) {
  for (int i = 0; i < SC.n; i++) {
    float v = -fracf((float)time * SC.rate[i]);
    SC.tex[i]->offset = (t3_vec2){ 0, v };
  }
}

/* ── emitters that wake only when a kart is near ─────────────────────────────────────────────────────── */

typedef struct emitter {
  cw_node n;
  t3_vec3 at;
  float range;  /* metres from any kart inside which it runs */
  bool on;
} emitter;
#define MAX_EMIT 96
static struct {
  emitter e[MAX_EMIT];
  int n;
} EM;

static void pset(cw_node n, const char *key, const char *val) {
  const cw_prop *p = cw_node_find_prop(n, key, NULL);
  cw_value v;
  if (p && p->type == CW_T_ENUM) {
    cw_node_set(n, key, cw_val_enum(val));
    return;
  }
  if (!p || !cw_value_parse(val, p->type, &v)) {
    cw_warn("dam: bad particle setting %s = %s", key, val);
    return;
  }
  cw_node_set(n, key, v);
}

/* a particle system from a recipe of key / value strings (NULL ended), parked until a kart comes near */
static cw_node add_emitter(cw_node parent, const char *name, const char *const *kv, t3_vec3 at, float range, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(parent, n);
  for (int i = 0; kv[i]; i += 2) pset(n, kv[i], kv[i + 1]);
  cw_node3d_set_position(n, at);
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "visible", cw_val_bool(false));
  if (EM.n < MAX_EMIT) EM.e[EM.n++] = (emitter){ n, at, range, false };
  return n;
}

/* world_tick does not call a map's scenery_tick, so the karts are found through their scene nodes instead */
static void emitters_frame(void) {
  t3_vec3 kp[MAXK];
  int nk = 0;
  for (int k = 0; k < MAXK; k++) {
    cw_node kn = kartgfx_node(k);
    if (cw_node_valid(kn)) kp[nk++] = cw_node3d_global_position(kn);
  }
  for (int i = 0; i < EM.n; i++) {
    emitter *e = &EM.e[i];
    float best = 1e30f;
    for (int k = 0; k < nk; k++) {
      t3_vec3 d = v3sub(kp[k], e->at);
      float l = d.x * d.x + d.y * d.y + d.z * d.z;
      if (l < best) best = l;
    }
    bool want = best < e->range * e->range;
    if (want != e->on) {
      e->on = want;
      cw_node_set(e->n, "emitting", cw_val_bool(want));
      cw_node_set(e->n, "visible", cw_val_bool(want));
    }
  }
}

/* recipes */
static const char *const R_PLUME[] = { "amount", "34", "lifetime", "3.4", "lifetime_random", "0.3", "emission_shape", "BOX", "emission_extents", "vec3(2, 0.4, 7)",
  "direction", "vec3(0, 1, 0.3)", "spread", "24", "speed_min", "2.5", "speed_max", "5.5", "gravity", "vec3(0.1, 0.4, 2.2)", "damping", "0.35", "size", "3.4",
  "size_random", "0.4", "size_curve", "\"0 0.35, 0.3 1, 1 2.3\"",
  "color_ramp", "\"0 1 1 1 0, 0.12 0.98 0.99 1 0.55, 0.6 0.93 0.96 1 0.3, 1 0.9 0.94 1 0\"", "blend", "ALPHA", "soft_distance", "1.5",
  "turbulence", "1.1", "turbulence_scale", "0.12", "local_coords", "false", NULL };
static const char *const R_MIST[] = { "amount", "30", "lifetime", "8", "lifetime_random", "0.3", "emission_shape", "BOX", "emission_extents", "vec3(14, 1.5, 14)",
  "direction", "vec3(0, 1, 0)", "spread", "40", "speed_min", "1", "speed_max", "3", "gravity", "vec3(0.5, 0.5, 0.3)", "damping", "0.2", "size", "11",
  "size_random", "0.4", "size_curve", "\"0 0.5, 0.3 1, 1 1.8\"",
  "color_ramp", "\"0 1 1 1 0, 0.2 0.98 0.99 1 0.34, 1 0.94 0.97 1 0\"", "blend", "ALPHA", "soft_distance", "3", "turbulence", "0.8", "turbulence_scale", "0.05",
  "local_coords", "false", NULL };
static const char *const R_SPRAY[] = { "amount", "64", "lifetime", "1.4", "lifetime_random", "0.4", "emission_shape", "BOX", "emission_extents", "vec3(0.6, 0.4, 9)",
  "direction", "vec3(0, 1, 0.55)", "spread", "38", "speed_min", "5", "speed_max", "11", "gravity", "vec3(0, -7, 3)", "size", "0.17", "size_random", "0.5",
  "size_curve", "\"0 0.6, 0.2 1, 1 0.4\"", "color_ramp", "\"0 1 1 1 0.9, 1 0.9 0.96 1 0\"", "energy", "1.5", "blend", "ADD", "turbulence", "0.7",
  "turbulence_scale", "0.4", "local_coords", "false", NULL };

/* ── the spillway's head works: piers, a service bridge and raised gates across the channel ──────────── */

static pm *frame_world(pmc *c, t3_vec3 p, t3_vec3 f) { /* a standing frame at p facing along f (flat), y up, x to its right */
  pm *b = pmc_at(c, p);
  pm_push(b);
  f = flat_dir(f);
  pm_basis(b, p, v3(-f.z, 0, f.x), v3(0, 1, 0), f);
  return b;
}

static void build_gates(cw_node parent, const track *t) {
  const tsample *a = &t->s[A.gate];
  pmc *c = pmc_new(70, 41);
  t3_vec3 rf = flat_right(a), f = flat_dir(a->f);
  t3_vec3 o = v3add(a->p, v3mul(rf, -36.0f));
  o.y = LAKE_Y;
  pcol conc = rgb(0.74f, 0.72f, 0.67f), conc_d = rgb(0.5f, 0.49f, 0.46f), steel = rgb(0.86f, 0.30f, 0.12f), steel_d = rgb(0.5f, 0.17f, 0.08f);
  pcol dark = rgb(0.12f, 0.12f, 0.14f), yellow = rgb(0.98f, 0.8f, 0.1f);
  pm *b = frame_world(c, o, f);
  pm_vary(b, 0.05f);
  float px[4] = { -16.5f, -5.5f, 5.5f, 16.5f };
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < 4; i++) {
    pm_push(b);
    pm_move(b, px[i], -8.0f, 0);
    pm_box(b, 2.4f, 19.0f, 12.0f, conc_d, conc);       /* the pier */
    pm_move(b, 0, 0, -6.0f);
    pm_cyl(b, 1.2f, 1.2f, 19.0f, 14, conc_d, conc);    /* its rounded nose upstream */
    pm_pop(b);
  }
  /* the bridge on top: deck, a parapet each side and a stripe */
  pm_push(b);
  pm_move(b, 0, 10.4f, 1.0f);
  pm_box(b, 37.0f, 1.0f, 7.0f, conc_d, conc);
  pm_pop(b);
  pm_layer(b, PL_METAL);
  for (int s = -1; s <= 1; s += 2)
    for (int i = 0; i < 19; i++) {
      float x = -18.0f + (float)i * 2.0f;
      pm_beam(b, v3(x, 11.4f, 1.0f + (float)s * 3.2f), v3(x, 12.5f, 1.0f + (float)s * 3.2f), 0.12f, dark);
    }
  pm_beam(b, v3(-18, 12.5f, 4.2f), v3(18, 12.5f, 4.2f), 0.14f, yellow);
  pm_beam(b, v3(-18, 12.5f, -2.2f), v3(18, 12.5f, -2.2f), 0.14f, yellow);
  /* the gates: a steel leaf lifted clear of the water on two arms, hinged on the piers' downstream side */
  for (int i = 0; i < 3; i++) {
    float cx = (px[i] + px[i + 1]) * 0.5f, w = (px[i + 1] - px[i]) - 2.4f;
    pm_push(b);
    pm_move(b, cx, 4.6f, -1.0f);
    pm_box(b, w, 5.4f, 0.5f, steel_d, steel);
    for (int k = 0; k < 4; k++) pm_box(b, w, 0.25f, 0.62f, dark, dark), pm_move(b, 0, 1.3f, 0);
    pm_pop(b);
    for (int sd = -1; sd <= 1; sd += 2) {
      float xe = cx + (float)sd * (w * 0.5f - 0.3f);
      pm_beam(b, v3(xe, 7.0f, -1.0f), v3(xe, 1.2f, 5.0f), 0.34f, steel_d);
      pm_beam(b, v3(xe, 4.6f, -1.0f), v3(xe, 1.2f, 5.0f), 0.28f, steel_d);
    }
    /* the hoist chains up to the bridge */
    pm_rod(b, v3(cx - w * 0.25f, 10.0f, -0.6f), v3(cx - w * 0.25f, 7.2f, -0.9f), 0.07f, 5, dark);
    pm_rod(b, v3(cx + w * 0.25f, 10.0f, -0.6f), v3(cx + w * 0.25f, 7.2f, -0.9f), 0.07f, 5, dark);
  }
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < 3; i++) { /* hoist houses on the deck */
    float cx = (px[i] + px[i + 1]) * 0.5f;
    pm_push(b);
    pm_move(b, cx, 11.0f, 1.2f);
    pm_box(b, 4.2f, 2.8f, 3.4f, rgb(0.9f, 0.88f, 0.8f), rgb(0.96f, 0.95f, 0.9f));
    pm_move(b, 0, 2.8f, 0);
    pm_prism(b, 4.6f, 0.9f, 3.8f, rgb(0.5f, 0.2f, 0.12f), rgb(0.7f, 0.3f, 0.16f));
    pm_pop(b);
  }
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < 3; i++) {
    float cx = (px[i] + px[i + 1]) * 0.5f;
    pm_push(b);
    pm_move(b, cx, 12.2f, -0.55f);
    pm_box_c(b, 1.6f, 0.8f, 0.1f, glow(rgb(1.0f, 0.85f, 0.4f), 2.0f));
    pm_pop(b);
  }
  for (int i = 0; i < 4; i++) { /* a warning lamp on every pier */
    pm_push(b);
    pm_move(b, px[i], 11.2f, -2.0f);
    pm_box_c(b, 0.5f, 0.5f, 0.5f, glow(rgb(1.0f, 0.25f, 0.1f), 3.0f));
    pm_pop(b);
  }
  pm_pop(b);
  pmc_finish(c, parent, "Gates", true, TEX_WALL_CONCRETE);
}


/* ── the tunnel's hill: a swept mound over the tube with a concrete headwall round each mouth ─────────── */

#define HILL_N 11
static const float HILL_X[HILL_N] = { -27, -21, -15, -9.5f, -4.5f, 0, 4.5f, 9.5f, 15, 21, 27 };
static const float HILL_Y[HILL_N] = { -0.6f, 3.2f, 7.6f, 11.2f, 13.4f, 14.2f, 13.4f, 11.2f, 7.6f, 3.2f, -0.6f };

static t3_vec3 hill_pt(const tsample *s, int k, float lift) {
  t3_vec3 r = flat_right(s);
  float hwk = clampf(s->hw / 10.0f, 0.5f, 1.0f);
  (void)hwk;
  t3_vec3 p = v3add(s->p, v3mul(r, HILL_X[k]));
  p.y = s->p.y - 0.4f + HILL_Y[k] + lift;
  return p;
}

static void build_tunnel_hill(cw_node parent, const track *t, t3_material *rock, t3_material *conc) {
  /* the mound: rows of the profile every 2 m along the tunnel */
  tmesh m = { 0 };
  int rows = 0;
  for (int i = A.tun0; i <= A.tun1; i += 2) {
    const tsample *s = &t->s[i];
    for (int k = 0; k < HILL_N; k++) {
      t3_vec3 p = hill_pt(s, k, 0);
      float nx = k < HILL_N - 1 ? HILL_Y[k + 1] - HILL_Y[k] : 0, ny = HILL_X[k < HILL_N - 1 ? k + 1 : k] - HILL_X[k < HILL_N - 1 ? k : k - 1];
      t3_vec3 r = flat_right(s);
      t3_vec3 n = v3norm(v3add(v3mul(r, -nx), v3(0, ny, 0)));
      float sl = 1.0f - n.y;
      pcol c = land_colour(p.x, p.y, p.z, 0.4f + sl, KIND_ROCK);
      tv(&m, p.x, p.y, p.z, n.x, n.y, n.z, p.x / 9.0f + p.z / 14.0f, p.y / 9.0f + p.z / 11.0f, c);
    }
    rows++;
  }
  for (int r = 0; r + 1 < rows; r++)
    for (int k = 0; k + 1 < HILL_N; k++) {
      int a = r * HILL_N + k, b = a + 1, c = a + HILL_N, d = c + 1;
      tt(&m, a, b, d), tt(&m, a, d, c);
    }
  t3_geometry *g = tfinish(&m);
  t3_mesh *me = t3_mesh_new(g, rock);
  me->base.cast_shadow = true;
  me->base.receive_shadow = true;
  hang(parent, "TunnelHill", &me->base);
  t3_release(me);
  t3_release(g);
  /* a headwall round each mouth: from the arch of the tube out to the mound's profile */
  for (int end = 0; end < 2; end++) {
    int i = end == 0 ? A.tun0 : A.tun1;
    const tsample *s = &t->s[i];
    t3_vec3 f = flat_dir(s->f);
    if (end == 1) f = v3mul(f, -1);
    tmesh w = { 0 };
    float hw = s->hw, hc = 1.6f, Rt = 8.0f - hc;
    const int M = 20;
    t3_vec3 r = flat_right(s);
    t3_vec3 face = end == 0 ? v3mul(f, -1) : v3mul(f, -1); /* faces away from the tunnel */
    float push = end == 0 ? -0.05f : 0.05f;
    (void)push;
    for (int k = 0; k <= M; k++) {
      float u = (float)k / (float)M;
      /* outer: along the profile (feet to feet), mapped by index */
      float fo = u * (float)(HILL_N - 1);
      int k0 = (int)fo;
      if (k0 > HILL_N - 2) k0 = HILL_N - 2;
      float ft = fo - (float)k0;
      float ox = lerpf(HILL_X[k0], HILL_X[k0 + 1], ft), oy = lerpf(HILL_Y[k0], HILL_Y[k0 + 1], ft);
      t3_vec3 op = v3add(s->p, v3mul(r, ox));
      op.y = s->p.y - 0.4f + oy;
      /* inner: the arch of the tube */
      float a = 3.14159265f * u;
      float ix = -hw * cosf(a), iy = hc + Rt * sinf(a);
      t3_vec3 ip = v3add(s->p, v3mul(r, ix));
      ip.y = s->p.y + iy;
      tv(&w, op.x, op.y, op.z, face.x, 0, face.z, op.x / 4.0f + op.z / 4.0f, op.y / 4.0f, (pcol){ 1, 1, 1 });
      tv(&w, ip.x, ip.y, ip.z, face.x, 0, face.z, ip.x / 4.0f + ip.z / 4.0f, ip.y / 4.0f, (pcol){ 1, 1, 1 });
    }
    for (int k = 0; k < M; k++) {
      int a0 = k * 2, a1 = a0 + 1, b0 = a0 + 2, b1 = a0 + 3;
      /* double sided material: winding does not matter */
      tt(&w, a0, b0, b1), tt(&w, a0, b1, a1);
    }
    t3_geometry *gw = tfinish(&w);
    t3_mesh *mw = t3_mesh_new(gw, conc);
    mw->base.cast_shadow = true;
    mw->base.receive_shadow = true;
    hang(parent, end == 0 ? "PortalIn" : "PortalOut", &mw->base);
    t3_release(mw);
    t3_release(gw);
  }
}


/* ── the dam's concrete face: a skin laid on the fill beside the crest, the turn and the spillway ─────── */

#define SKIN_COLS 10
static void skin_rows(tmesh *m, const track *t, int i0, int i1, int *rows_out, t3_material *unused) {
  (void)unused;
  int rows = 0;
  for (int i = i0; i <= i1; i += 2) {
    const tsample *s = &t->s[i];
    t3_vec3 r = flat_right(s);
    float reach = s->hw + 1.0f + 0.07f * s->hw + (s->walls ? 0.6f : 0.0f);
    float base = s->p.y + s->hw * s->r.y - 0.4f;
    /* how far down the slope the face runs before the land itself is higher */
    float gend = 0;
    for (float g = 1.0f; g <= 70.0f; g += 1.5f) {
      float cy = base - SLOPE_K * fmaxf(g - 2.5f, 0.0f);
      t3_vec3 q = v3add(s->p, v3mul(r, reach + g));
      if (dam_land(q.x, q.z, NULL) >= cy - 0.12f) break;
      gend = g;
    }
    gend += 1.5f;
    float run = 0;
    t3_vec3 prev = v3(0, 0, 0);
    for (int c = 0; c <= SKIN_COLS; c++) {
      float g = gend * (float)c / (float)SKIN_COLS;
      float cy = base - SLOPE_K * fmaxf(g - 2.5f, 0.0f) + 0.28f;
      t3_vec3 q = v3add(s->p, v3mul(r, reach + g));
      q.y = cy;
      if (c) run += v3len(v3sub(q, prev));
      prev = q;
      t3_vec3 n = v3norm(v3add(v3mul(r, g > 2.5f ? SLOPE_K : 0.0f), v3(0, 1, 0)));
      float wet = 1.0f - sm(-30, 4, q.y - A.yfloor) * 0.0f;
      float shade = 0.74f + 0.26f * sm(A.yfloor, A.yfloor + 40.0f, q.y);
      (void)wet;
      pcol col = { shade, shade, shade * 0.98f };
      tv(m, q.x, q.y, q.z, n.x, n.y, n.z, run / 4.0f, s->s / 4.0f, col);
    }
    rows++;
  }
  *rows_out = rows;
}

static void build_dam_skin(cw_node parent, const track *t) {
  t3_material *conc = phys((t3_color){ 1, 1, 1 }, 0.92f, 0);
  tex_apply(conc, TEX_WALL_CONCRETE, 1, 1, 1.0f, 1.0f);
  conc->color = lin(0.86f, 0.84f, 0.80f);
  conc->vertex_colors = true;
  conc->side = T3_DOUBLE_SIDE;
  int ranges[2][2] = { { 0, A.spill1 + 30 }, { t->n - 720, t->n - 2 } };
  for (int r = 0; r < 2; r++) {
    /* cut each range into pieces a view can cull */
    for (int a = ranges[r][0]; a < ranges[r][1]; a += 120) {
      int b = a + 122 < ranges[r][1] ? a + 122 : ranges[r][1];
      tmesh m = { 0 };
      int rows = 0;
      skin_rows(&m, t, a, b, &rows, NULL);
      for (int rr = 0; rr + 1 < rows; rr++)
        for (int c = 0; c < SKIN_COLS; c++) {
          int p = rr * (SKIN_COLS + 1) + c, q = p + 1, u = p + SKIN_COLS + 1, v = u + 1;
          tt(&m, p, q, v), tt(&m, p, v, u);
        }
      if (!m.nv) continue;
      t3_geometry *g = tfinish(&m);
      t3_mesh *me = t3_mesh_new(g, conc);
      me->base.cast_shadow = true;
      me->base.receive_shadow = true;
      char nm[24];
      snprintf(nm, sizeof nm, "DamFace%d", a);
      hang(parent, nm, &me->base);
      t3_release(me);
      t3_release(g);
    }
  }
  t3_release(conc);
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
  water_surface(parent, 79.0f, -29.0f, 142.0f, 802.0f, LAKE_Y, &w);
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
  if (getenv("RACER_DAM_CHECK")[0] == 'p')
    for (int i = A.tun0 - 20; i < A.tun0 + 30; i += 3) {
      const tsample *s = &t->s[i];
      cw_info("dam probe s=%d flags=%x land %.2f road %.2f", i, s->flags, dam_land(s->p.x, s->p.z, NULL), s->p.y);
    }
}

/* spray, plumes and mist along the white water */
static void build_emitters(cw_node parent, const track *t) {
  EM.n = 0;
  int n = 0;
  /* tall plumes just beyond the road's wall, on the descent: the wind carries them across the road */
  for (int i = A.gate + 25; i < A.spill1 + 20; i += 38) {
    const tsample *s = &t->s[i];
    t3_vec3 p = v3add(s->p, v3mul(flat_right(s), -17.0f));
    p.y = s->p.y - 2.0f;
    char nm[24];
    snprintf(nm, sizeof nm, "Plume%d", n);
    add_emitter(parent, nm, R_PLUME, p, 150, 100 + n);
    n++;
  }
  /* fine spray right at the wall, over the road, close to the camera */
  for (int i = A.gate + 40; i < A.spill1 + 10; i += 26) {
    const tsample *s = &t->s[i];
    t3_vec3 p = v3add(s->p, v3mul(flat_right(s), -13.0f));
    p.y = s->p.y + 0.4f;
    char nm[24];
    snprintf(nm, sizeof nm, "Spray%d", n);
    add_emitter(parent, nm, R_SPRAY, p, 80, 300 + n);
    n++;
  }
  /* mist over the churning water at the foot and along the canyon */
  for (int i = 0; i < NRV; i += 14) {
    if (RV[i].p.y > A.yfloor - 3.0f) continue;
    t3_vec3 p = RV[i].p;
    p.y += 4.0f;
    char nm[24];
    snprintf(nm, sizeof nm, "Mist%d", n);
    add_emitter(parent, nm, R_MIST, p, 260, 500 + n);
    n++;
  }
  cw_info("dam: %d emitters", EM.n);
}

/* the road's own dressing: start gantry, checkpoint arches, jump marks, corner arrows, lamps and banner hoops on the crest */
static void dress_road(cw_node root, const track *t) {
  dress_style st = dress_defaults();
  st.main = rgb(0.30f, 0.33f, 0.40f);
  st.accent = rgb(0.98f, 0.78f, 0.08f);
  st.lamp = rgb(1.0f, 0.93f, 0.75f);
  st.has_colours = true;
  st.seed = 54;
  dress_start_line(root, t, &st);
  dress_checkpoints(root, t, &st);
  deco_jump_marks(root, t, &st);
  deco_corner_arrows(root, t, 0.008f, &st);
  deco_corner_signs(root, t, 0.008f, &st);
  float crest_end = t->s[A.spill0].s;
  float run0 = t->s[t->n - 1].s - 500.0f;
  deco_lights(root, t, 42.0f, LAMP_POLE, 0, crest_end - 60.0f, &st);
  deco_lights(root, t, 42.0f, LAMP_POLE, run0, t->length, &st);
  deco_banners(root, t, 120.0f, crest_end - 120.0f, 130.0f, &st);
  deco_banners(root, t, run0 + 40.0f, t->length - 60.0f, 130.0f, &st);
}

static void dam_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  find_anchors(t);
  cw_info("dam anchors: crest z %.0f..%.0f, spill %d..%d, tunnel %d..%d, cork %d..%d, gap %d..%d, climb %d..%d, loop %d..%d, xc %.0f floor %.1f rim %.1f",
          A.zS, A.zN, A.spill0, A.spill1, A.tun0, A.tun1, A.cork0, A.cork1, A.gap0, A.gap1, A.climb0, A.climb1, A.loop0, A.loop1, A.xc, A.yfloor, A.yrim);
  SUN_NODE = cw_node_get(scene, "Sun");
  SCENE_NODE = scene;
  cw_node root = cw_node_new("Node3D", "Dam");
  cw_node_add_child(scene, root);
  if (getenv("RACER_DAM_CHECK")) check_land(t);
  build_terrain(root);
  build_lake(root);
  t3_vec3 sun_to = v3norm(v3(0.46f, -0.82f, 0.35f)); /* the way the sun's light travels (see the scene file) */
  build_white_water(root, sun_to);
  build_gates(root, t);
  build_dam_skin(root, t);
  {
    t3_material *conc = phys((t3_color){ 1, 1, 1 }, 0.9f, 0);
    tex_apply(conc, TEX_WALL_CONCRETE, 1, 1, 1.0f, 1.0f);
    conc->color = lin(0.8f, 0.78f, 0.74f);
    conc->side = T3_DOUBLE_SIDE;
    build_tunnel_hill(root, t, TMAT, conc);
    t3_release(conc);
  }
  build_emitters(root, t);
  dress_road(root, t);
}

/* RACER_DAM_CAM=x,y,z,tx,ty,tz[,fov]: a still camera for looking at the map from anywhere (a development aid) */
static void photo_cam(void) {
  static bool done;
  const char *e = getenv("RACER_DAM_CAM");
  if (done || !e || !cw_node_valid(SCENE_NODE)) return;
  done = true;
  float v[7] = { 0, 20, 0, 0, 0, 100, 60 };
  sscanf(e, "%f,%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
  cw_node cam = cw_node_new("Camera3D", "PhotoCam");
  cw_node_add_child(SCENE_NODE, cam);
  cw_node3d_set_position(cam, v3(v[0], v[1], v[2]));
  cw_node3d_look_at(cam, v3(v[3], v[4], v[5]), v3(0, 1, 0));
  cw_node_set(cam, "fov", cw_val_float(v[6]));
  cw_node_set(cam, "near", cw_val_float(0.5f));
  cw_node_set(cam, "far", cw_val_float(3600));
  cw_node_set(cam, "current", cw_val_bool(true));
  cw_node old = cw_node_get(SCENE_NODE, "Camera");
  if (cw_node_valid(old)) cw_node_set(old, "current", cw_val_bool(false));
}

static void dam_frame(float alpha, double time) {
  (void)alpha;
  photo_cam();
  water_scroll(time);
  emitters_frame();
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
