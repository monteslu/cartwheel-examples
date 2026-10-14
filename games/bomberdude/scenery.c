/* bomberdude scenery: the world round each arena, and its weather. All of it
 * is placed from a seeded integer hash (one draw per statement: argument
 * evaluation order is the host's), so every host builds the same scene
 * (debug field scenery.hash), and it only ever draws.
 *
 *   meadow   rolling grass with thousands of instanced blades swaying in a
 *            slow travelling breeze, flowers, round trees, a fence
 *   frost    snow drifts, snowy pines, ice crystals, falling snow
 *   forge    basalt, glowing lava pools that breathe, jagged rocks, embers
 *   temple   jungle: big leaves, palms, mossy statues, fireflies
 *   candy    lollipops, gumdrops, candy canes, sugar sparkle
 *   neon     a night city: towers with lit windows, neon signs, wet street, rain
 *   pirate   the open sea round the deck, masts and sails, barrels
 *   desert   dunes, cacti, broken columns, the sandstorm */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "world.h"

#define BLADES 2400
#define BLADE_PARTS 8
#define FANS 84

static struct {
  bool ready;
  int theme;
  cw_node root;
  /* the blades in BLADE_PARTS sectors round the arena, one InstancedMesh each
   * so a close camera culls the sectors behind it; blades in sector order */
  struct { t3_instanced_mesh *im; int start, n; } bpart[BLADE_PARTS];
  uint8_t blade_part[BLADES];
  float blade_x[BLADES], blade_z[BLADES], blade_h[BLADES], blade_lean[BLADES];
  float blade_cy[BLADES], blade_sy[BLADES]; /* cos and sin of half the blade's yaw (fixed) */
  int nblades;
  t3_material *lava, *sea, *neon[4], *windows;
  cw_node weather, storm;
  bool storm_on;
  int ticks;
  /* the crowd: little critter fans on bleachers behind the arena */
  t3_instanced_mesh *fan_body, *fan_head, *fan_ear;
  float fan_x[FANS], fan_y[FANS], fan_z[FANS], fan_phase[FANS];
  int nfans;
  float excite;
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
static t3_mesh *mesh_at(t3_object *g, t3_geometry *geo, t3_material *m, t3_vec3 p, t3_vec3 s) {
  t3_mesh *me = t3_mesh_new(geo, m);
  me->base.position = p;
  me->base.scale = s;
  me->base.cast_shadow = me->base.receive_shadow = true;
  t3_object_add(g, me);
  t3_release(me);
  return me;
}

/* a spot outside the arena (and its rim), inside radius r, kept off the
 * camera's near side: the foreground strip only gets low things */
static t3_vec3 spot(float r, bool tall) {
  for (int tries = 0; tries < 64; tries++) {
    float x = rr(-r, r), z = rr(-r * 0.8f, r * 0.5f);
    if (IABS((int)(x * 10)) < 85 && IABS((int)(z * 10)) < 75) continue; /* the arena */
    if (tall && z > 5.5f) continue;
    if (z < -6.5f && z > -11.2f && x > -9.2f && x < 9.2f) continue; /* the bleachers */
    return (t3_vec3){ x, 0, z };
  }
  return (t3_vec3){ -r, 0, -r * 0.6f };
}

/* scatter n copies of one mesh as an instanced mesh */
static t3_instanced_mesh *scatter(t3_geometry *g, t3_material *m, int n, float r, float smin, float smax, bool tall, const float tint[3],
                                  float tint_var, float yoff) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  im->mesh.base.cast_shadow = im->mesh.base.receive_shadow = true;
  for (int i = 0; i < n; i++) {
    t3_vec3 p = spot(r, tall);
    float s = rr(smin, smax), yaw = rr(0, 6.2831853f);
    float sq = rr(0.85f, 1.15f);
    t3_mat4 mx;
    t3_quat q = { 0, cw_sin(yaw * 0.5f), 0, cw_cos(yaw * 0.5f) };
    t3_mat4_compose(&mx, (t3_vec3){ p.x, p.y + yoff * s, p.z }, q, (t3_vec3){ s, s * sq, s });
    t3_instanced_mesh_set_matrix_at(im, i, &mx);
    float v = 1 - tint_var * rnd();
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ tint[0] * v, tint[1] * v, tint[2] * v });
  }
  hang(&im->mesh.base);
  t3_release(im);
  t3_release(g);
  t3_release(m);
  return im;
}

static void ground(int tex, float tint_r, float tint_g, float tint_b, float rough) {
  t3_material *m = stdm(lin(tint_r, tint_g, tint_b), rough, 0);
  tex_apply(m, tex, 24, 1, 1);
  t3_geometry *g = t3_plane_geometry_new(140, 140, 1, 1);
  t3_mesh *me = t3_mesh_new(g, m);
  t3_object_set_rotation(&me->base, -1.5707963f, 0, 0);
  me->base.position = (t3_vec3){ 0, -0.3f, 0 };
  me->base.receive_shadow = true;
  hang(&me->base);
  t3_release(me);
  t3_release(g);
  t3_release(m);
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
  return n;
}

/* ── meadow ─────────────────────────────────────────────────────────── */

static t3_geometry *blade_geometry(void) {
  /* a tapered, curved blade: 4 segments, two sided by duplication */
  t3_geometry *g = t3_geometry_new();
  enum { SEG = 4 };
  float pos[(SEG + 1) * 2 * 3], nor[(SEG + 1) * 2 * 3], uv[(SEG + 1) * 2 * 2];
  for (int i = 0; i <= SEG; i++) {
    float t = (float)i / SEG, w = 0.035f * (1 - t * 0.92f), y = t, bend = t * t * 0.25f;
    for (int s = 0; s < 2; s++) {
      int k = i * 2 + s;
      pos[k * 3] = s ? w : -w, pos[k * 3 + 1] = y, pos[k * 3 + 2] = bend;
      nor[k * 3] = 0, nor[k * 3 + 1] = 0.3f, nor[k * 3 + 2] = -1;
      uv[k * 2] = (float)s, uv[k * 2 + 1] = t;
    }
  }
  uint16_t idx[SEG * 12];
  int n = 0;
  for (int i = 0; i < SEG; i++) {
    int a = i * 2, b = a + 1, c = a + 2, d = a + 3;
    uint16_t q[12] = { (uint16_t)a, (uint16_t)c, (uint16_t)b, (uint16_t)b, (uint16_t)c, (uint16_t)d,
                       (uint16_t)a, (uint16_t)b, (uint16_t)c, (uint16_t)b, (uint16_t)d, (uint16_t)c };
    memcpy(idx + n, q, sizeof q);
    n += 12;
  }
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, (SEG + 1) * 2, 3));
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nor, (SEG + 1) * 2, 3));
  t3_geometry_set_attribute(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, (SEG + 1) * 2, 2));
  t3_geometry_set_index(g, t3_attribute_new(T3_UINT16, idx, n, 1));
  for (int a = 0; a < T3_ATTR_COUNT; a++)
    if (g->attributes[a]) t3_release(g->attributes[a]); /* the geometry holds them */
  t3_release(g->index);
  return g;
}

