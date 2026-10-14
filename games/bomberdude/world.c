/* bomberdude's arena in 3D: the themed floor, rim walls, pillars and
 * crates as instanced meshes, the floor features (ice, vents, glyphs,
 * belts, sand, cannons), the critters, bombs and power-ups. It reads the
 * simulation on the fixed tick (so every host draws the same frame) and
 * never writes it.
 *
 * The critters are animated here: a waddle with squash and stretch, blinks,
 * a squash when they drop a bomb, a victory hop, and a knockout tumble: the
 * body flies off the blast as a spinning rigid body that bounces on the
 * floor while its head, ears, paws and feet flop on damped springs. */
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

#define MAXI (GW * GH)
#define MAX_VENTS 32
#define MAX_CANNONS (GW * 2 + GH * 2)

typedef struct look {
  int floor_tex, wall_tex, pillar_tex, crate_tex;
  float floor_rgb[3], floor_alt[3], wall_rgb[3], pillar_rgb[3], crate_rgb[3];
  float wall_h, pillar_h, wall_rough, crate_cc;
} look;

static const look LOOKS[TH_KINDS] = {
  [TH_MEADOW] = { TEX_GRASS, TEX_BRICK, TEX_BRICK, TEX_CRATE, { 1, 1, 1 }, { 0.86f, 0.95f, 0.8f }, { 1, 0.97f, 0.92f },
                  { 0.92f, 0.9f, 0.86f }, { 1, 0.95f, 0.88f }, 0.9f, 1.0f, 0.9f, 0 },
  [TH_FROST] = { TEX_SNOW, TEX_BRICK, TEX_ICE, TEX_CRATE, { 1, 1, 1 }, { 0.9f, 0.94f, 1 }, { 0.82f, 0.87f, 0.95f },
                 { 0.75f, 0.9f, 1 }, { 0.85f, 0.9f, 1 }, 0.9f, 1.0f, 0.8f, 0.3f },
  [TH_FORGE] = { TEX_BASALT, TEX_METAL, TEX_METAL, TEX_CRATE, { 1.2f, 1.1f, 1.05f }, { 1.05f, 0.98f, 0.95f }, { 1.25f, 1.15f, 1.05f },
                 { 1.25f, 1.12f, 1.0f }, { 1.05f, 0.85f, 0.68f }, 1.0f, 1.1f, 0.5f, 0 },
  [TH_TEMPLE] = { TEX_MOSS, TEX_JUNGLE, TEX_JUNGLE, TEX_CRATE, { 1.1f, 0.95f, 0.8f }, { 1, 0.88f, 0.74f }, { 0.95f, 0.85f, 0.75f },
                  { 0.9f, 0.82f, 0.72f }, { 0.95f, 0.85f, 0.7f }, 1.0f, 1.15f, 0.9f, 0 },
  [TH_CANDY] = { TEX_FROSTING, TEX_CANDY, TEX_CANDY, TEX_CANDY, { 1, 0.86f, 0.92f }, { 0.86f, 0.94f, 1 }, { 1, 0.75f, 0.85f },
                 { 0.7f, 0.9f, 1 }, { 0.6f, 1, 0.75f }, 0.85f, 1.0f, 0.3f, 1 },
  [TH_NEON] = { TEX_ASPHALT, TEX_PANEL, TEX_PANEL, TEX_CRATE, { 1, 1, 1 }, { 0.8f, 0.82f, 0.9f }, { 0.75f, 0.8f, 1 },
                { 0.8f, 0.85f, 1 }, { 0.55f, 0.55f, 0.65f }, 1.0f, 1.25f, 0.4f, 0.6f },
  [TH_PIRATE] = { TEX_DECK, TEX_DECK, TEX_DECK, TEX_CRATE, { 1.3f, 1.25f, 1.15f }, { 1.18f, 1.12f, 1.05f }, { 1.1f, 1.05f, 1 },
                  { 1.35f, 1.15f, 0.95f }, { 1.2f, 1.05f, 0.85f }, 0.9f, 1.0f, 0.7f, 0.2f },
  [TH_DESERT] = { TEX_SAND, TEX_SANDSTONE, TEX_SANDSTONE, TEX_CRATE, { 1, 1, 1 }, { 0.95f, 0.92f, 0.88f },
                  { 1, 0.97f, 0.93f }, { 0.95f, 0.88f, 0.8f }, { 0.95f, 0.85f, 0.7f }, 1.0f, 1.2f, 0.95f, 0 },
};

typedef struct ragdoll {
  bool on;
  t3_vec3 p, v;             /* body centre, metres; m/s */
  t3_vec3 rot, spin;        /* euler radians; rad/s */
  float limb[6], limb_v[6]; /* head nod, ear flop, arm flail x2, leg kick x2 */
  int ticks, bounces;
} ragdoll;

typedef struct pstate {
  critter c;
  float yaw;                /* current facing */
  float phase;              /* walk cycle */
  float squash;             /* drop-bomb squash, decays */
  int blink;                /* ticks left of a blink */
  int celebrate;            /* victory hop ticks */
  ragdoll rd;
  bool was_alive;
} pstate;

static struct {
  bool ready;
  int theme;
  cw_node root;
  t3_instanced_mesh *floor, *walls, *pillars, *crates, *ice, *sand, *belts, *lightbox[2], *danger;
  t3_object *ring[MAXP];
  uint8_t humans;
  t3_material *belt_mat, *floor_mat;
  pstate ps[MAXP];
  bool on[MAXP];
  t3_object *bomb[MAXB];
  t3_material *bomb_glow[MAXB], *bomb_band[MAXB];
  t3_object *bomb_spark[MAXB];
  int bomb_owner[MAXB];
  bool bomb_was_live[MAXB];
  t3_object *item_obj[GH][GW];
  cw_node item_node[GH][GW];
  int item_kind[GH][GW];
  t3_object *vent[MAX_VENTS], *tele[MAX_TELE * 2], *cannon[MAX_CANNONS];
  t3_material *vent_mat[MAX_VENTS], *tele_mat[MAX_TELE * 2], *cannon_mat[MAX_CANNONS];
  int vent_x[MAX_VENTS], vent_y[MAX_VENTS], nvent, ncannon, cannon_x[MAX_CANNONS], cannon_y[MAX_CANNONS];
  int ticks;
  bool podium;
  int podium_rank[MAXP];
  t3_object *podium_obj;
  /* the walls and crates as last placed: they are placed again only when these change */
  bool blocks_placed, placed_podium, dropping, wobbling;
  uint8_t placed_block[GH][GW], placed_hp[GH][GW];
  int16_t placed_burn[GH][GW];
} W;

