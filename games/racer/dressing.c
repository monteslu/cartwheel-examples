/* racer's track dressing: what stands around, over and under the road: the start gantry and grid, checkpoint arches,
 * supports for raised road, roadside lights, guard rails, jump and corner markings, signs and banner hoops.
 *
 * Everything is built into world-space merged meshes cut into 60 m cells (pmc), or instanced from a prop_set, so a
 * view culls it by cell and the whole dressing of a map is a few dozen draws. Local frames: a road frame has x to the
 * right, y up and z forward, so a part is designed once and set on any sample with pm_basis. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/base.h"
#include "cartwheel/math.h"
#include "props.h"

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V(a.x * k, a.y * k, a.z * k); }
static float vlen(t3_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static t3_vec3 vnorm(t3_vec3 a) {
  float l = vlen(a);
  return l > 1e-9f ? vmul(a, 1.0f / l) : V(0, 1, 0);
}
static pcol dk(pcol c, float k) { return pc_mul(c, k); }

/* Roadside dressing keeps off a fork: the side of a sample where the other road is right there (TF_FORK_*), and any spot
 * on or next to a branch (a shortcut) when dressing the main road. */
static bool fork_side(const tsample *a, int side) { return (a->flags & (side < 0 ? TF_FORK_L : TF_FORK_R)) != 0; }
static bool keep_clear(const track *t, const tsample *a, int side, t3_vec3 p, float room) {
  if (fork_side(a, side)) return true;
  return t->nbranch > 0 && branch_gap(t, p.x, p.z) < room;
}

dress_style dress_defaults(void) {
  dress_style d = { 0 };
  d.main = rgb(0.55f, 0.57f, 0.62f);
  d.accent = rgb(0.95f, 0.75f, 0.12f);
  d.lamp = rgb(1.0f, 0.9f, 0.65f);
  d.seed = 3;
  return d;
}
static dress_style styled(const dress_style *s) { return s ? *s : dress_defaults(); }

/* put the builder of the cell holding p into the local frame of a sample (x right, y up, z forward) */
static pm *frame_at(pmc *c, const tsample *a, float lat, float lift) {
  t3_vec3 p = vadd(vadd(a->p, vmul(a->r, lat)), vmul(a->u, lift));
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_basis(b, p, a->r, a->u, a->f);
  return b;
}
/* the same frame but standing upright: y is world up, x the road's right flattened, z forward flattened */
static pm *upright_at(pmc *c, t3_vec3 p, const tsample *a) {
  pm *b = pmc_at(c, p);
  pm_push(b);
  t3_vec3 r = vnorm(V(a->r.x, 0, a->r.z)), f = vnorm(V(a->f.x, 0, a->f.z));
  pm_basis(b, p, r, V(0, 1, 0), f);
  return b;
}
/* a flat painted rectangle on y = 0 of the current frame, seen from above */
static void paint(pm *b, float x0, float x1, float z0, float z1, float y, pcol c) {
  pm_quad(b, V(x0, y, z1), V(x1, y, z1), V(x1, y, z0), V(x0, y, z0), c);
}

/* ── the start line ──────────────────────────────────────────────────────────────────────────────── */

static void chequer(pm *b, float x0, float x1, float y0, float y1, float z, float cell, bool front, pcol a, pcol c) {
  int nx = (int)((x1 - x0) / cell + 0.5f), ny = (int)((y1 - y0) / cell + 0.5f);
  if (nx < 1) nx = 1;
  if (ny < 1) ny = 1;
  float dx = (x1 - x0) / (float)nx, dy = (y1 - y0) / (float)ny;
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      pcol col = ((i + j) & 1) ? a : c;
      float xa = x0 + dx * (float)i, xb = xa + dx, ya = y0 + dy * (float)j, yb = ya + dy;
      if (front) pm_quad(b, V(xa, ya, z), V(xb, ya, z), V(xb, yb, z), V(xa, yb, z), col);
      else pm_quad(b, V(xb, ya, z), V(xa, ya, z), V(xa, yb, z), V(xb, yb, z), col);
    }
}

void dress_start_line(cw_node parent, const track *t, const dress_style *in) {
  dress_style st = styled(in);
  int idx = t->nchecks > 0 ? t->check_at[0] : 0;
  const tsample *a = &t->s[idx];
  pmc *c = pmc_new(80, st.seed);
  float hw = a->hw, px = hw + 1.8f, H = 8.4f;
  pcol steel = st.main, accent = st.accent;
  pcol black = rgb(0.04f, 0.04f, 0.05f), white = rgb(0.95f, 0.95f, 0.92f);
  pm *b = frame_at(c, a, 0, 0);
  /* two legs, braced, with a footing plate */
  pm_layer(b, PL_METAL);
  for (int k = -1; k <= 1; k += 2) {
    pm_push(b);
    pm_move(b, k * px, -1.2f, 0);
    pm_box(b, 1.1f, H + 1.2f, 1.1f, dk(steel, 0.7f), steel);
    pm_box(b, 1.8f, 0.5f, 1.8f, dk(steel, 0.5f), dk(steel, 0.8f));
    pm_pop(b);
  }
  /* the truss beam: two chords, uprights and diagonals */
  float yb = H - 0.3f, yt = H + 1.1f;
  pm_beam(b, V(-px, yb, 0.4f), V(px, yb, 0.4f), 0.22f, steel);
  pm_beam(b, V(-px, yb, -0.4f), V(px, yb, -0.4f), 0.22f, steel);
  pm_beam(b, V(-px, yt, 0), V(px, yt, 0), 0.26f, steel);
  int bays = (int)(2 * px / 1.6f);
  for (int i = 0; i <= bays; i++) {
    float x = -px + 2 * px * (float)i / (float)bays;
    pm_beam(b, V(x, yb, 0.4f), V(x, yt, 0), 0.12f, dk(steel, 0.85f));
    pm_beam(b, V(x, yb, -0.4f), V(x, yt, 0), 0.12f, dk(steel, 0.85f));
    if (i < bays) {
      float x2 = -px + 2 * px * (float)(i + 1) / (float)bays;
      pm_beam(b, V(x, yb, 0.4f), V(x2, yt, 0), 0.1f, dk(steel, 0.9f));
      pm_beam(b, V(x, yb, -0.4f), V(x2, yt, 0), 0.1f, dk(steel, 0.9f));
    }
  }
  pm_layer(b, PL_MAIN);
  /* the chequered banner hangs under the beam, a face each way, with a bold stripe above it */
  chequer(b, -hw, hw, H - 2.9f, H - 0.4f, 0.0f, 0.5f, false, black, white);
  chequer(b, -hw, hw, H - 2.9f, H - 0.4f, 0.0f, 0.5f, true, black, white);
  pm_quad(b, V(-px, yt + 0.25f, 0.25f), V(px, yt + 0.25f, 0.25f), V(px, yt + 1.6f, 0.25f), V(-px, yt + 1.6f, 0.25f), accent);
  pm_quad(b, V(px, yt + 0.25f, -0.25f), V(-px, yt + 0.25f, -0.25f), V(-px, yt + 1.6f, -0.25f), V(px, yt + 1.6f, -0.25f), accent);
  for (int i = 0; i < 6; i++) { /* bold stripes on the sign */
    float x0 = -px + (2 * px) * ((float)i + 0.3f) / 6.0f, x1 = x0 + (2 * px) / 6.0f * 0.4f;
    pm_quad(b, V(x0, yt + 0.4f, 0.26f), V(x1, yt + 0.4f, 0.26f), V(x1, yt + 1.45f, 0.26f), V(x0, yt + 1.45f, 0.26f), black);
    pm_quad(b, V(x1, yt + 0.4f, -0.26f), V(x0, yt + 0.4f, -0.26f), V(x0, yt + 1.45f, -0.26f), V(x1, yt + 1.45f, -0.26f), black);
  }
  pm_layer(b, PL_GLOW);
  /* a row of lamps under the beam and a lit strip along the sign's top */
  int lamps = (int)(2 * px / 2.4f);
  for (int i = 0; i < lamps; i++) {
    float x = -px + 1.2f + (2 * px - 2.4f) * (float)i / (float)(lamps - 1 > 0 ? lamps - 1 : 1);
    pm_quad(b, V(x - 0.35f, yb - 0.13f, 0.5f), V(x + 0.35f, yb - 0.13f, 0.5f), V(x + 0.35f, yb - 0.13f, -0.5f), V(x - 0.35f, yb - 0.13f, -0.5f), glow(st.lamp, 6.0f));
  }
  pm_quad(b, V(-px, yt + 1.62f, 0.3f), V(px, yt + 1.62f, 0.3f), V(px, yt + 1.62f, -0.3f), V(-px, yt + 1.62f, -0.3f), glow(accent, 3.0f));
  pm_layer(b, PL_MAIN);
  pm_pop(b);
  /* the painted chequered line across the road and the grid slots behind it */
  b = frame_at(c, a, 0, 0);
  pm_layer(b, PL_MAIN);
  int nc = (int)(2 * hw / 0.5f);
  for (int i = 0; i < nc; i++)
    for (int j = 0; j < 2; j++) {
      float x0 = -hw + 0.5f * (float)i, z0 = -0.5f + 0.5f * (float)j;
      paint(b, x0, x0 + 0.5f, z0, z0 + 0.5f, 0.035f, ((i + j) & 1) ? black : white);
    }
  pm_pop(b);
  if (t->circuit) {
    float lat = fminf(hw * 0.42f, 3.4f);
    for (int k = 0; k < 8; k++) {
      int row = k / 2, col = k & 1;
      float back = 8.0f + (float)row * 8.0f + (col ? 4.0f : 0.0f);
      float ss = fmodf(a->s - back + t->length, t->length);
      tsample sl = track_at(t, ss);
      if (sl.flags & (TF_GAP | TF_LOOP)) continue;
      float x = (col ? 1.0f : -1.0f) * lat;
      b = frame_at(c, &sl, 0, 0);
      float w = 1.25f, l = 2.3f, lw = 0.12f;
      pcol line = rgb(0.92f, 0.92f, 0.88f);
      paint(b, x - w, x + w, -l, -l + lw, 0.03f, line);
      paint(b, x - w, x - w + lw, -l, l * 0.35f, 0.03f, line);
      paint(b, x + w - lw, x + w, -l, l * 0.35f, 0.03f, line);
      paint(b, x - w, x - w + 0.7f, l * 0.35f - lw, l * 0.35f, 0.03f, line);
      paint(b, x + w - 0.7f, x + w, l * 0.35f - lw, l * 0.35f, 0.03f, line);
      pm_pop(b);
    }
  }
  pmc_finish(c, parent, "StartLine", true, -1);
}

