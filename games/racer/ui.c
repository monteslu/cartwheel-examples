/* racer's front end and HUD (first pass: the UI owner makes this lovely). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/render2d.h"
#include "ui.h"
#include "world.h"

enum { UM_TITLE, UM_RACE, UM_PAUSE, UM_RESULTS };

static struct {
  int mode, ticks;
  int map, laps, players;
  cw_font font_title, font_hud, font_small;
  cw_action a_start, a_esc, m_left, m_right, m_up, m_down, m_ok;
} U;

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

static bool any_pressed(cw_action a) {
  for (int p = 0; p < 4; p++)
    if (cw_action_just_pressed(p, a)) return true;
  return false;
}

void ui_init(void) {
  U.a_start = cw_action_find("start");
  U.a_esc = cw_action_find("back");
  U.m_left = cw_action_find("m_left");
  U.m_right = cw_action_find("m_right");
  U.m_up = cw_action_find("m_up");
  U.m_down = cw_action_find("m_down");
  U.m_ok = cw_action_find("m_ok");
  const cw_font_desc sdf = { .mode = CW_FONT_SDF, .sdf_padding = 8 };
  U.font_title = cw_font_load("fonts/Bungee-Regular.ttf", &sdf);
  U.font_hud = cw_font_load("fonts/Fredoka-Bold.ttf", &sdf);
  U.font_small = cw_font_load("fonts/Oswald-VF.ttf", &sdf);
  const cw_font fonts[] = { U.font_title, U.font_hud, U.font_small };
  for (unsigned f = 0; f < sizeof fonts / sizeof fonts[0]; f++) cw_font_preload_range(fonts[f], 32, 126);
  const char *pl = getenv("RACER_PLAYERS");
  U.players = pl ? CW_CLAMP(atoi(pl), 1, MAXV) : 1;
  const char *lp = getenv("RACER_LAPS");
  U.laps = lp ? CW_CLAMP(atoi(lp), 1, MAX_LAPS) : 3;
  U.mode = UM_TITLE;
}

void ui_shutdown(void) {}

static void start(void) {
  race_setup rs = { .map = U.map, .laps = U.laps, .humans = U.players, .cpu_skill = 3, .items = true };
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = (i * 2) % NVEH, rs.colour[i] = i;
  ui_enter_race(&rs);
}

void ui_enter_race(const race_setup *rs) {
  game_start_race(rs);
  U.mode = UM_RACE;
  U.ticks = 0;
}

bool ui_race_runs(void) { return U.mode == UM_RACE || U.mode == UM_RESULTS; }
void ui_race_over(void) {
  U.mode = UM_RESULTS;
  U.ticks = 0;
  game_set_camera(2);
}

void ui_fixed(void) {
  U.ticks++;
  switch (U.mode) {
  case UM_TITLE: {
    int dy = (cw_action_just_pressed(0, U.m_down) ? 1 : 0) - (cw_action_just_pressed(0, U.m_up) ? 1 : 0);
    int dx = (cw_action_just_pressed(0, U.m_right) ? 1 : 0) - (cw_action_just_pressed(0, U.m_left) ? 1 : 0);
    U.map = (U.map + dy + NMAPS) % NMAPS;
    U.laps = CW_CLAMP(U.laps + dx, 1, MAX_LAPS);
    if (U.ticks > 20 && (cw_action_just_pressed(0, U.m_ok) || cw_action_just_pressed(0, U.a_start))) start();
    break;
  }
  case UM_RACE:
    if (any_pressed(U.a_start)) U.mode = UM_PAUSE, U.ticks = 0;
    break;
  case UM_PAUSE:
    if (U.ticks > 5 && any_pressed(U.a_start)) U.mode = UM_RACE;
    else if (U.ticks > 5 && any_pressed(U.a_esc)) {
      game_end_race();
      U.mode = UM_TITLE;
      U.ticks = 0;
    }
    break;
  case UM_RESULTS:
    if (U.ticks > 120 && (any_pressed(U.m_ok) || any_pressed(U.a_start))) {
      game_end_race();
      U.mode = UM_TITLE;
      U.ticks = 0;
    }
    break;
  }
}

/* ── drawing ────────────────────────────────────────────────────────── */

