/* racer's race: karts, items and laps on Box3D.
 *
 *   track    the path's static meshes (track.c / trackmesh.c): the driving
 *            surface, and the walls and roofs
 *   karts    dynamic bodies on cartwheel's vehicle controller, plus the arcade
 *            layer on top: speed-sensitive steering, a drift that charges a
 *            mini-turbo, boost, air control and a landing assist, and TRACK
 *            GRAVITY: near the road the pull is towards the road's own down,
 *            which is what lets a kart ride a loop or a corkscrew
 *   items    boxes, missiles, oil, mines and the rest live in track
 *            coordinates (distance along, metres to the right, height), not
 *            as bodies
 *   race     progress is the unwrapped distance along the track; laps, the
 *            order and the finish are read from it
 *
 * One world step per 60 Hz tick (Box3D sub-steps inside it) on the job pool.
 * Deterministic: Box3D gives the same result at any worker count and on every
 * host, and the game's arithmetic is + - * / and sqrtf with cartwheel's
 * cw_sin / cw_cos / cw_atan2, one seeded random stream. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "game.h"

#define DT (1.0f / TICK_HZ)

enum { L_TRACK, L_WALL, L_KART, L_GHOST };

/* ── the vehicles ───────────────────────────────────────────────────── */

const vehicle_def VEH[NVEH] = {
  { "Scrapper", "Balanced. Takes a hit and keeps going.", 38.0f, 11.0f, 1.00f, 1.00f, 200, 1.00f, 1.00f, 1.15f, 0.72f, 0.38f },
  { "Rhino", "Heavy. Shoulders everyone aside, slow to get going.", 36.0f, 8.5f, 0.85f, 1.05f, 300, 0.85f, 1.00f, 1.25f, 0.80f, 0.42f },
  { "Gnat", "Light and twitchy. Wins the corners, loses the shoves.", 36.0f, 13.5f, 1.20f, 0.95f, 140, 1.20f, 0.95f, 1.05f, 0.66f, 0.34f },
  { "Sidewinder", "Drifts like a dream. Charges its turbo fast.", 37.0f, 10.5f, 1.10f, 0.92f, 190, 1.55f, 1.05f, 1.15f, 0.72f, 0.38f },
  { "Longhaul", "Fastest flat out. Wide turns, long recovery.", 41.0f, 9.0f, 0.82f, 1.00f, 260, 0.90f, 1.10f, 1.30f, 0.78f, 0.42f },
  { "Pogo", "Springy and quick off the line. Boosts hit hardest.", 37.0f, 12.0f, 1.05f, 0.98f, 170, 1.05f, 1.25f, 1.10f, 0.68f, 0.36f },
};

const char *const ITEM_NAME[IT_KINDS] = { "-", "Turbo", "Triple Turbo", "Missile", "Homing Missile", "Oil Slick", "Mine", "Shield", "Shockwave" };

/* ── tuning ─────────────────────────────────────────────────────────── */

#define RIDE 0.62f      /* the body origin's height above the road at rest */
#define TRAVEL 0.40f
#define HZ_SPRING 3.0f
#define TYRE_GRIP 2.6f
#define MAX_SPEED 78.0f
#define STICK 0.45f     /* extra downforce, as a share of g, once moving */
#define DRIFT_MIN 13.0f /* m/s */
#define BOOST_TURBO 96  /* ticks */
#define HIT_SPIN 70
#define OIL_SPIN 60
#define BOOM_SPIN 85
#define SHOCK_SPIN 80
#define SHOCK_WARN 78 /* ticks between the alarm and the wave: a quick hop on the drift button clears it */

static struct {
  cw_world3d w;
  cw_body3d trackb, wallb;
  cw_mesh3d road_mesh, wall_mesh;
  track_phys road, walls;
  cw_body3d body[MAXK];
  cw_shape3d hull[MAXK];
  cw_vehicle3d car[MAXK];
  bool ready;
  t3_vec3 v0[MAXK];
  float prev_s[MAXK];
  float min_y;
} PH;

/* ── small maths ─────────────────────────────────────────────────────── */

