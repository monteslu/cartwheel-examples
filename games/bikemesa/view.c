/* rider's world look: the track as a solid extruded ribbon (a textured
 * riding surface on top, a cliff face down to the bedrock in front, built
 * ramps as plank decks), and the scenery each theme stands in, all built in C
 * from the track data:
 *
 *   canyon   red sandstone mesas and buttes far behind, boulders, dry grass
 *            tufts swaying along the edges, drifting dust
 *   (others) their own palettes on the same pieces (view.c: THEME)
 *
 * Placement uses an integer hash of the track's seed, never rand(): the same
 * world on every host. Nothing here is physical (bike.c owns collision), but
 * a crash tumbles in 3D through this scenery: the bank's height and the
 * rocks' shapes are handed to it (view_bank_y, view_rocks; crash.c). */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "cartwheel/particles.h"
#include "game.h"

static uint32_t sh(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}
static uint32_t S;
static float rnd(void) {
  S = sh(S + 0x9e3779b9u);
  return (float)(S & 0xffffff) / 16777216.0f;
}
static t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

/* per theme: the riding surface, the cliff face, the far landforms and the fill */
typedef struct theme {
  const char *name;
  int top_tex, face_tex, far_tex;
  float top[3], face[3], far[3], tuft[3], tuft2[3];
  int far_kind;   /* 0 mesas, 1 peaks, 2 towers, 3 domes */
  float top_rough;
} theme;
static const theme THEMES[] = {
  { "canyon", TEX_DIRT, TEX_SANDSTONE, TEX_SANDSTONE, { 0.95f, 0.72f, 0.55f }, { 1, 0.62f, 0.45f }, { 1, 0.6f, 0.42f },
    { 0.78f, 0.66f, 0.36f }, { 0.6f, 0.55f, 0.3f }, 0, 1 },
  { "alpine", TEX_SNOW, TEX_ROCK, TEX_ROCK, { 1, 1, 1 }, { 0.62f, 0.64f, 0.68f }, { 0.8f, 0.84f, 0.9f },
    { 0.55f, 0.6f, 0.4f }, { 0.4f, 0.5f, 0.35f }, 1, 0.7f },
  { "neon", TEX_ASPHALT, TEX_METAL, TEX_ASPHALT, { 1.4f, 1.4f, 1.6f }, { 0.55f, 0.55f, 0.65f }, { 0.4f, 0.4f, 0.55f },
    { 0.3f, 0.9f, 1 }, { 1, 0.3f, 0.8f }, 2, 0.6f },
  { "jungle", TEX_MOSS, TEX_MOSS, TEX_ROCK, { 0.9f, 1, 0.85f }, { 0.75f, 0.8f, 0.7f }, { 0.45f, 0.6f, 0.4f },
    { 0.3f, 0.6f, 0.2f }, { 0.4f, 0.7f, 0.25f }, 3, 0.9f },
  { "desert", TEX_SAND, TEX_SANDSTONE, TEX_SAND, { 1, 0.92f, 0.8f }, { 1, 0.78f, 0.58f }, { 1, 0.8f, 0.6f },
    { 0.8f, 0.72f, 0.45f }, { 0.65f, 0.6f, 0.35f }, 3, 1 },
  { "autumn", TEX_DIRT, TEX_ROCK, TEX_ROCK, { 0.8f, 0.68f, 0.55f }, { 0.75f, 0.7f, 0.65f }, { 0.65f, 0.55f, 0.45f },
    { 0.85f, 0.5f, 0.2f }, { 0.75f, 0.35f, 0.15f }, 1, 1 },
  { "coast", TEX_GRASS, TEX_ROCK, TEX_ROCK, { 0.85f, 0.95f, 0.8f }, { 0.55f, 0.57f, 0.58f }, { 0.5f, 0.55f, 0.58f },
    { 0.5f, 0.65f, 0.4f }, { 0.65f, 0.7f, 0.45f }, 1, 0.95f },
  { "cosmic", TEX_ROCK, TEX_ROCK, TEX_ROCK, { 0.8f, 0.75f, 1 }, { 0.55f, 0.5f, 0.8f }, { 0.6f, 0.5f, 0.9f },
    { 0.5f, 0.9f, 1 }, { 1, 0.5f, 0.95f }, 1, 0.5f },
  { "scary", TEX_DIRT, TEX_ROCK, TEX_ROCK, { 0.55f, 0.5f, 0.48f }, { 0.42f, 0.42f, 0.45f }, { 0.25f, 0.25f, 0.3f },
    { 0.45f, 0.42f, 0.35f }, { 0.35f, 0.33f, 0.28f }, 1, 1 },
  { "xmas", TEX_SNOW, TEX_ROCK, TEX_ROCK, { 1, 1, 1 }, { 0.62f, 0.66f, 0.72f }, { 0.85f, 0.9f, 0.98f },
    { 0.5f, 0.6f, 0.45f }, { 0.4f, 0.5f, 0.38f }, 1, 0.6f },
  { "moon", TEX_SAND, TEX_ROCK, TEX_ROCK, { 0.62f, 0.62f, 0.64f }, { 0.5f, 0.5f, 0.52f }, { 0.55f, 0.55f, 0.57f },
    { 0, 0, 0 }, { 0, 0, 0 }, 3, 1 },
  { "beach", TEX_SAND, TEX_SANDSTONE, TEX_SAND, { 1.05f, 0.96f, 0.82f }, { 1, 0.85f, 0.68f }, { 1, 0.9f, 0.72f },
    { 0.6f, 0.7f, 0.4f }, { 0.75f, 0.72f, 0.45f }, 3, 1 },
  { "mars", TEX_SAND, TEX_SANDSTONE, TEX_SANDSTONE, { 0.9f, 0.5f, 0.32f }, { 0.85f, 0.45f, 0.3f }, { 0.85f, 0.48f, 0.32f },
    { 0, 0, 0 }, { 0, 0, 0 }, 0, 1 },
};
/* themes where nothing grows along the track */
static bool barren(const theme *th) { return !strcmp(th->name, "moon") || !strcmp(th->name, "mars"); }

static const theme *theme_for(const char *name) {
  for (int i = 0; i < (int)CW_ARRAY_LEN(THEMES); i++)
    if (!strcmp(THEMES[i].name, name)) return &THEMES[i];
  return &THEMES[0];
}

/* ── a growable mesh ─────────────────────────────────────────────────── */

typedef struct mb {
  float *pos, *nor, *uv, *col;
  uint32_t *idx;
  int nv, ni, cv, ci;
  bool colors;
} mb;
static int mb_v(mb *b, t3_vec3 p, t3_vec3 n, float u, float v, float shade) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 1024;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->cv);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->cv);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cv);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->cv);
  }
  float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *U = b->uv + b->nv * 2, *C = b->col + b->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x / l, N[1] = n.y / l, N[2] = n.z / l;
  U[0] = u, U[1] = v;
  C[0] = C[1] = C[2] = shade;
  return b->nv++;
}
static void mb_t(mb *b, int a, int c, int d) {
  if (b->ni + 3 > b->ci) {
    b->ci = b->ci ? b->ci * 2 : 3072;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->ci);
  }
  b->idx[b->ni++] = (uint32_t)a;
  b->idx[b->ni++] = (uint32_t)c;
  b->idx[b->ni++] = (uint32_t)d;
}
static t3_geometry *mb_done(mb *b) {
  t3_geometry *g = t3_geometry_new();
  set_attr(g, T3_ATTR_POSITION, t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3));
  set_attr(g, T3_ATTR_NORMAL, t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3));
  set_attr(g, T3_ATTR_UV, t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2));
  if (b->colors) set_attr(g, T3_ATTR_COLOR, t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3));
  set_index(g, t3_attribute_new(T3_UINT32, b->idx, b->ni, 1));
  free(b->pos);
  free(b->nor);
  free(b->uv);
  free(b->col);
  free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

static t3_mesh *add(t3_object *root, t3_geometry *g, t3_material *m, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = true;
  t3_object_add(root, me);
  t3_release(me);
  return me;
}

static t3_material *mat(int tex, const float c[3], float rx, float ry, float nscale, float rough) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = lin(c[0], c[1], c[2]);
  m->roughness = rough;
  tex_apply(m, tex, rx, ry, nscale, 1);
  return m;
}

/* ── distant ground: a graphics setting (view_ground_detail) ───────────
 * The bank behind the track, the plains and the landforms cover over half
 * the frame, farther from the camera than the riding line, where their
 * normal, roughness and occlusion maps show least. Simple draws them with the
 * colour map alone (a cheaper program: no normal mapping, three fewer
 * texture reads a pixel; a fifth of the scene pass at 1080p); Full with every
 * map.
 * The materials are shared (merged meshes keep them), so the maps come and
 * go on the material itself. */
enum { MAX_FAR_MATS = 8 };
static struct far_mat { t3_material *m; t3_texture *nmap, *rmap, *ao; } far_mats[MAX_FAR_MATS];
static int n_far_mats;
static bool ground_full = true;
static void far_detail(struct far_mat *f, bool full) {
  t3_material_set_texture(f->m, T3_NORMAL_MAP, full ? f->nmap : NULL);
  t3_material_set_texture(f->m, T3_ROUGHNESS_MAP, full ? f->rmap : NULL);
  t3_material_set_texture(f->m, T3_AO_MAP, full ? f->ao : NULL);
}
static t3_material *far_mat(t3_material *m) {
  if (n_far_mats >= MAX_FAR_MATS) return m;
  struct far_mat *f = &far_mats[n_far_mats++];
  f->m = m;
  f->nmap = m->normal_map;
  f->rmap = m->roughness_map;
  f->ao = m->ao_map;
  t3_retain(m);
  t3_retain(f->nmap);
  t3_retain(f->rmap);
  t3_retain(f->ao);
  if (!ground_full) far_detail(f, false);
  return m;
}
void view_ground_detail(bool full) {
  if (full == ground_full) return;
  ground_full = full;
  for (int i = 0; i < n_far_mats; i++) far_detail(&far_mats[i], full);
}
static void far_mats_free(void) {
  for (int i = 0; i < n_far_mats; i++) {
    far_detail(&far_mats[i], true);
    t3_release(far_mats[i].nmap);
    t3_release(far_mats[i].rmap);
    t3_release(far_mats[i].ao);
    t3_release(far_mats[i].m);
  }
  n_far_mats = 0;
}

/* ── the track ribbon ──────────────────────────────────────────────────
 * top: the riding surface across z -W..W with a rounded lip at the front;
 * face: from the lip straight down to the bedrock, uv in world metres so the
 * strata stay level. UVs: top u along x, v across z. */
/* the open part of the track at x, back edge zb to front edge zf: the four
 * lanes, or fewer where it narrows, the edges sliding in over NARROW_TAPER
 * before a barrier and out over WIDEN_TAPER after */
static void open_edges(const narrow *nw, int n, float x, float *zb, float *zf) {
  const float B0 = lane_z(-0.5f), F0 = lane_z(LANES - 0.5f);
  *zb = B0, *zf = F0;
  for (int i = 0; i < n; i++) {
    float w = 0;
    bool followed = false;
    for (int j = 0; j < n; j++) followed |= j != i && fabsf(nw[j].x0 - nw[i].x1) < 0.01f;
    if (x >= nw[i].x0 && x < nw[i].x1) w = 1;
    else if (nw[i].wall && x >= nw[i].x0 - NARROW_TAPER && x < nw[i].x0) w = (x - (nw[i].x0 - NARROW_TAPER)) / NARROW_TAPER;
    else if (!followed && x >= nw[i].x1 && x < nw[i].x1 + WIDEN_TAPER) w = 1 - (x - nw[i].x1) / WIDEN_TAPER;
    if (w <= 0) continue;
    w = cw_smoothstep(0, 1, w);
    *zb = fmaxf(*zb, B0 + (lane_z(nw[i].lo - 0.5f) - B0) * w);
    *zf = fminf(*zf, F0 + (lane_z(nw[i].hi + 0.5f) - F0) * w);
  }
}

static bool in_loop(const track_desc *t, float x, float pad) {
  for (int i = 0; i < t->loop_count; i++)
    if (fabsf(x - t->loops[i * 3]) < t->loops[i * 3 + 2] + pad) return true;
  return false;
}

/* the lanes painted on the surface: solid edge lines (they close in where the
 * track narrows), dashed lines between open lanes, red and white hatching
 * over a closed lane, a strip each a hair above the ground (over the ramps'
 * decks too); cones along a closing edge and a striped barrier where a lane
 * ends */
static void lane_stripes(t3_object *root, const track_desc *t) {
  narrow nw[MAX_NARROWS];
  int nn = track_narrows(t, nw);
  mb m = { 0 }, hatch = { 0 };
  const float HW = 0.04f, DASH = 2.0f, GAP = 1.4f, STEP = 0.25f;
  float x0 = t->pieces[0].pts[0], x1 = t->length + 60;
  /* lines: 0 the back edge, 1..LANES-1 between lanes, LANES the front edge */
  for (int b = 0; b <= LANES; b++) {
    int run = -1;
    for (float x = x0; x <= x1; x += STEP) {
      bool f;
      float y = track_height_at(x, &f) + 0.015f, zb, zf;
      for (int q = 0; q < t->kicker_count; q++) /* over a ramp's deck */
        if (x >= t->kickers[q * 3] && x <= t->kickers[q * 3 + 1]) y += 0.045f;
      open_edges(nw, nn, x, &zb, &zf);
      float zc = b == 0 ? zb : b == LANES ? zf : lane_z(b - 0.5f);
      bool on = f && (b == 0 || b == LANES || (fmodf(x + 1000, DASH + GAP) < DASH && zc > zb + 0.1f && zc < zf - 0.1f));
      if (in_loop(t, x, 0.3f) && b > 0 && b < LANES) on = false;
      if (!on) {
        run = -1;
        continue;
      }
      int v = mb_v(&m, (t3_vec3){ x, y, zc - HW }, (t3_vec3){ 0, 1, 0 }, 0, 0, 1);
      mb_v(&m, (t3_vec3){ x, y, zc + HW }, (t3_vec3){ 0, 1, 0 }, 1, 0, 1);
      if (run >= 0) mb_t(&m, run, run + 1, v + 1), mb_t(&m, run, v + 1, v);
      run = v;
    }
  }
  /* hatching over the closed lanes of a narrow with a barrier: red bars across, every metre */
  for (int i = 0; i < nn; i++) {
    if (!nw[i].wall) continue;
    for (float x = nw[i].x0 - NARROW_TAPER * 0.5f; x < nw[i].x1 - 0.5f; x += 1.0f) {
      if (in_loop(t, x + 0.5f, 0.6f)) break;
      float zb, zf, y = track_height_at(x, NULL) + 0.016f, y2 = track_height_at(x + 0.45f, NULL) + 0.016f;
      open_edges(nw, nn, x, &zb, &zf);
      float spans[2][2] = { { lane_z(-0.5f), zb - 0.12f }, { zf + 0.12f, lane_z(LANES - 0.5f) } };
      for (int sp = 0; sp < 2; sp++) {
        if (spans[sp][1] - spans[sp][0] < 0.2f) continue;
        int v = mb_v(&hatch, (t3_vec3){ x, y, spans[sp][0] }, (t3_vec3){ 0, 1, 0 }, 0, 0, 1);
        mb_v(&hatch, (t3_vec3){ x, y, spans[sp][1] }, (t3_vec3){ 0, 1, 0 }, 1, 0, 1);
        mb_v(&hatch, (t3_vec3){ x + 0.45f, y2, spans[sp][1] }, (t3_vec3){ 0, 1, 0 }, 1, 1, 1);
        mb_v(&hatch, (t3_vec3){ x + 0.45f, y2, spans[sp][0] }, (t3_vec3){ 0, 1, 0 }, 0, 1, 1);
        mb_t(&hatch, v, v + 1, v + 2), mb_t(&hatch, v, v + 2, v + 3);
      }
    }
  }
  t3_material *paint = t3_mesh_standard_material_new(0xffffff);
  paint->color = lin(0.92f, 0.92f, 0.88f);
  paint->roughness = 0.7f;
  t3_material *red = t3_mesh_standard_material_new(0xffffff);
  red->color = lin(0.85f, 0.12f, 0.08f);
  red->roughness = 0.7f;
  t3_geometry *g = mb_done(&m), *gh = mb_done(&hatch);
  add(root, g, paint, false);
  if (hatch.nv) add(root, gh, red, false);
  t3_release(g);
  t3_release(gh);
  /* cones along the closing edges (not round a loop), a barrier where a lane ends */
  t3_geometry *cone = t3_cone_geometry_new(0.13f, 0.42f, 12, 1, false), *blk = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  t3_material *orange = t3_mesh_standard_material_new(0xffffff), *white = t3_mesh_standard_material_new(0xffffff);
  orange->color = lin(1, 0.38f, 0.05f);
  orange->roughness = 0.5f;
  white->color = lin(0.95f, 0.95f, 0.92f);
  white->roughness = 0.5f;
  for (int i = 0; i < nn; i++) {
    if (!nw[i].wall) continue;
    for (float x = nw[i].x0 - NARROW_TAPER; x < nw[i].x1 + WIDEN_TAPER; x += 2.0f) {
      if (in_loop(t, x, 1.5f)) continue;
      bool f;
      float zb, zf, y = track_height_at(x, &f);
      if (!f) continue;
      open_edges(nw, nn, x, &zb, &zf);
      float zs[2] = { zb - 0.16f, zf + 0.16f };
      bool use[2] = { zb > lane_z(-0.5f) + 0.1f, zf < lane_z(LANES - 0.5f) - 0.1f };
      for (int k = 0; k < 2; k++) {
        if (!use[k]) continue;
        t3_mesh *c = add(root, cone, orange, true);
        c->base.position = (t3_vec3){ x, y + 0.21f, zs[k] };
      }
    }
    /* the barrier: striped blocks across the closed lanes at x0 */
    float y = track_height_at(nw[i].x0, NULL);
    float spans[2][2] = { { lane_z(-0.5f), lane_z(nw[i].lo - 0.5f) }, { lane_z(nw[i].hi + 0.5f), lane_z(LANES - 0.5f) } };
    for (int sp = 0; sp < 2; sp++) {
      float za = spans[sp][0] + 0.02f, zz = spans[sp][1] - 0.02f;
      if (zz - za < 0.3f) continue;
      int nseg = (int)((zz - za) / 0.3f);
      for (int k = 0; k < nseg; k++) {
        t3_mesh *bk = add(root, blk, k & 1 ? white : red, true);
        bk->base.position = (t3_vec3){ nw[i].x0 + 0.2f, y + 0.5f, za + (k + 0.5f) * (zz - za) / nseg };
        bk->base.scale = (t3_vec3){ 0.35f, 1.0f, (zz - za) / nseg };
      }
    }
  }
  t3_release(cone);
  t3_release(blk);
  t3_release(orange);
  t3_release(white);
  t3_release(paint);
  t3_release(red);
}

/* the partial ramps: a plank deck over their lanes, closed sides (so the
 * lanes beside ride past a clean wall), a white line along each top edge */
