/* pugball's stadium: everything round the pitch, and its weather. All of it
 * is placed from a seeded integer hash (one draw per statement: argument
 * evaluation order is the host's), so every host builds the same stadium
 * (debug field scenery.hash), and it only ever draws.
 *
 *   pitch    thousands of instanced grass clumps (curved, tapered blades)
 *            in the mown stripes, swaying in a slow travelling breeze and
 *            flattened where the pugs and the cat roll over them
 *   stands   concrete tiers swept round the bowl, moulded seats in blocks
 *            of the team colours, and a crowd of dog fans in jerseys who
 *            bob along and leap up on a goal
 *   roof     a cantilevered canopy with a ring of floodlights under its lip,
 *            on steel columns; at night each lamp is a real spot light on
 *            the pitch (clustered lighting, cartwheel/lighting.h), a few of
 *            them casting shadows from the shadow atlas
 *   outside  per stadium: park trees, a beach with palms and the sea, a
 *            snowy yard of pines in falling snow, a rose garden with
 *            hedges and drifting petals, a neon city of lit towers */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "cartwheel/quality.h"
#include "world.h"

#define MAX_FANS 5200
#define MAX_CLUMPS 8000
#define CROWD_CHUNKS 12
#define GRASS_BANDS 8
#define FLOODS 18    /* spot lights in the floodlight ring at night */

static struct {
  bool ready;
  int theme;
  cw_node root;
  /* the crowd in sections of the stands (one set of instanced meshes each,
   * so the view drops the sections it cannot see); its fans in section order */
  struct crowd_chunk {
    t3_instanced_mesh *im[4];   /* body, head, arms, and the scarves (only the fans holding one) */
    int first, n, nscarf;       /* fans [first, first + n); scarf holders among them */
    int shown, shown_scarf;     /* drawn at the current quality */
    t3_vec3 c;                  /* where the section's meshes sit (instances are relative to it) */
  } ch[CROWD_CHUNKS];
  int nchunks;
  /* the fans' meshes (body, head, arms, scarf): [0] as built, [1] the light
   * ones split screen draws (its views are a half or a quarter of the
   * screen, where the detail is under a pixel), and which the crowd has */
  t3_geometry *fgeo[2][4];
  int flod;
  float *fan_x, *fan_y, *fan_z, *fan_yaw, *fan_phase;
  uint8_t *fan_team, *fan_kind; /* kind: 0 sits, 1 claps and waves, 2 holds a scarf up */
  float t, ex;                  /* this tick's crowd time and excitement (crowd_range) */
  int cheer_team;               /* the side that just scored (-1 none) */
  int nfans;
  float excite;
  /* the pitch's grass in bands across the field (GRASS_BANDS along z), one
   * InstancedMesh each so the views cull the bands they cannot see; the
   * clumps in band order, band b holding [start, start + n) */
  struct { t3_instanced_mesh *im; int start, n; } gband[GRASS_BANDS];
  int gband_shown[GRASS_BANDS]; /* clumps drawn at the current quality */
  float gband_c[GRASS_BANDS][3]; /* each band's middle (x, z) and half its length */
  uint8_t *gband_of;
  float *gx, *gz, *gyaw, *gs, *gsy;
  int nclumps;
  t3_material *flood;
  float ring_y, ring_d;         /* the floodlights: their height and distance out from the wall */
  cw_node flood_light[FLOODS];  /* night: a spot light under each lamp (none by day) */
  float flood_energy;
  int ticks;
} SC;

uint32_t scenery_hash = 2166136261u;
static uint32_t seed_state;

static uint32_t rndu(void) {
  uint32_t x = seed_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  seed_state = x ? x : 1;
  scenery_hash = (scenery_hash ^ seed_state) * 16777619u;
  return seed_state;
}
static float rnd(void) { return (float)(rndu() & 0xffffff) / 16777216.0f; }
static float rr(float a, float b) {
  float t = rnd();
  return a + (b - a) * t;
}

static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}
static t3_material *stdm(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}
static t3_material *glow(float r, float g, float b, float k) {
  t3_material *m = stdm(lin(r, g, b), 0.5f, 0);
  m->emissive = lin(r, g, b);
  m->emissive_intensity = k;
  return m;
}
static cw_node hang(t3_object *obj) {
  cw_node n = cw_node_new("Node3D", "Prop");
  cw_node_add_child(SC.root, n);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, obj);
  cw_node3d_set_object(n, wrap);
  t3_release(wrap);
  return n;
}
/* An instanced mesh staged whole, hung as one mesh per part (part[i] is
 * instance i's), so the views cull the parts they cannot see; each part's
 * sphere comes from its instances (call it for instances that do not move).
 * Takes the staged mesh's reference. */
static void hang_parts(t3_instanced_mesh *src, int n, const uint8_t *part, int parts) {
  const float *mat = (const float *)src->instance_matrix->array;
  const float *col = src->instance_color ? (const float *)src->instance_color->array : NULL;
  for (int p = 0; p < parts; p++) {
    int np = 0;
    for (int i = 0; i < n; i++) np += part[i] == p;
    if (!np) continue;
    t3_instanced_mesh *im = t3_instanced_mesh_new(src->mesh.geometry, src->mesh.materials[0], np);
    for (int i = 0, j = 0; i < n; i++) {
      if (part[i] != p) continue;
      memcpy((float *)im->instance_matrix->array + j * 16, mat + i * 16, 16 * sizeof(float));
      if (col) t3_instanced_mesh_set_color_at(im, j, (t3_color){ col[i * 3], col[i * 3 + 1], col[i * 3 + 2] });
      j++;
    }
    im->mesh.base.cast_shadow = src->mesh.base.cast_shadow;
    im->mesh.base.receive_shadow = src->mesh.base.receive_shadow;
    hang(&im->mesh.base);
    t3_release(im);
  }
  t3_release(src);
}
static t3_mesh *mesh_at(t3_object *g, t3_geometry *geo, t3_material *m, t3_vec3 p, t3_vec3 s) {
  t3_mesh *me = t3_mesh_new(geo, m);
  me->base.position = p;
  me->base.scale = s;
  me->base.cast_shadow = me->base.receive_shadow = true;
  t3_object_add(g, me);
  t3_release(me);
  return me;
}
static void add_mesh(t3_geometry *g, t3_material *m, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = true;
  me->base.frustum_culled = false;
  hang(&me->base);
  t3_release(me);
  t3_release(g);
  t3_release(m);
}
static t3_quat yawq(float yaw) {
  float s, c;
  cw_sincos(yaw * 0.5f, &s, &c);
  return (t3_quat){ 0, s, 0, c };
}

/* ── geometry builders ──────────────────────────────────────────────── */

typedef struct mb { float *pos, *nor, *col; uint32_t *idx; int nv, ni, cv, ci; } mb;
static int mb_v(mb *b, t3_vec3 p, t3_vec3 n, float shade) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 256;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->cv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->cv);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->cv);
  }
  float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *C = b->col + b->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x / l, N[1] = n.y / l, N[2] = n.z / l;
  C[0] = C[1] = C[2] = shade;
  return b->nv++;
}
static void mb_t(mb *b, int a, int c, int d) {
  if (b->ni + 3 > b->ci) {
    b->ci = b->ci ? b->ci * 2 : 768;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->ci);
  }
  b->idx[b->ni++] = (uint32_t)a, b->idx[b->ni++] = (uint32_t)c, b->idx[b->ni++] = (uint32_t)d;
}
static t3_geometry *mb_done(mb *b, bool colours) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  if (colours) {
    a = t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3);
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, a);
    t3_release(a);
  }
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  free(b->pos);
  free(b->nor);
  free(b->col);
  free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}
/* an ellipsoid appended (low poly), shaded uniformly */
static void mb_blob(mb *b, t3_vec3 c, t3_vec3 r, int ws, int hs, float shade) {
  int base = b->nv;
  for (int j = 0; j <= hs; j++) {
    float th = 3.1415927f * j / hs, st = cw_sin(th), ct = cw_cos(th);
    for (int i = 0; i <= ws; i++) {
      float ph = 6.2831853f * i / ws, sp = cw_sin(ph), cp = cw_cos(ph);
      t3_vec3 n = { st * cp, ct, st * sp };
      mb_v(b, (t3_vec3){ c.x + r.x * n.x, c.y + r.y * n.y, c.z + r.z * n.z }, (t3_vec3){ n.x / r.x, n.y / r.y, n.z / r.z }, shade);
    }
  }
  for (int j = 0; j < hs; j++)
    for (int i = 0; i < ws; i++) {
      int a = base + j * (ws + 1) + i, c2 = a + ws + 1;
      mb_t(b, a, a + 1, c2);
      mb_t(b, a + 1, c2 + 1, c2);
    }
}

