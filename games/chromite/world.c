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
#include <stdio.h>
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
#define LAMPS_MAX 128
typedef struct pool { t3_object *obj[POOL_MAX]; t3_object *spin[POOL_MAX][4]; int nspin[POOL_MAX]; t3_material *glow[POOL_MAX]; int n, used; } pool;

static struct {
  cw_node node;
  t3_object *root, *deck, *seg[SEGS], *play;
  int variation;
  t3_material *deck_mat, *trim_mat, *dark_mat, *grate_mat, *lamp_mat[3], *pipe_mat;
  t3_material *lamp_halo[3];
  t3_light *lamp_light[LAMPS_MAX]; /* the deck lamps' own light (clustered: cartwheel/lighting.h) */
  float lamp_light_k[LAMPS_MAX];
  int lamp_light_kind[LAMPS_MAX], lamps;
  t3_material *steel_mat;
  /* the stage worlds: level 1, 4, 7.. the fortress deck; 2, 5, 8.. an orbital ring over a planet; 3, 6, 9.. a
   * foundry with rivers of melt */
  t3_mesh *planet;
  t3_material *planet_mat, *lava_mat;
  t3_light *lava_light[SEGS * 6];
  float lava_light_k[SEGS * 6];
  int lava_lights;
  /* pits: holes in the fortress and foundry decks down to a machinery floor 10 below, haze in them (a
   * FogVolume3D each, which the engine draws on Medium and up) and a work light sweeping slowly along each */
  cw_node pit_fog[SEGS];
  t3_light *pit_light[SEGS];
  int pit_seg[SEGS], pits;
  pool pools[CRAFT_KINDS];
  t3_object *hero, *hero_spin[4];
  int hero_flames;
  t3_mesh *shield;
  t3_material *shield_mat;
  t3_light *engine;
  t3_mesh *hitbox;          /* the ship's hit point: a small white dot with a ring, always drawn over the hull */
  t3_material *hitbox_mat[2];
  t3_instanced_mesh *bolts[BOLT_KINDS];
  t3_instanced_mesh *cores[BOLT_KINDS], *glows[BOLT_KINDS]; /* enemy rounds: a white core and a soft halo round each */
  t3_material *glow_mat[BOLT_KINDS];
  t3_object *pu[MAX_POWERUPS], *pu_ring[MAX_POWERUPS], *pu_core[MAX_POWERUPS];
  t3_material *pu_mat[PU_KINDS], *pu_core_mat[PU_KINDS];
  t3_geometry *pu_geo[PU_KINDS];
  t3_sprite *pu_halo[MAX_POWERUPS];
  t3_material *halo_mat[PU_KINDS];
  t3_texture *halo_tex;
  t3_mesh *bomb;
  t3_material *bomb_mat;
  t3_light *bomb_light;
  t3_mesh *warn;
  t3_material *warn_mat;
  float cam_x;
  /* score chips: cancelled enemy rounds as small gold gems that fly home to the ship */
  t3_instanced_mesh *chips;
  float chip_x[MAX_CHIPS], chip_y[MAX_CHIPS], chip_vx[MAX_CHIPS], chip_vy[MAX_CHIPS], chip_age[MAX_CHIPS];
  int chip_live[MAX_CHIPS], chip_next;
  float chip_t; /* the frame time the chips last moved */
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
  static const float TINT[3][3] = { { 0.86f, 0.86f, 0.9f }, { 0.8f, 0.84f, 0.92f }, { 0.9f, 0.74f, 0.62f } };
  /* the deck's lamps keep to hues no round uses (enemy fire is magenta and cyan, the ship's pale blue): warm
   * white, amber and a sodium yellow-green, and they are bars, not dots, so nothing on the deck reads as a bullet */
  static const float LAMP[3][3][3] = {
    { { 1, 0.78f, 0.5f }, { 1, 0.55f, 0.16f }, { 1, 0.9f, 0.7f } },
    { { 1, 0.55f, 0.12f }, { 1, 0.78f, 0.45f }, { 0.95f, 0.85f, 0.3f } },
    { { 0.7f, 1, 0.25f }, { 1, 0.85f, 0.45f }, { 0.85f, 0.95f, 0.4f } },
  };
  const float *t = TINT[variation];
  W.deck_mat = t3_mesh_physical_material_new(0xffffff);
  tex_apply(W.deck_mat, MT_DECK, DECK_W / 12, SEG_L / 12);
  W.deck_mat->color = (t3_color){ t[0] * 0.28f, t[1] * 0.28f, t[2] * 0.28f }; /* a step under the action layer */
  t3_material_set_texture(W.deck_mat, T3_ENV_MAP, env_texture());
  W.deck_mat->env_map_intensity = 0.1f;
  W.deck_mat->metalness = 0.45f;
  W.trim_mat = t3_mesh_physical_material_new(0xffffff);
  tex_apply(W.trim_mat, MT_DECK, 1, 1);
  W.trim_mat->color = (t3_color){ t[0] * 0.5f, t[1] * 0.5f, t[2] * 0.5f };
  t3_material_set_texture(W.trim_mat, T3_ENV_MAP, env_texture());
  W.trim_mat->env_map_intensity = 0.12f;
  W.trim_mat->metalness = 0.55f;
  W.dark_mat = phys(lin(0.06f, 0.065f, 0.075f), 0.55f, 0.8f);
  W.steel_mat = phys(lin(0.42f, 0.42f, 0.44f), 0.32f, 1);
  W.steel_mat->env_map_intensity = 0.35f;
  W.pipe_mat = phys(lin(0.45f * t[0], 0.45f * t[1], 0.47f * t[2]), 0.3f, 1);
  W.grate_mat = t3_mesh_physical_material_new(0xffffff);
  tex_apply(W.grate_mat, MT_GRATE, 1, 1);
  t3_material_set_texture(W.grate_mat, T3_ENV_MAP, env_texture());
  W.grate_mat->env_map_intensity = 0.4f;
  for (int i = 0; i < 3; i++) {
    t3_material *m = t3_mesh_standard_material_new(0x000000);
    m->color = (t3_color){ 0.02f, 0.02f, 0.02f };
    m->emissive = lin(LAMP[variation][i][0], LAMP[variation][i][1], LAMP[variation][i][2]);
    m->emissive_intensity = 1.2f;
    m->roughness = 0.3f;
    m->metalness = 0;
    W.lamp_mat[i] = m;
    t3_material *h = t3_sprite_material_new(0xffffff);
    h->color = (t3_color){ m->emissive.r * 0.9f, m->emissive.g * 0.9f, m->emissive.b * 0.9f };
    t3_material_set_map(h, W.halo_tex);
    h->blending = T3_ADDITIVE_BLENDING;
    h->depth_write = false;
    W.lamp_halo[i] = h;
  }
  if (variation == 1) {
    W.planet_mat = t3_mesh_standard_material_new(0xffffff);
    t3_texture *pt = planet_texture();
    /* the view takes in a few degrees of the globe: the map repeats so its continents and cloud bands are
     * the size of the screen, not of a hemisphere (its noise is periodic, so the repeat has no seam) */
    pt->repeat = (t3_vec2){ 10, 5 };
    pt->wrap_t = T3_REPEAT;
    t3_material_set_map(W.planet_mat, pt);
    W.planet_mat->roughness = 0.85f;
    W.planet_mat->metalness = 0;
    W.planet_mat->emissive = (t3_color){ 0.012f, 0.018f, 0.04f }; /* the night side keeps a faint blue */
  }
  if (variation == 2) {
    W.lava_mat = t3_mesh_standard_material_new(0x000000);
    W.lava_mat->color = (t3_color){ 0.03f, 0.02f, 0.015f };
    t3_texture *lt = lava_texture(); /* only the foundry uses it: its repeat and offset (the melt flows) are ours */
    lt->repeat = (t3_vec2){ 1, SEG_L / 3.2f };
    t3_material_set_texture(W.lava_mat, T3_EMISSIVE_MAP, lt);
    W.lava_mat->emissive = (t3_color){ 1, 1, 1 };
    W.lava_mat->emissive_intensity = 1.1f; /* a glow under the action, never brighter than the fire over it */
    W.lava_mat->roughness = 0.9f;
    W.lava_mat->metalness = 0;
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

/* a deck lamp's light: a small clustered point light just over the lens,
 * its colour the lens's, breathing with it (world_sync), so each lamp pools
 * its colour on the plating and the machinery round it */
static void lamp_light(t3_object *s, float x, float y, float z, int which, float energy, float range) {
  if (W.lamps >= LAMPS_MAX) return;
  t3_light *l = t3_point_light_new(0xffffff, 0, range, 2);
  l->color = W.lamp_mat[which]->emissive;
  t3_object_set_position(l, x, y, z);
  t3_object_add(s, l);
  t3_release(l);
  W.lamp_light[W.lamps] = l;
  W.lamp_light_k[W.lamps] = energy;
  W.lamp_light_kind[W.lamps++] = which;
}

/* a great round hatch: a raised collar, a chrome rim, a dark well with an
 * iris of plates, a lamp at its heart */
static void hatch(t3_object *s, float x, float z, float r, int lamp) {
  mesh(s, cyl(r * 1.08f, r * 1.18f, 0.7f, 40), W.trim_mat, x, 0.35f, z, true, true);
  mesh(s, torus(r, 0.16f, 40, 10), W.steel_mat, x, 0.72f, z, true, true);
  mesh(s, cyl(r * 0.94f, r * 0.94f, 0.3f, 40), W.dark_mat, x, 0.5f, z, false, true);
  for (int i = 0; i < 6; i++) {
    t3_geometry *g = box(r * 0.5f, 0.12f, r * 0.9f);
    t3_geometry_translate(g, 0, 0, r * 0.4f);
    t3_geometry_rotate_y(g, i * 1.0471976f + 0.3f);
    /* each blade a little above the last, as an iris stacks: blades that met at
     * one height z-fought where they overlap round the lamp */
    mesh(s, g, W.trim_mat, x, 0.7f + i * 0.012f, z, false, true);
  }
  mesh(s, torus(r * 0.13f, 0.035f, 24, 6), W.lamp_mat[lamp], x, 0.74f, z, false, false); /* a lit ring at the heart */
  lamp_light(s, x, 1.2f, z, lamp, 6, r * 1.3f);
  mesh(s, torus(r * 0.3f, 0.06f, 24, 8), W.steel_mat, x, 0.78f, z, false, true);
  t3_sprite *h = t3_sprite_new(W.lamp_halo[lamp]);
  t3_object_set_position(h, x, 1.4f, z);
  t3_object_set_scale(h, r * 0.22f, r * 0.22f, r * 0.22f);
  t3_object_add(s, h);
  t3_release(h);
}

/* a raised plate with a bevelled cap and rivet studs at its corners */
static void plate(t3_object *s, float x, float z, float w, float d, float h) {
  mesh(s, box(w, h, d), W.trim_mat, x, h * 0.5f, z, true, true);
  mesh(s, box(w - 0.3f, 0.12f, d - 0.3f), W.deck_mat, x, h + 0.06f, z, false, true);
  for (int c = 0; c < 4; c++)
    mesh(s, t3_sphere_geometry_new_ex(0.12f, 10, 6, 0, 6.2831853f, 0, 1.5707963f), W.steel_mat, x + (c & 1 ? 1 : -1) * (w * 0.5f - 0.3f),
         h + 0.1f, z + (c & 2 ? 1 : -1) * (d * 0.5f - 0.3f), false, true);
}

/* a conduit along the deck with collars, laid in a shallow channel */
static void conduit(t3_object *s, float x, float r) {
  mesh(s, box(r * 3.2f, 0.25f, SEG_L), W.dark_mat, x, 0.12f, SEG_L * 0.5f, false, true);
  mesh(s, pipe_z(r, SEG_L, 20), W.pipe_mat, x, r + 0.1f, SEG_L * 0.5f, true, true);
  for (int i = 0; i < 4; i++) {
    t3_geometry *g = t3_torus_geometry_new(r * 1.15f, r * 0.22f, 20, 8, 6.2831853f);
    mesh(s, g, W.steel_mat, x, r + 0.1f, 3 + i * 6, false, true);
  }
}

/* a lamp post: a short chrome column with a glowing cap */
static void lamp(t3_object *s, float x, float z, int which) {
  /* a flush lens in a shallow bezel, as the original's deck lights were */
  /* a strip light in a shallow bezel along the deck: long and thin, never a round dot */
  mesh(s, box(0.42f, 0.08f, 1.5f), W.dark_mat, x, 0.04f, z, false, true);
  mesh(s, box(0.16f, 0.03f, 1.2f), W.lamp_mat[which], x, 0.09f, z, false, false);
  lamp_light(s, x, 0.6f, z, which, 5, 5);
  t3_sprite *h = t3_sprite_new(W.lamp_halo[which]);
  t3_object_set_position(h, x, 0.3f, z);
  t3_object_set_scale(h, 0.5f, 0.5f, 0.5f);
  t3_object_add(s, h);
  t3_release(h);
}

/* a pylon: a tall block of machinery near the edges, with a mast */
static void pylon(t3_object *s, float x, float z, float h, uint32_t seed) {
  float w = 2.4f + hf(seed, 1) * 2, d = 2.4f + hf(seed, 2) * 2;
  mesh(s, box(w, h, d), W.trim_mat, x, h * 0.5f, z, true, true);
  mesh(s, box(w * 0.7f, 0.6f, d * 0.7f), W.dark_mat, x, h + 0.3f, z, true, true);
  mesh(s, box(w + 0.2f, 0.3f, d + 0.2f), W.steel_mat, x, h * 0.7f, z, false, true);
  mesh(s, cyl(0.06f, 0.1f, 3, 8), W.steel_mat, x + w * 0.2f, h + 2.1f, z, true, true);
  mesh(s, box(0.12f, 0.5f, 0.12f), W.lamp_mat[0], x + w * 0.2f, h + 3.6f, z, false, false); /* a beacon rod, not a ball */
  lamp_light(s, x + w * 0.2f, h + 3.3f, z, 0, 4, 5);
}

/* a vent bank: a grate set into a frame */
static void vent(t3_object *s, float x, float z, float w, float d) {
  mesh(s, box(w + 0.4f, 0.35f, d + 0.4f), W.trim_mat, x, 0.17f, z, true, true);
  mesh(s, box(w, 0.05f, d), W.grate_mat, x, 0.36f, z, false, true);
}

/* greebles: a scatter of small machinery (housings, junction boxes, valve
 * stubs, tanks) over the plating, in two instanced meshes per segment */
static void greebles(t3_object *s, uint32_t seed) {
  enum { NB = 200, NC = 26 };
  t3_geometry *bg = box(1, 1, 1);
  t3_geometry_translate(bg, 0, 0.5f, 0);
  t3_instanced_mesh *ib = t3_instanced_mesh_new(bg, W.trim_mat, NB);
  t3_release(bg);
  t3_geometry *cg = cyl(0.5f, 0.5f, 1, 14);
  t3_geometry_translate(cg, 0, 0.5f, 0);
  t3_instanced_mesh *ic = t3_instanced_mesh_new(cg, W.trim_mat, NC);
  t3_release(cg);
  for (int i = 0; i < NB + NC; i++) {
    float side = hf(seed, 200 + i) < 0.5f ? -1 : 1;
    float x = side * (6.5f + 24 * hf(seed, 300 + i)), z = SEG_L * hf(seed, 400 + i);
    x = floorf(x * 2 + 0.5f) * 0.5f, z = floorf(z * 2 + 0.5f) * 0.5f; /* laid out on the plating's half-unit grid */
    float k = hf(seed, 500 + i);
    t3_mat4 m;
    if (i < NB) {
      float w = 0.3f + 1.4f * k * k, d = 0.3f + 1.8f * hf(seed, 600 + i), h = 0.12f + 0.7f * hf(seed, 700 + i) * hf(seed, 800 + i);
      float yaw = hf(seed, 900 + i) < 0.5f ? 0 : 1.5707963f;
      t3_mat4_compose(&m, (t3_vec3){ x, 0, z }, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, yaw), (t3_vec3){ w, h, d });
      t3_instanced_mesh_set_matrix_at(ib, i, &m);
    } else {
      float r = 0.15f + 0.6f * k * k, h = 0.2f + 1.2f * hf(seed, 1000 + i);
      t3_mat4_compose(&m, (t3_vec3){ x, 0, z }, t3_quat_identity(), (t3_vec3){ r * 2, h, r * 2 });
      t3_instanced_mesh_set_matrix_at(ic, i - NB, &m);
    }
  }
  T3_OBJ(ib)->cast_shadow = T3_OBJ(ib)->receive_shadow = true;
  T3_OBJ(ic)->cast_shadow = T3_OBJ(ic)->receive_shadow = true;
  t3_object_add(s, ib);
  t3_object_add(s, ic);
  t3_release(ib);
  t3_release(ic);
}

/* the orbital ring: four box trusses running down the screen, cross beams every 12 units with a few plated bays on
 * them, beacon bars on the top chords, and the planet far below seen through the gaps. The truss members are one
 * instanced mesh per segment. */
static void truss_member(t3_instanced_mesh *im, int *n, float x, float y, float z, float sx, float sy, float sz, float rx) {
  t3_mat4 m;
  t3_mat4_compose(&m, (t3_vec3){ x, y, z }, t3_quat_from_axis_angle((t3_vec3){ 1, 0, 0 }, rx), (t3_vec3){ sx, sy, sz });
  t3_instanced_mesh_set_matrix_at(im, (*n)++, &m);
}
static void build_orbital(t3_object *s, int layout, uint32_t seed) {
  enum { MAXM = 240 };
  t3_geometry *bg = box(1, 1, 1);
  t3_instanced_mesh *im = t3_instanced_mesh_new(bg, W.trim_mat, MAXM);
  t3_release(bg);
  int n = 0;
  static const float RX[4] = { -21, -8, 8, 21 };
  const float H = 1.8f, STEP = 3;
  float diag = sqrtf(H * H + STEP * STEP), ang = atan2f(H, STEP);
  for (int r = 0; r < 4; r++) {
    float x = RX[r];
    truss_member(im, &n, x, H, SEG_L * 0.5f, 0.55f, 0.4f, SEG_L, 0);  /* top chord */
    truss_member(im, &n, x, 0, SEG_L * 0.5f, 0.7f, 0.5f, SEG_L, 0);   /* bottom chord */
    for (int k = 0; k < (int)(SEG_L / STEP); k++) {
      float z = k * STEP;
      truss_member(im, &n, x, H * 0.5f, z, 0.22f, H, 0.22f, 0);                                       /* post */
      truss_member(im, &n, x, H * 0.5f, z + STEP * 0.5f, 0.16f, 0.16f, diag, (k & 1) ? ang : -ang); /* diagonal */
    }
  }
  /* cross beams joining the trusses, out past the edges */
  for (int c = 0; c < 2; c++) {
    float z = c * SEG_L * 0.5f + 1;
    truss_member(im, &n, 0, H * 0.5f, z, 50, 0.6f, 0.9f, 0);
    truss_member(im, &n, 0, H * 0.5f - 0.7f, z, 50, 0.25f, 0.5f, 0);
  }
  im->count = n;
  T3_OBJ(im)->cast_shadow = T3_OBJ(im)->receive_shadow = true;
  t3_object_add(s, im);
  t3_release(im);
  /* plated bays resting on the cross beams, with machinery on them (two a segment, seeded) */
  for (int b = 0; b < 2; b++) {
    float side = hf(seed, 80 + b) < 0.5f ? -1 : 1;
    float x = side * (hf(seed, 82 + b) < 0.5f ? 14.5f : 1.5f + 3 * hf(seed, 84 + b));
    float z = b * SEG_L * 0.5f + 1;
    plate(s, x, z, 7, 4.5f, H + 0.35f);
    if (hf(seed, 86 + b) > 0.5f) vent(s, x, z, 3.5f, 2.2f);
  }
  /* a dish or a mast on the outer trusses */
  for (int side = -1; side <= 1; side += 2) {
    float z = SEG_L * (0.25f + 0.5f * hf(seed, 90 + side));
    mesh(s, cyl(0.08f, 0.12f, 4, 8), W.steel_mat, side * 21, H + 2, z, true, true);
    mesh(s, box(0.14f, 0.7f, 0.14f), W.lamp_mat[layout % 3], side * 21, H + 4.3f, z, false, false);
    lamp_light(s, side * 21, H + 4, z, layout % 3, 4, 5);
    /* beacon bars along the inner trusses' top chords */
    mesh(s, box(0.16f, 0.05f, 1.6f), W.lamp_mat[(layout + 1) % 3], side * 8, H + 0.23f, SEG_L * 0.5f, false, false);
    lamp_light(s, side * 8, H + 0.8f, SEG_L * 0.5f, (layout + 1) % 3, 4, 5);
  }
}

/* the foundry: a channel of melt in place of a conduit, walled in plating, lit by its own glow */
static void melt_channel(t3_object *s, float x) {
  const float w = 3.0f;
  for (int e = -1; e <= 1; e += 2) mesh(s, box(0.5f, 0.6f, SEG_L), W.trim_mat, x + e * (w * 0.5f + 0.25f), 0.3f, SEG_L * 0.5f, true, true);
  t3_geometry *g = t3_plane_geometry_new(w, SEG_L, 1, 1);
  t3_geometry_rotate_x(g, -1.5707963f);
  mesh(s, g, W.lava_mat, x, 0.04f, SEG_L * 0.5f, false, false);
  for (int k = 0; k < 3 && W.lava_lights < SEGS * 6; k++) {
    t3_light *l = t3_point_light_new(0xff6a20, 0, 9, 2);
    l->color = (t3_color){ 1, 0.38f, 0.08f };
    t3_object_set_position(l, x, 1.2f, SEG_L * (k + 0.5f) / 3);
    t3_object_add(s, l);
    t3_release(l);
    W.lava_light_k[W.lava_lights] = 5 + 1.5f * (float)k;
    W.lava_light[W.lava_lights++] = l;
  }
}

/* a piece of the deck plate, its texture continuing as if the plate were whole */
static void deck_piece(t3_object *s, float x0, float x1, float z0, float z1) {
  t3_geometry *g = t3_plane_geometry_new(x1 - x0, z1 - z0, 1, 1);
  t3_geometry_rotate_x(g, -1.5707963f);
  t3_attribute *uv = g->attributes[T3_ATTR_UV];
  float *a = uv->array;
  for (int i = 0; i < uv->count; i++) {
    /* the whole plate runs u 0..1 over x -DECK_W/2..DECK_W/2 and v 1..0 over z 0..SEG_L */
    float px = a[i * 2] < 0.5f ? x0 : x1, pz = a[i * 2 + 1] > 0.5f ? z0 : z1;
    a[i * 2] = (px + DECK_W * 0.5f) / DECK_W;
    a[i * 2 + 1] = 1 - pz / SEG_L;
  }
  mesh(s, g, W.deck_mat, (x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f, false, true);
}

#define PIT_X 5.0f
#define PIT_Z0 5.0f
#define PIT_Z1 19.0f
#define PIT_D 10.0f
static void pit(t3_object *s, int seg) {
  float w = PIT_X * 2, l = PIT_Z1 - PIT_Z0, zc = (PIT_Z0 + PIT_Z1) * 0.5f;
  /* the walls: plating down to the floor, a chrome lip round the top */
  for (int e = -1; e <= 1; e += 2) {
    mesh(s, box(0.6f, PIT_D, l + 1.2f), W.trim_mat, e * (PIT_X + 0.3f), -PIT_D * 0.5f, zc, true, true);
    mesh(s, box(w, PIT_D, 0.6f), W.trim_mat, 0, -PIT_D * 0.5f, e < 0 ? PIT_Z0 - 0.3f : PIT_Z1 + 0.3f, true, true);
    mesh(s, box(0.3f, 0.25f, l + 1.2f), W.steel_mat, e * (PIT_X + 0.3f), 0.12f, zc, false, true);
    mesh(s, box(w + 1.2f, 0.25f, 0.3f), W.steel_mat, 0, 0.12f, e < 0 ? PIT_Z0 - 0.3f : PIT_Z1 + 0.3f, false, true);
  }
  /* the floor and its machinery: two great pipes across, a turbine housing, struts up the walls */
  t3_geometry *g = t3_plane_geometry_new(w, l, 1, 1);
  t3_geometry_rotate_x(g, -1.5707963f);
  mesh(s, g, W.dark_mat, 0, -PIT_D, zc, false, true);
  for (int k = 0; k < 2; k++) {
    t3_geometry *p = cyl(0.7f, 0.7f, w, 20);
    t3_geometry_rotate_z(p, 1.5707963f);
    mesh(s, p, W.pipe_mat, 0, -PIT_D + 0.7f, PIT_Z0 + 3 + k * 7, true, true);
  }
  mesh(s, cyl(2.0f, 2.2f, 2.4f, 28), W.trim_mat, -1.5f, -PIT_D + 1.2f, zc, true, true);
  mesh(s, torus(2.1f, 0.18f, 28, 8), W.steel_mat, -1.5f, -PIT_D + 2.45f, zc, false, true);
  for (int k = 0; k < 4; k++) mesh(s, box(0.25f, PIT_D, 0.25f), W.steel_mat, (k & 1 ? 1 : -1) * (PIT_X - 0.2f), -PIT_D * 0.5f, PIT_Z0 + 1 + (k / 2) * (l - 2), true, true);
  if (W.pits >= SEGS) return;
  /* the work light: warm, it sweeps slowly up and down the pit (world_sync) */
  t3_light *wl = t3_point_light_new(0xffc080, 0, 14, 2);
  wl->color = (t3_color){ 1, 0.72f, 0.42f };
  wl->intensity = 26;
  t3_object_set_position(wl, 0, -PIT_D + 4, zc);
  t3_object_add(s, wl);
  t3_release(wl);
  W.pit_light[W.pits] = wl;
  W.pit_seg[W.pits] = seg;
  /* haze filling the pit: the engine draws it on Medium and up (plain fog below) */
  char nm[16];
  snprintf(nm, sizeof nm, "PitFog%d", W.pits);
  cw_node fog = cw_node_new("FogVolume3D", nm);
  cw_node_set(fog, "size", cw_val_vec3(w, PIT_D, l));
  cw_node_set(fog, "density", cw_val_float(0.22f));
  cw_node_set(fog, "albedo", cw_val_color(0.9f, 0.75f, 0.6f, 1));
  cw_node_set(fog, "height_falloff", cw_val_float(0.25f));
  cw_node_set(fog, "noise", cw_val_float(0.6f));
  cw_node_set(fog, "noise_scale", cw_val_float(0.35f));
  cw_node_set(fog, "noise_wind", cw_val_vec3(0.3f, 0.1f, 0.5f));
  cw_node_add_child(W.node, fog);
  W.pit_fog[W.pits++] = fog;
}

static void build_segment(t3_object *s, int layout, int variation) {
  uint32_t seed = (uint32_t)layout * 977u + (uint32_t)variation * 31u + 7u;
  if (variation == 1) {
    build_orbital(s, layout, seed);
    return;
  }
  /* the deck plate itself; the centre a hatch, or (every third segment) a pit down to the machinery under the
   * deck, or a vent bank between plates */
  bool has_pit = layout % 3 == 1;
  if (has_pit) {
    deck_piece(s, -DECK_W * 0.5f, -PIT_X, 0, SEG_L);
    deck_piece(s, PIT_X, DECK_W * 0.5f, 0, SEG_L);
    deck_piece(s, -PIT_X, PIT_X, 0, PIT_Z0);
    deck_piece(s, -PIT_X, PIT_X, PIT_Z1, SEG_L);
    pit(s, layout);
  } else {
    t3_geometry *g = t3_plane_geometry_new(DECK_W, SEG_L, 1, 1);
    t3_geometry_rotate_x(g, -1.5707963f);
    mesh(s, g, W.deck_mat, 0, 0, SEG_L * 0.5f, false, true);
    if (layout % 2 == 0) hatch(s, 0, SEG_L * 0.5f, 4 + hf(seed, 3) * 1.5f, layout % 3);
    else {
      vent(s, 0, SEG_L * 0.3f, 5, 3);
      vent(s, 0, SEG_L * 0.72f, 5, 3);
      plate(s, 0, SEG_L * 0.5f, 7, 2.6f, 0.6f);
    }
  }
  /* mirrored machinery out to the edges */
  for (int side = -1; side <= 1; side += 2) {
    if (variation == 2) melt_channel(s, side * (9 + hf(seed, 10)));
    else conduit(s, side * (9 + hf(seed, 10)), 0.42f);
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
  greebles(s, seed);
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
  if (variation == 1) {
    /* the planet: far under the ring, its curve filling the view below the girders */
    t3_geometry *g = t3_sphere_geometry_new(170, 96, 48);
    W.planet = t3_mesh_new(g, W.planet_mat);
    t3_release(g);
    t3_object_set_position(W.planet, 0, GROUND_Y - 235, -40);
    T3_OBJ(W.planet)->receive_shadow = false;
    t3_object_add(W.root, W.planet);
    t3_release(W.planet);
  }
}

/* ── craft, fire, power-ups ────────────────────────────────────────── */

/* the craft whose red rings charge before they fire (the original's
 * preFire glow) get rings of their own */
static bool charges(int kind) { return kind == CK_STRAIGHT || kind == CK_RAYGUN || kind == CK_TANK || kind == CK_BOSS0 || kind == CK_BOSS1; }
static void own_glow(t3_object *o, t3_material *g) {
  for (int i = 0; i < o->child_count; i++) {
    t3_object *c = o->children[i];
    if (c->type == T3_MESH) {
      t3_mesh *m = (t3_mesh *)c;
      if (m->materials[0] == model_mat(M_RED_GLOW)) {
        t3_retain(g);
        t3_release(m->materials[0]);
        m->materials[0] = g;
      }
    }
    own_glow(c, g);
  }
}

static t3_object *pool_get(int kind) {
  pool *p = &W.pools[kind];
  if (p->used == p->n) {
    if (p->n == POOL_MAX) return NULL;
    p->obj[p->n] = model_new(kind, W.play, p->spin[p->n], &p->nspin[p->n]);
    if (charges(kind)) {
      p->glow[p->n] = t3_material_clone(model_mat(M_RED_GLOW));
      own_glow(p->obj[p->n], p->glow[p->n]);
    }
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

static t3_instanced_mesh *bolt_mesh(t3_geometry *g, t3_material *m, int cap) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, cap);
  t3_release(g);
  t3_release(m);
  im->count = 0;
  T3_OBJ(im)->frustum_culled = false;
  t3_object_add(W.play, im);
  t3_release(im);
  return im;
}

static void build_fire(void) {
  /* danger reads first: enemy rounds are the most saturated, brightest things on screen, magenta (heavies,
   * flak, needles) and cyan (slugs, the ray gun's beam), each with a white-hot core and a soft halo; the
   * ship's own fire is dimmer and thinner (pale blue bolts, the green lance, the violet hammers) so it never
   * hides what is coming down */
  static const float C[BOLT_KINDS][4] = {
    { 0.3f, 0.5f, 1, 0.75f }, { 0.1f, 1, 0.2f, 0.9f }, { 0.55f, 0.15f, 1, 1.0f }, { 1, 0.04f, 0.7f, 2.6f },
    { 1, 0.08f, 0.5f, 2.4f }, { 0.02f, 0.9f, 1, 2.6f }, { 0.02f, 0.85f, 1, 2.8f }, { 1, 0.04f, 0.8f, 2.6f },
  };
  for (int k = 0; k < BOLT_KINDS; k++) {
    W.bolts[k] = bolt_mesh(t3_sphere_geometry_new(1, 12, 8), bolt_mat(C[k][0], C[k][1], C[k][2], C[k][3]), BOLT_CAP[k]);
    if (k < BK_HEAVY) continue;
    W.cores[k] = bolt_mesh(t3_sphere_geometry_new(1, 10, 6), bolt_mat(1, 1, 1, 2.2f), BOLT_CAP[k]);
    t3_geometry *pg = t3_plane_geometry_new(1, 1, 1, 1);
    t3_geometry_rotate_x(pg, -1.5707963f);
    t3_material *gm = bolt_mat(C[k][0], C[k][1], C[k][2], 0.9f);
    t3_material_set_map(gm, W.halo_tex);
    W.glows[k] = bolt_mesh(pg, gm, BOLT_CAP[k]);
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
  /* a core shape per kind: shields a smooth orb, super shields a gem,
   * repair a double pyramid, the stocks a faceted ball */
  W.pu_geo[PU_SHIELDS] = t3_sphere_geometry_new(0.3f, 24, 16);
  W.pu_geo[PU_SUPER] = t3_dodecahedron_geometry_new(0.34f, 0);
  W.pu_geo[PU_REPAIR] = t3_octahedron_geometry_new(0.36f, 0);
  for (int k = PU_AMMO0; k <= PU_AMMO2; k++) W.pu_geo[k] = t3_icosahedron_geometry_new(0.3f, 0);
  for (int i = 0; i < MAX_POWERUPS; i++) {
    W.pu[i] = t3_group_new();
    t3_geometry *rg = t3_torus_geometry_new(0.55f, 0.07f, 32, 8, 6.2831853f);
    t3_mesh *r = t3_mesh_new(rg, W.pu_mat[0]);
    t3_release(rg);
    t3_object_add(W.pu[i], r);
    t3_release(r);
    W.pu_ring[i] = T3_OBJ(r);
    t3_mesh *c = t3_mesh_new(W.pu_geo[0], W.pu_core_mat[0]);
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
  W.hero_flames = ns;
  W.shield_mat = t3_mesh_basic_material_new(0xffffff);
  W.shield_mat->color = (t3_color){ 0.2f, 0.45f, 1.2f };
  W.shield_mat->blending = T3_ADDITIVE_BLENDING;
  W.shield_mat->transparent = true;
  W.shield_mat->depth_write = false;
  W.shield_mat->opacity = 0;
  t3_geometry *sg = t3_icosahedron_geometry_new(0.95f, 3);
  W.shield = t3_mesh_new(sg, W.shield_mat);
  t3_release(sg);
  t3_object_add(W.hero, W.shield);
  t3_release(W.shield);
  /* the hit point: what a round must touch (the sim's 0.775 diamond is the ship's middle), shown as the
   * genre does, a white dot ringed in dark, drawn on top of the hull */
  for (int i = 0; i < 2; i++) {
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = i ? (t3_color){ 2.2f, 2.2f, 2.4f } : (t3_color){ 0.01f, 0.02f, 0.05f };
    m->depth_test = false;
    m->depth_write = false;
    m->transparent = true;
    m->opacity = i ? 1 : 0.8f;
    W.hitbox_mat[i] = m;
  }
  t3_geometry *rg = t3_circle_geometry_new(0.2f, 24);
  t3_geometry_rotate_x(rg, -1.5707963f);
  W.hitbox = t3_mesh_new(rg, W.hitbox_mat[0]);
  t3_release(rg);
  T3_OBJ(W.hitbox)->render_order = 50;
  t3_object_set_position(W.hitbox, 0, 0.6f, 0);
  t3_object_add(W.hero, W.hitbox);
  t3_release(W.hitbox);
  t3_geometry *dg = t3_circle_geometry_new(0.12f, 20);
  t3_geometry_rotate_x(dg, -1.5707963f);
  t3_mesh *dot = t3_mesh_new(dg, W.hitbox_mat[1]);
  t3_release(dg);
  T3_OBJ(dot)->render_order = 51;
  t3_object_set_position(dot, 0, 0.01f, 0);
  t3_object_add(T3_OBJ(W.hitbox), dot);
  t3_release(dot);
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

static void build_chips(void) {
  t3_geometry *g = t3_octahedron_geometry_new(0.2f, 0);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ 3.2f, 2.2f, 0.5f };
  m->blending = T3_ADDITIVE_BLENDING;
  m->transparent = true;
  m->depth_write = false;
  W.chips = t3_instanced_mesh_new(g, m, MAX_CHIPS);
  t3_release(g);
  t3_release(m);
  W.chips->count = 0;
  T3_OBJ(W.chips)->frustum_culled = false;
  T3_OBJ(W.chips)->visible = false;
  t3_object_add(W.play, W.chips);
  t3_release(W.chips);
}

/* this tick's cancelled rounds become chips (called from the fixed tick, after sim_step) */
void world_chips_spawn(const sim *S) {
  if (!W.chips) return;
  for (int i = 0; i < S->chips_n; i++) {
    int k = W.chip_next;
    W.chip_next = (k + 1) % MAX_CHIPS;
    uint32_t hsh = hash((uint32_t)S->tick, (uint32_t)i);
    float a = (hsh & 1023) / 1023.0f * 6.2831853f;
    W.chip_x[k] = S->chip_x[i], W.chip_y[k] = S->chip_y[i];
    W.chip_vx[k] = cosf(a) * 3, W.chip_vy[k] = sinf(a) * 3 + 2; /* a little pop outward before they home in */
    W.chip_age[k] = 0;
    W.chip_live[k] = 1;
  }
}

static void chips_sync(const sim *cur, float t) {
  float dt = W.chip_t > 0 ? cw_clampf(t - W.chip_t, 0, 0.1f) : 0;
  W.chip_t = t;
  int n = 0;
  float hx = cur->h.x, hy = cur->h.y;
  for (int k = 0; k < MAX_CHIPS; k++) {
    if (!W.chip_live[k]) continue;
    W.chip_age[k] += dt;
    float age = W.chip_age[k];
    /* the pop slows, then a pull toward the ship that grows with age */
    float pull = age < 0.2f ? 0 : (age - 0.2f) * 60;
    float dx = hx - W.chip_x[k], dy = hy - W.chip_y[k], d = sqrtf(dx * dx + dy * dy) + 1e-3f;
    W.chip_vx[k] = W.chip_vx[k] * (1 - 4 * dt) + dx / d * pull * dt;
    W.chip_vy[k] = W.chip_vy[k] * (1 - 4 * dt) + dy / d * pull * dt;
    W.chip_x[k] += W.chip_vx[k] * dt, W.chip_y[k] += W.chip_vy[k] * dt;
    if ((age > 0.2f && d < 0.6f) || age > 1.6f) {
      W.chip_live[k] = 0;
      continue;
    }
    t3_mat4 m;
    t3_vec3 p = to_world(W.chip_x[k], W.chip_y[k]);
    float sc = age < 0.08f ? age / 0.08f : 1; /* each grows in over a few frames: no pop of light */
    t3_mat4_compose(&m, p, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, age * 9 + k), (t3_vec3){ sc, sc * 1.4f, sc });
    t3_instanced_mesh_set_matrix_at(W.chips, n++, &m);
  }
  W.chips->count = n;
  if (n) W.chips->instance_matrix->version++;
  T3_OBJ(W.chips)->visible = n > 0;
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
  build_chips();
  logo_build(W.root, W.halo_tex);
  hud3d_build(W.root);
  probe_build(W.node, W.root);
  cw_node3d_set_object(W.node, W.root);
  t3_release(W.root);
}

t3_texture *world_halo_texture(void) { return W.halo_tex; }

void world_free(void) {
  probe_free();
  for (int k = 0; k < CRAFT_KINDS; k++)
    for (int i = 0; i < W.pools[k].n; i++)
      if (W.pools[k].glow[i]) t3_release(W.pools[k].glow[i]);
  logo_free();
  hud3d_free();
  if (cw_node_valid(W.node)) cw_node_free(W.node);
  for (int i = 0; i < 2; i++)
    if (W.hitbox_mat[i]) t3_release(W.hitbox_mat[i]);
  if (W.planet_mat) t3_release(W.planet_mat);
  if (W.lava_mat) t3_release(W.lava_mat);
  t3_material *mats[] = { W.deck_mat, W.trim_mat, W.dark_mat, W.grate_mat, W.lamp_mat[0], W.lamp_mat[1], W.lamp_mat[2], W.lamp_halo[0], W.lamp_halo[1], W.lamp_halo[2], W.pipe_mat, W.steel_mat, W.shield_mat, W.bomb_mat, W.warn_mat };
  for (size_t i = 0; i < sizeof mats / sizeof mats[0]; i++)
    if (mats[i]) t3_release(mats[i]);
  for (int k = 0; k < PU_KINDS; k++) {
    if (W.pu_mat[k]) t3_release(W.pu_mat[k]);
    if (W.pu_core_mat[k]) t3_release(W.pu_core_mat[k]);
    if (W.halo_mat[k]) t3_release(W.halo_mat[k]);
    if (W.pu_geo[k]) t3_release(W.pu_geo[k]);
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
/* an enemy round: its colour, a white core inside, a halo under it (one count for the three meshes) */
static void set_round(int k, int *n, float x, float y, float sx, float sz) {
  if (*n >= BOLT_CAP[k]) return;
  t3_mat4 m;
  t3_vec3 p = to_world(x, y);
  t3_mat4_compose(&m, p, t3_quat_identity(), (t3_vec3){ sx * 0.5f, sx * 0.5f, sz * 0.6f });
  t3_instanced_mesh_set_matrix_at(W.cores[k], *n, &m);
  float g = (sx > sz ? sx : sz) * 5;
  t3_mat4_compose(&m, (t3_vec3){ p.x, p.y - 0.05f, p.z }, t3_quat_identity(), (t3_vec3){ sx * 5 + g * 0.3f, 1, sz * 2.4f + g * 0.3f });
  t3_instanced_mesh_set_matrix_at(W.glows[k], *n, &m);
  set_bolt(W.bolts[k], n, BOLT_CAP[k], x, y, sx, sz);
}

/* a mesh drawn with another material (the mesh keeps a reference) */
static void swap_mat(t3_mesh *m, t3_material *mat) {
  t3_retain(mat);
  t3_release(m->materials[0]);
  m->materials[0] = mat;
}

static int bolt_kind_e(int ammo) { return BK_HEAVY + ammo; }

void world_sync(const sim *prev, const sim *cur, float a, float t, int state) {
  if (!W.root) return;
  /* the deck scrolls on the level's clock */
  float travel = lerp(prev->frame, cur->frame, a) / 50.0f * SCROLL;
  probe_sync(travel, state == 0);
  for (int i = 0; i < SEGS; i++) {
    float z = travel - i * SEG_L;
    float span = SEGS * SEG_L, lo = -100;
    z = z - span * floorf((z - lo) / span);
    t3_object_set_position(W.seg[i], 0, 0, z);
  }
  for (int i = 0; i < W.pits; i++) {
    const t3_vec3 sp = T3_OBJ(W.seg[W.pit_seg[i]])->position;
    cw_node3d_set_position(W.pit_fog[i], (t3_vec3){ 0, GROUND_Y - PIT_D * 0.5f, sp.z + (PIT_Z0 + PIT_Z1) * 0.5f });
    t3_object_set_position(W.pit_light[i], 3 * sinf(t * 0.4f + i), -PIT_D + 4, (PIT_Z0 + PIT_Z1) * 0.5f + 5 * sinf(t * 0.25f + i * 2));
  }
  if (W.planet) t3_object_set_rotation(W.planet, 0.35f, 0, travel * -0.004f); /* it turns under the ring as the ring flies on */
  if (W.lava_mat) {
    t3_texture *lt = W.lava_mat->emissive_map;
    if (lt) lt->offset.y = -travel * 0.02f - t * 0.03f; /* the melt creeps along its channel */
    for (int i = 0; i < W.lava_lights; i++) W.lava_light[i]->intensity = W.lava_light_k[i] * (0.85f + 0.15f * sinf(t * 1.1f + i * 1.7f));
  }
  /* lamps breathe, as the original's ground lights did */
  for (int i = 0; i < 3; i++) W.lamp_mat[i]->emissive_intensity = 0.9f + 0.4f * (0.5f + 0.5f * sinf(t * (1.3f + i * 0.7f) + i * 2));
  for (int i = 0; i < W.lamps; i++) W.lamp_light[i]->intensity = W.lamp_light_k[i] * W.lamp_mat[W.lamp_light_kind[i]]->emissive_intensity;

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
    /* a boss's rim lobes are its armour: models.c builds them as parts 1..6 of the inner group */
    if (e->kind >= EK_BOSS0 && o->child_count > 0) {
      t3_object *in = o->children[0];
      for (int k = 0; k < BOSS_PLATES && 1 + k < in->child_count; k++) in->children[1 + k]->visible = k < e->plates;
    }
    if (p->glow[slot]) p->glow[slot]->emissive_intensity = 3 + 14 * cw_clampf(e->pre_fire, 0, 1) + 20 * cw_clampf(e->hit_flash, 0, 1);
    for (int k = 0; k < p->nspin[slot]; k++) t3_object_set_rotation(p->spin[slot][k], p->spin[slot][k]->rotation.x, age * (e->kind == EK_GNAT ? 9 : 3) + e->phase, p->spin[slot][k]->rotation.z);
  }

  /* the hero */
  const hero *h = &cur->h, *ph = &prev->h;
  bool shown = cur->h.dont_show <= 0 && cur->mode != PLAY_DEAD;
  W.hero->visible = shown;
  T3_OBJ(W.hitbox)->visible = state != 0; /* the hit point is for play, not the title */
  if (shown) {
    float x = lerp(ph->x, h->x, a), y = lerp(ph->y, h->y, a);
    t3_vec3 w = to_world(x, y);
    t3_object_set_position(W.hero, w.x, w.y, w.z);
    float vx = h->x - ph->x;
    t3_object_set_rotation(W.hero, 0.05f, 0, cw_clampf(-vx * 4, -0.5f, 0.5f));
    float super = h->shields > HERO_SHIELDS ? (h->shields - HERO_SHIELDS) / HERO_SHIELDS : 0;
    W.shield_mat->opacity = cw_clampf(h->shield_flash * 0.5f + super * (0.12f + 0.05f * sinf(t * 9)), 0, 1);
    T3_OBJ(W.shield)->visible = W.shield_mat->opacity > 0.01f;
    float heat = h->engine_heat;
    W.engine->color = (t3_color){ lerp(0.5f, 1, 1 - heat), lerp(0.25f, 0.65f, heat), lerp(0.1f, 1, heat) };
    W.engine->intensity = 6 + 2 * sinf(t * 40);
    /* the flames: long and blue when healthy, short and ragged when hurt */
    for (int i = 0; i < W.hero_flames; i++) {
      float fl = (0.75f + 0.25f * sinf(t * 53 + i * 2.1f) * sinf(t * 31 + i)) * (0.55f + 0.45f * heat);
      t3_object_set_scale(W.hero_spin[i], 1 + 0.15f * sinf(t * 47 + i), 1, fl);
    }
  }

  /* fire */
  int n[BOLT_KINDS] = { 0 };
  for (int i = 0; i < MAX_HBULLETS; i++) {
    const hbullet *b = &cur->hb[i];
    if (!b->live) continue;
    float y = b->y - b->vy * K * (1 - a);
    if (b->gun == GUN_DEFAULT) set_bolt(W.bolts[BK_HERO0], &n[BK_HERO0], BOLT_CAP[BK_HERO0], b->x, y, 0.032f, 0.7f);
    else if (b->gun == GUN_GREEN) set_bolt(W.bolts[BK_HERO1], &n[BK_HERO1], BOLT_CAP[BK_HERO1], b->x, y, 0.06f + 0.01f * sinf(t * 9 + i), 1.6f);
    else set_bolt(W.bolts[BK_HERO2], &n[BK_HERO2], BOLT_CAP[BK_HERO2], b->x, y, 0.13f, 0.75f);
  }
  static const float ES[EA_KINDS][2] = { { 0.18f, 0.5f }, { 0.16f, 0.16f }, { 0.3f, 0.3f }, { 0.14f, 0.5f }, { 0.09f, 0.42f } };
  for (int i = 0; i < MAX_EBULLETS; i++) {
    const ebullet *b = &cur->eb[i];
    if (!b->live) continue;
    float x = b->x - b->vx * K * (1 - a), y = b->y - b->vy * K * (1 - a);
    int k = bolt_kind_e(b->kind);
    set_round(k, &n[k], x, y, ES[b->kind][0], ES[b->kind][1]);
  }
  for (int k = 0; k < BOLT_KINDS; k++) {
    t3_instanced_mesh *ms[3] = { W.bolts[k], W.cores[k], W.glows[k] };
    for (int j = 0; j < 3; j++) {
      if (!ms[j]) continue;
      ms[j]->count = n[k];
      if (n[k]) ms[j]->instance_matrix->version++;
      T3_OBJ(ms[j])->visible = n[k] > 0;
    }
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
      t3_retain(W.pu_geo[p->kind]);
      t3_release(core->geometry);
      core->geometry = W.pu_geo[p->kind];
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
  chips_sync(cur, t);
  W.warn_mat->opacity = cw_clampf(cur->past_warn, 0, 1) * (0.55f + 0.45f * sinf(t * 35 / 3));
  T3_OBJ(W.warn)->visible = W.warn_mat->opacity > 0.01f;
}

/* the camera: high over the play plane, tipped forward a little so the
 * craft show their flanks and the deck below runs away in depth */
void world_camera(cw_node cam, const sim *S, float t, int state) {
  float target = state == 0 ? 0 : S->h.x * CAM_FOLLOW;
  W.cam_x += (target - W.cam_x) * 0.05f;
  float zoom = S->mode == PLAY_DEAD ? (50 - S->hero_death) / 50 : 0;
  if (zoom < 0) zoom = 0;
  if (zoom > 1) zoom = 1;
  zoom = zoom * zoom;
  t3_vec3 eye = { W.cam_x, CAM_H - 6 * zoom, CAM_Z };
  t3_vec3 at = { W.cam_x * 0.5f, 0, CAM_AT_Z };
  bool hud = state != 0;
#ifdef CHROMITE_AUTOPILOT
  /* art aid (the self-flying build only): CHROMITE_NO_HUD hides the 3D instruments, CHROMITE_EYE=x,y,z,tx,ty,tz holds the camera */
  if (getenv("CHROMITE_NO_HUD")) hud = false;
  const char *ev = getenv("CHROMITE_EYE");
  if (ev && sscanf(ev, "%f,%f,%f,%f,%f,%f", &eye.x, &eye.y, &eye.z, &at.x, &at.y, &at.z) == 6 && getenv("CHROMITE_EYE_FOLLOW")) {
    float f = (float)atof(getenv("CHROMITE_EYE_FOLLOW")) * S->h.x; /* the held camera slides sideways with the ship */
    eye.x += f, at.x += f;
  }
#endif
  cw_node3d_set_position(cam, eye);
  cw_node3d_look_at(cam, at, (t3_vec3){ 0, 1, 0 });
  logo_sync(state == 0, t, eye, at, W.bomb_light);
  int ww, wh;
  cw_window_size(&ww, &wh);
  hud3d_sync(S, t, eye, at, CAM_FOV, wh > 0 ? (float)ww / (float)wh : 16.0f / 9, hud);
}
