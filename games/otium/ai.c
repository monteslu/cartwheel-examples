/* otium's rival houses: each time a house is free it scores every order it
 * could give now (what it is worth in points, roughly, over the days it
 * takes) and gives the best. Nothing random and nothing hidden: they pay
 * what you pay, see what you see, and the same position gets the same
 * answer on every host. A personality weighs the gods it courts:
 *   builder   Saturn and Jupiter: spread over many regions
 *   merchant  Mercury and Vesta: many kinds of goods, a full purse
 *   general   Mars: caravans and galleys, then whatever they reach */
#include <string.h>

#include "game.h"

static const float WEIGHT[PERSONALITIES][GODS] = {
  /*            Jup   Sat   Mer   Mars  Vesta */
  /* builder */ { 1.3f, 1.4f, 0.9f, 0.7f, 0.7f },
  /* merchant*/ { 0.9f, 0.8f, 1.4f, 0.7f, 1.3f },
  /* general */ { 1.0f, 1.0f, 0.9f, 1.5f, 0.7f },
};

/* the points this player would have after a change, with its favor and a little
 * extra pull toward the personality's gods */
static float worth(const sim *S, int p, int pers) {
  float t = 0;
  for (int g = 0; g < GODS; g++) t += (float)S->p[p].favor[g] * WEIGHT[pers][g] * (float)sim_score(S, p, g);
  return t;
}

/* what a house in city c adds (the goods it will make count too) */
static float house_value(const sim *S, int p, int pers, int c) {
  if (CITIES[c].good < 0 || (S->house[c] >> p & 1)) return 0;
  sim T = *S;
  float before = worth(&T, p, pers);
  T.house[c] |= (uint8_t)(1u << p);
  float v = worth(&T, p, pers) - before;
  v += 0.45f * (float)S->price[CITIES[c].good]; /* what its harvests will bring */
  /* a crowded city costs more and its harvests feed rivals too */
  int others = 0;
  for (int i = 0; i < NP; i++)
    if (i != p && (S->house[c] >> i & 1)) others++;
  return v - 0.6f * (float)others;
}

static float goods_short(const sim *S, int p, int c) {
  int goods[GOODS], coins = sim_house_cost(S, p, c, goods);
  float short_by = 0;
  for (int g = 0; g < GOODS; g++)
    if (S->p[p].goods[g] < goods[g]) short_by += (float)(goods[g] - S->p[p].goods[g]) * (float)S->price[g];
  if (S->p[p].coins < coins) short_by += (float)(coins - S->p[p].coins);
  return short_by;
}