/* a curved, tapering grass blade (both windings: lit alike from either side) */
static uint32_t gh = 7;
static float hrnd(void) {
  gh ^= gh << 13;
  gh ^= gh >> 17;
  gh ^= gh << 5;
  return (float)(gh & 0xffffff) / 16777216.0f;
}
static void blade(mb *b, t3_vec3 base, float ang, float h, float curve, float w0) {
  enum { SEG = 3 };
  float dx = cw_cos(ang), dz = cw_sin(ang), sx = -dz, sz = dx;
  int prev[2] = { -1, -1 };
  for (int k = 0; k <= SEG; k++) {
    float t = (float)k / SEG;
    t3_vec3 c = { base.x + dx * curve * t * t, base.y + h * t, base.z + dz * curve * t * t };
    t3_vec3 n = { dx * 0.45f, 1, dz * 0.45f };
    float shade = 0.45f + 0.55f * t, w = w0 * (1 - t) * 0.5f;
    if (k == SEG) {
      int tip = mb_v(b, c, n, shade);
      mb_t(b, prev[0], prev[1], tip);
      mb_t(b, prev[0], tip, prev[1]);
      break;
    }
    int l = mb_v(b, (t3_vec3){ c.x - sx * w, c.y, c.z - sz * w }, n, shade);
    int r = mb_v(b, (t3_vec3){ c.x + sx * w, c.y, c.z + sz * w }, n, shade);
    if (prev[0] >= 0) {
      mb_t(b, prev[0], prev[1], r);
      mb_t(b, prev[0], r, l);
      mb_t(b, prev[0], r, prev[1]);
      mb_t(b, prev[0], l, r);
    }
    prev[0] = l;
    prev[1] = r;
  }
}

/* ── the pitch's grass ──────────────────────────────────────────────── */

static float floor_half(float z) {
  const float W2 = AW - AR, L2 = AL - AR, r = AC - AR;
  float dz = fabsf(z) - (L2 - r);
  if (dz <= 0) return W2;
  if (dz >= r) return W2 - r;
  return W2 - r + sqrtf(r * r - dz * dz);
}

static void build_grass(float tint_r, float tint_g, float tint_b) {
  mb b = { 0 };
  gh = 11;
  for (int i = 0; i < 7; i++) {
    float ang = i * 0.8976f + (hrnd() - 0.5f) * 0.5f, r0 = 0.01f + hrnd() * 0.05f;
    float h = 0.07f + hrnd() * 0.06f, cv = 0.02f + hrnd() * 0.04f, w = 0.016f + hrnd() * 0.006f;
    blade(&b, (t3_vec3){ cw_cos(ang) * r0, 0, cw_sin(ang) * r0 }, ang, h, cv, w);
  }
  t3_material *m = phys(lin(1, 1, 1), 0.6f, 0);
  m->vertex_colors = true;
  m->sheen = 0.6f;
  m->sheen_color = lin(0.8f, 0.95f, 0.45f);
  m->sheen_roughness = 0.5f;
  t3_geometry *clump = mb_done(&b, true);
  float *gx = malloc(sizeof(float) * MAX_CLUMPS), *gz = malloc(sizeof(float) * MAX_CLUMPS);
  float *gyaw = malloc(sizeof(float) * MAX_CLUMPS), *gs = malloc(sizeof(float) * MAX_CLUMPS);
  float *gsy = malloc(sizeof(float) * MAX_CLUMPS);
  t3_color *gcol = malloc(sizeof(t3_color) * MAX_CLUMPS);
  int n = 0;
  const float L2 = AL - AR - 0.3f;
  for (int i = 0; i < MAX_CLUMPS * 2 && n < MAX_CLUMPS; i++) {
    float z = rr(-L2, L2);
    float x = rr(-1, 1);
    float hw = floor_half(z) - 0.3f;
    x *= hw;
    float yaw = rr(0, 6.2831853f);
    float s = rr(0.8f, 1.3f);
    float sy = rr(0.7f, 1.3f);
    float v = rr(0.85f, 1.1f);
    gx[n] = x, gz[n] = z, gyaw[n] = yaw, gs[n] = s, gsy[n] = sy;
    /* the mown stripes: the same bands as the floor below */
    int band = (int)floorf((z + (AL - AR)) / 4.0f);
    float k = band & 1 ? 0.84f : 1.0f;
    gcol[n] = lin(tint_r * k * v * 0.55f, tint_g * k * v * 0.82f, tint_b * k * v * 0.4f);
    n++;
  }
  SC.nclumps = n;
  /* sorted into the bands, each keeping the clumps' order */
  SC.gx = malloc(sizeof(float) * (n + 1));
  SC.gz = malloc(sizeof(float) * (n + 1));
  SC.gyaw = malloc(sizeof(float) * (n + 1));
  SC.gs = malloc(sizeof(float) * (n + 1));
  SC.gsy = malloc(sizeof(float) * (n + 1));
  SC.gband_of = malloc((size_t)n + 1);
  int at = 0;
  for (int b = 0; b < GRASS_BANDS; b++) {
    SC.gband[b].start = at;
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < n; i++) {
      if (CW_CLAMP((int)((gz[i] + L2) / (2 * L2) * GRASS_BANDS), 0, GRASS_BANDS - 1) != b) continue;
      SC.gx[at] = gx[i], SC.gz[at] = gz[i], SC.gyaw[at] = gyaw[i], SC.gs[at] = gs[i], SC.gsy[at] = gsy[i];
      SC.gband_of[at] = (uint8_t)b;
      x0 = fminf(x0, gx[i]), x1 = fmaxf(x1, gx[i]), z0 = fminf(z0, gz[i]), z1 = fmaxf(z1, gz[i]);
      at++;
    }
    int nb = SC.gband[b].n = at - SC.gband[b].start;
    if (!nb) continue;
    t3_instanced_mesh *im = t3_instanced_mesh_new(clump, m, nb);
    for (int k = 0, i = 0; i < n; i++)
      if (CW_CLAMP((int)((gz[i] + L2) / (2 * L2) * GRASS_BANDS), 0, GRASS_BANDS - 1) == b) t3_instanced_mesh_set_color_at(im, k++, gcol[i]);
    /* the band's region with a margin for the clumps themselves (a scaled clump
     * is under 0.35 m from its root, bent by the breeze included), set once:
     * not recomputed as the breeze bends them */
    float dx = (x1 - x0) * 0.5f + 0.4f, dz = (z1 - z0) * 0.5f + 0.4f;
    t3_instanced_mesh_set_bounding_sphere(im, &(t3_sphere){ { (x0 + x1) * 0.5f, 0.15f, (z0 + z1) * 0.5f }, sqrtf(dx * dx + dz * dz + 0.35f * 0.35f) });
    SC.gband_c[b][0] = (x0 + x1) * 0.5f, SC.gband_c[b][1] = (z0 + z1) * 0.5f, SC.gband_c[b][2] = dz;
    im->mesh.base.receive_shadow = true;
    im->mesh.base.cast_shadow = false;
    SC.gband[b].im = im;
    hang(&im->mesh.base); /* the tree holds it; SC keeps its own reference */
  }
  free(gx), free(gz), free(gyaw), free(gs), free(gsy), free(gcol);
  t3_release(clump);
  t3_release(m);
}

/* the breeze: two broad travelling gusts and a small flutter, a function of
 * the tick and the position only (every host draws the same breeze) */
static const float WIND_X = 0.96f, WIND_Z = 0.28f;
static float gust(float x, float z, float t) {
  float p = x * WIND_X + z * WIND_Z, q = -x * WIND_Z + z * WIND_X;
  float g1 = 0.5f + 0.5f * cw_sin(6.2831853f * (p / 22 - t / 7.5f));
  float g2 = 0.5f + 0.5f * cw_sin(6.2831853f * ((p * 0.7f + q * 0.35f) / 13 - t / 9.7f) + 1.7f);
  float g = g1 * 0.65f + g2 * 0.35f;
  return g * g;
}
/* half the clumps a tick (alternating): a breeze at 30 Hz per blade; in
 * blocks on the job pool, each writing only its own clumps */
static struct { const sim *s; float t; int parity; } GT;
static void grass_range(int begin, int end, int worker, void *ctx) {
  (void)worker, (void)ctx;
  const sim *s = GT.s;
  float t = GT.t;
  for (int j = begin; j < end; j++) {
    int i = j * 2 + GT.parity;
    if (i >= SC.nclumps) break;
    int b = SC.gband_of[i], li = i - SC.gband[b].start;
    t3_instanced_mesh *im = SC.gband[b].im;
    if (li >= im->count) continue; /* not drawn at this quality */
    float x = SC.gx[i], z = SC.gz[i];
    float bend = 0.08f + 0.5f * gust(x, z, t) + 0.08f * cw_sin(t * 2.4f + x * 1.9f + z * 1.3f);
    /* the pugs and the cat flatten the grass they roll over */
    float press = 0;
    for (int k = 0; k < MAXP; k++) {
      const pug *u = &s->p[k];
      if (!u->on || u->dead || u->p.y > 1.2f) continue;
      float dx = x - u->p.x, dz = z - u->p.z, d2 = dx * dx + dz * dz;
      if (d2 < 0.5f) press = fmaxf(press, 1 - d2 / 0.5f);
    }
    if (s->ball.y < RB + 0.4f) {
      float dx = x - s->ball.x, dz = z - s->ball.z, d2 = dx * dx + dz * dz;
      if (d2 < 1.1f) press = fmaxf(press, 1 - d2 / 1.1f);
    }
    float sy = SC.gsy[i] * (1 - 0.7f * press);
    float sn, c, sc = SC.gs[i];
    cw_sincos(SC.gyaw[i], &sn, &c);
    float kx = bend * WIND_X, kz = bend * WIND_Z;
    float m[16] = { c * sc, 0, -sn * sc, 0, 0, sy, 0, 0, sn * sc, 0, c * sc, 0, x, 0, z, 1 };
    for (int col = 0; col < 3; col++) {
      m[col * 4 + 0] += kx * m[col * 4 + 1];
      m[col * 4 + 2] += kz * m[col * 4 + 1];
    }
    memcpy((float *)im->instance_matrix->array + 16 * li, m, sizeof m);
  }
}
static void grass_tick(const sim *s, float t, int parity) {
  if (!SC.nclumps) return;
  GT.s = s, GT.t = t, GT.parity = parity;
  cw_parallel_for((SC.nclumps - parity + 1) / 2, 256, grass_range, NULL); /* the clumps not drawn are skipped */
  for (int b = 0; b < GRASS_BANDS; b++)
    if (SC.gband[b].im) SC.gband[b].im->instance_matrix->version++;
}

