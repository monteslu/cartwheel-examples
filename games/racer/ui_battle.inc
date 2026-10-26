/* ui_battle.inc: battle mode's screens and HUD (included by ui.c after the other parts; never built on its own).
 *
 *   the arena select   two arenas drawn from above (the map is the arena's own shapes), the karts in the round (2..8,
 *                      the humans first), the round's length and the CPU drivers' skill; START BATTLE
 *   the HUD            per view: balloons left and points (top left), the item (top middle), the clock and the karts
 *                      still in (top right), the arena map with everyone on it, a points table on a big view, "POP!"
 *                      stamps, the elimination feed, and once your kart is out who you are watching
 *   the results        the standings (last one standing, then points, balloons, who lasted) and AGAIN / NEXT ARENA /
 *                      MENU */

static const int BATTLE_SECS[4] = { 60, 120, 180, 300 };
enum { BO_KARTS, BO_TIME, BO_CPU, BO_KINDS };
static const char *const BO_LABEL[BO_KINDS] = { "KARTS", "TIME", "CPU DRIVERS" };

static int battle_karts_min(void) { return U.setup.humans > 2 ? U.setup.humans : 2; }

/* ── the arena from above ─────────────────────────────────────────────── */

typedef struct afit {
  float cx, cy, sc;
} afit;

/* the arena's own frame (x across, z along) to a rect: z up the screen, x to the left (the view from above) */
static afit arena_fit(const arena_def *d, cw_rect r, float pad) {
  float w = d->half_x * 2 + 6, h = d->half_z * 2 + 6;
  afit f = { r.x + r.w * 0.5f, r.y + r.h * 0.5f, fminf((r.w - 2 * pad) / w, (r.h - 2 * pad) / h) };
  return f;
}
static cw_vec2 arena_pt(const afit *f, float x, float z) { return cw_v2(f->cx - x * f->sc, f->cy - z * f->sc); }

static void arena_prim_corners(const afit *f, const arena_prim *p, cw_vec2 q[4]) {
  float yaw = p->yaw * (CW_PI / 180.0f), cs = cw_cos(yaw), sn = cw_sin(yaw);
  static const float SX[4] = { -1, 1, 1, -1 }, SZ[4] = { -1, -1, 1, 1 };
  for (int k = 0; k < 4; k++) {
    float lx = SX[k] * p->w * 0.5f, lz = SZ[k] * p->d * 0.5f;
    q[k] = arena_pt(f, p->x + lx * cs + lz * sn, p->z - lx * sn + lz * cs);
  }
}

/* the arena drawn flat: floor, decks lighter, ramps with their rise marked, cover dark, the walls round it; S (may be
 * NULL) adds the boxes, the hazards and the karts; me is the kart to ring */
static void arena_draw_map(const arena_def *d, cw_rect r, float pad, float s, const sim *S, int me, bool detail) {
  afit f = arena_fit(d, r, pad);
  cw_vec2 a = arena_pt(&f, d->half_x + 1.5f, d->half_z + 1.5f), b = arena_pt(&f, -d->half_x - 1.5f, -d->half_z - 1.5f);
  cw_rect fl = cw_rect_make(fminf(a.x, b.x), fminf(a.y, b.y), fabsf(b.x - a.x), fabsf(b.y - a.y));
  cw_r2d_rect(cw_rect_make(fl.x - 3 * s, fl.y - 3 * s, fl.w + 6 * s, fl.h + 6 * s), rgba(0.02f, 0.02f, 0.03f, 0.95f));
  cw_r2d_rect(fl, rgba(0.36f, 0.33f, 0.35f, 1));
  float lv_max = 0.01f;
  for (int i = 0; i < d->nprims; i++)
    if (d->prims[i].look == AL_DECK) lv_max = fmaxf(lv_max, d->prims[i].y + d->prims[i].h);
  /* the floors first (decks and ramps), then what stands on them */
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < d->nprims; i++) {
      const arena_prim *p = &d->prims[i];
      bool floorish = p->look == AL_DECK || p->look == AL_RAMP;
      if (p->ghost || floorish != (pass == 0)) continue;
      if (p->look != AL_DECK && p->look != AL_RAMP && p->look != AL_COVER && p->look != AL_FEATURE) continue;
      cw_vec2 q[4];
      arena_prim_corners(&f, p, q);
      if (p->kind == AP_POST) {
        cw_vec2 c = arena_pt(&f, p->x, p->z);
        cw_r2d_circle(c, p->w * 0.5f * f.sc + 0.5f, 14, p->look == AL_FEATURE ? rgba(0.95f, 0.55f, 0.15f, 1) : rgba(0.09f, 0.08f, 0.1f, 1));
        continue;
      }
      if (p->kind == AP_MOUND) {
        /* the slopes in the ramps' grey, the flat top as a deck inside them */
        cw_r2d_polygon(q, 4, rgba(0.62f, 0.6f, 0.58f, 1));
        arena_prim top = *p;
        top.w = fmaxf(p->w - 2 * p->h * MOUND_RUN, 0.5f), top.d = fmaxf(p->d - 2 * p->h * MOUND_RUN, 0.5f);
        arena_prim_corners(&f, &top, q);
        cw_r2d_polygon(q, 4, rgba(0.78f, 0.74f, 0.66f, 1));
        cw_r2d_polygon_lines(q, 4, 1.5f * s + 0.4f, rgba(0.95f, 0.8f, 0.2f, 0.9f));
      } else if (p->look == AL_DECK) {
        float h = clamp01((p->y + p->h) / lv_max);
        cw_r2d_polygon(q, 4, mix(rgba(0.5f, 0.47f, 0.48f, 1), rgba(0.78f, 0.74f, 0.66f, 1), h));
        cw_r2d_polygon_lines(q, 4, 1.5f * s + 0.4f, rgba(0.95f, 0.8f, 0.2f, 0.9f));
      } else if (p->look == AL_RAMP) {
        cw_r2d_polygon(q, 4, rgba(0.62f, 0.6f, 0.58f, 1));
        /* the rise: a chevron pointing up the ramp */
        cw_vec2 lo = cw_v2((q[0].x + q[1].x) * 0.5f, (q[0].y + q[1].y) * 0.5f), hi = cw_v2((q[2].x + q[3].x) * 0.5f, (q[2].y + q[3].y) * 0.5f);
        cw_vec2 mid = cw_v2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
        cw_vec2 dir = cw_v2_normalize(cw_v2_sub(hi, lo)), n = cw_v2(-dir.y, dir.x);
        float k = fminf(cw_v2_length(cw_v2_sub(hi, lo)), cw_v2_length(cw_v2_sub(q[1], q[0]))) * 0.28f;
        cw_r2d_line(cw_v2(mid.x - dir.x * k + n.x * k, mid.y - dir.y * k + n.y * k), cw_v2(mid.x + dir.x * k * 0.6f, mid.y + dir.y * k * 0.6f), 1.6f * s + 0.4f, INK);
        cw_r2d_line(cw_v2(mid.x - dir.x * k - n.x * k, mid.y - dir.y * k - n.y * k), cw_v2(mid.x + dir.x * k * 0.6f, mid.y + dir.y * k * 0.6f), 1.6f * s + 0.4f, INK);
      } else {
        cw_color c = p->col ? rgba((float)((p->col >> 16) & 255) / 255.0f, (float)((p->col >> 8) & 255) / 255.0f, (float)(p->col & 255) / 255.0f, 1) : rgba(0.12f, 0.11f, 0.13f, 1);
        if (p->look == AL_FEATURE) c = rgba(0.95f, 0.55f, 0.15f, 1);
        cw_r2d_polygon(q, 4, shade(c, 0.8f));
        cw_r2d_polygon_lines(q, 4, 1.2f * s + 0.3f, rgba(0.02f, 0.02f, 0.03f, 0.9f));
      }
    }
  cw_r2d_rect_lines(fl, 3 * s + 0.5f, rgba(0.62f, 0.33f, 0.2f, 1));
  if (!S) {
    /* the boxes where they stand */
    for (int i = 0; i < d->nboxes && detail; i++) {
      cw_vec2 c = arena_pt(&f, d->boxes[i][0], d->boxes[i][1]);
      cw_r2d_circle(c, 2.6f * s + 0.6f, 8, MUSTARD);
    }
    return;
  }
  float t = (float)S->tick / 60.0f;
  for (int i = 0; i < S->nbox; i++) {
    if (S->box[i].cool > 0) continue;
    cw_vec2 c = arena_pt(&f, S->box[i].p.x, S->box[i].p.z - ARENA_CZ);
    cw_r2d_circle(c, 1.9f * s + 0.5f, 6, alpha(MUSTARD, 0.9f));
  }
  for (int h = 0; h < MAXHZ; h++) {
    const hazard *z = &S->hz[h];
    if (z->kind != HZ_MINE && z->kind != HZ_MISSILE && z->kind != HZ_HOMING) continue;
    cw_vec2 c = arena_pt(&f, z->p.x, z->p.z - ARENA_CZ);
    cw_r2d_circle(c, 1.8f * s + 0.5f, 6, z->kind == HZ_MINE ? RED : rgba(1, 0.6f, 0.2f, 1));
  }
  for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i < MAXK; i++) {
      const kart *k = &S->k[i];
      if (!k->on || k->parked) continue;
      bool mine = i == me;
      int want = mine ? 2 : k->human ? 1 : 0;
      if (want != pass) continue;
      cw_vec2 p = arena_pt(&f, k->p.x, k->p.z - ARENA_CZ);
      float rad = (mine ? 5.2f : k->human ? 4.6f : 3.6f) * s + 0.8f;
      /* the heading: a little wedge ahead of the dot */
      t3_vec3 fw = kart_fwd(k);
      cw_vec2 hd = cw_v2_normalize(cw_v2(-fw.x, -fw.z));
      cw_vec2 tip = cw_v2(p.x + hd.x * rad * 2.0f, p.y + hd.y * rad * 2.0f), side = cw_v2(-hd.y * rad * 0.8f, hd.x * rad * 0.8f);
      cw_vec2 tri3[3] = { tip, cw_v2(p.x + side.x, p.y + side.y), cw_v2(p.x - side.x, p.y - side.y) };
      cw_r2d_polygon(tri3, 3, alpha(INK, k->out ? 0.4f : 0.9f));
      if (mine) cw_r2d_circle_lines(p, rad + 3 * s + 1.5f * cw_sin(t * 6) * s, 2 * s + 0.5f, 16, CREAM);
      cw_r2d_circle(p, rad + 1.6f * s, 14, INK);
      cw_r2d_circle(p, rad, 14, alpha(kart_col(k->colour), k->out ? 0.45f : 1));
      if (k->human) cw_r2d_circle_lines(p, rad, 1.6f * s + 0.4f, 14, CREAM);
    }
}

