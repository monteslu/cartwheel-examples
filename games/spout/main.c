/* spout: a papercraft spider climbs a rain spout, on cartwheel and three.c
 * (three.js r186).
 *
 *   flow      title -> spout select (padlocked in order) -> climb -> top
 *   climbing  the stick or WASD / arrows: up climbs, left / right walk round
 *             the spout; A / Space jumps off the paper; B / Shift braces.
 *             Touch: a floating stick on the left, JUMP and BRACE on the
 *             right, shown from the first frame on a phone.
 *   weather   sun, a warning, rain, clearing. Rain soaks the paper wraps
 *             (slick: you slide) and sends waves down the streams (brace or
 *             be washed down to the last bracket); the sun dries them.
 *   save      per spout: cleared, best time, dewdrops (cw_app_desc.save).
 *
 * Determinism: the climb replays identically from the input stream (fixed
 * ticks, sim.c on cartwheel/math.h); the debug fields name what the gates
 * compare across hosts. spout_auto climbs by itself (for hosts that send no input). */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/platform.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "cartwheel/vfs.h"
#include "game.h"

enum { GS_TITLE, GS_SELECT, GS_PLAY, GS_PAUSE, GS_WIN };

/* ── save ───────────────────────────────────────────────────────────── */

#define SAVE_MAGIC 0x54554f53u /* 'SOUT' */
#define SAVE_VERSION 3
typedef struct save_data {
  uint32_t magic;
  uint16_t version, size;
  struct { int32_t cleared, best_ticks, dews, flies; } lv[SP_LEVELS];
  uint8_t quality[4]; /* the graphics settings: cw_quality_saved() little-endian, 0 = none chosen */
  uint32_t checksum;
} save_data;
static save_data SAVE;

static uint32_t save_sum(void) { return cw_hash32(&SAVE.lv, offsetof(save_data, checksum) - offsetof(save_data, lv), CW_HASH32_INIT) ^ (SAVE.version | (uint32_t)SAVE.size << 16); }
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC;
  SAVE.version = SAVE_VERSION;
  SAVE.size = sizeof SAVE;
  SAVE.checksum = save_sum();
}
static uint32_t gfx_saved(void) {
  return SAVE.quality[0] | (uint32_t)SAVE.quality[1] << 8 | (uint32_t)SAVE.quality[2] << 16 | (uint32_t)SAVE.quality[3] << 24;
}
static void save_write(void) {
  uint32_t q = cw_quality_saved();
  if (q)
    for (int i = 0; i < 4; i++) SAVE.quality[i] = (uint8_t)(q >> (8 * i));
  SAVE.checksum = save_sum();
  cw_save_commit();
}
static void save_check(void) {
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) save_reset();
}
static bool level_open(int i) { return i < LEVELS_BUILT && (i == 0 || SAVE.lv[i - 1].cleared); }

/* ── game state ─────────────────────────────────────────────────────── */

static struct {
  int32_t state, state_ticks, ticks, level, cursor;
  sim S, prev;
  cw_node scene, camera, sky, sun;
  cw_action a_left, a_right, a_up, a_down, a_jump, a_brace, a_silk, a_start, a_back, a_pause;
  bool stopmotion;
  int32_t locked_bumps;
  uint32_t state_hash;
  float spider_pos[3];
  int32_t result_ticks, new_best;
  int title_level;
} G;

static int32_t input_mode, dbg_unlocked, dbg_cleared;
static bool device_used;
enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
static int last_device = DEV_MOUSE;
static uint32_t prev_mouse;
static bool mouse_clicked;

/* ── on-screen touch controls (logical window coordinates) ─────────── */

static int T_STICK = -1, T_JUMP = -1, T_BRACE = -1, T_PAUSE = -1, T_SILK = -1;
static void touch_rects(float *jx, float *jy, float *jr, float *bx, float *by, float *br) {
  int w, h;
  cw_window_size(&w, &h);
  float W = (float)w, H = (float)h, k = H / 540.0f;
  *jr = 84 * k, *jx = W - 110 * k, *jy = H - 120 * k;
  *br = 64 * k, *bx = W - 250 * k, *by = H - 90 * k;
}
static void silk_rect(float *sx, float *sy, float *sr) {
  int w, h;
  cw_window_size(&w, &h);
  float W = (float)w, H = (float)h, k = H / 540.0f;
  *sr = 56 * k, *sx = W - 130 * k, *sy = H - 280 * k;
}
static bool touch_live; /* the on-screen controls only claim touches while climbing */
static void touch_layout(void) {
  int w, h;
  cw_window_size(&w, &h);
  float W = (float)w, H = (float)h, k = H / 540.0f, jx, jy, jr, bx, by, br;
  touch_rects(&jx, &jy, &jr, &bx, &by, &br);
  if (T_STICK < 0) {
    T_STICK = cw_touch_add_stick(0, H * 0.2f, W * 0.5f, H * 0.8f, 60 * k, true);
    T_JUMP = cw_touch_add_button(jx - jr, jy - jr, jr * 2, jr * 2);
    T_BRACE = cw_touch_add_button(bx - br, by - br, br * 2, br * 2);
    T_PAUSE = cw_touch_add_button(14 * k, 14 * k, 64 * k, 52 * k);
    float sx, sy, sr;
    silk_rect(&sx, &sy, &sr);
    T_SILK = cw_touch_add_button(sx - sr, sy - sr, sr * 2, sr * 2);
    char b[48];
    static const char *const DIRS[4] = { "left", "right", "up", "down" };
    cw_action dirs[4] = { G.a_left, G.a_right, G.a_up, G.a_down };
    for (int d = 0; d < 4; d++) {
      snprintf(b, sizeof b, "touch:%d.%s", T_STICK, DIRS[d]);
      cw_action_bind_text(dirs[d], b);
    }
    snprintf(b, sizeof b, "touch:%d", T_JUMP);
    cw_action_bind_text(G.a_jump, b);
    snprintf(b, sizeof b, "touch:%d", T_BRACE);
    cw_action_bind_text(G.a_brace, b);
    snprintf(b, sizeof b, "touch:%d", T_PAUSE);
    cw_action_bind_text(G.a_pause, b);
    snprintf(b, sizeof b, "touch:%d", T_SILK);
    cw_action_bind_text(G.a_silk, b);
  } else if (touch_live) {
    float sx, sy, sr;
    silk_rect(&sx, &sy, &sr);
    cw_touch_set_rect(T_SILK, sx - sr, sy - sr, sr * 2, sr * 2);
    cw_touch_set_rect(T_STICK, 0, H * 0.2f, W * 0.5f, H * 0.8f);
    cw_touch_set_rect(T_JUMP, jx - jr, jy - jr, jr * 2, jr * 2);
    cw_touch_set_rect(T_BRACE, bx - br, by - br, br * 2, br * 2);
    cw_touch_set_rect(T_PAUSE, 14 * k, 14 * k, 64 * k, 52 * k);
  } else { /* menus: every tap is the pointer's */
    cw_touch_set_rect(T_STICK, 0, 0, 0, 0);
    cw_touch_set_rect(T_JUMP, 0, 0, 0, 0);
    cw_touch_set_rect(T_BRACE, 0, 0, 0, 0);
    cw_touch_set_rect(T_PAUSE, 0, 0, 0, 0);
    cw_touch_set_rect(T_SILK, 0, 0, 0, 0);
  }
}