/* ── checkpoints ─────────────────────────────────────────────────────────────────────────────────── */

/* an arch over the road: posts, an elliptical hoop of tube with a glowing inner ring, pennants */
static void arch_at(pmc *c, const tsample *a, float height, const dress_style *st, bool pennants) {
  float hw = a->hw, rx = hw + 1.4f;
  pm *b = frame_at(c, a, 0, 0);
  pm_layer(b, PL_METAL);
  pcol steel = st->main;
  const int n = 18;
  t3_vec3 prev = V(-rx, -0.8f, 0);
  pm_beam(b, prev, V(-rx, 0.2f, 0), 0.5f, dk(steel, 0.7f));
  pm_beam(b, V(rx, -0.8f, 0), V(rx, 0.2f, 0), 0.5f, dk(steel, 0.7f));
  prev = V(-rx, 0.2f, 0);
  for (int i = 1; i <= n; i++) {
    float an = CW_PI * (1.0f - (float)i / (float)n);
    t3_vec3 cur = V(cosf(an) * rx, 0.2f + sinf(an) * height, 0);
    pm_rod(b, prev, cur, 0.26f, 6, steel);
    prev = cur;
  }
  pm_layer(b, PL_GLOW);
  prev = V(-rx + 0.36f, 0.2f, 0.28f);
  for (int i = 1; i <= n; i++) {
    float an = CW_PI * (1.0f - (float)i / (float)n);
    t3_vec3 cur = V(cosf(an) * (rx - 0.36f), 0.2f + sinf(an) * (height - 0.36f), 0.28f);
    pm_rod(b, prev, cur, 0.1f, 4, glow(st->accent, 5.0f));
    pm_rod(b, V(prev.x, prev.y, -prev.z), V(cur.x, cur.y, -cur.z), 0.1f, 4, glow(st->accent, 5.0f));
    prev = cur;
  }
  if (pennants) {
    pm_layer(b, PL_MAIN);
    int np = (int)(2 * rx / 1.2f);
    for (int i = 0; i < np; i++) {
      float x = -rx * 0.9f + 1.8f * rx * (float)i / (float)(np - 1);
      float yy = 0.2f + height * sqrtf(fmaxf(0, 1 - (x / rx) * (x / rx))) - 0.3f;
      pcol col = (i & 1) ? st->accent : rgb(0.95f, 0.95f, 0.92f);
      pm_tri(b, V(x - 0.4f, yy, 0), V(x + 0.4f, yy, 0), V(x, yy - 1.5f, 0), col);
      pm_tri(b, V(x + 0.4f, yy, 0), V(x - 0.4f, yy, 0), V(x, yy - 1.5f, 0), col);
    }
  }
  pm_pop(b);
}

void dress_checkpoints(cw_node parent, const track *t, const dress_style *in) {
  dress_style st = styled(in);
  pmc *c = pmc_new(80, st.seed + 1);
  for (int i = 1; i < t->nchecks; i++) {
    const tsample *a = &t->s[t->check_at[i]];
    if (a->flags & (TF_TUNNEL | TF_GAP)) continue;
    arch_at(c, a, fminf(7.0f, 4.5f + a->hw * 0.3f), &st, true);
  }
  pmc_finish(c, parent, "Checkpoints", true, -1);
}

/* ── supports ────────────────────────────────────────────────────────────────────────────────────── */

static bool support_ok(const track *t, int i) {
  const tsample *a = &t->s[i];
  return !(a->flags & (TF_GAP | TF_LOOP | TF_TUNNEL));
}
/* is another part of the course directly beneath this point, so a column would pierce it */
static bool crossing(const track *t, int i, float x, float z) {
  const tsample *a = &t->s[i];
  for (int j = 0; j < t->n; j += 2) {
    const tsample *o = &t->s[j];
    float ds = fabsf(o->s - a->s);
    if (t->circuit) ds = fminf(ds, t->length - ds);
    if (ds < 30 || o->p.y > a->p.y - 0.5f) continue;
    float dx = o->p.x - x, dz = o->p.z - z;
    float lim = o->hw + 4.0f;
    if (dx * dx + dz * dz < lim * lim) return true;
  }
  return false;
}

