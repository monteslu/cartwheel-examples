/* chromite world: the fortress deck far below, the craft on the play plane,
 * their fire and the power-ups, built with three.c (three.js r186) and hung
 * under one cartwheel node. Render-only: nothing here feeds the simulation.
 *
 *   the deck    a ring of six segments of machinery (plated decks, great
 *               round hatches with chrome rims, conduits with collars,
 *               vents, pylons, pairs of glowing status lamps), mirrored
 *               about the centre line as the original's ground was; it
 *               scrolls at the original's rate, far enough below the play
 *               plane to parallax
 *   the craft   pools of models per kind, posed from the interpolated sim
 *   fire        instanced emissive bolts per kind of round (HDR colours, so
 *               the bloom makes them glow)
 *   power-ups   a chrome core inside a lit ring and a halo
 *   the bomb    a widening violet ring from below the screen */
#include <stdlib.h>
#include <string.h>

#include "game.h"

uint32_t world_hash = CW_HASH32_INIT;

#define SEGS 6
#define SEG_L 24.0f
#define DECK_W 76.0f
#define SCROLL 1.39f /* deck units per second: the original's 1.7 at its depth */

enum { BK_HERO0, BK_HERO1, BK_HERO2, BK_HEAVY, BK_FLAK, BK_SLUG, BK_BEAM, BK_NEEDLE, BOLT_KINDS };
static const int BOLT_CAP[BOLT_KINDS] = { 256, 32, 64, 256, 512, 64, 512, 256 };

#define POOL_MAX 128
typedef struct pool { t3_object *obj[POOL_MAX]; t3_object *spin[POOL_MAX][4]; int nspin[POOL_MAX]; int n, used; } pool;

static struct {
  cw_node node;
  t3_object *root, *deck, *seg[SEGS], *play;
  int variation;
  t3_material *deck_mat, *trim_mat, *dark_mat, *grate_mat, *lamp_mat[3], *pipe_mat;
  t3_material *lamp_halo[3];
  pool pools[CRAFT_KINDS];
  t3_object *hero, *hero_spin[4];
  t3_mesh *shield;
  t3_material *shield_mat;
  t3_light *engine;
  t3_instanced_mesh *bolts[BOLT_KINDS];
  t3_object *pu[MAX_POWERUPS], *pu_ring[MAX_POWERUPS], *pu_core[MAX_POWERUPS];
  t3_material *pu_mat[PU_KINDS], *pu_core_mat[PU_KINDS];
  t3_sprite *pu_halo[MAX_POWERUPS];
  t3_material *halo_mat[PU_KINDS];
  t3_texture *halo_tex;
  t3_mesh *bomb;
  t3_material *bomb_mat;
  t3_light *bomb_light;
  t3_mesh *warn;
  t3_material *warn_mat;
  float cam_x;
} W;

static t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

static uint32_t hash(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float hf(uint32_t a, uint32_t b) { return (hash(a, b) & 0xffffff) / 16777216.0f; }

static t3_mesh *mesh(t3_object *parent, t3_geometry *g, t3_material *m, float x, float y, float z, bool cast, bool recv) {
  const t3_attribute *p = g->attributes[T3_ATTR_POSITION];
  if (p) world_hash = cw_hash32(p->array, sizeof(float) * 3 * (size_t)p->count, world_hash);
  t3_mesh *me = t3_mesh_new(g, m);
  t3_release(g);
  t3_object_set_position(me, x, y, z);
  T3_OBJ(me)->cast_shadow = cast;
  T3_OBJ(me)->receive_shadow = recv;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}

/* a radial glow: white in the middle to nothing at the edge */
static t3_texture *halo_texture(void) {
  enum { N = 64 };
  uint8_t px[N * N * 4];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1, d = sqrtf(dx * dx + dy * dy);
      float a = d >= 1 ? 0 : (1 - d) * (1 - d);
      uint8_t *p = px + (y * N + x) * 4;
      p[0] = p[1] = p[2] = 255;
      p[3] = (uint8_t)(a * 255 + 0.5f);
    }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}

/* ── the deck ──────────────────────────────────────────────────────── */

static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  t3_material_set_texture(m, T3_ENV_MAP, env_texture());
  m->env_map_intensity = 0.25f;
  return m;
}

