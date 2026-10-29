/* racer's CPU drivers in a battle: no road to follow, so they pick a place to go and drive at it.
 *
 *   no item      the nearest box that is there (a short way, not behind a wall it would have to go round)
 *   an item      the nearest kart worth hitting, ahead first; fire when the shot is lined up and clear
 *   always       a steering field of nine rays (sim_ray) picks the open heading nearest the goal, wary of mines, oil
 *                and other karts; it brakes when the way ends too soon, and a kart pinned or nosed into a corner
 *                backs out briefly with the nose swinging to the open side, then drives on. A goal on another level
 *                goes by a ramp up to it, or by a teleport pad when no ramp does (a tower's roof, and back down)
 *
 * The other arena rounds give it an objective (mode_goal) that stands in for roaming, and it only turns aside for a box or
 * a kart close by:
 *   capture the flag     a carrier heads home; its own flag taken, the guards and the nearest chase the carrier, its own
 *                        flag lying out, the nearest sends it home; attackers go for the other flag (or escort their
 *                        carrier home), racing: a turbo goes on a long clear run at the goal; a guard idles round a
 *                        post just out in front of its base and goes for whoever comes near. Teammates are never
 *                        targets
 *   king of the mountain onto the hill and round it, fighting whoever else is on it
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

/* the goal b in sight from a: clear, or only something within a few metres of b in the way (the goal's own solid: a target,
 * an altar in the middle of its zone, which otherwise no point of the route ever has in sight) */
static bool sees_goal(const sim *s, t3_vec3 a, t3_vec3 b) {
  t3_vec3 from = v3add(a, (t3_vec3){ 0, 0.5f, 0 }), to = v3add(b, (t3_vec3){ 0, 0.5f, 0 }), d = v3sub(to, from);
  float fr = 1;
  t3_vec3 n;
  if (!sim_ray(s, from, d, &fr, &n) || fabsf(n.y) > 0.6f) return true;
  /* (only on the goal's own level: a wall just short of a ramp's foot a level up is a wall in the way) */
  return fabsf(a.y - b.y) < 2.0f && (1.0f - fr) * v3len(d) < 4.0f;
}

/* the box to go for (no farther than max_d), -1 none */
static int pick_box(const sim *s, int me, const ai_mem *m, float max_d) {
  const kart *k = &s->k[me];
  int best = -1;
  float bs = 1e9f;
  for (int b = 0; b < s->nbox; b++) {
    const itembox *x = &s->box[b];
    if (x->cool > 0) continue;
    if (m->lane_ticks > 0 && (int)m->lane_want == b) continue; /* it just got stuck going for this one */
    float d = flat_dist(x->p, k->p), a = fabsf(bearing(k, x->p));
    if (d > max_d) continue;
    if (s->amode != AM_BALLOON && fabsf(x->p.y - k->p.y) > 3.0f) continue; /* a round with a goal: not up a tower for a box */
    if (s->amode == AM_CTF && a > 1.0f) continue; /* capture the flag: a box on the way, never one to turn round for */
    float score = d + a * 16.0f + (fabsf(x->p.y - k->p.y) > 2.0f ? 18.0f : 0);
    if (!clear_line(s, k->p, x->p)) score += 40;
    /* another kart is nearer to it and heading there: leave it */
    for (int j = 0; j < MAXK; j++) {
      if (j == me || !s->k[j].on || s->k[j].out) continue;
      if (flat_dist(s->k[j].p, x->p) < d * 0.5f) score += 15;
    }
    if (s->arena) {
      /* a box tucked into a corner costs a stop to turn out again: worth it only if nothing better is near */
      float ex = s->arena->half_x - fabsf(x->p.x), ez = s->arena->half_z - fabsf(x->p.z - ARENA_CZ);
      if (ex < 16 && ez < 16) score += 25;
      else if (ex < 10 || ez < 10) score += 10;
    }
    if (b == m->b_box) score *= 0.6f; /* stay with the box it is going for: flipping between two is how a driver dithers */
    if (score < bs) bs = score, best = b;
  }
  return best;
}

