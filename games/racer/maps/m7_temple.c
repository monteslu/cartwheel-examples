/* map 7: Overgrowth Temple. A lost jungle city in the early morning mist.
 *
 * The course (the coordinator's, do not change the pieces): a start straight, an S-bend, a temple tunnel, a banked turn,
 * a flooded stretch under a waterfall, an S-bend, a rope bridge over a gorge, a jump, a vertical loop, a helix that climbs
 * round a stepped pyramid, a second tunnel and a long descent. Everything below is placed from the built track (flags
 * and sample positions), never from fixed numbers, so the set pieces follow the course if it is retuned.
 *
 * Set pieces: the start gate (two carved guardians holding a banner), the waterfall ford, the rope bridge over the
 * misty gorge, the stone ring the loop runs through, and the pyramid the helix climbs with the canopy below it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game.h"
#include "../textures.h" /* the map_def names its road and wall textures, also in the host-side track tools */

static const piece SPEC[] = {
  WALLS(2, 2),
  SURFACE(SURF_METAL),
  SURFACE(SURF_ROAD),
  ITEMS(),
  STRAIGHT(120),
  CHECK(),
  BOOST(30),
  STRAIGHT(30),
  BANKED(-40, 100, -12),
  BANKED(40, 100, 12),
  TUNNEL(1),
  STRAIGHT(80),
  TUNNEL(0),
  ITEMS(),
  ABANKED(90, 130, 14),
  STRAIGHT(30),
  WATER(100),
  STRAIGHT(60),
  BANKED(-40, 100, -12),
  BANKED(40, 100, 12),
  STRAIGHT(30),
  CHECK(),
  SURFACE(SURF_WOOD),
  WALLS(0, 0),
  STRAIGHT(150),
  WALLS(2, 2),
  SURFACE(SURF_ROAD),
  ABANKED(90, 130, 14),
  ITEMS(),
  STRAIGHT(20),
  BOOST(30),
  STRAIGHT(30),
  SLOPE(36, 20),
  SLOPE(22, 20),
  JUMP(0, 30),
  SLOPE(80, 0),
  STRAIGHT(30),
  LOOP(22, 1),
  STRAIGHT(40),
  ITEMS(),
  CHECK(),
  ABANKED(90, 130, 14),
  HILL(300, 360, 14),
  STRAIGHT(30),
  TUNNEL(1),
  STRAIGHT(70),
  TUNNEL(0),
  SLOPE(130, -13),
  STRAIGHT(80),
  SLOPE(130, 0),
  ABANKED(90, 130, 14),
  ASTRAIGHT(100),
  ASLOPE(50, 0),
  ASTRAIGHT(100),
  STRAIGHT(40),
  END(),
};

#define MAP_SPEC (&(track_spec){ .name = "temple", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 6.5f, .circuit = true })

#ifndef MAPFILE /* the scenery needs the engine: the host-side track tools include this file for the course only */

#include "cartwheel/math.h"
#include "../props.h"

/* ── small helpers ──────────────────────────────────────────────────────────────────────────────────── */

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V(a.x * k, a.y * k, a.z * k); }
static float vdot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float sstep(float a, float b, float x) { return cw_smoothstep(a, b, x); }

/* a small deterministic generator for placing things. Draw each value in a statement (or declarator) of its own, never two
 * in one call's arguments or one braced initializer: C leaves their order unspecified, and GCC (native) and clang (carts)
 * evaluate them in different orders, so the same seed grew different trees on the two hosts (tools/check-eval-order.mjs,
 * run by tools/build.sh, finds them). */
typedef struct { uint32_t s; } rng;
static float rnd(rng *r) {
  r->s = r->s * 1664525u + 1013904223u;
  uint32_t h = r->s;
  h ^= h >> 15, h *= 0x2c1b3c6du, h ^= h >> 12;
  return (float)(h & 0xffffff) / 16777216.0f;
}
static float rnds(rng *r) { return rnd(r) * 2 - 1; }

/* the palette: deep greens with golden light, turquoise water, red and gold temple accents */
#define STONE_D rgb(0.30f, 0.31f, 0.26f)
#define STONE_L rgb(0.62f, 0.60f, 0.48f)
#define STONE_MOSS rgb(0.26f, 0.42f, 0.18f)
#define GOLD rgb(0.95f, 0.72f, 0.18f)
#define RED rgb(0.72f, 0.12f, 0.08f)
#define WOOD_D rgb(0.22f, 0.14f, 0.08f)
#define WOOD_L rgb(0.46f, 0.31f, 0.17f)
#define ROPE rgb(0.72f, 0.6f, 0.38f)

/* RACER_TEMPLE_OFF=a,b (a development switch): leave out parts of the scenery by name, to see what draws what */
static bool off(const char *name) {
  const char *e = getenv("RACER_TEMPLE_OFF");
  return e && strstr(e, name);
}

static const track *T;
static int NV = 1; /* views planned for this race: more than one thins the scenery */
static float thin(float one, float many) { return NV > 1 ? many : one; }
static ground_opts G;
static rng R;

/* ── the course read back: stretches by flag ───────────────────────────────────────────────────────── */

typedef struct { float s0, s1; } range;
static int ranges_of(const track *t, uint32_t flag, bool want, range *out, int max) {
  int n = 0;
  bool in = false;
  for (int i = 0; i < t->n; i++) {
    bool has = ((t->s[i].flags & flag) != 0) == want;
    if (has && !in) {
      if (n < max) out[n].s0 = t->s[i].s;
      in = true;
    }
    if (!has && in) {
      if (n < max) out[n].s1 = t->s[i].s;
      n++;
      in = false;
    }
  }
  if (in) {
    if (n < max) out[n].s1 = t->length;
    n++;
  }
  return n < max ? n : max;
}

static struct {
  range tunnel[4];
  int ntunnel;
  range water;
  range bridge;
  float loop0, loop1;
  float gap0, gap1;
  t3_vec3 hel_c;      /* the helix's axis (the pyramid) */
  float hel_r, hel_s0, hel_s1;
  float gorge_s;      /* the bridge's middle */
} C;

static void read_course(const track *t) {
  range w[2], o[4];
  C.ntunnel = ranges_of(t, TF_TUNNEL, true, C.tunnel, 4);
  if (ranges_of(t, TF_WATER, true, w, 2) > 0) C.water = w[0];
  int no = ranges_of(t, TF_OPEN, true, o, 4);
  for (int i = 0; i < no; i++)
    if (o[i].s1 - o[i].s0 > 60) C.bridge = o[i];
  C.gorge_s = (C.bridge.s0 + C.bridge.s1) * 0.5f;
  if (t->nloops > 0) C.loop0 = t->s[t->loop_from[0]].s, C.loop1 = t->s[t->loop_to[0] < t->n ? t->loop_to[0] : t->n - 1].s;
  if (t->ngaps > 0) C.gap0 = t->s[t->gap_from[0]].s, C.gap1 = t->s[t->gap_to[0] < t->n ? t->gap_to[0] : t->n - 1].s;
  /* the helix: the piece that turns a full circle (the HILL), found in the spec, and the samples it made */
  int hp = -1;
  for (int i = 0; i < (int)(sizeof SPEC / sizeof SPEC[0]); i++)
    if (SPEC[i].kind == P_MOVE && fabsf(SPEC[i].b) >= 300) hp = i;
  int a = -1, b = -1;
  for (int i = 0; i < t->n; i++)
    if (t->s[i].seg == hp) {
      if (a < 0) a = i;
      b = i;
    }
  if (a < 0) a = 0, b = t->n - 1;
  float cx = 0, cz = 0;
  int cnt = 0;
  for (int i = a; i <= b; i += 2) cx += t->s[i].p.x, cz += t->s[i].p.z, cnt++;
  C.hel_c = V(cx / (float)cnt, 0, cz / (float)cnt);
  float rr = 0;
  for (int i = a; i <= b; i += 2) rr += sqrtf((t->s[i].p.x - C.hel_c.x) * (t->s[i].p.x - C.hel_c.x) + (t->s[i].p.z - C.hel_c.z) * (t->s[i].p.z - C.hel_c.z));
  C.hel_r = rr / (float)cnt;
  C.hel_s0 = t->s[a].s, C.hel_s1 = t->s[b].s;
}

/* ── the ground ─────────────────────────────────────────────────────────────────────────────────────── */

/* distance from p to the segment a..b on the ground plane */
static float seg_dist(float px, float pz, float ax, float az, float bx, float bz, float *along) {
  float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
  float t = l2 > 0 ? clampf(((px - ax) * dx + (pz - az) * dz) / l2, 0, 1) : 0;
  if (along) *along = t;
  float qx = ax + dx * t - px, qz = az + dz * t - pz;
  return sqrtf(qx * qx + qz * qz);
}

/* the gorge: a trench across the course at the bridge, walls dropping away from the bridge's two ends */
static struct { t3_vec3 c, f, r; float half, depth; } GORGE;
/* the waterfall river: a polyline the ground is cut along */
#define NRIV 8
static struct { t3_vec3 p[NRIV]; int n; float width; } RIVERS[2];
static struct { t3_vec3 c, r, f; float cliff_lat, h; } FALL;
static struct { t3_vec3 c; float base, top, h, yaw; } PYR;
#define FALL_AL 12.0f

/* how deep the waterfall's rivers cut the ground at (x, z): 0 away from them, 1 in the middle of the bed */
static float river_cut(float x, float z) {
  float cut = 0;
  for (int k = 0; k < 2; k++) {
    float best = 1e9f;
    for (int i = 0; i + 1 < RIVERS[k].n; i++) best = fminf(best, seg_dist(x, z, RIVERS[k].p[i].x, RIVERS[k].p[i].z, RIVERS[k].p[i + 1].x, RIVERS[k].p[i + 1].z, NULL));
    float w = RIVERS[k].width;
    cut = fmaxf(cut, 1.0f - sstep(w * 0.35f, w * 0.85f, best));
  }
  return cut;
}

static float temple_ground(float x, float z, void *ud) {
  (void)ud;
  float h = ground_hills(T, x, z, 7.0f, 110.0f, 16.0f, 60.0f, 11);
  {
    float pdx = x - PYR.c.x, pdz = z - PYR.c.z; /* the pyramid stands on level ground */
    h *= sstep(PYR.base + 2.0f, PYR.base + 20.0f, sqrtf(pdx * pdx + pdz * pdz));
  }
  h += ground_noise(x, z, 9.0f, 0.5f, 2, 5) * sstep(6, 25, road_gap(T, x, z, NULL));
  /* the gorge */
  {
    float dx = x - GORGE.c.x, dz = z - GORGE.c.z;
    float d = fabsf(dx * GORGE.f.x + dz * GORGE.f.z);       /* along the road from the bridge's middle */
    float l = fabsf(dx * GORGE.r.x + dz * GORGE.r.z);       /* across: the gorge runs this way */
    float lip = GORGE.half;
    float cut = 1.0f - sstep(lip - 26.0f, lip + 1.0f, d);   /* 1 in the gorge */
    cut *= 1.0f - sstep(150.0f, 330.0f, l);
    /* keep the other parts of the course out of it */
    float ry;
    int idx;
    float gap = road_gap_idx(T, x, z, &ry, &idx);
    float ds = fabsf(T->s[idx].s - C.gorge_s);
    if (ds > C.bridge.s1 - C.bridge.s0) cut *= sstep(10, 40, gap);
    /* the floor undulates a little and the walls are not perfectly straight */
    float wob = ground_noise(x, z, 14.0f, 2.2f, 2, 31);
    h = lerpf(h, -GORGE.depth + wob, cut);
  }
  /* the river bed: a gentle channel along the waterfall's river */
  {
    h -= 1.8f * river_cut(x, z);
  }
  /* the cliff the waterfall drops from: a plateau beyond cliff_lat, to the left of the road */
  {
    float dx = x - FALL.c.x, dz = z - FALL.c.z;
    float lat = dx * FALL.r.x + dz * FALL.r.z, al = dx * FALL.f.x + dz * FALL.f.z;
    float pl = sstep(FALL.cliff_lat - 18.0f, FALL.cliff_lat, -lat) * (1.0f - sstep(70.0f, 130.0f, fabsf(al)));
    /* and it ends behind the falls: it ran on without end and stood 34 m high over the start straight, the S-bend and
     * the first tunnel, a dark bank cut at 45 degrees beside the road and an earth slab through the tunnel */
    pl *= 1.0f - sstep(FALL.cliff_lat + 25.0f, FALL.cliff_lat + 60.0f, -lat);
    h = lerpf(h, FALL.h + ground_noise(x, z, 30.0f, 3.0f, 2, 8), pl);
  }
  /* a temple tunnel stands in a clearing: the land stays under its road out past its masonry, and rises beyond. The
   * ground mesh's 8 m triangles otherwise ran from the high land down to the road graded at the mouth, straight through
   * the tube: a dark slab of earth standing in the lane a few metres before the way out */
  {
    float ry;
    int idx;
    float gap = road_gap_idx(T, x, z, &ry, &idx);
    if (T->s[idx].flags & TF_TUNNEL) h = fminf(h, ry - 0.6f + fmaxf(gap - 12.0f, 0.0f) * 1.2f);
  }
  return h;
}

/* ── placing models made of the toolbox's builder, instanced ───────────────────────────────────────── */

typedef struct { prop_batch *b[PL_COUNT]; } inst;
static inst inst_new(prop_model m, int tex, pcol tint, float rough) {
  inst r = { 0 };
  for (int l = 0; l < PL_COUNT; l++)
    if (m.g[l]) r.b[l] = pb_begin(m.g[l], prop_material(l, l == PL_MAIN ? tex : -1, tint, rough));
  prop_model_free(&m);
  return r;
}
static void inst_add(inst *r, t3_vec3 p, float yaw, float scale) {
  for (int l = 0; l < PL_COUNT; l++)
    if (r->b[l]) pb_add(r->b[l], p, yaw, V(scale, scale, scale));
}
static void inst_add_c(inst *r, t3_vec3 p, t3_quat q, t3_vec3 scale, pcol tint) {
  for (int l = 0; l < PL_COUNT; l++)
    if (r->b[l]) pb_add_c(r->b[l], p, q, scale, tint);
}
static void inst_add3(inst *r, t3_vec3 p, float yaw, t3_vec3 scale) {
  for (int l = 0; l < PL_COUNT; l++)
    if (r->b[l]) pb_add(r->b[l], p, yaw, scale);
}
static void inst_finish(inst *r, cw_node parent, const char *name, bool shadow, float chunk) {
  for (int l = 0; l < PL_COUNT; l++) {
    if (!r->b[l]) continue;
    char n[48];
    snprintf(n, sizeof n, "%s%d", name, l);
    if (chunk > 0) pb_chunk(r->b[l], chunk);
    pb_finish(r->b[l], parent, n, shadow && l != PL_GLOW);
    r->b[l] = NULL;
  }
}

/* the frame of a road sample, as the dressing code uses it: x right, y up, z forward */
static pm *fr(pmc *c, const tsample *a, float lat, float lift) {
  t3_vec3 p = vadd(vadd(a->p, vmul(a->r, lat)), vmul(a->u, lift));
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_basis(b, p, a->r, a->u, a->f);
  return b;
}
/* a builder standing at a world point (y up, z the world's +z) */
static pm *at(pmc *c, t3_vec3 p) {
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_move(b, p.x, p.y, p.z);
  return b;
}
static float yaw_of(t3_vec3 d) { return atan2f(d.x, d.z); }

/* ── the props this map adds ───────────────────────────────────────────────────────────────────────── */

/* a guardian: a carved stone figure on a plinth with its arms raised, gem eyes and a gold collar. Origin at the plinth's
 * middle, facing +z. arms_up: the hands hold something overhead (the start banner's pole). */
static void guardian(pm *b, float sz, bool arms_up) {
  pcol d = STONE_D, l = STONE_L, moss = STONE_MOSS, gold = GOLD, red = RED;
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.09f);
  /* plinth, two steps */
  pm_box(b, 6.4f * sz, 0.9f * sz, 6.4f * sz, pc_mul(d, 0.7f), d);
  pm_push(b);
  pm_move(b, 0, 0.9f * sz, 0);
  pm_box(b, 5.0f * sz, 1.5f * sz, 5.0f * sz, d, l);
  /* moss creeping up the plinth */
  pm_box(b, 5.1f * sz, 0.5f * sz, 5.1f * sz, moss, pc_mul(moss, 1.2f));
  pm_move(b, 0, 1.5f * sz, 0);
  /* the robe: a tapering skirt, then the torso, shoulders and head */
  pm_cyl(b, 1.9f * sz, 1.25f * sz, 4.2f * sz, 8, d, l);
  pm_move(b, 0, 4.2f * sz, 0);
  pm_box(b, 3.2f * sz, 2.6f * sz, 1.9f * sz, l, pc_mul(l, 1.12f));
  /* gold collar and chest gem */
  pm_push(b);
  pm_move(b, 0, 2.2f * sz, 0);
  pm_box(b, 3.5f * sz, 0.45f * sz, 2.1f * sz, pc_mul(gold, 0.8f), gold);
  pm_pop(b);
  pm_move(b, 0, 2.6f * sz, 0);
  /* head */
  pm_box(b, 1.7f * sz, 1.9f * sz, 1.7f * sz, l, pc_mul(l, 1.1f));
  /* the headdress: a red and gold crown that fans out */
  pm_push(b);
  pm_move(b, 0, 1.9f * sz, 0);
  pm_box(b, 2.1f * sz, 0.5f * sz, 2.1f * sz, pc_mul(red, 0.7f), red);
  for (int i = -2; i <= 2; i++) {
    pm_push(b);
    pm_move(b, (float)i * 0.45f * sz, 0.5f * sz, 0);
    pm_rotz(b, (float)i * -0.16f);
    pm_box(b, 0.32f * sz, (1.6f - 0.25f * (float)abs(i)) * sz, 0.5f * sz, (i & 1) ? pc_mul(gold, 0.8f) : pc_mul(red, 0.8f), (i & 1) ? gold : red);
    pm_pop(b);
  }
  pm_pop(b);
  /* a heavy brow and nose block */
  pm_push(b);
  pm_move(b, 0, 0.95f * sz, 0.85f * sz);
  pm_box_c(b, 1.5f * sz, 0.25f * sz, 0.35f * sz, d);
  pm_move(b, 0, -0.45f * sz, 0.05f * sz);
  pm_box_c(b, 0.35f * sz, 0.7f * sz, 0.4f * sz, l);
  pm_pop(b);
  /* arms */
  float up = arms_up ? 1 : 0;
  for (int k = -1; k <= 1; k += 2) {
    t3_vec3 sh = V((float)k * 1.75f * sz, -0.35f * sz, 0), hand = arms_up ? V((float)k * 2.5f * sz, 1.9f * sz, 0.6f * sz) : V((float)k * 2.2f * sz, -2.0f * sz, 0.9f * sz);
    /* local origin here is the head's base: the shoulders are 0.2 m under it */
    pm_limb(b, sh, hand, 0.6f * sz, 0.5f * sz, 6, d, l);
    pm_push(b);
    pm_move(b, hand.x, hand.y, hand.z);
    pm_box_c(b, 0.9f * sz, 0.9f * sz, 0.9f * sz, pc_mul(l, 1.1f));
    pm_pop(b);
  }
  (void)up;
  pm_pop(b);
  /* the glowing parts */
  pm_layer(b, PL_GLOW);
  pcol eye = glow(rgb(0.25f, 1.0f, 0.75f), 6.0f), chest = glow(rgb(1.0f, 0.55f, 0.1f), 5.0f);
  pm_push(b);
  pm_move(b, 0, (1.5f + 4.2f + 2.6f + 1.0f - 0.1f + 0.95f) * sz, 0);
  pm_move(b, 0, 0, 0.88f * sz);
  pm_push(b);
  pm_move(b, -0.38f * sz, 0, 0);
  pm_box_c(b, 0.3f * sz, 0.18f * sz, 0.12f * sz, eye);
  pm_pop(b);
  pm_move(b, 0.38f * sz, 0, 0);
  pm_box_c(b, 0.3f * sz, 0.18f * sz, 0.12f * sz, eye);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, (0.9f + 1.5f + 4.2f + 1.3f) * sz, 1.0f * sz);
  pm_box_c(b, 0.7f * sz, 0.7f * sz, 0.22f * sz, chest);
  pm_pop(b);
  pm_layer(b, PL_MAIN);
}