/* the node drives its object's transform: our object hangs inside a
 * wrapper group, so world.c moves it freely */
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
static t3_material *stdm(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

static const t3_mat4 ZERO;

static t3_instanced_mesh *instanced(const char *name, t3_geometry *g, t3_material *m, int count) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, count);
  im->mesh.base.cast_shadow = true;
  im->mesh.base.receive_shadow = true;
  for (int i = 0; i < count; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  hang(W.root, name, &im->mesh.base); /* we keep our reference */
  t3_release(g);
  t3_release(m);
  return im;
}

static void place(t3_instanced_mesh *im, int i, t3_vec3 p, t3_vec3 s, float yaw) {
  t3_mat4 mx;
  t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
  t3_mat4_compose(&mx, p, q, s);
  t3_instanced_mesh_set_matrix_at(im, i, &mx);
}

static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = a * 374761393u + b * 668265263u + c * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float hrnd(uint32_t h) { return (float)(h & 0xffffff) / 16777216.0f; }

void world_reset(void) {
  if (W.ready) {
    t3_instanced_mesh *ims[] = { W.floor, W.walls, W.pillars, W.crates, W.ice, W.sand, W.belts, W.lightbox[0], W.lightbox[1], W.danger };
    for (int i = 0; i < 10; i++)
      if (ims[i]) t3_release(ims[i]);
    if (W.belt_mat) t3_release(W.belt_mat);
    if (W.floor_mat) t3_release(W.floor_mat);
    for (int i = 0; i < MAXP; i++)
      if (W.ps[i].c.root) {
        t3_release(W.ps[i].c.team_mat);
        t3_release(W.ps[i].c.root);
      }
    for (int i = 0; i < MAXB; i++)
      if (W.bomb[i]) t3_release(W.bomb[i]);
    for (int y = 0; y < GH; y++)
      for (int x = 0; x < GW; x++)
        if (W.item_obj[y][x]) t3_release(W.item_obj[y][x]);
  }
  memset(&W, 0, sizeof W);
}

/* ── building ───────────────────────────────────────────────────────── */

static t3_mesh *add(t3_object *g, t3_geometry *geo, t3_material *m, t3_vec3 p) {
  t3_mesh *me = t3_mesh_new(geo, m);
  me->base.position = p;
  me->base.cast_shadow = me->base.receive_shadow = true;
  t3_object_add(g, me);
  t3_release(me);
  return me;
}

static t3_object *glyph_pad(t3_material **mat_out) {
  t3_object *g = t3_group_new();
  t3_material *stone = stdm(lin(0.55f, 0.55f, 0.5f), 0.8f, 0);
  tex_apply(stone, TEX_JUNGLE, 1, 1, 1);
  t3_geometry *disc = t3_cylinder_geometry_new(0.44f, 0.46f, 0.06f, 32, 1, false);
  add(g, disc, stone, (t3_vec3){ 0, 0.03f, 0 });
  t3_material *glow = phys(lin(0.3f, 1, 0.8f), 0.3f, 0);
  glow->emissive = lin(0.25f, 1, 0.75f);
  glow->emissive_intensity = 2;
  t3_geometry *ring = t3_torus_geometry_new(0.3f, 0.035f, 8, 40, 6.2831853f);
  t3_mesh *r = add(g, ring, glow, (t3_vec3){ 0, 0.07f, 0 });
  t3_object_set_rotation(&r->base, 1.5707963f, 0, 0);
  t3_geometry *gem = t3_octahedron_geometry_new(0.1f, 0);
  t3_mesh *gm = add(g, gem, glow, (t3_vec3){ 0, 0.25f, 0 });
  gm->base.scale = (t3_vec3){ 1, 1.6f, 1 };
  t3_release(gem);
  t3_release(ring);
  t3_release(disc);
  t3_release(stone);
  *mat_out = glow;
  return g;
}

static t3_object *vent_model(t3_material **mat_out) {
  t3_object *g = t3_group_new();
  t3_material *iron = stdm(lin(0.3f, 0.28f, 0.27f), 0.5f, 0.9f);
  tex_apply(iron, TEX_METAL, 1, 1, 1);
  t3_geometry *rim = t3_torus_geometry_new(0.36f, 0.06f, 8, 32, 6.2831853f);
  t3_mesh *r = add(g, rim, iron, (t3_vec3){ 0, 0.03f, 0 });
  t3_object_set_rotation(&r->base, 1.5707963f, 0, 0);
  t3_material *core = phys(lin(1, 0.4f, 0.08f), 0.5f, 0);
  core->emissive = lin(1, 0.32f, 0.05f);
  core->emissive_intensity = 0.6f;
  t3_geometry *disc = t3_circle_geometry_new(0.34f, 32);
  t3_mesh *d = add(g, disc, core, (t3_vec3){ 0, 0.012f, 0 });
  d->base.cast_shadow = false;
  t3_object_set_rotation(&d->base, -1.5707963f, 0, 0);
  t3_geometry *bar = geo_rounded_box(0.66f, 0.04f, 0.05f, 0.015f, 2);
  for (int i = -2; i <= 2; i++) {
    t3_mesh *b = add(g, bar, iron, (t3_vec3){ 0, 0.04f, i * 0.12f });
    b->base.scale.x = cw_clampf(1 - (i * i) * 0.12f, 0.3f, 1);
  }
  t3_release(bar);
  t3_release(disc);
  t3_release(rim);
  t3_release(iron);
  *mat_out = core;
  return g;
}

static t3_object *cannon_model(int dir, t3_material **mat_out) {
  t3_object *g = t3_group_new();
  t3_material *iron = phys(lin(0.12f, 0.12f, 0.14f), 0.35f, 0.85f);
  iron->clearcoat = 0.5f;
  t3_geometry *barrel = t3_cylinder_geometry_new(0.16f, 0.21f, 0.75f, 24, 1, false);
  t3_mesh *b = add(g, barrel, iron, (t3_vec3){ 0, 1.15f, 0.25f });
  t3_object_set_rotation(&b->base, 1.5707963f, 0, 0);
  t3_material *mouth = phys(lin(1, 0.5f, 0.1f), 0.5f, 0);
  mouth->emissive = lin(1, 0.45f, 0.08f);
  mouth->emissive_intensity = 0;
  t3_geometry *disc = t3_circle_geometry_new(0.12f, 20);
  add(g, disc, mouth, (t3_vec3){ 0, 1.15f, 0.63f });
  t3_material *brass = stdm(lin(0.9f, 0.7f, 0.3f), 0.3f, 1);
  t3_geometry *ring = t3_torus_geometry_new(0.2f, 0.03f, 8, 24, 6.2831853f);
  add(g, ring, brass, (t3_vec3){ 0, 1.15f, 0.55f });
  t3_release(ring);
  t3_release(brass);
  t3_release(disc);
  t3_release(barrel);
  t3_release(iron);
  /* +z local turned to the way it fires */
  static const float YAW[4] = { 1.5707963f, 3.1415927f, -1.5707963f, 0 };
  t3_object_set_rotation(g, 0, YAW[dir], 0);
  *mat_out = mouth;
  return g;
}

