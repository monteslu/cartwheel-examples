/* map: Salt Flats Run. A dry lakebed at dawn: blinding white-pink crust under a huge sky, a low sun that has just
 * risen (long shadows, warm rim light, cool blue shade), haze and a mirage on the horizon, mesas and a mountain range
 * in violet, rust and white. The road runs along a plateau (the first half, with a big salvaged-container jump), drops
 * down a steep escarpment through a forest of oil derricks and runs the pan below (a second jump past a stranded ship,
 * wide bends, a lone giant crane) before it climbs back to the start gantry and its scrap-built grandstands.
 *
 * The course (SPEC) is the coordinator's. Everything else here is the map's: ground, horizon, set pieces, animated
 * scenery and ambient dust. Models made here are small generators over the props.h builder (pm_*). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game.h"
#include "../props.h"

static const piece SPEC[] = {
  WALLS(1, 1),
  ITEMS(),
  STRAIGHT(102),
  CHECK(),
  BANKED(-40, 102, -10),
  BANKED(40, 102, 10),
  STRAIGHT(30),
  BOOST(36),
  STRAIGHT(40),
  SLOPE(44, 20),
  SLOPE(26, 20),
  JUMP(0, 30),
  SLOPE(90, 0),
  ITEMS(),
  STRAIGHT(40),
  BANKED(90, 136, 14),
  STRAIGHT(50),
  ITEMS(),
  SLOPE(70, 7),
  SLOPE(70, -7),
  SLOPE(60, 0),
  BANKED(30, 75, 7),
  BANKED(-30, 75, -7),
  STRAIGHT(30),
  CHECK(),
  BANKED(90, 136, 14),
  SLOPE(70, -8),
  STRAIGHT(70),
  SLOPE(60, 0),
  STRAIGHT(20),
  BOOST(36),
  STRAIGHT(40),
  SLOPE(40, 18),
  SLOPE(20, 18),
  JUMP(0, 29),
  SLOPE(80, 0),
  ITEMS(),
  STRAIGHT(60),
  BANKED(-45, 95, -10),
  BANKED(45, 95, 10),
  CHECK(),
  ABANKED(90, 136, 14),
  ASTRAIGHT(122),
  ITEMS(),
  ABANKED(90, 136, 14),
  ASTRAIGHT(82),
  ASLOPE(60, -2),
  ASTRAIGHT(109),
  SLOPE(40, 0),
  STRAIGHT(60),
  END(),
};
#define MAP_SPEC (&(track_spec){ .name = "salt", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 11, .circuit = true })

/* ── small helpers ──────────────────────────────────────────────────────────────────────────────────── */

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V(a.x * k, a.y * k, a.z * k); }
static t3_vec3 vcross(t3_vec3 a, t3_vec3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float vdot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float sstep(float a, float b, float x) {
  float t = fminf(1, fmaxf(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
}
static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hashf_(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

/* the palette: bleached, sun-baked, a few loud paints so it stays comic */
#define C_RUST hex(0x9a4a2a)
#define C_RUST_D hex(0x5e2c1d)
#define C_HULL hex(0x8c3a2c)
#define C_STEEL hex(0x6b7078)
#define C_STEEL_L hex(0x9aa0a8)
#define C_BONE hex(0xe9e1d2)
#define C_SALT hex(0xf6e6df)
#define C_TEAL hex(0x23a0a4)
#define C_MUST hex(0xe5a823)
#define C_ORNG hex(0xea5d1c)
#define C_RED hex(0xc9302c)
#define C_PINK hex(0xe0587a)
#define C_BLUE hex(0x2c6fd0)
#define C_DARK hex(0x2a2523)

/* ── state ──────────────────────────────────────────────────────────────────────────────────────────── */

static bool SPLIT; /* two or more views: a thinner map (cells, rings, scatter, animated parts) */
static ground_opts GO;                       /* the pan: heights are asked of it by the props */
static const track *TR;
static float CX, CZ;                         /* the middle of the course */

static float ground_y(float x, float z) { return ground_height_at(&GO, x, z); }
static float ground_cb(float x, float z, void *ud) {
  (void)ud;
  return ground_y(x, z);
}
static t3_vec3 on_ground(float x, float z, float up) { return V(x, ground_y(x, z) + up, z); }
static float yaw_to(t3_vec3 from, t3_vec3 to) { return atan2f(to.x - from.x, to.z - from.z); }

static float ws(float s) {
  float L = TR->length;
  s = fmodf(s, L);
  return s < 0 ? s + L : s;
}

/* a point beside the road at s, lat metres to the right, standing on the ground */
static t3_vec3 beside(float s, float lat, float up) {
  t3_vec3 p = track_point(TR, ws(s), lat, 0);
  return on_ground(p.x, p.z, up);
}
/* the same, but the yaw that makes a model's front (+z) face the road's centre line */
static float face_road(float s, float lat) {
  t3_vec3 p = track_point(TR, ws(s), lat, 0), c = track_point(TR, ws(s), 0, 0);
  return yaw_to(p, c);
}
/* yaw that makes a model's +z follow the road's direction at s */
static float along_road(float s) {
  tsample a = track_at(TR, ws(s));
  return atan2f(a.f.x, a.f.z);
}

/* ── the pan: a high plateau (the first half of the lap) and a sunken salt pan (the second), joined by an escarpment ── */

#define PAN_DROP 17.3f
static float base_height(float x, float z) {
  /* signed distance to the escarpment line: positive on the pan side */
  float d = x * 0.649f - (z + 50.0f) * 0.760f;
  float basin = sstep(-40.0f, 95.0f, d);
  float h = -PAN_DROP * basin;
  /* salt crust ridges: low polygons of heaved crust, a metre or so across hundreds of metres */
  h += ground_noise(x, z, 140.0f, 1.1f, 3, 31) - 0.5f;
  h += ground_noise(x, z, 26.0f, 0.35f, 2, 77);
  /* the lakebed rises into foothills far from the course, so the mountain ring has a base to stand on */
  float r = sqrtf((x - CX) * (x - CX) + (z - CZ) * (z - CZ));
  float far = sstep(900.0f, 2000.0f, r);
  h += far * (14.0f + 36.0f * ground_noise(x, z, 520.0f, 1.0f, 3, 5));
  return h;
}
static float height_fn(float x, float z, void *ud) {
  (void)ud;
  float h = base_height(x, z);
  float ry;
  int idx;
  float gap = road_gap_idx(TR, x, z, &ry, &idx);
  if (gap < 90.0f && !(TR->s[idx].flags & (TF_GAP | TF_TUNNEL))) {
    /* grade the crust up or down to the road, a wide gentle embankment (a ramp is a salt-block earthwork) */
    /* the crust lies below the road, with room for a banked road's low edge, then eases up to the open flats */
    const tsample *sm = &TR->s[idx];
    float floor_y = ry - 0.4f - sm->hw * fabsf(sinf(sm->bank));
    float w = 1.0f - sstep(14.0f, 48.0f, gap);
    h = lerpf(h, floor_y, w);
  }
  return h;
}

/* ── geometry helpers (a plain triangle soup with vertex colours) ───────────────────────────────────── */

typedef struct soup {
  float *p, *n, *c;
  uint32_t *ix;
  int nv, ni, cv, ci;
} soup;
static int sv(soup *s, t3_vec3 p, t3_vec3 n, pcol c) {
  if (s->nv == s->cv) {
    s->cv = s->cv ? s->cv * 2 : 1024;
    s->p = realloc(s->p, sizeof(float) * 3 * (size_t)s->cv), s->n = realloc(s->n, sizeof(float) * 3 * (size_t)s->cv);
    s->c = realloc(s->c, sizeof(float) * 3 * (size_t)s->cv);
  }
  int i = s->nv++;
  s->p[i * 3] = p.x, s->p[i * 3 + 1] = p.y, s->p[i * 3 + 2] = p.z;
  s->n[i * 3] = n.x, s->n[i * 3 + 1] = n.y, s->n[i * 3 + 2] = n.z;
  s->c[i * 3] = c.r, s->c[i * 3 + 1] = c.g, s->c[i * 3 + 2] = c.b;
  return i;
}
static void st(soup *s, int a, int b, int c) {
  if (s->ni + 3 > s->ci) s->ci = s->ci ? s->ci * 2 : 2048, s->ix = realloc(s->ix, sizeof(uint32_t) * (size_t)s->ci);
  s->ix[s->ni++] = (uint32_t)a, s->ix[s->ni++] = (uint32_t)b, s->ix[s->ni++] = (uint32_t)c;
}
static t3_geometry *soup_geo(soup *s) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, s->p, s->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, s->n, s->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, s->c, s->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, s->ix, s->ni, 1), t3_geometry_set_index(g, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(g);
  free(s->p), free(s->n), free(s->c), free(s->ix);
  memset(s, 0, sizeof *s);
  return g;
}

/* a node holding a prop_model's layers (the model's geometry is handed over) */
static cw_node model_node(cw_node parent, const char *name, prop_model m, int tex, bool cast) {
  t3_object *grp = t3_group_new();
  for (int l = 0; l < PL_COUNT; l++) {
    if (!m.g[l]) continue;
    t3_mesh *me = t3_mesh_new(m.g[l], prop_material(l, l == PL_MAIN ? tex : -1, (pcol){ 1, 1, 1 }, 1));
    me->base.cast_shadow = cast && l != PL_GLOW;
    me->base.receive_shadow = true;
    t3_object_add(grp, me);
    t3_release(me);
  }
  cw_node n = hang(parent, name, grp);
  t3_release(grp);
  prop_model_free(&m);
  return n;
}

/* instanced custom models: one batch per layer */
typedef struct inst {
  prop_batch *b[PL_COUNT];
  float radius;
} inst;
static inst inst_new(prop_model m, int tex, pcol tint) {
  inst r = { 0 };
  for (int l = 0; l < PL_COUNT; l++)
    if (m.g[l]) r.b[l] = pb_begin(m.g[l], prop_material(l, l == PL_MAIN ? tex : -1, l == PL_GLOW ? (pcol){ 1, 1, 1 } : tint, 1));
  r.radius = m.radius;
  prop_model_free(&m);
  return r;
}
static void inst_add(inst *i, t3_vec3 p, float yaw, float scale) {
  for (int l = 0; l < PL_COUNT; l++)
    if (i->b[l]) pb_add(i->b[l], p, yaw, V(scale, scale, scale));
}
static void inst_addq(inst *i, t3_vec3 p, t3_quat q, float scale) {
  for (int l = 0; l < PL_COUNT; l++)
    if (i->b[l]) pb_add_q(i->b[l], p, q, V(scale, scale, scale));
}
static void inst_add_tint(inst *i, t3_vec3 p, float yaw, float scale, pcol tint) {
  for (int l = 0; l < PL_COUNT; l++)
    if (i->b[l]) pb_add_c(i->b[l], p, t3_quat_from_axis_angle(V(0, 1, 0), yaw), V(scale, scale, scale), l == PL_GLOW ? (pcol){ 1, 1, 1 } : tint);
}
static cw_node inst_finish(inst *i, cw_node parent, const char *name, bool cast) {
  t3_object *dummy = t3_group_new();
  cw_node root = hang(parent, name, dummy);
  t3_release(dummy);
  for (int l = 0; l < PL_COUNT; l++)
    if (i->b[l]) {
      char nm[40];
      snprintf(nm, sizeof nm, "%s_%d", name, l);
      pb_finish(i->b[l], root, nm, cast && l != PL_GLOW);
      i->b[l] = NULL;
    }
  return root;
}

/* a quad whose front faces away from `inside` (so a hull's plates always face out) */
static void oquad(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, pcol c, t3_vec3 inside) {
  t3_vec3 n = vcross(vsub(p1, p0), vsub(p2, p0));
  t3_vec3 mid = vmul(vadd(vadd(p0, p1), vadd(p2, p3)), 0.25f);
  if (vdot(n, vsub(mid, inside)) < 0) pm_quad(b, p0, p3, p2, p1, c);
  else pm_quad(b, p0, p1, p2, p3, c);
}

/* ── models ─────────────────────────────────────────────────────────────────────────────────────────── */

/* an oil derrick: a tapering four-legged lattice with a crown block, a working platform and a red lamp on top */
static prop_model m_derrick(uint32_t seed, float H) {
  pm *b = pm_new(seed);
  float w0 = H * 0.105f, w1 = H * 0.022f;
  int N = 8;
  pcol steel = pc_mul(C_RUST, 0.9f), dark = pc_mul(C_STEEL, 0.65f);
  pm_layer(b, PL_METAL);
  for (int j = 0; j < N; j++) {
    float y0 = H * 0.92f * (float)j / (float)N, y1 = H * 0.92f * (float)(j + 1) / (float)N;
    float a0 = lerpf(w0, w1, (float)j / (float)N), a1 = lerpf(w0, w1, (float)(j + 1) / (float)N);
    for (int k = 0; k < 4; k++) {
      float sx0 = (k & 1) ? 1.f : -1.f, sz0 = (k & 2) ? 1.f : -1.f;
      int k2 = (k + 1) & 3;
      float sx1 = (k2 & 1) ? 1.f : -1.f, sz1 = (k2 & 2) ? 1.f : -1.f;
      /* corner order around the square: 0 (-,-) 1 (+,-) 3 (+,+) 2 (-,+) */
      (void)sx1, (void)sz1;
      t3_vec3 c0 = V(sx0 * a0, y0, sz0 * a0), c1 = V(sx0 * a1, y1, sz0 * a1);
      pm_beam(b, c0, c1, H * 0.012f + 0.1f, steel);
    }
    /* horizontal square and the diagonals of each face */
    t3_vec3 q0[4] = { V(-a0, y0, -a0), V(a0, y0, -a0), V(a0, y0, a0), V(-a0, y0, a0) };
    t3_vec3 q1[4] = { V(-a1, y1, -a1), V(a1, y1, -a1), V(a1, y1, a1), V(-a1, y1, a1) };
    for (int k = 0; k < 4; k++) {
      int n = (k + 1) & 3;
      pm_beam(b, q1[k], q1[n], H * 0.007f + 0.06f, dark);
      if (((j + k) & 1) == 0) pm_beam(b, q0[k], q1[n], H * 0.006f + 0.05f, dark);
      else pm_beam(b, q0[n], q1[k], H * 0.006f + 0.05f, dark);
    }
  }
  /* the working platform two thirds up and the crown block on top */
  pm_push(b);
  pm_move(b, 0, H * 0.62f, 0);
  pm_box_c(b, w0 * 0.75f, 0.35f, w0 * 0.75f, dark);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, H * 0.92f, 0);
  pm_box(b, w1 * 3.2f, H * 0.045f, w1 * 3.2f, C_STEEL, C_STEEL_L);
  pm_move(b, 0, H * 0.045f, 0);
  pm_box(b, w1 * 1.4f, H * 0.03f, w1 * 1.4f, dark, dark);
  pm_pop(b);
  /* a hanging block and a cable down the middle */
  pm_rod(b, V(0, H * 0.92f, 0), V(0, H * 0.35f, 0), 0.05f, 4, C_DARK);
  pm_box_c(b, 0.5f, 0.7f, 0.5f, C_STEEL);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, H * 0.99f, 0);
  pm_box_c(b, 0.5f, 0.5f, 0.5f, glow(rgb(1.0f, 0.12f, 0.1f), 5.0f));
  pm_pop(b);
  return pm_finish(b);
}

/* a nodding pump jack: the static base, and the walking beam (its own model, pivoting at the post top) */
static prop_model m_pump_base(uint32_t seed) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_MAIN);
  pm_box(b, 3.4f, 0.35f, 6.5f, C_RUST_D, C_RUST);               /* the skid */
  pm_push(b);
  pm_move(b, 0, 0.35f, -2.6f);
  pm_box(b, 1.4f, 1.0f, 1.2f, C_TEAL, pc_mul(C_TEAL, 1.2f));    /* the motor, painted */
  pm_pop(b);
  pm_layer(b, PL_METAL);
  /* the samson post: an A frame */
  for (int k = -1; k <= 1; k += 2) {
    pm_beam(b, V(k * 0.9f, 0.35f, 1.2f), V(k * 0.15f, 5.2f, 0.0f), 0.2f, C_STEEL);
    pm_beam(b, V(k * 0.9f, 0.35f, -1.2f), V(k * 0.15f, 5.2f, 0.0f), 0.2f, C_STEEL);
  }
  pm_beam(b, V(-0.5f, 2.3f, 0.6f), V(0.5f, 2.3f, 0.6f), 0.14f, C_STEEL);
  pm_box_c(b, 1.2f, 0.35f, 0.5f, C_DARK);
  return pm_finish(b);
}
static prop_model m_pump_beam(uint32_t seed) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, 0, 0.5f);
  pm_box_c(b, 0.5f, 0.55f, 7.2f, pc_mul(C_MUST, 0.95f));        /* the walking beam */
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, -0.1f, 4.1f);                                   /* the horsehead */
  pm_rotx(b, 0.0f);
  pm_wedge(b, 0.6f, 1.7f, 0.9f, C_ORNG, pc_mul(C_ORNG, 1.1f));
  pm_pop(b);
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, 0, -0.6f, -3.0f);                                  /* the counterweight */
  pm_rotz(b, 1.5708f);
  pm_cyl(b, 0.9f, 0.9f, 0.6f, 12, C_DARK, C_STEEL);
  pm_pop(b);
  pm_rod(b, V(0, -0.2f, 3.9f), V(0, -4.3f, 3.9f), 0.06f, 4, C_DARK); /* the rod down into the well */
  return pm_finish(b);
}

/* a farm wind pump: lattice tower, tail vane; the wheel is a separate model turned about z */
static prop_model m_wp_tower(uint32_t seed, float H) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_METAL);
  float w0 = H * 0.2f, w1 = H * 0.06f;
  for (int j = 0; j < 4; j++) {
    float y0 = H * (float)j / 4.0f, y1 = H * (float)(j + 1) / 4.0f;
    float a0 = lerpf(w0, w1, (float)j / 4.0f), a1 = lerpf(w0, w1, (float)(j + 1) / 4.0f);
    for (int k = 0; k < 4; k++) {
      float sx = (k & 1) ? 1.f : -1.f, sz = (k & 2) ? 1.f : -1.f;
      pm_beam(b, V(sx * a0, y0, sz * a0), V(sx * a1, y1, sz * a1), 0.09f, C_STEEL);
    }
    t3_vec3 q1[4] = { V(-a1, y1, -a1), V(a1, y1, -a1), V(a1, y1, a1), V(-a1, y1, a1) };
    t3_vec3 q0[4] = { V(-a0, y0, -a0), V(a0, y0, -a0), V(a0, y0, a0), V(-a0, y0, a0) };
    for (int k = 0; k < 4; k++) {
      int n = (k + 1) & 3;
      pm_beam(b, q1[k], q1[n], 0.06f, C_STEEL);
      pm_beam(b, (j & 1) ? q0[k] : q0[n], (j & 1) ? q1[n] : q1[k], 0.05f, C_STEEL);
    }
  }
  pm_push(b);
  pm_move(b, 0, H, 0);
  pm_box(b, 0.5f, 0.6f, 1.2f, C_DARK, C_STEEL);
  pm_pop(b);
  pm_layer(b, PL_MAIN);
  /* the tail vane, a rusty fin, and a little paint */
  pm_quad(b, V(0, H + 0.2f, -0.6f), V(0, H + 0.2f, -3.6f), V(0, H + 1.5f, -3.6f), V(0, H + 1.1f, -0.6f), C_TEAL);
  pm_quad(b, V(0, H + 0.2f, -3.6f), V(0, H + 0.2f, -0.6f), V(0, H + 1.1f, -0.6f), V(0, H + 1.5f, -3.6f), C_TEAL);
  pm_beam(b, V(0, H + 0.3f, -0.6f), V(0, H + 0.3f, -3.6f), 0.08f, C_STEEL);
  return pm_finish(b);
}
static prop_model m_wp_wheel(uint32_t seed, float R) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_METAL);
  pm_ring(b, R, 0.06f, 28, 4, C_STEEL);
  pm_ring(b, R * 0.55f, 0.05f, 20, 4, C_STEEL);
  int blades = 18;
  for (int i = 0; i < blades; i++) {
    float a = 6.2832f * (float)i / (float)blades;
    float ca = cosf(a), sa = sinf(a);
    pm_rod(b, V(0, 0, 0), V(ca * R, sa * R, 0), 0.03f, 3, C_STEEL);
  }
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < blades; i++) {
    float a = 6.2832f * (float)i / (float)blades, a2 = a + 0.2f;
    float r0 = R * 0.55f, r1 = R * 0.98f;
    pcol col = (i & 1) ? C_BONE : C_RUST;
    t3_vec3 p0 = V(cosf(a) * r0, sinf(a) * r0, 0.05f), p1 = V(cosf(a) * r1, sinf(a) * r1, 0.05f);
    t3_vec3 p2 = V(cosf(a2) * r1, sinf(a2) * r1, -0.05f), p3 = V(cosf(a2) * r0, sinf(a2) * r0, -0.05f);
    pm_quad(b, p0, p1, p2, p3, col);
    pm_quad(b, p0, p3, p2, p1, col);
  }
  pm_layer(b, PL_METAL);
  pm_cyl(b, 0.25f, 0.25f, 0.5f, 8, C_DARK, C_STEEL);
  return pm_finish(b);
}

