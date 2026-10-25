/* racer's CPU drivers: follow the road on a racing line, brake for corners, use the items.
 * Reads the simulation and a seeded stream of its own. */
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

void ai_reset(ai_mem *m, int me, int skill, uint32_t seed) {
  memset(m, 0, sizeof *m);
  m->skill = skill;
  m->rng = seed * 2654435761u + (uint32_t)me * 40503u + 1u;
  m->weave_phase = rnd01(m) * 6.28f;
}

kin ai_think(const sim *s, int me, ai_mem *m) {
  const kart *k = &s->k[me];
  const track *t = s->track;
  kin in = { 0 };
  if (!k->on) return in;
  float spd = k->speed;
  float look = 7.0f + clampf(spd, 0, 60) * 0.42f;
  /* the line: a lane that drifts, pulled off the walls, away from the kart it is about to hit */
  m->lane_ticks--;
  if (m->lane_ticks <= 0) {
    float hw = t->s[k->loc.i].hw - 2.5f;
    m->lane_want = (rnd01(m) * 2 - 1) * hw * (m->skill == 2 ? 0.55f : 0.7f);
    m->lane_ticks = 120 + (int)(rnd(m) % 200u);
  }
  m->lane += clampf(m->lane_want - m->lane, -0.05f, 0.05f);
  tsample here = track_at(t, k->loc.s);
  float hw = here.hw - 2.0f;
  float lane = clampf(m->lane, -hw, hw);
  /* avoid a kart just ahead in the lane */
  for (int j = 0; j < MAXK; j++) {
    if (j == me || !s->k[j].on) continue;
    float ds = s->k[j].loc.s - k->loc.s;
    if (t->circuit) {
      if (ds > t->length * 0.5f) ds -= t->length;
      if (ds < -t->length * 0.5f) ds += t->length;
    }
    if (ds > 0 && ds < 14 && fabsf(s->k[j].loc.lat - k->loc.lat) < 2.4f) lane += (k->loc.lat > s->k[j].loc.lat ? 1.0f : -1.0f) * 2.6f;
  }
  lane = clampf(lane, -hw, hw);
  /* on a corkscrew or in a loop the road turns under the kart: it holds the middle */
  if (here.flags & TF_LOOP) lane = 0;
  t3_vec3 target = track_point(t, k->loc.s + look, lane, 0);
  t3_vec3 d = v3sub(target, k->p);
  t3_vec3 up = kart_up(k), f = kart_fwd(k), r = kart_right(k);
  d = v3sub(d, v3mul(up, v3dot(d, up)));
  float fx = v3dot(d, f), rx = v3dot(d, r);
  float ang = cw_atan2(rx, fx); /* + = target to the right */
  float st = clampf(ang * 2.2f, -1, 1);
  m->steer_smooth += (st - m->steer_smooth) * 0.35f;
  in.steer = (int8_t)(clampf(m->steer_smooth, -1, 1) * 127);
  /* corners: the speed the road ahead allows */
  float curv = 0;
  for (int c = 1; c <= 4; c++) {
    /* loops, corkscrews and jumps have no plan-view bend worth braking for */
    const tsample *ahead = &t->s[(k->loc.i + c * 14) % t->n];
    if (ahead->flags & (TF_LOOP | TF_GAP)) continue;
    float cv = fabsf(track_curvature(t, k->loc.s + c * 14.0f, 14.0f));
    if (cv > curv) curv = cv;
  }
  float vmax = curv > 1e-4f ? sqrtf(32.0f / curv) : 99;
  float skill_k = m->skill == 0 ? 0.80f : m->skill == 1 ? 0.92f : 1.0f;
  vmax *= skill_k;
  in.thr = 127;
  if (s->phase == PH_COUNT) {
    /* the lights: the better drivers get the launch right, a rookie floods it now and then */
    int when = m->skill == 2 ? 163 : m->skill == 1 ? 168 : 150 + (int)(m->rng % 40u);
    in.thr = s->phase_ticks >= when ? 127 : 0;
  }
  if (spd > vmax + 2) in.thr = (int8_t)(spd > vmax * 1.2f ? -127 : 0);
  /* a gap ahead: all the speed it can have */
  for (int c = 0; c < 6; c++) {
    int idx = (k->loc.i + c * 6) % t->n;
    if (t->s[idx].flags & (TF_GAP | TF_RAMP | TF_LOOP)) in.thr = 127;
  }
  /* drift through the sharp ones (the skilled) */
  if (m->skill >= 1 && curv > 0.012f && spd > 22 && fabsf(ang) > 0.15f && !(here.flags & (TF_LOOP | TF_GAP | TF_RAMP))) in.btn |= KB_DRIFT;
  /* stuck: back out */
  if (fabsf(spd) < 1.5f && s->phase == PH_RACE) m->stuck++;
  else m->stuck = 0;
  if (m->stuck > 90) m->unstick = 60, m->stuck = 0;
  if (m->unstick > 0) {
    m->unstick--;
    in.thr = -127;
    in.steer = (int8_t)(-in.steer);
  }
  /* items */
  if (k->item != IT_NONE && k->item_wait == 0) {
    bool use = false;
    switch (k->item) {
    case IT_TURBO:
    case IT_TURBO3: use = k->boost_ticks == 0 && k->grounded; break;
    case IT_SHIELD: use = true; break;
    case IT_MISSILE:
    case IT_HOMING:
      for (int j = 0; j < MAXK; j++) {
        if (j == me || !s->k[j].on) continue;
        float ds = s->k[j].loc.s - k->loc.s;
        if (ds > 6 && ds < 90 && (k->item == IT_HOMING || fabsf(s->k[j].loc.lat - k->loc.lat) < 3.0f)) use = true;
      }
      break;
    case IT_OIL:
    case IT_MINE:
      for (int j = 0; j < MAXK; j++) {
        if (j == me || !s->k[j].on) continue;
        float ds = k->loc.s - s->k[j].loc.s;
        if (ds > 3 && ds < 30) use = true;
      }
      break;
    case IT_SHOCK: use = k->place > 3; break;
    }
    m->item_wait++;
    if (use && m->item_wait > 30 + (int)(rnd(m) % 90u)) in.btn |= KB_ITEM, m->item_wait = 0;
  }
  return in;
}