void world_build(cw_node scene, const sim *s, const int crit[MAXP]) {
  world_reset();
  W.theme = s->theme;
  const look *L = &LOOKS[s->theme];
  W.root = cw_node_new("Node3D", "World");
  cw_node_add_child(scene, W.root);

  /* floor slabs: a gentle two-tone checker in the instance colours */
  t3_material *fm = stdm(lin(1, 1, 1), 1, 0);
  tex_apply(fm, L->floor_tex, 1, 1, 1);
  if (s->theme == TH_FORGE) { /* the lava in the cracks glows */
    fm->emissive = lin(1, 0.35f, 0.06f);
    fm->emissive_intensity = 0.4f;
    t3_material_set_texture(fm, T3_EMISSIVE_MAP, tex_get(TEX_BASALT)->map);
  }
  if (s->theme == TH_NEON) fm->metalness = 0.2f;
  W.floor_mat = fm;
  t3_retain(fm);
  W.floor = instanced("Floor", geo_rounded_box(0.985f, 0.3f, 0.985f, 0.045f, 3), fm, MAXI);
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      int i = y * GW + x;
      uint32_t h = hash3((uint32_t)x, (uint32_t)y, 77);
      place(W.floor, i, (t3_vec3){ tx_w(x), -0.15f - hrnd(h) * 0.015f, tz_w(y) }, (t3_vec3){ 1, 1, 1 }, (float)(h >> 24 & 3) * 1.5707963f);
      const float *c = (x + y) & 1 ? L->floor_alt : L->floor_rgb;
      float k = 0.94f + 0.06f * hrnd(h >> 5);
      t3_instanced_mesh_set_color_at(W.floor, i, (t3_color){ c[0] * k, c[1] * k, c[2] * k });
    }
  /* rim walls and pillars */
  t3_material *wm = stdm(lin(1, 1, 1), L->wall_rough, 0);
  tex_apply(wm, L->wall_tex, 1, 1, 1);
  if (s->theme == TH_FORGE) wm->metalness = 0.6f;
  W.walls = instanced("Walls", geo_rounded_box(1, 1, 1, 0.07f, 3), wm, MAXI);
  t3_material *pm;
  if (s->theme == TH_FROST) { /* ice pillars: clear, glossy, a little blue */
    pm = phys(lin(0.8f, 0.92f, 1), 0.08f, 0);
    tex_apply(pm, TEX_ICE, 1, 0.6f, 1);
    pm->clearcoat = 1;
    pm->clearcoat_roughness = 0.03f;
  } else if (s->theme == TH_CANDY) {
    pm = phys(lin(1, 1, 1), 0.25f, 0);
    tex_apply(pm, TEX_CANDY, 1, 0.6f, 1);
    pm->clearcoat = 1;
    pm->clearcoat_roughness = 0.05f;
  } else {
    pm = stdm(lin(1, 1, 1), L->wall_rough, s->theme == TH_FORGE ? 0.6f : 0);
    tex_apply(pm, L->pillar_tex, 1, 1, 1);
  }
  t3_geometry *pg = s->theme == TH_PIRATE ? t3_cylinder_geometry_new(0.44f, 0.44f, 1, 24, 4, false)
                                          : geo_rounded_box(0.94f, 1, 0.94f, s->theme == TH_CANDY ? 0.25f : 0.1f, 4);
  if (s->theme == TH_PIRATE) { /* barrels: the staves bulge */
    t3_attribute *pa = pg->attributes[T3_ATTR_POSITION];
    float *pp = pa->array;
    for (int i = 0; i < pa->count; i++) {
      float y = pp[i * 3 + 1], k = 1 + 0.12f * (1 - 4 * y * y);
      pp[i * 3] *= k, pp[i * 3 + 2] *= k;
    }
    pa->version++;
  }
  W.pillars = instanced("Pillars", pg, pm, MAXI);
  if (s->theme == TH_NEON) { /* lightboxes on every wall top: pink on the rim, cyan on the pillars */
    static const float C[2][3] = { { 1, 0.25f, 0.75f }, { 0.25f, 0.9f, 1 } };
    for (int k = 0; k < 2; k++) {
      t3_material *lm = stdm(lin(C[k][0], C[k][1], C[k][2]), 0.4f, 0);
      lm->emissive = lin(C[k][0], C[k][1], C[k][2]);
      lm->emissive_intensity = 2.2f;
      W.lightbox[k] = instanced(k ? "Lightbox" : "Rimlight", geo_rounded_box(k ? 0.5f : 0.86f, 0.04f, k ? 0.5f : 0.18f, 0.02f, 2), lm, MAXI);
      W.lightbox[k]->mesh.base.cast_shadow = false;
    }
  }
  /* crates */
  t3_material *cm;
  if (s->theme == TH_CANDY) { /* jelly cubes: clearcoated, glossy, take two blasts */
    cm = phys(lin(1, 1, 1), 0.15f, 0);
    cm->clearcoat = 1;
    cm->clearcoat_roughness = 0.03f;
  } else {
    cm = phys(lin(1, 1, 1), 0.8f, 0);
    tex_apply(cm, L->crate_tex, 1, 1, 1);
    if (L->crate_cc > 0) cm->clearcoat = L->crate_cc, cm->clearcoat_roughness = 0.2f;
  }
  W.crates = instanced("Crates", geo_rounded_box(0.84f, 0.84f, 0.84f, s->theme == TH_CANDY ? 0.2f : 0.05f, 3), cm, MAXI);
  /* floor features: only the ones this arena has */
  int nice = 0, nsand = 0, nbelt = 0;
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      nice += s->floor[y][x] == F_ICE;
      nsand += s->floor[y][x] == F_SAND;
      nbelt += s->floor[y][x] >= F_BELT_E && s->floor[y][x] <= F_BELT_S;
    }
  if (nice) {
    t3_material *im = phys(lin(0.75f, 0.9f, 1), 0.05f, 0);
    tex_apply(im, TEX_ICE, 1, 0.5f, 1);
    im->clearcoat = 1;
    im->clearcoat_roughness = 0.02f;
    W.ice = instanced("Ice", geo_rounded_box(0.99f, 0.06f, 0.99f, 0.02f, 2), im, MAXI);
    W.ice->mesh.base.cast_shadow = false;
  }
  if (nsand) {
    t3_material *sm = stdm(lin(1, 1, 1), 1, 0);
    tex_apply(sm, TEX_SAND, 1, 1.2f, 1);
    W.sand = instanced("Sand", geo_rounded_box(0.99f, 0.05f, 0.99f, 0.02f, 2), sm, MAXI);
    W.sand->mesh.base.cast_shadow = false;
  }
  if (nbelt) {
    t3_material *bm = stdm(lin(1, 1, 1), 0.7f, 0.1f);
    tex_apply(bm, TEX_RUBBER, 2, 1, 1); /* its own textures: they scroll */
    W.belt_mat = bm;
    t3_retain(bm);
    W.belts = instanced("Belts", geo_rounded_box(0.96f, 0.08f, 0.96f, 0.03f, 2), bm, MAXI);
  }
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      int i = y * GW + x, f = s->floor[y][x];
      t3_vec3 p = { tx_w(x), 0, tz_w(y) };
      if (f == F_ICE) place(W.ice, i, (t3_vec3){ p.x, 0.02f, p.z }, (t3_vec3){ 1, 1, 1 }, 0);
      if (f == F_SAND) place(W.sand, i, (t3_vec3){ p.x, 0.015f, p.z }, (t3_vec3){ 1, 1, 1 }, (float)(hash3(x, y, 9) & 3) * 1.5707963f);
      if (f >= F_BELT_E && f <= F_BELT_S) {
        /* the texture runs along v (+z local); turned to the belt's direction */
        static const float YAW[4] = { 1.5707963f, 3.1415927f, -1.5707963f, 0 };
        place(W.belts, i, (t3_vec3){ p.x, 0.03f, p.z }, (t3_vec3){ 1, 1, 1 }, YAW[f - F_BELT_E]);
      }
      if (f == F_VENT && W.nvent < MAX_VENTS) {
        t3_object *v = vent_model(&W.vent_mat[W.nvent]);
        v->position = p;
        W.vent[W.nvent] = v;
        W.vent_x[W.nvent] = x, W.vent_y[W.nvent] = y;
        hang(W.root, "Vent", v);
        t3_release(v);
        W.nvent++;
      }
      if (f == F_TELE)
        for (int k = 0; k < s->tele_n; k++)
          if (s->tele_x[k] == x && s->tele_y[k] == y) {
            t3_object *t = glyph_pad(&W.tele_mat[k]);
            t->position = p;
            W.tele[k] = t;
            hang(W.root, "Glyph", t);
            t3_release(t);
          }
      if ((f == F_CANNON_ROW || f == F_CANNON_COL) && W.ncannon < MAX_CANNONS) {
        int d = f == F_CANNON_ROW ? (x == 0 ? D_E : D_W) : (y == 0 ? D_S : D_N);
        t3_object *c = cannon_model(d, &W.cannon_mat[W.ncannon]);
        c->position = p;
        W.cannon[W.ncannon] = c;
        W.cannon_x[W.ncannon] = x, W.cannon_y[W.ncannon] = y;
        hang(W.root, "Cannon", c);
        t3_release(c);
        W.ncannon++;
      }
    }
  /* the danger preview: tiles a blast is about to reach glow red under the critters */
  t3_material *dm = stdm(lin(1, 0.15f, 0.05f), 1, 0);
  dm->emissive = lin(1, 0.12f, 0.03f);
  dm->emissive_intensity = 1.2f;
  dm->transparent = true;
  dm->opacity = 0.45f;
  dm->depth_write = false;
  W.danger = instanced("Danger", geo_rounded_box(0.9f, 0.02f, 0.9f, 0.01f, 1), dm, MAXI);
  W.danger->mesh.base.cast_shadow = W.danger->mesh.base.receive_shadow = false;
  /* critters */
  for (int i = 0; i < MAXP; i++) {
    W.on[i] = s->p[i].on;
    if (!W.on[i]) continue;
    pstate *ps = &W.ps[i];
    critter_build(&ps->c, crit ? crit[i] : i, i);
    hang(W.root, "Critter", ps->c.root);
    /* a glowing team ring under the feet of every human player */
    t3_material *rm = stdm(lin(TEAM_RGB[i][0], TEAM_RGB[i][1], TEAM_RGB[i][2]), 0.5f, 0);
    rm->emissive = lin(TEAM_RGB[i][0], TEAM_RGB[i][1], TEAM_RGB[i][2]);
    rm->emissive_intensity = 2.2f;
    t3_geometry *rg = t3_torus_geometry_new(0.36f, 0.035f, 8, 40, 6.2831853f);
    t3_mesh *ring = t3_mesh_new(rg, rm);
    t3_object_set_rotation(&ring->base, 1.5707963f, 0, 0);
    ring->base.visible = false;
    W.ring[i] = &ring->base;
    hang(W.root, "Ring", &ring->base);
    t3_release(ring);
    t3_release(rg);
    t3_release(rm);
    ps->yaw = s->p[i].dir == D_N ? 3.1415927f : 0;
    ps->was_alive = true;
  }
  for (int i = 0; i < MAXB; i++) {
    W.bomb[i] = model_bomb(0, &W.bomb_glow[i], &W.bomb_band[i], &W.bomb_spark[i]);
    W.bomb[i]->visible = false;
    hang(W.root, "Bomb", W.bomb[i]);
  }
  W.ready = true;
}