static void deck_materials(int variation) {
  static const float TINT[3][3] = { { 0.86f, 0.86f, 0.9f }, { 0.94f, 0.84f, 0.7f }, { 0.74f, 0.88f, 0.85f } };
  static const float LAMP[3][3][3] = {
    { { 1, 0.1f, 0.12f }, { 0.75f, 0.2f, 1 }, { 1, 0.5f, 0.6f } },
    { { 1, 0.45f, 0.08f }, { 1, 0.15f, 0.08f }, { 1, 0.85f, 0.4f } },
    { { 0.2f, 1, 0.75f }, { 0.4f, 0.5f, 1 }, { 1, 0.2f, 0.5f } },
  };
  const float *t = TINT[variation];
  W.deck_mat = t3_mesh_physical_material_new(0xffffff);
  tex_apply(W.deck_mat, MT_DECK, DECK_W / 12, SEG_L / 12);
  W.deck_mat->color = (t3_color){ t[0] * 0.42f, t[1] * 0.42f, t[2] * 0.42f };
  t3_material_set_texture(W.deck_mat, T3_ENV_MAP, env_texture());
  W.deck_mat->env_map_intensity = 0.1f;
  W.deck_mat->metalness = 0.45f;
  W.trim_mat = t3_mesh_physical_material_new(0xffffff);
  tex_apply(W.trim_mat, MT_DECK, 1, 1);
  W.trim_mat->color = (t3_color){ t[0] * 0.6f, t[1] * 0.6f, t[2] * 0.6f };
  t3_material_set_texture(W.trim_mat, T3_ENV_MAP, env_texture());
  W.trim_mat->env_map_intensity = 0.12f;
  W.trim_mat->metalness = 0.55f;
  W.dark_mat = phys(lin(0.06f, 0.065f, 0.075f), 0.55f, 0.8f);
  W.pipe_mat = phys(lin(0.45f * t[0], 0.45f * t[1], 0.47f * t[2]), 0.3f, 1);
  W.grate_mat = t3_mesh_physical_material_new(0xffffff);
  tex_apply(W.grate_mat, MT_GRATE, 1, 1);
  t3_material_set_texture(W.grate_mat, T3_ENV_MAP, env_texture());
  W.grate_mat->env_map_intensity = 0.4f;
  for (int i = 0; i < 3; i++) {
    t3_material *m = t3_mesh_standard_material_new(0x000000);
    m->color = (t3_color){ 0.02f, 0.02f, 0.02f };
    m->emissive = lin(LAMP[variation][i][0], LAMP[variation][i][1], LAMP[variation][i][2]);
    m->emissive_intensity = 2.5f;
    m->roughness = 0.3f;
    m->metalness = 0;
    W.lamp_mat[i] = m;
    t3_material *h = t3_sprite_material_new(0xffffff);
    h->color = (t3_color){ m->emissive.r * 1.6f, m->emissive.g * 1.6f, m->emissive.b * 1.6f };
    t3_material_set_map(h, W.halo_tex);
    h->blending = T3_ADDITIVE_BLENDING;
    h->depth_write = false;
    W.lamp_halo[i] = h;
  }
}

static t3_geometry *box(float w, float h, float d) { return t3_box_geometry_new(w, h, d, 1, 1, 1); }
static t3_geometry *cyl(float r0, float r1, float h, int seg) { return t3_cylinder_geometry_new(r0, r1, h, seg, 1, false); }
static t3_geometry *torus(float r, float tube, int rs, int ts) {
  t3_geometry *g = t3_torus_geometry_new(r, tube, rs, ts, 6.2831853f);
  t3_geometry_rotate_x(g, 1.5707963f);
  return g;
}
static t3_geometry *pipe_z(float r, float len, int seg) {
  t3_geometry *g = cyl(r, r, len, seg);
  t3_geometry_rotate_x(g, 1.5707963f);
  return g;
}

/* a great round hatch: a raised collar, a chrome rim, a dark well with an
 * iris of plates, a lamp at its heart */
