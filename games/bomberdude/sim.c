/* bomberdude's rules: the grid simulation. Integer only (positions in SUB
 * units per tile, timers in ticks, a xorshift stream), one input byte per
 * player per tick: every host replays a round bit for bit.
 *
 *   moving    along the lanes between the pillars; a player slightly off a
 *             lane is pulled onto it, and one pressing into a wall near a
 *             corner slides round it (the forgiving feel the genre needs)
 *   bombs     dropped on the tile underfoot, BOMB_FUSE ticks; the dropper may
 *             walk off its own bomb, nobody may walk onto one; with kick a
 *             bomb walked into slides until it meets something
 *   blasts    a cross of `range` tiles, stopped by walls, crates (which burn,
 *             then may show a power-up), bombs (which go off a moment later:
 *             chains) and items (which burn); pierce passes through crates
 *   rounds    last one standing; after round_ticks, sudden death drops walls
 *             along a spiral from the rim inward; a draw when the last ones
 *             fall within DRAW_WINDOW ticks of each other
 *   twists    ice (slide until stopped), forge vents (erupt on a cycle),
 *             teleporter pairs, belts (carry players and bombs), sand (slow),
 *             candy (crates take two blasts), cannon rows (a warning, then a
 *             cannonball's fire sweeps the row) */
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

#define BOMB_FUSE 150
#define FIRE_TICKS 36
#define BURN_TICKS 30
#define CHAIN_DELAY 5
#define KICK_SPEED 36
#define DRAW_WINDOW 45
#define SD_STEP 10
#define VENT_PERIOD 330
#define VENT_WARN 100
#define CANNON_PERIOD 420
#define CANNON_WARN 100

static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, -1, 0, 1 };

uint32_t sim_rand(sim *s) {
  uint32_t x = s->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s->rng = x ? x : 0x9e3779b9u;
  return s->rng;
}
static int rnd_n(sim *s, int n) { return (int)(sim_rand(s) % (uint32_t)n); }

static void event(sim *s, int kind, int x, int y, int a, int b) {
  if (s->ev_n < (int)(sizeof s->ev / sizeof s->ev[0])) s->ev[s->ev_n++] = (struct sim_event){ kind, x, y, a, b };
}

static bool in_grid(int cx, int cy) { return cx >= 0 && cy >= 0 && cx < GW && cy < GH; }

int sim_bomb_at(const sim *s, int cx, int cy) {
  for (int i = 0; i < MAXB; i++)
    if (s->bombs[i].live && s->bombs[i].cx == cx && s->bombs[i].cy == cy) return i;
  return -1;
}

bool sim_tile_open(const sim *s, int cx, int cy) {
  return in_grid(cx, cy) && s->block[cy][cx] == C_EMPTY && sim_bomb_at(s, cx, cy) < 0;
}

bool sim_passable(const sim *s, int cx, int cy, int who) {
  if (!in_grid(cx, cy) || s->block[cy][cx] != C_EMPTY) return false;
  int b = sim_bomb_at(s, cx, cy);
  return b < 0 || (who >= 0 && (s->bombs[b].pass >> who & 1));
}

int sim_alive(const sim *s) {
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += s->p[i].on && s->p[i].alive;
  return n;
}

/* ── loading an arena ───────────────────────────────────────────────── */

static int pick_item(sim *s, int theme) {
  /* about a third of the crates hide something; candy crates are generous */
  int chance = theme == TH_CANDY ? 50 : 36;
  if (rnd_n(s, 100) >= chance) return PU_NONE;
  static const int W[PU_KINDS] = { 0, 28, 28, 16, 9, 5, 6, 8 };
  int r = rnd_n(s, 100);
  for (int k = 1; k < PU_KINDS; k++) {
    if (r < W[k]) return k;
    r -= W[k];
  }
  return PU_BOMB;
}

