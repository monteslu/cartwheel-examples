/* chromite models: every craft is built here from code, in the original's
 * shapes. Smooth chrome volumes (lathes, ellipsoids, tori) catch the
 * environment the way the 2000 renders did, rust blotches the enemies, and
 * the glowing rings and engines are emissive so the bloom picks them up.
 *
 *   hero      a silver delta: a needle nose, a blue spine canopy, swept
 *             wings with sawtooth trailing edges, two long cannon pods and
 *             two engine nacelles
 *   straight  an egg-shaped pod with a red-lit belly ring and a snout gun
 *   omni      a spinning chrome disc with a black inner ring
 *   raygun    four teal glass bubbles in a chrome clover, a red seam, a
 *             nose emitter
 *   tank      a squat armoured block, red-ringed turret dome, track pods
 *   gnat      a small magenta-rimmed disc
 *   boss0     a wide chrome saucer with spire turrets and a red ring
 *   boss1     a wider saucer with a raked gun boom
 *
 * Craft face -Z (screen up for the hero; enemies are turned to face +Z).
 * Geometry and materials are shared by every craft of a kind, so three.c
 * draws a wave of one kind as one instanced batch. */
#include <stdlib.h>
#include <string.h>

#include "game.h"

uint32_t model_hash = CW_HASH32_INIT;

/* ── materials ─────────────────────────────────────────────────────── */

static t3_material *MAT[MATS_N];

static t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}
static t3_material *metal(int kind, float ru, float rv, t3_color tint) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  tex_apply(m, kind, ru, rv);
  m->color = tint;
  t3_material_set_texture(m, T3_ENV_MAP, env_texture());
  m->env_map_intensity = 1;
  return m;
}
static t3_material *glow(t3_color c, float k) {
  t3_material *m = t3_mesh_standard_material_new(0x000000);
  m->color = (t3_color){ 0, 0, 0 };
  m->emissive = c;
  m->emissive_intensity = k;
  m->roughness = 0.5f;
  m->metalness = 0;
  return m;
}

t3_material *model_mat(int which) { return MAT[which]; }

static void mats_init(void) {
  if (MAT[M_CHROME]) return;
  MAT[M_CHROME] = metal(MT_CHROME, 1, 1, (t3_color){ 0.95f, 0.95f, 0.97f });
  MAT[M_LILAC] = metal(MT_CHROME, 1, 1, lin(0.82f, 0.82f, 0.98f));
  MAT[M_LILAC]->metalness = 0.72f; /* silver paint over metal: it takes the key light too */
  MAT[M_LILAC]->roughness = 1.6f;   /* (x the map: about 0.2) */
  MAT[M_RUST] = metal(MT_RUST, 1, 1, (t3_color){ 1, 1, 1 });
  MAT[M_RUST_DARK] = metal(MT_RUST, 2, 2, lin(0.62f, 0.6f, 0.6f));
  /* the hero's spine: deep blue lacquer over metal, clear coated */
  t3_material *b = t3_mesh_physical_material_new(0xffffff);
  b->color = lin(0.06f, 0.12f, 0.85f);
  b->metalness = 0.6f;
  b->roughness = 0.25f;
  b->clearcoat = 1;
  b->clearcoat_roughness = 0.05f;
  t3_material_set_texture(b, T3_ENV_MAP, env_texture());
  MAT[M_BLUE] = b;
  /* teal glass bubbles */
  t3_material *g = t3_mesh_physical_material_new(0xffffff);
  g->color = lin(0.05f, 0.5f, 0.45f);
  g->metalness = 0.2f;
  g->roughness = 0.08f;
  g->clearcoat = 1;
  g->clearcoat_roughness = 0.02f;
  g->emissive = lin(0.0f, 0.12f, 0.1f);
  g->emissive_intensity = 1;
  t3_material_set_texture(g, T3_ENV_MAP, env_texture());
  MAT[M_TEAL] = g;
  t3_material *k = t3_mesh_physical_material_new(0xffffff);
  k->color = lin(0.03f, 0.03f, 0.035f);
  k->metalness = 0.3f;
  k->roughness = 0.35f;
  t3_material_set_texture(k, T3_ENV_MAP, env_texture());
  MAT[M_BLACK] = k;
  MAT[M_RED_GLOW] = glow(lin(1, 0.08f, 0.04f), 6);
  MAT[M_PINK_GLOW] = glow(lin(1, 0.2f, 0.9f), 5);
  MAT[M_ENGINE] = glow(lin(0.55f, 0.75f, 1), 9);
  MAT[M_AMBER] = glow(lin(1, 0.55f, 0.15f), 6);
  MAT[M_GREEN_GLOW] = glow(lin(0.2f, 1, 0.35f), 5);
  /* the canopy: dark tinted glass, glossy */
  t3_material *cp = t3_mesh_physical_material_new(0xffffff);
  cp->color = lin(0.04f, 0.07f, 0.1f);
  cp->metalness = 0.4f;
  cp->roughness = 0.04f;
  cp->clearcoat = 1;
  cp->clearcoat_roughness = 0.0f;
  cp->emissive = lin(0.05f, 0.25f, 0.4f);
  cp->emissive_intensity = 0.6f;
  t3_material_set_texture(cp, T3_ENV_MAP, env_texture());
  MAT[M_CANOPY] = cp;
  /* engine flame: additive, unlit, a hot blue-white core */
  t3_material *fl = t3_mesh_basic_material_new(0xffffff);
  fl->color = (t3_color){ 1.6f, 2.4f, 5.5f };
  fl->blending = T3_ADDITIVE_BLENDING;
  fl->transparent = true;
  fl->depth_write = false;
  MAT[M_FLAME] = fl;
}

