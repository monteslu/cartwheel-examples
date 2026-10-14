/* bomberdude effects: the blasts are the star.
 *
 *   blast     each explosion takes the next of BUNDLES pooled bundles of
 *             GPUParticles3D: a white-hot burst at the bomb, a sheet of
 *             flame along each arm of the cross (box emitters sized to the
 *             arm), sparks that bounce off the scene's depth, embers and a
 *             smoke column; plus the fire cross itself as two layers of
 *             emissive instanced blobs (a glowing core inside an orange
 *             skin) that swell, flicker and die with the simulation's fire
 *   crates    splinter into instanced planks that tumble and bounce (CPU,
 *             fixed tick, cartwheel math), with a dust puff
 *   decals    scorch marks under every blast (Decal3D), fading slowly
 *   other     sparkles on pick-ups, a bubble's pop, knockout stars and fur,
 *             teleport glints, dust where sudden-death walls land; camera
 *             shake on booms
 * All of it reads the simulation's events; nothing here feeds the rules. */
#include <stdio.h>
#include <string.h>

#include "cartwheel/decals.h"
#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "world.h"

#define BUNDLES 6
#define SHARDS 192
#define SCORCH 16
#define MAXI (GW * GH)

typedef struct bundle {
  cw_node burst, arm[4], sparks, embers, smoke;
} bundle;

typedef struct shard { t3_vec3 p, v, rot, spin, size; t3_color col; bool new_col; int life; } shard;

static struct {
  bool ready;
  int theme;
  cw_node root;
  bundle b[BUNDLES];
  int next_b;
  cw_node dust[4], sparkle[4], pop[2], stars[2], fur[2], glint[2], confetti[4];
  int dust_i, sparkle_i, pop_i, stars_i, fur_i, glint_i;
  cw_node scorch[SCORCH];
  float scorch_a[SCORCH];
  int scorch_i;
  t3_instanced_mesh *flame, *core, *planks;
  shard s[SHARDS];
  int next_s;
  int16_t plank_of[SHARDS]; /* the shard each drawn plank instance shows + 1 (its colour is there), 0 none */
  int shake;
  int ticks;
  cw_node flash[3];
  int flash_age[3];
  int next_flash;
} FX;

static const t3_mat4 ZERO;

static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  return cw_particle_hash(a * 0x9e3779b9u ^ cw_particle_hash(b + 0x85ebca6bu * c));
}
static float rnd(uint32_t h) { return (float)(h & 0xffffff) / 16777216.0f; }

