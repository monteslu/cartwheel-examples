/* sim_battle.inc: battle mode's rules, included by sim.c (it shares sim.c's statics; never built on its own).
 *
 * An arena map (map_def.arena) makes the sim a battle: no laps, checkpoints or wrong way. Every kart starts with
 * BALLOONS balloons. A hit that spins a kart (a missile, a homing missile, a mine and its blast, oil, a Shockwave not
 * hopped, a turbo ram) pops one and gives the attacker a point; a kart with none left is out (it finishes its spin
 * and is parked under the arena: its view watches the others). The round ends when one kart is left (BE_LAST), the
 * clock runs out (BE_TIME, three minutes unless sim_set_battle_time) or every human is out (BE_HUMANS, a moment
 * later). The order: the last kart standing first, then points, then balloons left, then who lasted longer.
 *
 * Hazards live in the world here, not along the track: a missile flies where its kart pointed, hugs the floor over
 * ramps and decks, and glances off walls and cover (a few times, then it bursts); a homing missile turns after the
 * kart it locked at launch; oil and mines lie where they were dropped. A kart under any boost that drives into
 * another at RAM_SPEED or more pops one of its balloons (the TURBO RAM): turbos and drift boosts are weapons.
 * Item boxes stand where the arena puts them and come back six seconds after they are taken. A kart that drives onto one
 * of the arena's teleport pads (arena_tele) is at its far end the next tick, facing the pad's way at the pad's speed, with
 * its item and any flag it carries (EV_TELE); a kart that falls off an open arena is put back at a spawn (its own team's in
 * capture the flag) once it drops under the kill plane.
 *
 * The same arena plays two more rounds (sim.amode, sim_set_arena_mode), with no balloons and nobody out:
 *   capture the flag     two teams (red and blue), a flag on each team's base at opposite ends. Drive over the other
 *                        team's flag to take it and bring it to your own base while your own flag is home: a capture.
 *                        A carrier that spins (any hit) drops the flag where it is; the flag's own team touching it sends
 *                        it home, the other team picks it up again; left alone it goes home after FLAG_RETURN. First to
 *                        CTF_WIN captures, or the most when the clock runs out (level: a draw). No friendly fire.
 *   king of the mountain one hill (a circle HILL_R wide on one of the arena's hill spots, moving to another every
 *                        HILL_MOVE, called out five seconds before). A kart alone on it scores its time there; two or
 *                        more score nobody; a spinning kart is not on it (and a hit shoves it off). The most time when
 *                        the clock runs out wins. */

#define BATTLE_TICKS (180 * TICK_HZ)
#define POP_GRACE 100   /* ticks after a pop before the same kart can lose another */
#define OUT_LINGER 80   /* ticks an eliminated kart stays (its spin plays out) before it is parked */
#define RAM_SPEED 6.0f  /* m/s closing speed of a turbo ram */
#define B_MISSILE_SPEED 44.0f
#define B_MISSILE_LIFE (int)(3.4f * TICK_HZ)
#define B_HOMING_LIFE (int)(4.2f * TICK_HZ)
#define B_BOUNCES 3
#define FLAG_R 3.4f              /* a kart this near a flag (or its base, the flag at home) touches it */
#define BASE_R 7.0f              /* a carrier this near its own base captures */
#define FLAG_RETURN (20 * TICK_HZ)
#define HILL_MOVE (35 * TICK_HZ)
#define HILL_WARN (5 * TICK_HZ)

/* the items in battle: no place to weight by, so a kart with fewer balloons left draws more of the strong ones (the other
 * rounds draw from the middle column) */
static const uint8_t BATTLE_W[IT_KINDS][BALLOONS] = {
  /*            1 left  2  3 */
  [IT_TURBO] = { 8, 10, 12 },
  [IT_TURBO3] = { 6, 5, 4 },
  [IT_MISSILE] = { 22, 24, 26 },
  [IT_HOMING] = { 18, 14, 10 },
  [IT_OIL] = { 8, 10, 12 },
  [IT_MINE] = { 12, 14, 16 },
  [IT_SHIELD] = { 14, 10, 7 },
  [IT_SHOCK] = { 6, 4, 2 },
  [IT_MISSILE3] = { 8, 7, 6 },
  [IT_BOOMERANG] = { 10, 10, 10 },
  [IT_BOMB] = { 10, 9, 8 },
};

static int roll_battle_item(sim *s, int i) {
  if (s->item_set == 1) return (rnd(s) % 3u) ? IT_TURBO : IT_TURBO3; /* the turbos-only preset: a battle of turbo rams */
  int b = s->amode == AM_BALLOON ? s->k[i].balloons - 1 : 1;
  b = b < 0 ? 0 : b >= BALLOONS ? BALLOONS - 1 : b;
  int total = 0;
  for (int it = 1; it < IT_KINDS; it++) total += BATTLE_W[it][b];
  int r = (int)(rnd(s) % (uint32_t)total);
  for (int it = 1; it < IT_KINDS; it++) {
    r -= BATTLE_W[it][b];
    if (r < 0) return it;
  }
  return IT_MISSILE;
}

bool sim_ray(const sim *s, t3_vec3 from, t3_vec3 delta, float *frac, t3_vec3 *normal) {
  (void)s;
  if (!PH.ready) return false;
  cw_phys_filter f = { .layers = (1u << L_TRACK) | (1u << L_WALL) };
  cw_ray_hit3d hit;
  if (!cw_world3d_raycast(PH.w, from, delta, &f, &hit)) return false;
  if (frac) *frac = hit.fraction;
  if (normal) *normal = hit.normal;
  return true;
}

/* the floor's height under (x, z), searching down from y_from (-100 when nothing is under it) */
static float battle_floor(const sim *s, float x, float y_from, float z) {
  float fr = 0;
  if (sim_ray(s, (t3_vec3){ x, y_from, z }, (t3_vec3){ 0, -40, 0 }, &fr, NULL)) return y_from - 40 * fr;
  return -100;
}

