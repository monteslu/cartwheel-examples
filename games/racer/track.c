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
int track_debug; /* the host-side track tool sets it: print the closing solve */
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
  track *t;              /* the main road's walk records its checkpoints, item rows, jumps and loops here */
  track_branch *br;      /* a branch's walk records its jumps here (and nothing else) */
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

/* the pitch of a loop at fraction t of its length: the turn rate ramps up over the first and last `ramp` of it (a
 * clothoid-like entry, so a kart meets the curve gently instead of at once), 0..2 pi in all */
static float loop_pitch(float t, float ramp) {
  float e;
  if (t < ramp) e = t * t / (2 * ramp);
  else if (t <= 1 - ramp) e = ramp * 0.5f + (t - ramp);
  else e = 1 - ramp - (1 - t) * (1 - t) / (2 * ramp);
  return TWO_PI * e / (1 - ramp);
}

static void do_loop(walker *w, const piece *pc) {
  float R = pc->a;
  const float ramp = 0.16f;
  float len = TWO_PI * R / (1 - ramp);
  int n = (int)(len / STEP_M + 0.5f);
  float ds = len / (float)n;
  float th0 = w->ps.theta;
  float shift = pc->b * (2.0f * w->ps.hw + 5.0f);
  int start = w->n;
  for (int i = 0; i < n; i++) {
    float t0 = (float)i / (float)n, t1 = (float)(i + 1) / (float)n, tm = 0.5f * (t0 + t1);
    emit(w, v3_(0, 0, 0), false, TF_LOOP);
    t3_vec3 r, u, f;
    frame_of(w->ps.psi, th0 + loop_pitch(tm, ramp), w->ps.phi, &r, &u, &f);
    w->ps.p = add(add(w->ps.p, mul(f, ds)), mul(r, shift * (smooth(t1) - smooth(t0))));
    w->s += ds;
    w->ps.theta = th0 + loop_pitch(t1, ramp);
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
  /* a gap of 0 is the arc that lands level with the lip at the design speed */
  if (L <= 0) L = 2.0f * v * v * cw_sin(th0) * c / TRACK_G;
  if (L < 3) return;
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
  if (w->br && w->br->ngaps < 2) {
    w->br->gap_from[w->br->ngaps] = start;
    w->br->gap_to[w->br->ngaps++] = w->n;
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

/* where a walk begins: the main road at the origin heading +z, a branch at its mouth */
typedef struct walk_start {
  pose ps;
  uint8_t wl, wr;
} walk_start;

static void walker_init(walker *w, tsample *out, int cap, const walk_start *st, track *t) {
  memset(w, 0, sizeof *w);
  w->out = out;
  w->cap = cap;
  w->t = t;
  w->ps = st->ps;
  w->wl = st->wl, w->wr = st->wr;
  w->surf = SURF_ROAD;
}

static walk_start main_start(const track_spec *spec) {
  walk_start st = { .wl = 2, .wr = 2 };
  st.ps.hw = spec->hw0;
  return st;
}

/* the pose after all pieces, and the sample count */
static pose end_pose(const piece *p, int np, const walk_start *st) {
  walker w;
  walker_init(&w, NULL, 0, st, NULL);
  walk_pieces(&w, p, np);
  return w.ps;
}

/* ── the closing solve ──────────────────────────────────────────────── */

typedef struct var {
  int piece;
  char which; /* 'a' length, 'b' turn, 'c' pitch */
  float scale;
  float lo, hi; /* the range the solve may move it within */
} var;

static float *var_ref(piece *p, const var *v) {
  return v->which == 'a' ? &p[v->piece].a : v->which == 'b' ? &p[v->piece].b : &p[v->piece].c;
}

/* where a walk must end: a point and a heading (the origin and 0 for a circuit, the rejoin for a branch) */
typedef struct walk_goal {
  t3_vec3 p;
  float psi;
} walk_goal;

/* 4 residuals: x, y, z of the end against the goal, and the heading */
static void residual(piece *p, int np, const walk_start *st, const walk_goal *g, float r[4]) {
  pose e = end_pose(p, np, st);
  r[0] = e.p.x - g->p.x;
  r[1] = e.p.y - g->p.y;
  r[2] = e.p.z - g->p.z;
  r[3] = wrap_pi(e.psi - g->psi) * 20.0f; /* radians weigh as 20 m: a degree is about a third of a metre */
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

static bool close_walk(piece *p, int np, const char *name, const walk_start *st, const walk_goal *g, float *err_out) {
  var vars[48];
  int nv = 0;
  for (int i = 0; i < np && nv < 45; i++) {
    if (!p[i].adj) continue;
    /* a piece keeps its character: lengths move by 40% down to 250% up, a turn by 40% to 190%, a slope by 12 degrees */
    vars[nv++] = (var){ i, 'a', 1.0f, p[i].a * 0.4f, p[i].a * 2.5f };
    if (p[i].kind == P_MOVE && p[i].b != 0) {
      float b = p[i].b;
      vars[nv++] = (var){ i, 'b', 0.5f, b > 0 ? b * 0.4f : b * 1.9f, b > 0 ? b * 1.9f : b * 0.4f };
    }
    if (p[i].kind == P_MOVE && p[i].c < KEEP) vars[nv++] = (var){ i, 'c', 0.3f, p[i].c - 12.0f, p[i].c + 12.0f };
  }
  if (nv < 4) {
    cw_error("track %s: the closing solve needs at least four adjustable values (ADJ), got %d", name, nv);
    return false;
  }
  float r[4];
  residual(p, np, st, g, r);
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
      residual(p, np, st, g, r2);
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
      cw_error("track %s: the closing solve is singular (the adjustable pieces cannot move the end where it must go)", name);
      return false;
    }
    float damp = e > 50 ? 0.5f : 1.0f;
    for (int k = 0; k < nv; k++) {
      float dx = 0;
      for (int i = 0; i < 4; i++) dx += J[i][k] * y[i];
      *var_ref(p, &vars[k]) += dx * vars[k].scale * damp;
      float *ref = var_ref(p, &vars[k]);
      if (*ref < vars[k].lo) *ref = vars[k].lo;
      if (*ref > vars[k].hi) *ref = vars[k].hi;
    }
    residual(p, np, st, g, r);
    if (track_debug) cw_info("closing solve %d: error %.2f", it, sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]));
  }
  if (track_debug)
    for (int k = 0; k < nv; k++) cw_info("  var piece %d %c = %.2f in [%.2f, %.2f]", vars[k].piece, vars[k].which, *var_ref(p, &vars[k]), vars[k].lo, vars[k].hi);
  float e = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
  *err_out = e;
  if (e > 0.5f || fabsf(r[3] / 20.0f) > 1.0f * DEG) {
    cw_error("track %s: the closing solve misses: %.2f m off, heading %.2f deg", name, e, r[3] / 20.0f / DEG);
    return false;
  }
  return true;
}

