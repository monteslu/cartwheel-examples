/* racer's ambient emitters: particles a map's scenery() asks for (rain and snow that follow each view's camera,
 * embers, steam, dust, mist and fireflies anchored in the world). They only read the camera positions fx.c hands
 * them, so they are render only and the same on every host. */
#include <stdio.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/particles.h"
#include "fx_int.h"

#define MAXAMB 64

typedef struct amb {
  cw_node n;
  int kind;
  bool follow;
  int view;          /* follower: which camera */
  int base_amount;
  float fade_dist;   /* world emitters: off beyond this from every camera (0: never) */
  float alpha;       /* the tint alpha now (quantised), 1 = full */
  bool shown;
  t3_vec3 box;       /* half extents of the emission box */
} amb;

static struct {
  amb a[MAXAMB];
  int n;
  int nviews;
} AM;

/* one recipe per kind. A follower's box sits above the camera and the particles fall through the view. */
static const char *const REC_RAIN[] = { "amount", "900", "lifetime", "0.8", "lifetime_random", "0.2", "emission_shape", "BOX",
  "emission_extents", "vec3(13, 0.3, 13)", "direction", "vec3(0, -1, 0)", "spread", "2", "speed_min", "22", "speed_max", "27",
  "gravity", "vec3(0, -6, 0)", "draw_mode", "STRETCHED", "stretch", "0.03", "size", "0.014", "size_random", "0.3",
  "color_ramp", "\"0 0.7 0.78 0.9 0, 0.12 0.72 0.8 0.92 0.55, 0.9 0.7 0.78 0.9 0.5, 1 0.7 0.78 0.9 0\"", "blend", "ALPHA", "local_coords", "false", NULL };
static const char *const REC_SNOW[] = { "amount", "1100", "lifetime", "6", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(18, 0.3, 18)", "direction", "vec3(0, -1, 0)", "spread", "25", "speed_min", "1.2", "speed_max", "2.2",
  "gravity", "vec3(0, 0, 0)", "size", "0.11", "size_random", "0.5", "spin", "2",
  "color_ramp", "\"0 0.95 0.97 1 0, 0.08 0.95 0.97 1 0.85, 0.9 0.95 0.97 1 0.8, 1 0.95 0.97 1 0\"", "blend", "ALPHA",
  "turbulence", "0.9", "turbulence_scale", "0.3", "turbulence_speed", "0.4", "local_coords", "false", NULL };
static const char *const REC_EMBERS[] = { "amount", "80", "lifetime", "4", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(6, 0.3, 6)", "direction", "vec3(0, 1, 0)", "spread", "30", "speed_min", "0.6", "speed_max", "1.6",
  "gravity", "vec3(0, 0.3, 0)", "size", "0.06", "size_random", "0.6", "size_curve", "\"0 0.2, 0.1 1, 1 0.1\"",
  "color_ramp", "\"0 1 0.85 0.4 1, 0.3 1 0.45 0.1 1, 1 0.5 0.08 0.02 0\"", "energy", "5", "blend", "ADD",
  "turbulence", "1.4", "turbulence_scale", "0.3", "local_coords", "false", NULL };
static const char *const REC_STEAM[] = { "amount", "40", "lifetime", "3", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(0.4, 0.1, 0.4)", "direction", "vec3(0, 1, 0)", "spread", "14", "speed_min", "1.4", "speed_max", "2.6",
  "gravity", "vec3(0.3, 0.5, 0)", "damping", "0.5", "size", "0.9", "size_random", "0.4", "size_curve", "\"0 0.3, 0.3 1, 1 2.6\"",
  "color_ramp", "\"0 0.9 0.92 0.95 0, 0.1 0.9 0.92 0.95 0.45, 1 0.9 0.92 0.95 0\"", "blend", "ALPHA", "soft_distance", "0.5",
  "turbulence", "1", "turbulence_scale", "0.3", "local_coords", "false", NULL };
static const char *const REC_DUST[] = { "amount", "120", "lifetime", "6", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(14, 1.5, 14)", "direction", "vec3(1, 0.1, 0.3)", "spread", "40", "speed_min", "0.6", "speed_max", "2.4",
  "gravity", "vec3(0, 0, 0)", "size", "1.4", "size_random", "0.6", "size_curve", "\"0 0.5, 0.3 1, 1 1.4\"",
  "color_ramp", "\"0 0.62 0.5 0.36 0, 0.2 0.62 0.5 0.36 0.16, 1 0.62 0.5 0.36 0\"", "blend", "ALPHA", "soft_distance", "1",
  "turbulence", "1.2", "turbulence_scale", "0.12", "local_coords", "false", NULL };
static const char *const REC_MIST[] = { "amount", "70", "lifetime", "9", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(20, 1, 20)", "direction", "vec3(1, 0, 0.4)", "spread", "60", "speed_min", "0.3", "speed_max", "1",
  "gravity", "vec3(0, 0, 0)", "size", "5", "size_random", "0.4", "size_curve", "\"0 0.6, 0.3 1, 1 1.3\"",
  "color_ramp", "\"0 0.8 0.86 0.92 0, 0.25 0.8 0.86 0.92 0.14, 1 0.8 0.86 0.92 0\"", "blend", "ALPHA", "soft_distance", "2",
  "turbulence", "0.6", "turbulence_scale", "0.06", "local_coords", "false", NULL };