static void battle_locate(const sim *s, t3_vec3 p, float *sd, float *lat, float *h) {
  track_loc l;
  int hint = (int)p.z;
  hint = hint < 0 ? 0 : hint >= s->track->n ? s->track->n - 1 : hint;
  track_locate(s->track, p, hint, &l);
  *sd = l.s, *lat = l.lat, *h = l.height;
}

/* stands kart i on the floor found looking down from (x, y_from, z), facing yaw (radians, 0 = +z), moving at speed: its
 * pose and velocity only (the item, the flag and the rest stay) */
static t3_vec3 battle_pose(sim *s, int i, float x, float y_from, float z, float yaw, float speed) {
  kart *k = &s->k[i];
  float sn, cs;
  cw_sincos(yaw, &sn, &cs);
  t3_vec3 f = { sn, 0, cs }, u = { 0, 1, 0 }, r = v3cross(f, u);
  float y = battle_floor(s, x, y_from, z);
  if (y < -50) y = 0;
  t3_vec3 pos = { x, y + RIDE + 0.15f, z };
  t3_quat q = q_from_frame(r, u, f);
  cw_vehicle3d_reset(&PH.car[i]);
  cw_vehicle3d_set_axle(&PH.car[i], 0, 0, 0, 0);
  cw_vehicle3d_set_axle(&PH.car[i], 1, 0, 0, 0);
  cw_vehicle3d_teleport(&PH.car[i], (cw_xform3){ pos, q });
  cw_body3d_set_velocity(PH.body[i], v3mul(f, speed));
  cw_body3d_set_angular_velocity(PH.body[i], (t3_vec3){ 0, 0, 0 });
  k->p = pos, k->q = q, k->v = v3mul(f, speed), k->w = (t3_vec3){ 0, 0, 0 };
  k->speed = speed;
  track_locate(s->track, pos, -1, &k->loc);
  k->hint = k->loc.i;
  PH.prev_s[i] = k->loc.s;
  return pos;
}

/* an arena's boost strips and teleport pads: a kart on its wheels on a strip gets a pad's boost; one that drives onto a pad
 * goes to its far end (the flag it carries with it) */
static void battle_teleports(sim *s, int i) {
  const arena_rt *a = s->arena;
  kart *k = &s->k[i];
  for (int b = 0; b < a->nboost && k->grounded && k->boost_ticks < 50 && !k->out && s->phase == PH_RACE; b++) {
    const struct arena_boost_rt *o = &a->boost[b];
    float dx = k->p.x - o->at.x, dz = k->p.z - o->at.z, lx = dx * o->cs - dz * o->sn, lz = dx * o->sn + dz * o->cs;
    if (absf(lx) < o->hw && absf(lz) < o->hd && absf(k->p.y - RIDE - o->at.y) < 1.2f) start_boost(s, i, 70, 1.1f, 0);
  }
  if (k->tele_cool > 0) {
    k->tele_cool--;
    return;
  }
  if (a->ntele <= 0 || k->out || k->parked || k->respawn_ticks > 0 || s->phase != PH_RACE) return;
  for (int t = 0; t < a->ntele; t++) {
    const struct arena_tele_rt *p = &a->tele[t];
    float dx = k->p.x - p->at.x, dz = k->p.z - p->at.z;
    if (dx * dx + dz * dz > p->r * p->r || absf(k->p.y - RIDE - p->at.y) > 1.6f) continue;
    t3_vec3 from = k->p;
    battle_pose(s, i, p->to.x, p->to.y + 3.0f, p->to.z, p->yaw, p->speed);
    k->drifting = false;
    k->drift_ticks = k->drift_level = 0;
    k->air_ticks = 0;
    k->stall_ticks = k->stuck_ticks = 0;
    k->tele_cool = TELE_COOL;
    event(s, EV_TELE, i, t, from, 0);
    return;
  }
}

/* stands kart i at (x, z) on whatever floor is there, facing yaw (radians, 0 = +z) */
static void battle_place(sim *s, int i, float x, float z, float yaw, float speed) {
  kart *k = &s->k[i];
  battle_pose(s, i, x, 30, z, yaw, speed);
  kart_reset_state(k);
  k->speed = speed;
  k->prog = 0;
}

void sim_battle_put(sim *s, int i, float x, float z, float yaw_deg, float speed) {
  if (s->battle && i >= 0 && i < MAXK && s->k[i].on) battle_place(s, i, x, z + ARENA_CZ, yaw_deg * (CW_PI / 180.0f), speed);
}

/* a point of the arena's own frame (x, z) in the world, standing on the floor there (the top of a deck or a mound) */
static t3_vec3 arena_spot(const sim *s, float x, float z) {
  float wz = z + ARENA_CZ;
  return (t3_vec3){ x, arena_level(s->arena, x, wz), wz };
}

/* king of the mountain: the hill onto spot h */
static void hill_put(sim *s, int h) {
  const arena_def *d = s->arena->def;
  s->hill_i = h;
  s->hill = d->nhills > 0 ? arena_spot(s, d->hills[h][0], d->hills[h][1]) : arena_spot(s, 0, 0);
  s->hill_left = HILL_MOVE;
  s->hill_holder = -1;
}

static void flag_home(sim *s, int t) {
  s->flag[t].state = FL_HOME;
  s->flag[t].carrier = -1;
  s->flag[t].timer = 0;
  s->flag[t].p = s->base[t];
}

