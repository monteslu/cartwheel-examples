/* pugball models, built with three.c (three.js r186) Physical materials.
 *
 *   pugs   chunky pugs: a barrel body and a big round head in sheen fur, a
 *          black velvet mask with forehead wrinkles, bulging glossy eyes
 *          with catchlights, flappy ears and a curly tail on pivots
 *          (world.c animates them), stubby legs, a tongue for full boost,
 *          a lacquered harness in the team colour carrying a twin-canister
 *          jet pack whose nozzles glow and flame when it boosts, and a hat
 *   cat    the ball: a fat, furry, deeply unimpressed tabby rolled into a
 *          sphere, paws tucked, tail wrapped round, ears flat or up, three
 *          faces (grumpy, outraged, dizzy) and whiskers
 * Pugs face +z; the cat's face looks along +z of its own frame. */
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

const char *const PUG_COAT_NAME[PUG_KINDS] = { "Fawn", "Black", "Apricot", "Silver" };
const char *const HAT_NAME[HAT_KINDS] = { "No hat", "Ball cap", "Crown", "Party hat", "Beanie", "Top hat" };
const char *const TRAIL_NAME[TRAIL_KINDS] = { "Fire", "Bubbles", "Rainbow", "Stardust", "Hearts" };
const float TEAM_RGB[2][3] = { { 0.2f, 0.55f, 1 }, { 1, 0.3f, 0.25f } };

static const float COAT[PUG_KINDS][3] = { { 0.84f, 0.74f, 0.58f }, { 0.13f, 0.12f, 0.12f }, { 0.92f, 0.62f, 0.36f }, { 0.74f, 0.74f, 0.76f } };
static const float CHEST[PUG_KINDS][3] = { { 0.93f, 0.86f, 0.72f }, { 0.2f, 0.19f, 0.19f }, { 0.98f, 0.78f, 0.55f }, { 0.86f, 0.86f, 0.88f } };

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
static t3_geometry *SPH;
static t3_mesh *blob(t3_object *g, t3_material *m, float rx, float ry, float rz, float x, float y, float z) {
  if (!SPH) SPH = t3_sphere_geometry_new(1, 32, 24);
  t3_mesh *me = part(g, SPH, m, x, y, z);
  me->base.scale = (t3_vec3){ rx, ry, rz };
  return me;
}
static t3_object *pivot(t3_object *parent, float x, float y, float z) {
  t3_object *p = t3_group_new();
  p->position = (t3_vec3){ x, y, z };
  t3_object_add(parent, p);
  t3_release(p);
  return p;
}
static void rot(t3_object *o, float x, float y, float z) { t3_object_set_rotation(o, x, y, z); }
static t3_material *emissive(float r, float g, float b, float k) {
  t3_material *m = phys(lin(r, g, b), 0.4f, 0);
  m->emissive = lin(r, g, b);
  m->emissive_intensity = k;
  return m;
}
static t3_material *fur(const float rgb[3], float sheen) {
  t3_material *m = phys(lin(rgb[0], rgb[1], rgb[2]), 0.82f, 0);
  tex_apply(m, TEX_FUR, 2, 0.45f, 1);
  m->sheen = sheen;
  m->sheen_color = lin(0.45f + rgb[0] * 0.55f, 0.45f + rgb[1] * 0.55f, 0.45f + rgb[2] * 0.55f);
  m->sheen_roughness = 0.45f;
  return m;
}

/* ── pugs ───────────────────────────────────────────────────────────── */

