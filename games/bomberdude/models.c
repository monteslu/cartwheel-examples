/* bomberdude models, built with three.c (three.js r186) Physical materials:
 *
 *   critters  four chunky bomb critters (fox, bunny, bear, frog): a big head
 *             over a small round body, fur with sheen, glossy eyes with
 *             catchlights, blushing cheeks, paws and feet as separate parts
 *             (world.c animates them), a lacquered team-colour scarf and
 *             helmet, and an iridescent bubble for the shield power-up
 *   bombs     lacquered black (clearcoat) with a brass cap, a rope fuse, a
 *             glowing spark and a team-colour band
 *   items     a glossy floating tile (iridescent edge) carrying an icon
 * Every model faces +z (toward the camera); world.c turns them. */
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

const char *const CRIT_NAME[CRIT_KINDS] = { "Rusty", "Mochi", "Bruno", "Lily" };

static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

static t3_mesh *part(t3_object *parent, t3_geometry *g, t3_material *m, float x, float y, float z) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.position = (t3_vec3){ x, y, z };
  me->base.cast_shadow = true;
  me->base.receive_shadow = true;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}
static t3_mesh *blob(t3_object *g, t3_material *m, float rx, float ry, float rz, float x, float y, float z) {
  t3_geometry *sph = t3_sphere_geometry_new(1, 32, 24);
  t3_mesh *me = part(g, sph, m, x, y, z);
  me->base.scale = (t3_vec3){ rx, ry, rz };
  t3_release(sph);
  return me;
}
static t3_object *pivot(t3_object *parent, float x, float y, float z) {
  t3_object *p = t3_group_new();
  p->position = (t3_vec3){ x, y, z };
  t3_object_add(parent, p);
  t3_release(p);
  return p;
}

/* ── critters ───────────────────────────────────────────────────────── */

static const float FUR[CRIT_KINDS][3] = { { 0.95f, 0.5f, 0.2f }, { 0.97f, 0.95f, 0.93f }, { 0.55f, 0.36f, 0.24f }, { 0.45f, 0.8f, 0.35f } };
static const float BELLY[CRIT_KINDS][3] = { { 1, 0.95f, 0.88f }, { 1, 0.86f, 0.9f }, { 0.9f, 0.76f, 0.6f }, { 0.95f, 0.95f, 0.75f } };