static const char *const REC_FLIES[] = { "amount", "60", "lifetime", "5", "lifetime_random", "0.5", "emission_shape", "BOX",
  "emission_extents", "vec3(10, 1.5, 10)", "direction", "vec3(0, 1, 0)", "spread", "180", "speed_min", "0.1", "speed_max", "0.5",
  "gravity", "vec3(0, 0, 0)", "size", "0.09", "size_random", "0.4", "size_curve", "\"0 0, 0.2 1, 0.5 0.3, 0.8 1, 1 0\"",
  "color_ramp", "\"0 0.7 1 0.35 1, 1 0.9 1 0.4 1\"", "energy", "6", "blend", "ADD",
  "turbulence", "1.1", "turbulence_scale", "0.25", "turbulence_speed", "0.5", "local_coords", "false", NULL };

static const char *const *const RECIPES[FXA_KINDS] = { REC_RAIN, REC_SNOW, REC_EMBERS, REC_STEAM, REC_DUST, REC_MIST, REC_FLIES };
static const char *const NAMES[FXA_KINDS] = { "Rain", "Snow", "Embers", "Steam", "Dust", "Mist", "Fireflies" };
/* a follower's box rides above the camera by this much; rain starts high, the rest near eye height */
static const float FOLLOW_UP[FXA_KINDS] = { 9, 8, 1, 1, 1, 1, 1 };

static amb *amb_new(cw_node parent, int kind, float density) {
  if (AM.n >= MAXAMB || kind < 0 || kind >= FXA_KINDS) return NULL;
  amb *a = &AM.a[AM.n++];
  memset(a, 0, sizeof *a);
  a->kind = kind;
  a->n = fx_pnode(parent, NAMES[kind], RECIPES[kind], 9000 + AM.n);
  cw_value v;
  cw_node_get_prop(a->n, "amount", &v);
  a->base_amount = (int)((float)v.i * (density < 0.05f ? 0.05f : density));
  cw_node_set(a->n, "amount", cw_val_int(a->base_amount));
  a->alpha = 1;
  return a;
}

void fx_amb_reset(void) { memset(&AM, 0, sizeof AM); }

cw_node fx_ambient_at(cw_node scene, int kind, t3_vec3 at, t3_vec3 extents, float density, float fade_dist) {
  amb *a = amb_new(scene, kind, density);
  if (!a) return (cw_node){ 0 };
  a->fade_dist = fade_dist;
  a->box = extents;
  cw_node3d_set_position(a->n, at);
  cw_node_set(a->n, "emission_extents", cw_val_vec3(extents.x, extents.y, extents.z));
  cw_node_set(a->n, "emitting", cw_val_bool(true));
  a->shown = true;
  return a->n;
}

void fx_ambient_follow(cw_node scene, int kind, float density) {
  for (int v = 0; v < MAXV; v++) {
    amb *a = amb_new(scene, kind, density);
    if (!a) return;
    a->follow = true;
    a->view = v;
    cw_node_set(a->n, "visible", cw_val_bool(false));
  }
}

/* split screen: each view needs less of a follower, and a view that does not exist gets none */
void fx_amb_init(int nviews) {
  AM.nviews = nviews;
  float k = nviews >= 3 ? 0.5f : nviews == 2 ? 0.7f : 1.0f;
  for (int i = 0; i < AM.n; i++) {
    amb *a = &AM.a[i];
    if (!a->follow) continue;
    cw_node_set(a->n, "amount", cw_val_int((int)((float)a->base_amount * k)));
    cw_node_set(a->n, "emitting", cw_val_bool(a->view < nviews));
    a->shown = a->view < nviews;
    cw_node_set(a->n, "visible", cw_val_bool(a->shown));
  }
}

void fx_amb_frame(const t3_vec3 *cam, const t3_vec3 *vel, int ncams) {
  for (int i = 0; i < AM.n; i++) {
    amb *a = &AM.a[i];
    if (a->follow) {
      if (a->view >= ncams) continue;
      /* the box rides above the camera and a little ahead of where it is going, so the swept volume is full */
      t3_vec3 p = cam[a->view], vv = vel[a->view];
      float lead = a->kind == FXA_RAIN ? 0.25f : 0.5f;
      p.x += vv.x * lead, p.z += vv.z * lead;
      p.y += FOLLOW_UP[a->kind];
      cw_node3d_set_position(a->n, p);
      continue;
    }
    if (a->fade_dist <= 0 || ncams < 1) continue;
    t3_vec3 at = cw_node3d_global_position(a->n);
    float best = 1e30f;
    for (int c = 0; c < ncams; c++) {
      float dx = cam[c].x - at.x, dy = cam[c].y - at.y, dz = cam[c].z - at.z;
      float d = dx * dx + dy * dy + dz * dz;
      if (d < best) best = d;
    }
    float d = sqrtf(best);
    float want = d >= a->fade_dist ? 0 : d <= a->fade_dist * 0.6f ? 1 : (a->fade_dist - d) / (a->fade_dist * 0.4f);
    want = floorf(want * 6.0f + 0.5f) / 6.0f; /* six steps: a tint change costs a property write */
    bool show = want > 0;
    if (show != a->shown) {
      a->shown = show;
      cw_node_set(a->n, "emitting", cw_val_bool(show));
      cw_node_set(a->n, "visible", cw_val_bool(show));
    }
    if (show && want != a->alpha) {
      a->alpha = want;
      cw_node_set(a->n, "color", cw_val_color(1, 1, 1, want));
    }
  }
}
