/* racer's props: the mesh builder every generator uses, instanced batches chunked for culling, prop sets, the
 * scatter helpers and the road clearance index. The generators themselves are in propgen_nature.c and
 * propgen_man.c, the dressing in dressing.c, the ground and water in ground.c.
 *
 * Why vertex colours: a material has one roughness, so a model with a dozen painted parts would be a dozen draws.
 * Here every part carries its own colour and a model is at most four draws (PL_MAIN, PL_METAL, PL_LEAF, PL_GLOW),
 * instanced across every copy in a view-sized chunk. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"

#include "cartwheel/base.h"
#include "cartwheel/math.h"
#include "props.h"
#include "props_gen.h"

/* ── colours and hashing ─────────────────────────────────────────────────────────────────────────── */

pcol rgb(float r, float g, float b) {
  t3_color c = lin(r, g, b);
  return (pcol){ c.r, c.g, c.b };
}
pcol hex(uint32_t v) { return rgb(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f); }
pcol pc_mix(pcol a, pcol b, float t) { return (pcol){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
pcol pc_mul(pcol a, float k) { return (pcol){ a.r * k, a.g * k, a.b * k }; }
pcol glow(pcol c, float gain) { return pc_mul(c, gain); }

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static float hashf(uint32_t x) { return (float)(hash32(x) & 0xffffff) / 16777215.0f; }
static float vsign(float x) { return x < 0 ? -1.0f : 1.0f; }

/* smooth value noise in -1..1 */
float pm_noise3(float x, float y, float z, uint32_t seed) {
  float fx = floorf(x), fy = floorf(y), fz = floorf(z);
  int ix = (int)fx, iy = (int)fy, iz = (int)fz;
  float u = x - fx, v = y - fy, w = z - fz;
  u = u * u * (3 - 2 * u), v = v * v * (3 - 2 * v), w = w * w * (3 - 2 * w);
  float c[8];
  for (int k = 0; k < 8; k++) {
    uint32_t h = (seed * 747796405u + (uint32_t)(ix + (k & 1)) * 73856093u) ^ ((uint32_t)(iy + ((k >> 1) & 1)) * 19349663u) ^
                 ((uint32_t)(iz + ((k >> 2) & 1)) * 83492791u);
    c[k] = hashf(h) * 2 - 1;
  }
  float a = c[0] + (c[1] - c[0]) * u, b = c[2] + (c[3] - c[2]) * u, cc = c[4] + (c[5] - c[4]) * u, d = c[6] + (c[7] - c[6]) * u;
  float e = a + (b - a) * v, f = cc + (d - cc) * v;
  return e + (f - e) * w;
}

/* ── the builder ─────────────────────────────────────────────────────────────────────────────────── */

typedef struct lbuf {
  float *p, *n, *c;
  int nv, cap;
  uint32_t *ix;
  int ni, capi;
} lbuf;

#define PM_STACK 20
struct pm {
  lbuf L[PL_COUNT];
  int layer;
  t3_mat4 st[PM_STACK];
  float det[PM_STACK];
  int sp;
  uint32_t rng, seed;
  float vary, jit, uvm;
  uint32_t jid;
  bool smooth;
};

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V(a.x * k, a.y * k, a.z * k); }
static float vdot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static t3_vec3 vcross(t3_vec3 a, t3_vec3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float vlen(t3_vec3 a) { return sqrtf(vdot(a, a)); }
static t3_vec3 vnorm(t3_vec3 a) {
  float l = vlen(a);
  return l > 1e-9f ? vmul(a, 1.0f / l) : V(0, 1, 0);
}

pm *pm_new(uint32_t seed) {
  pm *b = calloc(1, sizeof *b);
  b->seed = seed;
  b->rng = hash32(seed * 2654435761u + 977u);
  b->vary = 0.04f;
  b->uvm = 1.0f;
  t3_mat4_identity(&b->st[0]);
  b->det[0] = 1;
  return b;
}
float pm_rand(pm *b) {
  b->rng = b->rng * 1664525u + 1013904223u;
  return (float)(hash32(b->rng) & 0xffffff) / 16777215.0f;
}
float pm_rands(pm *b) { return pm_rand(b) * 2 - 1; }
void pm_layer(pm *b, prop_layer l) { b->layer = l; }
void pm_vary(pm *b, float a) { b->vary = a; }
void pm_smooth(pm *b, bool on) { b->smooth = on; }
void pm_jitter(pm *b, float amount) { b->jit = amount; }
void pm_uv_scale(pm *b, float metres) { b->uvm = metres > 0.05f ? metres : 1.0f; }
/* how many metres one repeat of a texture kind should cover on a prop: big carved blocks want several metres, planks one */
float tex_metres(int tex) {
  switch (tex) {
  case TEX_GROUND_ROCK: return 4.0f;
  case TEX_GROUND_SAND: case TEX_GROUND_SNOW: case TEX_GROUND_ASH: return 6.0f;
  case TEX_WALL_STONE: case TEX_WALL_CONCRETE: case TEX_WALL_BRICK: return 3.0f;
  case TEX_WALL_ICE: return 3.5f;
  case TEX_WALL_RUST: case TEX_METAL: case TEX_RUST: return 2.0f;
  case TEX_WALL_WOOD: return 1.6f;
  default: return 2.5f;
  }
}

static float det3(const t3_mat4 *m) {
  const float *e = m->e;
  return e[0] * (e[5] * e[10] - e[6] * e[9]) - e[4] * (e[1] * e[10] - e[2] * e[9]) + e[8] * (e[1] * e[6] - e[2] * e[5]);
}
static void top_changed(pm *b) { b->det[b->sp] = det3(&b->st[b->sp]); }
void pm_push(pm *b) {
  if (b->sp + 1 >= PM_STACK) return;
  b->st[b->sp + 1] = b->st[b->sp];
  b->det[b->sp + 1] = b->det[b->sp];
  b->sp++;
}
void pm_pop(pm *b) {
  if (b->sp > 0) b->sp--;
}
static void mul_top(pm *b, const t3_mat4 *m) {
  t3_mat4 r;
  t3_mat4_multiply(&r, &b->st[b->sp], m);
  b->st[b->sp] = r;
  top_changed(b);
}
void pm_move(pm *b, float x, float y, float z) {
  t3_mat4 m;
  t3_mat4_compose(&m, V(x, y, z), (t3_quat){ 0, 0, 0, 1 }, V(1, 1, 1));
  mul_top(b, &m);
}
static void rot_axis(pm *b, t3_vec3 ax, float a) {
  t3_mat4 m;
  t3_mat4_compose(&m, V(0, 0, 0), t3_quat_from_axis_angle(ax, a), V(1, 1, 1));
  mul_top(b, &m);
}
void pm_rotx(pm *b, float a) { rot_axis(b, V(1, 0, 0), a); }
void pm_roty(pm *b, float a) { rot_axis(b, V(0, 1, 0), a); }
void pm_rotz(pm *b, float a) { rot_axis(b, V(0, 0, 1), a); }
void pm_scale(pm *b, float x, float y, float z) {
  t3_mat4 m;
  t3_mat4_compose(&m, V(0, 0, 0), (t3_quat){ 0, 0, 0, 1 }, V(x, y, z));
  mul_top(b, &m);
}
void pm_basis(pm *b, t3_vec3 pos, t3_vec3 r, t3_vec3 u, t3_vec3 f) {
  t3_mat4 m = { { r.x, r.y, r.z, 0, u.x, u.y, u.z, 0, f.x, f.y, f.z, 0, pos.x, pos.y, pos.z, 1 } };
  mul_top(b, &m);
}

static int lb_vert(lbuf *l, t3_vec3 p, t3_vec3 n, pcol c) {
  if (l->nv == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 256;
    l->p = realloc(l->p, sizeof(float) * 3 * (size_t)l->cap);
    l->n = realloc(l->n, sizeof(float) * 3 * (size_t)l->cap);
    l->c = realloc(l->c, sizeof(float) * 3 * (size_t)l->cap);
  }
  float *P = l->p + l->nv * 3, *N = l->n + l->nv * 3, *C = l->c + l->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x, N[1] = n.y, N[2] = n.z;
  C[0] = c.r, C[1] = c.g, C[2] = c.b;
  return l->nv++;
}
static void lb_tri(lbuf *l, int a, int c, int d) {
  if (l->ni + 3 > l->capi) {
    l->capi = l->capi ? l->capi * 2 : 768;
    l->ix = realloc(l->ix, sizeof(uint32_t) * (size_t)l->capi);
  }
  l->ix[l->ni++] = (uint32_t)a, l->ix[l->ni++] = (uint32_t)c, l->ix[l->ni++] = (uint32_t)d;
}

/* a triangle with flat shading: positions are local (the matrix places them) */
static void tri_flat(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, pcol c0, pcol c1, pcol c2) {
  const t3_mat4 *m = &b->st[b->sp];
  t3_vec3 a = t3_vec3_apply_mat4(p0, m), c = t3_vec3_apply_mat4(p1, m), d = t3_vec3_apply_mat4(p2, m);
  t3_vec3 n = vcross(vsub(c, a), vsub(d, a));
  float l = vlen(n);
  if (l < 1e-12f) return;
  n = vmul(n, 1.0f / l);
  bool flip = b->det[b->sp] < 0;
  if (flip) n = vmul(n, -1.0f);
  float k = 1.0f + b->vary * (pm_rand(b) * 2 - 1);
  c0 = pc_mul(c0, k), c1 = pc_mul(c1, k), c2 = pc_mul(c2, k);
  lbuf *L = &b->L[b->layer];
  int i0 = lb_vert(L, a, n, c0), i1 = lb_vert(L, c, n, c1), i2 = lb_vert(L, d, n, c2);
  if (flip) lb_tri(L, i0, i2, i1);
  else lb_tri(L, i0, i1, i2);
}
/* a triangle with given (local) normals */
static void tri_smooth(pm *b, t3_vec3 p0, t3_vec3 n0, pcol c0, t3_vec3 p1, t3_vec3 n1, pcol c1, t3_vec3 p2, t3_vec3 n2, pcol c2) {
  const t3_mat4 *m = &b->st[b->sp];
  bool flip = b->det[b->sp] < 0;
  t3_vec3 np[3] = { n0, n1, n2 };
  t3_vec3 pp[3] = { p0, p1, p2 };
  pcol cc[3] = { c0, c1, c2 };
  int id[3];
  lbuf *L = &b->L[b->layer];
  for (int i = 0; i < 3; i++) {
    t3_vec3 q = t3_vec3_apply_mat4(pp[i], m), n = vnorm(t3_vec3_transform_direction(np[i], m));
    id[i] = lb_vert(L, q, flip ? vmul(n, -1.0f) : n, cc[i]);
  }
  if (flip) lb_tri(L, id[0], id[2], id[1]);
  else lb_tri(L, id[0], id[1], id[2]);
}
void pm_tri_n(pm *b, t3_vec3 p0, t3_vec3 n0, pcol c0, t3_vec3 p1, t3_vec3 n1, pcol c1, t3_vec3 p2, t3_vec3 n2, pcol c2) {
  tri_smooth(b, p0, n0, c0, p1, n1, c1, p2, n2, c2);
}
static void quad_flat(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, pcol c0, pcol c1, pcol c2, pcol c3) {
  tri_flat(b, p0, p1, p2, c0, c1, c2);
  tri_flat(b, p0, p2, p3, c0, c2, c3);
}
void pm_tri(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, pcol c) { tri_flat(b, p0, p1, p2, c, c, c); }
void pm_quad(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, pcol c) { quad_flat(b, p0, p1, p2, p3, c, c, c, c); }

/* a box face: centre c, axes u, v (half extents folded in) with u x v the outward normal; colours by height */
static void box_face(pm *b, t3_vec3 c, t3_vec3 u, t3_vec3 v, float y0, float h, pcol c0, pcol c1) {
  t3_vec3 p0 = vsub(vsub(c, u), v), p1 = vsub(vadd(c, u), v), p2 = vadd(vadd(c, u), v), p3 = vadd(vsub(c, u), v);
  float hh = h > 1e-6f ? h : 1;
  pcol k0 = pc_mix(c0, c1, cw_clampf((p0.y - y0) / hh, 0, 1)), k1 = pc_mix(c0, c1, cw_clampf((p1.y - y0) / hh, 0, 1));
  pcol k2 = pc_mix(c0, c1, cw_clampf((p2.y - y0) / hh, 0, 1)), k3 = pc_mix(c0, c1, cw_clampf((p3.y - y0) / hh, 0, 1));
  quad_flat(b, p0, p1, p2, p3, k0, k1, k2, k3);
}
static void box_ex(pm *b, float w, float h, float d, float yc, pcol c0, pcol c1, bool bottom) {
  float hx = w * 0.5f, hy = h * 0.5f, hz = d * 0.5f, y0 = yc - hy;
  box_face(b, V(0, yc, hz), V(hx, 0, 0), V(0, hy, 0), y0, h, c0, c1);
  box_face(b, V(0, yc, -hz), V(-hx, 0, 0), V(0, hy, 0), y0, h, c0, c1);
  box_face(b, V(hx, yc, 0), V(0, 0, -hz), V(0, hy, 0), y0, h, c0, c1);
  box_face(b, V(-hx, yc, 0), V(0, 0, hz), V(0, hy, 0), y0, h, c0, c1);
  box_face(b, V(0, yc + hy, 0), V(0, 0, hz), V(hx, 0, 0), y0, h, c0, c1);
  if (bottom) box_face(b, V(0, yc - hy, 0), V(hx, 0, 0), V(0, 0, hz), y0, h, c0, c1);
}
void pm_box(pm *b, float w, float h, float d, pcol c0, pcol c1) { box_ex(b, w, h, d, h * 0.5f, c0, c1, false); }
void pm_box_c(pm *b, float w, float h, float d, pcol c) { box_ex(b, w, h, d, 0, c, c, true); }

void pm_cyl_ex(pm *b, float r0, float r1, float h, int seg, pcol c0, pcol c1, bool cap_bottom, bool cap_top) {
  if (seg < 3) seg = 3;
  float dr = r0 - r1;
  uint32_t jid = ++b->jid * 7919u + b->seed;
  for (int i = 0; i < seg; i++) {
    float a0 = CW_TAU * (float)i / (float)seg, a1 = CW_TAU * (float)(i + 1) / (float)seg;
    float s0 = sinf(a0), k0 = cosf(a0), s1 = sinf(a1), k1 = cosf(a1);
    float ja0 = 1, ja1 = 1, jb0 = 1, jb1 = 1;
    if (b->jit > 0) {
      int i1 = (i + 1) % seg;
      ja0 = 1 + b->jit * (hashf(jid + (uint32_t)i * 131u) * 2 - 1), ja1 = 1 + b->jit * (hashf(jid + (uint32_t)i1 * 131u) * 2 - 1);
      jb0 = 1 + b->jit * (hashf(jid + 77777u + (uint32_t)i * 131u) * 2 - 1), jb1 = 1 + b->jit * (hashf(jid + 77777u + (uint32_t)i1 * 131u) * 2 - 1);
    }
    t3_vec3 pa = V(r0 * k0 * ja0, 0, -r0 * s0 * ja0), pb = V(r0 * k1 * ja1, 0, -r0 * s1 * ja1);
    t3_vec3 pc = V(r1 * k1 * jb1, h, -r1 * s1 * jb1), pd = V(r1 * k0 * jb0, h, -r1 * s0 * jb0);
    if (b->smooth) {
      t3_vec3 na = vnorm(V(k0 * h, dr, -s0 * h)), nb = vnorm(V(k1 * h, dr, -s1 * h));
      if (r1 > 1e-5f) {
        tri_smooth(b, pa, na, c0, pb, nb, c0, pc, nb, c1);
        tri_smooth(b, pa, na, c0, pc, nb, c1, pd, na, c1);
      } else {
        tri_smooth(b, pa, na, c0, pb, nb, c0, pd, na, c1);
      }
    } else if (r1 > 1e-5f) {
      quad_flat(b, pa, pb, pc, pd, c0, c0, c1, c1);
    } else {
      tri_flat(b, pa, pb, pd, c0, c0, c1);
    }
    if (cap_top && r1 > 1e-5f) tri_flat(b, pd, pc, V(0, h, 0), c1, c1, c1);
    if (cap_bottom) tri_flat(b, pb, pa, V(0, 0, 0), c0, c0, c0);
  }
}
void pm_cyl(pm *b, float r0, float r1, float h, int seg, pcol c0, pcol c1) { pm_cyl_ex(b, r0, r1, h, seg, c0, c1, false, true); }


/* ── balls (faceted lumps) ───────────────────────────────────────────────────────────────────────── */

static const float ICO[12][3] = {
  { -1, 1.618034f, 0 }, { 1, 1.618034f, 0 }, { -1, -1.618034f, 0 }, { 1, -1.618034f, 0 }, { 0, -1, 1.618034f }, { 0, 1, 1.618034f },
  { 0, -1, -1.618034f }, { 0, 1, -1.618034f }, { 1.618034f, 0, -1 }, { 1.618034f, 0, 1 }, { -1.618034f, 0, -1 }, { -1.618034f, 0, 1 },
};
static const int ICF[20][3] = {
  { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
  { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
};

typedef struct ball_ctx {
  pm *b;
  float rx, ry, rz, lumpy, ymin;
  pcol c0, c1;
  uint32_t seed;
} ball_ctx;

static t3_vec3 ball_pt(const ball_ctx *x, t3_vec3 d) {
  float l = vlen(d);
  d = vmul(d, 1.0f / l);
  float n = pm_noise3(d.x * 1.7f + 5, d.y * 1.7f, d.z * 1.7f, x->seed) * 0.65f +
            pm_noise3(d.x * 4.3f, d.y * 4.3f + 9, d.z * 4.3f, x->seed + 7) * 0.35f;
  float k = 1.0f + x->lumpy * n;
  t3_vec3 p = V(d.x * x->rx * k, d.y * x->ry * k, d.z * x->rz * k);
  if (p.y < x->ymin * x->ry) p.y = x->ymin * x->ry;
  return p;
}
static void ball_tri(ball_ctx *x, t3_vec3 a, t3_vec3 b2, t3_vec3 c, int depth) {
  if (depth > 0) {
    t3_vec3 ab = vnorm(vadd(a, b2)), bc = vnorm(vadd(b2, c)), ca = vnorm(vadd(c, a));
    ball_tri(x, a, ab, ca, depth - 1);
    ball_tri(x, ab, b2, bc, depth - 1);
    ball_tri(x, ca, bc, c, depth - 1);
    ball_tri(x, ab, bc, ca, depth - 1);
    return;
  }
  t3_vec3 p0 = ball_pt(x, a), p1 = ball_pt(x, b2), p2 = ball_pt(x, c);
  float ry = x->ry > 1e-6f ? x->ry : 1;
  pcol k0 = pc_mix(x->c0, x->c1, cw_clampf(p0.y / ry * 0.5f + 0.5f, 0, 1));
  pcol k1 = pc_mix(x->c0, x->c1, cw_clampf(p1.y / ry * 0.5f + 0.5f, 0, 1));
  pcol k2 = pc_mix(x->c0, x->c1, cw_clampf(p2.y / ry * 0.5f + 0.5f, 0, 1));
  /* shade by the facet's own slope too, so flat tops catch light and undersides sink: reads as form at a distance */
  tri_flat(x->b, p0, p1, p2, k0, k1, k2);
}
static void ball_ex(pm *b, float rx, float ry, float rz, int detail, float lumpy, pcol c0, pcol c1, float ymin) {
  ball_ctx x = { b, rx, ry, rz, lumpy, ymin, c0, c1, hash32(b->seed + (uint32_t)(rx * 131.0f) + (uint32_t)(b->L[0].nv)) };
  x.seed ^= (uint32_t)(pm_rand(b) * 65535.0f);
  for (int f = 0; f < 20; f++) {
    t3_vec3 a = vnorm(V(ICO[ICF[f][0]][0], ICO[ICF[f][0]][1], ICO[ICF[f][0]][2]));
    t3_vec3 c = vnorm(V(ICO[ICF[f][1]][0], ICO[ICF[f][1]][1], ICO[ICF[f][1]][2]));
    t3_vec3 d = vnorm(V(ICO[ICF[f][2]][0], ICO[ICF[f][2]][1], ICO[ICF[f][2]][2]));
    ball_tri(&x, a, c, d, detail < 0 ? 0 : detail > 3 ? 3 : detail);
  }
}
void pm_ball(pm *b, float rx, float ry, float rz, int detail, float lumpy, pcol c0, pcol c1) {
  ball_ex(b, rx, ry, rz, detail, lumpy, c0, c1, -2);
}
void pm_rock(pm *b, float r, float squash, float lumpy, pcol c0, pcol c1) {
  pm_push(b);
  pm_move(b, 0, r * squash * 0.5f, 0);
  ball_ex(b, r, r * squash, r, 1, lumpy, c0, c1, -0.5f);
  pm_pop(b);
}

void pm_prism(pm *b, float w, float h, float d, pcol c0, pcol c1) {
  float hx = w * 0.5f, hz = d * 0.5f;
  t3_vec3 a = V(-hx, 0, hz), c = V(hx, 0, hz), r1 = V(0, h, hz), a2 = V(-hx, 0, -hz), c2 = V(hx, 0, -hz), r2 = V(0, h, -hz);
  tri_flat(b, a, c, r1, c0, c0, c1);
  tri_flat(b, c2, a2, r2, c0, c0, c1);
  quad_flat(b, a, r1, r2, a2, c0, c1, c1, c0);
  quad_flat(b, c, c2, r2, r1, c0, c0, c1, c1);
  quad_flat(b, a, a2, c2, c, c0, c0, c0, c0);
}
void pm_wedge(pm *b, float w, float h, float d, pcol c0, pcol c1) {
  float hx = w * 0.5f, hz = d * 0.5f;
  t3_vec3 A = V(-hx, 0, -hz), B = V(hx, 0, -hz), C = V(hx, 0, hz), D = V(-hx, 0, hz), E = V(-hx, h, hz), F = V(hx, h, hz);
  quad_flat(b, A, E, F, B, c0, c1, c1, c0); /* the slope */
  quad_flat(b, D, C, F, E, c0, c0, c1, c1); /* the high end */
  quad_flat(b, A, B, C, D, c0, c0, c0, c0);
  tri_flat(b, A, D, E, c0, c0, c1);
  tri_flat(b, B, F, C, c0, c1, c0);
}

static void align_from(pm *b, t3_vec3 a, t3_vec3 c, float *len) {
  t3_vec3 d = vsub(c, a);
  float l = vlen(d);
  *len = l;
  t3_vec3 y = l > 1e-9f ? vmul(d, 1.0f / l) : V(0, 1, 0);
  t3_vec3 ref = fabsf(y.y) < 0.95f ? V(0, 1, 0) : V(1, 0, 0);
  t3_vec3 x = vnorm(vcross(ref, y));
  t3_vec3 z = vcross(x, y);
  pm_push(b);
  pm_basis(b, a, x, y, z);
}
void pm_beam(pm *b, t3_vec3 a, t3_vec3 c, float thick, pcol col) {
  float l;
  align_from(b, a, c, &l);
  box_ex(b, thick, l, thick, l * 0.5f, col, col, true);
  pm_pop(b);
}
void pm_limb(pm *b, t3_vec3 a, t3_vec3 c, float r0, float r1, int seg, pcol c0, pcol c1) {
  float l;
  align_from(b, a, c, &l);
  pm_cyl_ex(b, r0, r1, l, seg, c0, c1, false, true);
  pm_pop(b);
}
void pm_rod(pm *b, t3_vec3 a, t3_vec3 c, float r, int seg, pcol col) {
  float l;
  align_from(b, a, c, &l);
  pm_cyl_ex(b, r, r, l, seg, col, col, true, true);
  pm_pop(b);
}
void pm_cable(pm *b, t3_vec3 a, t3_vec3 c, float sag, float r, pcol col) {
  const int n = 8;
  t3_vec3 prev = a;
  for (int i = 1; i <= n; i++) {
    float t = (float)i / (float)n;
    t3_vec3 p = vadd(vadd(vmul(a, 1 - t), vmul(c, t)), V(0, -sag * 4.0f * t * (1 - t), 0));
    pm_rod(b, prev, p, r, 4, col);
    prev = p;
  }
}
void pm_lathe(pm *b, const float *rz, int n, int seg, pcol c0, pcol c1) {
  if (n < 2) return;
  float ymin = rz[1], ymax = rz[1];
  for (int i = 0; i < n; i++) ymin = fminf(ymin, rz[i * 2 + 1]), ymax = fmaxf(ymax, rz[i * 2 + 1]);
  float hh = ymax - ymin > 1e-6f ? ymax - ymin : 1;
  bool sm = b->smooth;
  b->smooth = true;
  for (int j = 0; j < seg; j++) {
    float a0 = CW_TAU * (float)j / (float)seg, a1 = CW_TAU * (float)(j + 1) / (float)seg;
    float s0 = sinf(a0), k0 = cosf(a0), s1 = sinf(a1), k1 = cosf(a1);
    for (int i = 0; i + 1 < n; i++) {
      float r0 = rz[i * 2], y0 = rz[i * 2 + 1], r1 = rz[i * 2 + 2], y1 = rz[i * 2 + 3];
      float tr = r1 - r0, ty = y1 - y0;
      float nl = sqrtf(tr * tr + ty * ty);
      if (nl < 1e-7f) continue;
      float nr = ty / nl, ny = -tr / nl; /* outward for a profile that runs upward */
      pcol ca = pc_mix(c0, c1, (y0 - ymin) / hh), cb = pc_mix(c0, c1, (y1 - ymin) / hh);
      t3_vec3 pa = V(r0 * k0, y0, -r0 * s0), pb = V(r0 * k1, y0, -r0 * s1), pc = V(r1 * k1, y1, -r1 * s1), pd = V(r1 * k0, y1, -r1 * s0);
      t3_vec3 na = V(nr * k0, ny, -nr * s0), nb = V(nr * k1, ny, -nr * s1);
      tri_smooth(b, pa, na, ca, pb, nb, ca, pc, nb, cb);
      tri_smooth(b, pa, na, ca, pc, nb, cb, pd, na, cb);
    }
  }
  b->smooth = sm;
}
void pm_ring(pm *b, float R, float r, int segR, int segr, pcol c) {
  bool sm = b->smooth;
  b->smooth = true;
  for (int i = 0; i < segR; i++) {
    float a0 = CW_TAU * (float)i / (float)segR, a1 = CW_TAU * (float)(i + 1) / (float)segR;
    for (int j = 0; j < segr; j++) {
      float u0 = CW_TAU * (float)j / (float)segr, u1 = CW_TAU * (float)(j + 1) / (float)segr;
      t3_vec3 p[4], n[4];
      float aa[4] = { a0, a1, a1, a0 }, uu[4] = { u0, u0, u1, u1 };
      for (int k = 0; k < 4; k++) {
        float ca = cosf(aa[k]), sa = sinf(aa[k]), cu = cosf(uu[k]), su = sinf(uu[k]);
        p[k] = V((R + r * cu) * ca, (R + r * cu) * sa, r * su);
        n[k] = V(cu * ca, cu * sa, su);
      }
      tri_smooth(b, p[0], n[0], c, p[1], n[1], c, p[2], n[2], c);
      tri_smooth(b, p[0], n[0], c, p[2], n[2], c, p[3], n[3], c);
    }
  }
  b->smooth = sm;
}
void pm_geo(pm *b, t3_geometry *g, pcol c0, pcol c1) {
  t3_attribute *P = g->attributes[T3_ATTR_POSITION], *Nn = g->attributes[T3_ATTR_NORMAL];
  if (!P || !g->index) return;
  const float *p = P->array, *n = Nn ? Nn->array : NULL;
  float ymin = 1e9f, ymax = -1e9f;
  for (int i = 0; i < P->count; i++) ymin = fminf(ymin, p[i * 3 + 1]), ymax = fmaxf(ymax, p[i * 3 + 1]);
  float hh = ymax - ymin > 1e-6f ? ymax - ymin : 1;
  const uint32_t *i32 = g->index->array;
  const uint16_t *i16 = g->index->array;
  for (int i = 0; i + 2 < g->index->count; i += 3) {
    int id[3];
    for (int k = 0; k < 3; k++) id[k] = g->index->type == T3_UINT16 ? i16[i + k] : (int)i32[i + k];
    t3_vec3 q[3];
    pcol cc[3];
    for (int k = 0; k < 3; k++) {
      q[k] = V(p[id[k] * 3], p[id[k] * 3 + 1], p[id[k] * 3 + 2]);
      cc[k] = pc_mix(c0, c1, (q[k].y - ymin) / hh);
    }
    if (b->smooth && n) {
      t3_vec3 nn[3];
      for (int k = 0; k < 3; k++) nn[k] = V(n[id[k] * 3], n[id[k] * 3 + 1], n[id[k] * 3 + 2]);
      tri_smooth(b, q[0], nn[0], cc[0], q[1], nn[1], cc[1], q[2], nn[2], cc[2]);
    } else {
      tri_flat(b, q[0], q[1], q[2], cc[0], cc[1], cc[2]);
    }
  }
}

t3_geometry *geo_merge(t3_geometry *const *g, const t3_mat4 *m, int n) {
  lbuf L = { 0 };
  bool uv = true;
  for (int k = 0; k < n; k++) uv = uv && g[k]->attributes[T3_ATTR_UV];
  float *uvs = NULL;
  int nuv = 0, capuv = 0;
  for (int k = 0; k < n; k++) {
    t3_attribute *P = g[k]->attributes[T3_ATTR_POSITION], *Nn = g[k]->attributes[T3_ATTR_NORMAL], *U = g[k]->attributes[T3_ATTR_UV];
    t3_attribute *C = g[k]->attributes[T3_ATTR_COLOR];
    if (!P) continue;
    const float *p = P->array, *nn = Nn ? Nn->array : NULL, *u = U ? U->array : NULL, *c = C && C->item_size == 3 ? C->array : NULL;
    int base = L.nv;
    for (int i = 0; i < P->count; i++) {
      t3_vec3 q = t3_vec3_apply_mat4(V(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]), &m[k]);
      t3_vec3 nrm = nn ? vnorm(t3_vec3_transform_direction(V(nn[i * 3], nn[i * 3 + 1], nn[i * 3 + 2]), &m[k])) : V(0, 1, 0);
      lb_vert(&L, q, nrm, c ? (pcol){ c[i * 3], c[i * 3 + 1], c[i * 3 + 2] } : (pcol){ 1, 1, 1 });
      if (uv) {
        if (nuv + 2 > capuv) capuv = capuv ? capuv * 2 : 512, uvs = realloc(uvs, sizeof(float) * (size_t)capuv);
        uvs[nuv++] = u ? u[i * 2] : 0, uvs[nuv++] = u ? u[i * 2 + 1] : 0;
      }
    }
    if (g[k]->index) {
      const uint32_t *i32 = g[k]->index->array;
      const uint16_t *i16 = g[k]->index->array;
      for (int i = 0; i + 2 < g[k]->index->count; i += 3) {
        int a = g[k]->index->type == T3_UINT16 ? i16[i] : (int)i32[i];
        int c2 = g[k]->index->type == T3_UINT16 ? i16[i + 1] : (int)i32[i + 1];
        int d = g[k]->index->type == T3_UINT16 ? i16[i + 2] : (int)i32[i + 2];
        lb_tri(&L, base + a, base + c2, base + d);
      }
    } else {
      for (int i = 0; i + 2 < P->count; i += 3) lb_tri(&L, base + i, base + i + 1, base + i + 2);
    }
  }
  t3_geometry *out = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, L.p, L.nv, 3), t3_geometry_set_attribute(out, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, L.n, L.nv, 3), t3_geometry_set_attribute(out, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, L.c, L.nv, 3), t3_geometry_set_attribute(out, T3_ATTR_COLOR, a), t3_release(a);
  if (uv && uvs) a = t3_attribute_new(T3_FLOAT32, uvs, L.nv, 2), t3_geometry_set_attribute(out, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, L.ix, L.ni, 1), t3_geometry_set_index(out, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(out);
  free(L.p), free(L.n), free(L.c), free(L.ix), free(uvs);
  return out;
}

/* box-projected uvs: one repeat per metre, on the plane the face leans to. */
static t3_geometry *lbuf_geometry(lbuf *l, float *radius, float *height, float uvm) {
  float iu = 1.0f / uvm;
  if (l->ni == 0) return NULL;
  t3_geometry *g = t3_geometry_new();
  float *uv = malloc(sizeof(float) * 2 * (size_t)l->nv);
  for (int i = 0; i < l->nv; i++) {
    const float *p = l->p + i * 3, *n = l->n + i * 3;
    float ax = fabsf(n[0]), ay = fabsf(n[1]), az = fabsf(n[2]);
    if (ay >= ax && ay >= az) uv[i * 2] = p[0] * iu, uv[i * 2 + 1] = p[2] * iu;
    else if (ax >= az) uv[i * 2] = p[2] * iu, uv[i * 2 + 1] = p[1] * iu;
    else uv[i * 2] = p[0] * iu, uv[i * 2 + 1] = p[1] * iu;
    float rr = sqrtf(p[0] * p[0] + p[2] * p[2]);
    if (rr > *radius) *radius = rr;
    if (p[1] > *height) *height = p[1];
  }
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, l->p, l->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, l->n, l->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, l->c, l->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, uv, l->nv, 2), t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, l->ix, l->ni, 1), t3_geometry_set_index(g, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(g);
  free(l->p), free(l->n), free(l->c), free(l->ix), free(uv);
  memset(l, 0, sizeof *l);
  return g;
}
prop_model pm_finish(pm *b) {
  prop_model m = { 0 };
  for (int i = 0; i < PL_COUNT; i++) m.g[i] = lbuf_geometry(&b->L[i], &m.radius, &m.height, b->uvm);
  free(b);
  return m;
}
void prop_model_free(prop_model *m) {
  for (int i = 0; i < PL_COUNT; i++)
    if (m->g[i]) t3_release(m->g[i]), m->g[i] = NULL;
}

/* ── materials ───────────────────────────────────────────────────────────────────────────────────── */

typedef struct matkey {
  int layer, tex;
  float tint[3], rough;
  t3_material *m;
} matkey;
static matkey MATS[96];
static int nmats;

/* The shared material for a layer. tex < 0: no texture. PL_GLOW is unlit and carries its brightness in the
 * vertex colours (values above 1 bloom). */
t3_material *prop_material(int layer, int tex, pcol tint, float rough_k);
t3_material *prop_material(int layer, int tex, pcol tint, float rough_k) {
  for (int i = 0; i < nmats; i++) {
    const matkey *k = &MATS[i];
    if (k->layer == layer && k->tex == tex && k->tint[0] == tint.r && k->tint[1] == tint.g && k->tint[2] == tint.b && k->rough == rough_k)
      return k->m;
  }
  t3_material *m;
  if (layer == PL_GLOW) {
    m = t3_mesh_basic_material_new(0xffffff);
    m->vertex_colors = true;
  } else {
    m = phys((t3_color){ tint.r, tint.g, tint.b }, 0.8f, 0);
    m->vertex_colors = true;
    if (layer == PL_MAIN) m->diffuse_roughness = rough_diffuse(); /* rock, wood, cloth: soft at grazing angles, not plastic */
    if (layer == PL_METAL) m->roughness = 0.38f, m->metalness = 0.85f;
    if (layer == PL_LEAF) m->roughness = 0.75f, m->side = T3_DOUBLE_SIDE;
    m->roughness = cw_clampf(m->roughness * rough_k, 0.04f, 1);
    if (tex >= 0 && layer == PL_MAIN) {
      tex_apply(m, tex, 1, 1, 0.8f, 0.8f);
      /* a texture multiplies the vertex colours; its albedo averages mid-dark, so lift the tint to keep the painted look */
      if (m->map) m->color = (t3_color){ m->color.r * 2.2f, m->color.g * 2.2f, m->color.b * 2.2f };
    }
  }
  if (nmats < (int)(sizeof MATS / sizeof MATS[0]))
    MATS[nmats++] = (matkey){ layer, tex, { tint.r, tint.g, tint.b }, rough_k, m };
  return m; /* the cache holds the reference */
}

/* ── instancing ──────────────────────────────────────────────────────────────────────────────────── */

struct prop_batch {
  t3_geometry *g;
  t3_material *m;
  float *mat, *col;
  int n, cap;
  float chunk;
  float radius; /* the model's bounding radius at scale 1: a cell of them too small to see from a view is not drawn there */
  bool tinted;
};

prop_batch *pb_begin(t3_geometry *g, t3_material *m) {
  prop_batch *b = calloc(1, sizeof *b);
  b->g = t3_retain(g);
  b->m = t3_retain(m);
  b->chunk = 110;
  b->tinted = true; /* always carry instance colours: one program family for every instanced prop */
  return b;
}
void pb_chunk(prop_batch *b, float metres) { b->chunk = metres > 5 ? metres : 5; }
void pb_radius(prop_batch *b, float r) { b->radius = r; }
int pb_count(const prop_batch *b) { return b ? b->n : 0; }
void pb_add_c(prop_batch *b, t3_vec3 pos, t3_quat q, t3_vec3 scale, pcol tint) {
  if (!b) return;
  if (b->n == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 64;
    b->mat = realloc(b->mat, sizeof(float) * 16 * (size_t)b->cap);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)b->cap);
  }
  t3_mat4 m;
  t3_mat4_compose(&m, pos, q, scale);
  memcpy(b->mat + b->n * 16, m.e, sizeof m.e);
  b->col[b->n * 3] = tint.r, b->col[b->n * 3 + 1] = tint.g, b->col[b->n * 3 + 2] = tint.b;
  if (tint.r != 1 || tint.g != 1 || tint.b != 1) b->tinted = true;
  b->n++;
}
void pb_add_q(prop_batch *b, t3_vec3 pos, t3_quat q, t3_vec3 scale) { pb_add_c(b, pos, q, scale, (pcol){ 1, 1, 1 }); }
void pb_add(prop_batch *b, t3_vec3 pos, float yaw, t3_vec3 scale) {
  pb_add_q(b, pos, t3_quat_from_axis_angle(V(0, 1, 0), yaw), scale);
}

