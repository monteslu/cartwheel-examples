/* chromite: a vertical shooter over a chrome fortress, after Chromium
 * B.S.U. (2000), on cartwheel and three.c (three.js r186).
 *
 *   flow      title (skill, start level, graphics) -> play -> level clear
 *             -> the next level, round and round with rising skill; game
 *             over -> the high score table
 *   playing   the stick, d-pad, WASD or arrows fly the ship; A / Space /
 *             J fires (hold it); B / X / K twice inside a second
 *             self-destructs (the ship's last resort: it ejects its stocks
 *             and sets off a super bomb); Start / Esc pauses. Touch: a
 *             floating stick on the left, FIRE and BOMB on the right.
 *   rules     the original's: shields soak 80% of each hit while they last,
 *             the hull takes the rest; an extra ship every 50000. An enemy
 *             that gets past costs points and the chain (modern, the
 *             default) or a ship (classic, the original's; title menu)
 *   score     kills within CHAIN_WINDOW of each other chain a multiplier
 *             (up to x5), a kill up close scores double, enemy fire a boss's
 *             fall or the super bomb cancels pays as score chips
 *   feel      hits, bombs and a boss's fall rumble the pad; a boss's fall
 *             holds the fight 3 ticks (no screen shake)
 *   save      high scores per skill, the furthest level reached, the
 *             graphics preset (cw_app_desc.save)
 *
 * Determinism: the game replays identically from the input stream (fixed
 * ticks, sim.c on cartwheel/math.h); the debug fields name what the gates
 * compare across hosts. chromite_auto flies itself (for hosts that send no
 * input). */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "cartwheel/vfs.h"
#include "game.h"

enum { GS_TITLE, GS_PLAY, GS_PAUSE, GS_CLEAR, GS_OVER };
#define SKILLS 7
static const char *const SKILL_NAME[SKILLS] = { "wimp", "easy", "normal", "experienced", "fun", "insane", "impossible" };
static const float SKILL_BASE[SKILLS] = { 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f };
#define MAX_START_LEVEL 10

/* ── save ───────────────────────────────────────────────────────────── */

#define SAVE_MAGIC 0x4d524843u /* 'CHRM' */
#define SAVE_VERSION 3
/* version 3: the text size and text background; version 2: the graphics word in 4 bytes (cw_quality_saved is
 * 30 bits since Very Low); version 1 kept one byte */
typedef struct save_data {
  uint32_t magic;
  uint16_t version, size;
  int32_t hi[SKILLS][5];
  int32_t hi_level[SKILLS][5];
  int32_t best_level;
  uint8_t skill, start_level, quality_v1, rules; /* rules: RULES_MODERN (0, what older saves hold there) or RULES_CLASSIC */
  uint32_t quality; /* cw_quality_saved(), 0 = none chosen */
  uint8_t text_size, text_bg, pad2[2]; /* cw_text_scale_saved() (0 = none chosen), cw_text_backdrop_get() */
  uint32_t checksum;
} save_data;
static save_data SAVE;

static uint32_t save_sum(void) { return cw_hash32(&SAVE.hi, offsetof(save_data, checksum) - offsetof(save_data, hi), CW_HASH32_INIT) ^ (SAVE.version | (uint32_t)SAVE.size << 16); }
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC;
  SAVE.version = SAVE_VERSION;
  SAVE.size = sizeof SAVE;
  SAVE.skill = 2;
  SAVE.start_level = 1;
  SAVE.best_level = 1;
  /* a table to beat, as the original shipped */
  for (int s = 0; s < SKILLS; s++)
    for (int i = 0; i < 5; i++) SAVE.hi[s][i] = 250000 - 50000 * i, SAVE.hi_level[s][i] = 1;
  SAVE.checksum = save_sum();
}
static void save_write(void) {
  SAVE.text_size = cw_text_scale_saved();
  SAVE.text_bg = (uint8_t)cw_text_backdrop_get();
  SAVE.checksum = save_sum();
  cw_save_commit();
}
/* a version 1 save: the same fields up to the graphics byte, its checksum where the graphics word now is */
static bool save_from_v1(void) {
  enum { V1_SIZE = offsetof(save_data, quality) + sizeof(uint32_t) };
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != 1 || SAVE.size != V1_SIZE) return false;
  uint32_t sum = cw_hash32(&SAVE.hi, offsetof(save_data, quality) - offsetof(save_data, hi), CW_HASH32_INIT) ^ (1u | (uint32_t)V1_SIZE << 16);
  if (SAVE.quality != sum) return false;
  SAVE.quality = SAVE.quality_v1; /* an old word reads as before */
  SAVE.quality_v1 = 0;
  SAVE.text_size = SAVE.text_bg = SAVE.pad2[0] = SAVE.pad2[1] = 0;
  SAVE.version = SAVE_VERSION;
  SAVE.size = sizeof SAVE;
  SAVE.checksum = save_sum();
  return true;
}
/* a version 2 save: the same fields up to the graphics word, its checksum where the text bytes now are */
static bool save_from_v2(void) {
  enum { V2_SIZE = offsetof(save_data, text_size) + sizeof(uint32_t) };
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != 2 || SAVE.size != V2_SIZE) return false;
  uint32_t stored, sum = cw_hash32(&SAVE.hi, offsetof(save_data, text_size) - offsetof(save_data, hi), CW_HASH32_INIT) ^ (2u | (uint32_t)V2_SIZE << 16);
  memcpy(&stored, &SAVE.text_size, sizeof stored);
  if (stored != sum) return false;
  SAVE.text_size = SAVE.text_bg = SAVE.pad2[0] = SAVE.pad2[1] = 0;
  SAVE.version = SAVE_VERSION;
  SAVE.size = sizeof SAVE;
  SAVE.checksum = save_sum();
  return true;
}
static void save_check(void) {
  if (save_from_v1() || save_from_v2()) return;
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) save_reset();
}

/* ── game state ─────────────────────────────────────────────────────── */

static struct {
  int32_t state, state_ticks, ticks, cursor, level_shown;
  sim S, prev;
  cw_node scene, camera, sun;
  cw_action a_left, a_right, a_up, a_down, a_fire, a_item, a_start, a_back, a_pause;
  int32_t rank;       /* the last game's place in the table: 1..5, 0 none */
  int32_t final_score, final_level;
  uint32_t state_hash;
  float hero_pos[2];
  int variation;
  int32_t live_enemies, live_ebullets, score32, lives, level, losses, kills;
  float shields, damage;
  int32_t pb_ticks, esc_ticks; /* the HUD's point-blank and got-past words: ticks left */
  int64_t esc_cost, esc_last;
  int32_t continues; /* continues taken (modern rules), for the gates */
} G;

static int32_t input_mode;
/* development jumps, written over the debug channel (romdev: wasm write):
 * cheat.level starts that level; cheat.boss jumps the level's clock to just
 * before its boss. Each clears itself once done. cheat.gallery (nonzero)
 * holds every craft still in a lineup over the deck, for model work.
 * cheat.blasts (n) sets off n blasts across the screen (effects only, the
 * rules never see them), for lighting work. */
static int32_t cheat_level, cheat_boss, cheat_gallery, cheat_blasts;
static int32_t dbg_skill, dbg_hi0, dbg_armed;
/* the rules and the score system, for the gates */
static int32_t dbg_boss_plates = -1, dbg_boss_tells;
/* what the screen shows, as the world draws it (for the newcomer gate, which plays by what is drawn): the craft
 * (x, y; 99 = none), and the enemy rounds nearest the ship */
#define VIEW_N 24
static float view_enemies[VIEW_N * 2], view_rounds[VIEW_N * 2];
static void view_fill(void) {
  int n = 0;
  for (int i = 0; i < MAX_ENEMIES && n < VIEW_N; i++)
    if (G.S.en[i].live && G.S.en[i].y < FIELD_Y) view_enemies[n * 2] = G.S.en[i].x, view_enemies[n * 2 + 1] = G.S.en[i].y, n++;
  for (; n < VIEW_N; n++) view_enemies[n * 2] = view_enemies[n * 2 + 1] = 99;
  /* the nearest VIEW_N rounds to the ship, nearest first */
  static float d2[MAX_EBULLETS];
  for (int i = 0; i < MAX_EBULLETS; i++) {
    const ebullet *b = &G.S.eb[i];
    float dx = b->x - G.S.h.x, dy = b->y - G.S.h.y;
    d2[i] = b->live ? dx * dx + dy * dy : 1e30f;
  }
  for (n = 0; n < VIEW_N; n++) {
    int k = 0;
    for (int i = 1; i < MAX_EBULLETS; i++)
      if (d2[i] < d2[k]) k = i;
    if (d2[k] >= 1e30f) break;
    view_rounds[n * 2] = G.S.eb[k].x, view_rounds[n * 2 + 1] = G.S.eb[k].y;
    d2[k] = 1e30f;
  }
  for (; n < VIEW_N; n++) view_rounds[n * 2] = view_rounds[n * 2 + 1] = 99;
}
static int32_t dbg_rules, dbg_chain, dbg_chain_best, dbg_mult10, dbg_escapes, dbg_point_blanks, dbg_cancelled, dbg_hitstop_ticks;
/* rumble requests made (presentation; the engine's cw.rumble shows the last one) */
static int32_t rumbles;
/* GAME OVER headings drawn on the last frame (the card must draw exactly one) */
static int32_t over_headings, over_headings_drawing;
/* sw double_over 1: the dying HUD keeps its own GAME OVER under the results card, as it once did (a control) */
static int32_t sw_double_over;
/* hit-stop: ticks the fight holds still when a command craft falls */
#define HITSTOP_BOSS 3
static int32_t hitstop;
static uint32_t dbg_mouse;
static bool device_used;
/* the control scheme (input.mode): moving the mouse makes the ship follow it,
 * a finger shows the touch controls. This picks how the ship is steered, so it
 * is the game's own; the prompts ask the engine (cartwheel/prompt.h). */
enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
static int last_device = DEV_KEYS;
/* P1's device for the prompts, from the engine (asked once a frame in update) */
static cw_prompt_device p1_device = CW_PROMPT_KEYBOARD;

/* ── touch controls (logical window coordinates) ───────────────────── */

static int T_STICK = -1, T_FIRE = -1, T_BOMB = -1, T_PAUSE = -1;
static bool touch_live;
static void touch_rects(float *fx, float *fy, float *fr, float *bx, float *by, float *br) {
  int w, h;
  cw_window_size(&w, &h);
  float W = (float)w, H = (float)h, k = H / 540.0f;
  *fr = 86 * k, *fx = W - 115 * k, *fy = H - 125 * k;
  *br = 52 * k, *bx = W - 120 * k, *by = H - 300 * k;
}
static void touch_layout(void) {
  int w, h;
  cw_window_size(&w, &h);
  float W = (float)w, H = (float)h, k = H / 540.0f, fx, fy, fr, bx, by, br;
  touch_rects(&fx, &fy, &fr, &bx, &by, &br);
  if (T_STICK < 0) {
    T_STICK = cw_touch_add_stick(0, H * 0.15f, W * 0.55f, H * 0.85f, 60 * k, true);
    T_FIRE = cw_touch_add_button(fx - fr, fy - fr, fr * 2, fr * 2);
    T_BOMB = cw_touch_add_button(bx - br, by - br, br * 2, br * 2);
    T_PAUSE = cw_touch_add_button(14 * k, 14 * k, 64 * k, 52 * k);
    char b[48];
    static const char *const DIRS[4] = { "left", "right", "up", "down" };
    cw_action dirs[4] = { G.a_left, G.a_right, G.a_up, G.a_down };
    for (int d = 0; d < 4; d++) {
      snprintf(b, sizeof b, "touch:%d.%s", T_STICK, DIRS[d]);
      cw_action_bind_text(dirs[d], b);
    }
    snprintf(b, sizeof b, "touch:%d", T_FIRE);
    cw_action_bind_text(G.a_fire, b);
    snprintf(b, sizeof b, "touch:%d", T_BOMB);
    cw_action_bind_text(G.a_item, b);
    snprintf(b, sizeof b, "touch:%d", T_PAUSE);
    cw_action_bind_text(G.a_pause, b);
  }
  if (touch_live) {
    cw_touch_set_rect(T_STICK, 0, H * 0.15f, W * 0.55f, H * 0.85f);
    cw_touch_set_rect(T_FIRE, fx - fr, fy - fr, fr * 2, fr * 2);
    cw_touch_set_rect(T_BOMB, bx - br, by - br, br * 2, br * 2);
    cw_touch_set_rect(T_PAUSE, 14 * k, 14 * k, 64 * k, 52 * k);
  } else {
    cw_touch_set_rect(T_STICK, 0, 0, 0, 0);
    cw_touch_set_rect(T_FIRE, 0, 0, 0, 0);
    cw_touch_set_rect(T_BOMB, 0, 0, 0, 0);
    cw_touch_set_rect(T_PAUSE, 0, 0, 0, 0);
  }
}

static uint32_t prev_mouse;
static bool mouse_clicked, mouse_right_clicked;
static void read_devices(void) {
  const cw_raw_input *in = cw_input_raw();
  int before = last_device;
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse & 1);
  mouse_right_clicked = (in->mouse_buttons & 2) && !(prev_mouse & 2);
  static float pmx = -1, pmy = -1;
  if (pmx >= 0 && !in->mouse_from_touch && (fabsf(in->mouse_x - pmx) > 3 || fabsf(in->mouse_y - pmy) > 3)) last_device = DEV_MOUSE;
  pmx = in->mouse_x, pmy = in->mouse_y;
  if (mouse_clicked) last_device = in->mouse_from_touch ? DEV_TOUCH : DEV_MOUSE;
  prev_mouse = in->mouse_buttons;
  dbg_mouse = in->mouse_buttons;
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
  if (!device_used) last_device = cw_platform_touch_primary() ? DEV_TOUCH : DEV_KEYS;
  input_mode = last_device;
}

/* ── audio (the game runs silent if the files are missing) ─────────── */

enum { SFX_SHOT, SFX_LANCE, SFX_HAMMER, SFX_POP, SFX_BOOM, SFX_BIG, SFX_HIT, SFX_SHIELD, SFX_HULL, SFX_PICKUP, SFX_LIFE, SFX_LOSE, SFX_BOMB, SFX_SELECT, SFX_ARMED, SFX_ALARM, SFX_COUNT };
static const char *const SFX_FILES[SFX_COUNT] = { "audio/shot.wav", "audio/lance.wav", "audio/hammer.wav", "audio/pop.wav", "audio/boom.wav", "audio/big.wav",
                                                  "audio/hit.wav", "audio/shield.wav", "audio/hull.wav", "audio/pickup.wav", "audio/life.wav", "audio/lose.wav",
                                                  "audio/bomb.wav", "audio/select.wav", "audio/armed.wav", "audio/alarm.wav" };
static struct {
  bool on;
  cw_bus sfx_bus, music_bus;
  cw_sound sfx[SFX_COUNT];
  cw_voice music;
  int music_which;
  int cooldown[SFX_COUNT];
} A;

static void sfx_at(int id, float vol, float x) {
  if (!A.on || !A.sfx[id].id || A.cooldown[id] > 0) return;
  A.cooldown[id] = id == SFX_SHOT ? 4 : 3;
  cw_audio_play(A.sfx[id], &(cw_play_desc){ .bus = A.sfx_bus, .volume = vol, .pan = cw_clampf(x / FIELD_X, -1, 1) * 0.6f });
}
static void sfx(int id, float vol) { sfx_at(id, vol, 0); }
static void audio_init(void) {
  A.on = getenv("CHROMITE_NO_AUDIO") == NULL && cw_vfs_exists("audio/music_game.ogg") && cw_audio_init(&(cw_audio_desc){ 0 });
  if (!A.on) return;
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.45f);
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1, 0.1f } });
  for (int i = 0; i < SFX_COUNT; i++)
    if (cw_vfs_exists(SFX_FILES[i]))
      A.sfx[i] = cw_res_audio_sound(cw_res_load(SFX_FILES[i], 0), &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = 4, .bus = A.sfx_bus });
  A.music_which = -1;
}
static void music(int which) {
  if (!A.on || A.music_which == which) return;
  if (A.music_which >= 0) cw_voice_stop(A.music, 1.0f);
  A.music_which = which;
  if (which < 0) return;
  const char *path = which == 0 ? "audio/music_title.ogg" : "audio/music_game.ogg";
  cw_sound m = cw_res_audio_sound(cw_res_load(path, 0), &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.music_bus });
  A.music = cw_audio_play(m, &(cw_play_desc){ .loop = true, .fade_in = 1.5f, .bus = A.music_bus });
}

/* the pad answers what the ship feels: hits, the bomb, a command craft falling (no screen shake) */
static void rumble(float low, float high, int ms) {
  cw_input_rumble(0, low, high, ms);
  rumbles++;
}
static void feel_events(const sim *S) {
  for (int i = 0; i < S->ev_n; i++) {
    const sim_event *e = &S->ev[i];
    switch (e->type) {
    case EV_HERO_HIT:
      if (e->a < 0) rumble(0.25f, 0.2f, 60); /* rubbing a craft */
      else if (S->h.shields > 0) rumble(0.15f, 0.4f, 90);
      else rumble(0.5f, 0.6f, 150);
      break;
    case EV_HERO_DIE: rumble(0.9f, 0.7f, 500); break;
    case EV_BOMB: rumble(0.7f, 0.3f, 420); break;
    case EV_BOSS_DIE: rumble(1, 0.6f, 700), hitstop = HITSTOP_BOSS; break;
    case EV_PLATE: rumble(0.45f, 0.35f, 200); break;
    case EV_BOSS_PHASE: rumble(0.8f, 0.5f, 420); break;
    case EV_ESCAPE:
    case EV_LOSE: rumble(0.35f, 0.15f, 180); break;
    case EV_KILL:
      if (e->a == EK_TANK || e->a == EK_RAYGUN) rumble(0.3f, 0.25f, 110);
      break;
    default: break;
    }
  }
}

static void play_events(const sim *S) {
  feel_events(S);
  for (int i = 0; i < S->ev_n; i++) {
    const sim_event *e = &S->ev[i];
    switch (e->type) {
    case EV_KILL: sfx_at(e->a == EK_GNAT || e->a == EK_OMNI ? SFX_POP : e->a == EK_STRAIGHT ? SFX_BOOM : SFX_BIG, e->a >= EK_RAYGUN ? 1 : 0.7f, e->x); break;
    case EV_BOSS_DIE: sfx(SFX_BIG, 1); break;
    case EV_HIT: sfx_at(SFX_HIT, 0.25f, e->x); break;
    case EV_HERO_HIT: sfx_at(S->h.shields > 0 ? SFX_SHIELD : SFX_HULL, 0.6f, e->x); break;
    case EV_HERO_DIE: sfx(SFX_BIG, 1); break;
    case EV_PICKUP: sfx_at(SFX_PICKUP, 0.8f, e->x); break;
    case EV_SHOT: sfx_at(e->a == GUN_DEFAULT ? SFX_SHOT : e->a == GUN_GREEN ? SFX_LANCE : SFX_HAMMER, e->a == GUN_DEFAULT ? 0.18f : 0.35f, e->x); break;
    case EV_LIFE: sfx(SFX_LIFE, 0.9f); break;
    case EV_LOSE:
    case EV_ESCAPE: sfx(SFX_LOSE, 0.9f); break;
    case EV_CANCEL: sfx(SFX_PICKUP, 0.7f); break;
    case EV_PLATE: sfx_at(SFX_BIG, 0.8f, e->x); break;
    case EV_BOSS_TELL: sfx_at(SFX_ARMED, 0.6f, e->x); break;
    case EV_BOMB: sfx(SFX_BOMB, 1); break;
    case EV_ARMED: sfx(SFX_ARMED, 0.8f); break;
    default: break;
    }
  }
}

