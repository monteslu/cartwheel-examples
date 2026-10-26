/* ui_modes.inc: the time trial, the cups and the assist hints (included by ui.c after ui_screens.inc, never built on its own).
 *
 * TIME TRIAL  one kart on a lap from the grid, no boxes, the best run kept as a ghost (ghost.c) in the save data. The
 *             item button restarts the run at once (game_retry: no menu, no reload). Medals by the run's time against a
 *             reference run per map: bronze, silver, gold, and AUTHOR for matching the reference itself, a CPU driver's
 *             lap measured with tools/trial_ref.sh (the maps' own par_lap is a flat 60 s, no use for this).
 * CUP         four races in a row (CUPS in ui.c), points from the race results (POINTS), standings between the races,
 *             a trophy by the final place (1st gold, 2nd silver, 3rd bronze) and the next cup opening on a trophy. Any
 *             number of humans, each scored as a kart of their own. */

/* ── medals ─────────────────────────────────────────────────────────── */

enum { MEDAL_NONE, MEDAL_BRONZE, MEDAL_SILVER, MEDAL_GOLD, MEDAL_AUTHOR, MEDAL_KINDS };
static const char *const MEDAL_NAME[MEDAL_KINDS] = { "NO MEDAL", "BRONZE", "SILVER", "GOLD", "AUTHOR" };

/* the reference lap of each map from the grid at the standard class, in centiseconds (tools/trial_ref.sh prints these) */
static const uint32_t TRIAL_REF_CS[NMAPS] = { 7913, 8590, 8717, 8222, 8438, 9168, 8185, 8237 };
/* a medal's time as a share of the reference: you earn the medal by being at least that quick */
static const int MEDAL_PCT[MEDAL_KINDS] = { 0, 135, 117, 105, 100 };

static uint32_t medal_time(int map, int tier) {
  if (map < 0 || map >= NMAPS || tier < MEDAL_BRONZE || tier > MEDAL_AUTHOR) return 0;
  return (TRIAL_REF_CS[map] * (uint32_t)MEDAL_PCT[tier] + 50) / 100;
}
static int trial_medal_of(int map, uint32_t cs) {
  if (cs == 0 || map < 0 || map >= NMAPS) return MEDAL_NONE;
  for (int t = MEDAL_AUTHOR; t >= MEDAL_BRONZE; t--)
    if (cs <= medal_time(map, t)) return t;
  return MEDAL_NONE;
}
static cw_color medal_col(int tier) {
  switch (tier) {
  case MEDAL_BRONZE: return rgba(0.82f, 0.50f, 0.26f, 1);
  case MEDAL_SILVER: return rgba(0.84f, 0.87f, 0.94f, 1);
  case MEDAL_GOLD: return rgba(1.0f, 0.80f, 0.15f, 1);
  default: return rgba(0.30f, 0.92f, 0.82f, 1);
  }
}

/* a medal on a ribbon: r the disc's radius, centre c; the author medal is a faceted gem that glints */
static void draw_medal(cw_vec2 c, float r, int tier, float s, float t) {
  cw_color col = medal_col(tier);
  /* the ribbon */
  cw_vec2 rl[4] = { cw_v2(c.x - r * 0.75f, c.y - r * 1.9f), cw_v2(c.x - r * 0.05f, c.y - r * 1.9f), cw_v2(c.x + r * 0.45f, c.y - r * 0.2f), cw_v2(c.x - r * 0.35f, c.y - r * 0.2f) };
  cw_vec2 rr[4] = { cw_v2(c.x + r * 0.75f, c.y - r * 1.9f), cw_v2(c.x + r * 0.05f, c.y - r * 1.9f), cw_v2(c.x - r * 0.45f, c.y - r * 0.2f), cw_v2(c.x + r * 0.35f, c.y - r * 0.2f) };
  cw_color rib = tier == MEDAL_AUTHOR ? rgba(0.15f, 0.55f, 0.55f, 1) : rgba(0.80f, 0.20f, 0.20f, 1);
  cw_r2d_polygon(rl, 4, rib);
  cw_r2d_polygon(rr, 4, shade(rib, 0.8f));
  cw_r2d_polygon_lines(rl, 4, 1.4f * s + 0.4f, INK);
  cw_r2d_polygon_lines(rr, 4, 1.4f * s + 0.4f, INK);
  if (tier == MEDAL_AUTHOR) {
    /* a gem: an octagon with a bright table and a travelling glint */
    cw_vec2 o[8];
    for (int i = 0; i < 8; i++) o[i] = cw_v2(c.x + cw_cos((float)i * CW_TAU / 8 + 0.3927f) * r * 1.12f, c.y + cw_sin((float)i * CW_TAU / 8 + 0.3927f) * r * 1.12f);
    cw_r2d_polygon(o, 8, shade(col, 0.7f));
    cw_vec2 in[8];
    for (int i = 0; i < 8; i++) in[i] = cw_v2(c.x + cw_cos((float)i * CW_TAU / 8 + 0.3927f) * r * 0.62f, c.y + cw_sin((float)i * CW_TAU / 8 + 0.3927f) * r * 0.62f);
    cw_r2d_polygon(in, 8, mix(col, rgba(1, 1, 1, 1), 0.35f));
    for (int i = 0; i < 8; i++) cw_r2d_line(o[i], in[i], 1.2f * s + 0.3f, alpha(INK, 0.6f));
    cw_r2d_polygon_lines(in, 8, 1.2f * s + 0.3f, alpha(INK, 0.6f));
    cw_r2d_polygon_lines(o, 8, 2.0f * s + 0.5f, INK);
    float g = fmodf(t * 0.7f, 2.2f);
    if (g < 1) cw_r2d_circle(cw_v2(c.x - r * 0.9f + g * r * 1.8f, c.y - r * 0.55f + g * r * 0.9f), r * 0.16f, 8, rgba(1, 1, 1, 0.9f * (1 - fabsf(g - 0.5f) * 1.6f)));
    return;
  }
  cw_r2d_circle(c, r + 1.8f * s, 28, INK);
  cw_r2d_circle(c, r, 28, shade(col, 0.82f));
  cw_r2d_circle(c, r * 0.8f, 28, col);
  cw_r2d_circle_lines(c, r * 0.8f, 1.2f * s + 0.3f, 28, alpha(INK, 0.45f));
  /* a star in the middle, and a glint on the rim */
  cw_vec2 st[10];
  for (int i = 0; i < 10; i++) {
    float a = -CW_PI * 0.5f + (float)i * CW_PI / 5.0f, rad = (i & 1 ? 0.26f : 0.55f) * r;
    st[i] = cw_v2(c.x + cw_cos(a) * rad, c.y + cw_sin(a) * rad);
  }
  cw_r2d_fan(c, st, 10, true, mix(col, rgba(1, 1, 1, 1), 0.5f));
  cw_r2d_polygon_lines(st, 10, 1.0f * s + 0.3f, alpha(INK, 0.5f));
  cw_r2d_arc(c, r * 0.86f, r * 0.96f, 3.6f, 4.5f, 8, rgba(1, 1, 1, 0.55f));
}

/* ── the save's ghosts and the trial's setup ───────────────────────── */

const ghost_saved *ui_trial_ghost(int map) {
  if (map < 0 || map >= NMAPS) return NULL;
  return SAVE.ghost_on && SAVE.ghost[map].time_cs ? &SAVE.ghost[map] : NULL;
}
static const ghost_saved *trial_ghost_for(int map) { return ui_trial_ghost(map); }
void ui_set_auto_runs(int n) { U.auto_runs = n; }

/* the run goes again at once: no fade to black, no reload, a short countdown */
static void trial_retry(void) {
  game_retry();
  hud_reset();
  U.saved_this_race = false;
  goto_mode(M_RACE);
  U.tr.on = true;
  U.fade = 0.55f;
  U.fade_dir = -1;
}

