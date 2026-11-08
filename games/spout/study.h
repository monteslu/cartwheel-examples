/* spout's study: climbs run without drawing, for the gates (study.c) */
#ifndef SPOUT_STUDY_H
#define SPOUT_STUDY_H
#include "game.h"
typedef struct study_climb { int ticks, washes, knocks, pecks, braced; } study_climb; /* ticks -1: not cleared in 3 minutes */
void study_hazards(int level, study_climb out[3][3]); /* [hold, tell, none][seed] */
#endif
