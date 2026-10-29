/* otium: Roman trade across Italy, against two rival houses, on cartwheel and
 * three.c (three.js r186). Time only moves while you act: every order costs
 * days, and the rivals act during them. Shared state between the files. */
#ifndef OTIUM_GAME_H
#define OTIUM_GAME_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/audio.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/platform.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"
#include "three.h"

#define OTIUM_TITLE "OTIUM"

/* ── the map (map.c): Italy, its cities, roads and sea lanes ─────────── */

enum { GD_BRICK, GD_FOOD, GD_TOOLS, GD_WINE, GD_CLOTH, GOODS };
enum { GOD_JUPITER, GOD_SATURN, GOD_MERCURY, GOD_MARS, GOD_VESTA, GODS };

#define MAX_CITIES 24
#define MAX_EDGES 64
#define MAX_REGIONS 8
#define CAPITAL 0 /* Roma: where every house starts; it makes nothing */

typedef struct city_def {
  const char *name;
  float lon, lat; /* degrees: map_xz turns them into the board's x, z */
  int region;     /* -1: the capital */
  int good;       /* -1: the capital */
  bool port;
} city_def;
typedef struct edge_def { int a, b, days; bool sea; } edge_def;
typedef struct region_def { const char *name; int bonus; /* the good a harvest caller gets extra */ } region_def;

extern const city_def CITIES[];
extern const int N_CITIES;
extern const region_def REGIONS[];
extern const int N_REGIONS;
extern edge_def EDGES[MAX_EDGES];
extern int N_EDGES;
void map_init(void); /* the edges' days from their lengths (once) */

/* the board: x east, z south, in metres of table; 1 degree of longitude = 1 m */
static inline float map_x(float lon) { return (lon - 13.0f) * 1.0f; }
static inline float map_z(float lat) { return -(lat - 41.5f) * 1.3f; }
#define MAP_LON0 6.4f
#define MAP_LON1 19.6f
#define MAP_LAT0 36.3f
#define MAP_LAT1 46.6f

/* land outlines (lon, lat pairs), for the board's texture and relief */
typedef struct outline { const float *pts; int n; } outline;
extern const outline LANDS[];
extern const int N_LANDS;
bool map_on_land(float lon, float lat);

/* ── the simulation (sim.c): fixed ticks, integers and cartwheel/math.h ─ */

#define NP 3            /* you and two rival houses */
#define MAX_UNITS 6
#define MAX_PATH 24
#define DAY 10          /* ticks per day (6 days a second while time runs) */
#define SEASON_DAYS 30
#define SEASONS 6
#define END_TICK (SEASONS * SEASON_DAYS * DAY)
#define OFFERS 3        /* the temple's offers on show */
#define TRADE_LOTS 3    /* goods one market visit may buy or sell */

enum { O_MOVE, O_BUILD, O_HARVEST, O_TRADE, O_RECRUIT, O_TEMPLE, O_REST, ORDERS, O_NONE = -1 };
/* the days each order takes (a move takes its road's days) */
extern const int ORDER_DAYS[ORDERS];

typedef struct unit {
  int city;               /* where it stands (or set out from) */
  int path[MAX_PATH], path_n; /* cities from city to the target, moving */
  int t0, t1;             /* the move's start and end ticks */
} unit;

typedef struct offer { int god, cost[GOODS], coins; } offer;

typedef struct player {
  int coins, goods[GOODS], favor[GODS];
  unit units[MAX_UNITS];
  int n_units;
  uint32_t spent;         /* bit per order used since the last rest (O_MOVE never) */
  int free_at;            /* tick its current order ends */
  int order, order_city, order_unit; /* the order running (O_NONE: idle) */
  int houses;
  int last_trade[GOODS];  /* the last market visit's net lots (for the presentation) */
} player;

typedef struct order_req { /* what a player asks for */
  int order, city, unit;   /* city: the cursor's; unit: for a move (-1: the nearest idle) */
  int trade[GOODS];        /* O_TRADE: + buy, - sell */
  int offer;               /* O_TEMPLE: which offer */
} order_req;

