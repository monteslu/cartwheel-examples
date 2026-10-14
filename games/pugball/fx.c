/* pugball effects. All of them read the simulation's state and events;
 * nothing here feeds the rules. GPU particles (GPUParticles3D) and ribbons
 * (Trail3D) simulate on the tree's fixed step from hashed seeds, so every
 * host draws the same effects.
 *
 *   boost     each pug's jet: an exhaust plume from the nozzles in the
 *             pug's chosen trail (fire, bubbles, rainbow, stardust, hearts)
 *             and a ribbon behind it at supersonic
 *   turf      clods and grass kicked up by powerslides, jumps and landings
 *   the cat   fur tufts and stars on every touch (a burst of both on a
 *             whack), a dust ring where it bounces hard
 *   goals     the cat goes up in a puff of fur: a fireball, a shockwave
 *             ring, confetti in the scorers' colour from both posts,
 *             fireworks over the stands, a flash, a shake
 *   demos     a cartoon explosion of smoke, sparks and stars
 *   pads      a sparkle as the boost is taken */
#include <stdio.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "world.h"

#define FIREWORKS 6

static struct {
  bool ready;
  int theme;
  cw_node root;
  cw_node exhaust[MAXP], jet[MAXP], puff[MAXP], ribbon[MAXP];
  int trail_kind[MAXP];
  cw_node turf[4], tufts[3], stars[3], ring[2], sparkle[4], smoke[2], boom[2], dsparks[2];
  int turf_i, tufts_i, stars_i, ring_i, sparkle_i, smoke_i, boom_i;
  cw_node goal_fire, goal_ring, goal_fur, confetti[4], fw[FIREWORKS];
  int fw_t[FIREWORKS];
  cw_node flash[3];
  int flash_age[3];
  float flash_e[3];
  int next_flash;
  float shake;
  int ticks;
} FX;

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
      cw_warn("pugball: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  return n;
}
static void set_str(cw_node n, const char *prop, const char *s) {
  cw_value v;
  if (cw_value_parse(s, CW_T_STRING, &v)) cw_node_set(n, prop, v);
}
static void burst_at(cw_node n, t3_vec3 at) {
  cw_node3d_set_position(n, at);
  cw_particles_restart(n);
}

/* ── recipes ────────────────────────────────────────────────────────── */

static const char *const EXHAUST[] = { "amount", "90", "lifetime", "0.45", "lifetime_random", "0.3", "emission_shape", "SPHERE",
  "emission_radius", "0.08", "direction", "vec3(0, 0, -1)", "spread", "12", "speed_min", "3", "speed_max", "6", "gravity", "vec3(0, 1.5, 0)",
  "damping", "3", "size", "0.28", "size_random", "0.4", "size_curve", "\"0 0.7, 0.2 1, 1 0.1\"", "energy", "1.4", "blend", "ALPHA",
  "turbulence", "1.2", "turbulence_scale", "0.6", NULL };
/* the jet's hot core: additive and bright, so it reads over sunlit grass */
static const char *const JET[] = { "amount", "70", "lifetime", "0.22", "lifetime_random", "0.3", "emission_shape", "SPHERE",
  "emission_radius", "0.05", "direction", "vec3(0, 0, -1)", "spread", "6", "speed_min", "7", "speed_max", "10", "damping", "2",
  "size", "0.17", "size_random", "0.3", "size_curve", "\"0 1, 1 0.2\"", "energy", "5", "blend", "ADD", NULL };
/* and the smoke it leaves: dark-edged puffs that grow and hang, a contrast
 * against bright turf whatever the trail's colour */
static const char *const PUFF[] = { "amount", "60", "lifetime", "1.1", "lifetime_random", "0.4", "emission_shape", "SPHERE",
  "emission_radius", "0.1", "direction", "vec3(0, 0, -1)", "spread", "20", "speed_min", "1.5", "speed_max", "3", "gravity", "vec3(0, 0.8, 0)",
  "damping", "2.5", "size", "0.75", "size_random", "0.5", "size_curve", "\"0 0.4, 0.3 1, 1 1.8\"",
  "color_ramp", "\"0 0.06 0.05 0.05 0, 0.1 0.07 0.06 0.06 0.8, 0.6 0.16 0.15 0.15 0.45, 1 0.3 0.29 0.28 0\"", "blend", "ALPHA",
  "soft_distance", "0.3", "turbulence", "1.5", "turbulence_scale", "0.5", NULL };