/* a spectator: a little person with a shirt colour; arms up if the seed says so */
static prop_model m_person(uint32_t seed, pcol shirt) {
  pm *b = pm_new(seed);
  pcol skin = pc_mix(hex(0xd9a37c), hex(0x8a5a3a), hashf_(seed * 7 + 1));
  pcol trou = pc_mix(C_DARK, C_STEEL, hashf_(seed * 13));
  pm_layer(b, PL_MAIN);
  pm_box(b, 0.5f, 0.85f, 0.28f, trou, trou);                                    /* legs */
  pm_push(b);
  pm_move(b, 0, 0.8f, 0);
  pm_box(b, 0.6f, 0.65f, 0.34f, shirt, pc_mul(shirt, 1.12f));                    /* torso */
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 1.55f, 0);
  pm_ball(b, 0.2f, 0.22f, 0.2f, 1, 0.0f, skin, skin);                            /* head */
  pm_pop(b);
  bool up = (hash_u(seed) & 1) != 0;
  for (int k = -1; k <= 1; k += 2) {
    if (up) pm_limb(b, V(k * 0.33f, 1.35f, 0), V(k * 0.55f, 2.05f, 0.1f), 0.07f, 0.06f, 4, shirt, skin);
    else pm_limb(b, V(k * 0.33f, 1.35f, 0), V(k * 0.4f, 0.8f, 0.1f), 0.07f, 0.06f, 4, shirt, skin);
  }
  return pm_finish(b);
}

/* a scrap-built grandstand facing +z: rising tiers on scaffolding, a patched corrugated roof, painted fascia boards */
static prop_model m_stand(uint32_t seed, float W, int tiers) {
  pm *b = pm_new(seed);
  float rowd = 1.35f, rowh = 0.95f, top = rowh * (float)tiers;
  float depth = rowd * (float)tiers;
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < tiers; i++) {
    float y = rowh * (float)(i + 1), z = -rowd * ((float)i + 0.5f);
    pcol c = ((i + (int)(seed & 3)) & 1) ? pc_mul(C_RUST, 1.1f) : pc_mul(C_BONE, 0.82f);
    pm_push(b);
    pm_move(b, 0, 0, z);
    pm_box(b, W, y, rowd, pc_mul(c, 0.55f), c);
    pm_pop(b);
  }
  /* the back wall: rusty sheets, each a slightly different colour */
  int sheets = (int)(W / 2.4f);
  for (int i = 0; i < sheets; i++) {
    float x0 = -W / 2 + W * (float)i / (float)sheets, x1 = -W / 2 + W * (float)(i + 1) / (float)sheets;
    float k = hashf_(seed * 31 + (uint32_t)i);
    pcol c = pc_mix(C_RUST, k > 0.7f ? C_TEAL : C_STEEL_L, k > 0.5f ? 0.5f : 0.15f);
    float zb = -depth - 0.1f, h = top + 5.5f + 0.4f * (k - 0.5f);
    pm_quad(b, V(x1, 0, zb), V(x0, 0, zb), V(x0, h, zb), V(x1, h, zb), c);
    pm_quad(b, V(x0, 0, zb), V(x1, 0, zb), V(x1, h, zb), V(x0, h, zb), pc_mul(c, 0.8f));
  }
  /* the roof: a slanted sheet, high at the back, with a bright painted lip */
  float hf = top + 4.2f, hb = top + 5.7f;
  for (int i = 0; i < sheets; i++) {
    float x0 = -W / 2 - 0.5f + (W + 1.0f) * (float)i / (float)sheets, x1 = -W / 2 - 0.5f + (W + 1.0f) * (float)(i + 1) / (float)sheets;
    float k = hashf_(seed * 17 + (uint32_t)i);
    pcol c = pc_mix(C_STEEL_L, C_RUST, k * 0.8f);
    pm_quad(b, V(x0, hf, 1.3f), V(x1, hf, 1.3f), V(x1, hb, -depth - 0.3f), V(x0, hb, -depth - 0.3f), c);
    pm_quad(b, V(x0, hf - 0.1f, 1.3f), V(x0, hb - 0.1f, -depth - 0.3f), V(x1, hb - 0.1f, -depth - 0.3f), V(x1, hf - 0.1f, 1.3f), pc_mul(c, 0.55f));
    pcol lip = ((i + (int)seed) % 3 == 0) ? C_ORNG : ((i + (int)seed) % 3 == 1) ? C_TEAL : C_BONE;
    pm_quad(b, V(x0, hf - 0.8f, 1.32f), V(x1, hf - 0.8f, 1.32f), V(x1, hf, 1.32f), V(x0, hf, 1.32f), lip);
  }
  /* fascia boards along the front of the first tier: loud paint */
  for (int i = 0; i < sheets; i++) {
    float x0 = -W / 2 + W * (float)i / (float)sheets, x1 = -W / 2 + W * (float)(i + 1) / (float)sheets;
    pcol c = (i & 1) ? C_MUST : C_RED;
    if (i % 5 == 2) c = C_BLUE;
    pm_quad(b, V(x0, 0, 0.02f), V(x1, 0, 0.02f), V(x1, rowh * 0.95f, 0.02f), V(x0, rowh * 0.95f, 0.02f), c);
  }
  pm_layer(b, PL_METAL);
  /* scaffold poles: front posts under the roof lip, rear posts, braces */
  int posts = (int)(W / 4.0f) + 1;
  for (int i = 0; i < posts; i++) {
    float x = -W / 2 + W * (float)i / (float)(posts - 1);
    pm_beam(b, V(x, 0, 1.2f), V(x, hf, 1.2f), 0.18f, C_STEEL);
    pm_beam(b, V(x, hf, 1.2f), V(x, hb, -depth - 0.2f), 0.16f, C_STEEL);
    if (i + 1 < posts) {
      float x2 = -W / 2 + W * (float)(i + 1) / (float)(posts - 1);
      pm_beam(b, V(x, 0.3f, 1.2f), V(x2, hf * 0.8f, 1.2f), 0.09f, pc_mul(C_STEEL, 0.9f));
    }
  }
  pm_beam(b, V(-W / 2, hf, 1.2f), V(W / 2, hf, 1.2f), 0.2f, C_STEEL);
  /* a hand rail along the top tier */
  pm_beam(b, V(-W / 2, top + 1.0f, -depth + 0.5f), V(W / 2, top + 1.0f, -depth + 0.5f), 0.08f, C_STEEL_L);
  pm_layer(b, PL_GLOW);
  /* bare lamps under the roof lip */
  for (int i = 0; i < posts - 1; i++) {
    float x = -W / 2 + W * ((float)i + 0.5f) / (float)(posts - 1);
    pm_push(b);
    pm_move(b, x, hf - 0.5f, 1.0f);
    pm_box_c(b, 0.3f, 0.2f, 0.3f, glow(rgb(1.0f, 0.82f, 0.55f), 3.0f));
    pm_pop(b);
  }
  return pm_finish(b);
}

/* a flag cloth: one panel, double sided (several are chained so the cloth bends in a wave) */
static prop_model m_cloth(uint32_t seed, float len, float hgt, pcol c) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_LEAF);
  pm_quad(b, V(0, 0, 0), V(0, hgt, 0), V(len, hgt, 0), V(len, 0, 0), c);
  return pm_finish(b);
}
static prop_model m_pole(uint32_t seed, float H) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_METAL);
  pm_cyl(b, 0.14f, 0.07f, H, 6, C_STEEL, C_STEEL_L);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, H, 0);
  pm_ball(b, 0.2f, 0.2f, 0.2f, 1, 0, glow(rgb(1.0f, 0.8f, 0.4f), 3.0f), glow(rgb(1.0f, 0.8f, 0.4f), 3.0f));
  pm_pop(b);
  return pm_finish(b);
}

/* a painted dune buggy: roll cage, big knobbly tyres, a bucket seat, a whip aerial. ~3.4 m long, facing +z */
static prop_model m_buggy(uint32_t seed, pcol paint) {
  pm *b = pm_new(seed);
  pcol tube = pc_mul(C_STEEL, 0.8f);
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, 0.35f, 0);
  pm_box(b, 1.5f, 0.5f, 3.1f, pc_mul(paint, 0.7f), paint);                    /* tub */
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0.85f, 1.35f);
  pm_wedge(b, 1.3f, 0.35f, 0.9f, paint, pc_mul(paint, 1.2f));                  /* nose */
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0.85f, -0.4f);
  pm_box(b, 0.7f, 0.5f, 0.6f, C_DARK, C_DARK);                                  /* seat */
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0.8f, -1.3f);
  pm_box(b, 0.9f, 0.7f, 0.8f, C_STEEL, C_STEEL_L);                              /* engine */
  pm_pop(b);
  /* a number board on the nose */
  pm_quad(b, V(-0.35f, 1.1f, 1.62f), V(0.35f, 1.1f, 1.62f), V(0.35f, 1.55f, 1.55f), V(-0.35f, 1.55f, 1.55f), C_BONE);
  pm_layer(b, PL_METAL);
  for (int k = -1; k <= 1; k += 2) {
    pm_rod(b, V(k * 0.7f, 0.8f, 0.5f), V(k * 0.65f, 2.0f, -0.2f), 0.05f, 5, tube);
    pm_rod(b, V(k * 0.7f, 0.8f, -0.9f), V(k * 0.65f, 2.0f, -0.3f), 0.05f, 5, tube);
    pm_rod(b, V(k * 0.65f, 2.0f, -0.2f), V(k * 0.65f, 2.0f, -0.3f), 0.05f, 5, tube);
  }
  pm_rod(b, V(-0.65f, 2.0f, -0.25f), V(0.65f, 2.0f, -0.25f), 0.05f, 5, tube);
  pm_rod(b, V(-0.7f, 0.8f, 0.5f), V(0.7f, 0.8f, 0.5f), 0.05f, 5, tube);
  pm_rod(b, V(0.4f, 1.2f, -1.4f), V(0.5f, 3.4f, -1.6f), 0.02f, 3, C_DARK);       /* the whip aerial */
  /* the wheels: a fat tyre, a bright rim */
  for (int i = 0; i < 4; i++) {
    float x = (i & 1) ? 1.0f : -1.0f, z = (i & 2) ? 1.15f : -1.15f, r = (i & 2) ? 0.5f : 0.62f;
    pm_push(b);
    pm_move(b, x, r, z);
    pm_roty(b, 1.5708f);
    pm_ring(b, r * 0.72f, r * 0.3f, 14, 6, C_DARK);
    pm_pop(b);
    pm_push(b);
    pm_move(b, x + (x > 0 ? 0.12f : -0.12f), r, z);
    pm_rotz(b, 1.5708f);
    pm_cyl(b, r * 0.45f, r * 0.45f, 0.1f, 8, C_STEEL_L, C_STEEL_L);
    pm_pop(b);
  }
  pm_layer(b, PL_LEAF);
  pm_quad(b, V(0.5f, 3.4f, -1.6f), V(0.5f, 3.0f, -1.6f), V(0.5f, 3.2f, -2.1f), V(0.5f, 3.4f, -1.6f), C_ORNG);
  pm_quad(b, V(0.5f, 3.4f, -1.6f), V(0.5f, 3.2f, -2.1f), V(0.5f, 3.0f, -1.6f), V(0.5f, 3.4f, -1.6f), C_ORNG);
  return pm_finish(b);
}

/* a ship hull and its works, bow toward +z, deck level at y = D above the waterline (y = 0), keel below. Everything
 * is flat shaded plates in rust and bleached paint; windows glow. kind 0: big freighter, 1: trawler. */
static prop_model m_ship(uint32_t seed, float L, float B, float D, float K, int kind) {
  pm *b = pm_new(seed);
  pm_vary(b, 0.1f);
  pm_layer(b, PL_MAIN);
  enum { NS = 16, NR = 7 };
  t3_vec3 ring[NS + 1][NR];
  float deckY[NS + 1];
  for (int i = 0; i <= NS; i++) {
    float t = (float)i / (float)NS;
    float hb = B * 0.5f;
    if (t < 0.12f) hb *= 0.62f + 0.38f * (t / 0.12f);
    if (t > 0.6f) hb *= powf(fmaxf(0.0f, 1.0f - powf((t - 0.6f) / 0.4f, 1.7f)), 0.8f) * 0.95f + 0.05f;
    float keel = -K * (1.0f - 0.55f * powf(fabsf(2 * t - 1), 3.0f));
    float dy = D + 0.32f * D * powf(t, 4.0f);
    deckY[i] = dy;
    float z = L * (t - 0.5f);
    ring[i][0] = V(-hb, dy, z);
    ring[i][1] = V(-hb, 0.0f, z);
    ring[i][2] = V(-hb * 0.78f, keel * 0.7f, z);
    ring[i][3] = V(0, keel, z);
    ring[i][4] = V(hb * 0.78f, keel * 0.7f, z);
    ring[i][5] = V(hb, 0.0f, z);
    ring[i][6] = V(hb, dy, z);
  }
  t3_vec3 inside = V(0, D * 0.4f, 0);
  pcol topside = kind == 0 ? C_HULL : pc_mix(C_TEAL, C_RUST, 0.45f);
  pcol under = pc_mul(C_RUST_D, 0.9f);
  for (int i = 0; i < NS; i++) {
    for (int j = 0; j + 1 < NR; j++) {
      pcol c = (j == 0 || j == 5) ? pc_mix(topside, C_RUST, hashf_(seed + (uint32_t)(i * 7 + j)) * 0.7f) : under;
      if (j == 1 || j == 4) c = pc_mix(under, C_RUST, 0.5f);
      /* a band of the old paint: boot-top white above the waterline on the freighter */
      if (kind == 0 && (j == 0 || j == 5) && (i % 5 == 2)) c = pc_mix(c, C_BONE, 0.55f);
      oquad(b, ring[i][j], ring[i + 1][j], ring[i + 1][j + 1], ring[i][j + 1], c, inside);
    }
    /* the main deck */
    pcol dc = pc_mix(hex(0x5a4a3c), C_RUST, hashf_(seed + (uint32_t)i * 3));
    pm_quad(b, ring[i][0], ring[i + 1][0], ring[i + 1][6], ring[i][6], dc);
  }
  /* the stern plate */
  for (int j = 0; j + 1 < NR; j++) oquad(b, ring[0][j], ring[0][j + 1], V(0, deckY[0] * 0.5f, ring[0][0].z), V(0, deckY[0] * 0.5f, ring[0][0].z), under, V(0, 0, 1e4f));
  /* bulwarks along both sides */
  pm_layer(b, PL_METAL);
  float zb0 = L * -0.46f, zb1 = L * 0.42f;
  for (int k = -1; k <= 1; k += 2) {
    float hbk = ring[6][6].x < 0 ? -ring[6][6].x : ring[6][6].x;
    pm_beam(b, V(k * hbk, deckY[6] + 0.6f, zb0), V(k * hbk, deckY[11] + 0.6f, zb1), 0.18f, C_STEEL);
  }
  pm_layer(b, PL_MAIN);
  /* the accommodation block aft with a bridge on top, the funnel behind it */
  float az = L * -0.3f, aw = B * 0.7f, ah = kind == 0 ? D * 0.9f : D * 0.7f;
  pm_push(b);
  pm_move(b, 0, D, az);
  pm_box(b, aw, ah, L * 0.16f, pc_mul(C_BONE, 0.8f), C_BONE);
  pm_move(b, 0, ah, 0);
  pm_box(b, aw * 0.9f, ah * 0.6f, L * 0.13f, pc_mul(C_BONE, 0.7f), pc_mul(C_BONE, 0.95f));
  pm_move(b, 0, ah * 0.6f, 0);
  pm_box(b, aw * 1.05f, ah * 0.4f, L * 0.1f, C_RUST, pc_mul(C_RUST, 1.2f));          /* the bridge */
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, D + ah * 1.2f, az - L * 0.1f);
  pm_cyl(b, B * 0.12f, B * 0.09f, ah * 1.1f, 10, C_RUST_D, pc_mul(C_ORNG, 0.8f));      /* the funnel */
  pm_cyl_ex(b, B * 0.092f, B * 0.092f, 0.5f, 10, C_DARK, C_DARK, false, true);
  pm_pop(b);
  if (kind == 0) {
    /* hatches and a pair of cargo cranes along the deck */
    for (int i = 0; i < 3; i++) {
      pm_push(b);
      pm_move(b, 0, deckY[8], L * (0.02f + 0.16f * (float)i));
      pm_box(b, B * 0.55f, 1.4f, L * 0.1f, pc_mul(C_RUST, 0.8f), C_RUST);
      pm_pop(b);
    }
  } else {
    pm_push(b);
    pm_move(b, 0, D, L * 0.15f);
    pm_box(b, B * 0.5f, D * 0.5f, L * 0.1f, C_ORNG, pc_mul(C_ORNG, 1.1f));         /* the fish hold house */
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  /* masts, a derrick post, crane jibs and rigging */
  pm_rod(b, V(0, D + ah * 2.0f, az), V(0, D + ah * 3.3f + (kind == 0 ? 6 : 3), az), 0.2f, 5, C_STEEL);
  float mz = L * 0.18f;
  pm_rod(b, V(0, deckY[10], mz), V(0, deckY[10] + (kind == 0 ? 16.0f : 11.0f), mz), 0.28f, 6, C_STEEL);
  pm_beam(b, V(0, deckY[10] + (kind == 0 ? 15.0f : 10.0f), mz), V(B * 0.55f, deckY[10] + 4.0f, mz + 5.0f), 0.2f, C_STEEL);
  pm_rod(b, V(B * 0.55f, deckY[10] + 4.0f, mz + 5.0f), V(B * 0.55f, deckY[10] + 0.5f, mz + 5.0f), 0.05f, 3, C_DARK);
  pm_cable(b, V(0, deckY[10] + (kind == 0 ? 15.0f : 10.0f), mz), V(0, deckY[8] + 3.0f, L * 0.4f), 1.6f, 0.05f, C_DARK);
  pm_layer(b, PL_GLOW);
  /* lit portholes down the accommodation block */
  for (int r = 0; r < 3; r++)
    for (int i = 0; i < 6; i++) {
      float x = -aw * 0.4f + aw * 0.8f * (float)i / 5.0f;
      pm_push(b);
      pm_move(b, x, D + ah * (0.2f + 0.3f * (float)r), az + L * 0.081f);
      pm_box_c(b, 0.6f, 0.45f, 0.1f, glow(rgb(1.0f, 0.78f, 0.45f), 2.6f));
      pm_pop(b);
    }
  return pm_finish(b);
}

/* a crashed airliner standing nose-down in the salt (axis along y, nose buried at y = -6): a vertical fuselage with
 * a peeling livery stripe, two swept wings sticking out sideways (+x is the one that reaches over the road), engine
 * pods, tail fin and a row of lit windows. About 56 m of it shows above the crust. */
