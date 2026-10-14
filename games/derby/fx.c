/* derby effects. All of them read the simulation's state and events;
 * nothing here feeds the rules. GPU particles (GPUParticles3D) simulate on
 * the tree's fixed step from hashed seeds, so every host draws the same
 * effects.
 *
 *   dust      each car's tyres throw dirt when they skid, spin up or
 *             barrel along; a big cloud off a hard hit
 *   engines   smoke from under the bonnet as a car takes damage, thin
 *             grey to thick black; fire and black smoke on a wreck, then
 *             a smoulder
 *   crashes   sparks off the contact, chips of paint and glass, a flash,
 *             a shake that grows with the hit
 *   scrapes   a spray of sparks along a grind
 *   wrecks    a burst of flame, a scorch on the dirt under it */
#include <stdio.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "world.h"

#define SCORCHES 12

static struct {
  bool ready;
  int theme;
  cw_node root;
  cw_node dust[MAXC], smoke[MAXC], fire[MAXC], mud[MAXC];
  cw_node sparks[4], chips[3], glass[2], cloud[3], grind[3], boom[2];
  int sparks_i, chips_i, glass_i, cloud_i, grind_i, boom_i;
  cw_node scorch[SCORCHES];
  int scorch_i;
  cw_node flash[3];
  int flash_age[3];
  float flash_e[3];
  int next_flash;
  float shake;
  int ticks;
  int smoke_level[MAXC]; /* 0 none, 1 thin, 2 thick, 3 fire, 4 smoulder */
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
      cw_warn("derby: bad particle setting %s = %s", kv[i], kv[i + 1]);
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

static const char *const DUST[] = { "amount", "70", "lifetime", "1.6", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(0.9, 0.1, 0.3)", "direction", "vec3(0, 0.4, -1)", "spread", "35", "speed_min", "0.6", "speed_max", "2.4",
  "gravity", "vec3(0, 0.25, 0)", "damping", "1.4", "size", "0.7", "size_random", "0.5", "size_curve", "\"0 0.35, 0.3 1, 1 1.7\"",
  "color_ramp", "\"0 0.42 0.33 0.24 0, 0.1 0.45 0.36 0.27 0.26, 1 0.5 0.42 0.33 0\"", "blend", "ALPHA", "soft_distance", "0.6",
  "turbulence", "0.8", "turbulence_scale", "0.4", "local_coords", "false", NULL };
static const char *const SMOKE[] = { "amount", "90", "lifetime", "2.6", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(0.5, 0.1, 0.4)", "direction", "vec3(0, 1, 0)", "spread", "20", "speed_min", "0.8", "speed_max", "1.8",
  "gravity", "vec3(0.3, 0.6, 0)", "damping", "0.8", "size", "0.7", "size_random", "0.4", "size_curve", "\"0 0.3, 0.3 1, 1 2.4\"",
  "color_ramp", "\"0 0.5 0.5 0.5 0, 0.12 0.45 0.45 0.45 0.5, 1 0.6 0.6 0.6 0\"", "blend", "ALPHA", "soft_distance", "0.5",
  "turbulence", "1.1", "turbulence_scale", "0.35", "local_coords", "false", NULL };
static const char *const FIRE[] = { "amount", "220", "lifetime", "0.9", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(0.7, 0.25, 1.1)", "direction", "vec3(0, 1, 0)", "spread", "18", "speed_min", "1.8", "speed_max", "4",
  "gravity", "vec3(0, 2.5, 0)", "damping", "1", "size", "0.95", "size_random", "0.5", "size_curve", "\"0 0.6, 0.25 1.2, 1 0.2\"",
  "color_ramp", "\"0 1 0.85 0.45 1, 0.25 1 0.5 0.1 1, 0.7 0.7 0.18 0.03 0.7, 1 0.2 0.05 0.02 0\"", "energy", "3.5", "blend", "ADD",
  "turbulence", "1.6", "turbulence_scale", "0.45", "local_coords", "false", NULL };
static const char *const MUD[] = { "amount", "40", "lifetime", "1.1", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(0.9, 0.05, 0.15)", "direction", "vec3(0, 0.7, -1)", "spread", "30", "speed_min", "2", "speed_max", "5",
  "gravity", "vec3(0, -9.8, 0)", "damping", "0.3", "draw_mode", "MESH", "mesh_primitive", "TETRA", "size", "0.06", "size_random", "0.7",
  "spin", "10", "blend", "OPAQUE", "color_ramp", "\"0 0.22 0.16 0.11 1, 1 0.18 0.13 0.09 1\"", "bounce", "0.15", "collision_radius", "0.03",
  "collide_depth", "true", "local_coords", "false", NULL };
static const char *const SPARKS[] = { "amount", "45", "lifetime", "0.55", "lifetime_random", "0.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.15", "spread", "75", "speed_min", "3", "speed_max", "8", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -12, 0)", "damping", "1", "draw_mode", "STRETCHED", "stretch", "0.025", "size", "0.025",
  "color_ramp", "\"0 1 0.95 0.75 1, 0.3 1 0.65 0.2 1, 1 0.8 0.15 0.04 0\"", "energy", "7", "blend", "ADD", "bounce", "0.35",
  "collision_radius", "0.03", "collide_depth", "true", NULL };
