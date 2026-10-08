/* spout world: a papercraft diorama round one rain spout, built with three.c
 * (three.js r186) and hung under cartwheel nodes. Render-only: nothing here
 * feeds the simulation.
 *
 *   the spout   a faceted paper tube (flat folds), collars every 3 m, bracket
 *               straps at the checkpoints with paper stays back to the house,
 *               paper wraps that darken and gloss while wet, the loose end of
 *               each one peeled off the pipe and lifting in the wind: a
 *               small stiff Cloth3D sheet (cartwheel/cloth.h) pinned where
 *               the glue holds, gusting with the rain, patted by drops,
 *               drooping heavier as it soaks, and drawn on twelves with the
 *               spider in stop-motion
 *   the spider  a folded-card body clinging belly to the pipe, eight two-part
 *               paper legs (two-bone IK to feet planted on the pipe) that step with
 *               the distance walked (optionally on twelves, like stop-motion),
 *               googly eyes whose pupils hang toward the ground
 *   the garden  a paper meadow, folded grass tufts, a water butt, flowers,
 *               a house wall, a roof and a gutter, houses and trees cut out
 *               on sticks at three depths, cloud cutouts, a paper sun
 *   weather     cellophane rain streaks, blue paper streams down the pipe,
 *               white wave fronts, paper leaves, the sun turning in after */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/cloth.h"
#include "game.h"

#define RAIN_N 360
#define FACETS 14

uint32_t world_hash = CW_HASH32_INIT;

/* ── a small mesh builder: non-indexed triangles with face normals (flat folds) ── */

typedef struct mb { float *p, *n, *uv; int count, cap; } mb;
static void mb_tri(mb *b, t3_vec3 a, t3_vec3 c, t3_vec3 d, float ua, float va, float uc, float vc, float ud, float vd) {
  if (b->count + 3 > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 384;
    b->p = realloc(b->p, sizeof(float) * 3 * b->cap);
    b->n = realloc(b->n, sizeof(float) * 3 * b->cap);
    b->uv = realloc(b->uv, sizeof(float) * 2 * b->cap);
  }
  t3_vec3 e1 = { c.x - a.x, c.y - a.y, c.z - a.z }, e2 = { d.x - a.x, d.y - a.y, d.z - a.z };
  t3_vec3 nn = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
  float l = sqrtf(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
  if (l > 0) nn.x /= l, nn.y /= l, nn.z /= l;
  t3_vec3 v[3] = { a, c, d };
  float uu[3] = { ua, uc, ud }, vv[3] = { va, vc, vd };
  for (int i = 0; i < 3; i++) {
    int k = b->count++;
    b->p[k * 3] = v[i].x, b->p[k * 3 + 1] = v[i].y, b->p[k * 3 + 2] = v[i].z;
    b->n[k * 3] = nn.x, b->n[k * 3 + 1] = nn.y, b->n[k * 3 + 2] = nn.z;
    b->uv[k * 2] = uu[i], b->uv[k * 2 + 1] = vv[i];
  }
}
static void mb_quad(mb *b, t3_vec3 a, t3_vec3 c, t3_vec3 d, t3_vec3 e, float u0, float v0, float u1, float v1) {
  /* a-c-d-e counter-clockwise seen from the front */
  mb_tri(b, a, c, d, u0, v0, u1, v0, u1, v1);
  mb_tri(b, a, d, e, u0, v0, u1, v1, u0, v1);
}
/* every geometry built for the world so far (this world_build): a mesh with
 * the same triangles as an earlier one draws that geometry, so identical
 * pieces (houses, sticks, leaves, wings, spools, stream bands) share one and
 * three.c batches them into instanced draws */
static struct { t3_geometry **g; int n, cap; } built;
static bool same_attr(const t3_attribute *a, const float *v, int count, int size) {
  return a && a->count == count && a->item_size == size && !memcmp(a->array, v, sizeof(float) * (size_t)size * count);
}
static t3_geometry *built_find(const mb *b) {
  for (int i = 0; i < built.n; i++) {
    t3_geometry *g = built.g[i];
    if (same_attr(g->attributes[T3_ATTR_POSITION], b->p, b->count, 3) && same_attr(g->attributes[T3_ATTR_NORMAL], b->n, b->count, 3) &&
        same_attr(g->attributes[T3_ATTR_UV], b->uv, b->count, 2))
      return g;
  }
  return NULL;
}
static void built_free(void) {
  for (int i = 0; i < built.n; i++) t3_release(built.g[i]);
  free(built.g);
  memset(&built, 0, sizeof built);
}
/* a new reference to the geometry of b's triangles (b is emptied) */
static t3_geometry *mb_finish(mb *b) {
  world_hash = cw_hash32(b->p, sizeof(float) * 3 * b->count, world_hash);
  t3_geometry *g = built_find(b);
  if (g) {
    free(b->p), free(b->n), free(b->uv);
    memset(b, 0, sizeof *b);
    return t3_retain(g);
  }
  g = t3_geometry_new();
  t3_attribute *p = t3_attribute_new(T3_FLOAT32, b->p, b->count, 3), *n = t3_attribute_new(T3_FLOAT32, b->n, b->count, 3),
               *uv = t3_attribute_new(T3_FLOAT32, b->uv, b->count, 2);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, p);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, n);
  t3_geometry_set_attribute(g, T3_ATTR_UV, uv);
  t3_release(p), t3_release(n), t3_release(uv);
  free(b->p), free(b->n), free(b->uv);
  memset(b, 0, sizeof *b);
  if (built.n == built.cap) {
    built.cap = built.cap ? built.cap * 2 : 64;
    built.g = realloc(built.g, sizeof *built.g * (size_t)built.cap);
  }
  built.g[built.n++] = t3_retain(g);
  return g;
}

static t3_vec3 on_cyl(float r, float a, float y) {
  float s, c;
  cw_sincos(a, &s, &c);
  return (t3_vec3){ r * s, y, r * c };
}
/* a band of the tube from angle a0 over len, y0..y1, n flat facets; outward faces */
static void mb_band(mb *b, float r, float a0, float len, float y0, float y1, int n, float ru, float rv) {
  for (int i = 0; i < n; i++) {
    float a = a0 + len * i / n, c = a0 + len * (i + 1) / n;
    mb_quad(b, on_cyl(r, a, y0), on_cyl(r, c, y0), on_cyl(r, c, y1), on_cyl(r, a, y1), ru * i / n, 0, ru * (i + 1) / n, rv);
  }
}
/* the same, facing inward (seen from inside the ring): top edge per segment */
static void mb_band_in(mb *b, float r, float a0, float len, float y0, const float *y1, int n, float ru, float rv) {
  for (int i = 0; i < n; i++) {
    float a = a0 + len * i / n, c = a0 + len * (i + 1) / n;
    t3_vec3 p0 = on_cyl(r, a, y0), p1 = on_cyl(r, c, y0), p2 = on_cyl(r, c, y1[i + 1]), p3 = on_cyl(r, a, y1[i]);
    mb_tri(b, p1, p0, p3, ru * (i + 1) / n, 0, ru * i / n, 0, ru * i / n, rv * y1[i] / 90);
    mb_tri(b, p1, p3, p2, ru * (i + 1) / n, 0, ru * i / n, rv * y1[i] / 90, ru * (i + 1) / n, rv * y1[i + 1] / 90);
  }
}
/* a box (for straps, stays, the gutter, houses), axis aligned, centred */
static void mb_box(mb *b, t3_vec3 c, t3_vec3 h, float rep) {
  t3_vec3 v[8];
  for (int i = 0; i < 8; i++) v[i] = (t3_vec3){ c.x + (i & 1 ? h.x : -h.x), c.y + (i & 2 ? h.y : -h.y), c.z + (i & 4 ? h.z : -h.z) };
  float ux = h.x * 2 * rep, uy = h.y * 2 * rep, uz = h.z * 2 * rep;
  mb_quad(b, v[4], v[5], v[7], v[6], 0, 0, ux, uy); /* +z */
  mb_quad(b, v[1], v[0], v[2], v[3], 0, 0, ux, uy); /* -z */
  mb_quad(b, v[5], v[1], v[3], v[7], 0, 0, uz, uy); /* +x */
  mb_quad(b, v[0], v[4], v[6], v[2], 0, 0, uz, uy); /* -x */
  mb_quad(b, v[6], v[7], v[3], v[2], 0, 0, ux, uz); /* +y */
  mb_quad(b, v[0], v[1], v[5], v[4], 0, 0, ux, uz); /* -y */
}

/* ── materials ─────────────────────────────────────────────────────── */

static t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}
/* paper: Standard with the full paper set; tint multiplies the paper's colour */
/* paper: Physical, matte (roughness from the fibre map, near 0.9), a low
 * dielectric specular so sheets never show a hotspot */
static t3_material *paper(int kind, float ru, float rv, float tr, float tg, float tb) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  paper_apply(m, kind, ru, rv);
  m->color = lin(tr, tg, tb);
  m->roughness = 1;
  m->metalness = 0;
  m->specular_intensity = 0.25f;
  return m;
}
/* the white core of card where it is cut: every cutout's edge band */
static t3_material *edge_mat;

/* ── the scene graph ───────────────────────────────────────────────── */

typedef struct leg { t3_object *hip, *knee; float side, base; } leg;
#define LEG_SEG 0.16f /* each leg segment (the strip in build_spider) */
static struct {
  cw_node node;      /* the cartwheel node everything hangs under */
  t3_object *root;
  const level_def *L;
  bool title;
  /* spider */
  t3_object *spider, *body, *abdomen, *pupils[2], *eyes[2];
  leg legs[8];
  float pupil_off[2][2], pupil_vel[2][2];
  float flip; /* 0 clinging belly to the pipe .. 1 turned out while washed down */
  /* pipe and weather */
  t3_mesh *patches[MAX_PATCHES];
  t3_material *patch_mat[MAX_PATCHES];
  /* the wraps' loose ends: cloth beside a world holding only the pipe */
  cw_world3d air;
  cw_cloth flap[MAX_PATCHES * 2];        /* wrap i: 2i its th1 end, 2i + 1 its th0 end */
  t3_geometry *flap_geo[MAX_PATCHES * 2];
  t3_material *flap_mat[MAX_PATCHES * 2];
  float flap_acc;
  int flap_steps, flap_drawn;
  t3_mesh *streams[MAX_STREAMS];
  t3_material *stream_mat;
  t3_texture *stream_tex[1];
  t3_mesh *waves[8];
  t3_material *wave_mat;
  t3_mesh *leaves[MAX_LEAVES];
  t3_mesh *dews[MAX_DEWS];
  t3_mesh *rain;
  t3_material *rain_mat;
  t3_object *sun, *sun_rays;
  /* stage 3: silk line, the bird and its mark, flies, spools */
  t3_mesh *silk;
  t3_object *bird, *bird_wing[2], *mark;
  t3_object *flies[MAX_FLIES], *fly_wing[MAX_FLIES][2], *spools[MAX_SPOOLS];
  t3_object *clouds[6];
  t3_material *cloud_mat, *backdrop_mat, *hill_mat[3];
  t3_color hill_glow[3], backdrop_glow;
  float cloud_x[6];
  /* camera smoothing (render frames) */
  float cam_phi, last_t;
  t3_vec3 cam_eye;
  bool cam_live;
  float rain_amount, sun_amount;
} W;