void sim_load(sim *s, const arena_info *a, const bool on[MAXP], uint32_t seed) {
  memset(s, 0, sizeof *s);
  s->theme = a ? a->theme : TH_MEADOW;
  s->rng = (seed ^ (uint32_t)(a ? a->seed : 1) * 2654435761u) | 1;
  s->winner = -2;
  s->round_ticks = (a && a->round_secs > 0 ? a->round_secs : 120) * 60;
  int spawn_x[MAXP] = { 1, GW - 2, 1, GW - 2 }, spawn_y[MAXP] = { 1, GH - 2, GH - 2, 1 };
  const char *L = a && a->layout ? a->layout : "";
  int crate_pct = a && a->crate_pct > 0 ? a->crate_pct : 70;
  uint8_t maybe[GH][GW];
  memset(maybe, 0, sizeof maybe);
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      char c = *L ? *L : (x == 0 || y == 0 || x == GW - 1 || y == GH - 1 || (x % 2 == 0 && y % 2 == 0)) ? '#' : '?';
      if (*L) L++;
      while (*L == '\n' || *L == ' ' || *L == '|') L++;
      uint8_t blk = C_EMPTY, fl = F_PLAIN;
      switch (c) {
      case '#': blk = C_WALL; break;
      case '+': blk = C_CRATE; break;
      case '?': maybe[y][x] = 1; break;
      case 'i': fl = F_ICE; break;
      case 'I': fl = F_ICE; maybe[y][x] = 1; break;
      case 'x': fl = F_VENT; break;
      case 't': fl = F_TELE; break;
      case 'E': fl = F_BELT_E; break;
      case 'W': fl = F_BELT_W; break;
      case 'N': fl = F_BELT_N; break;
      case 'S': fl = F_BELT_S; break;
      case '~': fl = F_SAND; break;
      case ':': fl = F_SAND; maybe[y][x] = 1; break;
      case 'c': blk = C_WALL; fl = F_CANNON_ROW; break; /* a rim wall holding a cannon: fires along its row */
      case 'k': blk = C_WALL; fl = F_CANNON_COL; break; /* ... along its column */
      case '1': case '2': case '3': case '4':
        spawn_x[c - '1'] = x;
        spawn_y[c - '1'] = y;
        break;
      default: break;
      }
      s->block[y][x] = blk;
      s->floor[y][x] = fl;
      if (fl == F_TELE && s->tele_n < MAX_TELE * 2) {
        s->tele_x[s->tele_n] = x;
        s->tele_y[s->tele_n] = y;
        s->tele_n++;
      }
      if (fl == F_VENT) s->vent[y][x] = (int16_t)((x * 37 + y * 53) % VENT_PERIOD);
    }
  /* spawns and their two neighbours stay open */
  for (int i = 0; i < MAXP; i++)
    for (int k = 0; k < 5; k++) {
      int x = spawn_x[i] + (k == 1) - (k == 2), y = spawn_y[i] + (k == 3) - (k == 4);
      if (in_grid(x, y)) maybe[y][x] = 0;
    }
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      if (maybe[y][x]) {
        int r = rnd_n(s, 100); /* one draw per statement: the evaluation order is the host's */
        if (r < crate_pct) s->block[y][x] = C_CRATE;
      }
      if (s->block[y][x] == C_CRATE) {
        s->crate_hp[y][x] = s->theme == TH_CANDY ? 2 : 1;
        s->item[y][x] = (uint8_t)pick_item(s, s->theme);
      }
    }
  /* the sudden-death spiral over every tile that is not a rim wall */
  {
    int x0 = 1, y0 = 1, x1 = GW - 2, y1 = GH - 2, n = 0;
    while (x0 <= x1 && y0 <= y1) {
      for (int x = x0; x <= x1; x++) s->sd_order_x[n] = x, s->sd_order_y[n++] = y0;
      for (int y = y0 + 1; y <= y1; y++) s->sd_order_x[n] = x1, s->sd_order_y[n++] = y;
      if (y1 > y0)
        for (int x = x1 - 1; x >= x0; x--) s->sd_order_x[n] = x, s->sd_order_y[n++] = y1;
      if (x1 > x0)
        for (int y = y1 - 1; y > y0; y--) s->sd_order_x[n] = x0, s->sd_order_y[n++] = y;
      x0++, y0++, x1--, y1--;
    }
    s->sd_count = n;
  }
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    p->on = on ? on[i] : i < 2;
    if (!p->on) continue;
    s->nplayers++;
    p->alive = true;
    p->x = centre_of(spawn_x[i]);
    p->y = centre_of(spawn_y[i]);
    p->dir = spawn_y[i] < GH / 2 ? D_S : D_N;
    p->bombs = 1;
    p->range = 2;
    p->speed = 14;
    p->slide_dir = D_NONE;
    p->ko_by = -1;
  }
  s->hash = 2166136261u;
  sim_refresh(s);
}

/* ── blasts ─────────────────────────────────────────────────────────── */

static void set_fire(sim *s, int x, int y, int owner, int shape) {
  s->fire[y][x] = FIRE_TICKS;
  s->fire_shape[y][x] |= (uint8_t)shape;
  s->fire_owner[y][x] = (int8_t)owner;
}

