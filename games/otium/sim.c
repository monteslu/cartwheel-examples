/* otium simulation: integer state on fixed ticks. Time runs only while
 * someone is busy: main.c stops ticking while you are free to give an
 * order, so the rivals never act while you think. Each order is paid for
 * when it is given and keeps its player busy for its days; a move's unit
 * arrives when they pass. Everything else is cartwheel/math.h and integers,
 * so every host plays the same game. */
#include <stddef.h>
#include <string.h>

#include "game.h"

const int ORDER_DAYS[ORDERS] = { 0, 3, 4, 2, 3, 3, 3 };
const int BASE_PRICE[GOODS] = { 2, 3, 4, 5, 6 };
/* the goods a house needs, by the good its city makes */
static const int HOUSE_GOODS[GOODS][GOODS] = {
  /* brick city */ { 0, 1, 0, 0, 0 },
  /* food  */ { 1, 0, 0, 0, 0 },
  /* tools */ { 1, 1, 0, 0, 0 },
  /* wine  */ { 1, 0, 1, 0, 0 },
  /* cloth */ { 1, 0, 0, 1, 0 },
};
static const int HOUSE_COINS[GOODS] = { 1, 2, 3, 4, 5 };
static const int RECRUIT_GOODS[GOODS] = { 0, 1, 1, 0, 0 };
#define RECRUIT_COINS 3

static uint32_t rnd(sim *S) {
  uint32_t x = S->rng;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return S->rng = x;
}

static void new_offer(sim *S, offer *o) {
  memset(o, 0, sizeof *o);
  o->god = (int)(rnd(S) % GODS);
  int a = (int)(rnd(S) % GOODS);
  int b = (int)(rnd(S) % GOODS);
  o->cost[a]++;
  o->cost[b]++;
  o->coins = 2 + (int)(rnd(S) % 3);
}

void sim_start(sim *S, uint32_t seed) {
  map_init();
  memset(S, 0, sizeof *S);
  S->rng = seed ? seed : 0x07105u;
  for (int g = 0; g < GOODS; g++) S->price[g] = BASE_PRICE[g];
  for (int i = 0; i < NP; i++) {
    player *p = &S->p[i];
    p->coins = 8 + i; /* the later houses a little richer, as in a turn order */
    p->goods[GD_BRICK] = 2, p->goods[GD_FOOD] = 2, p->goods[GD_TOOLS] = 1, p->goods[GD_WINE] = 1;
    for (int g = 0; g < GODS; g++) p->favor[g] = 1;
    p->n_units = 2;
    for (int u = 0; u < p->n_units; u++) p->units[u].city = CAPITAL;
    p->order = O_NONE;
    p->free_at = 0;
  }
  for (int i = 0; i < OFFERS; i++) new_offer(S, &S->offers[i]);
}

void sim_clear_events(sim *S) {
  memset(S->ev_build, 0, sizeof S->ev_build);
  memset(S->ev_harvest, 0, sizeof S->ev_harvest);
  memset(S->ev_trade, 0, sizeof S->ev_trade);
  memset(S->ev_recruit, 0, sizeof S->ev_recruit);
  memset(S->ev_temple, 0, sizeof S->ev_temple);
  memset(S->ev_rest, 0, sizeof S->ev_rest);
  memset(S->ev_arrive, 0, sizeof S->ev_arrive);
  S->ev_season = S->ev_end = 0;
}

int sim_season(const sim *S) { return S->tick / (SEASON_DAYS * DAY); }
bool sim_idle(const sim *S, int p) { return !S->over && S->p[p].free_at <= S->tick; }
void sim_wait(sim *S, int p, int days) {
  if (S->p[p].free_at <= S->tick) S->p[p].free_at = S->tick + days * DAY;
}

/* ── paths: Dijkstra over the edges, in days ─────────────────────────── */