static void hatch(t3_object *s, float x, float z, float r, int lamp) {
  mesh(s, cyl(r * 1.08f, r * 1.18f, 0.7f, 40), W.trim_mat, x, 0.35f, z, true, true);
  mesh(s, torus(r, 0.16f, 40, 10), model_mat(M_CHROME), x, 0.72f, z, true, true);
  mesh(s, cyl(r * 0.94f, r * 0.94f, 0.3f, 40), W.dark_mat, x, 0.5f, z, false, true);
  for (int i = 0; i < 6; i++) {
    t3_geometry *g = box(r * 0.5f, 0.12f, r * 0.9f);
    t3_geometry_translate(g, 0, 0, r * 0.4f);
    t3_geometry_rotate_y(g, i * 1.0471976f + 0.3f);
    mesh(s, g, W.trim_mat, x, 0.7f, z, false, true);
  }
  mesh(s, t3_sphere_geometry_new_ex(r * 0.22f, 20, 10, 0, 6.2831853f, 0, 1.5707963f), W.lamp_mat[lamp], x, 0.72f, z, false, false);
  mesh(s, torus(r * 0.3f, 0.06f, 24, 8), model_mat(M_CHROME), x, 0.78f, z, false, true);
  t3_sprite *h = t3_sprite_new(W.lamp_halo[lamp]);
  t3_object_set_position(h, x, 1.4f, z);
  t3_object_set_scale(h, r * 0.9f, r * 0.9f, r * 0.9f);
  t3_object_add(s, h);
  t3_release(h);
}

/* a raised plate with a bevelled cap and rivet studs at its corners */
static void plate(t3_object *s, float x, float z, float w, float d, float h) {
  mesh(s, box(w, h, d), W.trim_mat, x, h * 0.5f, z, true, true);
  mesh(s, box(w - 0.3f, 0.12f, d - 0.3f), W.deck_mat, x, h + 0.06f, z, false, true);
  for (int c = 0; c < 4; c++)
    mesh(s, t3_sphere_geometry_new_ex(0.12f, 10, 6, 0, 6.2831853f, 0, 1.5707963f), model_mat(M_CHROME), x + (c & 1 ? 1 : -1) * (w * 0.5f - 0.3f),
         h + 0.1f, z + (c & 2 ? 1 : -1) * (d * 0.5f - 0.3f), false, true);
}

/* a conduit along the deck with collars, laid in a shallow channel */
static void conduit(t3_object *s, float x, float r) {
  mesh(s, box(r * 3.2f, 0.25f, SEG_L), W.dark_mat, x, 0.12f, SEG_L * 0.5f, false, true);
  mesh(s, pipe_z(r, SEG_L, 20), W.pipe_mat, x, r + 0.1f, SEG_L * 0.5f, true, true);
  for (int i = 0; i < 4; i++) {
    t3_geometry *g = t3_torus_geometry_new(r * 1.15f, r * 0.22f, 20, 8, 6.2831853f);
    mesh(s, g, model_mat(M_CHROME), x, r + 0.1f, 3 + i * 6, false, true);
  }
}

/* a lamp post: a short chrome column with a glowing cap */
static void lamp(t3_object *s, float x, float z, int which) {
  mesh(s, cyl(0.28f, 0.36f, 0.5f, 16), model_mat(M_CHROME), x, 0.25f, z, true, true);
  mesh(s, t3_sphere_geometry_new_ex(0.3f, 16, 8, 0, 6.2831853f, 0, 1.5707963f), W.lamp_mat[which], x, 0.5f, z, false, false);
  t3_sprite *h = t3_sprite_new(W.lamp_halo[which]);
  t3_object_set_position(h, x, 0.9f, z);
  t3_object_set_scale(h, 2.2f, 2.2f, 2.2f);
  t3_object_add(s, h);
  t3_release(h);
}

/* a pylon: a tall block of machinery near the edges, with a mast */
static void pylon(t3_object *s, float x, float z, float h, uint32_t seed) {
  float w = 2.4f + hf(seed, 1) * 2, d = 2.4f + hf(seed, 2) * 2;
  mesh(s, box(w, h, d), W.trim_mat, x, h * 0.5f, z, true, true);
  mesh(s, box(w * 0.7f, 0.6f, d * 0.7f), W.dark_mat, x, h + 0.3f, z, true, true);
  mesh(s, box(w + 0.2f, 0.3f, d + 0.2f), model_mat(M_CHROME), x, h * 0.7f, z, false, true);
  mesh(s, cyl(0.06f, 0.1f, 3, 8), model_mat(M_CHROME), x + w * 0.2f, h + 2.1f, z, true, true);
  mesh(s, t3_sphere_geometry_new(0.18f, 10, 8), W.lamp_mat[0], x + w * 0.2f, h + 3.6f, z, false, false);
}

/* a vent bank: a grate set into a frame */
static void vent(t3_object *s, float x, float z, float w, float d) {
  mesh(s, box(w + 0.4f, 0.35f, d + 0.4f), W.trim_mat, x, 0.17f, z, true, true);
  mesh(s, box(w, 0.05f, d), W.grate_mat, x, 0.36f, z, false, true);
}