/* a balloon for the HUD: a round body, a shine, the knot and a bit of string; burst 0..1 swells it and fades it out */
static void balloon_icon(cw_vec2 c, float r, cw_color col, float a, float burst, bool empty) {
  if (empty) {
    cw_r2d_circle_lines(c, r, fmaxf(r * 0.12f, 1), 16, alpha(rgba(0.55f, 0.5f, 0.48f, 1), 0.6f * a));
    return;
  }
  float k = 1 + burst * 0.7f, fa = a * (1 - burst);
  cw_r2d_line(cw_v2(c.x, c.y + r * 1.05f), cw_v2(c.x + r * 0.15f, c.y + r * 1.9f), fmaxf(r * 0.08f, 1), alpha(CREAM, 0.8f * fa));
  cw_vec2 kn[3] = { cw_v2(c.x, c.y + r * 0.95f), cw_v2(c.x - r * 0.2f, c.y + r * 1.22f), cw_v2(c.x + r * 0.2f, c.y + r * 1.22f) };
  cw_r2d_polygon(kn, 3, alpha(shade(col, 0.8f), fa));
  cw_r2d_circle(c, r * k + fmaxf(r * 0.12f, 1), 18, alpha(INK, fa));
  cw_r2d_circle(c, r * k, 18, alpha(col, fa));
  cw_r2d_circle(cw_v2(c.x - r * 0.35f * k, c.y - r * 0.38f * k), r * 0.26f * k, 10, alpha(rgba(1, 1, 1, 0.55f), fa));
  if (burst > 0) {
    /* the burst: shreds flying out */
    for (int i = 0; i < 8; i++) {
      float ang = (float)i * (CW_TAU / 8) + 0.3f, d0 = r * (1.0f + burst * 1.4f), d1 = d0 + r * 0.5f;
      cw_r2d_line(cw_v2(c.x + cw_cos(ang) * d0, c.y + cw_sin(ang) * d0), cw_v2(c.x + cw_cos(ang) * d1, c.y + cw_sin(ang) * d1), fmaxf(r * 0.16f, 1), alpha(col, a * (1 - burst)));
    }
  }
}

static const char *kart_name(const sim *S, int ki, char *b, size_t n, bool long_form) {
  if (S->k[ki].human) snprintf(b, n, long_form ? "PLAYER %d" : "P%d", ki + 1);
  else snprintf(b, n, "%s", CPU_NAME[ki % MAXK]);
  return b;
}

/* ── the HUD ──────────────────────────────────────────────────────────── */

#define FEED 4
typedef struct bhud {
  int seen_tick;
  int pop_t;          /* you popped someone: the stamp */
  int popped_t;       /* you lost one: the burst on your panel */
  int popped_slot;
  int out_t;          /* you are out */
  int by;             /* who did it */
  int pts_t;          /* the points counter flashes */
} bhud;
static bhud BH[MAXV];
static struct {
  int seen_tick;
  struct { int victim, by, t; } feed[FEED]; /* the elimination feed, newest first */
  int end_t;
} BF;

