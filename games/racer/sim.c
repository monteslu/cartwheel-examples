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
#include "arena.h"
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

const char *const ITEM_NAME[IT_KINDS] = { "-", "Turbo", "Triple Turbo", "Missile", "Homing Missile", "Oil Slick", "Mine", "Shield", "Shockwave", "Triple Missile", "Boomerang", "Bomb",
                                          "Flak Burst", "Shock Orb", "Detonate", "Rocket Pod", "Rail Lance" };

/* ── tuning ─────────────────────────────────────────────────────────── */

#define RIDE 0.62f      /* the body origin's height above the road at rest */
#define TRAVEL 0.40f
#define HZ_SPRING 3.0f
#define TYRE_GRIP 2.6f
#define MAX_SPEED 78.0f
#define STICK 0.45f     /* extra downforce, as a share of g, once moving */
#define DRIFT_MIN 13.0f /* m/s */
#define BOOST_TURBO 126 /* ticks */
#define HIT_SPIN 70
#define OIL_SPIN 60
#define BOOM_SPIN 85
#define SHOCK_SPIN 80
#define SHOCK_WARN 78 /* ticks between the alarm and the wave: a quick hop on the drift button clears it */
/* the boomerang: out this far (metres, from where it was thrown) at this speed over the thrower's, then back */
#define BOOM_OUT_M 40.0f
#define BOOM_REL 36.0f
#define BOOM_LIFE (5 * TICK_HZ)
#define BOOM_HITS 2
/* the bomb: lobbed, it fizzes this long once down, then a blast this wide (a kart inside BOMB_CORE is thrown) */
#define BOMB_FUSE 60
#define BOMB_R 7.0f
#define BOMB_CORE 3.5f

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
  /* assault: the arena's gates (arena_prim.gate), each a static body of its own while it stands */
  int ngate, gate[32];
  cw_body3d gateb[32];
  bool gate_up[32];
} PH;

/* battle mode (sim_battle.inc, included after spin_kart) */
static void battle_place_all(sim *s);
static void battle_boxes(sim *s);
static void battle_hit(sim *s, int i, int by, int kind);
static void battle_bolt(sim *s, int i);

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