static void build_segment(t3_object *s, int layout, int variation) {
  uint32_t seed = (uint32_t)layout * 977u + (uint32_t)variation * 31u + 7u;
  /* the deck plate itself */
  t3_geometry *g = t3_plane_geometry_new(DECK_W, SEG_L, 1, 1);
  t3_geometry_rotate_x(g, -1.5707963f);
  mesh(s, g, W.deck_mat, 0, 0, SEG_L * 0.5f, false, true);
  /* the centre: a hatch, or a vent bank between plates */
  if (layout % 2 == 0) hatch(s, 0, SEG_L * 0.5f, 4 + hf(seed, 3) * 1.5f, layout % 3);
  else {
    vent(s, 0, SEG_L * 0.3f, 5, 3);
    vent(s, 0, SEG_L * 0.72f, 5, 3);
    plate(s, 0, SEG_L * 0.5f, 7, 2.6f, 0.6f);
  }
  /* mirrored machinery out to the edges */
  for (int side = -1; side <= 1; side += 2) {
    conduit(s, side * (9 + hf(seed, 10)), 0.42f);
    if (hf(seed, 11) > 0.4f) hatch(s, side * 16, SEG_L * (0.3f + 0.4f * hf(seed, 12)), 2.6f, (layout + 1) % 3);
    else vent(s, side * 16, SEG_L * 0.5f, 4, 6);
    for (int k = 0; k < 3; k++) {
      float z = SEG_L * (0.15f + 0.3f * k) + hf(seed, 20 + k) * 2;
      float w = 2.5f + 3 * hf(seed, 30 + k), d = 2 + 3 * hf(seed, 40 + k), h = 0.4f + 1.2f * hf(seed, 50 + k);
      plate(s, side * (5 + w * 0.5f + 0.5f), z, w, d, h);
    }
    /* lamps in pairs, as the original's status lights */
    lamp(s, side * 6.2f, SEG_L * 0.08f, 0);
    lamp(s, side * 6.2f, SEG_L * 0.92f, 1);
    lamp(s, side * 12.6f, SEG_L * (0.2f + 0.6f * hf(seed, 60)), 2);
    /* pylons along the far edges: the big parallax */
    if (hf(seed, 70) > 0.3f) pylon(s, side * (24 + 4 * hf(seed, 71)), SEG_L * hf(seed, 72), 3 + 6 * hf(seed, 73), seed + 5);
    plate(s, side * 31, SEG_L * 0.5f, 6, SEG_L - 2, 2.2f);
  }
}

static void build_deck(int variation) {
  W.deck = t3_group_new();
  t3_object_set_position(W.deck, 0, GROUND_Y, 0);
  for (int i = 0; i < SEGS; i++) {
    W.seg[i] = t3_group_new();
    build_segment(W.seg[i], i, variation);
    t3_object_add(W.deck, W.seg[i]);
    t3_release(W.seg[i]);
  }
  t3_object_add(W.root, W.deck);
  t3_release(W.deck);
}

/* ── craft, fire, power-ups ────────────────────────────────────────── */

static t3_object *pool_get(int kind) {
  pool *p = &W.pools[kind];
  if (p->used == p->n) {
    if (p->n == POOL_MAX) return NULL;
    p->obj[p->n] = model_new(kind, W.play, p->spin[p->n], &p->nspin[p->n]);
    p->n++;
  }
  t3_object *o = p->obj[p->used++];
  o->visible = true;
  return o;
}

static t3_material *bolt_mat(float r, float g, float b, float k) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ r * k, g * k, b * k };
  m->blending = T3_ADDITIVE_BLENDING;
  m->transparent = true;
  m->depth_write = false;
  return m;
}