float v3len(t3_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
t3_vec3 v3norm(t3_vec3 a) {
  float l = v3len(a);
  return l > 1e-8f ? v3mul(a, 1.0f / l) : (t3_vec3){ 0, 0, 0 };
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float minf(float a, float b) { return a < b ? a : b; }
static float absf(float a) { return a < 0 ? -a : a; }

static t3_quat qaxis(t3_vec3 axis, float angle) {
  float s, c;
  cw_sincos(angle * 0.5f, &s, &c);
  return (t3_quat){ axis.x * s, axis.y * s, axis.z * s, c };
}
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = { q.x, q.y, q.z };
  t3_vec3 t = v3mul(v3cross(u, v), 2);
  return v3add(v3add(v, v3mul(t, q.w)), v3cross(u, t));
}
/* the rotation taking the frame (r, u, f) to the world: columns */
static t3_quat q_from_frame(t3_vec3 r, t3_vec3 u, t3_vec3 f) {
  /* the kart's local axes: +x is its LEFT (right is -x), +y up, +z forward */
  t3_vec3 x = v3mul(r, -1), y = u, z = f;
  float tr = x.x + y.y + z.z, qx, qy, qz, qw;
  if (tr > 0) {
    float s = sqrtf(tr + 1.0f) * 2;
    qw = 0.25f * s;
    qx = (y.z - z.y) / s;
    qy = (z.x - x.z) / s;
    qz = (x.y - y.x) / s;
  } else if (x.x > y.y && x.x > z.z) {
    float s = sqrtf(1.0f + x.x - y.y - z.z) * 2;
    qw = (y.z - z.y) / s;
    qx = 0.25f * s;
    qy = (y.x + x.y) / s;
    qz = (z.x + x.z) / s;
  } else if (y.y > z.z) {
    float s = sqrtf(1.0f + y.y - x.x - z.z) * 2;
    qw = (z.x - x.z) / s;
    qx = (y.x + x.y) / s;
    qy = 0.25f * s;
    qz = (z.y + y.z) / s;
  } else {
    float s = sqrtf(1.0f + z.z - x.x - y.y) * 2;
    qw = (x.y - y.x) / s;
    qx = (z.x + x.z) / s;
    qy = (z.y + y.z) / s;
    qz = 0.25f * s;
  }
  float l = sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
  return (t3_quat){ qx / l, qy / l, qz / l, qw / l };
}

t3_vec3 kart_fwd(const kart *k) { return qrot(k->q, (t3_vec3){ 0, 0, 1 }); }
t3_vec3 kart_up(const kart *k) { return qrot(k->q, (t3_vec3){ 0, 1, 0 }); }
t3_vec3 kart_right(const kart *k) { return v3cross(kart_fwd(k), kart_up(k)); }

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

/* ── the world ───────────────────────────────────────────────────────── */

static void phys_free(void) {
  if (!PH.ready) return;
  if (cw_world3d_valid(PH.w)) cw_world3d_destroy(PH.w);
  if (PH.road_mesh.id) cw_mesh3d_destroy(PH.road_mesh);
  if (PH.wall_mesh.id) cw_mesh3d_destroy(PH.wall_mesh);
  track_phys_free(&PH.road);
  track_phys_free(&PH.walls);
  memset(&PH, 0, sizeof PH);
}

static void car_make(int i, const vehicle_def *vd) {
  cw_vehicle3d_def d = cw_vehicle3d_default_def();
  d.mask = 1u << L_TRACK | 1u << L_WALL;
  d.rolling_resistance = 0.01f;
  d.drag = 0.0f;
  d.roll_influence = 0.25f;
  float hz = HZ_SPRING, rest = GRAV / (4 * 3.1415927f * 3.1415927f * hz * hz);
  float wr = vd->wheel_r, ay = -RIDE + TRAVEL - rest + wr;
  float wb = vd->len * 0.72f, tw = vd->wid * 0.95f;
  for (int k = 0; k < 4; k++) {
    int w = cw_vehicle3d_def_add_wheel(&d, (t3_vec3){ k & 1 ? -tw : tw, ay, k < 2 ? wb : -wb }, k < 2 ? 0 : 1);
    cw_wheel3d_def *wd = &d.wheels[w];
    wd->radius = wr;
    wd->travel = TRAVEL;
    wd->hertz = hz;
    wd->damping_ratio = 0.75f;
    wd->grip = TYRE_GRIP * vd->grip;
    wd->slide_grip = 0.9f;
    wd->lateral_stiffness = 1;
    wd->handbrake = false;
  }
  d.max_load = 3;
  cw_vehicle3d_init(&PH.car[i], PH.body[i], &d);
}

static void world_make(sim *s, const map_def *map, const bool on[MAXK], const int veh[MAXK]) {
  phys_free();
  cw_world3d_def d = cw_world3d_default_def();
  d.gravity = (t3_vec3){ 0, -GRAV, 0 };
  d.workers = 0;
  d.substeps = 4;
  d.hit_speed = 3.0f;
  d.sleep = false;
  d.layers[L_TRACK] = "track";
  d.layers[L_WALL] = "wall";
  d.layers[L_KART] = "kart";
  d.layers[L_GHOST] = "ghost";
  PH.w = cw_world3d_create(&d);
  track_phys_build(&PH.road, &PH.walls, s->track, &map->mesh);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.name = "road";
  PH.trackb = cw_body3d_create(PH.w, &bd);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = L_TRACK;
  sd.material.friction = 0.9f;
  if (PH.road.nt) {
    PH.road_mesh = cw_mesh3d_create(PH.road.v, PH.road.nv, PH.road.idx, PH.road.nt);
    cw_shape3d_mesh(PH.trackb, &sd, PH.road_mesh, (t3_vec3){ 1, 1, 1 });
  }
  bd.name = "walls";
  PH.wallb = cw_body3d_create(PH.w, &bd);
  sd = cw_shape3d_default_def();
  sd.layer = L_WALL;
  sd.material.friction = 0.0f;
  sd.material.restitution = 0.0f;
  if (PH.walls.nt) {
    PH.wall_mesh = cw_mesh3d_create(PH.walls.v, PH.walls.nv, PH.walls.idx, PH.walls.nt);
    cw_shape3d_mesh(PH.wallb, &sd, PH.wall_mesh, (t3_vec3){ 1, 1, 1 });
  }
  float miny = 1e9f;
  for (int i = 0; i < s->track->n; i++) miny = minf(miny, s->track->s[i].p.y);
  PH.min_y = miny;
  for (int i = 0; i < MAXK; i++) {
    if (!on[i]) continue;
    const vehicle_def *vd = &VEH[veh[i] % NVEH];
    bd = cw_body3d_default_def();
    bd.position = (t3_vec3){ 0, 50 + (float)i * 3, 0 };
    bd.sleep = false;
    bd.interpolate = true;
    bd.name = "kart";
    bd.bullet = true;
    bd.linear_damping = 0.0f;
    bd.angular_damping = 0.4f;
    PH.body[i] = cw_body3d_create(PH.w, &bd);
    sd = cw_shape3d_default_def();
    sd.layer = L_KART;
    sd.mask = (1u << L_TRACK) | (1u << L_WALL) | (1u << L_KART);
    float rr0 = vd->wid * 0.78f, vol = 3.1416f * rr0 * rr0 * (2 * (vd->len - rr0) + 1.333f * rr0);
    sd.density = vd->mass / vol;
    sd.material.friction = 0.2f;
    sd.material.restitution = 0.2f;
    sd.hit_events = true;
    /* a rounded hull (a capsule along the kart): a wall or another kart slides along it instead of catching a corner */
    float rr = vd->wid * 0.78f;
    PH.hull[i] = cw_shape3d_capsule(PH.body[i], &sd, (t3_vec3){ 0, 0.36f, -(vd->len - rr) }, (t3_vec3){ 0, 0.36f, vd->len - rr }, rr);
    b3MassData md = b3Body_GetMassData(cw_body3d_raw(PH.body[i]));
    md.center = (b3Vec3){ 0, -0.25f, 0 };
    b3Body_SetMassData(cw_body3d_raw(PH.body[i]), md);
    car_make(i, vd);
  }
  PH.ready = true;
}

/* ── kart state ──────────────────────────────────────────────────────── */

static void kart_reset_state(kart *k) {
  k->speed = 0;
  k->steer = 0;
  k->drifting = false;
  k->drift_ticks = k->drift_level = 0;
  k->boost_ticks = 0;
  k->spin_ticks = 0;
  k->spin_kind = SPIN_NONE;
  k->shield_ticks = 0;
  k->trick_ticks = 0;
  k->trick_done = false;
  k->air_ticks = 0;
  k->air_peak = 0;
  k->stuck_ticks = 0;
  k->reset_hold = 0;
  k->bump_ticks = 0;
}

/* sets a kart standing on the road at track distance s, lat metres to the right, facing along the road */
static void kart_place(sim *s, int i, float sdist, float lat, float speed) {
  kart *k = &s->k[i];
  tsample m = track_at(s->track, sdist);
  t3_vec3 pos = v3add(v3add(m.p, v3mul(m.r, lat)), v3mul(m.u, RIDE + 0.15f));
  t3_quat q = q_from_frame(m.r, m.u, m.f);
  cw_vehicle3d_reset(&PH.car[i]);
  cw_vehicle3d_set_axle(&PH.car[i], 0, 0, 0, 0);
  cw_vehicle3d_set_axle(&PH.car[i], 1, 0, 0, 0);
  cw_vehicle3d_teleport(&PH.car[i], (cw_xform3){ pos, q });
  cw_body3d_set_velocity(PH.body[i], v3mul(m.f, speed));
  cw_body3d_set_angular_velocity(PH.body[i], (t3_vec3){ 0, 0, 0 });
  k->p = pos;
  k->q = q;
  k->v = v3mul(m.f, speed);
  k->w = (t3_vec3){ 0, 0, 0 };
  kart_reset_state(k);
  k->speed = speed;
  k->hint = -1;
  track_locate(s->track, pos, -1, &k->loc);
  k->hint = k->loc.i;
  PH.prev_s[i] = k->loc.s;
}

void sim_place_on_grid(sim *s) {
  const track *t = s->track;
  int row = 0, col = 0;
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on) continue;
    float back = 8.0f + (float)row * 8.0f + (col ? 4.0f : 0.0f);
    float lat = (col ? 1.0f : -1.0f) * minf(t->s[0].hw * 0.42f, 3.4f);
    float sd = t->circuit ? t->length - back : 0;
    kart_place(s, i, sd, lat, 0);
    k->prog = -back;
    k->lap = 0;
    k->last_safe_s = sd;
    PH.prev_s[i] = k->loc.s;
    col ^= 1;
    row += col == 0;
  }
}

/* ── the box rows ───────────────────────────────────────────────────── */

static void make_boxes(sim *s) {
  const track *t = s->track;
  s->nbox = 0;
  for (int r = 0; r < t->nitem_rows; r++) {
    const tsample *a = &t->s[t->item_row_at[r]];
    int cnt = a->hw > 9 ? 5 : a->hw > 6 ? 4 : 3;
    for (int j = 0; j < cnt && s->nbox < MAXBOX; j++) {
      float lat = ((float)j - (float)(cnt - 1) * 0.5f) * (2.0f * a->hw - 3.0f) / (float)cnt;
      s->box[s->nbox++] = (itembox){ a->s, lat, track_point(t, a->s, lat, 1.1f), 0 };
    }
  }
}

