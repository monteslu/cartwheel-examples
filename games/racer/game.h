/* racer: shared state between the race simulation (sim.c), the CPU drivers
 * (ai.c), the 3D world (world.c, models.c, fx.c, scenery in maps/) and the app
 * (main.c).
 *
 * The simulation is the engine's Box3D world (cartwheel/physics3d.h): each
 * kart a dynamic body on cw_vehicle3d wheels (cartwheel/vehicle3d.h), the track
 * static meshes built from the path in track.c. It reads one quantised input
 * per kart per tick and nothing else; the 3D side only reads it. Items live in
 * track coordinates (distance along, metres to the right, height), not as
 * bodies, so they are cheap and exact.
 *
 * World: metres, y up. A kart's forward is its local +z, up +y, right -x. */
#ifndef RACER_GAME_H
#define RACER_GAME_H

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/physics3d.h"
#include "cartwheel/scene.h"
#include "cartwheel/vehicle3d.h"
#include "three.h"
#include "track.h"

/* the working title (the game's name is still to be decided) */
#define GAME_TITLE "RACER"

#define TICK_HZ 60
#define GRAV 20.0f /* must equal TRACK_G in track.c */
#define MAXK 8     /* karts in a race */
#define MAXV 4     /* human players / split screen views */
#define NMAPS 8
#define NVEH 6
#define MAX_LAPS 5

/* ── vehicles ───────────────────────────────────────────────────────── */

typedef struct vehicle_def {
  const char *name;
  const char *blurb;
  float top;       /* m/s without a boost */
  float accel;     /* m/s^2 from rest */
  float turn;      /* steering, 1 = the reference */
  float grip;      /* tyre grip, 1 = the reference */
  float mass;      /* kg */
  float drift;     /* how fast a drift charges, 1 = the reference */
  float boost;     /* boost strength, 1 = the reference */
  float len, wid;  /* body half extents the shape and the models agree on */
  float wheel_r;
} vehicle_def;
extern const vehicle_def VEH[NVEH];

/* ── input ──────────────────────────────────────────────────────────── */

typedef struct kin {
  int8_t thr;    /* -127 (brake / reverse) .. 127 */
  int8_t steer;  /* -127 left .. 127 right */
  int8_t pitch;  /* air control: -127 nose down .. 127 nose up */
  uint8_t btn;   /* KB_* */
} kin;
enum { KB_DRIFT = 1, KB_ITEM = 2, KB_TRICK = 4, KB_RESET = 8, KB_BACK = 16 /* look back */ };

/* ── items ──────────────────────────────────────────────────────────── */

enum { IT_NONE, IT_TURBO, IT_TURBO3, IT_MISSILE, IT_HOMING, IT_OIL, IT_MINE, IT_SHIELD, IT_SHOCK, IT_KINDS };
extern const char *const ITEM_NAME[IT_KINDS];

/* something in the race that is not a kart: a missile in flight, an oil slick, a mine */
enum { HZ_NONE, HZ_MISSILE, HZ_HOMING, HZ_OIL, HZ_MINE, HZ_SHOCK, HZ_KINDS };
typedef struct hazard {
  int kind;
  int owner;      /* the kart that set it down (immune for a moment) */
  int target;     /* a homing missile's mark */
  float s, lat, h; /* track coordinates */
  float speed;    /* m/s along the track (missiles) */
  int life;       /* ticks left */
  int age;
  t3_vec3 p;      /* world position, set each tick */
  t3_vec3 f;      /* world heading (missiles) */
} hazard;
#define MAXHZ 48

typedef struct itembox {
  float s, lat;
  t3_vec3 p;
  int cool;       /* ticks until it is back (0 = there) */
} itembox;
#define MAXBOX 160

/* ── karts ──────────────────────────────────────────────────────────── */

enum { SPIN_NONE, SPIN_OIL, SPIN_HIT, SPIN_BOOM, SPIN_SHOCK };

typedef struct kart {
  bool on;
  bool human;
  int veh;
  int colour;
  t3_vec3 p, v;
  t3_quat q;
  t3_vec3 w;
  float speed;          /* m/s along its forward */
  float steer;          /* front wheel angle, radians (+ left) */
  bool grounded;
  int air_ticks;
  float air_peak;       /* highest above the road this flight (a trick's worth) */
  /* the track */
  int hint;
  track_loc loc;
  int lap;              /* 0 before the line, laps done once past it */
  int next_check;
  float prog;           /* lap * length + s: the order of the race */
  int place;            /* 1 = leading */
  bool finished;
  int finish_tick;
  float best_lap;       /* seconds */
  int lap_start_tick;
  float last_safe_s;    /* a place to put it back after a fall */
  int safe_hold;
  /* handling */
  bool drifting;
  int drift_dir;        /* +1 left, -1 right */
  int drift_ticks;
  int drift_level;      /* 0 none, 1 blue, 2 orange, 3 purple */
  int boost_ticks;      /* boost left */
  float boost_k;        /* strength of the boost running */
  int spin_ticks;       /* out of control: spun by oil, a hit, a mine, a shock */
  int spin_kind;
  float spin_dir;
  int shield_ticks;
  int trick_ticks;      /* a trick in the air */
  bool trick_done;
  int bump_ticks;
  int respawn_ticks;    /* flickering back in */
  int stuck_ticks;
  int reset_hold;
  /* items */
  int item;
  int item_count;       /* charges of a triple turbo, and so on */
  int item_wait;        /* ticks the box is spinning before the item shows */
  int item_cool;        /* ticks before another item can be used */
  uint8_t last_btn;
  /* stats */
  int hits_given, hits_taken, boosts, tricks, falls, drift_boosts;
  float top_speed_seen;
} kart;