typedef struct cellkey {
  int64_t key;
  int idx;
} cellkey;
static int cell_cmp(const void *a, const void *b) {
  const cellkey *x = a, *y = b;
  return x->key < y->key ? -1 : x->key > y->key ? 1 : x->idx - y->idx;
}

cw_node pb_finish(prop_batch *b, cw_node parent, const char *name, bool cast_shadow) {
  cw_node node = { 0 };
  if (!b) return node;
  if (b->n > 0) {
    cellkey *ck = malloc(sizeof *ck * (size_t)b->n);
    float inv = 1.0f / b->chunk;
    for (int i = 0; i < b->n; i++) {
      const float *e = b->mat + i * 16;
      int64_t cx = (int64_t)floorf(e[12] * inv) + 4096, cy = (int64_t)floorf(e[13] * inv * 0.5f) + 4096, cz = (int64_t)floorf(e[14] * inv) + 4096;
      ck[i] = (cellkey){ (cx << 40) | (cy << 20) | cz, i };
    }
    qsort(ck, (size_t)b->n, sizeof *ck, cell_cmp);
    t3_object *group = t3_group_new();
    for (int i = 0; i < b->n;) {
      int j = i;
      while (j < b->n && ck[j].key == ck[i].key) j++;
      t3_instanced_mesh *im = t3_instanced_mesh_new(b->g, b->m, j - i);
      for (int k = i; k < j; k++) {
        t3_mat4 mm;
        memcpy(mm.e, b->mat + ck[k].idx * 16, sizeof mm.e);
        t3_instanced_mesh_set_matrix_at(im, k - i, &mm);
        if (b->tinted)
          t3_instanced_mesh_set_color_at(im, k - i, (t3_color){ b->col[ck[k].idx * 3], b->col[ck[k].idx * 3 + 1], b->col[ck[k].idx * 3 + 2] });
      }
      im->mesh.base.cast_shadow = cast_shadow;
      im->mesh.base.receive_shadow = true;
      /* screen-size culling: past the distance where the biggest copy in the cell is under two pixels tall in a view
       * (that view's height and field of view), the cell is not drawn there; three.c's LOD picks per view. The distance
       * is from the cell's middle, so the cell's own size is added */
      float cull = 0;
      if (b->radius > 0) {
        t3_vec3 c = { 0, 0, 0 };
        float smax = 0;
        for (int k = i; k < j; k++) {
          const float *e = b->mat + ck[k].idx * 16;
          c.x += e[12], c.y += e[13], c.z += e[14];
          smax = fmaxf(smax, sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]));
        }
        c = (t3_vec3){ c.x / (float)(j - i), c.y / (float)(j - i), c.z / (float)(j - i) };
        float spread = 0;
        for (int k = i; k < j; k++) {
          const float *e = b->mat + ck[k].idx * 16;
          float dx = e[12] - c.x, dy = e[13] - c.y, dz = e[14] - c.z;
          spread = fmaxf(spread, sqrtf(dx * dx + dy * dy + dz * dz));
        }
        bool split = game_planned_views() > 1;
        /* a view 1080 px tall at 64 degrees, or 540 px at no wider than 48 in split screen; two pixels */
        float px_per = split ? 540.0f / (2.0f * 0.445f) : 1080.0f / (2.0f * 0.625f);
        cull = b->radius * smax * 2.0f * px_per / 2.0f + spread + b->radius * smax;
        if (cull < 2600.0f) {
          t3_lod *lod = t3_lod_new();
          t3_object_set_position(lod, c.x, c.y, c.z);
          t3_object_set_position(im, -c.x, -c.y, -c.z);
          t3_lod_add_level(lod, im, 0);
          t3_object *none = t3_group_new();
          t3_lod_add_level(lod, none, cull);
          t3_release(none);
          t3_object_add(group, lod);
          t3_release(lod);
        }
      }
      if (!(cull > 0 && cull < 2600.0f)) t3_object_add(group, im);
      t3_release(im);
      i = j;
    }
    free(ck);
    node = hang(parent, name, group);
    t3_release(group);
  }
  t3_release(b->g);
  t3_release(b->m);
  free(b->mat), free(b->col);
  free(b);
  return node;
}

