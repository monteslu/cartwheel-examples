/* derby: shared state between the bout simulation (sim.c), the CPU drivers
 * (ai.c), the 3D world (world.c, models.c, fx.c, scenery.c, textures.c) and
 * the app (main.c).
 *
 * The simulation is the engine's Box3D world (cartwheel/physics3d.h): each
 * car a dynamic body on cw_vehicle3d wheels (cartwheel/vehicle3d.h), the
 * arena static geometry. It reads one quantised input per car per tick and
 * nothing else, and the engine's physics is deterministic on every host and
 * with any number of step workers, so every host replays a bout bit for bit.
 * The 3D side only reads it.
 *
 * World: metres, y up. A car's heading h points it along (sin h, cos h) in
 * x/z (its nose projected on the floor); its side vector is (cos h, -sin h). */
#ifndef DERBY_GAME_H
#define DERBY_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/vehicle3d.h"
#include "three.h"

/* the display name lives here only (the game's name is still to be decided) */
#define GAME_TITLE "DERBY"

/* ── the arena: a rounded rectangle of concrete and tyres ───────────── */

#define AW 21.0f   /* half width (x), to the inside face of the wall */
#define AL 30.0f   /* half length (z) */
#define AC 9.0f    /* corner radius seen from above */

#define MAXC 8     /* cars in a bout */
#define TICK_HZ 60

enum { CAR_SEDAN, CAR_WAGON, CAR_PICKUP, CAR_VAN, CAR_KINDS };

/* one driver's input for one tick, quantised so every host agrees */
typedef struct cin {
  int8_t thr;    /* -127 (brake / reverse) .. 127 (throttle) */
  int8_t steer;  /* -127 left .. 127 right */
  uint8_t btn;   /* CB_* */
} cin;
enum { CB_HANDBRAKE = 1, CB_HORN = 2 };

#define MAX_DENTS 20
typedef struct dent {
  float f, s;    /* where, in the car's frame: metres forward, metres to the side */
  float y;       /* height on the body, metres above the floor */
  float depth;   /* metres pushed in */
} dent;

enum { ZONE_FRONT, ZONE_REAR, ZONE_LEFT, ZONE_RIGHT, ZONES };

typedef struct car {
  bool on;              /* in this bout */
  int kind;
  /* the body after the last step: planar summary (heading, ground speed,
   * yaw rate) for the rules, AI and camera; the full pose for drawing */
  float x, z, h;        /* centre on the floor and heading */
  float vx, vz, w;      /* m/s and rad/s (about y) */
  float y;              /* the body origin's height (0 resting on its wheels) */
  t3_quat q;            /* the body's rotation */
  float up_y;           /* the body's up . world up: 1 upright, < 0 on its roof */
  float steer;          /* the front wheels' angle, radians (+ left) */
  float hp;             /* 0..100: the engine; 0 is a wreck */
  float zone[ZONES];    /* 0..1 panel damage per side (looks, bumpers) */
  bool wrecked;
  int wreck_ticks;      /* since it died */
  int last_hit_by;      /* the last car that hurt it (-1) */
  int last_hit_tick;
  float rpm;            /* 0..1, for the engine note */
  float slip_f, slip_r; /* sideways skid speed at each axle, m/s (the worse wheel) */
  float ax, az;         /* last tick's acceleration, car frame (bumpers, bonnet) */
  int stuck_ticks;      /* on its roof or side, or out of the ring, for this long */
  int rights;           /* self-right kicks since it was last on its wheels */
  int flips;            /* times it went over (count.flips) */
  int ndent;
  dent dents[MAX_DENTS];
  uint8_t last_btn;
  bool horn;
  /* the bout's stats */
  float dealt, taken;
  int wrecks;           /* cars it finished */
  int points;           /* the points-for-hits ruleset */
  int respawn;          /* ticks until it is back (points ruleset) */
  /* physics */
  cw_body3d body;
  cw_vehicle3d veh;     /* wheels 0 front left, 1 front right, 2 rear left, 3 rear right (left is +x) */
} car;

