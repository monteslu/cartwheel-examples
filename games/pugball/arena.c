/* pugball's arena as Box3D static geometry: one welded triangle mesh.
 *
 *   bowl    the plan (a rectangle AW x AL with AC-rounded corners, seen from
 *           above) swept with a profile: a quarter circle of radius AR from
 *           the floor up to the wall, the wall, a quarter circle to the
 *           ceiling; the floor and the ceiling close it
 *   goals   a box GW x GH x GD behind each end, cut into the end wall and its
 *           ramp, so the mouth's floor is flat and level with the pitch
 *
 * Every vertex is shared by the triangles that meet at it (welded by exact
 * position) and Box3D gets the mesh's edge adjacency, so a ball rolling from
 * the floor up a ramp or a pug driving from the wall onto the ceiling never
 * catches an inner edge. The mesh is built once, from + - * / and
 * cw_sincos only, so every host gets the same triangles; every face looks
 * into the arena. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/physics3d.h"
#include "game.h"

#define STEP 1.0f   /* plan samples along the straight sides */
#define CORNER_N 16 /* segments per rounded corner of the plan */
#define ARC_N 12    /* segments per ramp quarter circle */

typedef struct mb {
  t3_vec3 *v;
  int32_t *idx;
  int nv, nt, cap_v, cap_t;
  int32_t *hash; /* open addressing over v, exact positions */
  int hcap;
} mb;

static uint32_t vhash(t3_vec3 p) {
  uint32_t a, b, c;
  memcpy(&a, &p.x, 4), memcpy(&b, &p.y, 4), memcpy(&c, &p.z, 4);
  return (a * 73856093u) ^ (b * 19349663u) ^ (c * 83492791u);
}

static int vert(mb *m, t3_vec3 p) {
  if (p.x == 0) p.x = 0; /* -0 and 0 are one vertex */
  if (p.y == 0) p.y = 0;
  if (p.z == 0) p.z = 0;
  uint32_t h = vhash(p) & (uint32_t)(m->hcap - 1);
  while (m->hash[h] >= 0) {
    t3_vec3 q = m->v[m->hash[h]];
    if (q.x == p.x && q.y == p.y && q.z == p.z) return m->hash[h];
    h = (h + 1) & (uint32_t)(m->hcap - 1);
  }
  if (m->nv == m->cap_v) {
    m->cap_v = m->cap_v ? m->cap_v * 2 : 4096;
    m->v = realloc(m->v, sizeof *m->v * (size_t)m->cap_v);
  }
  m->v[m->nv] = p;
  m->hash[h] = m->nv;
  return m->nv++;
}

/* a triangle facing along `in` (into the arena) */
static void tri(mb *m, t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 in) {
  int ia = vert(m, a), ib = vert(m, b), ic = vert(m, c);
  if (ia == ib || ib == ic || ia == ic) return;
  t3_vec3 n = v3cross(v3sub(b, a), v3sub(c, a));
  if (v3dot(n, in) < 0) {
    int t = ib;
    ib = ic, ic = t;
  }
  if (m->nt == m->cap_t) {
    m->cap_t = m->cap_t ? m->cap_t * 2 : 8192;
    m->idx = realloc(m->idx, sizeof *m->idx * 3 * (size_t)m->cap_t);
  }
  m->idx[m->nt * 3] = ia, m->idx[m->nt * 3 + 1] = ib, m->idx[m->nt * 3 + 2] = ic;
  m->nt++;
}
static void quad(mb *m, t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, t3_vec3 in) { /* a b c d round the edge */
  tri(m, a, b, c, in);
  tri(m, a, c, d, in);
}

/* ── the plan and the profile ───────────────────────────────────────── */

typedef struct pl { float x, z, nx, nz; } pl; /* outward normal */
static pl PL[512];
static int npl;

static void plan(void) {
  const float hx = AW - AC, hz = AL - AC;
  npl = 0;
  for (float z = -hz; z < hz - 1e-3f; z += STEP) PL[npl++] = (pl){ AW, z, 1, 0 };
  for (int q = 0; q < 4; q++) {
    /* corner q at (sx hx, sz hz), then the next side */
    static const float SX[4] = { 1, -1, -1, 1 }, SZ[4] = { 1, 1, -1, -1 };
    for (int k = 0; k < CORNER_N; k++) {
      float s, c;
      cw_sincos(1.5707963f * (float)(q * CORNER_N + k) / CORNER_N, &s, &c);
      PL[npl++] = (pl){ SX[q] * hx + AC * c, SZ[q] * hz + AC * s, c, s };
    }
    if (q == 0)
      for (float x = hx; x > -hx + 1e-3f; x -= STEP) PL[npl++] = (pl){ x, AL, 0, 1 };
    else if (q == 1)
      for (float z = hz; z > -hz + 1e-3f; z -= STEP) PL[npl++] = (pl){ -AW, z, -1, 0 };
    else if (q == 2)
      for (float x = -hx; x < hx - 1e-3f; x += STEP) PL[npl++] = (pl){ x, -AL, 0, -1 };
  }
}