/* ── build ──────────────────────────────────────────────────────────── */

static bool build_branch(track *t, const branch_spec *bs);

bool track_build(track *t, const track_spec *spec) {
  memset(t, 0, sizeof *t);
  if (!spec || !spec->npieces) return false;
  piece p[MAX_PIECES];
  int np = spec->npieces < MAX_PIECES ? spec->npieces : MAX_PIECES;
  memcpy(p, spec->pieces, sizeof(piece) * (size_t)np);
  t->circuit = spec->circuit;
  walk_start st0 = main_start(spec);
  if (spec->circuit) {
    float err = 0;
    walk_goal home = { { 0, 0, 0 }, 0 };
    if (!close_walk(p, np, spec->name, &st0, &home, &err)) return false;
    t->closure_error = err;
  }
  t->s = calloc(MAX_SAMPLES, sizeof(tsample));
  walker w;
  walker_init(&w, t->s, MAX_SAMPLES, &st0, t);
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
  /* the run-up to every jump: the AI wants speed here and a respawn is never placed in it */
  for (int g = 0; g < t->ngaps; g++)
    for (int i = t->gap_from[g] - 60; i < t->gap_from[g]; i++) {
      int j = t->circuit ? (i + t->n) % t->n : i;
      if (j >= 0 && j < t->n) t->s[j].flags |= TF_RAMP;
    }
  for (int g = 0; g < t->nloops; g++)
    for (int i = t->loop_from[g] - 60; i < t->loop_from[g]; i++) {
      int j = t->circuit ? (i + t->n) % t->n : i;
      if (j >= 0 && j < t->n) t->s[j].flags |= TF_RAMP;
    }
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
  t->nall = t->n;
  for (int b = 0; b < spec->nbranches && t->nbranch < 4; b++)
    if (!build_branch(t, &spec->branches[b])) {
      free(t->s);
      t->s = NULL;
      return false;
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

static void locate_in(const track *t, t3_vec3 p, int hint, track_loc *o) {
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

/* ── branches ───────────────────────────────────────────────────────── */

track track_branch_view(const track *t, int b) {
  track v;
  memset(&v, 0, sizeof v);
  const track_branch *B = &t->br[b];
  v.s = t->s + B->first;
  v.n = B->n;
  v.nall = B->n;
  v.length = B->length;
  v.circuit = false;
  v.ngaps = B->ngaps; /* its jumps in its own indices (for markings) */
  for (int g = 0; g < B->ngaps; g++) v.gap_from[g] = B->gap_from[g] - B->first, v.gap_to[g] = B->gap_to[g] - B->first;
  return v;
}

static float wrap_s(const track *t, float s) {
  if (!t->circuit) return s;
  s = fmodf(s, t->length);
  return s < 0 ? s + t->length : s;
}

float track_branch_ms(const track *t, int b, float bs) {
  const track_branch *B = &t->br[b];
  if (bs <= 0) return wrap_s(t, B->s0 + bs);
  if (bs >= B->length) return wrap_s(t, B->s1 + (bs - B->length));
  track v = track_branch_view(t, b);
  int i = find_s(&v, bs), j = i + 1 < v.n ? i + 1 : i;
  float span = v.s[j].s - v.s[i].s, k = span > 1e-4f ? (bs - v.s[i].s) / span : 0;
  k = k < 0 ? 0 : k > 1 ? 1 : k;
  return wrap_s(t, B->s0 + v.s[i].ms + (v.s[j].ms - v.s[i].ms) * k);
}

tsample track_branch_at(const track *t, int b, float bs) {
  const track_branch *B = &t->br[b];
  if (bs < 0) return track_at(t, B->s0 + bs);
  if (bs > B->length) return track_at(t, B->s1 + (bs - B->length));
  track v = track_branch_view(t, b);
  return track_at(&v, bs);
}

tsample track_at_loc(const track *t, const track_loc *l, float ahead) {
  if (l->br >= 0 && l->br < t->nbranch) return track_branch_at(t, l->br, l->s + ahead);
  return track_at(t, l->s + ahead);
}

static int branch_of(const track *t, int i) {
  for (int b = 0; b < t->nbranch; b++)
    if (i >= t->br[b].first && i < t->br[b].first + t->br[b].n) return b;
  return -1;
}

int track_next(const track *t, int i) {
  if (i < t->n) return wrap_i(t, i + 1);
  int b = branch_of(t, i);
  if (b < 0) return i;
  return i + 1 < t->br[b].first + t->br[b].n ? i + 1 : i;
}

void track_locate_main(const track *t, t3_vec3 p, int hint, track_loc *o) {
  if (hint >= t->n) {
    int b = branch_of(t, hint);
    hint = b >= 0 ? find_s(t, track_branch_ms(t, b, t->s[hint].s)) : -1;
  }
  locate_in(t, p, hint, o);
  o->ms = o->s;
  o->br = -1;
}

void track_locate_branch(const track *t, int b, t3_vec3 p, track_loc *o) {
  track v = track_branch_view(t, b);
  locate_in(&v, p, -1, o);
  o->i += t->br[b].first;
  o->br = b;
  o->ms = track_branch_ms(t, b, o->s);
}

/* how well a location fits its path: a share of the half width when on the road, else far worse and by distance */
static float loc_score(const track *t, const track_loc *l) {
  float hw = t->s[l->i].hw;
  return l->on_road ? fabsf(l->lat) / (hw > 0.5f ? hw : 0.5f) : 10.0f + l->dist;
}

void track_locate(const track *t, t3_vec3 p, int hint, track_loc *o) {
  int hb = hint >= t->n ? branch_of(t, hint) : -1;
  track_locate_main(t, p, hint, o);
  if (!t->nbranch) return;
  float best = loc_score(t, o) - (hb < 0 && hint >= 0 ? 0.12f : 0.0f);
  for (int b = 0; b < t->nbranch; b++) {
    const track_branch *B = &t->br[b];
    if (p.x < B->lo.x || p.y < B->lo.y || p.z < B->lo.z || p.x > B->hi.x || p.y > B->hi.y || p.z > B->hi.z) continue;
    track v = track_branch_view(t, b);
    track_loc l;
    locate_in(&v, p, hb == b ? hint - B->first : -1, &l);
    l.i += B->first;
    l.br = b;
    l.ms = track_branch_ms(t, b, l.s);
    /* where it still overlaps the main road only its own side of the main edge is its road */
    const tsample *q = &t->s[l.i];
    if ((q->flags & TF_JOIN) && l.on_road) l.on_road = B->side < 0 ? l.lat < q->cut + 0.3f : l.lat > q->cut - 0.3f;
    float sc = loc_score(t, &l) - (hb == b ? 0.12f : 0.0f);
    if (sc < best) best = sc, *o = l;
  }
}

/* Builds branch bs after the main road's samples: the walk from the mouth, the solve onto the rejoin, then where it
 * overlaps the main road (TF_JOIN, clipped) and where the main road's wall gives way to it (TF_FORK_*). */
static bool build_branch(track *t, const branch_spec *bs) {
  if (!bs->npieces || !t->n) return false;
  piece p[MAX_PIECES];
  int np = bs->npieces < MAX_PIECES ? bs->npieces : MAX_PIECES;
  memcpy(p, bs->pieces, sizeof(piece) * (size_t)np);
  const char *name = bs->name ? bs->name : "branch";
  tsample a = track_at(t, bs->s0), e = track_at(t, bs->s1);
  walk_start st = { .wl = bs->walls_l, .wr = bs->walls_r };
  st.ps.p = add(a.p, mul(a.r, bs->lat0));
  st.ps.psi = cw_atan2(a.f.x, a.f.z);
  st.ps.theta = cw_atan2(a.f.y, sqrtf(a.f.x * a.f.x + a.f.z * a.f.z));
  st.ps.phi = a.bank;
  st.ps.hw = bs->hw0;
  walk_goal goal = { add(e.p, mul(e.r, bs->lat1)), cw_atan2(e.f.x, e.f.z) };
  /* the heading goal is the start's heading plus the turn between: the walk's heading is not wrapped */
  goal.psi = st.ps.psi + wrap_pi(goal.psi - st.ps.psi);
  float err = 0;
  if (!close_walk(p, np, name, &st, &goal, &err)) return false;
  track_branch *B = &t->br[t->nbranch];
  memset(B, 0, sizeof *B);
  B->first = t->nall;
  walker w;
  walker_init(&w, t->s + t->nall, MAX_SAMPLES - t->nall, &st, NULL);
  w.br = B;
  walk_pieces(&w, p, np);
  if (t->nall + w.n >= MAX_SAMPLES) {
    cw_error("track %s: more than %d samples with branch %s", name, MAX_SAMPLES, name);
    return false;
  }
  B->n = w.n;
  B->length = w.s;
  B->s0 = wrap_s(t, bs->s0);
  B->s1 = wrap_s(t, bs->s1);
  B->span = B->s1 - B->s0;
  if (t->circuit && B->span < 0) B->span += t->length;
  B->ai_speed = bs->ai_speed;
  for (int k = 0; k < 3; k++) B->ai_take[k] = bs->ai_take[k];
  for (int g = 0; g < B->ngaps; g++) B->gap_from[g] += B->first, B->gap_to[g] += B->first;
  tsample *S = t->s + B->first;
  /* snap the end onto the rejoin exactly, spread over its last 40 samples */
  t3_vec3 miss = sub(goal.p, w.ps.p);
  int k0 = B->n > 40 ? B->n - 40 : 0;
  for (int i = k0; i < B->n; i++) S[i].p = add(S[i].p, mul(miss, (float)(i - k0 + 1) / (float)(B->n - k0)));
  for (int i = 0; i < B->n; i++) {
    S[i].flags = (S[i].flags & ~(uint32_t)(TF_CHECK | TF_ITEMS | TF_FINISH)) | TF_BRANCH;
    S[i].cut = 0;
  }
  for (int g = 0; g < B->ngaps; g++)
    for (int i = B->gap_from[g] - 60; i < B->gap_from[g]; i++)
      if (i >= B->first) t->s[i].flags |= TF_RAMP;
  /* which side of the main road it lies: its middle */
  track_loc lm;
  locate_in(t, S[B->n / 2].p, -1, &lm);
  B->side = lm.lat < 0 ? -1 : 1;
  /* the mouth and the rejoin: against the main road sample by sample */
  int hint = -1;
  t3_vec3 *own = malloc(sizeof(t3_vec3) * (size_t)B->n), *ofs = calloc((size_t)B->n, sizeof(t3_vec3));
  for (int i = 0; i < B->n; i++) own[i] = S[i].p;
  for (int i = 0; i < B->n; i++) {
    tsample *q = &S[i];
    track_loc l;
    locate_in(t, q->p, hint, &l);
    hint = l.i;
    if (fabsf(l.height) > 3.0f) continue; /* above or below it (a jump, a ledge): no meeting here */
    float hm = t->s[l.i].hw;
    float inner = fabsf(l.lat) - q->hw; /* the main centre to the branch's near edge */
    /* the main road's wall gives way where the branch's road reaches across its edge; it comes back where they part */
    if (inner < hm + 0.3f)
      for (int d = -1; d <= 1; d++) {
        tsample *m = &t->s[wrap_i(t, l.i + d)];
        if (B->side < 0) m->flags = (m->flags & ~(uint32_t)TF_WALL_L) | TF_FORK_L | TF_OPEN;
        else m->flags = (m->flags & ~(uint32_t)TF_WALL_R) | TF_FORK_R | TF_OPEN;
      }
    /* its own wall on the main road's side starts once it is clear of the main wall; until a little after, that side
     * is marked (TF_FORK_*) so roadside dressing keeps off it */
    if (inner < hm + 0.55f + 0.4f) {
      q->flags &= ~(uint32_t)(B->side < 0 ? TF_WALL_R : TF_WALL_L);
      if (!(q->flags & (TF_WALL_L | TF_WALL_R))) q->flags |= TF_OPEN;
    }
    if (inner < hm + 4.0f) q->flags |= B->side < 0 ? TF_FORK_R : TF_FORK_L;
    if (inner < hm) {
      /* where it overlaps the main road it lies in the main road's own surface, so the seam at the cut has no step: the
       * main road may lean (a banked turn's inside edge sinks) where the branch, left to itself, would not */
      tsample m = track_at(t, l.s);
      t3_vec3 on = track_point(t, l.s, l.lat, 0);
      t3_vec3 f = norm(sub(q->f, mul(m.u, dot(q->f, m.u))));
      t3_vec3 r = norm(cross(f, m.u));
      q->p = on, q->f = f, q->r = r, q->u = cross(r, f), q->bank = m.bank;
      t3_vec3 edge = track_point(t, l.s, (float)B->side * hm, 0);
      q->cut = dot(sub(edge, q->p), q->r);
      q->flags |= TF_JOIN | (B->side < 0 ? TF_FORK_R : TF_FORK_L); /* on a branch sample: the side the main road is on */
      ofs[i] = sub(on, own[i]);
      float d = l.s - B->s0;
      if (t->circuit) d = d > t->length * 0.5f ? d - t->length : d < -t->length * 0.5f ? d + t->length : d;
      q->ms = d;
    }
  }
  /* the main-road metres each sample stands for: its own beside the main road, spread evenly between */
  {
    int a = -1, b = B->n;
    while (a + 1 < B->n && (S[a + 1].flags & TF_JOIN)) a++;
    while (b - 1 > a && (S[b - 1].flags & TF_JOIN)) b--;
    float ma = a >= 0 ? S[a].ms : 0, sa = a >= 0 ? S[a].s : 0;
    float mb = b < B->n ? S[b].ms : B->span, sb = b < B->n ? S[b].s : B->length;
    for (int i = a + 1; i < b; i++) S[i].ms = sb > sa ? ma + (mb - ma) * (S[i].s - sa) / (sb - sa) : ma;
    for (int i = 0; i < B->n; i++) {
      if (S[i].ms < 0) S[i].ms = 0;
      if (S[i].ms > B->span) S[i].ms = B->span;
      if (i && S[i].ms < S[i - 1].ms) S[i].ms = S[i - 1].ms;
    }
  }
  /* Between the overlaps it carries the shift the main road's surface gave its ends (a banked turn's sunken inside
   * edge, say), eased from one end's to the other's over its whole free length, so it never dips and climbs back; its
   * lean eases from the main road's back to its own over 25 m past each overlap. */
  {
    int a = -1, b = B->n;
    while (a + 1 < B->n && (S[a + 1].flags & TF_JOIN)) a++;
    while (b - 1 > a && (S[b - 1].flags & TF_JOIN)) b--;
    t3_vec3 oa = a >= 0 ? ofs[a] : v3_(0, 0, 0), ob = b < B->n ? ofs[b] : v3_(0, 0, 0);
    /* the height shift leaves each overlap at the slope it had there and lets that slope die away over 60 m, so the
     * road bends gently out of the main road's lean instead of kinking */
    float da = 0, db = 0;
    if (a >= 3) da = (ofs[a].y - ofs[a - 3].y) / fmaxf(S[a].s - S[a - 3].s, 0.5f);
    if (b + 3 < B->n) db = (ofs[b + 3].y - ofs[b].y) / fmaxf(S[b + 3].s - S[b].s, 0.5f);
    float s_a = a >= 0 ? S[a].s : 0, s_b = b < B->n ? S[b].s : B->length, L = fmaxf(s_b - s_a, 1.0f);
    const float Lk = fminf(60.0f, L * 0.5f);
    for (int i = a + 1; i < b; i++) {
      float x = (S[i].s - s_a) / L, k = smooth(x);
      t3_vec3 o = add(mul(oa, 1 - k), mul(ob, k));
      float xa = S[i].s - s_a, xb = s_b - S[i].s;
      if (xa < Lk) o.y += da * xa * (1 - xa / Lk) * (1 - xa / Lk);
      if (xb < Lk) o.y -= db * xb * (1 - xb / Lk) * (1 - xb / Lk);
      S[i].p = add(S[i].p, o);
    }
    const float span = 25.0f;
    for (int end = 0; end < 2; end++) {
      int last = end ? b : a, dir = end ? -1 : 1;
      if (last < 0 || last >= B->n) continue;
      for (int k = 1, j = last + dir; k <= (int)span && j > a && j < b; k++, j += dir) {
        float w = 1.0f - smooth((float)k / span);
        tsample *q = &S[j];
        t3_vec3 u = norm(add(mul(q->u, 1 - w), mul(S[last].u, w)));
        t3_vec3 r = norm(cross(q->f, u));
        q->r = r, q->u = cross(r, q->f);
        q->bank = q->bank * (1 - w) + S[last].bank * w;
      }
    }
  }
  free(ofs);
  free(own);
  /* the frames follow the shifted path: forward along it, up kept as square to it as it was */
  for (int i = 1; i + 1 < B->n; i++) {
    tsample *q = &S[i];
    if ((q->flags | S[i - 1].flags | S[i + 1].flags) & (TF_GAP | TF_LOOP)) continue;
    t3_vec3 f = norm(sub(S[i + 1].p, S[i - 1].p));
    t3_vec3 u = norm(sub(q->u, mul(f, dot(q->u, f))));
    q->f = f, q->r = norm(cross(f, u)), q->u = cross(q->r, f);
  }
  t3_vec3 lo = S[0].p, hi = S[0].p;
  for (int i = 0; i < B->n; i++) {
    float r = S[i].hw + 6.0f;
    lo = v3_(fminf(lo.x, S[i].p.x - r), fminf(lo.y, S[i].p.y - 12), fminf(lo.z, S[i].p.z - r));
    hi = v3_(fmaxf(hi.x, S[i].p.x + r), fmaxf(hi.y, S[i].p.y + 14), fmaxf(hi.z, S[i].p.z + r));
  }
  B->lo = lo, B->hi = hi;
  t->nall += B->n;
  t->nbranch++;
  cw_info("track %s: branch %s %.0f m for %.0f m of road (s %.0f to %.0f), side %d, solve %.2f m", t->circuit ? "circuit" : "course", name, B->length, B->span, B->s0, B->s1, B->side, err);
  return true;
}