/* ── kinds ───────────────────────────────────────────────────────────────────────────────────────── */

typedef struct kind_def {
  const char *name;
  gen_fn gen;
  float size;
  int variants;
  int tex;
  bool shadow;
} kind_def;

static const kind_def KINDS[PK_KINDS] = {
  [PK_ROCK] = { "rock", gen_rock, 2.2f, 4, TEX_GROUND_ROCK, true },
  [PK_BOULDERS] = { "boulders", gen_boulders, 3.0f, 3, TEX_GROUND_ROCK, true },
  [PK_MESA] = { "mesa", gen_mesa, 38, 3, TEX_GROUND_ROCK, true },
  [PK_SPIRE] = { "spire", gen_spire, 26, 3, TEX_GROUND_ROCK, true },
  [PK_CANYON_WALL] = { "canyon wall", gen_canyon_wall, 30, 3, TEX_GROUND_ROCK, true },
  [PK_DUNE] = { "dune", gen_dune, 7, 3, TEX_GROUND_SAND, false },
  [PK_CACTUS] = { "cactus", gen_cactus, 5, 3, -1, true },
  [PK_DEAD_TREE] = { "dead tree", gen_dead_tree, 6.5f, 4, -1, true },
  [PK_PINE] = { "pine", gen_pine, 12, 4, -1, true },
  [PK_PALM] = { "palm", gen_palm, 9, 3, -1, true },
  [PK_BROADLEAF] = { "broadleaf", gen_broadleaf, 10, 4, -1, true },
  [PK_JUNGLE_TREE] = { "jungle tree", gen_jungle_tree, 20, 3, -1, true },
  [PK_FERN] = { "fern", gen_fern, 2.4f, 3, -1, false },
  [PK_MUSHROOM] = { "mushroom", gen_mushroom, 4, 3, -1, true },
  [PK_ICE_SPIKES] = { "ice spikes", gen_ice_spikes, 6, 3, TEX_WALL_ICE, true },
  [PK_ICE_BOULDER] = { "ice boulder", gen_ice_boulder, 2.6f, 3, TEX_WALL_ICE, true },
  [PK_SNOW_PINE] = { "snow pine", gen_snow_pine, 12, 4, -1, true },
  [PK_CRYSTAL] = { "crystal", gen_crystal, 4, 4, -1, false },
  [PK_GRASS] = { "grass", gen_grass, 1.1f, 3, -1, false },
  [PK_BUSH] = { "bush", gen_bush, 1.4f, 3, -1, false },
  [PK_BARREL] = { "barrel", gen_barrel, 1.0f, 5, -1, true },
  [PK_CRATE] = { "crate", gen_crate, 1.3f, 4, TEX_WALL_WOOD, true },
  [PK_CONTAINER] = { "container", gen_container, 2.6f, 4, TEX_WALL_RUST, true },
  [PK_TYRE] = { "tyre", gen_tyre, 0.55f, 1, -1, true },
  [PK_TYRE_STACK] = { "tyre stack", gen_tyre_stack, 1.8f, 3, -1, true },
  [PK_WRECK_CAR] = { "wrecked car", gen_wreck_car, 1.6f, 4, TEX_RUST, true },
  [PK_WRECK_TRUCK] = { "wrecked truck", gen_wreck_truck, 3.2f, 2, TEX_RUST, true },
  [PK_PIPE] = { "pipe", gen_pipe, 1.6f, 3, -1, true },
  [PK_PIPE_RACK] = { "pipe rack", gen_pipe_rack, 5.5f, 3, -1, true },
  [PK_TANK] = { "tank", gen_tank, 9, 3, TEX_METAL, true },
  [PK_SILO] = { "silo", gen_silo, 18, 3, TEX_METAL, true },
  [PK_FLARE_STACK] = { "flare stack", gen_flare_stack, 42, 2, -1, true },
  [PK_COOLING_TOWER] = { "cooling tower", gen_cooling_tower, 55, 2, TEX_WALL_CONCRETE, true },
  [PK_CRANE] = { "crane", gen_crane, 38, 3, -1, true },
  [PK_PYLON] = { "pylon", gen_pylon, 30, 2, -1, true },
  [PK_LIGHT_POLE] = { "light pole", gen_light_pole, 14, 3, -1, true },
  [PK_STREET_LAMP] = { "street lamp", gen_street_lamp, 7, 3, -1, true },
  [PK_NEON_SIGN] = { "neon sign", gen_neon_sign, 12, 6, -1, false },
  [PK_BILLBOARD] = { "billboard", gen_billboard, 14, 4, -1, true },
  [PK_SKYSCRAPER] = { "skyscraper", gen_skyscraper, 70, 5, TEX_WALL_CONCRETE, true },
  [PK_RUIN_BUILDING] = { "ruined building", gen_ruin_building, 18, 4, TEX_WALL_BRICK, true },
  [PK_WATER_TOWER] = { "water tower", gen_water_tower, 22, 2, -1, true },
  [PK_WINDMILL] = { "windmill", gen_windmill, 20, 2, -1, true },
  [PK_WIND_PUMP] = { "wind pump", gen_wind_pump, 12, 2, -1, true },
  [PK_TENT] = { "tent", gen_tent, 6, 4, -1, true },
  [PK_BIGTOP] = { "big top", gen_bigtop, 24, 3, -1, true },
  [PK_FERRIS_WHEEL] = { "ferris wheel", gen_ferris_wheel, 40, 2, -1, true },
  [PK_CAROUSEL] = { "carousel", gen_carousel, 10, 2, -1, true },
  [PK_COASTER_LIFT] = { "coaster lift hill", gen_coaster_lift, 34, 3, -1, true },
  [PK_TEMPLE_BLOCK] = { "temple block", gen_temple_block, 8, 4, TEX_WALL_STONE, true },
  [PK_PILLAR] = { "pillar", gen_pillar, 9, 3, TEX_WALL_STONE, true },
  [PK_STATUE] = { "statue", gen_statue, 10, 3, TEX_WALL_STONE, true },
  [PK_RUIN_ARCH] = { "ruin arch", gen_ruin_arch, 12, 3, TEX_WALL_STONE, true },
  [PK_STAIRS] = { "stairs", gen_stairs, 6, 3, TEX_WALL_STONE, true },
  [PK_BRIDGE_POST] = { "bridge post", gen_bridge_post, 4.5f, 2, TEX_WALL_WOOD, true },
  [PK_DISH] = { "dish", gen_dish, 7, 2, -1, true },
  [PK_SOLAR_PANEL] = { "solar panel", gen_solar_panel, 3, 3, -1, true },
  [PK_STATION_MODULE] = { "station module", gen_station_module, 14, 4, -1, true },
  [PK_ANTENNA] = { "antenna", gen_antenna, 16, 3, -1, true },
  [PK_DEBRIS] = { "debris", gen_debris, 4, 5, -1, false },
  [PK_CONE] = { "cone", gen_cone, 0.75f, 1, -1, false },
  [PK_HAY] = { "hay bale", gen_hay, 1.2f, 2, -1, true },
  [PK_BOLLARD] = { "bollard", gen_bollard, 1.1f, 3, -1, false },
  [PK_TORCH] = { "torch", gen_torch, 3.2f, 2, -1, false },
};

