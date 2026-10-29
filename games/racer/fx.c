/* racer's effects. Everything here reads the simulation's state and events and never feeds back, so every host
 * draws the same race. GPU particles (GPUParticles3D) simulate on the fixed step from hashed seeds.
 *
 *   per kart   (children of the kart's node, so they ride with it as drawn)
 *     puff     dust on rough ground, rubber smoke in a drift, snow on ice, mist in water, oil smoke
 *     drift    sparks at the rear wheels, blue / orange / purple by the drift level
 *     flame    the exhaust while boosting (colour by what gave the boost)
 *     spray    water thrown up by the tyres in a ford (made the first time it is needed)
 *     stars    over a kart that is spun out, and a sparkle trail through a trick
 *   pooled     one-shot bursts, round robin: sparks, wall grinds, debris, dust clouds, fireballs, flares,
 *              splashes, sparkles, shockwave rings; a few lights for flashes
 *   skid marks a ring of screen-space decals along the road's own frame (so they follow banked roads and loops)
 *   hazards    smoke and flame trails and a light on each missile in flight
 *   detail     what a kart emits depends on its distance to the nearest camera, so four views and eight karts stay
 *              cheap: near all of it, middle puffs and flames, far nothing; an emitter idle for a while is hidden
 *   speed lines strength from speed, boost and drafting (no camera shake: the pad rumbles instead)
 *
 * Dev aid: RACER_FX=<name> forces that effect on kart 0 once a second (land, wall, bump, trick, boost0..boost4, box,
 * missile, homing, mine, oil, shock, shield, respawn, fall, water, level1..3). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "arena.h"
#include "fx_int.h"

/* ── recipes ────────────────────────────────────────────────────────── */

static const char *const R_PUFF[] = { "amount", "90", "lifetime", "1.4", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(0.7, 0.05, 0.15)", "direction", "vec3(0, 0.5, -1)", "spread", "35", "speed_min", "0.8", "speed_max", "3",
  "gravity", "vec3(0, 0.35, 0)", "damping", "1.2", "size", "0.7", "size_random", "0.5", "size_curve", "\"0 0.4, 0.25 1, 1 2.6\"",
  "color_ramp", "\"0 0.5 0.4 0.3 0, 0.1 0.5 0.4 0.3 0.4, 1 0.5 0.4 0.3 0\"", "blend", "ALPHA", "soft_distance", "0.5", "camera_fade", "2.5",
  "turbulence", "0.8", "turbulence_scale", "0.4", "local_coords", "false", NULL };
static const char *const R_DRIFT[] = { "amount", "40", "lifetime", "0.36", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(0.75, 0.03, 0.1)", "direction", "vec3(0, 0.6, -1)", "spread", "50", "speed_min", "5", "speed_max", "11",
  "gravity", "vec3(0, -15, 0)", "draw_mode", "STRETCHED", "stretch", "0.07", "size", "0.022", "size_random", "0.4",
  "color_ramp", "\"0 0.5 0.8 1 1, 1 0.1 0.3 1 0\"", "energy", "8", "blend", "ADD", "local_coords", "false", NULL };
static const char *const R_FLAME[] = { "amount", "60", "lifetime", "0.34", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(0.3, 0.06, 0.04)", "direction", "vec3(0, 0.05, -1)", "spread", "6", "speed_min", "5", "speed_max", "9",
  "gravity", "vec3(0, 0.5, 0)", "size", "0.42", "size_random", "0.3", "size_curve", "\"0 0.7, 0.3 1, 1 0.1\"",
  "color_ramp", "\"0 1 1 1 1, 0.15 0.55 0.8 1 1, 0.5 0.2 0.35 1 0.8, 1 0.05 0.1 0.6 0\"", "energy", "4", "blend", "ADD",
  "local_coords", "false", NULL };
static const char *const R_SPRAY[] = { "amount", "70", "lifetime", "0.7", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(0.9, 0.05, 0.2)", "direction", "vec3(0, 1, -0.3)", "spread", "50", "speed_min", "3", "speed_max", "9",
  "gravity", "vec3(0, -16, 0)", "draw_mode", "STRETCHED", "stretch", "0.03", "size", "0.04", "size_random", "0.5",
  "color_ramp", "\"0 0.9 0.97 1 0.9, 1 0.7 0.85 1 0\"", "energy", "1.4", "blend", "ALPHA", "local_coords", "false", NULL };
static const char *const R_STARS[] = { "amount", "16", "lifetime", "0.8", "lifetime_random", "0.4", "emission_shape", "SPHERE",
  "emission_radius", "0.5", "direction", "vec3(0, 1, 0)", "spread", "30", "speed_min", "0.4", "speed_max", "1.2",
  "gravity", "vec3(0, 0.2, 0)", "size", "0.17", "size_random", "0.4", "spin", "4", "size_curve", "\"0 0.3, 0.2 1, 1 0\"",
  "color_ramp", "\"0 1 0.95 0.5 1, 1 1 0.7 0.1 0\"", "energy", "4", "blend", "ADD", "local_coords", "false", NULL };
static const char *const R_SPARKS[] = { "amount", "45", "lifetime", "0.55", "lifetime_random", "0.5", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.15", "spread", "75", "speed_min", "3", "speed_max", "9", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -14, 0)", "damping", "0.6", "draw_mode", "STRETCHED", "stretch", "0.025", "size", "0.028",
  "color_ramp", "\"0 1 0.95 0.75 1, 0.3 1 0.65 0.2 1, 1 0.8 0.15 0.04 0\"", "energy", "7", "blend", "ADD", "bounce", "0.35",
  "collision_radius", "0.03", "collide_depth", "true", NULL };
static const char *const R_GRIND[] = { "amount", "50", "lifetime", "0.5", "lifetime_random", "0.4", "explosiveness", "0.5", "one_shot", "true",
  "emission_shape", "BOX", "emission_extents", "vec3(0.1, 0.1, 0.9)", "spread", "50", "speed_min", "3", "speed_max", "9", "direction", "vec3(0, 0.7, 0)",
  "gravity", "vec3(0, -14, 0)", "draw_mode", "STRETCHED", "stretch", "0.05", "size", "0.035",
  "color_ramp", "\"0 1 0.9 0.6 1, 1 0.9 0.3 0.05 0\"", "energy", "6", "blend", "ADD", NULL };
static const char *const R_CHIPS[] = { "amount", "24", "lifetime", "1.4", "lifetime_random", "0.4", "explosiveness", "1", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.25", "spread", "70", "speed_min", "2.5", "speed_max", "8", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -16, 0)", "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.08", "size_random", "0.6", "spin", "9",
  "blend", "OPAQUE", "bounce", "0.3", "collision_radius", "0.03", "collide_depth", "true", NULL };
static const char *const R_CLOUD[] = { "amount", "36", "lifetime", "1.6", "lifetime_random", "0.4", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.6", "spread", "100", "speed_min", "1", "speed_max", "3.6", "direction", "vec3(0, 0.5, 0)",
  "gravity", "vec3(0, 0.3, 0)", "damping", "1.8", "size", "1.2", "size_random", "0.5", "size_curve", "\"0 0.4, 0.3 1, 1 1.8\"",
  "color_ramp", "\"0 0.5 0.4 0.3 0, 0.08 0.5 0.4 0.3 0.5, 1 0.5 0.4 0.3 0\"", "blend", "ALPHA", "soft_distance", "0.8", "camera_fade", "3.5",
  "turbulence", "0.9", "turbulence_scale", "0.35", NULL };
static const char *const R_BOOM[] = { "amount", "80", "lifetime", "0.9", "explosiveness", "1", "one_shot", "true", "emission_shape", "SPHERE",
  "emission_radius", "0.6", "spread", "180", "speed_min", "1.5", "speed_max", "7", "gravity", "vec3(0, 3, 0)", "damping", "3.2",
  "size", "1.4", "size_random", "0.5", "size_curve", "\"0 0.5, 0.2 1.4, 1 0.3\"",
  "color_ramp", "\"0 1 0.62 0.25 1, 0.25 1 0.36 0.07 1, 0.6 0.45 0.1 0.03 0.8, 1 0.08 0.05 0.05 0\"", "energy", "0.9", "blend", "ADD", "camera_fade", "3.5",
  "turbulence", "1.5", "turbulence_scale", "0.5", NULL };