static void build_hat(pug_model *m, int hat, t3_color tc) {
  if (hat == HAT_NONE) return;
  t3_object *h = pivot(m->head, 0, 0.27f, -0.02f);
  m->hat = h;
  switch (hat) {
  case HAT_CAP: {
    t3_material *c = phys(tc, 0.5f, 0);
    c->sheen = 0.6f;
    c->sheen_color = lin(1, 1, 1);
    t3_geometry *dome = t3_sphere_geometry_new_ex(0.2f, 24, 12, 0, 6.2831853f, 0, 1.5707963f);
    part(h, dome, c, 0, 0, 0);
    t3_release(dome);
    blob(h, c, 0.17f, 0.018f, 0.14f, 0, 0.01f, 0.17f); /* the bill */
    t3_material *w = phys(lin(1, 1, 1), 0.5f, 0);
    blob(h, w, 0.03f, 0.02f, 0.03f, 0, 0.2f, 0);
    t3_release(w);
    t3_release(c);
    break;
  }
  case HAT_CROWN: {
    t3_material *gold = phys(lin(1, 0.78f, 0.3f), 0.22f, 1);
    t3_geometry *ring = t3_cylinder_geometry_new(0.15f, 0.14f, 0.1f, 24, 1, false);
    part(h, ring, gold, 0, 0.04f, 0);
    t3_release(ring);
    t3_geometry *stud = t3_cone_geometry_new(0.035f, 0.1f, 8, 1, false);
    t3_material *gem = phys(lin(0.9f, 0.1f, 0.2f), 0.05f, 0);
    gem->iridescence = 0.6f;
    gem->clearcoat = 1;
    for (int k = 0; k < 5; k++) {
      float a = k * 1.2566371f;
      part(h, stud, gold, 0.145f * cw_sin(a), 0.13f, 0.145f * cw_cos(a));
      blob(h, gem, 0.022f, 0.022f, 0.022f, 0.15f * cw_sin(a), 0.05f, 0.15f * cw_cos(a));
    }
    t3_release(stud);
    t3_release(gem);
    t3_release(gold);
    break;
  }
  case HAT_PARTY: {
    t3_material *c = phys(lin(1, 0.45f, 0.75f), 0.4f, 0);
    c->clearcoat = 0.8f;
    t3_geometry *cone = t3_cone_geometry_new(0.11f, 0.32f, 24, 1, false);
    t3_mesh *k = part(h, cone, c, 0, 0.15f, 0);
    rot(&k->base, -0.15f, 0, 0.12f);
    t3_release(cone);
    t3_material *pom = phys(lin(1, 0.9f, 0.3f), 0.8f, 0);
    pom->sheen = 1;
    pom->sheen_color = lin(1, 1, 0.8f);
    blob(h, pom, 0.05f, 0.05f, 0.05f, 0.04f, 0.31f, -0.05f);
    t3_release(pom);
    t3_release(c);
    break;
  }
  case HAT_BEANIE: {
    t3_material *c = phys(tc, 0.9f, 0);
    tex_apply(c, TEX_FUR, 4, 0.8f, 1);
    c->sheen = 1;
    c->sheen_color = lin(1, 1, 1);
    t3_geometry *dome = t3_sphere_geometry_new_ex(0.21f, 24, 12, 0, 6.2831853f, 0, 1.6f);
    part(h, dome, c, 0, -0.03f, 0);
    t3_release(dome);
    t3_material *w = phys(lin(0.97f, 0.97f, 0.95f), 0.95f, 0);
    w->sheen = 1;
    w->sheen_color = lin(1, 1, 1);
    blob(h, w, 0.075f, 0.075f, 0.075f, 0, 0.2f, 0);
    t3_geometry *cuff = t3_torus_geometry_new(0.2f, 0.035f, 10, 32, 6.2831853f);
    t3_mesh *cm = part(h, cuff, w, 0, -0.01f, 0);
    rot(&cm->base, 1.5707963f, 0, 0);
    t3_release(cuff);
    t3_release(w);
    t3_release(c);
    break;
  }
  case HAT_TOPHAT: {
    t3_material *c = phys(lin(0.05f, 0.05f, 0.06f), 0.35f, 0);
    c->sheen = 0.8f;
    c->sheen_color = lin(0.4f, 0.4f, 0.45f);
    t3_geometry *brim = t3_cylinder_geometry_new(0.22f, 0.22f, 0.02f, 32, 1, false);
    part(h, brim, c, 0, 0.01f, 0);
    t3_geometry *crown = t3_cylinder_geometry_new(0.13f, 0.14f, 0.24f, 32, 1, false);
    part(h, crown, c, 0, 0.13f, 0);
    t3_material *band = phys(tc, 0.3f, 0);
    band->clearcoat = 1;
    t3_geometry *bg = t3_cylinder_geometry_new(0.142f, 0.142f, 0.04f, 32, 1, false);
    part(h, bg, band, 0, 0.04f, 0);
    t3_release(bg);
    t3_release(band);
    t3_release(brim);
    t3_release(crown);
    t3_release(c);
    break;
  }
  default: break;
  }
}

/* A pug, stylised-real: a compact square body with a deep chest on short
 * sturdy legs (paws and toes), a big round head with a flat muzzle, the
 * black mask over the muzzle, round the eyes and on the ears, a nose roll
 * and forehead wrinkles in geometry, wide-set dark glossy eyes, a jowly
 * underbite with a pink tongue tip, small folded rose ears, a neck roll and
 * a tightly curled double tail over the back. Fawns carry a dark trace down
 * the spine. The harness wraps the body; the jet pack rides on its plate.
 * Origin: the body's centre, RIDE above the ground; faces +z. */
/* the skull: a sphere pushed into a pug's head. Broad, flat on top, a flat
 * face, a brow ridge over the eyes, cheek pads, the forehead creases cut in.
 * The seam is at the back; the uv stays the sphere's, so the head map's mask
 * lands on the face. */