enum { PH_COUNTDOWN, PH_GO, PH_OVER };
enum { RULE_LAST_RUNNING, RULE_POINTS, RULE_KINDS };

typedef struct sim {
  int tick;
  uint32_t rng;
  int rule;
  int phase, phase_ticks;
  int clock;            /* ticks of bout time left */
  int ncars;
  car c[MAXC];
  int winner;           /* -1 none yet */
  int alive;
  int crashes, wall_hits, wrecks;
  int flips;
  float max_hit;
  uint32_t hash;
  /* the arena and the cars in Box3D */
  cw_world3d world;
  cw_body3d floor, wall, fence;
  /* this tick's events (the 3D side and the audio read them) */
  int ev_n;
  struct sim_event { int kind, a, b; t3_vec3 at; float k; } ev[64];
} sim;

enum {
  EV_CRASH,     /* a and b collided: k = impulse (tonne m/s), at = contact */
  EV_LAND,      /* a's body hit the floor (a roll, a landing): k = impulse */
  EV_RIGHT,     /* a kicked itself back onto its wheels */
  EV_SCRAPE,    /* a sliding along b (b = -1: the wall): k = grind speed */
  EV_WALL,      /* a hit the wall: k = impulse */
  EV_DENT,      /* a took a dent: k = depth */
  EV_WRECK,     /* a is done for: b = who did it (-1: the wall) */
  EV_BEEP, EV_GO, EV_OVER, EV_HORN,
  EV_RESPAWN    /* a is back (points ruleset) */
};

/* car shapes: half length, half width, mass (tonnes), the body's height, the
 * axles (metres forward of the middle; the rear is negative), the wheel radius;
 * for the collision hull the rocker's underside, the waist line at the ends
 * and the middle, the greenhouse (roof z front and back, its foot z front
 * and back) and the centre of mass's height */
typedef struct car_spec {
  float hl, hw, mass, height, ride, af, ar, wr;
  float sill, belt_end, belt_mid, roof_f, roof_b, cowl, deck, com_y;
  const char *name;
} car_spec;
extern const car_spec CAR_SPEC[CAR_KINDS];

/* sim.c */
void sim_init(sim *s, const bool on[MAXC], const int kind[MAXC], int rule, int bout_secs, uint32_t seed);
void sim_step(sim *s, const cin in[MAXC]);
void sim_free(sim *s);                                    /* the Box3D world */
void sim_place(sim *s, int i, float x, float z, float h); /* puts car i on its wheels there, still */
void sim_wheel(const car *c, int w, t3_vec3 *pos, t3_quat *rot); /* wheel w in the car's frame (this tick) */
float arena_sd(float x, float z, float *nx, float *nz); /* signed distance to the wall (+ outside), outward normal */
/* the wall's line: the rounded rectangle walked a point every `step` metres,
 * `inset` metres out from the inner face; tangent (tx, tz) runs round it */
typedef struct arena_pt { float x, z, tx, tz; } arena_pt;
int arena_line(arena_pt *out, int max, float step, float inset);
/* the jersey barrier's cross section: (out from the inner face, up), inner foot first */
#define JERSEY_N 10
extern const float JERSEY[JERSEY_N][2];
float car_speed(const car *c);       /* along its heading, m/s (negative reversing) */
void car_local(const car *c, float wx, float wz, float *f, float *sd); /* world point -> car frame */
int sim_alive(const sim *s);

/* ai.c: CPU drivers (0 rookie, 1 pro, 2 ace); all their state is here */
typedef struct ai_mem {
  int target, retarget;  /* who it is after; ticks until it thinks again */
  int stuck, reverse;    /* stuck-ticks counter; ticks of backing out */
  int react;             /* reaction lag */
  float aim_side;        /* -1..1: which flank it goes for */
  uint32_t rng;
} ai_mem;
void ai_reset(ai_mem *m, int me, uint32_t seed);
cin ai_think(const sim *s, int me, int level, ai_mem *m);

#endif