static void build_fire(void) {
  /* colours after the original's rounds: the hero's pale blue bolts, the
   * green lance, the violet hammers; the enemies' red heavies, amber flak,
   * white slugs, red beam and pink needles */
  static const float C[BOLT_KINDS][4] = {
    { 0.15f, 0.45f, 1, 2.2f }, { 0.2f, 1, 0.25f, 2 }, { 0.6f, 0.15f, 1, 2.4f }, { 1, 0.22f, 0.08f, 2.6f },
    { 1, 0.55f, 0.1f, 2.6f }, { 0.85f, 0.85f, 1, 3 }, { 1, 0.12f, 0.2f, 3 }, { 1, 0.3f, 0.8f, 2.6f },
  };
  for (int k = 0; k < BOLT_KINDS; k++) {
    t3_geometry *g = t3_sphere_geometry_new(1, 12, 8);
    t3_material *m = bolt_mat(C[k][0], C[k][1], C[k][2], C[k][3]);
    W.bolts[k] = t3_instanced_mesh_new(g, m, BOLT_CAP[k]);
    t3_release(g);
    t3_release(m);
    W.bolts[k]->count = 0;
    T3_OBJ(W.bolts[k])->frustum_culled = false;
    t3_object_add(W.play, W.bolts[k]);
    t3_release(W.bolts[k]);
  }
}

static void build_powerups(void) {
  static const float C[PU_KINDS][3] = { { 0.2f, 0.5f, 1 }, { 1, 0.9f, 0.6f }, { 1, 0.2f, 0.15f }, { 1, 0.85f, 0.2f }, { 0.25f, 1, 0.3f }, { 0.7f, 0.25f, 1 } };
  for (int k = 0; k < PU_KINDS; k++) {
    t3_material *ring = t3_mesh_standard_material_new(0x000000);
    ring->color = (t3_color){ 0, 0, 0 };
    ring->emissive = (t3_color){ C[k][0], C[k][1], C[k][2] };
    ring->emissive_intensity = 6;
    W.pu_mat[k] = ring;
    t3_material *core = t3_mesh_physical_material_new(0xffffff);
    core->color = (t3_color){ 0.6f + 0.4f * C[k][0], 0.6f + 0.4f * C[k][1], 0.6f + 0.4f * C[k][2] };
    core->metalness = 1;
    core->roughness = 0.12f;
    t3_material_set_texture(core, T3_ENV_MAP, env_texture());
    W.pu_core_mat[k] = core;
    t3_material *h = t3_sprite_material_new(0xffffff);
    h->color = (t3_color){ C[k][0] * 3, C[k][1] * 3, C[k][2] * 3 };
    t3_material_set_map(h, W.halo_tex);
    h->blending = T3_ADDITIVE_BLENDING;
    h->depth_write = false;
    W.halo_mat[k] = h;
  }
  for (int i = 0; i < MAX_POWERUPS; i++) {
    W.pu[i] = t3_group_new();
    t3_geometry *rg = t3_torus_geometry_new(0.55f, 0.07f, 32, 8, 6.2831853f);
    t3_mesh *r = t3_mesh_new(rg, W.pu_mat[0]);
    t3_release(rg);
    t3_object_add(W.pu[i], r);
    t3_release(r);
    W.pu_ring[i] = T3_OBJ(r);
    t3_geometry *cg = t3_icosahedron_geometry_new(0.3f, 0);
    t3_mesh *c = t3_mesh_new(cg, W.pu_core_mat[0]);
    t3_release(cg);
    T3_OBJ(c)->cast_shadow = true;
    t3_object_add(W.pu[i], c);
    t3_release(c);
    W.pu_core[i] = T3_OBJ(c);
    W.pu_halo[i] = t3_sprite_new(W.halo_mat[0]);
    t3_object_set_scale(W.pu_halo[i], 2.4f, 2.4f, 2.4f);
    t3_object_add(W.pu[i], W.pu_halo[i]);
    t3_release(W.pu_halo[i]);
    W.pu[i]->visible = false;
    t3_object_add(W.play, W.pu[i]);
    t3_release(W.pu[i]);
  }
}

static void build_hero(void) {
  int ns = 0;
  W.hero = model_new(CK_HERO, W.play, W.hero_spin, &ns);
  W.shield_mat = t3_mesh_basic_material_new(0xffffff);
  W.shield_mat->color = (t3_color){ 0.2f, 0.45f, 1.2f };
  W.shield_mat->blending = T3_ADDITIVE_BLENDING;
  W.shield_mat->transparent = true;
  W.shield_mat->depth_write = false;
  W.shield_mat->opacity = 0;
  t3_geometry *sg = t3_icosahedron_geometry_new(1.3f, 3);
  W.shield = t3_mesh_new(sg, W.shield_mat);
  t3_release(sg);
  t3_object_add(W.hero, W.shield);
  t3_release(W.shield);
  W.engine = t3_point_light_new(0x88aaff, 0, 8, 2);
  t3_object_set_position(W.engine, 0, 0.3f, 1.4f);
  t3_object_add(W.hero, W.engine);
  t3_release(W.engine);
}