static const char *const TRAIL_RAMP[TRAIL_KINDS] = {
  "\"0 1 0.75 0.2 1, 0.2 1 0.3 0.02 0.9, 0.6 0.6 0.05 0.01 0.45, 1 0.15 0.02 0.01 0\"",
  "\"0 0.8 1 1 0.9, 0.5 0.6 0.9 1 0.7, 1 0.9 0.8 1 0\"",
  "\"0 1 0.2 0.2 1, 0.2 1 0.7 0.1 1, 0.4 0.9 1 0.2 1, 0.6 0.2 1 0.4 1, 0.8 0.2 0.6 1 1, 1 0.6 0.3 1 0\"",
  "\"0 1 1 0.8 1, 0.5 1 0.85 0.3 1, 1 1 0.7 0.2 0\"",
  "\"0 1 0.6 0.8 1, 0.5 1 0.35 0.6 1, 1 1 0.5 0.7 0\"" };
/* the supersonic ribbon: saturated at the head, no white in it (white
 * washes out over sunlit turf), fading through deep colour */
static const char *const RIBBON_RAMP[TRAIL_KINDS] = {
  "\"0 1 0.62 0.12 1, 0.12 1 0.32 0.02 0.95, 0.4 0.4 0.06 0.02 0.75, 1 0.1 0.09 0.09 0\"",
  "\"0 0.75 0.95 1 0.8, 0.5 0.5 0.8 1 0.5, 1 0.85 0.95 1 0\"",
  "\"0 1 0.15 0.15 1, 0.2 1 0.65 0.05 1, 0.4 1 0.95 0.1 1, 0.6 0.15 0.9 0.3 0.9, 0.8 0.15 0.45 1 0.7, 1 0.55 0.2 0.9 0\"",
  "\"0 1 1 0.85 1, 0.3 1 0.8 0.25 0.9, 0.7 0.3 0.22 0.08 0.5, 1 0.1 0.08 0.06 0\"",
  "\"0 1 0.55 0.75 1, 0.4 1 0.25 0.55 0.9, 1 0.35 0.08 0.2 0\"" };
static const char *const TURF[] = { "amount", "40", "lifetime", "0.9", "explosiveness", "0.9", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.3", "direction", "vec3(0, 1, 0)", "spread", "60", "speed_min", "1.5", "speed_max", "4", "gravity", "vec3(0, -9, 0)",
  "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.06", "size_random", "0.5", "spin", "8", "blend", "OPAQUE",
  "color_ramp", "\"0 0.25 0.5 0.15 1, 1 0.3 0.45 0.15 1\"", "bounce", "0.3", "collision_radius", "0.03", NULL };
static const char *const TUFTS[] = { "amount", "40", "lifetime", "1.6", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE_SURFACE", "emission_radius", "1.2", "spread", "180", "speed_min", "1", "speed_max", "4", "gravity", "vec3(0, -1.2, 0)",
  "damping", "2.2", "draw_mode", "MESH", "mesh_primitive", "SPHERE", "size", "0.09", "size_random", "0.6", "spin", "4", "turbulence", "2",
  "turbulence_scale", "0.5", "blend", "OPAQUE", "color_ramp", "\"0 1 0.62 0.3 1, 0.5 1 0.85 0.65 1, 1 1 0.95 0.85 1\"", NULL };
static const char *const STARS[] = { "amount", "18", "lifetime", "0.8", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.6", "spread", "180", "speed_min", "3", "speed_max", "6", "damping", "3", "size", "0.22", "size_random", "0.4",
  "size_curve", "\"0 1, 1 0\"", "spin", "6", "color_ramp", "\"0 1 1 0.7 1, 0.6 1 0.85 0.3 1, 1 1 0.7 0.2 0\"", "energy", "5", "blend", "ADD",
  NULL };