static void read_devices(void) {
  const cw_raw_input *in = cw_input_raw();
  int before = last_device;
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse & 1);
  if (mouse_clicked) last_device = in->mouse_from_touch ? DEV_TOUCH : DEV_MOUSE;
  prev_mouse = in->mouse_buttons;
  for (int i = 0; i < CW_MAX_TOUCHES; i++)
    if (in->touches[i].active) last_device = DEV_TOUCH;
  for (int i = 0; i < CW_MAX_PADS; i++) {
    const cw_pad_state *p = &in->pads[i];
    if (p->connected && (p->buttons || fabsf(p->axes[CW_AXIS_LX]) > 0.3f || fabsf(p->axes[CW_AXIS_LY]) > 0.3f)) last_device = DEV_PAD;
  }
  for (int k = 0; k < CW_KEY_COUNT; k++)
    if (in->keys_pressed[k]) {
      last_device = DEV_KEYS;
      break;
    }
  if (last_device != before) device_used = true;
  if (!device_used) last_device = cw_platform_touch_primary() ? DEV_TOUCH : DEV_MOUSE;
  input_mode = last_device;
}

/* ── audio (optional: the game runs silent if the files are missing) ── */

enum { SFX_JUMP, SFX_LAND, SFX_SPLASH, SFX_CHECK, SFX_DEW, SFX_WIN, SFX_SELECT, SFX_LOCKED, SFX_RAIN_IN, SFX_SUN_IN, SFX_HOLD, SFX_ZIP, SFX_TWEET, SFX_PECK, SFX_COUNT };
static const char *const SFX_FILES[SFX_COUNT] = { "audio/jump.wav", "audio/land.wav", "audio/splash.wav", "audio/check.wav",
                                                  "audio/dew.wav", "audio/win.wav", "audio/select.wav", "audio/locked.wav",
                                                  "audio/thunder.wav", "audio/chime.wav", "audio/hold.wav", "audio/zip.wav",
                                                  "audio/tweet.wav", "audio/peck.wav" };
static struct {
  bool on;
  cw_bus sfx_bus, music_bus, rain_bus;
  cw_sound sfx[SFX_COUNT];
  cw_voice music, rain;
  int music_theme;
  int cooldown[SFX_COUNT];
} A;
static void sfx(int id, float vol) {
  if (!A.on || !A.sfx[id].id || A.cooldown[id] > 0) return;
  A.cooldown[id] = 4;
  cw_audio_play(A.sfx[id], &(cw_play_desc){ .bus = A.sfx_bus, .volume = vol });
}
static void audio_init(void) {
  A.on = getenv("SPOUT_NO_AUDIO") == NULL && cw_vfs_exists("audio/music_spring.ogg") && cw_audio_init(&(cw_audio_desc){ 0 });
  if (!A.on) return;
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  A.rain_bus = cw_bus_create("rain", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.4f);
  cw_bus_set_volume(A.rain_bus, 0);
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1, 0.1f } });
  for (int i = 0; i < SFX_COUNT; i++)
    if (cw_vfs_exists(SFX_FILES[i]))
      A.sfx[i] = cw_res_audio_sound(cw_res_load(SFX_FILES[i], 0), &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = 3, .bus = A.sfx_bus });
  A.music_theme = -1;
  if (cw_vfs_exists("audio/rain.ogg")) {
    cw_sound r = cw_res_audio_sound(cw_res_load("audio/rain.ogg", 0), &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.rain_bus });
    A.rain = cw_audio_play(r, &(cw_play_desc){ .loop = true, .bus = A.rain_bus });
  }
}

/* ── levels ─────────────────────────────────────────────────────────── */

static const char *const THEME_NAMES[THEME_COUNT] = { "spring", "city", "autumn", "winter", "jungle" };

/* each spout has its own music; crossfade when the theme changes */
static void music_for(int theme) {
  if (!A.on || A.music_theme == theme) return;
  if (A.music_theme >= 0) cw_voice_stop(A.music, 1.0f);
  char path[64];
  snprintf(path, sizeof path, "audio/music_%s.ogg", THEME_NAMES[theme]);
  cw_sound m = cw_res_audio_sound(cw_res_load(path, 0), &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.music_bus });
  A.music = cw_audio_play(m, &(cw_play_desc){ .loop = true, .fade_in = 1.5f, .bus = A.music_bus });
  A.music_theme = theme;
}