/* ── cloth ──────────────────────────────────────────────────────────────────────────────────────────── */

/* a banner hung between two points: a strip of cloth that billows a little, two-sided, colour bands from top to hem.
 * a and c are the top corners (world), drop the length it hangs, billow the sideways swell in metres. */
static void cloth(pm *b, t3_vec3 a, t3_vec3 c, float drop, float billow, pcol top, pcol mid, pcol hem, float phase) {
  int nx = 10, ny = 6;
  t3_vec3 side = vsub(c, a);
  float len = sqrtf(vdot(side, side));
  t3_vec3 nrm = V(-side.z / len, 0, side.x / len);
  for (int j = 0; j < ny; j++) {
    float v0 = (float)j / (float)ny, v1 = (float)(j + 1) / (float)ny;
    pcol col = v0 < 0.5f ? pc_mix(top, mid, v0 * 2) : pc_mix(mid, hem, (v0 - 0.5f) * 2);
    for (int i = 0; i < nx; i++) {
      float u0 = (float)i / (float)nx, u1 = (float)(i + 1) / (float)nx;
      float w[4] = { sinf((u0 * 3.0f + v0 * 1.4f + phase) * 3.14159f) * billow * (0.3f + v0), sinf((u1 * 3.0f + v0 * 1.4f + phase) * 3.14159f) * billow * (0.3f + v0),
                     sinf((u1 * 3.0f + v1 * 1.4f + phase) * 3.14159f) * billow * (0.3f + v1), sinf((u0 * 3.0f + v1 * 1.4f + phase) * 3.14159f) * billow * (0.3f + v1) };
      t3_vec3 p0 = vadd(vadd(vadd(a, vmul(side, u0)), V(0, -drop * v0, 0)), vmul(nrm, w[0]));
      t3_vec3 p1 = vadd(vadd(vadd(a, vmul(side, u1)), V(0, -drop * v0, 0)), vmul(nrm, w[1]));
      t3_vec3 p2 = vadd(vadd(vadd(a, vmul(side, u1)), V(0, -drop * v1, 0)), vmul(nrm, w[2]));
      t3_vec3 p3 = vadd(vadd(vadd(a, vmul(side, u0)), V(0, -drop * v1, 0)), vmul(nrm, w[3]));
      pm_quad(b, p0, p1, p2, p3, col);
      pm_quad(b, p1, p0, p3, p2, col);
    }
  }
}

/* ── the start: two guardians hold the banner ─────────────────────────────────────────────────────── */

static void paint_quad(pm *b, float x0, float x1, float z0, float z1, float y, pcol c) {
  pm_quad(b, V(x0, y, z1), V(x1, y, z1), V(x1, y, z0), V(x0, y, z0), c);
}

static void build_start(cw_node parent, const track *t) {
  int idx = t->nchecks > 0 ? t->check_at[0] : 0;
  const tsample *a = &t->s[idx];
  float hw = a->hw;
  pmc *c = pmc_new(90, 71);
  pmc_texture_metres(c, 3.0f);
  float px = hw + 4.2f;
  /* the guardians face the oncoming karts and hold a pole across the road */
  for (int k = -1; k <= 1; k += 2) {
    pm *b = fr(c, a, (float)k * px, 0);
    pm_roty(b, 3.14159f);
    guardian(b, 1.0f, true);
    pm_pop(b);
  }
  pmc_finish(c, parent, "StartGuardians", true, TEX_WALL_STONE);

  /* the pole, the banner, the lamps: wood and cloth and glow */
  pmc *w = pmc_new(90, 72);
  pm *b = fr(w, a, 0, 0);
  float hy = 11.1f, hz = -0.6f, hx = px + 2.5f;
  pm_layer(b, PL_MAIN);
  pm_beam(b, V(-hx - 0.8f, hy, hz), V(hx + 0.8f, hy, hz), 0.5f, WOOD_D);
  for (int k = -1; k <= 1; k += 2) { /* gold caps on the pole's ends */
    pm_beam(b, V((float)k * (hx + 0.7f), hy, hz), V((float)k * (hx + 1.15f), hy, hz), 0.75f, GOLD);
  }
  float bx = px - 2.8f;
  cloth(b, V(-bx, hy - 0.3f, hz), V(bx, hy - 0.3f, hz), 4.6f, 0.25f, RED, pc_mul(RED, 1.15f), pc_mul(RED, 0.7f), 0.0f);
  /* a gold band with chequers across the middle, and a gold hem */
  int nc = 24;
  for (int i = 0; i < nc; i++) {
    float x0 = -bx + 2 * bx * (float)i / (float)nc, x1 = -bx + 2 * bx * (float)(i + 1) / (float)nc;
    for (int j = 0; j < 2; j++) {
      pcol cc = ((i + j) & 1) ? rgb(0.05f, 0.04f, 0.03f) : rgb(0.96f, 0.92f, 0.78f);
      float y0 = hy - 2.05f - 0.5f * (float)j, y1 = y0 - 0.5f;
      pm_quad(b, V(x0, y1, hz - 0.12f), V(x1, y1, hz - 0.12f), V(x1, y0, hz - 0.12f), V(x0, y0, hz - 0.12f), cc);
      pm_quad(b, V(x1, y1, hz + 0.12f), V(x0, y1, hz + 0.12f), V(x0, y0, hz + 0.12f), V(x1, y0, hz + 0.12f), cc);
    }
  }
  pm_quad(b, V(-bx, hy - 4.95f, hz - 0.1f), V(bx, hy - 4.95f, hz - 0.1f), V(bx, hy - 4.55f, hz - 0.1f), V(-bx, hy - 4.55f, hz - 0.1f), GOLD);
  pm_quad(b, V(bx, hy - 4.95f, hz + 0.1f), V(-bx, hy - 4.95f, hz + 0.1f), V(-bx, hy - 4.55f, hz + 0.1f), V(bx, hy - 4.55f, hz + 0.1f), GOLD);
  /* a gold sun above the chequers: a disc of wedges */
  for (int side = -1; side <= 1; side += 2) {
    float zz = hz + (float)side * 0.14f;
    for (int i = 0; i < 12; i++) {
      float a0 = 6.28318f * (float)i / 12, a1 = 6.28318f * (float)(i + 1) / 12;
      float r0 = 0.0f, r1 = (i & 1) ? 1.15f : 0.75f;
      t3_vec3 c0 = V(0, hy - 1.05f, zz);
      t3_vec3 p0 = V(sinf(a0) * r1 * 0.8f, hy - 1.05f + cosf(a0) * r1 * 0.8f, zz), p1 = V(sinf(a1) * r1 * 0.8f, hy - 1.05f + cosf(a1) * r1 * 0.8f, zz);
      (void)r0;
      if (side < 0) pm_tri(b, c0, p1, p0, GOLD);
      else pm_tri(b, c0, p0, p1, GOLD);
    }
  }
  pm_pop(b);
  pmc_finish(w, parent, "StartBanner", true, TEX_WALL_WOOD);

  /* the painted chequered line and the grid boxes */
  pmc *p = pmc_new(80, 73);
  b = fr(p, a, 0, 0);
  pm_layer(b, PL_MAIN);
  int nc2 = (int)(2 * hw / 0.5f);
  pcol black = rgb(0.04f, 0.04f, 0.05f), white = rgb(0.95f, 0.95f, 0.9f);
  for (int i = 0; i < nc2; i++)
    for (int j = 0; j < 2; j++) paint_quad(b, -hw + 0.5f * (float)i, -hw + 0.5f * (float)i + 0.5f, -0.5f + 0.5f * (float)j, 0.5f * (float)j, 0.035f, ((i + j) & 1) ? black : white);
  pm_pop(b);
  if (t->circuit) {
    float lat = fminf(hw * 0.42f, 3.4f);
    for (int k = 0; k < 8; k++) {
      int row = k / 2, col = k & 1;
      float back = 8.0f + (float)row * 8.0f + (col ? 4.0f : 0.0f);
      float ss = fmodf(a->s - back + t->length, t->length);
      tsample sl = track_at(t, ss);
      if (sl.flags & (TF_GAP | TF_LOOP)) continue;
      float x = (col ? 1.0f : -1.0f) * lat;
      b = fr(p, &sl, 0, 0);
      float ww = 1.25f, l = 2.3f, lw = 0.12f;
      pcol line = rgb(0.92f, 0.82f, 0.45f);
      paint_quad(b, x - ww, x + ww, -l, -l + lw, 0.03f, line);
      paint_quad(b, x - ww, x - ww + lw, -l, l * 0.35f, 0.03f, line);
      paint_quad(b, x + ww - lw, x + ww, -l, l * 0.35f, 0.03f, line);
      pm_pop(b);
    }
  }
  pmc_finish(p, parent, "StartLine", false, -1);
}

/* ── the models this map adds ────────────────────────────────────────────────────────────────────── */

static pcol leaf_green(rng *r, float lo) {
  float k = lo + rnd(r) * (1.0f - lo);
  return pc_mix(rgb(0.06f, 0.22f, 0.07f), rgb(0.36f, 0.62f, 0.14f), k);
}

/* hanging vines: a few strands of stem with leaves, hanging down from the origin */
static prop_model model_vines(uint32_t seed, float reach) {
  pm *b = pm_new(seed);
  rng r = { seed * 2654435761u + 1 };
  pm_layer(b, PL_LEAF);
  for (int k = 0; k < 6; k++) {
    float x = rnds(&r) * 1.8f, z = rnds(&r) * 0.6f, len = reach * (0.45f + 0.55f * rnd(&r)), sway = rnds(&r) * 0.5f;
    t3_vec3 prev = V(x, 0, z);
    for (float y = 0.9f; y <= len; y += 0.9f) {
      t3_vec3 cur = V(x + sway * sinf(y * 0.7f), -y, z + 0.2f * sinf(y * 1.3f + (float)k));
      pm_rod(b, prev, cur, 0.035f, 3, rgb(0.14f, 0.26f, 0.08f));
      pcol lc = leaf_green(&r, 0.2f);
      float w = 0.38f + 0.22f * rnd(&r), s = rnd(&r) < 0.5f ? 1.0f : -1.0f;
      pm_quad(b, cur, vadd(cur, V(s * w, -0.18f, 0.12f)), vadd(cur, V(s * w * 1.2f, -0.62f, 0.05f)), vadd(cur, V(s * 0.05f, -0.5f, 0)), lc);
      pm_quad(b, cur, vadd(cur, V(-s * w * 0.8f, -0.25f, -0.1f)), vadd(cur, V(-s * w, -0.7f, 0.05f)), vadd(cur, V(0, -0.55f, 0)), pc_mul(lc, 0.85f));
      prev = cur;
    }
  }
  return pm_finish(b);
}

/* a crown of canopy: lumpy leaf masses on a short trunk stub (the trunk is only there to be seen from above) */
static prop_model model_crown(uint32_t seed, float rad, float trunk) {
  pm *b = pm_new(seed);
  rng r = { seed * 40503u + 7 };
  /* a buttressed trunk reaching down into the ground, three limbs up to the leaf masses */
  pm_layer(b, PL_MAIN);
  pcol bark0 = rgb(0.24f, 0.17f, 0.1f), bark1 = rgb(0.5f, 0.38f, 0.24f);
  pm_push(b);
  pm_move(b, 0, -trunk, 0);
  pm_cyl(b, 1.7f, 0.95f, trunk - 3.0f, 7, bark0, bark1);
  pm_pop(b);
  for (int k = 0; k < 3; k++) {
    float a = (float)k * 2.094f + rnd(&r);
    pm_limb(b, V(0, -3.5f, 0), V(cosf(a) * rad * 0.55f, rad * 0.1f, sinf(a) * rad * 0.55f), 0.8f, 0.4f, 5, bark0, bark1);
  }
  pm_layer(b, PL_LEAF);
  pm_vary(b, 0.1f);
  for (int k = 0; k < 6; k++) {
    pm_push(b);
    float mx = rnds(&r), my = rnds(&r), mz = rnds(&r);
    pm_move(b, mx * rad * 0.55f, my * rad * 0.12f + rad * 0.12f, mz * rad * 0.55f);
    float s = rad * (0.45f + 0.5f * rnd(&r)), sy = s * (0.5f + 0.45f * rnd(&r)), sz = s * (0.8f + 0.3f * rnd(&r));
    pcol leaf = leaf_green(&r, 0.6f);
    pm_ball(b, s, sy, sz, 2, 0.35f, rgb(0.06f, 0.26f, 0.08f), leaf);
    pm_pop(b);
  }
  return pm_finish(b);
}

/* an iron brazier on a stone stem: the coals glow; the flame is a separate, flickering model */
static prop_model model_brazier(uint32_t seed) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.08f);
  pm_box(b, 1.5f, 0.5f, 1.5f, pc_mul(STONE_D, 0.7f), STONE_D);
  pm_push(b);
  pm_move(b, 0, 0.5f, 0);
  pm_cyl(b, 0.5f, 0.38f, 1.0f, 6, STONE_D, STONE_L);
  pm_move(b, 0, 1.0f, 0);
  pm_layer(b, PL_METAL);
  pm_cyl(b, 0.45f, 0.95f, 0.65f, 8, rgb(0.1f, 0.09f, 0.08f), rgb(0.3f, 0.26f, 0.2f));
  pm_cyl_ex(b, 0.98f, 0.9f, 0.14f, 8, GOLD, GOLD, false, false);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, 0.6f, 0);
  pm_cyl(b, 0.85f, 0.8f, 0.05f, 8, glow(rgb(1.0f, 0.4f, 0.08f), 3.0f), glow(rgb(1.0f, 0.55f, 0.12f), 3.0f));
  pm_pop(b);
  pm_pop(b);
  return pm_finish(b);
}

/* the flame: nested cones, hot at the heart. Unit size about one metre tall. */
static prop_model model_flame(uint32_t seed) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_GLOW);
  pm_cyl(b, 0.42f, 0.0f, 1.25f, 6, glow(rgb(1.0f, 0.3f, 0.04f), 2.4f), glow(rgb(1.0f, 0.5f, 0.1f), 3.2f));
  pm_push(b);
  pm_rotz(b, 0.22f);
  pm_cyl(b, 0.26f, 0.0f, 1.0f, 5, glow(rgb(1.0f, 0.55f, 0.1f), 3.0f), glow(rgb(1.0f, 0.85f, 0.35f), 4.0f));
  pm_pop(b);
  pm_cyl(b, 0.16f, 0.0f, 0.7f, 5, glow(rgb(1.0f, 0.85f, 0.4f), 4.5f), glow(rgb(1.0f, 1.0f, 0.8f), 5.0f));
  return pm_finish(b);
}

/* a clump of flowers: a few stems, each with a bright blossom (petals as small diamonds round a yellow heart) */
static prop_model model_flowers(uint32_t seed, uint32_t petal) {
  pm *b = pm_new(seed);
  rng r = { seed * 7919u + 3 };
  pm_layer(b, PL_MAIN);
  for (int k = 0; k < 5; k++) {
    float x = rnds(&r) * 0.5f, z = rnds(&r) * 0.5f, h = 0.5f + 0.5f * rnd(&r);
    t3_vec3 top = V(x + rnds(&r) * 0.1f, h, z);
    pm_rod(b, V(x, 0, z), top, 0.018f, 3, rgb(0.15f, 0.4f, 0.1f));
    pm_quad(b, V(x, h * 0.35f, z), V(x + 0.22f, h * 0.5f, z + 0.05f), V(x + 0.3f, h * 0.55f, z), V(x + 0.05f, h * 0.3f, z), rgb(0.2f, 0.5f, 0.12f));
    int np = 6;
    for (int i = 0; i < np; i++) {
      float a0 = 6.2832f * (float)i / (float)np, a1 = 6.2832f * ((float)i + 0.5f) / (float)np, a2 = 6.2832f * ((float)i + 1.0f) / (float)np, rr = 0.16f + 0.05f * rnd(&r);
      t3_vec3 c0 = top, p0 = V(top.x + cosf(a0) * rr * 0.4f, top.y + 0.03f, top.z + sinf(a0) * rr * 0.4f), p1 = V(top.x + cosf(a1) * rr, top.y + 0.0f, top.z + sinf(a1) * rr), p2 = V(top.x + cosf(a2) * rr * 0.4f, top.y + 0.03f, top.z + sinf(a2) * rr * 0.4f);
      pm_quad(b, c0, p0, p1, p2, hex(petal));
      pm_quad(b, c0, p2, p1, p0, hex(petal));
    }
    pm_push(b);
    pm_move(b, top.x, top.y + 0.02f, top.z);
    pm_ball(b, 0.05f, 0.05f, 0.05f, 1, 0.0f, rgb(1.0f, 0.8f, 0.1f), rgb(1.0f, 0.8f, 0.1f));
    pm_pop(b);
  }
  return pm_finish(b);
}

