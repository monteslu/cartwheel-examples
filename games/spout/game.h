/* spout: a papercraft spider climbs a rain spout (cartwheel + three.c r186).
 * Shared state between the files of the game. */
#ifndef SPOUT_GAME_H
#define SPOUT_GAME_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/platform.h"
#include "cartwheel/audio.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"
#include "three.h"

#define SPOUT_TITLE "SPOUT" /* the display name: one place to rename the game */

/* ── the level: one spout, a vertical pipe of radius R from y = 0 to height ── */

#define PIPE_R 0.55f
#define SP_LEVELS 5
#define MAX_CHECKS 8
#define MAX_PATCHES 12
#define MAX_STREAMS 4
#define MAX_DEWS 24
#define MAX_LEAVES 6

typedef struct patch { float th0, th1, y0, y1; bool dries; } patch; /* paper wrap: slick while wet */
typedef struct level_def {
  const char *name, *blurb;
  float height;
  int theme;
  int n_checks;
  float checks[MAX_CHECKS]; /* bracket heights (checkpoints); checks[0] = 0 (the ground) */
  int n_patches;
  patch patches[MAX_PATCHES];
  int n_streams;
  float streams[MAX_STREAMS]; /* angle of each rain stream (they wander a little) */
  int n_dews;
  float dew_th[MAX_DEWS], dew_y[MAX_DEWS];
  int sun_ticks, warn_ticks, rain_ticks; /* the weather cycle */
  int wave_every;                        /* ticks between waves down a stream while it rains */
  int leaf_every;                        /* ticks between leaves (0: none) */
  int twist;                             /* TWIST_*: what this spout adds */
} level_def;

enum { TWIST_NONE, TWIST_WIND, TWIST_LEAVES, TWIST_FREEZE, TWIST_NODES };

enum { THEME_SPRING, THEME_CITY, THEME_AUTUMN, THEME_WINTER, THEME_JUNGLE, THEME_COUNT };
extern const level_def LEVELS[SP_LEVELS];
extern const int LEVELS_BUILT; /* levels playable in this build (the rest are padlocked "soon") */

/* ── the simulation (sim.c): fixed ticks, cartwheel/math.h only ─────── */

enum { SP_CLING, SP_JUMP, SP_WASHED, SP_WIN, SP_ZIP };
#define GUST_EVERY 420
#define GUST_WARN 90
#define GUST_LEN 120

enum { WX_SUN, WX_WARN, WX_RAIN, WX_CLEAR };

typedef struct spider {
  float th, y;   /* angle round the pipe (radians, wraps) and height (m) */
  float r;       /* lift off the surface while jumping (m) */
  float vth, vy; /* jump velocity: radians/s and m/s */
  float vr;
  int state;
  float slide;   /* metres still to slide when washed down */
  float face;    /* heading on the surface: radians, 0 = up */
  float walk;    /* distance walked: drives the legs */
  bool brace;
  int stun;      /* ticks knocked about */
} spider;

typedef struct wave { float y; bool live; int stream; } wave;
typedef struct leaf { float th, y, spin; bool live; } leaf;

typedef struct sim_input { float x, y; bool jump, brace, silk; } sim_input;

#define MAX_FLIES 3
#define MAX_SPOOLS 2
#define SILK_MAX 5

typedef struct sim {
  const level_def *L;
  spider s;
  int tick;
  int weather, weather_ticks; /* phase and ticks into it */
  float wet;                  /* 0 dry .. 1 soaked: paper wraps slick while > 0.5 */
  int checkpoint;             /* highest bracket reached */
  wave waves[8];
  leaf leaves[MAX_LEAVES];
  uint32_t rng;
  uint32_t dew_got;           /* bit per dewdrop collected */
  int dews;
  int washes, knocks, jumps, braced_waves;
  float stream_th[MAX_STREAMS];
  int win_tick;
  int gust_ticks, gust_dir; /* TWIST_WIND: ticks into the gust cycle, the push direction */
  /* silk: zip up a line to the next collar or bracket above, swinging round */
  int silk;                 /* charges left */
  float zip_y, zip_swing;   /* the anchor height; swing speed round the pipe */
  int zips;
  /* the paper bird: comes, marks a spot near you, pecks it, goes */
  int bird_ticks;           /* into its visit (0 = away) */
  float bird_th, bird_y;    /* where it will peck */
  int pecks;
  /* collectibles */
  float fly_th[MAX_FLIES], fly_y[MAX_FLIES], spool_th[MAX_SPOOLS], spool_y[MAX_SPOOLS];
  uint32_t fly_got, spool_got;
  int flies;
  /* events for the presentation (sounds, effects), cleared each tick */
  int ev_jump, ev_land, ev_wash, ev_knock, ev_check, ev_dew, ev_win, ev_rain_start, ev_sun_start, ev_wave, ev_brace_hold, ev_gust, ev_zip, ev_zip_fizzle, ev_peck, ev_bird, ev_fly, ev_spool;
} sim;

void sim_start(sim *S, const level_def *L, uint32_t seed);
bool sim_gusting(const sim *S); /* the wind pushing now (TWIST_WIND) */
bool sim_gust_warn(const sim *S);
void sim_step(sim *S, const sim_input *in);
bool sim_on_patch(const sim *S, float th, float y, int *which);
#define BIRD_EVERY 1500
#define BIRD_WARN 75   /* ticks the mark shows before the peck */
#define BIRD_STAY 140
float sim_silk_reach(const sim *S);
bool sim_anchor_above(const sim *S, float *y);
uint32_t sim_hash(const sim *S);
float wrap_pi(float a);

/* ── presentation (world.c): three.c objects, render-only ───────────── */

void world_build(cw_node root, const level_def *L, int title);
void world_free(void);
/* positions the spider, water, rain, sun and clouds for this frame */
void world_sync(const sim *prev, const sim *cur, float alpha, float t_seconds, bool stopmotion);
t3_vec3 world_spider_pos(const sim *prev, const sim *cur, float alpha, float *th_out);
void world_camera(cw_node cam, const sim *prev, const sim *cur, float alpha, float t, bool title);
extern uint32_t tex_hash, world_hash;

/* textures.c: procedural paper (albedo, normal, roughness, ao) */
enum { PAPER_WHITE, PAPER_KRAFT, PAPER_PIPE, PAPER_BRICK, PAPER_GRASS, PAPER_SKYBLUE, PAPER_DARK, PAPER_WOOD, PAPER_BACKDROP, PAPER_PLAIN, PAPER_KINDS };
void paper_apply(t3_material *m, int kind, float repeat_u, float repeat_v);
void paper_free_all(void);

#endif