static void explode(sim *s, int bi) {
  bomb *b = &s->bombs[bi];
  int owner = b->owner, cx = b->cx, cy = b->cy, range = b->range;
  bool pierce = b->pierce;
  b->live = false;
  event(s, EV_BOOM, cx, cy, owner, range);
  set_fire(s, cx, cy, owner, 1);
  for (int d = 0; d < 4; d++) {
    int shape = d == D_E || d == D_W ? 2 : 4;
    for (int k = 1; k <= range; k++) {
      int x = cx + DX[d] * k, y = cy + DY[d] * k;
      if (!in_grid(x, y) || s->block[y][x] == C_WALL) break;
      bool last = k == range;
      if (s->block[y][x] == C_CRATE) {
        if (s->burn[y][x] == 0) {
          if (s->crate_hp[y][x] > 1) {
            s->crate_hp[y][x]--;
            event(s, EV_BUMP, x, y, 1, 0);
          } else {
            s->burn[y][x] = BURN_TICKS;
            event(s, EV_CRATE, x, y, s->item[y][x], owner);
          }
        }
        set_fire(s, x, y, owner, shape | 16);
        if (!pierce) break;
        continue;
      }
      int other = sim_bomb_at(s, x, y);
      if (other >= 0) { /* the next one goes off a moment later: a chain you can see */
        bomb *o = &s->bombs[other];
        if (o->fuse < 0 || o->fuse > CHAIN_DELAY) {
          o->fuse = CHAIN_DELAY;
          s->chains++;
        }
        o->slide_dir = D_NONE;
        set_fire(s, x, y, owner, shape);
        break;
      }
      if (s->item[y][x] && s->item_grace[y][x] == 0) {
        event(s, EV_ITEM_BURN, x, y, s->item[y][x], 0);
        s->item[y][x] = PU_NONE;
        set_fire(s, x, y, owner, shape | 16);
        break;
      }
      set_fire(s, x, y, owner, shape | (last ? 16 : 0));
    }
  }
}

/* ── players ────────────────────────────────────────────────────────── */

static int player_on_tile(const sim *s, int cx, int cy, int except) {
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    if (i == except || !p->on || !p->alive) continue;
    if (tile_of(p->x) == cx && tile_of(p->y) == cy) return i;
  }
  return -1;
}

static bool try_kick(sim *s, int who, int bx, int by, int d) {
  player *p = &s->p[who];
  int bi = sim_bomb_at(s, bx, by);
  if (!p->kick || bi < 0 || s->bombs[bi].slide_dir != D_NONE) return false;
  int nx = bx + DX[d], ny = by + DY[d];
  if (!sim_tile_open(s, nx, ny) || player_on_tile(s, nx, ny, -1) >= 0) return false;
  s->bombs[bi].slide_dir = d;
  event(s, EV_KICK, bx, by, who, d);
  return true;
}

/* one step of `speed` toward d; returns the distance moved. The classic
 * forgiving feel: moving along a free lane pulls the player onto its centre
 * line as it goes; pressing into a wall while leaning toward an opening
 * slides round the corner into it; pressing into a wall otherwise walks up
 * to it (and kicks a bomb there with the kick power-up). */
static int try_move(sim *s, int who, int d, int speed) {
  player *p = &s->p[who];
  bool horiz = d == D_E || d == D_W;
  int *along = horiz ? &p->x : &p->y, *perp = horiz ? &p->y : &p->x;
  int sign = d == D_E || d == D_S ? 1 : -1;
  int cx = tile_of(p->x), cy = tile_of(p->y);
  int lane = horiz ? cy : cx, cell = horiz ? cx : cy;
  int off = *perp - centre_of(lane), aoff = IABS(off);
  int ax = cx + DX[d], ay = cy + DY[d];
  int moved = 0;
  if (sim_passable(s, ax, ay, who)) {
    int fix = CW_MIN(speed, aoff); /* onto the lane while moving along it */
    *perp += off > 0 ? -fix : fix;
    speed -= fix;
    *along += sign * speed;
    return fix + speed;
  }
  /* blocked ahead: round the corner on the side we lean toward */
  if (aoff > 6) {
    int side = off > 0 ? 1 : -1;
    int lx = horiz ? cx : cx + side, ly = horiz ? cy + side : cy;
    if (sim_passable(s, lx, ly, who) && sim_passable(s, lx + DX[d], ly + DY[d], who)) {
      int step = CW_MIN(speed, SUB - aoff + 1);
      *perp += side * step;
      return step;
    }
  }
  /* walk up to the wall (no sideways nudge here: with two directions held,
   * the other one must win, not a wobble on the spot) */
  int room = CW_MAX(0, (centre_of(cell) - *along) * sign);
  int step = CW_MIN(speed, room);
  *along += sign * step;
  moved += step;
  if (room == 0) try_kick(s, who, ax, ay, d);
  return moved;
}

