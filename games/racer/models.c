/* racer's kart modelling kit (meshes built from code) and the kart rig: materials, wheels, driver, plates, flames.
 * The six vehicles' shapes are in models_veh.c; the motion is in kartgfx.c.
 *
 * Each vehicle is a handful of merged meshes (one per material slot, models.h) built once and shared by every kart
 * of that vehicle. Paint, the one thing that differs, is a material per kart. Vertex colours carry grime, rust, decals
 * and the colours of the driver, so a whole kart is a few draws. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "models.h"

const float KART_RGB[8][3] = { { 0.95f, 0.25f, 0.2f }, { 0.2f, 0.55f, 0.95f }, { 0.95f, 0.8f, 0.15f }, { 0.3f, 0.8f, 0.35f },
                               { 0.85f, 0.4f, 0.9f }, { 0.95f, 0.55f, 0.15f }, { 0.25f, 0.85f, 0.85f }, { 0.9f, 0.9f, 0.9f } };

const float C_STEEL[3] = { 0.55f, 0.56f, 0.58f }, C_RUST[3] = { 0.5f, 0.28f, 0.16f }, C_DARK[3] = { 0.1f, 0.1f, 0.11f };
const float C_CHROME[3] = { 0.9f, 0.92f, 0.95f }, C_BRASS[3] = { 0.8f, 0.6f, 0.25f }, C_WHITE[3] = { 0.92f, 0.92f, 0.9f };
const float C_BLACK[3] = { 0.04f, 0.04f, 0.045f }, C_YELLOW[3] = { 0.95f, 0.78f, 0.1f }, C_RED[3] = { 0.8f, 0.12f, 0.1f };

void rgb_mix(float out[3], const float a[3], const float b[3], float t) {
  for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * t;
}
void rgb_mul(float out[3], const float a[3], float k) {
  for (int i = 0; i < 3; i++) out[i] = a[i] * k;
}

/* ── transforms ─────────────────────────────────────────────────────── */

xf xf_at(float x, float y, float z) { return (xf){ { x, y, z }, { 0, 0, 0, 1 }, { 1, 1, 1 } }; }
xf xf_rot(float x, float y, float z, float rx, float ry, float rz) {
  return (xf){ { x, y, z }, t3_quat_from_euler((t3_euler){ rx, ry, rz, T3_XYZ }), { 1, 1, 1 } };
}
xf xf_scaled(xf a, float sx, float sy, float sz) {
  a.s = (t3_vec3){ a.s.x * sx, a.s.y * sy, a.s.z * sz };
  return a;
}
static t3_quat quat_from_to(t3_vec3 f, t3_vec3 t) {
  f = v3norm(f), t = v3norm(t);
  float d = v3dot(f, t);
  if (d > 0.99999f) return (t3_quat){ 0, 0, 0, 1 };
  if (d < -0.99999f) {
    t3_vec3 ax = fabsf(f.x) < 0.9f ? v3(1, 0, 0) : v3(0, 0, 1);
    return t3_quat_from_axis_angle(v3norm(v3cross(f, ax)), 3.14159265f);
  }
  return t3_quat_from_axis_angle(v3norm(v3cross(f, t)), acosf(d));
}
xf xf_between(t3_vec3 a, t3_vec3 b, float rs) {
  t3_vec3 d = v3sub(b, a);
  float l = v3len(d);
  xf x = { v3mul(v3add(a, b), 0.5f), { 0, 0, 0, 1 }, { rs, l, rs } };
  if (l > 1e-6f) x.q = quat_from_to(v3(0, 1, 0), d);
  return x;
}
t3_vec3 xf_apply(const xf *x, t3_vec3 p) {
  p = (t3_vec3){ p.x * x->s.x, p.y * x->s.y, p.z * x->s.z };
  return v3add(t3_vec3_apply_quat(p, x->q), x->p);
}
void place_tube(t3_object *o, t3_vec3 a, t3_vec3 b) {
  xf x = xf_between(a, b, 1);
  t3_object_set_position(o, x.p.x, x.p.y, x.p.z);
  t3_object_set_quaternion(o, x.q);
  t3_object_set_scale(o, 1, x.s.y, 1);
}

/* ── the builder ────────────────────────────────────────────────────── */

static float hash3(t3_vec3 p) {
  uint32_t h = (uint32_t)((int)floorf(p.x * 37.0f) * 73856093) ^ (uint32_t)((int)floorf(p.y * 37.0f) * 19349663) ^ (uint32_t)((int)floorf(p.z * 37.0f) * 83492791);
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return (float)(h & 0xffff) / 65535.0f;
}