static void battle_place_all(sim *s) {
  const arena_rt *a = s->arena;
  const arena_def *d = a->def;
  int n = 0, nt[2] = { 0, 0 };
  if (s->amode == AM_CTF)
    for (int t = 0; t < 2; t++) s->base[t] = arena_spot(s, d->base[t][0], d->base[t][1]);
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on) continue;
    if (s->amode == AM_CTF && d->team_spawns) {
      /* round its own base, facing the other */
      int t = k->team & 1, sp = t * 4 + nt[t]++ % 4;
      battle_place(s, i, d->team_spawns[sp][0], d->team_spawns[sp][1] + ARENA_CZ, d->team_spawns[sp][2] * (CW_PI / 180.0f), 0);
    } else {
      /* the spawns are listed so that the first two face each other across the arena, the first four are a quarter turn apart */
      int sp = a->nspawn > 0 ? n % a->nspawn : 0;
      if (a->nspawn > 0) battle_place(s, i, a->spawn[sp][0], a->spawn[sp][1], a->spawn[sp][2], 0);
      else battle_place(s, i, (float)n * 4 - 14, ARENA_CZ, 0, 0);
    }
    k->balloons = s->amode == AM_BALLOON ? BALLOONS : 0;
    k->points = k->pops_taken = 0;
    k->out = k->parked = false;
    k->out_tick = 0;
    k->ram_cool = 0;
    k->lap = 0;
    k->carry = -1;
    k->caps = k->steals = k->returns = k->flag_cool = 0;
    k->hill_ticks = 0;
    k->tele_cool = 0;
    n++;
  }
  s->alive = n;
  s->battle_left = s->battle_limit;
  s->battle_end = BE_NONE;
  s->humans_out_tick = 0;
  s->team_score[0] = s->team_score[1] = 0;
  s->win_team = -1;
  if (s->amode == AM_CTF)
    for (int t = 0; t < 2; t++) flag_home(s, t);
  if (s->amode == AM_KOTM) hill_put(s, 0);
}

void sim_set_arena_mode(sim *s, int mode) {
  if (!s->battle) return;
  s->amode = mode >= 0 && mode < AM_KINDS ? mode : AM_BALLOON;
  /* the teams: the humans by player (one or two all red; three or four alternate), then the CPUs to whichever team is
   * short (red first) */
  int humans = 0;
  for (int i = 0; i < MAXK; i++) humans += s->k[i].on && s->k[i].human;
  int nt[2] = { 0, 0 };
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on) continue;
    if (k->human) k->team = humans <= 2 ? 0 : i & 1;
    else k->team = nt[1] < nt[0] ? 1 : 0;
    nt[k->team]++;
  }
  battle_place_all(s);
}

static void battle_boxes(sim *s) {
  const arena_def *d = s->arena->def;
  s->nbox = 0;
  for (int b = 0; b < d->nboxes && s->nbox < MAXBOX; b++) {
    t3_vec3 p = { d->boxes[b][0], d->boxes[b][2] + 1.1f, d->boxes[b][1] + ARENA_CZ };
    float sd, lat, h;
    battle_locate(s, p, &sd, &lat, &h);
    s->box[s->nbox++] = (itembox){ sd, lat, p, 0 };
  }
}

void sim_set_battle_time(sim *s, int ticks) {
  if (!s->battle) return;
  s->battle_limit = ticks > 0 ? ticks : BATTLE_TICKS;
  s->battle_left = s->battle_limit;
}

/* capture the flag: kart i (spun, or put back) lets go of the flag it carries; dropped where it is, or home when it fell */
static void flag_drop(sim *s, int i, bool home) {
  kart *k = &s->k[i];
  if (s->amode != AM_CTF || k->carry < 0) return;
  int t = k->carry;
  k->carry = -1;
  k->flag_cool = 60;
  if (home) {
    flag_home(s, t);
    event(s, EV_FLAG, i, FE_HOME, s->flag[t].p, (float)t);
    return;
  }
  float y = battle_floor(s, k->p.x, k->p.y + 1.0f, k->p.z);
  s->flag[t].state = FL_DROPPED;
  s->flag[t].carrier = -1;
  s->flag[t].timer = FLAG_RETURN;
  s->flag[t].p = (t3_vec3){ k->p.x, y < -50 ? k->p.y - RIDE : y, k->p.z };
  event(s, EV_FLAG, i, FE_DROPPED, s->flag[t].p, (float)t);
}

static void battle_pop(sim *s, int i, int by);
/* a hit that spun kart i (spin_kart): in a battle a balloon; capture the flag drops the flag it carries; king of the
 * mountain shoves it off the hill. Every round: a shorter spin and a moment's grace after it */
static void battle_hit(sim *s, int i, int by) {
  kart *k = &s->k[i];
  if (s->amode == AM_BALLOON) {
    battle_pop(s, i, by);
    return;
  }
  k->ram_cool = POP_GRACE;
  k->spin_ticks = (int)((float)k->spin_ticks * BATTLE_SPIN_K);
  if (by >= 0 && by < MAXK && by != i && s->k[by].on) s->k[by].points += s->amode == AM_CTF ? 1 : 0;
  if (s->amode == AM_CTF) flag_drop(s, i, false);
  else {
    t3_vec3 d = { k->p.x - s->hill.x, 0, k->p.z - s->hill.z };
    float l = v3len(d);
    if (l < HILL_R + 3.0f && l > 1e-3f) cw_body3d_apply_impulse(PH.body[i], v3mul(d, 9.0f * VEH[k->veh].mass / l), NULL);
  }
}

/* a hit that spun kart i in a battle: a balloon goes, the attacker scores, and maybe it is out */
static void battle_pop(sim *s, int i, int by) {
  kart *k = &s->k[i];
  if (k->out || k->balloons <= 0) return;
  k->balloons--;
  k->pops_taken++;
  k->ram_cool = POP_GRACE;
  k->spin_ticks = (int)((float)k->spin_ticks * BATTLE_SPIN_K); /* a battle spin is shorter: losing the balloon is the cost, the long tumble only feeds the pile-up */
  if (by >= 0 && by < MAXK && by != i && s->k[by].on) s->k[by].points++;
  event(s, EV_POP, i, by == i ? -1 : by, k->p, (float)k->balloons);
  if (k->balloons == 0) {
    k->out = true;
    k->out_tick = s->race_ticks;
    k->item = IT_NONE, k->item_count = 0, k->item_wait = 0;
    k->shield_ticks = 0;
    s->alive--;
    event(s, EV_OUT, i, by == i ? -1 : by, k->p, (float)s->alive);
  }
}

