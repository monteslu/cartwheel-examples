/* racer's world: the road swept from the track, the scenery a map adds, the
 * karts. Placed from the simulation on the fixed tick; nothing here feeds
 * back into the rules. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "textures.h"
#include "props.h"
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

static float fractf_(float x) { return x - floorf(x); }

static struct {
  cw_node root;
  track_geo geo;
  bool built;
  const map_def *map;
  float frame_time;
  t3_vec3 eye[4];
  int neye;
  t3_material *boost;   /* one material for every boost strip: its arrows run forward and pulse */
  cw_node scene;
  const sim *sim;
  bool checked;         /* RACER_GEOMCHECK ran */
  bool cleaned;         /* geomclean ran: nothing stands in the lanes */
  int clean_frames;
} W;

const track_geo *world_geo(void) { return &W.geo; }
bool world_ready(void) { return !W.sim || !W.map || W.map->arena || W.cleaned || getenv("RACER_NOCLEAN"); }

void world_reset(void) {
  props_reset();
  kartgfx_reset();
  if (W.built) track_geo_free(&W.geo);
  if (W.boost) t3_release(W.boost);
  memset(&W, 0, sizeof W);
}

/* A look's colour is a multiplier on the texture ((1, 1, 1) keeps the texture's own colours). A dark flat-colour
 * value (every component well under 0.7) is lifted so its hue shows and the texture does not go black. */
static t3_color tint(const float c[3], float min_peak) {
  float peak = fmaxf(c[0], fmaxf(c[1], c[2])), k = peak > 0.0001f && peak < min_peak ? min_peak / peak : 1.0f;
  return lin(fminf(c[0] * k, 1.0f), fminf(c[1] * k, 1.0f), fminf(c[2] * k, 1.0f));
}

/* RACER_LOOK=<n> (a development switch): the road shows the n-th road texture (n % 12 of the TEX_ROAD_* kinds)
 * and the walls the n-th wall texture (n % 8 of the TEX_WALL_* kinds), whatever the map asked for, so every
 * texture can be looked at on the same course. Unset or negative: the map's own look. */
static int look_override(void) {
  const char *e = getenv("RACER_LOOK");
  return e && *e ? atoi(e) : -1;
}

static t3_material *road_material(const map_look *L, int surf, int road_tex) {
  int kind = road_tex;
  float rough_k = 1.0f;
  switch (surf) {
  case SURF_DIRT: kind = TEX_ROAD_DIRT; break;
  case SURF_ICE: kind = TEX_ROAD_ICE; break;
  case SURF_METAL: kind = TEX_ROAD_METAL; break;
  case SURF_GLASS: kind = TEX_ROAD_GLASS; break;
  case SURF_WOOD: kind = TEX_ROAD_WOOD; break;
  default: break;
  }
  /* the surfaces with a look of their own keep their own colours; only the map's road surface is tinted */
  t3_material *m = phys(surf == SURF_ROAD ? tint(L->road, 0.85f) : lin(1, 1, 1), 1.0f, kind == TEX_ROAD_METAL ? 0.8f : 0.0f);
  tex_apply(m, kind, 1, 1, 1.0f, 1.0f);
  /* road_rough scales the texture's roughness map (1 = as made; a map that wants it glossier lowers it) */
  if (surf == SURF_ROAD) rough_k = L->road_rough > 0 ? L->road_rough / 0.75f : 1.0f;
  m->roughness = fminf(rough_k, 1.0f);
  if (kind == TEX_ROAD_GLASS) {
    m->transparent = true;
    m->opacity = 0.55f;
    m->depth_write = false;
  }
  return m;
}

static void dev_walk(cw_node n, int depth, const char *hide) {
  for (int i = 0; i < cw_node_child_count(n); i++) {
    cw_node c = cw_node_child(n, i);
    const char *nm = cw_node_name(c);
    if (getenv("RACER_LIST") && depth < 3) cw_log(CW_LOG_INFO, "node %*s%s children=%d", depth * 2, "", nm, cw_node_child_count(c));
    if (hide && nm && strstr(nm, hide)) {
      cw_node_set(c, "visible", cw_val_bool(false));
      continue;
    }
    dev_walk(c, depth + 1, hide);
  }
}