static void bent(pmc *c, const track *t, int i, support_style style, float gy, const dress_style *st, uint32_t seed) {
  const tsample *a = &t->s[i];
  float hw = a->hw;
  float top = a->p.y - 0.7f;
  float H = top - gy;
  t3_vec3 base = V(a->p.x, gy, a->p.z);
  pm *w = upright_at(c, base, a); /* y up from the ground */
  pcol steel = st->main, accent = st->accent;
  (void)seed;
  switch (style) {
  case SUP_CONCRETE: {
    pcol con = rgb(0.62f, 0.62f, 0.6f);
    pm_layer(w, PL_MAIN);
    pm_box(w, 3.4f, 0.6f, 3.4f, dk(con, 0.5f), dk(con, 0.8f));
    pm_move(w, 0, 0.6f, 0);
    pm_cyl_ex(w, 1.25f, 0.95f, H - 0.6f, 10, dk(con, 0.65f), con, false, false);
    break;
  }
  case SUP_TRUSS: {
    pm_layer(w, PL_METAL);
    float lx = hw * 0.55f;
    for (int k = -1; k <= 1; k += 2) {
      float top_x = k * lx, foot = k * (lx + 1.6f);
      pm_beam(w, V(foot - 0.9f, 0, -0.9f), V(top_x - 0.5f, H, -0.5f), 0.28f, steel);
      pm_beam(w, V(foot + 0.9f, 0, -0.9f), V(top_x + 0.5f, H, -0.5f), 0.28f, steel);
      pm_beam(w, V(foot - 0.9f, 0, 0.9f), V(top_x - 0.5f, H, 0.5f), 0.28f, steel);
      pm_beam(w, V(foot + 0.9f, 0, 0.9f), V(top_x + 0.5f, H, 0.5f), 0.28f, steel);
      int rungs = (int)(H / 4.5f) + 1;
      for (int r = 0; r < rungs; r++) {
        float t0 = (float)r / (float)rungs, t1 = (float)(r + 1) / (float)rungs;
        float x0 = foot + (top_x - foot) * t0, x1 = foot + (top_x - foot) * t1, h0 = H * t0, h1 = H * t1;
        float w0 = 0.9f + (0.5f - 0.9f) * t0, w1 = 0.9f + (0.5f - 0.9f) * t1;
        pm_beam(w, V(x0 - w0, h0, -w0), V(x1 + w1, h1, -w1), 0.14f, dk(steel, 0.9f));
        pm_beam(w, V(x0 + w0, h0, -w0), V(x1 - w1, h1, -w1), 0.14f, dk(steel, 0.9f));
        pm_beam(w, V(x0 - w0, h0, w0), V(x1 + w1, h1, w1), 0.14f, dk(steel, 0.9f));
        pm_beam(w, V(x1 - w1, h1, -w1), V(x1 + w1, h1, -w1), 0.14f, dk(steel, 0.9f));
      }
      pm_push(w);
      pm_move(w, foot, -0.0f, 0);
      pm_box(w, 3.2f, 0.5f, 3.0f, rgb(0.3f, 0.3f, 0.3f), rgb(0.5f, 0.5f, 0.5f));
      pm_pop(w);
    }
    break;
  }
  case SUP_TRESTLE: {
    pcol wood0 = rgb(0.22f, 0.14f, 0.08f), wood1 = rgb(0.5f, 0.36f, 0.22f);
    pm_layer(w, PL_MAIN);
    float lx = hw * 0.8f;
    for (int k = -1; k <= 1; k += 2)
      for (int d = -1; d <= 1; d += 2) {
        pm_limb(w, V(k * (lx + 0.9f), 0, d * 1.2f), V(k * lx, H, d * 0.7f), 0.3f, 0.26f, 6, wood0, wood1);
      }
    int lv = (int)(H / 3.5f);
    for (int l = 1; l <= lv; l++) {
      float tt = (float)l / (float)(lv + 1), y = H * tt, xo = lx + 0.9f - 0.9f * tt, zo = 1.2f - 0.5f * tt;
      for (int k = -1; k <= 1; k += 2) pm_beam(w, V(k * xo, y, -zo), V(k * xo, y, zo), 0.18f, wood0);
      pm_beam(w, V(-xo, y, -zo), V(xo, y, -zo), 0.16f, wood0);
      pm_beam(w, V(-xo, y, zo), V(xo, y, zo), 0.16f, wood0);
      float tt2 = (float)(l - 1) / (float)(lv + 1) , y2 = H * tt2, xo2 = lx + 0.9f - 0.9f * tt2;
      pm_beam(w, V(-xo2, y2, zo), V(xo, y, zo), 0.14f, wood1);
      pm_beam(w, V(xo2, y2, zo), V(-xo, y, zo), 0.14f, wood1);
    }
    break;
  }
  case SUP_ICE: {
    pcol i0 = rgb(0.35f, 0.62f, 0.85f), i1 = rgb(0.92f, 0.98f, 1.0f);
    pm_layer(w, PL_MAIN);
    pm_jitter(w, 0.08f);
    pm_cyl_ex(w, 2.0f, 1.2f, H, 6, i0, i1, false, false);
    for (int k = -1; k <= 1; k += 2) {
      pm_push(w);
      pm_move(w, k * 1.7f, 0, 0.3f * (float)k);
      pm_rotz(w, -k * 0.16f);
      pm_cyl_ex(w, 1.1f, 0.001f, H * 0.7f, 5, i0, i1, false, false);
      pm_pop(w);
    }
    pm_jitter(w, 0);
    break;
  }
  case SUP_STRUTS: {
    pm_layer(w, PL_METAL);
    float reach = hw * 0.9f, len = fminf(H, 12.0f);
    for (int k = -1; k <= 1; k += 2) {
      pm_limb(w, V(0, H - 0.2f, 0), V(k * reach, H - len, 0), 0.4f, 0.26f, 6, dk(steel, 0.8f), steel);
      pm_push(w);
      pm_move(w, k * reach, H - len - 0.3f, 0);
      pm_box(w, 1.6f, 0.4f, 1.6f, dk(steel, 0.6f), dk(steel, 0.9f));
      pm_pop(w);
    }
    pm_push(w);
    pm_move(w, 0, H - 0.9f, 0);
    pm_box(w, 1.4f, 0.8f, 1.4f, dk(steel, 0.6f), steel);
    pm_pop(w);
    pm_layer(w, PL_GLOW);
    pm_push(w);
    pm_move(w, 0, H - 0.5f, 0);
    pm_ball(w, 0.35f, 0.35f, 0.35f, 0, 0, glow(accent, 2.5f), glow(accent, 3.5f));
    pm_pop(w);
    break;
  }
  default: break;
  }
  pm_pop(w);
  /* the cap beam follows the road's own bank, so it always sits flush under the surface */
  if (style != SUP_ICE && style != SUP_STRUTS) {
    pm *r = frame_at(c, a, 0, -0.7f);
    pcol col = style == SUP_CONCRETE ? rgb(0.66f, 0.66f, 0.64f) : style == SUP_TRESTLE ? rgb(0.4f, 0.28f, 0.17f) : steel;
    pm_layer(r, style == SUP_TRUSS ? PL_METAL : PL_MAIN);
    float cw = style == SUP_CONCRETE ? 1.0f : 0.6f;
    float wd = hw * (style == SUP_CONCRETE ? 0.95f : style == SUP_TRESTLE ? 1.0f : 0.95f);
    pm_push(r);
    pm_move(r, 0, -cw + 0.0f, 0);
    pm_box(r, 2 * wd + 2.0f, cw, style == SUP_CONCRETE ? 2.6f : 1.8f, dk(col, 0.7f), col);
    pm_pop(r);
    pm_pop(r);
  } else if (style == SUP_ICE) {
    pm *r = frame_at(c, a, 0, -0.7f);
    pm_layer(r, PL_MAIN);
    pm_push(r);
    pm_move(r, 0, -0.7f, 0);
    pm_box(r, 2 * hw * 0.9f, 0.7f, 2.4f, rgb(0.5f, 0.75f, 0.92f), rgb(0.9f, 0.97f, 1.0f));
    pm_pop(r);
    pm_pop(r);
  }
}