static int dir_of_input(uint8_t in, int prefer_not) {
  int h = (in & IN_RIGHT) && !(in & IN_LEFT) ? D_E : (in & IN_LEFT) && !(in & IN_RIGHT) ? D_W : D_NONE;
  int v = (in & IN_DOWN) && !(in & IN_UP) ? D_S : (in & IN_UP) && !(in & IN_DOWN) ? D_N : D_NONE;
  if (h != D_NONE && v != D_NONE) return (prefer_not == D_E || prefer_not == D_W) ? v : h;
  return h != D_NONE ? h : v;
}

static bool drop_bomb(sim *s, int who) {
  player *p = &s->p[who];
  int cx = tile_of(p->x), cy = tile_of(p->y);
  if (s->block[cy][cx] != C_EMPTY || sim_bomb_at(s, cx, cy) >= 0) return false;
  int mine = 0, slot = -1;
  for (int i = 0; i < MAXB; i++) {
    if (s->bombs[i].live && s->bombs[i].owner == who) mine++;
    if (!s->bombs[i].live && slot < 0) slot = i;
  }
  if (mine >= p->bombs || slot < 0) return false;
  bomb *b = &s->bombs[slot];
  *b = (bomb){ .live = true, .owner = who, .cx = cx, .cy = cy, .x = centre_of(cx), .y = centre_of(cy), .fuse = p->remote ? -1 : BOMB_FUSE,
               .range = p->range, .pierce = p->pierce, .remote = p->remote, .slide_dir = D_NONE, .id = ++s->bomb_ids };
  for (int i = 0; i < MAXP; i++) {
    const player *q = &s->p[i];
    if (q->on && q->alive && IABS(q->x - b->x) < SUB && IABS(q->y - b->y) < SUB) b->pass |= (uint8_t)(1u << i);
  }
  p->placed++;
  event(s, EV_DROP, cx, cy, who, b->fuse);
  return true;
}

static void act(sim *s, int who) {
  player *p = &s->p[who];
  /* remote: the oldest of mine goes off; otherwise stop my kicked bombs */
  int oldest = -1;
  for (int i = 0; i < MAXB; i++) {
    bomb *b = &s->bombs[i];
    if (!b->live || b->owner != who) continue;
    if (p->remote && b->remote && b->fuse < 0 && (oldest < 0 || b->id < s->bombs[oldest].id)) oldest = i;
    if (!p->remote && b->slide_dir != D_NONE) {
      b->slide_dir = D_NONE;
      b->x = centre_of(b->cx);
      b->y = centre_of(b->cy);
    }
  }
  if (oldest >= 0) s->bombs[oldest].fuse = 1;
}

static void pick_up(sim *s, int who) {
  player *p = &s->p[who];
  int cx = tile_of(p->x), cy = tile_of(p->y);
  int it = s->item[cy][cx];
  if (!it || s->block[cy][cx] != C_EMPTY) return;
  s->item[cy][cx] = PU_NONE;
  p->picked++;
  switch (it) {
  case PU_BOMB: if (p->bombs < 8) p->bombs++; break;
  case PU_FIRE: if (p->range < 9) p->range++; break;
  case PU_SPEED: if (p->speed < 26) p->speed += 3; break;
  case PU_KICK: p->kick = true; break;
  case PU_REMOTE: p->remote = true; break;
  case PU_PIERCE: p->pierce = true; break;
  case PU_BUBBLE: if (p->bubble < 1) p->bubble = 1; break;
  }
  event(s, EV_PICK, cx, cy, who, it);
}