static const char *const R_FLARE[] = { "amount", "1", "lifetime", "0.3", "explosiveness", "1", "one_shot", "true", "emission_shape", "POINT",
  "size", "1.5", "size_curve", "\"0 0.3, 0.15 1, 1 0.2\"", "color_ramp", "\"0 1 0.9 0.6 1, 1 1 0.4 0.1 0\"", "energy", "0.9", "blend", "ADD", "camera_fade", "3.5", NULL };
static const char *const R_SPLASH[] = { "amount", "80", "lifetime", "0.9", "lifetime_random", "0.4", "explosiveness", "0.8", "one_shot", "true",
  "emission_shape", "BOX", "emission_extents", "vec3(0.8, 0.05, 0.6)", "direction", "vec3(0, 1, 0)", "spread", "60", "speed_min", "4", "speed_max", "11",
  "gravity", "vec3(0, -16, 0)", "draw_mode", "STRETCHED", "stretch", "0.03", "size", "0.045", "size_random", "0.5",
  "color_ramp", "\"0 0.9 0.97 1 0.9, 1 0.7 0.85 1 0\"", "energy", "1.4", "blend", "ALPHA", NULL };
static const char *const R_SPARKLE[] = { "amount", "60", "lifetime", "1", "lifetime_random", "0.4", "explosiveness", "0.9", "one_shot", "true",
  "emission_shape", "SPHERE", "emission_radius", "0.5", "spread", "180", "speed_min", "1", "speed_max", "4.5", "direction", "vec3(0, 1, 0)",
  "gravity", "vec3(0, -2.5, 0)", "damping", "0.8", "size", "0.3", "size_random", "0.5", "spin", "5", "size_curve", "\"0 0.2, 0.15 1, 1 0\"",
  "color_ramp", "\"0 1 1 1 1, 1 1 0.8 0.3 0\"", "energy", "5", "blend", "ADD", NULL };
static const char *const R_TRAIL_SMOKE[] = { "amount", "60", "lifetime", "1.4", "lifetime_random", "0.3", "emission_shape", "SPHERE",
  "emission_radius", "0.15", "direction", "vec3(0, 0.3, 0)", "spread", "30", "speed_min", "0.2", "speed_max", "0.9",
  "gravity", "vec3(0, 0.5, 0)", "damping", "0.8", "size", "0.5", "size_random", "0.4", "size_curve", "\"0 0.4, 0.3 1, 1 2\"",
  "color_ramp", "\"0 0.6 0.6 0.6 0, 0.08 0.55 0.55 0.55 0.6, 1 0.45 0.45 0.45 0\"", "blend", "ALPHA", "turbulence", "0.7",
  "turbulence_scale", "0.4", "local_coords", "false", "camera_fade", "2.5", NULL };
static const char *const R_TRAIL_FLAME[] = { "amount", "40", "lifetime", "0.3", "lifetime_random", "0.3", "emission_shape", "SPHERE",
  "emission_radius", "0.1", "direction", "vec3(0, 0, -1)", "spread", "12", "speed_min", "3", "speed_max", "7",
  "size", "0.4", "size_curve", "\"0 0.7, 0.3 1, 1 0.1\"",
  "color_ramp", "\"0 1 0.95 0.6 1, 0.3 1 0.5 0.1 1, 1 0.4 0.08 0.02 0\"", "energy", "4", "blend", "ADD", "local_coords", "false", NULL };

/* ── helpers ────────────────────────────────────────────────────────── */

