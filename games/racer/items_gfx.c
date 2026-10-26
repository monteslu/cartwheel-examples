/* racer's item graphics: the item boxes along the road, missiles and homing missiles in flight, oil slicks, mines,
 * the lock-on reticle, and the blasts and the shockwave ring.
 *
 * Boxes are instanced (three meshes for all of them: the shell, the glowing "?" and a halo); hazards and blasts come
 * from pools built up front, so nothing is allocated while racing. Every pose is a function of the simulation
 * (tick, box cooldowns, hazard ages), placed each frame between the last two ticks. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "models.h"

#define NFX 14
#define PI 3.14159265f

typedef struct fxrec {
  int kind;       /* 0 free, 1 shockwave, 2 blast, 3 small flash */
  int age, life;
  t3_vec3 at;
  t3_quat q;
  float size;
} fxrec;

static struct {
  bool built;
  t3_object *root;
  int nbox;
  t3_instanced_mesh *shell, *glyph, *halo;
  t3_object *miss[MAXHZ], *homing[MAXHZ], *oil[MAXHZ], *mine[MAXHZ], *mine_light[MAXHZ];
  t3_object *miss_flame[MAXHZ][2], *homing_flame[MAXHZ][2];
  t3_object *reticle[MAXK];
  t3_object *held_mine[MAXK], *held_oil[MAXK]; /* an item held out behind a kart */
  t3_material *reticle_mat;
  fxrec fx[NFX];
  t3_object *fx_obj[NFX], *fx_ball[NFX], *fx_ring[NFX], *fx_ring2[NFX];
  t3_material *fx_mat[NFX][3];
  t3_material *flame_mat[2], *light_mat;
  t3_geometry *geo[16];
  int ngeo;
  const sim *sim;
  int last_tick;
} I;

void item_icon_color(int item, float rgb[3]) {
  static const float C[IT_KINDS][3] = {
    { 0.5f, 0.5f, 0.5f }, { 0.3f, 0.65f, 1.0f }, { 0.55f, 0.4f, 1.0f }, { 1.0f, 0.3f, 0.2f },  { 1.0f, 0.3f, 0.7f },
    { 0.55f, 0.35f, 0.8f }, { 1.0f, 0.6f, 0.15f }, { 0.3f, 0.95f, 0.9f }, { 1.0f, 0.9f, 0.25f },
  };
  memcpy(rgb, C[item < 0 || item >= IT_KINDS ? 0 : item], sizeof(float) * 3);
}

static t3_geometry *keep(t3_geometry *g) {
  if (g && I.ngeo < 16) I.geo[I.ngeo++] = g;
  return g;
}

void items_reset(void) {
  for (int i = 0; i < I.ngeo; i++) t3_release(I.geo[i]);
  for (int i = 0; i < 2; i++)
    if (I.flame_mat[i]) t3_release(I.flame_mat[i]);
  if (I.light_mat) t3_release(I.light_mat);
  if (I.reticle_mat) t3_release(I.reticle_mat);
  for (int i = 0; i < NFX; i++)
    for (int j = 0; j < 3; j++)
      if (I.fx_mat[i][j]) t3_release(I.fx_mat[i][j]);
  memset(&I, 0, sizeof I);
}

static t3_material *additive(float r, float g, float b, float opacity) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = lin(r, g, b);
  m->transparent = true, m->depth_write = false, m->blending = T3_ADDITIVE_BLENDING, m->opacity = opacity, m->fog = false;
  m->side = T3_DOUBLE_SIDE;
  return m;
}

static t3_mesh *add_geo(t3_object *parent, t3_geometry *g, t3_material *m, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = false;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}

static t3_object *group_in(t3_object *parent) {
  t3_object *o = t3_group_new();
  t3_object_add(parent, o);
  t3_release(o);
  o->visible = false;
  return o;
}