int sim_path(int from, int to, int path[MAX_PATH], int *n) {
  int dist[MAX_CITIES], prev[MAX_CITIES];
  bool done[MAX_CITIES] = { 0 };
  for (int i = 0; i < N_CITIES; i++) dist[i] = 1 << 28, prev[i] = -1;
  dist[from] = 0;
  for (;;) {
    int c = -1;
    for (int i = 0; i < N_CITIES; i++)
      if (!done[i] && (c < 0 || dist[i] < dist[c])) c = i;
    if (c < 0 || dist[c] >= 1 << 28) break;
    done[c] = true;
    if (c == to) break;
    for (int e = 0; e < N_EDGES; e++) {
      int o = EDGES[e].a == c ? EDGES[e].b : EDGES[e].b == c ? EDGES[e].a : -1;
      if (o < 0 || done[o]) continue;
      int d = dist[c] + EDGES[e].days;
      if (d < dist[o]) dist[o] = d, prev[o] = c;
    }
  }
  if (dist[to] >= 1 << 28) return -1;
  if (path && n) {
    int rev[MAX_CITIES], k = 0;
    for (int c = to; c >= 0 && k < MAX_CITIES; c = prev[c]) rev[k++] = c;
    *n = k < MAX_PATH ? k : MAX_PATH;
    for (int i = 0; i < *n; i++) path[i] = rev[k - 1 - i];
  }
  return dist[to];
}

static int edge_days(int a, int b, bool *sea) {
  for (int e = 0; e < N_EDGES; e++)
    if ((EDGES[e].a == a && EDGES[e].b == b) || (EDGES[e].a == b && EDGES[e].b == a)) {
      if (sea) *sea = EDGES[e].sea;
      return EDGES[e].days;
    }
  return 1;
}

static bool unit_moving(const sim *S, const player *pl, const unit *u) { return u->path_n > 1 && S->tick < u->t1; }

bool sim_unit_at(const sim *S, int p, int city) {
  const player *pl = &S->p[p];
  for (int u = 0; u < pl->n_units; u++)
    if (!unit_moving(S, pl, &pl->units[u]) && pl->units[u].city == city) return true;
  return false;
}

int sim_nearest_unit(const sim *S, int p, int city) {
  const player *pl = &S->p[p];
  int best = -1, bd = 1 << 28;
  for (int u = 0; u < pl->n_units; u++) {
    const unit *un = &pl->units[u];
    if (unit_moving(S, pl, un) || un->city == city) continue;
    int d = sim_path(un->city, city, NULL, NULL);
    if (d >= 0 && d < bd) bd = d, best = u;
  }
  return best;
}

/* ── costs ───────────────────────────────────────────────────────────── */

int sim_house_cost(const sim *S, int p, int city, int goods[GOODS]) {
  int g = CITIES[city].good, others = 0;
  for (int i = 0; i < NP; i++)
    if (i != p && (S->house[city] >> i & 1)) others++;
  for (int k = 0; k < GOODS; k++) goods[k] = g >= 0 ? HOUSE_GOODS[g][k] : 0;
  return g >= 0 ? HOUSE_COINS[g] * (1 + others) : 0;
}

static bool can_pay(const player *pl, const int goods[GOODS], int coins) {
  if (pl->coins < coins) return false;
  for (int g = 0; g < GOODS; g++)
    if (pl->goods[g] < goods[g]) return false;
  return true;
}
static void pay(player *pl, const int goods[GOODS], int coins) {
  pl->coins -= coins;
  for (int g = 0; g < GOODS; g++) pl->goods[g] -= goods[g];
}

bool sim_can_build(const sim *S, int p, int city) {
  if (CITIES[city].good < 0 || (S->house[city] >> p & 1) || !sim_unit_at(S, p, city)) return false;
  int goods[GOODS], coins = sim_house_cost(S, p, city, goods);
  return can_pay(&S->p[p], goods, coins);
}

/* the cities a build would build in, in the order it pays for them (the
 * cursor's first is main.c's business: it passes it as r->city) */
static int build_list(const sim *S, int p, int first, int list[MAX_UNITS]) {
  player tmp = S->p[p];
  int n = 0;
  uint8_t seen[MAX_CITIES] = { 0 };
  int order[MAX_UNITS + 1], k = 0;
  if (first >= 0) order[k++] = first;
  for (int u = 0; u < tmp.n_units; u++) order[k++] = tmp.units[u].city;
  for (int i = 0; i < k && n < MAX_UNITS; i++) {
    int c = order[i];
    if (c < 0 || seen[c] || CITIES[c].good < 0 || (S->house[c] >> p & 1) || !sim_unit_at(S, p, c)) continue;
    seen[c] = 1;
    int goods[GOODS], coins = sim_house_cost(S, p, c, goods);
    if (!can_pay(&tmp, goods, coins)) continue;
    pay(&tmp, goods, coins);
    list[n++] = c;
  }
  return n;
}
int sim_buildable(const sim *S, int p, int list[MAX_UNITS]) { return build_list(S, p, -1, list); }