static void move_player(sim *s, int who, uint8_t in) {
  player *p = &s->p[who];
  int cx = tile_of(p->x), cy = tile_of(p->y);
  int fl = s->floor[cy][cx];
  int speed = p->speed;
  if (fl == F_SAND) speed = speed * 3 / 5;
  if (fl == F_ICE) speed = speed * 6 / 5;
  /* two directions held: the one pressed last first (a stick swept across a corner keeps its old axis) */
  int d = dir_of_input(in, p->pref_vert ? D_E : D_N);
  int moved = 0;
  if (d != D_NONE) {
    moved = try_move(s, who, d, speed);
    if (!moved) { /* pressing two ways at a wall: take the other one */
      int alt = dir_of_input(in, d == D_E || d == D_W ? D_E : D_N); /* the other axis */
      if (alt != d && alt != D_NONE) {
        moved = try_move(s, who, alt, speed);
        if (moved) d = alt;
      }
    }
    p->dir = d;
    p->slide_dir = moved ? d : D_NONE;
  } else if (fl == F_ICE && p->slide_dir != D_NONE) {
    moved = try_move(s, who, p->slide_dir, speed);
    if (moved) s->slides++;
    if (!moved) p->slide_dir = D_NONE;
  } else p->slide_dir = D_NONE;
  /* belts carry whoever stands on them */
  int bx = tile_of(p->x), by = tile_of(p->y), bf = s->floor[by][bx];
  if (bf >= F_BELT_E && bf <= F_BELT_S && try_move(s, who, bf - F_BELT_E, 5)) s->belt_moves++;
  p->moving = moved ? p->moving + 1 : 0;
  if (moved && p->moving % 16 == 1) event(s, EV_STEP, tile_of(p->x), tile_of(p->y), who, fl);
  /* teleporters: arrive near the pad's centre */
  if (p->tele_cool > 0) p->tele_cool--;
  int tx = tile_of(p->x), ty = tile_of(p->y);
  if (s->floor[ty][tx] == F_TELE && p->tele_cool == 0 && IABS(p->x - centre_of(tx)) < 40 && IABS(p->y - centre_of(ty)) < 40) {
    for (int k = 0; k < s->tele_n; k++)
      if (s->tele_x[k] == tx && s->tele_y[k] == ty) {
        int o = k ^ 1;
        if (o >= s->tele_n) break;
        int ox = s->tele_x[o], oy = s->tele_y[o];
        if (s->block[oy][ox] != C_EMPTY) break;
        event(s, EV_TELE, tx, ty, who, o);
        p->x = centre_of(ox);
        p->y = centre_of(oy);
        p->tele_cool = 70;
        p->slide_dir = D_NONE;
        event(s, EV_TELE, ox, oy, who, -1);
        break;
      }
  }
}

static void knock_out(sim *s, int who, int by) {
  player *p = &s->p[who];
  if (!p->alive) return;
  if (p->invuln > 0) return;
  if (p->bubble > 0) {
    p->bubble--;
    p->invuln = 75;
    event(s, EV_BUBBLE, tile_of(p->x), tile_of(p->y), who, by);
    return;
  }
  p->alive = false;
  p->ko_tick = s->tick;
  p->ko_by = by;
  if (by >= 0 && by != who) s->p[by].kos++;
  event(s, EV_KO, tile_of(p->x), tile_of(p->y), who, by);
  /* remote bombs left behind go off on their own */
  for (int i = 0; i < MAXB; i++)
    if (s->bombs[i].live && s->bombs[i].owner == who && s->bombs[i].fuse < 0) s->bombs[i].fuse = 60 + i * 4;
}

/* ── hazards ────────────────────────────────────────────────────────── */