/* a question mark standing on the xy plane, its strokes at z; tubes joined at the corners */
static void qmark(mb *b, float z, const float rgb[3]) {
  t3_vec3 pts[12];
  int n = 0;
  for (int i = 0; i <= 8; i++) {
    float a = (150.0f - 285.0f * (float)i / 8.0f) * PI / 180.0f; /* from the upper left over the top and round to the right */
    pts[n++] = v3(0.00f + 0.15f * cosf(a), 0.1f + 0.15f * sinf(a), z);
  }
  pts[n++] = v3(0.0f, -0.08f, z);
  pts[n++] = v3(0.0f, -0.14f, z);
  mb_polyline(b, pts, n, 0.04f, 7, rgb);
  mb_ball(b, v3(0, -0.27f, z), 0.05f, rgb);
}

static t3_geometry *missile_geo(bool homing) {
  mb b = { 0 };
  float body[3] = { 0.92f, 0.22f, 0.16f }, nose[3] = { 0.9f, 0.9f, 0.88f }, dark[3] = { 0.12f, 0.12f, 0.13f }, band[3] = { 0.95f, 0.8f, 0.15f };
  if (homing) body[0] = 0.75f, body[1] = 0.2f, body[2] = 0.85f;
  mb_cyl(&b, xf_rot(0, 0, 0, PI * 0.5f, 0, 0), 0.13f, 0.13f, 1.0f, 14, body);
  mb_cone(&b, xf_rot(0, 0, 0.7f, PI * 0.5f, 0, 0), 0.13f, 0.4f, 14, nose);
  mb_cyl(&b, xf_rot(0, 0, 0.25f, PI * 0.5f, 0, 0), 0.136f, 0.136f, 0.07f, 14, band);
  mb_cyl(&b, xf_rot(0, 0, -0.5f, PI * 0.5f, 0, 0), 0.1f, 0.14f, 0.12f, 12, dark);
  for (int i = 0; i < 4; i++) {
    float a = (float)i * PI * 0.5f + PI * 0.25f;
    float cx = cosf(a), cy = sinf(a);
    float fl = homing ? 0.34f : 0.26f;
    xf fx = xf_at(cx * 0.2f, cy * 0.2f, -0.4f);
    fx.q = t3_quat_from_axis_angle(v3(0, 0, 1), a);
    mb_rbox(&b, fx, 0.2f, 0.022f, fl, 0.006f, 1, dark);
    if (homing) {
      xf cx2 = xf_at(cx * 0.18f, cy * 0.18f, 0.3f);
      cx2.q = t3_quat_from_axis_angle(v3(0, 0, 1), a);
      mb_rbox(&b, cx2, 0.15f, 0.018f, 0.12f, 0.005f, 1, nose);
    }
  }
  if (homing) mb_ball(&b, v3(0, 0, 0.9f), 0.03f, body); /* a seeker eye */
  return keep(mb_done(&b));
}

static t3_geometry *mine_geo(void) {
  mb b = { .jit = 0.3f };
  float core[3] = { 0.2f, 0.21f, 0.23f }, spike[3] = { 0.45f, 0.42f, 0.4f };
  mb_ball(&b, v3(0, 0, 0), 0.3f, core);
  mb_torus(&b, xf_rot(0, 0, 0, PI * 0.5f, 0, 0), 0.3f, 0.04f, 6, 20, C_YELLOW);
  const int N = 14;
  for (int i = 0; i < N; i++) {
    float y = 1.0f - 2.0f * ((float)i + 0.5f) / (float)N, r = sqrtf(1 - y * y), a = (float)i * 2.399963f;
    t3_vec3 d = v3(cosf(a) * r, y, sinf(a) * r);
    xf sx = xf_between(v3mul(d, 0.28f), v3mul(d, 0.5f), 1);
    mb_geo(&b, t3_cone_geometry_new(1, 1, 6, 1, false), xf_between(v3mul(d, 0.27f), v3mul(d, 0.52f), 0.06f), spike);
    (void)sx;
  }
  return keep(mb_done(&b));
}