int mb_vert(mb *b, t3_vec3 p, t3_vec3 n, const float rgb[3]) {
  if (b->nv == b->cv) {
    b->cv = b->cv ? b->cv * 2 : 512;
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cv);
    b->n = realloc(b->n, sizeof(float) * 3 * (size_t)b->cv);
    b->c = realloc(b->c, sizeof(float) * 3 * (size_t)b->cv);
  }
  float k = 1.0f;
  if (b->jit > 0) k = 1.0f - b->jit * 0.45f * hash3(p);
  float *P = b->p + b->nv * 3, *N = b->n + b->nv * 3, *C = b->c + b->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x, N[1] = n.y, N[2] = n.z;
  C[0] = rgb[0] * k, C[1] = rgb[1] * k, C[2] = rgb[2] * k;
  return b->nv++;
}
void mb_tri(mb *b, int a, int c, int d) {
  if (b->ni + 3 > b->ci) {
    b->ci = b->ci ? b->ci * 2 : 1536;
    b->i = realloc(b->i, sizeof(uint32_t) * (size_t)b->ci);
  }
  b->i[b->ni++] = (uint32_t)a, b->i[b->ni++] = (uint32_t)c, b->i[b->ni++] = (uint32_t)d;
}
t3_geometry *mb_done(mb *b) {
  if (!b->nv || !b->ni) {
    free(b->p), free(b->n), free(b->c), free(b->i);
    float j = b->jit;
    memset(b, 0, sizeof *b);
    b->jit = j;
    return NULL;
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, b->p, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->n, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  float *uv = calloc((size_t)b->nv * 2, sizeof(float));
  for (int i = 0; i < b->nv; i++) uv[i * 2] = b->p[i * 3] * 2, uv[i * 2 + 1] = b->p[i * 3 + 2] * 2;
  a = t3_attribute_new(T3_FLOAT32, uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a);
  t3_release(a);
  free(uv);
  a = t3_attribute_new(T3_FLOAT32, b->c, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a);
  t3_release(a);
  a = t3_attribute_new(T3_UINT32, b->i, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  free(b->p), free(b->n), free(b->c), free(b->i);
  float j = b->jit;
  memset(b, 0, sizeof *b);
  b->jit = j;
  return g;
}

void mb_geo(mb *b, t3_geometry *g, xf x, const float rgb[3]) {
  t3_attribute *P = g->attributes[T3_ATTR_POSITION], *Nn = g->attributes[T3_ATTR_NORMAL];
  const float *p = P->array, *n = Nn->array;
  int base = b->nv;
  for (int i = 0; i < P->count; i++) {
    t3_vec3 q = xf_apply(&x, (t3_vec3){ p[i * 3], p[i * 3 + 1], p[i * 3 + 2] });
    t3_vec3 nn = { n[i * 3] / x.s.x, n[i * 3 + 1] / x.s.y, n[i * 3 + 2] / x.s.z };
    nn = v3norm(t3_vec3_apply_quat(nn, x.q));
    mb_vert(b, q, nn, rgb);
  }
  if (g->index) {
    const uint32_t *ix = g->index->array;
    const uint16_t *ix16 = g->index->array;
    bool flip = x.s.x * x.s.y * x.s.z < 0;
    for (int i = 0; i + 2 < g->index->count; i += 3) {
      int a, c, d;
      if (g->index->type == T3_UINT16) a = ix16[i], c = ix16[i + 1], d = ix16[i + 2];
      else a = (int)ix[i], c = (int)ix[i + 1], d = (int)ix[i + 2];
      if (flip) mb_tri(b, base + a, base + d, base + c);
      else mb_tri(b, base + a, base + c, base + d);
    }
  }
  t3_release(g);
}

void mb_cyl(mb *b, xf x, float r0, float r1, float h, int seg, const float rgb[3]) {
  mb_geo(b, t3_cylinder_geometry_new(r0, r1, h, seg, 1, false), x, rgb);
}
void mb_cone(mb *b, xf x, float r, float h, int seg, const float rgb[3]) { mb_geo(b, t3_cone_geometry_new(r, h, seg, 1, false), x, rgb); }
void mb_torus(mb *b, xf x, float r, float tube, int rs, int ts, const float rgb[3]) {
  mb_geo(b, t3_torus_geometry_new(r, tube, rs, ts, 6.2831853f), x, rgb);
}
void mb_lathe(mb *b, xf x, const t3_vec2 *pts, int n, int seg, const float rgb[3]) {
  mb_geo(b, t3_lathe_geometry_new(pts, n, seg, 0, 6.2831853f), x, rgb);
}
void mb_ball(mb *b, t3_vec3 c, float r, const float rgb[3]) { mb_geo(b, t3_sphere_geometry_new(r, 10, 7), xf_at(c.x, c.y, c.z), rgb); }
void mb_ballxyz(mb *b, t3_vec3 c, t3_vec3 r, const float rgb[3]) {
  xf x = xf_at(c.x, c.y, c.z);
  x.s = r;
  mb_geo(b, t3_sphere_geometry_new(1, 12, 8), x, rgb);
}
void mb_tube(mb *b, t3_vec3 a, t3_vec3 c, float r, int seg, const float rgb[3]) {
  mb_geo(b, t3_cylinder_geometry_new(1, 1, 1, seg, 1, false), xf_between(a, c, r), rgb);
}
void mb_tube_cap(mb *b, t3_vec3 a, t3_vec3 c, float r0, float r1, int seg, const float rgb[3]) {
  /* r0 at a, r1 at c */
  xf x = xf_between(a, c, 1);
  mb_geo(b, t3_cylinder_geometry_new(r1, r0, 1, seg, 1, false), x, rgb);
}
void mb_bolt(mb *b, t3_vec3 at, t3_vec3 dir, float r, const float rgb[3]) {
  t3_vec3 d = v3norm(dir);
  mb_geo(b, t3_cylinder_geometry_new(1, 1, 1, 6, 1, false), xf_between(v3sub(at, v3mul(d, r * 0.15f)), v3add(at, v3mul(d, r * 0.35f)), r), rgb);
}
void mb_polyline(mb *b, const t3_vec3 *pts, int n, float r, int seg, const float rgb[3]) {
  for (int i = 0; i + 1 < n; i++) mb_tube(b, pts[i], pts[i + 1], r, seg, rgb);
  for (int i = 1; i + 1 < n; i++) mb_ball(b, pts[i], r * 1.02f, rgb);
}

/* the grid lines of a rounded box side: flat in the middle, a few rows round each bevel */
static int box_lines(float *out, float h, float r, int steps) {
  int n = 0;
  if (r <= 0 || steps <= 0) {
    out[n++] = -h, out[n++] = h;
    return n;
  }
  static const float f1[2] = { 0, 1 }, f2[3] = { 0, 0.4f, 1 };
  const float *f = steps == 1 ? f1 : f2;
  int nf = steps == 1 ? 2 : 3;
  for (int i = 0; i < nf; i++) out[n++] = -h + r * f[i];
  for (int i = nf - 1; i >= 0; i--) out[n++] = h - r * f[i];
  return n;
}

void mb_rbox(mb *b, xf x, float w, float h, float d, float r, int steps, const float rgb[3]) {
  float half[3] = { w * 0.5f, h * 0.5f, d * 0.5f };
  float rr = fminf(r, fminf(half[0], fminf(half[1], half[2])) * 0.98f);
  float ln[3][8];
  int nl[3];
  for (int a = 0; a < 3; a++) nl[a] = box_lines(ln[a], half[a], rr, steps);
  for (int a = 0; a < 3; a++) {
    int ua = (a + 1) % 3, va = (a + 2) % 3;
    for (int s = -1; s <= 1; s += 2) {
      int base = b->nv;
      for (int j = 0; j < nl[va]; j++)
        for (int i = 0; i < nl[ua]; i++) {
          float pc[3];
          pc[a] = (float)s * half[a], pc[ua] = ln[ua][i], pc[va] = ln[va][j];
          float in[3], dd[3];
          for (int k = 0; k < 3; k++) {
            float lim = half[k] - rr;
            in[k] = pc[k] < -lim ? -lim : pc[k] > lim ? lim : pc[k];
            dd[k] = pc[k] - in[k];
          }
          t3_vec3 n = { dd[0], dd[1], dd[2] };
          float l = v3len(n);
          t3_vec3 lp;
          if (rr > 0 && l > 1e-6f) {
            n = v3mul(n, 1.0f / l);
            lp = (t3_vec3){ in[0] + n.x * rr, in[1] + n.y * rr, in[2] + n.z * rr };
          } else {
            lp = (t3_vec3){ pc[0], pc[1], pc[2] };
            float nv[3] = { 0, 0, 0 };
            nv[a] = (float)s;
            n = (t3_vec3){ nv[0], nv[1], nv[2] };
          }
          t3_vec3 wn = v3norm(t3_vec3_apply_quat((t3_vec3){ n.x / x.s.x, n.y / x.s.y, n.z / x.s.z }, x.q));
          mb_vert(b, xf_apply(&x, lp), wn, rgb);
        }
      for (int j = 0; j + 1 < nl[va]; j++)
        for (int i = 0; i + 1 < nl[ua]; i++) {
          int v00 = base + j * nl[ua] + i, v10 = v00 + 1, v01 = v00 + nl[ua], v11 = v01 + 1;
          if (s > 0) mb_tri(b, v00, v10, v11), mb_tri(b, v00, v11, v01);
          else mb_tri(b, v00, v11, v10), mb_tri(b, v00, v01, v11);
        }
    }
  }
}

static float spow(float v, float e) { return v < 0 ? -powf(-v, e) : powf(v, e); }

void mb_loft(mb *b, const sect *s, int ns, int ring, float a0, float a1, float inflate, const float bot[3], const float top[3]) {
  bool closed = a1 - a0 > 6.2f;
  int nr = closed ? ring : ring + 1;
  int base = b->nv;
  t3_vec3 *pos = malloc(sizeof(t3_vec3) * (size_t)(ns * nr));
  float *tt = malloc(sizeof(float) * (size_t)(ns * nr));
  for (int i = 0; i < ns; i++) {
    float cy = (s[i].yb + s[i].yt) * 0.5f, hh = (s[i].yt - s[i].yb) * 0.5f + inflate, ww = s[i].w + inflate;
    float e = 2.0f / s[i].n;
    for (int j = 0; j < nr; j++) {
      float th = a0 + (a1 - a0) * (float)j / (float)ring;
      float sx = sinf(th), cyv = cosf(th);
      pos[i * nr + j] = (t3_vec3){ s[i].x + ww * spow(sx, e), cy + hh * spow(cyv, e), s[i].z };
      tt[i * nr + j] = hh > 1e-5f ? (pos[i * nr + j].y - s[i].yb) / (s[i].yt - s[i].yb + 2 * inflate + 1e-6f) : 1;
    }
  }
  for (int i = 0; i < ns; i++)
    for (int j = 0; j < nr; j++) {
      int j0 = closed ? (j + nr - 1) % nr : (j > 0 ? j - 1 : j), j1 = closed ? (j + 1) % nr : (j + 1 < nr ? j + 1 : j);
      int i0 = i > 0 ? i - 1 : i, i1 = i + 1 < ns ? i + 1 : i;
      t3_vec3 dth = v3sub(pos[i * nr + j1], pos[i * nr + j0]), dz = v3sub(pos[i1 * nr + j], pos[i0 * nr + j]);
      t3_vec3 n = v3cross(dz, dth);
      if (v3len(n) < 1e-9f) n = v3(0, 1, 0);
      n = v3norm(n);
      float t = tt[i * nr + j];
      float rgb[3];
      if (bot) rgb_mix(rgb, bot, top, cw_clampf(t * 1.6f, 0, 1));
      else memcpy(rgb, top, sizeof rgb);
      mb_vert(b, pos[i * nr + j], n, rgb);
    }
  for (int i = 0; i + 1 < ns; i++) {
    int jn = closed ? nr : nr - 1;
    for (int j = 0; j < jn; j++) {
      int a = base + i * nr + j, c = base + i * nr + (j + 1) % nr, d = base + (i + 1) * nr + (j + 1) % nr, e = base + (i + 1) * nr + j;
      mb_tri(b, a, d, c);
      mb_tri(b, a, e, d);
    }
  }
  if (closed) {
    for (int end = 0; end < 2; end++) {
      int i = end ? ns - 1 : 0;
      float cy = (s[i].yb + s[i].yt) * 0.5f;
      t3_vec3 nz = v3(0, 0, end ? 1.0f : -1.0f);
      float rgb[3];
      memcpy(rgb, top, sizeof rgb);
      int c = mb_vert(b, v3(s[i].x, cy, s[i].z), nz, bot ? bot : top);
      int r0 = b->nv;
      for (int j = 0; j < nr; j++) mb_vert(b, pos[i * nr + j], nz, rgb);
      for (int j = 0; j < nr; j++) {
        int j2 = (j + 1) % nr;
        if (end) mb_tri(b, c, r0 + j2, r0 + j);
        else mb_tri(b, c, r0 + j, r0 + j2);
      }
    }
  }
  free(pos);
  free(tt);
}

void mb_arch(mb *b, t3_vec3 c, float r, float x0, float x1, float a0, float a1, int seg, const float rgb[3]) {
  float th = 0.03f;
  for (int layer = 0; layer < 2; layer++) {
    float rad = layer ? r : r - th;
    int base = b->nv;
    for (int j = 0; j <= seg; j++) {
      float a = a0 + (a1 - a0) * (float)j / (float)seg;
      t3_vec3 dir = { 0, cosf(a), sinf(a) };
      t3_vec3 nn = layer ? dir : v3mul(dir, -1);
      for (int k = 0; k < 2; k++) mb_vert(b, v3(k ? x1 : x0, c.y + dir.y * rad, c.z + dir.z * rad), nn, rgb);
    }
    for (int j = 0; j < seg; j++) {
      int a = base + j * 2, d = base + j * 2 + 1, e = base + j * 2 + 2, f = base + j * 2 + 3;
      if (layer) mb_tri(b, a, f, d), mb_tri(b, a, e, f);
      else mb_tri(b, a, d, f), mb_tri(b, a, f, e);
    }
  }
  /* the two edges: a flange strip so the shell has a thickness */
  for (int k = 0; k < 2; k++) {
    int base = b->nv;
    t3_vec3 nn = v3(k ? 1.0f : -1.0f, 0, 0);
    for (int j = 0; j <= seg; j++) {
      float a = a0 + (a1 - a0) * (float)j / (float)seg;
      t3_vec3 dir = { 0, cosf(a), sinf(a) };
      mb_vert(b, v3(k ? x1 : x0, c.y + dir.y * r, c.z + dir.z * r), nn, rgb);
      mb_vert(b, v3(k ? x1 : x0, c.y + dir.y * (r - th), c.z + dir.z * (r - th)), nn, rgb);
    }
    for (int j = 0; j < seg; j++) {
      int a = base + j * 2, d = base + j * 2 + 1, e = base + j * 2 + 2, f = base + j * 2 + 3;
      if (k) mb_tri(b, a, e, f), mb_tri(b, a, f, d);
      else mb_tri(b, a, f, e), mb_tri(b, a, d, f);
    }
  }
}

void mb_quad(mb *b, t3_vec3 a, t3_vec3 c, t3_vec3 d, t3_vec3 e, const float rgb[3]) {
  t3_vec3 n = v3norm(v3cross(v3sub(c, a), v3sub(e, a)));
  int i0 = mb_vert(b, a, n, rgb), i1 = mb_vert(b, c, n, rgb), i2 = mb_vert(b, d, n, rgb), i3 = mb_vert(b, e, n, rgb);
  mb_tri(b, i0, i1, i2);
  mb_tri(b, i0, i2, i3);
}

void mb_digit(mb *b, xf x, int digit, float h, const float rgb[3]) {
  static const uint8_t seg[10] = { 63, 6, 91, 79, 102, 109, 125, 7, 127, 111 };
  int m = seg[digit % 10];
  float w = h * 0.55f, t = h * 0.15f, hh = h * 0.5f;
  /* centre, size of each of a b c d e f g */
  float cx[7] = { 0, w * 0.5f, w * 0.5f, 0, -w * 0.5f, -w * 0.5f, 0 };
  float cy[7] = { hh, hh * 0.5f, -hh * 0.5f, -hh, -hh * 0.5f, hh * 0.5f, 0 };
  bool horiz[7] = { true, false, false, true, false, false, true };
  for (int i = 0; i < 7; i++) {
    if (!(m & (1 << i))) continue;
    xf sx = x;
    sx.p = xf_apply(&x, v3(cx[i], cy[i], 0));
    float sw = horiz[i] ? w + t : t, sh = horiz[i] ? t : hh + t * 0.5f;
    mb_rbox(b, sx, sw, sh, t * 0.6f, 0, 0, rgb);
  }
}

/* ── wheels ─────────────────────────────────────────────────────────── */

/* axis x: turn a lathe (axis y) about z onto the x axis */
static xf along_x(float px) { return xf_rot(px, 0, 0, 0, 0, -1.5707963f); }

static void build_wheel(veh_geo *vg, const vehicle_def *vd) {
  const wheel_look *wl = &vg->wl;
  float R = vd->wheel_r, w = wl->width, lug = wl->lug, Rb = R - lug;
  float rr = R * wl->rim_r;
  /* the tyre: a rounded profile, then the tread blocks */
  mb t = { .jit = 0.25f };
  float body[3] = { 0.09f, 0.09f, 0.095f };
  t3_vec2 prof[10] = { { rr * 0.92f, -w * 0.42f }, { Rb * 0.8f, -w * 0.5f }, { Rb * 0.95f, -w * 0.44f }, { Rb, -w * 0.3f }, { Rb, w * 0.3f },
                       { Rb * 0.95f, w * 0.44f }, { Rb * 0.8f, w * 0.5f }, { rr * 0.92f, w * 0.42f } };
  mb_lathe(&t, along_x(0), prof, 8, 28, body);
  int N = wl->lugs;
  float chunk = 6.2831853f * Rb / (float)N;
  for (int i = 0; i < N; i++) {
    float a = 6.2831853f * (float)i / (float)N;
    float side = (i & 1) ? 1.0f : -1.0f;
    float lw = wl->wide_lug ? w * 0.52f : w * 0.44f;
    float cy = cosf(a) * (Rb + lug * 0.45f), cz = sinf(a) * (Rb + lug * 0.45f);
    xf lx = xf_rot(side * w * (wl->wide_lug ? 0.1f : 0.14f), cy, cz, a, 0, 0);
    /* a block elongated across the tread, its leading edge a touch narrower: a knobby zig-zag */
    mb_rbox(&t, lx, lw, lug * 1.1f, chunk * 0.42f, 0, 0, body);
  }
  vg->tyre = mb_done(&t);
  /* the rim, one per side: a barrel, a dished face, spokes and a hub */
  for (int side = 0; side < 2; side++) {
    float sg = side == 0 ? 1.0f : -1.0f; /* outward along x */
    mb m = { .jit = 0.2f };
    float face = sg * w * 0.34f;
    float dark[3] = { 0.16f, 0.16f, 0.17f };
    /* barrel (open ring) */
    t3_vec2 br[4] = { { rr, -w * 0.46f }, { rr, w * 0.46f }, { rr * 0.9f, w * 0.46f }, { rr * 0.9f, -w * 0.46f } };
    mb_lathe(&m, along_x(0), br, 4, 22, wl->rim);
    /* dished face (a dark disc recessed behind the spokes) */
    t3_vec2 dish[3] = { { 0.001f, face * 0.85f }, { rr * 0.55f, face * 0.8f }, { rr * 0.9f, sg * w * 0.45f } };
    if (sg > 0) {
      t3_vec2 t0 = dish[0];
      dish[0] = dish[2], dish[2] = t0;
    }
    mb_lathe(&m, along_x(0), dish, 3, 22, dark);
    for (int i = 0; i < wl->spokes; i++) {
      float a = 6.2831853f * (float)i / (float)wl->spokes;
      float len = rr * 0.86f;
      const float *col = i == 0 ? wl->mark : wl->rim;
      xf sx = xf_at(face + sg * w * 0.04f, cosf(a) * len * 0.5f, sinf(a) * len * 0.5f);
      sx.q = t3_quat_from_axis_angle(v3(1, 0, 0), a);
      mb_rbox(&m, sx, w * 0.1f, len, rr * (i == 0 ? 0.22f : 0.16f), 0.008f, 1, col);
    }
    xf hx = along_x(face + sg * w * 0.1f);
    mb_cyl(&m, hx, rr * 0.2f, rr * 0.24f, w * 0.18f, 10, C_CHROME);
    vg->rim[side] = mb_done(&m);
  }
}

/* ── per-vehicle shared geometry ────────────────────────────────────── */

static veh_geo *VG[NVEH];

static const float JIT[S_COUNT] = { 0.0f, 0.04f, 0.3f, 0.18f, 0, 0, 0 };

const veh_geo *kit_geo(int veh) {
  veh %= NVEH;
  if (VG[veh]) return VG[veh];
  veh_geo *vg = calloc(1, sizeof *vg);
  kit k;
  memset(&k, 0, sizeof k);
  k.vg = vg;
  k.vd = &VEH[veh];
  for (int i = 0; i < S_COUNT; i++) k.m[i].jit = JIT[i];
  vg->driver_scale = 1;
  vg->shield_r = 1.6f;
  vg->shield_c = v3(0, 0.3f, 0);
  vg->scarf_len = 0.16f;
  veh_build(veh, &k);
  for (int i = 0; i < S_COUNT; i++) vg->g[i] = mb_done(&k.m[i]);
  build_wheel(vg, &VEH[veh]);
  VG[veh] = vg;
  return vg;
}

void kit_free(void) {
  for (int v = 0; v < NVEH; v++) {
    if (!VG[v]) continue;
    for (int i = 0; i < S_COUNT; i++)
      if (VG[v]->g[i]) t3_release(VG[v]->g[i]);
    if (VG[v]->tyre) t3_release(VG[v]->tyre);
    for (int i = 0; i < 2; i++)
      if (VG[v]->rim[i]) t3_release(VG[v]->rim[i]);
    free(VG[v]);
    VG[v] = NULL;
  }
}

/* ── a kart's rig ───────────────────────────────────────────────────── */

static t3_material *mat_vc(t3_color c, float rough, float metal) {
  t3_material *m = phys(c, rough, metal);
  m->vertex_colors = true;
  return m;
}

static t3_mesh *add_mesh_to(t3_object *parent, t3_geometry *g, t3_material *m, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = true;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}

static t3_object *pivot_at(t3_object *parent, t3_vec3 p) {
  t3_object *o = t3_group_new();
  t3_object_set_position(o, p.x, p.y, p.z);
  t3_object_add(parent, o);
  t3_release(o);
  return o;
}

static t3_geometry *flame_geo(void) {
  /* a cone from its base at the origin to the tip along +z */
  t3_geometry *g = t3_cone_geometry_new(0.5f, 1.0f, 12, 1, true);
  t3_geometry_translate(g, 0, 0.5f, 0);
  t3_geometry_rotate_x(g, 1.5707963f);
  return g;
}

static void driver_build(kart_rig *r, const veh_geo *vg, t3_object *body) {
  float ds = vg->driver_scale;
  const float *su = vg->suit, *su2 = vg->suit2;
  float skin[3] = { 0.86f, 0.62f, 0.46f };
  float glove[3] = { 0.14f, 0.13f, 0.13f };
  /* torso */
  r->torso = pivot_at(body, v3add(vg->seat, v3(0, 0.1f * ds, -0.02f * ds)));
  mb tm = { .jit = 0.1f };
  mb_rbox(&tm, xf_at(0, 0.2f * ds, 0), 0.34f * ds, 0.44f * ds, 0.22f * ds, 0.07f * ds, 1, su);
  mb_rbox(&tm, xf_at(0, 0.02f * ds, 0), 0.32f * ds, 0.12f * ds, 0.22f * ds, 0.04f * ds, 1, su2); /* belt */
  mb_rbox(&tm, xf_at(0, 0.3f * ds, 0.1f * ds), 0.14f * ds, 0.2f * ds, 0.04f * ds, 0.015f * ds, 1, su2); /* harness chest plate */
  mb_tube(&tm, v3(0.1f * ds, 0.42f * ds, 0.1f * ds), v3(0.04f * ds, 0.04f * ds, 0.1f * ds), 0.018f * ds, 6, glove);
  mb_tube(&tm, v3(-0.1f * ds, 0.42f * ds, 0.1f * ds), v3(-0.04f * ds, 0.04f * ds, 0.1f * ds), 0.018f * ds, 6, glove);
  mb_cyl(&tm, xf_at(0, 0.45f * ds, 0), 0.05f * ds, 0.06f * ds, 0.08f * ds, 8, skin); /* neck */
  t3_geometry *g = mb_done(&tm);
  add_mesh_to(r->torso, g, r->mat[S_DARK], true);
  t3_release(g);
  /* head: face and goggles on the dark material, the helmet in the kart's paint */
  r->head = pivot_at(r->torso, v3(0, 0.56f * ds, 0));
  mb hm = { .jit = 0.05f };
  float strap[3] = { 0.08f, 0.08f, 0.09f }, gframe[3] = { 0.12f, 0.12f, 0.13f };
  mb_ballxyz(&hm, v3(0, -0.02f * ds, 0.015f * ds), v3(0.1f * ds, 0.11f * ds, 0.115f * ds), skin);
  mb_rbox(&hm, xf_at(0, 0.03f * ds, 0.1f * ds), 0.2f * ds, 0.085f * ds, 0.05f * ds, 0.03f * ds, 1, gframe); /* goggle frame */
  mb_ballxyz(&hm, v3(0.052f * ds, 0.03f * ds, 0.125f * ds), v3(0.04f * ds, 0.032f * ds, 0.02f * ds), (float[3]){ 0.95f, 0.75f, 0.2f });
  mb_ballxyz(&hm, v3(-0.052f * ds, 0.03f * ds, 0.125f * ds), v3(0.04f * ds, 0.032f * ds, 0.02f * ds), (float[3]){ 0.95f, 0.75f, 0.2f });
  mb_rbox(&hm, xf_at(0, 0.0f, 0.0f), 0.215f * ds, 0.03f * ds, 0.22f * ds, 0.01f, 0, strap);
  mb_tube(&hm, v3(0.1f * ds, -0.02f * ds, 0.02f * ds), v3(0.04f * ds, -0.12f * ds, 0.1f * ds), 0.008f * ds, 5, strap); /* chin strap */
  mb_tube(&hm, v3(-0.1f * ds, -0.02f * ds, 0.02f * ds), v3(-0.04f * ds, -0.12f * ds, 0.1f * ds), 0.008f * ds, 5, strap);
  g = mb_done(&hm);
  add_mesh_to(r->head, g, r->mat[S_DARK], true);
  t3_release(g);
  mb pm = { 0 };
  float hp[3] = { 1, 1, 1 };
  mb_geo(&pm, t3_sphere_geometry_new_ex(1, 14, 8, 0, 6.2831853f, 0, 1.75f), xf_scaled(xf_at(0, 0.0f, -0.005f * ds), 0.125f * ds, 0.125f * ds, 0.135f * ds), hp);
  if (vg->helmet == 1) {
    for (int i = 0; i < 5; i++) mb_cone(&pm, xf_rot(0, (0.13f - 0.012f * (float)(i % 2)) * ds, (-0.07f + (float)i * 0.035f) * ds, 0, 0, 0), 0.02f * ds, 0.06f * ds, 5, hp);
    mb_rbox(&pm, xf_at(0, 0.115f * ds, 0), 0.025f * ds, 0.04f * ds, 0.2f * ds, 0.01f, 1, hp);
  } else if (vg->helmet == 2) {
    mb_rbox(&pm, xf_at(0.12f * ds, -0.04f * ds, 0), 0.03f * ds, 0.1f * ds, 0.1f * ds, 0.01f, 1, hp);
    mb_rbox(&pm, xf_at(-0.12f * ds, -0.04f * ds, 0), 0.03f * ds, 0.1f * ds, 0.1f * ds, 0.01f, 1, hp);
    mb_rbox(&pm, xf_at(0, 0.075f * ds, 0.12f * ds), 0.2f * ds, 0.02f * ds, 0.08f * ds, 0.008f, 1, hp);
  } else if (vg->helmet == 3) {
    mb_rbox(&pm, xf_at(0, 0.0f, 0.11f * ds), 0.2f * ds, 0.03f * ds, 0.07f * ds, 0.01f, 1, hp);
  } else if (vg->helmet == 4) {
    mb_ball(&pm, v3(0, 0.135f * ds, 0), 0.025f * ds, hp);
  }
  g = mb_done(&pm);
  add_mesh_to(r->head, g, r->mat[S_PAINT], true);
  t3_release(g);
  /* a prop on the beanie: two blades on a pivot that spins */
  if (vg->helmet == 4) {
    r->prop = pivot_at(r->head, v3(0, 0.17f * ds, 0));
    mb bl = { 0 };
    float bc[3] = { 0.95f, 0.9f, 0.3f };
    mb_rbox(&bl, xf_at(0.1f * ds, 0, 0), 0.2f * ds, 0.008f, 0.04f * ds, 0.003f, 1, bc);
    mb_rbox(&bl, xf_at(-0.1f * ds, 0, 0), 0.2f * ds, 0.008f, 0.04f * ds, 0.003f, 1, bc);
    mb_cyl(&bl, xf_at(0, -0.02f * ds, 0), 0.012f, 0.012f, 0.04f * ds, 6, C_CHROME);
    g = mb_done(&bl);
    add_mesh_to(r->prop, g, r->mat[S_DARK], false);
    t3_release(g);
  }
  /* the scarf trailing back from the neck */
  {
    t3_object *par = r->torso;
    t3_vec3 at = v3(0, 0.44f * ds, -0.1f * ds);
    float sl = vg->scarf_len * ds;
    for (int i = 0; i < 4; i++) {
      r->scarf[i] = pivot_at(par, at);
      mb sm = { 0 };
      mb_rbox(&sm, xf_at(0, 0, -sl * 0.5f), (0.11f - 0.012f * (float)i) * ds, 0.012f, sl, 0.004f, 1, vg->suit2);
      g = mb_done(&sm);
      add_mesh_to(r->scarf[i], g, r->mat[S_DARK], false);
      t3_release(g);
      par = r->scarf[i];
      at = v3(0, 0, -sl * 0.95f);
    }
  }
  /* arms: an upper arm and a forearm each (stretched by place_tube each frame) and a glove */
  mb ar = { .jit = 0.08f };
  mb_tube(&ar, v3(0, -0.5f, 0), v3(0, 0.5f, 0), 0.036f * ds, 8, su);
  t3_geometry *ag = mb_done(&ar);
  mb fa = { .jit = 0.08f };
  mb_tube(&fa, v3(0, -0.5f, 0), v3(0, 0.5f, 0), 0.031f * ds, 8, su2);
  t3_geometry *fg = mb_done(&fa);
  mb hd = { 0 };
  mb_ball(&hd, v3(0, 0, 0), 0.04f * ds, glove);
  t3_geometry *hg = mb_done(&hd);
  for (int s = 0; s < 2; s++) {
    r->arm_up[s] = &t3_mesh_new(ag, r->mat[S_DARK])->base;
    r->arm_lo[s] = &t3_mesh_new(fg, r->mat[S_DARK])->base;
    r->hand[s] = &t3_mesh_new(hg, r->mat[S_DARK])->base;
    t3_object *three[3] = { r->arm_up[s], r->arm_lo[s], r->hand[s] };
    for (int i = 0; i < 3; i++) {
      three[i]->cast_shadow = true;
      t3_object_add(body, three[i]);
      t3_release(three[i]);
    }
  }
  t3_release(ag), t3_release(fg), t3_release(hg);
  /* the steering wheel */
  r->steer_wheel = pivot_at(body, vg->wheel_hub);
  t3_object_set_rotation(r->steer_wheel, vg->wheel_tilt, 0, 0);
  mb sw = { 0 };
  float wc[3] = { 0.12f, 0.12f, 0.13f };
  mb_torus(&sw, xf_at(0, 0, 0), vg->wheel_r, 0.016f, 6, 18, wc);
  mb_tube(&sw, v3(-vg->wheel_r, 0, 0), v3(vg->wheel_r, 0, 0), 0.01f, 5, C_STEEL);
  mb_tube(&sw, v3(0, 0, 0), v3(0, -vg->wheel_r, 0), 0.01f, 5, C_STEEL);
  mb_cyl(&sw, xf_rot(0, 0, 0.012f, 1.5707963f, 0, 0), 0.03f, 0.03f, 0.02f, 8, C_BRASS);
  g = mb_done(&sw);
  add_mesh_to(r->steer_wheel, g, r->mat[S_METAL], true);
  t3_release(g);
}

void rig_build(kart_rig *r, int veh, int colour, int number) {
  memset(r, 0, sizeof *r);
  veh %= NVEH;
  r->veh = veh, r->colour = colour & 7, r->number = number;
  const veh_geo *vg = kit_geo(veh);
  r->vg = vg;
  const float *c = KART_RGB[colour & 7];
  /* the second paint: near-black on a light kart, cream on a dark or saturated one */
  float lum = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
  t3_color paint2 = lum > 0.6f ? lin(0.1f, 0.1f, 0.12f) : lin(0.93f, 0.9f, 0.8f);
  r->mat[S_PAINT] = mat_vc(lin(c[0], c[1], c[2]), 0.3f, 0.1f);
  r->mat[S_PAINT]->clearcoat = 0.8f;
  r->mat[S_PAINT]->clearcoat_roughness = 0.15f;
  r->mat[S_PAINT2] = mat_vc(paint2, 0.4f, 0.1f);
  r->mat[S_METAL] = mat_vc(lin(1, 1, 1), 0.48f, 0.75f);
  r->mat[S_DARK] = mat_vc(lin(1, 1, 1), 0.8f, 0.05f);
  r->mat[S_LAMP] = phys(lin(1, 0.95f, 0.8f), 0.2f, 0);
  r->mat[S_LAMP]->emissive = lin(1, 0.9f, 0.6f);
  r->mat[S_LAMP]->emissive_intensity = 2.5f;
  r->mat[S_TAIL] = phys(lin(0.8f, 0.1f, 0.08f), 0.3f, 0);
  r->mat[S_TAIL]->emissive = lin(1, 0.1f, 0.06f);
  r->mat[S_TAIL]->emissive_intensity = 1.2f;
  r->mat[S_GLASS] = phys(lin(0.06f, 0.1f, 0.14f), 0.06f, 0.2f);
  r->mat[S_GLASS]->clearcoat = 1;
  r->mat[S_PAINT]->emissive = lin(0.4f, 0.7f, 1);
  r->mat[S_PAINT]->emissive_intensity = 0;
  r->root = t3_group_new();
  r->body = pivot_at(r->root, v3(0, 0, 0));
  for (int i = 0; i < S_COUNT; i++) {
    if (!vg->g[i]) continue;
    t3_mesh *me = add_mesh_to(r->body, vg->g[i], r->mat[i], i != S_LAMP && i != S_TAIL);
    if (i == S_GLASS) me->base.cast_shadow = false;
  }
  /* wheels: pivots under the root, the tyre and the rim of its side in each */
  for (int w = 0; w < 4; w++) {
    r->wheel[w] = pivot_at(r->root, v3(0, 0, 0));
    add_mesh_to(r->wheel[w], vg->tyre, r->mat[S_DARK], true);
    add_mesh_to(r->wheel[w], vg->rim[(w & 1) ? 1 : 0], r->mat[S_METAL], true);
  }
  /* the number plates: a tail plate and a roundel on each flank */
  {
    mb pl = { .jit = 0.1f };
    float white[3] = { 0.93f, 0.92f, 0.86f }, ink[3] = { 0.06f, 0.06f, 0.07f };
    int digit = (number % 10 + 10) % 10;
    float pw = vg->plate_back_w;
    if (pw > 0) {
      xf px = xf_rot(vg->plate_back.p.x, vg->plate_back.p.y, vg->plate_back.p.z, 0, 3.14159265f, 0);
      px.q = t3_quat_from_axis_angle(v3(0, 1, 0), 3.14159265f);
      mb_rbox(&pl, px, pw, pw * 0.62f, 0.03f, 0.012f, 1, white);
      xf dx = px;
      dx.p = xf_apply(&px, v3(0, 0, 0.02f));
      mb_digit(&pl, dx, digit, pw * 0.5f, ink);
    }
    float sr = vg->plate_side_r;
    if (sr > 0) {
      for (int s = 0; s < 2; s++) {
        float sg = s == 0 ? 1.0f : -1.0f;
        xf cx = xf_rot(sg * vg->plate_side.p.x, vg->plate_side.p.y, vg->plate_side.p.z, 0, 0, sg * -1.5707963f);
        mb_cyl(&pl, cx, sr, sr, 0.03f, 16, white);
        xf dx = xf_rot(sg * (vg->plate_side.p.x + 0.02f), vg->plate_side.p.y, vg->plate_side.p.z, 0, sg * 1.5707963f, 0);
        mb_digit(&pl, dx, digit, sr * 1.15f, ink);
      }
    }
    t3_geometry *g = mb_done(&pl);
    if (g) {
      add_mesh_to(r->body, g, r->mat[S_DARK], false);
      t3_release(g);
    }
  }
  driver_build(r, vg, r->body);
  /* Pogo's spring legs: a coil from the body to each hub, stretched each frame */
  if (vg->struts) {
    mb sp = { .jit = 0.2f };
    float coil[3] = { 0.7f, 0.72f, 0.75f };
    int turns = 7;
    t3_vec3 prev = v3(0.07f, -0.5f, 0);
    for (int i = 1; i <= turns * 8; i++) {
      float u = (float)i / (float)(turns * 8), a = u * (float)turns * 6.2831853f;
      t3_vec3 q = v3(cosf(a) * 0.07f, -0.5f + u, sinf(a) * 0.07f);
      mb_tube(&sp, prev, q, 0.014f, 5, coil);
      prev = q;
    }
    mb_tube(&sp, v3(0, -0.5f, 0), v3(0, 0.5f, 0), 0.02f, 6, C_CHROME);
    t3_geometry *g = mb_done(&sp);
    for (int i = 0; i < 4; i++) {
      t3_mesh *me = t3_mesh_new(g, r->mat[S_METAL]);
      me->base.cast_shadow = true;
      t3_object_add(r->root, me);
      r->strut[i] = &me->base;
      t3_release(me);
    }
    t3_release(g);
  }
  /* the antenna whip and its pennant */
  if (vg->antenna.dir.y > 0) {
    r->antenna = pivot_at(r->body, vg->antenna.p);
    mb an = { 0 };
    float len = vg->antenna.dir.y;
    mb_tube_cap(&an, v3(0, 0, 0), v3(0, len, 0), 0.012f, 0.004f, 5, C_STEEL);
    mb_quad(&an, v3(0, len, 0), v3(0, len - 0.12f, 0), v3(0, len - 0.12f, -0.2f), v3(0, len, -0.1f), C_RED);
    mb_quad(&an, v3(0, len, 0), v3(0, len, -0.1f), v3(0, len - 0.12f, -0.2f), v3(0, len - 0.12f, 0), C_RED);
    t3_geometry *g = mb_done(&an);
    add_mesh_to(r->antenna, g, r->mat[S_DARK], false);
    t3_release(g);
  }
  /* exhaust flames: two nested cones each (additive), off until the boost lights them */
  r->flame_mat[0] = t3_mesh_basic_material_new(0xffffff);
  r->flame_mat[0]->color = lin(1.0f, 0.42f, 0.08f);
  r->flame_mat[1] = t3_mesh_basic_material_new(0xffffff);
  r->flame_mat[1]->color = lin(1.0f, 0.9f, 0.55f);
  for (int i = 0; i < 2; i++) {
    t3_material *m = r->flame_mat[i];
    m->transparent = true, m->depth_write = false, m->blending = T3_ADDITIVE_BLENDING, m->opacity = i ? 0.9f : 0.6f, m->fog = false;
    m->side = T3_DOUBLE_SIDE;
  }
  t3_geometry *fg = flame_geo();
  for (int f = 0; f < vg->nflame; f++) {
    t3_object *pv = pivot_at(r->body, vg->flame[f].p);
    t3_object_set_quaternion(pv, quat_from_to(v3(0, 0, 1), vg->flame[f].dir));
    for (int l = 0; l < 2; l++) {
      t3_mesh *me = t3_mesh_new(fg, r->flame_mat[l]);
      me->base.visible = false;
      me->base.frustum_culled = false;
      t3_object_add(pv, me);
      r->flame[f][l] = &me->base;
      t3_release(me);
    }
  }
  t3_release(fg);
  /* the shield bubble */
  r->shield_mat = t3_mesh_basic_material_new(0xffffff);
  r->shield_mat->color = lin(0.3f, 0.8f, 1.0f);
  r->shield_mat->transparent = true, r->shield_mat->depth_write = false, r->shield_mat->blending = T3_ADDITIVE_BLENDING;
  r->shield_mat->opacity = 0.22f, r->shield_mat->fog = false;
  t3_mesh *sh = t3_mesh_new(t3_sphere_geometry_new(vg->shield_r, 24, 16), r->shield_mat);
  sh->base.visible = false;
  sh->base.frustum_culled = false;
  t3_object_set_position(sh, vg->shield_c.x, vg->shield_c.y, vg->shield_c.z);
  t3_object_add(r->root, sh);
  r->shield = &sh->base;
  t3_release(sh);
}

void rig_free(kart_rig *r) {
  for (int i = 0; i < S_COUNT; i++)
    if (r->mat[i]) t3_release(r->mat[i]);
  for (int i = 0; i < 2; i++)
    if (r->flame_mat[i]) t3_release(r->flame_mat[i]);
  if (r->shield_mat) t3_release(r->shield_mat);
  memset(r, 0, sizeof *r);
}

/* world.h's older entry: a kart as a plain model (the rig with a placeholder number) */
void kart_build(kart_model *m, int veh, int colour) {
  kart_rig r;
  rig_build(&r, veh, colour, veh + 1);
  memset(m, 0, sizeof *m);
  m->veh = veh, m->colour = colour;
  m->root = r.root;
  m->body = r.body;
  for (int i = 0; i < 4; i++) m->wheel[i] = r.wheel[i];
  rig_free(&r); /* the objects keep what they use */
}