/* ── the stands, the crowd, the roof ────────────────────────────────── */

#define ROWS 20
#define TIER0 6.5f   /* the first row's distance out from the wall */
#define TIER_Y 2.6f  /* and its height */
#define ROW_D 0.9f
#define ROW_H 0.45f

static void build_stands(int theme) {
  /* the tiers: a staircase swept round the bowl (flat-shaded steps), the
   * front wall below the first row, the back wall above the last */
  prof pr[ROWS * 4 + 8];
  int n = 0;
  pr[n++] = (prof){ TIER0 - 1.5f, 0, -1, 0 };
  pr[n++] = (prof){ TIER0 - 1.5f, TIER_Y, -1, 0 };
  pr[n++] = (prof){ TIER0 - 1.5f, TIER_Y, 0, 1 };
  for (int r = 0; r < ROWS; r++) {
    float d = TIER0 + r * ROW_D, y = TIER_Y + r * ROW_H;
    pr[n++] = (prof){ d, y, 0, 1 };
    pr[n++] = (prof){ d, y, -1, 0 };
    pr[n++] = (prof){ d, y + ROW_H, -1, 0 };
    pr[n++] = (prof){ d, y + ROW_H, 0, 1 };
  }
  float dend = TIER0 + ROWS * ROW_D, yend = TIER_Y + ROWS * ROW_H;
  pr[n++] = (prof){ dend, yend, 0, 1 };
  pr[n++] = (prof){ dend, yend, -1, 0 };
  pr[n++] = (prof){ dend, yend + 2.5f, -1, 0 };
  t3_material *conc = phys(lin(0.82f, 0.82f, 0.85f), 0.85f, 0);
  tex_apply(conc, TEX_CONCRETE, 1, 1, 1);
  add_mesh(arena_sweep(pr, n, 3, 3, 0, false), conc, true);
  /* the concourse floor between the bowl and the stands */
  prof walk[2] = { { 0, 0.02f, 0, 1 }, { TIER0 - 1.5f, 0.02f, 0, 1 } };
  t3_material *wm = phys(lin(0.55f, 0.55f, 0.58f), 0.9f, 0);
  tex_apply(wm, TEX_CONCRETE, 1, 1, 1);
  add_mesh(arena_sweep(walk, 2, 3, 3, 0, false), wm, false);
  /* the roof: a canopy from above the back row out over the front rows */
  float ry = yend + 4.5f;
  prof roof[4] = { { dend + 0.5f, ry + 0.6f, 0, -1 }, { TIER0 + 1, ry, 0, -1 }, { TIER0 + 1, ry + 0.5f, -1, 0 }, { dend + 0.5f, ry + 1.1f, 0, 1 } };
  t3_material *rm = phys(lin(0.92f, 0.93f, 0.96f), 0.4f, 0.6f);
  tex_apply(rm, TEX_PANEL, 1, 0.6f, 1);
  rm->side = T3_DOUBLE_SIDE;
  add_mesh(arena_sweep(roof, 4, 4, 4, 0, false), rm, true);
  /* floodlights under the roof's lip */
  prof lamp[2] = { { TIER0 + 1.3f, ry - 0.05f, 0, -1 }, { TIER0 + 2.1f, ry + 0.05f, 0, -1 } };
  SC.ring_y = ry - 0.2f;
  SC.ring_d = TIER0 + 1.7f;
  SC.flood = glow(1, 0.97f, 0.9f, theme == TH_NEON ? 9 : 2.5f);
  t3_retain(SC.flood);
  add_mesh(arena_sweep(lamp, 2, 1, 1, 0, false), SC.flood, false);
  /* the columns holding the roof */
  t3_material *steel = phys(lin(0.7f, 0.72f, 0.78f), 0.35f, 1);
  tex_apply(steel, TEX_METAL, 1, 0.4f, 1);
  steel->roughness = 0.35f;
  int ncol = 0;
  for (int i = 0; i < nps; i += 24) ncol++;
  t3_instanced_mesh *cols = t3_instanced_mesh_new(t3_cylinder_geometry_new(0.35f, 0.4f, 1, 12, 1, false), steel, ncol);
  t3_release(cols->mesh.geometry);
  t3_release(steel);
  int k = 0;
  uint8_t *col_part = malloc((size_t)ncol + 1);
  for (int i = 0; i < nps && k < ncol; i += 24, k++) {
    const psample *p = &PS[i];
    col_part[k] = (uint8_t)(i * CROWD_CHUNKS / nps); /* culled by section of the stands, as the seats */
    t3_mat4 m;
    t3_mat4_compose(&m, (t3_vec3){ p->x + p->nx * (dend + 0.8f), (ry + 1) * 0.5f, p->z + p->nz * (dend + 0.8f) }, (t3_quat){ 0, 0, 0, 1 },
                    (t3_vec3){ 1, ry + 1, 1 });
    t3_instanced_mesh_set_matrix_at(cols, k, &m);
  }
  cols->mesh.base.cast_shadow = cols->mesh.base.receive_shadow = true;
  hang_parts(cols, k, col_part, CROWD_CHUNKS);
  free(col_part);

  /* seats along every row, one every 0.62 m of the row's own length, and a
   * fan in most of them */
  const int max_seats = 12000;
  t3_material *sm = phys(lin(1, 1, 1), 0.35f, 0);
  tex_apply(sm, TEX_SEAT, 1, 0.6f, 1);
  sm->clearcoat = 0.5f;
  sm->clearcoat_roughness = 0.2f;
  t3_geometry *seat = geo_rounded_box(0.5f, 0.42f, 0.42f, 0.06f, 1);
  /* staged in one mesh, then split by section below (as the crowd) */
  t3_instanced_mesh *seats = t3_instanced_mesh_new(seat, sm, max_seats);
  uint8_t *seat_ch = malloc((size_t)max_seats);
  /* the fans, low poly (thousands of them, seen from afar) but in a dog's
   * shape: a jersey with shoulders and a collar, sitting thighs, a dog's head
   * with a muzzle, a dark nose and floppy ears; the arms (and a striped scarf
   * for some) are their own instances so they can wave and hold it up */
  mb bb = { 0 };
  mb_blob(&bb, (t3_vec3){ 0, 0.3f, 0 }, (t3_vec3){ 0.18f, 0.22f, 0.14f }, 8, 5, 1);     /* the chest */
  mb_blob(&bb, (t3_vec3){ 0, 0.47f, 0 }, (t3_vec3){ 0.24f, 0.08f, 0.14f }, 8, 4, 1);    /* shoulders */
  mb_blob(&bb, (t3_vec3){ 0, 0.56f, 0.01f }, (t3_vec3){ 0.1f, 0.04f, 0.09f }, 6, 3, 1);  /* the collar */
  for (int sd = -1; sd <= 1; sd += 2)
    mb_blob(&bb, (t3_vec3){ sd * 0.1f, 0.1f, 0.14f }, (t3_vec3){ 0.08f, 0.07f, 0.17f }, 6, 3, 0.75f); /* thighs */
  mb hb = { 0 };
  mb_blob(&hb, (t3_vec3){ 0, 0.74f, 0 }, (t3_vec3){ 0.15f, 0.14f, 0.14f }, 8, 5, 1);
  mb_blob(&hb, (t3_vec3){ 0, 0.69f, 0.13f }, (t3_vec3){ 0.075f, 0.06f, 0.08f }, 6, 3, 0.8f); /* the muzzle */
  mb_blob(&hb, (t3_vec3){ 0, 0.72f, 0.21f }, (t3_vec3){ 0.03f, 0.022f, 0.02f }, 4, 2, 0.08f); /* the nose */
  for (int sd = -1; sd <= 1; sd += 2) {
    mb_blob(&hb, (t3_vec3){ sd * 0.14f, 0.7f, 0 }, (t3_vec3){ 0.04f, 0.1f, 0.06f }, 4, 3, 0.55f); /* floppy ears */
    mb_blob(&hb, (t3_vec3){ sd * 0.055f, 0.77f, 0.125f }, (t3_vec3){ 0.022f, 0.022f, 0.012f }, 4, 2, 0.05f); /* eyes */
  }
  /* the arms hang from the shoulder line (the pivot at the origin) */
  mb ab = { 0 };
  for (int sd = -1; sd <= 1; sd += 2) {
    mb_blob(&ab, (t3_vec3){ sd * 0.21f, -0.19f, 0 }, (t3_vec3){ 0.06f, 0.21f, 0.06f }, 5, 3, 1);
    mb_blob(&ab, (t3_vec3){ sd * 0.21f, -0.41f, 0 }, (t3_vec3){ 0.055f, 0.055f, 0.055f }, 5, 3, 0.7f); /* paws */
  }
  /* the scarf, between the paws: a striped strip, both windings */
  mb sb = { 0 };
  for (int k = 0; k < 8; k++) {
    float x0 = -0.25f + k * 0.0625f, x1 = x0 + 0.0625f, sh = k & 1 ? 0.3f : 1;
    for (int f = 0; f < 2; f++) {
      float nz = f ? -1.0f : 1.0f;
      int a = mb_v(&sb, (t3_vec3){ x0, -0.36f, 0.04f }, (t3_vec3){ 0, 0, nz }, sh), b2 = mb_v(&sb, (t3_vec3){ x1, -0.36f, 0.04f }, (t3_vec3){ 0, 0, nz }, sh);
      int c2 = mb_v(&sb, (t3_vec3){ x1, -0.48f, 0.04f }, (t3_vec3){ 0, 0, nz }, sh), d = mb_v(&sb, (t3_vec3){ x0, -0.48f, 0.04f }, (t3_vec3){ 0, 0, nz }, sh);
      if (f) mb_t(&sb, a, c2, b2), mb_t(&sb, a, d, c2);
      else mb_t(&sb, a, b2, c2), mb_t(&sb, a, c2, d);
    }
  }
  for (int sd = -1; sd <= 1; sd += 2) /* the tails hanging from the paws */
    for (int f = 0; f < 2; f++) {
      float nz = f ? -1.0f : 1.0f, x = sd * 0.25f;
      int a = mb_v(&sb, (t3_vec3){ x - 0.03f, -0.42f, 0.045f }, (t3_vec3){ 0, 0, nz }, 1), b2 = mb_v(&sb, (t3_vec3){ x + 0.03f, -0.42f, 0.045f }, (t3_vec3){ 0, 0, nz }, 1);
      int c2 = mb_v(&sb, (t3_vec3){ x + 0.03f, -0.66f, 0.045f }, (t3_vec3){ 0, 0, nz }, 0.3f), d = mb_v(&sb, (t3_vec3){ x - 0.03f, -0.66f, 0.045f }, (t3_vec3){ 0, 0, nz }, 0.3f);
      if (f) mb_t(&sb, a, c2, b2), mb_t(&sb, a, d, c2);
      else mb_t(&sb, a, b2, c2), mb_t(&sb, a, c2, d);
    }
  t3_material *jm = phys(lin(1, 1, 1), 0.7f, 0);
  jm->sheen = 0.5f;
  jm->sheen_color = lin(1, 1, 1);
  t3_material *fm = phys(lin(1, 1, 1), 0.8f, 0);
  fm->vertex_colors = true;
  fm->sheen = 1;
  fm->sheen_color = lin(1, 0.95f, 0.85f);
  fm->sheen_roughness = 0.5f;
  t3_geometry *fgeo[4] = { mb_done(&bb, true), mb_done(&hb, true), mb_done(&ab, true), mb_done(&sb, true) };
  /* the light fan: the same shapes in a third of the triangles (no
   * collar, nose, eyes or paws; coarser blobs) */
  mb lb = { 0 }, lh = { 0 }, la = { 0 };
  mb_blob(&lb, (t3_vec3){ 0, 0.3f, 0 }, (t3_vec3){ 0.18f, 0.22f, 0.14f }, 6, 3, 1);
  mb_blob(&lb, (t3_vec3){ 0, 0.47f, 0 }, (t3_vec3){ 0.24f, 0.08f, 0.14f }, 6, 2, 1);
  for (int sd = -1; sd <= 1; sd += 2) mb_blob(&lb, (t3_vec3){ sd * 0.1f, 0.1f, 0.14f }, (t3_vec3){ 0.08f, 0.07f, 0.17f }, 4, 2, 0.75f);
  mb_blob(&lh, (t3_vec3){ 0, 0.74f, 0 }, (t3_vec3){ 0.15f, 0.14f, 0.14f }, 6, 3, 1);
  mb_blob(&lh, (t3_vec3){ 0, 0.69f, 0.13f }, (t3_vec3){ 0.075f, 0.06f, 0.08f }, 4, 2, 0.8f);
  for (int sd = -1; sd <= 1; sd += 2) mb_blob(&lh, (t3_vec3){ sd * 0.14f, 0.7f, 0 }, (t3_vec3){ 0.04f, 0.1f, 0.06f }, 3, 2, 0.55f);
  for (int sd = -1; sd <= 1; sd += 2) mb_blob(&la, (t3_vec3){ sd * 0.21f, -0.22f, 0 }, (t3_vec3){ 0.06f, 0.25f, 0.06f }, 4, 2, 1);
  SC.fgeo[1][0] = mb_done(&lb, true), SC.fgeo[1][1] = mb_done(&lh, true), SC.fgeo[1][2] = mb_done(&la, true);
  SC.fgeo[1][3] = t3_retain(fgeo[3]); /* the scarf is a strip already */
  for (int q = 0; q < 4; q++) SC.fgeo[0][q] = t3_retain(fgeo[q]);
  SC.flod = 0;
  t3_release(jm);
  /* the fans as the rows fill, then sorted into sections below */
  float *fx = malloc(sizeof(float) * MAX_FANS), *fy = malloc(sizeof(float) * MAX_FANS), *fz = malloc(sizeof(float) * MAX_FANS);
  float *fyaw = malloc(sizeof(float) * MAX_FANS), *fph = malloc(sizeof(float) * MAX_FANS);
  uint8_t *fteam = malloc(MAX_FANS), *fkind = malloc(MAX_FANS), *fch = malloc(MAX_FANS);
  t3_color *cbody = malloc(sizeof(t3_color) * MAX_FANS), *chead = malloc(sizeof(t3_color) * MAX_FANS), *cscarf = malloc(sizeof(t3_color) * MAX_FANS);
  SC.cheer_team = -1;
  static const float COATS[6][3] = { { 0.86f, 0.71f, 0.5f }, { 0.15f, 0.13f, 0.12f }, { 0.92f, 0.62f, 0.36f },
                                     { 0.95f, 0.93f, 0.9f }, { 0.55f, 0.36f, 0.22f }, { 0.74f, 0.74f, 0.76f } };
  int ns = 0, nf = 0, slot = 0;
  for (int r = 0; r < ROWS; r++) {
    float d = TIER0 + r * ROW_D + ROW_D * 0.45f, y = TIER_Y + r * ROW_H;
    float acc = 0;
    for (int i = 1; i < nps && ns < max_seats; i++) {
      const psample *a = &PS[i - 1], *b2 = &PS[i];
      float ax = a->x + a->nx * d, az = a->z + a->nz * d, bx = b2->x + b2->nx * d, bz = b2->z + b2->nz * d;
      acc += sqrtf((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
      if (acc < 0.62f) continue;
      acc -= 0.62f;
      if ((slot++ % 24) == 0) continue; /* the aisles */
      float yaw = cw_atan2(-b2->nx, -b2->nz);
      t3_mat4 m;
      t3_mat4_compose(&m, (t3_vec3){ bx, y + 0.21f, bz }, yawq(yaw), (t3_vec3){ 1, 1, 1 });
      t3_instanced_mesh_set_matrix_at(seats, ns, &m);
      /* seat colours in big blocks: the ends in the team colours */
      int block = (i / 40) & 1;
      int end = bz < -AL * 0.45f ? 0 : bz > AL * 0.45f ? 1 : 2;
      float bk = block ? 0.72f : 1;
      t3_color sc = end < 2 ? lin(TEAM_RGB[end][0] * bk, TEAM_RGB[end][1] * bk, TEAM_RGB[end][2] * bk)
                            : block ? lin(0.95f, 0.8f, 0.25f) : lin(0.92f, 0.92f, 0.95f);
      t3_instanced_mesh_set_color_at(seats, ns, sc);
      seat_ch[ns] = (uint8_t)(i * CROWD_CHUNKS / nps);
      ns++;
      float fill = rnd();
      if (fill < 0.5f && nf < MAX_FANS) {
        fx[nf] = bx, fy[nf] = y + 0.1f, fz[nf] = bz, fyaw[nf] = yaw;
        float ph = rr(0, 6.2831853f);
        fph[nf] = ph;
        int coat = (int)(rnd() * 5.99f);
        float side = rnd();
        int team = end < 2 ? end : (side < 0.5f ? 0 : 1);
        float v = rr(0.85f, 1.1f);
        float kind = rnd();
        fteam[nf] = (uint8_t)team;
        fkind[nf] = kind < 0.35f ? 0 : kind < 0.7f ? 1 : 2;
        fch[nf] = (uint8_t)(i * CROWD_CHUNKS / nps);
        cbody[nf] = lin(TEAM_RGB[team][0] * v, TEAM_RGB[team][1] * v, TEAM_RGB[team][2] * v);
        /* scarves in the team colour with white or dark bars */
        float wb = rnd();
        cscarf[nf] = wb < 0.5f ? lin(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2])
                               : lin(0.5f + TEAM_RGB[team][0] * 0.5f, 0.5f + TEAM_RGB[team][1] * 0.5f, 0.5f + TEAM_RGB[team][2] * 0.5f);
        chead[nf] = lin(COATS[coat][0], COATS[coat][1], COATS[coat][2]);
        nf++;
      }
    }
  }
  /* one mesh of seats per section of the stands, culled with its section */
  seats->mesh.base.receive_shadow = true;
  hang_parts(seats, ns, seat_ch, CROWD_CHUNKS);
  free(seat_ch);
  t3_release(seat);
  t3_release(sm);
  SC.nfans = nf;
  SC.fan_x = malloc(sizeof(float) * (nf + 1));
  SC.fan_y = malloc(sizeof(float) * (nf + 1));
  SC.fan_z = malloc(sizeof(float) * (nf + 1));
  SC.fan_yaw = malloc(sizeof(float) * (nf + 1));
  SC.fan_phase = malloc(sizeof(float) * (nf + 1));
  SC.fan_team = malloc(nf + 1);
  SC.fan_kind = malloc(nf + 1);
  /* each section: its fans in row order (the front rows first, so a lower
   * quality drops the back rows), a mesh set placed at its middle and a
   * bounding sphere round all of it, so the frustum test sees the section */
  SC.nchunks = CROWD_CHUNKS;
  int kk = 0;
  for (int c = 0; c < CROWD_CHUNKS; c++) {
    struct crowd_chunk *C = &SC.ch[c];
    C->first = kk;
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    for (int f = 0; f < nf; f++) {
      if (fch[f] != c) continue;
      SC.fan_x[kk] = fx[f], SC.fan_y[kk] = fy[f], SC.fan_z[kk] = fz[f], SC.fan_yaw[kk] = fyaw[f], SC.fan_phase[kk] = fph[f];
      SC.fan_team[kk] = fteam[f], SC.fan_kind[kk] = fkind[f];
      float p[3] = { fx[f], fy[f], fz[f] };
      for (int q = 0; q < 3; q++) lo[q] = fminf(lo[q], p[q]), hi[q] = fmaxf(hi[q], p[q]);
      kk++;
    }
    C->n = kk - C->first;
    if (!C->n) continue;
    C->c = (t3_vec3){ (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
    float rad = 0;
    for (int f = C->first; f < kk; f++) {
      float dx = SC.fan_x[f] - C->c.x, dy = SC.fan_y[f] - C->c.y, dz = SC.fan_z[f] - C->c.z;
      rad = fmaxf(rad, sqrtf(dx * dx + dy * dy + dz * dz));
    }
    rad += 1.6f; /* a fan, arms up, round its seat */
    for (int f = C->first; f < kk; f++) C->nscarf += SC.fan_kind[f] == 2;
    for (int q = 0; q < 4; q++) {
      C->im[q] = t3_instanced_mesh_new(fgeo[q], fm, q == 3 ? (C->nscarf ? C->nscarf : 1) : C->n);
      /* the section's sphere (round its middle, where the mesh sits), set once:
       * not recomputed as the fans bob */
      t3_instanced_mesh_set_bounding_sphere(C->im[q], &(t3_sphere){ { 0, 0, 0 }, rad });
      C->im[q]->count = q == 3 ? C->nscarf : C->n;
      C->im[q]->mesh.base.position = C->c;
      C->im[q]->mesh.base.receive_shadow = true;
      C->im[q]->mesh.base.frustum_culled = true;
      hang(&C->im[q]->mesh.base);
    }
    /* colours, in the sorted order */
    int sc_i = 0;
    for (int f = 0, j = 0; f < nf; f++) {
      if (fch[f] != c) continue;
      t3_instanced_mesh_set_color_at(C->im[0], j, cbody[f]);
      t3_instanced_mesh_set_color_at(C->im[1], j, chead[f]);
      t3_instanced_mesh_set_color_at(C->im[2], j, cbody[f]);
      if (fkind[f] == 2) t3_instanced_mesh_set_color_at(C->im[3], sc_i++, cscarf[f]);
      j++;
    }
    C->shown = C->n;
    C->shown_scarf = C->nscarf;
  }
  for (int q = 0; q < 4; q++) t3_release(fgeo[q]);
  t3_release(fm);
  free(fx), free(fy), free(fz), free(fyaw), free(fph), free(fteam), free(fkind), free(fch), free(cbody), free(chead), free(cscarf);
}

/* the crowd: they bob and sway; some clap and wave, some hold a scarf up and
 * swing it. When a side scores its fans leap with their arms up and their
 * scarves whirling; the other side's sag and sit. cheer = scoring team + 1.
 * One job per section of the stands, each writing only its own instances. */
static void crowd_range(int begin, int end, int worker, void *ctx) {
  (void)worker, (void)ctx;
  float t = SC.t, ex = SC.ex;
  for (int c = begin; c < end; c++) {
    struct crowd_chunk *C = &SC.ch[c];
    if (!C->shown) continue;
    float *eb = (float *)C->im[0]->instance_matrix->array, *eh = (float *)C->im[1]->instance_matrix->array;
    float *ea = (float *)C->im[2]->instance_matrix->array, *es = (float *)C->im[3]->instance_matrix->array;
    for (int i = C->first; i < C->first + C->shown; i++, eb += 16, eh += 16, ea += 16) {
      float ph = SC.fan_phase[i];
      int kind = SC.fan_kind[i];
      float joy = SC.cheer_team < 0 ? ex * 0.5f : SC.fan_team[i] == SC.cheer_team ? ex : -ex * 0.6f;
      float jump = joy > 0 ? joy * 0.5f * fabsf(cw_sin(t * 6.5f + ph)) : 0;
      float bob = 0.03f * cw_sin(t * 2.2f + ph) + jump + (joy < 0 ? joy * 0.05f : 0);
      float sway = 0.06f * cw_sin(t * 0.9f + ph * 2);
      float s, co;
      cw_sincos(SC.fan_yaw[i] + sway, &s, &co);
      float x = SC.fan_x[i] - C->c.x, y = SC.fan_y[i] + bob - C->c.y, z = SC.fan_z[i] - C->c.z;
      float m[16] = { co, 0, -s, 0, 0, 1, 0, 0, s, 0, co, 0, x, y, z, 1 };
      memcpy(eb, m, sizeof m);
      memcpy(eh, m, sizeof m);
      /* the arms swing forward and up from the shoulders: 0 hanging, pi overhead */
      float a;
      if (kind == 2) a = 2.55f + 0.25f * cw_sin(t * (1.6f + joy * 6) + ph);
      else if (kind == 1) a = 0.7f + 0.5f * (0.5f + 0.5f * cw_sin(t * 4 + ph)) * (0.3f + 0.7f * fabsf(cw_sin(t * 0.37f + ph * 3)));
      else a = 0.35f;
      if (joy > 0) a += (3.0f - a) * fminf(1, joy * 1.4f);
      else if (joy < 0) a *= 1 + joy * 0.7f;
      float sa, ca;
      cw_sincos(-a, &sa, &ca);
      float ma[16] = { co, 0, -s, 0, s * sa, ca, co * sa, 0, s * ca, -sa, co * ca, 0, x, y + 0.5f, z, 1 };
      memcpy(ea, ma, sizeof ma);
      if (kind == 2) memcpy(es, ma, sizeof ma), es += 16;
    }
    for (int q = 0; q < 4; q++) C->im[q]->instance_matrix->version++;
  }
}
static void crowd_tick(int ticks, int cheer) {
  if (!SC.nchunks) return;
  if (cheer) {
    SC.excite = fminf(1, SC.excite + 0.08f);
    SC.cheer_team = cheer - 1;
  } else SC.excite *= 0.992f;
  SC.t = ticks / 60.0f;
  SC.ex = SC.excite;
  cw_parallel_for(SC.nchunks, 1, crowd_range, NULL);
}

/* ── outside ────────────────────────────────────────────────────────── */

static float outer(void) { return TIER0 + ROWS * ROW_D + 3; }

/* a spot outside the stands, inside radius r */
static t3_vec3 spot(float r) {
  for (int tries = 0; tries < 64; tries++) {
    float x = rr(-r, r);
    float z = rr(-r, r);
    float o = outer() + 4;
    if (fabsf(x) < AW + o && fabsf(z) < AL + o) continue;
    return (t3_vec3){ x, 0, z };
  }
  return (t3_vec3){ r, 0, r };
}

static void ground(int tex, float r, float g, float b, float rough, float rep) {
  t3_material *m = stdm(lin(r, g, b), rough, 0);
  tex_apply(m, tex, rep, 1, 1);
  t3_geometry *pg = t3_plane_geometry_new(600, 600, 1, 1);
  t3_mesh *me = t3_mesh_new(pg, m);
  t3_object_set_rotation(&me->base, -1.5707963f, 0, 0);
  me->base.position = (t3_vec3){ 0, -0.05f, 0 };
  me->base.receive_shadow = true;
  hang(&me->base);
  t3_release(me);
  t3_release(pg);
  t3_release(m);
}

/* the trees round the stadium: a few hundred small meshes, merged by sector
 * of the ring and by material (tb_add, then tb_flush): a view draws the few
 * sectors it faces instead of every trunk and every ball of leaves */
#define TREE_SECTORS 8
#define TREE_MATS 4
typedef struct tbatch { t3_material *m; float *pos, *nor, *uv; uint32_t *idx; int nv, ni, cv, ci; } tbatch;
static tbatch TB[TREE_SECTORS][TREE_MATS];
static void tb_put(tbatch *b, const t3_geometry *g, const t3_mat4 *m) {
  const t3_attribute *pa = g->attributes[T3_ATTR_POSITION], *na = g->attributes[T3_ATTR_NORMAL], *ua = g->attributes[T3_ATTR_UV], *ia = g->index;
  if (!pa || !na || !ia) return;
  if (b->nv + pa->count > b->cv) {
    b->cv = (b->nv + pa->count) * 2;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->cv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->cv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cv);
  }
  if (b->ni + ia->count > b->ci) {
    b->ci = (b->ni + ia->count) * 2;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->ci);
  }
  const float *e = m->e;
  t3_mat3 nm;
  t3_mat3_normal_matrix(&nm, m);
  const float *q = nm.e, *P = pa->array, *N = na->array, *U = ua ? ua->array : NULL;
  for (int i = 0; i < pa->count; i++) {
    float x = P[i * 3], y = P[i * 3 + 1], z = P[i * 3 + 2], a = N[i * 3], bb = N[i * 3 + 1], c = N[i * 3 + 2];
    float *d = b->pos + (size_t)(b->nv + i) * 3, *dn = b->nor + (size_t)(b->nv + i) * 3, *du = b->uv + (size_t)(b->nv + i) * 2;
    d[0] = e[0] * x + e[4] * y + e[8] * z + e[12];
    d[1] = e[1] * x + e[5] * y + e[9] * z + e[13];
    d[2] = e[2] * x + e[6] * y + e[10] * z + e[14];
    float nx = q[0] * a + q[3] * bb + q[6] * c, ny = q[1] * a + q[4] * bb + q[7] * c, nz = q[2] * a + q[5] * bb + q[8] * c;
    float l = sqrtf(nx * nx + ny * ny + nz * nz);
    if (l > 0) nx /= l, ny /= l, nz /= l;
    dn[0] = nx, dn[1] = ny, dn[2] = nz;
    du[0] = U ? U[i * 2] : 0, du[1] = U ? U[i * 2 + 1] : 0;
  }
  for (int i = 0; i < ia->count; i++)
    b->idx[b->ni + i] = (uint32_t)b->nv + (ia->type == T3_UINT16 ? ((const uint16_t *)ia->array)[i] : ((const uint32_t *)ia->array)[i]);
  b->nv += pa->count;
  b->ni += ia->count;
}
/* a tree (a group of meshes, posed) into its sector's batches; the group is the caller's */
static void tb_add(t3_object *g) {
  float a = cw_atan2(g->position.z, g->position.x);
  int sec = (int)((a + 3.1415927f) / 6.2831853f * TREE_SECTORS) % TREE_SECTORS;
  t3_object_update_matrix(g);
  for (int i = 0; i < g->child_count; i++) {
    t3_object *o = g->children[i];
    if (o->type != T3_MESH) continue;
    t3_mesh *me = (t3_mesh *)o;
    tbatch *b = NULL;
    for (int k = 0; k < TREE_MATS && !b; k++)
      if (!TB[sec][k].m || TB[sec][k].m == me->materials[0]) b = &TB[sec][k];
    if (!b) continue;
    if (!b->m) b->m = t3_retain(me->materials[0]);
    t3_object_update_matrix(o);
    t3_mat4 w;
    t3_mat4_multiply(&w, &g->matrix, &o->matrix);
    tb_put(b, me->geometry, &w);
  }
}
static void tb_flush(void) {
  for (int s = 0; s < TREE_SECTORS; s++)
    for (int k = 0; k < TREE_MATS; k++) {
      tbatch *b = &TB[s][k];
      if (b->m && b->nv) {
        t3_geometry *g = t3_geometry_new();
        float *data[3] = { b->pos, b->nor, b->uv };
        static const int slot[3] = { T3_ATTR_POSITION, T3_ATTR_NORMAL, T3_ATTR_UV }, size[3] = { 3, 3, 2 };
        for (int q = 0; q < 3; q++) {
          t3_attribute *at = t3_attribute_new(T3_FLOAT32, data[q], b->nv, size[q]);
          t3_geometry_set_attribute(g, slot[q], at);
          t3_release(at);
        }
        t3_attribute *ia = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
        t3_geometry_set_index(g, ia);
        t3_release(ia);
        t3_mesh *me = t3_mesh_new(g, b->m);
        me->base.cast_shadow = me->base.receive_shadow = true;
        hang(&me->base);
        t3_release(me);
        t3_release(g);
      }
      if (b->m) t3_release(b->m);
      free(b->pos), free(b->nor), free(b->uv), free(b->idx);
      memset(b, 0, sizeof *b);
    }
}

static void tree(t3_vec3 p, float s, t3_material *bark, t3_material *leaves) {
  t3_object *g = t3_group_new();
  t3_geometry *trunk = t3_cylinder_geometry_new(0.25f, 0.4f, 3.2f, 10, 1, false);
  mesh_at(g, trunk, bark, (t3_vec3){ 0, 1.6f, 0 }, (t3_vec3){ 1, 1, 1 });
  t3_geometry *ball = t3_icosahedron_geometry_new(1, 2);
  for (int k = 0; k < 5; k++) {
    float a = k * 1.7f + rnd(), r = k ? 1.2f : 0;
    float sz = k ? rr(1.3f, 1.8f) : 2.2f;
    float yy = k ? rr(-0.4f, 0.6f) : 0.4f;
    mesh_at(g, ball, leaves, (t3_vec3){ r * cw_cos(a), 4.2f + yy, r * cw_sin(a) }, (t3_vec3){ sz, sz * 0.85f, sz });
  }
  t3_release(trunk);
  t3_release(ball);
  g->position = p;
  g->scale = (t3_vec3){ s, s, s };
  tb_add(g);
  t3_release(g);
}

static void palm(t3_vec3 p, float s, t3_material *bark, t3_material *frond) {
  t3_object *g = t3_group_new();
  t3_geometry *seg = t3_cylinder_geometry_new(0.22f, 0.28f, 1.2f, 10, 1, false);
  float lean = rr(-0.25f, 0.25f), x = 0;
  for (int k = 0; k < 7; k++) {
    t3_mesh *m = mesh_at(g, seg, bark, (t3_vec3){ x, 0.6f + k * 1.1f, 0 }, (t3_vec3){ 1 - k * 0.06f, 1, 1 - k * 0.06f });
    t3_object_set_rotation(&m->base, 0, 0, lean * 0.3f);
    x += lean * 0.35f;
  }
  t3_geometry *leaf = t3_sphere_geometry_new(1, 12, 6);
  for (int k = 0; k < 8; k++) {
    float a = k * 0.785f + rnd() * 0.3f;
    t3_mesh *m = mesh_at(g, leaf, frond, (t3_vec3){ x + 1.4f * cw_cos(a), 7.8f, 1.4f * cw_sin(a) }, (t3_vec3){ 1.8f, 0.08f, 0.45f });
    t3_object_set_quaternion(&m->base, t3_quat_from_euler((t3_euler){ 0, -a, -0.45f, T3_YXZ }));
  }
  t3_release(seg);
  t3_release(leaf);
  g->position = p;
  g->scale = (t3_vec3){ s, s, s };
  tb_add(g);
  t3_release(g);
}

static void pine(t3_vec3 p, float s, t3_material *needles, t3_material *snow, t3_material *bark) {
  t3_object *g = t3_group_new();
  t3_geometry *trunk = t3_cylinder_geometry_new(0.25f, 0.35f, 2, 8, 1, false);
  mesh_at(g, trunk, bark, (t3_vec3){ 0, 1, 0 }, (t3_vec3){ 1, 1, 1 });
  for (int k = 0; k < 4; k++) {
    float r = 2.4f - k * 0.5f, y = 1.6f + k * 1.5f;
    t3_geometry *cone = t3_cone_geometry_new(r, 2.6f, 16, 1, false);
    mesh_at(g, cone, needles, (t3_vec3){ 0, y + 1.3f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_geometry *cap = t3_cone_geometry_new(r * 0.78f, 1.3f, 16, 1, false);
    mesh_at(g, cap, snow, (t3_vec3){ 0, y + 2.0f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_release(cone);
    t3_release(cap);
  }
  t3_release(trunk);
  g->position = p;
  g->scale = (t3_vec3){ s, s, s };
  tb_add(g);
  t3_release(g);
}

static cw_node weather(const char *const *kv, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", "Weather");
  cw_node_add_child(SC.root, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) {
      cw_node_set(n, kv[i], cw_val_enum(kv[i + 1]));
      continue;
    }
    if (p && cw_value_parse(kv[i + 1], p->type, &v)) cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node3d_set_position(n, (t3_vec3){ 0, 12, 0 });
  return n;
}

static const char *const SNOWFALL[] = { "amount", "2400", "lifetime", "9", "emission_shape", "BOX", "emission_extents", "vec3(40, 0.5, 48)",
  "direction", "vec3(0, -1, 0)", "spread", "15", "speed_min", "0.8", "speed_max", "1.6", "gravity", "vec3(0.4, -0.5, 0)",
  "size", "0.1", "size_random", "0.5", "color_ramp", "\"0 1 1 1 0, 0.1 1 1 1 0.9, 0.9 1 1 1 0.9, 1 1 1 1 0\"", "energy", "1.4",
  "turbulence", "0.6", "turbulence_scale", "0.2", "preprocess", "9", "blend", "ALPHA", NULL };
static const char *const PETALS[] = { "amount", "500", "lifetime", "10", "emission_shape", "BOX", "emission_extents", "vec3(40, 4, 48)",
  "direction", "vec3(1, -0.2, 0.3)", "spread", "30", "speed_min", "0.6", "speed_max", "1.6", "gravity", "vec3(0.5, -0.25, 0.1)",
  "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.07", "size_random", "0.4", "spin", "3", "turbulence", "1.2",
  "turbulence_scale", "0.2", "preprocess", "10", "blend", "OPAQUE", "color_ramp", "\"0 1 0.6 0.72 1, 1 1 0.75 0.85 1\"", NULL };
static const char *const MOTES[] = { "amount", "300", "lifetime", "8", "emission_shape", "BOX", "emission_extents", "vec3(38, 5, 46)",
  "direction", "vec3(1, 0.1, 0.2)", "spread", "40", "speed_min", "0.2", "speed_max", "0.6", "gravity", "vec3(0.1, 0.02, 0)",
  "size", "0.05", "size_random", "0.5", "color_ramp", "\"0 1 0.95 0.8 0, 0.2 1 0.95 0.8 0.6, 0.8 1 0.95 0.8 0.6, 1 1 0.95 0.8 0\"",
  "energy", "2.5", "blend", "ADD", "turbulence", "0.8", "turbulence_scale", "0.2", "preprocess", "8", NULL };
static const char *const FIREFLIES[] = { "amount", "260", "lifetime", "6", "emission_shape", "BOX", "emission_extents", "vec3(40, 3, 48)",
  "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.1", "speed_max", "0.4", "gravity", "vec3(0, 0.03, 0)",
  "size", "0.06", "color_ramp", "\"0 0.4 1 0.9 0, 0.3 0.6 1 0.95 1, 0.7 0.9 0.4 1 1, 1 0.4 1 0.9 0\"", "energy", "6", "blend", "ADD",
  "turbulence", "1", "turbulence_scale", "0.3", "preprocess", "6", NULL };

static void build_outside(int theme) {
  const float R = 150;
  t3_material *bark = phys(lin(1, 1, 1), 0.9f, 0);
  tex_apply(bark, TEX_BARK, 1, 1, 1);
  switch (theme) {
  case TH_PARK:
  case TH_GARDEN: {
    ground(TEX_GRASS, 0.85f, 1, 0.8f, 1, 80);
    t3_material *leaves = phys(lin(theme == TH_GARDEN ? 0.4f : 0.5f, theme == TH_GARDEN ? 0.7f : 0.8f, 0.32f), 0.75f, 0);
    tex_apply(leaves, TEX_LEAF, 2, 1, 1);
    leaves->sheen = 0.5f;
    leaves->sheen_color = lin(0.7f, 1, 0.5f);
    for (int i = 0; i < 40; i++) {
      t3_vec3 at = spot(R);
      float sc = rr(1.2f, 2.2f);
      tree(at, sc, bark, leaves);
    }
    if (theme == TH_GARDEN) {
      /* a clipped hedge round the stadium and roses along it */
      t3_material *hedge = phys(lin(0.5f, 0.8f, 0.42f), 0.75f, 0);
      tex_apply(hedge, TEX_HEDGE, 3, 1.2f, 1);
      float o = outer() + 6;
      prof h[4] = { { o, 0, -1, 0 }, { o, 2.4f, -1, 0 }, { o, 2.4f, 0, 1 }, { o + 1.6f, 2.4f, 0, 1 } };
      add_mesh(arena_sweep(h, 4, 3, 3, 0, false), hedge, true);
      t3_material *rose = phys(lin(1, 1, 1), 0.5f, 0);
      rose->sheen = 1;
      rose->sheen_color = lin(1, 0.6f, 0.7f);
      t3_instanced_mesh *roses = t3_instanced_mesh_new(t3_icosahedron_geometry_new(0.22f, 1), rose, 600);
      t3_release(roses->mesh.geometry);
      t3_release(rose);
      uint8_t rose_part[600];
      for (int i = 0; i < 600; i++) {
        int kk = (int)(rnd() * (float)(nps - 1));
        rose_part[i] = (uint8_t)(kk * CROWD_CHUNKS / nps);
        float up = rr(0.4f, 2.5f);
        float off = rr(-0.1f, 0.05f);
        float sz = rr(0.7f, 1.3f);
        float hue = rnd();
        const psample *p = &PS[kk];
        t3_mat4 m;
        t3_mat4_compose(&m, (t3_vec3){ p->x + p->nx * (o + off), up, p->z + p->nz * (o + off) }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ sz, sz, sz });
        t3_instanced_mesh_set_matrix_at(roses, i, &m);
        t3_instanced_mesh_set_color_at(roses, i, hue < 0.5f ? lin(1, 0.2f, 0.35f) : hue < 0.8f ? lin(1, 0.75f, 0.85f) : lin(1, 0.95f, 0.85f));
      }
      roses->mesh.base.cast_shadow = roses->mesh.base.receive_shadow = true;
      hang_parts(roses, 600, rose_part, CROWD_CHUNKS); /* by section round the hedge */
      weather(PETALS, 5);
    } else weather(MOTES, 6);
    t3_release(leaves);
    break;
  }
  case TH_BEACH: {
    ground(TEX_SAND, 1, 0.95f, 0.88f, 1, 80);
    /* the sea, beyond the red end */
    t3_material *water = phys(lin(0.1f, 0.45f, 0.55f), 0.06f, 0);
    tex_apply(water, TEX_ASPHALT, 30, 0.35f, 0);
    water->clearcoat = 1;
    water->clearcoat_roughness = 0.05f;
    water->roughness = 0.08f;
    t3_geometry *sea = t3_plane_geometry_new(800, 400, 1, 1);
    t3_mesh *sm = t3_mesh_new(sea, water);
    t3_object_set_rotation(&sm->base, -1.5707963f, 0, 0);
    sm->base.position = (t3_vec3){ 0, 0.05f, AL + outer() + 230 };
    sm->base.receive_shadow = true;
    hang(&sm->base);
    t3_release(sm);
    t3_release(sea);
    t3_release(water);
    t3_material *frond = phys(lin(0.35f, 0.7f, 0.25f), 0.6f, 0);
    tex_apply(frond, TEX_LEAF, 2, 1, 1);
    frond->sheen = 0.6f;
    frond->sheen_color = lin(0.7f, 1, 0.5f);
    for (int i = 0; i < 30; i++) {
      t3_vec3 at = spot(R);
      float sc = rr(0.9f, 1.4f);
      if (at.z > AL + outer() + 20) continue;
      palm(at, sc, bark, frond);
    }
    t3_release(frond);
    weather(MOTES, 7);
    break;
  }
  case TH_SNOW: {
    ground(TEX_SNOW, 1, 1, 1, 0.75f, 60);
    t3_material *needles = phys(lin(0.2f, 0.42f, 0.28f), 0.8f, 0);
    tex_apply(needles, TEX_LEAF, 3, 1, 1);
    t3_material *snow = phys(lin(1, 1, 1), 0.7f, 0);
    tex_apply(snow, TEX_SNOW, 2, 1, 1);
    snow->sheen = 0.6f;
    snow->sheen_color = lin(0.8f, 0.9f, 1);
    for (int i = 0; i < 46; i++) {
      t3_vec3 at = spot(R);
      float sc = rr(1, 1.8f);
      pine(at, sc, needles, snow, bark);
    }
    t3_release(needles);
    t3_release(snow);
    weather(SNOWFALL, 8);
    break;
  }
  case TH_NEON: {
    ground(TEX_ASPHALT, 0.6f, 0.62f, 0.7f, 0.5f, 60);
    /* a city of towers with neon crowns round the stadium */
    t3_material *tw = phys(lin(0.25f, 0.27f, 0.35f), 0.35f, 0.4f);
    tex_apply(tw, TEX_PANEL, 1, 0.8f, 1);
    t3_instanced_mesh *towers = t3_instanced_mesh_new(t3_box_geometry_new(1, 1, 1, 1, 1, 1), tw, 70);
    t3_release(towers->mesh.geometry);
    t3_release(tw);
    t3_material *win = glow(1, 1, 1, 4);
    t3_instanced_mesh *lit = t3_instanced_mesh_new(t3_box_geometry_new(1, 1, 1, 1, 1, 1), win, 70);
    t3_release(lit->mesh.geometry);
    t3_release(win);
    static const float NEON[5][3] = { { 1, 0.2f, 0.7f }, { 0.2f, 0.9f, 1 }, { 0.7f, 0.3f, 1 }, { 1, 0.85f, 0.2f }, { 0.3f, 1, 0.5f } };
    uint8_t tower_part[70];
    for (int i = 0; i < 70; i++) {
      t3_vec3 at = spot(R);
      /* culled in eight sectors round the stadium */
      tower_part[i] = (uint8_t)CW_CLAMP((int)((cw_atan2(at.z, at.x) + 3.1415927f) / 6.2831853f * 8), 0, 7);
      float w = rr(8, 18);
      float d = rr(8, 18);
      float h = rr(20, 70);
      float hue = rnd();
      t3_mat4 m;
      t3_mat4_compose(&m, (t3_vec3){ at.x, h * 0.5f, at.z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ w, h, d });
      t3_instanced_mesh_set_matrix_at(towers, i, &m);
      t3_instanced_mesh_set_color_at(towers, i, (t3_color){ 1, 1, 1 });
      t3_mat4_compose(&m, (t3_vec3){ at.x, h - 2, at.z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ w + 0.3f, 1.2f, d + 0.3f });
      t3_instanced_mesh_set_matrix_at(lit, i, &m);
      const float *c = NEON[(int)(hue * 4.99f)];
      t3_instanced_mesh_set_color_at(lit, i, lin(c[0], c[1], c[2]));
    }
    t3_instanced_mesh *im[2] = { towers, lit };
    for (int q = 0; q < 2; q++) {
      im[q]->mesh.base.cast_shadow = q == 0;
      im[q]->mesh.base.receive_shadow = true;
      hang_parts(im[q], 70, tower_part, 8);
    }
    weather(FIREFLIES, 9);
    break;
  }
  default: break;
  }
  tb_flush();
  t3_release(bark);
}

/* ── lifecycle ──────────────────────────────────────────────────────── */

void scenery_reset(void) {
  if (SC.ready) {
    for (int l = 0; l < 2; l++)
      for (int q = 0; q < 4; q++)
        if (SC.fgeo[l][q]) t3_release(SC.fgeo[l][q]), SC.fgeo[l][q] = NULL;
    for (int c = 0; c < SC.nchunks; c++)
      for (int q = 0; q < 4; q++)
        if (SC.ch[c].im[q]) t3_release(SC.ch[c].im[q]);
    free(SC.fan_team);
    free(SC.fan_kind);
    for (int b = 0; b < GRASS_BANDS; b++)
      if (SC.gband[b].im) t3_release(SC.gband[b].im);
    free(SC.gband_of);
    if (SC.flood) t3_release(SC.flood);
    free(SC.fan_x);
    free(SC.fan_y);
    free(SC.fan_z);
    free(SC.fan_yaw);
    free(SC.fan_phase);
    free(SC.gx);
    free(SC.gz);
    free(SC.gyaw);
    free(SC.gs);
    free(SC.gsy);
  }
  memset(&SC, 0, sizeof SC);
}

void scenery_build(cw_node scene, int theme, uint32_t seed) {
  scenery_reset();
  SC.theme = theme;
  seed_state = seed * 2654435761u + 1u;
  scenery_hash = 2166136261u;
  SC.root = cw_node_new("Node3D", "Scenery");
  cw_node_add_child(scene, SC.root);
  plan_build();
  if (theme == TH_PARK || theme == TH_GARDEN) build_grass(1, 1, 1);
  else if (theme == TH_NEON) build_grass(0.8f, 1.05f, 0.85f);
  build_stands(theme);
  build_outside(theme);
  SC.ready = true;
}

/* night: the floodlight ring as real lights. Five lamps down each long side,
 * two at each end (clear of the goal) and one at each corner, every one a
 * spot light from under the roof's lip aimed into the pitch; with clustered
 * lighting each fragment loops only the lamps whose cones reach it. The
 * corner lamps (and on Ultra the middle of each side) cast shadows from the
 * shadow atlas, so a pug at night throws a shadow from each pylon. */
typedef struct flood_at { float x, z; uint8_t shadow_rank; } flood_at; /* rank: 1 the first two, 2 four, 3 six, 0 never */
static flood_at flood_place(int k) {
  float d = SC.ring_d, hx = AW - AC, hz = AL - AC;
  if (k < 10) { /* the long sides */
    float side = k < 5 ? 1 : -1, z = (float)(k % 5 - 2) * 10;
    return (flood_at){ side * (AW + d), z, (uint8_t)(k % 5 == 2 ? 3 : 0) };
  }
  if (k < 14) { /* the ends, either side of the goal */
    float end = k < 12 ? 1 : -1, x = (k & 1) ? 9.5f : -9.5f;
    return (flood_at){ x, end * (AL + d), 0 };
  }
  /* the corners: diagonal pairs first, so Low's two shadows sit across the pitch */
  static const float CX[4] = { 1, -1, -1, 1 }, CZ[4] = { 1, -1, 1, -1 };
  int c = k - 14;
  float r = AC + d, q = 0.70710678f;
  return (flood_at){ CX[c] * (hx + r * q), CZ[c] * (hz + r * q), (uint8_t)(c < 2 ? 1 : 2) };
}
void scenery_floodlights(float strength) {
  if (!SC.ready || strength <= 0) return;
  cw_node holder = cw_node_new("Node3D", "Floodlights");
  cw_node_add_child(SC.root, holder);
  SC.flood_energy = 300 * strength;
  for (int k = 0; k < FLOODS; k++) {
    flood_at f = flood_place(k);
    char name[16];
    snprintf(name, sizeof name, "Flood%d", k);
    cw_node n = cw_node_new("SpotLight3D", name);
    cw_node_add_child(holder, n);
    cw_node3d_set_position(n, (t3_vec3){ f.x, SC.ring_y, f.z });
    cw_node_set(n, "color", cw_val_color(1, 0.95f, 0.86f, 1));
    cw_node_set(n, "energy", cw_val_float(SC.flood_energy));
    cw_node_set(n, "range", cw_val_float(80));
    cw_node_set(n, "decay", cw_val_float(2));
    cw_node_set(n, "angle", cw_val_float(0.66f));
    cw_node_set(n, "penumbra", cw_val_float(0.5f));
    cw_node_set(n, "shadow_size", cw_val_int(1024));
    cw_node_set(n, "shadow_bias", cw_val_float(-0.0004f));
    cw_node_set(n, "shadow_normal_bias", cw_val_float(0.04f));
    cw_node3d_look_at(n, (t3_vec3){ f.x * 0.3f, 0, f.z * 0.3f }, (t3_vec3){ 0, 1, 0 });
    SC.flood_light[k] = n;
  }
  scenery_flood_quality();
}
/* the floodlights at the graphics settings: Shadows picks how many lamps
 * cast (Low and Medium two, High four, Ultra six); effects detail Low lights
 * every other lamp at twice the energy (half the lights a pixel loops, the
 * same light on the pitch) */
void scenery_flood_quality(void) {
  if (!SC.ready || !cw_node_valid(SC.flood_light[0])) return;
  static const int SHADOWS[4] = { 1, 1, 2, 3 };
  int rank = SHADOWS[CW_CLAMP(cw_gfx_get(CW_GFX_SHADOWS), 0, 3)];
  bool half = cw_gfx_get(CW_GFX_EFFECTS) == 0;
  for (int k = 0; k < FLOODS; k++) {
    flood_at f = flood_place(k);
    bool on = !half || (k & 1) == 0 || k >= 14; /* the corners stay */
    float e = !on ? 0 : half && k < 14 ? SC.flood_energy * 2 : SC.flood_energy;
    cw_node_set(SC.flood_light[k], "energy", cw_val_float(e));
    cw_node_set(SC.flood_light[k], "shadow", cw_val_bool(on && f.shadow_rank && f.shadow_rank <= rank));
  }
}

/* split screen, before each view draws: the grass thins out with distance
 * from its camera (eye). A view is a half or a quarter of the screen, so a
 * far blade is under a pixel there; the near ones stay as dense as the
 * quality allows. (The crowd stays whole: split screen draws its light fans.) */
static float view_share(float d, float near, float far, float least) {
  if (d <= near) return 1;
  if (d >= far) return least;
  return 1 - (1 - least) * (d - near) / (far - near);
}
void scenery_view_detail(t3_vec3 eye) {
  if (!SC.ready) return;
  for (int b = 0; b < GRASS_BANDS; b++) {
    t3_instanced_mesh *im = SC.gband[b].im;
    if (!im) continue;
    /* across the pitch the band is all near or all far: its distance along it */
    float dz = fabsf(SC.gband_c[b][1] - eye.z) - SC.gband_c[b][2];
    float d = dz > 0 ? dz : 0;
    im->count = (int)(SC.gband_shown[b] * view_share(d, 8, 30, 0.2f));
  }
}

/* the crowd's fans as built (light false) or the light ones */
void scenery_light_crowd(bool light) {
  int l = light ? 1 : 0;
  if (!SC.ready || SC.flod == l || !SC.fgeo[l][0]) return;
  SC.flod = l;
  for (int c = 0; c < SC.nchunks; c++)
    for (int q = 0; q < 4; q++) {
      t3_instanced_mesh *im = SC.ch[c].im[q];
      if (!im) continue;
      t3_geometry *old = im->mesh.geometry;
      im->mesh.geometry = t3_retain(SC.fgeo[l][q]);
      t3_release(old);
    }
}

/* the share of the crowd and grass drawn (cw_quality_density: 1 full) */
void scenery_quality(float k, float grass) {
  if (!SC.ready) return;
  k = k < 0.05f ? 0.05f : k > 1 ? 1 : k;
  grass = grass < 0.05f ? 0.05f : grass > 1 ? 1 : grass;
  for (int b = 0; b < GRASS_BANDS; b++)
    if (SC.gband[b].im) SC.gband[b].im->count = SC.gband_shown[b] = (int)(SC.gband[b].n * grass);
  for (int c = 0; c < SC.nchunks; c++) {
    struct crowd_chunk *C = &SC.ch[c];
    if (!C->n) continue;
    C->shown = (int)(C->n * k);
    C->shown_scarf = 0;
    for (int f = C->first; f < C->first + C->shown; f++) C->shown_scarf += SC.fan_kind[f] == 2;
    C->im[0]->count = C->im[1]->count = C->im[2]->count = C->shown;
    C->im[3]->count = C->shown_scarf;
  }
}

void scenery_tick(const sim *s, int ticks, int cheer) {
  if (!SC.ready) return;
  SC.ticks++;
  grass_tick(s, ticks / 60.0f, ticks & 1);
  if ((ticks & 1) == 0 || cheer) crowd_tick(ticks, cheer);
}
