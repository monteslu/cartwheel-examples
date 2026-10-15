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
 *             the hull takes the rest; do not let any enemy get past (each
 *             one costs a life); an extra ship every 50000
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
#define SAVE_VERSION 1
typedef struct save_data {
  uint32_t magic;
  uint16_t version, size;
  int32_t hi[SKILLS][5];
  int32_t hi_level[SKILLS][5];
  int32_t best_level;
  uint8_t skill, start_level, quality, pad;
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
  SAVE.checksum = save_sum();
  cw_save_commit();
}
static void save_check(void) {
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
} G;

static int32_t input_mode;
static bool device_used;
enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
static int last_device = DEV_KEYS;

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
static bool mouse_clicked;
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

static void play_events(const sim *S) {
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
    case EV_LOSE: sfx(SFX_LOSE, 0.9f); break;
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
  G.scene = cw_scene_instance_path("scenes/chromite.cwscene");
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), G.scene);
  G.camera = cw_node_get(G.scene, "Camera");
  G.sun = cw_node_get(G.scene, "Sun");
  /* the key light from up the screen and to the left, down across the deck */
  cw_node3d_look_at(G.sun, (t3_vec3){ 0, -10, 0 }, (t3_vec3){ 0, 1, 0 });
  world_build(G.scene, variation);
  fx_init(G.scene);
  G.variation = variation;
}

