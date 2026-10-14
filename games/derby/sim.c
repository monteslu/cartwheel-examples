/* derby's bout: cars on the engine's Box3D in a walled dirt arena.
 *
 *   arena   static geometry: the dirt floor (flat, as drawn), the ring of
 *           jersey barriers (a convex hull per block with the barrier's
 *           sloped profile, so a wheel that meets it climbs the foot) and
 *           the chain-link fence behind them
 *   cars    a dynamic body each (a hull for the body below the waist with
 *           a chamfered nose and tail, a hull for the greenhouse; a
 *           full-size car's centre of mass and inertia) on four cw_vehicle3d
 *           wheels: springs, a friction circle per tyre at its contact that
 *           carves at low speed and slides when pushed, tyres that dig into
 *           the dirt when shoved sideways (what trips a T-boned car over),
 *           all-wheel drive biased to the rear, brakes, reverse, a handbrake
 *           that locks the rear and swings it round, drag
 *   crashes Box3D contacts in 3D, nothing scripted: cars shove, spin,
 *           pitch into the wall, roll when T-boned hard, ride up on each
 *           other; a car on its roof or side (or thrown out of
 *           the ring) kicks itself back onto its wheels after a moment
 *   damage  every hard hit costs the engine (hp) by the contact impulse and
 *           where it lands: the flanks are soft, the nose is armoured; it
 *           leaves a dent where it hit and wears that side's panels; a car
 *           at 0 is a wreck that stays in the arena as a heavy obstacle
 *   rules   last car running wins; or, on the clock, the most points for
 *           hits, wrecks coming back after a moment
 *
 * Deterministic: Box3D through the engine (the same bits on every host and
 * with any number of step workers), plus + - * / sqrtf and cartwheel's
 * cw_sin / cw_cos / cw_atan2 here, one seeded random stream drawn once per
 * statement. The 3D side reads this; nothing here reads it back. */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

#define DT (1.0f / TICK_HZ)
#define GRAV 9.8f
#define MU_F 1.45f        /* front tyres on dirt */
#define MU_R 1.3f         /* rear */
#define MU_HB 0.28f       /* rear with the handbrake on */
#define DRIVE_ACC 8.5f    /* m/s^2 from rest */
#define TOP_SPEED 23.0f
#define REV_SPEED 9.0f
#define BRAKE_ACC 13.0f
#define STEER_MAX 0.62f   /* radians at a crawl */
#define STEER_FAST 0.24f  /* ... at top speed */
#define STEER_RATE 3.2f   /* radians per second */
#define E_CARS 0.28f      /* restitution car on car */
#define E_WALL 0.32f
#define MU_CARS 0.45f     /* friction between bodies */
#define DMG_FLOOR 1.6f    /* impulses (tonne m/s) below this do no harm */
#define DMG_K 2.4f
#define WRECK_POINTS 100
#define RESPAWN_TICKS 180
#define COUNTDOWN_TICKS 180
#define PLOUGH 2.0f       /* a tyre shoved sideways through the dirt digs in: a T-bone trips the car over */
#define PLOUGH_SPEED 6.0f /* m/s sideways (drifts stay below it) */
#define TRAVEL 0.22f      /* suspension */
#define HERTZ 2.0f
#define RIGHT_TICKS 75    /* on its roof this long, then it kicks itself over */
#define ANG_DAMP 0.25f   /* the body's angular damping, 1/s */
#define WRECK_MASS 1.8f   /* a wreck is heavier: it digs in */
#define HIT_SPEED 1.0f    /* a contact closing slower than this (m/s) is a push, not a hit */
/* Box3D's contact impulse (cw_contact3d.impulse) to tonne m/s: it sums the
 * solve and the relax pass of every sub-step, about twice the momentum the
 * hit actually moved (measured against the cars' change of speed) */
#define BOX3D_J (0.5f / 1000)

enum { L_ARENA, L_CARS };

const car_spec CAR_SPEC[CAR_KINDS] = {
  /* full-size 1970s-80s proportions: a 5.3 m sedan on a 2.9 m wheelbase, 0.65 m wheels */
  [CAR_SEDAN] = { 2.65f, 0.975f, 1.75f, 1.36f, 0.18f, 1.5f, -1.4f, 0.325f, 0.27f, 0.85f, 0.95f, -0.12f, -1.08f, 0.62f, -1.55f, 0.60f, "sedan" },
  [CAR_WAGON] = { 2.72f, 0.985f, 1.9f, 1.43f, 0.2f, 1.5f, -1.45f, 0.325f, 0.27f, 0.86f, 0.96f, -0.06f, -2.58f, 0.64f, -2.62f, 0.62f, "wagon" },
  [CAR_PICKUP] = { 2.75f, 1.0f, 2.0f, 1.78f, 0.26f, 1.65f, -1.55f, 0.36f, 0.36f, 0.96f, 1.06f, 0.38f, -0.42f, 0.88f, -0.48f, 0.70f, "pickup" },
  /* the van: the greenhouse is its whole tall box behind the bonnet */
  [CAR_VAN] = { 2.45f, 1.0f, 2.15f, 2.0f, 0.22f, 1.6f, -1.2f, 0.35f, 0.32f, 0.9f, 1.08f, 1.2f, -2.4f, 1.72f, -2.45f, 0.58f, "van" },
};

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint32_t rnd(sim *s) {
  s->rng ^= s->rng << 13;
  s->rng ^= s->rng >> 17;
  s->rng ^= s->rng << 5;
  return s->rng;
}
static float rnd01(sim *s) { return (float)(rnd(s) & 0xffffff) / 16777216.0f; }