/* ── geometry helpers ──────────────────────────────────────────────── */

static t3_geometry *track(t3_geometry *g) {
  const t3_attribute *p = g->attributes[T3_ATTR_POSITION];
  if (p) model_hash = cw_hash32(p->array, sizeof(float) * (size_t)p->count * (size_t)p->item_size, model_hash);
  return g;
}

/* a body of revolution along Z: profile points (radius, z), nose first */
static t3_geometry *lathe_z(const float *rz, int n, int seg) {
  t3_vec2 pts[32];
  for (int i = 0; i < n && i < 32; i++) pts[i] = (t3_vec2){ rz[i * 2], rz[i * 2 + 1] };
  t3_geometry *g = t3_lathe_geometry_new(pts, n, seg, 0, 6.2831853f);
  /* lathe revolves round Y with the profile's y up: turn Y into Z */
  t3_geometry_rotate_x(g, 1.5707963f);
  return track(g);
}
static t3_geometry *ellipsoid(float rx, float ry, float rz, int seg) {
  t3_geometry *g = t3_sphere_geometry_new(1, seg, seg * 2 / 3);
  t3_geometry_scale(g, rx, ry, rz);
  return track(g);
}
static t3_geometry *ring(float radius, float tube, int rs, int ts) {
  t3_geometry *g = t3_torus_geometry_new(radius, tube, rs, ts, 6.2831853f);
  t3_geometry_rotate_x(g, 1.5707963f); /* lie flat in XZ */
  return track(g);
}

/* a flat slab from an XZ outline (counter-clockwise seen from above),
 * thickness th, with flat facets: wings and fins */
typedef struct mb { float *p, *n; int count, cap; } mb;
static void mb_tri(mb *b, t3_vec3 a, t3_vec3 c, t3_vec3 d) {
  if (b->count + 3 > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 192;
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cap);
    b->n = realloc(b->n, sizeof(float) * 3 * (size_t)b->cap);
  }
  t3_vec3 e1 = { c.x - a.x, c.y - a.y, c.z - a.z }, e2 = { d.x - a.x, d.y - a.y, d.z - a.z };
  t3_vec3 nn = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
  float l = sqrtf(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
  if (l > 0) nn.x /= l, nn.y /= l, nn.z /= l;
  t3_vec3 v[3] = { a, c, d };
  for (int i = 0; i < 3; i++) {
    int k = b->count++;
    b->p[k * 3] = v[i].x, b->p[k * 3 + 1] = v[i].y, b->p[k * 3 + 2] = v[i].z;
    b->n[k * 3] = nn.x, b->n[k * 3 + 1] = nn.y, b->n[k * 3 + 2] = nn.z;
  }
}
static t3_geometry *mb_finish(mb *b) {
  t3_geometry *g = t3_geometry_new();
  float *uv = calloc((size_t)b->count * 2, sizeof(float));
  for (int i = 0; i < b->count; i++) uv[i * 2] = b->p[i * 3] * 0.5f, uv[i * 2 + 1] = b->p[i * 3 + 2] * 0.5f;
  t3_attribute *p = t3_attribute_new(T3_FLOAT32, b->p, b->count, 3), *n = t3_attribute_new(T3_FLOAT32, b->n, b->count, 3),
               *u = t3_attribute_new(T3_FLOAT32, uv, b->count, 2);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, p);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, n);
  t3_geometry_set_attribute(g, T3_ATTR_UV, u);
  t3_release(p), t3_release(n), t3_release(u);
  free(b->p), free(b->n), free(uv);
  memset(b, 0, sizeof *b);
  return track(g);
}
/* the slab: a top and bottom cap (fanned from the centroid, with the top
 * tented up by `crown` at the centroid so the facets catch the light) and
 * the side walls */
