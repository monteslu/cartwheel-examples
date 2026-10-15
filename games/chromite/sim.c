/* chromite simulation: the original's rules on a fixed 60 Hz tick.
 *
 * Everything that feeds game state is here, on cartwheel/math.h and cw_rng
 * only, so every host plays the same game from the same input. The numbers
 * are the original's (per 50 fps frame; see game.h), from its wasmcart port:
 * the slower enemies and ground and the quicker default gun it tuned. */
#include <string.h>

#include "game.h"

const int ENEMY_SCORE[EK_KINDS] = { 75, 25, 1000, 1500, 10, 5000, 5000 };
static const float HALF_W[EK_KINDS] = { 0.75f, 0.7f, 1.2f, 1.9f, 0.45f, 3.5f, 2.6f };
static const float HALF_H[EK_KINDS] = { 1.02f, 0.7f, 1.2f, 2.1f, 0.45f, 2.275f, 2.3f };
static const float COLLIDE_MOVE[EK_KINDS] = { 0.5f, 0.7f, 0.2f, 0.1f, 0.0f, 0.05f, 0.1f };
static const float EAMMO_DMG[EA_KINDS] = { 75, 6, 100, 20, 8.5f };
float enemy_half_w(int kind) { return HALF_W[kind]; }
float enemy_half_h(int kind) { return HALF_H[kind]; }

/* per-frame decays as per-tick factors, d^K (cw_pow: the same bits everywhere) */
static float D90, D975, D98, D99, D998, D982;
static void decays(void) {
  D90 = cw_pow(0.9f, K);
  D975 = cw_pow(0.975f, K);
  D98 = cw_pow(0.98f, K);
  D99 = cw_pow(0.99f, K);
  D998 = cw_pow(0.998f, K);
  D982 = cw_pow(0.982f, K);
}

static float frand(sim *S) { return cw_rng_float(&S->rng); }
static float srand1(sim *S) { return cw_rng_float(&S->rng) * 2 - 1; }
static void event(sim *S, int type, int a, float x, float y) {
  if (S->ev_n < MAX_EVENTS) S->ev[S->ev_n++] = (sim_event){ type, a, x, y };
}

/* did a counter in frames cross a multiple of n this tick (the port's ageInterval) */
static bool crossed(float age, int n) {
  float prev = age - K;
  if (prev < 0) prev = 0;
  return (int)age / n != (int)prev / n;
}

bool sim_hero_visible(const sim *S) { return S->h.dont_show <= 0 && S->mode != PLAY_DEAD; }

/* where enemies aim: the hero, or a phantom loop while the hero is away */
static void aim_point(const sim *S, float *x, float *y) {
  if (S->h.dont_show > 1) {
    float t = S->frame / 50.0f;
    *x = cw_cos(t) * 9;
    *y = 4 + cw_sin(t * 3.5f) * 2;
  } else {
    *x = S->h.x;
    *y = S->h.y;
  }
}

/* ── spawning ──────────────────────────────────────────────────────── */

static int enemy_add(sim *S, int kind, float x, float y, float rand_fact) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    enemy *e = &S->en[i];
    if (e->live) continue;
    memset(e, 0, sizeof *e);
    e->live = true;
    e->kind = kind;
    e->id = ++S->next_id;
    if (x < -(FIELD_X - 2)) x = -(FIELD_X - 2);
    if (x > FIELD_X - 2) x = FIELD_X - 2;
    e->x = x, e->y = y;
    e->shoot_interval = 1;
    e->rand_fact = rand_fact;
    e->rand_move = rand_fact * frand(S);
    e->target = -1;
    e->phase = frand(S) * 6.2831853f;
    float fr = frand(S) * rand_fact;
    switch (kind) {
    case EK_STRAIGHT: e->hp = -110 * S->skill, e->lmy = (-0.046f - fr * 0.04f) * 0.6f; break;
    case EK_OMNI: e->hp = -45, e->lmy = (-0.071f - fr * 0.04f) * 0.6f; break;
    case EK_RAYGUN: e->hp = -1000 * S->skill, e->lmy = 0, e->vel = 0.03f * 0.6f, e->rand_move = 0.5f + 0.5f * e->rand_move; break;
    case EK_TANK: e->hp = -2000 * S->skill, e->vel = 0.03f * 0.6f; break;
    case EK_GNAT: e->hp = -10, e->rand_move = 0.5f + 0.5f * e->rand_move, e->lmx = 0.2f, e->lmy = 0.1f * 0.6f; break;
    case EK_BOSS0: e->hp = -10000 * S->skill, e->vel = 0.02f; break;
    case EK_BOSS1: e->hp = -10000 * S->skill, e->vel = 0.02f, e->age = 600; break;
    }
    e->max_hp = -e->hp;
    if (kind >= EK_BOSS0) S->boss = i;
    return i;
  }
  return -1;
}