typedef struct sim {
  int tick;
  player p[NP];
  uint8_t house[MAX_CITIES]; /* bit per player */
  uint8_t fallow[MAX_REGIONS]; /* harvested this season */
  int price[GOODS];
  offer offers[OFFERS];
  uint32_t rng;
  bool over;
  /* events for the presentation (sounds, effects), cleared each tick */
  int ev_build[NP], ev_harvest[NP], ev_trade[NP], ev_recruit[NP], ev_temple[NP], ev_rest[NP], ev_arrive[NP], ev_season, ev_end;
  int ev_build_city[NP], ev_harvest_region[NP];
  /* stats */
  int orders_done[NP];
} sim;

extern const int BASE_PRICE[GOODS];

void sim_start(sim *S, uint32_t seed);
/* advance one tick (the world is running); returns false when the game is over */
bool sim_tick(sim *S);
void sim_clear_events(sim *S);
/* can p give this order now? (its cost in *days, the reason it cannot in *why: 0 ok) */
enum { WHY_OK, WHY_SPENT, WHY_POOR, WHY_NO_UNIT, WHY_NOTHING, WHY_FALLOW, WHY_FULL, WHY_BUSY };
int sim_check(const sim *S, int p, const order_req *r, int *days);
/* give it: costs paid now, the player busy until its days pass; false if it cannot */
bool sim_order(sim *S, int p, const order_req *r);
bool sim_idle(const sim *S, int p); /* free to act at this tick */
void sim_wait(sim *S, int p, int days); /* nothing to do: idle for days */
int sim_score(const sim *S, int p, int god); /* the category's count for that god */
int sim_total(const sim *S, int p);           /* every god's favor times its count */
int sim_house_cost(const sim *S, int p, int city, int goods[GOODS]); /* coins, goods in goods */
bool sim_can_build(const sim *S, int p, int city);
int sim_path(int from, int to, int path[MAX_PATH], int *n); /* days, -1 none */
int sim_nearest_unit(const sim *S, int p, int city);  /* the unit closest (by days) not already there, -1 */
bool sim_unit_at(const sim *S, int p, int city);
int sim_buildable(const sim *S, int p, int list[MAX_UNITS]); /* cities a build would build in, most valuable first */
void sim_trade_cost(const sim *S, const int trade[GOODS], int *coins_delta);
int sim_season(const sim *S);
uint32_t sim_hash(const sim *S);
float sim_unit_pos(const sim *S, int p, int u, float tick_f, float *x, float *z, bool *at_sea, float *heading);

/* ── the rival houses (ai.c) ──────────────────────────────────────────── */

enum { PERS_BUILDER, PERS_MERCHANT, PERS_GENERAL, PERSONALITIES };
order_req ai_choose(const sim *S, int p, int personality);

/* ── presentation (world.c): three.c objects, render-only ─────────────── */

#define PLAYER_RGB(p) (PLAYER_COLOR[p])
extern const float PLAYER_COLOR[NP][3];
extern const float GOOD_COLOR[GOODS][3];
extern const float GOD_COLOR[GODS][3];

void world_build(cw_node scene);
void world_free(void);
/* per frame: units, houses, cursor, path preview. tick_f: the sim tick with the frame's fraction */
typedef struct world_view {
  int cursor, sel_unit;      /* the city under the cursor, the unit a move would send (-1) */
  const int *preview_path;   /* the path a move would take (NULL) */
  int preview_n;
  bool title;
} world_view;
void world_sync(const sim *S, float tick_f, float t_seconds, const world_view *v);
t3_vec3 world_city_pos(int city); /* on the board's surface */
float world_height(float x, float z);
extern uint32_t world_hash, tex_hash;

/* textures.c */
t3_color lin(float r, float g, float b);
t3_texture *tex_board(uint32_t *hash);   /* the map: parchment, sea, coasts, regions */
t3_texture *tex_wood(uint32_t *hash);    /* the table */
t3_texture *tex_stone(uint32_t *hash);   /* plinths */
t3_texture *tex_normal_board(void);      /* the parchment's fibres */
float board_coast_dist(float lon, float lat); /* degrees to the coast: + on land */
float board_height(float lon, float lat, float *mountain); /* the relief (m), and how mountainous (0..1) */

#endif