static t3_geometry *pug_skull(void) {
  t3_geometry *g = t3_sphere_geometry_new_ex(1, 96, 72, 4.712389f, 6.2831853f, 0, 3.1415927f);
  t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
  float *p = pa->array;
  for (int i = 0; i < pa->count; i++) {
    float x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
    float r = 1;
    if (y > 0) r -= 0.16f * y * y * y;                                     /* flat on top */
    r += 0.07f * (1 - y * y) * x * x;                                      /* broad */
    if (z > 0.55f) r -= 0.55f * (z - 0.55f) * (z - 0.55f);                 /* the flat face */
    for (int sd = -1; sd <= 1; sd += 2) {
      float bx = x - sd * 0.42f, by = y - 0.36f, bz = z - 0.8f;            /* the brow ridge */
      r += 0.07f * cw_exp(-(bx * bx * 1.4f + by * by * 6 + bz * bz) / 0.05f);
      float cx = x - sd * 0.62f, cy = y + 0.3f, cz = z - 0.66f;            /* cheek pads */
      r += 0.1f * cw_exp(-(cx * cx + cy * cy + cz * cz) / 0.07f);
      float mx = x - sd * 0.2f, my = y + 0.42f, mz = z - 0.88f;            /* the muzzle: two pads */
      r += 0.2f * cw_exp(-(mx * mx * 1.6f + my * my + mz * mz) / 0.05f);
    }
    r -= 0.045f * pug_crease(x, y, z);                                     /* the creases */
    p[i * 3] = x * r, p[i * 3 + 1] = y * r, p[i * 3 + 2] = z * r;
  }
  t3_geometry_compute_vertex_normals(g);
  return g;
}

/* a fur material over a coat map, with the fine fur normal and ao */
static t3_material *coat_mat(t3_texture *map, const float rgb[3], float ru, float rv) {
  t3_material *m = phys(lin(1, 1, 1), 0.78f, 0);
  tex_apply_uv(m, TEX_FUR, ru, rv, 0.55f, 1);
  t3_material_set_texture(m, T3_MAP, map);
  m->sheen = 1;
  m->sheen_color = lin(0.4f + rgb[0] * 0.5f, 0.4f + rgb[1] * 0.5f, 0.4f + rgb[2] * 0.5f);
  m->sheen_roughness = 0.4f;
  return m;
}

/* the torso's uv: v runs belly (0) to back (1), u round the sides */
static void torso_uv(t3_geometry *g, float h) {
  t3_attribute *pa = g->attributes[T3_ATTR_POSITION], *ua = g->attributes[T3_ATTR_UV];
  float *p = pa->array, *uv = ua->array;
  for (int i = 0; i < pa->count; i++) {
    uv[i * 2] = (p[i * 3] + p[i * 3 + 2]) * 1.2f + 0.5f;
    uv[i * 2 + 1] = p[i * 3 + 1] / h + 0.5f;
  }
  ua->version++;
}

#define DY 0.035f  /* the body sits a little higher on longer legs */
#define HEAD_Y 0.235f