static void ebullet_add(sim *S, int kind, float x, float y, float vx, float vy) {
  for (int i = 0; i < MAX_EBULLETS; i++) {
    ebullet *b = &S->eb[i];
    if (b->live) continue;
    *b = (ebullet){ true, kind, x, y, vx, vy };
    return;
  }
}
static void hbullet_add(sim *S, int gun, float x, float y, float vy, float dmg) {
  for (int i = 0; i < MAX_HBULLETS; i++) {
    hbullet *b = &S->hb[i];
    if (b->live) continue;
    *b = (hbullet){ true, gun, x, y, vy, dmg, 0 };
    return;
  }
}
static void powerup_add(sim *S, int kind, float x, float y, float vx, float vy) {
  for (int i = 0; i < MAX_POWERUPS; i++) {
    powerup *p = &S->pu[i];
    if (p->live) continue;
    *p = (powerup){ true, kind, x, y, vx, vy, 0 };
    return;
  }
}

/* the level script: items whose time has come */
static void release_script(sim *S) {
  while (S->script_i < S->script_n && S->script[S->script_i].at <= (int)S->frame) {
    const script_item *it = &S->script[S->script_i++];
    if (it->kind >= 100) powerup_add(S, it->kind - 100, it->x, it->y, 0, 0);
    else enemy_add(S, it->kind, it->x, it->y, it->arrow ? 0 : 1);
  }
}

/* ── the hero ──────────────────────────────────────────────────────── */

static void hero_reset(sim *S) {
  hero *h = &S->h;
  h->x = 0, h->y = -3;
  h->smx = h->smy = 0;
  h->dont_show = 0;
  h->damage = -HERO_HULL;
  h->shields = HERO_SHIELDS;
  for (int g = 0; g < GUNS; g++) h->ammo[g] = 0, h->gun_on[g] = g == GUN_DEFAULT, h->cool[g] = 0;
}

static void start_death(sim *S) {
  hero *h = &S->h;
  event(S, EV_HERO_DIE, 0, h->x, h->y);
  S->deaths++;
  if (h->lives < 0) {
    S->mode = PLAY_DEAD;
    S->mode_ticks = 0;
    S->hero_death = 50;
    h->dont_show = -1;
    return;
  }
  h->bomb = 1;
  event(S, EV_BOMB, 0, 0, -15);
  hero_reset(S);
  h->dont_show = 130;
}

static void hero_damage(sim *S, float d) {
  hero *h = &S->h;
  if (h->shields > HERO_SHIELDS) {
    h->shields -= d * 0.25f;
    h->shield_flash = 1;
  } else if (h->shields > 0) {
    h->shields -= d * 0.8f;
    if (h->shields < 0) h->shields = 0;
    h->damage += d * 0.2f;
    h->shield_flash = 1;
  } else {
    h->damage += d;
    h->damage_flash = 1;
  }
  if (h->damage > 0) {
    h->lives--;
    h->damage = 0;
    start_death(S);
  }
}

static void add_life(sim *S) {
  hero *h = &S->h;
  if (h->lives < 9) {
    h->lives++;
    event(S, EV_LIFE, 0, h->x, h->y);
  } else if (!h->bomb) {
    h->bomb = 1;
    event(S, EV_BOMB, 0, 0, -15);
  }
}

static void fire(sim *S) {
  hero *h = &S->h;
  /* the default gun: two bolts, four with its extra stock */
  if (h->cool[GUN_DEFAULT] <= 0) {
    hbullet_add(S, GUN_DEFAULT, h->x + 0.3f, h->y + 0.8f, 0.5f, 3.5f);
    hbullet_add(S, GUN_DEFAULT, h->x - 0.3f, h->y + 0.8f, 0.5f, 3.5f);
    if (h->ammo[GUN_DEFAULT] > 0) {
      hbullet_add(S, GUN_DEFAULT, h->x + 0.45f, h->y + 0.2f, 0.5f, 3.5f);
      hbullet_add(S, GUN_DEFAULT, h->x - 0.45f, h->y + 0.2f, 0.5f, 3.5f);
      h->ammo[GUN_DEFAULT] -= 0.5f;
      if (h->ammo[GUN_DEFAULT] < 0) h->ammo[GUN_DEFAULT] = 0;
    }
    h->cool[GUN_DEFAULT] = 3;
    event(S, EV_SHOT, GUN_DEFAULT, h->x, h->y);
    S->shots++;
  }
  /* the green lance: slow, piercing, burns while it overlaps */
  if (h->ammo[GUN_GREEN] > 0 && h->cool[GUN_GREEN] <= 0) {
    hbullet_add(S, GUN_GREEN, h->x, h->y + 1.1f, 0.2f, 6);
    h->ammo[GUN_GREEN] -= 1.5f;
    if (h->ammo[GUN_GREEN] < 0) h->ammo[GUN_GREEN] = 0;
    h->cool[GUN_GREEN] = 25;
    event(S, EV_SHOT, GUN_GREEN, h->x, h->y);
  }
  /* the purple hammers: heavy rounds from alternate wings */
  if (h->ammo[GUN_PURPLE] > 0 && h->cool[GUN_PURPLE] <= 0) {
    h->gun2_side = !h->gun2_side;
    hbullet_add(S, GUN_PURPLE, h->x + (h->gun2_side ? 0.7f : -0.7f), h->y + 0.4f, 0.3f, 40);
    h->ammo[GUN_PURPLE] -= 1.5f;
    if (h->ammo[GUN_PURPLE] < 0) h->ammo[GUN_PURPLE] = 0;
    h->cool[GUN_PURPLE] = 5;
    event(S, EV_SHOT, GUN_PURPLE, h->x, h->y);
  }
}