/* an eliminated kart, its spin done: parked out of the way under the arena (it no longer takes part) */
static void battle_park(sim *s, int i) {
  kart *k = &s->k[i];
  t3_vec3 pos = { (float)i * 12.0f - 42.0f, -300.0f, ARENA_CZ };
  cw_vehicle3d_reset(&PH.car[i]);
  cw_body3d_set_velocity(PH.body[i], (t3_vec3){ 0, 0, 0 });
  cw_body3d_set_angular_velocity(PH.body[i], (t3_vec3){ 0, 0, 0 });
  cw_body3d_set_transform(PH.body[i], (cw_xform3){ pos, k->q });
  cw_body3d_set_type(PH.body[i], CW_BODY_STATIC);
  k->p = pos, k->v = (t3_vec3){ 0, 0, 0 }, k->w = (t3_vec3){ 0, 0, 0 };
  k->speed = 0;
  k->spin_ticks = 0;
  k->parked = true;
}

/* a kart that fell or stuck: back on the floor at the spawn farthest from the others, facing the middle */
static void battle_respawn(sim *s, int i) {
  const arena_rt *a = s->arena;
  const arena_def *d = a->def;
  flag_drop(s, i, true);
  /* capture the flag: one of its own team's starts (the one farthest from the other team) */
  bool team = s->amode == AM_CTF && d->team_spawns;
  int best = 0, nsp = team ? 4 : a->nspawn;
  float bd = -1;
  for (int sp = 0; sp < nsp; sp++) {
    float sx = team ? d->team_spawns[(s->k[i].team & 1) * 4 + sp][0] : a->spawn[sp][0];
    float sz = team ? d->team_spawns[(s->k[i].team & 1) * 4 + sp][1] + ARENA_CZ : a->spawn[sp][1];
    float dmin = 1e9f;
    for (int j = 0; j < MAXK; j++) {
      if (j == i || !s->k[j].on || s->k[j].out || (team && !kart_foe(s, i, j))) continue;
      float dx = s->k[j].p.x - sx, dz = s->k[j].p.z - sz;
      dmin = minf(dmin, dx * dx + dz * dz);
    }
    if (dmin > bd) bd = dmin, best = sp;
  }
  if (team) {
    const float *sp = d->team_spawns[(s->k[i].team & 1) * 4 + best];
    battle_place(s, i, sp[0], sp[1] + ARENA_CZ, sp[2] * (CW_PI / 180.0f), 6.0f);
  } else if (a->nspawn > 0) battle_place(s, i, a->spawn[best][0], a->spawn[best][1], a->spawn[best][2], 6.0f);
  kart *k = &s->k[i];
  k->respawn_ticks = 90;
  k->stall_ticks = 0;
  k->item_cool = 30;
  k->falls++;
  event(s, EV_RESPAWN, i, 0, k->p, 0);
}

/* the horizontal heading of kart i */
static t3_vec3 flat_fwd(const kart *k) {
  t3_vec3 f = kart_fwd(k);
  f.y = 0;
  float l = v3len(f);
  return l > 1e-4f ? v3mul(f, 1.0f / l) : (t3_vec3){ 0, 0, 1 };
}

/* the kart a homing missile from kart i locks onto: the nearest, favouring the ones ahead of it */
static int battle_mark(const sim *s, int i) {
  const kart *k = &s->k[i];
  t3_vec3 f = flat_fwd(k);
  int best = -1;
  float bs = 1e9f;
  for (int j = 0; j < MAXK; j++) {
    const kart *o = &s->k[j];
    if (j == i || !o->on || o->out || !kart_foe(s, i, j)) continue;
    t3_vec3 d = v3sub(o->p, k->p);
    d.y = 0;
    float dist = v3len(d);
    if (dist > 110) continue;
    float c = dist > 1e-3f ? v3dot(d, f) / dist : 1;
    float score = dist * (1.0f + (1.0f - c) * 1.6f);
    if (score < bs) bs = score, best = j;
  }
  return best;
}

static void battle_hz_place(sim *s, hazard *z) { battle_locate(s, z->p, &z->s, &z->lat, &z->h); }

/* the battle's items that leave something in the world; turbos and the shield work as in a race */
static bool battle_use_item(sim *s, int i, int item) {
  kart *k = &s->k[i];
  t3_vec3 f = flat_fwd(k);
  switch (item) {
  case IT_MISSILE:
  case IT_HOMING: {
    hazard *z = hz_add(s, item == IT_HOMING ? HZ_HOMING : HZ_MISSILE, i, 0, 0, 0);
    if (!z) return true;
    z->p = v3add(v3add(k->p, v3mul(f, 2.6f)), (t3_vec3){ 0, 0.25f, 0 });
    z->f = f;
    z->speed = B_MISSILE_SPEED + maxf(k->speed, 0) * 0.45f;
    z->life = item == IT_HOMING ? B_HOMING_LIFE : B_MISSILE_LIFE;
    if (item == IT_HOMING) {
      z->speed *= 0.9f;
      z->target = battle_mark(s, i);
    }
    battle_hz_place(s, z);
    return true;
  }
  case IT_OIL:
  case IT_MINE: {
    t3_vec3 at = v3sub(k->p, v3mul(f, 3.0f));
    float y = battle_floor(s, at.x, k->p.y + 1.5f, at.z);
    if (y < -50) y = k->p.y - RIDE;
    hazard *z = hz_add(s, item == IT_MINE ? HZ_MINE : HZ_OIL, i, 0, 0, 0);
    if (!z) return true;
    z->p = (t3_vec3){ at.x, y + (item == IT_MINE ? 0.5f : 0.1f), at.z };
    z->f = f;
    z->life = item == IT_MINE ? 25 * TICK_HZ : 20 * TICK_HZ;
    battle_hz_place(s, z);
    z->h = 0.1f; /* the puddle is drawn this far under z->p (along the flat frame), on whatever floor it lies on */
    return true;
  }
  case IT_BOOMERANG: {
    hazard *z = hz_add(s, HZ_BOOMERANG, i, 0, 0, 0);
    if (!z) return true;
    z->p = v3add(v3add(k->p, v3mul(f, 2.6f)), (t3_vec3){ 0, 0.5f, 0 });
    z->f = f;
    z->speed = BOOM_REL + 4.0f;
    z->life = BOOM_LIFE;
    battle_hz_place(s, z);
    return true;
  }
  case IT_BOMB: {
    /* lobbed: ahead, or behind with the brake held (z->f and z->speed the flight across the floor) */
    bool back = k->last_thr < -40;
    hazard *z = hz_add(s, HZ_BOMB, i, 0, 0, 0);
    if (!z) return true;
    z->p = v3add(v3add(k->p, v3mul(f, back ? -1.5f : 1.5f)), (t3_vec3){ 0, 1.2f, 0 });
    z->f = back ? v3mul(f, -1) : f;
    float fwd = v3dot(k->v, f);
    z->speed = back ? maxf(14.0f - fwd, 4.0f) : maxf(fwd, 0) + 22.0f;
    z->vy = 7.5f;
    z->life = 4 * TICK_HZ;
    battle_hz_place(s, z);
    return true;
  }
  case IT_SHOCK:
    /* a ring out from the kart: everyone within 30 m gets the alarm (a hop clears it) */
    for (int j = 0; j < MAXK; j++) {
      kart *o = &s->k[j];
      if (j == i || !o->on || o->out || o->shock_warn || !kart_foe(s, i, j)) continue;
      t3_vec3 d = v3sub(o->p, k->p);
      if (v3dot(d, d) > 30.0f * 30.0f) continue;
      o->shock_warn = SHOCK_WARN / 2, o->shock_by = i;
      event(s, EV_WARN, j, i, o->p, 0);
    }
    event(s, EV_HIT, i, i, k->p, (float)HZ_SHOCK);
    return true;
  default: return false;
  }
}