static const char *const GRIND[] = { "amount", "60", "lifetime", "0.5", "lifetime_random", "0.4", "explosiveness", "0.6", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.1", "spread", "50", "speed_min", "3", "speed_max", "8", "direction", "vec3(0, 0.6, 1)",
  "gravity", "vec3(0, -12, 0)", "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.04",
  "color_ramp", "\"0 1 0.9 0.6 1, 1 0.9 0.3 0.05 0\"", "energy", "6", "blend", "ADD", NULL };
static const char *const CHIPS[] = { "amount", "36", "lifetime", "1.6", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.3", "spread", "180", "speed_min", "2", "speed_max", "6", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -9.8, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.07", "size_random", "0.6", "spin", "9",
  "blend", "OPAQUE", "bounce", "0.3", "collision_radius", "0.03", "collide_depth", "true", NULL };
static const char *const GLASS[] = { "amount", "70", "lifetime", "1.4", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.4", "spread", "180", "speed_min", "2", "speed_max", "5", "gravity", "vec3(0, -9.8, 0)",
  "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.04", "size_random", "0.5", "spin", "12", "energy", "1.5",
  "color_ramp", "\"0 0.75 0.85 0.9 1, 1 0.6 0.7 0.75 1\"", "blend", "OPAQUE", "bounce", "0.4", "collision_radius", "0.02",
  "collide_depth", "true", NULL };
static const char *const CLOUD[] = { "amount", "60", "lifetime", "2.2", "lifetime_random", "0.4", "explosiveness", "0.95", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.8", "spread", "180", "speed_min", "1", "speed_max", "3.5", "direction", "vec3(0, 0.4, 0)",
  "gravity", "vec3(0, 0.3, 0)", "damping", "1.8", "size", "1.6", "size_random", "0.5", "size_curve", "\"0 0.4, 0.3 1, 1 1.8\"",
  "color_ramp", "\"0 0.45 0.36 0.27 0, 0.08 0.48 0.39 0.3 0.55, 1 0.52 0.45 0.36 0\"", "blend", "ALPHA", "soft_distance", "0.8",
  "turbulence", "0.9", "turbulence_scale", "0.35", NULL };
static const char *const BOOM[] = { "amount", "160", "lifetime", "0.9", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.8", "spread", "180", "speed_min", "1.5", "speed_max", "6.5", "gravity", "vec3(0, 3, 0)", "damping", "3.2",
  "size", "1.5", "size_random", "0.5", "size_curve", "\"0 0.5, 0.2 1.4, 1 0.3\"",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.2 1 0.6 0.15 1, 0.55 0.6 0.18 0.04 0.8, 1 0.12 0.08 0.06 0\"", "energy", "4", "blend", "ADD",
  "turbulence", "1.5", "turbulence_scale", "0.5", NULL };

void fx_reset(void) { memset(&FX, 0, sizeof FX); }

