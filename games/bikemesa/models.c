/* BikeMesa models: the bicycle and its rider in three.c, built here in C and
 * driven every frame by bike_pose (the bodies, interpolated). Riding, the 2D
 * world is the z = 0 plane: frame tubes and wheels sit on it, the rider's
 * left limbs a little toward the camera, the right ones away. In a crash
 * every piece takes its 3D pose from the crash world (crash.c).
 *
 *   bike    a folding bike in deep racing-green metallic (flake under a
 *           clear coat): curved hinged main tube, folding rear triangle,
 *           telescoping seat post, tall stem, flat bars, small guards, a
 *           carrier block, 16-inch alloy wheels
 *   rider   tapered limbs (lathe profiles): a black cotton t-shirt, khaki
 *           cargo shorts with pockets, bare arms and shins, short white
 *           socks, black shoes, a black baseball cap (it flies off in a
 *           crash), warm medium-brown skin
 * Render-only: nothing here feeds the simulation. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "game.h"

static t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

typedef struct model_t {
  t3_object *root;
  t3_object *frame, *rear, *front, *rear_guard, *front_guard, *crank, *cap, *neck;
  t3_object *limb[RAG_COUNT];  /* each a group placed by its 2D xform; the mesh inside runs along +x */
  t3_object *limb_r[RAG_COUNT]; /* the right arm / leg copies while riding (same pose, other side) */
  t3_object *foot[2];         /* ankle hinges: shoes turn independently of the shins */
  /* what is drawn: skinned meshes over the parts above (their bones), one per
   * material, and the far look, one for the bike and one for the rider */
  t3_object *near_mesh[24], *far_mesh[2];
  int near_n;
  cw_node node;
} model_t;
static model_t MS[MAX_RIDERS];
static model_t *Mc = MS;
#define M (*Mc)
/* each rider's colours (bike paint, shirt), so split screen tells them apart */
static const float PAINT[MAX_RIDERS][3] = { { 0.05f, 0.34f, 0.16f }, { 0.55f, 0.06f, 0.05f }, { 0.06f, 0.2f, 0.6f }, { 0.75f, 0.55f, 0.05f } };
static const float SHIRT[MAX_RIDERS][3] = { { 0.07f, 0.07f, 0.075f }, { 0.85f, 0.85f, 0.82f }, { 0.75f, 0.42f, 0.1f }, { 0.12f, 0.3f, 0.5f } };
static int rider_ix;
/* each rider's pick: the character (0 the regular rider, 1 the slim one) and the bike's paint (PAINT) */
static int selected_character[MAX_RIDERS], selected_bike[MAX_RIDERS] = { 0, 1, 2, 3 };
static bool slim_rider(void) { return selected_character[rider_ix] == 1; }
void models_set_selection(int idx, int character, int bike) {
  idx = CW_CLAMP(idx, 0, MAX_RIDERS - 1);
  selected_character[idx] = character;
  selected_bike[idx] = CW_CLAMP(bike, 0, MAX_RIDERS - 1);
}

/* frame-local points (bike.c's frame: origin at the bottom bracket). A
 * classic folding bike: 16-inch wheels, one curved main tube from the head
 * tube down to the seat tube with a fold hinge in its middle, a rear triangle
 * that pivots under the seat tube, a long telescoping seat post and a tall
 * folding stem to flat bars. No names, logos or decals. */
static const t3_vec2 BB = { 0, 0 }, RA = { -0.46f, -0.05f }, FA = { 0.56f, -0.05f }, ST = { -0.1f, 0.4f },
                     SADDLE = { -0.2f, 0.76f }, HT_TOP = { 0.42f, 0.48f }, HT_BOT = { 0.47f, 0.3f }, BARS = { 0.42f, 1.0f },
                     PIVOT = { -0.06f, 0.12f };

static t3_mesh *mesh(t3_object *parent, t3_geometry *g, t3_material *m) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = true;
  me->base.receive_shadow = true;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}

/* a tube of radius r from a to b (frame plane, at depth z) */
static void tube(t3_object *parent, t3_geometry *unit_cyl, t3_material *m, t3_vec2 a, t3_vec2 b, float r, float z);
/* a curved tube: a quadratic bend through c, in segments */
static void bent_tube(t3_object *parent, t3_geometry *unit_cyl, t3_material *m, t3_vec2 a, t3_vec2 c, t3_vec2 b, float r, float z, int seg) {
  t3_vec2 prev = a;
  for (int i = 1; i <= seg; i++) {
    float t = (float)i / seg, u = 1 - t;
    t3_vec2 p = { u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y };
    tube(parent, unit_cyl, m, prev, p, r, z);
    prev = p;
  }
}

static void tube(t3_object *parent, t3_geometry *unit_cyl, t3_material *m, t3_vec2 a, t3_vec2 b, float r, float z) {
  float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
  t3_mesh *me = mesh(parent, unit_cyl, m);
  me->base.position = (t3_vec3){ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, z };
  me->base.scale = (t3_vec3){ r, len, r };
  t3_object_set_rotation(&me->base, 0, 0, cw_atan2(dy, dx) - 1.5707963f);
}

/* spokes: thin boxes from the hub flange to the rim, crossing (one geometry) */
static t3_geometry *geo_spokes(float hub_r, float rim_r, int count) {
  int nv = count * 8, ni = count * 36;
  float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *nor = malloc(sizeof(float) * 3 * (size_t)nv);
  uint32_t *idx = malloc(sizeof(uint32_t) * (size_t)ni);
  const float w = 0.0035f;
  for (int s = 0; s < count; s++) {
    float a0 = s * 6.2831853f / count, side = (s & 1) ? 1.0f : -1.0f;
    float a1 = a0 + side * 0.35f; /* tangential lacing */
    float z0 = 0.028f * side, z1 = 0.004f * side;
    t3_vec3 A = { hub_r * cw_cos(a1), hub_r * cw_sin(a1), z0 }, Bv = { rim_r * cw_cos(a0), rim_r * cw_sin(a0), z1 };
    t3_vec3 d = { Bv.x - A.x, Bv.y - A.y, Bv.z - A.z };
    float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    d = (t3_vec3){ d.x / l, d.y / l, d.z / l };
    t3_vec3 u = { -d.y, d.x, 0 }; /* in plane, across */
    float ul = sqrtf(u.x * u.x + u.y * u.y);
    u = (t3_vec3){ u.x / ul, u.y / ul, 0 };
    t3_vec3 v = { 0, 0, 1 };
    t3_vec3 corner[4] = { { u.x * w + v.x * w, u.y * w + v.y * w, v.z * w }, { -u.x * w + v.x * w, -u.y * w + v.y * w, v.z * w },
                          { -u.x * w - v.x * w, -u.y * w - v.y * w, -v.z * w }, { u.x * w - v.x * w, u.y * w - v.y * w, -v.z * w } };
    for (int e = 0; e < 2; e++)
      for (int c = 0; c < 4; c++) {
        t3_vec3 base = e ? Bv : A;
        int vi = s * 8 + e * 4 + c;
        pos[vi * 3] = base.x + corner[c].x;
        pos[vi * 3 + 1] = base.y + corner[c].y;
        pos[vi * 3 + 2] = base.z + corner[c].z;
        float nl = sqrtf(corner[c].x * corner[c].x + corner[c].y * corner[c].y + corner[c].z * corner[c].z);
        nor[vi * 3] = corner[c].x / nl;
        nor[vi * 3 + 1] = corner[c].y / nl;
        nor[vi * 3 + 2] = corner[c].z / nl;
      }
    for (int c = 0; c < 4; c++) {
      int a = s * 8 + c, b = s * 8 + (c + 1) % 4, a2 = a + 4, b2 = b + 4, k = s * 36 + c * 6;
      idx[k] = (uint32_t)a, idx[k + 1] = (uint32_t)b, idx[k + 2] = (uint32_t)b2;
      idx[k + 3] = (uint32_t)a, idx[k + 4] = (uint32_t)b2, idx[k + 5] = (uint32_t)a2;
    }
    /* end caps: two triangles each */
    int k = s * 36 + 24, a = s * 8, b = s * 8 + 4;
    uint32_t caps[12] = { (uint32_t)a, (uint32_t)a + 2, (uint32_t)a + 1, (uint32_t)a, (uint32_t)a + 3, (uint32_t)a + 2,
                          (uint32_t)b, (uint32_t)b + 1, (uint32_t)b + 2, (uint32_t)b, (uint32_t)b + 2, (uint32_t)b + 3 };
    memcpy(idx + k, caps, sizeof caps);
  }
  t3_geometry *g = t3_geometry_new();
  set_attr(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, nv, 3));
  set_attr(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, nor, nv, 3));
  set_index(g, t3_attribute_new(T3_UINT32, idx, ni, 1));
  free(pos);
  free(nor);
  free(idx);
  return g;
}