/* ── animation ──────────────────────────────────────────────────────── */

static float approach_angle(float a, float target, float k) { return a + cw_wrap_angle(target - a) * k; }

static void spring(float *x, float *v, float target, float k, float damp) {
  *v += (target - *x) * k - *v * damp;
  *x += *v;
}

static void ko_start(pstate *ps, const sim *s, int who) {
  const player *p = &s->p[who];
  ragdoll *r = &ps->rd;
  memset(r, 0, sizeof *r);
  r->on = true;
  r->p = (t3_vec3){ wx(p->x), 0.3f, wz(p->y) };
  uint32_t h = hash3((uint32_t)who, (uint32_t)s->tick, 5);
  float a = hrnd(h) * 6.2831853f;
  r->v = (t3_vec3){ cw_cos(a) * 2.2f, 7.5f + hrnd(h >> 4) * 2, cw_sin(a) * 2.2f };
  r->spin = (t3_vec3){ (hrnd(h >> 8) - 0.5f) * 22, (hrnd(h >> 12) - 0.5f) * 14, (hrnd(h >> 16) - 0.5f) * 22 };
  r->rot.y = ps->yaw;
  for (int k = 0; k < 6; k++) r->limb_v[k] = (hrnd(h >> (k + 2)) - 0.5f) * 0.8f;
}