static t3_geometry *slab(const float *xz, int n, float th, float crown) {
  mb b = { 0 };
  float cx = 0, cz = 0;
  for (int i = 0; i < n; i++) cx += xz[i * 2], cz += xz[i * 2 + 1];
  cx /= n, cz /= n;
  t3_vec3 top = { cx, th + crown, cz }, bot = { cx, -th - crown * 0.5f, cz };
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    t3_vec3 a = { xz[i * 2], th, xz[i * 2 + 1] }, c = { xz[j * 2], th, xz[j * 2 + 1] };
    t3_vec3 a2 = { xz[i * 2], -th, xz[i * 2 + 1] }, c2 = { xz[j * 2], -th, xz[j * 2 + 1] };
    mb_tri(&b, top, c, a);
    mb_tri(&b, bot, a2, c2);
    mb_tri(&b, a, c, c2);
    mb_tri(&b, a, c2, a2);
  }
  return mb_finish(&b);
}

/* ── part placement ────────────────────────────────────────────────── */

static t3_mesh *part(t3_object *parent, t3_geometry *g, int mat, float x, float y, float z) {
  t3_mesh *m = t3_mesh_new(g, MAT[mat]);
  t3_object_set_position(m, x, y, z);
  T3_OBJ(m)->cast_shadow = mat != M_RED_GLOW && mat != M_PINK_GLOW && mat != M_ENGINE && mat != M_AMBER && mat != M_GREEN_GLOW && mat != M_FLAME;
  T3_OBJ(m)->receive_shadow = false;
  t3_object_add(parent, m);
  t3_release(m);
  return m;
}

/* every kind's parts, built once; a craft is a group of meshes on them */
typedef struct piece { t3_geometry *g; int mat; float x, y, z, rx, ry, rz, sx, sy, sz; int spin; } piece;
#define MAX_PIECES 40
typedef struct kind_def { piece p[MAX_PIECES]; int n; float radius; } kind_def;
static kind_def KINDS[CRAFT_KINDS];

static piece *add(kind_def *k, t3_geometry *g, int mat, float x, float y, float z) {
  piece *p = &k->p[k->n++];
  memset(p, 0, sizeof *p);
  p->g = g, p->mat = mat, p->x = x, p->y = y, p->z = z, p->sx = p->sy = p->sz = 1;
  return p;
}
/* the same geometry mirrored across X (it is shared: the mesh is scaled -1) */
static void add_pair(kind_def *k, t3_geometry *g, int mat, float x, float y, float z) {
  add(k, g, mat, x, y, z);
  t3_retain(g);
  piece *p = add(k, g, mat, -x, y, z);
  p->sx = -1;
}