void critter_build(critter *c, int kind, int team) {
  memset(c, 0, sizeof *c);
  c->kind = kind;
  c->root = t3_group_new();
  t3_color fur = lin(FUR[kind][0], FUR[kind][1], FUR[kind][2]);
  t3_color soft = { 0.4f + fur.r * 0.6f, 0.4f + fur.g * 0.6f, 0.4f + fur.b * 0.6f };
  t3_material *skin = phys(fur, 0.85f, 0);
  if (kind == CRIT_FROG) { /* frog skin: smooth and wet, a clear coat instead of fur */
    tex_apply(skin, TEX_FROSTING, 1, 0.2f, 1);
    skin->roughness = 0.45f;
    skin->clearcoat = 0.8f;
    skin->clearcoat_roughness = 0.15f;
  } else {
    tex_apply(skin, TEX_FUR, 1, 0.5f, 1);
    skin->sheen = 1;
    skin->sheen_color = soft;
    skin->sheen_roughness = 0.4f;
  }
  t3_material *belly = phys(lin(BELLY[kind][0], BELLY[kind][1], BELLY[kind][2]), 0.85f, 0);
  tex_apply(belly, TEX_FUR, 1, 0.4f, 1);
  belly->sheen = 1;
  belly->sheen_color = lin(1, 0.97f, 0.92f);
  belly->sheen_roughness = 0.5f;
  t3_color tc = lin(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2]);
  t3_material *lacquer = phys(tc, 0.32f, 0);
  lacquer->clearcoat = 1;
  lacquer->clearcoat_roughness = 0.06f;
  c->team_mat = lacquer;
  t3_material *white = phys(lin(0.99f, 0.98f, 0.96f), 0.12f, 0);
  white->clearcoat = 1;
  t3_material *black = phys(lin(0.02f, 0.02f, 0.03f), 0.1f, 0);
  black->clearcoat = 1;
  t3_material *glint = phys(lin(1, 1, 1), 0.2f, 0);
  glint->emissive = lin(1, 1, 1);
  glint->emissive_intensity = 2;
  t3_material *blush = phys(lin(1, 0.55f, 0.6f), 0.7f, 0);
  blush->transparent = true;
  blush->opacity = 0.75f;
  t3_material *dark = phys(lin(0.18f, 0.06f, 0.06f), 0.5f, 0);

  /* the body bounces as one; the head nods on its own pivot */
  c->body = pivot(c->root, 0, 0, 0);
  blob(c->body, skin, 0.25f, 0.24f, 0.23f, 0, 0.29f, 0);
  blob(c->body, belly, 0.17f, 0.17f, 0.1f, 0, 0.27f, 0.15f);
  /* scarf: a lacquered ring round the neck, knot and tail at the back */
  t3_geometry *ring = t3_torus_geometry_new(0.165f, 0.04f, 16, 32, 6.2831853f);
  t3_mesh *sc = part(c->body, ring, lacquer, 0, 0.49f, 0);
  t3_object_set_rotation(&sc->base, 1.5707963f, 0, 0);
  t3_release(ring);
  t3_mesh *knot = blob(c->body, lacquer, 0.06f, 0.045f, 0.1f, 0.08f, 0.47f, -0.16f);
  t3_object_set_rotation(&knot->base, 0.6f, 0.4f, 0);

  c->head = pivot(c->body, 0, 0.58f, 0);
  float hr = 0.27f;
  blob(c->head, skin, hr, hr * 0.92f, hr * 0.95f, 0, hr * 0.8f, 0);
  /* muzzle */
  if (kind == CRIT_FOX) { /* a pointy snout */
    t3_mesh *sn = blob(c->head, belly, hr * 0.36f, hr * 0.3f, hr * 0.5f, 0, hr * 0.5f, hr * 0.92f);
    (void)sn;
  } else if (kind != CRIT_FROG) blob(c->head, belly, hr * 0.48f, hr * 0.36f, hr * 0.3f, 0, hr * 0.52f, hr * 0.82f);
  t3_mesh *nose = blob(c->head, black, hr * 0.12f, hr * 0.09f, hr * 0.09f, 0, hr * 0.66f, hr * (kind == CRIT_FOX ? 1.38f : 1.1f));
  if (kind == CRIT_FROG) nose->base.visible = false;
  /* eyes: big, glossy, a catchlight each; frogs wear theirs on top */
  c->eyes = pivot(c->head, 0, 0, 0);
  t3_geometry *eye = t3_sphere_geometry_new(1, 24, 18);
  for (int s = -1; s <= 1; s += 2) {
    float ex = s * hr * 0.42f, ey = kind == CRIT_FROG ? hr * 1.55f : hr * 0.95f, ez = kind == CRIT_FROG ? hr * 0.45f : hr * 0.78f;
    float er = kind == CRIT_FROG ? hr * 0.3f : hr * 0.24f;
    if (kind == CRIT_FROG) blob(c->eyes, skin, er * 1.25f, er * 1.2f, er * 1.2f, ex, ey - er * 0.15f, ez - er * 0.3f);
    t3_mesh *w = part(c->eyes, eye, white, ex, ey, ez);
    w->base.scale = (t3_vec3){ er, er * 1.15f, er * 0.8f };
    t3_mesh *p = part(c->eyes, eye, black, ex * 0.96f, ey - er * 0.05f, ez + er * 0.62f);
    p->base.scale = (t3_vec3){ er * 0.55f, er * 0.68f, er * 0.32f };
    t3_mesh *g = part(c->eyes, eye, glint, ex * 0.96f + er * 0.18f, ey + er * 0.25f, ez + er * 0.86f);
    g->base.scale = (t3_vec3){ er * 0.16f, er * 0.16f, er * 0.08f };
    /* lids: closed for blinks and knockouts (world.c shows them) */
    t3_mesh *lid = part(c->eyes, eye, skin, ex, ey, ez + er * 0.05f);
    lid->base.scale = (t3_vec3){ er * 1.08f, er * 1.2f, er * 0.85f };
    lid->base.visible = false;
    c->lids[s > 0] = &lid->base;
    blob(c->head, blush, hr * 0.13f, hr * 0.08f, hr * 0.05f, s * hr * 0.6f, hr * 0.5f, hr * 0.78f);
  }
  t3_release(eye);
  /* mouths: a happy curve, or an O (surprise, knocked out) */
  t3_geometry *smile = t3_torus_geometry_new(hr * 0.16f, hr * 0.035f, 8, 16, 3.1415927f);
  t3_mesh *sm = part(c->head, smile, dark, 0, hr * (kind == CRIT_FOX ? 0.36f : 0.5f), hr * (kind == CRIT_FROG ? 0.93f : kind == CRIT_FOX ? 1.3f : 1.08f));
  t3_object_set_rotation(&sm->base, 0, 0, 3.1415927f);
  c->mouth_smile = &sm->base;
  t3_release(smile);
  t3_mesh *mo = blob(c->head, dark, hr * 0.09f, hr * 0.11f, hr * 0.05f, 0, hr * 0.4f, hr * (kind == CRIT_FROG ? 0.9f : 1.04f));
  mo->base.visible = false;
  c->mouth_o = &mo->base;
  /* ears */
  for (int s = -1; s <= 1; s += 2) {
    t3_object *e = pivot(c->head, s * hr * 0.55f, hr * 1.5f, -hr * 0.05f);
    c->ear[s > 0] = e;
    if (kind == CRIT_FOX) {
      t3_geometry *cone = t3_cone_geometry_new(hr * 0.4f, hr * 0.95f, 16, 1, false);
      t3_mesh *o = part(e, cone, skin, 0, hr * 0.3f, 0);
      o->base.scale = (t3_vec3){ 1, 1, 0.55f };
      t3_mesh *in = part(e, cone, dark, 0, hr * 0.26f, hr * 0.08f);
      in->base.scale = (t3_vec3){ 0.6f, 0.75f, 0.3f };
      t3_release(cone);
      t3_object_set_rotation(e, 0, 0, -s * 0.3f);
    } else if (kind == CRIT_BUN) {
      t3_mesh *o = blob(e, skin, hr * 0.15f, hr * 0.62f, hr * 0.1f, 0, hr * 0.55f, 0);
      t3_mesh *in = blob(e, blush, hr * 0.08f, hr * 0.48f, hr * 0.04f, 0, hr * 0.55f, hr * 0.07f);
      (void)o;
      (void)in;
      t3_object_set_rotation(e, 0, 0, -s * 0.18f);
    } else if (kind == CRIT_BEAR) {
      blob(e, skin, hr * 0.24f, hr * 0.22f, hr * 0.14f, 0, 0, 0);
      blob(e, belly, hr * 0.13f, hr * 0.12f, hr * 0.06f, 0, 0, hr * 0.08f);
      e->position.y = hr * 1.38f;
      e->position.x = s * hr * 0.66f;
    } else e->visible = false;
  }
  /* a lacquered backpack in the team colour (a spare bomb rides in it), on the body */
  t3_geometry *pack = geo_rounded_box(0.3f, 0.26f, 0.14f, 0.06f, 3);
  part(c->body, pack, lacquer, 0, 0.32f, -0.22f);
  t3_release(pack);
  t3_material *strap = phys(lin(0.15f, 0.1f, 0.12f), 0.6f, 0);
  t3_geometry *bar = geo_rounded_box(0.05f, 0.24f, 0.04f, 0.015f, 2);
  for (int s2 = -1; s2 <= 1; s2 += 2) {
    t3_mesh *st = part(c->body, bar, strap, s2 * 0.11f, 0.35f, 0.18f);
    t3_object_set_rotation(&st->base, 0.25f, 0, 0);
  }
  t3_release(bar);
  t3_release(strap);
  /* paws and feet on pivots */
  for (int s = -1; s <= 1; s += 2) {
    c->arm[s > 0] = pivot(c->body, s * 0.24f, 0.36f, 0.02f);
    blob(c->arm[s > 0], skin, 0.075f, 0.11f, 0.075f, s * 0.02f, -0.07f, 0);
    c->foot[s > 0] = pivot(c->body, s * 0.12f, 0.06f, 0.02f);
    blob(c->foot[s > 0], kind == CRIT_FROG ? skin : belly, 0.085f, 0.06f, 0.12f, 0, 0, 0.03f);
  }
  /* fox tail, bun puff, bear stub */
  c->tail = pivot(c->body, 0, 0.22f, -0.22f);
  if (kind == CRIT_FOX) {
    t3_mesh *t = blob(c->tail, skin, 0.1f, 0.1f, 0.24f, 0, 0.08f, -0.14f);
    t3_object_set_rotation(&t->base, -0.6f, 0, 0);
    blob(c->tail, belly, 0.07f, 0.07f, 0.08f, 0, 0.2f, -0.32f);
  } else if (kind == CRIT_BUN) blob(c->tail, belly, 0.09f, 0.09f, 0.09f, 0, 0, -0.02f);
  else if (kind == CRIT_BEAR) blob(c->tail, skin, 0.06f, 0.06f, 0.06f, 0, 0, 0);
  /* the bubble shield: thin-film iridescence on a clear, transmissive skin */
  t3_material *bub = phys(lin(1, 1, 1), 0.02f, 0);
  bub->transparent = true;
  bub->opacity = 0.28f;
  bub->iridescence = 1;
  bub->iridescence_ior = 1.33f;
  bub->iridescence_thickness_range[0] = 250;
  bub->iridescence_thickness_range[1] = 650;
  bub->depth_write = false;
  t3_geometry *bg = t3_sphere_geometry_new(0.58f, 40, 28);
  t3_mesh *bm = t3_mesh_new(bg, bub);
  bm->base.position = (t3_vec3){ 0, 0.55f, 0 };
  bm->base.visible = false;
  bm->base.render_order = 10;
  bm->base.receive_shadow = true;
  t3_object_add(c->root, bm);
  c->bubble = &bm->base;
  t3_release(bm);
  t3_release(bg);
  t3_release(bub);
  t3_release(skin);
  t3_release(belly);
  t3_release(white);
  t3_release(black);
  t3_release(glint);
  t3_release(blush);
  t3_release(dark);
  /* lacquer stays referenced by c->team_mat (released with the model) */
}

