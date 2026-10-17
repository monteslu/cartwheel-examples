/* pugball's goal nets: cloth (cartwheel/cloth.h) that billows when the cat or
 * a pug hits it. Render only: the nets live beside their own small Box3D
 * world, which holds nothing but kinematic stand-ins for the cat and the
 * pugs, placed from the simulation's state on the fixed tick. The match's
 * world (sim.c) never sees them, nothing here feeds the rules, and no field
 * the gates compare reads them.
 *
 *   net      one cloth per goal: a wrap round the two sides and the back,
 *            pegged along the ground, the top and the posts, and a roof
 *            pinned round its edges; a little inside the goal's box so the
 *            cat drives it back to the walls
 *   cords    the cloth's grid drawn as cords (a 0.4 m mesh), each a thin
 *            ribbon in the net's surface, rebuilt each frame from the
 *            particles blended by the render alpha while the net moves
 *   sleep    a net is stepped only while something is in its goal and for
 *            a few seconds after, so most of a match costs nothing
 *
 * The cloth is deterministic (the same bits natively and in a cart), and it
 * steps on the fixed tick from the simulation's state only, so every host
 * draws the same net. */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/cloth.h"
#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "world.h"

#define NS 11  /* segments along each side */
#define NB 28  /* along the back */
#define NV 12  /* up the wrap */
#define WU (NS + NB + NS + 1)  /* wrap columns */
#define WV (NV + 1)            /* wrap rows */
#define RX (NB + 1)            /* roof columns */
#define RZ (NS + 1)            /* roof rows */
#define NP (WU * WV + RX * RZ) /* particles a net */
#define CORD 0.014f            /* half a cord's width */
#define INSET_SIDE 0.35f
#define INSET_BACK 0.55f
#define INSET_ROOF 0.3f
#define AWAKE_TICKS 240        /* a net keeps stepping this long after the last visit */
#define PUG_R 0.55f

typedef struct goal_net {
  cw_cloth cloth;
  t3_geometry *geo;
  t3_mesh *mesh;
  int awake;          /* ticks of stepping left */
  bool moved;         /* stepped since the geometry was last written */
  int redraw;         /* frames to keep writing the geometry after a step */
  int side;           /* -1 the goal at -z, +1 at +z */
} goal_net;

static struct {
  bool ready;
  cw_world3d w;
  cw_body3d ball, pug[MAXP];
  t3_vec3 ball_at, pug_at[MAXP];
  goal_net g[2];
  t3_material *mat;
  t3_vec3 *tmp;      /* interpolated particles */
  t3_vec3 *hp, *hn;  /* one grid's points and normals */
  int32_t steps;     /* debug: net steps taken */
  uint32_t hash;     /* debug: both nets' hashes */
  float time_scale;
} N;

static t3_vec3 nv3(t3_vec3 a) {
  float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  return l > 1e-8f ? v3mul(a, 1 / l) : (t3_vec3){ 0, 1, 0 };
}

/* the rest shape: the wrap's columns run from the left post down the side,
 * across the back and up the other side; rows go up. The roof's columns run
 * across, its rows from the crossbar back. */
static void rest_shape(int side, t3_vec3 *p, float *inv) {
  float s = (float)side, x0 = GW - INSET_SIDE, zf = s * (AL + 0.12f), zb = s * (AL + GD - INSET_BACK), top = GH - INSET_ROOF;
  for (int j = 0; j < WV; j++)
    for (int i = 0; i < WU; i++) {
      float y = top * (float)j / NV, x, z;
      if (i <= NS) x = -x0, z = zf + (zb - zf) * (float)i / NS;
      else if (i <= NS + NB) x = -x0 + 2 * x0 * (float)(i - NS) / NB, z = zb;
      else x = x0, z = zb + (zf - zb) * (float)(i - NS - NB) / NS;
      int k = j * WU + i;
      p[k] = (t3_vec3){ x, y, z };
      inv[k] = (j == 0 || j == NV || i == 0 || i == WU - 1) ? 0 : 1; /* pegged at the ground and the top, tied to the posts */
    }
  for (int j = 0; j < RZ; j++)
    for (int i = 0; i < RX; i++) {
      int k = WU * WV + j * RX + i;
      p[k] = (t3_vec3){ -x0 + 2 * x0 * (float)i / NB, top, zf + (zb - zf) * (float)j / NS };
      inv[k] = (j == 0 || j == RZ - 1 || i == 0 || i == RX - 1) ? 0 : 1;
    }
}

