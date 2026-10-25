/* ui_hud.inc: the HUD over the race, one per view (included by ui.c, never built on its own).
 *
 * Every view lays itself out in a 960x540 design and scales by s, taken from the view's size against the
 * canvas (a quarter of the screen gets s about 0.5 at 960x540). Below s 0.85 the standings shrink and below
 * 0.62 they go, with the lap times, so a small view keeps only what a driver needs: place, lap, item, speed.
 *
 * The sim's events are valid only on the tick they are raised, so hud_tick reads them (once per sim tick)
 * and turns them into small timers and popups that the drawing reads. */

/* the drift charge needed for the blue, orange and purple levels (the numbers in sim.c) */
static const int DRIFT_LV[3] = { 4500, 9000, 14500 };

enum { PU_TRICK, PU_BOOST, PU_DRIFT, PU_START, PU_HIT, PU_BLOCK, PU_BACK, PU_SPIN, PU_KINDS };
typedef struct popup {
  int kind, t;
} popup;
#define NPOP 3

typedef struct hudv {
  int last_place, place_t, place_dir;
  int lap_t, lap_n;
  bool lap_final;
  float last_lap;
  int last_lap_t;
  bool last_lap_best;
  popup pop[NPOP];
  int hit_t, spin_t, shield_t, fall_t, boost_t;
  int wrong_t;
  int item_t, last_item;
  int drift_flash_t, drift_flash_lvl, drift_show;
  int finish_t;
  float speed_s; /* the needle, smoothed */
  float boost_glow;
} hudv;
static hudv HV[MAXV];
static int hud_seen_tick = -1;

static void hud_reset(void) {
  memset(HV, 0, sizeof HV);
  hud_seen_tick = -1;
  for (int v = 0; v < MAXV; v++) HV[v].last_place = 0, HV[v].last_item = 0;
}

static void pop_add(hudv *h, int kind) {
  for (int i = 0; i < NPOP; i++)
    if (h->pop[i].t > 0 && h->pop[i].kind == kind) {
      h->pop[i].t = 1;
      return;
    }
  int slot = 0, oldest = -1;
  for (int i = 0; i < NPOP; i++) {
    if (h->pop[i].t == 0) {
      slot = i;
      oldest = 1 << 30;
      break;
    }
    if (h->pop[i].t > oldest || oldest < 0) oldest = h->pop[i].t, slot = i;
  }
  h->pop[slot] = (popup){ kind, 1 };
}

static const char *const POP_TEXT[PU_KINDS] = { "TRICK!", "BOOST!", "DRIFT BOOST!", "GREAT START!", "HIT!", "BLOCKED!", "BACK ON TRACK", "SPIN OUT!" };
static cw_color pop_colour(int k) {
  switch (k) {
  case PU_TRICK: return MUSTARD;
  case PU_BOOST: return TEAL;
  case PU_DRIFT: return SKY;
  case PU_START: return LIME;
  case PU_HIT: return RED;
  case PU_BLOCK: return SKY;
  case PU_SPIN: return PINK;
  default: return CREAM;
  }
}

static void hud_event(int v, const struct sim_event *e, const sim *S) {
  hudv *h = &HV[v];
  int me = game_view_kart(v);
  switch (e->kind) {
  case EV_LAP:
    if (e->a == me) {
      h->lap_t = 1;
      h->lap_n = e->b;
      h->lap_final = e->b >= S->laps;
      h->last_lap = e->k;
      h->last_lap_t = 1;
      h->last_lap_best = S->k[me].best_lap > 0 && e->k <= S->k[me].best_lap + 0.005f;
    }
    break;
  case EV_FINISH:
    if (e->a == me) h->finish_t = 1;
    break;
  case EV_BOOST:
    if (e->a != me) break;
    h->boost_t = 1;
    if (e->b == 0 || e->b == 1) pop_add(h, PU_BOOST);
    else if (e->b == 2) pop_add(h, PU_DRIFT);
    else if (e->b == 3) pop_add(h, PU_START);
    else if (e->b == 4) pop_add(h, PU_BOOST);
    break;
  case EV_TRICK:
    if (e->a == me) pop_add(h, PU_TRICK);
    break;
  case EV_DRIFT_LEVEL:
    if (e->a == me) h->drift_flash_t = 1, h->drift_flash_lvl = e->b;
    break;
  case EV_HIT:
    if (e->a == me) h->hit_t = 1, pop_add(h, PU_HIT);
    break;
  case EV_SPIN:
    if (e->a == me) h->spin_t = 1, pop_add(h, PU_SPIN);
    break;
  case EV_SHIELD:
    if (e->a == me) h->shield_t = 1, pop_add(h, PU_BLOCK);
    break;
  case EV_FALL:
    if (e->a == me) h->fall_t = 1;
    break;
  case EV_RESPAWN:
    if (e->a == me) pop_add(h, PU_BACK);
    break;
  default: break;
  }
}

static void hud_tick(void) {
  if (!game_loaded() || (U.mode != M_RACE && U.mode != M_RESULTS)) return;
  const sim *S = game_sim();
  int nv = game_views();
  /* read each sim tick's events once */
  if (S->tick != hud_seen_tick) {
    hud_seen_tick = S->tick;
    for (int v = 0; v < nv; v++)
      for (int i = 0; i < S->ev_n; i++) hud_event(v, &S->ev[i], S);
    /* the countdown sounds are the UI's: a beep for each number, a note for go */
    for (int i = 0; i < S->ev_n; i++) {
      if (S->ev[i].kind == EV_COUNT) audio_ui(AUI_COUNT);
      else if (S->ev[i].kind == EV_GO) audio_ui(AUI_GO);
    }
  }
  for (int v = 0; v < nv; v++) {
    hudv *h = &HV[v];
    const kart *k = &S->k[game_view_kart(v)];
    if (h->last_place == 0) h->last_place = k->place;
    if (k->place != h->last_place) {
      h->place_dir = k->place < h->last_place ? 1 : -1;
      h->last_place = k->place;
      h->place_t = 1;
    }
    if (h->place_t > 0 && ++h->place_t > 90) h->place_t = 0;
    if (h->lap_t > 0 && ++h->lap_t > 150) h->lap_t = 0;
    if (h->last_lap_t > 0 && ++h->last_lap_t > 420) h->last_lap_t = 0;
    if (h->hit_t > 0 && ++h->hit_t > 40) h->hit_t = 0;
    if (h->spin_t > 0 && ++h->spin_t > 50) h->spin_t = 0;
    if (h->shield_t > 0 && ++h->shield_t > 24) h->shield_t = 0;
    if (h->fall_t > 0 && ++h->fall_t > 40) h->fall_t = 0;
    if (h->boost_t > 0 && ++h->boost_t > 20) h->boost_t = 0;
    if (h->finish_t > 0) h->finish_t++;
    if (h->drift_flash_t > 0 && ++h->drift_flash_t > 22) h->drift_flash_t = 0;
    if (k->drifting) h->drift_show = 20;
    else if (h->drift_show > 0) h->drift_show--;
    for (int i = 0; i < NPOP; i++)
      if (h->pop[i].t > 0 && ++h->pop[i].t > 56) h->pop[i].t = 0;
    if (k->item != h->last_item) {
      if (k->item) h->item_t = 1;
      h->last_item = k->item;
    }
    if (h->item_t > 0 && ++h->item_t > 30) h->item_t = 0;
    /* wrong way: the kart faces against the road for a second */
    bool wrong = false;
    if (S->phase == PH_RACE && !k->finished && k->grounded && k->loc.on_road && k->speed > 2.5f && !(S->track->s[k->loc.i].flags & (TF_LOOP | TF_GAP))) {
      tsample m = track_at(S->track, k->loc.s);
      wrong = v3dot(kart_fwd(k), m.f) < -0.15f;
    }
    if (wrong) h->wrong_t++;
    else h->wrong_t = h->wrong_t > 0 ? h->wrong_t - 2 : 0;
    if (h->wrong_t < 0) h->wrong_t = 0;
    h->speed_s += (fabsf(k->speed) - h->speed_s) * 0.2f;
    h->boost_glow += ((k->boost_ticks > 0 ? 1.0f : 0.0f) - h->boost_glow) * 0.15f;
  }
}

