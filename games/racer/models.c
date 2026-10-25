/* racer's kart models (first pass: boxes; models.c is the place to make them lovely). */
#include "world.h"

const float KART_RGB[8][3] = { { 0.95f, 0.25f, 0.2f }, { 0.2f, 0.55f, 0.95f }, { 0.95f, 0.8f, 0.15f }, { 0.3f, 0.8f, 0.35f },
                               { 0.85f, 0.4f, 0.9f }, { 0.95f, 0.55f, 0.15f }, { 0.25f, 0.85f, 0.85f }, { 0.9f, 0.9f, 0.9f } };

void kart_build(kart_model *m, int veh, int colour) {
  memset(m, 0, sizeof *m);
  m->veh = veh;
  m->colour = colour;
  const vehicle_def *vd = &VEH[veh];
  m->root = t3_group_new();
  const float *c = KART_RGB[colour & 7];
  t3_material *paint = phys(lin(c[0], c[1], c[2]), 0.35f, 0.2f);
  paint->clearcoat = 0.6f;
  t3_mesh *body = t3_mesh_new(t3_box_geometry_new(vd->wid * 2, 0.5f, vd->len * 2, 1, 1, 1), paint);
  t3_object_set_position(body, 0, 0.1f, 0);
  body->base.cast_shadow = true;
  t3_object_add(m->root, body);
  m->body = &body->base;
  t3_release(body);
  t3_release(paint);
  t3_material *rubber = phys(lin(0.05f, 0.05f, 0.05f), 0.9f, 0);
  for (int i = 0; i < 4; i++) {
    t3_object *piv = t3_group_new();
    t3_mesh *w = t3_mesh_new(t3_cylinder_geometry_new(vd->wheel_r, vd->wheel_r, 0.3f, 18, 1, false), rubber);
    w->base.cast_shadow = true;
    t3_object_set_quaternion(w, (t3_quat){ 0, 0, 0.70710678f, 0.70710678f });
    t3_object_add(piv, w);
    t3_release(w);
    t3_object_add(m->root, piv);
    m->wheel[i] = piv;
    t3_release(piv);
  }
  t3_release(rubber);
}
