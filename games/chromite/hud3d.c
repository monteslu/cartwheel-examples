/* chromite instruments: the HUD's gauges as 3D objects, after the
 * original's tall glass columns. They hang in front of the camera at fixed
 * places on the screen (worked out from the camera's field of view and the
 * window's aspect each frame, so they scale with any resolution) and are lit
 * and reflect like everything else.
 *
 *   columns   shields (left) and damage (right): a glass tube between chrome
 *             caps, 24 cells inside that glow when lit (instance colours on
 *             one instanced mesh), a pulse running up them
 *   stocks    three slim tubes beside the shields, one per special gun
 *   ships     the spare ships, top right: small copies of the hero's model
 *             turning slowly
 *
 * Render-only. */
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define CELLS 24
#define SPARE 9

typedef struct column { t3_object *root; t3_instanced_mesh *cells; float r, h; int n; } column;

static struct {
  t3_object *root;
  column col[2], stock[GUNS];
  t3_material *glass, *cell_mat, *cap;
  t3_object *ships[SPARE];
  t3_object *ship_spin[SPARE][4];
  bool built;
} H;

static t3_mesh *add(t3_object *parent, t3_geometry *g, t3_material *m, float y) {
  t3_mesh *me = t3_mesh_new(g, m);
  t3_release(g);
  t3_object_set_position(me, 0, y, 0);
  T3_OBJ(me)->cast_shadow = T3_OBJ(me)->receive_shadow = false;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}

static void column_build(column *c, float r, float h, int n) {
  c->root = t3_group_new();
  c->r = r, c->h = h, c->n = n;
  /* the tube and its caps */
  add(c->root, t3_cylinder_geometry_new(r, r, h, 32, 1, true), H.glass, 0);
  for (int e = -1; e <= 1; e += 2) {
    add(c->root, t3_cylinder_geometry_new(r * 1.35f, r * 1.35f, r * 0.9f, 32, 1, false), H.cap, e * (h * 0.5f + r * 0.4f));
    add(c->root, t3_cylinder_geometry_new(r * 1.1f, r * 1.25f, r * 0.35f, 32, 1, false), model_mat(M_BLACK), e * (h * 0.5f - r * 0.05f));
  }
  /* the cells */
  float ch = h / n;
  t3_geometry *g = t3_cylinder_geometry_new(r * 0.72f, r * 0.72f, ch * 0.78f, 20, 1, false);
  c->cells = t3_instanced_mesh_new(g, H.cell_mat, n);
  t3_release(g);
  for (int i = 0; i < n; i++) {
    t3_mat4 m;
    t3_mat4_compose(&m, (t3_vec3){ 0, -h * 0.5f + ch * (i + 0.5f), 0 }, t3_quat_identity(), (t3_vec3){ 1, 1, 1 });
    t3_instanced_mesh_set_matrix_at(c->cells, i, &m);
    t3_instanced_mesh_set_color_at(c->cells, i, (t3_color){ 0, 0, 0 });
  }
  T3_OBJ(c->cells)->frustum_culled = false;
  t3_object_add(c->root, c->cells);
  t3_release(c->cells);
  t3_object_add(H.root, c->root);
  t3_release(c->root);
}

void hud3d_build(t3_object *parent) {
  hud3d_free();
  H.root = t3_group_new();
  H.glass = t3_mesh_physical_material_new(0xffffff);
  H.glass->color = (t3_color){ 0.6f, 0.7f, 0.85f };
  H.glass->metalness = 0;
  H.glass->roughness = 0.04f;
  H.glass->transparent = true;
  H.glass->opacity = 0.05f;
  H.glass->depth_write = false;
  H.glass->clearcoat = 1;
  H.glass->clearcoat_roughness = 0.02f;
  t3_material_set_texture(H.glass, T3_ENV_MAP, env_texture());
  H.glass->env_map_intensity = 0.35f;
  H.cell_mat = t3_mesh_basic_material_new(0xffffff);
  H.cell_mat->blending = T3_ADDITIVE_BLENDING;
  H.cell_mat->transparent = true;
  H.cell_mat->depth_write = false;
  H.cap = t3_material_clone(model_mat(M_CHROME));
  column_build(&H.col[0], 0.048f, 1.3f, CELLS);
  column_build(&H.col[1], 0.048f, 1.3f, CELLS);
  for (int g = 0; g < GUNS; g++) column_build(&H.stock[g], 0.016f, 0.7f, 12);
  for (int i = 0; i < SPARE; i++) {
    int ns = 0;
    H.ships[i] = model_new(CK_HERO, H.root, H.ship_spin[i], &ns);
    for (int k = 0; k < ns; k++) H.ship_spin[i][k]->visible = false; /* no flames on the icons */
    t3_object_set_scale(H.ships[i], 0.075f, 0.075f, 0.075f);
  }
  t3_object_add(parent, H.root);
  t3_release(H.root);
  H.built = true;
}

void hud3d_free(void) {
  if (H.glass) t3_release(H.glass);
  if (H.cell_mat) t3_release(H.cell_mat);
  if (H.cap) t3_release(H.cap);
  memset(&H, 0, sizeof H);
}

/* lights cells 0..fill of a column in colour c, the top one flickering, a
 * pulse running up through the lit ones */