static void ko_tick(pstate *ps) {
  ragdoll *r = &ps->rd;
  const float dt = 1.0f / 60.0f;
  r->ticks++;
  r->v.y -= 22 * dt;
  r->p.x += r->v.x * dt;
  r->p.y += r->v.y * dt;
  r->p.z += r->v.z * dt;
  float lx = GW * 0.5f - 1.3f, lz = GH * 0.5f - 1.3f; /* the rim keeps it in */
  if (r->p.x < -lx || r->p.x > lx) r->v.x = -r->v.x * 0.6f, r->p.x = cw_clampf(r->p.x, -lx, lx);
  if (r->p.z < -lz || r->p.z > lz) r->v.z = -r->v.z * 0.6f, r->p.z = cw_clampf(r->p.z, -lz, lz);
  if (r->p.y < 0.25f && r->v.y < 0) { /* bounce, then roll to a stop */
    r->p.y = 0.25f;
    r->v.y = r->bounces < 3 ? -r->v.y * 0.45f : 0;
    r->v.x *= 0.6f;
    r->v.z *= 0.6f;
    r->spin.x *= 0.55f;
    r->spin.y *= 0.55f;
    r->spin.z *= 0.55f;
    r->bounces++;
    for (int k = 0; k < 6; k++) r->limb_v[k] += (k & 1 ? 0.5f : -0.5f) * (4 - CW_MIN(r->bounces, 3)) * 0.3f;
  }
  if (r->bounces >= 3) { /* settle on its back, belly up */
    r->rot.x += (-1.45f - cw_wrap_angle(r->rot.x)) * 0.1f;
    r->rot.z += (0 - cw_wrap_angle(r->rot.z)) * 0.1f;
    r->spin = (t3_vec3){ 0, 0, 0 };
  }
  r->rot.x += r->spin.x * dt;
  r->rot.y += r->spin.y * dt;
  r->rot.z += r->spin.z * dt;
  float jolt = (r->spin.x + r->spin.z) * 0.002f;
  for (int k = 0; k < 6; k++) {
    r->limb_v[k] += jolt * (k & 1 ? 1 : -1);
    spring(&r->limb[k], &r->limb_v[k], 0, 0.06f, 0.06f);
  }
}

static void pose_critter(pstate *ps, const sim *s, int i) {
  critter *c = &ps->c;
  const player *p = &s->p[i];
  int t = W.ticks;
  if (ps->rd.on) {
    ragdoll *r = &ps->rd;
    c->root->position = (t3_vec3){ r->p.x, r->p.y - 0.3f, r->p.z };
    t3_object_set_rotation(c->root, r->rot.x, r->rot.y, r->rot.z);
    t3_object_set_rotation(c->head, r->limb[0] * 1.5f, 0, r->limb[1] * 1.2f);
    t3_object_set_rotation(c->ear[0], 0, 0, 0.3f + r->limb[1] * 3);
    t3_object_set_rotation(c->ear[1], 0, 0, -0.3f - r->limb[1] * 3);
    t3_object_set_rotation(c->arm[0], r->limb[2] * 4, 0, -1.2f + r->limb[3] * 2);
    t3_object_set_rotation(c->arm[1], r->limb[3] * 4, 0, 1.2f + r->limb[2] * 2);
    t3_object_set_rotation(c->foot[0], -0.6f + r->limb[4] * 4, 0, -0.3f);
    t3_object_set_rotation(c->foot[1], -0.6f + r->limb[5] * 4, 0, 0.3f);
    c->lids[0]->visible = c->lids[1]->visible = true;
    c->mouth_o->visible = true;
    c->mouth_smile->visible = false;
    c->bubble->visible = false;
    float fade = (r->ticks > 150 ? cw_clampf(1 - (r->ticks - 150) / 40.0f, 0, 1) : 1) * 1.15f;
    c->root->scale = (t3_vec3){ fade, fade, fade };
    c->root->visible = fade > 0.01f;
    return;
  }
  bool moving = p->moving > 0 && s->over_tick == 0;
  float target_yaw = p->dir == D_E ? 1.5707963f : p->dir == D_W ? -1.5707963f : p->dir == D_N ? 3.1415927f : 0;
  ps->yaw = approach_angle(ps->yaw, target_yaw, 0.3f);
  if (moving) ps->phase += 0.2f + p->speed * 0.012f;
  float sw = moving ? cw_sin(ps->phase) : 0, bob = moving ? fabsf(cw_sin(ps->phase)) : 0;
  float breathe = cw_sin(t * 0.07f + i) * 0.02f;
  float hop = 0;
  if (ps->celebrate > 0) {
    float k = (float)(ps->celebrate % 40) / 40.0f;
    hop = 4 * k * (1 - k) * 0.6f;
    ps->yaw += 0.15f;
  }
  float sq = ps->squash;
  c->root->position = (t3_vec3){ wx(p->x), bob * 0.07f + hop, wz(p->y) };
  t3_object_set_rotation(c->root, 0, ps->yaw, moving ? sw * 0.09f : 0);
  float sy = 1 + breathe - sq * 0.25f + bob * 0.04f, sxz = 1 - breathe * 0.5f + sq * 0.15f;
  c->body->scale = (t3_vec3){ sxz, sy, sxz };
  c->root->scale = (t3_vec3){ 1.15f, 1.15f, 1.15f };
  c->root->visible = true;
  t3_object_set_rotation(c->head, moving ? 0.08f : cw_sin(t * 0.03f + i * 2) * 0.05f, cw_sin(t * 0.021f + i) * (moving ? 0 : 0.25f), 0);
  float flop = cw_sin(ps->phase * 2) * 0.15f;
  t3_object_set_rotation(c->ear[0], 0, 0, flop + (ps->celebrate ? 0.4f : 0));
  t3_object_set_rotation(c->ear[1], 0, 0, -flop - (ps->celebrate ? 0.4f : 0));
  float arm_up = ps->celebrate > 0 ? 2.6f : sq * 2.2f;
  t3_object_set_rotation(c->arm[0], sw * 0.8f, 0, -0.15f - arm_up);
  t3_object_set_rotation(c->arm[1], -sw * 0.8f, 0, 0.15f + arm_up);
  t3_object_set_rotation(c->foot[0], -sw * 0.7f, 0, 0);
  t3_object_set_rotation(c->foot[1], sw * 0.7f, 0, 0);
  c->foot[0]->position.y = 0.06f + (sw > 0 ? sw * 0.06f : 0);
  c->foot[1]->position.y = 0.06f + (sw < 0 ? -sw * 0.06f : 0);
  t3_object_set_rotation(c->tail, 0, cw_sin(t * 0.15f + i) * 0.4f, 0);
  if (ps->blink > 0) ps->blink--;
  else if ((t + i * 53) % 197 == 0) ps->blink = 7;
  c->lids[0]->visible = c->lids[1]->visible = ps->blink > 0;
  bool surprised = sq > 0.3f;
  c->mouth_o->visible = surprised;
  c->mouth_smile->visible = !surprised;
  c->bubble->visible = p->bubble > 0 || (p->invuln > 0 && (p->invuln / 4) % 2);
  float wb = cw_sin(t * 0.12f + i) * 0.04f;
  c->bubble->scale = (t3_vec3){ 1 + wb, 1 - wb, 1 + wb };
  ps->squash *= 0.86f;
  if (ps->celebrate > 0) ps->celebrate++;
}

