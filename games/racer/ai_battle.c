/* racer's CPU drivers in a battle: no road to follow, so they pick a place to go and drive at it.
 *
 *   no item      the nearest box that is there (a short way, not behind a wall it would have to go round)
 *   an item      the nearest kart worth hitting, ahead first; fire when the shot is lined up and clear
 *   always       three feelers (sim_ray) steer it off walls and cover, it swerves round mines and oil, and a kart
 *                that has not moved for a moment backs out the way it came, turning
 *
 * Everything it reads is the simulation and a seeded stream of its own, so a battle repeats. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "cartwheel/math.h"
#include "game.h"

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static uint32_t rnd(ai_mem *m) {
  m->rng ^= m->rng << 13;
  m->rng ^= m->rng >> 17;
  m->rng ^= m->rng << 5;
  return m->rng;
}

/* the angle (radians, + right) from kart k's heading to a point, seen from above */
static float bearing(const kart *k, t3_vec3 at) {
  t3_vec3 d = v3sub(at, k->p), f = kart_fwd(k), r = kart_right(k);
  return cw_atan2(v3dot(d, r), v3dot(d, f));
}
static float flat_dist(t3_vec3 a, t3_vec3 b) {
  float dx = a.x - b.x, dz = a.z - b.z;
  return sqrtf(dx * dx + dz * dz);
}

/* a clear line between two points at kart height (nothing static in the way) */
static bool clear_line(const sim *s, t3_vec3 a, t3_vec3 b) {
  t3_vec3 from = v3add(a, (t3_vec3){ 0, 0.5f, 0 }), to = v3add(b, (t3_vec3){ 0, 0.5f, 0 });
  float fr = 1;
  t3_vec3 n;
  return !sim_ray(s, from, v3sub(to, from), &fr, &n) || fabsf(n.y) > 0.6f;
}

/* a feeler: how much of `len` metres along the kart's heading turned by `ang` is free of walls (1 = all) */
static float feel(const sim *s, const kart *k, float ang, float len) {
  float sn, cs;
  cw_sincos(ang, &sn, &cs);
  t3_vec3 f = kart_fwd(k), r = kart_right(k);
  f.y = 0, r.y = 0;
  t3_vec3 dir = v3norm(v3add(v3mul(f, cs), v3mul(r, sn)));
  t3_vec3 from = v3add(k->p, (t3_vec3){ 0, 0.15f, 0 });
  float fr = 1;
  t3_vec3 n;
  if (!sim_ray(s, from, v3mul(dir, len), &fr, &n)) return 1;
  if (n.y > 0.6f) return 1; /* a ramp or a deck to drive up */
  return fr;
}

static int pick_box(const sim *s, int me) {
  const kart *k = &s->k[me];
  int best = -1;
  float bs = 1e9f;
  for (int b = 0; b < s->nbox; b++) {
    const itembox *x = &s->box[b];
    if (x->cool > 0) continue;
    float d = flat_dist(x->p, k->p), a = fabsf(bearing(k, x->p));
    float score = d + a * 9.0f + (fabsf(x->p.y - k->p.y) > 2.0f ? 18.0f : 0);
    if (!clear_line(s, k->p, x->p)) score += 40;
    /* another kart is nearer to it and heading there: leave it */
    for (int j = 0; j < MAXK; j++) {
      if (j == me || !s->k[j].on || s->k[j].out) continue;
      if (flat_dist(s->k[j].p, x->p) < d * 0.5f) score += 15;
    }
    if (score < bs) bs = score, best = b;
  }
  return best;
}

static int pick_target(const sim *s, int me, ai_mem *m) {
  const kart *k = &s->k[me];
  int best = -1;
  float bs = 1e9f;
  for (int j = 0; j < MAXK; j++) {
    const kart *o = &s->k[j];
    if (j == me || !o->on || o->out || o->respawn_ticks > 0) continue;
    float d = flat_dist(o->p, k->p), a = fabsf(bearing(k, o->p));
    float score = d + a * 14.0f;
    if (j == m->b_target) score -= 12; /* keep after the one it is chasing */
    if (o->balloons == 1) score -= 6;   /* one more pop finishes it */
    if (score < bs) bs = score, best = j;
  }
  return best;
}

/* a goal up on a deck is reached by a ramp: on one (or lined up at its foot) aim off its top, else go to the foot of the
 * one that makes the shortest way */
