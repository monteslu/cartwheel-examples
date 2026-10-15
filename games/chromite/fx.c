/* chromite effects. They read the simulation's events; nothing here feeds
 * the rules. GPU particles (GPUParticles3D) simulate on the tree's fixed
 * step from hashed seeds, so every host draws the same effects.
 *
 *   kills      a fireball, sparks and chrome debris, sized to the craft; a
 *              shock ring off the big ones; a flash that lights the
 *              chrome round it
 *   hits       sparks in the colour of the round that hit
 *   the hero   blue shield sparks or orange hull sparks when hit; a long
 *              chain of blasts when the ship goes
 *   bosses     smoke as they weaken, a spiral of blasts when they fall
 *   pickups    a burst ring in the power-up's colour */
#include <stdio.h>
#include <string.h>

#include "cartwheel/particles.h"
#include "game.h"

#define FLASHES 4
#define QUEUE 160

enum { BOOM_S, BOOM_M, BOOM_L, SPARK, DEBRIS, SMOKE, RING_FX, SHIELD, HULL, PICK, FLASH, MUZZLE, KINDS_FX };
static const int POOL_FX[KINDS_FX] = { 8, 6, 4, 10, 6, 8, 4, 4, 4, 3, 8, 6 };

typedef struct queued { int delay, kind; float x, y, size; } queued;

static struct {
  bool ready;
  cw_node root;
  cw_node n[KINDS_FX][10];
  int next[KINDS_FX];
  t3_light *flash[FLASHES];
  float flash_e[FLASHES], flash_k[FLASHES];
  int next_flash;
  queued q[QUEUE];
  int qn;
  cw_node exhaust[2];
  t3_mesh *rings[4];
  t3_material *ring_mat[4];
  float ring_age[4], ring_size[4];
  int next_ring;
} FX;

int fx_lights_live;