/* turns a horizontal unit heading by at most `max` radians towards want (about +y: x' = x cos + z sin, z' = -x sin + z cos) */
static t3_vec3 turn_to(t3_vec3 f, t3_vec3 want, float max) {
  float cr = f.z * want.x - f.x * want.z, dt = f.x * want.x + f.z * want.z;
  float ang = clampf(cw_atan2(cr, dt), -max, max);
  float sn, cs;
  cw_sincos(ang, &sn, &cs);
  return (t3_vec3){ f.x * cs + f.z * sn, 0, -f.x * sn + f.z * cs };
}

/* a step of a missile or a boomerang across the floor: glances off a wall (false when it has glanced off too many and
 * burst), then rides the floor up ramps and over decks */
static bool battle_fly(sim *s, hazard *z, int bounces) {
  t3_vec3 step = v3mul(z->f, z->speed * DT);
  float fr = 0;
  t3_vec3 n;
  if (sim_ray(s, z->p, v3mul(step, 1.6f), &fr, &n) && absf(n.y) < 0.6f && fr < 0.625f) {
    t3_vec3 nh = v3norm((t3_vec3){ n.x, 0, n.z });
    if (++z->bounces > bounces) return false;
    z->f = v3norm(v3sub(z->f, v3mul(nh, 2.0f * v3dot(z->f, nh))));
    z->f.y = 0;
    z->p = v3add(v3add(z->p, v3mul(step, fr * 1.6f)), v3mul(nh, 0.35f));
  } else z->p = v3add(z->p, step);
  float fl = battle_floor(s, z->p.x, z->p.y + 1.4f, z->p.z);
  float want = fl + 0.75f;
  if (fl < -50) z->p.y -= 0.35f;
  else z->p.y += (want - z->p.y) * 0.45f;
  return z->p.y > -20;
}

/* a boomerang or a bomb in an arena (world space; see thrown_step for the road's):
 *   boomerang  straight out BOOM_OUT_M, glancing off walls, then back after its thrower, who catches it; one hit on
 *              each kart it meets, BOOM_HITS at most; a hop lets it by
 *   bomb       an arc that lands on whatever floor is there (a wall stops it going on), BOMB_FUSE of fizzing, the blast;
 *              another kart running into it sets it off at once */
static void battle_thrown_step(sim *s, hazard *z) {
  z->age++;
  if (--z->life <= 0) {
    if (z->kind == HZ_BOMB) bomb_blast(s, z);
    else z->kind = HZ_NONE;
    return;
  }
  const kart *own = z->owner >= 0 ? &s->k[z->owner] : NULL;
  if (z->kind == HZ_BOOMERANG) {
    if (z->state == 0) {
      z->dist += z->speed * DT;
      if (z->dist >= BOOM_OUT_M) z->state = 1;
    } else if (own && own->on && !own->out) {
      t3_vec3 d = v3sub(own->p, z->p);
      d.y = 0;
      float l = v3len(d);
      if (l > 0.3f) z->f = turn_to(z->f, v3mul(d, 1.0f / l), 7.0f * DT);
      z->speed = BOOM_REL + 6.0f;
    }
    if (!battle_fly(s, z, 6)) {
      z->kind = HZ_NONE;
      return;
    }
  } else if (z->state == 0) {
    t3_vec3 step = v3mul(z->f, z->speed * DT);
    float fr = 0;
    t3_vec3 n;
    if (sim_ray(s, z->p, step, &fr, &n) && absf(n.y) < 0.6f) z->speed = 0; /* it hit a wall: it drops there */
    else z->p = v3add(z->p, step);
    z->p.y += z->vy * DT;
    z->vy -= GRAV * DT;
    float fl = battle_floor(s, z->p.x, z->p.y + 1.0f, z->p.z);
    if (z->vy < 0 && fl > -50 && z->p.y <= fl + 0.35f) z->p.y = fl + 0.35f, z->state = 1, z->life = BOMB_FUSE;
    if (z->p.y < -20) {
      z->kind = HZ_NONE;
      return;
    }
  }
  battle_hz_place(s, z);
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on || k->out || k->parked || k->respawn_ticks > 0) continue;
    if (i != z->owner && z->owner >= 0 && !kart_foe(s, i, z->owner)) continue;
    t3_vec3 d = v3sub(k->p, z->p);
    if (z->kind == HZ_BOMB) {
      if (z->state == 1 && i != z->owner && v3dot(d, d) < 2.3f * 2.3f) {
        bomb_blast(s, z);
        return;
      }
      continue;
    }
    if (i == z->owner) {
      if (z->state == 1 && z->age > 30 && v3dot(d, d) < 2.8f * 2.8f) {
        z->kind = HZ_NONE; /* caught */
        return;
      }
      continue;
    }
    if ((z->hitmask >> i) & 1 || v3dot(d, d) > 2.0f * 2.0f) continue;
    z->hitmask |= 1 << i;
    if (hopped(s, i, z)) continue;
    event(s, EV_HIT, i, z->owner, z->p, (float)HZ_BOOMERANG);
    spin_kart(s, i, SPIN_HIT, z->owner);
    if (++z->hits >= BOOM_HITS) {
      z->kind = HZ_NONE;
      return;
    }
  }
}