static t3_vec3 via_ramp(const sim *s, const kart *k, t3_vec3 goal, float goal_lv) {
  const arena_rt *a = s->arena;
  int best = -1;
  float bc = 1e9f;
  for (int i = 0; i < a->n; i++) {
    const arena_prim *p = &a->prim[i];
    if (p->kind != AP_RAMP || p->ghost || p->h < 1.8f || fabsf(p->y + p->h - goal_lv) > 0.8f) continue;
    float lx, lz;
    arena_prim_local(a, i, k->p, &lx, &lz);
    if (fabsf(lx) < p->w * 0.5f - 0.6f && lz > -p->d * 0.5f - 9.0f && lz < p->d * 0.5f + 1.0f) return arena_prim_point(a, i, lx * 0.5f, p->h, p->d * 0.5f + 6.0f);
    t3_vec3 foot = arena_prim_point(a, i, 0, 0, -p->d * 0.5f - 7.0f), top = arena_prim_point(a, i, 0, p->h, p->d * 0.5f);
    float c = flat_dist(k->p, foot) + flat_dist(top, goal);
    if (c < bc) bc = c, best = i;
  }
  if (best < 0) return goal;
  return arena_prim_point(a, best, 0, 0, -a->prim[best].d * 0.5f - 7.0f);
}

/* the free way out: rays fanned round the line to the goal; the longest clear one that heads most nearly there, a point along it */
static t3_vec3 pick_detour(const sim *s, const kart *k, t3_vec3 goal) {
  float base = cw_atan2(goal.x - k->p.x, goal.z - k->p.z), best = -1e9f;
  t3_vec3 at = goal;
  for (int i = -4; i <= 4; i++) {
    float sn, cs;
    cw_sincos(base + (float)i * 0.42f, &sn, &cs);
    t3_vec3 dir = { sn, 0, cs };
    float fr = 1;
    t3_vec3 n;
    float free = 18.0f;
    if (sim_ray(s, v3add(k->p, (t3_vec3){ 0, 0.15f, 0 }), v3mul(dir, 18.0f), &fr, &n) && n.y < 0.6f) free = 18.0f * fr;
    float score = free - fabsf((float)i) * 1.8f;
    if (score > best) best = score, at = v3add(k->p, v3mul(dir, clampf(free - 2.5f, 3.0f, 12.0f)));
  }
  return at;
}