static const char *const RING[] = { "amount", "60", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true", "emission_shape", "RING",
  "emission_radius", "0.6", "direction", "vec3(0, 0.15, 0)", "spread", "90", "speed_min", "5", "speed_max", "7", "damping", "4",
  "gravity", "vec3(0, 0.5, 0)", "size", "0.7", "size_random", "0.3", "size_curve", "\"0 0.4, 0.3 1, 1 1.6\"",
  "color_ramp", "\"0 0.8 0.75 0.65 0, 0.1 0.85 0.8 0.7 0.6, 1 0.9 0.85 0.8 0\"", "blend", "ALPHA", "soft_distance", "0.4", NULL };
static const char *const SPARKLE[] = { "amount", "40", "lifetime", "0.7", "explosiveness", "1", "one_shot", "true", "emission_shape", "RING",
  "emission_radius", "0.8", "direction", "vec3(0, 1, 0)", "spread", "15", "speed_min", "2", "speed_max", "4", "gravity", "vec3(0, -2, 0)",
  "size", "0.1", "size_curve", "\"0 1, 1 0\"", "color_ramp", "\"0 1 0.95 0.6 1, 1 1 0.6 0.1 0\"", "energy", "6", "blend", "ADD", NULL };
static const char *const SMOKE[] = { "amount", "50", "lifetime", "2", "lifetime_random", "0.3", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.5", "spread", "180", "speed_min", "0.5", "speed_max", "2", "gravity", "vec3(0, 1.2, 0)",
  "damping", "1.5", "size", "1.2", "size_random", "0.5", "size_curve", "\"0 0.3, 0.3 1, 1 1.7\"",
  "color_ramp", "\"0 0.2 0.18 0.17 0, 0.15 0.25 0.22 0.2 0.6, 1 0.5 0.48 0.46 0\"", "blend", "ALPHA", "soft_distance", "0.5",
  "turbulence", "1", "turbulence_scale", "0.35", NULL };
static const char *const BOOM[] = { "amount", "120", "lifetime", "0.6", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.4", "spread", "180", "speed_min", "1.5", "speed_max", "6", "gravity", "vec3(0, 2, 0)", "damping", "4",
  "size", "0.9", "size_random", "0.5", "size_curve", "\"0 0.5, 0.2 1.3, 1 0.2\"",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.2 1 0.6 0.15 1, 0.6 0.9 0.25 0.04 0.8, 1 0.4 0.05 0.02 0\"", "energy", "3", "blend", "ADD",
  "turbulence", "1.5", "turbulence_scale", "0.5", NULL };
static const char *const DSPARKS[] = { "amount", "100", "lifetime", "1", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.2", "spread", "180", "speed_min", "5", "speed_max", "12", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "damping", "0.5", "draw_mode", "STRETCHED", "stretch", "0.04", "size", "0.06",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.3 1 0.6 0.15 1, 1 0.7 0.12 0.04 0\"", "energy", "6", "bounce", "0.4", "collision_radius", "0.03",
  "collide_depth", "true", NULL };
static const char *const GOAL_FIRE[] = { "amount", "260", "lifetime", "0.9", "explosiveness", "1", "one_shot", "true", "emission_shape",
  "SPHERE", "emission_radius", "1", "spread", "180", "speed_min", "3", "speed_max", "11", "gravity", "vec3(0, 3, 0)", "damping", "3.5",
  "size", "1.6", "size_random", "0.5", "size_curve", "\"0 0.5, 0.15 1.3, 1 0.3\"",
  "color_ramp", "\"0 1 1 0.85 1, 0.15 1 0.7 0.25 1, 0.5 1 0.35 0.08 0.85, 1 0.4 0.08 0.03 0\"", "energy", "4", "blend", "ADD",
  "turbulence", "2", "turbulence_scale", "0.4", NULL };
static const char *const GOAL_RING[] = { "amount", "140", "lifetime", "0.9", "explosiveness", "1", "one_shot", "true", "emission_shape",
  "RING", "emission_radius", "1.2", "direction", "vec3(0, 0.05, 0)", "spread", "90", "speed_min", "14", "speed_max", "17", "damping", "3",
  "size", "0.5", "size_curve", "\"0 1, 1 0.2\"", "color_ramp", "\"0 1 1 1 1, 0.4 1 0.9 0.6 0.8, 1 1 0.6 0.3 0\"", "energy", "4",
  "blend", "ADD", NULL };