static prop_model m_airliner(uint32_t seed) {
  pm *b = pm_new(seed);
  pm_vary(b, 0.07f);
  float rz[] = { 0.1f, -6.0f, 1.6f, -5.2f, 2.7f, -3.0f, 3.5f, -0.5f, 3.9f, 3.0f, 4.0f, 8.0f, 4.0f, 38.0f, 3.7f, 44.0f, 2.8f, 50.0f, 1.7f, 56.0f, 1.0f, 60.0f };
  pm_layer(b, PL_MAIN);
  pm_lathe(b, rz, 11, 16, pc_mul(C_BONE, 0.7f), pc_mul(C_BONE, 1.0f));
  /* the livery band and a burnt belly along one side */
  pm_layer(b, PL_MAIN);
  for (int k = 0; k < 16; k++) {
    float a0 = 6.2832f * (float)k / 16.0f, a1 = 6.2832f * (float)(k + 1) / 16.0f;
    pcol c = (k % 4 == 0) ? C_TEAL : (k % 4 == 1 ? C_ORNG : C_BONE);
    (void)a0, (void)a1, (void)c;
  }
  /* a stripe: a thin ring just proud of the skin, two colours */
  pm_push(b);
  pm_move(b, 0, 20.0f, 0);
  pm_rotx(b, 1.5708f);
  pm_ring(b, 4.0f, 0.12f, 20, 4, C_TEAL);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 22.0f, 0);
  pm_rotx(b, 1.5708f);
  pm_ring(b, 4.0f, 0.12f, 20, 4, C_ORNG);
  pm_pop(b);
  /* wings: slabs in the xy plane, swept back toward -y (the tail), thickness along z */
  pm_layer(b, PL_MAIN);
  for (int k = -1; k <= 1; k += 2) {
    float x1 = k * 36.0f, x0 = k * 3.6f;
    t3_vec3 a = V(x0, 33.0f, 0), bq = V(x1, 22.0f, 0), c = V(x1, 18.5f, 0), d = V(x0, 24.0f, 0);
    float th = 0.55f;
    pcol top = pc_mul(C_STEEL_L, 0.9f), bot = pc_mul(C_STEEL, 0.7f);
    /* two faces (front +z, back -z) and the edge strips */
    if (k > 0) {
      pm_quad(b, V(a.x, a.y, th), V(d.x, d.y, th), V(c.x, c.y, th * 0.4f), V(bq.x, bq.y, th * 0.4f), top);
      pm_quad(b, V(a.x, a.y, -th), V(bq.x, bq.y, -th * 0.4f), V(c.x, c.y, -th * 0.4f), V(d.x, d.y, -th), bot);
    } else {
      pm_quad(b, V(a.x, a.y, th), V(bq.x, bq.y, th * 0.4f), V(c.x, c.y, th * 0.4f), V(d.x, d.y, th), top);
      pm_quad(b, V(a.x, a.y, -th), V(d.x, d.y, -th), V(c.x, c.y, -th * 0.4f), V(bq.x, bq.y, -th * 0.4f), bot);
    }
    pm_quad(b, V(a.x, a.y, th), V(a.x, a.y, -th), V(bq.x, bq.y, -th * 0.4f), V(bq.x, bq.y, th * 0.4f), pc_mul(C_BONE, 0.8f));
    pm_quad(b, V(d.x, d.y, th), V(c.x, c.y, th * 0.4f), V(c.x, c.y, -th * 0.4f), V(d.x, d.y, -th), pc_mul(C_RUST, 1.0f));
    pm_quad(b, V(bq.x, bq.y, th * 0.4f), V(bq.x, bq.y, -th * 0.4f), V(c.x, c.y, -th * 0.4f), V(c.x, c.y, th * 0.4f), C_ORNG);
    /* engine pod under the wing: a short fat cylinder along y */
    pm_push(b);
    pm_move(b, k * 14.0f, 14.0f, 0);
    pm_cyl(b, 1.9f, 2.2f, 12.0f, 12, pc_mul(C_STEEL, 0.8f), C_BONE);
    pm_move(b, 0, 12.0f, 0);
    pm_cyl(b, 2.2f, 1.2f, 1.6f, 12, C_STEEL_L, C_DARK);
    pm_pop(b);
  }
  /* the tail: a tall fin and two stabilisers */
  pm_layer(b, PL_MAIN);
  float tb = 40.0f;
  t3_vec3 f0 = V(0, tb, 4.0f), f1 = V(0, tb + 12.0f, 11.0f), f2 = V(0, tb + 22.0f, 11.0f), f3 = V(0, tb + 22.0f, 6.0f);
  (void)f0, (void)f1, (void)f2, (void)f3;
  pm_quad(b, V(0.35f, 42.0f, 3.5f), V(0.35f, 60.0f, 3.5f), V(0.35f, 60.0f, 12.0f), V(0.35f, 52.0f, 18.0f), C_TEAL);
  pm_quad(b, V(-0.35f, 42.0f, 3.5f), V(-0.35f, 52.0f, 18.0f), V(-0.35f, 60.0f, 12.0f), V(-0.35f, 60.0f, 3.5f), C_ORNG);
  pm_push(b);
  pm_move(b, 0, 54.0f, 0.0f);
  pm_box_c(b, 17.0f, 3.2f, 0.45f, pc_mul(C_STEEL_L, 0.85f));
  pm_pop(b);
  pm_layer(b, PL_METAL);
  /* exposed ribs where the nose tore open, and a boarding stair lying in the salt */
  for (int i = 0; i < 6; i++) {
    float a = 6.2832f * (float)i / 6.0f;
    pm_rod(b, V(cosf(a) * 3.3f, 3.0f, sinf(a) * 3.3f), V(cosf(a) * 3.0f, 9.0f, sinf(a) * 3.0f), 0.1f, 4, C_STEEL);
  }
  pm_layer(b, PL_GLOW);
  /* a row of lit windows on both sides, some dead */
  for (int i = 0; i < 20; i++) {
    if (hashf_(seed + (uint32_t)i) < 0.18f) continue;
    float y = 9.0f + 1.4f * (float)i;
    for (int k = -1; k <= 1; k += 2) {
      pm_push(b);
      pm_move(b, k * 3.97f, y, 0);
      pm_box_c(b, 0.12f, 0.7f, 0.55f, glow(rgb(1.0f, 0.8f, 0.5f), 2.4f));
      pm_pop(b);
    }
  }
  return pm_finish(b);
}

/* the lone giant crane: a lattice tower, a long jib with a counter-jib and its weights, a cab. +x is the jib.
 * The swinging load hangs from a separate node at the jib tip. */
static prop_model m_bigcrane(uint32_t seed, float H, float J) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_METAL);
  float w0 = H * 0.07f, w1 = H * 0.045f;
  pcol yel = pc_mix(C_MUST, C_RUST, 0.25f);
  int N = 12;
  for (int j = 0; j < N; j++) {
    float y0 = H * (float)j / (float)N, y1 = H * (float)(j + 1) / (float)N;
    float a0 = lerpf(w0, w1, (float)j / (float)N), a1 = lerpf(w0, w1, (float)(j + 1) / (float)N);
    for (int k = 0; k < 4; k++) {
      float sx = (k & 1) ? 1.f : -1.f, sz = (k & 2) ? 1.f : -1.f;
      pm_beam(b, V(sx * a0, y0, sz * a0), V(sx * a1, y1, sz * a1), 0.45f, yel);
    }
    t3_vec3 q0[4] = { V(-a0, y0, -a0), V(a0, y0, -a0), V(a0, y0, a0), V(-a0, y0, a0) };
    t3_vec3 q1[4] = { V(-a1, y1, -a1), V(a1, y1, -a1), V(a1, y1, a1), V(-a1, y1, a1) };
    for (int k = 0; k < 4; k++) {
      int n = (k + 1) & 3;
      pm_beam(b, q1[k], q1[n], 0.28f, pc_mul(yel, 0.85f));
      pm_beam(b, ((j + k) & 1) ? q0[k] : q0[n], ((j + k) & 1) ? q1[n] : q1[k], 0.24f, pc_mul(yel, 0.85f));
    }
  }
  float yt = H;
  /* the slewing deck, cab and apex */
  pm_push(b);
  pm_move(b, 0, yt, 0);
  pm_box_c(b, w1 * 2.6f, 2.0f, w1 * 2.6f, C_STEEL);
  pm_pop(b);
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, -w1 * 1.3f, yt + 0.6f, w1 * 1.7f);
  pm_box(b, 4.0f, 3.2f, 3.6f, C_BONE, pc_mul(C_BONE, 1.05f));                     /* the cab */
  pm_pop(b);
  pm_layer(b, PL_METAL);
  float ya = yt + H * 0.2f;
  for (int k = 0; k < 4; k++) {
    float sx = (k & 1) ? 1.f : -1.f, sz = (k & 2) ? 1.f : -1.f;
    pm_beam(b, V(sx * w1, yt, sz * w1), V(0, ya, 0), 0.3f, yel);
  }
  /* the jib: a triangular lattice running out along +x, and the counter jib behind */
  float jy = yt + 1.2f;
  int nj = 14;
  for (int j = 0; j < nj; j++) {
    float x0 = J * (float)j / (float)nj, x1 = J * (float)(j + 1) / (float)nj;
    float h0 = lerpf(2.6f, 0.8f, (float)j / (float)nj), h1 = lerpf(2.6f, 0.8f, (float)(j + 1) / (float)nj);
    pm_beam(b, V(x0, jy + h0, 0), V(x1, jy + h1, 0), 0.22f, yel);
    pm_beam(b, V(x0, jy, -1.1f), V(x1, jy, -1.1f), 0.22f, yel);
    pm_beam(b, V(x0, jy, 1.1f), V(x1, jy, 1.1f), 0.22f, yel);
    pm_beam(b, V(x0, jy + h0, 0), V(x0, jy, -1.1f), 0.12f, pc_mul(yel, 0.85f));
    pm_beam(b, V(x0, jy + h0, 0), V(x0, jy, 1.1f), 0.12f, pc_mul(yel, 0.85f));
    pm_beam(b, V(x0, jy, -1.1f), V(x1, jy + h1, 0), 0.1f, pc_mul(yel, 0.85f));
    pm_beam(b, V(x0, jy, 1.1f), V(x1, jy + h1, 0), 0.1f, pc_mul(yel, 0.85f));
  }
  pm_beam(b, V(-J * 0.3f, jy, 0), V(0, jy, 0), 0.3f, yel);
  pm_beam(b, V(-J * 0.3f, jy, -1.0f), V(0, jy, -1.0f), 0.2f, yel);
  pm_beam(b, V(-J * 0.3f, jy, 1.0f), V(0, jy, 1.0f), 0.2f, yel);
  /* pendants from the apex to the jib and the counter jib */
  pm_rod(b, V(0, ya, 0), V(J * 0.55f, jy + 1.8f, 0), 0.08f, 4, C_DARK);
  pm_rod(b, V(0, ya, 0), V(J, jy + 0.8f, 0), 0.08f, 4, C_DARK);
  pm_rod(b, V(0, ya, 0), V(-J * 0.3f, jy, 0), 0.08f, 4, C_DARK);
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, -J * 0.27f, jy - 3.0f, 0);
  pm_box(b, 4.0f, 3.0f, 3.0f, C_RUST, pc_mul(C_RUST, 1.2f));                      /* the counterweights */
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, ya + 1.0f, 0);
  pm_box_c(b, 0.7f, 0.7f, 0.7f, glow(rgb(1.0f, 0.1f, 0.08f), 5.0f));
  pm_pop(b);
  return pm_finish(b);
}
/* what hangs from the crane: a hook, four slings and a shipping container (pivot at the top) */
static prop_model m_crane_load(uint32_t seed, float drop) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_METAL);
  pm_rod(b, V(0, 0, 0), V(0, -drop + 3.2f, 0), 0.07f, 4, C_DARK);
  for (int k = 0; k < 4; k++) {
    float sx = (k & 1) ? 1.f : -1.f, sz = (k & 2) ? 1.f : -1.f;
    pm_rod(b, V(0, -drop + 3.2f, 0), V(sx * 2.2f, -drop + 0.1f, sz * 0.9f), 0.04f, 3, C_DARK);
  }
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, -drop, 0);
  pm_box(b, 5.8f, 2.6f, 2.4f, pc_mul(C_TEAL, 0.85f), C_TEAL);
  pm_pop(b);
  return pm_finish(b);
}

/* a flock of birds: a handful of V shapes on a node (the node flaps by squashing in y) */
static prop_model m_flock(uint32_t seed, int n) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < n; i++) {
    float x = (hashf_(seed + (uint32_t)i * 5) - 0.5f) * 26.0f, y = (hashf_(seed + (uint32_t)i * 11) - 0.5f) * 6.0f, z = -fabsf(x) * 0.7f + (hashf_(seed + (uint32_t)i * 3) - 0.5f) * 5.0f;
    pcol c = pc_mix(C_DARK, C_RUST_D, hashf_(seed + (uint32_t)i));
    pm_push(b);
    pm_move(b, x, y, z);
    pm_quad(b, V(0, 0, 0.8f), V(-1.6f, 0.5f, 0.0f), V(-0.2f, 0, -0.4f), V(0, 0, 0.3f), c);
    pm_quad(b, V(0, 0, 0.8f), V(0, 0, 0.3f), V(-0.2f, 0, -0.4f), V(-1.6f, 0.5f, 0.0f), c);
    pm_quad(b, V(0, 0, 0.8f), V(1.6f, 0.5f, 0.0f), V(0.2f, 0, -0.4f), V(0, 0, 0.3f), c);
    pm_quad(b, V(0, 0, 0.8f), V(0, 0, 0.3f), V(0.2f, 0, -0.4f), V(1.6f, 0.5f, 0.0f), c);
    pm_pop(b);
  }
  return pm_finish(b);
}

/* ── the pan's tall pieces: built straight into a world-space chunked builder (a handful of draws for all of them) ── */

/* a lattice mast of `legs` legs (3 or 4) from half-width w0 at the foot to w1 at the top, n bays; bays alternate the two
 * paints (a radio mast's warning bands) when band is set. Built at the builder's origin, up y. */
static void lattice_mast(pm *b, float H, float w0, float w1, int legs, int n, pcol c0, pcol c1, bool band, float thick) {
  for (int j = 0; j < n; j++) {
    float y0 = H * (float)j / (float)n, y1 = H * (float)(j + 1) / (float)n;
    float a0 = lerpf(w0, w1, (float)j / (float)n), a1 = lerpf(w0, w1, (float)(j + 1) / (float)n);
    pcol c = band && ((j >> 1) & 1) ? c1 : c0;
    t3_vec3 q0[4], q1[4];
    for (int k = 0; k < legs; k++) {
      float a = 6.2832f * (float)k / (float)legs + 0.7854f;
      q0[k] = V(cosf(a) * a0, y0, sinf(a) * a0), q1[k] = V(cosf(a) * a1, y1, sinf(a) * a1);
    }
    for (int k = 0; k < legs; k++) {
      int m = (k + 1) % legs;
      pm_beam(b, q0[k], q1[k], thick, c);
      pm_beam(b, q1[k], q1[m], thick * 0.6f, c);
      pm_beam(b, ((j + k) & 1) ? q0[k] : q0[m], ((j + k) & 1) ? q1[m] : q1[k], thick * 0.5f, c);
    }
  }
}

/* a standing stone of salt: two or three craggy drums, leaning a little, pink at the foot and bleached at the top,
 * with crystal blades round its foot and a heaved ring of crust it stands in. Foot at the origin. */
static void salt_stack(pm *b, float H, float R, uint32_t seed) {
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.1f);
  pm_jitter(b, 0.22f);
  /* the foot is stained dark by the brine and the dust the wind drives along the crust, the flanks rose, and the top
   * catches the low dawn sun in gold: a dark base and a bright crown read through the haze from across the pan */
  pcol foot = pc_mix(hex(0x6e3628), hex(0x4e2820), hashf_(seed + 90)), mid = pc_mix(C_SALT, hex(0xd98a74), 0.62f);
  pcol top = pc_mul(hex(0xffd59a), 1.12f);
  int drums = H > 15.0f ? 3 : 2;
  float y = -1.2f, r = R;
  pm_push(b);
  pm_rotz(b, (hashf_(seed) - 0.5f) * 0.16f);
  pm_rotx(b, (hashf_(seed + 1) - 0.5f) * 0.16f);
  for (int d = 0; d < drums; d++) {
    float h = (H + 1.2f) * (d == 0 ? 0.5f : d == 1 ? (drums == 2 ? 0.5f : 0.32f) : 0.18f);
    float r1 = r * (0.72f + 0.12f * hashf_(seed + 7u * (uint32_t)d));
    pm_push(b);
    pm_move(b, (hashf_(seed + 3u * (uint32_t)d) - 0.5f) * r * 0.25f, y, (hashf_(seed + 5u * (uint32_t)d) - 0.5f) * r * 0.25f);
    pm_roty(b, hashf_(seed + 11u * (uint32_t)d) * 6.28f);
    pm_cyl(b, r, r1, h, 7, d == 0 ? foot : mid, d + 1 == drums ? top : d == 0 ? pc_mix(foot, mid, 0.35f) : mid);
    pm_pop(b);
    /* a ledge of crust where one drum sits on the next */
    if (d + 1 < drums) {
      pm_push(b);
      pm_move(b, 0, y + h - 0.3f, 0);
      pm_cyl(b, r1 * 1.18f, r1 * 0.9f, 0.7f, 7, d == 0 ? foot : mid, top);
      pm_pop(b);
    }
    y += h, r = r1 * 0.92f;
  }
  pm_pop(b);
  pm_jitter(b, 0.0f);
  /* crystal blades and a heaved ring of crust at the foot */
  for (int i = 0; i < 6; i++) {
    float a = 6.2832f * ((float)i + hashf_(seed + 20u + (uint32_t)i)) / 6.0f, d = R * (1.05f + 0.35f * hashf_(seed + 30u + (uint32_t)i));
    float h = R * (0.6f + 0.9f * hashf_(seed + 40u + (uint32_t)i));
    pm_push(b);
    pm_move(b, cosf(a) * d, -0.4f, sinf(a) * d);
    pm_rotz(b, cosf(a) * 0.45f), pm_rotx(b, -sinf(a) * 0.45f);
    pm_cyl(b, 0.45f * R * 0.35f, 0.0f, h, 4, foot, top);
    pm_pop(b);
  }
  pm_rock(b, R * 1.7f, 0.18f, 0.3f, foot, mid);
}

/* a signal tower: a three-legged mast in red and white bands, a gallery with two dishes, guy wires to three concrete
 * anchors 0.45 H out, red lamps up its height. Foot at the origin. */
static void signal_tower(pm *b, float H, uint32_t seed) {
  pm_layer(b, PL_MAIN);
  /* stout members: thin ones went to a grey thread in the haze past a couple of hundred metres */
  lattice_mast(b, H, 2.1f, 0.8f, 3, 12, C_RED, C_BONE, true, 0.4f);
  /* the gallery and the dishes */
  pm_push(b);
  pm_move(b, 0, H * 0.78f, 0);
  pm_cyl(b, 2.6f, 2.6f, 0.35f, 10, C_STEEL, C_STEEL_L);
  for (int k = 0; k < 2; k++) {
    float a = (float)k * 2.4f + hashf_(seed) * 6.28f;
    pm_push(b);
    pm_move(b, cosf(a) * 2.2f, 1.6f + (float)k * 1.8f, sinf(a) * 2.2f);
    pm_roty(b, -a), pm_rotz(b, -1.5708f);
    pm_cyl(b, 1.6f, 0.2f, 0.9f, 12, C_BONE, pc_mul(C_BONE, 0.8f));
    pm_pop(b);
  }
  pm_pop(b);
  pm_layer(b, PL_METAL);
  pm_rod(b, V(0, H, 0), V(0, H + 7.0f, 0), 0.12f, 5, C_STEEL_L);
  /* guy wires and their anchors */
  for (int k = 0; k < 3; k++) {
    float a = 6.2832f * (float)k / 3.0f + 0.3f;
    t3_vec3 an = V(cosf(a) * H * 0.45f, 0.4f, sinf(a) * H * 0.45f);
    pm_rod(b, V(cosf(a) * 0.9f, H * 0.82f, sinf(a) * 0.9f), an, 0.05f, 3, C_DARK);
    pm_rod(b, V(cosf(a) * 1.3f, H * 0.45f, sinf(a) * 1.3f), an, 0.05f, 3, C_DARK);
    pm_layer(b, PL_MAIN);
    pm_push(b);
    pm_move(b, an.x, -0.6f, an.z);
    pm_box(b, 2.0f, 1.2f, 2.0f, pc_mul(C_BONE, 0.7f), C_BONE);
    pm_pop(b);
    pm_layer(b, PL_METAL);
  }
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < 3; i++) {
    pm_push(b);
    pm_move(b, 0, i == 2 ? H + 7.2f : H * (0.35f + 0.3f * (float)i), 0);
    pm_box_c(b, 0.7f, 0.7f, 0.7f, glow(rgb(1.0f, 0.1f, 0.06f), 6.0f));
    pm_pop(b);
  }
  /* a hut at the foot */
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 4.5f, 0, -3.0f);
  pm_box(b, 4.0f, 2.8f, 3.2f, C_TEAL, pc_mul(C_TEAL, 1.15f));
  pm_move(b, 0, 2.8f, 0);
  pm_prism(b, 4.6f, 0.9f, 3.8f, C_RUST_D, C_RUST);
  pm_pop(b);
}

