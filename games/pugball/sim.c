/* pugball's match: car-ball with pugs for cars, on Box3D.
 *
 *   arena   static Box3D geometry (arena.c): the ramped bowl, its rounded
 *           corners and the two goal boxes as one welded triangle mesh
 *   ball    a dynamic sphere: gravity, damping, restitution, rolling
 *           resistance, and an extra kick on every touch (a contact hit
 *           event) that grows with the closing speed
 *   pugs    dynamic bodies on cartwheel's vehicle controller, plus the
 *           pug's own arcade moves on top: a hold-to-go-higher jump, a
 *           double jump or a dodge flip, air pitch / yaw / roll, boost from
 *           a meter refilled by pads, supersonic and demolitions, and a
 *           light stick force that holds a driving pug to walls while
 *           gravity still peels it off ceilings and slow walls
 *   match   kickoffs from fixed spots, a clock that only runs in play,
 *           goals, golden-goal overtime, and the stats behind the MVP
 *
 * One world step per 60 Hz tick (Box3D sub-steps inside it) on the job
 * pool. Deterministic: Box3D gives the same result at any worker count and
 * on every host, and the game's own arithmetic is + - * / and sqrtf with
 * cartwheel's cw_sin / cw_cos / cw_atan2, one seeded random stream drawn
 * once per statement. The bodies' state is copied into the sim struct
 * after every step: the 3D side, the AI and the goal replay read that. */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/physics3d.h"
#include "cartwheel/vehicle3d.h"
#include "game.h"

#define DT (1.0f / TICK_HZ)
#define GRAV 8.0f
#define STICK 4.5f          /* m/s^2 pressing a driving pug into its surface */
#define STICK_MIN 3.0f      /* m/s: slower, the stick fades out (full 4 m/s faster) */
#define MAX_SPEED 23.0f
#define DRIVE_MAX 13.0f     /* throttle alone */
#define ALIGN 40.0f         /* rad/s per radian a driving pug turns to lie on its surface */
#define BOOST_ACC 22.0f
#define BOOST_USE (100.0f / 3.0f / TICK_HZ)
#define SUPERSONIC 19.5f
#define JUMP_V 5.2f
#define JUMP_HOLD_ACC 14.0f
#define DOUBLE_V 4.6f
#define DODGE_V 7.5f
#define BALL_E 0.62f
#define BALL_MAX 42.0f
#define BALL_MASS 1.0f
#define PUG_MASS 3.0f
#define KICKOFF_TICKS 180
#define GOAL_TICKS 210

/* collision layers */
enum { L_ARENA, L_BALL, L_PUG, L_FRAME, L_WRECK };

/* the live match's physics; one match runs at a time (the replay's copies
 * of the sim struct never step) */
static struct {
  cw_world3d w;
  cw_body3d arena, ball, pug[MAXP];
  cw_shape3d pug_shape[MAXP], frame_shape[MAXP];
  cw_vehicle3d car[MAXP];
  bool wreck[MAXP];         /* demolished: tumbling as a plain body */
  bool ball_live;
  t3_vec3 pug_v0[MAXP];     /* velocities before the step: who closed on whom */
  bool sonic0[MAXP];
  t3_vec3 ball0, ball_v0;
} PH;

/* ── vectors and quaternions ─────────────────────────────────────────── */

