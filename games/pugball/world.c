/* pugball's arena in 3D: the pitch (mown stripes and chalk), the ramped
 * bowl swept round the arena's plan (turf ramps, padded boards in the team
 * colours, glass up to the rim on steel mullions), the goals (posts, a
 * glowing frame; their cloth nets are net.c's), the boost pads, the pugs and
 * the cat.
 *
 * Everything is placed from the simulation on the fixed tick, so every host
 * draws the same frame; nothing here feeds back into the rules. The pugs
 * are animated here (legs, ears, tail, squash, blinks, the tongue and the
 * jet flame), and so is the cat (it rolls with the ball, squashes on hits,
 * splays its legs and tail in the air, pulls faces). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

typedef struct mesh_build {
  float *pos, *nor, *uv;
  uint32_t *idx;
  int nv, ni, cap_v, cap_i;
} mesh_build;

static int mb_vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v) {
  if (b->nv == b->cap_v) {
    b->cap_v = b->cap_v ? b->cap_v * 2 : 1024;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)(uint32_t)b->cap_v);
  }
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *U = b->uv + b->nv * 2;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x, N[1] = n.y, N[2] = n.z;
  U[0] = u, U[1] = v;
  return b->nv++;
}
static void mb_quad(mesh_build *b, int a, int c, int d, int e) { /* a b / d c, counter-clockwise from the front */
  if (b->ni + 6 > b->cap_i) {
    b->cap_i = b->cap_i ? b->cap_i * 2 : 3072;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)(uint32_t)b->cap_i);
  }
  uint32_t t[6] = { (uint32_t)a, (uint32_t)c, (uint32_t)d, (uint32_t)a, (uint32_t)d, (uint32_t)e };
  memcpy(b->idx + b->ni, t, sizeof t);
  b->ni += 6;
}
static t3_geometry *mb_finish(mesh_build *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a);
  t3_release(a);
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  free(b->pos);
  free(b->nor);
  free(b->uv);
  free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

/* ── the arena's plan: the rounded rectangle, sampled ───────────────── */

psample PS[MAXPS];
int nps;

static void plan_add(float x, float z, float nx, float nz) {
  if (nps >= MAXPS) return;
  float s = 0;
  if (nps) s = PS[nps - 1].s + sqrtf((x - PS[nps - 1].x) * (x - PS[nps - 1].x) + (z - PS[nps - 1].z) * (z - PS[nps - 1].z));
  PS[nps++] = (psample){ x, z, nx, nz, s };
}
void plan_build(void) {
  nps = 0;
  const float hx = AW - AC, hz = AL - AC, step = 0.5f;
  const int arc_n = 24;
  /* right side (+x), going +z; then each corner and side counter-clockwise from above */
  for (float z = -hz; z < hz - 1e-3f; z += step) plan_add(AW, z, 1, 0);
  for (int k = 0; k < arc_n; k++) {
    float a = 1.5707963f * k / arc_n;
    plan_add(hx + AC * cw_cos(a), hz + AC * cw_sin(a), cw_cos(a), cw_sin(a));
  }
  for (float x = hx; x > -hx + 1e-3f; x -= step) plan_add(x, AL, 0, 1);
  for (int k = 0; k < arc_n; k++) {
    float a = 1.5707963f + 1.5707963f * k / arc_n;
    plan_add(-hx + AC * cw_cos(a), hz + AC * cw_sin(a), cw_cos(a), cw_sin(a));
  }
  for (float z = hz; z > -hz + 1e-3f; z -= step) plan_add(-AW, z, -1, 0);
  for (int k = 0; k < arc_n; k++) {
    float a = 3.1415927f + 1.5707963f * k / arc_n;
    plan_add(-hx + AC * cw_cos(a), -hz + AC * cw_sin(a), cw_cos(a), cw_sin(a));
  }
  for (float x = -hx; x < hx - 1e-3f; x += step) plan_add(x, -AL, 0, -1);
  for (int k = 0; k <= arc_n; k++) {
    float a = 4.712389f + 1.5707963f * k / arc_n;
    plan_add(hx + AC * cw_cos(a), -hz + AC * cw_sin(a), cw_cos(a), cw_sin(a));
  }
}

/* sweep a profile round the plan. keep: -1 the blue half (z < 0), +1 red, 0 all.
 * Quads in a goal mouth are left out unless goal_holes is false. */
t3_geometry *arena_sweep(const prof *pr, int npr, float tile_u, float tile_v, int keep, bool goal_holes) {
  if (!nps) plan_build();
  mesh_build b = { 0 };
  float pv[256];
  pv[0] = 0;
  for (int j = 1; j < npr; j++) pv[j] = pv[j - 1] + sqrtf((pr[j].d - pr[j - 1].d) * (pr[j].d - pr[j - 1].d) + (pr[j].y - pr[j - 1].y) * (pr[j].y - pr[j - 1].y));
  for (int i = 0; i + 1 < nps; i++) {
    const psample *a = &PS[i], *c = &PS[i + 1];
    float cx = (a->x + c->x) * 0.5f, cz = (a->z + c->z) * 0.5f;
    if (keep && (cz < 0 ? -1 : 1) != keep) continue;
    int base = -1;
    for (int j = 0; j + 1 < npr; j++) {
      float my = (pr[j].y + pr[j + 1].y) * 0.5f, md = (pr[j].d + pr[j + 1].d) * 0.5f;
      float qx = cx + (a->nx + c->nx) * 0.5f * md, qz = cz + (a->nz + c->nz) * 0.5f * md;
      if (goal_holes && fabsf(qz) > AL - AR - 0.6f && fabsf(qx) < GW + 0.05f && my < GH) continue;
      (void)base;
      int v[4];
      const psample *ps[2] = { a, c };
      for (int k = 0; k < 4; k++) {
        const psample *s = ps[k == 1 || k == 2];
        const prof *p = &pr[j + (k >= 2)];
        t3_vec3 P = { s->x + s->nx * p->d, p->y, s->z + s->nz * p->d };
        t3_vec3 N = { s->nx * p->nd, p->ny, s->nz * p->nd };
        v[k] = mb_vert(&b, P, N, s->s / tile_u, pv[j + (k >= 2)] / tile_v);
      }
      /* faces look into the arena */
      mb_quad(&b, v[0], v[1], v[2], v[3]);
    }
  }
  return mb_finish(&b);
}