static void ramps_build(t3_object *root, const track_desc *t, const theme *th) {
  if (!t->ramp_count) return;
  mb deck = { 0 }, side = { 0 }, edge = { 0 };
  for (int i = 0; i < t->ramp_count; i++) {
    const float *r = &t->ramps[i * 7];
    int lo = (int)r[5], hi = (int)r[6];
    float len = r[1] + r[3] + r[4], za = lane_z(lo - 0.5f) + 0.04f, zb = lane_z(hi + 0.5f) - 0.04f;
    int n = (int)(len / 0.2f) + 1, bd = deck.nv;
    for (int k = 0; k <= n; k++) {
      float x = r[0] + len * k / n, g = track_height_at(x, NULL), y = g + track_ramp_h(t, x, lo);
      float xa = x - 0.05f, xb = x + 0.05f;
      float ya = track_height_at(xa, NULL) + track_ramp_h(t, xa, lo), yb = track_height_at(xb, NULL) + track_ramp_h(t, xb, lo);
      t3_vec3 nn = { -(yb - ya), 0.1f, 0 };
      mb_v(&deck, (t3_vec3){ x, y, za }, nn, (x - r[0]) / 2.5f, 0, 1);
      mb_v(&deck, (t3_vec3){ x, y, zb }, nn, (x - r[0]) / 2.5f, (zb - za) / 2.5f, 1);
      if (k > 0) mb_t(&deck, bd + (k - 1) * 2, bd + (k - 1) * 2 + 1, bd + k * 2 + 1), mb_t(&deck, bd + (k - 1) * 2, bd + k * 2 + 1, bd + k * 2);
      if (k > 0) { /* the two sides, ground to deck */
        float px = r[0] + len * (k - 1) / n, pg = track_height_at(px, NULL), py = pg + track_ramp_h(t, px, lo);
        for (int sd = 0; sd < 2; sd++) {
          float z = sd ? zb : za;
          int v = mb_v(&side, (t3_vec3){ px, pg - 0.05f, z }, (t3_vec3){ 0, 0, sd ? 1.0f : -1.0f }, px / 2, 0, 1);
          mb_v(&side, (t3_vec3){ px, py, z }, (t3_vec3){ 0, 0, sd ? 1.0f : -1.0f }, px / 2, py - pg, 1);
          mb_v(&side, (t3_vec3){ x, y, z }, (t3_vec3){ 0, 0, sd ? 1.0f : -1.0f }, x / 2, y - g, 1);
          mb_v(&side, (t3_vec3){ x, g - 0.05f, z }, (t3_vec3){ 0, 0, sd ? 1.0f : -1.0f }, x / 2, 0, 1);
          if (sd) mb_t(&side, v, v + 3, v + 2), mb_t(&side, v, v + 2, v + 1);
          else mb_t(&side, v, v + 1, v + 2), mb_t(&side, v, v + 2, v + 3);
          /* the white top edge */
          float ze = sd ? z - 0.09f : z + 0.01f;
          int e = mb_v(&edge, (t3_vec3){ px, py + 0.012f, ze }, (t3_vec3){ 0, 1, 0 }, 0, 0, 1);
          mb_v(&edge, (t3_vec3){ px, py + 0.012f, ze + 0.08f }, (t3_vec3){ 0, 1, 0 }, 1, 0, 1);
          mb_v(&edge, (t3_vec3){ x, y + 0.012f, ze + 0.08f }, (t3_vec3){ 0, 1, 0 }, 1, 1, 1);
          mb_v(&edge, (t3_vec3){ x, y + 0.012f, ze }, (t3_vec3){ 0, 1, 0 }, 0, 1, 1);
          mb_t(&edge, e, e + 1, e + 2), mb_t(&edge, e, e + 2, e + 3);
        }
      }
    }
  }
  t3_material *wood = mat(TEX_WOOD, (float[3]){ 0.95f, 0.86f, 0.72f }, 1, 1, 1.2f, 0.8f);
  t3_material *dark = mat(TEX_WOOD, (float[3]){ 0.5f, 0.4f, 0.32f }, 1, 1, 1.2f, 0.9f);
  t3_material *paint = t3_mesh_standard_material_new(0xffffff);
  paint->color = lin(0.95f, 0.95f, 0.9f);
  paint->roughness = 0.6f;
  (void)th;
  t3_geometry *g1 = mb_done(&deck), *g2 = mb_done(&side), *g3 = mb_done(&edge);
  add(root, g1, wood, true);
  add(root, g2, dark, true);
  add(root, g3, paint, false);
  t3_release(g1), t3_release(g2), t3_release(g3);
  t3_release(wood), t3_release(dark), t3_release(paint);
}

static void ribbon(t3_object *root, const track_desc *t, const theme *th) {
  mb top = { 0 }, face = { 0 };
  float bottom = t->min_y - 60;
  const float W = TRACK_HALF_W, LIP = 0.18f;
  for (int i = 0; i < t->piece_count; i++) {
    const track_piece *pc = &t->pieces[i];
    const float *p = pc->pts;
    int base_t = top.nv, base_f = face.nv;
    for (int k = 0; k < pc->count; k++) {
      float x = p[k * 2], y = p[k * 2 + 1];
      int a = k > 0 ? k - 1 : k, c = k < pc->count - 1 ? k + 1 : k;
      float tx = p[c * 2] - p[a * 2], ty = p[c * 2 + 1] - p[a * 2 + 1];
      float tl = sqrtf(tx * tx + ty * ty);
      t3_vec3 n = { -ty / tl, tx / tl, 0 };
      float u = x / 4;
      /* back edge, the surface, the lip bending down toward the camera */
      mb_v(&top, (t3_vec3){ x, y - 0.05f, -W - 0.3f }, (t3_vec3){ n.x, n.y, -0.8f }, u, -0.1f, 1);
      mb_v(&top, (t3_vec3){ x, y, -W }, n, u, 0, 1);
      mb_v(&top, (t3_vec3){ x, y, W - LIP }, n, u, 0.9f, 1);
      mb_v(&top, (t3_vec3){ x, y - LIP * 0.4f, W - LIP * 0.2f }, (t3_vec3){ n.x, n.y * 0.6f, 0.7f }, u, 1.0f, 1);
      mb_v(&top, (t3_vec3){ x, y - LIP, W }, (t3_vec3){ 0, 0, 1 }, u, 1.08f, 1);
      /* the face: below the lip down to the bedrock */
      mb_v(&face, (t3_vec3){ x, y - LIP, W }, (t3_vec3){ n.x * 0.3f, 0, 1 }, x / 5, (y - LIP) / 5, 1);
      mb_v(&face, (t3_vec3){ x, bottom, W + 1.5f }, (t3_vec3){ 0, 0, 1 }, x / 5, bottom / 5, 1);
      if (k > 0) {
        for (int r = 0; r < 4; r++) {
          int a0 = base_t + (k - 1) * 5 + r, b0 = base_t + k * 5 + r;
          mb_t(&top, a0, a0 + 1, b0 + 1);
          mb_t(&top, a0, b0 + 1, b0);
        }
        int f0 = base_f + (k - 1) * 2, f1 = base_f + k * 2;
        mb_t(&face, f0, f0 + 1, f1 + 1);
        mb_t(&face, f0, f1 + 1, f1);
      }
    }
    /* the piece's ends: cliff walls facing out of the gaps */
    for (int e = 0; e < 2; e++) {
      int k = e ? pc->count - 1 : 0;
      float x = p[k * 2], y = p[k * 2 + 1], nx = e ? 1.0f : -1.0f;
      int v0 = mb_v(&face, (t3_vec3){ x, y, -W - 0.3f }, (t3_vec3){ nx, 0, 0 }, -W / 7, y / 7, 1);
      int v1 = mb_v(&face, (t3_vec3){ x, y - LIP, W }, (t3_vec3){ nx, 0, 0 }, W / 7, y / 7, 1);
      int v2 = mb_v(&face, (t3_vec3){ x, bottom, W + 1.5f }, (t3_vec3){ nx, 0, 0 }, W / 7, bottom / 7, 1);
      int v3 = mb_v(&face, (t3_vec3){ x, bottom, -W - 0.3f }, (t3_vec3){ nx, 0, 0 }, -W / 7, bottom / 7, 1);
      if (e) {
        mb_t(&face, v0, v1, v2);
        mb_t(&face, v0, v2, v3);
      } else {
        mb_t(&face, v0, v2, v1);
        mb_t(&face, v0, v3, v2);
      }
    }
  }
  t3_material *mt = mat(th->top_tex, th->top, 1, 1, 1.2f, th->top_rough);
  t3_material *mf = mat(th->face_tex, th->face, 1, 1, 1.3f, 0.95f);
  /* the surface tiles every 4 m along and once across */
  t3_geometry *gt = mb_done(&top), *gf = mb_done(&face);
  add(root, gt, mt, true);
  add(root, gf, mf, true);
  t3_release(gt);
  t3_release(gf);
  t3_release(mt);
  t3_release(mf);

  /* built ramps: a plank deck over each kicker with two posts under its lip */
  t3_material *wood = t3_mesh_standard_material_new(0xffffff);
  wood->color = lin(0.95f, 0.88f, 0.8f);
  wood->roughness = 0.8f;
  tex_apply(wood, TEX_WOOD, 1, 1, 1.2f, 1);
  mb deck = { 0 };
  /* no carpentry on the Moon or Mars: their ramps are the ground itself */
  for (int k = 0; k < t->kicker_count && !barren(th); k++) {
    float x0 = t->kickers[k * 3], x1 = t->kickers[k * 3 + 1];
    int n = (int)((x1 - x0) / 0.25f);
    int base = deck.nv;
    for (int i = 0; i <= n; i++) {
      float x = x0 + (x1 - x0) * i / n;
      float y = track_height_at(x, NULL) + 0.04f;
      float xa = x - 0.05f, xb = x + 0.05f;
      float ya = track_height_at(xa, NULL), yb = track_height_at(xb, NULL);
      t3_vec3 nn = { -(yb - ya), 0.1f, 0 };
      mb_v(&deck, (t3_vec3){ x, y, -TRACK_HALF_W + 0.15f }, nn, (x - x0) / 2.5f, 0, 1);
      mb_v(&deck, (t3_vec3){ x, y, TRACK_HALF_W - 0.25f }, nn, (x - x0) / 2.5f, 1, 1);
      if (i > 0) {
        mb_t(&deck, base + (i - 1) * 2, base + (i - 1) * 2 + 1, base + i * 2 + 1);
        mb_t(&deck, base + (i - 1) * 2, base + i * 2 + 1, base + i * 2);
      }
    }
  }
  if (deck.nv) {
    t3_geometry *g = mb_done(&deck);
    add(root, g, wood, true);
    t3_release(g);
  } else t3_release(mb_done(&deck));
  t3_release(wood);
  lane_stripes(root, t);
  ramps_build(root, t, th);
}

/* ── the far landforms ─────────────────────────────────────────────────
 * mesas: flat-topped buttes with sheer strata walls (a lathe with a cap);
 * peaks: snowy cones; towers: city blocks; domes: dunes / canopy hills */
static t3_geometry *geo_mesa(float r, float h) {
  t3_vec2 p[] = { { r * 1.25f, 0 }, { r * 1.12f, h * 0.18f }, { r * 1.0f, h * 0.3f }, { r * 0.98f, h * 0.7f }, { r * 0.93f, h * 0.96f },
                  { r * 0.85f, h }, { 0, h } };
  return t3_lathe_geometry_new(p, (int)CW_ARRAY_LEN(p), 9, 0, 6.2831853f);
}
static t3_geometry *geo_peak(float r, float h) {
  t3_vec2 p[] = { { r, 0 }, { r * 0.7f, h * 0.35f }, { r * 0.38f, h * 0.7f }, { r * 0.12f, h * 0.95f }, { 0, h } };
  return t3_lathe_geometry_new(p, (int)CW_ARRAY_LEN(p), 7, 0, 6.2831853f);
}

static void landforms(t3_object *root, const track_desc *t, const theme *th) {
  t3_material *m = far_mat(mat(th->far_tex, th->far, 3, 3, 1.4f, 0.95f));
  float x0 = -120, x1 = t->length + 160;
  for (int layer = 0; layer < 3; layer++) {
    float z = -70 - layer * 70, spacing = 38 + layer * 22;
    for (float x = x0; x < x1; x += spacing) {
      float jx = (rnd() - 0.5f) * spacing * 0.6f, r = 14 + rnd() * 18 + layer * 8, h = 18 + rnd() * 26 + layer * 12;
      float base = track_height_at(cw_clampf(x, 0, t->length), NULL) - 25 - layer * 6;
      float yaw = rnd() * 6.2831853f, sq = 0.6f + rnd() * 0.5f;
      t3_geometry *g = th->far_kind == 1 ? geo_peak(r, h * 1.6f) : th->far_kind == 3 ? geo_peak(r * 1.6f, h * 0.45f) : geo_mesa(r, h);
      if (th->far_kind == 2) {
        t3_release(g);
        g = t3_box_geometry_new(r * 0.8f, h * 2.2f, r * 0.8f, 1, 1, 1);
      }
      t3_mesh *me = add(root, g, m, false);
      me->base.position = (t3_vec3){ x + jx, base + (th->far_kind == 2 ? h * 1.1f : 0), z + (rnd() - 0.5f) * 30 };
      me->base.scale = (t3_vec3){ 1, 1, sq };
      t3_object_set_rotation(&me->base, 0, yaw, 0);
      t3_release(g);
    }
  }
  t3_release(m);
}


/* ── the bank behind the track ──────────────────────────────────────────
 * The ground rises from the back edge of the riding surface into the hillside
 * the line is cut along: a smoothed copy of the track's height (bridged over
 * the gaps) plus a slope and broad undulations, textured like the cliff face
 * (world uvs, so strata stay level). Scenery behind the track sits on it. */
#define GH_X0 -80.0f
static float *ground_h;
static int gh_n;

static void ground_build(const track_desc *t) {
  free(ground_h);
  gh_n = (int)(t->length - GH_X0 + 120);
  ground_h = malloc(sizeof(float) * (size_t)gh_n);
  bool *ok = malloc((size_t)gh_n);
  for (int i = 0; i < gh_n; i++) {
    bool f;
    ground_h[i] = track_height_at(GH_X0 + i, &f);
    ok[i] = f;
  }
  /* bridge gaps and the ends */
  int first = -1, last = -1;
  for (int i = 0; i < gh_n; i++)
    if (ok[i]) {
      if (first < 0) first = i;
      last = i;
    }
  for (int i = 0; i < gh_n; i++) {
    if (ok[i]) continue;
    if (i < first) ground_h[i] = ground_h[first];
    else if (i > last) ground_h[i] = ground_h[last];
    else {
      int a = i, b = i;
      while (!ok[a]) a--;
      while (!ok[b]) b++;
      ground_h[i] = ground_h[a] + (ground_h[b] - ground_h[a]) * (float)(i - a) / (float)(b - a);
    }
  }
  free(ok);
  float *tmp = malloc(sizeof(float) * (size_t)gh_n);
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < gh_n; i++) {
      float s = 0;
      int n = 0;
      for (int k = -4; k <= 4; k++)
        if (i + k >= 0 && i + k < gh_n) s += ground_h[i + k], n++;
      tmp[i] = s / n;
    }
    memcpy(ground_h, tmp, sizeof(float) * (size_t)gh_n);
  }
  free(tmp);
}
static float ground_at(float x) {
  float f = x - GH_X0;
  if (f <= 0) return ground_h[0];
  if (f >= gh_n - 1) return ground_h[gh_n - 1];
  int i = (int)f;
  return ground_h[i] + (ground_h[i + 1] - ground_h[i]) * (f - i);
}
/* height of the bank at (x, z <= -W) */
static float bank_y(float x, float z, int kind) {
  float d = -z - TRACK_HALF_W - 0.3f;
  if (d < 0) d = 0;
  float near = track_height_at(x, NULL);
  bool f;
  near = track_height_at(x, &f);
  if (!f) near = ground_at(x);
  float base = near + (ground_at(x) - near) * cw_clampf(d / 4, 0, 1);
  float und = 1.6f * cw_sin(x * 0.11f + z * 0.07f) + 0.9f * cw_sin(x * 0.27f - z * 0.19f + 1.3f) + 0.35f * cw_sin(x * 0.9f + z * 0.6f);
  float dd = d < 10 ? d : 10, far = d > 10 ? d - 10 : 0;
  float rise = kind == 1 ? dd * 0.4f + far * 0.25f : kind == 2 ? d * 0.04f : dd * 0.22f + far * 0.06f;
  return base - 0.06f + rise + und * cw_clampf(d / 6, 0, 1) + (d > 0 ? cw_clampf(d * 0.6f, 0, 0.3f) : 0);
}

static int bank_kind; /* the theme's bank (bank_y's kind) */
float view_bank_y(float x, float z) { return bank_y(x, z, bank_kind); }

static void bank(t3_object *root, const track_desc *t, const theme *th) {
  static const float ZS[] = { 0, 0.4f, 1, 2, 3.5f, 5.5f, 8, 11, 15, 20, 27, 36, 48, 62 };
  /* rows 0..FAR_ROW the track's back edge, FAR_ROW.. (a metre back and on,
   * farther from the camera than the riding line) distant ground */
  enum { NZ = (int)CW_ARRAY_LEN(ZS), FAR_ROW = 2 };
  /* the bank: the riding surface's ground, paler (dust and sun) */
  float pale[3] = { th->top[0] * 1.08f, th->top[1] * 1.06f, th->top[2] * 1.02f };
  float x0 = GH_X0 + 4, x1 = t->length + 100;
  int nx = (int)((x1 - x0) / 1.0f) + 1;
  for (int part = 0; part < 2; part++) {
    int j0 = part ? FAR_ROW : 0, j1 = part ? NZ : FAR_ROW + 1, nz = j1 - j0;
    mb b = { 0 };
    for (int i = 0; i < nx; i++) {
      float x = x0 + i * 1.0f;
      for (int j = j0; j < j1; j++) {
        float z = -TRACK_HALF_W - 0.3f - ZS[j];
        float y = bank_y(x, z, th->far_kind == 1 ? 1 : 0) - (j == 0 ? 0.08f : 0);
        float yx = bank_y(x + 0.5f, z, th->far_kind == 1 ? 1 : 0) - bank_y(x - 0.5f, z, th->far_kind == 1 ? 1 : 0);
        float yz = bank_y(x, z + 0.5f, th->far_kind == 1 ? 1 : 0) - bank_y(x, z - 0.5f, th->far_kind == 1 ? 1 : 0);
        mb_v(&b, (t3_vec3){ x, y, z }, (t3_vec3){ -yx, 1, -yz }, x / 5, z / 5, 1);
        if (i > 0 && j > j0) {
          int a = (i - 1) * nz + j - j0 - 1, c = i * nz + j - j0 - 1;
          mb_t(&b, a, c + 1, a + 1);
          mb_t(&b, a, c, c + 1);
        }
      }
    }
    t3_material *m = mat(th->top_tex, pale, 1, 1, 1.0f, 1);
    if (part) far_mat(m);
    t3_geometry *g = mb_done(&b);
    add(root, g, m, false);
    t3_release(g);
    t3_release(m);
  }
}

/* ── boulders and grass along the track ───────────────────────────────── */

typedef struct sway { float x, y, z, yaw, s, sy, flex, col[3], c, sn; } sway; /* c, sn: cw_cos / cw_sin of yaw, once */
enum { TUFTS = 5000, BOULDERS = 160 };
static sway tuft_at[TUFTS];
static int n_tufts, wind_t;
static float density = 1; /* view_density */
static float tuft_reach; /* a tuft's bounding radius per unit of its scale, bent as far as the wind goes */
/* the tufts in chunks along the track, TUFT_CHUNK metres each: one
 * InstancedMesh per chunk, its instances tuft_at[start..start+n) */
#define TUFT_CHUNK 24.0f
#define MAX_TUFT_CHUNKS 96
typedef struct tuft_chunk { t3_instanced_mesh *im; int start, n, count; t3_sphere sph; } tuft_chunk;
static tuft_chunk tch[MAX_TUFT_CHUNKS];
static int n_tch;
uint32_t wind_hash;

static void blade(mb *b, t3_vec3 base, float ang, float h, float curve, float w0) {
  enum { SEG = 3 };
  float dx = cw_cos(ang), dz = cw_sin(ang), sx = -dz, sz = dx;
  int prev[2] = { -1, -1 };
  for (int k = 0; k <= SEG; k++) {
    float t = (float)k / SEG;
    t3_vec3 c = { base.x + dx * curve * t * t, base.y + h * t, base.z + dz * curve * t * t };
    t3_vec3 n = { dx * 0.45f, 1, dz * 0.45f };
    float shade = 0.4f + 0.6f * t, w = w0 * (1 - t) * 0.5f;
    if (k == SEG) {
      int tip = mb_v(b, c, n, 0.5f, 1, shade);
      mb_t(b, prev[0], prev[1], tip);
      mb_t(b, prev[0], tip, prev[1]);
      break;
    }
    int l = mb_v(b, (t3_vec3){ c.x - sx * w, c.y, c.z - sz * w }, n, 0, t, shade);
    int r = mb_v(b, (t3_vec3){ c.x + sx * w, c.y, c.z + sz * w }, n, 1, t, shade);
    if (prev[0] >= 0) {
      mb_t(b, prev[0], prev[1], r);
      mb_t(b, prev[0], r, l);
      mb_t(b, prev[0], r, prev[1]);
      mb_t(b, prev[0], l, r);
    }
    prev[0] = l;
    prev[1] = r;
  }
}

