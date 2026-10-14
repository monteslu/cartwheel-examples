/* bomberdude's CPU players. Each one reads the simulation (never the
 * screen), so it is as deterministic as the rules: the same round plays the
 * same way on every host.
 *
 * Every think it reads the danger map (ticks until each tile burns, chains
 * included; sim_refresh builds it once a tick for every CPU) and then, in
 * order:
 *   1. in danger: run to the nearest tile no blast will reach, by a path it
 *      can walk before the fire gets there
 *   2. standing where a bomb would pay (crates, a rival in line) with a way
 *      out afterwards: drop it
 *   3. otherwise walk to the best goal: a power-up, a spot next to crates, or
 *      (normal and hard) a spot that traps a rival
 * Easy thinks slowly, misses some chances and wanders; hard thinks every
 * tick, hunts, and kicks. */
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, -1, 0, 1 };
static const uint8_t DIR_IN[4] = { IN_RIGHT, IN_UP, IN_LEFT, IN_DOWN };

enum { M_IDLE, M_FLEE, M_SEEK, M_BOMB, M_WANDER };

void ai_reset(ai_mem *m, int me, uint32_t seed) {
  memset(m, 0, sizeof *m);
  m->goal_x = m->goal_y = -1;
  m->rng = (seed * 2654435761u) ^ (uint32_t)(me + 1) * 40503u;
  if (!m->rng) m->rng = 1;
  m->next_think = me * 3; /* not all on the same tick */
}

static uint32_t rnd(ai_mem *m) {
  uint32_t x = m->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return m->rng = x ? x : 1;
}

typedef struct bfs {
  int16_t dist[GH][GW];
  int8_t first[GH][GW]; /* the first step's direction from the start */
} bfs;

/* sim_passable from the tick's bomb index (no scan of every bomb slot) */
static bool passable(const sim *s, int cx, int cy, int who) {
  if (cx < 0 || cy < 0 || cx >= GW || cy >= GH || s->block[cy][cx] != C_EMPTY) return false;
  int b = s->bomb_at[cy][cx];
  return b < 0 || (who >= 0 && (s->bombs[b].pass >> who & 1));
}

/* a tile the walk may step onto: fire there now, or due while we would be crossing it, rules it out */
static bool fire_free(int dz, int arrive, int tpt) { return !(dz == 0 || (dz < 999 && dz <= arrive + tpt + 6 && dz + 40 >= arrive)); }

/* breadth-first from (sx, sy) over tiles the player may walk, skipping
 * tiles the fire reaches while it would be on them */
static void walk(const sim *s, int me, int sx, int sy, const int16_t danger[GH][GW], int tpt, bfs *o) {
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) o->dist[y][x] = -1, o->first[y][x] = -1;
  int qx[GW * GH], qy[GW * GH], head = 0, tail = 0;
  o->dist[sy][sx] = 0;
  qx[tail] = sx, qy[tail++] = sy;
  while (head < tail) {
    int x = qx[head], y = qy[head++];
    int k = o->dist[y][x];
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || o->dist[ny][nx] >= 0) continue;
      if (!passable(s, nx, ny, me) || !fire_free(danger[ny][nx], (k + 1) * tpt, tpt)) continue;
      o->dist[ny][nx] = (int16_t)(k + 1);
      o->first[ny][nx] = (int8_t)(k == 0 ? d : o->first[y][x]);
      qx[tail] = nx, qy[tail++] = ny;
    }
  }
}

/* the blast cross of a bomb that would sit at (cx, cy) */
static void cross(const sim *s, int cx, int cy, int range, bool pierce, int16_t d[GH][GW], int16_t t) {
  if (d[cy][cx] > t) d[cy][cx] = t;
  for (int k4 = 0; k4 < 4; k4++)
    for (int k = 1; k <= range; k++) {
      int x = cx + DX[k4] * k, y = cy + DY[k4] * k;
      if (x < 0 || y < 0 || x >= GW || y >= GH || s->block[y][x] == C_WALL) break;
      if (d[y][x] > t) d[y][x] = t;
      if (s->block[y][x] == C_CRATE && !pierce) break;
    }
}

/* would a bomb here leave a way out? A tile the walk reaches that no blast
 * touches, soon enough: the walk from (cx, cy) with the new bomb's cross in
 * the danger map, stopped at the first such tile (breadth-first distances
 * are final when a tile is first reached, and only grow, so the answer is
 * the full walk's) */
