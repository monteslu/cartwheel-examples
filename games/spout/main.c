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
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "cartwheel/vfs.h"
#include "game.h"
#include "study.h"

static int medal_of(int level, int ticks); /* (the paper medals, by the win card) */
static void paper_medal(float cx, float cy, float r, int m, float a);
static void paper_ribbon(float cx, float cy, float r, int kind, float a);

enum { GS_TITLE, GS_SELECT, GS_PLAY, GS_PAUSE, GS_WIN };

/* ── save ───────────────────────────────────────────────────────────── */

#define SAVE_MAGIC 0x54554f53u /* 'SOUT' */
#define SAVE_VERSION 5
/* version 5: the ghost of each spout's best climb and the dry-climb ribbons; version 4: the text size
 * (cw_text_scale_saved) and the text background; version 3 ended at the graphics settings */
#define GHOST_EVERY 8   /* ticks between a ghost's samples */
#define GHOST_MAX 900   /* samples: a climb of two minutes */
typedef struct ghost_track {
  uint16_t n, pad;
  uint8_t s[GHOST_MAX][5]; /* the spider's angle (16 bits round the pipe), height (16 bits over 40 m), lift (8 bits over 1 m) */
} ghost_track;
typedef struct save_data {
  uint32_t magic;
  uint16_t version, size;
  struct { int32_t cleared, best_ticks, dews, flies; } lv[SP_LEVELS];
  uint8_t quality[4]; /* the graphics settings: cw_quality_saved() little-endian, 0 = none chosen */
  uint8_t text_size, text_bg; /* cw_text_scale_saved() (0 = none chosen), cw_text_backdrop_get() */
  uint8_t dry, hands;         /* dry: a bit per spout climbed with no wash and no knock (the dry-climb ribbon); hands: 1 two hands */
  ghost_track ghost[SP_LEVELS];
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
  SAVE.text_size = cw_text_scale_saved();
  SAVE.text_bg = (uint8_t)cw_text_backdrop_get();
  SAVE.checksum = save_sum();
  cw_save_commit();
}
/* a version 4 save: the same fields up to the text bytes (its spare bytes were zero), its checksum where the ghosts now
 * are */
static bool save_from_v4(void) {
  enum { V4_SIZE = offsetof(save_data, ghost) + sizeof(uint32_t) };
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != 4 || SAVE.size != V4_SIZE) return false;
  uint32_t stored, sum = cw_hash32(&SAVE.lv, offsetof(save_data, ghost) - offsetof(save_data, lv), CW_HASH32_INIT) ^ (4u | (uint32_t)V4_SIZE << 16);
  memcpy(&stored, &SAVE.ghost, sizeof stored);
  if (stored != sum) return false;
  memset(SAVE.ghost, 0, sizeof SAVE.ghost);
  SAVE.dry = SAVE.hands = 0;
  SAVE.version = SAVE_VERSION;
  SAVE.size = sizeof SAVE;
  SAVE.checksum = save_sum();
  return true;
}
/* a version 3 save: the same fields up to the graphics settings, its checksum where the text bytes now are */
static bool save_from_v3(void) {
  enum { V3_SIZE = offsetof(save_data, text_size) + sizeof(uint32_t) };
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != 3 || SAVE.size != V3_SIZE) return false;
  uint32_t stored, sum = cw_hash32(&SAVE.lv, offsetof(save_data, text_size) - offsetof(save_data, lv), CW_HASH32_INIT) ^ (3u | (uint32_t)V3_SIZE << 16);
  memcpy(&stored, &SAVE.text_size, sizeof stored);
  if (stored != sum) return false;
  SAVE.text_size = SAVE.text_bg = SAVE.dry = SAVE.hands = 0;
  memset(SAVE.ghost, 0, sizeof SAVE.ghost);
  SAVE.version = SAVE_VERSION;
  SAVE.size = sizeof SAVE;
  SAVE.checksum = save_sum();
  return true;
}
static void save_check(void) {
  if (save_from_v3() || save_from_v4()) return;
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) save_reset();
}
static bool level_open(int i) { return i < LEVELS_BUILT && (i == 0 || SAVE.lv[i - 1].cleared); }

/* ── game state ─────────────────────────────────────────────────────── */

static struct {
  int32_t state, state_ticks, ticks, level, cursor;
  sim S, prev;
  cw_node scene, camera, sky, sun;
  cw_action a_left, a_right, a_up, a_down, a_jump, a_brace, a_silk, a_start, a_back, a_pause, a_settings, a_hand_l, a_hand_r;
  bool stopmotion;
  int32_t locked_bumps;
  uint32_t state_hash;
  float spider_pos[3];
  int32_t result_ticks, new_best;
  int32_t medal; /* the last climb's medal: 0 gold, 1 silver, 2 bronze */
  int title_level;
  int32_t ribbons; /* the last climb's ribbons: 1 all three flies, 2 a dry climb */
} G;

/* the climb being recorded, for the ghost (render-only: the sim never reads it) */
static ghost_track rec;
static int32_t rec_hash, ghost_hash, ghost_n, ghost_shown, sw_no_ghost;
static uint32_t track_hash(const ghost_track *g) { return cw_hash32(g->s, (size_t)g->n * 5, CW_HASH32_INIT); }
static void rec_sample(const sim *S) {
  if (rec.n >= GHOST_MAX) return;
  float th = (wrap_pi(S->s.th) + 3.1415927f) / 6.2831853f, y = cw_clampf(S->s.y / 40.0f, 0, 1), r = cw_clampf(S->s.r, 0, 1);
  uint16_t a = (uint16_t)cw_clampf(th * 65535.0f + 0.5f, 0, 65535), h = (uint16_t)(y * 65535.0f + 0.5f);
  uint8_t *o = rec.s[rec.n++];
  o[0] = (uint8_t)a, o[1] = (uint8_t)(a >> 8), o[2] = (uint8_t)h, o[3] = (uint8_t)(h >> 8), o[4] = (uint8_t)(r * 255.0f + 0.5f);
}
static void track_at(const ghost_track *g, int i, float *th, float *y, float *r) {
  const uint8_t *o = g->s[i];
  *th = (o[0] | o[1] << 8) / 65535.0f * 6.2831853f - 3.1415927f;
  *y = (o[2] | o[3] << 8) / 65535.0f * 40.0f;
  *r = o[4] / 255.0f;
}

static int32_t input_mode, dbg_unlocked, dbg_cleared, dbg_hands;
/* P1's device, as the engine's prompts see it (cartwheel/prompt.h): it picks
 * the prompts and the touch controls. Asked once a frame in update (prompts
 * are presentation, never the fixed tick); input.mode publishes it (0 mouse,
 * 1 pad, 2 keys, 3 touch). */
static cw_prompt_device p1_device = CW_PROMPT_MOUSE;
static uint32_t prev_mouse;
static bool mouse_clicked;
/* a pad or the keyboard reads glyphs; the mouse and touch click or tap what they see */
static bool glyphs(void) { return p1_device == CW_PROMPT_PAD || p1_device == CW_PROMPT_KEYBOARD; }
static bool on_touch(void) { return p1_device == CW_PROMPT_TOUCH; }

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

static void read_mouse(void) {
  const cw_raw_input *in = cw_input_raw();
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse & 1);
  prev_mouse = in->mouse_buttons;
}
static void read_device(void) {
  /* a phone starts on touch before its first tap. The hosts report pad 0 as
   * connected with no pad in hand, and the engine prefers a connected pad, so
   * until P1 presses a pad button or a key a touch-first device is held on
   * touch (a tap is the engine's own touch switch) */
  static bool held, pressed;
  if (!pressed) {
    const cw_raw_input *in = cw_input_raw();
    for (int i = 0; i < CW_MAX_PADS; i++) pressed |= in->pads[i].pressed != 0;
    for (int k = 0; k < CW_KEY_COUNT && !pressed; k++) pressed |= in->keys_pressed[k] != 0;
    bool hold = !pressed && cw_platform_touch_primary();
    if (hold != held) cw_prompt_force_device(0, hold ? CW_PROMPT_TOUCH : CW_PROMPT_NONE);
    held = hold;
  }
  p1_device = cw_prompt_device_of(0);
  input_mode = p1_device == CW_PROMPT_PAD ? 1 : p1_device == CW_PROMPT_KEYBOARD ? 2 : p1_device == CW_PROMPT_TOUCH ? 3 : 0;
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
  G.S.hands = !title && SAVE.hands && !on_touch(); /* two hands: the pad's or keyboard's shoulders */
  if (title) G.S.s.y = 1.2f, G.S.s.th = 0.3f;
  G.prev = G.S;
}