/* a tall pole with a red and gold banner hanging from a cross bar: the league's colours, no words */
static prop_model model_banner(uint32_t seed, float h) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_MAIN);
  pm_box(b, 0.5f, 0.5f, 0.5f, STONE_D, STONE_L);
  pm_beam(b, V(0, 0, 0), V(0, h, 0), 0.22f, WOOD_D);
  pm_beam(b, V(-1.25f, h - 0.5f, 0), V(1.25f, h - 0.5f, 0), 0.14f, WOOD_L);
  pm_box_c(b, 0.35f, 0.35f, 0.35f, GOLD);
  pm_push(b);
  pm_move(b, 0, h, 0);
  pm_ball(b, 0.22f, 0.22f, 0.22f, 1, 0.0f, GOLD, GOLD);
  pm_pop(b);
  float ty = h - 0.6f;
  cloth(b, V(-1.05f, ty, 0), V(1.05f, ty, 0), h * 0.62f, 0.1f, RED, pc_mul(RED, 1.2f), pc_mul(RED, 0.65f), 0.4f);
  /* gold border at the hem and a gold sun on the field */
  float yh = ty - h * 0.62f;
  pm_quad(b, V(-1.05f, yh, -0.03f), V(1.05f, yh, -0.03f), V(1.05f, yh + 0.3f, -0.03f), V(-1.05f, yh + 0.3f, -0.03f), GOLD);
  pm_quad(b, V(1.05f, yh, 0.03f), V(-1.05f, yh, 0.03f), V(-1.05f, yh + 0.3f, 0.03f), V(1.05f, yh + 0.3f, 0.03f), GOLD);
  float cy = ty - h * 0.28f;
  for (int side = -1; side <= 1; side += 2) {
    float zz = (float)side * 0.05f;
    for (int i = 0; i < 8; i++) {
      float a0 = 6.28318f * (float)i / 8, a1 = 6.28318f * (float)(i + 1) / 8, rr = (i & 1) ? 0.85f : 0.45f;
      t3_vec3 c0 = V(0, cy, zz), p0 = V(sinf(a0) * rr, cy + cosf(a0) * rr, zz), p1 = V(sinf(a1) * rr, cy + cosf(a1) * rr, zz);
      if (side < 0) pm_tri(b, c0, p1, p0, GOLD);
      else pm_tri(b, c0, p0, p1, GOLD);
    }
  }
  return pm_finish(b);
}

/* a wall sconce: an iron cup on an arm, the flame goes on top */
static prop_model model_sconce(uint32_t seed) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_METAL);
  pm_box_c(b, 0.3f, 0.5f, 0.1f, rgb(0.12f, 0.1f, 0.09f));
  pm_beam(b, V(0, 0, 0), V(0, -0.1f, 0.35f), 0.07f, rgb(0.15f, 0.12f, 0.1f));
  pm_push(b);
  pm_move(b, 0, -0.1f, 0.35f);
  pm_cyl(b, 0.1f, 0.2f, 0.28f, 6, rgb(0.12f, 0.1f, 0.09f), rgb(0.3f, 0.25f, 0.18f));
  pm_pop(b);
  return pm_finish(b);
}

static prop_model make_guardian(float sz, bool arms_up, uint32_t seed) {
  pm *b = pm_new(seed);
  guardian(b, sz, arms_up);
  return pm_finish(b);
}

/* ── the shared decoration: models instanced everywhere, finished together ─────────────────────────── */

static struct {
  inst vines, wallvines, crowns, crowns2, braziers, banners, guards, sconces, flowers[4];
  prop_batch *flame[4];
  t3_material *flame_mat[4];
  int nflame;
  int nbrazier;
} D;

static void dec_begin(void) {
  memset(&D, 0, sizeof D);
  D.vines = inst_new(model_vines(3, 9.0f), -1, (pcol){ 1, 1, 1 }, 1.0f);
  D.wallvines = inst_new(model_vines(8, 2.6f), -1, (pcol){ 1, 1, 1 }, 1.0f);
  D.crowns2 = inst_new(model_crown(17, 12.0f, 40.0f), -1, (pcol){ 1, 1, 1 }, 1.0f);
  D.crowns = inst_new(model_crown(5, 9.0f, 40.0f), -1, (pcol){ 1, 1, 1 }, 1.0f);
  D.braziers = inst_new(model_brazier(7), TEX_WALL_STONE, (pcol){ 1, 1, 1 }, 1.0f);
  D.banners = inst_new(model_banner(9, 7.0f), -1, (pcol){ 1, 1, 1 }, 1.0f);
  {
    static const uint32_t petals[4] = { 0xe02a2a, 0xf5c518, 0xd83fb0, 0xff8a1c };
    for (int i = 0; i < 4; i++) D.flowers[i] = inst_new(model_flowers(20 + (uint32_t)i, petals[i]), -1, (pcol){ 1, 1, 1 }, 1.0f);
  }
  D.sconces = inst_new(model_sconce(15), -1, (pcol){ 1, 1, 1 }, 1.0f);
  D.guards = inst_new(make_guardian(0.62f, false, 11), TEX_WALL_STONE, (pcol){ 1, 1, 1 }, 1.0f);
  prop_model fm = model_flame(13);
  for (int i = 0; i < 4; i++) {
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->vertex_colors = true;
    D.flame_mat[i] = m;
    D.flame[i] = pb_begin(fm.g[PL_GLOW], m);
    pb_chunk(D.flame[i], 90);
  }
  prop_model_free(&fm);
}
static void flame_at(t3_vec3 p, float scale) {
  int g = D.nflame++ & 3;
  pb_add(D.flame[g], p, (float)D.nflame * 1.7f, V(scale, scale * 1.15f, scale));
}
static void brazier_at(t3_vec3 p, float yaw, float scale) {
  inst_add(&D.braziers, p, yaw, scale);
  flame_at(vadd(p, V(0, 1.75f * scale, 0)), scale * 1.3f);
  D.nbrazier++;
}
/* a wall torch: the sconce model with its flame; p is where the arm meets the wall */
static void torch_at(t3_vec3 p, float scale) {
  inst_add(&D.sconces, p, 0, scale);
  flame_at(vadd(p, V(0, 0.45f * scale, 0)), scale * 0.6f);
}
static void dec_finish(cw_node scene) {
  inst_finish(&D.sconces, scene, "Sconces", false, 160);
  for (int i = 0; i < 4; i++) {
    char n[24];
    snprintf(n, sizeof n, "Flowers%d_", i);
    inst_finish(&D.flowers[i], scene, n, false, 200);
  }
  inst_finish(&D.vines, scene, "Vines", false, 200);
  inst_finish(&D.wallvines, scene, "WallVines", false, 160);
  inst_finish(&D.crowns, scene, "Crowns", true, 220);
  inst_finish(&D.crowns2, scene, "Crowns2", true, 220);
  inst_finish(&D.braziers, scene, "Braziers", true, 160);
  inst_finish(&D.banners, scene, "Banners", true, 160);
  inst_finish(&D.guards, scene, "Guards", true, 200);
  for (int i = 0; i < 4; i++) {
    char n[24];
    snprintf(n, sizeof n, "Flames%d", i);
    pb_finish(D.flame[i], scene, n, false);
    D.flame[i] = NULL;
  }
}

/* ── ground, trees, ferns: the dense jungle round the road ──────────────────────────────────────────── */

static float gorge_cut(float x, float z) {
  float dx = x - GORGE.c.x, dz = z - GORGE.c.z;
  float d = fabsf(dx * GORGE.f.x + dz * GORGE.f.z), l = fabsf(dx * GORGE.r.x + dz * GORGE.r.z);
  return (1.0f - sstep(GORGE.half - 26.0f, GORGE.half + 1.0f, d)) * (1.0f - sstep(150.0f, 330.0f, l));
}

static float mask_jungle(float x, float z, void *ud) {
  (void)ud;
  float dx = x - PYR.c.x, dz = z - PYR.c.z;
  if (sqrtf(dx * dx + dz * dz) < PYR.base + 4.0f) return 0.0f;
  float g = gorge_cut(x, z);
  return g > 0.4f ? 0.08f : 1.0f;
}
static float mask_open(float x, float z, void *ud) {
  (void)ud;
  float dx = x - PYR.c.x, dz = z - PYR.c.z;
  if (sqrtf(dx * dx + dz * dz) < PYR.base + 2.0f) return 0.0f;
  return gorge_cut(x, z) > 0.4f ? 0.0f : 1.0f;
}

/* A forest of few models, placed along the course: every (model, layer, 140 m cell) is one draw, so the jungle is
 * built from a handful of variants instead of the toolbox's scatter (which spreads every kind over all its variants). */
static float gy(float x, float z);

typedef struct {
  prop_kind kind;
  float radius;
  inst in[2]; /* [0] near the road, [1] the cheap model for far away */
} fmodel;
static fmodel FM[16];
static int nfm;

static int fm_make(prop_kind k, int variant, float size, int tex, pcol tint, uint32_t seed) {
  fmodel *f = &FM[nfm];
  f->kind = k;
  for (int lod = 0; lod < 2; lod++) {
    prop_model pmod = prop_make(k, size, seed, variant, lod);
    if (lod == 0) f->radius = pmod.radius;
    f->in[lod] = inst_new(pmod, tex, tint, 1.0f);
  }
  return nfm++;
}
/* every copy leans a little, is taller or squatter than its neighbours and has its own shade of green (a few turn yellow) */
static rng VAR = { 99 };
static void fm_add(int i, t3_vec3 p, float yaw, float scale, bool far_) {
  float lean = 0.09f * rnds(&VAR), lean_dir = rnd(&VAR) * 6.2832f;
  t3_quat qy = t3_quat_from_axis_angle(V(0, 1, 0), yaw), qt = t3_quat_from_axis_angle(V(cosf(lean_dir), 0, sinf(lean_dir)), lean);
  t3_quat q = { qt.w * qy.x + qt.x * qy.w + qt.y * qy.z - qt.z * qy.y, qt.w * qy.y - qt.x * qy.z + qt.y * qy.w + qt.z * qy.x, qt.w * qy.z + qt.x * qy.y - qt.y * qy.x + qt.z * qy.w,
                qt.w * qy.w - qt.x * qy.x - qt.y * qy.y - qt.z * qy.z };
  float w = scale * (0.85f + 0.4f * rnd(&VAR)), h = scale * (0.8f + 0.45f * rnd(&VAR));
  float g = 0.8f + 0.4f * rnd(&VAR);
  float tr = 0.85f + 0.3f * rnd(&VAR), tg = 0.92f + 0.14f * rnd(&VAR), tb = 0.75f + 0.4f * rnd(&VAR);
  pcol tint = { g * tr, g * tg, g * tb };
  if (rnd(&VAR) < 0.07f) tint = (pcol){ 1.5f, 1.15f, 0.45f };
  inst_add_c(&FM[i].in[far_ ? 1 : 0], p, q, V(w, h, w), tint);
}
static void fm_finish(cw_node scene) {
  for (int i = 0; i < nfm; i++)
    for (int lod = 0; lod < 2; lod++) {
      char n[40];
      snprintf(n, sizeof n, "Forest%d_%d_", i, lod);
      inst_finish(&FM[i].in[lod], scene, n, FM[i].kind == PK_JUNGLE_TREE || FM[i].kind == PK_BROADLEAF, 230);
    }
  nfm = 0;
}

typedef struct {
  int models[4];
  int n;
  float spacing, lat_min, lat_max, curve, smin, smax, far_dist, far_keep;
  bool both_sides_only_ground;
} planting;

/* plant along the whole course, both sides; nothing on, over or under any road */
static int plant_along(const track *t, const planting *p, rng *r, float (*mask)(float, float, void *)) {
  int count = 0;
  for (float s = 0; s < t->length; s += p->spacing * (0.5f + rnd(r))) {
    tsample a = track_at(t, s);
    for (int side = -1; side <= 1; side += 2) {
      float lat = a.hw + 1.0f + p->lat_min + (p->lat_max - p->lat_min) * powf(rnd(r), p->curve);
      t3_vec3 q = vadd(a.p, vmul(a.r, (float)side * lat));
      float ry;
      int mi = p->models[(int)(rnd(r) * (float)p->n) % p->n];
      float sc = p->smin + (p->smax - p->smin) * rnd(r);
      float gap = road_gap(t, q.x, q.z, &ry);
      if (gap < 1.5f + FM[mi].radius * sc * 0.45f) continue;
      if (s > t->length - 70.0f && lat < a.hw + 12.0f && FM[mi].kind != PK_FERN) continue; /* keep the grid's rear clear for the cameras */
      float w = mask ? mask(q.x, q.z, NULL) : 1.0f;
      if (w < rnd(r)) continue;
      bool far_ = gap > p->far_dist;
      if (far_ && rnd(r) > p->far_keep) continue;
      q.y = gy(q.x, q.z) - 0.15f;
      fm_add(mi, q, rnd(r) * 6.2832f, sc, far_);
      count++;
    }
  }
  return count;
}

static void build_jungle(cw_node scene, const track *t) {
  rng r = { 4242 };
  nfm = 0;
  /* big trees: jungle giants and round broadleaf */
  planting trees = { .n = 0, .spacing = thin(9.5f, 17.0f), .lat_min = 2.5f, .lat_max = 85, .curve = 1.5f, .smin = 1.0f, .smax = 1.9f, .far_dist = 38, .far_keep = thin(0.5f, 0.3f) };
  trees.models[trees.n++] = fm_make(PK_JUNGLE_TREE, 0, 0, -1, rgb(0.7f, 1.0f, 0.55f), 11);
  trees.models[trees.n++] = fm_make(PK_JUNGLE_TREE, 1, 0, -1, rgb(0.62f, 1.0f, 0.5f), 12);
  trees.models[trees.n++] = fm_make(PK_JUNGLE_TREE, 2, 0, -1, rgb(0.66f, 0.98f, 0.5f), 14);
  trees.models[trees.n++] = fm_make(PK_BROADLEAF, 0, 0, -1, rgb(0.55f, 0.95f, 0.45f), 13);
  int a = plant_along(t, &trees, &r, mask_jungle);
  /* the canopy sea round the pyramid: giants whose crowns reach the helix's upper turns */
  int canopy = 0;
  for (int i = 0; i < (int)thin(260.0f, 110.0f); i++) {
    float ang = rnd(&r) * 6.2832f, dist = PYR.base + 8.0f + 190.0f * sqrtf(rnd(&r));
    t3_vec3 q = V(PYR.c.x + cosf(ang) * dist, 0, PYR.c.z + sinf(ang) * dist);
    float ry, gap = road_gap(t, q.x, q.z, &ry);
    if (gap < 9.0f || mask_open(q.x, q.z, NULL) < 0.5f) continue;
    q.y = gy(q.x, q.z) - 0.2f;
    int mi = (int)(rnd(&r) * 3.0f) % 3;
    float yaw = rnd(&r) * 6.28f, sc = 1.5f + 1.0f * rnd(&r);
    fm_add(trees.models[mi], q, yaw, sc, dist > 110);
    canopy++;
  }
  /* the gorge: giants growing from its floor, their crowns well under the bridge, half hidden in the mist */
  int gorge_trees = 0;
  for (int i = 0; i < (int)thin(34.0f, 16.0f); i++) {
    float d = (rnd(&r) * 2 - 1) * (GORGE.half - 6.0f), l = (rnd(&r) * 2 - 1) * 120.0f;
    t3_vec3 q = V(GORGE.c.x + GORGE.f.x * d + GORGE.r.x * l, 0, GORGE.c.z + GORGE.f.z * d + GORGE.r.z * l);
    float ry, gap = road_gap(t, q.x, q.z, &ry);
    if (gap < 12.0f) continue;
    q.y = gy(q.x, q.z) - 0.3f;
    float sc = (GORGE.depth - 15.0f + rnds(&r) * 8.0f) / 18.0f; /* the crowns stop 10 m or so under the deck */
    int mi = (int)(rnd(&r) * 3.0f) % 3;
    float yaw = rnd(&r) * 6.28f;
    fm_add(trees.models[mi], q, yaw, sc, false);
    gorge_trees++;
  }
  /* ferns, grass tufts and bushes at the verge */
  planting fern = { .n = 0, .spacing = thin(3.6f, 6.5f), .lat_min = 0.4f, .lat_max = 20, .curve = 1.9f, .smin = 1.0f, .smax = 2.4f, .far_dist = 24, .far_keep = 0.4f };
  fern.models[fern.n++] = fm_make(PK_FERN, 0, 0, -1, rgb(0.6f, 1.0f, 0.5f), 21);
  fern.models[fern.n++] = fm_make(PK_FERN, 1, 0, -1, rgb(0.5f, 0.95f, 0.45f), 22);
  fern.models[fern.n++] = fm_make(PK_BUSH, 0, 0, -1, rgb(0.55f, 0.95f, 0.4f), 23);
  int b = plant_along(t, &fern, &r, mask_open);
  planting rock = { .n = 0, .spacing = 24, .lat_min = 0.6f, .lat_max = 32, .curve = 1.3f, .smin = 0.8f, .smax = 2.2f, .far_dist = 30, .far_keep = 0.5f };
  rock.models[rock.n++] = fm_make(PK_ROCK, 0, 0, TEX_GROUND_ROCK, rgb(0.34f, 0.46f, 0.3f), 31);
  rock.models[rock.n++] = fm_make(PK_ROCK, 2, 0, TEX_GROUND_ROCK, rgb(0.38f, 0.46f, 0.32f), 32);
  int c = plant_along(t, &rock, &r, mask_open);
  cw_info("jungle: %d trees, %d canopy giants, %d gorge giants, %d ferns, %d rocks", a, canopy, gorge_trees, b, c);
  fm_finish(scene);
}

/* ── masonry under the road (arches), and the masses of the tunnels ──────────────────────────────────── */

static float gy(float x, float z) { return ground_height_at(&G, x, z); }

static bool crossing_below(const track *t, int i, float x, float z) {
  const tsample *a = &t->s[i];
  for (int j = 0; j < t->n; j += 2) {
    const tsample *o = &t->s[j];
    float ds = fabsf(o->s - a->s);
    if (t->circuit) ds = fminf(ds, t->length - ds);
    if (ds < 30 || o->p.y > a->p.y - 0.5f) continue;
    float dx = o->p.x - x, dz = o->p.z - z, lim = o->hw + 4.0f;
    if (dx * dx + dz * dz < lim * lim) return true;
  }
  return false;
}

/* how far (on the ground plane) a pier under sample a stands from the nearest road edge of another, lower stretch */
static float lower_road_dist(const track *t, const tsample *a) {
  float best = 1e9f;
  for (int j = 0; j < t->n; j++) {
    const tsample *o = &t->s[j];
    float ds = fabsf(o->s - a->s);
    if (t->circuit) ds = fminf(ds, t->length - ds);
    if (ds < 30 || o->p.y > a->p.y - 3.0f) continue;
    float dx = o->p.x - a->p.x, dz = o->p.z - a->p.z, d = sqrtf(dx * dx + dz * dz);
    if (d > 0.6f * ds) continue; /* the same stretch further down its own slope, not another road passing by */
    best = fminf(best, d);
  }
  return best;
}