typedef struct mats {
  t3_material *paint, *alloy, *rubber, *black, *saddle, *shirt, *shorts, *skin, *sock, *shoe, *cap, *guard, *sole, *tread, *worn, *lace;
} mats;

static t3_object *build_wheel(const mats *m, t3_geometry *spokes) {
  t3_object *w = t3_group_new();
  t3_geometry *tyre = t3_torus_geometry_new(0.178f, 0.025f, 14, 40, 6.2831853f);
  t3_geometry *rim = t3_torus_geometry_new(0.155f, 0.009f, 8, 40, 6.2831853f);
  t3_geometry *hub = t3_cylinder_geometry_new(0.022f, 0.022f, 0.08f, 16, 1, false);
  mesh(w, tyre, m->rubber);
  mesh(w, rim, m->alloy);
  t3_mesh *h = mesh(w, hub, m->alloy);
  t3_object_set_rotation(&h->base, 1.5707963f, 0, 0);
  mesh(w, spokes, m->alloy);
  t3_release(tyre);
  t3_release(rim);
  t3_release(hub);
  return w;
}


/* ── shoes: a lofted low-top sneaker (heel at -x, toe at +x, y up) ────── */
static float sh_top(float x) {  /* the upper's height: collar at the ankle, sloping to the toe box */
  if (x < 0.02f) return 0.095f - 0.012f * (x < -0.06f ? (-0.06f - x) / 0.025f : 0);
  float t = cw_clampf((x - 0.02f) / 0.15f, 0, 1);
  return 0.095f - 0.05f * t * (2 - t);
}
static float sh_width(float x) {
  float t = (x + 0.085f) / 0.27f;                                  /* 0 heel .. 1 toe */
  float w = 0.036f + 0.014f * cw_sin(cw_clampf(t * 1.25f, 0, 1) * 1.5707963f);
  float tip = cw_clampf((1 - t) / 0.12f, 0, 1), heel = cw_clampf(t / 0.06f, 0, 1);
  return w * sqrtf(tip) * sqrtf(heel);
}
static float sh_spring(float x) { return x > 0.12f ? (x - 0.12f) * (x - 0.12f) * 4.0f : 0; } /* the toe lifts */
/* a loft between y0(x) and y1(x), width w(x)*wk, as an indexed smooth mesh */
static t3_geometry *loft(float x0, float x1, float (*y0f)(float), float (*y1f)(float), float wk, int S, int R) {
  int nv = (S + 1) * R + 2, ni = S * R * 6 + R * 6;
  float *pos = malloc(sizeof(float) * 3 * (size_t)nv), *uv = malloc(sizeof(float) * 2 * (size_t)nv);
  uint32_t *idx = malloc(sizeof(uint32_t) * (size_t)ni);
  int v = 0, k = 0;
  for (int i = 0; i <= S; i++) {
    float x = x0 + (x1 - x0) * i / S, ya = y0f(x), yb = y1f(x), w = sh_width(x) * wk;
    float yc = (ya + yb) * 0.5f, hh = (yb - ya) * 0.5f;
    for (int j = 0; j < R; j++) {
      float a = j * 6.2831853f / R, sa, ca;
      cw_sincos(a, &sa, &ca);
      /* a superellipse: boxy sides, a rounded top */
      float sz = ca < 0 ? -cw_pow(-ca, 0.6f) : cw_pow(ca, 0.6f), sy = sa < 0 ? -cw_pow(-sa, 0.8f) : cw_pow(sa, 0.8f);
      pos[v * 3] = x, pos[v * 3 + 1] = yc + hh * sy, pos[v * 3 + 2] = w * sz;
      uv[v * 2] = (x - x0) / (x1 - x0) * 3, uv[v * 2 + 1] = (float)j / R;
      v++;
    }
  }
  int heelc = v, toec = v + 1;
  pos[v * 3] = x0, pos[v * 3 + 1] = (y0f(x0) + y1f(x0)) * 0.5f, pos[v * 3 + 2] = 0, uv[v * 2] = uv[v * 2 + 1] = 0, v++;
  pos[v * 3] = x1, pos[v * 3 + 1] = (y0f(x1) + y1f(x1)) * 0.5f, pos[v * 3 + 2] = 0, uv[v * 2] = uv[v * 2 + 1] = 0, v++;
  for (int i = 0; i < S; i++)
    for (int j = 0; j < R; j++) {
      uint32_t a = (uint32_t)(i * R + j), b = (uint32_t)(i * R + (j + 1) % R), c = a + (uint32_t)R, d = b + (uint32_t)R;
      idx[k++] = a, idx[k++] = c, idx[k++] = b, idx[k++] = b, idx[k++] = c, idx[k++] = d;
    }
  for (int j = 0; j < R; j++) {
    idx[k++] = (uint32_t)heelc, idx[k++] = (uint32_t)j, idx[k++] = (uint32_t)((j + 1) % R);
    idx[k++] = (uint32_t)toec, idx[k++] = (uint32_t)(S * R + (j + 1) % R), idx[k++] = (uint32_t)(S * R + j);
  }
  t3_geometry *g = t3_geometry_new();
  set_attr(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, v, 3));
  set_attr(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, uv, v, 2));
  set_index(g, t3_attribute_new(T3_UINT32, idx, k, 1));
  t3_geometry_compute_vertex_normals(g);
  free(pos);
  free(uv);
  free(idx);
  return g;
}
static float y_sole0(float x) { return sh_spring(x); }
static float y_sole1(float x) { return 0.026f + sh_spring(x); }
static float y_tread1(float x) { return 0.008f + sh_spring(x); }
static float y_up0(float x) { return 0.02f + sh_spring(x); }
static float y_up1(float x) { return sh_top(x) + sh_spring(x); }
static float y_toe0(float x) { return 0.022f + sh_spring(x); }
static float y_toe1(float x) { return (x > 0.11f ? sh_top(x) + 0.0015f : 0.023f) + sh_spring(x); }

/* a whole shoe in a group: sole (off-white midsole edge, a dark tread),
 * the black suede upper, a worn lighter toe, laces over the tongue, the
 * collar around the ankle */
static t3_object *shoe_build(t3_material *upper, t3_material *sole, t3_material *tread, t3_material *worn, t3_material *lace) {
  t3_object *g = t3_group_new();
  t3_geometry *gs = loft(-0.088f, 0.188f, y_sole0, y_sole1, 1.08f, 20, 16);
  t3_geometry *gt = loft(-0.086f, 0.186f, y_sole0, y_tread1, 1.1f, 20, 16);
  t3_geometry *gu = loft(-0.085f, 0.185f, y_up0, y_up1, 1.0f, 24, 18);
  t3_geometry *gw = loft(0.1f, 0.184f, y_toe0, y_toe1, 1.02f, 10, 18);
  mesh(g, gs, sole);
  mesh(g, gt, tread);
  mesh(g, gu, upper);
  mesh(g, gw, worn);
  /* laces: four little bars across the tongue */
  t3_geometry *lb = t3_box_geometry_new(0.012f, 0.006f, 0.05f, 1, 1, 1);
  for (int i = 0; i < 4; i++) {
    float x = 0.035f + i * 0.022f;
    t3_mesh *l = mesh(g, lb, lace);
    l->base.position = (t3_vec3){ x, sh_top(x) + sh_spring(x) + 0.004f, 0 };
    t3_object_set_rotation(&l->base, 0, (i & 1) ? 0.35f : -0.35f, -0.25f);
  }
  /* the collar: a padded ring around the ankle opening */
  t3_geometry *col = t3_torus_geometry_new(0.038f, 0.008f, 6, 18, 6.2831853f);
  t3_mesh *cm = mesh(g, col, upper);
  cm->base.position = (t3_vec3){ -0.03f, 0.094f, 0 };
  cm->base.scale = (t3_vec3){ 1.25f, 1, 0.95f };
  t3_object_set_rotation(&cm->base, 1.5707963f, 0, 0);
  t3_geometry *rel[] = { gs, gt, gu, gw, lb, col };
  for (int i = 0; i < 6; i++) t3_release(rel[i]);
  return g;
}