cw_node fx_pnode(cw_node parent, const char *name, const char *const *kv, int seed) {
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
      cw_warn("racer fx: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "seed", cw_val_int(seed));
  return n;
}

void fx_set_str(cw_node n, const char *prop, const char *s) {
  cw_value v;
  if (cw_value_parse(s, CW_T_STRING, &v)) cw_node_set(n, prop, v);
}

static void set_ramp(cw_node n, float r0, float g0, float b0, float r1, float g1, float b1) {
  char s[160];
  snprintf(s, sizeof s, "\"0 %.2f %.2f %.2f 1, 0.35 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 0\"", r0, g0, b0, (r0 + r1) * 0.5f, (g0 + g1) * 0.5f,
           (b0 + b1) * 0.5f, r1, g1, b1);
  fx_set_str(n, "color_ramp", s);
}
/* a soft puff: fades in from nothing, holds, fades out */
static void set_puff_ramp(cw_node n, float r, float g, float b, float a) {
  char s[160];
  snprintf(s, sizeof s, "\"0 %.2f %.2f %.2f 0, 0.1 %.2f %.2f %.2f %.2f, 1 %.2f %.2f %.2f 0\"", r, g, b, r, g, b, a, r, g, b);
  fx_set_str(n, "color_ramp", s);
}

/* an emitter we switch on and off: hidden once it has been quiet long enough for its particles to be gone */
typedef struct emit {
  cw_node n;
  bool on, vis;
  int idle;
} emit;
static void emit_set(emit *e, bool on) {
  if (!cw_node_valid(e->n)) return;
  if (on) {
    e->idle = 0;
    if (!e->vis) cw_node_set(e->n, "visible", cw_val_bool(true)), e->vis = true;
    if (!e->on) cw_node_set(e->n, "emitting", cw_val_bool(true)), e->on = true;
  } else {
    if (e->on) cw_node_set(e->n, "emitting", cw_val_bool(false)), e->on = false;
    if (e->vis && ++e->idle > 200) cw_node_set(e->n, "visible", cw_val_bool(false)), e->vis = false;
  }
}
static emit emit_make(cw_node parent, const char *name, const char *const *kv, int seed) {
  emit e = { .n = fx_pnode(parent, name, kv, seed) };
  cw_node_set(e.n, "visible", cw_val_bool(false));
  return e;
}

/* ── state ──────────────────────────────────────────────────────────── */

#define NSPARK 6
#define NGRIND 4
#define NCHIPS 4
#define NCLOUD 5
#define NBOOM 3
#define NFLARE 4
#define NSPLASH 3
#define NSPARKLE 6
#define NRING 3
#define RING_LIFE 26
#define NLIGHT 5
#define NTRAIL 6
#define NSKID 192
#define SKID_LIFE 420
#define SKID_STEP 1.9f

enum { PM_NONE, PM_DUST, PM_SMOKE, PM_SNOW, PM_MIST, PM_OIL, PM_FALL };
enum { ST_NONE, ST_HIT, ST_SHOCK, ST_BOOM, ST_TRICK };

typedef struct kfx {
  cw_node root;
  emit puff, drift, flame, spray, stars;
  int puff_mode, drift_level, flame_src, stars_mode;
  int flame_size;      /* quantised */
  float sl;            /* speed lines, smoothed */
  int prev_dlvl;       /* drift level last tick (the release event arrives after it was reset) */
  int wall_cool;
  int rough_cool;
  int fall_ticks;
  t3_vec3 skid_last[2];
  bool skid_on[2];
  float dist2;         /* to the nearest camera */
} kfx;

typedef struct light {
  cw_node n;
  float e0, decay;
  int age;
} light;

static struct {
  bool ready, dev_ready;
  cw_node scene, root, cam0;
  const sim *s;
  float dust_rgb[3];
  int nviews;
  int ticks;
  kfx k[MAXK];
  cw_node sparks[NSPARK], grind[NGRIND], chips[NCHIPS], cloud[NCLOUD], boom[NBOOM], flare[NFLARE], splash[NSPLASH], sparkle[NSPARKLE];
  struct { cw_node d; int age; float maxr, r, g, b; } ring[NRING];
  int sparks_i, grind_i, chips_i, cloud_i, boom_i, flare_i, splash_i, sparkle_i, ring_i;
  light light[NLIGHT];
  int light_i;
  emit tsmoke[NTRAIL], tflame[NTRAIL];
  int trail_of[NTRAIL]; /* hazard slot, or -1 */
  int hz_trail[MAXHZ];
  light mlight[2];
  cw_node skid[NSKID];
  int skid_birth[NSKID];
  int skid_alpha[NSKID];
  int skid_i;
  cw_node cam[MAXV];
  t3_vec3 cam_pos[MAXV], cam_prev[MAXV], cam_vel[MAXV];
  int ncams;
  int dev_kind, dev_state;
  bool off;
} FX;

/* ── geometry helpers ───────────────────────────────────────────────── */

static t3_quat quat_basis(t3_vec3 x, t3_vec3 y, t3_vec3 z) {
  t3_mat4 m;
  t3_mat4_identity(&m);
  m.e[0] = x.x, m.e[1] = x.y, m.e[2] = x.z;
  m.e[4] = y.x, m.e[5] = y.y, m.e[6] = y.z;
  m.e[8] = z.x, m.e[9] = z.y, m.e[10] = z.z;
  return t3_quat_from_rotation_matrix(&m);
}
/* orient a node so its +y is `up` and its +z points as near `fwd` as it can */
static void orient(cw_node n, t3_vec3 up, t3_vec3 fwd) {
  t3_vec3 f = v3sub(fwd, v3mul(up, v3dot(fwd, up)));
  if (v3len(f) < 1e-3f) f = v3(0, 0, 1), f = v3sub(f, v3mul(up, v3dot(f, up)));
  if (v3len(f) < 1e-3f) f = v3(1, 0, 0), f = v3sub(f, v3mul(up, v3dot(f, up)));
  f = v3norm(f);
  cw_node3d_set_quaternion(n, quat_basis(v3cross(up, f), up, f));
}

/* the frame to put an effect at a kart in: the road's when it is on it, else the kart's own */
static void kart_frame(const sim *s, int i, t3_vec3 *up, t3_vec3 *fwd) {
  const kart *k = &s->k[i];
  *up = kart_up(k);
  *fwd = kart_fwd(k);
  if (k->loc.on_road) {
    tsample m = track_at_loc(s->track, &k->loc, 0);
    *up = m.u;
    *fwd = m.f;
  }
}

static void burst(cw_node n, t3_vec3 at, t3_vec3 up, t3_vec3 fwd) {
  cw_node3d_set_position(n, at);
  orient(n, up, fwd);
  cw_node3d_snapshot(n);
  cw_particles_restart(n);
}
#define NEXT(arr, idx) (FX.arr[FX.idx++ % (int)(sizeof FX.arr / sizeof FX.arr[0])])

static void light_flash(t3_vec3 at, float energy, float range, float decay, float r, float g, float b) {
  light *l = &FX.light[FX.light_i++ % NLIGHT];
  if (!cw_node_valid(l->n)) return;
  cw_node3d_set_position(l->n, at);
  cw_node_set(l->n, "color", cw_val_color(r, g, b, 1));
  cw_node_set(l->n, "range", cw_val_float(range));
  l->e0 = energy, l->decay = decay, l->age = 0;
}

static void spark_burst(t3_vec3 at, t3_vec3 up, t3_vec3 fwd, float r, float g, float b) {
  cw_node n = NEXT(sparks, sparks_i);
  char s[160];
  snprintf(s, sizeof s, "\"0 1 1 0.9 1, 0.25 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 0\"", r, g, b, r * 0.7f, g * 0.3f, b * 0.2f);
  fx_set_str(n, "color_ramp", s);
  burst(n, at, up, fwd);
}
static void sparkle_burst(t3_vec3 at, t3_vec3 up, float r, float g, float b) {
  cw_node n = NEXT(sparkle, sparkle_i);
  set_ramp(n, 1, 1, 1, r, g, b);
  char s[160];
  snprintf(s, sizeof s, "\"0 1 1 1 1, 0.3 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 0\"", r, g, b, r * 0.6f, g * 0.6f, b * 0.6f);
  fx_set_str(n, "color_ramp", s);
  burst(n, at, up, v3(0, 0, 1));
}
static void cloud_burst(t3_vec3 at, t3_vec3 up, float r, float g, float b, float a, float size) {
  cw_node n = NEXT(cloud, cloud_i);
  set_puff_ramp(n, r, g, b, a);
  cw_node_set(n, "size", cw_val_float(size));
  burst(n, at, up, v3(0, 0, 1));
}
static void dust_cloud(t3_vec3 at, t3_vec3 up, float a, float size) {
  cloud_burst(at, up, FX.dust_rgb[0], FX.dust_rgb[1], FX.dust_rgb[2], a, size);
}
static void chips_burst(t3_vec3 at, t3_vec3 up, float r, float g, float b) {
  cw_node n = NEXT(chips, chips_i);
  char s[120];
  snprintf(s, sizeof s, "\"0 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 1\"", r, g, b, r * 0.6f, g * 0.6f, b * 0.6f);
  fx_set_str(n, "color_ramp", s);
  burst(n, at, up, v3(0, 0, 1));
}
static float dist2_to_cams(t3_vec3 p);
static void flare_at(t3_vec3 at, float size, float r, float g, float b) {
  cw_node n = NEXT(flare, flare_i);
  char s[120];
  snprintf(s, sizeof s, "\"0 1 1 0.9 1, 0.3 %.2f %.2f %.2f 1, 1 %.2f %.2f %.2f 0\"", r, g, b, r * 0.5f, g * 0.3f, b * 0.2f);
  fx_set_str(n, "color_ramp", s);
  cw_node_set(n, "size", cw_val_float(size));
  burst(n, v3add(at, v3(0, 0.7f, 0)), v3(0, 1, 0), v3(0, 0, 1));
}
static void ring_at(t3_vec3 at, t3_vec3 up, t3_vec3 fwd, float r, float g, float b, float maxr) {
  int i = FX.ring_i++ % NRING;
  cw_node d = FX.ring[i].d;
  if (!cw_node_valid(d)) return;
  t3_vec3 dir = v3sub(fwd, v3mul(up, v3dot(fwd, up)));
  dir = v3len(dir) < 0.1f ? v3(1, 0, 0) : v3norm(dir);
  cw_node3d_set_position(d, v3add(at, v3mul(up, 0.1f)));
  cw_node3d_set_quaternion(d, quat_basis(v3cross(v3mul(dir, -1), up), v3mul(dir, -1), up));
  FX.ring[i].age = 1, FX.ring[i].maxr = maxr, FX.ring[i].r = r, FX.ring[i].g = g, FX.ring[i].b = b;
  cw_node_set(d, "visible", cw_val_bool(true));
}
/* rings: a soft disc of light on the road that swells and fades (shockwave, blast, shield pop) */
static void set_region(cw_node d, float x, float y, float w, float h) {
  cw_value r = { CW_T_VEC4 };
  r.v[0] = x, r.v[1] = y, r.v[2] = w, r.v[3] = h;
  cw_node_set(d, "region", r);
}

static void rings_update(void) {
  for (int i = 0; i < NRING; i++) {
    if (!FX.ring[i].age) continue;
    cw_node d = FX.ring[i].d;
    float t = (float)FX.ring[i].age / RING_LIFE;
    if (FX.ring[i].age++ >= RING_LIFE) {
      FX.ring[i].age = 0;
      cw_node_set(d, "visible", cw_val_bool(false));
      continue;
    }
    float e = 1 - (1 - t) * (1 - t); /* quick out, then slows */
    float rad = FX.ring[i].maxr * (0.15f + 0.85f * e);
    cw_node_set(d, "size", cw_val_vec3(rad * 2, rad * 2, 2.5f));
    float a = (1 - t) * (1 - t);
    cw_node_set(d, "color", cw_val_color(FX.ring[i].r, FX.ring[i].g, FX.ring[i].b, a));
  }
}


static const float DRIFT_RGB[4][3] = { { 1, 0.9, 0.6 }, { 0.3, 0.6, 1 }, { 1, 0.55, 0.1 }, { 0.8, 0.3, 1 } };

/* ── init ───────────────────────────────────────────────────────────── */

void fx_reset(void) {
  fx_amb_reset();
  memset(&FX, 0, sizeof FX);
}

static cw_node find_cam(cw_node n) {
  if (cw_node_is(n, "Camera3D")) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_cam(c);
    if (cw_node_valid(r)) return r;
  }
}