/* arches of mossy masonry under every raised stretch of road (not the bridge, loop or jump gap) */
static void build_viaduct(cw_node parent, const track *t) {
  pmc *c = pmc_new(70, 51);
  pmc_texture_metres(c, 4.0f);
  pcol stone0 = pc_mix(STONE_MOSS, STONE_D, 0.5f), stone1 = rgb(0.66f, 0.62f, 0.48f);
  const float span = 22.0f;
  int step = (int)(span / STEP_M), count = 0;
  for (int i = 0; i + step < t->n; i += step) {
    int j = i + step;
    bool ok = true;
    for (int k = i; k <= j && ok; k++) {
      const tsample *q = &t->s[k];
      ok = !(q->flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) && !(q->flags & TF_OPEN && q->s > C.bridge.s0 - 2 && q->s < C.bridge.s1 + 2);
    }
    if (!ok) continue;
    const tsample *a = &t->s[i], *e = &t->s[j];
    float g0 = gy(a->p.x, a->p.z), g1 = gy(e->p.x, e->p.z);
    if (a->p.y - g0 < 3.0f || e->p.y - g1 < 3.0f) continue;
    if (crossing_below(t, i, a->p.x, a->p.z)) continue;
    /* a span right beside a lower road (the helix's last turn passes over the approach to it) was a solid carved wall
     * 20 m high filling a third of that view: there it is an open span, a slim deck beam on slim columns */
    bool open_span = fminf(lower_road_dist(t, a), lower_road_dist(t, e)) < a->hw + 30.0f;
    int strips = 10;
    for (int si = 0; si < strips && open_span; si++) {
      float f0 = (float)si / (float)strips, fm = f0 + 0.5f / (float)strips;
      tsample m = track_at(t, a->s + (e->s - a->s) * fm);
      pm *w = at(c, V(m.p.x, m.p.y - 0.7f - 1.1f, m.p.z));
      pm_roty(w, yaw_of(V(m.f.x, 0, m.f.z)));
      pm_box(w, 2 * m.hw * 0.5f, 1.1f, (e->s - a->s) / (float)strips * 1.04f, pc_mul(stone0, 0.9f), stone1);
      pm_pop(w);
    }
    for (int si = 0; si < strips && !open_span; si++) {
      float f0 = (float)si / (float)strips, f1 = (float)(si + 1) / (float)strips, fm = (f0 + f1) * 0.5f;
      tsample m = track_at(t, a->s + (e->s - a->s) * fm);
      float g = gy(m.p.x, m.p.z), topy = m.p.y - 0.7f;
      float Rr = (e->s - a->s) * 0.5f * 0.82f, dx = (fm - 0.5f) * (e->s - a->s);
      float archy = fabsf(dx) < Rr ? g + sqrtf(Rr * Rr - dx * dx) * 0.9f + 0.3f : topy;
      float arch_top = fminf(archy, topy);
      pm *w = at(c, V(m.p.x, g, m.p.z));
      pm_roty(w, yaw_of(V(m.f.x, 0, m.f.z)));
      pm_vary(w, 0.1f);
      float wd = 2 * m.hw * 0.7f, seg = (e->s - a->s) / (float)strips * 1.04f;
      if (topy - arch_top > 0.05f) {
        pm_push(w);
        pm_move(w, 0, arch_top - g, 0);
        pm_box(w, wd, topy - arch_top, seg, pc_mul(stone0, 0.9f), stone1);
        pm_pop(w);
      }
      if (fabsf(dx) < Rr) {
        pm_push(w);
        pm_move(w, 0, archy - g - 0.8f, 0);
        pm_box(w, wd * 1.05f, 0.9f, seg * 1.02f, pc_mul(STONE_D, 0.8f), pc_mul(stone1, 0.9f));
        pm_pop(w);
      }
      pm_pop(w);
    }
    count++;
    for (int e2 = 0; e2 < 2; e2++) { /* the pier at each end */
      const tsample *pa = e2 ? e : a;
      float g = e2 ? g1 : g0;
      /* a pier standing beside another, lower stretch of road (the helix's approach runs under the high road) filled
       * a third of the view there: near a road below it is a slim column, and right beside one it is left out */
      float room_m = lower_road_dist(t, pa);
      if (room_m < pa->hw + 8.0f) continue;
      bool slim = room_m < pa->hw + 30.0f;
      pm *w = at(c, V(pa->p.x, g, pa->p.z));
      pm_roty(w, yaw_of(V(pa->f.x, 0, pa->f.z)));
      if (slim) {
        pm_cyl(w, 1.5f, 1.25f, pa->p.y - 0.7f - g, 10, stone0, stone1);
        pm_push(w);
        pm_move(w, 0, pa->p.y - 0.7f - g - 1.4f, 0);
        pm_cyl(w, 1.7f, 1.7f, 0.35f, 10, pc_mul(GOLD, 0.7f), GOLD);
        pm_pop(w);
        /* a corbel under the deck */
        pm_push(w);
        pm_move(w, 0, pa->p.y - 0.7f - g - 1.05f, 0);
        pm_box(w, 2 * pa->hw * 0.5f, 1.05f, 2.2f, stone0, stone1);
        pm_pop(w);
        pm_pop(w);
        continue;
      }
      pm_box(w, 2 * pa->hw * 0.76f, pa->p.y - 0.7f - g, 3.6f, stone0, stone1);
      /* a gold collar and a cap where the arch springs */
      pm_push(w);
      pm_move(w, 0, pa->p.y - 0.7f - g - 1.4f, 0);
      pm_box(w, 2 * pa->hw * 0.8f, 0.35f, 3.9f, pc_mul(GOLD, 0.7f), GOLD);
      pm_pop(w);
      pm_pop(w);
    }
  }
  cw_info("temple viaduct: %d arches", count);
  pmc_finish(c, parent, "Viaduct", true, TEX_WALL_STONE);
}


/* ── the temple tunnels: a carved stone face at each mouth and a stepped mass of masonry over the tube ─── */

static void arch_pt(float a, float rx, float ry, float cy, float *x, float *y) {
  *x = -rx * cosf(a);
  *y = cy + ry * sinf(a);
}

typedef struct { t3_vec3 p; bool side; } rhit;
static rhit rect_hit(float ang, float cy, float X, float Y) {
  float dx = cosf(ang), dy = sinf(ang);
  float tx = fabsf(dx) > 1e-4f ? X / fabsf(dx) : 1e9f, ty = dy > 1e-4f ? (Y - cy) / dy : 1e9f;
  rhit h;
  h.side = tx <= ty;
  float tt = h.side ? tx : ty;
  h.p = V(dx * tt, cy + dy * tt, 0);
  return h;
}
/* a quad that shows from both sides */
static void quad2(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, pcol c) {
  pm_quad(b, p0, p1, p2, p3, c);
  pm_quad(b, p0, p3, p2, p1, c);
}
static void tri2(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, pcol c) {
  pm_tri(b, p0, p1, p2, c);
  pm_tri(b, p0, p2, p1, c);
}

/* the face of a tunnel mouth, built facing out of the tube (exit_ turns it round for the far end) */
static void portal(pmc *c, const tsample *a, bool exit_) {
  float hw = a->hw;
  float X = hw + 4.6f, Y = 12.6f, cy = 1.6f, rx = hw + 0.05f, ry = 6.4f + 0.05f, ring = 1.15f;
  t3_vec3 r = exit_ ? vmul(a->r, -1) : a->r, f = exit_ ? vmul(a->f, -1) : a->f;
  pm *b = pmc_at(c, a->p);
  pm_push(b);
  pm_basis(b, a->p, r, a->u, f);
  pm_vary(b, 0.06f);
  pcol face = pc_mul(STONE_L, 0.8f);
  const int N = 18;
  for (int k = 0; k < N; k++) {
    float a0 = 3.14159f * (float)k / (float)N, a1 = 3.14159f * (float)(k + 1) / (float)N;
    float ex0, ey0, ex1, ey1, ox0, oy0, ox1, oy1;
    arch_pt(a0, rx, ry, cy, &ex0, &ey0);
    arch_pt(a1, rx, ry, cy, &ex1, &ey1);
    arch_pt(a0, rx + ring, ry + ring, cy, &ox0, &oy0);
    arch_pt(a1, rx + ring, ry + ring, cy, &ox1, &oy1);
    pcol vc = (k & 1) ? STONE_L : pc_mul(STONE_L, 0.7f);
    if (k == N / 2 - 1 || k == N / 2) vc = GOLD;
    float z0 = -1.2f;
    quad2(b, V(ex0, ey0, z0), V(ex1, ey1, z0), V(ox1, oy1, z0), V(ox0, oy0, z0), vc);
    quad2(b, V(ex1, ey1, z0), V(ex0, ey0, z0), V(ex0, ey0, 0), V(ex1, ey1, 0), pc_mul(vc, 0.7f));
    quad2(b, V(ox0, oy0, z0), V(ox1, oy1, z0), V(ox1, oy1, 0), V(ox0, oy0, 0), pc_mul(vc, 0.8f));
    /* the plain face from the ring out to the rectangle's edge */
    rhit h0 = rect_hit(3.14159f - a0, cy, X, Y), h1 = rect_hit(3.14159f - a1, cy, X, Y);
    t3_vec3 e0 = V(ox0, oy0, 0), e1 = V(ox1, oy1, 0), o0 = h0.p, o1 = h1.p;
    pcol fc = (k & 1) ? face : pc_mul(face, 0.92f);
    tri2(b, e0, e1, o1, fc);
    if (h0.side != h1.side) {
      float sx = fabsf(o0.x) > 1e-3f && h0.side ? o0.x : o1.x;
      t3_vec3 cn = V(sx < 0 ? -X : X, Y, 0);
      tri2(b, e0, o1, cn, fc);
      tri2(b, e0, cn, o0, fc);
    } else {
      tri2(b, e0, o1, o0, fc);
    }
  }
  /* beside the opening below the springing line, and the piers */
  for (int k = -1; k <= 1; k += 2) {
    pm_push(b);
    pm_move(b, (float)k * (X + rx) * 0.5f, -1.4f, -0.6f);
    pm_box(b, X - rx, cy + 1.4f, 1.2f, pc_mul(STONE_D, 0.8f), face);
    pm_pop(b);
    pm_push(b);
    pm_move(b, (float)k * (X - 1.0f), -1.4f, -1.8f);
    pm_box(b, 2.6f, Y + 0.7f, 2.2f, pc_mul(STONE_D, 0.8f), STONE_L);
    pm_move(b, 0, Y * 0.55f, 0);
    pm_box(b, 2.9f, 0.5f, 2.5f, pc_mul(GOLD, 0.7f), GOLD);
    pm_pop(b);
  }
  /* the cornice: stone, a red band, a gold band */
  pm_push(b);
  pm_move(b, 0, Y, -1.4f);
  pm_box(b, 2 * X + 2.2f, 1.2f, 3.4f, pc_mul(STONE_D, 0.9f), STONE_L);
  pm_move(b, 0, 1.2f, 0.2f);
  pm_box(b, 2 * X - 0.6f, 0.9f, 2.6f, pc_mul(RED, 0.8f), RED);
  pm_move(b, 0, 0.9f, 0);
  pm_box(b, 2 * X - 2.2f, 0.7f, 2.0f, pc_mul(GOLD, 0.8f), GOLD);
  pm_pop(b);
  /* the gem in the keystone */
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, cy + ry + 0.55f, -1.5f);
  pm_ball(b, 0.5f, 0.7f, 0.4f, 1, 0.0f, glow(rgb(0.2f, 1.0f, 0.7f), 6.0f), glow(rgb(0.6f, 1.0f, 0.9f), 8.0f));
  pm_pop(b);
  pm_layer(b, PL_MAIN);
  pm_pop(b);
}

/* masonry over (and, when the road is high, under) a tunnel: side blocks beside the tube and stepped slabs over it */
static void tunnel_mass(pmc *c, const track *t, float s0, float s1, bool to_ground, prop_set *tops) {
  float seg = 6.0f;
  for (float s = s0; s < s1 - 0.01f; s += seg) {
    float sm = fminf(s + seg * 0.5f, s1);
    tsample a = track_at(t, sm), lo = track_at(t, s), hi = track_at(t, fminf(s + seg, s1));
    float hw = a.hw, Wout = hw + 4.8f;
    float roadtop = fmaxf(lo.p.y, hi.p.y);
    float roof = roadtop + 8.5f;
    /* the high tunnel's mass stood to the ground in tiers stepping 2.6 m out each (over 50 m wide at its foot) right
     * beside the road that runs under it to the helix: a third of the view there. Near a road below, the tiers stand
     * straight, and close to one the mass stops under the road on a pair of carved columns instead */
    float room_m = to_ground ? lower_road_dist(t, &a) : 1e9f;
    float step_out = 2.6f * clampf((room_m - 25.0f) / 45.0f, 0.0f, 1.0f);
    bool columns = room_m < 45.0f;
    float ground = to_ground ? gy(a.p.x, a.p.z) - 3.0f : a.p.y - 1.4f;
    float base = columns ? a.p.y - 9.0f : ground;
    float yaw = yaw_of(V(a.f.x, 0, a.f.z));
    pm *b = at(c, V(a.p.x, base, a.p.z));
    pm_roty(b, yaw);
    pm_vary(b, 0.08f);
    pcol lo_c = (int)(s / seg) & 1 ? pc_mul(STONE_D, 0.85f) : STONE_D;
    if (columns && ((int)(s / seg) % 3) == 0) {
      for (int k = -1; k <= 1; k += 2) { /* round columns down to the ground under the block, with gold collars */
        pm_push(b);
        pm_move(b, (float)k * (hw + 1.0f), ground - base, 0);
        pm_cyl(b, 1.5f, 1.3f, base - ground + 0.2f, 10, pc_mul(STONE_MOSS, 0.7f), STONE_D);
        pm_move(b, 0, base - ground - 1.3f, 0);
        pm_cyl(b, 1.75f, 1.75f, 0.4f, 10, pc_mul(GOLD, 0.7f), GOLD);
        pm_pop(b);
      }
    }
    float H = roof - base;
    int K = to_ground ? (int)clampf(H / 9.0f, 1.0f, 7.0f) : 1;
    for (int tier = 0; tier < K; tier++) {
      float y0 = H * (float)tier / (float)K, y1 = H * (float)(tier + 1) / (float)K, W = Wout + (float)(K - 1 - tier) * step_out;
      float kk = (float)tier / (float)K;
      pcol c0 = pc_mix(pc_mul(STONE_MOSS, 0.7f), lo_c, clampf(kk * 2.0f, 0, 1)), c1 = pc_mix(STONE_L, STONE_MOSS, 0.3f * (1.0f - kk));
      for (int k = -1; k <= 1; k += 2) {
        pm_push(b);
        pm_move(b, (float)k * (hw + 0.2f + (W - hw - 0.2f) * 0.5f), y0, 0);
        pm_box(b, W - hw - 0.2f, y1 - y0 + 0.02f, seg * 1.04f, c0, c1);
        if (to_ground && tier < K - 1) { /* a painted band under each ledge */
          pm_move(b, 0, y1 - y0 - 0.6f, 0);
          pm_box(b, W - hw - 0.2f + 0.5f, 0.6f, seg * 1.06f, (tier & 1) ? pc_mul(RED, 0.8f) : pc_mul(GOLD, 0.75f), (tier & 1) ? RED : GOLD);
        }
        pm_pop(b);
      }
      /* plants on the ledges, vines hanging from them */
      if (to_ground && tops && tier < K - 1 && ((int)(s / seg) % 2) == 0) {
        for (int k = -1; k <= 1; k += 2) {
          float lat = (float)k * (W - 1.2f);
          t3_vec3 p = V(a.p.x + a.r.x * lat, base + y1, a.p.z + a.r.z * lat);
          ps_add(tops, (int)(s / seg + (float)tier) % 2 ? PK_BUSH : PK_FERN, p, rnd(&R) * 6.28f, 2.2f, (uint32_t)(s * 5 + (float)tier * 3 + (float)k));
          inst_add(&D.vines, V(a.p.x + a.r.x * (float)k * (W + 0.1f), base + y1 - 0.4f, a.p.z + a.r.z * (float)k * (W + 0.1f)), yaw, 0.55f + rnd(&R) * 0.35f);
        }
      }
    }
    if (to_ground) { /* the plinth under the road */
      pm_push(b);
      pm_box(b, 2 * hw + 0.6f, a.p.y - 0.9f - base, seg * 1.04f, pc_mul(STONE_MOSS, 0.7f), lo_c);
      pm_pop(b);
    }
    pm_push(b);
    pm_move(b, 0, roof - base, 0);
    pm_box(b, 2 * Wout, 2.2f, seg * 1.04f, pc_mul(STONE_L, 0.8f), STONE_L);
    pm_move(b, 0, 2.2f, 0);
    pm_box(b, 2 * (Wout - 2.0f), 1.8f, seg * 1.04f, pc_mul(STONE_MOSS, 0.9f), pc_mul(STONE_MOSS, 1.3f));
    pm_pop(b);
    pm_pop(b);
    /* foliage on the roof */
    if (tops) {
      float ty = roof + 4.0f;
      for (int k = 0; k < 2; k++) {
        float lat = ((float)k - 0.5f) * (Wout - 2.5f);
        t3_vec3 p = V(a.p.x + a.r.x * lat, ty, a.p.z + a.r.z * lat);
        if ((int)(s / seg + (float)k) % 3 == 0) {
          float yaw = rnd(&R) * 6.28f, sc = 0.9f + rnd(&R) * 0.5f;
          ps_add(tops, PK_BROADLEAF, p, yaw, sc, (uint32_t)(s * 7 + (float)k));
        }
        else ps_add(tops, PK_FERN, V(p.x, ty - 0.2f, p.z), rnd(&R) * 6.28f, 1.8f, (uint32_t)(s * 3 + (float)k));
      }
    }
  }
}

/* The track's tunnel tubes are drawn with the shared lining (ribbed concrete and a strip of lamps along the crown: a road
 * tunnel). Inside a temple they are carved stone instead, lit by the torches on their walls: the tube meshes (the world's
 * T4_ chunks, built before the scenery) get a mossy stone material of their own here. */
static void line_tunnels(cw_node scene) {
  cw_node w = cw_node_get(scene, "World");
  int n = cw_node_child_count(w);
  t3_material *m = NULL;
  for (int i = 0; i < n; i++) {
    cw_node c = cw_node_child(w, i);
    const char *nm = cw_node_name(c);
    if (!nm || strncmp(nm, "T4_", 3) != 0) continue;
    t3_object *wrap = cw_node3d_object(c);
    if (!wrap || wrap->child_count < 1) continue;
    t3_mesh *me = (t3_mesh *)wrap->children[0];
    if (me->material_count < 1) continue;
    if (!m) {
      m = t3_mesh_physical_material_new(0xffffff);
      m->color = (t3_color){ 0.3f, 0.32f, 0.23f };
      m->roughness = 1.0f;
      tex_apply(m, TEX_WALL_STONE, 2.0f, 2.0f, 1.0f, 1.0f);
      /* a warm torchlit fill: the sun does not reach in here */
      m->emissive = (t3_color){ 0.42f, 0.27f, 0.12f };
      m->emissive_intensity = 0.12f;
      m->side = T3_DOUBLE_SIDE;
    }
    t3_retain(m);
    t3_release(me->materials[0]);
    me->materials[0] = m;
  }
  if (m) t3_release(m);
}