/* a wrecked drilling rig: its skid and drawworks shed, the derrick broken at the knee and leaning hard over, its crown
 * block and a spill of drill pipe in the salt. Foot at the origin, the derrick leans toward +x. */
static void wrecked_rig(pm *b, float H, uint32_t seed) {
  pcol rust = pc_mul(C_RUST, 0.95f), dark = pc_mul(C_STEEL, 0.6f);
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, -0.3f, 0);
  pm_box(b, 16.0f, 2.4f, 14.0f, C_RUST_D, rust);                           /* the substructure */
  pm_move(b, -4.5f, 2.4f, 3.5f);
  pm_box(b, 6.0f, 3.6f, 5.0f, C_MUST, pc_mul(C_MUST, 1.1f));             /* the drawworks shed, still painted */
  pm_move(b, 0, 3.6f, 0);
  pm_prism(b, 6.6f, 1.2f, 5.6f, C_RUST_D, C_RUST);
  pm_pop(b);
  /* the stump of the derrick, then the rest of it hinged over at 1/4 of its height */
  pm_layer(b, PL_METAL);
  float knee = H * 0.25f;
  pm_push(b);
  pm_move(b, 0, 2.1f, 0);
  lattice_mast(b, knee, 3.4f, 2.9f, 4, 3, rust, dark, false, 0.3f);
  pm_move(b, 0, knee, 0);
  pm_rotz(b, -0.62f - 0.12f * hashf_(seed));
  pm_roty(b, 0.25f * (hashf_(seed + 1) - 0.5f));
  lattice_mast(b, H * 0.75f, 2.9f, 0.8f, 4, 8, rust, dark, false, 0.24f);
  pm_move(b, 0, H * 0.75f, 0);
  pm_box_c(b, 2.4f, 1.6f, 2.4f, C_STEEL);                                  /* the crown block */
  pm_layer(b, PL_GLOW);
  pm_move(b, 0, 1.1f, 0);
  pm_box_c(b, 0.5f, 0.5f, 0.5f, glow(rgb(1.0f, 0.12f, 0.1f), 5.0f));     /* its lamp, still burning */
  pm_pop(b);
  /* drill pipe spilled across the salt */
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 9; i++) {
    float a = 0.3f * (hashf_(seed + 50u + (uint32_t)i) - 0.5f), z = -6.0f + 1.4f * (float)i;
    pm_rod(b, V(9.0f, 0.3f + 0.25f * (float)(i % 3), z), V(9.0f + 18.0f * cosf(a), 0.3f, z + 18.0f * sinf(a)), 0.22f, 6, i % 3 ? C_STEEL : C_RUST);
  }
}

/* a crashed airship: the envelope lies pitched nose up at about 18 degrees, its tail third torn to the bare ring frames
 * where it ploughed into the salt; a gondola hangs under the middle, fins at the tail, the old livery on its flanks and
 * two mooring lines pinned to the crust. Tail at the origin, the nose toward +z. */
static void crashed_airship(pm *b, uint32_t seed) {
  const float L = 124.0f, R = 13.0f, pitch = 0.4f;
  /* the radius along the hull, y from the tail (0) to the nose (L) */
  float prof[] = { 0.10f, 0.0f, 0.40f, 0.04f, 0.68f, 0.13f, 0.88f, 0.26f, 0.98f, 0.42f, 1.0f, 0.58f, 0.95f, 0.72f, 0.82f, 0.84f, 0.58f, 0.93f, 0.26f, 0.985f, 0.02f, 1.0f };
  float tear = 0.3f;                                  /* the fabric is gone from the tail to here */
  pm_push(b);
  pm_rotx(b, 1.5708f - pitch);                         /* the hull's axis (local y) toward +z, tipped nose up */
  /* the skin: a lathe from the tear to the nose */
  float rz[2 * 11];
  int n = 0;
  for (int i = 0; i < 11; i++) {
    float r = prof[i * 2] * R, y = prof[i * 2 + 1] * L;
    if (prof[i * 2 + 1] < tear) continue;
    if (n == 0) rz[n * 2] = R * 0.96f, rz[n * 2 + 1] = tear * L, n++;
    rz[n * 2] = r, rz[n * 2 + 1] = y, n++;
  }
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.05f);
  /* weathered dark silver darkening toward the torn tail: a pale hull went the colour of the haze across the pan */
  pm_lathe(b, rz, n, 20, hex(0x4f4d4f), hex(0xa79d92));
  /* a dark bulkhead closes the torn end (the skin is one sided) */
  pm_push(b);
  pm_move(b, 0, tear * L - 0.1f, 0);
  pm_cyl(b, R * 0.95f, R * 0.95f, 0.3f, 20, C_DARK, pc_mul(C_RUST_D, 0.8f));
  pm_pop(b);
  /* the livery: two fat bands proud of the skin */
  float bands[3] = { 0.5f, 0.56f, 0.8f };
  pcol bandc[3] = { C_ORNG, C_TEAL, C_ORNG };
  for (int k = 0; k < 3; k++) {
    float y = bands[k] * L;
    float r = R * (bands[k] < 0.6f ? 1.0f : 0.86f);
    pm_push(b);
    pm_move(b, 0, y, 0);
    pm_rotx(b, 1.5708f);
    pm_ring(b, r + 0.1f, 0.9f, 24, 4, bandc[k]);
    pm_pop(b);
  }
  /* the gondola under the middle (local +z is down once tipped over) */
  pm_push(b);
  pm_move(b, 0, L * 0.58f, R + 1.6f);
  pm_box_c(b, 4.4f, 18.0f, 3.4f, pc_mul(C_BONE, 0.9f));
  pm_move(b, 0, 9.4f, 0);
  pm_box_c(b, 3.6f, 1.6f, 2.8f, C_RUST);
  pm_pop(b);
  /* fins: two left standing at the torn tail, one bent */
  for (int k = 0; k < 3; k++) {
    float a = 1.5708f * (float)k + 0.7854f;
    float ca = cosf(a), sa = sinf(a), r0 = R * 0.55f, r1 = R * 1.25f;
    float bend = k == 2 ? 0.6f : 0.0f;
    t3_vec3 p0 = V(ca * r0, 6.0f, sa * r0), p1 = V(ca * r1, 9.0f + bend * 6.0f, sa * r1 + bend * 4.0f);
    t3_vec3 p2 = V(ca * r1, 22.0f, sa * r1), p3 = V(ca * r0, 30.0f, sa * r0);
    pm_quad(b, p0, p1, p2, p3, k & 1 ? C_TEAL : C_ORNG);
    pm_quad(b, p0, p3, p2, p1, k & 1 ? C_TEAL : C_ORNG);
  }
  /* the bare frames: rings and girders where the skin tore away */
  pm_layer(b, PL_METAL);
  int rings = 6;
  for (int i = 0; i <= rings; i++) {
    float y = tear * L * (float)i / (float)rings + 1.0f;
    float t = y / L, r = R * (t < 0.04f ? 0.4f + 7.0f * t : t < 0.13f ? 0.68f + 2.2f * (t - 0.04f) : 0.88f + 0.75f * (t - 0.13f));
    if (r > R) r = R;
    if (i == 1) continue;                              /* one ring snapped clean away */
    pm_push(b);
    pm_move(b, 0, y, 0);
    pm_rotx(b, 1.5708f);
    pm_ring(b, r, 0.28f, 18, 4, C_STEEL);
    pm_pop(b);
  }
  for (int k = 0; k < 10; k++) {
    if (hashf_(seed + (uint32_t)k) < 0.25f) continue;
    float a = 6.2832f * (float)k / 10.0f;
    float y1 = tear * L * (0.75f + 0.25f * hashf_(seed + 9u + (uint32_t)k));
    pm_beam(b, V(cosf(a) * R * 0.45f, 1.0f, sinf(a) * R * 0.45f), V(cosf(a) * R * 0.93f, y1, sinf(a) * R * 0.93f), 0.3f, C_STEEL);
  }
  pm_layer(b, PL_GLOW);
  /* the gondola's windows, still lit */
  for (int i = 0; i < 6; i++) {
    for (int k = -1; k <= 1; k += 2) {
      pm_push(b);
      pm_move(b, (float)k * 2.25f, L * 0.58f - 7.0f + 2.6f * (float)i, R + 1.4f);
      pm_box_c(b, 0.12f, 1.3f, 0.9f, glow(rgb(1.0f, 0.78f, 0.45f), 2.4f));
      pm_pop(b);
    }
  }
  /* a warning beacon still burning on the raised nose: a point of red the eye finds across the pan */
  pm_push(b);
  pm_move(b, 0, L * 0.995f, 0);
  pm_box_c(b, 1.6f, 1.6f, 1.6f, glow(rgb(1.0f, 0.12f, 0.08f), 7.0f));
  pm_pop(b);
  pm_pop(b);
  /* the mooring lines from the nose to two stakes in the salt, and wreckage heaped round the tail */
  float ny = sinf(pitch) * L * 0.86f, nz = cosf(pitch) * L * 0.86f;
  pm_layer(b, PL_METAL);
  for (int k = -1; k <= 1; k += 2) {
    t3_vec3 st_ = V((float)k * 26.0f, 0.2f, nz + 16.0f);
    pm_rod(b, V(0, ny - 3.0f, nz), st_, 0.09f, 4, C_DARK);
    pm_layer(b, PL_MAIN);
    pm_push(b);
    pm_move(b, st_.x, -0.5f, st_.z);
    pm_box(b, 1.6f, 1.2f, 1.6f, C_STEEL, C_STEEL_L);
    pm_pop(b);
    pm_layer(b, PL_METAL);
  }
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < 7; i++) {
    float a = 6.2832f * hashf_(seed + 60u + (uint32_t)i), d = R * (0.9f + 0.8f * hashf_(seed + 70u + (uint32_t)i));
    pm_push(b);
    pm_move(b, cosf(a) * d, -0.2f, sinf(a) * d - 4.0f);
    pm_rock(b, 2.0f + 2.0f * hashf_(seed + 80u + (uint32_t)i), 0.4f, 0.4f, pc_mix(C_SALT, hex(0xd9a59a), 0.5f), C_SALT);
    pm_pop(b);
  }
}

/* ── the horizon: a ring of ridges, and the mirage ──────────────────────────────────────────────────── */

/* a closed ridge line around the course: radius R, peaks `amp` high, colour fading from `low` at the foot to
 * `high` on the crest. The noise samples a circle, so the ring joins up. */
static cw_node ridge_ring(cw_node parent, const char *name, float R, float amp, float base, float feature, pcol low, pcol high, uint32_t seed, float rim_warm) {
  soup s = { 0 };
  int N = 360;
  float prevh = 0;
  int first = -1, prev = -1;
  (void)prevh;
  for (int i = 0; i <= N; i++) {
    float a = 6.2831853f * (float)(i % N) / (float)N;
    float x = CX + cosf(a) * R, z = CZ + sinf(a) * R;
    float u = ground_noise(cosf(a) * 900.0f, sinf(a) * 900.0f, feature, 1.0f, 4, seed);
    float v = ground_noise(cosf(a) * 400.0f + 77.0f, sinf(a) * 400.0f, feature * 0.35f, 1.0f, 2, seed + 4);
    float ridged = 1.0f - fabsf(2.0f * u - 1.0f);
    float h = base + amp * (0.25f + 0.75f * ridged * ridged) * (0.7f + 0.3f * v);
    t3_vec3 nrm = V(-cosf(a), 0.35f, -sinf(a));
    float shade = 0.82f + 0.18f * sinf(a * 5.0f + (float)seed);
    /* faces toward the rising sun catch warm light (the sun is in the +x,+z quarter at dawn) */
    float sunk = fmaxf(0.0f, cosf(a - 0.7f)) * rim_warm;
    pcol hi = pc_mix(pc_mul(high, shade), hex(0xffc9a0), sunk * 0.6f);
    int v0 = sv(&s, V(x, -90.0f, z), nrm, low);
    int v1 = sv(&s, V(x, base + h * 0.35f, z), nrm, pc_mix(low, hi, 0.35f));
    int v2 = sv(&s, V(x, h + base, z), nrm, hi);
    if (first < 0) first = v0;
    if (prev >= 0) {
      st(&s, prev, v0, prev + 1), st(&s, prev + 1, v0, v1);
      st(&s, prev + 1, v1, prev + 2), st(&s, prev + 2, v1, v2);
    }
    prev = v0;
  }
  t3_geometry *g = soup_geo(&s);
  t3_material *m = phys((t3_color){ 1, 1, 1 }, 1.0f, 0);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false;
  me->base.receive_shadow = false;
  cw_node n = hang(parent, name, &me->base);
  t3_release(me);
  t3_release(g);
  t3_release(m);
  return n;
}

/* high streaky cloud: stacked soft ellipses in one translucent mesh (overlap adds up, so the rim fades), lit pink toward
 * the sun and grey-blue away from it. The group drifts slowly round the course in the frame callback. */
static cw_node CLOUDS;
static void build_clouds(cw_node parent) {
  CLOUDS = cw_node_new("Node3D", "Clouds");
  cw_node_add_child(parent, CLOUDS);
  int ncl = SPLIT ? 6 : 16;
  for (int c = 0; c < ncl; c++) {
    float a = 6.2832f * ((float)c + hashf_(1200u + (uint32_t)c)) / (float)ncl;
    float r = 500.0f + 1500.0f * hashf_(1210u + (uint32_t)c);
    float y = 380.0f + 520.0f * hashf_(1220u + (uint32_t)c);
    float cx = CX + cosf(a) * r, cz = CZ + sinf(a) * r;
    float len = 260.0f + 520.0f * hashf_(1230u + (uint32_t)c), wid = 28.0f + 60.0f * hashf_(1240u + (uint32_t)c);
    float rot = 0.6f + 0.5f * (hashf_(1250u + (uint32_t)c) - 0.5f);
    soup s = { 0 };
    int nl = SPLIT ? 5 : 12;
    for (int layer = 0; layer < nl; layer++) {
      float k = 1.0f - 0.19f * (float)layer * (SPLIT ? 1.0f : 0.42f);
      int c0 = sv(&s, V(cx, y, cz), V(0, -1, 0), (pcol){ 1, 1, 1 });
      for (int i = 0; i <= 20; i++) {
        float t = 6.2832f * (float)i / 20.0f;
        float lx = cosf(t) * len * k * (1.0f + 0.12f * sinf(t * 3.0f + (float)c)), lz = sinf(t) * wid * k;
        float wx = cx + lx * cosf(rot) - lz * sinf(rot), wz = cz + lx * sinf(rot) + lz * cosf(rot);
        /* the rim adds nothing: each layer is a soft lens, so a cloud seen from below has no hard polygon edge */
        sv(&s, V(wx, y + 4.0f * sinf(t * 2.0f), wz), V(0, -1, 0), (pcol){ 0, 0, 0 });
        if (i > 0) st(&s, c0, c0 + i + 1, c0 + i);
      }
    }
    t3_geometry *g = soup_geo(&s);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    /* toward the sun (+x +z quarter at dawn) the cloud glows pink and gold, elsewhere it is a cool grey */
    float sunk = 0.5f + 0.5f * cosf(a - 0.75f);
    m->color = (t3_color){ lerpf(0.6f, 2.0f, sunk), lerpf(0.65f, 0.95f, sunk), lerpf(0.85f, 0.85f, sunk) };
    m->transparent = true;
    m->blending = T3_ADDITIVE_BLENDING;
    m->vertex_colors = true;
    m->opacity = SPLIT ? 0.08f : 0.036f;
    m->depth_write = false;
    m->side = T3_DOUBLE_SIDE;
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.cast_shadow = false;
    me->base.receive_shadow = false;
    hang(CLOUDS, "Cloud", &me->base);
    t3_release(me);
    t3_release(g);
    t3_release(m);
  }
}

#define NMIRAGE 10
static t3_material *MIR_M[NMIRAGE];
static float MIR_PH[NMIRAGE];

/* pale shimmering sheets lying on the far pan: the mirage. They are flat, additive-ish and their strength breathes. */
static void build_mirage(cw_node parent) {
  for (int i = 0; i < NMIRAGE; i++) {
    float a = 6.2832f * ((float)i + hashf_(900u + (uint32_t)i) * 0.7f) / (float)NMIRAGE;
    float r = 900.0f + 700.0f * hashf_(910u + (uint32_t)i);
    float cx = CX + cosf(a) * r, cz = CZ + sinf(a) * r;
    float rx = 280.0f + 260.0f * hashf_(920u + (uint32_t)i), rz = 40.0f + 50.0f * hashf_(930u + (uint32_t)i);
    float rot = a + 1.5708f;
    soup s = { 0 };
    int seg = 28;
    int c0 = sv(&s, V(cx, 0, cz), V(0, 1, 0), (pcol){ 1, 1, 1 });
    for (int k = 0; k <= seg; k++) {
      float t = 6.2832f * (float)k / (float)seg;
      float lx = cosf(t) * rx, lz = sinf(t) * rz;
      float wx = cx + lx * cosf(rot) - lz * sinf(rot), wz = cz + lx * sinf(rot) + lz * cosf(rot);
      float gy = base_height(wx, wz) + 0.5f;
      sv(&s, V(wx, gy, wz), V(0, 1, 0), (pcol){ 1, 1, 1 });
      if (k > 0) st(&s, c0, c0 + k + 1, c0 + k);
    }
    /* centre height: the average of the rim */
    float sum = 0;
    for (int k = 1; k <= seg; k++) sum += s.p[(c0 + k) * 3 + 1];
    s.p[c0 * 3 + 1] = sum / (float)seg;
    t3_geometry *g = soup_geo(&s);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = (t3_color){ 0.85f, 1.05f, 1.35f };
    m->vertex_colors = true;
    m->transparent = true;
    m->opacity = 0.2f;
    m->depth_write = false;
    m->side = T3_DOUBLE_SIDE;
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.cast_shadow = false;
    me->base.receive_shadow = false;
    char nm[24];
    snprintf(nm, sizeof nm, "Mirage%d", i);
    hang(parent, nm, &me->base);
    t3_release(me);
    t3_release(g);
    MIR_M[i] = m; /* kept (a reference each) so the frame callback can breathe the opacity */
    MIR_PH[i] = hashf_(940u + (uint32_t)i) * 6.28f;
  }
}

/* ── animated scenery ───────────────────────────────────────────────────────────────────────────────── */

#define MAX_FLAGS 14
#define FLAG_SEGS 4
static struct {
  cw_node seg[FLAG_SEGS];
  float ph, amp, yaw;
} FLAG[MAX_FLAGS];
static int nflag;

#define MAX_BOB 12
static struct {
  cw_node n;
  float ph, rate, amp, y0;
} BOB[MAX_BOB];
static int nbob;

#define MAX_PUMP 12
static struct {
  cw_node beam;
  float ph, yaw, rate;
  t3_vec3 pos;
} PUMP[MAX_PUMP];
static int npump;

#define MAX_WHEEL 16
static struct {
  cw_node wheel;
  float ph, yaw, rate;
  t3_vec3 pos;
} WHEEL[MAX_WHEEL];
static int nwheel;