static cw_node light_node(const char *name) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(FX.root, l);
  cw_node_set(l, "color", cw_val_color(1, 0.6f, 0.25f, 1));
  cw_node_set(l, "energy", cw_val_float(0));
  cw_node_set(l, "range", cw_val_float(14));
  cw_node_set(l, "decay", cw_val_float(2));
  cw_node_set(l, "fog_energy", cw_val_float(0));
  cw_node_set(l, "visible", cw_val_bool(false));
  return l;
}

void fx_init(cw_node scene, const sim *s, const map_def *map) {
  /* the ambient emitters the map placed during scenery() stay: only fx_reset drops them */
  memset(&FX, 0, sizeof FX);
  FX.scene = scene;
  FX.s = s;
  FX.root = cw_node_new("Node3D", "FX");
  cw_node_add_child(scene, FX.root);
  const float *rd = map ? map->look.road : NULL;
  /* dust takes a little of the road's own colour so a salt flat kicks up white and a refinery grey */
  float base[3] = { 0.5f, 0.4f, 0.3f };
  for (int c = 0; c < 3; c++) FX.dust_rgb[c] = rd ? 0.55f * base[c] + 0.45f * (0.25f + 0.5f * rd[c]) : base[c];
  int humans = 0;
  for (int i = 0; i < MAXK; i++) humans += s->k[i].on && s->k[i].human;
  FX.nviews = humans < 1 ? 1 : humans;
  const char *dev = getenv("RACER_FX");
  static const char *const DEV[] = { "land", "wall", "bump", "trick", "boost0", "boost1", "boost2", "boost3", "boost4", "box", "missile",
                                     "homing", "mine", "oil", "shock", "shield", "respawn", "fall", "water", "level1", "level2", "level3" };
  FX.dev_kind = -1;
  FX.off = getenv("RACER_FX_OFF") != NULL;
  static const char *const DEVS[] = { "", "s_dust", "s_snow", "s_mist", "s_stars", "s_flame", "s_drift3", "s_smoke", "s_oil", "s_trick" };
  for (int i = 1; dev && i < 10; i++)
    if (!strcmp(dev, DEVS[i])) FX.dev_state = i;
  if (dev && !strcmp(dev, "rain")) fx_ambient_follow(scene, FXA_RAIN, 1);
  if (dev && !strcmp(dev, "snow")) fx_ambient_follow(scene, FXA_SNOW, 1);
  if (dev && !strcmp(dev, "dust")) fx_ambient_follow(scene, FXA_DUST, 1);
  if (dev && !strcmp(dev, "mist")) fx_ambient_follow(scene, FXA_MIST, 1);
  for (int i = 0; dev && i < (int)(sizeof DEV / sizeof DEV[0]); i++)
    if (!strcmp(dev, DEV[i])) FX.dev_kind = i;

  for (int i = 0; i < MAXK; i++) {
    kfx *f = &FX.k[i];
    const kart *k = &s->k[i];
    if (!k->on) continue;
    f->root = kartgfx_node(i);
    if (!cw_node_valid(f->root)) f->root = FX.root;
    const vehicle_def *vd = &VEH[k->veh];
    float rz = -vd->len * 0.72f, rx = vd->wid * 0.95f;
    f->puff = emit_make(f->root, "Puff", R_PUFF, 100 + i);
    cw_node_set(f->puff.n, "emission_extents", cw_val_vec3(rx, 0.05f, 0.15f));
    cw_node3d_set_position(f->puff.n, (t3_vec3){ 0, -0.55f, rz });
    f->drift = emit_make(f->root, "DriftSparks", R_DRIFT, 120 + i);
    cw_node_set(f->drift.n, "emission_extents", cw_val_vec3(rx, 0.03f, 0.1f));
    cw_node3d_set_position(f->drift.n, (t3_vec3){ 0, -0.5f, rz });
    f->flame = emit_make(f->root, "Flame", R_FLAME, 140 + i);
    cw_node_set(f->flame.n, "emission_extents", cw_val_vec3(vd->wid * 0.45f, 0.06f, 0.04f));
    cw_node3d_set_position(f->flame.n, (t3_vec3){ 0, 0.08f, -vd->len });
    f->flame_src = -1;
    f->puff_mode = PM_NONE;
    f->drift_level = -1;
    f->prev_dlvl = 0;
  }
  for (int i = 0; i < NSPARK; i++) FX.sparks[i] = fx_pnode(FX.root, "Sparks", R_SPARKS, 200 + i);
  for (int i = 0; i < NGRIND; i++) FX.grind[i] = fx_pnode(FX.root, "Grind", R_GRIND, 210 + i);
  for (int i = 0; i < NCHIPS; i++) FX.chips[i] = fx_pnode(FX.root, "Chips", R_CHIPS, 220 + i);
  for (int i = 0; i < NCLOUD; i++) FX.cloud[i] = fx_pnode(FX.root, "Cloud", R_CLOUD, 230 + i);
  for (int i = 0; i < NBOOM; i++) FX.boom[i] = fx_pnode(FX.root, "Boom", R_BOOM, 240 + i);
  for (int i = 0; i < NFLARE; i++) FX.flare[i] = fx_pnode(FX.root, "Flare", R_FLARE, 250 + i);
  for (int i = 0; i < NSPLASH; i++) FX.splash[i] = fx_pnode(FX.root, "Splash", R_SPLASH, 260 + i);
  for (int i = 0; i < NSPARKLE; i++) FX.sparkle[i] = fx_pnode(FX.root, "Sparkle", R_SPARKLE, 270 + i);
  for (int i = 0; i < NRING; i++) {
    cw_node d = cw_node_new("Decal3D", "Ring");
    cw_node_add_child(FX.root, d);
    cw_node_set(d, "blend", cw_val_enum("ADD"));
    cw_node_set(d, "texture", cw_val_string("fx/decals.png"));
    set_region(d, 0, 0, 256, 256);
    cw_node_set(d, "energy", cw_val_float(1.6f));
    cw_node_set(d, "normal_fade", cw_val_float(0.4f));
    cw_node_set(d, "visible", cw_val_bool(false));
    FX.ring[i].d = d;
  }
  for (int i = 0; i < NLIGHT; i++) FX.light[i].n = light_node("FlashLight");
  for (int i = 0; i < 2; i++) FX.mlight[i].n = light_node("MissileLight");
  for (int i = 0; i < NTRAIL; i++) {
    FX.tsmoke[i] = emit_make(FX.root, "TrailSmoke", R_TRAIL_SMOKE, 300 + i);
    FX.tflame[i] = emit_make(FX.root, "TrailFlame", R_TRAIL_FLAME, 320 + i);
    FX.trail_of[i] = -1;
  }
  for (int i = 0; i < MAXHZ; i++) FX.hz_trail[i] = -1;
  fx_amb_init(FX.nviews);
  FX.ready = true;
}

/* ── skid marks ─────────────────────────────────────────────────────── */

/* ── events ─────────────────────────────────────────────────────────── */