static float hr(uint32_t *h) {
  *h = sh(*h + 0x9e3779b9u);
  return (float)(*h & 0xffffff) / 16777216.0f;
}

static t3_geometry *geo_tuft(void) {
  mb b = { .colors = true };
  uint32_t h = 3;
  for (int i = 0; i < 9; i++) {
    float ang = i * 0.698f + (hr(&h) - 0.5f) * 0.6f, r0 = 0.01f + hr(&h) * 0.04f;
    blade(&b, (t3_vec3){ cw_cos(ang) * r0, 0, cw_sin(ang) * r0 }, ang, 0.3f + hr(&h) * 0.25f, 0.06f + hr(&h) * 0.12f,
          0.025f + hr(&h) * 0.01f);
  }
  return mb_done(&b);
}

static const float WIND_X = 0.94f, WIND_Z = 0.34f;
static float gust(float x, float z, float t) {
  float p = x * WIND_X + z * WIND_Z;
  float g1 = 0.5f + 0.5f * cw_sin(6.2831853f * (p / 24 - t / 7.0f));
  float g2 = 0.5f + 0.5f * cw_sin(6.2831853f * (p / 13 - t / 9.3f) + 1.7f);
  float g = g1 * 0.65f + g2 * 0.35f;
  return g * g;
}

/* the wind's pose of tuft a at time t (seconds) */
static void sway_matrix(const sway *a, float t, float *m) {
  float bend = (0.06f + 0.36f * gust(a->x, a->z, t) + 0.04f * cw_sin(t * 2.3f + a->x * 1.7f)) * a->flex;
  float c = a->c, s = a->sn, kx = bend * WIND_X, kz = bend * WIND_Z;
  float r[16] = { c * a->s, 0, -s * a->s, 0, 0, a->sy, 0, 0, s * a->s, 0, c * a->s, 0, a->x, a->y, a->z, 1 };
  for (int col = 0; col < 3; col++) {
    r[col * 4 + 0] += kx * r[col * 4 + 1];
    r[col * 4 + 2] += kz * r[col * 4 + 1];
  }
  memcpy(m, r, sizeof r);
}

/* Only the chunks the camera can see are posed and drawn; the job pool
 * poses their tufts (each a function of its own sway and t, so every host
 * alike). The renderer culls each drawn chunk again on its instances'
 * bounds. The whole track's grass used to be swayed every tick and drawn
 * every frame. */
typedef struct sway_job { const int *vis; const int *first; int nvis; float t; } sway_job;
static void sway_range(int begin, int end, int worker, void *ctx) {
  (void)worker;
  const sway_job *j = ctx;
  int v = 0;
  for (int i = begin; i < end; i++) {
    while (v + 1 < j->nvis && j->first[v + 1] <= i) v++;
    tuft_chunk *c = &tch[j->vis[v]];
    int k = i - j->first[v];
    sway_matrix(&tuft_at[c->start + k], j->t, (float *)c->im->instance_matrix->array + (size_t)k * 16);
  }
}
/* the tuft chunks any of the n frusta sees (none given: all), swayed once for them all */
static void sway_shown(const t3_frustum *f, int nf) {
  int vis[MAX_TUFT_CHUNKS], first[MAX_TUFT_CHUNKS], nvis = 0, total = 0;
  for (int i = 0; i < n_tch; i++) {
    tuft_chunk *c = &tch[i];
    c->count = (int)(c->n * density);
    bool seen = !f;
    for (int k = 0; k < nf && !seen; k++) seen = t3_frustum_intersects_sphere(&f[k], c->sph);
    bool on = c->count > 0 && seen;
    c->im->count = on ? c->count : 0;
    c->im->mesh.base.visible = on;
    if (!on) continue;
    vis[nvis] = i;
    first[nvis++] = total;
    total += c->count;
  }
  if (!total) return;
  sway_job j = { vis, first, nvis, wind_t / 60.0f };
  cw_parallel_for(total, 64, sway_range, &j);
  for (int v = 0; v < nvis; v++) tch[vis[v]].im->instance_matrix->version++;
}

/* ── rocks: irregular faceted shapes, never smooth domes ──────────────
 * An icosphere displaced by a deterministic 3D value noise, then shaped:
 * blocks (pushed toward a cube), slabs (flattened), shards (tall), boulders
 * (lumpy). Flat shaded, so the facets' sharp edges catch the light. Each
 * theme has its stone (sandstone strata, granite, basalt, mossy rock,
 * concrete rubble) and three tints for variety. */
enum { ROCK_BLOCK, ROCK_SLAB, ROCK_SHARD, ROCK_BOULDER, ROCK_KINDS, ROCK_VARIANTS = 3 };
static float n3h(int x, int y, int z, uint32_t seed) {
  uint32_t h = sh((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ seed);
  return (float)(h & 0xffffff) / 16777216.0f;
}
static float noise3(float x, float y, float z, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y), z0 = (int)floorf(z);
  float fx = x - x0, fy = y - y0, fz = z - z0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy), fz = fz * fz * (3 - 2 * fz);
  float v[8];
  for (int i = 0; i < 8; i++) v[i] = n3h(x0 + (i & 1), y0 + ((i >> 1) & 1), z0 + (i >> 2), seed);
  float a = v[0] + (v[1] - v[0]) * fx, b = v[2] + (v[3] - v[2]) * fx, c = v[4] + (v[5] - v[4]) * fx, d = v[6] + (v[7] - v[6]) * fx;
  float e = a + (b - a) * fy, f = c + (d - c) * fy;
  return e + (f - e) * fz;
}
static t3_geometry *rock_geo(int kind, uint32_t seed) {
  t3_geometry *g = t3_icosahedron_geometry_new(1, kind == ROCK_BOULDER ? 3 : 2);
  t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
  float *p = pa->array;
  for (int i = 0; i < pa->count; i++) {
    float x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
    if (kind == ROCK_BLOCK || kind == ROCK_SLAB) { /* toward a cube: flat faces */
      float m = fmaxf(fabsf(x), fmaxf(fabsf(y), fabsf(z)));
      x = x * 0.35f + x / m * 0.65f * 0.75f, y = y * 0.35f + y / m * 0.65f * 0.75f, z = z * 0.35f + z / m * 0.65f * 0.75f;
    }
    float d = 1 + 0.3f * (noise3(x * 1.6f, y * 1.6f, z * 1.6f, seed) - 0.5f) + 0.14f * (noise3(x * 4, y * 4, z * 4, seed + 7) - 0.5f);
    x *= d, y *= d, z *= d;
    if (kind == ROCK_SLAB) y *= 0.38f, x *= 1.2f;
    if (kind == ROCK_SHARD) y *= 2.1f, x *= 0.55f, z *= 0.6f, x += y * 0.12f;
    if (kind == ROCK_BOULDER) y *= 0.8f;
    p[i * 3] = x, p[i * 3 + 1] = y, p[i * 3 + 2] = z;
  }
  pa->version++;
  t3_geometry_compute_vertex_normals(g); /* the polyhedron is non-indexed: flat facets */
  return g;
}
static int rock_tex_for(const theme *th) {
  const char *n = th->name;
  if (!strcmp(n, "canyon") || !strcmp(n, "desert") || !strcmp(n, "beach")) return TEX_SANDSTONE;
  if (!strcmp(n, "mars") || !strcmp(n, "moon") || !strcmp(n, "cosmic")) return TEX_BASALT;
  if (!strcmp(n, "jungle") || !strcmp(n, "autumn")) return TEX_MOSSROCK;
  if (!strcmp(n, "neon") || !strcmp(n, "scary")) return TEX_CONCRETE;
  return TEX_GRANITE;
}
/* the rocks as the crash's 3D world meets them: each a hull of its shape's
 * outermost points along VIEW_ROCK_PTS directions, placed as drawn */
#define MAX_ROCK_HULLS 512
static view_rock rock_hulls[MAX_ROCK_HULLS];
static int n_rock_hulls;
int view_rocks(const view_rock **out) {
  *out = rock_hulls;
  return n_rock_hulls;
}
static const float HULL_DIR[VIEW_ROCK_PTS][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
  { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 }, { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 } };
/* geometry-local extremes of a rock shape */
static void rock_extremes(const t3_geometry *g, t3_vec3 *out) {
  const t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
  const float *p = pa->array;
  for (int d = 0; d < VIEW_ROCK_PTS; d++) {
    float best = -1e30f;
    for (int i = 0; i < pa->count; i++) {
      float k = p[i * 3] * HULL_DIR[d][0] + p[i * 3 + 1] * HULL_DIR[d][1] + p[i * 3 + 2] * HULL_DIR[d][2];
      if (k > best) best = k, out[d] = (t3_vec3){ p[i * 3], p[i * 3 + 1], p[i * 3 + 2] };
    }
  }
}
/* a rock's hull in the world: scale, then the XYZ euler rotation
 * t3_object_set_rotation gives the mesh (with cartwheel's sin/cos: the same
 * on every host), then the position */
static void rock_hull(const t3_vec3 *ext, t3_vec3 pos, t3_vec3 scale, float rx, float ry, float rz) {
  if (n_rock_hulls >= MAX_ROCK_HULLS) return;
  float s1, c1, s2, c2, s3, c3;
  cw_sincos(rx * 0.5f, &s1, &c1);
  cw_sincos(ry * 0.5f, &s2, &c2);
  cw_sincos(rz * 0.5f, &s3, &c3);
  float qx = s1 * c2 * c3 + c1 * s2 * s3, qy = c1 * s2 * c3 - s1 * c2 * s3, qz = c1 * c2 * s3 + s1 * s2 * c3, qw = c1 * c2 * c3 - s1 * s2 * s3;
  view_rock *r = &rock_hulls[n_rock_hulls++];
  for (int d = 0; d < VIEW_ROCK_PTS; d++) {
    float vx = ext[d].x * scale.x, vy = ext[d].y * scale.y, vz = ext[d].z * scale.z;
    /* v + 2 q.xyz x (q.xyz x v + w v) */
    float tx = qy * vz - qz * vy + qw * vx, ty = qz * vx - qx * vz + qw * vy, tz = qx * vy - qy * vx + qw * vz;
    r->p[d] = (t3_vec3){ pos.x + vx + 2 * (qy * tz - qz * ty), pos.y + vy + 2 * (qz * tx - qx * tz), pos.z + vz + 2 * (qx * ty - qy * tx) };
  }
}

static void rocks(t3_object *root, const track_desc *t, const theme *th) {
  int tex = rock_tex_for(th);
  t3_geometry *geo[ROCK_KINDS][ROCK_VARIANTS];
  t3_vec3 ext[ROCK_KINDS][ROCK_VARIANTS][VIEW_ROCK_PTS];
  n_rock_hulls = 0;
  for (int k = 0; k < ROCK_KINDS; k++)
    for (int v = 0; v < ROCK_VARIANTS; v++) {
      geo[k][v] = rock_geo(k, (uint32_t)(k * 31 + v * 7 + 1));
      rock_extremes(geo[k][v], ext[k][v]);
    }
  t3_material *mt[3];
  for (int i = 0; i < 3; i++) {
    float tint = 0.82f + 0.12f * i;
    float c[3] = { th->face[0] * tint * (1.02f - 0.02f * i), th->face[1] * tint, th->face[2] * tint * (0.98f + 0.03f * i) };
    if (tex == TEX_BASALT && strcmp(th->name, "moon")) c[0] = c[0] * 0.8f + 0.12f, c[1] *= 0.75f, c[2] *= 0.7f;
    mt[i] = mat(tex, c, 1.5f, 1.5f, 1.6f, 0.9f);
  }
  int kindw = th->far_kind == 1 ? 1 : 0;
  /* a few hero rocks, many medium, lots of small near the edge */
  for (int i = 0; i < 230; i++) {
    float pick = rnd();
    float x = rnd() * (t->length + 20) - 10;
    float s, z;
    if (pick < 0.06f) s = 1.8f + rnd() * 1.6f, z = -TRACK_HALF_W - 6 - rnd() * 22;
    else if (pick < 0.45f) s = 0.5f + rnd() * 0.9f, z = -TRACK_HALF_W - 1.2f - rnd() * 16;
    else s = 0.12f + rnd() * 0.25f, z = -TRACK_HALF_W - 0.35f - rnd() * 3;
    int kind = (int)(rnd() * ROCK_KINDS) % ROCK_KINDS, var = (int)(rnd() * ROCK_VARIANTS) % ROCK_VARIANTS;
    if (tex == TEX_SANDSTONE && kind == ROCK_SHARD && pick < 0.45f) kind = ROCK_BLOCK; /* sandstone breaks in blocks */
    float ax = 0.8f + rnd() * 0.5f, az = 0.7f + rnd() * 0.5f, ry = rnd() * 6.2831853f, tilt = (rnd() - 0.5f) * 0.5f, tilt2 = (rnd() - 0.5f) * 0.4f;
    int tint = (int)(rnd() * 3) % 3;
    float y = bank_y(x, z, kindw);
    t3_mesh *me = add(root, geo[kind][var], mt[tint], s > 0.4f);
    float hy = kind == ROCK_SHARD ? 2.1f : kind == ROCK_SLAB ? 0.38f : 0.85f;
    me->base.position = (t3_vec3){ x, y + s * hy * 0.55f - s * hy * 0.3f, z }; /* partly buried */
    me->base.scale = (t3_vec3){ s * ax, s, s * az };
    t3_object_set_rotation(&me->base, tilt, ry, tilt2);
    rock_hull(ext[kind][var], me->base.position, me->base.scale, tilt, ry, tilt2);
    /* scree: a split boulder leaves a few chips at its foot */
    if (pick < 0.2f)
      for (int c = 0; c < 4; c++) {
        float cx = x + (rnd() - 0.5f) * s * 2.4f, cz = z + (rnd() - 0.5f) * s * 1.6f, cs = s * (0.12f + rnd() * 0.15f), cr = rnd() * 6;
        int cv = (int)(rnd() * ROCK_VARIANTS) % ROCK_VARIANTS;
        t3_mesh *chip = add(root, geo[ROCK_BLOCK][cv], mt[tint], false);
        chip->base.position = (t3_vec3){ cx, bank_y(cx, cz, kindw) + cs * 0.2f, cz };
        chip->base.scale = (t3_vec3){ cs, cs * 0.7f, cs };
        t3_object_set_rotation(&chip->base, cr, cr * 1.7f, 0);
        rock_hull(ext[ROCK_BLOCK][cv], chip->base.position, chip->base.scale, cr, cr * 1.7f, 0);
      }
  }
  for (int k = 0; k < ROCK_KINDS; k++)
    for (int v = 0; v < ROCK_VARIANTS; v++) t3_release(geo[k][v]);
  for (int i = 0; i < 3; i++) t3_release(mt[i]);
}

