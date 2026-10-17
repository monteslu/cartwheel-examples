/* derby's 3D side: everything here only reads the simulation (game.h) and
 * draws it, on the fixed tick, so every host draws the same frame. */
#ifndef DERBY_WORLD_H
#define DERBY_WORLD_H

#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

/* arena moods (assets/scenes/arena*.cwscene) */
enum { TH_DUSK, TH_NIGHT, TH_KINDS };

/* textures.c: procedural PBR sets (albedo sRGB, normal, roughness, ao), made
 * on first use, deterministic integer hashing (debug field tex.hash) */
enum {
  TEX_DIRT, TEX_CONCRETE, TEX_RUBBER, TEX_PAINT, TEX_RUST, TEX_STEEL, TEX_WOOD, TEX_GRAVEL, TEX_GRASS, TEX_CHAIN, TEX_GLASS, TEX_PRIMER,
  TEX_KINDS
};
typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
const tex_set *tex_get(int kind);
bool tex_warm(int kind); /* a band of the set per call; true once it is made */
void tex_apply(t3_material *m, int kind, float repeat, float normal_scale, float ao_intensity);
void tex_apply_uv(t3_material *m, int kind, float ru, float rv, float normal_scale, float ao_intensity);
void tex_free_all(void);
t3_texture *tex_number(int n, const float paint[3]); /* a door roundel with a race number (shared, cached) */
extern uint32_t tex_hash;
t3_color lin(float r, float g, float b);           /* screen colour -> linear */
t3_geometry *geo_rounded_box(float w, float h, float d, float r, int seg);
t3_geometry *geo_beam(float r_lamp, float r_far, float len); /* models.c: a fading light cone */

/* cars.c: the cars. Each body is a deformable mesh (dents push its
 * vertices in; paint gives way to bare, rusty metal round them), in a full
 * and a light level of detail */
#define CAR_COLOURS 8
extern const float CAR_RGB[CAR_COLOURS][3];
typedef struct car_shell {
  t3_mesh *mesh;
  t3_geometry *geo;
  float *rest, *paint;      /* undented positions; colour per vertex (rgb) */
  int nv;
  int *adj_start, *adj_tri; /* each vertex's triangles (in index order), for its normal */
  int *touched;             /* scratch: the vertices a dent moved or re-lit */
  uint8_t *mark;
} car_shell;
typedef struct car_model {
  t3_object *root;          /* placed by world.c (the sim's pose) */
  t3_object *body;          /* the body's levels of detail, bonnet, numbers */
  t3_object *bodies[3];     /* the body at each level of detail */
  car_shell shell[3][3];    /* [lod][paint, primer, rust] */
  car_shell glass[3], trim, lid, numbers, screen; /* dented too */
  t3_object *hood;          /* the lid's hinge */
  t3_object *lamps;         /* head and tail lamps (levels 0 and 1) */
  t3_mesh *details;         /* level 0's black details (grille, bezels, parking lamps, exhaust) */
  bool beams_on, trimmed;   /* the headlamp beams lit; car_set_trimmed */
  t3_vec3 lid_origin;
  t3_mesh *cabin;
  t3_object *wheel[4], *wheel_lod[4][3], *bumper[2], *beams;
  t3_material *lamp_mat, *tail_mat;
  int kind, colour, number, lod;
  /* the dents each level of detail shows (3: the pieces every level shares);
   * a level not drawn catches up when it is shown */
  dent cur[MAX_DENTS], seen[4][MAX_DENTS];
  int ncur, nseen[4];
  float bumper_a[2], bumper_v[2], hood_a, hood_v;
} car_model;
void car_build(car_model *m, int kind, int colour, int number);
void car_free(car_model *m);
void car_apply_dents(car_model *m, const car *c);   /* when the sim's dents change */
void car_pose(car_model *m, const car *c, int ticks); /* fixed tick: wheels, lean, bumpers, bonnet, lamps */
void car_set_lod(car_model *m, int lod);            /* 0 full, 1 light, 2 silhouette */
void car_set_trimmed(car_model *m, bool trimmed);   /* level 0 without the parts a small view cannot show */
float car_wheel_yaw(const car *c, int w);           /* the front wheels' drawn angle (+: toward +x, the car's left) */
t3_texture *tex_crack(void);
t3_texture *tex_crack_clean(void); /* side-window grime, sampled like the crack */
bool tex_crack_warm(void); /* a band of it per call; true once made */                        /* textures.c: a cracked windscreen */

/* fx.c: everything that flies */
void fx_init(cw_node scene, int theme);
void fx_reset(void);
void fx_events(const sim *s);       /* this tick's sim events -> effects */
void fx_tick(const sim *s);         /* fixed tick */
float fx_shake(void);               /* camera shake amount */
void fx_volumetric(bool on);        /* the haze volumes (volumetric fog on) */

/* scenery.c: the arena's surroundings: fence, stands and crowd, floodlights,
 * crushed-car stacks, the lot */
void scenery_build(cw_node scene, int theme, uint32_t seed);
void scenery_tick(const sim *s, int ticks, int cheer);
void scenery_beams(const t3_vec3 *eyes, int n); /* floodlight beams only from a distance */
void scenery_volumetric(bool on); /* volumetric fog draws the beams: no cones */
void scenery_reset(void);
void scenery_light_crowd(bool light); /* lighter figures for the crowd (splitscreen) */
void scenery_density(float k); /* share of the crowd drawn, 0..1 (cw_quality_density) */
extern uint32_t scenery_hash;

/* world.c: the floor, the wall, the cars, the skid marks */
typedef struct car_look { int colour, number; } car_look;
void world_build(cw_node scene, const sim *s, int theme, const car_look looks[MAXC]);
/* build car i's model ahead (the warm-up, a car a frame); world_build takes it if it still matches */
void world_prebuild_car(int i, int kind, const car_look *look);
void world_reset(void);
void world_tick(const sim *s);      /* fixed tick */
/* each car's detail from a camera whose view is k of the screen's height;
 * a split view (k < 1) draws every car but the one it follows (own: -1 none)
 * at level 1 or lighter beyond 12k metres */
void world_lod(t3_vec3 eye, float k, int own);
t3_vec3 world_car_pos(int i);

/* a little mesh builder shared by the 3D files */
typedef struct mesh_build {
  float *pos, *nor, *uv, *col;
  uint32_t *idx;
  int nv, ni, cap_v, cap_i;
  bool colors;
} mesh_build;
int mb_vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v);
void mb_color(mesh_build *b, int v, float r, float g, float bl);
void mb_tri(mesh_build *b, int a, int c, int d);
void mb_quad(mesh_build *b, int a, int c, int d, int e);
t3_geometry *mb_finish(mesh_build *b);
void mb_append(mesh_build *b, t3_geometry *g, const t3_mat4 *m); /* a static part placed by m; takes g */
void mb_merge(mesh_build *a, mesh_build *b); /* b's vertices and triangles onto a's; frees b */

#endif