/* ── state ──────────────────────────────────────────────────────────── */

typedef struct panim {
  float phase, squash, squash_v, ear, ear_v, wag;
  float yaw_last, roll, pant;
  int blink, land, sneeze;
  bool was_dead;
  t3_vec3 last_v;
} panim;

static struct {
  bool ready;
  int theme;
  cw_node root;
  pug_model pug[MAXP];
  panim pa[MAXP];
  bool on[MAXP];
  cat_model cat;
  t3_quat cat_q;
  float cat_squash, cat_squash_v, cat_air;
  int cat_angry, cat_dizzy, cat_ticks;
  t3_instanced_mesh *pad_lit, *pad_dim, *orb;
  uint8_t pad_state[MAX_PADS_BOOST]; /* the rings as last posed: 0 never, 1 lit, 2 dim */
  t3_material *orb_mat;
  int ticks;
  int last_phase;
  bool showcase;
  t3_object *cat_ring;
  t3_material *ring_mat;
  /* the Node3Ds that carry the pugs, the cat and its ring: their transforms
   * are set on the fixed tick and rendered between the last two ticks */
  cw_node pug_node[MAXP], cat_node, ring_node;
  bool far_on[MAXP + 1]; /* drawn as their far copies now: the pugs, then the cat */
} W;

static cw_node hang(cw_node parent, const char *name, t3_object *obj) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, obj);
  cw_node3d_set_object(n, wrap);
  t3_release(wrap);
  return n;
}

static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

static void add_mesh(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool cast, bool recv) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = recv;
  hang(parent, name, &me->base);
  t3_release(me);
  t3_release(g);
  t3_release(m);
}

static const t3_mat4 ZERO;

/* ── the pitch ──────────────────────────────────────────────────────── */

/* the half width of the flat floor at z (its rounded corners) */
static float floor_half(float z) {
  const float W2 = AW - AR, L2 = AL - AR, r = AC - AR;
  float dz = fabsf(z) - (L2 - r);
  if (dz <= 0) return W2;
  if (dz >= r) return W2 - r;
  return W2 - r + sqrtf(r * r - dz * dz);
}

typedef struct look {
  float grass[3], grass_alt[3], ramp[3], board[2][3], glass_tint[3], rim[3];
  int floor_tex;
  float floor_rough;
} look;
static const look LOOKS[TH_KINDS] = {
  [TH_PARK] = { { 0.95f, 1.05f, 0.85f }, { 0.78f, 0.92f, 0.7f }, { 0.85f, 0.95f, 0.78f }, { { 0.25f, 0.55f, 1 }, { 1, 0.35f, 0.3f } },
                { 0.85f, 0.95f, 1 }, { 0.92f, 0.92f, 0.95f }, TEX_GRASS, 0.9f },
  [TH_NEON] = { { 0.7f, 0.95f, 0.75f }, { 0.55f, 0.8f, 0.62f }, { 0.4f, 0.42f, 0.5f }, { { 0.15f, 0.45f, 1 }, { 1, 0.2f, 0.45f } },
                { 0.75f, 0.8f, 1 }, { 0.6f, 0.62f, 0.7f }, TEX_GRASS, 0.85f },
  [TH_BEACH] = { { 1, 0.95f, 0.85f }, { 0.94f, 0.88f, 0.76f }, { 1, 0.92f, 0.8f }, { { 0.2f, 0.65f, 0.95f }, { 1, 0.45f, 0.3f } },
                 { 0.9f, 0.97f, 1 }, { 0.95f, 0.9f, 0.8f }, TEX_SAND, 0.92f },
  [TH_SNOW] = { { 0.97f, 0.99f, 1.02f }, { 0.88f, 0.92f, 0.98f }, { 0.95f, 0.97f, 1 }, { { 0.3f, 0.55f, 1 }, { 0.95f, 0.3f, 0.35f } },
                { 0.85f, 0.93f, 1 }, { 0.9f, 0.92f, 0.96f }, TEX_SNOW, 0.75f },
  [TH_GARDEN] = { { 0.92f, 1.04f, 0.82f }, { 0.76f, 0.9f, 0.68f }, { 0.8f, 0.92f, 0.74f }, { { 0.3f, 0.5f, 0.95f }, { 0.95f, 0.3f, 0.4f } },
                  { 0.9f, 1, 0.92f }, { 0.75f, 0.68f, 0.5f }, TEX_GRASS, 0.9f },
};

