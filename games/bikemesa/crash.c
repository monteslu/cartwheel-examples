/* rider's crash world: a Box3D world the wipeouts tumble through in 3D.
 *
 * Riding is a 2D Box2D simulation in the track's plane (bike.c). When the
 * rider comes off, the rider, the bike and the cap leave that plane: bike.c
 * rebuilds them here as 3D bodies, from where they were drawn, and they fall,
 * roll and bounce against the scenery a player sees. That scenery is built
 * once per track (after view_build, which hands over the bank and the rocks),
 * as static shapes made from the same track data view.c draws:
 *
 *   ribbon    the riding surface across the four lanes, its rounded front
 *             lip, the cliff face below it and the cliff walls at each gap
 *   ramps     the partial ramps' decks and sides
 *   bank      the hillside rising behind the track (view_bank_y)
 *   rocks     every boulder and chip on the bank, each as a convex hull
 *   ground    the plains below, behind and in front
 *   decks     the high routes' plank decks and their posts, the loops'
 *             bands and masts, the barriers where a lane ends, the seesaw's
 *             fulcrum
 *
 * The bridge planks and the moving set pieces are bike.c's 2D bodies: it
 * mirrors them here as kinematic bodies while a crash is on.
 *
 * Fixed step, Box3D's deterministic solver on every job-pool thread: the
 * same tumble on every host at any worker count (bike_hash). */
#include <stdlib.h>
#include <string.h>

#include "game.h"

static cw_world3d W3;
static cw_mesh3d scene_mesh;

/* a growable triangle list */
typedef struct tri_list {
  t3_vec3 *v;
  int32_t *i;
  int nv, ni, cv, ci;
} tri_list;
static int tl_v(tri_list *b, float x, float y, float z) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 4096;
    b->v = realloc(b->v, sizeof *b->v * (size_t)b->cv);
  }
  b->v[b->nv] = (t3_vec3){ x, y, z };
  return b->nv++;
}
static void tl_t(tri_list *b, int a, int c, int d) {
  if (b->ni + 3 > b->ci) {
    b->ci = b->ci ? b->ci * 2 : 8192;
    b->i = realloc(b->i, sizeof *b->i * (size_t)b->ci);
  }
  b->i[b->ni++] = a, b->i[b->ni++] = c, b->i[b->ni++] = d;
}
static void tl_quad(tri_list *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3) {
  int a = tl_v(b, p0.x, p0.y, p0.z), c = tl_v(b, p1.x, p1.y, p1.z), d = tl_v(b, p2.x, p2.y, p2.z), e = tl_v(b, p3.x, p3.y, p3.z);
  tl_t(b, a, c, d);
  tl_t(b, a, d, e);
}

/* the ribbon as view.c's ribbon() draws it: per track sample the back edge,
 * the surface, the lip bending down toward the camera, and the face to the
 * bedrock; a wall at each end of a piece */
static void ribbon(tri_list *b, const track_desc *t) {
  const float W = TRACK_HALF_W, LIP = 0.18f, bottom = t->min_y - 60;
  for (int i = 0; i < t->piece_count; i++) {
    const track_piece *pc = &t->pieces[i];
    const float *p = pc->pts;
    int base = b->nv;
    for (int k = 0; k < pc->count; k++) {
      float x = p[k * 2], y = p[k * 2 + 1];
      tl_v(b, x, y - 0.05f, -W - 0.3f);
      tl_v(b, x, y, -W);
      tl_v(b, x, y, W - LIP);
      tl_v(b, x, y - LIP * 0.4f, W - LIP * 0.2f);
      tl_v(b, x, y - LIP, W);
      tl_v(b, x, bottom, W + 1.5f);
      if (k > 0)
        for (int r = 0; r < 5; r++) {
          int a0 = base + (k - 1) * 6 + r, b0 = base + k * 6 + r;
          tl_t(b, a0, a0 + 1, b0 + 1);
          tl_t(b, a0, b0 + 1, b0);
        }
    }
    for (int e = 0; e < 2; e++) {
      int k = e ? pc->count - 1 : 0;
      float x = p[k * 2], y = p[k * 2 + 1];
      tl_quad(b, (t3_vec3){ x, y, -W - 0.3f }, (t3_vec3){ x, y - LIP, W }, (t3_vec3){ x, bottom, W + 1.5f }, (t3_vec3){ x, bottom, -W - 0.3f });
    }
  }
}