const char *prop_name(prop_kind k) { return k >= 0 && k < PK_KINDS ? KINDS[k].name : "?"; }
int prop_variants(prop_kind k) { return k >= 0 && k < PK_KINDS ? KINDS[k].variants : 1; }
float prop_default_size(prop_kind k) { return k >= 0 && k < PK_KINDS ? KINDS[k].size : 1; }
int prop_tex(prop_kind k) { return k >= 0 && k < PK_KINDS ? KINDS[k].tex : -1; }

prop_model prop_make(prop_kind k, float size, uint32_t seed, int variant, int lod) {
  prop_model none = { 0 };
  if (k < 0 || k >= PK_KINDS) return none;
  const kind_def *d = &KINDS[k];
  pm *b = pm_new(seed * 7919u + (uint32_t)k * 104729u + (uint32_t)variant * 31337u);
  if (size <= 0) size = d->size;
  int nv = d->variants > 0 ? d->variants : 1;
  if (d->tex >= 0) pm_uv_scale(b, tex_metres(d->tex));
  d->gen(b, size, ((variant % nv) + nv) % nv, lod);
  return pm_finish(b);
}

/* ── prop sets ───────────────────────────────────────────────────────────────────────────────────── */

typedef struct ps_entry {
  prop_kind kind;
  int variant, lod;
  prop_model model;
  prop_batch *batch[PL_COUNT];
  int tex;
  pcol tint;
  float rough;
} ps_entry;
typedef struct ps_style_t {
  prop_kind kind;
  int tex;
  pcol tint;
  float rough;
} ps_style_t;
struct prop_set {
  cw_node parent;
  char name[40];
  ps_entry *e;
  int n, cap;
  ps_style_t *style;
  int nstyle;
  bool shadows, done;
  int count;
  float chunk;
};

