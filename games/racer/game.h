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
#define NARENAS 6  /* arena maps (ARENAS[]; separate from the race maps): the battle arenas, then the capture the flag maps */
#define BALLOONS 3 /* a battle kart's balloons at the start */
#define BATTLE_SPIN_K 0.45f /* a battle spin lasts this share of a race spin (the pop is the cost; sim_battle.inc, kartgfx.c) */

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

enum { IT_NONE, IT_TURBO, IT_TURBO3, IT_MISSILE, IT_HOMING, IT_OIL, IT_MINE, IT_SHIELD, IT_SHOCK, IT_MISSILE3, IT_BOOMERANG, IT_BOMB, IT_KINDS };
extern const char *const ITEM_NAME[IT_KINDS];

/* something in the race that is not a kart: a missile in flight, an oil slick, a mine */
/* HZ_BOOMERANG flies out and comes back to its thrower (two hits at most); HZ_BOMB is lobbed, lands, and goes off a second
 * later with a wide blast (sim.c bomb_blast) */
enum { HZ_NONE, HZ_MISSILE, HZ_HOMING, HZ_OIL, HZ_MINE, HZ_SHOCK, HZ_BOOMERANG, HZ_BOMB, HZ_KINDS };
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
  int bounces;    /* battle: walls a missile has glanced off */
  bool owner_clear; /* the kart that dropped it has been 8 m away: from then on it can hit its owner too */
  int state;      /* a boomerang: 0 going out, 1 coming back; a bomb: 0 in the air, 1 down and fizzing */
  float dist;     /* a boomerang: metres it has flown out */
  float vy;       /* a bomb in the air: its climb (m/s, up the road's up in a race, +y in an arena) */
  int hitmask;    /* a boomerang: the karts it has hit (one hit each), and how many */
  int hits;
} hazard;
#define MAXHZ 48

typedef struct itembox {
  float s, lat;
  t3_vec3 p;
  int cool;       /* ticks until it is back (0 = there) */
} itembox;
#define MAXBOX 160

typedef struct map_def map_def;

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
  int land_grace;       /* ticks of firmer stability after a landing (sim.c) */
  int hop_tick;         /* the tick of the last hop (the drift button): a missile met just after it misses */
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
  int shock_warn, shock_by;   /* a Shockwave is coming: ticks to go, and whose; a hop over it dodges it */
  int draft_ticks, draft_cool;
  int pass_cool;        /* ticks before another close pass can pay */
  bool trailing;        /* an oil can or a mine held out behind (the item button held): it stops a missile from behind */ /* time spent in the slipstream of a kart ahead; a pause after the boost it pays */
  bool trick_done;
  int bump_ticks;
  int respawn_ticks;    /* flickering back in */
  int stuck_ticks;
  int stall_ticks;      /* ticks it has barely moved (a respawn when it is long) */
  int launch;           /* the start: 0 nothing yet, 1 a good launch (throttle in the last half second), -1 flooded (too early) */
  int start_stall;      /* ticks the engine coughs after a flooded start */
  int reset_hold;
  /* items */
  int item;
  int item_count;       /* charges of a triple turbo, and so on */
  int item_wait;        /* ticks the box is spinning before the item shows */
  int item_cool;        /* ticks before another item can be used */
  uint8_t last_btn;
  /* stats */
  int hits_given, hits_taken, boosts, tricks, falls, drift_boosts;
  int cuts;             /* shortcuts taken: branches driven from the mouth back onto the main road */
  int cut_br;           /* 1 + the branch it was last on (0 none), for counting */
  float top_speed_seen;
  /* battle (sim_battle.inc): balloons left, points (a balloon popped), out of the round and when */
  int balloons, points, pops_taken;
  bool out, parked;     /* out: no balloons left; parked: its last spin over, moved off the arena (no longer drawn) */
  int out_tick;
  int ram_cool;         /* ticks before a turbo ram can pop this kart again */
  int scrum_ticks;      /* battle: ticks it has been crawling pressed against another kart (sim_battle.inc pulls them apart) */
  int8_t last_thr;      /* the throttle asked this tick (a bomb is thrown back while the brake is held) */
  /* capture the flag and king of the mountain (sim_battle.inc) */
  int team;             /* capture the flag: 0 red, 1 blue */
  int carry;            /* capture the flag: the team whose flag it carries, -1 none */
  int caps, steals, returns; /* flags captured, flags taken, its own team's flag sent home */
  int flag_cool;        /* ticks before it can take a flag again (it just dropped one) */
  int hill_ticks;       /* king of the mountain: ticks it has held the hill alone */
  int tele_cool;        /* an arena's teleport pads: ticks before one takes this kart again */
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
  EV_WATER,        /* a = kart in water, k = speed */
  EV_NEARMISS,     /* a = kart that shaved past b at speed (a little boost) */
  EV_WARN,         /* a = kart a Shockwave is coming for (hop to dodge it), b = attacker */
  EV_POP,          /* battle: a = victim lost a balloon, b = attacker (or -1 / itself), k = balloons left */
  EV_OUT,          /* battle: a = victim is out of the round, b = attacker */
  EV_RAM,          /* battle: a = rammed b under a boost (the pop follows as EV_POP) */
  EV_FLAG,         /* capture the flag: a = kart (-1: none), b = FE_*, k = the flag's team */
  EV_HILL,         /* king of the mountain: a = kart (-1: none), b = HE_* */
  EV_TELE          /* an arena's teleport pad: a = kart, b = the pad, at = where it left (it is at the far end now) */
};
enum { FE_TAKEN, FE_DROPPED, FE_RETURNED, FE_CAPTURED, FE_HOME /* left alone long enough, it went home by itself */ };
enum { HE_SOON /* it moves in five seconds */, HE_MOVED, HE_TAKEN /* a = the kart holding it alone now */, HE_CONTESTED };