static void on_event(const sim *s, const struct sim_event *e) {
  const track *tr = s->track;
  (void)tr;
  int a = e->a;
  bool ka = a >= 0 && a < MAXK && s->k[a].on;
  t3_vec3 up = v3(0, 1, 0), fwd = v3(0, 0, 1);
  if (ka && e->kind != EV_BOX && e->kind != EV_COUNT && e->kind != EV_GO) kart_frame(s, a, &up, &fwd);
  float kr[3] = { 1, 1, 1 };
  if (ka) memcpy(kr, KART_RGB[s->k[a].colour & 7], sizeof kr);
  switch (e->kind) {
  case EV_LAND: {
    float h = e->k;
    t3_vec3 under = v3sub(e->at, v3mul(up, 0.45f));
    dust_cloud(under, up, 0.25f + 0.45f * h, 0.8f + 1.0f * h);
    if (h > 0.35f) {
      spark_burst(under, up, fwd, 1, 0.65f, 0.2f);
      light_flash(v3add(under, v3mul(up, 0.5f)), 12 + 20 * h, 7, 0.28f, 1, 0.7f, 0.3f);
    }
    if (ka && (s->track->s[s->k[a].loc.i].flags & TF_WATER)) {
      burst(NEXT(splash, splash_i), under, up, fwd);
    }
    break;
  }
  case EV_WALL: {
    if (!ka || FX.k[a].wall_cool > 0) break;
    FX.k[a].wall_cool = 3;
    burst(NEXT(grind, grind_i), e->at, up, fwd);
    if (e->k > 0.5f) chips_burst(e->at, up, kr[0], kr[1], kr[2]);
    if (e->k > 0.3f) light_flash(e->at, 6 + 10 * e->k, 5, 0.2f, 1, 0.75f, 0.35f);
    break;
  }
  case EV_BUMP: {
    spark_burst(e->at, v3(0, 1, 0), fwd, 1, 0.85f, 0.4f);
    if (e->k > 0.4f && ka) chips_burst(e->at, up, kr[0], kr[1], kr[2]);
    flare_at(e->at, 0.35f + 0.35f * e->k, 1, 0.8f, 0.4f);
    break;
  }
  case EV_TRICK:
    if (ka) sparkle_burst(s->k[a].p, up, 1, 0.85f, 0.3f);
    break;
  case EV_BOOST: {
    if (!ka) break;
    kfx *f = &FX.k[a];
    const kart *k = &s->k[a];
    int src = e->b;
    t3_vec3 rear = v3sub(k->p, v3mul(kart_fwd(k), VEH[k->veh].len));
    float r = 0.4f, g = 0.8f, b = 1;
    if (src == 1 || src == 3) r = 1, g = 0.65f, b = 0.2f;
    if (src == 4) r = 0.7f, g = 1, b = 0.4f;
    if (src == 2) r = DRIFT_RGB[f->prev_dlvl & 3][0], g = DRIFT_RGB[f->prev_dlvl & 3][1], b = DRIFT_RGB[f->prev_dlvl & 3][2];
    f->flame_src = src == 2 ? 10 + (f->prev_dlvl & 3) : src;
    if (src != 0) flare_at(rear, src == 2 ? 0.6f + 0.15f * (float)f->prev_dlvl : 0.8f, r, g, b);
    if (src != 0) spark_burst(rear, kart_up(k), v3mul(kart_fwd(k), -1), r, g, b);
    if (src == 2 || src == 4) sparkle_burst(rear, kart_up(k), r, g, b);
    if (src != 0) light_flash(v3add(rear, v3mul(kart_up(k), 0.5f)), 16 + 6 * (float)f->prev_dlvl, 9, 0.22f, r, g, b);
    break;
  }
  case EV_DRIFT_LEVEL: {
    if (!ka) break;
    const kart *k = &s->k[a];
    int l = e->b & 3;
    t3_vec3 rear = v3sub(k->p, v3mul(kart_fwd(k), VEH[k->veh].len * 0.7f));
    sparkle_burst(v3sub(rear, v3mul(kart_up(k), 0.3f)), kart_up(k), DRIFT_RGB[l][0], DRIFT_RGB[l][1], DRIFT_RGB[l][2]);
    break;
  }
  case EV_BOX: {
    sparkle_burst(e->at, v3(0, 1, 0), 0.5f, 0.9f, 1);
    sparkle_burst(v3add(e->at, v3(0, 0.3f, 0)), v3(0, 1, 0), 1, 0.9f, 0.4f);
    light_flash(e->at, 10, 6, 0.2f, 0.6f, 0.9f, 1);
    break;
  }
  case EV_HIT: {
    int hk = (int)e->k;
    /* a blast at a player's camera (on the kart it follows, 5 m off) comes out small, thin and dim: full size there its
     * flare, cloud and light whited out the whole view for half a second. Full size from 17 m out */
    float nk = 0.35f + 0.65f * CW_CLAMP((sqrtf(dist2_to_cams(e->at)) - 5.0f) / 12.0f, 0, 1);
    if (hk == HZ_BOMB && ka) {
      /* a kart caught in a bomb's blast: the blast (a = -1) has the fire (the pad rumbles in rumble.c) */
      break;
    }
    if (hk == HZ_BOMB) {
      t3_vec3 u = v3(0, 1, 0);
      {
      cw_node bn = NEXT(boom, boom_i);
      cw_node_set(bn, "size", cw_val_float(1.4f * nk));
      burst(bn, v3add(e->at, v3mul(u, 0.8f)), u, v3(0, 0, 1));
    }
      flare_at(e->at, 4.0f * nk, 1, 0.7f, 0.3f);
      cloud_burst(v3add(e->at, v3mul(u, 0.5f)), u, 0.13f, 0.12f, 0.11f, 0.85f * nk, 4.0f * nk);
      ring_at(e->at, u, v3(0, 0, 1), 1, 0.65f, 0.25f, 14);
      spark_burst(e->at, u, v3(0, 0, 1), 1, 0.7f, 0.2f);
      chips_burst(e->at, u, 0.12f, 0.1f, 0.1f);
      light_flash(e->at, 220 * nk * nk, 34, 0.12f, 1, 0.6f, 0.2f);
      break;
    }
    if (hk == HZ_SHOCK) {
      if (!ka) break;
      ring_at(e->at, up, fwd, 0.45f, 0.65f, 1, 13);
      sparkle_burst(e->at, up, 0.6f, 0.8f, 1);
      light_flash(e->at, 60, 22, 0.15f, 0.5f, 0.7f, 1);
      break;
    }
    t3_vec3 u = v3(0, 1, 0);
    if (ka) u = up;
    {
      cw_node bn = NEXT(boom, boom_i);
      cw_node_set(bn, "size", cw_val_float(1.4f * nk));
      burst(bn, v3add(e->at, v3mul(u, 0.8f)), u, v3(0, 0, 1));
    }
    flare_at(e->at, (hk == HZ_MINE ? 2.2f : 1.8f) * nk, 1, 0.8f, 0.4f);
    cloud_burst(v3add(e->at, v3mul(u, ka ? 1.2f : 0.4f)), u, 0.12f, 0.11f, 0.1f, 0.75f * nk, 2.0f * nk);
    ring_at(e->at, u, v3(0, 0, 1), 1, 0.6f, 0.25f, 7);
    spark_burst(e->at, u, v3(0, 0, 1), 1, 0.7f, 0.2f);
    chips_burst(e->at, u, 0.12f, 0.1f, 0.1f);
    light_flash(e->at, 140 * nk * nk, 26, 0.1f, 1, 0.6f, 0.2f);
    break;
  }
  case EV_SPIN:
    if ((int)e->k == SPIN_OIL && ka) {
      cloud_burst(e->at, up, 0.04f, 0.035f, 0.03f, 0.8f, 1.4f);
      chips_burst(e->at, up, 0.03f, 0.03f, 0.03f);
      burst(NEXT(splash, splash_i), e->at, up, fwd);
    }
    break;
  case EV_WARN:
    break;
  case EV_SHIELD:
    if (!ka) break;
    ring_at(s->k[a].p, up, fwd, 0.35f, 0.8f, 1, 3.2f);
    sparkle_burst(s->k[a].p, up, 0.5f, 0.95f, 1);
    flare_at(s->k[a].p, 1.1f, 0.5f, 0.9f, 1);
    light_flash(s->k[a].p, 30, 10, 0.18f, 0.4f, 0.8f, 1);
    break;
  case EV_RESPAWN:
    if (!ka) break;
    sparkle_burst(s->k[a].p, up, 0.8f, 1, 0.8f);
    sparkle_burst(v3add(s->k[a].p, v3mul(up, 0.6f)), up, 1, 1, 1);
    light_flash(s->k[a].p, 14, 8, 0.2f, 0.8f, 1, 0.8f);
    break;
  case EV_TELE: {
    /* a teleport pad: a ring and a flash of the pad's light where it left, the same where it came out */
    if (!ka) break;
    const arena_rt *ar = s->arena;
    float c[3] = { 0.5f, 0.9f, 1.0f };
    if (ar && e->b >= 0 && e->b < ar->ntele && ar->tele[e->b].col) {
      uint32_t col = ar->tele[e->b].col;
      c[0] = (float)((col >> 16) & 255) / 255.0f, c[1] = (float)((col >> 8) & 255) / 255.0f, c[2] = (float)(col & 255) / 255.0f;
    }
    t3_vec3 up0 = v3(0, 1, 0);
    for (int end = 0; end < 2; end++) {
      t3_vec3 at = end ? s->k[a].p : e->at;
      ring_at(at, up0, fwd, c[0], c[1], c[2], 4.0f);
      sparkle_burst(at, up0, c[0], c[1], c[2]);
      sparkle_burst(v3add(at, v3mul(up0, 1.2f)), up0, 1, 1, 1);
      flare_at(v3add(at, v3mul(up0, 0.8f)), 2.2f, c[0], c[1], c[2]);
      light_flash(v3add(at, v3mul(up0, 1.0f)), 40, 12, 0.22f, c[0], c[1], c[2]);
    }
    break;
  }
  case EV_FALL:
    if (!ka) break;
    cloud_burst(e->at, v3(0, 1, 0), 0.5f, 0.5f, 0.52f, 0.5f, 1.8f);
    spark_burst(e->at, v3(0, 1, 0), v3(0, 0, 1), 0.8f, 0.9f, 1);
    FX.k[a].fall_ticks = 40;
    break;
  case EV_WATER: {
    if (!ka) break;
    t3_vec3 under = v3sub(s->k[a].p, v3mul(up, 0.5f));
    burst(NEXT(splash, splash_i), under, up, fwd);
    cloud_burst(under, up, 0.85f, 0.92f, 0.98f, 0.25f + 0.25f * e->k, 1.0f + e->k);
    break;
  }
  case EV_FINISH:
    if (ka) sparkle_burst(v3add(s->k[a].p, v3mul(up, 1)), up, 1, 0.8f, 0.3f);
    break;
  default: break;
  }
}