static bool can_escape(const sim *s, int me, int cx, int cy, const int16_t danger[GH][GW], int tpt, int margin) {
  int16_t d2[GH][GW];
  memcpy(d2, danger, sizeof d2);
  const player *p = &s->p[me];
  cross(s, cx, cy, p->range, p->pierce, d2, 150); /* the bomb's own tile too: never safe */
  int16_t dist[GH][GW];
  memset(dist, -1, sizeof dist);
  int qx[GW * GH], qy[GW * GH], head = 0, tail = 0;
  dist[cy][cx] = 0;
  qx[tail] = cx, qy[tail++] = cy;
  while (head < tail) {
    int x = qx[head], y = qy[head++];
    int k = dist[y][x];
    if ((k + 1) * tpt + margin >= 150) return false; /* every tile still to come is too far */
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || dist[ny][nx] >= 0) continue;
      if (!passable(s, nx, ny, me) || !fire_free(d2[ny][nx], (k + 1) * tpt, tpt)) continue;
      if (d2[ny][nx] == 999) return true;
      dist[ny][nx] = (int16_t)(k + 1);
      qx[tail] = nx, qy[tail++] = ny;
    }
  }
  return false;
}

/* what a bomb at (cx, cy) would hit */
static int bomb_value(const sim *s, int me, int cx, int cy, int level) {
  const player *p = &s->p[me];
  int v = 0;
  for (int d = 0; d < 4; d++)
    for (int k = 1; k <= p->range; k++) {
      int x = cx + DX[d] * k, y = cy + DY[d] * k;
      if (x < 0 || y < 0 || x >= GW || y >= GH || s->block[y][x] == C_WALL) break;
      if (s->block[y][x] == C_CRATE) {
        if (!s->burn[y][x]) v += 10;
        if (!p->pierce) break;
      }
      if (s->item[y][x] && s->block[y][x] == C_EMPTY) v -= 6; /* would burn a power-up */
      for (int i = 0; i < MAXP; i++) {
        const player *q = &s->p[i];
        if (i != me && q->on && q->alive && tile_of(q->x) == x && tile_of(q->y) == y) v += level == 0 ? 8 : level == 1 ? 22 : 34;
      }
    }
  return v;
}

static int bombs_out(const sim *s, int me) {
  int n = 0;
  for (int i = 0; i < MAXB; i++) n += s->bombs[i].live && s->bombs[i].owner == me;
  return n;
}