/* the self-destruct: eject the stocks as power-ups, then go up in a bomb */
static void use_item(sim *S) {
  hero *h = &S->h;
  if (h->bomb > 0 || !sim_hero_visible(S)) return;
  if (h->armed <= 0) {
    h->armed = 1;
    event(S, EV_ARMED, 0, h->x, h->y);
    return;
  }
  for (int g = 0; g < GUNS; g++)
    if (h->ammo[g] > 1) {
      float vx = srand1(S) * 0.15f, vy = 0.1f + frand(S) * 0.1f;
      powerup_add(S, PU_AMMO0 + g, h->x, h->y, vx, vy);
    }
  h->armed = 0;
  h->damage = 0;
  h->shields = 0;
  h->lives--;
  start_death(S);
}

static void hero_step(sim *S, const sim_input *in) {
  hero *h = &S->h;
  if (S->mode == PLAY_DEAD) return;
  if (h->dont_show > 0) {
    h->dont_show -= K;
    if (h->dont_show <= 0) {
      h->dont_show = 0;
      h->x = 0, h->y = -3;
      event(S, EV_RESPAWN, 0, h->x, h->y);
    }
  }
  bool live = h->dont_show <= 0;
  if (live) {
    h->x += in->x * 8 * 0.03f * K;
    h->y += in->y * 8 * 0.03f * K;
  }
  for (int g = 0; g < GUNS; g++) h->cool[g] -= K;
  if (live && in->fire && S->mode == PLAY_ON) fire(S);
  if (in->item && S->mode == PLAY_ON) use_item(S);
  if (h->armed > 0) h->armed -= 0.02f * K;
  if (h->armed < 0) h->armed = 0;
  if (h->shields >= HERO_SHIELDS) h->shields -= 0.15f * K;
  float s = (1 - K) + K * 0.8f;
  h->smx *= s, h->smy *= s;
  h->x += h->smx * K, h->y += h->smy * K;
  if (h->x < -HERO_X) h->x = -HERO_X;
  if (h->x > HERO_X) h->x = HERO_X;
  if (h->y < -HERO_Y) h->y = -HERO_Y;
  if (h->y > HERO_Y) h->y = HERO_Y;
  h->shield_flash *= 0.94f, h->damage_flash *= 0.94f;
  float want = cw_clampf((h->damage + HERO_HULL) / HERO_HULL, 0, 1);
  h->engine_heat += (want - h->engine_heat) * 0.05f;
  /* an extra life every 50000 */
  if (live && S->score >= S->next_life) {
    S->next_life += LIFE_SCORE;
    add_life(S);
  }
}

/* ── enemies ───────────────────────────────────────────────────────── */

static void move_clamp(enemy *e) {
  if (e->x < -FIELD_X) e->x = -FIELD_X;
  if (e->x > FIELD_X) e->x = FIELD_X;
}