static void text(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_text_align align) {
  cw_text_style st = { .size = size, .color = c, .outline_width = size * 0.07f + 0.6f, .outline_color = rgba(0.02f, 0.02f, 0.03f, 0.9f) };
  cw_text_metrics m = cw_font_measure(f, s, &st);
  if (align == CW_ALIGN_RIGHT) x -= m.width;
  else if (align == CW_ALIGN_CENTER) x -= m.width * 0.5f;
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}

static void draw_hud_for(int v, float W, float H, float s) {
  const sim *S = game_sim();
  float x0, y0, w, h;
  game_view_rect(v, W, H, &x0, &y0, &w, &h);
  const kart *k = &S->k[game_view_kart(v)];
  char b[96];
  snprintf(b, sizeof b, "%d", k->place);
  text(U.font_title, b, x0 + 24 * s, y0 + 8 * s, 64 * s, rgba(1, 0.85f, 0.2f, 1), CW_ALIGN_LEFT);
  snprintf(b, sizeof b, "LAP %d/%d", k->lap < 1 ? 1 : k->lap, S->laps);
  text(U.font_hud, b, x0 + w - 20 * s, y0 + 12 * s, 30 * s, rgba(1, 1, 1, 1), CW_ALIGN_RIGHT);
  snprintf(b, sizeof b, "%d", (int)(k->speed * 3.6f));
  text(U.font_title, b, x0 + w - 24 * s, y0 + h - 70 * s, 48 * s, rgba(1, 1, 1, 1), CW_ALIGN_RIGHT);
  if (k->item) text(U.font_hud, ITEM_NAME[k->item], x0 + 24 * s, y0 + 80 * s, 26 * s, rgba(0.6f, 1, 0.7f, 1), CW_ALIGN_LEFT);
  if (S->phase == PH_COUNT) {
    int n = 3 - S->phase_ticks / 60;
    if (n >= 1) {
      snprintf(b, sizeof b, "%d", n);
      text(U.font_title, b, x0 + w * 0.5f, y0 + h * 0.3f, 120 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    }
  }
}

void ui_render(float W, float H) {
  float s = H / 540.0f;
  switch (U.mode) {
  case UM_TITLE: {
    text(U.font_title, GAME_TITLE, W * 0.5f, H * 0.08f, 90 * s, rgba(1, 0.85f, 0.2f, 1), CW_ALIGN_CENTER);
    for (int i = 0; i < NMAPS; i++) {
      char b[96];
      snprintf(b, sizeof b, "%s%s", i == U.map ? "> " : "  ", MAPS[i]->name);
      text(U.font_hud, b, W * 0.5f, H * (0.3f + 0.058f * (float)i), 28 * s, i == U.map ? rgba(1, 1, 1, 1) : rgba(0.7f, 0.7f, 0.75f, 1), CW_ALIGN_CENTER);
    }
    char b[64];
    snprintf(b, sizeof b, "LAPS %d  (left / right)    PLAYERS %d", U.laps, U.players);
    text(U.font_small, b, W * 0.5f, H * 0.85f, 22 * s, rgba(0.9f, 0.9f, 0.9f, 1), CW_ALIGN_CENTER);
    break;
  }
  case UM_RACE:
  case UM_PAUSE:
    if (!game_loaded()) break;
    for (int v = 0; v < game_views(); v++) draw_hud_for(v, W, H, game_views() > 1 ? s * 0.6f : s);
    if (U.mode == UM_PAUSE) text(U.font_title, "PAUSED", W * 0.5f, H * 0.4f, 80 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    break;
  case UM_RESULTS: {
    if (!game_loaded()) break;
    const sim *S = game_sim();
    text(U.font_title, "RESULTS", W * 0.5f, H * 0.06f, 64 * s, rgba(1, 0.85f, 0.2f, 1), CW_ALIGN_CENTER);
    for (int i = 0; i < MAXK; i++) {
      int kk = S->order[i];
      char b[64];
      snprintf(b, sizeof b, "%d  %s %d", i + 1, S->k[kk].human ? "PLAYER" : "CPU", kk + 1);
      text(U.font_hud, b, W * 0.5f, H * (0.2f + 0.075f * (float)i), 28 * s, S->k[kk].human ? rgba(1, 1, 0.5f, 1) : rgba(0.85f, 0.85f, 0.9f, 1), CW_ALIGN_CENTER);
    }
    break;
  }
  }
}
