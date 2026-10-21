/* pugball's 3D side: everything here only reads the simulation (game.h)
 * and draws it, on the fixed tick, so every host draws the same frame. */
#ifndef PUGBALL_WORLD_H
#define PUGBALL_WORLD_H

#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

/* stadium themes (assets/scenes/stadium*.cwscene) */
enum { TH_PARK, TH_NEON, TH_BEACH, TH_SNOW, TH_GARDEN, TH_KINDS };

/* textures.c: procedural PBR sets (albedo sRGB, normal, roughness, ao), made
 * on first use, deterministic integer hashing (debug field tex.hash) */
enum {
  TEX_GRASS, TEX_BRICK, TEX_SNOW, TEX_METAL, TEX_ASPHALT, TEX_PANEL, TEX_WOOD, TEX_SAND, TEX_FUR, TEX_PAD, TEX_LEAF, TEX_BARK,
  TEX_TABBY, TEX_CONCRETE, TEX_SEAT, TEX_HEDGE, TEX_KINDS
};
typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
const tex_set *tex_get(int kind);
void tex_apply(t3_material *m, int kind, float repeat, float normal_scale, float ao_intensity);
void tex_apply_uv(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity);
void tex_free_all(void);
extern uint32_t tex_hash;
t3_color lin(float r, float g, float b);           /* screen colour -> linear */
/* the forehead creases on the unit skull (front = +z): 0 off a crease, 1 in
 * the valley. The skull geometry cuts them; the head map darkens them. */
static inline float pug_crease(float x, float y, float z) {
  if (z < 0.45f || x > 0.5f || x < -0.5f) return 0;
  float t = y + 0.9f * x * x, g = 0;
  for (int k = 0; k < 3; k++) {
    float d = (t - (0.34f + k * 0.13f)) / 0.04f;
    g += 1 / (1 + d * d * d * d);
  }
  float fx = 1 - (x * x) / 0.25f, fz = (z - 0.45f) / 0.2f;
  return g * fx * (fz > 1 ? 1 : fz);
}
t3_texture *tex_pug_head(int coat, const float rgb[3], bool black); /* the mask gradient, in a sphere's uv */
t3_texture *tex_pug_body(int coat, const float rgb[3], const float belly[3], bool black); /* belly (v 0) to back (v 1) */
t3_geometry *geo_rounded_box(float w, float h, float d, float r, int seg);

/* models.c: pugs, the cat, cosmetics */
enum { PUG_FAWN, PUG_BLACK, PUG_APRICOT, PUG_SILVER, PUG_KINDS };
extern const char *const PUG_COAT_NAME[PUG_KINDS];
enum { HAT_NONE, HAT_CAP, HAT_CROWN, HAT_PARTY, HAT_BEANIE, HAT_TOPHAT, HAT_KINDS };
extern const char *const HAT_NAME[HAT_KINDS];
enum { TRAIL_FIRE, TRAIL_BUBBLES, TRAIL_RAINBOW, TRAIL_STARS, TRAIL_HEARTS, TRAIL_KINDS };
extern const char *const TRAIL_NAME[TRAIL_KINDS];
extern const float TEAM_RGB[2][3];
typedef struct pug_look { int coat, hat, harness, trail; } pug_look;
typedef struct pug_model {
  t3_object *root;          /* placed by world.c (the sim's pose) */
  t3_object *body;          /* squash and stretch */
  t3_object *head, *ear[2], *tail, *jaw, *tongue, *leg[4], *lids[2], *pack, *flame, *hat;
  t3_object *roll;          /* leans into turns */
  t3_material *flame_mat, *eye_glint;
  t3_object *far;           /* the whole pug as one light mesh (split screen, far from a view) */
  pug_look look;
  int team;
} pug_model;
void pug_build(pug_model *m, int team, pug_look look);
typedef struct cat_model {
  t3_object *root;          /* at the sim's ball, rolled */
  t3_object *body;          /* squash */
  t3_object *head, *ear[2], *tail, *paw[4], *eyes_open, *eyes_dizzy, *eyes_angry, *mouth_grump, *mouth_yowl, *whisk;
  t3_object *far;           /* the whole cat as one light mesh */
} cat_model;
void cat_build(cat_model *c);

/* fx.c: everything that flies */
void fx_init(cw_node scene, int theme);
void fx_reset(void);
void fx_events(const sim *s);       /* this tick's sim events -> effects */
void fx_tick(const sim *s);         /* fixed tick */
float fx_shake(void);               /* camera shake amount */
void fx_confetti(t3_vec3 at, int team);
void fx_set_trail(int pug, int kind);  /* the boost trail cosmetic */

/* scenery.c: the stadium around the pitch, its crowd and its weather */
void scenery_build(cw_node scene, int theme, uint32_t seed);
void scenery_tick(const sim *s, int ticks, int cheer);
void scenery_reset(void);
void scenery_quality(float k, float grass); /* share of the crowd and of the grass drawn, 0..1 */
void scenery_crowd_detail(t3_vec3 eye, float focal_pixels);
void scenery_light_crowd(bool light); /* the crowd's light meshes (split screen) or the full ones */
void scenery_view_detail(t3_vec3 eye); /* split screen, per view: the grass and crowd thinned with distance */
void scenery_floodlights(float strength); /* night: the floodlight ring's spot lights (strength 0: none) */
void scenery_flood_quality(void);         /* their shadows and count at the graphics settings */
extern uint32_t scenery_hash;

/* the arena's plan (the rounded rectangle of its walls), sampled every half
 * metre with outward normals and the running length; and a sweep of a
 * profile (offset along that normal, height, inward normal) round it */
typedef struct psample { float x, z, nx, nz, s; } psample;
#define MAXPS 1024
extern psample PS[MAXPS];
extern int nps;
void plan_build(void);
typedef struct prof { float d, y, nd, ny; } prof;
t3_geometry *arena_sweep(const prof *pr, int npr, float tile_u, float tile_v, int keep, bool goal_holes);

/* dev aids from the environment (main.c reads them once, at init) */
typedef struct pug_dev {
  const char *tt;      /* PUG_TT: a turntable of one pug on parade (front34, side, face, head, profile, back) */
  int tt_pug;          /* PUG_TT_PUG: which one (2) */
  bool cam_pug, cam_crowd, cam_cat; /* PUG_CAM_PUG / _CROWD / _CAT: fixed close-up cameras */
  bool no_replay;      /* PUG_NO_REPLAY: skip the goal replays */
  const char *record;  /* PUG_RECORD: log every tick's input to this file */
} pug_dev;
extern pug_dev DEV;

/* world.c: the arena shell, pitch, goals, pads, pugs and the cat */
void world_build(cw_node scene, const sim *s, int theme, const pug_look looks[MAXP]);
void world_reset(void);
void world_tick(const sim *s);      /* fixed tick: animation state */
void world_showcase(int ticks, const pug_look looks[MAXP], int focus); /* title / select: pugs on parade */
t3_vec3 world_pug_pos(int i);
t3_vec3 world_pug_render_pos(int i); /* as drawn this frame (ragdolls included) */
/* split screen, before each view draws: the pugs and the cat far from its
 * camera (eye) as their far copies; full = every one as itself */
void world_view_detail(t3_vec3 eye, bool full);

/* net.c: the goals' cloth nets (render only, their own world) */
void net_build(cw_node root, const sim *s);
void net_reset(void);
void net_tick(const sim *s);        /* fixed tick, from world_tick */
void net_time_scale(float k);       /* the replay's slow motion: the nets' step is this share of a tick */
void net_frame(float alpha);        /* every frame: the moving nets blended between their last two steps */


#endif