static void hazards(sim *s) {
  /* forge vents: glow, then a burst of fire on the vent and its four neighbours */
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      if (s->floor[y][x] != F_VENT) continue;
      int ph = (s->vent[y][x] + s->tick) % VENT_PERIOD;
      if (ph == VENT_PERIOD - VENT_WARN) event(s, EV_VENT_WARN, x, y, 0, 0);
      if (ph == 0) {
        event(s, EV_VENT, x, y, 0, 0);
        set_fire(s, x, y, -1, 1);
        for (int d = 0; d < 4; d++) {
          int nx = x + DX[d], ny = y + DY[d];
          if (in_grid(nx, ny) && s->block[ny][nx] == C_EMPTY) set_fire(s, nx, ny, -1, (d == D_E || d == D_W ? 2 : 4) | 16);
        }
      }
    }
  /* pirate cannons: one rim cannon at a time warns, then its shot burns along the line */
  bool cannons = false;
  for (int y = 0; y < GH && !cannons; y++)
    for (int x = 0; x < GW; x++)
      if (s->floor[y][x] == F_CANNON_ROW || s->floor[y][x] == F_CANNON_COL) {
        cannons = true;
        break;
      }
  if (cannons && s->over_tick == 0) {
    int ph = s->tick % CANNON_PERIOD;
    if (ph == CANNON_PERIOD - CANNON_WARN) {
      int n = 0, cx[GW * 2 + GH * 2], cy[GW * 2 + GH * 2];
      for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
          if (s->floor[y][x] == F_CANNON_ROW || s->floor[y][x] == F_CANNON_COL) cx[n] = x, cy[n++] = y;
      int k = rnd_n(s, n);
      s->cannon_t = (cy[k] << 8) | cx[k];
      event(s, EV_CANNON_WARN, cx[k], cy[k], 0, 0);
    }
    if (ph < 60 && s->tick >= CANNON_PERIOD) {
      int x0 = s->cannon_t & 255, y0 = s->cannon_t >> 8;
      bool row = s->floor[y0][x0] == F_CANNON_ROW;
      int d = row ? (x0 == 0 ? D_E : D_W) : (y0 == 0 ? D_S : D_N);
      if (ph == 0) event(s, EV_CANNON, x0, y0, d, 0);
      if (ph % 4 == 0) {
        int k = ph / 4 + 1, x = x0 + DX[d] * k, y = y0 + DY[d] * k;
        if (in_grid(x, y) && s->block[y][x] != C_WALL) {
          if (s->block[y][x] == C_CRATE && s->burn[y][x] == 0) {
            s->burn[y][x] = BURN_TICKS;
            event(s, EV_CRATE, x, y, s->item[y][x], -1);
          }
          int bi = sim_bomb_at(s, x, y);
          if (bi >= 0 && (s->bombs[bi].fuse < 0 || s->bombs[bi].fuse > CHAIN_DELAY)) s->bombs[bi].fuse = CHAIN_DELAY;
          set_fire(s, x, y, -1, (row ? 2 : 4) | 16);
        }
      }
    }
  }
  /* the desert's sandstorms come and go (the look; and the CPUs see less far) */
  if (s->theme == TH_DESERT) {
    if (s->storm > 0) s->storm--;
    else if (s->tick % 900 == 600) s->storm = 360;
  }
  /* sudden death */
  if (s->tick >= s->round_ticks && s->over_tick == 0 && s->sd_index < s->sd_count && (s->tick - s->round_ticks) % SD_STEP == 0) {
    int x, y;
    do {
      x = s->sd_order_x[s->sd_index];
      y = s->sd_order_y[s->sd_index];
      s->sd_index++;
    } while (s->block[y][x] == C_WALL && s->sd_index < s->sd_count);
    if (s->block[y][x] != C_WALL) {
      s->block[y][x] = C_WALL;
      s->burn[y][x] = 0;
      s->item[y][x] = PU_NONE;
      s->floor[y][x] = F_PLAIN;
      int bi = sim_bomb_at(s, x, y);
      if (bi >= 0) s->bombs[bi].live = false;
      s->sd_drop_x = x;
      s->sd_drop_y = y;
      s->sd_drop_t = s->tick;
      event(s, EV_SD_WALL, x, y, s->sd_index, 0);
      for (int i = 0; i < MAXP; i++) {
        player *p = &s->p[i];
        if (p->on && p->alive && tile_of(p->x) == x && tile_of(p->y) == y) {
          p->invuln = p->bubble = 0;
          knock_out(s, i, -1);
        }
      }
    }
  }
}

/* ── the tick ───────────────────────────────────────────────────────── */