static void new_game(int level) {
  sim_new_game(&G.S, level, SKILL_BASE[SAVE.skill], 0xc4801eu + (uint32_t)level * 7919u);
  if ((level - 1) % 3 != G.variation) load_world((level - 1) % 3);
  G.prev = G.S;
  G.state = GS_PLAY;
  G.state_ticks = 0;
  G.level_shown = 0;
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

static void init(void *u) {
  (void)u;
  save_check();
  cw_quality_restore(SAVE.quality);
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
  font_title = cw_font_load("fonts/RussoOne-Regular.ttf", NULL);
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
  cw_debug_i32("over.score", &G.final_score);
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  cw_debug_u32("model.hash", &model_hash);
  cw_debug_u32("world.hash", &world_hash);
  cw_debug_i32("fx.lights", &fx_lights_live);
  cw_tree_add_hook(cw_scene_app_tree(), 2, 0, sync_hook, NULL);
  G.variation = -1;
  load_world(0);
  sim_new_game(&G.S, 1, SKILL_BASE[SAVE.skill], 0xc4801eu);
  G.S.mode = PLAY_LEVEL_OVER; /* the title: the deck scrolls, nothing spawns */
  G.S.success = 1e9f;
  G.prev = G.S;
  G.state = GS_TITLE;
  music(0);
#ifdef CHROMITE_AUTOPILOT
  new_game(1);
#endif
  const char *sl = getenv("CHROMITE_START_LEVEL");
  if (sl && atoi(sl) >= 1) new_game(atoi(sl));
}

static bool pressed(cw_action a) { return cw_action_just_pressed(0, a); }

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

static void title_input(void) {
  /* the menu: start, skill, start level, graphics */
  int dy = (pressed(G.a_down) ? 1 : 0) - (pressed(G.a_up) ? 1 : 0);
  int dx = (pressed(G.a_right) ? 1 : 0) - (pressed(G.a_left) ? 1 : 0);
  if (dy) G.cursor = (G.cursor + dy + 4) % 4, sfx(SFX_SELECT, 0.5f);
  if (dx) {
    int max_level = SAVE.best_level < MAX_START_LEVEL ? SAVE.best_level : MAX_START_LEVEL;
    if (G.cursor == 1) SAVE.skill = (uint8_t)((SAVE.skill + dx + SKILLS) % SKILLS);
    if (G.cursor == 2) SAVE.start_level = (uint8_t)(1 + (SAVE.start_level - 1 + dx + max_level) % max_level);
    if (G.cursor == 3) cw_quality_cycle(), SAVE.quality = cw_quality_saved();
    if (G.cursor) sfx(SFX_SELECT, 0.5f), save_write();
  }
  bool go = pressed(G.a_start) || pressed(G.a_fire) || (mouse_clicked && input_mode == DEV_TOUCH);
  if (G.state_ticks > 20 && go) {
    if (G.cursor == 3) {
      cw_quality_cycle();
      SAVE.quality = cw_quality_saved();
      save_write();
      sfx(SFX_SELECT, 0.5f);
    } else if (G.cursor == 1 || G.cursor == 2) {
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
    sim_input in = { 0 };
    in.x = cw_action_strength(0, G.a_right) - cw_action_strength(0, G.a_left);
    in.y = cw_action_strength(0, G.a_up) - cw_action_strength(0, G.a_down);
    float l = sqrtf(in.x * in.x + in.y * in.y);
    if (l > 1) in.x /= l, in.y /= l;
    in.fire = cw_action_pressed(0, G.a_fire);
    in.item = pressed(G.a_item);
#ifdef CHROMITE_AUTOPILOT
    in = autopilot(&G.S);
#endif
    sim_step(&G.S, &in);
    play_events(&G.S);
    fx_events(&G.S);
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
    if (G.state_ticks > 60 && (pressed(G.a_start) || pressed(G.a_fire) || mouse_clicked)) {
      G.state = GS_TITLE;
      G.state_ticks = 0;
      G.cursor = 0;
      G.S.mode = PLAY_LEVEL_OVER;
      G.S.success = 1e9f;
    }
    break;
  }
  }
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
}

/* ── per frame: the 3D world follows the sim (interpolated) ─────────── */

static float frame_t;
static void sync_hook(cw_tree *t, double alpha, void *u) {
  (void)t;
  (void)u;
  float a = cw_clampf((float)alpha, 0, 1);
  if (G.state == GS_PAUSE) a = 1;
  frame_t = (G.ticks + a) / 60.0f;
  world_sync(&G.prev, &G.S, a, frame_t, G.state);
  world_camera(G.camera, &G.S, frame_t, G.state == GS_TITLE ? 0 : 1);
}

static void update(void *u, double dt) {
  (void)u;
  (void)dt;
  cw_quality_auto_measure(G.state == GS_PLAY);
  touch_live = G.state == GS_PLAY;
  touch_layout();
  if (A.on) cw_audio_update();
}

/* ── HUD ────────────────────────────────────────────────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

static void text(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap, float spacing) {
  cw_text_style st = { .size = size, .color = c, .align = al, .wrap_width = wrap, .letter_spacing = spacing,
                       .shadow_color = rgba(0, 0, 0, 0.7f * c.a), .shadow_offset = cw_v2(size * 0.05f, size * 0.06f), .shadow_softness = size * 0.08f };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}
/* chrome lettering: a cool top, a hot horizon line, a warm bottom, an
 * outline, as the original's logo text */
static void chrome_text(const char *s, float x, float y, float size, float alpha, cw_text_align al, float wrap) {
  cw_text_style st = { .size = size, .color = rgba(0.75f, 0.85f, 1, alpha), .align = al, .wrap_width = wrap, .letter_spacing = size * 0.06f,
                       .outline_width = size * 0.07f, .outline_color = rgba(0.02f, 0.04f, 0.18f, alpha),
                       .shadow_color = rgba(0.1f, 0.3f, 1, 0.55f * alpha), .shadow_offset = cw_v2(0, 0), .shadow_spread = size * 0.12f,
                       .shadow_softness = size * 0.3f };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}

/* a vertical gradient quad */
static void vgrad(cw_rect r, cw_color top, cw_color bot) {
  cw_vertex2d v[4] = { { cw_v2(r.x, r.y), cw_v2(0, 0), top }, { cw_v2(r.x + r.w, r.y), cw_v2(1, 0), top },
                       { cw_v2(r.x + r.w, r.y + r.h), cw_v2(1, 1), bot }, { cw_v2(r.x, r.y + r.h), cw_v2(0, 1), bot } };
  static const uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
  cw_r2d_triangles((cw_texture){ 0 }, v, 4, idx, 6);
}

/* a glass tube gauge, filled from the bottom, with a flowing light inside */
static void tube(float x, float y, float w, float h, float fill, float over, cw_color c, float flash) {
  cw_r2d_round_rect(cw_rect_make(x - 3, y - 3, w + 6, h + 6), w * 0.5f, 12, rgba(0.02f, 0.03f, 0.06f, 0.75f));
  float fh = h * cw_clampf(fill, 0, 1);
  if (fh > 1) {
    cw_rect r = cw_rect_make(x, y + h - fh, w, fh);
    vgrad(r, rgba(c.r * 1.3f, c.g * 1.3f, c.b * 1.3f, 0.95f), rgba(c.r * 0.45f, c.g * 0.45f, c.b * 0.45f, 0.95f));
    /* bands of light rising through it */
    cw_r2d_set_blend(CW_BLEND_ADD);
    for (int i = 0; i < 5; i++) {
      float p = fmodf(frame_t * 0.35f + i * 0.2f, 1.0f);
      float by = y + h - p * h;
      if (by < y + h - fh) continue;
      cw_r2d_rect(cw_rect_make(x + 2, by, w - 4, 2 + w * 0.08f), rgba(c.r * 0.5f, c.g * 0.5f, c.b * 0.5f, 0.6f));
    }
    if (over > 0) cw_r2d_rect(cw_rect_make(x, y, w, h), rgba(1, 1, 1, 0.12f * over + 0.08f * over * sinf(frame_t * 9)));
    if (flash > 0.02f) cw_r2d_rect(cw_rect_make(x - 3, y - 3, w + 6, h + 6), rgba(c.r, c.g, c.b, 0.6f * flash));
    cw_r2d_set_blend(CW_BLEND_ALPHA);
  }
  /* the glass: a highlight down one side, a rim */
  cw_r2d_rect(cw_rect_make(x + w * 0.18f, y + 4, w * 0.12f, h - 8), rgba(1, 1, 1, 0.18f));
  cw_r2d_round_rect_lines(cw_rect_make(x - 3, y - 3, w + 6, h + 6), w * 0.5f, 2, 12, rgba(0.7f, 0.8f, 0.95f, 0.65f));
}

static void hero_icon(float cx, float cy, float s, cw_color c) {
  cw_vec2 p[7] = { cw_v2(cx, cy - 9 * s), cw_v2(cx + 2 * s, cy - 2 * s), cw_v2(cx + 8 * s, cy + 6 * s), cw_v2(cx + 2 * s, cy + 5 * s),
                   cw_v2(cx - 2 * s, cy + 5 * s), cw_v2(cx - 8 * s, cy + 6 * s), cw_v2(cx - 2 * s, cy - 2 * s) };
  cw_r2d_polygon(p, 7, c);
  cw_r2d_line(cw_v2(cx, cy - 8 * s), cw_v2(cx, cy + 4 * s), 2 * s, rgba(0.25f, 0.4f, 1, c.a));
}

static void draw_hud(float W, float H, float s) {
  const sim *S = &G.S;
  const hero *h = &S->h;
  char b[64];
  /* side gauges: shields on the left (blue), hull on the right (red) */
  float tw = 26 * s, th = 230 * s;
  float sh = h->shields / HERO_SHIELDS, over = sh > 1 ? sh - 1 : 0;
  tube(28 * s, H - th - 50 * s, tw, th, sh > 1 ? 1 : sh, over, rgba(0.2f, 0.45f, 1, 1), h->shield_flash);
  text(font_title, "SHIELDS", 28 * s + tw * 0.5f - 60 * s, H - 44 * s, 15 * s, rgba(0.6f, 0.75f, 1, 0.9f), CW_ALIGN_CENTER, 120 * s, 2 * s);
  float hull = (h->damage + HERO_HULL) / HERO_HULL;
  tube(W - 28 * s - tw, H - th - 50 * s, tw, th, hull, 0, rgba(1, 0.2f, 0.12f, 1), h->damage_flash);
  text(font_title, "DAMAGE", W - 28 * s - tw * 0.5f - 60 * s, H - 44 * s, 15 * s, rgba(1, 0.6f, 0.55f, 0.9f), CW_ALIGN_CENTER, 120 * s, 2 * s);
  /* the stocks: three slim bars beside the shields */
  static const float AC[GUNS][3] = { { 1, 0.85f, 0.25f }, { 0.3f, 1, 0.35f }, { 0.75f, 0.3f, 1 } };
  for (int g = 0; g < GUNS; g++) {
    float x = 66 * s + g * 14 * s, y = H - 50 * s - 120 * s;
    cw_r2d_rect(cw_rect_make(x, y, 8 * s, 120 * s), rgba(0.02f, 0.03f, 0.06f, 0.6f));
    float f = h->ammo[g] / AMMO_MAX;
    if (f > 0) cw_r2d_rect(cw_rect_make(x, y + 120 * s * (1 - f), 8 * s, 120 * s * f), rgba(AC[g][0], AC[g][1], AC[g][2], 0.9f));
  }
  /* the score, bottom left; ships in reserve, top right */
  snprintf(b, sizeof b, "%07lld", (long long)S->score);
  text(font_title, b, 24 * s, 16 * s, 30 * s, rgba(0.85f, 0.92f, 1, 0.95f), CW_ALIGN_LEFT, 0, 3 * s);
  snprintf(b, sizeof b, "LEVEL %d", S->level);
  text(font_title, b, 24 * s, 52 * s, 16 * s, rgba(0.6f, 0.7f, 0.9f, 0.85f), CW_ALIGN_LEFT, 0, 2 * s);
  for (int i = 0; i < h->lives && i < 9; i++) hero_icon(W - 30 * s - (i % 5) * 24 * s, 30 * s + (i / 5) * 26 * s, 1.3f * s, rgba(0.8f, 0.85f, 1, 0.9f));
  /* the boss's health across the top */
  if (S->boss_hp >= 0) {
    float bw = W * 0.4f, bx = (W - bw) * 0.5f, by = 18 * s;
    cw_r2d_round_rect(cw_rect_make(bx - 3, by - 3, bw + 6, 14 * s + 6), 6 * s, 8, rgba(0.02f, 0.02f, 0.05f, 0.7f));
    vgrad(cw_rect_make(bx, by, bw * cw_clampf(1 - S->boss_hp, 0, 1), 14 * s), rgba(1, 0.4f, 0.3f, 0.95f), rgba(0.6f, 0.05f, 0.05f, 0.95f));
    cw_r2d_round_rect_lines(cw_rect_make(bx - 3, by - 3, bw + 6, 14 * s + 6), 6 * s, 2, 8, rgba(0.9f, 0.6f, 0.55f, 0.7f));
  }
  /* messages */
  if (G.state_ticks < 240 && S->mode == PLAY_ON) {
    float a = cw_clampf(G.state_ticks / 20.0f, 0, 1) * cw_clampf((240 - G.state_ticks) / 40.0f, 0, 1);
    snprintf(b, sizeof b, "LEVEL %d", S->level);
    chrome_text(b, 0, H * 0.3f, 64 * s, a, CW_ALIGN_CENTER, W);
    text(font_ui, "do not allow -any- enemies get past", 0, H * 0.3f + 80 * s, 22 * s, rgba(0.8f, 0.85f, 1, a), CW_ALIGN_CENTER, W, 3 * s);
  }
  if (S->mode == PLAY_LEVEL_OVER) {
    float a = cw_clampf((500 - S->success) / 30.0f, 0, 1);
    chrome_text("LEVEL COMPLETE", 0, H * 0.32f, 54 * s, a, CW_ALIGN_CENTER, W);
  }
  if (S->mode == PLAY_DEAD) chrome_text("GAME OVER", 0, H * 0.36f, 72 * s, cw_clampf((50 - S->hero_death) / 30.0f, 0, 1), CW_ALIGN_CENTER, W);
  if (h->armed > 0) text(font_title, input_mode == DEV_PAD ? "SELF-DESTRUCT ARMED: (B) AGAIN" : "SELF-DESTRUCT ARMED: AGAIN TO FIRE", 0, H * 0.62f, 22 * s,
                         rgba(1, 0.35f, 0.25f, 0.6f + 0.4f * sinf(frame_t * 20)), CW_ALIGN_CENTER, W, 2 * s);
  if (S->past_warn > 0.05f && S->mode == PLAY_ON)
    text(font_title, "!  ENEMY GETTING PAST  !", 0, H - 36 * s, 20 * s, rgba(1, 0.25f, 0.2f, S->past_warn * (0.6f + 0.4f * sinf(frame_t * 18))), CW_ALIGN_CENTER, W, 3 * s);
}

static void touch_button(int id, const char *label, cw_vec2 wc, float wr, cw_color c) {
  cw_vec2 cc = cw_r2d_window_to_canvas(wc), e = cw_r2d_window_to_canvas(cw_v2(wc.x + wr, wc.y));
  float r = e.x - cc.x;
  bool on = cw_touch_control_active(id);
  cw_r2d_circle(cc, r * (on ? 0.92f : 1), 32, rgba(c.r, c.g, c.b, on ? 0.6f : 0.35f));
  cw_r2d_circle_lines(cc, r, 3, 32, rgba(0.8f, 0.9f, 1, 0.7f));
  text(font_title, label, cc.x - r * 2, cc.y - r * 0.25f, r * 0.38f, rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER, r * 4, 0);
}
static void draw_touch(void) {
  int ww, wh;
  cw_window_size(&ww, &wh);
  float k = (float)wh / 540.0f, fx, fy, fr, bx, by, br;
  touch_rects(&fx, &fy, &fr, &bx, &by, &br);
  bool on = cw_touch_control_active(T_STICK);
  cw_vec2 wc = on ? cw_touch_stick_center(T_STICK) : cw_v2(150 * k, (float)wh - 150 * k);
  cw_vec2 c = cw_r2d_window_to_canvas(wc), e = cw_r2d_window_to_canvas(cw_v2(wc.x + 60 * k, wc.y));
  float R = e.x - c.x;
  cw_r2d_circle(c, R * 1.25f, 36, rgba(0.3f, 0.45f, 0.8f, on ? 0.3f : 0.18f));
  cw_r2d_circle_lines(c, R * 1.25f, 3, 36, rgba(0.7f, 0.8f, 1, 0.5f));
  cw_vec2 v = on ? cw_touch_stick_value(T_STICK) : cw_v2(0, 0);
  cw_r2d_circle(cw_v2(c.x + v.x * R, c.y + v.y * R), R * 0.45f, 24, rgba(0.75f, 0.85f, 1, on ? 0.8f : 0.5f));
  touch_button(T_FIRE, "FIRE", cw_v2(fx, fy), fr, rgba(0.3f, 0.5f, 1, 1));
  touch_button(T_BOMB, "BOMB", cw_v2(bx, by), br, rgba(1, 0.3f, 0.2f, 1));
  touch_button(T_PAUSE, "II", cw_v2(14 * k + 32 * k, 14 * k + 26 * k), 26 * k, rgba(0.2f, 0.25f, 0.35f, 1));
}

static void draw_title(float W, float H, float s) {
  chrome_text(CHROMITE_TITLE, 0, H * 0.12f, 112 * s, 1, CW_ALIGN_CENTER, W);
  text(font_ui, "a chrome fortress, a cargo ship, and nothing gets past", 0, H * 0.12f + 130 * s, 22 * s, rgba(0.75f, 0.82f, 1, 0.9f), CW_ALIGN_CENTER, W, 2 * s);
  char lines[4][64];
  snprintf(lines[0], 64, "START");
  snprintf(lines[1], 64, "SKILL   %s", SKILL_NAME[SAVE.skill]);
  snprintf(lines[2], 64, "LEVEL   %d", SAVE.start_level);
  snprintf(lines[3], 64, "GRAPHICS   %s", cw_quality_label());
  float y0 = H * 0.5f;
  for (int i = 0; i < 4; i++) {
    bool sel = i == G.cursor;
    float y = y0 + i * 40 * s;
    if (sel) {
      cw_r2d_set_blend(CW_BLEND_ADD);
      vgrad(cw_rect_make(W * 0.3f, y - 4 * s, W * 0.4f, 36 * s), rgba(0.15f, 0.25f, 0.8f, 0.5f), rgba(0.05f, 0.08f, 0.3f, 0.3f));
      cw_r2d_set_blend(CW_BLEND_ALPHA);
    }
    text(font_title, lines[i], 0, y, 24 * s, sel ? rgba(1, 1, 1, 1) : rgba(0.6f, 0.68f, 0.85f, 0.85f), CW_ALIGN_CENTER, W, 4 * s);
  }
  /* the table for this skill */
  float tx = W * 0.8f, ty = H * 0.52f;
  text(font_title, "HIGH SCORES", tx - 100 * s, ty, 16 * s, rgba(0.7f, 0.8f, 1, 0.9f), CW_ALIGN_CENTER, 200 * s, 3 * s);
  for (int i = 0; i < 5; i++) {
    char b[48];
    snprintf(b, sizeof b, "%d", SAVE.hi[SAVE.skill][i]);
    text(font_title, b, tx - 100 * s, ty + (26 + i * 22) * s, 18 * s, i == 0 ? rgba(1, 0.5f, 0.45f, 1) : rgba(0.75f, 0.8f, 0.9f, 0.85f - i * 0.1f), CW_ALIGN_CENTER, 200 * s, 2 * s);
  }
  const char *h = input_mode == DEV_TOUCH ? "tap to launch" : input_mode == DEV_PAD ? "d-pad: choose   (A): launch" : "arrows: choose   Enter / Space: launch";
  text(font_ui, h, 0, H * 0.92f, 20 * s, rgba(0.6f, 0.7f, 0.9f, 0.6f + 0.3f * sinf(frame_t * 3)), CW_ALIGN_CENTER, W, 2 * s);
}

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.0f, 0.02f, 0.06f, 0.45f));
  chrome_text("PAUSED", 0, H * 0.34f, 64 * s, 1, CW_ALIGN_CENTER, W);
  text(font_ui, input_mode == DEV_PAD ? "(A) resume    Back: end the game" : input_mode == DEV_TOUCH ? "tap to resume" : "Space: resume    Backspace: end the game", 0, H * 0.5f,
       22 * s, rgba(0.8f, 0.85f, 1, 0.9f), CW_ALIGN_CENTER, W, 2 * s);
}

