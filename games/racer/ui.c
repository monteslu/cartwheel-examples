/* racer's front end and HUD: the title with a live demo race behind it, player join, vehicle and
 * map select, options, pause, results, and the HUD over the race.
 *
 * Layout of this unit:
 *   ui.c           state, input (pad, keys, mouse, touch), drawing helpers, the save, the screen flow
 *   ui_hud.inc     the HUD of each split-screen view
 *   ui_screens.inc the menu screens (drawing and input for each)
 *
 * Everything here is render-side: it reads the sim and never writes it. The menus are driven from the
 * fixed tick (ui_fixed) so a press is seen exactly once; drawing happens in ui_render over the 3D frame.
 *
 * Dev aids from the environment:
 *   RACER_UI=title|menu|join|vehicle|map|options|controls|pause|results   open that screen (with the demo race
 *                                                                          or, for pause and results, a race)
 *   RACER_RES=WxH        window size (a portrait window for example: 540x960)
 *   RACER_PLAYERS=n      humans for join / vehicle / race (1..4)
 *   RACER_UI_MAP=n      the map of a pause / results dev screen
 *   RACER_UI_FAKE=1      with RACER_UI=results: fill the sim with a finished race so the table can be looked at
 *   RACER_UI=trial|trialres|cup|standings|cupend|assist   the time trial's map select, a finished trial's results (RACER_UI_MEDAL=0..4
 *                        picks the medal, RACER_UI_RECORD=1 a new record), the cup select, the standings between two races, the
 *                        trophy screen (RACER_UI_CUP=0|1, RACER_UI_PLACE=1..8 where the human finished) and the assists page
 *
 * Modes (ui_modes.inc): RACE, TIME TRIAL (ghost, medals) and CUP (four races, points, trophies, unlocks); speed class and
 * assists are in the race setup (map select, cup select) and the options page. The save data is version 2 (see save_data). */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/platform.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "arena.h"
#include "ui.h"
#include "world.h"

/* ── palette: a bright junkyard comic, ink outlines, warm light ───────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static cw_color alpha(cw_color c, float a) { return (cw_color){ c.r, c.g, c.b, c.a * a }; }
static cw_color mix(cw_color a, cw_color b, float t) {
  return (cw_color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}
static cw_color shade(cw_color c, float k) { return (cw_color){ c.r * k, c.g * k, c.b * k, c.a }; }
#define INK rgba(0.07f, 0.05f, 0.06f, 1)
#define CREAM rgba(1.0f, 0.95f, 0.82f, 1)
#define MUSTARD rgba(1.0f, 0.80f, 0.15f, 1)
#define RUST rgba(0.93f, 0.42f, 0.14f, 1)
#define TEAL rgba(0.16f, 0.78f, 0.72f, 1)
#define SKY rgba(0.35f, 0.65f, 0.95f, 1)
#define PINK rgba(0.95f, 0.30f, 0.50f, 1)
#define LIME rgba(0.55f, 0.90f, 0.30f, 1)
#define RED rgba(0.95f, 0.20f, 0.18f, 1)
#define DIM rgba(0.68f, 0.64f, 0.58f, 1)
#define PANEL rgba(0.11f, 0.08f, 0.09f, 0.88f)

static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float ease_out(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
/* overshoots a little and settles: the pop of an element arriving */
static float ease_back(float t) {
  t = clamp01(t);
  float c = 1.70158f, u = t - 1;
  return 1 + (c + 1) * u * u * u + c * u * u;
}
static float ease_io(float t) { t = clamp01(t); return t * t * (3 - 2 * t); }

/* ── state ──────────────────────────────────────────────────────────── */

enum { M_TITLE, M_JOIN, M_VEHICLE, M_MAP, M_OPTIONS, M_CONTROLS, M_RACE, M_PAUSE, M_RESULTS, M_MODES, M_CUP, M_STANDINGS, M_CUPEND, M_ARENA };
/* what waits behind a fade to black */
enum { PD_NONE, PD_START_RACE, PD_TO_MENU, PD_ATTRACT, PD_RESTART, PD_NEXT_MAP, PD_CUP_NEXT };
enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
#define ATTRACT_SECS 40
#define N_COLOURS 8
#define MMN 240 /* points in a track's outline */

typedef struct navstate {
  int px, py, rx, ry;
} navstate;

/* a track outline for the minimaps: x,z of the centre line, height, what the road is doing there */
typedef struct mmap_t {
  bool ok;
  int n;
  float x[MMN], z[MMN], h[MMN];
  uint8_t fl[MMN]; /* 1 jump gap, 2 loop or corkscrew, 4 tunnel, 8 boost strip, 16 water */
  float minx, maxx, minz, maxz, miny, maxy;
  float length;
  int njumps, nloops, ntunnels;
  int nbr, nbp[4];   /* the branches (shortcuts): a thinner line each */
  float bx[4][40], bz[4][40];
} mmap_t;

typedef struct result_row {
  int kart, place, veh, colour, human; /* human: view index or -1 */
  bool finished;
  float time, best;
  int hits, tricks, points;
  bool record;
} result_row;

#define SAVE_MAGIC 0x52435231u /* 'RCR1' */
#define SAVE_VERSION 2
#define SAVE_V1_SIZE 76        /* the bytes a version 1 save held */
#define NCUPS 2
typedef struct save_data {
  uint32_t magic;
  uint16_t version, size;      /* size: version 1 held the byte count; the ghosts took the block past 65535, so version 2 holds it in 4 byte units */
  uint32_t checksum;
  uint8_t gfx[4];              /* the graphics settings (cw_quality_saved, little-endian) */
  uint8_t music, sfx;          /* 0..10 */
  uint8_t laps, skill, items, map;
  uint8_t veh[MAXV], col[MAXV];
  uint8_t pad[4];
  uint32_t best_lap_cs[NMAPS]; /* centiseconds, 0 = none yet */
  uint32_t races, wins;
  /* version 2: everything above is a version 1 save, which loads with these empty */
  uint8_t speed_class, assist, ghost_on, cup_pick; /* the setup last used: class, assists (1 steering, 2 auto accelerate), the ghost shown, the cup picked */
  uint8_t cup_best[NCUPS];     /* the best trophy of each cup: 0 none, 1 bronze, 2 silver, 3 gold (cup 2 is open once cup 1 has one) */
  uint8_t voice_off;           /* the announcer: 0 on (what every older save holds in this byte), 1 off */
  uint8_t pad2;
  uint32_t trial_cs[NMAPS];    /* the best time trial per map, centiseconds, 0 = none yet */
  ghost_saved ghost[NMAPS];    /* the run behind it: 24 KB a map at most */
} save_data;
_Static_assert(offsetof(save_data, wins) + 4 == SAVE_V1_SIZE, "a version 1 save was 76 bytes");
static save_data SAVE;