static void start_level(int level) {
  load_world(level, false);
  G.state = GS_PLAY;
  G.state_ticks = 0;
  G.new_best = 0;
  rec.n = 0;
  rec_sample(&G.S);
  ghost_n = sw_no_ghost ? 0 : SAVE.ghost[level].n;
  ghost_hash = (int32_t)track_hash(&SAVE.ghost[level]);
}

/* bind <action> <bindings>: replace an action's bindings (cartwheel/input.h
 * binding text, "pad:north, key:v"), as a player rebinding a control would;
 * every prompt shows the new binding from that frame (tests of the prompts) */
static void cmd_bind(int argc, const char **argv, cw_reply *r) {
  if (argc < 3) {
    cw_reply_error(r, "usage: bind <action> <bindings>");
    return;
  }
  cw_action a = cw_action_find(argv[1]);
  char list[160] = "";
  for (int i = 2; i < argc; i++) snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", i > 2 ? " " : "", argv[i]);
  cw_action_clear_bindings(a);
  if (!cw_action_bind_text(a, list)) {
    cw_reply_error(r, "bind: no action %s or bad bindings \"%s\"", argv[1], list);
    return;
  }
  cw_reply_printf(r, "{\"action\":\"%s\",\"bindings\":", argv[1]);
  cw_reply_str(r, list);
  cw_reply_raw(r, "}");
}
/* Inspection command: defer scene replacement to the fixed tick. Does not
 * unlock levels or write progression data. */
static int pending_level=-1;
/* study hazards <level>: three climbers (hold, tell, none) on three seeds, without drawing (study.c);
 * sw old_hazards <0|1>: streams that only wander and the old bird (the hazard gate's control) */