void sim_init(sim *s, const track *t, const map_def *map, int map_index, int laps, const bool on[MAXK], const int veh[MAXK], uint32_t seed) {
  memset(s, 0, sizeof *s);
  s->rng = seed * 2654435761u + 0x1234567u;
  s->track = t;
  s->map = map_index;
  s->md = map;
  s->laps = laps < 1 ? 1 : laps > MAX_LAPS ? MAX_LAPS : laps;
  if (!t->circuit) s->laps = 1;
  s->class_top = s->class_acc = 1.0f;
  for (int i = 0; i < MAXK; i++) {
    s->k[i].on = on[i];
    s->k[i].veh = veh[i] % NVEH;
    s->k[i].place = i + 1;
    s->nkarts += on[i];
  }
  world_make(s, map, on, veh);
  make_boxes(s);
  sim_place_on_grid(s);
  s->phase = PH_GRID;
  for (int i = 0; i < MAXK; i++) s->order[i] = i;
}

void sim_free(sim *s) {
  (void)s;
  phys_free();
}

/* the speed classes: top speed and acceleration of every kart, as a share of the vehicle's own */
static const float CLASS_TOP[3] = { 1.0f, 1.16f, 1.32f };
static const float CLASS_ACC[3] = { 1.0f, 1.22f, 1.45f };
void sim_set_class(sim *s, int cls) {
  cls = cls < 0 ? 0 : cls > 2 ? 2 : cls;
  s->speed_class = cls;
  s->class_top = CLASS_TOP[cls];
  s->class_acc = CLASS_ACC[cls];
}

void sim_restart(sim *s, uint32_t seed, int skip_ticks) {
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    kart keep = { .on = k->on, .human = k->human, .veh = k->veh, .colour = k->colour, .place = i + 1 };
    *k = keep;
    PH.v0[i] = (t3_vec3){ 0, 0, 0 };
  }
  memset(s->hz, 0, sizeof s->hz);
  for (int b = 0; b < s->nbox; b++) s->box[b].cool = 0;
  s->tick = 0;
  s->rng = seed * 2654435761u + 0x1234567u;
  s->phase_ticks = 0;
  s->race_ticks = 0;
  s->finished_count = 0;
  s->ev_n = 0;
  s->hash = 0;
  sim_place_on_grid(s);
  s->phase = PH_GRID;
  if (skip_ticks > 0) s->phase = PH_COUNT, s->phase_ticks = skip_ticks;
  for (int i = 0; i < MAXK; i++) s->order[i] = i;
}

/* dev aid: stand kart i on the road at s metres, lat to the right, already moving at speed */
void sim_put(sim *s, int i, float sd, float lat, float speed) {
  kart_place(s, i, sd, lat, speed);
  s->k[i].prog = sd;
  s->k[i].last_safe_s = sd;
}

static void use_item(sim *s, int i);
/* dev aid: kart i uses an item now, whatever it held */
void sim_use(sim *s, int i, int item) {
  s->k[i].item = item, s->k[i].item_count = 1;
  use_item(s, i);
}

const track *sim_track(const sim *s) { return s->track; }
const cw_vehicle3d *sim_car(const sim *s, int i) {
  (void)s;
  return &PH.car[i];
}

/* ── hazards ─────────────────────────────────────────────────────────── */

static hazard *hz_add(sim *s, int kind, int owner, float sd, float lat, float h) {
  for (int i = 0; i < MAXHZ; i++) {
    if (s->hz[i].kind) continue;
    s->hz[i] = (hazard){ .kind = kind, .owner = owner, .target = -1, .s = sd, .lat = lat, .h = h, .life = 600 };
    return &s->hz[i];
  }
  return NULL;
}

static void spin_kart(sim *s, int i, int kind, int by) {
  kart *k = &s->k[i];
  if (k->respawn_ticks > 0 || k->finished) return;
  if (k->shield_ticks > 0) {
    k->shield_ticks = 0;
    event(s, EV_SHIELD, i, by, k->p, 1);
    return;
  }
  if (k->spin_ticks > 20 && kind != SPIN_BOOM) return;
  k->spin_kind = kind;
  k->spin_ticks = kind == SPIN_OIL ? OIL_SPIN : kind == SPIN_BOOM ? BOOM_SPIN : kind == SPIN_SHOCK ? SHOCK_SPIN : HIT_SPIN;
  k->spin_dir = (rnd(s) & 1) ? 1.0f : -1.0f;
  k->drifting = false;
  k->drift_ticks = 0;
  k->boost_ticks = 0;
  k->hits_taken++;
  if (by >= 0 && by != i) s->k[by].hits_given++;
  event(s, EV_SPIN, i, by, k->p, (float)kind);
  if (kind == SPIN_BOOM) {
    cw_body3d_apply_impulse(PH.body[i], v3mul(kart_up(k), 9.0f * VEH[k->veh].mass), NULL);
  }
}

static void hazards_step(sim *s) {
  const track *t = s->track;
  for (int h = 0; h < MAXHZ; h++) {
    hazard *z = &s->hz[h];
    if (!z->kind) continue;
    z->age++;
    if (--z->life <= 0) {
      z->kind = HZ_NONE;
      continue;
    }
    if (z->kind == HZ_MISSILE || z->kind == HZ_HOMING) {
      if (z->kind == HZ_HOMING && z->target >= 0 && s->k[z->target].on) {
        const kart *tk = &s->k[z->target];
        float dl = tk->loc.lat - z->lat;
        z->lat += clampf(dl, -14 * DT, 14 * DT);
        /* it closes on its mark: a touch faster when behind it */
        float ds = tk->loc.s - z->s;
        if (t->circuit) {
          if (ds > t->length * 0.5f) ds -= t->length;
          if (ds < -t->length * 0.5f) ds += t->length;
        }
        z->speed = clampf(56.0f + ds * 0.9f, 48, 90);
        z->h = clampf(tk->loc.height + 0.2f, 0.3f, 3.0f);
      }
      z->s += z->speed * DT;
      if (!t->circuit && z->s > t->length) z->kind = HZ_NONE;
    }
    if (z->kind == HZ_NONE) continue;
    tsample m = track_at(t, z->s);
    z->p = v3add(m.p, v3add(v3mul(m.r, z->lat), v3mul(m.u, z->h)));
    z->f = m.f;
    for (int i = 0; i < MAXK; i++) {
      kart *k = &s->k[i];
      if (!k->on || k->respawn_ticks > 0 || k->finished) continue;
      if (z->age < 12 && z->owner == i && (z->kind == HZ_MISSILE || z->kind == HZ_HOMING)) continue;
      if (z->age < 45 && z->owner == i) continue;
      t3_vec3 d = v3sub(k->p, z->p);
      float r = z->kind == HZ_OIL ? 1.8f : z->kind == HZ_MINE ? 2.3f : 1.9f;
      if (z->kind == HZ_OIL && k->loc.height > 1.2f) continue;
      if (v3dot(d, d) > r * r) continue;
      if (z->kind == HZ_OIL) {
        spin_kart(s, i, SPIN_OIL, z->owner);
        z->kind = HZ_NONE;
      } else if (z->kind == HZ_MINE) {
        event(s, EV_HIT, i, z->owner, z->p, (float)HZ_MINE);
        spin_kart(s, i, SPIN_BOOM, z->owner);
        /* the blast reaches a little way */
        for (int j = 0; j < MAXK; j++) {
          if (j == i || !s->k[j].on) continue;
          t3_vec3 e = v3sub(s->k[j].p, z->p);
          if (v3dot(e, e) < 4.5f * 4.5f) spin_kart(s, j, SPIN_HIT, z->owner);
        }
        z->kind = HZ_NONE;
      } else {
        event(s, EV_HIT, i, z->owner, z->p, (float)z->kind);
        spin_kart(s, i, SPIN_HIT, z->owner);
        z->kind = HZ_NONE;
      }
      break;
    }
  }
}