/* a forced event for the dev aid: kart 0, once a second */
static void dev_event(const sim *s) {
  if (FX.dev_kind < 0 || s->phase == PH_GRID || s->tick != 330) return;
  const kart *k = &s->k[0];
  struct sim_event e = { .a = 0, .b = 1, .at = k->p };
  switch (FX.dev_kind) {
  case 0: e.kind = EV_LAND, e.k = 0.8f; break;
  case 1: e.kind = EV_WALL, e.k = 0.8f; break;
  case 2: e.kind = EV_BUMP, e.k = 0.8f; break;
  case 3: e.kind = EV_TRICK; break;
  case 4: case 5: case 6: case 7: case 8: e.kind = EV_BOOST, e.b = FX.dev_kind - 4, e.k = 1; FX.k[0].prev_dlvl = 2; break;
  case 9: e.kind = EV_BOX, e.at = v3add(k->p, v3mul(kart_fwd(k), 8)); break;
  case 10: e.kind = EV_HIT, e.k = (float)HZ_MISSILE, e.at = v3add(k->p, v3mul(kart_fwd(k), 6)); break;
  case 11: e.kind = EV_HIT, e.k = (float)HZ_HOMING, e.at = v3add(k->p, v3mul(kart_fwd(k), 6)); break;
  case 12: e.kind = EV_HIT, e.k = (float)HZ_MINE, e.at = v3add(k->p, v3mul(kart_fwd(k), 6)); break;
  case 13: e.kind = EV_SPIN, e.k = (float)SPIN_OIL, e.at = v3add(k->p, v3mul(kart_fwd(k), 6)); break;
  case 14: e.kind = EV_HIT, e.k = (float)HZ_SHOCK; break;
  case 15: e.kind = EV_SHIELD; break;
  case 16: e.kind = EV_RESPAWN; break;
  case 17: e.kind = EV_FALL; break;
  case 18: e.kind = EV_WATER, e.k = 0.8f; break;
  default: e.kind = EV_DRIFT_LEVEL, e.b = FX.dev_kind - 18; break;
  }
  on_event(s, &e);
}

void fx_events(const sim *s) {
  if (!FX.ready || FX.off) return;
  for (int i = 0; i < s->ev_n; i++) on_event(s, &s->ev[i]);
  dev_event(s);
}

/* ── tick ───────────────────────────────────────────────────────────── */

static void refresh_cams(void) {
  if (!cw_node_valid(FX.scene)) return;
  if (!cw_node_valid(FX.cam0)) FX.cam0 = find_cam(FX.scene);
  int n = 0;
  for (int v = 0; v < FX.nviews && v < MAXV; v++) {
    cw_node c = v == 0 ? FX.cam0 : (cw_node){ 0 };
    if (v > 0) {
      char name[16];
      snprintf(name, sizeof name, "Camera%d", v + 1);
      c = cw_node_get(FX.scene, name);
    }
    if (!cw_node_valid(c)) continue;
    FX.cam[n] = c;
    t3_vec3 p = cw_node3d_global_position(c);
    FX.cam_vel[n] = v3mul(v3sub(p, FX.cam_prev[n]), 60.0f);
    FX.cam_prev[n] = FX.cam_pos[n] = p;
    n++;
  }
  FX.ncams = n;
}

static float dist2_to_cams(t3_vec3 p) {
  float best = 1e30f;
  for (int c = 0; c < FX.ncams; c++) {
    t3_vec3 d = v3sub(p, FX.cam_pos[c]);
    float q = v3dot(d, d);
    if (q < best) best = q;
  }
  return FX.ncams ? best : 0;
}