static void battle_hazards_step(sim *s) {
  for (int h = 0; h < MAXHZ; h++) {
    hazard *z = &s->hz[h];
    if (!z->kind) continue;
    if (z->kind == HZ_BOOMERANG || z->kind == HZ_BOMB) {
      battle_thrown_step(s, z);
      continue;
    }
    z->age++;
    if (--z->life <= 0) {
      if (z->kind == HZ_MISSILE || z->kind == HZ_HOMING) event(s, EV_HIT, -1, z->owner, z->p, (float)z->kind);
      z->kind = HZ_NONE;
      continue;
    }
    if (z->kind == HZ_MISSILE || z->kind == HZ_HOMING) {
      if (z->kind == HZ_HOMING && z->target >= 0 && s->k[z->target].on && !s->k[z->target].out && z->age > 6) {
        t3_vec3 d = v3sub(s->k[z->target].p, z->p);
        d.y = 0;
        float l = v3len(d);
        if (l > 0.5f) z->f = turn_to(z->f, v3mul(d, 1.0f / l), 2.7f * DT);
      }
      t3_vec3 step = v3mul(z->f, z->speed * DT);
      float fr = 0;
      t3_vec3 n;
      if (sim_ray(s, z->p, v3mul(step, 1.6f), &fr, &n) && absf(n.y) < 0.6f && fr < 0.625f) {
        /* a wall or a block: glance off it */
        t3_vec3 nh = v3norm((t3_vec3){ n.x, 0, n.z });
        if (++z->bounces > B_BOUNCES) {
          event(s, EV_HIT, -1, z->owner, z->p, (float)z->kind);
          z->kind = HZ_NONE;
          continue;
        }
        z->f = v3norm(v3sub(z->f, v3mul(nh, 2.0f * v3dot(z->f, nh))));
        z->f.y = 0;
        z->p = v3add(v3add(z->p, v3mul(step, fr * 1.6f)), v3mul(nh, 0.35f));
        if (z->kind == HZ_HOMING) z->target = -1; /* a bounce loses the lock */
      } else z->p = v3add(z->p, step);
      /* it rides the floor: up a ramp, over a deck, down off an edge */
      float fl = battle_floor(s, z->p.x, z->p.y + 1.4f, z->p.z);
      float want = fl + 0.75f;
      if (fl < -50) z->p.y -= 0.35f;
      else z->p.y += (want - z->p.y) * 0.45f;
      if (z->p.y < -20) {
        z->kind = HZ_NONE;
        continue;
      }
      battle_hz_place(s, z);
    }
    for (int i = 0; i < MAXK; i++) {
      kart *k = &s->k[i];
      if (!k->on || k->out || k->respawn_ticks > 0) continue;
      if (i != z->owner && z->owner >= 0 && !kart_foe(s, i, z->owner)) continue; /* a teammate's: it passes through */
      bool missile = z->kind == HZ_MISSILE || z->kind == HZ_HOMING;
      if (z->owner == i && z->age < (missile ? 24 : 45)) continue;
      t3_vec3 d = v3sub(k->p, z->p);
      float r = z->kind == HZ_OIL ? 1.8f : z->kind == HZ_MINE ? 2.3f : 1.9f;
      if (z->kind == HZ_OIL && absf(d.y) > 1.6f) continue;
      if (v3dot(d, d) > r * r) continue;
      if (z->kind == HZ_OIL) {
        spin_kart(s, i, SPIN_OIL, z->owner);
      } else if (z->kind == HZ_MINE) {
        event(s, EV_HIT, i, z->owner, z->p, (float)HZ_MINE);
        spin_kart(s, i, SPIN_BOOM, z->owner);
        for (int j = 0; j < MAXK; j++) {
          if (j == i || !s->k[j].on || s->k[j].out || (z->owner >= 0 && j != z->owner && !kart_foe(s, j, z->owner))) continue;
          t3_vec3 e = v3sub(s->k[j].p, z->p);
          if (v3dot(e, e) < 4.5f * 4.5f) spin_kart(s, j, SPIN_HIT, z->owner);
        }
      } else {
        event(s, EV_HIT, i, z->owner, z->p, (float)z->kind);
        spin_kart(s, i, SPIN_HIT, z->owner);
      }
      z->kind = HZ_NONE;
      break;
    }
  }
}

/* two karts touched (contacts): a turbo ram pops the one that was driven into */
static void battle_ram(sim *s, int a, int b, t3_vec3 n_ab, float speed) {
  if (speed < RAM_SPEED || s->phase != PH_RACE) return;
  for (int pass = 0; pass < 2; pass++) {
    int att = pass ? b : a, vic = pass ? a : b;
    t3_vec3 to_vic = pass ? v3mul(n_ab, -1) : n_ab;
    kart *ka = &s->k[att], *kv = &s->k[vic];
    if (ka->out || kv->out || ka->boost_ticks <= 0 || ka->spin_ticks > 0 || !kart_foe(s, att, vic)) continue;
    if (v3dot(flat_fwd(ka), to_vic) < 0.55f) continue; /* its nose went in, not its flank */
    event(s, EV_RAM, att, vic, kv->p, speed);
    spin_kart(s, vic, SPIN_HIT, att);
    return;
  }
}

