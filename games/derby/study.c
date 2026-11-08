/* derby's studies: whole bouts and test drives run in the background, without drawing, for the gates (the debug
 * command `study`). Each is the bout's own simulation and CPU drivers, deterministic like the bout.
 *
 *   opening  n quick matches on the first arena's field, the player a newcomer: slow to start, part throttle,
 *            lagging hands, aiming at the nearest car and letting go of the gas for a moment after every hit.
 *            Each bout differs in how soon and how hard the newcomer goes. Reports the player's engine ten seconds
 *            after the flag, how many CPU drivers hunted the player through that time, and when the player was
 *            wrecked.
 *   pull     a sedan with three hits on one side, then the gas held for 2.5 s with the wheel straight: how far its
 *            heading turned, its top speed, its grip per side. */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "study.h"

typedef struct newcomer {
  int delay;      /* ticks after the flag before the gas */
  float gain, thr;
  float hands;    /* the steering as it lags the aim */
  int dazed;      /* ticks off the gas after a hit */
} newcomer;

static cin newcomer_drive(const sim *s, int me, newcomer *n) {
  cin o = { 0 };
  const car *c = &s->c[me];
  if (s->phase != PH_GO || c->wrecked || s->phase_ticks < n->delay) return o;
  int best = -1;
  float bd = 1e9f;
  for (int i = 0; i < MAXC; i++) {
    if (i == me || !s->c[i].on || s->c[i].wrecked) continue;
    float dx = s->c[i].x - c->x, dz = s->c[i].z - c->z, d = dx * dx + dz * dz;
    if (d < bd) bd = d, best = i;
  }
  if (best < 0) return o;
  float f, sd;
  car_local(c, s->c[best].x, s->c[best].z, &f, &sd);
  float st = -cw_atan2(sd, f) * n->gain;
  st = st > 1 ? 1 : st < -1 ? -1 : st;
  for (int e = 0; e < s->ev_n; e++)
    if (s->ev[e].kind == EV_CRASH && (s->ev[e].a == me || s->ev[e].b == me)) n->dazed = 50;
  n->hands += (st - n->hands) * 0.08f;
  o.steer = (int8_t)(n->hands * 127);
  if (n->dazed > 0) n->dazed--;
  else o.thr = (int8_t)(127 * n->thr);
  return o;
}

/* the quick match's field (main.c line_up): you, two pros, two rookies, an ace */
static const int KINDS[MAXC] = { CAR_SEDAN, CAR_PICKUP, CAR_WAGON, CAR_VAN, CAR_SEDAN, CAR_WAGON, CAR_PICKUP, CAR_VAN };
static const int LEVEL[MAXC] = { 1, 1, 1, 0, 0, 2, 0, 0 };

void study_opening(int bouts, study_bout *out) {
  static sim S;
  static ai_mem M[MAXC];
  for (int b = 0; b < bouts; b++) {
    bool on[MAXC] = { 0 };
    for (int i = 0; i < 6; i++) on[i] = true;
    sim_init(&S, on, KINDS, RULE_LAST_RUNNING, 180, 1000u + 7919u); /* arena 1, as start_bout seeds it */
    for (int i = 0; i < MAXC; i++) ai_reset(&M[i], i, 2u);
    newcomer n = { 30 + (b % 10) * 9, 1.4f + 0.15f * (float)(b / 10) + 0.05f * (float)(b % 3), 0.55f + 0.04f * (float)(b % 10), 0, 0 };
    study_bout r = { -1, 0, -1 };
    int hunted = 0;
    while (S.phase != PH_OVER && r.wrecked_tick < 0) {
      cin in[MAXC] = { 0 };
      in[0] = newcomer_drive(&S, 0, &n);
      for (int i = 1; i < MAXC; i++)
        if (on[i]) in[i] = ai_think(&S, i, LEVEL[i], &M[i]);
      sim_step(&S, in);
      if (S.phase == PH_GO && S.phase_ticks < 600)
        for (int i = 1; i < MAXC; i++) hunted += on[i] && M[i].target == 0;
      if (S.phase == PH_GO && S.phase_ticks == 600) r.hp10 = S.c[0].hp;
      if (S.c[0].wrecked) r.wrecked_tick = S.tick - 180; /* from the flag */
    }
    if (r.hp10 < 0) r.hp10 = S.c[0].wrecked ? 0 : S.c[0].hp;
    r.hunters10 = hunted / 600.0f;
    out[b] = r;
  }
  sim_free(&S);
}