void pug_build(pug_model *m, int team, pug_look look) {
  memset(m, 0, sizeof *m);
  m->look = look;
  m->team = team;
  m->root = t3_group_new();
  int coat = look.coat % PUG_KINDS;
  bool black = coat == PUG_BLACK;
  const float *cc = COAT[coat];
  t3_material *skin = coat_mat(tex_pug_body(coat, cc, CHEST[coat], black), cc, 7, 5);
  t3_material *chest = fur(CHEST[coat], 1);
  t3_material *head_m = coat_mat(tex_pug_head(coat, cc, black), cc, 10, 6);
  float mk = black ? 0.1f : 0.15f;
  t3_material *mask = phys(lin(mk, mk * 0.9f, mk * 0.88f), 0.62f, 0);
  tex_apply(mask, TEX_FUR, 4, 0.35f, 1);
  mask->sheen = 1;
  mask->sheen_color = lin(0.3f, 0.27f, 0.27f);
  mask->sheen_roughness = 0.3f;
  /* the ears: dark velvet, the light comes through warm at the edges */
  t3_material *ear_m = phys(lin(mk * 1.1f, mk * 0.95f, mk * 0.9f), 0.7f, 0);
  ear_m->sheen = 1;
  ear_m->sheen_color = lin(0.5f, 0.3f, 0.25f);
  ear_m->sheen_roughness = 0.25f;
  t3_material *nose = phys(lin(0.015f, 0.015f, 0.018f), 0.18f, 0);
  nose->clearcoat = 0.8f;
  nose->clearcoat_roughness = 0.2f;
  t3_material *dark = phys(lin(0.004f, 0.004f, 0.005f), 0.5f, 0);
  /* the eye: a dark brown iris under a wet cornea, a black pupil, a little
   * white at the corners */
  t3_material *eye = phys(lin(0.36f, 0.19f, 0.08f), 0.05f, 0);
  eye->clearcoat = 1;
  eye->clearcoat_roughness = 0.01f;
  t3_material *pupil = phys(lin(0.006f, 0.005f, 0.005f), 0.05f, 0);
  pupil->clearcoat = 1;
  pupil->clearcoat_roughness = 0.01f;
  t3_material *white = phys(lin(0.86f, 0.8f, 0.74f), 0.15f, 0);
  white->clearcoat = 1;
  t3_material *glint = emissive(1, 1, 1, 2.5f);
  m->eye_glint = glint;
  t3_material *pink = phys(lin(0.95f, 0.42f, 0.48f), 0.3f, 0);
  pink->clearcoat = 0.7f;
  t3_material *nail = phys(lin(0.12f, 0.1f, 0.09f), 0.35f, 0);
  t3_color tc = lin(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2]);
  float hk = look.harness == 0 ? 1 : look.harness == 1 ? 0.55f : 1.25f;
  t3_material *lacquer = phys((t3_color){ tc.r * hk, tc.g * hk, tc.b * hk }, 0.4f, 0.05f);
  lacquer->clearcoat = 0.8f;
  lacquer->clearcoat_roughness = 0.12f;
  t3_material *steel = phys(lin(0.85f, 0.86f, 0.9f), 0.22f, 1);
  tex_apply(steel, TEX_METAL, 1, 0.3f, 1);
  steel->roughness = 0.3f;

  /* the body on a squash pivot at the ground, and a roll pivot for turns.
   * Cobby and square: a straight topline from the withers to the tail */
  m->body = pivot(m->root, 0, -RIDE, 0);
  m->roll = pivot(m->body, 0, RIDE, 0);
  t3_object *b = m->roll;
  t3_geometry *torso = geo_rounded_box(0.4f, 0.32f, 0.54f, 0.14f, 4);
  torso_uv(torso, 0.32f);
  part(b, torso, skin, 0, 0.01f + DY, -0.07f);
  t3_release(torso);
  blob(b, chest, 0.18f, 0.15f, 0.15f, 0, -0.03f + DY, 0.17f);   /* the deep chest */
  blob(b, skin, 0.13f, 0.14f, 0.15f, -0.12f, 0.0f + DY, -0.23f);  /* haunches */
  blob(b, skin, 0.13f, 0.14f, 0.15f, 0.12f, 0.0f + DY, -0.23f);
  if (!black) blob(b, mask, 0.024f, 0.01f, 0.24f, 0, 0.168f + DY, -0.12f); /* the trace down the spine */
  /* the neck and its roll */
  blob(b, skin, 0.17f, 0.15f, 0.15f, 0, 0.1f + DY, 0.23f);
  t3_geometry *roll = t3_torus_geometry_new(0.155f, 0.03f, 10, 28, 6.2831853f);
  t3_material *plain = fur(cc, 1);
  t3_mesh *nr = part(b, roll, plain, 0, 0.11f + DY, 0.24f);
  t3_release(plain);
  nr->base.scale = (t3_vec3){ 1, 0.9f, 1 };
  t3_object_set_rotation(&nr->base, -0.3f, 0, 0);
  t3_release(roll);
  /* legs: sturdy and straight, a paw with three toes and dark nails */
  t3_geometry *leg = t3_cylinder_geometry_new(0.058f, 0.068f, 0.235f, 14, 1, false);
  for (int k = 0; k < 4; k++) {
    float sx = k & 1 ? 1.0f : -1.0f, front = k < 2 ? 1.0f : 0.0f;
    float z = front ? 0.17f : -0.25f;
    m->leg[k] = pivot(b, sx * (front ? 0.115f : 0.13f), -0.11f + DY, z);
    blob(m->leg[k], skin, 0.072f, 0.08f, 0.08f, 0, 0.0f, 0); /* the upper leg, thick */
    part(m->leg[k], leg, skin, 0, -0.118f, 0);
    blob(m->leg[k], black ? skin : chest, 0.062f, 0.034f, 0.08f, 0, -0.253f, 0.025f);
    for (int t = -1; t <= 1; t++) {
      blob(m->leg[k], black ? skin : chest, 0.021f, 0.02f, 0.024f, t * 0.025f, -0.261f, 0.083f);
      blob(m->leg[k], nail, 0.008f, 0.008f, 0.01f, t * 0.025f, -0.269f, 0.104f);
    }
  }
  t3_release(leg);
  /* the tail: a tight double curl lying on the hip */
  m->tail = pivot(b, 0, 0.17f + DY, -0.345f);
  t3_geometry *curl = t3_torus_geometry_new(0.065f, 0.03f, 10, 24, 6.2831853f);
  t3_geometry *curl2 = t3_torus_geometry_new(0.032f, 0.022f, 8, 18, 6.2831853f);
  t3_mesh *c1 = part(m->tail, curl, skin, 0, 0.03f, 0);
  t3_object_set_rotation(&c1->base, -1.15f, 0, 0);
  t3_mesh *c2 = part(m->tail, curl2, black ? skin : mask, 0.005f, 0.058f, 0.012f);
  t3_object_set_rotation(&c2->base, -1.15f, 0, 0);
  t3_release(curl);
  t3_release(curl2);
  /* the harness: the girth strap behind the front legs, the chest strap round
   * the deep chest, a back plate */
  t3_geometry *band = t3_torus_geometry_new(0.21f, 0.022f, 8, 40, 6.2831853f);
  t3_mesh *g1 = part(b, band, lacquer, 0, 0.01f + DY, -0.05f);
  g1->base.scale = (t3_vec3){ 1.0f, 0.82f, 1 };
  t3_mesh *g2 = part(b, band, lacquer, 0, 0.0f + DY, 0.2f);
  g2->base.scale = (t3_vec3){ 0.9f, 0.8f, 1 };
  t3_object_set_rotation(&g2->base, -0.35f, 0, 0);
  t3_release(band);
  t3_geometry *plate = geo_rounded_box(0.3f, 0.04f, 0.4f, 0.018f, 2);
  part(b, plate, lacquer, 0, 0.19f + DY, -0.06f);
  t3_release(plate);
  t3_geometry *strap = geo_rounded_box(0.05f, 0.3f, 0.025f, 0.01f, 1);
  t3_mesh *cs = part(b, strap, lacquer, 0, 0.0f + DY, 0.285f);
  t3_object_set_rotation(&cs->base, -0.35f, 0, 0);
  t3_release(strap);
  /* the jet pack: twin canisters on the plate, nozzles glowing, a flame behind */
  m->pack = pivot(b, 0, 0.27f + DY, -0.04f);
  t3_geometry *can = t3_cylinder_geometry_new(0.065f, 0.065f, 0.28f, 16, 1, false);
  t3_geometry *nozzle = t3_cylinder_geometry_new(0.045f, 0.066f, 0.07f, 16, 1, false);
  t3_material *noz = phys(lin(0.25f, 0.25f, 0.28f), 0.35f, 1);
  noz->emissive = lin(1, 0.45f, 0.1f);
  noz->emissive_intensity = 0.4f;
  m->flame_mat = noz;
  for (int sd = -1; sd <= 1; sd += 2) {
    t3_mesh *c = part(m->pack, can, steel, sd * 0.085f, 0.0f, 0);
    t3_object_set_rotation(&c->base, 1.5707963f, 0, 0);
    blob(m->pack, steel, 0.065f, 0.065f, 0.065f, sd * 0.085f, 0.0f, 0.14f);
    t3_mesh *n = part(m->pack, nozzle, noz, sd * 0.085f, 0.0f, -0.17f);
    t3_object_set_rotation(&n->base, 1.5707963f, 0, 0);
    blob(m->pack, lacquer, 0.067f, 0.067f, 0.025f, sd * 0.085f, 0.0f, 0.05f);
  }
  t3_release(can);
  t3_release(nozzle);
  m->flame = pivot(m->pack, 0, 0.0f, -0.22f);
  t3_material *fl = t3_mesh_basic_material_new(0xffffff);
  fl->color = lin(1, 0.55f, 0.15f);
  fl->transparent = true;
  fl->opacity = 0.9f;
  fl->blending = T3_ADDITIVE_BLENDING;
  fl->depth_write = false;
  t3_material *core = t3_mesh_basic_material_new(0xffffff);
  core->color = lin(1, 0.95f, 0.75f);
  core->transparent = true;
  core->blending = T3_ADDITIVE_BLENDING;
  core->depth_write = false;
  t3_geometry *cone = t3_cone_geometry_new(0.06f, 0.42f, 16, 1, true);
  t3_geometry *cone2 = t3_cone_geometry_new(0.035f, 0.26f, 12, 1, true);
  for (int sd = -1; sd <= 1; sd += 2) {
    t3_mesh *f1 = part(m->flame, cone, fl, sd * 0.085f, 0, -0.2f);
    t3_object_set_rotation(&f1->base, -1.5707963f, 0, 0);
    f1->base.cast_shadow = false;
    f1->base.render_order = 5;
    t3_mesh *f2 = part(m->flame, cone2, core, sd * 0.085f, 0, -0.12f);
    t3_object_set_rotation(&f2->base, -1.5707963f, 0, 0);
    f2->base.cast_shadow = false;
    f2->base.render_order = 6;
  }
  t3_release(cone);
  t3_release(cone2);
  t3_release(fl);
  t3_release(core);
  m->flame->visible = false;

  /* the head: about 0.9 of the shoulders across, carried high on the neck.
   * The skull is one shaped mesh; the muzzle, nose roll, jowls and jaw are
   * charcoal and blend into the head map's mask */
  m->head = pivot(b, 0, HEAD_Y, 0.33f);
  t3_object *h = m->head;
  t3_geometry *skull = pug_skull();
  t3_mesh *sk = part(h, skull, head_m, 0, 0.02f, 0.0f);
  sk->base.scale = (t3_vec3){ 0.172f, 0.15f, 0.16f };
  t3_release(skull);
  /* the muzzle is in the skull (short, two pads); the nose sits on top of it
   * under the nose roll, with a dark valley between them */
  blob(h, nose, 0.032f, 0.021f, 0.018f, 0, -0.008f, 0.158f);
  for (int sd = -1; sd <= 1; sd += 2) blob(h, dark, 0.008f, 0.006f, 0.005f, sd * 0.012f, -0.011f, 0.173f);
  t3_geometry *fold = t3_torus_geometry_new(0.042f, 0.015f, 8, 22, 3.1415927f);
  t3_mesh *nf = part(h, fold, mask, 0, 0.012f, 0.145f);
  t3_object_set_rotation(&nf->base, -0.4f, 0, 0);
  nf->base.scale = (t3_vec3){ 1, 0.8f, 1 };
  t3_release(fold);
  blob(h, dark, 0.04f, 0.009f, 0.016f, 0, 0.012f, 0.147f); /* the valley under the roll */
  /* jowls: they hang below the jaw line */
  for (int sd = -1; sd <= 1; sd += 2) blob(h, mask, 0.036f, 0.042f, 0.032f, sd * 0.058f, -0.084f, 0.098f);
  /* the underbite: a jaw that juts a touch, a lower lip, the tongue tip */
  m->jaw = pivot(h, 0, -0.088f, 0.09f);
  blob(m->jaw, mask, 0.05f, 0.022f, 0.05f, 0, 0, 0.03f);
  blob(m->jaw, dark, 0.04f, 0.008f, 0.014f, 0, 0.01f, 0.07f);
  m->tongue = pivot(m->jaw, 0.01f, 0.008f, 0.066f);
  blob(m->tongue, pink, 0.02f, 0.008f, 0.02f, 0, 0, 0.004f);
  /* eyes: set wide in the flat face under the brow, dark brown, wet; lids
   * that droop a little (the upper one blinks and squints) */
  t3_geometry *eg = t3_sphere_geometry_new(1, 24, 18);
  t3_geometry *upper = t3_sphere_geometry_new_ex(1, 24, 10, 0, 6.2831853f, 0, 1.5707963f);
  t3_geometry *lower = t3_sphere_geometry_new_ex(1, 24, 10, 0, 6.2831853f, 1.5707963f, 1.5707963f);
  for (int sd = -1; sd <= 1; sd += 2) {
    float ex = sd * 0.078f, ey = 0.035f, ez = 0.112f;
    t3_object *ep = pivot(h, ex, ey, ez);
    t3_object_set_rotation(ep, 0, sd * 0.38f, 0); /* they look out a little */
    t3_mesh *wc = part(ep, eg, white, 0, -0.002f, 0.006f);
    wc->base.scale = (t3_vec3){ 0.036f, 0.026f, 0.028f };
    t3_mesh *e = part(ep, eg, eye, 0, 0, 0.01f);
    e->base.scale = (t3_vec3){ 0.03f, 0.03f, 0.028f };
    t3_mesh *pu = part(ep, eg, pupil, 0, 0, 0.0365f);
    pu->base.scale = (t3_vec3){ 0.013f, 0.013f, 0.002f };
    t3_mesh *g = part(ep, eg, glint, 0.009f, 0.01f, 0.036f);
    g->base.scale = (t3_vec3){ 0.0055f, 0.0045f, 0.002f };
    g->base.cast_shadow = false;
    t3_object *ul = pivot(ep, 0, 0, 0.01f);
    t3_mesh *lu = part(ul, upper, mask, 0, 0, 0);
    lu->base.scale = (t3_vec3){ 0.034f, 0.033f, 0.032f };
    m->lids[sd > 0] = ul;
    t3_mesh *ll = part(ep, lower, mask, 0, 0, 0.01f);
    ll->base.scale = (t3_vec3){ 0.033f, 0.031f, 0.031f };
    t3_object_set_rotation(&ll->base, 0.62f, 0, 0);
  }
  t3_release(eg);
  t3_release(upper);
  t3_release(lower);
  /* rose ears: thin, set at the skull's top corners; the flap folds back on
   * itself so the fold shows, the tip drops forward */
  for (int sd = -1; sd <= 1; sd += 2) {
    t3_object *e = pivot(h, sd * 0.13f, 0.125f, -0.01f);
    m->ear[sd > 0] = e;
    t3_mesh *base = blob(e, ear_m, 0.044f, 0.03f, 0.006f, sd * 0.016f, -0.016f, -0.006f);
    t3_object_set_rotation(&base->base, 0, sd * 1.25f, sd * 0.25f);
    t3_mesh *flap = blob(e, ear_m, 0.036f, 0.026f, 0.006f, sd * 0.026f, -0.014f, 0.02f);
    t3_object_set_rotation(&flap->base, -0.5f, sd * 0.95f, sd * 0.85f);
    t3_mesh *crease = blob(e, dark, 0.032f, 0.004f, 0.005f, sd * 0.021f, 0.0f, 0.008f);
    t3_object_set_rotation(&crease->base, 0, sd * 1.1f, sd * 0.2f);
  }
  build_hat(m, look.hat, tc);
  if (m->hat) {
    m->hat->position = (t3_vec3){ 0, 0.16f, 0.0f };
    m->hat->scale = (t3_vec3){ 0.78f, 0.78f, 0.78f };
  }
  t3_release(skin);
  t3_release(chest);
  t3_release(head_m);
  t3_release(mask);
  t3_release(ear_m);
  t3_release(nose);
  t3_release(dark);
  t3_release(eye);
  t3_release(pupil);
  t3_release(white);
  t3_release(pink);
  t3_release(nail);
  t3_release(lacquer);
  t3_release(steel);
  /* glint and flame_mat stay referenced by the meshes (the model reads them) */
}