static struct {
  int mode, mt, ticks;
  int title_menu;                  /* 0 the splash, 1 the menu */
  int sel;                         /* the highlighted entry of the current list */
  int from;                        /* where options / controls return to */
  /* fade to black around a heavy change */
  float fade;
  int fade_dir, pending;
  /* the demo race behind the menus */
  int attract_map, attract_ticks;
  bool attract_on;
  /* the race to start */
  race_setup setup;
  race_setup last;                 /* the race that is on (for restart) */
  int split;                       /* human players chosen */
  bool joined[MAXV];               /* by pad slot while joining */
  int slot_pad[MAXV];              /* human i -> pad slot */
  /* vehicle select, per human */
  int veh[MAXV], col[MAXV];
  bool ready[MAXV];
  int veh_anim[MAXV];              /* ticks since the vehicle last changed (slide) */
  int veh_dir[MAXV];
  navstate pnav[MAXV];
  int psel[MAXV];                  /* the highlighted control in a vehicle panel (0 vehicle, 1 colour, 2 ready) */
  /* map select */
  int map, laps, skill, items;
  int mapfocus;                    /* 0 the grid of maps, 1.. the option rows, then the start button */
  int gmode;                       /* RM_*: what the menus are setting up (race, time trial, cup) */
  int cls, assist, ghost_on;       /* speed class, assists (bits: 1 steering, 2 auto accelerate), the ghost shown in a trial */
  int auto_runs;                   /* dev/gates: trial runs the bot still retries by itself */
  struct trial_state {
    bool on, record, had_ghost, new_medal;
    int map, medal, prev_medal;
    uint32_t cs, best_cs, prev_best_cs, ghost_cs;
    int gap_cs, runs;
  } tr;
  struct cup_state {
    bool on, unlocked_now;
    int cup, race;                 /* which cup, the race it is on (0..3) */
    int humans, counted, n, best;  /* humans in the cup, the race whose points are in, karts in the standings, the best trophy won */
    int laps, nraces;              /* 0: the usual (3 laps, four races); a dev run or a gate shortens them */
    int pts[MAXK], race_pts[MAXK], last_place[MAXK];
    int order[MAXK];               /* the standings, first first */
    int trophy[MAXV];              /* each human's result */
    int final_place[MAXV];
  } cup;
  int cup_sel, cup_focus;
  struct battle_setup {
    int arena, karts, secs_i;      /* the arena, the karts in the round (2..8), the round's length (BATTLE_SECS index) */
    int focus;                     /* the arena screen: 0 the arenas, 1.. the option rows, then START */
  } bt;
  bool auto_cup;                   /* dev/gates: a cup that clicks through its own results and standings (the bot drives) */
  /* options */
  int opt_sel, cfrom, rec_dummy, shake;
  /* pause / results */
  int pauser;
  int res_ticks;
  result_row res[MAXK];
  int res_n;
  int res_winner_human;            /* the first human's place, for the headline */
  int rec_at_start[NMAPS];
  bool saved_this_race;
  /* input */
  cw_font f_title, f_hud, f_small;
  cw_action a_start, a_back, m_left, m_right, m_up, m_down, m_ok, a_fwd, a_brake, a_left, a_right, a_pup, a_pdn, a_drift, a_item, a_trick;
  int device;
  bool device_used;
  uint32_t prev_buttons;
  float prev_mx, prev_my;
  uint64_t click_frame;
  navstate nav;
  /* touch */
  int t_stick, t_gas, t_brake, t_drift, t_item, t_trick;
  bool touch_on;
  /* tracks */
  mmap_t mm[NMAPS + 1];
  float stat_lo[7], stat_hi[7];
  float time;                      /* seconds, for animation */
  bool dev_done;
} U;

/* the pointer and presses of this tick */
static struct {
  int dx, dy;
  bool ok, back, start, click, down, moved, any;
  cw_vec2 m;
} N;

static bool any_held(cw_action a) {
  for (int p = 0; p < MAXV; p++)
    if (cw_action_pressed(p, a)) return true;
  return false;
}
static bool any_pressed(cw_action a) {
  for (int p = 0; p < MAXV; p++)
    if (cw_action_just_pressed(p, a)) return true;
  return false;
}

/* a direction with key repeat: the first press at once, then after a pause every few ticks */
static void nav_step(navstate *s, int hx, int hy, int *dx, int *dy) {
  *dx = *dy = 0;
  if (hx != s->px) s->px = hx, s->rx = 0, *dx = hx;
  else if (hx && ++s->rx > 22 && (s->rx - 22) % 5 == 0) *dx = hx;
  if (hy != s->py) s->py = hy, s->ry = 0, *dy = hy;
  else if (hy && ++s->ry > 22 && (s->ry - 22) % 5 == 0) *dy = hy;
}

static bool pad_present(int slot) {
  int pd = cw_input_player_pad(slot);
  return pd >= 0 && cw_input_raw()->pads[pd].connected;
}

static void sfx_move(void) { audio_ui(AUI_MOVE); }
static void sfx_ok(void) { audio_ui(AUI_SELECT); }
static void sfx_back(void) { audio_ui(AUI_BACK); }

/* which device the player used last drives the prompts */
static void read_devices(void) {
  const cw_raw_input *in = cw_input_raw();
  int before = U.device, now = -1;
  bool pressed_mouse = (in->mouse_pressed & 1) != 0;
  N.moved = in->mouse_x != U.prev_mx || in->mouse_y != U.prev_my;
  if (pressed_mouse || (N.moved && in->mouse_present && U.prev_mx + U.prev_my != 0)) now = in->mouse_from_touch ? DEV_TOUCH : DEV_MOUSE;
  U.prev_mx = in->mouse_x;
  U.prev_my = in->mouse_y;
  bool padany = false, keyany = false;
  for (int i = 0; i < CW_MAX_PADS; i++) {
    const cw_pad_state *p = &in->pads[i];
    if (p->connected && (p->buttons || fabsf(p->axes[CW_AXIS_LX]) > 0.4f || fabsf(p->axes[CW_AXIS_LY]) > 0.4f || p->axes[CW_AXIS_RT] > 0.4f ||
                         p->axes[CW_AXIS_LT] > 0.4f)) {
      now = DEV_PAD;
      padany = true;
    }
  }
  for (int k = 0; k < CW_KEY_COUNT; k++)
    if (in->keys_pressed[k]) {
      now = DEV_KEYS;
      keyany = true;
      break;
    }
  bool touchany = false;
  for (int t = 0; t < CW_MAX_TOUCHES; t++)
    if (in->touches[t].active || in->touches[t].tapped) now = DEV_TOUCH, touchany = true;
  if (now >= 0) U.device_used = true, U.device = now;
  else if (!U.device_used) U.device = cw_platform_touch_primary() ? DEV_TOUCH : DEV_PAD;
  (void)before;
  /* a click is seen once per rendered frame however many ticks that frame has */
  uint64_t fr = cw_frame_index();
  N.click = false;
  if (pressed_mouse && fr != U.click_frame) N.click = true, U.click_frame = fr;
  N.down = (in->mouse_buttons & 1) != 0;
  N.m = cw_r2d_window_to_canvas(cw_v2(in->mouse_x, in->mouse_y));
  N.any = padany || keyany || touchany || N.click;
}

static bool pointer_mode(void) { return U.device == DEV_MOUSE || U.device == DEV_TOUCH; }
static bool in_rect(cw_vec2 m, cw_rect r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }

/* ── drawing helpers ────────────────────────────────────────────────── */

typedef struct txopt {
  cw_text_align align;
  float outline;       /* 0: the default for the size */
  bool no_shadow;
} txopt;

static float tx_asc(cw_font f, float size) {
  cw_text_style st = { .size = size };
  return cw_font_measure(f, " ", &st).ascent;
}
static float tx_width(cw_font f, const char *s, float size) {
  cw_text_style st = { .size = size };
  return cw_font_measure(f, s, &st).width;
}
/* text with its cap-height centred on cy (so rows of different fonts line up) */
static void tx_o(cw_font f, const char *s, float x, float cy, float size, cw_color c, txopt o) {
  cw_text_style st = { .size = size, .color = c };
  st.outline_width = o.outline > 0 ? o.outline : size * 0.075f + 0.8f;
  st.outline_color = rgba(0.05f, 0.03f, 0.04f, c.a * 0.95f);
  if (!o.no_shadow) {
    st.shadow_color = rgba(0, 0, 0, 0.35f * c.a);
    st.shadow_offset = cw_v2(size * 0.04f + 1, size * 0.06f + 1.5f);
    st.shadow_spread = st.outline_width;
  }
  cw_text_metrics m = cw_font_measure(f, s, &st);
  if (o.align == CW_ALIGN_RIGHT) x -= m.width;
  else if (o.align == CW_ALIGN_CENTER) x -= m.width * 0.5f;
  cw_r2d_text(f, s, cw_v2(x, cy + 0.35f * size - m.ascent), &st);
}
static void tx(cw_font f, const char *s, float x, float cy, float size, cw_color c, cw_text_align al) { tx_o(f, s, x, cy, size, c, (txopt){ .align = al }); }
/* shrunk so it fits maxw (never grown) */
static void tx_fit(cw_font f, const char *s, float x, float cy, float size, float maxw, cw_color c, cw_text_align al) {
  float w = tx_width(f, s, size);
  if (w > maxw && w > 0) size *= maxw / w;
  tx(f, s, x, cy, size, c, al);
}
/* wrapped text in a box; returns the height used. Plain (no outline) so a paragraph stays calm. */
static float tx_para(cw_font f, const char *s, cw_rect r, float size, cw_color c, cw_text_align al) {
  cw_text_style st = { .size = size, .color = c, .wrap_width = r.w, .align = al, .line_spacing = 1.08f };
  st.outline_width = size * 0.045f + 0.4f;
  st.outline_color = rgba(0.05f, 0.03f, 0.04f, 0.85f * c.a);
  cw_text_metrics m = cw_r2d_text(f, s, cw_v2(r.x, r.y), &st);
  return m.height;
}
static float tx_para_h(cw_font f, const char *s, float w, float size) {
  cw_text_style st = { .size = size, .wrap_width = w, .line_spacing = 1.08f };
  return cw_font_measure(f, s, &st).height;
}

/* a parallelogram leaning right by skew (the comic slant) */
static void para_pts(cw_rect r, float skew, cw_vec2 p[4]) {
  p[0] = cw_v2(r.x + skew, r.y);
  p[1] = cw_v2(r.x + r.w, r.y);
  p[2] = cw_v2(r.x + r.w - skew, r.y + r.h);
  p[3] = cw_v2(r.x, r.y + r.h);
}
static void para(cw_rect r, float skew, cw_color c) {
  cw_vec2 p[4];
  para_pts(r, skew, p);
  cw_r2d_polygon(p, 4, c);
}
static void para_lines(cw_rect r, float skew, float w, cw_color c) {
  cw_vec2 p[4];
  para_pts(r, skew, p);
  cw_r2d_polygon_lines(p, 4, w, c);
}
/* a framed slab: hard shadow, fill, light top edge, ink outline */
static void slab(cw_rect r, float skew, cw_color fill, float s) {
  cw_rect sh = cw_rect_make(r.x + 4 * s, r.y + 5 * s, r.w, r.h);
  para(sh, skew, rgba(0, 0, 0, 0.42f * fill.a));
  para(r, skew, fill);
  para(cw_rect_make(r.x, r.y, r.w, r.h * 0.09f + 1), skew * 0.9f, rgba(1, 1, 1, 0.12f * fill.a));
  para_lines(r, skew, 2.5f * s + 0.5f, INK);
}
static void rslab(cw_rect r, float rad, cw_color fill, float s) {
  cw_r2d_round_rect(cw_rect_make(r.x + 3 * s, r.y + 4 * s, r.w, r.h), rad, 6, rgba(0, 0, 0, 0.4f * fill.a));
  cw_r2d_round_rect(r, rad, 6, fill);
  cw_r2d_round_rect_lines(r, rad, 2.5f * s + 0.5f, 6, INK);
}
/* a vertical gradient across a rect */
static void vgrad(cw_rect r, cw_color top, cw_color bot) {
  cw_vertex2d v[4] = { { { r.x, r.y }, { 0, 0 }, top }, { { r.x + r.w, r.y }, { 1, 0 }, top }, { { r.x + r.w, r.y + r.h }, { 1, 1 }, bot },
                       { { r.x, r.y + r.h }, { 0, 1 }, bot } };
  static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
}
static void hgrad(cw_rect r, cw_color left, cw_color right) {
  cw_vertex2d v[4] = { { { r.x, r.y }, { 0, 0 }, left }, { { r.x + r.w, r.y }, { 1, 0 }, right }, { { r.x + r.w, r.y + r.h }, { 1, 1 }, right },
                       { { r.x, r.y + r.h }, { 0, 1 }, left } };
  static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
}
/* hazard tape: diagonal yellow and ink stripes across a band, scrolling */
static void tape(cw_rect r, float s, float scroll) {
  cw_r2d_push_clip(r);
  cw_r2d_rect(r, MUSTARD);
  float period = r.h * 1.5f + 6 * s;
  for (float x = r.x - r.h - fmodf(scroll, period); x < r.x + r.w + r.h; x += period) {
    cw_vec2 p[4] = { { x, r.y + r.h }, { x + period * 0.5f, r.y + r.h }, { x + period * 0.5f + r.h, r.y }, { x + r.h, r.y } };
    cw_r2d_polygon(p, 4, INK);
  }
  cw_r2d_pop_clip();
  cw_r2d_rect_lines(r, 2 * s + 0.5f, INK);
}
static void tri(cw_vec2 c, float r, float ang, cw_color col) {
  cw_vec2 p[3];
  for (int i = 0; i < 3; i++) p[i] = cw_v2(c.x + r * cw_cos(ang + (float)i * 2.0944f), c.y + r * cw_sin(ang + (float)i * 2.0944f));
  cw_r2d_polygon(p, 3, col);
}
/* a chevron arrow pointing left (dir -1) or right (+1) */
static void arrow(cw_vec2 c, float r, int dir, cw_color col, float s) {
  tri(cw_v2(c.x + (float)dir * r * 0.18f, c.y), r, dir > 0 ? 0 : CW_PI, INK);
  tri(cw_v2(c.x + (float)dir * r * 0.18f, c.y), r * 0.78f, dir > 0 ? 0 : CW_PI, col);
  (void)s;
}