static void build_tunnels(cw_node parent, const track *t) {
  pmc *c = pmc_new(60, 61);
  pmc_texture_metres(c, 4.0f);
  prop_set *tops = ps_new(parent, "MoundTops");
  ps_style(tops, PK_BROADLEAF, -1, rgb(0.55f, 0.95f, 0.4f), 1.0f);
  for (int i = 0; i < C.ntunnel; i++) {
    float s0 = C.tunnel[i].s0, s1 = C.tunnel[i].s1;
    tsample a = track_at(t, s0 + 0.5f), e_ = track_at(t, s1 - 0.5f);
    tsample e = e_;
    bool high = a.p.y - gy(a.p.x, a.p.z) > 6.0f;
    portal(c, &a, false);
    portal(c, &e, true);
    tunnel_mass(c, t, s0 + 1.2f, s1 - 1.2f, high, tops);
    /* vines hanging over each mouth, above the camera's reach */
    for (int ee = 0; ee < 2; ee++) {
      tsample q = ee ? e_ : a;
      float dirn = ee ? 1.0f : -1.0f;
      for (int k = 0; k < 6; k++) {
        float lat = -8.0f + 3.2f * (float)k + rnds(&R) * 0.6f;
        t3_vec3 p = vadd(vadd(q.p, vmul(q.r, lat)), vadd(vmul(q.u, 13.6f), vmul(q.f, dirn * (ee ? 0.8f : 1.4f))));
        float yaw = rnd(&R) * 6.28f, sc = 0.52f + rnd(&R) * 0.2f;
        inst_add(&D.vines, p, yaw, sc);
      }
    }
    /* torches along the tube's walls and statues beside each mouth */
    for (float s = s0 + 6; s < s1 - 3; s += 10) {
      tsample q = track_at(t, s);
      for (int k = -1; k <= 1; k += 2) {
        t3_vec3 p = vadd(vadd(q.p, vmul(q.r, (float)k * (q.hw - 0.3f))), vmul(q.u, 3.0f));
        torch_at(p, 0.8f);
      }
    }
  }
  pmc_finish(c, parent, "TunnelStone", true, TEX_WALL_STONE);
  ps_finish(tops);
}

/* ── set piece: the rope bridge over the misty gorge ──────────────────────────────────────────────── */

static struct { cw_node node; t3_vec3 axis; } BR; /* the bridge's ropes and posts, which sway a little */

static t3_vec3 tp(const track *t, float s, float lat, float h) { return track_point(t, s, lat, h); }

static void build_bridge(cw_node parent, const track *t) {
  float s0 = C.bridge.s0, s1 = C.bridge.s1, hw = track_at(t, s0 + 1).hw;
  /* wood and rope */
  pmc *w = pmc_new(60, 62);
  pmc_texture_metres(w, 2.0f);
  pcol wood = WOOD_D, wood2 = WOOD_L, rope = ROPE, rope_d = pc_mul(ROPE, 0.6f);
  float Ls = (s1 - s0) + 8.0f, ctop = 12.8f, cmid = 4.6f;
  for (int k = -1; k <= 1; k += 2) {
    float latc = (float)k * (hw + 2.6f);
    /* the main cable: a parabola from tower top to tower top, low in the middle */
    t3_vec3 prev = tp(t, s0 - 4, latc, ctop);
    pm *b = pmc_at(w, prev);
    pm_push(b);
    pm_layer(b, PL_MAIN);
    for (float s = s0 - 4 + 3.0f; s <= s1 + 4.01f; s += 3.0f) {
      float u = ((s - (s0 - 4)) / Ls) * 2.0f - 1.0f;
      float h = cmid + (ctop - cmid) * u * u;
      t3_vec3 cur = tp(t, s, latc, h);
      pm_rod(b, prev, cur, 0.17f, 5, rope);
      prev = cur;
    }
    pm_pop(b);
    /* hangers down to the deck edge and the posts and hand ropes at the edge */
    for (float s = s0 + 3.0f; s < s1 - 2.0f; s += 3.0f) {
      float u = ((s - (s0 - 4)) / Ls) * 2.0f - 1.0f, h = cmid + (ctop - cmid) * u * u;
      t3_vec3 top = tp(t, s, latc, h), bot = tp(t, s, (float)k * (hw + 0.3f), 1.0f);
      b = pmc_at(w, top);
      pm_push(b);
      pm_rod(b, top, bot, 0.045f, 3, rope_d);
      pm_pop(b);
    }
    for (float s = s0; s < s1 - 0.5f; s += 3.0f) {
      t3_vec3 p0 = tp(t, s, (float)k * (hw + 0.3f), 0), p1 = tp(t, s + 3.0f, (float)k * (hw + 0.3f), 0);
      b = pmc_at(w, p0);
      pm_push(b);
      pm_beam(b, V(p0.x, p0.y - 0.3f, p0.z), V(p0.x, p0.y + 1.45f, p0.z), 0.2f, wood);
      pm_beam(b, V(p0.x, p0.y + 1.4f, p0.z), V(p0.x, p0.y + 1.62f, p0.z), 0.26f, GOLD);
      pm_cable(b, V(p0.x, p0.y + 1.2f, p0.z), V(p1.x, p1.y + 1.2f, p1.z), 0.14f, 0.05f, rope);
      pm_cable(b, V(p0.x, p0.y + 0.6f, p0.z), V(p1.x, p1.y + 0.6f, p1.z), 0.14f, 0.045f, rope);
      pm_pop(b);
    }
  }
  /* cross logs and under ropes: the deck hangs from them */
  for (float s = s0; s <= s1; s += 3.0f) {
    t3_vec3 l = tp(t, s, -(hw + 0.9f), -0.7f), r = tp(t, s, hw + 0.9f, -0.7f);
    pm *b = pmc_at(w, l);
    pm_push(b);
    pm_beam(b, l, r, 0.42f, wood2);
    pm_pop(b);
  }
  for (int k = -1; k <= 1; k += 2) {
    for (float s = s0; s < s1 - 0.5f; s += 6.0f) {
      t3_vec3 p0 = tp(t, s, (float)k * (hw - 1.4f), -1.0f), p1 = tp(t, s + 6.0f, (float)k * (hw - 1.4f), -1.0f);
      pm *b = pmc_at(w, p0);
      pm_push(b);
      pm_cable(b, p0, p1, 0.5f, 0.1f, rope_d);
      pm_pop(b);
    }
  }
  /* lanterns on the posts, a few torn planks and ladders hanging beneath */
  for (float s = s0 + 9.0f; s < s1 - 6.0f; s += 12.0f) {
    for (int k = -1; k <= 1; k += 2) {
      t3_vec3 p = tp(t, s, (float)k * (hw + 0.3f), 1.62f);
      pm *b = pmc_at(w, p);
      pm_push(b);
      pm_layer(b, PL_METAL);
      pm_box_c(b, 0.34f, 0.5f, 0.34f, rgb(0.1f, 0.08f, 0.06f));
      pm_layer(b, PL_GLOW);
      pm_box_c(b, 0.26f, 0.36f, 0.26f, glow(rgb(1.0f, 0.62f, 0.2f), 4.0f));
      pm_layer(b, PL_MAIN);
      pm_pop(b);
    }
  }
  BR.node = pmc_finish(w, parent, "BridgeRope", true, TEX_WALL_WOOD);
  BR.axis = V(track_at(t, (s0 + s1) * 0.5f).r.x, 0, track_at(t, (s0 + s1) * 0.5f).r.z);

  /* the stone towers at each end, with a gate beam and banners */
  pmc *st = pmc_new(60, 63);
  pmc_texture_metres(st, 3.0f);
  for (int e = 0; e < 2; e++) {
    float s = e ? s1 + 4.0f : s0 - 4.0f;
    tsample a = track_at(t, s);
    for (int k = -1; k <= 1; k += 2) {
      pm *b = fr(st, &a, (float)k * (hw + 2.6f), -1.0f);
      pm_vary(b, 0.08f);
      pm_box(b, 4.0f, 6.0f, 4.0f, pc_mul(STONE_MOSS, 0.7f), STONE_D);
      pm_move(b, 0, 6.0f, 0);
      pm_box(b, 3.3f, 5.0f, 3.3f, STONE_D, STONE_L);
      pm_move(b, 0, 5.0f, 0);
      pm_box(b, 3.8f, 0.5f, 3.8f, pc_mul(GOLD, 0.7f), GOLD);
      pm_move(b, 0, 0.5f, 0);
      pm_box(b, 3.0f, 1.6f, 3.0f, pc_mul(RED, 0.8f), RED);
      pm_move(b, 0, 1.6f, 0);
      pm_box(b, 3.4f, 0.4f, 3.4f, pc_mul(STONE_D, 0.9f), STONE_L);
      pm_pop(b);
    }
    /* the gate beam across the road, high enough to pass under */
    pm *b = fr(st, &a, 0, 0);
    pm_move(b, 0, 11.4f, 0);
    pm_box(b, 2 * (hw + 2.6f) + 3.4f, 1.1f, 2.2f, pc_mul(STONE_D, 0.9f), STONE_L);
    pm_move(b, 0, 1.1f, 0);
    pm_box(b, 2 * (hw + 2.6f) + 1.0f, 0.5f, 1.6f, pc_mul(GOLD, 0.7f), GOLD);
    pm_pop(b);
  }
  pmc_finish(st, parent, "BridgeTowers", true, TEX_WALL_STONE);
  for (int e = 0; e < 2; e++) {
    float s = e ? s1 + 4.0f : s0 - 4.0f;
    for (int k = -1; k <= 1; k += 2) {
      t3_vec3 p = tp(t, s, (float)k * (hw + 2.6f), 11.4f);
      brazier_at(p, 0, 0.9f);
    }
  }
}

/* ── set piece: the stone ring the loop runs through ──────────────────────────────────────────────── */

static void build_ring(cw_node parent, const track *t) {
  if (!t->nloops) return;
  float s0 = C.loop0 - 6.0f, s1 = C.loop1 + 6.0f, step = 2.0f;
  pmc *c = pmc_new(50, 81);
  pmc_texture_metres(c, 3.0f);
  float top_y = -1e9f, top_s = 0;
  for (float s = s0; s < s1; s += step) {
    tsample a = track_at(t, s);
    if (a.flags & TF_GAP) continue;
    float hw = a.hw;
    pm *b = fr(c, &a, 0, 0);
    pm_vary(b, 0.07f);
    int rib = ((int)((s - s0) / step) % 5) == 0;
    pcol d0 = pc_mul(STONE_D, 1.35f), l0 = rib ? pc_mix(STONE_L, GOLD, 0.45f) : pc_mul(STONE_L, 1.15f);
    /* the ring's body under the road, with a deep cheek each side (the loop's own walls stay inside the cheeks) */
    pm_push(b);
    pm_move(b, 0, -4.2f, 0);
    pm_box(b, 2 * (hw + 1.6f), 3.3f, step * 1.06f, d0, l0);
    pm_pop(b);
    for (int k = -1; k <= 1; k += 2) {
      pm_push(b);
      pm_move(b, (float)k * (hw + 1.1f), -4.2f, 0);
      pm_box(b, 1.6f, 4.9f + (rib ? 1.4f : 0.4f), step * 1.06f, d0, rib ? GOLD : STONE_L);
      pm_pop(b);
      /* the ears: the ring's outer flange, wider than the road, with carved steps */
      pm_push(b);
      pm_move(b, (float)k * (hw + 3.6f), -5.6f, 0);
      pm_box(b, 2.8f, 1.9f + (rib ? 1.0f : 0.0f), step * 1.06f, pc_mul(STONE_D, 0.8f), pc_mix(STONE_L, STONE_MOSS, 0.2f));
      pm_pop(b);
    }
    if (a.p.y > top_y && a.u.y < -0.5f) top_y = a.p.y, top_s = s;
    pm_pop(b);
  }
  /* feet: heavy blocks from the ground up to the ring's flanks where it stands high above it */
  for (float s = s0; s < s1; s += 8.0f) {
    tsample a = track_at(t, s);
    /* only under road that lies level: on the loop's climb "under the road" points into the loop, and a foot there stood
     * a 5 m deep block of stone right across the loop's mouth (a wall in the road) */
    if ((a.flags & TF_GAP) || a.u.y < 0.9f) continue;
    t3_vec3 p = vadd(a.p, vmul(a.u, -4.6f));
    float g = gy(p.x, p.z);
    if (p.y - g < 2.0f || p.y > 36.0f) continue;
    if (crossing_below(t, (int)s, p.x, p.z)) continue;
    pm *b = at(c, V(p.x, g - 2.0f, p.z));
    pm_roty(b, yaw_of(V(a.f.x, 0, a.f.z)));
    pm_box(b, 2 * (a.hw + 5.0f), p.y - g + 2.0f, 5.0f, pc_mul(STONE_MOSS, 0.6f), STONE_D);
    pm_pop(b);
  }
  pmc_finish(c, parent, "StoneRing", true, TEX_WALL_STONE);
  /* glowing runes along both flanks, and fires at the foot of the ring */
  {
    pmc *gl = pmc_new(60, 83);
    for (float s = s0 + 4.0f; s < s1 - 2.0f; s += 6.0f) {
      tsample a = track_at(t, s);
      if (a.flags & TF_GAP) continue;
      for (int k = -1; k <= 1; k += 2) {
        pm *b = fr(gl, &a, (float)k * (a.hw + 5.05f), -5.0f);
        pm_layer(b, PL_GLOW);
        pcol rune = glow(((int)(s / 6.0f) & 1) ? rgb(0.2f, 1.0f, 0.85f) : rgb(1.0f, 0.7f, 0.2f), 3.5f);
        pm_box_c(b, 0.12f, 1.1f, 0.5f, rune);
        pm_box_c(b, 0.12f, 0.4f, 1.5f, rune);
        pm_pop(b);
      }
    }
    pmc_finish(gl, parent, "RingRunes", false, -1);
    for (int e = 0; e < 2; e++) {
      float s = e ? C.loop1 + 14.0f : C.loop0 - 14.0f;
      tsample a = track_at(t, s);
      if (a.p.y > 1.5f) continue;
      for (int k = -1; k <= 1; k += 2) {
        t3_vec3 p = vadd(a.p, vmul(a.r, (float)k * (a.hw + 2.4f)));
        p.y = gy(p.x, p.z);
        brazier_at(p, 0, 1.1f);
      }
    }
  }
  /* the keystone gem on top, flanked by two brazier bowls */
  if (top_y > 0) {
    tsample a = track_at(t, top_s);
    pmc *gm = pmc_new(60, 82);
    pm *b = fr(gm, &a, 0, -6.9f);
    pm_layer(b, PL_MAIN);
    pm_box(b, 5.0f, 1.2f, 4.0f, pc_mul(GOLD, 0.7f), GOLD);
    pm_move(b, 0, -0.4f, 0);
    pm_layer(b, PL_GLOW);
    pm_ball(b, 1.7f, 1.7f, 1.2f, 1, 0.0f, glow(rgb(0.2f, 0.9f, 1.0f), 5.0f), glow(rgb(0.7f, 1.0f, 1.0f), 8.0f));
    pm_pop(b);
    pmc_finish(gm, parent, "RingGem", false, -1);
  }
}

/* ── textures made here: streaming water and beams of light ──────────────────────────────────────── */

static t3_texture *make_tex(int w, int h, const uint8_t *px) {
  t3_texture *tx = t3_texture_new(w, h, px);
  tx->wrap_s = tx->wrap_t = T3_REPEAT;
  tx->flip_y = false;
  tx->generate_mipmaps = true;
  tx->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  tx->mag_filter = T3_LINEAR;
  tx->color_space = T3_SRGB_COLOR_SPACE;
  return tx;
}

/* falling water: vertical streaks that tile along v, soft at the sides */
static t3_texture *tex_fall(void) {
  enum { W = 64, H = 256 };
  static uint8_t px[W * H * 4];
  rng r = { 77 };
  for (int x = 0; x < W; x++) {
    float ph1 = rnd(&r) * 6.283f, ph2 = rnd(&r) * 6.283f, ph3 = rnd(&r) * 6.283f, amp = 0.45f + 0.55f * rnd(&r);
    int k1 = 2 + (int)(rnd(&r) * 3), k2 = 5 + (int)(rnd(&r) * 4), k3 = 11 + (int)(rnd(&r) * 6);
    float u = ((float)x + 0.5f) / (float)W, edge = sstep(0.0f, 0.16f, u) * (1.0f - sstep(0.84f, 1.0f, u));
    for (int y = 0; y < H; y++) {
      float v = (float)y / (float)H * 6.28318f;
      float n = 0.55f + 0.22f * sinf(v * (float)k1 + ph1) + 0.17f * sinf(v * (float)k2 + ph2) + 0.1f * sinf(v * (float)k3 + ph3);
      float a = clampf(n * amp * edge, 0, 1);
      a = a * a * 1.6f;
      uint8_t *p = px + (y * W + x) * 4;
      p[0] = 225, p[1] = 248, p[2] = 255;
      p[3] = (uint8_t)(clampf(a, 0, 1) * 255.0f);
    }
  }
  return make_tex(W, H, px);
}

/* a beam: bright in the middle of its length, gone at both ends, soft across */
static t3_texture *tex_beam(void) {
  enum { W = 16, H = 64 };
  static uint8_t px[W * H * 4];
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float v = ((float)y + 0.5f) / (float)H, u = ((float)x + 0.5f) / (float)W;
      float a = powf(sinf(3.14159f * v), 1.3f) * powf(sinf(3.14159f * u), 1.6f);
      uint8_t *p = px + (y * W + x) * 4;
      p[0] = 255, p[1] = 240, p[2] = 200;
      p[3] = (uint8_t)(clampf(a, 0, 1) * 255.0f);
    }
  t3_texture *tx = make_tex(W, H, px);
  return tx;
}

static struct {
  t3_material *fall[2];
  t3_texture *fall_tex;
  t3_material *beam;
  t3_object *shafts[16];
  t3_vec3 shaft_at[16];
  float shaft_s[16], shaft_lat[16]; /* where along the road each beam stands, and how far out (either side) */
  int nshaft;
  cw_node scene;
  bool sun_known;
} FX;

static t3_material *additive_material(t3_texture *tx, float r, float g, float b, float k) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_texture(m, T3_MAP, tx);
  m->color = (t3_color){ r * k, g * k, b * k };
  m->transparent = true;
  m->blending = T3_ADDITIVE_BLENDING;
  m->depth_write = false;
  m->side = T3_DOUBLE_SIDE;
  return m;
}

/* ── set piece: the waterfall and its ford ────────────────────────────────────────────────────────── */

static t3_vec3 fall_pt(float al, float lat, float y) {
  return V(FALL.c.x + FALL.f.x * al + FALL.r.x * lat, y, FALL.c.z + FALL.f.z * al + FALL.r.z * lat);
}