/* ── items ───────────────────────────────────────────────────────────── */

static const uint8_t ITEM_W[IT_KINDS][MAXK] = {
  [IT_TURBO] = { 20, 20, 18, 15, 12, 10, 8, 6 },
  [IT_TURBO3] = { 0, 2, 6, 10, 14, 18, 22, 26 },
  [IT_MISSILE] = { 28, 26, 22, 18, 14, 10, 6, 4 },
  [IT_HOMING] = { 0, 4, 8, 12, 16, 18, 20, 22 },
  [IT_OIL] = { 22, 20, 18, 14, 10, 8, 6, 4 },
  [IT_MINE] = { 20, 18, 16, 12, 10, 8, 6, 4 },
  [IT_SHIELD] = { 8, 8, 10, 12, 12, 12, 12, 10 },
  [IT_SHOCK] = { 0, 0, 2, 7, 12, 16, 20, 24 },
};

static int roll_item(sim *s, int place) {
  int p = place - 1;
  if (p < 0) p = 0;
  if (p > MAXK - 1) p = MAXK - 1;
  int total = 0;
  for (int i = 1; i < IT_KINDS; i++) total += ITEM_W[i][p];
  int r = (int)(rnd(s) % (uint32_t)total);
  for (int i = 1; i < IT_KINDS; i++) {
    r -= ITEM_W[i][p];
    if (r < 0) return i;
  }
  return IT_TURBO;
}

static void start_boost(sim *s, int i, int ticks, float strength, int source) {
  kart *k = &s->k[i];
  if (k->boost_ticks < ticks) k->boost_ticks = ticks;
  if (strength > k->boost_k || k->boost_ticks <= ticks) k->boost_k = strength;
  k->boosts++;
  event(s, EV_BOOST, i, source, k->p, strength);
}

static int kart_ahead(const sim *s, int i) {
  int pl = s->k[i].place;
  for (int j = 0; j < MAXK; j++)
    if (s->k[j].on && s->k[j].place == pl - 1) return j;
  return -1;
}

static void use_item(sim *s, int i) {
  kart *k = &s->k[i];
  if (k->item == IT_NONE || k->item_wait > 0 || k->item_cool > 0 || k->spin_ticks > 0) return;
  const vehicle_def *vd = &VEH[k->veh];
  int item = k->item;
  event(s, EV_ITEM_USE, i, item, k->p, 0);
  k->item_cool = 18;
  switch (item) {
  case IT_TURBO: start_boost(s, i, BOOST_TURBO, 1.25f * vd->boost, 1); break;
  case IT_TURBO3:
    start_boost(s, i, 70, 1.2f * vd->boost, 1);
    if (--k->item_count > 0) return;
    break;
  case IT_MISSILE: {
    hazard *z = hz_add(s, HZ_MISSILE, i, k->loc.s + 2.5f, k->loc.lat, clampf(k->loc.height + 0.5f, 0.5f, 2.0f));
    if (z) z->speed = maxf(70.0f, k->speed + 38.0f);
    break;
  }
  case IT_HOMING: {
    hazard *z = hz_add(s, HZ_HOMING, i, k->loc.s + 2.5f, k->loc.lat, clampf(k->loc.height + 0.5f, 0.5f, 2.0f));
    if (z) {
      z->speed = maxf(60.0f, k->speed + 30.0f);
      z->target = kart_ahead(s, i);
      if (z->target < 0) z->kind = HZ_MISSILE; /* in front: it flies straight */
    }
    break;
  }
  case IT_OIL: hz_add(s, HZ_OIL, i, k->loc.s - 3.0f, k->loc.lat, 0.1f); break;
  case IT_MINE: hz_add(s, HZ_MINE, i, k->loc.s - 3.0f, k->loc.lat, 0.5f); break;
  case IT_SHIELD: k->shield_ticks = 8 * TICK_HZ; break;
  case IT_SHOCK:
    for (int j = 0; j < MAXK; j++) {
      if (j == i || !s->k[j].on) continue;
      if (s->k[j].place < k->place && s->k[j].shock_warn == 0) {
        s->k[j].shock_warn = SHOCK_WARN, s->k[j].shock_by = i;
        event(s, EV_WARN, j, i, s->k[j].p, 0);
      }
    }
    event(s, EV_HIT, i, i, k->p, (float)HZ_SHOCK);
    break;
  }
  k->item = IT_NONE;
  k->item_count = 0;
}

static void boxes_step(sim *s) {
  for (int b = 0; b < s->nbox; b++) {
    itembox *x = &s->box[b];
    if (x->cool > 0) {
      x->cool--;
      continue;
    }
    for (int i = 0; i < MAXK; i++) {
      kart *k = &s->k[i];
      if (!k->on || k->respawn_ticks > 0 || k->finished) continue;
      t3_vec3 d = v3sub(k->p, x->p);
      if (v3dot(d, d) > 2.6f * 2.6f) continue;
      x->cool = 6 * TICK_HZ;
      event(s, EV_BOX, i, b, x->p, 0);
      if (k->item == IT_NONE && k->item_wait == 0) {
        k->item_wait = 50;
        event(s, EV_ITEM_GET, i, 0, k->p, 0);
      }
      break;
    }
  }
}

/* ── driving ─────────────────────────────────────────────────────────── */

static void respawn(sim *s, int i) {
  kart *k = &s->k[i];
  float sd = k->last_safe_s - 18.0f;
  const track *t = s->track;
  if (!t->circuit && sd < 0) sd = 0;
  kart_place(s, i, sd, 0, 20.0f);
  k->respawn_ticks = 90;
  k->stall_ticks = 0;
  k->boost_ticks = 0;
  k->item_cool = 30;
  k->falls++;
  float L = t->length;
  float base = k->prog - fmodf(k->prog, L);
  float want = fmodf(sd + L * 2, L);
  float p = base + want;
  if (p > k->prog + L * 0.5f) p -= L;
  if (p < k->prog - L * 0.5f) p += L;
  k->prog = p;
  event(s, EV_RESPAWN, i, 0, k->p, 0);
}