static void battle_hud_event(int v, const struct sim_event *e, const sim *S) {
  bhud *h = &BH[v];
  int me = game_view_kart(v);
  if (e->kind == EV_POP) {
    if (e->b == me) h->pop_t = 1, h->pts_t = 1;
    if (e->a == me) h->popped_t = 1, h->popped_slot = (int)(e->k + 0.5f), h->by = e->b;
  } else if (e->kind == EV_OUT) {
    if (e->a == me) h->out_t = 1, h->by = e->b;
  }
  (void)S;
}

static void battle_hud_tick(void) {
  const sim *S = game_sim();
  if (!S->battle) return;
  if (S->tick < BF.seen_tick) memset(&BF, 0, sizeof BF), memset(BH, 0, sizeof BH);
  if (S->tick != BF.seen_tick) {
    BF.seen_tick = S->tick;
    for (int i = 0; i < S->ev_n; i++) {
      const struct sim_event *e = &S->ev[i];
      if (e->kind != EV_OUT) continue;
      memmove(&BF.feed[1], &BF.feed[0], sizeof BF.feed[0] * (FEED - 1));
      BF.feed[0].victim = e->a, BF.feed[0].by = e->b, BF.feed[0].t = 1;
    }
  }
  for (int i = 0; i < FEED; i++)
    if (BF.feed[i].t > 0 && ++BF.feed[i].t > 240) BF.feed[i].t = 0;
  if (S->phase == PH_DONE) BF.end_t++;
  int nv = game_views();
  for (int v = 0; v < nv; v++) {
    bhud *h = &BH[v];
    if (h->pop_t > 0 && ++h->pop_t > 60) h->pop_t = 0;
    if (h->popped_t > 0 && ++h->popped_t > 40) h->popped_t = 0;
    if (h->pts_t > 0 && ++h->pts_t > 40) h->pts_t = 0;
    if (h->out_t > 0) h->out_t++;
  }
}

static void fmt_clock(char *b, size_t n, int ticks) {
  int secs = (ticks + TICK_HZ - 1) / TICK_HZ;
  snprintf(b, n, "%d:%02d", secs / 60, secs % 60);
}

/* top left: your balloons and your points */
static void hud_b_balloons(const hv *g, const bhud *h) {
  float s = g->s, x = g->x + 14 * s, y = g->y + 12 * s;
  const kart *k = g->k;
  cw_rect r = cw_rect_make(x, y, 196 * s, 86 * s);
  float flash = h->popped_t > 0 ? 1 - (float)h->popped_t / 40.0f : 0;
  slab(r, 14 * s, mix(PANEL, rgba(0.6f, 0.12f, 0.1f, 0.92f), flash * 0.8f), s);
  cw_color kc = kart_col(k->colour);
  for (int i = 0; i < BALLOONS; i++) {
    cw_vec2 c = cw_v2(x + 34 * s + (float)i * 36 * s, y + 34 * s + 2 * s * cw_sin(U.time * 3 + (float)i));
    bool have = i < k->balloons;
    float burst = !have && h->popped_t > 0 && i == h->popped_slot ? (float)h->popped_t / 40.0f : 0;
    if (burst > 0) balloon_icon(c, 14 * s, kc, 1, burst, false);
    else balloon_icon(c, 14 * s, kc, 1, 0, !have);
  }
  /* points */
  char b[16];
  snprintf(b, sizeof b, "%d", k->points);
  float pop = h->pts_t > 0 ? 1 + 0.5f * (1 - ease_back((float)h->pts_t / 14.0f)) : 1;
  tx_o(U.f_title, b, x + 168 * s, y + 38 * s, 40 * s * pop, h->pts_t > 0 ? LIME : MUSTARD, (txopt){ .align = CW_ALIGN_CENTER, .outline = 2 * s });
  tx(U.f_small, k->points == 1 ? "POP" : "POPS", x + 168 * s, y + 70 * s, hpx(14, s, 10), DIM, CW_ALIGN_CENTER);
}

/* top right: the clock and who is still in */
static void hud_b_clock(const hv *g) {
  const sim *S = g->S;
  float s = g->s, w = 170 * s, x = g->x + g->w - w - 14 * s, y = g->y + 12 * s;
  int left = S->phase == PH_RACE || S->phase == PH_DONE ? S->battle_left : S->battle_limit;
  bool hurry = S->phase == PH_RACE && left < 10 * TICK_HZ;
  cw_color fill = hurry && ((S->tick / 15) & 1) ? rgba(0.55f, 0.1f, 0.08f, 0.92f) : PANEL;
  slab(cw_rect_make(x, y, w, 62 * s), 12 * s, fill, s);
  char b[24];
  fmt_clock(b, sizeof b, left);
  tx(U.f_title, b, x + w * 0.5f + 4 * s, y + 31 * s, 38 * s, hurry ? CREAM : left < 30 * TICK_HZ ? RUST : CREAM, CW_ALIGN_CENTER);
  if (g->compact) return;
  cw_rect r2 = cw_rect_make(x, y + 68 * s, w, 34 * s);
  slab(r2, 12 * s, PANEL, s);
  snprintf(b, sizeof b, "%d OF %d IN", S->alive, S->nkarts);
  tx(U.f_hud, b, x + w * 0.5f + 4 * s, r2.y + 17 * s, 18 * s, CREAM, CW_ALIGN_CENTER);
}