static const char *const GOAL_FUR[] = { "amount", "220", "lifetime", "3", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "1.3", "spread", "180", "speed_min", "3", "speed_max", "10", "gravity", "vec3(0, -1, 0)",
  "damping", "2", "draw_mode", "MESH", "mesh_primitive", "SPHERE", "size", "0.12", "size_random", "0.6", "spin", "3", "turbulence", "2.5",
  "turbulence_scale", "0.4", "blend", "OPAQUE", "color_ramp", "\"0 1 0.6 0.25 1, 0.5 1 0.82 0.6 1, 1 1 0.95 0.85 1\"", NULL };
static const char *const CONFETTI[] = { "amount", "260", "lifetime", "4.5", "lifetime_random", "0.4", "explosiveness", "0.85",
  "one_shot", "true", "emission_shape", "SPHERE", "emission_radius", "0.4", "direction", "vec3(0, 1, 0)", "spread", "35", "speed_min", "9",
  "speed_max", "16", "gravity", "vec3(0, -3.5, 0)", "damping", "1.6", "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.12",
  "size_random", "0.3", "spin", "8", "turbulence", "2", "turbulence_scale", "0.4", "blend", "OPAQUE", NULL };
static const char *const FIREWORK[] = { "amount", "150", "lifetime", "1.6", "lifetime_random", "0.3", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "spread", "180", "speed_min", "8", "speed_max", "11", "gravity", "vec3(0, -4, 0)",
  "damping", "1.8", "size", "0.25", "size_curve", "\"0 1, 1 0\"", "energy", "8", "blend", "ADD", "trail_length", "8", "trail_width", "0.12",
  NULL };

void fx_reset(void) { memset(&FX, 0, sizeof FX); }

void fx_init(cw_node scene, int theme) {
  fx_reset();
  FX.theme = theme;
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(scene, FX.root);
  for (int i = 0; i < MAXP; i++) {
    FX.exhaust[i] = pnode("Exhaust", EXHAUST, 100 + i);
    FX.jet[i] = pnode("Jet", JET, 120 + i);
    FX.puff[i] = pnode("Puff", PUFF, 140 + i);
    FX.ribbon[i] = cw_node_new("Trail3D", "Ribbon");
    cw_node_add_child(FX.root, FX.ribbon[i]);
    cw_node_set(FX.ribbon[i], "length", cw_val_int(30));
    cw_node_set(FX.ribbon[i], "width", cw_val_float(0.2f));
    cw_node_set(FX.ribbon[i], "energy", cw_val_float(2.5f));
    cw_node_set(FX.ribbon[i], "blend", cw_val_enum("ADD"));
    cw_node_set(FX.ribbon[i], "emitting", cw_val_bool(false));
    FX.trail_kind[i] = -1;
  }
  for (int i = 0; i < 4; i++) FX.turf[i] = pnode("Turf", TURF, 200 + i);
  for (int i = 0; i < 3; i++) FX.tufts[i] = pnode("Tufts", TUFTS, 300 + i);
  for (int i = 0; i < 3; i++) FX.stars[i] = pnode("Stars", STARS, 320 + i);
  for (int i = 0; i < 2; i++) FX.ring[i] = pnode("Ring", RING, 340 + i);
  for (int i = 0; i < 4; i++) FX.sparkle[i] = pnode("Sparkle", SPARKLE, 360 + i);
  for (int i = 0; i < 2; i++) FX.smoke[i] = pnode("Smoke", SMOKE, 380 + i);
  for (int i = 0; i < 2; i++) FX.boom[i] = pnode("Boom", BOOM, 390 + i);
  for (int i = 0; i < 2; i++) FX.dsparks[i] = pnode("Sparks", DSPARKS, 395 + i);
  FX.goal_fire = pnode("GoalFire", GOAL_FIRE, 400);
  FX.goal_ring = pnode("GoalRing", GOAL_RING, 401);
  FX.goal_fur = pnode("GoalFur", GOAL_FUR, 402);
  for (int i = 0; i < 4; i++) FX.confetti[i] = pnode("Confetti", CONFETTI, 410 + i);
  for (int i = 0; i < FIREWORKS; i++) FX.fw[i] = pnode("Firework", FIREWORK, 420 + i);
  for (int i = 0; i < 3; i++) {
    char name[16];
    snprintf(name, sizeof name, "Flash%d", i);
    FX.flash[i] = cw_node_get(scene, name);
  }
  FX.ready = true;
}

