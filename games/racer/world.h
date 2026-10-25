/* racer's 3D side: everything here only reads the simulation (game.h) and
 * draws it, on the fixed tick, so every host draws the same frame. */
#ifndef RACER_WORLD_H
#define RACER_WORLD_H

#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

t3_color lin(float r, float g, float b); /* screen colour -> linear */
t3_material *phys(t3_color c, float rough, float metal);
cw_node hang(cw_node parent, const char *name, t3_object *obj);
void add_mesh(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool cast, bool recv);

/* models.c: the karts */
typedef struct kart_model {
  t3_object *root;
  t3_object *wheel[4];   /* pivots at the wheel centres: steer about y, spin about x (set by world.c) */
  t3_object *body;       /* the shell: rolls and pitches a little on its own */
  t3_object *shield, *flame[2];
  t3_material *paint;
  int veh, colour;
} kart_model;
extern const float KART_RGB[8][3];
void kart_build(kart_model *m, int veh, int colour);

/* world.c */
void world_build(cw_node scene, const sim *s, const map_def *map);
void world_reset(void);
void world_tick(const sim *s);
const track_geo *world_geo(void);
cw_node world_kart_node(int i);

#endif