void fx_init(cw_node scene, int theme) {
  fx_reset();
  FX.theme = theme;
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(scene, FX.root);
  for (int i = 0; i < MAXC; i++) {
    FX.dust[i] = pnode("Dust", DUST, 100 + i);
    FX.smoke[i] = pnode("Smoke", SMOKE, 120 + i);
    FX.fire[i] = pnode("Fire", FIRE, 140 + i);
    FX.mud[i] = pnode("Mud", MUD, 160 + i);
  }
  for (int i = 0; i < 4; i++) FX.sparks[i] = pnode("Sparks", SPARKS, 200 + i);
  for (int i = 0; i < 3; i++) FX.chips[i] = pnode("Chips", CHIPS, 210 + i);
  for (int i = 0; i < 2; i++) FX.glass[i] = pnode("Glass", GLASS, 220 + i);
  for (int i = 0; i < 3; i++) FX.cloud[i] = pnode("Cloud", CLOUD, 230 + i);
  for (int i = 0; i < 3; i++) FX.grind[i] = pnode("Grind", GRIND, 240 + i);
  for (int i = 0; i < 2; i++) FX.boom[i] = pnode("Boom", BOOM, 250 + i);
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

static void scorch(t3_vec3 at, float rot) {
  int i = FX.scorch_i++ % SCORCHES;
  if (!cw_node_valid(FX.scorch[i])) {
    FX.scorch[i] = cw_node_new("Decal3D", "Scorch");
    cw_node_add_child(FX.root, FX.scorch[i]);
    cw_node_set(FX.scorch[i], "blend", cw_val_enum("MULTIPLY"));
    cw_node_set(FX.scorch[i], "color", cw_val_color(0.1f, 0.08f, 0.07f, 0.9f));
    cw_node_set(FX.scorch[i], "size", cw_val_vec3(5.5f, 5.5f, 2.4f));
    cw_node_set(FX.scorch[i], "normal_fade", cw_val_float(0.3f));
  }
  cw_node3d_set_position(FX.scorch[i], (t3_vec3){ at.x, 0.5f, at.z });
  cw_node3d_set_rotation(FX.scorch[i], (t3_vec3){ -1.5707963f, 0, rot });
}

static void paint_chips(cw_node n, int colour) {
  char ramp[96];
  const float *c = CAR_RGB[colour % CAR_COLOURS];
  snprintf(ramp, sizeof ramp, "\"0 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 1\"", c[0], c[1], c[2], c[0] * 0.6f, c[1] * 0.6f, c[2] * 0.6f);
  set_str(n, "color_ramp", ramp);
}

static int colour_of[MAXC];
void fx_set_colours(const int colours[MAXC]);
void fx_set_colours(const int colours[MAXC]) { memcpy(colour_of, colours, sizeof colour_of); }

void fx_events(const sim *s) {
  if (!FX.ready) return;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    switch (e->kind) {
    case EV_CRASH: {
      float k = e->k;
      if (k > 1.5f) burst_at(FX.sparks[FX.sparks_i++ % 4], e->at);
      if (k > 3) {
        cw_node ch = FX.chips[FX.chips_i++ % 3];
        paint_chips(ch, colour_of[k > 6 ? e->b : e->a]);
        burst_at(ch, e->at);
        burst_at(FX.cloud[FX.cloud_i++ % 3], (t3_vec3){ e->at.x, 0.4f, e->at.z });
        flash((t3_vec3){ e->at.x, 1.2f, e->at.z }, fminf(40, k * 3));
      }
      if (k > 7) burst_at(FX.glass[FX.glass_i++ % 2], (t3_vec3){ e->at.x, 1.1f, e->at.z });
      FX.shake = fmaxf(FX.shake, fminf(1, k * 0.06f));
      break;
    }
    case EV_WALL:
      if (e->k > 2) {
        burst_at(FX.sparks[FX.sparks_i++ % 4], e->at);
        burst_at(FX.cloud[FX.cloud_i++ % 3], (t3_vec3){ e->at.x, 0.4f, e->at.z });
      }
      if (e->k > 5) {
        cw_node ch = FX.chips[FX.chips_i++ % 3];
        paint_chips(ch, colour_of[e->a]);
        burst_at(ch, e->at);
      }
      FX.shake = fmaxf(FX.shake, fminf(0.7f, e->k * 0.04f));
      break;
    case EV_LAND: /* a body slammed onto the dirt: a roll, a landing on its roof */
      burst_at(FX.cloud[FX.cloud_i++ % 3], (t3_vec3){ e->at.x, 0.3f, e->at.z });
      if (e->k > 5) burst_at(FX.glass[FX.glass_i++ % 2], e->at);
      FX.shake = fmaxf(FX.shake, fminf(0.6f, e->k * 0.04f));
      break;
    case EV_SCRAPE:
      if ((FX.ticks & 3) == 0 && e->k > 3) burst_at(FX.grind[FX.grind_i++ % 3], e->at);
      break;
    case EV_WRECK: {
      burst_at(FX.boom[FX.boom_i++ % 2], (t3_vec3){ e->at.x, 1, e->at.z });
      burst_at(FX.glass[FX.glass_i++ % 2], (t3_vec3){ e->at.x, 1.2f, e->at.z });
      burst_at(FX.cloud[FX.cloud_i++ % 3], (t3_vec3){ e->at.x, 0.5f, e->at.z });
      flash((t3_vec3){ e->at.x, 1.5f, e->at.z }, 60);
      scorch(e->at, s->c[e->a].h);
      FX.shake = fmaxf(FX.shake, 0.8f);
      break;
    }
    default: break;
    }
  }
}