/* a market visit: lots bought (+) or sold (-), one at a time, the price moving after each */
static bool trade_run(const int price_in[GOODS], const int trade[GOODS], const player *pl, int *coins_delta) {
  int price[GOODS], coins = pl->coins, lots = 0;
  memcpy(price, price_in, sizeof price);
  for (int g = 0; g < GOODS; g++) {
    int q = trade[g];
    lots += q < 0 ? -q : q;
    if (q < 0 && pl->goods[g] < -q) return false;
    for (; q < 0; q++) coins += price[g], price[g] = price[g] > 1 ? price[g] - 1 : 1;
    for (; q > 0; q--) coins -= price[g] + 1, price[g]++;
  }
  if (coins_delta) *coins_delta = coins - pl->coins;
  return lots > 0 && lots <= TRADE_LOTS && coins >= 0;
}
void sim_trade_cost(const sim *S, const int trade[GOODS], int *coins_delta) {
  player rich = { 0 };
  rich.coins = 1 << 20;
  for (int g = 0; g < GOODS; g++) rich.goods[g] = 99;
  *coins_delta = 0;
  trade_run(S->price, trade, &rich, coins_delta);
}

/* ── orders ──────────────────────────────────────────────────────────── */

int sim_check(const sim *S, int p, const order_req *r, int *days) {
  const player *pl = &S->p[p];
  int d = r->order >= 0 && r->order < ORDERS ? ORDER_DAYS[r->order] : 0;
  if (days) *days = d;
  if (S->over || pl->free_at > S->tick) return WHY_BUSY;
  if (r->order != O_MOVE && (pl->spent >> r->order & 1)) return WHY_SPENT;
  switch (r->order) {
  case O_MOVE: {
    int u = r->unit >= 0 ? r->unit : sim_nearest_unit(S, p, r->city);
    if (u < 0 || u >= pl->n_units || pl->units[u].city == r->city) return WHY_NO_UNIT;
    int dd = sim_path(pl->units[u].city, r->city, NULL, NULL);
    if (dd < 0) return WHY_NO_UNIT;
    if (days) *days = dd;
    return WHY_OK;
  }
  case O_BUILD: {
    int list[MAX_UNITS];
    if (build_list(S, p, r->city, list) > 0) return WHY_OK;
    bool any_spot = false;
    for (int u = 0; u < pl->n_units; u++) {
      int c = pl->units[u].city;
      if (CITIES[c].good >= 0 && !(S->house[c] >> p & 1) && sim_unit_at(S, p, c)) any_spot = true;
    }
    return any_spot ? WHY_POOR : WHY_NO_UNIT;
  }
  case O_HARVEST: {
    int reg = CITIES[r->city].region;
    if (reg < 0) return WHY_NOTHING;
    if (S->fallow[reg]) return WHY_FALLOW;
    for (int c = 0; c < N_CITIES; c++)
      if (CITIES[c].region == reg && S->house[c]) return WHY_OK;
    return WHY_NOTHING;
  }
  case O_TRADE: return trade_run(S->price, r->trade, pl, NULL) ? WHY_OK : WHY_POOR;
  case O_RECRUIT:
    if (pl->n_units >= MAX_UNITS) return WHY_FULL;
    return can_pay(pl, RECRUIT_GOODS, RECRUIT_COINS) ? WHY_OK : WHY_POOR;
  case O_TEMPLE: {
    if (r->offer < 0 || r->offer >= OFFERS) return WHY_NOTHING;
    const offer *o = &S->offers[r->offer];
    return can_pay(pl, o->cost, o->coins) ? WHY_OK : WHY_POOR;
  }
  case O_REST: return pl->spent ? WHY_OK : WHY_NOTHING;
  default: return WHY_NOTHING;
  }
}