static void trial_results_build(void) {
  const sim *S = game_sim();
  const kart *k = &S->k[0];
  int map = S->map;
  struct trial_state *tr = &U.tr;
  tr->runs++;
  float secs = k->finished ? (float)k->finish_tick / TICK_HZ : 0;
  uint32_t cs = k->finished ? (uint32_t)((uint64_t)k->finish_tick * 100u + TICK_HZ / 2) / TICK_HZ : 0;
  uint32_t prev = map >= 0 && map < NMAPS ? SAVE.trial_cs[map] : 0;
  (void)secs;
  tr->cs = cs;
  tr->prev_best_cs = prev;
  tr->record = cs > 0 && (prev == 0 || cs < prev);
  tr->medal = trial_medal_of(map, cs);
  tr->prev_medal = trial_medal_of(map, prev);
  tr->new_medal = tr->medal > tr->prev_medal && tr->medal > 0;
  tr->had_ghost = ghost_loaded();
  tr->ghost_cs = tr->had_ghost ? ghost_time_cs() : 0;
  tr->gap_cs = tr->had_ghost && cs ? (int)cs - (int)tr->ghost_cs : 0;
  if (tr->record && map >= 0 && map < NMAPS) {
    SAVE.trial_cs[map] = cs;
    /* the run behind it; one too long for the ghost (two minutes) keeps its time without one */
    if (!game_commit_ghost(&SAVE.ghost[map], cs)) memset(&SAVE.ghost[map], 0, sizeof SAVE.ghost[map]);
    game_set_ghost(ui_trial_ghost(map));
  }
  tr->best_cs = map >= 0 && map < NMAPS ? SAVE.trial_cs[map] : 0;
  if (tr->record) save_write();
  goto_mode(M_RESULTS);
  U.res_ticks = 0;
  U.sel = 0;
  if (tr->record || tr->new_medal) audio_ui(AUI_GO);
  if (tr->record) audio_announce(ANN_RECORD);
  if (tr->new_medal) audio_announce(ANN_MEDAL_BRONZE + tr->medal - MEDAL_BRONZE);
}

static void tick_trial_results(float W, float H) {
  (void)W, (void)H;
  int hx = (any_held(U.m_right) ? 1 : 0) - (any_held(U.m_left) ? 1 : 0);
  int dx, dy;
  nav_step(&U.nav, hx, 0, &dx, &dy);
  if (dx) U.sel = (U.sel + dx + 3) % 3, sfx_move();
  if (is_portrait(W, H)) {
    int hy = (any_held(U.m_down) ? 1 : 0) - (any_held(U.m_up) ? 1 : 0);
    int ddx, ddy;
    nav_step(&U.pnav[0], 0, hy, &ddx, &ddy);
    if (ddy) U.sel = (U.sel + ddy + 3) % 3, sfx_move();
  }
  int act = -1;
  if (pointer_mode() && N.moved)
    for (int i = 0; i < 3; i++)
      if (in_rect(N.m, res_button(i, W, H)) && U.sel != i) U.sel = i, sfx_move();
  if (N.click)
    for (int i = 0; i < 3; i++)
      if (in_rect(N.m, res_button(i, W, H))) U.sel = i, act = i;
  if (N.ok && U.res_ticks > 24) act = U.sel;
  if (cw_action_just_pressed(0, U.a_item) && U.res_ticks > 14) act = 0;
  if (U.auto_runs > 0 && U.res_ticks > 70) {
    U.auto_runs--;
    act = 0;
  }
  if (U.res_ticks < 14) return;
  if (act == 0) {
    sfx_ok();
    trial_retry();
  } else if (act == 1) sfx_ok(), begin_pending(PD_NEXT_MAP);
  else if (act == 2) sfx_ok(), begin_pending(PD_TO_MENU);
}

static void fmt_gap(char *b, size_t n, int cs) {
  int a = cs < 0 ? -cs : cs;
  snprintf(b, n, "%c%d.%02d", cs < 0 ? '-' : '+', a / 100, a % 100);
}

static void draw_trial_results(float W, float H, float s) {
  const struct trial_state *tr = &U.tr;
  int t = U.res_ticks;
  float head = ease_back((float)t / 16.0f), panel = ease_out(((float)t - 6) / 16.0f);
  const char *title = tr->record ? "NEW RECORD!" : "TRIAL DONE";
  cw_color tcol = tr->record ? LIME : MUSTARD;
  {
    float bw = fminf(W * 0.94f, tx_width(U.f_title, title, 46 * s) + 110 * s);
    cw_rect r = cw_rect_make(W * 0.5f - bw * 0.5f, 22 * s - (1 - head) * 120 * s, bw, 72 * s);
    slab(r, 18 * s, rgba(0.1f, 0.07f, 0.09f, 0.94f), s);
    para(cw_rect_make(r.x + 6 * s, r.y + r.h - 9 * s, r.w - 12 * s, 6 * s), 0, tcol);
    tx_fit(U.f_title, title, r.x + r.w * 0.5f + 8 * s, r.y + 33 * s, 46 * s, r.w - 90 * s, tcol, CW_ALIGN_CENTER);
  }
  if (panel <= 0) return;
  float pw = fminf(W * 0.94f, 600 * s), ph = 296 * s;
  cw_rect p = cw_rect_make(W * 0.5f - pw * 0.5f, 112 * s + (1 - panel) * 160 * s, pw, ph);
  slab(p, 0, rgba(0.09f, 0.065f, 0.08f, 0.93f * panel), s);
  const map_def *md = MAPS[U.tr.map >= 0 && U.tr.map < NMAPS ? U.tr.map : 0];
  tx_fit(U.f_hud, md->name, p.x + 24 * s, p.y + 22 * s, 18 * s, pw * 0.6f, alpha(DIM, panel), CW_ALIGN_LEFT);
  char b[48], b2[48];
  fmt_time(b, sizeof b, (float)tr->cs / 100.0f);
  tx_o(U.f_title, b, p.x + pw * 0.27f, p.y + 76 * s, 58 * s, alpha(CREAM, panel), (txopt){ .align = CW_ALIGN_CENTER });
  /* the numbers: best, the ghost, the gap */
  float lx = p.x + 28 * s, vx = p.x + pw * 0.5f - 18 * s, ry = p.y + 128 * s, rg = 30 * s;
  tx(U.f_small, "BEST", lx, ry, 17 * s, DIM, CW_ALIGN_LEFT);
  fmt_time(b, sizeof b, (float)tr->best_cs / 100.0f);
  tx(U.f_hud, b, vx, ry, 22 * s, tr->record ? LIME : CREAM, CW_ALIGN_RIGHT);
  tx(U.f_small, "GHOST", lx, ry + rg, 17 * s, DIM, CW_ALIGN_LEFT);
  if (tr->had_ghost) {
    fmt_time(b, sizeof b, (float)tr->ghost_cs / 100.0f);
    tx(U.f_hud, b, vx, ry + rg, 22 * s, CREAM, CW_ALIGN_RIGHT);
    fmt_gap(b2, sizeof b2, tr->gap_cs);
    tx(U.f_small, "GAP", lx, ry + 2 * rg, 17 * s, DIM, CW_ALIGN_LEFT);
    tx_o(U.f_title, b2, vx, ry + 2 * rg, 26 * s, tr->gap_cs <= 0 ? LIME : RED, (txopt){ .align = CW_ALIGN_RIGHT });
  } else {
    tx(U.f_small, "none to race", vx, ry + rg, 17 * s, DIM, CW_ALIGN_RIGHT);
    tx(U.f_small, SAVE.ghost_on ? "your first run is the ghost" : "ghost is off in the setup", lx, ry + 2 * rg, 15 * s, alpha(DIM, 0.9f), CW_ALIGN_LEFT);
  }
  /* the medal */
  cw_vec2 mc = cw_v2(p.x + pw * 0.78f, p.y + 112 * s);
  float pop = ease_back(((float)t - 22) / 18.0f);
  if (tr->medal > 0) {
    draw_medal(cw_v2(mc.x, mc.y + 8 * s), 40 * s * pop, tr->medal, s, U.time);
    tx_o(U.f_title, MEDAL_NAME[tr->medal], mc.x, mc.y + 74 * s, 22 * s, medal_col(tr->medal), (txopt){ .align = CW_ALIGN_CENTER });
    if (tr->new_medal) tx(U.f_hud, "NEW MEDAL", mc.x, mc.y + 98 * s, 16 * s, alpha(LIME, 0.7f + 0.3f * cw_sin(U.time * 7)), CW_ALIGN_CENTER);
  } else {
    cw_r2d_circle_lines(cw_v2(mc.x, mc.y + 8 * s), 34 * s, 3 * s, 28, alpha(DIM, 0.5f));
    tx(U.f_small, "no medal yet", mc.x, mc.y + 66 * s, 17 * s, DIM, CW_ALIGN_CENTER);
  }
  /* the four targets */
  float ty = p.y + ph - 30 * s, cwid = (pw - 36 * s) / 4;
  cw_r2d_rect(cw_rect_make(p.x + 14 * s, ty - 26 * s, pw - 28 * s, 1.5f * s), alpha(CREAM, 0.15f));
  for (int k2 = 0; k2 < 4; k2++) {
    int tier = MEDAL_BRONZE + k2;
    float cx = p.x + 18 * s + (float)k2 * cwid;
    bool got = tr->cs > 0 && tr->cs <= medal_time(U.tr.map, tier);
    cw_color col = got ? CREAM : alpha(DIM, 0.8f);
    if (got) draw_medal(cw_v2(cx + 10 * s, ty + 4 * s), 8 * s, tier, s, U.time);
    else cw_r2d_circle_lines(cw_v2(cx + 10 * s, ty + 2 * s), 7 * s, 1.6f * s, 14, alpha(DIM, 0.5f));
    fmt_time(b, sizeof b, (float)medal_time(U.tr.map, tier) / 100.0f);
    tx(U.f_small, b, cx + 26 * s, ty + 1 * s, 15 * s, col, CW_ALIGN_LEFT);
  }
  /* the buttons */
  static const char *const BL[3] = { "RETRY", "NEXT MAP", "MENU" };
  for (int i = 0; i < 3; i++) draw_button(res_button(i, W, H), BL[i], U.sel == i && t > 14, true, s * 0.8f, clamp01(((float)t - 14 - (float)i * 4) / 12.0f));
  tx(U.f_small, dev_hint("(X) also retries", "E also retries", "", "ITEM also retries"), W * 0.5f, H - 11 * s, 14 * s, alpha(DIM, 0.85f), CW_ALIGN_CENTER);
}