/* ── item icons ─────────────────────────────────────────────────────── */

static cw_vec2 rot2(cw_vec2 c, float x, float y, float a, float r) {
  float cs = cw_cos(a), sn = cw_sin(a);
  return cw_v2(c.x + (x * cs - y * sn) * r, c.y + (x * sn + y * cs) * r);
}
static void poly_rot(cw_vec2 c, const float *xy, int n, float a, float r, cw_color col, cw_color edge, float lw) {
  cw_vec2 p[16];
  for (int i = 0; i < n && i < 16; i++) p[i] = rot2(c, xy[i * 2], xy[i * 2 + 1], a, r);
  cw_r2d_polygon(p, n, col);
  if (lw > 0) cw_r2d_polygon_lines(p, n, lw, edge);
}
static void chevron(cw_vec2 c, float r, float off, cw_color col, float lw) {
  cw_vec2 p[3] = { cw_v2(c.x + off - r * 0.5f, c.y - r * 0.8f), cw_v2(c.x + off + r * 0.35f, c.y), cw_v2(c.x + off - r * 0.5f, c.y + r * 0.8f) };
  cw_r2d_polyline(p, 3, lw + 3, false, INK);
  cw_r2d_polyline(p, 3, lw, false, col);
}
static void missile_body(cw_vec2 c, float r, float a) {
  static const float body[] = { -0.7f, -0.2f, 0.35f, -0.2f, 0.7f, 0, 0.35f, 0.2f, -0.7f, 0.2f };
  static const float fin1[] = { -0.7f, -0.2f, -0.35f, -0.2f, -0.75f, -0.55f };
  static const float fin2[] = { -0.7f, 0.2f, -0.35f, 0.2f, -0.75f, 0.55f };
  poly_rot(c, fin1, 3, a, r, RED, INK, 2);
  poly_rot(c, fin2, 3, a, r, RED, INK, 2);
  poly_rot(c, body, 5, a, r, CREAM, INK, 2.5f);
  static const float nose[] = { 0.35f, -0.2f, 0.7f, 0, 0.35f, 0.2f };
  poly_rot(c, nose, 3, a, r, RED, INK, 2);
  /* exhaust flame */
  static const float fl[] = { -0.7f, -0.12f, -1.05f, 0, -0.7f, 0.12f };
  poly_rot(c, fl, 3, a, r, MUSTARD, INK, 0);
}
/* kind: IT_*; t: seconds, for the small animations */
static void item_icon(int kind, cw_vec2 c, float r, float t) {
  switch (kind) {
  case IT_TURBO:
    chevron(c, r * 0.8f, -r * 0.35f, RUST, r * 0.26f);
    chevron(c, r * 0.8f, r * 0.2f, MUSTARD, r * 0.26f);
    break;
  case IT_TURBO3:
    chevron(c, r * 0.62f, -r * 0.55f, RUST, r * 0.2f);
    chevron(c, r * 0.62f, -r * 0.05f, MUSTARD, r * 0.2f);
    chevron(c, r * 0.62f, r * 0.45f, CREAM, r * 0.2f);
    break;
  case IT_MISSILE: missile_body(c, r * 1.05f, -0.65f); break;
  case IT_HOMING: {
    missile_body(cw_v2(c.x - r * 0.1f, c.y + r * 0.1f), r * 0.9f, -0.65f);
    float p = 1 + 0.06f * cw_sin(t * 8);
    cw_r2d_circle_lines(cw_v2(c.x + r * 0.28f, c.y - r * 0.28f), r * 0.55f * p, r * 0.1f + 1, 20, LIME);
    for (int i = 0; i < 4; i++) {
      float a = (float)i * CW_PI * 0.5f;
      cw_vec2 d = cw_v2(cw_cos(a), cw_sin(a)), q = cw_v2(c.x + r * 0.28f, c.y - r * 0.28f);
      cw_r2d_line(cw_v2(q.x + d.x * r * 0.4f, q.y + d.y * r * 0.4f), cw_v2(q.x + d.x * r * 0.72f, q.y + d.y * r * 0.72f), r * 0.1f + 1, LIME);
    }
    break;
  }
  case IT_OIL: {
    cw_r2d_circle(cw_v2(c.x, c.y + r * 0.1f), r * 0.62f, 24, INK);
    cw_r2d_circle(cw_v2(c.x - r * 0.4f, c.y + r * 0.35f), r * 0.32f, 16, INK);
    cw_r2d_circle(cw_v2(c.x + r * 0.45f, c.y + r * 0.3f), r * 0.28f, 16, INK);
    cw_r2d_circle(cw_v2(c.x + r * 0.05f, c.y - r * 0.5f), r * 0.2f, 14, INK);
    cw_r2d_circle_lines(cw_v2(c.x, c.y + r * 0.1f), r * 0.62f, 1.5f, 24, rgba(0.6f, 0.5f, 0.9f, 0.7f));
    cw_r2d_arc(cw_v2(c.x - r * 0.1f, c.y), r * 0.18f, r * 0.3f, 3.6f, 4.9f, 8, rgba(0.7f, 0.65f, 1, 0.85f));
    break;
  }
  case IT_MINE: {
    for (int i = 0; i < 8; i++) {
      float a = (float)i * CW_PI * 0.25f;
      float sp[] = { 0.62f, -0.18f, 1.0f, 0, 0.62f, 0.18f };
      poly_rot(c, sp, 3, a, r * 0.95f, shade(DIM, 0.9f), INK, 1.5f);
    }
    cw_r2d_circle(c, r * 0.62f, 22, rgba(0.28f, 0.28f, 0.32f, 1));
    cw_r2d_circle_lines(c, r * 0.62f, 2.5f, 22, INK);
    cw_r2d_circle(c, r * 0.2f, 12, (int)(t * 4) & 1 ? RED : rgba(0.5f, 0.1f, 0.1f, 1));
    break;
  }
  case IT_SHIELD: {
    static const float sh[] = { -0.62f, -0.7f, 0.62f, -0.7f, 0.62f, -0.05f, 0.3f, 0.5f, 0, 0.78f, -0.3f, 0.5f, -0.62f, -0.05f };
    poly_rot(c, sh, 7, 0, r * 1.05f, SKY, INK, 3);
    static const float in[] = { -0.4f, -0.5f, 0.4f, -0.5f, 0.4f, -0.1f, 0, 0.5f, -0.4f, -0.1f };
    poly_rot(c, in, 5, 0, r * 1.05f, rgba(0.75f, 0.92f, 1, 1), INK, 0);
    cw_r2d_line(cw_v2(c.x, c.y - r * 0.55f), cw_v2(c.x, c.y + r * 0.4f), r * 0.12f + 1, SKY);
    break;
  }
  case IT_SHOCK: {
    static const float bolt[] = { 0.15f, -0.95f, -0.55f, 0.1f, -0.08f, 0.1f, -0.25f, 0.95f, 0.55f, -0.2f, 0.08f, -0.2f };
    poly_rot(c, bolt, 6, 0, r * 0.95f, MUSTARD, INK, 3);
    float p = 0.12f * cw_sin(t * 9);
    cw_r2d_circle_lines(c, r * (0.85f + p), 2, 22, alpha(MUSTARD, 0.6f));
    break;
  }
  default: break;
  }
}