static const char *const SMOKE_RAMP[3] = {
  "\"0 0.45 0.45 0.46 0, 0.12 0.38 0.38 0.39 0.38, 1 0.5 0.5 0.5 0\"",          /* steam and burning oil, grey */
  "\"0 0.09 0.085 0.08 0, 0.08 0.07 0.065 0.06 0.72, 1 0.22 0.21 0.2 0\"",      /* thick, oily black */
  "\"0 0.05 0.045 0.04 0, 0.08 0.04 0.038 0.035 0.85, 1 0.2 0.19 0.18 0\"" };   /* a fire's */

void fx_tick(const sim *s) {
  if (!FX.ready) return;
  FX.ticks++;
  FX.shake *= 0.9f;
  for (int i = 0; i < MAXC; i++) {
    const car *c = &s->c[i];
    if (!c->on) continue;
    const car_spec *k = &CAR_SPEC[c->kind];
    float fx, fz;
    cw_sincos(c->h, &fx, &fz);
    float speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
    /* dust off the rear wheels, pointing back from the car */
    t3_vec3 rear = { c->x + fx * k->ar, c->y + 0.25f, c->z + fz * k->ar };
    cw_node3d_set_position(FX.dust[i], rear);
    cw_node3d_set_rotation(FX.dust[i], (t3_vec3){ 0, c->h, 0 });
    bool dusty = !c->wrecked && (c->slip_r > 1.5f || c->slip_f > 2.5f || speed > 9 || (c->rpm > 0.6f && speed < 4 && s->phase == PH_GO));
    bool down = c->veh.contacts > 0; /* nothing kicks up dirt in the air or on its roof */
    cw_node_set(FX.dust[i], "emitting", cw_val_bool(dusty && down));
    /* clods flung off the rear tyres when they spin or slide hard */
    cw_node3d_set_position(FX.mud[i], (t3_vec3){ rear.x, rear.y - 0.05f, rear.z });
    cw_node3d_set_rotation(FX.mud[i], (t3_vec3){ 0, c->h, 0 });
    cw_node_set(FX.mud[i], "emitting", cw_val_bool(down && !c->wrecked && (c->slip_r > 3.5f || (c->rpm > 0.7f && speed < 5 && s->phase == PH_GO))));
    /* the bonnet: smoke as the engine fails, fire on a wreck, then a smoulder (from
     * under the car when it lies on its roof) */
    float hy = c->y + (c->up_y > 0 ? 0.95f * c->up_y : 0.3f);
    t3_vec3 hood = { c->x + fx * k->hl * 0.55f, hy, c->z + fz * k->hl * 0.55f };
    if (c->wrecked) hood = (t3_vec3){ c->x + fx * k->hl * 0.3f, hy - 0.05f, c->z + fz * k->hl * 0.3f };
    cw_node3d_set_position(FX.smoke[i], hood);
    cw_node3d_set_position(FX.fire[i], hood);
    int lvl = c->wrecked ? (c->wreck_ticks < 600 ? 3 : 4) : c->hp < 30 ? 2 : c->hp < 65 ? 1 : 0;
    if (c->respawn > 0 && c->respawn < 30) lvl = 0;
    if (lvl != FX.smoke_level[i]) {
      FX.smoke_level[i] = lvl;
      if (lvl >= 1) set_str(FX.smoke[i], "color_ramp", SMOKE_RAMP[lvl == 1 ? 0 : lvl == 2 ? 1 : 2]);
      cw_node_set(FX.smoke[i], "amount", cw_val_int(lvl == 1 ? 30 : lvl == 2 ? 80 : lvl == 3 ? 120 : 40));
      cw_node_set(FX.smoke[i], "emitting", cw_val_bool(lvl >= 1));
      cw_node_set(FX.fire[i], "emitting", cw_val_bool(lvl == 3));
    }
  }
  for (int i = 0; i < 3; i++) {
    if (!cw_node_valid(FX.flash[i])) continue;
    int a = FX.flash_age[i];
    float e = 0;
    if (a > 0) {
      e = a < 3 ? FX.flash_e[i] * a / 3 : FX.flash_e[i] * cw_exp(-(a - 3) * 0.12f);
      FX.flash_age[i] = a < 45 ? a + 1 : 0;
    }
    cw_node_set(FX.flash[i], "energy", cw_val_float(e));
  }
}