static int grid_tris(uint32_t *t, int base, int nu, int nv) {
  int n = 0;
  for (int j = 0; j < nv - 1; j++)
    for (int i = 0; i < nu - 1; i++) {
      uint32_t a = (uint32_t)(base + j * nu + i), b = a + 1, c = a + (uint32_t)nu, d = c + 1;
      if ((i + j) & 1) t[n++] = a, t[n++] = b, t[n++] = d, t[n++] = a, t[n++] = d, t[n++] = c;
      else t[n++] = a, t[n++] = b, t[n++] = c, t[n++] = b, t[n++] = d, t[n++] = c;
    }
  return n / 3;
}

/* cords: segments between neighbouring particles of each grid, both ways */
static int cord_segments(int nu, int nv) { return (nu - 1) * nv + nu * (nv - 1); }
#define SEGS (cord_segments(WU, WV) + cord_segments(RX, RZ))

static t3_geometry *cord_geometry(void) {
  int nseg = SEGS;
  float *pos = calloc((size_t)nseg * 12, sizeof(float)), *nor = calloc((size_t)nseg * 12, sizeof(float));
  float *uv = calloc((size_t)nseg * 8, sizeof(float));
  uint32_t *idx = malloc((size_t)nseg * 6 * sizeof(uint32_t));
  for (int q = 0; q < nseg; q++) {
    uint32_t a = (uint32_t)q * 4;
    uint32_t t[6] = { a, a + 1, a + 2, a, a + 2, a + 3 };
    memcpy(idx + q * 6, t, sizeof t);
    float u[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    memcpy(uv + q * 8, u, sizeof u);
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nseg * 4, 3);
  at->usage = T3_DYNAMIC_DRAW;
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, at);
  t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, nor, nseg * 4, 3);
  at->usage = T3_DYNAMIC_DRAW;
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, at);
  t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, uv, nseg * 4, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, at);
  t3_release(at);
  at = t3_attribute_new(T3_UINT32, idx, nseg * 6, 1);
  t3_geometry_set_index(g, at);
  t3_release(at);
  free(pos), free(nor), free(uv), free(idx);
  return g;
}

/* one grid's particles and their normals */
static void grid_normals(const t3_vec3 *p, int nu, int nv, t3_vec3 *hp, t3_vec3 *hn) {
  for (int b = 0; b < nv; b++)
    for (int a = 0; a < nu; a++) {
      hp[b * nu + a] = p[b * nu + a];
      t3_vec3 du = v3sub(p[b * nu + (a + 1 < nu ? a + 1 : a)], p[b * nu + (a > 0 ? a - 1 : a)]);
      t3_vec3 dv = v3sub(p[(b + 1 < nv ? b + 1 : b) * nu + a], p[(b > 0 ? b - 1 : b) * nu + a]);
      hn[b * nu + a] = nv3(v3cross(du, dv));
    }
}

static float *put_cords(float *pos, float *nor, const t3_vec3 *hp, const t3_vec3 *hn, int hu, int hv, int along_u) {
  int na = along_u ? hu - 1 : hu, nb = along_u ? hv : hv - 1;
  for (int b = 0; b < nb; b++)
    for (int a = 0; a < na; a++) {
      int k0 = b * hu + a, k1 = along_u ? k0 + 1 : k0 + hu;
      t3_vec3 p0 = hp[k0], p1 = hp[k1], n0 = hn[k0], n1 = hn[k1];
      t3_vec3 w = v3mul(nv3(v3cross(v3sub(p1, p0), v3add(n0, n1))), CORD); /* across the cord, in the net's surface */
      t3_vec3 q[4] = { v3sub(p0, w), v3sub(p1, w), v3add(p1, w), v3add(p0, w) };
      t3_vec3 m[4] = { n0, n1, n1, n0 };
      for (int v = 0; v < 4; v++) {
        pos[v * 3] = q[v].x, pos[v * 3 + 1] = q[v].y, pos[v * 3 + 2] = q[v].z;
        nor[v * 3] = m[v].x, nor[v * 3 + 1] = m[v].y, nor[v * 3 + 2] = m[v].z;
      }
      pos += 12, nor += 12;
    }
  (void)nor;
  return pos;
}