static void fill(t3_object *root, const track_desc *t, const theme *th) {
  rocks(root, t, th);
  /* ground plains behind the track at bedrock height */
  t3_material *plain = far_mat(mat(th->top_tex, th->top, 40, 40, 1.0f, 1));
  t3_geometry *pl = t3_plane_geometry_new(1, 1, 1, 1);
  t3_mesh *ground = add(root, pl, plain, false);
  ground->base.position = (t3_vec3){ t->length * 0.5f, t->min_y - 8, -150 };
  ground->base.scale = (t3_vec3){ t->length + 900, 300, 1 };
  t3_object_set_rotation(&ground->base, -1.5707963f, 0, 0);
  t3_mesh *front = add(root, pl, plain, false);
  front->base.position = (t3_vec3){ t->length * 0.5f, t->min_y - 20, 40 };
  front->base.scale = (t3_vec3){ t->length + 900, 80, 1 };
  t3_object_set_rotation(&front->base, -1.5707963f, 0, 0);

  /* grass tufts along both edges, bent by the wind */
  t3_material *bl = t3_mesh_physical_material_new(0xffffff);
  bl->roughness = 0.65f;
  bl->vertex_colors = true;
  bl->sheen = 0.6f;
  bl->sheen_color = lin(th->tuft[0], th->tuft[1], th->tuft[2]);
  bl->sheen_roughness = 0.5f;
  t3_geometry *tg = geo_tuft();
  n_tufts = 0;
  n_tch = 0;
  for (int i = 0; i < TUFTS * 2 && n_tufts < TUFTS && !barren(th); i++) {
    float x = rnd() * (t->length + 10) - 5, side = rnd(), zz = rnd();
    /* on the verges, outside the four lanes (+-LANES * LANE_W / 2) */
    float z = side < 0.5f ? -TRACK_HALF_W + zz * 0.12f : (side < 0.8f ? TRACK_HALF_W - 0.05f - zz * 0.1f : -TRACK_HALF_W - 0.4f - zz * 6);
    bool found;
    float y = track_height_at(x, &found);
    if (!found) continue;
    if (z < -TRACK_HALF_W - 0.3f) y = bank_y(x, z, th->far_kind == 1 ? 1 : 0);
    float s = 0.7f + rnd() * 0.8f, yaw = rnd() * 6.2831853f, sy = 0.8f + rnd() * 0.6f, g = rnd(), flex = 0.7f + rnd() * 0.6f;
    tuft_at[n_tufts] = (sway){ x, y - 0.02f, z, yaw, s, s * sy, flex };
    tuft_at[n_tufts].c = cw_cos(yaw);
    tuft_at[n_tufts].sn = cw_sin(yaw);
    const float *c = g < 0.5f ? th->tuft : th->tuft2;
    float k = 0.8f + 0.4f * g;
    t3_color tc = lin(c[0] * k, c[1] * k, c[2] * k);
    tuft_at[n_tufts].col[0] = tc.r, tuft_at[n_tufts].col[1] = tc.g, tuft_at[n_tufts].col[2] = tc.b;
    n_tufts++;
  }
  /* a tuft's reach: its geometry's bounding sphere about the root (the
   * instance's origin), stretched by the strongest bend (shear up to 0.6 of
   * the height: 1.17x) with room to spare */
  t3_geometry_compute_bounding_sphere(tg);
  {
    t3_sphere bs = tg->bounding_sphere;
    tuft_reach = (sqrtf(bs.center.x * bs.center.x + bs.center.y * bs.center.y + bs.center.z * bs.center.z) + bs.radius) * 1.5f + 0.05f;
  }
  /* into chunks along the track (stable: each keeps its tufts' order) */
  {
    static sway sorted[TUFTS];
    int cnt[MAX_TUFT_CHUNKS] = { 0 }, at[MAX_TUFT_CHUNKS];
#define TUFT_CHUNK_OF(x) CW_CLAMP((int)floorf(((x) + 10) / TUFT_CHUNK), 0, MAX_TUFT_CHUNKS - 1)
    for (int i = 0; i < n_tufts; i++) cnt[TUFT_CHUNK_OF(tuft_at[i].x)]++;
    for (int c = 0, sum = 0; c < MAX_TUFT_CHUNKS; c++) at[c] = sum, sum += cnt[c];
    int fill[MAX_TUFT_CHUNKS] = { 0 };
    for (int i = 0; i < n_tufts; i++) {
      int c = TUFT_CHUNK_OF(tuft_at[i].x);
      sorted[at[c] + fill[c]++] = tuft_at[i];
    }
    memcpy(tuft_at, sorted, sizeof(sway) * (size_t)n_tufts);
    for (int c = 0; c < MAX_TUFT_CHUNKS; c++) {
      if (!cnt[c]) continue;
      tuft_chunk *k = &tch[n_tch++];
      k->start = at[c];
      k->n = cnt[c];
      k->im = t3_instanced_mesh_new(tg, bl, cnt[c]);
      t3_vec3 lo = { 1e30f, 1e30f, 1e30f }, hi = { -1e30f, -1e30f, -1e30f };
      for (int i = 0; i < cnt[c]; i++) {
        const sway *a = &tuft_at[at[c] + i];
        t3_instanced_mesh_set_color_at(k->im, i, (t3_color){ a->col[0], a->col[1], a->col[2] });
        float r = tuft_reach * (a->s > a->sy ? a->s : a->sy);
        lo = (t3_vec3){ fminf(lo.x, a->x - r), fminf(lo.y, a->y - r), fminf(lo.z, a->z - r) };
        hi = (t3_vec3){ fmaxf(hi.x, a->x + r), fmaxf(hi.y, a->y + r), fmaxf(hi.z, a->z + r) };
      }
      t3_vec3 ce = { (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f };
      float dx = hi.x - ce.x, dy = hi.y - ce.y, dz = hi.z - ce.z;
      k->sph = (t3_sphere){ ce, sqrtf(dx * dx + dy * dy + dz * dz) };
      k->im->mesh.base.receive_shadow = true;
      t3_object_add(root, k->im);
      t3_release(k->im);
    }
#undef TUFT_CHUNK_OF
  }
  wind_t = 0;
  sway_shown(NULL, 0);

  t3_release(plain);
  t3_release(pl);
  t3_release(bl);
  t3_release(tg);
}


/* ── the start, checkpoints and the finish ─────────────────────────────
 * Gates of lacquered posts with a glowing banner (bloom catches it); a
 * checkered strip across the track at start and finish; a tall pennant at
 * each checkpoint. */
static t3_texture *checker_tex(void) {
  enum { CN = 64 };
  uint8_t *px = malloc(CN * CN * 4);
  for (int y = 0; y < CN; y++)
    for (int x = 0; x < CN; x++) {
      uint8_t v = ((x / 8) ^ (y / 8)) & 1 ? 240 : 20;
      uint8_t *p = px + (y * CN + x) * 4;
      p[0] = p[1] = p[2] = v;
      p[3] = 255;
    }
  t3_texture *t = t3_texture_new(CN, CN, px);
  free(px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->mag_filter = T3_NEAREST;
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  return t;
}

/* flags beside the track, facing the camera: a pole with a finial, a cloth
 * that waves (its vertices re-posed every frame from render time; drawn only) */
enum { FLAG_SEG = 16, FLAG_ROWS = 6 };
typedef struct flag { t3_geometry *g; float w, h; } flag;
static flag flags[2];
static int n_flags;
static float flag_time;
static void flag_build(t3_object *root, float x, float y, float w, float h, t3_material *cloth, t3_material *pole) {
  float z = -TRACK_HALF_W - 0.45f;
  t3_geometry *pg = t3_cylinder_geometry_new(0.045f, 0.055f, 4.6f, 12, 1, false);
  t3_mesh *p = add(root, pg, pole, true);
  p->base.position = (t3_vec3){ x, y + 2.3f, z };
  t3_geometry *fin = t3_sphere_geometry_new(0.09f, 12, 8);
  t3_mesh *fm = add(root, fin, pole, true);
  fm->base.position = (t3_vec3){ x, y + 4.65f, z };
  t3_geometry *g = t3_plane_geometry_new(w, h, FLAG_SEG, FLAG_ROWS);
  t3_mesh *c = add(root, g, cloth, true);
  c->base.position = (t3_vec3){ x + w * 0.5f + 0.05f, y + 4.55f - h * 0.5f, z };
  c->base.frustum_culled = false;
  if (n_flags < 2) flags[n_flags++] = (flag){ g, w, h };
  t3_release(pg);
  t3_release(fin);
  t3_release(g);
}
/* the cloth: a sine wave travelling out from the pole, growing toward the free edge */
static void flags_wave(float dt) {
  flag_time += dt;
  for (int i = 0; i < n_flags; i++) {
    t3_attribute *pa = flags[i].g->attributes[T3_ATTR_POSITION];
    float *pp = pa->array;
    for (int v = 0; v < pa->count; v++) {
      float u = pp[v * 3] / flags[i].w + 0.5f; /* 0 at the pole .. 1 at the free edge */
      pp[v * 3 + 2] = u * 0.22f * cw_sin(u * 7.5f - flag_time * 5.2f + i) + u * 0.06f * cw_sin(pp[v * 3 + 1] * 3 + flag_time * 3);
    }
    pa->version++;
    t3_geometry_compute_vertex_normals(flags[i].g);
  }
}

static void markers(t3_object *root, const track_desc *t) {
  t3_material *post = t3_mesh_physical_material_new(0xffffff);
  post->color = lin(0.95f, 0.95f, 0.97f);
  post->roughness = 0.3f;
  post->clearcoat = 1;
  post->clearcoat_roughness = 0.08f;
  t3_material *banner = t3_mesh_standard_material_new(0xffffff);
  banner->color = lin(1, 0.45f, 0.1f);
  banner->emissive = lin(1, 0.42f, 0.08f);
  banner->emissive_intensity = 0.7f;
  /* the checks over a cloth's normal, roughness and ao maps: a map-only
   * standard state with point lights about (Scary Town) is not in three.c's table */
  t3_material *check = t3_mesh_standard_material_new(0xffffff);
  tex_apply(check, TEX_FABRIC, 1, 3, 0.6f, 1);
  t3_texture *ct = checker_tex();
  ct->repeat = (t3_vec2){ 1, 3 };
  t3_material_set_texture(check, T3_MAP, ct);
  check->roughness = 0.6f;
  t3_release(ct);
  /* the start and finish lines: a checkered strip across the track, a flag beside it */
  t3_geometry *lg = t3_plane_geometry_new(0.8f, TRACK_HALF_W * 2, 1, 1);
  const float lx[2] = { 0, t->finish_x };
  for (int i = 0; i < 2; i++) {
    t3_mesh *l = add(root, lg, check, false);
    l->base.position = (t3_vec3){ lx[i], track_height_at(lx[i], NULL) + 0.02f, 0 };
    t3_object_set_rotation(&l->base, -1.5707963f, 0, 0);
  }
  t3_release(lg);
  n_flags = 0;
  t3_material *green = t3_mesh_physical_material_new(0xffffff);
  green->color = lin(0.1f, 0.65f, 0.25f);
  green->roughness = 0.7f;
  green->sheen = 1;
  green->sheen_color = lin(0.6f, 1, 0.6f);
  green->side = T3_DOUBLE_SIDE;
  t3_material *checker = t3_mesh_standard_material_new(0xffffff);
  tex_apply(checker, TEX_FABRIC, 2.5f, 1.5f, 0.6f, 1);
  t3_texture *ct2 = checker_tex();
  ct2->repeat = (t3_vec2){ 2.5f, 1.5f };
  t3_material_set_texture(checker, T3_MAP, ct2);
  t3_release(ct2);
  checker->roughness = 0.75f;
  /* front only: the camera never sees the back */
  t3_material *metal = t3_mesh_standard_material_new(0xffffff);
  metal->color = lin(0.75f, 0.76f, 0.78f);
  metal->metalness = 1;
  metal->roughness = 0.3f;
  flag_build(root, -1.0f, track_height_at(0, NULL), 1.9f, 1.2f, green, metal);
  flag_build(root, t->finish_x + 0.6f, t->finish_y, 2.3f, 1.45f, checker, metal);
  t3_release(green);
  t3_release(checker);
  t3_release(metal);
  /* checkpoints: a pennant on a pole at the back edge */
  t3_material *flag = t3_mesh_physical_material_new(0xffffff);
  flag->color = lin(1, 0.8f, 0.15f);
  flag->emissive = lin(1, 0.7f, 0.1f);
  flag->emissive_intensity = 0.8f;
  flag->sheen = 1;
  flag->sheen_color = lin(1, 0.9f, 0.6f);
  flag->side = T3_DOUBLE_SIDE;
  t3_geometry *pole = t3_cylinder_geometry_new(0.05f, 0.06f, 3.2f, 10, 1, false);
  t3_geometry *pen = t3_cone_geometry_new(0.45f, 1.4f, 3, 1, false);
  for (int i = 0; i < t->check_count; i++) {
    float x = t->checks[i * 2], y = t->checks[i * 2 + 1];
    t3_mesh *p = add(root, pole, post, true);
    p->base.position = (t3_vec3){ x, y + 1.6f, -TRACK_HALF_W - 0.1f };
    t3_mesh *f = add(root, pen, flag, true);
    f->base.position = (t3_vec3){ x + 0.7f, y + 2.85f, -TRACK_HALF_W - 0.1f };
    f->base.scale = (t3_vec3){ 1, 1, 0.08f };
    t3_object_set_rotation(&f->base, 0, 0, -1.5707963f);
  }
  t3_release(pole);
  t3_release(pen);
  t3_release(flag);
  t3_release(post);
  t3_release(banner);
  t3_release(check);
}


/* ── theme props: what grows or stands on the bank behind the track ────
 * canyon: (boulders only); alpine: snowy pines; neon: towers with lit
 * windows and neon strips along the track; jungle and autumn: broadleaf
 * trees (green / orange); desert: saguaro cacti; coast: wind-bent pines and
 * a grey sea far below; cosmic: floating rocks and glowing crystals */
static t3_texture *window_tex(void) {
  enum { WN = 128 };
  uint8_t *px = malloc(WN * WN * 4);
  uint32_t h = 99;
  for (int y = 0; y < WN; y++)
    for (int x = 0; x < WN; x++) {
      int cx = x / 8, cy = y / 8, ix = x % 8, iy = y % 8;
      uint32_t r = sh((uint32_t)(cx * 131 + cy * 977) + h);
      bool win = ix > 1 && ix < 7 && iy > 1 && iy < 6;
      bool lit = (r & 3) != 0;
      float warm = (float)((r >> 4) & 255) / 255.0f;
      uint8_t *p = px + (y * WN + x) * 4;
      if (win && lit) {
        p[0] = (uint8_t)(200 + 55 * warm), p[1] = (uint8_t)(150 + 60 * warm), p[2] = (uint8_t)(90 + 120 * (1 - warm));
      } else p[0] = p[1] = p[2] = win ? 8 : 0;
      p[3] = 255;
    }
  t3_texture *t = t3_texture_new(WN, WN, px);
  free(px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  return t;
}

static void tree(t3_object *root, t3_geometry *trunk, t3_geometry *crown, t3_material *bark, t3_material *leaves, float x, float y,
                 float z, float h, float r, bool conifer) {
  t3_mesh *tk = add(root, trunk, bark, true);
  tk->base.position = (t3_vec3){ x, y + h * 0.5f, z };
  tk->base.scale = (t3_vec3){ 1, h, 1 };
  if (conifer) {
    for (int k = 0; k < 3; k++) {
      t3_mesh *c = add(root, crown, leaves, true);
      float f = 1 - k * 0.25f;
      c->base.position = (t3_vec3){ x, y + h * (0.45f + k * 0.2f), z };
      c->base.scale = (t3_vec3){ r * f, h * 0.45f, r * f };
    }
  } else
    for (int k = 0; k < 3; k++) {
      float ox = (rnd() - 0.5f) * r, oz = (rnd() - 0.5f) * r * 0.6f, ry = rnd() * 6;
      t3_mesh *c = add(root, crown, leaves, true);
      c->base.position = (t3_vec3){ x + ox, y + h + k * r * 0.35f, z + oz };
      c->base.scale = (t3_vec3){ r * (1 - k * 0.2f), r * 0.8f * (1 - k * 0.2f), r * (1 - k * 0.2f) };
      t3_object_set_rotation(&c->base, 0, ry, 0);
    }
}

static t3_material *glow(float r, float g, float b, float e) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = lin(r, g, b);
  m->emissive = lin(r, g, b);
  m->emissive_intensity = e;
  return m;
}

static void props(t3_object *root, const track_desc *t, const theme *th) {
  int kind = th->far_kind == 1 ? 1 : 0;
  const char *n = th->name;
  t3_geometry *trunk = t3_cylinder_geometry_new(0.12f, 0.2f, 1, 10, 1, false);
  if (!strcmp(n, "alpine") || !strcmp(n, "coast")) {
    t3_material *bark = mat(TEX_BARK, (float[3]){ 0.7f, 0.6f, 0.55f }, 1, 1, 1.4f, 0.9f);
    float leaf[3] = { 0.42f, 0.55f, 0.4f };
    if (!strcmp(n, "alpine")) leaf[0] = 0.55f, leaf[1] = 0.66f, leaf[2] = 0.58f;
    t3_material *needles = mat(TEX_LEAVES, leaf, 2, 2, 1.2f, 0.85f);
    t3_geometry *cone = t3_cone_geometry_new(1, 1, 9, 1, false);
    for (int i = 0; i < 70; i++) {
      float x = -30 + rnd() * (t->length + 60), z = -TRACK_HALF_W - 3 - rnd() * 40, h = 3 + rnd() * 4, r = 1 + rnd() * 0.8f;
      tree(root, trunk, cone, bark, needles, x, bank_y(x, z, kind) - 0.2f, z, h, r, true);
    }
    if (!strcmp(n, "coast")) {
      /* the sea, grey-green and glossy, far below in front */
      t3_material *sea = t3_mesh_physical_material_new(0xffffff);
      sea->color = lin(0.16f, 0.24f, 0.25f);
      sea->roughness = 0.18f;
      sea->clearcoat = 1;
      sea->clearcoat_roughness = 0.12f;
      tex_apply(sea, TEX_SAND, 60, 60, 0.6f, 1);
      t3_geometry *pl = t3_plane_geometry_new(1, 1, 1, 1);
      t3_mesh *m = add(root, pl, sea, false);
      m->base.position = (t3_vec3){ t->length * 0.5f, t->min_y - 14, 120 };
      m->base.scale = (t3_vec3){ t->length + 1200, 300, 1 };
      t3_object_set_rotation(&m->base, -1.5707963f, 0, 0);
      t3_release(pl);
      t3_release(sea);
    }
    t3_release(cone);
    t3_release(bark);
    t3_release(needles);
  } else if (!strcmp(n, "jungle") || !strcmp(n, "autumn")) {
    bool jungle = !strcmp(n, "jungle");
    t3_material *bark = mat(TEX_BARK, (float[3]){ 0.75f, 0.65f, 0.55f }, 1, 1, 1.4f, 0.9f);
    float leaf[3] = { 0.35f, 0.62f, 0.25f }, leaf2[3] = { 1.0f, 0.55f, 0.18f };
    t3_material *crown = mat(TEX_LEAVES, jungle ? leaf : leaf2, 1, 1, 1.2f, 0.8f);
    float leaf3[3] = { 0.95f, 0.32f, 0.12f }, leaf4[3] = { 0.25f, 0.5f, 0.2f };
    t3_material *crown2 = mat(TEX_LEAVES, jungle ? leaf4 : leaf3, 1, 1, 1.2f, 0.8f);
    crown->sheen = crown2->sheen = 0;
    t3_geometry *ico = t3_icosahedron_geometry_new(1, 2);
    for (int i = 0; i < 90; i++) {
      float x = -30 + rnd() * (t->length + 60), z = -TRACK_HALF_W - 2.5f - rnd() * 42, h = 2.5f + rnd() * 3.5f, r = 1.2f + rnd() * 1.2f;
      bool two = rnd() < 0.5f;
      tree(root, trunk, ico, bark, two ? crown2 : crown, x, bank_y(x, z, kind) - 0.2f, z, h, r, false);
    }
    t3_release(ico);
    t3_release(bark);
    t3_release(crown);
    t3_release(crown2);
  } else if (!strcmp(n, "desert")) {
    t3_material *cac = t3_mesh_physical_material_new(0xffffff);
    cac->color = lin(0.3f, 0.5f, 0.28f);
    cac->roughness = 0.6f;
    cac->sheen = 0.5f;
    cac->sheen_color = lin(0.8f, 0.9f, 0.6f);
    t3_geometry *stem = t3_cylinder_geometry_new(0.22f, 0.25f, 1, 12, 1, false), *tip = t3_sphere_geometry_new(0.22f, 12, 8);
    for (int i = 0; i < 40; i++) {
      float x = -30 + rnd() * (t->length + 60), z = -TRACK_HALF_W - 3 - rnd() * 35, h = 2 + rnd() * 2.5f;
      float y = bank_y(x, z, kind) - 0.1f;
      t3_mesh *m = add(root, stem, cac, true);
      m->base.position = (t3_vec3){ x, y + h * 0.5f, z };
      m->base.scale = (t3_vec3){ 1, h, 1 };
      t3_mesh *tp = add(root, tip, cac, true);
      tp->base.position = (t3_vec3){ x, y + h, z };
      for (int a = -1; a <= 1; a += 2) {
        if (rnd() < 0.3f) continue;
        float ah = h * (0.35f + rnd() * 0.2f), al = 0.5f + rnd() * 0.3f, up = 0.6f + rnd() * 0.6f;
        t3_mesh *arm = add(root, stem, cac, true);
        arm->base.position = (t3_vec3){ x + a * al * 0.5f, y + ah, z };
        arm->base.scale = (t3_vec3){ 0.7f, al, 0.7f };
        t3_object_set_rotation(&arm->base, 0, 0, 1.5707963f);
        t3_mesh *arm2 = add(root, stem, cac, true);
        arm2->base.position = (t3_vec3){ x + a * al, y + ah + up * 0.5f, z };
        arm2->base.scale = (t3_vec3){ 0.7f, up, 0.7f };
        t3_mesh *at = add(root, tip, cac, true);
        at->base.position = (t3_vec3){ x + a * al, y + ah + up, z };
        at->base.scale = (t3_vec3){ 0.7f, 0.7f, 0.7f };
      }
    }
    t3_release(stem);
    t3_release(tip);
    t3_release(cac);
  } else if (!strcmp(n, "neon")) {
    /* towers behind, their lit windows as rows of glowing panes on the
     * face toward the track (constant emission: no emissive map) */
    t3_material *tower = mat(TEX_ASPHALT, (float[3]){ 0.4f, 0.4f, 0.48f }, 2, 6, 1.0f, 0.5f);
    t3_material *pane[2] = { glow(1, 0.82f, 0.5f, 2.2f), glow(0.55f, 0.75f, 1, 2.0f) };
    t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
    for (int i = 0; i < 60; i++) {
      float x = -40 + rnd() * (t->length + 80), z = -10 - rnd() * 60, w = 4 + rnd() * 6, h = 12 + rnd() * 30, d = w * (0.7f + rnd() * 0.5f);
      float base = bank_y(x, z, kind) - 4;
      t3_mesh *m = add(root, box, tower, false);
      m->base.position = (t3_vec3){ x, base + h * 0.5f, z };
      m->base.scale = (t3_vec3){ w, h, d };
      int cols = (int)(w / 1.4f), rows = (int)(h / 2.2f);
      for (int r = 1; r < rows; r++)
        for (int c = 0; c < cols; c++) {
          if (rnd() < 0.35f) continue; /* dark rooms */
          int warm = rnd() < 0.7f;
          t3_mesh *wp = add(root, box, pane[warm ? 0 : 1], false);
          wp->base.position = (t3_vec3){ x - w * 0.5f + (c + 0.5f) * w / cols, base + r * 2.2f, z + d * 0.5f + 0.03f };
          wp->base.scale = (t3_vec3){ w / cols * 0.55f, 0.9f, 0.04f };
        }
    }
    static const float NEON[3][3] = { { 1, 0.2f, 0.75f }, { 0.2f, 0.9f, 1 }, { 1, 0.75f, 0.15f } };
    for (int k = 0; k < 3; k++) {
      t3_material *glow = t3_mesh_standard_material_new(0xffffff);
      glow->color = lin(NEON[k][0], NEON[k][1], NEON[k][2]);
      glow->emissive = lin(NEON[k][0], NEON[k][1], NEON[k][2]);
      glow->emissive_intensity = 3.5f;
      for (float x = -20 + k * 7; x < t->length + 20; x += 21) {
        float y = track_height_at(x, NULL);
        t3_mesh *m = add(root, box, glow, false);
        m->base.position = (t3_vec3){ x, y + 0.08f, -TRACK_HALF_W - 0.25f };
        m->base.scale = (t3_vec3){ 5, 0.08f, 0.08f };
      }
      t3_release(glow);
    }
    t3_release(box);
    t3_release(tower);
    t3_release(pane[0]);
    t3_release(pane[1]);
  } else if (!strcmp(n, "cosmic")) {
    t3_material *rock = mat(TEX_ROCK, (float[3]){ 0.5f, 0.45f, 0.7f }, 1, 1, 1.5f, 0.85f);
    t3_material *gem = t3_mesh_physical_material_new(0xffffff);
    gem->color = lin(0.5f, 0.9f, 1);
    gem->emissive = lin(0.3f, 0.75f, 1);
    gem->emissive_intensity = 2.2f;
    gem->iridescence = 1;
    gem->roughness = 0.15f;
    t3_geometry *ico = t3_icosahedron_geometry_new(1, 1), *oct = t3_octahedron_geometry_new(1, 0);
    for (int i = 0; i < 50; i++) {
      float x = -40 + rnd() * (t->length + 80), z = -8 - rnd() * 60, y = track_height_at(cw_clampf(x, 0, t->length), NULL) + 6 + rnd() * 18;
      float sc = 1 + rnd() * 4, rx = rnd() * 3, ry = rnd() * 3;
      t3_mesh *m = add(root, ico, rock, false);
      m->base.position = (t3_vec3){ x, y, z };
      m->base.scale = (t3_vec3){ sc, sc * 0.6f, sc * 0.8f };
      t3_object_set_rotation(&m->base, rx, ry, 0);
    }
    for (int i = 0; i < 80; i++) {
      float x = -20 + rnd() * (t->length + 40), z = -TRACK_HALF_W - 1 - rnd() * 18, sc = 0.25f + rnd() * 0.6f, tilt = (rnd() - 0.5f) * 0.6f;
      t3_mesh *m = add(root, oct, gem, false);
      m->base.position = (t3_vec3){ x, bank_y(x, z, kind) + sc * 0.8f, z };
      m->base.scale = (t3_vec3){ sc * 0.5f, sc * 1.6f, sc * 0.5f };
      t3_object_set_rotation(&m->base, 0, 0, tilt);
    }
    t3_release(ico);
    t3_release(oct);
    t3_release(rock);
    t3_release(gem);
  }
  t3_release(trunk);
}


/* a house: walls, a pitched roof, glowing windows; lean tilts it (a crooked
 * haunted house) */
static void house(t3_object *root, t3_geometry *box, t3_geometry *roofg, t3_material *wall, t3_material *roof, t3_material *win,
                  float x, float y, float z, float w, float h, float lean) {
  t3_object *g = t3_group_new();
  g->position = (t3_vec3){ x, y, z };
  t3_object_set_rotation(g, 0, 0, lean);
  t3_object_add(root, g);
  t3_release(g);
  t3_mesh *b = add(g, box, wall, true);
  b->base.position = (t3_vec3){ 0, h * 0.5f, 0 };
  b->base.scale = (t3_vec3){ w, h, w * 0.8f };
  t3_mesh *r = add(g, roofg, roof, true);
  r->base.position = (t3_vec3){ 0, h + w * 0.3f, 0 };
  r->base.scale = (t3_vec3){ w * 0.78f, w * 0.6f, w * 0.62f };
  t3_object_set_rotation(&r->base, 0, 0.7853982f, 0);
  for (int k = 0; k < 2; k++) {
    t3_mesh *wd = add(g, box, win, false);
    wd->base.position = (t3_vec3){ (k ? 0.22f : -0.22f) * w, h * 0.55f, w * 0.41f };
    wd->base.scale = (t3_vec3){ w * 0.18f, h * 0.22f, 0.05f };
  }
}


/* ── jack-o'-lanterns (Scary Town) ─────────────────────────────────────
 * ribbed pumpkins with carved faces toward the camera, in small groups (on
 * porch steps, by a few headstones, at the start and the checkpoints, on a
 * fence), each sat on the surface under it. Inside, a candle: a stub and a
 * swaying flame seen through the holes. The faces, the flames and three
 * small warm point lights (which follow the rider to the nearest pumpkins
 * and wobble with the flame) flicker with layered noise: a fast jitter, a
 * slow breathing and now and then a dip. Presentation only (render time). */
#define MAX_PK 48
#define PK_GROUPS 5
typedef struct pk_t { float x, y, z, r; t3_mesh *flame; } pk_t;
static pk_t pks[MAX_PK];
static int n_pk;
static t3_material *pk_face[PK_GROUPS], *pk_flame[PK_GROUPS];
static t3_light *pk_light[3];
static float pk_time, pk_level[PK_GROUPS];

/* a pumpkin of radius 1: a squashed sphere with ten ribs, its base at y = -0.76 */
static t3_geometry *geo_pumpkin(void) {
  t3_geometry *g = t3_sphere_geometry_new(1, 30, 18);
  t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
  float *p = pa->array;
  for (int i = 0; i < pa->count; i++) {
    float x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
    float rib = 1 + 0.07f * cw_cos(10 * cw_atan2(z, x)) * (1 - y * y);
    p[i * 3] = x * rib, p[i * 3 + 1] = y * 0.82f - 0.06f * y * y * y, p[i * 3 + 2] = z * rib;
  }
  t3_geometry_compute_vertex_normals(g);
  return g;
}

/* the shell's front at (x, y), over the ribs (the body without them: x^2 + (y / 0.82)^2 + z^2 = 1) */
static float pk_front(float x, float y) { return 1.08f * sqrtf(fmaxf(0.04f, 1 - x * x - (y / 0.82f) * (y / 0.82f))); }
/* a carved hole: a convex outline (counter-clockwise) through the shell along
 * +z, its face just proud of the surface and its walls going in */
static void carve(mb *b, const float (*pt)[2], int n) {
  int f0 = b->nv;
  for (int i = 0; i < n; i++) mb_v(b, (t3_vec3){ pt[i][0], pt[i][1], pk_front(pt[i][0], pt[i][1]) + 0.025f }, (t3_vec3){ 0, 0, 1 }, 0, 0, 1);
  for (int i = 1; i + 1 < n; i++) mb_t(b, f0, f0 + i, f0 + i + 1);
  for (int i = 0; i < n; i++) { /* the walls, toward the candle */
    int j = (i + 1) % n;
    float dx = pt[j][0] - pt[i][0], dy = pt[j][1] - pt[i][1];
    t3_vec3 nn = { dy, -dx, 0 };
    float za = pk_front(pt[i][0], pt[i][1]) + 0.025f, zb = pk_front(pt[j][0], pt[j][1]) + 0.025f;
    int a = mb_v(b, (t3_vec3){ pt[i][0], pt[i][1], za }, nn, 0, 0, 1), c = mb_v(b, (t3_vec3){ pt[j][0], pt[j][1], zb }, nn, 0, 0, 1);
    int d = mb_v(b, (t3_vec3){ pt[j][0], pt[j][1], zb - 0.16f }, nn, 0, 0, 1), e = mb_v(b, (t3_vec3){ pt[i][0], pt[i][1], za - 0.16f }, nn, 0, 0, 1);
    mb_t(b, a, e, d);
    mb_t(b, a, d, c);
  }
}
static void tri(mb *b, float x0, float y0, float x1, float y1, float x2, float y2) {
  const float p[3][2] = { { x0, y0 }, { x1, y1 }, { x2, y2 } };
  carve(b, p, 3);
}
static void quad(mb *b, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
  const float p[4][2] = { { x0, y0 }, { x1, y1 }, { x2, y2 }, { x3, y3 } };
  carve(b, p, 4);
}
/* three faces: triangle eyes and a toothy grin; slanted angry eyes and a
 * zigzag mouth; round surprised eyes and an open howl */
static t3_geometry *geo_face(int kind) {
  mb b = { 0 };
  if (kind == 0) {
    for (int s2 = -1; s2 <= 1; s2 += 2) tri(&b, s2 * 0.35f - 0.16f, 0.12f, s2 * 0.35f + 0.16f, 0.12f, s2 * 0.35f, 0.34f);
    tri(&b, -0.08f, -0.06f, 0.08f, -0.06f, 0, 0.07f);
    /* the grin: a crescent in five pieces with four teeth left standing */
    for (int k = 0; k < 5; k++) {
      float xa = -0.52f + k * 0.21f, xb = xa + 0.15f;
      float ya = -0.2f - 0.16f * (1 - (xa / 0.55f) * (xa / 0.55f)), yb = -0.2f - 0.16f * (1 - (xb / 0.55f) * (xb / 0.55f));
      float da = k % 2 ? 0.1f : 0.16f;
      quad(&b, xa, ya - da, xb, yb - da, xb, yb, xa, ya);
    }
  } else if (kind == 1) {
    for (int s2 = -1; s2 <= 1; s2 += 2)
      tri(&b, s2 * 0.38f - 0.17f * s2, 0.08f + (s2 < 0 ? 0.0f : 0.14f), s2 * 0.38f + 0.17f * s2, 0.08f + (s2 < 0 ? 0.14f : 0.0f), s2 * 0.38f, 0.3f);
    tri(&b, -0.07f, -0.08f, 0.07f, -0.08f, 0, 0.04f);
    /* the zigzag: six slanted strips, alternating */
    for (int k = 0; k < 6; k++) {
      float xa = -0.48f + k * 0.16f, xb = xa + 0.16f, up = k % 2 ? 0.08f : -0.08f;
      quad(&b, xa, -0.32f - up, xb, -0.32f + up, xb, -0.22f + up, xa, -0.22f - up);
    }
  } else {
    for (int s2 = -1; s2 <= 1; s2 += 2) {
      float p[8][2];
      for (int k = 0; k < 8; k++) p[k][0] = s2 * 0.33f + 0.12f * cw_cos(k * 0.785398f), p[k][1] = 0.2f + 0.14f * cw_sin(k * 0.785398f);
      carve(&b, p, 8);
    }
    float p[10][2];
    for (int k = 0; k < 10; k++) p[k][0] = 0.17f * cw_cos(k * 0.628318f), p[k][1] = -0.3f + 0.2f * cw_sin(k * 0.628318f);
    carve(&b, p, 10);
  }
  return mb_done(&b);
}

/* the surface at (x, z): the track on it, the bank behind it */
static float surface_at(float x, float z, int kind) { return z >= -TRACK_HALF_W ? track_height_at(x, NULL) : bank_y(x, z, kind); }

typedef struct pk_parts { t3_geometry *body, *stem, *faces[3], *candle, *flame; t3_material *skin, *stalk, *wax; } pk_parts;
/* a pumpkin of radius r on a base at height base (NAN: the surface under its footprint, its lowest point) */
static void pumpkin(t3_object *root, const pk_parts *pp, int kind, float x, float z, float r, float base) {
  if (n_pk >= MAX_PK) return;
  float tilt = 0;
  if (isnan(base)) {
    float s[5] = { surface_at(x, z, kind), surface_at(x - 0.7f * r, z, kind), surface_at(x + 0.7f * r, z, kind),
                   surface_at(x, z - 0.7f * r, kind), surface_at(x, z + 0.7f * r, kind) };
    base = s[0];
    for (int k = 1; k < 5; k++) base = fminf(base, s[k]);
    tilt = cw_clampf(cw_atan2(s[2] - s[1], 1.4f * r), -0.25f, 0.25f);
  }
  float y = base - 0.015f + 0.76f * r; /* sunk a centimetre or two */
  t3_object *g = t3_group_new();
  g->position = (t3_vec3){ x, y, z };
  g->scale = (t3_vec3){ r, r, r };
  float yaw = (rnd() - 0.5f) * 0.6f; /* one draw per statement: argument order is unspecified */
  t3_object_set_rotation(g, 0, yaw, tilt);
  t3_object_add(root, g);
  t3_release(g);
  add(g, pp->body, pp->skin, true);
  t3_mesh *st = add(g, pp->stem, pp->stalk, true);
  st->base.position = (t3_vec3){ 0.02f, 0.86f, 0 };
  st->base.scale = (t3_vec3){ 0.1f, 0.3f, 0.1f };
  float lean = (rnd() - 0.5f) * 0.6f;
  t3_object_set_rotation(&st->base, 0.2f, 0, lean);
  int grp = n_pk % PK_GROUPS, face = (int)(rnd() * 2.999f);
  add(g, pp->faces[face], pk_face[grp], false);
  /* the candle: a stub on the floor inside and its flame */
  t3_mesh *cd = add(g, pp->candle, pp->wax, false);
  cd->base.position = (t3_vec3){ 0, -0.5f, 0.15f };
  cd->base.scale = (t3_vec3){ 0.1f, 0.3f, 0.1f };
  t3_mesh *fl = add(g, pp->flame, pk_flame[grp], false);
  fl->base.position = (t3_vec3){ 0, -0.2f, 0.18f };
  pks[n_pk] = (pk_t){ x, y, z, r, fl };
  n_pk++;
}

/* smooth value noise, 0..1 */
static float pk_noise(float t) {
  float f = floorf(t), u = t - f;
  uint32_t a = sh((uint32_t)(int32_t)f * 2654435761u), b = sh((uint32_t)(int32_t)(f + 1) * 2654435761u);
  float va = (a & 0xffff) / 65535.0f, vb = (b & 0xffff) / 65535.0f;
  u = u * u * (3 - 2 * u);
  return va + (vb - va) * u;
}

/* every frame: the candles flicker; the lights go to the three pumpkins nearest the rider */
static void pumpkins_frame(float dt) {
  if (!n_pk) return;
  pk_time += dt;
  for (int i = 0; i < PK_GROUPS && i < n_pk; i++) { /* a group no pumpkin uses was freed */
    float s = i * 37.1f, t = pk_time;
    float v = 0.78f + 0.14f * pk_noise(t * 11 + s) + 0.08f * pk_noise(t * 27 + s * 2) /* jitter */
              + 0.1f * (pk_noise(t * 0.8f + s) - 0.5f);                            /* breathing */
    float dip = pk_noise(t * 1.7f + s * 3);
    if (dip > 0.82f) v -= (dip - 0.82f) * 2.2f; /* now and then a gust nearly puts it out */
    pk_level[i] = v;
    pk_face[i]->emissive_intensity = 3.4f * v;
    pk_flame[i]->emissive_intensity = 6 * v;
  }
  for (int i = 0; i < n_pk; i++) { /* the flames sway and stretch */
    float s = i * 11.3f, w = pk_noise(pk_time * 3 + s) - 0.5f, h = pk_level[i % PK_GROUPS];
    t3_object_set_rotation(&pks[i].flame->base, 0, 0, w * 0.5f);
    pks[i].flame->base.scale = (t3_vec3){ 0.11f, 0.22f + 0.1f * h, 1 };
  }
  float bx = bike_get()->x;
  int best[3] = { -1, -1, -1 };
  for (int i = 0; i < n_pk; i++) {
    float d = fabsf(pks[i].x - bx) + fabsf(pks[i].z) * 0.3f;
    for (int k = 0; k < 3; k++) {
      float dk = best[k] < 0 ? 1e9f : fabsf(pks[best[k]].x - bx) + fabsf(pks[best[k]].z) * 0.3f;
      if (d < dk) {
        for (int m = 2; m > k; m--) best[m] = best[m - 1];
        best[k] = i;
        break;
      }
    }
  }
  for (int k = 0; k < 3; k++) {
    t3_light *l = pk_light[k];
    if (best[k] < 0) {
      l->intensity = 0;
      continue;
    }
    const pk_t *a = &pks[best[k]];
    float s = best[k] * 5.7f;
    /* the candle itself, inside: the shell stays dark, the ground round it glows; it wobbles with the flame */
    l->base.position = (t3_vec3){ a->x + 0.05f * (pk_noise(pk_time * 4 + s) - 0.5f), a->y + a->r * 0.1f + 0.04f * (pk_noise(pk_time * 5 + s + 9) - 0.5f),
                                  a->z + a->r * 0.2f };
    l->intensity = 16 * pk_level[best[k] % PK_GROUPS];
  }
}

static void props2(t3_object *root, const track_desc *t, const theme *th) {
  int kind = th->far_kind == 1 ? 1 : 0;
  const char *n = th->name;
  t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1), *cone4 = t3_cone_geometry_new(1, 1, 4, 1, false);
  t3_geometry *cyl = t3_cylinder_geometry_new(1, 1, 1, 10, 1, false), *ball = t3_sphere_geometry_new(1, 20, 14);
  if (!strcmp(n, "scary")) {
    t3_material *wall = mat(TEX_WOOD, (float[3]){ 0.45f, 0.4f, 0.38f }, 1, 1, 1.2f, 0.9f);
    t3_material *roof = mat(TEX_WOOD, (float[3]){ 0.22f, 0.2f, 0.22f }, 2, 2, 1.2f, 0.9f);
    t3_material *win = glow(1, 0.55f, 0.12f, 3.0f), *stone = mat(TEX_ROCK, (float[3]){ 0.55f, 0.56f, 0.6f }, 1, 1, 1.4f, 0.9f);
    t3_material *deadwood = mat(TEX_BARK, (float[3]){ 0.35f, 0.32f, 0.3f }, 1, 1, 1.4f, 0.95f), *lamp = glow(1, 0.85f, 0.5f, 4);
    t3_material *iron = t3_mesh_standard_material_new(0xffffff);
    iron->color = lin(0.08f, 0.08f, 0.09f);
    iron->metalness = 0.8f;
    iron->roughness = 0.5f;
    pk_parts pp = { 0 };
    pp.skin = t3_mesh_standard_material_new(0xffffff);
    pp.skin->color = lin(0.9f, 0.38f, 0.05f);
    pp.skin->roughness = 0.55f;
    pp.stalk = mat(TEX_BARK, (float[3]){ 0.5f, 0.45f, 0.25f }, 1, 1, 1.2f, 0.9f);
    pp.wax = glow(1, 0.92f, 0.78f, 0.6f);
    static const float TINT[PK_GROUPS][3] = { { 1, 0.58f, 0.14f }, { 1, 0.52f, 0.1f }, { 1, 0.64f, 0.2f }, { 1, 0.5f, 0.12f }, { 1, 0.6f, 0.24f } };
    for (int i = 0; i < PK_GROUPS; i++) { /* each group a slightly different candle */
      pk_face[i] = glow(TINT[i][0], TINT[i][1], TINT[i][2], 3.4f);
      pk_flame[i] = glow(1, 0.75f + 0.05f * (i & 1), 0.35f, 6);
    }
    pp.body = geo_pumpkin();
    pp.stem = t3_cylinder_geometry_new(0.7f, 1, 1, 7, 1, false);
    pp.candle = t3_cylinder_geometry_new(1, 1, 1, 8, 1, false);
    pp.flame = t3_plane_geometry_new(1, 1, 1, 1);
    for (int i = 0; i < 3; i++) pp.faces[i] = geo_face(i);
    for (int i = 0; i < 26; i++) {
      float x = -20 + rnd() * (t->length + 40), z = -TRACK_HALF_W - 6 - rnd() * 30, w = 3 + rnd() * 2.5f, h = 3 + rnd() * 3, lean = (rnd() - 0.5f) * 0.25f;
      float y = bank_y(x, z, kind) - 0.3f;
      house(root, box, cone4, wall, roof, win, x, y, z, w, h, lean);
      if (rnd() < 0.3f) { /* a porch step with a group of pumpkins on it */
        float pz = z + w * 0.4f + 0.55f, g0 = fminf(bank_y(x - w * 0.35f, pz, kind), bank_y(x + w * 0.35f, pz, kind));
        float top = fmaxf(fmaxf(bank_y(x - w * 0.35f, pz, kind), bank_y(x + w * 0.35f, pz, kind)), y + 0.1f) + 0.12f;
        t3_mesh *step = add(root, box, wall, true);
        step->base.position = (t3_vec3){ x, (top + g0 - 0.3f) * 0.5f, pz };
        step->base.scale = (t3_vec3){ w * 0.8f, top - g0 + 0.3f, 0.9f };
        int n = 2 + (rnd() < 0.4f);
        for (int k = 0; k < n; k++) {
          float r = 0.3f + rnd() * 0.16f;
          pumpkin(root, &pp, kind, x + (k - (n - 1) * 0.5f) * w * 0.28f, pz + 0.05f, r, top);
        }
      }
    }
    for (int i = 0; i < 80; i++) {
      float x = -10 + rnd() * (t->length + 20), z = -TRACK_HALF_W - 1.5f - rnd() * 10, tilt = (rnd() - 0.5f) * 0.4f, sc = 0.7f + rnd() * 0.5f;
      t3_mesh *g = add(root, box, stone, true);
      g->base.position = (t3_vec3){ x, bank_y(x, z, kind) + 0.35f * sc, z };
      g->base.scale = (t3_vec3){ 0.5f * sc, 0.8f * sc, 0.12f };
      t3_object_set_rotation(&g->base, 0, (rnd() - 0.5f) * 0.4f, tilt);
      if (rnd() < 0.06f) { /* a few left at a grave's foot */
        float r = 0.24f + rnd() * 0.12f;
        pumpkin(root, &pp, kind, x + 0.4f, z + 0.35f, r, NAN);
      }
    }
    /* markers: a pair at the start and at each checkpoint, at the back edge */
    for (int i = -1; i < t->check_count; i++) {
      float x = i < 0 ? 2.5f : t->checks[i * 2] - 1.2f;
      float r0 = 0.32f + rnd() * 0.1f;
      pumpkin(root, &pp, kind, x, -TRACK_HALF_W - 0.75f, r0, NAN);
      float r1 = 0.22f + rnd() * 0.08f;
      pumpkin(root, &pp, kind, x + 0.85f, -TRACK_HALF_W - 0.95f, r1, NAN);
    }
    /* one picket fence with a pumpkin on every other post */
    {
      float x = t->length * 0.45f, z = -TRACK_HALF_W - 2.4f;
      for (int k = 0; k < 5; k++) {
        float px = x + k * 1.3f, py = bank_y(px, z, kind);
        t3_mesh *pst = add(root, box, deadwood, true);
        pst->base.position = (t3_vec3){ px, py + 0.45f, z };
        pst->base.scale = (t3_vec3){ 0.13f, 1.3f, 0.13f }; /* 0.2 m of it in the ground; the top at py + 1.1 */
        if (k % 2 == 0) {
          float r = 0.2f + rnd() * 0.05f;
          pumpkin(root, &pp, kind, px, z, r, py + 1.1f);
        }
        if (k < 4)
          for (int r2 = 0; r2 < 2; r2++) {
            t3_mesh *rl = add(root, box, deadwood, true);
            rl->base.position = (t3_vec3){ px + 0.65f, py + 0.4f + r2 * 0.45f, z + 0.08f };
            rl->base.scale = (t3_vec3){ 1.3f, 0.08f, 0.04f };
          }
      }
    }
    for (int k = 0; k < 3; k++) {
      pk_light[k] = t3_point_light_new(0xff8a30, 0, 8, 2);
      t3_object_add(root, &pk_light[k]->base);
      t3_release(pk_light[k]);
    }
    for (int i = 0; i < PK_GROUPS; i++) t3_release(pk_face[i]), t3_release(pk_flame[i]);
    for (int i = 0; i < 3; i++) t3_release(pp.faces[i]);
    t3_geometry *pg[] = { pp.body, pp.stem, pp.candle, pp.flame };
    for (int i = 0; i < 4; i++) t3_release(pg[i]);
    t3_material *pm[] = { pp.skin, pp.stalk, pp.wax };
    for (int i = 0; i < 3; i++) t3_release(pm[i]);
    for (int i = 0; i < 40; i++) { /* dead trees: a trunk and crooked branches */
      float x = -20 + rnd() * (t->length + 40), z = -TRACK_HALF_W - 3 - rnd() * 25, h = 3 + rnd() * 3, y = bank_y(x, z, kind);
      t3_mesh *tk = add(root, cyl, deadwood, true);
      tk->base.position = (t3_vec3){ x, y + h * 0.5f, z };
      tk->base.scale = (t3_vec3){ 0.15f, h, 0.15f };
      for (int k = 0; k < 4; k++) {
        float a = (rnd() - 0.5f) * 2.4f, l = 0.8f + rnd() * 1.2f, by = y + h * (0.5f + rnd() * 0.45f);
        t3_mesh *br = add(root, cyl, deadwood, true);
        br->base.position = (t3_vec3){ x + cw_sin(a) * l * 0.5f, by + cw_cos(a) * l * 0.5f, z };
        br->base.scale = (t3_vec3){ 0.05f, l, 0.05f };
        t3_object_set_rotation(&br->base, 0, 0, -a);
      }
    }
    for (float x = 5; x < t->length; x += 24) { /* lamp posts at the back edge */
      float y = track_height_at(x, NULL);
      t3_mesh *pst = add(root, cyl, iron, true);
      pst->base.position = (t3_vec3){ x, y + 1.5f, -TRACK_HALF_W - 0.6f };
      pst->base.scale = (t3_vec3){ 0.05f, 3, 0.05f };
      t3_mesh *lp = add(root, ball, lamp, false);
      lp->base.position = (t3_vec3){ x, y + 3.05f, -TRACK_HALF_W - 0.6f };
      lp->base.scale = (t3_vec3){ 0.16f, 0.2f, 0.16f };
    }
    /* the big moon */
    t3_material *moonm = glow(1, 0.97f, 0.85f, 1.6f);
    t3_mesh *mn = add(root, ball, moonm, false);
    mn->base.position = (t3_vec3){ t->length * 0.5f, track_height_at(t->length * 0.5f, NULL) + 80, -320 };
    mn->base.scale = (t3_vec3){ 36, 36, 36 };
    mn->base.frustum_culled = false;
    t3_material *rel[] = { wall, roof, win, stone, deadwood, lamp, iron, moonm };
    for (int i = 0; i < 8; i++) t3_release(rel[i]);
  } else if (!strcmp(n, "xmas")) {
    t3_material *wall = mat(TEX_WOOD, (float[3]){ 0.75f, 0.35f, 0.25f }, 1, 1, 1.2f, 0.85f);
    t3_material *roof = mat(TEX_SNOW, (float[3]){ 1, 1, 1 }, 1, 1, 1.0f, 0.6f), *win = glow(1, 0.75f, 0.35f, 2.5f);
    t3_material *pine = mat(TEX_LEAVES, (float[3]){ 0.3f, 0.5f, 0.3f }, 2, 2, 1.2f, 0.85f);
    t3_material *light[4] = { glow(1, 0.2f, 0.2f, 5), glow(0.2f, 1, 0.3f, 5), glow(1, 0.85f, 0.2f, 5), glow(0.3f, 0.5f, 1, 5) };
    t3_material *red = t3_mesh_physical_material_new(0xffffff), *white = t3_mesh_physical_material_new(0xffffff);
    red->color = lin(0.85f, 0.08f, 0.1f);
    white->color = lin(0.97f, 0.97f, 0.97f);
    red->clearcoat = white->clearcoat = 1;
    red->roughness = white->roughness = 0.3f;
    for (int i = 0; i < 22; i++) {
      float x = -20 + rnd() * (t->length + 40), z = -TRACK_HALF_W - 7 - rnd() * 28, w = 3 + rnd() * 2, h = 2.5f + rnd() * 2;
      float y = bank_y(x, z, kind) - 0.3f;
      house(root, box, cone4, wall, roof, win, x, y, z, w, h, 0);
      for (int k = 0; k < 12; k++) { /* string lights along the eaves */
        t3_mesh *b = add(root, ball, light[k & 3], false);
        b->base.position = (t3_vec3){ x - w * 0.5f + w * k / 11.0f, y + h + 0.05f, z + w * 0.42f };
        b->base.scale = (t3_vec3){ 0.07f, 0.07f, 0.07f };
      }
    }
    /* the big tree halfway, decorated, a star on top */
    float tx = t->length * 0.45f, tz = -TRACK_HALF_W - 9, ty = bank_y(tx, tz, kind);
    for (int k = 0; k < 4; k++) {
      t3_mesh *c = add(root, cone4, pine, true);
      c->base.position = (t3_vec3){ tx, ty + 3 + k * 2.2f, tz };
      c->base.scale = (t3_vec3){ 3.6f - k * 0.8f, 3.2f, 3.6f - k * 0.8f };
    }
    for (int k = 0; k < 40; k++) {
      float a = rnd() * 6.2831853f, hh = rnd() * 8, rr = (3.4f - hh * 0.35f) * 0.95f;
      t3_mesh *b = add(root, ball, light[k & 3], false);
      b->base.position = (t3_vec3){ tx + cw_cos(a) * rr, ty + 1.6f + hh, tz + cw_sin(a) * rr };
      b->base.scale = (t3_vec3){ 0.16f, 0.16f, 0.16f };
    }
    t3_mesh *st = add(root, ball, light[2], false);
    st->base.position = (t3_vec3){ tx, ty + 11.8f, tz };
    st->base.scale = (t3_vec3){ 0.5f, 0.5f, 0.5f };
    /* candy-cane rails along the back edge: red and white bands */
    for (float x = 0; x < t->length; x += 0.5f) {
      float y = track_height_at(x, NULL);
      t3_mesh *r = add(root, cyl, ((int)(x * 2)) & 1 ? red : white, false);
      r->base.position = (t3_vec3){ x, y + 0.55f, -TRACK_HALF_W - 0.3f };
      r->base.scale = (t3_vec3){ 0.045f, 0.5f, 0.045f };
      t3_object_set_rotation(&r->base, 0, 0, 1.5707963f);
    }
    /* gift boxes beside each kicker */
    for (int k = 0; k < t->kicker_count; k++) {
      if (t->kickers[k * 3 + 2] <= 0) continue;
      float x = t->kickers[k * 3], y = track_height_at(x, NULL);
      for (int j = 0; j < 3; j++) {
        float sc = 0.4f + rnd() * 0.3f;
        t3_mesh *g = add(root, box, light[(k + j) & 3], true);
        g->base.position = (t3_vec3){ x - 1 + j * 0.7f, y + sc * 0.5f, -TRACK_HALF_W - 0.9f };
        g->base.scale = (t3_vec3){ sc, sc, sc };
        t3_mesh *rb = add(root, box, white, true);
        rb->base.position = g->base.position;
        rb->base.scale = (t3_vec3){ sc * 1.02f, sc * 1.02f, sc * 0.15f };
      }
    }
    t3_material *rel[] = { wall, roof, win, pine, red, white, light[0], light[1], light[2], light[3] };
    for (int i = 0; i < 10; i++) t3_release(rel[i]);
  } else if (!strcmp(n, "moon")) {
    /* the Earth in the black sky, and boulders strewn across the regolith */
    t3_material *earth = t3_mesh_standard_material_new(0xffffff);
    earth->color = lin(0.25f, 0.45f, 0.85f);
    earth->emissive = lin(0.2f, 0.4f, 0.75f);
    earth->emissive_intensity = 0.9f;
    tex_apply(earth, TEX_MOSS, 1, 1, 0.5f, 1);
    t3_mesh *e = add(root, ball, earth, false);
    e->base.position = (t3_vec3){ t->length * 0.6f, track_height_at(t->length * 0.6f, NULL) + 70, -300 };
    e->base.scale = (t3_vec3){ 24, 24, 24 };
    e->base.frustum_culled = false;
    t3_release(earth);
  } else if (!strcmp(n, "beach")) {
    t3_material *trunk = mat(TEX_BARK, (float[3]){ 0.8f, 0.7f, 0.55f }, 1, 3, 1.4f, 0.9f);
    t3_material *frond = mat(TEX_LEAVES, (float[3]){ 0.45f, 0.75f, 0.35f }, 1, 1, 1.2f, 0.8f);

    t3_material *redm = t3_mesh_physical_material_new(0xffffff), *woodm = mat(TEX_WOOD, (float[3]){ 1, 0.95f, 0.85f }, 1, 1, 1.2f, 0.8f);
    redm->color = lin(0.9f, 0.18f, 0.12f);
    redm->roughness = 0.4f;
    redm->clearcoat = 1;
    for (int i = 0; i < 40; i++) { /* palms: a leaning segmented trunk, a crown of fronds */
      float x = -20 + rnd() * (t->length + 40), z = -TRACK_HALF_W - 3 - rnd() * 25, h = 5 + rnd() * 3, lean = (rnd() - 0.5f) * 0.5f;
      float y = bank_y(x, z, kind), px = x, py = y;
      for (int k = 0; k < 6; k++) {
        float a = lean * (k + 1) / 6.0f, l = h / 6;
        t3_mesh *sg = add(root, cyl, trunk, true);
        sg->base.position = (t3_vec3){ px + cw_sin(a) * l * 0.5f, py + cw_cos(a) * l * 0.5f, z };
        sg->base.scale = (t3_vec3){ 0.17f - k * 0.015f, l, 0.17f - k * 0.015f };
        t3_object_set_rotation(&sg->base, 0, 0, -a);
        px += cw_sin(a) * l;
        py += cw_cos(a) * l;
      }
      for (int k = 0; k < 7; k++) {
        float a = k * 0.8975979f;
        t3_mesh *f = add(root, cone4, frond, true);
        f->base.position = (t3_vec3){ px + cw_cos(a) * 1.1f, py - 0.25f, z + cw_sin(a) * 1.1f };
        f->base.scale = (t3_vec3){ 0.35f, 2.4f, 0.06f };
        t3_object_set_rotation(&f->base, 0, -a, 1.2f);
      }
    }
    for (float x = 40; x < t->length; x += 110) { /* lifeguard towers */
      float z = -TRACK_HALF_W - 5, y = bank_y(x, z, kind);
      for (int k = 0; k < 4; k++) {
        t3_mesh *l = add(root, cyl, woodm, true);
        l->base.position = (t3_vec3){ x + (k & 1 ? 0.8f : -0.8f), y + 1.5f, z + (k & 2 ? 0.8f : -0.8f) };
        l->base.scale = (t3_vec3){ 0.07f, 3, 0.07f };
      }
      t3_mesh *hut = add(root, box, redm, true);
      hut->base.position = (t3_vec3){ x, y + 3.6f, z };
      hut->base.scale = (t3_vec3){ 2, 1.3f, 2 };
      t3_mesh *rf = add(root, cone4, redm, true);
      rf->base.position = (t3_vec3){ x, y + 4.7f, z };
      rf->base.scale = (t3_vec3){ 1.7f, 0.9f, 1.7f };
      t3_object_set_rotation(&rf->base, 0, 0.7853982f, 0);
    }
    /* the sea in front: clear turquoise near the shore, ocean blue further
     * out, a glossy coat reflecting the sky */
    t3_material *sea = t3_mesh_physical_material_new(0xffffff), *deep = t3_mesh_physical_material_new(0xffffff);
    sea->color = lin(0.1f, 0.6f, 0.72f);
    deep->color = lin(0.07f, 0.34f, 0.66f);
    t3_material *both[2] = { sea, deep };
    for (int k = 0; k < 2; k++) {
      both[k]->roughness = 0.08f;
      both[k]->clearcoat = 1;
      both[k]->clearcoat_roughness = 0.05f;
      both[k]->env_map_intensity = 1.4f;
      tex_apply(both[k], TEX_SAND, 80, 80, 0.45f, 1);
    }
    t3_geometry *pl = t3_plane_geometry_new(1, 1, 1, 1);
    t3_mesh *sm = add(root, pl, sea, false);
    sm->base.position = (t3_vec3){ t->length * 0.5f, t->min_y - 6, 45 };
    sm->base.scale = (t3_vec3){ t->length + 1200, 90, 1 };
    t3_object_set_rotation(&sm->base, -1.5707963f, 0, 0);
    t3_mesh *dm = add(root, pl, deep, false);
    dm->base.position = (t3_vec3){ t->length * 0.5f, t->min_y - 6.02f, 200 };
    dm->base.scale = (t3_vec3){ t->length + 1200, 240, 1 };
    t3_object_set_rotation(&dm->base, -1.5707963f, 0, 0);
    /* and the open sea on the horizon behind the dunes */
    t3_mesh *bm = add(root, pl, deep, false);
    bm->base.position = (t3_vec3){ t->length * 0.5f, t->min_y + 4, -700 };
    bm->base.scale = (t3_vec3){ t->length + 3000, 1100, 1 };
    t3_object_set_rotation(&bm->base, -1.5707963f, 0, 0);
    t3_release(deep);
    t3_release(pl);
    t3_material *rel[] = { trunk, frond, redm, woodm, sea };
    for (int i = 0; i < 5; i++) t3_release(rel[i]);
  } else if (!strcmp(n, "mars")) {
    /* a rover and a lander beside the track, rock gardens */
    t3_material *body = t3_mesh_physical_material_new(0xffffff), *foil = t3_mesh_physical_material_new(0xffffff);
    body->color = lin(0.85f, 0.85f, 0.82f);
    body->roughness = 0.4f;
    body->metalness = 0.3f;
    foil->color = lin(0.95f, 0.75f, 0.3f);
    foil->metalness = 1;
    foil->roughness = 0.35f;
    foil->iridescence = 0.6f;
    t3_material *dark = t3_mesh_standard_material_new(0xffffff);
    dark->color = lin(0.12f, 0.12f, 0.13f);
    dark->roughness = 0.6f;
    float rx = t->length * 0.3f, rz = -TRACK_HALF_W - 4, ry = bank_y(rx, rz, kind);
    t3_mesh *rv = add(root, box, body, true);
    rv->base.position = (t3_vec3){ rx, ry + 0.9f, rz };
    rv->base.scale = (t3_vec3){ 2.4f, 0.6f, 1.6f };
    for (int k = 0; k < 6; k++) {
      t3_mesh *w = add(root, cyl, dark, true);
      w->base.position = (t3_vec3){ rx - 1 + (k % 3) * 1.0f, ry + 0.35f, rz + (k < 3 ? 0.95f : -0.95f) };
      w->base.scale = (t3_vec3){ 0.35f, 0.25f, 0.35f };
      t3_object_set_rotation(&w->base, 1.5707963f, 0, 0);
    }
    t3_mesh *mast = add(root, cyl, body, true);
    mast->base.position = (t3_vec3){ rx + 0.8f, ry + 1.8f, rz + 0.4f };
    mast->base.scale = (t3_vec3){ 0.06f, 1.4f, 0.06f };
    t3_mesh *cam = add(root, box, dark, true);
    cam->base.position = (t3_vec3){ rx + 0.8f, ry + 2.55f, rz + 0.4f };
    cam->base.scale = (t3_vec3){ 0.35f, 0.2f, 0.25f };
    float lx = t->length * 0.7f, lz = -TRACK_HALF_W - 9, ly = bank_y(lx, lz, kind);
    t3_mesh *ld = add(root, cyl, foil, true);
    ld->base.position = (t3_vec3){ lx, ly + 1.6f, lz };
    ld->base.scale = (t3_vec3){ 1.5f, 1.2f, 1.5f };
    for (int k = 0; k < 4; k++) {
      float a = k * 1.5707963f;
      t3_mesh *leg = add(root, cyl, body, true);
      leg->base.position = (t3_vec3){ lx + cw_cos(a) * 1.6f, ly + 0.6f, lz + cw_sin(a) * 1.6f };
      leg->base.scale = (t3_vec3){ 0.06f, 1.5f, 0.06f };
      t3_object_set_rotation(&leg->base, cw_sin(a) * 0.5f, 0, -cw_cos(a) * 0.5f);
    }
    t3_mesh *dish = add(root, ball, body, true);
    dish->base.position = (t3_vec3){ lx, ly + 2.5f, lz };
    dish->base.scale = (t3_vec3){ 0.8f, 0.2f, 0.8f };
    t3_material *rock = mat(TEX_ROCK, (float[3]){ 0.7f, 0.42f, 0.3f }, 1, 1, 1.5f, 0.9f);
    t3_geometry *bld = t3_icosahedron_geometry_new(1, 1);
    for (int i = 0; i < 160; i++) {
      float x = rnd() * t->length, z = -TRACK_HALF_W - 1 - rnd() * 22, sc = 0.15f + rnd() * 0.7f, a1 = rnd() * 3, a2 = rnd() * 3;
      t3_mesh *m = add(root, bld, rock, true);
      m->base.position = (t3_vec3){ x, bank_y(x, z, kind) + sc * 0.2f, z };
      m->base.scale = (t3_vec3){ sc, sc * 0.6f, sc * 0.8f };
      t3_object_set_rotation(&m->base, a1, a2, 0);
    }
    t3_release(bld);
    t3_material *rel[] = { body, foil, dark, rock };
    for (int i = 0; i < 4; i++) t3_release(rel[i]);
  }
  t3_release(box);
  t3_release(cone4);
  t3_release(cyl);
  t3_release(ball);
}

/* the icy patches: a glossy pale sheen laid on the riding surface */
static void ice_patches(t3_object *root, const track_desc *t) {
  if (!t->ice_count) return;
  t3_material *ice = t3_mesh_physical_material_new(0xffffff);
  ice->color = lin(0.75f, 0.88f, 1);
  ice->roughness = 0.08f;
  ice->clearcoat = 1;
  ice->clearcoat_roughness = 0.03f;
  ice->transparent = true;
  ice->opacity = 0.7f;
  mb b = { 0 };
  for (int i = 0; i < t->ice_count; i++) {
    float x0 = t->ice[i * 2], x1 = t->ice[i * 2 + 1];
    int n = (int)((x1 - x0) / 0.25f), base = b.nv;
    for (int k = 0; k <= n; k++) {
      float x = x0 + (x1 - x0) * k / n, y = track_height_at(x, NULL) + 0.02f;
      mb_v(&b, (t3_vec3){ x, y, -TRACK_HALF_W + 0.1f }, (t3_vec3){ 0, 1, 0 }, x / 3, 0, 1);
      mb_v(&b, (t3_vec3){ x, y, TRACK_HALF_W - 0.25f }, (t3_vec3){ 0, 1, 0 }, x / 3, 1, 1);
      if (k > 0) {
        mb_t(&b, base + (k - 1) * 2, base + (k - 1) * 2 + 1, base + k * 2 + 1);
        mb_t(&b, base + (k - 1) * 2, base + k * 2 + 1, base + k * 2);
      }
    }
  }
  t3_geometry *g = mb_done(&b);
  add(root, g, ice, false);
  t3_release(g);
  t3_release(ice);
}

/* the bridge planks, drawn where Box2D has them (they fall after you) */
#define MAX_PLANK_MESH 96
static t3_mesh *plank_mesh[MAX_PLANK_MESH];
static int n_plank_mesh;
static void planks_build(t3_object *root) {
  cw_xform2 x[MAX_PLANK_MESH];
  float hw[MAX_PLANK_MESH];
  n_plank_mesh = bike_planks(1, x, hw, MAX_PLANK_MESH);
  if (!n_plank_mesh) return;
  t3_material *wood = mat(TEX_WOOD, (float[3]){ 0.85f, 0.75f, 0.62f }, 1, 1, 1.2f, 0.85f);
  t3_geometry *box = t3_box_geometry_new(1, 0.1f, TRACK_HALF_W * 2 - 0.2f, 1, 1, 1);
  for (int i = 0; i < n_plank_mesh; i++) {
    plank_mesh[i] = add(root, box, wood, true);
    plank_mesh[i]->base.scale = (t3_vec3){ hw[i] * 2, 1, 1 };
  }
  t3_release(box);
  t3_release(wood);
}


/* the high routes: a plank deck on posts down to the low line */
static void highs_build(t3_object *root, const track_desc *t) {
  if (!t->high_count) return;
  t3_material *wood = mat(TEX_WOOD, (float[3]){ 0.85f, 0.75f, 0.62f }, 1, 1, 1.2f, 0.85f);
  t3_material *post = mat(TEX_WOOD, (float[3]){ 0.55f, 0.45f, 0.38f }, 1, 4, 1.2f, 0.9f);
  t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  for (int i = 0; i < t->high_count; i++) {
    float x0 = t->highs[i * 4], x1 = t->highs[i * 4 + 1], y0 = t->highs[i * 4 + 2], y1 = t->highs[i * 4 + 3];
    float dx = x1 - x0, dy = y1 - y0;
    t3_mesh *d = add(root, box, wood, true);
    d->base.position = (t3_vec3){ (x0 + x1) * 0.5f, (y0 + y1) * 0.5f - 0.08f, -0.3f };
    d->base.scale = (t3_vec3){ sqrtf(dx * dx + dy * dy), 0.16f, TRACK_HALF_W * 1.4f };
    t3_object_set_rotation(&d->base, 0, 0, cw_atan2(dy, dx));
    for (float x = x0 + 1; x < x1; x += 4) {
      float y = y0 + dy * (x - x0) / dx;
      float g = track_height_at(x, NULL), h = y - 0.16f - g;
      if (h < 0.3f) continue;
      for (int s2 = -1; s2 <= 1; s2 += 2) {
        t3_mesh *p = add(root, box, post, true);
        p->base.position = (t3_vec3){ x, g + h * 0.5f, -0.3f + s2 * TRACK_HALF_W * 0.6f };
        p->base.scale = (t3_vec3){ 0.16f, h, 0.16f };
      }
    }
  }
  t3_release(box);
  t3_release(wood);
  t3_release(post);
}

/* the loops: a plank band round the circle the bike rides inside, drifting
 * a little across the track as it goes round (so the way in and the way out
 * pass side by side, as a real loop's do), its outside and edges dark
 * timber, on posts down to the ground */
static void quad_n(mb *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, t3_vec3 n, float u0, float u1, float v0, float v1) {
  int a = mb_v(b, p0, n, u0, v0, 1), c = mb_v(b, p1, n, u1, v0, 1), d = mb_v(b, p2, n, u1, v1, 1), e = mb_v(b, p3, n, u0, v1, 1);
  t3_vec3 e1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z }, e2 = { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
  float dot = (e1.y * e2.z - e1.z * e2.y) * n.x + (e1.z * e2.x - e1.x * e2.z) * n.y + (e1.x * e2.y - e1.y * e2.x) * n.z;
  if (dot >= 0) mb_t(b, a, c, d), mb_t(b, a, d, e);
  else mb_t(b, a, d, c), mb_t(b, a, e, d);
}
/* Scary Town's loop: lanterns with a pale green glow on crooked posts along
 * its run-in, the last at its foot */
static void ghost_lanterns(t3_object *root, const track_desc *t, float cx, float r) {
  t3_material *iron = t3_mesh_standard_material_new(0xffffff);
  iron->color = lin(0.16f, 0.15f, 0.15f);
  iron->metalness = 0.85f;
  iron->roughness = 0.45f;
  t3_material *post_mat = mat(TEX_WOOD, (float[3]){ 0.3f, 0.25f, 0.22f }, 1, 1, 1.2f, 0.95f);
  t3_material *ghost = glow(0.55f, 1, 0.7f, 3.5f);
  /* each: a post, a dark cage, a glowing core; both sides of the track */
  t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1), *core = t3_sphere_geometry_new(1, 12, 8);
  float xs[12];
  int n = 0;
  for (float x = cx - r - 34; x < cx - r - 2 && n < 8; x += 8) xs[n++] = x;
  xs[n++] = cx - r - 1.2f;
  for (int i = 0; i < n; i++)
    for (int side = -1; side <= -1; side += 2) { /* the back verge only: nothing between the camera and the riders */
      bool f;
      float x = xs[i], gy = track_height_at(x, &f), z = side * (TRACK_HALF_W + 0.55f), lean = 0.06f * ((i * 7 + side * 3) % 5 - 2);
      if (!f) continue;
      float h = 1.9f + 0.25f * ((i + side) & 1);
      t3_mesh *p = add(root, box, post_mat, true);
      p->base.position = (t3_vec3){ x, gy + h * 0.5f - 0.1f, z };
      p->base.scale = (t3_vec3){ 0.1f, h, 0.1f };
      t3_object_set_rotation(&p->base, lean, 0, lean * 0.5f);
      t3_mesh *arm = add(root, box, iron, false); /* a bracket over the track side, the lantern hanging from it */
      arm->base.position = (t3_vec3){ x, gy + h - 0.12f, z - side * 0.22f };
      arm->base.scale = (t3_vec3){ 0.04f, 0.04f, 0.45f };
      t3_mesh *cage = add(root, box, iron, false);
      cage->base.position = (t3_vec3){ x, gy + h - 0.42f, z - side * 0.4f };
      cage->base.scale = (t3_vec3){ 0.3f, 0.4f, 0.3f };
      t3_mesh *c = add(root, core, ghost, false);
      c->base.position = (t3_vec3){ x, gy + h - 0.42f, z - side * 0.4f - side * 0.151f };
      c->base.scale = (t3_vec3){ 0.14f, 0.18f, 0.14f };
    }
  t3_release(box);
  t3_release(core);
  t3_release(iron);
  t3_release(post_mat);
  t3_release(ghost);
  (void)t;
}