void deco_supports(cw_node parent, const track *t, support_style style, ground_fn ground, void *ud, float base_y, float min_clear, const dress_style *in) {
  dress_style st = styled(in);
  if (min_clear <= 0) min_clear = 2;
  pmc *c = pmc_new(70, st.seed + 2);
  static const float spacing[SUP_STYLES] = { 24, 18, 9, 20, 22, 14 }; /* truss, concrete, trestle, ice, arches, struts */
  float sp = spacing[style];
  int step = (int)(sp / STEP_M);
  int count = 0;
  if (style == SUP_ARCHES) {
    /* piers every span, and a masonry wall between them with an arch cut in it */
    pcol stone0 = rgb(0.45f, 0.4f, 0.34f), stone1 = rgb(0.74f, 0.68f, 0.58f);
    for (int i = 0; i + step < t->n; i += step) {
      int j = i + step;
      bool ok = true;
      for (int k = i; k <= j && ok; k++) ok = support_ok(t, k);
      if (!ok) continue;
      const tsample *a = &t->s[i], *e = &t->s[j];
      float gy0 = ground ? ground(a->p.x, a->p.z, ud) : base_y, gy1 = ground ? ground(e->p.x, e->p.z, ud) : base_y;
      if (a->p.y - gy0 < min_clear || e->p.y - gy1 < min_clear) continue;
      if (crossing(t, i, a->p.x, a->p.z)) continue;
      int strips = 10;
      for (int sIdx = 0; sIdx < strips; sIdx++) {
        float f0 = (float)sIdx / (float)strips, f1 = (float)(sIdx + 1) / (float)strips, fm = (f0 + f1) * 0.5f;
        tsample m = track_at(t, a->s + (e->s - a->s) * fm);
        t3_vec3 p = V(m.p.x, 0, m.p.z);
        float g = ground ? ground(m.p.x, m.p.z, ud) : base_y;
        float topy = m.p.y - 0.7f;
        float R = (e->s - a->s) * 0.5f * 0.82f;
        float dx = (fm - 0.5f) * (e->s - a->s);
        float archy = fabsf(dx) < R ? g + sqrtf(R * R - dx * dx) * 0.9f + 0.3f : topy;
        float arch_top = fminf(archy, topy);
        pm *w = upright_at(c, V(p.x, g, p.z), &m);
        pm_layer(w, PL_MAIN);
        float wd = 2 * m.hw * 0.62f;
        pm_vary(w, 0.1f);
        /* the wall above the arch: a strip from arch_top to the deck */
        if (topy - arch_top > 0.05f) {
          pm_push(w);
          pm_move(w, 0, arch_top - g, 0);
          pm_box(w, wd, topy - arch_top, (e->s - a->s) / (float)strips * 1.03f, dk(stone0, 0.8f), stone1);
          pm_pop(w);
        }
        /* the voussoir ring: a thicker band riding the arch line */
        if (fabsf(dx) < R) {
          pm_push(w);
          pm_move(w, 0, archy - g - 0.8f, 0);
          pm_box(w, wd * 1.04f, 0.9f, (e->s - a->s) / (float)strips * 1.05f, dk(stone0, 0.6f), dk(stone1, 0.85f));
          pm_pop(w);
        }
        pm_pop(w);
      }
      count++;
      for (int e2 = 0; e2 < 2; e2++) { /* the pier at each end (the shared one is drawn twice: harmless, hidden inside) */
        const tsample *pa = e2 ? e : a;
        float g = e2 ? gy1 : gy0;
        pm *w = upright_at(c, V(pa->p.x, g, pa->p.z), pa);
        pm_layer(w, PL_MAIN);
        float wd = 2 * pa->hw * 0.66f;
        pm_box(w, wd, pa->p.y - 0.7f - g, 3.2f, dk(stone0, 0.7f), stone1);
        pm_pop(w);
      }
    }
    cw_info("supports: %d arches", count);
    pmc_finish(c, parent, "Supports", true, TEX_WALL_STONE);
    return;
  }
  for (int i = step / 2; i < t->n; i += step) {
    if (!support_ok(t, i)) continue;
    const tsample *a = &t->s[i];
    float gy = ground ? ground(a->p.x, a->p.z, ud) : base_y;
    if (a->p.y - 0.7f - gy < min_clear) continue;
    if (crossing(t, i, a->p.x, a->p.z)) continue;
    bent(c, t, i, style, gy, &st, st.seed);
    count++;
  }
  cw_info("supports: %d bents", count);
  pmc_finish(c, parent, "Supports", true, style == SUP_CONCRETE ? TEX_WALL_CONCRETE : style == SUP_TRESTLE ? TEX_WALL_WOOD : -1);
}

/* ── lights ──────────────────────────────────────────────────────────────────────────────────────── */

void deco_lights(cw_node parent, const track *t, float spacing, lamp_style style, float s0, float s1, const dress_style *in) {
  (void)in;
  if (s1 <= s0) s0 = 0, s1 = t->length;
  if (spacing < 4) spacing = 4;
  prop_set *ps = ps_new(parent, "Lights");
  prop_kind k = style == LAMP_POLE ? PK_LIGHT_POLE : style == LAMP_STREET ? PK_STREET_LAMP : style == LAMP_TORCH ? PK_TORCH : PK_BOLLARD;
  int n = 0;
  for (int side = -1; side <= 1; side += 2) {
    int idx = 0;
    for (float s = s0; s < s1; s += spacing, idx++) {
      /* alternate sides on poles so the road is lit evenly; posts go on both */
      if ((style == LAMP_POLE || style == LAMP_STREET) && (idx & 1) != (side > 0)) continue;
      tsample a = track_at(t, s);
      if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
      float lat = side * (a.hw + 1.1f + (a.walls ? 0.7f : 0.0f) + (style == LAMP_POLE ? 0.8f : 0.0f));
      t3_vec3 p = track_point(t, s, lat, 0);
      if (keep_clear(t, &a, side, p, 1.0f)) continue;
      p.y -= 0.7f;
      t3_vec3 d = V(-side * a.r.x, 0, -side * a.r.z);
      float yaw = atan2f(d.x, d.z);
      float sc = style == LAMP_POLE ? 0.75f + 0.02f * a.hw : style == LAMP_STREET ? 0.9f : 1.0f;
      ps_add_ex(ps, k, idx % prop_variants(k), 0, p, t3_quat_from_axis_angle(V(0, 1, 0), yaw), V(sc, sc, sc), (pcol){ 1, 1, 1 }, (uint32_t)idx);
      n++;
    }
  }
  cw_info("lights: %d", n);
  ps_shadows(ps, false);
  ps_finish(ps);
}

/* ── rails ───────────────────────────────────────────────────────────────────────────────────────── */