/* ── pieces of the HUD ──────────────────────────────────────────────── */

typedef struct hv {
  int v, kart;
  float x, y, w, h, s;
  bool medium, compact, shared_map;
  const sim *S;
  const kart *k;
  const hudv *hs;
} hv;

static float hpx(float design, float s, float minpx) { return fmaxf(design * s, minpx); }

static const cw_color PLACE_COL[3] = { { 1.0f, 0.80f, 0.15f, 1 }, { 0.85f, 0.88f, 0.95f, 1 }, { 0.92f, 0.58f, 0.32f, 1 } };

static void hud_place(const hv *g) {
  float s = g->s, x = g->x + 14 * s, y = g->y + 12 * s;
  const hudv *h = g->hs;
  int p = g->k->place;
  float pop = h->place_t > 0 ? 1 + 0.55f * (1 - ease_back((float)h->place_t / 22.0f)) : 1;
  float flash = h->place_t > 0 ? clamp01(1 - (float)h->place_t / 50.0f) : 0;
  cw_color base = p >= 1 && p <= 3 ? PLACE_COL[p - 1] : CREAM;
  cw_rect r = cw_rect_make(x, y, 150 * s, 96 * s);
  cw_color fill = PANEL;
  if (flash > 0) fill = mix(PANEL, h->place_dir > 0 ? rgba(0.15f, 0.5f, 0.2f, 0.9f) : rgba(0.6f, 0.15f, 0.12f, 0.9f), flash * 0.8f);
  slab(r, 14 * s, fill, s);
  char b[16];
  snprintf(b, sizeof b, "%d", p);
  float nsz = 78 * s * pop;
  float nx = x + 22 * s;
  tx_o(U.f_title, b, nx, y + 50 * s, nsz, base, (txopt){ .align = CW_ALIGN_LEFT, .outline = nsz * 0.06f });
  float nw = tx_width(U.f_title, b, nsz);
  tx(U.f_title, place_suffix(p), nx + nw + 3 * s, y + 30 * s, 30 * s, base, CW_ALIGN_LEFT);
  snprintf(b, sizeof b, "/%d", g->S->nkarts);
  tx(U.f_small, b, x + 142 * s, y + 80 * s, hpx(20, s, 12), DIM, CW_ALIGN_RIGHT);
  /* the way it went: a little arrow for a moment */
  if (h->place_t > 0 && h->place_t < 70) {
    float a = clamp01(1 - (float)h->place_t / 70.0f);
    float ay = y + 70 * s + (h->place_dir > 0 ? -1 : 1) * (float)h->place_t * 0.12f * s;
    tri(cw_v2(x + 118 * s, ay), 9 * s, h->place_dir > 0 ? -CW_PI * 0.5f : CW_PI * 0.5f, alpha(h->place_dir > 0 ? LIME : RED, a));
  }
}

static void hud_lap(const hv *g) {
  float s = g->s, w = 190 * s, x = g->x + g->w - w - 14 * s, y = g->y + 12 * s;
  const kart *k = g->k;
  slab(cw_rect_make(x, y, w, 62 * s), 12 * s, PANEL, s);
  char b[48];
  int lap = k->lap < 1 ? 1 : k->lap;
  if (k->finished) lap = g->S->laps;
  tx(U.f_small, "LAP", x + 26 * s, y + 21 * s, hpx(20, s, 12), DIM, CW_ALIGN_LEFT);
  snprintf(b, sizeof b, "%d/%d", lap, g->S->laps);
  tx(U.f_title, b, x + w - 22 * s, y + 31 * s, 38 * s, lap == g->S->laps ? RUST : CREAM, CW_ALIGN_RIGHT);
  if (g->compact) return;
  /* the clock */
  cw_rect r2 = cw_rect_make(x, y + 68 * s, w, g->medium ? 36 * s : 62 * s);
  slab(r2, 12 * s, PANEL, s);
  float secs = k->finished ? (float)k->finish_tick / TICK_HZ : (float)g->S->race_ticks / TICK_HZ;
  fmt_time(b, sizeof b, secs);
  tx(U.f_title, b, x + w * 0.5f + 4 * s, r2.y + 19 * s, 24 * s, CREAM, CW_ALIGN_CENTER);
  if (g->medium) return;
  const hudv *h = g->hs;
  if (h->last_lap > 0 && h->last_lap_t > 0) {
    fmt_time(b, sizeof b, h->last_lap);
    char l[64];
    snprintf(l, sizeof l, "LAST %s", b);
    float a = h->last_lap_t > 360 ? clamp01((420.0f - (float)h->last_lap_t) / 60.0f) : 1;
    tx(U.f_small, l, x + w * 0.5f + 4 * s, r2.y + 46 * s, 20 * s, alpha(h->last_lap_best ? LIME : CREAM, a), CW_ALIGN_CENTER);
  } else if (k->best_lap > 0) {
    fmt_time(b, sizeof b, k->best_lap);
    char l[64];
    snprintf(l, sizeof l, "BEST %s", b);
    tx(U.f_small, l, x + w * 0.5f + 4 * s, r2.y + 46 * s, 20 * s, DIM, CW_ALIGN_CENTER);
  }
}