static struct {
  cw_node load;
  t3_vec3 pos;
  float yaw;
} CRANE;
static bool have_crane;

#define MAX_FLOCK 5
static struct {
  cw_node n;
  float cx, cz, r, y, ph, rate, flap;
} FLOCK[MAX_FLOCK];
static int nflock;

#define MAX_DEVIL 5
static struct {
  cw_node n, column, layer[3];
  float cx, cz, rx, rz, ph, rate;
} DEVIL[MAX_DEVIL];
static int ndevil;

static void add_flag(cw_node parent, t3_vec3 base, float pole_h, float yaw, pcol col, float len, float hgt, uint32_t seed) {
  if (nflag >= MAX_FLAGS) return;
  cw_node pole = model_node(parent, "FlagPole", m_pole(seed, pole_h), -1, true);
  cw_node3d_set_position(pole, base);
  cw_node prev = pole;
  float seg = len / (float)FLAG_SEGS;
  for (int j = 0; j < FLAG_SEGS; j++) {
    cw_node n = model_node(prev, "Flag", m_cloth(seed + (uint32_t)j, seg, hgt, j & 1 ? pc_mul(col, 0.9f) : col), -1, false);
    if (j == 0) cw_node3d_set_position(n, V(0, pole_h - hgt - 0.2f, 0));
    else cw_node3d_set_position(n, V(seg, 0, 0));
    FLAG[nflag].seg[j] = n;
    prev = n;
  }
  /* the first segment is hung off the pole; the pole itself is yawed with the flag */
  FLAG[nflag].ph = hashf_(seed) * 6.28f, FLAG[nflag].amp = 0.5f, FLAG[nflag].yaw = yaw;
  cw_node3d_set_rotation(pole, V(0, yaw, 0));
  nflag++;
}

/* a string of pennants between two points, built into a world-space chunked mesh */
static void bunting(pmc *c, t3_vec3 a, t3_vec3 b, float sag, int n, uint32_t seed) {
  pm *pb = pmc_at(c, a);
  pm_layer(pb, PL_METAL);
  pm_cable(pb, a, b, sag, 0.04f, C_DARK);
  pm_layer(pb, PL_LEAF);
  pcol cols[5] = { C_ORNG, C_TEAL, C_MUST, C_BONE, C_PINK };
  for (int i = 1; i < n; i++) {
    float t = (float)i / (float)n;
    t3_vec3 p = vadd(vadd(vmul(a, 1 - t), vmul(b, t)), V(0, -sag * 4.0f * t * (1 - t), 0));
    pm *pm_ = pmc_at(c, p);
    pm_layer(pm_, PL_LEAF);
    t3_vec3 d = vsub(b, a);
    float l = sqrtf(d.x * d.x + d.z * d.z);
    t3_vec3 dir = l > 0 ? V(d.x / l, 0, d.z / l) : V(1, 0, 0);
    t3_vec3 w = vmul(dir, 0.55f);
    pm_tri(pm_, vsub(p, w), vadd(p, w), vadd(p, V(0, -1.1f, 0)), cols[(hash_u(seed + (uint32_t)i) >> 3) % 5]);
  }
}

/* ── the scenery ────────────────────────────────────────────────────────────────────────────────────── */

static cw_node ROOT, ROOT_SCENE;

/* development aid: SALT_OFF=letters leaves parts of the map out (g ground, s start, r ramps, d derricks, p pan, l life,
 * h horizon rings, c clouds, x mirage, w the road's wear) so a run can show what each costs; unset in a normal run. */
static bool off(char c) {
  const char *e = getenv("SALT_OFF");
  return e && strchr(e, c);
}

/* The outer pan: a ring of coarse polar cells from the edge of the detailed ground out to the foothills. (ground_plane only
 * subdivides cells within a limited search distance of the road, so one huge plane would be a flat sheet.) The ring
 * sits a little below the detailed ground where they overlap, so the two never fight over a pixel. */
static void far_ground(cw_node root) {
  const float r0 = 980.0f, r1 = 2350.0f;
  const int K = SPLIT ? 14 : 40, A = SPLIT ? 72 : 160;
  soup s = { 0 };
  for (int k = 0; k <= K; k++) {
    float r = r0 * powf(r1 / r0, (float)k / (float)K);
    for (int i = 0; i <= A; i++) {
      float a = 6.2831853f * (float)(i % A) / (float)A;
      float x = CX + cosf(a) * r, z = CZ + sinf(a) * r;
      float lower = 1.3f * (1.0f - sstep(r0, 1500.0f, r));
      float h = base_height(x, z) - lower;
      float hx = base_height(x + 8.0f, z) - h, hz = base_height(x, z + 8.0f) - h;
      float l = sqrtf(hx * hx / 64.0f + 1.0f + hz * hz / 64.0f);
      float n = ground_noise(x, z, 300.0f, 1.0f, 2, 12);
      pcol c = pc_mix(GO.tint, GO.tint2, sstep(0.3f, 0.7f, n));
      float fade = 1.0f - 0.25f * sstep(1400.0f, 2300.0f, r);
      sv(&s, V(x, h, z), V(-hx / 8.0f / l, 1.0f / l, -hz / 8.0f / l), pc_mul(c, fade));
      if (k > 0 && i > 0) {
        int b = k * (A + 1) + i, a0 = b - 1, a1 = b - (A + 1) - 1, a2 = b - (A + 1);
        st(&s, a1, a0, b), st(&s, a1, b, a2);
      }
    }
  }
  int nv = s.nv;
  float *uv = malloc(sizeof(float) * 2 * (size_t)nv);
  for (int i = 0; i < nv; i++) uv[i * 2] = s.p[i * 3] / 40.0f, uv[i * 2 + 1] = s.p[i * 3 + 2] / 40.0f;
  t3_geometry *g = soup_geo(&s);
  t3_attribute *ua = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, ua);
  t3_release(ua);
  free(uv);
  t3_material *m = phys((t3_color){ 1, 1, 1 }, 0.85f, 0);
  m->vertex_colors = true;
  tex_apply(m, TEX_ROAD_SALT, 1, 1, 0.9f, 0.8f);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false;
  me->base.receive_shadow = true;
  hang(root, "OuterPan", &me->base);
  t3_release(me);
  t3_release(g);
  t3_release(m);
}

static void build_ground(cw_node root, const track *t) {
  GO = ground_defaults();
  GO.cx = CX, GO.cz = CZ;
  GO.half = 900.0f;
  GO.height = height_fn;
  GO.tex = TEX_ROAD_SALT;
  GO.tex_repeat = 9.0f;
  GO.tint = pc_mul(hex(0xfff0ea), 2.5f);
  GO.tint2 = pc_mul(hex(0xf0c2bc), 2.3f);
  GO.patch_scale = 70.0f;
  GO.rough = 0.82f;
  GO.near_cell = SPLIT ? 96.0f : 64.0f;
  GO.detail_range = SPLIT ? 36.0f : 70.0f;
  GO.follow_road = false;
  GO.darken_edge = 0.22f;
  GO.lumpy = 0.18f;
  GO.seed = 21;
  ground_plane(root, t, &GO);
  far_ground(root);
}

static void salt_start(cw_node root, const track *t, prop_set *ps);
static void salt_ramps(cw_node root, const track *t, prop_set *ps);
static void salt_derricks(cw_node root, const track *t, prop_set *ps);
static void salt_pan(cw_node root, const track *t, prop_set *ps);
static void salt_life(cw_node scene, cw_node root, const track *t);
static void salt_pan_tall(cw_node root, const track *t);
static void salt_road_wear(cw_node root, const track *t);

static void salt_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  TR = t;
  nflag = nbob = npump = nwheel = nflock = ndevil = 0;
  have_crane = false;
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (int i = 0; i < t->n; i++) {
    x0 = fminf(x0, t->s[i].p.x), x1 = fmaxf(x1, t->s[i].p.x);
    z0 = fminf(z0, t->s[i].p.z), z1 = fmaxf(z1, t->s[i].p.z);
  }
  CX = 0.5f * (x0 + x1), CZ = 0.5f * (z0 + z1);
  SPLIT = game_planned_views() > 1;
  ROOT_SCENE = scene;
  ROOT = cw_node_new("Node3D", "Salt");
  cw_node_add_child(scene, ROOT);
  cw_node root = ROOT;

  if (!off('g')) build_ground(root, t);

  /* the dressing the road itself wears */
  dress_style ds = dress_defaults();
  ds.has_colours = true;
  ds.main = pc_mul(C_STEEL, 0.9f), ds.accent = C_ORNG, ds.lamp = rgb(1.0f, 0.85f, 0.6f), ds.seed = 11;
  dress_start_line(root, t, &ds);
  dress_checkpoints(root, t, &ds);
  deco_jump_marks(root, t, &ds);
  deco_corner_signs(root, t, 0.011f, &ds);
  deco_supports(root, t, SUP_TRESTLE, ground_cb, NULL, -PAN_DROP, 2.5f, &ds);

  /* the horizon */
  if (!off('h')) ridge_ring(root, "RidgesFar", 2150.0f, 300.0f, 70.0f, 600.0f, hex(0xb98fb8), hex(0x6e4f94), 3, 0.6f);
  if (!off('h')) ridge_ring(root, "RidgesMid", 1650.0f, 120.0f, 34.0f, 330.0f, hex(0xd9a2a0), hex(0x9a4c48), 9, 1.0f);
  if (!off('x') && !SPLIT) build_mirage(root);
  if (!off('c')) build_clouds(root);

  /* a bare salt lakebed would be a flat page; the pieces below break it up and tell the story. */
  prop_set *ps = ps_new(root, "SaltProps");
  ps_style(ps, PK_MESA, TEX_GROUND_ROCK, hex(0xe6a58a), 1.0f);
  ps_style(ps, PK_SPIRE, TEX_GROUND_ROCK, hex(0xe3b9a8), 1.0f);
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, hex(0xe9c8bb), 1.0f);
  ps_style(ps, PK_DUNE, TEX_GROUND_SAND, hex(0xfbefe8), 1.0f);

  /* mesas and spires on the horizon, far from the road */
  int nfar = SPLIT ? 20 : 46;
  for (int i = 0; i < nfar; i++) {
    float a = 6.2832f * ((float)i + hashf_(100u + (uint32_t)i)) / (float)nfar;
    float r = 950.0f + 600.0f * hashf_(200u + (uint32_t)i);
    float x = CX + cosf(a) * r, z = CZ + sinf(a) * r;
    float sc = 2.0f + 3.2f * hashf_(300u + (uint32_t)i);
    prop_kind k = (i % 3 == 0) ? PK_SPIRE : PK_MESA;
    ps_add(ps, k, on_ground(x, z, -2.0f), hashf_(400u + (uint32_t)i) * 6.28f, sc, 500u + (uint32_t)i);
  }
  /* nearer mesas, a few hundred metres out, big and bright against the haze */
  for (int i = 0; i < 20; i++) {
    float a = 6.2832f * ((float)i + hashf_(110u + (uint32_t)i)) / 20.0f;
    float r = 640.0f + 260.0f * hashf_(210u + (uint32_t)i);
    float x = CX + cosf(a) * r, z = CZ + sinf(a) * r;
    float gap = road_gap(t, x, z, NULL);
    if (gap < 140.0f) continue;
    ps_add(ps, i & 1 ? PK_MESA : PK_SPIRE, on_ground(x, z, -1.5f), hashf_(410u + (uint32_t)i) * 6.28f, 1.1f + 1.4f * hashf_(310u + (uint32_t)i), 600u + (uint32_t)i);
  }
  /* the escarpment: cliff faces along the edge between plateau and pan, seen from both halves of the lap */
  for (int i = 0; i < 9; i++) {
    float u = (float)i / 8.0f;
    float x = lerpf(-70.0f, 560.0f, u) + 55.0f * (hashf_(700u + (uint32_t)i) - 0.5f), z = lerpf(-60.0f, 380.0f, u) + 40.0f * (hashf_(710u + (uint32_t)i) - 0.5f);
    /* sit on the plateau side of the line, away from the road */
    float d = x * 0.649f - (z + 50.0f) * 0.760f;
    x -= 0.649f * (d + 95.0f), z += 0.760f * (d + 95.0f);
    /* A face is 90 m long: tested at its middle alone, one reached into the lanes at its end and the cleaner hid it, a
     * gap in the cliff line. Test its whole footprint (both ends, its foot and its back) and slide it back onto the
     * plateau, then along the line, until all of it is clear of every road. */
    float sc = 0.95f + 0.3f * hashf_(720u + (uint32_t)i);
    float half = 45.0f * sc + 4.0f;
    bool ok = false;
    for (int k = 0; k < 24 && !ok; k++) {
      float back = 12.0f * (float)(k % 6), slide = (float)(k / 6) * 18.0f * ((k / 6) & 1 ? 1.0f : -1.0f);
      float tx = x - 0.649f * back + 0.760f * slide, tz = z + 0.760f * back + 0.649f * slide;
      ok = true;
      for (int e = 0; e < 7 && ok; e++) {
        float u = -half + 2.0f * half * (float)e / 6.0f;
        for (int f = -1; f <= 1 && ok; f++) {
          float v = f < 0 ? -20.0f * sc : f > 0 ? 10.0f * sc : 0.0f; /* the back of the wall, its face, its scree */
          float px = tx + 0.760f * u + 0.649f * v, pz = tz + 0.649f * u - 0.760f * v;
          ok = road_gap(t, px, pz, NULL) > 6.0f;
        }
      }
      if (ok) x = tx, z = tz;
    }
    if (!ok) continue;
    ps_add(ps, PK_CANYON_WALL, on_ground(x, z, -0.5f), yaw_to(V(x, 0, z), V(x + 0.649f, 0, z - 0.760f)), sc, 730u + (uint32_t)i);
  }

  /* loose boulders and low salt dunes across the flats, thin near the road, denser in the middle distance */
  scatter_opts so = scatter_defaults();
  so.kinds[0] = PK_BOULDERS, so.weight[0] = 2, so.kinds[1] = PK_DUNE, so.weight[1] = 2, so.kinds[2] = PK_ROCK, so.weight[2] = 3;
  so.nkinds = 3, so.spacing = SPLIT ? 90.0f : 46.0f, so.lat_min = 18.0f, so.lat_max = 260.0f, so.lat_curve = 1.3f;
  so.ground = ground_cb, so.far_thin = 0.5f, so.seed = 41;
  scatter_along(ps, t, &so);

  /* ── the set pieces ── */
  if (!off('s')) salt_start(root, t, ps);
  if (!off('r')) salt_ramps(root, t, ps);
  if (!off('d')) salt_derricks(root, t, ps);
  if (!off('p')) salt_pan(root, t, ps);
  ps_finish(ps);
  if (!off('w')) salt_road_wear(root, t);
  if (!off('l')) salt_life(scene, root, t);
}


/* ── set piece 1: the start line, the crowd, the flags and the banner strings ───────────────────────── */

typedef struct crowd {
  inst m[2][3];       /* by bob group and shirt */
  cw_node bobnode[2];
} crowd;

static void crowd_add(crowd *c, t3_vec3 base, float yaw, float W, int tiers, uint32_t seed) {
  float rowd = 1.35f, rowh = 0.95f;
  float cy = cosf(yaw), sy = sinf(yaw);
  for (int r = 0; r < tiers; r++) {
    int n = (int)(W / 1.15f);
    for (int i = 0; i < n; i++) {
      uint32_t h = hash_u(seed * 977u + (uint32_t)(r * 131 + i));
      if ((h & 15) < (SPLIT ? 8 : 2)) continue;                           /* gaps in the rows */
      float lx = -W * 0.5f + W * ((float)i + 0.5f) / (float)n + ((float)((h >> 4) & 255) / 255.0f - 0.5f) * 0.5f;
      float lz = -rowd * ((float)r + 0.55f) + ((float)((h >> 12) & 255) / 255.0f - 0.5f) * 0.4f;
      float ly = rowh * (float)(r + 1);
      t3_vec3 p = V(base.x + lx * cy + lz * sy, base.y + ly, base.z - lx * sy + lz * cy);
      int g = (int)((h >> 20) & 1), sh = (int)((h >> 21) % 3);
      float sc = 0.9f + 0.2f * (float)((h >> 24) & 15) / 15.0f;
      inst_add(&c->m[g][sh], p, yaw + ((float)((h >> 8) & 15) / 15.0f - 0.5f) * 0.5f, sc);
    }
  }
}

static void salt_start(cw_node root, const track *t, prop_set *ps) {
  float hw = t->s[0].hw;
  pmc *pc = pmc_new(70, 61);
  crowd cr = { 0 };
  pcol shirts[3] = { C_ORNG, C_TEAL, C_MUST };
  for (int g = 0; g < 2; g++)
    for (int k = 0; k < 3; k++) cr.m[g][k] = inst_new(m_person((uint32_t)(g * 3 + k + 1), shirts[k]), -1, (pcol){ 1, 1, 1 });

  /* three scrap-built stands round the line: a big one on each side and a short one further out. */
  struct { float s, lat, W; int tiers; uint32_t seed; } stands[3] = {
    { 46.0f, -(hw + 17.0f), 38.0f, 6, 3 }, { -12.0f, hw + 17.0f, 42.0f, 6, 4 }, { 120.0f, hw + 17.0f, 26.0f, 5, 5 }
  };
  inst stand_i[3];
  int nst = SPLIT ? 2 : 3;
  for (int i = 0; i < nst; i++) {
    stand_i[i] = inst_new(m_stand(stands[i].seed, stands[i].W, stands[i].tiers), -1, (pcol){ 1, 1, 1 });
    t3_vec3 p = beside(stands[i].s, stands[i].lat, -0.05f);
    float yaw = face_road(stands[i].s, stands[i].lat);
    inst_add(&stand_i[i], p, yaw, 1.0f);
    crowd_add(&cr, p, yaw, stands[i].W - 2.0f, stands[i].tiers, stands[i].seed);
  }
  for (int i = 0; i < nst; i++) inst_finish(&stand_i[i], root, "Stand", true);
  for (int g = 0; g < 2; g++) {
    cr.bobnode[g] = cw_node_new("Node3D", g ? "CrowdB" : "CrowdA");
    cw_node_add_child(root, cr.bobnode[g]);
    for (int k = 0; k < 3; k++) inst_finish(&cr.m[g][k], cr.bobnode[g], "Crowd", true);
    if (nbob < MAX_BOB) BOB[nbob++] = (typeof(BOB[0])){ cr.bobnode[g], g * 2.4f, 5.5f + g * 1.3f, 0.2f, 0 };
  }

  /* flags on tall poles lining the straight, and bunting strings across it */
  pcol flagc[5] = { C_ORNG, C_TEAL, C_MUST, C_RED, C_BONE };
  for (int i = 0; i < (SPLIT ? 4 : 10); i++) {
    float s = -96.0f + 28.0f * (float)i;
    float lat = (i & 1) ? hw + 3.2f : -(hw + 3.2f);
    t3_vec3 p = beside(s, lat, -0.1f);
    float yaw = along_road(s) + 1.5708f * ((i & 1) ? 1.0f : -1.0f);
    yaw = atan2f(sinf(yaw), cosf(yaw));
    add_flag(root, p, 9.5f, yaw, flagc[i % 5], 4.6f, 2.8f, 70u + (uint32_t)i);
  }
  /* four taller masts carry the bunting across the road */
  float ms[4] = { 12.0f, 34.0f, 72.0f, 100.0f };
  for (int i = 0; i < (SPLIT ? 2 : 4); i++) {
    t3_vec3 pl = beside(ms[i], -(hw + 2.2f), 0), pr = beside(ms[i], hw + 2.2f, 0);
    float H = 13.0f;
    for (int k = 0; k < 2; k++) {
      t3_vec3 p = k ? pr : pl;
      pm *b = pmc_at(pc, p);
      pm_push(b);
      pm_move(b, p.x, p.y - 0.4f, p.z);
      pm_layer(b, PL_METAL);
      pm_cyl(b, 0.28f, 0.13f, H + 0.4f, 8, C_STEEL, C_STEEL_L);
      pm_cyl(b, 0.6f, 0.6f, 0.4f, 8, C_DARK, C_DARK);
      pm_pop(b);
    }
    t3_vec3 a = vadd(pl, V(0, H, 0)), c = vadd(pr, V(0, H, 0));
    bunting(pc, a, c, 1.8f, 16, 80u + (uint32_t)i);
  }
  pmc_finish(pc, root, "StartDressing", true, -1);

  /* tyre stacks and drums beside the walls in the S sweep: chicane after chicane of painted rubber */
  for (int i = 0; i < 18; i++) {
    float s = 118.0f + 10.5f * (float)i;
    float side = (i & 1) ? 1.0f : -1.0f;
    float lat = side * (hw + 3.0f + 1.4f * hashf_(800u + (uint32_t)i));
    t3_vec3 p = beside(s, lat, -0.05f);
    ps_add(ps, PK_TYRE_STACK, p, hashf_(810u + (uint32_t)i) * 6.28f, 0.9f + 0.4f * hashf_(820u + (uint32_t)i), 830u + (uint32_t)i);
    if (i % 3 == 0) ps_add(ps, PK_BARREL, beside(s + 3.0f, lat + side * 2.2f, -0.05f), hashf_(840u + (uint32_t)i) * 6.28f, 1.1f, 850u + (uint32_t)i);
  }
  /* back of the stands: a water tower, a billboard, a pile of wrecks and a crate yard */
  ps_add(ps, PK_WATER_TOWER, beside(70.0f, -(hw + 62.0f), -0.2f), 0.6f, 1.25f, 861);
  ps_add(ps, PK_BILLBOARD, beside(-60.0f, hw + 40.0f, -0.2f), face_road(-60.0f, hw + 40.0f), 1.2f, 862);
  for (int i = 0; i < 6; i++) {
    float s = -40.0f + 20.0f * (float)i;
    ps_add(ps, i % 3 == 0 ? PK_WRECK_TRUCK : PK_WRECK_CAR, beside(s, hw + 48.0f + 6.0f * (float)(i & 1), -0.1f), hashf_(870u + (uint32_t)i) * 6.28f, 1.2f, 880u + (uint32_t)i);
  }
}

