/* chromite's study: whole games of level 1 run without drawing, flown by a newcomer bot, for the newcomer gate
 * (the debug command `study newcomer <runs>`). Each run is the game's own simulation from the title's new game,
 * on the default skill and rules, and the same bot (newcomer_*) the cart gate drives over the pad.
 *
 * The newcomer plays by what the screen draws (sim_view: the craft and the enemy rounds nearest the ship) and makes a
 * person's mistakes:
 *   it looks every NB_LOOK ticks and acts on what it saw NB_DELAY_LOOKS looks before (a reaction of about a quarter
 *   of a second), guessing where a round has gone from how far it moved between two of those looks;
 *   it aims at the lowest craft with an error of about NB_AIM_SIGMA field units that it corrects only now and then;
 *   it misses one round in eight it could have seen;
 *   it holds fire, never self-destructs and never goes for a power-up. */
#include <string.h>

#include "game.h"
#include "study.h"

void sim_view(const sim *S, float *en, float *rn) {
  int n = 0;
  for (int i = 0; i < MAX_ENEMIES && n < VIEW_N; i++)
    if (S->en[i].live && S->en[i].y < FIELD_Y) en[n * 2] = S->en[i].x, en[n * 2 + 1] = S->en[i].y, n++;
  for (; n < VIEW_N; n++) en[n * 2] = en[n * 2 + 1] = 99;
  /* the nearest VIEW_N rounds to the ship, nearest first */
  static float d2[MAX_EBULLETS];
  for (int i = 0; i < MAX_EBULLETS; i++) {
    const ebullet *b = &S->eb[i];
    float dx = b->x - S->h.x, dy = b->y - S->h.y;
    d2[i] = b->live ? dx * dx + dy * dy : 1e30f;
  }
  for (n = 0; n < VIEW_N; n++) {
    int k = 0;
    for (int i = 1; i < MAX_EBULLETS; i++)
      if (d2[i] < d2[k]) k = i;
    if (d2[k] >= 1e30f) break;
    rn[n * 2] = S->eb[k].x, rn[n * 2 + 1] = S->eb[k].y;
    d2[k] = 1e30f;
  }
  for (; n < VIEW_N; n++) rn[n * 2] = rn[n * 2 + 1] = 99;
}

/* ── the newcomer (tests/chromite-newcomer.mjs runs the same steps in JavaScript) ── */

static uint32_t nb_next(newcomer *b) {
  uint32_t x = b->rng;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return b->rng = x;
}
static float nb_unit(newcomer *b) { return (float)(nb_next(b) >> 8) / 16777216.0f; }
/* about a unit normal: the sum of three uniforms */
static float nb_gauss(newcomer *b) { return (nb_unit(b) + nb_unit(b) + nb_unit(b) - 1.5f) * 2.0f; }

void newcomer_init(newcomer *b, uint32_t seed) {
  memset(b, 0, sizeof *b);
  b->rng = seed * 2654435761u + 0x9e3779b9u;
  if (!b->rng) b->rng = 1;
  for (int i = 0; i < 4; i++) nb_next(b);
  b->aim_err = nb_gauss(b) * NB_AIM_SIGMA;
}

/* a look: the view a tick ago (ra) and now (en, rb); each round's move is the one from the round a tick before it that
 * is as far off as a round can move in a tick (the nearest would often be its neighbour in a stream) */
void newcomer_see(newcomer *b, const float *en, const float *ra, const float *rb, float hx, float hy) {
  int k = b->looks % NB_HIST;
  memcpy(b->en[k], en, sizeof b->en[k]);
  for (int i = 0; i < VIEW_N; i++) {
    float *r = &b->rn[k][i * 4];
    r[0] = rb[i * 2], r[1] = rb[i * 2 + 1], r[2] = 0, r[3] = -0.2f * K;
    if (r[0] > 90) continue;
    float best = 1e9f;
    for (int j = 0; j < VIEW_N; j++) {
      if (ra[j * 2] > 90) break;
      float dx = r[0] - ra[j * 2], dy = r[1] - ra[j * 2 + 1], d2 = dx * dx + dy * dy;
      if (d2 < 0.12f * 0.12f || d2 > 0.65f * 0.65f || d2 >= best) continue;
      best = d2, r[2] = dx, r[3] = dy;
    }
  }
  b->hx = hx, b->hy = hy;
  b->looks++;
}

/* the stick (x right, y up the screen, -1..1): the rounds from the look NB_DELAY_LOOKS before, carried on by the move
 * it saw (a person guesses most of the way), against where its ship is now (it feels its own stick). It weighs fifteen
 * moves over the next 36 ticks: the danger of each, the aim at the lowest craft, home low on the screen. */
