/* bomberdude: round statistics from the rules alone (sim.c and ai.c, no
 * graphics), for tuning the arenas and the revenge rule. Four CPUs play many
 * rounds of each arena, seeded the way the game seeds them, and it prints per
 * arena: median ticks to the first contact (two players with an open path of
 * at most 4 tiles between them), median ticks to the first knockout, the share
 * of players who knock themselves out in the first 10 seconds, and the longest
 * time any seat went without being able to act (a knocked-out seat counts the
 * whole time it has nothing to do).
 *
 *   cc -O2 -I deps/cartwheel-sdk/linux-x64/include -o build/sim-stats \
 *      games/bomberdude/tools/sim-stats.c games/bomberdude/sim.c games/bomberdude/ai.c -lm
 *   build/sim-stats [rounds per arena] [cpu level 0..2] [revenge 0|1]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game.h"

static char LAYOUT[ARENAS][GW * GH + GH + 1];
static arena_info INFO[ARENAS];

static const char *THEMES[TH_KINDS] = { "MEADOW", "FROST", "FORGE", "TEMPLE", "CANDY", "NEON", "PIRATE", "DESERT" };

static void load(int i, const char *dir) {
  char path[512], line[1024];
  snprintf(path, sizeof path, "%s/arena%d.cwscene", dir, i + 1);
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "no %s\n", path);
    exit(2);
  }
  arena_info *a = &INFO[i];
  a->index = i + 1;
  bool in_arena = false;
  while (fgets(line, sizeof line, f)) {
    if (!strncmp(line, "[node", 5)) in_arena = strstr(line, "type=\"Arena\"") != NULL;
    if (!in_arena) continue;
    if (!strncmp(line, "layout = \"", 10)) {
      char *e = strchr(line + 10, '"');
      if (e) *e = 0;
      snprintf(LAYOUT[i], sizeof LAYOUT[i], "%s", line + 10);
      a->layout = LAYOUT[i];
    }
    if (!strncmp(line, "theme = ", 8))
      for (int t = 0; t < TH_KINDS; t++)
        if (!strncmp(line + 8, THEMES[t], strlen(THEMES[t]))) a->theme = t;
    sscanf(line, "crate_pct = %d", &a->crate_pct);
    sscanf(line, "seed = %d", &a->seed);
    sscanf(line, "round_secs = %d", &a->round_secs);
  }
  fclose(f);
}

/* an open path (no wall or crate; bombs ignored) of at most 4 tiles between two live players */
static bool contact(const sim *s) {
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (!p->on || !p->alive) continue;
    int16_t d[GH][GW];
    memset(d, -1, sizeof d);
    int qx[GW * GH], qy[GW * GH], h = 0, t = 0;
    int sx = tile_of(p->x), sy = tile_of(p->y);
    d[sy][sx] = 0;
    qx[t] = sx, qy[t++] = sy;
    while (h < t) {
      int x = qx[h], y = qy[h++];
      if (d[y][x] >= 4) continue;
      static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, -1, 0, 1 };
      for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || d[ny][nx] >= 0 || s->block[ny][nx] != C_EMPTY) continue;
        d[ny][nx] = (int16_t)(d[y][x] + 1);
        qx[t] = nx, qy[t++] = ny;
      }
    }
    for (int j = i + 1; j < MAXP; j++) {
      const player *q = &s->p[j];
      if (q->on && q->alive && d[tile_of(q->y)][tile_of(q->x)] >= 0) return true;
    }
  }
  return false;
}

/* a first bomb dropped on the start tile (range 2) has a tile out of its cross
 * the player can walk to, at most 4 steps away, before it goes off */