static cw_node pnode(cw_node parent, const char *name, const char *const *kv, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(parent, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) {
      cw_node_set(n, kv[i], cw_val_enum(kv[i + 1]));
      continue;
    }
    if (!p || !cw_value_parse(kv[i + 1], p->type, &v)) {
      cw_warn("bomberdude: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  return n;
}

static void burst_at(cw_node n, t3_vec3 at) {
  cw_node3d_set_position(n, at);
  cw_node3d_global_transform(n); /* the emitter's world matrix now: it emits this tick, not where it was last */
  cw_particles_restart(n);
}

/* ── particle recipes ───────────────────────────────────────────────── */

static const char *const BURST[] = { "amount", "140", "lifetime", "0.55", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.25", "spread", "180", "speed_min", "1.2", "speed_max", "4",
  "gravity", "vec3(0, 2, 0)", "damping", "4", "size", "0.62", "size_random", "0.5", "size_curve", "\"0 0.5, 0.2 1.2, 1 0.2\"",
  "color_ramp", "\"0 1 0.9 0.6 1, 0.15 1 0.6 0.15 1, 0.5 0.9 0.25 0.04 0.8, 1 0.4 0.05 0.02 0\"", "energy", "2.6", "blend", "ADD",
  "turbulence", "1.5", "turbulence_scale", "0.6", NULL };
static const char *const ARM[] = { "amount", "110", "lifetime", "0.5", "lifetime_random", "0.3", "explosiveness", "0.9",
  "one_shot", "true", "emission_shape", "BOX", "emission_extents", "vec3(1, 0.12, 0.2)", "direction", "vec3(0, 1, 0)",
  "spread", "50", "speed_min", "0.3", "speed_max", "1.6", "gravity", "vec3(0, 2.5, 0)", "damping", "2", "size", "0.55",
  "size_random", "0.4", "size_curve", "\"0 0.6, 0.25 1.1, 1 0.1\"",
  "color_ramp", "\"0 1 0.85 0.5 1, 0.2 1 0.5 0.1 1, 0.6 0.85 0.18 0.03 0.7, 1 0.35 0.04 0.02 0\"", "energy", "2.2", "blend", "ADD",
  "turbulence", "1.2", "turbulence_scale", "0.5", NULL };
static const char *const SPARKS[] = { "amount", "120", "lifetime", "0.9", "lifetime_random", "0.4", "explosiveness", "1",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.2", "spread", "180", "speed_min", "4", "speed_max", "10",
  "direction", "vec3(0, 1, 0)", "gravity", "vec3(0, -12, 0)", "damping", "0.6", "draw_mode", "STRETCHED", "stretch", "0.04",
  "size", "0.05", "color_ramp", "\"0 1 0.95 0.7 1, 0.3 1 0.6 0.15 1, 1 0.7 0.12 0.04 0\"", "energy", "5", "bounce", "0.45",
  "collision_radius", "0.03", "collide_depth", "true", NULL };
static const char *const EMBERS[] = { "amount", "60", "lifetime", "1.8", "lifetime_random", "0.5", "explosiveness", "0.8",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.6", "spread", "180", "speed_min", "0.5", "speed_max", "2",
  "gravity", "vec3(0, 0.8, 0)", "damping", "1", "size", "0.06", "size_random", "0.5",
  "color_ramp", "\"0 1 0.7 0.3 1, 0.7 1 0.4 0.1 1, 1 0.6 0.1 0.02 0\"", "energy", "6", "blend", "ADD",
  "turbulence", "2", "turbulence_scale", "0.4", NULL };
static const char *const SMOKE[] = { "amount", "70", "lifetime", "2.2", "lifetime_random", "0.3", "explosiveness", "0.85",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.5", "spread", "180", "speed_min", "0.3", "speed_max", "1.4",
  "gravity", "vec3(0, 1.1, 0)", "damping", "1.2", "size", "1.0", "size_random", "0.5", "size_curve", "\"0 0.2, 0.3 1, 1 1.8\"",
  "color_ramp", "\"0 0.2 0.18 0.17 0, 0.18 0.22 0.2 0.19 0.55, 1 0.45 0.43 0.42 0\"", "blend", "ALPHA", "soft_distance", "0.5",
  "turbulence", "1", "turbulence_scale", "0.35", NULL };
static const char *const DUST[] = { "amount", "60", "lifetime", "1.1", "explosiveness", "0.95", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.35", "spread", "180", "speed_min", "0.3", "speed_max", "1.8",
  "gravity", "vec3(0, 0.3, 0)", "damping", "2", "size", "0.45", "size_random", "0.5", "size_curve", "\"0 0.4, 0.3 1, 1 1.4\"",
  "color_ramp", "\"0 0.8 0.68 0.5 0, 0.1 0.8 0.7 0.55 0.5, 1 0.85 0.78 0.65 0\"", "blend", "ALPHA", "soft_distance", "0.3", NULL };
static const char *const SPARKLE[] = { "amount", "50", "lifetime", "0.8", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.25", "spread", "180", "speed_min", "1", "speed_max", "3",
  "direction", "vec3(0, 1, 0)", "gravity", "vec3(0, 1.5, 0)", "damping", "2.5", "size", "0.12", "size_random", "0.5",
  "size_curve", "\"0 1, 1 0\"", "color_ramp", "\"0 1 1 1 1, 1 1 0.9 0.5 0\"", "energy", "5", "blend", "ADD", NULL };
static const char *const POP[] = { "amount", "40", "lifetime", "0.6", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE_SURFACE", "emission_radius", "0.55", "spread", "180", "speed_min", "1.5", "speed_max", "3",
  "gravity", "vec3(0, -3, 0)", "size", "0.08", "color_ramp", "\"0 0.7 1 1 1, 0.5 1 0.7 1 1, 1 1 1 0.6 0\"", "energy", "4",
  "blend", "ADD", NULL };
static const char *const STARS[] = { "amount", "14", "lifetime", "1.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "RING", "emission_radius", "0.4", "direction", "vec3(0, 1, 0)", "spread", "30", "speed_min", "2",
  "speed_max", "3", "gravity", "vec3(0, -4, 0)", "size", "0.18", "spin", "6",
  "color_ramp", "\"0 1 0.95 0.4 1, 0.7 1 0.85 0.3 1, 1 1 0.8 0.2 0\"", "energy", "4", "blend", "ADD", NULL };
static const char *const FUR[] = { "amount", "36", "lifetime", "1.6", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.2", "spread", "180", "speed_min", "1.5", "speed_max", "4",
  "gravity", "vec3(0, -1.5, 0)", "damping", "2", "draw_mode", "MESH", "mesh_primitive", "SPHERE", "size", "0.07",
  "size_random", "0.5", "spin", "5", "turbulence", "1.5", "turbulence_scale", "0.5", "blend", "OPAQUE",
  "color_ramp", "\"0 1 1 1 1, 1 1 1 1 1\"", NULL };
static const char *const CONFETTI[] = { "amount", "160", "lifetime", "4", "lifetime_random", "0.4", "explosiveness", "0.6",
  "one_shot", "false", "emission_shape", "BOX", "emission_extents", "vec3(3.5, 0.2, 2)", "direction", "vec3(0, 1, 0)", "spread", "40",
  "speed_min", "1", "speed_max", "3.5", "gravity", "vec3(0, -1.6, 0)", "damping", "1.2", "draw_mode", "MESH", "mesh_primitive", "BOX",
  "size", "0.07", "size_random", "0.3", "spin", "8", "turbulence", "2", "turbulence_scale", "0.6", "blend", "OPAQUE", NULL };
static const char *const GLINT[] = { "amount", "40", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "RING", "emission_radius", "0.35", "direction", "vec3(0, 1, 0)", "spread", "10", "speed_min", "1.5",
  "speed_max", "3", "size", "0.08", "color_ramp", "\"0 0.4 1 0.8 1, 1 0.3 1 0.7 0\"", "energy", "6", "blend", "ADD", NULL };

void fx_reset(void) {
  if (FX.ready) {
    t3_release(FX.flame);
    t3_release(FX.core);
    t3_release(FX.planks);
  }
  memset(&FX, 0, sizeof FX);
}

static t3_instanced_mesh *imesh(const char *name, t3_geometry *g, t3_material *m, int n) {
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  for (int i = 0; i < n; i++) {
    t3_instanced_mesh_set_matrix_at(im, i, &ZERO);
    t3_instanced_mesh_set_color_at(im, i, (t3_color){ 1, 1, 1 });
  }
  cw_node node = cw_node_new("Node3D", name);
  cw_node_add_child(FX.root, node);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, &im->mesh.base);
  cw_node3d_set_object(node, wrap);
  t3_release(wrap);
  t3_release(g);
  t3_release(m);
  return im;
}

void fx_init(cw_node scene, int theme) {
  fx_reset();
  FX.theme = theme;
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(scene, FX.root);
  for (int i = 0; i < BUNDLES; i++) {
    bundle *b = &FX.b[i];
    b->burst = pnode(FX.root, "Burst", BURST, 100 + i);
    for (int k = 0; k < 4; k++) b->arm[k] = pnode(FX.root, "Arm", ARM, 200 + i * 4 + k);
    b->sparks = pnode(FX.root, "Sparks", SPARKS, 300 + i);
    b->embers = pnode(FX.root, "Embers", EMBERS, 400 + i);
    b->smoke = pnode(FX.root, "Smoke", SMOKE, 500 + i);
  }
  for (int i = 0; i < 4; i++) FX.dust[i] = pnode(FX.root, "Dust", DUST, 600 + i);
  for (int i = 0; i < 4; i++) FX.sparkle[i] = pnode(FX.root, "Sparkle", SPARKLE, 700 + i);
  for (int i = 0; i < 2; i++) FX.pop[i] = pnode(FX.root, "Pop", POP, 800 + i);
  for (int i = 0; i < 2; i++) FX.stars[i] = pnode(FX.root, "Stars", STARS, 900 + i);
  for (int i = 0; i < 2; i++) FX.fur[i] = pnode(FX.root, "Fur", FUR, 950 + i);
  for (int i = 0; i < 2; i++) FX.glint[i] = pnode(FX.root, "Glint", GLINT, 980 + i);
  static const char *const CC[4] = { "\"0 1 0.3 0.35 1, 1 1 0.3 0.35 1\"", "\"0 0.3 0.7 1 1, 1 0.3 0.7 1 1\"",
                                     "\"0 1 0.85 0.2 1, 1 1 0.85 0.2 1\"", "\"0 0.45 0.95 0.4 1, 1 0.45 0.95 0.4 1\"" };
  for (int i = 0; i < 4; i++) {
    FX.confetti[i] = pnode(FX.root, "Confetti", CONFETTI, 990 + i);
    cw_value v;
    if (cw_value_parse(CC[i], CW_T_STRING, &v)) cw_node_set(FX.confetti[i], "color_ramp", v);
  }
  for (int i = 0; i < SCORCH; i++) {
    FX.scorch[i] = cw_node_new("Decal3D", "Scorch");
    cw_node_add_child(FX.root, FX.scorch[i]);
    cw_node_set(FX.scorch[i], "blend", cw_val_enum("MULTIPLY"));
    cw_node_set(FX.scorch[i], "color", cw_val_color(0.2f, 0.16f, 0.13f, 0));
    cw_node_set(FX.scorch[i], "normal_fade", cw_val_float(0.3f));
    cw_node_set(FX.scorch[i], "edge_fade", cw_val_float(0.35f));
    cw_node3d_set_rotation(FX.scorch[i], (t3_vec3){ -1.5707963f, 0, 0 });
    cw_node3d_set_position(FX.scorch[i], (t3_vec3){ 0, -50, 0 });
  }
  /* the fire cross: an orange skin and a white-hot core, emissive (they bloom) */
  t3_material *fm = t3_mesh_standard_material_new(0xffffff);
  fm->color = lin(0.12f, 0.03f, 0);
  fm->emissive = lin(1, 0.3f, 0.0f);
  fm->emissive_intensity = 2.2f;
  fm->roughness = 1;
  fm->transparent = true;
  fm->opacity = 0.85f;
  fm->depth_write = false;
  FX.flame = imesh("Flame", t3_icosahedron_geometry_new(0.5f, 2), fm, MAXI);
  FX.flame->mesh.base.render_order = 5;
  t3_material *cm = t3_mesh_standard_material_new(0xffffff);
  cm->color = lin(0.15f, 0.1f, 0);
  cm->emissive = lin(1, 0.72f, 0.12f);
  cm->emissive_intensity = 3.0f;
  cm->roughness = 1;
  FX.core = imesh("Core", t3_icosahedron_geometry_new(0.5f, 2), cm, MAXI);
  /* crate planks */
  t3_material *pm = t3_mesh_standard_material_new(0xffffff);
  pm->roughness = 0.8f;
  tex_apply(pm, FX.theme == TH_CANDY ? TEX_CANDY : TEX_CRATE, 1, 1, 1);
  FX.planks = imesh("Planks", geo_rounded_box(1, 1, 1, 0.08f, 2), pm, SHARDS);
  FX.planks->mesh.base.cast_shadow = true;
  FX.planks->mesh.base.receive_shadow = true;
  /* the scene's three flash lights (always present, dark until a blast:
   * a light that came and went would change every material's program) */
  for (int i = 0; i < 3; i++) {
    char name[16];
    snprintf(name, sizeof name, "Flash%d", i);
    FX.flash[i] = cw_node_get(scene, name);
  }
  FX.ready = true;
}

int fx_shake(void) { return FX.shake; }

void fx_confetti(t3_vec3 at) {
  if (!FX.ready) return;
  for (int i = 0; i < 4; i++) burst_at(FX.confetti[i], (t3_vec3){ at.x + (i - 1.5f) * 0.4f, at.y, at.z });
}

/* an arm's length: fire cells in a row from the centre */
static int arm_len(const sim *s, int cx, int cy, int dx, int dy, int range) {
  int n = 0;
  for (int k = 1; k <= range; k++) {
    int x = cx + dx * k, y = cy + dy * k;
    if (x < 0 || y < 0 || x >= GW || y >= GH || s->fire[y][x] <= 0) break;
    n = k;
  }
  return n;
}

static void blast(const sim *s, int cx, int cy, int range) {
  bundle *b = &FX.b[FX.next_b++ % BUNDLES];
  t3_vec3 c = { tx_w(cx), 0.35f, tz_w(cy) };
  burst_at(b->burst, c);
  burst_at(b->sparks, c);
  burst_at(b->embers, c);
  burst_at(b->smoke, (t3_vec3){ c.x, 0.6f, c.z });
  static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, -1, 0, 1 };
  for (int k = 0; k < 4; k++) {
    int n = arm_len(s, cx, cy, DX[k], DY[k], range);
    if (n == 0) {
      cw_node3d_set_position(b->arm[k], (t3_vec3){ 0, -50, 0 });
      continue;
    }
    float len = (float)n;
    t3_vec3 mid = { c.x + DX[k] * len * 0.5f + DX[k] * 0.3f, 0.25f, c.z + DY[k] * len * 0.5f + DY[k] * 0.3f };
    cw_node_set(b->arm[k], "emission_extents", cw_val_vec3(len * 0.5f, 0.12f, 0.2f));
    cw_node_set(b->arm[k], "amount", cw_val_int(40 + n * 35));
    cw_node3d_set_rotation(b->arm[k], (t3_vec3){ 0, DX[k] ? 0 : 1.5707963f, 0 });
    burst_at(b->arm[k], mid);
  }
  /* scorch */
  int si = FX.scorch_i++ % SCORCH;
  cw_node d = FX.scorch[si];
  uint32_t h = hash3((uint32_t)cx, (uint32_t)cy, (uint32_t)s->tick);
  cw_node3d_set_position(d, (t3_vec3){ c.x, 0.5f, c.z });
  cw_node3d_set_rotation(d, (t3_vec3){ -1.5707963f, 0, rnd(h) * 6.28f });
  float sz = 1.3f + 0.35f * rnd(h >> 5);
  cw_node_set(d, "size", cw_val_vec3(sz, sz, 1.4f));
  FX.scorch_a[si] = 0.85f;
  FX.shake = CW_MIN(FX.shake + 7, 16);
  int fi = FX.next_flash++ % 3;
  if (cw_node_valid(FX.flash[fi])) {
    cw_node3d_set_position(FX.flash[fi], (t3_vec3){ c.x, 1.1f, c.z });
    FX.flash_age[fi] = 1;
  }
}