/* ── the game ───────────────────────────────────────────────────────── */

static void load_world(int variation) {
  if (cw_node_valid(G.scene)) {
    fx_free();
    world_free();
    cw_node_free(G.scene);
  }
  /* the graphics preset sets the maps' resolution: remake them (and what uses them) when it changed */
  if (tex_stale()) models_free(), tex_free_all();
  G.scene = cw_scene_instance_path("scenes/chromite.cwscene");
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), G.scene);
  G.camera = cw_node_get(G.scene, "Camera");
  G.sun = cw_node_get(G.scene, "Sun");
  /* the key light from up the screen and to the left, down across the deck */
  cw_node3d_look_at(G.sun, (t3_vec3){ 0, -10, 0 }, (t3_vec3){ 0, 1, 0 });
  world_build(G.scene, variation);
  fx_init(G.scene);
  /* each stage world its own grade and air: the fortress steel-blue in its haze, the orbital ring cold and clear
   * over the planet (thin haze far down), the foundry warm with smoke over the melt */
  static const char *const GRADE[3] = { "\"grade_steel.cube\"", "\"grade_steel.cube\"", "\"grade_rust.cube\"" };
  static const float HAZE[3][5] = { { 0.055f, -14, 0.03f, 0.036f, 0.046f }, { 0.012f, -60, 0.02f, 0.035f, 0.07f }, { 0.07f, -14, 0.06f, 0.03f, 0.02f } };
  cw_node post = cw_node_get(G.scene, "Post"), haze = cw_node_get(G.scene, "Haze");
  cw_value v;
  if (cw_node_valid(post) && cw_value_parse(GRADE[variation % 3], CW_T_STRING, &v)) cw_node_set(post, "lut", v);
  if (cw_node_valid(haze)) {
    const float *h = HAZE[variation % 3];
    cw_node_set(haze, "density", cw_val_float(h[0]));
    cw_node_set(haze, "base_height", cw_val_float(h[1]));
    cw_node_set(haze, "color", cw_val_color(h[2], h[3], h[4], 1));
  }
  G.variation = variation;
}

static void new_game(int level) {
  sim_new_game(&G.S, level, SKILL_BASE[SAVE.skill], 0xc4801eu + (uint32_t)level * 7919u, SAVE.rules == RULES_CLASSIC ? RULES_CLASSIC : RULES_MODERN);
  if ((level - 1) % 3 != G.variation) load_world((level - 1) % 3);
  G.prev = G.S;
  G.state = GS_PLAY;
  G.state_ticks = 0;
  G.level_shown = 0;
  G.pb_ticks = G.esc_ticks = 0, G.esc_cost = G.esc_last = 0, hitstop = 0;
  music(1);
}

/* ── autopilot (chromite_auto): flies itself from the sim's own state ── */

#ifdef CHROMITE_AUTOPILOT
static sim_input autopilot(const sim *S) {
  sim_input in = { 0 };
  const hero *h = &S->h;
  in.fire = true;
  /* line up under the lowest enemy, keep low, and shy away from fire */
  float tx = 0, best = 99, ty = -5.5f;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const enemy *e = &S->en[i];
    if (!e->live || e->y > FIELD_Y) continue;
    float k = e->y + (e->kind == EK_GNAT ? 6 : 0);
    if (k < best) best = k, tx = e->x;
  }
  /* go for a power-up within reach */
  for (int i = 0; i < MAX_POWERUPS; i++) {
    const powerup *p = &S->pu[i];
    if (p->live && p->y < 2 && p->y > -7) tx = p->x, ty = p->y;
  }
  float ax = 0, ay = 0;
  for (int i = 0; i < MAX_EBULLETS; i++) {
    const ebullet *b = &S->eb[i];
    if (!b->live) continue;
    float dx = h->x - b->x, dy = h->y - b->y;
    if (dy < -1 || dy > 5 || fabsf(dx) > 2.2f) continue;
    float w = (2.2f - fabsf(dx)) * (5 - dy) * 0.15f;
    ax += (dx >= 0 ? 1 : -1) * w;
  }
  for (int i = 0; i < MAX_ENEMIES; i++) {
    const enemy *e = &S->en[i];
    if (!e->live) continue;
    float dx = h->x - e->x, dy = h->y - e->y;
    if (fabsf(dx) < enemy_half_w(e->kind) + 1.2f && fabsf(dy) < enemy_half_h(e->kind) + 1.5f) ax += (dx >= 0 ? 1 : -1) * 2, ay -= 1;
  }
  in.x = cw_clampf((tx - h->x) * 0.6f + ax, -1, 1);
  in.y = cw_clampf((ty - h->y) * 0.5f + ay, -1, 1);
  return in;
}
#endif

/* ── the app ────────────────────────────────────────────────────────── */

static cw_font font_ui, font_title;
static void sync_hook(cw_tree *t, double alpha, void *u);

static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc > 2 && !strcmp(argv[1], "no_chain")) sim_sw_no_chain = atoi(argv[2]);
  if (argc > 2 && !strcmp(argv[1], "double_over")) sw_double_over = atoi(argv[2]);
  if (argc > 2 && !strcmp(argv[1], "rules")) SAVE.rules = atoi(argv[2]) ? RULES_CLASSIC : RULES_MODERN;
  cw_reply_printf(r, "{\"no_chain\":%d,\"double_over\":%d,\"rules\":%d}", sim_sw_no_chain, sw_double_over, SAVE.rules);
}
static void cmd_boss(int argc, const char **argv, cw_reply *r) {
  /* boss: the level jumps to its command craft (cheat.boss); boss low: the craft that is up has one hit left in it */
  if (argc > 1 && !strcmp(argv[1], "low")) {
    if (G.S.boss >= 0 && G.S.en[G.S.boss].live) G.S.en[G.S.boss].hp = -1;
  } else if (argc > 1 && !strcmp(argv[1], "half")) {
    if (G.S.boss >= 0 && G.S.en[G.S.boss].live) G.S.en[G.S.boss].hp = -0.45f * G.S.en[G.S.boss].max_hp;
  } else
    cheat_boss = 1;
  cw_reply_printf(r, "{\"boss\":%d}", G.S.boss);
}
static bool die_pending;
static void cmd_die(int argc, const char **argv, cw_reply *r) {
  (void)argc, (void)argv;
  die_pending = true;
  cw_reply_raw(r, "{\"ok\":true}");
}

