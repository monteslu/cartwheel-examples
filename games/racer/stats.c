/* stats.c: dev aid RACER_STATS=1, a race's dynamics as one log line when the race ends, the numbers the fun checklist
 * (internal research) is measured by: lead changes and overtakes (is it a race), the finish spread (does catch-up keep
 * the pack together), item odds by place (hope for the back, a tax on the front), and how often each skill reward
 * fires (drift, trick, draft, near-miss boosts, Shockwave warnings and dodges, shield blocks, shortcuts).
 * It only reads sim events, so it changes nothing about a race. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

static struct {
  bool on, done;
  int leader, lead_changes, overtakes, human_overtakes;
  int boosts[7];              /* by EV_BOOST source */
  int gets[3][IT_KINDS];      /* item boxes by place third: front (1-2), middle (3-5), back (6-8) */
  int warns, shock_hits, shields, nearmiss, hits, spins, falls;
  int human_best, human_worst;
  int prev_item[MAXK], get_third[MAXK];
} T;

static int third(int place) { return place <= 2 ? 0 : place <= 5 ? 1 : 2; }

void stats_events(const sim *s) {
  if (!T.on) {
    if (!getenv("RACER_STATS") || s->battle) return;
    T.on = true, T.leader = -1, T.human_best = 99;
  }
  if (T.done) return;
  for (int e = 0; e < s->ev_n; e++) {
    const struct sim_event *v = &s->ev[e];
    switch (v->kind) {
    case EV_BOOST: if (v->b >= 0 && v->b < 7) T.boosts[v->b]++; break;
    case EV_ITEM_GET: T.get_third[v->a] = third(s->k[v->a].place) + 1; break;
    case EV_OVERTAKE: T.overtakes++, T.human_overtakes += s->k[v->a].human; break;
    case EV_WARN: T.warns++; break;
    case EV_HIT: T.hits++, T.shock_hits += (int)v->k == HZ_SHOCK; break;
    case EV_SHIELD: T.shields++; break;
    case EV_NEARMISS: T.nearmiss++; break;
    case EV_SPIN: T.spins++; break;
    case EV_FALL: T.falls++; break;
    }
  }
  if (s->phase != PH_RACE && s->phase != PH_DONE) return;
  /* the item is rolled after the box (item_wait): count it by the place the kart was in when it took the box */
  for (int i = 0; i < MAXK; i++) {
    int it = s->k[i].item;
    if (it != IT_NONE && T.prev_item[i] == IT_NONE && T.get_third[i]) T.gets[T.get_third[i] - 1][it]++, T.get_third[i] = 0;
    T.prev_item[i] = it;
  }
  if (s->race_ticks > 60 && s->order[0] != T.leader) T.lead_changes += T.leader >= 0, T.leader = s->order[0];
  for (int i = 0; i < s->nkarts; i++)
    if (s->k[i].human && s->race_ticks > 300 && !s->k[i].finished) {
      if (s->k[i].place < T.human_best) T.human_best = s->k[i].place;
      if (s->k[i].place > T.human_worst) T.human_worst = s->k[i].place;
    }
  bool all = true;
  int first = 1 << 30, last = 0, second = 1 << 30, human_place = 0, cuts = 0, nfin = 0;
  for (int i = 0; i < s->nkarts; i++) {
    const kart *k = &s->k[i];
    if (!k->on) continue;
    cuts += k->cuts;
    if (!k->finished) { all = false; continue; }
    nfin++;
    if (k->finish_tick < first) second = first, first = k->finish_tick;
    else if (k->finish_tick < second) second = k->finish_tick;
    if (k->finish_tick > last) last = k->finish_tick;
    if (k->human) human_place = k->place;
  }
  if (!all && s->phase != PH_DONE) return;
  T.done = true;
  char items[3][64];
  for (int t = 0; t < 3; t++) {
    int n = 0, atk = 0, def = 0, tur = 0;
    for (int it = 1; it < IT_KINDS; it++) n += T.gets[t][it];
    atk = T.gets[t][IT_MISSILE] + T.gets[t][IT_HOMING] + T.gets[t][IT_SHOCK];
    def = T.gets[t][IT_OIL] + T.gets[t][IT_MINE] + T.gets[t][IT_SHIELD];
    tur = T.gets[t][IT_TURBO] + T.gets[t][IT_TURBO3];
    snprintf(items[t], sizeof items[t], "%d(atk %d%% def %d%% turbo %d%% shock %d)", n, n ? atk * 100 / n : 0,
             n ? def * 100 / n : 0, n ? tur * 100 / n : 0, T.gets[t][IT_SHOCK]);
  }
  fprintf(stderr,
          "[stats] map %d laps %d finished %d | lead changes %d, overtakes %d (human %d) | human place %d (best %d worst %d) | "
          "spread 1st-2nd %.2f s, 1st-last %.2f s | boosts drift %d trick %d pad %d draft %d nearmiss %d start %d item %d | "
          "shock warns %d shock hits %d shields %d hits %d spins %d falls %d shortcuts %d | "
          "items front %s mid %s back %s\n",
          s->map, s->laps, nfin, T.lead_changes, T.overtakes, T.human_overtakes, human_place, T.human_best, T.human_worst,
          nfin > 1 ? (second - first) / 60.0 : 0.0, nfin > 1 ? (last - first) / 60.0 : 0.0, T.boosts[2], T.boosts[4], T.boosts[0], T.boosts[5], T.boosts[6],
          T.boosts[3], T.boosts[1], T.warns, T.shock_hits, T.shields, T.hits, T.spins, T.falls, cuts, items[0], items[1],
          items[2]);
}

/* dev aid RACER_BUMPLOG=1: a kart that loses more than 8 m/s in a tenth of a second with nothing to show for it (no wall,
 * no kart, no hit, no spin, no landing, no water or slow ground) has met something it should not have: an obstacle no
 * one can see. Each one is logged with where it was. */
void bumpcheck_events(const sim *s) {
  static int on = -1;
  if (on < 0) on = getenv("RACER_BUMPLOG") != NULL;
  if (!on || s->battle || s->phase != PH_RACE) return;
  static float spd[MAXK][8];
  static int why[MAXK]; /* the last tick something explained a slow-down */
  for (int e = 0; e < s->ev_n; e++) {
    const struct sim_event *v = &s->ev[e];
    if (v->kind == EV_WALL || v->kind == EV_HIT || v->kind == EV_SPIN || v->kind == EV_LAND || v->kind == EV_WATER || v->kind == EV_FALL || v->kind == EV_RESPAWN) why[v->a & 7] = s->tick;
    if (v->kind == EV_BUMP) why[v->a & 7] = why[v->b & 7] = s->tick;
  }
  for (int i = 0; i < s->nkarts; i++) {
    const kart *k = &s->k[i];
    float now = v3len(k->v), was = spd[i][s->tick & 7];
    spd[i][s->tick & 7] = now;
    if (!k->on || k->finished || k->respawn_ticks > 0 || k->spin_ticks > 0 || k->loc.i < 0) continue;
    const tsample *a = &s->track->s[k->loc.i];
    if (a->flags & (TF_SLOW | TF_WATER | TF_GAP)) continue;
    if (was - now > 8.0f && s->tick - why[i] > 12 && s->race_ticks > 60)
      cw_info("bumpcheck: kart %d lost %.0f m/s at s %.0f lat %.1f h %.1f br %d tick %d (flags 0x%x, p %.0f,%.0f,%.0f)", i, was - now,
              k->loc.ms, k->loc.lat, k->loc.height, k->loc.br, s->tick, (unsigned)a->flags, k->p.x, k->p.y, k->p.z);
  }
}