float fx_shake(void) { return FX.shake; }

static void flash(t3_vec3 at, float energy) {
  int i = FX.next_flash++ % 3;
  if (!cw_node_valid(FX.flash[i])) return;
  cw_node3d_set_position(FX.flash[i], at);
  FX.flash_age[i] = 1;
  FX.flash_e[i] = energy;
}

void fx_confetti(t3_vec3 at, int team) {
  if (!FX.ready) return;
  char ramp[96];
  float r = TEAM_RGB[team][0], g = TEAM_RGB[team][1], b = TEAM_RGB[team][2];
  for (int i = 0; i < 4; i++) {
    /* the team's colour, white and gold, a little of each per cannon */
    float k = i == 3 ? 1 : 0;
    snprintf(ramp, sizeof ramp, "\"0 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 1\"", i == 2 ? 1 : r, i == 2 ? 0.85f : g, i == 2 ? 0.25f : b,
             k ? 1 : r, k ? 1 : g, k ? 1 : b);
    set_str(FX.confetti[i], "color_ramp", ramp);
    float side = i & 1 ? 1.0f : -1.0f;
    burst_at(FX.confetti[i], (t3_vec3){ at.x + side * (GW + 0.2f), 0.5f + (i / 2) * GH, at.z });
  }
}

static void goal(const sim *s, const struct sim_event *e) {
  float side = s->ball.z > 0 ? 1.0f : -1.0f;
  t3_vec3 c = s->ball;
  burst_at(FX.goal_fire, c);
  burst_at(FX.goal_ring, (t3_vec3){ c.x, 0.3f, c.z });
  burst_at(FX.goal_fur, c);
  fx_confetti((t3_vec3){ 0, 0, side * AL }, e->a);
  char ramp[96];
  for (int i = 0; i < FIREWORKS; i++) {
    FX.fw_t[i] = 20 + i * 14;
    const float *tc = i % 3 == 2 ? (const float[3]){ 1, 0.85f, 0.3f } : TEAM_RGB[e->a];
    snprintf(ramp, sizeof ramp, "\"0 1 1 1 1, 0.2 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 0\"", tc[0], tc[1], tc[2], tc[0], tc[1], tc[2]);
    set_str(FX.fw[i], "color_ramp", ramp);
  }
  flash((t3_vec3){ c.x, c.y + 1, c.z }, 60);
  flash((t3_vec3){ 0, 4, side * (AL - 6) }, 30);
  FX.shake = 1;
}

void fx_events(const sim *s) {
  if (!FX.ready) return;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    switch (e->kind) {
    case EV_HIT:
      if (e->k > 0.08f) burst_at(FX.tufts[FX.tufts_i++ % 3], e->at);
      if (e->k > 0.4f) {
        burst_at(FX.stars[FX.stars_i++ % 3], e->at);
        flash(e->at, 10 * e->k);
        FX.shake = fmaxf(FX.shake, 0.25f * e->k);
      }
      break;
    case EV_BOUNCE:
      if (e->k > 0.35f && e->at.y < 1) burst_at(FX.ring[FX.ring_i++ % 2], (t3_vec3){ e->at.x, 0.2f, e->at.z });
      break;
    case EV_JUMP:
    case EV_LAND:
      if (e->at.y < 1.2f && (e->kind == EV_JUMP || e->k > 0.3f)) burst_at(FX.turf[FX.turf_i++ % 4], (t3_vec3){ e->at.x, 0.1f, e->at.z });
      break;
    case EV_PAD: burst_at(FX.sparkle[FX.sparkle_i++ % 4], (t3_vec3){ e->at.x, 0.2f, e->at.z }); break;
    case EV_DEMO:
      burst_at(FX.boom[FX.boom_i % 2], e->at);
      burst_at(FX.dsparks[FX.boom_i % 2], e->at);
      FX.boom_i++;
      burst_at(FX.smoke[FX.smoke_i++ % 2], e->at);
      burst_at(FX.stars[FX.stars_i++ % 3], e->at);
      flash(e->at, 25);
      FX.shake = fmaxf(FX.shake, 0.6f);
      break;
    case EV_GOAL: goal(s, e); break;
    default: break;
    }
  }
}