static void write_geometry(goal_net *g, float alpha) {
  cw_cloth_interpolated(g->cloth, alpha, N.tmp);
  float *pos = t3_attribute_f32(g->geo->attributes[T3_ATTR_POSITION]);
  float *nor = t3_attribute_f32(g->geo->attributes[T3_ATTR_NORMAL]);
  const struct { int base, nu, nv; } grids[2] = { { 0, WU, WV }, { WU * WV, RX, RZ } };
  for (int k = 0; k < 2; k++) {
    int hu = grids[k].nu, hv = grids[k].nv;
    grid_normals(N.tmp + grids[k].base, hu, hv, N.hp, N.hn);
    float *p2 = put_cords(pos, nor, N.hp, N.hn, hu, hv, 1);
    nor += p2 - pos, pos = p2;
    p2 = put_cords(pos, nor, N.hp, N.hn, hu, hv, 0);
    nor += p2 - pos, pos = p2;
  }
  g->geo->attributes[T3_ATTR_POSITION]->version++;
  g->geo->attributes[T3_ATTR_NORMAL]->version++;
}

void net_reset(void) {
  if (N.ready) {
    for (int k = 0; k < 2; k++) {
      if (N.g[k].mesh) t3_release(N.g[k].mesh);
      if (N.g[k].geo) t3_release(N.g[k].geo);
    }
    if (N.mat) t3_release(N.mat);
    if (cw_world3d_valid(N.w)) cw_world3d_destroy(N.w); /* its cloths with it */
    free(N.tmp), free(N.hp), free(N.hn);
  }
  int32_t steps = N.steps;
  uint32_t hash = N.hash;
  memset(&N, 0, sizeof N);
  N.steps = steps, N.hash = hash;
}