static void build_bomb(void) {
  t3_geometry *g = t3_torus_geometry_new(1, 0.05f, 96, 8, 6.2831853f);
  t3_geometry_rotate_x(g, 1.5707963f);
  W.bomb_mat = t3_mesh_basic_material_new(0xffffff);
  W.bomb_mat->color = (t3_color){ 2.4f, 0.8f, 4.5f };
  W.bomb_mat->blending = T3_ADDITIVE_BLENDING;
  W.bomb_mat->transparent = true;
  W.bomb_mat->depth_write = false;
  W.bomb = t3_mesh_new(g, W.bomb_mat);
  t3_release(g);
  T3_OBJ(W.bomb)->visible = false;
  T3_OBJ(W.bomb)->frustum_culled = false;
  t3_object_add(W.play, W.bomb);
  t3_release(W.bomb);
  W.bomb_light = t3_point_light_new(0xaa66ff, 0, 40, 2);
  t3_object_add(W.play, W.bomb_light);
  t3_release(W.bomb_light);
  /* the red bar along the bottom when something is getting past */
  t3_geometry *bg = t3_plane_geometry_new(FIELD_X * 2 + 4, 1.2f, 1, 1);
  t3_geometry_rotate_x(bg, -1.5707963f);
  W.warn_mat = t3_mesh_basic_material_new(0xffffff);
  W.warn_mat->color = (t3_color){ 3, 0.15f, 0.1f };
  W.warn_mat->blending = T3_ADDITIVE_BLENDING;
  W.warn_mat->transparent = true;
  W.warn_mat->depth_write = false;
  W.warn_mat->opacity = 0;
  W.warn = t3_mesh_new(bg, W.warn_mat);
  t3_release(bg);
  t3_object_set_position(W.warn, 0, PLAY_Y - 0.5f, FIELD_Y + 0.2f);
  t3_object_add(W.play, W.warn);
  t3_release(W.warn);
}

void world_build(cw_node root, int variation) {
  world_free();
  models_init();
  W.variation = variation;
  W.node = cw_node_new("Node3D", "World");
  cw_node_add_child(root, W.node);
  W.root = t3_group_new();
  W.play = t3_group_new();
  t3_object_add(W.root, W.play);
  t3_release(W.play);
  W.halo_tex = halo_texture();
  deck_materials(variation);
  build_deck(variation);
  build_fire();
  build_powerups();
  build_hero();
  build_bomb();
  cw_node3d_set_object(W.node, W.root);
  t3_release(W.root);
}

void world_free(void) {
  if (cw_node_valid(W.node)) cw_node_free(W.node);
  t3_material *mats[] = { W.deck_mat, W.trim_mat, W.dark_mat, W.grate_mat, W.lamp_mat[0], W.lamp_mat[1], W.lamp_mat[2], W.lamp_halo[0], W.lamp_halo[1], W.lamp_halo[2], W.pipe_mat, W.shield_mat, W.bomb_mat, W.warn_mat };
  for (size_t i = 0; i < sizeof mats / sizeof mats[0]; i++)
    if (mats[i]) t3_release(mats[i]);
  for (int k = 0; k < PU_KINDS; k++) {
    if (W.pu_mat[k]) t3_release(W.pu_mat[k]);
    if (W.pu_core_mat[k]) t3_release(W.pu_core_mat[k]);
    if (W.halo_mat[k]) t3_release(W.halo_mat[k]);
  }
  if (W.halo_tex) t3_release(W.halo_tex);
  memset(&W, 0, sizeof W);
}

/* ── per frame ─────────────────────────────────────────────────────── */

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static void set_bolt(t3_instanced_mesh *im, int *n, int cap, float x, float y, float sx, float sz) {
  if (*n >= cap) return;
  t3_mat4 m;
  t3_vec3 p = to_world(x, y);
  t3_mat4_compose(&m, p, t3_quat_identity(), (t3_vec3){ sx, sx, sz });
  t3_instanced_mesh_set_matrix_at(im, (*n)++, &m);
}

/* a mesh drawn with another material (the mesh keeps a reference) */
static void swap_mat(t3_mesh *m, t3_material *mat) {
  t3_retain(mat);
  t3_release(m->materials[0]);
  m->materials[0] = mat;
}

static int bolt_kind_e(int ammo) { return BK_HEAVY + ammo; }