/* the rivers: one from the plunge pool to the ford, one leaving it (as polylines, for the ground and the water) */
static void river_setup(const track *t) {
  RIVERS[0].n = 5, RIVERS[0].width = 30.0f;
  t3_vec3 *a = RIVERS[0].p;
  a[0] = fall_pt(FALL_AL, -40, -0.35f), a[1] = fall_pt(FALL_AL - 14, -36, -0.35f), a[2] = fall_pt(FALL_AL - 36, -26, -0.35f), a[3] = fall_pt(FALL_AL - 54, -16, -0.35f),
  a[4] = fall_pt(FALL_AL - 62, -6, -0.2f);
  tsample e = track_at(t, C.water.s1 - 4.0f);
  RIVERS[1].n = 5, RIVERS[1].width = 22.0f;
  for (int i = 0; i < 5; i++) {
    float d = 6.0f + (float)i * 34.0f, lat = -15.0f - (float)i * 15.0f;
    RIVERS[1].p[i] = V(e.p.x + e.f.x * d + e.r.x * lat, -0.35f, e.p.z + e.f.z * d + e.r.z * lat);
  }
}

static void build_waterfall(cw_node scene, const track *t) {
  water_opts wo = water_defaults();
  wo.color = rgb(0.08f, 0.5f, 0.46f);
  wo.deep = rgb(0.04f, 0.42f, 0.4f);
  wo.clarity = 0.3f;
  wo.wave_height = 0.12f;
  wo.foam = 0.0f;
  wo.reflect = 0.0f;
  wo.refract = false;
  wo.flow_x = 0.5f, wo.flow_z = 0.5f;
  /* the ford itself: a film of water over the road, wider than the walls */
  if (!off("film")) water_along_range(scene, t, C.water.s0, C.water.s1, 5.0f, 0.2f, &wo);
  /* the river that feeds it from the plunge pool, and the one that leaves */
  if (!off("ribbons")) {
    water_ribbon(scene, RIVERS[0].p, RIVERS[0].n, RIVERS[0].width, &wo);
    water_ribbon(scene, RIVERS[1].p, RIVERS[1].n, RIVERS[1].width, &wo);
  }
  /* the cliff: big strata walls facing the road, ledges and mossy caps, with the water going over the top */
  {
    prop_model cw = prop_make(PK_CANYON_WALL, 44.0f, 5, 0, 0);
    inst walls = inst_new(cw, TEX_GROUND_ROCK, rgb(0.5f, 0.62f, 0.44f), 1.0f);
    float yawc = yaw_of(FALL.r);
    float lat_face = -(FALL.cliff_lat + 4.0f);
    for (int i = 0; i < 4; i++) {
      float al = -112.0f + 66.0f * (float)i;
      inst_add3(&walls, fall_pt(al, lat_face - 4.0f, -2.5f), yawc, V(1.0f, 1.0f, 1.0f));
    }
    if (!off("cliff")) inst_finish(&walls, scene, "FallCliff", true, 400);
  }
  pmc *c = pmc_new(60, 95);
  pmc_texture_metres(c, 5.0f);
  rng r = { 1234 };
  /* the lip the water leaves over, ledges where it breaks, boulders in the plunge pool */
  {
    float lat_lip = -(FALL.cliff_lat - 11.0f);
    pm *b2 = at(c, fall_pt(FALL_AL, lat_lip - 3.0f, FALL.h - 2.0f));
    pm_roty(b2, yaw_of(FALL.f));
    pm_vary(b2, 0.1f);
    pm_box(b2, 15.0f, 2.4f, 12.0f, pc_mul(STONE_MOSS, 0.6f), pc_mix(STONE_L, STONE_MOSS, 0.4f));
    pm_pop(b2);
    for (int i = 0; i < 9; i++) {
      float al = FALL_AL + rnds(&r) * 16.0f, lat = lat_lip + 1.0f + rnd(&r) * 9.0f, sz = 1.4f + rnd(&r) * 2.4f;
      pm *b3 = at(c, fall_pt(al, lat, -0.5f));
      pm_vary(b3, 0.12f);
      pm_rock(b3, sz, 0.7f, 0.3f, pc_mul(STONE_MOSS, 0.55f), pc_mix(STONE_D, STONE_MOSS, 0.4f));
      pm_pop(b3);
    }
  }
  if (!off("rocks")) pmc_finish(c, scene, "WaterfallRocks", true, TEX_GROUND_ROCK);
  /* the falling sheets: two layers so the water has depth, scrolling at slightly different speeds */
  FX.fall_tex = tex_fall();
  float lip = -(FALL.cliff_lat - 11.0f) + 3.0f;
  for (int i = 0; i < 2; i++) {
    t3_material *m = additive_material(FX.fall_tex, 0.85f, 1.0f, 1.0f, i ? 0.8f : 1.05f);
    FX.fall[i] = m;
    float w = i ? 13.0f : 11.0f, h = FALL.h - 0.2f;
    t3_mesh *me = t3_mesh_new(t3_plane_geometry_new(w, h, 1, 1), m);
    me->base.cast_shadow = false;
    t3_object_set_rotation(me, i ? -0.1f : -0.14f, 0, 0);
    t3_object *g = t3_group_new();
    t3_object_add(g, me);
    t3_release(me);
    t3_vec3 p = fall_pt(FALL_AL, lip + 0.3f * (float)i, h * 0.5f + 0.3f);
    t3_object_set_position(g, p.x, p.y, p.z);
    t3_object_set_rotation(g, 0, yaw_of(FALL.r), 0);
    hang(scene, i ? "FallB" : "FallA", g);
    t3_release(g);
  }
  /* mist lying on the ford */
  if (NV == 1) {
    tsample f0 = track_at(t, C.water.s0 + 25.0f), f1 = track_at(t, C.water.s1 - 25.0f);
    fx_ambient_at(scene, FXA_MIST, vadd(f0.p, V(0, 1.2f, 0)), V(10, 0.8f, 10), 0.4f, 170);
    fx_ambient_at(scene, FXA_MIST, vadd(f1.p, V(0, 1.2f, 0)), V(10, 0.8f, 10), 0.4f, 170);
  }
  /* boulders and a stone lip across the pool's far side; spray and mist rising from the plunge */
  t3_vec3 base = fall_pt(FALL_AL, lip + 3.0f, 1.0f);
  fx_ambient_at(scene, FXA_STEAM, base, V(7, 2.5f, 3), 1.6f, 190);
  fx_ambient_at(scene, FXA_MIST, V(base.x, 3.0f, base.z), V(14, 3.0f, 8), 0.45f, 200);
  
}

/* ── the jump's grounds: ruins under the flight ──────────────────────────────────────────────────────
 * From the top of the jump ramp and in the air the kart looks down on the ground round it, and that was bare low-poly
 * earth between the trees. Now it is a ruined court: broken walls along both sides with vines over them, a colonnade of
 * column stumps (a few toppled), two giant guardians facing the landing and, off to one side, a spring that drops over a
 * stepped outcrop into a pool. Everything stands on the ground (sunk into it a little) and well clear of the road's
 * footprint, the gap's included. */
static bool ruin_clear(const track *t, float x, float z, float need) {
  float ry;
  return road_gap(t, x, z, &ry) > need;
}

static void build_jump_ruins(cw_node scene, const track *t) {
  if (C.gap1 <= C.gap0) return;
  rng r = { 8181 };
  pmc *c = pmc_new(70, 83);
  pmc_texture_metres(c, 4.0f);
  pcol moss = pc_mix(STONE_MOSS, STONE_D, 0.35f), top = pc_mix(STONE_L, STONE_MOSS, 0.45f);
  int walls = 0, cols = 0;
  /* broken walls both sides, from before the ramp to past the landing */
  for (float s = C.gap0 - 70.0f; s < C.gap1 + 80.0f; s += 8.5f) {
    for (int side = -1; side <= 1; side += 2) {
      float keep = rnd(&r), latj = rnd(&r), hj = rnd(&r), hj2 = rnd(&r), lenj = rnd(&r);
      if (keep < 0.35f) continue;
      tsample a = track_at(t, s);
      float hw = 6.5f, lat = (float)side * (hw + 7.0f + latj * 4.0f), len = 5.0f + lenj * 3.0f;
      t3_vec3 p = track_point(t, s, lat, 0);
      if (!ruin_clear(t, p.x, p.z, 6.5f)) continue;
      float g = gy(p.x, p.z);
      pm *b = at(c, V(p.x, g - 0.6f, p.z));
      pm_roty(b, yaw_of(V(a.f.x, 0, a.f.z)));
      pm_vary(b, 0.1f);
      float h1 = 1.6f + 3.0f * hj, h2 = h1 * (0.35f + 0.5f * hj2);
      /* two courses of different height: a broken top */
      pm_push(b);
      pm_move(b, 0, 0, -len * 0.22f);
      pm_box(b, 1.3f, h1 + 0.6f, len * 0.56f, moss, top);
      pm_pop(b);
      pm_push(b);
      pm_move(b, 0, 0, len * 0.28f);
      pm_box(b, 1.3f, h2 + 0.6f, len * 0.44f, moss, top);
      pm_pop(b);
      /* a fallen block at its foot, on the far side */
      pm_push(b);
      pm_move(b, (float)side * 1.6f, 0.2f, len * 0.1f);
      pm_roty(b, 0.5f);
      pm_box(b, 1.0f, 0.8f, 1.4f, moss, top);
      pm_pop(b);
      pm_pop(b);
      /* vines over its top, hanging on the road side */
      t3_vec3 v = vadd(V(p.x, g - 0.6f + h1 + 0.55f, p.z), vmul(V(a.r.x, 0, a.r.z), -(float)side * 0.66f));
      inst_add(&D.vines, v, yaw_of(V(a.f.x, 0, a.f.z)) + 1.5708f, 0.22f + 0.1f * hj2);
      walls++;
    }
  }
  /* the colonnade: stumps and whole columns farther out, a few lying where they fell */
  for (float s = C.gap0 - 50.0f; s < C.gap1 + 70.0f; s += 11.0f) {
    for (int side = -1; side <= 1; side += 2) {
      float latj = rnd(&r), hj = rnd(&r), fall = rnd(&r), yawj = rnd(&r);
      tsample a = track_at(t, s);
      float lat = (float)side * (6.5f + 15.0f + latj * 5.0f);
      t3_vec3 p = track_point(t, s, lat, 0);
      if (!ruin_clear(t, p.x, p.z, 10.0f)) continue;
      float g = gy(p.x, p.z);
      pm *b = at(c, V(p.x, g - 0.3f, p.z));
      pm_vary(b, 0.08f);
      if (fall < 0.22f) {
        /* toppled: lying on the ground in three drums */
        pm_roty(b, yawj * 6.28f);
        for (int k = 0; k < 3; k++) {
          pm_push(b);
          pm_move(b, 0, 0.75f, -3.0f + 2.3f * (float)k + 0.15f * (float)k);
          pm_rotx(b, 1.5708f);
          pm_cyl(b, 0.8f, 0.8f, 2.2f, 10, moss, top);
          pm_pop(b);
        }
      } else {
        float h = fall < 0.6f ? 1.5f + 3.0f * hj : 7.0f + 2.5f * hj;
        pm_box(b, 2.2f, 0.7f, 2.2f, moss, top); /* the base */
        pm_push(b);
        pm_move(b, 0, 0.7f, 0);
        pm_cyl(b, 0.85f, 0.75f, h, 12, moss, top);
        if (h > 6.0f) { /* a whole one keeps its capital, gilt under the moss */
          pm_move(b, 0, h, 0);
          pm_box(b, 2.3f, 0.5f, 2.3f, pc_mul(GOLD, 0.6f), pc_mix(GOLD, STONE_MOSS, 0.4f));
          pm_move(b, 0, 0.5f, 0);
          pm_box(b, 2.6f, 0.6f, 2.6f, moss, top);
        }
        pm_pop(b);
        if (h > 6.0f) inst_add(&D.vines, V(p.x, g + 0.4f + h + 1.1f, p.z), yawj * 6.28f, 0.3f + 0.1f * hj);
      }
      pm_pop(b);
      (void)a;
      cols++;
    }
  }
  /* two giant guardians beside the landing, facing the karts as they come down */
  {
    tsample a = track_at(t, C.gap1 + 18.0f);
    for (int k = -1; k <= 1; k += 2) {
      t3_vec3 p = track_point(t, C.gap1 + 18.0f, (float)k * (a.hw + 12.0f), 0);
      if (!ruin_clear(t, p.x, p.z, 8.0f)) continue;
      p.y = gy(p.x, p.z) - 0.2f;
      inst_add(&D.guards, p, yaw_of(V(-a.f.x, 0, -a.f.z)) + (float)k * 0.35f, 2.1f);
    }
  }
  /* the spring: a stepped outcrop to one side of the flight, water over its lip into a pool, rocks round the pool */
  {
    float s = 0.5f * (C.gap0 + C.gap1) - 6.0f;
    tsample a = track_at(t, s);
    int side = -1;
    float lat = (float)side * (a.hw + 30.0f);
    t3_vec3 p = track_point(t, s, lat, 0), f = V(a.f.x, 0, a.f.z), rr = V(a.r.x, 0, a.r.z);
    if (ruin_clear(t, p.x, p.z, 20.0f)) {
      float g = gy(p.x, p.z);
      float yaw = yaw_of(vmul(rr, -(float)side)); /* the outcrop's +z faces the road */
      pm *b = at(c, V(p.x, g - 1.0f, p.z));
      pm_roty(b, yaw);
      pm_vary(b, 0.12f);
      pcol rock0 = pc_mul(STONE_MOSS, 0.6f), rock1 = pc_mix(STONE_D, STONE_MOSS, 0.5f);
      pm_push(b);
      pm_move(b, 0, 0, -6.0f);
      pm_box(b, 22.0f, 12.0f, 10.0f, rock0, rock1);
      pm_move(b, 0, 12.0f, -1.0f);
      pm_box(b, 16.0f, 3.0f, 7.0f, rock0, pc_mul(STONE_MOSS, 1.2f));
      pm_pop(b);
      for (int k = -1; k <= 1; k += 2) { /* shoulders either side of the falls */
        pm_push(b);
        pm_move(b, (float)k * 7.5f, 0, -0.5f);
        pm_box(b, 6.0f, 8.0f + 1.5f * (float)k, 4.0f, rock0, rock1);
        pm_pop(b);
      }
      /* a carved spout: a stone face over the lip the water leaves from */
      pm_push(b);
      pm_move(b, 0, 10.4f, -0.6f);
      pm_box(b, 4.6f, 2.6f, 1.2f, pc_mul(STONE_D, 0.9f), STONE_L);
      pm_move(b, 0, 2.6f, 0);
      pm_box(b, 5.4f, 0.5f, 1.4f, pc_mul(GOLD, 0.6f), GOLD);
      pm_pop(b);
      pm_pop(b);
      /* the pool's rim stones */
      for (int k = 0; k < 11; k++) {
        float an = 3.14159f * ((float)k / 10.0f), rx = 9.0f, rz = 6.0f;
        t3_vec3 q = vadd(vadd(p, vmul(rr, -(float)side * (4.0f + sinf(an) * rz))), vmul(f, cosf(an) * rx));
        if (!ruin_clear(t, q.x, q.z, 8.0f)) continue;
        pm *rb = at(c, V(q.x, gy(q.x, q.z) - 0.3f, q.z));
        pm_vary(rb, 0.12f);
        pm_rock(rb, 1.0f + 0.5f * (float)(k % 3), 0.6f, 0.3f, pc_mul(STONE_MOSS, 0.55f), pc_mix(STONE_D, STONE_MOSS, 0.4f));
        pm_pop(rb);
      }
      /* the falling sheet, in the waterfall's own scrolling material */
      if (FX.fall[0]) {
        t3_mesh *me = t3_mesh_new(t3_plane_geometry_new(4.2f, 10.6f, 1, 1), FX.fall[0]);
        me->base.cast_shadow = false;
        t3_object *grp = t3_group_new();
        t3_object_add(grp, me);
        t3_release(me);
        t3_vec3 fp = vadd(p, vmul(rr, -(float)side * 0.4f));
        t3_object_set_position(grp, fp.x, g + 5.6f, fp.z);
        t3_object_set_rotation(grp, 0, yaw, 0);
        hang(scene, "SpringFall", grp);
        t3_release(grp);
      }
      /* the pool */
      water_opts wo = water_defaults();
      wo.color = rgb(0.08f, 0.5f, 0.46f);
      wo.deep = rgb(0.04f, 0.42f, 0.4f);
      wo.clarity = 0.3f, wo.wave_height = 0.08f, wo.foam = 0.4f, wo.reflect = 0.0f, wo.refract = false;
      t3_vec3 pc = vadd(p, vmul(rr, -(float)side * 4.0f));
      water_surface(scene, pc.x, pc.z, 16.0f, 11.0f, g + 0.25f, &wo);
      if (NV == 1) fx_ambient_at(scene, FXA_MIST, V(pc.x, g + 1.0f, pc.z), V(6, 1.0f, 4), 0.5f, 150);
    }
  }
  cw_info("temple jump ruins: %d walls, %d columns", walls, cols);
  pmc_finish(c, scene, "JumpRuins", true, TEX_WALL_STONE);
}

/* ── set piece: the pyramid, stairs, shrine and the canopy ────────────────────────────────────────── */

static void shrine(pmc *c, float y) {
  pm *b = at(c, V(PYR.c.x, y, PYR.c.z));
  pm_roty(b, PYR.yaw);
  pm_vary(b, 0.07f);
  pm_box(b, 20.0f, 1.0f, 20.0f, pc_mul(STONE_D, 0.9f), STONE_L);
  pm_move(b, 0, 1.0f, 0);
  pm_box(b, 17.0f, 0.8f, 17.0f, STONE_D, pc_mul(STONE_L, 1.1f));
  pm_move(b, 0, 0.8f, 0);
  for (int i = 0; i < 4; i++) { /* corner pillars with gold collars */
    float sx = (i & 1) ? 6.2f : -6.2f, sz = (i & 2) ? 6.2f : -6.2f;
    pm_push(b);
    pm_move(b, sx, 0, sz);
    pm_box(b, 2.0f, 7.5f, 2.0f, pc_mul(STONE_D, 0.8f), STONE_L);
    pm_move(b, 0, 6.6f, 0);
    pm_box(b, 2.5f, 0.5f, 2.5f, pc_mul(GOLD, 0.7f), GOLD);
    pm_pop(b);
  }
  pm_push(b);
  pm_move(b, 0, 7.5f, 0);
  pm_box(b, 17.5f, 1.2f, 17.5f, pc_mul(RED, 0.7f), RED);
  pm_move(b, 0, 1.2f, 0);
  pm_box(b, 14.0f, 1.0f, 14.0f, pc_mul(STONE_D, 0.9f), STONE_L);
  pm_move(b, 0, 1.0f, 0);
  pm_box(b, 9.0f, 1.2f, 9.0f, pc_mul(RED, 0.7f), RED);
  pm_move(b, 0, 1.2f, 0);
  pm_box(b, 4.0f, 2.4f, 4.0f, pc_mul(GOLD, 0.7f), GOLD);
  pm_pop(b);
  /* the altar and the gem */
  pm_push(b);
  pm_box(b, 3.4f, 2.0f, 3.4f, STONE_D, STONE_L);
  pm_layer(b, PL_GLOW);
  pm_move(b, 0, 3.7f, 0);
  pm_ball(b, 1.3f, 1.6f, 1.3f, 1, 0.0f, glow(rgb(0.2f, 1.0f, 0.75f), 6.0f), glow(rgb(0.7f, 1.0f, 0.95f), 9.0f));
  pm_layer(b, PL_MAIN);
  pm_pop(b);
  pm_pop(b);
}