bool sim_order(sim *S, int p, const order_req *r) {
  int days;
  if (sim_check(S, p, r, &days) != WHY_OK) return false;
  player *pl = &S->p[p];
  pl->order = r->order;
  pl->order_city = r->city;
  pl->order_unit = -1;
  switch (r->order) {
  case O_MOVE: {
    int u = r->unit >= 0 ? r->unit : sim_nearest_unit(S, p, r->city);
    unit *un = &pl->units[u];
    sim_path(un->city, r->city, un->path, &un->path_n);
    un->t0 = S->tick;
    un->t1 = S->tick + days * DAY;
    pl->order_unit = u;
    break;
  }
  case O_BUILD: {
    int list[MAX_UNITS], n = build_list(S, p, r->city, list);
    for (int i = 0; i < n; i++) {
      int goods[GOODS], coins = sim_house_cost(S, p, list[i], goods);
      pay(pl, goods, coins);
      S->house[list[i]] |= (uint8_t)(1u << p);
      pl->houses++;
    }
    S->ev_build[p] = n;
    S->ev_build_city[p] = list[0];
    break;
  }
  case O_HARVEST: {
    int reg = CITIES[r->city].region;
    for (int c = 0; c < N_CITIES; c++)
      if (CITIES[c].region == reg)
        for (int i = 0; i < NP; i++)
          if (S->house[c] >> i & 1) S->p[i].goods[CITIES[c].good]++;
    pl->goods[REGIONS[reg].bonus]++;
    S->fallow[reg] = 1;
    S->ev_harvest[p] = 1;
    S->ev_harvest_region[p] = reg;
    break;
  }
  case O_TRADE: {
    int delta;
    trade_run(S->price, r->trade, pl, &delta);
    for (int g = 0; g < GOODS; g++) {
      int q = r->trade[g];
      pl->goods[g] += q;
      for (; q < 0; q++) S->price[g] = S->price[g] > 1 ? S->price[g] - 1 : 1;
      for (; q > 0; q--) S->price[g]++;
      pl->last_trade[g] = r->trade[g];
    }
    pl->coins += delta;
    S->ev_trade[p] = 1;
    break;
  }
  case O_RECRUIT: {
    pay(pl, RECRUIT_GOODS, RECRUIT_COINS);
    int c = CITIES[r->city].good >= 0 && (S->house[r->city] >> p & 1) ? r->city : CAPITAL;
    unit *un = &pl->units[pl->n_units++];
    memset(un, 0, sizeof *un);
    un->city = c;
    pl->order_city = c;
    S->ev_recruit[p] = 1;
    break;
  }
  case O_TEMPLE: {
    offer *o = &S->offers[r->offer];
    pay(pl, o->cost, o->coins);
    pl->favor[o->god]++;
    S->ev_temple[p] = o->god + 1;
    new_offer(S, o);
    break;
  }
  case O_REST: {
    int n = 0;
    for (int o = 0; o < ORDERS; o++) n += pl->spent >> o & 1;
    pl->coins += n + 1;
    pl->spent = 0;
    S->ev_rest[p] = n + 1;
    break;
  }
  }
  if (r->order != O_MOVE && r->order != O_REST) pl->spent |= 1u << r->order;
  pl->free_at = S->tick + days * DAY;
  S->orders_done[p]++;
  return true;
}

/* ── the clock ───────────────────────────────────────────────────────── */

static void season_turn(sim *S) {
  memset(S->fallow, 0, sizeof S->fallow);
  for (int g = 0; g < GOODS; g++) S->price[g] += S->price[g] < BASE_PRICE[g] ? 1 : S->price[g] > BASE_PRICE[g] ? -1 : 0;
  /* the oldest offer leaves the temple */
  S->offers[0] = S->offers[1];
  S->offers[1] = S->offers[2];
  new_offer(S, &S->offers[2]);
  S->ev_season = 1;
}