static void build_hero(kind_def *k) {
  /* fuselage: needle nose to a rounded tail, along -Z */
  static const float BODY[] = { 0, -1.25f, 0.05f, -1.1f, 0.13f, -0.75f, 0.2f, -0.3f, 0.24f, 0.2f, 0.22f, 0.6f, 0.15f, 0.82f, 0, 0.88f };
  add(k, lathe_z(BODY, 8, 24), M_LILAC, 0, 0, 0);
  /* the blue spine: a long canopy ridge along the top */
  add(k, ellipsoid(0.1f, 0.12f, 0.75f, 20), M_BLUE, 0, 0.13f, -0.15f);
  /* wings: a swept delta with a sawtooth trailing edge */
  static const float WING[] = { 0.15f, -0.35f, 0.15f, 0.55f, 0.55f, 0.62f, 0.6f, 0.48f, 0.82f, 0.6f, 0.86f, 0.46f, 1.02f, 0.58f, 1.0f, 0.42f };
  add_pair(k, slab(WING, 8, 0.025f, 0.05f), M_LILAC, 0, -0.02f, 0);
  /* cannon pods: long tubes with cone tips, inboard of the wing roots */
  static const float POD[] = { 0, -0.95f, 0.035f, -0.85f, 0.06f, -0.6f, 0.07f, 0.3f, 0.06f, 0.55f, 0, 0.6f };
  add_pair(k, lathe_z(POD, 6, 16), M_CHROME, 0.3f, 0.02f, 0);
  /* engine nacelles at the tail, each with a hot nozzle */
  static const float NAC[] = { 0, -0.2f, 0.07f, -0.1f, 0.1f, 0.25f, 0.09f, 0.72f, 0.06f, 0.78f };
  add_pair(k, lathe_z(NAC, 5, 18), M_CHROME, 0.16f, -0.04f, 0.15f);
  add_pair(k, ellipsoid(0.055f, 0.055f, 0.03f, 12), M_ENGINE, 0.16f, -0.04f, 0.94f);
  /* a fin under the tail and outrigger fins on the pods */
  static const float FIN[] = { 0, 0.2f, 0, 0.75f, 0.02f, 0.75f, 0.02f, 0.3f };
  piece *f = add(k, slab(FIN, 4, 0.012f, 0), M_CHROME, -0.01f, 0.05f, 0);
  f->rz = 1.5707963f, f->sy = 1.6f;
  /* the canopy at the front of the spine, wingtip lights (red port, green
   * starboard), strakes on the leading edge roots, flames behind the nozzles
   * (world.c flickers them: spin flag 2) */
  add(k, ellipsoid(0.085f, 0.075f, 0.24f, 20), M_CANOPY, 0, 0.17f, -0.55f);
  add(k, ellipsoid(0.03f, 0.025f, 0.05f, 10), M_GREEN_GLOW, 1.0f, -0.0f, 0.5f);
  add(k, ellipsoid(0.03f, 0.025f, 0.05f, 10), M_RED_GLOW, -1.0f, -0.0f, 0.5f);
  static const float STRAKE[] = { 0.12f, -0.75f, 0.2f, -0.35f, 0.18f, -0.3f };
  add_pair(k, slab(STRAKE, 3, 0.02f, 0.02f), M_CHROME, 0, 0, 0);
  static const float FLAME[] = { 0, 0, 0.05f, 0.05f, 0.035f, 0.35f, 0, 0.75f };
  piece *fa = add(k, lathe_z(FLAME, 4, 12), M_FLAME, 0.16f, -0.04f, 0.93f);
  fa->spin = 2;
  t3_retain(fa->g);
  piece *fb = add(k, fa->g, M_FLAME, -0.16f, -0.04f, 0.93f);
  fb->spin = 2;
  k->radius = 1.05f;
}

/* an enemy egg: a chrome pod, tusks, a red belly ring, a snout gun */
static void build_straight(kind_def *k) {
  add(k, ellipsoid(0.8f, 0.45f, 0.75f, 28), M_RUST, 0, 0, -0.1f);
  /* the twin rear lobes (the egg's "cheeks") */
  add_pair(k, ellipsoid(0.42f, 0.4f, 0.45f, 20), M_RUST, 0.42f, 0.05f, -0.45f);
  add(k, ring(0.62f, 0.06f, 32, 10), M_RED_GLOW, 0, -0.05f, 0.1f)->sz = 0.7f;
  static const float GUN[] = { 0, 0.85f, 0.06f, 0.82f, 0.09f, 0.6f, 0.11f, 0.1f, 0.0f, 0.0f };
  add(k, lathe_z(GUN, 5, 14), M_CHROME, 0, 0.18f, 0.2f);
  k->radius = 0.9f;
}

static void build_omni(kind_def *k) {
  static const float DISC[] = { 0, -0.32f, 0.5f, -0.25f, 0.75f, -0.1f, 0.8f, 0.0f, 0.75f, 0.1f, 0.5f, 0.22f, 0, 0.26f };
  piece *d = add(k, lathe_z(DISC, 7, 32), M_RUST, 0, 0, 0);
  d->rx = -1.5707963f; /* the lathe's axis up: a disc lying flat */
  d->spin = 1;
  piece *r = add(k, ring(0.56f, 0.07f, 32, 10), M_BLACK, 0, 0.2f, 0);
  r->spin = 1;
  add(k, ellipsoid(0.18f, 0.12f, 0.18f, 16), M_RED_GLOW, 0, 0.18f, 0);
  k->radius = 0.82f;
}