/* throttle, brake and reverse as forces through the wheels that touch */
static void drive(sim *s, int i, float thr, float st, bool grounded_front) {
  kart *k = &s->k[i];
  const vehicle_def *vd = &VEH[k->veh];
  cw_vehicle3d *c = &PH.car[i];
  float spd = k->speed;
  float top = vd->top * s->class_top * (k->boost_ticks > 0 ? 1.0f + 0.42f * k->boost_k : 1.0f);
  /* the CPU karts hold the pack together: a kart far behind the leading human gets up to 7% more, one far ahead 4% less */
  float rb = 0, accel_k = 1;
  if (!k->human) {
    float lead = -1e9f;
    for (int j = 0; j < MAXK; j++)
      if (s->k[j].on && s->k[j].human && s->k[j].prog > lead) lead = s->k[j].prog;
    if (lead > -1e8f) rb = clampf((lead - k->prog) / 900.0f, -0.04f, 0.07f);
  }
  if (k->human) {
    /* a human far behind the pack gets up to 4% more top speed: the road is long, and so is the gap, but never hopeless */
    float lead = k->prog;
    for (int j = 0; j < MAXK; j++)
      if (s->k[j].on && s->k[j].prog > lead) lead = s->k[j].prog;
    rb = clampf((lead - k->prog) / 1500.0f, 0, 0.04f);
  }
  top *= 1.0f + rb;
  /* the slipstream: close behind another kart in its lane the air is thin. A little more top speed while it lasts, and a
   * short boost once the tow has held for a moment (with a pause before the next, so a train does not boost for ever) */
  if (k->draft_cool > 0) k->draft_cool--;
  {
    bool towed = false;
    if (s->phase == PH_RACE && k->grounded && spd > vd->top * s->class_top * 0.6f) {
      t3_vec3 fw = kart_fwd(k), rt = kart_right(k);
      for (int j = 0; j < MAXK && !towed; j++) {
        if (j == i || !s->k[j].on || s->k[j].spin_ticks > 0) continue;
        t3_vec3 d = v3sub(s->k[j].p, k->p);
        float ahead = v3dot(d, fw), side = v3dot(d, rt);
        towed = ahead > 3 && ahead < 18 && side < 2.0f && side > -2.0f && absf(v3dot(d, kart_up(k))) < 3;
      }
    }
    /* a close pass: shaving past another kart at speed pays a little boost (once a pass, with a pause) */
    if (k->pass_cool > 0) k->pass_cool--;
    if (s->phase == PH_RACE && k->grounded && spd > vd->top * 0.55f && k->pass_cool == 0) {
      t3_vec3 fw = kart_fwd(k), rt = kart_right(k);
      for (int j = 0; j < MAXK; j++) {
        if (j == i || !s->k[j].on || s->k[j].spin_ticks > 0) continue;
        t3_vec3 d = v3sub(s->k[j].p, k->p);
        float ahead = v3dot(d, fw), side = v3dot(d, rt), closing = spd - v3dot(s->k[j].v, fw);
        if (ahead < -0.5f && ahead > -2.5f && absf(side) > 1.0f && absf(side) < 2.1f && closing > 4 && absf(v3dot(d, kart_up(k))) < 2) {
          start_boost(s, i, 30, 0.7f * vd->boost, 6);
          event(s, EV_NEARMISS, i, j, k->p, closing);
          if (getenv("RACER_FALLLOG")) cw_info("nearmiss kart %d past %d closing %.1f", i, j, closing);
          k->pass_cool = 90;
          break;
        }
      }
    }
    if (towed) {
      top *= 1.03f;
      if (++k->draft_ticks >= 100 && k->draft_cool == 0) {
        start_boost(s, i, 45, 0.9f * vd->boost, 5);
        k->draft_ticks = 0;
        k->draft_cool = 240;
      }
    } else if (k->draft_ticks > 0) {
      k->draft_ticks = k->draft_ticks > 2 ? k->draft_ticks - 2 : 0;
    }
  }
  accel_k = 1.0f + 2.0f * rb;
  float acc = 0, brake = 0;
  if (thr > 0.05f) {
    if (spd < -0.5f) brake = 40;
    else {
      float f = 1.0f - clampf(spd / top, 0, 1);
      acc = thr * vd->accel * s->class_acc * accel_k * (0.12f + 0.88f * f * (2.0f - f));
      if (spd > top) acc = -(spd - top) * 2.0f;
    }
  } else if (thr < -0.05f) {
    if (spd > 0.5f) brake = 46;
    else if (spd > -12) acc = thr * 10.0f;
  } else brake = 4.0f;
  if (k->boost_ticks > 0) acc += 16.0f * k->boost_k * (1.0f - clampf(spd / (top + 2), 0, 1));
  (void)grounded_front;
  float m = vd->mass;
  int front = c->wheel[0].contact + c->wheel[1].contact, back = c->wheel[2].contact + c->wheel[3].contact;
  float kf = front + back ? (float)front / (float)(front + back) : 0.5f;
  /* rear-biased drive keeps the nose pointing where it is steered */
  float kfront = 0.35f * kf, krear = 1.0f - kfront;
  cw_vehicle3d_set_axle(c, 0, st, acc * m * kfront, brake * m * 0.5f);
  cw_vehicle3d_set_axle(c, 1, 0, acc * m * krear, brake * m * 0.5f);
}