/* ── set pieces 2 and 4: the salvage-yard ramps and the airliner standing over the first jump ─────── */

static void pile_container_row(prop_set *ps, float s0, float s1, float hw, uint32_t seed) {
  for (float s = s0; s < s1; s += 13.0f) {
    for (int k = -1; k <= 1; k += 2) {
      uint32_t h = hash_u(seed + (uint32_t)(s * 3.0f) * 5u + (uint32_t)(k + 2));
      float lat = k * (hw + 4.6f + 0.8f * hashf_(h));
      int var = (int)(h >> 8) % 2;
      ps_add_ex(ps, PK_CONTAINER, var, 0, beside(s, lat, -0.1f), t3_quat_from_axis_angle(V(0, 1, 0), along_road(s) + (hashf_(h >> 3) - 0.5f) * 0.05f), V(0.9f, 0.9f, 0.9f), (pcol){ 1, 1, 1 }, h);
    }
  }
}

static void salt_ramps(cw_node root, const track *t, prop_set *ps) {
  float hw = t->s[0].hw;
  for (int j = 0; j < t->ngaps && j < 2; j++) {
    float lip = (float)t->gap_from[j], land = (float)t->gap_to[j];
    /* salvaged shipping containers line the climb to the lip, wrecked trucks lean against them and the lip is a
     * kicker of crushed cars; past the landing the same crew has left a pile to look at. */
    pile_container_row(ps, lip - 74.0f, lip - 4.0f, hw, 5000u + (uint32_t)j * 77u);
    for (int k = -1; k <= 1; k += 2) {
      float s = lip - 26.0f;
      t3_vec3 p = beside(s, k * (hw + 12.5f), -0.1f);
      t3_quat q = t3_quat_from_axis_angle(V(0, 1, 0), along_road(s) + (k > 0 ? 0.3f : -0.3f));
      q = t3_quat_multiply(q, t3_quat_from_axis_angle(V(1, 0, 0), -0.2f));
      ps_add_ex(ps, PK_WRECK_TRUCK, 0, 0, vadd(p, V(0, 0.5f, 0)), q, V(1.1f, 1.1f, 1.1f), (pcol){ 1, 1, 1 }, 5100u + (uint32_t)(j * 2 + (k > 0)));
      ps_add(ps, PK_WRECK_CAR, beside(lip - 8.0f, k * (hw + 9.0f), 0.2f), along_road(lip) + 0.5f * (float)k, 1.5f, 5200u + (uint32_t)(j * 2 + (k > 0)));
    }
    for (int i = 0; i < 5; i++) {
      float s = land + 14.0f + 9.0f * (float)i;
      int k = (i & 1) ? 1 : -1;
      ps_add(ps, i % 2 ? PK_WRECK_CAR : PK_CONTAINER, beside(s, k * (hw + 7.0f + 2.5f * (float)(i % 3)), -0.1f), hashf_(5300u + (uint32_t)(i + j * 9)) * 6.28f, 1.3f, 5400u + (uint32_t)(i + j * 9));
    }
    /* glowing bollards down both sides of the last 60 m before the lip, a bloom the eye finds against the pale crust */
    for (float s = lip - 62.0f; s < lip - 2.0f; s += 6.0f)
      for (int k = -1; k <= 1; k += 2) ps_add(ps, PK_BOLLARD, beside(s, k * (hw + 2.4f), 0), 0, 1.3f, 5800u + (uint32_t)s);
    /* a billboard before the lip, its colour field warning of what comes */
    ps_add(ps, PK_BILLBOARD, beside(lip - 108.0f, (j ? 1 : -1) * (hw + 14.0f), -0.2f), face_road(lip - 108.0f, (j ? 1 : -1) * (hw + 14.0f)), 0.9f, 5500u + (uint32_t)j);
  }

  /* the airliner: planted nose-down on the left of the first jump's crest, one wing reaching out over the road. */
  if (t->ngaps > 0) {
    float s = (float)t->gap_from[0] + 16.0f;
    float lat = -(hw + 21.0f);
    tsample a = track_at(t, s);
    float yaw = atan2f(-a.r.z, a.r.x);                       /* the model's +x points to the road's right */
    t3_vec3 p = beside(s, lat, -2.0f);
    inst plane = inst_new(m_airliner(9), TEX_METAL, (pcol){ 1, 1, 1 });
    t3_quat q = t3_quat_from_axis_angle(V(0, 1, 0), yaw);
    q = t3_quat_multiply(q, t3_quat_from_axis_angle(V(0, 0, 1), -0.07f)); /* leaning a little toward the road */
    inst_addq(&plane, p, q, 1.0f);
    inst_finish(&plane, root, "Airliner", true);
    /* wreckage round its foot: a torn-off engine, tyres, crates */
    for (int i = 0; i < 7; i++) {
      float ang = 6.2832f * (float)i / 7.0f;
      t3_vec3 w = V(p.x + cosf(ang) * 9.0f, 0, p.z + sinf(ang) * 9.0f);
      w.y = ground_y(w.x, w.z) - 0.1f;
      ps_add(ps, i % 2 ? PK_BOULDERS : PK_DEBRIS, w, ang, 1.0f + 0.6f * hashf_(5600u + (uint32_t)i), 5700u + (uint32_t)i);
    }
  }

  /* crashed-car launch ramps and painted dune buggies caught mid-leap, along the hills after the first jump */
  pcol paints[4] = { C_ORNG, C_TEAL, C_PINK, C_MUST };
  inst bug[4];
  for (int i = 0; i < 4; i++) bug[i] = inst_new(m_buggy(31u + (uint32_t)i, paints[i]), -1, (pcol){ 1, 1, 1 });
  for (int i = 0; i < 9; i++) {
    float s = 520.0f + 62.0f * (float)i + 14.0f * hashf_(6000u + (uint32_t)i);
    if (s > 1120.0f) s -= 560.0f;
    float side = (i & 1) ? 1.0f : -1.0f;
    float lat = side * (hw + 11.0f + 18.0f * hashf_(6100u + (uint32_t)i));
    t3_vec3 p = beside(s, lat, 0);
    float yaw = along_road(s) + (side > 0 ? 3.1416f : 0.0f) + 0.5f * (hashf_(6200u + (uint32_t)i) - 0.5f);
    t3_quat qy = t3_quat_from_axis_angle(V(0, 1, 0), yaw);
    t3_quat qt = t3_quat_multiply(qy, t3_quat_from_axis_angle(V(1, 0, 0), -0.36f));
    /* the ramp: a tilted container lying on the crust, the buggy on top of it in the air */
    ps_add_ex(ps, PK_CONTAINER, 0, 0, vadd(p, V(0, 0.2f, 0)), qt, V(1.1f, 1.1f, 1.1f), (pcol){ 1, 1, 1 }, 6300u + (uint32_t)i);
    t3_vec3 up = V(0, 2.9f, 0);
    t3_quat qb = t3_quat_multiply(qy, t3_quat_from_axis_angle(V(1, 0, 0), -0.5f));
    inst_addq(&bug[i % 4], vadd(p, vadd(up, vmul(V(sinf(yaw), 0, cosf(yaw)), 1.6f))), qb, 1.0f);
  }
  for (int i = 0; i < 4; i++) inst_finish(&bug[i], root, "Buggies", true);

  /* the hills' wind pumps (the wheels turn) and a scatter of wrecks and tyres by the walls */
  inst tower = inst_new(m_wp_tower(7, 13.0f), -1, (pcol){ 1, 1, 1 });
  for (int i = 0; i < (SPLIT ? 3 : 7); i++) {
    float s = 840.0f + 50.0f * (float)i;
    float side = (i & 1) ? 1.0f : -1.0f;
    float lat = side * (hw + 26.0f + 40.0f * hashf_(6400u + (uint32_t)i));
    t3_vec3 p = beside(s, lat, -0.3f);
    float yaw = 1.0f + 2.0f * hashf_(6500u + (uint32_t)i);
    inst_add(&tower, p, yaw, 1.0f);
    if (nwheel < MAX_WHEEL) {
      cw_node w = model_node(root, "WindWheel", m_wp_wheel(8, 2.6f), -1, true);
      cw_node3d_set_position(w, vadd(p, vadd(V(0, 13.4f, 0), V(sinf(yaw) * 0.8f, 0, cosf(yaw) * 0.8f))));
      WHEEL[nwheel++] = (typeof(WHEEL[0])){ w, hashf_(6600u + (uint32_t)i) * 6.28f, yaw, 0.6f + 0.8f * hashf_(6700u + (uint32_t)i), p };
    }
  }
  inst_finish(&tower, root, "WindPumps", true);
}

/* ── set piece 3: the oil derrick forest on the downhill, nodding pumps and flares ──────────────────── */

static void salt_derricks(cw_node root, const track *t, prop_set *ps) {
  float hw = t->s[0].hw;
  inst der[3];
  for (int i = 0; i < 3; i++) der[i] = inst_new(m_derrick(300u + (uint32_t)i * 17u, 32.0f), -1, (pcol){ 1, 1, 1 });
  int placed = 0;
  for (int i = 0; i < 220 && placed < (SPLIT ? 30 : 84); i++) {
    float s = 1180.0f + 400.0f * hashf_(7000u + (uint32_t)i);
    float side = (hash_u(7100u + (uint32_t)i) & 1) ? 1.0f : -1.0f;
    float lat = side * (hw + 16.0f + powf(hashf_(7200u + (uint32_t)i), 1.5f) * 190.0f);
    t3_vec3 p = beside(s, lat, -0.4f);
    if (road_gap(t, p.x, p.z, NULL) < 15.0f) continue;
    /* keep the forest off the road wherever any other part of the course runs (the pan below the plateau) */
    float sc = 0.7f + 0.7f * hashf_(7300u + (uint32_t)i);
    inst_add(&der[i % 3], p, hashf_(7400u + (uint32_t)i) * 6.28f, sc);
    placed++;
  }
  for (int i = 0; i < 3; i++) inst_finish(&der[i], root, "Derricks", true);

  /* the pump jacks: base static, beam nodding */
  inst base = inst_new(m_pump_base(5), -1, (pcol){ 1, 1, 1 });
  for (int i = 0; i < 7; i++) {
    float s = 1210.0f + 48.0f * (float)i;
    float side = (i & 1) ? 1.0f : -1.0f;
    float lat = side * (hw + 15.0f + 16.0f * hashf_(7500u + (uint32_t)i));
    t3_vec3 p = beside(s, lat, -0.1f);
    float yaw = face_road(s, lat) + 1.5708f * (float)((i % 3) - 1);
    inst_add(&base, p, yaw, 1.0f);
    if (npump < MAX_PUMP) {
      cw_node b = model_node(root, "PumpBeam", m_pump_beam(6), -1, true);
      cw_node3d_set_position(b, vadd(p, V(0, 5.3f, 0)));
      PUMP[npump++] = (typeof(PUMP[0])){ b, hashf_(7600u + (uint32_t)i) * 6.28f, yaw, 1.7f + 0.5f * hashf_(7700u + (uint32_t)i), p };
    }
  }
  inst_finish(&base, root, "PumpBases", true);

  /* tanks, pipe racks and two flare stacks at the foot of the escarpment */
  for (int i = 0; i < 5; i++) {
    float s = 1330.0f + 38.0f * (float)i;
    ps_add(ps, PK_TANK, beside(s, -(hw + 78.0f + 14.0f * (float)(i & 1)), -0.2f), hashf_(7800u + (uint32_t)i) * 6.28f, 1.3f, 7900u + (uint32_t)i);
  }
  for (int i = 0; i < 6; i++) {
    float s = 1260.0f + 30.0f * (float)i;
    ps_add(ps, PK_PIPE_RACK, beside(s, hw + 54.0f, -0.2f), along_road(s), 1.3f, 8000u + (uint32_t)i);
  }
  ps_add(ps, PK_FLARE_STACK, beside(1420.0f, hw + 88.0f, -0.2f), 0.5f, 1.3f, 8100);
  ps_add(ps, PK_FLARE_STACK, beside(1520.0f, -(hw + 96.0f), -0.2f), 2.0f, 1.1f, 8101);
}

/* ── set piece 2 (continued) and the wide bends: the stranded ship, the trawler, the crane, the wrecks ─ */