static void init(void *u) {
  (void)u;
  save_check();
  cw_quality_restore(SAVE.quality);
  cw_text_scale_restore(SAVE.text_size); /* the player's text size and text background */
  cw_text_backdrop_set(SAVE.text_bg <= CW_TEXT_BACKDROP_PANEL ? (cw_text_backdrop)SAVE.text_bg : CW_TEXT_BACKDROP_OFF);
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("chromite: input.cfg had errors");
  cw_input_commit_defaults();
  G.a_left = cw_action_find("left");
  G.a_right = cw_action_find("right");
  G.a_up = cw_action_find("up");
  G.a_down = cw_action_find("down");
  G.a_fire = cw_action_find("fire");
  G.a_item = cw_action_find("item");
  G.a_start = cw_action_find("start");
  G.a_back = cw_action_find("back");
  G.a_pause = cw_action_find("pause");
  touch_layout();
  audio_init();
  font_ui = cw_font_load("fonts/Oswald-VF.ttf", NULL);
  font_title = cw_font_load("fonts/Hack-Bold.ttf", NULL);
  /* Prepare printable English UI glyphs while loading. SDF glyph generation
   * must not interrupt gameplay when a new score or result first appears. */
  const cw_font ui_fonts[] = {font_ui, font_title};
  for (unsigned f = 0; f < sizeof ui_fonts / sizeof ui_fonts[0]; f++)
    cw_font_preload_range(ui_fonts[f], 32, 126);

  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("input.mode", &input_mode);
  cw_debug_i32("level", &G.level);
  cw_debug_i32("score", &G.score32);
  cw_debug_i32("lives", &G.lives);
  cw_debug_i32("kills", &G.kills);
  cw_debug_i32("losses", &G.losses);
  cw_debug_i32("enemies", &G.live_enemies);
  cw_debug_i32("ebullets", &G.live_ebullets);
  cw_debug_f32("hero.shields", &G.shields);
  cw_debug_f32("hero.damage", &G.damage);
  cw_debug_f32_array("hero.pos", G.hero_pos, 2);
  cw_debug_i32("over.rank", &G.rank);
  cw_debug_i32("cheat.level", &cheat_level);
  cw_debug_i32("menu.cursor", &G.cursor);
  cw_debug_u32("input.mouse", &dbg_mouse);
  cw_debug_i32("save.skill", &dbg_skill);
  cw_debug_i32("save.hi0", &dbg_hi0);
  cw_debug_i32("hero.armed", &dbg_armed);
  cw_debug_i32("cheat.boss", &cheat_boss);
  cw_debug_i32("cheat.gallery", &cheat_gallery);
  cw_debug_i32("cheat.blasts", &cheat_blasts);
  cw_debug_i32("over.score", &G.final_score);
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  /* (render-only: three.c's geometry generators use the host's sin / cos, so
   * these differ by ulps between native and carts; the gates leave them out) */
  cw_debug_u32("model.hash", &model_hash);
  cw_debug_u32("world.hash", &world_hash);
  cw_debug_i32("fx.lights", &fx_lights_live);
  cw_debug_i32("rules", &dbg_rules);
  cw_debug_i32("chain", &dbg_chain);
  cw_debug_i32("chain.best", &dbg_chain_best);
  cw_debug_i32("chain.mult10", &dbg_mult10);
  cw_debug_i32("escapes", &dbg_escapes);
  cw_debug_i32("point_blanks", &dbg_point_blanks);
  cw_debug_i32("cancelled", &dbg_cancelled);
  cw_debug_i32("hitstop.ticks", &dbg_hitstop_ticks);
  cw_debug_i32("rumbles", &rumbles);
  cw_debug_i32("continues", &G.continues);
  cw_debug_f32_array("view.enemies", view_enemies, VIEW_N * 2);
  cw_debug_f32_array("view.rounds", view_rounds, VIEW_N * 2);
  cw_debug_i32("boss.plates", &dbg_boss_plates);
  cw_debug_i32("boss.tells", &dbg_boss_tells);
  cw_debug_i32("over.headings", &over_headings);
  cw_debug_command("sw", "sw no_chain <0|1>: 1 = kills score flat (control); sw double_over <0|1>: 1 = the dying HUD draws its GAME OVER under the results card too (control); sw rules <0|1>: 1 = classic rules from the next game", cmd_sw);
  cw_debug_command("boss", "boss: jump to the level's command craft; boss low: the craft that is up has one hit left; boss half: it is down to 45%", cmd_boss);
  cw_debug_command("die", "die: the ship loses its last life now (game over tests)", cmd_die);
  cw_tree_add_hook(cw_scene_app_tree(), 2, 0, sync_hook, NULL);
  cw_debug_i32("probe.refreshes", &probe_refreshes);
  cw_debug_i32("probe.every", &probe_every);
  G.variation = -1;
  load_world(0);
  sim_new_game(&G.S, 1, SKILL_BASE[SAVE.skill], 0xc4801eu, RULES_MODERN);
  G.S.mode = PLAY_LEVEL_OVER; /* the title: the deck scrolls, nothing spawns */
  G.S.success = 1e9f;
  G.S.h.y = -7.2f; /* the ship idles low on the title, under the menu */
  G.prev = G.S;
  G.state = GS_TITLE;
  music(0);
#ifdef CHROMITE_AUTOPILOT
  new_game(1);
#endif
  const char *sl = getenv("CHROMITE_START_LEVEL");
  if (sl && atoi(sl) >= 1) new_game(atoi(sl));
#ifdef CHROMITE_AUTOPILOT
  if (getenv("CHROMITE_BOSS")) cheat_boss = 1; /* art aid: straight to the level's command ship */
#endif
}

static bool pressed(cw_action a) { return cw_action_just_pressed(0, a); }

/* the mouse flies the ship, as in the original: the cursor's point on the
 * play plane (a ray from the camera, which world_camera aims from where the
 * ship is) is where the ship heads, at its own top speed; the left button
 * fires. Only the input it makes reaches the simulation. */
