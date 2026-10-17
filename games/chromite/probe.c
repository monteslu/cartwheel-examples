/* chromite reflection probe: what the chrome craft mirror, kept live.
 *
 * The engine's ReflectionProbe3D (cartwheel/gi.h) does the capturing: every
 * other frame it renders the scene around the middle of the play plane into
 * a 128 cube-UV radiance map (three.js r186's PMREMGenerator.fromScene), and
 * every Standard and Physical surface inside its box reflects that, and
 * takes its diffuse ambient from it, in place of the fixed environment.
 *
 * What it captures is a stand-in on a layer of its own (the view camera
 * never draws it; the probe's cull_mask sees only it): the sky (the horizon
 * environment on a dome), a wide plate of the deck's plating far below that
 * scrolls with the real deck, and a glow on that plate under every
 * explosion flash. So the hulls carry the deck sliding by underneath and
 * light up orange when something blows up beside them, for six 128 x 128
 * renders of a handful of draws a refresh rather than the whole fortress.
 *
 * The box is the slab the craft fly in. On the title it shrinks to the ship
 * idling under the menu, so the chrome wordmark keeps its own studio light.
 * The deck, far below the slab, keeps the fixed environment.
 *
 * Quality: the engine halves a probe's capture size on Low and Medium and
 * refreshes it less often; Chromite's chrome is the point of the game, so
 * the probe asks for twice the size there (128 again) and is refreshed on
 * demand every other frame at every preset. Ultra captures at 256.
 * Render-only: nothing here reaches the simulation. */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/gi.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/quality.h"
#include "game.h"

#define GLOWS 8
#define PROBE_SIZE 128
#define DECK_TILE 12.0f
#define STAND_IN_LAYER (1u << 20) /* the stand-in's three.c layer: no camera draws it */

static struct {
  bool ready;
  cw_node node;
  t3_object *stand_in;
  t3_mesh *deck;
  t3_material *deck_mat, *glow_mat[GLOWS], *sky_mat;
  t3_texture *deck_map;
  t3_sprite *glow[GLOWS];
  uint64_t frames;
  int resolution, title, on;
} P;

int probe_refreshes;     /* (debug field) captures so far */
int32_t probe_every = 2; /* (debug field) refresh every n frames; 0 = off (the fixed environment) */

/* The capture is the frame's first render of the scene, so it draws the
 * sun's shadow map (the engine draws a frame's shadow maps once, on the
 * first render that uses them) with everything but the stand-in hidden:
 * the deck would lose its shadows on every capture frame. After a capture
 * (the "gi" pass, SHADOW phase order 150) the maps are marked stale again,
 * so the view draws them with the real casters. */
static void shadows_exec(cw_pass_ctx *ctx, void *user) {
  (void)user;
  if (!ctx->renderer) return;
  cw_gi_stats st;
  cw_gi_get_stats(ctx->tree, &st);
  if (st.captures > 0) t3_renderer_shadow_needs_update(ctx->renderer);
}

void probe_init(cw_tree *tree) {
  cw_pipeline *pl = cw_pipeline_of(tree);
  if (!cw_pipeline_has_pass(pl, "chromite_probe_shadows"))
    cw_pipeline_add_pass(pl, &(cw_pass_desc){ .name = "chromite_probe_shadows", .phase = CW_PHASE_SHADOW, .order = 160,
                                              .execute = shadows_exec, .webgpu = true });
}

static void stand_in(t3_object *o) {
  t3_object_set_layers(o, STAND_IN_LAYER);
  t3_object_add(P.stand_in, o);
}