/* A compact closed grip shared by both riders. */
static t3_object *grip_hand_build(t3_material *skin, float side) {
  t3_object *g = t3_group_new();
  t3_geometry *ball = t3_sphere_geometry_new(1, 10, 8);
  t3_mesh *palm = mesh(g, ball, skin);
  palm->base.position = (t3_vec3){ 0.008f, 0.007f, 0 };
  palm->base.scale = (t3_vec3){ 0.046f, 0.025f, 0.047f };
  for (int i = 0; i < 4; i++) {
    t3_mesh *finger = mesh(g, ball, skin);
    finger->base.position = (t3_vec3){ 0.035f, -0.018f, (-0.029f + i * 0.019f) * side };
    finger->base.scale = (t3_vec3){ 0.025f, 0.026f, 0.009f };
  }
  /* Both thumbs lie on the inside edge of their own grip, toward the stem. */
  t3_mesh *thumb = mesh(g, ball, skin);
  thumb->base.position = (t3_vec3){ -0.018f, 0.008f, -0.045f * side };
  thumb->base.scale = (t3_vec3){ 0.03f, 0.015f, 0.018f };
  t3_release(ball);
  return g;
}

/* a limb: a lathe profile along +y, turned to run along the group's +x; the
 * mesh can start and stop part way (a sleeve) */
static t3_mesh *limb_part(t3_object *grp, float len, float r0, float r1, float from, float to, float puff, t3_material *m, float z) {
  enum { K = 9 };
  t3_vec2 prof[K];
  for (int i = 0; i < K; i++) {
    float t = (float)i / (K - 1), at = from + (to - from) * t;
    float r = (r0 + (r1 - r0) * at) * puff;
    float end = i == 0 || i == K - 1 ? (from == 0 && i == 0) || (to == 1 && i == K - 1) ? 0.25f : 0.92f : i == 1 || i == K - 2 ? 0.97f : 1;
    float bulge = 1 + 0.12f * cw_sin(at * 3.1415927f);
    prof[i] = (t3_vec2){ r * end * bulge, -r0 * 0.5f * (from == 0) + (len + r0 * 0.5f + r1 * 0.5f) * at };
  }
  t3_geometry *g = t3_lathe_geometry_new(prof, K, 14, 0, 6.2831853f);
  t3_mesh *me = mesh(grp, g, m);
  me->base.position.z = z;
  t3_object_set_rotation(&me->base, 0, 0, -1.5707963f);
  t3_release(g);
  return me;
}
static void shirt_torso(t3_object *grp, t3_material *m) {
  /* The shirt narrows into an ordinary neck opening, with no raised collar. */
  const t3_vec2 prof[] = { { 0.16f, 0.05f }, { 0.16f, 0.07f }, { 0.155f, 0.12f },
                           { 0.14f, 0.22f }, { 0.14f, 0.42f }, { 0.135f, 0.53f },
                           { 0.105f, 0.565f }, { 0.062f, 0.595f }, { 0.052f, 0.605f } };
  t3_geometry *g = t3_lathe_geometry_new(prof, 9, 14, 0, 6.2831853f);
  t3_mesh *me = mesh(grp, g, m);
  t3_object_set_rotation(&me->base, 0, 0, -1.5707963f);
  t3_release(g);
}
static t3_object *limb_group(t3_object *root) {
  t3_object *grp = t3_group_new();
  t3_object_add(root, grp);
  t3_release(grp);
  return grp;
}

/* Opaque curved hair cards: broad coverage near the crown, tapered overlapping
 * clumps at the hairline. Their length varies by region so the face and ears
 * remain visible while the back reaches the nape. */
static t3_geometry *medium_hair_cards(void) {
  enum { CLUMPS = 32, ROWS = 7, VERTS = CLUMPS * ROWS * 3, INDICES = CLUMPS * (ROWS - 1) * 24 };
  float *pos = malloc(sizeof(float) * 3 * VERTS);
  uint32_t *idx = malloc(sizeof(uint32_t) * INDICES);
  int k = 0;
  for (int c = 0; c < CLUMPS; c++) {
    float phi = 6.2831853f * c / CLUMPS;
    float front = -cw_cos(phi);
    float end = front > 0.4f ? 1.02f : front < -0.35f ? 2.13f : 1.53f;
    end += 0.055f * cw_sin(c * 2.4f);
    for (int row = 0; row < ROWS; row++) {
      float t = (float)row / (ROWS - 1);
      float theta = 0.13f + (end - 0.13f) * t;
      float half = 0.135f * (1 - 0.25f * t);
      for (int side = 0; side < 3; side++) {
        float p = phi + (side - 1) * half + 0.18f * t;
        float r = 0.118f + (side == 1 ? 0.008f : 0) + 0.003f * cw_sin(t * 3.1415927f);
        int v = (c * ROWS * 3 + row * 3 + side) * 3;
        pos[v] = 0.01f - r * cw_cos(p) * cw_sin(theta);
        pos[v + 1] = -0.015f + r * cw_cos(theta);
        pos[v + 2] = r * cw_sin(p) * cw_sin(theta);
      }
    }
    for (int row = 0; row < ROWS - 1; row++)
      for (int side = 0; side < 2; side++) {
        uint32_t a = c * ROWS * 3 + row * 3 + side, b = a + 1, d = a + 3, e = d + 1;
        idx[k++] = a; idx[k++] = b; idx[k++] = d;
        idx[k++] = b; idx[k++] = e; idx[k++] = d;
        idx[k++] = d; idx[k++] = b; idx[k++] = a;
        idx[k++] = d; idx[k++] = e; idx[k++] = b;
      }
  }
  t3_geometry *g = t3_geometry_new();
  set_attr(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, pos, VERTS, 3));
  set_index(g, t3_attribute_new(T3_UINT32, idx, k, 1));
  t3_geometry_compute_vertex_normals(g);
  free(pos);
  free(idx);
  return g;
}