static void hud_item(const hv *g) {
  float s = g->s, sz = (g->compact ? 78 : 92) * s;
  float cx = g->x + g->w * 0.5f, y = g->y + 12 * s;
  const kart *k = g->k;
  const hudv *h = g->hs;
  bool roll = k->item_wait > 0;
  bool has = k->item != IT_NONE && !roll;
  float pop = h->item_t > 0 ? 1 + 0.4f * (1 - ease_back((float)h->item_t / 14.0f)) : 1;
  float wob = roll ? 0.04f * cw_sin(g->S->tick * 0.9f) : 0;
  cw_rect r = cw_rect_make(cx - sz * 0.5f * pop, y - (pop - 1) * sz * 0.2f, sz * pop, sz * pop);
  cw_color fill = has ? mix(PANEL, rgba(0.35f, 0.22f, 0.05f, 0.95f), 0.5f) : PANEL;
  rslab(r, 14 * s, fill, s);
  cw_color edge = has ? MUSTARD : roll ? TEAL : rgba(0.5f, 0.45f, 0.4f, 1);
  cw_r2d_round_rect_lines(cw_rect_make(r.x + 3 * s, r.y + 3 * s, r.w - 6 * s, r.h - 6 * s), 11 * s, 3 * s, 6, edge);
  cw_vec2 c = cw_v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f + wob * sz);
  if (roll) {
    int idx = 1 + (g->S->tick / 3) % (IT_KINDS - 1);
    float spin = (float)((g->S->tick) % 3) / 3.0f;
    cw_r2d_push_clip(r);
    item_icon(idx, cw_v2(c.x, c.y + (spin - 0.5f) * 8 * s), sz * 0.3f, g->S->tick / 60.0f);
    cw_r2d_pop_clip();
    cw_r2d_rect(cw_rect_make(r.x + 4 * s, r.y + 4 * s, r.w - 8 * s, r.h * 0.2f), rgba(1, 1, 1, 0.07f));
  } else if (has) {
    item_icon(k->item, c, sz * 0.32f * pop, g->S->tick / 60.0f);
    if (k->item == IT_TURBO3 || k->item_count > 1) {
      char b[8];
      snprintf(b, sizeof b, "x%d", k->item_count);
      tx(U.f_title, b, r.x + r.w - 8 * s, r.y + r.h - 16 * s, 22 * s, CREAM, CW_ALIGN_RIGHT);
    }
    if (!g->compact) tx_fit(U.f_small, ITEM_NAME[k->item], cx, r.y + r.h + 14 * s, hpx(18, s, 12), 150 * s, CREAM, CW_ALIGN_CENTER);
  } else {
    tx(U.f_title, "?", c.x, c.y, sz * 0.5f, rgba(0.6f, 0.55f, 0.5f, 0.45f), CW_ALIGN_CENTER);
  }
}

static void hud_speed(const hv *g) {
  float s = g->s * (g->compact ? 0.95f : 1);
  float R = 78 * s;
  float cx = g->x + g->w - 22 * g->s - R, cy = g->y + g->h - 24 * g->s - R;
  const kart *k = g->k;
  const hudv *h = g->hs;
  float frac = clamp01(h->speed_s / 62.0f);
  float a0 = 2.356f, sweep = 4.712f; /* 135 degrees clockwise from the right, over 270 */
  float glow = h->boost_glow;
  cw_r2d_circle(cw_v2(cx, cy), R + 6 * s, 40, rgba(0.05f, 0.03f, 0.04f, 0.72f));
  cw_r2d_circle_lines(cw_v2(cx, cy), R + 6 * s, 3 * s, 40, INK);
  if (glow > 0.01f) {
    cw_r2d_set_blend(CW_BLEND_ADD);
    float pulse = 0.65f + 0.35f * cw_sin(g->S->tick * 0.7f);
    cw_r2d_arc(cw_v2(cx, cy), R + 4 * s, R + 18 * s * (0.6f + glow * 0.7f), a0 - 0.1f, a0 + sweep + 0.1f, 40, rgba(1.0f, 0.5f, 0.1f, 0.35f * glow * pulse));
    cw_r2d_set_blend(CW_BLEND_ALPHA);
  }
  cw_r2d_arc(cw_v2(cx, cy), R - 18 * s, R, a0, a0 + sweep, 40, rgba(0.22f, 0.2f, 0.22f, 0.95f));
  int segs = 36;
  int fill = (int)(frac * (float)segs + 0.5f);
  for (int i = 0; i < fill; i++) {
    float t0 = (float)i / (float)segs, t1 = (float)(i + 1) / (float)segs - 0.006f;
    cw_color c = t0 < 0.5f ? mix(TEAL, MUSTARD, t0 * 2) : mix(MUSTARD, RUST, (t0 - 0.5f) * 2);
    if (glow > 0.3f) c = mix(c, rgba(1, 0.95f, 0.7f, 1), glow * 0.5f);
    cw_r2d_arc(cw_v2(cx, cy), R - 17 * s, R - 1 * s, a0 + t0 * sweep, a0 + t1 * sweep, 3, c);
  }
  for (int i = 0; i <= 6; i++) {
    float a = a0 + sweep * (float)i / 6.0f;
    cw_vec2 d = cw_v2(cw_cos(a), cw_sin(a));
    cw_r2d_line(cw_v2(cx + d.x * (R - 22 * s), cy + d.y * (R - 22 * s)), cw_v2(cx + d.x * (R - 30 * s), cy + d.y * (R - 30 * s)), 2 * s + 0.5f, alpha(CREAM, 0.55f));
  }
  char b[16];
  snprintf(b, sizeof b, "%d", (int)(h->speed_s * 3.6f + 0.5f));
  tx_o(U.f_title, b, cx, cy - 4 * s, 44 * s, glow > 0.3f ? rgba(1, 0.9f, 0.6f, 1) : CREAM, (txopt){ .align = CW_ALIGN_CENTER });
  tx(U.f_small, "KM/H", cx, cy + 28 * s, hpx(16, s, 11), DIM, CW_ALIGN_CENTER);
  (void)k;
}

static void hud_drift(const hv *g) {
  const kart *k = g->k;
  const hudv *h = g->hs;
  if (h->drift_show <= 0 && !(k->drifting)) return;
  float s = g->s, w = 300 * s, bh = 20 * s;
  float x = g->x + g->w * 0.5f - w * 0.5f, y = g->y + g->h - 54 * s;
  float a = k->drifting ? 1 : (float)h->drift_show / 20.0f;
  static const cw_color LC[3] = { { 0.30f, 0.62f, 1.0f, 1 }, { 1.0f, 0.55f, 0.15f, 1 }, { 0.82f, 0.35f, 1.0f, 1 } };
  float gap = 6 * s, segw = (w - 2 * gap) / 3;
  float flash = h->drift_flash_t > 0 ? 1 - (float)h->drift_flash_t / 22.0f : 0;
  for (int i = 0; i < 3; i++) {
    float sx = x + (float)i * (segw + gap);
    int lo = i == 0 ? 0 : DRIFT_LV[i - 1], hi = DRIFT_LV[i];
    float f = clamp01(((float)k->drift_ticks - (float)lo) / (float)(hi - lo));
    cw_rect r = cw_rect_make(sx, y, segw, bh);
    cw_rect rr = r;
    cw_r2d_round_rect(cw_rect_make(rr.x + 2 * s, rr.y + 3 * s, rr.w, rr.h), 4 * s, 3, rgba(0, 0, 0, 0.4f * a));
    cw_r2d_round_rect(rr, 4 * s, 3, rgba(0.1f, 0.08f, 0.1f, 0.85f * a));
    if (f > 0) {
      cw_color c = LC[i];
      if (k->drift_level > i) c = mix(c, CREAM, 0.25f + 0.2f * flash);
      cw_r2d_round_rect(cw_rect_make(r.x, r.y, r.w * f, r.h), 4 * s, 3, alpha(c, a));
    }
    cw_r2d_round_rect_lines(rr, 4 * s, 2.5f * s, 3, alpha(INK, a));
    if (k->drift_level > i && flash > 0) cw_r2d_round_rect_lines(cw_rect_make(r.x - 2 * s, r.y - 2 * s, r.w + 4 * s, r.h + 4 * s), 6 * s, 2.5f * s, 3, alpha(LC[i], flash * a));
  }
  if (!g->compact) {
    const char *lbl = k->drift_level >= 3 ? "MAX DRIFT" : k->drift_level == 2 ? "ORANGE" : k->drift_level == 1 ? "BLUE" : "DRIFT";
    tx(U.f_small, lbl, x + w * 0.5f, y - 11 * s, hpx(17, s, 11), alpha(k->drift_level ? LC[k->drift_level - 1] : DIM, a), CW_ALIGN_CENTER);
  }
}