/* the profile, inward offset d and height y, floor tangent to ceiling
 * tangent; hd/hy: the inward normal's direction for the segment that starts
 * at the sample (toward the arc's centre, or off the wall) */
typedef struct pr { float d, y; } pr;
static pr PR[2 * ARC_N + 8];
static int npr;

static void profile(void) {
  npr = 0;
  for (int k = 0; k <= ARC_N; k++) { /* floor -> wall, centre (AR, AR) */
    float s, c;
    cw_sincos(1.5707963f * (float)k / ARC_N, &s, &c);
    PR[npr++] = (pr){ AR - AR * s, AR - AR * c };
  }
  PR[npr - 1] = (pr){ 0, AR };
  PR[npr++] = (pr){ 0, GH }; /* the crossbar's height: the goal's hole stops here */
  for (int k = 0; k <= ARC_N; k++) { /* wall -> ceiling, centre (AR, AH - AR) */
    float s, c;
    cw_sincos(1.5707963f * (float)k / ARC_N, &s, &c);
    PR[npr++] = (pr){ AR - AR * c, AH - AR + AR * s };
  }
  PR[npr - ARC_N - 1] = (pr){ 0, AH - AR };
  PR[npr - 1] = (pr){ AR, AH };
}

static t3_vec3 at(const pl *p, pr r) { return (t3_vec3){ p->x - p->nx * r.d, r.y, p->z - p->nz * r.d }; }
/* the inner rectangle the floor and ceiling bands run to (the corners' centres) */
static t3_vec3 inner(const pl *p, float y) {
  const float hx = AW - AC, hz = AL - AC;
  float x = p->x < -hx ? -hx : p->x > hx ? hx : p->x;
  float z = p->z < -hz ? -hz : p->z > hz ? hz : p->z;
  return (t3_vec3){ x, y, z };
}

/* ── the goal's side wall: the polygon under the ramp, ear-clipped ───── */

static float cross2(float ax, float ay, float bx, float by, float cx, float cy) {
  return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}
/* polygon in (u = |z|, y), counter-clockwise; x = the wall's plane */
static void side_wall(mb *m, const float *u, const float *y, int n, float x, float zs) {
  int ix[64];
  for (int i = 0; i < n; i++) ix[i] = i;
  t3_vec3 in = { x > 0 ? -1.0f : 1.0f, 0, 0 };
  int guard = 0;
  while (n > 3 && guard++ < 4096) {
    bool cut = false;
    for (int i = 0; i < n && !cut; i++) {
      int a = ix[(i + n - 1) % n], b = ix[i], c = ix[(i + 1) % n];
      if (cross2(u[a], y[a], u[b], y[b], u[c], y[c]) <= 1e-6f) continue; /* reflex or flat */
      bool ear = true;
      for (int k = 0; k < n && ear; k++) {
        int p = ix[k];
        if (p == a || p == b || p == c) continue;
        if (cross2(u[a], y[a], u[b], y[b], u[p], y[p]) >= 0 && cross2(u[b], y[b], u[c], y[c], u[p], y[p]) >= 0 &&
            cross2(u[c], y[c], u[a], y[a], u[p], y[p]) >= 0)
          ear = false;
      }
      if (!ear) continue;
      tri(m, (t3_vec3){ x, y[a], zs * u[a] }, (t3_vec3){ x, y[b], zs * u[b] }, (t3_vec3){ x, y[c], zs * u[c] }, in);
      for (int k = i; k + 1 < n; k++) ix[k] = ix[k + 1];
      n--;
      cut = true;
    }
    if (!cut) break;
  }
  if (n == 3)
    tri(m, (t3_vec3){ x, y[ix[0]], zs * u[ix[0]] }, (t3_vec3){ x, y[ix[1]], zs * u[ix[1]] },
        (t3_vec3){ x, y[ix[2]], zs * u[ix[2]] }, in);
}