static t3_geometry *oil_geo(void) {
  mb b = { 0 };
  float edge[3] = { 0.1f, 0.09f, 0.14f }, mid[3] = { 0.02f, 0.02f, 0.03f };
  const int N = 28;
  int c = mb_vert(&b, v3(0, 0, 0), v3(0, 1, 0), mid);
  int ring0 = b.nv;
  for (int i = 0; i < N; i++) {
    float a = 2.0f * PI * (float)i / (float)N;
    float r = 1.0f + 0.12f * sinf(3 * a + 1) + 0.07f * sinf(5 * a + 2) + 0.05f * sinf(9 * a);
    mb_vert(&b, v3(cosf(a) * r, 0, sinf(a) * r), v3(0, 1, 0), edge);
  }
  for (int i = 0; i < N; i++) mb_tri(&b, c, ring0 + (i + 1) % N, ring0 + i);
  /* splatter: three satellite puddles */
  for (int s = 0; s < 3; s++) {
    float ca = 1.2f + 2.1f * (float)s, cr = 1.12f, rr = 0.22f - 0.03f * (float)s;
    t3_vec3 cc = v3(cosf(ca) * cr, 0, sinf(ca) * cr);
    int sc = mb_vert(&b, cc, v3(0, 1, 0), mid), s0 = b.nv;
    for (int i = 0; i < 10; i++) {
      float a = 2.0f * PI * (float)i / 10.0f;
      mb_vert(&b, v3(cc.x + cosf(a) * rr, 0, cc.z + sinf(a) * rr), v3(0, 1, 0), edge);
    }
    for (int i = 0; i < 10; i++) mb_tri(&b, sc, s0 + (i + 1) % 10, s0 + i);
  }
  return keep(mb_done(&b));
}

static t3_geometry *reticle_geo(void) {
  mb b = { 0 };
  float red[3] = { 1, 1, 1 };
  mb_torus(&b, xf_rot(0, 0, 0, PI * 0.5f, 0, 0), 0.95f, 0.035f, 5, 40, red);
  for (int i = 0; i < 4; i++) {
    float a = (float)i * PI * 0.5f;
    t3_vec3 c = v3(cosf(a) * 1.2f, 0, sinf(a) * 1.2f), in = v3(cosf(a) * 0.82f, 0, sinf(a) * 0.82f);
    t3_vec3 side = v3(-sinf(a) * 0.16f, 0, cosf(a) * 0.16f);
    int v0 = mb_vert(&b, in, v3(0, 1, 0), red), v1 = mb_vert(&b, v3add(c, side), v3(0, 1, 0), red), v2 = mb_vert(&b, v3sub(c, side), v3(0, 1, 0), red);
    mb_tri(&b, v0, v1, v2);
    mb_tri(&b, v0, v2, v1);
  }
  return keep(mb_done(&b));
}

static t3_geometry *flame_cone(void) {
  t3_geometry *g = t3_cone_geometry_new(0.5f, 1.0f, 10, 1, true);
  t3_geometry_translate(g, 0, 0.5f, 0);
  t3_geometry_rotate_x(g, PI * 0.5f);
  return keep(g);
}

