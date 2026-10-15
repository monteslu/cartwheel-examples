/* chromite level scripts: the original's wave generators. A level is a
 * schedule of enemies and power-ups made at its start from the game's
 * random stream, in 50 fps frames; sim.c releases each item when the
 * level's clock reaches it. Three scripts, repeated with rising skill:
 *
 *   1  straights and omnis, arrows of both, rayguns late, the first boss
 *   2  adds gnat swarms, the second boss (who brings his own gnats)
 *   3  adds tanks, more swarms and power-up bursts, the second boss again */
#include <stdlib.h>

#include "game.h"

typedef struct wave {
  int type, begin, end, period, jitter;
  bool arrow;
  float x, y, xj;
} wave;

static wave W(int type) { return (wave){ type, 1, 500, 60, 10, false, 0, 10, 8 }; }
static void in_out(wave *w, int b, int e) { w->begin = b, w->end = e <= b ? b + 1 : e; }

static sim *S_;
static float frand(void) { return cw_rng_float(&S_->rng); }
static float srand1(void) { return cw_rng_float(&S_->rng) * 2 - 1; }

static void item(int at, int kind, float x, float y, bool arrow) {
  if (S_->script_n >= MAX_SCRIPT) return;
  S_->script[S_->script_n++] = (script_item){ at, (int16_t)kind, (int16_t)arrow, x, y };
}

static void add_wave(const wave *w) {
  int interval = 1, iteration = 0;
  int jitter = (int)(w->jitter * (2.0f - S_->skill)), period = (int)(w->period * (2.0f - S_->skill));
  if (jitter >= period) jitter = period - 1;
  if (period < 1) period = 1;
  for (int i = w->begin; i < w->end; i++) {
    if (--interval >= 1) continue;
    if (!w->arrow) {
      item(i, w->type, w->x + w->xj * srand1(), w->y, false);
    } else {
      item(i, w->type, w->x + w->xj * iteration, w->y, true);
      if (iteration > 0) item(i, w->type, w->x - w->xj * iteration, w->y, true);
      iteration++;
    }
    interval = period + (int)(srand1() * jitter);
  }
}

static void straight_wave(int o, int duration, float density) {
  wave w = W(EK_STRAIGHT);
  w.xj = 8;
  w.period = (int)(60 / density), w.jitter = 5;
  in_out(&w, o, o + duration);
  add_wave(&w);
}
static void omni_wave(int o, int duration, float density) {
  wave w = W(EK_OMNI);
  w.xj = 1;
  w.period = (int)(39 / density), w.jitter = 7;
  w.x = srand1() * 8, w.y = 9;
  in_out(&w, o, o + duration / 2 + 50);
  add_wave(&w);
  w.x = srand1() * 8, w.y = 9;
  in_out(&w, o + duration / 2 - 50, o + duration);
  add_wave(&w);
  wave s = W(EK_STRAIGHT);
  s.xj = 8, s.period = 200, s.jitter = 50;
  in_out(&s, o + 100, o + duration);
  add_wave(&s);
}
static void straight_arrow(int o, float density) {
  float c = srand1();
  wave a = W(EK_STRAIGHT);
  in_out(&a, o, o + 130);
  a.x = c, a.y = 10;
  a.period = (int)(50 / density), a.jitter = 0;
  a.xj = 1.6f, a.arrow = true;
  add_wave(&a);
  wave w = W(EK_OMNI);
  w.period = (int)(15 / density), w.jitter = 5;
  w.x = c, w.y = 9, w.xj = 2;
  in_out(&w, o + 220, o + 260);
  add_wave(&w);
  w.xj = 3, w.period = (int)(22 / density), w.jitter = 5;
  in_out(&w, o + 440, o + 600);
  add_wave(&w);
}
static void omni_arrow(int o, float density) {
  float c = srand1() * 2;
  wave a = W(EK_OMNI);
  a.arrow = true, a.x = c, a.y = 10;
  a.period = (int)(25 / density), a.jitter = 0, a.xj = 1;
  static const int SPANS[4][2] = { { 50, 150 }, { 250, 320 }, { 300, 330 }, { 350, 470 } };
  for (int i = 0; i < 4; i++) in_out(&a, o + SPANS[i][0], o + SPANS[i][1]), add_wave(&a);
  a.period = 5, a.jitter = 0, a.xj = 1.8f;
  in_out(&a, o + 550, o + 555);
  add_wave(&a);
}
static void gnat_wave(int o, int duration, float density, bool mixed) {
  float c = -frand() * 3;
  if (mixed) {
    /* c is never above 0: the omni branch always runs (as in the original) */
    wave s = W(EK_STRAIGHT);
    in_out(&s, o + 50, o + duration);
    s.x = c, s.y = 10, s.period = (int)(90 / density), s.jitter = 20, s.xj = 8;
    wave w = W(EK_OMNI);
    in_out(&w, o + 50, o + 130);
    w.x = c, w.y = 10, w.period = (int)(20 / density), w.jitter = 0, w.xj = 1.1f;
    add_wave(&w);
    in_out(&w, o + 320, o + 400);
    add_wave(&w);
    in_out(&s, o + 200, o + 250);
    add_wave(&s);
    wave g = W(EK_GNAT);
    in_out(&g, o, o + (int)((25 * S_->skill) * (1 + frand() * 0.2f)));
    g.x = srand1() * 5, g.y = 10, g.period = (int)(3 / density), g.jitter = 0, g.xj = 3;
    add_wave(&g);
  } else {
    wave g = W(EK_GNAT);
    g.x = -3 + srand1() * 5, g.y = 10;
    g.period = (int)(1 / density), g.jitter = 0, g.xj = 3;
    in_out(&g, o, o + 35);
    add_wave(&g);
    g.x = srand1() * 5;
    in_out(&g, o + 300, o + 310);
    add_wave(&g);
    g.period = (int)(30 / density);
    in_out(&g, o + 300, o + 400);
    add_wave(&g);
  }
}