static void ev(sim *s, int kind, int a, int b, float x, float y, float z, float k) {
  if (s->ev_n >= (int)(sizeof s->ev / sizeof s->ev[0])) return;
  s->ev[s->ev_n++] = (struct sim_event){ kind, a, b, { x, y, z }, k };
}

/* ── geometry ───────────────────────────────────────────────────────── */

static t3_vec3 vec(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = { q.x, q.y, q.z };
  t3_vec3 t = { 2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z), 2 * (u.x * v.y - u.y * v.x) };
  return vec(v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z), v.z + q.w * t.z + (u.x * t.y - u.y * t.x));
}
static t3_vec3 qrot_inv(t3_quat q, t3_vec3 v) { return qrot((t3_quat){ -q.x, -q.y, -q.z, q.w }, v); }
static t3_quat yaw_quat(float h) {
  float s, c;
  cw_sincos(h * 0.5f, &s, &c);
  return (t3_quat){ 0, s, 0, c };
}

float car_speed(const car *c) {
  float fx, fz;
  cw_sincos(c->h, &fx, &fz);
  return c->vx * fx + c->vz * fz;
}
void car_local(const car *c, float wx, float wz, float *f, float *sd) {
  float fx, fz;
  cw_sincos(c->h, &fx, &fz);
  float dx = wx - c->x, dz = wz - c->z;
  *f = dx * fx + dz * fz;
  *sd = dx * fz - dz * fx; /* the side vector (cos h, -sin h) */
}

/* the arena: distance to the wall's inside face (+ beyond it) and the
 * outward normal, a rounded rectangle */
float arena_sd(float x, float z, float *nx, float *nz) {
  float ax = fabsf(x), az = fabsf(z);
  float qx = ax - (AW - AC), qz = az - (AL - AC);
  float d;
  if (qx > 0 && qz > 0) {
    float l = sqrtf(qx * qx + qz * qz);
    d = l - AC;
    *nx = qx / l, *nz = qz / l;
  } else if (qx > qz) {
    d = qx - AC;
    *nx = 1, *nz = 0;
  } else {
    d = qz - AC;
    *nx = 0, *nz = 1;
  }
  if (x < 0) *nx = -*nx;
  if (z < 0) *nz = -*nz;
  return d;
}

int arena_line(arena_pt *out, int max, float step, float inset) {
  int n = 0;
  float hx = AW - AC, hz = AL - AC, r = AC + inset;
  float per = 4 * hx + 4 * hz + CW_TAU * r;
  int count = (int)(per / step);
  for (int i = 0; i < count && n < max; i++) {
    float s = per * i / count;
    float x, z, tx, tz;
    /* segments: right side (+x) going +z, top-right arc, top (+z) going -x, ... */
    float L1 = 2 * hz, A = CW_TAU * r / 4, L2 = 2 * hx;
    if (s < L1) x = hx + r, z = -hz + s, tx = 0, tz = 1;
    else if ((s -= L1) < A) {
      float a = s / r, sa, ca;
      cw_sincos(a, &sa, &ca);
      x = hx + r * ca, z = hz + r * sa, tx = -sa, tz = ca;
    } else if ((s -= A) < L2) x = hx - s, z = hz + r, tx = -1, tz = 0;
    else if ((s -= L2) < A) {
      float a = 1.5707963f + s / r, sa, ca;
      cw_sincos(a, &sa, &ca);
      x = -hx + r * ca, z = hz + r * sa, tx = -sa, tz = ca;
    } else if ((s -= A) < L1) x = -hx - r, z = hz - s, tx = 0, tz = -1;
    else if ((s -= L1) < A) {
      float a = 3.1415927f + s / r, sa, ca;
      cw_sincos(a, &sa, &ca);
      x = -hx + r * ca, z = -hz + r * sa, tx = -sa, tz = ca;
    } else if ((s -= A) < L2) x = -hx + s, z = -hz - r, tx = 1, tz = 0;
    else {
      s -= L2;
      float a = 4.712389f + s / r, sa, ca;
      cw_sincos(a, &sa, &ca);
      x = hx + r * ca, z = -hz + r * sa, tx = -sa, tz = ca;
    }
    out[n++] = (arena_pt){ x, z, tx, tz };
  }
  return n;
}

/* the jersey barrier's cross section: (out from the inner face, up), inner foot first */
const float JERSEY[JERSEY_N][2] = { { 0, 0 }, { 0, 0.26f }, { 0.13f, 0.42f }, { 0.2f, 1.08f }, { 0.25f, 1.12f }, { 0.4f, 1.12f },
                                    { 0.45f, 1.08f }, { 0.52f, 0.42f }, { 0.65f, 0.26f }, { 0.65f, 0 } };