static void enemy_step(sim *S, int idx) {
  enemy *e = &S->en[idx];
  float hx, hy;
  aim_point(S, &hx, &hy);
  e->age += K;
  e->shoot_interval -= K;
  e->x += e->smx * K, e->y += e->smy * K;
  float s = (1 - K) + K * 0.7f;
  e->smx *= s, e->smy *= s;
  if (e->hit_flash > 0) e->hit_flash -= 0.08f;
  /* steering targets: the hero (or the phantom), or a gnat's boss */
  float tx = hx, ty = hy;
  if (e->target >= 0 && S->en[e->target].live) tx = S->en[e->target].x, ty = S->en[e->target].y;
  else e->target = -1;
  float dx = tx - e->x, dy = ty - e->y;
  float a = hx - e->x, b = hy - e->y;
  switch (e->kind) {
  case EK_STRAIGHT:
    e->y += K * e->lmy * S->skill;
    move_clamp(e);
    e->pre_fire = e->shoot_interval < 10 ? (10 - e->shoot_interval) / 10 : 0;
    if (e->shoot_interval <= 0) {
      e->shoot_interval = 30 + frand(S) * 90;
      ebullet_add(S, EA_HEAVY, e->x, e->y - 0.9f, 0, -0.2f);
      event(S, EV_ESHOT, EA_HEAVY, e->x, e->y);
    }
    break;
  case EK_OMNI: {
    e->lmx = D90 * e->lmx + 0.1f * K * 0.01f * dx;
    e->x += K * e->rand_move * e->lmx;
    e->y += K * e->lmy * S->skill;
    move_clamp(e);
    if (e->swap >= 108 * 6) e->swap = 0;
    int sw = e->swap / 6; /* swap counts sixths of a frame (an integer in ticks) */
    if (sw < 18 && crossed(e->age, 1)) {
      if (!(sw % 6)) {
        float dist = fabsf(a) + fabsf(b);
        if (dist < 0.001f) dist = 0.001f;
        e->aim_x = 0.3f * S->skill * a / dist;
        e->aim_y = 0.3f * S->skill * b / dist;
      }
      ebullet_add(S, EA_FLAK, e->x, e->y, e->aim_x, e->aim_y);
      if (!(sw % 6)) event(S, EV_ESHOT, EA_FLAK, e->x, e->y);
    }
    if (e->y < FIELD_Y) e->swap += 5; /* K frames: 5 sixths */
    break;
  }
  case EK_RAYGUN: {
    float ddx = dx, ddy = dy, wob = 0;
    if (fabsf(ddx) < 3) wob = ((3 - fabsf(ddx)) / 3) * (0.1f * cw_sin(e->age * 0.25f));
    if (fabsf(ddy) < 7) ddy *= 0.1f;
    e->lmx = D975 * e->lmx + 0.002f * K * ddx;
    e->lmy = D90 * e->lmy + 0.001f * K * ddy;
    e->x += K * (e->rand_move * e->lmx * (S->skill + 0.1f) + wob);
    e->y += K * (e->lmy + e->vel * (S->skill + 0.1f));
    move_clamp(e);
    if (fabsf(a) < 1.5f && crossed(e->age, 1)) {
      ebullet_add(S, EA_BEAM, e->x, e->y - 0.5f, 0, -0.6f);
      e->pre_fire = 1;
    } else e->pre_fire *= 0.9f;
    break;
  }
  case EK_TANK: {
    float v1 = fabsf(dx) > 8 ? 0.04f : 0.04f * (fabsf(dx) / 8);
    e->vel = D99 * e->vel + (1 - D99) * v1;
    if (e->y < -3) e->vel = -0.1f;
    else if (e->y < 0) e->vel *= D99;
    float side = e->x < 0 ? -FIELD_X + 2.85f : FIELD_X - 2.85f;
    e->x = D998 * e->x + (1 - D998) * side;
    if (((int)e->age / 50) % 8 == 2) e->y += K * 0.05f;
    else e->y -= K * e->vel;
    move_clamp(e);
    if (fabsf(a) < 4) {
      int sw = e->swap / 6;
      if ((sw == 0 || sw == 8 || sw == 16) && crossed(e->age, 1)) {
        ebullet_add(S, EA_HEAVY, e->x + 1.5f, e->y - 1.7f, 0, -0.2f);
        ebullet_add(S, EA_HEAVY, e->x - 1.5f, e->y - 1.7f, 0, -0.2f);
        event(S, EV_ESHOT, EA_HEAVY, e->x, e->y);
      }
      e->swap += 5;
      if (e->swap >= 100 * 6) e->swap = 0;
    }
    if (!(((int)e->age / 200) % 2)) {
      int t = (int)e->age % 200;
      if (t < 100) e->pre_fire = t / 100.0f;
      else if (t < 170) {
        if (crossed(e->age, 10)) {
          float px = e->x, py = e->y - 0.45f, aa = hx - px, bb = hy - py, dist = fabsf(aa) + fabsf(bb);
          if (dist < 0.001f) dist = 0.001f;
          float vx = 0.7f * aa / dist, vy = 0.7f * bb / dist;
          e->smx -= vx * 0.1f, e->smy -= vy * 0.1f;
          ebullet_add(S, EA_SLUG, px, py, vx, vy);
          event(S, EV_ESHOT, EA_SLUG, px, py);
          e->pre_fire -= 0.4f * K;
          if (e->pre_fire < 0) e->pre_fire = 0;
        } else e->pre_fire += 0.035f * K;
      } else e->pre_fire = 0;
    }
    break;
  }
  case EK_GNAT: {
    bool at_hero = e->target < 0;
    float rx = at_hero ? e->rand_move : 0.75f + frand(S) * 0.15f;
    const float R = 3.8f;
    float dist = sqrtf(dx * dx + dy * dy) * rx;
    if (dist < 0.0001f) dist = 0.0001f;
    float td = 0.4f + 0.6f * ((dist + 0.2f * cw_sin(e->age * 0.001f)) / R);
    float speed = td * 0.25f * rx;
    float x = speed * dx / dist, y = speed * dy / dist;
    if (dist < R) {
      float tx2 = x, ty2 = y;
      x = td * tx2 + -(1 - td) * dy / td;
      y = td * ty2 + (1 - td) * dx / td;
      y += 0.01f * cw_sin(S->frame / 50.0f * 0.05f);
    } else {
      float d2 = 0.97f, tx2 = x, ty2 = y;
      if (rx < 0.65f) x = d2 * tx2 + (1 - d2) * dy / d2, y = d2 * ty2 + -(1 - d2) * dx / d2;
      else x = d2 * tx2 + -(1 - d2) * dy / d2, y = d2 * ty2 + (1 - d2) * dx / d2;
    }
    float blend = 1 - cw_pow(0.85f - rx * 0.2f, K);
    float v0 = (((int)e->age / 8) % 2) ? e->lmx + blend * ((rx - 0.2f) * x - e->lmx) : e->lmx;
    float v1 = e->lmy + blend * ((rx - 0.2f) * y - e->lmy);
    if (e->age < 50) {
      float amt = e->age > 20 ? (e->age - 20) / 30.0f : 0;
      e->lmx = (1 - amt) * e->lmx + amt * v0;
      e->lmy = (1 - amt) * e->lmy + amt * v1;
    } else e->lmx = v0, e->lmy = v1;
    e->x += K * e->lmx;
    e->y += K * e->lmy;
    if (e->y < -10) e->y = -10;
    move_clamp(e);
    if (e->shoot_interval <= 0) {
      e->shoot_interval = 1 + frand(S) * 5;
      if (fabsf(a) < 2 && b < 0) {
        ebullet_add(S, EA_NEEDLE, e->x, e->y - 0.5f, 0, -0.39f);
        event(S, EV_ESHOT, EA_NEEDLE, e->x, e->y);
      }
    }
    break;
  }
  case EK_BOSS0: {
    float approach = 7 * (2 - S->skill), ddx = dx, ddy = dy;
    if (fabsf(ddy) < approach) ddy = ddy * ddy / approach;
    ddx += 5 * cw_sin(e->age * 0.1f);
    e->lmx = D98 * e->lmx + 0.0005f * K * S->skill * ddx;
    e->lmy = D90 * e->lmy + 0.001f * K * S->skill * ddy;
    e->x += K * e->lmx;
    e->y += K * (e->lmy + e->vel);
    move_clamp(e);
    if (fabsf(a) < 1.6f && crossed(e->age, 1)) ebullet_add(S, EA_BEAM, e->x, e->y - 1.7f, 0, -0.6f);
    if (crossed(e->age, 5)) {
      e->swap++;
      if (e->swap >= 15) e->swap = 0;
      if (e->swap < 6) {
        float o = 2.0f + (e->swap % 3) * 0.4f;
        ebullet_add(S, EA_HEAVY, e->x + o, e->y - 1.9f, 0, -0.2f);
        ebullet_add(S, EA_HEAVY, e->x - o, e->y - 1.9f, 0, -0.2f);
        event(S, EV_ESHOT, EA_HEAVY, e->x, e->y);
      }
    }
    if (crossed(e->age, 7)) {
      float dist = fabsf(a) + fabsf(b);
      if (dist < 0.001f) dist = 0.001f;
      e->aim_x = 0.35f * a / dist, e->aim_y = 0.35f * b / dist;
    }
    if (!(((int)e->age / 200) % 2)) {
      if (!(((int)e->age / 100) % 2)) {
        if (!(((int)e->age / 50) % 2) && crossed(e->age, 1)) {
          ebullet_add(S, EA_FLAK, e->x - 1.1f, e->y - 0.45f, e->aim_x, e->aim_y);
          ebullet_add(S, EA_FLAK, e->x + 1.1f, e->y - 0.45f, e->aim_x, e->aim_y);
        }
        e->pre_fire = ((int)e->age % 100) / 100.0f;
      } else if (crossed(e->age, 10)) {
        for (int sd = -1; sd <= 1; sd += 2) {
          float px = e->x + 1.1f * sd, py = e->y - 0.45f, aa = hx - px, bb = hy - py, dist = fabsf(aa) + fabsf(bb);
          if (dist < 0.001f) dist = 0.001f;
          ebullet_add(S, EA_SLUG, px, py, 0.7f * aa / dist, 0.7f * bb / dist);
        }
        event(S, EV_ESHOT, EA_SLUG, e->x, e->y);
        e->pre_fire -= 0.4f * K;
        if (e->pre_fire < 0) e->pre_fire = 0;
      } else e->pre_fire += 0.035f * K;
    } else e->pre_fire = 0;
    break;
  }
  case EK_BOSS1: {
    float approach = ((((int)e->age + 25) / 512) % 2) ? 9 * (2 - S->skill) : 12 * (2 - S->skill), ddx = dx, ddy = dy;
    if (fabsf(ddy) < approach + 2 * cw_sin(S->frame / 50.0f * 2.5f)) ddy = ddy * ddy / approach;
    ddx += 5 * cw_sin(e->age * 0.1f);
    bool aggressive = ((int)e->age / 512) % 2;
    if (aggressive) {
      e->lmx = D98 * e->lmx + 0.001f * K * S->skill * ddx;
      e->lmy = D90 * e->lmy + 0.002f * K * S->skill * ddy;
    } else {
      e->lmx = D90 * e->lmx + 0.0003f * K * S->skill * ddx;
      e->lmy = D90 * e->lmy + 0.001f * K * S->skill * ddy;
    }
    e->x += K * e->lmx;
    e->y += K * (e->lmy + e->vel);
    move_clamp(e);
    if (fabsf(a) < 5) {
      e->pre_fire = (e->age - 6.0f * floorf(e->age / 6.0f)) / 6.0f;
      if (crossed(e->age, 6)) {
        e->swap = !e->swap;
        float px = e->swap ? e->x + 0.55f : e->x - 1.22f, py = e->swap ? e->y - 1.7f : e->y - 1.22f;
        ebullet_add(S, EA_HEAVY, px, py, 0, -0.65f);
        ebullet_add(S, EA_HEAVY, px, py + 0.5f, 0, -0.65f);
        event(S, EV_ESHOT, EA_HEAVY, px, py);
      }
    } else if (e->pre_fire > 0) e->pre_fire -= 0.05f * K;
    else e->pre_fire = 0;
    /* the gnat phase: a gnat every 5 frames, on alternate 64-frame stretches */
    if (!aggressive && !(((int)e->age / 64) % 2) && crossed(e->age, 5)) {
      int g = enemy_add(S, EK_GNAT, e->x + 1.7f, e->y + 1.2f, 1);
      if (g >= 0) S->en[g].target = idx;
    }
    break;
  }
  }
}