static void splinter(const sim *s, int cx, int cy) {
  t3_vec3 at = { tx_w(cx), 0.42f, tz_w(cy) };
  for (int k = 0; k < 8; k++) {
    shard *sh = &FX.s[FX.next_s];
    FX.next_s = (FX.next_s + 1) % SHARDS;
    uint32_t h = hash3((uint32_t)(cx * 31 + cy), (uint32_t)k, (uint32_t)s->tick);
    sh->p = (t3_vec3){ at.x + (rnd(h) - 0.5f) * 0.6f, at.y + (rnd(h >> 3) - 0.5f) * 0.5f, at.z + (rnd(h >> 6) - 0.5f) * 0.6f };
    sh->v = (t3_vec3){ (rnd(h >> 9) - 0.5f) * 6, 2.5f + rnd(h >> 12) * 4.5f, (rnd(h >> 15) - 0.5f) * 6 };
    sh->spin = (t3_vec3){ (rnd(h >> 18) - 0.5f) * 16, (rnd(h >> 21) - 0.5f) * 16, (rnd(h >> 24) - 0.5f) * 16 };
    sh->rot = (t3_vec3){ 0, 0, 0 };
    float f = 0.7f + rnd(h >> 27) * 0.6f;
    sh->size = FX.theme == TH_CANDY ? (t3_vec3){ 0.2f * f, 0.2f * f, 0.2f * f } : (t3_vec3){ 0.5f * f, 0.07f, 0.12f * f };
    sh->life = 110 + (int)(rnd(h >> 13) * 50);
    float t = 0.7f + rnd(h >> 16) * 0.4f;
    t3_color col = FX.theme == TH_CANDY ? lin(1, 0.5f + 0.4f * rnd(h >> 2), 0.6f + 0.3f * rnd(h >> 7)) : (t3_color){ t, t * 0.85f, t * 0.7f };
    sh->col = col;
    sh->new_col = true;
  }
  burst_at(FX.dust[FX.dust_i++ % 4], at);
}

