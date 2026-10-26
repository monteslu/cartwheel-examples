/* racer's CPU drivers in a battle: no road to follow, so they pick a place to go and drive at it.
 *
 *   no item      the nearest box that is there (a short way, not behind a wall it would have to go round)
 *   an item      the nearest kart worth hitting, ahead first; fire when the shot is lined up and clear
 *   always       a steering field of nine rays (sim_ray) picks the open heading nearest the goal, wary of mines, oil
 *                and other karts; it brakes when the way ends too soon, and a kart pinned or nosed into a corner
 *                backs out briefly with the nose swinging to the open side, then drives on
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

static int pick_box(const sim *s, int me, const ai_mem *m) {
  const kart *k = &s->k[me];
  int best = -1;
  float bs = 1e9f;
  for (int b = 0; b < s->nbox; b++) {
    const itembox *x = &s->box[b];
    if (x->cool > 0) continue;
    if (m->lane_ticks > 0 && (int)m->lane_want == b) continue; /* it just got stuck going for this one */
    float d = flat_dist(x->p, k->p), a = fabsf(bearing(k, x->p));
    float score = d + a * 16.0f + (fabsf(x->p.y - k->p.y) > 2.0f ? 18.0f : 0);
    if (!clear_line(s, k->p, x->p)) score += 40;
    /* another kart is nearer to it and heading there: leave it */
    for (int j = 0; j < MAXK; j++) {
      if (j == me || !s->k[j].on || s->k[j].out) continue;
      if (flat_dist(s->k[j].p, x->p) < d * 0.5f) score += 15;
    }
    if (b == m->b_box) score *= 0.6f; /* stay with the box it is going for: flipping between two is how a driver dithers */
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
    float score = d + a * 20.0f;
    if (j == m->b_target) score -= 12; /* keep after the one it is chasing */
    if (o->balloons == 1) score -= 6;   /* one more pop finishes it */
    /* one in a scrum is a poor mark: the pile-up stops everyone who joins it */
    for (int q = 0; q < MAXK; q++)
      if (q != j && q != me && s->k[q].on && !s->k[q].out && flat_dist(s->k[q].p, o->p) < 5.0f) score += 12;
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
    m->b_box = (!armed && k->item_wait == 0) ? pick_box(s, me, m) : -1;
    if (m->b_target < 0 && m->b_box < 0) {
      /* nothing to do: roam towards the far side, or after the nearest kart */
      m->b_target = pick_target(s, me, m);
      if (m->b_target < 0 || armed) {
        const arena_rt *a = s->arena;
        int sp = a && a->nspawn ? (int)(rnd(m) % (uint32_t)a->nspawn) : 0;
        if (flat_dist(k->p, m->b_goal) < 12.0f || m->b_goal.z == 0) m->b_goal = a && a->nspawn ? (t3_vec3){ a->spawn[sp][0] * 0.6f, 0, (a->spawn[sp][1] - ARENA_CZ) * 0.6f + ARENA_CZ } : (t3_vec3){ 0, 0, ARENA_CZ };
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
  /* the steering field: nine headings fanned round the kart's own. Each scores how nearly it heads for the goal, how far
   * it is clear of walls and cover (rays into the static world; ramps and decks are floor), and whether a mine, oil or
   * another kart lies along it; the best one is steered for, with a little loyalty to the last choice so it does not
   * dither. A concave corner never wins: its rays are short long before the kart gets there. */
  float spd = k->speed, look = clampf(9.0f + spd * 0.7f, 10.0f, 28.0f);
  t3_vec3 fw = kart_fwd(k), rt = kart_right(k);
  fw.y = 0, rt.y = 0;
  fw = v3norm(fw), rt = v3norm(rt);
  static const float FAN[9] = { -1.75f, -1.2f, -0.75f, -0.38f, 0, 0.38f, 0.75f, 1.2f, 1.75f };
  float clear[9], best = -1e9f, best_a = 0;
  int best_c = 4;
  t3_vec3 from = v3add(k->p, (t3_vec3){ 0, 0.15f, 0 });
  for (int c = 0; c < 9; c++) {
    float a = FAN[c], sn, cs;
    cw_sincos(a, &sn, &cs);
    t3_vec3 dir = v3add(v3mul(fw, cs), v3mul(rt, sn));
    float fr = 1;
    t3_vec3 n;
    clear[c] = sim_ray(s, from, v3mul(dir, look), &fr, &n) && n.y < 0.6f ? fr : 1.0f;
    float free_m = clear[c] * look;
    float score = cw_cos(a - ang) * 2.0f + clampf(free_m / 12.0f, 0, 1) * 1.6f - (free_m < 4.0f ? 2.0f : 0);
    /* the goal itself is nearer than the wall that way: the wall does not matter */
    if (free_m >= dist && fabsf(a - ang) < 0.4f) score += 0.8f;
    for (int h = 0; h < MAXHZ; h++) {
      const hazard *z = &s->hz[h];
      if (z->kind != HZ_MINE && z->kind != HZ_OIL) continue;
      t3_vec3 d = v3sub(z->p, k->p);
      float along = d.x * dir.x + d.z * dir.z, side = fabsf(d.x * dir.z - d.z * dir.x);
      if (along > 0 && along < fminf(free_m, 18.0f) && side < 2.4f) score -= 1.6f;
    }
    for (int j = 0; j < MAXK; j++) {
      const kart *o = &s->k[j];
      if (j == me || j == m->b_target || !o->on || o->out) continue;
      t3_vec3 d = v3sub(o->p, k->p);
      float along = d.x * dir.x + d.z * dir.z, side = fabsf(d.x * dir.z - d.z * dir.x);
      if (along > 0 && along < 9.0f && side < 1.8f) score -= 0.7f;
    }
    score -= fabsf(a - m->lane) * 0.22f;
    if (score > best) best = score, best_a = a, best_c = c;
  }
  m->lane = best_a;
  float fc = clear[4], ahead_m = fminf(clear[3], fminf(clear[4], clear[5])) * look;
  m->b_aim = goal, m->b_feel[0] = clear[2], m->b_feel[1] = fc, m->b_feel[2] = clear[6];
  /* from a crawl full lock scrubs the speed away: wind the lock on as it gets going, unless a wall leaves no room for a wider turn */
  float lock = ahead_m < 7.0f ? 1.0f : 0.55f + 0.45f * clampf(spd / 12.0f, 0, 1);
  float steer = clampf(best_a * 1.8f, -lock, lock);
  m->steer_smooth += (steer - m->steer_smooth) * 0.5f;
  in.steer = (int8_t)(clampf(m->steer_smooth, -1, 1) * 127);
  in.thr = 127;
  /* a hard turn at speed: lift; the skilled drift through it (a drift boost is a ram waiting to happen) */
  if (fabsf(best_a) > 1.3f && spd > 22) in.thr = 0, in.btn |= KB_DRIFT; /* lift and slide it round */
  if (m->skill >= 1 && fabsf(best_a) > 0.45f && fabsf(best_a) < 1.3f && spd > 17 && fc > 0.6f) in.btn |= KB_DRIFT;
  /* close to a box but off to the side: slow right down so it turns tight instead of circling it */
  if (m->b_box >= 0 && m->b_roam == 0 && dist < 7 && fabsf(ang) > 1.2f && spd > 14) in.thr = 0;
  /* the way it is going ends sooner than it can turn: brake */
  if (clear[best_c] * look < spd * 0.3f && spd > 14) in.thr = -127, in.btn &= ~KB_DRIFT;
  /* stuck (pinned on something, or nose into a wall with nowhere to turn): a short committed back-out, the nose swinging to
   * the open side (the steering turns the nose the same way in reverse), then a moment driving on before it may again */
  /* stuck: it has not got 1.2 m from where it last was for most of a second (pinned, or rocking on an edge) */
  if (flat_dist(k->p, m->b_last_p) > 1.2f || m->b_reverse > 0 || k->spin_ticks > 0 || k->respawn_ticks > 0) m->b_last_p = k->p, m->b_slow = 0;
  else m->b_slow++;
  if (m->b_commit > 0) m->b_commit--;
  if (m->lane_ticks > 0) m->lane_ticks--;
  /* nose to a wall: if there is room to the side it means to turn to, it turns there going forward; only boxed in does it back out */
  float side_m = (best_a > 0 ? fminf(clear[6], clear[7]) : fminf(clear[1], clear[2])) * look;
  bool walled = spd < 4 && fabsf(best_a) > 0.5f && ahead_m < 1.6f && side_m < 2.5f; /* the crawl turn-in (battle handling) swings it round in place otherwise */
  /* another kart nose to nose with it, both crawling: one of them has to give way */
  for (int j = 0; j < MAXK && !walled; j++) {
    const kart *o = &s->k[j];
    if (j == me || !o->on || o->out || spd > 3) continue;
    t3_vec3 d = v3sub(o->p, k->p);
    float dd = d.x * d.x + d.z * d.z, along = d.x * fw.x + d.z * fw.z;
    walled = dd < 3.4f * 3.4f && along > 0.7f * sqrtf(dd) && m->b_slow > 20;
  }
  if ((m->b_slow > 50 || walled) && (m->b_commit == 0 || m->b_slow > 70) && m->b_reverse == 0) {
    /* a length of its own (two karts stuck on each other do not mirror each other for ever) */
    m->b_reverse = 60 + (int)(rnd(m) % 16u);
    m->b_slow = 0;
    m->b_side = best_a > 0.05f ? 1 : best_a < -0.05f ? -1 : (rnd(m) & 1 ? 1 : -1);
    m->b_rev_p = k->p;
    if (m->b_box >= 0) m->lane_want = (float)m->b_box, m->lane_ticks = 180, m->b_box = -1, m->b_think = 0;
  }
  if (m->b_reverse > 0 && k->spin_ticks > 0) m->b_reverse = 0; /* a spin takes the wheel: start afresh after it */
  if (m->b_reverse > 0) {
    m->b_reverse--;
    in.thr = -127;
    in.steer = (int8_t)(m->b_side * 127);
    in.btn &= ~KB_DRIFT;
    bool behind = false;
    {
      t3_vec3 back = v3mul(fw, -2.6f);
      float bf = 1;
      t3_vec3 bn;
      if (sim_ray(s, from, back, &bf, &bn) && bn.y < 0.6f) behind = true;
      for (int j = 0; j < MAXK && !behind; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->parked) continue;
        t3_vec3 d = v3sub(o->p, k->p);
        behind = v3dot(d, d) < 3.5f * 3.5f && v3dot(d, fw) < -0.5f;
      }
    }
    /* until the nose has come round to the goal (or there is room ahead to turn in), a second at most */
    float back_m = flat_dist(k->p, m->b_rev_p);
    bool round = back_m > 1.2f && (fabsf(ang) < 0.8f || ahead_m > 9.0f);
    if (round || back_m > 6.0f || m->b_reverse == 0 || (behind && m->b_reverse < 54)) m->b_reverse = 0, m->b_commit = 30, in.thr = 127;
  }

  m->b_thr = in.thr;
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