/* the order: the last one standing, then points, then balloons, then who lasted longer */
static void battle_rank(sim *s) {
  int idx[MAXK], n = 0;
  for (int i = 0; i < MAXK; i++)
    if (s->k[i].on) idx[n++] = i;
  for (int a = 1; a < n; a++) {
    int x = idx[a], b = a - 1;
    while (b >= 0) {
      const kart *p = &s->k[idx[b]], *q = &s->k[x];
      bool better;
      if (s->amode == AM_CTF) {
        /* the leading team first, then the points (a capture 3, a flag taken or sent home 1, a hit 1) */
        int tp = s->team_score[p->team & 1] - s->team_score[(p->team & 1) ^ 1], tq = s->team_score[q->team & 1] - s->team_score[(q->team & 1) ^ 1];
        if (tq != tp) better = tq > tp;
        else if (q->points != p->points) better = q->points > p->points;
        else better = q->caps > p->caps;
      } else if (s->amode == AM_KOTM) {
        better = q->hill_ticks != p->hill_ticks ? q->hill_ticks > p->hill_ticks : q->hits_given > p->hits_given;
      } else if (p->out != q->out) better = !q->out;
      else if (q->points != p->points) better = q->points > p->points;
      else if (q->balloons != p->balloons) better = q->balloons > p->balloons;
      else if (q->out && p->out) better = q->out_tick > p->out_tick;
      else better = false;
      if (!better) break;
      idx[b + 1] = idx[b];
      b--;
    }
    idx[b + 1] = x;
  }
  for (int a = 0; a < n; a++) {
    s->k[idx[a]].place = a + 1;
    s->order[a] = idx[a];
  }
}

/* a kart that can touch a flag or hold the hill: in the round, on its wheels (not spinning or flickering back in) */
static bool battle_able(const kart *k) { return k->on && !k->out && !k->parked && k->respawn_ticks <= 0 && k->spin_ticks <= 0; }

/* near a point: across the floor, and on the same level (a deck over it is not there) */
static bool battle_near(t3_vec3 p, t3_vec3 at, float r) {
  float dx = p.x - at.x, dz = p.z - at.z;
  return dx * dx + dz * dz < r * r && absf(p.y - RIDE - at.y) < 2.5f;
}

/* capture the flag, every tick of the round: carried flags ride their carriers, dropped ones wait (then go home),
 * karts touch flags, carriers reach their bases */
static void ctf_step(sim *s) {
  for (int t = 0; t < 2; t++) {
    struct ctf_flag *f = &s->flag[t];
    if (f->state == FL_CARRIED) f->p = s->k[f->carrier].p;
    else if (f->state == FL_DROPPED && --f->timer <= 0) {
      flag_home(s, t);
      event(s, EV_FLAG, -1, FE_HOME, f->p, (float)t);
    }
  }
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (k->flag_cool > 0) k->flag_cool--;
    if (!battle_able(k)) continue;
    int own = k->team & 1, other = own ^ 1;
    struct ctf_flag *mine = &s->flag[own], *theirs = &s->flag[other];
    /* its own flag lying out: sent home */
    if (mine->state == FL_DROPPED && battle_near(k->p, mine->p, FLAG_R)) {
      flag_home(s, own);
      k->returns++, k->points++;
      event(s, EV_FLAG, i, FE_RETURNED, k->p, (float)own);
    }
    /* the other team's flag, at home or lying out: taken */
    if (k->carry < 0 && k->flag_cool == 0 && theirs->state != FL_CARRIED && battle_near(k->p, theirs->p, FLAG_R)) {
      theirs->state = FL_CARRIED;
      theirs->carrier = i;
      theirs->p = k->p;
      k->carry = other;
      k->steals++, k->points++;
      event(s, EV_FLAG, i, FE_TAKEN, k->p, (float)other);
    }
    /* home with it, its own flag at home: a capture */
    if (k->carry >= 0 && mine->state == FL_HOME && battle_near(k->p, s->base[own], BASE_R)) {
      int t = k->carry;
      k->carry = -1;
      flag_home(s, t);
      s->team_score[own]++;
      k->caps++, k->points += 3;
      event(s, EV_FLAG, i, FE_CAPTURED, k->p, (float)own);
    }
  }
}

/* king of the mountain, every tick of the round: who is on the hill, the score, and the move */
static void kotm_step(sim *s) {
  const arena_def *d = s->arena->def;
  if (--s->hill_left == HILL_WARN && d->nhills > 1) event(s, EV_HILL, -1, HE_SOON, s->hill, 0);
  if (s->hill_left <= 0) {
    int h = s->hill_i;
    if (d->nhills > 1) h = (s->hill_i + 1 + (int)(rnd(s) % (uint32_t)(d->nhills - 1))) % d->nhills;
    hill_put(s, h);
    event(s, EV_HILL, -1, HE_MOVED, s->hill, 0);
  }
  int on = 0, who = -1;
  for (int i = 0; i < MAXK; i++)
    if (battle_able(&s->k[i]) && battle_near(s->k[i].p, s->hill, HILL_R)) on++, who = i;
  int holder = on == 1 ? who : on > 1 ? -2 : -1;
  if (holder >= 0) {
    kart *k = &s->k[holder];
    k->hill_ticks++;
    k->points = k->hill_ticks / TICK_HZ;
  }
  if (holder != s->hill_holder) {
    if (holder >= 0) event(s, EV_HILL, holder, HE_TAKEN, s->hill, 0);
    else if (holder == -2) event(s, EV_HILL, -1, HE_CONTESTED, s->hill, 0);
    s->hill_holder = holder;
  }
}