static void build_pyramid(cw_node parent, const track *t) {
  (void)t;
  pmc *c = pmc_new(70, 91);
  pmc_texture_metres(c, 4.0f);
  const int tiers = 10;
  float th = PYR.h / (float)tiers, half0 = PYR.base * 0.69f, topHalf = 10.6f, inset = (half0 - topHalf) / (float)tiers;
  for (int i = 0; i < tiers; i++) {
    float half = half0 - inset * (float)i, y = th * (float)i - 2.5f;
    pm *b = at(c, V(PYR.c.x, y, PYR.c.z));
    pm_roty(b, PYR.yaw);
    pm_vary(b, 0.07f);
    float k = (float)i / (float)tiers;
    pcol lo = pc_mix(pc_mul(STONE_MOSS, 0.7f), pc_mul(STONE_D, 0.9f), clampf(k * 3.0f, 0, 1));
    pm_box(b, half * 2, th + 2.5f, half * 2, lo, pc_mix(STONE_L, STONE_MOSS, 0.25f * (1.0f - k)));
    /* a band of red or gold under each ledge */
    pm_push(b);
    pm_move(b, 0, th + 2.5f - 0.7f, 0);
    pm_box(b, half * 2 + 0.25f, 0.7f, half * 2 + 0.25f, (i & 1) ? pc_mul(RED, 0.8f) : pc_mul(GOLD, 0.7f), (i & 1) ? RED : GOLD);
    pm_pop(b);
    pm_pop(b);
  }
  /* the four staircases, steps at about half a metre, with balustrades */
  int nsteps = (int)(PYR.h / 0.55f);
  float run = (half0 + 0.6f - topHalf) / (float)nsteps;
  for (int f = 0; f < 4; f++) {
    pm *b = at(c, V(PYR.c.x, 0, PYR.c.z));
    pm_roty(b, PYR.yaw + 1.5708f * (float)f);
    for (int k = 0; k < nsteps; k++) {
      float yk = (float)(k + 1) * (PYR.h / (float)nsteps), zk = half0 + 0.6f - run * (float)k;
      pm_push(b);
      pm_move(b, 0, -2.5f, zk - run * 0.5f);
      pm_box(b, 7.6f, yk + 2.5f, run + 0.35f, pc_mul(STONE_D, 0.85f), (k & 1) ? pc_mul(STONE_L, 1.1f) : pc_mul(STONE_L, 0.9f));
      pm_pop(b);
    }
    for (int sd = -1; sd <= 1; sd += 2) {
      pm_beam(b, V((float)sd * 4.3f, -0.4f, half0 + 1.4f), V((float)sd * 4.3f, PYR.h + 0.8f, topHalf - 0.2f), 1.1f, pc_mul(STONE_D, 1.2f));
      pm_beam(b, V((float)sd * 4.3f, -0.1f, half0 + 1.4f), V((float)sd * 4.3f, PYR.h + 1.2f, topHalf - 0.2f), 0.5f, GOLD);
    }
    pm_pop(b);
  }
  shrine(c, PYR.h);
  pmc_finish(c, parent, "Pyramid", true, TEX_WALL_STONE);
  /* braziers up each staircase, guardians at the top and bottom, vines on the tiers */
  for (int f = 0; f < 4; f++) {
    float ang = PYR.yaw + 1.5708f * (float)f;
    t3_vec3 dir = V(sinf(ang), 0, cosf(ang)), side = V(dir.z, 0, -dir.x);
    for (int k = 1; k <= 4; k++) {
      float kk = (float)k / 4.4f, z = lerpf(half0 + 1.0f, topHalf, kk), y = PYR.h * kk;
      for (int sd = -1; sd <= 1; sd += 2) {
        t3_vec3 p = V(PYR.c.x + dir.x * z + side.x * (float)sd * 5.6f, y + 0.9f, PYR.c.z + dir.z * z + side.z * (float)sd * 5.6f);
        brazier_at(p, 0, 0.8f);
      }
    }
    for (int sd = -1; sd <= 1; sd += 2) {
      t3_vec3 p = V(PYR.c.x + dir.x * (half0 + 5.0f) + side.x * (float)sd * 7.0f, 0, PYR.c.z + dir.z * (half0 + 5.0f) + side.z * (float)sd * 7.0f);
      p.y = gy(p.x, p.z) - 0.1f;
      inst_add(&D.guards, p, ang + 3.14159f, 1.15f);
    }
    for (int n = 0; n < 5; n++) {
      int tier = 2 + (int)(rnd(&R) * 8.0f);
      float half = half0 - inset * (float)tier, tang = rnds(&R) * (half - 3.0f);
      t3_vec3 p = V(PYR.c.x + dir.x * (half + 0.3f) + side.x * tang, th * (float)tier - 0.2f, PYR.c.z + dir.z * (half + 0.3f) + side.z * tang);
      inst_add(&D.vines, p, ang + 3.14159f, 0.7f + rnd(&R) * 0.5f);
    }
  }
  /* the pillar of light above the shrine */
  t3_material *m = additive_material(tex_beam(), 1.0f, 0.86f, 0.5f, 0.3f);
  FX.beam = m;
  t3_object *g = t3_group_new();
  for (int k = 0; k < 3; k++) {
    t3_mesh *me = t3_mesh_new(t3_plane_geometry_new(9.0f, 110.0f, 1, 1), m);
    me->base.cast_shadow = false;
    t3_object_set_rotation(me, 0, (float)k * 1.0472f, 0);
    t3_object_add(g, me);
    t3_release(me);
  }
  t3_object_set_position(g, PYR.c.x, PYR.h + 3.0f + 52.0f, PYR.c.z);
  hang(parent, "ShrineBeam", g);
  t3_release(g);
  if (NV == 1) fx_ambient_at(parent, FXA_FIREFLIES, V(PYR.c.x, 12, PYR.c.z), V(46, 12, 46), 1.6f, 230);
  fx_ambient_at(parent, FXA_MIST, V(PYR.c.x, 3, PYR.c.z), V(50, 3, 50), 1.1f, 260);
}

/* ── beams of light through the canopy, aimed at the sun once the sky has placed it ──────────────────── */

static void build_shafts(cw_node scene, const track *t) {
  FX.beam = FX.beam ? FX.beam : additive_material(tex_beam(), 1.0f, 0.88f, 0.55f, 0.55f);
  t3_material *m = additive_material(tex_beam(), 1.0f, 0.86f, 0.5f, 0.3f);
  rng r = { 909 };
  struct { float s, lat; } where[] = { { 40, 12 }, { 105, -14 }, { 200, 10 }, { 300, -11 }, { 520, 9 }, { 640, -9 }, { 800, 14 }, { 1100, -15 }, { 1700, 12 }, { 2330, -14 }, { 2560, 11 }, { 2760, -10 } };
  for (int i = 0; i < (int)(sizeof where / sizeof where[0]) && FX.nshaft < 16; i++) {
    float jit = 0.8f + 0.4f * rnd(&r);
    float rad = 2.2f + 2.2f * rnd(&r);
    /* its sheets are rad * 3.4 wide: their edge stays well clear of the wall (frame turns it to the sun's side) */
    float lat = fabsf(where[i].lat) * jit + 9.0f + rad * 1.7f;
    t3_vec3 p = tp(t, where[i].s, where[i].lat < 0 ? -lat : lat, 0);
    p.y = gy(p.x, p.z); /* the beam stands on the ground */
    t3_object *g2 = t3_group_new();
    for (int k = 0; k < 3; k++) { /* three crossed sheets read as a beam from every side */
      t3_mesh *me = t3_mesh_new(t3_plane_geometry_new(rad * 3.4f, 70.0f, 1, 1), m);
      me->base.cast_shadow = false;
      t3_object_set_rotation(me, 0, (float)k * 1.0472f, 0);
      t3_object_add(g2, me);
      t3_release(me);
    }
    t3_object_set_position(g2, p.x, p.y + 28.0f, p.z);
    FX.shaft_s[FX.nshaft] = where[i].s, FX.shaft_lat[FX.nshaft] = lat;
    FX.shafts[FX.nshaft] = g2;
    FX.shaft_at[FX.nshaft++] = V(p.x, p.y, p.z);
    char n[24];
    snprintf(n, sizeof n, "Shaft%d", i);
    hang(scene, n, g2);
    t3_retain(g2);
    t3_release(g2);
  }
  FX.sun_known = false;
}

/* ── the roadside: banners, braziers, guardians, vines and the far horizon ──────────────────────────── */

static bool roadside_ok(const track *t, float s, float lat_abs) {
  tsample a = track_at(t, s);
  if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL | TF_OPEN)) return false;
  if (a.p.y > 1.2f) return false; /* only where the road lies on the ground */
  (void)lat_abs;
  return true;
}

static void build_roadside(cw_node scene, const track *t) {
  rng r = { 555 };
  for (float s = 14; s < t->length - 10; s += 26.0f + rnd(&r) * 8.0f) {
    if (!roadside_ok(t, s, 0)) continue;
    int side = ((int)(s / 26.0f) & 1) ? 1 : -1;
    tsample a = track_at(t, s);
    float lat = (float)side * (a.hw + 2.0f);
    t3_vec3 p = vadd(a.p, vmul(a.r, lat));
    p.y = gy(p.x, p.z);
    float kind = rnd(&r);
    if (kind < 0.45f) inst_add(&D.banners, p, yaw_of(V(a.f.x, 0, a.f.z)) + 1.5708f, 1.0f);
    else if (kind < 0.8f) brazier_at(p, rnd(&r) * 6.0f, 0.95f);
    else inst_add(&D.guards, p, yaw_of(vmul(V(a.r.x, 0, a.r.z), -(float)side)), 1.0f);
  }
  /* vines draped over the walls' inner faces */
  for (float s = 4; s < t->length - 4; s += thin(6.0f, 12.0f) + rnd(&r) * 7.0f) {
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
    for (int k = -1; k <= 1; k += 2) {
      uint32_t bit = k < 0 ? TF_WALL_L : TF_WALL_R;
      if (!(a.flags & bit) || rnd(&r) < 0.4f) continue;
      float h = t->s[(int)s].walls >= 2 ? 1.6f : 0.9f;
      /* on the wall's face, not 0.3 m into the lane */
      t3_vec3 p = vadd(vadd(a.p, vmul(a.r, (float)k * (a.hw + 0.05f))), vmul(a.u, h));
      inst_add(&D.wallvines, p, yaw_of(V(a.f.x, 0, a.f.z)) + 1.5708f, 0.8f + rnd(&r) * 0.6f);
    }
  }
  /* flowers in the verge, in four colours */
  for (float s = 3; s < t->length - 3; s += thin(3.2f, 7.0f) + rnd(&r) * 3.0f) {
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL | TF_OPEN) || a.p.y > 1.2f) continue;
    for (int k = -1; k <= 1; k += 2) {
      if (rnd(&r) < 0.45f) continue;
      float lat = (float)k * (a.hw + 1.4f + rnd(&r) * 8.0f);
      t3_vec3 p = vadd(a.p, vmul(a.r, lat));
      float ry;
      if (road_gap(t, p.x, p.z, &ry) < 1.2f) continue;
      p.y = gy(p.x, p.z) - 0.05f;
      int fi = (int)(rnd(&r) * 4.0f) & 3;
      float yaw = rnd(&r) * 6.28f, sc = 1.2f + rnd(&r) * 1.2f;
      inst_add(&D.flowers[fi], p, yaw, sc);
    }
  }
  /* (vines from the canopy edge used to hang here 12-16 m up beside the road with no canopy above them: vines in the
   * sky. The draws stay so the rest of the dressing is laid out as before; nothing is placed) */
  for (float s = 6; s < t->length - 6; s += thin(14.0f, 26.0f) + rnd(&r) * 10.0f) {
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
    for (int k = -1; k <= 1; k += 2) {
      if (rnd(&r) < 0.4f) continue;
      (void)rnd(&r), (void)rnd(&r), (void)rnd(&r), (void)rnd(&r);
    }
  }
  /* crowns of canopy over parts of the road: leaf masses with holes between, high enough to pass under */
  for (float s = 20; s < t->length - 10; s += 22.0f + rnd(&r) * 14.0f) {
    tsample a = track_at(t, s);
    if ((a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL | TF_OPEN)) || a.p.y > 1.5f || fabsf(s - C.gorge_s) < 120.0f || s < 60.0f || s > t->length - 90.0f) continue;
    float side = rnd(&r) < 0.5f ? -1.0f : 1.0f;
    float lat = side * (a.hw + 6.0f + rnd(&r) * 8.0f);
    t3_vec3 p = vadd(a.p, vmul(a.r, lat));
    p.y = a.p.y + 20.0f + rnd(&r) * 8.0f;
    {
      float w = 0.8f + rnd(&r) * 0.9f;
      bool big = rnd(&r) >= 0.5f;
      /* no crown where another stretch of road (a climb, a bridge) passes at canopy height: the chase camera would fly through the
       * leaves */
      float R = (big ? 12.0f : 9.0f) * w * 1.3f + 8.0f;
      bool clear = true;
      for (int i = 0; i < t->n && clear; i += 3) {
        float dx = t->s[i].p.x - p.x, dz = t->s[i].p.z - p.z;
        if (dx * dx + dz * dz < R * R && t->s[i].p.y + 6.0f > p.y - R * 0.6f) clear = false;
      }
      if (!clear) continue;
      float yaw = rnd(&r) * 6.28f, sy = 0.7f + rnd(&r) * 0.8f, sz = w * (0.8f + 0.4f * rnd(&r));
      float cr = 0.8f + 0.5f * rnd(&r), cg = 0.9f + 0.2f * rnd(&r), cb = 0.7f + 0.4f * rnd(&r);
      inst_add_c(big ? &D.crowns2 : &D.crowns, p, t3_quat_from_axis_angle(V(0, 1, 0), yaw), V(w, sy, sz), (pcol){ cr, cg, cb });
    }
  }
}

/* guardians in pairs facing the road where the set pieces start: tunnel mouths, the ford, the jump */
static void build_sentinels(const track *t) {
  float where[16];
  int n = 0;
  for (int i = 0; i < C.ntunnel && n < 12; i++) where[n++] = C.tunnel[i].s0 - 5.0f, where[n++] = C.tunnel[i].s1 + 5.0f;
  where[n++] = C.water.s0 - 10.0f;
  where[n++] = C.water.s1 + 8.0f;
  where[n++] = C.gap0 - 55.0f;
  where[n++] = C.gap1 + 16.0f;
  for (int i = 0; i < n; i++) {
    tsample a = track_at(t, where[i]);
    if (a.p.y > 1.5f || (a.flags & (TF_GAP | TF_LOOP))) continue;
    for (int k = -1; k <= 1; k += 2) {
      t3_vec3 p = vadd(a.p, vmul(a.r, (float)k * (a.hw + 9.0f)));
      p.y = gy(p.x, p.z) - 0.1f;
      inst_add(&D.guards, p, yaw_of(V(-(float)k * a.r.x, 0, -(float)k * a.r.z)), 1.05f);
    }
  }
}

static void build_backdrop(cw_node scene, const track *t) {
  prop_set *ps = ps_new(scene, "Backdrop");
  ps_shadows(ps, false);
  ps_style(ps, PK_MESA, TEX_GROUND_ROCK, rgb(0.3f, 0.46f, 0.34f), 1.0f);
  ps_style(ps, PK_SPIRE, TEX_GROUND_ROCK, rgb(0.28f, 0.42f, 0.32f), 1.0f);
  scatter_opts o = scatter_defaults();
  o.cx = PYR.c.x * 0.5f + 280.0f, o.cz = 80.0f;
  o.ring_min = 640, o.ring_max = 1300;
  o.kinds[0] = PK_MESA, o.weight[0] = 3;
  o.kinds[1] = PK_SPIRE, o.weight[1] = 2;
  o.nkinds = 2;
  o.scale_min = 2.2f, o.scale_max = 4.8f;
  o.density = 0.012f;
  o.base_y = -2;
  o.seed = 77;
  o.margin = 40;
  o.lod_dist = 0;
  scatter_area(ps, t, &o);
  ps_finish(ps);
  /* far temples: stepped masses on the horizon with torch fires */
  pmc *c = pmc_new(300, 99);
  pmc_texture_metres(c, 8.0f);
  rng r = { 321 };
  for (int i = 0; i < 7; i++) {
    float ang = 6.28318f * ((float)i + 0.3f * rnds(&r)) / 7.0f, dist = 520.0f + 260.0f * rnd(&r);
    float cx = 280.0f + cosf(ang) * dist, cz = 100.0f + sinf(ang) * dist, half = 36.0f + 24.0f * rnd(&r);
    int tiers = 6 + (int)(rnd(&r) * 3.0f);
    for (int k = 0; k < tiers; k++) {
      float h = half * (1.0f - (float)k / (float)tiers * 0.82f);
      pm *b = at(c, V(cx, (float)k * 9.0f - 4.0f, cz));
      pm_roty(b, rnd(&r) * 3.0f);
      pm_box(b, 2 * h, 13.0f, 2 * h, pc_mul(STONE_MOSS, 0.8f), pc_mix(STONE_L, STONE_MOSS, 0.4f));
      pm_pop(b);
    }
    pm *b = at(c, V(cx, (float)tiers * 9.0f - 4.0f, cz));
    pm_layer(b, PL_GLOW);
    pm_ball(b, 3.0f, 4.0f, 3.0f, 1, 0.0f, glow(rgb(1.0f, 0.55f, 0.2f), 3.5f), glow(rgb(1.0f, 0.8f, 0.4f), 5.0f));
    pm_layer(b, PL_MAIN);
    pm_pop(b);
  }
  pmc_finish(c, scene, "FarTemples", false, TEX_WALL_STONE);
}

/* ── things that fly: parrots circling, butterflies in the ferns ──────────────────────────────────── */

typedef struct {
  t3_object *root, *wl, *wr;
  float cx, cy, cz, rad, speed, phase, flap, bob;
  bool ccw, butterfly;
} flyer;
static flyer FLY[24];
static int nfly;