static void load_world(int level, bool title) {
  if (cw_node_valid(G.scene)) {
    world_free();
    cw_node_free(G.scene);
  }
  char scene[64];
  snprintf(scene, sizeof scene, "scenes/spout_%s.cwscene", THEME_NAMES[LEVELS[level].theme]);
  G.scene = cw_scene_instance_path(scene);
  music_for(LEVELS[level].theme);
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), G.scene);
  G.camera = cw_node_get(G.scene, "Camera");
  G.sky = cw_node_get(G.scene, "Sky");
  G.sun = cw_node_get(G.scene, "Sun");
  world_build(G.scene, &LEVELS[level], title);
  G.level = level;
  sim_start(&G.S, &LEVELS[level], 0x51ed0u + (uint32_t)level);
  if (title) G.S.s.y = 1.2f, G.S.s.th = 0.3f;
  G.prev = G.S;
}

static void start_level(int level) {
  load_world(level, false);
  G.state = GS_PLAY;
  G.state_ticks = 0;
  G.new_best = 0;
}

/* ── autopilot (spout_auto): climbs by itself from the sim's own state ── */

#ifdef SPOUT_AUTOPILOT
static sim_input autopilot(const sim *S) {
  sim_input in = { 0 };
  const spider *s = &S->s;
  const level_def *L = S->L;
  in.y = 1;
  /* brace while a wave is about to reach us in our stream */
  for (int i = 0; i < 8; i++) {
    const wave *w = &S->waves[i];
    if (w->live && fabsf(wrap_pi(s->th - S->stream_th[w->stream])) < 0.5f && w->y > s->y && w->y - s->y < 1.2f) in.brace = true;
  }
  /* a slick wrap just above: jump it, or walk round it to its gap */
  int which;
  if (!in.brace && s->state == SP_CLING && sim_on_patch(S, s->th, s->y + 0.5f, &which)) {
    const patch *p = &L->patches[which];
    bool slick = !p->dries || S->wet > 0.5f;
    float ay;
    if (slick && S->silk > 0 && sim_anchor_above(S, &ay) && ay > p->y1) in.silk = (S->tick / 20) % 2 == 0;
    else if (slick && p->y1 - p->y0 <= 1.0f && !sim_on_patch(S, s->th, s->y - 0.05f, NULL)) in.jump = (S->tick / 30) % 2 == 0;
    else if (slick) in.x = wrap_pi(3.1415927f - s->th) > 0 ? 1 : -1, in.y = 0.2f;
  }
  return in;
}
#endif

/* ── the app ────────────────────────────────────────────────────────── */

static cw_font font_ui, font_title;
static void sync_hook(cw_tree *t, double alpha, void *u);

static void init(void *u) {
  (void)u;
  save_check();
  cw_quality_restore(gfx_saved()); /* the player's graphics settings, if they chose any; else Auto */
  cw_quality_auto(true, 0);
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("spout: input.cfg had errors");
  cw_input_commit_defaults();
  G.a_left = cw_action_find("left");
  G.a_right = cw_action_find("right");
  G.a_up = cw_action_find("up");
  G.a_down = cw_action_find("down");
  G.a_jump = cw_action_find("jump");
  G.a_brace = cw_action_find("brace");
  G.a_silk = cw_action_find("silk");
  G.a_start = cw_action_find("start");
  G.a_back = cw_action_find("back");
  G.a_pause = cw_action_find("pause");
  touch_layout();
  audio_init();
  font_ui = cw_font_load("fonts/PatrickHand-Regular.ttf", NULL);
  font_title = cw_font_load("fonts/Sniglet-ExtraBold.ttf", NULL);
  G.stopmotion = true;
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.level", &G.level);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("input.mode", &input_mode);
  cw_debug_i32("spider.state", &G.S.s.state);
  cw_debug_i32("checkpoint", &G.S.checkpoint);
  cw_debug_i32("weather", &G.S.weather);
  cw_debug_i32("washes", &G.S.washes);
  cw_debug_i32("knocks", &G.S.knocks);
  cw_debug_i32("jumps", &G.S.jumps);
  cw_debug_i32("braced", &G.S.braced_waves);
  cw_debug_i32("dews", &G.S.dews);
  cw_debug_i32("silk", &G.S.silk);
  cw_debug_i32("zips", &G.S.zips);
  cw_debug_i32("pecks", &G.S.pecks);
  cw_debug_i32("flies", &G.S.flies);
  cw_debug_i32("win.ticks", &G.result_ticks);
  cw_debug_i32("new_best", &G.new_best);
  cw_debug_i32("save.unlocked", &dbg_unlocked);
  cw_debug_i32("save.cleared", &dbg_cleared);
  cw_debug_i32("select.locked_bumps", &G.locked_bumps);
  cw_debug_f32_array("spider.pos", G.spider_pos, 3);
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  cw_debug_u32("world.hash", &world_hash);
  cw_tree_add_hook(cw_scene_app_tree(), 2, 0, sync_hook, NULL);
  load_world(0, true);
  G.state = GS_TITLE;
#ifdef SPOUT_AUTOPILOT
  start_level(0);
#endif
  const char *sl = getenv("SPOUT_START_LEVEL");
  if (sl && atoi(sl) >= 1 && atoi(sl) <= LEVELS_BUILT) start_level(atoi(sl) - 1);
}

static bool pressed(cw_action a) { return cw_action_just_pressed(0, a); }

static void save_fields(void) {
  dbg_unlocked = dbg_cleared = 0;
  for (int i = 0; i < SP_LEVELS; i++) {
    dbg_unlocked += level_open(i);
    if (SAVE.lv[i].cleared) dbg_cleared |= 1 << i;
  }
}

static void play_events(void) {
  const sim *S = &G.S;
  if (S->ev_jump) sfx(SFX_JUMP, 0.6f);
  if (S->ev_land) sfx(SFX_LAND, 0.5f);
  if (S->ev_wash || S->ev_knock) sfx(SFX_SPLASH, 0.8f);
  if (S->ev_check) sfx(SFX_CHECK, 0.8f);
  if (S->ev_dew) sfx(SFX_DEW, 0.7f);
  if (S->ev_rain_start) sfx(SFX_RAIN_IN, 0.6f);
  if (S->ev_sun_start) sfx(SFX_SUN_IN, 0.7f);
  if (S->ev_brace_hold) sfx(SFX_HOLD, 0.6f);
  if (S->ev_gust) sfx(SFX_RAIN_IN, 0.4f);
  if (S->ev_zip) sfx(SFX_ZIP, 0.7f);
  if (S->ev_zip_fizzle) sfx(SFX_LOCKED, 0.4f);
  if (S->ev_bird) sfx(SFX_TWEET, 0.6f);
  if (S->ev_peck) sfx(SFX_PECK, 0.8f);
  if (S->ev_fly || S->ev_spool) sfx(SFX_DEW, 0.8f);
}