static void build_raygun(kind_def *k) {
  /* four bubbles in a clover, chrome lobes between them */
  for (int i = 0; i < 4; i++) {
    float a = 0.785398f + i * 1.5707963f, x = 0.42f * cw_cos(a), z = 0.42f * cw_sin(a);
    add(k, ellipsoid(0.36f, 0.32f, 0.36f, 20), M_TEAL, x, 0.05f, z);
  }
  add(k, ellipsoid(0.85f, 0.25f, 0.85f, 28), M_RUST, 0, -0.1f, 0);
  add(k, ring(0.68f, 0.035f, 40, 8), M_RED_GLOW, 0, 0.08f, 0);
  static const float SPINE[] = { 0, -0.7f, 0.12f, -0.55f, 0.16f, 0.2f, 0.1f, 0.85f, 0, 0.95f };
  add(k, lathe_z(SPINE, 5, 16), M_CHROME, 0, 0.25f, 0);
  static const float EMIT[] = { 0, 0.85f, 0.1f, 0.95f, 0.12f, 1.15f, 0, 1.25f };
  add(k, lathe_z(EMIT, 4, 16), M_TEAL, 0, 0.1f, 0);
  k->radius = 0.95f;
}

static void build_tank(kind_def *k) {
  t3_geometry *hull = track(t3_box_geometry_new(1.7f, 0.5f, 1.9f, 1, 1, 1));
  add(k, hull, M_RUST_DARK, 0, -0.1f, 0);
  t3_geometry *deck = track(t3_box_geometry_new(1.2f, 0.3f, 1.3f, 1, 1, 1));
  add(k, deck, M_RUST, 0, 0.25f, -0.05f);
  /* track pods down both sides, round-ended */
  static const float PODP[] = { 0, -1.1f, 0.22f, -1.0f, 0.3f, -0.6f, 0.3f, 0.7f, 0.22f, 1.0f, 0, 1.1f };
  add_pair(k, lathe_z(PODP, 6, 18), M_RUST, 0.95f, -0.05f, 0);
  /* the turret: a dome in a red ring, a short double barrel */
  add(k, ellipsoid(0.42f, 0.32f, 0.42f, 24), M_CHROME, 0, 0.42f, 0.2f);
  add(k, ring(0.46f, 0.05f, 32, 8), M_RED_GLOW, 0, 0.42f, 0.2f);
  static const float BAR[] = { 0, 0.95f, 0.05f, 0.92f, 0.06f, 0.2f, 0, 0.15f };
  add_pair(k, lathe_z(BAR, 4, 10), M_CHROME, 0.1f, 0.5f, 0.3f);
  /* corner spires */
  static const float SP[] = { 0, -0.45f, 0.1f, -0.3f, 0.12f, 0.2f, 0, 0.25f };
  for (int s = -1; s <= 1; s += 2) {
    piece *p = add(k, lathe_z(SP, 4, 12), M_CHROME, 0.72f * s, 0.3f, -0.9f);
    p->rx = -1.5707963f;
    p = add(k, lathe_z(SP, 4, 12), M_CHROME, 0.72f * s, 0.3f, 0.9f);
    p->rx = -1.5707963f;
  }
  add_pair(k, track(t3_box_geometry_new(0.08f, 0.04f, 1.1f, 1, 1, 1)), M_PINK_GLOW, 0.62f, 0.15f, 0);
  k->radius = 1.15f;
}

static void build_gnat(kind_def *k) {
  static const float DISC[] = { 0, -0.18f, 0.3f, -0.15f, 0.42f, -0.05f, 0.44f, 0.02f, 0.38f, 0.1f, 0, 0.16f };
  piece *d = add(k, lathe_z(DISC, 6, 24), M_CHROME, 0, 0, 0);
  d->rx = -1.5707963f;
  piece *r = add(k, ring(0.38f, 0.045f, 28, 8), M_PINK_GLOW, 0, 0.06f, 0);
  r->spin = 1;
  k->radius = 0.45f;
}

