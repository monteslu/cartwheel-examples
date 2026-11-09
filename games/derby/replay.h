/* replay.c: the results screen's replay of the bout's biggest hit (render only) */
#ifndef DERBY_REPLAY_H
#define DERBY_REPLAY_H
#include "game.h"

#define REPLAY_MIN_K 4.0f   /* a crash this big (tonne m/s) or more is worth a replay */
#define REPLAY_FRAMES 210   /* a second up to it, the hit at a third of the speed, a second after */
typedef struct replay_hit { int a, b; float k; t3_vec3 at; int tick; } replay_hit;
void replay_reset(void);
void replay_record(const sim *s);  /* each sim tick of the bout */
void replay_finish(void);          /* the bout is over: keep a window the end cut short */
bool replay_have(void);
const replay_hit *replay_the_hit(void);
int replay_frames(void);           /* 0 when there is nothing to replay */
bool replay_slow(int f);           /* frame f plays at a third of the speed (the hit) */
void replay_pose(int f);           /* the cars as they were at frame f of the playback */
#endif