/* a loop's sideways shift as it goes round (0..1 of two lanes): none on the
 * lower right (the way in rises from the back lanes), all of it by the lower
 * left (the way out comes down onto the front lanes), the move across the top */
static float loop_shift(float t) { return cw_smoothstep(0.25f, 0.75f, t); }

static void loops_build(t3_object *root, const track_desc *t) {
  if (!t->loop_count) return;
  t3_material *deck = mat(TEX_WOOD, (float[3]){ 0.9f, 0.8f, 0.66f }, 1, 1, 1.2f, 0.8f);
  t3_material *frame = mat(TEX_WOOD, (float[3]){ 0.42f, 0.34f, 0.28f }, 1, 1, 1.2f, 0.9f);
  t3_geometry *box = t3_box_geometry_new(1, 1, 1, 1, 1, 1);
  for (int i = 0; i < t->loop_count; i++) {
    float cx = t->loops[i * 3], y0 = t->loops[i * 3 + 1], r = t->loops[i * 3 + 2], cy = y0 + r;
    /* a band two lanes wide (and a little), a helix: in on lanes 0-1, out on lanes 2-3 */
    const float W = LANE_W + 0.12f, TH = 0.22f, ZIN = lane_z(0.5f), ZSH = 2 * LANE_W;
    enum { N = 96 };
    mb in = { 0 }, out = { 0 };
    for (int k = 0; k < N; k++) {
      float t0 = (float)k / N, t1 = (float)(k + 1) / N;
      float a0 = -1.5707963f + 6.2831853f * t0, a1 = -1.5707963f + 6.2831853f * t1;
      float c0 = cw_cos(a0), s0 = cw_sin(a0), c1 = cw_cos(a1), s1 = cw_sin(a1);
      float z0 = ZIN + ZSH * loop_shift(t0), z1 = ZIN + ZSH * loop_shift(t1);
      t3_vec3 i0a = { cx + r * c0, cy + r * s0, z0 - W }, i0b = { cx + r * c0, cy + r * s0, z0 + W };
      t3_vec3 i1a = { cx + r * c1, cy + r * s1, z1 - W }, i1b = { cx + r * c1, cy + r * s1, z1 + W };
      float R2 = r + TH;
      t3_vec3 o0a = { cx + R2 * c0, cy + R2 * s0, z0 - W }, o0b = { cx + R2 * c0, cy + R2 * s0, z0 + W };
      t3_vec3 o1a = { cx + R2 * c1, cy + R2 * s1, z1 - W }, o1b = { cx + R2 * c1, cy + R2 * s1, z1 + W };
      float cm = (c0 + c1) * 0.5f, sm = (s0 + s1) * 0.5f, arc = r * 6.2831853f;
      /* the riding surface faces the centre; planks run across it */
      quad_n(&in, i0a, i1a, i1b, i0b, (t3_vec3){ -cm, -sm, 0 }, t0 * arc / 1.2f, t1 * arc / 1.2f, 0, 2.5f);
      quad_n(&out, o0a, o1a, o1b, o0b, (t3_vec3){ cm, sm, 0 }, t0 * arc, t1 * arc, 0, 2);
      quad_n(&out, i0b, i1b, o1b, o0b, (t3_vec3){ 0, 0, 1 }, t0 * arc, t1 * arc, 0, 0.3f);
      quad_n(&out, i0a, i1a, o1a, o0a, (t3_vec3){ 0, 0, -1 }, t0 * arc, t1 * arc, 0, 0.3f);
    }
    t3_geometry *gi = mb_done(&in), *go = mb_done(&out);
    add(root, gi, deck, true);
    add(root, go, frame, true);
    t3_release(gi);
    t3_release(go);
    /* supports, all behind the track (nothing between the camera and the
     * loop, nothing in a lane): posts past the back verge, and where the band
     * is high enough to clear a rider's head a beam out to its back edge */
    static const float AT[] = { 0.36f, 0.64f }; /* two masts behind, either side of the top */
    for (int k = 0; k < 2; k++) {
      float tt = AT[k], a = -1.5707963f + 6.2831853f * tt;
      float px = cx + (r + TH) * cw_cos(a), py = cy + (r + TH) * cw_sin(a), g = track_height_at(px, NULL);
      if (py - g < 2.6f && tt > 0.1f && tt < 0.9f) continue; /* low on the sides: a beam there would cross a lane */
      float zb = ZIN + ZSH * loop_shift(tt) - W, zp = -TRACK_HALF_W - 0.45f;
      t3_mesh *p = add(root, box, frame, true);
      p->base.position = (t3_vec3){ px, g + (py - g) * 0.5f - 0.05f, zp };
      p->base.scale = (t3_vec3){ 0.26f, py - g + 0.1f, 0.26f };
      if (zb - zp > 0.3f) { /* the beam across to the band's back edge */
        t3_mesh *bm = add(root, box, frame, true);
        bm->base.position = (t3_vec3){ px, py, (zp + zb) * 0.5f };
        bm->base.scale = (t3_vec3){ 0.16f, 0.16f, zb - zp };
      }
    }
    if (!strcmp(t->theme, "scary")) ghost_lanterns(root, t, cx, r);
  }
  t3_release(box);
  t3_release(deck);
  t3_release(frame);
}