static void make_mats(mats *m) {
  /* deep racing green, metallic, a fine flake under a thick clear coat */
  m->paint = t3_mesh_physical_material_new(0xffffff);
  int paint_ix = selected_bike[rider_ix];
  m->paint->color = lin(PAINT[paint_ix][0], PAINT[paint_ix][1], PAINT[paint_ix][2]);
  m->paint->metalness = 0.6f;
  m->paint->roughness = 0.32f;
  m->paint->clearcoat = 1;
  m->paint->clearcoat_roughness = 0.04f;
  tex_apply(m->paint, TEX_FLAKE, 6, 6, 0.35f, 1);
  m->alloy = t3_mesh_standard_material_new(0xffffff);
  m->alloy->color = lin(0.82f, 0.84f, 0.87f);
  m->alloy->metalness = 1;
  m->alloy->roughness = 0.28f;
  tex_apply(m->alloy, TEX_METAL, 1, 1, 0.3f, 1);
  m->rubber = t3_mesh_standard_material_new(0xffffff);
  m->rubber->color = lin(0.11f, 0.105f, 0.1f);
  m->rubber->roughness = 0.88f;
  tex_apply(m->rubber, TEX_ASPHALT, 3, 1, 1.4f, 1);
  m->black = t3_mesh_standard_material_new(0xffffff);
  m->black->color = lin(0.06f, 0.06f, 0.065f);
  m->black->roughness = 0.5f;
  m->black->metalness = 0.3f;
  m->guard = t3_mesh_physical_material_new(0xffffff);
  m->guard->color = lin(0.08f, 0.08f, 0.09f);
  m->guard->roughness = 0.35f;
  m->guard->clearcoat = 0.6f;
  m->guard->clearcoat_roughness = 0.2f;
  m->saddle = t3_mesh_physical_material_new(0xffffff);
  m->saddle->color = lin(0.09f, 0.08f, 0.08f);
  m->saddle->roughness = 0.45f;
  m->saddle->clearcoat = 0.5f;
  m->saddle->clearcoat_roughness = 0.3f;
  /* a plain black cotton t-shirt: soft sheen, folds in the normal map */
  m->shirt = t3_mesh_physical_material_new(0xffffff);
  m->shirt->color = slim_rider() && rider_ix == 0 ? lin(0.85f, 0.11f, 0.095f) : lin(SHIRT[rider_ix][0], SHIRT[rider_ix][1], SHIRT[rider_ix][2]);
  m->shirt->roughness = 0.9f;
  m->shirt->sheen = 1;
  m->shirt->sheen_color = lin(0.38f, 0.38f, 0.42f);
  m->shirt->sheen_roughness = 0.6f;
  tex_apply(m->shirt, TEX_COTTON, 2, 2, 0.9f, 1);
  /* khaki cargo shorts: cotton twill */
  m->shorts = t3_mesh_physical_material_new(0xffffff);
  m->shorts->color = slim_rider() ? lin(0.52f, 0.45f, 0.31f) : lin(0.66f, 0.58f, 0.4f);
  m->shorts->roughness = 0.85f;
  m->shorts->sheen = 0.6f;
  m->shorts->sheen_color = lin(0.85f, 0.8f, 0.65f);
  m->shorts->sheen_roughness = 0.6f;
  tex_apply(m->shorts, TEX_FABRIC, 3, 3, 0.8f, 1);
  /* warm medium-brown skin (#9E6A45): a soft warm sheen stands in for
   * subsurface glow */
  m->skin = t3_mesh_physical_material_new(0xffffff);
  m->skin->color = lin(0.62f, 0.416f, 0.27f);
  m->skin->roughness = 0.5f;
  m->skin->sheen = 0.55f;
  m->skin->sheen_color = lin(0.95f, 0.55f, 0.4f);
  m->skin->sheen_roughness = 0.45f;
  m->skin->specular_intensity = 0.6f;
  m->sock = t3_mesh_standard_material_new(0xffffff);
  m->sock->color = lin(0.9f, 0.9f, 0.87f);
  m->sock->roughness = 0.95f;
  /* black canvas / suede shoes: an off-white midsole, a dark tread, a worn toe, dark laces */
  m->shoe = t3_mesh_physical_material_new(0xffffff);
  m->shoe->color = slim_rider() ? lin(0.32f, 0.17f, 0.085f) : lin(0.05f, 0.05f, 0.055f);
  m->shoe->roughness = 0.8f;
  m->shoe->sheen = 0.5f;
  m->shoe->sheen_color = lin(0.3f, 0.3f, 0.32f);
  m->shoe->sheen_roughness = 0.6f;
  tex_apply(m->shoe, TEX_FABRIC, 2, 2, 0.8f, 1);
  m->worn = t3_mesh_physical_material_new(0xffffff);
  m->worn->color = slim_rider() ? lin(0.44f, 0.26f, 0.14f) : lin(0.13f, 0.13f, 0.135f);
  m->worn->roughness = 0.85f;
  m->worn->sheen = 0.6f;
  m->worn->sheen_color = lin(0.4f, 0.4f, 0.4f);
  m->sole = t3_mesh_standard_material_new(0xffffff);
  m->sole->color = lin(0.86f, 0.85f, 0.8f);
  m->sole->roughness = 0.7f;
  m->tread = t3_mesh_standard_material_new(0xffffff);
  m->tread->color = lin(0.12f, 0.11f, 0.11f);
  m->tread->roughness = 0.9f;
  m->lace = t3_mesh_standard_material_new(0xffffff);
  m->lace->color = lin(0.18f, 0.18f, 0.19f);
  m->lace->roughness = 0.75f;
  /* a black cotton baseball cap, a little worn */
  m->cap = t3_mesh_physical_material_new(0xffffff);
  m->cap->color = lin(0.055f, 0.055f, 0.06f);
  m->cap->roughness = 0.88f;
  m->cap->sheen = 0.7f;
  m->cap->sheen_color = lin(0.4f, 0.4f, 0.43f);
  m->cap->sheen_roughness = 0.6f;
  tex_apply(m->cap, TEX_COTTON, 1, 1, 1.0f, 1);
}
static void free_mats(mats *m) {
  t3_material *all[] = { m->paint, m->alloy, m->rubber, m->black, m->saddle, m->shirt, m->shorts, m->skin, m->sock, m->shoe,
                         m->cap, m->guard, m->sole, m->tread, m->worn, m->lace };
  for (int i = 0; i < (int)CW_ARRAY_LEN(all); i++) t3_release(all[i]);
}

/* a mudguard: a slice of a torus over the top of a wheel */
static void mudguard(t3_object *f, const mats *m, t3_vec2 axle, float a0, float arc) {
  /* Outer tire radius is 0.203; keep the guard's inner edge clear of it. */
  t3_geometry *g = t3_torus_geometry_new(0.225f, 0.013f, 6, 24, arc);
  int paint_ix = selected_bike[rider_ix];
  t3_mesh *me = mesh(f, g, paint_ix == 1 ? m->alloy : m->guard);
  me->base.position = (t3_vec3){ axle.x, axle.y, 0 };
  me->base.scale = (t3_vec3){ 1, 1, 0.9f };
  t3_object_set_rotation(&me->base, 0, 0, a0);
  t3_release(g);
}

/* the frame: curved main tube with its hinge, the rear triangle, the seat
 * post, stem and bars, guards, the carrier block and the drivetrain */
static t3_object *build_frame(const mats *m, t3_geometry *cyl) {
  t3_object *f = t3_group_new();
  /* the main tube sweeps from the head tube down and back to the seat tube */
  t3_vec2 mid = { 0.2f, 0.36f };
  bent_tube(f, cyl, m->paint, HT_BOT, (t3_vec2){ 0.34f, 0.38f }, mid, 0.026f, 0, 4);
  bent_tube(f, cyl, m->paint, mid, (t3_vec2){ 0.05f, 0.36f }, ST, 0.026f, 0, 4);
  /* the fold hinge: a clamp block and its lever on the near side */
  t3_geometry *hinge = t3_box_geometry_new(0.07f, 0.075f, 0.075f, 1, 1, 1);
  t3_mesh *hb = mesh(f, hinge, m->alloy);
  hb->base.position = (t3_vec3){ mid.x, mid.y, 0 };
  t3_object_set_rotation(&hb->base, 0, 0, -0.1f);
  t3_geometry *lever = t3_box_geometry_new(0.12f, 0.018f, 0.02f, 1, 1, 1);
  t3_mesh *lv = mesh(f, lever, m->black);
  lv->base.position = (t3_vec3){ mid.x + 0.02f, mid.y - 0.02f, 0.05f };
  t3_object_set_rotation(&lv->base, 0, 0, -0.25f);
  /* the seat tube, short, from the rear pivot up past the main tube */
  tube(f, cyl, m->paint, PIVOT, ST, 0.025f, 0);
  tube(f, cyl, m->paint, ST, (t3_vec2){ ST.x - 0.035f, ST.y + 0.08f }, 0.027f, 0);
  /* the head tube with the carrier block on its front */
  tube(f, cyl, m->paint, HT_BOT, HT_TOP, 0.03f, 0);
  t3_geometry *block = t3_box_geometry_new(0.06f, 0.07f, 0.11f, 1, 1, 1);
  t3_mesh *cb = mesh(f, block, m->black);
  cb->base.position = (t3_vec3){ HT_TOP.x + 0.05f, HT_TOP.y - 0.04f, 0 };
  /* the rear triangle: stays from the pivot and the seat tube to the axle */
  for (int s2 = -1; s2 <= 1; s2 += 2) {
    tube(f, cyl, m->paint, PIVOT, RA, 0.014f, 0.045f * s2);
    tube(f, cyl, m->paint, (t3_vec2){ ST.x - 0.01f, ST.y - 0.12f }, RA, 0.012f, 0.04f * s2);
    tube(f, cyl, m->paint, HT_BOT, FA, 0.015f, 0.045f * s2); /* fork blades */
  }
  /* the long telescoping seat post: a thick lower tube, a thinner upper one */
  tube(f, cyl, m->alloy, (t3_vec2){ ST.x - 0.035f, ST.y + 0.08f }, (t3_vec2){ -0.16f, 0.6f }, 0.017f, 0);
  tube(f, cyl, m->alloy, (t3_vec2){ -0.16f, 0.6f }, (t3_vec2){ SADDLE.x + 0.01f, SADDLE.y - 0.03f }, 0.014f, 0);
  /* the tall folding stem: hinge block at the head tube, then up to the bars */
  t3_mesh *sh = mesh(f, hinge, m->alloy);
  sh->base.position = (t3_vec3){ HT_TOP.x, HT_TOP.y + 0.04f, 0 };
  sh->base.scale = (t3_vec3){ 0.7f, 0.6f, 0.7f };
  tube(f, cyl, m->paint, (t3_vec2){ HT_TOP.x, HT_TOP.y + 0.06f }, BARS, 0.022f, 0);
  /* flat bars across, grips at the ends */
  t3_geometry *bar = t3_cylinder_geometry_new(0.012f, 0.012f, 0.56f, 12, 1, false);
  t3_mesh *b = mesh(f, bar, m->black);
  b->base.position = (t3_vec3){ BARS.x, BARS.y, 0 };
  t3_object_set_rotation(&b->base, 1.5707963f, 0, 0);
  t3_geometry *grip = t3_cylinder_geometry_new(0.018f, 0.018f, 0.11f, 12, 1, false);
  for (int s2 = -1; s2 <= 1; s2 += 2) {
    t3_mesh *g = mesh(f, grip, m->rubber);
    g->base.position = (t3_vec3){ BARS.x, BARS.y, 0.23f * s2 };
    t3_object_set_rotation(&g->base, 1.5707963f, 0, 0);
  }
  /* the saddle */
  t3_vec2 sp[6] = { { 0.0f, -0.02f }, { 0.07f, -0.015f }, { 0.08f, 0.0f }, { 0.07f, 0.015f }, { 0.03f, 0.02f }, { 0.0f, 0.021f } };
  t3_geometry *sad = t3_lathe_geometry_new(sp, 6, 16, 0, 6.2831853f);
  t3_mesh *s = mesh(f, sad, m->saddle);
  s->base.position = (t3_vec3){ SADDLE.x, SADDLE.y, 0 };
  s->base.scale = (t3_vec3){ 1.6f, 1, 0.75f };
  /* the drivetrain on the far (right) side: a big chainring behind a guard
   * disc, the chain to a small sprocket */
  t3_geometry *ring = t3_torus_geometry_new(0.1f, 0.008f, 6, 32, 6.2831853f);
  t3_mesh *r = mesh(f, ring, m->alloy);
  r->base.position = (t3_vec3){ BB.x, BB.y, -0.06f };
  t3_geometry *gd = t3_cylinder_geometry_new(0.115f, 0.115f, 0.006f, 32, 1, false);
  t3_mesh *gm = mesh(f, gd, m->guard);
  gm->base.position = (t3_vec3){ BB.x, BB.y, -0.075f };
  t3_object_set_rotation(&gm->base, 1.5707963f, 0, 0);
  t3_geometry *cog = t3_torus_geometry_new(0.03f, 0.007f, 6, 16, 6.2831853f);
  t3_mesh *cg = mesh(f, cog, m->alloy);
  cg->base.position = (t3_vec3){ RA.x, RA.y, -0.06f };
  for (int s2 = -1; s2 <= 1; s2 += 2) tube(f, cyl, m->black, (t3_vec2){ BB.x, BB.y + 0.1f * s2 }, (t3_vec2){ RA.x, RA.y + 0.03f * s2 }, 0.004f, -0.06f);
  t3_geometry *shell = t3_cylinder_geometry_new(0.028f, 0.028f, 0.1f, 16, 1, false);
  t3_mesh *bs = mesh(f, shell, m->paint);
  t3_object_set_rotation(&bs->base, 1.5707963f, 0, 0);
  tube(f, cyl, m->paint, BB, PIVOT, 0.026f, 0);
  t3_geometry *rel[] = { hinge, lever, block, bar, grip, sad, ring, gd, cog, shell };
  for (int i = 0; i < (int)CW_ARRAY_LEN(rel); i++) t3_release(rel[i]);
  return f;
}

