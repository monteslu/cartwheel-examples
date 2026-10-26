/* racer's CPU drivers: follow the road on a racing line, brake for corners, use the items, and take the shortcuts
 * (the track's branches) by skill: before each mouth a driver decides once, from its own seeded stream, whether it
 * goes in (an Ace mostly does, a Rookie mostly does not), and drops the idea when it is too slow for a branch that
 * asks for speed (a jump). Reads the simulation and a seeded stream of its own. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
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

/* main-road metres from a to b going forward (a circuit wraps) */
static float ahead_of(const track *t, float a, float b) {
  float d = b - a;
  if (t->circuit) {
    while (d < 0) d += t->length;
    while (d >= t->length) d -= t->length;
  }
  return d;
}

/* The branch this driver is heading into, while still on the main road: its mouth within reach ahead, or just passed
 * (the branch still runs alongside the main road there). *bs: where the kart is in the branch's metres (negative before
 * the mouth). -1: none. */
static int next_mouth(const sim *s, const kart *k, const ai_mem *m, float reach, float *bs) {
  const track *t = s->track;
  if (k->loc.br >= 0) return -1;
  for (int b = 0; b < t->nbranch && b < 4; b++) {
    if (m->take[b] != 1) continue;
    const track_branch *B = &t->br[b];
    float d = ahead_of(t, k->loc.s, B->s0);
    if (d < reach) {
      *bs = -d;
      return b;
    }
    if (ahead_of(t, B->s0, k->loc.s) < 80.0f) {
      track_loc l;
      track_locate_branch(t, b, k->p, &l);
      *bs = l.s;
      return b;
    }
  }
  return -1;
}

/* the road `ahead` metres along the way this driver is going: its branch, or the main road (into the branch it chose:
 * rb, at rbs metres of it, from next_mouth) */
static tsample route_at(const sim *s, const kart *k, int rb, float rbs, float ahead) {
  const track *t = s->track;
  if (k->loc.br >= 0) return track_at_loc(t, &k->loc, ahead);
  if (rb >= 0) return track_branch_at(t, rb, rbs + ahead);
  return track_at(t, k->loc.s + ahead);
}

static float heading_of(t3_vec3 f) { return cw_atan2(f.x, f.z); }
static float wrap_pi(float a) {
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  return a;
}

/* the shortcuts: decide each one once on the way to it, forget the choice once past it */
static void plan_branches(const sim *s, const kart *k, ai_mem *m) {
  const track *t = s->track;
  for (int b = 0; b < t->nbranch && b < 4; b++) {
    const track_branch *B = &t->br[b];
    if (k->loc.br == b) continue;
    float d = ahead_of(t, k->loc.ms, B->s0);
    if (d > 60.0f && d < 260.0f) {
      if (m->take[b] == 0) {
        m->take[b] = rnd01(m) < B->ai_take[m->skill < 0 ? 0 : m->skill > 2 ? 2 : m->skill] ? 1 : -1;
        /* dev aid: RACER_AI_TAKE=0|1 makes every driver stay out of (0) or take (1) every branch */
        if (getenv("RACER_AI_TAKE")) m->take[b] = atoi(getenv("RACER_AI_TAKE")) ? 1 : -1;
        if (getenv("RACER_AILOG")) cw_info("ai skill %d branch %d: %s (%.0f m before it)", m->skill, b, m->take[b] > 0 ? "takes it" : "stays out", d);
      }
    } else if (d >= 260.0f && ahead_of(t, B->s0, k->loc.ms) > 100.0f) {
      m->take[b] = 0; /* well past it (or far before): the next lap decides afresh */
    }
    /* a branch that asks for speed: too slow at the run-in and the driver stays on the main road */
    if (m->take[b] == 1 && d < 45.0f && B->ai_speed > 0 && k->speed < B->ai_speed * 0.92f) m->take[b] = -1;
    /* in trouble just before the mouth (spinning, airborne, way off line): not now */
    if (m->take[b] == 1 && d < 30.0f && (k->spin_ticks > 0 || !k->grounded)) m->take[b] = -1;
  }
}