/* the set pieces' meshes, posed every frame from the sim */
#define MAX_MOVER_MESH 24
static t3_object *mover_obj[MAX_MOVER_MESH];
static int mover_kind[MAX_MOVER_MESH], n_mover_obj;
static t3_mesh *chain_mesh[4];
static void movers_build(t3_object *root) {
  n_mover_obj = 0;
  mover_pose mp[MAX_MOVER_MESH];
  int n = bike_movers(1, mp, MAX_MOVER_MESH);
  int logs = 0;
  for (int i = 0; i < n; i++) logs += mp[i].kind == MOVER_LOGS;
  t3_material *wood = mat(TEX_WOOD, (float[3]){ 0.9f, 0.8f, 0.68f }, 1, 1, 1.2f, 0.85f);
  t3_material *iron = t3_mesh_physical_material_new(0xffffff);
  iron->color = lin(0.12f, 0.12f, 0.13f);
  iron->metalness = 0.9f;
  iron->roughness = 0.35f;
  iron->clearcoat = 0.4f;
  t3_material *bark = mat(TEX_BARK, (float[3]){ 0.8f, 0.65f, 0.5f }, 1, 1, 1.4f, 0.9f);
  t3_material *car = t3_mesh_physical_material_new(0xffffff), *lit = glow(1, 0.85f, 0.5f, 3);
  car->color = lin(0.75f, 0.12f, 0.1f);
  car->metalness = 0.5f;
  car->roughness = 0.35f;
  car->clearcoat = 1;
  int chains = 0;
  for (int i = 0; i < n && n_mover_obj < MAX_MOVER_MESH; i++) {
    t3_object *g = t3_group_new();
    t3_object_add(root, g);
    t3_release(g);
    int k = mp[i].kind;
    if (k == MOVER_DRAWBRIDGE || k == MOVER_SEESAW) {
      t3_geometry *b = t3_box_geometry_new(mp[i].a, k == MOVER_SEESAW ? 0.12f : 0.1f, TRACK_HALF_W * 2 - 0.3f, 1, 1, 1);
      t3_mesh *me = add(g, b, wood, true);
      me->base.position = (t3_vec3){ k == MOVER_DRAWBRIDGE ? mp[i].a * 0.5f : 0, k == MOVER_DRAWBRIDGE ? -0.07f : 0, 0 };
      for (int c = 0; c < 3; c++) { /* iron bands */
        t3_geometry *bb = t3_box_geometry_new(0.08f, 0.13f, TRACK_HALF_W * 2 - 0.2f, 1, 1, 1);
        t3_mesh *band = add(g, bb, iron, true);
        band->base.position = (t3_vec3){ (k == MOVER_DRAWBRIDGE ? mp[i].a * 0.5f : 0) + (c - 1) * mp[i].a * 0.35f, me->base.position.y, 0 };
        t3_release(bb);
      }
      t3_release(b);
      if (k == MOVER_SEESAW) { /* the fulcrum, fixed */
        t3_geometry *f = t3_cone_geometry_new(0.45f, 0.45f, 3, 1, false);
        t3_mesh *fm = add(root, f, iron, true);
        fm->base.position = (t3_vec3){ mp[i].x.p.x, mp[i].x.p.y - 0.2f, 0 };
        fm->base.scale = (t3_vec3){ 1, 1, 3.4f };
        t3_release(f);
      }
    } else if (k == MOVER_WRECK) {
      t3_geometry *ball = t3_sphere_geometry_new(0.6f, 24, 16);
      add(g, ball, iron, true);
      t3_release(ball);
      if (chains < 4) {
        t3_geometry *cy = t3_cylinder_geometry_new(1, 1, 1, 8, 1, false);
        chain_mesh[chains] = add(root, cy, iron, true);
        chains++;
        t3_release(cy);
      }
    } else if (k == MOVER_LOGS) {
      t3_geometry *lg = t3_cylinder_geometry_new(0.12f, 0.12f, TRACK_HALF_W * 1.4f, 12, 1, false);
      t3_mesh *me = add(g, lg, bark, true);
      t3_object_set_rotation(&me->base, 1.5707963f, 0, 0);
      t3_release(lg);
    } else if (k == MOVER_TRAIN) { /* four cars nose to tail along z, with lit windows */
      t3_geometry *bx = t3_box_geometry_new(2.6f, 2.6f, 9, 1, 1, 1), *wn = t3_box_geometry_new(2.7f, 0.5f, 7.5f, 1, 1, 1);
      for (int c = 0; c < 4; c++) {
        t3_mesh *cm = add(g, bx, car, true);
        cm->base.position = (t3_vec3){ 0, 1.3f, -c * 9.6f };
        t3_mesh *wm = add(g, wn, lit, false);
        wm->base.position = (t3_vec3){ 0, 1.8f, -c * 9.6f };
      }
      t3_release(bx);
      t3_release(wn);
    }
    mover_obj[n_mover_obj] = g;
    mover_kind[n_mover_obj++] = k;
  }
  /* the log pool: more than are alive now */
  for (int i = logs; i < (logs ? 6 : 0) && n_mover_obj < MAX_MOVER_MESH; i++) {
    t3_object *g = t3_group_new();
    t3_object_add(root, g);
    t3_release(g);
    t3_geometry *lg = t3_cylinder_geometry_new(0.12f, 0.12f, TRACK_HALF_W * 1.4f, 12, 1, false);
    t3_mesh *me = add(g, lg, bark, true);
    t3_object_set_rotation(&me->base, 1.5707963f, 0, 0);
    t3_release(lg);
    g->visible = false;
    mover_obj[n_mover_obj] = g;
    mover_kind[n_mover_obj++] = MOVER_LOGS;
  }
  t3_release(wood);
  t3_release(iron);
  t3_release(bark);
  t3_release(car);
  t3_release(lit);
}
static void movers_frame(float alpha) {
  mover_pose mp[MAX_MOVER_MESH];
  int n = bike_movers(alpha, mp, MAX_MOVER_MESH);
  int used[MAX_MOVER_MESH] = { 0 }, chain = 0;
  for (int i = 0; i < n; i++) {
    int j = 0;
    while (j < n_mover_obj && (used[j] || mover_kind[j] != mp[i].kind)) j++;
    if (j == n_mover_obj) continue;
    used[j] = 1;
    t3_object *o = mover_obj[j];
    o->visible = true;
    if (mp[i].kind == MOVER_TRAIN) o->position = (t3_vec3){ mp[i].x.p.x, mp[i].x.p.y, mp[i].a };
    else {
      o->position = (t3_vec3){ mp[i].x.p.x, mp[i].x.p.y, 0 };
      t3_object_set_rotation(o, 0, 0, cw_atan2(mp[i].x.q.s, mp[i].x.q.c));
    }
    if (mp[i].kind == MOVER_WRECK && chain < 4 && chain_mesh[chain]) {
      t3_mesh *c = chain_mesh[chain++];
      float dx = mp[i].x.p.x - mp[i].a, dy = mp[i].x.p.y - mp[i].b, l = sqrtf(dx * dx + dy * dy);
      c->base.position = (t3_vec3){ (mp[i].x.p.x + mp[i].a) * 0.5f, (mp[i].x.p.y + mp[i].b) * 0.5f, 0 };
      c->base.scale = (t3_vec3){ 0.04f, l, 0.04f };
      t3_object_set_rotation(&c->base, 0, 0, cw_atan2(dy, dx) - 1.5707963f);
    }
  }
  for (int j = 0; j < n_mover_obj; j++)
    if (!used[j] && mover_kind[j] == MOVER_LOGS) mover_obj[j]->visible = false;
}