prop_set *ps_new(cw_node parent, const char *name) {
  prop_set *s = calloc(1, sizeof *s);
  s->parent = parent;
  snprintf(s->name, sizeof s->name, "%s", name ? name : "Props");
  s->shadows = true;
  return s;
}
void ps_shadows(prop_set *s, bool on) { s->shadows = on; }
void ps_chunk(prop_set *s, float metres) { s->chunk = metres; }
int ps_count(const prop_set *s) { return s->count; }
void ps_style(prop_set *s, prop_kind k, int tex, pcol tint, float rough_scale) {
  s->style = realloc(s->style, sizeof *s->style * (size_t)(s->nstyle + 1));
  s->style[s->nstyle++] = (ps_style_t){ k, tex, tint, rough_scale > 0 ? rough_scale : 1 };
}

static ps_entry *ps_entry_get(prop_set *s, prop_kind k, int variant, int lod) {
  for (int i = 0; i < s->n; i++)
    if (s->e[i].kind == k && s->e[i].variant == variant && s->e[i].lod == lod) return &s->e[i];
  if (s->n == s->cap) s->cap = s->cap ? s->cap * 2 : 32, s->e = realloc(s->e, sizeof *s->e * (size_t)s->cap);
  ps_entry *e = &s->e[s->n++];
  memset(e, 0, sizeof *e);
  e->kind = k, e->variant = variant, e->lod = lod;
  e->tex = KINDS[k].tex, e->tint = (pcol){ 1, 1, 1 }, e->rough = 1;
  for (int i = 0; i < s->nstyle; i++)
    if (s->style[i].kind == k) e->tex = s->style[i].tex, e->tint = s->style[i].tint, e->rough = s->style[i].rough;
  e->model = prop_make(k, 0, 1, variant, lod);
  for (int l = 0; l < PL_COUNT; l++) {
    if (!e->model.g[l]) continue;
    t3_material *m = prop_material(l, l == PL_MAIN ? e->tex : -1, l == PL_GLOW ? (pcol){ 1, 1, 1 } : e->tint, e->rough);
    e->batch[l] = pb_begin(e->model.g[l], m);
  }
  return e;
}
float prop_radius(prop_set *s, prop_kind k, int variant, int lod);
float prop_radius(prop_set *s, prop_kind k, int variant, int lod) { return ps_entry_get(s, k, variant, lod)->model.radius; }