static void ammunition(int o, int duration, int a, int b, int c) {
  int p0 = a + (int)(frand() * 200), p1 = b + (int)(frand() * 200), p2 = c + (int)(frand() * 500);
  float sk = 2.0f - S_->skill;
  for (int i = o; i < o + duration; i++) {
    p0--, p1--, p2--;
    float x = 8 * srand1();
    if (p0 < 1) p0 = (int)(sk * 2000 + (int)(srand1() * 500)), item(i, 100 + PU_AMMO0, x, 9, false);
    else if (p1 < 1) p1 = (int)(sk * 2500 + (int)(srand1() * 1000)), item(i, 100 + PU_AMMO1, x, 9, false);
    else if (p2 < 1) p2 = (int)(sk * sk * 4000 + (int)(srand1() * 1000)), item(i, 100 + PU_AMMO2, x, 9, false);
  }
}
static void powerups(int o, int duration, int a, int b, int c) {
  int p0 = a + (int)(frand() * 500), p1 = b + (int)(frand() * 500), p2 = c + (int)(frand() * 1500);
  for (int i = o; i < o + duration; i++) {
    p0--, p1--, p2--;
    float x = 8 * srand1();
    if (p0 < 1) p0 = 2500 + (int)(srand1() * 700), item(i, 100 + PU_SHIELDS, x, 9, false);
    else if (p1 < 1) p1 = 4000 + (int)(srand1() * 900), item(i, 100 + PU_REPAIR, x, 9, false);
    else if (p2 < 1) p2 = 5000 + (int)(srand1() * 3000), item(i, 100 + PU_SUPER, x, 9, false);
  }
}

static void level_1(void) {
  int n = 12000, i = 600, dur = 500;
  straight_wave(1, i, 0.4f);
  while (i < n - 1000) {
    float d = i < 1500 ? (i + 250.0f) / 2000.0f : 1;
    float r = frand();
    if (r < 0.15f) straight_arrow(i, d);
    else if (r < 0.25f) omni_arrow(i, d);
    else if (r > 0.6f) straight_wave(i, dur, d);
    else omni_wave(i, dur, d);
    i += dur;
    dur = (int)(600 * S_->skill) + (int)(100 * srand1());
    i += 50 + (int)(50 * frand());
  }
  wave ray = W(EK_RAYGUN);
  ray.xj = 8, ray.period = 2000, ray.jitter = 1000;
  in_out(&ray, n / 2, i - 1000);
  add_wave(&ray);
  wave boss = W(EK_BOSS0);
  in_out(&boss, i + 75, i + 1000);
  boss.x = 0, boss.y = 15, boss.xj = 4, boss.period = 5000, boss.jitter = 0;
  add_wave(&boss);
  ammunition(0, n + 9000, 0, 100, 1000);
  powerups(0, n + 9000, 300, 1200, 1000);
}

