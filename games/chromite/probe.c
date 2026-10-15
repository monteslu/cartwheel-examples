/* chromite reflection probe: what the chrome craft mirror, kept live.
 *
 * A small proxy scene stands in for the world below and around the play
 * plane: the sky (the horizon environment as the background), a wide plate
 * of the deck's plating far below that scrolls with the real deck, and a
 * glow on that plate under every explosion flash. Every other frame a pass
 * in the pipeline's SHADOW phase renders it from the middle of the play
 * plane into a 128 cube-UV probe (three.c's PMREMGenerator.fromScene with
 * r186's options: size, position, and the probe's own target, so nothing is
 * allocated after the first), and the craft materials take that probe as
 * their envMap. So the hulls carry the deck sliding by underneath and light
 * up orange when something blows up beside them.
 *
 * Cost per refresh: six 128 x 128 renders of four or five draws, and the
 * GGX filter's small passes. GL and WebGPU alike. Render-only. */
#include <string.h>

#include "cartwheel/pipeline.h"
#include "cartwheel/gpu.h"
#include "game.h"

#define GLOWS 4
#define PROBE_SIZE 128
#define DECK_TILE 12.0f

static struct {
  bool ready;
  t3_scene *scene;
  t3_mesh *deck;
  t3_material *deck_mat, *glow_mat[GLOWS];
  t3_texture *deck_map;
  t3_sprite *glow[GLOWS];
  t3_pmrem_generator *pmrem;
  t3_render_target *rt;
  int every;          /* refresh every n frames (render-only; 0 = off) */
  uint64_t frames;
  int refreshes;
} P;

int probe_refreshes; /* (debug field) */

static void probe_exec(cw_pass_ctx *ctx, void *user) {
  (void)user;
  if (!P.ready || !ctx->renderer || P.every <= 0) return;
  if (P.rt && (P.frames++ % (uint64_t)P.every) != 0) return;
  t3_renderer *r = ctx->renderer;
  bool wgpu = cw_gpu_backend() == CW_GPU_WEBGPU;
  /* three.c renders the probe on command buffers of its own (WebGPU) or
   * with its own GL state: forget the cached state either way */
  if (wgpu) t3_renderer_set_external_encoder(r, 0);
  else t3_renderer_reset_bindings(r);
  if (!P.pmrem) P.pmrem = t3_pmrem_generator_new(r);
  t3_render_target *next = t3_pmrem_from_scene_ex(P.pmrem, P.scene, 0, 0.5f, 200,
                                                  &(t3_pmrem_scene_options){ .size = PROBE_SIZE, .position = { 0, PLAY_Y, 0 }, .render_target = P.rt });
  if (!wgpu) t3_renderer_reset_bindings(r);
  if (!next) return;
  if (next != P.rt) {
    /* the first refresh (or a new size): every chrome material takes it */
    static const int MATS[] = { M_CHROME, M_LILAC, M_RUST, M_RUST_DARK, M_BLUE, M_TEAL, M_BLACK };
    for (size_t i = 0; i < sizeof MATS / sizeof MATS[0]; i++) {
      t3_material *m = model_mat(MATS[i]);
      if (m) t3_material_set_texture(m, T3_ENV_MAP, next->texture);
    }
  }
  if (P.rt) t3_release(P.rt);
  P.rt = next;
  P.refreshes++;
  probe_refreshes = P.refreshes;
}

void probe_init(cw_tree *tree) {
  cw_pipeline *pl = cw_pipeline_of(tree);
  if (!cw_pipeline_has_pass(pl, "chromite_probe"))
    cw_pipeline_add_pass(pl, &(cw_pass_desc){ .name = "chromite_probe", .phase = CW_PHASE_SHADOW, .order = 50, .writes = CW_RES_SHADOW,
                                              .execute = probe_exec, .webgpu = true });
  P.every = 2;
}

void probe_build(void) {
  probe_free();
  P.scene = t3_scene_new();
  t3_scene_set_background_texture(P.scene, env_texture());
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
  t3_object_add(&P.scene->base, P.deck);
  t3_release(P.deck);
  /* glows under the explosion flashes */
  for (int i = 0; i < GLOWS; i++) {
    P.glow_mat[i] = t3_sprite_material_new(0xffffff);
    t3_material_set_map(P.glow_mat[i], world_halo_texture());
    P.glow_mat[i]->blending = T3_ADDITIVE_BLENDING;
    P.glow_mat[i]->depth_write = false;
    P.glow[i] = t3_sprite_new(P.glow_mat[i]);
    T3_OBJ(P.glow[i])->visible = false;
    t3_object_add(&P.scene->base, P.glow[i]);
    t3_release(P.glow[i]);
  }
  P.ready = true;
}

void probe_free(void) {
  /* the craft go back to the fixed environment before the probe goes */
  if (P.rt) {
    static const int MATS[] = { M_CHROME, M_LILAC, M_RUST, M_RUST_DARK, M_BLUE, M_TEAL, M_BLACK };
    for (size_t i = 0; i < sizeof MATS / sizeof MATS[0]; i++)
      if (model_mat(MATS[i])) t3_material_set_texture(model_mat(MATS[i]), T3_ENV_MAP, env_texture());
    t3_release(P.rt);
  }
  if (P.scene) t3_release(P.scene);
  if (P.deck_mat) t3_release(P.deck_mat);
  if (P.deck_map) t3_release(P.deck_map);
  for (int i = 0; i < GLOWS; i++)
    if (P.glow_mat[i]) t3_release(P.glow_mat[i]);
  /* the generator belongs to the renderer's lifetime; keep it across worlds */
  t3_pmrem_generator *keep = P.pmrem;
  int every = P.every, refreshes = P.refreshes;
  memset(&P, 0, sizeof P);
  P.pmrem = keep, P.every = every, P.refreshes = refreshes;
}

/* follows the deck's scroll and the explosion flashes */
void probe_sync(float travel) {
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
}

void probe_set_every(int every) { P.every = every; }