static t3_object *build_crank(const mats *m) {
  t3_object *c = t3_group_new();
  t3_geometry *arm = t3_box_geometry_new(0.17f, 0.022f, 0.014f, 1, 1, 1);
  t3_geometry *pedal = t3_box_geometry_new(0.1f, 0.022f, 0.1f, 1, 1, 1);
  for (int s = -1; s <= 1; s += 2) {
    /* left crank (toward the camera, +z) points along +x; the right one opposite */
    t3_mesh *a = mesh(c, arm, m->alloy);
    a->base.position = (t3_vec3){ 0.085f * -s, 0, 0.075f * -s };
    t3_mesh *p = mesh(c, pedal, m->black);
    p->base.position = (t3_vec3){ 0.17f * -s, 0, 0.13f * -s };
  }
  t3_release(arm);
  t3_release(pedal);
  return c;
}

/* ── one draw per material ────────────────────────────────────────────
 * The rider and the bike are built as a hundred-odd small meshes in a couple
 * of dozen moving parts (the frame, the wheels, the crank, each limb...).
 * They are drawn as skinned meshes instead, one per material, each part a
 * bone: the same picture in a fifth of the draws. The far look (split
 * screen's other riders) is two: the bike and the rider, each part in its
 * material's colour. Every view of every rider costs draws, so this is
 * what keeps split screen cheap. */
#define MAX_BONES 32
typedef struct part { t3_mesh *m; int bone; } part;
/* geometries placed by their matrices, as one indexed geometry: the
 * attributes in mask (bits of MERGE_SLOTS, float32 in every one) */
static const t3_attr_slot MERGE_SLOTS[] = { T3_ATTR_POSITION, T3_ATTR_NORMAL, T3_ATTR_UV, T3_ATTR_COLOR, T3_ATTR_UV1 };
enum { MERGE_SLOT_COUNT = (int)CW_ARRAY_LEN(MERGE_SLOTS) };
unsigned geo_merge_mask(const t3_geometry *g) {
  unsigned m = 0;
  for (int k = 0; k < MERGE_SLOT_COUNT; k++)
    if (g->attributes[MERGE_SLOTS[k]]) m |= 1u << k;
  return m;
}
bool geo_mergeable(const t3_geometry *g) {
  if (!g->attributes[T3_ATTR_POSITION] || g->morph_count || g->named_count || g->attributes[T3_ATTR_TANGENT] || g->draw_start != 0 ||
      (g->draw_count >= 0 && g->draw_count != (g->index ? g->index->count : g->attributes[T3_ATTR_POSITION]->count)))
    return false;
  for (int k = 0; k < MERGE_SLOT_COUNT; k++)
    if (g->attributes[MERGE_SLOTS[k]] && g->attributes[MERGE_SLOTS[k]]->type != T3_FLOAT32) return false;
  return true;
}
t3_geometry *geo_merge(const t3_geometry *const *gs, const t3_mat4 *const *mats, int n, unsigned mask) {
  int nv = 0, ni = 0;
  for (int i = 0; i < n; i++) {
    int c = gs[i]->attributes[T3_ATTR_POSITION]->count;
    nv += c;
    ni += gs[i]->index ? gs[i]->index->count : c;
  }
  float *data[MERGE_SLOT_COUNT] = { 0 };
  int size[MERGE_SLOT_COUNT] = { 0 };
  for (int k = 0; k < MERGE_SLOT_COUNT; k++)
    if (mask & (1u << k)) {
      size[k] = gs[0]->attributes[MERGE_SLOTS[k]]->item_size;
      data[k] = malloc(sizeof(float) * (size_t)(nv * size[k]));
    }
  uint32_t *idx = malloc(sizeof(uint32_t) * (size_t)ni);
  int v0 = 0, i0 = 0;
  for (int i = 0; i < n; i++) {
    const t3_geometry *src = gs[i];
    int c = src->attributes[T3_ATTR_POSITION]->count;
    t3_geometry *g = t3_geometry_new(); /* a copy to place */
    for (int k = 0; k < MERGE_SLOT_COUNT; k++)
      if (mask & (1u << k)) {
        const t3_attribute *at = src->attributes[MERGE_SLOTS[k]];
        set_attr(g, MERGE_SLOTS[k], t3_attribute_new(T3_FLOAT32, at->array, at->count, at->item_size));
      }
    t3_geometry_apply_matrix4(g, mats[i]);
    for (int k = 0; k < MERGE_SLOT_COUNT; k++)
      if (mask & (1u << k)) memcpy(data[k] + (size_t)v0 * size[k], g->attributes[MERGE_SLOTS[k]]->array, sizeof(float) * (size_t)(c * size[k]));
    t3_release(g);
    const float *e = mats[i]->e; /* a mirrored piece's triangles turn the other way */
    float det = e[0] * (e[5] * e[10] - e[9] * e[6]) - e[4] * (e[1] * e[10] - e[9] * e[2]) + e[8] * (e[1] * e[6] - e[5] * e[2]);
    int m = src->index ? src->index->count : c;
    for (int t = 0; t < m; t++) {
      uint32_t v = !src->index ? (uint32_t)t
                 : src->index->type == T3_UINT16 ? ((const uint16_t *)src->index->array)[t] : ((const uint32_t *)src->index->array)[t];
      idx[i0 + t] = (uint32_t)v0 + v;
    }
    if (det < 0)
      for (int t = 0; t + 2 < m; t += 3) {
        uint32_t tmp = idx[i0 + t + 1];
        idx[i0 + t + 1] = idx[i0 + t + 2];
        idx[i0 + t + 2] = tmp;
      }
    v0 += c;
    i0 += m;
  }
  t3_geometry *g = t3_geometry_new();
  for (int k = 0; k < MERGE_SLOT_COUNT; k++)
    if (mask & (1u << k)) {
      set_attr(g, MERGE_SLOTS[k], t3_attribute_new(T3_FLOAT32, data[k], nv, size[k]));
      free(data[k]);
    }
  set_index(g, t3_attribute_new(T3_UINT32, idx, ni, 1));
  free(idx);
  return g;
}