void world_sync(const sim *prev, const sim *cur, float a, float t, int state) {
  (void)state;
  if (!W.root) return;
  /* the deck scrolls on the level's clock */
  float travel = lerp(prev->frame, cur->frame, a) / 50.0f * SCROLL;
  for (int i = 0; i < SEGS; i++) {
    float z = travel - i * SEG_L;
    float span = SEGS * SEG_L, lo = -100;
    z = z - span * floorf((z - lo) / span);
    t3_object_set_position(W.seg[i], 0, 0, z);
  }
  /* lamps breathe, as the original's ground lights did */
  for (int i = 0; i < 3; i++) W.lamp_mat[i]->emissive_intensity = 1.6f + 1.4f * (0.5f + 0.5f * sinf(t * (1.3f + i * 0.7f) + i * 2));

  /* craft */
  for (int k = 0; k < CRAFT_KINDS; k++) {
    pool *p = &W.pools[k];
    for (int i = 0; i < p->n; i++) p->obj[i]->visible = false;
    p->used = 0;
  }
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const enemy *e = &cur->en[i], *pe = &prev->en[i];
    if (!e->live) continue;
    bool same = pe->live && pe->id == e->id;
    float x = same ? lerp(pe->x, e->x, a) : e->x, y = same ? lerp(pe->y, e->y, a) : e->y;
    int kind = CK_STRAIGHT + e->kind;
    t3_object *o = pool_get(kind);
    if (!o) continue;
    pool *p = &W.pools[kind];
    int slot = p->used - 1;
    t3_vec3 w = to_world(x, y);
    t3_object_set_position(o, w.x, w.y, w.z);
    /* craft face down the screen; they bank into their sideways drift */
    float bank = cw_clampf(-(e->lmx + e->smx) * 6, -0.6f, 0.6f);
    float age = e->age / 50.0f;
    float bob = 0.15f * sinf(age * 2.1f + e->phase);
    t3_object_set_rotation(o, 0.12f + bob * 0.3f, 3.1415927f, bank);
    float s = e->kind >= EK_BOSS0 ? 1.4f : 1.3f;
    t3_object_set_scale(o, s, s, s);
    for (int k = 0; k < p->nspin[slot]; k++) t3_object_set_rotation(p->spin[slot][k], p->spin[slot][k]->rotation.x, age * (e->kind == EK_GNAT ? 9 : 3) + e->phase, p->spin[slot][k]->rotation.z);
  }

  /* the hero */
  const hero *h = &cur->h, *ph = &prev->h;
  bool shown = cur->h.dont_show <= 0 && cur->mode != PLAY_DEAD;
  W.hero->visible = shown;
  if (shown) {
    float x = lerp(ph->x, h->x, a), y = lerp(ph->y, h->y, a);
    t3_vec3 w = to_world(x, y);
    t3_object_set_position(W.hero, w.x, w.y, w.z);
    float vx = h->x - ph->x;
    t3_object_set_rotation(W.hero, 0.05f, 0, cw_clampf(-vx * 4, -0.5f, 0.5f));
    t3_object_set_scale(W.hero, 1.3f, 1.3f, 1.3f);
    float super = h->shields > HERO_SHIELDS ? (h->shields - HERO_SHIELDS) / HERO_SHIELDS : 0;
    W.shield_mat->opacity = cw_clampf(h->shield_flash * 0.5f + super * (0.12f + 0.05f * sinf(t * 9)), 0, 1);
    T3_OBJ(W.shield)->visible = W.shield_mat->opacity > 0.01f;
    float heat = h->engine_heat;
    W.engine->color = (t3_color){ lerp(0.5f, 1, 1 - heat), lerp(0.25f, 0.65f, heat), lerp(0.1f, 1, heat) };
    W.engine->intensity = 6 + 2 * sinf(t * 40);
  }

  /* fire */
  int n[BOLT_KINDS] = { 0 };
  for (int i = 0; i < MAX_HBULLETS; i++) {
    const hbullet *b = &cur->hb[i];
    if (!b->live) continue;
    float y = b->y - b->vy * K * (1 - a);
    if (b->gun == GUN_DEFAULT) set_bolt(W.bolts[BK_HERO0], &n[BK_HERO0], BOLT_CAP[BK_HERO0], b->x, y, 0.07f, 0.6f);
    else if (b->gun == GUN_GREEN) set_bolt(W.bolts[BK_HERO1], &n[BK_HERO1], BOLT_CAP[BK_HERO1], b->x, y, 0.16f + 0.03f * sinf(t * 30 + i), 1.5f);
    else set_bolt(W.bolts[BK_HERO2], &n[BK_HERO2], BOLT_CAP[BK_HERO2], b->x, y, 0.3f, 1.2f);
  }
  static const float ES[EA_KINDS][2] = { { 0.24f, 0.5f }, { 0.2f, 0.2f }, { 0.4f, 0.4f }, { 0.22f, 0.45f }, { 0.1f, 0.42f } };
  for (int i = 0; i < MAX_EBULLETS; i++) {
    const ebullet *b = &cur->eb[i];
    if (!b->live) continue;
    float x = b->x - b->vx * K * (1 - a), y = b->y - b->vy * K * (1 - a);
    int k = bolt_kind_e(b->kind);
    set_bolt(W.bolts[k], &n[k], BOLT_CAP[k], x, y, ES[b->kind][0], ES[b->kind][1]);
  }
  for (int k = 0; k < BOLT_KINDS; k++) {
    W.bolts[k]->count = n[k];
    if (n[k]) W.bolts[k]->instance_matrix->version++;
    T3_OBJ(W.bolts[k])->visible = n[k] > 0;
  }

  /* power-ups */
  for (int i = 0; i < MAX_POWERUPS; i++) {
    const powerup *p = &cur->pu[i];
    W.pu[i]->visible = p->live;
    if (!p->live) continue;
    float age = p->age / 50.0f;
    t3_vec3 w = to_world(p->x + 0.1f * sinf(age * 7), p->y + 0.3f * sinf(age * 4.2f));
    t3_object_set_position(W.pu[i], w.x, w.y + 0.3f, w.z);
    t3_mesh *ring = (t3_mesh *)W.pu_ring[i];
    t3_mesh *core = (t3_mesh *)W.pu_core[i];
    if (ring->materials[0] != W.pu_mat[p->kind]) {
      swap_mat(ring, W.pu_mat[p->kind]);
      swap_mat(core, W.pu_core_mat[p->kind]);
      swap_mat(&W.pu_halo[i]->mesh, W.halo_mat[p->kind]);
    }
    t3_object_set_rotation(W.pu_ring[i], 1.2f + 0.3f * sinf(age * 2), age * 2.5f, 0);
    t3_object_set_rotation(W.pu_core[i], age * 1.7f, age * 2.3f, 0);
  }

  /* the bomb */
  float bomb = h->bomb > 0 ? lerp(ph->bomb > 0 ? ph->bomb : h->bomb, h->bomb, a) : 0;
  T3_OBJ(W.bomb)->visible = bomb > 0;
  if (bomb > 0) {
    t3_vec3 c = to_world(0, -15);
    float r = bomb * 0.1f;
    t3_object_set_position(W.bomb, c.x, c.y, c.z);
    t3_object_set_scale(W.bomb, r, r * 4, r);
    W.bomb_mat->opacity = cw_clampf(1.4f - bomb / 300, 0, 1);
    t3_object_set_position(W.bomb_light, 0, 2, -(-15 + r));
    W.bomb_light->intensity = 400 * W.bomb_mat->opacity;
  } else W.bomb_light->intensity = 0;
  W.warn_mat->opacity = cw_clampf(cur->past_warn, 0, 1) * (0.55f + 0.45f * sinf(t * 35 / 3));
  T3_OBJ(W.warn)->visible = W.warn_mat->opacity > 0.01f;
}

/* the camera: high over the play plane, tipped forward a little so the
 * craft show their flanks and the deck below runs away in depth */
void world_camera(cw_node cam, const sim *S, float t, int state) {
  (void)t;
  float target = state == 0 ? 0 : S->h.x * 0.12f;
  W.cam_x += (target - W.cam_x) * 0.05f;
  float zoom = S->mode == PLAY_DEAD ? (50 - S->hero_death) / 50 : 0;
  if (zoom < 0) zoom = 0;
  if (zoom > 1) zoom = 1;
  zoom = zoom * zoom;
  t3_vec3 eye = { W.cam_x, 31 - 8 * zoom, 7.5f };
  t3_vec3 at = { W.cam_x * 0.5f, 0, -0.8f };
  cw_node3d_set_position(cam, eye);
  cw_node3d_look_at(cam, at, (t3_vec3){ 0, 1, 0 });
}