/* the podium: three lacquered steps, the critters on them by wins */
static void podium_pose(pstate *ps, int i) {
  static const float X[MAXP] = { 0, -1.25f, 1.25f, 2.5f }, Y[MAXP] = { 0.9f, 0.6f, 0.35f, 0 }, Z[MAXP] = { 0, 0.15f, 0.15f, 0.6f };
  int r = W.podium_rank[i];
  critter *c = &ps->c;
  float hop = 0;
  if (r == 0) {
    float k = (float)(W.ticks % 36) / 36.0f;
    hop = 4 * k * (1 - k) * 0.45f;
  }
  c->root->position = (t3_vec3){ X[r], Y[r] + hop, Z[r] + 0.4f };
  t3_object_set_rotation(c->root, 0, r == 0 ? cw_sin(W.ticks * 0.05f) * 0.4f : (r == 1 ? 0.25f : -0.25f), 0);
  c->root->scale = (t3_vec3){ 1, 1, 1 };
  c->root->visible = true;
  float up = r == 0 ? 2.6f : r == 1 ? 1.2f : 0.3f;
  t3_object_set_rotation(c->arm[0], 0, 0, -0.2f - up + (r == 0 ? cw_sin(W.ticks * 0.3f) * 0.3f : 0));
  t3_object_set_rotation(c->arm[1], 0, 0, 0.2f + up - (r == 0 ? cw_sin(W.ticks * 0.3f) * 0.3f : 0));
  t3_object_set_rotation(c->head, r >= 2 ? 0.25f : -0.1f, 0, 0);
  c->lids[0]->visible = c->lids[1]->visible = r == 0 && (W.ticks % 40) < 20;
  c->mouth_o->visible = r == 0;
  c->mouth_smile->visible = r != 0;
  c->bubble->visible = false;
}

void world_podium(const sim *s, const int wins[MAXP], int winner) {
  if (!W.ready) return;
  (void)s;
  int order[MAXP], n = 0;
  for (int i = 0; i < MAXP; i++)
    if (W.on[i]) order[n++] = i;
  for (int a = 0; a < n; a++)
    for (int b = a + 1; b < n; b++)
      if (wins[order[b]] > wins[order[a]] || (order[b] == winner && order[a] != winner)) {
        int t = order[a];
        order[a] = order[b];
        order[b] = t;
      }
  for (int r = 0; r < n; r++) {
    W.podium_rank[order[r]] = r;
    W.ps[order[r]].rd.on = false;
  }
  /* hide the arena's middle, raise the steps */
  t3_object *g = t3_group_new();
  static const float X[3] = { 0, -1.25f, 1.25f }, H[3] = { 0.9f, 0.6f, 0.35f };
  static const float C[3][3] = { { 1, 0.78f, 0.2f }, { 0.82f, 0.85f, 0.9f }, { 0.85f, 0.5f, 0.25f } };
  for (int k = 0; k < 3 && k < n; k++) {
    t3_material *m = phys(lin(C[k][0], C[k][1], C[k][2]), 0.25f, k == 2 ? 0.6f : 0.9f);
    m->clearcoat = 1;
    m->clearcoat_roughness = 0.05f;
    t3_geometry *box = geo_rounded_box(1.15f, H[k], 1.15f, 0.08f, 3);
    add(g, box, m, (t3_vec3){ X[k], H[k] * 0.5f, 0.55f });
    t3_release(box);
    t3_release(m);
  }
  /* a round dais under them */
  t3_material *dm = phys(lin(0.25f, 0.15f, 0.4f), 0.4f, 0);
  dm->clearcoat = 0.6f;
  dm->sheen = 1;
  dm->sheen_color = lin(0.8f, 0.6f, 1);
  t3_geometry *dais = t3_cylinder_geometry_new(2.6f, 2.8f, 0.12f, 48, 1, false);
  add(g, dais, dm, (t3_vec3){ 0, 0.06f, 0.4f });
  t3_release(dais);
  t3_release(dm);
  W.podium_obj = g;
  hang(W.root, "Podium", g);
  t3_release(g);
  /* clear the floor round it: bombs and items away */
  for (int i = 0; i < MAXB; i++) W.bomb[i]->visible = false;
  W.podium = true;
  fx_confetti((t3_vec3){ 0, 3, 0.5f });
}

