/* racer's world: the road swept from the track, the scenery a map adds, the
 * karts. Placed from the simulation on the fixed tick; nothing here feeds
 * back into the rules. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

t3_color lin(float r, float g, float b) {
  /* screen (sRGB) to linear */
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : powf((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

cw_node hang(cw_node parent, const char *name, t3_object *obj) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, obj);
  cw_node3d_set_object(n, wrap);
  t3_release(wrap);
  return n;
}

t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

void add_mesh(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool cast, bool recv) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = recv;
  hang(parent, name, &me->base);
  t3_release(me);
  t3_release(g);
  t3_release(m);
}

static struct {
  cw_node root;
  track_geo geo;
  bool built;
  const map_def *map;
} W;

const track_geo *world_geo(void) { return &W.geo; }

void world_reset(void) {
  kartgfx_reset();
  if (W.built) track_geo_free(&W.geo);
  memset(&W, 0, sizeof W);
}

static t3_material *road_material(const map_look *L, int surf) {
  t3_color c = lin(L->road[0], L->road[1], L->road[2]);
  float rough = L->road_rough;
  float metal = 0;
  switch (surf) {
  case SURF_DIRT: c = lin(0.55f, 0.38f, 0.22f), rough = 0.95f; break;
  case SURF_ICE: c = lin(0.72f, 0.86f, 0.98f), rough = 0.08f; break;
  case SURF_METAL: c = lin(0.45f, 0.47f, 0.5f), rough = 0.4f, metal = 0.9f; break;
  case SURF_GLASS: c = lin(0.6f, 0.8f, 0.9f), rough = 0.05f; break;
  case SURF_WOOD: c = lin(0.5f, 0.34f, 0.18f), rough = 0.8f; break;
  default: break;
  }
  return phys(c, rough, metal);
}

void world_build(cw_node scene, const sim *s, const map_def *map) {
  world_reset();
  W.map = map;
  W.root = cw_node_new("Node3D", "World");
  cw_node_add_child(scene, W.root);
  track_geo_build(&W.geo, s->track, &map->mesh);
  W.built = true;
  const map_look *L = &map->look;
  for (int i = 0; i < W.geo.n; i++) {
    track_chunk *c = &W.geo.c[i];
    t3_material *m = NULL;
    bool cast = false;
    switch (c->kind) {
    case TM_ROAD: m = road_material(L, c->surf), cast = true; break;
    case TM_KERB: m = phys(lin(L->kerb_a[0], L->kerb_a[1], L->kerb_a[2]), 0.6f, 0); break;
    case TM_WALL: m = phys(lin(L->wall[0], L->wall[1], L->wall[2]), 0.7f, 0), cast = true; break;
    case TM_WALLTOP: m = phys(lin(L->wall_top[0], L->wall_top[1], L->wall_top[2]), 0.6f, 0), cast = true; break;
    case TM_TUNNEL: m = phys(lin(L->wall[0] * 0.8f, L->wall[1] * 0.8f, L->wall[2] * 0.8f), 0.8f, 0), cast = true; break;
    case TM_PLATE:
      m = phys(lin(0.7f, 0.9f, 1), 0.04f, 0);
      m->transparent = true;
      m->opacity = 0.18f;
      m->depth_write = false;
      break;
    case TM_BOOST:
      m = phys(lin(0.1f, 0.3f, 0.5f), 0.4f, 0);
      m->emissive = lin(0.2f, 0.8f, 1.0f);
      m->emissive_intensity = 2.5f;
      break;
    case TM_UNDER: m = phys(lin(L->wall[0] * 0.6f, L->wall[1] * 0.6f, L->wall[2] * 0.6f), 0.9f, 0), cast = true; break;
    }
    if (!m) continue;
    if (c->kind == TM_TUNNEL || c->kind == TM_PLATE || c->kind == TM_UNDER) m->side = T3_DOUBLE_SIDE;
    t3_mesh *me = t3_mesh_new(c->geo, m);
    me->base.cast_shadow = cast;
    me->base.receive_shadow = true;
    char name[24];
    snprintf(name, sizeof name, "T%d_%d", c->kind, i);
    hang(W.root, name, &me->base);
    t3_release(me);
    t3_release(m);
  }
  kartgfx_build(W.root, s);
  if (map->scenery) map->scenery(scene, s->track, 1234);
}

void world_tick(const sim *s) { kartgfx_tick(s); }