void fx_events(const sim *s) {
  if (!FX.ready) return;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    t3_vec3 at = { tx_w(e->x), 0.4f, tz_w(e->y) };
    switch (e->kind) {
    case EV_BOOM: blast(s, e->x, e->y, e->b); break;
    case EV_VENT: blast(s, e->x, e->y, 1); break;
    case EV_CANNON: FX.shake = CW_MIN(FX.shake + 5, 16); burst_at(FX.dust[FX.dust_i++ % 4], (t3_vec3){ at.x, 1.2f, at.z }); break;
    case EV_CRATE: splinter(s, e->x, e->y); break;
    case EV_PICK: burst_at(FX.sparkle[FX.sparkle_i++ % 4], at); break;
    case EV_BUBBLE: burst_at(FX.pop[FX.pop_i++ % 2], (t3_vec3){ at.x, 0.55f, at.z }); break;
    case EV_KO:
      burst_at(FX.stars[FX.stars_i++ % 2], (t3_vec3){ at.x, 0.8f, at.z });
      burst_at(FX.fur[FX.fur_i++ % 2], (t3_vec3){ at.x, 0.5f, at.z });
      break;
    case EV_TELE: burst_at(FX.glint[FX.glint_i++ % 2], (t3_vec3){ at.x, 0.1f, at.z }); break;
    case EV_SD_WALL:
      burst_at(FX.dust[FX.dust_i++ % 4], (t3_vec3){ at.x, 0.1f, at.z });
      FX.shake = CW_MIN(FX.shake + 3, 16);
      break;
    default: break;
    }
  }
}