void ps_add_ex(prop_set *s, prop_kind k, int variant, int lod, t3_vec3 pos, t3_quat q, t3_vec3 scale, pcol tint, uint32_t seed) {
  if (s->done || k < 0 || k >= PK_KINDS) return;
  int nv = KINDS[k].variants;
  if (variant < 0) variant = (int)(hash32(seed * 31u + 7u) % (uint32_t)nv);
  ps_entry *e = ps_entry_get(s, k, variant % nv, lod);
  for (int l = 0; l < PL_COUNT; l++) pb_add_c(e->batch[l], pos, q, scale, tint);
  /* dev aid: RACER_FLOATLOG=1 logs a prop set down over a metre above the ground as drawn (scenery in the air) */
  if (ground_have() && getenv("RACER_FLOATLOG")) {
    float gh = ground_height_at(NULL, pos.x, pos.z);
    if (pos.y - gh > 1.0f) cw_info("floatlog: %s at %.0f %.1f %.0f, %.1f m over the ground", KINDS[k].name, pos.x, pos.y, pos.z, pos.y - gh);
  }
  s->count++;
}
void ps_add(prop_set *s, prop_kind k, t3_vec3 pos, float yaw, float scale, uint32_t seed) {
  float j = 0.92f + 0.16f * hashf(seed * 977u + 3u);
  float warm = 0.96f + 0.08f * hashf(seed * 131u + 11u);
  pcol tint = { j * warm, j, j / warm };
  float sc = scale * (0.96f + 0.08f * hashf(seed * 17u + 5u));
  ps_add_ex(s, k, -1, 0, pos, t3_quat_from_axis_angle(V(0, 1, 0), yaw), V(sc, sc, sc), tint, seed);
}

void ps_finish(prop_set *s) {
  if (s->done) return;
  s->done = true;
  cw_node group = cw_node_new("Node3D", s->name);
  cw_node_add_child(s->parent, group);
  for (int i = 0; i < s->n; i++) {
    ps_entry *e = &s->e[i];
    for (int l = 0; l < PL_COUNT; l++) {
      if (!e->batch[l]) continue;
      char nm[64];
      snprintf(nm, sizeof nm, "%s%d.%d.%d", KINDS[e->kind].name, e->variant, e->lod, l);
      /* far copies (lod 1) never cast: a shadow map pass is dear and nobody sees what they would shade */
      bool cast = s->shadows && KINDS[e->kind].shadow && l != PL_GLOW && e->model.height > 1.5f && e->lod == 0;
      /* far copies are coarse too: bigger cells, far fewer draws */
      float ch = s->chunk > 0 ? s->chunk : 110.0f;
      /* split screen: bigger cells (each view draws every cell it sees, a draw apiece; a quarter-screen view culls
       * nearly as well with them) */
      if (game_planned_views() > 1) ch *= 2.5f;
      pb_chunk(e->batch[l], e->lod ? ch * 2.4f : ch);
      pb_radius(e->batch[l], fmaxf(e->model.radius, e->model.height * 0.5f));
      pb_finish(e->batch[l], group, nm, cast);
      e->batch[l] = NULL;
    }
    prop_model_free(&e->model);
  }
  free(s->e), free(s->style);
  s->e = NULL, s->n = 0;
}

static void road_index_reset(void);
void props_reset(void) {
  /* materials stay cached: they are shared by every set and cost nothing to keep; what pointed into the scene goes */
  water_reset();
  road_index_reset();
}

/* ── the road index: where the road is, horizontally, for clearance ─────────────────────────────── */

#define RC 24.0f
typedef struct road_index {
  const track *t;
  int n;
  float x0, z0;
  int w, h;
  int *start, *items;
} road_index;
static road_index RI;
static void road_index_reset(void) {
  free(RI.start), free(RI.items);
  memset(&RI, 0, sizeof RI);
}

static void road_index_build(const track *t) {
  if (RI.t == t && RI.n == t->nall && RI.start) return;
  free(RI.start), free(RI.items);
  memset(&RI, 0, sizeof RI);
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (int i = 0; i < t->nall; i++) {
    x0 = fminf(x0, t->s[i].p.x), x1 = fmaxf(x1, t->s[i].p.x);
    z0 = fminf(z0, t->s[i].p.z), z1 = fmaxf(z1, t->s[i].p.z);
  }
  RI.t = t, RI.n = t->nall;
  RI.x0 = x0 - RC, RI.z0 = z0 - RC;
  RI.w = (int)((x1 - RI.x0) / RC) + 2, RI.h = (int)((z1 - RI.z0) / RC) + 2;
  int cells = RI.w * RI.h;
  RI.start = calloc((size_t)cells + 1, sizeof(int));
  RI.items = malloc(sizeof(int) * (size_t)t->nall);
  int *cnt = calloc((size_t)cells, sizeof(int));
  for (int i = 0; i < t->nall; i++) {
    int cx = (int)((t->s[i].p.x - RI.x0) / RC), cz = (int)((t->s[i].p.z - RI.z0) / RC);
    cnt[cz * RI.w + cx]++;
  }
  for (int c = 0; c < cells; c++) RI.start[c + 1] = RI.start[c] + cnt[c];
  memset(cnt, 0, sizeof(int) * (size_t)cells);
  for (int i = 0; i < t->nall; i++) {
    int cx = (int)((t->s[i].p.x - RI.x0) / RC), cz = (int)((t->s[i].p.z - RI.z0) / RC);
    int c = cz * RI.w + cx;
    RI.items[RI.start[c] + cnt[c]++] = i;
  }
  free(cnt);
}

/* the road's horizontal reach at a sample: half width plus the wall/kerb shoulder, never less than a metre */
static float sample_reach(const tsample *s) { return s->hw + 1.0f + 0.07f * s->hw + (s->walls ? 0.6f : 0.0f); }

float road_gap_idx(const track *t, float x, float z, float *road_y, int *idx) {
  road_index_build(t);
  float best = 1e9f, by = 0;
  int bi = 0;
  int cx = (int)floorf((x - RI.x0) / RC), cz = (int)floorf((z - RI.z0) / RC);
  for (int ring = 0; ring < 6; ring++) {
    for (int dz = -ring; dz <= ring; dz++)
      for (int dx = -ring; dx <= ring; dx++) {
        if (ring > 0 && abs(dx) != ring && abs(dz) != ring) continue;
        int ix = cx + dx, iz = cz + dz;
        if (ix < 0 || iz < 0 || ix >= RI.w || iz >= RI.h) continue;
        int c = iz * RI.w + ix;
        for (int k = RI.start[c]; k < RI.start[c + 1]; k++) {
          const tsample *s = &t->s[RI.items[k]];
          float ex = s->p.x - x, ez = s->p.z - z;
          float g = sqrtf(ex * ex + ez * ez) - sample_reach(s);
          if (g < best) best = g, by = s->p.y, bi = RI.items[k];
        }
      }
    /* once the best found is closer than the ring's inner edge nothing farther can win */
    if (best < (float)ring * RC) break;
  }
  if (road_y) *road_y = by;
  if (idx) *idx = bi;
  return best;
}
float road_gap(const track *t, float x, float z, float *road_y) { return road_gap_idx(t, x, z, road_y, NULL); }
/* the lowest road surface (the low edge of a banked road) of every sample whose road comes within `reach` of (x, z), or
 * 1e9 when none does: where roads cross, the ground under them must stay under the lower one */
/* the highest the ground may stand at (x, z): just under every road near it, rising a metre for each metre out from
 * that road's edge (a bank), up to `reach` out; 1e9 when no road is that near. The ground mesh is coarse away from the
 * road: capping only right at the road left big triangles whose corners were clear of it spanning over the deck */
float road_cap_y(const track *t, float x, float z, float reach) {
  road_index_build(t);
  float cap = 1e9f;
  int cx = (int)floorf((x - RI.x0) / RC), cz = (int)floorf((z - RI.z0) / RC), rr = (int)ceilf(reach / RC) + 2;
  for (int dz = -rr; dz <= rr; dz++)
    for (int dx = -rr; dx <= rr; dx++) {
      int ix = cx + dx, iz = cz + dz;
      if (ix < 0 || iz < 0 || ix >= RI.w || iz >= RI.h) continue;
      int c = iz * RI.w + ix;
      for (int k = RI.start[c]; k < RI.start[c + 1]; k++) {
        const tsample *s = &t->s[RI.items[k]];
        if (s->flags & (TF_GAP | TF_TUNNEL)) continue;
        float ex = s->p.x - x, ez = s->p.z - z;
        float out = sqrtf(ex * ex + ez * ez) - sample_reach(s);
        if (out > reach) continue;
        float y = s->p.y - s->hw * fabsf(s->r.y) - 0.25f + fmaxf(out, 0.0f);
        if (y < cap) cap = y;
      }
    }
  return cap;
}
float road_low_y(const track *t, float x, float z, float reach) {
  road_index_build(t);
  float lo = 1e9f;
  int cx = (int)floorf((x - RI.x0) / RC), cz = (int)floorf((z - RI.z0) / RC), rr = (int)ceilf(reach / RC) + 2;
  for (int dz = -rr; dz <= rr; dz++)
    for (int dx = -rr; dx <= rr; dx++) {
      int ix = cx + dx, iz = cz + dz;
      if (ix < 0 || iz < 0 || ix >= RI.w || iz >= RI.h) continue;
      int c = iz * RI.w + ix;
      for (int k = RI.start[c]; k < RI.start[c + 1]; k++) {
        const tsample *s = &t->s[RI.items[k]];
        if (s->flags & (TF_GAP | TF_TUNNEL)) continue;
        float ex = s->p.x - x, ez = s->p.z - z;
        if (sqrtf(ex * ex + ez * ez) - sample_reach(s) > reach) continue;
        float y = s->p.y - s->hw * fabsf(s->r.y);
        if (y < lo) lo = y;
      }
    }
  return lo;
}
float branch_gap(const track *t, float x, float z) {
  float best = 1e9f;
  for (int i = t->n; i < t->nall; i++) {
    const tsample *s = &t->s[i];
    float ex = s->p.x - x, ez = s->p.z - z;
    float g = sqrtf(ex * ex + ez * ez) - sample_reach(s);
    if (g < best) best = g;
  }
  return best;
}
int road_nearest(const track *t, float x, float z, float *dist2d) {
  road_index_build(t);
  float best = 1e18f;
  int bi = 0;
  int cx = (int)floorf((x - RI.x0) / RC), cz = (int)floorf((z - RI.z0) / RC);
  for (int ring = 0; ring < 40; ring++) {
    for (int dz = -ring; dz <= ring; dz++)
      for (int dx = -ring; dx <= ring; dx++) {
        if (ring > 0 && abs(dx) != ring && abs(dz) != ring) continue;
        int ix = cx + dx, iz = cz + dz;
        if (ix < 0 || iz < 0 || ix >= RI.w || iz >= RI.h) continue;
        int c = iz * RI.w + ix;
        for (int k = RI.start[c]; k < RI.start[c + 1]; k++) {
          const tsample *s = &t->s[RI.items[k]];
          float ex = s->p.x - x, ez = s->p.z - z, d = ex * ex + ez * ez;
          if (d < best) best = d, bi = RI.items[k];
        }
      }
    if (best < ((float)ring * RC) * ((float)ring * RC) && best < 1e17f) break;
  }
  if (dist2d) *dist2d = sqrtf(best);
  return bi;
}