void world_tick(const sim *s) {
  if (!W.ready) return;
  W.ticks++;
  int t = W.ticks;
  const look *L = &LOOKS[W.theme];
  /* walls and crates: placed again only when what they show changed (the
   * blocks, a crate's burn or cracks, the podium, a falling sudden-death
   * wall, a cracked jelly's quiver), and only the ones there drawn */
  bool blocks = !W.blocks_placed || W.placed_podium != W.podium || memcmp(W.placed_block, s->block, sizeof W.placed_block);
  bool dropping = s->sd_drop_t && s->tick - s->sd_drop_t < 12;
  bool place_walls = blocks || dropping || W.dropping;
  bool place_crates = blocks || W.wobbling || memcmp(W.placed_burn, s->burn, sizeof W.placed_burn) ||
                      memcmp(W.placed_hp, s->crate_hp, sizeof W.placed_hp);
  W.dropping = dropping;
  int nwall = 0, npillar = 0, ncrate = 0, nlight[2] = { 0, 0 };
  if (place_crates) W.wobbling = false;
  for (int y = 0; y < GH && (place_walls || place_crates); y++)
    for (int x = 0; x < GW; x++) {
      uint32_t h = hash3((uint32_t)x, (uint32_t)y, 31);
      if (W.podium && x >= 4 && x <= 10 && y >= 3 && y <= 9) continue; /* room for the podium */
      if (s->block[y][x] == C_WALL && place_walls) {
        bool rim = x == 0 || y == 0 || x == GW - 1 || y == GH - 1;
        float hgt = rim ? (y == GH - 1 ? 0.42f : L->wall_h) : L->pillar_h, drop = 0; /* the near wall low: nothing hides behind it */
        if (x == s->sd_drop_x && y == s->sd_drop_y && dropping) {
          float k = (s->tick - s->sd_drop_t) / 12.0f;
          drop = (1 - k * k) * 8;
        }
        float k = 0.9f + 0.1f * hrnd(h >> 3);
        const float *c = rim ? L->wall_rgb : L->pillar_rgb;
        t3_color wc = { c[0] * k, c[1] * k, c[2] * k };
        if (rim) {
          t3_instanced_mesh_set_color_at(W.walls, nwall, wc);
          place(W.walls, nwall++, (t3_vec3){ tx_w(x), hgt * 0.5f, tz_w(y) }, (t3_vec3){ 1, hgt, 1 }, (float)(h & 3) * 1.5707963f);
        }
        if (W.lightbox[0]) {
          int lk = rim ? 0 : 1;
          place(W.lightbox[lk], nlight[lk]++, (t3_vec3){ tx_w(x), hgt + drop + 0.01f, tz_w(y) }, (t3_vec3){ 1, 1, 1 }, 0);
        } else { /* a rim wall holds a pillar too (untinted): their tops meet, and the look is the two together */
          t3_instanced_mesh_set_color_at(W.pillars, npillar, rim ? (t3_color){ 1, 1, 1 } : wc);
          place(W.pillars, npillar++, (t3_vec3){ tx_w(x), hgt * 0.5f + drop, tz_w(y) }, (t3_vec3){ 1, hgt, 1 }, (float)(h & 3) * 1.5707963f);
        }
      } else if (s->block[y][x] == C_CRATE && place_crates) {
        float k = 1, lift = 0, wob = 0;
        if (s->burn[y][x]) { /* burning: it shrinks away */
          float b = s->burn[y][x] / 30.0f;
          k = 0.4f + 0.6f * b;
          lift = (1 - b) * 0.2f;
        }
        if (W.theme == TH_CANDY && s->crate_hp[y][x] == 1) { /* a cracked jelly quivers */
          wob = cw_sin(t * 0.3f + x) * 0.06f;
          W.wobbling = true;
        }
        float yaw = (hrnd(h) - 0.5f) * 0.16f;
        place(W.crates, ncrate, (t3_vec3){ tx_w(x), 0.42f * k + lift, tz_w(y) }, (t3_vec3){ k * (1 + wob), k * (1 - wob), k * (1 + wob) }, yaw);
        t3_color cc;
        if (W.theme == TH_CANDY) {
          static const float J[5][3] = { { 1, 0.35f, 0.5f }, { 0.4f, 0.85f, 1 }, { 0.6f, 1, 0.5f }, { 1, 0.8f, 0.3f }, { 0.8f, 0.5f, 1 } };
          const float *j = J[h % 5];
          float d = s->crate_hp[y][x] == 1 ? 0.55f : 1;
          cc = lin(j[0] * d, j[1] * d, j[2] * d);
        } else {
          const float *c = L->crate_rgb;
          float v = 0.88f + 0.12f * hrnd(h >> 7);
          cc = (t3_color){ c[0] * v, c[1] * v, c[2] * v };
        }
        if (s->burn[y][x]) {
          float b = s->burn[y][x] / 30.0f;
          cc = (t3_color){ cc.r * (0.3f + 0.7f * b) + (1 - b) * 0.6f, cc.g * (0.3f + 0.7f * b) + (1 - b) * 0.15f, cc.b * (0.3f + 0.7f * b) };
        }
        t3_instanced_mesh_set_color_at(W.crates, ncrate++, cc);
      }
    }
  t3_instanced_mesh *ims[5] = { W.walls, W.pillars, W.crates, W.lightbox[0], W.lightbox[1] };
  int counts[5] = { nwall, npillar, ncrate, nlight[0], nlight[1] };
  for (int k = 0; k < 5; k++) {
    if (!ims[k] || (k == 2 ? !place_crates : !place_walls)) continue;
    ims[k]->count = counts[k];
    ims[k]->instance_matrix->version++;
    if (ims[k]->instance_color) ims[k]->instance_color->version++;
  }
  if (place_walls || place_crates) {
    memcpy(W.placed_block, s->block, sizeof W.placed_block);
    memcpy(W.placed_burn, s->burn, sizeof W.placed_burn);
    memcpy(W.placed_hp, s->crate_hp, sizeof W.placed_hp);
    W.placed_podium = W.podium;
    W.blocks_placed = true;
  }
  if (W.belt_mat) { /* the belts run */
    t3_texture *maps[4] = { W.belt_mat->map, W.belt_mat->normal_map, W.belt_mat->roughness_map, W.belt_mat->ao_map };
    float off = (float)(t % 60) / 60.0f;
    for (int k = 0; k < 4; k++)
      if (maps[k]) maps[k]->offset.y = off;
  }
  if (W.theme == TH_FORGE) W.floor_mat->emissive_intensity = 0.22f + 0.1f * cw_sin(t * 0.03f);
  for (int k = 0; k < W.nvent; k++) { /* vents glow as they wind up */
    int ph = (s->vent[W.vent_y[k]][W.vent_x[k]] + s->tick) % 330;
    float warm = ph > 230 ? (ph - 230) / 100.0f : ph < 40 ? 1 - ph / 40.0f : 0;
    W.vent_mat[k]->emissive_intensity = 0.5f + warm * warm * 9 + 0.15f * cw_sin(t * 0.2f + k);
  }
  for (int k = 0; k < s->tele_n; k++)
    if (W.tele[k]) {
      W.tele_mat[k]->emissive_intensity = 2 + 0.8f * cw_sin(t * 0.08f + k);
      t3_object_set_rotation(W.tele[k], 0, t * 0.01f * (k & 1 ? -1 : 1), 0);
    }
  for (int k = 0; k < W.ncannon; k++) {
    int ph = s->tick % 420;
    bool cur = (s->cannon_t & 255) == W.cannon_x[k] && (s->cannon_t >> 8) == W.cannon_y[k];
    float glow = cur && ph >= 320 ? (ph - 320) / 100.0f * 6 : cur && ph < 30 && s->tick >= 420 ? 8 * (1 - ph / 30.0f) : 0;
    W.cannon_mat[k]->emissive_intensity = glow * (0.8f + 0.2f * cw_sin(t * 0.6f));
  }
  /* items: made when they appear, dropped when they go */
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      int it = s->block[y][x] == C_EMPTY && !W.podium ? s->item[y][x] : 0;
      if (W.item_kind[y][x] != it) {
        if (W.item_obj[y][x]) {
          cw_node_free(W.item_node[y][x]);
          t3_release(W.item_obj[y][x]);
          W.item_obj[y][x] = NULL;
        }
        if (it) {
          t3_object *o = model_item(it);
          W.item_obj[y][x] = o;
          W.item_node[y][x] = hang(W.root, "Item", o);
        }
        W.item_kind[y][x] = it;
      }
      if (W.item_obj[y][x]) {
        t3_object *o = W.item_obj[y][x];
        float ph = t * 0.06f + x * 0.7f + y * 1.3f;
        int age = s->item_grace[y][x] ? 36 - s->item_grace[y][x] : 36;
        float pop = cw_clampf(age / 12.0f, 0, 1);
        o->position = (t3_vec3){ tx_w(x), 0.22f + 0.07f * cw_sin(ph) + (1 - pop) * 0.5f, tz_w(y) };
        t3_object_set_rotation(o, 0, t * 0.03f + x, 0);
        o->scale = (t3_vec3){ pop, pop, pop };
      }
    }
  /* bombs */
  for (int i = 0; i < MAXB; i++) {
    const bomb *b = &s->bombs[i];
    t3_object *o = W.bomb[i];
    if (!b->live || W.podium) {
      o->visible = false;
      W.bomb_was_live[i] = false;
      continue;
    }
    if (!W.bomb_was_live[i] || W.bomb_owner[i] != b->owner) {
      W.bomb_owner[i] = b->owner;
      W.bomb_band[i]->color = lin(TEAM_RGB[b->owner][0], TEAM_RGB[b->owner][1], TEAM_RGB[b->owner][2]);
    }
    W.bomb_was_live[i] = true;
    o->visible = true;
    float in = cw_clampf(b->age / 8.0f, 0, 1), drop = (1 - in) * 0.5f;
    float rate = b->fuse < 0 ? 0.12f : 0.12f + (150 - b->fuse) * 0.0025f;
    float pulse = cw_sin(b->age * rate);
    float sx = 1 + 0.06f * pulse, sy = 1 - 0.07f * pulse;
    o->position = (t3_vec3){ wx(b->x), drop, wz(b->y) };
    o->scale = (t3_vec3){ sx, sy, sx };
    float roll = b->slide_dir == D_NONE ? 0 : b->age * 0.35f;
    t3_object_set_rotation(o, b->slide_dir == D_N || b->slide_dir == D_S ? roll * (b->slide_dir == D_S ? 1 : -1) : 0, 0,
                           b->slide_dir == D_E || b->slide_dir == D_W ? roll * (b->slide_dir == D_W ? 1 : -1) : 0);
    float hot = b->fuse >= 0 && b->fuse < 70 ? (70 - b->fuse) / 70.0f : 0;
    W.bomb_glow[i]->emissive_intensity = hot * hot * (2.5f + 2.5f * (pulse * 0.5f + 0.5f));
    float fl = 0.8f + 0.4f * hrnd(hash3((uint32_t)i, (uint32_t)t, 3));
    W.bomb_spark[i]->scale = (t3_vec3){ fl, fl, fl };
  }
  /* the danger preview */
  {
    int n = 0;
    for (int y = 0; y < GH; y++)
      for (int x = 0; x < GW; x++) {
        int d = s->danger[y][x];
        if (W.podium || d <= 0 || d > 90 || s->block[y][x] != C_EMPTY || s->over_tick) continue;
        float k = 0.55f + 0.45f * (1 - d / 90.0f) + 0.06f * cw_sin(t * 0.5f);
        place(W.danger, n++, (t3_vec3){ tx_w(x), 0.012f, tz_w(y) }, (t3_vec3){ k, 1, k }, 0);
      }
    W.danger->count = n; /* only the tiles lit, in grid order */
    if (n) W.danger->instance_matrix->version++;
  }
  for (int i = 0; i < MAXP; i++)
    if (W.ring[i]) {
      const player *p = &s->p[i];
      W.ring[i]->visible = (W.humans >> i & 1) && p->alive && !W.podium;
      float k = 1 + 0.06f * cw_sin(t * 0.15f);
      W.ring[i]->position = (t3_vec3){ wx(p->x), 0.03f, wz(p->y) };
      W.ring[i]->scale = (t3_vec3){ k, k, k };
    }
  /* critters */
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    if (e->kind == EV_DROP && W.on[e->a]) W.ps[e->a].squash = 1;
    if (e->kind == EV_PICK && W.on[e->a]) W.ps[e->a].squash = 0.5f;
  }
  for (int i = 0; i < MAXP; i++) {
    if (!W.on[i]) continue;
    pstate *ps = &W.ps[i];
    const player *p = &s->p[i];
    if (ps->was_alive && !p->alive) ko_start(ps, s, i);
    ps->was_alive = p->alive;
    if (ps->rd.on) ko_tick(ps);
    if (s->winner == i && ps->celebrate == 0 && !ps->rd.on) ps->celebrate = 1;
    pose_critter(ps, s, i);
    if (W.podium) podium_pose(ps, i);
  }
}

/* the title: the critters lined up and bouncing, one after another */
void world_title_pose(int ticks) {
  if (!W.ready) return;
  for (int i = 0; i < MAXP; i++) {
    if (!W.on[i]) continue;
    critter *c = &W.ps[i].c;
    float ph = ticks * 0.08f - i * 0.9f, hop = fabsf(cw_sin(ph));
    c->root->position = (t3_vec3){ (i - 1.5f) * 1.3f, hop * 0.35f - 0.3f, 7.6f };
    t3_object_set_rotation(c->root, 0, cw_sin(ticks * 0.02f + i) * 0.35f, cw_sin(ph) * 0.1f);
    c->body->scale = (t3_vec3){ 1 + (1 - hop) * 0.12f, 1 - (1 - hop) * 0.15f, 1 + (1 - hop) * 0.12f };
    t3_object_set_rotation(c->arm[0], 0, 0, -0.3f - hop * 1.6f);
    t3_object_set_rotation(c->arm[1], 0, 0, 0.3f + hop * 1.6f);
    c->lids[0]->visible = c->lids[1]->visible = (ticks + i * 40) % 180 < 6;
  }
}

void world_set_humans(uint8_t mask) { W.humans = mask; }
