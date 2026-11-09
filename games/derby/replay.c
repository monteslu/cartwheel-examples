/* derby: the results screen's replay of the bout's biggest hit. Looking only: the bout records every car's state
 * each sim tick into a ring (the last three seconds); a crash bigger than any before it is kept, once a second and a
 * half after it is in the ring, as a window from a second and a half before the hit to a second and a half after.
 * The results screen poses the car models from that window (world_pose_car) instead of the sim, so nothing here
 * changes a result or a replayed bout. */
#include <math.h>
#include <string.h>

#include "game.h"
#include "replay.h"
#include "world.h"

#define RING 180       /* ticks kept: 3 s */
#define HALF (RING / 2) /* the hit sits in the middle of the window */

static struct {
  car ring[RING][MAXC];
  int head, filled;    /* the next slot to write; slots written (up to RING) */
  car win[RING][MAXC]; /* the kept window, hit at HALF */
  bool have;           /* a window is kept */
  int pending;         /* sim ticks until the newest biggest hit fills its window (0: none waiting) */
  float best_k;        /* the biggest crash so far (tonne m/s) */
  replay_hit hit, wait_hit;
  bool on[MAXC];
} R;

void replay_reset(void) { memset(&R, 0, sizeof R); }

void replay_record(const sim *s) {
  for (int i = 0; i < MAXC; i++) {
    R.ring[R.head][i] = s->c[i];
    R.on[i] = s->c[i].on;
  }
  R.head = (R.head + 1) % RING;
  if (R.filled < RING) R.filled++;
  if (R.pending > 0 && --R.pending == 0 && R.filled >= RING) {
    /* the hit is HALF ticks back: the ring's oldest slot is the window's first tick */
    for (int t = 0; t < RING; t++) memcpy(R.win[t], R.ring[(R.head + t) % RING], sizeof R.win[t]);
    R.hit = R.wait_hit;
    R.have = true;
  }
  for (int e = 0; e < s->ev_n; e++) {
    const struct sim_event *v = &s->ev[e];
    if (v->kind != EV_CRASH || v->k <= R.best_k || v->k < REPLAY_MIN_K) continue;
    R.best_k = v->k;
    R.wait_hit = (replay_hit){ v->a, v->b, v->k, v->at, s->tick };
    R.pending = HALF - 1; /* the ring holds the half after it this many ticks on */
  }
}

/* the bout ended inside a hit's window: keep what there is, the end held */
void replay_finish(void) {
  if (R.pending <= 0 || R.filled < RING) return;
  int after = HALF - 1 - R.pending; /* ticks recorded since the hit (the newest is the hit + after) */
  for (int t = 0; t < RING; t++) {
    int back = after - (t - HALF); /* how far the window's tick t is behind the newest */
    if (back < 0) back = 0;        /* past the end: the last pose held */
    memcpy(R.win[t], R.ring[((R.head - 1 - back) % RING + RING) % RING], sizeof R.win[t]);
  }
  R.hit = R.wait_hit;
  R.have = true;
  R.pending = 0;
}

bool replay_have(void) { return R.have; }
const replay_hit *replay_the_hit(void) { return R.have ? &R.hit : NULL; }

/* the window's tick for a frame of the playback: a second at full speed up to the hit, the hit and half a second
 * after it at a third of the speed, then a second at full speed; fractional between ticks */
static float play_tick(int f) {
  const float pre = 60, slow = 90, post = 60;
  float t = (float)f;
  if (t < pre) return HALF - pre + t;
  if (t < pre + slow) return HALF + (t - pre) / 3.0f;
  return HALF + slow / 3.0f + fminf(t - pre - slow, post - 1);
}
int replay_frames(void) { return R.have ? REPLAY_FRAMES : 0; }
bool replay_slow(int f) { return f >= 60 && f < 150; }

static t3_quat qlerp(t3_quat a, t3_quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w, s = d < 0 ? -1.0f : 1.0f;
  t3_quat q = { a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t };
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (l > 1e-6f) q.x /= l, q.y /= l, q.z /= l, q.w /= l;
  return q;
}

/* poses every car of the window at frame f of the playback (the car's drawn pose only) */
void replay_pose(int f) {
  if (!R.have) return;
  float tk = play_tick(f);
  int t0 = (int)tk;
  if (t0 < 0) t0 = 0;
  if (t0 > RING - 2) t0 = RING - 2;
  float u = cw_clampf(tk - (float)t0, 0, 1);
  for (int i = 0; i < MAXC; i++) {
    if (!R.on[i]) continue;
    car c = R.win[t0][i];
    const car *n = &R.win[t0 + 1][i];
    c.x += (n->x - c.x) * u, c.y += (n->y - c.y) * u, c.z += (n->z - c.z) * u;
    c.q = qlerp(c.q, n->q, u);
    world_pose_car(i, &c);
  }
}