static void draw_over(float W, float H, float s) {
  float a = cw_clampf(G.state_ticks / 30.0f, 0, 1);
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0, 0.01f, 0.04f, 0.5f * a));
  chrome_text("GAME OVER", 0, H * 0.2f, 72 * s, a, CW_ALIGN_CENTER, W);
  char b[96];
  snprintf(b, sizeof b, "score %d   level %d", G.final_score, G.final_level);
  text(font_title, b, 0, H * 0.38f, 28 * s, rgba(0.85f, 0.9f, 1, a), CW_ALIGN_CENTER, W, 3 * s);
  const char *verdict = G.rank == 1 ? "new high score!" : G.rank > 1 ? "n o t   b a d !" : "l o s e r";
  text(font_title, verdict, 0, H * 0.48f, 34 * s, G.rank ? rgba(1, 0.6f, 0.4f, a) : rgba(0.6f, 0.65f, 0.8f, a), CW_ALIGN_CENTER, W, 3 * s);
  if (G.rank > 1) {
    snprintf(b, sizeof b, "rank %d", G.rank);
    text(font_title, b, 0, H * 0.56f, 24 * s, rgba(1, 0.75f, 0.5f, a), CW_ALIGN_CENTER, W, 3 * s);
  }
  text(font_ui, input_mode == DEV_TOUCH ? "tap to continue" : "press fire to continue", 0, H * 0.8f, 20 * s, rgba(0.6f, 0.7f, 0.9f, a * 0.8f), CW_ALIGN_CENTER, W, 2 * s);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (G.state == GS_TITLE) draw_title(W, H, s);
  else {
    draw_hud(W, H, s);
    if (input_mode == DEV_TOUCH && G.state == GS_PLAY) draw_touch();
    if (G.state == GS_PAUSE) draw_pause(W, H, s);
    if (G.state == GS_OVER) draw_over(W, H, s);
  }
  cw_r2d_end();
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