void sim_step(sim *s, const uint8_t in[MAXP]) {
  s->tick++;
  s->ev_n = 0;
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      if (s->fire[y][x] > 0 && --s->fire[y][x] == 0) {
        s->fire_shape[y][x] = 0;
        s->fire_owner[y][x] = -1;
      }
      if (s->item_grace[y][x] > 0) s->item_grace[y][x]--;
      if (s->burn[y][x] > 0 && --s->burn[y][x] == 0) {
        s->block[y][x] = C_EMPTY;
        if (s->item[y][x]) s->item_grace[y][x] = FIRE_TICKS; /* revealed: this blast spares it */
      }
    }
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->on || !p->alive) continue;
    uint8_t now = in ? in[i] : 0, pressed = now & ~p->last_in;
    p->last_in = now;
    if (p->invuln > 0) p->invuln--;
    if (s->over_tick == 0) {
      /* a press is held for a few ticks: one a moment early (still on the last tile's bomb) still drops */
      if (pressed & IN_BOMB) p->bomb_buf = 8;
      if (p->bomb_buf > 0) p->bomb_buf = drop_bomb(s, i) ? 0 : p->bomb_buf - 1;
      if (pressed & IN_ACT) act(s, i);
    }
    bool pv = pressed & (IN_UP | IN_DOWN), ph = pressed & (IN_LEFT | IN_RIGHT);
    if (pv != ph) p->pref_vert = pv;
    move_player(s, i, s->over_tick ? 0 : now);
    pick_up(s, i);
  }
  /* bombs: who may still walk off them, sliding, belts, fuses */
  for (int i = 0; i < MAXB; i++) {
    bomb *b = &s->bombs[i];
    if (!b->live) continue;
    b->age++;
    for (int k = 0; k < MAXP; k++) {
      const player *q = &s->p[k];
      if ((b->pass >> k & 1) && (!q->alive || IABS(q->x - b->x) >= SUB - 24 || IABS(q->y - b->y) >= SUB - 24))
        b->pass &= (uint8_t)~(1u << k);
    }
    int bf = s->floor[b->cy][b->cx];
    bool on_belt = bf >= F_BELT_E && bf <= F_BELT_S;
    if (b->slide_dir == D_NONE && on_belt) {
      int d = bf - F_BELT_E;
      if (sim_tile_open(s, b->cx + DX[d], b->cy + DY[d]) && player_on_tile(s, b->cx + DX[d], b->cy + DY[d], -1) < 0) {
        b->slide_dir = d;
        b->belt = true;
      }
    }
    if (b->slide_dir != D_NONE) {
      int d = b->slide_dir, sign = d == D_E || d == D_S ? 1 : -1;
      int speed = b->belt ? 7 : bf == F_ICE ? KICK_SPEED + 8 : KICK_SPEED;
      int nx = b->cx + DX[d], ny = b->cy + DY[d];
      bool blocked = !sim_tile_open(s, nx, ny) || player_on_tile(s, nx, ny, -1) >= 0;
      /* a belt's bomb rides to the next tile's centre, where the belt under it decides again */
      bool stop_here = blocked || (b->belt && !on_belt);
      int *along = d == D_E || d == D_W ? &b->x : &b->y;
      int centre = centre_of(d == D_E || d == D_W ? b->cx : b->cy), rel = (*along - centre) * sign;
      if (stop_here && rel + speed >= 0 && rel <= 0) {
        *along = centre;
        b->slide_dir = D_NONE;
        if (!b->belt) event(s, EV_BUMP, b->cx, b->cy, 0, 0);
        b->belt = false;
      } else if (stop_here && rel > 0) {
        *along -= sign * CW_MIN(speed, rel); /* past the centre already: back onto it */
      } else {
        *along += sign * speed;
        b->cx = tile_of(b->x);
        b->cy = tile_of(b->y);
      }
    }
    if (s->fire[b->cy][b->cx] > 0 && (b->fuse < 0 || b->fuse > CHAIN_DELAY)) b->fuse = CHAIN_DELAY;
    if (b->fuse > 0) b->fuse--;
  }
  for (int i = 0; i < MAXB; i++)
    if (s->bombs[i].live && s->bombs[i].fuse == 0) explode(s, i);
  hazards(s);
  for (int i = 0; i < MAXP; i++) {
    player *p = &s->p[i];
    if (!p->on || !p->alive) continue;
    int cx = tile_of(p->x), cy = tile_of(p->y);
    if (s->fire[cy][cx] > 0) knock_out(s, i, s->fire_owner[cy][cx]);
  }
  /* the round: decided when one (or nobody) is left; a near-simultaneous end is a draw */
  int alive = sim_alive(s);
  if (s->over_tick == 0 && alive <= (s->nplayers > 1 ? 1 : 0)) s->over_tick = s->tick;
  if (s->over_tick && s->winner == -2 && s->tick - s->over_tick >= DRAW_WINDOW) {
    s->winner = -1;
    for (int i = 0; i < MAXP; i++)
      if (s->p[i].on && s->p[i].alive) s->winner = i;
  }
  /* the fold every gate compares */
  uint32_t h = s->hash;
  const uint8_t *bytes[] = { &s->block[0][0], &s->item[0][0] };
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < GW * GH; i++) h = (h ^ bytes[k][i]) * 16777619u;
  for (int i = 0; i < MAXP; i++) {
    const player *p = &s->p[i];
    int32_t v[] = { p->alive, p->x, p->y, p->bombs, p->range, p->speed, p->bubble };
    for (int k = 0; k < 7; k++) h = (h ^ (uint32_t)v[k]) * 16777619u;
  }
  for (int i = 0; i < MAXB; i++)
    if (s->bombs[i].live) h = (h ^ (uint32_t)(s->bombs[i].x * 31 + s->bombs[i].y * 17 + s->bombs[i].fuse)) * 16777619u;
  s->hash = h;
  sim_refresh(s);
}

/* ── danger map (the CPUs) ──────────────────────────────────────────── */

/* ticks until each tile burns, from the bombs as they stand (chains
 * included), the fire already burning (0) and the warnings of vents and
 * cannons; 999 = safe */