/* ── bombs ──────────────────────────────────────────────────────────── */

t3_object *model_bomb(int team, t3_material **glow_out, t3_material **band_out, t3_object **spark_out) {
  t3_object *g = t3_group_new();
  t3_material *shell = phys(lin(0.06f, 0.06f, 0.08f), 0.35f, 0.2f);
  shell->clearcoat = 1;
  shell->clearcoat_roughness = 0.05f;
  shell->emissive = lin(1, 0.15f, 0.05f);
  shell->emissive_intensity = 0;
  blob(g, shell, 0.3f, 0.29f, 0.3f, 0, 0.31f, 0);
  t3_material *brass = phys(lin(0.95f, 0.75f, 0.35f), 0.28f, 1);
  t3_geometry *cap = t3_cylinder_geometry_new(0.09f, 0.11f, 0.08f, 20, 1, false);
  part(g, cap, brass, 0, 0.61f, 0);
  t3_release(cap);
  t3_material *band = phys(lin(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2]), 0.3f, 0);
  band->clearcoat = 1;
  t3_geometry *ring = t3_torus_geometry_new(0.296f, 0.035f, 12, 40, 6.2831853f);
  t3_mesh *r = part(g, ring, band, 0, 0.31f, 0);
  t3_object_set_rotation(&r->base, 1.5707963f, 0, 0);
  t3_release(ring);
  t3_material *rope = phys(lin(0.8f, 0.68f, 0.45f), 0.9f, 0);
  tex_apply(rope, TEX_BARK, 1, 0.5f, 1);
  t3_geometry *fuse = t3_cylinder_geometry_new(0.025f, 0.03f, 0.2f, 8, 1, false);
  t3_mesh *fu = part(g, fuse, rope, 0.04f, 0.72f, 0);
  t3_object_set_rotation(&fu->base, 0, 0, -0.45f);
  t3_release(fuse);
  t3_material *spark = phys(lin(1, 0.75f, 0.3f), 0.5f, 0);
  spark->emissive = lin(1, 0.6f, 0.15f);
  spark->emissive_intensity = 8;
  t3_geometry *sg = t3_icosahedron_geometry_new(0.055f, 1);
  t3_mesh *sp = part(g, sg, spark, 0.09f, 0.82f, 0);
  sp->base.cast_shadow = false;
  t3_release(sg);
  /* a soft white highlight band (the lacquer reads as glossy at any angle) */
  if (glow_out) *glow_out = shell;
  if (band_out) *band_out = band;
  if (spark_out) *spark_out = &sp->base;
  t3_release(brass);
  t3_release(rope);
  t3_release(spark);
  return g; /* shell stays referenced by *glow_out's caller through the mesh */
}