kin ai_battle_think(const sim *s, int me, ai_mem *m) {
  const kart *k = &s->k[me];
  kin in = { 0 };
  if (!k->on || k->out) return in;
  if (s->phase == PH_COUNT) {
    int when = m->skill == 2 ? 163 : m->skill == 1 ? 168 : 150 + (int)(m->rng % 40u);
    in.thr = s->phase_ticks >= when ? 127 : 0;
    return in;
  }
  if (s->phase != PH_RACE) {
    in.thr = 40;
    return in;
  }
  bool armed = k->item != IT_NONE && k->item_wait == 0;
  bool offensive = armed && (k->item == IT_MISSILE || k->item == IT_HOMING || k->item == IT_TURBO || k->item == IT_TURBO3 || k->item == IT_SHOCK);
  /* rethink the goal now and then */
  if (--m->b_think <= 0) {
    m->b_think = 20 + (int)(rnd(m) % 20u);
    m->b_target = offensive ? pick_target(s, me, m) : -1;
    m->b_box = (!armed && k->item_wait == 0) ? pick_box(s, me) : -1;
    if (m->b_target < 0 && m->b_box < 0) {
      /* nothing to do: roam towards the far side, or after the nearest kart */
      m->b_target = pick_target(s, me, m);
      if (m->b_target < 0 || armed) {
        const arena_rt *a = s->arena;
        int sp = a && a->nspawn ? (int)(rnd(m) % (uint32_t)a->nspawn) : 0;
        m->b_goal = a && a->nspawn ? (t3_vec3){ a->spawn[sp][0] * 0.6f, 0, (a->spawn[sp][1] - ARENA_CZ) * 0.6f + ARENA_CZ } : (t3_vec3){ 0, 0, ARENA_CZ };
        if (!armed) m->b_target = -1;
      }
    }
  }
  if (m->b_box >= 0 && s->box[m->b_box].cool > 0) m->b_box = -1, m->b_think = 0;
  if (m->b_target >= 0 && (s->k[m->b_target].out || !s->k[m->b_target].on)) m->b_target = -1, m->b_think = 0;
  t3_vec3 goal = m->b_goal;
  if (m->b_roam > 0) m->b_roam--; /* fed up with the last goal: wander to the roaming point a while */
  else if (m->b_box >= 0) goal = s->box[m->b_box].p;
  else if (m->b_target >= 0) {
    /* lead the target a little */
    const kart *o = &s->k[m->b_target];
    float t = clampf(flat_dist(o->p, k->p) / 45.0f, 0, 0.8f);
    goal = v3add(o->p, v3mul(o->v, t));
  }
  if (s->arena) {
    float my_lv = arena_level(s->arena, k->p.x, k->p.z), goal_lv = arena_level(s->arena, goal.x, goal.z);
    if (goal_lv > my_lv + 1.5f) goal = via_ramp(s, k, goal, goal_lv);
  }
  float ang = bearing(k, goal), dist = flat_dist(goal, k->p);
  /* no nearer to it for a few seconds (a wall between, a kart that keeps away): give up on it for a while */
  if (dist < m->b_best_d - 1.0f || m->b_best_d <= 0) m->b_best_d = dist, m->b_frust = 0;
  else if (++m->b_frust > 180) {
    /* the open way round, or now and then somewhere else altogether */
    const arena_rt *a = s->arena;
    int sp = a && a->nspawn ? (int)(rnd(m) % (uint32_t)a->nspawn) : 0;
    if (rnd(m) % 3u && m->b_roam == 0) m->b_goal = pick_detour(s, k, goal), m->b_roam = 80;
    else m->b_goal = a && a->nspawn ? (t3_vec3){ a->spawn[sp][0] * 0.55f, 0, (a->spawn[sp][1] - ARENA_CZ) * 0.55f + ARENA_CZ } : (t3_vec3){ 0, 0, ARENA_CZ }, m->b_roam = 120, m->b_box = -1;
    m->b_frust = 0, m->b_best_d = 0;
  }
  /* mines and oil in the way: aim to the side of them */
  for (int h = 0; h < MAXHZ; h++) {
    const hazard *z = &s->hz[h];
    if (z->kind != HZ_MINE && z->kind != HZ_OIL) continue;
    float zd = flat_dist(z->p, k->p);
    if (zd > 16 || zd > dist) continue;
    float za = bearing(k, z->p);
    if (fabsf(za - ang) < 0.35f) ang += (za > ang ? -1.0f : 1.0f) * (0.5f - fabsf(za - ang));
  }
  /* the feelers */
  float spd = k->speed, len = 6.0f + clampf(spd, 0, 40) * 0.45f;
  float fc = feel(s, k, 0, len), fl = feel(s, k, -0.5f, len * 0.8f), fr = feel(s, k, 0.5f, len * 0.8f);
  m->b_aim = goal, m->b_feel[0] = fl, m->b_feel[1] = fc, m->b_feel[2] = fr;
  float steer = clampf(ang * 2.0f, -1, 1);
  if (fc < 1.0f || fl < 0.6f || fr < 0.6f) {
    float push = (1.0f - fc) * 1.6f;
    float side = fr > fl ? 1.0f : fl > fr ? -1.0f : (m->b_side ? (float)m->b_side : 1.0f);
    if (fc < 1.0f) m->b_side = side > 0 ? 1 : -1;
    steer = clampf(steer + side * push + (fl < 0.6f ? (0.6f - fl) * 1.5f : 0) - (fr < 0.6f ? (0.6f - fr) * 1.5f : 0), -1, 1);
  }
  m->steer_smooth += (steer - m->steer_smooth) * 0.4f;
  in.steer = (int8_t)(clampf(m->steer_smooth, -1, 1) * 127);
  in.thr = 127;
  /* a tight turn: off the gas, the skilled drift through it (a drift boost is a ram waiting to happen) */
  if (fabsf(ang) > 1.4f && spd > 14) in.thr = 0;
  if (m->skill >= 1 && fabsf(ang) > 0.55f && fabsf(ang) < 1.6f && spd > 17 && fc > 0.7f) in.btn |= KB_DRIFT;
  /* close to the goal but off to the side: slow right down so it turns tight instead of circling it */
  if (m->b_box >= 0 && m->b_roam == 0 && dist < 11 && fabsf(ang) > 0.7f) in.thr = spd > 7 ? -127 : 80;
  /* something solid close ahead for this speed: brake while it steers round it */
  if (fc < 1.0f && fc * len < spd * 0.42f && spd > 8) in.thr = -127, in.btn &= ~KB_DRIFT;
  /* stuck on something: back out, turned the other way, then try again */
  float moved = flat_dist(k->p, m->b_last_p);
  m->b_last_p = k->p;
  if (moved < 0.03f && k->spin_ticks == 0 && k->respawn_ticks == 0) m->b_slow++;
  else if (m->b_slow > 0) m->b_slow -= 2;
  if (m->b_slow < 0) m->b_slow = 0;
  /* a wall close ahead and the goal off to the side: back and turn (the steering turns the nose the same way in reverse) */
  bool boxed = fc < 1.0f && fc * len < 4.5f && fabsf(ang) > 1.0f && spd < 9;
  if (m->b_commit > 0) m->b_commit--;
  if ((m->b_slow > 35 || (fc < 0.25f && spd < 5) || boxed) && m->b_commit == 0 && m->b_reverse == 0) {
    m->b_reverse = 75;
    m->b_slow = 0;
    m->b_side = ang > 0 ? 1 : -1;
    m->b_rev_p = k->p;
  }
  if (m->b_reverse > 0) {
    /* back out until it has come away three and a half metres (or a second has gone), the nose swinging to the goal, then drive on a while */
    m->b_reverse--;
    in.thr = -127;
    in.steer = (int8_t)(m->b_side * 90);
    in.btn &= ~KB_DRIFT;
    /* something behind it (a wall, a kart): stop backing */
    bool behind = false;
    {
      t3_vec3 back = kart_fwd(k);
      back.y = 0;
      back = v3mul(v3norm(back), -3.2f);
      float bf = 1;
      t3_vec3 bn;
      if (sim_ray(s, v3add(k->p, (t3_vec3){ 0, 0.15f, 0 }), back, &bf, &bn) && bn.y < 0.6f) behind = true;
      for (int j = 0; j < MAXK && !behind; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->parked) continue;
        t3_vec3 d = v3sub(o->p, k->p);
        behind = v3dot(d, d) < 4.0f * 4.0f && v3dot(d, kart_fwd(k)) < -0.5f;
      }
    }
    if (flat_dist(k->p, m->b_rev_p) > 3.5f || m->b_reverse == 0 || (behind && m->b_reverse < 68) || (m->b_slow > 24 && m->b_reverse < 50)) {
      m->b_reverse = 0, m->b_commit = 45, in.thr = 0;
      /* and then the open way that heads most nearly where it wants to go */
      m->b_goal = pick_detour(s, k, goal), m->b_roam = 70, m->b_best_d = 0;
    }
  }

  /* a Shockwave is coming: hop it */
  if (m->skill >= 1 && k->shock_warn > 0 && k->shock_warn <= 9 + 2 * (2 - m->skill)) in.btn |= KB_DRIFT;
  /* items */
  if (armed) {
    bool use = false;
    m->b_hold++;
    int tgt = m->b_target;
    switch (k->item) {
    case IT_MISSILE:
      for (int j = 0; j < MAXK && !use; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out || o->respawn_ticks > 0) continue;
        float d = flat_dist(o->p, k->p), a = bearing(k, v3add(o->p, v3mul(o->v, d / 50.0f)));
        if (d < 55 && fabsf(a) < 0.09f + 1.2f / (d + 8.0f) && clear_line(s, k->p, o->p)) use = true;
      }
      break;
    case IT_HOMING:
      for (int j = 0; j < MAXK && !use; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out || o->respawn_ticks > 0) continue;
        float d = flat_dist(o->p, k->p);
        if (d < 70 && fabsf(bearing(k, o->p)) < 0.7f && clear_line(s, k->p, o->p)) use = true;
      }
      break;
    case IT_OIL:
    case IT_MINE:
      for (int j = 0; j < MAXK; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out) continue;
        if (flat_dist(o->p, k->p) < 24 && fabsf(bearing(k, o->p)) > 2.2f) use = true;
      }
      if (m->b_hold > 9 * TICK_HZ) use = true; /* lay it somewhere */
      break;
    case IT_SHIELD:
      for (int h = 0; h < MAXHZ; h++) {
        const hazard *z = &s->hz[h];
        if ((z->kind == HZ_MISSILE || z->kind == HZ_HOMING) && z->owner != me && flat_dist(z->p, k->p) < 26) use = true;
      }
      if (m->b_hold > 5 * TICK_HZ) use = true;
      break;
    case IT_TURBO:
    case IT_TURBO3:
      /* a ram: the target close ahead and lined up */
      if (tgt >= 0 && flat_dist(s->k[tgt].p, k->p) < 26 && fabsf(bearing(k, s->k[tgt].p)) < 0.18f && clear_line(s, k->p, s->k[tgt].p)) use = true;
      if (m->b_hold > 8 * TICK_HZ && fc > 0.9f) use = true;
      break;
    case IT_SHOCK:
      for (int j = 0; j < MAXK; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out) continue;
        if (flat_dist(o->p, k->p) < 24) use = true;
      }
      break;
    default: break;
    }
    /* the rookies are slower on the trigger */
    int wait = m->skill == 2 ? 6 : m->skill == 1 ? 14 : 30;
    if (use && m->b_hold > wait && k->item_cool == 0 && k->spin_ticks == 0) {
      in.btn |= KB_ITEM;
      m->b_hold = 0;
      m->b_think = 0;
    }
  } else m->b_hold = 0;
  return in;
}