static void tree(t3_vec3 p, float s, t3_material *bark, t3_material *leaves) {
  t3_object *g = t3_group_new();
  t3_geometry *trunk = t3_cylinder_geometry_new(0.12f, 0.2f, 1.6f, 10, 1, false);
  mesh_at(g, trunk, bark, (t3_vec3){ 0, 0.8f, 0 }, (t3_vec3){ 1, 1, 1 });
  t3_geometry *ball = t3_icosahedron_geometry_new(1, 2);
  for (int k = 0; k < 4; k++) {
    float a = k * 1.7f + rnd(), r = k ? 0.5f : 0;
    float sz = k ? rr(0.55f, 0.75f) : 0.95f;
    mesh_at(g, ball, leaves, (t3_vec3){ r * cw_cos(a), 1.9f + (k ? rr(-0.2f, 0.3f) : 0.2f), r * cw_sin(a) }, (t3_vec3){ sz, sz * 0.9f, sz });
  }
  t3_release(trunk);
  t3_release(ball);
  g->position = p;
  g->scale = (t3_vec3){ s, s, s };
  hang(g);
  t3_release(g);
}

static void build_meadow(void) {
  ground(TEX_GRASS, 0.9f, 1, 0.85f, 1);
  /* blades of grass in a band round the arena */
  t3_material *gm = phys(lin(1, 1, 1), 0.7f, 0); /* the blade is two-sided geometry */
  gm->sheen = 0.6f;
  gm->sheen_color = lin(0.8f, 1, 0.6f);
  struct { float x, z, h, cy, sy, lean; t3_color c; } *bl = malloc(sizeof *bl * BLADES);
  for (int i = 0; i < BLADES; i++) {
    t3_vec3 p = spot(14, false);
    bl[i].x = p.x;
    bl[i].z = p.z;
    bl[i].h = rr(0.25f, 0.55f);
    float yaw = rr(0, 6.2831853f);
    cw_sincos(yaw * 0.5f, &bl[i].sy, &bl[i].cy);
    bl[i].lean = rr(-0.15f, 0.15f);
    float g = rr(0.75f, 1.1f), y = rnd();
    bl[i].c = lin(0.35f * g + y * 0.12f, 0.62f * g, 0.18f * g);
  }
  t3_geometry *bg = blade_geometry();
  int at = 0;
  for (int b = 0; b < BLADE_PARTS; b++) {
    SC.bpart[b].start = at;
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < BLADES; i++) {
      if (CW_CLAMP((int)((cw_atan2(bl[i].z, bl[i].x) + 3.1415927f) / 6.2831853f * BLADE_PARTS), 0, BLADE_PARTS - 1) != b) continue;
      SC.blade_x[at] = bl[i].x, SC.blade_z[at] = bl[i].z, SC.blade_h[at] = bl[i].h;
      SC.blade_cy[at] = bl[i].cy, SC.blade_sy[at] = bl[i].sy, SC.blade_lean[at] = bl[i].lean;
      SC.blade_part[at] = (uint8_t)b;
      x0 = fminf(x0, bl[i].x), x1 = fmaxf(x1, bl[i].x), z0 = fminf(z0, bl[i].z), z1 = fmaxf(z1, bl[i].z);
      at++;
    }
    int nb = SC.bpart[b].n = at - SC.bpart[b].start;
    if (!nb) continue;
    t3_instanced_mesh *im = t3_instanced_mesh_new(bg, gm, nb);
    for (int i = 0, k = 0; i < BLADES; i++)
      if (CW_CLAMP((int)((cw_atan2(bl[i].z, bl[i].x) + 3.1415927f) / 6.2831853f * BLADE_PARTS), 0, BLADE_PARTS - 1) == b)
        t3_instanced_mesh_set_color_at(im, k++, bl[i].c);
    /* the sector's region with a blade's height (0.55 m at most, leaning and
     * bent by the breeze), set once: not recomputed as the blades sway */
    float dx = (x1 - x0) * 0.5f + 0.6f, dz = (z1 - z0) * 0.5f + 0.6f;
    t3_instanced_mesh_set_bounding_sphere(im, &(t3_sphere){ { (x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f }, sqrtf(dx * dx + dz * dz + 0.36f) });
    im->mesh.base.cast_shadow = false;
    im->mesh.base.receive_shadow = true;
    SC.bpart[b].im = im;
    hang(&im->mesh.base); /* the tree holds it; SC keeps its own reference */
  }
  SC.nblades = BLADES;
  free(bl);
  t3_release(bg);
  t3_release(gm);
  /* flowers: a stem and a five-petal head (instanced heads in many colours) */
  static const float PET[3] = { 1, 1, 1 };
  t3_material *fm = phys(lin(1, 1, 1), 0.5f, 0);
  fm->sheen = 1;
  fm->sheen_color = lin(1, 1, 1);
  scatter(t3_icosahedron_geometry_new(0.09f, 1), fm, 160, 13, 0.8f, 1.3f, false, PET, 0, 0.35f);
  /* round trees and bushes */
  t3_material *bark = stdm(lin(1, 1, 1), 0.9f, 0);
  tex_apply(bark, TEX_BARK, 1, 1, 1);
  t3_material *leaves = phys(lin(0.45f, 0.75f, 0.3f), 0.7f, 0);
  tex_apply(leaves, TEX_LEAF, 2, 1, 1);
  leaves->sheen = 0.5f;
  leaves->sheen_color = lin(0.7f, 1, 0.5f);
  for (int i = 0; i < 9; i++) { /* one draw per statement */
    t3_vec3 at = spot(15, true);
    float sc = rr(1.1f, 1.7f);
    tree(at, sc, bark, leaves);
  }
  static const float BUSH[3] = { 0.5f, 0.8f, 0.35f };
  t3_material *bm = phys(lin(1, 1, 1), 0.7f, 0);
  tex_apply(bm, TEX_LEAF, 2, 1, 1);
  scatter(t3_icosahedron_geometry_new(0.5f, 2), bm, 22, 13, 0.6f, 1.2f, false, BUSH, 0.3f, 0.2f);
  t3_release(bark);
  t3_release(leaves);
  /* the flower heads take random colours */
}

/* ── frost ──────────────────────────────────────────────────────────── */

