/* spout's study: climbs run without drawing, for the gates (study.c) */
#ifndef SPOUT_STUDY_H
#define SPOUT_STUDY_H
#include "game.h"
typedef struct study_climb { int ticks, washes, knocks, pecks, braced; } study_climb; /* ticks -1: not cleared in 3 minutes */
void study_hazards(int level, study_climb out[3][3]); /* [hold, tell, none][seed] */
typedef struct study_web { float web_y, before, after; int washed, web_left, caught, silk; } study_web;
typedef struct study_line { int started; float anchor, lowest, back, swung; int ticks, silk_spent, cling; } study_line;
typedef struct study_silk_out { study_web web, none; study_line line; } study_silk_out;
void study_silk(study_silk_out *o);
void study_hands(int out[4][3]); /* [stick, steady hands, ragged hands, one hand][seed]: ticks to the top */
#endif