static void salt_pan(cw_node root, const track *t, prop_set *ps) {
  float hw = t->s[0].hw;
  /* the stranded freighter beside the second jump: bow toward the way the road runs, a list to the road side */
  if (t->ngaps > 1) {
    float lip = (float)t->gap_from[1];
    float lat = hw + 86.0f;
    t3_vec3 pa = beside(lip, lat, 0), pb = beside(lip, -lat, 0);
    if (pb.x > pa.x) lat = -lat;                                      /* the side with the open pan */
    float sc = lat > 0 ? 1.0f : -1.0f;
    float s = lip + 14.0f;
    t3_vec3 p = beside(s, lat, 0);
    float yaw = along_road(s);
    inst ship = inst_new(m_ship(2, 124.0f, 25.0f, 12.0f, 8.0f, 0), TEX_RUST, (pcol){ 1, 1, 1 });
    t3_quat q = t3_quat_from_axis_angle(V(0, 1, 0), yaw);
    q = t3_quat_multiply(q, t3_quat_from_axis_angle(V(1, 0, 0), -0.05f));
    q = t3_quat_multiply(q, t3_quat_from_axis_angle(V(0, 0, 1), 0.2f * -sc));
    inst_addq(&ship, vadd(p, V(0, 3.0f, 0)), q, 1.0f);
    inst_finish(&ship, root, "Freighter", true);
    /* wreckage and a scaffold ladder at its flank */
    for (int i = 0; i < 6; i++) {
      float ss = s - 40.0f + 15.0f * (float)i;
      ps_add(ps, i & 1 ? PK_CONTAINER : PK_CRATE, beside(ss, lat - sc * 30.0f, -0.1f), hashf_(8200u + (uint32_t)i) * 6.28f, 1.4f, 8300u + (uint32_t)i);
    }
  }

  /* the beached trawler on the inside of the wide bends */
  {
    float s = 2300.0f, lat = hw + 50.0f;
    t3_vec3 pa = beside(s, lat, 0), pb = beside(s, -lat, 0);
    float da = (pa.x - CX) * (pa.x - CX) + (pa.z - CZ) * (pa.z - CZ), db = (pb.x - CX) * (pb.x - CX) + (pb.z - CZ) * (pb.z - CZ);
    if (db < da) lat = -lat;
    t3_vec3 p = beside(s, lat, 0);
    inst tr = inst_new(m_ship(3, 46.0f, 11.0f, 5.0f, 3.5f, 1), TEX_RUST, (pcol){ 1, 1, 1 });
    t3_quat q = t3_quat_from_axis_angle(V(0, 1, 0), along_road(s) + 2.6f);
    q = t3_quat_multiply(q, t3_quat_from_axis_angle(V(0, 0, 1), 0.28f));
    q = t3_quat_multiply(q, t3_quat_from_axis_angle(V(1, 0, 0), -0.07f));
    inst_addq(&tr, vadd(p, V(0, 1.8f, 0)), q, 1.0f);
    inst_finish(&tr, root, "Trawler", true);
  }

  /* the lone giant crane, with a container swinging on its hook */
  {
    float s = 2020.0f, lat = hw + 120.0f;
    t3_vec3 pa = beside(s, lat, 0), pb = beside(s, -lat, 0);
    float da = (pa.x - CX) * (pa.x - CX) + (pa.z - CZ) * (pa.z - CZ), db = (pb.x - CX) * (pb.x - CX) + (pb.z - CZ) * (pb.z - CZ);
    t3_vec3 p = da > db ? pa : pb;
    float J = 74.0f, H = 86.0f;
    float yaw = atan2f(-(CZ - p.z) * 0.0f + (track_point(t, ws(s), 0, 0).x - p.x), 0.0f);
    t3_vec3 c0 = track_point(t, ws(s), 0, 0);
    /* the model's +x is the jib: yaw rotates local x to (cos, -sin) so aim it at the road */
    yaw = atan2f(-(c0.z - p.z), c0.x - p.x);
    cw_node cn = model_node(root, "GiantCrane", m_bigcrane(4, H, J), -1, true);
    cw_node3d_set_position(cn, vadd(p, V(0, -1.0f, 0)));
    cw_node3d_set_rotation(cn, V(0, yaw, 0));
    cw_node load = model_node(cn, "CraneLoad", m_crane_load(5, 22.0f), -1, true);
    cw_node3d_set_position(load, V(J * 0.96f, H + 1.2f, 0));
    CRANE.load = load, CRANE.pos = p, CRANE.yaw = yaw;
    have_crane = true;
    /* a stack of containers at its foot and a gantry of crates */
    for (int i = 0; i < 6; i++) {
      float a = 6.2832f * (float)i / 6.0f;
      ps_add(ps, PK_CONTAINER, on_ground(p.x + cosf(a) * 24.0f, p.z + sinf(a) * 24.0f, -0.1f), a, 1.3f, 8400u + (uint32_t)i);
    }
  }

  /* wrecked airliners lying in the salt (smaller, on their bellies), silos, tanks and water towers on the pan */
  {
    inst wreck = inst_new(m_airliner(21), TEX_METAL, (pcol){ 1, 1, 1 });
    float ws_[2] = { 2200.0f, 2610.0f };
    for (int i = 0; i < 2; i++) {
      float lat = (i ? -1.0f : 1.0f) * (hw + 70.0f + 30.0f * (float)i);
      t3_vec3 p = beside(ws_[i], lat, 0);
      t3_quat q = t3_quat_from_axis_angle(V(0, 1, 0), along_road(ws_[i]) + 0.4f + (float)i);
      q = t3_quat_multiply(q, t3_quat_from_axis_angle(V(1, 0, 0), 1.5708f - 0.1f));
      inst_addq(&wreck, vadd(p, V(0, 2.3f, 0)), q, 0.55f);
    }
    inst_finish(&wreck, root, "WreckedAirliners", true);
  }
  prop_kind yard[6] = { PK_SILO, PK_TANK, PK_WATER_TOWER, PK_CONTAINER, PK_WRECK_TRUCK, PK_PIPE_RACK };
  for (int i = 0; i < (SPLIT ? 8 : 16); i++) {
    float s = 1760.0f + 70.0f * (float)i;
    float side = (i & 1) ? 1.0f : -1.0f;
    float lat = side * (hw + 55.0f + 80.0f * hashf_(8500u + (uint32_t)i));
    t3_vec3 p = beside(s, lat, -0.2f);
    if (road_gap(t, p.x, p.z, NULL) < 30.0f) continue;
    ps_add(ps, yard[i % 6], p, hashf_(8600u + (uint32_t)i) * 6.28f, 1.1f + 0.5f * hashf_(8700u + (uint32_t)i), 8800u + (uint32_t)i);
  }

  /* wind pumps with turning wheels on the pan */
  {
    inst tower = inst_new(m_wp_tower(17, 14.0f), -1, (pcol){ 1, 1, 1 });
    for (int i = 0; i < (SPLIT ? 4 : 9); i++) {
      float s = 1720.0f + 118.0f * (float)i;
      float side = (i & 1) ? 1.0f : -1.0f;
      float lat = side * (hw + 38.0f + 70.0f * hashf_(8900u + (uint32_t)i));
      t3_vec3 p = beside(s, lat, -0.3f);
      if (road_gap(t, p.x, p.z, NULL) < 25.0f) continue;
      float yaw = 0.8f + 2.5f * hashf_(9000u + (uint32_t)i);
      inst_add(&tower, p, yaw, 1.1f);
      if (nwheel < MAX_WHEEL) {
        cw_node w = model_node(root, "PanWheel", m_wp_wheel(18, 2.9f), -1, true);
        cw_node3d_set_position(w, vadd(p, vadd(V(0, 15.2f, 0), V(sinf(yaw) * 0.9f, 0, cosf(yaw) * 0.9f))));
        WHEEL[nwheel++] = (typeof(WHEEL[0])){ w, hashf_(9100u + (uint32_t)i) * 6.28f, yaw, 0.5f + 0.9f * hashf_(9200u + (uint32_t)i), p };
      }
    }
    inst_finish(&tower, root, "PanPumps", true);
  }

  /* pylons marching across the pan and their cables */
  {
    t3_vec3 pts[9];
    int np = 0;
    float yp = 0;
    for (int i = 0; i < 9; i++) {
      float s = 1980.0f + 90.0f * (float)i;
      float lat = (i < 5 ? -1.0f : 1.0f) * (hw + 150.0f);
      t3_vec3 p = beside(s, lat, -0.3f);
      if (road_gap(t, p.x, p.z, NULL) < 40.0f) continue;
      ps_add(ps, PK_PYLON, p, along_road(s) + 1.5708f, 1.5f, 9300u + (uint32_t)i);
      yp = 30.0f * 1.5f * 0.84f;
      pts[np++] = vadd(p, V(0, yp, 0));
    }
    if (np > 1) deco_cables(root, pts, np, 2.6f, 0.06f, C_DARK);
  }

  /* the salvage yard thickens along the hills and the pan: wrecks, drums, crates, pipes, hay and boulders in loose groups */
  {
    scatter_opts o = scatter_defaults();
    o.kinds[0] = PK_WRECK_CAR, o.weight[0] = 3, o.kinds[1] = PK_BARREL, o.weight[1] = 3, o.kinds[2] = PK_CRATE, o.weight[2] = 2, o.kinds[3] = PK_PIPE, o.weight[3] = 1;
    o.kinds[4] = PK_HAY, o.weight[4] = 2, o.kinds[5] = PK_BOULDERS, o.weight[5] = 1, o.kinds[6] = PK_CONTAINER, o.weight[6] = 1, o.kinds[7] = PK_DEBRIS, o.weight[7] = 2;
    o.nkinds = 8, o.spacing = SPLIT ? 52.0f : 26.0f, o.lat_min = 9.0f, o.lat_max = 70.0f, o.lat_curve = 1.6f, o.margin = 2.0f, o.scale_min = 0.9f, o.scale_max = 1.4f;
    o.ground = ground_cb, o.far_thin = 0.6f, o.lod_dist = 90.0f;
    o.from_s = 600.0f, o.to_s = 1200.0f, o.seed = 101;
    scatter_along(ps, t, &o);
    o.from_s = 1740.0f, o.to_s = 2900.0f, o.spacing = SPLIT ? 68.0f : 34.0f, o.lat_max = 110.0f, o.seed = 102;
    scatter_along(ps, t, &o);
  }
  /* telegraph poles beside the pan road: a steady rhythm of verticals, with a wire */
  {
    t3_vec3 pts[40];
    int np = 0;
    pmc *pc = pmc_new(90, 71);
    for (float s = 1760.0f; s < 2860.0f && np < 40; s += 42.0f) {
      t3_vec3 p = beside(s, -(hw + 9.0f), -0.2f);
      pm *b = pmc_at(pc, p);
      pm_push(b);
      pm_move(b, p.x, p.y, p.z);
      pm_layer(b, PL_MAIN);
      pm_cyl(b, 0.18f, 0.12f, 9.5f, 6, hex(0x5c4636), hex(0x7a6048));
      pm_beam(b, V(-1.4f, 8.8f, 0), V(1.4f, 8.8f, 0), 0.14f, hex(0x5c4636));
      pm_beam(b, V(-1.0f, 7.6f, 0), V(1.0f, 7.6f, 0), 0.12f, hex(0x5c4636));
      pm_pop(b);
      pts[np++] = vadd(p, V(0, 8.8f, 0));
    }
    pmc_finish(pc, root, "TelegraphPoles", true, -1);
    if (np > 1) deco_cables(root, pts, np, 0.5f, 0.03f, C_DARK);
  }

  /* tyre-stack chicanes, drums and cones along the walls of the bends and the pan */
  {
    scatter_opts o = scatter_defaults();
    o.kinds[0] = PK_TYRE_STACK, o.weight[0] = 3, o.kinds[1] = PK_BARREL, o.weight[1] = 2, o.kinds[2] = PK_CONE, o.weight[2] = 2, o.kinds[3] = PK_TYRE, o.weight[3] = 1;
    o.nkinds = 4, o.spacing = SPLIT ? 40.0f : 24.0f, o.lat_min = 1.2f, o.lat_max = 6.5f, o.margin = 0.5f, o.scale_min = 0.9f, o.scale_max = 1.35f;
    o.ground = ground_cb, o.from_s = 560.0f, o.to_s = 1180.0f, o.seed = 91;
    scatter_along(ps, t, &o);
    o.from_s = 1700.0f, o.to_s = 2800.0f, o.spacing = 30.0f, o.seed = 92;
    scatter_along(ps, t, &o);
  }
  salt_pan_tall(root, t);
}

/* a spot for a piece of radius `r` (its whole footprint, guy wires and spills included) clear of every part of the
 * course by `clear` metres: the point beside the road at s, lat, pushed further out until it fits (false: none did) */
static bool clear_spot(const track *t, float s, float lat, float r, float clear, t3_vec3 *out) {
  float side = lat < 0 ? -1.0f : 1.0f;
  for (int k = 0; k < 8; k++) {
    t3_vec3 p = beside(s, lat + side * 10.0f * (float)k, 0);
    bool ok = true;
    for (int i = 0; i < 9 && ok; i++) {
      float a = 6.2832f * (float)i / 8.0f, rr = i == 8 ? 0.0f : r;
      ok = road_gap(t, p.x + cosf(a) * rr, p.z + sinf(a) * rr, NULL) > clear;
    }
    if (ok) {
      *out = p;
      return true;
    }
  }
  return false;
}

/* The pan below the escarpment was flat from kart height: everything on it stood lower than a kart's horizon. Tall
 * verticals now stand out on the crust at every distance: standing stones of salt in clusters, red and white signal
 * towers on guy wires, wrecked drilling rigs with their derricks broken over, and a crashed airship pitched nose up
 * on the inside of the long bends. All of it goes into one chunked builder with big cells: a few draws per view. */
static void salt_pan_tall(cw_node root, const track *t) {
  float hw = t->s[0].hw;
  pmc *c = pmc_new(320.0f, 8800u);
  int placed = 0;
  /* the airship, on the inside of the long bends, lying roughly along the road so its whole length shows */
  {
    float s = 2330.0f, lat = hw + 120.0f;
    t3_vec3 pa = beside(s, lat, 0), pb = beside(s, -lat, 0);
    float da = (pa.x - CX) * (pa.x - CX) + (pa.z - CZ) * (pa.z - CZ), db = (pb.x - CX) * (pb.x - CX) + (pb.z - CZ) * (pb.z - CZ);
    if (db < da) lat = -lat;
    t3_vec3 p;
    if (clear_spot(t, s, lat, 75.0f, 12.0f, &p)) {
      /* the tail lies up the road from the middle, so the nose points along the way the karts run */
      float yaw = along_road(s) + 0.35f * (lat > 0 ? -1.0f : 1.0f);
      t3_vec3 tail = vsub(p, V(sinf(yaw) * 55.0f, 0, cosf(yaw) * 55.0f));
      tail.y = ground_y(tail.x, tail.z) - 2.5f;
      pm *b = pmc_at(c, p);
      pm_push(b);
      pm_move(b, tail.x, tail.y, tail.z);
      pm_roty(b, yaw);
      crashed_airship(b, 8810u);
      pm_pop(b);
      placed++;
      cw_info("salt: the airship's middle at (%.0f, %.0f)", p.x, p.z);
    }
  }
  /* signal towers and wrecked rigs alternate sides down the pan */
  struct { float s, lat; int kind; float H; } big[] = {
    { 1815.0f, -34.0f, 0, 66.0f }, { 1960.0f, 30.0f, 1, 46.0f }, { 2140.0f, -40.0f, 0, 72.0f }, { 2290.0f, -30.0f, 1, 44.0f },
    { 2470.0f, 36.0f, 0, 68.0f }, { 2600.0f, -32.0f, 1, 48.0f }, { 2760.0f, 40.0f, 0, 62.0f },
  };
  for (int i = 0; i < (int)(sizeof big / sizeof big[0]); i++) {
    float lat = big[i].lat < 0 ? big[i].lat - hw : big[i].lat + hw;
    float r = big[i].kind == 0 ? big[i].H * 0.47f + 3.0f : 30.0f;
    t3_vec3 p;
    if (!clear_spot(t, big[i].s, lat, r, 8.0f, &p)) continue;
    pm *b = pmc_at(c, p);
    pm_push(b);
    pm_move(b, p.x, p.y - 0.2f, p.z);
    /* a rig leans away from the road, so its broken derrick never reaches over the lanes */
    pm_roty(b, big[i].kind == 0 ? hashf_(8820u + (uint32_t)i) * 6.28f : face_road(big[i].s, lat) + 1.5708f);
    if (big[i].kind == 0) signal_tower(b, big[i].H, 8830u + (uint32_t)i);
    else wrecked_rig(b, big[i].H, 8840u + (uint32_t)i);
    pm_pop(b);
    placed++;
  }
  /* standing stones of salt in clusters of one to three, near and far, both sides */
  for (int i = 0; i < 30; i++) {
    float s = 1760.0f + 1140.0f * ((float)i + hashf_(8850u + (uint32_t)i)) / 30.0f;
    float side = (hash_u(8860u + (uint32_t)i) & 1) ? 1.0f : -1.0f;
    float lat = side * (hw + 16.0f + 90.0f * powf(hashf_(8870u + (uint32_t)i), 1.6f));
    int nst = 1 + (int)(hash_u(8880u + (uint32_t)i) % 3);
    t3_vec3 p;
    if (!clear_spot(t, s, lat, 14.0f, 6.0f, &p)) continue;
    for (int k = 0; k < nst; k++) {
      uint32_t h = 8890u + (uint32_t)(i * 5 + k);
      float H = (k == 0 ? 16.0f : 8.0f) + 18.0f * hashf_(h);
      float R = 1.8f + 0.11f * H * (0.7f + 0.6f * hashf_(h + 1));
      float a = hashf_(h + 2) * 6.28f, d = k == 0 ? 0.0f : 6.0f + 4.0f * hashf_(h + 3);
      t3_vec3 q = on_ground(p.x + cosf(a) * d, p.z + sinf(a) * d, 0);
      pm *b = pmc_at(c, q);
      pm_push(b);
      pm_move(b, q.x, q.y, q.z);
      salt_stack(b, H, R, h);
      pm_pop(b);
      placed++;
    }
  }
  pmc_finish(c, root, "PanTall", true, -1);
  cw_info("salt: %d tall pieces on the pan", placed);
}

/* ── the road's wear: the one hex-plate texture ran the whole 3 km lap ──────────────────────────────────
 * Two skins laid a few centimetres over the deck, in the road's own uv (u across the full width, v = s / tile_v) so
 * they line up with it plate for plate:
 *   - the same salt texture with vertex colours: 1 at a patch's edge (exactly the road below it, so no seam shows) and
 *     a tint inside: dark rubber in the wheel tracks (heavier where karts brake for a bend), bright wind-blown salt
 *     drifting across the deck in diagonal tongues on the open pan, and a brown stain that leads into
 *   - the dirt road through the derrick field below the escarpment (the dirt texture), its two ends ragged tongues.
 * Neither touches the start line, a boost strip, a checkpoint, a jump's run-up or its landing marks. */