static void build_pitch(cw_node root, const look *L) {
  /* the flat floor, in mown stripes across the pitch (two meshes, two tints) */
  const float L2 = AL - AR, band = 4.0f, step = 0.5f;
  for (int alt = 0; alt < 2; alt++) {
    mesh_build b = { 0 };
    for (float z0 = -L2; z0 < L2 - 1e-3f; z0 += band) {
      int bi = (int)floorf((z0 + L2) / band + 0.5f);
      if ((bi & 1) != alt) continue;
      int prev[2] = { -1, -1 };
      for (float z = z0; z <= z0 + band + 1e-3f; z += step) {
        float zz = fminf(z, L2), hw = floor_half(zz);
        int l = mb_vert(&b, (t3_vec3){ -hw, 0, zz }, (t3_vec3){ 0, 1, 0 }, -hw / 3.0f, zz / 3.0f);
        int r = mb_vert(&b, (t3_vec3){ hw, 0, zz }, (t3_vec3){ 0, 1, 0 }, hw / 3.0f, zz / 3.0f);
        if (prev[0] >= 0) mb_quad(&b, prev[0], l, r, prev[1]);
        prev[0] = l, prev[1] = r;
        if (zz >= L2) break;
      }
    }
    /* the goal mouths' floors (their nets' floors) */
    if (alt == 0)
      for (int s = -1; s <= 1; s += 2) {
        float za = s * (AL - AR), zb = s * (AL + GD);
        int a = mb_vert(&b, (t3_vec3){ -GW, 0, za }, (t3_vec3){ 0, 1, 0 }, -GW / 3, za / 3);
        int c = mb_vert(&b, (t3_vec3){ -GW, 0, zb }, (t3_vec3){ 0, 1, 0 }, -GW / 3, zb / 3);
        int d = mb_vert(&b, (t3_vec3){ GW, 0, zb }, (t3_vec3){ 0, 1, 0 }, GW / 3, zb / 3);
        int e = mb_vert(&b, (t3_vec3){ GW, 0, za }, (t3_vec3){ 0, 1, 0 }, GW / 3, za / 3);
        if (s > 0) mb_quad(&b, a, c, d, e);
        else mb_quad(&b, a, e, d, c);
      }
    const float *g = alt ? L->grass_alt : L->grass;
    t3_material *m = phys(lin(g[0], g[1], g[2]), L->floor_rough, 0);
    tex_apply(m, L->floor_tex, 1, 1.2f, 1);
    if (L->floor_tex == TEX_GRASS) {
      m->sheen = 0.5f;
      m->sheen_color = lin(0.75f, 0.95f, 0.5f);
      m->sheen_roughness = 0.6f;
    }
    add_mesh(root, alt ? "PitchB" : "PitchA", mb_finish(&b), m, false, true);
  }

  /* chalk: the halfway line, the centre circle, the boundary and the boxes */
  mesh_build b = { 0 };
  const float w = 0.14f, y = 0.02f;
  t3_vec3 up = { 0, 1, 0 };
#define SEG(x0, z0, x1, z1)                                                                                            \
  do {                                                                                                                 \
    float dx = (x1) - (x0), dz = (z1) - (z0), l = sqrtf(dx * dx + dz * dz), px = -dz / l * w * 0.5f, pz = dx / l * w * 0.5f; \
    int a = mb_vert(&b, (t3_vec3){ (x0) + px, y, (z0) + pz }, up, 0, 0);                                                \
    int c = mb_vert(&b, (t3_vec3){ (x0) - px, y, (z0) - pz }, up, 1, 0);                                                \
    int d = mb_vert(&b, (t3_vec3){ (x1) - px, y, (z1) - pz }, up, 1, 1);                                                \
    int e = mb_vert(&b, (t3_vec3){ (x1) + px, y, (z1) + pz }, up, 0, 1);                                                \
    mb_quad(&b, a, c, d, e);                                                                                           \
    mb_quad(&b, a, e, d, c);                                                                                           \
  } while (0)
  float hw0 = floor_half(0) - 0.6f;
  SEG(-hw0, 0, hw0, 0);
  for (int k = 0; k < 64; k++) {
    float a0 = 6.2831853f * k / 64, a1 = 6.2831853f * (k + 1) / 64, r = 6;
    SEG(r * cw_cos(a0), r * cw_sin(a0), r * cw_cos(a1), r * cw_sin(a1));
  }
  for (int s = -1; s <= 1; s += 2) {
    float zl = s * (AL - AR - 0.6f), zb = s * (AL - AR - 7);
    SEG(-10, zl, -10, zb);
    SEG(10, zl, 10, zb);
    SEG(-10, zb, 10, zb);
    /* the penalty arc of the box */
    for (int k = 0; k < 24; k++) {
      float a0 = 3.1415927f * k / 24, a1 = 3.1415927f * (k + 1) / 24;
      SEG(4 * cw_cos(a0), zb - s * 4 * cw_sin(a0) * 0.6f, 4 * cw_cos(a1), zb - s * 4 * cw_sin(a1) * 0.6f);
    }
  }
  /* the boundary, a little inside where the ramps start */
  for (float z = -(AL - AR) + 0.6f; z < AL - AR - 0.6f - 1e-3f; z += 0.5f) {
    float z1 = fminf(z + 0.5f, AL - AR - 0.6f);
    float h0 = floor_half(z) - 0.6f, h1 = floor_half(z1) - 0.6f;
    SEG(h0, z, h1, z1);
    SEG(-h0, z, -h1, z1);
  }
#undef SEG
  t3_material *chalk = W.theme == TH_SNOW ? phys(lin(0.2f, 0.45f, 0.95f), 0.6f, 0) : phys(lin(0.95f, 0.96f, 0.93f), 0.9f, 0);
  add_mesh(root, "Chalk", mb_finish(&b), chalk, false, true);
}

/* ── the bowl: ramps, boards, glass, rim ────────────────────────────── */

