/* debris.c: parts that come off (bumpers, bonnets), render only */
#ifndef DERBY_DEBRIS_H
#define DERBY_DEBRIS_H
#include "game.h"
void debris_reset(void);           /* a new scene: forget every part (the scene frees their nodes) */
void debris_tick(const sim *s);    /* fixed tick, after world_tick: this tick's hits knock parts off; parts fly and lie */
bool debris_resting(t3_vec3 *at);     /* the newest part lying on the dirt (dev camera DERBY_CAM_DEBRIS) */
extern int32_t debris_count_dbg;   /* parts knocked off this bout (debug debris.count) */
extern int32_t debris_float_dbg;   /* the largest gap under a resting part, mm (debug debris.float_mm) */
#endif
