/* racer's time trial ghost (ghost.c): a recording of a kart's pose along a run, and a translucent kart that replays it.
 *
 * RENDER ONLY. Nothing here feeds the simulation: the ghost has no body, no collision, takes no item box and no
 * kart can be hit by it. main.c records the human's kart while a trial runs and hands the best run to the save data;
 * the next trial replays it. The HUD asks for the gap in seconds between the player and the ghost.
 *
 * The recording: one pose every GH_STEP ticks (20 a second) from the lights going out, a position as 16 bit
 * steps of 1/32 m from the previous pose (the quantised value is what the next step is measured from, so the error
 * never builds up) and the orientation as a "smallest three" quaternion in 32 bits: 10 bytes a pose, at most GH_MAX
 * poses (two minutes), so a ghost is 24 KB and the save data (one per map) stays bounded. A run longer than that
 * keeps its time but not its ghost. */
#ifndef RACER_GHOST_H
#define RACER_GHOST_H

#include "game.h"

#define GH_STEP 3
#define GH_MAX 2400
#define GH_POSE 10

/* a ghost as the save data holds it (little-endian fields, written as plain structs by the app) */
typedef struct ghost_saved {
  uint32_t time_cs;     /* the run's time in hundredths of a second, 0 = no ghost */
  uint16_t n;           /* poses */
  uint8_t veh, colour;  /* the kart it was */
  int32_t x0, y0, z0;   /* the first pose, 1/32 m */
  uint8_t data[GH_MAX * GH_POSE];
} ghost_saved;

/* ── recording (fixed tick, after the sim step) ──────────────────────── */
void ghost_rec_start(const kart *k);
void ghost_rec_tick(const kart *k, int race_ticks); /* every tick of the race; it keeps every GH_STEP-th */
int ghost_rec_count(void);
uint32_t ghost_rec_hash(void);                      /* over the recorded bytes (a debug/gate aid) */
/* copies the recording into dst when it is whole (it fits and has poses); false: nothing written */
bool ghost_rec_commit(ghost_saved *dst, uint32_t time_cs);

/* ── playback ────────────────────────────────────────────────────────── */
/* decode a saved ghost for this track (works out how far along the lap each pose is); false when empty or malformed */
bool ghost_load(const ghost_saved *g, const track *t);
void ghost_unload(void);
bool ghost_loaded(void);
uint32_t ghost_time_cs(void);
uint32_t ghost_play_hash(void);                     /* over the decoded poses' bytes (a debug/gate aid) */
/* the ghost's pose at a time in ticks since the lights went out (fractional); false once the run is over */
bool ghost_pose(float ticks, t3_vec3 *p, t3_quat *q);
/* seconds the player is behind (+) or ahead (-) of the ghost, at the same place on the track: prog is the player's
 * kart.prog (metres along the run, unwrapped), ticks the race clock. Valid only while the ghost has covered that far. */
bool ghost_gap(float prog, int ticks, float *gap_secs);

/* ── drawing ─────────────────────────────────────────────────────────── */
void ghost_gfx_build(cw_node parent);               /* after ghost_load; no ghost loaded builds nothing */
void ghost_gfx_reset(void);                         /* before the scene is freed */
void ghost_gfx_tick(const sim *s);                  /* fixed tick: the pose for the sim's clock */
void ghost_gfx_frame(float alpha);                  /* every frame: wheels, strut, shadow-less translucent look */
void ghost_gfx_show(bool on);

#endif