static void kill_enemy(sim *S, int i) {
  enemy *e = &S->en[i];
  if (!e->live) return;
  e->live = false;
  if (S->mode != PLAY_DEAD) S->score += ENEMY_SCORE[e->kind];
  S->kills++;
  S->kills_by[e->kind]++;
  event(S, EV_KILL, e->kind, e->x, e->y);
  /* a straight's death (and the second boss's beat) sends every gnat at the hero */
  if (e->kind >= EK_BOSS0) {
    event(S, EV_BOSS_DIE, e->kind, e->x, e->y);
    S->boss = -1;
    for (int j = 0; j < MAX_ENEMIES; j++)
      if (S->en[j].live) kill_enemy(S, j);
    for (int j = 0; j < MAX_EBULLETS; j++) S->eb[j].live = false;
    if (S->mode == PLAY_ON) S->mode = PLAY_LEVEL_OVER, S->mode_ticks = 0, S->success = 500;
  }
}

static void fleet_step(sim *S) {
  S->past_warn = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    enemy *e = &S->en[i];
    if (!e->live) continue;
    enemy_step(S, i);
    /* gnats turn on the hero on the straights' and the second boss's beat */
    if ((e->kind == EK_STRAIGHT && crossed(e->age - 192, 256) && e->age > 192) ||
        (e->kind == EK_BOSS1 && crossed(e->age - 272, 256) && e->age > 272))
      for (int j = 0; j < MAX_ENEMIES; j++)
        if (S->en[j].live && S->en[j].kind == EK_GNAT) S->en[j].target = -1;
    /* bosses smoke as they weaken */
    if (e->kind >= EK_BOSS0) {
      float f = -e->hp / e->max_hp;
      float every = f < 0.3f ? 0.08f : f < 0.5f ? 0.2f : f < 0.7f ? 0.36f : 0;
      if (every > 0) {
        e->smoke -= 1.0f / 60;
        if (e->smoke <= 0) {
          e->smoke = every;
          event(S, EV_SMOKE, e->kind, e->x + srand1(S) * HALF_W[e->kind] * 0.7f, e->y + srand1(S) * HALF_H[e->kind] * 0.5f);
        }
      }
      S->boss_hp = f;
    }
    if (e->kind != EK_GNAT && e->y < -8) {
      float w = 1 - (e->y + 14) / 6;
      if (w > S->past_warn) S->past_warn = w;
    }
    if (e->y < -14) {
      e->live = false;
      if (e->kind >= EK_BOSS0) S->boss = -1;
      if (S->mode == PLAY_ON) {
        S->h.lives--;
        S->losses++;
        event(S, EV_LOSE, e->kind, e->x, -9);
        if (S->h.lives < 0) start_death(S);
      }
    }
  }
}