void deco_rails(cw_node parent, const track *t, rail_style style, const dress_style *in) {
  dress_style st = styled(in);
  pmc *c = pmc_new(60, st.seed + 4);
  float step = style == RAIL_CONCRETE ? 2.0f : style == RAIL_ICE ? 2.5f : 3.0f;
  int made = 0;
  for (int side = -1; side <= 1; side += 2) {
    for (float s = 0; s + step < t->length; s += step) {
      tsample a = track_at(t, s), e = track_at(t, s + step);
      if (!(a.flags & TF_OPEN) || (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL))) continue;
      uint32_t wallbit = side < 0 ? TF_WALL_L : TF_WALL_R;
      if (a.flags & wallbit) continue;
      float lat = side * (a.hw + 0.35f), lat2 = side * (e.hw + 0.35f);
      t3_vec3 p0 = track_point(t, s, lat, 0), p1 = track_point(t, s + step, lat2, 0);
      if (fork_side(&a, side) || fork_side(&e, side) || keep_clear(t, &a, side, p0, 0.3f) || keep_clear(t, &e, side, p1, 0.3f)) continue;
      pm *b = pmc_at(c, p0);
      pm_push(b);
      pm_layer(b, PL_METAL);
      pcol steel = st.main;
      switch (style) {
      case RAIL_GUARD: {
        pm_beam(b, V(p0.x, p0.y, p0.z), V(p0.x, p0.y + 0.9f, p0.z), 0.12f, dk(steel, 0.7f));
        /* the W-beam: two rails with a highlight band */
        pm_beam(b, V(p0.x, p0.y + 0.55f, p0.z), V(p1.x, p1.y + 0.55f, p1.z), 0.2f, steel);
        pm_beam(b, V(p0.x, p0.y + 0.8f, p0.z), V(p1.x, p1.y + 0.8f, p1.z), 0.16f, pc_mul(steel, 1.2f));
        pm_layer(b, PL_MAIN);
        if (made % 3 == 0) { /* reflector stripe */
          pm_beam(b, V(p0.x, p0.y + 0.55f, p0.z), V(p0.x * 0.7f + p1.x * 0.3f, p0.y * 0.7f + p1.y * 0.3f + 0.55f, p0.z * 0.7f + p1.z * 0.3f), 0.22f, st.accent);
        }
        break;
      }
      case RAIL_FENCE: {
        pm_beam(b, V(p0.x, p0.y - 0.2f, p0.z), V(p0.x, p0.y + 1.4f, p0.z), 0.1f, dk(steel, 0.8f));
        pm_beam(b, V(p0.x, p0.y + 1.35f, p0.z), V(p1.x, p1.y + 1.35f, p1.z), 0.07f, steel);
        pm_layer(b, PL_MAIN);
        t3_vec3 q0 = V(p0.x, p0.y + 0.15f, p0.z), q1 = V(p1.x, p1.y + 0.15f, p1.z);
        pcol mesh = rgb(0.55f, 0.58f, 0.6f);
        pm_quad(b, q0, q1, V(q1.x, q1.y + 1.2f, q1.z), V(q0.x, q0.y + 1.2f, q0.z), mesh);
        pm_quad(b, q1, q0, V(q0.x, q0.y + 1.2f, q0.z), V(q1.x, q1.y + 1.2f, q1.z), mesh);
        break;
      }
      case RAIL_ROPE: {
        pm_layer(b, PL_MAIN);
        pcol wood = rgb(0.4f, 0.28f, 0.16f), rope = rgb(0.75f, 0.62f, 0.4f);
        pm_beam(b, V(p0.x, p0.y - 0.3f, p0.z), V(p0.x, p0.y + 1.1f, p0.z), 0.18f, wood);
        pm_cable(b, V(p0.x, p0.y + 1.0f, p0.z), V(p1.x, p1.y + 1.0f, p1.z), 0.18f, 0.05f, rope);
        pm_cable(b, V(p0.x, p0.y + 0.55f, p0.z), V(p1.x, p1.y + 0.55f, p1.z), 0.18f, 0.05f, rope);
        break;
      }
      case RAIL_CONCRETE: {
        pm_layer(b, PL_MAIN);
        pcol con = rgb(0.66f, 0.65f, 0.62f);
        t3_vec3 m = V((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f, (p0.z + p1.z) * 0.5f);
        pm_pop(b);
        pm_push(b);
        pm_basis(b, m, V(a.r.x, a.r.y, a.r.z), V(a.u.x, a.u.y, a.u.z), V(a.f.x, a.f.y, a.f.z));
        pm_layer(b, PL_MAIN);
        pm_move(b, 0, -0.2f, 0);
        pm_box(b, 0.7f, 1.1f, step * 0.98f, dk(con, 0.6f), (made & 1) ? con : dk(con, 0.92f));
        pm_box(b, 0.3f, 1.25f, step * 0.98f, con, con);
        if ((made % 4) == 0) {
          pm_layer(b, PL_MAIN);
          pm_quad(b, V(0.36f, 0.55f, -step * 0.4f), V(0.36f, 0.55f, step * 0.4f), V(0.36f, 0.85f, step * 0.4f), V(0.36f, 0.85f, -step * 0.4f), st.accent);
          pm_quad(b, V(-0.36f, 0.55f, step * 0.4f), V(-0.36f, 0.55f, -step * 0.4f), V(-0.36f, 0.85f, -step * 0.4f), V(-0.36f, 0.85f, step * 0.4f), st.accent);
        }
        break;
      }
      default: {
        pm_layer(b, PL_MAIN);
        pcol ice0 = rgb(0.4f, 0.65f, 0.88f), ice1 = rgb(0.92f, 0.98f, 1.0f);
        pm_move(b, p0.x, p0.y - 0.2f, p0.z);
        pm_roty(b, atan2f(p1.x - p0.x, p1.z - p0.z));
        pm_jitter(b, 0.1f);
        pm_cyl_ex(b, 0.5f, 0.28f, 1.2f + 0.4f * (float)(made % 3), 5, ice0, ice1, false, false);
        break;
      }
      }
      pm_pop(b);
      made++;
    }
  }
  cw_info("rails: %d segments", made);
  pmc_finish(c, parent, "Rails", true, -1);
}

/* ── jump markings ───────────────────────────────────────────────────────────────────────────────── */

static void chevron(pm *b, float x, float z, float w, float len, float th, float y, pcol c) {
  /* a '>' pointing forward (+z), centred on x, apex at z */
  pm_quad(b, V(x - w, y, z - len), V(x - w + th, y, z - len), V(x + th * 0.2f, y, z), V(x - th * 0.8f, y, z), c);
  pm_quad(b, V(x + w - th, y, z - len), V(x + w, y, z - len), V(x + th * 0.8f, y, z), V(x - th * 0.2f, y, z), c);
}

