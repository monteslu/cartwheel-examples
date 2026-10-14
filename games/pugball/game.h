/* pugball: shared state between the match simulation (sim.c), the CPU
 * players (ai.c), the 3D world (world.c, models.c, fx.c, scenery.c,
 * textures.c) and the app (main.c).
 *
 * The simulation runs on Box3D (cartwheel's world3d) and the game's own
 * arithmetic is + - * / and sqrtf with cartwheel/math.h for everything
 * transcendental; it reads one quantised input per player per tick and
 * nothing else, so every host replays a match bit for bit. The 3D side only
 * reads it.
 *
 * World: metres, y up. The pitch runs along z: team 0 (the Biscuits, blue)
 * defends the goal at -z and attacks +z; team 1 (the Sausages, red) the
 * other way. */
#ifndef PUGBALL_GAME_H
#define PUGBALL_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "three.h"

/* ── the arena ──────────────────────────────────────────────────────── */

#define AW 22.0f    /* half width (x) */
#define AL 30.0f    /* half length (z), the goal lines */
#define AH 14.0f    /* ceiling */
#define AR 4.0f     /* the ramp: floor / wall / ceiling fillet radius */
#define AC 8.0f     /* the corner radius, seen from above */
#define GW 6.0f     /* goal mouth half width */
#define GH 5.0f     /* goal mouth height */
#define GD 5.0f     /* goal depth behind the line */

#define RB 1.3f     /* ball radius (a giant chew ball) */
#define RIDE 0.36f  /* a pug's centre above the surface it runs on */
#define PHX 0.42f   /* pug hitbox half extents: across, up, along */
#define PHY 0.30f
#define PHZ 0.62f

#define MAXP 6      /* pugs on the pitch (3 a side) */
#define TICK_HZ 60
#define MAX_PADS_BOOST 34
#define DEMO_TICKS 180  /* a demolished pug is out this long */
#define WRECK_TICKS 130 /* its wreck tumbles this long before it pops away */

enum { TEAM_BLUE, TEAM_RED };

/* one player's input for one tick, quantised so every host agrees */
typedef struct pin {
  int8_t thr;    /* throttle -127..127 (triggers / W S) */
  int8_t steer;  /* left stick x: steer on the ground, yaw in the air */
  int8_t pitch;  /* left stick y: pitch in the air, dodge direction */
  uint8_t btn;   /* PB_* */
} pin;
enum { PB_JUMP = 1, PB_BOOST = 2, PB_SLIDE = 4 };

typedef struct pug {
  bool on;              /* in this match */
  int team;
  t3_vec3 p, v;         /* centre; m/s */
  t3_quat q;            /* body orientation; forward is local +z, up +y */
  t3_vec3 w;            /* angular velocity, world rad/s */
  float boost;          /* 0..100 */
  bool grounded;
  t3_vec3 n;            /* the surface under it when grounded */
  int air_ticks;        /* since it left the ground */
  int jump_ticks;       /* since the first jump (-1: none this flight) */
  bool jump_held;       /* the first jump's hold window is open */
  bool second;          /* the double jump / dodge is still there */
  int dodge_ticks;      /* a dodge in progress (the flip spin) */
  t3_vec3 dodge_axis;
  int dead;             /* demolished: ticks until it is back (0 = alive) */
  bool boosting, sliding, supersonic;
  int touch_cool;       /* ticks before the ball takes another hit bonus from it */
  uint8_t last_btn;
  /* match stats */
  int goals, assists, saves, shots, demos, touches, points;
} pug;

typedef struct boost_pad { float x, z; bool big; int cool; } boost_pad;

enum { PH_KICKOFF, PH_PLAY, PH_GOAL, PH_OVER };

typedef struct sim {
  int tick;
  uint32_t rng;
  int phase, phase_ticks;
  int clock;            /* ticks of match time left (counts up in overtime) */
  int match_ticks;      /* the match length */
  bool overtime;
  int score[2];
  int nplayers;
  pug p[MAXP];
  t3_vec3 ball, ball_v;
  t3_vec3 ball_spin;    /* the ball's angular velocity (axis * rad/s) */
  bool ball_low;        /* within 0.3 m of the arena */
  bool ball_rest;       /* within 0.1 m of it */
  int last_touch, prev_touch, last_touch_tick; /* player indices (-1 none) */
  int kickoff_style;
  int scorer, assister, goal_team; /* the last goal */
  int winner;           /* -1 tie / undecided */
  boost_pad pads[MAX_PADS_BOOST];
  int npads;
  int hits, bounces, demos, jumps, dodges, pads_taken; /* counters (gates) */
  float max_ball_speed;
  uint32_t hash;
  /* events this tick (the 3D side and the audio read them) */
  int ev_n;
  struct sim_event { int kind, a, b; t3_vec3 at; float k; } ev[64];
} sim;

enum {
  EV_HIT,      /* a pug touched the ball: a = pug, k = strength 0..1 */
  EV_BOUNCE,   /* the ball hit the arena: k = speed into the surface */
  EV_JUMP, EV_DOUBLE, EV_DODGE, EV_LAND,
  EV_PAD,      /* a = pug, b = big */
  EV_BUMP,     /* a hit b, no demolition */
  EV_DEMO,     /* a demolished b */
  EV_RESPAWN,  /* a is back */
  EV_GOAL,     /* a = scoring team, b = scorer */
  EV_KICKOFF, EV_GO, EV_OVERTIME, EV_FINAL, EV_SUPERSONIC
};

/* sim.c */
void sim_init(sim *s, const bool on[MAXP], int match_secs, uint32_t seed);
void sim_step(sim *s, const pin in[MAXP]);
/* test stages: move a pug (at rest, facing yaw) or the ball (at rest) */
void sim_place_pug(sim *s, int i, float x, float z, float yaw);
void sim_place_ball(sim *s, t3_vec3 at);
/* the live match's arena: true when the segment from..to crosses no arena
 * surface and a sphere of radius r at `to` touches none (render-time
 * cameras; queries only, never during a step) */
bool arena_clear(t3_vec3 from, t3_vec3 to, float r);
t3_vec3 pug_fwd(const pug *u);
t3_vec3 pug_up(const pug *u);
t3_vec3 pug_right(const pug *u);
float v3len(t3_vec3 a);
int team_size(const sim *s, int team);

/* arena.c: the arena as one static Box3D mesh body in world w */
#include "cartwheel/physics3d.h"
cw_body3d arena_create(cw_world3d w, const cw_shape3d_def *sd);

static inline t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static inline t3_vec3 v3add(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline t3_vec3 v3sub(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline t3_vec3 v3mul(t3_vec3 a, float k) { return (t3_vec3){ a.x * k, a.y * k, a.z * k }; }
static inline float v3dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline t3_vec3 v3cross(t3_vec3 a, t3_vec3 b) {
  return (t3_vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
t3_vec3 v3norm(t3_vec3 a);

/* ai.c: CPU players (0 rookie, 1 pro, 2 all-star); all their state is here */
typedef struct ai_mem {
  int role;            /* 0 attack, 1 support, 2 defend (this tick's) */
  int react;           /* ticks of reaction lag left before it re-plans */
  t3_vec3 target;
  int jump_hold, dodge_wait;
  int stuck, unstick;  /* ticks pushing without moving; ticks left backing out */
  uint32_t rng;
} ai_mem;
void ai_reset(ai_mem *m, int me, uint32_t seed);
pin ai_think(const sim *s, int me, int level, ai_mem *m);

#endif