static void kart_control(sim *s, int i, kin in) {
  kart *k = &s->k[i];
  const vehicle_def *vd = &VEH[k->veh];
  const track *t = s->track;
  cw_body3d b = PH.body[i];
  cw_vehicle3d *car = &PH.car[i];
  bool pressed_drift = (in.btn & KB_DRIFT) && !(k->last_btn & KB_DRIFT);
  bool pressed_item = (in.btn & KB_ITEM) && !(k->last_btn & KB_ITEM);
  bool pressed_trick = (in.btn & KB_TRICK) && !(k->last_btn & KB_TRICK);
  float thr = in.thr / 127.0f, steer = in.steer / 127.0f, pitch = in.pitch / 127.0f;
  if (s->phase != PH_RACE) thr = steer = pitch = 0;
  if (k->start_stall > 0) k->start_stall--, thr = 0;
  if (k->spin_ticks > 0) thr = steer = pitch = 0;

  t3_vec3 f = kart_fwd(k), up = kart_up(k);
  k->speed = v3dot(k->v, f);
  int contacts = car->contacts;
  bool touching = contacts > 0;
  bool grounded = contacts >= 2;
  if (grounded) {
    if (!k->grounded && k->air_ticks > 14) {
      float hard = clampf(-v3dot(k->v, up) / 22.0f, 0, 1);
      event(s, EV_LAND, i, 0, k->p, hard);
      if (k->trick_done) {
        start_boost(s, i, 60, 1.15f * vd->boost, 4);
        k->trick_done = false;
        k->tricks++;
      }
    }
    k->air_ticks = 0;
    k->air_peak = 0;
    k->trick_ticks = 0;
  } else if (!touching) {
    k->air_ticks++;
  }
  k->grounded = grounded;
  if (k->shock_warn > 0) {
    /* the alarm: a tap of the drift button hops (even straight), and a kart in the air when the wave arrives is over it */
    if (pressed_drift && grounded && !k->drifting && s->phase == PH_RACE) cw_body3d_apply_impulse(b, v3mul(up, 2.6f * vd->mass), NULL);
    if (--k->shock_warn == 0) {
      if (!grounded || k->air_ticks > 0) {
        event(s, EV_SHIELD, i, k->shock_by, k->p, 1);
        start_boost(s, i, 36, 0.8f * vd->boost, 4);
      } else {
        spin_kart(s, i, SPIN_SHOCK, k->shock_by);
      }
    }
  }

  /* track gravity: the road's own down near the road, the world's elsewhere */
  const tsample *ts = &t->s[k->loc.i];
  tsample m = track_at(t, k->loc.s);
  float h = k->loc.height;
  float lat_over = absf(k->loc.lat) - m.hw;
  bool gap = (ts->flags & TF_GAP) != 0 || (t->s[(k->loc.i + 1) % t->n].flags & TF_GAP) != 0;
  float attach = 0;
  if (!gap && lat_over < 2.5f && h > -1.0f) {
    float reach = (ts->flags & TF_LOOP) ? 2.3f : 3.0f;
    attach = clampf((reach - h) / 0.9f, 0, 1);
    float al = v3dot(up, m.u);
    if (al < 0.0f && !(ts->flags & TF_LOOP)) attach *= 0.0f;
  }
  float mass = vd->mass;
  if (attach > 0) {
    /* gravity leans towards the road's own down: all of the way in a loop or corkscrew (a kart needs no more speed
     * than it can drive), half way on a hill (a slope still slows a climb and speeds a drop, a little) */
    float blend = (ts->flags & TF_LOOP) ? 1.0f : 0.45f;
    t3_vec3 gdiff = v3sub((t3_vec3){ 0, 1, 0 }, m.u);
    cw_body3d_apply_force(b, v3mul(gdiff, GRAV * mass * attach * blend), NULL);
    float sp = absf(k->speed);
    float press = STICK * clampf(sp / 14.0f, 0, 1) * GRAV * mass;
    if (grounded || touching) cw_body3d_apply_force(b, v3mul(m.u, -press * attach), NULL);
  }

  /* spin out: no control, a spin about the up axis, and the road still holds it */
  if (k->spin_ticks > 0) {
    k->spin_ticks--;
    float rate = k->spin_kind == SPIN_BOOM ? 11.0f : 9.0f;
    float ph = (float)k->spin_ticks / (float)(k->spin_kind == SPIN_BOOM ? BOOM_SPIN : HIT_SPIN);
    if (k->spin_kind != SPIN_SHOCK && k->spin_kind != SPIN_OIL) {
      t3_vec3 w = v3add(k->w, v3mul(up, k->spin_dir * rate * ph * DT * 12));
      float wl = v3dot(w, up);
      w = v3add(v3sub(w, v3mul(up, wl)), v3mul(up, clampf(wl, -rate, rate)));
      cw_body3d_set_angular_velocity(b, w);
    } else {
      cw_body3d_set_angular_velocity(b, v3add(v3sub(k->w, v3mul(up, v3dot(k->w, up))), v3mul(up, k->spin_dir * 8.0f * (ph > 0.3f ? 1.0f : ph / 0.3f))));
    }
    /* slows to a crawl, then picks up */
    t3_vec3 fv = v3mul(f, k->speed);
    t3_vec3 slow = v3sub(k->v, v3mul(fv, 0.035f));
    if (grounded) cw_body3d_set_velocity(b, slow);
    drive(s, i, 0, 0, true);
    cw_vehicle3d_set_handbrake(car, false);
    k->drifting = false;
    k->last_btn = in.btn;
    cw_vehicle3d_update(car, DT);
    return;
  }

  /* drift */
  float sp = k->speed;
  if (!k->drifting && grounded && (in.btn & KB_DRIFT) && (pressed_drift || k->last_btn & KB_DRIFT) && absf(steer) > 0.25f && sp > DRIFT_MIN && !(ts->flags & (TF_LOOP | TF_GAP | TF_RAMP))) {
    k->drifting = true;
    k->drift_dir = steer > 0 ? 1 : -1; /* + right */
    k->drift_ticks = 0;
    k->drift_level = 0;
    /* a little hop into it */
    cw_body3d_apply_impulse(b, v3mul(up, 2.0f * mass), NULL);
  }
  if (k->drifting) {
    if (!(in.btn & KB_DRIFT) || sp < DRIFT_MIN * 0.6f || !touching) {
      if (k->drift_level > 0 && sp > DRIFT_MIN * 0.6f) {
        static const int T[4] = { 0, 42, 66, 96 };
        static const float K[4] = { 0, 1.0f, 1.18f, 1.4f };
        start_boost(s, i, T[k->drift_level], K[k->drift_level] * vd->boost, 2);
        k->drift_boosts++;
      }
      k->drifting = false;
      k->drift_ticks = 0;
      k->drift_level = 0;
    } else {
      float toward = steer * (float)k->drift_dir;
      k->drift_ticks += (int)((1.0f + 0.5f * clampf(toward, -0.5f, 1.0f)) * vd->drift * 100) ;
      static const int LV[3] = { 4500, 9000, 14500 };
      int lvl = k->drift_ticks >= LV[2] ? 3 : k->drift_ticks >= LV[1] ? 2 : k->drift_ticks >= LV[0] ? 1 : 0;
      if (lvl > k->drift_level) {
        k->drift_level = lvl;
        event(s, EV_DRIFT_LEVEL, i, lvl, k->p, 0);
      }
    }
  }

  /* steering: the wheel angle that gives the yaw rate the stick asks for at this speed */
  float aspd = absf(sp);
  float eff = steer;
  if (k->drifting) {
    float d = (float)k->drift_dir;
    eff = d * (0.55f + 0.45f * clampf(steer * d, -1.0f, 1.0f));
  }
  float rate = (2.35f - 1.15f * clampf(aspd / 40.0f, 0, 1)) * vd->turn * clampf(aspd / 3.0f + 0.15f, 0, 1);
  if (k->drifting) rate *= 1.18f;
  float ang = cw_atan2(rate * vd->len * 1.45f, maxf(aspd, 0.8f));
  ang = minf(ang, 0.62f);
  float st = -eff * ang;
  if (sp < -0.5f) st = -st;
  k->steer = st;
  /* the rear tyres let go in a drift */
  for (int w = 2; w < 4; w++) car->def.wheels[w].grip = TYRE_GRIP * vd->grip * (k->drifting ? 0.62f : 1.0f);
  for (int w = 0; w < 4; w++) {
    float g = TYRE_GRIP * vd->grip;
    if (ts->flags & TF_DRIFTY) g *= 0.42f;
    else if (ts->flags & TF_ROUGH) g *= 0.8f;
    if (w >= 2 && k->drifting) g *= 0.62f;
    car->def.wheels[w].grip = g;
  }
  if (ts->flags & TF_WATER) {
    k->v = v3mul(k->v, 1.0f);
    if (touching && k->speed > 8) cw_body3d_apply_force(b, v3mul(f, -k->speed * 0.55f * mass), NULL);
    if (touching && (s->tick & 7) == 0) event(s, EV_WATER, i, 0, k->p, clampf(k->speed / 40.0f, 0, 1));
  }
  drive(s, i, thr, st, true);

  /* the road's boost strips */
  if ((ts->flags & TF_BOOST) && touching && k->boost_ticks < 50) start_boost(s, i, 70, 1.1f, 0);

  /* grounded: the body lies on the road; the legs take what the surface asks. The turn that lays it flat is a rate
   * (so many radians a second per radian off), held hard in a loop or corkscrew where the road turns under the kart */
  bool in_loop = (ts->flags & TF_LOOP) != 0;
  if (attach > 0 && (touching || in_loop)) {
    t3_vec3 w = k->w;
    t3_vec3 ax = v3cross(up, m.u);
    float sa = v3len(ax);
    if (sa > 1e-5f) {
      float ang2 = cw_atan2(sa, v3dot(up, m.u));
      t3_vec3 axn = v3mul(ax, 1.0f / sa);
      float gain = (ts->flags & TF_LOOP) ? 16.0f : 7.0f;
      float target = ang2 * gain * (grounded ? 1.0f : touching ? 0.7f : 0.6f), have = v3dot(w, axn);
      w = v3add(w, v3mul(axn, (target - have) * 0.5f));
    }
    cw_body3d_set_angular_velocity(b, w);
  }

  /* in the air: a small say in pitch and yaw, a landing assist, the trick */
  if (!touching) {
    t3_vec3 w = k->w;
    t3_vec3 rt = kart_right(k);
    if (pitch != 0) w = v3add(w, v3mul(rt, pitch * 7.0f * DT));
    if (steer != 0) w = v3add(w, v3mul(up, -steer * 4.5f * DT));
    /* gently level out along the flight (wings level, nose along the path) */
    float spd = v3len(k->v);
    if (spd > 8 && k->air_ticks > 8 && !(attach > 0 && in_loop)) {
      t3_vec3 want_f = v3norm(k->v);
      t3_vec3 want_r = v3norm(v3cross(want_f, (t3_vec3){ 0, 1, 0 }));
      t3_vec3 want_u = v3cross(want_r, want_f);
      if (v3len(want_r) > 0.5f) {
        if (attach > 0) want_u = m.u;
        t3_vec3 ax = v3cross(up, want_u);
        float sa = v3len(ax);
        if (sa > 1e-5f) w = v3add(w, v3mul(ax, cw_atan2(sa, v3dot(up, want_u)) * 1.3f * DT / sa));
        t3_vec3 ay = v3cross(f, want_f);
        float sb = v3len(ay);
        if (sb > 1e-5f) w = v3add(w, v3mul(ay, cw_atan2(sb, v3dot(f, want_f)) * (pitch == 0 ? 1.1f : 0.2f) * DT / sb));
      }
    }
    w = v3mul(w, 1 - 0.6f * DT);
    cw_body3d_set_angular_velocity(b, w);
    if (pressed_trick && k->air_ticks > 10 && k->trick_ticks == 0 && !k->trick_done) {
      k->trick_ticks = 40;
      k->trick_done = true;
      event(s, EV_TRICK, i, 0, k->p, 0);
    }
    if (k->trick_ticks > 0) k->trick_ticks--;
  }

  /* items */
  if (k->item_wait > 0 && --k->item_wait == 0) {
    k->item = roll_item(s, k->place);
    k->item_count = k->item == IT_TURBO3 ? 3 : 1;
  }
  if (k->item_cool > 0) k->item_cool--;
  if (pressed_item) use_item(s, i);

  if (k->boost_ticks > 0) k->boost_ticks--;
  if (k->shield_ticks > 0) k->shield_ticks--;
  if (k->respawn_ticks > 0) k->respawn_ticks--;
  k->last_btn = in.btn;
  cw_vehicle3d_update(car, DT);
}