void newcomer_decide(newcomer *b, float *sx, float *sy) {
  *sx = *sy = 0;
  if (b->looks <= NB_DELAY_LOOKS) return;
  int k = (b->looks - 1 - NB_DELAY_LOOKS) % NB_HIST;
  const float *en = b->en[k], *rn = b->rn[k];
  float hx = b->hx, hy = b->hy;
  /* now and then it corrects its aim, never fully */
  if (nb_unit(b) < 1.0f / NB_AIM_HOLD) b->aim_err = b->aim_err * 0.4f + nb_gauss(b) * NB_AIM_SIGMA;
  float bx = 99, by = 99;
  for (int i = 0; i < VIEW_N; i++)
    if (en[i * 2] < 90 && en[i * 2 + 1] > hy + 1 && en[i * 2 + 1] < by) bx = en[i * 2], by = en[i * 2 + 1];
  if (bx < 90) b->tx = bx + b->aim_err;
  float px[VIEW_N], py[VIEW_N], vx[VIEW_N], vy[VIEW_N];
  int n = 0;
  const float ahead = NB_DELAY_LOOKS * NB_LOOK * NB_LEAD;
  for (int i = 0; i < VIEW_N; i++) {
    const float *r = &rn[i * 4];
    if (r[0] > 90) break;
    if (nb_unit(b) < NB_MISS) continue; /* one it did not notice */
    px[n] = r[0] + r[2] * ahead, py[n] = r[1] + r[3] * ahead, vx[n] = r[2], vy[n] = r[3], n++;
  }
  float best = 1e30f;
  for (int cx = -2; cx <= 2; cx++)
    for (int cy = -1; cy <= 1; cy++) {
      float mx = cx * 0.5f, my = (float)cy, l = sqrtf(mx * mx + my * my);
      if (l > 1) mx /= l, my /= l;
      float danger = 0;
      for (int t = 0; t <= 36; t += 3) {
        float x = cw_clampf(hx + mx * NB_SPEED * t, -HERO_X, HERO_X), y = cw_clampf(hy + my * NB_SPEED * t, -HERO_Y, HERO_Y);
        for (int j = 0; j < n; j++) {
          float d = fabsf(px[j] + vx[j] * t - x) + fabsf(py[j] + vy[j] * t - y);
          if (d < 1.3f) danger += (1.3f - d) * (1 - t / 45.0f);
        }
        for (int i = 0; i < VIEW_N; i++) {
          if (en[i * 2] > 90) continue;
          float d = fabsf(en[i * 2] - x) + fabsf(en[i * 2 + 1] - y);
          if (d < 2.0f) danger += (2.0f - d) * 0.5f;
        }
      }
      float fx = hx + mx * NB_SPEED * 12, fy = hy + my * NB_SPEED * 12;
      float cost = danger * 10 + fabsf(fx - b->tx) * 0.3f + fabsf(fy - NB_HOME_Y) * 0.15f;
      if (cost < best) best = cost, *sx = mx, *sy = my;
    }
}

/* one game of level 1 from the title's new game, with continues as the results card offers them (modern rules): until
 * the level is cleared, NB_MAX_CONTINUES are spent or the clock runs out */
void study_newcomer_run(uint32_t seed, float base_skill, uint32_t game_seed, study_run *out) {
  static sim S;
  static newcomer b;
  static float en[VIEW_N * 2], ra[VIEW_N * 2], rb[VIEW_N * 2];
  sim_new_game(&S, 1, base_skill, game_seed, RULES_MODERN);
  newcomer_init(&b, seed);
  memset(out, 0, sizeof *out);
  out->boss_tick = out->clear_tick = out->lives_at_boss = -1;
  float sx = 0, sy = 0;
  int t = 0;
  while (t < 60 * 60 * 10) {
    /* a look: the view a tick apart, then the stick for the rest of the look */
    for (int i = 0; i < NB_LOOK && S.mode != PLAY_DEAD; i++, t++) {
      if (i == 0) sim_view(&S, en, ra);
      if (i == 1) {
        sim_view(&S, en, rb);
        newcomer_see(&b, en, ra, rb, S.h.x, S.h.y);
        newcomer_decide(&b, &sx, &sy);
      }
      sim_input in = { sx, sy, true, false };
      sim_step(&S, &in);
      for (int e = 0; e < S.ev_n; e++)
        if (S.ev[e].type == EV_HERO_DIE) *(S.boss >= 0 ? &out->boss_deaths : &out->wave_deaths) += 1;
      if (out->boss_tick < 0 && S.boss >= 0) out->boss_tick = t, out->lives_at_boss = S.h.lives;
    }
    if (S.mode == PLAY_LEVEL_OVER) { out->clear_tick = t; break; }
    if (S.mode == PLAY_DEAD) {
      if (out->continues >= NB_MAX_CONTINUES) break;
      out->continues++;
      sim_continue(&S);
    }
  }
  out->ticks = t;
  out->lives = S.h.lives;
  out->kills = S.kills;
  out->escapes = S.escapes;
  out->boss_hp100 = (int)(S.boss_hp * 100);
  out->score = (int32_t)(S.score > 2000000000 ? 2000000000 : S.score);
}