/* PH_RACE in a battle: the clock and the end of the round */
static void battle_phase(sim *s) {
  if (s->battle_left > 0) s->battle_left--;
  if (s->amode == AM_CTF) ctf_step(s);
  else if (s->amode == AM_KOTM) kotm_step(s);
  int humans = 0, humans_in = 0;
  for (int i = 0; i < MAXK; i++)
    if (s->k[i].on && s->k[i].human) humans++, humans_in += !s->k[i].out;
  if (humans > 0 && humans_in == 0 && s->humans_out_tick == 0) s->humans_out_tick = s->race_ticks;
  int end = BE_NONE;
  if (s->amode == AM_CTF && (s->team_score[0] >= CTF_WIN || s->team_score[1] >= CTF_WIN)) end = BE_SCORE;
  else if (s->nkarts > 1 && s->alive <= 1) end = BE_LAST;
  else if (s->battle_left <= 0) end = BE_TIME;
  else if (s->humans_out_tick > 0 && s->race_ticks - s->humans_out_tick > 150) end = BE_HUMANS;
  if (end != BE_NONE) {
    if (s->amode == AM_CTF) s->win_team = s->team_score[0] > s->team_score[1] ? 0 : s->team_score[1] > s->team_score[0] ? 1 : -1;
    battle_rank(s);
    s->battle_end = end;
    s->phase = PH_DONE;
    s->phase_ticks = 0;
    for (int i = 0; i < MAXK; i++)
      if (s->k[i].on) s->k[i].finished = true, s->k[i].finish_tick = s->race_ticks;
    event(s, EV_FINISH, s->order[0], 1, s->k[s->order[0]].p, 0);
  }
}

/* sim_step's per-kart checks after the world step, in a battle: fell out of the arena, flipped, stuck */
static void battle_kart_after(sim *s, int i) {
  kart *k = &s->k[i];
  track_locate(s->track, k->p, k->hint, &k->loc);
  k->hint = k->loc.i;
  if (k->ram_cool > 0) k->ram_cool--;
  if (k->out) {
    if (!k->parked && s->race_ticks - k->out_tick >= OUT_LINGER) battle_park(s, i);
    return;
  }
  battle_teleports(s, i);
  bool lost = k->p.y < -14.0f;
  bool flipped = !k->grounded && kart_up(k).y < -0.3f && k->air_ticks > 30;
  if (s->phase == PH_RACE && k->respawn_ticks <= 0 && k->spin_ticks <= 0 && absf(k->speed) < 1.5f) k->stall_ticks++;
  else k->stall_ticks = 0;
  if (flipped) k->stuck_ticks++;
  else if (k->stuck_ticks > 0) k->stuck_ticks--;
  if (k->respawn_ticks <= 0 && (lost || k->stuck_ticks > 120 || k->stall_ticks > (k->human ? 300 : 170) || k->reset_hold > 55)) {
    if (lost) event(s, EV_FALL, i, 0, k->p, 0);
    if (getenv("RACER_FALLLOG")) cw_info("battle respawn kart %d tick %d lost=%d stuck=%d stall=%d reset=%d p=(%.1f %.1f %.1f) v=(%.1f %.1f %.1f)", i, s->tick, lost, k->stuck_ticks, k->stall_ticks, k->reset_hold, k->p.x, k->p.y, k->p.z - ARENA_CZ, k->v.x, k->v.y, k->v.z);
    battle_respawn(s, i);
  }
}

/* battle handling (kart_control calls it, battle only; a race never sees it): an arena is all tight turns and restarts,
 * so a kart under 17 m/s gets more drive (fading to none by then) and, under 12 m/s, the yaw the stick asks for even at
 * walking pace, where the tyres alone give almost none. Forward or in reverse (the stick turns the nose the same way). */
static void battle_handling(sim *s, int i, float thr, float steer, bool grounded) {
  kart *k = &s->k[i];
  if (!grounded || k->spin_ticks > 0 || k->out || s->phase != PH_RACE) return;
  float spd = k->speed, m = VEH[k->veh].mass;
  t3_vec3 f = kart_fwd(k), up = kart_up(k);
  float low = 1.0f - clampf(absf(spd) / 17.0f, 0, 1);
  /* only on something like level ground (a ramp is 12 degrees): a nose pushed up a wall gets no help climbing it */
  if (up.y < 0.9f) low = 0;
  if (thr > 0.05f && spd > -0.5f) cw_body3d_apply_force(PH.body[i], v3mul(f, thr * 14.0f * low * m), NULL);
  else if (thr < -0.05f && spd < 0.5f) cw_body3d_apply_force(PH.body[i], v3mul(f, thr * 5.0f * low * m), NULL);
  float crawl = 1.0f - clampf(absf(spd) / 12.0f, 0, 1);
  if (crawl > 0 && absf(steer) > 0.05f && absf(thr) > 0.05f) {
    t3_vec3 w = cw_body3d_angular_velocity(PH.body[i]);
    float wy = v3dot(w, up), want = -steer * 2.4f * crawl;
    float nw = wy + (want - wy) * 0.2f * crawl;
    cw_body3d_set_angular_velocity(PH.body[i], v3add(w, v3mul(up, nw - wy)));
  }
}

/* two karts crawling pressed together for a while (a scrum after a hit): a little shove apart, so neither has to back out */
static void battle_unscrum(sim *s) {
  if (s->phase != PH_RACE) return;
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on || k->out) continue;
    bool pressed = false;
    for (int j = 0; j < MAXK && !pressed; j++) {
      const kart *o = &s->k[j];
      if (j == i || !o->on || o->out) continue;
      float dx = o->p.x - k->p.x, dz = o->p.z - k->p.z;
      pressed = dx * dx + dz * dz < 3.0f * 3.0f && absf(k->speed) < 3 && absf(o->speed) < 3;
    }
    k->scrum_ticks = pressed ? k->scrum_ticks + 1 : 0;
    if (k->scrum_ticks < 24) continue;
    k->scrum_ticks = 0;
    for (int j = 0; j < MAXK; j++) {
      const kart *o = &s->k[j];
      if (j == i || !o->on || o->out) continue;
      t3_vec3 d = { k->p.x - o->p.x, 0, k->p.z - o->p.z };
      float l = v3len(d);
      if (l > 3.0f || l < 1e-3f) continue;
      cw_body3d_apply_impulse(PH.body[i], v3mul(d, 2.2f * VEH[k->veh].mass / l), NULL);
    }
  }
}