static void level_23(bool third) {
  int n = 14000, i = 500, dur = 500, waves = 0;
  straight_wave(100, i, third ? 0.5f : 0.4f);
  while (i < n) {
    float r = frand();
    waves++;
    bool swarm = third ? (waves == 5 || waves == 12) : (waves == 5 || waves == 6 || waves == 11 || waves == 12 || waves == 15 || waves == 16);
    bool tanks = third && (waves == 6 || waves == 11 || waves == 15 || waves == 16);
    if (swarm) gnat_wave(i, dur, third ? 0.9f : 1, false);
    else if (tanks) {
      /* the original's setInOut(i+50, waveDuration-50) leaves one tank */
      wave t = W(EK_TANK);
      t.x = 0, t.y = 11, t.xj = 10, t.period = 700, t.jitter = 100;
      in_out(&t, i + 50, dur - 50);
      add_wave(&t);
      straight_wave(i, 300, 1);
    } else if (waves < 5) {
      if (r < 0.2f) straight_arrow(i, 1);
      else if (r < 0.3f) omni_arrow(i, 1);
      else if (r < 0.6f) omni_wave(i, dur, 1);
      else straight_wave(i, dur, 1);
    } else {
      if (r < 0.25f) gnat_wave(i, dur, 1, true);
      else if (r < 0.35f) straight_arrow(i, 1);
      else if (r < 0.5f) omni_arrow(i, 1);
      else if (r < 0.8f) omni_wave(i, dur, 1);
      else straight_wave(i, dur, 1);
    }
    i += dur;
    dur = (int)(700 + 100 * srand1());
    i += 50 + (int)(50 * frand());
  }
  wave g = W(EK_GNAT);
  in_out(&g, third ? 3000 : 2200, 5000);
  g.x = frand() * 4, g.y = 10, g.period = 150, g.jitter = 140, g.xj = 5;
  add_wave(&g);
  if (third) {
    in_out(&g, 8000, 11000);
    add_wave(&g);
    ammunition(0, 2000, 500, 700, 0);
    ammunition(10000, 2000, 500, 700, 0);
    powerups(3000, 2000, 2500, 2500, 0);
    powerups(9500, 2000, 2500, 2500, 0);
    powerups(n, 2000, 2500, 2500, 1);
  }
  wave boss = W(EK_BOSS1);
  boss.x = 0, boss.y = 15, boss.xj = 4, boss.period = 5000, boss.jitter = 0;
  in_out(&boss, n + 700, n + 710);
  add_wave(&boss);
  if (third) {
    wave t = W(EK_TANK);
    t.x = 0, t.y = 11, t.xj = 10, t.period = 2000, t.jitter = 200;
    in_out(&t, n + 400, n + 3000);
    add_wave(&t);
  }
  ammunition(0, n + 9000, 0, 100, 1000);
  powerups(0, n + 9000, 300, 1200, 1000);
}

static int by_time(const void *a, const void *b) {
  const script_item *x = a, *y = b;
  return x->at < y->at ? -1 : x->at > y->at;
}

void script_build(sim *S) {
  S_ = S;
  S->script_n = S->script_i = 0;
  switch ((S->level - 1) % 3) {
  case 0: level_1(); break;
  case 1: level_23(false); break;
  default: level_23(true); break;
  }
  /* stable by time: insertion order breaks ties (qsort is not stable, so
   * the index rides along in the high bits of a key) */
  for (int i = 0; i < S->script_n; i++) S->script[i].at = S->script[i].at * 8192 + i;
  qsort(S->script, (size_t)S->script_n, sizeof S->script[0], by_time);
  for (int i = 0; i < S->script_n; i++) S->script[i].at /= 8192;
  S_ = NULL;
}