/* ── collisions ────────────────────────────────────────────────────── */

static void hero_bullets(sim *S) {
  for (int i = 0; i < MAX_HBULLETS; i++) {
    hbullet *b = &S->hb[i];
    if (!b->live) continue;
    b->y += b->vy * K;
    b->age += K;
    if (b->y > FIELD_Y) {
      b->live = false;
      continue;
    }
    for (int j = 0; j < MAX_ENEMIES; j++) {
      enemy *e = &S->en[j];
      if (!e->live || fabsf(b->x - e->x) > HALF_W[e->kind] || fabsf(b->y - e->y) > HALF_H[e->kind]) continue;
      if (b->gun == GUN_GREEN) {
        e->hp += b->dmg * K; /* the lance burns every frame it overlaps */
      } else {
        e->hp += b->dmg;
        b->live = false;
      }
      e->hit_flash = 1;
      if ((S->tick & 3) == 0 || b->gun != GUN_GREEN) event(S, EV_HIT, b->gun, b->x, b->y);
      if (e->hp > 0) kill_enemy(S, j);
      if (!b->live) break;
    }
  }
}

static void enemy_bullets(sim *S) {
  hero *h = &S->h;
  bool vulnerable = sim_hero_visible(S) && h->bomb <= 0 && S->mode == PLAY_ON;
  for (int i = 0; i < MAX_EBULLETS; i++) {
    ebullet *b = &S->eb[i];
    if (!b->live) continue;
    b->x += b->vx * K;
    b->y += b->vy * K;
    if (fabsf(b->x) > FIELD_X || fabsf(b->y) > FIELD_Y) {
      b->live = false;
      continue;
    }
    if (vulnerable && fabsf(b->x - h->x) + fabsf(b->y - h->y) < 0.775f) {
      float d = EAMMO_DMG[b->kind];
      float f = d / 50;
      h->smx = b->vx * f, h->smy = b->vy * f;
      h->x += b->vx * f * 2, h->y += b->vy * f * 2;
      event(S, EV_HERO_HIT, b->kind, b->x, b->y);
      b->live = false;
      hero_damage(S, d);
      if (!sim_hero_visible(S)) vulnerable = false;
    }
  }
}