void deco_jump_marks(cw_node parent, const track *t, const dress_style *in) {
  dress_style st = styled(in);
  pmc *c = pmc_new(70, st.seed + 5);
  pcol yel = st.accent, blk = rgb(0.05f, 0.05f, 0.06f), white = rgb(0.95f, 0.95f, 0.92f), red = rgb(0.86f, 0.14f, 0.1f);
  for (int g = 0; g < t->ngaps; g++) {
    int lip = t->gap_from[g] - 1, land = t->gap_to[g] + 1;
    if (lip < 12 || land >= t->n - 4) continue;
    const tsample *a = &t->s[lip];
    float hw = a->hw;
    pm *b = frame_at(c, a, 0, 0);
    pm_layer(b, PL_MAIN);
    /* hazard stripes across the last 3 m before the edge */
    int ns = (int)(2 * hw / 1.2f);
    for (int i = 0; i < ns; i++) {
      float x0 = -hw + 1.2f * (float)i;
      pcol col = (i & 1) ? yel : blk;
      pm_quad(b, V(x0, 0.04f, 0.0f), V(x0 + 1.2f, 0.04f, 0.0f), V(x0 + 1.2f + 0.8f, 0.04f, -3.0f), V(x0 + 0.8f, 0.04f, -3.0f), col);
    }
    pm_layer(b, PL_GLOW);
    for (int k = 0; k < 3; k++) { /* chevrons in glowing paint, getting closer together towards the lip */
      float z = -6.0f - 6.5f * (float)k;
      chevron(b, 0, z, hw * 0.55f, 3.2f, 0.9f, 0.05f, glow(yel, 1.6f));
    }
    pm_pop(b);
    /* the landing: a target of rings on the road beyond the gap */
    const tsample *l = &t->s[land + 6 < t->n ? land + 6 : land];
    b = frame_at(c, l, 0, 0);
    pm_layer(b, PL_MAIN);
    float R = fminf(hw * 0.8f, 7.0f);
    pcol rings[4] = { red, white, red, white };
    for (int r = 0; r < 4; r++) {
      float rr = R * (1.0f - 0.22f * (float)r);
      pm_push(b);
      pm_move(b, 0, 0.03f + 0.006f * (float)r, 0);
      pm_cyl_ex(b, rr, rr, 0.008f, 28, rings[r], rings[r], false, true);
      pm_pop(b);
    }
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, 0, 0.05f, 0);
    pm_cyl_ex(b, R * 0.2f, R * 0.2f, 0.008f, 16, glow(yel, 1.4f), glow(yel, 1.4f), false, true);
    pm_pop(b);
    pm_pop(b);
  }
  pmc_finish(c, parent, "JumpMarks", false, -1);
}

/* ── corners: arrows and signs ───────────────────────────────────────────────────────────────────── */

/* where each sharp corner begins (distance along), and which way it turns (+ left) */
static int find_corners(const track *t, float min_curv, float *at, float *dir, int max) {
  int n = 0;
  bool in = false;
  for (float s = 0; s < t->length - 4 && n < max; s += 4) {
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP)) { in = false; continue; }
    float k = track_curvature(t, s + 14, 14);
    if (fabsf(k) > min_curv) {
      if (!in) at[n] = s + 8, dir[n] = k > 0 ? 1.0f : -1.0f, n++;
      in = true;
    } else {
      in = false;
    }
  }
  return n;
}

/* an arrow painted on the road at lateral x of sample a that bends the way d says (+ left) through `ang` radians of an
 * arc of radius R: slabs round the arc, a head at the end */
static void bend_arrow(pmc *c, const tsample *a, float x, float d, float R, float ang, const dress_style *st) {
  pm *b = frame_at(c, a, x, 0);
  pm_layer(b, PL_GLOW);
  float w = 0.55f, px = 0, pz = 0;
  for (int sIdx = 0; sIdx < 8; sIdx++) {
    float an0 = ang * (float)sIdx / 8.0f, an1 = ang * (float)(sIdx + 1) / 8.0f;
    float x0 = d * -R * (cosf(an0) - 1) * -1.0f, z0 = R * sinf(an0), x1 = d * -R * (cosf(an1) - 1) * -1.0f, z1 = R * sinf(an1);
    float tx = x1 - x0, tz = z1 - z0, l = sqrtf(tx * tx + tz * tz);
    float nx = -tz / l * w, nz = tx / l * w;
    pm_quad(b, V(x0 - nx, 0.045f, z0 - nz), V(x0 + nx, 0.045f, z0 + nz), V(x1 + nx, 0.045f, z1 + nz), V(x1 - nx, 0.045f, z1 - nz), glow(st->lamp, 1.5f));
    px = x1, pz = z1;
  }
  float tx = px - d * -R * (cosf(ang * 0.875f) - 1) * -1.0f, tz = pz - R * sinf(ang * 0.875f), l = sqrtf(tx * tx + tz * tz);
  tx /= l, tz /= l;
  float nx = -tz, nz = tx;
  pm_tri(b, V(px - nx * 1.6f, 0.045f, pz - nz * 1.6f), V(px + nx * 1.6f, 0.045f, pz + nz * 1.6f), V(px + tx * 3.2f, 0.045f, pz + tz * 3.2f), glow(st->accent, 1.8f));
  pm_pop(b);
}

void deco_corner_arrows(cw_node parent, const track *t, float min_curv, const dress_style *in) {
  dress_style st = styled(in);
  if (min_curv <= 0) min_curv = 0.012f;
  pmc *c = pmc_new(70, st.seed + 6);
  float at[64], dir[64];
  int n = find_corners(t, min_curv, at, dir, 64);
  for (int i = 0; i < n; i++) {
    float s = at[i] - 34;
    if (s < 4) continue;
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
    for (int k = -1; k <= 1; k += 2) bend_arrow(c, &a, k * a.hw * 0.5f, dir[i], 14.0f, 0.7f, &st);
  }
  pmc_finish(c, parent, "CornerArrows", false, -1);
}

/* a sign face: kind SIGN_*; local frame: x right across the board, y up, board faces +z, post below y = 0 */
static void sign_model(pm *b, int kind, float sz, const dress_style *st) {
  pcol blk = rgb(0.05f, 0.05f, 0.06f), yel = st->accent, white = rgb(0.95f, 0.95f, 0.92f);
  pm_layer(b, PL_METAL);
  pm_beam(b, V(0, -2.6f, 0), V(0, sz * 0.5f, -0.1f), 0.12f, rgb(0.35f, 0.36f, 0.4f));
  pm_layer(b, PL_MAIN);
  float h = sz * 0.5f;
  bool wide = kind == SIGN_CHEVRON_L || kind == SIGN_CHEVRON_R;
  float w = wide ? sz * 0.9f : sz * 0.5f;
  pm_push(b);
  pm_move(b, 0, sz * 0.5f, 0);
  pm_box_c(b, w * 2, h * 2, 0.12f, (kind == SIGN_WARN) ? yel : blk);
  float z = 0.065f, d = 1.0f;
  /* drawn once for the right-hand versions; the left-hand ones mirror the symbol (the board itself is symmetric) */
  if (kind == SIGN_ARROW_L || kind == SIGN_CHEVRON_L) pm_scale(b, -1, 1, 1);
  switch (kind) {
  case SIGN_ARROW_L:
  case SIGN_ARROW_R:
    pm_quad(b, V(-w * 0.7f, -h * 0.12f, z), V(w * 0.15f, -h * 0.12f, z), V(w * 0.15f, h * 0.12f, z), V(-w * 0.7f, h * 0.12f, z), yel);
    pm_tri(b, V(w * 0.1f, -h * 0.55f, z), V(w * 0.8f, 0, z), V(w * 0.1f, h * 0.55f, z), yel);
    break;
  case SIGN_CHEVRON_L:
  case SIGN_CHEVRON_R:
    for (int i = 0; i < 2; i++) {
      float ox = d * (-w * 0.35f + w * 0.55f * (float)i);
      float tip = ox + d * w * 0.38f, back = ox - d * w * 0.18f, th = w * 0.17f;
      pm_quad(b, V(back, -h * 0.7f, z), V(back + d * th, -h * 0.7f, z), V(tip + d * th, 0, z), V(tip, 0, z), yel);
      pm_quad(b, V(tip, 0, z), V(tip + d * th, 0, z), V(back + d * th, h * 0.7f, z), V(back, h * 0.7f, z), yel);
    }
    break;
  case SIGN_WARN:
    pm_tri(b, V(-w * 0.8f, -h * 0.7f, z), V(w * 0.8f, -h * 0.7f, z), V(0, h * 0.85f, z), blk);
    pm_tri(b, V(-w * 0.55f, -h * 0.55f, z + 0.01f), V(w * 0.55f, -h * 0.55f, z + 0.01f), V(0, h * 0.6f, z + 0.01f), yel);
    pm_quad(b, V(-w * 0.07f, -h * 0.35f, z + 0.02f), V(w * 0.07f, -h * 0.35f, z + 0.02f), V(w * 0.07f, h * 0.25f, z + 0.02f), V(-w * 0.07f, h * 0.25f, z + 0.02f), blk);
    pm_quad(b, V(-w * 0.07f, -h * 0.5f, z + 0.02f), V(w * 0.07f, -h * 0.5f, z + 0.02f), V(w * 0.07f, -h * 0.4f, z + 0.02f), V(-w * 0.07f, -h * 0.4f, z + 0.02f), blk);
    break;
  default: /* the finish board: a chequered field */
    for (int j = 0; j < 4; j++)
      for (int i = 0; i < 4; i++) {
        float x0 = -w * 0.8f + w * 1.6f * (float)i / 4.0f, y0 = -h * 0.8f + h * 1.6f * (float)j / 4.0f;
        pm_quad(b, V(x0, y0, z), V(x0 + w * 0.4f, y0, z), V(x0 + w * 0.4f, y0 + h * 0.4f, z), V(x0, y0 + h * 0.4f, z), ((i + j) & 1) ? blk : white);
      }
    break;
  }
  pm_pop(b);
}