void probe_build(cw_node parent, t3_object *root) {
  probe_free();
  P.stand_in = t3_group_new();
  /* the sky: a dome carrying the horizon's radiance at every vertex (HDR
   * colours: the softboxes and strips run well above 1), seen from inside */
  P.sky_mat = t3_mesh_basic_material_new(0xffffff);
  P.sky_mat->vertex_colors = true;
  P.sky_mat->side = T3_BACK_SIDE;
  P.sky_mat->depth_write = false;
  t3_geometry *sg = t3_sphere_geometry_new(150, 96, 48);
  {
    const t3_attribute *pa = sg->attributes[T3_ATTR_POSITION];
    const float *pv = pa->array;
    float *col = malloc(sizeof(float) * 3 * (size_t)pa->count);
    for (int i = 0; i < pa->count; i++) env_radiance(pv[i * 3], pv[i * 3 + 1], pv[i * 3 + 2], col + i * 3);
    t3_attribute *ca = t3_attribute_new(T3_FLOAT32, col, pa->count, 3);
    free(col);
    t3_geometry_set_attribute(sg, T3_ATTR_COLOR, ca);
    t3_release(ca);
  }
  t3_mesh *sky = t3_mesh_new(sg, P.sky_mat);
  t3_release(sg);
  t3_object_set_position(sky, 0, PLAY_Y, 0);
  T3_OBJ(sky)->frustum_culled = false;
  stand_in(T3_OBJ(sky));
  t3_release(sky);
  /* the deck plate: its own copy of the plating's colour map (the offset
   * scrolls it), unlit and dim */
  P.deck_map = tex_map_copy(MT_DECK, 160 / DECK_TILE, 160 / DECK_TILE);
  P.deck_mat = t3_mesh_basic_material_new(0xffffff);
  P.deck_mat->color = (t3_color){ 0.55f, 0.56f, 0.6f };
  t3_material_set_map(P.deck_mat, P.deck_map);
  t3_geometry *g = t3_plane_geometry_new(160, 160, 1, 1);
  t3_geometry_rotate_x(g, -1.5707963f);
  P.deck = t3_mesh_new(g, P.deck_mat);
  t3_release(g);
  t3_object_set_position(P.deck, 0, GROUND_Y, 0);
  stand_in(T3_OBJ(P.deck));
  t3_release(P.deck);
  /* glows under the explosion flashes */
  for (int i = 0; i < GLOWS; i++) {
    P.glow_mat[i] = t3_sprite_material_new(0xffffff);
    t3_material_set_map(P.glow_mat[i], world_halo_texture());
    P.glow_mat[i]->blending = T3_ADDITIVE_BLENDING;
    P.glow_mat[i]->depth_write = false;
    P.glow[i] = t3_sprite_new(P.glow_mat[i]);
    T3_OBJ(P.glow[i])->visible = false;
    stand_in(T3_OBJ(P.glow[i]));
    t3_release(P.glow[i]);
  }
  t3_object_add(root, P.stand_in);
  t3_release(P.stand_in);
  /* the probe: in the middle of the play plane, captured on demand */
  P.node = cw_node_new("ReflectionProbe3D", "Probe");
  cw_node_add_child(parent, P.node);
  cw_node3d_set_position(P.node, (t3_vec3){ 0, PLAY_Y, 0 });
  cw_node_set(P.node, "update_mode", cw_val_enum("ON_DEMAND"));
  cw_node_set(P.node, "cull_mask", cw_val_int(STAND_IN_LAYER));
  cw_node_set(P.node, "near", cw_val_float(0.5));
  cw_node_set(P.node, "far", cw_val_float(200));
  cw_node_set(P.node, "blend_distance", cw_val_float(1));
  cw_node_set(P.node, "ambient", cw_val_bool(true));
  P.title = -1, P.resolution = 0, P.on = -1;
  P.ready = true;
}

void probe_free(void) {
  /* the nodes and the stand-in go with the world's node and root */
  for (int i = 0; i < GLOWS; i++)
    if (P.glow_mat[i]) t3_release(P.glow_mat[i]);
  if (P.deck_mat) t3_release(P.deck_mat);
  if (P.sky_mat) t3_release(P.sky_mat);
  if (P.deck_map) t3_release(P.deck_map);
  memset(&P, 0, sizeof P);
}

/* follows the deck's scroll, the explosion flashes, the screen (the title or
 * the game) and the quality preset; asks for a capture every probe_every
 * frames */
void probe_sync(float travel, bool title) {
  if (!P.ready) return;
  P.deck_map->offset.y = travel / DECK_TILE;
  t3_vec3 pos[GLOWS];
  t3_color col[GLOWS];
  float e[GLOWS];
  int n = fx_flashes(pos, col, e, GLOWS);
  for (int i = 0; i < GLOWS; i++) {
    bool on = i < n && e[i] > 1;
    T3_OBJ(P.glow[i])->visible = on;
    if (!on) continue;
    float k = e[i] / 60;
    t3_object_set_position(P.glow[i], pos[i].x, PLAY_Y - 1, pos[i].z);
    t3_object_set_scale(P.glow[i], 10, 10, 10);
    P.glow_mat[i]->color = (t3_color){ col[i].r * k, col[i].g * k, col[i].b * k };
  }
  /* the box: the play slab, or on the title the ship under the menu */
  if (P.title != (int)title) {
    P.title = title;
    cw_node_set(P.node, "size", title ? cw_val_vec3(30, 8, 7) : cw_val_vec3(64, 8, 56));
    cw_node3d_set_position(P.node, (t3_vec3){ 0, PLAY_Y, title ? 7.2f : 0 });
    cw_node_set(P.node, "origin_offset", cw_val_vec3(0, 0, title ? -7.2f : 0));
  }
  /* 128 at every preset (the engine halves it on Low and Medium) */
  int res = cw_gfx_get(CW_GFX_EFFECTS) <= 1 ? PROBE_SIZE * 2 : PROBE_SIZE;
  if (res != P.resolution) P.resolution = res, cw_node_set(P.node, "resolution", cw_val_int(res));
  int on = probe_every > 0;
  if (on != P.on) P.on = on, cw_node_set(P.node, "intensity", cw_val_float(on ? 1 : 0));
  if (on && (P.frames++ % (uint64_t)probe_every) == 0) cw_reflection_probe_update(P.node);
  cw_value v;
  if (cw_node_get_prop(P.node, "captures", &v)) probe_refreshes = (int)v.i;
}