void view_frame(float alpha) {
  movers_frame(alpha);
  flags_wave(1.0f / 60);
  pumpkins_frame(1.0f / 60);
  cw_xform2 x[MAX_PLANK_MESH];
  float hw[MAX_PLANK_MESH];
  int n = bike_planks(alpha, x, hw, MAX_PLANK_MESH);
  for (int i = 0; i < n && i < n_plank_mesh; i++) {
    plank_mesh[i]->base.position = (t3_vec3){ x[i].p.x, x[i].p.y, 0 };
    t3_object_set_rotation(&plank_mesh[i]->base, 0, 0, cw_atan2(x[i].q.s, x[i].q.c));
  }
}

/* ── the still scenery, merged ───────────────────────────────────────
 * The landforms, rocks and theme props are thousands of single meshes,
 * most of them a few shapes in a few materials, and far off screen. The
 * meshes of each material (and shadow flags) are merged per STILL_CHUNK
 * metres of track into one mesh, their placement baked in: the renderer
 * culls a chunk off screen (camera and shadow map) on its bounds, as it
 * culled each mesh, and draws what is left in one draw per material. Every
 * view in split screen draws the scenery again, so this is most of what
 * keeps four views cheap. Only meshes nothing animates (the pumpkins'
 * flames are left alone), opaque. */