/* ── progress, the order, the finish ─────────────────────────────────── */

static void progress(sim *s, int i) {
  kart *k = &s->k[i];
  const track *t = s->track;
  float L = t->length;
  track_locate(t, k->p, k->hint, &k->loc);
  k->hint = k->loc.i;
  float ds = k->loc.s - PH.prev_s[i];
  if (t->circuit) {
    if (ds > L * 0.5f) ds -= L;
    if (ds < -L * 0.5f) ds += L;
  }
  if (absf(ds) < 40.0f) k->prog += ds; /* a jump can move it forty metres in a tick; more is a re-locate */
  PH.prev_s[i] = k->loc.s;
  if (!k->finished && k->prog >= (float)s->laps * L - 0.5f && s->phase == PH_RACE) {
    k->finished = true;
    k->finish_tick = s->race_ticks;
    s->finished_count++;
    float last = (float)(s->race_ticks - k->lap_start_tick) / TICK_HZ; /* the final lap counts too */
    if (s->laps > 1 && (k->best_lap == 0 || last < k->best_lap)) k->best_lap = last;
    event(s, EV_FINISH, i, s->finished_count, k->p, last);
  }
  int lap_now = k->prog < 0 ? 0 : (int)(k->prog / L) + 1;
  if (lap_now > s->laps) lap_now = s->laps;
  if (lap_now > k->lap && s->phase == PH_RACE) {
    k->lap = lap_now;
    float secs = (float)(s->race_ticks - k->lap_start_tick) / TICK_HZ;
    if (lap_now > 1 && (k->best_lap == 0 || secs < k->best_lap)) k->best_lap = secs;
    k->lap_start_tick = s->race_ticks;
    if (lap_now > 1) event(s, EV_LAP, i, lap_now, k->p, secs);
  }
  /* a safe place to come back to */
  if (k->grounded && k->loc.on_road && absf(k->loc.lat) < t->s[k->loc.i].hw - 0.5f && !(t->s[k->loc.i].flags & (TF_GAP | TF_RAMP))) {
    if (++k->safe_hold >= 24) {
      k->last_safe_s = k->loc.s;
      k->safe_hold = 0;
    }
  } else k->safe_hold = 0;
}

static void rank(sim *s) {
  int idx[MAXK], n = 0;
  for (int i = 0; i < MAXK; i++)
    if (s->k[i].on) idx[n++] = i;
  /* finished karts first in finish order, then by progress */
  for (int a = 1; a < n; a++) {
    int x = idx[a], b = a - 1;
    while (b >= 0) {
      const kart *p = &s->k[idx[b]], *q = &s->k[x];
      bool later;
      if (p->finished && q->finished) later = q->finish_tick < p->finish_tick;
      else if (p->finished) later = false;
      else if (q->finished) later = true;
      else later = q->prog > p->prog;
      if (!later) break;
      idx[b + 1] = idx[b];
      b--;
    }
    idx[b + 1] = x;
  }
  for (int a = 0; a < n; a++) {
    if (s->k[idx[a]].place != a + 1 && s->phase == PH_RACE && a > 0 && s->k[idx[a]].place == a + 2 &&
        s->k[idx[a - 1]].place == a)
      event(s, EV_OVERTAKE, idx[a], idx[a - 1], s->k[idx[a]].p, 0);
    s->k[idx[a]].place = a + 1;
    s->order[a] = idx[a];
  }
}

/* ── events from the physics ─────────────────────────────────────────── */

static int kart_of(cw_body3d b) {
  for (int i = 0; i < MAXK; i++)
    if (PH.body[i].id && PH.body[i].id == b.id) return i;
  return -1;
}

static void contacts(sim *s) {
  int ne = 0;
  const cw_phys_event3d *ev = cw_world3d_events(PH.w, &ne);
  for (int e = 0; e < ne; e++) {
    const cw_phys_event3d *h = &ev[e];
    if (h->type != CW_PHYS_HIT) continue;
    int a = kart_of(h->body_a), b = kart_of(h->body_b);
    if (a >= 0 && b >= 0) {
      /* the heavier shoves the lighter, sideways along the road: never up into the air */
      float ma = VEH[s->k[a].veh].mass, mb = VEH[s->k[b].veh].mass;
      t3_vec3 n = h->normal; /* from a to b */
      t3_vec3 ua = kart_up(&s->k[a]);
      n = v3norm(v3sub(n, v3mul(ua, v3dot(n, ua))));
      float shove = clampf(h->speed * 0.08f, 0, 2.5f);
      cw_body3d_apply_impulse(PH.body[b], v3mul(n, shove * ma * (ma / (ma + mb))), NULL);
      cw_body3d_apply_impulse(PH.body[a], v3mul(n, -shove * mb * (mb / (ma + mb))), NULL);
      if (h->speed > 4) event(s, EV_BUMP, a, b, h->point, clampf(h->speed / 20, 0, 1));
    } else if (a >= 0 || b >= 0) {
      int ki = a >= 0 ? a : b;
      kart *k = &s->k[ki];
      /* a wall takes some of the speed, kills the spin it would give and never throws the kart back */
      t3_vec3 n = a >= 0 ? h->normal : v3mul(h->normal, -1); /* kart to wall */
      t3_vec3 v = cw_body3d_velocity(PH.body[ki]);
      float vn = v3dot(v, n);
      if (vn > 0) v = v3sub(v, v3mul(n, vn));
      v = v3mul(v, h->speed > 8 ? 0.93f : 0.99f);
      cw_body3d_set_velocity(PH.body[ki], v);
      t3_vec3 w = cw_body3d_angular_velocity(PH.body[ki]);
      t3_vec3 up = kart_up(k);
      float wy = v3dot(w, up);
      cw_body3d_set_angular_velocity(PH.body[ki], v3add(v3sub(w, v3mul(up, wy)), v3mul(up, wy * 0.25f)));
      if (h->speed > 6) event(s, EV_WALL, ki, 0, h->point, clampf(h->speed / 25, 0, 1));
    }
  }
}