static int sw_old_hazards;
static void cmd_study(int argc, const char **argv, cw_reply *r) {
  if (argc >= 2 && !strcmp(argv[1], "silk")) { /* the web and the line, scripted (study.c) */
    static study_silk_out o;
    study_silk(&o);
    const study_web *w[2] = { &o.web, &o.none };
    cw_reply_raw(r, "{");
    for (int i = 0; i < 2; i++)
      cw_reply_printf(r, "\"%s\":{\"web_y\":%.3f,\"before\":%.3f,\"after\":%.3f,\"washed\":%d,\"web_left\":%d,\"caught\":%d,\"silk\":%d},", i ? "none" : "web",
                      w[i]->web_y, w[i]->before, w[i]->after, w[i]->washed, w[i]->web_left, w[i]->caught, w[i]->silk);
    cw_reply_printf(r, "\"line\":{\"started\":%d,\"anchor\":%.3f,\"lowest\":%.3f,\"back\":%.3f,\"swung\":%.3f,\"ticks\":%d,\"silk_spent\":%d,\"cling\":%d}}",
                    o.line.started, o.line.anchor, o.line.lowest, o.line.back, o.line.swung, o.line.ticks, o.line.silk_spent, o.line.cling);
    return;
  }
  if (argc >= 2 && !strcmp(argv[1], "hands")) { /* the climb by stick and by two hands (study.c) */
    static int hh[4][3];
    study_hands(hh);
    cw_reply_printf(r, "{\"stick\":[%d,%d,%d],\"steady\":[%d,%d,%d],\"ragged\":[%d,%d,%d],\"one_hand\":[%d,%d,%d]}", hh[0][0], hh[0][1], hh[0][2], hh[1][0], hh[1][1], hh[1][2],
                    hh[2][0], hh[2][1], hh[2][2], hh[3][0], hh[3][1], hh[3][2]);
    return;
  }
  int n = argc > 2 ? atoi(argv[2]) : 1;
  if (argc < 2 || strcmp(argv[1], "hazards") || n < 1 || n > LEVELS_BUILT) {
    cw_reply_error(r, "usage: study hazards <1..5>");
    return;
  }
  static study_climb c[3][3];
  study_hazards(n - 1, c);
  static const char *const BOT[3] = { "hold", "tell", "none" };
  cw_reply_raw(r, "{");
  for (int b = 0; b < 3; b++) {
    cw_reply_printf(r, "%s\"%s\":[", b ? "," : "", BOT[b]);
    for (int k = 0; k < 3; k++)
      cw_reply_printf(r, "%s{\"ticks\":%d,\"washes\":%d,\"knocks\":%d,\"pecks\":%d,\"braced\":%d}", k ? "," : "", c[b][k].ticks, c[b][k].washes, c[b][k].knocks,
                      c[b][k].pecks, c[b][k].braced);
    cw_reply_raw(r, "]");
  }
  cw_reply_raw(r, "}");
}
static int sw_old_hud;
static int32_t hud_over_pipe, hud_samples;
static int32_t wave_near; /* a wave front drawn coming down the spider's stream, within 1.6 m above it (what a player sees) */
static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc > 2 && !strcmp(argv[1], "old_hazards")) sw_old_hazards = atoi(argv[2]), sim_old_hazards(sw_old_hazards);
  if (argc > 2 && !strcmp(argv[1], "old_hud")) sw_old_hud = atoi(argv[2]);
  if (argc > 2 && !strcmp(argv[1], "hide_waves")) world_hide_waves = atoi(argv[2]) != 0;
  if (argc > 2 && !strcmp(argv[1], "no_ghost")) sw_no_ghost = atoi(argv[2]);
  if (argc > 2 && !strcmp(argv[1], "fresh") && atoi(argv[2])) save_reset(); /* a new player's save in memory (tests on a host that keeps saves) */
  cw_reply_printf(r, "{\"old_hazards\":%d,\"old_hud\":%d,\"hide_waves\":%d,\"no_ghost\":%d}", sw_old_hazards, sw_old_hud, (int)world_hide_waves, sw_no_ghost);
}
static void cmd_level(int argc,const char **argv,cw_reply *r) {
  int n=argc>1?atoi(argv[1]):0;
  if(n<1||n>LEVELS_BUILT){cw_reply_error(r,"usage: level <1..5>");return;}
  pending_level=n-1;
  cw_reply_printf(r,"{\"level\":%d}",n);
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
  /* the bird's mark near: step round out of it while it shows */
  if (!in.brace && S->bird_ticks > 0 && S->bird_ticks < BIRD_WARN && fabsf(wrap_pi(s->th - S->bird_th)) < 0.9f && fabsf(s->y - S->bird_y) < 1.2f) {
    in.x = wrap_pi(s->th - S->bird_th) >= 0 ? 1.0f : -1.0f, in.y = 0.2f;
    return in;
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
  cw_quality_full_resolution(true); /* Auto gives up detail, never pixels */
  cw_quality_restore(gfx_saved()); /* the player's graphics settings, if they chose any; else Auto */
  cw_quality_auto(true, 0);
  cw_text_scale_restore(SAVE.text_size); /* the player's text size and text background */
  cw_text_backdrop_set(SAVE.text_bg <= CW_TEXT_BACKDROP_PANEL ? (cw_text_backdrop)SAVE.text_bg : CW_TEXT_BACKDROP_OFF);
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
  G.a_settings = cw_action_find("settings");
  G.a_hand_l = cw_action_find("hand_l");
  G.a_hand_r = cw_action_find("hand_r");
  touch_layout();
  audio_init();
  font_ui = cw_font_load("fonts/PatrickHand-Regular.ttf", NULL);
  font_title = cw_font_load("fonts/Sniglet-ExtraBold.ttf", NULL);
  /* Prepare printable English UI glyphs while loading. SDF glyph generation
   * must not interrupt gameplay when a new score or result first appears. */
  const cw_font ui_fonts[] = {font_ui, font_title};
  for (unsigned f = 0; f < sizeof ui_fonts / sizeof ui_fonts[0]; f++)
    cw_font_preload_range(ui_fonts[f], 32, 126);

  G.stopmotion = true;
  cw_debug_command("level", "level <1..5>: inspect a playable level without changing unlocks",cmd_level);
  cw_debug_command("study", "study hazards <1..5>: three climbers on three seeds, without drawing (the hazard gate); study silk: the web and the line, scripted", cmd_study);
  cw_debug_command("sw", "sw old_hazards|old_hud|hide_waves|no_ghost <0|1>: the old hazards or HUD back, the wave fronts undrawn, no ghost kept or shown (the gates' controls); sw fresh 1: a new player's save in memory", cmd_sw);
  cw_debug_i32("hud.over_pipe", &hud_over_pipe);
  cw_debug_i32("hud.wave_near", &wave_near);
  cw_debug_i32("hud.samples", &hud_samples);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings (binding text, as input.cfg writes it)", cmd_bind);
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
  cw_debug_i32("win.medal", &G.medal);
  cw_debug_i32("win.ribbons", &G.ribbons);
  cw_debug_i32("ghost.n", &ghost_n);
  cw_debug_i32("ghost.hash", &ghost_hash);
  cw_debug_i32("ghost.shown", &ghost_shown);
  cw_debug_i32("rec.hash", &rec_hash);
  cw_debug_i32("nets", &G.S.nets);
  cw_debug_i32("nets.caught", &G.S.nets_caught);
  cw_debug_i32("rappels", &G.S.rappels);
  cw_debug_i32("pulls", &G.S.pulls);
  cw_debug_i32("fumbles", &G.S.fumbles);
  cw_debug_i32("save.hands", &dbg_hands);
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

/* the screens' layouts (drawn below), shared with the pointer here */
typedef struct select_geo { float tw, gap, y, th, name_y, stat_y, blurb_y, hint_y, wrap; } select_geo;
typedef struct pause_geo { cw_rect card, row[2]; float title_y, prompt_y, gfx_y, quit_y, quit_top; } pause_geo;
/* the last drawn layouts: the pointer reads them here (prompts are measured only while drawing, never in the fixed tick) */
static select_geo select_drawn;
static pause_geo pause_drawn = { .row = { { -1e4f, -1e4f, 0, 0 }, { -1e4f, -1e4f, 0, 0 } }, .quit_top = 1e9f };
static float pause_quit_top = 1e9f;

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
  if (S->ev_rappel) sfx(SFX_ZIP, 0.45f);
  if (S->ev_net) sfx(SFX_HOLD, 0.5f);
  if (S->ev_net_catch) sfx(SFX_HOLD, 0.9f);
}

static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  if(pending_level>=0){int level=pending_level;pending_level=-1;start_level(level);}
  G.ticks++;
  G.state_ticks++;
  read_mouse();
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
    select_geo sg = select_drawn;
    int hit = -1;
    for (int i = 0; i < SP_LEVELS; i++) {
      float x = sg.gap + i * (sg.tw + sg.gap);
      if (m.x >= x && m.x < x + sg.tw && m.y >= sg.y && m.y < sg.y + sg.th) hit = i;
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
    if (G.state_ticks > 10 && pressed(G.a_silk) && !on_touch()) { /* the climb: the stick, or two hands */
      SAVE.hands = !SAVE.hands;
      save_write();
      sfx(SFX_SELECT, 0.6f);
    }
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
    in.hand_l = pressed(G.a_hand_l);
    in.hand_r = pressed(G.a_hand_r);
#ifdef SPOUT_AUTOPILOT
    in = autopilot(&G.S);
#endif
    sim_step(&G.S, &in);
    play_events();
    if (G.S.tick % GHOST_EVERY == 0) rec_sample(&G.S);
    wave_near = 0;
    for (int i = 0; i < 8 && !world_hide_waves; i++) {
      const wave *w = &G.S.waves[i];
      if (w->live && fabsf(wrap_pi(G.S.s.th - G.S.stream_th[w->stream])) < 0.6f && w->y > G.S.s.y - 0.1f && w->y - G.S.s.y < 1.6f) wave_near = 1;
    }
    if (G.S.s.state == SP_WIN) {
      G.result_ticks = G.S.win_tick;
      int lv = G.level;
      G.medal = medal_of(lv, G.S.win_tick);
      if (!SAVE.lv[lv].cleared || G.S.win_tick < SAVE.lv[lv].best_ticks) {
        SAVE.lv[lv].best_ticks = G.S.win_tick, G.new_best = 1;
        /* the ghost: this climb's track, whole (a climb over two minutes keeps none: it is not the best's) */
        rec_sample(&G.S);
        bool whole = G.S.win_tick / GHOST_EVERY < GHOST_MAX - 1;
        if (!sw_no_ghost && whole) SAVE.ghost[lv] = rec;
        else SAVE.ghost[lv].n = 0;
      }
      rec_hash = (int32_t)track_hash(&rec);
      G.ribbons = (G.S.flies == MAX_FLIES ? 1 : 0) | (G.S.washes + G.S.knocks == 0 ? 2 : 0);
      if (G.ribbons & 2) SAVE.dry |= (uint8_t)(1u << lv);
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
    if (G.state_ticks > 5) { /* the text settings (render-only): left / right the size, up / down the background, a tap on a row steps it */
      pause_geo pg = pause_drawn;
      int step = 0, row = -1;
      if (pressed(G.a_right)) row = 0, step = 1;
      else if (pressed(G.a_left)) row = 0, step = -1;
      else if (pressed(G.a_down)) row = 1, step = 1;
      else if (pressed(G.a_up)) row = 1, step = -1;
      for (int i = 0; i < 2; i++)
        if (mouse_clicked && m.x >= pg.row[i].x && m.x < pg.row[i].x + pg.row[i].w && m.y >= pg.row[i].y && m.y < pg.row[i].y + pg.row[i].h) row = i, step = 1;
      if (row == 0) step > 0 ? cw_text_scale_cycle() : cw_text_scale_cycle_back();
      if (row == 1) {
        cw_text_backdrop_cycle();
        if (step < 0) cw_text_backdrop_cycle(); /* three settings: twice forward is one back */
      }
      if (row >= 0) {
        save_write();
        sfx(SFX_SELECT, 0.5f);
        break;
      }
      pause_quit_top = pg.quit_top;
    }
    if (G.state_ticks > 5 && (pressed(G.a_pause) || pressed(G.a_jump) || pressed(G.a_start) || mouse_clicked)) {
      /* under the settings the card is QUIT for the pointer; keys and pads resume */
      if (mouse_clicked && m.y >= pause_quit_top) load_world(0, true), G.state = GS_SELECT;
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
  dbg_hands = SAVE.hands;
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
  { /* the best climb's ghost, on the climb's clock; at its end it has reached the gutter and goes */
    const ghost_track *g = &SAVE.ghost[G.level];
    float f = (G.prev.tick + a * (G.S.tick - G.prev.tick)) / (float)GHOST_EVERY;
    int i = (int)f;
    bool on = !sw_no_ghost && (G.state == GS_PLAY || G.state == GS_PAUSE) && g->n > 1 && i < g->n - 1;
    float th = 0, y = 0, r = 0, t0 = 0, y0 = 0, r0 = 0, t1 = 0, y1 = 0, r1 = 0;
    if (on) {
      float k = f - i;
      track_at(g, i, &t0, &y0, &r0);
      track_at(g, i + 1, &t1, &y1, &r1);
      th = t0 + wrap_pi(t1 - t0) * k, y = y0 + (y1 - y0) * k, r = r0 + (r1 - r0) * k;
      /* on the spider's own path it would sit inside it: it shows only when it is a body length away */
      float sth = G.prev.s.th + wrap_pi(G.S.s.th - G.prev.s.th) * a, sy = G.prev.s.y + (G.S.s.y - G.prev.s.y) * a;
      float dx = wrap_pi(th - sth) * PIPE_R, dy = y - sy;
      if (dx * dx + dy * dy < 0.3f * 0.3f) on = false;
    }
    ghost_shown = on || (g->n > 1 && i < g->n - 1 && !sw_no_ghost && (G.state == GS_PLAY || G.state == GS_PAUSE));
    if (on) {
      float face = cw_atan2(wrap_pi(t1 - t0) * PIPE_R, fmaxf(y1 - y0, 0.02f)); /* heading along its track, up the pipe */
      world_ghost(true, th, y, r, face);
    } else world_ghost(false, 0, 0, 0, 0);
  }
  world_camera(G.camera, &G.prev, &G.S, a, frame_t, G.state == GS_TITLE || G.state == GS_SELECT);
#ifdef SPOUT_AUTOPILOT
  { /* art captures: "dist dphi lift ahead" round the spout from the spider (radians, metres) */
    const char *ac = getenv("SPOUT_ART_CAM");
    float d, dp, lift, ahead, th;
    if (ac && sscanf(ac, "%f %f %f %f", &d, &dp, &lift, &ahead) == 4 && cw_node_valid(G.camera)) {
      t3_vec3 p = world_spider_pos(&G.prev, &G.S, a, &th);
      cw_node3d_set_position(G.camera, (t3_vec3){ d * cw_sin(th + dp), p.y + lift, d * cw_cos(th + dp) });
      cw_node3d_look_at(G.camera, (t3_vec3){ p.x * 0.6f, p.y + ahead, p.z * 0.6f }, (t3_vec3){ 0, 1, 0 });
    }
  }
#endif
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
  /* a miniature's depth of field on High and Ultra: the spider sharp, the garden behind it soft, as a macro lens sees
   * a diorama (0.5 ms GL, 1.2 ms WebGPU on the test GPU; the lower presets keep every pixel sharp) */
  {
    cw_node post = cw_node_get(G.scene, "Post");
    bool dof = cw_node_valid(post) && G.state == GS_PLAY && cw_quality_get() >= CW_QUALITY_HIGH;
    if (cw_node_valid(post)) {
      cw_node_set(post, "dof", cw_val_bool(dof));
      if (dof) {
        t3_vec3 p = world_spider_pos(&G.prev, &G.S, a, NULL), e = cw_node3d_position(G.camera);
        float d = sqrtf((p.x - e.x) * (p.x - e.x) + (p.y - e.y) * (p.y - e.y) + (p.z - e.z) * (p.z - e.z));
        cw_node_set(post, "dof_focus_distance", cw_val_float(d));
        cw_node_set(post, "dof_focus_range", cw_val_float(0.9f));
        cw_node_set(post, "dof_blur", cw_val_float(7));
        cw_node_set(post, "dof_near_blur", cw_val_float(0.3f));
      }
    }
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
  read_device();
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
/* text on the game's own paper (a torn card, a tile) says so: the player's
 * Text background setting adds no second panel behind it */
static bool on_paper = true;
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = al, .wrap_width = wrap, .own_backdrop = on_paper };
  cw_r2d_text(font_ui, s, cw_v2(x, y), &st);
}
/* a line's size as drawn (the player's text size included), wrapped at wrap when wrap > 0 */
static cw_vec2 text_box(const char *s, float size, float wrap) {
  cw_text_style st = { .size = size, .wrap_width = wrap };
  cw_text_metrics m = cw_font_measure(font_ui, s, &st);
  return cw_v2(m.width, m.height);
}
static float line_h(float size) { return text_box("Ag", size, 0).y; }
/* text with button prompts in it ({jump}, {left|right}: cartwheel/prompt.h)
 * for P1's device, centred in [x, x + wrap]. The paper look keeps its ink:
 * the words are drawn as text() draws them and each {action} as the
 * engine's glyph in its light fill (cw_prompt_actions), laid out here, so a
 * glyph reads as rings and dots on the paper instead of an inked blob. */
static float ptext_run(const char *s, float x, float y, float size, cw_color c, bool draw) {
  cw_text_style st = { .size = size, .color = c, .own_backdrop = on_paper };
  float lh = cw_font_measure(font_ui, "Ag", &st).height, gap = size * 0.25f;
  float space = cw_font_measure(font_ui, "a a", &st).width - cw_font_measure(font_ui, "aa", &st).width;
  cw_color fill = rgba(1, 1, 1, c.a);
  float w = 0;
  char buf[160];
  for (const char *p = s; *p;) {
    const char *o = strchr(p, '{');
    size_t n = o ? (size_t)(o - p) : strlen(p);
    if (n) { /* a run of words */
      snprintf(buf, sizeof buf, "%.*s", (int)n, p);
      float tw = cw_font_measure(font_ui, buf, &st).width;
      for (size_t t = n; t > 0 && buf[t - 1] == ' '; t--) tw += space; /* the measure drops trailing spaces */
      if (draw) cw_r2d_text(font_ui, buf, cw_v2(x + w, y), &st);
      w += tw;
    }
    if (!o) break;
    const char *e = strchr(o, '}');
    if (!e) break;
    cw_action acts[4];
    int na = 0;
    for (const char *q = o + 1; q < e && na < 4;) { /* {a} or {a|b} or {a|b|c|d} */
      const char *bar = memchr(q, '|', (size_t)(e - q));
      size_t k = bar ? (size_t)(bar - q) : (size_t)(e - q);
      snprintf(buf, sizeof buf, "%.*s", (int)k, q);
      acts[na++] = cw_action_find(buf);
      q += k + (bar ? 1 : 0);
    }
    cw_vec2 g = cw_prompt_actions_size(acts, na, 0, size);
    if (draw) cw_prompt_actions(acts, na, 0, cw_v2(x + w + gap * 0.5f, y + (lh - g.y) * 0.5f), size, fill);
    w += g.x + gap;
    p = e + 1;
  }
  return w;
}
static void ptext(const char *s, float x, float y, float size, cw_color c, float wrap) {
  float w = ptext_run(s, 0, 0, size, c, false);
  ptext_run(s, x + (wrap - w) * 0.5f, y, size, c, true);
}
/* a prompt line that breaks at its wide gaps (three spaces or more) into as
 * many lines as it needs to fit [x, x + wrap], each centred; returns its
 * height and its widest line (*w) */
static float ptext_block(const char *s, float x, float y, float size, cw_color c, float wrap, bool draw, float *w) {
  char line[192] = "", cand[192];
  float lh = line_h(size) * 1.1f, h = 0, widest = 0;
  int gap_n = 0; /* the gap before this piece, kept when the line holds both */
  for (const char *p = s; *p;) {
    const char *g = strstr(p, "   ");
    size_t n = g ? (size_t)(g - p) : strlen(p);
    snprintf(cand, sizeof cand, "%s%.*s%.*s", line, line[0] ? gap_n : 0, "                ", (int)n, p);
    if (line[0] && ptext_run(cand, 0, 0, size, c, false) > wrap) {
      float lw = ptext_run(line, 0, 0, size, c, false);
      widest = lw > widest ? lw : widest;
      if (draw) ptext(line, x, y + h, size, c, wrap);
      h += lh;
      snprintf(line, sizeof line, "%.*s", (int)n, p);
    } else
      memcpy(line, cand, sizeof line);
    p += n;
    for (gap_n = 0; *p == ' '; p++) gap_n += gap_n < 16;
  }
  if (line[0]) {
    float lw = ptext_run(line, 0, 0, size, c, false);
    widest = lw > widest ? lw : widest;
    if (draw) ptext(line, x, y + h, size, c, wrap);
    h += line_h(size);
  }
  if (w) *w = widest;
  return h;
}
static cw_text_style title_style(float size, cw_color c, cw_text_align al, float wrap) {
  return (cw_text_style){ .size = size, .color = c, .align = al, .wrap_width = wrap, .outline_width = size * 0.06f,
                          .outline_color = rgba(0.2f, 0.12f, 0.06f, c.a) };
}
static void title_text(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap) {
  cw_text_style st = title_style(size, c, al, wrap);
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}
static float title_h(const char *s, float size, float wrap) {
  cw_text_style st = title_style(size, rgba(1, 1, 1, 1), CW_ALIGN_LEFT, wrap);
  return cw_font_measure(font_title, s, &st).height;
}

static const char *prompt_go(void) {
  return glyphs() ? "press {jump} to climb" : on_touch() ? "tap to climb" : "click to climb";
}

/* the forecast card, top right: its size from its words; returns its rect */
static cw_rect forecast_rect(float W, float s, const char *now, const char *next) {
  float tw = fmaxf(text_box(now, 22 * s, 0).x, text_box(next, 19 * s, 0).x);
  float th = line_h(22 * s) + 4 * s + line_h(19 * s);
  float w = 72 * s + tw + 14 * s, h = fmaxf(74 * s, th + 16 * s);
  return cw_rect_make(W - 14 * s - w, 14 * s, w, h);
}
static void forecast_words(const sim *S, const char **now, char *next, size_t n) {
  const level_def *L = S->L;
  int left = 0, nx = WX_RAIN;
  switch (S->weather) {
  case WX_SUN: left = L->sun_ticks - S->weather_ticks, nx = WX_RAIN; break;
  case WX_WARN: left = L->warn_ticks - S->weather_ticks, nx = WX_RAIN; break;
  case WX_RAIN: left = L->rain_ticks - S->weather_ticks, nx = WX_SUN; break;
  default: left = 90 - S->weather_ticks, nx = WX_SUN; break;
  }
  bool snow = L->theme == THEME_WINTER, night = L->theme == THEME_CITY;
  *now = S->weather == WX_SUN ? (night ? "clear night" : "sunny") : S->weather == WX_WARN ? (snow ? "snow soon!" : "rain soon!") : S->weather == WX_RAIN ? (snow ? "snowing" : "raining") : "clearing";
  snprintf(next, n, "%s in %d", nx == WX_RAIN ? (snow ? "snow" : "rain") : (night ? "clear" : "sun"), (left + 59) / 60);
}
static void draw_forecast(cw_rect r, float s) {
  const sim *S = &G.S;
  const level_def *L = S->L;
  const char *now;
  char b[48];
  forecast_words(S, &now, b, sizeof b);
  torn(r, rgba(1, 0.98f, 0.92f, 0.95f), 3);
  cw_vec2 c = cw_v2(r.x + 40 * s, r.y + r.h * 0.5f);
  bool snow = L->theme == THEME_WINTER;
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
    for (int i = 0; i < 3; i++) {
      if (snow) {
        cw_vec2 flake=cw_v2(c.x-12*s+i*12*s,c.y+21*s);
        for(int arm=0;arm<3;arm++) {
          float a=arm*1.047198f,dx=4*s*cw_cos(a),dy=4*s*cw_sin(a);
          cw_r2d_line(cw_v2(flake.x-dx,flake.y-dy),cw_v2(flake.x+dx,flake.y+dy),1.5f*s,rgba(.3f,.5f,.7f,1));
        }
      } else cw_r2d_line(cw_v2(c.x - 10 * s + i * 9 * s, c.y + 14 * s), cw_v2(c.x - 13 * s + i * 9 * s, c.y + 24 * s), 2.5f * s, rgba(0.3f, 0.5f, 0.9f, 1));
    }
  }
  float h1 = line_h(22 * s), y = r.y + (r.h - h1 - 4 * s - line_h(19 * s)) * 0.5f;
  text(now, r.x + 72 * s, y, 22 * s, S->weather == WX_WARN ? rgba(0.75f, 0.2f, 0.1f, 1) : rgba(0.2f, 0.15f, 0.1f, 1), CW_ALIGN_LEFT, 0);
  text(b, r.x + 72 * s, y + h1 + 4 * s, 19 * s, rgba(0.35f, 0.3f, 0.25f, 1), CW_ALIGN_LEFT, 0);
}

static void draw_meter(float W, float H, float s, float top) {
  (void)W;
  const sim *S = &G.S;
  float x = 26 * s, y0 = H - 40 * s, y1 = fmaxf(110 * s, top + 14 * s);
  torn(cw_rect_make(x - 10 * s, y1 - 14 * s, 20 * s, y0 - y1 + 28 * s), rgba(0.75f, 0.85f, 0.95f, 0.9f), 9);
  for (int i = 0; i < S->L->n_checks; i++) {
    float y = y0 + (y1 - y0) * S->L->checks[i] / S->L->height;
    cw_r2d_rect(cw_rect_make(x - 9 * s, y - 2 * s, 18 * s, 4 * s), i <= S->checkpoint ? rgba(0.3f, 0.6f, 0.3f, 1) : rgba(0.45f, 0.45f, 0.5f, 1));
  }
  if (S->net_y >= 0) { /* the web, where it waits below: six silk spokes on the meter */
    float wy = y0 + (y1 - y0) * cw_clampf(S->net_y / S->L->height, 0, 1);
    for (int k = 0; k < 6; k++) {
      float a = k * 1.0471976f;
      cw_r2d_line(cw_v2(x - 8 * s * cw_cos(a), wy - 8 * s * cw_sin(a)), cw_v2(x + 8 * s * cw_cos(a), wy + 8 * s * cw_sin(a)), 2 * s, rgba(1, 1, 1, 0.95f));
    }
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
  /* the button's name is part of the button, sized to it (well over the body floor): it keeps its size */
  cw_text_style st = title_style(r * 0.42f, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r * 4);
  st.fixed_size = true;
  cw_r2d_text(font_title, label, cw_v2(cc.x - r * 2, cc.y - r * 0.28f), &st);
}
static void draw_touch(void) {
  int ww, wh;
  cw_window_size(&ww, &wh);
  float k = (float)wh / 540.0f, jx, jy, jr, bx, by, br;
  touch_rects(&jx, &jy, &jr, &bx, &by, &br);
  bool on = cw_touch_control_active(T_STICK);
  cw_vec2 wc = cw_touch_stick_center(T_STICK); /* the engine keeps the ring at its home */
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

/* is the HUD over the pipe above the spider? (hud.over_pipe: of 12 samples of the pipe from the spider up 2.4 m, those
 * whose strip on screen any card or banner covers). sw old_hud 1: the card in the middle of the top and the banners
 * over the climb, as before (the gate's control) */
static bool scr(t3_vec3 p, float W, float H, cw_vec2 *out) {
  t3_camera *c = cw_node_valid(G.camera) ? (t3_camera *)cw_node3d_object(G.camera) : NULL;
  if (!c) return false;
  t3_vec3 v = t3_vec3_apply_mat4(p, &c->matrix_world_inverse);
  if (v.z > -0.1f) return false;
  t3_vec3 n = t3_vec3_apply_mat4(v, &c->projection_matrix);
  *out = cw_v2((n.x * 0.5f + 0.5f) * W, (0.5f - n.y * 0.5f) * H);
  return true;
}
static bool hits(cw_rect r, float x0, float x1, float y) { return r.w > 0 && y >= r.y && y <= r.y + r.h && x1 >= r.x && x0 <= r.x + r.w; }
static void hud_measure(float W, float H, float s, cw_rect card, cw_rect forecast, float banner_y, float banner_x, float banner_w) {
  const sim *S = &G.S;
  bool banner = (S->s.state == SP_WASHED && S->s.stun > 0) || sim_gust_warn(S) || sim_gusting(S) || S->weather == WX_WARN;
  cw_rect b = banner ? cw_rect_make(banner_x, banner_y, banner_w, 2 * title_h("Mg", 40 * s, 0)) : cw_rect_make(0, 0, 0, 0);
  hud_over_pipe = hud_samples = 0;
  float th = S->s.th;
  for (int i = 0; i < 12; i++) {
    float y = S->s.y + 0.2f + i * 0.2f;
    cw_vec2 a, l, r;
    t3_vec3 axis = { 0, y, 0 }, side = { cw_cos(th) * PIPE_R, 0, -cw_sin(th) * PIPE_R };
    if (!scr(axis, W, H, &a) || !scr((t3_vec3){ side.x, y, side.z }, W, H, &l) || !scr((t3_vec3){ -side.x, y, -side.z }, W, H, &r)) continue;
    float x0 = fminf(l.x, r.x), x1 = fmaxf(l.x, r.x);
    if (a.y < 0 || a.y > H) continue;
    hud_samples++;
    hud_over_pipe += hits(card, x0, x1, a.y) || hits(forecast, x0, x1, a.y) || hits(b, x0, x1, a.y);
  }
}

/* the HUD: every card sized from its words, so a larger text size keeps them apart */
static void draw_hud(float W, float H, float s) {
  const sim *S = &G.S;
  char b[64], nx[48];
  const char *now;
  int secs = S->tick / 60;
  snprintf(b, sizeof b, "%d:%02d   dew %d/%d   flies %d/3", secs / 60, secs % 60, S->dews, S->L->n_dews, S->flies);
  forecast_words(S, &now, nx, sizeof nx);
  cw_rect fr = forecast_rect(W, s, now, nx);
  cw_vec2 tb = text_box(b, 26 * s, 0);
  float pw = fmaxf(340 * s, tb.x + 40 * s), ph = fmaxf(56 * s, tb.y + 20 * s);
  float px = W * 0.5f - pw * 0.5f, left = 48 * s; /* the meter's column */
  /* the card stands on the left rail, beside the meter: the middle of the top is the pipe the spider climbs into */
  if (!sw_old_hud) px = left, pw = tb.x + 40 * s; /* (as wide as its words: the pipe stands just right of it) */
  if (px + pw > fr.x - 10 * s) px = fmaxf(left, fr.x - 10 * s - pw);
  if (px + pw > fr.x - 10 * s) fr.y = 12 * s + ph + 8 * s; /* no room beside it: the forecast goes under */
  torn(cw_rect_make(px, 12 * s, pw, ph), rgba(1, 0.98f, 0.92f, 0.95f), 6);
  text(b, px, 12 * s + (ph - tb.y) * 0.5f, 26 * s, rgba(0.22f, 0.16f, 0.1f, 1), CW_ALIGN_CENTER, pw);
  /* silk: a little spool per charge, under the card */
  float sy = 12 * s + ph + 16 * s, scx = px + pw * 0.5f;
  for (int i = 0; i < SILK_MAX; i++) {
    cw_vec2 c = cw_v2(scx - 52 * s + i * 26 * s, sy);
    if (i >= S->silk && i >= 3) continue;
    cw_r2d_circle(c, 9 * s, 16, i < S->silk ? rgba(0.97f, 0.97f, 1, 0.95f) : rgba(0.3f, 0.3f, 0.35f, 0.35f));
    cw_r2d_circle_lines(c, 9 * s, 2 * s, 16, rgba(0.35f, 0.3f, 0.4f, 0.8f));
  }
  draw_forecast(fr, s);
  float top = fmaxf(sy + 9 * s, fr.y + fr.h); /* the lowest of the cards */
  draw_meter(W, H, s, 0);
  float by = fmaxf(H * 0.18f, top + 10 * s), wy = fmaxf(H * 0.3f, top + 10 * s);
  /* the banners go to the lower third and the right half, beside and under the spider: the pipe and the climb above
   * it stand left of the middle (the camera looks from a little to the left) */
  float bx = 0, bw = W;
  if (!sw_old_hud) by = wy = H * 0.72f, bx = W * 0.58f, bw = W - bx - 16 * s;
  hud_measure(W, H, s, cw_rect_make(px, 12 * s, pw, sy + 9 * s - 12 * s), fr, by, bx, bw);
  if (S->s.state == SP_WASHED && S->s.stun > 0 && S->pecks && S->bird_ticks > BIRD_WARN) title_text("pecked!", bx, wy, 40 * s, rgba(1, 0.8f, 0.5f, 1), CW_ALIGN_CENTER, bw);
  else if (S->s.state == SP_WASHED && S->s.stun > 0) title_text("washed down!", bx, wy, 40 * s, rgba(0.55f, 0.75f, 1, 1), CW_ALIGN_CENTER, bw);
  else if (sim_gust_warn(S)) title_text("wind's picking up - hold tight!", bx + 16 * s * (sw_old_hud != 0), by, 30 * s, rgba(0.8f, 0.9f, 1, 1), CW_ALIGN_CENTER, sw_old_hud ? W - 32 * s : bw);
  else if (sim_gusting(S)) title_text("whoosh!", bx, by, 36 * s, rgba(0.8f, 0.9f, 1, 1), CW_ALIGN_CENTER, bw);
  else if (S->weather == WX_WARN)
    title_text(S->L->theme == THEME_WINTER ? "snow's coming - hold tight!" : "rain's coming - hold tight!", bx + 16 * s * (sw_old_hud != 0), by, 30 * s, rgba(1, 0.85f, 0.5f, 1),
               CW_ALIGN_CENTER, sw_old_hud ? W - 32 * s : bw);
  if (on_touch()) draw_touch();
  else if (G.state_ticks < 600) {
    const char *h = G.S.hands ? "{hand_l} then {hand_r}, in turn: climb   {left|right|down} walk round, down   {jump} jump   {brace} hold   {silk} silk up   {down} + {silk} line down   {brace} + {silk} web"
                              : "{left|right|up|down} climb   {jump} jump   {brace} hold   {silk} silk up   {down} + {silk} line down   {brace} + {silk} web";
    float hw, hh = ptext_block(h, 0, 0, 22 * s, rgba(0, 0, 0, 1), W - 120 * s, false, &hw);
    float bh = hh + 18 * s, y = H - 14 * s - bh;
    torn(cw_rect_make(W * 0.5f - (hw + 40 * s) * 0.5f, y, hw + 40 * s, bh), rgba(1, 0.98f, 0.92f, 0.85f), 8);
    ptext_block(h, 60 * s, y + 9 * s, 22 * s, rgba(0.25f, 0.2f, 0.15f, 1), W - 120 * s, true, NULL);
  }
}

static void draw_title(float W, float H, float s) {
  const char *sub = "a paper spider, a long wet climb";
  cw_text_style ts = title_style(96 * s, rgba(0.95f, 0.55f, 0.3f, 1), CW_ALIGN_CENTER, W);
  ts.fixed_size = true; /* the name is the logo: art, drawn as it is */
  float logo_h = cw_font_measure(font_title, SPOUT_TITLE, &ts).height;
  float pw = fminf(W - 16 * s, fmaxf(460 * s, text_box(sub, 26 * s, 0).x + 40 * s));
  cw_vec2 sb = text_box(sub, 26 * s, pw - 24 * s);
  float sub_y = fmaxf(114 * s, 10 * s + logo_h);
  torn(cw_rect_make(W * 0.5f - pw * 0.5f, H * 0.1f, pw, sub_y + sb.y + 8 * s), rgba(1, 0.97f, 0.88f, 0.95f), 1);
  cw_r2d_text(font_title, SPOUT_TITLE, cw_v2(0, H * 0.1f + 10 * s), &ts);
  text(sub, W * 0.5f - pw * 0.5f + 12 * s, H * 0.1f + sub_y, 26 * s, rgba(0.3f, 0.22f, 0.15f, 1), CW_ALIGN_CENTER, pw - 24 * s);
  float p = 0.6f + 0.4f * cw_sin(frame_t * 3);
  float gw, gh = ptext_block(prompt_go(), 0, 0, 28 * s, rgba(0, 0, 0, 1), W - 56 * s, false, &gw);
  float bw = fmaxf(260 * s, gw + 40 * s), bh = gh + 18 * s, by = fminf(H * 0.82f, H - 8 * s - bh);
  torn(cw_rect_make(W * 0.5f - bw * 0.5f, by, bw, bh), rgba(1, 0.98f, 0.92f, 0.6f + 0.3f * p), 2);
  ptext_block(prompt_go(), 28 * s, by + 9 * s, 28 * s, rgba(0.25f, 0.18f, 0.12f, 1), W - 56 * s, true, NULL);
}

/* the spout select's layout, from the size its words draw at (the pointer's
 * tiles in the fixed tick and the drawing share it) */
static const char *select_hint(void) {
  if (!glyphs()) return on_touch() ? "tap a spout" : "click a spout to climb";
  return SAVE.hands ? "{left|right} choose,   {jump} to climb,   {silk} climb: two hands" : "{left|right} choose,   {jump} to climb,   {silk} climb: the stick";
}
static select_geo select_layout(float W, float H, float s) {
  select_geo g;
  g.tw = W / 6.2f, g.gap = (W - g.tw * SP_LEVELS) / (SP_LEVELS + 1), g.wrap = W - 32 * s;
  float name_h = 0, blurb_h = 0;
  for (int i = 0; i < SP_LEVELS; i++) {
    name_h = fmaxf(name_h, text_box(LEVELS[i].name, 20 * s, g.tw - 12 * s).y);
    blurb_h = fmaxf(blurb_h, text_box(LEVELS[i].blurb, 26 * s, g.wrap).y);
  }
  g.name_y = fmaxf(62 * s, 12 * s + title_h("5", 40 * s, 0));
  g.stat_y = fmaxf(g.tw * 0.95f, g.name_y + name_h + 6 * s);
  g.th = fmaxf(g.tw * 1.25f, g.stat_y + line_h(19 * s) + 10 * s);
  float hint_h = ptext_block(select_hint(), 0, 0, 22 * s, rgba(0, 0, 0, 1), g.wrap, false, NULL);
  g.hint_y = fminf(H * 0.9f, H - 8 * s - hint_h);
  float top = H * 0.12f + title_h("pick a spout", 52 * s, 0) + 14 * s;
  g.y = fmaxf(top, fminf(H * 0.36f, g.hint_y - 10 * s - blurb_h - 18 * s - g.th));
  g.blurb_y = g.y + g.th + 18 * s;
  return g;
}

static void draw_select(float W, float H, float s) {
  select_geo g = select_layout(W, H, s);
  select_drawn = g;
  title_text("pick a spout", 0, H * 0.12f, 52 * s, rgba(0.95f, 0.55f, 0.3f, 1), CW_ALIGN_CENTER, W);
  static const float TC[THEME_COUNT][3] = { { 0.75f, 0.9f, 0.65f }, { 0.55f, 0.6f, 0.85f }, { 0.95f, 0.7f, 0.45f }, { 0.85f, 0.92f, 1 }, { 0.5f, 0.8f, 0.55f } };
  float tw = g.tw;
  for (int i = 0; i < SP_LEVELS; i++) {
    float x = g.gap + i * (tw + g.gap), y = g.y;
    const level_def *L = &LEVELS[i];
    bool open = level_open(i), sel = i == G.cursor;
    float lift = sel ? -8 * s : 0;
    torn(cw_rect_make(x, y + lift, tw, g.th), rgba(TC[L->theme][0], TC[L->theme][1], TC[L->theme][2], open ? 1 : 0.55f), (uint32_t)(20 + i));
    if (sel) cw_r2d_polyline((cw_vec2[]){ cw_v2(x - 4, y + lift - 4), cw_v2(x + tw + 4, y + lift - 4), cw_v2(x + tw + 4, y + lift + g.th + 4), cw_v2(x - 4, y + lift + g.th + 4) }, 4, 4 * s, true, rgba(0.95f, 0.55f, 0.3f, 1));
    char b[32];
    snprintf(b, sizeof b, "%d", i + 1);
    title_text(b, x, y + lift + 8 * s, 40 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, tw);
    text(L->name, x + 6 * s, y + lift + g.name_y, 20 * s, rgba(0.2f, 0.15f, 0.1f, 1), CW_ALIGN_CENTER, tw - 12 * s);
    if (!open) {
      text(i < LEVELS_BUILT ? "locked" : "coming soon", x + 4 * s, y + lift + g.stat_y, 19 * s, rgba(0.35f, 0.3f, 0.3f, 1), CW_ALIGN_CENTER, tw - 8 * s);
    } else if (SAVE.lv[i].cleared) {
      int t = SAVE.lv[i].best_ticks / 60;
      snprintf(b, sizeof b, "best %d:%02d", t / 60, t % 60);
      int m = medal_of(i, SAVE.lv[i].best_ticks);
      paper_medal(x + tw - 18 * s, y + lift + 18 * s, 11 * s, m, 1);
      int rk = 0;
      if (SAVE.lv[i].flies >= MAX_FLIES) paper_ribbon(x + 16 * s + rk++ * 20 * s, y + lift + 16 * s, 7 * s, 0, 1);
      if (SAVE.dry >> i & 1) paper_ribbon(x + 16 * s + rk++ * 20 * s, y + lift + 16 * s, 7 * s, 1, 1);
      text(b, x + 4 * s, y + lift + g.stat_y, 19 * s, rgba(0.15f, 0.4f, 0.15f, 1), CW_ALIGN_CENTER, tw - 8 * s);
    }
  }
  const level_def *L = &LEVELS[G.cursor];
  on_paper = false; /* over the scene */
  text(L->blurb, 16 * s, g.blurb_y, 26 * s, rgba(0.2f, 0.15f, 0.1f, 1), CW_ALIGN_CENTER, g.wrap);
  ptext_block(select_hint(), 16 * s, g.hint_y, 22 * s, rgba(0.3f, 0.25f, 0.2f, 1), g.wrap, true, NULL);
  on_paper = true;
}

/* the graphics settings from the pause card (cartwheel/quality_menu.h): the
 * settings action opens the page (a click on the card already means resume or quit),
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
  (void)in;
  if (pressed(G.a_settings)) cw_quality_menu_open();
}

/* the pause card: paused, how to resume and quit, the text size and text
 * background (left / right and up / down step them; a tap on a row steps
 * it), the graphics line, and (for the pointer) the quit line. Laid out from
 * the size its words draw at; the fixed tick's pointer shares it */
static void pause_lines(char rows[2][80], char *gfx, size_t n) {
  char bg[16];
  snprintf(bg, sizeof bg, "%s", cw_text_backdrop_label());
  for (char *c = bg; *c; c++) *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
  snprintf(rows[0], 80, glyphs() ? "{left|right} text size  %s" : "text size: %s", cw_text_scale_label());
  snprintf(rows[1], 80, glyphs() ? "{up|down} text background  %s" : "text background: %s", bg);
  snprintf(gfx, n, "graphics: %s", cw_quality_label());
  for (char *c = gfx; *c; c++) *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
  if (!on_touch()) snprintf(gfx + strlen(gfx), n - strlen(gfx), "   ({settings} settings)");
}
static const char *pause_prompt(void) { return glyphs() ? "{jump} resume    {back} quit" : "tap the top: resume"; }
static pause_geo pause_layout(float W, float H, float s) {
  pause_geo g;
  char rows[2][80], gfx[96];
  pause_lines(rows, gfx, sizeof gfx);
  cw_color ink = rgba(0, 0, 0, 1);
  float th = title_h("paused", 48 * s, 0), ph = line_h(24 * s), rh = line_h(24 * s), gh = line_h(20 * s);
  float w = 340 * s;
  w = fmaxf(w, ptext_run(pause_prompt(), 0, 0, 24 * s, ink, false) + 48 * s);
  for (int i = 0; i < 2; i++) w = fmaxf(w, ptext_run(rows[i], 0, 0, 24 * s, ink, false) + 48 * s);
  w = fmaxf(w, ptext_run(gfx, 0, 0, 20 * s, ink, false) + 48 * s);
  if (!glyphs()) w = fmaxf(w, text_box("tap here: quit to spouts", 24 * s, 0).x + 48 * s);
  w = fminf(w, W - 16 * s);
  float h = 12 * s + th + 6 * s + ph + 14 * s + 2 * rh + 6 * s + 10 * s + gh + (glyphs() ? 0 : 14 * s + rh) + 18 * s;
  float y = fmaxf(8 * s, (H - h) * 0.5f);
  g.card = cw_rect_make(W * 0.5f - w * 0.5f, y, w, h);
  y += 12 * s;
  g.title_y = y, y += th + 6 * s;
  g.prompt_y = y, y += ph + 14 * s;
  g.row[0] = cw_rect_make(g.card.x, y - 3 * s, w, rh + 6 * s), y += rh + 6 * s;
  g.row[1] = cw_rect_make(g.card.x, y - 3 * s, w, rh + 6 * s), y += rh + 10 * s;
  g.gfx_y = y, y += gh;
  g.quit_top = y + 4 * s; /* the pointer: at or under the quit line quits */
  g.quit_y = y + 14 * s;
  return g;
}

static void draw_pause(float W, float H, float s) {
  pause_geo g = pause_layout(W, H, s);
  pause_drawn = g;
  char rows[2][80], gfx[96];
  pause_lines(rows, gfx, sizeof gfx);
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.1f, 0.08f, 0.06f, 0.35f));
  torn(g.card, rgba(1, 0.97f, 0.88f, 0.97f), 11);
  title_text("paused", 0, g.title_y, 48 * s, rgba(0.95f, 0.55f, 0.3f, 1), CW_ALIGN_CENTER, W);
  ptext(pause_prompt(), 0, g.prompt_y, 24 * s, rgba(0.25f, 0.2f, 0.15f, 1), W);
  for (int i = 0; i < 2; i++) {
    cw_r2d_rect(g.row[i], rgba(0.95f, 0.55f, 0.3f, 0.14f));
    ptext(rows[i], 0, g.row[i].y + 3 * s, 24 * s, rgba(0.25f, 0.2f, 0.15f, 1), W);
  }
  ptext(gfx, 0, g.gfx_y, 20 * s, rgba(0.4f, 0.3f, 0.2f, 1), W);
  if (!glyphs()) text("tap here: quit to spouts", 0, g.quit_y, 24 * s, rgba(0.6f, 0.2f, 0.1f, 1), CW_ALIGN_CENTER, W);
}

/* paper medals per spout, by the climb's time: gold at par (a clean climb that braces for the waves it meets),
 * silver within a third more, bronze for getting to the top at all */
static const int PAR_SECS[SP_LEVELS] = { 24, 31, 30, 30, 40 };
static const char *const MEDAL[3] = { "GOLD", "SILVER", "BRONZE" };
static const float MEDAL_RGB[3][3] = { { 0.95f, 0.75f, 0.2f }, { 0.75f, 0.78f, 0.82f }, { 0.75f, 0.45f, 0.22f } };
static int medal_of(int level, int ticks) {
  float secs = ticks / 60.0f, par = (float)PAR_SECS[level % SP_LEVELS];
  return secs <= par ? 0 : secs <= par * 1.34f ? 1 : 2;
}
static void paper_medal(float cx, float cy, float r, int m, float a) {
  cw_r2d_circle(cw_v2(cx + 2, cy + 3), r, 24, rgba(0.2f, 0.15f, 0.1f, 0.3f * a));
  cw_r2d_circle(cw_v2(cx, cy), r, 24, rgba(MEDAL_RGB[m][0], MEDAL_RGB[m][1], MEDAL_RGB[m][2], a));
  cw_r2d_circle_lines(cw_v2(cx, cy), r * 0.72f, r * 0.12f, 24, rgba(1, 1, 1, 0.5f * a));
}

/* a paper ribbon: a rosette and two tails, blue for every fly, green for a dry climb */
static void paper_ribbon(float cx, float cy, float r, int kind, float a) {
  cw_color c = kind ? rgba(0.35f, 0.7f, 0.4f, a) : rgba(0.3f, 0.5f, 0.9f, a);
  cw_vec2 tl[3] = { cw_v2(cx - r * 0.5f, cy), cw_v2(cx - r * 0.85f, cy + r * 1.9f), cw_v2(cx - r * 0.1f, cy + r * 1.5f) };
  cw_vec2 tr[3] = { cw_v2(cx + r * 0.5f, cy), cw_v2(cx + r * 0.1f, cy + r * 1.5f), cw_v2(cx + r * 0.85f, cy + r * 1.9f) };
  cw_r2d_fan(cw_v2(cx - r * 0.45f, cy + r), tl, 3, true, c);
  cw_r2d_fan(cw_v2(cx + r * 0.45f, cy + r), tr, 3, true, c);
  cw_r2d_circle(cw_v2(cx + 1.5f, cy + 2), r, 20, rgba(0.2f, 0.15f, 0.1f, 0.3f * a));
  cw_r2d_circle(cw_v2(cx, cy), r, 20, c);
  cw_r2d_circle(cw_v2(cx, cy), r * 0.55f, 16, rgba(1, 0.98f, 0.92f, 0.9f * a));
}

static void draw_win(float W, float H, float s) {
  const sim *S = &G.S;
  float in = cw_clampf(G.state_ticks / 20.0f, 0, 1);
  char b[224];
  int t = S->win_tick / 60;
  int m = medal_of(G.level, S->win_tick);
  const char *rib = G.ribbons == 3 ? "\nribbons: every fly, a dry climb" : G.ribbons == 1 ? "\nribbon: every fly" : G.ribbons == 2 ? "\nribbon: a dry climb" : "";
  snprintf(b, sizeof b, "time %d:%02d%s\n%s medal (gold at %d:%02d)\ndewdrops %d of %d, flies %d of 3\nwashed down %d, held on %d%s", t / 60, t % 60,
           G.new_best ? "  (best!)" : "", MEDAL[m], PAR_SECS[G.level % SP_LEVELS] / 60, PAR_SECS[G.level % SP_LEVELS] % 60, S->dews, S->L->n_dews, S->flies,
           S->washes + S->knocks, S->braced_waves, rib);
  float th = title_h("to the top!", 52 * s, 0);
  cw_vec2 bb = text_box(b, 26 * s, W - 48 * s);
  float gw, gh = ptext_block(prompt_go(), 0, 0, 22 * s, rgba(0, 0, 0, 1), W - 48 * s, false, &gw);
  float w = fminf(W - 16 * s, fmaxf(420 * s, fmaxf(bb.x, gw) + 48 * s));
  float h = 10 * s + th + 12 * s + bb.y + 14 * s + gh + 18 * s;
  float y = fmaxf(8 * s, fminf(H * 0.22f, (H - h) * 0.5f));
  torn(cw_rect_make(W * 0.5f - w * 0.5f, y, w, h), rgba(1, 0.97f, 0.88f, 0.97f * in), 13);
  title_text("to the top!", 0, y + 10 * s, 52 * s, rgba(0.95f, 0.55f, 0.3f, in), CW_ALIGN_CENTER, W);
  paper_medal(W * 0.5f + w * 0.5f - 34 * s, y + 10 * s + th * 0.5f, 22 * s, m, in);
  for (int k = 0, n = 0; k < 2; k++)
    if (G.ribbons >> k & 1) paper_ribbon(W * 0.5f - w * 0.5f + 34 * s + n++ * 40 * s, y + 10 * s + th * 0.5f - 8 * s, 13 * s, k, in);
  text(b, 24 * s, y + 10 * s + th + 12 * s, 26 * s, rgba(0.25f, 0.2f, 0.15f, in), CW_ALIGN_CENTER, W - 48 * s);
  ptext_block(prompt_go(), 24 * s, y + h - 18 * s - gh, 22 * s, rgba(0.4f, 0.3f, 0.2f, in), W - 48 * s, true, NULL);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
#ifdef SPOUT_AUTOPILOT
  if (getenv("SPOUT_NO_HUD")) { /* art captures: the 3D alone */
    cw_r2d_end();
    return;
  }
#endif
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