static void goal(mb *m, float zs) {
  const float z0 = AL - AR, z1 = AL + GD;
  /* floor, ceiling, back */
  for (float x = -GW; x < GW - 1e-3f; x += 1)
    for (float u = z0; u < z1 - 1e-3f; u += 1)
      quad(m, (t3_vec3){ x, 0, zs * u }, (t3_vec3){ x + 1, 0, zs * u }, (t3_vec3){ x + 1, 0, zs * (u + 1) },
           (t3_vec3){ x, 0, zs * (u + 1) }, (t3_vec3){ 0, 1, 0 });
  for (float x = -GW; x < GW - 1e-3f; x += 1)
    for (float u = AL; u < z1 - 1e-3f; u += 1)
      quad(m, (t3_vec3){ x, GH, zs * u }, (t3_vec3){ x + 1, GH, zs * u }, (t3_vec3){ x + 1, GH, zs * (u + 1) },
           (t3_vec3){ x, GH, zs * (u + 1) }, (t3_vec3){ 0, -1, 0 });
  for (float x = -GW; x < GW - 1e-3f; x += 1)
    for (float y = 0; y < GH - 1e-3f; y += 1)
      quad(m, (t3_vec3){ x, y, zs * z1 }, (t3_vec3){ x + 1, y, zs * z1 }, (t3_vec3){ x + 1, y + 1, zs * z1 },
           (t3_vec3){ x, y + 1, zs * z1 }, (t3_vec3){ 0, 0, -zs });
  /* the side walls: along the floor to the back, up the back, along the
   * ceiling to the end wall, down the end wall and the ramp's own curve */
  float U[64], Y[64];
  int n = 0;
  for (float u = z0; u < z1 - 1e-3f; u += 1) U[n] = u, Y[n++] = 0;
  for (float y = 0; y < GH - 1e-3f; y += 1) U[n] = z1, Y[n++] = y;
  for (float u = z1; u > AL + 1e-3f; u -= 1) U[n] = u, Y[n++] = GH;
  U[n] = AL, Y[n++] = GH;
  for (int j = ARC_N; j > 0; j--) U[n] = AL - PR[j].d, Y[n++] = PR[j].y;
  for (int sx = -1; sx <= 1; sx += 2) {
    /* counter-clockwise seen from inside the goal; the clipper works in (u, y)
     * and tri() turns each face toward the goal's middle */
    side_wall(m, U, Y, n, (float)sx * GW, zs);
  }
}

static cw_mesh3d MESH;

static void build(void) {
  plan();
  profile();
  mb m = { 0 };
  m.hcap = 1 << 16;
  m.hash = malloc(sizeof *m.hash * (size_t)m.hcap);
  for (int i = 0; i < m.hcap; i++) m.hash[i] = -1;
  /* the swept bowl, with the goal mouths cut out */
  for (int i = 0; i < npl; i++) {
    const pl *a = &PL[i], *b = &PL[(i + 1) % npl];
    float cx = (a->x + b->x) * 0.5f, cz = (a->z + b->z) * 0.5f;
    for (int j = 0; j + 1 < npr; j++) {
      float my = (PR[j].y + PR[j + 1].y) * 0.5f, md = (PR[j].d + PR[j + 1].d) * 0.5f;
      if (fabsf(cz) > AL - AR - 0.6f && fabsf(cx) < GW && my < GH) continue;
      /* into the arena: toward the arc's centre, or straight off the wall */
      float hd = 1, hy = 0;
      if (my < AR) hd = AR - md, hy = AR - my;
      else if (my > AH - AR) hd = AR - md, hy = AH - AR - my;
      float nx = (a->nx + b->nx) * 0.5f, nz = (a->nz + b->nz) * 0.5f;
      quad(&m, at(a, PR[j]), at(b, PR[j]), at(b, PR[j + 1]), at(a, PR[j + 1]), (t3_vec3){ -nx * hd, hy, -nz * hd });
    }
  }
  /* floor and ceiling: a band from the plan's inset edge to the inner
   * rectangle, and a grid over that */
  for (int c = 0; c < 2; c++) {
    float y = c ? AH : 0;
    pr edge = c ? PR[npr - 1] : PR[0];
    t3_vec3 in = { 0, c ? -1.0f : 1.0f, 0 };
    for (int i = 0; i < npl; i++) {
      const pl *a = &PL[i], *b = &PL[(i + 1) % npl];
      t3_vec3 ia = inner(a, y), ib = inner(b, y);
      if (ia.x == ib.x && ia.z == ib.z) tri(&m, at(a, edge), at(b, edge), ia, in);
      else quad(&m, at(a, edge), at(b, edge), ib, ia, in);
    }
    const float hx = AW - AC, hz = AL - AC;
    for (float x = -hx; x < hx - 1e-3f; x += STEP)
      for (float z = -hz; z < hz - 1e-3f; z += STEP)
        quad(&m, (t3_vec3){ x, y, z }, (t3_vec3){ x + STEP, y, z }, (t3_vec3){ x + STEP, y, z + STEP }, (t3_vec3){ x, y, z + STEP }, in);
  }
  goal(&m, 1);
  goal(&m, -1);
  MESH = cw_mesh3d_create(m.v, m.nv, m.idx, m.nt);
  cw_info("pugball: arena mesh %d vertices, %d triangles", m.nv, m.nt);
  free(m.v);
  free(m.idx);
  free(m.hash);
}

cw_body3d arena_create(cw_world3d w, const cw_shape3d_def *sd) {
  if (!MESH.id) build();
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.name = "arena";
  cw_body3d b = cw_body3d_create(w, &bd);
  cw_shape3d_mesh(b, sd, MESH, (t3_vec3){ 1, 1, 1 });
  return b;
}