static void pine(t3_vec3 p, float s, t3_material *needles, t3_material *snow, t3_material *bark) {
  t3_object *g = t3_group_new();
  t3_geometry *trunk = t3_cylinder_geometry_new(0.1f, 0.15f, 0.8f, 8, 1, false);
  mesh_at(g, trunk, bark, (t3_vec3){ 0, 0.4f, 0 }, (t3_vec3){ 1, 1, 1 });
  for (int k = 0; k < 3; k++) {
    float r = 0.9f - k * 0.25f, y = 0.8f + k * 0.65f;
    t3_geometry *cone = t3_cone_geometry_new(r, 1.1f, 14, 1, false);
    mesh_at(g, cone, needles, (t3_vec3){ 0, y + 0.55f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_geometry *cap = t3_cone_geometry_new(r * 0.75f, 0.55f, 14, 1, false);
    mesh_at(g, cap, snow, (t3_vec3){ 0, y + 0.85f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_release(cone);
    t3_release(cap);
  }
  t3_release(trunk);
  g->position = p;
  g->scale = (t3_vec3){ s, s, s };
  hang(g);
  t3_release(g);
}

static const char *const SNOW[] = { "amount", "900", "lifetime", "6", "emission_shape", "BOX", "emission_extents", "vec3(16, 0.5, 12)",
  "direction", "vec3(0, -1, 0)", "spread", "15", "speed_min", "0.5", "speed_max", "1.2", "gravity", "vec3(0.3, -0.4, 0)",
  "size", "0.06", "size_random", "0.5", "color_ramp", "\"0 1 1 1 0, 0.1 1 1 1 0.9, 0.9 1 1 1 0.9, 1 1 1 1 0\"", "energy", "1.4",
  "turbulence", "0.6", "turbulence_scale", "0.3", "preprocess", "6", "blend", "ALPHA", NULL };

static void build_frost(void) {
  ground(TEX_SNOW, 1, 1, 1, 0.8f);
  t3_material *needles = phys(lin(0.2f, 0.42f, 0.32f), 0.8f, 0);
  tex_apply(needles, TEX_LEAF, 3, 1, 1);
  t3_material *snow = phys(lin(1, 1, 1), 0.6f, 0);
  tex_apply(snow, TEX_SNOW, 1, 1, 1);
  snow->sheen = 0.6f;
  snow->sheen_color = lin(0.8f, 0.9f, 1);
  t3_material *bark = stdm(lin(1, 1, 1), 0.9f, 0);
  tex_apply(bark, TEX_BARK, 1, 1, 1);
  for (int i = 0; i < 14; i++) { /* one draw per statement */
    t3_vec3 at = spot(16, true);
    float sc = rr(1.0f, 1.8f);
    pine(at, sc, needles, snow, bark);
  }
  /* snow drifts */
  static const float DRIFT[3] = { 1, 1, 1 };
  t3_material *dm = phys(lin(1, 1, 1), 0.65f, 0);
  tex_apply(dm, TEX_SNOW, 1, 1, 1);
  dm->sheen = 0.5f;
  dm->sheen_color = lin(0.8f, 0.9f, 1);
  scatter(t3_sphere_geometry_new(1, 24, 12), dm, 26, 14, 0.5f, 1.4f, false, DRIFT, 0.05f, -0.35f);
  /* ice crystals: clear, iridescent */
  static const float ICE[3] = { 0.8f, 0.92f, 1 };
  t3_material *im = phys(lin(1, 1, 1), 0.05f, 0);
  im->clearcoat = 1;
  im->iridescence = 0.6f;
  im->iridescence_ior = 1.3f;
  scatter(t3_octahedron_geometry_new(0.4f, 0), im, 18, 13, 0.6f, 1.5f, false, ICE, 0.1f, 0.3f);
  t3_release(needles);
  t3_release(snow);
  t3_release(bark);
  SC.weather = weather(SNOW, 71);
  cw_node3d_set_position(SC.weather, (t3_vec3){ 0, 9, 0 });
  cw_particles_restart(SC.weather);
}

/* ── forge ──────────────────────────────────────────────────────────── */

static const char *const EMBERS[] = { "amount", "300", "lifetime", "4", "emission_shape", "BOX", "emission_extents", "vec3(16, 0.2, 12)",
  "direction", "vec3(0, 1, 0)", "spread", "20", "speed_min", "0.4", "speed_max", "1.2", "gravity", "vec3(0, 0.3, 0)",
  "size", "0.05", "size_random", "0.5", "color_ramp", "\"0 1 0.6 0.2 0, 0.15 1 0.55 0.15 1, 1 1 0.25 0.05 0\"", "energy", "6",
  "turbulence", "1.2", "turbulence_scale", "0.4", "preprocess", "4", "blend", "ADD", NULL };

static void build_forge(void) {
  ground(TEX_BASALT, 0.6f, 0.55f, 0.55f, 0.9f);
  /* lava pools: emissive, breathing */
  SC.lava = stdm(lin(1, 0.35f, 0.05f), 0.3f, 0);
  tex_apply(SC.lava, TEX_BASALT, 2, 0.5f, 1);
  SC.lava->emissive = lin(1, 0.3f, 0.03f);
  SC.lava->emissive_intensity = 2.5f;
  t3_material_set_texture(SC.lava, T3_EMISSIVE_MAP, SC.lava->map);
  t3_retain(SC.lava);
  static const float LAVA[3] = { 1, 1, 1 };
  scatter(t3_cylinder_geometry_new(1, 1, 0.05f, 32, 1, false), SC.lava, 10, 14, 1.0f, 2.2f, false, LAVA, 0, -5.5f);
  /* jagged rocks */
  static const float ROCK[3] = { 0.5f, 0.45f, 0.45f };
  t3_material *rm = stdm(lin(1, 1, 1), 0.85f, 0);
  tex_apply(rm, TEX_BASALT, 1, 1, 1);
  scatter(t3_dodecahedron_geometry_new(0.8f, 0), rm, 26, 15, 0.5f, 1.8f, false, ROCK, 0.3f, 0.2f);
  /* chimneys at the back, glowing at the top */
  t3_material *iron = stdm(lin(0.35f, 0.3f, 0.3f), 0.6f, 0.8f);
  tex_apply(iron, TEX_METAL, 1, 1, 1);
  t3_material *hot = glow(1, 0.4f, 0.08f, 4);
  for (int i = 0; i < 4; i++) {
    t3_object *g = t3_group_new();
    t3_geometry *c = t3_cylinder_geometry_new(0.6f, 0.9f, 6, 16, 1, false);
    mesh_at(g, c, iron, (t3_vec3){ 0, 3, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_geometry *ring = t3_torus_geometry_new(0.6f, 0.12f, 8, 24, 6.2831853f);
    t3_mesh *r = mesh_at(g, ring, hot, (t3_vec3){ 0, 6, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_object_set_rotation(&r->base, 1.5707963f, 0, 0);
    t3_release(c);
    t3_release(ring);
    float jx = rr(-1, 1);
    float jz = rnd() * 2;
    g->position = (t3_vec3){ -9 + i * 6 + jx, 0, -10.5f - jz };
    hang(g);
    t3_release(g);
  }
  t3_release(iron);
  t3_release(hot);
  SC.weather = weather(EMBERS, 73);
  cw_node3d_set_position(SC.weather, (t3_vec3){ 0, 0, 0 });
  cw_particles_restart(SC.weather);
}

/* ── temple ─────────────────────────────────────────────────────────── */

static const char *const FIREFLIES[] = { "amount", "120", "lifetime", "5", "emission_shape", "BOX", "emission_extents", "vec3(15, 1.2, 11)",
  "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.1", "speed_max", "0.4", "gravity", "vec3(0, 0, 0)",
  "size", "0.07", "color_ramp", "\"0 0.8 1 0.4 0, 0.3 0.8 1 0.4 1, 0.6 0.6 1 0.3 0.2, 0.8 0.8 1 0.4 1, 1 0.8 1 0.4 0\"", "energy", "5",
  "turbulence", "1", "turbulence_scale", "0.5", "preprocess", "5", "blend", "ADD", NULL };

static void palm(t3_vec3 p, float s, t3_material *bark, t3_material *frond) {
  t3_object *g = t3_group_new();
  float lean = rr(-0.25f, 0.25f);
  for (int k = 0; k < 6; k++) { /* a segmented, leaning trunk */
    t3_geometry *seg = t3_cylinder_geometry_new(0.13f - k * 0.008f, 0.16f - k * 0.008f, 0.55f, 10, 1, false);
    t3_mesh *m = mesh_at(g, seg, bark, (t3_vec3){ lean * k * k * 0.06f, 0.27f + k * 0.52f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_object_set_rotation(&m->base, 0, 0, -lean * k * 0.12f);
    t3_release(seg);
  }
  t3_vec3 top = { lean * 25 * 0.06f, 3.2f, 0 };
  t3_geometry *leaf = t3_sphere_geometry_new(1, 16, 8);
  for (int k = 0; k < 7; k++) {
    float a = k * 0.8975979f + rnd() * 0.3f;
    t3_mesh *m = mesh_at(g, leaf, frond, (t3_vec3){ top.x + cw_cos(a) * 0.7f, top.y - 0.15f, cw_sin(a) * 0.7f }, (t3_vec3){ 0.9f, 0.05f, 0.25f });
    t3_object_set_rotation(&m->base, 0, -a, -0.45f);
  }
  t3_release(leaf);
  g->position = p;
  g->scale = (t3_vec3){ s, s, s };
  hang(g);
  t3_release(g);
}

static void build_temple(void) {
  ground(TEX_MOSS, 0.85f, 0.95f, 0.8f, 0.9f);
  t3_material *bark = stdm(lin(1, 1, 1), 0.9f, 0);
  tex_apply(bark, TEX_BARK, 1, 1, 1);
  t3_material *frond = phys(lin(0.35f, 0.7f, 0.25f), 0.6f, 0);
  tex_apply(frond, TEX_LEAF, 1, 1, 1);
  frond->sheen = 0.6f;
  frond->sheen_color = lin(0.7f, 1, 0.5f);
  for (int i = 0; i < 10; i++) { /* one draw per statement */
    t3_vec3 at = spot(15, true);
    float sc = rr(1, 1.5f);
    palm(at, sc, bark, frond);
  }
  /* big leafy plants hugging the ground */
  static const float LEAF[3] = { 0.4f, 0.75f, 0.3f };
  t3_material *lm = phys(lin(1, 1, 1), 0.6f, 0);
  tex_apply(lm, TEX_LEAF, 2, 1, 1);
  scatter(t3_sphere_geometry_new(0.6f, 16, 10), lm, 40, 13, 0.5f, 1.2f, false, LEAF, 0.35f, 0.1f);
  /* mossy idols at the back: stacked carved blocks with glowing eyes */
  t3_material *stone = stdm(lin(1, 1, 1), 0.85f, 0);
  tex_apply(stone, TEX_JUNGLE, 1, 1, 1);
  t3_material *eye = glow(0.3f, 1, 0.75f, 4);
  for (int i = 0; i < 3; i++) {
    t3_object *g = t3_group_new();
    t3_geometry *b = geo_rounded_box(1.3f, 1.1f, 1, 0.08f, 3);
    mesh_at(g, b, stone, (t3_vec3){ 0, 0.55f, 0 }, (t3_vec3){ 1, 1, 1 });
    mesh_at(g, b, stone, (t3_vec3){ 0, 1.65f, 0 }, (t3_vec3){ 0.85f, 1, 0.9f });
    t3_geometry *e = t3_sphere_geometry_new(0.12f, 12, 8);
    mesh_at(g, e, eye, (t3_vec3){ -0.25f, 1.8f, 0.46f }, (t3_vec3){ 1, 0.6f, 0.5f });
    mesh_at(g, e, eye, (t3_vec3){ 0.25f, 1.8f, 0.46f }, (t3_vec3){ 1, 0.6f, 0.5f });
    t3_release(b);
    t3_release(e);
    g->position = (t3_vec3){ -7 + i * 7, 0, -9.5f };
    hang(g);
    t3_release(g);
  }
  t3_release(stone);
  t3_release(eye);
  t3_release(bark);
  t3_release(frond);
  SC.weather = weather(FIREFLIES, 79);
  cw_node3d_set_position(SC.weather, (t3_vec3){ 0, 1, 0 });
  cw_particles_restart(SC.weather);
}

/* ── candy ──────────────────────────────────────────────────────────── */

static const char *const SPARKLE[] = { "amount", "160", "lifetime", "3", "emission_shape", "BOX", "emission_extents", "vec3(15, 2, 11)",
  "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.05", "speed_max", "0.2", "gravity", "vec3(0, -0.05, 0)",
  "size", "0.06", "color_ramp", "\"0 1 0.9 1 0, 0.5 1 0.9 1 1, 1 1 0.9 1 0\"", "energy", "4", "preprocess", "3", "blend", "ADD", NULL };

static void build_candy(void) {
  ground(TEX_FROSTING, 1, 0.92f, 0.96f, 0.5f);
  t3_material *stick = phys(lin(1, 1, 1), 0.3f, 0);
  stick->clearcoat = 1;
  static const float HEAD[5][3] = { { 1, 0.4f, 0.55f }, { 0.4f, 0.8f, 1 }, { 1, 0.85f, 0.3f }, { 0.6f, 1, 0.5f }, { 0.8f, 0.5f, 1 } };
  for (int i = 0; i < 12; i++) { /* lollipops */
    t3_object *g = t3_group_new();
    float h = rr(1.6f, 3.2f);
    t3_geometry *st = t3_cylinder_geometry_new(0.06f, 0.06f, h, 8, 1, false);
    mesh_at(g, st, stick, (t3_vec3){ 0, h * 0.5f, 0 }, (t3_vec3){ 1, 1, 1 });
    int c = (int)(rndu() % 5);
    t3_material *candy = phys(lin(HEAD[c][0], HEAD[c][1], HEAD[c][2]), 0.2f, 0);
    tex_apply(candy, TEX_CANDY, 1, 0.5f, 1);
    candy->clearcoat = 1;
    candy->clearcoat_roughness = 0.04f;
    t3_geometry *disc = t3_cylinder_geometry_new(0.65f, 0.65f, 0.18f, 32, 1, false);
    t3_mesh *d = mesh_at(g, disc, candy, (t3_vec3){ 0, h + 0.5f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_object_set_rotation(&d->base, 1.5707963f, 0, 0);
    t3_release(st);
    t3_release(disc);
    t3_release(candy);
    g->position = spot(15, true);
    t3_object_set_rotation(g, 0, rr(-0.6f, 0.6f), 0);
    hang(g);
    t3_release(g);
  }
  t3_release(stick);
  /* gumdrops */
  static const float GUM[3] = { 1, 1, 1 };
  t3_material *gm = phys(lin(1, 1, 1), 0.25f, 0);
  gm->clearcoat = 1;
  gm->sheen = 1;
  gm->sheen_color = lin(1, 0.9f, 0.95f);
  t3_instanced_mesh *gum = scatter(t3_sphere_geometry_new(0.5f, 24, 16), gm, 40, 13, 0.5f, 1.3f, false, GUM, 0, -0.1f);
  for (int i = 0; i < 40; i++) {
    int c = (int)(rndu() % 5);
    t3_instanced_mesh_set_color_at(gum, i, lin(HEAD[c][0], HEAD[c][1], HEAD[c][2]));
  }
  SC.weather = weather(SPARKLE, 83);
  cw_node3d_set_position(SC.weather, (t3_vec3){ 0, 2, 0 });
  cw_particles_restart(SC.weather);
}

/* ── neon ───────────────────────────────────────────────────────────── */

static const char *const RAIN[] = { "amount", "1200", "lifetime", "0.9", "emission_shape", "BOX", "emission_extents", "vec3(16, 0.5, 12)",
  "direction", "vec3(0.1, -1, 0)", "spread", "2", "speed_min", "12", "speed_max", "15", "gravity", "vec3(0, -4, 0)",
  "draw_mode", "STRETCHED", "stretch", "0.03", "size", "0.02", "color_ramp", "\"0 0.7 0.8 1 0.5, 1 0.7 0.8 1 0.5\"", "energy", "1.5",
  "preprocess", "1", "blend", "ADD", NULL };

static void build_neon(void) {
  ground(TEX_ASPHALT, 0.7f, 0.7f, 0.8f, 0.3f);
  /* towers with lit windows: the panel texture as the emissive map */
  SC.windows = stdm(lin(0.25f, 0.25f, 0.32f), 0.4f, 0.5f);
  tex_apply(SC.windows, TEX_PANEL, 3, 1, 1);
  SC.windows->emissive = lin(1, 0.75f, 0.45f);
  SC.windows->emissive_intensity = 0.6f;
  t3_material_set_texture(SC.windows, T3_EMISSIVE_MAP, tex_get(TEX_PANEL)->ao);
  t3_retain(SC.windows);
  static const float TOWER[3] = { 1, 1, 1 };
  t3_instanced_mesh *tw = t3_instanced_mesh_new(geo_rounded_box(1, 1, 1, 0.05f, 2), SC.windows, 22);
  tw->mesh.base.cast_shadow = tw->mesh.base.receive_shadow = true;
  for (int i = 0; i < 22; i++) {
    float x = -15 + i * 1.45f + rr(-0.3f, 0.3f), z = -10.5f - rnd() * 3, w = rr(1.2f, 2.2f), h = rr(3, 9);
    t3_mat4 mx;
    t3_mat4_compose(&mx, (t3_vec3){ x, h * 0.5f, z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ w, h, rr(1.5f, 2.5f) });
    t3_instanced_mesh_set_matrix_at(tw, i, &mx);
    float v = rr(0.7f, 1);
    t3_instanced_mesh_set_color_at(tw, i, (t3_color){ v * TOWER[0], v, v });
  }
  hang(&tw->mesh.base);
  t3_release(tw);
  /* neon signs: glowing rings and bars on poles round the arena */
  static const float NEON[4][3] = { { 1, 0.2f, 0.7f }, { 0.2f, 0.9f, 1 }, { 1, 0.85f, 0.2f }, { 0.6f, 0.3f, 1 } };
  for (int k = 0; k < 4; k++) {
    SC.neon[k] = glow(NEON[k][0], NEON[k][1], NEON[k][2], 5);
    t3_retain(SC.neon[k]);
  }
  t3_material *pole = stdm(lin(0.2f, 0.2f, 0.25f), 0.4f, 0.8f);
  for (int i = 0; i < 10; i++) {
    t3_object *g = t3_group_new();
    float h = rr(2.2f, 3.5f);
    t3_geometry *p = t3_cylinder_geometry_new(0.05f, 0.07f, h, 8, 1, false);
    mesh_at(g, p, pole, (t3_vec3){ 0, h * 0.5f, 0 }, (t3_vec3){ 1, 1, 1 });
    int k = i % 4;
    if (i & 1) {
      t3_geometry *ring = t3_torus_geometry_new(0.45f, 0.06f, 8, 32, 6.2831853f);
      mesh_at(g, ring, SC.neon[k], (t3_vec3){ 0, h + 0.4f, 0 }, (t3_vec3){ 1, 1, 1 });
      t3_release(ring);
    } else {
      t3_geometry *bar = geo_rounded_box(1.2f, 0.12f, 0.12f, 0.05f, 2);
      mesh_at(g, bar, SC.neon[k], (t3_vec3){ 0, h + 0.2f, 0 }, (t3_vec3){ 1, 1, 1 });
      mesh_at(g, bar, SC.neon[(k + 1) % 4], (t3_vec3){ 0, h + 0.5f, 0 }, (t3_vec3){ 0.7f, 1, 1 });
      t3_release(bar);
    }
    t3_release(p);
    g->position = spot(12, true);
    hang(g);
    t3_release(g);
  }
  t3_release(pole);
  SC.weather = weather(RAIN, 89);
  cw_node3d_set_position(SC.weather, (t3_vec3){ 0, 10, 0 });
  cw_particles_restart(SC.weather);
}

/* ── pirate ─────────────────────────────────────────────────────────── */

static void build_pirate(void) {
  /* the sea: glossy, clear-coated, its normal map drifting */
  SC.sea = phys(lin(0.08f, 0.3f, 0.42f), 0.12f, 0);
  tex_apply(SC.sea, TEX_ICE, 18, 0.6f, 1);
  SC.sea->clearcoat = 1;
  SC.sea->clearcoat_roughness = 0.05f;
  t3_retain(SC.sea);
  t3_geometry *pg = t3_plane_geometry_new(160, 160, 1, 1);
  t3_mesh *sea = t3_mesh_new(pg, SC.sea);
  t3_object_set_rotation(&sea->base, -1.5707963f, 0, 0);
  sea->base.position = (t3_vec3){ 0, -0.9f, 0 };
  sea->base.receive_shadow = true;
  hang(&sea->base);
  t3_release(sea);
  t3_release(pg);
  /* the ship round the deck: a hull skirt below the rim */
  t3_material *hull = stdm(lin(1, 1, 1), 0.7f, 0);
  tex_apply(hull, TEX_HULL, 1, 1, 1);
  t3_object *g = t3_group_new();
  t3_geometry *side = geo_rounded_box(17.5f, 1.4f, 15.5f, 0.6f, 4);
  mesh_at(g, side, hull, (t3_vec3){ 0, -0.95f, 0 }, (t3_vec3){ 1, 1, 1 });
  t3_geometry *bow = t3_cone_geometry_new(5.5f, 6, 4, 1, false);
  t3_mesh *bm = mesh_at(g, bow, hull, (t3_vec3){ 0, -0.95f, -10.5f }, (t3_vec3){ 1, 0.23f, 1 });
  t3_object_set_rotation(&bm->base, -1.5707963f, 0.785f, 0);
  t3_release(side);
  t3_release(bow);
  /* masts and sails behind */
  t3_material *wood = stdm(lin(1, 1, 1), 0.8f, 0);
  tex_apply(wood, TEX_BARK, 1, 1, 1);
  t3_material *cloth = phys(lin(0.96f, 0.92f, 0.82f), 0.85f, 0);
  cloth->sheen = 1;
  cloth->sheen_color = lin(1, 0.95f, 0.85f);
  for (int i = 0; i < 2; i++) {
    float x = i ? 4.5f : -4.5f;
    t3_geometry *mast = t3_cylinder_geometry_new(0.18f, 0.25f, 10, 12, 1, false);
    mesh_at(g, mast, wood, (t3_vec3){ x, 5, -8.6f }, (t3_vec3){ 1, 1, 1 });
    t3_geometry *sail = t3_sphere_geometry_new(1, 24, 16);
    mesh_at(g, sail, cloth, (t3_vec3){ x, 6.5f, -8.3f }, (t3_vec3){ 2.4f, 2.2f, 0.35f });
    mesh_at(g, sail, cloth, (t3_vec3){ x, 3.4f, -8.3f }, (t3_vec3){ 2.8f, 1.5f, 0.3f });
    t3_release(mast);
    t3_release(sail);
  }
  hang(g);
  t3_release(g);
  t3_release(hull);
  t3_release(wood);
  t3_release(cloth);
}

/* ── desert ─────────────────────────────────────────────────────────── */

static const char *const SANDSTORM[] = { "amount", "1400", "lifetime", "2.5", "emission_shape", "BOX", "emission_extents",
  "vec3(4, 3, 12)", "direction", "vec3(1, 0.05, 0.2)", "spread", "8", "speed_min", "9", "speed_max", "14", "gravity", "vec3(0, -0.5, 0)",
  "size", "0.5", "size_random", "0.6", "color_ramp", "\"0 0.9 0.75 0.5 0, 0.2 0.9 0.75 0.5 0.35, 1 0.9 0.75 0.5 0\"", "blend", "ALPHA",
  "turbulence", "1.5", "turbulence_scale", "0.2", "soft_distance", "0.4", NULL };

static void cactus(t3_vec3 p, float s, t3_material *m) {
  t3_object *g = t3_group_new();
  t3_geometry *c = t3_cylinder_geometry_new(0.28f, 0.3f, 2.2f, 12, 1, false);
  t3_geometry *cap = t3_sphere_geometry_new(0.28f, 12, 8);
  mesh_at(g, c, m, (t3_vec3){ 0, 1.1f, 0 }, (t3_vec3){ 1, 1, 1 });
  mesh_at(g, cap, m, (t3_vec3){ 0, 2.2f, 0 }, (t3_vec3){ 1, 1, 1 });
  for (int s2 = -1; s2 <= 1; s2 += 2) {
    float y = rr(0.8f, 1.3f);
    t3_mesh *arm = mesh_at(g, c, m, (t3_vec3){ s2 * 0.45f, y, 0 }, (t3_vec3){ 0.6f, 0.25f, 0.6f });
    t3_object_set_rotation(&arm->base, 0, 0, 1.5707963f);
    mesh_at(g, c, m, (t3_vec3){ s2 * 0.72f, y + 0.4f, 0 }, (t3_vec3){ 0.6f, 0.35f, 0.6f });
    mesh_at(g, cap, m, (t3_vec3){ s2 * 0.72f, y + 0.8f, 0 }, (t3_vec3){ 0.6f, 0.6f, 0.6f });
  }
  t3_release(c);
  t3_release(cap);
  g->position = p;
  g->scale = (t3_vec3){ s, s, s };
  t3_object_set_rotation(g, 0, rr(0, 6.28f), 0);
  hang(g);
  t3_release(g);
}

static void build_desert(void) {
  ground(TEX_SAND, 1, 1, 1, 1);
  static const float DUNE[3] = { 1, 0.95f, 0.88f };
  t3_material *dm = stdm(lin(1, 1, 1), 1, 0);
  tex_apply(dm, TEX_SAND, 3, 1.2f, 1);
  t3_instanced_mesh *d = scatter(t3_sphere_geometry_new(1, 32, 16), dm, 16, 18, 2.5f, 5, false, DUNE, 0.08f, -0.75f);
  (void)d;
  t3_material *cm = phys(lin(0.35f, 0.6f, 0.3f), 0.55f, 0);
  tex_apply(cm, TEX_FUR, 2, 2, 1);
  cm->clearcoat = 0.3f;
  for (int i = 0; i < 10; i++) { /* one draw per statement */
    t3_vec3 at = spot(14, true);
    float sc = rr(0.8f, 1.3f);
    cactus(at, sc, cm);
  }
  t3_release(cm);
  /* broken columns */
  t3_material *stone = stdm(lin(1, 1, 1), 0.9f, 0);
  tex_apply(stone, TEX_SANDSTONE, 1, 1, 1);
  for (int i = 0; i < 7; i++) {
    t3_object *g = t3_group_new();
    float h = rr(1.2f, 4.5f);
    t3_geometry *col = t3_cylinder_geometry_new(0.42f, 0.48f, h, 16, 1, false);
    mesh_at(g, col, stone, (t3_vec3){ 0, h * 0.5f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_geometry *base = geo_rounded_box(1.2f, 0.3f, 1.2f, 0.05f, 2);
    mesh_at(g, base, stone, (t3_vec3){ 0, 0.15f, 0 }, (t3_vec3){ 1, 1, 1 });
    t3_release(col);
    t3_release(base);
    g->position = spot(15, true);
    hang(g);
    t3_release(g);
  }
  t3_release(stone);
  SC.storm = weather(SANDSTORM, 97);
  cw_node3d_set_position(SC.storm, (t3_vec3){ -14, 1.5f, 0 });
  cw_node_set(SC.storm, "emitting", cw_val_bool(false));
}

/* ── the crowd ──────────────────────────────────────────────────────── */

static const float FAN_RGB[8][3] = { { 0.95f, 0.5f, 0.2f }, { 0.97f, 0.95f, 0.93f }, { 0.6f, 0.4f, 0.27f }, { 0.45f, 0.8f, 0.35f },
                                     { 0.95f, 0.75f, 0.3f }, { 0.55f, 0.6f, 0.7f }, { 0.9f, 0.6f, 0.75f }, { 0.4f, 0.55f, 0.85f } };
static const int SEAT_TEX[TH_KINDS] = { TEX_BRICK, TEX_SNOW, TEX_METAL, TEX_JUNGLE, TEX_CANDY, TEX_PANEL, TEX_DECK, TEX_SANDSTONE };

static void build_crowd(int theme) {
  /* three tiers of bleachers along the back, in the arena's material */
  t3_material *seat = stdm(lin(1, 1, 1), 0.7f, theme == TH_FORGE || theme == TH_NEON ? 0.5f : 0);
  tex_apply(seat, SEAT_TEX[theme], 4, 1, 1);
  t3_object *g = t3_group_new();
  for (int k = 0; k < 3; k++) {
    t3_geometry *step = geo_rounded_box(17, 0.55f + k * 0.55f, 0.9f, 0.08f, 3);
    mesh_at(g, step, seat, (t3_vec3){ 0, -0.3f + (0.55f + k * 0.55f) * 0.5f, -7.9f - k * 0.9f }, (t3_vec3){ 1, 1, 1 });
    t3_release(step);
  }
  /* bunting: a row of little lacquered flags over the top tier */
  static const float FLAG[4][3] = { { 1, 0.3f, 0.25f }, { 0.25f, 0.6f, 1 }, { 1, 0.8f, 0.15f }, { 0.4f, 0.9f, 0.35f } };
  t3_geometry *pole = t3_cylinder_geometry_new(0.04f, 0.05f, 2.2f, 8, 1, false);
  t3_material *wood = stdm(lin(0.5f, 0.35f, 0.2f), 0.8f, 0);
  for (int i = 0; i < 7; i++) {
    float x = -7.5f + i * 2.5f;
    mesh_at(g, pole, wood, (t3_vec3){ x, 1.75f, -10.1f }, (t3_vec3){ 1, 1, 1 });
    t3_material *fm = phys(lin(FLAG[i % 4][0], FLAG[i % 4][1], FLAG[i % 4][2]), 0.5f, 0);
    fm->sheen = 1;
    fm->sheen_color = lin(1, 1, 1);
    t3_geometry *flag = t3_cone_geometry_new(0.28f, 0.6f, 3, 1, false);
    t3_mesh *f = mesh_at(g, flag, fm, (t3_vec3){ x + 0.32f, 2.65f, -10.1f }, (t3_vec3){ 1, 1, 0.15f });
    t3_object_set_rotation(&f->base, 0, 0, -1.5707963f);
    t3_release(flag);
    t3_release(fm);
  }
  t3_release(pole);
  t3_release(wood);
  hang(g);
  t3_release(g);
  t3_release(seat);
  /* the fans: a body, a head and two ears each, instanced, coloured from a palette */
  t3_material *fur = phys(lin(1, 1, 1), 0.85f, 0);
  fur->sheen = 0.8f;
  fur->sheen_color = lin(1, 1, 1);
  SC.fan_body = t3_instanced_mesh_new(t3_sphere_geometry_new(0.2f, 16, 12), fur, FANS);
  SC.fan_head = t3_instanced_mesh_new(t3_sphere_geometry_new(0.17f, 16, 12), fur, FANS);
  SC.fan_ear = t3_instanced_mesh_new(t3_sphere_geometry_new(0.06f, 8, 6), fur, FANS * 2);
  t3_release(fur);
  t3_instanced_mesh *ims[3] = { SC.fan_body, SC.fan_head, SC.fan_ear };
  for (int k = 0; k < 3; k++) {
    t3_release(ims[k]->mesh.geometry); /* the mesh holds it */
    ims[k]->mesh.base.cast_shadow = true;
    ims[k]->mesh.base.receive_shadow = true;
  }
  int n = 0;
  for (int row = 0; row < 3; row++)
    for (int i = 0; i < FANS / 3; i++) {
      float jx = rr(-0.18f, 0.18f);
      SC.fan_x[n] = -7.9f + i * (15.8f / (FANS / 3 - 1)) + jx;
      SC.fan_y[n] = -0.3f + (0.55f + row * 0.55f);
      SC.fan_z[n] = -7.85f - row * 0.9f;
      SC.fan_phase[n] = rr(0, 6.2831853f);
      int c = (int)(rndu() % 8);
      t3_color col = lin(FAN_RGB[c][0], FAN_RGB[c][1], FAN_RGB[c][2]);
      t3_instanced_mesh_set_color_at(SC.fan_body, n, col);
      t3_instanced_mesh_set_color_at(SC.fan_head, n, col);
      t3_instanced_mesh_set_color_at(SC.fan_ear, n * 2, col);
      t3_instanced_mesh_set_color_at(SC.fan_ear, n * 2 + 1, col);
      n++;
    }
  SC.nfans = n;
  for (int k = 0; k < 3; k++) {
    t3_retain(ims[k]);
    hang(&ims[k]->mesh.base);
    t3_release(ims[k]);
  }
}

static void crowd_tick(const sim *s, int ticks) {
  if (!SC.fan_body) return;
  if (s)
    for (int i = 0; i < s->ev_n; i++) { /* the crowd goes wild for booms and knockouts */
      int k = s->ev[i].kind;
      if (k == EV_BOOM) SC.excite = fminf(1, SC.excite + 0.25f);
      if (k == EV_KO) SC.excite = 1;
      if (k == EV_PICK) SC.excite = fminf(1, SC.excite + 0.1f);
    }
  SC.excite *= 0.985f;
  if (ticks & 1) return;
  for (int i = 0; i < SC.nfans; i++) {
    float ph = ticks * (0.12f + 0.1f * SC.excite) + SC.fan_phase[i];
    float hop = fabsf(cw_sin(ph)) * (0.03f + 0.28f * SC.excite);
    float sway = cw_sin(ticks * 0.03f + SC.fan_phase[i]) * 0.06f;
    float x = SC.fan_x[i] + sway, y = SC.fan_y[i] + hop, z = SC.fan_z[i];
    t3_mat4 m;
    t3_mat4_compose(&m, (t3_vec3){ x, y + 0.18f, z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1.1f, 1 });
    t3_instanced_mesh_set_matrix_at(SC.fan_body, i, &m);
    t3_mat4_compose(&m, (t3_vec3){ x, y + 0.48f, z + 0.02f }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
    t3_instanced_mesh_set_matrix_at(SC.fan_head, i, &m);
    for (int e = 0; e < 2; e++) {
      t3_mat4_compose(&m, (t3_vec3){ x + (e ? 0.11f : -0.11f), y + 0.63f, z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1.3f, 0.7f });
      t3_instanced_mesh_set_matrix_at(SC.fan_ear, i * 2 + e, &m);
    }
  }
  SC.fan_body->instance_matrix->version++;
  SC.fan_head->instance_matrix->version++;
  SC.fan_ear->instance_matrix->version++;
}

/* ── the public side ────────────────────────────────────────────────── */

void scenery_reset(void) {
  if (SC.ready) {
    for (int b = 0; b < BLADE_PARTS; b++)
      if (SC.bpart[b].im) t3_release(SC.bpart[b].im);
    if (SC.lava) t3_release(SC.lava);
    if (SC.sea) t3_release(SC.sea);
    if (SC.windows) t3_release(SC.windows);
    if (SC.fan_body) t3_release(SC.fan_body), t3_release(SC.fan_head), t3_release(SC.fan_ear);
    for (int k = 0; k < 4; k++)
      if (SC.neon[k]) t3_release(SC.neon[k]);
  }
  memset(&SC, 0, sizeof SC);
}

static void apply_density(void);
void scenery_build(cw_node scene, int theme, uint32_t seed) {
  scenery_reset();
  SC.theme = theme;
  seed_state = seed * 2654435761u + 12345u;
  if (!seed_state) seed_state = 1;
  scenery_hash = 2166136261u;
  SC.root = cw_node_new("Node3D", "Scenery");
  cw_node_add_child(scene, SC.root);
  switch (theme) {
  case TH_MEADOW: build_meadow(); break;
  case TH_FROST: build_frost(); break;
  case TH_FORGE: build_forge(); break;
  case TH_TEMPLE: build_temple(); break;
  case TH_CANDY: build_candy(); break;
  case TH_NEON: build_neon(); break;
  case TH_PIRATE: build_pirate(); break;
  case TH_DESERT: build_desert(); break;
  }
  build_crowd(theme);
  crowd_tick(NULL, 0);
  SC.ready = true;
  apply_density();
}

/* the share of the meadow's blades and the crowd drawn (cw_quality_density):
 * the instances past it are neither drawn nor swayed */
static float density = 1;
static void apply_density(void) {
  float k = density < 0.05f ? 0.05f : density > 1 ? 1 : density;
  for (int b = 0; b < BLADE_PARTS; b++)
    if (SC.bpart[b].im) SC.bpart[b].im->count = (int)(SC.bpart[b].n * k);
  if (SC.fan_body) {
    int n = (int)(SC.nfans * k);
    SC.fan_body->count = SC.fan_head->count = n;
    SC.fan_ear->count = n * 2;
  }
}
void scenery_density(float k) {
  density = k;
  if (SC.ready) apply_density();
}

/* blades [begin, end) bent by the breeze at time *(float *)ctx: each one
 * from its own fields alone, so the result is the same on any thread */
static void sway(int begin, int end, int worker, void *ctx) {
  (void)worker;
  float t = *(const float *)ctx;
  for (int i = begin; i < end; i++) {
    float x = SC.blade_x[i], z = SC.blade_z[i];
    float gust = cw_sin(x * 0.35f + z * 0.2f - t) * 0.5f + 0.5f;
    float bend = SC.blade_lean[i] + 0.22f * gust + 0.05f * cw_sin(t * 3 + i);
    float cy = SC.blade_cy[i], sy = SC.blade_sy[i], cb, sb;
    cw_sincos(bend * 0.5f, &sb, &cb);
    /* a quaternion from yaw then a bend about the blade's x axis */
    t3_quat q = { cy * sb, sy * cb, -sy * sb, cy * cb };
    t3_mat4 mx;
    t3_mat4_compose(&mx, (t3_vec3){ x, -0.3f, z }, q, (t3_vec3){ 1, SC.blade_h[i], 1 });
    int b = SC.blade_part[i], li = i - SC.bpart[b].start;
    if (li < SC.bpart[b].im->count) t3_instanced_mesh_set_matrix_at(SC.bpart[b].im, li, &mx);
  }
}

void scenery_tick(const sim *s, int ticks) {
  if (!SC.ready) return;
  SC.ticks = ticks;
  crowd_tick(s, ticks);
  /* the breeze: a slow wave travelling across the meadow (every other tick),
   * the blades shared out over the job pool */
  if (SC.nblades && (ticks & 1) == 0) {
    float t = ticks * 0.025f;
    cw_parallel_for(SC.nblades, 256, sway, &t); /* the blades not drawn are skipped */
    for (int b = 0; b < BLADE_PARTS; b++)
      if (SC.bpart[b].im) SC.bpart[b].im->instance_matrix->version++;
  }
  if (SC.lava) SC.lava->emissive_intensity = 2.2f + 0.8f * cw_sin(ticks * 0.025f);
  if (SC.sea) {
    t3_texture *maps[2] = { SC.sea->normal_map, SC.sea->map };
    for (int k = 0; k < 2; k++)
      if (maps[k]) maps[k]->offset = (t3_vec2){ ticks * 0.0004f, ticks * 0.00025f };
  }
  if (SC.neon[0]) /* neon flicker: now and then one sign stutters */
    for (int k = 0; k < 4; k++) {
      int ph = (ticks + k * 97) % 400;
      SC.neon[k]->emissive_intensity = ph < 12 && (ph / 3) % 2 ? 0.6f : 5 + 0.6f * cw_sin(ticks * 0.05f + k);
    }
  if (cw_node_valid(SC.storm)) {
    bool on = s && s->storm > 0;
    if (on && !SC.storm_on) cw_particles_restart(SC.storm);
    if (!on && SC.storm_on) cw_node_set(SC.storm, "emitting", cw_val_bool(false));
    SC.storm_on = on;
  }
}