/* ── chunked builders: world-space geometry merged per cell so views cull it ──────────────────────── */

typedef struct pmc_cell {
  int64_t key;
  pm *b;
} pmc_cell;
struct pmc {
  float chunk;
  pmc_cell *c;
  int n, cap;
  uint32_t seed;
  float uvm;
};
pmc *pmc_new(float chunk, uint32_t seed) {
  pmc *c = calloc(1, sizeof *c);
  c->chunk = chunk > 8 ? chunk : 8;
  c->seed = seed;
  return c;
}
pm *pmc_at(pmc *c, t3_vec3 p) {
  float inv = 1.0f / c->chunk;
  int64_t cx = (int64_t)floorf(p.x * inv) + 4096, cy = (int64_t)floorf(p.y * inv * 0.5f) + 4096, cz = (int64_t)floorf(p.z * inv) + 4096;
  int64_t key = (cx << 40) | (cy << 20) | cz;
  for (int i = 0; i < c->n; i++)
    if (c->c[i].key == key) return c->c[i].b;
  if (c->n == c->cap) c->cap = c->cap ? c->cap * 2 : 16, c->c = realloc(c->c, sizeof *c->c * (size_t)c->cap);
  c->c[c->n] = (pmc_cell){ key, pm_new(c->seed + (uint32_t)c->n * 7919u) };
  if (c->uvm > 0) pm_uv_scale(c->c[c->n].b, c->uvm);
  return c->c[c->n++].b;
}
void pmc_texture_metres(pmc *c, float metres) { c->uvm = metres; }
cw_node pmc_finish(pmc *c, cw_node parent, const char *name, bool cast_shadow, int tex) {
  t3_object *group = t3_group_new();
  int tris = 0;
  for (int i = 0; i < c->n; i++) {
    if (tex >= 0 && c->uvm <= 0) pm_uv_scale(c->c[i].b, tex_metres(tex));
    prop_model m = pm_finish(c->c[i].b);
    for (int l = 0; l < PL_COUNT; l++) {
      if (!m.g[l]) continue;
      t3_material *mat = prop_material(l, l == PL_MAIN ? tex : -1, (pcol){ 1, 1, 1 }, 1);
      t3_mesh *me = t3_mesh_new(m.g[l], mat);
      me->base.cast_shadow = cast_shadow && l != PL_GLOW;
      me->base.receive_shadow = true;
      tris += m.g[l]->index->count / 3;
      t3_object_add(group, me);
      t3_release(me);
    }
    prop_model_free(&m);
  }
  cw_node n = hang(parent, name, group);
  t3_release(group);
  free(c->c), free(c);
  cw_info("%s: %d triangles", name, tris);
  return n;
}

/* ── scatter ─────────────────────────────────────────────────────────────────────────────────────── */

scatter_opts scatter_defaults(void) {
  scatter_opts o = { 0 };
  o.scale_min = 0.8f, o.scale_max = 1.3f;
  o.spacing = 20, o.jitter = 1;
  o.lat_min = 4, o.lat_max = 60;
  o.sides = SIDE_BOTH;
  o.lat_curve = 1;
  o.density = 4;
  o.margin = 3;
  o.lod_dist = 120;
  o.sink = 0.1f;
  o.max_variants = 3;
  o.seed = 1;
  return o;
}

static prop_kind pick_kind(const scatter_opts *o, uint32_t h) {
  float tot = 0;
  for (int i = 0; i < o->nkinds; i++) tot += o->weight[i] > 0 ? o->weight[i] : 1;
  float r = hashf(h) * tot;
  for (int i = 0; i < o->nkinds; i++) {
    float w = o->weight[i] > 0 ? o->weight[i] : 1;
    if (r < w) return o->kinds[i];
    r -= w;
  }
  return o->kinds[o->nkinds - 1];
}

/* tries one spot; true when placed */
static bool try_place(prop_set *s, const track *t, const scatter_opts *o, float x, float z, uint32_t h, float extra_margin) {
  if (o->nkinds <= 0) return false;
  if (o->mask && o->mask(x, z, o->mask_ud) < hashf(h + 91u)) return false;
  prop_kind k = pick_kind(o, h + 3u);
  int nv = KINDS[k].variants;
  int mv = o->max_variants > 0 && o->max_variants < nv ? o->max_variants : nv;
  int variant = (int)(hash32(h + 5u) % (uint32_t)mv);
  float sc = o->scale_min + (o->scale_max - o->scale_min) * hashf(h + 7u);
  float ry;
  float gap = road_gap(t, x, z, &ry);
  /* the first look at a model needs its radius: it is built once per (kind, variant, lod) */
  float far = o->lod_dist > 0 ? o->lod_dist : 120;
  int lod = gap > far ? 1 : 0;
  if (lod && o->far_thin > 0 && hashf(h + 13u) < o->far_thin) return false;
  float rad = ps_entry_get(s, k, variant, lod)->model.radius * sc * 0.8f;
  if (!o->allow_under && gap < rad + o->margin + extra_margin) return false;
  if (o->allow_under && gap < rad + o->margin + extra_margin) {
    float gy = o->ground ? o->ground(x, z, o->ground_ud) : o->base_y;
    if (fabsf(gy - ry) < 7) return false;
  }
  float gy = o->ground ? o->ground(x, z, o->ground_ud) : o->base_y;
  /* never over the ground as drawn: it is cut down beside the roads, and a rock set on the land before that cut hung
   * in the air (Frostbite's boulders over the snow slope) */
  if (o->ground && ground_have()) gy = fminf(gy, ground_height_at(NULL, x, z));
  t3_vec3 pos = V(x, gy - o->sink * sc, z);
  float yaw = hashf(h + 11u) * CW_TAU;
  t3_quat q = t3_quat_from_axis_angle(V(0, 1, 0), yaw);
  if (o->align_slope && o->ground) {
    float e = 1.5f;
    t3_vec3 n = vnorm(V(o->ground(x - e, z, o->ground_ud) - o->ground(x + e, z, o->ground_ud), 2 * e,
                        o->ground(x, z - e, o->ground_ud) - o->ground(x, z + e, o->ground_ud)));
    t3_quat tilt = t3_quat_from_axis_angle(vnorm(vcross(V(0, 1, 0), n)), acosf(cw_clampf(n.y, -1, 1)) * 0.8f);
    q = t3_quat_multiply(tilt, q);
  }
  float j = 0.9f + 0.2f * hashf(h + 17u), warm = 0.95f + 0.1f * hashf(h + 19u);
  ps_add_ex(s, k, variant, lod, pos, q, V(sc, sc, sc), (pcol){ j * warm, j, j / warm }, h);
  return true;
}

int scatter_along(prop_set *s, const track *t, const scatter_opts *o) {
  int placed = 0;
  float s0 = o->from_s, s1 = o->to_s;
  if (s1 <= s0) s0 = 0, s1 = t->length;
  float sp = o->spacing > 0.5f ? o->spacing : 0.5f;
  uint32_t hs = o->seed * 2654435761u;
  for (int side = 0; side < 2; side++) {
    if (!(o->sides & (side ? SIDE_LEFT : SIDE_RIGHT))) continue;
    int idx = 0;
    for (float d = s0; d < s1; d += sp, idx++) {
      uint32_t h = hash32(hs + (uint32_t)idx * 7349u + (uint32_t)side * 100003u);
      float ds = d + (hashf(h) - 0.5f) * sp * o->jitter;
      tsample a = track_at(t, cw_clampf(ds, 0, t->length));
      float edge = a.hw + 1.0f + 0.07f * a.hw + (a.walls ? 0.6f : 0.0f);
      float u = powf(hashf(h + 21u), o->lat_curve > 0 ? o->lat_curve : 1);
      float off = o->lat_min + (o->lat_max - o->lat_min) * u;
      float lat = (side ? -1.0f : 1.0f) * (edge + off);
      t3_vec3 p = track_point(t, ds, lat, 0);
      if (try_place(s, t, o, p.x, p.z, h, 0)) placed++;
    }
  }
  return placed;
}

int scatter_area(prop_set *s, const track *t, const scatter_opts *o) {
  int placed = 0;
  bool ring = o->ring_max > 0;
  float hx = ring ? o->ring_max : o->half_x, hz = ring ? o->ring_max : o->half_z;
  float cell = sqrtf(1000.0f / (o->density > 0.001f ? o->density : 0.001f));
  int nx = (int)(2 * hx / cell) + 1, nz = (int)(2 * hz / cell) + 1;
  uint32_t hs = o->seed * 2246822519u;
  for (int j = 0; j < nz; j++)
    for (int i = 0; i < nx; i++) {
      uint32_t h = hash32(hs + (uint32_t)(j * 7919 + i) * 2654435761u);
      float x = o->cx - hx + (i + hashf(h)) * cell, z = o->cz - hz + (j + hashf(h + 1u)) * cell;
      if (ring) {
        float d = sqrtf((x - o->cx) * (x - o->cx) + (z - o->cz) * (z - o->cz));
        if (d < o->ring_min || d > o->ring_max) continue;
      } else if (fabsf(x - o->cx) > hx || fabsf(z - o->cz) > hz) {
        continue;
      }
      if (try_place(s, t, o, x, z, h, 0)) placed++;
    }
  return placed;
}