void deco_sign(cw_node parent, const track *t, float s, int side, int kind, const dress_style *in) {
  dress_style st = styled(in);
  pmc *c = pmc_new(70, st.seed + 7);
  tsample a = track_at(t, s);
  float lat = (side < 0 ? -1.0f : 1.0f) * (a.hw + 2.4f);
  t3_vec3 p = track_point(t, s, lat, 0);
  if (keep_clear(t, &a, side < 0 ? -1 : 1, p, 1.5f)) {
    pmc_finish(c, parent, "Sign", true, -1);
    return;
  }
  pm *b = pmc_at(c, p);
  pm_push(b);
  /* faces back down the road at the oncoming kart, turned a little toward the line */
  t3_vec3 f = vnorm(V(-a.f.x, 0, -a.f.z));
  t3_vec3 r = V(a.r.x, 0, a.r.z);
  float tw = -(side < 0 ? -1.0f : 1.0f) * 0.25f;
  t3_vec3 face = vnorm(V(f.x + r.x * tw, 0, f.z + r.z * tw));
  t3_vec3 right = V(face.z, 0, -face.x);
  pm_basis(b, V(p.x, p.y, p.z), right, V(0, 1, 0), face);
  pm_move(b, 0, 2.6f - 0.7f, 0);
  sign_model(b, kind, 2.6f, &st);
  pm_pop(b);
  pmc_finish(c, parent, "Sign", true, -1);
}

int deco_corner_signs(cw_node parent, const track *t, float min_curv, const dress_style *in) {
  dress_style st = styled(in);
  if (min_curv <= 0) min_curv = 0.012f;
  pmc *c = pmc_new(70, st.seed + 8);
  float at[64], dir[64];
  int n = find_corners(t, min_curv, at, dir, 64), made = 0;
  for (int i = 0; i < n; i++) {
    /* a warning before the corner, then three chevron boards along its outside */
    float s0 = at[i] - 40;
    for (int k = 0; k < 4; k++) {
      float s = k == 0 ? s0 : at[i] + 8.0f + 10.0f * (float)(k - 1);
      if (s < 2 || s > t->length - 2) continue;
      tsample a = track_at(t, s);
      if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
      int side = dir[i] > 0 ? 1 : -1; /* outside of a left turn is the right */
      if (k == 0) side = -side;
      float lat = side * (a.hw + 2.4f + (a.walls ? 0.8f : 0));
      t3_vec3 p = track_point(t, s, lat, 0);
      if (keep_clear(t, &a, side, p, 1.5f)) continue;
      pm *b = pmc_at(c, p);
      pm_push(b);
      t3_vec3 face;
      if (k == 0) face = vnorm(V(-a.f.x + a.r.x * -side * 0.25f, 0, -a.f.z + a.r.z * -side * 0.25f));
      else face = vnorm(V(-a.r.x * side, 0, -a.r.z * side)); /* the chevrons face across the road */
      t3_vec3 right = V(face.z, 0, -face.x);
      pm_basis(b, p, right, V(0, 1, 0), face);
      pm_move(b, 0, 2.2f - 0.7f, 0);
      sign_model(b, k == 0 ? SIGN_WARN : (dir[i] > 0 ? SIGN_CHEVRON_L : SIGN_CHEVRON_R), 2.5f, &st);
      pm_pop(b);
      made++;
    }
  }
  pmc_finish(c, parent, "CornerSigns", true, -1);
  return made;
}

/* ── banner hoops, cables ────────────────────────────────────────────────────────────────────────── */

void deco_banners(cw_node parent, const track *t, float s0, float s1, float spacing, const dress_style *in) {
  dress_style st = styled(in);
  if (s1 <= s0) s0 = 0, s1 = t->length;
  if (spacing < 20) spacing = 20;
  pmc *c = pmc_new(80, st.seed + 9);
  int n = 0;
  for (float s = s0 + spacing * 0.5f; s < s1; s += spacing, n++) {
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
    if (fabsf(track_curvature(t, s, 12)) > 0.02f) continue;
    float rx = a.hw + 1.2f, H = 6.2f + a.hw * 0.12f;
    if (keep_clear(t, &a, -1, track_point(t, s, -rx, 0), 1.0f) || keep_clear(t, &a, 1, track_point(t, s, rx, 0), 1.0f)) continue;
    pm *b = frame_at(c, &a, 0, 0);
    pm_layer(b, PL_METAL);
    pm_beam(b, V(-rx, -0.7f, 0), V(-rx, H, 0), 0.34f, st.main);
    pm_beam(b, V(rx, -0.7f, 0), V(rx, H, 0), 0.34f, st.main);
    pm_beam(b, V(-rx, H, 0), V(rx, H, 0), 0.3f, st.main);
    pm_layer(b, PL_MAIN);
    /* the banner: stripes of the style's colours, a face each way, with a drooping lower edge */
    int ns = (int)(2 * rx / 2.0f);
    for (int i = 0; i < ns; i++) {
      float x0 = -rx + 2 * rx * (float)i / (float)ns, x1 = -rx + 2 * rx * (float)(i + 1) / (float)ns;
      pcol col = (i & 1) ? st.accent : st.main;
      float d0 = 0.5f * sinf((float)i * 1.7f + (float)n), d1 = 0.5f * sinf((float)(i + 1) * 1.7f + (float)n);
      pm_quad(b, V(x0, H - 0.3f, 0), V(x1, H - 0.3f, 0), V(x1, H - 1.9f + d1 * 0.2f, 0), V(x0, H - 1.9f + d0 * 0.2f, 0), col);
      pm_quad(b, V(x1, H - 0.3f, 0), V(x0, H - 0.3f, 0), V(x0, H - 1.9f + d0 * 0.2f, 0), V(x1, H - 1.9f + d1 * 0.2f, 0), col);
    }
    pm_layer(b, PL_GLOW);
    pm_quad(b, V(-rx, H + 0.2f, 0.2f), V(rx, H + 0.2f, 0.2f), V(rx, H + 0.2f, -0.2f), V(-rx, H + 0.2f, -0.2f), glow(st.lamp, 2.4f));
    pm_pop(b);
  }
  pmc_finish(c, parent, "Banners", true, -1);
}