void study_pull(int zone, study_pull_result *out) {
  static sim S;
  bool on[MAXC] = { true };
  int kind[MAXC] = { CAR_SEDAN };
  sim_init(&S, on, kind, RULE_POINTS, 180, 7); /* points: a car alone is not the last one running */
  sim_place(&S, 0, 0, -20, 0);
  if (zone >= 0)
    for (int k = 0; k < 3; k++) sim_hurt(&S, 0, zone, 15);
  cin in[MAXC] = { 0 };
  while (S.phase == PH_COUNTDOWN) sim_step(&S, in);
  float h0 = S.c[0].h, top = 0;
  in[0].thr = 127;
  for (int t = 0; t < 150; t++) {
    sim_step(&S, in);
    float v = sqrtf(S.c[0].vx * S.c[0].vx + S.c[0].vz * S.c[0].vz);
    if (v > top) top = v;
  }
  out->turned_deg = (S.c[0].h - h0) * 57.29578f;
  out->top = top;
  out->hp = S.c[0].hp;
  out->bend = S.c[0].bend;
  out->grip_l = S.c[0].veh.def.wheels[0].grip;
  out->grip_r = S.c[0].veh.def.wheels[1].grip;
  sim_free(&S);
}

void study_classes(int bouts, int wins[CAR_KINDS], int starts[CAR_KINDS], study_kind_drive drive[CAR_KINDS]) {
  static sim S;
  static ai_mem M[MAXC];
  for (int k = 0; k < CAR_KINDS; k++) wins[k] = starts[k] = 0;
  for (int b = 0; b < bouts; b++) {
    bool on[MAXC] = { 0 };
    int kind[MAXC] = { 0 };
    for (int i = 0; i < 6; i++) on[i] = true, kind[i] = (i + b) % CAR_KINDS;
    sim_init(&S, on, kind, RULE_LAST_RUNNING, 180, 1000u + (uint32_t)b * 7919u);
    for (int i = 0; i < MAXC; i++) ai_reset(&M[i], i, (uint32_t)b + 1);
    while (S.phase != PH_OVER) {
      cin in[MAXC] = { 0 };
      for (int i = 0; i < 6; i++) in[i] = ai_think(&S, i, 1, &M[i]);
      sim_step(&S, in);
    }
    for (int i = 0; i < 6; i++) starts[kind[i]]++;
    if (S.winner >= 0) wins[kind[S.winner]]++;
  }
  /* each body alone: one standard hit on the door, then the gas held 4 s from rest */
  for (int k = 0; k < CAR_KINDS; k++) {
    bool on[MAXC] = { true };
    int kind[MAXC] = { k };
    sim_init(&S, on, kind, RULE_POINTS, 180, 7);
    sim_place(&S, 0, 0, -AL + 6, 0);
    sim_hurt(&S, 0, ZONE_LEFT, 20);
    drive[k].hp_after_hit = S.c[0].hp;
    S.c[0].hp = 100; /* (the drive on a sound engine) */
    cin in[MAXC] = { 0 };
    while (S.phase == PH_COUNTDOWN) sim_step(&S, in);
    in[0].thr = 127;
    drive[k].top = 0;
    for (int t = 0; t < 240; t++) {
      sim_step(&S, in);
      float v = sqrtf(S.c[0].vx * S.c[0].vx + S.c[0].vz * S.c[0].vz);
      if (t == 119) drive[k].speed2 = v;
      if (v > drive[k].top) drive[k].top = v;
    }
  }
  sim_free(&S);
}
