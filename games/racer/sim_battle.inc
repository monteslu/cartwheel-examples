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
 * Item boxes stand where the arena puts them and come back six seconds after they are taken. */

#define BATTLE_TICKS (180 * TICK_HZ)
#define POP_GRACE 100   /* ticks after a pop before the same kart can lose another */
#define OUT_LINGER 80   /* ticks an eliminated kart stays (its spin plays out) before it is parked */
#define RAM_SPEED 6.0f  /* m/s closing speed of a turbo ram */
#define B_MISSILE_SPEED 44.0f
#define B_MISSILE_LIFE (int)(3.4f * TICK_HZ)
#define B_HOMING_LIFE (int)(4.2f * TICK_HZ)
#define B_BOUNCES 3

/* the items in battle: no place to weight by, so a kart with fewer balloons left draws more of the strong ones */
static const uint8_t BATTLE_W[IT_KINDS][BALLOONS] = {
  /*            1 left  2  3 */
  [IT_TURBO] = { 8, 10, 12 },
  [IT_TURBO3] = { 6, 5, 4 },
  [IT_MISSILE] = { 26, 28, 30 },
  [IT_HOMING] = { 18, 14, 10 },
  [IT_OIL] = { 8, 10, 12 },
  [IT_MINE] = { 14, 16, 18 },
  [IT_SHIELD] = { 14, 10, 7 },
  [IT_SHOCK] = { 6, 4, 2 },
};

static int roll_battle_item(sim *s, int i) {
  if (s->item_set == 1) return (rnd(s) % 3u) ? IT_TURBO : IT_TURBO3; /* the turbos-only preset: a battle of turbo rams */
  int b = s->k[i].balloons - 1;
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

/* stands kart i at (x, z) on whatever floor is there, facing yaw (radians, 0 = +z) */
static void battle_place(sim *s, int i, float x, float z, float yaw, float speed) {
  kart *k = &s->k[i];
  float sn, cs;
  cw_sincos(yaw, &sn, &cs);
  t3_vec3 f = { sn, 0, cs }, u = { 0, 1, 0 }, r = v3cross(f, u);
  float y = battle_floor(s, x, 30, z);
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
  kart_reset_state(k);
  k->speed = speed;
  k->hint = -1;
  track_locate(s->track, pos, -1, &k->loc);
  k->hint = k->loc.i;
  k->prog = 0;
  PH.prev_s[i] = k->loc.s;
}

static void battle_place_all(sim *s) {
  const arena_rt *a = s->arena;
  int n = 0;
  for (int i = 0; i < MAXK; i++) {
    kart *k = &s->k[i];
    if (!k->on) continue;
    /* the spawns are listed so that the first two face each other across the arena, the first four are a quarter turn apart */
    int sp = a->nspawn > 0 ? n % a->nspawn : 0;
    if (a->nspawn > 0) battle_place(s, i, a->spawn[sp][0], a->spawn[sp][1], a->spawn[sp][2], 0);
    else battle_place(s, i, (float)n * 4 - 14, ARENA_CZ, 0, 0);
    k->balloons = BALLOONS;
    k->points = k->pops_taken = 0;
    k->out = k->parked = false;
    k->out_tick = 0;
    k->ram_cool = 0;
    k->lap = 0;
    n++;
  }
  s->alive = n;
  s->battle_left = s->battle_limit;
  s->battle_end = BE_NONE;
  s->humans_out_tick = 0;
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

/* a hit that spun kart i (spin_kart): a balloon goes, the attacker scores, and maybe it is out */
static void battle_pop(sim *s, int i, int by) {
  kart *k = &s->k[i];
  if (k->out || k->balloons <= 0) return;
  k->balloons--;
  k->pops_taken++;
  k->ram_cool = POP_GRACE;
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
  int best = 0;
  float bd = -1;
  for (int sp = 0; sp < a->nspawn; sp++) {
    float dmin = 1e9f;
    for (int j = 0; j < MAXK; j++) {
      if (j == i || !s->k[j].on || s->k[j].out) continue;
      float dx = s->k[j].p.x - a->spawn[sp][0], dz = s->k[j].p.z - a->spawn[sp][1];
      dmin = minf(dmin, dx * dx + dz * dz);
    }
    if (dmin > bd) bd = dmin, best = sp;
  }
  if (a->nspawn > 0) battle_place(s, i, a->spawn[best][0], a->spawn[best][1], a->spawn[best][2], 6.0f);
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
    if (j == i || !o->on || o->out) continue;
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
  case IT_SHOCK:
    /* a ring out from the kart: everyone within 30 m gets the alarm (a hop clears it) */
    for (int j = 0; j < MAXK; j++) {
      kart *o = &s->k[j];
      if (j == i || !o->on || o->out || o->shock_warn) continue;
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

static void battle_hazards_step(sim *s) {
  for (int h = 0; h < MAXHZ; h++) {
    hazard *z = &s->hz[h];
    if (!z->kind) continue;
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
          if (j == i || !s->k[j].on || s->k[j].out) continue;
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
    if (ka->out || kv->out || ka->boost_ticks <= 0 || ka->spin_ticks > 0) continue;
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
      if (p->out != q->out) better = !q->out;
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

/* PH_RACE in a battle: the clock and the end of the round */
static void battle_phase(sim *s) {
  if (s->battle_left > 0) s->battle_left--;
  int humans = 0, humans_in = 0;
  for (int i = 0; i < MAXK; i++)
    if (s->k[i].on && s->k[i].human) humans++, humans_in += !s->k[i].out;
  if (humans > 0 && humans_in == 0 && s->humans_out_tick == 0) s->humans_out_tick = s->race_ticks;
  int end = BE_NONE;
  if (s->nkarts > 1 && s->alive <= 1) end = BE_LAST;
  else if (s->battle_left <= 0) end = BE_TIME;
  else if (s->humans_out_tick > 0 && s->race_ticks - s->humans_out_tick > 150) end = BE_HUMANS;
  if (end != BE_NONE) {
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
  bool lost = k->p.y < -14.0f;
  bool flipped = !k->grounded && kart_up(k).y < -0.3f && k->air_ticks > 30;
  if (s->phase == PH_RACE && k->respawn_ticks <= 0 && k->spin_ticks <= 0 && absf(k->speed) < 1.5f) k->stall_ticks++;
  else k->stall_ticks = 0;
  if (flipped) k->stuck_ticks++;
  else if (k->stuck_ticks > 0) k->stuck_ticks--;
  if (k->respawn_ticks <= 0 && (lost || k->stuck_ticks > 120 || k->stall_ticks > 300 || k->reset_hold > 55)) {
    if (lost) event(s, EV_FALL, i, 0, k->p, 0);
    battle_respawn(s, i);
  }
}