void items_build(cw_node scene, const sim *s) {
  items_reset();
  I.sim = s;
  t3_object *root = t3_group_new();
  hang(scene, "Items", root);
  I.root = root;
  t3_release(root);
  I.nbox = s->nbox;
  /* the boxes: a rounded glossy cube in one of four hues, a "?" on every side, a soft halo */
  if (I.nbox > 0) {
    mb sb = { 0 }, gb = { 0 };
    float w[3] = { 1, 1, 1 }, yel[3] = { 1.0f, 0.95f, 0.6f };
    /* kart height and bright, to be seen coming from far down the road */
    mb_rbox(&sb, xf_at(0, 0, 0), 1.35f, 1.35f, 1.35f, 0.2f, 2, w);
    for (int f = 0; f < 4; f++) {
      xf fx = xf_rot(0, 0, 0, 0, (float)f * PI * 0.5f, 0);
      mb gf = { 0 };
      qmark(&gf, 0.69f, yel);
      t3_geometry *g = mb_done(&gf);
      mb_geo(&gb, g, fx, yel);
    }
    {
      mb gt = { 0 };
      qmark(&gt, 0.69f, yel);
      mb_geo(&gb, mb_done(&gt), xf_rot(0, 0, 0, -PI * 0.5f, 0, 0), yel);
    }
    t3_material *sm = phys(lin(1, 1, 1), 0.18f, 0.2f);
    sm->clearcoat = 1;
    sm->emissive = lin(0.35f, 0.35f, 0.35f); /* a glow of their own: they read at a distance and in the dark maps */
    sm->emissive_intensity = 1.0f;
    t3_material *gm = phys(lin(1.0f, 0.9f, 0.5f), 0.4f, 0);
    gm->emissive = lin(1.0f, 0.85f, 0.4f);
    gm->emissive_intensity = 2.6f;
    t3_material *hm = additive(0.6f, 0.85f, 1.0f, 0.1f);
    t3_geometry *sg = keep(mb_done(&sb)), *gg = keep(mb_done(&gb)), *hg = keep(t3_sphere_geometry_new(1.25f, 16, 12));
    I.shell = t3_instanced_mesh_new(sg, sm, I.nbox);
    I.glyph = t3_instanced_mesh_new(gg, gm, I.nbox);
    I.halo = t3_instanced_mesh_new(hg, hm, I.nbox);
    I.shell->mesh.base.cast_shadow = true;
    /* the boxes are spread over the whole map: an instanced mesh is culled by one sphere about its origin, so culling
     * them hid every box away from the start line */
    I.shell->mesh.base.frustum_culled = I.glyph->mesh.base.frustum_culled = I.halo->mesh.base.frustum_culled = false;
    t3_object_add(I.root, I.shell);
    t3_object_add(I.root, I.glyph);
    t3_object_add(I.root, I.halo);
    static const float HUE[4][3] = { { 0.05f, 0.55f, 1.0f }, { 1.0f, 0.12f, 0.75f }, { 0.15f, 0.95f, 0.25f }, { 1.0f, 0.55f, 0.0f } };
    for (int i = 0; i < I.nbox; i++) {
      const float *h = HUE[(i / 3) & 3];
      t3_instanced_mesh_set_color_at(I.shell, i, lin(h[0], h[1], h[2]));
    }
    t3_release(I.shell), t3_release(I.glyph), t3_release(I.halo);
    t3_release(sm), t3_release(gm), t3_release(hm);
  }
  /* shared bits for the hazards */
  I.flame_mat[0] = additive(1.0f, 0.45f, 0.1f, 0.65f);
  I.flame_mat[1] = additive(1.0f, 0.92f, 0.6f, 0.95f);
  I.light_mat = t3_mesh_basic_material_new(0xffffff);
  I.light_mat->color = lin(1.0f, 0.15f, 0.1f);
  I.light_mat->fog = false;
  t3_material *mm = phys(lin(1, 1, 1), 0.35f, 0.5f);
  mm->vertex_colors = true;
  t3_material *mn = phys(lin(1, 1, 1), 0.6f, 0.6f);
  mn->vertex_colors = true;
  t3_material *om = phys(lin(0.04f, 0.04f, 0.06f), 0.04f, 0.0f);
  om->vertex_colors = true;
  om->iridescence = 0.8f;
  om->iridescence_ior = 1.6f;
  om->clearcoat = 1;
  om->clearcoat_roughness = 0.03f;
  t3_geometry *mg = missile_geo(false), *hg = missile_geo(true), *ng = mine_geo(), *og = oil_geo(), *fg = flame_cone();
  t3_geometry *lg = keep(t3_sphere_geometry_new(0.075f, 8, 6));
  for (int h = 0; h < MAXHZ; h++) {
    for (int v = 0; v < 2; v++) {
      t3_object *g = group_in(I.root);
      add_geo(g, v ? hg : mg, mm, true);
      t3_object *pv = t3_group_new();
      t3_object_set_position(pv, 0, 0, -0.56f);
      t3_object_set_rotation(pv, 0, PI, 0);
      t3_object_add(g, pv);
      for (int l = 0; l < 2; l++) {
        t3_mesh *fm = add_geo(pv, fg, I.flame_mat[l], false);
        fm->base.frustum_culled = false;
        (v ? I.homing_flame : I.miss_flame)[h][l] = &fm->base;
      }
      t3_release(pv);
      (v ? I.homing : I.miss)[h] = g;
    }
    t3_object *o = group_in(I.root);
    add_geo(o, og, om, false);
    I.oil[h] = o;
    t3_object *m = group_in(I.root);
    add_geo(m, ng, mn, true);
    t3_mesh *lm = add_geo(m, lg, I.light_mat, false);
    t3_object_set_position(lm, 0, 0.34f, 0);
    I.mine_light[h] = &lm->base;
    I.mine[h] = m;
  }
  {
    /* held out behind a kart: the mine itself, or a squat oil drum */
    /* the drum wears the mine's material (vertex colours), so it needs no program states of its own */
    t3_geometry *dg = keep(t3_cylinder_geometry_new(0.26f, 0.26f, 0.52f, 14, 1, false));
    {
      int nv = dg->attributes[T3_ATTR_POSITION] ? dg->attributes[T3_ATTR_POSITION]->count : 0;
      float *col = malloc(sizeof(float) * 3 * (size_t)(nv > 0 ? nv : 1));
      const float *pos = dg->attributes[T3_ATTR_POSITION]->array;
      for (int v = 0; v < nv; v++) {
        /* dark steel with two painted hoops */
        float y = pos[v * 3 + 1], hoop = fabsf(fabsf(y) - 0.16f) < 0.04f ? 1.0f : 0.0f;
        col[v * 3] = 0.16f + 0.7f * hoop, col[v * 3 + 1] = 0.13f + 0.45f * hoop, col[v * 3 + 2] = 0.17f + 0.05f * hoop;
      }
      t3_attribute *a = t3_attribute_new(T3_FLOAT32, col, nv, 3);
      t3_geometry_set_attribute(dg, T3_ATTR_COLOR, a);
      t3_release(a);
      free(col);
    }
    for (int k = 0; k < MAXK; k++) {
      I.held_mine[k] = group_in(I.root);
      add_geo(I.held_mine[k], ng, mn, true);
      I.held_mine[k]->visible = false;
      I.held_oil[k] = group_in(I.root);
      add_geo(I.held_oil[k], dg, mn, true);
      I.held_oil[k]->visible = false;
    }
  }
  t3_release(mm), t3_release(mn), t3_release(om);
  /* the lock-on reticles, one per kart that could be marked */
  I.reticle_mat = additive(1.0f, 0.18f, 0.1f, 0.9f);
  t3_geometry *rg = reticle_geo();
  for (int i = 0; i < MAXK; i++) {
    t3_object *o = group_in(I.root);
    add_geo(o, rg, I.reticle_mat, false)->base.frustum_culled = false;
    I.reticle[i] = o;
  }
  /* blasts and shock rings */
  t3_geometry *bg = keep(t3_sphere_geometry_new(1, 14, 10)), *ring = t3_ring_geometry_new(0.9f, 1.0f, 56, 1);
  t3_geometry_rotate_x(ring, -PI * 0.5f);
  keep(ring);
  for (int i = 0; i < NFX; i++) {
    I.fx_mat[i][0] = additive(1.0f, 0.55f, 0.15f, 0.8f);
    I.fx_mat[i][1] = additive(0.4f, 0.9f, 1.0f, 0.9f);
    I.fx_mat[i][2] = additive(1.0f, 1.0f, 0.9f, 0.9f);
    t3_object *o = group_in(I.root);
    t3_mesh *b1 = add_geo(o, bg, I.fx_mat[i][0], false), *r1 = add_geo(o, ring, I.fx_mat[i][1], false), *r2 = add_geo(o, ring, I.fx_mat[i][2], false);
    b1->base.frustum_culled = r1->base.frustum_culled = r2->base.frustum_culled = false;
    I.fx_obj[i] = o;
    I.fx_ball[i] = &b1->base;
    I.fx_ring[i] = &r1->base;
    I.fx_ring2[i] = &r2->base;
  }
  I.built = true;
  I.last_tick = s->tick;
}