static void car_make(int i, const vehicle_def *vd, bool arena) {
  cw_vehicle3d_def d = cw_vehicle3d_default_def();
  /* in an arena the tyres stand on its floors only (the floor, decks, ramps): a kart pressed along a wall or a rail rolled
   * onto it and drove up it and over the top */
  d.mask = arena ? 1u << L_TRACK : 1u << L_TRACK | 1u << L_WALL;
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
  if (!s->battle) track_phys_build(&PH.road, &PH.walls, s->track, &map->mesh);
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.name = "road";
  PH.trackb = cw_body3d_create(PH.w, &bd);
  if (s->battle) arena_phys_build(s->arena, PH.trackb, L_TRACK, L_WALL, 0); /* an arena: its floor, walls and cover instead of a road */
  if (s->battle) PH.ngate = arena_gates(s->arena, PH.gate, 32); /* its gates: put up by the round (gates_set) */
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
    car_make(i, vd, s->battle);
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
static void kart_place_m(sim *s, int i, tsample m, float lat, float speed);
static void kart_place(sim *s, int i, float sdist, float lat, float speed) { kart_place_m(s, i, track_at(s->track, sdist), lat, speed); }
static void kart_place_m(sim *s, int i, tsample m, float lat, float speed) {
  kart *k = &s->k[i];
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
  PH.prev_s[i] = k->loc.ms;
}

void sim_place_on_grid(sim *s) {
  if (s->battle) {
    battle_place_all(s);
    return;
  }
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
    PH.prev_s[i] = k->loc.ms;
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
      s->box[s->nbox++] = (itembox){ a->s, lat, track_point(t, a->s, lat, 1.25f), 0 };
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
  s->battle = map->arena != NULL;
  s->arena = arena_load(map->arena);
  if (s->battle) s->laps = 1, s->battle_limit = 180 * TICK_HZ;
  for (int i = 0; i < MAXK; i++) {
    s->k[i].on = on[i];
    s->k[i].veh = veh[i] % NVEH;
    s->k[i].place = i + 1;
    s->nkarts += on[i];
  }
  world_make(s, map, on, veh);
  if (s->battle) battle_boxes(s);
  else make_boxes(s);
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
  if (s->battle) s->class_top *= 0.85f; /* an arena is small: battles run a little slower than races */
}

void sim_restart(sim *s, uint32_t seed, int skip_ticks) {
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    kart keep = { .on = k->on, .human = k->human, .veh = k->veh, .colour = k->colour, .place = i + 1, .team = k->team };
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
  /* RACER_TEST_YAW=degrees: turned that far off the road's heading (+ = to the right), moving the way it faces */
  if (getenv("RACER_TEST_YAW")) {
    float a = (float)atof(getenv("RACER_TEST_YAW")) * 0.01745329f;
    tsample m = track_at(s->track, sd);
    t3_vec3 f = v3add(v3mul(m.f, cw_cos(a)), v3mul(m.r, cw_sin(a))), r = v3sub(v3mul(m.r, cw_cos(a)), v3mul(m.f, cw_sin(a)));
    kart *k = &s->k[i];
    k->q = q_from_frame(r, m.u, f);
    cw_vehicle3d_teleport(&PH.car[i], (cw_xform3){ k->p, k->q });
    cw_body3d_set_velocity(PH.body[i], v3mul(f, speed));
    k->v = v3mul(f, speed);
  }
  s->k[i].prog = sd;
  s->k[i].last_safe_s = sd;
}

/* dev aid: the same on branch b, bs metres from its mouth */
void sim_put_branch(sim *s, int i, int b, float bs, float speed) {
  if (b < 0 || b >= s->track->nbranch) return;
  kart_place_m(s, i, track_branch_at(s->track, b, bs), 0, speed);
  s->k[i].prog = s->k[i].loc.ms;
  s->k[i].last_safe_s = s->track->br[b].s0 - 10.0f;
}

static void use_item(sim *s, int i);
/* dev aid: kart i uses an item now, whatever it held */
void sim_use(sim *s, int i, int item) {
  s->k[i].item = item, s->k[i].item_count = item == IT_TURBO3 || item == IT_MISSILE3 ? 3 : 1;
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

static void start_boost(sim *s, int i, int ticks, float strength, int source);

static void spin_kart(sim *s, int i, int kind, int by) {
  kart *k = &s->k[i];
  if (k->respawn_ticks > 0 || k->finished) return;
  if (by >= 0 && by < MAXK && by != i && !kart_foe(s, i, by)) return; /* capture the flag: no friendly fire */
  if (s->battle && (k->out || k->ram_cool > 0 || k->spin_ticks > 0 || s->phase != PH_RACE)) return; /* a moment's grace after each pop */
  if (k->shield_ticks > 0) {
    k->shield_ticks = 0;
    event(s, EV_SHIELD, i, by, k->p, 1);
    return;
  }
  /* inside a loop or corkscrew a hit only shakes a kart and costs it speed: spun round in the tube it rolled through the rest
   * of it backwards (Wreck Orbit's first corkscrew, 17 s), and it cannot steer out of a tube anyway */
  /* and on a jump's run-up or in its flight a hit only shakes it, at full speed: spun on the ramp at 62 m/s a kart left the
   * lip at 25 and dropped into Wreck Orbit's pit */
  bool on_jump = false;
  if (!s->battle && k->loc.i >= 0 && k->loc.br < 0) {
    uint32_t fl = s->track->s[k->loc.i].flags;
    on_jump = (fl & TF_GAP) != 0;
    for (int g = 0; g < s->track->ngaps && !on_jump && (fl & TF_RAMP); g++) {
      float ahead = s->track->s[s->track->gap_from[g]].s - k->loc.s;
      if (s->track->circuit && ahead < 0) ahead += s->track->length;
      on_jump = ahead >= 0 && ahead < 70.0f;
    }
  }
  if (on_jump) {
    k->hits_taken++;
    if (by >= 0 && by != i) s->k[by].hits_given++;
    event(s, EV_SPIN, i, by, k->p, (float)kind);
    return;
  }
  if (!s->battle && k->loc.i >= 0 && (s->track->s[k->loc.i].flags & TF_LOOP)) {
    t3_vec3 v = cw_body3d_velocity(PH.body[i]);
    cw_body3d_set_velocity(PH.body[i], v3mul(v, 0.75f));
    k->hits_taken++;
    if (by >= 0 && by != i) s->k[by].hits_given++;
    event(s, EV_SPIN, i, by, k->p, (float)kind);
    return;
  }
  if (k->spin_ticks > 20 && kind != SPIN_BOOM) return;
  /* a race: a second after a spin is over before another can start (a missile, a mine or oil right as it recovered
   * spun it again with no way to answer) */
  if (!s->battle && k->ram_cool > 0 && kind != SPIN_BOOM) return;
  k->spin_kind = kind;
  if (k->trailing) k->trailing = false, k->item = IT_NONE, k->item_count = 0; /* a hit knocks the held item away */
  k->spin_ticks = kind == SPIN_OIL ? OIL_SPIN : kind == SPIN_BOOM ? BOOM_SPIN : kind == SPIN_SHOCK ? SHOCK_SPIN : HIT_SPIN;
  if (!s->battle) k->ram_cool = k->spin_ticks + TICK_HZ;
  k->spin_dir = (rnd(s) & 1) ? 1.0f : -1.0f;
  k->drifting = false;
  k->drift_ticks = 0;
  k->boost_ticks = 0;
  k->hits_taken++;
  if (by >= 0 && by != i) s->k[by].hits_given++;
  event(s, EV_SPIN, i, by, k->p, (float)kind);
  if (kind == SPIN_BOOM) {
    /* the blast throws it up off the road; in a loop or corkscrew up is across the tube, so only a hop there (the full
     * throw put karts hit upside down out of Orbit's corkscrews) */
    bool looped = k->loc.i >= 0 && (s->track->s[k->loc.i].flags & TF_LOOP);
    cw_body3d_apply_impulse(PH.body[i], v3mul(kart_up(k), (looped ? 2.0f : 9.0f) * VEH[k->veh].mass), NULL);
  }
  if (s->battle) battle_hit(s, i, by, kind);
}

/* a hazard that passes kart j by: a teammate's in a team round (a turret's, owner -2 - its team: its own team's karts) */
static bool hz_spares(const sim *s, const hazard *z, int j) {
  if (z->owner <= -2) return mode_teams(s) && s->k[j].team == -2 - z->owner;
  return j != z->owner && z->owner >= 0 && !kart_foe(s, j, z->owner);
}
static void assault_blast(sim *s, t3_vec3 at, float r, int dmg, int by);

/* a bomb goes off at z->p: everyone within BOMB_R spins (thrown inside BOMB_CORE), its thrower too. One EV_HIT with no
 * victim (a = -1) for the blast itself, then one for each kart caught (for the announcer; the effects skip those) */
static void bomb_blast(sim *s, hazard *z) {
  event(s, EV_HIT, -1, z->owner, z->p, (float)HZ_BOMB);
  if (s->battle) assault_blast(s, z->p, BOMB_R, 25, z->owner);
  for (int j = 0; j < MAXK; j++) {
    kart *o = &s->k[j];
    if (!o->on || o->out || o->parked || o->respawn_ticks > 0) continue;
    if (hz_spares(s, z, j)) continue;
    t3_vec3 d = v3sub(o->p, z->p);
    float dd = v3dot(d, d);
    if (dd > BOMB_R * BOMB_R) continue;
    event(s, EV_HIT, j, z->owner, o->p, (float)HZ_BOMB);
    spin_kart(s, j, dd < BOMB_CORE * BOMB_CORE ? SPIN_BOOM : SPIN_HIT, z->owner);
  }
  z->kind = HZ_NONE;
}

/* a hop timed as something arrives (the drift button in the last sixth of a second): it goes by underneath, with the near
 * miss's little boost */
static bool hopped(sim *s, int i, const hazard *z) {
  kart *k = &s->k[i];
  if (k->hop_tick <= 0 || s->tick - k->hop_tick > 10) return false;
  event(s, EV_NEARMISS, i, z->owner, z->p, 0);
  start_boost(s, i, 30, 0.7f * VEH[k->veh].boost, 6);
  return true;
}

#include "sim_battle.inc"

/* the road's wrapped distance from a to b (b ahead: positive) */
static float road_ds(const track *t, float a, float b) {
  float d = b - a;
  if (t->circuit) {
    if (d > t->length * 0.5f) d -= t->length;
    if (d < -t->length * 0.5f) d += t->length;
  }
  return d;
}

/* a boomerang or a bomb on the road (track coordinates, as the missiles):
 *   boomerang  out along the road at BOOM_REL over the thrower's speed for BOOM_OUT_M, then back to the thrower, who
 *              catches it; any other kart it meets spins (once each, BOOM_HITS at most); a hop lets it by
 *   bomb       an arc ahead (or behind) down onto the road, BOMB_FUSE of fizzing, then the blast; another kart running into
 *              it sets it off at once */
static void thrown_step(sim *s, hazard *z) {
  const track *t = s->track;
  z->age++;
  if (--z->life <= 0) {
    if (z->kind == HZ_BOMB) bomb_blast(s, z);
    else z->kind = HZ_NONE;
    return;
  }
  const kart *own = z->owner >= 0 ? &s->k[z->owner] : NULL;
  if (z->kind == HZ_BOOMERANG) {
    if (z->state == 0) {
      z->s += z->speed * DT;
      z->dist += BOOM_REL * DT;
      if (z->dist >= BOOM_OUT_M) z->state = 1;
    } else if (own && own->on && !own->finished) {
      /* back to the thrower: at its speed and BOOM_REL more, along the road and across it */
      float ds = road_ds(t, z->s, own->loc.br < 0 ? own->loc.ms : own->loc.s);
      z->s += own->speed * DT + clampf(ds, -BOOM_REL * DT, BOOM_REL * DT);
      z->lat += clampf((own->loc.br < 0 ? own->loc.lat : 0) - z->lat, -16 * DT, 16 * DT);
      z->h += (1.0f - z->h) * 0.1f;
    } else z->s += 30.0f * DT;
  } else if (z->state == 0) {
    z->s += z->speed * DT;
    z->h += z->vy * DT;
    z->vy -= GRAV * DT;
    if (z->h <= 0.35f && z->vy < 0) z->h = 0.35f, z->state = 1, z->life = BOMB_FUSE;
  }
  if (t->circuit) z->s = fmodf(z->s + t->length, t->length);
  else if (z->s > t->length || z->s < 0) {
    if (z->kind == HZ_BOMB) z->s = clampf(z->s, 0, t->length);
    else {
      z->kind = HZ_NONE;
      return;
    }
  }
  tsample m = track_at(t, z->s);
  z->p = v3add(m.p, v3add(v3mul(m.r, z->lat), v3mul(m.u, z->h)));
  z->f = m.f;
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on || k->respawn_ticks > 0 || k->finished) continue;
    t3_vec3 d = v3sub(k->p, z->p);
    if (z->kind == HZ_BOMB) {
      /* down and fizzing: another kart running into it sets it off */
      if (z->state == 1 && i != z->owner && v3dot(d, d) < 2.2f * 2.2f) {
        bomb_blast(s, z);
        return;
      }
      continue;
    }
    if (i == z->owner) {
      /* caught on its way back */
      if (z->state == 1 && z->age > 30 && v3dot(d, d) < 2.6f * 2.6f) {
        z->kind = HZ_NONE;
        return;
      }
      continue;
    }
    if ((z->hitmask >> i) & 1 || v3dot(d, d) > 1.9f * 1.9f) continue;
    z->hitmask |= 1 << i;
    if (hopped(s, i, z)) continue;
    event(s, EV_HIT, i, z->owner, z->p, (float)HZ_BOOMERANG);
    spin_kart(s, i, SPIN_HIT, z->owner);
    if (++z->hits >= BOOM_HITS) {
      z->kind = HZ_NONE;
      return;
    }
  }
}