/* the kart to go after (no farther than max_d, never a teammate), -1 none */
static int pick_target(const sim *s, int me, ai_mem *m, float max_d) {
  const kart *k = &s->k[me];
  int best = -1;
  float bs = 1e9f;
  for (int j = 0; j < MAXK; j++) {
    const kart *o = &s->k[j];
    if (j == me || !o->on || o->out || o->respawn_ticks > 0 || !kart_foe(s, me, j)) continue;
    float d = flat_dist(o->p, k->p), a = fabsf(bearing(k, o->p));
    if (d > max_d) continue;
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
 * one that makes the shortest way (*found false: no ramp goes up there) */
static t3_vec3 via_ramp(const sim *s, const kart *k, t3_vec3 goal, float goal_lv, bool *found) {
  const arena_rt *a = s->arena;
  int best = -1;
  float bc = 1e9f;
  for (int i = 0; i < a->n; i++) {
    const arena_prim *p = &a->prim[i];
    if (p->kind != AP_RAMP || p->ghost || p->h < 1.8f || fabsf(p->y + p->h - goal_lv) > 0.8f) continue;
    float lx, lz;
    arena_prim_local(a, i, k->p, &lx, &lz);
    if (fabsf(lx) < p->w * 0.5f - 0.6f && lz > -p->d * 0.5f - 9.0f && lz < p->d * 0.5f + 1.0f) {
      *found = true;
      return arena_prim_point(a, i, lx * 0.5f, p->h, p->d * 0.5f + 6.0f);
    }
    t3_vec3 foot = arena_prim_point(a, i, 0, 0, -p->d * 0.5f - 7.0f), top = arena_prim_point(a, i, 0, p->h, p->d * 0.5f);
    float c = flat_dist(k->p, foot) + flat_dist(top, goal);
    if (c < bc) bc = c, best = i;
  }
  *found = best >= 0;
  if (best < 0) return goal;
  return arena_prim_point(a, best, 0, 0, -a->prim[best].d * 0.5f - 7.0f);
}

/* a goal on another level that no ramp reaches (a tower's roof, or the floor from a walled roof): the teleport pad on
 * this level whose far end is nearest the goal's level and makes the shortest way, driven onto along its way in (lined
 * up 14 m out first). False when no pad helps */
static bool via_tele(const sim *s, const kart *k, float my_lv, t3_vec3 goal, float goal_lv, t3_vec3 *at) {
  const arena_rt *a = s->arena;
  int best = -1;
  float bc = 1e9f;
  for (int i = 0; i < a->ntele; i++) {
    const struct arena_tele_rt *t = &a->tele[i];
    if (fabsf(t->at.y - my_lv) > 1.5f || fabsf(t->to.y - goal_lv) > fabsf(my_lv - goal_lv) - 1.0f) continue;
    float c = flat_dist(k->p, t->at) + flat_dist(t->to, goal);
    if (c < bc) bc = c, best = i;
  }
  if (best < 0) return false;
  const struct arena_tele_rt *t = &a->tele[best];
  float sn, cs;
  cw_sincos(t->in_yaw, &sn, &cs);
  float dx = k->p.x - t->at.x, dz = k->p.z - t->at.z, along = dx * sn + dz * cs, side = fabsf(dx * cs - dz * sn);
  /* in the funnel in front of it: straight on; otherwise to the mouth of the funnel */
  if (along < 0.5f && along > -18.0f && side < 2.0f - along * 0.2f) *at = t->at;
  else *at = (t3_vec3){ t->at.x - sn * 14.0f, t->at.y, t->at.z - cs * 14.0f };
  return true;
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

/* the route's shortest distances between its points (arena_def.route, links), worked out once per arena, and again whenever
 * an assault's gates change (a link a standing gate blocks is no way through) */
static struct {
  const arena_def *def;
  int sig;
  float d[ARENA_MAXR][ARENA_MAXR];
} RT;

static t3_vec3 route_pt(const sim *s, int i);
static bool clear_line(const sim *s, t3_vec3 a, t3_vec3 b);
static void route_prep(const sim *s) {
  const arena_def *def = s->arena->def;
  int sig = 0;
  if (s->amode == AM_ASSAULT && def->assault) {
    sig = 1 + s->as_stage * 4096;
    for (int o = 0; o < def->assault->nobjs && o < 12; o++) sig += s->as_done[o] << o;
  }
  if (RT.def == def && RT.sig == sig) return;
  RT.def = def, RT.sig = sig;
  int n = def->nroute > ARENA_MAXR ? ARENA_MAXR : def->nroute;
  for (int a = 0; a < n; a++)
    for (int b = 0; b < n; b++) RT.d[a][b] = a == b ? 0 : 1e9f;
  for (int l = 0; l < def->nlinks; l++) {
    int a = def->links[l][0], b = def->links[l][1];
    if (a >= n || b >= n) continue;
    if (sig && !clear_line(s, route_pt(s, a), route_pt(s, b))) continue;
    float dx = def->route[a][0] - def->route[b][0], dz = def->route[a][1] - def->route[b][1];
    RT.d[a][b] = RT.d[b][a] = sqrtf(dx * dx + dz * dz);
  }
  for (int k = 0; k < n; k++)
    for (int a = 0; a < n; a++)
      for (int b = 0; b < n; b++)
        if (RT.d[a][k] + RT.d[k][b] < RT.d[a][b]) RT.d[a][b] = RT.d[a][k] + RT.d[k][b];
}

static t3_vec3 route_pt(const sim *s, int i) {
  const arena_def *d = s->arena->def;
  float z = d->route[i][1] + ARENA_CZ;
  return (t3_vec3){ d->route[i][0], arena_level(s->arena, d->route[i][0], z), z };
}

/* two points both on the arena's rock: one level however far apart in height (one surface, its slopes driven: arena_terrain) */
static bool same_level(const sim *s, t3_vec3 a, t3_vec3 b) {
  return s->arena->def->terrain && arena_on_terrain(s->arena, a) && arena_on_terrain(s->arena, b);
}

/* on an arena with a route: the goal itself when it is in sight, else the route's point in sight that makes the shortest way
 * to it (looked at every few ticks, kept in between) */
static t3_vec3 route_to(const sim *s, const kart *k, ai_mem *m, t3_vec3 goal) {
  const arena_def *d = s->arena->def;
  int n = d->nroute > ARENA_MAXR ? ARENA_MAXR : d->nroute;
  if (n <= 0) return goal;
  route_prep(s);
  if (--m->b_node_t > 0) return m->b_node >= 0 ? route_pt(s, m->b_node) : goal;
  m->b_node_t = 8;
  int was = m->b_node;
  m->b_node = -1;
  if ((fabsf(goal.y - k->p.y) < 3.0f || same_level(s, goal, k->p)) && sees_goal(s, k->p, goal)) return goal;
  /* the points the goal can be reached from (in sight of it), with the last stretch */
  float tail[ARENA_MAXR];
  for (int i = 0; i < n; i++) {
    t3_vec3 p = route_pt(s, i);
    tail[i] = sees_goal(s, p, goal) ? flat_dist(p, goal) : 1e9f;
  }
  float best = 1e9f;
  for (int i = 0; i < n; i++) {
    t3_vec3 p = route_pt(s, i);
    if ((fabsf(p.y - (k->p.y - 0.6f)) > 3.0f && !same_level(s, p, k->p)) || !clear_line(s, k->p, p)) continue;
    float rest = 1e9f;
    for (int j = 0; j < n; j++) rest = fminf(rest, RT.d[i][j] + tail[j]);
    float c = flat_dist(k->p, p) + rest;
    if (c < best) best = c, m->b_node = i;
  }
  if (m->b_node != was) m->b_best_d = 0; /* a new point to make for: no nearer to it yet is not being stuck */
  return m->b_node >= 0 ? route_pt(s, m->b_node) : goal;
}

/* a point going round c at radius r (each driver its own way round), for holding a place without parking on it */
static t3_vec3 orbit(const sim *s, int me, t3_vec3 c, float r) {
  float sn, cs;
  cw_sincos((float)s->tick * 0.018f + (float)me * 1.7f, &sn, &cs);
  return (t3_vec3){ c.x + sn * r, c.y, c.z + cs * r };
}

/* kart me is among the n of its team nearest to p (carriers and karts flickering back in do not count) */
static bool among_nearest(const sim *s, int me, t3_vec3 p, int n) {
  float d = flat_dist(s->k[me].p, p);
  int nearer = 0;
  for (int j = 0; j < MAXK; j++) {
    const kart *o = &s->k[j];
    if (j == me || !o->on || o->team != s->k[me].team || o->carry >= 0 || o->respawn_ticks > 0) continue;
    if (flat_dist(o->p, p) < d) nearer++;
  }
  return nearer < n;
}

/* the nearest kart of the other team within r of p, -1 none */
static int foe_near(const sim *s, int me, t3_vec3 p, float r) {
  int best = -1;
  float bd = r;
  for (int j = 0; j < MAXK; j++) {
    const kart *o = &s->k[j];
    if (j == me || !o->on || o->out || o->respawn_ticks > 0 || !kart_foe(s, me, j)) continue;
    float d = flat_dist(o->p, p);
    if (d < bd) bd = d, best = j;
  }
  return best;
}

/* assault: the objectives that count now (the attackers' stage, not done), the one this driver works on (the team spread
 * over them by kart order), -1 none */
static int as_pick(const sim *s, int me) {
  const assault_def *d = s->arena ? s->arena->def->assault : NULL;
  if (!d || s->as_break > 0) return -1;
  int live[ASSAULT_MAXO], n = 0;
  for (int o = 0; o < d->nobjs && o < ASSAULT_MAXO; o++)
    if (d->objs[o].kind != AO_TURRET && d->objs[o].stage == s->as_stage && !s->as_done[o]) live[n++] = o;
  if (n == 0) return -1;
  int before = 0;
  for (int j = 0; j < me; j++) before += s->k[j].on && s->k[j].team == s->k[me].team;
  return live[before % n];
}
static t3_vec3 as_obj_at(const sim *s, int o) {
  const assault_obj *b = &s->arena->def->assault->objs[o];
  return (t3_vec3){ b->x, b->y, b->z + ARENA_CZ };
}

/* assault: an attacker drives at its objective (a target to shoot or ram, a zone to get into and stay in), a defender
 * guards one and goes for any attacker that comes near it */
static t3_vec3 assault_goal(const sim *s, int me, int *chase, bool *busy, bool *hold) {
  const kart *k = &s->k[me];
  int o = as_pick(s, me);
  if (o < 0) return k->p;
  const assault_obj *b = &s->arena->def->assault->objs[o];
  t3_vec3 at = as_obj_at(s, o);
  float d = flat_dist(k->p, at);
  if ((k->team & 1) == s->as_att) {
    if (b->kind == AO_DESTROY) {
      /* in at it, lined up to shoot (the blaster, a missile) or to ram with a turbo. The way is planned to a point just
       * off it on this side: the target itself is solid, and nothing on the route ever has it in sight */
      /* (a foe right on it only: the blaster takes the ones lined up on the way, and an attacker that stops to brawl never
       * gets there) */
      *busy = k->item != IT_NONE;
      *chase = foe_near(s, me, k->p, 7.0f);
      t3_vec3 to = { k->p.x - at.x, 0, k->p.z - at.z };
      float l = sqrtf(to.x * to.x + to.z * to.z);
      if (l < b->r + 3.0f) return at;
      float off = b->r + 6.0f;
      return (t3_vec3){ at.x + to.x / l * off, at.y, at.z + to.z / l * off };
    }
    *busy = d < b->r + 25.0f;
    if (d < b->r + 8.0f) *chase = foe_near(s, me, at, b->r + 4.0f);
    return d < b->r * 0.8f && b->kind == AO_HOLD ? orbit(s, me, at, b->r * 0.4f) : at;
  }
  /* a defender: after the attacker nearest the objective (within 45 m of it), else round it */
  *chase = foe_near(s, me, at, 45.0f + b->r);
  *busy = *chase >= 0;
  *hold = *chase < 0;
  return orbit(s, me, at, b->r + 9.0f);
}

/* capture the flag and king of the mountain: where this driver means to be, the kart to chase on the way (*chase, -1
 * none) and whether that is too urgent to turn aside for a box (*busy) */
static t3_vec3 mode_goal(const sim *s, int me, int *chase, bool *busy, bool *hold) {
  const kart *k = &s->k[me];
  *chase = -1, *busy = false, *hold = false;
  if (s->amode == AM_ASSAULT) return assault_goal(s, me, chase, busy, hold);
  if (s->amode == AM_KOTM) {
    float d = flat_dist(k->p, s->hill);
    *busy = d < HILL_R + 2.0f; /* off it, a box close by is worth the turn: an armed kart takes the hill back */
    /* on it: fight whoever else is there */
    if (d < HILL_R + 6.0f) *chase = foe_near(s, me, s->hill, HILL_R + 2.0f);
    return d < HILL_R ? orbit(s, me, s->hill, 4.5f) : s->hill;
  }
  int own = k->team & 1, other = own ^ 1;
  const struct ctf_flag *mine = &s->flag[own], *theirs = &s->flag[other];
  if (k->carry >= 0) {
    *busy = true;
    return s->base[own];
  }
  /* the guards: the second of every three on the team (by kart order) */
  int before = 0;
  for (int j = 0; j < me; j++) before += s->k[j].on && s->k[j].team == k->team;
  bool guard = before % 3 == 1;
  if (mine->state == FL_CARRIED && (guard || among_nearest(s, me, mine->p, 2))) {
    *chase = mine->carrier, *busy = true;
    return s->k[mine->carrier].p;
  }
  if (mine->state == FL_DROPPED && (guard || among_nearest(s, me, mine->p, 2))) {
    *busy = true;
    return mine->p;
  }
  if (guard) {
    /* round a point a little out in front of the base (towards the middle), not into whatever stands behind it */
    t3_vec3 b = s->base[own], c = { 0, b.y, ARENA_CZ };
    float l = flat_dist(b, c);
    t3_vec3 at = l > 1 ? v3add(b, v3mul(v3sub(c, b), 4.0f / l)) : b;
    at.y = b.y;
    *chase = foe_near(s, me, b, 30.0f);
    *hold = *chase < 0; /* nobody near: it idles round there (flat out it only ran wide into whatever stands behind) */
    return orbit(s, me, at, 8.0f);
  }
  if (theirs->state == FL_CARRIED) {
    /* a teammate has it: escort it home, after whoever closes on it */
    *chase = foe_near(s, me, theirs->p, 25.0f);
    return s->base[own];
  }
  *busy = theirs->state == FL_DROPPED && flat_dist(k->p, theirs->p) < 40.0f;
  return theirs->p;
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
  bool offensive = armed && (k->item == IT_MISSILE || k->item == IT_HOMING || k->item == IT_TURBO || k->item == IT_TURBO3 || k->item == IT_SHOCK ||
                             k->item == IT_MISSILE3 || k->item == IT_BOOMERANG || k->item == IT_BOMB);
  bool modes = s->amode != AM_BALLOON && s->arena;
  int chase = -1;
  bool busy = false, hold = false;
  t3_vec3 obj = { 0, 0, ARENA_CZ };
  if (modes) {
    obj = mode_goal(s, me, &chase, &busy, &hold);
    if (chase >= 0 && chase != m->b_target) m->b_target = chase, m->b_box = -1; /* a carrier to run down: at once */
  }
  /* rethink the goal now and then */
  if (--m->b_think <= 0) {
    m->b_think = 20 + (int)(rnd(m) % 20u);
    if (modes) {
      /* the objective first: only a box or a kart close by turns it aside */
      m->b_target = chase >= 0 ? chase : offensive ? pick_target(s, me, m, 28.0f) : -1;
      m->b_box = (!busy && !armed && k->item_wait == 0) ? pick_box(s, me, m, 24.0f) : -1;
    } else {
      m->b_target = offensive ? pick_target(s, me, m, 1e9f) : -1;
      m->b_box = (!armed && k->item_wait == 0) ? pick_box(s, me, m, 1e9f) : -1;
    }
    if (!modes && m->b_target < 0 && m->b_box < 0) {
      /* nothing to do: roam towards the far side, or after the nearest kart */
      m->b_target = pick_target(s, me, m, 1e9f);
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
  t3_vec3 goal = modes ? obj : m->b_goal;
  if (m->b_roam > 0) m->b_roam--, goal = m->b_goal; /* fed up with the last goal: wander to the roaming point a while */
  else if (m->b_box >= 0) goal = s->box[m->b_box].p;
  else if (m->b_target >= 0 && (!modes || m->b_target == chase || flat_dist(s->k[m->b_target].p, k->p) < 28.0f)) {
    /* lead the target a little */
    const kart *o = &s->k[m->b_target];
    float t = clampf(flat_dist(o->p, k->p) / 45.0f, 0, 0.8f);
    goal = v3add(o->p, v3mul(o->v, t));
    /* right on top of it: do not try to turn round it (that is a circle at walking pace); carry on past and come again */
    if (flat_dist(o->p, k->p) < 8.0f && fabsf(bearing(k, o->p)) > 0.6f) {
      t3_vec3 fw = kart_fwd(k);
      goal = v3add(k->p, (t3_vec3){ fw.x * 16.0f, 0, fw.z * 16.0f });
    }
  }
  if (s->arena) {
    /* its own level is what it stands on (in a hall under a roof, the hall's floor); the goal's the top there */
    float my_lv = arena_level_below(s->arena, k->p.x, k->p.y, k->p.z), goal_lv = arena_level(s->arena, goal.x, goal.z);
    bool ramp = false;
    t3_vec3 pad;
    /* up a level at a time: the ramp to the goal's level, and if its foot is still above this kart, the ramp to that, and so
     * on (a pyramid's stair, then the ramp up its upper tier) */
    for (int up = 0; up < 3 && goal_lv > my_lv + 1.5f; up++) {
      t3_vec3 g2 = via_ramp(s, k, goal, goal_lv, &ramp);
      if (!ramp) break;
      goal = g2;
      float foot_lv = arena_level(s->arena, goal.x, goal.z);
      if (foot_lv >= goal_lv - 0.8f) break; /* on it (aiming off its top): no lower foot to go for */
      goal_lv = foot_lv;
    }
    if (!ramp && fabsf(goal_lv - my_lv) > 3.0f && via_tele(s, k, my_lv, goal, goal_lv, &pad)) goal = pad;
    goal = route_to(s, k, m, goal);
  }
  float ang = bearing(k, goal), dist = flat_dist(goal, k->p);
  if (flat_dist(goal, m->b_aim) > 8.0f) m->b_best_d = 0; /* a new goal (not one on the move): no nearer to it yet is not being stuck */
  /* no nearer to it for a few seconds (a wall between, a kart that keeps away): give up on it for a while. Circling its
   * objective close by (a guard at its base, a king on the hill) is not being stuck */
  if (dist < m->b_best_d - 1.0f || m->b_best_d <= 0 || (modes && m->b_roam == 0 && flat_dist(k->p, obj) < 14.0f)) m->b_best_d = dist, m->b_frust = 0;
  else if (++m->b_frust > 180) {
    /* the open way round, or now and then somewhere else altogether */
    const arena_rt *a = s->arena;
    int sp = a && a->nspawn ? (int)(rnd(m) % (uint32_t)a->nspawn) : 0;
    if (rnd(m) % 3u && m->b_roam == 0) m->b_goal = pick_detour(s, k, goal), m->b_roam = 80;
    else if (a && a->def->nroute > 0) m->b_goal = route_pt(s, (int)(rnd(m) % (uint32_t)a->def->nroute)), m->b_roam = 120, m->b_box = -1; /* a point on the way round */
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
    /* the kart is wider than a ray: two more down its flanks, so a heading that clips a post or a tank is not taken for clear */
    t3_vec3 side = { dir.z * 0.85f, 0, -dir.x * 0.85f };
    for (int fl2 = -1; fl2 <= 1; fl2 += 2) {
      float f2 = 1;
      if (sim_ray(s, v3add(from, v3mul(side, (float)fl2)), v3mul(dir, look), &f2, &n) && n.y < 0.6f && f2 < clear[c]) clear[c] = f2;
    }
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
  if (spd > 18) lock = fminf(lock, 0.75f); /* fast: a wide arc keeps the grip; full lock only scrubs and spins */
  float steer = clampf(best_a * (spd < 10 ? 1.2f : 1.8f), -lock, lock); /* gentler at a crawl, where the battle turn-in answers the stick quickly */
  m->steer_smooth += (steer - m->steer_smooth) * 0.5f;
  in.steer = (int8_t)(clampf(m->steer_smooth, -1, 1) * 127);
  in.thr = 127;
  /* a hard turn at speed: lift; the skilled drift through it (a drift boost is a ram waiting to happen) */
  if (fabsf(best_a) > 1.3f && spd > 22) in.thr = 0; /* lift through a hard turn (a drift at full lock this fast spins it round) */
  if (m->skill >= 1 && fabsf(best_a) > 0.45f && fabsf(best_a) < 1.3f && spd > 17 && fc > 0.6f) in.btn |= KB_DRIFT;
  /* close to a box but off to the side: slow right down so it turns tight instead of circling it */
  if (m->b_box >= 0 && m->b_roam == 0 && dist < 7 && fabsf(ang) > 1.2f && spd > 14) in.thr = 0;
  /* the way it is going ends sooner than it can turn: brake */
  if (clear[best_c] * look < spd * 0.3f && spd > 14) in.thr = -127, in.btn &= ~KB_DRIFT;
  /* a wall straight ahead less than half a second off: brake and slide round (a crash costs far more than a brake) */
  if (fc * look < spd * 0.4f && spd > 14 && fabsf(best_a) > 0.3f) in.thr = -127, in.btn &= ~KB_DRIFT;
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
    bool round = back_m > 0.5f && (fabsf(ang) < 0.8f || ahead_m > 9.0f);
    if (round || back_m > 6.0f || m->b_reverse == 0 || (behind && m->b_reverse < 54)) m->b_reverse = 0, m->b_commit = 30, in.thr = 127;
  }

  if (hold && flat_dist(k->p, obj) < 14.0f && in.thr > 55) in.thr = 55; /* a guard with nobody near: easy round its post */
  m->b_thr = in.thr;
  /* a Shockwave is coming: hop it */
  if (m->skill >= 1 && k->shock_warn > 0 && k->shock_warn <= 9 + 2 * (2 - m->skill)) in.btn |= KB_DRIFT;
  /* items */
  if (armed) {
    bool use = false, back = false;
    m->b_hold++;
    int tgt = m->b_target;
    switch (k->item) {
    case IT_MISSILE:
    case IT_MISSILE3:
    case IT_BOOMERANG: {
      /* lined up and clear; a boomerang's reach is shorter (it turns back at BOOM_OUT_M) */
      float reach = k->item == IT_BOOMERANG ? 38.0f : 55.0f;
      for (int j = 0; j < MAXK && !use; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out || o->respawn_ticks > 0 || !kart_foe(s, me, j)) continue;
        float d = flat_dist(o->p, k->p), a = bearing(k, v3add(o->p, v3mul(o->v, d / 50.0f)));
        if (d < reach && fabsf(a) < 0.09f + 1.2f / (d + 8.0f) && clear_line(s, k->p, o->p)) use = true;
      }
      break;
    }
    case IT_BOMB:
      /* lobbed onto someone 12 to 30 m ahead, or dropped back on someone close behind (the brake held as it goes) */
      for (int j = 0; j < MAXK && !use; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out || o->respawn_ticks > 0 || !kart_foe(s, me, j)) continue;
        float d = flat_dist(o->p, k->p), a = fabsf(bearing(k, o->p));
        if (d > 12 && d < 30 && a < 0.3f) use = true;
        else if (d < 14 && a > 2.4f) use = back = true;
      }
      if (m->b_hold > 8 * TICK_HZ) use = true;
      break;
    case IT_HOMING:
      for (int j = 0; j < MAXK && !use; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out || o->respawn_ticks > 0 || !kart_foe(s, me, j)) continue;
        float d = flat_dist(o->p, k->p);
        if (d < 70 && fabsf(bearing(k, o->p)) < 0.7f && clear_line(s, k->p, o->p)) use = true;
      }
      break;
    case IT_OIL:
    case IT_MINE:
      for (int j = 0; j < MAXK; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out || !kart_foe(s, me, j)) continue;
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
      /* capture the flag: on a long clear run at the goal, a turbo is speed (an attacker going for the flag, a carrier home) */
      if (modes && mode_teams(s) && fabsf(ang) < 0.22f && dist > 30.0f && fc > 0.98f && spd > 14.0f && m->b_hold > 20) use = true;
      break;
    case IT_SHOCK:
      for (int j = 0; j < MAXK; j++) {
        const kart *o = &s->k[j];
        if (j == me || !o->on || o->out || !kart_foe(s, me, j)) continue;
        if (flat_dist(o->p, k->p) < 24) use = true;
      }
      break;
    default: break;
    }
    /* assault: an attacker's shot at its target (missiles, a boomerang, a bomb lobbed on it, a turbo to ram it) */
    int ao = s->amode == AM_ASSAULT && (k->team & 1) == s->as_att ? as_pick(s, me) : -1;
    if (ao >= 0 && s->arena->def->assault->objs[ao].kind == AO_DESTROY && !use) {
      const assault_obj *b = &s->arena->def->assault->objs[ao];
      t3_vec3 at = as_obj_at(s, ao);
      float d = flat_dist(at, k->p), a = fabsf(bearing(k, at)), cone = cw_atan2(b->r * 0.8f, d);
      switch (k->item) {
      case IT_MISSILE:
      case IT_MISSILE3:
      case IT_HOMING:
      case IT_BOOMERANG: use = d < 60.0f && a < cone; break;
      case IT_BOMB: use = d > 9.0f && d < 30.0f && a < 0.3f; break;
      case IT_TURBO:
      case IT_TURBO3: use = d < 28.0f && a < cone * 0.8f && spd > 8.0f; break;
      default: break;
      }
    }
    /* the rookies are slower on the trigger */
    int wait = m->skill == 2 ? 6 : m->skill == 1 ? 14 : 30;
    if (use && m->b_hold > wait && k->item_cool == 0 && k->spin_ticks == 0) {
      in.btn |= KB_ITEM;
      if (back) in.thr = -127; /* the brake held as the bomb goes: it goes back */
      m->b_hold = 0;
      m->b_think = 0;
    }
  } else m->b_hold = 0;
  /* assault, nothing in hand: the blaster at a foe lined up in reach, or (an attacker) at its target */
  if (s->amode == AM_ASSAULT && !armed && k->item_wait == 0 && k->spin_ticks == 0) {
    bool fire = false;
    for (int j = 0; j < MAXK && !fire; j++) {
      const kart *o = &s->k[j];
      if (j == me || !o->on || o->respawn_ticks > 0 || !kart_foe(s, me, j)) continue;
      float d = flat_dist(o->p, k->p), a = bearing(k, v3add(o->p, v3mul(o->v, d / 85.0f)));
      fire = d < 48.0f && fabsf(a) < 0.07f + 1.0f / (d + 8.0f) && fabsf(o->p.y - k->p.y) < 3.0f && clear_line(s, k->p, o->p);
    }
    int ao = (k->team & 1) == s->as_att ? as_pick(s, me) : -1;
    if (!fire && ao >= 0 && s->arena->def->assault->objs[ao].kind == AO_DESTROY) {
      t3_vec3 at = as_obj_at(s, ao);
      float d = flat_dist(at, k->p);
      fire = d < 55.0f && fabsf(bearing(k, at)) < cw_atan2(s->arena->def->assault->objs[ao].r * 0.8f, d) && fabsf(at.y - (k->p.y - 0.6f)) < 3.0f;
    }
    /* the rookies hold off a moment; nobody sprays it at nothing */
    if (fire && (m->skill > 0 || (s->tick + me * 7) % 40 < 24)) in.btn |= KB_ITEM;
  }
  return in;
}