/* new blasts and shock rings from this tick's events */
void items_tick(const sim *s) {
  if (!I.built) return;
  I.sim = s;
  for (int i = 0; i < NFX; i++) {
    if (!I.fx[i].kind) continue;
    if (++I.fx[i].age >= I.fx[i].life) I.fx[i].kind = 0;
  }
  for (int e = 0; e < s->ev_n; e++) {
    const struct sim_event *ev = &s->ev[e];
    int kind = 0, life = 0;
    float size = 1;
    t3_quat q = { 0, 0, 0, 1 };
    if (ev->kind == EV_HIT) {
      int hk = (int)ev->k;
      if (hk == HZ_SHOCK) kind = 1, life = 50, size = 1;
      else if (hk == HZ_MINE) kind = 2, life = 36, size = 1;
      else kind = 3, life = 22, size = 0.6f;
      if (ev->a >= 0 && ev->a < MAXK) {
        t3_vec3 up = kart_up(&s->k[ev->a]);
        q = t3_quat_from_axis_angle(v3(0, 1, 0), 0);
        t3_vec3 ax = v3cross(v3(0, 1, 0), up);
        float l = v3len(ax);
        if (l > 1e-4f) q = t3_quat_from_axis_angle(v3mul(ax, 1.0f / l), acosf(cw_clampf(up.y, -1, 1)));
      }
    }
    if (!kind) continue;
    for (int i = 0; i < NFX; i++) {
      if (I.fx[i].kind) continue;
      I.fx[i] = (fxrec){ kind, 0, life, ev->at, q, size };
      break;
    }
  }
}