static void hud_minimap(const hv *g, cw_rect r) {
  const sim *S = g->S;
  const mmap_t *m = mm_race(S);
  float s = g->s;
  rslab(r, 10 * s, rgba(0.07f, 0.06f, 0.08f, 0.8f), s);
  if (!m) return;
  float pad = 12 * s;
  mm_draw(m, r, pad, s, 3.2f * s + 0.6f, !g->compact);
  mfit f = mm_fit(m, r, pad);
  float t = S->tick / 60.0f;
  /* the CPU karts, then the other humans, then this view's own on top */
  for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i < MAXK; i++) {
      const kart *k = &S->k[i];
      if (!k->on) continue;
      bool mine = i == g->kart;
      int want = mine ? 2 : k->human ? 1 : 0;
      if (want != pass) continue;
      cw_vec2 p = mm_pt(&f, k->p.x, k->p.z);
      cw_color c = kart_col(k->colour);
      float rad = (mine ? 5.6f : k->human ? 4.8f : 3.3f) * s + 0.8f;
      if (mine) cw_r2d_circle_lines(p, rad + 3 * s + 1.5f * cw_sin(t * 6) * s, 2 * s + 0.5f, 16, CREAM);
      cw_r2d_circle(p, rad + 1.6f * s, 14, INK);
      cw_r2d_circle(p, rad, 14, c);
      if (k->human) cw_r2d_circle_lines(p, rad, 1.6f * s + 0.4f, 14, CREAM);
    }
}

static void hud_standings(const hv *g, float x, float y, int rows_max) {
  const sim *S = g->S;
  float s = g->s, rh = 25 * s, w = 196 * s;
  int n = S->nkarts;
  int mine_pos = g->k->place - 1;
  int first = 0, rows = n;
  if (rows > rows_max) {
    rows = rows_max;
    first = CW_CLAMP(mine_pos - 1, 0, n - rows);
  }
  const kart *lead = &S->k[S->order[0]];
  for (int r = 0; r < rows; r++) {
    int idx = first + r;
    int ki = S->order[idx];
    const kart *k = &S->k[ki];
    bool me = ki == g->kart;
    cw_rect rr = cw_rect_make(x, y + (float)r * rh, w, rh - 3 * s);
    cw_color bg = me ? alpha(MUSTARD, 0.95f) : rgba(0.09f, 0.07f, 0.09f, 0.72f);
    para(rr, 6 * s, bg);
    para_lines(rr, 6 * s, 1.5f * s + 0.4f, alpha(INK, 0.9f));
    cw_color ink = me ? INK : CREAM;
    char b[48];
    snprintf(b, sizeof b, "%d", idx + 1);
    tx_o(U.f_title, b, rr.x + 18 * s, rr.y + rr.h * 0.5f, 15 * s, ink, (txopt){ .align = CW_ALIGN_CENTER, .outline = me ? 0.01f : 0, .no_shadow = true });
    cw_r2d_circle(cw_v2(rr.x + 36 * s, rr.y + rr.h * 0.5f), 5.4f * s, 12, INK);
    cw_r2d_circle(cw_v2(rr.x + 36 * s, rr.y + rr.h * 0.5f), 4.2f * s, 12, kart_col(k->colour));
    if (k->human) snprintf(b, sizeof b, "P%d", (ki < MAXV ? ki : 0) + 1);
    else snprintf(b, sizeof b, "%s", CPU_NAME[ki % MAXK]);
    tx_o(U.f_hud, b, rr.x + 48 * s, rr.y + rr.h * 0.5f, 15 * s, ink, (txopt){ .align = CW_ALIGN_LEFT, .outline = me ? 0.01f : 0, .no_shadow = true });
    if (k->finished) {
      tx_o(U.f_small, "DONE", rr.x + rr.w - 12 * s, rr.y + rr.h * 0.5f, 14 * s, me ? INK : LIME, (txopt){ .align = CW_ALIGN_RIGHT, .outline = 0.01f, .no_shadow = true });
    } else if (idx > 0) {
      float gap = (lead->prog - k->prog) / fmaxf(18.0f, fabsf(k->speed));
      if (gap < 0) gap = 0;
      if (gap < 99.5f) snprintf(b, sizeof b, "+%.1f", gap);
      else snprintf(b, sizeof b, "+%d:%02d", (int)gap / 60, (int)gap % 60);
      tx_o(U.f_small, b, rr.x + rr.w - 12 * s, rr.y + rr.h * 0.5f, 14 * s, me ? INK : DIM, (txopt){ .align = CW_ALIGN_RIGHT, .outline = 0.01f, .no_shadow = true });
    }
  }
}

/* a banner sliding across the middle: text, colour, how far through (0..1), the row */
static void banner(const hv *g, const char *text, const char *sub, cw_color col, float t, float cy, float size) {
  float s = g->s;
  float in = ease_back(t / 0.14f), out = t > 0.8f ? ease_io((t - 0.8f) / 0.2f) : 0;
  float cx = g->x + g->w * 0.5f + (1 - in) * g->w * 0.6f + out * g->w * 0.6f;
  float w = fminf(g->w * 0.9f, tx_width(U.f_title, text, size * s) + 90 * s);
  float h = size * s * 1.45f + (sub ? 24 * s : 0);
  cw_rect r = cw_rect_make(cx - w * 0.5f, cy - size * s * 0.72f, w, h);
  slab(r, 16 * s, rgba(0.1f, 0.07f, 0.09f, 0.9f), s);
  cw_rect strip = cw_rect_make(r.x, r.y + r.h - 7 * s, r.w, 7 * s);
  para(strip, 0, col);
  tx_o(U.f_title, text, cx, cy, size * s, col, (txopt){ .align = CW_ALIGN_CENTER });
  if (sub) tx(U.f_hud, sub, cx, cy + size * s * 0.78f, hpx(20, s, 12), CREAM, CW_ALIGN_CENTER);
}