static void danger_map(const sim *s, int16_t out[GH][GW]) {
  int t[MAXB];
  for (int i = 0; i < MAXB; i++) t[i] = !s->bombs[i].live ? -1 : s->bombs[i].fuse < 0 ? 240 : s->bombs[i].fuse;
  /* chains: a bomb in another's blast goes when that one does, if sooner */
  for (int pass = 0; pass < 6; pass++) {
    bool changed = false;
    for (int i = 0; i < MAXB; i++) {
      if (t[i] < 0) continue;
      const bomb *b = &s->bombs[i];
      for (int d = 0; d < 4; d++)
        for (int k = 1; k <= b->range; k++) {
          int x = b->cx + DX[d] * k, y = b->cy + DY[d] * k;
          if (!in_grid(x, y) || s->block[y][x] != C_EMPTY) break;
          int o = s->bomb_at[y][x];
          if (o >= 0) {
            if (t[o] > t[i] + CHAIN_DELAY) {
              t[o] = t[i] + CHAIN_DELAY;
              changed = true;
            }
            break;
          }
        }
    }
    if (!changed) break;
  }
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) out[y][x] = s->fire[y][x] > 0 ? 0 : 999;
  for (int i = 0; i < MAXB; i++) {
    if (t[i] < 0) continue;
    const bomb *b = &s->bombs[i];
    int16_t tt = (int16_t)t[i];
    if (out[b->cy][b->cx] > tt) out[b->cy][b->cx] = tt;
    for (int d = 0; d < 4; d++)
      for (int k = 1; k <= b->range; k++) {
        int x = b->cx + DX[d] * k, y = b->cy + DY[d] * k;
        if (!in_grid(x, y) || s->block[y][x] == C_WALL) break;
        if (out[y][x] > tt) out[y][x] = tt;
        if (s->block[y][x] == C_CRATE && !b->pierce) break;
        if (s->bomb_at[y][x] >= 0) break;
      }
  }
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++)
      if (s->floor[y][x] == F_VENT) {
        int ph = (s->vent[y][x] + s->tick) % VENT_PERIOD, until = (VENT_PERIOD - ph) % VENT_PERIOD;
        if (until < VENT_WARN + 20) {
          if (out[y][x] > until) out[y][x] = (int16_t)until;
          for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (in_grid(nx, ny) && out[ny][nx] > until) out[ny][nx] = (int16_t)until;
          }
        }
      }
  int ph = s->tick % CANNON_PERIOD;
  if (s->cannon_t && (ph >= CANNON_PERIOD - CANNON_WARN || ph < 60)) {
    int x0 = s->cannon_t & 255, y0 = s->cannon_t >> 8;
    bool row = s->floor[y0][x0] == F_CANNON_ROW;
    int until = ph >= CANNON_PERIOD - CANNON_WARN ? CANNON_PERIOD - ph : 0;
    for (int k = 0; k < (row ? GW : GH); k++) {
      int x = row ? k : x0, y = row ? y0 : k;
      if (out[y][x] > until) out[y][x] = (int16_t)until;
    }
  }
  if (s->tick + 60 >= s->round_ticks && s->over_tick == 0) /* sudden death: the next tiles of the spiral */
    for (int k = 0; k < 8 && s->sd_index + k < s->sd_count; k++) {
      int x = s->sd_order_x[s->sd_index + k], y = s->sd_order_y[s->sd_index + k];
      int until = CW_MAX(0, s->round_ticks - s->tick) + k * SD_STEP;
      if (out[y][x] > until) out[y][x] = (int16_t)until;
    }
}

void sim_refresh(sim *s) {
  memset(s->bomb_at, -1, sizeof s->bomb_at);
  for (int i = MAXB - 1; i >= 0; i--) /* down, so the lowest index wins as in sim_bomb_at */
    if (s->bombs[i].live && in_grid(s->bombs[i].cx, s->bombs[i].cy)) s->bomb_at[s->bombs[i].cy][s->bombs[i].cx] = (int8_t)i;
  danger_map(s, s->danger);
}

/* dev aid (BOMBERDUDE_BOOM): a bomb of `range` at (cx, cy) that goes off next tick */
void sim_force_bomb(sim *s, int cx, int cy, int range) {
  if (!in_grid(cx, cy) || s->block[cy][cx] == C_WALL) return;
  s->block[cy][cx] = C_EMPTY;
  for (int i = 0; i < MAXB; i++)
    if (!s->bombs[i].live) {
      s->bombs[i] = (bomb){ .live = true, .owner = 0, .cx = cx, .cy = cy, .x = centre_of(cx), .y = centre_of(cy), .fuse = 2, .range = range,
                            .slide_dir = D_NONE, .id = ++s->bomb_ids };
      sim_refresh(s);
      return;
    }
}