static void kart_tick(const sim *s, int i) {
  kfx *f = &FX.k[i];
  const kart *k = &s->k[i];
  const vehicle_def *vd = &VEH[k->veh];
  const track *tr = s->track;
  const tsample *ts = &tr->s[k->loc.i];
  float spd = k->speed < 0 ? -k->speed : k->speed;
  bool ground = k->grounded || (k->loc.on_road && k->loc.height < 1.1f);
  bool near = f->dist2 < 45.0f * 45.0f, mid = f->dist2 < 110.0f * 110.0f;
  bool race = s->phase != PH_GRID;

  if (f->wall_cool > 0) f->wall_cool--;
  if (f->rough_cool > 0) f->rough_cool--;
  if (f->fall_ticks > 0) f->fall_ticks--;

  /* where the tyres meet the road, in the kart's frame */
  float gy = -(k->loc.on_road ? CW_CLAMP(k->loc.height, 0.3f, 1.0f) : 0.62f);

  /* the puff */
  int mode = PM_NONE;
  bool water = (ts->flags & TF_WATER) && ground && spd > 4;
  if (race && k->respawn_ticks <= 0) {
    if (f->fall_ticks > 0) mode = PM_FALL;
    else if (k->spin_ticks > 0 && k->spin_kind == SPIN_OIL) mode = PM_OIL;
    else if (water) mode = PM_MIST;
    else if (ground && spd > 8 && k->drifting) mode = ts->surf == SURF_ICE ? PM_SNOW : (ts->flags & TF_ROUGH) || ts->surf == SURF_DIRT ? PM_DUST : PM_SMOKE;
    else if (ground && spd > 6 && ts->surf == SURF_ICE) mode = PM_SNOW;
    else if (ground && spd > 5 && ((ts->flags & TF_ROUGH) || ts->surf == SURF_DIRT)) mode = PM_DUST;
  }
  if (i == 0 && FX.dev_state >= 1 && FX.dev_state <= 3) mode = FX.dev_state == 1 ? PM_DUST : FX.dev_state == 2 ? PM_SNOW : PM_MIST;
  if (i == 0 && FX.dev_state == 7) mode = PM_SMOKE;
  if (i == 0 && FX.dev_state == 8) mode = PM_OIL;
  if (i == 0 && FX.dev_state) water = water || FX.dev_state == 3;
  if (!near && mode != PM_FALL) mode = mid && mode != PM_MIST ? mode : PM_NONE;
  if (mode != PM_NONE && mode != f->puff_mode) {
    f->puff_mode = mode;
    switch (mode) {
    case PM_DUST: set_puff_ramp(f->puff.n, FX.dust_rgb[0], FX.dust_rgb[1], FX.dust_rgb[2], 0.42f); break;
    case PM_SMOKE: set_puff_ramp(f->puff.n, 0.78f, 0.78f, 0.8f, 0.5f); break;
    case PM_SNOW: set_puff_ramp(f->puff.n, 0.95f, 0.98f, 1, 0.6f); break;
    case PM_MIST: set_puff_ramp(f->puff.n, 0.85f, 0.92f, 0.98f, 0.4f); break;
    case PM_OIL: set_puff_ramp(f->puff.n, 0.06f, 0.055f, 0.05f, 0.8f); break;
    default: set_puff_ramp(f->puff.n, 0.55f, 0.55f, 0.58f, 0.6f); break;
    }
  }
  if (cw_node_valid(f->puff.n)) {
    cw_node3d_snapshot(f->puff.n);
    cw_node3d_set_position(f->puff.n, (t3_vec3){ 0, mode == PM_FALL || mode == PM_OIL ? 0 : gy + 0.07f, -vd->len * 0.72f });
  }
  emit_set(&f->puff, mode != PM_NONE);

  /* drift sparks */
  int dl = k->drifting && ground && near ? k->drift_level : -1;
  if (i == 0 && FX.dev_state == 6) dl = 3;
  if (dl >= 0 && dl != f->drift_level) {
    f->drift_level = dl;
    set_ramp(f->drift.n, DRIFT_RGB[dl][0] * 1.0f, DRIFT_RGB[dl][1], DRIFT_RGB[dl][2], DRIFT_RGB[dl][0] * 0.4f, DRIFT_RGB[dl][1] * 0.3f, DRIFT_RGB[dl][2] * 0.5f);
    cw_node_set(f->drift.n, "energy", cw_val_float(dl == 0 ? 4.0f : 9.0f));
  }
  if (cw_node_valid(f->drift.n)) {
    cw_node3d_snapshot(f->drift.n);
    cw_node3d_set_position(f->drift.n, (t3_vec3){ 0, gy + 0.1f, -vd->len * 0.72f });
  }
  emit_set(&f->drift, dl >= 1);

  /* the exhaust */
  bool boosting = k->boost_ticks > 0 && race && mid;
  if (i == 0 && FX.dev_state == 5) boosting = true;
  if (boosting) {
    if (f->flame_src >= 0) {
      /* colour by what gave the boost, set when it changes */
      static const float C[5][3] = { { 0.35f, 0.65f, 1 }, { 1, 0.6f, 0.15f }, { 0.4f, 0.8f, 1 }, { 1, 0.6f, 0.15f }, { 0.55f, 1, 0.35f } };
      float r, g, b;
      if (f->flame_src >= 10) r = DRIFT_RGB[f->flame_src - 10][0], g = DRIFT_RGB[f->flame_src - 10][1], b = DRIFT_RGB[f->flame_src - 10][2];
      else r = C[f->flame_src][0], g = C[f->flame_src][1], b = C[f->flame_src][2];
      char s2[200];
      snprintf(s2, sizeof s2, "\"0 1 1 1 1, 0.15 %.2f %.2f %.2f 1, 0.5 %.2f %.2f %.2f 0.8, 1 %.2f %.2f %.2f 0\"", r, g, b, r * 0.6f, g * 0.6f, b * 0.9f, r * 0.2f,
               g * 0.2f, b * 0.6f);
      fx_set_str(f->flame.n, "color_ramp", s2);
      f->flame_src = -1;
    }
    int q = (int)(CW_CLAMP(k->boost_k, 0.8f, 1.6f) * 4.0f) + (k->boost_ticks > 25 ? 8 : 0);
    if (q != f->flame_size) {
      f->flame_size = q;
      cw_node_set(f->flame.n, "size", cw_val_float(0.3f + 0.1f * (float)(q & 7) + (q >= 8 ? 0.12f : 0)));
    }
  }
  emit_set(&f->flame, boosting);

  /* spray, made when it is first wanted */
  if (water && near && !cw_node_valid(f->spray.n)) {
    f->spray = emit_make(f->root, "Spray", R_SPRAY, 160 + i);
    cw_node_set(f->spray.n, "emission_extents", cw_val_vec3(vd->wid * 0.95f, 0.05f, vd->len * 0.5f));
  }
  if (cw_node_valid(f->spray.n)) {
    cw_node3d_set_position(f->spray.n, (t3_vec3){ 0, gy + 0.1f, -vd->len * 0.1f });
    emit_set(&f->spray, water && near);
  }

  /* stars over a spun kart, a sparkle trail through a trick */
  int sm = ST_NONE;
  if (near && race) {
    if (k->spin_ticks > 0 && k->spin_kind != SPIN_OIL) sm = k->spin_kind == SPIN_SHOCK ? ST_SHOCK : k->spin_kind == SPIN_BOOM ? ST_BOOM : ST_HIT;
    else if (k->trick_ticks > 0) sm = ST_TRICK;
  }
  if (i == 0 && FX.dev_state == 4) sm = ST_HIT;
  if (i == 0 && FX.dev_state == 9) sm = ST_TRICK;
  if (sm != ST_NONE && !cw_node_valid(f->stars.n)) {
    f->stars = emit_make(f->root, "Stars", R_STARS, 180 + i);
    f->stars_mode = ST_NONE;
  }
  if (sm != ST_NONE && sm != f->stars_mode) {
    f->stars_mode = sm;
    switch (sm) {
    case ST_SHOCK: set_ramp(f->stars.n, 0.8f, 0.95f, 1, 0.2f, 0.4f, 1); break;
    case ST_BOOM: set_ramp(f->stars.n, 1, 0.8f, 0.3f, 0.6f, 0.15f, 0.05f); break;
    case ST_TRICK: set_ramp(f->stars.n, 1, 1, 0.8f, 1, 0.55f, 0.1f); break;
    default: set_ramp(f->stars.n, 1, 0.95f, 0.5f, 1, 0.65f, 0.1f); break;
    }
    cw_node_set(f->stars.n, "emission_radius", cw_val_float(sm == ST_TRICK ? 0.8f : 0.5f));
    cw_node3d_set_position(f->stars.n, (t3_vec3){ 0, sm == ST_TRICK ? 0.1f : 0.9f, 0 });
  }
  if (cw_node_valid(f->stars.n)) emit_set(&f->stars, sm != ST_NONE);

  /* debris kicked up on rough ground: a chip burst now and then from the wheel that is turning it up */
  if (near && ground && spd > 12 && f->rough_cool <= 0 && (ts->flags & TF_ROUGH) && race) {
    f->rough_cool = 14 + (i & 3) * 3;
    t3_vec3 rz = v3sub(k->p, v3mul(kart_fwd(k), vd->len * 0.7f));
    rz = v3sub(rz, v3mul(kart_up(k), 0.5f));
    cw_node c = NEXT(chips, chips_i);
    set_ramp(c, FX.dust_rgb[0] * 0.8f, FX.dust_rgb[1] * 0.8f, FX.dust_rgb[2] * 0.8f, FX.dust_rgb[0] * 0.5f, FX.dust_rgb[1] * 0.5f, FX.dust_rgb[2] * 0.5f);
    burst(c, rz, kart_up(k), kart_fwd(k));
  }

  /* skid marks: a stamp every SKID_STEP metres of a drift, per rear wheel, along the direction it travels */
  bool skid = k->drifting && ground && spd > 8 && near && !water;
  for (int w = 0; w < 2; w++) {
    if (!skid) {
      f->skid_on[w] = false;
      continue;
    }
    t3_vec3 right = kart_right(k), fw = kart_fwd(k);
    float sx = (w ? -1.0f : 1.0f) * vd->wid * 0.95f;
    /* kart local +x is the left; kart_right is -x, so a wheel at local +x is at -right */
    t3_vec3 wp = v3add(v3sub(k->p, v3mul(fw, vd->len * 0.72f)), v3mul(right, -sx));
    if (f->skid_on[w]) {
      t3_vec3 d = v3sub(wp, f->skid_last[w]);
      if (v3dot(d, d) < SKID_STEP * SKID_STEP) continue;
    }
    f->skid_last[w] = wp;
    f->skid_on[w] = true;
    /* the road under the wheel */
    tsample m = track_at_loc(tr, &k->loc, -vd->len * 0.72f);
    t3_vec3 rel = v3sub(wp, m.p);
    t3_vec3 on = v3add(v3sub(wp, v3mul(m.u, v3dot(rel, m.u))), v3mul(m.u, 0.03f));
    t3_vec3 dir = k->v;
    dir = v3sub(dir, v3mul(m.u, v3dot(dir, m.u)));
    if (v3len(dir) < 1) dir = fw;
    dir = v3norm(dir);
    t3_vec3 x = v3cross(v3mul(dir, -1), m.u);
    int di = FX.skid_i++ % NSKID;
    if (!cw_node_valid(FX.skid[di])) {
      cw_node d = cw_node_new("Decal3D", "Skid");
      cw_node_add_child(FX.root, d);
      cw_node_set(d, "blend", cw_val_enum("MULTIPLY"));
      cw_node_set(d, "texture", cw_val_string("fx/decals.png"));
      set_region(d, 256, 0, 128, 256);
      cw_node_set(d, "normal_fade", cw_val_float(0.3f));
      cw_node_set(d, "size", cw_val_vec3(0.34f, SKID_STEP * 1.25f, 0.5f));
      FX.skid[di] = d;
    }
    cw_node d = FX.skid[di];
    cw_node3d_set_position(d, on);
    cw_node3d_set_quaternion(d, quat_basis(x, v3mul(dir, -1), m.u));
    cw_node3d_snapshot(d);
    float lv = ts->surf == SURF_ICE ? 0.55f : 0.12f;
    cw_node_set(d, "color", cw_val_color(lv, lv, lv, 0.7f));
    cw_node_set(d, "visible", cw_val_bool(true));
    FX.skid_birth[di] = FX.ticks;
    FX.skid_alpha[di] = 7;
  }

  /* the speed lines */
  float top = vd->top;

  float raw = CW_CLAMP((spd / top - 0.8f) * 3.0f, 0, 1) * 0.8f;
  if (k->boost_ticks > 0) raw += 0.35f + 0.15f * CW_CLAMP(k->boost_k - 1, 0, 1);
  /* drafting: a kart close ahead in the same lane makes the road rush */
  t3_vec3 fw = kart_fwd(k);
  for (int j = 0; j < MAXK && raw < 1; j++) {
    if (j == i || !s->k[j].on) continue;
    t3_vec3 d = v3sub(s->k[j].p, k->p);
    float ahead = v3dot(d, fw), side = v3dot(d, kart_right(k));
    if (ahead > 2 && ahead < 16 && side < 2.2f && side > -2.2f) {
      raw += 0.18f;
      break;
    }
  }
  raw = CW_CLAMP(raw, 0, 1);
  f->sl += (raw - f->sl) * (raw > f->sl ? 0.12f : 0.06f);
  f->prev_dlvl = k->drifting ? k->drift_level : f->prev_dlvl;
  if (!k->drifting && f->prev_dlvl && k->boost_ticks == 0) f->prev_dlvl = 0;
}