/* a mesh of g (the caller's reference to g passes to the mesh) under parent */
static t3_mesh *add_mesh(t3_object *parent, t3_geometry *g, t3_material *m, bool cast, bool recv) {
  t3_mesh *me = t3_mesh_new(g, m);
  t3_release(g);
  T3_OBJ(me)->cast_shadow = cast;
  T3_OBJ(me)->receive_shadow = recv;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}

/* an icosahedron with flat facets (each face its own normal) */
static t3_geometry *faceted_ico(float r) {
  t3_geometry *ico = t3_icosahedron_geometry_new(r, 0), *g = t3_geometry_to_non_indexed(ico);
  t3_release(ico);
  t3_geometry_compute_vertex_normals(g);
  return g;
}

/* a flat paper cutout from a polygon outline (in its XY plane), front and
 * back, plus its cut edge as a separate whiter band (the paper's core) */
static t3_geometry *cutout_part(const float *xy, int n, float thick, bool edges) {
  mb b = { 0 };
  float cx = 0, cy = 0;
  for (int i = 0; i < n; i++) cx += xy[i * 2], cy += xy[i * 2 + 1];
  cx /= n, cy /= n;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    t3_vec3 c = { xy[i * 2], xy[i * 2 + 1], thick }, d = { xy[j * 2], xy[j * 2 + 1], thick };
    t3_vec3 c2 = { xy[i * 2], xy[i * 2 + 1], -thick }, d2 = { xy[j * 2], xy[j * 2 + 1], -thick };
    if (edges) {
      mb_quad(&b, c2, d2, d, c, 0, 0, 0.05f, 0.05f);
      continue;
    }
    t3_vec3 a = { cx, cy, thick }, a2 = { cx, cy, -thick };
    mb_tri(&b, a, c, d, cx * 0.5f, cy * 0.5f, c.x * 0.5f, c.y * 0.5f, d.x * 0.5f, d.y * 0.5f);
    mb_tri(&b, a2, d2, c2, cx * 0.5f, cy * 0.5f, d.x * 0.5f, d.y * 0.5f, c.x * 0.5f, c.y * 0.5f);
  }
  return mb_finish(&b);
}
static t3_mesh *add_mesh(t3_object *parent, t3_geometry *g, t3_material *m, bool cast, bool recv);
/* a cutout hung under parent: its faces in m, its edge in the white core */
static t3_mesh *cutout_add(t3_object *parent, const float *xy, int n, float thick, t3_material *m, bool cast) {
  t3_object *g = t3_group_new();
  add_mesh(g, cutout_part(xy, n, thick, false), m, cast, true);
  add_mesh(g, cutout_part(xy, n, thick, true), edge_mat, false, true);
  t3_object_add(parent, g);
  t3_release(g);
  return (t3_mesh *)g;
}
static int blob_xy(float *xy, float rx, float ry, int n, uint32_t seed, float bump) {
  if (n > 32) n = 32;
  for (int i = 0; i < n; i++) {
    float a = 6.2831853f * i / n;
    uint32_t h = (uint32_t)i * 2654435761u ^ seed * 40503u;
    h ^= h >> 15;
    float k = 1 + bump * (((h & 255) / 255.0f) - 0.5f);
    xy[i * 2] = rx * k * cw_cos(a);
    xy[i * 2 + 1] = ry * k * cw_sin(a);
  }
  return n;
}
static t3_mesh *blob_add(t3_object *parent, float rx, float ry, int n, uint32_t seed, float bump, float thick, t3_material *m, bool cast) {
  float xy[64];
  n = blob_xy(xy, rx, ry, n, seed, bump);
  return cutout_add(parent, xy, n, thick, m, cast);
}

/* ── the five sets: one palette and dressing per spout ─────────────── */

enum { WALL_BRICK, WALL_PLANKS, WALL_PANELS, WALL_BAMBOO };
typedef struct theme {
  int wall;
  float piece[4][3], back[3];   /* raised wall pieces, the sheet behind them */
  float pipe[3];
  float glow[3];                /* the backdrop's light */
  float hills[3][3];
  bool skyline;                 /* city blocks instead of rolling hills */
  int ground;                   /* paper kind of the floor */
  float ground_tint[3];
  int tufts;                    /* 0 none, 1 grass, 2 snow drifts */
  bool flowers, pines, palms, lit_windows;
  float crown[3], house[3];
} theme;

static const theme THEMES[THEME_COUNT] = {
  [THEME_SPRING] = { WALL_BRICK, { { 0.78f, 0.36f, 0.27f }, { 0.72f, 0.31f, 0.24f }, { 0.84f, 0.44f, 0.32f }, { 0.68f, 0.35f, 0.3f } }, { 0.42f, 0.36f, 0.33f },
                     { 1, 1, 1 }, { 0.9f, 0.86f, 0.78f }, { { 0.70f, 0.80f, 0.78f }, { 0.58f, 0.74f, 0.52f }, { 0.45f, 0.64f, 0.38f } }, false,
                     PAPER_GRASS, { 1, 1, 1 }, 1, true, false, false, false, { 0.6f, 0.85f, 0.5f }, { 0.98f, 0.88f, 0.72f } },
  [THEME_CITY] = { WALL_PANELS, { { 0.62f, 0.64f, 0.68f }, { 0.56f, 0.58f, 0.63f }, { 0.68f, 0.69f, 0.72f }, { 0.6f, 0.6f, 0.62f } }, { 0.25f, 0.26f, 0.3f },
                   { 0.75f, 0.85f, 0.8f }, { 0.22f, 0.26f, 0.45f }, { { 0.32f, 0.36f, 0.55f }, { 0.25f, 0.28f, 0.45f }, { 0.18f, 0.2f, 0.33f } }, true,
                   PAPER_PLAIN, { 0.45f, 0.46f, 0.5f }, 0, false, false, false, true, { 0.3f, 0.45f, 0.35f }, { 0.55f, 0.58f, 0.7f } },
  [THEME_AUTUMN] = { WALL_PLANKS, { { 0.66f, 0.2f, 0.16f }, { 0.6f, 0.17f, 0.14f }, { 0.72f, 0.24f, 0.18f }, { 0.56f, 0.19f, 0.17f } }, { 0.3f, 0.12f, 0.1f },
                     { 0.8f, 0.78f, 0.7f }, { 0.98f, 0.8f, 0.6f }, { { 0.92f, 0.72f, 0.5f }, { 0.85f, 0.55f, 0.28f }, { 0.68f, 0.42f, 0.2f } }, false,
                     PAPER_GRASS, { 0.95f, 0.75f, 0.45f }, 1, false, false, false, false, { 0.95f, 0.55f, 0.2f }, { 0.95f, 0.85f, 0.65f } },
  [THEME_WINTER] = { WALL_PLANKS, { { 0.86f, 0.88f, 0.92f }, { 0.8f, 0.83f, 0.88f }, { 0.9f, 0.9f, 0.94f }, { 0.78f, 0.8f, 0.86f } }, { 0.5f, 0.55f, 0.62f },
                     { 0.85f, 0.9f, 1 }, { 0.82f, 0.86f, 0.92f }, { { 0.88f, 0.9f, 0.95f }, { 0.82f, 0.86f, 0.92f }, { 0.94f, 0.95f, 0.98f } }, false,
                     PAPER_WHITE, { 0.92f, 0.94f, 1 }, 2, false, true, false, false, { 0.35f, 0.55f, 0.45f }, { 0.95f, 0.75f, 0.6f } },
  [THEME_JUNGLE] = { WALL_BAMBOO, { { 0.78f, 0.75f, 0.42f }, { 0.7f, 0.72f, 0.38f }, { 0.82f, 0.78f, 0.48f }, { 0.66f, 0.7f, 0.36f } }, { 0.3f, 0.32f, 0.18f },
                     { 0.62f, 0.85f, 0.45f }, { 0.75f, 0.92f, 0.82f }, { { 0.55f, 0.78f, 0.62f }, { 0.35f, 0.62f, 0.38f }, { 0.22f, 0.48f, 0.25f } }, false,
                     PAPER_GRASS, { 0.6f, 0.85f, 0.55f }, 1, true, false, true, false, { 0.25f, 0.6f, 0.3f }, { 0.85f, 0.75f, 0.55f } },
};
static const theme *T_;
float world_pipe_tint[3];

/* the paper wraps' loose ends: past each end of a paper wrap (th0, th1) the
 * last 0.24 m of the sheet never took the glue, a flap from the wrap's top
 * down most of its height, pinned along the glued edge and free elsewhere.
 * Paper, not fabric: light, stiff across its folds (a coarse grid, flat
 * shaded, so it bends at a few creases like card), and it keeps off the
 * pipe (a capsule in its own small world). Moss wraps stay put. */