/* the parts' meshes as one skinned geometry (world space at the bind pose,
 * each vertex on its part's bone); far: position and normal, and each
 * part's material colour */
static t3_geometry *skin_geometry(const part *ps, int n, bool far) {
  const t3_geometry *gs[256];
  const t3_mat4 *ms[256];
  n = CW_MIN(n, 256);
  for (int i = 0; i < n; i++) gs[i] = ps[i].m->geometry, ms[i] = &ps[i].m->base.matrix_world;
  t3_geometry *g = geo_merge(gs, ms, n, far ? 3u : geo_merge_mask(gs[0]));
  int nv = g->attributes[T3_ATTR_POSITION]->count, v0 = 0;
  float *col = far ? malloc(sizeof(float) * (size_t)nv * 3) : NULL;
  float *si = calloc((size_t)nv * 4, sizeof(float)), *sw = calloc((size_t)nv * 4, sizeof(float));
  for (int i = 0; i < n; i++) {
    int c = gs[i]->attributes[T3_ATTR_POSITION]->count;
    t3_color mc = ps[i].m->materials[0]->color;
    for (int v = v0; v < v0 + c; v++) {
      si[v * 4] = (float)ps[i].bone;
      sw[v * 4] = 1;
      if (col) col[v * 3] = mc.r, col[v * 3 + 1] = mc.g, col[v * 3 + 2] = mc.b;
    }
    v0 += c;
  }
  if (col) set_attr(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, col, nv, 3));
  set_attr(g, T3_ATTR_SKIN_INDEX, t3_attribute_new(T3_FLOAT32, si, nv, 4));
  set_attr(g, T3_ATTR_SKIN_WEIGHT, t3_attribute_new(T3_FLOAT32, sw, nv, 4));
  free(col), free(si), free(sw);
  return g;
}
static t3_object *skin_mesh(t3_geometry *g, t3_material *mat, t3_skeleton *sk, const t3_object *like) {
  t3_skinned_mesh *sm = t3_skinned_mesh_new(g, mat);
  t3_release(g);
  /* the parts carry the rider's place in the world: bounds at the bind pose mean nothing */
  sm->mesh.base.frustum_culled = false;
  sm->mesh.base.cast_shadow = like->cast_shadow;
  sm->mesh.base.receive_shadow = like->receive_shadow;
  sm->mesh.base.render_order = like->render_order;
  t3_object_add(M.root, sm);
  t3_release(sm);
  t3_object_update_world_matrix(sm, false, false);
  t3_skinned_mesh_bind(sm, sk, NULL);
  return &sm->mesh.base;
}
static void skin_build(void) {
  t3_object *bones[MAX_BONES];
  int nb = 0;
  bones[nb++] = M.root; /* for the pieces no part moves */
  t3_object *moving[] = { M.frame, M.rear, M.front, M.rear_guard, M.front_guard, M.crank, M.cap, M.neck, M.foot[0], M.foot[1] };
  int bike_bones = 7; /* the root and the bike's six parts */
  for (int i = 0; i < (int)CW_ARRAY_LEN(moving); i++)
    if (moving[i]) bones[nb++] = moving[i];
  for (int i = 0; i < RAG_COUNT; i++) {
    if (M.limb[i]) bones[nb++] = M.limb[i];
    if (M.limb_r[i]) bones[nb++] = M.limb_r[i];
  }
  t3_object_update_world_matrix(M.root, false, true);
  /* every mesh, on the bone of the nearest part that holds it */
  part *ps = malloc(sizeof *ps * 256);
  int np = 0;
  t3_object *st[256];
  int sp = 0;
  for (int c = M.root->child_count - 1; c >= 0; c--) st[sp++] = M.root->children[c];
  while (sp) {
    t3_object *o = st[--sp];
    for (int c = o->child_count - 1; c >= 0 && sp < 256; c--) st[sp++] = o->children[c];
    if (o->type != T3_MESH || !o->visible || np == 256 || !geo_mergeable(((t3_mesh *)o)->geometry) || ((t3_mesh *)o)->material_count != 1) continue;
    int bone = 0;
    for (const t3_object *a = o; a && a != M.root && !bone; a = a->parent)
      for (int k = 1; k < nb; k++)
        if (bones[k] == a) bone = k;
    ps[np++] = (part){ (t3_mesh *)o, bone };
  }
  t3_skeleton *sk = t3_skeleton_new(bones, nb, NULL);
  /* near: a skinned mesh per material (and shadow flags, attribute set) */
  part *run = malloc(sizeof *run * (size_t)np);
  bool *done = calloc((size_t)np, 1);
  M.near_n = 0;
  for (int i = 0; i < np; i++) {
    if (done[i]) continue;
    int nr = 0;
    for (int j = i; j < np; j++) {
      const t3_object *a = &ps[i].m->base, *b = &ps[j].m->base;
      if (!done[j] && ps[j].m->materials[0] == ps[i].m->materials[0] && a->cast_shadow == b->cast_shadow && a->receive_shadow == b->receive_shadow &&
          a->render_order == b->render_order && geo_merge_mask(ps[j].m->geometry) == geo_merge_mask(ps[i].m->geometry))
        run[nr++] = ps[j], done[j] = true;
    }
    if (M.near_n < (int)CW_ARRAY_LEN(M.near_mesh))
      M.near_mesh[M.near_n++] = skin_mesh(skin_geometry(run, nr, false), ps[i].m->materials[0], sk, &ps[i].m->base);
  }
  /* far: the bike, the rider */
  for (int f = 0; f < 2; f++) {
    int nr = 0;
    for (int i = 0; i < np; i++)
      if ((ps[i].bone > 0 && ps[i].bone < bike_bones) == (f == 0)) run[nr++] = ps[i];
    t3_material *mat = t3_mesh_standard_material_new(0xffffff);
    mat->vertex_colors = true;
    mat->roughness = f == 0 ? 0.4f : 0.8f;
    mat->metalness = f == 0 ? 0.3f : 0;
    M.far_mesh[f] = nr ? skin_mesh(skin_geometry(run, nr, true), mat, sk, &run[0].m->base) : NULL;
    if (M.far_mesh[f]) M.far_mesh[f]->visible = false;
    t3_release(mat);
  }
  for (int i = 0; i < np; i++) ps[i].m->base.visible = false; /* the parts stay, as bones */
  t3_release(sk);
  free(done);
  free(run);
  free(ps);
}

/* rider idx near (every material) or far (two draws) */
void models_far(int idx, bool far) {
  model_t *mm = &MS[CW_CLAMP(idx, 0, MAX_RIDERS - 1)];
  if (!mm->root) return;
  for (int i = 0; i < mm->near_n; i++) mm->near_mesh[i]->visible = !far;
  for (int f = 0; f < 2; f++)
    if (mm->far_mesh[f]) mm->far_mesh[f]->visible = far;
}

void models_build(cw_node parent) { models_build_rider(parent, 0); }

