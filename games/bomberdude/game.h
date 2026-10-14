/* bomberdude: shared state between the grid simulation (sim.c), the CPU
 * players (ai.c), the 3D world (world.c, models.c, fx.c, scenery.c,
 * textures.c) and the app (main.c).
 *
 * The simulation is integer only: positions in SUB units per tile, timers in
 * fixed ticks, one seeded random stream. It reads one input word per player
 * per tick and nothing else, so every host replays it bit for bit. The 3D
 * side only reads it. */
#ifndef BOMBERDUDE_GAME_H
#define BOMBERDUDE_GAME_H

#define IABS(x) ((x) < 0 ? -(x) : (x))

#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/audio.h"
#include "cartwheel/input.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"

/* ── the grid ───────────────────────────────────────────────────────── */

#define GW 15          /* columns */
#define GH 13          /* rows */
#define SUB 256        /* simulation units per tile */
#define MAXP 4         /* players */
#define MAXB 48        /* bombs live at once */
#define MAX_TELE 4     /* teleporter pairs */
#define ARENAS 8

enum { C_EMPTY, C_WALL, C_CRATE };                       /* blocks */
enum {                                                   /* floor features */
  F_PLAIN, F_ICE, F_VENT, F_TELE, F_BELT_E, F_BELT_W, F_BELT_N, F_BELT_S, F_SAND, F_CANNON_ROW, F_CANNON_COL, F_KINDS
};
enum { D_NONE = -1, D_E = 0, D_N = 1, D_W = 2, D_S = 3 }; /* +x, -y (up the screen), -x, +y */

/* power-ups (the eighth is this game's own: a bubble that takes one blast) */
enum { PU_NONE, PU_BOMB, PU_FIRE, PU_SPEED, PU_KICK, PU_REMOTE, PU_PIERCE, PU_BUBBLE, PU_KINDS };

/* themes / twists, one per arena */
enum { TH_MEADOW, TH_FROST, TH_FORGE, TH_TEMPLE, TH_CANDY, TH_NEON, TH_PIRATE, TH_DESERT, TH_KINDS };

/* input word bits, one per player per tick */
enum { IN_LEFT = 1, IN_RIGHT = 2, IN_UP = 4, IN_DOWN = 8, IN_BOMB = 16, IN_ACT = 32 };

typedef struct bomb {
  bool live;
  int owner;          /* player index */
  int cx, cy;         /* tile (for a sliding bomb: the tile it is in) */
  int x, y;           /* sub-tile position (slides) */
  int fuse;           /* ticks to go; a remote bomb waits (fuse < 0) */
  int range;
  bool pierce, remote;
  int slide_dir;      /* D_NONE when still */
  uint8_t pass;       /* bit p: player p may still walk off it (was on it when it was dropped) */
  int age;
  int id;             /* drop order, for visuals and the remote's oldest-first */
  bool belt;          /* riding a belt (slow, stops on each centre) */
} bomb;

typedef struct player {
  bool on;            /* in this round */
  bool alive;
  int x, y;           /* sub units, tile centres at k*SUB + SUB/2 */
  int dir;            /* facing */
  int moving;         /* ticks moved in a row (0 = standing) */
  int bombs, range, speed;  /* capacity, blast length, sub units per tick */
  bool kick, remote, pierce;
  int bubble;         /* shields left */
  int invuln;         /* ticks of grace after a bubble pops */
  int ko_tick;        /* sim tick when knocked out */
  int ko_by;          /* whose blast (-1: the arena) */
  int slide_dir;      /* on ice: the direction it keeps sliding */
  int tele_cool;      /* ticks before a teleporter takes it again */
  int placed;         /* bombs dropped this round */
  int kos;            /* knockouts dealt this round */
  int picked;         /* power-ups picked this round */
  uint8_t last_in;
  int bomb_buf;       /* ticks a bomb press keeps trying */
  bool pref_vert;     /* with two directions held, try the vertical one first (it was pressed last) */
} player;