static void build_bowl(cw_node root, const look *L) {
  plan_build();
  prof ramp[11];
  for (int k = 0; k <= 10; k++) {
    float t = 1.5707963f * k / 10, s = cw_sin(t), c = cw_cos(t);
    ramp[k] = (prof){ -AR + AR * s, AR - AR * c, -s, c };
  }
  t3_material *rm = phys(lin(L->ramp[0], L->ramp[1], L->ramp[2]), 0.9f, 0);
  tex_apply(rm, L->floor_tex, 1, 1.0f, 1);
  add_mesh(root, "Ramps", arena_sweep(ramp, 11, 3, 3, 0, true), rm, false, true);
  const float bh = 1.5f;
  prof boards[2] = { { 0, AR, -1, 0 }, { 0, AR + bh, -1, 0 } };
  for (int t = 0; t < 2; t++) {
    const float *c = L->board[t];
    t3_material *bm = phys(lin(c[0], c[1], c[2]), 0.4f, 0);
    tex_apply_uv(bm, TEX_PAD, 1, 1, 0.8f, 1);
    bm->clearcoat = 0.4f;
    bm->clearcoat_roughness = 0.3f;
    add_mesh(root, t ? "BoardsRed" : "BoardsBlue", arena_sweep(boards, 2, 1.5f, 1.5f, t ? 1 : -1, true), bm, false, true);
  }
  /* a lit strip on the boards' top edge */
  prof strip[2] = { { -0.02f, AR + bh, -1, 0 }, { -0.02f, AR + bh + 0.12f, -1, 0 } };
  t3_material *sm = phys(lin(1, 1, 1), 0.3f, 0);
  sm->emissive = lin(1, 0.97f, 0.9f);
  sm->emissive_intensity = W.theme == TH_NEON ? 3.5f : 1.2f;
  add_mesh(root, "BoardLight", arena_sweep(strip, 2, 1, 1, 0, true), sm, false, true);
  /* the glass up to the rim */
  prof glass[2] = { { 0, AR + bh + 0.12f, -1, 0 }, { 0, AH - AR, -1, 0 } };
  t3_material *gm = phys(lin(L->glass_tint[0], L->glass_tint[1], L->glass_tint[2]), 0.04f, 0);
  gm->transparent = true;
  gm->opacity = 0.06f;
  gm->depth_write = false;
  t3_mesh *gmesh = t3_mesh_new(arena_sweep(glass, 2, 4, 4, 0, true), gm);
  gmesh->base.render_order = 20;
  gmesh->base.receive_shadow = true;
  hang(root, "Glass", &gmesh->base);
  t3_release(gmesh->geometry);
  t3_release(gmesh);
  t3_release(gm);
  /* the rim and the mullions holding the glass */
  prof rim[3] = { { 0, AH - AR, -1, 0 }, { 0, AH - AR + 0.35f, -1, 0 }, { 0.5f, AH - AR + 0.35f, 0, 1 } };
  t3_material *steel = phys(lin(L->rim[0], L->rim[1], L->rim[2]), 0.35f, 1);
  tex_apply(steel, TEX_METAL, 1, 0.4f, 1);
  steel->roughness = 0.35f;
  add_mesh(root, "Rim", arena_sweep(rim, 3, 2, 2, 0, true), steel, true, true);
  /* the mullions in MULLION_PARTS runs round the rim, one InstancedMesh each,
   * so the views cull the runs they cannot see (each run's sphere computed
   * from its mullions: they never move) */
  enum { MULLION_PARTS = 12 };
  t3_material *mm = phys(lin(L->rim[0], L->rim[1], L->rim[2]), 0.35f, 1);
  t3_geometry *mg = geo_rounded_box(0.14f, 1, 0.14f, 0.04f, 2);
  float top = AH - AR, bot = AR + bh;
  for (int part = 0; part < MULLION_PARTS; part++) {
    int nm = 0;
    for (int i = 0; i < nps; i += 10) nm += i * MULLION_PARTS / nps == part;
    if (!nm) continue;
    t3_instanced_mesh *mul = t3_instanced_mesh_new(mg, mm, nm);
    int k = 0;
    for (int i = 0; i < nps && k < nm; i += 10) {
      if (i * MULLION_PARTS / nps != part) continue;
      const psample *p = &PS[i];
      bool mouth = fabsf(p->z) > AL - 0.5f && fabsf(p->x) < GW + 0.3f;
      t3_mat4 m;
      t3_mat4_compose(&m, (t3_vec3){ p->x + p->nx * 0.05f, (top + bot) * 0.5f, p->z + p->nz * 0.05f }, (t3_quat){ 0, 0, 0, 1 },
                      (t3_vec3){ 1, mouth ? 0 : top - bot, 1 });
      t3_instanced_mesh_set_matrix_at(mul, k++, &m);
    }
    mul->mesh.base.cast_shadow = mul->mesh.base.receive_shadow = true;
    hang(root, "Mullions", &mul->mesh.base);
    t3_release(mul);
  }
  t3_release(mg);
  t3_release(mm);
}

/* ── goals ──────────────────────────────────────────────────────────── */

static void build_goals(cw_node root) {
  t3_geometry *post = t3_cylinder_geometry_new(0.16f, 0.16f, GH, 20, 1, false);
  t3_geometry *bar = t3_cylinder_geometry_new(0.16f, 0.16f, GW * 2 + 0.32f, 20, 1, false);
  for (int s = -1; s <= 1; s += 2) {
    int team = s < 0 ? TEAM_BLUE : TEAM_RED; /* the goal at -z is blue's */
    t3_object *g = t3_group_new();
    t3_material *white = phys(lin(0.97f, 0.97f, 0.97f), 0.25f, 0);
    white->clearcoat = 1;
    t3_material *glow = phys(lin(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2]), 0.3f, 0);
    glow->emissive = lin(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2]);
    glow->emissive_intensity = 3;
    for (int x = -1; x <= 1; x += 2) {
      t3_mesh *p = t3_mesh_new(post, white);
      p->base.position = (t3_vec3){ x * (GW + 0.16f), GH * 0.5f, s * (AL + 0.1f) };
      p->base.cast_shadow = p->base.receive_shadow = true;
      t3_object_add(g, p);
      t3_release(p);
    }
    t3_mesh *cb = t3_mesh_new(bar, white);
    cb->base.position = (t3_vec3){ 0, GH + 0.16f, s * (AL + 0.1f) };
    t3_object_set_rotation(&cb->base, 0, 0, 1.5707963f);
    cb->base.cast_shadow = cb->base.receive_shadow = true;
    t3_object_add(g, cb);
    t3_release(cb);
    /* a glowing frame round the back of the goal */
    t3_geometry *fr = geo_rounded_box(GW * 2, 0.12f, 0.12f, 0.04f, 2);
    t3_mesh *f1 = t3_mesh_new(fr, glow);
    f1->base.position = (t3_vec3){ 0, GH - 0.1f, s * (AL + GD - 0.1f) };
    f1->base.receive_shadow = true;
    t3_object_add(g, f1);
    t3_release(f1);
    t3_mesh *f2 = t3_mesh_new(fr, glow);
    f2->base.position = (t3_vec3){ 0, 0.08f, s * (AL + GD - 0.1f) };
    f2->base.receive_shadow = true;
    t3_object_add(g, f2);
    t3_release(f2);
    t3_release(fr);
    /* the net: cloth (net.c) */
    hang(root, s < 0 ? "GoalBlue" : "GoalRed", g);
    t3_release(g);
    t3_release(white);
    t3_release(glow);
  }
  t3_release(post);
  t3_release(bar);
}