#define STILL_CHUNK 256.0f
static bool still_ok(t3_object *o) {
  if (o->type != T3_MESH || o->child_count || !o->visible) return false;
  t3_mesh *m = (t3_mesh *)o;
  if (m->material_count != 1 || !geo_mergeable(m->geometry)) return false;
  const t3_material *mt = m->materials[0];
  if (mt->transparent || mt->transmission > 0) return false;
  for (int i = 0; i < n_pk; i++)
    if (pks[i].flame == m) return false;
  return true;
}
static void merge_still(t3_object *root, int a, int b) {
  typedef struct { t3_material *m; unsigned mask; bool cast, recv; int chunk, n, first; } run;
  int nc = b - a;
  if (nc <= 0) return;
  run *runs = malloc(sizeof *runs * (size_t)nc);
  int *run_of = malloc(sizeof(int) * (size_t)nc), nr = 0;
  /* runs in the order their first mesh was made: the same on every host */
  for (int i = 0; i < nc; i++) {
    t3_object *o = root->children[a + i];
    run_of[i] = -1;
    if (!still_ok(o)) continue;
    t3_mesh *m = (t3_mesh *)o;
    int chunk = (int)floorf(o->position.x / STILL_CHUNK);
    unsigned mask = geo_merge_mask(m->geometry);
    int r = 0;
    while (r < nr && !(runs[r].m == m->materials[0] && runs[r].mask == mask && runs[r].cast == o->cast_shadow &&
                       runs[r].recv == o->receive_shadow && runs[r].chunk == chunk))
      r++;
    if (r == nr) runs[nr++] = (run){ m->materials[0], mask, o->cast_shadow, o->receive_shadow, chunk, 0, i };
    runs[r].n++;
    run_of[i] = r;
  }
  t3_object **gone = malloc(sizeof *gone * (size_t)nc);
  const t3_geometry **gs = malloc(sizeof *gs * (size_t)nc);
  const t3_mat4 **ms = malloc(sizeof *ms * (size_t)nc);
  int ngone = 0;
  for (int r = 0; r < nr; r++) {
    if (runs[r].n < 2) continue;
    int k = 0;
    for (int i = runs[r].first; i < nc; i++) {
      if (run_of[i] != r) continue;
      t3_object *o = root->children[a + i];
      t3_object_update_matrix(o);
      gs[k] = ((t3_mesh *)o)->geometry;
      ms[k++] = &o->matrix;
      gone[ngone++] = o;
    }
    t3_geometry *g = geo_merge(gs, ms, k, runs[r].mask);
    t3_mesh *mm = t3_mesh_new(g, runs[r].m);
    t3_release(g);
    mm->base.cast_shadow = runs[r].cast;
    mm->base.receive_shadow = runs[r].recv;
    t3_object_add(root, mm);
    t3_release(mm);
  }
  for (int i = 0; i < ngone; i++) t3_object_remove(root, gone[i]);
  free(ms);
  free(gs);
  free(gone);
  free(run_of);
  free(runs);
}

/* ── particles ─────────────────────────────────────────────────────── */

static cw_node dust, burst_n, motes;
static float wx_dy = 3, wx_dz = -2; /* where the weather rides, relative to the rider */

static cw_node particles(cw_node parent, const char *name, const char *const *kv) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(parent, n);
  for (int i = 0; kv[i]; i += 2) {
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    cw_value v;
    if (p && p->type == CW_T_ENUM) cw_node_set(n, kv[i], cw_val_enum(kv[i + 1]));
    else if (p && cw_value_parse(kv[i + 1], p->type, &v)) cw_node_set(n, kv[i], v);
    else cw_warn("bikemesa: bad particle setting %s = %s", kv[i], kv[i + 1]);
  }
  return n;
}

void view_build(cw_node parent, const track_desc *t, int track_index) {
  const theme *th = theme_for(t->theme);
  S = (uint32_t)(track_index + 1) * 2654435761u + 11;
  bank_kind = th->far_kind == 1 ? 1 : 0;
  t3_object *root = t3_group_new();
  ground_build(t);
  ribbon(root, t, th);
  bank(root, t, th);
  markers(root, t);
  int still0 = root->child_count;
  landforms(root, t, th);
  fill(root, t, th);
  props(root, t, th);
  props2(root, t, th);
  merge_still(root, still0, root->child_count);
  ice_patches(root, t);
  planks_build(root);
  movers_build(root);
  highs_build(root, t);
  loops_build(root, t);
  cw_node n = cw_node_new("Node3D", "World");
  cw_node_add_child(parent, n);
  cw_node3d_set_object(n, root);
  t3_release(root);

  static const char *const dust_kv[] = { "amount", "300", "lifetime", "1.4", "emission_shape", "SPHERE", "emission_radius", "0.15",
    "direction", "vec3(-1, 0.6, 0)", "spread", "35", "speed_min", "0.6", "speed_max", "2.4", "gravity", "vec3(0, -1.2, 0)",
    "damping", "1.2", "turbulence", "0.6", "turbulence_scale", "0.6", "size", "0.32", "size_random", "0.5",
    "size_curve", "\"0 0.3, 0.3 1, 1 1.6\"", "blend", "ALPHA", "emitting", "false",
    "color_ramp", "\"0 0.82 0.66 0.5 0, 0.1 0.82 0.66 0.5 0.45, 1 0.8 0.68 0.55 0\"", NULL };
  dust = particles(parent, "Dust", dust_kv);
  static const char *const burst_kv[] = { "amount", "90", "lifetime", "1.1", "one_shot", "true", "explosiveness", "1",
    "emission_shape", "SPHERE", "emission_radius", "0.3", "spread", "180", "speed_min", "1.5", "speed_max", "5.5",
    "gravity", "vec3(0, -6, 0)", "damping", "1.5", "size", "0.22", "size_random", "0.6", "blend", "ALPHA", "emitting", "false",
    "size_curve", "\"0 0.6, 0.2 1, 1 1.4\"",
    "color_ramp", "\"0 0.85 0.7 0.55 0.9, 1 0.8 0.7 0.6 0\"", NULL };
  burst_n = particles(parent, "Burst", burst_kv);
  /* the weather: one system per theme, riding along with the rider */
  static const char *const motes_kv[] = { "amount", "500", "lifetime", "8", "emission_shape", "BOX",
    "emission_extents", "vec3(30, 6, 10)", "spread", "180", "speed_min", "0.05", "speed_max", "0.3",
    "gravity", "vec3(0.25, 0.02, 0.08)", "turbulence", "0.5", "turbulence_scale", "0.5", "size", "0.04", "size_random", "0.6",
    "blend", "ADD", "energy", "2", "local_coords", "false", "preprocess", "8",
    "color_ramp", "\"0 1 0.85 0.6 0, 0.3 1 0.85 0.6 0.7, 0.7 1 0.8 0.55 0.7, 1 1 0.8 0.5 0\"", NULL };
  static const char *const snow_kv[] = { "amount", "1500", "lifetime", "6", "emission_shape", "BOX",
    "emission_extents", "vec3(32, 1, 14)", "direction", "vec3(0.2, -1, 0)", "spread", "15", "speed_min", "1.2", "speed_max", "2.4",
    "gravity", "vec3(0.3, -0.3, 0)", "turbulence", "0.8", "turbulence_scale", "0.4", "size", "0.06", "size_random", "0.5",
    "blend", "ALPHA", "preprocess", "6", "color_ramp", "\"0 1 1 1 0, 0.1 1 1 1 0.95, 0.9 1 1 1 0.95, 1 1 1 1 0\"", NULL };
  static const char *const rain_kv[] = { "amount", "2400", "lifetime", "1.2", "emission_shape", "BOX",
    "emission_extents", "vec3(30, 1, 12)", "direction", "vec3(-0.3, -1, 0)", "spread", "4", "speed_min", "16", "speed_max", "20",
    "draw_mode", "STRETCHED", "size", "0.03", "blend", "ADD", "energy", "0.6", "preprocess", "2",
    "color_ramp", "\"0 0.7 0.8 0.9 0.5, 1 0.7 0.8 0.9 0.5\"", NULL };
  static const char *const leaves_kv[] = { "amount", "260", "lifetime", "9", "emission_shape", "BOX",
    "emission_extents", "vec3(30, 4, 10)", "direction", "vec3(0.9, -0.3, 0.2)", "spread", "30", "speed_min", "0.6", "speed_max", "1.8",
    "gravity", "vec3(0, -0.35, 0)", "turbulence", "1.4", "turbulence_scale", "0.35", "draw_mode", "MESH", "mesh_primitive", "BOX",
    "size", "0.09", "size_random", "0.4", "spin", "3", "blend", "OPAQUE", "preprocess", "9",
    "color_ramp", "\"0 1 0.55 0.15 1, 0.5 0.95 0.35 0.1 1, 1 0.9 0.7 0.2 1\"", NULL };
  static const char *const stars_kv[] = { "amount", "1200", "lifetime", "5", "emission_shape", "BOX",
    "emission_extents", "vec3(60, 25, 4)", "spread", "180", "speed_min", "0", "speed_max", "0.05", "size", "0.12", "size_random", "0.7",
    "blend", "ADD", "energy", "3", "preprocess", "5", "color_ramp", "\"0 0.8 0.9 1 0, 0.5 0.9 0.85 1 1, 1 1 0.8 1 0\"", NULL };
  static const char *const sand_kv[] = { "amount", "700", "lifetime", "2.5", "emission_shape", "BOX",
    "emission_extents", "vec3(30, 2, 10)", "direction", "vec3(1, 0.05, 0)", "spread", "6", "speed_min", "6", "speed_max", "10",
    "draw_mode", "STRETCHED", "size", "0.03", "blend", "ALPHA", "preprocess", "3",
    "color_ramp", "\"0 0.95 0.8 0.6 0, 0.2 0.95 0.8 0.6 0.35, 1 0.95 0.8 0.6 0\"", NULL };
  static const char *const fireflies_kv[] = { "amount", "220", "lifetime", "6", "emission_shape", "BOX",
    "emission_extents", "vec3(28, 4, 10)", "spread", "180", "speed_min", "0.1", "speed_max", "0.5", "turbulence", "1.2",
    "turbulence_scale", "0.4", "size", "0.07", "blend", "ADD", "energy", "4", "preprocess", "6",
    "color_ramp", "\"0 0.7 1 0.3 0, 0.5 0.8 1 0.3 1, 1 0.7 1 0.3 0\"", NULL };
  static const char *const bats_kv[] = { "amount", "40", "lifetime", "7", "emission_shape", "BOX",
    "emission_extents", "vec3(28, 4, 12)", "direction", "vec3(1, 0.2, 0)", "spread", "60", "speed_min", "2", "speed_max", "4",
    "turbulence", "2.5", "turbulence_scale", "0.3", "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.22",
    "spin", "9", "blend", "OPAQUE", "preprocess", "7", "color_ramp", "\"0 0.05 0.04 0.06 1, 1 0.05 0.04 0.06 1\"", NULL };
  static const char *const dust_hang_kv[] = { "amount", "600", "lifetime", "12", "emission_shape", "BOX",
    "emission_extents", "vec3(30, 3, 10)", "spread", "180", "speed_min", "0.02", "speed_max", "0.12", "size", "0.05",
    "size_random", "0.6", "blend", "ALPHA", "preprocess", "12",
    "color_ramp", "\"0 0.8 0.8 0.82 0, 0.3 0.8 0.8 0.82 0.5, 1 0.8 0.8 0.82 0\"", NULL };
  static const char *const gulls_kv[] = { "amount", "24", "lifetime", "10", "emission_shape", "BOX",
    "emission_extents", "vec3(30, 3, 14)", "direction", "vec3(1, 0.05, 0)", "spread", "20", "speed_min", "2", "speed_max", "3.5",
    "turbulence", "0.6", "turbulence_scale", "0.2", "draw_mode", "MESH", "mesh_primitive", "BOX", "size", "0.3",
    "blend", "OPAQUE", "preprocess", "10", "color_ramp", "\"0 0.97 0.97 0.97 1, 1 0.97 0.97 0.97 1\"", NULL };
  static const char *const devils_kv[] = { "amount", "900", "lifetime", "4", "emission_shape", "RING", "emission_radius", "1.2",
    "emission_inner_radius", "0.6", "direction", "vec3(0, 1, 0)", "spread", "20", "speed_min", "1.5", "speed_max", "3",
    "turbulence", "3", "turbulence_scale", "0.8", "turbulence_speed", "2", "size", "0.18", "size_random", "0.6", "blend", "ALPHA",
    "preprocess", "4", "color_ramp", "\"0 0.75 0.45 0.3 0, 0.2 0.75 0.45 0.3 0.4, 1 0.8 0.5 0.35 0\"", NULL };
  const char *const *wx = !strcmp(th->name, "scary") ? bats_kv : !strcmp(th->name, "xmas") ? snow_kv
                         : !strcmp(th->name, "moon") ? dust_hang_kv : !strcmp(th->name, "beach") ? gulls_kv
                         : !strcmp(th->name, "mars") ? devils_kv
                         : !strcmp(th->name, "alpine") ? snow_kv : !strcmp(th->name, "coast") ? rain_kv
                         : !strcmp(th->name, "autumn") ? leaves_kv : !strcmp(th->name, "cosmic") ? stars_kv
                         : !strcmp(th->name, "desert") ? sand_kv : !strcmp(th->name, "jungle") || !strcmp(th->name, "neon") ? fireflies_kv
                         : motes_kv;
  motes = particles(parent, "Weather", wx);
  wx_dy = wx == snow_kv ? 8 : wx == rain_kv ? 11 : wx == stars_kv ? 10 : wx == leaves_kv ? 5 : wx == gulls_kv || wx == bats_kv ? 7 : 3;
  wx_dz = wx == stars_kv ? -45 : wx == devils_kv ? -9 : wx == gulls_kv ? -8 : -2;

  cw_node_set(motes, "seed", cw_val_int(track_index * 7 + 3));
}

/* one fixed tick: wind, and the motes riding along with the camera's area */
void view_tick(float rider_x, float rider_y, float speed, bool grounded) {
  wind_t++; /* the grass is posed for the frame, the tufts in view only (view_grass) */
  if (n_tufts) { /* wind.hash: every 64th tuft's pose (the wind's math on this host; which tufts are in view is not hashed) */
    float m[16];
    uint32_t h = (uint32_t)wind_t;
    for (int i = 0; i < n_tufts; i += 64) {
      sway_matrix(&tuft_at[i], wind_t / 60.0f, m);
      h = cw_hash32(m, sizeof m, h);
    }
    wind_hash = h;
  }
  if (cw_node_valid(motes)) cw_node3d_set_position(motes, (t3_vec3){ rider_x + 12, rider_y + wx_dy, wx_dz });
  if (cw_node_valid(dust)) {
    bool on = grounded && speed > 2.5f;
    cw_node_set(dust, "emitting", cw_val_bool(on));
    cw_node_set(dust, "speed_max", cw_val_float(1 + speed * 0.2f));
  }
}

/* the dust follows the rear tyre's contact patch */
void fx_dust(float x, float y, float amount) {
  (void)amount;
  if (cw_node_valid(dust)) cw_node3d_set_position(dust, (t3_vec3){ x, y + 0.08f, 0.2f });
}

void fx_burst(float x, float y, int kind) {
  (void)kind;
  if (!cw_node_valid(burst_n)) return;
  cw_node3d_set_position(burst_n, (t3_vec3){ x, y + 0.2f, 0.3f });
  cw_particles_restart(burst_n);
}

/* the share of the tufts drawn (cw_quality_density) */
void view_density(float k) { density = k < 0.05f ? 0.05f : k > 1 ? 1 : k; }

/* the grass for this frame, posed and culled against the camera's view (the
 * camera node already placed for the frame) */
void view_grass(const cw_node *cameras, int n) {
  t3_frustum f[MAX_RIDERS];
  int nf = 0;
  for (int i = 0; i < n && nf < MAX_RIDERS; i++) {
    t3_camera *cam = cw_camera3d_t3(cameras[i]);
    /* the projection is the renderer's from the last frame (fov, aspect, near and
     * far do not change in play); before the first render there is none: all */
    if (!cam || cam->projection_matrix.e[11] != -1.0f) {
      sway_shown(NULL, 0);
      return;
    }
    t3_mat4 view, vp;
    t3_mat4_invert(&view, cw_node3d_global_transform(cameras[i]));
    t3_mat4_multiply(&vp, &cam->projection_matrix, &view);
    t3_frustum_from_matrix(&f[nf++], &vp);
  }
  sway_shown(f, nf);
}

void view_forget(void) {
  far_mats_free();
  n_flags = 0;
  n_pk = 0; /* the pumpkins' faces and lights went with the scene */
  n_plank_mesh = 0;
  n_mover_obj = 0;
  memset(chain_mesh, 0, sizeof chain_mesh);
  n_tch = 0;
  n_tufts = 0;
  dust = burst_n = motes = (cw_node){ 0 };
}
