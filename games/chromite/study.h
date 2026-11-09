/* chromite's study (study.c): the newcomer bot and the runs of level 1 the newcomer gate makes without drawing */
#ifndef CHROMITE_STUDY_H
#define CHROMITE_STUDY_H

#include "game.h"

#define VIEW_N 24 /* craft and rounds the view lists (the debug fields view.enemies and view.rounds) */
/* the newcomer's make-up (tests/chromite-newcomer.mjs has the same numbers) */
#ifndef NB_LOOK
#define NB_LOOK 6 /* ticks between its looks */
#endif
#ifndef NB_DELAY_LOOKS
#define NB_DELAY_LOOKS 2 /* it acts on the rounds it saw this many looks before: 12 to 18 ticks, a person's reaction */
#endif
#define NB_HIST 4
#ifndef NB_LEAD
#define NB_LEAD 0.7f /* how much of a round's move since that look it guesses */
#endif
#ifndef NB_AIM_SIGMA
#define NB_AIM_SIGMA 0.7f /* its aim error, field units */
#endif
#ifndef NB_AIM_HOLD
#define NB_AIM_HOLD 40 /* looks between its aim corrections, on average */
#endif
#ifndef NB_MISS
#define NB_MISS 0.1f /* the share of rounds it does not notice */
#endif
#define NB_HOME_Y -4.5f /* where it likes to fly */
#define NB_SPEED (8 * 0.03f * K) /* the ship's top speed a tick */
#ifndef NB_MAX_CONTINUES
#define NB_MAX_CONTINUES 9
#endif

/* what the screen shows: the craft (x, y; 99 = none) and the VIEW_N enemy rounds nearest the ship */
void sim_view(const sim *S, float *en, float *rn);

typedef struct newcomer {
  uint32_t rng;
  int looks;
  float aim_err, tx, hx, hy;
  float en[NB_HIST][VIEW_N * 2], rn[NB_HIST][VIEW_N * 4]; /* rounds: x, y, and the move it saw in a tick */
} newcomer;
void newcomer_init(newcomer *b, uint32_t seed);
void newcomer_see(newcomer *b, const float *en, const float *ra, const float *rb, float hx, float hy);
void newcomer_decide(newcomer *b, float *sx, float *sy);

typedef struct study_run {
  int ticks, clear_tick, boss_tick, lives_at_boss, lives, wave_deaths, boss_deaths, continues, kills, escapes, boss_hp100;
  int32_t score;
} study_run;
void study_newcomer_run(uint32_t seed, float base_skill, uint32_t game_seed, study_run *out);

#endif