/* what an arena round is played for (sim.amode): balloons (battle), capture the flag, king of the mountain */
enum { AM_BALLOON, AM_CTF, AM_KOTM, AM_KINDS };
#define CTF_WIN 3        /* captures that win a capture the flag round */
#define HILL_R 9.0f      /* king of the mountain: the hill's radius */
enum { FL_HOME, FL_CARRIED, FL_DROPPED };

/* battle: why the round ended (sim.battle_end); BE_SCORE: a capture the flag team reached CTF_WIN */
enum { BE_NONE, BE_LAST, BE_TIME, BE_HUMANS, BE_SCORE };
typedef struct arena_rt arena_rt;

typedef struct sim {
  int tick;
  uint32_t rng;
  int phase, phase_ticks;
  int map;                 /* the map's index in MAPS (-1: the proving ground) */
  const map_def *md;
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
  int speed_class;         /* 0 Standard, 1 Fast, 2 Blazing (sim_set_class) */
  int item_set;            /* 0 every item, 1 turbos only (a party preset) */
  float class_top, class_acc; /* every kart's top speed and acceleration scale for the class (1 = Standard) */
  /* battle (an arena map): no laps, balloons and points, a clock (sim_battle.inc) */
  bool battle;
  const arena_rt *arena;   /* the arena's world-space layout (arena.c) */
  int battle_limit;        /* ticks the round lasts */
  int battle_left;         /* ticks to go */
  int battle_end;          /* BE_* */
  int alive;               /* karts still in */
  int humans_out_tick;
  int amode;               /* AM_*: what the arena round is played for */
  /* capture the flag: the score, each team's flag and base */
  int team_score[2];
  int win_team;            /* at the end: the team that won, -1 a draw */
  struct ctf_flag {
    int state;             /* FL_* */
    int carrier;           /* the kart carrying it (FL_CARRIED) */
    int timer;             /* FL_DROPPED: ticks until it goes home by itself */
    t3_vec3 p;             /* where it is (home: on its base) */
  } flag[2];
  t3_vec3 base[2];
  /* king of the mountain: where the hill is and who holds it */
  t3_vec3 hill;
  int hill_i;              /* the arena's hill spot it stands on */
  int hill_left;           /* ticks until it moves */
  int hill_holder;         /* the kart holding it alone, -1 nobody, -2 contested */
  uint32_t hash;
  int ev_n;
  struct sim_event { int kind, a, b; t3_vec3 at; float k; } ev[96];
} sim;