static void column_fill(column *c, float fill, float over, t3_color col, float flash, float t) {
  int lit = (int)(cw_clampf(fill, 0, 1) * c->n + 0.999f);
  float pulse = fmodf(t * 0.6f, 1.0f);
  for (int i = 0; i < c->n; i++) {
    float k = 0.02f;
    if (i < lit) {
      k = 0.5f + 0.5f * (float)i / c->n;
      if (i == lit - 1) k *= 0.7f + 0.3f * sinf(t * 24);
      float pd = fabsf((float)i / c->n - pulse);
      if (pd < 0.07f) k += (0.07f - pd) * 8;
      k += over * (0.8f + 0.4f * sinf(t * 9 + i));
    }
    k += flash * 2;
    t3_instanced_mesh_set_color_at(c->cells, i, (t3_color){ col.r * k, col.g * k, col.b * k });
  }
  c->cells->instance_color->version++; /* three.js's instanceColor.needsUpdate */
}

/* a place on the screen (x, y in -1..1, +y up) at distance d before the eye */
static t3_vec3 screen_at(t3_vec3 eye, t3_vec3 f, t3_vec3 right, t3_vec3 up, float d, float hh, float hw, float x, float y) {
  return (t3_vec3){ eye.x + f.x * d + right.x * x * hw + up.x * y * hh, eye.y + f.y * d + right.y * x * hw + up.y * y * hh,
                    eye.z + f.z * d + right.z * x * hw + up.z * y * hh };
}

void hud3d_sync(const sim *S, float t, t3_vec3 eye, t3_vec3 at, float fov_deg, float aspect, bool visible) {
  if (!H.built) return;
  H.root->visible = visible;
  if (!visible) return;
  t3_vec3 f = { at.x - eye.x, at.y - eye.y, at.z - eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  f.x /= fl, f.y /= fl, f.z /= fl;
  t3_vec3 up = { 0, 1, 0 };
  /* the camera's own up: world up off its forward (it looks down the screen's -Z) */
  t3_vec3 right = { up.y * f.z - up.z * f.y, up.z * f.x - up.x * f.z, up.x * f.y - up.y * f.x };
  float rl = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
  right.x /= -rl, right.y /= -rl, right.z /= -rl;
  up = (t3_vec3){ right.y * f.z - right.z * f.y, right.z * f.x - right.x * f.z, right.x * f.y - right.y * f.x };
  const float d = 4;
  float hh = d * tanf(fov_deg * 0.5f * 3.1415927f / 180), hw = hh * aspect;
  /* the instruments sit upright on the screen: their local Y along screen up */
  t3_mat4 basis = { { right.x, right.y, right.z, 0, up.x, up.y, up.z, 0, -f.x, -f.y, -f.z, 0, 0, 0, 0, 1 } };
  t3_quat q = t3_quat_from_rotation_matrix(&basis);
  const hero *h = &S->h;
  float col_h = H.col[0].h, y_mid = -1 + (col_h * 0.5f + 0.32f) / hh;
  float sx = 1 - 0.3f / hw;
  float xs[2] = { -sx, sx };
  for (int i = 0; i < 2; i++) {
    t3_vec3 p = screen_at(eye, f, right, up, d, hh, hw, xs[i], y_mid);
    t3_object_set_position(H.col[i].root, p.x, p.y, p.z);
    t3_object_set_quaternion(H.col[i].root, q);
  }
  float sh = h->shields / HERO_SHIELDS;
  column_fill(&H.col[0], sh > 1 ? 1 : sh, sh > 1 ? sh - 1 : 0, (t3_color){ 0.03f, 0.2f, 1.1f }, h->shield_flash, t);
  column_fill(&H.col[1], (h->damage + HERO_HULL) / HERO_HULL, 0, (t3_color){ 1.1f, 0.06f, 0.02f }, h->damage_flash, t);
  static const t3_color AC[GUNS] = { { 1.0f, 0.55f, 0.06f }, { 0.12f, 1.0f, 0.18f }, { 0.6f, 0.12f, 1.0f } };
  for (int g = 0; g < GUNS; g++) {
    column *c = &H.stock[g];
    t3_vec3 p = screen_at(eye, f, right, up, d, hh, hw, -sx + (0.13f + 0.05f * g) / hw, -1 + (c->h * 0.5f + 0.32f) / hh);
    t3_object_set_position(c->root, p.x, p.y, p.z);
    t3_object_set_quaternion(c->root, q);
    column_fill(c, h->ammo[g] / AMMO_MAX, 0, AC[g], 0, t + g * 0.3f);
  }
  /* spare ships, top right, nose up the screen */
  for (int i = 0; i < SPARE; i++) {
    bool on = i < h->lives;
    H.ships[i]->visible = on;
    if (!on) continue;
    t3_vec3 p = screen_at(eye, f, right, up, d, hh, hw, 1 - (0.16f + (i % 5) * 0.14f) / hw, 1 - (0.17f + (i / 5) * 0.16f) / hh);
    t3_object_set_position(H.ships[i], p.x, p.y, p.z);
    /* the hero model points -Z with +Y up; on screen its nose points up and
     * it faces the camera: model -Z -> screen up, model +Y -> toward the eye */
    t3_mat4 sb = { { right.x, right.y, right.z, 0, -f.x, -f.y, -f.z, 0, -up.x, -up.y, -up.z, 0, 0, 0, 0, 1 } };
    t3_quat sq = t3_quat_from_rotation_matrix(&sb);
    t3_quat spin = t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, 0.35f * sinf(t * 1.3f + i));
    t3_object_set_quaternion(H.ships[i], t3_quat_multiply(sq, spin));
  }
}