/* ── the trial's HUD panels ─────────────────────────────────────────── */

static void hud_trial(const hv *g) {
  const sim *S = g->S;
  const kart *k = g->k;
  float s = g->s;
  int map = S->map;
  bool racing = S->phase == PH_RACE && !k->finished;
  int clock_cs = S->phase == PH_RACE || k->finished ? (int)((uint64_t)(k->finished ? k->finish_tick : S->race_ticks) * 100u / TICK_HZ) : 0;
  /* the medal targets, top left: the ones already out of reach go dim */
  {
    float x = g->x + 14 * s, y = g->y + 12 * s, w = 158 * s, rh = 22 * s;
    float h = g->compact ? 4 * rh + 8 * s : 4 * rh + 30 * s;
    slab(cw_rect_make(x, y, w, h), 14 * s, PANEL, s);
    float y0 = y + 14 * s;
    if (!g->compact) tx(U.f_small, "TIME TRIAL", x + 18 * s, y0, hpx(15, s, 11), DIM, CW_ALIGN_LEFT), y0 += 18 * s;
    int aim = 0; /* the best medal still in reach */
    for (int tier = MEDAL_AUTHOR; tier >= MEDAL_BRONZE && !aim; tier--)
      if (clock_cs <= (int)medal_time(map, tier)) aim = tier;
    if (k->finished) aim = 0;
    for (int tier = MEDAL_AUTHOR, row = 0; tier >= MEDAL_BRONZE; tier--, row++) {
      float cy = y0 + (float)row * rh + 4 * s;
      bool out = (int)medal_time(map, tier) < clock_cs, mine = tier == aim;
      char b[24];
      fmt_time(b, sizeof b, (float)medal_time(map, tier) / 100.0f);
      if (mine) cw_r2d_rect(cw_rect_make(x + 6 * s, cy - rh * 0.5f + 1 * s, w - 12 * s, rh - 2 * s), alpha(MUSTARD, 0.18f));
      if (out) cw_r2d_circle_lines(cw_v2(x + 26 * s, cy), 6 * s, 1.5f * s, 12, alpha(DIM, 0.45f));
      else draw_medal(cw_v2(x + 26 * s, cy + 2 * s), 6.5f * s, tier, s, U.time);
      tx(U.f_hud, b, x + w - 16 * s, cy, hpx(16, s, 11), out ? alpha(DIM, 0.55f) : mine ? MUSTARD : CREAM, CW_ALIGN_RIGHT);
    }
  }
  /* the ghost's gap, top centre */
  {
    float cx = g->x + g->w * 0.5f, y = g->y + 12 * s, w = 190 * s, h = 62 * s;
    cw_rect r = cw_rect_make(cx - w * 0.5f, y, w, h);
    float gap_s;
    bool have = ghost_loaded() && racing && ghost_gap(k->prog, S->race_ticks, &gap_s);
    cw_color edge = rgba(0.5f, 0.45f, 0.4f, 1);
    if (have) edge = gap_s <= 0 ? LIME : RED;
    rslab(r, 14 * s, PANEL, s);
    cw_r2d_round_rect_lines(cw_rect_make(r.x + 3 * s, r.y + 3 * s, r.w - 6 * s, r.h - 6 * s), 11 * s, 3 * s, 6, alpha(edge, have ? 0.9f : 0.5f));
    char b[32];
    if (have) {
      int cs = (int)(gap_s * 100.0f + (gap_s < 0 ? -0.5f : 0.5f));
      fmt_gap(b, sizeof b, cs);
      tx(U.f_small, "GHOST", cx, y + 15 * s, hpx(15, s, 11), DIM, CW_ALIGN_CENTER);
      tx_o(U.f_title, b, cx, y + 40 * s, 32 * s, gap_s <= 0 ? LIME : RED, (txopt){ .align = CW_ALIGN_CENTER });
    } else if (ghost_loaded()) {
      tx(U.f_small, "GHOST", cx, y + 17 * s, hpx(15, s, 11), DIM, CW_ALIGN_CENTER);
      tx(U.f_hud, k->finished ? "FINISHED" : "WAITING FOR THE LIGHTS", cx, y + 42 * s, hpx(k->finished ? 22 : 15, s, 11), CREAM, CW_ALIGN_CENTER);
    } else if (SAVE.trial_cs[map]) {
      tx(U.f_small, "BEST", cx, y + 17 * s, hpx(15, s, 11), DIM, CW_ALIGN_CENTER);
      fmt_time(b, sizeof b, (float)SAVE.trial_cs[map] / 100.0f);
      tx(U.f_hud, b, cx, y + 42 * s, 26 * s, CREAM, CW_ALIGN_CENTER);
    } else {
      tx(U.f_small, "NO GHOST YET", cx, y + 22 * s, hpx(16, s, 11), DIM, CW_ALIGN_CENTER);
      tx(U.f_small, "set a time to make one", cx, y + 44 * s, hpx(14, s, 10), alpha(DIM, 0.85f), CW_ALIGN_CENTER);
    }
    /* the retry button, in reach of a thumb */
    tx(U.f_small, dev_hint("(X) RETRY", "E: RETRY", "", "ITEM: RETRY"), cx, y + h + 14 * s, hpx(15, s, 11), alpha(CREAM, 0.7f), CW_ALIGN_CENTER);
  }
}

/* the map select's detail panel for a trial: the best time and the medal targets */
static void trial_detail(float x, float y, float ybot, float w, int map, float s) {
  char b[48], t[24];
  uint32_t best = SAVE.trial_cs[map];
  int md = trial_medal_of(map, best);
  if (best) {
    fmt_time(t, sizeof t, (float)best / 100.0f);
    snprintf(b, sizeof b, "Best  %s", t);
    tx(U.f_hud, b, x, y, 19 * s, LIME, CW_ALIGN_LEFT);
    if (md > 0) draw_medal(cw_v2(x + w - 16 * s, y + 6 * s), 11 * s, md, s, U.time);
  } else tx(U.f_small, "No time yet", x, y, 18 * s, DIM, CW_ALIGN_LEFT);
  y += 26 * s;
  if (y > ybot - 6 * s) return;
  float cwid = w / 4;
  for (int k = 0; k < 4; k++) {
    int tier = MEDAL_BRONZE + k;
    float cx = x + (float)k * cwid;
    bool got = best && best <= medal_time(map, tier);
    if (got) draw_medal(cw_v2(cx + 7 * s, y + 3 * s), 6 * s, tier, s, U.time);
    else cw_r2d_circle_lines(cw_v2(cx + 7 * s, y + 1 * s), 5.5f * s, 1.5f * s, 12, alpha(DIM, 0.5f));
    fmt_time(t, sizeof t, (float)medal_time(map, tier) / 100.0f);
    tx(U.f_small, t, cx + 18 * s, y, 14 * s, got ? CREAM : alpha(DIM, 0.85f), CW_ALIGN_LEFT);
  }
  (void)ybot;
}

