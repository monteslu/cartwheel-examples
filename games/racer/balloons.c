/* racer's battle balloons: three on strings tied to the back of every kart in a battle, in its paint. Render only:
 * they read kart.balloons and nothing feeds back. A balloon that goes swells and vanishes, the rest close up; they
 * lean back with speed and swing out in a turn. Each hangs under the kart's node, so it moves with the kart as drawn
 * (interpolated between ticks). */
#include <math.h>
#include <string.h>

#include "arena.h"
#include "cartwheel/math.h"
#include "world.h"

#define NB BALLOONS
#define POP_FRAMES 7

typedef struct bkart {
  t3_object *grp;            /* the tie point: everything below leans about it */
  t3_object *ball[NB], *str[NB];
  float x[NB];               /* each balloon's place across, sprung to its slot */
  int shown;                 /* balloons drawn last frame */
  int pop_slot, pop_t;       /* the one going and how far through */
  float lean, lean_v, swing, swing_v;
  bool on;
} bkart;

static struct {
  bkart k[MAXK];
  t3_material *str_mat;
  const sim *s;
  bool built;
} B;

void balloons_reset(void) {
  /* the nodes go with the scene; the materials and meshes are held by them */
  if (B.str_mat) t3_release(B.str_mat);
  memset(&B, 0, sizeof B);
}

static t3_quat from_to(t3_vec3 f, t3_vec3 t) {
  f = v3norm(f), t = v3norm(t);
  float d = v3dot(f, t);
  if (d > 0.99999f) return (t3_quat){ 0, 0, 0, 1 };
  t3_vec3 c = v3cross(f, t);
  float w = 1.0f + d, l = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z + w * w);
  return (t3_quat){ c.x / l, c.y / l, c.z / l, w / l };
}

void balloons_build(cw_node scene, const sim *s) {
  (void)scene;
  balloons_reset();
  B.s = s;
  if (!s->battle) return;
  B.str_mat = phys(lin(0.95f, 0.93f, 0.88f), 0.7f, 0);
  t3_geometry *sg = t3_sphere_geometry_new(0.28f, 16, 12);
  t3_geometry *kg = t3_cone_geometry_new(0.07f, 0.11f, 8, 1, false);
  t3_geometry *cg = t3_cylinder_geometry_new(0.012f, 0.012f, 1.0f, 4, 1, true);
  t3_geometry_translate(cg, 0, 0.5f, 0); /* the string grows up from its foot */
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &s->k[i];
    cw_node kn = kartgfx_node(i);
    if (!k->on || !cw_node_valid(kn)) continue;
    bkart *b = &B.k[i];
    const float *c = KART_RGB[k->colour & 7];
    t3_material *m = phys(lin(c[0], c[1], c[2]), 0.22f, 0);
    m->emissive = lin(c[0], c[1], c[2]);
    m->emissive_intensity = 0.25f;
    /* see-through, smaller and higher: they mark a kart's balloons without hiding the arena behind them */
    m->transparent = true;
    m->opacity = 0.55f;
    m->depth_write = false;
    const vehicle_def *vd = &VEH[k->veh];
    b->grp = t3_group_new();
    t3_object_set_position(b->grp, 0, 0.55f, -vd->len * 0.82f);
    for (int j = 0; j < NB; j++) {
      t3_mesh *bm = t3_mesh_new(sg, m);
      bm->base.cast_shadow = false;
      t3_mesh *knot = t3_mesh_new(kg, m);
      t3_object_set_position(&knot->base, 0, -0.32f, 0);
      t3_object_set_rotation(&knot->base, 3.14159f, 0, 0);
      t3_object_add(&bm->base, &knot->base);
      t3_release(knot);
      b->ball[j] = &bm->base;
      t3_object_add(b->grp, &bm->base);
      t3_release(bm);
      t3_mesh *sm = t3_mesh_new(cg, B.str_mat);
      b->str[j] = &sm->base;
      t3_object_add(b->grp, &sm->base);
      t3_release(sm);
      b->x[j] = ((float)j - 1.0f) * 0.38f;
    }
    t3_release(m);
    b->shown = k->balloons;
    b->pop_slot = -1;
    b->on = true;
    hang(kn, "Balloons", b->grp);
    t3_release(b->grp);
  }
  t3_release(sg), t3_release(kg), t3_release(cg);
  B.built = true;
}

void balloons_frame(const sim *s, float alpha) {
  (void)alpha;
  if (!B.built || !s) return;
  for (int i = 0; i < MAXK; i++) {
    bkart *b = &B.k[i];
    const kart *k = &s->k[i];
    if (!b->on) continue;
    int n = k->balloons < 0 ? 0 : k->balloons > NB ? NB : k->balloons;
    if (n < b->shown) b->pop_slot = n, b->pop_t = 1; /* the last one in the row goes */
    if (n > b->shown) b->pop_slot = -1;
    b->shown = n;
    /* lean back with speed, out in a turn (springs in the kart's frame) */
    const float dt = 1.0f / 60.0f;
    float lean_t = cw_clampf(k->speed / 40.0f, -0.3f, 1.0f) * 0.7f;
    float swing_t = cw_clampf(-v3dot(k->w, kart_up(k)) * 0.22f, -0.5f, 0.5f);
    b->lean_v += (90.0f * (lean_t - b->lean) - 9.0f * b->lean_v) * dt, b->lean += b->lean_v * dt;
    b->swing_v += (70.0f * (swing_t - b->swing) - 7.0f * b->swing_v) * dt, b->swing += b->swing_v * dt;
    t3_object_set_rotation(b->grp, -b->lean, 0, b->swing);
    float t = (float)s->tick / 60.0f;
    for (int j = 0; j < NB; j++) {
      bool popping = j == b->pop_slot && b->pop_t > 0;
      /* just popped: the rest blink while the kart cannot lose another */
      bool vis = (j < n || popping) && !k->parked && !(k->ram_cool > 0 && !popping && ((k->ram_cool / 6) & 1));
      b->ball[j]->visible = vis;
      b->str[j]->visible = vis && !popping;
      if (!vis) continue;
      float want = ((float)j - (float)(n - 1) * 0.5f) * 0.38f;
      if (!popping) b->x[j] += (want - b->x[j]) * 0.12f;
      float bob = 0.06f * sinf(t * 2.3f + (float)(i * 3 + j));
      t3_vec3 top = { b->x[j] * 1.25f + 0.05f * sinf(t * 1.7f + (float)j), 2.05f + bob + (j == 1 ? 0.1f : 0), -0.1f };
      float sc = 1.0f;
      if (popping) {
        float u = (float)b->pop_t / (float)POP_FRAMES;
        sc = 1.0f + 0.7f * u;
        if (++b->pop_t > POP_FRAMES) b->pop_t = 0, b->pop_slot = -1, b->ball[j]->visible = false;
      }
      t3_object_set_position(b->ball[j], top.x, top.y, top.z);
      t3_object_set_scale(b->ball[j], sc, sc * 1.12f, sc);
      /* the string from the tie point to the knot */
      t3_vec3 knot = { top.x, top.y - 0.35f, top.z };
      float len = v3len(knot);
      t3_object_set_position(b->str[j], 0, 0, 0);
      t3_object_set_quaternion(b->str[j], from_to((t3_vec3){ 0, 1, 0 }, knot));
      t3_object_set_scale(b->str[j], 1, len, 1);
    }
  }
}