static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  G.ticks++;
  G.state_ticks++;
  read_devices();
  touch_live = G.state == GS_PLAY;
  touch_layout();
  for (int i = 0; i < SFX_COUNT; i++)
    if (A.cooldown[i] > 0) A.cooldown[i]--;
  cw_vec2 m = cw_r2d_window_to_canvas(cw_v2(cw_input_raw()->mouse_x, cw_input_raw()->mouse_y));
  cw_vec2 cs = cw_r2d_canvas_size();
  bool confirm = pressed(G.a_start) || pressed(G.a_jump);
  switch (G.state) {
  case GS_TITLE:
    G.prev = G.S;
    G.S.s.walk += 0.004f;
    if (G.state_ticks > 20 && (confirm || mouse_clicked)) {
      G.state = GS_SELECT;
      G.state_ticks = 0;
      G.cursor = 0;
      sfx(SFX_SELECT, 0.7f);
    }
    break;
  case GS_SELECT: {
    G.prev = G.S;
    int dx = (pressed(G.a_right) ? 1 : 0) - (pressed(G.a_left) ? 1 : 0);
    if (dx) G.cursor = (G.cursor + dx + SP_LEVELS) % SP_LEVELS, sfx(SFX_SELECT, 0.4f);
    /* tiles in a row: a tap or click picks one */
    float tw = cs.x / 6.2f, gap = (cs.x - tw * SP_LEVELS) / (SP_LEVELS + 1);
    int hit = -1;
    for (int i = 0; i < SP_LEVELS; i++) {
      float x = gap + i * (tw + gap), y = cs.y * 0.36f;
      if (m.x >= x && m.x < x + tw && m.y >= y && m.y < y + tw * 1.25f) hit = i;
    }
    if (mouse_clicked && hit >= 0) G.cursor = hit;
    if (G.state_ticks > 10 && (confirm || (mouse_clicked && hit >= 0))) {
      if (level_open(G.cursor)) {
        sfx(SFX_SELECT, 0.8f);
        start_level(G.cursor);
      } else {
        G.locked_bumps++;
        sfx(SFX_LOCKED, 0.7f);
      }
    }
    if (pressed(G.a_back)) G.state = GS_TITLE, G.state_ticks = 0;
    break;
  }
  case GS_PLAY: {
    G.prev = G.S;
    sim_input in = { 0 };
    in.x = cw_action_strength(0, G.a_right) - cw_action_strength(0, G.a_left);
    in.y = cw_action_strength(0, G.a_up) - cw_action_strength(0, G.a_down);
    in.jump = pressed(G.a_jump);
    in.brace = cw_action_pressed(0, G.a_brace);
    in.silk = pressed(G.a_silk);
#ifdef SPOUT_AUTOPILOT
    in = autopilot(&G.S);
#endif
    sim_step(&G.S, &in);
    play_events();
    if (G.S.s.state == SP_WIN) {
      G.result_ticks = G.S.win_tick;
      int lv = G.level;
      if (!SAVE.lv[lv].cleared || G.S.win_tick < SAVE.lv[lv].best_ticks) SAVE.lv[lv].best_ticks = G.S.win_tick, G.new_best = 1;
      SAVE.lv[lv].cleared = 1;
      if (G.S.dews > SAVE.lv[lv].dews) SAVE.lv[lv].dews = G.S.dews;
      if (G.S.flies > SAVE.lv[lv].flies) SAVE.lv[lv].flies = G.S.flies;
      save_write();
      sfx(SFX_WIN, 1);
      G.state = GS_WIN;
      G.state_ticks = 0;
    } else if (pressed(G.a_pause) || pressed(G.a_back)) {
      G.state = GS_PAUSE;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_PAUSE:
    G.prev = G.S;
    if (cw_quality_menu_is_open()) break; /* the graphics page has the presses */
    if (G.state_ticks > 5 && (pressed(G.a_pause) || pressed(G.a_jump) || pressed(G.a_start) || mouse_clicked)) {
      /* the lower half of the panel is QUIT for the pointer; keys and pads resume */
      if (mouse_clicked && m.y > cs.y * 0.55f) load_world(0, true), G.state = GS_SELECT;
      else G.state = GS_PLAY;
      G.state_ticks = 0;
    }
    if (G.state_ticks > 5 && pressed(G.a_back)) load_world(0, true), G.state = GS_SELECT, G.state_ticks = 0;
    break;
  case GS_WIN:
    G.prev = G.S;
#ifdef SPOUT_AUTOPILOT
    if (G.state_ticks > 360) start_level((G.level + 1) % LEVELS_BUILT); /* the next spout, round and round */
#endif
    if (G.state_ticks > 40 && (confirm || mouse_clicked)) {
      load_world(0, true);
      G.state = GS_SELECT;
      G.state_ticks = 0;
      G.cursor = G.level + 1 < LEVELS_BUILT ? G.level + 1 : G.level;
    }
    break;
  }
  save_fields();
  G.state_hash = sim_hash(&G.S);
  G.spider_pos[0] = G.S.s.th, G.spider_pos[1] = G.S.s.y, G.spider_pos[2] = G.S.s.r;
}

/* ── per frame: the 3D world follows the sim (interpolated) ─────────── */

static float frame_t;
static void sync_hook(cw_tree *t, double alpha, void *u) {
  (void)t;
  (void)u;
  float a = (float)cw_clampf((float)alpha, 0, 1);
  if (G.state == GS_PAUSE || G.state == GS_WIN) a = 1;
  /* presentation time on the tick clock: every host draws the same frame */
  frame_t = (G.ticks + a) / 60.0f;
  world_sync(&G.prev, &G.S, a, frame_t, G.stopmotion);
  world_camera(G.camera, &G.prev, &G.S, a, frame_t, G.state == GS_TITLE || G.state == GS_SELECT);
  /* rain greys the sky and dims the sun; the sun brings them back */
  static float gloom;
  float want = G.S.weather == WX_RAIN ? 1 : G.S.weather == WX_WARN ? 0.5f : 0;
  gloom += (want - gloom) * 0.02f;
  /* settled: snap, so the sky's parameters stop changing. The ease never got
   * there (0.98^n), so sun_energy and mie moved by a hair every frame and the
   * ProceduralSky re-rendered its cube and environment (PMREM: a new render
   * target, ~17 submits on WebGPU) every frame of fair weather */
  if (fabsf(want - gloom) < 1e-4f) gloom = want;
  if (cw_node_valid(G.sky)) {
    cw_node_set(G.sky, "sun_energy", cw_val_float(3.6 - 2.6 * gloom));
    cw_node_set(G.sky, "mie", cw_val_float(2.2 + 2.0 * gloom));
  }
  /* the key light's tight shadow square follows the spider; the light sits
   * up-sun of it so every caster between the sun and the spider is in it */
  if (cw_node_valid(G.sun)) {
    const t3_mat4 *m = cw_node3d_global_transform(G.sun);
    t3_vec3 z = { m->e[8], m->e[9], m->e[10] };
    t3_vec3 p = world_spider_pos(&G.prev, &G.S, a, NULL);
    cw_node3d_set_position(G.sun, (t3_vec3){ p.x * 0.5f + z.x * 25, p.y + z.y * 25, p.z * 0.5f + z.z * 25 });
  }
}

static void gfx_input(void);
static void update(void *u, double dt) {
  gfx_input();
  cw_quality_auto_measure(G.state == GS_PLAY); /* the preset steps down when play runs slow */
  (void)u;
  (void)dt;
  touch_live = G.state == GS_PLAY;
  touch_layout();
  if (A.on) {
    float target = G.state == GS_PLAY && G.S.weather == WX_RAIN ? (G.S.L->theme == THEME_WINTER ? 0.15f : 0.5f) : 0;
    static float rv;
    rv += (target - rv) * 0.05f;
    cw_bus_set_volume(A.rain_bus, rv);
    cw_audio_update();
  }
}

/* ── HUD: paper-cut panels ──────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

/* a torn-paper panel: a jagged outline (fixed per rect), a soft shadow */
static void torn(cw_rect r, cw_color c, uint32_t seed) {
  enum { STEPS = 14 };
  cw_vec2 p[4 * STEPS];
  int n = 0;
  uint32_t h = seed * 2654435761u + 7;
  for (int side = 0; side < 4; side++)
    for (int i = 0; i < STEPS; i++) {
      h ^= h << 13, h ^= h >> 17, h ^= h << 5;
      float j = ((h & 255) / 255.0f - 0.5f) * 5;
      float t = (float)i / STEPS;
      cw_vec2 q;
      if (side == 0) q = cw_v2(r.x + r.w * t, r.y + j);
      else if (side == 1) q = cw_v2(r.x + r.w + j, r.y + r.h * t);
      else if (side == 2) q = cw_v2(r.x + r.w * (1 - t), r.y + r.h + j);
      else q = cw_v2(r.x + j, r.y + r.h * (1 - t));
      p[n++] = q;
    }
  cw_vec2 mid = cw_v2(r.x + r.w * 0.5f, r.y + r.h * 0.5f), sh[4 * STEPS];
  for (int i = 0; i < n; i++) sh[i] = cw_v2(p[i].x + 5, p[i].y + 5);
  cw_r2d_fan(cw_v2(mid.x + 5, mid.y + 5), sh, n, true, rgba(0.15f, 0.1f, 0.05f, 0.25f * c.a));
  cw_r2d_fan(mid, p, n, true, c);
}
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = al, .wrap_width = wrap };
  cw_r2d_text(font_ui, s, cw_v2(x, y), &st);
}
static void title_text(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = al, .wrap_width = wrap, .outline_width = size * 0.06f,
                       .outline_color = rgba(0.2f, 0.12f, 0.06f, c.a) };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}