typedef struct sim {
  int tick;
  int theme;
  uint32_t rng;
  uint8_t block[GH][GW];
  uint8_t floor[GH][GW];
  uint8_t floor_dir[GH][GW];   /* belts: direction; cannons: which side fires */
  uint8_t crate_hp[GH][GW];    /* candy: two-hit jelly crates */
  uint8_t item[GH][GW];        /* power-up lying here (PU_*) */
  int16_t fire[GH][GW];        /* ticks of blast left */
  uint8_t fire_shape[GH][GW];  /* bit 0 centre, 1 horizontal, 2 vertical, 4.. end caps */
  int8_t fire_owner[GH][GW];
  int16_t burn[GH][GW];        /* a crate in flames: ticks until it is gone */
  int16_t item_grace[GH][GW];  /* an item a blast just revealed survives that blast */
  int16_t vent[GH][GW];        /* forge vents / cannon rows: phase timer */
  int tele_x[MAX_TELE * 2], tele_y[MAX_TELE * 2], tele_n;
  bomb bombs[MAXB];
  int bomb_ids;
  player p[MAXP];
  int nplayers;
  int round_ticks;             /* length of the round before sudden death */
  int sd_tick, sd_index;       /* sudden death: walls drop along a spiral */
  int sd_order_x[GW * GH], sd_order_y[GW * GH], sd_count;
  int sd_drop_x, sd_drop_y, sd_drop_t; /* the wall falling now (visual) */
  int over_tick;               /* tick the round was decided (0 = playing) */
  int winner;                  /* -1 draw, -2 undecided */
  int storm;                   /* desert: ticks of sandstorm left */
  int cannon_t;                /* pirate: cannon cycle */
  int chains;                  /* bombs set off by another's blast */
  int belt_moves, slides;      /* twist counters (gates) */
  uint32_t hash;
  /* the CPUs' and the danger preview's view of the state, rebuilt by
   * sim_refresh after every change (sim_load, sim_step, sim_force_bomb):
   * computed once a tick, read by every CPU and the world */
  int8_t bomb_at[GH][GW];      /* the lowest live bomb index on each tile, -1 none (= sim_bomb_at) */
  int16_t danger[GH][GW];      /* ticks until fire reaches each tile (0 = burning, 999 = safe) */
  /* events this tick (the 3D side and the audio read them) */
  int ev_n;
  struct sim_event { int kind, x, y, a, b; } ev[64];
} sim;

enum {
  EV_DROP, EV_BOOM, EV_CRATE, EV_PICK, EV_KO, EV_BUBBLE, EV_KICK, EV_BUMP, EV_TELE, EV_VENT_WARN, EV_VENT,
  EV_CANNON_WARN, EV_CANNON, EV_SD_WALL, EV_ITEM_BURN, EV_STEP
};

/* the arena an Arena node describes (assets/scenes/arena*.cwscene) */
typedef struct arena_info {
  int index;           /* 1..8; 0 = the title backdrop */
  const char *title;
  const char *blurb;   /* one line on the twist */
  int theme;
  const char *layout;  /* GH rows of GW chars: # wall, + crate (always), . open, ? random crate,
                          s spawn, i ice, v vent, t teleporter, > < ^ v belts (b..: see make-arenas),
                          ~ sand, c / C cannon rows / columns */
  int crate_pct;       /* chance a '?' becomes a crate */
  int seed;
  int cpu;             /* adventure: CPU rivals */
  int cpu_level;       /* adventure: 0 easy .. 2 hard */
  int round_secs;
} arena_info;

/* sim.c */
void sim_load(sim *s, const arena_info *a, const bool on[MAXP], uint32_t seed);
void sim_step(sim *s, const uint8_t in[MAXP]);
int sim_alive(const sim *s);
bool sim_passable(const sim *s, int cx, int cy, int who); /* for a player (bombs block) */
bool sim_tile_open(const sim *s, int cx, int cy);         /* no wall, crate or bomb */
int sim_bomb_at(const sim *s, int cx, int cy);            /* index or -1 */
void sim_refresh(sim *s); /* rebuilds bomb_at and danger: after any change to the state outside sim.c (a dev aid) */
uint32_t sim_rand(sim *s);
void sim_force_bomb(sim *s, int cx, int cy, int range); /* dev aid */
static inline int tile_of(int v) { return v / SUB; }
static inline int centre_of(int t) { return t * SUB + SUB / 2; }

/* ai.c: CPU players (0 easy, 1 normal, 2 hard); all their state is here */
typedef struct ai_mem {
  int next_think;
  uint8_t held;        /* the input it keeps until it thinks again */
  int goal_x, goal_y;  /* -1: none */
  int mode;            /* what it is doing (debug) */
  uint32_t rng;
} ai_mem;
void ai_reset(ai_mem *m, int me, uint32_t seed);
uint8_t ai_think(const sim *s, int me, int level, ai_mem *m);

#endif