static void rams(sim *S) {
  hero *h = &S->h;
  if (!sim_hero_visible(S) || S->mode != PLAY_ON) return;
  for (int j = 0; j < MAX_ENEMIES && sim_hero_visible(S); j++) {
    enemy *e = &S->en[j];
    if (!e->live) continue;
    float dx = h->x - e->x, dy = h->y - e->y;
    if (fabsf(dx) + fabsf(dy) >= HALF_W[e->kind] + 0.7f) continue;
    float power = -e->hp * 0.5f;
    if (power > 35) power = 35;
    bool super = h->shields > HERO_SHIELDS;
    hero_damage(S, power * K);
    if (super) power *= 0.5f, e->hp += 70 * K;
    else e->hp += 40 * K;
    h->smx = dx * power * 0.03f, h->smy = dy * power * 0.03f;
    e->smx -= dx * COLLIDE_MOVE[e->kind];
    e->smy -= dy * COLLIDE_MOVE[e->kind] * 0.5f;
    e->hit_flash = 1;
    if ((S->tick & 3) == 0) event(S, EV_HERO_HIT, -1, (h->x + e->x) * 0.5f, (h->y + e->y) * 0.5f);
    if (e->hp > 0) kill_enemy(S, j);
  }
}

static void super_bomb(sim *S) {
  hero *h = &S->h;
  if (h->bomb <= 0) return;
  for (int j = 0; j < MAX_ENEMIES; j++) {
    enemy *e = &S->en[j];
    if (!e->live) continue;
    float dx = -e->x, dy = -15 - e->y, dist = sqrtf(dx * dx + dy * dy);
    if ((dist < h->bomb * 0.1f && e->kind < EK_BOSS0) || e->y < -11) {
      e->hp += 5000;
      if (e->hp > 0) kill_enemy(S, j);
    }
  }
  /* the wave burns away enemy fire it passes */
  for (int i = 0; i < MAX_EBULLETS; i++) {
    ebullet *b = &S->eb[i];
    if (!b->live) continue;
    float dx = b->x, dy = b->y + 15;
    if (dx * dx + dy * dy < h->bomb * 0.1f * h->bomb * 0.1f) b->live = false;
  }
  h->bomb += 2 * K;
  if (h->bomb > 300) h->bomb = 0;
}