kin ai_think(const sim *s, int me, ai_mem *m) {
  const kart *k = &s->k[me];
  const track *t = s->track;
  kin in = { 0 };
  if (!k->on) return in;
  float spd = k->speed;
  float look = 7.0f + clampf(spd, 0, 60) * 0.42f;
  plan_branches(s, k, m);
  bool on_branch = k->loc.br >= 0;
  float rbs = 0;
  int rb = next_mouth(s, k, m, 200.0f, &rbs), mouth = rbs > -90.0f ? rb : -1;
  /* a branch is narrow and its bends tight: a shorter look ahead holds its line instead of cutting across */
  if (on_branch || (mouth >= 0 && rbs > -25.0f)) look = 6.5f + clampf(spd, 0, 60) * 0.36f;
  /* the line: a lane that drifts, pulled off the walls, away from the kart it is about to hit */
  m->lane_ticks--;
  if (m->lane_ticks <= 0) {
    float hw = t->s[k->loc.i].hw - 2.5f;
    m->lane_want = (rnd01(m) * 2 - 1) * hw * (m->skill == 2 ? 0.55f : 0.7f);
    m->lane_ticks = 120 + (int)(rnd(m) % 200u);
  }
  float lane_rate = 0.05f;
  float want = m->lane_want;
  if (on_branch) want = 0, lane_rate = 0.2f; /* a branch is narrow: its middle */
  else if (mouth >= 0) {
    /* lining up on the mouth: the lane its centre leaves from */
    tsample bm = track_branch_at(t, mouth, 0);
    tsample mm = track_at(t, t->br[mouth].s0);
    want = v3dot(v3sub(bm.p, mm.p), mm.r);
    lane_rate = 0.12f;
  }
  m->lane += clampf(want - m->lane, -lane_rate, lane_rate);
  tsample here = track_at_loc(t, &k->loc, 0);
  float hw = here.hw - 2.0f;
  if (hw < 0.3f) hw = 0.3f;
  float lane = clampf(m->lane, -hw, hw);
  /* avoid a kart just ahead in the lane (not on a branch: no room) */
  if (!on_branch)
    for (int j = 0; j < MAXK; j++) {
      if (j == me || !s->k[j].on || s->k[j].loc.br >= 0) continue;
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
  tsample tgt = route_at(s, k, rb, rbs, look);
  /* the lane eases to the branch's middle once the target is on it */
  float tlane = (tgt.flags & TF_BRANCH) ? 0 : lane;
  t3_vec3 target = v3add(tgt.p, v3mul(tgt.r, tlane));
  t3_vec3 d = v3sub(target, k->p);
  t3_vec3 up = kart_up(k), f = kart_fwd(k), r = kart_right(k);
  d = v3sub(d, v3mul(up, v3dot(d, up)));
  float fx = v3dot(d, f), rx = v3dot(d, r);
  float ang = cw_atan2(rx, fx); /* + = target to the right */
  float st = clampf(ang * 2.2f, -1, 1);
  m->steer_smooth += (st - m->steer_smooth) * 0.35f;
  in.steer = (int8_t)(clampf(m->steer_smooth, -1, 1) * 127);
  /* corners: the speed the road ahead allows */
  float curv = 0, vroute = 99;
  bool routed = on_branch || mouth >= 0;
  for (int c = 1; c <= 4; c++) {
    /* loops, corkscrews and jumps have no plan-view bend worth braking for */
    if (!routed) {
      const tsample *ahead = &t->s[(k->loc.i + c * 14) % t->n];
      if (ahead->flags & (TF_LOOP | TF_GAP)) continue;
      float cv = fabsf(track_curvature(t, k->loc.s + c * 14.0f, 14.0f));
      if (cv > curv) curv = cv;
    } else {
      /* along its own way; on a branch (narrow, perhaps no walls to lean on) it trusts less grip */
      tsample a = route_at(s, k, rb, rbs, (float)c * 14.0f), b = route_at(s, k, rb, rbs, (float)c * 14.0f + 14.0f);
      if ((a.flags | b.flags) & (TF_LOOP | TF_GAP)) continue;
      float cv = fabsf(wrap_pi(heading_of(b.f) - heading_of(a.f))) / 14.0f;
      bool br = ((a.flags | b.flags) & TF_BRANCH) != 0;
      if (!br && cv > curv) curv = cv;
      if (cv > 1e-4f) vroute = fminf(vroute, sqrtf((br ? 26.0f : 32.0f) / cv));
    }
  }
  float vmax = routed ? vroute : curv > 1e-4f ? sqrtf(32.0f / curv) : 99;
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
    uint32_t fl = routed ? route_at(s, k, rb, rbs, (float)c * 6.0f).flags : t->s[(k->loc.i + c * 6) % t->n].flags;
    if (fl & (TF_GAP | TF_RAMP | TF_LOOP)) in.thr = 127;
  }
  /* drift through the sharp ones (the skilled; not on a narrow branch) */
  if (m->skill >= 1 && !on_branch && curv > 0.012f && spd > 22 && fabsf(ang) > 0.15f && !(here.flags & (TF_LOOP | TF_GAP | TF_RAMP))) in.btn |= KB_DRIFT;
  /* a Shockwave is coming: the skilled hop it */
  if (m->skill >= 1 && k->shock_warn > 0 && k->shock_warn <= 9 + 2 * (2 - m->skill)) in.btn |= KB_DRIFT;
  /* stuck: back out */
  if (fabsf(spd) < 1.5f && s->phase == PH_RACE) m->stuck++;
  else m->stuck = 0;
  if (m->stuck > 90) m->unstick = 60, m->stuck = 0;
  if (m->unstick > 0) {
    m->unstick--;
    in.thr = -127;
    in.steer = (int8_t)(-in.steer);
  }
  /* the skilled hold an oil can or a mine out behind as a shield, and let it go when a kart closes in behind */
  if ((k->item == IT_OIL || k->item == IT_MINE) && k->item_wait == 0 && m->skill >= 1 && s->phase == PH_RACE) {
    bool close = false;
    for (int j = 0; j < MAXK && !close; j++) {
      if (j == me || !s->k[j].on) continue;
      float ds = k->loc.s - s->k[j].loc.s;
      close = ds > 3 && ds < 12;
    }
    if (!close || !k->trailing) in.btn |= KB_ITEM;
    return in;
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
      for (int j = 0; j < MAXK && !on_branch; j++) {
        if (j == me || !s->k[j].on) continue;
        float ds = s->k[j].loc.ms - k->loc.ms;
        if (ds > 6 && ds < 90 && (k->item == IT_HOMING || fabsf(s->k[j].loc.lat - k->loc.lat) < 3.0f)) use = true;
      }
      break;
    case IT_OIL:
    case IT_MINE:
      for (int j = 0; j < MAXK && !on_branch; j++) {
        if (j == me || !s->k[j].on) continue;
        float ds = k->loc.ms - s->k[j].loc.ms;
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