static t3_quat from_to(t3_vec3 f, t3_vec3 t) {
  f = v3norm(f), t = v3norm(t);
  float d = v3dot(f, t);
  if (d > 0.99999f) return (t3_quat){ 0, 0, 0, 1 };
  if (d < -0.99999f) return t3_quat_from_axis_angle(v3(0, 1, 0), PI);
  return t3_quat_from_axis_angle(v3norm(v3cross(f, t)), acosf(d));
}

static float ease_out_back(float x) {
  float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * powf(x - 1, 3) + c1 * powf(x - 1, 2);
}

/* every frame: boxes, hazards and blasts posed between the last two ticks */
void items_frame(const sim *s, float alpha) {
  if (!I.built) return;
  if (!s) s = I.sim;
  float t = ((float)s->tick + alpha) / (float)TICK_HZ;
  /* boxes */
  for (int i = 0; i < I.nbox; i++) {
    const itembox *b = &s->box[i];
    float cool = (float)b->cool + (b->cool > 0 ? 1.0f - alpha : 0.0f);
    float sc = 1.0f, halo = 1.0f, flash = 0;
    if (b->cool > 0) {
      float since = 360.0f - cool; /* ticks since it was taken */
      if (since < 10.0f) sc = fmaxf(0, 1.3f * (1.0f - since / 10.0f)), flash = 1.0f - since / 10.0f;
      else if (cool < 26.0f) {
        float u = 1.0f - cool / 26.0f;
        sc = ease_out_back(u);
        flash = u > 0.6f ? (u - 0.6f) / 0.4f * 0.7f + 0.3f * (1.0f - u) : 0;
      } else sc = 0;
    }
    halo = sc > 0 ? 1.0f + flash * 1.4f : 0;
    float spin = t * 1.7f + (float)i * 0.9f;
    float bob = 0.14f * sinf(t * 2.2f + (float)i * 1.3f);
    t3_quat q = t3_quat_multiply(t3_quat_from_axis_angle(v3(0, 1, 0), spin), t3_quat_from_axis_angle(v3(1, 0, 1), 0.35f + 0.1f * sinf(t * 1.3f + (float)i)));
    t3_mat4 m;
    t3_vec3 p = v3add(b->p, v3(0, bob, 0));
    t3_mat4_compose(&m, p, q, v3(sc, sc, sc));
    t3_instanced_mesh_set_matrix_at(I.shell, i, &m);
    t3_instanced_mesh_set_matrix_at(I.glyph, i, &m);
    float hs = halo * (1.05f + 0.05f * sinf(t * 3.0f + (float)i));
    t3_mat4_compose(&m, p, q, v3(hs * sc, hs * sc, hs * sc));
    t3_instanced_mesh_set_matrix_at(I.halo, i, &m);
  }
  /* new matrices only reach the GPU when the buffer's version moves (three.js's instanceMatrix.needsUpdate): without
   * this every box stayed where it was first uploaded, at the world's origin under the road, and no box was ever seen */
  if (I.nbox > 0) I.shell->instance_matrix->version++, I.glyph->instance_matrix->version++, I.halo->instance_matrix->version++;
  /* hazards */
  bool marked[MAXK] = { false };
  float near_d[MAXK];
  for (int k = 0; k < MAXK; k++) near_d[k] = 1e9f;
  for (int h = 0; h < MAXHZ; h++) {
    const hazard *z = &s->hz[h];
    bool is_m = z->kind == HZ_MISSILE, is_h = z->kind == HZ_HOMING;
    I.miss[h]->visible = is_m;
    I.homing[h]->visible = is_h;
    I.oil[h]->visible = z->kind == HZ_OIL;
    I.mine[h]->visible = z->kind == HZ_MINE;
    if (is_m || is_h) {
      t3_object *o = is_h ? I.homing[h] : I.miss[h];
      t3_vec3 f = z->f, p = v3sub(z->p, v3mul(f, z->speed / (float)TICK_HZ * (1.0f - alpha)));
      /* a homing missile points the way it flies, not along the road */
      if (is_h && z->target >= 0) {
        t3_vec3 to = v3sub(s->k[z->target].p, z->p);
        float l = v3len(to);
        if (l > 3.0f) f = v3norm(v3add(v3mul(f, 0.6f), v3mul(to, 0.4f / l)));
      }
      t3_quat q = t3_quat_multiply(from_to(v3(0, 0, 1), f), t3_quat_from_axis_angle(v3(0, 0, 1), (float)z->age * 0.35f));
      t3_object_set_position(o, p.x, p.y, p.z);
      t3_object_set_quaternion(o, q);
      float k = 0.8f + 0.25f * sinf(t * 70.0f + (float)h) + 0.1f * sinf(t * 113.0f);
      t3_object **fl = is_h ? I.homing_flame[h] : I.miss_flame[h];
      float ramp = cw_clampf((float)z->age / 6.0f, 0, 1);
      t3_object_set_scale(fl[0], 0.34f, 0.34f, 1.1f * k * ramp);
      t3_object_set_scale(fl[1], 0.18f, 0.18f, 0.65f * k * ramp);
      if (is_h && z->target >= 0 && z->target < MAXK) {
        marked[z->target] = true;
        float d = v3len(v3sub(s->k[z->target].p, z->p));
        if (d < near_d[z->target]) near_d[z->target] = d;
      }
    } else if (z->kind == HZ_OIL) {
      tsample m = track_at(s->track, z->s);
      float fill = cw_clampf((float)z->age / 24.0f, 0, 1);
      float grow = 0.15f + 0.85f * (1.0f - (1.0f - fill) * (1.0f - fill));
      float fade = cw_clampf((float)z->life / 70.0f, 0, 1);
      float sc = 1.65f * grow * (0.2f + 0.8f * fade);
      t3_vec3 p = v3add(z->p, v3mul(m.u, -z->h + 0.04f));
      t3_quat q = from_to(v3(0, 1, 0), m.u);
      /* turn the puddle's long axis with the road so the lumps are the same every time */
      t3_object_set_position(I.oil[h], p.x, p.y, p.z);
      t3_object_set_quaternion(I.oil[h], t3_quat_multiply(q, t3_quat_from_axis_angle(v3(0, 1, 0), (float)h * 1.7f)));
      t3_object_set_scale(I.oil[h], sc, 1, sc * 0.9f);
    } else if (z->kind == HZ_MINE) {
      float drop = cw_clampf((float)z->age / 14.0f, 0, 1);
      float bob = 0.06f * sinf(t * 3.0f + (float)h);
      t3_vec3 p = v3add(z->p, v3(0, bob + (1.0f - drop) * 0.7f, 0));
      t3_object_set_position(I.mine[h], p.x, p.y, p.z);
      t3_object_set_rotation(I.mine[h], 0.2f, t * 0.7f + (float)h, 0.1f);
      /* the armed light blinks faster for a while after it is laid, then settles */
      bool armed = z->age > 45;
      float rate = armed ? 1.6f : 7.0f;
      I.mine_light[h]->visible = fmodf(t * rate + (float)h * 0.37f, 1.0f) < 0.45f;
    }
  }
  for (int k = 0; k < MAXK; k++) {
    const kart *kk = &s->k[k];
    bool hold = kk->on && kk->trailing;
    I.held_mine[k]->visible = hold && kk->item == IT_MINE;
    I.held_oil[k]->visible = hold && kk->item == IT_OIL;
    if (hold) {
      t3_vec3 kp = v3sub(kk->p, v3mul(kk->v, (1.0f - alpha) / (float)TICK_HZ));
      t3_vec3 f = kart_fwd(kk), u = kart_up(kk);
      t3_vec3 p = v3add(v3sub(kp, v3mul(f, VEH[kk->veh].len * 0.5f + 1.35f)), v3mul(u, 0.35f + 0.05f * sinf(t * 9.0f)));
      t3_object *o = kk->item == IT_MINE ? I.held_mine[k] : I.held_oil[k];
      t3_object_set_position(o, p.x, p.y, p.z);
      t3_object_set_quaternion(o, from_to(v3(0, 1, 0), u));
    }
  }
  for (int k = 0; k < MAXK; k++) {
    t3_object *r = I.reticle[k];
    r->visible = marked[k] && s->k[k].on;
    if (!r->visible) continue;
    t3_vec3 p = v3sub(s->k[k].p, v3mul(s->k[k].v, (1.0f - alpha) / (float)TICK_HZ));
    float danger = cw_clampf(1.0f - near_d[k] / 70.0f, 0, 1);
    float pulse = 1.0f + (0.06f + 0.14f * danger) * sinf(t * (8.0f + 14.0f * danger));
    t3_object_set_position(r, p.x, p.y + 0.15f, p.z);
    t3_object_set_quaternion(r, t3_quat_multiply(from_to(v3(0, 1, 0), kart_up(&s->k[k])), t3_quat_from_axis_angle(v3(0, 1, 0), t * (2.0f + 4.0f * danger))));
    float sc = 1.35f * pulse * (1.0f + 0.3f * (1.0f - danger));
    t3_object_set_scale(r, sc, sc, sc);
  }
  I.reticle_mat->opacity = 0.65f + 0.3f * sinf(t * 12.0f);
  /* blasts */
  for (int i = 0; i < NFX; i++) {
    const fxrec *f = &I.fx[i];
    t3_object *o = I.fx_obj[i];
    o->visible = f->kind != 0;
    if (!f->kind) continue;
    float u = ((float)f->age + alpha) / (float)f->life;
    u = cw_clampf(u, 0, 1);
    t3_object_set_position(o, f->at.x, f->at.y, f->at.z);
    t3_object_set_quaternion(o, f->q);
    if (f->kind == 1) {
      I.fx_mat[i][1]->color = lin(0.4f, 0.9f, 1.0f);
      I.fx_ball[i]->visible = u < 0.25f;
      float bs = 1.5f + 6.0f * u;
      t3_object_set_scale(I.fx_ball[i], bs, bs * 0.5f, bs);
      I.fx_mat[i][0]->opacity = 0.6f * (1 - u * 4);
      float r1 = 2.0f + 46.0f * (1.0f - (1.0f - u) * (1.0f - u));
      t3_object_set_scale(I.fx_ring[i], r1, r1, r1);
      I.fx_mat[i][1]->opacity = 0.9f * (1 - u);
      I.fx_ring2[i]->visible = u > 0.08f;
      float r2 = 2.0f + 40.0f * (1.0f - (1.0f - (u - 0.08f) / 0.92f) * (1.0f - (u - 0.08f) / 0.92f));
      t3_object_set_scale(I.fx_ring2[i], r2, r2, r2);
      I.fx_mat[i][2]->opacity = 0.7f * (1 - u);
    } else {
      float big = f->kind == 2 ? 1.0f : 0.5f;
      I.fx_ball[i]->visible = true;
      float bs = (0.5f + 3.2f * (1.0f - (1.0f - u) * (1.0f - u))) * big;
      t3_object_set_scale(I.fx_ball[i], bs, bs, bs);
      I.fx_mat[i][0]->opacity = 0.85f * (1 - u) * (1 - u);
      float rs = (1.0f + 7.0f * u) * big;
      t3_object_set_scale(I.fx_ring[i], rs, rs, rs);
      I.fx_mat[i][1]->color = lin(1.0f, 0.7f, 0.3f);
      I.fx_mat[i][1]->opacity = 0.9f * (1 - u);
      I.fx_ring2[i]->visible = false;
    }
  }
}