static void hazards_step(sim *s) {
  if (s->battle) {
    battle_hazards_step(s);
    return;
  }
  const track *t = s->track;
  for (int h = 0; h < MAXHZ; h++) {
    hazard *z = &s->hz[h];
    if (!z->kind) continue;
    if (z->kind == HZ_BOOMERANG || z->kind == HZ_BOMB) {
      thrown_step(s, z);
      continue;
    }
    z->age++;
    if (--z->life <= 0) {
      z->kind = HZ_NONE;
      continue;
    }
    if (z->kind == HZ_MISSILE || z->kind == HZ_HOMING) {
      if (z->kind == HZ_HOMING && z->target >= 0 && s->k[z->target].on) {
        const kart *tk = &s->k[z->target];
        /* (a mark on a branch is off the missile's road: it holds its lane and keeps after it) */
        float dl = tk->loc.br < 0 ? tk->loc.lat - z->lat : 0;
        z->lat += clampf(dl, -14 * DT, 14 * DT);
        /* it closes on its mark: a touch faster when behind it */
        float ds = tk->loc.ms - z->s;
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
      /* an oil slick or a mine never catches the kart that dropped it until that kart has got clear of it once (a slow
       * kart, or a blast set off by a kart right behind, hit its owner the moment it was dropped) */
      if (z->owner == i && (z->kind == HZ_OIL || z->kind == HZ_MINE)) {
        t3_vec3 od = v3sub(k->p, z->p);
        if (!z->owner_clear && v3dot(od, od) > 8.0f * 8.0f) z->owner_clear = true;
        if (!z->owner_clear) continue;
      }
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
          if (j == i || !s->k[j].on || (j == z->owner && !z->owner_clear)) continue;
          t3_vec3 e = v3sub(s->k[j].p, z->p);
          if (v3dot(e, e) < 4.5f * 4.5f) spin_kart(s, j, SPIN_HIT, z->owner);
        }
        z->kind = HZ_NONE;
      } else {
        /* a hop timed as it arrives (the drift button in the last sixth of a second) and it goes by underneath: a
         * close call, with the near miss's little boost. The counterplay the HUD's INCOMING warning asks for */
        if (k->hop_tick > 0 && s->tick - k->hop_tick <= 10) {
          event(s, EV_NEARMISS, i, z->owner, z->p, 0);
          start_boost(s, i, 30, 0.7f * VEH[k->veh].boost, 6);
          z->target = -1;
          z->owner = i; /* and it does not turn back for the one who dodged it */
          z->age = 0;
          break;
        }
        /* a missile from behind meets the item held out back first */
        if (k->trailing && v3dot(v3sub(z->p, k->p), kart_fwd(k)) < 0) {
          k->trailing = false;
          k->item = IT_NONE, k->item_count = 0;
          event(s, EV_SHIELD, i, z->owner, z->p, 1);
          z->kind = HZ_NONE;
          break;
        }
        if (getenv("RACER_FALLLOG")) cw_info("missile hit kart %d tick %d race %d (hop %d)", i, s->tick, s->race_ticks, k->hop_tick);
        event(s, EV_HIT, i, z->owner, z->p, (float)z->kind);
        spin_kart(s, i, SPIN_HIT, z->owner);
        z->kind = HZ_NONE;
      }
      break;
    }
  }
}

/* ── items ───────────────────────────────────────────────────────────── */

/* the odds by position (after Mario Kart 8's datamined tables): the leader gets defence (oil, mines, a shield), the
 * middle the attack (missiles, the homing one only there), the back speed (turbos, the triple) and the Shockwave */
static const uint8_t ITEM_W[IT_KINDS][MAXK] = {
  [IT_TURBO] = { 6, 14, 18, 18, 16, 14, 10, 8 },
  [IT_TURBO3] = { 0, 2, 6, 10, 16, 22, 28, 32 },
  [IT_MISSILE] = { 24, 24, 20, 16, 12, 8, 6, 4 },
  [IT_HOMING] = { 0, 4, 10, 14, 14, 10, 6, 2 },
  [IT_OIL] = { 30, 22, 16, 12, 8, 6, 4, 2 },
  [IT_MINE] = { 28, 20, 14, 10, 8, 6, 4, 2 },
  [IT_SHIELD] = { 12, 10, 10, 10, 10, 8, 6, 4 },
  [IT_SHOCK] = { 0, 0, 2, 6, 10, 16, 20, 22 },
  [IT_MISSILE3] = { 0, 4, 8, 10, 10, 6, 3, 0 },
  [IT_BOOMERANG] = { 4, 8, 10, 10, 8, 6, 4, 2 },
  [IT_BOMB] = { 0, 2, 6, 10, 12, 14, 14, 12 },
};
/* metres behind the leader that count as each position for the odds: a kart far back draws from the back's table
 * whatever its place (Mario Kart 8 picks by distance; place alone gave a kart half a length behind the leader the
 * same as one a straight away) */
static const float ITEM_GAP_M[MAXK] = { 0, 12, 30, 60, 100, 150, 220, 300 };


/* how far this kart is behind the leader, in metres of race progress */
static float item_gap_m(const sim *s, int i) {
  float lead = s->k[i].prog;
  for (int j = 0; j < MAXK; j++)
    if (s->k[j].on && s->k[j].prog > lead) lead = s->k[j].prog;
  return lead - s->k[i].prog;
}

static int roll_item(sim *s, int place, float gap_m, bool final_lap) {
  int p = place - 1;
  for (int g = MAXK - 1; g > 0; g--)
    if (gap_m >= ITEM_GAP_M[g]) {
      if (g > p) p = g;
      break;
    }
  if (p < 0) p = 0;
  if (p > MAXK - 1) p = MAXK - 1;
  int w[IT_KINDS] = { 0 }, total = 0;
  for (int i = 1; i < IT_KINDS; i++) {
    w[i] = ITEM_W[i][p];
    /* turbos only: the party preset with no weapons */
    if (s->item_set == 1 && i != IT_TURBO && i != IT_TURBO3) w[i] = 0;
    /* the last lap is decided by driving more than by a Shockwave from the back */
    if (final_lap && i == IT_SHOCK) w[i] /= 3;
    total += w[i];
  }
  if (total <= 0) return IT_TURBO;
  int r = (int)(rnd(s) % (uint32_t)total);
  for (int i = 1; i < IT_KINDS; i++) {
    r -= w[i];
    if (r < 0) return i;
  }
  return IT_TURBO;
}