static const char *prompt_go(void) {
  return input_mode == DEV_TOUCH ? "tap to climb" : input_mode == DEV_PAD ? "press (A) to climb" : input_mode == DEV_KEYS ? "press Space to climb" : "click to climb";
}

static void draw_forecast(float W, float s) {
  const sim *S = &G.S;
  const level_def *L = S->L;
  int left = 0, next = WX_RAIN;
  switch (S->weather) {
  case WX_SUN: left = L->sun_ticks - S->weather_ticks, next = WX_RAIN; break;
  case WX_WARN: left = L->warn_ticks - S->weather_ticks, next = WX_RAIN; break;
  case WX_RAIN: left = L->rain_ticks - S->weather_ticks, next = WX_SUN; break;
  default: left = 90 - S->weather_ticks, next = WX_SUN; break;
  }
  cw_rect r = cw_rect_make(W - 190 * s, 14 * s, 176 * s, 74 * s);
  torn(r, rgba(1, 0.98f, 0.92f, 0.95f), 3);
  cw_vec2 c = cw_v2(r.x + 40 * s, r.y + 37 * s);
  bool rain_now = S->weather == WX_RAIN || S->weather == WX_WARN;
  if (!rain_now && S->L->theme == THEME_CITY) { /* a crescent moon */
    cw_r2d_circle(c, 16 * s, 24, rgba(0.95f, 0.92f, 0.7f, 1));
    cw_r2d_circle(cw_v2(c.x + 7 * s, c.y - 4 * s), 13 * s, 24, rgba(1, 0.98f, 0.92f, 0.95f));
  } else if (!rain_now) {
    cw_r2d_circle(c, 16 * s, 24, rgba(1, 0.75f, 0.2f, 1));
    for (int i = 0; i < 8; i++) {
      float a = i * 0.785398f + frame_t * 0.5f;
      cw_r2d_line(cw_v2(c.x + 20 * s * cw_cos(a), c.y + 20 * s * cw_sin(a)), cw_v2(c.x + 27 * s * cw_cos(a), c.y + 27 * s * cw_sin(a)), 3 * s,
                  rgba(1, 0.7f, 0.2f, 1));
    }
  } else {
    cw_r2d_circle(cw_v2(c.x - 9 * s, c.y), 12 * s, 20, rgba(0.55f, 0.6f, 0.7f, 1));
    cw_r2d_circle(cw_v2(c.x + 6 * s, c.y - 5 * s), 15 * s, 20, rgba(0.6f, 0.65f, 0.75f, 1));
    for (int i = 0; i < 3; i++) cw_r2d_line(cw_v2(c.x - 10 * s + i * 9 * s, c.y + 14 * s), cw_v2(c.x - 13 * s + i * 9 * s, c.y + 24 * s), 2.5f * s, rgba(0.3f, 0.5f, 0.9f, 1));
  }
  char b[48];
  bool snow = S->L->theme == THEME_WINTER, night = S->L->theme == THEME_CITY;
  const char *now = S->weather == WX_SUN ? (night ? "clear night" : "sunny") : S->weather == WX_WARN ? (snow ? "snow soon!" : "rain soon!") : S->weather == WX_RAIN ? (snow ? "snowing" : "raining") : "clearing";
  text(now, r.x + 72 * s, r.y + 8 * s, 22 * s, S->weather == WX_WARN ? rgba(0.75f, 0.2f, 0.1f, 1) : rgba(0.2f, 0.15f, 0.1f, 1), CW_ALIGN_LEFT, 0);
  snprintf(b, sizeof b, "%s in %d", next == WX_RAIN ? "rain" : "sun", (left + 59) / 60);
  text(b, r.x + 72 * s, r.y + 36 * s, 19 * s, rgba(0.35f, 0.3f, 0.25f, 1), CW_ALIGN_LEFT, 0);
}