static bool mouse_target(float *tx, float *ty) {
  const cw_raw_input *in = cw_input_raw();
  int w, h;
  cw_window_size(&w, &h);
  if (w <= 0 || h <= 0) return false;
  float cx = G.S.h.x * CAM_FOLLOW;
  t3_vec3 eye = { cx, CAM_H, CAM_Z }, at = { cx * 0.5f, 0, CAM_AT_Z };
  t3_vec3 f = { at.x - eye.x, at.y - eye.y, at.z - eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  f.x /= fl, f.y /= fl, f.z /= fl;
  t3_vec3 r = { -f.z, 0, f.x }; /* f x (0, 1, 0) */
  float rl = sqrtf(r.x * r.x + r.z * r.z);
  r.x /= rl, r.z /= rl;
  t3_vec3 u = { r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };
  float th = tanf(CAM_FOV * 0.5f * 3.1415927f / 180), aspect = (float)w / (float)h;
  float nx = 2 * in->mouse_x / w - 1, ny = 1 - 2 * in->mouse_y / h;
  t3_vec3 d = { f.x + r.x * nx * th * aspect + u.x * ny * th, f.y + r.y * nx * th * aspect + u.y * ny * th,
                f.z + r.z * nx * th * aspect + u.z * ny * th };
  if (d.y > -1e-4f) return false;
  float t = (PLAY_Y - eye.y) / d.y;
  *tx = eye.x + d.x * t, *ty = -(eye.z + d.z * t);
  return true;
}

/* the results card offers a continue: modern rules, and the run ended in a death (not ended from the pause menu) */
static bool can_continue_now(void) { return G.S.rules == RULES_MODERN && G.S.mode == PLAY_DEAD; }

static void record_score(void) {
  int s = SAVE.skill;
  G.final_score = (int32_t)(G.S.score > 2000000000 ? 2000000000 : G.S.score);
  G.final_level = G.S.level;
  G.rank = 0;
  for (int i = 0; i < 5; i++)
    if (G.final_score > SAVE.hi[s][i]) {
      for (int j = 4; j > i; j--) SAVE.hi[s][j] = SAVE.hi[s][j - 1], SAVE.hi_level[s][j] = SAVE.hi_level[s][j - 1];
      SAVE.hi[s][i] = G.final_score;
      SAVE.hi_level[s][i] = G.final_level;
      G.rank = i + 1;
      break;
    }
  save_write();
}

/* the title menu's layout, from the size its words draw at (drawn below; the pointer here shares it) */
enum { MENU_N = 7, M_NEW = 0, M_SKILL, M_LEVEL, M_RULES, M_GFX, M_TEXT, M_TEXTBG };
typedef struct title_geo {
  float x0, y0, pitch, bar_h, lh, panel_w; /* the menu: rows from y0, a bar per row */
  int first, n;                            /* the rows shown (a long menu scrolls with the choice) */
  float hs_y, hs_head, hs_pitch;           /* the high scores, bottom left */
  float ticker_y, prompt_y;
} title_geo;
static title_geo title_layout(float W, float H, float s);
static title_geo title_drawn; /* the last drawn layout: the pointer reads it (prompts are measured only while drawing) */
static void menu_step(int dx) {
  int max_level = SAVE.best_level < MAX_START_LEVEL ? SAVE.best_level : MAX_START_LEVEL;
  if (G.cursor == M_SKILL) SAVE.skill = (uint8_t)((SAVE.skill + dx + SKILLS) % SKILLS);
  if (G.cursor == M_LEVEL) SAVE.start_level = (uint8_t)(1 + (SAVE.start_level - 1 + dx + max_level) % max_level);
  if (G.cursor == M_RULES) SAVE.rules = SAVE.rules == RULES_CLASSIC ? RULES_MODERN : RULES_CLASSIC;
  if (G.cursor == M_GFX) {
    cw_quality_cycle(), SAVE.quality = cw_quality_saved();
    if (tex_stale()) load_world(G.variation);
  }
  if (G.cursor == M_TEXT) dx < 0 ? cw_text_scale_cycle_back() : cw_text_scale_cycle();
  if (G.cursor == M_TEXTBG) {
    cw_text_backdrop_cycle();
    if (dx < 0) cw_text_backdrop_cycle(); /* three settings: twice forward is one back */
  }
  if (G.cursor) sfx(SFX_SELECT, 0.5f), save_write();
}

static void title_input(void) {
  /* the menu: start, skill, start level, rules, graphics, text size, text background */
  int dy = (pressed(G.a_down) ? 1 : 0) - (pressed(G.a_up) ? 1 : 0);
  int dx = (pressed(G.a_right) ? 1 : 0) - (pressed(G.a_left) ? 1 : 0);
  if (dy) G.cursor = (G.cursor + dy + MENU_N) % MENU_N, sfx(SFX_SELECT, 0.5f);
  if (dx) menu_step(dx);
  /* the pointer: hovering a line chooses it, a click on it acts (on skill
   * and level, a click steps the value) */
  bool click = false;
  if (input_mode == DEV_MOUSE || input_mode == DEV_TOUCH) {
    cw_vec2 cs = cw_r2d_canvas_size(), m = cw_r2d_window_to_canvas(cw_v2(cw_input_raw()->mouse_x, cw_input_raw()->mouse_y));
    title_geo g = title_drawn;
    float sc = cs.y / 540.0f;
    for (int k = 0; k < g.n; k++) {
      int i = g.first + k;
      float y = g.y0 - 3 * sc + k * g.pitch;
      if (m.x > g.x0 - 30 * sc && m.x < g.x0 - 30 * sc + g.panel_w && m.y > y && m.y < y + g.bar_h) {
        if (G.cursor != i && input_mode == DEV_MOUSE) G.cursor = i, sfx(SFX_SELECT, 0.3f);
        if (mouse_clicked) G.cursor = i, click = true;
      }
    }
    if (click && (G.cursor == M_SKILL || G.cursor == M_LEVEL || G.cursor == M_RULES)) {
      menu_step(1);
      click = false;
    }
  }
  bool go = pressed(G.a_start) || pressed(G.a_fire) || click || (mouse_clicked && input_mode == DEV_TOUCH && G.cursor == 0);
  if (G.state_ticks > 20 && go) {
    if (G.cursor >= M_GFX) {
      menu_step(1);
    } else if (G.cursor == M_SKILL || G.cursor == M_LEVEL || G.cursor == M_RULES) {
      G.cursor = 0;
    } else {
      sfx(SFX_PICKUP, 0.8f);
      new_game(SAVE.start_level);
    }
  }
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
  if (cheat_level > 0) new_game(cheat_level), cheat_level = 0;
  if (cheat_boss && G.state == GS_PLAY) {
    for (int i = G.S.script_n - 1; i >= 0; i--)
      if (G.S.script[i].kind == EK_BOSS0 || G.S.script[i].kind == EK_BOSS1) {
        G.S.frame = (float)(G.S.script[i].at - 60);
        while (G.S.script_i < G.S.script_n && G.S.script[G.S.script_i].at < G.S.script[i].at - 60) G.S.script_i++;
        break;
      }
    cheat_boss = 0;
  }
  if (cheat_blasts > 0) fx_cheat_blasts(cheat_blasts), cheat_blasts = 0;
  switch (G.state) {
  case GS_TITLE: {
    G.prev = G.S;
    sim_input in = { 0 };
    sim_step(&G.S, &in);
    title_input();
    break;
  }
  case GS_PLAY: {
    G.prev = G.S;
    if (die_pending && G.S.mode == PLAY_ON) sim_last_ship_down(&G.S), die_pending = false;
    if (hitstop > 0) { /* the fight holds still a few ticks as a command craft goes (the effects keep playing) */
      hitstop--;
      dbg_hitstop_ticks++;
      break;
    }
    sim_input in = { 0 };
    in.x = cw_action_strength(0, G.a_right) - cw_action_strength(0, G.a_left);
    in.y = cw_action_strength(0, G.a_up) - cw_action_strength(0, G.a_down);
    float l = sqrtf(in.x * in.x + in.y * in.y);
    if (l > 1) in.x /= l, in.y /= l;
    in.fire = cw_action_pressed(0, G.a_fire);
    in.item = pressed(G.a_item);
    float mx, my;
    if (input_mode == DEV_MOUSE && mouse_target(&mx, &my)) {
      const float step = 8 * 0.03f * K; /* the ship's top speed per tick */
      in.x = cw_clampf((mx - G.S.h.x) / step, -1, 1);
      in.y = cw_clampf((my - G.S.h.y) / step, -1, 1);
      in.fire |= (cw_input_raw()->mouse_buttons & 1) != 0;
      in.item |= mouse_right_clicked;
    }
#ifdef CHROMITE_AUTOPILOT
    in = autopilot(&G.S);
#endif
    sim_step(&G.S, &in);
    play_events(&G.S);
    fx_events(&G.S);
    world_chips_spawn(&G.S);
    for (int i = 0; i < G.S.ev_n; i++) dbg_boss_tells += G.S.ev[i].type == EV_BOSS_TELL;
    for (int i = 0; i < G.S.ev_n; i++) {
      if (G.S.ev[i].type == EV_POINT_BLANK) G.pb_ticks = 70;
      if (G.S.ev[i].type == EV_ESCAPE) G.esc_ticks = 110, G.esc_cost = G.S.escape_cost - G.esc_last;
    }
    G.esc_last = G.S.escape_cost;
    if (G.pb_ticks > 0) G.pb_ticks--;
    if (G.esc_ticks > 0) G.esc_ticks--;
    if (G.S.mode == PLAY_LEVEL_OVER && G.S.success <= 0) {
      G.state = GS_CLEAR;
      G.state_ticks = 0;
    } else if (G.S.mode == PLAY_DEAD && G.S.hero_death <= -100) {
      record_score();
      G.state = GS_OVER;
      G.state_ticks = 0;
      music(0);
    } else if (pressed(G.a_pause)) {
      G.state = GS_PAUSE;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_PAUSE:
    G.prev = G.S;
    if (G.state_ticks > 5 && (pressed(G.a_pause) || pressed(G.a_fire) || mouse_clicked)) G.state = GS_PLAY, G.state_ticks = 0;
    else if (G.state_ticks > 5 && pressed(G.a_back)) {
      record_score();
      G.state = GS_OVER, G.state_ticks = 0;
      music(0);
    }
    break;
  case GS_CLEAR:
    G.prev = G.S;
    /* the next level: a new deck every level, the script by level */
    sim_next_level(&G.S);
    if (G.S.level > SAVE.best_level) SAVE.best_level = G.S.level, save_write();
    if ((G.S.level - 1) % 3 != G.variation) load_world((G.S.level - 1) % 3);
    G.prev = G.S;
    G.state = GS_PLAY;
    G.state_ticks = 0;
    break;
  case GS_OVER: {
    G.prev = G.S;
    sim_input in = { 0 };
    sim_step(&G.S, &in);
    fx_events(&G.S);
#ifdef CHROMITE_AUTOPILOT
    if (G.state_ticks > 420) new_game(1);
#endif
    /* modern rules, after a death: {fire} continues from the start of this level (the score starts again; the
     * high score table already has this run), {back} or Start goes to the title. Classic: any press, the title. */
    bool can_continue = can_continue_now();
    if (G.state_ticks > 60 && can_continue && (pressed(G.a_fire) || (mouse_clicked && input_mode == DEV_TOUCH))) {
      G.continues++;
      new_game(G.S.level);
    } else if (G.state_ticks > 60 && (pressed(G.a_start) || (!can_continue && (pressed(G.a_fire) || mouse_clicked)) || (can_continue && (pressed(G.a_back) || mouse_right_clicked)))) {
      G.state = GS_TITLE;
      G.state_ticks = 0;
      G.cursor = 0;
      G.S.mode = PLAY_LEVEL_OVER;
      G.S.success = 1e9f;
      G.S.h.y = -7.2f;
    }
    break;
  }
  }
  fx_hero(&G.S);
  fx_update(1.0f / 60);
  G.state_hash = sim_hash(&G.S);
  G.hero_pos[0] = G.S.h.x, G.hero_pos[1] = G.S.h.y;
  G.score32 = (int32_t)(G.S.score > 2000000000 ? 2000000000 : G.S.score);
  G.lives = G.S.h.lives;
  G.level = G.S.level;
  G.losses = G.S.losses;
  G.kills = G.S.kills;
  G.shields = G.S.h.shields;
  G.damage = G.S.h.damage;
  int ne = 0, nb = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) ne += G.S.en[i].live;
  for (int i = 0; i < MAX_EBULLETS; i++) nb += G.S.eb[i].live;
  G.live_enemies = ne, G.live_ebullets = nb;
  dbg_skill = SAVE.skill, dbg_hi0 = SAVE.hi[SAVE.skill][0], dbg_armed = G.S.h.armed > 0;
  dbg_rules = G.state == GS_TITLE ? SAVE.rules : G.S.rules;
  dbg_chain = G.S.chain, dbg_chain_best = G.S.chain_best, dbg_mult10 = sim_multiplier10(&G.S);
  view_fill();
  dbg_boss_plates = G.S.boss >= 0 && G.S.en[G.S.boss].live ? G.S.en[G.S.boss].plates : -1;
  dbg_escapes = G.S.escapes, dbg_point_blanks = G.S.point_blanks, dbg_cancelled = G.S.cancelled;
}

/* ── per frame: the 3D world follows the sim (interpolated) ─────────── */

static float frame_t;
static void sync_hook(cw_tree *t, double alpha, void *u) {
  (void)t;
  (void)u;
  float a = cw_clampf((float)alpha, 0, 1);
  if (G.state == GS_PAUSE) a = 1;
  frame_t = (G.ticks + a) / 60.0f;
  if (cheat_gallery) {
    /* the lineup: the bosses at the top, the five enemy kinds across the
     * middle, the hero below, as the game camera sees them */
    static sim L;
    L = G.S;
    memset(L.en, 0, sizeof L.en), memset(L.eb, 0, sizeof L.eb);
    memset(L.hb, 0, sizeof L.hb), memset(L.pu, 0, sizeof L.pu);
    static const float AT[][3] = { { EK_BOSS0, -5, 5 }, { EK_BOSS1, 5, 5 }, { EK_STRAIGHT, -8, -0.5f }, { EK_OMNI, -4, -0.5f },
                                   { EK_RAYGUN, 0, -0.5f }, { EK_TANK, 4, -0.5f }, { EK_GNAT, 8, -0.5f } };
    for (int i = 0; i < 7; i++) {
      enemy *e = &L.en[i];
      e->live = true, e->kind = (int)AT[i][0], e->id = (uint32_t)i + 1, e->x = AT[i][1], e->y = AT[i][2];
    }
    L.h.x = 0, L.h.y = -6, L.h.dont_show = 0, L.mode = PLAY_ON;
    world_sync(&L, &L, 1, frame_t, GS_PLAY);
    world_camera(G.camera, &L, frame_t, 1);
    return;
  }
  world_sync(&G.prev, &G.S, a, frame_t, G.state);
  world_camera(G.camera, &G.S, frame_t, G.state == GS_TITLE ? 0 : 1);
}

static void update(void *u, double dt) {
  p1_device = cw_prompt_device_of(0);
  (void)u;
  (void)dt;
  cw_quality_auto_measure(G.state == GS_PLAY);
  touch_live = G.state == GS_PLAY;
  touch_layout();
  if (A.on) cw_audio_update();
}

/* ── HUD: after the original's, a thin instrument layer ───────────────
 * Two tall glass columns (shields left, damage right) with segmented
 * fills and a pulse running up them, stock bars beside the shields, the
 * score in mono digits, spare ships as small silhouettes, wide-spaced
 * lowercase messages. */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

/* spaced text: mono (Hack) for readouts, Oswald for words */
static void text(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap, float spacing) {
  cw_text_style st = { .size = size, .color = c, .align = al, .wrap_width = wrap, .letter_spacing = spacing,
                       .shadow_color = rgba(0, 0, 0, 0.8f * c.a), .shadow_offset = cw_v2(0, size * 0.04f), .shadow_softness = size * 0.12f };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}
/* a heading: wide-spaced capitals in steel with a cold glow behind */
static void heading(const char *s, float x, float y, float size, float alpha, cw_text_align al, float wrap) {
  cw_text_style st = { .size = size, .color = rgba(0.86f, 0.9f, 0.96f, alpha), .align = al, .wrap_width = wrap, .letter_spacing = size * 0.32f,
                       .outline_width = size * 0.03f, .outline_color = rgba(0.02f, 0.03f, 0.06f, alpha),
                       .shadow_color = rgba(0.15f, 0.35f, 0.9f, 0.45f * alpha), .shadow_offset = cw_v2(0, 0), .shadow_spread = size * 0.1f,
                       .shadow_softness = size * 0.45f };
  cw_r2d_text(font_ui, s, cw_v2(x, y), &st);
}
static void spaced(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap) {
  text(font_ui, s, x, y, size, c, al, wrap, size * 0.35f);
}
/* a spaced line's size as drawn (the player's text size included) */
static cw_vec2 spaced_size(const char *s, float size) {
  cw_text_style st = { .size = size, .letter_spacing = size * 0.35f };
  cw_text_metrics m = cw_font_measure(font_ui, s, &st);
  return cw_v2(m.width, m.height);
}
/* a spaced line with button prompts in it ({fire}, {up|down}: cartwheel/prompt.h),
 * centred in [x, x + wrap]: the words as spaced() draws them, each {...} the
 * engine's glyph for that action's binding on P1's device (cw_prompt_actions) */
static float pspaced_run(const char *s, float x, float y, float size, cw_color c, bool draw) {
  cw_text_style st = { .size = size, .letter_spacing = size * 0.35f };
  float lh = cw_font_measure(font_ui, "Ag", &st).height, gap = size * 0.5f, w = 0;
  float sp = cw_font_measure(font_ui, "a a", &st).width - cw_font_measure(font_ui, "aa", &st).width;
  char buf[160];
  for (const char *p = s; *p;) {
    const char *o = strchr(p, '{');
    size_t n = o ? (size_t)(o - p) : strlen(p);
    if (n) {
      snprintf(buf, sizeof buf, "%.*s", (int)n, p);
      if (draw) spaced(buf, x + w, y, size, c, CW_ALIGN_LEFT, 0);
      w += cw_font_measure(font_ui, buf, &st).width;
      for (size_t t = n; t > 0 && buf[t - 1] == ' '; t--) w += sp; /* the measure drops trailing spaces */
    }
    if (!o) break;
    const char *e = strchr(o, '}');
    if (!e) break;
    cw_action acts[4];
    int na = 0;
    for (const char *q = o + 1; q < e && na < 4;) {
      const char *bar = memchr(q, '|', (size_t)(e - q));
      size_t k = bar ? (size_t)(bar - q) : (size_t)(e - q);
      snprintf(buf, sizeof buf, "%.*s", (int)k, q);
      acts[na++] = cw_action_find(buf);
      q += k + (bar ? 1 : 0);
    }
    float gs = size * 1.2f;
    cw_vec2 g = cw_prompt_actions_size(acts, na, 0, gs);
    if (draw) cw_prompt_actions(acts, na, 0, cw_v2(x + w + gap * 0.5f, y + (lh - g.y) * 0.5f), gs, rgba(0.86f, 0.9f, 0.96f, c.a));
    w += g.x + gap;
    p = e + 1;
  }
  return w;
}
static void pspaced(const char *s, float x, float y, float size, cw_color c, float wrap) {
  float w = pspaced_run(s, 0, 0, size, c, false);
  pspaced_run(s, x + (wrap - w) * 0.5f, y, size, c, true);
}
/* a prompt line that breaks at its wide gaps (three spaces or more) into as
 * many centred lines as it needs to fit [x, x + wrap]; returns its height */
static float pspaced_block(const char *s, float x, float y, float size, cw_color c, float wrap, bool draw) {
  char line[192] = "", cand[192];
  float lh = spaced_size("Ag", size).y, h = 0;
  int gap_n = 0; /* the gap before this piece, kept when the line holds both */
  for (const char *p = s; *p;) {
    const char *g = strstr(p, "   ");
    size_t n = g ? (size_t)(g - p) : strlen(p);
    snprintf(cand, sizeof cand, "%s%.*s%.*s", line, line[0] ? gap_n : 0, "                ", (int)n, p);
    if (line[0] && pspaced_run(cand, 0, 0, size, c, false) > wrap) {
      if (draw) pspaced(line, x, y + h, size, c, wrap);
      h += lh * 1.15f;
      snprintf(line, sizeof line, "%.*s", (int)n, p);
    } else
      memcpy(line, cand, sizeof line);
    p += n;
    for (gap_n = 0; *p == ' '; p++) gap_n += gap_n < 16;
  }
  if (line[0]) {
    if (draw) pspaced(line, x, y + h, size, c, wrap);
    h += lh;
  }
  return h;
}
/* mono or heading text's size as drawn */
static cw_vec2 mono_size(const char *s, float size, float spacing) {
  cw_text_style st = { .size = size, .letter_spacing = spacing };
  cw_text_metrics m = cw_font_measure(font_title, s, &st);
  return cw_v2(m.width, m.height);
}
static float heading_h(const char *s, float size, float wrap) {
  cw_text_style st = { .size = size, .letter_spacing = size * 0.32f, .wrap_width = wrap, .outline_width = size * 0.03f };
  return cw_font_measure(font_ui, s, &st).height;
}
static bool prompt_touch(void) { return p1_device == CW_PROMPT_TOUCH; }

/* a vertical gradient quad */
static void vgrad(cw_rect r, cw_color top, cw_color bot) {
  cw_vertex2d v[4] = { { cw_v2(r.x, r.y), cw_v2(0, 0), top }, { cw_v2(r.x + r.w, r.y), cw_v2(1, 0), top },
                       { cw_v2(r.x + r.w, r.y + r.h), cw_v2(1, 1), bot }, { cw_v2(r.x, r.y + r.h), cw_v2(0, 1), bot } };
  static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
}
static void hgrad(cw_rect r, cw_color left, cw_color right) {
  cw_vertex2d v[4] = { { cw_v2(r.x, r.y), cw_v2(0, 0), left }, { cw_v2(r.x + r.w, r.y), cw_v2(1, 0), right },
                       { cw_v2(r.x + r.w, r.y + r.h), cw_v2(1, 1), right }, { cw_v2(r.x, r.y + r.h), cw_v2(0, 1), left } };
  static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
}

#define OVER_Y 0.22f /* the GAME OVER heading's place, a share of the height (the dying HUD and the results card) */
static void draw_hud(float W, float H, float s) {
  const sim *S = &G.S;
  const hero *h = &S->h;
  char b[64];
  /* the gauges and spare ships are 3D (hud3d.c); their labels are here */
  /* the gauge labels on the bottom edge, placed by their measured size so a larger text size keeps them on screen */
  float lab = 14 * s;
  cw_vec2 sh = spaced_size("shields", lab), dm = spaced_size("damage", lab);
  spaced("shields", 4 * s, H - 8 * s - sh.y, lab, rgba(0.55f, 0.68f, 0.95f, 0.85f), CW_ALIGN_LEFT, 0);
  spaced("damage", W - 4 * s - dm.x, H - 8 * s - dm.y, lab, rgba(0.95f, 0.55f, 0.5f, 0.85f), CW_ALIGN_LEFT, 0);
  /* score and level, top left; reserve ships, top right */
  char lv[64];
  snprintf(b, sizeof b, "%08lld", (long long)S->score);
  snprintf(lv, sizeof lv, "level %d   %s", S->level, SKILL_NAME[SAVE.skill]);
  cw_vec2 sc = mono_size(b, 26 * s, 3 * s), ls = spaced_size(lv, 14 * s);
  float ly = 14 * s + fmaxf(32 * s, sc.y + 2 * s); /* the level line under the score, by its measured height */
  hgrad(cw_rect_make(0, 0, fmaxf(330 * s, 28 * s + fmaxf(sc.x, ls.x) + 80 * s), fmaxf(70 * s, ly + ls.y + 10 * s)), rgba(0.01f, 0.015f, 0.025f, 0.6f),
        rgba(0.01f, 0.015f, 0.025f, 0));
  text(font_title, b, 26 * s, 14 * s, 26 * s, rgba(0.8f, 0.86f, 0.95f, 0.95f), CW_ALIGN_LEFT, 0, 3 * s);
  spaced(lv, 28 * s, ly, 14 * s, rgba(0.5f, 0.58f, 0.72f, 0.85f), CW_ALIGN_LEFT, 0);
  /* the chain: its count and multiplier under the level line, a bar that drains until the next kill must come */
  float cy = ly + ls.y + 8 * s;
  if (S->chain >= 2 && G.state != GS_OVER) {
    int m = sim_multiplier10(S);
    snprintf(b, sizeof b, "chain %d   x%d.%d", S->chain, m / 10, m % 10);
    cw_vec2 cs = spaced_size(b, 16 * s);
    float heat = cw_clampf((S->chain - 1) / (float)CHAIN_CAP, 0, 1);
    spaced(b, 28 * s, cy, 16 * s, rgba(1, 0.85f - 0.4f * heat, 0.35f - 0.2f * heat, 0.95f), CW_ALIGN_LEFT, 0);
    float bw = fmaxf(cs.x, 120 * s), fill = cw_clampf((float)S->chain_timer / CHAIN_WINDOW, 0, 1);
    cw_r2d_rect(cw_rect_make(28 * s, cy + cs.y + 3 * s, bw, 4 * s), rgba(0.05f, 0.04f, 0.02f, 0.6f));
    cw_r2d_rect(cw_rect_make(28 * s, cy + cs.y + 3 * s, bw * fill, 4 * s), rgba(1, 0.7f, 0.25f, 0.9f));
    cy += cs.y + 12 * s;
  }
  if (G.pb_ticks > 0 && G.state != GS_OVER) /* a point-blank kill: the word holds, then fades */
    spaced("point blank  x2", 28 * s, cy, 16 * s, rgba(1, 0.5f, 0.85f, cw_clampf(G.pb_ticks / 20.0f, 0, 1)), CW_ALIGN_LEFT, 0);
  if (G.esc_ticks > 0 && G.state != GS_OVER) { /* modern rules: what the last one through cost, over the red bar */
    snprintf(b, sizeof b, "got past   -%lld", (long long)G.esc_cost);
    cw_vec2 es = spaced_size(b, 16 * s);
    spaced(b, 0, H - 14 * s - 2 * es.y, 16 * s, rgba(1, 0.4f, 0.3f, cw_clampf(G.esc_ticks / 20.0f, 0, 1)), CW_ALIGN_CENTER, W);
  }
  /* the boss: a hairline gauge across the top */
  if (S->boss_hp >= 0) {
    float bw = W * 0.36f, bx = (W - bw) * 0.5f, by = 22 * s;
    cw_r2d_rect(cw_rect_make(bx, by, bw, 6 * s), rgba(0.05f, 0.03f, 0.03f, 0.7f));
    cw_r2d_set_blend(CW_BLEND_ADD);
    cw_r2d_rect(cw_rect_make(bx, by, bw * cw_clampf(S->boss_hp, 0, 1), 6 * s), rgba(1, 0.25f, 0.15f, 0.9f));
    cw_r2d_set_blend(CW_BLEND_ALPHA);
    spaced("hostile command craft", 0, by + 10 * s, 14 * s, rgba(0.9f, 0.45f, 0.4f, 0.8f), CW_ALIGN_CENTER, W);
  }
  /* messages, in the original's spaced lowercase */
  if (G.state_ticks < 260 && S->mode == PLAY_ON && G.state != GS_PAUSE) { /* not behind the pause screen */
    float a = cw_clampf(G.state_ticks / 30.0f, 0, 1) * cw_clampf((260 - G.state_ticks) / 50.0f, 0, 1);
    snprintf(b, sizeof b, "LEVEL %d", S->level);
    heading(b, 0, H * 0.3f, 54 * s, a, CW_ALIGN_CENTER, W);
    spaced(S->rules == RULES_CLASSIC ? "do not allow -any- enemies to get past" : "kill fast to chain   do not let them get past", 16 * s, H * 0.3f + fmaxf(74 * s, heading_h(b, 54 * s, 0) + 12 * s), 17 * s, rgba(0.7f, 0.76f, 0.88f, a),
           CW_ALIGN_CENTER, W - 32 * s);
  }
  if (S->mode == PLAY_LEVEL_OVER) heading("LEVEL COMPLETE", 0, H * 0.32f, 46 * s, cw_clampf((500 - S->success) / 30.0f, 0, 1), CW_ALIGN_CENTER, W);
  /* the words come up where the results card keeps them (draw_over), which then draws them alone: one GAME OVER */
  if (S->mode == PLAY_DEAD && (G.state != GS_OVER || sw_double_over)) {
    heading("GAME OVER", 0, H * (sw_double_over ? 0.36f : OVER_Y), 60 * s, cw_clampf((50 - S->hero_death) / 30.0f, 0, 1), CW_ALIGN_CENTER, W);
    over_headings_drawing++;
  }
  if (h->armed > 0)
    pspaced_block(prompt_touch() ? "self-destruct armed :   again to fire" : "self-destruct armed :   {item} again", 16 * s, H * 0.62f, 18 * s,
                  rgba(1, 0.3f, 0.2f, 0.6f + 0.4f * sinf(frame_t * 20)), W - 32 * s, true);
  if (S->past_warn > 0.05f && S->mode == PLAY_ON) {
    cw_r2d_set_blend(CW_BLEND_ADD);
    vgrad(cw_rect_make(0, H - 90 * s, W, 90 * s), rgba(0.6f, 0.02f, 0.02f, 0), rgba(0.6f * S->past_warn, 0.03f, 0.02f, 0.5f * (0.6f + 0.4f * sinf(frame_t * 18))));
    cw_r2d_set_blend(CW_BLEND_ALPHA);
    spaced("enemy getting past", 0, H - 14 * s - spaced_size("enemy getting past", 15 * s).y, 15 * s, rgba(1, 0.3f, 0.25f, S->past_warn), CW_ALIGN_CENTER, W);
  }
}

static void touch_button(int id, const char *label, cw_vec2 wc, float wr, cw_color c) {
  cw_vec2 cc = cw_r2d_window_to_canvas(wc), e = cw_r2d_window_to_canvas(cw_v2(wc.x + wr, wc.y));
  float r = e.x - cc.x;
  bool on = cw_touch_control_active(id);
  cw_r2d_circle(cc, r * (on ? 0.92f : 1), 40, rgba(c.r * 0.3f, c.g * 0.3f, c.b * 0.3f, on ? 0.55f : 0.35f));
  cw_r2d_circle_lines(cc, r, 1.5f, 40, rgba(c.r, c.g, c.b, 0.8f));
  spaced(label, cc.x - r * 2, cc.y - r * 0.2f, r * 0.3f, rgba(0.9f, 0.92f, 1, 0.95f), CW_ALIGN_CENTER, r * 4);
}
static void draw_touch(void) {
  int ww, wh;
  cw_window_size(&ww, &wh);
  float k = (float)wh / 540.0f, fx, fy, fr, bx, by, br;
  touch_rects(&fx, &fy, &fr, &bx, &by, &br);
  bool on = cw_touch_control_active(T_STICK);
  cw_vec2 wc = cw_touch_stick_center(T_STICK); /* the engine keeps the ring at its home */
  cw_vec2 c = cw_r2d_window_to_canvas(wc), e = cw_r2d_window_to_canvas(cw_v2(wc.x + 60 * k, wc.y));
  float R = e.x - c.x;
  cw_r2d_circle_lines(c, R * 1.25f, 1.5f, 48, rgba(0.6f, 0.7f, 0.9f, on ? 0.6f : 0.35f));
  cw_vec2 v = on ? cw_touch_stick_value(T_STICK) : cw_v2(0, 0);
  cw_r2d_circle(cw_v2(c.x + v.x * R, c.y + v.y * R), R * 0.4f, 32, rgba(0.5f, 0.6f, 0.8f, on ? 0.6f : 0.3f));
  touch_button(T_FIRE, "fire", cw_v2(fx, fy), fr, rgba(0.35f, 0.55f, 1, 1));
  touch_button(T_BOMB, "bomb", cw_v2(bx, by), br, rgba(1, 0.3f, 0.2f, 1));
  touch_button(T_PAUSE, "||", cw_v2(14 * k + 32 * k, 14 * k + 26 * k), 26 * k, rgba(0.6f, 0.65f, 0.75f, 1));
}

/* the title: the 3D logo is world.c's; this draws the menu under it as the
 * original did, wide-spaced lowercase with a light bar sweeping the choice */
/* the menu's lines; tight: one space round the colons (a large text size on a narrow screen) */
static bool menu_tight;
static void menu_lines(char lines[MENU_N][64]) {
  char bg[16];
  const char *sep = menu_tight ? " : " : "   :   ";
  snprintf(bg, sizeof bg, "%s", cw_text_backdrop_label());
  snprintf(lines[0], 64, "new game");
  snprintf(lines[1], 64, "skill%s%s", sep, SKILL_NAME[SAVE.skill]);
  snprintf(lines[2], 64, "level%s%d", sep, SAVE.start_level);
  snprintf(lines[M_RULES], 64, "rules%s%s", sep, SAVE.rules == RULES_CLASSIC ? "classic" : "modern");
  snprintf(lines[M_GFX], 64, "graphics%s%s", sep, cw_quality_label());
  snprintf(lines[M_TEXT], 64, "text size%s%s", sep, cw_text_scale_label());
  snprintf(lines[M_TEXTBG], 64, "text background%s%s", sep, bg);
  for (int i = M_GFX; i < MENU_N; i++)
    for (char *c = lines[i]; *c; c++) *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
}
static const char *title_hint(void) { return prompt_touch() ? "tap to launch" : "{up|down} : choose      {fire} : launch"; }
static title_geo title_layout(float W, float H, float s) {
  title_geo g;
  char lines[MENU_N][64];
  g.lh = spaced_size("Ag", 17 * s).y;
  g.pitch = fmaxf(34 * s, g.lh + 10 * s);
  g.bar_h = g.pitch - 6 * s;
  float maxw = 0, hs_right = 40 * s + mono_size("00000000", 15 * s, 2 * s).x;
  for (int tight = 0; tight < 2; tight++) {
    menu_tight = tight;
    menu_lines(lines);
    maxw = 0;
    for (int i = 0; i < MENU_N; i++) maxw = fmaxf(maxw, spaced_size(lines[i], 17 * s).x);
    if (W - 24 * s - maxw >= hs_right + 50 * s) break;
  }
  /* from the bottom up: the hint, the ticker, then the menu and the high scores above them */
  float ph = pspaced_block(title_hint(), 0, 0, 16 * s, rgba(1, 1, 1, 1), W - 32 * s, false);
  g.prompt_y = fminf(H * 0.935f, H - 6 * s - ph);
  g.ticker_y = fminf(H * 0.89f, g.prompt_y - 4 * s - spaced_size("Ag", 15 * s).y);
  float bottom = g.ticker_y - 10 * s;
  g.hs_head = spaced_size("high scores", 14 * s).y;
  g.hs_pitch = fmaxf(19 * s, mono_size("0", 15 * s, 2 * s).y);
  /* the high scores' last row ends a clear gap above the ticker's line, by their measured sizes */
  float hs_block = fmaxf(22 * s, g.hs_head + 4 * s) + 4 * g.hs_pitch + mono_size("0", 15 * s, 2 * s).y;
  g.hs_y = fminf(H * 0.7f, g.ticker_y - 12 * s - hs_block);
  g.x0 = fmaxf(hs_right + 50 * s, fminf(W * 0.36f, W - 24 * s - maxw));
  g.panel_w = fminf(W - (g.x0 - 30 * s) - 8 * s, fmaxf(W * 0.34f, maxw + 60 * s));
  g.y0 = H * 0.55f;
  if (g.y0 - 3 * s + MENU_N * g.pitch > bottom) g.y0 = fmaxf(H * 0.42f, bottom + 3 * s - MENU_N * g.pitch);
  g.n = (int)((bottom + 3 * s - g.y0) / g.pitch);
  g.n = g.n < 1 ? 1 : g.n > MENU_N ? MENU_N : g.n;
  g.first = G.cursor - g.n + 1;
  g.first = g.first < 0 ? 0 : g.first > MENU_N - g.n ? MENU_N - g.n : g.first;
  return g;
}
static void draw_title(float W, float H, float s) {
  char lines[MENU_N][64];
  title_geo g = title_layout(W, H, s); /* picks menu_tight */
  title_drawn = g;
  menu_lines(lines);
  float y0 = g.y0, x0 = g.x0, bx = x0 - 30 * s;
  vgrad(cw_rect_make(bx, y0 - 18 * s, g.panel_w, g.n * g.pitch + 28 * s), rgba(0.0f, 0.0f, 0.01f, 0.55f), rgba(0.0f, 0.0f, 0.01f, 0.35f));
  for (int k = 0; k < g.n; k++) {
    int i = g.first + k;
    bool sel = i == G.cursor;
    float y = y0 - 3 * s + k * g.pitch; /* the row's bar */
    if (sel) {
      /* the original's bar: bright at the left edge, fading right, with a glint sweeping it */
      float bw = g.panel_w - 4 * s;
      cw_r2d_set_blend(CW_BLEND_ADD);
      hgrad(cw_rect_make(bx, y, bw, g.bar_h), rgba(0.25f, 0.35f, 0.95f, 0.75f), rgba(0.05f, 0.08f, 0.3f, 0.0f));
      float gx = bx + fmodf(frame_t * 0.7f, 1.0f) * bw;
      hgrad(cw_rect_make(gx - 40 * s, y, 40 * s, g.bar_h), rgba(0.4f, 0.5f, 1, 0), rgba(0.5f, 0.6f, 1, 0.5f));
      cw_r2d_set_blend(CW_BLEND_ALPHA);
    }
    spaced(lines[i], x0, y + (g.bar_h - g.lh) * 0.5f, 17 * s, sel ? rgba(1, 1, 1, 1) : rgba(0.62f, 0.68f, 0.8f, 0.85f), CW_ALIGN_LEFT, 0);
  }
  /* more rows above or below: a small arrow at the panel's right edge */
  float ax = bx + g.panel_w - 16 * s, t = 6 * s;
  cw_color ac = rgba(0.62f, 0.68f, 0.82f, 0.9f);
  if (g.first > 0) {
    float y = y0 - 10 * s;
    cw_r2d_fan(cw_v2(ax, y), (cw_vec2[]){ cw_v2(ax - t, y + t * 0.5f), cw_v2(ax, y - t * 0.5f), cw_v2(ax + t, y + t * 0.5f) }, 3, true, ac);
  }
  if (g.first + g.n < MENU_N) {
    float y = y0 - 3 * s + g.n * g.pitch + 2 * s;
    cw_r2d_fan(cw_v2(ax, y), (cw_vec2[]){ cw_v2(ax - t, y - t * 0.5f), cw_v2(ax + t, y - t * 0.5f), cw_v2(ax, y + t * 0.5f) }, 3, true, ac);
  }
  /* high scores, bottom left, as the original kept them */
  float tx = 40 * s, ty = g.hs_y;
  spaced("high scores", tx, ty, 14 * s, rgba(0.62f, 0.68f, 0.82f, 0.9f), CW_ALIGN_LEFT, 0);
  float ey = ty + fmaxf(22 * s, g.hs_head + 4 * s);
  for (int i = 0; i < 5; i++) {
    char b[48];
    snprintf(b, sizeof b, "%8d", SAVE.hi[SAVE.skill][i]);
    text(font_title, b, tx, ey + i * g.hs_pitch, 15 * s, i == 0 ? rgba(1, 0.42f, 0.38f, 1) : rgba(0.6f, 0.64f, 0.72f, 0.85f - i * 0.12f), CW_ALIGN_LEFT, 0, 2 * s);
  }
  pspaced_block(title_hint(), 16 * s, g.prompt_y, 16 * s, rgba(0.5f, 0.56f, 0.7f, 0.55f + 0.25f * sinf(frame_t * 2.5f)), W - 32 * s, true);
  /* the original's ticker */
  const char *tick = SAVE.rules == RULES_CLASSIC ? "classic rules :   do not allow -any- enemies to get past"
                                                : "kills in quick succession chain a multiplier   kills up close score double   an enemy that gets past costs points";
  float off = fmodf(frame_t * 90 * s, W + spaced_size(tick, 15 * s).x);
  spaced(tick, W - off, g.ticker_y, 15 * s, rgba(0.35f, 0.4f, 0.85f, 0.85f), CW_ALIGN_LEFT, 0);
}

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.0f, 0.01f, 0.03f, 0.55f));
  heading("PAUSED", 0, H * 0.36f, 52 * s, 1, CW_ALIGN_CENTER, W);
  float y = H * 0.36f + fmaxf(H * 0.14f, heading_h("PAUSED", 52 * s, 0) + 16 * s);
  pspaced_block(prompt_touch() ? "tap to resume" : "{fire} : resume      {back} : end the game", 16 * s, y, 16 * s, rgba(0.7f, 0.76f, 0.88f, 0.9f), W - 32 * s, true);
}