/* ── power-ups ──────────────────────────────────────────────────────── */

static const float ITEM_RGB[PU_KINDS][3] = {
  { 1, 1, 1 }, { 0.3f, 0.35f, 0.5f }, { 1, 0.42f, 0.15f }, { 0.25f, 0.75f, 1 }, { 0.95f, 0.3f, 0.35f },
  { 0.65f, 0.4f, 1 }, { 0.8f, 0.85f, 0.95f }, { 0.55f, 0.95f, 0.95f } };

static t3_material *emissive_mat(float r, float g, float b, float k) {
  t3_material *m = phys(lin(r, g, b), 0.4f, 0);
  m->emissive = lin(r, g, b);
  m->emissive_intensity = k;
  return m;
}

t3_object *model_item(int kind) {
  t3_object *g = t3_group_new();
  /* the tile: a glossy gem slab with a thin-film rim */
  t3_material *gem = phys(lin(ITEM_RGB[kind][0], ITEM_RGB[kind][1], ITEM_RGB[kind][2]), 0.18f, 0);
  gem->clearcoat = 1;
  gem->clearcoat_roughness = 0.04f;
  gem->iridescence = 0.6f;
  gem->iridescence_ior = 1.4f;
  gem->emissive = lin(ITEM_RGB[kind][0], ITEM_RGB[kind][1], ITEM_RGB[kind][2]);
  gem->emissive_intensity = 0.25f;
  t3_geometry *slab = geo_rounded_box(0.56f, 0.14f, 0.56f, 0.06f, 3);
  t3_mesh *s = part(g, slab, gem, 0, 0, 0);
  (void)s;
  t3_release(slab);
  t3_material *white = phys(lin(1, 0.98f, 0.94f), 0.3f, 0);
  white->clearcoat = 0.8f;
  t3_object *icon = pivot(g, 0, 0.12f, 0);
  switch (kind) {
  case PU_BOMB: {
    t3_material *b = phys(lin(0.08f, 0.08f, 0.1f), 0.3f, 0.2f);
    b->clearcoat = 1;
    blob(icon, b, 0.14f, 0.14f, 0.14f, 0, 0.12f, 0);
    t3_material *sp = emissive_mat(1, 0.7f, 0.2f, 6);
    blob(icon, sp, 0.035f, 0.035f, 0.035f, 0.05f, 0.3f, 0);
    t3_release(b);
    t3_release(sp);
    break;
  }
  case PU_FIRE: {
    t3_material *f1 = emissive_mat(1, 0.45f, 0.1f, 3), *f2 = emissive_mat(1, 0.85f, 0.3f, 4);
    t3_geometry *cone = t3_cone_geometry_new(0.12f, 0.3f, 16, 1, false);
    part(icon, cone, f1, 0, 0.17f, 0);
    t3_mesh *in = part(icon, cone, f2, 0, 0.12f, 0.03f);
    in->base.scale = (t3_vec3){ 0.6f, 0.6f, 0.6f };
    blob(icon, f1, 0.12f, 0.08f, 0.12f, 0, 0.05f, 0);
    t3_release(cone);
    t3_release(f1);
    t3_release(f2);
    break;
  }
  case PU_SPEED: { /* a lightning bolt */
    t3_material *y = emissive_mat(1, 0.88f, 0.25f, 2.5f);
    t3_geometry *bar = geo_rounded_box(0.07f, 0.2f, 0.05f, 0.02f, 2);
    t3_mesh *a = part(icon, bar, y, -0.03f, 0.22f, 0);
    t3_object_set_rotation(&a->base, 0, 0, -0.5f);
    t3_mesh *b = part(icon, bar, y, 0.03f, 0.08f, 0);
    t3_object_set_rotation(&b->base, 0, 0, -0.5f);
    t3_mesh *c = part(icon, bar, y, 0, 0.15f, 0);
    t3_object_set_rotation(&c->base, 0, 0, 1.2f);
    c->base.scale = (t3_vec3){ 1, 0.6f, 1 };
    t3_release(bar);
    t3_release(y);
    break;
  }
  case PU_KICK: { /* a boot */
    t3_material *r = phys(lin(0.85f, 0.2f, 0.2f), 0.35f, 0);
    r->clearcoat = 1;
    t3_geometry *leg = geo_rounded_box(0.12f, 0.2f, 0.12f, 0.04f, 3), *toe = geo_rounded_box(0.12f, 0.09f, 0.24f, 0.04f, 3);
    part(icon, leg, r, 0, 0.16f, -0.04f);
    part(icon, toe, r, 0, 0.05f, 0.02f);
    t3_geometry *sole = geo_rounded_box(0.13f, 0.03f, 0.25f, 0.01f, 2);
    part(icon, sole, white, 0, 0.0f, 0.02f);
    t3_release(sole);
    t3_release(leg);
    t3_release(toe);
    t3_release(r);
    break;
  }
  case PU_REMOTE: {
    t3_material *body = phys(lin(0.25f, 0.25f, 0.3f), 0.4f, 0.3f), *btn = emissive_mat(1, 0.15f, 0.15f, 3);
    t3_geometry *box = geo_rounded_box(0.14f, 0.24f, 0.08f, 0.03f, 3);
    part(icon, box, body, 0, 0.13f, 0);
    blob(icon, btn, 0.04f, 0.02f, 0.04f, 0, 0.18f, 0.045f);
    t3_geometry *ant = t3_cylinder_geometry_new(0.01f, 0.01f, 0.14f, 6, 1, false);
    part(icon, ant, white, 0.04f, 0.31f, 0);
    blob(icon, btn, 0.025f, 0.025f, 0.025f, 0.04f, 0.38f, 0);
    t3_release(ant);
    t3_release(box);
    t3_release(body);
    t3_release(btn);
    break;
  }
  case PU_PIERCE: { /* a spear point through a ring */
    t3_material *steel = phys(lin(0.9f, 0.92f, 0.95f), 0.15f, 1);
    t3_geometry *cone = t3_cone_geometry_new(0.07f, 0.16f, 12, 1, false), *shaft = t3_cylinder_geometry_new(0.02f, 0.02f, 0.3f, 8, 1, false);
    t3_mesh *c = part(icon, cone, steel, 0.12f, 0.15f, 0);
    t3_object_set_rotation(&c->base, 0, 0, -1.5707963f);
    t3_mesh *sh = part(icon, shaft, steel, -0.06f, 0.15f, 0);
    t3_object_set_rotation(&sh->base, 0, 0, 1.5707963f);
    t3_material *o = emissive_mat(1, 0.5f, 0.15f, 2);
    t3_geometry *ring = t3_torus_geometry_new(0.08f, 0.02f, 8, 20, 6.2831853f);
    t3_mesh *rg = part(icon, ring, o, 0.0f, 0.15f, 0);
    t3_object_set_rotation(&rg->base, 0, 1.5707963f, 0);
    t3_release(ring);
    t3_release(o);
    t3_release(cone);
    t3_release(shaft);
    t3_release(steel);
    break;
  }
  case PU_BUBBLE: {
    t3_material *b = phys(lin(0.9f, 0.97f, 1), 0.03f, 0);
    b->iridescence = 1;
    b->iridescence_ior = 1.33f;
    b->iridescence_thickness_range[0] = 250;
    b->iridescence_thickness_range[1] = 700;
    b->clearcoat = 1;
    blob(icon, b, 0.15f, 0.15f, 0.15f, 0, 0.16f, 0);
    blob(icon, white, 0.03f, 0.03f, 0.02f, 0.06f, 0.24f, 0.12f);
    t3_release(b);
    break;
  }
  default: break;
  }
  t3_release(white);
  t3_release(gem);
  return g;
}