int sim_alive(const sim *s) {
  int n = 0;
  for (int i = 0; i < MAXC; i++) n += s->c[i].on && !s->c[i].wrecked;
  return n;
}

/* ── the arena in Box3D ─────────────────────────────────────────────── */

static cw_body3d static_body(cw_world3d w) {
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  return cw_body3d_create(w, &bd);
}

static void build_arena(sim *s) {
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = L_ARENA;
  /* the dirt, out under the barriers and the lot */
  s->floor = static_body(s->world);
  sd.material.friction = 0.9f;
  sd.material.restitution = 0.05f;
  cw_shape3d_box(s->floor, &sd, vec(AW + 40, 0.5f, AL + 40), cw_xform3_at(0, -0.5f, 0));
  /* the barriers: a block every 2.4 m round the line, the profile swept along it */
  s->wall = static_body(s->world);
  sd.material.friction = 0.55f;
  sd.material.restitution = E_WALL;
  static arena_pt P[256];
  int n = arena_line(P, 256, 2.4f, 0);
  for (int i = 0; i < n; i++) {
    const arena_pt *p = &P[i];
    float ox = p->tz, oz = -p->tx; /* out of the arena */
    t3_vec3 pts[2 * JERSEY_N];
    for (int e = 0; e < 2; e++)
      for (int k = 0; k < JERSEY_N; k++) {
        float a = e ? 1.2f : -1.2f, d = JERSEY[k][0];
        pts[e * JERSEY_N + k] = vec(p->x + p->tx * a + ox * d, JERSEY[k][1], p->z + p->tz * a + oz * d);
      }
    cw_shape3d_hull(s->wall, &sd, pts, 2 * JERSEY_N);
  }
  /* the chain-link fence 3 m behind the wall (scenery.c draws it): nothing gets out */
  s->fence = static_body(s->world);
  sd.material.friction = 0.6f;
  sd.material.restitution = 0.2f;
  n = arena_line(P, 256, 3.0f, 3.0f);
  for (int i = 0; i < n; i++) {
    const arena_pt *a = &P[i], *b = &P[(i + 1) % n];
    float dx = b->x - a->x, dz = b->z - a->z, l = sqrtf(dx * dx + dz * dz);
    float ang = cw_atan2(-dz, dx);
    cw_xform3 at = { vec((a->x + b->x) * 0.5f, 1.7f, (a->z + b->z) * 0.5f), yaw_quat(ang) };
    cw_shape3d_box(s->fence, &sd, vec(l * 0.5f + 0.05f, 1.7f, 0.05f), at);
  }
}

/* ── the cars in Box3D ──────────────────────────────────────────────── */

/* mass, centre of mass and inertia as a full-size car's: a centre of mass a
 * little above the bumpers and radii of gyration of a body-on-frame car
 * (most of the mass between the axles, not spread like a solid box) */
static void set_mass(car *c, float scale) {
  const car_spec *k = &CAR_SPEC[c->kind];
  float m = k->mass * 1000 * scale, L = 2 * k->hl, W = 2 * k->hw;
  float kr = 0.31f * W, kp = 0.24f * L, ky = 0.29f * L; /* roll, pitch, yaw */
  b3MassData md = { m, { 0, k->com_y, 0 }, { { m * kp * kp, 0, 0 }, { 0, m * ky * ky, 0 }, { 0, 0, m * kr * kr } } };
  b3Body_SetMassData(cw_body3d_raw(c->body), md);
}