/* the results: each line under the last by its measured height */
static void draw_over(float W, float H, float s) {
  float a = cw_clampf(G.state_ticks / 30.0f, 0, 1);
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0, 0.005f, 0.02f, 0.55f * a));
  /* after a death the HUD already brought the heading up at this place: it stays, the rest fades in under it */
  heading("GAME OVER", 0, H * OVER_Y, 60 * s, G.S.mode == PLAY_DEAD ? 1 : a, CW_ALIGN_CENTER, W);
  over_headings_drawing++;
  float y = H * OVER_Y + fmaxf(H * 0.16f, heading_h("GAME OVER", 60 * s, 0) + 12 * s);
  char b[96];
  snprintf(b, sizeof b, "%08d", G.final_score);
  text(font_title, b, 0, y, 34 * s, rgba(0.85f, 0.9f, 1, a), CW_ALIGN_CENTER, W, 6 * s);
  y += fmaxf(H * 0.08f, mono_size(b, 34 * s, 6 * s).y + 4 * s);
  snprintf(b, sizeof b, "level %d", G.final_level);
  spaced(b, 0, y, 14 * s, rgba(0.6f, 0.66f, 0.8f, a), CW_ALIGN_CENTER, W);
  y += fmaxf(H * 0.08f, spaced_size(b, 14 * s).y + 10 * s);
  const char *verdict = G.rank == 1 ? "new high score !" : G.rank > 1 ? "not bad !" : "fly again";
  spaced(verdict, 0, y, 24 * s, G.rank ? rgba(1, 0.55f, 0.4f, a) : rgba(0.55f, 0.6f, 0.75f, a), CW_ALIGN_CENTER, W);
  y += fmaxf(H * 0.06f, spaced_size(verdict, 24 * s).y + 6 * s);
  if (G.rank > 1) {
    snprintf(b, sizeof b, "rank %d", G.rank);
    spaced(b, 0, y, 16 * s, rgba(1, 0.7f, 0.5f, a), CW_ALIGN_CENTER, W);
    y += spaced_size(b, 16 * s).y + 6 * s;
  }
  const char *p = can_continue_now() ? (prompt_touch() ? "tap to continue from this level" : "{fire} : continue from this level      {back} : title")
                                     : prompt_touch() ? "tap to continue" : "press {fire} to continue";
  float ph = pspaced_block(p, 16 * s, 0, 16 * s, rgba(1, 1, 1, 1), W - 32 * s, false);
  pspaced_block(p, 16 * s, fmaxf(y + 10 * s, fminf(H * 0.82f, H - 8 * s - ph)), 16 * s, rgba(0.5f, 0.56f, 0.72f, a * 0.8f), W - 32 * s, true);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  over_headings_drawing = 0;
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (G.state == GS_TITLE) draw_title(W, H, s);
#ifdef CHROMITE_AUTOPILOT
  else if (getenv("CHROMITE_NO_HUD")) { /* art aid: a clean look at the 3D */
  }
#endif
  else {
    draw_hud(W, H, s);
    if (input_mode == DEV_TOUCH && G.state == GS_PLAY) draw_touch();
    if (G.state == GS_PAUSE) draw_pause(W, H, s);
    if (G.state == GS_OVER) draw_over(W, H, s);
  }
  cw_r2d_end();
  over_headings = over_headings_drawing;
}

static void shutdown(void *u) {
  (void)u;
  fx_free();
  world_free();
  models_free();
  tex_free_all();
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = "Chromite", .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed,
                              .update = update, .render = render, .shutdown = shutdown, .save = &SAVE, .save_size = sizeof SAVE,
                              .touch_pointer = true };
  cw_app_desc d = cw_scene_app(NULL, &base);
  return d;
}