/* ── the race ───────────────────────────────────────────────────────── */

enum { PH_GRID, PH_COUNT, PH_RACE, PH_DONE };

enum {
  EV_GO,           /* the lights went out */
  EV_COUNT,        /* a = 3,2,1 */
  EV_LAP,          /* a = kart, b = lap now on */
  EV_FINISH,       /* a = kart, b = place */
  EV_BOOST,        /* a = kart, b = source (0 pad, 1 item, 2 drift, 3 start, 4 trick) */
  EV_DRIFT_LEVEL,  /* a = kart, b = level */
  EV_LAND,         /* a = kart, k = how hard 0..1 */
  EV_TRICK,        /* a = kart */
  EV_BUMP,         /* a hit b (karts), k = strength */
  EV_WALL,         /* a = kart hit a wall, k = strength */
  EV_ITEM_GET,     /* a = kart, b = item */
  EV_ITEM_USE,     /* a = kart, b = item */
  EV_HIT,          /* a = victim, b = attacker, k = hazard kind */
  EV_SPIN,         /* a = kart */
  EV_FALL,         /* a = kart fell off */
  EV_RESPAWN,      /* a = kart back */
  EV_SHIELD,       /* a = kart absorbed a hit */
  EV_BOX,          /* a = kart took a box */
  EV_OVERTAKE,     /* a = kart passed b */
  EV_WATER         /* a = kart in water, k = speed */
};

typedef struct sim {
  int tick;
  uint32_t rng;
  int phase, phase_ticks;
  int map;
  int laps;
  int nkarts;
  kart k[MAXK];
  const track *track;
  hazard hz[MAXHZ];
  itembox box[MAXBOX];
  int nbox;
  int order[MAXK];         /* kart indices, first place first */
  int finished_count;
  int race_ticks;          /* ticks since the lights went out */
  uint32_t hash;
  int ev_n;
  struct sim_event { int kind, a, b; t3_vec3 at; float k; } ev[96];
} sim;

/* sim.c */
void sim_init(sim *s, const track *t, int map, int laps, const bool on[MAXK], const int veh[MAXK], uint32_t seed);
void sim_free(sim *s);
void sim_step(sim *s, const kin in[MAXK]);
void sim_place_on_grid(sim *s); /* line the karts up behind the line */
const track *sim_track(const sim *s);
const cw_vehicle3d *sim_car(const sim *s, int i); /* the wheels' poses for the renderer */
float v3len(t3_vec3 a);
t3_vec3 v3norm(t3_vec3 a);
t3_vec3 kart_fwd(const kart *k);
t3_vec3 kart_up(const kart *k);
t3_vec3 kart_right(const kart *k);

static inline t3_vec3 v3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static inline t3_vec3 v3add(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline t3_vec3 v3sub(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline t3_vec3 v3mul(t3_vec3 a, float k) { return (t3_vec3){ a.x * k, a.y * k, a.z * k }; }
static inline float v3dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline t3_vec3 v3cross(t3_vec3 a, t3_vec3 b) {
  return (t3_vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

/* ai.c: CPU drivers; all their state is here */
typedef struct ai_mem {
  int skill;          /* 0 rookie, 1 pro, 2 ace */
  uint32_t rng;
  float lane;         /* the lateral line it is holding (m) */
  float lane_want;
  int lane_ticks;
  float weave_phase;
  int item_wait;
  int stuck, unstick;
  float steer_smooth;
} ai_mem;
void ai_reset(ai_mem *m, int me, int skill, uint32_t seed);
kin ai_think(const sim *s, int me, ai_mem *m);

/* ── maps ───────────────────────────────────────────────────────────── */

typedef struct map_look {
  float road[3];        /* road base tint (linear multiplier of the texture) */
  float kerb_a[3], kerb_b[3];
  float wall[3];
  float wall_top[3];
  int road_tex;         /* textures.h TEX_* for the road */
  int wall_tex;
  float road_rough;
  bool wall_emissive;   /* lit strips along the walls (neon) */
  float glow[3];
} map_look;

typedef struct map_def {
  const char *name;
  const char *blurb;
  const char *scene;                 /* assets/scenes/<x>.cwscene: sky, sun, fog, post */
  const track_spec *spec;
  track_mesh_opts mesh;
  map_look look;
  int laps;                          /* default laps */
  float par_lap;                     /* seconds of a decent lap (AI and HUD) */
  /* called once the scene and the track exist: builds the world around the road */
  void (*scenery)(cw_node scene, const track *t, uint32_t seed);
  /* called each fixed tick and each frame for animated scenery (water, flames) */
  void (*scenery_tick)(const sim *s, int ticks);
  void (*scenery_frame)(float alpha, double time);
} map_def;
extern const map_def *const MAPS[NMAPS];

#endif
