/* derby's CPU drivers. Each picks a mark (near, already battered, and not
 * facing it nose on), aims where the mark will be when it gets there, at its
 * flank, and floors it; it backs off the wall when it is stuck, swerves off
 * a wall it is about to meet head on, and the better ones handbrake round
 * tight turns and pick their moments. All of it reads the simulation and a
 * seeded stream of its own: deterministic like the bout.
 *
 * Levels: 0 rookie (slow to react, sloppy aim, half throttle in the turns),
 * 1 pro, 2 ace (quick, goes for the flanks, finishes off the weak). */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint32_t rnd(ai_mem *m) {
  m->rng ^= m->rng << 13;
  m->rng ^= m->rng >> 17;
  m->rng ^= m->rng << 5;
  return m->rng;
}
static float rnd01(ai_mem *m) { return (float)(rnd(m) & 0xffffff) / 16777216.0f; }

void ai_reset(ai_mem *m, int me, uint32_t seed) {
  memset(m, 0, sizeof *m);
  m->rng = seed * 2654435761u + (uint32_t)me * 40503u + 1u;
  m->target = -1;
  m->retarget = (int)(rnd(m) % 30u);
  m->aim_side = 1;
}

static int pick(const sim *s, int me, int level, ai_mem *m) {
  const car *c = &s->c[me];
  int best = -1;
  float bs = 1e9f;
  for (int i = 0; i < MAXC; i++) {
    const car *o = &s->c[i];
    if (i == me || !o->on || o->wrecked || o->respawn > 0) continue;
    float dx = o->x - c->x, dz = o->z - c->z, d = sqrtf(dx * dx + dz * dz);
    float score = d;
    if (level >= 1) score -= (100 - o->hp) * 0.12f;          /* the weak are tempting */
    if (level >= 2) {
      float f, sd;
      car_local(o, c->x, c->z, &f, &sd);
      if (f > 0 && fabsf(sd) < 1.5f) score += 8;            /* not into its armoured nose */
    }
    if (i == m->target) score -= 4;                         /* some loyalty */
    float jitter = rnd01(m) * 6;
    score += jitter;
    if (score < bs) bs = score, best = i;
  }
  return best;
}

cin ai_think(const sim *s, int me, int level, ai_mem *m) {
  cin out = { 0 };
  const car *c = &s->c[me];
  if (!c->on || c->wrecked || s->phase != PH_GO) return out;
  static const int REACT[3] = { 26, 14, 6 }, THINK[3] = { 150, 110, 70 };
  static const float THR[3] = { 0.72f, 0.88f, 1.0f }, AIM_ERR[3] = { 2.4f, 1.2f, 0.4f };
  if (--m->retarget <= 0 || m->target < 0 || s->c[m->target].wrecked) {
    m->target = pick(s, me, level, m);
    int span = THINK[level] / 2;
    m->retarget = THINK[level] + (int)(rnd(m) % (uint32_t)span);
    float side = rnd01(m);
    m->aim_side = side < 0.5f ? -1 : 1;
  }
  float speed = car_speed(c);
  /* stuck: throttling into something and going nowhere -> back out */
  if (m->reverse > 0) {
    m->reverse--;
    out.thr = -110;
    out.steer = (int8_t)(m->aim_side > 0 ? 100 : -100);
    return out;
  }
  if (fabsf(speed) < 1.2f && s->phase_ticks > 60) m->stuck++;
  else m->stuck = m->stuck > 2 ? m->stuck - 2 : 0;
  if (m->stuck > 70 - level * 15) {
    m->stuck = 0;
    m->reverse = 45 + (int)(rnd(m) % 30u);
    return out;
  }
  float tx = 0, tz = 0;
  if (m->target >= 0) {
    const car *o = &s->c[m->target];
    float dx = o->x - c->x, dz = o->z - c->z, d = sqrtf(dx * dx + dz * dz);
    float t = d / fmaxf(8, fabsf(speed) + 4); /* lead it */
    tx = o->x + o->vx * t * (0.4f + 0.3f * level);
    tz = o->z + o->vz * t * (0.4f + 0.3f * level);
    /* go for a flank: the target's side, a little ahead of its middle */
    float s2, c2;
    cw_sincos(o->h, &s2, &c2);
    float side = m->aim_side * 1.1f * (level >= 1 ? 1 : 0.4f);
    tx += c2 * side + s2 * 0.4f;
    tz += -s2 * side + c2 * 0.4f;
    /* sloppy hands */
    float e = AIM_ERR[level] * (1 - clampf(d / 30, 0, 1) * 0.5f);
    tx += cw_sin(s->tick * 0.031f + me) * e;
    tz += cw_cos(s->tick * 0.027f + me * 2) * e;
  }
  /* the wall ahead: steer for the middle instead */
  float fx, fz;
  cw_sincos(c->h, &fx, &fz);
  float look = 4 + fmaxf(0, speed) * 0.45f;
  float nx, nz, ahead = arena_sd(c->x + fx * look, c->z + fz * look, &nx, &nz);
  if (ahead > -2.5f && speed > 6) {
    float k = clampf((ahead + 2.5f) / 3, 0, 1);
    tx = tx * (1 - k) + (c->x * 0.2f) * k;
    tz = tz * (1 - k) + (c->z * 0.2f) * k;
  }
  float f, sd;
  car_local(c, tx, tz, &f, &sd);
  float ang = cw_atan2(sd, f); /* + : the mark is to the left */
  /* reaction lag: the steering eases toward the aim */
  float steer = clampf(-ang * 2.2f, -1, 1);
  if (m->react > 0) m->react--;
  else {
    m->react = REACT[level];
  }
  float thr = THR[level];
  if (fabsf(ang) > 1.2f && speed > 8) thr = level >= 1 ? 0.2f : 0.45f;
  if (fabsf(ang) > 2.3f && speed < 4) {
    /* it is behind: reverse round toward it */
    thr = -0.8f;
    steer = -steer;
  }
  out.thr = (int8_t)(clampf(thr, -1, 1) * 127);
  out.steer = (int8_t)(steer * 127);
  if (level >= 1 && fabsf(ang) > 1.0f && speed > 9 && f < 14) out.btn |= CB_HANDBRAKE;
  return out;
}