void models_build_rider(cw_node parent, int idx) {
  Mc = &MS[CW_CLAMP(idx, 0, MAX_RIDERS - 1)];
  rider_ix = (int)(Mc - MS);
  bool slim = slim_rider();
  memset(&M, 0, sizeof M);
  mats m;
  make_mats(&m);
  M.root = t3_group_new();
  t3_geometry *cyl = t3_cylinder_geometry_new(1, 1, 1, 14, 1, false);
  t3_geometry *spokes = geo_spokes(0.024f, 0.152f, 16);
  M.frame = build_frame(&m, cyl);
  t3_object_add(M.root, M.frame);
  t3_release(M.frame);
  M.crank = build_crank(&m);
  t3_object_add(M.frame, M.crank);
  t3_release(M.crank);
  M.rear = build_wheel(&m, spokes);
  M.front = build_wheel(&m, spokes);
  t3_object_add(M.root, M.rear);
  t3_object_add(M.root, M.front);
  t3_release(M.rear);
  t3_release(M.front);
  M.rear_guard = t3_group_new();
  M.front_guard = t3_group_new();
  mudguard(M.rear_guard, &m, (t3_vec2){ 0, 0 }, 0.35f, 2.3f);
  mudguard(M.front_guard, &m, (t3_vec2){ 0, 0 }, 0.5f, 2.0f);
  t3_object_add(M.root, M.rear_guard);
  t3_object_add(M.root, M.front_guard);
  t3_release(M.rear_guard);
  t3_release(M.front_guard);

  /* the rider (left limbs toward the camera, +z): a black t-shirt with short
   * sleeves over bare arms, khaki cargo shorts with side pockets over bare
   * shins, short socks and black shoes. Each limb is its own group, the
   * same pieces the ragdoll throws. */
  for (int i = 0; i < RAG_COUNT; i++)
    if (i != RAG_HEAD) M.limb[i] = limb_group(M.root);
  if (slim) shirt_torso(M.limb[RAG_TORSO], m.shirt);
  else limb_part(M.limb[RAG_TORSO], 0.6f, 0.15f, 0.17f, 0, 1, 1, m.shirt, 0);
  t3_geometry *seat = t3_sphere_geometry_new(0.115f, 16, 12);
  t3_mesh *hips = mesh(M.limb[RAG_TORSO], seat, m.shorts);
  hips->base.position = (t3_vec3){ -0.005f, 0.015f, 0 };
  hips->base.scale = (t3_vec3){ 0.85f, 0.7f, slim ? 1.25f : 1.4f };
  t3_release(seat);
  for (int side = 0; side < 2; side++) {
    float z = side ? -1 : 1;
    int ua = side ? RAG_UARM_R : RAG_UARM_L, fa = side ? RAG_FARM_R : RAG_FARM_L;
    int th = side ? RAG_THIGH_R : RAG_THIGH_L, sn = side ? RAG_SHIN_R : RAG_SHIN_L;
    float width = slim ? 0.84f : 1;
    limb_part(M.limb[ua], 0.3f, 0.05f, 0.044f, 0, 1, width, m.skin, 0.2f * z);
    limb_part(M.limb[ua], 0.3f, 0.05f, 0.044f, 0, 0.5f, 1.25f * width, m.shirt, 0.2f * z); /* the sleeve */
    limb_part(M.limb[fa], 0.3f, 0.043f, 0.036f, 0, 1, width, m.skin, 0.22f * z);
    limb_part(M.limb[th], 0.44f, 0.09f, 0.065f, 0, 1, slim ? 0.92f : 1.2f, m.shorts, 0.1f * z);
    limb_part(M.limb[sn], 0.44f, 0.056f, 0.032f, 0, 1, width, m.skin, 0.12f * z);
    limb_part(M.limb[sn], 0.44f, 0.056f, 0.032f, 0.70f, 0.97f, 1.03f * width, m.sock, 0.12f * z);
    /* a cargo pocket on the outer side of each thigh */
    t3_geometry *pocket = t3_box_geometry_new(0.13f, 0.09f, 0.03f, 1, 1, 1);
    t3_mesh *pk = mesh(M.limb[th], pocket, m.shorts);
    pk->base.position = (t3_vec3){ 0.26f, -0.01f, (slim ? 0.175f : 0.2f) * z };
    t3_geometry *flap = t3_box_geometry_new(0.135f, 0.025f, 0.034f, 1, 1, 1);
    t3_mesh *fl = mesh(M.limb[th], flap, m.shorts);
    fl->base.position = (t3_vec3){ 0.215f, -0.01f, (slim ? 0.177f : 0.202f) * z };
    t3_object_set_rotation(&fl->base, 0, 0, 1.5707963f);
    t3_release(pocket);
    t3_release(flap);
    /* hands; each shoe has its own ankle hinge at the end of its shin */
    t3_object *hd = grip_hand_build(m.skin, z);
    t3_object_add(M.limb[fa], hd);
    t3_release(hd);
    hd->position = (t3_vec3){ 0.3f, 0, 0.23f * z };
    /* the shoe: its length along the foot (the shin's +y), its top toward the knee */
    t3_object *sh = shoe_build(m.shoe, m.sole, m.tread, m.worn, m.lace);
    if (slim) sh->scale = (t3_vec3){ 0.93f, 1, 0.9f };
    M.foot[side] = limb_group(M.root);
    t3_object_add(M.foot[side], sh);
    t3_release(sh);
    /* Shoe collar at local (-.03, .094) meets the hinge at the shin tip. */
    sh->position = (t3_vec3){ 0.03f, -0.094f, 0 };
  }
  /* the head; the cap is its own piece (it flies off in a crash) */
  t3_object *head = t3_group_new();
  t3_object_add(M.root, head);
  t3_release(head);
  M.limb[RAG_HEAD] = head;
  t3_geometry *face = t3_sphere_geometry_new(0.11f, 20, 14);
  t3_mesh *fm = mesh(head, face, m.skin);
  fm->base.position = (t3_vec3){ 0.01f, -0.015f, 0 };
  t3_geometry *neck = t3_cylinder_geometry_new(0.043f, 0.052f, 1, 12, 1, false);
  t3_mesh *nm_neck = mesh(M.root, neck, m.skin);
  M.neck = &nm_neck->base;
  t3_release(neck);
  t3_material *hair = t3_mesh_standard_material_new(0xffffff);
  hair->color = lin(0.012f, 0.012f, 0.015f);
  hair->roughness = 0.95f;
  t3_geometry *scalp = t3_sphere_geometry_new_ex(slim ? 0.114f : 0.113f, 24, 12, 0, 6.2831853f, 0, slim ? 0.9f : 1.48f);
  t3_mesh *hm = mesh(head, scalp, hair);
  hm->base.position = fm->base.position;
  if (slim) {
    t3_geometry *backing = t3_sphere_geometry_new_ex(0.114f, 20, 12, -1.45f, 2.9f, 0.85f, 1.3f);
    t3_mesh *bm = mesh(head, backing, hair);
    bm->base.position = fm->base.position;
    t3_release(backing);
    t3_geometry *cards = medium_hair_cards();
    mesh(head, cards, hair);
    t3_release(cards);
  }
  t3_release(scalp);
  t3_release(hair);
  /* a short, neat chin beard: dark brown, a few mm proud of the chin, a
   * slight sheen */
  if (!slim) {
  t3_material *beard = t3_mesh_physical_material_new(0xffffff);
  beard->color = lin(0.09f, 0.06f, 0.045f);
  beard->roughness = 0.75f;
  beard->sheen = 0.8f;
  beard->sheen_color = lin(0.35f, 0.27f, 0.2f);
  beard->sheen_roughness = 0.5f;
  t3_geometry *chin = t3_sphere_geometry_new(0.05f, 14, 10);
  t3_mesh *bd = mesh(head, chin, beard);
  bd->base.position = (t3_vec3){ 0.075f, -0.09f, 0 };
  bd->base.scale = (t3_vec3){ 0.62f, 0.72f, 0.95f };
  t3_object_set_rotation(&bd->base, 0, 0, 0.5f);
  t3_release(chin);
  t3_release(beard);
  }
  t3_geometry *ear = t3_sphere_geometry_new(0.025f, 8, 6);
  for (int sd2 = -1; sd2 <= 1; sd2 += 2) {
    t3_mesh *e = mesh(head, ear, m.skin);
    e->base.position = (t3_vec3){ -0.01f, -0.01f, 0.105f * sd2 };
  }
  /* a black baseball cap worn BACKWARDS: a structured six-panel crown with
   * seams and a top button snug on the head, the curved brim over the nape
   * (head-local -x; the face looks along +x), the snapback strap and its
   * opening above the forehead; no logo */
  if (!slim) {
  M.cap = t3_group_new();
  t3_object_add(M.root, M.cap);
  t3_release(M.cap);
  t3_material *under = t3_mesh_physical_material_new(0xffffff);
  under->color = lin(0.03f, 0.03f, 0.035f);
  under->roughness = 0.9f;
  /* front-faced (a back-faced physical state is not in three.c's table): it reads from below anyway */
  t3_geometry *crown = t3_sphere_geometry_new_ex(0.118f, 24, 12, 0, 6.2831853f, 0, 1.57f);
  t3_mesh *cr = mesh(M.cap, crown, m.cap);
  cr->base.position = (t3_vec3){ 0, -0.02f, 0 };
  cr->base.scale = (t3_vec3){ 1.04f, 0.9f, 1 };
  /* panel seams: three half-circles over the crown, 60 degrees apart */
  t3_geometry *seam = t3_torus_geometry_new(0.119f, 0.0025f, 4, 24, 3.1415927f);
  for (int k = 0; k < 3; k++) {
    t3_mesh *sm = mesh(M.cap, seam, under);
    sm->base.position = cr->base.position;
    sm->base.scale = (t3_vec3){ 1.04f, 0.9f, 1 };
    t3_object_set_rotation(&sm->base, 0, k * 1.0471976f, 0);
  }
  /* the brim: a patch of a bigger sphere, curving down over the nape */
  t3_geometry *brim = t3_sphere_geometry_new_ex(0.34f, 18, 6, -0.42f, 0.84f, 0.33f, 0.24f);
  t3_mesh *br = mesh(M.cap, brim, m.cap);
  br->base.position = (t3_vec3){ 0.0f, -0.36f, 0 };
  br->base.scale = (t3_vec3){ 1, 1, 0.8f };
  t3_mesh *bu = mesh(M.cap, brim, under);
  bu->base.position = (t3_vec3){ 0.0f, -0.364f, 0 };
  bu->base.scale = (t3_vec3){ 1, 1, 0.8f };
  /* the snapback band and its opening, over the forehead */
  t3_geometry *band = t3_torus_geometry_new(0.045f, 0.006f, 4, 16, 3.1415927f);
  t3_mesh *bn = mesh(M.cap, band, under);
  bn->base.position = (t3_vec3){ 0.105f, -0.03f, 0 };
  t3_object_set_rotation(&bn->base, 0, 1.5707963f, 0);
  t3_geometry *button = t3_sphere_geometry_new(0.012f, 8, 6);
  t3_mesh *bt = mesh(M.cap, button, m.cap);
  bt->base.position = (t3_vec3){ 0.0f, 0.087f, 0 };
  t3_release(seam);
  t3_release(band);
  t3_release(under);
  t3_release(ear);
  t3_release(crown);
  t3_release(brim);
  t3_release(button);
  }

  skin_build();
  char nm[16];
  snprintf(nm, sizeof nm, rider_ix ? "Bike%d" : "Bike", rider_ix + 1);
  M.node = cw_node_new("Node3D", nm);
  cw_node_add_child(parent, M.node);
  cw_node3d_set_object(M.node, M.root);
  t3_release(M.root);
  t3_release(cyl);
  t3_release(spokes);
  t3_release(face);
  free_mats(&m);
}