/* sim.c */
void sim_init(sim *s, const track *t, const map_def *map, int map_index, int laps, const bool on[MAXK], const int veh[MAXK], uint32_t seed);
void sim_free(sim *s);
/* the speed class (race_setup): Standard, Fast or Blazing scale every kart's top speed and acceleration. Call it after
 * sim_init; sim_restart keeps it. The tables are in sim.c. */
void sim_set_class(sim *s, int cls);
/* start the same race over without rebuilding the world: every kart back on the grid, the clock, hazards, boxes and
 * the random stream as sim_init left them (the time trial's instant retry). The karts, map and class stay. */
void sim_restart(sim *s, uint32_t seed, int skip_ticks); /* skip_ticks > 0 joins the countdown that far in (60: the "2" and the "1" only) */
void sim_step(sim *s, const kin in[MAXK]);
void sim_put(sim *s, int i, float s_dist, float lat, float speed);
void sim_put_branch(sim *s, int i, int b, float bs, float speed); /* dev aid: on branch b, bs metres from its mouth */
void sim_use(sim *s, int i, int item); /* dev aid: kart i uses this item now */
void sim_battle_put(sim *s, int i, float x, float y, float z, float yaw_deg, float speed); /* dev aid: kart i at (x, z) of the arena's own frame, on what is under height y */
/* battle: the round's length in ticks (call after sim_init; 0 keeps the default three minutes) */
void sim_set_battle_time(sim *s, int ticks);
/* an arena round's rules (AM_*): call after sim_init once the karts' human flags are set (capture the flag puts the humans
 * in teams by player: one or two players on red, three or four 1 and 3 red, 2 and 4 blue; the CPUs even the teams up) */
void sim_set_arena_mode(sim *s, int mode);
/* capture the flag: a and b are on opposite teams (always true outside capture the flag) */
static inline bool kart_foe(const sim *s, int a, int b) { return s->amode != AM_CTF || s->k[a].team != s->k[b].team; }
/* a ray against the static world (floor, walls, cover; never the karts): true on a hit, with how far along (0..1) and the normal.
 * A query only: it changes nothing (the CPU drivers' feelers and line of sight) */
bool sim_ray(const sim *s, t3_vec3 from, t3_vec3 delta, float *frac, t3_vec3 *normal);
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
  /* battle (ai_battle.c) */
  int b_target, b_box, b_think, b_reverse, b_hold, b_slow, b_side, b_frust, b_roam;
  float b_best_d;
  int b_commit;       /* ticks it drives on after backing out before it may back out again */
  t3_vec3 b_rev_p;    /* where the backing out began */
  t3_vec3 b_aim;      /* where it steered this tick (the trace) */
  float b_feel[3];    /* its feelers this tick, left, centre, right (the trace) */
  int b_thr;          /* the throttle it chose this tick (the trace) */
  t3_vec3 b_goal, b_last_p;
  int8_t take[4];     /* each branch (shortcut) on the way: 0 not decided yet, 1 taking it, -1 staying on the main road */
  int b_role;         /* capture the flag: 0 attacks the other flag, 1 stays back and guards its own */
  int b_node, b_node_t; /* an arena with a route (arena_def.route): the point it is heading for (-1 none), ticks to the next look */
} ai_mem;
void ai_reset(ai_mem *m, int me, int skill, uint32_t seed);
kin ai_think(const sim *s, int me, ai_mem *m);
/* ai_battle.c: the CPU driver of a battle (roams to boxes, chases and fires, backs off walls) */
kin ai_battle_think(const sim *s, int me, ai_mem *m);

/* assist.c: the humans' steering assist (main.c human_in only; never the CPU drivers) */
void assist_steer(const sim *s, int me, kin *in);

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

struct map_def {
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
  const struct arena_def *arena;     /* a battle arena (arena.h): its floor, walls, cover, spawns and boxes; NULL for a race map */
  int music;                         /* the music track (audio_music) a battle arena plays; 0: the default */
};
extern const map_def *const MAPS[NMAPS];
extern const map_def *const ARENAS[NARENAS]; /* the battle arenas (maps.c) */
extern const map_def map_test; /* the proving ground: RACER_MAP=-1 */

#endif