/* soft dark vignette and a vertical scrim over the 3D behind a menu */
static void scrim(float W, float H, float a) {
  vgrad(cw_rect_make(0, 0, W, H), rgba(0.02f, 0.01f, 0.03f, 0.55f * a), rgba(0.02f, 0.01f, 0.03f, 0.78f * a));
  hgrad(cw_rect_make(0, 0, W * 0.2f, H), rgba(0, 0, 0, 0.35f * a), rgba(0, 0, 0, 0));
  hgrad(cw_rect_make(W * 0.8f, 0, W * 0.2f, H), rgba(0, 0, 0, 0), rgba(0, 0, 0, 0.35f * a));
}

/* the layout scale: 1 at 960x540 (or 540x960 portrait) */
static bool is_portrait(float W, float H) { return W < H * 0.9f; }
static float lscale(float W, float H) { return is_portrait(W, H) ? fminf(W / 540.0f, H / 960.0f) : fminf(W / 960.0f, H / 540.0f); }

/* ── the save ───────────────────────────────────────────────────────── */

static uint32_t sum_n(size_t total, uint32_t tag) {
  const uint8_t *b = (const uint8_t *)&SAVE + offsetof(save_data, gfx);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < total - offsetof(save_data, gfx); i++) h = (h ^ b[i]) * 16777619u;
  return h ^ tag;
}
static uint32_t sum_of(void) { return sum_n(sizeof SAVE, SAVE.version | (uint32_t)SAVE.size << 16); }
static uint16_t save_size_field(void) { return (uint16_t)(sizeof SAVE / 4); }
static uint32_t gfx_saved(void) { return SAVE.gfx[0] | (uint32_t)SAVE.gfx[1] << 8 | (uint32_t)SAVE.gfx[2] << 16 | (uint32_t)SAVE.gfx[3] << 24; }
static void save_write(void) {
  uint32_t q = cw_quality_saved();
  if (q)
    for (int i = 0; i < 4; i++) SAVE.gfx[i] = (uint8_t)(q >> (8 * i));
  SAVE.laps = (uint8_t)U.laps, SAVE.skill = (uint8_t)U.skill, SAVE.items = (uint8_t)U.items, SAVE.map = (uint8_t)U.map;
  SAVE.speed_class = (uint8_t)U.cls, SAVE.assist = (uint8_t)U.assist, SAVE.ghost_on = (uint8_t)U.ghost_on, SAVE.cup_pick = (uint8_t)U.cup_sel;
  for (int i = 0; i < MAXV; i++) SAVE.veh[i] = (uint8_t)U.veh[i], SAVE.col[i] = (uint8_t)U.col[i];
  SAVE.checksum = sum_of();
  cw_save_commit();
}
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC;
  SAVE.version = SAVE_VERSION;
  SAVE.size = save_size_field();
  SAVE.music = 8, SAVE.sfx = 10;
  SAVE.laps = 3, SAVE.skill = 3, SAVE.items = 1;
  SAVE.ghost_on = 1;
  for (int i = 0; i < MAXV; i++) SAVE.veh[i] = (uint8_t)((i * 2 + i / 3) % NVEH), SAVE.col[i] = (uint8_t)i;
  SAVE.checksum = sum_of();
}
static int save_migrated; /* debug: the save loaded was a version 1 and was carried over */
static void save_check(void) {
  if (SAVE.magic == SAVE_MAGIC && SAVE.version == 1 && SAVE.size == SAVE_V1_SIZE && SAVE.checksum == sum_n(SAVE_V1_SIZE, 1u | (uint32_t)SAVE_V1_SIZE << 16)) {
    /* a version 1 save: everything it held stays (best laps, settings, the last race's choices); the new fields start empty */
    memset((uint8_t *)&SAVE + SAVE_V1_SIZE, 0, sizeof SAVE - SAVE_V1_SIZE);
    SAVE.version = SAVE_VERSION;
    SAVE.size = save_size_field();
    SAVE.ghost_on = 1;
    SAVE.checksum = sum_of();
    save_migrated = 1;
    cw_info("racer: save data carried over from version 1");
  }
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != save_size_field() || SAVE.checksum != sum_of()) {
    if (SAVE.magic) cw_warn("racer: save data rejected (magic %08x version %u), starting fresh", SAVE.magic, SAVE.version);
    save_reset();
  }
  if (SAVE.speed_class >= CLASS_COUNT) SAVE.speed_class = 0;
  if (SAVE.assist > 3) SAVE.assist = 0;
  if (SAVE.ghost_on > 1) SAVE.ghost_on = 1;
  if (SAVE.voice_off > 1) SAVE.voice_off = 0;
  if (SAVE.cup_pick >= NCUPS) SAVE.cup_pick = 0;
  for (int c = 0; c < NCUPS; c++)
    if (SAVE.cup_best[c] > 3) SAVE.cup_best[c] = 0;
  for (int m = 0; m < NMAPS; m++) {
    ghost_saved *g = &SAVE.ghost[m];
    if (g->n > GH_MAX || (g->n == 0) != (g->time_cs == 0)) memset(g, 0, sizeof *g);
  }
  if (SAVE.music > 10) SAVE.music = 10;
  if (SAVE.sfx > 10) SAVE.sfx = 10;
  if (SAVE.laps < 1 || SAVE.laps > MAX_LAPS) SAVE.laps = 3;
  if (SAVE.skill > 3) SAVE.skill = 3;
  if (SAVE.map >= NMAPS) SAVE.map = 0;
  for (int i = 0; i < MAXV; i++) {
    if (SAVE.veh[i] >= NVEH) SAVE.veh[i] = 0;
    if (SAVE.col[i] >= N_COLOURS) SAVE.col[i] = (uint8_t)i;
  }
}
cw_font ui_menu_font(void) { return U.f_hud; }
void *ui_save_ptr(void) { return &SAVE; }
size_t ui_save_size(void) { return sizeof SAVE; }

bool ui_dev_resolution(int *w, int *h) {
  const char *r = getenv("RACER_RES");
  if (!r || !strchr(r, 'x')) return false;
  *w = atoi(r);
  *h = atoi(strchr(r, 'x') + 1);
  return *w > 100 && *h > 100;
}

/* ── volume ─────────────────────────────────────────────────────────── */

static void apply_volumes(void) {
  audio_set_volume(ABUS_MUSIC, (float)SAVE.music / 10.0f);
  audio_set_volume(ABUS_SFX, (float)SAVE.sfx / 10.0f);
  audio_set_announcer(SAVE.voice_off == 0);
}

/* ── names ──────────────────────────────────────────────────────────── */

static const char *const COLOUR_NAME[N_COLOURS] = { "Red", "Blue", "Yellow", "Green", "Purple", "Orange", "Cyan", "White" };
static const char *const CPU_NAME[MAXK] = { "Rusty", "Sprocket", "Grit", "Nova", "Dusty", "Cinder", "Bolt", "Mags" };
static const char *const SKILL_NAME[4] = { "Rookie", "Pro", "Ace", "Mixed" };

/* the cups: four races each, the second opens when the first is won a trophy in. The maps are grouped wide and plain first, then the
 * tight, steep and strange ones. */