static void draw_meter(float W, float H, float s) {
  (void)W;
  const sim *S = &G.S;
  float x = 26 * s, y0 = H - 40 * s, y1 = 110 * s;
  torn(cw_rect_make(x - 10 * s, y1 - 14 * s, 20 * s, y0 - y1 + 28 * s), rgba(0.75f, 0.85f, 0.95f, 0.9f), 9);
  for (int i = 0; i < S->L->n_checks; i++) {
    float y = y0 + (y1 - y0) * S->L->checks[i] / S->L->height;
    cw_r2d_rect(cw_rect_make(x - 9 * s, y - 2 * s, 18 * s, 4 * s), i <= S->checkpoint ? rgba(0.3f, 0.6f, 0.3f, 1) : rgba(0.45f, 0.45f, 0.5f, 1));
  }
  float y = y0 + (y1 - y0) * cw_clampf(S->s.y / S->L->height, 0, 1);
  cw_r2d_circle(cw_v2(x, y), 9 * s, 16, rgba(0.2f, 0.17f, 0.24f, 1));
  cw_r2d_circle(cw_v2(x - 3 * s, y - 2 * s), 3 * s, 10, rgba(1, 1, 1, 1));
  cw_r2d_circle(cw_v2(x + 3 * s, y - 2 * s), 3 * s, 10, rgba(1, 1, 1, 1));
}

static void touch_button(int id, const char *label, cw_vec2 wc, float wr, cw_color c) {
  cw_vec2 cc = cw_r2d_window_to_canvas(wc), e = cw_r2d_window_to_canvas(cw_v2(wc.x + wr, wc.y));
  float r = e.x - cc.x;
  bool on = cw_touch_control_active(id);
  cw_r2d_circle(cc, r * (on ? 0.92f : 1), 32, rgba(c.r, c.g, c.b, on ? 0.85f : 0.6f));
  cw_r2d_circle_lines(cc, r, 3, 32, rgba(1, 1, 1, 0.8f));
  title_text(label, cc.x - r * 2, cc.y - r * 0.28f, r * 0.42f, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r * 4);
}
static void draw_touch(void) {
  int ww, wh;
  cw_window_size(&ww, &wh);
  float k = (float)wh / 540.0f, jx, jy, jr, bx, by, br;
  touch_rects(&jx, &jy, &jr, &bx, &by, &br);
  bool on = cw_touch_control_active(T_STICK);
  cw_vec2 wc = on ? cw_touch_stick_center(T_STICK) : cw_v2(140 * k, (float)wh - 140 * k);
  cw_vec2 c = cw_r2d_window_to_canvas(wc), e = cw_r2d_window_to_canvas(cw_v2(wc.x + 60 * k, wc.y));
  float R = e.x - c.x;
  cw_r2d_circle(c, R * 1.25f, 36, rgba(1, 0.98f, 0.9f, on ? 0.5f : 0.35f));
  cw_r2d_circle_lines(c, R * 1.25f, 3, 36, rgba(0.3f, 0.2f, 0.1f, 0.5f));
  cw_vec2 v = on ? cw_touch_stick_value(T_STICK) : cw_v2(0, 0);
  cw_r2d_circle(cw_v2(c.x + v.x * R, c.y + v.y * R), R * 0.45f, 24, rgba(0.25f, 0.2f, 0.3f, on ? 0.9f : 0.6f));
  touch_button(T_JUMP, "JUMP", cw_v2(jx, jy), jr, rgba(0.95f, 0.55f, 0.3f, 1));
  touch_button(T_BRACE, "HOLD", cw_v2(bx, by), br, rgba(0.35f, 0.55f, 0.85f, 1));
  {
    float sx, sy, sr;
    silk_rect(&sx, &sy, &sr);
    touch_button(T_SILK, "SILK", cw_v2(sx, sy), sr, rgba(0.92f, 0.92f, 0.98f, 1));
  }
  touch_button(T_PAUSE, "II", cw_v2(14 * k + 32 * k, 14 * k + 26 * k), 26 * k, rgba(0.3f, 0.25f, 0.2f, 1));
}