/* "new here? try this": a small tag on the assists row while the player is new and has them off */
static void assist_tag(cw_rect row, float s) {
  float a = 0.75f + 0.25f * cw_sin(U.time * 5);
  cw_rect r = cw_rect_make(row.x + row.w * 0.27f, row.y + row.h * 0.5f - 9 * s, 64 * s, 18 * s);
  para(r, 6 * s, alpha(TEAL, a));
  para_lines(r, 6 * s, 1.4f * s, alpha(INK, a));
  tx_o(U.f_small, "TRY ME", r.x + r.w * 0.5f + 3 * s, r.y + r.h * 0.5f, 11.5f * s, alpha(INK, a), (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
}

/* ── cups ───────────────────────────────────────────────────────────── */

static int cup_nraces(void) { return U.cup.nraces > 0 && U.cup.nraces < CUP_RACES ? U.cup.nraces : CUP_RACES; }
static bool cup_unlocked(int cup) { return cup <= 0 || (cup < NCUPS && SAVE.cup_best[cup - 1] > 0); }

static cw_color trophy_col(int tier) {
  switch (tier) {
  case 3: return rgba(1.0f, 0.80f, 0.15f, 1);
  case 2: return rgba(0.84f, 0.87f, 0.94f, 1);
  case 1: return rgba(0.82f, 0.50f, 0.26f, 1);
  default: return rgba(0.34f, 0.32f, 0.34f, 1);
  }
}

/* polished metal across a round body: how bright a column is from its left edge (0) to its right (1). A dark rim each side, a hot
 * highlight left of centre, the shadow side and a thin reflected light at the right edge, the way a lathed cup catches a room */
static float metal_k(float s) {
  static const float X[9] = { 0.0f, 0.08f, 0.2f, 0.3f, 0.42f, 0.6f, 0.78f, 0.9f, 1.0f };
  static const float K[9] = { 0.32f, 0.62f, 1.05f, 1.75f, 1.05f, 0.62f, 0.48f, 0.92f, 0.36f };
  if (s <= 0) return K[0];
  for (int i = 1; i < 9; i++)
    if (s <= X[i]) {
      float f = (s - X[i - 1]) / (X[i] - X[i - 1]);
      return K[i - 1] + (K[i] - K[i - 1]) * f;
    }
  return K[8];
}
static cw_color metal_col(cw_color c, float k) {
  /* under 1 the metal's own colour darkens; over 1 it goes toward white (the highlight) */
  return k <= 1 ? shade(c, k) : mix(c, rgba(1, 1, 1, 1), cw_clampf((k - 1) * 0.85f, 0, 0.9f));
}
/* a body of revolution seen from the side: rows of (height, half width) from top to bottom, shaded across like metal, and a glint
 * sweeping over it with time */
static void metal_body(float cx, const float *ys, const float *hw, int rows, cw_color c, float glint) {
  enum { COLS = 16 };
  cw_vertex2d v[12 * (COLS + 1)];
  uint32_t idx[11 * COLS * 6];
  if (rows > 12) rows = 12;
  int nv = 0, ni = 0;
  for (int r = 0; r < rows; r++)
    for (int k = 0; k <= COLS; k++) {
      float s = (float)k / COLS, x = cx + (s * 2 - 1) * hw[r];
      float g = 1.0f - cw_clampf(fabsf(s + (float)r * 0.05f - glint) / 0.07f, 0, 1);
      cw_color col = metal_col(c, metal_k(s) + 0.9f * g);
      v[nv++] = (cw_vertex2d){ cw_v2(x, ys[r]), cw_v2(0, 0), col };
    }
  for (int r = 0; r + 1 < rows; r++)
    for (int k = 0; k < COLS; k++) {
      uint32_t i0 = (uint32_t)(r * (COLS + 1) + k), i1 = i0 + 1, i2 = i0 + COLS + 1, i3 = i2 + 1;
      idx[ni++] = i0, idx[ni++] = i1, idx[ni++] = i3, idx[ni++] = i0, idx[ni++] = i3, idx[ni++] = i2;
    }
  cw_r2d_triangles((cw_texture){ 0 }, v, nv, idx, ni);
}
static void metal_outline(float cx, const float *ys, const float *hw, int rows, float w) {
  cw_vec2 p[24];
  int n = 0;
  for (int r = 0; r < rows; r++) p[n++] = cw_v2(cx + hw[r], ys[r]);
  for (int r = rows - 1; r >= 0; r--) p[n++] = cw_v2(cx - hw[r], ys[r]);
  cw_r2d_polygon_lines(p, n, w, INK);
}

/* a trophy standing on gy, cx its middle, u half its height */
static void draw_trophy(float cx, float gy, float u, int tier, float t) {
  cw_color c = trophy_col(tier);
  /* the glint crosses every few seconds */
  float ph = fmodf(t * 0.45f, 1.0f), glint = -0.3f + ph * 2.2f;
  /* handles first, behind the bowl: a dark core, the metal, then a bright line along the lit side */
  for (int sd = -1; sd <= 1; sd += 2) {
    cw_vec2 h[9], hl[9];
    for (int i = 0; i < 9; i++) {
      float f = (float)i / 8.0f, bulge = cw_sin(f * CW_PI) * u * 0.42f;
      float x = u * (0.70f + (0.40f - 0.70f) * f) + bulge, y = gy - u * (1.74f + (1.12f - 1.74f) * f);
      h[i] = cw_v2(cx + (float)sd * x, y);
      hl[i] = cw_v2(cx + (float)sd * (x - u * 0.03f), y - u * 0.035f);
    }
    cw_r2d_polyline(h, 9, u * 0.15f + 4, false, INK);
    cw_r2d_polyline(h, 9, u * 0.15f, false, shade(c, 0.72f));
    cw_r2d_polyline(hl, 9, u * 0.045f, false, metal_col(c, sd < 0 ? 1.6f : 1.15f));
  }
  /* base: two plinth steps */
  {
    float y1[2] = { gy - u * 0.2f, gy }, w1[2] = { u * 0.62f, u * 0.62f };
    float y2[2] = { gy - u * 0.4f, gy - u * 0.2f }, w2[2] = { u * 0.44f, u * 0.44f };
    metal_outline(cx, y1, w1, 2, 4);
    metal_body(cx, y1, w1, 2, shade(c, 0.8f), glint);
    metal_outline(cx, y2, w2, 2, 4);
    metal_body(cx, y2, w2, 2, c, glint);
  }
  /* stem with a collar */
  {
    float ys[4] = { gy - u * 0.78f, gy - u * 0.7f, gy - u * 0.56f, gy - u * 0.4f }, hw[4] = { u * 0.16f, u * 0.08f, u * 0.08f, u * 0.13f };
    metal_outline(cx, ys, hw, 4, 3.5f);
    metal_body(cx, ys, hw, 4, c, glint);
  }
  /* the bowl, lathed: flared lip, round belly, the neck */
  float bys[10] = { gy - u * 1.9f, gy - u * 1.78f, gy - u * 1.6f, gy - u * 1.42f, gy - u * 1.26f, gy - u * 1.1f, gy - u * 0.98f, gy - u * 0.88f, gy - u * 0.8f, gy - u * 0.76f };
  float bhw[10] = { u * 0.8f, u * 0.77f, u * 0.74f, u * 0.68f, u * 0.58f, u * 0.44f, u * 0.3f, u * 0.19f, u * 0.13f, u * 0.11f };
  metal_outline(cx, bys, bhw, 10, 5);
  metal_body(cx, bys, bhw, 10, c, glint);
  /* the lip: a bright band, its inside dark (the cup is hollow) */
  {
    float ly[2] = { gy - u * 1.98f, gy - u * 1.86f }, lw[2] = { u * 0.84f, u * 0.84f };
    metal_outline(cx, ly, lw, 2, 3.5f);
    metal_body(cx, ly, lw, 2, mix(c, rgba(1, 1, 1, 1), 0.15f), glint);
    cw_r2d_rect(cw_rect_make(cx - u * 0.66f, gy - u * 1.98f, u * 1.32f, u * 0.045f), alpha(shade(c, 0.35f), 0.9f));
  }
  /* a star on the bowl, a sparkle that pops on the highlight */
  if (tier > 0) {
    cw_vec2 sc = cw_v2(cx, gy - u * 1.46f), sp[10];
    float pulse = 1 + 0.06f * cw_sin(t * 6);
    for (int i = 0; i < 10; i++) {
      float a = -CW_PI * 0.5f + (float)i * CW_PI / 5.0f, rr = (i & 1 ? 0.13f : 0.3f) * u * pulse;
      sp[i] = cw_v2(sc.x + cw_cos(a) * rr, sc.y + cw_sin(a) * rr);
    }
    cw_r2d_fan(sc, sp, 10, true, shade(c, 0.55f));
    cw_vec2 sp2[10];
    for (int i = 0; i < 10; i++) sp2[i] = cw_v2(sc.x + (sp[i].x - sc.x) * 0.8f - u * 0.015f, sc.y + (sp[i].y - sc.y) * 0.8f - u * 0.015f);
    cw_r2d_fan(sc, sp2, 10, true, mix(c, rgba(1, 1, 1, 1), 0.55f));
    cw_r2d_polygon_lines(sp, 10, 1.6f, alpha(INK, 0.6f));
  }
  /* twinkles on the lit side of the lip and the belly */
  for (int i = 0; i < 2; i++) {
    float tw = cw_clampf(cw_sin(t * 2.3f + (float)i * 2.1f) * 1.6f - 0.6f, 0, 1);
    if (tw <= 0) continue;
    cw_vec2 p = i == 0 ? cw_v2(cx - u * 0.36f, gy - u * 1.82f) : cw_v2(cx - u * 0.3f, gy - u * 1.2f);
    float r = u * (0.16f + 0.06f * (float)i) * tw;
    cw_color w = rgba(1, 1, 0.94f, 0.95f * tw);
    cw_r2d_line(cw_v2(p.x - r, p.y), cw_v2(p.x + r, p.y), 2.5f, w);
    cw_r2d_line(cw_v2(p.x, p.y - r), cw_v2(p.x, p.y + r), 2.5f, w);
    cw_r2d_circle(p, u * 0.03f, 8, w);
  }
}

static void draw_padlock(cw_vec2 c, float r, cw_color col) {
  cw_r2d_arc(c, r * 0.62f, r * 0.9f, CW_PI, CW_TAU, 14, INK);
  cw_r2d_arc(c, r * 0.68f, r * 0.8f, CW_PI, CW_TAU, 14, col);
  cw_r2d_round_rect(cw_rect_make(c.x - r * 0.95f, c.y - r * 0.05f, r * 1.9f, r * 1.4f), r * 0.2f, 5, INK);
  cw_r2d_round_rect(cw_rect_make(c.x - r * 0.84f, c.y + r * 0.06f, r * 1.68f, r * 1.18f), r * 0.16f, 5, col);
  cw_r2d_circle(cw_v2(c.x, c.y + r * 0.58f), r * 0.2f, 10, INK);
  cw_r2d_rect(cw_rect_make(c.x - r * 0.06f, c.y + r * 0.58f, r * 0.12f, r * 0.42f), INK);
}

static race_setup cup_race_setup(void) {
  race_setup rs = { .map = CUPS[U.cup.cup].map[U.cup.race], .laps = U.cup.laps > 0 ? U.cup.laps : 3, .humans = U.cup.humans, .cpu_skill = U.skill, .items = true, .mode = RM_CUP };
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = U.veh[i], rs.colour[i] = U.col[i];
  rs.speed_class = U.cls, rs.assist_steer = (U.assist & 1) != 0, rs.assist_gas = (U.assist & 2) != 0;
  return rs;
}

static void cup_start(void) {
  int humans = U.setup.humans < 1 ? 1 : U.setup.humans;
  memset(&U.cup, 0, sizeof U.cup);
  U.auto_cup = false;
  U.cup.on = true;
  U.cup.cup = U.cup_sel;
  U.cup.race = 0;
  U.cup.humans = humans;
  U.cup.counted = -1;
  race_setup rs = cup_race_setup();
  U.setup = rs;
  save_write();
  sfx_ok();
  begin_pending(PD_START_RACE);
}

/* the race just finished counts: points by place for every kart, the human ones included */
static void cup_results_add(void) {
  if (U.cup.counted == U.cup.race) return;
  U.cup.counted = U.cup.race;
  for (int i = 0; i < U.res_n; i++) {
    const result_row *r = &U.res[i];
    U.cup.pts[r->kart] += r->points;
    U.cup.race_pts[r->kart] = r->points;
    U.cup.last_place[r->kart] = r->place;
  }
}

/* total points, then the last race's place, then the kart number */
static void cup_sort(void) {
  int n = 0, idx[MAXK];
  for (int i = 0; i < MAXK; i++)
    if (U.cup.last_place[i] > 0) idx[n++] = i;
  for (int a = 1; a < n; a++) {
    int x = idx[a], b = a - 1;
    while (b >= 0) {
      int p = idx[b];
      bool later = U.cup.pts[x] > U.cup.pts[p] || (U.cup.pts[x] == U.cup.pts[p] && U.cup.last_place[x] < U.cup.last_place[p]);
      if (!later) break;
      idx[b + 1] = idx[b];
      b--;
    }
    idx[b + 1] = x;
  }
  for (int i = 0; i < MAXK; i++) U.cup.order[i] = i < n ? idx[i] : -1;
  U.cup.n = n;
}

static void cup_open_standings(void) {
  cup_sort();
  goto_mode(M_STANDINGS);
}

/* the last race is in: trophies for the humans by their place, the next cup opens on a trophy, and it is all saved */
static void cup_finish(void) {
  cup_sort();
  int best = 0;
  bool was_open = U.cup.cup + 1 < NCUPS ? cup_unlocked(U.cup.cup + 1) : true;
  for (int h = 0; h < U.cup.humans && h < MAXV; h++) {
    int place = 0;
    for (int i = 0; i < U.cup.n; i++)
      if (U.cup.order[i] == h) place = i + 1;
    U.cup.final_place[h] = place;
    U.cup.trophy[h] = place == 1 ? 3 : place == 2 ? 2 : place == 3 ? 1 : 0;
    if (U.cup.trophy[h] > best) best = U.cup.trophy[h];
  }
  U.cup.best = best;
  if (best > SAVE.cup_best[U.cup.cup]) SAVE.cup_best[U.cup.cup] = (uint8_t)best;
  U.cup.unlocked_now = !was_open && cup_unlocked(U.cup.cup + 1);
  save_write();
  goto_mode(M_CUPEND);
  U.res_ticks = 0;
  if (best > 0) audio_ui(AUI_GO);
  audio_announce(best == 3 ? ANN_CUP_WIN : best > 0 ? ANN_CUP_TROPHY : ANN_CUP_DONE);
}

/* ── the cup select ─────────────────────────────────────────────────── */

typedef struct cuplay {
  float s;
  bool por;
  cw_rect card[NCUPS], row[MAX_OPT_ROWS], start, info;
  int nrows;
} cuplay;

static void cup_layout(float W, float H, cuplay *l) {
  float s = lscale(W, H);
  int rows[MAX_OPT_ROWS];
  l->nrows = option_rows(rows);
  l->s = s;
  l->por = is_portrait(W, H);
  float gx = W * 0.03f, gw = W * 0.94f, gap = 12 * s;
  if (!l->por) {
    float cwid = (gw - gap) / 2, y0 = 92 * s, ch = 216 * s;
    for (int i = 0; i < NCUPS; i++) l->card[i] = cw_rect_make(gx + (float)i * (cwid + gap), y0, cwid, ch);
    float oy = y0 + ch + 12 * s, rh = 36 * s, rg = 5 * s;
    for (int i = 0; i < l->nrows; i++) l->row[i] = cw_rect_make(gx, oy + (float)i * (rh + rg), gw * 0.56f, rh);
    float bx = gx + gw * 0.56f + gap;
    l->info = cw_rect_make(bx, oy, gw * 0.44f - gap, 52 * s);
    l->start = cw_rect_make(bx, oy + 62 * s, gw * 0.44f - gap, 54 * s);
  } else {
    float cwid = gw, y0 = 92 * s, ch = 232 * s;
    for (int i = 0; i < NCUPS; i++) l->card[i] = cw_rect_make(gx, y0 + (float)i * (ch + gap), cwid, ch);
    float oy = y0 + 2 * (ch + gap) + 4 * s, rh = 38 * s, rg = 5 * s;
    for (int i = 0; i < l->nrows; i++) l->row[i] = cw_rect_make(gx, oy + (float)i * (rh + rg), gw, rh);
    l->info = cw_rect_make(gx, oy + (float)l->nrows * (rh + rg), gw, 0);
    l->start = cw_rect_make(gx, oy + (float)l->nrows * (rh + rg) + 4 * s, gw, 52 * s);
  }
}

static void tick_cup(float W, float H) {
  cuplay l;
  cup_layout(W, H, &l);
  int rows[MAX_OPT_ROWS];
  int nr = option_rows(rows);
  int dx = N.dx, dy = N.dy;
  bool ok = cw_action_just_pressed(0, U.m_ok) && U.mt > 12;
  bool back = cw_action_just_pressed(0, U.a_back) && U.mt > 8;
  bool start = false;
  if (U.cup_focus > nr + 1) U.cup_focus = nr + 1;
  if (U.cup_focus == 0) {
    if (dx) U.cup_sel = (U.cup_sel + dx + NCUPS) % NCUPS, sfx_move();
    if (dy > 0) U.cup_focus = 1, sfx_move();
    if (ok) start = true;
  } else {
    if (dy) {
      int nf = U.cup_focus + dy;
      U.cup_focus = nf < 0 ? 0 : CW_MIN(nf, nr + 1);
      sfx_move();
    }
    if (U.cup_focus >= 1 && U.cup_focus <= nr) {
      if (dx) opt_bump(rows[U.cup_focus - 1], dx);
      if (ok) opt_bump(rows[U.cup_focus - 1], 1);
    } else if (U.cup_focus == nr + 1 && ok) start = true;
  }
  if (pointer_mode() && N.moved)
    for (int i = 0; i < NCUPS; i++)
      if (in_rect(N.m, l.card[i]) && (U.cup_sel != i || U.cup_focus != 0)) U.cup_sel = i, U.cup_focus = 0, sfx_move();
  if (N.click) {
    for (int i = 0; i < NCUPS; i++)
      if (in_rect(N.m, l.card[i])) {
        if (U.cup_sel == i && U.cup_focus == 0 && U.device != DEV_TOUCH) start = true;
        else if (U.cup_sel != i) sfx_move();
        U.cup_sel = i;
        U.cup_focus = 0;
      }
    for (int r = 0; r < nr; r++)
      if (in_rect(N.m, l.row[r])) {
        U.cup_focus = r + 1;
        float t = (N.m.x - l.row[r].x) / l.row[r].w;
        opt_bump(rows[r], t < 0.4f ? -1 : 1);
      }
    if (in_rect(N.m, l.start)) start = true;
    if (in_rect(N.m, back_button_rect(W, H))) back = true;
  }
  if (start) {
    if (cup_unlocked(U.cup_sel)) cup_start();
    else sfx_back(), U.shake = 24;
    return;
  }
  if (back) {
    sfx_back();
    for (int i = 0; i < U.setup.humans; i++) U.ready[i] = false;
    goto_mode(M_VEHICLE);
    U.mt = 30;
  }
  if (U.shake > 0) U.shake--;
}

static void draw_cup_card(const cuplay *l, int c, float s) {
  cw_rect r = l->card[c];
  bool sel = U.cup_sel == c, open = cup_unlocked(c);
  float ap = ease_back(((float)U.mt - (float)c * 4) / 16.0f);
  if (ap <= 0) return;
  float pop = sel && U.cup_focus == 0 ? 1.02f + 0.008f * cw_sin(U.time * 7) : 1;
  r.y += (1 - ap) * 70 * s;
  cw_rect q = cw_rect_make(r.x - r.w * (pop - 1) * 0.5f, r.y - r.h * (pop - 1) * 0.5f, r.w * pop, r.h * pop);
  if (c == U.cup_sel && U.shake > 0) q.x += 6 * s * cw_sin((float)U.shake * 2.5f);
  slab(q, 0, sel ? rgba(0.2f, 0.15f, 0.1f, 0.95f) : rgba(0.1f, 0.075f, 0.09f, 0.88f), s);
  cw_r2d_rect_lines(cw_rect_make(q.x + 3 * s, q.y + 3 * s, q.w - 6 * s, q.h - 6 * s), 3 * s, sel ? (U.cup_focus == 0 ? MUSTARD : alpha(MUSTARD, 0.55f)) : rgba(0.4f, 0.36f, 0.36f, 0.6f));
  const cup_def *cd = &CUPS[c];
  tx_fit(U.f_title, cd->name, q.x + 22 * s, q.y + 30 * s, 28 * s, q.w * 0.6f, open ? MUSTARD : DIM, CW_ALIGN_LEFT);
  tx_para(U.f_hud, cd->blurb, cw_rect_make(q.x + 22 * s, q.y + 52 * s, q.w - 120 * s, 44 * s), 15 * s, alpha(CREAM, open ? 1.0f : 0.6f), CW_ALIGN_LEFT);
  /* the best trophy */
  if (open) draw_trophy(q.x + q.w - 52 * s, q.y + 100 * s, 22 * s, SAVE.cup_best[c], U.time);
  /* the four races */
  float gy = q.y + 108 * s, cwid = (q.w - 36 * s) / 4;
  for (int i = 0; i < CUP_RACES; i++) {
    int m = cd->map[i];
    cw_rect mr = cw_rect_make(q.x + 18 * s + (float)i * cwid, gy, cwid - 8 * s, q.h - 108 * s - 32 * s);
    cw_r2d_round_rect(mr, 8 * s, 5, rgba(0.05f, 0.04f, 0.06f, open ? 0.85f : 0.6f));
    if (U.mm[m].ok) mm_draw(&U.mm[m], mr, 8 * s, s, 2.4f * s + 0.4f, false);
    cw_r2d_round_rect_lines(mr, 8 * s, 1.6f * s, 5, INK);
    char nb[8];
    snprintf(nb, sizeof nb, "%d", i + 1);
    tx_o(U.f_title, nb, mr.x + 12 * s, mr.y + 12 * s, 14 * s, alpha(MUSTARD, open ? 1 : 0.5f), (txopt){ .align = CW_ALIGN_CENTER });
    tx_fit(U.f_small, MAPS[m]->name, mr.x + mr.w * 0.5f, mr.y + mr.h + 15 * s, 14 * s, mr.w + 6 * s, alpha(CREAM, open ? 0.95f : 0.55f), CW_ALIGN_CENTER);
  }
  if (!open) {
    cw_r2d_rect(q, rgba(0.02f, 0.01f, 0.03f, 0.74f));
    draw_padlock(cw_v2(q.x + q.w * 0.5f, q.y + q.h * 0.36f), 26 * s, rgba(0.82f, 0.78f, 0.7f, 1));
    char b[96];
    snprintf(b, sizeof b, "Win a trophy in the %s to open this", CUPS[c - 1].name);
    float tw = fminf(q.w - 24 * s, tx_width(U.f_hud, b, 17 * s) + 40 * s);
    cw_rect pill = cw_rect_make(q.x + q.w * 0.5f - tw * 0.5f, q.y + q.h * 0.62f, tw, 32 * s);
    slab(pill, 8 * s, rgba(0.1f, 0.07f, 0.09f, 0.95f), s * 0.8f);
    tx_fit(U.f_hud, b, pill.x + pill.w * 0.5f, pill.y + pill.h * 0.5f, 17 * s, pill.w - 20 * s, CREAM, CW_ALIGN_CENTER);
  }
}

static void draw_cup(float W, float H, float s) {
  backdrop(W, H, s, 0.86f);
  header(W, H, s, "CUP", "Four races, one trophy");
  cuplay l;
  cup_layout(W, H, &l);
  for (int c = 0; c < NCUPS; c++) draw_cup_card(&l, c, s);
  int rows[MAX_OPT_ROWS];
  int nr = option_rows(rows);
  for (int r = 0; r < nr; r++) {
    char v[24];
    opt_text(rows[r], v, sizeof v);
    draw_option_row(l.row[r], OPT_LABEL[rows[r]], v, U.cup_focus == r + 1, s, clamp01(((float)U.mt - 14 - (float)r * 3) / 14.0f));
    if (rows[r] == OPT_ASSIST && (U.assist & 3) == 0 && SAVE.races < 3) assist_tag(l.row[r], s);
  }
  if (l.info.h > 0) {
    tx_fit(U.f_hud, "4 races of 3 laps", l.info.x + l.info.w * 0.5f, l.info.y + 14 * s, 17 * s, l.info.w, CREAM, CW_ALIGN_CENTER);
    tx_fit(U.f_small, "Points: 10 8 6 5 4 3 2 1 by place. Top three win a trophy.", l.info.x + l.info.w * 0.5f, l.info.y + 38 * s, 14 * s, l.info.w, alpha(CREAM, 0.8f), CW_ALIGN_CENTER);
  }
  bool open = cup_unlocked(U.cup_sel);
  bool sf = U.cup_focus == 0 || U.cup_focus == nr + 1;
  draw_button(l.start, open ? "START CUP" : "LOCKED", sf, open, s * 0.82f, clamp01(((float)U.mt - 22) / 14.0f));
  draw_button(back_button_rect(W, H), "BACK", false, true, s * 0.8f, 1);
  hint_line(W, H, s, dev_hint("D-pad: choose   (A) go   down: options   (B) back", "Arrows choose   Enter: go   Down: options   Esc: back", "Click a cup, again to go", "Tap a cup, then START CUP"));
}

/* ── the standings between races and the final ─────────────────────── */

static int stand_nb(void) { return 2; }

static void tick_standings(float W, float H) {
  int hx = (any_held(U.m_right) ? 1 : 0) - (any_held(U.m_left) ? 1 : 0);
  int dx, dy;
  nav_step(&U.nav, hx, 0, &dx, &dy);
  int nb = stand_nb();
  if (dx) U.sel = (U.sel + dx + nb) % nb, sfx_move();
  if (is_portrait(W, H)) {
    int hy = (any_held(U.m_down) ? 1 : 0) - (any_held(U.m_up) ? 1 : 0);
    int ddx, ddy;
    nav_step(&U.pnav[0], 0, hy, &ddx, &ddy);
    if (ddy) U.sel = (U.sel + ddy + nb) % nb, sfx_move();
  }
  int act = -1;
  if (pointer_mode() && N.moved)
    for (int i = 0; i < nb; i++)
      if (in_rect(N.m, res_button_n(i, nb, W, H)) && U.sel != i) U.sel = i, sfx_move();
  if (N.click && U.mt > 10)
    for (int i = 0; i < nb; i++)
      if (in_rect(N.m, res_button_n(i, nb, W, H))) U.sel = i, act = i;
  if (N.ok && U.mt > 30) act = U.sel;
  if (U.auto_cup && U.mt > 60) act = 0;
  if (U.pending != PD_NONE) act = -1; /* already on the way to the next race: a second OK must not skip a race or end the cup */
  if (act == 0) {
    sfx_ok();
    if (U.cup.race + 1 < cup_nraces()) {
      U.cup.race++;
      begin_pending(PD_CUP_NEXT);
    } else cup_finish();
  } else if (act == 1) {
    sfx_ok();
    U.cup.on = false;
    begin_pending(PD_TO_MENU);
  }
}

static void draw_standings(float W, float H, float s) {
  backdrop(W, H, s, 0.8f);
  char sub[64];
  snprintf(sub, sizeof sub, "%s  -  after race %d of %d", CUPS[U.cup.cup].name, U.cup.race + 1, cup_nraces());
  header(W, H, s, "STANDINGS", sub);
  bool por = is_portrait(W, H);
  float pw = fminf(W * 0.94f, 640 * s), px = W * 0.5f - pw * 0.5f, py = 104 * s;
  float ph = por ? 8 * 40 * s + 56 * s : H - py - 108 * s;
  float panel = ease_out((float)U.mt / 16.0f);
  cw_rect tab = cw_rect_make(px, py + (1 - panel) * 120 * s, pw, ph);
  slab(tab, 0, rgba(0.09f, 0.065f, 0.08f, 0.93f * panel), s);
  float rowh = fminf(40 * s, (tab.h - 52 * s) / 8.0f);
  float hy = tab.y + 22 * s;
  tx(U.f_small, "DRIVER", tab.x + 66 * s, hy, 15 * s, DIM, CW_ALIGN_LEFT);
  tx(U.f_small, "THIS RACE", tab.x + tab.w * 0.66f, hy, 15 * s, DIM, CW_ALIGN_CENTER);
  tx(U.f_small, "TOTAL", tab.x + tab.w - 24 * s, hy, 15 * s, DIM, CW_ALIGN_RIGHT);
  for (int i = 0; i < U.cup.n; i++) {
    int ki = U.cup.order[i];
    float ra = clamp01(((float)U.mt - 8 - (float)i * 4) / 14.0f);
    if (ra <= 0) continue;
    float y = tab.y + 40 * s + (float)i * rowh;
    cw_rect rr = cw_rect_make(tab.x + 10 * s + (1 - ease_out(ra)) * 120 * s, y, tab.w - 20 * s, rowh - 3 * s);
    bool human = ki < U.cup.humans;
    int colour = ki < MAXV && human ? U.col[ki] : (ki + 3) & 7;
    para(rr, 6 * s, human ? alpha(mix(PANEL, kart_col(colour), 0.28f), ra) : alpha(rgba(0.14f, 0.11f, 0.13f, 0.8f), ra));
    if (human) para_lines(rr, 6 * s, 2 * s, alpha(kart_col(colour), ra));
    float cy = rr.y + rr.h * 0.5f;
    char b[48];
    snprintf(b, sizeof b, "%d", i + 1);
    tx(U.f_title, b, rr.x + 16 * s, cy, rowh * 0.5f, alpha(i < 3 ? PLACE_COL[i] : CREAM, ra), CW_ALIGN_CENTER);
    cw_r2d_circle(cw_v2(rr.x + 42 * s, cy), 6 * s, 12, alpha(INK, ra));
    cw_r2d_circle(cw_v2(rr.x + 42 * s, cy), 4.8f * s, 12, alpha(kart_col(colour), ra));
    if (human) snprintf(b, sizeof b, "PLAYER %d", ki + 1);
    else snprintf(b, sizeof b, "%s", CPU_NAME[ki % MAXK]);
    tx_fit(U.f_hud, b, rr.x + 56 * s, cy, rowh * 0.5f, rr.w * 0.4f, alpha(CREAM, ra), CW_ALIGN_LEFT);
    snprintf(b, sizeof b, "+%d", U.cup.race_pts[ki]);
    tx(U.f_hud, b, tab.x + tab.w * 0.66f, cy, rowh * 0.46f, alpha(LIME, ra), CW_ALIGN_CENTER);
    snprintf(b, sizeof b, "%d", U.cup.pts[ki]);
    tx(U.f_title, b, rr.x + rr.w - 14 * s, cy, rowh * 0.5f, alpha(MUSTARD, ra), CW_ALIGN_RIGHT);
  }
  static const char *const BL[2] = { "NEXT RACE", "QUIT CUP" };
  const char *first = U.cup.race + 1 < cup_nraces() ? BL[0] : "FINAL RESULTS";
  for (int i = 0; i < 2; i++) draw_button(res_button_n(i, 2, W, H), i == 0 ? first : BL[1], U.sel == i && U.mt > 20, true, s * 0.8f, clamp01(((float)U.mt - 24 - (float)i * 4) / 14.0f));
  if (U.cup.race + 1 < cup_nraces()) {
    snprintf(sub, sizeof sub, "Next: %s", MAPS[CUPS[U.cup.cup].map[U.cup.race + 1]]->name);
    tx(U.f_hud, sub, W * 0.04f, H - 50 * s, 20 * s, alpha(CREAM, 0.9f * panel), CW_ALIGN_LEFT);
  }
}

static void tick_cupend(float W, float H) {
  U.res_ticks++;
  if (U.pending != PD_NONE) return;
  int hx = (any_held(U.m_right) ? 1 : 0) - (any_held(U.m_left) ? 1 : 0);
  int dx, dy;
  nav_step(&U.nav, hx, 0, &dx, &dy);
  if (dx) U.sel = (U.sel + dx + 2) % 2, sfx_move();
  if (is_portrait(W, H)) {
    int hy = (any_held(U.m_down) ? 1 : 0) - (any_held(U.m_up) ? 1 : 0);
    int ddx, ddy;
    nav_step(&U.pnav[0], 0, hy, &ddx, &ddy);
    if (ddy) U.sel = (U.sel + ddy + 2) % 2, sfx_move();
  }
  int act = -1;
  if (pointer_mode() && N.moved)
    for (int i = 0; i < 2; i++)
      if (in_rect(N.m, res_button_n(i, 2, W, H)) && U.sel != i) U.sel = i, sfx_move();
  if (N.click && U.res_ticks > 20)
    for (int i = 0; i < 2; i++)
      if (in_rect(N.m, res_button_n(i, 2, W, H))) U.sel = i, act = i;
  if (N.ok && U.res_ticks > 60) act = U.sel;
  if (act == 0) {
    sfx_ok();
    /* the same cup again from the first race */
    int humans = U.cup.humans;
    memset(&U.cup, 0, sizeof U.cup);
    U.cup.on = true;
    U.cup.cup = U.cup_sel;
    U.cup.humans = humans;
    U.cup.counted = -1;
    race_setup rs = cup_race_setup();
    U.setup = rs;
    begin_pending(PD_START_RACE);
  } else if (act == 1) {
    sfx_ok();
    U.cup.on = false;
    begin_pending(PD_TO_MENU);
  }
}

static void draw_cupend(float W, float H, float s) {
  backdrop(W, H, s, 0.84f);
  int t = U.res_ticks;
  bool por = is_portrait(W, H);
  char b[96];
  snprintf(b, sizeof b, "%s", CUPS[U.cup.cup].name);
  float head = ease_back((float)t / 18.0f);
  {
    const char *msg = U.cup.best > 0 ? "CUP WON!" : "CUP DONE";
    snprintf(b, sizeof b, "%s", msg);
    float bw = fminf(W * 0.94f, tx_width(U.f_title, b, 46 * s) + 110 * s);
    cw_rect r = cw_rect_make(W * 0.5f - bw * 0.5f, 22 * s - (1 - head) * 120 * s, bw, 72 * s);
    slab(r, 18 * s, rgba(0.1f, 0.07f, 0.09f, 0.94f), s);
    cw_color col = U.cup.best > 0 ? trophy_col(U.cup.best) : DIM;
    para(cw_rect_make(r.x + 6 * s, r.y + r.h - 9 * s, r.w - 12 * s, 6 * s), 0, col);
    tx_fit(U.f_title, b, r.x + r.w * 0.5f + 8 * s, r.y + 33 * s, 46 * s, r.w - 90 * s, col, CW_ALIGN_CENTER);
    snprintf(b, sizeof b, "%s", CUPS[U.cup.cup].name);
    tx(U.f_hud, b, W * 0.5f, r.y + r.h + 20 * s, 20 * s, alpha(CREAM, head), CW_ALIGN_CENTER);
  }
  float panel = ease_out(((float)t - 8) / 18.0f);
  /* the trophy */
  float tcx = por ? W * 0.5f : W * 0.27f, tgy = por ? 330 * s : H * 0.7f, tu = (por ? 70 : 92) * s * ease_back(((float)t - 14) / 24.0f);
  if (U.cup.best > 0) draw_confetti(cw_rect_make(0, 0, W, H * 0.8f), s, U.time);
  if (tu > 1) {
    draw_trophy(tcx, tgy, tu, U.cup.best, U.time);
    if (U.cup.best > 0) {
      cw_r2d_set_blend(CW_BLEND_ADD);
      cw_r2d_circle(cw_v2(tcx, tgy - tu * 1.2f), tu * 1.6f, 30, rgba(0.35f, 0.26f, 0.08f, 0.22f + 0.05f * cw_sin(U.time * 3)));
      cw_r2d_set_blend(CW_BLEND_ALPHA);
    }
  }
  tx_o(U.f_title, TROPHY_NAME[U.cup.best], tcx, tgy + 34 * s, 22 * s, trophy_col(U.cup.best > 0 ? U.cup.best : 0) , (txopt){ .align = CW_ALIGN_CENTER });
  /* the final table */
  float pw = por ? W * 0.94f : W * 0.5f, px = por ? W * 0.03f : W * 0.47f, py = por ? 400 * s : 140 * s;
  float ph = por ? H - py - 200 * s : H - py - 108 * s;
  cw_rect tab = cw_rect_make(px + (1 - panel) * 300 * s, py, pw, ph);
  slab(tab, 0, rgba(0.09f, 0.065f, 0.08f, 0.93f * panel), s);
  float rowh = fminf(34 * s, (tab.h - 44 * s - (U.cup.unlocked_now ? 34 * s : 0)) / 8.0f);
  tx(U.f_small, "FINAL STANDINGS", tab.x + 20 * s, tab.y + 18 * s, 15 * s, DIM, CW_ALIGN_LEFT);
  tx(U.f_small, "POINTS", tab.x + tab.w - 22 * s, tab.y + 18 * s, 15 * s, DIM, CW_ALIGN_RIGHT);
  for (int i = 0; i < U.cup.n; i++) {
    int ki = U.cup.order[i];
    float ra = clamp01(((float)t - 20 - (float)i * 4) / 14.0f);
    if (ra <= 0) continue;
    cw_rect rr = cw_rect_make(tab.x + 10 * s, tab.y + 34 * s + (float)i * rowh, tab.w - 20 * s, rowh - 3 * s);
    bool human = ki < U.cup.humans;
    int colour = human ? U.col[ki] : (ki + 3) & 7;
    para(rr, 6 * s, human ? alpha(mix(PANEL, kart_col(colour), 0.28f), ra) : alpha(rgba(0.14f, 0.11f, 0.13f, 0.8f), ra));
    if (human) para_lines(rr, 6 * s, 2 * s, alpha(kart_col(colour), ra));
    float cy = rr.y + rr.h * 0.5f;
    snprintf(b, sizeof b, "%d", i + 1);
    tx(U.f_title, b, rr.x + 14 * s, cy, rowh * 0.5f, alpha(i < 3 ? PLACE_COL[i] : CREAM, ra), CW_ALIGN_CENTER);
    cw_r2d_circle(cw_v2(rr.x + 38 * s, cy), 5.4f * s, 12, alpha(INK, ra));
    cw_r2d_circle(cw_v2(rr.x + 38 * s, cy), 4.3f * s, 12, alpha(kart_col(colour), ra));
    if (human) snprintf(b, sizeof b, "PLAYER %d", ki + 1);
    else snprintf(b, sizeof b, "%s", CPU_NAME[ki % MAXK]);
    tx_fit(U.f_hud, b, rr.x + 52 * s, cy, rowh * 0.5f, rr.w * 0.5f, alpha(CREAM, ra), CW_ALIGN_LEFT);
    if (human && U.cup.trophy[ki] > 0) {
      float tw = tx_width(U.f_hud, b, rowh * 0.5f);
      cw_vec2 tc = cw_v2(rr.x + 52 * s + fminf(tw, rr.w * 0.5f) + 18 * s, cy);
      cw_r2d_circle(tc, 6 * s, 12, alpha(INK, ra));
      cw_r2d_circle(tc, 4.8f * s, 12, alpha(trophy_col(U.cup.trophy[ki]), ra));
    }
    snprintf(b, sizeof b, "%d", U.cup.pts[ki]);
    tx(U.f_title, b, rr.x + rr.w - 12 * s, cy, rowh * 0.5f, alpha(MUSTARD, ra), CW_ALIGN_RIGHT);
  }
  if (U.cup.unlocked_now && U.cup.cup + 1 < NCUPS) {
    snprintf(b, sizeof b, "%s UNLOCKED!", CUPS[U.cup.cup + 1].name);
    float a = 0.75f + 0.25f * cw_sin(U.time * 6);
    cw_rect ur = cw_rect_make(tab.x + 10 * s, tab.y + tab.h - 36 * s, tab.w - 20 * s, 28 * s);
    para(ur, 8 * s, alpha(TEAL, a * panel));
    para_lines(ur, 8 * s, 2 * s, alpha(INK, panel));
    tx_o(U.f_title, b, ur.x + ur.w * 0.5f + 4 * s, ur.y + ur.h * 0.5f, 17 * s, alpha(INK, panel), (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
  }
  static const char *const BL[2] = { "AGAIN", "MENU" };
  for (int i = 0; i < 2; i++) draw_button(res_button_n(i, 2, W, H), BL[i], U.sel == i && t > 60, true, s * 0.8f, clamp01(((float)t - 50 - (float)i * 4) / 14.0f));
}

/* dev aid and gates: a cup that runs by itself (the bot drives the humans): the cup, how many humans, laps and races (0: the usual) */
void ui_debug_cup(int cup, int humans, int laps, int races) {
  memset(&U.cup, 0, sizeof U.cup);
  U.cup.on = true;
  U.cup.cup = CW_CLAMP(cup, 0, NCUPS - 1);
  U.cup_sel = U.cup.cup;
  U.cup.humans = CW_CLAMP(humans, 1, MAXV);
  U.cup.laps = laps;
  U.cup.nraces = races;
  U.cup.counted = -1;
  U.auto_cup = true;
  U.gmode = RM_CUP;
  race_setup rs = cup_race_setup();
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = i * 2 % NVEH, rs.colour[i] = i;
  ui_enter_race(&rs);
}