/* ── boost pads ─────────────────────────────────────────────────────── */

static void build_pads(cw_node root, const sim *s) {
  t3_material *lit = phys(lin(1, 0.75f, 0.2f), 0.3f, 0);
  lit->emissive = lin(1, 0.62f, 0.12f);
  lit->emissive_intensity = 2.5f;
  t3_material *dim = phys(lin(0.35f, 0.3f, 0.25f), 0.4f, 0.5f);
  W.pad_lit = t3_instanced_mesh_new(t3_torus_geometry_new(0.55f, 0.08f, 8, 32, 6.2831853f), lit, s->npads);
  W.pad_dim = t3_instanced_mesh_new(t3_torus_geometry_new(0.55f, 0.08f, 8, 32, 6.2831853f), dim, s->npads);
  t3_release(W.pad_lit->mesh.geometry);
  t3_release(W.pad_dim->mesh.geometry);
  t3_release(lit);
  t3_release(dim);
  W.orb_mat = phys(lin(1, 0.7f, 0.25f), 0.1f, 0);
  W.orb_mat->emissive = lin(1, 0.55f, 0.1f);
  W.orb_mat->emissive_intensity = 3;
  W.orb_mat->iridescence = 0.8f;
  W.orb_mat->iridescence_ior = 1.5f;
  W.orb_mat->clearcoat = 1;
  W.orb = t3_instanced_mesh_new(t3_icosahedron_geometry_new(0.45f, 3), W.orb_mat, s->npads);
  t3_release(W.orb->mesh.geometry);
  t3_instanced_mesh *im[3] = { W.pad_lit, W.pad_dim, W.orb };
  const char *names[3] = { "PadsLit", "PadsDim", "Orbs" };
  for (int k = 0; k < 3; k++) {
    /* culled by the sphere of the pads shown (a few, scattered: computed from
     * the instances, again whenever they change) */
    for (int i = 0; i < s->npads; i++) t3_instanced_mesh_set_matrix_at(im[k], i, &ZERO);
    im[k]->mesh.base.receive_shadow = true;
    hang(root, names[k], &im[k]->mesh.base);
  }
  /* big pads stand on a little plinth */
  t3_material *pm = phys(lin(0.3f, 0.3f, 0.34f), 0.35f, 1);
  tex_apply(pm, TEX_METAL, 1, 0.4f, 1);
  pm->roughness = 0.35f;
  int nbig = 0;
  for (int i = 0; i < s->npads; i++) nbig += s->pads[i].big;
  t3_instanced_mesh *pl = t3_instanced_mesh_new(t3_cylinder_geometry_new(0.9f, 1.05f, 0.18f, 32, 1, false), pm, nbig);
  t3_release(pl->mesh.geometry);
  t3_release(pm);
  int k = 0;
  for (int i = 0; i < s->npads; i++)
    if (s->pads[i].big) {
      t3_mat4 m;
      t3_mat4_compose(&m, (t3_vec3){ s->pads[i].x, 0.09f, s->pads[i].z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
      t3_instanced_mesh_set_matrix_at(pl, k++, &m);
    }
  pl->mesh.base.receive_shadow = true;
  pl->mesh.base.cast_shadow = true;
  hang(root, "Plinths", &pl->mesh.base);
  t3_release(pl);
}

static void pads_tick(const sim *s) {
  float t = W.ticks / 60.0f;
  bool rings = false; /* the rings only change when a pad is taken or comes back */
  for (int i = 0; i < s->npads; i++) {
    const boost_pad *p = &s->pads[i];
    bool on = p->cool == 0;
    t3_mat4 m;
    if (W.pad_state[i] != (on ? 1 : 2)) {
      W.pad_state[i] = on ? 1 : 2;
      rings = true;
      float sc = p->big ? 1.6f : 1.0f, y = p->big ? 0.2f : 0.05f;
      t3_quat flat = { 0.7071068f, 0, 0, 0.7071068f };
      t3_mat4_compose(&m, (t3_vec3){ p->x, y, p->z }, flat, (t3_vec3){ sc, sc, sc });
      t3_instanced_mesh_set_matrix_at(W.pad_lit, i, on ? &m : &ZERO);
      t3_instanced_mesh_set_matrix_at(W.pad_dim, i, on ? &ZERO : &m);
    }
    if (p->big && on) {
      float bob = 0.15f * cw_sin(t * 2.2f + i);
      float sy, cy;
      cw_sincos(t * 1.4f + i, &sy, &cy);
      t3_mat4_compose(&m, (t3_vec3){ p->x, 1.0f + bob, p->z }, (t3_quat){ 0, sy * 0.7071f, 0, cy }, (t3_vec3){ 1, 1, 1 });
      t3_instanced_mesh_set_matrix_at(W.orb, i, &m);
    } else if (!p->big && on) {
      t3_mat4_compose(&m, (t3_vec3){ p->x, 0.3f + 0.05f * cw_sin(t * 3 + i), p->z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 0.3f, 0.3f, 0.3f });
      t3_instanced_mesh_set_matrix_at(W.orb, i, &m);
    } else t3_instanced_mesh_set_matrix_at(W.orb, i, &ZERO);
  }
  if (rings) {
    W.pad_lit->instance_matrix->version++;
    W.pad_dim->instance_matrix->version++;
  }
  W.orb->instance_matrix->version++;
}

/* ── build / reset ──────────────────────────────────────────────────── */

void world_reset(void) {
  if (W.ready) {
    for (int i = 0; i < MAXP; i++)
      if (W.pug[i].root) t3_release(W.pug[i].root);
    if (W.cat.root) t3_release(W.cat.root);
    if (W.pad_lit) t3_release(W.pad_lit);
    if (W.pad_dim) t3_release(W.pad_dim);
    if (W.orb) t3_release(W.orb);
    if (W.orb_mat) t3_release(W.orb_mat);
    if (W.cat_ring) t3_release(W.cat_ring);
    if (W.ring_mat) t3_release(W.ring_mat);
  }
  net_reset();
  memset(&W, 0, sizeof W);
}

void world_build(cw_node scene, const sim *s, int theme, const pug_look looks[MAXP]) {
  world_reset();
  W.theme = theme;
  W.root = cw_node_new("Node3D", "World");
  cw_node_add_child(scene, W.root);
  const look *L = &LOOKS[theme];
  build_pitch(W.root, L);
  build_bowl(W.root, L);
  build_goals(W.root);
  net_build(W.root, s);
  build_pads(W.root, s);
  for (int i = 0; i < MAXP; i++) {
    W.on[i] = s->p[i].on;
    pug_build(&W.pug[i], s->p[i].team, looks[i]);
    t3_retain(W.pug[i].root);
    char name[16];
    snprintf(name, sizeof name, "Pug%d", i);
    W.pug_node[i] = hang(W.root, name, W.pug[i].root);
    cw_node_set(W.pug_node[i], "interpolate", cw_val_bool(true));
    W.pug[i].root->visible = W.on[i];
  }
  /* a ring on the floor under the cat while it flies: where it will land */
  W.ring_mat = t3_mesh_basic_material_new(0xffffff);
  W.ring_mat->color = lin(1, 0.85f, 0.45f);
  W.ring_mat->transparent = true;
  W.ring_mat->opacity = 0.6f;
  W.ring_mat->depth_write = false;
  t3_geometry *rg = t3_ring_geometry_new(0.82f, 1, 48, 1);
  t3_mesh *rm = t3_mesh_new(rg, W.ring_mat);
  t3_release(rg);
  t3_object_set_rotation(&rm->base, -1.5707963f, 0, 0);
  W.cat_ring = t3_group_new();
  t3_object_add(W.cat_ring, rm);
  t3_release(rm);
  t3_retain(W.cat_ring);
  W.ring_node = hang(W.root, "CatRing", W.cat_ring);
  cw_node_set(W.ring_node, "interpolate", cw_val_bool(true));
  W.cat_ring->visible = false;
  cat_build(&W.cat);
  t3_retain(W.cat.root);
  W.cat_node = hang(W.root, "Cat", W.cat.root);
  cw_node_set(W.cat_node, "interpolate", cw_val_bool(true));
  W.cat_q = (t3_quat){ 0, 0, 0, 1 };
  W.ready = true;
  world_tick(s);
}

/* ── animation ──────────────────────────────────────────────────────── */

static t3_quat qmul(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static t3_quat qaxis(t3_vec3 ax, float a) {
  float s, c;
  cw_sincos(a * 0.5f, &s, &c);
  return (t3_quat){ ax.x * s, ax.y * s, ax.z * s, c };
}
static t3_quat qnorm(t3_quat q) {
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return (t3_quat){ q.x / l, q.y / l, q.z / l, q.w / l };
}
static t3_quat qslerp_small(t3_quat a, t3_quat b, float t) { /* nlerp: fine for small steps */
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) b = (t3_quat){ -b.x, -b.y, -b.z, -b.w };
  return qnorm((t3_quat){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t });
}
static void set_rot(t3_object *o, float x, float y, float z) { t3_object_set_quaternion(o, t3_quat_from_euler((t3_euler){ x, y, z, T3_XYZ })); }

static void pug_anim(int i, const pug *u, pug_model *m, panim *a, bool showcase) {
  float spd = v3len(u->v);
  /* legs: a scampering gallop scaled by speed, tucked in the air */
  if (u->grounded || showcase) a->phase += 0.06f + spd * 0.045f;
  float run = u->grounded ? fminf(spd / 8, 1) : 0;
  /* a waddle with weight in it: the body rocks side to side with the trot and
   * leans into turns; the head bobs on the stride; a sneeze jerks it */
  t3_vec3 fw = pug_fwd(u);
  float yaw = cw_atan2(fw.x, fw.z), dyaw = yaw - a->yaw_last;
  if (dyaw > 3.1415927f) dyaw -= 6.2831853f;
  if (dyaw < -3.1415927f) dyaw += 6.2831853f;
  a->yaw_last = yaw;
  float lean = u->grounded ? cw_clampf(-dyaw * 60 * spd * 0.012f, -0.28f, 0.28f) : 0;
  a->roll += (lean - a->roll) * 0.15f;
  set_rot(m->roll, 0, 0, a->roll + run * 0.05f * cw_sin(a->phase));
  if (a->sneeze > 0) a->sneeze--;
  float sn = a->sneeze > 0 ? cw_sin((14 - a->sneeze) * 0.45f) * 0.35f : 0;
  m->head->position = (t3_vec3){ 0, 0.235f + run * 0.014f * cw_sin(a->phase * 2 + 0.6f), 0.33f };
  set_rot(m->head, sn - run * 0.03f * cw_sin(a->phase * 2), 0, a->roll * -0.4f);
  for (int k = 0; k < 4; k++) {
    float ph = a->phase + (k < 2 ? 0 : 1.6f) + (k & 1) * 0.35f;
    float sw = run * 0.9f * cw_sin(ph);
    if (!u->grounded && !showcase) sw = (k < 2 ? -0.9f : 0.9f); /* superpug: front paws forward, back paws back */
    set_rot(m->leg[k], sw, 0, 0);
  }
  /* ears: springy, they fly up in the air and at speed */
  float target = u->grounded ? -0.1f - spd * 0.03f : -0.9f;
  a->ear_v += (target - a->ear) * 0.18f;
  a->ear_v *= 0.82f;
  a->ear += a->ear_v;
  for (int s = 0; s < 2; s++) set_rot(m->ear[s], a->ear, 0, (s ? -1 : 1) * (0.1f + 0.15f * cw_sin(a->phase * 0.5f)));
  /* the tail wags, faster when it is happy (boosting) */
  a->wag += u->boosting ? 0.6f : 0.25f;
  set_rot(m->tail, 0, 0.5f * cw_sin(a->wag), 0);
  /* squash: a spring kicked by landings */
  if (a->land > 0) a->land--;
  a->squash_v += -a->squash * 0.25f;
  a->squash_v *= 0.8f;
  a->squash += a->squash_v;
  float sq = cw_clampf(a->squash, -0.3f, 0.3f);
  m->body->scale = (t3_vec3){ 1 + sq * 0.6f, 1 - sq, 1 + sq * 0.4f };
  /* blinks; the tongue lolls at full boost; the jet flames */
  if (a->blink > 0) a->blink--;
  else if (((W.ticks + i * 37) % 211) == 0) a->blink = 7;
  /* faces: a squint into the wind at supersonic, a pant with the tongue out
   * while boosting and for a while after (the tongue tip always shows: the underbite) */
  bool squint = u->supersonic && !showcase;
  for (int s = 0; s < 2; s++) {
    /* the upper lid rests a little droopy; it rolls down over the eye to blink */
    set_rot(m->lids[s], a->blink > 0 ? 1.45f : squint ? 0.75f : -0.35f, 0, 0);
  }
  float pant_target = u->boosting ? 1 : showcase ? 0.45f : 0;
  a->pant += (pant_target - a->pant) * (pant_target > a->pant ? 0.2f : 0.02f);
  m->tongue->scale = (t3_vec3){ 1, 1, 1 + 1.1f * a->pant };
  m->tongue->position.z = 0.085f + 0.015f * a->pant;
  set_rot(m->tongue, 0.35f * a->pant + 0.06f * a->pant * cw_sin(W.ticks * 0.6f + i), 0, 0);
  set_rot(m->jaw, 0.05f + 0.3f * a->pant * (0.8f + 0.2f * cw_sin(W.ticks * 0.6f + i)), 0, 0);
  m->flame->visible = u->boosting;
  if (u->boosting) {
    float fl = 0.85f + 0.3f * cw_sin(W.ticks * 1.7f + i);
    m->flame->scale = (t3_vec3){ 1, 1, fl * (u->supersonic ? 1.5f : 1) };
  }
  m->flame_mat->emissive_intensity = u->boosting ? 6 : 0.4f;
  if (m->hat) set_rot(m->hat, -a->ear * 0.08f, 0, 0);
}

/* place a carried object for this tick: the node renders between its last
 * and this position (a jump of more than 4 m is a teleport: no blend) */
static void place_node(cw_node n, t3_vec3 p, t3_quat q) {
  t3_vec3 was = cw_node3d_position(n);
  float dx = p.x - was.x, dy = p.y - was.y, dz = p.z - was.z;
  cw_node3d_snapshot(n);
  cw_node3d_set_position(n, p);
  cw_node3d_set_quaternion(n, q);
  if (dx * dx + dy * dy + dz * dz > 16) cw_node3d_snapshot(n);
}

void world_tick(const sim *s) {
  if (!W.ready) return;
  W.ticks++;
  net_tick(s);
  /* landings squash, hits jolt */
  for (int e = 0; e < s->ev_n; e++) {
    const struct sim_event *ev = &s->ev[e];
    if (ev->kind == EV_LAND && ev->a >= 0) {
      W.pa[ev->a].squash_v -= 0.06f + 0.15f * ev->k;
      W.pa[ev->a].ear_v += 0.35f + 0.5f * ev->k; /* the ears flop down on landing */
    }
    if (ev->kind == EV_GO) /* a snort at the whistle */
      for (int k = 0; k < MAXP; k++) W.pa[k].sneeze = 14 + (k * 3) % 7;
    if (ev->kind == EV_DEMO && ev->a >= 0) W.pa[ev->a].sneeze = 14;
    if (ev->kind == EV_JUMP || ev->kind == EV_DOUBLE) W.pa[ev->a].squash_v += 0.08f;
    if (ev->kind == EV_HIT) {
      W.cat_squash_v -= 0.1f + 0.25f * ev->k;
      if (ev->k > 0.35f) W.cat_angry = 50;
    }
    if (ev->kind == EV_BOUNCE) W.cat_squash_v -= 0.05f + 0.15f * ev->k;
    if (ev->kind == EV_GOAL) W.cat_dizzy = 400;
  }
  for (int i = 0; i < MAXP; i++) {
    if (!W.on[i] || W.showcase) continue;
    const pug *u = &s->p[i];
    pug_model *m = &W.pug[i];
    if (u->dead) {
      /* demolished: the wreck tumbles round the bowl (the sim's body), then pops away */
      m->root->visible = u->dead > DEMO_TICKS - WRECK_TICKS;
      place_node(W.pug_node[i], u->p, u->q);
      continue;
    }
    m->root->visible = true;
    place_node(W.pug_node[i], u->p, u->q);
    pug_anim(i, u, m, &W.pa[i], false);
  }
  /* the cat: it rolls with the ball; at rest it sits up and glares */
  cat_model *c = &W.cat;
  W.cat_ticks++;
  t3_vec3 cat_at = W.showcase ? (t3_vec3){ 0, RB, -AL + 15 } : s->ball;
  float bs = v3len(s->ball_v);
  W.cat_air = !s->ball_low ? fminf(W.cat_air + 0.08f, 1) : fmaxf(W.cat_air - 0.12f, 0);
  float spin = v3len(s->ball_spin);
  if (W.showcase || s->phase == PH_KICKOFF || (bs < 1.0f && s->ball_rest)) {
    /* upright, facing the broadcast camera's side (on parade: the camera), a slow breath */
    float yaw = (W.showcase ? 3.1415927f : 3.1415927f * 0.5f) + 0.3f * cw_sin(W.cat_ticks * 0.01f);
    W.cat_q = qslerp_small(W.cat_q, qaxis((t3_vec3){ 0, 1, 0 }, yaw), 0.06f);
  } else if (spin > 1e-3f) {
    t3_vec3 ax = v3mul(s->ball_spin, 1.0f / spin);
    W.cat_q = qnorm(qmul(qaxis(ax, spin / 60.0f), W.cat_q));
  }
  place_node(W.cat_node, cat_at, W.cat_q);
  W.cat_squash_v += -W.cat_squash * 0.3f;
  W.cat_squash_v *= 0.78f;
  W.cat_squash += W.cat_squash_v;
  float sq = cw_clampf(W.cat_squash, -0.25f, 0.25f) + 0.015f * cw_sin(W.cat_ticks * 0.08f) * (s->phase == PH_KICKOFF);
  c->body->scale = (t3_vec3){ 1 - sq * 0.5f, 1 + sq, 1 - sq * 0.5f };
  /* in the air the paws and tail fling out */
  float fling = W.cat_air;
  for (int k = 0; k < 4; k++) {
    float sx = k & 1 ? 1.0f : -1.0f, front = k < 2 ? 1.0f : -1.0f;
    set_rot(c->paw[k], front * fling * 0.9f + 0.15f * fling * cw_sin(W.cat_ticks * 0.5f + k), 0, -sx * fling * 0.9f);
    c->paw[k]->position = (t3_vec3){ sx * RB * (0.42f + 0.2f * fling), -RB * (0.82f - 0.1f * fling), front * RB * (0.45f + 0.2f * fling) };
  }
  set_rot(c->tail, fling * 0.8f * cw_sin(W.cat_ticks * 0.3f), fling * 0.6f, 0);
  /* ears flatten when it is angry */
  if (W.cat_angry > 0) W.cat_angry--;
  if (W.cat_dizzy > 0) W.cat_dizzy--;
  bool angry = W.cat_angry > 0 || (bs > 18 && W.cat_dizzy == 0);
  bool dizzy = W.cat_dizzy > 0 && !angry;
  for (int k = 0; k < 2; k++) set_rot(c->ear[k], angry ? -0.9f : 0, 0, (k ? -1 : 1) * (angry ? 0.9f : 0.35f));
  c->eyes_open->visible = !angry && !dizzy;
  c->eyes_angry->visible = angry;
  c->eyes_dizzy->visible = dizzy;
  if (dizzy) set_rot(c->eyes_dizzy, 0, 0, W.cat_ticks * 0.2f);
  c->mouth_yowl->visible = angry || dizzy;
  c->mouth_grump->visible = !c->mouth_yowl->visible;
  set_rot(c->whisk, 0, 0, angry ? 0.25f : 0.05f * cw_sin(W.cat_ticks * 0.05f));
  /* hidden while the goal's fur puff hides it, back for the kickoff */
  c->root->visible = !(s->phase == PH_GOAL && s->phase_ticks > 8 && s->phase_ticks < 150);
  {
    float h = s->ball.y - RB;
    bool over = fabsf(s->ball.x) < AW - AR && fabsf(s->ball.z) < AL - AR + 1;
    W.cat_ring->visible = !W.showcase && over && h > 0.6f && c->root->visible;
    if (W.cat_ring->visible) {
      float k = cw_clampf(h / 10, 0, 1), sc = RB * (0.7f + 0.6f * k);
      place_node(W.ring_node, (t3_vec3){ s->ball.x, 0.06f, s->ball.z }, (t3_quat){ 0, 0, 0, 1 });
      W.cat_ring->scale = (t3_vec3){ sc, sc, sc };
      W.ring_mat->opacity = 0.75f - 0.45f * k;
    }
  }
  pads_tick(s);
  W.last_phase = s->phase;
}

/* the title and the select screens: the pugs line up and show off */
void world_showcase(int ticks, const pug_look looks[MAXP], int focus) {
  (void)looks;
  if (!W.ready) return;
  W.showcase = true;
  for (int i = 0; i < MAXP; i++) {
    pug_model *m = &W.pug[i];
    if (!W.on[i]) continue;
    pug u = { 0 };
    u.grounded = true;
    u.boosting = (ticks / 90 + i) % 4 == 0;
    float x = (i - 2.5f) * 1.6f, hop = fabsf(cw_sin(ticks * 0.08f + i * 0.9f)) * (i == focus ? 0.5f : 0.15f);
    if (DEV.tt) hop = 0;
    m->root->visible = !(DEV.tt && i != DEV.tt_pug);
    t3_vec3 at = { x, RIDE + hop, -AL + 9 };
    float yaw = 3.1415927f + (DEV.tt ? 0 : 0.25f * cw_sin(ticks * 0.02f + i));
    place_node(W.pug_node[i], at, qaxis((t3_vec3){ 0, 1, 0 }, yaw));
    pug_anim(i, &u, m, &W.pa[i], true);
  }
}

t3_vec3 world_pug_pos(int i) { return W.pug[i].root ? cw_node3d_position(W.pug_node[i]) : (t3_vec3){ 0, 0, 0 }; }

/* the pug as drawn this frame (its node's object), for camera checks */
t3_vec3 world_pug_render_pos(int i) {
  t3_object *o = cw_node3d_object(W.pug_node[i]);
  return o ? o->position : (t3_vec3){ 0, 0, 0 };
}

/* split screen draws each view on its own: a pug or the cat far from the
 * view's camera is a few dozen pixels there, so it draws as its far copy */
#define FAR_PUG 9.0f
#define FAR_CAT 20.0f
static void set_far(int k, t3_object *body, t3_object *far, bool on) {
  if (!body || !far || W.far_on[k] == on) return;
  W.far_on[k] = on;
  body->visible = !on;
  far->visible = on;
}
void world_view_detail(t3_vec3 eye, bool full) {
  if (!W.ready) return;
  for (int i = 0; i <= MAXP; i++) {
    t3_object *o = cw_node3d_object(i < MAXP ? W.pug_node[i] : W.cat_node);
    if (!o) continue;
    t3_vec3 d = v3sub(o->position, eye);
    float far = i < MAXP ? FAR_PUG : FAR_CAT;
    bool on = !full && v3dot(d, d) > far * far;
    if (i < MAXP) set_far(i, W.pug[i].body, W.pug[i].far, on);
    else set_far(i, W.cat.body, W.cat.far, on);
  }
}