static void light_update(light *l) {
  if (!cw_node_valid(l->n)) return;
  if (l->e0 <= 0) return;
  float e = l->age < 2 ? l->e0 * (float)(l->age + 1) / 2 : l->e0 * cw_exp(-(float)(l->age - 2) * l->decay);
  l->age++;
  if (e < 0.4f) {
    cw_node_set(l->n, "visible", cw_val_bool(false));
    cw_node_set(l->n, "energy", cw_val_float(0));
    l->e0 = 0;
    return;
  }
  cw_node_set(l->n, "visible", cw_val_bool(true));
  cw_node_set(l->n, "energy", cw_val_float(e));
}

static void skids_update(void) {
  for (int i = 0; i < NSKID; i++) {
    if (!cw_node_valid(FX.skid[i]) || FX.skid_alpha[i] == 0) continue;
    int age = FX.ticks - FX.skid_birth[i];
    if (age >= SKID_LIFE) {
      cw_node_set(FX.skid[i], "visible", cw_val_bool(false));
      FX.skid_alpha[i] = 0;
      continue;
    }
    /* full for the first 40 percent of its life, then it fades in steps */
    float t = (float)age / SKID_LIFE;
    int lvl = t < 0.4f ? 7 : (int)(7.0f * (1 - t) / 0.6f + 0.5f);
    if (lvl != FX.skid_alpha[i]) {
      FX.skid_alpha[i] = lvl < 1 ? 1 : lvl;
      cw_value c;
      cw_node_get_prop(FX.skid[i], "color", &c);
      cw_node_set(FX.skid[i], "color", cw_val_color(c.v[0], c.v[1], c.v[2], 0.7f * (float)FX.skid_alpha[i] / 7.0f));
    }
  }
}

static void hazards_tick(const sim *s) {
  int nm = 0;
  for (int h = 0; h < MAXHZ; h++) {
    const hazard *z = &s->hz[h];
    bool missile = z->kind == HZ_MISSILE || z->kind == HZ_HOMING;
    int t = FX.hz_trail[h];
    if (!missile) {
      if (t >= 0) {
        emit_set(&FX.tsmoke[t], false);
        emit_set(&FX.tflame[t], false);
        FX.trail_of[t] = -1;
        FX.hz_trail[h] = -1;
      }
      continue;
    }
    if (t < 0) {
      for (int j = 0; j < NTRAIL; j++)
        if (FX.trail_of[j] < 0) {
          t = j;
          break;
        }
      if (t < 0) continue;
      FX.trail_of[t] = h;
      FX.hz_trail[h] = t;
      if (getenv("RACER_FX_LOG")) cw_info("racer fx: trail %d for hazard %d (owner %d) at tick %d", t, h, z->owner, FX.ticks);
      cw_node3d_set_position(FX.tsmoke[t].n, z->p);
      cw_node3d_snapshot(FX.tsmoke[t].n);
      cw_node3d_set_position(FX.tflame[t].n, z->p);
      cw_node3d_snapshot(FX.tflame[t].n);
    }
    t3_vec3 tail = v3sub(z->p, v3mul(z->f, 0.7f));
    cw_node3d_snapshot(FX.tsmoke[t].n);
    cw_node3d_set_position(FX.tsmoke[t].n, tail);
    cw_node3d_snapshot(FX.tflame[t].n);
    cw_node3d_set_position(FX.tflame[t].n, tail);
    orient(FX.tflame[t].n, v3(0, 1, 0), z->f);
    emit_set(&FX.tsmoke[t], true);
    emit_set(&FX.tflame[t], true);
    if (nm < 2 && cw_node_valid(FX.mlight[nm].n)) {
      cw_node3d_set_position(FX.mlight[nm].n, tail);
      float fl = 26 + 8 * cw_sin((float)(FX.ticks * 3 + h * 7));
      cw_node_set(FX.mlight[nm].n, "visible", cw_val_bool(true));
      cw_node_set(FX.mlight[nm].n, "energy", cw_val_float(fl));
      cw_node_set(FX.mlight[nm].n, "range", cw_val_float(9));
      cw_node_set(FX.mlight[nm].n, "color", cw_val_color(1, 0.55f, 0.2f, 1));
      FX.mlight[nm].e0 = 1;
      nm++;
    }
  }
  for (int j = nm; j < 2; j++) {
    if (cw_node_valid(FX.mlight[j].n) && FX.mlight[j].e0 > 0) {
      FX.mlight[j].e0 = 0;
      cw_node_set(FX.mlight[j].n, "visible", cw_val_bool(false));
      cw_node_set(FX.mlight[j].n, "energy", cw_val_float(0));
    }
  }
  /* idle trails: let their smoke finish, then hide */
  for (int j = 0; j < NTRAIL; j++)
    if (FX.trail_of[j] < 0) {
      emit_set(&FX.tsmoke[j], false);
      emit_set(&FX.tflame[j], false);
    }
}

void fx_tick(const sim *s) {
  if (!FX.ready || FX.off) return;
  FX.ticks++;
  refresh_cams();
  for (int i = 0; i < MAXK; i++) {
    if (!s->k[i].on) continue;
    FX.k[i].dist2 = dist2_to_cams(s->k[i].p);
    kart_tick(s, i);
  }
  hazards_tick(s);
  for (int i = 0; i < NLIGHT; i++) light_update(&FX.light[i]);
  skids_update();
  rings_update();
}

void fx_frame(const sim *s, float alpha) {
  (void)s, (void)alpha;
  if (!FX.ready) return;
  fx_amb_frame(FX.cam_pos, FX.cam_vel, FX.ncams);
}


float fx_speed_lines(int kart) {
  if (kart < 0 || kart >= MAXK) return 0;
  return FX.k[kart].sl;
}
