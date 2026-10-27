/* racer's front end and HUD (ui.c) and what the game (main.c) offers it.
 *
 * ui.c owns every screen that is not the 3D race: title, player join, vehicle and map
 * select, options, pause, results, and the HUD drawn over the race (also each split-screen
 * view's HUD). main.c owns the race itself: it builds the track and the world, steps
 * the sim, runs the cameras, and asks the UI each tick whether to step.
 *
 * ui.c is one translation unit made of three files: ui.c (state, input, drawing helpers,
 * the save, the screen flow), ui_hud.inc (the race HUD) and ui_screens.inc (the menus). */
#ifndef RACER_UI_H
#define RACER_UI_H

#include "cartwheel/render2d.h"
#include "game.h"
#include "ghost.h"

/* what a race is: a normal race, a time trial (one kart, no items, a ghost to race), a race of a cup */
enum { RM_RACE, RM_TRIAL, RM_CUP, RM_BATTLE };
/* speed classes (sim_set_class) */
enum { CLASS_STANDARD, CLASS_FAST, CLASS_BLAZING, CLASS_COUNT };

typedef struct race_setup {
  int map;          /* 0..NMAPS-1 */
  int laps;         /* 1..MAX_LAPS (0: the map's own) */
  int humans;       /* 0 = a race of CPUs only (the title's background); 1..MAXV: that many views */
  int cpu_skill;    /* 0 rookie, 1 pro, 2 ace, 3 mixed */
  int vehicle[MAXV];/* each human's vehicle (VEH index) */
  int colour[MAXV]; /* each human's paint (0..7) */
  bool items;       /* item boxes on */
  bool turbos_only; /* the boxes give turbos only (a party preset) */
  bool attract;     /* a demo race behind the menus: the camera flies the track */
  int mode;         /* RM_* */
  int speed_class;  /* CLASS_*: every kart's top speed and acceleration (the time trial is always Standard) */
  bool assist_steer;/* the humans' steering assist: a gentle pull back towards the road when a wall or the edge is close */
  bool assist_gas;  /* the humans' auto-accelerate: the throttle is held unless the brake is pressed */
  const ghost_saved *ghost; /* a time trial: the run to race (NULL: none). Must stay valid for the race (the save data does) */
  /* battle (mode RM_BATTLE): map is an arena (ARENAS[]), karts the karts in it (humans first, CPUs fill; 0: eight), the round's
   * length in seconds (0: three minutes) */
  int karts;
  int battle_secs;
  int arena_mode;   /* AM_*: what the arena round is played for (balloons, capture the flag, king of the mountain) */
} race_setup;

/* ── provided by main.c ─────────────────────────────────────────────── */
void game_start_race(const race_setup *rs);
void game_end_race(void);                    /* unload the race (back to a bare front end) */
bool game_is_trial(void);                    /* the race loaded is a time trial */
/* a time trial's instant retry: the same race over from the grid, no menu and no reload (a short countdown) */
void game_retry(void);
/* the time trial's recording of this run (the kart's pose along the run) into dst; false when it is not whole */
bool game_commit_ghost(ghost_saved *dst, uint32_t time_cs);
/* show this ghost from now on (NULL: none), e.g. the run that was just a record */
void game_set_ghost(const ghost_saved *g);
bool game_loaded(void);                      /* a race is loaded */
const sim *game_sim(void);
int game_views(void);                        /* views on screen: 1..MAXV */
int game_view_kart(int v);                   /* the kart view v follows */
int game_view_own_kart(int v);               /* the kart view v belongs to (a battle: game_view_kart differs once it is out and the view watches another) */
int game_view_player(int v);                 /* the input player (pad slot) behind view v */
/* the rectangle of view v on a W x H canvas */
void game_view_rect(int v, float W, float H, float *x, float *y, float *w, float *h);
/* the camera: 0 chase, 1 fly the track (a demo), 2 orbit the leader (the results) */
void game_set_camera(int mode);

/* ── provided by ui.c ───────────────────────────────────────────────── */
void ui_init(void);
void ui_fixed(void);                         /* every fixed tick, first */
bool ui_race_runs(void);                     /* the sim steps this tick (a race is on and not paused) */
void ui_race_over(void);                     /* main: the race has ended (everyone is done or the clock ran out) */
void ui_render(float W, float H);            /* 2D over the 3D frame */
void ui_enter_race(const race_setup *rs);    /* dev aids and tests: start this race now, as if chosen from the menus */
void ui_shutdown(void);
/* the save data's ghost for a map's time trial (NULL when there is none yet) */
const ghost_saved *ui_trial_ghost(int map);
/* dev aid and gates: the next n trial runs retry by themselves a moment after the results appear (with the bot driving) */
void ui_set_auto_runs(int n);
/* dev aid and gates: start a cup that runs by itself (the bot drives): cup, humans, laps per race, races (0: the usual) */
void ui_debug_cup(int cup, int humans, int laps, int races);

/* The save data (cw_app_desc.save / save_size): best laps, the last race's choices, the
 * graphics and volume settings, in a versioned and checksummed layout. main.c hands these
 * to the app description (a call, so set the fields after the static initialiser). */
void *ui_save_ptr(void);
size_t ui_save_size(void);

/* The canvas size a dev run opens at: RACER_RES=WxH (e.g. 1920x1080, 540x960 for a portrait
 * window). Returns false when it is not set. */
bool ui_dev_resolution(int *w, int *h);

/* the font the graphics page (cw_quality_menu_draw, drawn by main.c after ui_render) is set in */
cw_font ui_menu_font(void);

#endif