/* a turbo's kick: metres a second added along the nose at once, grounded (in the air it only boosts) */
static void turbo_kick(sim *s, int i, float dv) {
  kart *k = &s->k[i];
  if (!k->grounded) return;
  t3_vec3 v = cw_body3d_velocity(PH.body[i]);
  cw_body3d_set_velocity(PH.body[i], v3add(v, v3mul(kart_fwd(k), dv)));
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
  int item = k->item == IT_MISSILE3 ? IT_MISSILE : k->item; /* a triple missile fires missiles, one a press */
  event(s, EV_ITEM_USE, i, item, k->p, 0);
  k->item_cool = 18;
  if (s->battle && battle_use_item(s, i, item)) {
    if (k->item == IT_MISSILE3 && --k->item_count > 0) return;
    if (item == IT_ORB) { /* the orb is out: the button is its detonator now */
      k->item = IT_DETONATE, k->item_count = 1, k->item_cool = 10;
      return;
    }
    k->item = IT_NONE;
    k->item_count = 0;
    return;
  }
  /* hazards live on the main road: from a branch they start at the nearest point of it */
  track_loc il = k->loc;
  if (il.br >= 0) {
    track_locate_main(s->track, k->p, k->loc.i, &il);
    float hw = s->track->s[il.i].hw - 1.0f;
    il.lat = clampf(il.lat, -hw, hw);
  }
  switch (item) {
  /* a turbo hits at once (a kick forward on the spot) and pulls hard for a while after: it was a slow swell */
  case IT_TURBO:
    start_boost(s, i, BOOST_TURBO, 1.7f * vd->boost, 1);
    turbo_kick(s, i, 7.0f);
    break;
  case IT_TURBO3:
    start_boost(s, i, 90, 1.6f * vd->boost, 1);
    turbo_kick(s, i, 6.0f);
    if (--k->item_count > 0) return;
    break;
  case IT_MISSILE: {
    hazard *z = hz_add(s, HZ_MISSILE, i, il.s + 2.5f, il.lat, clampf(k->loc.height + 0.5f, 0.5f, 2.0f));
    if (z) z->speed = maxf(70.0f, k->speed + 38.0f);
    if (k->item == IT_MISSILE3 && --k->item_count > 0) return;
    break;
  }
  case IT_BOOMERANG: {
    hazard *z = hz_add(s, HZ_BOOMERANG, i, il.s + 2.5f, il.lat, clampf(k->loc.height + 0.8f, 0.8f, 2.0f));
    if (z) z->speed = maxf(k->speed, 0) + BOOM_REL, z->life = BOOM_LIFE;
    break;
  }
  case IT_BOMB: {
    /* lobbed ahead, or behind with the brake held; about 0.85 s in the air */
    bool back = k->last_thr < -40;
    hazard *z = hz_add(s, HZ_BOMB, i, il.s + (back ? -1.5f : 2.0f), il.lat, clampf(k->loc.height + 1.2f, 1.2f, 2.5f));
    if (z) z->speed = maxf(k->speed, 0) + (back ? -14.0f : 26.0f), z->vy = 7.0f, z->life = 4 * TICK_HZ;
    break;
  }
  case IT_HOMING: {
    hazard *z = hz_add(s, HZ_HOMING, i, il.s + 2.5f, il.lat, clampf(k->loc.height + 0.5f, 0.5f, 2.0f));
    if (z) {
      z->speed = maxf(60.0f, k->speed + 30.0f);
      z->target = kart_ahead(s, i);
      if (z->target < 0) z->kind = HZ_MISSILE; /* in front: it flies straight */
    }
    break;
  }
  case IT_OIL: hz_add(s, HZ_OIL, i, il.s - 3.0f, il.lat, 0.1f); break;
  case IT_MINE: hz_add(s, HZ_MINE, i, il.s - 3.0f, il.lat, 0.5f); break;
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
      if (!k->on || k->respawn_ticks > 0 || k->finished || k->out) continue;
      t3_vec3 d = v3sub(k->p, x->p);
      if (v3dot(d, d) > 2.6f * 2.6f) continue;
      x->cool = (s->battle && s->amode == AM_ASSAULT ? 4 : 6) * TICK_HZ; /* (an assault burns through items: quicker back) */
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
  float top = vd->top * s->class_top * (k->boost_ticks > 0 ? 1.0f + 0.5f * k->boost_k : 1.0f);
  /* the CPU karts hold the pack together: one behind the leading human gets up to 4% more, one ahead up to 12% less
   * (full at 60 m clear), and none is faster flat out than 39 m/s whatever it drives: a CPU in the Longhaul (41) used to
   * pull away for good, "flat out faster and can't be caught" */
  float rb = 0, accel_k = 1;
  if (!k->human) {
    float lead = -1e9f;
    for (int j = 0; j < MAXK; j++)
      if (s->k[j].on && s->k[j].human && s->k[j].prog > lead) lead = s->k[j].prog;
    if (lead > -1e8f) rb = lead >= k->prog ? minf((lead - k->prog) / 900.0f, 0.04f) : -minf((k->prog - lead) / 500.0f, 0.12f);
    float vt = vd->top * s->class_top;
    if (vt > 39.0f * s->class_top) top *= 39.0f * s->class_top / vt;
  }
  if (k->human) {
    /* a human behind the pack gets up to 6% more top speed (full at 40 m back): the gap is never hopeless */
    float lead = k->prog;
    for (int j = 0; j < MAXK; j++)
      if (s->k[j].on && s->k[j].prog > lead) lead = s->k[j].prog;
    rb = clampf((lead - k->prog) / 660.0f, 0, 0.06f);
    /* and by place: 6th, 7th and 8th get 4, 6 and 8% (whichever help is bigger) */
    if (!s->battle && k->place >= 6) rb = maxf(rb, 0.02f * (float)(k->place - 4));
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
  float boost_acc = k->boost_ticks > 0 ? 20.0f * k->boost_k * (1.0f - clampf(spd / (top + 2), 0, 1)) : 0;
  (void)grounded_front;
  float m = vd->mass;
  /* the boost pushes the kart along its nose at its middle, not through the tyres: through the (rear-biased) wheels it
   * used up their grip and the back stepped out, a spin from every boost taken in a bend */
  if (boost_acc > 0 && (c->wheel[0].contact || c->wheel[1].contact || c->wheel[2].contact || c->wheel[3].contact))
    cw_body3d_apply_force(PH.body[i], v3mul(kart_fwd(k), boost_acc * m), NULL);
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
  k->last_thr = in.thr;
  if (s->phase != PH_RACE) thr = steer = pitch = 0;
  if (k->start_stall > 0) k->start_stall--, thr = 0;
  if (k->spin_ticks > 0) thr = steer = pitch = 0;

  t3_vec3 f = kart_fwd(k), up = kart_up(k);
  k->speed = v3dot(k->v, f);
  int contacts = car->contacts;
  bool touching = contacts > 0;
  bool grounded = contacts >= 2;
  /* the first touch after a long drop in an arena (off a tower's roof): the fall stops there, no tumble, the run kept */
  if (touching && s->battle && k->air_ticks > 60) {
    cw_body3d_set_angular_velocity(b, (t3_vec3){ 0, 0, 0 });
    cw_body3d_set_velocity(b, (t3_vec3){ k->v.x, fmaxf(k->v.y, 0), k->v.z });
  }
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
    if (!k->grounded && k->air_ticks > 6) k->land_grace = 24;
    k->air_ticks = 0;
    k->air_peak = 0;
    k->trick_ticks = 0;
  } else if (!touching) {
    k->air_ticks++;
    if (k->loc.height > k->air_peak) k->air_peak = k->loc.height;
    /* a long drop in an arena (off a tower's roof) lands, it does not crash: coming down hard over a floor, the fall
     * eases to a firm landing in the last quarter second, the run along the nose kept */
    if (s->battle && k->air_ticks > 20 && k->v.y < -8.0f && k->trick_ticks == 0)
      cw_body3d_set_angular_velocity(b, v3mul(v3cross(up, (t3_vec3){ 0, 1, 0 }), 4.0f)); /* level out, the heading kept */
    if (s->battle && k->v.y < -14.0f) {
      float fl = arena_level_below(s->arena, k->p.x, k->p.y, k->p.z);
      if ((fl > 0.5f || arena_on_terrain(s->arena, k->p)) && k->p.y - fl < -k->v.y * 0.25f) cw_body3d_set_velocity(b, (t3_vec3){ k->v.x, -14.0f, k->v.z });
    }
  }
  k->grounded = grounded;
  /* every hop's tick: a missile met just after one goes by underneath */
  if (pressed_drift && s->phase == PH_RACE) k->hop_tick = s->tick;
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
  tsample m = track_at_loc(t, &k->loc, 0);
  float h = k->loc.height;
  float lat_over = absf(k->loc.lat) - m.hw;
  bool gap = (ts->flags & TF_GAP) != 0 || (t->s[track_next(t, k->loc.i)].flags & TF_GAP) != 0;
  /* the ramp carries you: on the last stretch before a jump's lip a kart heading over it slower than the jump was built
   * for is brought up to that speed, so the landing is always reached (a short one met the far deck's underside) */
  /* (whichever way the kart faces: one spun round just before the lip arrived at 1 m/s and dropped into the pit; the push is
   * along the road, so it crosses either way) */
  if (grounded && k->loc.br < 0 && (ts->flags & TF_RAMP) && !(ts->flags & TF_LOOP) && !s->battle)
    for (int g = 0; g < t->ngaps; g++) {
      float ahead = t->s[t->gap_from[g]].s - ts->s;
      if (t->circuit && ahead < 0) ahead += t->length;
      float along = v3dot(k->v, ts->f);
      if (ahead > 0 && ahead < 14.0f && along < t->gap_v[g]) {
        float need = fminf(t->gap_v[g] - along, 2.5f); /* a firm push each tick, not a teleport of speed */
        cw_body3d_set_velocity(b, v3add(k->v, v3mul(ts->f, need)));
        k->v = v3add(k->v, v3mul(ts->f, need));
        break;
      }
    }
  /* on an unwalled edge (a rope bridge) the tyres bite hard against a sideways slide: a kart clipped in a pass slid off the
   * temple's bridge sideways at 13 m/s with its nose still straight down the road. Pointed off the edge, it still drives off */
  if (touching && (ts->flags & TF_OPEN) && !s->battle && k->spin_ticks == 0 && !k->drifting) {
    t3_vec3 rt = kart_right(k);
    float slip = v3dot(k->v, rt);
    if (absf(slip) > 1.0f) {
      t3_vec3 dv = v3mul(rt, -slip * 0.25f);
      cw_body3d_set_velocity(b, v3add(k->v, dv));
      k->v = v3add(k->v, dv);
    }
  }
  /* and through a loop or corkscrew it keeps a kart rolling: one slowed by a bump in the tube used to stall there until the
   * stuck timer put it back (3.5 s gone) */
  if (touching && (ts->flags & TF_LOOP) && !s->battle && k->spin_ticks == 0) {
    float along = v3dot(k->v, ts->f);
    if (along < 14.0f) {
      float need = fminf(14.0f - along, 0.6f);
      cw_body3d_set_velocity(b, v3add(k->v, v3mul(ts->f, need)));
      k->v = v3add(k->v, v3mul(ts->f, need));
    }
  }
  float attach = 0;
  if (!gap && lat_over < ((ts->flags & TF_LOOP) ? 6.0f : 2.5f) && h > -1.0f && !s->battle) {
    /* in a loop or corkscrew the road keeps its hold a long way out: a kart bounced off it upside down was dropped out
     * of the tube (2.3 m was all it had) */
    float reach = (ts->flags & TF_LOOP) ? 7.0f : 3.0f;
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
    /* over a crest the road falls away faster than gravity can follow at speed (v^2 x its downward curve beyond 0.85 g, up to 6 g: a boosted kart at 68 m/s off the top of the temple's 13 degree descent needed 5):
     * the road holds the kart down the rest of the way, up to 2 g, so a crest is a lift, not a launch. A fast kart was
     * thrown off the top of the fairground's lift hill into the drop. A jump's lip (TF_RAMP before a gap) still throws */
    if (!(ts->flags & (TF_LOOP | TF_GAP)) && !s->battle && k->loc.br < 0 && sp > 15.0f) {
      const tsample *nx = &t->s[(k->loc.i + 6) % t->n];
      float ds = nx->s - ts->s;
      if (ds < 0) ds += t->length;
      bool lip = false;
      for (int g = 0; g < t->ngaps && !lip; g++) {
        float ahead = t->s[t->gap_from[g]].s - ts->s;
        if (t->circuit && ahead < 0) ahead += t->length;
        float span = t->s[t->gap_to[g]].s - t->s[t->gap_from[g]].s;
        lip = ahead > -span - 10.0f && ahead < 30.0f; /* the run-up and the whole flight (held down just past the lip, a kart
                                                      * at the jump's speed dropped into Frostbite's crevasse) */
      }
      if (ds > 1.0f && !lip && !grounded && !(nx->flags & TF_GAP)) { /* (only as it lifts: pressed down on every crest it was dragged, 6 s a lap on the refinery) */
        float kappa = -v3dot(v3sub(nx->f, ts->f), ts->u) / ds; /* > 0: the road bends down ahead */
        float need = sp * sp * kappa - 0.85f * GRAV;
        if (need > 0) cw_body3d_apply_force(b, v3mul(m.u, -fminf(need, 2.0f * GRAV) * mass * attach), NULL);
      }
    }
  }
  if (s->battle && (grounded || touching) && up.y > 0.7f) {
    /* an arena has no track gravity: the same downforce, along the kart's own down (ramps and decks); never while it leans on
     * a wall (rolled over against one, its own down is into the wall: it rode up it and over the top) */
    float press = STICK * clampf(absf(k->speed) / 14.0f, 0, 1) * GRAV * mass;
    cw_body3d_apply_force(b, v3mul(up, -press), NULL);
  }

  /* (a race's grace after a spin: the ticks left before the kart can be spun again) */
  if (!s->battle && k->ram_cool > 0) k->ram_cool--;
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
  /* loops and corkscrews: the road bends and twists under the kart faster than a driver can steer for it, so the wheels
   * steer themselves along the tube and back toward its middle, and the stick adds to that at half strength. (A kart
   * held straight ran sideways into the wall; the tyres own the yaw, so this has to be steering, not a push.) */
  if ((ts->flags & TF_LOOP) && touching && k->spin_ticks <= 0 && aspd > 2) {
    t3_vec3 fr = v3sub(m.f, v3mul(up, v3dot(m.f, up))), fk = v3sub(f, v3mul(up, v3dot(f, up)));
    float frl = v3len(fr), fkl = v3len(fk);
    if (frl > 0.3f && fkl > 0.3f) {
      fr = v3mul(fr, 1.0f / frl), fk = v3mul(fk, 1.0f / fkl);
      float err = cw_atan2(v3dot(v3cross(fk, fr), up), v3dot(fk, fr)); /* + : the road heads left of the nose */
      eff = clampf(-err * 3.0f - k->loc.lat * 0.22f + steer * 0.5f, -1.0f, 1.0f);
      k->drifting = false;
    }
  }
  /* gentle at speed without a drift, tight in one: the drift is how a sharp corner is taken */
  float rate = (2.35f - 1.35f * clampf(aspd / 40.0f, 0, 1)) * vd->turn * clampf(aspd / 3.0f + 0.15f, 0, 1);
  if (k->drifting) rate *= 1.34f;
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
    if (k->boost_ticks > 0 && !k->drifting) g *= 1.25f; /* a boost holds the road: more speed, not less control */
    car->def.wheels[w].grip = g;
  }
  if (ts->flags & TF_WATER) {
    k->v = v3mul(k->v, 1.0f);
    if (touching && k->speed > 8) cw_body3d_apply_force(b, v3mul(f, -k->speed * 0.55f * mass), NULL);
    if (touching && (s->tick & 7) == 0) event(s, EV_WATER, i, 0, k->p, clampf(k->speed / 40.0f, 0, 1));
  }
  /* deep sand, mud, loose snow: the speed bleeds away (a boost pushes through it) */
  if ((ts->flags & TF_SLOW) && touching && k->speed > 12 && k->boost_ticks <= 0) cw_body3d_apply_force(b, v3mul(f, -(k->speed - 12) * 0.5f * mass), NULL);
  drive(s, i, thr, st, true);
  /* a drift carries its speed round the arc: the travel turns toward the nose instead of the loose rear tyres
   * scrubbing it away (without this a drifting kart lost more than its mini-turbo gave back) */
  if (k->drifting && touching) {
    t3_vec3 v = cw_body3d_velocity(b);
    float vn = v3dot(v, up);
    t3_vec3 vp = v3sub(v, v3mul(up, vn)), fp = v3sub(f, v3mul(up, v3dot(f, up)));
    float sp2 = v3len(vp), fl = v3len(fp);
    if (sp2 > 4 && fl > 0.5f) {
      t3_vec3 nd = v3norm(v3add(v3mul(vp, 0.92f / sp2), v3mul(fp, 0.08f / fl)));
      cw_body3d_set_velocity(b, v3add(v3mul(nd, sp2), v3mul(up, vn)));
    }
  }
  /* stability, the arcade kind: out of a drift or a spin the kart never yaws faster than its steering asks, and a
   * sideways slide bleeds back into travel along the nose, so a bump, a kerb or a crooked landing is a wobble, not a
   * spin-out. For a moment after a landing it holds harder (land_grace) */
  if (touching && !k->drifting && k->spin_ticks <= 0 && !(ts->flags & TF_LOOP)) {
    t3_vec3 v = cw_body3d_velocity(b);
    float vn = v3dot(v, up);
    t3_vec3 vp = v3sub(v, v3mul(up, vn)), fp = v3sub(f, v3mul(up, v3dot(f, up)));
    float sp2 = v3len(vp), fl = v3len(fp);
    if (sp2 > 3 && fl > 0.5f) {
      fp = v3mul(fp, 1.0f / fl);
      float along = v3dot(vp, fp);
      if (along > 0) {
        float pull = k->land_grace > 0 ? 0.35f : k->boost_ticks > 0 ? 0.15f : 0.06f;
        t3_vec3 lat = v3sub(vp, v3mul(fp, along));
        /* the slide turns into travel along the nose at the same speed: a crooked landing costs direction, not pace */
        t3_vec3 nvp = v3add(v3mul(fp, along), v3mul(lat, 1 - pull));
        float nl = v3len(nvp);
        if (nl > 1e-3f) nvp = v3mul(nvp, sp2 / nl);
        cw_body3d_set_velocity(b, v3add(nvp, v3mul(up, vn)));
      }
    }
    float asked = rate * absf(eff), wy = v3dot(k->w, up), cap = k->land_grace > 0 ? asked + 0.1f : asked * 1.2f + rate * 0.2f + 0.2f;
    if (k->boost_ticks > 0) cap = minf(cap, asked * 1.05f + 0.1f); /* boosting, the nose goes only where it is steered */
    if (absf(wy) > cap) {
      k->w = v3add(k->w, v3mul(up, (wy > 0 ? cap : -cap) - wy));
      cw_body3d_set_angular_velocity(b, k->w);
    }
  }
  /* a drift begun under a boost keeps its arc but never whips round */
  if (touching && k->drifting && k->boost_ticks > 0 && k->spin_ticks <= 0 && !(ts->flags & TF_LOOP)) {
    float cap = rate * 1.1f + 0.2f, wy = v3dot(k->w, up);
    if (absf(wy) > cap) {
      k->w = v3add(k->w, v3mul(up, (wy > 0 ? cap : -cap) - wy));
      cw_body3d_set_angular_velocity(b, k->w);
    }
  }
  if (k->land_grace > 0) k->land_grace--;

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

  /* never on its side or its roof near the road (outside loops): a kart tipped past about 30 degrees from the road's up
   * is turned back upright, hard, so a bump or a wall is a wobble and not a roll into the barrier */
  if (!(ts->flags & TF_LOOP) && m.u.y > 0.8f && k->loc.height < 3.0f && k->spin_ticks <= 0 && k->trick_ticks <= 0) {
    float c = v3dot(up, m.u);
    if (c < 0.87f) {
      t3_vec3 ax = v3cross(up, m.u);
      float sa = v3len(ax);
      if (sa > 1e-4f) {
        t3_vec3 axn = v3mul(ax, 1.0f / sa);
        float ang = cw_atan2(sa, c), want = ang * 7.0f, have = v3dot(k->w, axn);
        k->w = v3add(k->w, v3mul(axn, (want - have) * 0.4f));
        cw_body3d_set_angular_velocity(b, k->w);
      }
    }
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
        /* and the yaw nobody asks for dies away: the pull to the path alone swung the nose from side to side over a long
         * flight (the Dam jump: 20 degrees off at the landing, 50 m/s sideways into the bounce and over the wall) */
        float wy = v3dot(w, up);
        w = v3sub(w, v3mul(up, wy * 2.4f * DT * (1 - absf(steer))));
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
    k->item = s->battle ? roll_battle_item(s, i) : roll_item(s, k->place, item_gap_m(s, i), k->lap >= s->laps);
    k->item_count = k->item == IT_TURBO3 || k->item == IT_MISSILE3 ? 3 : 1;
  }
  if (k->item_cool > 0) k->item_cool--;
  {
    /* oil and mines can be held out behind while the button is held, as a shield against a missile from behind; letting go
     * drops it */
    bool holdable = k->item == IT_OIL || k->item == IT_MINE;
    if (pressed_item && holdable && !k->trailing) {
      k->trailing = true;
      if (getenv("RACER_FALLLOG")) cw_info("trailing kart %d item %d tick %d", i, k->item, s->tick);
    }
    else if (pressed_item) use_item(s, i);
    /* assault: nothing to use, the blaster instead (held, it fires again as soon as it can) */
    if (s->battle && s->amode == AM_ASSAULT && k->item == IT_NONE && k->item_wait == 0 && (in.btn & KB_ITEM) && k->blast_cool == 0 &&
        k->spin_ticks <= 0 && s->phase == PH_RACE)
      battle_bolt(s, i);
    if (k->blast_cool > 0) k->blast_cool--;
    if (k->trailing && (!holdable || !(in.btn & KB_ITEM))) {
      k->trailing = false;
      if (holdable) use_item(s, i);
    }
  }

  if (k->boost_ticks > 0) k->boost_ticks--;
  if (k->shield_ticks > 0) k->shield_ticks--;
  if (k->respawn_ticks > 0) k->respawn_ticks--;
  if (s->battle) battle_handling(s, i, thr, steer, grounded); /* battle only: a quicker getaway from a crawl */
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
  float ds = k->loc.ms - PH.prev_s[i];
  if (t->circuit) {
    if (ds > L * 0.5f) ds -= L;
    if (ds < -L * 0.5f) ds += L;
  }
  if (absf(ds) < 40.0f) k->prog += ds; /* a jump can move it forty metres in a tick; more is a re-locate */
  PH.prev_s[i] = k->loc.ms;
  /* a shortcut counts when the kart comes off a branch back onto the main road at its far end */
  if (k->loc.br >= 0 && !(t->s[k->loc.i].flags & TF_JOIN)) k->cut_br = k->loc.br + 1;
  else if (k->loc.br < 0 && k->cut_br) {
    const track_branch *B = &t->br[k->cut_br - 1];
    float d = k->loc.ms - B->s0; /* how far along the stretch it stands for the kart came back */
    if (t->circuit) d = d < -L * 0.5f ? d + L : d > L * 0.5f ? d - L : d;
    if (d > B->span * 0.6f && d < B->span + 30.0f) k->cuts++;
    k->cut_br = 0;
  }
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
  if (k->grounded && k->loc.br < 0 && k->loc.on_road && absf(k->loc.lat) < t->s[k->loc.i].hw - 0.5f && !(t->s[k->loc.i].flags & (TF_GAP | TF_RAMP))) {
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

/* a kart is never a ramp: one riding up onto another (a spun-out kart sideways in the lane) loses the climb every tick it
 * is over it, not only on the tick the contact began. A fast kart that met a stopped one climbed it nose up and took off
 * 8 m, over the Dam's outer wall before its tunnel. The road's up decides "up" (a kart already tipping has its own
 * tilted) */
static void no_kart_ramps(sim *s) {
  for (int a = 0; a < MAXK; a++) {
    kart *ka = &s->k[a];
    if (!ka->on || ka->respawn_ticks > 0) continue;
    t3_vec3 up = (s->battle || ka->loc.i < 0) ? (t3_vec3){ 0, 1, 0 } : s->track->s[ka->loc.i].u;
    for (int b = 0; b < MAXK; b++) {
      const kart *kb = &s->k[b];
      if (b == a || !kb->on || kb->respawn_ticks > 0) continue;
      t3_vec3 d = v3sub(ka->p, kb->p);
      float h = v3dot(d, up);
      t3_vec3 flat = v3sub(d, v3mul(up, h));
      if (h < 0.15f || h > 2.5f || v3dot(flat, flat) > 2.6f * 2.6f) continue;
      /* a is on top of b: no rising off it, and its nose-up (or roll) spin from the climb taken out */
      t3_vec3 v = cw_body3d_velocity(PH.body[a]);
      float vu = v3dot(v, up);
      if (vu > 1.0f) cw_body3d_set_velocity(PH.body[a], v3sub(v, v3mul(up, vu - 1.0f)));
      t3_vec3 w = cw_body3d_angular_velocity(PH.body[a]);
      float wy = v3dot(w, up);
      cw_body3d_set_angular_velocity(PH.body[a], v3add(v3mul(v3sub(w, v3mul(up, wy)), 0.5f), v3mul(up, wy)));
      break;
    }
  }
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
      /* on an unwalled edge (a rope bridge) a bump only nudges: a pass there shoved karts off the temple's bridge into the
       * gorge. Driving off it is still the driver's own doing */
      for (int q = 0; q < 2; q++) {
        const kart *kq = &s->k[q ? b : a];
        if (!s->battle && kq->loc.i >= 0 && (s->track->s[kq->loc.i].flags & TF_OPEN)) shove *= 0.25f;
      }
      cw_body3d_apply_impulse(PH.body[b], v3mul(n, shove * ma * (ma / (ma + mb))), NULL);
      cw_body3d_apply_impulse(PH.body[a], v3mul(n, -shove * mb * (mb / (ma + mb))), NULL);
      /* and the contact itself never throws a kart up off the road, nor sets it tumbling end over end (two karts
       * meeting at the start launched one nose up into the wall) */
      for (int q = 0; q < 2; q++) {
        int ki = q ? b : a;
        t3_vec3 uq = kart_up(&s->k[ki]), v = cw_body3d_velocity(PH.body[ki]), w = cw_body3d_angular_velocity(PH.body[ki]);
        float vu = v3dot(v, uq);
        if (vu > 1.0f) cw_body3d_set_velocity(PH.body[ki], v3sub(v, v3mul(uq, vu - 1.0f)));
        float wy = v3dot(w, uq);
        cw_body3d_set_angular_velocity(PH.body[ki], v3add(v3mul(v3sub(w, v3mul(uq, wy)), 0.3f), v3mul(uq, wy)));
      }
      if (h->speed > 4) event(s, EV_BUMP, a, b, h->point, clampf(h->speed / 20, 0, 1));
      if (s->battle) battle_ram(s, a, b, n, h->speed);
    } else if (a >= 0 || b >= 0) {
      int ki = a >= 0 ? a : b;
      kart *k = &s->k[ki];
      /* a wall takes some of the speed, kills the spin it would give and bounces the kart a little back onto the road;
       * the push is across the road only, so a wall never lifts a kart up and over it */
      t3_vec3 up0 = kart_up(k);
      t3_vec3 n = a >= 0 ? h->normal : v3mul(h->normal, -1); /* kart to wall */
      n = v3sub(n, v3mul(up0, v3dot(n, up0)));
      float nl = v3len(n);
      n = nl > 0.2f ? v3mul(n, 1.0f / nl) : n;
      t3_vec3 v = cw_body3d_velocity(PH.body[ki]);
      float vn = v3dot(v, n);
      if (vn > 0 && nl > 0.2f) v = v3sub(v, v3mul(n, vn * 1.0f + clampf(vn * 0.35f, 0, 4.0f)));
      float vu = v3dot(v, up0);
      if (vu > 1.0f) v = v3sub(v, v3mul(up0, vu - 1.0f));
      /* the speed lost goes with how square the hit is: a graze along the wall costs next to nothing, a head-on
       * thump up to 15% (a flat 7% a tick of contact made a scrape feel like being grabbed) */
      v = v3mul(v, 1.0f - clampf(fmaxf(vn, 0.0f) * 0.02f, 0.0f, 0.15f));
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
    if (s->battle) {
      MIX(k->balloons);
      MIX(k->points);
      MIX(k->carry);
      MIX(k->hill_ticks);
    }
  }
  if (s->battle) {
    MIX(s->battle_left);
    MIX(s->team_score[0]);
    MIX(s->team_score[1]);
    MIX(s->hill_holder);
    MIX(s->hill_i);
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
    if (s->battle) {
      battle_phase(s);
      break;
    }
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
    if (!k->on || k->parked) continue;
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
    if (!k->on || k->parked) continue;
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
      /* and the flight bends gently toward the middle of the landing: a crooked take-off or a bump at the lip lands
       * on the far side, not in the gorge beside it */
      tsample gm = track_at_loc(s->track, &k->loc, 0);
      float vlat = v3dot(k->v, gm.r), want = clampf(-k->loc.lat * 1.4f, -9.0f, 9.0f);
      k->v = v3add(k->v, v3mul(gm.r, (want - vlat) * 0.08f));
      /* and up toward the arc the landing is built for when a kart near the jump's speed is falling below it (one that left
       * the ramp a moment early at 33 m/s on a 31 m/s jump dropped into Frostbite's crevasse) */
      float along = v3dot(k->v, gm.f);
      for (int g = 0; g < s->track->ngaps; g++) {
        if (k->loc.i < s->track->gap_from[g] || k->loc.i > s->track->gap_to[g]) continue;
        if (along > 0.85f * s->track->gap_v[g] && k->loc.height < -0.3f) {
          float vn = v3dot(k->v, gm.u), want_n = clampf(-k->loc.height * 2.0f, 0.0f, 6.0f);
          if (vn < want_n) k->v = v3add(k->v, v3mul(gm.u, (want_n - vn) * 0.1f));
        }
        break;
      }
      cw_body3d_set_velocity(PH.body[i], k->v);
    }
    if (k->loc.i >= 0 && k->loc.br < 0 && !(s->track->s[k->loc.i].flags & (TF_LOOP | TF_GAP)) && !k->grounded && k->air_ticks > 10 &&
        k->loc.height > 0.3f && k->loc.height < 10.0f) {
      /* a long flight over the road (off a crest at speed, down a slope): it drifts no further out over an edge, and past
       * three quarters of the road's half width it bends gently back, as a jump's flight does toward its landing. A boosted
       * kart off the top of the temple's descent drifted 6 m out in the air, clipped the wall top and tumbled off */
      tsample am = track_at_loc(s->track, &k->loc, 0);
      float edge = am.hw * 0.75f, lat = k->loc.lat;
      if (absf(lat) > edge) {
        float vlat = v3dot(k->v, am.r), want = clampf(-(lat - (lat > 0 ? edge : -edge)) * 1.2f, -6.0f, 6.0f);
        if (vlat * lat > 0 || absf(vlat) < absf(want)) {
          k->v = v3add(k->v, v3mul(am.r, (want - vlat) * 0.08f));
          cw_body3d_set_velocity(PH.body[i], k->v);
        }
      }
    }
    if (k->loc.i >= 0 && (s->track->s[k->loc.i].flags & TF_LOOP) && !k->grounded && k->loc.height < 8.0f) {
      /* off the deck in a loop or corkscrew (the road rolled away faster than the kart followed): it stops drifting
       * away from the road and bends back toward the tube's middle, as a jump's flight does toward its landing */
      tsample lm = track_at_loc(s->track, &k->loc, 0);
      float vn = v3dot(k->v, lm.u);
      if (vn > 0) k->v = v3sub(k->v, v3mul(lm.u, vn * 0.25f));
      float vlat = v3dot(k->v, lm.r), want = clampf(-k->loc.lat * 1.6f, -8.0f, 8.0f);
      k->v = v3add(k->v, v3mul(lm.r, (want - vlat) * 0.1f));
      cw_body3d_set_velocity(PH.body[i], k->v);
    }
    bool loop_near = false; /* in a loop, or one coming within 40 m */
    if (k->loc.i >= 0 && k->loc.br < 0)
      for (int j = 0; j <= 40 && !loop_near; j += 4) loop_near = (s->track->s[(k->loc.i + j) % s->track->n].flags & TF_LOOP) != 0;
    else if (k->loc.i >= 0) loop_near = (s->track->s[k->loc.i].flags & TF_LOOP) != 0;
    if (loop_near) {
      static float capv = -1;
      if (capv < 0) capv = getenv("RACER_LOOPCAP") ? (float)atof(getenv("RACER_LOOPCAP")) : 45.0f;
      /* eased on the approach, never snapped: a kart above it sheds at most 0.45 m/s a tick over the last 40 m before the
       * loop. (A hard clamp at 40 inside took 10-15 m/s off a turboed kart in one tick at the mouth of every loop: an
       * invisible wall) */
      if (sp > capv) {
        float to = maxf(capv, sp - 0.45f);
        k->v = v3mul(k->v, to / sp);
        cw_body3d_set_velocity(PH.body[i], k->v);
      }
    }
    k->top_speed_seen = maxf(k->top_speed_seen, sp);
    if (s->battle) {
      battle_kart_after(s, i);
      continue;
    }
    progress(s, i);
    /* falling off, flipped on its roof, or reset by hand */
    const track *t = s->track;
    const tsample *ts = &t->s[k->loc.i];
    bool lost = k->p.y < PH.min_y - 40.0f || (k->loc.dist > ts->hw * 2.5f + 14.0f) ||
                (k->loc.height < -9.0f && !(ts->flags & TF_GAP)) ||
                (k->loc.br >= 0 && k->loc.height < -4.5f && !(ts->flags & TF_GAP)); /* off a raised branch: gone before it reaches the ground */
    bool flipped = k->grounded == false && k->loc.on_road && v3dot(kart_up(k), ts->u) < -0.35f && !(ts->flags & TF_LOOP) && k->loc.height < 1.3f;
    if (s->phase == PH_RACE && !k->finished && k->respawn_ticks <= 0 && k->spin_ticks <= 0 && absf(k->speed) < 2.5f) k->stall_ticks++;
    else k->stall_ticks = 0;
    if (flipped) k->stuck_ticks++;
    else if (k->stuck_ticks > 0) k->stuck_ticks--;
    if (k->respawn_ticks <= 0 && (lost || k->stuck_ticks > 150 || k->stall_ticks > 210 || k->reset_hold > 55) && !k->finished) {
      if (lost) event(s, EV_FALL, i, 0, k->p, 0);
      if (getenv("RACER_FALLLOG")) cw_info("respawn kart %d tick %d br=%d s=%.0f spd=%.0f lost=%d (dist %.1f hw %.1f height %.1f) stuck=%d stall=%d reset=%d p=(%.0f %.0f %.0f)", i, s->tick, k->loc.br, k->loc.s, k->speed, lost, k->loc.dist, ts->hw, k->loc.height, k->stuck_ticks, k->stall_ticks, k->reset_hold, k->p.x, k->p.y, k->p.z);
      respawn(s, i);
    }
  }
  contacts(s);
  no_kart_ramps(s);
  if (s->battle) battle_unscrum(s);
  boxes_step(s);
  hazards_step(s);
  if (s->battle) battle_rank(s);
  else rank(s);
  hash_state(s);
}

/* dev aid (world.c's geometry check): the collision walls as built */
const track_phys *sim_wall_phys(void) { return &PH.walls; }
