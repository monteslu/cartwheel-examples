/* racer's track path: the turtle, the closing solve and the queries.
 * See track.h. + - * / sqrtf and cartwheel's cw_sin / cw_cos / cw_atan2 only,
 * so every host builds the same samples. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "track.h"

#define DEG (3.14159265f / 180.0f)
#define TWO_PI 6.2831853f
#define TRACK_G 20.0f /* must match the sim's gravity: the arc of a jump */
#define MAX_PIECES 256
#define MAX_SAMPLES 16384

typedef struct pose {
  float psi, theta, phi; /* heading, pitch, bank */
  t3_vec3 p;
  float hw;
} pose;

static float smooth(float t) { return t <= 0 ? 0 : t >= 1 ? 1 : t * t * (3 - 2 * t); }
static t3_vec3 v3_(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 add(t3_vec3 a, t3_vec3 b) { return v3_(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 sub(t3_vec3 a, t3_vec3 b) { return v3_(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 mul(t3_vec3 a, float k) { return v3_(a.x * k, a.y * k, a.z * k); }
static float dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static t3_vec3 cross(t3_vec3 a, t3_vec3 b) { return v3_(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static t3_vec3 norm(t3_vec3 a) {
  float l = sqrtf(dot(a, a));
  return l > 1e-9f ? mul(a, 1.0f / l) : v3_(0, 1, 0);
}
static float wrap_pi(float a) {
  while (a > 3.14159265f) a -= TWO_PI;
  while (a < -3.14159265f) a += TWO_PI;
  return a;
}

/* the road frame of a pose: right = forward x up (looking along +z with y up, right is -x) */
static void frame_of(float psi, float theta, float phi, t3_vec3 *r, t3_vec3 *u, t3_vec3 *f) {
  float sp, cp, st, ct, sf, cf;
  cw_sincos(psi, &sp, &cp);
  cw_sincos(theta, &st, &ct);
  cw_sincos(phi, &sf, &cf);
  t3_vec3 fh = v3_(sp, 0, cp), rh = v3_(-cp, 0, sp), y = v3_(0, 1, 0);
  t3_vec3 ff = add(mul(fh, ct), mul(y, st));
  t3_vec3 u0 = add(mul(fh, -st), mul(y, ct));
  /* bank leans the road to the left for a positive angle */
  *f = ff;
  *r = add(mul(rh, cf), mul(u0, sf));
  *u = add(mul(u0, cf), mul(rh, -sf));
}

typedef struct walker {
  tsample *out; /* NULL: poses only */
  int n, cap;
  pose ps;
  float s;
  uint8_t surf, wl, wr;
  bool tunnel, plate;
  uint32_t persist;       /* P_FLAG from here on */
  uint32_t timed;         /* P_FLAG with a length */
  float timed_left;
  float boost_left, water_left;
  uint32_t pending;       /* one-shot flags for the next sample */
  int seg;
  track *t;
} walker;

static void emit(walker *w, t3_vec3 f_override, bool use_override, uint32_t extra) {
  if (!w->out || w->n >= w->cap) {
    w->n++;
    return;
  }
  tsample *q = &w->out[w->n++];
  t3_vec3 r, u, f;
  frame_of(w->ps.psi, w->ps.theta, w->ps.phi, &r, &u, &f);
  if (use_override) {
    /* a jump's flight: the frame follows the arc's tangent, wings level */
    f = f_override;
    r = norm(cross(f, v3_(0, 1, 0)));
    u = cross(r, f);
  }
  q->p = w->ps.p;
  q->r = r;
  q->u = u;
  q->f = f;
  q->hw = w->ps.hw;
  q->s = w->s;
  q->bank = w->ps.phi;
  q->surf = w->surf;
  q->walls = (uint8_t)(w->wl > w->wr ? w->wl : w->wr);
  q->seg = (uint16_t)w->seg;
  uint32_t fl = w->persist | extra | w->pending;
  w->pending = 0;
  if (w->timed_left > 0) {
    fl |= w->timed;
    w->timed_left -= STEP_M;
  }
  if (w->boost_left > 0) {
    fl |= TF_BOOST;
    w->boost_left -= STEP_M;
  }
  if (w->water_left > 0) {
    fl |= TF_WATER;
    w->water_left -= STEP_M;
  }
  if (w->tunnel) fl |= TF_TUNNEL;
  if (w->plate) fl |= TF_PLATE;
  if (w->wl) fl |= TF_WALL_L;
  if (w->wr) fl |= TF_WALL_R;
  if (!w->wl || !w->wr) fl |= TF_OPEN;
  if (w->surf == SURF_ICE) fl |= TF_DRIFTY;
  if (w->surf == SURF_DIRT) fl |= TF_ROUGH;
  q->flags = fl;
}

static void advance(walker *w, float ds) {
  t3_vec3 r, u, f;
  frame_of(w->ps.psi, w->ps.theta, w->ps.phi, &r, &u, &f);
  w->ps.p = add(w->ps.p, mul(f, ds));
  w->s += ds;
}

static void do_move(walker *w, const piece *pc) {
  float len = pc->a;
  if (len < 0.5f) return;
  int n = (int)(len / STEP_M + 0.5f);
  if (n < 1) n = 1;
  float ds = len / (float)n;
  float psi0 = w->ps.psi, th0 = w->ps.theta, ph0 = w->ps.phi, hw0 = w->ps.hw;
  float dpsi = pc->b * DEG;
  float th1 = pc->c >= KEEP ? th0 : pc->c * DEG;
  float ph1 = pc->d >= KEEP ? ph0 : pc->d * DEG;
  float hw1 = pc->e >= KEEP ? hw0 : pc->e;
  for (int i = 0; i < n; i++) {
    float t0 = (float)i / (float)n, t1 = (float)(i + 1) / (float)n, tm = 0.5f * (t0 + t1);
    emit(w, v3_(0, 0, 0), false, 0);
    /* position moves along the frame at the middle of the step */
    pose mid = w->ps;
    mid.psi = psi0 + dpsi * tm;
    mid.theta = th0 + (th1 - th0) * smooth(tm);
    mid.phi = pc->peak ? ph0 + (ph1 - ph0) * cw_sin(3.14159265f * tm) : ph0 + (ph1 - ph0) * smooth(tm);
    t3_vec3 r, u, f;
    frame_of(mid.psi, mid.theta, mid.phi, &r, &u, &f);
    w->ps.p = add(w->ps.p, mul(f, ds));
    w->s += ds;
    w->ps.psi = psi0 + dpsi * t1;
    w->ps.theta = th0 + (th1 - th0) * smooth(t1);
    w->ps.phi = pc->peak ? ph0 + (ph1 - ph0) * cw_sin(3.14159265f * t1) : ph0 + (ph1 - ph0) * smooth(t1);
    w->ps.hw = hw0 + (hw1 - hw0) * smooth(t1);
  }
}

static void do_loop(walker *w, const piece *pc) {
  float R = pc->a;
  float len = TWO_PI * R;
  int n = (int)(len / STEP_M + 0.5f);
  float ds = len / (float)n;
  float th0 = w->ps.theta;
  float shift = pc->b * (2.0f * w->ps.hw + 5.0f);
  int start = w->n;
  for (int i = 0; i < n; i++) {
    float t0 = (float)i / (float)n, t1 = (float)(i + 1) / (float)n, tm = 0.5f * (t0 + t1);
    emit(w, v3_(0, 0, 0), false, TF_LOOP);
    t3_vec3 r, u, f;
    frame_of(w->ps.psi, th0 + TWO_PI * tm, w->ps.phi, &r, &u, &f);
    w->ps.p = add(add(w->ps.p, mul(f, ds)), mul(r, shift * (smooth(t1) - smooth(t0))));
    w->s += ds;
    w->ps.theta = th0 + TWO_PI * t1;
  }
  w->ps.theta = th0;
  if (w->t && w->t->nloops < 16) {
    w->t->loop_from[w->t->nloops] = start;
    w->t->loop_to[w->t->nloops++] = w->n;
  }
}

static void do_cork(walker *w, const piece *pc) {
  float len = pc->a;
  int n = (int)(len / STEP_M + 0.5f);
  if (n < 1) return;
  float ds = len / (float)n;
  float ph0 = w->ps.phi, turns = pc->b;
  int start = w->n;
  for (int i = 0; i < n; i++) {
    float t0 = (float)i / (float)n, t1 = (float)(i + 1) / (float)n, tm = 0.5f * (t0 + t1);
    w->ps.phi = ph0 + TWO_PI * turns * smooth(t0);
    emit(w, v3_(0, 0, 0), false, TF_LOOP);
    t3_vec3 r, u, f;
    frame_of(w->ps.psi, w->ps.theta, ph0 + TWO_PI * turns * smooth(tm), &r, &u, &f);
    w->ps.p = add(w->ps.p, mul(f, ds));
    w->s += ds;
  }
  w->ps.phi = ph0;
  if (w->t && w->t->nloops < 16) {
    w->t->loop_from[w->t->nloops] = start;
    w->t->loop_to[w->t->nloops++] = w->n;
  }
}

static void do_jump(walker *w, const piece *pc) {
  float L = pc->a, v = pc->b;
  float th0 = w->ps.theta;
  float c = cw_cos(th0), tn = cw_sin(th0) / (c > 0.2f ? c : 0.2f);
  if (c < 0.2f) return;
  t3_vec3 r0, u0, f0;
  frame_of(w->ps.psi, 0, 0, &r0, &u0, &f0); /* f0: the horizontal heading */
  float k = TRACK_G / (2.0f * v * v * c * c);
  int start = w->n;
  float x = 0;
  int guard = 0;
  while (x < L - 1e-3f && guard++ < 4000) {
    float y = x * tn - k * x * x;
    float slope = tn - 2.0f * k * x;
    t3_vec3 tang = norm(add(mul(f0, 1.0f), mul(v3_(0, 1, 0), slope)));
    t3_vec3 here = add(w->ps.p, v3_(0, 0, 0));
    (void)here;
    /* position of the flight at x: from the lip */
    (void)y;
    emit(w, tang, true, TF_GAP);
    float dx = STEP_M / sqrtf(1.0f + slope * slope);
    if (x + dx > L) dx = L - x;
    float x1 = x + dx;
    float y1 = x1 * tn - k * x1 * x1;
    float y0 = x * tn - k * x * x;
    w->ps.p = add(w->ps.p, add(mul(f0, dx), v3_(0, y1 - y0, 0)));
    w->s += sqrtf(dx * dx + (y1 - y0) * (y1 - y0));
    x = x1;
  }
  float slope = tn - 2.0f * k * L;
  w->ps.theta = cw_atan2(slope, 1.0f);
  if (w->t && w->t->ngaps < 24) {
    w->t->gap_from[w->t->ngaps] = start;
    w->t->gap_to[w->t->ngaps++] = w->n;
  }
}

static void walk_pieces(walker *w, const piece *p, int np) {
  for (int i = 0; i < np; i++) {
    const piece *pc = &p[i];
    w->seg = i;
    switch (pc->kind) {
    case P_MOVE: do_move(w, pc); break;
    case P_LOOP: do_loop(w, pc); break;
    case P_CORK: do_cork(w, pc); break;
    case P_JUMP: do_jump(w, pc); break;
    case P_SURF: w->surf = (uint8_t)pc->a; break;
    case P_WALLS: w->wl = (uint8_t)pc->a, w->wr = (uint8_t)pc->b; break;
    case P_TUNNEL: w->tunnel = pc->a > 0.5f, w->plate = pc->a > 1.5f; break;
    case P_BOOST: w->boost_left = pc->a; break;
    case P_WATER: w->water_left = pc->a; break;
    case P_CHECK:
      w->pending |= TF_CHECK;
      if (w->t && w->t->nchecks < 32) w->t->check_at[w->t->nchecks++] = w->n;
      break;
    case P_ITEMS:
      w->pending |= TF_ITEMS;
      if (w->t && w->t->nitem_rows < 48) w->t->item_row_at[w->t->nitem_rows++] = w->n;
      break;
    case P_FLAG:
      if (pc->b > 0) w->timed |= (uint32_t)pc->a, w->timed_left = pc->b;
      else w->persist |= (uint32_t)pc->a;
      break;
    case P_CLEAR: w->persist &= ~(uint32_t)pc->a; break;
    case P_END: return;
    }
  }
}

static void walker_init(walker *w, tsample *out, int cap, const track_spec *spec, track *t) {
  memset(w, 0, sizeof *w);
  w->out = out;
  w->cap = cap;
  w->t = t;
  w->ps.hw = spec->hw0;
  w->wl = w->wr = 2;
  w->surf = SURF_ROAD;
}

/* the pose after all pieces, and the sample count */
static pose end_pose(const piece *p, int np, const track_spec *spec) {
  walker w;
  walker_init(&w, NULL, 0, spec, NULL);
  walk_pieces(&w, p, np);
  return w.ps;
}

/* ── the closing solve ──────────────────────────────────────────────── */

typedef struct var {
  int piece;
  char which; /* 'a' length, 'b' turn, 'c' pitch */
  float scale;
} var;

static float *var_ref(piece *p, const var *v) {
  return v->which == 'a' ? &p[v->piece].a : v->which == 'b' ? &p[v->piece].b : &p[v->piece].c;
}

/* 4 residuals: x, y, z of the end against the origin, and the heading */
static void residual(piece *p, int np, const track_spec *spec, float r[4]) {
  pose e = end_pose(p, np, spec);
  r[0] = e.p.x;
  r[1] = e.p.y;
  r[2] = e.p.z;
  r[3] = wrap_pi(e.psi) * 20.0f; /* radians weigh as 20 m: a degree is about a third of a metre */
}

static bool solve4(float A[4][4], float b[4], float x[4]) {
  float M[4][5];
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) M[i][j] = A[i][j];
    M[i][4] = b[i];
  }
  for (int c = 0; c < 4; c++) {
    int piv = c;
    for (int i = c + 1; i < 4; i++)
      if (fabsf(M[i][c]) > fabsf(M[piv][c])) piv = i;
    if (fabsf(M[piv][c]) < 1e-9f) return false;
    if (piv != c)
      for (int j = 0; j < 5; j++) {
        float t = M[c][j];
        M[c][j] = M[piv][j];
        M[piv][j] = t;
      }
    for (int i = c + 1; i < 4; i++) {
      float k = M[i][c] / M[c][c];
      for (int j = c; j < 5; j++) M[i][j] -= k * M[c][j];
    }
  }
  for (int i = 3; i >= 0; i--) {
    float s = M[i][4];
    for (int j = i + 1; j < 4; j++) s -= M[i][j] * x[j];
    x[i] = s / M[i][i];
  }
  return true;
}

static bool close_circuit(piece *p, int np, const track_spec *spec, float *err_out) {
  var vars[48];
  int nv = 0;
  for (int i = 0; i < np && nv < 45; i++) {
    if (!p[i].adj) continue;
    vars[nv++] = (var){ i, 'a', 1.0f };
    if (p[i].kind == P_MOVE && p[i].b != 0) vars[nv++] = (var){ i, 'b', 0.5f };
    if (p[i].kind == P_MOVE && p[i].c < KEEP) vars[nv++] = (var){ i, 'c', 0.3f };
  }
  if (nv < 4) {
    cw_error("track %s: the closing solve needs at least four adjustable values (ADJ), got %d", spec->name, nv);
    return false;
  }
  float r[4];
  residual(p, np, spec, r);
  for (int it = 0; it < 80; it++) {
    float e = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    if (e < 0.02f) break;
    /* Jacobian by differences */
    float J[4][48];
    for (int k = 0; k < nv; k++) {
      float *ref = var_ref(p, &vars[k]);
      float h = vars[k].which == 'a' ? 0.05f : 0.002f, keep = *ref;
      *ref = keep + h;
      float r2[4];
      residual(p, np, spec, r2);
      *ref = keep;
      for (int i = 0; i < 4; i++) J[i][k] = (r2[i] - r[i]) / h;
    }
    /* minimum-norm step with each variable weighted by its scale: dx = W J^T (J W J^T + eps I)^-1 (-r) */
    float A[4][4] = { { 0 } }, rhs[4];
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++) {
        float s = 0;
        for (int k = 0; k < nv; k++) s += J[i][k] * vars[k].scale * J[j][k];
        A[i][j] = s + (i == j ? 1e-3f : 0);
      }
    for (int i = 0; i < 4; i++) rhs[i] = -r[i];
    float y[4];
    if (!solve4(A, rhs, y)) {
      cw_error("track %s: the closing solve is singular (the adjustable pieces cannot move the end where it must go)", spec->name);
      return false;
    }
    float damp = e > 50 ? 0.5f : 1.0f;
    for (int k = 0; k < nv; k++) {
      float dx = 0;
      for (int i = 0; i < 4; i++) dx += J[i][k] * y[i];
      *var_ref(p, &vars[k]) += dx * vars[k].scale * damp;
      if (vars[k].which == 'a' && *var_ref(p, &vars[k]) < 1.0f) *var_ref(p, &vars[k]) = 1.0f;
    }
    residual(p, np, spec, r);
  }
  float e = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
  *err_out = e;
  if (e > 0.5f || fabsf(r[3] / 20.0f) > 1.0f * DEG) {
    cw_error("track %s: the circuit does not close: %.2f m off, heading %.2f deg", spec->name, e, r[3] / 20.0f / DEG);
    return false;
  }
  return true;
}

/* ── build ──────────────────────────────────────────────────────────── */

bool track_build(track *t, const track_spec *spec) {
  memset(t, 0, sizeof *t);
  if (!spec || !spec->npieces) return false;
  piece p[MAX_PIECES];
  int np = spec->npieces < MAX_PIECES ? spec->npieces : MAX_PIECES;
  memcpy(p, spec->pieces, sizeof(piece) * (size_t)np);
  t->circuit = spec->circuit;
  if (spec->circuit) {
    float err = 0;
    if (!close_circuit(p, np, spec, &err)) return false;
    t->closure_error = err;
  }
  t->s = malloc(sizeof(tsample) * MAX_SAMPLES);
  walker w;
  walker_init(&w, t->s, MAX_SAMPLES, spec, t);
  /* the finish line is the first sample */
  w.pending = TF_FINISH | TF_CHECK;
  t->check_at[t->nchecks++] = 0;
  walk_pieces(&w, p, np);
  if (w.n >= MAX_SAMPLES) {
    cw_error("track %s: more than %d samples", spec->name, MAX_SAMPLES);
    free(t->s);
    t->s = NULL;
    return false;
  }
  t->n = w.n;
  t->length = w.s;
  /* a circuit's flags wrap: the finish sample keeps the line, the last keeps what it had */
  if (t->circuit) {
    /* snap the end to the start exactly, spread over the last 40 samples */
    t3_vec3 miss = sub(t->s[0].p, w.ps.p);
    int k0 = t->n > 40 ? t->n - 40 : 0;
    for (int i = k0; i < t->n; i++) {
      float k = (float)(i - k0 + 1) / (float)(t->n - k0);
      t->s[i].p = add(t->s[i].p, mul(miss, k));
    }
  }
  return true;
}

void track_free(track *t) {
  free(t->s);
  memset(t, 0, sizeof *t);
}

/* ── queries ────────────────────────────────────────────────────────── */

static int wrap_i(const track *t, int i) {
  if (t->circuit) {
    i %= t->n;
    return i < 0 ? i + t->n : i;
  }
  return i < 0 ? 0 : i >= t->n ? t->n - 1 : i;
}

static int find_s(const track *t, float s) { /* last sample with .s <= s */
  int lo = 0, hi = t->n - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (t->s[mid].s <= s) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

static tsample blend(const tsample *a, const tsample *b, float k) {
  tsample o = *a;
  o.p = add(mul(a->p, 1 - k), mul(b->p, k));
  o.f = norm(add(mul(a->f, 1 - k), mul(b->f, k)));
  o.u = norm(add(mul(a->u, 1 - k), mul(b->u, k)));
  o.r = norm(cross(o.f, o.u));
  o.hw = a->hw * (1 - k) + b->hw * k;
  o.s = a->s * (1 - k) + b->s * k;
  o.bank = a->bank * (1 - k) + b->bank * k;
  return o;
}

tsample track_at(const track *t, float s) {
  float L = t->length;
  if (t->circuit) {
    s = fmodf(s, L);
    if (s < 0) s += L;
  } else s = s < 0 ? 0 : s > t->s[t->n - 1].s ? t->s[t->n - 1].s : s;
  int i = find_s(t, s);
  int j = wrap_i(t, i + 1);
  float s1 = j > i ? t->s[j].s : L;
  float span = s1 - t->s[i].s;
  float k = span > 1e-4f ? (s - t->s[i].s) / span : 0;
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  return blend(&t->s[i], &t->s[j], k);
}

void track_locate(const track *t, t3_vec3 p, int hint, track_loc *o) {
  int best = -1;
  float bd = 1e30f;
  if (hint >= 0) {
    for (int d = -40; d <= 60; d++) {
      int i = wrap_i(t, hint + d);
      t3_vec3 q = sub(p, t->s[i].p);
      float dd = dot(q, q);
      if (dd < bd) bd = dd, best = i;
    }
    if (bd > 60.0f * 60.0f) best = -1;
  }
  if (best < 0) {
    bd = 1e30f;
    for (int i = 0; i < t->n; i++) {
      t3_vec3 q = sub(p, t->s[i].p);
      float dd = dot(q, q);
      if (dd < bd) bd = dd, best = i;
    }
  }
  /* segment: (best-1, best) or (best, best+1), by which side of the best sample the point is */
  int i0 = best;
  t3_vec3 q = sub(p, t->s[best].p);
  if (dot(q, t->s[best].f) < 0 && (t->circuit || best > 0)) i0 = wrap_i(t, best - 1);
  int i1 = wrap_i(t, i0 + 1);
  const tsample *a = &t->s[i0], *b = &t->s[i1];
  t3_vec3 ab = sub(b->p, a->p);
  float len2 = dot(ab, ab);
  float k = len2 > 1e-6f ? dot(sub(p, a->p), ab) / len2 : 0;
  if (k < 0) k = 0;
  if (k > 1) k = 1;
  tsample m = blend(a, b, k);
  t3_vec3 d = sub(p, m.p);
  o->i = k > 0.5f ? i1 : i0;
  o->frac = k;
  float sa = a->s, sb = i1 > i0 ? b->s : t->length;
  o->s = sa + (sb - sa) * k;
  o->lat = dot(d, m.r);
  o->height = dot(d, m.u);
  o->dist = sqrtf(dot(d, d));
  bool gap = (a->flags & TF_GAP) != 0 || (b->flags & TF_GAP) != 0;
  o->on_road = !gap && fabsf(o->lat) < m.hw + 0.8f && o->height > -1.5f && o->height < 4.0f;
}

t3_vec3 track_point(const track *t, float s, float lat, float height) {
  tsample m = track_at(t, s);
  return add(m.p, add(mul(m.r, lat), mul(m.u, height)));
}

float track_curvature(const track *t, float s, float span) {
  tsample a = track_at(t, s), b = track_at(t, s + span);
  /* the change of the forward vector's heading seen from above */
  float ha = cw_atan2(a.f.x, a.f.z), hb = cw_atan2(b.f.x, b.f.z);
  return wrap_pi(hb - ha) / span;
}