/* ── the cat ────────────────────────────────────────────────────────── */

void cat_build(cat_model *c) {
  memset(c, 0, sizeof *c);
  c->root = t3_group_new();
  c->body = pivot(c->root, 0, 0, 0);
  t3_material *tabby = phys(lin(1, 1, 1), 0.85f, 0);
  tex_apply_uv(tabby, TEX_TABBY, 1, 1, 0.5f, 1);
  tabby->sheen = 1;
  tabby->sheen_color = lin(1, 0.85f, 0.65f);
  tabby->sheen_roughness = 0.5f;
  t3_material *cream = phys(lin(1, 0.93f, 0.82f), 0.85f, 0);
  tex_apply(cream, TEX_FUR, 2, 0.4f, 1);
  cream->sheen = 1;
  cream->sheen_color = lin(1, 1, 0.95f);
  cream->sheen_roughness = 0.5f;
  t3_material *pink = phys(lin(1, 0.55f, 0.6f), 0.4f, 0);
  pink->clearcoat = 0.5f;
  t3_material *eye = phys(lin(0.72f, 0.85f, 0.25f), 0.05f, 0);
  eye->clearcoat = 1;
  eye->emissive = lin(0.6f, 0.75f, 0.15f);
  eye->emissive_intensity = 0.25f;
  t3_material *black = phys(lin(0.02f, 0.02f, 0.02f), 0.1f, 0);
  black->clearcoat = 1;
  t3_material *glint = emissive(1, 1, 1, 3);
  t3_material *white = phys(lin(1, 1, 1), 0.5f, 0);
  t3_material *dark = phys(lin(0.25f, 0.1f, 0.08f), 0.6f, 0);
  t3_material *mouth = phys(lin(0.45f, 0.08f, 0.1f), 0.5f, 0);

  /* the chonk: very nearly the physics sphere */
  t3_geometry *bodyg = t3_sphere_geometry_new(RB * 0.98f, 64, 40);
  t3_mesh *bm = part(c->body, bodyg, tabby, 0, 0, 0);
  rot(&bm->base, 1.5707963f, 0, 0); /* the stripes wrap round its middle */
  t3_release(bodyg);
  blob(c->body, cream, RB * 0.72f, RB * 0.6f, RB * 0.55f, 0, -RB * 0.42f, RB * 0.38f); /* belly */
  /* tucked paws peeping out underneath, front pair forward */
  for (int k = 0; k < 4; k++) {
    float sx = k & 1 ? 1.0f : -1.0f, front = k < 2 ? 1.0f : -1.0f;
    c->paw[k] = pivot(c->body, sx * RB * 0.42f, -RB * 0.82f, front * RB * 0.45f);
    blob(c->paw[k], cream, 0.24f, 0.17f, 0.3f, 0, 0, front * 0.06f);
    for (int t = -1; t <= 1; t++) blob(c->paw[k], pink, 0.045f, 0.03f, 0.04f, t * 0.08f, -0.14f, front * 0.2f);
  }
  /* the tail: a striped arc wrapped round its side (it splays in the air) */
  c->tail = pivot(c->body, -RB * 0.55f, -RB * 0.55f, -RB * 0.7f);
  t3_geometry *tg = t3_torus_geometry_new(RB * 0.85f, 0.17f, 12, 36, 2.6f);
  t3_mesh *tm = part(c->tail, tg, tabby, RB * 0.55f, 0, RB * 0.15f);
  rot(&tm->base, 1.5707963f, 0, 2.2f);
  t3_release(tg);
  /* the head: big, round, bulging out of the front */
  c->head = pivot(c->body, 0, RB * 0.38f, RB * 0.6f);
  t3_object *h = c->head;
  blob(h, tabby, 0.66f, 0.56f, 0.55f, 0, 0, 0);
  blob(h, cream, 0.36f, 0.24f, 0.2f, 0, -0.18f, 0.42f); /* muzzle */
  blob(h, pink, 0.06f, 0.045f, 0.04f, 0, -0.06f, 0.6f);  /* nose */
  /* ears: up, with pink insides */
  t3_geometry *ear = t3_cone_geometry_new(0.2f, 0.36f, 4, 1, false);
  for (int s = -1; s <= 1; s += 2) {
    t3_object *e = pivot(h, s * 0.36f, 0.4f, -0.02f);
    c->ear[s > 0] = e;
    rot(e, 0, 0, -s * 0.35f);
    t3_mesh *o = part(e, ear, tabby, 0, 0.12f, 0);
    o->base.scale = (t3_vec3){ 1, 1, 0.55f };
    rot(&o->base, 0, 0.785f, 0);
    t3_mesh *in = part(e, ear, pink, 0, 0.1f, 0.05f);
    in->base.scale = (t3_vec3){ 0.6f, 0.7f, 0.25f };
    rot(&in->base, 0, 0.785f, 0);
  }
  t3_release(ear);
  /* eyes: open (half-lidded, unimpressed), angry (wide, tiny pupils), dizzy (spirals) */
  c->eyes_open = pivot(h, 0, 0, 0);
  c->eyes_angry = pivot(h, 0, 0, 0);
  c->eyes_dizzy = pivot(h, 0, 0, 0);
  t3_geometry *spiral = t3_torus_geometry_new(0.07f, 0.014f, 6, 24, 9.0f);
  for (int s = -1; s <= 1; s += 2) {
    float ex = s * 0.24f, ey = 0.1f, ez = 0.44f;
    blob(c->eyes_open, eye, 0.13f, 0.12f, 0.08f, ex, ey, ez);
    blob(c->eyes_open, black, 0.03f, 0.1f, 0.03f, ex, ey, ez + 0.07f);
    blob(c->eyes_open, glint, 0.022f, 0.022f, 0.01f, ex + 0.04f, ey + 0.04f, ez + 0.085f);
    t3_mesh *lid = blob(c->eyes_open, tabby, 0.142f, 0.07f, 0.09f, ex, ey + 0.065f, ez + 0.012f);
    rot(&lid->base, 0, 0, s * 0.18f);
    blob(c->eyes_angry, eye, 0.15f, 0.15f, 0.09f, ex, ey + 0.01f, ez);
    blob(c->eyes_angry, black, 0.022f, 0.06f, 0.02f, ex, ey, ez + 0.085f);
    blob(c->eyes_angry, glint, 0.02f, 0.02f, 0.01f, ex + 0.05f, ey + 0.05f, ez + 0.09f);
    t3_geometry *brow = geo_rounded_box(0.22f, 0.05f, 0.06f, 0.02f, 2);
    t3_mesh *bw = part(c->eyes_angry, brow, dark, ex, ey + 0.18f, ez + 0.03f);
    rot(&bw->base, 0, 0, s * 0.45f);
    t3_release(brow);
    blob(c->eyes_dizzy, white, 0.13f, 0.13f, 0.07f, ex, ey, ez);
    t3_mesh *sp = part(c->eyes_dizzy, spiral, black, ex, ey, ez + 0.07f);
    sp->base.cast_shadow = false;
  }
  t3_release(spiral);
  c->eyes_angry->visible = c->eyes_dizzy->visible = false;
  /* mouths: a small grumpy frown, or an open yowl */
  c->mouth_grump = pivot(h, 0, -0.27f, 0.52f);
  t3_geometry *frown = t3_torus_geometry_new(0.07f, 0.014f, 6, 16, 3.1415927f);
  part(c->mouth_grump, frown, dark, 0, 0, 0);
  t3_release(frown);
  c->mouth_yowl = pivot(h, 0, -0.28f, 0.5f);
  blob(c->mouth_yowl, mouth, 0.1f, 0.11f, 0.06f, 0, 0, 0);
  blob(c->mouth_yowl, pink, 0.06f, 0.03f, 0.04f, 0, -0.06f, 0.02f);
  t3_geometry *fang = t3_cone_geometry_new(0.018f, 0.05f, 6, 1, false);
  for (int s = -1; s <= 1; s += 2) {
    t3_mesh *fm = part(c->mouth_yowl, fang, white, s * 0.05f, 0.08f, 0.04f);
    rot(&fm->base, 3.1415927f, 0, 0);
  }
  t3_release(fang);
  c->mouth_yowl->visible = false;
  /* whiskers */
  c->whisk = pivot(h, 0, -0.16f, 0.5f);
  t3_geometry *wg = t3_cylinder_geometry_new(0.006f, 0.004f, 0.55f, 4, 1, false);
  for (int s = -1; s <= 1; s += 2)
    for (int k = 0; k < 3; k++) {
      t3_mesh *w = part(c->whisk, wg, white, s * 0.3f, -0.02f + k * 0.035f, 0);
      rot(&w->base, 0, 0, s * (1.5707963f + (k - 1) * 0.15f));
      w->base.cast_shadow = false;
    }
  t3_release(wg);
  t3_release(tabby);
  t3_release(cream);
  t3_release(pink);
  t3_release(eye);
  t3_release(black);
  t3_release(glint);
  t3_release(white);
  t3_release(dark);
  t3_release(mouth);
}