/* the partial ramps (view.c ramps_build): the deck over lanes lo..hi and its two sides */
static void ramps(tri_list *b, const track_desc *t) {
  for (int i = 0; i < t->ramp_count; i++) {
    const float *r = &t->ramps[i * 7];
    int lo = (int)r[5], hi = (int)r[6];
    float len = r[1] + r[3] + r[4], za = lane_z(lo - 0.5f) + 0.04f, zb = lane_z(hi + 0.5f) - 0.04f;
    int n = (int)(len / 0.2f) + 1;
    float px = 0, pg = 0, py = 0;
    for (int k = 0; k <= n; k++) {
      float x = r[0] + len * k / n, g = track_height_at(x, NULL), y = g + track_ramp_h(t, x, lo);
      if (k > 0) {
        tl_quad(b, (t3_vec3){ px, py, za }, (t3_vec3){ px, py, zb }, (t3_vec3){ x, y, zb }, (t3_vec3){ x, y, za });
        for (int sd = 0; sd < 2; sd++) {
          float z = sd ? zb : za;
          tl_quad(b, (t3_vec3){ px, pg - 0.05f, z }, (t3_vec3){ px, py, z }, (t3_vec3){ x, y, z }, (t3_vec3){ x, g - 0.05f, z });
        }
      }
      px = x, pg = g, py = y;
    }
  }
}

/* the bank (view.c bank()): rows back from the track's back edge, a metre apart along it */
static void bank(tri_list *b, const track_desc *t) {
  static const float ZS[] = { 0, 0.4f, 1, 2, 3.5f, 5.5f, 8, 11, 15, 20, 27, 36 };
  enum { NZ = (int)CW_ARRAY_LEN(ZS) };
  float x0 = -12, x1 = t->length + 24;
  int nx = (int)(x1 - x0) + 1, base = b->nv;
  for (int i = 0; i < nx; i++) {
    float x = x0 + i;
    for (int j = 0; j < NZ; j++) {
      float z = -TRACK_HALF_W - 0.3f - ZS[j];
      tl_v(b, x, view_bank_y(x, z) - (j == 0 ? 0.08f : 0), z);
      if (i > 0 && j > 0) {
        int a = base + (i - 1) * NZ + j - 1, c = base + i * NZ + j - 1;
        tl_t(b, a, c + 1, a + 1);
        tl_t(b, a, c, c + 1);
      }
    }
  }
}