static void put(t3_object *o, cw_xform2 x) {
  o->position = (t3_vec3){ x.p.x, x.p.y, 0 };
  t3_object_set_rotation(o, 0, 0, cw_atan2(x.q.s, x.q.c));
}

static void put3(t3_object *o, cw_xform3 x) {
  o->position = x.p;
  t3_object_set_quaternion(o, x.q);
}

static t3_vec3 neck_rot(t3_quat q, t3_vec3 v) {
  t3_vec3 t = { 2 * (q.y * v.z - q.z * v.y), 2 * (q.z * v.x - q.x * v.z), 2 * (q.x * v.y - q.y * v.x) };
  return (t3_vec3){ v.x + q.w * t.x + q.y * t.z - q.z * t.y,
                    v.y + q.w * t.y + q.z * t.x - q.x * t.z,
                    v.z + q.w * t.z + q.x * t.y - q.y * t.x };
}
static void neck_between(t3_vec3 a, t3_vec3 b) {
  if (!M.neck) return;
  t3_vec3 d = { b.x - a.x, b.y - a.y, b.z - a.z };
  float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
  if (len < 0.001f) len = 0.001f;
  t3_vec3 u = { d.x / len, d.y / len, d.z / len };
  t3_quat q = { u.z, 0, -u.x, 1 + u.y };
  float qlen = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (qlen < 0.001f) q = (t3_quat){ 1, 0, 0, 0 };
  else q = (t3_quat){ q.x / qlen, q.y / qlen, q.z / qlen, q.w / qlen };
  M.neck->position = (t3_vec3){ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f };
  M.neck->scale = (t3_vec3){ 1, len + 0.04f, 1 };
  t3_object_set_quaternion(M.neck, q);
}

void models_sync(const bike_pose *p) { models_sync_rider(0, p, p->z); }

/* rider idx's model at its pose, its lane z across the track */
static void models_sync_rider_(const bike_pose *p, float z);
void models_sync_rider(int idx, const bike_pose *p, float z) {
  Mc = &MS[CW_CLAMP(idx, 0, MAX_RIDERS - 1)];
  if (!M.root) return;
  models_sync_rider_(p, z);
  /* split screen poses a rider again between views (its replay in its own
   * view), after the scene's matrices were updated for the frame */
  t3_object_update_world_matrix(M.root, true, true);
}
static void models_sync_rider_(const bike_pose *p, float z) {
  if (p->ragdoll) { /* a crash: every piece where the crash world has it (world space) */
    if (cw_node_valid(M.node)) cw_node3d_set_position(M.node, (t3_vec3){ 0, 0, 0 });
    put3(M.frame, p->frame3);
    put3(M.rear, p->rear3);
    put3(M.front, p->front3);
    put3(M.rear_guard, (cw_xform3){ p->rear3.p, p->frame3.q });
    put3(M.front_guard, (cw_xform3){ p->front3.p, p->frame3.q });
    t3_object_set_rotation(M.crank, 0, 0, p->crank);
    for (int i = 0; i < RAG_COUNT; i++)
      if (M.limb[i]) put3(M.limb[i], p->limb3[i]);
    for (int side = 0; side < 2; side++) {
      int sn = side ? RAG_SHIN_R : RAG_SHIN_L;
      cw_xform3 leg = p->limb3[sn];
      float depth = side ? -0.12f : 0.12f;
      t3_vec3 tip = neck_rot(leg.q, (t3_vec3){ 0.44f, 0, depth });
      M.foot[side]->position = (t3_vec3){ leg.p.x + tip.x, leg.p.y + tip.y, leg.p.z + tip.z };
      t3_object_set_quaternion(M.foot[side], t3_quat_multiply(leg.q,
        t3_quat_from_axis_angle((t3_vec3){ 0, 0, 1 }, 1.5707963f)));
    }
    t3_vec3 ta = neck_rot(p->limb3[RAG_TORSO].q, (t3_vec3){ 0.58f, 0, 0 });
    t3_vec3 hb = neck_rot(p->limb3[RAG_HEAD].q, (t3_vec3){ -0.01f, -0.06f, 0 });
    t3_vec3 a = p->limb3[RAG_TORSO].p, b = p->limb3[RAG_HEAD].p;
    neck_between((t3_vec3){ a.x + ta.x, a.y + ta.y, a.z + ta.z },
                 (t3_vec3){ b.x + hb.x, b.y + hb.y, b.z + hb.z });
    if (M.cap) put3(M.cap, p->cap3);
    return;
  }
  if (cw_node_valid(M.node)) cw_node3d_set_position(M.node, (t3_vec3){ 0, 0, z });
  put(M.frame, p->frame);
  put(M.rear, p->rear);
  put(M.front, p->front);
  put(M.rear_guard, (cw_xform2){ p->rear.p, p->frame.q });
  put(M.front_guard, (cw_xform2){ p->front.p, p->frame.q });
  t3_object_set_rotation(M.crank, 0, 0, p->crank);
  M.crank->position = (t3_vec3){ 0, 0, 0 };
  for (int i = 0; i < RAG_COUNT; i++)
    if (M.limb[i]) put(M.limb[i], p->limb[i]);
  for (int side = 0; side < 2; side++) {
    int sn = side ? RAG_SHIN_R : RAG_SHIN_L;
    cw_xform2 leg = p->limb[sn];
    put(M.foot[side], (cw_xform2){ { leg.p.x + 0.44f * leg.q.c,
                                    leg.p.y + 0.44f * leg.q.s }, p->frame.q });
    M.foot[side]->position.z = side ? -0.12f : 0.12f;
  }
  cw_xform2 torso = p->limb[RAG_TORSO], head = p->limb[RAG_HEAD];
  neck_between((t3_vec3){ torso.p.x + 0.58f * torso.q.c, torso.p.y + 0.58f * torso.q.s, 0 },
               (t3_vec3){ head.p.x - 0.01f * head.q.c + 0.06f * head.q.s,
                           head.p.y - 0.01f * head.q.s - 0.06f * head.q.c, 0 });
  if (M.cap) put(M.cap, p->cap);
}

void models_forget(void) {
  memset(MS, 0, sizeof MS);
  Mc = MS;
}

void models_turntable(int idx, float angle, float center_x) {
  t3_object *root = MS[CW_CLAMP(idx, 0, MAX_RIDERS - 1)].root;
  if (!root) return;
  float sn, cs;
  cw_sincos(angle, &sn, &cs);
  t3_object_set_rotation(root, 0, angle, 0);
  root->position = (t3_vec3){ center_x * (1 - cs), 0, center_x * sn };
}