static void hud_countdown(const hv *g) {
  const sim *S = g->S;
  float s = g->s, cx = g->x + g->w * 0.5f, cy = g->y + g->h * 0.36f;
  if (S->phase == PH_GRID) {
    float a = clamp01((float)S->phase_ticks / 20.0f);
    tx_o(U.f_title, "GET READY", cx, cy, 70 * s, alpha(CREAM, a), (txopt){ .align = CW_ALIGN_CENTER });
    return;
  }
  if (S->phase == PH_COUNT) {
    int n = 3 - (S->phase_ticks - 1) / 60;
    if (n < 1) n = 1;
    float t = (float)((S->phase_ticks - 1) % 60) / 60.0f;
    float sc = 1 + 0.9f * (1 - ease_out(t * 3));
    float a = t > 0.8f ? 1 - (t - 0.8f) / 0.2f : 1;
    static const cw_color NC[3] = { { 1.0f, 0.25f, 0.2f, 1 }, { 1.0f, 0.62f, 0.15f, 1 }, { 1.0f, 0.88f, 0.2f, 1 } };
    cw_color c = NC[3 - n];
    char b[16];
    snprintf(b, sizeof b, "%d", n);
    /* a ring that opens as the number lands */
    cw_r2d_circle_lines(cw_v2(cx, cy), (50 + 150 * ease_out(t * 1.6f)) * s, 5 * s * (1 - t) + 1, 40, alpha(c, 0.55f * (1 - t)));
    tx_o(U.f_title, b, cx, cy, 190 * s * sc, alpha(c, a), (txopt){ .align = CW_ALIGN_CENTER, .outline = 190 * s * 0.05f });
    if (!g->compact) tx(U.f_hud, "Floor it when the lights go out", cx, cy + 128 * s, hpx(22, s, 12), alpha(CREAM, 0.9f), CW_ALIGN_CENTER);
    return;
  }
  if (S->phase == PH_RACE && S->race_ticks < 80) {
    float t = (float)S->race_ticks / 80.0f;
    float sc = 1 + 0.5f * (1 - ease_out(t * 4));
    cw_color c = LIME;
    for (int i = 0; i < 14; i++) {
      float a = (float)i / 14.0f * CW_TAU + 0.2f;
      float r0 = (90 + 40 * ease_out(t * 2)) * s, r1 = r0 + (50 + 60 * (1 - t)) * s;
      cw_r2d_line(cw_v2(cx + cw_cos(a) * r0, cy + cw_sin(a) * r0), cw_v2(cx + cw_cos(a) * r1, cy + cw_sin(a) * r1), 5 * s * (1 - t) + 1, alpha(c, 1 - t));
    }
    tx_o(U.f_title, "GO!", cx, cy, 170 * s * sc, alpha(c, t > 0.7f ? 1 - (t - 0.7f) / 0.3f : 1), (txopt){ .align = CW_ALIGN_CENTER, .outline = 170 * s * 0.05f });
  }
}

static void hud_edges(const hv *g, cw_color c, float a, float depth) {
  if (a <= 0.01f) return;
  float d = g->h * depth;
  cw_color t = alpha(c, 0);
  cw_color o = alpha(c, a);
  vgrad(cw_rect_make(g->x, g->y, g->w, d), o, t);
  vgrad(cw_rect_make(g->x, g->y + g->h - d, g->w, d), t, o);
  hgrad(cw_rect_make(g->x, g->y, d, g->h), o, t);
  hgrad(cw_rect_make(g->x + g->w - d, g->y, d, g->h), t, o);
}

static void hud_speedlines(const hv *g) {
  float inten = fx_speed_lines(g->kart);
  if (inten <= 0.02f) return;
  float s = g->s, cx = g->x + g->w * 0.5f, cy = g->y + g->h * 0.5f;
  float rx = g->w * 0.5f, ry = g->h * 0.5f;
  uint32_t seed = 7u;
  cw_r2d_push_clip(cw_rect_make(g->x, g->y, g->w, g->h));
  float t = g->S->tick * 0.017f;
  for (int i = 0; i < 34; i++) {
    seed = seed * 1664525u + 1013904223u;
    float a = (float)(seed >> 8) / 16777216.0f * CW_TAU;
    seed = seed * 1664525u + 1013904223u;
    float ph = fmodf((float)(seed >> 8) / 16777216.0f + t * (1.4f + (float)(i % 3) * 0.4f), 1.0f);
    float r0 = 0.62f + 0.3f * ph, r1 = r0 + 0.1f + 0.25f * inten;
    float ca = cw_cos(a), sa = cw_sin(a);
    cw_r2d_line(cw_v2(cx + ca * rx * r0 * 1.25f, cy + sa * ry * r0 * 1.25f), cw_v2(cx + ca * rx * r1 * 1.25f, cy + sa * ry * r1 * 1.25f), (1.2f + 2.2f * ph) * s + 0.5f,
                rgba(1, 1, 1, 0.22f * inten * (1 - ph * 0.5f)));
  }
  cw_r2d_pop_clip();
}

static void hud_wrong_way(const hv *g) {
  if (g->hs->wrong_t < 50) return;
  float s = g->s, cx = g->x + g->w * 0.5f, cy = g->y + g->h * 0.2f;
  float blink = (g->S->tick / 14) & 1 ? 1.0f : 0.65f;
  cw_rect r = cw_rect_make(cx - 170 * s, cy - 30 * s, 340 * s, 60 * s);
  slab(r, 14 * s, alpha(shade(RED, 0.8f), blink), s);
  tx_o(U.f_title, "WRONG WAY", cx, cy, 36 * s, CREAM, (txopt){ .align = CW_ALIGN_CENTER });
  /* a U-turn arrow either side */
  for (int d = -1; d <= 1; d += 2) {
    cw_vec2 c = cw_v2(cx + (float)d * 138 * s, cy);
    cw_r2d_arc(c, 8 * s, 13 * s, CW_PI, CW_TAU, 10, CREAM);
    tri(cw_v2(c.x - 10.5f * s, c.y + 6 * s), 8 * s, CW_PI * 0.5f, CREAM);
    cw_r2d_line(cw_v2(c.x + 10.5f * s, c.y), cw_v2(c.x + 10.5f * s, c.y + 14 * s), 5 * s, CREAM);
  }
}

static void hud_popups(const hv *g) {
  const hudv *h = g->hs;
  float s = g->s, cx = g->x + g->w * 0.5f;
  int shown = 0;
  for (int i = 0; i < NPOP; i++) {
    const popup *p = &h->pop[i];
    if (p->t <= 0) continue;
    float t = (float)p->t / 56.0f;
    float sc = 0.5f + 0.5f * ease_back((float)p->t / 10.0f);
    float a = t > 0.75f ? 1 - (t - 0.75f) / 0.25f : 1;
    float cy = g->y + g->h * (g->compact ? 0.5f : 0.54f) + (float)shown * 42 * s - t * 14 * s;
    cw_color c = pop_colour(p->kind);
    tx_o(U.f_title, POP_TEXT[p->kind], cx, cy, 36 * s * sc, alpha(c, a), (txopt){ .align = CW_ALIGN_CENTER });
    shown++;
  }
}

static void hud_lapflash(const hv *g) {
  const hudv *h = g->hs;
  if (h->lap_t <= 0) return;
  char b[32], sub[96] = "";
  snprintf(b, sizeof b, h->lap_final ? "FINAL LAP" : "LAP %d", h->lap_n);
  if (h->last_lap > 0) {
    char t[24];
    fmt_time(t, sizeof t, h->last_lap);
    snprintf(sub, sizeof sub, h->last_lap_best ? "%s   BEST LAP SO FAR" : "Last lap %s", t);
  }
  banner(g, b, g->compact ? NULL : sub, h->lap_final ? (((g->S->tick / 8) & 1) ? RUST : MUSTARD) : MUSTARD, (float)h->lap_t / 150.0f, g->y + g->h * 0.27f, g->compact ? 40 : 54);
}