/* ── the showcase ────────────────────────────────────────────────────────────────────────────────── */

static int size_cmp(const void *a, const void *b) {
  float x = prop_default_size(*(const prop_kind *)a), y = prop_default_size(*(const prop_kind *)b);
  return x < y ? -1 : x > y ? 1 : (*(const prop_kind *)a - *(const prop_kind *)b);
}

/* RACER_PROPS=1: every kind (the first two variants) in rows beside the start straight, small things nearest the road.
 * RACER_PROPS=2+k: just kind k, all its variants in a row at the roadside just ahead of kart 0's grid slot.
 * RACER_PROPS_LOD=1 uses the cheap models throughout. View it with RACER_STUDIO=0 RACER_STUDIO_CAM=0,12,3.5 (close) or
 * 0,90,60 (the whole gallery); the studio camera orbits kart 0 and hides the HUD. */
static cw_node find_type(cw_node n, const char *type) {
  if (cw_node_is(n, type)) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_type(c, type);
    if (cw_node_valid(r)) return r;
  }
}

static const track *SHOW_T;
static t3_vec3 show_lake(void) {
  const track *t = SHOW_T;
  return track_point(t, fmodf(t->length - 8.0f + 55.0f, t->length), -58.0f, 0);
}
static float show_ground(float x, float z, void *ud) {
  (void)ud;
  float h = ground_hills(SHOW_T, x, z, 9, 160, 14, 70, 7);
  t3_vec3 l = show_lake();
  float r = h + ground_basin(x, z, l.x, l.z, 36, 26, 5, 8);
  if (getenv("RACER_RAVINE")) { /* a gorge under the first straight, so the supports have something to stand on */
    float ry;
    int idx;
    float gap = road_gap_idx(SHOW_T, x, z, &ry, &idx);
    float sd = SHOW_T->s[idx].s;
    if (sd > 20 && sd < 135) r -= 9.0f * (1.0f - cw_smoothstep(8.0f, 26.0f, gap)) * cw_smoothstep(20, 34, sd) * (1.0f - cw_smoothstep(122, 135, sd));
  }
  return r;
}

/* RACER_DRESS=1: the dressing and the ground and water, on the proving ground (see props_gallery) */
static void dress_showcase(cw_node parent, const track *t) {
  SHOW_T = t;
  ground_opts g = ground_defaults();
  g.height = show_ground;
  g.tex = TEX_GROUND_GRASS;
  g.tint = rgb(0.36f, 0.52f, 0.24f), g.tint2 = rgb(0.58f, 0.5f, 0.3f);
  g.half = 1500;
  if (!getenv("RACER_NOGROUND")) ground_plane(parent, t, &g);
  if (getenv("RACER_GROUND_DEBUG")) {
    for (float sd = 200; sd < 500; sd += 40) {
      tsample a = track_at(t, sd);
      for (float lat = 0; lat <= 14; lat += 3.5f) {
        t3_vec3 q = track_point(t, sd, a.hw + lat, 0);
        float ry2;
        int ix2;
        float gp = road_gap_idx(t, q.x, q.z, &ry2, &ix2);
        cw_info("ground dbg s=%.0f lat=+%.1f road y=%.2f ground=%.2f gap=%.1f ry=%.2f idx=%d (s=%.0f)", sd, lat, q.y, ground_height_at(NULL, q.x, q.z), gp, ry2, ix2, t->s[ix2].s);
      }
    }
  }
  water_opts w = water_defaults();
  t3_vec3 lk = show_lake();
  if (!getenv("RACER_NOLAKE")) water_lake(parent, lk.x, lk.z, 36, 26, -0.6f, &w);
  /* the proving ground has no ford and no open edge: show those on a copy of the track with flags added */
  track tc = *t;
  tc.s = malloc(sizeof(tsample) * (size_t)t->n);
  memcpy(tc.s, t->s, sizeof(tsample) * (size_t)t->n);
  for (int i = 0; i < tc.n; i++) {
    float sd = tc.s[i].s;
    if (sd > 90 && sd < 135) tc.s[i].flags |= TF_WATER;
    if (sd > 20 && sd < 80) tc.s[i].flags |= TF_OPEN, tc.s[i].flags &= ~(uint32_t)TF_WALL_L, tc.s[i].hw += 2.5f; /* clear of the real wall */
  }
  water_along_track(parent, &tc, 0.6f, 0.06f, NULL);
  dress_style st = dress_defaults();
  deco_rails(parent, &tc, getenv("RACER_RAIL") ? atoi(getenv("RACER_RAIL")) : RAIL_GUARD, &st);
  dress_start_line(parent, t, &st);
  dress_checkpoints(parent, t, &st);
  deco_supports(parent, t, getenv("RACER_SUP") ? atoi(getenv("RACER_SUP")) : SUP_CONCRETE, ground_height_cb, NULL, 0, 2, &st);
  deco_lights(parent, t, 40, LAMP_STREET, 0, 0, &st);
  deco_jump_marks(parent, t, &st);
  deco_corner_arrows(parent, t, 0.01f, &st);
  deco_corner_signs(parent, t, 0.01f, &st);
  deco_banners(parent, t, 0, 0, 90, &st);
  if (getenv("RACER_NOSCATTER")) return;
  prop_set *ps = ps_new(parent, "Scenery");
  scatter_opts o = scatter_defaults();
  o.kinds[0] = PK_BROADLEAF, o.weight[0] = 3, o.kinds[1] = PK_PINE, o.weight[1] = 2, o.kinds[2] = PK_ROCK, o.weight[2] = 2, o.kinds[3] = PK_BUSH, o.weight[3] = 3;
  o.nkinds = 4, o.spacing = 12, o.lat_min = 3, o.lat_max = 70, o.ground = ground_height_cb, o.far_thin = 0.3f;
  int n = scatter_along(ps, t, &o);
  o.kinds[0] = PK_PINE, o.kinds[1] = PK_BROADLEAF, o.kinds[2] = PK_ROCK, o.nkinds = 3, o.density = 1.2f, o.ring_min = 90, o.ring_max = 600;
  o.cx = t->s[0].p.x, o.cz = t->s[0].p.z, o.seed = 9;
  n += scatter_area(ps, t, &o);
  ps_finish(ps);
  cw_info("showcase: %d scattered props", n);
}

void props_gallery(cw_node parent, const track *t) {
  const char *env = getenv("RACER_PROPS");
  int mode = env ? atoi(env) : 0;
  if (getenv("RACER_DRESS") && atoi(getenv("RACER_DRESS"))) {
    cw_node sky = find_type(parent, "ProceduralSky");
    if (cw_node_valid(sky)) cw_node_set(sky, "time_of_day", cw_val_float(getenv("RACER_PROPS_TOD") ? atof(getenv("RACER_PROPS_TOD")) : 14.0));
    dress_showcase(parent, t);
  }
  if (mode <= 0) return;
  /* judge the models in daylight (RACER_PROPS_TOD=hour, default 12; the scene's own dusk is 18.98) */
  cw_node sky = find_type(parent, "ProceduralSky");
  if (cw_node_valid(sky)) cw_node_set(sky, "time_of_day", cw_val_float(getenv("RACER_PROPS_TOD") ? atof(getenv("RACER_PROPS_TOD")) : 12.0));
  bool cheap = getenv("RACER_PROPS_LOD") && atoi(getenv("RACER_PROPS_LOD"));
  prop_set *ps = ps_new(parent, "Gallery");
  float hw = t->s[0].hw;
  float base = t->length - 8.0f; /* kart 0's slot */
  if (mode >= 2) {
    prop_kind k = (prop_kind)(mode - 2);
    if (k >= PK_KINDS) k = PK_KINDS - 1;
    int nv = prop_variants(k);
    float r = ps_entry_get(ps, k, 0, 0)->model.radius;
    float lat = 0;
    float step = r * 2.2f + 1.5f;
    float s = base - step * (float)(nv - 1) * 0.5f;
    for (int v = 0; v < nv; v++) {
      float ss = fmodf(s + t->length, t->length);
      tsample m = track_at(t, ss);
      t3_vec3 p = track_point(t, ss, lat, 0);
      float yaw = atan2f(-m.r.x, -m.r.z);
      ps_add_ex(ps, k, v, cheap, V(p.x, 0, p.z), t3_quat_from_axis_angle(V(0, 1, 0), yaw), V(1, 1, 1), (pcol){ 1, 1, 1 }, 1);
      s += step;
    }
    /* the camera orbits kart 0 (3.4 m left of the middle): T metres from the row. Close ones stay inside the road, far ones
     * go beyond its wall and look over it from higher up */
    float sz = prop_default_size(k);
    float T = fmaxf(sz * 2.0f, (float)nv * step * 0.55f);
    if (T < 4.5f) T = 4.5f;
    float hgt = T < hw + 2.0f ? fmaxf(1.8f, sz * 0.4f) : fminf(fmaxf(5.0f, sz * 0.5f), 40.0f);
    float dist = T - 3.4f;
    cw_info("props: %s, %d variants, default size %.1f, radius %.1f\nprops-cam: 90,%.0f,%.1f\n", prop_name(k), nv, sz, r, dist, hgt);
    ps_finish(ps);
    return;
  }
  prop_kind order[PK_KINDS];
  for (int i = 0; i < PK_KINDS; i++) order[i] = (prop_kind)i;
  qsort(order, PK_KINDS, sizeof order[0], size_cmp);
  float lat = hw + 8, rowd = 0, s = 4;
  for (int i = 0; i < PK_KINDS; i++) {
    prop_kind k = order[i];
    int nv = prop_variants(k);
    float r = ps_entry_get(ps, k, 0, 0)->model.radius;
    for (int v = 0; v < nv && v < 2; v++) {
      float step = r * 1.1f + 1.5f;
      if (s + 2 * step > 170) s = 4, lat += rowd * 2 + 6, rowd = 0;
      if (r > rowd) rowd = r;
      s += step;
      float ss = fmodf(base + s, t->length);
      tsample m = track_at(t, ss);
      t3_vec3 p = track_point(t, ss, lat + r, 0);
      float yaw = atan2f(-m.r.x, -m.r.z);
      ps_add_ex(ps, k, v, cheap, V(p.x, 0, p.z), t3_quat_from_axis_angle(V(0, 1, 0), yaw), V(1, 1, 1), (pcol){ 1, 1, 1 }, 1);
      s += step;
    }
  }
  ps_finish(ps);
}