/* the loops (view.c loops_build): the band's inside, outside and edges */
static float loop_shift(float t) { return cw_smoothstep(0.25f, 0.75f, t); }
static void loops(tri_list *b, const track_desc *t) {
  for (int i = 0; i < t->loop_count; i++) {
    float cx = t->loops[i * 3], y0 = t->loops[i * 3 + 1], r = t->loops[i * 3 + 2], cy = y0 + r;
    const float W = LANE_W + 0.12f, TH = 0.22f, ZIN = lane_z(0.5f), ZSH = 2 * LANE_W;
    enum { N = 96 };
    for (int k = 0; k < N; k++) {
      float t0 = (float)k / N, t1 = (float)(k + 1) / N;
      float a0 = -1.5707963f + 6.2831853f * t0, a1 = -1.5707963f + 6.2831853f * t1;
      float c0 = cw_cos(a0), s0 = cw_sin(a0), c1 = cw_cos(a1), s1 = cw_sin(a1);
      float z0 = ZIN + ZSH * loop_shift(t0), z1 = ZIN + ZSH * loop_shift(t1), R2 = r + TH;
      t3_vec3 i0a = { cx + r * c0, cy + r * s0, z0 - W }, i0b = { cx + r * c0, cy + r * s0, z0 + W };
      t3_vec3 i1a = { cx + r * c1, cy + r * s1, z1 - W }, i1b = { cx + r * c1, cy + r * s1, z1 + W };
      t3_vec3 o0a = { cx + R2 * c0, cy + R2 * s0, z0 - W }, o0b = { cx + R2 * c0, cy + R2 * s0, z0 + W };
      t3_vec3 o1a = { cx + R2 * c1, cy + R2 * s1, z1 - W }, o1b = { cx + R2 * c1, cy + R2 * s1, z1 + W };
      tl_quad(b, i0a, i1a, i1b, i0b);
      tl_quad(b, o0a, o1a, o1b, o0b);
      tl_quad(b, i0b, i1b, o1b, o0b);
      tl_quad(b, i0a, i1a, o1a, o0a);
    }
  }
}

static cw_xform3 at_rot_z(float x, float y, float z, float angle) {
  float s, c;
  cw_sincos(angle * 0.5f, &s, &c);
  return (cw_xform3){ { x, y, z }, { 0, 0, s, c } };
}