static void draw_hud(float W, float H, float s) {
  const sim *S = &G.S;
  char b[64];
  int secs = S->tick / 60;
  snprintf(b, sizeof b, "%d:%02d   dew %d/%d   flies %d/3", secs / 60, secs % 60, S->dews, S->L->n_dews, S->flies);
  torn(cw_rect_make(W * 0.5f - 170 * s, 12 * s, 340 * s, 56 * s), rgba(1, 0.98f, 0.92f, 0.95f), 6);
  text(b, W * 0.5f - 170 * s, 22 * s, 26 * s, rgba(0.22f, 0.16f, 0.1f, 1), CW_ALIGN_CENTER, 340 * s);
  /* silk: a little spool per charge */
  for (int i = 0; i < SILK_MAX; i++) {
    cw_vec2 c = cw_v2(W * 0.5f - 52 * s + i * 26 * s, 84 * s);
    if (i >= S->silk && i >= 3) continue;
    cw_r2d_circle(c, 9 * s, 16, i < S->silk ? rgba(0.97f, 0.97f, 1, 0.95f) : rgba(0.3f, 0.3f, 0.35f, 0.35f));
    cw_r2d_circle_lines(c, 9 * s, 2 * s, 16, rgba(0.35f, 0.3f, 0.4f, 0.8f));
  }
  draw_forecast(W, s);
  draw_meter(W, H, s);
  if (S->s.state == SP_WASHED && S->s.stun > 0 && S->pecks && S->bird_ticks > BIRD_WARN) title_text("pecked!", 0, H * 0.3f, 40 * s, rgba(1, 0.8f, 0.5f, 1), CW_ALIGN_CENTER, W);
  else if (S->s.state == SP_WASHED && S->s.stun > 0) title_text("washed down!", 0, H * 0.3f, 40 * s, rgba(0.55f, 0.75f, 1, 1), CW_ALIGN_CENTER, W);
  else if (sim_gust_warn(S)) title_text("wind's picking up - hold tight!", 0, H * 0.18f, 30 * s, rgba(0.8f, 0.9f, 1, 1), CW_ALIGN_CENTER, W);
  else if (sim_gusting(S)) title_text("whoosh!", 0, H * 0.18f, 36 * s, rgba(0.8f, 0.9f, 1, 1), CW_ALIGN_CENTER, W);
  else if (S->weather == WX_WARN)
    title_text(S->L->theme == THEME_WINTER ? "snow's coming - hold tight!" : "rain's coming - hold tight!", 0, H * 0.18f, 30 * s, rgba(1, 0.85f, 0.5f, 1), CW_ALIGN_CENTER, W);
  if (S->ev_check || (S->checkpoint > 0 && G.state_ticks % 600 < 0)) (void)0;
  if (input_mode == DEV_TOUCH) draw_touch();
  else if (G.state_ticks < 600) {
    const char *h = input_mode == DEV_PAD ? "stick: climb   (A) jump   (B) hold   (X) silk up" : "WASD: climb   Space: jump   Shift: hold   E: silk up";
    torn(cw_rect_make(W * 0.5f - 270 * s, H - 54 * s, 540 * s, 40 * s), rgba(1, 0.98f, 0.92f, 0.85f), 8);
    text(h, W * 0.5f - 270 * s, H - 48 * s, 22 * s, rgba(0.25f, 0.2f, 0.15f, 1), CW_ALIGN_CENTER, 540 * s);
  }
}

static void draw_title(float W, float H, float s) {
  torn(cw_rect_make(W * 0.5f - 230 * s, H * 0.1f, 460 * s, 150 * s), rgba(1, 0.97f, 0.88f, 0.95f), 1);
  title_text(SPOUT_TITLE, 0, H * 0.1f + 10 * s, 96 * s, rgba(0.95f, 0.55f, 0.3f, 1), CW_ALIGN_CENTER, W);
  text("a paper spider, a long wet climb", 0, H * 0.1f + 114 * s, 26 * s, rgba(0.3f, 0.22f, 0.15f, 1), CW_ALIGN_CENTER, W);
  float p = 0.6f + 0.4f * cw_sin(frame_t * 3);
  torn(cw_rect_make(W * 0.5f - 130 * s, H * 0.82f, 260 * s, 46 * s), rgba(1, 0.98f, 0.92f, 0.6f + 0.3f * p), 2);
  text(prompt_go(), 0, H * 0.82f + 7 * s, 28 * s, rgba(0.25f, 0.18f, 0.12f, 1), CW_ALIGN_CENTER, W);
}