/* ── power-ups ─────────────────────────────────────────────────────── */

static void powerups(sim *S) {
  hero *h = &S->h;
  for (int i = 0; i < MAX_POWERUPS; i++) {
    powerup *p = &S->pu[i];
    if (!p->live) continue;
    p->age += K;
    p->x += p->vx * K;
    p->y += (p->vy - 0.0272f) * K;
    p->vx *= D982, p->vy *= D982;
    if (p->x < -HERO_X) p->x = -HERO_X;
    if (p->x > HERO_X) p->x = HERO_X;
    if (sim_hero_visible(S) && S->mode == PLAY_ON && fabsf(p->x - h->x) + fabsf(p->y - h->y) < 0.85f) {
      switch (p->kind) {
      case PU_SHIELDS:
        if (h->shields < HERO_SHIELDS) h->shields = HERO_SHIELDS;
        break;
      case PU_SUPER:
        h->damage -= h->shields;
        if (h->damage < -HERO_HULL) h->damage = -HERO_HULL;
        h->shields = 2 * HERO_SHIELDS;
        break;
      case PU_REPAIR: h->damage = -HERO_HULL; break;
      default: {
        int g = p->kind - PU_AMMO0;
        h->ammo[g] += AMMO_MAX;
        if (h->ammo[g] > AMMO_MAX) h->ammo[g] = AMMO_MAX;
        S->score += 100;
        break;
      }
      }
      event(S, EV_PICKUP, p->kind, p->x, p->y);
      p->live = false;
      continue;
    }
    /* one let fall all the way pays a bonus */
    if (p->y < -12) {
      p->live = false;
      if (S->mode != PLAY_ON) continue;
      if (p->kind == PU_SUPER) add_life(S), S->score += 2500;
      else if (p->kind == PU_SHIELDS || p->kind == PU_REPAIR) S->score += 10000;
      else S->score += 2500;
    }
  }
}

/* ── the game ──────────────────────────────────────────────────────── */

static void level_start(sim *S) {
  memset(S->en, 0, sizeof S->en);
  memset(S->eb, 0, sizeof S->eb);
  memset(S->hb, 0, sizeof S->hb);
  memset(S->pu, 0, sizeof S->pu);
  S->frame = 0;
  S->tick = 0;
  S->mode = PLAY_ON;
  S->mode_ticks = 0;
  S->boss = -1;
  S->boss_hp = -1;
  S->success = 0;
  script_build(S);
}

void sim_new_game(sim *S, int level, float base_skill, uint32_t seed) {
  decays();
  memset(S, 0, sizeof *S);
  cw_rng_seed(&S->rng, seed);
  S->level = level < 1 ? 1 : level;
  S->skill = base_skill + 0.5f + (S->level - 1) * 0.05f;
  if (S->skill > 1.9f) S->skill = 1.9f;
  S->h.lives = 4;
  S->next_life = LIFE_SCORE;
  hero_reset(S);
  level_start(S);
}

void sim_next_level(sim *S) {
  S->level++;
  S->skill += 0.05f;
  if (S->skill > 1.9f) S->skill = 1.9f;
  /* full health (super shields go), lives and stocks are kept */
  hero *h = &S->h;
  h->damage = -HERO_HULL;
  h->shields = HERO_SHIELDS;
  h->x = 0, h->y = -3, h->smx = h->smy = 0, h->bomb = 0, h->dont_show = 0;
  level_start(S);
}

void sim_step(sim *S, const sim_input *in) {
  S->ev_n = 0;
  S->tick++;
  S->mode_ticks++;
  S->frame += K;
  if (S->mode == PLAY_ON) release_script(S);
  hero_step(S, in);
  fleet_step(S);
  hero_bullets(S);
  enemy_bullets(S);
  rams(S);
  super_bomb(S);
  powerups(S);
  if (S->mode == PLAY_LEVEL_OVER) S->success -= K;
  if (S->mode == PLAY_DEAD) S->hero_death -= K;
  if (S->boss < 0) S->boss_hp = -1;
}

uint32_t sim_hash(const sim *S) {
  uint32_t h = cw_hash32(&S->h, sizeof S->h, CW_HASH32_INIT);
  h = cw_hash32(&S->score, sizeof S->score, h);
  h = cw_hash32(S->en, sizeof S->en, h);
  h = cw_hash32(S->eb, sizeof S->eb, h);
  h = cw_hash32(&S->frame, sizeof S->frame, h);
  return h;
}