static void hud_finish(const hv *g) {
  const kart *k = g->k;
  if (!k->finished) return;
  const hudv *h = g->hs;
  float s = g->s;
  char b[48];
  snprintf(b, sizeof b, "FINISHED %d%s", k->place, place_suffix(k->place));
  float in = ease_back((float)h->finish_t / 24.0f);
  float cx = g->x + g->w * 0.5f, cy = g->y + g->h * 0.3f;
  float w = fminf(g->w * 0.92f, 560 * s);
  cw_rect r = cw_rect_make(cx - w * 0.5f, cy - 44 * s * in, w, 88 * s * in);
  if (in < 0.05f) return;
  cw_color col = k->place >= 1 && k->place <= 3 ? PLACE_COL[k->place - 1] : CREAM;
  slab(r, 18 * s, rgba(0.1f, 0.07f, 0.09f, 0.92f), s);
  para(cw_rect_make(r.x, r.y + r.h - 8 * s, r.w, 8 * s), 0, col);
  tx_fit(U.f_title, b, cx, cy - 4 * s, 46 * s * in, w - 60 * s, col, CW_ALIGN_CENTER);
  if (!g->compact && g->S->phase == PH_RACE) tx(U.f_hud, "Waiting for the others", cx, cy + 62 * s, hpx(20, s, 12), alpha(CREAM, 0.85f), CW_ALIGN_CENTER);
}

static void hud_mapname(const hv *g) {
  const sim *S = g->S;
  if (S->phase == PH_RACE || !S->md) return;
  float s = g->s;
  float t = S->phase == PH_GRID ? (float)S->phase_ticks / 45.0f * 0.2f : 0.2f + (float)S->phase_ticks / 180.0f * 0.8f;
  float in = ease_out(t / 0.2f), out = t > 0.85f ? ease_io((t - 0.85f) / 0.15f) : 0;
  float cx = g->x + g->w * 0.5f, y = g->y + g->h * 0.7f + (1 - in) * 60 * s + out * 60 * s;
  char b[64];
  snprintf(b, sizeof b, "%d LAPS", S->laps);
  float w = fminf(g->w * 0.9f, 460 * s);
  cw_rect r = cw_rect_make(cx - w * 0.5f, y, w, 64 * s);
  float a = in * (1 - out);
  slab(r, 14 * s, rgba(0.1f, 0.07f, 0.09f, 0.9f * a), s);
  tx_fit(U.f_title, S->md->name, cx, y + 24 * s, 30 * s, w - 50 * s, alpha(MUSTARD, a), CW_ALIGN_CENTER);
  tx(U.f_hud, b, cx, y + 49 * s, hpx(17, s, 11), alpha(CREAM, a), CW_ALIGN_CENTER);
}

static cw_rect pause_button_rect(const hv *g) {
  float s = g->s;
  return cw_rect_make(g->x + g->w * 0.5f + 60 * s, g->y + 14 * s, 46 * s, 40 * s);
}

/* the shared quarter when three play: the track with everyone on it, and the order */
static void hud_shared(const hv *g, float W, float H) {
  float qx = W * 0.5f, qy = 0, qw = W * 0.5f, qh = H * 0.5f;
  hv q = *g;
  q.x = qx, q.y = qy, q.w = qw, q.h = qh;
  q.s = fminf(qw / 960.0f, qh / 540.0f) * 1.3f;
  q.kart = -1;
  cw_r2d_rect(cw_rect_make(qx, qy, qw, qh), rgba(0.06f, 0.05f, 0.08f, 1));
  vgrad(cw_rect_make(qx, qy, qw, qh), rgba(0.2f, 0.12f, 0.1f, 0.35f), rgba(0, 0, 0, 0.1f));
  cw_rect mr = cw_rect_make(qx + 10 * q.s, qy + 10 * q.s, qh - 20 * q.s, qh - 20 * q.s);
  hv m = q;
  m.compact = false;
  hud_minimap(&m, mr);
  float lx = mr.x + mr.w + 14 * q.s;
  if (lx + 196 * q.s > qx + qw) lx = qx + qw - 196 * q.s - 8;
  hud_standings(&m, lx, qy + 14 * q.s, 8);
}

static void hud_view(int v, float W, float H, int nviews) {
  const sim *S = game_sim();
  hv g = { 0 };
  g.v = v;
  g.kart = game_view_kart(v);
  game_view_rect(v, W, H, &g.x, &g.y, &g.w, &g.h);
  g.s = sqrtf(fminf(g.w / 960.0f, 3.0f) * fminf(g.h / 540.0f, 3.0f));
  if (nviews == 1) g.s = fminf(g.w / 960.0f, g.h / 540.0f) * (is_portrait(W, H) ? 1.5f : 1.0f);
  g.medium = g.s < 0.85f;
  g.compact = g.s < 0.62f;
  g.S = S;
  g.k = &S->k[g.kart];
  g.hs = &HV[v];
  g.shared_map = nviews == 3;
  const hudv *h = g.hs;
  cw_r2d_push_clip(cw_rect_make(g.x, g.y, g.w, g.h));
  hud_speedlines(&g);
  float edge_depth = 0.12f;
  if (h->hit_t > 0) hud_edges(&g, RED, 0.55f * (1 - (float)h->hit_t / 40.0f), edge_depth);
  if (h->spin_t > 0) hud_edges(&g, PINK, 0.35f * (1 - (float)h->spin_t / 50.0f), edge_depth);
  if (h->shield_t > 0) hud_edges(&g, SKY, 0.6f * (1 - (float)h->shield_t / 24.0f), edge_depth);
  if (h->fall_t > 0) hud_edges(&g, CREAM, 0.55f * (1 - (float)h->fall_t / 40.0f), 0.2f);
  if (h->boost_t > 0) hud_edges(&g, TEAL, 0.3f * (1 - (float)h->boost_t / 20.0f), 0.1f);
  if (g.k->respawn_ticks > 0 && (g.k->respawn_ticks / 5) & 1) cw_r2d_rect(cw_rect_make(g.x, g.y, g.w, g.h), rgba(1, 1, 1, 0.06f));
  hud_place(&g);
  hud_lap(&g);
  hud_item(&g);
  if (!g.compact && !g.shared_map) {
    float ms = (g.medium ? 130 : 176) * g.s;
    hud_minimap(&g, cw_rect_make(g.x + 14 * g.s, g.y + g.h - ms - 14 * g.s, ms, ms));
  } else if (g.compact && !g.shared_map && g.w > 300) {
    float ms = 118 * g.s * 1.1f;
    hud_minimap(&g, cw_rect_make(g.x + 12 * g.s, g.y + g.h - ms - 12 * g.s, ms, ms));
  }
  if (!g.medium) hud_standings(&g, g.x + 14 * g.s, g.y + 116 * g.s, 8);
  else if (!g.compact) hud_standings(&g, g.x + 14 * g.s, g.y + 114 * g.s, 4);
  hud_speed(&g);
  hud_drift(&g);
  hud_mapname(&g);
  hud_countdown(&g);
  hud_lapflash(&g);
  hud_popups(&g);
  hud_wrong_way(&g);
  hud_finish(&g);
  if (nviews == 1 && pointer_mode() && S->phase != PH_DONE) {
    cw_rect pr = pause_button_rect(&g);
    bool hot = in_rect(N.m, pr) && U.device == DEV_MOUSE;
    rslab(pr, 8 * g.s, hot ? alpha(MUSTARD, 0.95f) : PANEL, g.s);
    cw_color bar = hot ? INK : CREAM;
    cw_r2d_rect(cw_rect_make(pr.x + pr.w * 0.32f, pr.y + pr.h * 0.25f, pr.w * 0.12f, pr.h * 0.5f), bar);
    cw_r2d_rect(cw_rect_make(pr.x + pr.w * 0.56f, pr.y + pr.h * 0.25f, pr.w * 0.12f, pr.h * 0.5f), bar);
  }
  cw_r2d_pop_clip();
}