void world_build(cw_node scene, const sim *s, const map_def *map) {
  world_reset();
  W.map = map;
  W.scene = scene, W.sim = s, W.checked = false, W.cleaned = false, W.clean_frames = 0;
  W.root = cw_node_new("Node3D", "World");
  cw_node_add_child(scene, W.root);
  /* a battle arena's track is only the sim's frame under the floor: nothing of it is drawn (arena.c draws the arena) */
  if (!map->arena) track_geo_build(&W.geo, s->track, &map->mesh);
  W.built = true;
  const map_look *L = &map->look;
  int road_tex = L->road_tex, wall_tex = L->wall_tex, lk = look_override();
  if (lk >= 0) road_tex = TEX_ROAD_ASPHALT + lk % 12, wall_tex = TEX_WALL_CONCRETE + lk % 8;
  bool wall_glow = L->wall_emissive;
  for (int i = 0; i < W.geo.n; i++) {
    track_chunk *c = &W.geo.c[i];
    t3_material *m = NULL;
    bool cast = false;
    switch (c->kind) {
    case TM_ROAD: m = road_material(L, c->surf, road_tex), cast = true; break;
    case TM_KERB:
      m = phys(lin(1, 1, 1), 0.9f, 0);
      tex_apply_kerb(m, L->kerb_a, L->kerb_b, 1, 1, 1.0f, 1.0f);
      break;
    case TM_WALL:
      m = phys(tint(L->wall, 0.85f), 1.0f, wall_tex == TEX_WALL_RUST || wall_tex == TEX_WALL_TECH ? 0.5f : 0.0f);
      tex_apply(m, wall_tex, 1, 1, 1.0f, 1.0f);
      if (m->emissive_map) {
        /* lit strips: the map's glow colour, or off */
        if (wall_glow) m->emissive = lin(L->glow[0], L->glow[1], L->glow[2]), m->emissive_intensity = 2.2f;
        else m->emissive_intensity = 0;
      }
      cast = true;
      break;
    case TM_WALLTOP:
      m = phys(tint(L->wall_top, 0.85f), 1.0f, 0);
      tex_apply(m, wall_tex == TEX_WALL_TECH || wall_tex == TEX_WALL_RUST ? TEX_METAL : wall_tex, 1, 1, 1.0f, 1.0f);
      m->emissive_intensity = 0;
      cast = true;
      break;
    case TM_TUNNEL:
      m = phys(lin(0.85f + 0.15f * L->wall[0], 0.85f + 0.15f * L->wall[1], 0.85f + 0.15f * L->wall[2]), 1.0f, 0);
      tex_apply(m, TEX_TUNNEL, 1, 1, 1.0f, 1.0f);
      m->emissive = lin(1.0f, 0.93f, 0.75f);
      m->emissive_intensity = 2.4f;
      cast = true;
      break;
    case TM_PLATE:
      /* glass: clear, glossy, the frames and dots catch a little more light */
      m = phys(lin(0.8f, 0.95f, 1), 1.0f, 0);
      tex_apply(m, TEX_ROAD_GLASS, 0.5f, 0.5f, 0.6f, 1.0f);
      m->transparent = true;
      m->opacity = 0.28f;
      m->depth_write = false;
      m->specular_intensity = 1.0f;
      break;
    case TM_BOOST:
      if (!W.boost) {
        W.boost = phys(lin(1, 1, 1), 1.0f, 0);
        tex_apply(W.boost, TEX_ARROWS, 1, 1, 1.0f, 1.0f);
        W.boost->emissive_intensity = 2.6f;
      }
      m = W.boost;
      t3_retain(m);
      break;
    case TM_UNDER:
      /* the belly of the ramp: dark riveted plate, tinted toward the wall colour */
      m = phys(lin(0.35f + 0.3f * L->wall[0], 0.35f + 0.3f * L->wall[1], 0.35f + 0.3f * L->wall[2]), 1.0f, 0.4f);
      tex_apply(m, TEX_METAL, 4, 2, 1.0f, 1.0f);
      cast = true;
      break;
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
  ground_forget(); /* the last map's ground must not stand in for this one's */
  if (map->scenery) map->scenery(scene, s->track, 1234);
  {
    /* dev aid: RACER_LIST=1 logs the scene's nodes (to a depth of three) with their child counts; RACER_HIDE=text hides the nodes whose name contains it (draw-call attribution) */
    const char *hide = getenv("RACER_HIDE");
    if (hide || getenv("RACER_LIST")) dev_walk(scene, 0, hide);
  }
}

void world_tick(const sim *s) { kartgfx_tick(s); }

void world_set_eyes(const t3_vec3 *e, int n) {
  W.neye = n > 4 ? 4 : n;
  for (int i = 0; i < W.neye; i++) W.eye[i] = e[i];
  kartgfx_set_eyes(e, n);
}

void world_frame(double alpha) {
  if (W.map && W.map->scenery_frame) W.map->scenery_frame((float)alpha, (double)W.frame_time);
  W.frame_time += 1.0f / 60.0f;
  /* the first frame of a map: scenery standing in the lanes is taken out (geomcheck.c); RACER_NOCLEAN=1 leaves it */
  if (!W.cleaned && W.sim && !W.map->arena && !getenv("RACER_NOCLEAN")) {
    t3_object *ro = cw_node3d_object(W.scene);
    /* a snapshot on the first frame, the clean on the fourth: what moved in between is left alone */
    if (ro && W.clean_frames == 0) geomclean_snapshot(ro);
    if (ro && ++W.clean_frames == 4) geomclean_begin(W.scene, ro, W.sim->track);
    /* behind the loading screen's black (world_ready holds the fade and the race), so it takes big bites */
    if (ro && W.clean_frames >= 4 && geomclean_step(400000)) W.cleaned = true, cw_info("geomclean: done on frame %d", W.clean_frames);
    if (ro && W.clean_frames >= 4) W.clean_frames++;
  }
  /* dev aid: RACER_GROUNDQ=x,z logs the ground's height there once */
  static int gq = 0;
  if (!gq && getenv("RACER_GROUNDQ") && W.sim) {
    float qx, qz;
    gq = 1;
    if (sscanf(getenv("RACER_GROUNDQ"), "%f,%f", &qx, &qz) == 2)
      for (int d = -12; d <= 12; d += 3) cw_info("groundq %.0f,%.0f: %.2f", qx + (float)d, qz, ground_height_cb(qx + (float)d, qz, NULL));
    for (int i = 0; i < W.sim->track->n; i++) {
      const tsample *a = &W.sim->track->s[i];
      if (fabsf(a->p.z - qz) < 1.0f && fabsf(a->p.x - qx) < 20) cw_info("groundq sample %d s %.0f p %.1f,%.1f,%.1f hw %.1f r %.2f,%.2f,%.2f flags 0x%x", i, a->s, a->p.x, a->p.y, a->p.z, a->hw, a->r.x, a->r.y, a->r.z, (unsigned)a->flags);
    }
  }
  static int gvd = 0;
  if (!gvd && getenv("RACER_GROUNDV") && W.frame_time > 0.4f) {
    gvd = 1;
    float qx, qz;
    if (sscanf(getenv("RACER_GROUNDV"), "%f,%f", &qx, &qz) == 2) groundv_run(W.scene, cw_node3d_object(W.scene), qx, qz);
  }
  /* dev aid: RACER_GEOMCHECK=1, once the map has stood a moment, lists geometry standing in the lanes (geomcheck.c) */
  if (!W.checked && W.frame_time > 0.5f && W.sim && !W.map->arena && getenv("RACER_GEOMCHECK")) {
    W.checked = true;
    t3_object *ro = cw_node3d_object(W.scene);
    if (ro) geomcheck_run(W.scene, ro, W.sim->track);
  }
  kartgfx_frame(NULL, (float)alpha); /* models agent: the karts and the items posed between ticks */
  items_frame(NULL, (float)alpha);
  if (W.boost) {
    /* the chevrons run forward (toward +v) about 14 m/s, 6 m per repeat, and the pad breathes */
    tex_scroll(TEX_ARROWS, 0, -fractf_(W.frame_time * 14.0f / 6.0f));
    W.boost->emissive_intensity = 2.5f + 0.7f * sinf(W.frame_time * 9.0f);
  }
}