static cw_node pnode(const char *name, const char *const *kv, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(FX.root, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) {
      cw_node_set(n, kv[i], cw_val_enum(kv[i + 1]));
      continue;
    }
    if (!p || !cw_value_parse(kv[i + 1], p->type, &v)) {
      cw_warn("chromite: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  return n;
}

/* ── recipes ────────────────────────────────────────────────────────── */

/* fireballs: flipbook billboards (fx/fireball.png, 8 x 8 frames of a
 * fireball cooling) thrown out and slowed, a few big ones in the middle */
static const char *const BOOM_SMALL[] = { "amount", "14", "lifetime", "0.7", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.35", "spread", "180", "speed_min", "0.8", "speed_max", "3.2", "gravity", "vec3(0, 0.6, 0)",
  "damping", "3.5", "size", "1.5", "size_random", "0.45", "size_curve", "\"0 0.55, 0.3 1, 1 1.25\"", "spin", "1.2",
  "texture", "\"fx/fireball.png\"", "hframes", "8", "vframes", "8", "energy", "2.4", "blend", "ADD", NULL };
static const char *const BOOM_MED[] = { "amount", "26", "lifetime", "1.0", "lifetime_random", "0.35", "explosiveness", "0.92", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.6", "spread", "180", "speed_min", "1", "speed_max", "4.5", "gravity", "vec3(0, 0.9, 0)",
  "damping", "3", "size", "2.4", "size_random", "0.5", "size_curve", "\"0 0.5, 0.25 1, 1 1.35\"", "spin", "1",
  "texture", "\"fx/fireball.png\"", "hframes", "8", "vframes", "8", "energy", "2.6", "blend", "ADD", NULL };
static const char *const BOOM_BIG[] = { "amount", "60", "lifetime", "1.5", "lifetime_random", "0.4", "explosiveness", "0.85", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "1.3", "spread", "180", "speed_min", "1.5", "speed_max", "7", "gravity", "vec3(0, 1.2, 0)",
  "damping", "2.6", "size", "4", "size_random", "0.5", "size_curve", "\"0 0.45, 0.25 1, 1 1.45\"", "spin", "0.8",
  "texture", "\"fx/fireball.png\"", "hframes", "8", "vframes", "8", "energy", "2.8", "blend", "ADD", NULL };
/* sparks: hot streaks with short trails */
static const char *const SPARKS[] = { "amount", "36", "lifetime", "0.5", "lifetime_random", "0.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.15", "spread", "180", "speed_min", "5", "speed_max", "16", "gravity", "vec3(0, -9, 0)",
  "damping", "1.6", "draw_mode", "STRETCHED", "stretch", "0.035", "size", "0.07", "texture", "\"fx/spark.png\"",
  "color_ramp", "\"0 1 0.95 0.85 1, 0.3 1 0.7 0.3 1, 1 0.9 0.25 0.05 0\"", "energy", "6", "blend", "ADD", NULL };
/* chrome debris: tumbling shards that fall away toward the deck */
static const char *const DEBRIS_R[] = { "amount", "22", "lifetime", "1.8", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.4", "spread", "180", "speed_min", "3", "speed_max", "10", "gravity", "vec3(0, -16, 0)",
  "damping", "0.5", "draw_mode", "MESH", "mesh_primitive", "TETRA", "size", "0.16", "size_random", "0.7", "spin", "16",
  "color_ramp", "\"0 0.8 0.8 0.85 1, 1 0.35 0.33 0.33 1\"", "blend", "OPAQUE", "trail_length", "6", "trail_width", "0.05", NULL };
/* smoke: a flipbook puff (fx/smoke.png) that rolls up and drifts with the deck */
static const char *const SMOKE_R[] = { "amount", "16", "lifetime", "2.6", "lifetime_random", "0.4", "explosiveness", "0.8", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.7", "spread", "180", "speed_min", "0.3", "speed_max", "1.4", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, 1.0, 1.39)", "damping", "1.1", "size", "3", "size_random", "0.5", "size_curve", "\"0 0.5, 0.3 1, 1 1.8\"", "spin", "0.4",
  "texture", "\"fx/smoke.png\"", "hframes", "4", "vframes", "4",
  "color_ramp", "\"0 0.09 0.085 0.08 0, 0.1 0.08 0.075 0.07 0.75, 1 0.12 0.12 0.12 0\"", "blend", "ALPHA", "turbulence", "0.7", "turbulence_scale", "0.5", NULL };
static const char *const SHIELD_R[] = { "amount", "40", "lifetime", "0.45", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE_SURFACE", "emission_radius", "1.3", "spread", "180", "speed_min", "1", "speed_max", "5", "gravity", "vec3(0, 0, 0)",
  "damping", "3", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.06", "texture", "\"fx/spark.png\"",
  "color_ramp", "\"0 0.7 0.9 1 1, 0.5 0.3 0.5 1 1, 1 0.1 0.2 1 0\"", "energy", "7", "blend", "ADD", NULL };
static const char *const HULL_R[] = { "amount", "44", "lifetime", "0.6", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "spread", "180", "speed_min", "3", "speed_max", "10", "gravity", "vec3(0, -9, 0)",
  "damping", "1.8", "draw_mode", "STRETCHED", "stretch", "0.035", "size", "0.06", "texture", "\"fx/spark.png\"",
  "color_ramp", "\"0 1 0.8 0.4 1, 0.4 1 0.35 0.05 1, 1 0.5 0.05 0.02 0\"", "energy", "6", "blend", "ADD", NULL };
static const char *const PICK_R[] = { "amount", "60", "lifetime", "0.6", "lifetime_random", "0.2", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "RING", "emission_radius", "0.6", "emission_inner_radius", "0.5", "emission_normal_speed", "5", "spread", "10",
  "speed_min", "0.2", "speed_max", "0.6", "gravity", "vec3(0, 0, 0)", "damping", "2.5", "size", "0.35", "size_curve", "\"0 1, 1 0\"",
  "energy", "6", "blend", "ADD", NULL };
/* the flash: one star flare that blooms for a few frames */
static const char *const FLASH_R[] = { "amount", "1", "lifetime", "0.22", "explosiveness", "1", "one_shot", "true", "emission_shape", "POINT",
  "spread", "0", "speed_min", "0", "speed_max", "0", "gravity", "vec3(0, 0, 0)", "size", "7", "size_curve", "\"0 0.6, 0.15 1, 1 0.2\"",
  "texture", "\"fx/flare.png\"", "color_ramp", "\"0 1 0.9 0.75 1, 1 1 0.5 0.2 0\"", "energy", "5", "blend", "ADD", NULL };

/* a muzzle flash: a small blue-white flare for a frame or two */
static const char *const MUZZLE_R[] = { "amount", "1", "lifetime", "0.06", "explosiveness", "1", "one_shot", "true", "emission_shape", "POINT",
  "spread", "0", "speed_min", "0", "speed_max", "0", "gravity", "vec3(0, 0, 0)", "size", "1.1", "size_curve", "\"0 1, 1 0.4\"",
  "texture", "\"fx/flare.png\"", "color_ramp", "\"0 0.6 0.8 1 1, 1 0.3 0.5 1 0\"", "energy", "4", "blend", "ADD", NULL };
/* the hero's exhaust: hot gas off each nozzle, blue-white to orange, trailing behind */
static const char *const EXHAUST_R[] = { "amount", "70", "lifetime", "0.32", "lifetime_random", "0.3", "emission_shape", "SPHERE",
  "emission_radius", "0.04", "direction", "vec3(0, 0, 1)", "spread", "7", "speed_min", "7", "speed_max", "10", "gravity", "vec3(0, 0, 0)",
  "damping", "4", "size", "0.42", "size_random", "0.3", "size_curve", "\"0 0.6, 0.3 1, 1 0.2\"", "texture", "\"fx/smoke.png\"", "hframes", "4", "vframes", "4",
  "color_ramp", "\"0 0.5 0.75 1 1, 0.35 1 0.55 0.2 0.8, 1 0.4 0.1 0.05 0\"", "energy", "3", "blend", "ADD", "local_coords", "false", NULL };

static const char *const *const RECIPE[KINDS_FX] = { BOOM_SMALL, BOOM_MED, BOOM_BIG, SPARKS, DEBRIS_R, SMOKE_R, NULL, SHIELD_R, HULL_R, PICK_R, FLASH_R, MUZZLE_R };

void fx_init(cw_node parent) {
  fx_free();
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(parent, FX.root);
  int seed = 101;
  for (int k = 0; k < KINDS_FX; k++) {
    if (!RECIPE[k]) continue;
    for (int i = 0; i < POOL_FX[k]; i++) {
      char nm[32];
      snprintf(nm, sizeof nm, "fx%d_%d", k, i);
      FX.n[k][i] = pnode(nm, RECIPE[k], seed++);
    }
  }
  for (int i = 0; i < 2; i++) {
    char nm[24];
    snprintf(nm, sizeof nm, "exhaust%d", i);
    FX.exhaust[i] = pnode(nm, EXHAUST_R, 900 + i);
  }
  /* flash lights: always present (a fixed light count keeps one program
   * per material), dark until a blast lights them */
  t3_object *grp = t3_group_new();
  for (int i = 0; i < FLASHES; i++) {
    FX.flash[i] = t3_point_light_new(0xffaa55, 0, 16, 2);
    t3_object_add(grp, FX.flash[i]);
    t3_release(FX.flash[i]);
  }
  /* shock rings: flat additive discs that widen and fade */
  for (int i = 0; i < 4; i++) {
    t3_geometry *g = t3_ring_geometry_new(0.82f, 1, 64, 1);
    t3_geometry_rotate_x(g, -1.5707963f);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->color = (t3_color){ 3, 2.2f, 1.4f };
    m->blending = T3_ADDITIVE_BLENDING;
    m->transparent = true;
    m->depth_write = false;
    m->side = T3_DOUBLE_SIDE;
    FX.ring_mat[i] = m;
    FX.rings[i] = t3_mesh_new(g, m);
    t3_release(g);
    T3_OBJ(FX.rings[i])->visible = false;
    t3_object_add(grp, FX.rings[i]);
    t3_release(FX.rings[i]);
  }
  cw_node holder = cw_node_new("Node3D", "FXLights");
  cw_node_add_child(FX.root, holder);
  cw_node3d_set_object(holder, grp);
  t3_release(grp);
  FX.ready = true;
}

void fx_free(void) {
  if (cw_node_valid(FX.root)) cw_node_free(FX.root);
  for (int i = 0; i < 4; i++)
    if (FX.ring_mat[i]) t3_release(FX.ring_mat[i]);
  memset(&FX, 0, sizeof FX);
}

static void burst(int kind, float x, float y, float lift) {
  if (!FX.ready || !RECIPE[kind]) return;
  cw_node n = FX.n[kind][FX.next[kind]];
  FX.next[kind] = (FX.next[kind] + 1) % POOL_FX[kind];
  t3_vec3 p = to_world(x, y);
  p.y += lift;
  cw_node3d_set_position(n, p);
  cw_particles_restart(n);
}

static void flash(float x, float y, float energy, float r, float g, float b) {
  int i = FX.next_flash;
  FX.next_flash = (i + 1) % FLASHES;
  t3_vec3 p = to_world(x, y);
  t3_object_set_position(FX.flash[i], p.x, p.y + 1.5f, p.z);
  FX.flash[i]->color = (t3_color){ r, g, b };
  FX.flash_e[i] = energy;
  FX.flash_k[i] = 0.86f;
}

static void ring(float x, float y, float size) {
  int i = FX.next_ring;
  FX.next_ring = (i + 1) % 4;
  t3_vec3 p = to_world(x, y);
  t3_object_set_position(FX.rings[i], p.x, p.y + 0.2f, p.z);
  FX.ring_age[i] = 0;
  FX.ring_size[i] = size;
  T3_OBJ(FX.rings[i])->visible = true;
}

static void queue(int delay, int kind, float x, float y, float size) {
  if (FX.qn < QUEUE) FX.q[FX.qn++] = (queued){ delay, kind, x, y, size };
}

static void blast(int size, float x, float y) {
  burst(FLASH, x, y, 0.8f);
  if (size == 0) {
    burst(BOOM_S, x, y, 0);
    burst(SPARK, x, y, 0);
    flash(x, y, 60, 1, 0.6f, 0.3f);
  } else if (size == 1) {
    burst(BOOM_M, x, y, 0);
    burst(SPARK, x, y, 0);
    burst(DEBRIS, x, y, 0);
    burst(SMOKE, x, y, 0.3f);
    flash(x, y, 140, 1, 0.55f, 0.25f);
  } else {
    burst(BOOM_L, x, y, 0);
    burst(DEBRIS, x, y, 0);
    burst(SPARK, x, y, 0);
    burst(SMOKE, x, y, 0.5f);
    ring(x, y, size == 2 ? 6 : 10);
    flash(x, y, 320, 1, 0.6f, 0.3f);
  }
}

/* the deterministic jitter of a queued chain (render-only) */
static float jit(int i, int k) {
  uint32_t h = (uint32_t)i * 2654435761u ^ (uint32_t)k * 40503u;
  h ^= h >> 15;
  return ((h & 1023) / 1023.0f) * 2 - 1;
}

void fx_events(const sim *S) {
  for (int i = 0; i < S->ev_n; i++) {
    const sim_event *e = &S->ev[i];
    switch (e->type) {
    case EV_KILL:
      if (e->a == EK_GNAT || e->a == EK_OMNI) blast(0, e->x, e->y);
      else if (e->a == EK_STRAIGHT) blast(1, e->x, e->y);
      else if (e->a < EK_BOSS0) {
        blast(2, e->x, e->y);
        for (int k = 0; k < 4; k++) queue(6 + k * 7, 1, e->x + jit(i, k) * 1.2f, e->y + jit(i, k + 9) * 1.2f, 0);
      }
      break;
    case EV_BOSS_DIE:
      for (int k = 0; k < 24; k++) {
        float a = k * 0.9f, r = 0.3f + k * 0.12f;
        queue(k * 5, k % 4 == 3 ? 2 : 1, e->x + cosf(a) * r, e->y + sinf(a) * r * 0.6f, 0);
      }
      queue(125, 3, e->x, e->y, 0);
      break;
    case EV_HIT:
      if (S->tick % 2 == 0 || e->a == GUN_PURPLE) burst(SPARK, e->x, e->y, 0);
      break;
    case EV_HERO_HIT:
      burst(S->h.shields > 0 ? SHIELD : HULL, S->h.x, S->h.y, 0);
      break;
    case EV_HERO_DIE:
      blast(2, e->x, e->y);
      for (int k = 0; k < 40; k++) queue(2 + k * 2, k % 6 == 5 ? 2 : k % 2, e->x + jit(i, k) * 2.5f, e->y + jit(i, k + 50) * 2.5f, 0);
      break;
    case EV_PICKUP: burst(PICK, e->x, e->y, 0.3f); break;
    case EV_SHOT:
      if (e->a == GUN_DEFAULT) burst(MUZZLE, S->h.x + ((S->tick / 3) % 2 ? 0.39f : -0.39f), S->h.y + 1.05f, 0.1f);
      else burst(MUZZLE, S->h.x, S->h.y + 1.3f, 0.1f);
      break;
    case EV_SMOKE: burst(SMOKE, e->x, e->y, 0.6f); break;
    case EV_LOSE: flash(e->x, -8, 200, 1, 0.1f, 0.1f); break;
    default: break;
    }
  }
  /* the queue runs on ticks too */
  int w = 0;
  for (int i = 0; i < FX.qn; i++) {
    queued *q = &FX.q[i];
    if (--q->delay <= 0) blast(q->kind, q->x, q->y);
    else FX.q[w++] = *q;
  }
  FX.qn = w;
}

/* the live flashes, for the reflection probe */
int fx_flashes(t3_vec3 *pos, t3_color *col, float *energy, int max) {
  int n = 0;
  if (!FX.ready) return 0;
  for (int i = 0; i < FLASHES && n < max; i++) {
    if (FX.flash_e[i] <= 0) continue;
    pos[n] = T3_OBJ(FX.flash[i])->position;
    col[n] = FX.flash[i]->color;
    energy[n] = FX.flash_e[i];
    n++;
  }
  return n;
}

/* the exhaust follows the hero's nozzles and stops while the ship is away */
void fx_hero(const sim *S) {
  if (!FX.ready) return;
  bool on = sim_hero_visible(S);
  for (int i = 0; i < 2; i++) {
    t3_vec3 p = to_world(S->h.x + (i ? 0.21f : -0.21f), S->h.y - 1.25f);
    p.y -= 0.05f;
    cw_node3d_set_position(FX.exhaust[i], p);
    cw_node_set(FX.exhaust[i], "emitting", cw_val_bool(on));
  }
}

void fx_update(float dt) {
  (void)dt;
  fx_lights_live = 0;
  for (int i = 0; i < FLASHES; i++) {
    FX.flash[i]->intensity = FX.flash_e[i];
    FX.flash_e[i] *= FX.flash_k[i];
    if (FX.flash_e[i] < 0.5f) FX.flash_e[i] = 0;
    else fx_lights_live++;
  }
  for (int i = 0; i < 4; i++) {
    if (!T3_OBJ(FX.rings[i])->visible) continue;
    FX.ring_age[i] += 1.0f / 60;
    float t = FX.ring_age[i] / 0.5f;
    if (t >= 1) {
      T3_OBJ(FX.rings[i])->visible = false;
      continue;
    }
    float r = FX.ring_size[i] * (0.2f + 0.8f * (1 - (1 - t) * (1 - t)));
    t3_object_set_scale(FX.rings[i], r, 1, r);
    FX.ring_mat[i]->opacity = (1 - t) * (1 - t);
  }
}