typedef struct rwbuf {
  float *p, *n, *c, *uv;
  uint32_t *ix;
  int nv, ni, cv, ci;
} rwbuf;
static int rw_v(rwbuf *b, t3_vec3 p, t3_vec3 n, pcol c, float u, float v) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 4096;
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cv), b->n = realloc(b->n, sizeof(float) * 3 * (size_t)b->cv);
    b->c = realloc(b->c, sizeof(float) * 3 * (size_t)b->cv), b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cv);
  }
  int i = b->nv++;
  b->p[i * 3] = p.x, b->p[i * 3 + 1] = p.y, b->p[i * 3 + 2] = p.z;
  b->n[i * 3] = n.x, b->n[i * 3 + 1] = n.y, b->n[i * 3 + 2] = n.z;
  b->c[i * 3] = c.r, b->c[i * 3 + 1] = c.g, b->c[i * 3 + 2] = c.b;
  b->uv[i * 2] = u, b->uv[i * 2 + 1] = v;
  return i;
}
/* one point of a skin across the road */
typedef struct rwpt {
  t3_vec3 p, n;
  pcol c;
  float u, v;
} rwpt;
/* the quad between two rows' points c and c + 1 (a0 a1 on the row behind, b0 b1 on the row ahead), facing up */
static void rw_quad(rwbuf *b, const rwpt *a0, const rwpt *a1, const rwpt *b0, const rwpt *b1, int tris) {
  int i0 = rw_v(b, a0->p, a0->n, a0->c, a0->u, a0->v), i1 = rw_v(b, a1->p, a1->n, a1->c, a1->u, a1->v);
  int j0 = rw_v(b, b0->p, b0->n, b0->c, b0->u, b0->v), j1 = rw_v(b, b1->p, b1->n, b1->c, b1->u, b1->v);
  if (b->ni + 6 > b->ci) b->ci = b->ci ? b->ci * 2 : 8192, b->ix = realloc(b->ix, sizeof(uint32_t) * (size_t)b->ci);
  /* wound so the front faces up the road's normal */
  bool up = vdot(vcross(vsub(b0->p, a0->p), vsub(b1->p, a0->p)), a0->n) > 0;
  uint32_t q[6] = { (uint32_t)i0, (uint32_t)j0, (uint32_t)j1, (uint32_t)i0, (uint32_t)j1, (uint32_t)i1 };
  if (!up) q[1] = (uint32_t)j1, q[2] = (uint32_t)j0, q[4] = (uint32_t)i1, q[5] = (uint32_t)j1;
  /* tris: bit 0 the triangle a0 b0 b1, bit 1 the triangle a0 b1 a1 */
  if (tris & 1) memcpy(b->ix + b->ni, q, sizeof(uint32_t) * 3), b->ni += 3;
  if (tris & 2) memcpy(b->ix + b->ni, q + 3, sizeof(uint32_t) * 3), b->ni += 3;
}
/* which of a quad's two triangles have every corner over thr (a skin with an edge cut on the diagonals, not in steps) */
static int rw_tris(float a0, float a1, float b0, float b1, float thr) {
  return (fminf(a0, fminf(b0, b1)) > thr ? 1 : 0) | (fminf(a0, fminf(b1, a1)) > thr ? 2 : 0);
}
/* the chunk's mesh, hung under parent (nothing if no quad was laid) */
static void rw_finish(rwbuf *b, cw_node parent, const char *name, t3_material *m) {
  if (b->ni > 0) {
    t3_geometry *g = t3_geometry_new();
    t3_attribute *a;
    a = t3_attribute_new(T3_FLOAT32, b->p, b->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
    a = t3_attribute_new(T3_FLOAT32, b->n, b->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
    a = t3_attribute_new(T3_FLOAT32, b->c, b->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
    a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2), t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
    a = t3_attribute_new(T3_UINT32, b->ix, b->ni, 1), t3_geometry_set_index(g, a), t3_release(a);
    t3_geometry_compute_bounding_sphere(g);
    t3_retain(m);
    add_mesh(parent, name, g, m, false, true);
  }
  free(b->p), free(b->n), free(b->c), free(b->uv), free(b->ix);
  memset(b, 0, sizeof *b);
}

#define RW_COLS 20
#define RW_DIRT0 1405.0f /* the dirt road through the derrick field, from the foot of the escarpment */
#define RW_DIRT1 1552.0f

/* 0 where nothing may be laid (paint and marks of the course's own), easing to 1 over a few metres */
static float rw_allowed(const track *t, int i) {
  float s = t->s[i].s, L = t->length, k = 1.0f;
  if (s < 40.0f || s > L - 30.0f) return 0;
  for (int d = -12; d <= 12; d++) {
    int j = i + d;
    if (j < 0 || j >= t->n) continue;
    uint32_t f = t->s[j].flags;
    float ds = fabsf(t->s[j].s - s);
    if (f & (TF_BOOST | TF_CHECK)) k = fminf(k, sstep(3.0f, 9.0f, ds));
  }
  return k;
}

static void salt_road_wear(cw_node root, const track *t) {
  const float tile = 8.0f; /* the map's mesh.tile_v */
  /* the run-ups, flights and landing marks of the jumps, as s ranges: nothing is laid there */
  float keep0[8], keep1[8];
  int nkeep = 0;
  for (int i = 1; i < t->n && nkeep < 8; i++)
    if ((t->s[i].flags & TF_RAMP) && !(t->s[i - 1].flags & TF_RAMP)) {
      int j = i;
      while (j < t->n && !(t->s[j].flags & TF_GAP)) j++;
      while (j < t->n && (t->s[j].flags & TF_GAP)) j++;
      keep0[nkeep] = t->s[i].s - 15.0f, keep1[nkeep] = (j < t->n ? t->s[j].s : t->length) + 75.0f, nkeep++;
    }
  /* the salt skin's material is the road's own (map_salt.look), with vertex colours; the dirt is the dirt texture */
  t3_material *ms = phys(lin(0.9f, 0.82f, 0.78f), 1.0f, 0);
  tex_apply(ms, TEX_ROAD_SALT, 1, 1, 1.0f, 1.0f);
  ms->roughness = 1.0f, ms->vertex_colors = true;
  /* sun-dried ochre dust, not mud: the dirt texture lifted well above its own brown */
  t3_material *md = phys((t3_color){ 1.2f, 0.95f, 0.72f }, 1.0f, 0);
  tex_apply(md, TEX_ROAD_DIRT, 1, 1, 1.0f, 1.0f);
  md->roughness = 1.0f, md->vertex_colors = true;
  /* the drifts: wind-rippled grains (the sand texture) bleached to the salt's white and a little brighter than the deck */
  t3_material *mw = phys((t3_color){ 1.2f, 1.46f, 2.0f }, 1.0f, 0); /* cancels the sand's tan */
  tex_apply(mw, TEX_GROUND_SAND, 1, 1, 1.0f, 1.0f);
  mw->roughness = 1.0f, mw->vertex_colors = true;

  const pcol WEAR = { 0.52f, 0.49f, 0.49f }, DRIFT = { 1.34f, 1.33f, 1.4f }, STAIN = { 0.86f, 0.6f, 0.42f };
  const float CHUNK = 420.0f;
  rwbuf bs = { 0 }, bd = { 0 }, bw = { 0 };
  float st_w = 0, st_d = 0;
  int st_n = 0;
  float cs = t->s[0].s;
  int nq = 0, nd = 0, nw = 0, nchunk = 0;
  rwpt prev_s[RW_COLS + 1], prev_d[RW_COLS + 1], cur_s[RW_COLS + 1], cur_d[RW_COLS + 1], prev_w[RW_COLS + 1], cur_w[RW_COLS + 1];
  bool have_prev = false;
  float prev_dm[RW_COLS + 1], prev_sm[RW_COLS + 1], prev_wm[RW_COLS + 1];
  for (int i = 0; i <= t->n; i++) {
    bool wrap = i == t->n;
    if (wrap && !t->circuit) break;
    const tsample *a = &t->s[wrap ? 0 : i];
    float s = wrap ? t->length : a->s;
    bool gone = (a->flags & (TF_GAP | TF_BRANCH | TF_TUNNEL | TF_LOOP)) != 0;
    float allow = wrap ? 0.0f : rw_allowed(t, i);
    for (int k = 0; k < nkeep; k++) allow = fminf(allow, 1.0f - sstep(keep0[k], keep0[k] + 8.0f, s) * (1.0f - sstep(keep1[k] - 8.0f, keep1[k], s)));
    /* a new chunk every CHUNK metres: each chunk is its own mesh and culls alone (every quad carries its own four
     * points, so the strip runs on across the cut) */
    if (s - cs > CHUNK) {
      char nm[24];
      snprintf(nm, sizeof nm, "RoadWear_%d", nchunk);
      rw_finish(&bs, root, nm, ms);
      snprintf(nm, sizeof nm, "RoadDirt_%d", nchunk);
      rw_finish(&bd, root, nm, md);
      snprintf(nm, sizeof nm, "RoadDrift_%d", nchunk++);
      rw_finish(&bw, root, nm, mw);
      cs = s;
    }
    if (gone) {
      have_prev = false;
      continue;
    }
    /* how hard karts brake here: the heading turns ahead (the next 40 m) */
    int ja = i + 20 < t->n ? i + 20 : t->n - 1;
    float turn = 1.0f - vdot(a->f, t->s[ja].f);
    float brake = sstep(0.01f, 0.08f, turn);
    float hw = a->hw - 0.9f; /* the kerbs cover the last 0.9 m */
    float sm[RW_COLS + 1], dm[RW_COLS + 1], wm[RW_COLS + 1];
    for (int c = 0; c <= RW_COLS; c++) {
      float lat = -hw + 2.0f * hw * (float)c / (float)RW_COLS;
      t3_vec3 p = vadd(a->p, vmul(a->r, lat));
      /* the wheel tracks: two dark bands either side of the racing line, patchy along the road, heavier into bends */
      float band = expf(-((fabsf(lat) - 2.8f) * (fabsf(lat) - 2.8f)) / 2.2f) + 0.45f * expf(-((fabsf(lat) - 6.5f) * (fabsf(lat) - 6.5f)) / 1.6f);
      float patchy = sstep(-0.3f, 0.15f, ground_noise(s, lat * 0.25f, 55.0f, 1.0f, 2, 61u) + 0.35f * brake);
      float wear = fminf(band, 1.0f) * patchy * (0.7f + 0.3f * brake);
      /* salt blown across the deck: diagonal tongues, many on the pan (the second half), a few on the plateau */
      float pan = sstep(1700.0f, 1830.0f, s) * (1.0f - sstep(2850.0f, 2950.0f, s));
      float plateau = sstep(560.0f, 640.0f, s) * (1.0f - sstep(1250.0f, 1330.0f, s));
      float ph = (s + lat * 0.9f) / 31.0f + 0.35f * ground_noise(p.x, p.z, 40.0f, 1.0f, 2, 67u);
      float tongue = sstep(0.62f, 0.9f, 0.5f + 0.5f * sinf(ph * 6.2832f)) * sstep(-0.3f, 0.1f, ground_noise(p.x, p.z, 22.0f, 1.0f, 3, 71u));
      float drift = tongue * fmaxf(pan, 0.55f * plateau) * sstep(-0.4f, 0.0f, ground_noise(s, 0, 140.0f, 1.0f, 2, 73u));
      st_w += wear, st_d += drift, st_n++;
      /* the dirt road: the stain leads in over 30 m, the dirt itself starts in ragged tongues (each column its own) */
      float jag = 9.0f * (0.5f + 0.5f * ground_noise(lat * 3.0f, 11.0f, 4.0f, 1.0f, 2, 79u));
      float jag1 = 9.0f * (0.5f + 0.5f * ground_noise(lat * 3.0f, 37.0f, 4.0f, 1.0f, 2, 83u));
      float stain = sstep(RW_DIRT0 - 30.0f, RW_DIRT0 + 4.0f, s) * (1.0f - sstep(RW_DIRT1 - 4.0f, RW_DIRT1 + 30.0f, s));
      float dirt = s > RW_DIRT0 + jag && s < RW_DIRT1 - jag1 ? 1.0f : 0.0f;
      /* the edges of the deck stay as built, so the skins never meet the kerbs */
      float edge = 1.0f - sstep(hw - 1.5f, hw, fabsf(lat));
      float w = allow * edge;
      pcol col = { 1, 1, 1 };
      col = pc_mix(col, WEAR, fminf(wear * w * (1.0f - stain), 1.0f));
      col = pc_mix(col, DRIFT, fminf(drift * w * (1.0f - stain), 1.0f));
      col = pc_mix(col, STAIN, stain * w);
      sm[c] = fmaxf(fmaxf(wear, drift), stain) * w;
      dm[c] = dirt * allow;
      wm[c] = drift * w * (1.0f - stain);
      t3_vec3 n = a->u;
      float u = (lat + a->hw) / (2.0f * a->hw), v = s / tile;
      cur_s[c] = (rwpt){ vadd(p, vmul(n, 0.03f)), n, col, u, v };
      /* the dirt's own colours: darker oily blotches where the trucks stood */
      float oil = sstep(0.1f, 0.6f, ground_noise(p.x, p.z, 9.0f, 1.0f, 2, 89u));
      cur_d[c] = (rwpt){ vadd(p, vmul(n, 0.05f)), n, pc_mix((pcol){ 1, 1, 1 }, (pcol){ 0.55f, 0.5f, 0.48f }, 0.45f * oil), u, v };
      /* the drift's grains: their ripples run across the wind, a repeat every 5 m */
      cur_w[c] = (rwpt){ vadd(p, vmul(n, 0.04f)), n, pc_mul((pcol){ 1, 1, 1 }, 0.9f + 0.1f * drift), lat / 5.0f, s / 5.0f };
    }
    if (have_prev) {
      for (int c = 0; c < RW_COLS; c++) {
        /* a quad whose four corners are all the plain road is not laid */
        if (fmaxf(fmaxf(prev_sm[c], prev_sm[c + 1]), fmaxf(sm[c], sm[c + 1])) > 0.02f) rw_quad(&bs, &prev_s[c], &prev_s[c + 1], &cur_s[c], &cur_s[c + 1], 3), nq++;
        /* the dirt is laid only where every corner of a triangle is dirt: its ends run out in ragged tongues */
        int tr = rw_tris(prev_dm[c], prev_dm[c + 1], dm[c], dm[c + 1], 0.5f);
        if (tr) rw_quad(&bd, &prev_d[c], &prev_d[c + 1], &cur_d[c], &cur_d[c + 1], tr), nd++;
        /* the drift's grains lie where it is deep (the bleached halo of the salt skin softens their ragged edge) */
        tr = rw_tris(prev_wm[c], prev_wm[c + 1], wm[c], wm[c + 1], 0.6f);
        if (tr) rw_quad(&bw, &prev_w[c], &prev_w[c + 1], &cur_w[c], &cur_w[c + 1], tr), nw++;
      }
    }
    memcpy(prev_s, cur_s, sizeof cur_s), memcpy(prev_d, cur_d, sizeof cur_d), memcpy(prev_w, cur_w, sizeof cur_w);
    memcpy(prev_sm, sm, sizeof sm), memcpy(prev_dm, dm, sizeof dm), memcpy(prev_wm, wm, sizeof wm);
    have_prev = true;
  }
  char nm[24];
  snprintf(nm, sizeof nm, "RoadWear_%d", nchunk);
  rw_finish(&bs, root, nm, ms);
  snprintf(nm, sizeof nm, "RoadDirt_%d", nchunk);
  rw_finish(&bd, root, nm, md);
  snprintf(nm, sizeof nm, "RoadDrift_%d", nchunk);
  rw_finish(&bw, root, nm, mw);
  t3_release(ms), t3_release(md), t3_release(mw);
  cw_info("salt: road wear %d quads (mean wear %.2f, drift %.2f), drift grains %d quads, dirt %d quads, %d jump spans kept clear", nq, st_w / (float)(st_n ? st_n : 1), st_d / (float)(st_n ? st_n : 1), nw, nd, nkeep);
}

/* a dust devil's body: three nested translucent funnels (narrow at the ground, wide and thin at the top) that turn at
 * different rates, so the column seems to twist. Unlit, tan, very faint where thin. */
static cw_node dust_column(cw_node parent, cw_node layer_out[3]) {
  cw_node col = cw_node_new("Node3D", "DustDevil");
  cw_node_add_child(parent, col);
  for (int L = 0; L < 3; L++) {
    soup s = { 0 };
    int rings = 12, seg = 14;
    float H = 48.0f + 10.0f * (float)L, k = 1.0f + 0.35f * (float)L;
    for (int r = 0; r <= rings; r++) {
      float u = (float)r / (float)rings, y = H * u;
      float rad = (1.1f + 6.5f * powf(u, 1.35f)) * k;
      for (int i = 0; i < seg; i++) {
        float a = 6.2832f * (float)i / (float)seg + u * 2.2f * (float)(L + 1);
        float wob = 1.0f + 0.18f * sinf(a * 3.0f + u * 5.0f);
        sv(&s, V(cosf(a) * rad * wob, y, sinf(a) * rad * wob), V(cosf(a), 0, sinf(a)), (pcol){ 1, 1, 1 });
        if (r > 0) {
          int b = r * seg + i, b1 = r * seg + (i + 1) % seg, t0 = (r - 1) * seg + i, t1 = (r - 1) * seg + (i + 1) % seg;
          st(&s, t0, b, b1), st(&s, t0, b1, t1);
        }
      }
    }
    t3_geometry *g = soup_geo(&s);
    t3_material *mat = t3_mesh_basic_material_new(0xffffff);
    mat->color = (t3_color){ 1.15f, 0.88f, 0.7f };
    mat->transparent = true;
    mat->opacity = 0.11f;
    mat->depth_write = false;
    mat->side = T3_DOUBLE_SIDE;
    t3_mesh *me = t3_mesh_new(g, mat);
    me->base.cast_shadow = false;
    me->base.receive_shadow = false;
    layer_out[L] = hang(col, "Funnel", &me->base);
    t3_release(me);
    t3_release(g);
    t3_release(mat);
  }
  return col;
}

/* ── life: dust devils, flocks, ambient drifting dust, flare embers ─────────────────────────────────── */

static void salt_life(cw_node scene, cw_node root, const track *t) {
  float hw = t->s[0].hw;
  fx_ambient_follow(scene, FXA_DUST, 0.45f);
  /* dust devils wandering over the pan: tall thin columns of dust that cross the view */
  struct { float s, lat, rx, rz; } dv[4] = { { 1850.0f, hw + 140.0f, 60.0f, 30.0f }, { 2450.0f, -(hw + 110.0f), 80.0f, 40.0f }, { 640.0f, hw + 160.0f, 70.0f, 50.0f }, { 150.0f, -(hw + 200.0f), 60.0f, 60.0f } };
  for (int i = 0; i < (SPLIT ? 2 : 4) && ndevil < MAX_DEVIL; i++) {
    t3_vec3 p = beside(dv[i].s, dv[i].lat, 0);
    cw_node n = fx_ambient_at(scene, FXA_DUST, p, V(2.5f, 16.0f, 2.5f), 2.2f, 520.0f);
    cw_node lay[3];
    cw_node column = dust_column(root, lay);
    DEVIL[ndevil] = (typeof(DEVIL[0])){ n, column, { lay[0], lay[1], lay[2] }, p.x, p.z, dv[i].rx, dv[i].rz, hashf_(9400u + (uint32_t)i) * 6.28f, 0.05f + 0.03f * (float)i };
    ndevil++;
  }
  /* flare embers above the two flare stacks */
  fx_ambient_at(scene, FXA_EMBERS, beside(1420.0f, hw + 88.0f, 40.0f), V(1.5f, 1.0f, 1.5f), 0.8f, 380.0f);
  fx_ambient_at(scene, FXA_EMBERS, beside(1520.0f, -(hw + 96.0f), 36.0f), V(1.5f, 1.0f, 1.5f), 0.8f, 380.0f);
  /* a haze of dust sitting low on the start straight and the escarpment base */
  fx_ambient_at(scene, FXA_MIST, beside(1500.0f, 0, 3.0f), V(60.0f, 3.0f, 60.0f), 0.5f, 420.0f);

  /* flocks of birds wheeling over the pan */
  float fc[4][3] = { { 0.55f, 0, 80 }, { 0.2f, 1, 110 }, { 0.8f, 0, 70 }, { 0.38f, 1, 95 } };
  for (int i = 0; i < (SPLIT ? 1 : 4) && nflock < MAX_FLOCK; i++) {
    cw_node n = model_node(root, "Flock", m_flock(9500u + (uint32_t)i, 9 + i * 2), -1, false);
    FLOCK[nflock++] = (typeof(FLOCK[0])){ n, CX + (fc[i][0] - 0.5f) * 300.0f, CZ + (hashf_(9600u + (uint32_t)i) - 0.5f) * 400.0f, 120.0f + 90.0f * (float)i, fc[i][2], hashf_(9700u + (uint32_t)i) * 6.28f, (i & 1 ? -1.0f : 1.0f) * (0.06f + 0.02f * (float)i), 5.0f + (float)i };
  }
}

/* ── the frame: everything that moves ───────────────────────────────────────────────────────────────── */

static cw_node find_cam(cw_node n) {
  if (cw_node_is(n, "Camera3D")) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_cam(c);
    if (cw_node_valid(r)) return r;
  }
}

/* development aid: SALT_CAM=s,lat,height,looks,looklat,lookheight holds the first camera at that spot on the course and
 * looks at the second, so a set piece can be inspected from the side (not read in a normal run). */
static void dev_camera(void) {
  static int mode = -1;
  static float v[6];
  static cw_node cam;
  if (mode < 0) {
    const char *e = getenv("SALT_CAM");
    mode = e && sscanf(e, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6;
    if (mode) cam = find_cam(ROOT_SCENE);
  }
  if (!mode || !cw_node_valid(cam)) return;
  t3_vec3 e = track_point(TR, ws(v[0]), v[1], v[2]), a = track_point(TR, ws(v[3]), v[4], v[5]);
  cw_node3d_snapshot(cam);
  cw_node3d_set_position(cam, e);
  cw_node3d_look_at(cam, a, V(0, 1, 0));
  cw_node3d_snapshot(cam);
}

static void salt_frame(float alpha, double time) {
  (void)alpha;
  float T = (float)time;
  dev_camera();
  /* flags: each cloth segment turns a little more than the one before, so the cloth waves down its length */
  for (int i = 0; i < nflag; i++) {
    float base = 0.3f * sinf(T * 0.7f + FLAG[i].ph);
    for (int j = 0; j < FLAG_SEGS; j++) {
      float w = sinf(T * 4.6f + FLAG[i].ph - (float)j * 1.15f) * FLAG[i].amp * (0.35f + 0.28f * (float)j);
      cw_node3d_set_rotation(FLAG[i].seg[j], V(0, (j == 0 ? base : 0.0f) + w, 0));
    }
  }
  for (int i = 0; i < nbob; i++) {
    float k = fabsf(sinf(T * BOB[i].rate * 0.5f + BOB[i].ph));
    cw_node3d_set_position(BOB[i].n, V(0, BOB[i].y0 + BOB[i].amp * k, 0));
  }
  for (int i = 0; i < npump; i++) {
    t3_quat qy = t3_quat_from_axis_angle(V(0, 1, 0), PUMP[i].yaw);
    t3_quat qx = t3_quat_from_axis_angle(V(1, 0, 0), sinf(T * PUMP[i].rate + PUMP[i].ph) * 0.3f);
    cw_node3d_set_quaternion(PUMP[i].beam, t3_quat_multiply(qy, qx));
  }
  for (int i = 0; i < nwheel; i++) {
    t3_quat qy = t3_quat_from_axis_angle(V(0, 1, 0), WHEEL[i].yaw);
    t3_quat qz = t3_quat_from_axis_angle(V(0, 0, 1), T * WHEEL[i].rate + WHEEL[i].ph);
    cw_node3d_set_quaternion(WHEEL[i].wheel, t3_quat_multiply(qy, qz));
  }
  if (have_crane) {
    float sw = sinf(T * 0.5f) * 0.07f;
    cw_node3d_set_rotation(CRANE.load, V(sw * 0.5f, 0, sw));
  }
  for (int i = 0; i < nflock; i++) {
    float a = FLOCK[i].ph + T * FLOCK[i].rate;
    t3_vec3 p = V(FLOCK[i].cx + cosf(a) * FLOCK[i].r, FLOCK[i].y + 6.0f * sinf(T * 0.3f + FLOCK[i].ph), FLOCK[i].cz + sinf(a) * FLOCK[i].r);
    cw_node3d_set_position(FLOCK[i].n, p);
    float dir = FLOCK[i].rate > 0 ? 1.0f : -1.0f;
    cw_node3d_set_rotation(FLOCK[i].n, V(0, -a * 1.0f + (dir > 0 ? 0.0f : 3.1416f) + (dir > 0 ? 0.0f : 0.0f), 0));
    /* the wings: the whole flock flaps by squashing in y */
    cw_node3d_set_scale(FLOCK[i].n, V(1, 0.45f + 0.9f * fabsf(sinf(T * FLOCK[i].flap + FLOCK[i].ph)), 1));
  }
  for (int i = 0; i < ndevil; i++) {
    float a = DEVIL[i].ph + T * DEVIL[i].rate;
    float x = DEVIL[i].cx + cosf(a) * DEVIL[i].rx, z = DEVIL[i].cz + sinf(a * 1.3f) * DEVIL[i].rz;
    float gy = ground_y(x, z);
    cw_node3d_set_position(DEVIL[i].n, V(x, gy + 8.0f, z));
    cw_node3d_set_position(DEVIL[i].column, V(x, gy - 0.5f, z));
    for (int L = 0; L < 3; L++) cw_node3d_set_rotation(DEVIL[i].layer[L], V(0, T * (1.6f + 0.9f * (float)L) * (L & 1 ? -1.0f : 1.0f), 0));
  }
  if (cw_node_valid(CLOUDS)) cw_node3d_set_rotation(CLOUDS, V(0, T * 0.0006f, 0));
  for (int i = 0; i < NMIRAGE; i++)
    if (MIR_M[i]) MIR_M[i]->opacity = 0.17f + 0.1f * sinf(T * 0.9f + MIR_PH[i]) + 0.05f * sinf(T * 2.3f + MIR_PH[i] * 2.0f);
}

const map_def map_salt = {
  .name = "Salt Flats Run",
  .blurb = "A dawn dash across the dry lakebed: wide open road, big jumps, mirages.",
  .scene = "scenes/salt.cwscene",
  .spec = &(track_spec){ .name = "salt", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 11, .circuit = true },
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.9f, 0.82f, 0.78f }, .kerb_a = { 0.93f, 0.36f, 0.1f }, .kerb_b = { 0.96f, 0.94f, 0.9f }, .wall = { 0.95f, 0.88f, 0.8f },
            .wall_top = { 0.98f, 0.93f, 0.86f }, .road_tex = TEX_ROAD_SALT, .wall_tex = TEX_WALL_HAZARD, .road_rough = 0.8f },
  .laps = 3,
  .par_lap = 60,
  .scenery = salt_scenery,
  .scenery_frame = salt_frame,
};