uint8_t ai_think(const sim *s, int me, int level, ai_mem *m) {
  const player *p = &s->p[me];
  if (!p->on || !p->alive || s->over_tick) return 0;
  int mx = tile_of(p->x), my = tile_of(p->y);
  int tpt = (SUB + p->speed - 1) / p->speed; /* ticks per tile */
  const int16_t(*danger)[GW] = s->danger;
  int mine_out = bombs_out(s, me);
  bool in_danger = danger[my][mx] < 999;
  /* thinking costs time: easy reacts late, but never sits still in a blast */
  int every = level == 0 ? 14 : level == 1 ? 6 : 2;
  bool centred = IABS(p->x - centre_of(mx)) <= p->speed && IABS(p->y - centre_of(my)) <= p->speed;
  if (s->tick < m->next_think && !(in_danger && m->mode != M_FLEE) && !(centred && m->goal_x == mx && m->goal_y == my))
    return m->held & (uint8_t)~IN_BOMB;
  m->next_think = s->tick + every;
  bfs b;
  walk(s, me, mx, my, danger, tpt, &b);
  uint8_t out = 0;

  /* remote bombs: set them off once I am clear of them */
  if (p->remote && !in_danger) {
    for (int i = 0; i < MAXB; i++) {
      const bomb *bb = &s->bombs[i];
      if (!bb->live || bb->owner != me || bb->fuse >= 0) continue;
      int16_t d2[GH][GW];
      for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) d2[y][x] = 999;
      cross(s, bb->cx, bb->cy, bb->range, bb->pierce, d2, 0);
      if (d2[my][mx] == 999) out |= IN_ACT;
      break;
    }
  }

  if (in_danger) {
    /* a spare bomb dropped on the way out, when it pays and the way stays open (chains) */
    if (level > 0 && mine_out < p->bombs && s->bomb_at[my][mx] < 0 && centred && danger[my][mx] > 40 &&
        bomb_value(s, me, mx, my, level) >= 10 && can_escape(s, me, mx, my, danger, tpt, 30)) {
      m->next_think = s->tick + 1;
      m->held = 0;
      return out | IN_BOMB;
    }
    /* 1. run: the nearest safe tile */
    int best = -1, bx = -1, by = -1;
    for (int y = 0; y < GH; y++)
      for (int x = 0; x < GW; x++)
        if (b.dist[y][x] > 0 && danger[y][x] == 999 && (best < 0 || b.dist[y][x] < best)) best = b.dist[y][x], bx = x, by = y;
    m->mode = M_FLEE;
    if (best > 0) {
      m->goal_x = bx;
      m->goal_y = by;
      out |= DIR_IN[b.first[by][bx]];
    } else { /* nowhere safe: anywhere further from the bombs beats here */
      int far = -1;
      for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
          if (b.dist[y][x] > 0 && danger[y][x] > far) far = danger[y][x], bx = x, by = y;
      if (far >= 0) out |= DIR_IN[b.first[by][bx]];
    }
    m->held = out;
    return out;
  }

  /* 2. a bomb here? */
  bool can_drop = mine_out < p->bombs && s->bomb_at[my][mx] < 0 && centred;
  if (can_drop) {
    int v = bomb_value(s, me, mx, my, level);
    int want = level == 0 ? 10 : 8;
    bool at_goal = m->goal_x == mx && m->goal_y == my;
    if (v >= want && (at_goal || v >= 20 || level == 2) && (level > 0 || (rnd(m) & 3) != 0) &&
        can_escape(s, me, mx, my, danger, tpt, level == 0 ? 40 : 24)) {
      m->mode = M_BOMB;
      m->goal_x = m->goal_y = -1;
      m->next_think = s->tick + 1; /* run next tick */
      m->held = 0;
      return out | IN_BOMB;
    }
  }

  /* 3. a goal: score every reachable tile */
  int best = -9999, gx = -1, gy = -1;
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      int d = b.dist[y][x];
      if (d < 0 || danger[y][x] < 999) continue;
      int sc = -1000;
      bool bomb_spot = false;
      if (s->item[y][x] && s->block[y][x] == C_EMPTY) sc = 60 - d * 3;
      if (mine_out < p->bombs) {
        int v = bomb_value(s, me, x, y, level);
        if (v > 0 && v - d * 3 > sc) sc = v - d * 3 + (level == 2 ? 4 : 0), bomb_spot = true;
      }
      if (level > 0)
        for (int i = 0; i < MAXP; i++) {
          const player *q = &s->p[i];
          if (i == me || !q->on || !q->alive) continue;
          int ex = tile_of(q->x), ey = tile_of(q->y), md = IABS(ex - x) + IABS(ey - y);
          if (md <= 1 && level == 2) sc = CW_MAX(sc, 30 - d * 2);
        }
      if (d == 0 && sc < 0) sc = -50; /* staying put is an option, not a goal */
      sc = sc * 4 + (int)(rnd(m) % 3); /* a little randomness: rivals do not mirror each other */
      /* a bomb spot only counts when a bomb there would leave a way out */
      if (sc > best && bomb_spot && !(s->item[y][x] && s->block[y][x] == C_EMPTY) && !can_escape(s, me, x, y, danger, tpt, level == 0 ? 40 : 24))
        continue;
      if (sc > best) best = sc, gx = x, gy = y;
    }
  if (gx >= 0 && best > -150 && !(gx == mx && gy == my)) {
    m->mode = M_SEEK;
    m->goal_x = gx;
    m->goal_y = gy;
    out |= DIR_IN[b.first[gy][gx]];
  } else if (gx == mx && gy == my) {
    m->mode = M_IDLE;
    m->goal_x = gx;
    m->goal_y = gy;
    /* line up on the tile's centre */
    if (p->x < centre_of(mx) - 2) out |= IN_RIGHT;
    else if (p->x > centre_of(mx) + 2) out |= IN_LEFT;
    else if (p->y < centre_of(my) - 2) out |= IN_DOWN;
    else if (p->y > centre_of(my) + 2) out |= IN_UP;
  } else {
    /* wander: a random open neighbour that is safe */
    m->mode = M_WANDER;
    int d0 = (int)(rnd(m) % 4);
    for (int k = 0; k < 4; k++) {
      int d = (d0 + k) % 4, nx = mx + DX[d], ny = my + DY[d];
      if (b.dist[ny][nx] == 1) {
        out |= DIR_IN[d];
        break;
      }
    }
  }
  m->held = out;
  return out;
}
