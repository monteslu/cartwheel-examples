/* racer's realistic land: a map's ground as the engine's Terrain3D, its heights from the map's own height function,
 * painted from eight photo layers (assets/terrain/<map>/, tools/terrain-layers.sh) by the map's paint function, and
 * covered with scatter layers of real trees, grass and rocks (assets/models/, tools/foliage-art.mjs and
 * tools/rock-decimate.py).
 *
 *     land_def d = land_defaults();
 *     d.x0 = -2432, d.z0 = -2432, d.nx = d.nz = 1216, d.spacing = 4;    // the grid (quads: multiples of 32)
 *     d.height = my_height, d.paint = my_paint, d.dir = "terrain/salt";
 *     cw_node land = land_build(parent, &d);
 *     land_scatter(land, &(land_layer){ .name = "Juniper", .mesh = "models/trees/juniper.glb", .density = 0.002f, ... });
 *
 * The node builds its terrain as it enters the tree, so land_build sets every property first and the heights and paint
 * after (a property set later would rebuild it flat). Physics is off: the road is its own mesh, the land only has to
 * look right and sit under it. */
#ifndef RACER_LAND_H
#define RACER_LAND_H

#include <stdbool.h>
#include <stdint.h>
#include "cartwheel/scene.h"

/* the paint of one sample: base and overlay layer (0..7) and the overlay's share (0..255) */
typedef struct land_paint {
  int base, over, blend;
} land_paint;

typedef struct land_def {
  float x0, z0;         /* world position of sample (0, 0) */
  int nx, nz;           /* quads (multiples of 32) */
  float spacing;        /* metres between samples */
  float (*height)(float x, float z, void *ud);
  /* what a sample is painted with, from where it is, its height and its slope (rise over run) */
  land_paint (*paint)(float x, float y, float z, float slope, void *ud);
  void *ud;
  const char *dir;      /* "terrain/<map>": the layers' <n>_albedo.png and <n>_normal.png */
  const char *names[8]; /* the eight layers' file stems ("0_crust", ...) */
  float uv[8];          /* each layer's repeats per metre */
  int quality;          /* terrain shading 0..3, -1 the graphics settings */
  float far;            /* metres past which only the macro colour is drawn */
} land_def;
land_def land_defaults(void);

/* The Terrain3D node, added under parent and filled; an invalid node (logged) if the engine made no terrain. */
cw_node land_build(cw_node parent, const land_def *d);

typedef struct land_layer {
  const char *name, *mesh; /* node name; "models/....glb" */
  float density;           /* per square metre */
  uint32_t layers;         /* paint layers it grows on (bit i = layer i) */
  float max_slope;         /* degrees */
  float scale_min, scale_max;
  bool align;              /* tilt with the ground (grass, rocks) */
  float sink;              /* metres into the ground (x scale) */
  float range, fade, lod;  /* visible range, the fade over its end, metres between mesh LODs */
  bool shadow;
  bool grass;              /* thinned by the grass knob, else the scatter knob */
  int seed;
} land_layer;
/* A scatter layer on the land (density 0 when RACER_NOSCATTER is "all" or names it: what each layer costs). */
cw_node land_scatter(cw_node land, const land_layer *l);

/* debug fields <prefix>.patches, .scatter, .scatter_cells, .scatter_draws, updated by land_frame */
void land_stats_fields(cw_node land, const char *prefix);
void land_frame(void);

#endif