#define FLAP_U 4
#define FLAP_V 5
static void build_flaps(t3_object *root, const level_def *L) {
  cw_world3d_def wd = cw_world3d_default_def();
  wd.workers = 1;
  W.air = cw_world3d_create(&wd);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  cw_body3d pipe = cw_body3d_create(W.air, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  cw_shape3d_capsule(pipe, &sd, (t3_vec3){ 0, -0.2f, 0 }, (t3_vec3){ 0, L->height + 0.2f, 0 }, PIPE_R + 0.014f);
  float inv[FLAP_U * FLAP_V];
  for (int k = 0; k < FLAP_U * FLAP_V; k++) inv[k] = k % FLAP_U ? 1.0f : 0.0f;
  for (int i = 0; i < L->n_patches; i++) {
    const patch *p = &L->patches[i];
    if (!p->dries) continue;
    for (int e = 0; e < 2; e++) {
      int fi = i * 2 + e;
      float th = e ? p->th0 : p->th1, dir = e ? -1.0f : 1.0f, hgt = (p->y1 - p->y0) * 0.8f, wid = 0.24f * dir, s, c;
      cw_sincos(th, &s, &c);
      cw_cloth_def d = cw_cloth_default_def();
      d.inv_mass = inv;
      d.mass = 0.02f;            /* a scrap of paper */
      d.bend_compliance = 2e-5f; /* stiff as card */
      d.damping = 0.5f;
      d.thickness = 0.012f;
      d.friction = 0.6f;
      d.quality_scaled = true;
      d.workers = 1;
      t3_vec3 o = on_cyl(PIPE_R + 0.016f, th, p->y1);
      W.flap[fi] = cw_cloth_create_grid(W.air, &d, FLAP_U, FLAP_V, o, (t3_vec3){ c * wid, 0, -s * wid }, (t3_vec3){ 0, -hgt, 0 });
      if (!cw_cloth_valid(W.flap[fi])) continue;
      t3_material *m = t3_mesh_physical_material_new(0xffffff);
      paper_apply(m, PAPER_WHITE, 0.24f / 0.4f, hgt / 0.4f);
      m->color = W.patch_mat[i]->color;
      m->roughness = 1;
      m->side = T3_DOUBLE_SIDE;
      m->flat_shading = true;
      W.flap_mat[fi] = m;
      W.flap_geo[fi] = cw_cloth_geometry_new(W.flap[fi]);
      t3_mesh *me = add_mesh(root, W.flap_geo[fi], m, true, true);
      T3_OBJ(me)->frustum_culled = false;
    }
  }
}

/* one fixed step of the flaps: gusts with the rain, drops patting the paper */
static void flaps_step(float rain, float wet) {
  float gust = 0.5f + 0.5f * cw_sin(W.flap_steps * 0.05f) * cw_sin(W.flap_steps * 0.013f + 1);
  float v = 0.8f + 6.5f * rain * gust; /* a breeze; gusts in the rain */
  for (int i = 0; i < MAX_PATCHES * 2; i++) {
    if (!cw_cloth_valid(W.flap[i])) continue;
    /* the air streams round the pipe: past a wrap's loose end it runs on
     * along the sheet and off the surface, lifting it like a flag */
    const patch *p = &W.L->patches[i / 2];
    float s, c, dir = i & 1 ? -1.0f : 1.0f;
    cw_sincos(i & 1 ? p->th0 : p->th1, &s, &c);
    t3_vec3 wind = { v * (dir * c + 0.5f * s), 0.15f * v, v * (-dir * s + 0.5f * c) };
    cw_cloth_set_wind(W.flap[i], wind, 1, 0.3f, 0.8f);
    cw_cloth_set_gravity_scale(W.flap[i], 1 + 1.5f * wet); /* soaked paper hangs heavier */
    uint32_t h = (uint32_t)(W.flap_steps * 31 + i) * 2654435761u;
    if (rain > 0.3f && (h >> 28) < 3) { /* a raindrop lands on it */
      int n = 0;
      const t3_vec3 *pp = cw_cloth_positions(W.flap[i], &n);
      if (n) cw_cloth_impulse(W.flap[i], pp[(h >> 8) % (uint32_t)n], 0.1f, (t3_vec3){ 0, -0.9f * rain, 0 });
    }
    cw_cloth_step(W.flap[i], 1 / 60.0f);
  }
  W.flap_steps++;
}

static void build_pipe(t3_object *root, const level_def *L) {
  float H = L->height;
  /* the tube: cardstock folded into 14 flat panels */
  mb b = { 0 };
  mb_band(&b, PIPE_R, 0, 6.2831853f, -0.2f, H + 0.2f, FACETS, 7, H / 0.5f);
  const theme *T = &THEMES[L->theme];
  t3_material *m = paper(PAPER_PIPE, 1, 1, T->pipe[0], T->pipe[1], T->pipe[2]);
  add_mesh(root, mb_finish(&b), m, true, true);
  t3_release(m);
  /* the folds: a slightly darker crease line down every edge, and at the
   * back the glued tab where the sheet's two ends overlap */
  mb bk = { 0 };
  for (int i = 0; i < FACETS; i++) {
    float a = 6.2831853f * i / FACETS;
    if (i == FACETS / 2) continue; /* the seam has the tab instead */
    mb_band(&bk, PIPE_R + 0.002f, a - 0.012f, 0.024f, -0.2f, H + 0.2f, 1, 0.05f, H / 0.5f);
  }
  t3_material *mk = paper(PAPER_PIPE, 1, 1, 0.78f, 0.8f, 0.84f);
  add_mesh(root, mb_finish(&bk), mk, false, true);
  t3_release(mk);
  mb bt = { 0 };
  float ta = 3.1415927f;
  t3_vec3 r0 = on_cyl(PIPE_R, ta, 0), r1 = on_cyl(PIPE_R + 0.035f, ta + 0.05f, 0);
  mb_quad(&bt, (t3_vec3){ r0.x, -0.2f, r0.z }, (t3_vec3){ r1.x, -0.2f, r1.z }, (t3_vec3){ r1.x, H + 0.2f, r1.z }, (t3_vec3){ r0.x, H + 0.2f, r0.z }, 0, 0, 0.1f, H);
  mb_quad(&bt, (t3_vec3){ r1.x, -0.2f, r1.z }, (t3_vec3){ r0.x, -0.2f, r0.z }, (t3_vec3){ r0.x, H + 0.2f, r0.z }, (t3_vec3){ r1.x, H + 0.2f, r1.z }, 0, 0, 0.1f, H);
  add_mesh(root, mb_finish(&bt), edge_mat, true, true);
  /* collars every 3 m and the straps at the brackets, darker card */
  t3_material *mc = paper(PAPER_PIPE, 1, 1, 0.72f, 0.76f, 0.82f);
  t3_material *ms = paper(PAPER_KRAFT, 1, 1, 0.9f, 0.9f, 0.9f);
  mb bc = { 0 }, bs = { 0 };
  bool bamboo = L->twist == TWIST_NODES; /* the bamboo spout: a node every 2 m, a ledge the water stops at */
  for (float y = bamboo ? 2 : 3; y < H; y += bamboo ? 2 : 3)
    mb_band(&bc, PIPE_R + (bamboo ? 0.06f : 0.035f), 0, 6.2831853f, y - (bamboo ? 0.09f : 0.06f), y + (bamboo ? 0.09f : 0.06f), FACETS, 2, 0.1f);
  for (int i = 1; i < L->n_checks; i++) {
    float y = L->checks[i];
    mb_band(&bs, PIPE_R + 0.05f, 0, 6.2831853f, y - 0.1f, y + 0.1f, FACETS, 3, 0.2f);
    /* two paper stays back to the house wall */
    mb_box(&bs, (t3_vec3){ -0.3f, y, -3.2f }, (t3_vec3){ 0.03f, 0.05f, 2.7f }, 1);
    mb_box(&bs, (t3_vec3){ 0.3f, y, -3.2f }, (t3_vec3){ 0.03f, 0.05f, 2.7f }, 1);
  }
  add_mesh(root, mb_finish(&bc), mc, true, true);
  add_mesh(root, mb_finish(&bs), ms, true, true);
  t3_release(mc), t3_release(ms);
  /* the paper wraps: one mesh each, so each darkens on its own */
  for (int i = 0; i < L->n_patches; i++) {
    const patch *p = &L->patches[i];
    mb bp = { 0 };
    int n = (int)((p->th1 - p->th0) / 0.31f) + 1;
    mb_band(&bp, PIPE_R + 0.014f, p->th0, p->th1 - p->th0, p->y0, p->y1, n, (p->th1 - p->th0) * PIPE_R / 0.4f, (p->y1 - p->y0) / 0.4f);
    bool moss = !p->dries;
    t3_material *pm = t3_mesh_physical_material_new(0xffffff);
    paper_apply(pm, moss ? PAPER_GRASS : PAPER_WHITE, 1, 1);
    pm->color = moss ? lin(0.55f, 0.75f, 0.45f) : lin(0.98f, 0.95f, 0.86f);
    pm->roughness = 1;
    pm->clearcoat = 0;
    pm->clearcoat_roughness = 0.25f;
    W.patches[i] = add_mesh(root, mb_finish(&bp), pm, false, true);
    W.patch_mat[i] = pm;
    t3_release(pm);
  }
  build_flaps(root, L);
  /* the gutter and the roof eave at the top, a water butt at the foot */
  mb bg = { 0 };
  mb_box(&bg, (t3_vec3){ -6, H + 0.35f, 0 }, (t3_vec3){ 7.8f, 0.22f, 0.32f }, 1);
  t3_material *mg = paper(PAPER_KRAFT, 1, 1, 0.75f, 0.78f, 0.85f);
  add_mesh(root, mb_finish(&bg), mg, true, true);
  t3_release(mg);
  mb br = { 0 };
  float ry0 = H + 0.55f, ry1 = H + 3.4f;
  for (int i = 0; i < 12; i++) { /* overlapping paper shingles */
    float z0 = 0.4f - i * 0.55f, z1 = z0 - 0.7f, y0 = ry0 + (ry1 - ry0) * i / 12, y1 = ry0 + (ry1 - ry0) * (i + 1.3f) / 12;
    mb_quad(&br, (t3_vec3){ -14, y0, z0 }, (t3_vec3){ 1.8f, y0, z0 }, (t3_vec3){ 1.8f, y1, z1 }, (t3_vec3){ -14, y1, z1 }, 0, 0, 6, 0.5f);
  }
  t3_material *mr = paper(PAPER_BRICK, 1, 1, 0.55f, 0.45f, 0.5f);
  add_mesh(root, mb_finish(&br), mr, true, true);
  t3_release(mr);
  mb bw = { 0 };
  mb_band(&bw, 0.75f, 0, 6.2831853f, 0, 1.1f, 14, 3, 1);
  mb_band(&bw, 0.8f, 0, 6.2831853f, 0.95f, 1.05f, 14, 3, 0.2f);
  t3_material *mw = paper(PAPER_WOOD, 1, 1, 1, 1, 1);
  t3_mesh *butt = add_mesh(root, mb_finish(&bw), mw, true, true);
  t3_object_set_position(butt, 1.1f, 0, 0.9f);
  t3_release(mw);
}


static void build_wall(t3_object *root, const theme *T, float top) {
  mb bw = { 0 };
  mb_quad(&bw, (t3_vec3){ -14, 0, -6 }, (t3_vec3){ 1.6f, 0, -6 }, (t3_vec3){ 1.6f, top, -6 }, (t3_vec3){ -14, top, -6 }, 0, 0, 30, top * 2);
  t3_material *mback = paper(PAPER_PLAIN, 1, 1, T->back[0], T->back[1], T->back[2]);
  add_mesh(root, mb_finish(&bw), mback, false, true);
  t3_release(mback);
  mb bb[4] = { { 0 } };
  int row = 0;
  if (T->wall == WALL_BRICK) {
    const float bwid = 0.42f, bh = 0.17f, gap = 0.03f;
    for (float y = gap; y + bh < top; y += bh + gap, row++) {
      float x0 = -14 + ((row & 1) ? -bwid * 0.5f : 0);
      int col = 0;
      for (float x = x0; x < 1.6f; x += bwid + gap, col++) {
        float xa = fmaxf(x, -14), xb = fminf(x + bwid, 1.6f);
        if (xb - xa < 0.05f) continue;
        uint32_t h = (uint32_t)row * 73856093u ^ (uint32_t)col * 19349663u;
        h ^= h >> 13;
        float d = 0.012f + 0.006f * ((h >> 4) & 3) / 3.0f;
        mb_box(&bb[h & 3], (t3_vec3){ (xa + xb) * 0.5f, y + bh * 0.5f, -6 + d * 0.5f }, (t3_vec3){ (xb - xa) * 0.5f, bh * 0.5f, d * 0.5f }, 2);
      }
    }
  } else if (T->wall == WALL_PANELS) {
    /* concrete card panels in a grid */
    for (float y = 0.05f; y + 1.1f < top; y += 1.25f, row++)
      for (int col = 0; -14 + col * 1.6f < 1.6f; col++) {
        float xa = -14 + col * 1.6f + 0.05f, xb = fminf(xa + 1.5f, 1.6f);
        uint32_t h = (uint32_t)row * 73856093u ^ (uint32_t)col * 19349663u;
        h ^= h >> 13;
        mb_box(&bb[h & 3], (t3_vec3){ (xa + xb) * 0.5f, y + 0.55f, -5.99f }, (t3_vec3){ (xb - xa) * 0.5f, 0.55f, 0.01f }, 1);
      }
  } else {
    /* planks or bamboo canes: vertical, raised, a seam between */
    float w = T->wall == WALL_BAMBOO ? 0.12f : 0.3f;
    int col = 0;
    for (float x = -14; x < 1.6f; x += w + 0.02f, col++) {
      float xb = fminf(x + w, 1.6f);
      uint32_t h = (uint32_t)col * 19349663u;
      h ^= h >> 13;
      mb_box(&bb[h & 3], (t3_vec3){ (x + xb) * 0.5f, top * 0.5f, -5.985f }, (t3_vec3){ (xb - x) * 0.5f, top * 0.5f, 0.015f }, 1);
      if (T->wall == WALL_BAMBOO) /* the canes' nodes */
        for (float y = 0.6f + (col % 3) * 0.4f; y < top; y += 1.6f)
          mb_box(&bb[(h + 1) & 3], (t3_vec3){ (x + xb) * 0.5f, y, -5.97f }, (t3_vec3){ (xb - x) * 0.55f, 0.03f, 0.02f }, 1);
    }
  }
  for (int k = 0; k < 4; k++) {
    t3_material *mm = paper(PAPER_PLAIN, 1, 1, T->piece[k][0], T->piece[k][1], T->piece[k][2]);
    add_mesh(root, mb_finish(&bb[k]), mm, true, true);
    t3_release(mm);
  }
}

static void build_garden(t3_object *root, const level_def *L) {
  float H = L->height;
  const theme *T = T_ = &THEMES[L->theme];
  /* the floor */
  mb bm = { 0 };
  mb_quad(&bm, (t3_vec3){ -60, 0, 60 }, (t3_vec3){ 60, 0, 60 }, (t3_vec3){ 60, 0, -60 }, (t3_vec3){ -60, 0, -60 }, 0, 0, 40, 40);
  t3_material *mg = paper(T->ground, 1, 1, T->ground_tint[0], T->ground_tint[1], T->ground_tint[2]);
  add_mesh(root, mb_finish(&bm), mg, false, true);
  t3_release(mg);
  float top = H + 3.6f;
  build_wall(root, T, top);
  /* the backdrop: a curved sheet of painted paper all round, then cut-paper
   * hills (or city blocks) at three depths, each throwing a soft shadow on
   * the one behind */
  {
    mb bd = { 0 };
    float tops[49];
    for (int i = 0; i <= 48; i++) tops[i] = 90;
    mb_band_in(&bd, 58, 0, 6.2831853f, -1, tops, 48, 6, 1);
    t3_material *md = paper(PAPER_BACKDROP, 1, 1, 0.35f, 0.35f, 0.35f);
    md->roughness = 1;
    md->emissive = lin(T->glow[0], T->glow[1], T->glow[2]);
    md->emissive_intensity = 0.75f;
    W.backdrop_mat = md;
    W.backdrop_glow = md->emissive;
    t3_retain(md);
    add_mesh(root, mb_finish(&bd), md, false, false);
    t3_release(md);
    static const float HR[3] = { 50, 42, 34 }, HH[3] = { 16, 11, 7 };
    t3_material *msh = t3_mesh_basic_material_new(0x1e2418);
    msh->transparent = true;
    msh->opacity = 0.22f;
    msh->depth_write = false;
    t3_material *mwin = t3_mesh_basic_material_new(0xffd77a); /* lit windows at night */
    mb bwin = { 0 };
    for (int layer = 0; layer < 3; layer++) {
      enum { SEG = 96 };
      float hy[SEG + 1], sy[SEG + 1];
      for (int i = 0; i <= SEG; i++) {
        float a = 6.2831853f * i / SEG;
        if (T->skyline) { /* blocks: a stepped top, a few cells wide each */
          uint32_t hh = (uint32_t)(i / 3) * 2654435761u + (uint32_t)layer * 97u;
          hh ^= hh >> 15;
          hy[i] = HH[layer] * (0.6f + 0.9f * ((hh & 255) / 255.0f));
        } else hy[i] = HH[layer] * (0.55f + 0.25f * cw_sin(a * 3 + layer * 1.7f) + 0.2f * cw_sin(a * 7 + layer * 0.9f));
      }
      for (int i = 0; i <= SEG; i++) sy[i] = hy[(i + SEG - 1) % SEG] - 1.2f;
      hy[SEG] = hy[0];
      mb bh2 = { 0 }, bs2 = { 0 };
      mb_band_in(&bh2, HR[layer], 0, 6.2831853f, -1, hy, SEG, 12, 1);
      t3_material *mh2 = paper(T->skyline || L->theme == THEME_WINTER ? PAPER_PLAIN : PAPER_GRASS, 1, 1, T->hills[layer][0], T->hills[layer][1], T->hills[layer][2]);
      float haze = 0.45f - layer * 0.15f;
      mh2->emissive = lin(T->hills[layer][0] * haze, T->hills[layer][1] * haze, T->hills[layer][2] * haze);
      mh2->emissive_intensity = 1;
      W.hill_mat[layer] = mh2;
      W.hill_glow[layer] = mh2->emissive;
      t3_retain(mh2);
      add_mesh(root, mb_finish(&bh2), mh2, false, false);
      t3_release(mh2);
      if (layer > 0) {
        mb_band_in(&bs2, HR[layer - 1] - 0.3f, 0, 6.2831853f, -1, sy, SEG, 1, 1);
        t3_mesh *sh = add_mesh(root, mb_finish(&bs2), msh, false, false);
        T3_OBJ(sh)->render_order = 1;
      }
      if (T->lit_windows) /* window dots on the blocks, just in front of them */
        for (int i = 0; i < SEG; i++)
          for (float y = 1.5f; y < hy[i] - 1; y += 2.2f) {
            uint32_t hh = (uint32_t)i * 2246822519u ^ (uint32_t)(y * 10) * 3266489917u ^ (uint32_t)layer;
            hh ^= hh >> 13;
            if ((hh & 3) == 0) continue;
            float a = 6.2831853f * (i + 0.5f) / SEG, r = HR[layer] - 0.15f;
            t3_vec3 c = on_cyl(r, a, y), t = { cw_cos(a) * 0.35f, 0, -cw_sin(a) * 0.35f };
            mb_quad(&bwin, (t3_vec3){ c.x + t.x, y - 0.4f, c.z + t.z }, (t3_vec3){ c.x - t.x, y - 0.4f, c.z - t.z }, (t3_vec3){ c.x - t.x, y + 0.4f, c.z - t.z },
                    (t3_vec3){ c.x + t.x, y + 0.4f, c.z + t.z }, 0, 0, 1, 1);
          }
    }
    if (bwin.count) add_mesh(root, mb_finish(&bwin), mwin, false, false);
    else free(bwin.p), free(bwin.n), free(bwin.uv);
    t3_release(mwin);
    t3_release(msh);
  }
  /* windows on the wall: card frames; panes lit at night */
  t3_material *mf = paper(PAPER_WHITE, 1, 1, 1, 1, 1), *mp = paper(PAPER_SKYBLUE, 1, 1, 0.7f, 0.8f, 0.95f);
  if (T->lit_windows) mp->emissive = lin(1, 0.78f, 0.38f), mp->emissive_intensity = 1.2f;
  mb bf = { 0 }, bp = { 0 };
  for (float y = 3; y < H; y += (T->lit_windows ? 4.5f : 7))
    for (int s = -1; s <= 0; s++) {
      float x = s * 4.2f - 3.2f;
      mb_box(&bf, (t3_vec3){ x, y, -5.97f }, (t3_vec3){ 1.1f, 1.4f, 0.03f }, 1);
      mb_box(&bp, (t3_vec3){ x, y, -5.93f }, (t3_vec3){ 0.9f, 1.2f, 0.01f }, 1);
    }
  add_mesh(root, mb_finish(&bf), mf, true, true);
  add_mesh(root, mb_finish(&bp), mp, false, true);
  t3_release(mf), t3_release(mp);
  /* folded grass tufts (or snow drifts) round the foot */
  uint32_t h = 12345;
  if (T->tufts) {
    mb bt = { 0 };
    for (int i = 0; i < 160; i++) {
      h = h * 1664525u + 1013904223u;
      float a = (h >> 8) / 16777216.0f * 6.2831853f;
      h = h * 1664525u + 1013904223u;
      float r = 1.4f + (h >> 8) / 16777216.0f * 9;
      if (cw_cos(a) < -0.55f && r > 5) continue;
      float x = r * cw_sin(a), z = r * cw_cos(a), ht = 0.25f + 0.25f * ((h >> 4) & 15) / 15.0f;
      if (T->tufts == 2) ht *= 0.4f;
      for (int k = 0; k < 3; k++) {
        float d = k * 2.1f + a, wd = T->tufts == 2 ? 0.18f : 0.06f;
        float dx = cw_cos(d) * wd, dz = cw_sin(d) * wd, tx = cw_cos(d + 1.3f) * 0.08f, tz = cw_sin(d + 1.3f) * 0.08f;
        mb_tri(&bt, (t3_vec3){ x - dx, 0, z - dz }, (t3_vec3){ x + dx, 0, z + dz }, (t3_vec3){ x + tx, ht, z + tz }, 0, 0, 0.3f, 0, 0.15f, 1);
        mb_tri(&bt, (t3_vec3){ x + dx, 0, z + dz }, (t3_vec3){ x - dx, 0, z - dz }, (t3_vec3){ x + tx, ht, z + tz }, 0, 0, 0.3f, 0, 0.15f, 1);
      }
    }
    t3_material *mt = T->tufts == 2 ? paper(PAPER_WHITE, 1, 1, 0.95f, 0.97f, 1) : paper(PAPER_GRASS, 1, 1, T->ground_tint[0] * 0.8f, T->ground_tint[1] * 0.95f, T->ground_tint[2] * 0.7f);
    add_mesh(root, mb_finish(&bt), mt, true, true);
    t3_release(mt);
  }
  if (T->flowers) {
    static const float FC[4][3] = { { 1, 0.55f, 0.6f }, { 1, 0.85f, 0.35f }, { 0.7f, 0.6f, 1 }, { 1, 1, 1 } };
    for (int c = 0; c < 4; c++) {
      mb bfl = { 0 };
      for (int i = 0; i < 9; i++) {
        h = h * 1664525u + 1013904223u;
        float a = (h >> 8) / 16777216.0f * 6.2831853f;
        h = h * 1664525u + 1013904223u;
        float r = 1.8f + (h >> 8) / 16777216.0f * 6;
        if (cw_cos(a) < -0.4f) continue;
        float x = r * cw_sin(a), z = r * cw_cos(a), y = 0.35f + 0.2f * ((h >> 4) & 7) / 7.0f;
        for (int p = 0; p < 6; p++) {
          float pa = p * 1.0471976f, pb = pa + 1.0471976f;
          t3_vec3 c0 = { x, y, z }, c1 = { x + 0.12f * cw_cos(pa), y + 0.12f * cw_sin(pa), z }, c2 = { x + 0.12f * cw_cos(pb), y + 0.12f * cw_sin(pb), z };
          mb_tri(&bfl, c0, c1, c2, 0.5f, 0.5f, 1, 0, 0, 1);
          mb_tri(&bfl, c0, c2, c1, 0.5f, 0.5f, 0, 1, 1, 0);
        }
        mb_box(&bfl, (t3_vec3){ x, y * 0.5f, z - 0.01f }, (t3_vec3){ 0.012f, y * 0.5f, 0.012f }, 1);
      }
      t3_material *mfl = paper(PAPER_WHITE, 1, 1, FC[c][0], FC[c][1], FC[c][2]);
      add_mesh(root, mb_finish(&bfl), mfl, true, true);
      t3_release(mfl);
    }
  }
  /* houses and trees (pines, palms) cut out on sticks, three depths */
  t3_material *mh = paper(PAPER_WHITE, 1, 1, T->house[0], T->house[1], T->house[2]),
              *mt2 = paper(PAPER_GRASS, 1, 1, T->crown[0], T->crown[1], T->crown[2]), *mk = paper(PAPER_KRAFT, 1, 1, 0.8f, 0.7f, 0.55f);
  for (int i = 0; i < 18; i++) {
    float a = i < 9 ? -1.2f + i * 0.3f : 0.9f + (i - 9) * 0.22f, r = 12 + (i % 3) * 5;
    float x = r * cw_sin(a), z = r * cw_cos(a), sc = 1 + (i % 3) * 0.6f;
    t3_object *g = t3_group_new();
    if (i % 2 == 0 && !T->palms) {
      float house[] = { -1.2f, 0, 1.2f, 0, 1.2f, 1.6f, 0, 2.6f, -1.2f, 1.6f };
      cutout_add(g, house, 5, 0.02f, mh, true);
    } else if (T->pines) {
      float pine[] = { 0, 3.2f, -0.9f, 1.8f, -0.45f, 1.8f, -1.2f, 0.7f, 1.2f, 0.7f, 0.45f, 1.8f, 0.9f, 1.8f };
      cutout_add(g, pine, 7, 0.02f, mt2, true);
    } else if (T->palms) {
      float frond[16];
      for (int k = 0; k < 8; k++) {
        float aa = k * 0.785398f, rr = (k & 1) ? 0.35f : 1.5f;
        frond[k * 2] = rr * cw_cos(aa), frond[k * 2 + 1] = 2.6f + rr * cw_sin(aa) * 0.6f;
      }
      cutout_add(g, frond, 8, 0.02f, mt2, true);
    } else {
      t3_mesh *crown = blob_add(g, 1.1f, 1.3f, 14, (uint32_t)i, 0.25f, 0.02f, mt2, true);
      t3_object_set_position(crown, 0, 2.1f, 0);
    }
    mb bstk = { 0 };
    mb_box(&bstk, (t3_vec3){ 0, T->palms ? 1.3f : 0.5f, -0.05f }, (t3_vec3){ 0.05f, T->palms ? 1.3f : 0.5f, 0.02f }, 1);
    add_mesh(g, mb_finish(&bstk), mk, true, true);
    t3_object_set_position(g, x, 0, z);
    t3_object_set_rotation(g, 0, a + 3.1415927f, 0);
    t3_object_set_scale(g, sc, sc, sc);
    t3_object_add(root, g);
    t3_release(g);
  }
  t3_release(mh), t3_release(mt2), t3_release(mk);
  /* clouds: paper cutouts high in the sky, drifting */
  W.cloud_mat = paper(PAPER_WHITE, 1, 1, 1, 1, 1);
  W.cloud_mat->emissive = lin(0.55f, 0.55f, 0.55f); /* white card against the lit backdrop */
  W.cloud_mat->emissive_intensity = 1;
  for (int i = 0; i < 6; i++) {
    t3_mesh *c = blob_add(root, 2.6f + (i % 3) * 0.8f, 1.1f, 18, (uint32_t)(i + 40), 0.45f, 0.05f, W.cloud_mat, false);
    W.clouds[i] = T3_OBJ(c);
    W.cloud_x[i] = -18 + i * 7.5f;
  }
  /* the sun: a card disc with folded rays */
  t3_material *msun = t3_mesh_standard_material_new(0xffffff);
  paper_apply(msun, PAPER_WHITE, 1, 1);
  bool moon = L->theme == THEME_CITY;
  msun->color = moon ? lin(0.95f, 0.95f, 0.85f) : lin(1, 0.82f, 0.35f);
  msun->emissive = moon ? lin(0.8f, 0.82f, 0.7f) : lin(1, 0.7f, 0.25f);
  msun->emissive_intensity = 0.9f;
  W.sun = t3_group_new();
  blob_add(W.sun, 2.2f, 2.2f, 24, 3, 0.0f, 0.06f, msun, false);
  mb brays = { 0 };
  for (int i = 0; i < 12; i++) {
    float a = i * 0.5235988f, a0 = a - 0.12f, a1 = a + 0.12f;
    t3_vec3 r0 = { 2.4f * cw_cos(a0), 2.4f * cw_sin(a0), 0 }, r1 = { 3.6f * cw_cos(a), 3.6f * cw_sin(a), 0.15f }, r2 = { 2.4f * cw_cos(a1), 2.4f * cw_sin(a1), 0 };
    mb_tri(&brays, r0, r1, r2, 0, 0, 0.5f, 1, 1, 0);
    mb_tri(&brays, r0, r2, r1, 0, 0, 1, 0, 0.5f, 1);
  }
  t3_mesh *rays = add_mesh(W.sun, mb_finish(&brays), msun, false, false);
  W.sun_rays = T3_OBJ(rays);
  W.sun_rays->visible = !moon;
  t3_object_add(root, W.sun);
  t3_release(W.sun);
  t3_release(msun);
}

static void build_weather(t3_object *root, const level_def *L) {
  float H = L->height;
  /* the streams: blue paper ribbons down the pipe, a scrolling map */
  W.stream_mat = t3_mesh_standard_material_new(0xffffff);
  paper_apply(W.stream_mat, PAPER_SKYBLUE, 1, H / 2);
  W.stream_mat->color = lin(0.55f, 0.75f, 1);
  W.stream_mat->transparent = true;
  W.stream_mat->opacity = 0;
  W.stream_mat->roughness = 0.85f;
  W.stream_mat->depth_write = false;
  for (int k = 0; k < L->n_streams; k++) {
    mb b = { 0 };
    mb_band(&b, PIPE_R + 0.03f, -0.36f, 0.72f, 0, H + 0.2f, 4, 1, 1);
    W.streams[k] = add_mesh(root, mb_finish(&b), W.stream_mat, false, false);
  }
  /* wave fronts: white-blue bands that run down a stream */
  W.wave_mat = t3_mesh_standard_material_new(0xffffff);
  paper_apply(W.wave_mat, PAPER_WHITE, 1, 1);
  W.wave_mat->color = lin(0.85f, 0.93f, 1);
  W.wave_mat->transparent = true;
  W.wave_mat->opacity = 0.9f;
  W.wave_mat->roughness = 0.75f;
  W.wave_mat->depth_write = false;
  for (int i = 0; i < 8; i++) {
    mb b = { 0 };
    mb_band(&b, PIPE_R + 0.06f, -0.42f, 0.84f, -0.35f, 0.35f, 5, 1, 1);
    W.waves[i] = add_mesh(root, mb_finish(&b), W.wave_mat, false, false);
    T3_OBJ(W.waves[i])->visible = false;
  }
  /* leaves: folded paper leaves */
  float leafxy[] = { 0, -0.16f, 0.07f, -0.05f, 0.08f, 0.06f, 0, 0.16f, -0.08f, 0.06f, -0.07f, -0.05f };
  t3_material *ml = paper(PAPER_KRAFT, 1, 1, 1, 0.6f, 0.3f);
  ml->emissive = lin(0.35f, 0.15f, 0.04f); /* thin paper: light comes through */
  ml->emissive_intensity = 0.5f;
  for (int i = 0; i < MAX_LEAVES; i++) {
    W.leaves[i] = cutout_add(root, leafxy, 6, 0.006f, ml, true);
    T3_OBJ(W.leaves[i])->visible = false;
  }
  t3_release(ml);
  /* dewdrops: little glossy beads */
  t3_material *md = t3_mesh_physical_material_new(0xffffff);
  md->color = lin(0.75f, 0.92f, 1);
  md->roughness = 0.08f;
  md->clearcoat = 1;
  md->clearcoat_roughness = 0.05f;
  md->emissive = lin(0.25f, 0.45f, 0.6f);
  md->emissive_intensity = 0.6f;
  t3_geometry *gd = t3_icosahedron_geometry_new(0.07f, 1);
  for (int i = 0; i < L->n_dews; i++) {
    W.dews[i] = add_mesh(root, t3_retain(gd), md, true, false);
    t3_vec3 p = on_cyl(PIPE_R + 0.07f, L->dew_th[i], L->dew_y[i]);
    t3_object_set_position(W.dews[i], p.x, p.y, p.z);
  }
  t3_release(gd);
  t3_release(md);
  /* rain: cellophane streaks round the camera */
  W.rain_mat = t3_mesh_basic_material_new(L->theme == THEME_WINTER ? 0xffffff : 0xbfe0ff); /* winter: snow */
  W.rain_mat->transparent = true;
  W.rain_mat->opacity = 0;
  W.rain_mat->depth_write = false;
  /* one dynamic mesh of camera-facing quads, rewritten each frame */
  t3_geometry *rg = t3_geometry_new();
  float *zero = calloc(RAIN_N * 6 * 3, sizeof(float));
  t3_attribute *rp = t3_attribute_new(T3_FLOAT32, zero, RAIN_N * 6, 3);
  rp->usage = T3_DYNAMIC_DRAW;
  t3_geometry_set_attribute(rg, T3_ATTR_POSITION, rp);
  t3_release(rp);
  free(zero);
  W.rain = t3_mesh_new(rg, W.rain_mat);
  t3_release(rg);
  T3_OBJ(W.rain)->frustum_culled = false;
  t3_object_add(root, W.rain);
  t3_release(W.rain);
}


/* ── stage 3 props: the silk line, the paper bird, flies, spools ───── */

static void build_critters(t3_object *root) {
  /* the silk: one camera-facing quad, rewritten each frame */
  t3_material *msilk = t3_mesh_basic_material_new(0xf4f4ff);
  msilk->transparent = true;
  msilk->opacity = 0.85f;
  msilk->depth_write = false;
  t3_geometry *sg = t3_geometry_new();
  float zero[18] = { 0 };
  t3_attribute *sp = t3_attribute_new(T3_FLOAT32, zero, 6, 3);
  sp->usage = T3_DYNAMIC_DRAW;
  t3_geometry_set_attribute(sg, T3_ATTR_POSITION, sp);
  t3_release(sp);
  W.silk = t3_mesh_new(sg, msilk);
  t3_release(sg);
  t3_release(msilk);
  T3_OBJ(W.silk)->frustum_culled = false;
  T3_OBJ(W.silk)->visible = false;
  t3_object_add(root, W.silk);
  t3_release(W.silk);
  /* the bird: a folded-card body, two wings that flap, a beak */
  t3_material *mbird = paper(PAPER_SKYBLUE, 1, 1, 0.55f, 0.7f, 0.95f), *mbeak = paper(PAPER_KRAFT, 1, 1, 1, 0.75f, 0.3f);
  W.bird = t3_group_new();
  t3_geometry *gb = faceted_ico(0.16f);
  t3_mesh *body = add_mesh(W.bird, gb, mbird, true, true);
  t3_object_set_scale(body, 1, 0.8f, 1.4f);
  float beak[] = { -0.04f, 0, 0.04f, 0, 0, 0.12f };
  t3_mesh *bk = cutout_add(W.bird, beak, 3, 0.01f, mbeak, true);
  t3_object_set_position(bk, 0, 0.02f, 0.2f);
  t3_object_set_rotation(bk, 1.5707963f, 0, 0);
  float wing[] = { 0, 0, 0.32f, -0.06f, 0.28f, 0.08f, 0.05f, 0.1f };
  for (int k = 0; k < 2; k++) {
    t3_object *wpiv = t3_group_new();
    t3_mesh *wm = cutout_add(wpiv, wing, 4, 0.01f, mbird, true);
    t3_object_set_rotation(wm, -1.5707963f, 0, 0);
    if (k) t3_object_set_scale(wpiv, -1, 1, 1);
    t3_object_set_position(wpiv, k ? -0.08f : 0.08f, 0.06f, 0);
    t3_object_add(W.bird, wpiv);
    W.bird_wing[k] = wpiv;
    t3_release(wpiv);
  }
  t3_material *meye = t3_mesh_standard_material_new(0x111111);
  t3_geometry *geye = t3_circle_geometry_new(0.025f, 10);
  for (int k = 0; k < 2; k++) {
    t3_mesh *e = add_mesh(W.bird, t3_retain(geye), meye, false, false);
    t3_object_set_position(e, k ? -0.09f : 0.09f, 0.06f, 0.14f);
    t3_object_set_rotation(e, 0, k ? -0.9f : 0.9f, 0);
  }
  t3_release(meye), t3_release(geye);
  T3_OBJ(W.bird)->visible = false;
  t3_object_add(root, W.bird);
  t3_release(W.bird);
  t3_release(mbird), t3_release(mbeak);
  /* the mark: a red paper ring where it will peck */
  t3_material *mmark = t3_mesh_basic_material_new(0xe2493a);
  mmark->transparent = true;
  mmark->opacity = 0.85f;
  mmark->depth_write = false;
  W.mark = T3_OBJ(add_mesh(root, t3_ring_geometry_new(0.2f, 0.3f, 24, 1), mmark, false, false));
  W.mark->visible = false;
  t3_release(mmark);
  /* flies: a dark bead, two white paper wings */
  t3_material *mfly = paper(PAPER_DARK, 1, 1, 0.3f, 0.32f, 0.3f), *mwing = paper(PAPER_WHITE, 1, 1, 1, 1, 1);
  mwing->emissive = lin(0.3f, 0.3f, 0.32f);
  float fw[] = { 0, 0, 0.09f, 0.02f, 0.08f, 0.06f, 0.01f, 0.04f };
  t3_geometry *gfly = t3_icosahedron_geometry_new(0.035f, 0);
  for (int i = 0; i < MAX_FLIES; i++) {
    t3_object *f = t3_group_new();
    add_mesh(f, t3_retain(gfly), mfly, true, false);
    for (int k = 0; k < 2; k++) {
      t3_object *wp = t3_group_new();
      cutout_add(wp, fw, 4, 0.004f, mwing, false);
      if (k) t3_object_set_scale(wp, -1, 1, 1);
      t3_object_add(f, wp);
      W.fly_wing[i][k] = wp;
      t3_release(wp);
    }
    W.flies[i] = f;
    t3_object_add(root, f);
    t3_release(f);
  }
  t3_release(mfly), t3_release(mwing), t3_release(gfly);
  /* spools of silk: white card drums with kraft ends */
  t3_material *mspool = paper(PAPER_WHITE, 1, 1, 1, 1, 1), *mend = paper(PAPER_KRAFT, 1, 1, 1, 1, 1);
  mspool->emissive = lin(0.25f, 0.25f, 0.3f);
  for (int i = 0; i < MAX_SPOOLS; i++) {
    t3_object *g = t3_group_new();
    mb b = { 0 };
    mb_band(&b, 0.07f, 0, 6.2831853f, -0.06f, 0.06f, 10, 1, 1);
    add_mesh(g, mb_finish(&b), mspool, true, true);
    mb e = { 0 };
    mb_box(&e, (t3_vec3){ 0, 0.07f, 0 }, (t3_vec3){ 0.1f, 0.012f, 0.1f }, 1);
    mb_box(&e, (t3_vec3){ 0, -0.07f, 0 }, (t3_vec3){ 0.1f, 0.012f, 0.1f }, 1);
    add_mesh(g, mb_finish(&e), mend, true, true);
    W.spools[i] = g;
    t3_object_add(root, g);
    t3_release(g);
  }
  t3_release(mspool), t3_release(mend);
}

/* ── the spider ────────────────────────────────────────────────────── */

static void build_spider(t3_object *root) {
  W.spider = t3_group_new();
  W.body = t3_group_new();
  t3_object_add(W.spider, W.body);
  t3_material *card = paper(PAPER_DARK, 1, 1, 1, 1, 1);
  t3_material *belly = paper(PAPER_WHITE, 1, 1, 0.85f, 0.45f, 0.55f);
  /* folded card body: a faceted abdomen, a smaller head-thorax, a pink stripe */
  t3_geometry *ga = faceted_ico(0.12f);
  t3_mesh *ab = add_mesh(W.body, ga, card, true, false);
  t3_object_set_position(ab, 0, -0.1f, 0.07f);
  t3_object_set_scale(ab, 0.9f, 1.15f, 0.75f);
  W.abdomen = T3_OBJ(ab);
  t3_geometry *gs = faceted_ico(0.05f);
  t3_mesh *stripe = add_mesh(W.body, gs, belly, false, false);
  t3_object_set_position(stripe, 0, -0.1f, 0.15f);
  t3_object_set_scale(stripe, 1.2f, 1.6f, 0.4f);
  {
    mb bt = { 0 };
    t3_vec3 p0 = { -0.03f, -0.19f, 0.09f }, p1 = { 0.03f, -0.19f, 0.09f }, p2 = { 0.02f, -0.24f, 0.07f }, p3 = { -0.02f, -0.24f, 0.07f };
    mb_quad(&bt, p0, p1, p2, p3, 0, 0, 1, 1);
    mb_quad(&bt, p1, p0, p3, p2, 0, 0, 1, 1);
    add_mesh(W.body, mb_finish(&bt), edge_mat, true, true);
  }
  t3_geometry *gh = faceted_ico(0.075f);
  t3_mesh *head = add_mesh(W.body, gh, card, true, false);
  t3_object_set_position(head, 0, 0.06f, 0.06f);
  /* googly eyes: white discs, black pupils that hang toward the ground */
  t3_material *white = t3_mesh_standard_material_new(0xffffff);
  white->roughness = 0.4f;
  t3_material *black = t3_mesh_standard_material_new(0x111111);
  black->roughness = 0.3f;
  t3_geometry *gwhite = t3_circle_geometry_new(0.032f, 16), *gpupil = t3_circle_geometry_new(0.015f, 12);
  for (int e = 0; e < 2; e++) {
    t3_object *eye = t3_group_new();
    add_mesh(eye, t3_retain(gwhite), white, false, false);
    t3_mesh *p = add_mesh(eye, t3_retain(gpupil), black, false, false);
    t3_object_set_position(p, 0, 0, 0.003f);
    W.pupils[e] = T3_OBJ(p);
    W.eyes[e] = eye;
    t3_object_add(W.body, eye);
    t3_release(eye);
  }
  t3_release(gwhite), t3_release(gpupil);
  /* eight legs: hip and knee pivots, paper-strip segments */
  /* a leg segment: a strip of card folded lengthwise into a shallow V (both
   * sides of the card, the crease along the middle) */
  mb bl = { 0 };
  {
    t3_vec3 a0 = { -0.011f, 0, 0 }, a1 = { 0, 0, 0.005f }, a2 = { 0.011f, 0, 0 };
    t3_vec3 b0 = { -0.011f, 0.16f, 0 }, b1 = { 0, 0.16f, 0.005f }, b2 = { 0.011f, 0.16f, 0 };
    mb_quad(&bl, a0, a1, b1, b0, 0, 0, 0.5f, 1);
    mb_quad(&bl, a1, a2, b2, b1, 0.5f, 0, 1, 1);
    mb_quad(&bl, a1, a0, b0, b1, 0, 0, 0.5f, 1);
    mb_quad(&bl, a2, a1, b1, b2, 0.5f, 0, 1, 1);
  }
  t3_geometry *seg = mb_finish(&bl);
  for (int i = 0; i < 8; i++) {
    leg *l = &W.legs[i];
    l->side = i < 4 ? -1 : 1;
    l->base = 0.6f + (i % 4) * 0.62f; /* the foot's bearing from the heading: front pair forward, back pair back */
    l->hip = t3_group_new();
    t3_mesh *up = add_mesh(l->hip, t3_retain(seg), card, true, true);
    t3_object_set_position(up, 0, 0, 0);
    l->knee = t3_group_new();
    t3_object_set_position(l->knee, 0, LEG_SEG, 0);
    t3_mesh *lo = add_mesh(l->knee, t3_retain(seg), card, true, true);
    t3_object_set_position(lo, 0, 0, 0);
    t3_object_add(l->hip, l->knee);
    t3_object_add(W.body, l->hip);
    t3_release(l->knee);
    t3_release(l->hip);
  }
  t3_release(seg);
  t3_release(card), t3_release(belly), t3_release(white), t3_release(black);
  t3_object_add(root, W.spider);
  t3_release(W.spider);
}

void world_build(cw_node root, const level_def *L, int title) {
  world_free();
  W.L = L;
  W.title = title != 0;
  W.node = cw_node_new("Node3D", "World");
  cw_node_add_child(root, W.node);
  W.root = t3_group_new();
  edge_mat = paper(PAPER_WHITE, 1, 1, 1, 1, 0.97f);
  build_pipe(W.root, L);
  build_garden(W.root, L);
  build_weather(W.root, L);
  build_spider(W.root);
  build_critters(W.root);
  cw_node3d_set_object(W.node, W.root);
  t3_release(W.root);
  t3_release(edge_mat);
  edge_mat = NULL;
  built_free();
  W.cam_live = false;
  W.last_t = -1;
}

void world_free(void) {
  if (cw_node_valid(W.node)) cw_node_free(W.node);
  for (int i = 0; i < MAX_PATCHES * 2; i++)
    if (W.flap_mat[i]) t3_release(W.flap_mat[i]);
  if (!CW_HANDLE_IS_NULL(W.air)) cw_world3d_destroy(W.air); /* its cloths with it */
  if (W.stream_mat) t3_release(W.stream_mat);
  if (W.wave_mat) t3_release(W.wave_mat);
  if (W.rain_mat) t3_release(W.rain_mat);
  if (W.cloud_mat) t3_release(W.cloud_mat);
  if (W.backdrop_mat) t3_release(W.backdrop_mat);
  for (int i = 0; i < 3; i++)
    if (W.hill_mat[i]) t3_release(W.hill_mat[i]);
  memset(&W, 0, sizeof W);
}

/* ── per frame ─────────────────────────────────────────────────────── */

static float lerpa(float a, float b, float t) { return a + wrap_pi(b - a) * t; }

t3_vec3 world_spider_pos(const sim *prev, const sim *cur, float alpha, float *th_out) {
  float th = lerpa(prev->s.th, cur->s.th, alpha), y = prev->s.y + (cur->s.y - prev->s.y) * alpha;
  float r = prev->s.r + (cur->s.r - prev->s.r) * alpha;
  if (th_out) *th_out = th;
  return on_cyl(PIPE_R + 0.06f + r, th, y);
}

static t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 v3add(t3_vec3 a, t3_vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 v3sub(t3_vec3 a, t3_vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 v3mul(t3_vec3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static float v3dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static t3_vec3 v3cross(t3_vec3 a, t3_vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static t3_vec3 v3norm(t3_vec3 a) {
  float l = sqrtf(v3dot(a, a));
  return l > 1e-6f ? v3mul(a, 1 / l) : a;
}
/* an orientation from its x, y and z axes (orthonormal) */
static t3_quat quat_axes(t3_vec3 x, t3_vec3 y, t3_vec3 z) {
  t3_mat4 m = { 0 };
  m.e[0] = x.x, m.e[1] = x.y, m.e[2] = x.z;
  m.e[4] = y.x, m.e[5] = y.y, m.e[6] = y.z;
  m.e[8] = z.x, m.e[9] = z.y, m.e[10] = z.z;
  m.e[15] = 1;
  return t3_quat_from_rotation_matrix(&m);
}

/* two-bone IK for one leg in the body's frame: the hip at h reaches for the
 * foot at f, the knee rising toward up (the spider's back), both segments
 * LEG_SEG long; out of reach, the leg points straight at the target */
static void leg_ik(leg *l, t3_vec3 h, t3_vec3 f, t3_vec3 up) {
  t3_vec3 d = v3sub(f, h);
  float dl = sqrtf(v3dot(d, d));
  t3_vec3 dn = dl > 1e-5f ? v3mul(d, 1 / dl) : v3(0, 0, -1);
  dl = cw_clampf(dl, 0.04f, 2 * LEG_SEG * 0.985f);
  t3_vec3 pv = v3sub(up, v3mul(dn, v3dot(up, dn)));
  if (v3dot(pv, pv) < 1e-4f) pv = v3(l->side, 0, 0);
  pv = v3norm(pv);
  float hh = sqrtf(LEG_SEG * LEG_SEG - dl * dl * 0.25f);
  t3_vec3 k = v3add(h, v3add(v3mul(dn, dl * 0.5f), v3mul(pv, hh)));
  t3_vec3 s1 = v3norm(v3sub(k, h)), s2 = v3norm(v3sub(v3add(h, v3mul(dn, dl)), k));
  t3_vec3 pn = v3norm(v3cross(dn, pv)); /* the leg's plane: the strip lies across it */
  t3_vec3 z1 = v3cross(pn, s1);
  t3_object_set_position(l->hip, h.x, h.y, h.z);
  t3_object_set_quaternion(l->hip, quat_axes(pn, s1, z1));
  /* the knee turns about the hip's x axis (the plane's normal) */
  t3_object_set_rotation(l->knee, cw_atan2(v3dot(s2, z1), v3dot(s2, s1)), 0, 0);
}

static void place_spider(const sim *prev, const sim *cur, float alpha, float t, float dt, bool stop) {
  float th;
  t3_vec3 p = world_spider_pos(prev, cur, alpha, &th);
  const spider *s = &cur->s;
  float face = lerpa(prev->s.face, cur->s.face, alpha);
  bool air = s->state == SP_JUMP, washed = s->state == SP_WASHED, zip = s->state == SP_ZIP;
  if (washed) face += cw_sin(t * 30) * 0.6f; /* tumbling down */
  /* washed down it turns out, belly and eyes off the pipe, legs flailing;
   * otherwise it clings belly to the pipe. The turn takes 0.2 s each way. */
  float ft = washed ? 1.f : 0.f, fstep = dt / 0.2f;
  W.flip += cw_clampf(ft - W.flip, -fstep, fstep);
  float fl = W.flip * W.flip * (3 - 2 * W.flip);
  /* the surface frame: n out of the pipe, u up, r round */
  float sn, cs;
  cw_sincos(th, &sn, &cs);
  t3_vec3 n = { sn, 0, cs }, u = { 0, 1, 0 }, rr = { cs, 0, -sn };
  float fs, fc;
  cw_sincos(face, &fs, &fc);
  t3_vec3 hd = v3add(v3mul(u, fc), v3mul(rr, fs));
  t3_vec3 rt = v3sub(v3mul(rr, fc), v3mul(u, fs));
  /* the flip rolls the body half a turn about its heading */
  float rs, rc;
  cw_sincos(fl * 3.1415927f, &rs, &rc);
  t3_vec3 rt2 = v3add(v3mul(rt, rc), v3mul(n, rs)), n2 = v3sub(v3mul(n, rc), v3mul(rt, rs));
  t3_object_set_quaternion(W.spider, quat_axes(rt2, hd, n2));
  t3_vec3 po = v3add(p, v3mul(n, 0.15f * fl)); /* turned out, its back would sink into the pipe */
  t3_object_set_position(W.spider, po.x, po.y, po.z);
  float bz = s->brace ? -0.03f : air ? 0.02f : 0;
  t3_object_set_position(W.body, 0, 0, bz);
  /* legs: feet planted on the pipe round the body, stepping on the distance
   * walked; in the air they reach for the pipe; turned out they flail */
  float walk = prev->s.walk + (cur->s.walk - prev->s.walk) * alpha;
  float tt = t;
  if (stop) {
    walk = floorf(walk * 40) / 40; /* steps in twelves-ish: stop-motion */
    tt = floorf(t * 12) / 12;
  }
  for (int i = 0; i < 8; i++) {
    leg *l = &W.legs[i];
    float ph = walk * 22 + (i % 2 ? 3.1415927f : 0) + (i >= 4 ? 3.1415927f : 0);
    if (zip) ph = tt * 16 + (i % 2 ? 3.1415927f : 0) + (i >= 4 ? 3.1415927f : 0); /* scrabbling up the silk */
    float swing = cw_sin(ph), lift = cw_clampf(cw_cos(ph), 0, 1);
    if (air) swing = 0, lift = 0;
    if (s->brace) swing = 0, lift = 0;
    int k = i % 4;
    t3_vec3 hip = v3(l->side * 0.05f, -k * 0.025f, 0.06f); /* in the spider's frame */
    /* the grip: a foot on the pipe, bearing l->base from the heading */
    float bs, bc;
    cw_sincos(l->base, &bs, &bc);
    float reach = (s->brace ? 0.2f : 0.24f) - (air ? 0.06f : 0);
    float a = l->side * bs * reach, b = hip.y + bc * reach + swing * 0.055f;
    t3_vec3 w = v3add(v3mul(rt, a), v3mul(hd, b)); /* along the surface */
    float fth = th + v3dot(w, rr) / PIPE_R;
    t3_vec3 fw = on_cyl(PIPE_R + 0.004f + lift * 0.05f, fth, p.y + w.y);
    t3_vec3 dv = v3sub(fw, po);
    t3_vec3 grip = v3(v3dot(dv, rt2), v3dot(dv, hd), v3dot(dv, n2));
    /* the flail: feet waving off the belly, out from the pipe */
    float wv = cw_sin(tt * 25 + i * 1.7f);
    t3_vec3 flail = v3(l->side * (0.12f + bs * 0.1f), hip.y + bc * 0.18f + wv * 0.05f, -0.16f + cw_cos(tt * 21 + i) * 0.05f);
    t3_vec3 f = v3add(v3mul(grip, 1 - fl), v3mul(flail, fl));
    hip.z -= bz, f.z -= bz; /* into the body's frame */
    leg_ik(l, hip, f, v3(l->side * 0.35f, 0, 1));
  }
  float breathe = 1 + 0.03f * cw_sin(tt * 4);
  t3_object_set_scale(W.abdomen, 0.9f * breathe, 1.15f * breathe, 0.75f);
  /* googly eyes on the top-front of the head, looking up the pipe: from the
   * camera only an edge shows while it clings; turned out they face it */
  t3_vec3 ed = v3norm(v3add(v3mul(v3(0, 0.95f, 0.3f), 1 - fl), v3mul(v3(0, 0.35f, -0.94f), fl)));
  t3_vec3 ey = v3cross(ed, v3(1, 0, 0));
  /* googly pupils hang toward world down: world down in the eye's frame */
  t3_vec3 dl = v3(-u.y * rt2.y, -u.y * hd.y, -u.y * n2.y);
  float down_x = dl.x, down_y = v3dot(dl, ey);
  for (int e = 0; e < 2; e++) {
    t3_object_set_position(W.eyes[e], e ? 0.036f : -0.036f, 0.06f + ed.y * 0.07f, 0.06f + ed.z * 0.07f);
    t3_object_set_quaternion(W.eyes[e], quat_axes(v3(1, 0, 0), ey, ed));
    float tx = down_x * 0.012f, ty = down_y * 0.012f;
    float *o = W.pupil_off[e], *v = W.pupil_vel[e];
    for (int kk = 0; kk < 2; kk++) {
      float target = kk ? ty : tx;
      v[kk] += (target - o[kk]) * 0.35f;
      v[kk] *= 0.78f;
      o[kk] += v[kk];
    }
    t3_object_set_position(W.pupils[e], o[0], o[1], 0.003f);
  }
}

void world_sync(const sim *prev, const sim *cur, float alpha, float t, bool stop) {
  if (!W.root) return;
  const level_def *L = W.L;
  float dt = W.last_t < 0 ? 1 / 60.0f : cw_clampf(t - W.last_t, 0, 0.1f);
  W.last_t = t;
  place_spider(prev, cur, alpha, t, dt, stop);
  /* weather amounts eased per frame */
  float rain_target = cur->weather == WX_RAIN ? 1 : cur->weather == WX_WARN ? 0.15f : 0;
  float sun_target = cur->weather == WX_SUN ? 1 : cur->weather == WX_CLEAR ? 0.6f : 0;
  float k = 1 - cw_exp(-2.5f * dt);
  W.rain_amount += (rain_target - W.rain_amount) * k;
  W.sun_amount += (sun_target - W.sun_amount) * k;
  /* paper wraps darken and gloss with the wet */
  float wet = cur->wet;
  for (int i = 0; i < L->n_patches; i++) {
    t3_material *m = W.patch_mat[i];
    bool moss = !L->patches[i].dries;
    float w = moss ? 1 : wet;
    t3_color dry = moss ? lin(0.55f, 0.75f, 0.45f) : lin(0.98f, 0.95f, 0.86f), soak = moss ? lin(0.3f, 0.5f, 0.28f) : lin(0.55f, 0.6f, 0.68f);
    m->color = (t3_color){ dry.r + (soak.r - dry.r) * w, dry.g + (soak.g - dry.g) * w, dry.b + (soak.b - dry.b) * w };
    m->roughness = 1 - 0.7f * w;
    m->clearcoat = w * 0.8f;
    for (int e = 0; e < 2; e++) {
      t3_material *fm = W.flap_mat[i * 2 + e];
      if (!fm) continue;
      fm->color = m->color;
      fm->roughness = 1 - 0.4f * w; /* a loose sheet sheds the water: damp, never glazed */
    }
  }
  /* the loose ends: fixed 60 Hz steps; in stop-motion the sheets are posed
   * on twelves (every fifth step), as the spider's legs are */
  if (!CW_HANDLE_IS_NULL(W.air)) {
    W.flap_acc += dt;
    for (int n = 0; W.flap_acc >= 1 / 60.0f - 1e-5f && n < 4; n++) {
      W.flap_acc -= 1 / 60.0f;
      flaps_step(W.rain_amount, wet);
    }
    if (W.flap_acc > 1 / 60.0f) W.flap_acc = 0;
    bool pose = !stop || W.flap_steps / 5 != W.flap_drawn;
    if (pose) {
      W.flap_drawn = W.flap_steps / 5;
      for (int i = 0; i < MAX_PATCHES * 2; i++)
        if (W.flap_geo[i]) cw_cloth_update_geometry(W.flap[i], W.flap_geo[i], stop ? 1 : W.flap_acc * 60);
    }
  }
  /* streams, scrolling down */
  W.stream_mat->opacity = 0.55f * W.rain_amount;
  for (int kk = 0; kk < L->n_streams; kk++) {
    t3_object_set_rotation(W.streams[kk], 0, cur->stream_th[kk], 0);
    T3_OBJ(W.streams[kk])->visible = W.rain_amount > 0.02f;
  }
  t3_texture *smap = W.stream_mat->map;
  if (smap) smap->offset.y = t * 0.9f;
  for (int i = 0; i < 8; i++) {
    const wave *w = &cur->waves[i];
    T3_OBJ(W.waves[i])->visible = w->live;
    if (!w->live) continue;
    float y = w->y;
    if (prev->waves[i].live) y = prev->waves[i].y + (w->y - prev->waves[i].y) * alpha;
    t3_object_set_position(W.waves[i], 0, y, 0);
    t3_object_set_rotation(W.waves[i], 0, cur->stream_th[w->stream], 0);
  }
  for (int i = 0; i < MAX_LEAVES; i++) {
    const leaf *f = &cur->leaves[i];
    T3_OBJ(W.leaves[i])->visible = f->live;
    if (!f->live) continue;
    t3_vec3 p = on_cyl(PIPE_R + 0.1f, f->th, f->y);
    t3_object_set_position(W.leaves[i], p.x, p.y, p.z);
    t3_object_set_rotation(W.leaves[i], 0.4f, f->th, f->spin);
  }
  for (int i = 0; i < L->n_dews; i++) {
    T3_OBJ(W.dews[i])->visible = !(cur->dew_got >> i & 1);
    float bob = 1 + 0.08f * cw_sin(t * 3 + i);
    t3_object_set_scale(W.dews[i], bob, bob, bob);
  }

  /* stage 3: the silk line, the bird, flies, spools */
  {
    t3_vec3 sp = world_spider_pos(prev, cur, alpha, NULL);
    bool zip = cur->s.state == SP_ZIP;
    T3_OBJ(W.silk)->visible = zip;
    if (zip) {
      float th = cw_atan2(sp.x, sp.z);
      t3_vec3 top = on_cyl(PIPE_R + 0.06f, th, cur->zip_y + 0.1f);
      float dx = W.cam_eye.x - sp.x, dz = W.cam_eye.z - sp.z, l = sqrtf(dx * dx + dz * dz) + 1e-4f;
      float wx = -dz / l * 0.006f, wz = dx / l * 0.006f;
      t3_vec3 q[4] = { { sp.x - wx, sp.y, sp.z - wz }, { sp.x + wx, sp.y, sp.z + wz }, { top.x + wx, top.y, top.z + wz }, { top.x - wx, top.y, top.z - wz } };
      t3_attribute *pa = W.silk->geometry->attributes[T3_ATTR_POSITION];
      float *v = pa->array;
      int idx[6] = { 0, 2, 1, 0, 3, 2 };
      for (int k = 0; k < 6; k++) v[k * 3] = q[idx[k]].x, v[k * 3 + 1] = q[idx[k]].y, v[k * 3 + 2] = q[idx[k]].z;
      pa->version++;
      W.silk->geometry->version++;
    }
    int bt = cur->bird_ticks;
    W.bird->visible = bt > 0;
    W.mark->visible = bt > 0 && bt < BIRD_WARN;
    if (bt > 0) {
      float ft = bt + alpha;
      t3_vec3 at = on_cyl(PIPE_R + 0.02f, cur->bird_th, cur->bird_y);
      t3_object_set_position(W.mark, at.x, at.y, at.z);
      t3_object_set_rotation(W.mark, 0, cur->bird_th, 0);
      float pulse = 1 + 0.15f * cw_sin(ft * 0.4f);
      t3_object_set_scale(W.mark, pulse, pulse, 1);
      /* it swoops in from out in the garden, hovers, pecks, leaves */
      float in = cw_clampf(ft / 40.0f, 0, 1), out = cw_clampf((ft - BIRD_WARN - 20) / 40.0f, 0, 1);
      float dist = PIPE_R + 0.32f + 3.5f * (1 - in) * (1 - in) + 4 * out * out;
      float peck = (ft > BIRD_WARN - 8 && ft < BIRD_WARN + 6) ? 0.22f * cw_sin((ft - BIRD_WARN + 8) / 14 * 3.1415927f) : 0;
      t3_vec3 bp = on_cyl(dist - peck, cur->bird_th, cur->bird_y + 0.15f + 2.5f * (1 - in) + 3 * out);
      t3_object_set_position(W.bird, bp.x, bp.y, bp.z);
      t3_object_set_rotation(W.bird, peck * 2, cur->bird_th + 3.1415927f, 0);
      float flap = cw_sin(ft * (in < 1 || out > 0 ? 0.9f : 0.35f)) * 0.8f;
      for (int k = 0; k < 2; k++) t3_object_set_rotation(W.bird_wing[k], 0, 0, k ? -flap : flap);
    }
    for (int i = 0; i < MAX_FLIES; i++) {
      bool got = cur->fly_got >> i & 1;
      W.flies[i]->visible = !got;
      if (got) continue;
      float a = t * 2.2f + i * 2, rr = 0.12f;
      t3_vec3 c = on_cyl(PIPE_R + 0.18f, cur->fly_th[i] + rr * cw_cos(a) / PIPE_R, cur->fly_y[i] + rr * cw_sin(a * 1.3f));
      t3_object_set_position(W.flies[i], c.x, c.y, c.z);
      t3_object_set_rotation(W.flies[i], 0, cur->fly_th[i] + a, 0);
      float fl = cw_sin(t * 40 + i) * 0.7f;
      for (int k = 0; k < 2; k++) t3_object_set_rotation(W.fly_wing[i][k], fl, 0, 0);
    }
    for (int i = 0; i < MAX_SPOOLS; i++) {
      bool got = cur->spool_got >> i & 1;
      W.spools[i]->visible = !got;
      if (got) continue;
      t3_vec3 c = on_cyl(PIPE_R + 0.14f, cur->spool_th[i], cur->spool_y[i] + 0.05f * cw_sin(t * 2 + i));
      t3_object_set_position(W.spools[i], c.x, c.y, c.z);
      t3_object_set_rotation(W.spools[i], 0, t * 1.2f, 0.3f);
    }
  }
  /* the sun turns in with fine weather; clouds gather for rain */
  float H = L->height;
  t3_object_set_position(W.sun, 26, cur->s.y * 0.6f + 6 + 8 * W.sun_amount, -18);
  t3_object_set_rotation(W.sun, 0, -0.965f, t * 0.15f);
  T3_OBJ(W.sun)->visible = W.sun_amount > 0.05f;
  t3_object_set_scale(W.sun, 0.4f + 0.6f * W.sun_amount, 0.4f + 0.6f * W.sun_amount, 1);
  for (int i = 0; i < 6; i++) {
    W.cloud_x[i] += dt * 0.6f * (1 + i % 2);
    if (W.cloud_x[i] > 26) W.cloud_x[i] -= 52;
    float gather = W.rain_amount;
    /* round the open side of the garden, drifting; rain gathers them overhead */
    float b = 0.5f + i * 0.45f + W.cloud_x[i] * 0.01f, rr = 34 - gather * 12;
    t3_object_set_position(W.clouds[i], rr * cw_sin(b), cur->s.y * 0.6f + 9 + (i % 3) * 3 - gather * 4, rr * cw_cos(b));
    t3_object_set_rotation(W.clouds[i], 0, b + 3.1415927f, 0);
  }
  float g = 1 - 0.55f * W.rain_amount;
  W.cloud_mat->color = (t3_color){ g, g, g * 1.04f };
  W.cloud_mat->emissive = (t3_color){ 0.55f * g, 0.55f * g, 0.57f * g };
  /* the whole set darkens under rain, a little cooler */
  W.backdrop_mat->emissive_intensity = 0.75f * (1 - 0.55f * W.rain_amount);
  W.backdrop_mat->emissive = (t3_color){ W.backdrop_glow.r * (1 - 0.3f * W.rain_amount), W.backdrop_glow.g * (1 - 0.2f * W.rain_amount), W.backdrop_glow.b };
  for (int i = 0; i < 3; i++) {
    t3_color c = W.hill_glow[i];
    float k2 = 1 - 0.6f * W.rain_amount;
    W.hill_mat[i]->emissive = (t3_color){ c.r * k2, c.g * k2, c.b * k2 };
  }
  /* rain streaks round the spider */
  W.rain_mat->opacity = 0.55f * W.rain_amount;
  T3_OBJ(W.rain)->visible = W.rain_amount > 0.02f;
  if (T3_OBJ(W.rain)->visible) {
    t3_vec3 c = world_spider_pos(prev, cur, alpha, NULL);
    t3_attribute *pa = W.rain->geometry->attributes[T3_ATTR_POSITION];
    float *v = pa->array;
    for (int i = 0; i < RAIN_N; i++) {
      uint32_t h = (uint32_t)i * 2654435761u;
      float rx = ((h >> 8) & 1023) / 1023.0f, rz = ((h >> 18) & 1023) / 1023.0f, ry = ((h * 2246822519u) >> 20) / 4096.0f;
      bool snow = W.L->theme == THEME_WINTER;
      float y = c.y + 6 - fmodf(ry * 12 + t * (snow ? 1.3f : 9), 12);
      float a = rx * 6.2831853f, r = 0.9f + rz * 5;
      float px = r * cw_sin(a), pz = r * cw_cos(a);
      /* the streak's width runs across the view */
      float dx = W.cam_eye.x - px, dz = W.cam_eye.z - pz, l = sqrtf(dx * dx + dz * dz) + 1e-4f;
      float ww = snow ? 0.022f : 0.012f, hgt = snow ? 0.045f : 0.42f;
      if (snow) px += 0.3f * cw_sin(t * 0.8f + i), pz += 0.3f * cw_cos(t * 0.7f + i * 1.3f); /* flakes drift */
      float wx = -dz / l * ww, wz = dx / l * ww, tilt = snow ? 0 : 0.05f;
      t3_vec3 q[4] = { { px - wx, y, pz - wz }, { px + wx, y, pz + wz }, { px + wx + tilt, y + hgt, pz + wz }, { px - wx + tilt, y + hgt, pz - wz } };
      int o = i * 18, idx[6] = { 0, 2, 1, 0, 3, 2 }; /* wound toward the camera */
      for (int k = 0; k < 6; k++) v[o + k * 3] = q[idx[k]].x, v[o + k * 3 + 1] = q[idx[k]].y, v[o + k * 3 + 2] = q[idx[k]].z;
    }
    pa->version++;
    W.rain->geometry->version++;
  }
}

/* the camera: orbits the pipe behind the spider; only the orbit angle is
 * smoothed (per frame, frame-rate independent), the height and distance
 * follow the interpolated spider exactly, so it never lags and lurches */
void world_camera(cw_node cam, const sim *prev, const sim *cur, float alpha, float t, bool title) {
  if (!cw_node_valid(cam)) return;
  float th;
  t3_vec3 p = world_spider_pos(prev, cur, alpha, &th);
  static float last = -1;
  float dt = last < 0 ? 1 / 60.0f : cw_clampf(t - last, 0, 0.1f);
  last = t;
  /* from a little to the left, so the garden opens out past the house corner */
  float target = title ? th - 0.7f + 0.25f * cw_sin(t * 0.2f) : th - 0.55f;
  if (!W.cam_live) W.cam_phi = target, W.cam_live = true;
  W.cam_phi = wrap_pi(W.cam_phi + wrap_pi(target - W.cam_phi) * (1 - cw_exp(-3.0f * dt)));
  /* a little below the spider, looking up the climb ahead */
  float dist = title ? 3.6f : 2.9f, lift = title ? 0.6f : -0.05f, ahead = title ? 0.3f : 0.75f;
  t3_vec3 eye = on_cyl(dist, W.cam_phi, cw_clampf(p.y + lift, 0.35f, 1e9f));
  cw_node3d_set_position(cam, eye);
  W.cam_eye = eye;
  cw_node3d_look_at(cam, (t3_vec3){ p.x * 0.6f, p.y + ahead, p.z * 0.6f }, (t3_vec3){ 0, 1, 0 });
}
