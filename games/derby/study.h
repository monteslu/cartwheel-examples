/* derby's studies: bouts and test drives run without drawing, for the gates (study.c) */
#ifndef DERBY_STUDY_H
#define DERBY_STUDY_H
#include "game.h"

typedef struct study_bout {
  float hp10;       /* the player's engine ten seconds after the flag (0 wrecked) */
  float hunters10;  /* CPU drivers after the player, averaged over those ten seconds */
  int wrecked_tick; /* ticks from the flag to the player's wreck; -1 still running at the end */
} study_bout;
void study_opening(int bouts, study_bout *out);

typedef struct study_pull_result {
  float turned_deg, top, hp, bend, grip_l, grip_r;
} study_pull_result;
void study_pull(int zone, study_pull_result *out); /* zone -1: undamaged */

typedef struct study_kind_drive {
  float speed2, top, hp_after_hit; /* m/s 2 s after the gas from rest, the most in 4 s; the engine left after a standard door hit */
} study_kind_drive;
/* bouts of six CPU pros, the four bodies rotated through the grid: wins and starts per body; and each body's test drive */
void study_classes(int bouts, int wins[CAR_KINDS], int starts[CAR_KINDS], study_kind_drive drive[CAR_KINDS]);

#endif