void fx_tick(const sim *s) {
  if (!FX.ready) return;
  FX.ticks++;
  if (FX.shake > 0) FX.shake--;
  /* the fire cross: only the burning tiles are drawn, in grid order (the
   * flames blend in the order they were before) */
  int t = FX.ticks, nfire = 0;
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      int f = s->fire[y][x];
      if (f <= 0) continue;
      int age = 36 - f, sh = s->fire_shape[y][x];
      float grow = cw_clampf(age / 4.0f, 0, 1), fade = f < 14 ? f / 14.0f : 1;
      float k = grow * fade * (0.92f + 0.08f * cw_sin(t * 0.9f + x * 1.7f + y * 2.3f));
      float lx = 0.85f, lz = 0.85f, ly = 1.25f;
      if (sh & 1) lx = lz = 1.15f, ly = 1.6f;
      else {
        if (sh & 2) lx = 1.2f, lz = 0.72f;
        if (sh & 4) lz = 1.2f, lx = sh & 2 ? 1.2f : 0.72f;
        if (sh & 16) { lx *= 0.85f; lz *= 0.85f; }
      }
      t3_mat4 m;
      float wob = cw_sin(t * 0.7f + x * 3.1f + y) * 0.06f;
      t3_mat4_compose(&m, (t3_vec3){ tx_w(x), 0.45f + wob, tz_w(y) }, (t3_quat){ 0, 0, 0, 1 },
                      (t3_vec3){ lx * k, ly * k, lz * k });
      t3_instanced_mesh_set_matrix_at(FX.flame, nfire, &m);
      float kc = k * (age < 8 ? 0.8f : 0.55f);
      t3_mat4_compose(&m, (t3_vec3){ tx_w(x), 0.42f, tz_w(y) }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ lx * kc, ly * kc * 0.9f, lz * kc });
      t3_instanced_mesh_set_matrix_at(FX.core, nfire++, &m);
    }
  FX.flame->count = FX.core->count = nfire;
  if (nfire) {
    FX.flame->instance_matrix->version++;
    FX.core->instance_matrix->version++;
  }
  /* planks: only the live ones are drawn; a plank instance takes its shard's colour when it changes shard */
  const float dt = 1.0f / 60.0f;
  int nplank = 0;
  bool recolour = false;
  for (int i = 0; i < SHARDS; i++) {
    shard *sh = &FX.s[i];
    if (sh->life <= 0) continue;
    sh->life--;
    sh->v.y -= 14 * dt;
    sh->p.x += sh->v.x * dt;
    sh->p.y += sh->v.y * dt;
    sh->p.z += sh->v.z * dt;
    float half = sh->size.y * 0.5f;
    if (sh->p.y < half) {
      sh->p.y = half;
      sh->v.y = -sh->v.y * 0.3f;
      sh->v.x *= 0.6f, sh->v.z *= 0.6f;
      sh->spin.x *= 0.4f, sh->spin.y *= 0.5f, sh->spin.z *= 0.4f;
      sh->rot.x *= 0.5f, sh->rot.z *= 0.5f;
    }
    sh->rot.x += sh->spin.x * dt;
    sh->rot.y += sh->spin.y * dt;
    sh->rot.z += sh->spin.z * dt;
    if (sh->life == 0) continue; /* gone this tick */
    float shrink = sh->life < 30 ? sh->life / 30.0f : 1;
    t3_mat4 mx;
    t3_quat q = t3_quat_from_euler((t3_euler){ sh->rot.x, sh->rot.y, sh->rot.z, T3_XYZ });
    t3_mat4_compose(&mx, sh->p, q, (t3_vec3){ sh->size.x * shrink, sh->size.y * shrink, sh->size.z * shrink });
    t3_instanced_mesh_set_matrix_at(FX.planks, nplank, &mx);
    if (FX.plank_of[nplank] != i + 1 || sh->new_col) {
      t3_instanced_mesh_set_color_at(FX.planks, nplank, sh->col);
      FX.plank_of[nplank] = (int16_t)(i + 1);
      sh->new_col = false;
      recolour = true;
    }
    nplank++;
  }
  FX.planks->count = nplank;
  if (nplank) FX.planks->instance_matrix->version++;
  if (recolour) FX.planks->instance_color->version++;
  for (int i = 0; i < 3; i++) { /* flashes: a hot white-orange pop that reddens and dies */
    if (!cw_node_valid(FX.flash[i])) continue;
    int a = FX.flash_age[i];
    float e = 0;
    if (a > 0) {
      e = a < 3 ? 14.0f * a / 3 : 14.0f * cw_exp(-(a - 3) * 0.12f);
      FX.flash_age[i] = a < 40 ? a + 1 : 0;
    }
    cw_node_set(FX.flash[i], "energy", cw_val_float(e));
    float k = cw_clampf(a / 20.0f, 0, 1);
    cw_node_set(FX.flash[i], "color", cw_val_color(1, 0.75f - 0.3f * k, 0.45f - 0.35f * k, 1));
  }
  for (int i = 0; i < SCORCH; i++)
    if (FX.scorch_a[i] > 0) {
      FX.scorch_a[i] = FX.scorch_a[i] > 0.002f ? FX.scorch_a[i] - 0.0012f : 0;
      cw_node_set(FX.scorch[i], "color", cw_val_color(0.2f, 0.16f, 0.13f, FX.scorch_a[i]));
    }
}