void deco_cables(cw_node parent, const t3_vec3 *pts, int n, float sag, float r, pcol col) {
  pmc *c = pmc_new(100, 11);
  for (int i = 0; i + 1 < n; i++) {
    pm *b = pmc_at(c, pts[i]);
    pm_layer(b, PL_METAL);
    pm_cable(b, pts[i], pts[i + 1], sag, r, col);
  }
  pmc_finish(c, parent, "Cables", false, -1);
}

/* ── branches: the shortcuts signposted ──────────────────────────────────────────────────────────────── */

branch_dress branch_dress_defaults(void) {
  branch_dress d = { 0 };
  d.st = dress_defaults();
  d.lamps = LAMP_GLOW_POST;
  d.lamp_spacing = 12;
  d.tip = PK_BOLLARD;
  d.tip_scale = 1.0f;
  d.chevrons = true;
  d.arrows = true;
  d.sign = true;
  d.nose = true;
  return d;
}

/* the island tip where branch b parts from (end 0) or meets (end 1) the main road: the first sample past the overlap
 * with at least `gap` metres of ground between the two roads' edges; *tip is the middle of that ground */
static int island_tip(const track *t, int b, int end, float gap, t3_vec3 *tip) {
  const track_branch *B = &t->br[b];
  int dir = end ? -1 : 1, i = end ? B->first + B->n - 1 : B->first;
  int hint = -1;
  for (int k = 0; k < B->n; k++, i += dir) {
    const tsample *q = &t->s[i];
    if (q->flags & (TF_JOIN | TF_GAP)) continue;
    float in = B->side < 0 ? q->hw : -q->hw; /* the branch's edge on the main road's side */
    t3_vec3 e = vadd(q->p, vmul(q->r, in));
    track_loc l;
    track_locate_main(t, e, hint, &l);
    hint = l.i;
    float hm = t->s[l.i].hw, g = fabsf(l.lat) - hm;
    if (g < gap) continue;
    t3_vec3 m = track_point(t, l.s, (float)B->side * hm, 0);
    *tip = vmul(vadd(e, m), 0.5f);
    return i;
  }
  return -1;
}

void deco_branches(cw_node parent, const track *t, const branch_dress *in) {
  branch_dress bd = in ? *in : branch_dress_defaults();
  dress_style st = bd.st;
  pmc *c = pmc_new(70, st.seed + 12);
  prop_set *ps = ps_new(parent, "BranchMarks");
  for (int b = 0; b < t->nbranch; b++) {
    const track_branch *B = &t->br[b];
    track v = track_branch_view(t, b);
    /* its own edges lit, its bends chevroned, arrows on its road before them */
    if (bd.lamp_spacing > 0) deco_lights(parent, &v, bd.lamp_spacing, bd.lamps, 0, 0, &st);
    if (bd.chevrons) deco_corner_signs(parent, &v, 0.012f, &st);
    if (bd.arrows) deco_corner_arrows(parent, &v, 0.012f, &st);
    /* on the main road before the mouth: an arrow in the lane that leads in, bending its way */
    if (bd.arrows) {
      tsample m = track_at(t, B->s0 - 26.0f);
      if (!(m.flags & (TF_GAP | TF_LOOP | TF_TUNNEL))) bend_arrow(c, &m, (float)B->side * m.hw * 0.55f, B->side < 0 ? 1.0f : -1.0f, 18.0f, 0.5f, &st);
    }
    /* the islands' noses paved in hazard stripes, from where the roads part until they are a lane apart */
    if (bd.nose)
      for (int end = 0; end < 2; end++) {
        int dir = end ? -1 : 1, i = end ? B->first + B->n - 1 : B->first, hint = -1, made = 0;
        t3_vec3 pe = { 0 }, pm0 = { 0 };
        bool have = false;
        for (int k = 0; k < B->n && made < 40; k++, i += dir) {
          const tsample *q = &t->s[i];
          if (q->flags & TF_GAP) break;
          if (q->flags & TF_JOIN) continue;
          t3_vec3 e = vadd(q->p, vmul(q->r, B->side < 0 ? q->hw : -q->hw));
          track_loc l;
          track_locate_main(t, e, hint, &l);
          hint = l.i;
          float hm = t->s[l.i].hw, g = fabsf(l.lat) - hm;
          t3_vec3 m = track_point(t, l.s, (float)B->side * hm, 0);
          if (g > 7.0f) break;
          float y = fminf(e.y, m.y) - 0.03f;
          e.y = m.y = y;
          if (have) {
            pm *pb = pmc_at(c, e);
            pm_push(pb);
            pm_layer(pb, PL_MAIN);
            pcol col = ((made / 2) & 1) ? st.accent : rgb(0.06f, 0.06f, 0.07f);
            /* both windings: it reads from above whichever side the branch is on (and from below, on a station) */
            pm_quad(pb, pe, e, m, pm0, col);
            pm_quad(pb, pm0, m, e, pe, col);
            pm_pop(pb);
          }
          pe = e, pm0 = m, have = true, made++;
        }
      }
    /* the island tips: a sign at the parting pointing in, markers along the island's nose at both ends */
    for (int end = 0; end < 2; end++) {
      t3_vec3 tip;
      int i = island_tip(t, b, end, 1.6f, &tip);
      if (i < 0) continue;
      const tsample *q = &t->s[i];
      t3_vec3 f = vnorm(V(q->f.x, 0, q->f.z));
      if (end == 0 && bd.sign) {
        pm *s = pmc_at(c, tip);
        pm_push(s);
        t3_vec3 face = V(-f.x, 0, -f.z), right = V(face.z, 0, -face.x);
        pm_basis(s, V(tip.x, tip.y, tip.z), right, V(0, 1, 0), face);
        pm_move(s, 0, 2.6f - 0.7f, 0);
        sign_model(s, B->side < 0 ? SIGN_ARROW_L : SIGN_ARROW_R, 2.6f, &st);
        pm_pop(s);
      }
      /* a short row of markers along the nose of the island, from its tip back between the two roads */
      for (int k = 0; k < 5; k++) {
        t3_vec3 p = vadd(tip, vmul(f, (end ? 1.0f : -1.0f) * (1.6f + 2.2f * (float)k)));
        float ry, gm = road_gap(t, p.x, p.z, &ry);
        if (gm < -0.2f) break; /* back on a road: the island has closed */
        p.y = tip.y - 0.05f;
        ps_add(ps, bd.tip, p, atan2f(f.x, f.z), bd.tip_scale * (k ? 0.9f : 1.15f), st.seed + (uint32_t)(b * 16 + end * 8 + k));
      }
    }
  }
  ps_shadows(ps, false);
  ps_finish(ps);
  pmc_finish(c, parent, "BranchSigns", true, -1);
}
