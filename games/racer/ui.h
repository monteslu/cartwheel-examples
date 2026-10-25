/* racer's front end and HUD (ui.c) and what the game (main.c) offers it.
 *
 * ui.c owns every screen that is not the 3D race: title, player join, vehicle and map
 * select, pause, results, and the HUD drawn over the race (also each split-screen
 * view's HUD). main.c owns the race itself: it builds the track and the world, steps
 * the sim, runs the cameras, and asks the UI each tick whether to step. */
#ifndef RACER_UI_H
#define RACER_UI_H

#include "game.h"

typedef struct race_setup {
  int map;          /* 0..NMAPS-1 */
  int laps;         /* 1..MAX_LAPS (0: the map's own) */
  int humans;       /* 0 = a race of CPUs only (the title's background); 1..MAXV: that many views */
  int cpu_skill;    /* 0 rookie, 1 pro, 2 ace, 3 mixed */
  int vehicle[MAXV];/* each human's vehicle (VEH index) */
  int colour[MAXV]; /* each human's paint (0..7) */
  bool items;       /* item boxes on */
  bool attract;     /* a demo race behind the menus: the camera flies the track */
} race_setup;

/* ── provided by main.c ─────────────────────────────────────────────── */
void game_start_race(const race_setup *rs);
void game_end_race(void);                    /* unload the race (back to a bare front end) */
bool game_loaded(void);                      /* a race is loaded */
const sim *game_sim(void);
int game_views(void);                        /* views on screen: 1..MAXV */
int game_view_kart(int v);                   /* the kart view v follows */
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

#endif