static bool start_safe(const sim *s, int who) {
  const player *p = &s->p[who];
  int sx = tile_of(p->x), sy = tile_of(p->y);
  int16_t d[GH][GW];
  memset(d, -1, sizeof d);
  int qx[GW * GH], qy[GW * GH], h = 0, t = 0;
  d[sy][sx] = 0;
  qx[t] = sx, qy[t++] = sy;
  static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, -1, 0, 1 };
  while (h < t) {
    int x = qx[h], y = qy[h++];
    bool in_cross = (x == sx && IABS(y - sy) <= 2) || (y == sy && IABS(x - sx) <= 2);
    if (!in_cross) return true;
    if (d[y][x] >= 4) continue;
    for (int k = 0; k < 4; k++) {
      int nx = x + DX[k], ny = y + DY[k];
      if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || d[ny][nx] >= 0 || s->block[ny][nx] != C_EMPTY) continue;
      d[ny][nx] = (int16_t)(d[y][x] + 1);
      qx[t] = nx, qy[t++] = ny;
    }
  }
  return false;
}

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(int argc, char **argv) {
  int rounds = argc > 1 ? atoi(argv[1]) : 40, level = argc > 2 ? atoi(argv[2]) : 1;
  int revenge = argc > 3 ? atoi(argv[3]) : 1;
  const char *dir = getenv("SCENES") ? getenv("SCENES") : "games/bomberdude/assets/scenes";
  for (int i = 0; i < ARENAS; i++) load(i, dir);
  if (getenv("SIM_RECORD")) {
    /* SIM_RECORD=<file> SIM_ARENA=n SIM_PLAYERS=n: round 1 of a Battle match of hard CPUs, its inputs as the
     * game's BOMBERDUDE_RECORD writes them ("tick in0 in1 in2 in3"; tools/make-gate-script.mjs reads it) */
    int a = getenv("SIM_ARENA") ? atoi(getenv("SIM_ARENA")) - 1 : 0, np = getenv("SIM_PLAYERS") ? atoi(getenv("SIM_PLAYERS")) : 2;
    FILE *f = fopen(getenv("SIM_RECORD"), "w");
    static sim S;
    bool on[MAXP];
    for (int i = 0; i < MAXP; i++) on[i] = i < np;
    sim_load(&S, &INFO[a], on, 1000u + 7919u);
    S.revenge = revenge;
    ai_mem m[MAXP];
    for (int i = 0; i < MAXP; i++) ai_reset(&m[i], i, 2);
    int lv[MAXP] = { 2, 2, 2, 2 }; /* SIM_LEVELS=2111: each seat's CPU level (the _auto build: P1 hard, the rest normal) */
    for (int i = 0; i < MAXP && getenv("SIM_LEVELS") && getenv("SIM_LEVELS")[i]; i++) lv[i] = getenv("SIM_LEVELS")[i] - '0';
    while (S.winner == -2 && S.tick < 60 * 400) {
      uint8_t in[MAXP] = { 0 };
      for (int i = 0; i < np; i++) in[i] = ai_think(&S, i, lv[i], &m[i]);
      sim_step(&S, in);
      fprintf(f, "%d %d %d %d %d\n", S.tick, in[0], in[1], in[2], in[3]);
      if (getenv("SIM_HASH_AT") && S.tick == atoi(getenv("SIM_HASH_AT"))) printf("tick %d hash %u\n", S.tick, S.hash);
    }
    fclose(f);
    int kos = 0;
    for (int i = 0; i < MAXP; i++) kos += S.p[i].kos;
    printf("recorded %d ticks, chains %d, winner %d, kos %d, swaps %d\n", S.tick, S.chains, S.winner, kos, S.rev_swaps);
    return 0;
  }
  printf("arena                 contact_s  first_ko_s  self_ko_10s%%  round_s  worst_idle_s  rev_throws  rev_swaps  unsafe_starts  glove_throws\n");
  int all_idle = 0;
  for (int a = 0; a < ARENAS; a++) {
    int *tc = calloc((size_t)rounds, sizeof(int)), *tk = calloc((size_t)rounds, sizeof(int)), *tr = calloc((size_t)rounds, sizeof(int));
    int gloves = 0, unsafe = 0, self_early = 0, worst_idle = 0, throws = 0, swaps = 0;
    for (int r = 0; r < rounds; r++) {
      static sim S;
      bool on[MAXP] = { true, true, true, true };
      sim_load(&S, &INFO[a], on, 1000u + (uint32_t)(r + 1) * 7919u);
#ifndef BASELINE
      S.revenge = revenge;
#else
      (void)revenge;
#endif
      int idle[MAXP] = { 0 };
      for (int i = 0; i < MAXP; i++) unsafe += !start_safe(&S, i);
      ai_mem m[MAXP];
      for (int i = 0; i < MAXP; i++) ai_reset(&m[i], i, (uint32_t)(r + 1) + 1);
      tc[r] = tk[r] = -1;
      while (S.winner == -2 && S.tick < 60 * 400) {
        uint8_t in[MAXP];
        for (int i = 0; i < MAXP; i++) in[i] = ai_think(&S, i, level, &m[i]);
        sim_step(&S, in);
        if (tc[r] < 0 && contact(&S)) tc[r] = S.tick;
        for (int i = 0; i < MAXP; i++) {
#ifndef BASELINE
          bool can = sim_can_act(&S, i);
#else
          bool can = S.p[i].alive;
#endif
          idle[i] = can ? 0 : idle[i] + 1;
          if (idle[i] > worst_idle) worst_idle = idle[i];
        }
        for (int e = 0; e < S.ev_n; e++) {
          if (S.ev[e].kind == EV_KO && tk[r] < 0) tk[r] = S.tick;
          if (S.ev[e].kind == EV_KO && S.ev[e].a == S.ev[e].b && S.tick <= 600) self_early++;
        }
      }
#ifndef BASELINE
      throws += S.rev_throws;
      swaps += S.rev_swaps;
      for (int i = 0; i < MAXP; i++) gloves += S.p[i].throws;
      gloves -= S.rev_throws;
#endif
      tr[r] = S.tick;
      if (tc[r] < 0) tc[r] = S.tick;
      if (tk[r] < 0) tk[r] = S.tick;
    }
    qsort(tc, (size_t)rounds, sizeof(int), cmp);
    qsort(tk, (size_t)rounds, sizeof(int), cmp);
    qsort(tr, (size_t)rounds, sizeof(int), cmp);
    printf("%-20s  %9.1f  %10.1f  %12.1f  %7.1f  %12.1f  %10d  %9d  %13d  %12d\n", INFO[a].layout ? (a == 0 ? "1 Clover Commons" : a == 1 ? "2 Frostbite Pond" : a == 2 ? "3 Ember Forge" : a == 3 ? "4 Jungle Temple" : a == 4 ? "5 Gumdrop Gardens" : a == 5 ? "6 Neon Nights" : a == 6 ? "7 Powder Deck" : "8 Sandstorm Ruins") : "?",
           tc[rounds / 2] / 60.0, tk[rounds / 2] / 60.0, 100.0 * self_early / (rounds * MAXP), tr[rounds / 2] / 60.0, worst_idle / 60.0, throws, swaps, unsafe, gloves);
    if (worst_idle > all_idle) all_idle = worst_idle;
    free(tc), free(tk), free(tr);
  }
  printf("worst idle over every arena: %.1f s\n", all_idle / 60.0);
  return 0;
}
