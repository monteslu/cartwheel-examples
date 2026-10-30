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
#include "cartwheel/debug.h"
#include "cartwheel/resource.h"
#include "cartwheel/terrain.h"
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
static int PV = 1; /* the views of this race: split screen thins the detail */
static cw_node SUN_NODE, SCENE_NODE;

static void build_river(const track *t);
static void build_river2(void);
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
  build_river2();
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
static rv_pt RV2[96]; /* the calm river in the valley below the dam */
static int NRV2;
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

/* distance on the ground plane from (x, z) to a river's centre line; also the surface height and half width there */
static float river_dist_in(const rv_pt *rv, int n, float x, float z, float *wy, float *hw) {
  float best = 1e18f, by = 0, bw = 0;
  for (int i = 0; i + 1 < n; i++) {
    const rv_pt *a = &rv[i], *b = &rv[i + 1];
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
static float river_dist(float x, float z, float *wy, float *hw) {
  float w1, h1, w2, h2;
  float d1 = river_dist_in(RV, NRV, x, z, &w1, &h1), d2 = NRV2 > 1 ? river_dist_in(RV2, NRV2, x, z, &w2, &h2) : 1e9f;
  /* the nearer by its own bank: whichever has the less distance past its edge */
  if (NRV2 > 1 && d2 - h2 < d1 - h1) {
    if (wy) *wy = w2;
    if (hw) *hw = h2;
    return d2;
  }
  if (wy) *wy = w1;
  if (hw) *hw = h1;
  return d1;
}

/* the calm river below the dam: from the power station's outflow north, then east along the valley's foot */
static void build_river2(void) {
  float yf = A.yfloor - 0.7f;
  static const float RP[][3] = { { -96, -330, 12 }, { -98, -200, 14 }, { -102, -60, 16 }, { -104, 80, 20 }, { -108, 220, 24 }, { -96, 340, 28 }, { -60, 440, 30 },
                                 { 20, 488, 32 }, { 140, 506, 34 }, { 300, 500, 36 }, { 460, 494, 38 }, { 620, 492, 40 }, { 760, 486, 42 } };
  int nr = (int)(sizeof RP / sizeof RP[0]);
  t3_vec3 P[16];
  for (int i = 0; i < nr; i++) P[i] = v3(RP[i][0], yf - 0.7f, RP[i][1]);
  NRV2 = 0;
  for (int i = 0; i < nr - 1; i++) {
    t3_vec3 a = i ? P[i - 1] : P[0], d = i + 2 < nr ? P[i + 2] : P[nr - 1];
    for (int k = 0; k < 4 && NRV2 < 95; k++) {
      float u = (float)k / 4.0f;
      rv_pt *r = &RV2[NRV2++];
      r->p = catmull(a, P[i], P[i + 1], d, u);
      r->hw = lerpf(RP[i][2], RP[i + 1][2], u);
    }
  }
  RV2[NRV2].p = P[nr - 1], RV2[NRV2].hw = RP[nr - 1][2], NRV2++;
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

static const bool TER_REAL = true; /* the realistic Dam: no cartoon clouds, mesas, cactus or procedural rocks */
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
  /* buttes out in the far valley: flat tops, sheer sides, a talus apron (the land paints them sandstone by slope) */
  static const float BUTTE[][4] = { { -650, -520, 95, 105 }, { -720, 360, 120, 130 }, { -280, -900, 80, 90 }, { 920, -700, 110, 115 },
                                    { 1150, 120, 75, 140 }, { 820, 760, 130, 100 }, { -220, 920, 90, 120 }, { 420, -1010, 70, 85 },
                                    { 1230, -420, 60, 95 } };
  for (int b = 0; b < (int)(sizeof BUTTE / sizeof BUTTE[0]); b++) {
    float dx = x - BUTTE[b][0], dz = z - BUTTE[b][1];
    float d = sqrtf(dx * dx + dz * dz) * (1.0f + 0.18f * ground_noise(x, z, 40, 1.0f, 2, 60 + b));
    float r = BUTTE[b][2], top = A.yrim + BUTTE[b][3];
    float hb = top * 0.92f + top * 0.08f * ground_noise(x, z, 30, 1.0f, 2, 70 + b);
    float face = 1.0f - sm(r * 0.9f, r * 1.04f, d);
    float talus = (1.0f - sm(r * 1.0f, r * 1.9f, d)) * 0.32f;
    p = fmaxf(p, lerpf(p, hb, fmaxf(face, talus)));
  }
  float v = fmaxf(box_mask(x, z, -520, -24, -640, 520, 80), box_mask(x, z, -520, 760, 436, 600, 70));
  v = fmaxf(v, box_mask(x, z, 376, 498, -340, 440, 55));
  float floor_n = yf + ground_noise(x, z, 41, 1.3f, 2, 15);
  float h = lerpf(p, floor_n, v);
  /* terraces: the walls and mesas step in ledges, as layered red rock does (treads stay flat, risers are steep) */
  if (h > yf + 1.5f && getenv("RACER_TERRACE")) { /* (off on the Terrain3D land: sampled every 2 m the ledges read as dune ripples) */
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
  /* the ramps either side of the jump stand clear of the ground on trestles: no earth heaped under them */
  bool ramp = (idx >= A.gap0 - 110 && idx < A.gap0) || (idx > A.gap1 && idx <= A.gap1 + 110);
  if (ramp && ry - h0 > 4.0f) {
    if (kind) *kind = KIND_ROCK;
    return h0;
  }
  if (fl & TF_LOOP) {
    /* round the loop and the corkscrew the rock stays under the lowest of its road (it stood up inside the tube) */
    if (kind) *kind = KIND_ROCK;
    float flr = track_loop_floor(t, idx) - 0.6f;
    /* the corkscrew rolls inside its tube: upside down the tube's roof hangs as far below the road as it stood above
     * it, and the land at the road's floor cut through the tube as a red slab. Under a roof keep the rock under that */
    if (fl & TF_TUNNEL) flr -= 10.0f;
    return g < 14.0f ? fminf(h0, flr) : fminf(h0, flr + SLOPE_K * (g - 14.0f));
  }
  if (fl & TF_GAP) {
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
    /* (the end of the lap is concrete only by the dam itself: on the realistic land the loop's embankments read as snow) */
    k = (idx < A.pool1 + 60 || (idx > t->n - 700 && (!TER_REAL || fabsf(x) < 30.0f))) ? KIND_CONCRETE : KIND_ROCK;
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
  pcol strata[6] = { rgb(0.74f, 0.27f, 0.09f), rgb(0.58f, 0.16f, 0.07f), rgb(0.84f, 0.42f, 0.14f), rgb(0.46f, 0.15f, 0.08f),
                     rgb(0.88f, 0.55f, 0.2f), rgb(0.66f, 0.22f, 0.08f) };
  float warp = ground_noise(x, z, 38, 3.2f, 2, 21) + ground_noise(x * 0.4f + z * 0.9f, y * 3, 9, 0.5f, 1, 22);
  float yy = (y + warp) / 5.2f;
  int band = (int)floorf(yy);
  float within = yy - floorf(yy);
  pcol c = strata[hash32((uint32_t)(band + 400) * 2654435761u) % 6];
  c = pc_mul(c, within < 0.1f ? 0.72f : 1.0f - 0.18f * within);
  float fine = ground_noise(x * 1.7f, z * 1.7f, 5, 0.5f, 2, 23);
  c = pc_mul(c, 1.0f + 0.35f * fine);
  float steep = sm(0.35f, 1.0f, slope);
  pcol dusty = pc_mix(rgb(0.78f, 0.26f, 0.07f), rgb(0.62f, 0.2f, 0.06f), 0.5f + 0.5f * ground_noise(x, z, 30, 1.0f, 2, 24));
  pcol shrub = rgb(0.36f, 0.34f, 0.15f);
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
      float u = x / 7.0f, v = z / 7.0f;
      float w = sm(0.8f, 1.8f, slope);
      if (w > 0.001f) {
        float hl = sqrtf(gx * gx + gz * gz) + 1e-4f;
        float along = (x * (-gz) + z * gx) / hl;
        u = lerpf(u, along / 7.0f, w), v = lerpf(v, hh[k] / 7.0f, w);
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
static void pset(cw_node n, const char *key, const char *val);

/* ── the land: the engine's Terrain3D ────────────────────────────────────────────────────────────────────
 * One heightmap over the whole valley, 2 m between samples, its heights from dam_land (so everything built on that
 * function stands on it), painted from eight photo-sourced layers (assets/terrain/dam, tools/terrain-layers.sh):
 * red sandstone on the steep ground in two strata, cracked red earth and dusty dirt on the floor, scrub where brush
 * grows, sand on the river banks and the lake bed, gravel on the road's verges, concrete on the dam. Trees and grass
 * grow on it as scatter layers (TerrainScatter3D: the EZ-Tree conifers and Yughues grass cards, assets/models). */
enum { LY_CLIFF, LY_BAND, LY_FLOOR, LY_DIRT, LY_SCRUB, LY_SAND, LY_GRAVEL, LY_CONCRETE };
#define TER_X0 (-1280.0f)
#define TER_Z0 (-1152.0f)
#define TER_SP 2.0f
#define TER_NX 1344 /* quads: multiples of the 32-quad patch */
#define TER_NZ 1152
static cw_node TER_NODE;
static int TER_STAT[4]; /* debug fields dam.patches, dam.scatter (instances drawn), dam.scatter_cells, dam.scatter_draws */

/* what a terrain sample is painted with: base layer, overlay layer and the overlay's share (0..255) */
static uint32_t land_paint(float x, float y, float z, float slope, int kind, float road_g) {
  if (kind == KIND_CONCRETE) return cw_terrain_control(LY_CONCRETE, LY_CONCRETE, 0, 0, 0, false);
  /* the switchyard's pad (its transformers stand in rows over x -162..-110, z -150..18): cleared gravel, no trees */
  if (box_mask(x, z, -175, -97, -165, 32, 1) > 0.5f) return cw_terrain_control(LY_GRAVEL, LY_DIRT, 50, 0, 0, false);
  if (kind == KIND_BED || (lake_mask(x, z, 28) > 0.5f && y < LAKE_Y + 1.5f)) return cw_terrain_control(LY_SAND, LY_DIRT, (int)(60 * sm(LAKE_Y - 6, LAKE_Y, y)), 0, 0, false);
  if (kind == KIND_VERGE || road_g < 4.0f) return cw_terrain_control(LY_GRAVEL, LY_DIRT, (int)(255 * sm(1.5f, 4.0f, road_g)), 0, 0, false);
  /* the rock: strata by height (warped, so the bands wander), one of two reds a band */
  float warp = ground_noise(x, z, 38, 3.2f, 2, 21);
  int band = (int)floorf((y + warp) / 5.2f);
  int rock = hash32((uint32_t)(band + 400) * 2654435761u) % 3 == 0 ? LY_BAND : LY_CLIFF;
  float steep = sm(0.45f, 1.05f, slope);
  /* the flat: cracked earth and dusty dirt by noise, brush in patches, sand down the washes near the river */
  float n = ground_noise(x, z, 46, 1.0f, 2, 24);
  float patch = sm(0.1f, 0.5f, ground_noise(x, z, 24, 1.0f, 2, 25));
  int flat = n > 0 ? LY_FLOOR : LY_DIRT;
  int over = patch > 0.5f ? LY_SCRUB : (flat == LY_FLOOR ? LY_DIRT : LY_FLOOR);
  int fb = patch > 0.5f ? (int)(255 * sm(0.5f, 1.0f, patch) * 0.85f) : (int)(255 * (0.5f - 0.5f * fabsf(n)) * 0.6f);
  if (kind == KIND_CUT) steep = fmaxf(steep, 0.6f);
  if (steep > 0.5f) return cw_terrain_control(rock, flat, (int)(255 * (1 - steep) * 1.4f > 255 ? 255 : 255 * (1 - steep) * 1.4f), 0, 0, false);
  return cw_terrain_control(flat, steep > 0.2f ? rock : over, steep > 0.2f ? (int)(255 * steep * 1.6f) : fb, 0, 0, false);
}

static cw_node scatter(cw_node terr, const char *name, const char *mesh, float density, uint32_t layers, float slope, float smin,
                    float smax, bool align, float range, float fade, float lod, bool shadow, const char *knob, int seed) {
  cw_node s = cw_node_new("TerrainScatter3D", name);
  /* dev aid: RACER_NOSCATTER=all or a list of layer names leaves those layers out (what each one costs) */
  const char *skip = getenv("RACER_NOSCATTER");
  if (skip && (!strcmp(skip, "all") || strstr(skip, name))) density = 0;
  cw_node_set(s, "mesh", cw_val_string(mesh));
  cw_node_set(s, "density", cw_val_float(density));
  cw_node_set(s, "layers", cw_val_int((int)layers));
  cw_node_set(s, "min_slope", cw_val_float(slope));
  cw_node_set(s, "scale_min", cw_val_float(smin));
  cw_node_set(s, "scale_max", cw_val_float(smax));
  cw_node_set(s, "align", cw_val_bool(align));
  cw_node_set(s, "visible_range", cw_val_float(range));
  cw_node_set(s, "fade", cw_val_float(fade));
  cw_node_set(s, "lod_distance", cw_val_float(lod));
  cw_node_set(s, "cast_shadow", cw_val_bool(shadow));
  pset(s, "knob", knob);
  cw_node_set(s, "seed", cw_val_int(seed));
  cw_node_add_child(terr, s);
  return s;
}

static void build_terrain(cw_node parent) {
  const track *t = A.t;
  /* the tunnel hill's rock still takes this material */
  t3_material *mat = phys((t3_color){ 1, 1, 1 }, 0.95f, 0);
  mat->vertex_colors = true;
  tex_apply(mat, TEX_NOISE, 1, 1, 0.35f, 0.6f);
  if (mat->map) mat->color = (t3_color){ 1.0f, 0.9f, 0.85f };
  TMAT = mat;

  const int sx = TER_NX + 1, sz = TER_NZ + 1;
  float *h = malloc(sizeof(float) * (size_t)sx * (size_t)sz);
  uint8_t *kd = malloc((size_t)sx * (size_t)sz);
  float *rg = malloc(sizeof(float) * (size_t)sx * (size_t)sz);
  float lo = 1e9f, hi = -1e9f;
  for (int j = 0; j < sz; j++)
    for (int i = 0; i < sx; i++) {
      float x = TER_X0 + (float)i * TER_SP, z = TER_Z0 + (float)j * TER_SP;
      int k = 0;
      float y = dam_land(x, z, &k);
      size_t o = (size_t)j * (size_t)sx + (size_t)i;
      h[o] = y, kd[o] = (uint8_t)k;
      float ry;
      int idx;
      rg[o] = road_gap_idx(t, x, z, &ry, &idx);
      lo = fminf(lo, y), hi = fmaxf(hi, y);
    }
  lo -= 1.0f, hi += 1.0f;
  cw_node n = cw_node_new("Terrain3D", "Land");
  cw_node_set(n, "size_x", cw_val_int(TER_NX));
  cw_node_set(n, "size_z", cw_val_int(TER_NZ));
  cw_node_set(n, "vertex_spacing", cw_val_float(TER_SP));
  cw_node_set(n, "region_size", cw_val_int(64));
  cw_node_set(n, "min_height", cw_val_float(lo));
  cw_node_set(n, "max_height", cw_val_float(hi));
  cw_node_set(n, "far_distance", cw_val_float(PV > 1 ? 350.0f : 600.0f));
  cw_node_set(n, "physics", cw_val_bool(false));
  cw_node_set(n, "quality", cw_val_int(getenv("RACER_TERQ") ? atoi(getenv("RACER_TERQ")) : 2));
  static const char *const LN[8] = { "0_cliff", "1_band", "2_floor", "3_dirt", "4_scrub", "5_sand", "6_gravel", "7_concrete" };
  static const float UV[8] = { 0.06f, 0.06f, 0.16f, 0.16f, 0.14f, 0.2f, 0.3f, 0.12f };
  for (int i = 0; i < 8; i++) {
    char key[32], path[64];
    snprintf(key, sizeof key, "layer%d_albedo", i), snprintf(path, sizeof path, "terrain/dam/%s_albedo.png", LN[i]);
    cw_node_set(n, key, cw_val_string(path));
    snprintf(key, sizeof key, "layer%d_normal", i), snprintf(path, sizeof path, "terrain/dam/%s_normal.png", LN[i]);
    cw_node_set(n, key, cw_val_string(path));
    snprintf(key, sizeof key, "layer%d_uv_scale", i);
    cw_node_set(n, key, cw_val_float(UV[i]));
  }
  /* the node is centred on its position */
  cw_node3d_set_position(n, v3(TER_X0 + TER_NX * TER_SP * 0.5f, 0, TER_Z0 + TER_NZ * TER_SP * 0.5f));
  /* trees: pinyon and juniper the most, ponderosas in the cooler side draws, scrub oak in the brush, a dead snag here
   * and there; never on the verges, the sand or the concrete, nor on steep rock */
  const uint32_t TREE_L = 1u << LY_SCRUB; /* in groves: the brush patches */
  float tr = PV > 1 ? 260.0f : 480.0f;
  scatter(n, "Pinyon", "models/trees/pinyon.glb", 0.004f, TREE_L, 28, 1.0f, 1.6f, false, tr, 40, 60, true, "SCATTER", 11);
  scatter(n, "Juniper", "models/trees/juniper.glb", 0.004f, TREE_L, 30, 0.9f, 1.4f, false, tr, 40, 60, true, "SCATTER", 12);
  scatter(n, "Ponderosa", "models/trees/ponderosa.glb", 0.0006f, (1u << LY_SCRUB) | (1u << LY_DIRT), 22, 1.3f, 2.0f, false, tr, 40, 70, true, "SCATTER", 13);
  scatter(n, "Ponderosa2", "models/trees/ponderosa-2.glb", 0.0006f, (1u << LY_SCRUB) | (1u << LY_FLOOR), 22, 1.2f, 1.8f, false, tr, 40, 70, true, "SCATTER", 14);
  scatter(n, "ScrubOak", "models/trees/scruboak.glb", 0.0025f, 1u << LY_SCRUB, 30, 0.8f, 1.3f, false, tr * 0.7f, 30, 45, true, "SCATTER", 15);
  scatter(n, "Snag", "models/trees/snag.glb", 0.0002f, (1u << LY_FLOOR) | (1u << LY_DIRT), 25, 0.8f, 1.2f, false, tr, 40, 70, true, "SCATTER", 16);
  /* rocks: scanned desert boulders (Poly Haven, CC0) over the floor and up the slopes, outcrops on the steep rock */
  const uint32_t ROCK_L = (1u << LY_FLOOR) | (1u << LY_DIRT) | (1u << LY_SCRUB) | (1u << LY_CLIFF) | (1u << LY_BAND) | (1u << LY_SAND);
  cw_node_set(scatter(n, "BoulderA", "models/rocks/boulder_a.glb", 0.0018f, ROCK_L, 40, 0.8f, 2.4f, true, tr, 40, 80, true, "SCATTER", 31), "sink", cw_val_float(0.3f));
  cw_node_set(scatter(n, "BoulderB", "models/rocks/boulder_b.glb", 0.0010f, ROCK_L, 40, 0.7f, 2.0f, true, tr, 40, 80, true, "SCATTER", 32), "sink", cw_val_float(0.35f));
  cw_node_set(scatter(n, "BoulderC", "models/rocks/boulder_c.glb", 0.0040f, ROCK_L, 45, 0.6f, 1.6f, true, tr * 0.5f, 20, 50, true, "SCATTER", 33), "sink", cw_val_float(0.25f));
  cw_node_set(scatter(n, "Stones", "models/rocks/boulders.glb", 0.02f, ROCK_L, 40, 0.8f, 1.8f, true, PV > 1 ? 50.0f : 90.0f, 15, 40, false, "SCATTER", 34), "sink", cw_val_float(0.1f));
  cw_node_set(scatter(n, "Outcrop", "models/rocks/outcrop.glb", 0.0006f, (1u << LY_CLIFF) | (1u << LY_BAND), 80, 1.5f, 4.0f, true, tr * 1.3f, 60, 120, true, "SCATTER", 35), "sink", cw_val_float(0.6f));
  /* grass: dry tufts over the earth, green and tall in the brush and down by the water */
  scatter(n, "DryGrass", "models/grass/dry.glb", 0.25f, (1u << LY_FLOOR) | (1u << LY_DIRT), 30, 0.5f, 1.0f, true, PV > 1 ? 45.0f : 70.0f, 15, 30, false, "GRASS", 21);
  scatter(n, "Grass", "models/grass/green.glb", 0.6f, (1u << LY_SCRUB) | (1u << LY_SAND), 30, 0.5f, 1.0f, true, PV > 1 ? 45.0f : 70.0f, 15, 30, false, "GRASS", 22);
  scatter(n, "TallGrass", "models/grass/tall.glb", 0.15f, 1u << LY_SCRUB, 25, 0.8f, 1.4f, true, PV > 1 ? 40.0f : 60.0f, 12, 30, false, "GRASS", 23);
  cw_node_add_child(parent, n);
  TER_NODE = n;
  cw_terrain *ter = cw_terrain_of(n);
  if (!ter) {
    cw_error("dam: no terrain from the Land node");
    free(h), free(kd), free(rg);
    return;
  }
  cw_terrain_set_heights_m(ter, 0, 0, sx, sz, h);
  uint32_t *ctl = malloc(sizeof(uint32_t) * (size_t)sx * (size_t)sz);
  for (int j = 0; j < sz; j++)
    for (int i = 0; i < sx; i++) {
      size_t o = (size_t)j * (size_t)sx + (size_t)i;
      int i0 = i > 0 ? i - 1 : i, i1 = i < sx - 1 ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < sz - 1 ? j + 1 : j;
      float gx = (h[(size_t)j * sx + i1] - h[(size_t)j * sx + i0]) / ((float)(i1 - i0) * TER_SP);
      float gz = (h[(size_t)j1 * sx + i] - h[(size_t)j0 * sx + i]) / ((float)(j1 - j0) * TER_SP);
      ctl[o] = land_paint(TER_X0 + (float)i * TER_SP, h[o], TER_Z0 + (float)j * TER_SP, sqrtf(gx * gx + gz * gz), kd[o], rg[o]);
    }
  cw_terrain_set_control(ter, 0, 0, sx, sz, ctl);
  cw_terrain_scatter_invalidate(ter);
  free(ctl), free(h), free(kd), free(rg);
  cw_info("dam: terrain %d x %d samples, %.1f .. %.1f m", sx, sz, lo, hi);
  static bool fields;
  if (!fields) {
    fields = true;
    cw_debug_i32("dam.patches", &TER_STAT[0]);
    cw_debug_i32("dam.scatter", &TER_STAT[1]);
    cw_debug_i32("dam.scatter_cells", &TER_STAT[2]);
    cw_debug_i32("dam.scatter_draws", &TER_STAT[3]);
  }
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
  cw_terrain *ter = cw_node_valid(TER_NODE) ? cw_terrain_of(TER_NODE) : NULL;
  if (ter) {
    cw_terrain_stats st;
    cw_terrain_get_stats(ter, &st);
    TER_STAT[0] = st.patches + st.patches_far, TER_STAT[1] = st.scatter_instances, TER_STAT[2] = st.scatter_cells, TER_STAT[3] = st.scatter_draws;
  }
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
  int ranges[2][2] = { { 0, A.spill1 + 30 }, { A.loop1 + 60, t->n - 2 } };
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


/* ── below the dam: the calm river, the power station at the toe, its switchyard and the lines out ───── */

static t3_vec3 yawed(float yaw, float x, float z) { return v3(x * cosf(yaw) + z * sinf(yaw), 0, -x * sinf(yaw) + z * cosf(yaw)); }

/* a line of lattice pylons through pts (their feet), the cables between the arm tips; returns nothing, adds to ps and cables */
static void pylon_line(prop_set *ps, cw_node parent, const t3_vec3 *pts, int n, float scale, uint32_t seed) {
  float h = prop_default_size(PK_PYLON) * scale;
  float arms[3] = { 0.58f, 0.74f, 0.9f }, ext[3] = { 0.34f, 0.28f, 0.2f };
  t3_vec3 tip[16][6];
  for (int i = 0; i < n; i++) {
    t3_vec3 d = v3sub(pts[i + 1 < n ? i + 1 : i], pts[i > 0 ? i - 1 : i]);
    float yaw = atan2f(-d.z, d.x); /* local x (the arms) lies across the line */
    ps_add(ps, PK_PYLON, pts[i], yaw, scale, seed + (uint32_t)i);
    for (int a = 0; a < 3; a++)
      for (int k = 0; k < 2; k++) {
        float e = h * ext[a] * 0.7f * (k ? 1.0f : -1.0f), y = h * arms[a] - h * 0.095f;
        t3_vec3 o = yawed(yaw, e, 0);
        tip[i][a * 2 + k] = v3(pts[i].x + o.x, pts[i].y + y, pts[i].z + o.z);
      }
  }
  for (int i = 0; i + 1 < n; i++)
    for (int c = 0; c < 6; c++) {
      t3_vec3 pair[2] = { tip[i][c], tip[i + 1][c] };
      float span = v3len(v3sub(pair[1], pair[0]));
      deco_cables(parent, pair, 2, span * 0.03f, 0.07f, rgb(0.15f, 0.15f, 0.17f));
    }
}

static void build_valley(cw_node parent, const track *t) {
  (void)t;
  float yf = A.yfloor - 0.7f;
  /* the film of the river */
  {
    t3_vec3 pts[96];
    for (int i = 0; i < NRV2; i++) pts[i] = v3(RV2[i].p.x, RV2[i].p.y + 0.35f, RV2[i].p.z);
    water_opts w = water_defaults();
    w.color = rgb(0.16f, 0.62f, 0.62f);
    w.deep = rgb(0.02f, 0.22f, 0.28f);
    w.clarity = 0.5f;
    w.reflect = 0.7f;
    w.flow_x = 0.0f, w.flow_z = 0.8f;
    water_ribbon(parent, pts, NRV2, 56.0f, &w);
  }
  /* ── the power station: a long hall at the dam's toe with a barrel roof, six penstocks down the face ── */
  pmc *c = pmc_new(80, 61);
  float zc = -70.0f, len = 170.0f, x0 = -52.0f, wid = 36.0f;
  pcol wall = rgb(0.80f, 0.77f, 0.70f), wall_d = rgb(0.52f, 0.50f, 0.46f), roof = rgb(0.38f, 0.62f, 0.62f), roof_d = rgb(0.24f, 0.42f, 0.45f);
  pm *b = pmc_at(c, v3(x0 - wid * 0.5f, yf, zc));
  pm_push(b);
  pm_move(b, x0 - wid * 0.5f, yf - 0.6f, zc);
  pm_vary(b, 0.04f);
  pm_layer(b, PL_MAIN);
  pm_box(b, wid, 17.0f, len, wall_d, wall);
  /* barrel roof: a squashed cylinder laid along the hall */
  pm_push(b);
  pm_move(b, 0, 17.0f, 0);
  pm_rotx(b, 1.5707963f);
  pm_scale(b, 1, 1, 0.38f);
  pm_cyl_ex(b, wid * 0.5f, wid * 0.5f, len, 18, roof_d, roof, false, true);
  pm_pop(b);
  /* a tall transformer annex and a crane gantry along the east side */
  pm_push(b);
  pm_move(b, -wid * 0.5f - 7.0f, 0, -len * 0.5f + 20.0f);
  pm_box(b, 14.0f, 24.0f, 30.0f, wall_d, wall);
  pm_pop(b);
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 12; i++) {
    float z = -len * 0.5f + 8.0f + (float)i * (len - 16.0f) / 11.0f;
    pm_beam(b, v3(-wid * 0.5f - 0.5f, 0, z), v3(-wid * 0.5f - 0.5f, 16.0f, z), 0.3f, rgb(0.35f, 0.38f, 0.42f));
  }
  pm_layer(b, PL_GLOW);
  /* a band of tall windows along both long sides: warm light even by day */
  for (int side = -1; side <= 1; side += 2)
    for (int i = 0; i < 28; i++) {
      float z = -len * 0.5f + 4.0f + (float)i * (len - 8.0f) / 27.0f;
      pm_push(b);
      pm_move(b, (float)side * (wid * 0.5f + 0.06f), 10.0f, z);
      pm_box_c(b, 0.12f, 4.2f, 3.2f, glow(rgb(1.0f, 0.82f, 0.45f), 1.6f));
      pm_pop(b);
    }
  pm_pop(b);
  /* penstocks: six steel pipes laid on the face, from under the crest to the hall's wall */
  float base_y = 0.0f; /* the crest road's level over the hall */
  (void)base_y;
  for (int p = 0; p < 6; p++) {
    float z = zc - len * 0.5f + 16.0f + (float)p * (len - 32.0f) / 5.0f;
    int idx = 0;
    float bestd = 1e9f;
    for (int i = 0; i < t->n; i++) {
      float d = fabsf(t->s[i].p.z - z) + fabsf(t->s[i].p.x) * 10.0f;
      if (d < bestd) bestd = d, idx = i;
    }
    const tsample *s = &t->s[idx];
    t3_vec3 r = flat_right(s);
    float reach = s->hw + 1.0f + 0.07f * s->hw + 0.6f, base = s->p.y - 0.4f;
    t3_vec3 face_n = v3norm(v3add(v3mul(r, SLOPE_K), v3(0, 1, 0)));
    t3_vec3 prev;
    pcol pipe = rgb(0.86f, 0.42f, 0.14f), pipe_d = rgb(0.5f, 0.22f, 0.1f);
    for (int k = 0; k <= 8; k++) {
      float g = 5.0f + (float)k * (30.0f - 5.0f) / 8.0f;
      t3_vec3 q = v3add(s->p, v3mul(r, reach + g));
      q.y = base - SLOPE_K * (g - 2.5f) + 0.28f;
      q = v3add(q, v3mul(face_n, 2.7f));
      pm *pb = pmc_at(c, q);
      pm_layer(pb, PL_METAL);
      if (k) {
        pm_rod(pb, prev, q, 2.5f, 10, pipe);
        pm_rod(pb, v3add(prev, v3mul(v3sub(q, prev), 0.9f)), v3add(prev, v3mul(v3sub(q, prev), 0.98f)), 2.9f, 10, pipe_d); /* a flange */
      }
      prev = q;
    }
  }
  pmc_finish(c, parent, "PowerStation", true, TEX_WALL_CONCRETE);

  /* ── the switchyard: transformers in rows, a line of pylons out to the valley's far wall ── */
  pmc *y = pmc_new(80, 62);
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 8; col++) {
      t3_vec3 p = v3(-110.0f - (float)row * 26.0f, yf, -150.0f + (float)col * 24.0f);
      pm *pb = pmc_at(y, p);
      pm_push(pb);
      pm_move(pb, p.x, p.y, p.z);
      pm_vary(pb, 0.05f);
      pm_layer(pb, PL_METAL);
      pm_box(pb, 6.0f, 7.0f, 8.0f, rgb(0.3f, 0.34f, 0.38f), rgb(0.52f, 0.56f, 0.6f));
      for (int f = 0; f < 5; f++) pm_box(pb, 0.4f, 5.0f, 5.0f, rgb(0.28f, 0.3f, 0.34f), rgb(0.4f, 0.42f, 0.46f)), pm_move(pb, 0.9f, 0, 0);
      pm_pop(pb);
      pm_push(pb);
      pm_move(pb, p.x, p.y + 7.0f, p.z);
      for (int i = -1; i <= 1; i++) {
        pm_cyl(pb, 0.45f, 0.3f, 3.2f, 8, rgb(0.82f, 0.84f, 0.86f), rgb(0.95f, 0.95f, 0.95f));
        pm_move(pb, 0, 0, 2.2f);
      }
      pm_pop(pb);
    }
  pmc_finish(y, parent, "Switchyard", true, -1);

  prop_set *ps = ps_new(parent, "Pylons");
  t3_vec3 line1[6], line2[6];
  for (int i = 0; i < 6; i++) {
    float z = -180.0f + (float)i * 90.0f;
    line1[i] = v3(-215.0f - (float)i * 18.0f, yf, z);
    line2[i] = v3(-150.0f - (float)i * 38.0f, yf + 0.0f, 380.0f - (float)i * 5.0f);
  }
  (void)line2;
  /* a line marching up the valley's far wall: the feet follow the ground */
  t3_vec3 up[7];
  for (int i = 0; i < 7; i++) {
    float x = -190.0f - (float)i * 70.0f, z = -170.0f + (float)i * 20.0f;
    up[i] = v3(x, dam_land(x, z, NULL), z);
  }
  pylon_line(ps, parent, up, 7, 1.15f, 300);
  /* a short line from the yard along the valley floor to the north */
  t3_vec3 flo[5];
  for (int i = 0; i < 5; i++) flo[i] = v3(-180.0f, yf, -120.0f + (float)i * 110.0f);
  pylon_line(ps, parent, flo, 5, 1.0f, 340);
  /* a line of pylons up the canyon wall beside the climb */
  {
    t3_vec3 cl[6];
    int nc = 0;
    for (int i = A.climb0; i < A.loop0 && nc < 6; i += (A.loop0 - A.climb0) / 6) {
      const tsample *q = &t->s[i];
      t3_vec3 p = v3add(q->p, v3mul(flat_right(q), 48.0f));
      p.y = dam_land(p.x, p.z, NULL);
      cl[nc++] = p;
    }
    if (nc >= 3) pylon_line(ps, parent, cl, nc, 1.1f, 380);
  }
  ps_finish(ps);
}


/* ── rocks, scrub and far mesas ───────────────────────────────────────────────────────────────────── */

typedef struct maskp {
  float hmin, hmax, slope_max, river_clear;
} maskp;
/* 1 where a prop may stand: dry ground, not too steep, not in or at the river */
static float mask_ground(float x, float z, void *ud) {
  const maskp *m = ud;
  float h = dam_land(x, z, NULL);
  if (h < m->hmin || h > m->hmax) return 0;
  float gx = dam_land(x + 4, z, NULL) - dam_land(x - 4, z, NULL), gz = dam_land(x, z + 4, NULL) - dam_land(x, z - 4, NULL);
  if (sqrtf(gx * gx + gz * gz) / 8.0f > m->slope_max) return 0;
  float wy, hw;
  if (river_dist(x, z, &wy, &hw) < hw + m->river_clear) return 0;
  return 1;
}

static void build_land_props(cw_node parent, const track *t) {
  prop_set *ps = ps_new(parent, "Land");
  /* the far rock is the canyon's own sandstone: in their stock grey and buff the mesas read as stacked boxes on the red */
  ps_style(ps, PK_MESA, prop_tex(PK_MESA), rgb(1.0f, 0.72f, 0.56f), 1.0f);
  ps_style(ps, PK_SPIRE, prop_tex(PK_SPIRE), rgb(1.0f, 0.74f, 0.58f), 1.0f);
  ps_style(ps, PK_CANYON_WALL, prop_tex(PK_CANYON_WALL), rgb(1.0f, 0.7f, 0.54f), 1.0f);
  int n = 0;
  float cx = 215, cz = -80;
  /* far: mesas, spires and cliff slabs round the whole bowl, to give the horizon a silhouette (the realistic Dam: buttes
   * in the land itself, rocks, trees and grass as the terrain's scatter layers) */
  if (!TER_REAL) {
    maskp mk = { A.yfloor + 30, 1e9f, 0.55f, 0 };
    scatter_opts o = scatter_defaults();
    o.kinds[0] = PK_MESA, o.weight[0] = 3, o.kinds[1] = PK_SPIRE, o.weight[1] = 2, o.kinds[2] = PK_CANYON_WALL, o.weight[2] = 2, o.nkinds = 3;
    o.scale_min = 1.5f, o.scale_max = 3.2f;
    o.cx = cx, o.cz = cz, o.ring_min = 380, o.ring_max = 1250, o.density = 0.016f;
    o.ground = dam_ground_fn, o.mask = mask_ground, o.mask_ud = &mk;
    o.lod_dist = 60, o.far_thin = 0, o.sink = 1.5f, o.margin = 10, o.seed = 71;
    n += scatter_area(ps, t, &o);
  }
  /* near: rocks, boulders and a little scrub over the plateau, the valley floors and the canyon */
  if (!TER_REAL) {
    maskp mk = { LAKE_Y + 1.5f, 1e9f, 0.8f, 6 };
    scatter_opts o = scatter_defaults();
    o.kinds[0] = PK_ROCK, o.weight[0] = 3, o.kinds[1] = PK_BOULDERS, o.weight[1] = 1.2f, o.kinds[2] = PK_BUSH, o.weight[2] = 3, o.kinds[3] = PK_GRASS, o.weight[3] = 3,
    o.kinds[4] = PK_CACTUS, o.weight[4] = 0.5f, o.kinds[5] = PK_DEAD_TREE, o.weight[5] = 0.35f, o.nkinds = 6;
    o.scale_min = 0.8f, o.scale_max = 1.8f;
    o.cx = cx, o.cz = cz, o.half_x = 760, o.half_z = 700, o.density = PV > 1 ? 0.45f : 0.9f;
    o.ground = dam_ground_fn, o.mask = mask_ground, o.mask_ud = &mk;
    o.lod_dist = 90, o.far_thin = 0.6f, o.margin = 4, o.seed = 72;
    n += scatter_area(ps, t, &o);
  }
  /* along the road: bigger rocks and tufts a little out from the walls, where the eye passes them at speed */
  if (!TER_REAL) {
    maskp mk = { LAKE_Y + 1.5f, 1e9f, 0.7f, 4 };
    scatter_opts o = scatter_defaults();
    o.kinds[0] = PK_ROCK, o.weight[0] = 3, o.kinds[1] = PK_BOULDERS, o.weight[1] = 1, o.kinds[2] = PK_BUSH, o.weight[2] = 2, o.kinds[3] = PK_GRASS, o.weight[3] = 3, o.nkinds = 4;
    o.scale_min = 0.9f, o.scale_max = 2.1f;
    o.spacing = PV > 1 ? 24 : 13, o.lat_min = 3, o.lat_max = 44, o.lat_curve = 1.5f;
    o.ground = dam_ground_fn, o.mask = mask_ground, o.mask_ud = &mk;
    o.lod_dist = 80, o.far_thin = 0.3f, o.seed = 73;
    n += scatter_along(ps, t, &o);
  }
  /* rocks standing in the white water make it break */
  for (int i = 8; i < NRV - 1; i += 7) {
    float hw = RV[i].hw;
    for (int k = 0; k < 2; k++) {
      uint32_t h = hash32((uint32_t)(i * 31 + k * 7 + 5));
      t3_vec3 d = v3norm(v3sub(RV[i + 1].p, RV[i].p));
      t3_vec3 side = v3norm(v3cross(v3(0, 1, 0), d));
      float lat = (hashf(h) - 0.5f) * hw * 1.3f;
      t3_vec3 p = v3add(RV[i].p, v3mul(side, lat));
      p.y = RV[i].p.y - 0.8f;
      ps_add(ps, hashf(h + 3) < 0.4f ? PK_BOULDERS : PK_ROCK, p, hashf(h + 9) * 6.28f, 1.1f + 1.4f * hashf(h + 11), h);
      n++;
    }
  }
  /* the spillway's right edge: pipe racks, tanks, a dish and bollards, the plant that feeds the turbines */
  {
    maskp mk = { A.yfloor - 6.0f, 1e9f, 0.5f, 4 };
    scatter_opts o = scatter_defaults();
    o.kinds[0] = PK_PIPE_RACK, o.weight[0] = 3, o.kinds[1] = PK_TANK, o.weight[1] = 1.2f, o.kinds[2] = PK_PIPE, o.weight[2] = 2, o.kinds[3] = PK_DISH, o.weight[3] = 0.6f, o.kinds[4] = PK_BOLLARD, o.weight[4] = 3, o.nkinds = 5;
    o.scale_min = 1.0f, o.scale_max = 1.7f;
    o.from_s = t->s[A.gate].s, o.to_s = t->s[A.pool1].s;
    o.spacing = PV > 1 ? 30 : 15, o.lat_min = 3, o.lat_max = 16, o.sides = SIDE_RIGHT, o.lat_curve = 1.2f;
    o.ground = dam_ground_fn, o.mask = mask_ground, o.mask_ud = &mk;
    o.lod_dist = 70, o.seed = 74;
    n += scatter_along(ps, t, &o);
  }
  /* the climb: broken rock, scrub and cacti on both sides of the long ascent */
  if (!TER_REAL) {
    maskp mk = { A.yfloor, 1e9f, 0.9f, 3 };
    scatter_opts o = scatter_defaults();
    o.kinds[0] = PK_BOULDERS, o.weight[0] = 2, o.kinds[1] = PK_ROCK, o.weight[1] = 3, o.kinds[2] = PK_CACTUS, o.weight[2] = 1.5f, o.kinds[3] = PK_DEAD_TREE, o.weight[3] = 1, o.kinds[4] = PK_BUSH, o.weight[4] = 3, o.nkinds = 5;
    o.scale_min = 1.0f, o.scale_max = 2.0f;
    o.from_s = t->s[A.climb0 - 30].s, o.to_s = t->s[A.loop0].s;
    o.spacing = PV > 1 ? 14 : 7, o.lat_min = 2, o.lat_max = 22, o.lat_curve = 1.4f;
    o.ground = dam_ground_fn, o.mask = mask_ground, o.mask_ud = &mk;
    o.lod_dist = 70, o.seed = 75;
    n += scatter_along(ps, t, &o);
  }
  ps_finish(ps);
  cw_info("dam: %d props", n);
}

/* ── clouds and eagles ───────────────────────────────────────────────────────────────────────────── */

#define NCLOUD 16
static struct {
  cw_node n[NCLOUD];
  t3_vec3 p0[NCLOUD];
  float speed[NCLOUD], span;
  cw_node eagle[6];
  t3_vec3 ec[6];
  float er[6], ew[6], ep[6];
} SKY;

static t3_geometry *make_cloud(uint32_t seed, float size) {
  /* smooth spheres merged: a cumulus is a heap of round puffs with a flat grey-blue belly */
  int balls = 9 + (int)(hashf(seed) * 5);
  t3_geometry *g[16];
  t3_mat4 m[16];
  int n = 0;
  t3_geometry *unit = t3_sphere_geometry_new(1.0f, 18, 12);
  for (int i = 0; i < balls && n < 16; i++) {
    float u = (float)i / (float)(balls - 1), mid = 1.0f - fabsf(u - 0.5f) * 2.0f;
    float x = (u - 0.5f) * size * 1.5f + (hashf(seed + 7u * (uint32_t)i) - 0.5f) * size * 0.1f;
    float r = size * (0.2f + 0.2f * mid) * (0.75f + 0.5f * hashf(seed + 31u * (uint32_t)i + 1));
    float y = r * 0.55f + size * 0.12f * mid * hashf(seed + 17u * (uint32_t)i);
    t3_mat4_compose(&m[n], v3(x, y, (hashf(seed + 3u * (uint32_t)i + 2) - 0.5f) * size * 0.2f), (t3_quat){ 0, 0, 0, 1 }, v3(r, r * 0.7f, r * 0.9f));
    g[n++] = unit;
  }
  t3_geometry *out = geo_merge(g, m, n);
  t3_release(unit);
  return out;
}

static t3_geometry *make_eagle(void) {
  pm *b = pm_new(404);
  pm_layer(b, PL_MAIN);
  pcol brown = rgb(0.16f, 0.1f, 0.06f), light = rgb(0.34f, 0.24f, 0.14f), white = rgb(0.95f, 0.94f, 0.9f);
  pm_push(b);
  pm_ball(b, 0.22f, 0.2f, 0.7f, 1, 0.05f, light, brown);
  pm_move(b, 0, 0.06f, 0.72f);
  pm_ball(b, 0.16f, 0.15f, 0.2f, 1, 0.03f, white, white);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0, -0.65f);
  pm_wedge(b, 0.5f, 0.05f, 0.7f, brown, brown);
  pm_pop(b);
  for (int s = -1; s <= 1; s += 2) {
    float x = (float)s;
    pm_quad(b, v3(0.15f * x, 0.08f, 0.45f), v3(0.15f * x, 0.08f, -0.35f), v3(2.2f * x, 0.12f, -0.55f), v3(2.0f * x, 0.1f, 0.1f), light);
    pm_quad(b, v3(0.15f * x, 0.08f, -0.35f), v3(0.15f * x, 0.08f, 0.45f), v3(2.0f * x, 0.1f, 0.1f), v3(2.2f * x, 0.12f, -0.55f), brown);
    pm_quad(b, v3(2.0f * x, 0.1f, 0.1f), v3(2.2f * x, 0.12f, -0.55f), v3(3.1f * x, 0.2f, -0.95f), v3(3.0f * x, 0.2f, -0.1f), brown);
    pm_quad(b, v3(2.2f * x, 0.12f, -0.55f), v3(2.0f * x, 0.1f, 0.1f), v3(3.0f * x, 0.2f, -0.1f), v3(3.1f * x, 0.2f, -0.95f), brown);
  }
  prop_model m = pm_finish(b);
  t3_geometry *g = m.g[PL_MAIN];
  if (g) t3_retain(g);
  prop_model_free(&m);
  return g;
}

static void build_sky_life(cw_node parent) {
  t3_material *cm = phys(lin(0.97f, 0.96f, 0.93f), 1.0f, 0);
  cm->emissive = lin(0.55f, 0.62f, 0.8f);
  cm->emissive_intensity = 0.28f; /* the shaded belly keeps a cool sky glow */
  for (int i = 0; i < NCLOUD && !TER_REAL; i++) { /* (no cartoon clouds over the real land) */
    uint32_t h = hash32((uint32_t)(i * 977 + 13));
    float size = 90.0f + 130.0f * hashf(h);
    t3_geometry *g = make_cloud(h, size);
    if (!g) continue;
    t3_mesh *me = t3_mesh_new(g, cm);
    me->base.cast_shadow = false;
    me->base.receive_shadow = false;
    char nm[24];
    snprintf(nm, sizeof nm, "Cloud%d", i);
    SKY.n[i] = hang(parent, nm, &me->base);
    t3_release(me);
    t3_release(g);
    float ang = (float)i / NCLOUD * 6.2831853f + hashf(h + 1) * 0.5f, rad = 500.0f + 1300.0f * hashf(h + 2);
    SKY.p0[i] = v3(215.0f + cosf(ang) * rad, 300.0f + 220.0f * hashf(h + 3), -80.0f + sinf(ang) * rad);
    SKY.speed[i] = 3.0f + 4.0f * hashf(h + 4);
    cw_node3d_set_position(SKY.n[i], SKY.p0[i]);
    cw_node3d_set_scale(SKY.n[i], v3(1, 1, 1));
    cw_node3d_set_rotation(SKY.n[i], v3(0, hashf(h + 5) * 3.14f, 0));
  }
  SKY.span = 3200.0f;
  t3_geometry *eg = make_eagle();
  t3_material *em = prop_material(PL_MAIN, -1, (pcol){ 1, 1, 1 }, 1.0f);
  static const float EC[6][4] = { { 430, 52, 40, 90 }, { 60, 68, -300, 120 }, { -140, 44, 120, 100 }, { 110, 74, -460, 70 }, { 250, 40, 400, 85 }, { 480, 80, -250, 130 } };
  for (int i = 0; i < 6 && eg; i++) {
    t3_mesh *me = t3_mesh_new(eg, em);
    me->base.cast_shadow = false;
    char nm[24];
    snprintf(nm, sizeof nm, "Eagle%d", i);
    SKY.eagle[i] = hang(parent, nm, &me->base);
    t3_release(me);
    SKY.ec[i] = v3(EC[i][0], EC[i][1], EC[i][2]);
    SKY.er[i] = EC[i][3];
    SKY.ew[i] = (i & 1 ? -1.0f : 1.0f) * (0.16f + 0.03f * (float)i);
    SKY.ep[i] = (float)i * 1.3f;
    cw_node3d_set_scale(SKY.eagle[i], v3(2.2f, 2.2f, 2.2f));
  }
  if (eg) t3_release(eg);
}

static void sky_life_frame(double time) {
  float tm = (float)time;
  for (int i = 0; i < NCLOUD; i++) {
    if (!cw_node_valid(SKY.n[i])) continue;
    /* drift on the wind (toward +x, a little +z) and wrap round the far side of the bowl */
    float d = fmodf(SKY.speed[i] * tm, SKY.span);
    t3_vec3 p = SKY.p0[i];
    p.x += d, p.z += d * 0.3f;
    if (p.x > 215.0f + SKY.span * 0.5f) p.x -= SKY.span, p.z -= SKY.span * 0.3f;
    cw_node3d_set_position(SKY.n[i], p);
  }
  for (int i = 0; i < 6; i++) {
    if (!cw_node_valid(SKY.eagle[i])) continue;
    float a = SKY.ep[i] + SKY.ew[i] * tm;
    t3_vec3 p = v3(SKY.ec[i].x + cosf(a) * SKY.er[i], SKY.ec[i].y + 3.0f * sinf(tm * 0.5f + SKY.ep[i]), SKY.ec[i].z + sinf(a) * SKY.er[i]);
    float dir = SKY.ew[i] > 0 ? 1.0f : -1.0f;
    /* heading along the circle, banked into it */
    float yaw = atan2f(-sinf(a) * dir, cosf(a) * dir);
    cw_node3d_set_position(SKY.eagle[i], p);
    cw_node3d_set_rotation(SKY.eagle[i], v3(0, yaw, dir * 0.28f));
  }
}


/* ── a concrete arch bridge over the canyon, the lookout above the spillway, the intake towers ─────── */

/* x where the land first rises to height y going out from x0 in direction dir along z = z0 */
static float scan_wall(float z0, float x0, float dir, float y, float limit) {
  for (float d = 0; d < limit; d += 2.0f) {
    float x = x0 + dir * d;
    if (dam_land(x, z0, NULL) >= y) return x;
  }
  return x0 + dir * limit;
}

static void build_bridge(cw_node parent, float z0) {
  float deck_y = A.yfloor + 66.0f;
  float xl = scan_wall(z0, A.xc, -1, deck_y - 2.0f, 260), xr = scan_wall(z0, A.xc, +1, deck_y - 2.0f, 300);
  float yl = dam_land(xl, z0, NULL), yr = dam_land(xr, z0, NULL);
  deck_y = fmaxf(yl, yr) + 1.0f;
  /* the arch springs from the walls 24 m under the deck */
  float spring = deck_y - 26.0f;
  float xal = scan_wall(z0, A.xc, -1, spring, 260), xar = scan_wall(z0, A.xc, +1, spring, 300);
  pmc *c = pmc_new(120, 81);
  pcol conc = rgb(0.84f, 0.82f, 0.76f), conc_d = rgb(0.6f, 0.58f, 0.54f), steel = rgb(0.30f, 0.33f, 0.40f), glowc = rgb(1.0f, 0.9f, 0.6f);
  pm *b = pmc_at(c, v3((xl + xr) * 0.5f, deck_y, z0));
  pm_push(b);
  pm_vary(b, 0.04f);
  float W = 13.0f;
  /* the deck: a long slab with kerbs */
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, (xl + xr) * 0.5f, deck_y - 2.2f, z0);
  pm_box(b, xr - xl, 2.2f, W, conc_d, conc);
  pm_pop(b);
  /* the arch: a ring of tapering limbs on a shallow parabola, doubled for width */
  int N = 24;
  float rise = 22.0f;
  for (int side = -1; side <= 1; side += 2) {
    t3_vec3 prev = { 0 };
    for (int i = 0; i <= N; i++) {
      float u = (float)i / (float)N;
      float x = lerpf(xal, xar, u);
      float y = spring + rise * 4.0f * u * (1.0f - u) * 0.0f + (deck_y - 2.4f - spring) * (1.0f - powf(2.0f * u - 1.0f, 2.0f)) * 0.98f;
      t3_vec3 q = v3(x, y, z0 + (float)side * (W * 0.5f - 1.2f));
      if (i) pm_limb(b, prev, q, 1.9f, 1.9f, 8, conc_d, conc);
      /* a column from the arch up to the deck every other segment */
      if (i && i < N && (i & 1) == 0) pm_rod(b, v3(q.x, q.y, q.z), v3(q.x, deck_y - 2.2f, q.z), 0.7f, 8, conc);
      prev = q;
    }
  }
  /* cross braces between the two ribs */
  for (int i = 1; i < N; i += 2) {
    float u = (float)i / (float)N;
    float x = lerpf(xal, xar, u);
    float y = spring + (deck_y - 2.4f - spring) * (1.0f - powf(2.0f * u - 1.0f, 2.0f)) * 0.98f;
    pm_beam(b, v3(x, y, z0 - W * 0.5f + 1.2f), v3(x, y, z0 + W * 0.5f - 1.2f), 0.9f, conc_d);
  }
  /* abutment blocks where the deck meets the rims */
  for (int e = 0; e < 2; e++) {
    float x = e ? xr : xl;
    pm_push(b);
    pm_move(b, x, deck_y - 14.0f, z0);
    pm_box_c(b, 16.0f, 28.0f, W + 2.0f, conc_d);
    pm_pop(b);
  }
  /* rails with lamps */
  pm_layer(b, PL_METAL);
  for (int side = -1; side <= 1; side += 2) {
    float zz = z0 + (float)side * (W * 0.5f - 0.3f);
    pm_beam(b, v3(xl, deck_y + 1.1f, zz), v3(xr, deck_y + 1.1f, zz), 0.14f, steel);
    pm_beam(b, v3(xl, deck_y + 0.5f, zz), v3(xr, deck_y + 0.5f, zz), 0.1f, steel);
    for (float x = xl; x <= xr; x += 6.0f) pm_beam(b, v3(x, deck_y, zz), v3(x, deck_y + 1.15f, zz), 0.1f, steel);
  }
  pm_layer(b, PL_GLOW);
  for (float x = xl + 12; x < xr - 12; x += 24.0f)
    for (int side = -1; side <= 1; side += 2) {
      pm_push(b);
      pm_move(b, x, deck_y + 5.2f, z0 + (float)side * (W * 0.5f - 0.3f));
      pm_box_c(b, 1.0f, 0.5f, 1.0f, glow(glowc, 2.6f));
      pm_pop(b);
    }
  pm_layer(b, PL_METAL);
  for (float x = xl + 12; x < xr - 12; x += 24.0f)
    for (int side = -1; side <= 1; side += 2) pm_beam(b, v3(x, deck_y, z0 + (float)side * (W * 0.5f - 0.3f)), v3(x, deck_y + 5.0f, z0 + (float)side * (W * 0.5f - 0.3f)), 0.12f, steel);
  pm_pop(b);
  pmc_finish(c, parent, "Bridge", true, TEX_WALL_CONCRETE);
  cw_info("dam: bridge %.0f..%.0f at z %.0f, deck %.1f", xl, xr, z0, deck_y);
}

static void build_lookout(cw_node parent, const track *t) {
  int i = A.gate - 75;
  const tsample *s = &t->s[i];
  t3_vec3 r = flat_right(s), f = flat_dir(s->f);
  t3_vec3 p = v3add(s->p, v3mul(r, 34.0f));
  p.y = dam_land(p.x, p.z, NULL);
  pmc *c = pmc_new(60, 91);
  pcol conc = rgb(0.84f, 0.82f, 0.76f), conc_d = rgb(0.6f, 0.58f, 0.54f), steel = rgb(0.30f, 0.33f, 0.40f);
  pm *b = frame_world(c, p, f);
  pm_vary(b, 0.04f);
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, -2.0f, 0);
  pm_box_c(b, 24.0f, 4.0f, 16.0f, conc);       /* the terrace */
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0.08f, 0);
  pm_box_c(b, 22.0f, 0.2f, 14.0f, conc_d);
  pm_pop(b);
  /* the kiosk */
  pm_push(b);
  pm_move(b, -8.0f, 0.1f, -3.0f);
  pm_box(b, 5.0f, 3.2f, 4.0f, rgb(0.92f, 0.9f, 0.84f), rgb(0.98f, 0.97f, 0.92f));
  pm_move(b, 0, 3.2f, 0);
  pm_prism(b, 5.8f, 1.1f, 4.8f, rgb(0.7f, 0.22f, 0.14f), rgb(0.86f, 0.3f, 0.18f));
  pm_pop(b);
  pm_layer(b, PL_METAL);
  /* a rail on the far sides and a row of flag poles */
  for (int side = -1; side <= 1; side += 2) {
    pm_beam(b, v3(-11.0f, 1.1f, (float)side * 7.0f), v3(11.0f, 1.1f, (float)side * 7.0f), 0.12f, steel);
    for (float x = -11.0f; x <= 11.0f; x += 2.2f) pm_beam(b, v3(x, 0.1f, (float)side * 7.0f), v3(x, 1.15f, (float)side * 7.0f), 0.09f, steel);
  }
  pm_beam(b, v3(11.0f, 1.1f, -7.0f), v3(11.0f, 1.1f, 7.0f), 0.12f, steel);
  pcol flags[5] = { rgb(0.92f, 0.22f, 0.16f), rgb(0.98f, 0.8f, 0.1f), rgb(0.12f, 0.6f, 0.7f), rgb(0.96f, 0.96f, 0.94f), rgb(0.9f, 0.4f, 0.1f) };
  for (int k = 0; k < 5; k++) {
    float x = -9.0f + (float)k * 4.5f;
    pm_rod(b, v3(x, 0, 6.2f), v3(x, 11.0f, 6.2f), 0.1f, 6, steel);
    pm_layer(b, PL_MAIN);
    float wob = 0.5f * sinf((float)k * 1.3f);
    pm_quad(b, v3(x, 10.9f, 6.2f), v3(x + 3.6f, 10.5f + wob, 6.2f + 0.5f), v3(x + 3.4f, 8.7f + wob, 6.2f + 0.4f), v3(x, 9.0f, 6.2f), flags[k]);
    pm_quad(b, v3(x + 3.6f, 10.5f + wob, 6.2f + 0.5f), v3(x, 10.9f, 6.2f), v3(x, 9.0f, 6.2f), v3(x + 3.4f, 8.7f + wob, 6.2f + 0.4f), flags[k]);
    pm_layer(b, PL_METAL);
  }
  pm_pop(b);
  pmc_finish(c, parent, "Lookout", true, TEX_WALL_CONCRETE);
  /* the tourists: stone statues in a row on the terrace, facing the valley */
  prop_set *ps = ps_new(parent, "Tourists");
  for (int k = 0; k < 6; k++) {
    float x = -6.0f + (float)k * 2.6f, z = 3.2f + 0.6f * sinf((float)k * 2.1f);
    t3_vec3 q = v3add(v3add(p, v3mul(r, x)), v3mul(f, z * 0.3f));
    q.y = p.y + 0.15f;
    ps_style(ps, PK_STATUE, TEX_WALL_STONE, rgb(0.95f, 0.9f, 0.82f), 1.0f);
    ps_add(ps, PK_STATUE, q, atan2f(r.x, r.z) + 3.14159f + 0.3f * sinf((float)k), 0.22f + 0.04f * (float)(k % 3), 700 + (uint32_t)k);
  }
  ps_finish(ps);
}

static void build_intake_towers(cw_node parent, const track *t) {
  (void)t;
  pmc *c = pmc_new(80, 95);
  pcol conc = rgb(0.86f, 0.84f, 0.78f), conc_d = rgb(0.58f, 0.56f, 0.52f), steel = rgb(0.30f, 0.33f, 0.40f);
  float zs[2] = { 120.0f, -170.0f };
  for (int k = 0; k < 2; k++) {
    float z = zs[k], x = 52.0f;
    t3_vec3 base = v3(x, LAKE_Y - 22.0f, z);
    pm *b = pmc_at(c, base);
    pm_push(b);
    pm_move(b, x, LAKE_Y - 22.0f, z);
    pm_layer(b, PL_MAIN);
    pm_vary(b, 0.04f);
    pm_cyl(b, 8.0f, 6.6f, 22.0f + 38.0f, 16, conc_d, conc);       /* the shaft, rising from the lake bed to above the crest */
    pm_push(b);
    pm_move(b, 0, 60.0f, 0);
    pm_cyl(b, 9.2f, 9.2f, 3.0f, 16, conc_d, conc);                /* the gallery */
    pm_move(b, 0, 3.0f, 0);
    pm_cyl(b, 7.6f, 0.0f, 5.0f, 16, rgb(0.5f, 0.2f, 0.12f), rgb(0.75f, 0.32f, 0.18f)); /* the cap */
    pm_pop(b);
    /* a band of trash-rack slots near the water */
    pm_layer(b, PL_METAL);
    for (int i = 0; i < 8; i++) {
      float a = (float)i / 8.0f * 6.2831853f;
      pm_push(b);
      pm_move(b, cosf(a) * 7.3f, 17.0f, sinf(a) * 7.3f);
      pm_roty(b, -a);
      pm_box_c(b, 0.4f, 6.0f, 3.0f, rgb(0.2f, 0.22f, 0.26f));
      pm_pop(b);
    }
    pm_layer(b, PL_GLOW);
    for (int i = 0; i < 6; i++) {
      float a = (float)i / 6.0f * 6.2831853f + 0.3f;
      pm_push(b);
      pm_move(b, cosf(a) * 9.25f, 61.4f, sinf(a) * 9.25f);
      pm_box_c(b, 0.2f, 1.2f, 1.6f, glow(rgb(1.0f, 0.85f, 0.45f), 2.0f));
      pm_pop(b);
    }
    pm_pop(b);
    /* the service catwalk from the crest's edge out to the gallery */
    pm_layer(b, PL_METAL);
    pm *cb = pmc_at(c, v3(30.0f, 0, z));
    pm_beam(cb, v3(12.5f, 1.2f, z - 1.1f), v3(x - 9.0f, 1.2f, z - 1.1f), 0.18f, steel);
    pm_beam(cb, v3(12.5f, 1.2f, z + 1.1f), v3(x - 9.0f, 1.2f, z + 1.1f), 0.18f, steel);
    pm_beam(cb, v3(12.5f, 2.3f, z - 1.1f), v3(x - 9.0f, 2.3f, z - 1.1f), 0.1f, steel);
    pm_beam(cb, v3(12.5f, 2.3f, z + 1.1f), v3(x - 9.0f, 2.3f, z + 1.1f), 0.1f, steel);
    pm_layer(cb, PL_MAIN);
    pm_push(cb);
    pm_move(cb, 30.0f, 0.6f, z);
    pm_box_c(cb, 38.0f, 0.4f, 2.4f, conc_d);
    pm_pop(cb);
  }
  pmc_finish(c, parent, "IntakeTowers", true, TEX_WALL_CONCRETE);
}


/* ── the flooded tunnel: a green-lit lining, water running off the roof, a few lamps ────────────────── */

static void build_tunnel_lining(cw_node parent, const track *t) {
  /* a glazed lining (the old drab brown concrete is now a pale sea-green tile that takes the lamps' glow) */
  t3_material *m = phys(lin(0.5f, 0.78f, 0.72f), 0.35f, 0);
  tex_apply(m, TEX_TUNNEL, 1, 1, 1.0f, 1.0f);
  m->emissive = lin(0.12f, 0.8f, 0.5f);
  m->emissive_intensity = 2.2f;
  m->side = T3_DOUBLE_SIDE;
  const int ARC = 18;
  const float hc = 1.6f, Rt0 = 8.0f - 1.6f, inset = 0.1f;
  const int per = ARC + 3;
  for (int a = A.tun0; a < A.tun1; a += 90) {
    int b = a + 92 < A.tun1 ? a + 92 : A.tun1;
    tmesh mm = { 0 };
    int rows = 0;
    for (int i = a; i <= b; i += 2) {
      const tsample *s = &t->s[i];
      float Rr = s->hw - inset, Rt = Rt0 - inset;
      float len = 0, px = -Rr, py = hc;
      for (int k = -1; k <= ARC + 1; k++) {
        float x, y, nx, ny;
        if (k < 0) x = -Rr, y = 0, nx = 1, ny = 0;
        else if (k > ARC) x = Rr, y = 0, nx = -1, ny = 0;
        else {
          float an = 3.14159265f * (float)k / (float)ARC;
          x = -Rr * cosf(an), y = hc + Rt * sinf(an);
          nx = -x / (Rr * Rr), ny = -(y - hc) / (Rt * Rt);
          float nl = sqrtf(nx * nx + ny * ny);
          nx /= nl, ny /= nl;
        }
        len += sqrtf((x - px) * (x - px) + (y - py) * (y - py));
        px = x, py = y;
        t3_vec3 p = v3add(v3add(s->p, v3mul(s->r, x)), v3mul(s->u, y));
        t3_vec3 n = v3add(v3mul(s->r, nx), v3mul(s->u, ny));
        tv(&mm, p.x, p.y, p.z, n.x, n.y, n.z, len / 8.0f, s->s / 4.0f, (pcol){ 1, 1, 1 });
      }
      rows++;
    }
    for (int r = 0; r + 1 < rows; r++)
      for (int k = 0; k + 1 < per; k++) {
        int p = r * per + k, q = p + 1, u = p + per, v = u + 1;
        tt(&mm, p, q, v), tt(&mm, p, v, u);
      }
    t3_geometry *g = tfinish(&mm);
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.cast_shadow = false;
    me->base.receive_shadow = true;
    char nm[24];
    snprintf(nm, sizeof nm, "TunnelLining%d", a);
    hang(parent, nm, &me->base);
    t3_release(me);
    t3_release(g);
  }
  t3_release(m);
}

static const char *const R_DRIP[] = { "amount", "30", "lifetime", "0.8", "lifetime_random", "0.3", "emission_shape", "BOX", "emission_extents", "vec3(3.2, 0.05, 0.5)",
  "direction", "vec3(0, -1, 0)", "spread", "4", "speed_min", "0.4", "speed_max", "1.4", "gravity", "vec3(0, -13, 0)", "draw_mode", "STRETCHED", "stretch", "0.07",
  "size", "0.05", "size_random", "0.4", "color_ramp", "\"0 0.85 1 0.95 0.9, 1 0.7 0.95 0.9 0.5\"", "energy", "1.4", "blend", "ALPHA", "local_coords", "false", NULL };

static void build_tunnel_extras(cw_node parent, const track *t) {
  /* the water over the road in the ford */
  {
    water_opts w = water_defaults();
    w.color = rgb(0.18f, 0.62f, 0.52f);
    w.deep = rgb(0.02f, 0.2f, 0.2f);
    w.reflect = 0.8f;
    water_along_track(parent, t, 0.0f, 0.07f, &w);
  }
  /* lamps: a few real green lights down the tunnel, the rest of the glow is the lining's own */
  int lamps[6] = { A.tun0 + 25, A.tun0 + 90, A.tun0 + 150, A.cork0 + 20, A.cork0 + 130, A.tun1 - 25 };
  for (int k = 0; k < 6; k++) {
    int i = lamps[k];
    if (i < A.tun0 || i > A.tun1) continue;
    const tsample *s = &t->s[i];
    cw_node l = cw_node_new("OmniLight3D", "TunnelLamp");
    cw_node_add_child(parent, l);
    cw_node3d_set_position(l, v3add(s->p, v3mul(s->u, 5.5f)));
    cw_node_set(l, "color", cw_val_color(0.25f, 1.0f, 0.5f, 1));
    cw_node_set(l, "energy", cw_val_float(220.0f));
    cw_node_set(l, "range", cw_val_float(34.0f));
  }
  /* water streaming off the roof in the straight stretches (the corkscrew rolls, so it keeps dry) */
  int n = 0;
  for (int i = A.tun0 + 8; i < A.cork0 - 8; i += 11) {
    const tsample *s = &t->s[i];
    t3_vec3 p = v3add(s->p, v3mul(s->u, 6.6f));
    p = v3add(p, v3mul(s->r, ((n & 1) ? 2.2f : -2.2f)));
    float ex = fabsf(s->r.x) * 3.0f + fabsf(s->f.x) * 0.5f, ez = fabsf(s->r.z) * 3.0f + fabsf(s->f.z) * 0.5f;
    cw_node e = add_emitter(parent, "Drip", R_DRIP, p, 70, 800 + n);
    cw_node_set(e, "emission_extents", cw_val_vec3(ex, 0.05f, ez));
    n++;
  }
  for (int i = A.cork1 + 6; i < A.tun1 - 6; i += 11) {
    const tsample *s = &t->s[i];
    t3_vec3 p = v3add(s->p, v3mul(s->u, 6.6f));
    float ex = fabsf(s->r.x) * 3.0f + fabsf(s->f.x) * 0.5f, ez = fabsf(s->r.z) * 3.0f + fabsf(s->f.z) * 0.5f;
    cw_node e = add_emitter(parent, "Drip", R_DRIP, p, 70, 800 + n);
    cw_node_set(e, "emission_extents", cw_val_vec3(ex, 0.05f, ez));
    n++;
  }
}

/* ── the dam's jet gates, the mist and the rainbow ────────────────────────────────────────────────── */

static void build_jets(cw_node parent, const track *t, t3_vec3 sun_to) {
  t3_texture *tex = SC.tex[0];
  t3_material *mat = foam_material(tex, 1.0f);
  /* the closing run: find the samples nearest these z on the x = 0 line */
  static const float JZ[3] = { -430.0f, -392.0f, -354.0f };
  pmc *c = pmc_new(80, 97);
  for (int j = 0; j < 3; j++) {
    int idx = t->n - 1;
    float bestd = 1e9f;
    for (int i = A.loop1 + 60; i < t->n; i++) {
      float d = fabsf(t->s[i].p.z - JZ[j]);
      if (d < bestd) bestd = d, idx = i;
    }
    const tsample *s = &t->s[idx];
    t3_vec3 r = flat_right(s);
    float reach = s->hw + 1.0f + 0.07f * s->hw + 0.6f, base = s->p.y - 0.4f;
    float g0 = 9.0f;
    t3_vec3 o = v3add(s->p, v3mul(r, reach + g0));
    o.y = base - SLOPE_K * (g0 - 2.5f) + 1.5f;
    /* the steel cone the jet leaves from */
    pm *b = pmc_at(c, o);
    pm_push(b);
    pm_layer(b, PL_METAL);
    pm_basis(b, o, v3(0, 1, 0), v3norm(v3add(v3mul(r, 0.4f), v3(0, 0.9f, 0))), r);
    pm_cyl(b, 3.0f, 1.8f, 4.0f, 12, rgb(0.5f, 0.52f, 0.56f), rgb(0.78f, 0.8f, 0.84f));
    pm_pop(b);
    /* the arc of the jet */
    t3_vec3 pts[24];
    float hwj[24], vst[24];
    int n = 0;
    float vh = 17.0f, vv = 7.5f, g = 20.0f;
    for (int k = 0; k < 24; k++) {
      float tt_ = (float)k * 0.19f;
      t3_vec3 p = v3add(o, v3mul(r, vh * tt_));
      p.y = o.y + 3.0f + vv * tt_ - 0.5f * g * tt_ * tt_;
      if (p.y < A.yfloor - 0.5f) break;
      pts[n] = p;
      hwj[n] = 2.2f + 0.26f * (float)k;
      vst[n] = 10.0f;
      n++;
    }
    if (n < 4) continue;
    char nm[24];
    snprintf(nm, sizeof nm, "Jet%d", j);
    /* the ribbon lies flat to the ground plane's side direction, so it reads from the road and from the lake */
    flow_ribbon(parent, nm, pts, hwj, vst, n, 6, 0.15f, 5.0f, mat, sun_to);
    t3_vec3 land = pts[n - 1];
    add_emitter(parent, "JetMist", R_MIST, v3(land.x, land.y + 6.0f, land.z), 330, 900 + j);
    add_emitter(parent, "JetPlume", R_PLUME, v3(land.x, land.y + 2.0f, land.z), 260, 910 + j);
    add_emitter(parent, "JetSpray", R_SPRAY, v3add(o, v3(0, 3.0f, 0)), 140, 920 + j);
  }
  pmc_finish(c, parent, "JetGates", true, -1);
  t3_release(mat);
}

/* a rainbow: seven bands on an arc, added to the sky, standing in the plane that faces `from` */
static void build_rainbow(cw_node parent, const char *name, t3_vec3 centre, t3_vec3 from, float R, float bw, float alpha) {
  t3_vec3 n = v3sub(from, centre);
  n.y = 0;
  n = v3norm(n);
  t3_vec3 e1 = v3norm(v3cross(v3(0, 1, 0), n)); /* along the arc's width */
  static const float BAND[7][3] = { { 1.0f, 0.15f, 0.1f }, { 1.0f, 0.5f, 0.1f }, { 1.0f, 0.95f, 0.2f }, { 0.25f, 0.9f, 0.3f }, { 0.2f, 0.65f, 1.0f }, { 0.3f, 0.3f, 0.95f }, { 0.65f, 0.25f, 0.9f } };
  tmesh m = { 0 };
  const int N = 40;
  for (int b = 0; b <= 7; b++) {
    float r = R + (float)b * bw;
    for (int i = 0; i <= N; i++) {
      float a = 3.14159265f * (float)i / (float)N;
      t3_vec3 p = v3add(centre, v3add(v3mul(e1, cosf(a) * r), v3(0, sinf(a) * r, 0)));
      float fade = sm(0.0f, 0.18f, sinf(a)) * 1.0f;
      float k = b < 7 ? 1.0f : 0.0f;
      int bi = b < 7 ? b : 6;
      pcol col = { BAND[bi][0] * alpha * fade * k, BAND[bi][1] * alpha * fade * k, BAND[bi][2] * alpha * fade * k };
      (void)k;
      tv(&m, p.x, p.y, p.z, n.x, n.y, n.z, (float)i / (float)N, (float)b / 7.0f, col);
    }
  }
  /* each band is a strip between ring b and ring b + 1, coloured by the lower ring's colour (so the last ring has none) */
  for (int b = 0; b < 7; b++)
    for (int i = 0; i < N; i++) {
      int p = b * (N + 1) + i, q = p + 1, u = p + N + 1, v = u + 1;
      tt(&m, p, u, v), tt(&m, p, v, q);
    }
  t3_geometry *g = tfinish(&m);
  t3_material *mat = t3_mesh_basic_material_new(0xffffff);
  mat->vertex_colors = true;
  mat->transparent = true;
  mat->blending = T3_ADDITIVE_BLENDING;
  mat->depth_write = false;
  mat->side = T3_DOUBLE_SIDE;
  mat->fog = false;
  t3_mesh *me = t3_mesh_new(g, mat);
  me->base.cast_shadow = false;
  me->base.receive_shadow = false;
  hang(parent, name, &me->base);
  t3_release(me);
  t3_release(g);
  t3_release(mat);
}

/* ── the lake ───────────────────────────────────────────────────────────────────────────────────────── */

static void build_lake(cw_node parent) {
  water_opts w = water_defaults();
  w.color = rgb(0.04f, 0.78f, 0.74f);
  w.deep = rgb(0.0f, 0.30f, 0.38f);
  w.clarity = 0.2f;
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
  if (getenv("RACER_DAM_CHECK")[0] == 'k')
    for (int i = 1500; i < 1700; i += 25) {
      const tsample *s = &t->s[i];
      for (int g = 6; g <= 30; g += 8) {
        t3_vec3 p = v3add(s->p, v3mul(s->r, s->hw + (float)g));
        int kd = -1;
        float h = dam_land(p.x, p.z, &kd);
        cw_info("dam kind s=%d g=%d kind=%d h=%.1f road=%.1f", i, g, kd, h, s->p.y);
      }
    }
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
  for (int i = A.gate + 25; i < A.spill1 + 20; i += (PV > 1 ? 76 : 38)) {
    const tsample *s = &t->s[i];
    t3_vec3 p = v3add(s->p, v3mul(flat_right(s), -17.0f));
    p.y = s->p.y - 2.0f;
    char nm[24];
    snprintf(nm, sizeof nm, "Plume%d", n);
    add_emitter(parent, nm, R_PLUME, p, 150, 100 + n);
    n++;
  }
  /* fine spray right at the wall, over the road, close to the camera */
  for (int i = A.gate + 40; i < A.spill1 + 10; i += (PV > 1 ? 52 : 26)) {
    const tsample *s = &t->s[i];
    t3_vec3 p = v3add(s->p, v3mul(flat_right(s), -13.0f));
    p.y = s->p.y + 0.4f;
    char nm[24];
    snprintf(nm, sizeof nm, "Spray%d", n);
    add_emitter(parent, nm, R_SPRAY, p, 80, 300 + n);
    n++;
  }
  /* mist over the churning water at the foot and along the canyon */
  for (int i = 0; i < NRV; i += (PV > 1 ? 28 : 14)) {
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
  deco_supports(root, t, SUP_TRESTLE, dam_ground_fn, NULL, A.yfloor - 3.0f, 3.0f, &st);
  deco_banners(root, t, 120.0f, crest_end - 120.0f, 130.0f, &st);
  /* the jump: a run of lit hoops leads the eye to the lip, so it cannot be missed */
  deco_banners(root, t, t->s[A.gap0].s - 150.0f, t->s[A.gap0].s - 20.0f, 32.0f, &st);
  deco_lights(root, t, 36.0f, LAMP_POLE, t->s[A.climb0].s, t->s[A.loop0].s - 20.0f, &st);
  deco_banners(root, t, run0 + 40.0f, t->length - 60.0f, 130.0f, &st);
}

/* ── the closing crest: the reservoir opened up, a balustrade, lamp standards, flags and the gate hoists ── */

/* The last half kilometre runs along the crest, and the solid 1.6 m wall on the reservoir side hid the water: a long
 * grey straight between two grey walls. On that side the wall is no longer drawn (its collision stays exactly as it
 * was, so nothing changes under the wheels) and a balustrade stands on its line instead: a concrete plinth, open
 * steel rails and a capping rail, posts every three metres, so the lake shows between and over them. Every 36 m a
 * lamp standard rises from a pier, every other one flying a long banner out over the water; on the downstream side the
 * spillway gates' hoist towers stand in a row joined by a walkway, so the straight has a rhythm to read the speed by. */
static int crest_i0, crest_i1;
static float crest_side; /* the lake's side of the road, as a sign of lat */

static void crest_find(const track *t) {
  int j = t->n - 1;
  while (j > 0 && fabsf(t->s[j].p.x) < 1.0f) j--;
  crest_i0 = j + 12, crest_i1 = t->n - 30;
  const tsample *m = &t->s[(crest_i0 + crest_i1) / 2];
  crest_side = v3add(m->p, m->r).x > m->p.x ? 1.0f : -1.0f;
}

/* the drawn wall (its face and its top) on the lake's side of the crest collapses to a line under the road's edge */
static void crest_unwall(cw_node scene, const track *t) {
  cw_node world = cw_node_get(scene, "World");
  const track_geo *geo = world_geo();
  if (!cw_node_valid(world) || !geo) return;
  int moved = 0;
  for (int ci = 0; ci < geo->n; ci++) {
    const track_chunk *c = &geo->c[ci];
    if ((c->kind != TM_WALL && c->kind != TM_WALLTOP) || c->last < crest_i0 || c->first > crest_i1) continue;
    char name[24];
    snprintf(name, sizeof name, "T%d_%d", c->kind, ci);
    cw_node n = cw_node_get(world, name);
    if (!cw_node_valid(n)) continue;
    t3_object *o = cw_node3d_object(n);
    if (!o || o->child_count < 1 || o->children[0]->type != T3_MESH) continue;
    t3_geometry *g = ((t3_mesh *)o->children[0])->geometry;
    t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
    if (!pa || pa->type != T3_FLOAT32) continue;
    float *P = t3_attribute_f32(pa);
    for (int v = 0; v < pa->count; v++) {
      t3_vec3 p = v3(P[v * 3], P[v * 3 + 1], P[v * 3 + 2]);
      int best = -1;
      float bd = 1e9f;
      for (int i = c->first; i <= c->last && i < t->n; i++) {
        float dx = p.x - t->s[i].p.x, dz = p.z - t->s[i].p.z, d = dx * dx + dz * dz;
        if (d < bd) bd = d, best = i;
      }
      if (best < crest_i0 || best > crest_i1) continue;
      const tsample *s = &t->s[best];
      float lat = v3dot(v3sub(p, s->p), s->r);
      if (lat * crest_side <= 0) continue;
      t3_vec3 q = v3add(v3add(s->p, v3mul(s->r, crest_side * s->hw)), v3mul(s->u, -0.5f));
      P[v * 3] = q.x, P[v * 3 + 1] = q.y, P[v * 3 + 2] = q.z;
      moved++;
    }
    pa->version++;
    t3_geometry_compute_bounding_sphere(g);
  }
  cw_info("dam: crest wall opened on the lake side (%d vertices)", moved);
}

static void build_crest(cw_node parent, const track *t) {
  pmc *c = pmc_new(260.0f, 4040u);
  pcol conc = rgb(0.88f, 0.86f, 0.80f), conc_d = rgb(0.62f, 0.60f, 0.56f), steel = rgb(0.22f, 0.25f, 0.32f), brass = rgb(0.86f, 0.62f, 0.22f);
  pcol bann[3] = { rgb(0.92f, 0.2f, 0.14f), rgb(0.98f, 0.78f, 0.08f), rgb(0.1f, 0.58f, 0.68f) };
  float s0 = t->s[crest_i0].s, s1 = t->s[crest_i1].s, side = crest_side;
  /* the balustrade: plinth and cap as continuous runs, posts and rails between */
  t3_vec3 prev[4];
  int k = 0;
  for (float s = s0; s <= s1; s += 3.0f, k++) {
    tsample a = track_at(t, s);
    float lat = side * (a.hw + 0.18f);
    t3_vec3 base = track_point(t, s, lat, 0);
    t3_vec3 pts[4] = { v3add(base, v3(0, 0.22f, 0)), v3add(base, v3(0, 0.62f, 0)), v3add(base, v3(0, 0.95f, 0)), v3add(base, v3(0, 1.3f, 0)) };
    pm *b = pmc_at(c, base);
    pm_push(b);
    if (k > 0) {
      pm_layer(b, PL_MAIN);
      pm_beam(b, prev[0], pts[0], 0.5f, conc_d);     /* the plinth */
      pm_beam(b, prev[3], pts[3], 0.34f, conc);      /* the capping rail */
      pm_layer(b, PL_METAL);
      pm_rod(b, prev[1], pts[1], 0.05f, 5, steel);
      pm_rod(b, prev[2], pts[2], 0.05f, 5, steel);
    }
    pm_layer(b, PL_MAIN);
    pm_beam(b, base, pts[3], 0.3f, conc);            /* a post */
    pm_pop(b);
    for (int i = 0; i < 4; i++) prev[i] = pts[i];
  }
  /* lamp standards on piers, banners out over the water */
  int n = 0;
  for (float s = s0 + 12.0f; s < s1 - 6.0f; s += 36.0f, n++) {
    tsample a = track_at(t, s);
    t3_vec3 base = track_point(t, s, side * (a.hw + 0.7f), 0);
    t3_vec3 out = v3mul(flat_right(&a), side);       /* toward the lake */
    pm *b = frame_world(c, base, a.f);
    pm_layer(b, PL_MAIN);
    pm_box(b, 1.1f, 2.0f, 1.1f, conc_d, conc);
    pm_push(b), pm_move(b, 0, 2.0f, 0), pm_box(b, 1.3f, 0.25f, 1.3f, conc, conc), pm_pop(b);
    pm_layer(b, PL_METAL);
    pm_push(b), pm_move(b, 0, 2.2f, 0), pm_cyl(b, 0.2f, 0.12f, 6.0f, 8, steel, steel), pm_pop(b);
    pm_push(b), pm_move(b, 0, 7.6f, 0), pm_box_c(b, 2.6f, 0.14f, 0.14f, brass), pm_pop(b);
    pm_layer(b, PL_GLOW);
    for (int e = -1; e <= 1; e += 2) {
      pm_push(b), pm_move(b, (float)e * 1.25f, 7.25f, 0), pm_ball(b, 0.38f, 0.45f, 0.38f, 1, 0, glow(rgb(1.0f, 0.86f, 0.6f), 3.0f), glow(rgb(1.0f, 0.86f, 0.6f), 3.0f)), pm_pop(b);
    }
    pm_push(b), pm_move(b, 0, 8.4f, 0), pm_ball(b, 0.3f, 0.3f, 0.3f, 1, 0, glow(rgb(1.0f, 0.86f, 0.6f), 3.0f), glow(rgb(1.0f, 0.86f, 0.6f), 3.0f)), pm_pop(b);
    pm_pop(b);
    if (n & 1) {
      /* a banner on an arm reaching out over the water, hanging in two panels with a kink in it */
      pm *q = pmc_at(c, base);
      pm_push(q);
      pm_layer(q, PL_METAL);
      t3_vec3 a0 = v3add(base, v3(0, 6.6f, 0)), a1 = v3add(a0, v3mul(out, 2.4f));
      pm_rod(q, a0, a1, 0.06f, 5, steel);
      pm_layer(q, PL_MAIN);
      pcol bc = bann[(n / 2) % 3];
      t3_vec3 f = flat_dir(a.f), d = v3mul(f, 0.15f);
      t3_vec3 p0 = v3add(base, v3mul(out, 0.5f)), p1 = v3add(base, v3mul(out, 2.4f));
      t3_vec3 q0 = v3add(p0, v3(0, 6.5f, 0)), q1 = v3add(p1, v3(0, 6.5f, 0)), m0 = v3add(v3add(p0, v3(0, 4.6f, 0)), d), m1 = v3add(v3add(p1, v3(0, 4.6f, 0)), d);
      t3_vec3 l0 = v3add(p0, v3(0, 3.0f, 0)), l1 = v3add(v3add(p1, v3(0, 2.6f, 0)), v3mul(f, -0.1f));
      pm_quad(q, q0, m0, m1, q1, bc), pm_quad(q, q0, q1, m1, m0, bc);
      pm_quad(q, m0, l0, l1, m1, pc_mul(bc, 0.85f)), pm_quad(q, m0, m1, l1, l0, pc_mul(bc, 0.85f));
      pm_pop(q);
    }
  }
  /* the hoist towers of the spillway gates on the downstream side, a walkway between them. They stood 4 m off the
   * road with their roofs 17 m up, so from a kart the top of the view cut them off as they came alongside and they
   * read as bare stumps. Now the hoist house sits on the pier 8 m up and the tower stands 7.5 m off the road (its pier
   * reaching further down the dam's face): the red roof at 13 m stays in the view the whole time a tower is in it. */
  t3_vec3 last = { 0 };
  bool have = false;
  const float HH = 8.0f; /* the hoist house's floor above the deck */
  for (float s = s0 + 30.0f; s < s1 - 30.0f; s += 64.0f) {
    tsample a = track_at(t, s);
    t3_vec3 base = track_point(t, s, -side * (a.hw + 7.5f), 0);
    pm *b = frame_world(c, base, a.f);
    pm_layer(b, PL_MAIN);
    pm_push(b), pm_move(b, 0, -16.0f, 0), pm_box(b, 3.4f, 16.0f + HH, 6.0f, conc_d, conc), pm_pop(b);     /* the pier */
    /* the pier's head flares out to carry the house */
    pm_push(b), pm_move(b, 0, HH - 0.9f, 0), pm_box(b, 4.4f, 0.9f, 6.8f, conc_d, conc), pm_pop(b);
    pm_push(b), pm_move(b, 0, HH, 0), pm_box(b, 5.0f, 3.6f, 7.4f, rgb(0.92f, 0.9f, 0.84f), rgb(0.98f, 0.97f, 0.92f)); /* the hoist house */
    pm_move(b, 0, 3.6f, 0), pm_prism(b, 5.8f, 1.5f, 8.2f, rgb(0.6f, 0.2f, 0.12f), rgb(0.84f, 0.3f, 0.18f)), pm_pop(b);
    pm_layer(b, PL_METAL);
    /* the gate's lifting chains down the pier's face, and its hazard band */
    for (int e = -1; e <= 1; e += 2) pm_rod(b, v3((float)e * 1.2f, HH - 0.9f, 3.1f), v3((float)e * 1.2f, -6.0f, 3.1f), 0.12f, 5, steel);
    pm_layer(b, PL_MAIN);
    pm_push(b), pm_move(b, 0, 2.2f, 0), pm_box(b, 3.5f, 0.8f, 6.1f, rgb(0.98f, 0.76f, 0.08f), rgb(0.98f, 0.76f, 0.08f)), pm_pop(b);
    pm_layer(b, PL_GLOW);
    for (int e = -1; e <= 1; e += 2) pm_push(b), pm_move(b, (float)e * 2.52f, HH + 1.8f, 0), pm_box_c(b, 0.1f, 1.1f, 3.2f, glow(rgb(1.0f, 0.82f, 0.5f), 2.4f)), pm_pop(b);
    pm_push(b), pm_move(b, 0, HH + 5.4f, 0), pm_box_c(b, 0.45f, 0.45f, 0.45f, glow(rgb(1.0f, 0.15f, 0.1f), 5.0f)), pm_pop(b);
    pm_pop(b);
    t3_vec3 top = v3add(base, v3(0, HH - 0.9f, 0));
    if (have) {
      pm *w = pmc_at(c, top);
      pm_push(w);
      pm_layer(w, PL_METAL);
      pm_beam(w, last, top, 0.5f, steel);
      pm_beam(w, v3add(last, v3(0, 1.1f, 0)), v3add(top, v3(0, 1.1f, 0)), 0.08f, steel);
      pm_pop(w);
    }
    last = top, have = true;
  }
  pmc_finish(c, parent, "Crest", true, TEX_WALL_CONCRETE);
}

/* ── the water tunnel's lining: ribs, cable trays, a tide line and lit section numbers ──────────── */

/* The tunnel was one smooth drab tube. Steel ring frames every 8 m (dark, so the green glow between them reads as
 * bays), a pair of cable trays high on the walls, a band of hazard paint at the water's old level and a lit section
 * plate every fourth frame give it a builder's character; all of it stays above head height or flat on the walls. */
static void build_tunnel_ribs(cw_node parent, const track *t) {
  pmc *c = pmc_new(120.0f, 4050u);
  pcol steel = rgb(0.16f, 0.18f, 0.2f), tray = rgb(0.5f, 0.52f, 0.5f), haz = rgb(0.98f, 0.62f, 0.06f), dark = rgb(0.05f, 0.05f, 0.05f);
  const float hc = 1.6f, Rt0 = 8.0f - 1.6f;
  int ribs = 0;
  for (int i = A.tun0 + 4; i < A.tun1 - 3; i += 8, ribs++) {
    const tsample *s = &t->s[i];
    float Rr = s->hw - 0.1f, Rt = Rt0 - 0.1f;
    pm *b = pmc_at(c, s->p);
    pm_push(b);
    pm_basis(b, s->p, s->r, s->u, s->f);
    pm_layer(b, PL_METAL);
    /* the frame over the arch, from 2.4 m up one side to the other */
    const int N = 12;
    float a0 = asinf(fminf(1.0f, (2.6f - hc) / Rt));
    for (int k = 0; k < N; k++) {
      float u0 = a0 + (3.14159265f - 2 * a0) * (float)k / N, u1 = a0 + (3.14159265f - 2 * a0) * (float)(k + 1) / N;
      t3_vec3 p0 = v3(-(Rr - 0.15f) * cosf(u0), hc + (Rt - 0.15f) * sinf(u0), 0), p1 = v3(-(Rr - 0.15f) * cosf(u1), hc + (Rt - 0.15f) * sinf(u1), 0);
      pm_beam(b, p0, p1, 0.32f, steel);
    }
    /* the cable trays, high on both walls, run from this frame to the next */
    if (i + 8 < A.tun1 - 3) {
      const tsample *n = &t->s[i + 8];
      float y = 4.3f, sa = (y - hc) / Rt, x = Rr * sqrtf(fmaxf(0.0f, 1.0f - sa * sa)) - 0.3f;
      for (int e = -1; e <= 1; e += 2) {
        t3_vec3 w0 = v3add(v3add(s->p, v3mul(s->r, (float)e * x)), v3mul(s->u, y));
        t3_vec3 w1 = v3add(v3add(n->p, v3mul(n->r, (float)e * (n->hw - 0.1f) * sqrtf(fmaxf(0.0f, 1.0f - sa * sa)) - (float)e * 0.3f)), v3mul(n->u, y));
        pm_pop(b);
        b = pmc_at(c, s->p);
        pm_push(b);
        pm_rod(b, w0, w1, 0.16f, 6, tray);
        pm_rod(b, v3add(w0, v3mul(s->u, -0.3f)), v3add(w1, v3mul(n->u, -0.3f)), 0.08f, 5, dark);
        /* the tide line: a hazard band flat on the wall at kerb height, alternating with black */
        pm_layer(b, PL_MAIN);
        t3_vec3 h0 = v3add(v3add(s->p, v3mul(s->r, (float)e * (s->hw - 0.12f))), v3mul(s->u, 0.9f));
        t3_vec3 h1 = v3add(v3add(n->p, v3mul(n->r, (float)e * (n->hw - 0.12f))), v3mul(n->u, 0.9f));
        t3_vec3 g0 = v3add(h0, v3mul(s->u, 0.45f)), g1 = v3add(h1, v3mul(n->u, 0.45f));
        pcol hc_ = (ribs & 1) ? haz : dark;
        pm_quad(b, h0, h1, g1, g0, hc_), pm_quad(b, h1, h0, g0, g1, hc_);
        pm_layer(b, PL_METAL);
      }
      pm_pop(b);
      b = pmc_at(c, s->p);
      pm_push(b);
      pm_basis(b, s->p, s->r, s->u, s->f);
    }
    if (ribs % 4 == 0) {
      /* a lit section plate on the crown */
      pm_layer(b, PL_GLOW);
      pm_push(b), pm_move(b, 0, hc + Rt - 0.45f, 0.4f), pm_box_c(b, 1.6f, 0.12f, 0.7f, glow(rgb(1.0f, 0.7f, 0.2f), 2.6f)), pm_pop(b);
    }
    pm_pop(b);
  }
  pmc_finish(c, parent, "TunnelRibs", false, -1);
}

static void dam_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  PV = game_planned_views();
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
  t3_vec3 sun_to = v3norm(v3(0.48f, -0.67f, 0.57f)); /* the way the sun's light travels (see the scene file) */
  build_white_water(root, sun_to);
  build_gates(root, t);
  build_valley(root, t);
  build_dam_skin(root, t);
  {
    t3_material *conc = phys((t3_color){ 1, 1, 1 }, 0.9f, 0);
    tex_apply(conc, TEX_WALL_CONCRETE, 1, 1, 1.0f, 1.0f);
    conc->color = lin(0.8f, 0.78f, 0.74f);
    conc->side = T3_DOUBLE_SIDE;
    build_tunnel_hill(root, t, TMAT, conc);
    t3_release(conc);
  }
  build_tunnel_lining(root, t);
  build_tunnel_ribs(root, t);
  build_tunnel_extras(root, t);
  crest_find(t);
  crest_unwall(scene, t);
  build_crest(root, t);
  build_emitters(root, t);
  build_jets(root, t, sun_to);
  build_land_props(root, t);
  build_sky_life(root);
  build_bridge(root, 300.0f);
  build_lookout(root, t);
  build_intake_towers(root, t);
  {
    /* rainbows: over the jets as seen from the closing run, and over the foot of the spillway as seen from the descent */
    const tsample *c0 = &t->s[t->n - 380];
    build_rainbow(root, "RainbowJets", v3(-58.0f, A.yfloor + 4.0f, c0->p.z + 40.0f), v3(0.0f, 4.0f, c0->p.z - 140.0f), 78.0f, 3.4f, 0.55f);
    const tsample *g0 = &t->s[A.spill1];
    t3_vec3 foot = v3add(g0->p, v3mul(flat_right(g0), -30.0f));
    build_rainbow(root, "RainbowPool", v3(foot.x + 40.0f, A.yfloor + 2.0f, foot.z), v3add(g0->p, v3mul(flat_dir(g0->f), -220.0f)), 70.0f, 3.2f, 0.5f);
  }
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
  water_frame(time);
  sky_life_frame(time);
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