static void hash_state(sim *s) {
  uint32_t h = 2166136261u;
#define MIX(x)                                                                                                         \
  do {                                                                                                                 \
    uint32_t _v;                                                                                                       \
    memcpy(&_v, &(x), 4);                                                                                              \
    h = (h ^ _v) * 16777619u;                                                                                          \
  } while (0)
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &s->k[i];
    if (!k->on) continue;
    MIX(k->p.x); MIX(k->p.y); MIX(k->p.z); MIX(k->v.x); MIX(k->v.y); MIX(k->v.z);
    MIX(k->q.x); MIX(k->q.y); MIX(k->q.z); MIX(k->q.w); MIX(k->prog);
  }
  MIX(s->rng);
  uint32_t ph = cw_world3d_stats(PH.w).hash;
  MIX(ph);
#undef MIX
  s->hash = h;
}

void sim_step(sim *s, const kin in_raw[MAXK]) {
  s->tick++;
  s->phase_ticks++;
  s->ev_n = 0;
  kin in[MAXK];
  memcpy(in, in_raw, sizeof in);
  switch (s->phase) {
  case PH_GRID:
    if (s->phase_ticks >= 45) {
      s->phase = PH_COUNT;
      s->phase_ticks = 0;
    }
    break;
  case PH_COUNT:
    for (int i = 0; i < MAXK; i++) {
      kart *k = &s->k[i];
      if (!k->on) continue;
      bool gas = in[i].thr > 90;
      if (gas && s->phase_ticks < 140) k->launch = -1;
      else if (gas && s->phase_ticks >= 160 && k->launch == 0) k->launch = 1;
      if (!gas && s->phase_ticks < 160 && k->launch == -1) k->launch = 0; /* let go in time: forgiven */
    }
    if (s->phase_ticks % 60 == 1) event(s, EV_COUNT, 3 - s->phase_ticks / 60, 0, (t3_vec3){ 0, 0, 0 }, 0);
    if (s->phase_ticks >= 180) {
      s->phase = PH_RACE;
      s->phase_ticks = 0;
      s->race_ticks = 0;
      for (int i = 0; i < MAXK; i++) {
        kart *k = &s->k[i];
        k->lap_start_tick = 0;
        /* the start: throttle held through the last half second gives a rocket start; held too early floods the engine */
        if (k->on && k->launch == 1) start_boost(s, i, 60, 1.3f * VEH[k->veh].boost, 3);
        else if (k->on && k->launch == -1) k->start_stall = 40;
      }
      event(s, EV_GO, 0, 0, (t3_vec3){ 0, 0, 0 }, 0);
    }
    break;
  case PH_RACE: {
    s->race_ticks++;
    bool all_humans_done = true, any_human = false;
    for (int i = 0; i < MAXK; i++)
      if (s->k[i].on && s->k[i].human) {
        any_human = true;
        if (!s->k[i].finished) all_humans_done = false;
      }
    bool timeout = false;
    if (s->finished_count > 0) {
      int first_finish = 1 << 30;
      for (int i = 0; i < MAXK; i++)
        if (s->k[i].on && s->k[i].finished && s->k[i].finish_tick < first_finish) first_finish = s->k[i].finish_tick;
      timeout = s->race_ticks - first_finish > 40 * TICK_HZ;
    }
    if ((any_human && all_humans_done) || s->finished_count >= s->nkarts || timeout) {
      s->phase = PH_DONE;
      s->phase_ticks = 0;
    }
    break;
  }
  default: break;
  }
  /* the GO moment: a kart holding the throttle at the lights gets a start boost */
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on) continue;
    if (k->finished) in[i].thr = in[i].thr > 55 ? 55 : in[i].thr; /* past the line: a cruise (main.c hands it to the CPU driver) */
    if (k->respawn_ticks > 40) in[i].thr = 0;
    /* hold a kart that fell: the respawn */
    if (in[i].btn & KB_RESET) k->reset_hold++;
    else k->reset_hold = 0;
    kart_control(s, i, in[i]);
    PH.v0[i] = k->v;
  }
  cw_world3d_step(PH.w, DT);
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on) continue;
    cw_xform3 x = cw_body3d_transform(PH.body[i]);
    k->p = x.p;
    k->q = x.q;
    k->v = cw_body3d_velocity(PH.body[i]);
    k->w = cw_body3d_angular_velocity(PH.body[i]);
    float sp = v3len(k->v);
    if (sp > MAX_SPEED * s->class_top) {
      k->v = v3mul(k->v, MAX_SPEED * s->class_top / sp);
      cw_body3d_set_velocity(PH.body[i], k->v);
    }
    /* a loop or corkscrew is built for the standard class's best (its own top and a boost): faster than that the road cannot hold
     * the kart, so the speed class does not reach in there */
    if (k->loc.i >= 0 && (s->track->s[k->loc.i].flags & TF_GAP) && k->grounded == false) {
      /* in the air over a gap: the jump's landing is built for what the standard class can carry off its ramp */
      static float gcap = -1;
      if (gcap < 0) gcap = getenv("RACER_GAPCAP") ? (float)atof(getenv("RACER_GAPCAP")) : 50.0f;
      if (sp > gcap) {
        k->v = v3mul(k->v, gcap / sp);
        cw_body3d_set_velocity(PH.body[i], k->v);
      }
    }
    if (k->loc.i >= 0 && (s->track->s[k->loc.i].flags & TF_LOOP)) {
      static float capv = -1;
      if (capv < 0) capv = getenv("RACER_LOOPCAP") ? (float)atof(getenv("RACER_LOOPCAP")) : 40.0f;
      float cap = capv;
      if (sp > cap) {
        k->v = v3mul(k->v, cap / sp);
        cw_body3d_set_velocity(PH.body[i], k->v);
      }
    }
    k->top_speed_seen = maxf(k->top_speed_seen, sp);
    progress(s, i);
    /* falling off, flipped on its roof, or reset by hand */
    const track *t = s->track;
    const tsample *ts = &t->s[k->loc.i];
    bool lost = k->p.y < PH.min_y - 40.0f || (k->loc.dist > ts->hw * 2.5f + 14.0f) ||
                (k->loc.height < -9.0f && !(ts->flags & TF_GAP));
    bool flipped = k->grounded == false && k->loc.on_road && v3dot(kart_up(k), ts->u) < -0.35f && !(ts->flags & TF_LOOP) && k->loc.height < 1.3f;
    if (s->phase == PH_RACE && !k->finished && k->respawn_ticks <= 0 && k->spin_ticks <= 0 && absf(k->speed) < 2.5f) k->stall_ticks++;
    else k->stall_ticks = 0;
    if (flipped) k->stuck_ticks++;
    else if (k->stuck_ticks > 0) k->stuck_ticks--;
    if (k->respawn_ticks <= 0 && (lost || k->stuck_ticks > 150 || k->stall_ticks > 210 || k->reset_hold > 55) && !k->finished) {
      if (lost) event(s, EV_FALL, i, 0, k->p, 0);
      if (getenv("RACER_FALLLOG")) cw_info("respawn kart %d s=%.0f spd=%.0f lost=%d (dist %.1f hw %.1f height %.1f) stuck=%d stall=%d reset=%d", i, k->loc.s, k->speed, lost, k->loc.dist, ts->hw, k->loc.height, k->stuck_ticks, k->stall_ticks, k->reset_hold);
      respawn(s, i);
    }
  }
  contacts(s);
  boxes_step(s);
  hazards_step(s);
  rank(s);
  hash_state(s);
}
