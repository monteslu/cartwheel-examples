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
#define TURRET_BACK (15 * TICK_HZ) /* assault: a gun nest knocked out is back this long after (UT's cannons smoke, then wake) */
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
  [IT_FLAK] = { 12, 11, 10 },
  [IT_ORB] = { 8, 8, 8 },
  [IT_ROCKETS] = { 9, 7, 5 },
  [IT_RAIL] = { 7, 7, 6 },
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
static void battle_place_from(sim *s, int i, float x, float y, float z, float yaw, float speed) {
  kart *k = &s->k[i];
  battle_pose(s, i, x, y, z, yaw, speed);
  kart_reset_state(k);
  k->speed = speed;
  k->prog = 0;
}
static void battle_place(sim *s, int i, float x, float z, float yaw, float speed) { battle_place_from(s, i, x, 30, z, yaw, speed); }

void sim_battle_put(sim *s, int i, float x, float y, float z, float yaw_deg, float speed) {
  if (s->battle && i >= 0 && i < MAXK && s->k[i].on) battle_place_from(s, i, x, y, z + ARENA_CZ, yaw_deg * (CW_PI / 180.0f), speed);
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
  s->flag[t].away = 0;
  s->flag[t].p = s->base[t];
}

static void as_half_start(sim *s);
static void gates_set(sim *s);
static const float *as_spawn(const sim *s, int team, int k);
static int as_struck(const sim *s, t3_vec3 p, float pad);
static const assault_def *as_def(const sim *s);
static t3_vec3 as_at(const assault_obj *o);
static bool as_damage(sim *s, int o, int dmg, int by);
static void battle_place_all(sim *s) {
  const arena_rt *a = s->arena;
  const arena_def *d = a->def;
  int n = 0, nt[2] = { 0, 0 };
  gates_set(s); /* every gate up (an assault puts its own back as its half starts) */
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
    k->hp = KART_HP, k->hp_quiet = 0, k->blast_cool = 0;
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
  if (s->amode == AM_ASSAULT && s->arena->def->assault) {
    s->as_half = s->as_att = s->as_break = 0;
    s->as_res[1] = (struct as_result){ 0, 0, false };
    as_half_start(s);
  }
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
  s->as_time = ticks;
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
static void as_harm(sim *s, int i, int dmg, int by);
/* a hit that spun kart i (spin_kart): in a battle a balloon; capture the flag drops the flag it carries; king of the
 * mountain shoves it off the hill; an assault takes hit points (by the kind of hit). Every round: a shorter spin and a
 * moment's grace after it */
static void battle_hit(sim *s, int i, int by, int kind) {
  kart *k = &s->k[i];
  if (s->amode == AM_BALLOON) {
    battle_pop(s, i, by);
    return;
  }
  k->ram_cool = POP_GRACE;
  k->spin_ticks = (int)((float)k->spin_ticks * BATTLE_SPIN_K);
  if (by >= 0 && by < MAXK && by != i && s->k[by].on) s->k[by].points += mode_teams(s) ? 1 : 0;
  if (s->amode == AM_CTF) flag_drop(s, i, false);
  else if (s->amode == AM_ASSAULT) as_harm(s, i, kind == SPIN_BOOM ? 55 : kind == SPIN_OIL ? 15 : kind == SPIN_SHOCK ? 35 : 40, by);
  else if (s->amode == AM_KOTM) {
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
  if (s->amode == AM_ASSAULT && d->assault) {
    /* assault: one of its team's four for the stage the round is on, the one farthest from the other team */
    int best = 0;
    float bd = -1;
    for (int sp = 0; sp < 4; sp++) {
      const float *p = as_spawn(s, s->k[i].team, sp);
      float dmin = 1e9f;
      for (int j = 0; j < MAXK; j++) {
        if (j == i || !s->k[j].on || !kart_foe(s, i, j)) continue;
        float dx = s->k[j].p.x - p[0], dz = s->k[j].p.z - (p[1] + ARENA_CZ);
        dmin = minf(dmin, dx * dx + dz * dz);
      }
      if (dmin > bd) bd = dmin, best = sp;
    }
    const float *p = as_spawn(s, s->k[i].team, best);
    battle_place(s, i, p[0], p[1] + ARENA_CZ, p[2] * (CW_PI / 180.0f), 6.0f);
    kart *k = &s->k[i];
    k->respawn_ticks = 90, k->stall_ticks = 0, k->item_cool = 30, k->falls++;
    event(s, EV_RESPAWN, i, 0, k->p, 0);
    return;
  }
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

/* ── the arena weapons ────────────────────────────────────────────────────────────────────────────────
 *   flak burst   a cone of pellets (FLAK_LEN long, opening FLAK_SPREAD per metre): close in (under FLAK_CLOSE) a kart
 *                spins, farther a jolt; a target in the cone takes FLAK_DMG (half far off). Worked out when fired; the
 *                pellets are drawn only
 *   shock orb    a slow glowing ball straight out along the floor; the button again (the detonator) or a foe running
 *                into it sets it off (ORB_R: foes spin, targets ORB_DMG). A blaster bolt that hits it sets off the shock
 *                combo, half as wide again and stronger: the skill shot
 *   rocket pod   four missiles at once in a fan
 *   rail lance   RAIL_CHARGE ticks of charge with its aiming line drawn (the target can see it coming), then an instant
 *                shot straight on up to RAIL_LEN: the first foe on the line spins (and in an assault loses RAIL_HARM), a
 *                target takes RAIL_DMG */
#define FLAK_LEN 26.0f
#define FLAK_SPREAD 0.36f
#define FLAK_CLOSE 12.0f
#define FLAK_DMG 20
#define ORB_SPEED 20.0f
#define ORB_LIFE (int)(3.5f * TICK_HZ)
#define ORB_R 8.0f
#define ORB_DMG 30
#define RAIL_CHARGE 36
#define RAIL_LEN 140.0f
#define RAIL_DMG 30
#define RAIL_HARM 60

/* a jolt (flak far off): a quarter of its speed and a twist, and in an assault some armour */
static void battle_jolt(sim *s, int j, t3_vec3 dir, int harm, int by) {
  t3_vec3 v = cw_body3d_velocity(PH.body[j]);
  cw_body3d_set_velocity(PH.body[j], v3add(v3mul(v, 0.75f), v3mul(dir, 3.0f)));
  cw_body3d_apply_angular_impulse(PH.body[j], (t3_vec3){ 0, ((s->tick + j) & 1 ? 1.0f : -1.0f) * 0.8f * VEH[s->k[j].veh].mass, 0 });
  if (s->amode == AM_ASSAULT) as_harm(s, j, harm, by);
}

static void flak_burst(sim *s, int i, t3_vec3 f) {
  kart *k = &s->k[i];
  t3_vec3 r = { f.z, 0, -f.x };
  for (int j = 0; j < MAXK; j++) {
    kart *o = &s->k[j];
    if (j == i || !o->on || o->out || o->parked || o->respawn_ticks > 0 || !kart_foe(s, i, j)) continue;
    t3_vec3 d = v3sub(o->p, k->p);
    float along = v3dot(d, f), side = absf(v3dot(d, r));
    if (along < 1.0f || along > FLAK_LEN || side > along * FLAK_SPREAD + 1.2f || absf(d.y) > 3.0f) continue;
    float fr = 1;
    if (sim_ray(s, v3add(k->p, (t3_vec3){ 0, 0.4f, 0 }), d, &fr, NULL) && fr < 0.95f) continue;
    event(s, EV_HIT, j, i, o->p, (float)HZ_PELLET);
    if (along < FLAK_CLOSE) spin_kart(s, j, SPIN_HIT, i);
    else battle_jolt(s, j, f, 20, i);
  }
  /* the targets: their middle in the cone */
  const assault_def *d = as_def(s);
  for (int o = 0; d && s->amode == AM_ASSAULT && o < d->nobjs; o++) {
    const assault_obj *b = &d->objs[o];
    if (b->kind != AO_DESTROY && b->kind != AO_TURRET) continue;
    t3_vec3 dv = v3sub(as_at(b), k->p);
    float along = v3dot(dv, f), side = absf(v3dot(dv, r));
    if (along > 0 && along < FLAK_LEN + b->r && side < along * FLAK_SPREAD + b->r && absf(dv.y + 1.0f) < b->h + 3.0f)
      as_damage(s, o, along < FLAK_CLOSE + b->r ? FLAK_DMG : FLAK_DMG / 2, i);
  }
  /* the pellets, to be seen */
  for (int p = 0; p < 7; p++) {
    hazard *z = hz_add(s, HZ_PELLET, i, 0, 0, 0);
    if (!z) break;
    float a = ((float)p - 3.0f) * 0.11f + 0.03f * (float)((s->tick * 7 + p * 13) % 5 - 2), sn, cs;
    cw_sincos(a, &sn, &cs);
    z->f = (t3_vec3){ f.x * cs + r.x * sn, 0, f.z * cs + r.z * sn };
    z->p = v3add(v3add(k->p, v3mul(f, 2.2f)), (t3_vec3){ 0, 0.45f, 0 });
    z->speed = 95.0f;
    z->life = 17;
    battle_hz_place(s, z);
  }
}

static void orb_blast(sim *s, hazard *z, bool combo) {
  float r = combo ? ORB_R * 1.5f : ORB_R;
  event(s, EV_HIT, -1, z->owner, z->p, combo ? (float)HZ_BOMB : (float)HZ_ORB);
  for (int j = 0; j < MAXK; j++) {
    kart *o = &s->k[j];
    if (j == z->owner || !o->on || o->out || o->parked || o->respawn_ticks > 0 || hz_spares(s, z, j)) continue;
    t3_vec3 d = v3sub(o->p, z->p);
    if (v3dot(d, d) > r * r) continue;
    event(s, EV_HIT, j, z->owner, o->p, (float)HZ_ORB);
    spin_kart(s, j, v3dot(d, d) < r * r * 0.25f ? SPIN_BOOM : SPIN_HIT, z->owner);
  }
  assault_blast(s, z->p, r, combo ? ORB_DMG * 3 / 2 : ORB_DMG, z->owner);
  /* its owner's detonator goes with it */
  if (z->owner >= 0 && z->owner < MAXK && s->k[z->owner].item == IT_DETONATE) s->k[z->owner].item = IT_NONE, s->k[z->owner].item_count = 0;
  z->kind = HZ_NONE;
}

/* a shock orb's tick: straight on along the floor, glancing off nothing (a wall sets it off), a foe close sets it off */
static void orb_step(sim *s, hazard *z) {
  z->age++;
  if (--z->life <= 0) {
    orb_blast(s, z, false);
    return;
  }
  t3_vec3 step = v3mul(z->f, z->speed * DT);
  float fr = 0;
  t3_vec3 n;
  if (sim_ray(s, z->p, v3mul(step, 1.5f), &fr, &n) && absf(n.y) < 0.6f) {
    orb_blast(s, z, false);
    return;
  }
  z->p = v3add(z->p, step);
  float fl = battle_floor(s, z->p.x, z->p.y + 1.4f, z->p.z);
  if (fl < -50) z->p.y -= 0.3f;
  else z->p.y += (fl + 1.1f - z->p.y) * 0.3f;
  battle_hz_place(s, z);
  for (int j = 0; j < MAXK; j++) {
    kart *o = &s->k[j];
    if (j == z->owner || !o->on || o->out || o->respawn_ticks > 0 || hz_spares(s, z, j)) continue;
    t3_vec3 d = v3sub(o->p, z->p);
    if (v3dot(d, d) < 2.4f * 2.4f) {
      orb_blast(s, z, false);
      return;
    }
  }
  if (as_struck(s, z->p, 0.5f) >= 0) orb_blast(s, z, false);
}

/* the rail lance fires (its charge done): along the kart's heading as it is now */
static void rail_fire(sim *s, int i) {
  kart *k = &s->k[i];
  t3_vec3 f = flat_fwd(k), r = { f.z, 0, -f.x }, from = v3add(k->p, (t3_vec3){ 0, 0.5f, 0 });
  float len = RAIL_LEN, fr = 1;
  if (sim_ray(s, from, v3mul(f, RAIL_LEN), &fr, NULL)) len = RAIL_LEN * fr;
  /* the first foe on the line, or the first target */
  int hit = -1, obj = -1;
  float best = len;
  for (int j = 0; j < MAXK; j++) {
    kart *o = &s->k[j];
    if (j == i || !o->on || o->out || o->parked || o->respawn_ticks > 0 || !kart_foe(s, i, j)) continue;
    t3_vec3 d = v3sub(o->p, k->p);
    float along = v3dot(d, f);
    if (along > 1.0f && along < best && absf(v3dot(d, r)) < 1.7f && absf(d.y) < 2.5f) best = along, hit = j;
  }
  const assault_def *d = as_def(s);
  for (int o = 0; d && s->amode == AM_ASSAULT && o < d->nobjs; o++) {
    const assault_obj *b = &d->objs[o];
    if ((b->kind != AO_DESTROY && b->kind != AO_TURRET) || s->as_done[o]) continue;
    t3_vec3 dv = v3sub(as_at(b), k->p);
    float along = v3dot(dv, f) - b->r;
    if (along > 0 && along < best + 1.0f && absf(v3dot(dv, r)) < b->r + 0.5f && from.y > b->y - 1.0f && from.y < b->y + b->h + 1.0f) best = along, hit = -1, obj = o;
  }
  if (hit >= 0) {
    event(s, EV_HIT, hit, i, s->k[hit].p, (float)HZ_BEAM);
    spin_kart(s, hit, SPIN_HIT, i);
    if (s->amode == AM_ASSAULT) as_harm(s, hit, RAIL_HARM - 40, i); /* (the spin's 40 already taken) */
  }
  if (obj >= 0) as_damage(s, obj, RAIL_DMG, i);
  hazard *z = hz_add(s, HZ_BEAM, i, 0, 0, 0);
  if (z) {
    z->p = from, z->f = f, z->dist = hit >= 0 || obj >= 0 ? best : len, z->life = 20;
    battle_hz_place(s, z);
  }
}

/* the battle's items that leave something in the world; turbos and the shield work as in a race */
static bool battle_use_item(sim *s, int i, int item) {
  kart *k = &s->k[i];
  t3_vec3 f = flat_fwd(k);
  if (getenv("RACER_ITEMLOG")) cw_info("item: kart %d uses %s tick %d", i, ITEM_NAME[item], s->tick); /* dev aid */
  switch (item) {
  case IT_FLAK:
    flak_burst(s, i, f);
    return true;
  case IT_ORB: {
    hazard *z = hz_add(s, HZ_ORB, i, 0, 0, 0);
    if (!z) return true;
    z->p = v3add(v3add(k->p, v3mul(f, 3.0f)), (t3_vec3){ 0, 0.6f, 0 });
    z->f = f;
    z->speed = ORB_SPEED + maxf(k->speed, 0) * 0.4f;
    z->life = ORB_LIFE;
    battle_hz_place(s, z);
    return true;
  }
  case IT_DETONATE:
    for (int h = 0; h < MAXHZ; h++)
      if (s->hz[h].kind == HZ_ORB && s->hz[h].owner == i) orb_blast(s, &s->hz[h], false);
    return true;
  case IT_ROCKETS:
    for (int m = 0; m < 4; m++) {
      hazard *z = hz_add(s, HZ_MISSILE, i, 0, 0, 0);
      if (!z) break;
      float a = ((float)m - 1.5f) * 0.12f, sn, cs;
      cw_sincos(a, &sn, &cs);
      z->f = (t3_vec3){ f.x * cs + f.z * sn, 0, f.z * cs - f.x * sn };
      z->p = v3add(v3add(k->p, v3mul(z->f, 2.6f)), (t3_vec3){ 0, 0.25f, 0 });
      z->speed = B_MISSILE_SPEED + maxf(k->speed, 0) * 0.45f;
      z->life = B_MISSILE_LIFE;
      battle_hz_place(s, z);
    }
    return true;
  case IT_RAIL:
    k->rail_charge = RAIL_CHARGE;
    return true;
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

/* assault: kart i takes dmg hit points (from kart by, -1 a turret or nobody). At none it is wrecked: a blast where it was,
 * the one who did it scores, and it is back at its team's spawn for the stage whole again (UT's respawn: the attrition
 * that is the defenders' whole job). Hit points come back by themselves a while after the last hit */
#define HP_REGEN_AFTER (5 * TICK_HZ)
static void as_harm(sim *s, int i, int dmg, int by) {
  kart *k = &s->k[i];
  if (k->respawn_ticks > 0) return;
  k->hp -= dmg;
  k->hp_quiet = 0;
  if (k->hp > 0) return;
  if (by < 0) by = -1;
  event(s, EV_HIT, -1, by, k->p, (float)HZ_BOMB);
  event(s, EV_OUT, i, by == i ? -1 : by, k->p, 0);
  if (by >= 0 && by < MAXK && by != i && s->k[by].on) s->k[by].points += 2;
  k->pops_taken++;
  battle_respawn(s, i);
  k->falls--; /* (a wreck, not a fall) */
  k->hp = KART_HP;
}

static void as_regen(sim *s, int i) {
  kart *k = &s->k[i];
  if (k->hp >= KART_HP) return;
  if (++k->hp_quiet > HP_REGEN_AFTER && (k->hp_quiet & 7) == 0) k->hp++;
}

/* the assault blaster: what a kart fires with nothing else to use. A fast bolt straight out along the floor, gone at the
 * first wall; it chips BOLT_DMG off a target, and a kart it meets loses a quarter of its speed and gets a jolt (no spin:
 * that is for the real weapons) */
#define BOLT_SPEED 82.0f
#define BOLT_LIFE 40
#define BOLT_EVERY 20
#define BOLT_DMG 4
#define BOLT_HARM 9   /* hit points off a kart */
static void battle_bolt(sim *s, int i) {
  kart *k = &s->k[i];
  t3_vec3 f = flat_fwd(k);
  k->blast_cool = BOLT_EVERY;
  hazard *z = hz_add(s, HZ_BOLT, i, 0, 0, 0);
  if (!z) return;
  z->p = v3add(v3add(k->p, v3mul(f, 2.4f)), (t3_vec3){ 0, 0.35f, 0 });
  z->f = f;
  z->speed = BOLT_SPEED + maxf(k->speed, 0) * 0.5f;
  z->life = BOLT_LIFE;
  battle_hz_place(s, z);
  event(s, EV_ITEM_USE, i, IT_NONE, z->p, 0);
}

static void bolt_step(sim *s, hazard *z) {
  t3_vec3 step = v3mul(z->f, z->speed * DT);
  int struck = as_struck(s, v3add(z->p, step), 0.3f);
  if (struck >= 0) {
    as_damage(s, struck, BOLT_DMG, z->owner);
    event(s, EV_HIT, -1, z->owner, z->p, (float)HZ_BOLT);
    z->kind = HZ_NONE;
    return;
  }
  float fr = 0;
  t3_vec3 n;
  if (sim_ray(s, z->p, step, &fr, &n) && absf(n.y) < 0.6f) {
    event(s, EV_HIT, -1, z->owner, v3add(z->p, v3mul(step, fr)), (float)HZ_BOLT);
    z->kind = HZ_NONE;
    return;
  }
  z->p = v3add(z->p, step);
  float fl = battle_floor(s, z->p.x, z->p.y + 1.4f, z->p.z);
  if (fl < -50) z->p.y -= 0.3f;
  else z->p.y += (fl + 0.75f - z->p.y) * 0.45f;
  if (z->p.y < -20) {
    z->kind = HZ_NONE;
    return;
  }
  battle_hz_place(s, z);
  /* a shock orb on its way (anyone's): the shock combo */
  for (int h = 0; h < MAXHZ; h++) {
    hazard *o = &s->hz[h];
    if (o->kind != HZ_ORB) continue;
    t3_vec3 d = v3sub(o->p, z->p);
    if (v3dot(d, d) > 1.8f * 1.8f) continue;
    if (o->owner >= 0 && z->owner >= 0 && kart_foe(s, o->owner, z->owner)) o->owner = z->owner; /* (a foe's orb: the combo is the shooter's) */
    orb_blast(s, o, true);
    z->kind = HZ_NONE;
    return;
  }
  for (int j = 0; j < MAXK; j++) {
    kart *o = &s->k[j];
    if (j == z->owner || !o->on || o->out || o->parked || o->respawn_ticks > 0 || hz_spares(s, z, j)) continue;
    t3_vec3 d = v3sub(o->p, z->p);
    if (v3dot(d, d) > 1.7f * 1.7f) continue;
    t3_vec3 v = cw_body3d_velocity(PH.body[j]);
    float m = VEH[o->veh].mass;
    cw_body3d_set_velocity(PH.body[j], v3add(v3mul(v, 0.75f), v3mul(z->f, 2.5f)));
    cw_body3d_apply_angular_impulse(PH.body[j], (t3_vec3){ 0, ((s->tick + j) & 1 ? 1.0f : -1.0f) * 0.6f * m, 0 });
    if (z->owner >= 0 && z->owner < MAXK) s->k[z->owner].hits_given++;
    event(s, EV_HIT, j, z->owner, z->p, (float)HZ_BOLT);
    z->kind = HZ_NONE;
    as_harm(s, j, BOLT_HARM, z->owner < -1 ? -1 : z->owner);
    return;
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
    /* assault: it strikes a target once on its way, and turns for home */
    int struck = as_struck(s, z->p, 0.4f);
    if (struck >= 0 && !(z->hitmask & (1 << 30))) {
      z->hitmask |= 1 << 30;
      as_damage(s, struck, 8, z->owner);
      z->state = 1;
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
    if (hz_spares(s, z, i)) continue;
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
    if (z->kind == HZ_BOLT) {
      if (--z->life <= 0) z->kind = HZ_NONE;
      else z->age++, bolt_step(s, z);
      continue;
    }
    if (z->kind == HZ_ORB) {
      orb_step(s, z);
      continue;
    }
    if (z->kind == HZ_PELLET || z->kind == HZ_BEAM) {
      /* drawn only: a pellet flies on (gone at a wall), a beam stays where it was fired */
      z->age++;
      if (--z->life <= 0) z->kind = HZ_NONE;
      else if (z->kind == HZ_PELLET) {
        t3_vec3 step = v3mul(z->f, z->speed * DT);
        float fr = 0;
        if (sim_ray(s, z->p, step, &fr, NULL)) z->kind = HZ_NONE;
        else z->p = v3add(z->p, step);
      }
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
      /* assault: a target or a turret in the way: it bursts on it (an attacker's does damage) */
      int struck = as_struck(s, v3add(z->p, step), 0.4f);
      if (struck >= 0) {
        as_damage(s, struck, 10, z->owner);
        event(s, EV_HIT, -1, z->owner < -1 ? -1 : z->owner, z->p, (float)z->kind);
        z->kind = HZ_NONE;
        continue;
      }
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
      if (hz_spares(s, z, i)) continue; /* a teammate's: it passes through */
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
          if (j == i || !s->k[j].on || s->k[j].out || hz_spares(s, z, j)) continue;
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
      if (s->amode == AM_ASSAULT) {
        /* the winning team first (level: neither), then the points (an objective 5, a turret 2, a hit on a target 1) */
        int wp = s->win_team >= 0 && (p->team & 1) == s->win_team, wq = s->win_team >= 0 && (q->team & 1) == s->win_team;
        better = wq != wp ? wq > wp : q->points > p->points;
      } else if (s->amode == AM_CTF) {
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
    if (f->state != FL_HOME) f->away++;
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
    /* home with it, its own flag at home (or away so long the round would stall: both teams holding the other's flag at
     * their bases left whole rounds at 0-0): a capture */
    if (k->carry >= 0 && (mine->state == FL_HOME || mine->away >= FLAG_AWAY_SCORE) && battle_near(k->p, s->base[own], BASE_R)) {
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

/* ── assault ──────────────────────────────────────────────────────────────────────────────────────────
 * Two halves: red attacks the first, blue the second (sim.as_att). Every tick of a half: the zones (reach, hold), the turrets,
 * the stages; hits on targets come from the hazards (assault_hit, assault_blast) and turbo rams (assault_ram). A finished
 * stage takes its gates away (gates_set) and moves both teams' spawns up. The second half's clock is the first half's time
 * when the first attackers finished, and it ends as soon as the result is beaten. */
#define AS_BREAK (6 * TICK_HZ)
#define TURRET_EVERY (int)(2.6f * TICK_HZ)
#define TURRET_FIRST (int)(1.2f * TICK_HZ)

static const assault_def *as_def(const sim *s) { return s->battle && s->arena ? s->arena->def->assault : NULL; }
static bool as_goal(const assault_obj *o) { return o->kind != AO_TURRET; }
static t3_vec3 as_at(const assault_obj *o) { return (t3_vec3){ o->x, o->y, o->z + ARENA_CZ }; }

/* gate g is out of the way: its objective done, or its stage passed (in an assault; every gate stands in the other rounds) */
static bool gate_open(const sim *s, int g) {
  if (s->amode != AM_ASSAULT || !as_def(s)) return false;
  if (g > 0) return g - 1 < ASSAULT_MAXO && s->as_done[g - 1];
  return s->as_stage > -1 - g;
}

/* every gate's body there or not, as the round stands */
static void gates_set(sim *s) {
  for (int k = 0; k < PH.ngate; k++) {
    bool want = !gate_open(s, PH.gate[k]);
    if (want == PH.gate_up[k]) continue;
    if (want) {
      cw_body3d_def bd = cw_body3d_default_def();
      bd.type = CW_BODY_STATIC;
      bd.name = "gate";
      PH.gateb[k] = cw_body3d_create(PH.w, &bd);
      arena_phys_build(s->arena, PH.gateb[k], L_TRACK, L_WALL, PH.gate[k]);
    } else cw_body3d_destroy(PH.gateb[k]);
    PH.gate_up[k] = want;
  }
}

/* the turret's or target's team says nothing: a kart attacks in this half when it is on the attacking team */
static bool as_attacker(const sim *s, int i) { return i >= 0 && i < MAXK && s->k[i].on && (s->k[i].team & 1) == s->as_att; }

/* an objective that counts now: a goal of the stage the attackers are on (a turret: until its stage is passed) */
static bool as_live(const sim *s, int o) {
  const assault_def *d = as_def(s);
  const assault_obj *b = &d->objs[o];
  if (s->as_done[o] || s->as_break > 0 || s->phase != PH_RACE) return false;
  return b->kind == AO_TURRET ? b->stage >= s->as_stage : b->stage == s->as_stage;
}

static void as_stage_check(sim *s);
/* objective o is done (by kart i, -1 a team's hold) */
static void as_finish(sim *s, int o, int i) {
  const assault_def *d = as_def(s);
  s->as_done[o] = true;
  s->as_done_tick[o] = s->as_half_ticks;
  if (d->objs[o].kind == AO_TURRET) s->as_fire[o] = 0; /* (counting to its wake) */
  if (getenv("RACER_ASLOG")) cw_info("assault: objective %d done by %d tick %d half %d", o, i, s->as_half_ticks, s->as_half);
  bool last = as_goal(&d->objs[o]) && d->objs[o].stage == d->nstages - 1;
  if (i >= 0) s->k[i].points += d->objs[o].kind == AO_TURRET ? 3 : last ? 25 : 10, s->k[i].caps += d->objs[o].kind != AO_TURRET;
  event(s, EV_OBJ, i, d->objs[o].kind == AO_TURRET ? OE_TURRET_DOWN : OE_DONE, as_at(&d->objs[o]), (float)o);
  gates_set(s);
  if (as_goal(&d->objs[o])) {
    struct as_result *r = &s->as_res[s->as_half];
    r->done++;
    r->ticks = s->as_half_ticks;
  }
  as_stage_check(s);
}

/* dmg to objective o from kart by (an attacker's: the defenders' and the turrets' do nothing) */
static bool as_damage(sim *s, int o, int dmg, int by) {
  const assault_def *d = as_def(s);
  const assault_obj *b = &d->objs[o];
  if ((b->kind != AO_DESTROY && b->kind != AO_TURRET) || !as_live(s, o) || !as_attacker(s, by)) return false;
  int was = s->as_hp[o];
  s->as_hp[o] -= dmg;
  if (getenv("RACER_ASLOG")) cw_info("assault: kart %d hits objective %d for %d (%d left) tick %d", by, o, dmg, s->as_hp[o], s->as_half_ticks);
  s->k[by].points += 1;
  event(s, EV_OBJ, by, OE_HIT, as_at(b), (float)o);
  if (was * 2 > b->hp && s->as_hp[o] * 2 <= b->hp && s->as_hp[o] > 0) event(s, EV_OBJ, by, OE_HALFWAY, as_at(b), (float)o);
  if (s->as_hp[o] <= 0) s->as_hp[o] = 0, as_finish(s, o, by);
  return true;
}

/* p (a missile, a boomerang) is inside a target or a turret: the one it struck (-1 none); any team's shot bursts on it */
static int as_struck(const sim *s, t3_vec3 p, float pad) {
  const assault_def *d = as_def(s);
  if (!d || s->amode != AM_ASSAULT) return -1;
  for (int o = 0; o < d->nobjs; o++) {
    const assault_obj *b = &d->objs[o];
    if ((b->kind != AO_DESTROY && b->kind != AO_TURRET) || s->as_done[o]) continue;
    float dx = p.x - b->x, dz = p.z - (b->z + ARENA_CZ);
    if (dx * dx + dz * dz < (b->r + pad) * (b->r + pad) && p.y > b->y - 1.0f && p.y < b->y + b->h + 1.0f) return o;
  }
  return -1;
}

/* a blast at `at`: every target or turret within r of it takes dmg (from kart by) */
static void assault_blast(sim *s, t3_vec3 at, float r, int dmg, int by) {
  const assault_def *d = as_def(s);
  if (!d || s->amode != AM_ASSAULT) return;
  for (int o = 0; o < d->nobjs; o++) {
    const assault_obj *b = &d->objs[o];
    if (b->kind != AO_DESTROY && b->kind != AO_TURRET) continue;
    float dx = at.x - b->x, dz = at.z - (b->z + ARENA_CZ);
    if (dx * dx + dz * dz < (b->r + r) * (b->r + r) && at.y > b->y - r && at.y < b->y + b->h + r) as_damage(s, o, dmg, by);
  }
}

/* a turbo ram into a target: kart i under a boost, its nose into it at speed */
static void assault_ram(sim *s, int i) {
  kart *k = &s->k[i];
  if (k->flag_cool > 0) {
    k->flag_cool--;
    return;
  }
  if (k->boost_ticks <= 0 || k->speed < 10.0f || !as_attacker(s, i)) return;
  int o = as_struck(s, v3add(k->p, v3mul(flat_fwd(k), 2.2f)), 0.6f);
  if (o >= 0 && as_damage(s, o, 15, i)) {
    k->flag_cool = 45;
    event(s, EV_RAM, i, -1, k->p, k->speed);
  }
}

static int as_goals_total(const assault_def *d) {
  int n = 0;
  for (int st = 0; st < d->nstages; st++) {
    int c = 0;
    for (int o = 0; o < d->nobjs; o++) c += d->objs[o].stage == st && as_goal(&d->objs[o]);
    n += d->need[st] > 0 && d->need[st] < c ? d->need[st] : c;
  }
  return n;
}

/* the attackers' stage: done when enough of its goals are */
static void as_stage_check(sim *s) {
  const assault_def *d = as_def(s);
  while (s->as_stage < d->nstages) {
    int c = 0, done = 0;
    for (int o = 0; o < d->nobjs; o++)
      if (d->objs[o].stage == s->as_stage && as_goal(&d->objs[o])) c++, done += s->as_done[o];
    int need = d->need[s->as_stage] > 0 && d->need[s->as_stage] < c ? d->need[s->as_stage] : c;
    if (done < need) return;
    event(s, EV_OBJ, -1, OE_STAGE, (t3_vec3){ 0, 0, ARENA_CZ }, (float)s->as_stage);
    s->as_stage++;
    gates_set(s);
  }
  s->as_res[s->as_half].all = true;
}

/* a team's spawn for the stage the round is on: the k-th of its four (x, z, yaw degrees) */
static const float *as_spawn(const sim *s, int team, int k) {
  const assault_def *d = as_def(s);
  int st = s->as_stage < d->nstages ? s->as_stage : d->nstages - 1;
  return ((team & 1) == s->as_att ? d->att_spawns : d->def_spawns)[st * 4 + (k & 3)];
}

/* a half begins: every objective back, the gates up, both teams at the first stage's spawns */
static void as_half_start(sim *s) {
  const assault_def *d = as_def(s);
  s->as_stage = 0;
  s->as_half_ticks = 0;
  s->as_res[s->as_half] = (struct as_result){ 0, 0, false };
  for (int o = 0; o < d->nobjs && o < ASSAULT_MAXO; o++) {
    s->as_done[o] = false;
    s->as_done_tick[o] = 0;
    s->as_hp[o] = d->objs[o].kind == AO_HOLD || d->objs[o].kind == AO_REACH ? 0 : d->objs[o].hp;
    s->as_holder[o] = -1;
    s->as_fire[o] = TURRET_FIRST + o * 17;
  }
  gates_set(s);
  for (int h = 0; h < MAXHZ; h++) s->hz[h].kind = HZ_NONE;
  int nt[2] = { 0, 0 };
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on) continue;
    flag_drop(s, i, true);
    const float *sp = as_spawn(s, k->team, nt[k->team & 1]++);
    battle_place(s, i, sp[0], sp[1] + ARENA_CZ, sp[2] * (CW_PI / 180.0f), 0);
    k->flag_cool = 0;
    k->hp = KART_HP, k->hp_quiet = 0;
  }
  /* the second half's clock: the first half's time if its attackers finished, else the whole of it */
  s->battle_limit = s->as_half == 1 && s->as_res[0].all ? s->as_res[0].ticks : s->as_time > 0 ? s->as_time : d->time_s * TICK_HZ;
  s->battle_left = s->battle_limit;
}

/* the match's winner once the second half is over (UT99's rule: only breaking through counts, never the objectives short
 * of it). The second attackers through in their time (which is the first's): theirs. Not through: the first attackers' if
 * they were, a draw if neither was */
static int as_winner(const sim *s) {
  if (s->as_res[1].all) return 1;
  return s->as_res[0].all ? 0 : -1;
}

/* every tick of an assault: BE_NONE, or the end of the match */
static int assault_step(sim *s) {
  const assault_def *d = as_def(s);
  if (!d) return BE_NONE;
  if (s->as_break > 0) {
    if (--s->as_break == 0) {
      s->as_half = 1;
      s->as_att = 1;
      as_half_start(s);
      event(s, EV_OBJ, -1, OE_SWAP, (t3_vec3){ 0, 0, ARENA_CZ }, 1);
    }
    return BE_NONE;
  }
  s->as_half_ticks++;
  if (s->battle_left > 0) s->battle_left--;
  for (int i = 0; i < MAXK; i++)
    if (battle_able(&s->k[i])) as_regen(s, i);
  /* a gun nest knocked out wakes again (while its stage is still to pass), once nobody is parked where it stands */
  for (int o = 0; o < d->nobjs && o < ASSAULT_MAXO; o++) {
    const assault_obj *b = &d->objs[o];
    if (b->kind != AO_TURRET || !s->as_done[o] || b->stage < s->as_stage || ++s->as_fire[o] < TURRET_BACK) continue;
    bool clear = true;
    for (int i = 0; i < MAXK && clear; i++) clear = !s->k[i].on || !battle_near(s->k[i].p, as_at(b), b->r + 2.0f);
    if (!clear) continue;
    s->as_done[o] = false;
    s->as_hp[o] = b->hp;
    s->as_fire[o] = TURRET_FIRST;
    gates_set(s);
    event(s, EV_OBJ, -1, OE_TURRET_UP, as_at(b), (float)o);
  }
  for (int o = 0; o < d->nobjs && o < ASSAULT_MAXO; o++) {
    const assault_obj *b = &d->objs[o];
    if (!as_live(s, o)) continue;
    t3_vec3 at = as_at(b);
    if (b->kind == AO_REACH || b->kind == AO_HOLD) {
      int att = 0, def = 0, who = -1;
      for (int i = 0; i < MAXK; i++) {
        if (!battle_able(&s->k[i]) || !battle_near(s->k[i].p, at, b->r)) continue;
        if (as_attacker(s, i)) att++, who = i;
        else def++;
      }
      if (b->kind == AO_REACH && att > 0) as_finish(s, o, who);
      else if (b->kind == AO_HOLD) {
        /* (UT2004's timed hold: the count runs while an attacker is in it, whoever else is; it stops when the last one
         * leaves or is wrecked, and never runs back. -2: defenders in it too, fighting for it) */
        int holder = att > 0 && def == 0 ? who : att > 0 ? -2 : -1;
        if (att > 0 && ++s->as_hp[o] >= b->hp * TICK_HZ) as_finish(s, o, -1);
        s->as_holder[o] = holder;
      }
    } else if (b->kind == AO_TURRET && --s->as_fire[o] <= 0) {
      /* the turret: the nearest attacker in reach it can see, a homing missile at it from its foot */
      s->as_fire[o] = TURRET_EVERY;
      int best = -1;
      float bd = b->range * b->range;
      t3_vec3 eye = { at.x, at.y + b->h * 0.6f, at.z };
      for (int i = 0; i < MAXK; i++) {
        const kart *k = &s->k[i];
        if (!battle_able(k) || !as_attacker(s, i)) continue;
        t3_vec3 dv = v3sub(k->p, eye);
        float dd = dv.x * dv.x + dv.z * dv.z;
        if (dd > bd || dd < 4) continue;
        /* its own shapes are in the way of the ray at the start: look from its rim */
        float l = sqrtf(dd);
        t3_vec3 from = { eye.x + dv.x / l * (b->r + 0.5f), eye.y, eye.z + dv.z / l * (b->r + 0.5f) };
        float fr = 0;
        if (sim_ray(s, from, v3sub(k->p, from), &fr, NULL) && fr < 0.97f) continue;
        bd = dd, best = i;
      }
      if (best >= 0) {
        t3_vec3 dv = v3sub(s->k[best].p, at);
        dv.y = 0;
        t3_vec3 f = v3norm(dv);
        hazard *z = hz_add(s, HZ_HOMING, -2 - (s->as_att ^ 1), 0, 0, 0);
        if (z) {
          z->p = (t3_vec3){ at.x + f.x * (b->r + 1.6f), at.y + 0.8f, at.z + f.z * (b->r + 1.6f) };
          z->f = f;
          z->speed = B_MISSILE_SPEED * 0.72f;
          z->life = B_HOMING_LIFE;
          z->target = best;
          z->age = 6;
          battle_hz_place(s, z);
          event(s, EV_OBJ, best, OE_FIRE, z->p, (float)o);
        }
      }
    }
  }
  for (int i = 0; i < MAXK; i++)
    if (battle_able(&s->k[i])) assault_ram(s, i);
  /* the half's end: the attackers through every stage, or the clock out (the defenders held) */
  bool over = s->as_res[s->as_half].all || s->battle_left <= 0;
  if (!over) return BE_NONE;
  event(s, EV_OBJ, -1, OE_HALF, (t3_vec3){ 0, 0, ARENA_CZ }, (float)s->as_half);
  if (s->as_half == 0) {
    s->as_break = AS_BREAK;
    return BE_NONE;
  }
  s->win_team = as_winner(s);
  s->team_score[0] = s->as_res[0].done, s->team_score[1] = s->as_res[1].done;
  return s->as_res[1].all ? BE_SCORE : BE_TIME;
}

/* PH_RACE in a battle: the clock and the end of the round */
static void battle_phase(sim *s) {
  int as_end = BE_NONE;
  if (s->amode == AM_ASSAULT) as_end = assault_step(s); /* (its own clock: it stands between the halves) */
  else if (s->battle_left > 0) s->battle_left--;
  if (s->amode == AM_CTF) ctf_step(s);
  else if (s->amode == AM_KOTM) kotm_step(s);
  int humans = 0, humans_in = 0;
  for (int i = 0; i < MAXK; i++)
    if (s->k[i].on && s->k[i].human) humans++, humans_in += !s->k[i].out;
  if (humans > 0 && humans_in == 0 && s->humans_out_tick == 0) s->humans_out_tick = s->race_ticks;
  int end = as_end;
  if (s->amode == AM_ASSAULT) {
    if (as_end == BE_NONE) return;
  } else if (s->amode == AM_CTF && (s->team_score[0] >= CTF_WIN || s->team_score[1] >= CTF_WIN)) end = BE_SCORE;
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
  if (k->rail_charge > 0 && --k->rail_charge == 0) rail_fire(s, i);
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