float v3len(t3_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
t3_vec3 v3norm(t3_vec3 a) {
  float l = v3len(a);
  return l > 1e-8f ? v3mul(a, 1.0f / l) : (t3_vec3){ 0, 0, 0 };
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float minf(float a, float b) { return a < b ? a : b; }

static t3_quat qaxis(t3_vec3 axis, float angle) { /* axis unit */
  float s, c;
  cw_sincos(angle * 0.5f, &s, &c);
  return (t3_quat){ axis.x * s, axis.y * s, axis.z * s, c };
}
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = { q.x, q.y, q.z };
  t3_vec3 t = v3mul(v3cross(u, v), 2);
  return v3add(v3add(v, v3mul(t, q.w)), v3cross(u, t));
}
static t3_quat qyaw(float yaw) { return qaxis((t3_vec3){ 0, 1, 0 }, yaw); }

t3_vec3 pug_fwd(const pug *u) { return qrot(u->q, (t3_vec3){ 0, 0, 1 }); }
t3_vec3 pug_up(const pug *u) { return qrot(u->q, (t3_vec3){ 0, 1, 0 }); }
t3_vec3 pug_right(const pug *u) { return v3cross(pug_fwd(u), pug_up(u)); } /* looking along +z with y up, right is -x */

static uint32_t rnd(sim *s) {
  uint32_t x = s->rng ? s->rng : 0x9e3779b9u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->rng = x;
  return x;
}

static void event(sim *s, int kind, int a, int b, t3_vec3 at, float k) {
  if (s->ev_n >= (int)(sizeof s->ev / sizeof s->ev[0])) return;
  s->ev[s->ev_n++] = (struct sim_event){ kind, a, b, at, k };
}

/* ── the vehicle: four wheels on cartwheel's vehicle controller ────── */

/* a pug's legs as wheels: small, stiff, nearly no sideways slip (arcade
 * grip of several g), all four driven. The powerslide tightens the turn
 * (drive() asks the front legs for a third more) rather than loosening the
 * back legs: at this grip that spins a pug out instead of carving */
#define WHEEL_R 0.16f
#define WHEELBASE 0.64f /* inside the frame, so a pug nosed into a wall still stands on all four */
#define TRAVEL 0.2f
#define FRAME_HY 0.14f /* the frame's half height, on the back */
#define TYRE_GRIP 6.0f
static void car_make(int i) {
  cw_vehicle3d_def vd = cw_vehicle3d_default_def();
  vd.mask = 1u << L_ARENA;
  vd.rolling_resistance = 0;
  vd.drag = 0;
  vd.roll_influence = 0.2f;
  const float hz = 3.5f, rest = GRAV / (4 * 3.1415927f * 3.1415927f * hz * hz); /* the springs' sag at rest */
  float ay = -RIDE + TRAVEL - rest + WHEEL_R;
  for (int k = 0; k < 4; k++) {
    int w = cw_vehicle3d_def_add_wheel(&vd, (t3_vec3){ k & 1 ? -0.3f : 0.3f, ay, k < 2 ? WHEELBASE * 0.5f : -WHEELBASE * 0.5f }, k < 2 ? 0 : 1);
    cw_wheel3d_def *wd = &vd.wheels[w];
    wd->radius = WHEEL_R;
    wd->travel = TRAVEL;
    wd->hertz = hz;
    wd->damping_ratio = 1.0f;
    wd->grip = TYRE_GRIP;
    wd->slide_grip = 0.85f;
    wd->lateral_stiffness = 1;
  }
  vd.max_load = 4;
  cw_vehicle3d_init(&PH.car[i], PH.pug[i], &vd);
}

/* the surface under the pug after the last step: its normal, when the pug's
 * underside is within reach of it (a little past where the legs reach, so
 * the stick force pulls it back down over a bump); or, tipped over, the
 * floor it lies on */
static bool ground(const pug *u, t3_vec3 *n) {
  cw_phys_filter f = cw_phys_filter_layers(1u << L_ARENA);
  cw_ray_hit3d h;
  if (cw_world3d_raycast(PH.w, u->p, v3mul(pug_up(u), -(RIDE + 0.22f)), &f, &h)) {
    *n = h.normal;
    return true;
  }
  if (cw_world3d_raycast(PH.w, u->p, (t3_vec3){ 0, -(RIDE + 0.4f), 0 }, &f, &h)) {
    *n = h.normal;
    return true;
  }
  return false;
}

/* the surface a tenth of a second ahead of a driving pug, under its feet there */
static bool surface_ahead(const pug *u, t3_vec3 *n) {
  t3_vec3 up = pug_up(u);
  t3_vec3 vt = v3sub(u->v, v3mul(up, v3dot(u->v, up)));
  if (v3dot(vt, vt) < 4) return false;
  t3_vec3 from = v3add(u->p, v3add(v3mul(vt, 0.1f), v3mul(up, 0.3f)));
  cw_phys_filter f = cw_phys_filter_layers(1u << L_ARENA);
  cw_ray_hit3d h;
  if (!cw_world3d_raycast(PH.w, from, v3mul(up, -(RIDE + 0.9f)), &f, &h)) return false;
  *n = h.normal;
  return true;
}

/* throttle, brake and reverse as forces, steering that tightens in a powerslide */
static void drive(int i, const pug *u, float thr, float steer, bool slide) {
  cw_vehicle3d *c = &PH.car[i];
  float spd = v3dot(u->v, pug_fwd(u));
  float acc = 0, brake = 0;
  if (thr > 0.05f) {
    if (spd < -0.3f) brake = 30;
    else if (spd < DRIVE_MAX) acc = thr * (16 - 13 * clampf(spd / DRIVE_MAX, 0, 1));
  } else if (thr < -0.05f) {
    if (spd > 0.3f) brake = 30;
    else if (spd > -9) acc = thr * 13;
  } else brake = 3.5f;
  /* the turn rate the stick asks for at this speed, as a front-wheel angle */
  float aspd = fabsf(spd);
  float rate = (3.6f - 1.8f * clampf(aspd / 22, 0, 1)) * (slide ? 1.35f : 1) * clampf(aspd / 1.5f + 0.25f, 0, 1);
  float ang = cw_atan2(rate * WHEELBASE, maxf(aspd, 0.5f));
  if (ang > 0.6f) ang = 0.6f;
  float st = -steer * ang;
  /* the push goes through the legs that are down (the last update's contacts) */
  int front = c->wheel[0].contact + c->wheel[1].contact, back = c->wheel[2].contact + c->wheel[3].contact;
  float kf = front + back ? (float)front / (float)(front + back) : 0.5f;
  cw_vehicle3d_set_axle(c, 0, st, acc * PUG_MASS * kf, brake * PUG_MASS * 0.5f);
  cw_vehicle3d_set_axle(c, 1, 0, acc * PUG_MASS * (1 - kf), brake * PUG_MASS * 0.5f);
}

/* ── the world ───────────────────────────────────────────────────────── */

bool arena_clear(t3_vec3 from, t3_vec3 to, float r) {
  if (!cw_world3d_valid(PH.w)) return true;
  cw_phys_filter f = cw_phys_filter_layers(1u << L_ARENA);
  cw_ray_hit3d h;
  if (cw_world3d_raycast(PH.w, from, v3sub(to, from), &f, &h)) return false;
  cw_qshape3d q = cw_qshape3d_sphere(to, r);
  cw_shape3d out;
  return cw_world3d_overlap(PH.w, &q, &f, &out, 1) == 0;
}

static int pug_of(cw_body3d b) {
  for (int i = 0; i < MAXP; i++)
    if (PH.pug[i].id && PH.pug[i].id == b.id) return i;
  return -1;
}

static void world_make(const sim *s) {
  if (cw_world3d_valid(PH.w)) cw_world3d_destroy(PH.w);
  memset(&PH, 0, sizeof PH);
  cw_world3d_def d = cw_world3d_default_def();
  d.gravity = (t3_vec3){ 0, -GRAV, 0 };
  d.workers = 0;                  /* every job-pool thread: Box3D gives the same result at any count */
  d.substeps = 4;
  d.hit_speed = 0.5f;             /* touches and bounces come from hit events */
  d.restitution_threshold = 1.2f; /* a ball barely moving into the floor settles */
  d.sleep = false;
  d.layers[L_ARENA] = "arena";
  d.layers[L_BALL] = "ball";
  d.layers[L_PUG] = "pug";
  d.layers[L_FRAME] = "frame";
  d.layers[L_WRECK] = "wreck";
  PH.w = cw_world3d_create(&d);

  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = L_ARENA;
  sd.material.friction = 0.6f;
  PH.arena = arena_create(PH.w, &sd);

  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = (t3_vec3){ 0, RB, 0 };
  bd.linear_damping = 0.03f;
  bd.angular_damping = 0.3f;
  bd.bullet = true;
  bd.sleep = false;
  bd.interpolate = false;
  bd.name = "ball";
  PH.ball = cw_body3d_create(PH.w, &bd);
  PH.ball_live = true;
  sd = cw_shape3d_default_def();
  sd.layer = L_BALL;
  sd.mask = (1u << L_ARENA) | (1u << L_PUG);
  sd.density = BALL_MASS / (4.0f / 3.0f * 3.1415927f * RB * RB * RB);
  sd.material.friction = 0.3f;
  sd.material.restitution = BALL_E;
  sd.material.rolling_resistance = 0.06f;
  sd.hit_events = true;
  cw_shape3d_sphere(PH.ball, &sd, (t3_vec3){ 0, 0, 0 }, RB);

  for (int i = 0; i < MAXP; i++) {
    if (!s->p[i].on) continue;
    bd = cw_body3d_default_def();
    bd.position = (t3_vec3){ 0, RIDE, (float)i * 3 - 8 };
    bd.sleep = false;
    bd.interpolate = false;
    bd.name = "pug";
    PH.pug[i] = cw_body3d_create(PH.w, &bd);
    /* the hitbox the ball and the other pugs meet */
    sd = cw_shape3d_default_def();
    sd.layer = L_PUG;
    sd.mask = (1u << L_BALL) | (1u << L_PUG);
    sd.density = PUG_MASS / (8 * PHX * PHY * PHZ);
    sd.material.friction = 0.3f;
    sd.material.restitution = 0.15f;
    sd.hit_events = true;
    PH.pug_shape[i] = cw_shape3d_box(PH.pug[i], &sd, (t3_vec3){ PHX, PHY, PHZ }, cw_xform3_identity());
    /* the frame the arena meets: shallower and shorter than the hitbox, inside
     * the legs, so they take a hard landing or a ramp met fast, not the belly */
    sd = cw_shape3d_default_def();
    sd.layer = L_FRAME;
    sd.mask = 1u << L_ARENA;
    sd.density = 0;
    sd.material.friction = 0.2f;
    sd.material.restitution = 0.15f;
    PH.frame_shape[i] = cw_shape3d_box(PH.pug[i], &sd, (t3_vec3){ 0.3f, FRAME_HY, 0.3f }, cw_xform3_at(0, FRAME_HY, 0));
    /* the weight sits low, between the legs */
    b3MassData md = b3Body_GetMassData(cw_body3d_raw(PH.pug[i]));
    md.center = (b3Vec3){ 0, -0.15f, 0 };
    b3Body_SetMassData(cw_body3d_raw(PH.pug[i]), md);
    car_make(i);
  }
}

/* the ball in play, or held out of the simulation (kickoff, goal, full time) */
static void ball_live(bool on) {
  if (on == PH.ball_live) return;
  PH.ball_live = on;
  if (on) b3Body_Enable(cw_body3d_raw(PH.ball));
  else b3Body_Disable(cw_body3d_raw(PH.ball));
}

/* a pug in play, or a demolished one tumbling off as a wreck that only meets the arena */
static void pug_wreck(int i, bool wreck) {
  if (PH.wreck[i] == wreck) return;
  PH.wreck[i] = wreck;
  cw_vehicle3d_set_active(&PH.car[i], !wreck);
  cw_shape3d_set_layer(PH.pug_shape[i], wreck ? L_WRECK : L_PUG, wreck ? 0 : (1u << L_BALL) | (1u << L_PUG));
}

/* ── setup ───────────────────────────────────────────────────────────── */

int team_size(const sim *s, int team) {
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += s->p[i].on && s->p[i].team == team;
  return n;
}

static void add_pads(sim *s) {
  static const float BIG[3][2] = { { AW - 3, 0 }, { AW - 4.5f, AL - 5 }, { -(AW - 4.5f), AL - 5 } };
  static const float SMALL[7][2] = { { 0, 25 }, { 9, 25 }, { 15, 17 }, { 6, 14 }, { 17, 8 }, { 0, 9 }, { 10, 0 } };
  s->npads = 0;
  for (int i = 0; i < 3; i++)
    for (int m = 0; m < 2; m++) {
      if (i == 0) s->pads[s->npads++] = (boost_pad){ m ? -BIG[i][0] : BIG[i][0], 0, true, 0 };
      else s->pads[s->npads++] = (boost_pad){ BIG[i][0], m ? -BIG[i][1] : BIG[i][1], true, 0 };
    }
  for (int i = 0; i < 7; i++)
    for (int mx = 0; mx < 2; mx++)
      for (int mz = 0; mz < 2; mz++) {
        float x = SMALL[i][0], z = SMALL[i][1];
        if ((mx && x == 0) || (mz && z == 0)) continue;
        s->pads[s->npads++] = (boost_pad){ mx ? -x : x, mz ? -z : z, false, 0 };
      }
}

/* kickoff spots for blue (red is the point reflection): diagonal left /
 * right, off-centre left / right, straight back */
static const float SPOT[5][2] = { { -12.5f, -21 }, { 12.5f, -21 }, { -2.5f, -26.5f }, { 2.5f, -26.5f }, { 0, -28.5f } };

/* a pug standing at (x, z) facing yaw, at rest, with its jumps back */
static void place_pug(sim *s, int i, float x, float z, float yaw) {
  pug *u = &s->p[i];
  pug_wreck(i, false);
  u->dead = 0;
  u->p = (t3_vec3){ x, RIDE, z };
  u->v = u->w = (t3_vec3){ 0, 0, 0 };
  u->q = qyaw(yaw);
  u->grounded = true;
  u->n = (t3_vec3){ 0, 1, 0 };
  u->air_ticks = 0;
  u->jump_ticks = -1;
  u->jump_held = u->second = false;
  u->dodge_ticks = 0;
  u->boosting = u->sliding = u->supersonic = false;
  u->boost = 33.3f;
  cw_vehicle3d_set_axle(&PH.car[i], 0, 0, 0, 0);
  cw_vehicle3d_set_axle(&PH.car[i], 1, 0, 0, 0);
  cw_vehicle3d_teleport(&PH.car[i], (cw_xform3){ u->p, u->q });
}

static void ball_place(sim *s, t3_vec3 at) {
  s->ball = at;
  s->ball_v = s->ball_spin = (t3_vec3){ 0, 0, 0 };
  cw_body3d_set_transform(PH.ball, (cw_xform3){ s->ball, { 0, 0, 0, 1 } });
  cw_body3d_set_velocity(PH.ball, s->ball_v);
  cw_body3d_set_angular_velocity(PH.ball, s->ball_spin);
}
void sim_place_pug(sim *s, int i, float x, float z, float yaw) { place_pug(s, i, x, z, yaw); }
void sim_place_ball(sim *s, t3_vec3 at) { ball_place(s, at); }

static void kickoff(sim *s) {
  s->phase = PH_KICKOFF;
  s->phase_ticks = 0;
  s->ball_low = s->ball_rest = true;
  ball_live(false);
  ball_place(s, (t3_vec3){ 0, RB, 0 });
  s->last_touch = s->prev_touch = -1;
  uint32_t r = rnd(s);
  s->kickoff_style = (int)(r % 5u);
  for (int t = 0; t < 2; t++) {
    int n = team_size(s, t), k = 0;
    int spots[3];
    if (n == 1) spots[0] = s->kickoff_style;
    else if (n == 2) {
      spots[0] = s->kickoff_style & 1 ? 2 : 0;
      spots[1] = s->kickoff_style & 1 ? 3 : 1;
    } else spots[0] = 0, spots[1] = 1, spots[2] = 4;
    for (int i = 0; i < MAXP; i++) {
      pug *u = &s->p[i];
      if (!u->on || u->team != t) continue;
      int sp = spots[k < 3 ? k : 2];
      k++;
      float x = SPOT[sp][0], z = SPOT[sp][1];
      if (t == TEAM_RED) x = -x, z = -z;
      place_pug(s, i, x, z, cw_atan2(-x, -z)); /* face the centre spot */
    }
  }
  for (int i = 0; i < s->npads; i++) s->pads[i].cool = 0;
  event(s, EV_KICKOFF, 0, 0, s->ball, 0);
}

void sim_init(sim *s, const bool on[MAXP], int match_secs, uint32_t seed) {
  memset(s, 0, sizeof *s);
  s->rng = seed * 2654435761u + 0x1234567u;
  s->match_ticks = match_secs * TICK_HZ;
  s->clock = s->match_ticks;
  s->winner = -1;
  s->scorer = s->assister = -1;
  s->goal_team = -1;
  for (int i = 0; i < MAXP; i++) {
    s->p[i].on = on[i];
    s->p[i].team = i & 1; /* even slots blue, odd slots red */
  }
  for (int i = 0; i < MAXP; i++) s->nplayers += on[i];
  world_make(s);
  add_pads(s);
  kickoff(s);
}

/* ── shots and saves: is the ball on its way into a goal? ────────────── */

static bool heading_in(t3_vec3 b, t3_vec3 v, int goal_sign) {
  float gz = goal_sign * AL;
  if (v.z * goal_sign < 2.5f) return false;
  float t = (gz - b.z) / v.z;
  if (t < 0 || t > 2.5f) return false;
  float x = b.x + v.x * t, y = b.y + v.y * t - 0.5f * GRAV * t * t;
  if (y < RB) y = RB; /* it may bounce on the way: the floor is fine */
  return fabsf(x) < GW + 0.3f && y < GH + 0.6f;
}

/* ── one pug, before the step ────────────────────────────────────────── */

static void respawn(sim *s, int i) {
  pug *u = &s->p[i];
  float side = u->team == TEAM_BLUE ? -1.0f : 1.0f;
  float x = (float)((i / 2) - 1) * 4.0f;
  place_pug(s, i, x, side * (AL - 6), side < 0 ? 0 : 3.1415927f);
  event(s, EV_RESPAWN, i, 0, u->p, 0);
}

static void control(sim *s, int i, pin in) {
  pug *u = &s->p[i];
  cw_body3d b = PH.pug[i];
  bool press_jump = (in.btn & PB_JUMP) && !(u->last_btn & PB_JUMP);
  float thr = in.thr / 127.0f, steer = in.steer / 127.0f, pitch = in.pitch / 127.0f;
  t3_vec3 g = { 0, -GRAV, 0 };
  t3_vec3 f = pug_fwd(u), up = pug_up(u), rt = v3cross(f, up);
  u->sliding = (in.btn & PB_SLIDE) != 0;

  /* on a surface it can drive on? (gravity must not pull it off harder than the stick holds) */
  t3_vec3 n = up;
  bool touching = ground(u, &n);
  /* the stick force holds a pug that is moving; a slow one peels off walls */
  float stick = STICK * clampf((v3len(u->v) - STICK_MIN) / 4, 0, 1);
  float press = v3dot(g, v3mul(n, -1)) + stick;
  bool can = touching && v3dot(up, n) > 0.55f && v3dot(u->v, n) < 2.0f && press > 0 && !(u->jump_ticks >= 0 && u->jump_ticks < 6);
  if (can && !u->grounded) {
    if (u->air_ticks > 12) event(s, EV_LAND, i, 0, u->p, clampf(-v3dot(u->v, n) / 10, 0, 1));
    u->jump_ticks = -1;
    u->dodge_ticks = 0;
  }
  u->grounded = can;

  if (u->grounded) {
    u->n = n;
    u->air_ticks = 0;
    u->second = false;
    drive(i, u, thr, steer, u->sliding);
    cw_body3d_apply_force(b, v3mul(n, -stick * PUG_MASS), NULL);
    /* the legs take whatever the surface asks: no speed into it, and the body
     * turns with it, so a ramp met fast is driven up, not hit */
    t3_vec3 sn = n;
    for (int k = 0; k < 4; k++)
      if (PH.car[i].wheel[k].contact) sn = v3add(sn, PH.car[i].wheel[k].normal);
    /* and where it is going: a ramp ahead turns it before the legs get there */
    t3_vec3 ahead;
    if (surface_ahead(u, &ahead)) sn = v3add(sn, v3mul(ahead, 2));
    sn = v3norm(sn);
    float vn = v3dot(u->v, sn);
    if (vn < -1) cw_body3d_set_velocity(b, v3sub(u->v, v3mul(sn, vn + 1)));
    t3_vec3 ax = v3cross(up, sn);
    float sa = v3len(ax);
    t3_vec3 w = v3mul(sn, v3dot(u->w, sn)); /* keep the turn about the surface */
    if (sa > 1e-5f) w = v3add(w, v3mul(ax, cw_atan2(sa, v3dot(up, sn)) * ALIGN / sa));
    cw_body3d_set_angular_velocity(b, w);
    if (press_jump) {
      cw_body3d_apply_impulse(b, v3mul(n, JUMP_V * PUG_MASS), NULL);
      u->grounded = false;
      u->jump_ticks = 0;
      u->jump_held = true;
      u->second = true;
      s->jumps++;
      event(s, EV_JUMP, i, 0, u->p, 0);
    }
  } else {
    drive(i, u, 0, 0, false);
    u->air_ticks++;
    if (u->jump_ticks >= 0) u->jump_ticks++;
    if (!(in.btn & PB_JUMP)) u->jump_held = false;
    if (u->jump_held && u->jump_ticks < 12) cw_body3d_apply_force(b, v3mul(up, JUMP_HOLD_ACC * PUG_MASS), NULL);
    /* the second jump: straight up, or a dodge where the stick points */
    bool window = u->jump_ticks >= 0 ? u->jump_ticks < 90 : u->air_ticks < 90;
    t3_vec3 w = u->w;
    if (press_jump && u->second && window) {
      u->second = false;
      float mag = sqrtf(steer * steer + pitch * pitch);
      t3_vec3 v = u->v;
      if (mag < 0.35f) {
        v = v3add(v, v3mul(up, DOUBLE_V));
        event(s, EV_DOUBLE, i, 0, u->p, 0);
      } else {
        t3_vec3 ff = v3norm((t3_vec3){ f.x, 0, f.z });
        if (v3len(ff) < 0.5f) ff = v3norm((t3_vec3){ up.x, 0, up.z });
        t3_vec3 rr = v3cross(ff, (t3_vec3){ 0, 1, 0 });
        t3_vec3 dir = v3norm(v3add(v3mul(ff, -pitch), v3mul(rr, steer)));
        if (v.y < 0) v.y *= 0.2f;
        v = v3add(v, v3mul(dir, DODGE_V));
        u->dodge_axis = v3norm(v3cross(dir, (t3_vec3){ 0, -1, 0 }));
        u->dodge_ticks = 40;
        s->dodges++;
        event(s, EV_DODGE, i, 0, u->p, 0);
      }
      cw_body3d_set_velocity(b, v);
    }
    if (u->dodge_ticks > 0) {
      u->dodge_ticks--;
      w = v3mul(u->dodge_axis, 9.8f);
      if (u->dodge_ticks == 0) w = v3mul(w, 0.25f);
    } else {
      /* air control: the stick pitches and yaws (rolls with the slide held) */
      const float acc = 13;
      if (pitch != 0) w = v3add(w, v3mul(rt, pitch * acc * DT));
      if (steer != 0) w = v3add(w, u->sliding ? v3mul(f, steer * acc * DT) : v3mul(up, -steer * acc * 0.8f * DT));
      float damp = (pitch == 0 && steer == 0) ? 3.2f : 0.9f;
      w = v3mul(w, 1 - damp * DT);
      float wl = v3len(w);
      if (wl > 5.5f) w = v3mul(w, 5.5f / wl);
    }
    /* tipped over against something: roll back onto its feet */
    if (touching && v3dot(up, n) < 0.55f && u->dodge_ticks == 0) {
      t3_vec3 ax = v3cross(up, n);
      float l = v3len(ax);
      w = l > 1e-3f ? v3mul(ax, 3.2f / l) : v3mul(f, 3.2f);
      if (press_jump) cw_body3d_apply_impulse(b, v3mul(n, 3.5f * PUG_MASS), NULL);
    }
    cw_body3d_set_angular_velocity(b, w);
  }

  /* boost */
  u->boosting = false;
  if ((in.btn & PB_BOOST) && u->boost > 0) {
    u->boosting = true;
    u->boost = maxf(0, u->boost - BOOST_USE);
    cw_body3d_apply_force(b, v3mul(f, BOOST_ACC * PUG_MASS), NULL);
  }
  u->last_btn = in.btn;
  cw_vehicle3d_update(&PH.car[i], DT);
}

/* ── after the step: the bodies into the sim struct, then the contacts ─ */

static void sync_pug(sim *s, int i) {
  pug *u = &s->p[i];
  cw_body3d b = PH.pug[i];
  cw_xform3 x = cw_body3d_transform(b);
  u->p = x.p;
  u->q = x.q;
  u->v = cw_body3d_velocity(b);
  u->w = cw_body3d_angular_velocity(b);
  if (u->dead) return;
  float sp = v3len(u->v);
  if (sp > MAX_SPEED) {
    u->v = v3mul(u->v, MAX_SPEED / sp);
    cw_body3d_set_velocity(b, u->v);
    sp = MAX_SPEED;
  }
  bool was = u->supersonic;
  u->supersonic = sp > (was ? 18.5f : SUPERSONIC);
  if (u->supersonic && !was) event(s, EV_SUPERSONIC, i, 0, u->p, 0);
  if (u->touch_cool > 0) u->touch_cool--;
}

static void sync_ball(sim *s) {
  if (!PH.ball_live) {
    s->ball_v = s->ball_spin = (t3_vec3){ 0, 0, 0 };
    return;
  }
  s->ball = cw_body3d_transform(PH.ball).p;
  s->ball_v = cw_body3d_velocity(PH.ball);
  s->ball_spin = cw_body3d_angular_velocity(PH.ball);
  cw_phys_filter f = cw_phys_filter_layers(1u << L_ARENA);
  cw_shape3d hit;
  cw_qshape3d q = cw_qshape3d_sphere(s->ball, RB + 0.3f);
  s->ball_low = cw_world3d_overlap(PH.w, &q, &f, &hit, 1) > 0;
  q = cw_qshape3d_sphere(s->ball, RB + 0.1f);
  s->ball_rest = s->ball_low && cw_world3d_overlap(PH.w, &q, &f, &hit, 1) > 0;
}

static void touch(sim *s, int i, float strength) {
  pug *u = &s->p[i];
  if (s->last_touch != i) {
    s->prev_touch = s->last_touch;
    s->last_touch = i;
  }
  s->last_touch_tick = s->tick;
  u->touches++;
  u->points += 2;
  s->hits++;
  event(s, EV_HIT, i, u->team, s->ball, strength);
}

/* a pug hit the ball: n from the pug to the ball, close = approach speed */
static void ball_hit(sim *s, int i, t3_vec3 n, float close) {
  pug *u = &s->p[i];
  if (u->dead || u->touch_cool > 0 || !PH.ball_live) return;
  int own = u->team == TEAM_BLUE ? -1 : 1;
  bool was_in_own = heading_in(PH.ball0, PH.ball_v0, own);
  /* the touch's extra kick: flatter than the contact normal, bigger the harder it was */
  t3_vec3 dir = v3norm((t3_vec3){ n.x, n.y * 0.35f, n.z });
  float bonus = 8.5f * clampf((close - 0.5f) / 18, 0, 1);
  s->ball_v = v3add(s->ball_v, v3mul(dir, bonus));
  cw_body3d_set_velocity(PH.ball, s->ball_v);
  u->touch_cool = 8;
  touch(s, i, clampf(close / 22, 0, 1));
  if (was_in_own && !heading_in(s->ball, s->ball_v, own)) {
    u->saves++;
    u->points += 50;
  } else if (heading_in(s->ball, s->ball_v, -own) && v3len(s->ball_v) > 8) {
    u->shots++;
    u->points += 20;
  }
}

/* pug a ran into pug b: n from a to b */
static void pug_hit(sim *s, int a, int b, t3_vec3 n) {
  pug *A = &s->p[a], *B = &s->p[b];
  if (A->dead || B->dead) return;
  float sa = v3dot(PH.pug_v0[a], n), sb = -v3dot(PH.pug_v0[b], n);
  float vr = sa + sb; /* closing speed */
  if (vr <= 0) return;
  /* a supersonic hit nose-first on the other team: a demolition */
  int att = -1, vic = -1;
  if (A->team != B->team) {
    if (PH.sonic0[a] && sa > 12 && v3dot(pug_fwd(A), n) > 0.5f) att = a, vic = b;
    else if (PH.sonic0[b] && sb > 12 && v3dot(pug_fwd(B), v3mul(n, -1)) > 0.5f) att = b, vic = a;
  }
  if (att >= 0) {
    pug *V = &s->p[vic];
    s->p[att].demos++;
    s->p[att].points += 25;
    s->demos++;
    event(s, EV_DEMO, att, vic, V->p, 1);
    V->dead = DEMO_TICKS;
    V->boosting = V->supersonic = V->sliding = false;
    V->grounded = false;
    /* the attacker smashes straight through; the victim tumbles off as a wreck */
    cw_body3d_set_velocity(PH.pug[att], PH.pug_v0[att]);
    s->p[att].v = PH.pug_v0[att];
    t3_vec3 dv = v3add(v3mul(n, att == a ? 14.0f : -14.0f), (t3_vec3){ 0, 7, 0 });
    V->v = v3add(PH.pug_v0[vic], dv);
    V->w = (t3_vec3){ 6 + V->v.z * 0.3f, 4 - V->v.x * 0.2f, 9 }; /* a ridiculous spin: end over end, some twist */
    pug_wreck(vic, true);
    cw_body3d_set_velocity(PH.pug[vic], V->v);
    cw_body3d_set_angular_velocity(PH.pug[vic], V->w);
    return;
  }
  /* the bumper shoves the bumped a bit extra */
  if (sa > sb) {
    B->v = v3add(B->v, v3add(v3mul(n, sa * 0.35f), (t3_vec3){ 0, sa * 0.12f, 0 }));
    cw_body3d_set_velocity(PH.pug[b], B->v);
  } else {
    A->v = v3add(A->v, v3add(v3mul(n, -sb * 0.35f), (t3_vec3){ 0, sb * 0.12f, 0 }));
    cw_body3d_set_velocity(PH.pug[a], A->v);
  }
  if (vr > 5) event(s, EV_BUMP, sa > sb ? a : b, sa > sb ? b : a, v3mul(v3add(A->p, B->p), 0.5f), clampf(vr / 20, 0, 1));
}

static void contacts(sim *s) {
  int ne = 0;
  const cw_phys_event3d *ev = cw_world3d_events(PH.w, &ne);
  for (int e = 0; e < ne; e++) {
    const cw_phys_event3d *h = &ev[e];
    if (h->type != CW_PHYS_HIT) continue;
    bool ball_a = h->body_a.id == PH.ball.id, ball_b = h->body_b.id == PH.ball.id;
    int pa = pug_of(h->body_a), pb = pug_of(h->body_b);
    if (ball_a || ball_b) {
      int p = ball_a ? pb : pa;
      t3_vec3 n = ball_a ? v3mul(h->normal, -1) : h->normal; /* toward the ball */
      if (p >= 0) ball_hit(s, p, n, h->speed);
      else if ((ball_a ? h->body_b.id : h->body_a.id) == PH.arena.id && h->speed > 4) {
        s->bounces++;
        event(s, EV_BOUNCE, -1, 0, h->point, clampf(h->speed / 20, 0, 1));
      }
    } else if (pa >= 0 && pb >= 0)
      pug_hit(s, pa, pb, h->normal);
  }
  float sp = v3len(s->ball_v);
  if (sp > BALL_MAX) {
    s->ball_v = v3mul(s->ball_v, BALL_MAX / sp);
    cw_body3d_set_velocity(PH.ball, s->ball_v);
    sp = BALL_MAX;
  }
  if (sp > s->max_ball_speed) s->max_ball_speed = sp;
}

static void pads_step(sim *s) {
  for (int k = 0; k < s->npads; k++) {
    boost_pad *pd = &s->pads[k];
    if (pd->cool > 0) {
      pd->cool--;
      continue;
    }
    float r = pd->big ? 1.6f : 1.05f;
    for (int i = 0; i < MAXP; i++) {
      pug *u = &s->p[i];
      if (!u->on || u->dead || u->boost >= 100 || u->p.y > 2.2f) continue;
      float dx = u->p.x - pd->x, dz = u->p.z - pd->z;
      if (dx * dx + dz * dz > r * r) continue;
      u->boost = pd->big ? 100 : minf(100, u->boost + 12);
      pd->cool = pd->big ? 600 : 240;
      s->pads_taken++;
      event(s, EV_PAD, i, pd->big, (t3_vec3){ pd->x, 0, pd->z }, 0);
      break;
    }
  }
}

static void goal_scored(sim *s, int team) {
  s->score[team]++;
  int sc = s->last_touch;
  s->goal_team = team;
  s->scorer = sc >= 0 && s->p[sc].team == team ? sc : -1; /* -1: an own goal */
  s->assister = -1;
  if (s->scorer >= 0) {
    s->p[sc].goals++;
    s->p[sc].points += 100;
    int pv = s->prev_touch;
    if (pv >= 0 && pv != sc && s->p[pv].team == team && s->tick - s->last_touch_tick < 300) {
      s->assister = pv;
      s->p[pv].assists++;
      s->p[pv].points += 50;
    }
  }
  s->phase = PH_GOAL;
  s->phase_ticks = 0;
  event(s, EV_GOAL, team, s->scorer, s->ball, v3len(s->ball_v) / 30);
}

static void hash_state(sim *s) {
  uint32_t h = 2166136261u;
#define MIX(x)                                                                                                         \
  do {                                                                                                                 \
    uint32_t _v;                                                                                                       \
    memcpy(&_v, &(x), 4);                                                                                              \
    h = (h ^ _v) * 16777619u;                                                                                          \
  } while (0)
  for (int i = 0; i < MAXP; i++) {
    const pug *u = &s->p[i];
    if (!u->on) continue;
    MIX(u->p.x); MIX(u->p.y); MIX(u->p.z); MIX(u->v.x); MIX(u->v.y); MIX(u->v.z);
    MIX(u->q.x); MIX(u->q.y); MIX(u->q.z); MIX(u->q.w); MIX(u->boost);
  }
  MIX(s->ball.x); MIX(s->ball.y); MIX(s->ball.z); MIX(s->ball_v.x); MIX(s->ball_v.y); MIX(s->ball_v.z);
  MIX(s->score[0]); MIX(s->score[1]); MIX(s->clock); MIX(s->rng);
  uint32_t ph = cw_world3d_stats(PH.w).hash; /* every body's motion and every contact event */
  MIX(ph);
#undef MIX
  s->hash = h;
}

void sim_step(sim *s, const pin in_raw[MAXP]) {
  s->tick++;
  s->phase_ticks++;
  s->ev_n = 0;
  pin in[MAXP];
  memcpy(in, in_raw, sizeof in);
  if (s->phase == PH_KICKOFF) {
    /* frozen in place until GO; boost and jumps wait too */
    for (int i = 0; i < MAXP; i++) in[i] = (pin){ 0 };
    if (s->phase_ticks >= KICKOFF_TICKS) {
      s->phase = PH_PLAY;
      s->phase_ticks = 0;
      event(s, EV_GO, 0, 0, s->ball, 0);
    }
  }
  if (s->phase == PH_OVER)
    for (int i = 0; i < MAXP; i++) in[i] = (pin){ 0 };
  for (int i = 0; i < MAXP; i++) {
    pug *u = &s->p[i];
    if (!u->on) continue;
    if (u->dead) {
      if (--u->dead == 0) respawn(s, i);
      continue;
    }
    control(s, i, in[i]);
  }
  ball_live(s->phase == PH_PLAY);
  for (int i = 0; i < MAXP; i++) PH.pug_v0[i] = s->p[i].v, PH.sonic0[i] = s->p[i].supersonic;
  PH.ball0 = s->ball;
  PH.ball_v0 = s->ball_v;

  cw_world3d_step(PH.w, DT);

  for (int i = 0; i < MAXP; i++)
    if (s->p[i].on) sync_pug(s, i);
  sync_ball(s);
  contacts(s);
  pads_step(s);
  switch (s->phase) {
  case PH_PLAY:
    if (fabsf(s->ball.z) > AL + RB) goal_scored(s, s->ball.z > 0 ? TEAM_BLUE : TEAM_RED);
    else if (s->overtime) s->clock++;
    else if (s->clock > 0 && --s->clock == 0) {
      if (s->score[0] != s->score[1]) {
        s->phase = PH_OVER;
        s->phase_ticks = 0;
        s->winner = s->score[0] > s->score[1] ? TEAM_BLUE : TEAM_RED;
        event(s, EV_FINAL, s->winner, 0, s->ball, 0);
      } else {
        s->overtime = true;
        event(s, EV_OVERTIME, 0, 0, s->ball, 0);
        kickoff(s);
      }
    }
    break;
  case PH_GOAL:
    if (s->phase_ticks >= GOAL_TICKS) {
      if (s->overtime || (s->clock == 0 && s->score[0] != s->score[1])) {
        s->phase = PH_OVER;
        s->phase_ticks = 0;
        s->winner = s->score[0] > s->score[1] ? TEAM_BLUE : TEAM_RED;
        event(s, EV_FINAL, s->winner, 0, s->ball, 0);
      } else kickoff(s);
    }
    break;
  default: break;
  }
  hash_state(s);
}