void crash_world_build(const track_desc *t) {
  crash_world_destroy();
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = (t3_vec3){ 0, -t->gravity, 0 };
  wd.substeps = 4;
  wd.workers = 0; /* every job-pool thread: Box3D gives the same result at any worker count */
  wd.hit_speed = 2.5f;
  wd.layers[L3_SCENE] = "scene";
  wd.layers[L3_RIDER] = "rider";
  wd.layers[L3_BIKE] = "bike";
  wd.layers[L3_HAND] = "hand";
  W3 = cw_world3d_create(&wd);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.name = "scenery";
  cw_body3d ground = cw_body3d_create(W3, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = L3_SCENE;
  sd.material.friction = 0.85f * t->grip;
  sd.material.restitution = 0.1f;
  sd.contact_events = false;

  tri_list b = { 0 };
  ribbon(&b, t);
  ramps(&b, t);
  bank(&b, t);
  loops(&b, t);
  scene_mesh = cw_mesh3d_create(b.v, b.nv, b.i, b.ni / 3);
  free(b.v);
  free(b.i);
  cw_shape3d_mesh(ground, &sd, scene_mesh, (t3_vec3){ 1, 1, 1 });

  /* the plains below: behind the track at the bedrock, and the low ground in front */
  float mid = t->length * 0.5f, half = t->length * 0.5f + 450;
  cw_shape3d_box(ground, &sd, (t3_vec3){ half, 0.5f, 150 }, cw_xform3_at(mid, t->min_y - 8.5f, -150));
  cw_shape3d_box(ground, &sd, (t3_vec3){ half, 0.5f, 40 }, cw_xform3_at(mid, t->min_y - 20.5f, 40));

  /* the rocks on the bank */
  const view_rock *rk;
  int nr = view_rocks(&rk);
  sd.material.friction = 0.7f;
  for (int i = 0; i < nr; i++) cw_shape3d_hull(ground, &sd, rk[i].p, VIEW_ROCK_PTS);
  sd.material.friction = 0.85f * t->grip;

  /* the high routes' decks and their posts (view.c highs_build) */
  for (int i = 0; i < t->high_count; i++) {
    float x0 = t->highs[i * 4], x1 = t->highs[i * 4 + 1], y0 = t->highs[i * 4 + 2], y1 = t->highs[i * 4 + 3];
    float dx = x1 - x0, dy = y1 - y0;
    cw_shape3d_box(ground, &sd, (t3_vec3){ sqrtf(dx * dx + dy * dy) * 0.5f, 0.08f, TRACK_HALF_W * 0.7f },
                   at_rot_z((x0 + x1) * 0.5f, (y0 + y1) * 0.5f - 0.08f, -0.3f, cw_atan2(dy, dx)));
    for (float x = x0 + 1; x < x1; x += 4) {
      float y = y0 + dy * (x - x0) / dx, g = track_height_at(x, NULL), h = y - 0.16f - g;
      if (h < 0.3f) continue;
      for (int s2 = -1; s2 <= 1; s2 += 2)
        cw_shape3d_box(ground, &sd, (t3_vec3){ 0.08f, h * 0.5f, 0.08f }, cw_xform3_at(x, g + h * 0.5f, -0.3f + s2 * TRACK_HALF_W * 0.6f));
    }
  }
  /* the loops' masts and beams behind the track (view.c loops_build) */
  for (int i = 0; i < t->loop_count; i++) {
    float cx = t->loops[i * 3], r = t->loops[i * 3 + 2], cy = t->loops[i * 3 + 1] + r;
    const float W = LANE_W + 0.12f, TH = 0.22f, ZIN = lane_z(0.5f), ZSH = 2 * LANE_W;
    static const float AT[] = { 0.36f, 0.64f };
    for (int k = 0; k < 2; k++) {
      float tt = AT[k], a = -1.5707963f + 6.2831853f * tt;
      float px = cx + (r + TH) * cw_cos(a), py = cy + (r + TH) * cw_sin(a), g = track_height_at(px, NULL);
      if (py - g < 2.6f && tt > 0.1f && tt < 0.9f) continue;
      float zb = ZIN + ZSH * loop_shift(tt) - W, zp = -TRACK_HALF_W - 0.45f;
      cw_shape3d_box(ground, &sd, (t3_vec3){ 0.13f, (py - g + 0.1f) * 0.5f, 0.13f }, cw_xform3_at(px, g + (py - g) * 0.5f - 0.05f, zp));
      if (zb - zp > 0.3f) cw_shape3d_box(ground, &sd, (t3_vec3){ 0.08f, 0.08f, (zb - zp) * 0.5f }, cw_xform3_at(px, py, (zp + zb) * 0.5f));
    }
  }
  /* the striped barriers where a lane ends (view.c lane_stripes) */
  narrow nw[MAX_NARROWS];
  int nn = track_narrows(t, nw);
  for (int i = 0; i < nn; i++) {
    if (!nw[i].wall) continue;
    float y = track_height_at(nw[i].x0, NULL);
    float spans[2][2] = { { lane_z(-0.5f), lane_z(nw[i].lo - 0.5f) }, { lane_z(nw[i].hi + 0.5f), lane_z(LANES - 0.5f) } };
    for (int sp = 0; sp < 2; sp++) {
      float za = spans[sp][0] + 0.02f, zz = spans[sp][1] - 0.02f;
      if (zz - za < 0.3f) continue;
      cw_shape3d_box(ground, &sd, (t3_vec3){ 0.175f, 0.5f, (zz - za) * 0.5f }, cw_xform3_at(nw[i].x0 + 0.2f, y + 0.5f, (za + zz) * 0.5f));
    }
  }
  /* the seesaws' fulcrums (view.c movers_build: a three-sided cone, stretched across) */
  for (int i = 0; i < t->mover_count; i++)
    if ((int)t->movers[i * 4] == MOVER_SEESAW) {
      float x = t->movers[i * 4 + 1], y = t->movers[i * 4 + 2];
      cw_shape3d_box(ground, &sd, (t3_vec3){ 0.2f, 0.2f, 1.5f }, cw_xform3_at(x, y - 0.02f, 0));
    }
}

void crash_world_destroy(void) {
  if (cw_world3d_valid(W3)) cw_world3d_destroy(W3);
  W3 = (cw_world3d){ 0 };
  cw_mesh3d_destroy(scene_mesh);
  scene_mesh = (cw_mesh3d){ 0 };
}

cw_world3d crash_world(void) { return W3; }