/* bosses: a wide saucer with turrets on the rim */
static void build_boss(kind_def *k, bool second) {
  float W = second ? 2.6f : 2.3f;
  static const float SAUCER[] = { 0, -0.55f, 0.9f, -0.5f, 1.0f, -0.3f, 1.0f, 0.0f, 0.95f, 0.2f, 0.6f, 0.4f, 0, 0.45f };
  piece *s = add(k, lathe_z(SAUCER, 7, 40), M_RUST, 0, 0, 0);
  s->rx = -1.5707963f, s->sx = W, s->sz = W * 0.55f, s->sy = 1;
  /* lobes along the rim */
  for (int i = 0; i < 6; i++) {
    float a = (i + 0.5f) * 1.0471976f, x = W * 0.78f * cw_cos(a), z = W * 0.43f * cw_sin(a);
    add(k, ellipsoid(0.45f, 0.35f, 0.4f, 18), M_RUST_DARK, x, 0.05f, z);
  }
  add(k, ellipsoid(0.75f, 0.35f, 0.55f, 24), M_CHROME, 0, 0.3f, 0.1f);
  piece *r = add(k, ring(0.8f, 0.06f, 40, 10), M_RED_GLOW, 0, 0.35f, 0.1f);
  r->sz = 0.75f;
  /* spire turrets */
  static const float SP[] = { 0, -0.9f, 0.08f, -0.7f, 0.14f, -0.2f, 0.18f, 0.2f, 0, 0.3f };
  for (int s2 = -1; s2 <= 1; s2 += 2) {
    piece *p = add(k, lathe_z(SP, 5, 14), M_CHROME, s2 * W * 0.7f, 0.4f, -0.2f);
    p->rx = -1.2f;
    p = add(k, lathe_z(SP, 5, 14), M_CHROME, s2 * W * 0.35f, 0.4f, -0.4f);
    p->rx = -1.3f, p->sx = p->sy = p->sz = 0.75f;
  }
  static const float GUN[] = { 0, 1.2f, 0.08f, 1.15f, 0.12f, 0.5f, 0.2f, 0.2f, 0, 0.1f };
  if (second) {
    /* the raked boom gun off one side */
    piece *b = add(k, lathe_z(GUN, 5, 16), M_CHROME, 0.9f, 0.45f, 0.4f);
    b->ry = -0.6f, b->sx = b->sy = 1.4f, b->sz = 1.6f;
    add(k, track(t3_box_geometry_new(0.1f, 0.05f, 1.3f, 1, 1, 1)), M_AMBER, 0, 0.4f, -0.7f);
  } else {
    add_pair(k, lathe_z(GUN, 5, 16), M_CHROME, 0.45f, 0.2f, 0.6f);
  }
  add_pair(k, ellipsoid(0.12f, 0.08f, 0.12f, 12), M_RED_GLOW, W * 0.55f, 0.3f, 0.55f);
  k->radius = W;
}

void models_init(void) {
  mats_init();
  if (KINDS[CK_HERO].n) return;
  build_hero(&KINDS[CK_HERO]);
  build_straight(&KINDS[CK_STRAIGHT]);
  build_omni(&KINDS[CK_OMNI]);
  build_raygun(&KINDS[CK_RAYGUN]);
  build_tank(&KINDS[CK_TANK]);
  build_gnat(&KINDS[CK_GNAT]);
  build_boss(&KINDS[CK_BOSS0], false);
  build_boss(&KINDS[CK_BOSS1], true);
}

float model_radius(int kind) { return KINDS[kind].radius; }

/* a new craft of `kind` under parent; spinners (discs, rings) are listed
 * in spin[] so the caller can turn them */
t3_object *model_new(int kind, t3_object *parent, t3_object **spin, int *n_spin) {
  const kind_def *k = &KINDS[kind];
  t3_object *g = t3_group_new();
  if (n_spin) *n_spin = 0;
  for (int i = 0; i < k->n; i++) {
    const piece *p = &k->p[i];
    t3_mesh *m = part(g, t3_retain(p->g), p->mat, p->x, p->y, p->z);
    t3_object_set_rotation(m, p->rx, p->ry, p->rz);
    t3_object_set_scale(m, p->sx, p->sy, p->sz);
    if (p->spin && spin && n_spin && *n_spin < 4) spin[(*n_spin)++] = T3_OBJ(m);
  }
  t3_object_add(parent, g);
  t3_release(g);
  return g;
}

void models_free(void) {
  for (int k = 0; k < CRAFT_KINDS; k++) {
    for (int i = 0; i < KINDS[k].n; i++) t3_release(KINDS[k].p[i].g);
    KINDS[k].n = 0;
  }
  for (int i = 0; i < MATS_N; i++)
    if (MAT[i]) t3_release(MAT[i]), MAT[i] = NULL;
}
