/* racer's realistic land (land.h): a map's Terrain3D, its paint and its scatter layers. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/base.h"
#include "cartwheel/debug.h"
#include "cartwheel/quality.h"
#include "cartwheel/terrain.h"
#include "land.h"

land_def land_defaults(void) {
  land_def d = { 0 };
  d.spacing = 2;
  d.quality = -1;
  d.far = 600;
  for (int i = 0; i < 8; i++) d.uv[i] = 0.15f;
  return d;
}

cw_node land_build(cw_node parent, const land_def *d) {
  const int sx = d->nx + 1, sz = d->nz + 1;
  size_t n = (size_t)sx * (size_t)sz;
  float *h = malloc(sizeof(float) * n);
  float lo = 1e9f, hi = -1e9f;
  for (int j = 0; j < sz; j++)
    for (int i = 0; i < sx; i++) {
      float y = d->height(d->x0 + (float)i * d->spacing, d->z0 + (float)j * d->spacing, d->ud);
      h[(size_t)j * sx + i] = y;
      lo = fminf(lo, y), hi = fmaxf(hi, y);
    }
  lo -= 1.0f, hi += 1.0f;
  cw_node node = cw_node_new("Terrain3D", "Land");
  cw_node_set(node, "size_x", cw_val_int(d->nx));
  cw_node_set(node, "size_z", cw_val_int(d->nz));
  cw_node_set(node, "vertex_spacing", cw_val_float(d->spacing));
  cw_node_set(node, "region_size", cw_val_int(64));
  cw_node_set(node, "min_height", cw_val_float(lo));
  cw_node_set(node, "max_height", cw_val_float(hi));
  /* Low: the macro colour from 40 m (0.5-1 ms of the 890M's scene pass, no change seen from the race camera) */
  cw_node_set(node, "far_distance", cw_val_float(cw_quality_get() == CW_QUALITY_LOW ? 40.0f : d->far));
  cw_node_set(node, "physics", cw_val_bool(false));
  cw_node_set(node, "quality", cw_val_int(d->quality));
  for (int i = 0; i < 8; i++) {
    if (!d->names[i]) continue;
    char key[32], path[96];
    snprintf(key, sizeof key, "layer%d_albedo", i), snprintf(path, sizeof path, "%s/%s_albedo.png", d->dir, d->names[i]);
    cw_node_set(node, key, cw_val_string(path));
    snprintf(key, sizeof key, "layer%d_normal", i), snprintf(path, sizeof path, "%s/%s_normal.png", d->dir, d->names[i]);
    cw_node_set(node, key, cw_val_string(path));
    snprintf(key, sizeof key, "layer%d_uv_scale", i);
    cw_node_set(node, key, cw_val_float(d->uv[i]));
  }
  /* the node is centred on its position */
  cw_node3d_set_position(node, (t3_vec3){ d->x0 + (float)d->nx * d->spacing * 0.5f, 0, d->z0 + (float)d->nz * d->spacing * 0.5f });
  cw_node_add_child(parent, node);
  cw_terrain *t = cw_terrain_of(node);
  if (!t) {
    cw_error("land: the Terrain3D node made no terrain");
    free(h);
    return node;
  }
  cw_terrain_set_heights_m(t, 0, 0, sx, sz, h);
  uint32_t *ctl = malloc(sizeof(uint32_t) * n);
  for (int j = 0; j < sz; j++)
    for (int i = 0; i < sx; i++) {
      size_t o = (size_t)j * sx + i;
      int i0 = i > 0 ? i - 1 : i, i1 = i < sx - 1 ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < sz - 1 ? j + 1 : j;
      float gx = (h[(size_t)j * sx + i1] - h[(size_t)j * sx + i0]) / ((float)(i1 - i0) * d->spacing);
      float gz = (h[(size_t)j1 * sx + i] - h[(size_t)j0 * sx + i]) / ((float)(j1 - j0) * d->spacing);
      land_paint p = d->paint(d->x0 + (float)i * d->spacing, h[o], d->z0 + (float)j * d->spacing, sqrtf(gx * gx + gz * gz), d->ud);
      ctl[o] = cw_terrain_control(p.base, p.over, CW_CLAMP(p.blend, 0, 255), 0, 0, false);
    }
  cw_terrain_set_control(t, 0, 0, sx, sz, ctl);
  cw_terrain_scatter_invalidate(t);
  free(ctl), free(h);
  cw_info("land: %d x %d samples, %.0f m apart, %.1f .. %.1f m", sx, sz, d->spacing, lo, hi);
  return node;
}

cw_node land_scatter(cw_node land, const land_layer *l) {
  cw_node s = cw_node_new("TerrainScatter3D", l->name);
  const char *skip = getenv("RACER_NOSCATTER");
  bool off = skip && (!strcmp(skip, "all") || strstr(skip, l->name));
  cw_node_set(s, "mesh", cw_val_string(l->mesh));
  cw_node_set(s, "density", cw_val_float(off ? 0.0f : l->density));
  cw_node_set(s, "layers", cw_val_int((int)l->layers));
  cw_node_set(s, "min_slope", cw_val_float(l->max_slope));
  cw_node_set(s, "scale_min", cw_val_float(l->scale_min));
  cw_node_set(s, "scale_max", cw_val_float(l->scale_max));
  cw_node_set(s, "align", cw_val_bool(l->align));
  cw_node_set(s, "sink", cw_val_float(l->sink));
  cw_node_set(s, "visible_range", cw_val_float(l->range));
  cw_node_set(s, "fade", cw_val_float(l->fade));
  cw_node_set(s, "lod_distance", cw_val_float(l->lod));
  cw_node_set(s, "cast_shadow", cw_val_bool(l->shadow));
  cw_node_set(s, "knob", cw_val_enum(l->grass ? "GRASS" : "SCATTER"));
  cw_node_set(s, "seed", cw_val_int(l->seed));
  cw_node_add_child(land, s);
  return s;
}

static cw_node STAT_NODE;
static int STAT[4];
void land_stats_fields(cw_node land, const char *prefix) {
  STAT_NODE = land;
  static bool fields;
  if (fields) return;
  fields = true;
  static char names[4][48];
  static const char *const SUF[4] = { "patches", "scatter", "scatter_cells", "scatter_draws" };
  for (int i = 0; i < 4; i++) {
    snprintf(names[i], sizeof names[i], "%s.%s", prefix, SUF[i]);
    cw_debug_i32(names[i], &STAT[i]);
  }
}
void land_frame(void) {
  cw_terrain *t = cw_node_valid(STAT_NODE) ? cw_terrain_of(STAT_NODE) : NULL;
  if (!t) return;
  cw_terrain_stats st;
  cw_terrain_get_stats(t, &st);
  STAT[0] = st.patches + st.patches_far, STAT[1] = st.scatter_instances, STAT[2] = st.scatter_cells, STAT[3] = st.scatter_draws;
}