static prop_model bird_body(int v, uint32_t seed) {
  static const uint32_t body[3] = { 0xd8281c, 0x1f6bd8, 0x25b24a }, belly[3] = { 0xf0a020, 0xf2d232, 0xe8e030 }, tail[3] = { 0x1c3fa8, 0xd8281c, 0x1c7a30 };
  pm *b = pm_new(seed);
  pm_layer(b, PL_MAIN);
  pm_ball(b, 0.11f, 0.11f, 0.3f, 1, 0.0f, hex(belly[v]), hex(body[v]));
  pm_push(b);
  pm_move(b, 0, 0.07f, 0.28f);
  pm_ball(b, 0.09f, 0.09f, 0.1f, 1, 0.0f, hex(body[v]), hex(body[v]));
  pm_push(b);
  pm_move(b, 0, -0.02f, 0.09f);
  pm_cyl(b, 0.04f, 0.0f, 0.1f, 4, rgb(0.95f, 0.8f, 0.3f), rgb(0.95f, 0.8f, 0.3f));
  pm_pop(b);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0, -0.28f);
  pm_wedge(b, 0.12f, 0.02f, 0.55f, hex(tail[v]), hex(tail[v]));
  pm_pop(b);
  return pm_finish(b);
}
static prop_model bird_wing(int v, uint32_t seed) {
  static const uint32_t wing[3] = { 0xc02015, 0x2a7ae0, 0x2cc050 }, tip[3] = { 0x1c3fa8, 0xf0d020, 0xf0d020 };
  pm *b = pm_new(seed);
  pm_layer(b, PL_LEAF);
  pm_quad(b, V(0, 0, 0.2f), V(0.6f, 0, 0.12f), V(0.62f, 0, -0.1f), V(0, 0, -0.22f), hex(wing[v]));
  pm_quad(b, V(0.6f, 0, 0.12f), V(1.05f, 0, 0.0f), V(0.98f, 0, -0.2f), V(0.62f, 0, -0.1f), hex(tip[v]));
  return pm_finish(b);
}
static prop_model fly_wing(uint32_t col, uint32_t edge) {
  pm *b = pm_new(1);
  pm_layer(b, PL_LEAF);
  pm_quad(b, V(0, 0, 0.0f), V(0.22f, 0, 0.16f), V(0.26f, 0, -0.04f), V(0, 0, -0.12f), hex(col));
  pm_quad(b, V(0.22f, 0, 0.16f), V(0.3f, 0, 0.12f), V(0.3f, 0, -0.02f), V(0.26f, 0, -0.04f), hex(edge));
  return pm_finish(b);
}

static void add_mesh_obj(t3_object *parent, t3_geometry *g, t3_material *m, float x, float y, float z, float sx) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false;
  t3_object_set_position(me, x, y, z);
  t3_object_set_scale(me, sx, 1, 1);
  t3_object_add(parent, me);
  t3_release(me);
}

static void make_flyer(cw_node scene, int variant, bool butterfly, t3_vec3 c, float rad, float speed, uint32_t seed) {
  if (nfly >= (NV > 1 ? 8 : 24)) return;
  flyer *f = &FLY[nfly];
  static char name[24][16];
  snprintf(name[nfly], 16, "Fly%d", nfly);
  t3_object *root = t3_group_new();
  t3_object *wl = t3_group_new(), *wr = t3_group_new();
  if (!butterfly) {
    prop_model bm = bird_body(variant % 3, seed), wm = bird_wing(variant % 3, seed + 1);
    add_mesh_obj(root, bm.g[PL_MAIN], prop_material(PL_MAIN, -1, (pcol){ 1, 1, 1 }, 1), 0, 0, 0, 1);
    add_mesh_obj(wl, wm.g[PL_LEAF], prop_material(PL_LEAF, -1, (pcol){ 1, 1, 1 }, 1), 0, 0, 0, 1);
    add_mesh_obj(wr, wm.g[PL_LEAF], prop_material(PL_LEAF, -1, (pcol){ 1, 1, 1 }, 1), 0, 0, 0, -1);
    prop_model_free(&bm);
    prop_model_free(&wm);
    t3_object_set_position(wl, 0.08f, 0.04f, 0.0f);
    t3_object_set_position(wr, -0.08f, 0.04f, 0.0f);
  } else {
    static const uint32_t cols[4][2] = { { 0xf08a1c, 0x201008 }, { 0x2f7fe8, 0x101830 }, { 0xf4e040, 0x403010 }, { 0xf4f4ec, 0x303030 } };
    prop_model wm = fly_wing(cols[variant & 3][0], cols[variant & 3][1]);
    add_mesh_obj(wl, wm.g[PL_LEAF], prop_material(PL_LEAF, -1, (pcol){ 1, 1, 1 }, 1), 0, 0, 0, 1);
    add_mesh_obj(wr, wm.g[PL_LEAF], prop_material(PL_LEAF, -1, (pcol){ 1, 1, 1 }, 1), 0, 0, 0, -1);
    prop_model_free(&wm);
  }
  t3_object_add(root, wl);
  t3_object_add(root, wr);
  { float sc = butterfly ? 2.6f : 2.0f; t3_object_set_scale(root, sc, sc, sc); }
  t3_retain(wl), t3_retain(wr), t3_retain(root);
  t3_release(wl);
  t3_release(wr);
  *f = (flyer){ .root = root, .wl = wl, .wr = wr, .cx = c.x, .cy = c.y, .cz = c.z, .rad = rad, .speed = speed, .phase = (float)(seed % 628) * 0.01f, .flap = butterfly ? 14.0f : 8.0f,
                .bob = butterfly ? 1.0f : 2.5f, .ccw = (seed & 1) != 0, .butterfly = butterfly };
  hang(scene, name[nfly], root);
  t3_release(root);
  nfly++;
}

static void flyers_update(double time) {
  for (int i = 0; i < nfly; i++) {
    flyer *f = &FLY[i];
    float t = (float)time * f->speed / f->rad * (f->ccw ? 1.0f : -1.0f) + f->phase;
    float x, y, z, vx, vz;
    if (f->butterfly) {
      float tt = (float)time * 0.6f + f->phase * 3.0f;
      x = f->cx + f->rad * (sinf(tt * 1.3f) + 0.5f * sinf(tt * 2.9f));
      z = f->cz + f->rad * (cosf(tt * 1.1f) + 0.5f * cosf(tt * 2.3f));
      y = f->cy + 0.5f * sinf(tt * 3.1f) + 0.3f * sinf(tt * 7.0f);
      vx = f->rad * (1.3f * cosf(tt * 1.3f) + 1.45f * cosf(tt * 2.9f)), vz = f->rad * (-1.1f * sinf(tt * 1.1f) - 1.15f * sinf(tt * 2.3f));
    } else {
      x = f->cx + cosf(t) * f->rad, z = f->cz + sinf(t) * f->rad;
      y = f->cy + f->bob * sinf(t * 3.0f);
      vx = -sinf(t) * (f->ccw ? 1.0f : -1.0f), vz = cosf(t) * (f->ccw ? 1.0f : -1.0f);
    }
    t3_object_set_position(f->root, x, y, z);
    t3_object_set_rotation(f->root, 0, atan2f(vx, vz), f->butterfly ? 0.0f : (f->ccw ? -0.25f : 0.25f));
    float a = sinf((float)time * f->flap + f->phase * 5.0f) * (f->butterfly ? 1.1f : 0.8f) + (f->butterfly ? 0.7f : 0.0f);
    t3_object_set_rotation(f->wl, 0, 0, a);
    t3_object_set_rotation(f->wr, 0, 0, -a);
  }
}

/* ── the scenery ────────────────────────────────────────────────────────────────────────────────── */

static void build_flyers(cw_node scene, const track *t) {
  /* parrots wheeling over the gorge, the pyramid, the start and the ford */
  struct { t3_vec3 c; float rad, speed; } birds[] = {
    { V(GORGE.c.x, -4.0f, GORGE.c.z), 38, 11 },
    { V(GORGE.c.x + GORGE.r.x * 60, -8.0f, GORGE.c.z + GORGE.r.z * 60), 30, 12 },
    { V(GORGE.c.x - GORGE.r.x * 60, -2.0f, GORGE.c.z - GORGE.r.z * 60), 34, 10 },
    { V(PYR.c.x, 58.0f, PYR.c.z), 56, 13 },
    { V(PYR.c.x, 46.0f, PYR.c.z), 70, 11 },
    { V(0, 24.0f, 140), 40, 10 },
    { V(0, 11.0f, 120), 13, 8 },
    { V(-40, 22.0f, 260), 36, 9 },
    { V(FALL.c.x, 20.0f, FALL.c.z), 44, 11 },
  };
  for (int i = 0; i < (int)(sizeof birds / sizeof birds[0]); i++) make_flyer(scene, i, false, birds[i].c, birds[i].rad, birds[i].speed, 100 + (uint32_t)i * 37);
  /* butterflies over the ferns beside the road */
  rng r = { 4141 };
  int placed = 0;
  for (float s = 30; s < t->length - 40 && placed < 12; s += 130.0f + rnd(&r) * 60.0f) {
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL | TF_OPEN) || a.p.y > 1.2f) continue;
    float lat = (r.s & 2 ? -1.0f : 1.0f) * (a.hw + 5.0f);
    t3_vec3 p = vadd(a.p, vmul(a.r, lat));
    for (int k = 0; k < 2; k++) make_flyer(scene, placed + k, true, V(p.x, gy(p.x, p.z) + 1.5f, p.z), 3.5f + 2 * rnd(&r), 1.0f, 300 + (uint32_t)(placed * 2 + k) * 17);
    placed += 2;
  }
}

static void build_gorge_river(cw_node scene) {
  water_opts wo = water_defaults();
  wo.deep = rgb(0.04f, 0.36f, 0.36f);
  wo.clarity = 0.3f;
  wo.reflect = 0.0f;
  wo.foam = 0.1f;
  wo.flow_x = 0.4f, wo.flow_z = 0.4f;
  t3_vec3 p[5];
  for (int i = 0; i < 5; i++) {
    float l = ((float)i - 2.0f) * 55.0f;
    p[i] = V(GORGE.c.x + GORGE.r.x * l, -GORGE.depth + 1.2f, GORGE.c.z + GORGE.r.z * l);
  }
  water_ribbon(scene, p, 5, 20.0f, &wo);
}

static void ambient_fx(cw_node scene, const track *t) {
  fx_ambient_follow(scene, FXA_DUST, 0.55f);
  /* the gorge: mist rolling at three depths along its length */
  for (int i = -2; i <= 2; i += (NV > 1 ? 2 : 1)) {
    t3_vec3 c = V(GORGE.c.x + GORGE.r.x * (float)i * 55.0f, -GORGE.depth * 0.35f, GORGE.c.z + GORGE.r.z * (float)i * 55.0f);
    fx_ambient_at(scene, FXA_MIST, c, V(40, 12, 40), 1.4f, 260);
  }
  fx_ambient_at(scene, FXA_MIST, V(GORGE.c.x, -6.0f, GORGE.c.z), V(60, 4, 60), 1.0f, 240);
  /* fireflies and motes in the verges at a few places along the lap */
  float where[] = { 70, 180, 320, 470, 700, 800, 1080, 1180, 2500, 2700 };
  for (int i = 0; i < (int)(sizeof where / sizeof where[0]); i++) {
    if (NV > 1 && (i & 1)) continue;
    tsample a = track_at(t, where[i]);
    float lat = (i & 1 ? -1.0f : 1.0f) * (a.hw + 7.0f);
    t3_vec3 p = vadd(a.p, vmul(a.r, lat));
    fx_ambient_at(scene, FXA_FIREFLIES, V(p.x, gy(p.x, p.z) + 2.5f, p.z), V(14, 3.0f, 14), 0.9f, 150);
  }
  for (int i = 0; i < C.ntunnel && NV == 1; i++) {
    tsample a = track_at(t, C.tunnel[i].s0 - 8);
    fx_ambient_at(scene, FXA_MIST, vadd(a.p, V(0, 2, 0)), V(9, 1.6f, 7), 0.8f, 160);
  }
}

static void scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  T = t;
  NV = game_planned_views();
  R.s = 7;
  nfly = 0;
  memset(&FX, 0, sizeof FX);
  memset(&BR, 0, sizeof BR);
  FX.scene = scene;
  read_course(t);
  GORGE.c = track_point(t, C.gorge_s, 0, 0);
  tsample gs = track_at(t, C.gorge_s);
  GORGE.f = V(gs.f.x, 0, gs.f.z), GORGE.r = V(gs.r.x, 0, gs.r.z);
  GORGE.half = (C.bridge.s1 - C.bridge.s0) * 0.5f - 4.0f;
  GORGE.depth = 44;
  float fs = (C.water.s0 + C.water.s1) * 0.5f - 20;
  tsample ws = track_at(t, fs);
  FALL.c = ws.p, FALL.r = ws.r, FALL.f = ws.f, FALL.cliff_lat = 54, FALL.h = 34;
  river_setup(t);
  PYR.c = C.hel_c;
  PYR.base = C.hel_r - 11.0f;
  PYR.h = 40;
  {
    t3_vec3 p0 = track_at(t, C.hel_s0).p;
    PYR.yaw = yaw_of(V(p0.x - PYR.c.x, 0, p0.z - PYR.c.z));
  }
  props_reset();
  G = ground_defaults();
  G.height = temple_ground;
  G.tex = TEX_GROUND_GRASS;
  G.tex_repeat = 6;
  G.tint = rgb(0.2f, 0.46f, 0.14f);
  G.tint2 = rgb(0.34f, 0.4f, 0.14f);
  G.near_cell = 64;
  G.detail_range = thin(32.0f, 18.0f);
  G.half = 1300;
  G.shoulder = 5;
  G.road_drop = 0.2f;
  G.darken_edge = 0.25f;
  if (!off("ground")) ground_plane(scene, t, &G);
  dec_begin();
  if (!off("jungle")) build_jungle(scene, t);
  if (!off("viaduct")) build_viaduct(scene, t);
  if (!off("tunnels")) build_tunnels(scene, t);
  if (!off("lining")) line_tunnels(scene);
  if (!off("bridge")) build_bridge(scene, t);
  if (!off("ring")) build_ring(scene, t);
  if (!off("fall")) build_waterfall(scene, t);
  if (!off("ruins")) build_jump_ruins(scene, t);
  if (!off("pyramid")) build_pyramid(scene, t);
  build_start(scene, t);
  if (!off("roadside")) build_roadside(scene, t);
  build_sentinels(t);
  if (!off("backdrop")) build_backdrop(scene, t);
  if (!off("shafts")) build_shafts(scene, t);
  dress_style st = dress_defaults();
  st.main = STONE_L, st.accent = GOLD, st.lamp = rgb(1.0f, 0.8f, 0.45f), st.has_colours = true;
  dress_checkpoints(scene, t, &st);
  deco_jump_marks(scene, t, &st);
  if (!off("flyers")) build_flyers(scene, t);
  if (!off("fx")) ambient_fx(scene, t);
  if (!off("gorge")) build_gorge_river(scene);
  dec_finish(scene);
}

static void scenery_frame(float alpha, double time) {
  (void)alpha;
  static double last = -1;
  float dt = last < 0 ? 0.0f : (float)(time - last);
  last = time;
  water_frame(time);
  /* flames flicker: four groups, each with its own rhythm */
  for (int i = 0; i < 4; i++) {
    t3_material *m = D.flame_mat[i];
    if (!m) continue;
    float tt = (float)time, k = 1.0f + 0.22f * sinf(tt * (9.0f + 2.3f * (float)i) + (float)i * 1.7f) + 0.12f * sinf(tt * (23.0f + 3.0f * (float)i) + (float)i * 5.1f);
    m->color = (t3_color){ k, k * (0.92f + 0.06f * sinf(tt * 13.0f + (float)i)), k * 0.88f };
  }
  /* the waterfall pours */
  if (FX.fall_tex && FX.fall[0] && FX.fall[1]) {
    FX.fall[0]->map->offset = (t3_vec2){ 0, -(float)fmod(time * 0.55, 1.0) };
    FX.fall[1]->map->offset = (t3_vec2){ 0.37f, -(float)fmod(time * 0.43 + 0.5, 1.0) };
  }
  (void)dt;
  flyers_update(time);
  if (cw_node_valid(BR.node)) { /* the bridge's ropes and posts sway: a slow sideways drift and a little heave */
    float sw = 0.14f * sinf((float)time * 1.1f) + 0.05f * sinf((float)time * 2.7f), hv = 0.04f * sinf((float)time * 1.9f + 1.0f);
    cw_node3d_set_position(BR.node, V(BR.axis.x * sw, hv, BR.axis.z * sw));
  }
  /* the sky puts the sun where the time of day says: point the beams of light at it once it is known */
  if (!FX.sun_known && FX.nshaft > 0) {
    cw_node sun = cw_node_get(FX.scene, "Sun");
    if (cw_node_valid(sun)) {
      const t3_mat4 *m = cw_node3d_global_transform(sun);
      t3_vec3 w = V(m->e[8], m->e[9], m->e[10]);
      float l = sqrtf(vdot(w, w));
      if (l > 0.5f && w.y / l > 0.05f) {
        w = vmul(w, 1.0f / l);
        t3_vec3 ax = V(w.z, 0, -w.x);  /* y cross w */
        float al = sqrtf(vdot(ax, ax));
        if (al > 1e-4f) {
          t3_quat q = t3_quat_from_axis_angle(vmul(ax, 1.0f / al), acosf(clampf(w.y, -1, 1)));
          for (int i = 0; i < FX.nshaft; i++) {
            /* a beam leans toward the sun from its foot: stand it on the side of the road the sun is on, clear of the
             * walls, so it leans away over the jungle. On the other side it leaned across the road and the karts drove
             * through a flat tan sheet where it cut into the hillside */
            tsample a = track_at(T, FX.shaft_s[i]);
            float side = (w.x * a.r.x + w.z * a.r.z) >= 0 ? 1.0f : -1.0f;
            t3_vec3 base = vadd(a.p, vmul(a.r, side * FX.shaft_lat[i]));
            base.y = gy(base.x, base.z);
            t3_object_set_quaternion(FX.shafts[i], q);
            t3_object_set_position(FX.shafts[i], base.x + w.x * 35.0f, base.y + w.y * 35.0f, base.z + w.z * 35.0f);
          }
          FX.sun_known = true;
        }
      }
    }
  }
}

#endif /* MAPFILE */

const map_def map_temple = {
  .name = "Overgrowth Temple",
  .blurb = "Jungle ruins: temple tunnels, a waterfall, rope bridges.",
  .scene = "scenes/temple.cwscene",
  .spec = &(track_spec){ .name = "temple", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 6.5f, .circuit = true },
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.55f, 0.55f, 0.5f }, .kerb_a = { 0.85f, 0.14f, 0.1f }, .kerb_b = { 0.95f, 0.72f, 0.2f }, .wall = { 0.62f, 0.66f, 0.5f },
            .wall_top = { 0.8f, 0.78f, 0.6f }, .road_rough = 0.8f, .road_tex = TEX_ROAD_STONE, .wall_tex = TEX_WALL_STONE },
  .laps = 3,
  .par_lap = 60,
#ifndef MAPFILE
  .scenery = scenery,
  .scenery_frame = scenery_frame,
#endif
};