static cw_body3d stand_in(float r, t3_vec3 at) {
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_KINEMATIC;
  bd.position = at;
  bd.sleep = false;
  bd.interpolate = false;
  cw_body3d b = cw_body3d_create(N.w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  cw_shape3d_sphere(b, &sd, (t3_vec3){ 0, 0, 0 }, r);
  return b;
}

void net_build(cw_node root, const sim *s) {
  static bool fields;
  if (!fields) {
    fields = true;
    cw_debug_i32("net.steps", &N.steps); /* not a gate field: cosmetic */
    cw_debug_u32("net.hash", &N.hash);
  }
  net_reset();
  N.time_scale = 1;
  cw_world3d_def d = cw_world3d_default_def();
  d.gravity = (t3_vec3){ 0, -9.81f, 0 };
  d.workers = 1;
  d.sleep = false;
  N.w = cw_world3d_create(&d);
  N.ball_at = s->ball;
  N.ball = stand_in(RB, s->ball);
  for (int i = 0; i < MAXP; i++) {
    N.pug_at[i] = s->p[i].on ? s->p[i].p : (t3_vec3){ 0, -50, 0 };
    N.pug[i] = stand_in(PUG_R, N.pug_at[i]);
  }
  N.tmp = malloc(sizeof(t3_vec3) * NP);
  int hmax = WU * WV;
  N.hp = malloc(sizeof(t3_vec3) * (size_t)hmax);
  N.hn = malloc(sizeof(t3_vec3) * (size_t)hmax);
  N.mat = t3_mesh_physical_material_new(0xffffff);
  N.mat->color = lin(0.95f, 0.95f, 0.95f);
  N.mat->roughness = 0.8f;
  N.mat->side = T3_DOUBLE_SIDE;
  t3_vec3 *rest = malloc(sizeof(t3_vec3) * NP);
  float *inv = malloc(sizeof(float) * NP);
  uint32_t *tris = malloc(sizeof(uint32_t) * 3 * 2 * NP);
  for (int k = 0; k < 2; k++) {
    goal_net *g = &N.g[k];
    g->side = k ? 1 : -1;
    rest_shape(g->side, rest, inv);
    int nt = grid_tris(tris, 0, WU, WV);
    nt += grid_tris(tris + nt * 3, WU * WV, RX, RZ);
    cw_cloth_def cd = cw_cloth_default_def();
    cd.positions = rest;
    cd.vertex_count = NP;
    cd.triangles = tris;
    cd.triangle_count = nt;
    cd.inv_mass = inv;
    cd.mass = 4;
    cd.stretch_compliance = 0.0001f; /* netting gives a little */
    cd.bend_compliance = 0.05f;
    cd.tether_slack = 1.06f;
    cd.damping = 0.9f;
    cd.thickness = 0.06f;
    cd.friction = 0.3f;
    cd.substeps = 8;
    cd.quality_scaled = true;
    cd.workers = 1;
    g->cloth = cw_cloth_create(N.w, &cd);
    g->geo = cord_geometry();
    g->geo->bounding_sphere = (t3_sphere){ { 0, GH * 0.5f, g->side * (AL + GD * 0.5f) }, 9 };
    g->geo->has_bounding_sphere = true;
    write_geometry(g, 1);
    g->mesh = t3_mesh_new(g->geo, N.mat);
    g->mesh->base.cast_shadow = true;
    g->mesh->base.receive_shadow = true;
    cw_node n = cw_node_new("Node3D", k ? "NetRed" : "NetBlue");
    cw_node_add_child(root, n);
    t3_object *wrap = t3_group_new();
    t3_object_add(wrap, &g->mesh->base);
    cw_node3d_set_object(n, wrap);
    t3_release(wrap);
  }
  free(rest), free(inv), free(tris);
  N.ready = true;
}

void net_time_scale(float k) { N.time_scale = k; }

/* a stand-in follows its sim body; a jump of more than 4 m in a tick (a
 * kickoff, a respawn) is a teleport, not a sweep through the net */
static void follow(cw_body3d b, t3_vec3 *at, t3_vec3 to, float dt) {
  t3_vec3 d = v3sub(to, *at);
  if (v3dot(d, d) > 16) cw_body3d_set_transform(b, (cw_xform3){ to, { 0, 0, 0, 1 } });
  else cw_body3d_set_target(b, (cw_xform3){ to, { 0, 0, 0, 1 } }, dt);
  *at = to;
}

static bool near_goal(t3_vec3 p, float r, int side) {
  return p.z * (float)side > AL - 1.5f - r && p.x > -GW - r && p.x < GW + r && p.y < GH + r;
}

void net_tick(const sim *s) {
  if (!N.ready) return;
  float dt = (1.0f / TICK_HZ) * N.time_scale;
  /* the cat while it is drawn; a hidden one (the goal's puff of fur) is out of the way */
  bool shown = !(s->phase == PH_GOAL && s->phase_ticks > 8 && s->phase_ticks < 150);
  t3_vec3 ball = shown ? s->ball : (t3_vec3){ 0, -50, 0 };
  follow(N.ball, &N.ball_at, ball, dt);
  for (int i = 0; i < MAXP; i++) {
    const pug *u = &s->p[i];
    t3_vec3 to = u->on && !u->dead ? u->p : (t3_vec3){ 0, -50, 0 };
    follow(N.pug[i], &N.pug_at[i], to, dt);
  }
  bool any = false;
  for (int k = 0; k < 2; k++) {
    goal_net *g = &N.g[k];
    bool visit = near_goal(ball, RB, g->side);
    for (int i = 0; i < MAXP; i++) visit |= near_goal(N.pug_at[i], PUG_R, g->side);
    if (visit) g->awake = AWAKE_TICKS;
    for (int e = 0; e < s->ev_n; e++) /* a goal shakes the whole net */
      if (s->ev[e].kind == EV_GOAL && s->ev[e].at.z * (float)g->side > 0) {
        g->awake = AWAKE_TICKS;
        cw_cloth_impulse(g->cloth, s->ev[e].at, 6, v3mul((t3_vec3){ 0, 0.15f, (float)g->side }, 2));
      }
    any |= g->awake > 0;
  }
  if (!any) return;
  cw_world3d_step(N.w, dt);
  for (int k = 0; k < 2; k++) {
    goal_net *g = &N.g[k];
    if (g->awake <= 0) continue;
    g->awake--;
    cw_cloth_step(g->cloth, dt);
    g->moved = true;
    N.steps++;
  }
  N.hash = cw_cloth_hash(N.g[0].cloth) * 16777619u ^ cw_cloth_hash(N.g[1].cloth);
}

/* every frame: a net that stepped is redrawn between its last two steps
 * (and once more after it falls asleep, at rest) */
void net_frame(float alpha) {
  if (!N.ready) return;
  for (int k = 0; k < 2; k++) {
    goal_net *g = &N.g[k];
    if (g->moved) g->redraw = 2, g->moved = false;
    if (g->redraw <= 0) continue;
    write_geometry(g, g->awake > 0 ? alpha : 1);
    if (g->awake <= 0) g->redraw--;
  }
}