void fx_set_trail(int i, int kind) {
  if (!FX.ready || FX.trail_kind[i] == kind) return;
  FX.trail_kind[i] = kind;
  set_str(FX.exhaust[i], "color_ramp", TRAIL_RAMP[kind]);
  set_str(FX.ribbon[i], "color_ramp", RIBBON_RAMP[kind]);
  set_str(FX.jet[i], "color_ramp", TRAIL_RAMP[kind]);
  if (kind == TRAIL_BUBBLES) {
    cw_node_set(FX.exhaust[i], "draw_mode", cw_val_enum("MESH"));
    cw_node_set(FX.exhaust[i], "mesh_primitive", cw_val_enum("SPHERE"));
    cw_node_set(FX.exhaust[i], "size", cw_val_float(0.12f));
    cw_node_set(FX.exhaust[i], "blend", cw_val_enum("ALPHA"));
  }
}

void fx_tick(const sim *s) {
  if (!FX.ready) return;
  FX.ticks++;
  FX.shake *= 0.93f;
  for (int i = 0; i < MAXP; i++) {
    const pug *u = &s->p[i];
    bool on = u->on && !u->dead && u->boosting;
    if (u->on) {
      t3_vec3 f = pug_fwd(u), up = pug_up(u);
      t3_vec3 at = v3add(u->p, v3add(v3mul(f, -0.44f), v3mul(up, 0.3f))); /* the nozzles */
      cw_node3d_set_position(FX.exhaust[i], at);
      /* the plume blows out behind the pug: point the emitter's -z backwards */
      float yaw = cw_atan2(f.x, f.z), pitch = -cw_asin(cw_clampf(f.y, -1, 1));
      cw_node3d_set_rotation(FX.exhaust[i], (t3_vec3){ pitch, yaw, 0 });
      cw_node3d_set_position(FX.jet[i], at);
      cw_node3d_set_rotation(FX.jet[i], (t3_vec3){ pitch, yaw, 0 });
      cw_node3d_set_position(FX.puff[i], at);
      cw_node3d_set_rotation(FX.puff[i], (t3_vec3){ pitch, yaw, 0 });
      cw_node3d_set_position(FX.ribbon[i], at);
    }
    cw_node_set(FX.exhaust[i], "emitting", cw_val_bool(on));
    cw_node_set(FX.jet[i], "emitting", cw_val_bool(on));
    cw_node_set(FX.puff[i], "emitting", cw_val_bool(on && FX.trail_kind[i] != TRAIL_BUBBLES));
    cw_node_set(FX.ribbon[i], "emitting", cw_val_bool(on && u->supersonic));
  }
  for (int i = 0; i < FIREWORKS; i++)
    if (FX.fw_t[i] > 0 && --FX.fw_t[i] == 0) {
      uint32_t h = cw_particle_hash((uint32_t)(FX.ticks * 31 + i));
      float x = ((float)(h & 255) / 255.0f - 0.5f) * 2 * (AW + 10);
      float z = ((float)((h >> 8) & 255) / 255.0f - 0.5f) * 2 * (AL + 6);
      float side = (h >> 16) & 1 ? 1.0f : -1.0f;
      if ((h >> 17) & 1) x = side * (AW + 14);
      else z = side * (AL + 18);
      burst_at(FX.fw[i], (t3_vec3){ x, 26 + (float)((h >> 20) & 15), z });
    }
  for (int i = 0; i < 3; i++) {
    if (!cw_node_valid(FX.flash[i])) continue;
    int a = FX.flash_age[i];
    float e = 0;
    if (a > 0) {
      e = a < 3 ? FX.flash_e[i] * a / 3 : FX.flash_e[i] * cw_exp(-(a - 3) * 0.1f);
      FX.flash_age[i] = a < 50 ? a + 1 : 0;
    }
    cw_node_set(FX.flash[i], "energy", cw_val_float(e));
  }
}