bool sim_tick(sim *S) {
  if (S->over) return false;
  S->tick++;
  for (int i = 0; i < NP; i++) {
    player *pl = &S->p[i];
    for (int u = 0; u < pl->n_units; u++) {
      unit *un = &pl->units[u];
      if (un->path_n > 1 && S->tick >= un->t1) {
        un->city = un->path[un->path_n - 1];
        un->path_n = 0;
        S->ev_arrive[i] = 1;
      }
    }
    if (pl->free_at <= S->tick) pl->order = O_NONE;
  }
  if (S->tick % (SEASON_DAYS * DAY) == 0 && S->tick < END_TICK) season_turn(S);
  if (S->tick >= END_TICK) {
    S->over = true;
    S->ev_end = 1;
  }
  return !S->over;
}

/* ── scoring ─────────────────────────────────────────────────────────── */

int sim_score(const sim *S, int p, int god) {
  const player *pl = &S->p[p];
  switch (god) {
  case GOD_JUPITER: { /* houses in cities that are not brickworks */
    int n = 0;
    for (int c = 0; c < N_CITIES; c++)
      if ((S->house[c] >> p & 1) && CITIES[c].good != GD_BRICK) n++;
    return n;
  }
  case GOD_SATURN: { /* regions settled, two each */
    int n = 0;
    for (int r = 0; r < N_REGIONS; r++) {
      bool in = false;
      for (int c = 0; c < N_CITIES; c++)
        if (CITIES[c].region == r && (S->house[c] >> p & 1)) in = true;
      n += in;
    }
    return n * 2;
  }
  case GOD_MERCURY: { /* kinds of goods made, two each */
    int mask = 0;
    for (int c = 0; c < N_CITIES; c++)
      if ((S->house[c] >> p & 1) && CITIES[c].good >= 0) mask |= 1 << CITIES[c].good;
    int n = 0;
    for (int g = 0; g < GOODS; g++) n += mask >> g & 1;
    return n * 2;
  }
  case GOD_MARS: return pl->n_units * 3; /* caravans and galleys, three each */
  case GOD_VESTA: { /* wealth: coins and the goods at market price, a point per 3 */
    int w = pl->coins;
    for (int g = 0; g < GOODS; g++) w += pl->goods[g] * S->price[g];
    return w / 3;
  }
  }
  return 0;
}
int sim_total(const sim *S, int p) {
  int t = 0;
  for (int g = 0; g < GODS; g++) t += S->p[p].favor[g] * sim_score(S, p, g);
  return t;
}

uint32_t sim_hash(const sim *S) {
  uint32_t h = 2166136261u;
  const uint8_t *b = (const uint8_t *)S;
  /* everything up to the events (the stats after them too) */
  size_t n = offsetof(sim, ev_build);
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

/* ── presentation helper: where a unit is at a fractional tick ──────── */

float sim_unit_pos(const sim *S, int p, int u, float tick_f, float *x, float *z, bool *at_sea, float *heading) {
  const unit *un = &S->p[p].units[u];
  *at_sea = false;
  *heading = 0;
  if (un->path_n < 2 || tick_f >= (float)un->t1) {
    int c = un->path_n >= 2 ? un->path[un->path_n - 1] : un->city;
    *x = map_x(CITIES[c].lon), *z = map_z(CITIES[c].lat);
    return 1;
  }
  float k = cw_clampf((tick_f - un->t0) / (float)(un->t1 - un->t0), 0, 1);
  int total = 0;
  for (int i = 0; i + 1 < un->path_n; i++) total += edge_days(un->path[i], un->path[i + 1], NULL);
  float at = k * total;
  for (int i = 0; i + 1 < un->path_n; i++) {
    bool sea;
    int d = edge_days(un->path[i], un->path[i + 1], &sea);
    if (at <= d || i + 2 == un->path_n) {
      float f = cw_clampf(at / d, 0, 1);
      const city_def *a = &CITIES[un->path[i]], *b = &CITIES[un->path[i + 1]];
      float ax = map_x(a->lon), az = map_z(a->lat), bx = map_x(b->lon), bz = map_z(b->lat);
      *x = ax + (bx - ax) * f, *z = az + (bz - az) * f;
      *at_sea = sea;
      *heading = cw_atan2(bx - ax, bz - az);
      return k;
    }
    at -= d;
  }
  *x = map_x(CITIES[un->city].lon), *z = map_z(CITIES[un->city].lat);
  return k;
}