static void draw_select(float W, float H, float s) {
  title_text("pick a spout", 0, H * 0.12f, 52 * s, rgba(0.95f, 0.55f, 0.3f, 1), CW_ALIGN_CENTER, W);
  float tw = W / 6.2f, gap = (W - tw * SP_LEVELS) / (SP_LEVELS + 1);
  static const float TC[THEME_COUNT][3] = { { 0.75f, 0.9f, 0.65f }, { 0.55f, 0.6f, 0.85f }, { 0.95f, 0.7f, 0.45f }, { 0.85f, 0.92f, 1 }, { 0.5f, 0.8f, 0.55f } };
  for (int i = 0; i < SP_LEVELS; i++) {
    float x = gap + i * (tw + gap), y = H * 0.36f;
    const level_def *L = &LEVELS[i];
    bool open = level_open(i), sel = i == G.cursor;
    float lift = sel ? -8 * s : 0;
    torn(cw_rect_make(x, y + lift, tw, tw * 1.25f), rgba(TC[L->theme][0], TC[L->theme][1], TC[L->theme][2], open ? 1 : 0.55f), (uint32_t)(20 + i));
    if (sel) cw_r2d_polyline((cw_vec2[]){ cw_v2(x - 4, y + lift - 4), cw_v2(x + tw + 4, y + lift - 4), cw_v2(x + tw + 4, y + lift + tw * 1.25f + 4), cw_v2(x - 4, y + lift + tw * 1.25f + 4) }, 4, 4 * s, true, rgba(0.95f, 0.55f, 0.3f, 1));
    char b[32];
    snprintf(b, sizeof b, "%d", i + 1);
    title_text(b, x, y + lift + 8 * s, 40 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, tw);
    text(L->name, x + 6 * s, y + lift + 62 * s, 20 * s, rgba(0.2f, 0.15f, 0.1f, 1), CW_ALIGN_CENTER, tw - 12 * s);
    if (!open) {
      text(i < LEVELS_BUILT ? "locked" : "coming soon", x, y + lift + tw * 0.95f, 19 * s, rgba(0.35f, 0.3f, 0.3f, 1), CW_ALIGN_CENTER, tw);
    } else if (SAVE.lv[i].cleared) {
      int t = SAVE.lv[i].best_ticks / 60;
      snprintf(b, sizeof b, "best %d:%02d", t / 60, t % 60);
      text(b, x, y + lift + tw * 0.95f, 19 * s, rgba(0.15f, 0.4f, 0.15f, 1), CW_ALIGN_CENTER, tw);
    }
  }
  const level_def *L = &LEVELS[G.cursor];
  text(L->blurb, 0, H * 0.36f + W / 6.2f * 1.25f + 24 * s, 26 * s, rgba(0.2f, 0.15f, 0.1f, 1), CW_ALIGN_CENTER, W);
  text(input_mode == DEV_TOUCH ? "tap a spout" : input_mode == DEV_PAD ? "left / right, (A) to climb" : "left / right, Enter or click to climb", 0, H * 0.9f, 22 * s,
       rgba(0.3f, 0.25f, 0.2f, 1), CW_ALIGN_CENTER, W);
}

/* the graphics settings from the pause card (cartwheel/quality_menu.h): Y or
 * G opens the page (a click on the card already means resume or quit),
 * which owns the presses while it is open (the pause tick leaves them
 * alone). Render-only, so it is read in update, never in the simulation;
 * the choice is kept in the save. Touch players keep Auto */
static void gfx_input(void) {
  if (G.state != GS_PAUSE) {
    cw_quality_menu_close();
    return;
  }
  if (cw_quality_menu_is_open()) {
    cw_quality_menu_update();
    if (!cw_quality_menu_is_open() && cw_quality_menu_changed()) save_write();
    return;
  }
  if (G.state_ticks < 5) return;
  const cw_raw_input *in = cw_input_raw();
  bool hit = in->keys_pressed[10]; /* G */
  for (int i = 0; i < CW_MAX_PADS; i++) hit |= (in->pads[i].pressed & (1u << CW_PAD_NORTH)) != 0;
  if (hit) cw_quality_menu_open();
}

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.1f, 0.08f, 0.06f, 0.35f));
  torn(cw_rect_make(W * 0.5f - 170 * s, H * 0.3f, 340 * s, H * 0.4f), rgba(1, 0.97f, 0.88f, 0.97f), 11);
  title_text("paused", 0, H * 0.32f, 48 * s, rgba(0.95f, 0.55f, 0.3f, 1), CW_ALIGN_CENTER, W);
  text(input_mode == DEV_PAD ? "(A) resume    Back: quit" : input_mode == DEV_KEYS ? "Space: resume    Esc: quit" : "tap the top: resume", 0, H * 0.45f, 24 * s,
       rgba(0.25f, 0.2f, 0.15f, 1), CW_ALIGN_CENTER, W);
  if (input_mode == DEV_TOUCH || input_mode == DEV_MOUSE) text("tap here: quit to spouts", 0, H * 0.6f, 24 * s, rgba(0.6f, 0.2f, 0.1f, 1), CW_ALIGN_CENTER, W);
  char buf[64];
  snprintf(buf, sizeof buf, input_mode == DEV_TOUCH ? "graphics: %s" : "graphics: %s   (Y / G for settings)", cw_quality_label());
  for (char *c = buf; *c; c++) *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
  text(buf, 0, H * 0.52f, 18 * s, rgba(0.4f, 0.3f, 0.2f, 1), CW_ALIGN_CENTER, W);
}

static void draw_win(float W, float H, float s) {
  const sim *S = &G.S;
  float in = cw_clampf(G.state_ticks / 20.0f, 0, 1);
  torn(cw_rect_make(W * 0.5f - 210 * s, H * 0.22f, 420 * s, 250 * s), rgba(1, 0.97f, 0.88f, 0.97f * in), 13);
  title_text("to the top!", 0, H * 0.24f, 52 * s, rgba(0.95f, 0.55f, 0.3f, in), CW_ALIGN_CENTER, W);
  char b[96];
  int t = S->win_tick / 60;
  snprintf(b, sizeof b, "time %d:%02d%s\ndewdrops %d of %d, flies %d of 3\nwashed down %d, held on %d", t / 60, t % 60, G.new_best ? "  (best!)" : "", S->dews,
           S->L->n_dews, S->flies, S->washes + S->knocks, S->braced_waves);
  text(b, 0, H * 0.24f + 80 * s, 26 * s, rgba(0.25f, 0.2f, 0.15f, in), CW_ALIGN_CENTER, W);
  text(prompt_go(), 0, H * 0.24f + 200 * s, 22 * s, rgba(0.4f, 0.3f, 0.2f, in), CW_ALIGN_CENTER, W);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (G.state == GS_TITLE) draw_title(W, H, s);
  else if (G.state == GS_SELECT) draw_select(W, H, s);
  else {
    draw_hud(W, H, s);
    if (G.state == GS_PAUSE) draw_pause(W, H, s);
    if (G.state == GS_WIN) draw_win(W, H, s);
  }
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = font_ui, .accent = rgba(0.95f, 0.55f, 0.3f, 1) });
  cw_r2d_end();
}

static void shutdown(void *u) {
  (void)u;
  world_free();
  paper_free_all();
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = "Spout", .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed,
                              .update = update, .render = render, .shutdown = shutdown, .save = &SAVE, .save_size = sizeof SAVE,
                              .touch_pointer = true };
  cw_app_desc d = cw_scene_app(NULL, &base);
  return d;
}