static void build_car(sim *s, int i) {
  car *c = &s->c[i];
  const car_spec *k = &CAR_SPEC[c->kind];
  cw_body3d_def bd = cw_body3d_default_def();
  bd.angular_damping = ANG_DAMP;
  bd.user = c;
  c->body = cw_body3d_create(s->world, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = L_CARS;
  sd.material.friction = MU_CARS;
  sd.material.restitution = E_CARS;
  sd.material.user_material = 1 + (uint64_t)i;
  /* below the waist: square sides, a nose and tail chamfered at the bottom (a car rides up over them) */
  float hl = k->hl, hw = k->hw;
  t3_vec3 lo[16];
  int n = 0;
  for (int e = -1; e <= 1; e += 2)
    for (int sx = -1; sx <= 1; sx += 2) {
      lo[n++] = vec(sx * hw, k->sill, e * (hl - 0.3f));
      lo[n++] = vec(sx * hw, k->sill + 0.2f, e * hl);
      lo[n++] = vec(sx * hw, k->belt_end, e * hl);
      lo[n++] = vec(sx * hw, k->belt_mid, e * (hl - 0.9f));
    }
  cw_shape3d_hull(c->body, &sd, lo, n);
  /* the greenhouse: its foot on the waist from the cowl back to the deck, the roof narrower */
  t3_vec3 up[8];
  n = 0;
  for (int sx = -1; sx <= 1; sx += 2) {
    up[n++] = vec(sx * (hw - 0.06f), k->belt_mid - 0.02f, k->cowl);
    up[n++] = vec(sx * (hw - 0.06f), k->belt_mid - 0.02f, k->deck);
    up[n++] = vec(sx * (hw - 0.2f), k->height, k->roof_f);
    up[n++] = vec(sx * (hw - 0.2f), k->height, k->roof_b);
  }
  cw_shape3d_hull(c->body, &sd, up, n);
  set_mass(c, 1);
  /* the wheels: anchored so the springs' static sag puts the body's origin on the floor */
  cw_vehicle3d_def vd = cw_vehicle3d_default_def();
  vd.mask = (1u << L_ARENA) | (1u << L_CARS);
  vd.roll_influence = 1;
  vd.rolling_resistance = 0.0255f;
  vd.drag = 0.0008f;
  vd.max_load = 3;
  float sag = GRAV / ((CW_TAU * HERTZ) * (CW_TAU * HERTZ));
  for (int w = 0; w < 4; w++) {
    float side = w & 1 ? -1.0f : 1.0f; /* left is +x */
    int axle = w < 2 ? 0 : 1;
    int at = cw_vehicle3d_def_add_wheel(&vd, vec(side * (hw - 0.12f), k->wr + TRAVEL - sag, axle ? k->ar : k->af), axle);
    cw_wheel3d_def *wd = &vd.wheels[at];
    wd->radius = k->wr;
    wd->travel = TRAVEL;
    wd->hertz = HERTZ;
    wd->damping_ratio = 0.65f;
    wd->grip = axle ? MU_R : MU_F;
    wd->slide_grip = 0.92f;
    wd->plough_grip = PLOUGH;
    wd->plough_speed = PLOUGH_SPEED;
    wd->handbrake = axle == 1;
    wd->handbrake_grip = MU_HB / MU_R;
  }
  cw_vehicle3d_init(&c->veh, c->body, &vd);
}

/* the planar summary and the pose, from the body */
static void read_car(car *c) {
  cw_xform3 x = cw_body3d_transform(c->body);
  t3_vec3 v = cw_body3d_velocity(c->body), w = cw_body3d_angular_velocity(c->body);
  t3_vec3 f = qrot(x.q, vec(0, 0, 1)), u = qrot(x.q, vec(0, 1, 0));
  c->x = x.p.x, c->y = x.p.y, c->z = x.p.z;
  c->q = x.q;
  c->h = cw_atan2(f.x, f.z);
  c->vx = v.x, c->vz = v.z, c->w = w.y;
  c->up_y = u.y;
  const cw_wheel3d *wh = c->veh.wheel;
  c->slip_f = fmaxf(wh[0].slip_side, wh[1].slip_side);
  c->slip_r = fmaxf(wh[2].slip_side, wh[3].slip_side);
  if (wh[2].locked && wh[2].contact) c->slip_r = fmaxf(c->slip_r, fmaxf(wh[2].slip_long, wh[3].slip_long));
}

/* on its wheels at (x, lift, z) heading h, still, no history */
static void set_down(car *c, float x, float lift, float z, float h) {
  cw_vehicle3d_teleport(&c->veh, (cw_xform3){ vec(x, lift, z), yaw_quat(h) });
  read_car(c);
}

void sim_place(sim *s, int i, float x, float z, float h) {
  car *c = &s->c[i];
  if (!cw_body3d_valid(c->body)) return;
  c->steer = 0;
  set_down(c, x, 0, z, h);
  /* set down at rest: its wheels drawn on their springs' sag until the first tick casts them */
  float sag = GRAV / ((CW_TAU * HERTZ) * (CW_TAU * HERTZ));
  for (int w = 0; w < 4; w++) c->veh.wheel[w].compression = c->veh.wheel[w].prev_compression = sag;
}

void sim_wheel(const car *c, int w, t3_vec3 *pos, t3_quat *rot) {
  cw_xform3 x = cw_vehicle3d_wheel_local(&c->veh, w, 1);
  *pos = x.p;
  *rot = x.q;
}

/* ── setup ──────────────────────────────────────────────────────────── */

static void place(sim *s, int i, int slot, int n) {
  /* round a ring, facing the middle, a little off so nobody meets head on */
  float a = CW_TAU * slot / n + 0.3f;
  float sa, ca;
  cw_sincos(a, &sa, &ca);
  float r = 12.0f;
  float x = sa * r * (AW - 4) / AW * 1.25f;
  float z = ca * r * 1.45f;
  sim_place(s, i, x, z, cw_atan2(-x, -z) + 0.35f);
}

void sim_free(sim *s) {
  if (cw_world3d_valid(s->world)) cw_world3d_destroy(s->world);
  s->world = (cw_world3d){ 0 };
}

void sim_init(sim *s, const bool on[MAXC], const int kind[MAXC], int rule, int bout_secs, uint32_t seed) {
  sim_free(s);
  memset(s, 0, sizeof *s);
  s->rng = seed ? seed : 1;
  s->rule = rule;
  s->clock = bout_secs * TICK_HZ;
  s->winner = -1;
  cw_world3d_def wd = cw_world3d_default_def();
  wd.gravity = vec(0, -GRAV, 0);
  wd.workers = 0; /* every job-pool thread */
  wd.layers[L_ARENA] = "arena";
  wd.layers[L_CARS] = "cars";
  s->world = cw_world3d_create(&wd);
  build_arena(s);
  int n = 0;
  for (int i = 0; i < MAXC; i++) n += on[i];
  int k = 0;
  for (int i = 0; i < MAXC; i++) {
    car *c = &s->c[i];
    c->on = on[i];
    c->kind = kind[i] % CAR_KINDS;
    c->hp = 100;
    c->last_hit_by = -1;
    c->q = (t3_quat){ 0, 0, 0, 1 };
    c->up_y = 1;
    if (!on[i]) continue;
    build_car(s, i);
    place(s, i, k++, n > 0 ? n : 1);
  }
  s->ncars = n;
  s->alive = n;
  s->phase = PH_COUNTDOWN;
}

/* ── driving ────────────────────────────────────────────────────────── */

static void drive(sim *s, car *c, cin in) {
  const car_spec *k = &CAR_SPEC[c->kind];
  float thr = in.thr / 127.0f, st = in.steer / 127.0f;
  bool hb = in.btn & CB_HANDBRAKE;
  if (c->wrecked || s->phase != PH_GO || c->respawn > 0) thr = 0, st = 0, hb = !c->wrecked ? true : hb;
  float v = car_speed(c), speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
  /* steering: rate-limited, tighter at a crawl than flat out */
  float smax = STEER_MAX + (STEER_FAST - STEER_MAX) * clampf(speed / TOP_SPEED, 0, 1);
  float want = -st * smax; /* right turns the heading toward -x (the side vector is the car's left) */
  float step = STEER_RATE * DT;
  c->steer += clampf(want - c->steer, -step, step);
  /* throttle forward; back on the stick brakes, then reverses */
  float dr = 0, br = 0;
  float health = c->wrecked ? 0 : 0.55f + 0.45f * c->hp / 100.0f; /* a battered engine pulls less */
  if (thr > 0) {
    if (v < -0.5f) br = BRAKE_ACC * thr;
    else dr = DRIVE_ACC * thr * health * clampf(1 - v / TOP_SPEED, 0, 1);
  } else if (thr < 0) {
    if (v > 0.5f) br = BRAKE_ACC * -thr;
    else dr = DRIVE_ACC * 0.7f * thr * health * clampf(1 + v / REV_SPEED, 0, 1);
  }
  if (c->wrecked) br = BRAKE_ACC * 0.6f;
  /* all-wheel drive, rear biased; the handbrake locks the rear; a wreck's wheels are seized */
  float m = k->mass * 1000;
  cw_vehicle3d_set_axle(&c->veh, 0, c->steer, dr * 0.35f * m, br * 0.5f * m);
  cw_vehicle3d_set_axle(&c->veh, 1, 0, dr * 0.65f * m, br * 0.5f * m);
  cw_vehicle3d_set_handbrake(&c->veh, hb || c->wrecked);
  /* the engine's note: road speed through a gearbox of three */
  float gear = fabsf(v) / TOP_SPEED * 3;
  float g = gear - floorf(gear);
  float target = (thr != 0 ? 0.35f : 0.15f) + 0.55f * (gear >= 3 ? 1 : g) + (thr != 0 && fabsf(v) < 3 ? 0.25f * fabsf(thr) : 0);
  if (c->wrecked) target = 0;
  c->rpm += (target - c->rpm) * 0.12f;
  c->horn = (in.btn & CB_HORN) && !c->wrecked;
  if (c->horn && !(c->last_btn & CB_HORN)) ev(s, EV_HORN, (int)(c - s->c), -1, c->x, 1, c->z, 1);
  c->last_btn = in.btn;
}

/* on its roof or side, or thrown out of the ring: after a moment it kicks
 * itself up and over onto its wheels (twice), then it is set down */
static void self_right(sim *s, int i) {
  car *c = &s->c[i];
  float nx, nz, out = arena_sd(c->x, c->z, &nx, &nz);
  bool down = c->up_y < 0.5f, outside = out > 0.4f;
  float speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
  if (c->up_y > 0.9f && c->veh.contacts >= 3) c->rights = 0;
  if (c->wrecked || c->respawn > 0 || !(down || outside) || speed > 3) {
    c->stuck_ticks = 0;
    return;
  }
  if (++c->stuck_ticks < RIGHT_TICKS) return;
  c->stuck_ticks = 0;
  if (outside || c->rights >= 2) {
    /* set down on its wheels, inside the ring, facing the way it pointed */
    float x = c->x, z = c->z;
    if (out > -4) x -= nx * (out + 4), z -= nz * (out + 4);
    /* somewhere clear: stepping toward the middle until nothing is in the way */
    const car_spec *k = &CAR_SPEC[c->kind];
    cw_phys_filter clear = { .layers = 1u << L_CARS, .ignore_body = c->body.id };
    for (int tries = 0; tries < 8; tries++) {
      cw_qshape3d box = cw_qshape3d_box(vec(x, 1.0f, z), vec(k->hw + 0.3f, 0.9f, k->hl + 0.3f), yaw_quat(c->h));
      cw_shape3d hit;
      if (cw_world3d_overlap(s->world, &box, &clear, &hit, 1) == 0) break;
      float l = sqrtf(x * x + z * z);
      if (l < 3) x += 3.5f; /* the middle is taken: try beside it */
      else x -= x / l * 3.5f, z -= z / l * 3.5f;
    }
    set_down(c, x, 0.6f, z, c->h);
    c->rights = 0;
    ev(s, EV_RIGHT, i, -1, x, 0.6f, z, 0);
    return;
  }
  /* the kick: up off the floor and rolled toward upright, landing on its wheels */
  t3_vec3 u = qrot(c->q, vec(0, 1, 0));
  t3_vec3 axis = { -u.z, 0, u.x }; /* u x (0,1,0): turns up toward the sky */
  float al = sqrtf(axis.x * axis.x + axis.z * axis.z);
  float angle = cw_atan2(al, u.y);
  if (u.y < -0.6f) { /* on its roof: barrel-roll over its length (end over end it would land on its nose) */
    t3_vec3 f = qrot(c->q, vec(0, 0, 1));
    float fl = sqrtf(f.x * f.x + f.z * f.z), side = axis.x * f.x + axis.z * f.z < 0 ? -1.0f : 1.0f;
    axis = vec(f.x * side, 0, f.z * side), al = fl > 1e-3f ? fl : 1;
  }
  const float lift = 5.5f, air = 2 * lift / GRAV; /* seconds off the ground */
  float rate = angle / air / (1 - ANG_DAMP * air * 0.5f); /* what the body's angular damping takes off on the way */
  t3_vec3 w = { axis.x / al * rate, axis.y / al * rate, axis.z / al * rate };
  cw_body3d_set_velocity(c->body, vec(c->vx * 0.3f, lift, c->vz * 0.3f));
  cw_body3d_set_angular_velocity(c->body, w);
  c->rights++;
  ev(s, EV_RIGHT, i, -1, c->x, c->y + 0.5f, c->z, 1);
}

/* ── damage ─────────────────────────────────────────────────────────── */

static int zone_at(const car *c, float f, float sd) {
  const car_spec *k = &CAR_SPEC[c->kind];
  if (fabsf(f) / k->hl > fabsf(sd) / k->hw * 0.82f) return f > 0 ? ZONE_FRONT : ZONE_REAR;
  return sd > 0 ? ZONE_LEFT : ZONE_RIGHT; /* the side vector points left */
}
/* how much a hit to each side hurts the engine: the nose is armoured, the
 * doors are not */
static const float ZONE_K[ZONES] = { 0.55f, 0.9f, 1.5f, 1.5f };

static void wreck(sim *s, int i, int by) {
  car *c = &s->c[i];
  if (c->wrecked) return;
  c->wrecked = true;
  c->hp = 0;
  c->wreck_ticks = 0;
  s->wrecks++;
  set_mass(c, WRECK_MASS);
  if (by >= 0 && by != i) {
    s->c[by].wrecks++;
    s->c[by].points += WRECK_POINTS;
  }
  ev(s, EV_WRECK, i, by, c->x, c->y + 0.8f, c->z, 1);
  if (s->rule == RULE_POINTS) c->respawn = RESPAWN_TICKS;
}

/* j: the hit's impulse in tonne m/s, at the world point p */
static void hurt(sim *s, int i, float j, t3_vec3 p, int by, float scale) {
  car *c = &s->c[i];
  if (!c->on) return;
  /* where it landed, in the car's own frame */
  t3_vec3 l = qrot_inv(c->q, vec(p.x - c->x, p.y - c->y, p.z - c->z));
  float f = l.z, sd = l.x;
  int zone = zone_at(c, f, sd);
  float dmg = fmaxf(0, j - DMG_FLOOR) * DMG_K * ZONE_K[zone] * scale;
  if (dmg <= 0) return;
  c->zone[zone] = fminf(1, c->zone[zone] + dmg / 60.0f);
  /* a dent where it landed, deeper the harder */
  if (j > DMG_FLOOR + 0.6f) {
    const car_spec *k = &CAR_SPEC[c->kind];
    float depth = fminf(0.32f, (j - DMG_FLOOR) * 0.022f);
    dent d = { clampf(f, -k->hl, k->hl), clampf(sd, -k->hw, k->hw), clampf(l.y, k->sill + 0.15f, k->height - 0.15f), depth };
    /* a dent near an old one deepens it; a full list replaces the shallowest */
    int at = -1;
    for (int n = 0; n < c->ndent; n++) {
      float df = c->dents[n].f - d.f, ds = c->dents[n].s - d.s;
      if (df * df + ds * ds < 0.25f) at = n;
    }
    if (at >= 0) c->dents[at].depth = fminf(0.42f, c->dents[at].depth + depth * 0.6f);
    else if (c->ndent < MAX_DENTS) c->dents[c->ndent++] = d;
    else {
      int m = 0;
      for (int n = 1; n < MAX_DENTS; n++)
        if (c->dents[n].depth < c->dents[m].depth) m = n;
      if (c->dents[m].depth < depth) c->dents[m] = d;
    }
    ev(s, EV_DENT, i, zone, p.x, p.y, p.z, depth);
  }
  c->taken += dmg;
  if (by >= 0 && by != i) {
    s->c[by].dealt += dmg;
    s->c[by].points += (int)(dmg + 0.5f);
    c->last_hit_by = by;
    c->last_hit_tick = s->tick;
  }
  if (c->wrecked) return;
  c->hp -= dmg;
  if (c->hp <= 0) {
    /* a wall finishing a car recently hit counts for whoever hit it */
    int credit = by >= 0 ? by : (c->last_hit_by >= 0 && s->tick - c->last_hit_tick < 180 ? c->last_hit_by : -1);
    wreck(s, i, credit);
  }
}

/* one body's contacts with one other thing, summed over shape pairs */
typedef struct touch {
  float j;          /* N*s */
  t3_vec3 at, n;    /* impulse-weighted point; normal from the car toward the other */
  float slide;      /* the fastest sliding speed across the contact, m/s */
  float approach;   /* the fastest closing speed before the step solved it, m/s */
  bool any;
} touch;
static void add_touch(touch *t, const cw_contact3d *k) {
  float w = k->impulse > 0 ? k->impulse : 1e-6f;
  float before = t->any ? (t->j > 0 ? t->j : 1e-6f) : 0;
  float tot = before + w, a = before / tot, b = w / tot;
  t->at = vec(t->at.x * a + k->point.x * b, t->at.y * a + k->point.y * b, t->at.z * a + k->point.z * b);
  t->n = vec(t->n.x * a + k->normal.x * b, t->n.y * a + k->normal.y * b, t->n.z * a + k->normal.z * b);
  float sl = sqrtf(k->slide.x * k->slide.x + k->slide.y * k->slide.y + k->slide.z * k->slide.z);
  if (sl > t->slide) t->slide = sl;
  if (k->approach > t->approach) t->approach = k->approach;
  t->j += k->impulse;
  t->any = true;
}

/* the step's crashes, scrapes and landings, from Box3D's contact impulses */
static void contacts(sim *s, const float pvx[MAXC], const float pvz[MAXC]) {
  static touch pair[MAXC][MAXC];
  touch wall[MAXC], floor_[MAXC];
  memset(pair, 0, sizeof pair);
  memset(wall, 0, sizeof wall);
  memset(floor_, 0, sizeof floor_);
  for (int i = 0; i < MAXC; i++) {
    car *c = &s->c[i];
    if (!c->on) continue;
    cw_contact3d k[32];
    int n = cw_body3d_contacts(c->body, k, 32);
    for (int e = 0; e < n; e++) {
      uint64_t o = k[e].other.id;
      if (o == s->floor.id) add_touch(&floor_[i], &k[e]);
      else if (o == s->wall.id || o == s->fence.id) add_touch(&wall[i], &k[e]);
      else
        for (int j = i + 1; j < MAXC; j++)
          if (s->c[j].on && s->c[j].body.id == o) add_touch(&pair[i][j], &k[e]);
    }
  }
  bool rammed[MAXC] = { 0 };
  for (int i = 0; i < MAXC; i++)
    for (int j = i + 1; j < MAXC; j++) {
      touch *t = &pair[i][j];
      if (!t->any) continue;
      float J = t->j * BOX3D_J; /* tonne m/s */
      if (t->slide > 4 && J < 2) {
        ev(s, EV_SCRAPE, i, j, t->at.x, t->at.y, t->at.z, t->slide);
        continue;
      }
      if (J < 0.4f || t->approach < HIT_SPEED) continue; /* pushing, resting on, not a hit */
      rammed[i] = rammed[j] = true;
      s->crashes++;
      if (J > s->max_hit) s->max_hit = J;
      ev(s, EV_CRASH, i, j, t->at.x, t->at.y, t->at.z, J);
      /* who hit whom: the faster one into the contact is the attacker, and both get hurt */
      float nl = sqrtf(t->n.x * t->n.x + t->n.z * t->n.z);
      float nx = nl > 1e-4f ? t->n.x / nl : 0, nz = nl > 1e-4f ? t->n.z / nl : 0;
      float sa = pvx[i] * nx + pvz[i] * nz, sb = -(pvx[j] * nx + pvz[j] * nz);
      hurt(s, i, J, t->at, j, sa > sb ? 0.75f : 1.0f);
      hurt(s, j, J, t->at, i, sb > sa ? 0.75f : 1.0f);
    }
  for (int i = 0; i < MAXC; i++) {
    touch *t = &wall[i];
    if (t->any) {
      float J = t->j * BOX3D_J;
      if (J > 1.2f && t->approach > HIT_SPEED) {
        s->wall_hits++;
        ev(s, EV_WALL, i, -1, t->at.x, t->at.y, t->at.z, J);
        /* rammed into the wall: the ram is the damage, the wall only holds it */
        if (!rammed[i]) hurt(s, i, J * 0.55f, t->at, -1, 1);
      } else if (t->slide > 3)
        ev(s, EV_SCRAPE, i, -1, t->at.x, t->at.y, t->at.z, t->slide);
    }
    t = &floor_[i];
    if (t->any && t->j * BOX3D_J > 2.5f && t->approach > 2 * HIT_SPEED) {
      float J = t->j * BOX3D_J;
      ev(s, EV_LAND, i, -1, t->at.x, t->at.y, t->at.z, J);
      hurt(s, i, J * 0.35f, t->at, -1, 1);
    }
  }
}

/* ── the tick ───────────────────────────────────────────────────────── */

static uint32_t hashf(uint32_t h, float f) {
  uint32_t u;
  memcpy(&u, &f, 4);
  return (h ^ u) * 16777619u;
}

void sim_step(sim *s, const cin in[MAXC]) {
  s->tick++;
  s->phase_ticks++;
  s->ev_n = 0;
  if (s->phase == PH_COUNTDOWN) {
    if (s->phase_ticks % 60 == 1) ev(s, EV_BEEP, -1, -1, 0, 0, 0, (float)(3 - s->phase_ticks / 60));
    if (s->phase_ticks >= COUNTDOWN_TICKS) {
      s->phase = PH_GO;
      s->phase_ticks = 0;
      ev(s, EV_GO, -1, -1, 0, 0, 0, 1);
    }
  }
  float pvx[MAXC] = { 0 }, pvz[MAXC] = { 0 };
  for (int i = 0; i < MAXC; i++) {
    car *c = &s->c[i];
    if (!c->on) continue;
    pvx[i] = c->vx, pvz[i] = c->vz;
    if (c->respawn > 0 && --c->respawn == 0) {
      /* back in the points ruleset: patched up where it died */
      c->wrecked = false;
      c->hp = 100;
      for (int z = 0; z < ZONES; z++) c->zone[z] *= 0.5f;
      set_mass(c, 1);
      cw_body3d_set_velocity(c->body, vec(0, 0, 0));
      cw_body3d_set_angular_velocity(c->body, vec(0, 0, 0));
      ev(s, EV_RESPAWN, i, -1, c->x, c->y + 0.5f, c->z, 1);
    }
    drive(s, c, in[i]);
    self_right(s, i);
    cw_vehicle3d_update(&c->veh, DT);
    if (c->wrecked) c->wreck_ticks++;
  }
  cw_world3d_step(s->world, DT);
  for (int i = 0; i < MAXC; i++) {
    car *c = &s->c[i];
    if (!c->on) continue;
    bool was_up = c->up_y > 0;
    read_car(c);
    if (was_up && c->up_y <= 0) s->flips++, c->flips++;
    /* acceleration in the car's frame: the bumpers and the bonnet swing with it */
    float fx, fz;
    cw_sincos(c->h, &fx, &fz);
    float dvx = c->vx - pvx[i], dvz = c->vz - pvz[i];
    float ax = clampf((dvx * fx + dvz * fz) / DT, -30, 30), as = clampf((dvx * fz - dvz * fx) / DT, -30, 30);
    c->ax += (ax - c->ax) * 0.3f, c->az += (as - c->az) * 0.3f;
  }
  contacts(s, pvx, pvz);
  /* the rules */
  s->alive = sim_alive(s);
  if (s->phase == PH_GO) {
    if (s->clock > 0) s->clock--;
    bool over = false;
    if (s->rule == RULE_LAST_RUNNING && s->alive <= 1) over = true;
    if (s->clock == 0) over = true;
    if (over) {
      int best = -1;
      for (int i = 0; i < MAXC; i++) {
        const car *c = &s->c[i];
        if (!c->on) continue;
        if (s->rule == RULE_LAST_RUNNING) {
          if (c->wrecked) continue;
          if (best < 0 || c->hp > s->c[best].hp) best = i;
        } else if (best < 0 || c->points > s->c[best].points) best = i;
      }
      s->winner = best;
      s->phase = PH_OVER;
      s->phase_ticks = 0;
      ev(s, EV_OVER, best, -1, 0, 0, 0, 1);
    }
  }
  /* the state's hash: what the gates compare across hosts (the world's own
   * hash covers every body's motion and contact event) */
  uint32_t h = 2166136261u;
  for (int i = 0; i < MAXC; i++) {
    const car *c = &s->c[i];
    if (!c->on) continue;
    h = hashf(h, c->x), h = hashf(h, c->y), h = hashf(h, c->z), h = hashf(h, c->h), h = hashf(h, c->vx), h = hashf(h, c->vz);
    h = hashf(h, c->q.x), h = hashf(h, c->q.y), h = hashf(h, c->q.z), h = hashf(h, c->q.w), h = hashf(h, c->hp);
  }
  h = (h ^ cw_world3d_stats(s->world).hash) * 16777619u;
  h = (h ^ (uint32_t)s->tick) * 16777619u;
  s->hash = h;
  (void)rnd01;
}