order_req ai_choose(const sim *S, int p, int pers) {
  const player *pl = &S->p[p];
  order_req best = { .order = O_NONE, .city = CAPITAL, .unit = -1, .offer = -1 };
  float best_v = -1e9f;
  int days_left = END_TICK / DAY - S->tick / DAY;
#define CONSIDER(req, value, days)                                                                                                         \
  do {                                                                                                                                     \
    float _v = (value) / (float)((days) + 2);                                                                                              \
    if (_v > best_v) best_v = _v, best = (req);                                                                                            \
  } while (0)

  /* build: everywhere a unit stands that it can pay for */
  {
    order_req r = { .order = O_BUILD, .city = CAPITAL, .unit = -1, .offer = -1 };
    int days;
    if (sim_check(S, p, &r, &days) == WHY_OK) {
      int list[MAX_UNITS], n = sim_buildable(S, p, list);
      float v = 0;
      for (int i = 0; i < n; i++) v += house_value(S, p, pers, list[i]) + 2.0f;
      if (n) r.city = list[0];
      CONSIDER(r, v * 1.4f, days);
    }
  }
  /* move: a unit to a city worth a house, if the road is short enough to pay */
  for (int c = 0; c < N_CITIES; c++) {
    if (CITIES[c].good < 0 || (S->house[c] >> p & 1)) continue;
    bool have_unit = sim_unit_at(S, p, c);
    if (have_unit) continue;
    int u = sim_nearest_unit(S, p, c);
    if (u < 0) continue;
    /* a unit standing where it could still build stays there */
    int uc = pl->units[u].city;
    if (CITIES[uc].good >= 0 && !(S->house[uc] >> p & 1)) continue;
    int days = sim_path(uc, c, NULL, NULL);
    if (days < 0 || days + 3 > days_left) continue;
    float v = house_value(S, p, pers, c) + 1.0f - 0.25f * goods_short(S, p, c);
    order_req r = { .order = O_MOVE, .city = c, .unit = u, .offer = -1 };
    CONSIDER(r, v, days);
  }
  /* harvest: the region where its own houses outnumber the rivals' most */
  for (int reg = 0; reg < N_REGIONS; reg++) {
    if (S->fallow[reg]) continue;
    int city = -1;
    float v = (float)S->price[REGIONS[reg].bonus];
    for (int c = 0; c < N_CITIES; c++) {
      if (CITIES[c].region != reg) continue;
      city = c;
      for (int i = 0; i < NP; i++)
        if (S->house[c] >> i & 1) v += (i == p ? 1.0f : -0.55f) * (float)S->price[CITIES[c].good];
    }
    order_req r = { .order = O_HARVEST, .city = city, .unit = -1, .offer = -1 };
    int days;
    if (city >= 0 && sim_check(S, p, &r, &days) == WHY_OK) CONSIDER(r, v * 0.55f, days);
  }
  /* trade: buy what the next house lacks, or sell a surplus when the purse is thin */
  {
    order_req r = { .order = O_TRADE, .city = CAPITAL, .unit = -1, .offer = -1 };
    int want = -1;
    for (int u = 0; u < pl->n_units && want < 0; u++) {
      int c = pl->units[u].city;
      if (CITIES[c].good < 0 || (S->house[c] >> p & 1)) continue;
      int goods[GOODS];
      sim_house_cost(S, p, c, goods);
      for (int g = 0; g < GOODS; g++)
        if (pl->goods[g] < goods[g]) want = g;
    }
    float v = 0;
    if (want >= 0 && pl->coins >= S->price[want] + 3) r.trade[want] = 1, v = 3.5f;
    else {
      int most = 0;
      for (int g = 1; g < GOODS; g++)
        if (pl->goods[g] * S->price[g] > pl->goods[most] * S->price[most]) most = g;
      if (pl->goods[most] >= 3 && pl->coins < 6) r.trade[most] = -2, v = (float)S->price[most] * 0.5f;
      else if (pl->goods[most] >= 2 && pl->coins < 3) r.trade[most] = -1, v = (float)S->price[most] * 0.6f;
    }
    v += 1.2f; /* the visit's pay */
    int days;
    if (sim_check(S, p, &r, &days) == WHY_OK) CONSIDER(r, v, days);
  }
  /* recruit: more units while there is time to use them */
  {
    order_req r = { .order = O_RECRUIT, .city = CAPITAL, .unit = -1, .offer = -1 };
    for (int c = 0; c < N_CITIES; c++)
      if (S->house[c] >> p & 1) r.city = c; /* the last settled: further out */
    int days;
    if (sim_check(S, p, &r, &days) == WHY_OK && days_left > 40) {
      float v = 2.0f * (float)pl->favor[GOD_MARS] * WEIGHT[pers][GOD_MARS] + (pl->n_units < 4 ? 3.0f : 0) - 2.5f;
      CONSIDER(r, v, days);
    }
  }
  /* the temple: a god whose count is already high is worth courting */
  for (int i = 0; i < OFFERS; i++) {
    order_req r = { .order = O_TEMPLE, .city = CAPITAL, .unit = -1, .offer = i };
    int days;
    if (sim_check(S, p, &r, &days) != WHY_OK) continue;
    const offer *o = &S->offers[i];
    float cost = (float)o->coins;
    for (int g = 0; g < GOODS; g++) cost += (float)(o->cost[g] * S->price[g]);
    /* late in the game the counts are close to final */
    float growth = 1.0f + 0.6f * (float)days_left / (float)(END_TICK / DAY);
    float v = (float)sim_score(S, p, o->god) * WEIGHT[pers][o->god] * growth - cost * 0.3f;
    CONSIDER(r, v, days);
  }
  /* rest: worth it once enough orders are spent */
  {
    order_req r = { .order = O_REST, .city = CAPITAL, .unit = -1, .offer = -1 };
    int days, n = 0;
    for (int o = 0; o < ORDERS; o++) n += pl->spent >> o & 1;
    if (sim_check(S, p, &r, &days) == WHY_OK) CONSIDER(r, (float)n * 1.6f + 0.5f, days);
  }
  /* nothing worth doing: wait a day (a short rest when anything is spent) */
  if (best.order == O_NONE) {
    order_req r = { .order = O_REST, .city = CAPITAL, .unit = -1, .offer = -1 };
    if (sim_check(S, p, &r, NULL) == WHY_OK) best = r;
  }
#undef CONSIDER
  return best;
}