/* the points table: everyone in standing order, their balloons as dots */
static void hud_b_table(const hv *g, float x, float y, int rows_max) {
  const sim *S = g->S;
  float s = g->s, rh = 24 * s, w = 196 * s;
  int n = S->nkarts < rows_max ? S->nkarts : rows_max;
  for (int r = 0; r < n; r++) {
    int ki = S->order[r];
    const kart *k = &S->k[ki];
    bool me = ki == g->kart;
    cw_rect rr = cw_rect_make(x, y + (float)r * rh, w, rh - 3 * s);
    para(rr, 6 * s, me ? alpha(MUSTARD, 0.95f) : rgba(0.09f, 0.07f, 0.09f, k->out ? 0.5f : 0.72f));
    para_lines(rr, 6 * s, 1.5f * s + 0.4f, alpha(INK, 0.9f));
    cw_color ink = me ? INK : k->out ? DIM : CREAM;
    char b[24];
    cw_r2d_circle(cw_v2(rr.x + 16 * s, rr.y + rr.h * 0.5f), 5.4f * s, 12, INK);
    cw_r2d_circle(cw_v2(rr.x + 16 * s, rr.y + rr.h * 0.5f), 4.2f * s, 12, kart_col(k->colour));
    tx_o(U.f_hud, kart_name(S, ki, b, sizeof b, false), rr.x + 28 * s, rr.y + rr.h * 0.5f, 14 * s, ink, (txopt){ .align = CW_ALIGN_LEFT, .outline = me ? 0.01f : 0, .no_shadow = true });
    if (k->out) tx_o(U.f_small, "OUT", rr.x + 128 * s, rr.y + rr.h * 0.5f, 13 * s, me ? INK : RED, (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
    else
      for (int bi = 0; bi < BALLOONS; bi++)
        cw_r2d_circle(cw_v2(rr.x + 114 * s + (float)bi * 11 * s, rr.y + rr.h * 0.5f), 3.6f * s, 10, bi < k->balloons ? (me ? INK : kart_col(k->colour)) : alpha(DIM, 0.35f));
    snprintf(b, sizeof b, "%d", k->points);
    tx_o(U.f_title, b, rr.x + rr.w - 14 * s, rr.y + rr.h * 0.5f, 14 * s, me ? INK : MUSTARD, (txopt){ .align = CW_ALIGN_RIGHT, .outline = 0.01f, .no_shadow = true });
  }
}

/* "POP!" when you popped one */
static void hud_b_stamps(const hv *g, const bhud *h) {
  float s = g->s, cx = g->x + g->w * 0.5f, cy = g->y + g->h * (g->compact || g->medium ? 0.46f : 0.34f);
  if (h->pop_t > 0) {
    float t = (float)h->pop_t / 60.0f;
    float sc = (0.4f + 0.6f * ease_back((float)h->pop_t / 9.0f)) * (g->compact ? 0.8f : 1.0f);
    float a = t > 0.7f ? 1 - (t - 0.7f) / 0.3f : 1;
    float rot = 0.12f * cw_sin((float)h->pop_t * 0.5f) * (1 - t);
    /* a star burst behind the word */
    cw_vec2 st[16];
    for (int k = 0; k < 16; k++) {
      float ang = (float)k * (CW_TAU / 16) + rot, rr = (k & 1 ? 46 : 74) * s * sc;
      st[k] = cw_v2(cx + cw_cos(ang) * rr * 1.4f, cy + cw_sin(ang) * rr);
    }
    cw_r2d_fan(cw_v2(cx, cy), st, 16, true, alpha(rgba(0.95f, 0.3f, 0.45f, 0.9f), a));
    cw_r2d_polygon_lines(st, 16, 3 * s, alpha(INK, a));
    tx_o(U.f_title, "POP!", cx, cy - 2 * s, 64 * s * sc, alpha(CREAM, a), (txopt){ .align = CW_ALIGN_CENTER, .outline = 3 * s });
    tx_o(U.f_title, "+1", cx + 96 * s * sc, cy + 34 * s * sc, 26 * s * sc, alpha(LIME, a), (txopt){ .align = CW_ALIGN_CENTER, .outline = 2 * s });
  }
  if (h->popped_t > 0 && h->popped_t < 40 && g->k->balloons > 0) {
    float t = (float)h->popped_t / 40.0f;
    float a = t > 0.6f ? 1 - (t - 0.6f) / 0.4f : 1;
    char b[48], nm[24];
    if (h->by >= 0) snprintf(b, sizeof b, "POPPED BY %s", kart_name(g->S, h->by, nm, sizeof nm, false));
    else snprintf(b, sizeof b, "POPPED!");
    tx_o(U.f_title, b, cx, g->y + g->h * 0.42f - t * 10 * s, 28 * s, alpha(RED, a), (txopt){ .align = CW_ALIGN_CENTER, .outline = 2 * s });
    snprintf(b, sizeof b, "%d BALLOON%s LEFT", g->k->balloons, g->k->balloons == 1 ? "" : "S");
    tx_o(U.f_hud, b, cx, g->y + g->h * 0.42f + 30 * s - t * 10 * s, 20 * s, alpha(CREAM, a), (txopt){ .align = CW_ALIGN_CENTER });
  }
}

/* the elimination feed under the item, in every view */
static void hud_b_feed(const hv *g) {
  const sim *S = g->S;
  float s = g->s, cx = g->x + g->w * 0.5f, y = g->y + (g->compact ? 96 : 128) * s;
  int shown = 0;
  for (int i = 0; i < FEED && shown < (g->compact ? 1 : 3); i++) {
    if (BF.feed[i].t <= 0) continue;
    float t = (float)BF.feed[i].t / 240.0f;
    float a = t > 0.8f ? 1 - (t - 0.8f) / 0.2f : 1;
    float in = ease_out((float)BF.feed[i].t / 12.0f);
    char b[64], n1[24], n2[24];
    int vi = BF.feed[i].victim, by = BF.feed[i].by;
    if (by >= 0) snprintf(b, sizeof b, "%s knocked out %s", kart_name(S, by, n2, sizeof n2, false), kart_name(S, vi, n1, sizeof n1, false));
    else snprintf(b, sizeof b, "%s is out", kart_name(S, vi, n1, sizeof n1, false));
    float fs = hpx(17, s, 11), w = tx_width(U.f_hud, b, fs) + 40 * s;
    cw_rect r = cw_rect_make(cx - w * 0.5f, y + (float)shown * 28 * s - (1 - in) * 20 * s, w, 24 * s);
    para(r, 8 * s, rgba(0.1f, 0.06f, 0.08f, 0.85f * a));
    cw_r2d_circle(cw_v2(r.x + 14 * s, r.y + r.h * 0.5f), 5 * s, 10, alpha(kart_col(S->k[vi].colour), a));
    tx(U.f_hud, b, cx + 6 * s, r.y + r.h * 0.5f, fs, alpha(CREAM, a), CW_ALIGN_CENTER);
    shown++;
  }
}

/* your kart is out: a big OUT, then who you are watching */
static void hud_b_out(const hv *g, const bhud *h) {
  const sim *S = g->S;
  int own = game_view_own_kart(g->v);
  if (own < 0 || !S->k[own].out) return;
  float s = g->s, cx = g->x + g->w * 0.5f;
  int ot = h->out_t;
  if (ot > 0 && ot < 150) {
    float t = (float)ot / 150.0f;
    float in = ease_back((float)ot / 16.0f), a = t > 0.8f ? 1 - (t - 0.8f) / 0.2f : 1;
    tx_o(U.f_title, "OUT!", cx, g->y + g->h * 0.36f, 110 * s * in, alpha(RED, a), (txopt){ .align = CW_ALIGN_CENTER, .outline = 5 * s });
    char b[64], nm[24];
    if (h->by >= 0) snprintf(b, sizeof b, "Your last balloon went to %s", kart_name(S, h->by, nm, sizeof nm, true));
    else snprintf(b, sizeof b, "Your last balloon is gone");
    tx_o(U.f_hud, b, cx, g->y + g->h * 0.36f + 70 * s, hpx(22, s, 12), alpha(CREAM, a), (txopt){ .align = CW_ALIGN_CENTER });
  }
  int wk = g->kart;
  if (wk == own || S->k[wk].out || S->phase == PH_DONE) return;
  /* the spectator's tag at the bottom */
  char b[48], nm[24];
  snprintf(b, sizeof b, "WATCHING %s", kart_name(S, wk, nm, sizeof nm, true));
  float fs = hpx(20, s, 12), w = tx_width(U.f_title, b, fs) + 60 * s;
  cw_rect r = cw_rect_make(cx - w * 0.5f, g->y + g->h - 56 * s, w, 36 * s);
  slab(r, 10 * s, rgba(0.1f, 0.07f, 0.09f, 0.85f), s * 0.8f);
  cw_r2d_circle(cw_v2(r.x + 20 * s, r.y + r.h * 0.5f), 6 * s, 12, kart_col(S->k[wk].colour));
  tx(U.f_title, b, cx + 8 * s, r.y + r.h * 0.5f, fs, CREAM, CW_ALIGN_CENTER);
}

/* before the start: the arena and the rules; at the end, why it ended */
static void hud_b_banner(const hv *g) {
  const sim *S = g->S;
  float s = g->s;
  if (S->phase == PH_GRID || S->phase == PH_COUNT) {
    float t = S->phase == PH_GRID ? (float)S->phase_ticks / 45.0f * 0.2f : 0.2f + (float)S->phase_ticks / 180.0f * 0.8f;
    float in = ease_out(t / 0.2f), out = t > 0.85f ? ease_io((t - 0.85f) / 0.15f) : 0;
    float cx = g->x + g->w * 0.5f, y = g->y + g->h - 92 * s + (1 - in) * 100 * s + out * 100 * s;
    float w = fminf(g->w * 0.92f, 520 * s), a = in * (1 - out);
    cw_rect r = cw_rect_make(cx - w * 0.5f, y, w, 74 * s);
    slab(r, 14 * s, rgba(0.1f, 0.07f, 0.09f, 0.9f * a), s);
    char b[64];
    snprintf(b, sizeof b, "BATTLE  -  %s", S->md->name);
    tx_fit(U.f_title, b, cx, y + 24 * s, 26 * s, w - 40 * s, alpha(MUSTARD, a), CW_ALIGN_CENTER);
    char c[96], tm[16];
    fmt_clock(tm, sizeof tm, S->battle_limit);
    snprintf(c, sizeof c, "%d balloons each. Pop the others. Last one standing, or most pops in %s.", BALLOONS, tm);
    tx_fit(U.f_hud, c, cx, y + 52 * s, hpx(16, s, 11), w - 30 * s, alpha(CREAM, a), CW_ALIGN_CENTER);
    return;
  }
  if (S->phase != PH_DONE) return;
  const char *head = S->battle_end == BE_TIME ? "TIME!" : S->battle_end == BE_LAST ? "LAST ONE STANDING" : "FINISHED";
  float in = ease_back((float)BF.end_t / 18.0f);
  float cx = g->x + g->w * 0.5f, cy = g->y + g->h * 0.3f;
  banner(g, head, NULL, MUSTARD, fminf((float)BF.end_t / 120.0f, 0.79f), cy, g->compact ? 40 : 54);
  (void)in;
  (void)cx;
}

static void hud_battle_view(const hv *g, int nviews) {
  const sim *S = g->S;
  const bhud *h = &BH[g->v];
  hud_b_balloons(g, h);
  hud_item(g);
  hud_b_clock(g);
  const arena_rt *a = arena_current();
  if (a) {
    float ms = (g->compact || g->medium ? 205 : 180) * g->s; /* a split view has no table: the map can be bigger */
    cw_rect mr = cw_rect_make(g->x + 14 * g->s, g->y + g->h - ms - 14 * g->s, ms, ms);
    if (U.touch_on) mr = cw_rect_make(g->x + 14 * g->s, g->y + 104 * g->s, 128 * g->s, 128 * g->s);
    if (!(nviews == 3)) {
      rslab(mr, 10 * g->s, rgba(0.07f, 0.06f, 0.08f, 0.8f), g->s);
      arena_draw_map(a->def, mr, 8 * g->s, g->s, S, g->kart, false);
    }
  }
  if (!g->medium && !U.touch_on) hud_b_table(g, g->x + 14 * g->s, g->y + 108 * g->s, MAXK);
  hud_speed(g);
  hud_drift(g);
  hud_b_banner(g);
  hud_countdown(g);
  hud_popups(g);
  hud_b_stamps(g, h);
  hud_b_feed(g);
  hud_b_out(g, h);
  if (nviews == 1 && pointer_mode() && S->phase != PH_DONE) {
    cw_rect pr = pause_button_rect(g);
    bool hot = in_rect(N.m, pr) && U.device == DEV_MOUSE;
    rslab(pr, 8 * g->s, hot ? alpha(MUSTARD, 0.95f) : PANEL, g->s);
    cw_color bar = hot ? INK : CREAM;
    cw_r2d_rect(cw_rect_make(pr.x + pr.w * 0.32f, pr.y + pr.h * 0.25f, pr.w * 0.12f, pr.h * 0.5f), bar);
    cw_r2d_rect(cw_rect_make(pr.x + pr.w * 0.56f, pr.y + pr.h * 0.25f, pr.w * 0.12f, pr.h * 0.5f), bar);
  }
}

/* the shared quarter when three play: the arena with everyone on it, and the table */
static void hud_battle_shared(float W, float H) {
  const arena_rt *a = arena_current();
  if (!a) return;
  float qx = W * 0.5f, qy = 0, qw = W * 0.5f, qh = H * 0.5f;
  hv q = { 0 };
  q.S = game_sim();
  q.x = qx, q.y = qy, q.w = qw, q.h = qh;
  q.s = fminf(qw / 960.0f, qh / 540.0f) * 2.0f;
  q.kart = -1;
  q.k = &q.S->k[0];
  cw_r2d_rect(cw_rect_make(qx, qy, qw, qh), rgba(0.06f, 0.05f, 0.08f, 1));
  cw_rect mr = cw_rect_make(qx + 10 * q.s, qy + 10 * q.s, qh - 20 * q.s, qh - 20 * q.s);
  arena_draw_map(a->def, mr, 6 * q.s, q.s, q.S, -1, false);
  float lx = mr.x + mr.w + 14 * q.s;
  if (lx + 196 * q.s > qx + qw) lx = qx + qw - 196 * q.s - 8;
  hud_b_table(&q, lx, qy + 14 * q.s, 8);
}

/* ── the arena select ─────────────────────────────────────────────────── */

typedef struct arlay {
  float s;
  cw_rect card[NARENAS], detail, optrow[BO_KINDS], start;
} arlay;

static void arena_layout(float W, float H, arlay *l) {
  float s = lscale(W, H);
  l->s = s;
  float gx = W * 0.03f, gw = W * 0.94f, gap = 14 * s;
  if (!is_portrait(W, H)) {
    float top = 92 * s, bot = 70 * s, ch = H - top - bot;
    float cw = (gw * 0.6f - gap) / 2;
    for (int i = 0; i < NARENAS; i++) l->card[i] = cw_rect_make(gx + (float)i * (cw + gap), top, cw, ch);
    float ox = gx + gw * 0.6f + gap, ow = gw * 0.4f - gap;
    l->detail = cw_rect_make(ox, top, ow, ch * 0.42f);
    float rh = 44 * s, rg = 6 * s, oy = top + ch * 0.42f + 12 * s;
    for (int i = 0; i < BO_KINDS; i++) l->optrow[i] = cw_rect_make(ox, oy + (float)i * (rh + rg), ow, rh);
    float sy = oy + (float)BO_KINDS * (rh + rg) + 6 * s;
    l->start = cw_rect_make(ox, sy, ow, fminf(56 * s, top + ch - sy));
  } else {
    float top = 96 * s, cw = (gw - gap) / 2, ch = cw * 1.15f;
    for (int i = 0; i < NARENAS; i++) l->card[i] = cw_rect_make(gx + (float)i * (cw + gap), top, cw, ch);
    float y = top + ch + 12 * s;
    l->detail = cw_rect_make(gx, y, gw, 110 * s);
    y += 120 * s;
    float rh = 46 * s, rg = 6 * s;
    for (int i = 0; i < BO_KINDS; i++) l->optrow[i] = cw_rect_make(gx, y + (float)i * (rh + rg), gw, rh);
    y += (float)BO_KINDS * (rh + rg) + 6 * s;
    l->start = cw_rect_make(gx, y, gw, 54 * s);
  }
}

static void bo_text(int o, char *b, size_t n) {
  switch (o) {
  case BO_KARTS: snprintf(b, n, "%d", U.bt.karts); break;
  case BO_TIME: snprintf(b, n, "%d:%02d", BATTLE_SECS[U.bt.secs_i] / 60, BATTLE_SECS[U.bt.secs_i] % 60); break;
  default: snprintf(b, n, "%s", SKILL_NAME[U.skill]); break;
  }
}
static void bo_bump(int o, int dir) {
  switch (o) {
  case BO_KARTS: {
    int lo = battle_karts_min();
    U.bt.karts += dir;
    if (U.bt.karts > MAXK) U.bt.karts = lo;
    if (U.bt.karts < lo) U.bt.karts = MAXK;
    break;
  }
  case BO_TIME: U.bt.secs_i = (U.bt.secs_i + dir + 4) % 4; break;
  default: U.skill = (U.skill + dir + 4) % 4; break;
  }
  sfx_move();
}

static void start_battle_from_menu(void) {
  race_setup rs = { .map = U.bt.arena, .laps = 1, .humans = U.setup.humans, .cpu_skill = U.skill, .items = true, .mode = RM_BATTLE,
                    .karts = U.bt.karts, .battle_secs = BATTLE_SECS[U.bt.secs_i] };
  U.cup.on = false;
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = U.veh[i], rs.colour[i] = U.col[i];
  rs.speed_class = U.cls, rs.assist_steer = (U.assist & 1) != 0, rs.assist_gas = (U.assist & 2) != 0;
  U.setup = rs;
  save_write();
  sfx_ok();
  begin_pending(PD_START_RACE);
}

static void tick_arena(float W, float H) {
  arlay l;
  arena_layout(W, H, &l);
  if (U.bt.karts < battle_karts_min() || U.bt.karts > MAXK) U.bt.karts = MAXK;
  int dx = 0, dy = 0;
  {
    int hx = (cw_action_pressed(0, U.m_right) ? 1 : 0) - (cw_action_pressed(0, U.m_left) ? 1 : 0);
    int hy = (cw_action_pressed(0, U.m_down) ? 1 : 0) - (cw_action_pressed(0, U.m_up) ? 1 : 0);
    nav_step(&U.nav, hx, hy, &dx, &dy);
  }
  bool ok = cw_action_just_pressed(0, U.m_ok) && U.mt > 12;
  bool back = cw_action_just_pressed(0, U.a_back) && U.mt > 8;
  bool start = false;
  if (dy) U.bt.focus = CW_CLAMP(U.bt.focus + dy, 0, BO_KINDS + 1), sfx_move();
  if (U.bt.focus == 0) {
    if (dx) U.bt.arena = (U.bt.arena + dx + NARENAS) % NARENAS, sfx_move();
    if (ok) start = true;
  } else if (U.bt.focus <= BO_KINDS) {
    if (dx) bo_bump(U.bt.focus - 1, dx);
    if (ok) bo_bump(U.bt.focus - 1, 1);
  } else if (ok) start = true;
  if (pointer_mode() && N.moved)
    for (int i = 0; i < NARENAS; i++)
      if (in_rect(N.m, l.card[i]) && (U.bt.arena != i || U.bt.focus != 0)) U.bt.arena = i, U.bt.focus = 0, sfx_move();
  if (N.click) {
    for (int i = 0; i < NARENAS; i++)
      if (in_rect(N.m, l.card[i])) {
        if (U.bt.arena == i && U.device != DEV_TOUCH) start = true;
        U.bt.arena = i, U.bt.focus = 0;
      }
    for (int r = 0; r < BO_KINDS; r++)
      if (in_rect(N.m, l.optrow[r])) {
        U.bt.focus = r + 1;
        float t = (N.m.x - l.optrow[r].x) / l.optrow[r].w;
        bo_bump(r, t < 0.5f ? -1 : 1);
      }
    if (in_rect(N.m, l.start)) start = true;
    if (in_rect(N.m, back_button_rect(W, H))) back = true;
  }
  if (start) {
    start_battle_from_menu();
    return;
  }
  if (back) {
    sfx_back();
    for (int i = 0; i < U.setup.humans; i++) U.ready[i] = false;
    goto_mode(M_VEHICLE);
    U.mt = 30;
  }
}

static void draw_arena(float W, float H, float s) {
  backdrop(W, H, s, 0.86f);
  header(W, H, s, "BATTLE", "Choose an arena");
  arlay l;
  arena_layout(W, H, &l);
  for (int i = 0; i < NARENAS; i++) {
    cw_rect r = l.card[i];
    float ap = ease_back(((float)U.mt - (float)i * 4) / 16.0f);
    if (ap <= 0) continue;
    r.y += (1 - ap) * 70 * s;
    bool sel = U.bt.arena == i;
    slab(r, 0, sel ? rgba(0.2f, 0.15f, 0.1f, 0.95f) : rgba(0.1f, 0.075f, 0.09f, 0.88f), s);
    cw_r2d_rect_lines(cw_rect_make(r.x + 3 * s, r.y + 3 * s, r.w - 6 * s, r.h - 6 * s), 3 * s, sel ? (U.bt.focus == 0 ? MUSTARD : alpha(MUSTARD, 0.55f)) : rgba(0.4f, 0.36f, 0.36f, 0.6f));
    cw_rect mr = cw_rect_make(r.x + 10 * s, r.y + 10 * s, r.w - 20 * s, r.h - 52 * s);
    arena_draw_map(ARENAS[i]->arena, mr, 6 * s, s, NULL, -1, true);
    tx_fit(U.f_title, ARENAS[i]->name, r.x + r.w * 0.5f, r.y + r.h - 22 * s, 22 * s, r.w - 20 * s, sel ? MUSTARD : CREAM, CW_ALIGN_CENTER);
  }
  /* the arena picked */
  const map_def *md = ARENAS[U.bt.arena];
  const arena_def *ad = md->arena;
  float ap = clamp01(((float)U.mt - 10) / 14.0f);
  cw_rect d = l.detail;
  d.y += (1 - ease_out(ap)) * 40 * s;
  slab(d, 0, rgba(0.1f, 0.075f, 0.09f, 0.9f), s);
  tx_fit(U.f_title, md->name, d.x + 16 * s, d.y + 26 * s, 26 * s, d.w - 32 * s, MUSTARD, CW_ALIGN_LEFT);
  float bh = tx_para(U.f_hud, md->blurb, cw_rect_make(d.x + 16 * s, d.y + 46 * s, d.w - 32 * s, 60 * s), 16 * s, CREAM, CW_ALIGN_LEFT);
  int ramps = 0, decks = 0;
  for (int i = 0; i < ad->nprims; i++) ramps += ad->prims[i].kind == AP_RAMP && !ad->prims[i].ghost, decks += ad->prims[i].look == AL_DECK;
  char b[96];
  snprintf(b, sizeof b, "%.0f x %.0f m    %d ramps    %d item boxes", ad->half_x * 2, ad->half_z * 2, ramps, ad->nboxes);
  if (d.y + 46 * s + bh + 14 * s < d.y + d.h) tx_fit(U.f_small, b, d.x + 16 * s, d.y + 46 * s + bh + 12 * s, 16 * s, d.w - 32 * s, DIM, CW_ALIGN_LEFT);
  (void)decks;
  for (int r = 0; r < BO_KINDS; r++) {
    char v[24];
    bo_text(r, v, sizeof v);
    draw_option_row(l.optrow[r], BO_LABEL[r], v, U.bt.focus == r + 1, s, clamp01(((float)U.mt - 14 - (float)r * 3) / 14.0f));
  }
  bool sf = U.bt.focus == 0 || U.bt.focus == BO_KINDS + 1;
  draw_button(l.start, "START BATTLE", sf, true, s * 0.82f, clamp01(((float)U.mt - 22) / 14.0f));
  draw_button(back_button_rect(W, H), "BACK", false, true, s * 0.8f, 1);
  hint_line(W, H, s, dev_hint("D-pad: choose   (A) go   down: options   (B) back", "Arrows choose   Enter: go   Down: options   Esc: back", "Click an arena, again to go", "Tap an arena, then START"));
}

/* ── the results ──────────────────────────────────────────────────────── */

static void battle_results_build(void) {
  const sim *S = game_sim();
  U.res_n = 0;
  U.res_winner_human = 0;
  for (int i = 0; i < S->nkarts && i < MAXK; i++) {
    int ki = S->order[i];
    const kart *k = &S->k[ki];
    result_row *r = &U.res[U.res_n++];
    memset(r, 0, sizeof *r);
    r->kart = ki, r->place = i + 1, r->veh = k->veh, r->colour = k->colour;
    r->human = k->human ? ki : -1;
    r->finished = !k->out;
    r->time = k->out ? (float)k->out_tick / TICK_HZ : 0;
    r->hits = k->points;
    r->tricks = k->balloons;
    r->points = k->points;
    if (k->human && !U.res_winner_human) U.res_winner_human = i + 1;
  }
  goto_mode(M_RESULTS);
  U.res_ticks = 0;
  U.sel = 0;
  game_set_camera(2);
}

static void tick_battle_results(float W, float H) {
  if (U.res_ticks < 100) {
    if ((N.ok || N.start || N.click) && U.res_ticks > 20) U.res_ticks = 100;
    return;
  }
  int nb = 3;
  if (U.sel >= nb) U.sel = 0;
  if (N.dx) U.sel = (U.sel + N.dx + nb) % nb, sfx_move();
  if (is_portrait(W, H) && N.dy) U.sel = (U.sel + N.dy + nb) % nb, sfx_move();
  int act = -1;
  if (pointer_mode() && N.moved)
    for (int i = 0; i < nb; i++)
      if (in_rect(N.m, res_button_n(i, nb, W, H)) && U.sel != i) U.sel = i, sfx_move();
  if (N.click)
    for (int i = 0; i < nb; i++)
      if (in_rect(N.m, res_button_n(i, nb, W, H))) U.sel = i, act = i;
  if (N.ok && U.res_ticks > 115) act = U.sel;
  if (act == 0) sfx_ok(), begin_pending(PD_RESTART);
  else if (act == 1) sfx_ok(), begin_pending(PD_NEXT_MAP);
  else if (act == 2) sfx_ok(), begin_pending(PD_TO_MENU);
}

static void draw_battle_results(float W, float H, float s) {
  const sim *S = game_sim();
  bool por = is_portrait(W, H);
  int t = U.res_ticks;
  float head = ease_back((float)t / 20.0f), panel = ease_out(((float)t - 80) / 24.0f);
  const result_row *w = &U.res[0];
  char nm[32], wn[32], b[96];
  kart_name(S, w->kart, wn, sizeof wn, true);
  {
    snprintf(b, sizeof b, U.res_winner_human == 1 ? "%s WINS THE BATTLE!" : "%s TAKES THE BATTLE", wn);
    float bw = fminf(W * 0.94f, tx_width(U.f_title, b, 42 * s) + 110 * s);
    float by = 22 * s - (1 - head) * 120 * s;
    cw_rect r = cw_rect_make(W * 0.5f - bw * 0.5f, by, bw, 70 * s);
    slab(r, 18 * s, rgba(0.1f, 0.07f, 0.09f, 0.94f), s);
    para(cw_rect_make(r.x + 6 * s, r.y + r.h - 9 * s, r.w - 12 * s, 6 * s), 0, kart_col(w->colour));
    tx_fit(U.f_title, b, r.x + r.w * 0.5f + 8 * s, r.y + 32 * s, 42 * s, r.w - 80 * s, PLACE_COL[0], CW_ALIGN_CENTER);
    const char *why = S->battle_end == BE_LAST ? "Last one standing" : S->battle_end == BE_TIME ? "Most pops when time ran out" : "Every player is out";
    tx(U.f_hud, why, W * 0.5f, r.y + r.h + 16 * s, 18 * s, alpha(CREAM, head), CW_ALIGN_CENTER);
  }
  if (panel <= 0) {
    tx(U.f_hud, dev_hint("(A) to see the results", "Enter to see the results", "Click to see the results", "Tap to see the results"), W * 0.5f, H - 50 * s, 22 * s, alpha(CREAM, 0.5f + 0.4f * cw_sin(U.time * 5)),
       CW_ALIGN_CENTER);
    return;
  }
  float gx = W * 0.025f;
  cw_rect tab = por ? cw_rect_make(gx, 130 * s + (1 - panel) * 200 * s, W - 2 * gx, 8 * 32 * s + 50 * s)
                    : cw_rect_make(gx - (1 - panel) * 500 * s, 124 * s, W * 0.56f, H - 124 * s - 100 * s);
  slab(tab, 0, rgba(0.09f, 0.065f, 0.08f, 0.92f * panel), s);
  float rowh = por ? 32 * s : fminf(36 * s, (tab.h - 50 * s) / 8.0f);
  float cx_name = tab.x + 60 * s, cx_bal = tab.x + tab.w * 0.52f, cx_stat = tab.x + tab.w * 0.74f, cx_pts = tab.x + tab.w - 22 * s;
  float hy = tab.y + 20 * s, hsz = 15 * s;
  tx(U.f_small, "DRIVER", cx_name, hy, hsz, DIM, CW_ALIGN_LEFT);
  tx(U.f_small, "BALLOONS", cx_bal, hy, hsz, DIM, CW_ALIGN_CENTER);
  tx(U.f_small, "LASTED", cx_stat, hy, hsz, DIM, CW_ALIGN_CENTER);
  tx(U.f_small, "POPS", cx_pts, hy, hsz, DIM, CW_ALIGN_RIGHT);
  for (int i = 0; i < U.res_n; i++) {
    const result_row *r = &U.res[i];
    float ra = clamp01(((float)t - 90 - (float)i * 5) / 16.0f);
    if (ra <= 0) continue;
    float y = tab.y + 36 * s + (float)i * rowh;
    float off = (1 - ease_out(ra)) * 120 * s;
    cw_rect rr = cw_rect_make(tab.x + 10 * s + off, y, tab.w - 20 * s, rowh - 4 * s);
    bool human = r->human >= 0;
    para(rr, 6 * s, human ? alpha(mix(PANEL, kart_col(r->colour), 0.28f), ra) : alpha(rgba(0.14f, 0.11f, 0.13f, 0.8f), ra));
    if (human) para_lines(rr, 6 * s, 2 * s, alpha(kart_col(r->colour), ra));
    float cy = rr.y + rr.h * 0.5f;
    snprintf(b, sizeof b, "%d", r->place);
    tx(U.f_title, b, rr.x + 16 * s, cy, rowh * 0.5f, alpha(r->place <= 3 ? PLACE_COL[r->place - 1] : CREAM, ra), CW_ALIGN_CENTER);
    cw_r2d_circle(cw_v2(rr.x + 40 * s, cy), 6 * s, 12, alpha(INK, ra));
    cw_r2d_circle(cw_v2(rr.x + 40 * s, cy), 4.8f * s, 12, alpha(kart_col(r->colour), ra));
    kart_name(S, r->kart, nm, sizeof nm, true);
    tx_fit(U.f_hud, nm, cx_name, cy, rowh * 0.5f, cx_bal - cx_name - 60 * s, alpha(CREAM, ra), CW_ALIGN_LEFT);
    for (int bi = 0; bi < BALLOONS; bi++) balloon_icon(cw_v2(cx_bal + ((float)bi - 1) * rowh * 0.62f, cy - rowh * 0.08f), rowh * 0.2f, kart_col(r->colour), ra, 0, bi >= r->tricks);
    if (r->finished) snprintf(b, sizeof b, r->place == 1 && S->battle_end == BE_LAST ? "STANDING" : "TO THE END");
    else fmt_time(b, sizeof b, r->time);
    tx(U.f_hud, b, cx_stat, cy, rowh * 0.42f, alpha(r->finished ? LIME : CREAM, ra), CW_ALIGN_CENTER);
    snprintf(b, sizeof b, "%d", r->points);
    tx(U.f_title, b, cx_pts, cy, rowh * 0.5f, alpha(MUSTARD, ra), CW_ALIGN_RIGHT);
  }
  /* the podium: the top three on their blocks, the balloons each kept tied on behind, confetti over it all */
  if (!por) {
    cw_rect pod = cw_rect_make(W * 0.6f + (1 - panel) * 400 * s, 124 * s, W * 0.37f, H - 124 * s - 110 * s);
    float baseY = pod.y + pod.h - 6 * s;
    static const int ord[3] = { 1, 0, 2 };
    static const float hgt[3] = { 80, 118, 56 };
    float bw = pod.w / 3 - 6 * s;
    draw_confetti(cw_rect_make(pod.x, pod.y - 40 * s, pod.w, pod.h + 40 * s), s, U.time);
    for (int c = 0; c < 3 && c < U.res_n; c++) {
      int pl = ord[c];
      const result_row *r = &U.res[pl];
      float bx = pod.x + (float)c * (bw + 9 * s);
      float bh2 = hgt[c] * s * ease_out(((float)t - 110 - (float)c * 6) / 20.0f);
      cw_rect blk = cw_rect_make(bx, baseY - bh2, bw, bh2);
      if (bh2 > 1) {
        slab(blk, 0, shade(PLACE_COL[pl], 0.9f), s);
        char nb[16];
        snprintf(nb, sizeof nb, "%d", pl + 1);
        tx_o(U.f_title, nb, bx + bw * 0.5f, blk.y + fminf(bh2 * 0.5f, 34 * s), 40 * s, INK, (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
      }
      float u = bw / 4.2f, hop = pl == 0 ? fabsf(cw_sin(U.time * 4)) * 8 * s : 0;
      float gy = baseY - bh2 - hop, kx = bx + bw * 0.5f;
      draw_kart_side(kx, gy, u, r->veh, r->colour, pl == 0 ? 0.8f : 0.2f, U.time * 0.6f);
      for (int bi = 0; bi < r->tricks; bi++) {
        cw_vec2 bc = cw_v2(kx - u * 1.2f + ((float)bi - (float)(r->tricks - 1) * 0.5f) * u * 0.62f, gy - u * 3.0f + 4 * s * cw_sin(U.time * 2 + (float)(bi + c)) - (bi == 1 ? u * 0.25f : 0));
        cw_r2d_line(cw_v2(bc.x, bc.y + u * 0.3f), cw_v2(kx - u * 1.2f, gy - u * 1.2f), 1.2f * s, alpha(CREAM, 0.8f));
        balloon_icon(bc, u * 0.27f, kart_col(r->colour), 1, 0, false);
      }
      char rn[32];
      kart_name(S, r->kart, rn, sizeof rn, true);
      tx_fit(U.f_hud, rn, kx, gy - u * 4.1f - 4 * s, 15 * s, bw + 4 * s, pl == 0 ? MUSTARD : CREAM, CW_ALIGN_CENTER);
    }
  }
  static const char *const BL[3] = { "AGAIN", "NEXT ARENA", "MENU" };
  for (int i = 0; i < 3; i++) draw_button(res_button_n(i, 3, W, H), BL[i], U.sel == i && t > 100, true, s * 0.8f, clamp01(((float)t - 120 - (float)i * 4) / 14.0f));
}