/* ── the touch overlay ──────────────────────────────────────────────── */

static cw_vec2 touch_to_canvas(float x, float y) {
  int ww, wh;
  cw_window_size(&ww, &wh);
  cw_vec2 cs = cw_r2d_canvas_size();
  return cw_v2(x * cs.x / (float)ww, y * cs.y / (float)wh);
}

typedef struct tbtn {
  int *id;
  const char *label;
  cw_color col;
  float x, y, w, h; /* in 540-high design units from the right/bottom edges: x from the right, y from the bottom */
} tbtn;
static const tbtn TB[] = {
  { &U.t_gas, "GAS", { 0.30f, 0.75f, 0.30f, 1 }, 190, 220, 170, 200 },
  { &U.t_item, "ITEM", { 0.95f, 0.6f, 0.15f, 1 }, 190, 330, 170, 100 },
  { &U.t_trick, "TRICK", { 0.85f, 0.4f, 0.9f, 1 }, 190, 440, 170, 100 },
  { &U.t_drift, "DRIFT", { 0.3f, 0.6f, 0.95f, 1 }, 370, 190, 170, 170 },
  { &U.t_brake, "BRAKE", { 0.85f, 0.25f, 0.2f, 1 }, 370, 300, 170, 100 },
};

static void touch_layout(float ww, float wh) {
  float k = wh / 540.0f;
  if (U.t_stick >= 0) cw_touch_set_rect(U.t_stick, 0, wh * 0.28f, ww * 0.42f, wh * 0.72f);
  for (unsigned i = 0; i < sizeof TB / sizeof TB[0]; i++) {
    const tbtn *b = &TB[i];
    if (*b->id >= 0) cw_touch_set_rect(*b->id, ww - b->x * k, wh - b->y * k, b->w * k, b->h * k);
  }
}

static void touch_setup(void) {
  U.t_stick = cw_touch_add_stick(0, 0, 10, 10, 70, true);
  U.t_gas = cw_touch_add_button(0, 0, 1, 1);
  U.t_item = cw_touch_add_button(0, 0, 1, 1);
  U.t_trick = cw_touch_add_button(0, 0, 1, 1);
  U.t_drift = cw_touch_add_button(0, 0, 1, 1);
  U.t_brake = cw_touch_add_button(0, 0, 1, 1);
  char b[24];
  snprintf(b, sizeof b, "touch:%d.left", U.t_stick), cw_action_bind_text(U.a_left, b);
  snprintf(b, sizeof b, "touch:%d.right", U.t_stick), cw_action_bind_text(U.a_right, b);
  snprintf(b, sizeof b, "touch:%d.up", U.t_stick), cw_action_bind_text(U.a_pup, b);
  snprintf(b, sizeof b, "touch:%d.down", U.t_stick), cw_action_bind_text(U.a_pdn, b);
  snprintf(b, sizeof b, "touch:%d", U.t_gas), cw_action_bind_text(U.a_fwd, b);
  snprintf(b, sizeof b, "touch:%d", U.t_brake), cw_action_bind_text(U.a_brake, b);
  snprintf(b, sizeof b, "touch:%d", U.t_drift), cw_action_bind_text(U.a_drift, b);
  snprintf(b, sizeof b, "touch:%d", U.t_item), cw_action_bind_text(U.a_item, b);
  snprintf(b, sizeof b, "touch:%d", U.t_trick), cw_action_bind_text(U.a_trick, b);
  cw_action_set_deadzone(U.a_left, 0.1f);
  cw_action_set_deadzone(U.a_right, 0.1f);
}

/* the touch regions only exist while the race is on and a finger is the device in use, so a menu tap is never eaten */
static void touch_tick(void) {
  bool want = U.mode == M_RACE && U.device == DEV_TOUCH && game_views() == 1;
  int ww, wh;
  cw_window_size(&ww, &wh);
  if (want) {
    touch_layout((float)ww, (float)wh);
  } else {
    if (U.t_stick >= 0) cw_touch_set_rect(U.t_stick, 0, 0, 0, 0);
    for (unsigned i = 0; i < sizeof TB / sizeof TB[0]; i++)
      if (*TB[i].id >= 0) cw_touch_set_rect(*TB[i].id, 0, 0, 0, 0);
  }
  U.touch_on = want;
}

static void touch_draw(float W, float H) {
  if (!U.touch_on) return;
  int ww, wh;
  cw_window_size(&ww, &wh);
  float k = (float)wh / 540.0f, s = H / 540.0f;
  bool on = cw_touch_control_active(U.t_stick);
  cw_vec2 c = cw_touch_stick_center(U.t_stick);
  if (!on) c = cw_v2(150 * k, (float)wh - 140 * k);
  cw_vec2 cc = touch_to_canvas(c.x, c.y);
  float r = touch_to_canvas(70 * k, 0).x;
  cw_r2d_circle(cc, r + 4, 40, rgba(0.02f, 0.02f, 0.03f, 0.35f));
  cw_r2d_circle_lines(cc, r, 3 * s, 40, rgba(1, 1, 1, on ? 0.85f : 0.45f));
  cw_vec2 v = on ? cw_touch_stick_value(U.t_stick) : cw_v2(0, 0);
  cw_r2d_circle(cw_v2(cc.x + v.x * r, cc.y + v.y * r), r * 0.42f, 28, rgba(0.95f, 0.65f, 0.25f, on ? 0.92f : 0.55f));
  if (!on) tx(U.f_small, "STEER", cc.x, cc.y + r + 16 * s, 16 * s, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER);
  for (unsigned i = 0; i < sizeof TB / sizeof TB[0]; i++) {
    const tbtn *b = &TB[i];
    if (*b->id < 0) continue;
    bool act = cw_touch_control_active(*b->id);
    cw_vec2 a = touch_to_canvas((float)ww - b->x * k, (float)wh - b->y * k), e = touch_to_canvas((float)ww - b->x * k + b->w * k, (float)wh - b->y * k + b->h * k);
    cw_rect rr = cw_rect_make(a.x + 4 * s, a.y + 4 * s, e.x - a.x - 8 * s, e.y - a.y - 8 * s);
    cw_r2d_round_rect(rr, 14 * s, 6, rgba(0.02f, 0.02f, 0.02f, act ? 0.7f : 0.42f));
    cw_r2d_round_rect(cw_rect_make(rr.x + 3 * s, rr.y + 3 * s, rr.w - 6 * s, rr.h - 6 * s), 12 * s, 6, rgba(b->col.r, b->col.g, b->col.b, act ? 0.88f : 0.4f));
    tx(U.f_title, b->label, rr.x + rr.w * 0.5f, rr.y + rr.h * 0.5f, fminf(rr.h * 0.3f, 30 * s), rgba(1, 1, 1, act ? 1 : 0.9f), CW_ALIGN_CENTER);
  }
}

static void hud_draw(float W, float H) {
  if (!game_loaded() || (U.mode != M_RACE && U.mode != M_PAUSE)) return;
  int nv = game_views();
  for (int v = 0; v < nv; v++) hud_view(v, W, H, nv);
  if (nv == 3) {
    hv g = { 0 };
    g.S = game_sim();
    g.k = &g.S->k[game_view_kart(0)];
    g.kart = -1;
    g.s = 1;
    hud_shared(&g, W, H);
  }
  touch_draw(W, H);
}