#define CUP_RACES 4
typedef struct cup_def {
  const char *name, *blurb;
  int map[CUP_RACES];
} cup_def;
static const cup_def CUPS[NCUPS] = {
  { "SCRAP CUP", "Wide open roads to learn the league on: salt flats, a refinery, a dam and a drowned city.", { 0, 3, 4, 1 } },
  { "WRECKER CUP", "Coaster loops, ice, a narrow temple and a broken station in orbit. Not for the careless.", { 2, 5, 6, 7 } },
};
static int cup_nraces(void);
static const char *const TROPHY_NAME[4] = { "NO TROPHY", "BRONZE TROPHY", "SILVER TROPHY", "GOLD TROPHY" };

static cw_color kart_col(int colour) {
  const float *c = KART_RGB[colour & 7];
  return rgba(c[0], c[1], c[2], 1);
}
static const char *place_suffix(int p) {
  if (p % 100 >= 11 && p % 100 <= 13) return "th";
  switch (p % 10) {
  case 1: return "st";
  case 2: return "nd";
  case 3: return "rd";
  default: return "th";
  }
}
static void fmt_time(char *b, size_t n, float secs) {
  if (secs <= 0) {
    snprintf(b, n, "--:--.--");
    return;
  }
  int cs = (int)(secs * 100 + 0.5f);
  snprintf(b, n, "%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
}

/* ── the track outlines ─────────────────────────────────────────────── */

static void mm_from_track(mmap_t *m, const track *t) {
  memset(m, 0, sizeof *m);
  if (!t || t->n < 8) return;
  m->length = t->length;
  m->minx = m->minz = m->miny = 1e9f;
  m->maxx = m->maxz = m->maxy = -1e9f;
  int n = MMN;
  float step = (float)(t->n - 1) / (float)n;
  for (int j = 0; j < n; j++) {
    int a = (int)((float)j * step), b = (int)((float)(j + 1) * step);
    if (b <= a) b = a + 1;
    if (b > t->n) b = t->n;
    const tsample *s = &t->s[a];
    m->x[j] = s->p.x, m->z[j] = s->p.z, m->h[j] = s->p.y;
    uint8_t fl = 0;
    for (int i = a; i < b; i++) {
      uint32_t f = t->s[i].flags;
      if (f & TF_GAP) fl |= 1;
      if (f & TF_LOOP) fl |= 2;
      if (f & TF_TUNNEL) fl |= 4;
      if (f & TF_BOOST) fl |= 8;
      if (f & TF_WATER) fl |= 16;
    }
    m->fl[j] = fl;
    m->minx = fminf(m->minx, s->p.x), m->maxx = fmaxf(m->maxx, s->p.x);
    m->minz = fminf(m->minz, s->p.z), m->maxz = fmaxf(m->maxz, s->p.z);
    m->miny = fminf(m->miny, s->p.y), m->maxy = fmaxf(m->maxy, s->p.y);
  }
  m->n = n;
  for (int b = 0; b < t->nbranch && b < 4; b++) {
    const track_branch *B = &t->br[b];
    int k = 0;
    for (int j = 0; j < 40; j++) {
      const tsample *s = &t->s[B->first + (B->n - 1) * j / 39];
      m->bx[b][k] = s->p.x, m->bz[b][k] = s->p.z, k++;
    }
    m->nbp[b] = k;
    m->nbr = b + 1;
  }
  m->njumps = t->ngaps, m->nloops = t->nloops;
  for (int j = 0; j < n; j++)
    if ((m->fl[j] & 4) && (j == 0 || !(m->fl[j - 1] & 4))) m->ntunnels++;
  m->ok = true;
}

/* builds a map's outline once (the track path only, no meshes) */
static const mmap_t *mm_get(int map) {
  if (map < 0 || map >= NMAPS) return NULL;
  mmap_t *m = &U.mm[map];
  if (m->ok) return m;
  track t;
  if (track_build(&t, MAPS[map]->spec)) {
    mm_from_track(m, &t);
    track_free(&t);
  }
  return m->ok ? m : NULL;
}

/* the outline of the race that is loaded (also the proving ground) */
static const mmap_t *mm_race(const sim *S) {
  if (S->map >= 0 && S->map < NMAPS) return mm_get(S->map);
  mmap_t *m = &U.mm[NMAPS];
  if (!m->ok && S->track) mm_from_track(m, S->track);
  return m->ok ? m : NULL;
}

/* world point -> a rect: the fit used by every minimap */
typedef struct mfit {
  float ox, oy, sc, minx, minz;
} mfit;
static mfit mm_fit(const mmap_t *m, cw_rect r, float pad) {
  float w = m->maxx - m->minx + 1e-3f, h = m->maxz - m->minz + 1e-3f;
  float sc = fminf((r.w - 2 * pad) / w, (r.h - 2 * pad) / h);
  mfit f = { r.x + r.w * 0.5f - w * sc * 0.5f, r.y + r.h * 0.5f - h * sc * 0.5f, sc, m->minx, m->minz };
  return f;
}
static cw_vec2 mm_pt(const mfit *f, float x, float z) { return cw_v2(f->ox + (x - f->minx) * f->sc, f->oy + (z - f->minz) * f->sc); }

/* the road itself: a dark casing, then the centre line coloured by height, jumps broken and
 * marked red, loops ringed in yellow, tunnels capped in grey, the line chequered */
static void mm_draw(const mmap_t *m, cw_rect r, float pad, float s, float width, bool marks) {
  if (!m || !m->ok) return;
  mfit f = mm_fit(m, r, pad);
  cw_vec2 pts[MMN];
  /* the shortcuts first, under the main line: a thinner pale line on its own casing */
  for (int b = 0; b < m->nbr; b++) {
    cw_vec2 bp[40];
    for (int i = 0; i < m->nbp[b]; i++) bp[i] = mm_pt(&f, m->bx[b][i], m->bz[b][i]);
    cw_r2d_polyline(bp, m->nbp[b], width + 4 * s, false, rgba(0.02f, 0.02f, 0.03f, 0.85f));
    cw_r2d_polyline(bp, m->nbp[b], width * 0.9f, false, rgba(0.55f, 1.0f, 0.5f, 1.0f));
  }
  for (int i = 0; i < m->n; i++) pts[i] = mm_pt(&f, m->x[i], m->z[i]);
  cw_r2d_polyline(pts, m->n, width + 4 * s, true, rgba(0.02f, 0.02f, 0.03f, 0.9f));
  float hr = m->maxy - m->miny + 1e-3f;
  for (int i = 0; i < m->n; i++) {
    int j = (i + 1) % m->n;
    float hh = clamp01((m->h[i] - m->miny) / hr);
    cw_color c = hh < 0.5f ? mix(rgba(0.30f, 0.80f, 0.95f, 1), rgba(0.95f, 0.88f, 0.35f, 1), hh * 2) : mix(rgba(0.95f, 0.88f, 0.35f, 1), rgba(1.0f, 0.45f, 0.30f, 1), (hh - 0.5f) * 2);
    if (m->fl[i] & 1) {
      if (i & 1) continue; /* a jump: dashes */
      c = rgba(1.0f, 0.22f, 0.22f, 1);
    } else if (m->fl[i] & 2 && marks) c = MUSTARD;
    cw_r2d_line(pts[i], pts[j], width, c);
    if ((m->fl[i] & 4) && marks) cw_r2d_line(pts[i], pts[j], width * 0.35f, rgba(0.12f, 0.12f, 0.14f, 0.9f));
  }
  if (!marks) return;
  /* a ring where a loop or corkscrew starts, and a bracket at a tunnel mouth */
  for (int i = 0; i < m->n; i++) {
    int p = (i + m->n - 1) % m->n;
    if ((m->fl[i] & 2) && !(m->fl[p] & 2)) {
      cw_r2d_circle_lines(pts[i], width * 1.5f + 2 * s, 2 * s + 0.5f, 14, MUSTARD);
    }
    if ((m->fl[i] & 4) && !(m->fl[p] & 4)) cw_r2d_circle(pts[i], width * 0.7f, 8, rgba(0.7f, 0.7f, 0.75f, 1));
    if ((m->fl[i] & 1) && !(m->fl[p] & 1)) cw_r2d_circle(pts[i], width * 0.8f, 8, rgba(1.0f, 0.22f, 0.22f, 1));
  }
  /* the start line */
  cw_vec2 a = pts[0], b = pts[1];
  cw_vec2 d = cw_v2_normalize(cw_v2_sub(b, a)), nrm = cw_v2(-d.y, d.x);
  float hw = width * 1.5f + 1;
  for (int k = 0; k < 4; k++) {
    float t0 = -1 + 0.5f * (float)k, t1 = t0 + 0.5f;
    cw_r2d_line(cw_v2(a.x + nrm.x * hw * t0, a.y + nrm.y * hw * t0), cw_v2(a.x + nrm.x * hw * t1, a.y + nrm.y * hw * t1), width * 0.9f + 1,
                (k & 1) ? INK : CREAM);
  }
}

/* ── prototypes between the three files ─────────────────────────────── */

static void hud_reset(void);
static void hud_tick(void);
static void hud_draw(float W, float H);
static void scr_tick(void);
static void scr_draw(float W, float H);
static void start_attract(int map);
static void goto_mode(int m);
static void begin_pending(int what);
/* ui_modes.inc: the time trial, the cups, the assists */
static void trial_retry(void);
static void trial_results_build(void);
static void cup_results_add(void);
static race_setup cup_race_setup(void);
static const ghost_saved *trial_ghost_for(int map);
static int trial_medal_of(int map, uint32_t cs);
static void draw_medal(cw_vec2 c, float r, int tier, float s, float t);
static void trial_detail(float x, float y, float ybot, float w, int map, float s);
static void assist_tag(cw_rect row, float s);
static void cup_open_standings(void);
static void tick_cup(float W, float H);
static void draw_cup(float W, float H, float s);
static void tick_standings(float W, float H);
static void draw_standings(float W, float H, float s);
static void tick_cupend(float W, float H);
static void draw_cupend(float W, float H, float s);
static bool cup_unlocked(int cup);
/* ui_battle.inc: battle mode's arena select, HUD and results */
static void battle_hud_event(int v, const struct sim_event *e, const sim *S);
static void battle_hud_tick(void);
static void hud_battle_shared(float W, float H);
static void tick_arena(float W, float H);
static void draw_arena(float W, float H, float s);
static void battle_results_build(void);
static void tick_battle_results(float W, float H);
static void draw_battle_results(float W, float H, float s);

#include "ui_hud.inc"
#include "ui_screens.inc"
#include "ui_modes.inc"
#include "ui_battle.inc"

/* ── the screen flow ────────────────────────────────────────────────── */

void ui_init(void) {
  U.a_start = cw_action_find("start");
  U.a_back = cw_action_find("back");
  U.m_left = cw_action_find("m_left");
  U.m_right = cw_action_find("m_right");
  U.m_up = cw_action_find("m_up");
  U.m_down = cw_action_find("m_down");
  U.m_ok = cw_action_find("m_ok");
  U.a_fwd = cw_action_find("thr_fwd");
  U.a_brake = cw_action_find("thr_back");
  U.a_left = cw_action_find("steer_l");
  U.a_right = cw_action_find("steer_r");
  U.a_pup = cw_action_find("pitch_up");
  U.a_pdn = cw_action_find("pitch_dn");
  U.a_drift = cw_action_find("drift");
  U.a_item = cw_action_find("item");
  U.a_trick = cw_action_find("trick");
  const cw_font_desc sdf = { .mode = CW_FONT_SDF, .sdf_padding = 8 };
  U.f_title = cw_font_load("fonts/Bungee-Regular.ttf", &sdf);
  U.f_hud = cw_font_load("fonts/Fredoka-Bold.ttf", &sdf);
  U.f_small = cw_font_load("fonts/Oswald-VF.ttf", &sdf);
  const cw_font fonts[] = { U.f_title, U.f_hud, U.f_small };
  for (unsigned f = 0; f < sizeof fonts / sizeof fonts[0]; f++) cw_font_preload_range(fonts[f], 32, 126);

  save_check();
  cw_quality_restore(gfx_saved()); /* the player's graphics settings if they chose any; else Auto */
  cw_quality_auto(true, 0);
  U.laps = SAVE.laps, U.skill = SAVE.skill, U.items = SAVE.items, U.map = SAVE.map;
  if (getenv("RACER_UI_ITEMS")) U.items = atoi(getenv("RACER_UI_ITEMS")) % 3; /* dev aid: the item-box setting the screens show */
  U.cls = SAVE.speed_class, U.assist = SAVE.assist, U.ghost_on = SAVE.ghost_on, U.cup_sel = SAVE.cup_pick;
  U.gmode = RM_RACE;
  U.bt.karts = MAXK, U.bt.secs_i = 2;
  for (int i = 0; i < MAXV; i++) U.veh[i] = SAVE.veh[i], U.col[i] = SAVE.col[i];
  apply_volumes();
  const char *pl = getenv("RACER_PLAYERS");
  U.split = pl ? CW_CLAMP(atoi(pl), 1, MAXV) : 1;
  const char *lp = getenv("RACER_LAPS");
  if (lp) U.laps = CW_CLAMP(atoi(lp), 1, MAX_LAPS);

  /* the ranges the stat bars span: the vehicles' own, padded, so a retune keeps the bars sensible */
  float lo[7], hi[7];
  for (int k = 0; k < 7; k++) lo[k] = 1e9f, hi[k] = -1e9f;
  for (int v = 0; v < NVEH; v++) {
    float x[7] = { VEH[v].top, VEH[v].accel, VEH[v].turn, VEH[v].grip, VEH[v].mass, VEH[v].drift, VEH[v].boost };
    for (int k = 0; k < 7; k++) lo[k] = fminf(lo[k], x[k]), hi[k] = fmaxf(hi[k], x[k]);
  }
  for (int k = 0; k < 7; k++) {
    float pad = (hi[k] - lo[k]) * 0.25f + 1e-3f;
    U.stat_lo[k] = lo[k] - pad, U.stat_hi[k] = hi[k] + pad * 0.4f;
  }
  scr_init();
  cw_debug_i32("ui.mode", &U.mode);
  cw_debug_i32("ui.device", &U.device); /* 0 mouse, 1 pad, 2 keys, 3 touch */
  cw_debug_i32("ui.pending", &U.pending);
  cw_debug_i32("ui.sel", &U.sel);
  cw_debug_field("save.version", &SAVE.version, CW_DBG_U16, 1);
  cw_debug_i32("save.migrated", &save_migrated);
  cw_debug_field("save.races", &SAVE.races, CW_DBG_U32, 1);
  cw_debug_field("save.best_lap0", &SAVE.best_lap_cs[0], CW_DBG_U32, 1);
  cw_debug_field("save.trial_cs", SAVE.trial_cs, CW_DBG_U32, NMAPS);
  cw_debug_i32("trial.runs", &U.tr.runs);
  cw_debug_i32("trial.medal", &U.tr.medal);
  cw_debug_i32("trial.gap_cs", &U.tr.gap_cs);
  cw_debug_u32("trial.last_cs", &U.tr.cs);
  cw_debug_u32("trial.best_cs", &U.tr.best_cs);
  cw_debug_i32("cup.race", &U.cup.race);
  cw_debug_field("cup.pts", U.cup.pts, CW_DBG_I32, MAXK);
  cw_debug_i32("cup.best", &U.cup.best);
  U.mode = M_TITLE;
  U.attract_map = 0;
  U.t_stick = U.t_gas = U.t_brake = U.t_drift = U.t_item = U.t_trick = -1;
  touch_setup();
}

void ui_shutdown(void) {}

static void goto_mode(int m) {
  U.mode = m;
  U.mt = 0;
  U.sel = 0;
}

static void start_attract(int map) {
  race_setup rs = { .map = map, .laps = 3, .humans = 0, .cpu_skill = 3, .items = true, .attract = true };
  U.tr.on = false;
  U.cup.on = false;
  game_start_race(&rs);
  game_set_camera(1);
  U.attract_on = game_loaded();
  U.attract_ticks = 0;
  U.attract_map = map;
}

static int next_attract_map(void) { return (U.attract_map + 3) % NMAPS; }

void ui_enter_race(const race_setup *rs) {
  U.setup = *rs;
  U.last = *rs;
  game_start_race(rs);
  U.attract_on = false;
  goto_mode(M_RACE);
  hud_reset();
  U.saved_this_race = false;
  U.tr.on = rs->mode == RM_TRIAL && !rs->attract && game_is_trial();
  U.tr.map = rs->map;
  U.tr.runs = 0;
  for (int m = 0; m < NMAPS; m++) U.rec_at_start[m] = (int)SAVE.best_lap_cs[m];
  U.fade = 1;
  U.fade_dir = -1;
  /* the humans' input players: human i is read from player i (join may have remapped the pads) */
}

bool ui_race_runs(void) {
  switch (U.mode) {
  case M_PAUSE: return false;
  case M_RACE: return true;
  case M_RESULTS: return U.tr.on || U.res_ticks < 15 * 60; /* the winner's lap of honour, then the scene holds still (a trial's kart cruises on) */
  case M_STANDINGS:
  case M_CUPEND: return U.mt < 30 * 60;
  default: return game_loaded() && U.attract_on;
  }
}

void ui_race_over(void) {
  if (U.mode == M_RACE) results_build();
  else if (U.attract_on && U.mode != M_RESULTS) begin_pending(PD_ATTRACT);
}

static void begin_pending(int what) {
  if (U.pending != PD_NONE) return;
  U.pending = what;
  U.fade_dir = 1;
}

/* the heavy change that waited for the screen to go black */
static void run_pending(void) {
  int what = U.pending;
  U.pending = PD_NONE;
  switch (what) {
  case PD_START_RACE: {
    race_setup rs = U.setup;
    ui_enter_race(&rs);
    break;
  }
  case PD_RESTART: {
    race_setup rs = U.last;
    ui_enter_race(&rs);
    break;
  }
  case PD_NEXT_MAP: {
    race_setup rs = U.last;
    if (rs.mode == RM_BATTLE) {
      rs.map = (rs.map + 1) % NARENAS;
      U.bt.arena = rs.map;
      ui_enter_race(&rs);
      break;
    }
    rs.map = (rs.map + 1) % NMAPS;
    U.map = rs.map;
    if (rs.mode == RM_TRIAL) rs.ghost = trial_ghost_for(rs.map);
    ui_enter_race(&rs);
    break;
  }
  case PD_CUP_NEXT: {
    race_setup rs = cup_race_setup();
    ui_enter_race(&rs);
    break;
  }
  case PD_TO_MENU:
    U.cup.on = false;
    U.tr.on = false;
    game_end_race();
    restore_pads();
    goto_mode(M_TITLE);
    U.title_menu = 1;
    U.sel = 0;
    start_attract(next_attract_map());
    audio_music(0);
    break;
  case PD_ATTRACT:
    start_attract(next_attract_map());
    break;
  default: break;
  }
  U.fade_dir = -1;
}

static void fade_tick(void) {
  if (U.fade_dir > 0) {
    U.fade += 1.0f / 14.0f;
    if (U.fade >= 1) {
      U.fade = 1;
      run_pending();
    }
  } else if (U.fade_dir < 0) {
    U.fade -= 1.0f / 22.0f;
    if (U.fade <= 0) U.fade = 0, U.fade_dir = 0;
  }
}

static void dev_screen(void) {
  const char *d = getenv("RACER_UI");
  if (!d || U.dev_done) return;
  U.dev_done = true;
  int n = U.split;
  if (!strcmp(d, "title")) return;
  if (!strcmp(d, "menu")) U.title_menu = 1;
  else if (!strcmp(d, "join")) {
    goto_mode(M_JOIN);
    for (int i = 0; i < n; i++) U.joined[i] = true;
  } else if (!strcmp(d, "vehicle")) {
    U.setup.humans = n;
    goto_mode(M_VEHICLE);
  } else if (!strcmp(d, "map")) goto_mode(M_MAP);
  else if (!strcmp(d, "arena")) {
    /* the battle's arena select (RACER_UI_MAP picks the arena) */
    U.gmode = RM_BATTLE;
    U.setup.humans = n;
    if (getenv("RACER_UI_MAP")) U.bt.arena = atoi(getenv("RACER_UI_MAP")) % NARENAS;
    goto_mode(M_ARENA);
  } else if (!strcmp(d, "bpause")) {
    /* a battle paused (RACER_UI_MAP picks the arena) */
    race_setup rs = { .map = getenv("RACER_UI_MAP") ? atoi(getenv("RACER_UI_MAP")) % NARENAS : 0, .laps = 1, .humans = n, .cpu_skill = 3, .items = true, .mode = RM_BATTLE };
    for (int i = 0; i < MAXV; i++) rs.vehicle[i] = U.veh[i], rs.colour[i] = U.col[i];
    ui_enter_race(&rs);
    U.fade = 0;
    U.fade_dir = 0;
    goto_mode(M_PAUSE);
  }
  else if (!strcmp(d, "options")) U.from = M_TITLE, goto_mode(M_OPTIONS);
  else if (!strcmp(d, "controls")) U.from = M_TITLE, goto_mode(M_CONTROLS);
  else if (!strcmp(d, "trial")) {
    U.gmode = RM_TRIAL;
    U.setup.humans = 1;
    if (getenv("RACER_UI_MAP")) U.map = atoi(getenv("RACER_UI_MAP")) % NMAPS;
    goto_mode(M_MAP);
    /* the medals of a few maps, so the cards show them */
    SAVE.trial_cs[0] = medal_time(0, MEDAL_GOLD) - 10, SAVE.trial_cs[1] = medal_time(1, MEDAL_SILVER) - 10, SAVE.trial_cs[2] = medal_time(2, MEDAL_AUTHOR) - 10;
    SAVE.trial_cs[3] = medal_time(3, MEDAL_BRONZE) - 10, SAVE.trial_cs[4] = medal_time(4, MEDAL_BRONZE) + 500;
  } else if (!strcmp(d, "cup")) {
    U.gmode = RM_CUP;
    U.setup.humans = n;
    if (getenv("RACER_UI_CUP")) U.cup_sel = atoi(getenv("RACER_UI_CUP")) % NCUPS;
    if (getenv("RACER_UI_UNLOCK")) SAVE.cup_best[0] = (uint8_t)atoi(getenv("RACER_UI_UNLOCK"));
    goto_mode(M_CUP);
  } else if (!strcmp(d, "assist")) {
    U.from = M_TITLE;
    U.opt_sel = OI_ASSIST_STEER;
    goto_mode(M_OPTIONS);
  } else if (!strcmp(d, "trialres")) {
    int map = getenv("RACER_UI_MAP") ? atoi(getenv("RACER_UI_MAP")) % NMAPS : 0;
    race_setup rs = { .map = map, .laps = 1, .humans = 1, .cpu_skill = 3, .mode = RM_TRIAL };
    for (int i = 0; i < MAXV; i++) rs.vehicle[i] = U.veh[i], rs.colour[i] = U.col[i];
    ui_enter_race(&rs);
    U.fade = 0;
    U.fade_dir = 0;
    int tier = getenv("RACER_UI_MEDAL") ? atoi(getenv("RACER_UI_MEDAL")) : 3;
    uint32_t cs = tier >= MEDAL_BRONZE ? medal_time(map, tier) - 20 : medal_time(map, MEDAL_BRONZE) + 900;
    sim *S = (sim *)game_sim();
    S->k[0].finished = true;
    S->k[0].finish_tick = (int)(cs * TICK_HZ / 100);
    S->phase = PH_DONE;
    bool rec = !getenv("RACER_UI_RECORD") || atoi(getenv("RACER_UI_RECORD"));
    SAVE.trial_cs[map] = rec ? 0 : cs - 137;
    trial_results_build();
    if (getenv("RACER_UI_GHOST")) U.tr.had_ghost = true, U.tr.ghost_cs = (uint32_t)((int)cs - atoi(getenv("RACER_UI_GHOST"))), U.tr.gap_cs = atoi(getenv("RACER_UI_GHOST"));
    U.res_ticks = 200;
  } else if (!strcmp(d, "standings") || !strcmp(d, "cupend")) {
    memset(&U.cup, 0, sizeof U.cup);
    U.cup.on = true;
    U.cup.cup = getenv("RACER_UI_CUP") ? atoi(getenv("RACER_UI_CUP")) % NCUPS : 0;
    U.cup.race = !strcmp(d, "cupend") ? CUP_RACES - 1 : getenv("RACER_UI_RACE") ? atoi(getenv("RACER_UI_RACE")) : 1;
    U.cup.humans = n;
    U.cup.counted = U.cup.race;
    int want = getenv("RACER_UI_PLACE") ? CW_CLAMP(atoi(getenv("RACER_UI_PLACE")), 1, 8) : 2;
    /* kart 0 ends where asked; the others take the other places in kart order */
    int rank_of[MAXK], r = 1;
    for (int i = 0; i < MAXK; i++) {
      if (i == 0) rank_of[i] = want;
      else {
        if (r == want) r++;
        rank_of[i] = r++;
      }
    }
    for (int i = 0; i < MAXK; i++) {
      U.cup.pts[i] = 90 - 10 * rank_of[i] + (i * 3) % 4;
      U.cup.race_pts[i] = POINTS[(rank_of[i] + (i % 3)) % MAXK];
      U.cup.last_place[i] = rank_of[i];
    }
    for (int i = 0; i < MAXV; i++) U.col[i] = U.col[i] & 7;
    if (!strcmp(d, "cupend")) {
      if (getenv("RACER_UI_UNLOCK_NOW")) SAVE.cup_best[0] = 0;
      cup_finish();
      U.res_ticks = 200;
    } else cup_open_standings();
    U.mt = 120;
  } else if (!strcmp(d, "pause") || !strcmp(d, "results")) {
    race_setup rs = { .map = getenv("RACER_UI_MAP") ? atoi(getenv("RACER_UI_MAP")) % NMAPS : 0, .laps = getenv("RACER_LAPS") ? atoi(getenv("RACER_LAPS")) : 1,
                      .humans = n, .cpu_skill = 3, .items = true };
    for (int i = 0; i < MAXV; i++) rs.vehicle[i] = U.veh[i], rs.colour[i] = U.col[i];
    ui_enter_race(&rs);
    U.fade = 0;
    U.fade_dir = 0;
    if (!strcmp(d, "pause")) goto_mode(M_PAUSE);
    else if (getenv("RACER_UI_FAKE")) fake_results();
  }
}

void ui_fixed(void) {
  U.ticks++;
  U.mt++;
  U.time = (float)U.ticks / 60.0f;
  read_devices();
  fade_tick();
  /* the dev screen is chosen before the demo race loads */
  if (U.ticks == 1 && getenv("RACER_UI")) dev_screen();
  /* the demo race behind the front end (not when a dev run starts a race by itself) */
  if (U.ticks == 2 && !getenv("RACER_MAP") && !game_loaded() && U.mode != M_RACE && U.mode != M_PAUSE && U.mode != M_RESULTS) {
    start_attract(U.attract_map);
    audio_music(0);
  }
  if (U.attract_on && game_loaded() && U.mode != M_RACE) {
    if (++U.attract_ticks > ATTRACT_SECS * 60 && U.pending == PD_NONE && U.fade_dir == 0) begin_pending(PD_ATTRACT);
  }
  if (cw_quality_menu_is_open()) {
    /* the graphics page has the presses */
    cw_quality_menu_update();
    if (!cw_quality_menu_is_open()) {
      sfx_back();
      if (cw_quality_menu_changed()) save_write();
    }
    return;
  }
  touch_tick();
  hud_tick();
  scr_tick();
}

void ui_render(float W, float H) {
  U.time = (float)U.ticks / 60.0f;
  hud_draw(W, H);
  scr_draw(W, H);
  if (U.fade > 0) cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.01f, 0.01f, 0.02f, ease_io(U.fade)));
}
