/* bomberdude: an arena bomber on cartwheel and three.c (three.js r186).
 * Four chunky critters, eight themed arenas, a lot of explosions.
 *
 *   flow      title -> Battle: players (critter, human or CPU) -> arena
 *             (locked until Adventure opens it) -> rounds -> podium
 *             title -> Adventure: stage select (each opens when the one
 *             before is beaten) -> rounds against the stage's CPU rivals
 *             (first to two wins) -> result
 *   rules     sim.c (integer, deterministic); ai.c the CPU rivals
 *   input     mouse, gamepad and keyboard all first class. Players 1-4 are
 *             pads 1-4; player 1 also has WASD / Space / E, a second
 *             keyboard player the arrows / Enter / Right Shift; the mouse
 *             walks player 1 to a clicked tile (left button), drops a bomb
 *             (right) and works every menu. Prompts follow the device used
 *             last.
 *   look      per-arena ProceduralSky (PMREM environment), three.c shadows,
 *             HeightFog, GTAO, bloom, SMAA, AgX and a LUT grade; Physical
 *             materials (clearcoat lacquer, sheen fur, iridescent bubbles
 *             and gems); GPU particle blasts; Titan One / Fredoka HUD with
 *             soft outlines
 *
 * Determinism: the whole game replays from the input stream (fixed ticks,
 * the tick action snapshot, integer rules, cartwheel/math.h); the debug
 * fields name the state the gates compare across hosts.
 *
 * Built twice: `bomberdude` reads devices; `bomberdude_auto` plays a pad
 * script itself through cw_set_raw_input_filter, so input-free hosts run it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "world.h"

const float TEAM_RGB[MAXP][3] = { { 1, 0.3f, 0.25f }, { 0.25f, 0.6f, 1 }, { 1, 0.8f, 0.15f }, { 0.4f, 0.9f, 0.35f } };
static const char *const ARENA_TITLES[ARENAS] = { "Clover Commons", "Frostbite Pond", "Ember Forge", "Jungle Temple",
                                                  "Gumdrop Gardens", "Neon Nights", "Powder Deck", "Sandstorm Ruins" };
static const float THEME_RGB[TH_KINDS][3] = { { 0.4f, 0.8f, 0.3f }, { 0.55f, 0.85f, 1 }, { 1, 0.45f, 0.15f }, { 0.3f, 0.65f, 0.35f },
                                              { 1, 0.55f, 0.75f }, { 0.65f, 0.35f, 1 }, { 0.2f, 0.5f, 0.75f }, { 0.95f, 0.75f, 0.4f } };

/* ── the Arena node: every arena scene's root ───────────────────────── */

static const char *const THEME_NAMES[] = { "MEADOW", "FROST", "FORGE", "TEMPLE", "CANDY", "NEON", "PIRATE", "DESERT", NULL };
static const cw_prop arena_props[] = {
  CW_PROP_FIELD(arena_info, index, CW_T_INT, CW_PROP_DEFAULT, "Arena number 1..8 (0: the title backdrop)."),
  CW_PROP_FIELD(arena_info, title, CW_T_STRING, CW_PROP_DEFAULT, "Its name."),
  CW_PROP_FIELD(arena_info, blurb, CW_T_STRING, CW_PROP_DEFAULT, "One line on its twist."),
  { .name = "theme", .type = CW_T_ENUM, .flags = CW_PROP_DEFAULT, .offset = offsetof(arena_info, theme), .size = sizeof(int),
    .enum_names = THEME_NAMES, .doc = "Look and twist." },
  CW_PROP_FIELD(arena_info, layout, CW_T_STRING, CW_PROP_DEFAULT, "13 rows of 15 tiles, '|' between rows (tools/make-arenas.mjs)."),
  CW_PROP_FIELD(arena_info, crate_pct, CW_T_INT, CW_PROP_DEFAULT, "Chance a '?' tile holds a crate."),
  CW_PROP_FIELD(arena_info, seed, CW_T_INT, CW_PROP_DEFAULT, "Crates and power-ups."),
  CW_PROP_FIELD(arena_info, cpu, CW_T_INT, CW_PROP_DEFAULT, "Adventure: CPU rivals."),
  CW_PROP_FIELD(arena_info, cpu_level, CW_T_INT, CW_PROP_DEFAULT, "Adventure: 0 easy, 1 normal, 2 hard."),
  CW_PROP_FIELD(arena_info, round_secs, CW_T_INT, CW_PROP_DEFAULT, "Seconds before sudden death."),
  CW_PROP_END,
};
static cw_node_type arena_type = { .name = "Arena", .parent = "Node3D", .data_size = sizeof(arena_info), .props = arena_props,
                                   .doc = "Root of a bomberdude arena: its layout, theme and twist." };

/* ── state ──────────────────────────────────────────────────────────── */

typedef enum { GS_TITLE, GS_PLAYERS, GS_ARENA, GS_INTRO, GS_PLAY, GS_ROUND_END, GS_PODIUM, GS_PAUSE } game_state;
enum { MODE_BATTLE, MODE_ADVENTURE };
enum { SLOT_OFF, SLOT_HUMAN, SLOT_KEYS2, SLOT_CPU_EASY, SLOT_CPU_NORMAL, SLOT_CPU_HARD, SLOT_KINDS };
static const char *const SLOT_NAME[SLOT_KINDS] = { "Off", "Player", "Player (arrows)", "CPU Easy", "CPU Normal", "CPU Hard" };

typedef enum {
  SFX_FUSE, SFX_BOOM, SFX_BOOM_BIG, SFX_CRATE, SFX_PICK, SFX_POP, SFX_KO, SFX_KICK, SFX_TELE, SFX_VENT, SFX_CANNON, SFX_THUD,
  SFX_MOVE, SFX_SELECT, SFX_LOCKED, SFX_READY, SFX_GO, SFX_ROUND_WIN, SFX_MATCH_WIN, SFX_ALARM, SFX_UNLOCK, SFX_COUNT
} sfx_id;

/* progress, kept in cw_app_desc.save: a small versioned, checksummed layout */
typedef struct save_data {
  uint32_t magic;          /* 'BDSV' */
  uint16_t version, size;
  uint32_t checksum;       /* FNV-1a over everything after this field */
  uint8_t cleared[ARENAS]; /* Adventure stages beaten */
  uint8_t shown[ARENAS];   /* its unlock has played on the select screen */
  int32_t best_secs[ARENAS];
  uint8_t crit[MAXP], slot[MAXP]; /* the last Battle setup */
  uint8_t wins_needed, pad[3]; /* pad: the graphics settings (cw_quality_saved, 24 bits little-endian) */
  /* version 4: the text size and background (cw_text_scale_saved: one byte holds both) */
  uint8_t text_scale, spare[3];
} save_data;
#define SAVE_V3_SIZE offsetof(save_data, text_scale) /* a version 3 save ends before the text settings */
static save_data SAVE;

static struct {
  /* debug fields */
  int32_t state, mode, arena, round, alive, winner, ticks, state_ticks, timer;
  int32_t wins[MAXP], kos[MAXP], placed[MAXP], match_winner, pos[MAXP * 2], slots[MAXP], picked, chains;
  uint8_t in_dbg[MAXP];
  uint32_t hash, state_hash;
  /* the match */
  int slot[MAXP], crit[MAXP];
  int wins_needed;
  int menu;                 /* title / pause cursor */
  int cursor;               /* setup and select cursors */
  int unlock_tile, unlock_ticks, bump_tile, bump_ticks;
  int round_winner;
  cw_node scene, camera;
  arena_info *info;
  sim S;
  ai_mem ai[MAXP];
  /* camera */
  t3_vec3 cam_at, cam_eye;
  bool cam_snap;
  /* mouse walking: the tile player 1 is walking to */
  /* floating callouts over the arena (render only): power-ups, knockouts */
  struct { int x, y, kind, who, age; } pops[16];
  int npops;
  cw_action a_left, a_right, a_up, a_down, a_bomb, a_act, a_start, a_cancel, a_pause, a_pick, a_settings;
  cw_action k2_left, k2_right, k2_up, k2_down, k2_bomb, k2_act;
  /* dev aids, read once at start */
  bool cam_close;           /* BOMBERDUDE_CAM_CLOSE: the camera close on player 1 */
  int boom_tick;            /* BOMBERDUDE_BOOM: a big blast mid-arena at that tick (-1 none) */
  FILE *rec;                /* BOMBERDUDE_RECORD: every tick's input logged (tools/make-gate-script.mjs) */
} G;

/* ── autopilot: a gate's pad script compiled in ───────────────────── */

#ifdef BOMBERDUDE_AUTOPILOT
static const struct { int frame, pad; uint32_t buttons; float lx, ly; } SCRIPT[] = {
#include "script.inc"
};
static void autopilot(cw_raw_input *in, uint64_t frame, void *user) {
  (void)user;
  memset(in->pads, 0, sizeof in->pads);
  memset(in->keys, 0, sizeof in->keys);
  memset(in->keys_pressed, 0, sizeof in->keys_pressed);
  in->mouse_buttons = in->mouse_pressed = 0;
  if (getenv("BOMBERDUDE_ART_HOLD")) return; /* art captures: stay on the title */
  for (int p = 0; p < 2; p++) {
    in->pads[p].connected = true;
    int cur = -1;
    for (int i = 0; i < (int)CW_ARRAY_LEN(SCRIPT) && (uint64_t)(SCRIPT[i].frame - 1) <= frame; i++)
      if (SCRIPT[i].pad == p) cur = i;
    if (cur < 0) continue;
    in->pads[p].buttons = SCRIPT[cur].buttons;
    in->pads[p].axes[CW_AXIS_LX] = SCRIPT[cur].lx;
    in->pads[p].axes[CW_AXIS_LY] = SCRIPT[cur].ly;
  }
}
#endif

/* ── audio ──────────────────────────────────────────────────────────── */

static const char *const SFX_FILES[SFX_COUNT] = {
  "audio/fuse.wav", "audio/boom.wav", "audio/boom_big.wav", "audio/crate.wav", "audio/pick.wav", "audio/pop.wav", "audio/ko.wav",
  "audio/kick.wav", "audio/tele.wav", "audio/vent.wav", "audio/cannon.wav", "audio/thud.wav", "audio/move.wav", "audio/select.wav",
  "audio/locked.wav", "audio/ready.wav", "audio/go.wav", "audio/round_win.wav", "audio/match_win.wav", "audio/alarm.wav",
  "audio/unlock.wav" };
static const char *const MUSIC[TH_KINDS + 1] = { "audio/music_meadow.ogg", "audio/music_frost.ogg", "audio/music_forge.ogg",
  "audio/music_temple.ogg", "audio/music_candy.ogg", "audio/music_neon.ogg", "audio/music_pirate.ogg", "audio/music_desert.ogg",
  "audio/music_title.ogg" };
static struct {
  bool on;
  cw_bus music_bus, sfx_bus;
  cw_res sfx_res[SFX_COUNT], music_res;
  cw_sound sfx[SFX_COUNT];
  cw_voice music;
  int music_now;
  int cooldown[SFX_COUNT];
} A = { .music_now = -1 };

static void sfx(sfx_id id, float volume) {
  if (!A.on || A.cooldown[id] > 0) return;
  A.cooldown[id] = 3;
  cw_audio_play(A.sfx[id], &(cw_play_desc){ .bus = A.sfx_bus, .volume = volume });
}
static void sfx_at(sfx_id id, int cx, int cy, float volume) {
  if (!A.on || A.cooldown[id] > 0) return;
  A.cooldown[id] = 3;
  cw_play_desc d = { .bus = A.sfx_bus, .volume = volume, .spatial = true };
  d.space = (cw_voice_3d){ .position = { tx_w(cx), 0.5f, tz_w(cy) }, .model = CW_ATTEN_INVERSE, .ref_distance = 12, .max_distance = 80,
                           .rolloff = 0.5f };
  cw_audio_play(A.sfx[id], &d);
}

static void music(int which) {
  if (!A.on || which == A.music_now) return;
  if (A.music_now >= 0) cw_voice_stop(A.music, 0.6f);
  A.music_now = which;
  A.music_res = cw_res_load(MUSIC[which], 0);
  cw_sound s = cw_res_audio_sound(A.music_res, &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.music_bus });
  A.music = cw_audio_play(s, &(cw_play_desc){ .loop = true, .fade_in = 1.0f, .bus = A.music_bus });
}

static void audio_init(void) {
  A.on = getenv("BOMBERDUDE_NO_AUDIO") == NULL && cw_audio_init(&(cw_audio_desc){ 0 });
  if (!A.on) return;
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.38f);
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1, 0.1f } });
  for (int i = 0; i < SFX_COUNT; i++) {
    A.sfx_res[i] = cw_res_load(SFX_FILES[i], 0);
    A.sfx[i] = cw_res_audio_sound(A.sfx_res[i], &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = 4, .bus = A.sfx_bus });
  }
}

/* ── devices: the engine says which one each player used last ──────── */

/* P1's device, as the engine's prompts see it (cartwheel/prompt.h): it picks
 * the prompts, the touch controls and whether the pointer drives the menus.
 * Asked once a frame in update (prompts are presentation, never the fixed
 * tick); input.mode publishes it (0 mouse, 1 pad, 2 keys, 3 touch). */
static cw_prompt_device p1_device = CW_PROMPT_PAD;
static int32_t input_mode = 1; /* debug field input.mode: 0 mouse, 1 pad, 2 keys, 3 touch */
/* a pad or the keyboard reads glyphs; the mouse and touch click or tap what they see */
#define GLYPHS (p1_device == CW_PROMPT_PAD || p1_device == CW_PROMPT_KEYBOARD)
#define ON_TOUCH (p1_device == CW_PROMPT_TOUCH)
/* the pointer drives the menus: P1 clicks or taps (now, or as the device in use), or the mouse just moved */
#define POINTER (!GLYPHS || mouse_moved || pointer_now)
/* The one switch for this game's own on-screen touch controls (wasmcart's
 * doctrine: a host may draw its own overlay from the manifest; a game that
 * draws its own keeps them behind a switch). BOMBERDUDE_TOUCH_CONTROLS=0 or the
 * debug request `touch_controls 0` turns them off. */
static bool touch_controls = true;
static uint32_t prev_mouse_buttons;
static float prev_mx, prev_my;
static bool mouse_clicked, mouse_right_clicked, mouse_moved, pointer_now;

static void read_mouse(void) {
  const cw_raw_input *in = cw_input_raw();
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse_buttons & 1);
  mouse_right_clicked = (in->mouse_buttons & 2) && !(prev_mouse_buttons & 2);
  mouse_moved = (in->mouse_x != prev_mx || in->mouse_y != prev_my) && in->mouse_present && prev_mx + prev_my != 0;
  prev_mouse_buttons = in->mouse_buttons;
  prev_mx = in->mouse_x;
  prev_my = in->mouse_y;
  pointer_now = mouse_clicked || mouse_right_clicked || in->mouse_from_touch;
  for (int t = 0; t < CW_MAX_TOUCHES; t++) pointer_now |= in->touches[t].active || in->touches[t].tapped;
}
static void read_device(void) {
  /* a touch-first device (a phone, a coarse-pointer browser) starts on touch
   * before its first tap. The hosts report pad 0 as connected with no pad in
   * hand, and the engine prefers a connected pad, so until P1 presses a pad
   * button or a key a touch-first device is held on touch */
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

/* ── on-screen touch controls (logical window coordinates) ──────────
 * A floating stick for the left thumb (anywhere on the left half: it centres
 * where the thumb lands), BOMB and ACTION for the right thumb, and a pause
 * button; bound to the same actions as the pad (touch:<id> bindings), drawn
 * only while touch is the device in use. */
static int TOUCH_STICK = -1, TOUCH_BOMB = -1, TOUCH_ACT = -1, TOUCH_PAUSE = -1;
static void touch_layout(void) {
  int w, h;
  cw_window_size(&w, &h);
  float W = (float)w, H = (float)h, k = H / 540.0f;
  if (TOUCH_STICK < 0) {
    TOUCH_STICK = cw_touch_add_stick(0, H * 0.22f, W * 0.5f, H * 0.78f, 62 * k, true);
    TOUCH_BOMB = cw_touch_add_button(W - 196 * k, H - 196 * k, 176 * k, 176 * k);
    TOUCH_ACT = cw_touch_add_button(W - 336 * k, H - 150 * k, 130 * k, 130 * k);
    TOUCH_PAUSE = cw_touch_add_button(W * 0.5f - 46 * k, 76 * k, 92 * k, 46 * k);
    char b[48];
    static const char *const DIRS[4] = { "left", "right", "up", "down" };
    cw_action dirs[4] = { G.a_left, G.a_right, G.a_up, G.a_down };
    for (int d = 0; d < 4; d++) {
      snprintf(b, sizeof b, "touch:%d.%s", TOUCH_STICK, DIRS[d]);
      cw_action_bind_text(dirs[d], b);
    }
    snprintf(b, sizeof b, "touch:%d", TOUCH_BOMB);
    cw_action_bind_text(G.a_bomb, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_ACT);
    cw_action_bind_text(G.a_act, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_PAUSE);
    cw_action_bind_text(G.a_pause, b);
  } else {
    cw_touch_set_rect(TOUCH_STICK, 0, H * 0.22f, W * 0.5f, H * 0.78f);
    cw_touch_set_rect(TOUCH_BOMB, W - 196 * k, H - 196 * k, 176 * k, 176 * k);
    cw_touch_set_rect(TOUCH_ACT, W - 336 * k, H - 150 * k, 130 * k, 130 * k);
    cw_touch_set_rect(TOUCH_PAUSE, W * 0.5f - 46 * k, 76 * k, 92 * k, 46 * k);
  }
}
static cw_vec2 mouse_canvas(void) {
  const cw_raw_input *in = cw_input_raw();
  return cw_r2d_window_to_canvas(cw_v2(in->mouse_x, in->mouse_y));
}

/* menu input: any pad, the keys */
static bool any_pressed(cw_action a, cw_action b) {
  for (int p = 0; p < MAXP; p++)
    if (cw_action_just_pressed(p, a)) return true;
  return b >= 0 && cw_action_just_pressed(0, b);
}
static bool m_confirm(void) { return any_pressed(G.a_bomb, G.k2_bomb) || any_pressed(G.a_start, -1); }
static bool m_back(void) { return any_pressed(G.a_cancel, -1); }
static int m_dx(void) { return (any_pressed(G.a_right, G.k2_right) ? 1 : 0) - (any_pressed(G.a_left, G.k2_left) ? 1 : 0); }
static int m_dy(void) { return (any_pressed(G.a_down, G.k2_down) ? 1 : 0) - (any_pressed(G.a_up, G.k2_up) ? 1 : 0); }

/* ── save data ──────────────────────────────────────────────────────── */

#define SAVE_MAGIC 0x56534442u /* 'BDSV' */
#define SAVE_VERSION 4

/* FNV-1a over the first n bytes after the checksum, xor the version and size */
static uint32_t save_sum_n(size_t n) {
  const uint8_t *b = (const uint8_t *)&SAVE + offsetof(save_data, cleared);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n - offsetof(save_data, cleared); i++) h = (h ^ b[i]) * 16777619u;
  return h ^ (SAVE.version | (uint32_t)SAVE.size << 16);
}
static uint32_t save_sum(void) { return save_sum_n(sizeof SAVE); }
static uint32_t gfx_saved(void) { return SAVE.pad[0] | (uint32_t)SAVE.pad[1] << 8 | (uint32_t)SAVE.pad[2] << 16; }
static void save_write(void) {
  uint32_t q = cw_quality_saved();
  if (q)
    for (int i = 0; i < 3; i++) SAVE.pad[i] = (uint8_t)(q >> (8 * i));
  SAVE.text_scale = cw_text_scale_saved();
  SAVE.checksum = save_sum();
  cw_save_commit();
}
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC;
  SAVE.version = SAVE_VERSION;
  SAVE.size = (uint16_t)sizeof SAVE;
  SAVE.shown[0] = 1;
  SAVE.wins_needed = 3;
  for (int i = 0; i < MAXP; i++) {
    SAVE.crit[i] = (uint8_t)i;
    SAVE.slot[i] = i == 0 ? SLOT_HUMAN : SLOT_CPU_NORMAL;
  }
  SAVE.checksum = save_sum();
}
static int32_t dbg_migrated;
static void save_check(void) {
  /* a version 3 save (no text settings) carries over with the defaults */
  if (SAVE.magic == SAVE_MAGIC && SAVE.version == 3 && SAVE.size == SAVE_V3_SIZE && SAVE.checksum == save_sum_n(SAVE_V3_SIZE)) {
    memset((uint8_t *)&SAVE + SAVE_V3_SIZE, 0, sizeof SAVE - SAVE_V3_SIZE);
    SAVE.version = SAVE_VERSION;
    SAVE.size = (uint16_t)sizeof SAVE;
    SAVE.checksum = save_sum();
    dbg_migrated = 1;
  }
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) {
    if (SAVE.magic) cw_warn("bomberdude: save data rejected (magic %08x version %u), starting fresh", SAVE.magic, SAVE.version);
    save_reset();
  }
  for (int i = 0; i < MAXP; i++) {
    if (SAVE.crit[i] >= CRIT_KINDS) SAVE.crit[i] = (uint8_t)i;
    if (SAVE.slot[i] >= SLOT_KINDS) SAVE.slot[i] = SLOT_CPU_NORMAL;
  }
  if (SAVE.wins_needed < 1 || SAVE.wins_needed > 5) SAVE.wins_needed = 3;
  SAVE.slot[0] = SLOT_HUMAN; /* player 1 is always you */
}
static bool stage_open(int i) { return i == 0 || (i < ARENAS && SAVE.cleared[i - 1]); }
static void save_preset(int n) { /* test / dev hook: stages 1..n beaten */
  save_reset();
  for (int i = 0; i < n && i < ARENAS; i++) SAVE.cleared[i] = SAVE.shown[i] = 1;
  if (n < ARENAS) SAVE.shown[n] = 1;
  save_write();
}
static int32_t dbg_unlocked, dbg_cleared;
static void save_fields(void) {
  dbg_unlocked = dbg_cleared = 0;
  for (int i = 0; i < ARENAS; i++) {
    dbg_unlocked += stage_open(i);
    if (SAVE.cleared[i]) dbg_cleared |= 1 << i;
  }
}
static void cmd_progress(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3 && !strcmp(argv[1], "preset")) {
    int n = atoi(argv[2]);
    if (n < 0 || n > ARENAS) {
      cw_reply_raw(r, "{\"error\":\"progress preset <0..8>\"}");
      return;
    }
    save_preset(n);
  }
  save_fields();
  cw_reply_printf(r, "{\"unlocked\":%d,\"cleared\":%d,\"checksum\":%u}", dbg_unlocked, dbg_cleared, SAVE.checksum);
}

/* ── scenes and rounds ──────────────────────────────────────────────── */

static cw_node find_type(cw_node n, const char *type) {
  if (cw_node_is(n, type)) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_type(c, type);
    if (cw_node_valid(r)) return r;
  }
}

/* Low and Very Low draw the arena after a depth prepass: the crates, walls and grass stack several deep over the
 * meadow, and the prepass shades each pixel once (the same picture; from Medium up the scene as authored) */
static void scene_prepass(void) {
  cw_node post = cw_node_valid(G.scene) ? find_type(G.scene, "PostProcess") : (cw_node){ 0 };
  if (cw_node_valid(post)) cw_node_set(post, "depth_prepass", cw_val_bool(cw_gfx_effective(CW_GFX_EFFECTS) <= CW_QUALITY_LOW));
}

static void load_scene(int index) {
  cw_tree *t = cw_scene_app_tree();
  fx_reset();
  scenery_reset();
  world_reset();
  if (cw_node_valid(G.scene)) cw_node_free(G.scene);
  char path[64];
  if (index) snprintf(path, sizeof path, "scenes/arena%d.cwscene", index);
  else snprintf(path, sizeof path, "scenes/title.cwscene");
  G.scene = cw_scene_instance_path(path);
  if (CW_HANDLE_IS_NULL(G.scene)) {
    cw_error("bomberdude: %s failed to load", path);
    return;
  }
  cw_node_add_child(cw_tree_root(t), G.scene);
  G.info = cw_node_data(G.scene, "Arena");
  G.camera = find_type(G.scene, "Camera3D");
  G.arena = index;
  G.cam_snap = true;
  scene_prepass();
}

/* a round: the arena rebuilt, seeded by the round so every round differs */
static void start_round(void) {
  load_scene(G.arena);
  bool on[MAXP];
  for (int i = 0; i < MAXP; i++) on[i] = G.slot[i] != SLOT_OFF;
  sim_load(&G.S, G.info, on, 1000u + (uint32_t)G.round * 7919u);
  if (getenv("BOMBERDUDE_ROUND_SECS")) { /* dev aid */
    G.S.round_ticks = atoi(getenv("BOMBERDUDE_ROUND_SECS")) * 60;
    sim_refresh(&G.S); /* the danger map reads it (sudden death) */
  }
  for (int i = 0; i < MAXP; i++) ai_reset(&G.ai[i], i, (uint32_t)G.round + 1);
  world_build(G.scene, &G.S, G.crit);
  fx_init(G.scene, G.S.theme);
  scenery_build(G.scene, G.S.theme, (uint32_t)G.arena * 131u);
  G.npops = 0;
  G.state = GS_INTRO;
  G.state_ticks = 0;
  G.round_winner = -2;
  music(G.S.theme);
}

/* the title backdrop: the meadow and the four critters in a row */
static void enter_title(void) {
  load_scene(0);
  bool on[MAXP] = { true, true, true, true };
  sim_load(&G.S, G.info, on, 5);
  world_build(G.scene, &G.S, G.crit);
  fx_init(G.scene, G.S.theme);
  scenery_build(G.scene, G.S.theme, 7);
  G.state = GS_TITLE;
  G.state_ticks = 0;
  music(TH_KINDS);
}

static void begin_match(int mode, int arena) {
  G.mode = mode;
  G.arena = arena;
  G.round = 1;
  memset(G.wins, 0, sizeof G.wins);
  G.match_winner = -1;
  if (mode == MODE_ADVENTURE) {
    /* you against the stage's rivals, first to two */
    load_scene(arena);
    int cpus = G.info ? G.info->cpu : 1, lvl = G.info ? G.info->cpu_level : 0;
    for (int i = 0; i < MAXP; i++) G.slot[i] = i == 0 ? SLOT_HUMAN : i <= cpus ? SLOT_CPU_EASY + lvl : SLOT_OFF;
    G.wins_needed = 2;
  } else G.wins_needed = SAVE.wins_needed;
#ifdef BOMBERDUDE_AUTOPILOT
  G.slot[0] = SLOT_CPU_HARD; /* the self-playing build (demo, input-free hosts): player 1 plays itself after the menus */
#endif
  if (getenv("BOMBERDUDE_FIRST_TO")) G.wins_needed = atoi(getenv("BOMBERDUDE_FIRST_TO")); /* dev aid */
  start_round();
}

static void enter_arena_select(void) {
  G.state = GS_ARENA;
  G.state_ticks = 0;
  G.unlock_tile = -1;
  G.bump_ticks = 0;
  if (G.mode == MODE_ADVENTURE)
    for (int i = 1; i < ARENAS; i++)
      if (stage_open(i) && !SAVE.shown[i]) {
        SAVE.shown[i] = 1;
        save_write();
        G.unlock_tile = i;
        G.unlock_ticks = 0;
        G.cursor = i;
        break;
      }
}

/* ── debug channel ──────────────────────────────────────────────────── */

static int pending_arena = -1;
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
static void cmd_arena(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 0;
  if (n < 1 || n > ARENAS) {
    cw_reply_error(r, "usage: arena <1..8>: a Battle round there now");
    return;
  }
  pending_arena = n;
  cw_reply_printf(r, "{\"arena\":%d}", n);
}

static void cmd_touch_controls(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) touch_controls = atoi(argv[1]) != 0;
  cw_reply_printf(r, "{\"touch_controls\":%d}", touch_controls);
}

/* ── the app ────────────────────────────────────────────────────────── */

static cw_font font_ui, font_ui_bold, font_title;

/* a graphics setting changed: thin or thicken the grass and the crowd */
static void quality_changed(cw_quality q, void *u) {
  (void)q, (void)u;
  scenery_density(cw_quality_density());
  scene_prepass();
}

static void init(void *u) {
  (void)u;
  save_check();
  cw_quality_on_change(quality_changed, NULL);
  cw_quality_full_resolution(true); /* Auto gives up detail, never pixels */
  cw_quality_restore(gfx_saved()); /* the player's graphics settings, if they chose any; else Auto */
  cw_quality_auto(true, 0);
  cw_text_scale_restore(SAVE.text_scale); /* the player's text size and background, if they chose any */
  scenery_density(cw_quality_density());
#ifdef BOMBERDUDE_PRESET
  save_preset(BOMBERDUDE_PRESET);
#endif
  const char *pp = getenv("BOMBERDUDE_PROGRESS");
  if (pp && atoi(pp) >= 0 && atoi(pp) <= ARENAS) save_preset(atoi(pp));
  save_fields();
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("bomberdude: input.cfg had errors (see above)");
  cw_input_commit_defaults();
  G.a_left = cw_action_find("left");
  G.a_right = cw_action_find("right");
  G.a_up = cw_action_find("up");
  G.a_down = cw_action_find("down");
  G.a_bomb = cw_action_find("bomb");
  G.a_act = cw_action_find("act");
  G.a_start = cw_action_find("start");
  G.a_cancel = cw_action_find("cancel");
  G.a_pause = cw_action_find("pause");
  G.a_pick = cw_action_find("pick");
  G.a_settings = cw_action_find("settings");
  touch_layout();
  G.k2_left = cw_action_find("k2_left");
  G.k2_right = cw_action_find("k2_right");
  G.k2_up = cw_action_find("k2_up");
  G.k2_down = cw_action_find("k2_down");
  G.k2_bomb = cw_action_find("k2_bomb");
  G.k2_act = cw_action_find("k2_act");
  audio_init();
  /* a 10 px distance range at the 48 px bake: room for the HUD's outlines
   * (up to 0.15 of the size on small text) and the title's glow */
  const cw_font_desc fd = { .mode = CW_FONT_SDF, .sdf_padding = 10 };
  font_ui = cw_font_load("fonts/Fredoka-SemiBold.ttf", &fd);
  font_ui_bold = cw_font_load("fonts/Fredoka-Bold.ttf", &fd);
  font_title = cw_font_load("fonts/TitanOne-Regular.ttf", &fd);
  /* Prepare printable English UI glyphs while loading. SDF glyph generation
   * must not interrupt gameplay when a new score or result first appears. */
  const cw_font ui_fonts[] = {font_ui, font_ui_bold, font_title};
  for (unsigned f = 0; f < sizeof ui_fonts / sizeof ui_fonts[0]; f++)
    cw_font_preload_range(ui_fonts[f], 32, 126);

  for (int i = 0; i < MAXP; i++) {
    G.slot[i] = SAVE.slot[i];
    G.crit[i] = SAVE.crit[i];
  }
  G.wins_needed = SAVE.wins_needed;
  G.match_winner = -1;
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.mode", &G.mode);
  cw_debug_i32("game.arena", &G.arena);
  cw_debug_i32("round", &G.round);
  cw_debug_i32("round.alive", &G.alive);
  cw_debug_i32("round.winner", &G.winner);
  cw_debug_i32("round.timer", &G.timer);
  cw_debug_field("match.wins", G.wins, CW_DBG_I32, MAXP);
  cw_debug_i32("match.winner", &G.match_winner);
  cw_debug_field("round.kos", G.kos, CW_DBG_I32, MAXP);
  cw_debug_field("round.placed", G.placed, CW_DBG_I32, MAXP);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_field("players.pos", G.pos, CW_DBG_I32, MAXP * 2);
  cw_debug_field("players.slot", G.slots, CW_DBG_I32, MAXP);
  cw_debug_field("players.input", G.in_dbg, CW_DBG_U8, MAXP);
  cw_debug_i32("round.picked", &G.picked);
  cw_debug_i32("round.tick", &G.S.tick);
  cw_debug_i32("twist.belt_moves", &G.S.belt_moves);
  cw_debug_i32("twist.slides", &G.S.slides);
  cw_debug_i32("round.chains", &G.chains);
  cw_debug_u32("sim.hash", &G.hash);
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  cw_debug_u32("scenery.hash", &scenery_hash);
  cw_debug_i32("save.unlocked", &dbg_unlocked);
  cw_debug_i32("save.cleared", &dbg_cleared);
  cw_debug_bytes("save.bytes", &SAVE, sizeof SAVE);
  cw_debug_i32("save.migrated", &dbg_migrated);
  cw_debug_command("arena", "arena <1..8>: start a Battle round there now", cmd_arena);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings (binding text, as input.cfg writes it)", cmd_bind);
  cw_debug_command("touch_controls", "touch_controls <0|1>: this game's own on-screen touch controls", cmd_touch_controls);
  cw_debug_i32("input.mode", &input_mode);
  if (getenv("BOMBERDUDE_TOUCH_CONTROLS") && !atoi(getenv("BOMBERDUDE_TOUCH_CONTROLS"))) touch_controls = false;
  cw_debug_command("progress", "progress [show | preset <0..8>]: the save data; preset n = stages 1..n beaten", cmd_progress);
  G.state_hash = 2166136261u;
  G.cam_close = getenv("BOMBERDUDE_CAM_CLOSE") != NULL;
  const char *boom = getenv("BOMBERDUDE_BOOM");
  G.boom_tick = boom ? atoi(boom) : -1;
  if (getenv("BOMBERDUDE_RECORD")) G.rec = fopen(getenv("BOMBERDUDE_RECORD"), "w");
  enter_title();
  const char *a = getenv("BOMBERDUDE_ARENA"); /* dev aid: straight into a Battle round */
  if (a && atoi(a) >= 1 && atoi(a) <= ARENAS) {
    if (getenv("BOMBERDUDE_CPU_ONLY")) G.slot[0] = SLOT_CPU_HARD;
    const char *np = getenv("BOMBERDUDE_PLAYERS"); /* dev aid: only the first n slots, the rest off */
    for (int i = np ? atoi(np) : MAXP; i < MAXP; i++) G.slot[i] = SLOT_OFF;
    if (np && getenv("BOMBERDUDE_CPU_ONLY"))
      for (int i = 0; i < atoi(np); i++) G.slot[i] = SLOT_CPU_HARD;
    begin_match(MODE_BATTLE, atoi(a));
  }
}

/* ── player input in a round ────────────────────────────────────────── */

static uint8_t vec_bits(cw_vec2 v) {
  uint8_t in = 0;
  if (v.x < -0.45f) in |= IN_LEFT;
  if (v.x > 0.45f) in |= IN_RIGHT;
  if (v.y < -0.45f) in |= IN_UP;
  if (v.y > 0.45f) in |= IN_DOWN;
  return in;
}

/* the second keyboard player (arrows): only when one joined on the players screen */
static int keys2_slot(void) {
  for (int i = 1; i < MAXP; i++)
    if (G.slot[i] == SLOT_KEYS2) return i;
  return -1;
}


static uint8_t human_input(int i) {
  int k2 = keys2_slot();
  uint8_t in = 0;
  if (i == k2) {
    in |= vec_bits(cw_input_vector(0, G.k2_left, G.k2_right, G.k2_up, G.k2_down, CW_DEADZONE_SCALED_RADIAL, 0.3f));
    if (cw_action_pressed(0, G.k2_bomb)) in |= IN_BOMB;
    if (cw_action_pressed(0, G.k2_act)) in |= IN_ACT;
    return in;
  }
  in |= vec_bits(cw_input_vector(i, G.a_left, G.a_right, G.a_up, G.a_down, CW_DEADZONE_SCALED_RADIAL, 0.3f));
  if (cw_action_pressed(i, G.a_bomb)) in |= IN_BOMB;
  if (cw_action_pressed(i, G.a_act)) in |= IN_ACT;
  if (i == 0) {
    if (k2 < 0) { /* nobody on the arrows: they are player 1's too */
      in |= vec_bits(cw_input_vector(0, G.k2_left, G.k2_right, G.k2_up, G.k2_down, CW_DEADZONE_SCALED_RADIAL, 0.3f));
      if (cw_action_pressed(0, G.k2_bomb)) in |= IN_BOMB;
      if (cw_action_pressed(0, G.k2_act)) in |= IN_ACT;
    }
  }
  return in;
}

/* ── camera (fixed tick: identical on every host) ─────────────────── */

static void camera_tick(void) {
  if (!cw_node_valid(G.camera)) return;
  t3_vec3 at = { 0, 0, -0.15f }, eye = { 0, 15.6f, 10.7f };
  int t = G.state_ticks;
  bool fast = false;
  switch (G.state) {
  case GS_TITLE:
  case GS_PLAYERS:
  case GS_ARENA:
    at = (t3_vec3){ 0.4f * cw_sin(G.ticks * 0.004f), 1.25f, 6.5f };
    eye = (t3_vec3){ at.x * 2, 2.1f, 13.6f };
    break;
  case GS_INTRO: { /* a swoop down from high over the arena */
    float k = cw_clampf(t / 110.0f, 0, 1), e = 1 - (1 - k) * (1 - k) * (1 - k);
    eye = (t3_vec3){ 6 * (1 - e), 15.6f + 7 * (1 - e), 10.7f - 7 * (1 - e) };
    fast = true;
    break;
  }
  case GS_ROUND_END: {
    int w = G.round_winner;
    if (w >= 0 && t > 30) { /* close on the winner */
      t3_vec3 p = { wx(G.S.p[w].x), 0.5f, wz(G.S.p[w].y) };
      at = p;
      eye = (t3_vec3){ p.x * 0.7f, 6.2f, p.z + 5.4f };
    }
    break;
  }
  case GS_PODIUM:
    at = (t3_vec3){ 0, 0.9f, 0.5f };
    eye = (t3_vec3){ 1.6f * cw_sin(G.ticks * 0.006f), 3.4f, 7.4f };
    break;
  default: break;
  }
  if (G.cam_close) {
    at = (t3_vec3){ wx(G.S.p[0].x), 0.5f, wz(G.S.p[0].y) };
    eye = (t3_vec3){ at.x, at.y + 1.6f, at.z + 2.6f };
  }
#ifdef BOMBERDUDE_AUTOPILOT
  const char *art_cam = getenv("BOMBERDUDE_ART_CAM"); /* art captures: "eyeX eyeY eyeZ atX atY atZ" */
  if (art_cam && sscanf(art_cam, "%f %f %f %f %f %f", &eye.x, &eye.y, &eye.z, &at.x, &at.y, &at.z) == 6) fast = true;
#endif
  if (G.cam_snap || fast) {
    G.cam_at = at;
    G.cam_eye = eye;
    G.cam_snap = false;
  } else {
    float k = 0.06f;
    G.cam_at.x += (at.x - G.cam_at.x) * k, G.cam_at.y += (at.y - G.cam_at.y) * k, G.cam_at.z += (at.z - G.cam_at.z) * k;
    G.cam_eye.x += (eye.x - G.cam_eye.x) * k, G.cam_eye.y += (eye.y - G.cam_eye.y) * k, G.cam_eye.z += (eye.z - G.cam_eye.z) * k;
  }
  t3_vec3 e = G.cam_eye;
  int sh = fx_shake();
  if (sh > 0) {
    float a = 0.012f * sh;
    e.x += a * cw_sin(G.ticks * 1.9f);
    e.y += a * cw_sin(G.ticks * 2.7f + 1);
    e.z += a * cw_sin(G.ticks * 2.3f + 2);
  }
  cw_node3d_set_position(G.camera, e);
  cw_node3d_look_at(G.camera, G.cam_at, (t3_vec3){ 0, 1, 0 });
}

/* the font text() picks for a size, and a string's box in it (both grow with
 * the player's text size, as drawing does) */
static cw_font ui_font(float size) { return size < 20 ? font_ui_bold : font_ui; }
static cw_text_metrics measure(const char *s, float size) { return cw_font_measure(ui_font(size), s, &(cw_text_style){ .size = size }); }
static cw_text_metrics measure_bold(const char *s, float size) { return cw_font_measure(font_title, s, &(cw_text_style){ .size = size }); }

/* cw_prompt_text_size for the layouts: prompts are measured while rendering
 * only (the engine counts a prompt asked for in the fixed tick as an error),
 * so the render keeps what it measured and the tick's hit tests reuse it */
static bool g_rendering;
static cw_vec2 psize(cw_font f, const char *s, int player, float size) {
  enum { N = 128 };
  static struct { char s[192]; float size; int player; cw_vec2 v; } cache[N];
  static int next;
  for (int i = 0; i < N; i++)
    if (cache[i].size == size && cache[i].player == player && !strcmp(cache[i].s, s)) {
      if (g_rendering) cache[i].v = cw_prompt_text_size(f, s, player, size);
      return cache[i].v;
    }
  if (!g_rendering) { /* not measured yet: the text alone, glyph names and all */
    cw_text_metrics m = cw_font_measure(f, s, &(cw_text_style){ .size = size });
    return cw_v2(m.width, m.height);
  }
  cw_vec2 v = cw_prompt_text_size(f, s, player, size);
  snprintf(cache[next].s, sizeof cache[next].s, "%s", s);
  cache[next].size = size, cache[next].player = player, cache[next].v = v;
  next = (next + 1) % N;
  return v;
}
/* a prompt line broken into lines no wider than maxw: at the wide gaps between
 * its items ("   ") where it can, else between words */
static const char *prompt_wrap(const char *s, float size, float maxw) {
  static char out[640];
  enum { MAXW = 64 };
  const char *w0[MAXW];
  int wl[MAXW], gap[MAXW], n = 0;
  for (const char *p = s; *p && n < MAXW;) {
    int g = 0;
    while (*p == ' ') p++, g++;
    if (!*p) break;
    const char *e = p;
    while (*e && *e != ' ') e++;
    w0[n] = p, wl[n] = (int)(e - p), gap[n] = n ? g : 0, n++;
    p = e;
  }
  cw_font f = ui_font(size);
  size_t o = 0;
  char line[640];
  int start = 0;
  while (start < n) {
    int end = start + 1; /* words [start, end) fit */
    for (; end < n; end++) {
      size_t l = 0;
      for (int k = start; k <= end; k++) {
        if (k > start) for (int g = 0; g < gap[k] && l < sizeof line - 1; g++) line[l++] = ' ';
        for (int c = 0; c < wl[k] && l < sizeof line - 1; c++) line[l++] = w0[k][c];
      }
      line[l] = 0;
      if (psize(f, line, 0, size).x > maxw) break;
    }
    if (end < n) { /* break at the last wide gap in the line, if there is one */
      for (int k = end - 1; k > start; k--)
        if (gap[k] >= 3) {
          end = k;
          break;
        }
    }
    if (o && o < sizeof out - 1) out[o++] = '\n';
    for (int k = start; k < end; k++) {
      if (k > start) for (int g = 0; g < gap[k] && o < sizeof out - 1; g++) out[o++] = ' ';
      for (int c = 0; c < wl[k] && o < sizeof out - 1; c++) out[o++] = w0[k][c];
    }
    start = end;
  }
  out[o] = 0;
  return out;
}
/* the height prompt_line_a would take */
static float prompt_line_h(const char *s, float W, float size) {
  cw_font f = ui_font(size);
  float drawn = cw_text_drawn_size(f, size), padx = drawn * 0.8f, pady = drawn * 0.3f;
  return psize(f, prompt_wrap(s, size, W - 2 * padx - W * 0.04f), 0, size).y + 2 * pady;
}
/* ── layout shared by the update (mouse hit tests) and the HUD ─────── */

/* Every screen is laid out from the size its text draws at (the player's
 * Text size grows it): a box is as big as what it holds, a row that no longer
 * fits the canvas becomes a column, and a line that would not fit is wrapped
 * or left out (the controls line, which the pause menu and the round start
 * also show). The same layout serves the mouse hit tests in the tick. */
static const char *menu_prompt(void);
static const char *controls_line(void);
static cw_text_metrics measure_w(const char *s, float size, float wrap, cw_text_align align) {
  return cw_font_measure(ui_font(size), s, &(cw_text_style){ .size = size, .wrap_width = wrap, .align = align });
}
static const char *const TITLE_ITEMS[3] = { "QUICK BATTLE", "BATTLE", "ADVENTURE" };
static const char *const TITLE_SUBS[3] = { "you vs 3 CPUs, right now", "1-4 players and CPUs", "beat the stages in order" };
static const char *const TITLE_TAG = "four critters. eight arenas. a lot of boom.";
#define TITLE_ITEM 25
#define TITLE_SUB 14
#define LOGO_SIZE 88
typedef struct {
  cw_rect btn[3];
  float sub_dy, logo_size, logo_y, tag_y, controls_y, prompt_bottom;
  bool tag, controls;
} title_layout;
static title_layout title_layout_of(float W, float H) {
  float s = H / 540.0f;
  title_layout L = { 0 };
  float bw = 214 * s, ht = measure_bold("QUICK BATTLE", TITLE_ITEM * s).height, hs = measure("Ay", TITLE_SUB * s).height;
  for (int i = 0; i < 3; i++)
    bw = fmaxf(bw, fmaxf(measure_bold(TITLE_ITEMS[i], TITLE_ITEM * s).width, measure(TITLE_SUBS[i], TITLE_SUB * s).width) + 28 * s);
  float bh = fmaxf(58 * s, 4 * s + ht + hs + 6 * s);
  L.sub_dy = 4 * s + ht;
  L.prompt_bottom = H - 10 * s;
  float ptop = L.prompt_bottom - prompt_line_h(menu_prompt(), W, 17 * s), gap = 18 * s;
  bool row = 3 * bw + 2 * gap <= W - 24 * s;
  float top;
  if (row) {
    top = fminf(H * 0.78f, ptop - 14 * s - bh);
    float x0 = W * 0.5f - (3 * bw + 2 * gap) * 0.5f;
    for (int i = 0; i < 3; i++) L.btn[i] = cw_rect_make(x0 + i * (bw + gap), top, bw, bh);
  } else {
    float g = 12 * s;
    top = ptop - 14 * s - 3 * bh - 2 * g;
    for (int i = 0; i < 3; i++) L.btn[i] = cw_rect_make(W * 0.5f - bw * 0.5f, top + i * (bh + g), bw, bh);
  }
  /* the logo, the tagline and the controls above the buttons, as far as they fit */
  L.logo_size = LOGO_SIZE * s;
  L.logo_y = H * 0.07f;
  float logo_h = measure_bold("B", L.logo_size).height * 0.95f;
  float tag_h = prompt_line_h(TITLE_TAG, W, 20 * s), ctl_h = prompt_line_h(controls_line(), W, 16 * s);
  float room = top - 12 * s;
  L.tag = L.controls = true;
  L.tag_y = fmaxf(H * 0.3f, L.logo_y + logo_h);
  L.controls_y = fmaxf(H * 0.375f, L.tag_y + tag_h + 8 * s);
  if (L.controls_y + ctl_h > room) L.controls = false;
  if (L.tag_y + tag_h > room) { /* a smaller logo makes room for the tagline */
    float need = L.tag_y + tag_h - room;
    L.logo_y = fmaxf(8 * s, L.logo_y - need);
    float k = fmaxf(0.55f, (room - tag_h - L.logo_y) / logo_h);
    if (k < 1) L.logo_size *= k, logo_h *= k;
    L.tag_y = L.logo_y + logo_h;
    L.tag = L.tag_y + tag_h <= room;
  }
  return L;
}
static cw_rect title_button(int i, float W, float H) { return title_layout_of(W, H).btn[i]; }

/* the players screen: four cards in a row as wide as the canvas allows, each
 * line in a card wrapped to its width and stacked by its measured height */
#define SLOT_P 26
#define SLOT_NAME_SZ 22
#define SLOT_WHO 18
#define SLOT_DEV 14
static const char *slot_dev(int i) {
  return G.slot[i] == SLOT_KEYS2 ? "arrows / Enter / R-Shift" : G.slot[i] != SLOT_HUMAN ? "" : i == 0 ? "pad 1  /  keyboard" : i == 1 ? "pad 2" : i == 2 ? "pad 3" : "pad 4";
}
typedef struct {
  cw_rect card[MAXP], wins;
  float face_r, top_h, h_p, h_name, h_who, h_dev, arrow_w, title_y, controls_y, prompt_bottom;
  bool controls, title;
} players_layout;
static players_layout players_layout_of(float W, float H) {
  float s = H / 540.0f;
  players_layout L = { 0 };
  float gap = 16 * s, w = fminf(200 * s, (W - 24 * s - 3 * gap) / 4);
  /* a card as wide as its longest unbroken word needs */
  L.arrow_w = measure("<", SLOT_WHO * s).width + 14 * s;
  float inner = w - 2 * L.arrow_w;
  L.h_p = measure_bold("P1", SLOT_P * s).height;
  L.h_name = measure("Mg", SLOT_NAME_SZ * s).height;
  L.h_who = 0, L.h_dev = 0;
  for (int i = 0; i < MAXP; i++) {
    for (int k = 0; k < SLOT_KINDS; k++) L.h_who = fmaxf(L.h_who, measure_w(SLOT_NAME[k], SLOT_WHO * s, inner, CW_ALIGN_CENTER).height);
    L.h_dev = fmaxf(L.h_dev, measure_w(slot_dev(i), SLOT_DEV * s, w - 12 * s, CW_ALIGN_CENTER).height);
  }
  L.h_dev = fmaxf(L.h_dev, measure_w("arrows / Enter / R-Shift", SLOT_DEV * s, w - 12 * s, CW_ALIGN_CENTER).height);
  L.face_r = fmaxf(21 * s, L.h_p * 0.6f);
  L.top_h = fmaxf(L.h_p + 4 * s, 2 * L.face_r * 1.18f + 4 * s);
  float rest = L.h_name + 4 * s + L.h_who + 4 * s + L.h_dev + 8 * s;
  float h = fmaxf(128 * s, 6 * s + L.top_h + rest);
  L.title_y = H * 0.03f;
  float title_h = measure_bold("WHO'S PLAYING?", 46 * s).height;
  L.prompt_bottom = H - 10 * s;
  float ptop = L.prompt_bottom - prompt_line_h(menu_prompt(), W, 16 * s);
  cw_text_metrics fm = measure_bold("FIGHT!   first to 5", 26 * s);
  float ww = fmaxf(320 * s, fm.width + 40 * s), wh = fmaxf(48 * s, fm.height + 14 * s);
  L.wins = cw_rect_make(W * 0.5f - ww * 0.5f, fminf(H * 0.86f - 8 * s, ptop - 12 * s - wh), ww, wh);
  /* no room for the cards: first a smaller face beside P1, then no heading */
  L.title = true;
  float y = fmaxf(H * 0.06f + 62 * s, L.title_y + title_h + 8 * s);
  if (y + h > L.wins.y - 12 * s) {
    L.face_r = fmaxf(14 * s, L.h_p * 0.42f);
    L.top_h = fmaxf(L.h_p + 4 * s, 2 * L.face_r * 1.18f + 4 * s);
    h = fmaxf(128 * s, 6 * s + L.top_h + rest);
  }
  if (y + h > L.wins.y - 12 * s) L.title = false, y = 12 * s;
  float x0 = (W - (4 * w + 3 * gap)) * 0.5f;
  for (int i = 0; i < MAXP; i++) L.card[i] = cw_rect_make(x0 + i * (w + gap), y, w, h);
  float bottom = y + h;
  float ctl_h = prompt_line_h(controls_line(), W, 16 * s);
  L.controls_y = fmaxf(H * 0.4f + 12 * s, bottom + 14 * s);
  L.controls = L.controls_y + ctl_h + 10 * s <= L.wins.y;
  return L;
}
static cw_rect slot_rect(int i, float W, float H) { return players_layout_of(W, H).card[i]; }
static cw_rect wins_rect(float W, float H) { return players_layout_of(W, H).wins; }

/* the arena select: eight tiles, 4 by 2; a tile shows its title when the
 * title fits it, else the stage under the cursor names itself above its blurb */
static const char *const ARENA_BLURB[ARENAS] = { "A sunny lawn. Nothing up its sleeve.", "Ice! Once you start sliding, you keep sliding.",
  "The vents glow, then they roar. Mind the glow.", "Step on a glyph, come out of its twin.",
  "Jelly crates wobble off the first blast. Hit them twice.", "The belts never stop. Neither does the city.",
  "When a cannon glows, its whole row is about to burn.", "Sand drags your feet. The storms hide the bombs." };
#define TILE_NUM 36
#define TILE_TITLE 18
#define TILE_BEST 14
typedef struct {
  cw_rect tile[ARENAS];
  float h_num, h_best, num_w, head_y, blurb_bottom, prompt_bottom;
  bool titles;
} arena_layout;
static arena_layout arena_layout_of(float W, float H) {
  float s = H / 540.0f;
  arena_layout L = { 0 };
  float gx = 16 * s, gy = 16 * s, tw = fminf(200 * s, (W - 24 * s - 3 * gx) / 4);
  L.h_num = measure_bold("8", TILE_NUM * s).height;
  L.num_w = measure_bold("8", TILE_NUM * s).width + 20 * s;
  L.h_best = measure("best 0:00", TILE_BEST * s).height;
  float head_h = measure_bold("PICK AN ARENA", 50 * s).height;
  L.head_y = H * 0.05f;
  float y0 = fmaxf(H * 0.24f, L.head_y + head_h + 10 * s);
  /* the bottom: the menu prompt, the blurb above it */
  L.prompt_bottom = H - 10 * s;
  float ptop = L.prompt_bottom - prompt_line_h(menu_prompt(), W, 15 * s);
  float blurb_h = 0;
  for (int i = 0; i < ARENAS; i++) {
    char b[200];
    snprintf(b, sizeof b, "%s:  %s", ARENA_TITLES[i], ARENA_BLURB[i]);
    blurb_h = fmaxf(blurb_h, prompt_line_h(b, W, 19 * s));
  }
  L.blurb_bottom = ptop - 10 * s;
  float room = L.blurb_bottom - blurb_h - 10 * s - y0; /* for two rows of tiles */
  float th_min = L.h_num + 16 * s; /* the best time goes beside the number */
  float title_h = 0;
  for (int i = 0; i < ARENAS; i++) title_h = fmaxf(title_h, measure_w(ARENA_TITLES[i], TILE_TITLE * s, tw - L.num_w - 6 * s, CW_ALIGN_LEFT).height);
  float th_title = fmaxf(L.h_num, title_h + 6 * s) + L.h_best + 22 * s;
  float th = fmaxf(108 * s, th_title);
  L.titles = 2 * th + gy <= room || th == 108 * s;
  if (!L.titles) th = fmaxf(fminf(108 * s, (room - gy) * 0.5f), th_min);
  float x0 = (W - (4 * tw + 3 * gx)) * 0.5f;
  for (int i = 0; i < ARENAS; i++) L.tile[i] = cw_rect_make(x0 + (i % 4) * (tw + gx), y0 + (i / 4) * (th + gy), tw, th);
  return L;
}
static cw_rect tile_rect(int i, float W, float H) { return arena_layout_of(W, H).tile[i]; }

/* the pause menu: PAUSED, the graphics row, the two text rows, the controls,
 * RESUME and QUIT; under the player cards when they leave room, else over
 * the arena alone */
enum { PM_RESUME, PM_QUIT, PM_TEXT_SIZE, PM_TEXT_BG, PM_ITEMS };
#define PAUSE_ROW 22
#define PAUSE_BTN 26
typedef struct {
  cw_rect btn[2], gfx, row[2];
  float title_y, gfx_hint_y, controls_y;
  bool hud, controls;
} pause_layout;
static float hud_bottom(float W, float H);
static pause_layout pause_layout_of(float W, float H) {
  float s = H / 540.0f;
  pause_layout L = { 0 };
  float title_h = measure_bold("PAUSED", 64 * s).height * 0.9f;
  float row_h = measure_bold("TEXT SIZE: 100%", PAUSE_ROW * s).height + 16 * s;
  float hint_h = psize(ui_font(16 * s), "{settings} settings", 0, 16 * s).y + 4 * s;
  float ctl_h = prompt_line_h(controls_line(), W, 16 * s);
  cw_text_metrics bm = measure_bold("QUIT TO TITLE", PAUSE_BTN * s);
  float bw = fmaxf(214 * s, bm.width + 36 * s), bh = fmaxf(58 * s, bm.height + 18 * s), g = 10 * s;
  float rw = 340 * s;
  static const char *const WIDEST[3] = { "GRAPHICS: VERY LOW Â· 2160p", "TEXT SIZE: 200%", "TEXT BACKGROUND: OUTLINE" };
  for (int k = 0; k < 3; k++) rw = fmaxf(rw, measure_bold(WIDEST[k], PAUSE_ROW * s).width + 40 * s);
  rw = fminf(rw, W - 24 * s);
  float total_ctl = title_h + g + row_h + hint_h + g + 2 * (row_h + g) + ctl_h + g + bh;
  float total = total_ctl - ctl_h - g;
  bool btn_row = 2 * bw + 18 * s <= W - 24 * s;
  if (!btn_row) total += bh + g, total_ctl += bh + g;
  float hb = hud_bottom(W, H) + 8 * s, bottom = H - 12 * s;
  L.hud = true;
  float avail = bottom - hb;
  if (avail < total) L.hud = false, hb = 10 * s, avail = bottom - hb;
  L.controls = avail >= total_ctl;
  float used = L.controls ? total_ctl : total;
  float y = hb + fmaxf(0, (avail - used) * 0.5f);
  L.title_y = y;
  y += title_h + g;
  L.gfx = cw_rect_make(W * 0.5f - rw * 0.5f, y, rw, row_h);
  y += row_h;
  L.gfx_hint_y = y + 2 * s;
  y += hint_h + g;
  for (int k = 0; k < 2; k++) {
    L.row[k] = cw_rect_make(W * 0.5f - rw * 0.5f, y, rw, row_h);
    y += row_h + g;
  }
  if (L.controls) {
    L.controls_y = y + cw_text_drawn_size(ui_font(16 * s), 16 * s) * 0.3f;
    y += ctl_h + g;
  }
  if (btn_row) {
    float x0 = W * 0.5f - (2 * bw + 18 * s) * 0.5f;
    for (int i = 0; i < 2; i++) L.btn[i] = cw_rect_make(x0 + i * (bw + 18 * s), y, bw, bh);
  } else
    for (int i = 0; i < 2; i++) L.btn[i] = cw_rect_make(W * 0.5f - bw * 0.5f, y + i * (bh + g), bw, bh);
  return L;
}
static bool in_rect(cw_vec2 m, cw_rect r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }

/* world -> canvas through the 3D camera */
static bool project(t3_vec3 p, float W, float H, cw_vec2 *out) {
  cw_node cam = cw_render3d_camera(cw_scene_app_tree());
  if (!cw_node_valid(cam)) return false;
  t3_camera *c = (t3_camera *)cw_node3d_object(cam);
  if (!c) return false;
  t3_vec3 v = t3_vec3_apply_mat4(p, &c->matrix_world_inverse);
  if (v.z > -0.1f) return false;
  t3_vec3 n = t3_vec3_apply_mat4(v, &c->projection_matrix);
  *out = cw_v2((n.x * 0.5f + 0.5f) * W, (0.5f - n.y * 0.5f) * H);
  return true;
}
/* ── the fixed tick ─────────────────────────────────────────────────── */

static void add_pop(int x, int y, int kind, int who) {
  if (G.npops == 16) memmove(G.pops, G.pops + 1, sizeof G.pops[0] * 15), G.npops--;
  G.pops[G.npops].x = x, G.pops[G.npops].y = y, G.pops[G.npops].kind = kind, G.pops[G.npops].who = who, G.pops[G.npops].age = 0;
  G.npops++;
}

static void sim_sounds(void) {
  for (int i = 0; i < G.S.ev_n; i++) {
    const struct sim_event *e = &G.S.ev[i];
    if (e->kind == EV_PICK) add_pop(e->x, e->y, e->b, e->a);
    if (e->kind == EV_KO) add_pop(e->x, e->y, -1, e->a);
    if (e->kind == EV_BUBBLE) add_pop(e->x, e->y, -2, e->a);
    switch (e->kind) {
    case EV_DROP: sfx_at(SFX_FUSE, e->x, e->y, 0.6f); break;
    case EV_BOOM: sfx_at(e->b >= 4 ? SFX_BOOM_BIG : SFX_BOOM, e->x, e->y, 1); break;
    case EV_CRATE: sfx_at(SFX_CRATE, e->x, e->y, 0.7f); break;
    case EV_PICK: sfx_at(SFX_PICK, e->x, e->y, 0.8f); break;
    case EV_BUBBLE: sfx_at(SFX_POP, e->x, e->y, 1); break;
    case EV_KO: sfx_at(SFX_KO, e->x, e->y, 1); break;
    case EV_KICK: sfx_at(SFX_KICK, e->x, e->y, 0.8f); break;
    case EV_TELE: if (e->b >= 0) sfx_at(SFX_TELE, e->x, e->y, 0.8f); break;
    case EV_VENT: sfx_at(SFX_VENT, e->x, e->y, 0.9f); break;
    case EV_CANNON: sfx_at(SFX_CANNON, e->x, e->y, 1); break;
    case EV_SD_WALL: sfx_at(SFX_THUD, e->x, e->y, 0.5f); break;
    default: break;
    }
  }
}

static void finish_round(void) {
  G.round_winner = G.S.winner;
  if (G.round_winner >= 0) G.wins[G.round_winner]++;
  G.state = GS_ROUND_END;
  G.state_ticks = 0;
  sfx(SFX_ROUND_WIN, 1);
}

static void end_match(void) {
  G.state = GS_PODIUM;
  G.state_ticks = 0;
  int best = -1;
  for (int i = 0; i < MAXP; i++)
    if (G.slot[i] != SLOT_OFF && (best < 0 || G.wins[i] > G.wins[best])) best = i;
  G.match_winner = best;
  if (G.mode == MODE_ADVENTURE && best == 0 && G.arena >= 1) {
    int i = G.arena - 1;
    bool first = !SAVE.cleared[i];
    SAVE.cleared[i] = 1;
    int secs = G.S.tick / 60;
    if (!SAVE.best_secs[i] || secs < SAVE.best_secs[i]) SAVE.best_secs[i] = secs;
    if (first && i + 1 < ARENAS) SAVE.shown[i + 1] = 0;
    save_write();
  }
  world_podium(&G.S, G.wins, G.match_winner);
  sfx(SFX_MATCH_WIN, 1);
}

static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  g_rendering = false; /* prompts are measured in render only */
  G.ticks++;
  G.state_ticks++;
  read_mouse();
  touch_layout();
  for (int i = 0; i < SFX_COUNT; i++)
    if (A.cooldown[i] > 0) A.cooldown[i]--;
  cw_vec2 cs = cw_r2d_canvas_size(), m = mouse_canvas();
  bool confirm = m_confirm(), back = m_back();
  int dx = m_dx(), dy = m_dy();
  switch (G.state) {
  case GS_TITLE: {
    /* QUICK BATTLE (you and three CPUs on Clover Commons) is one press away */
    if (dy || dx) {
      G.menu = (G.menu + dy + dx + 3) % 3;
      sfx(SFX_MOVE, 0.5f);
    }
    int hover = -1;
    for (int i = 0; i < 3; i++)
      if (in_rect(m, title_button(i, cs.x, cs.y))) hover = i;
    if (POINTER && hover >= 0) G.menu = hover;
    if (G.state_ticks > 20 && (confirm || (mouse_clicked && hover >= 0))) {
      sfx(SFX_SELECT, 1);
      if (G.menu == 0) {
        G.slot[0] = SLOT_HUMAN;
        for (int i = 1; i < MAXP; i++) G.slot[i] = SLOT_CPU_NORMAL;
        G.mode = MODE_BATTLE;
        int keep = SAVE.wins_needed;
        SAVE.wins_needed = 3;
        begin_match(MODE_BATTLE, 1);
        SAVE.wins_needed = (uint8_t)keep;
        break;
      }
      G.mode = G.menu == 1 ? MODE_BATTLE : MODE_ADVENTURE;
      if (G.mode == MODE_BATTLE) {
        G.state = GS_PLAYERS;
        G.cursor = MAXP; /* on FIGHT: one more press starts */
      } else {
        G.cursor = 0;
        for (int i = 0; i < ARENAS; i++)
          if (stage_open(i) && !SAVE.cleared[i]) {
            G.cursor = i;
            break;
          }
        enter_arena_select();
      }
      G.state_ticks = 0;
    }
    break;
  }
  case GS_PLAYERS: {
    /* Confirm (Enter / Space / A, or a click on FIGHT) always means go on.
     * The rows: P1-P4, then FIGHT. On P2-P4, left / right picks who plays it
     * (CPU Normal, Hard, off, a pad player, the arrows, CPU Easy); on P1
     * (always you) left / right picks the critter; X / E (pad X / Y) picks a
     * critter on any row; on FIGHT, left / right sets the rounds to win.
     * Pads 2-4 press A to join; Esc / B goes back. */
    int rows = MAXP + 1;
    if (dy) {
      G.cursor = (G.cursor + dy + rows) % rows;
      sfx(SFX_MOVE, 0.5f);
    }
    int crit_step = 0;
    if (G.cursor == 0 && dx) crit_step = dx;
    if (G.cursor < MAXP && cw_action_just_pressed(0, G.a_pick)) crit_step = 1;
    if (dx && G.cursor > 0 && G.cursor < MAXP) {
      static const int ORDER[] = { SLOT_CPU_NORMAL, SLOT_CPU_HARD, SLOT_OFF, SLOT_HUMAN, SLOT_KEYS2, SLOT_CPU_EASY };
      int at = 0, n = (int)CW_ARRAY_LEN(ORDER);
      for (int k = 0; k < n; k++)
        if (ORDER[k] == G.slot[G.cursor]) at = k;
      int next;
      do {
        at = (at + dx + n) % n;
        next = ORDER[at];
      } while (next == SLOT_KEYS2 && keys2_slot() >= 0 && keys2_slot() != G.cursor); /* one arrows player */
      G.slot[G.cursor] = next;
      sfx(SFX_MOVE, 0.6f);
    }
    if (dx && G.cursor == MAXP) {
      G.wins_needed = CW_CLAMP(G.wins_needed + dx, 1, 5);
      sfx(SFX_MOVE, 0.6f);
    }
    /* another pad pressing south joins its own slot as a player */
    bool joined = false;
    for (int p = 1; p < MAXP; p++)
      if (cw_action_just_pressed(p, G.a_bomb)) {
        joined = true;
        if (G.slot[p] != SLOT_HUMAN) {
          G.slot[p] = SLOT_HUMAN;
          sfx(SFX_SELECT, 0.8f);
        }
      }
    int hover = -1;
    for (int i = 0; i < MAXP; i++)
      if (in_rect(m, slot_rect(i, cs.x, cs.y))) hover = i;
    if (in_rect(m, wins_rect(cs.x, cs.y))) hover = MAXP;
    if (POINTER && hover >= 0 && mouse_moved) G.cursor = hover;
    if ((mouse_clicked || mouse_right_clicked) && hover >= 0 && hover < MAXP) { /* a click on a card: the next critter / who plays it */
      if (hover == 0 || mouse_right_clicked) G.crit[hover] = (G.crit[hover] + 1) % CRIT_KINDS, crit_step = 0, G.cursor = hover;
      else {
        static const int NEXT[SLOT_KINDS] = { [SLOT_OFF] = SLOT_HUMAN, [SLOT_HUMAN] = SLOT_KEYS2, [SLOT_KEYS2] = SLOT_CPU_EASY,
                                              [SLOT_CPU_EASY] = SLOT_CPU_NORMAL, [SLOT_CPU_NORMAL] = SLOT_CPU_HARD, [SLOT_CPU_HARD] = SLOT_OFF };
        int n = NEXT[G.slot[hover]];
        if (n == SLOT_KEYS2 && keys2_slot() >= 0) n = SLOT_CPU_EASY;
        G.slot[hover] = n;
      }
      sfx(SFX_MOVE, 0.7f);
    }
    if (crit_step) {
      int row = G.cursor;
      G.crit[row] = (G.crit[row] + crit_step + CRIT_KINDS) % CRIT_KINDS;
      sfx(SFX_MOVE, 0.6f);
      int keep = G.cursor, st = G.state_ticks;
      enter_title(); /* the row of critters rebuilt with the new pick */
      G.state = GS_PLAYERS;
      G.cursor = keep;
      G.state_ticks = st;
    }
    G.slot[0] = SLOT_HUMAN; /* player 1 is always you */
    int on = 0;
    for (int i = 0; i < MAXP; i++) on += G.slot[i] != SLOT_OFF;
    bool go = G.state_ticks > 10 && !joined && (confirm || (mouse_clicked && hover == MAXP));
    if (go && on >= 2) {
      for (int i = 0; i < MAXP; i++) SAVE.slot[i] = (uint8_t)G.slot[i], SAVE.crit[i] = (uint8_t)G.crit[i];
      SAVE.wins_needed = (uint8_t)G.wins_needed;
      save_write();
      sfx(SFX_SELECT, 1);
      G.cursor = 0;
      enter_arena_select();
    } else if (go) sfx(SFX_LOCKED, 1);
    if (back && G.state_ticks > 5) {
      G.state = GS_TITLE;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_ARENA: {
    if (dx || dy) {
      int c = G.cursor + dx + dy * 4;
      if (c >= 0 && c < ARENAS) {
        G.cursor = c;
        sfx(SFX_MOVE, 0.5f);
      }
    }
    int hover = -1;
    for (int i = 0; i < ARENAS; i++)
      if (in_rect(m, tile_rect(i, cs.x, cs.y))) hover = i;
    if (POINTER && hover >= 0 && mouse_moved) G.cursor = hover;
    if (G.bump_ticks > 0) G.bump_ticks--;
    if (G.unlock_tile >= 0 && ++G.unlock_ticks == 24) sfx(SFX_UNLOCK, 1);
    bool pick = G.state_ticks > 10 && (confirm || (mouse_clicked && hover >= 0));
    if (pick && mouse_clicked && hover >= 0) G.cursor = hover;
    if (pick && !stage_open(G.cursor)) {
      G.bump_tile = G.cursor;
      G.bump_ticks = 18;
      sfx(SFX_LOCKED, 1);
    } else if (pick) {
      sfx(SFX_SELECT, 1);
      begin_match(G.mode, G.cursor + 1);
    } else if (back && G.state_ticks > 5) {
      G.state = G.mode == MODE_BATTLE ? GS_PLAYERS : GS_TITLE;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_INTRO:
    if (G.state_ticks == 40) sfx(SFX_READY, 1);
    if (G.state_ticks == 100) sfx(SFX_GO, 1);
    if (G.state_ticks >= 110) {
      G.state = GS_PLAY;
      G.state_ticks = 0;
    }
    break;
  case GS_PLAY: {
    uint8_t in[MAXP] = { 0 };
    for (int i = 0; i < MAXP; i++) {
      if (G.slot[i] == SLOT_HUMAN || G.slot[i] == SLOT_KEYS2) in[i] = human_input(i);
      else if (G.slot[i] >= SLOT_CPU_EASY) in[i] = ai_think(&G.S, i, G.slot[i] - SLOT_CPU_EASY, &G.ai[i]);
    }
    if (G.S.tick == G.boom_tick) sim_force_bomb(&G.S, 7, 5, 4);
    bool before = G.S.tick < G.S.round_ticks;
    sim_step(&G.S, in);
    memcpy(G.in_dbg, in, sizeof G.in_dbg);
    if (G.rec) fprintf(G.rec, "%d %d %d %d %d\n", G.S.tick, in[0], in[1], in[2], in[3]), fflush(G.rec);
    if (before && G.S.tick >= G.S.round_ticks) sfx(SFX_ALARM, 1);
    fx_events(&G.S);
    sim_sounds();
    if (G.S.winner != -2 && G.S.tick - G.S.over_tick > 60) finish_round();
    else if (any_pressed(G.a_pause, -1)) {
      G.state = GS_PAUSE;
      G.menu = 0;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_PAUSE:
    if (cw_quality_menu_is_open()) break; /* the graphics page has the presses */
  {
    /* RESUME and QUIT side by side at the bottom, the two text rows above
     * them: up and down walk the rows, left and right pick a button or
     * change a row; a press or a click on a row changes it too */
    pause_layout pl = pause_layout_of(cs.x, cs.y);
    bool text_row = G.menu == PM_TEXT_SIZE || G.menu == PM_TEXT_BG;
    int change = 0;
    if (dy < 0) G.menu = G.menu == PM_TEXT_BG ? PM_TEXT_SIZE : G.menu == PM_TEXT_SIZE ? PM_RESUME : PM_TEXT_BG, sfx(SFX_MOVE, 0.5f);
    else if (dy > 0) G.menu = G.menu == PM_TEXT_SIZE ? PM_TEXT_BG : G.menu == PM_TEXT_BG ? PM_RESUME : PM_TEXT_SIZE, sfx(SFX_MOVE, 0.5f);
    else if (dx && text_row) change = dx;
    else if (dx) G.menu = G.menu == PM_RESUME ? PM_QUIT : PM_RESUME, sfx(SFX_MOVE, 0.5f);
    for (int i = 0; i < 2; i++) /* a click or a tap on RESUME / QUIT */
      if (mouse_clicked && in_rect(m, pl.btn[i])) G.menu = i, confirm = true;
    for (int k = 0; k < 2; k++)
      if (mouse_clicked && in_rect(m, pl.row[k])) G.menu = PM_TEXT_SIZE + k, change = 1, confirm = false;
    if (confirm && text_row) change = 1, confirm = false;
    if (change && G.state_ticks > 5) {
      if (G.menu == PM_TEXT_SIZE) change > 0 ? cw_text_scale_cycle() : cw_text_scale_cycle_back();
      else for (int k = 0; k < (change > 0 ? 1 : 2); k++) cw_text_backdrop_cycle(); /* three settings: twice is back one */
      sfx(SFX_SELECT, 0.6f);
      save_write();
    }
    if (G.state_ticks > 5 && (any_pressed(G.a_pause, -1) || back || (confirm && G.menu == PM_RESUME))) G.state = GS_PLAY;
    else if (G.state_ticks > 5 && confirm && G.menu == PM_QUIT) enter_title();
  }
    break;
  case GS_ROUND_END:
    if (G.state_ticks > 220 || (G.state_ticks > 80 && confirm)) {
      bool over = false;
      for (int i = 0; i < MAXP; i++) over |= G.wins[i] >= G.wins_needed;
      if (over) end_match();
      else {
        G.round++;
        start_round();
      }
    }
    break;
  case GS_PODIUM:
    if (G.state_ticks > 120 && (confirm || mouse_clicked)) {
      bool adv = G.mode == MODE_ADVENTURE, won = adv && G.match_winner == 0;
      int arena = G.arena;
      enter_title();
      if (adv) {
        G.cursor = won && arena < ARENAS ? arena : arena - 1;
        enter_arena_select();
      } else {
        G.state = GS_ARENA;
        G.state_ticks = 0;
      }
    }
    break;
  }
  if (pending_arena > 0) {
    int a = pending_arena;
    pending_arena = -1;
    begin_match(MODE_BATTLE, a);
  }
  /* the world animates in every state but the pause (a frozen sim: idle critters) */
  if (G.state != GS_PAUSE) {
    for (int i = 0; i < G.npops; i++) G.pops[i].age++;
    while (G.npops > 0 && G.pops[0].age > 80) memmove(G.pops, G.pops + 1, sizeof G.pops[0] * (size_t)(--G.npops));
    if (G.state != GS_PLAY) G.S.ev_n = 0;
    uint8_t humans = 0;
    for (int i = 0; i < MAXP; i++) humans |= (uint8_t)((G.slot[i] == SLOT_HUMAN || G.slot[i] == SLOT_KEYS2) << i);
    world_set_humans(G.state == GS_TITLE || G.state == GS_PLAYERS || G.state == GS_ARENA ? 0 : humans);
    world_tick(&G.S);
    fx_tick(&G.S);
    scenery_tick(&G.S, G.ticks);
  }
  if (G.state == GS_TITLE || G.state == GS_PLAYERS || G.state == GS_ARENA) world_title_pose(G.ticks);
  camera_tick();
  t3_scene *sc = cw_render3d_scene(cw_scene_app_tree());
  if (sc) sc->environment_intensity = 0.85f;
  save_fields();
  G.alive = sim_alive(&G.S);
  G.winner = G.S.winner;
  G.hash = G.S.hash;
  G.timer = G.S.round_ticks > G.S.tick ? (G.S.round_ticks - G.S.tick + 59) / 60 : 0;
  G.picked = 0;
  for (int i = 0; i < MAXP; i++) {
    G.kos[i] = G.S.p[i].kos, G.placed[i] = G.S.p[i].placed;
    G.pos[i * 2] = G.S.p[i].x, G.pos[i * 2 + 1] = G.S.p[i].y;
    G.slots[i] = G.slot[i];
    G.picked += G.S.p[i].picked;
  }
  G.chains = G.S.chains;
  int32_t ints[] = { G.state, G.mode, G.arena, G.round, G.alive, G.winner, G.wins[0], G.wins[1], G.wins[2], G.wins[3] };
  for (size_t k = 0; k < sizeof ints / sizeof ints[0]; k++) G.state_hash = (G.state_hash ^ (uint32_t)ints[k]) * 16777619u;
  G.state_hash = (G.state_hash ^ G.hash) * 16777619u;
}

static void gfx_input(void);
static void update(void *u, double dt) {
  read_device();
  (void)u;
  (void)dt;
  gfx_input();
  cw_quality_auto_measure(G.state == GS_PLAY); /* the preset steps down when play runs slow */
  if (!A.on) return;
  cw_node cam = cw_render3d_camera(cw_scene_app_tree());
  if (cw_node_valid(cam)) {
    const t3_mat4 *mm = cw_node3d_global_transform(cam);
    t3_vec3 p = { mm->e[12], mm->e[13], mm->e[14] }, f = { -mm->e[8], -mm->e[9], -mm->e[10] }, up = { mm->e[4], mm->e[5], mm->e[6] };
    cw_audio_set_listener(p, (t3_vec3){ 0, 0, 0 }, f, up);
  }
  cw_audio_update();
}

/* ── HUD (render2d over the 3D frame; render-only) ─────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

/* the outline alpha: the 12 offset stamps these outlines started as
 * overlapped about twice over each spot, so they read denser than one */
static float line_alpha(float a) { return 1 - (1 - a) * (1 - a); }
/* text with a soft dark outline (no hard drop shadow: thin strokes stay
 * readable anywhere) */
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_font f = size < 20 ? font_ui_bold : font_ui;
  bool light = c.r * 0.3f + c.g * 0.6f + c.b * 0.1f > 0.5f;
  cw_color line = light ? rgba(0.08f, 0.05f, 0.12f, line_alpha(0.85f * c.a)) : rgba(1, 0.98f, 0.95f, line_alpha(0.7f * c.a));
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap, .outline_color = line,
                       .outline_width = light ? size * 0.09f + 0.6f : size * 0.05f + 0.4f };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}
/* text on a panel of the game's own: no outline, the Text background adds nothing */
static void label(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap, .own_backdrop = true };
  cw_r2d_text(size < 20 ? font_ui_bold : font_ui, s, cw_v2(x, y), &st);
}
/* display text: Titan One, a thick outline and a soft under-glow */
static void bold(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap,
                       .outline_width = size * 0.065f, .outline_color = rgba(0.12f, 0.05f, 0.14f, c.a),
                       .shadow_color = rgba(0.07f, 0.03f, 0.1f, line_alpha(0.5f * c.a)), .shadow_offset = cw_v2(0, size * 0.04f),
                       .shadow_spread = size * 0.11f };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}
static void round_rect(cw_rect r, float rad, cw_color c) { cw_r2d_round_rect(r, rad, 8, c); }
static void panel(cw_rect r, float alpha, cw_color border, cw_color fill) {
  float rad = fminf(r.h * 0.25f, 20);
  round_rect(cw_rect_make(r.x + 3, r.y + 6, r.w, r.h), rad, rgba(0.05f, 0.02f, 0.08f, 0.3f * alpha));
  border.a *= alpha;
  fill.a *= alpha;
  round_rect(cw_rect_make(r.x - 3, r.y - 3, r.w + 6, r.h + 6), rad + 3, border);
  round_rect(r, rad, fill);
}
static void glass(cw_rect r, float alpha) { panel(r, alpha, rgba(1, 1, 1, 0.55f), rgba(0.1f, 0.07f, 0.18f, 0.62f)); }
static void chip(const char *s, float cx, float y, float size, float alpha) {
  cw_text_style st = { .size = size };
  cw_text_metrics mm = cw_font_measure(size < 20 ? font_ui_bold : font_ui, s, &st);
  float padx = size * 0.8f, pady = size * 0.3f;
  round_rect(cw_rect_make(cx - mm.width * 0.5f - padx, y - pady, mm.width + 2 * padx, mm.height + 2 * pady), size * 0.7f,
             rgba(0.08f, 0.04f, 0.12f, 0.55f * alpha));
}
/* a centred line on a dark chip, with button prompts in it ({bomb}, {left|right}:
 * cartwheel/prompt.h) drawn for P1's device, broken into lines to fit the
 * canvas; alpha fades both. Returns the chip's height */
static float prompt_line_a(const char *s, float W, float y, float size, float alpha) {
  cw_font f = ui_font(size);
  float drawn = cw_text_drawn_size(f, size), padx = drawn * 0.8f, pady = drawn * 0.3f;
  s = prompt_wrap(s, size, W - 2 * padx - W * 0.04f);
  cw_vec2 m = psize(f, s, 0, size);
  round_rect(cw_rect_make(W * 0.5f - m.x * 0.5f - padx, y - pady, m.x + 2 * padx, m.y + 2 * pady), drawn * 0.7f,
             rgba(0.08f, 0.04f, 0.12f, 0.55f * alpha));
  cw_prompt_text(f, s, 0, cw_v2(W * 0.5f, y), size, rgba(1, 1, 1, alpha), CW_ALIGN_CENTER);
  return m.y + 2 * pady;
}
static float prompt_line(const char *s, float W, float y, float size) { return prompt_line_a(s, W, y, size, 1); }
/* a prompt line whose chip ends at `bottom` (the canvas edge less a margin) */
static void prompt_line_above(const char *s, float W, float bottom, float size, float alpha) {
  float pady = cw_text_drawn_size(ui_font(size), size) * 0.3f;
  prompt_line_a(s, W, bottom - prompt_line_h(s, W, size) + pady, size, alpha);
}

static void star(float x, float y, float r, cw_color fill, float rot) {
  cw_vec2 p[10];
  for (int i = 0; i < 10; i++) {
    float a = rot - 1.5707963f + i * 0.6283185f, rr = i & 1 ? r * 0.45f : r, sn, cs;
    cw_sincos(a, &sn, &cs);
    p[i] = cw_v2(x + rr * cs, y + rr * sn);
  }
  cw_r2d_fan(cw_v2(x, y), p, 10, true, fill);
  cw_r2d_polygon_lines(p, 10, r * 0.1f + 1, rgba(0.25f, 0.12f, 0.02f, fill.a));
}

static void padlock(float x, float y, float sz, float open, float alpha) {
  cw_vec2 arc[13];
  float lift = open * sz * 0.45f;
  for (int i = 0; i <= 12; i++) {
    float a = 3.1415927f * i / 12;
    arc[i] = cw_v2(x + sz * 0.3f * cw_cos(a), y - sz * 0.15f - lift - sz * 0.32f * cw_sin(a));
  }
  cw_color steel = rgba(0.75f, 0.75f, 0.8f, alpha), dark = rgba(0.15f, 0.1f, 0.2f, alpha);
  cw_r2d_polyline(arc, 13, sz * 0.13f + 1.5f, false, dark);
  cw_r2d_polyline(arc, 13, sz * 0.13f, false, steel);
  cw_r2d_line(arc[0], cw_v2(arc[0].x, y + sz * 0.05f - lift), sz * 0.13f, steel);
  cw_r2d_line(arc[12], cw_v2(arc[12].x, y + sz * 0.05f), sz * 0.13f, steel);
  cw_rect body = cw_rect_make(x - sz * 0.42f, y - sz * 0.12f, sz * 0.84f, sz * 0.66f);
  round_rect(cw_rect_make(body.x - 1.5f, body.y - 1.5f, body.w + 3, body.h + 3), sz * 0.14f, dark);
  round_rect(body, sz * 0.12f, rgba(1, 0.75f, 0.25f, alpha));
  cw_vec2 c = cw_v2(body.x + body.w * 0.5f, body.y + body.h * 0.45f);
  cw_r2d_circle(c, sz * 0.08f, 12, dark);
  cw_r2d_rect(cw_rect_make(c.x - sz * 0.035f, c.y, sz * 0.07f, sz * 0.17f), dark);
}

/* a critter's face, flat (portraits) */
static const float FUR2D[CRIT_KINDS][3] = { { 0.95f, 0.5f, 0.2f }, { 0.97f, 0.95f, 0.93f }, { 0.6f, 0.4f, 0.27f }, { 0.45f, 0.8f, 0.35f } };
static void face(float x, float y, float r, int kind, int team, bool ko, float a) {
  cw_color fur = rgba(FUR2D[kind][0], FUR2D[kind][1], FUR2D[kind][2], a), dark = rgba(0.1f, 0.05f, 0.08f, a);
  cw_color tc = rgba(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2], a);
  cw_r2d_circle(cw_v2(x, y), r * 1.18f, 28, tc);
  cw_r2d_circle(cw_v2(x, y), r * 1.06f, 28, rgba(0.1f, 0.06f, 0.14f, a));
  for (int s = -1; s <= 1; s += 2) {
    if (kind == CRIT_FOX) {
      cw_vec2 t[3] = { { x + s * r * 0.85f, y - r * 0.1f }, { x + s * r * 0.75f, y - r * 1.15f }, { x + s * r * 0.2f, y - r * 0.7f } };
      cw_r2d_polygon(t, 3, fur);
    } else if (kind == CRIT_BUN) {
      cw_r2d_circle(cw_v2(x + s * r * 0.38f, y - r * 1.0f), r * 0.24f, 14, fur);
      cw_r2d_rect(cw_rect_make(x + s * r * 0.38f - r * 0.24f, y - r * 1.0f, r * 0.48f, r * 0.6f), fur);
    } else if (kind == CRIT_BEAR) cw_r2d_circle(cw_v2(x + s * r * 0.65f, y - r * 0.68f), r * 0.3f, 14, fur);
    else cw_r2d_circle(cw_v2(x + s * r * 0.4f, y - r * 0.62f), r * 0.32f, 14, fur);
  }
  cw_r2d_circle(cw_v2(x, y + r * 0.08f), r * 0.88f, 28, fur);
  if (kind != CRIT_FROG) cw_r2d_circle(cw_v2(x, y + r * 0.38f), r * 0.38f, 20, rgba(1, 0.95f, 0.88f, a));
  for (int s = -1; s <= 1; s += 2) {
    float ex = x + s * r * 0.36f, ey = y - (kind == CRIT_FROG ? r * 0.55f : r * 0.05f);
    if (ko) {
      cw_r2d_line(cw_v2(ex - r * 0.14f, ey - r * 0.14f), cw_v2(ex + r * 0.14f, ey + r * 0.14f), r * 0.08f + 1, dark);
      cw_r2d_line(cw_v2(ex - r * 0.14f, ey + r * 0.14f), cw_v2(ex + r * 0.14f, ey - r * 0.14f), r * 0.08f + 1, dark);
    } else {
      cw_r2d_circle(cw_v2(ex, ey), r * 0.2f, 16, rgba(1, 1, 1, a));
      cw_r2d_circle(cw_v2(ex, ey + r * 0.03f), r * 0.12f, 14, dark);
      cw_r2d_circle(cw_v2(ex + r * 0.05f, ey - r * 0.05f), r * 0.04f, 8, rgba(1, 1, 1, a));
    }
    cw_r2d_circle(cw_v2(x + s * r * 0.58f, y + r * 0.3f), r * 0.11f, 10, rgba(1, 0.5f, 0.55f, 0.6f * a));
  }
  cw_r2d_circle(cw_v2(x, y + r * 0.25f), r * 0.08f, 10, dark);
}

static const char *menu_prompt(void) {
  switch (G.state) {
  case GS_TITLE: return GLYPHS ? "{left|right|up|down} choose   {bomb} go" : ON_TOUCH ? "tap to choose" : "click to choose";
  case GS_PLAYERS:
    return ON_TOUCH ? "tap a card: who plays it (P1: the critter)   tap FIGHT to go"
         : !GLYPHS  ? "click a card: who plays it   right click: critter   click FIGHT to go"
         : p1_device == CW_PROMPT_PAD
           ? "{up|down} a player   {left|right} CPU / pad / off   {pick} critter   {bomb} go   {cancel} back   pads 2-4: {bomb} joins"
           : "{up|down} a player   {left|right} CPU / player / off   {pick} critter   {bomb} go   {cancel} back";
  case GS_ARENA: return GLYPHS ? "{left|right|up|down} choose   {bomb} go   {cancel} back" : ON_TOUCH ? "tap an arena" : "click an arena";
  case GS_PODIUM: return GLYPHS ? "{bomb} to continue" : ON_TOUCH ? "tap to continue" : "click to continue";
  default: return "";
  }
}

/* the controls, shown on the title and in the pause menu */
static const char *controls_line(void) {
  if (ON_TOUCH) return "move: the stick (left thumb, anywhere)    BOMB and ACTION: the buttons (right thumb)";
  return "move: {left|right|up|down}    bomb: {bomb}    detonate / stop a kick: {act}    pause: {pause}";
}

static void glow_cursor(cw_rect r, float s, float rad, bool dim) {
  float g = 4 * s + 2 * s * cw_sin(G.ticks * 0.15f);
  round_rect(cw_rect_make(r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g), rad + g, dim ? rgba(0.7f, 0.7f, 0.75f, 0.9f) : rgba(1, 0.85f, 0.3f, 0.95f));
}

static void draw_title(float W, float H, float s) {
  title_layout L = title_layout_of(W, H);
  float bob = 5 * s * cw_sin(G.ticks * 0.05f);
  /* the logo (art: its own size, whatever the text size): each letter its own colour, bouncing in a wave */
  const char *logo = "BOMBERDUDE";
  static const float LC[10][3] = { { 1, 0.35f, 0.25f }, { 1, 0.6f, 0.15f }, { 1, 0.85f, 0.2f }, { 0.55f, 0.9f, 0.3f }, { 0.3f, 0.8f, 1 },
                                   { 0.6f, 0.5f, 1 }, { 1, 0.45f, 0.75f }, { 1, 0.35f, 0.25f }, { 1, 0.75f, 0.15f }, { 0.4f, 0.85f, 1 } };
  float size = L.logo_size;
  cw_text_style st = { .size = size, .fixed_size = true };
  float total = cw_font_measure(font_title, logo, &st).width, x = (W - total) * 0.5f;
  for (int i = 0; logo[i]; i++) {
    char ch[2] = { logo[i], 0 };
    float w = cw_font_measure(font_title, ch, &st).width;
    float jump = fabsf(cw_sin(G.ticks * 0.07f - i * 0.45f)) * 14 * s * size / (LOGO_SIZE * s);
    cw_color c = rgba(LC[i][0], LC[i][1], LC[i][2], 1);
    cw_text_style ls = { .size = size, .color = c, .fixed_size = true, .outline_width = size * 0.065f, .outline_color = rgba(0.12f, 0.05f, 0.14f, 1),
                         .shadow_color = rgba(0.07f, 0.03f, 0.1f, line_alpha(0.5f)), .shadow_offset = cw_v2(0, size * 0.04f), .shadow_spread = size * 0.11f };
    cw_r2d_text(font_title, ch, cw_v2(x, L.logo_y + bob - jump), &ls);
    x += w;
  }
  if (L.tag) prompt_line(TITLE_TAG, W, L.tag_y, 20 * s);
  for (int i = 0; i < 3; i++) {
    cw_rect r = L.btn[i];
    bool cur = G.menu == i;
    if (cur) glow_cursor(r, s, 20, false);
    panel(r, 1, rgba(1, 1, 1, 0.8f), cur ? rgba(0.95f, 0.35f, 0.25f, 0.95f) : rgba(0.15f, 0.1f, 0.25f, 0.85f));
    bold(TITLE_ITEMS[i], r.x, r.y + 4 * s, TITLE_ITEM * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
    label(TITLE_SUBS[i], r.x, r.y + L.sub_dy, TITLE_SUB * s, rgba(1, 0.92f, 0.8f, 0.95f), CW_ALIGN_CENTER, r.w);
  }
  if (L.controls) prompt_line(controls_line(), W, L.controls_y, 16 * s);
  prompt_line_above(menu_prompt(), W, L.prompt_bottom, 17 * s, 1);
}

static void draw_players(float W, float H, float s) {
  players_layout L = players_layout_of(W, H);
  if (L.title) bold("WHO'S PLAYING?", 0, L.title_y, 46 * s, rgba(1, 0.82f, 0.25f, 1), CW_ALIGN_CENTER, W);
  char buf[64];
  for (int i = 0; i < MAXP; i++) {
    cw_rect r = L.card[i];
    bool cur = G.cursor == i, off = G.slot[i] == SLOT_OFF;
    cw_color tc = rgba(TEAM_RGB[i][0], TEAM_RGB[i][1], TEAM_RGB[i][2], 1);
    if (cur) glow_cursor(r, s, 20, false);
    panel(r, off ? 0.6f : 1, tc, rgba(0.1f, 0.07f, 0.18f, 0.82f));
    snprintf(buf, sizeof buf, "P%d", i + 1);
    bold(buf, r.x + 12 * s, r.y + 6 * s, SLOT_P * s, tc, CW_ALIGN_LEFT, 0);
    face(r.x + r.w - L.face_r * 1.18f - 10 * s, r.y + 6 * s + L.face_r * 1.18f, L.face_r, G.crit[i], i, off, off ? 0.5f : 1);
    float y = r.y + 6 * s + L.top_h;
    label(CRIT_NAME[G.crit[i]], r.x + 12 * s, y, SLOT_NAME_SZ * s, rgba(1, 1, 1, off ? 0.5f : 1), CW_ALIGN_LEFT, 0);
    y += L.h_name + 4 * s;
    cw_color ac = rgba(1, 0.85f, 0.4f, cur ? 1 : 0.4f);
    float ay = y + L.h_who * 0.5f - measure("<", SLOT_WHO * s).height * 0.5f;
    label("<", r.x + 8 * s, ay, SLOT_WHO * s, ac, CW_ALIGN_LEFT, 0);
    label(">", r.x, ay, SLOT_WHO * s, ac, CW_ALIGN_RIGHT, r.w - 8 * s);
    cw_color sc = G.slot[i] == SLOT_HUMAN || G.slot[i] == SLOT_KEYS2 ? rgba(0.55f, 1, 0.6f, 1) : off ? rgba(0.65f, 0.65f, 0.7f, 1) : rgba(0.75f, 0.85f, 1, 1);
    float inner = r.w - 2 * L.arrow_w, wy = y + (L.h_who - measure_w(SLOT_NAME[G.slot[i]], SLOT_WHO * s, inner, CW_ALIGN_CENTER).height) * 0.5f;
    label(SLOT_NAME[G.slot[i]], r.x + L.arrow_w, wy, SLOT_WHO * s, sc, CW_ALIGN_CENTER, inner);
    y += L.h_who + 4 * s;
    label(slot_dev(i), r.x + 6 * s, y, SLOT_DEV * s, rgba(1, 0.95f, 0.85f, 0.85f), CW_ALIGN_CENTER, r.w - 12 * s);
  }
  cw_rect wr = L.wins;
  if (G.cursor == MAXP) glow_cursor(wr, s, 18, false);
  panel(wr, 1, rgba(1, 1, 1, 0.7f), rgba(0.9f, 0.3f, 0.25f, 0.92f));
  snprintf(buf, sizeof buf, "FIGHT!   first to %d", G.wins_needed);
  bold(buf, wr.x, wr.y + (wr.h - measure_bold(buf, 26 * s).height) * 0.5f, 26 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, wr.w);
  if (L.controls) prompt_line(controls_line(), W, L.controls_y, 16 * s); /* clear of the cards */
  prompt_line_above(menu_prompt(), W, L.prompt_bottom, 16 * s, 1);
}

static void draw_arena_select(float W, float H, float s) {
  arena_layout L = arena_layout_of(W, H);
  char buf[200];
  bool unlock_banner = G.unlock_tile >= 0 && G.unlock_ticks >= 24 && G.unlock_ticks < 110;
  if (!unlock_banner) bold(G.mode == MODE_ADVENTURE ? "ADVENTURE" : "PICK AN ARENA", 0, L.head_y, 50 * s, rgba(1, 0.82f, 0.25f, 1), CW_ALIGN_CENTER, W);
  for (int i = 0; i < ARENAS; i++) {
    cw_rect r = L.tile[i];
    bool cur = i == G.cursor;
    bool unlocking = i == G.unlock_tile && G.unlock_ticks < 80;
    float ut = unlocking ? (float)G.unlock_ticks : 999;
    bool locked = !stage_open(i) || (unlocking && ut < 24);
    if (i == G.bump_tile && G.bump_ticks > 0) r.x += cw_sin(G.bump_ticks * 1.7f) * 7 * s * G.bump_ticks / 18.0f;
    if (unlocking && ut >= 24) {
      float k = (ut - 24) / 56, pop = 1 + 0.14f * cw_sin(k * 3.1415927f) * (1 - k);
      r = cw_rect_make(r.x - r.w * (pop - 1) * 0.5f, r.y - r.h * (pop - 1) * 0.5f, r.w * pop, r.h * pop);
    }
    const float *tc = THEME_RGB[i];
    if (cur) glow_cursor(r, s, 20, locked);
    if (locked) panel(r, 1, rgba(0.4f, 0.4f, 0.45f, 1), rgba(0.18f, 0.17f, 0.22f, 0.92f));
    else panel(r, 1, rgba(tc[0], tc[1], tc[2], 1), rgba(tc[0] * 0.35f, tc[1] * 0.35f, tc[2] * 0.35f + 0.06f, 0.92f));
    snprintf(buf, sizeof buf, "%d", i + 1);
    bold(buf, r.x + 12 * s, r.y + 4 * s, TILE_NUM * s, locked ? rgba(0.55f, 0.55f, 0.6f, 1) : rgba(tc[0], tc[1], tc[2], 1), CW_ALIGN_LEFT, 0);
    if (L.titles) label(ARENA_TITLES[i], r.x + L.num_w, r.y + 10 * s, TILE_TITLE * s, rgba(1, 1, 1, locked ? 0.45f : 1), CW_ALIGN_LEFT, r.w - L.num_w - 6 * s);
    float lock_sz = fminf(30 * s, r.h * 0.4f), lock_y = r.y + r.h - lock_sz * 1.05f;
    if (locked) {
      padlock(r.x + r.w * 0.5f, lock_y, lock_sz, 0, 1);
      continue;
    }
    if (unlocking && ut < 70) {
      float k = (ut - 24) / 46;
      padlock(r.x + r.w * 0.5f + k * 30 * s, lock_y - k * k * 90 * s, lock_sz, fminf(1, k * 4), 1 - k);
      for (int p = 0; p < 8; p++) {
        float a = p * 0.785f + 0.3f, d = (10 + k * 70) * s;
        star(r.x + r.w * 0.5f + d * cw_cos(a), r.y + r.h * 0.5f + d * cw_sin(a), (6 - 4 * k) * s, rgba(1, 0.9f, 0.3f, 1 - k), k * 3);
      }
    }
    if (G.mode == MODE_ADVENTURE && !L.titles) { /* no room under the number: the best time beside it */
      if (SAVE.cleared[i]) {
        snprintf(buf, sizeof buf, "best %d:%02d", SAVE.best_secs[i] / 60, SAVE.best_secs[i] % 60);
        label(buf, r.x + L.num_w, r.y + 4 * s + (L.h_num - L.h_best) * 0.5f, TILE_BEST * s, rgba(1, 0.95f, 0.85f, 0.9f), CW_ALIGN_LEFT, 0);
      }
    } else if (G.mode == MODE_ADVENTURE) {
      float by = r.y + r.h - L.h_best - 8 * s;
      if (SAVE.cleared[i]) {
        float sr = fminf(15 * s, L.h_best * 0.6f);
        star(r.x + r.w - sr - 9 * s, by + L.h_best * 0.5f, sr, rgba(1, 0.82f, 0.15f, 1), 0);
        snprintf(buf, sizeof buf, "best %d:%02d", SAVE.best_secs[i] / 60, SAVE.best_secs[i] % 60);
        label(buf, r.x + 12 * s, by, TILE_BEST * s, rgba(1, 0.95f, 0.85f, 0.9f), CW_ALIGN_LEFT, 0);
      } else label("not beaten yet", r.x + 12 * s, by, TILE_BEST * s, rgba(1, 0.95f, 0.85f, 0.8f), CW_ALIGN_LEFT, 0);
    }
  }
  if (unlock_banner) { /* in the heading's place */
    snprintf(buf, sizeof buf, "STAGE %d UNLOCKED!", G.unlock_tile + 1);
    float a = cw_clampf((110 - G.unlock_ticks) / 20.0f, 0, 1);
    bold(buf, 0, L.head_y, 50 * s, rgba(1, 0.92f, 0.4f, a), CW_ALIGN_CENTER, W);
  }
  const char *b = stage_open(G.cursor) ? ARENA_BLURB[G.cursor] : G.mode == MODE_ADVENTURE ? "Beat the stage before it to open this one."
                                                                                           : "Beat it in Adventure to fight here.";
  if (!L.titles) { /* the tiles have no room for their titles: the cursor's names itself */
    snprintf(buf, sizeof buf, "%s:  %s", ARENA_TITLES[G.cursor], b);
    b = buf;
  }
  prompt_line_above(b, W, L.blurb_bottom, 19 * s, 1);
  prompt_line_above(menu_prompt(), W, L.prompt_bottom, 15 * s, 1);
}

/* the power-up icons on the player cards, as the pickups look: a bomb with a
 * lit fuse, a flame, a lightning bolt; sz is the line height they sit in */
static void icon_bomb(float x, float y, float sz, float al) {
  float r = sz * 0.3f, cx = x + sz * 0.42f, cy = y + sz * 0.58f;
  cw_r2d_circle(cw_v2(cx, cy), r + sz * 0.05f, 16, rgba(1, 1, 1, 0.5f * al));
  cw_r2d_circle(cw_v2(cx, cy), r, 16, rgba(0.1f, 0.1f, 0.13f, al));
  cw_r2d_circle(cw_v2(cx - r * 0.35f, cy - r * 0.35f), r * 0.25f, 8, rgba(0.6f, 0.6f, 0.7f, al));
  cw_r2d_line(cw_v2(cx + r * 0.5f, cy - r * 0.7f), cw_v2(cx + r * 0.9f, cy - r * 1.25f), sz * 0.07f, rgba(0.85f, 0.75f, 0.55f, al));
  cw_r2d_circle(cw_v2(cx + r * 0.95f, cy - r * 1.35f), sz * 0.08f, 8, rgba(1, 0.75f, 0.2f, al));
}
static void icon_fire(float x, float y, float sz, float al) {
  float cx = x + sz * 0.42f, b = y + sz * 0.9f;
  cw_vec2 o[6] = { { cx, y + sz * 0.1f }, { cx + sz * 0.3f, y + sz * 0.5f }, { cx + sz * 0.3f, b - sz * 0.15f }, { cx, b },
                   { cx - sz * 0.3f, b - sz * 0.15f }, { cx - sz * 0.25f, y + sz * 0.45f } };
  cw_r2d_polygon(o, 6, rgba(1, 0.45f, 0.1f, al));
  cw_vec2 in[4] = { { cx, y + sz * 0.42f }, { cx + sz * 0.15f, b - sz * 0.2f }, { cx, b - sz * 0.05f }, { cx - sz * 0.15f, b - sz * 0.2f } };
  cw_r2d_polygon(in, 4, rgba(1, 0.85f, 0.3f, al));
}
static void icon_speed(float x, float y, float sz, float al) {
  float cx = x + sz * 0.42f;
  cw_vec2 p[6] = { { cx + sz * 0.12f, y + sz * 0.08f }, { cx - sz * 0.26f, y + sz * 0.56f }, { cx - sz * 0.01f, y + sz * 0.56f },
                   { cx - sz * 0.12f, y + sz * 0.95f }, { cx + sz * 0.26f, y + sz * 0.42f }, { cx + sz * 0.02f, y + sz * 0.42f } };
  cw_r2d_polygon(p, 6, rgba(1, 0.88f, 0.25f, al));
  cw_r2d_polygon_lines(p, 6, sz * 0.05f, rgba(0.35f, 0.22f, 0.02f, al));
}

/* the player cards and the timer, laid out from the size their text draws
 * at: a card is as wide as its widest line, the cards sit in a row along the
 * top when the row fits beside the timer, else in a column down each side */
#define CARD_NAME 15
#define CARD_STAT 15
#define CARD_TAG 14
static const char *const TAG_NAME[4] = { "KICK", "REMOTE", "PIERCE", "BUBBLE" };
static int card_tags(const player *p, const char **out) {
  int n = 0;
  if (p->kick) out[n++] = TAG_NAME[0];
  if (p->remote) out[n++] = TAG_NAME[1];
  if (p->pierce) out[n++] = TAG_NAME[2];
  if (p->bubble) out[n++] = TAG_NAME[3];
  return n;
}
static void card_name(int i, char *buf, size_t cap) {
  bool person = G.slot[i] == SLOT_HUMAN || G.slot[i] == SLOT_KEYS2;
  if (person) snprintf(buf, cap, "P%d  %s", i + 1, CRIT_NAME[G.crit[i]]);
  else snprintf(buf, cap, "CPU  %s", CRIT_NAME[G.crit[i]]);
}
typedef struct {
  float w, h, face, pad, h_name, h_stat, h_tag, star, content;
  cw_rect timer;
  float round_h, bottom;
  bool columns;
  cw_rect card[MAXP];
} hud_layout;
/* the stats line: an icon then its number, three times; returns its width */
static float stat_line(const player *p, float x, float y, float s, float al, bool draw) {
  char nb[8];
  int v[3] = { p->bombs, p->range, (p->speed - 14) / 3 + 1 };
  float sz = cw_text_drawn_size(ui_font(CARD_STAT * s), CARD_STAT * s) * 1.15f, x0 = x;
  for (int k = 0; k < 3; k++) {
    if (draw) (k == 0 ? icon_bomb : k == 1 ? icon_fire : icon_speed)(x, y, sz, al);
    x += sz * 0.9f;
    snprintf(nb, sizeof nb, "%d", v[k]);
    if (draw) text(nb, x, y, CARD_STAT * s, rgba(1, 0.92f, 0.75f, al), CW_ALIGN_LEFT, 0);
    x += measure(nb, CARD_STAT * s).width + (k < 2 ? sz * 0.55f : 0);
  }
  return x - x0;
}
static hud_layout hud_layout_of(float W, float H) {
  float s = H / 540.0f;
  hud_layout L = { 0 };
  char buf[64];
  L.pad = 6 * s;
  L.h_name = measure("P1 Mg", CARD_NAME * s).height;
  L.h_stat = cw_text_drawn_size(ui_font(CARD_STAT * s), CARD_STAT * s) * 1.25f;
  L.h_tag = measure("KICK", CARD_TAG * s).height + 2 * s;
  L.star = L.h_name * 0.36f;
  L.face = fmaxf(18 * s, (L.h_name + L.h_stat) * 0.42f);
  int total = 0, most_tags = 0;
  float content = 0;
  for (int i = 0; i < MAXP; i++) {
    if (G.slot[i] == SLOT_OFF) continue;
    total++;
    card_name(i, buf, sizeof buf);
    content = fmaxf(content, measure(buf, CARD_NAME * s).width + 6 * s + G.wins_needed * L.star * 2.3f);
    content = fmaxf(content, stat_line(&G.S.p[i], 0, 0, s, 1, false));
    const char *tags[4];
    most_tags = (int)fmaxf((float)most_tags, (float)card_tags(&G.S.p[i], tags));
  }
  /* room for the widest tag beside one other, so a card does not jump in width when one arrives */
  content = fmaxf(content, measure("REMOTE", CARD_TAG * s).width + measure("BUBBLE", CARD_TAG * s).width + 4 * L.h_tag * 0.3f + 3 * s);
  L.content = content;
  float face_w = 2 * L.face * 1.18f + 10 * s;
  L.w = face_w + content + 2 * L.pad;
  /* tags flow into rows inside the content width */
  int tag_rows = 0;
  for (int i = 0; i < MAXP; i++) {
    if (G.slot[i] == SLOT_OFF) continue;
    const char *tags[4];
    int n = card_tags(&G.S.p[i], tags), rows = n ? 1 : 0;
    float x = 0;
    for (int k = 0; k < n; k++) {
      float w = measure(tags[k], CARD_TAG * s).width + L.h_tag * 0.6f;
      if (x > 0 && x + w > content) rows++, x = 0;
      x += w + 3 * s;
    }
    tag_rows = rows > tag_rows ? rows : tag_rows;
  }
  (void)most_tags;
  L.h = fmaxf(58 * s, L.pad + L.h_name + L.h_stat + (tag_rows ? tag_rows * (L.h_tag + 2 * s) : 0) + L.pad);
  /* the timer: its digits measured, the round under it */
  cw_text_metrics tm = measure_bold("0:00", 32 * s);
  float tw = fmaxf(116 * s, tm.width + 36 * s), th = fmaxf(50 * s, tm.height + 10 * s);
  L.timer = cw_rect_make(W * 0.5f - tw * 0.5f, 8 * s, tw, th);
  L.round_h = measure("ROUND 9", 14 * s).height;
  float side = W * 0.5f - tw * 0.5f - 12 * s - 10 * s; /* room each side of the timer */
  int left_n = (total + 1) / 2, right_n = total - left_n;
  L.columns = left_n * (L.w + 10 * s) - 10 * s > side;
  int shown = 0;
  float bottom = L.timer.y + L.timer.h + 4 * s + L.round_h;
  for (int i = 0; i < MAXP; i++) {
    if (G.slot[i] == SLOT_OFF) continue;
    bool left = shown < left_n;
    int k = left ? shown : shown - left_n;
    float x, y;
    if (!L.columns) {
      x = left ? 12 * s + k * (L.w + 10 * s) : W - 12 * s - (right_n - k) * (L.w + 10 * s) + 10 * s;
      y = 10 * s;
    } else {
      x = left ? 12 * s : W - 12 * s - L.w;
      y = 10 * s + k * (L.h + 10 * s);
    }
    L.card[i] = cw_rect_make(x, y, L.w, L.h);
    bottom = fmaxf(bottom, y + L.h);
    shown++;
  }
  L.bottom = bottom;
  return L;
}

static float hud_bottom(float W, float H) { return hud_layout_of(W, H).bottom; }

static void draw_hud(float W, float H, float s) {
  char buf[64];
  hud_layout L = hud_layout_of(W, H);
  for (int i = 0; i < MAXP; i++) {
    if (G.slot[i] == SLOT_OFF) continue;
    const player *p = &G.S.p[i];
    cw_rect r = L.card[i];
    cw_color tc = rgba(TEAM_RGB[i][0], TEAM_RGB[i][1], TEAM_RGB[i][2], 1);
    float al = p->alive ? 1 : 0.6f;
    panel(r, al, tc, rgba(0.08f, 0.06f, 0.14f, 0.72f));
    float fx = r.x + L.pad + L.face * 1.18f + 2 * s;
    face(fx, r.y + L.pad + (L.h_name + L.h_stat) * 0.5f + 2 * s, L.face, G.crit[i], i, !p->alive, al);
    float cx = fx + L.face * 1.18f + 8 * s, y = r.y + L.pad;
    bool person = G.slot[i] == SLOT_HUMAN || G.slot[i] == SLOT_KEYS2;
    card_name(i, buf, sizeof buf);
    text(buf, cx, y, CARD_NAME * s, person ? rgba(1, 1, 0.75f, al) : rgba(1, 1, 1, al), CW_ALIGN_LEFT, 0);
    for (int w = 0; w < G.wins_needed; w++)
      star(r.x + r.w - L.pad - L.star * 1.1f - w * L.star * 2.3f, y + L.h_name * 0.5f, L.star,
           w < G.wins[i] ? rgba(1, 0.82f, 0.15f, 1) : rgba(0.4f, 0.38f, 0.45f, 0.8f), 0);
    y += L.h_name;
    stat_line(p, cx, y, s, al, true);
    y += L.h_stat;
    const char *tags[4];
    int n = card_tags(p, tags);
    float ex = cx;
    for (int k = 0; k < n; k++) {
      float w = measure(tags[k], CARD_TAG * s).width + L.h_tag * 0.6f;
      if (ex > cx && ex + w > cx + L.content) ex = cx, y += L.h_tag + 2 * s;
      round_rect(cw_rect_make(ex, y, w, L.h_tag), L.h_tag * 0.35f, rgba(TEAM_RGB[i][0], TEAM_RGB[i][1], TEAM_RGB[i][2], 0.85f));
      label(tags[k], ex + L.h_tag * 0.3f, y + 1 * s, CARD_TAG * s, rgba(0.05f, 0.03f, 0.1f, 1), CW_ALIGN_LEFT, 0);
      ex += w + 3 * s;
    }
  }
  /* who is who: a bobbing team marker over every human's critter */
  for (int i = 0; i < MAXP; i++) {
    const player *p = &G.S.p[i];
    if ((G.slot[i] != SLOT_HUMAN && G.slot[i] != SLOT_KEYS2) || !p->alive) continue;
    cw_vec2 sp;
    if (!project((t3_vec3){ wx(p->x), 1.55f + 0.06f * cw_sin(G.ticks * 0.12f + i), wz(p->y) }, W, H, &sp)) continue;
    cw_color tc = rgba(TEAM_RGB[i][0], TEAM_RGB[i][1], TEAM_RGB[i][2], 1);
    cw_vec2 tri[3] = { { sp.x - 7 * s, sp.y - 6 * s }, { sp.x + 7 * s, sp.y - 6 * s }, { sp.x, sp.y + 4 * s } };
    cw_r2d_polygon(tri, 3, tc);
    snprintf(buf, sizeof buf, "P%d", i + 1);
    text(buf, sp.x - 40 * s, sp.y - 24 * s, 14 * s, tc, CW_ALIGN_CENTER, 80 * s);
  }
  /* every lit bomb shows its fuse burning down as a ring over it */
  for (int i = 0; i < MAXB; i++) {
    const bomb *b = &G.S.bombs[i];
    if (!b->live || G.S.over_tick) continue;
    cw_vec2 sp;
    if (!project((t3_vec3){ wx(b->x), 1.0f, wz(b->y) }, W, H, &sp)) continue;
    float r = 9 * s;
    cw_r2d_circle(sp, r + 2 * s, 20, rgba(0.08f, 0.04f, 0.1f, 0.55f));
    if (b->fuse < 0) { /* a remote bomb waits for its owner */
      cw_r2d_circle_lines(sp, r - 1 * s, 2.5f * s, 20, rgba(TEAM_RGB[b->owner][0], TEAM_RGB[b->owner][1], TEAM_RGB[b->owner][2], 1));
      continue;
    }
    float f = cw_clampf(b->fuse / 150.0f, 0, 1);
    int n = 2 + (int)(f * 22);
    cw_color fc = f > 0.4f ? rgba(1, 0.85f, 0.3f, 1) : rgba(1, 0.35f, 0.2f, 1);
    float rm = r - 1 * s;
    cw_r2d_arc(sp, rm - 1.5f * s, rm + 1.5f * s, -1.5707963f, -1.5707963f + 6.2831853f * f, n - 1, fc);
  }
  /* callouts: what a pick-up gave, who got knocked out */
  static const char *const PU_LABEL[PU_KINDS] = { "", "+BOMB", "+FIRE", "+SPEED", "KICK!", "REMOTE!", "PIERCE!", "BUBBLE!" };
  for (int i = 0; i < G.npops; i++) {
    cw_vec2 sp;
    float age = (float)G.pops[i].age;
    if (!project((t3_vec3){ tx_w(G.pops[i].x), 1.2f + age * 0.012f, tz_w(G.pops[i].y) }, W, H, &sp)) continue;
    float a = age < 60 ? 1 : 1 - (age - 60) / 20.0f, grow = age < 8 ? 0.8f + 0.025f * age : 1; /* from 80%: never under the body size */
    int k = G.pops[i].kind, w = G.pops[i].who;
    const char *label = k == -1 ? "KO!" : k == -2 ? "POP!" : PU_LABEL[k];
    cw_color c = k == -1 ? rgba(1, 0.35f, 0.25f, a) : rgba(TEAM_RGB[w][0] * 0.4f + 0.6f, TEAM_RGB[w][1] * 0.4f + 0.6f, TEAM_RGB[w][2] * 0.4f + 0.6f, a);
    bold(label, sp.x - 150 * s, sp.y - 20 * s, (k == -1 ? 30 : 22) * s * grow, c, CW_ALIGN_CENTER, 300 * s);
  }
  /* the timer; sudden death flashes */
  int left = G.S.round_ticks - G.S.tick;
  cw_rect tr = L.timer;
  float ty = tr.y + (tr.h - measure_bold("0:00", 32 * s).height) * 0.5f;
  bool sd = left <= 0;
  panel(tr, 1, sd ? rgba(1, 0.3f, 0.2f, 1) : rgba(1, 1, 1, 0.7f), rgba(0.08f, 0.06f, 0.14f, 0.75f));
  if (sd) bold("0:00", tr.x, ty, 32 * s, (G.ticks / 15) % 2 ? rgba(1, 0.35f, 0.25f, 1) : rgba(1, 0.9f, 0.4f, 1), CW_ALIGN_CENTER, tr.w);
  else {
    int secs = (left + 59) / 60;
    snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
    bold(buf, tr.x, ty, 32 * s, secs <= 10 ? rgba(1, 0.5f, 0.3f, 1) : rgba(1, 1, 1, 1), CW_ALIGN_CENTER, tr.w);
  }
  snprintf(buf, sizeof buf, "ROUND %d", G.round);
  text(buf, tr.x - 20 * s, tr.y + tr.h + 4 * s, 14 * s, rgba(1, 0.95f, 0.85f, 0.85f), CW_ALIGN_CENTER, tr.w + 40 * s);
  if (sd && G.S.tick - G.S.round_ticks < 150) {
    float a = cw_clampf((150 - (G.S.tick - G.S.round_ticks)) / 30.0f, 0, 1), p = 1 + 0.08f * cw_sin(G.ticks * 0.3f);
    bold("SUDDEN DEATH!", 0, H * 0.4f, 64 * s * p, rgba(1, 0.35f, 0.2f, a), CW_ALIGN_CENTER, W);
  }
  if (G.state == GS_INTRO) {
    float a = cw_clampf((110 - G.state_ticks) / 12.0f, 0, 1);
    snprintf(buf, sizeof buf, "ROUND %d  -  %s", G.round, G.info && G.info->title ? G.info->title : "");
    float h1 = measure_bold(buf, 30 * s).height, h2 = measure_bold("READY...", 56 * s).height, by = fmaxf(H * 0.36f, L.bottom + 8 * s);
    cw_r2d_rect(cw_rect_make(0, by, W, h1 + h2 + 16 * s), rgba(0.06f, 0.03f, 0.1f, 0.45f * a));
    bold(buf, 0, by + 6 * s, 30 * s, rgba(1, 0.85f, 0.4f, a), CW_ALIGN_CENTER, W);
    const char *word = G.state_ticks < 40 ? "" : G.state_ticks < 100 ? "READY..." : "GO!";
    float pop = G.state_ticks >= 100 ? 1 + 0.3f * (1 - (G.state_ticks - 100) / 10.0f) : 1;
    bold(word, 0, by + 6 * s + h1 + h2 * (1 - pop) * 0.5f, 56 * s * pop, G.state_ticks >= 100 ? rgba(1, 0.4f, 0.25f, a) : rgba(1, 1, 1, a), CW_ALIGN_CENTER, W);
    if (G.round == 1 && G.state_ticks < 100 && G.info && G.info->blurb) prompt_line_above(G.info->blurb, W, H - 24 * s, 18 * s, 1);
  }
  if (G.state == GS_PLAY && G.S.tick < 240) {
    const char *how = controls_line();
    float a = cw_clampf((240 - G.S.tick) / 30.0f, 0, 1);
    prompt_line_above(how, W, H - 10 * s, 16 * s, a);
  }
}

/* the touch controls, while touch is the device in use */
static void touch_button(int id, const char *label, cw_color c, float s) {
  (void)s;
  int w, h;
  cw_window_size(&w, &h);
  cw_vec2 a = cw_r2d_window_to_canvas(cw_v2(0, 0)), b = cw_r2d_window_to_canvas(cw_v2((float)w, (float)h));
  float sx = (b.x - a.x) / (float)w;
  bool on = cw_touch_control_active(id);
  float x, y, bw, bh;
  int ww, wh;
  cw_window_size(&ww, &wh);
  float H = (float)wh, W = (float)ww, k = H / 540.0f;
  if (id == TOUCH_BOMB) x = W - 196 * k, y = H - 196 * k, bw = bh = 176 * k;
  else if (id == TOUCH_ACT) x = W - 336 * k, y = H - 150 * k, bw = bh = 130 * k;
  else x = W * 0.5f - 46 * k, y = 76 * k, bw = 92 * k, bh = 46 * k;
  cw_vec2 c0 = cw_r2d_window_to_canvas(cw_v2(x + bw * 0.5f, y + bh * 0.5f));
  float r = fminf(bw, bh) * 0.5f * sx * (on ? 0.92f : 1);
  if (id == TOUCH_PAUSE) {
    round_rect(cw_rect_make(c0.x - bw * 0.5f * sx, c0.y - bh * 0.5f * sx, bw * sx, bh * sx), 12 * sx, rgba(0.08f, 0.05f, 0.12f, 0.55f));
    text(label, c0.x - 60 * sx, c0.y - 9 * sx, 15 * sx, rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER, 120 * sx);
    return;
  }
  cw_r2d_circle(c0, r, 40, rgba(c.r * 0.35f, c.g * 0.35f, c.b * 0.35f, on ? 0.75f : 0.5f));
  cw_r2d_circle_lines(c0, r, 4 * sx, 40, rgba(c.r, c.g, c.b, on ? 1 : 0.8f));
  bold(label, c0.x - r * 2, c0.y - r * 0.3f, r * 0.55f, rgba(1, 1, 1, on ? 1 : 0.85f), CW_ALIGN_CENTER, r * 4);
}
static void draw_touch(float W, float H, float s) {
  (void)W;
  (void)H;
  bool on = cw_touch_control_active(TOUCH_STICK);
  int ww, wh;
  cw_window_size(&ww, &wh);
  float k = (float)wh / 540.0f;
  cw_vec2 a = cw_r2d_window_to_canvas(cw_v2(0, 0)), b = cw_r2d_window_to_canvas(cw_v2((float)ww, (float)wh));
  float sx = (b.x - a.x) / (float)ww;
  cw_vec2 wc = cw_touch_stick_center(TOUCH_STICK); /* the engine keeps the ring at its home */
  cw_vec2 c = cw_r2d_window_to_canvas(wc);
  float R = 62 * k * sx;
  cw_r2d_circle(c, R * 1.25f, 40, rgba(0.08f, 0.05f, 0.12f, on ? 0.45f : 0.3f));
  cw_r2d_circle_lines(c, R * 1.25f, 3 * sx, 40, rgba(1, 1, 1, on ? 0.8f : 0.45f));
  /* the four grid directions */
  for (int d = 0; d < 4; d++) {
    float ang = d * 1.5707963f, rr = R * 0.95f;
    cw_vec2 tip = cw_v2(c.x + cw_cos(ang) * rr, c.y + cw_sin(ang) * rr), l = cw_v2(c.x + cw_cos(ang + 0.25f) * rr * 0.72f, c.y + cw_sin(ang + 0.25f) * rr * 0.72f),
            m = cw_v2(c.x + cw_cos(ang - 0.25f) * rr * 0.72f, c.y + cw_sin(ang - 0.25f) * rr * 0.72f);
    cw_vec2 tri[3] = { tip, l, m };
    cw_r2d_polygon(tri, 3, rgba(1, 1, 1, on ? 0.7f : 0.4f));
  }
  cw_vec2 v = on ? cw_touch_stick_value(TOUCH_STICK) : cw_v2(0, 0);
  cw_r2d_circle(cw_v2(c.x + v.x * R, c.y + v.y * R), R * 0.45f, 28, rgba(1, 0.85f, 0.3f, on ? 0.9f : 0.5f));
  touch_button(TOUCH_BOMB, "BOMB", rgba(1, 0.4f, 0.25f, 1), s);
  touch_button(TOUCH_ACT, "ACT", rgba(0.35f, 0.65f, 1, 1), s);
  touch_button(TOUCH_PAUSE, "PAUSE", rgba(1, 1, 1, 1), s);
}

static void draw_round_end(float W, float H, float s) {
  char buf[96];
  int t = G.state_ticks;
  float in = cw_clampf((t - 20) / 15.0f, 0, 1);
  if (in <= 0) return;
  if (G.round_winner >= 0) snprintf(buf, sizeof buf, "%s WINS THE ROUND!", CRIT_NAME[G.crit[G.round_winner]]);
  else snprintf(buf, sizeof buf, "DRAW!");
  float p = 1 + 0.25f * cw_clampf(1 - (t - 20) / 12.0f, 0, 1);
  cw_color c = G.round_winner >= 0 ? rgba(TEAM_RGB[G.round_winner][0] * 0.5f + 0.5f, TEAM_RGB[G.round_winner][1] * 0.5f + 0.5f,
                                          TEAM_RGB[G.round_winner][2] * 0.5f + 0.5f, in)
                                   : rgba(0.9f, 0.9f, 1, in);
  bold(buf, 0, fmaxf(H * 0.15f, hud_bottom(W, H) + 8 * s), 54 * s * p, c, CW_ALIGN_CENTER, W);
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += G.slot[i] != SLOT_OFF;
  float cw2 = 120 * s, x0 = (W - n * cw2) * 0.5f;
  int k = 0;
  glass(cw_rect_make(x0 - 10 * s, H * 0.74f - 36 * s, n * cw2 + 20 * s, 96 * s), in);
  for (int i = 0; i < MAXP; i++) {
    if (G.slot[i] == SLOT_OFF) continue;
    float x = x0 + k++ * cw2 + cw2 * 0.5f, y = H * 0.74f;
    face(x, y, 22 * s, G.crit[i], i, !G.S.p[i].alive, in);
    for (int w = 0; w < G.wins_needed; w++) {
      bool got = w < G.wins[i];
      bool fresh = got && w == G.wins[i] - 1 && i == G.round_winner;
      float sc = fresh ? cw_clampf((t - 40) / 10.0f, 0, 1.2f) : 1;
      float sx = x + (w - (G.wins_needed - 1) * 0.5f) * 20 * s;
      star(sx, y + 40 * s, 8 * s, rgba(0.35f, 0.33f, 0.4f, in), 0);
      if (got && sc > 0) star(sx, y + 40 * s, 8 * s * sc, rgba(1, 0.82f, 0.15f, in), 0);
    }
  }
}

static void draw_podium(float W, float H, float s) {
  char buf[96];
  int t = G.state_ticks;
  float in = cw_clampf(t / 20.0f, 0, 1);
  if (G.mode == MODE_ADVENTURE) {
    bool won = G.match_winner == 0;
    bold(won ? "STAGE CLEAR!" : "SO CLOSE...", 0, H * 0.04f, 62 * s, won ? rgba(1, 0.82f, 0.25f, in) : rgba(0.8f, 0.85f, 1, in), CW_ALIGN_CENTER, W);
    if (won && G.arena < ARENAS) {
      snprintf(buf, sizeof buf, "%s is open!", ARENA_TITLES[G.arena]);
      prompt_line(buf, W, fmaxf(H * 0.19f, H * 0.04f + measure_bold("S", 62 * s).height + 4 * s), 20 * s);
    }
  } else if (G.match_winner >= 0) {
    snprintf(buf, sizeof buf, "%s TAKES THE MATCH!", CRIT_NAME[G.crit[G.match_winner]]);
    bold(buf, 0, H * 0.04f, 52 * s, rgba(1, 0.82f, 0.25f, in), CW_ALIGN_CENTER, W);
  }
  int order[MAXP], n = 0;
  for (int i = 0; i < MAXP; i++)
    if (G.slot[i] != SLOT_OFF) order[n++] = i;
  for (int a = 0; a < n; a++)
    for (int b = a + 1; b < n; b++)
      if (G.wins[order[b]] > G.wins[order[a]]) {
        int tmp = order[a];
        order[a] = order[b];
        order[b] = tmp;
      }
  /* the standings, measured: a row as tall as its name, the box as wide as the widest row */
  float hn = measure("Mg", 15 * s).height, ph = fmaxf(26 * s, hn + 4 * s), fr = fmaxf(10 * s, hn * 0.45f), nw = 0, ww = 0;
  for (int r = 0; r < n; r++) {
    snprintf(buf, sizeof buf, "%d. %s", r + 1, CRIT_NAME[G.crit[order[r]]]);
    nw = fmaxf(nw, measure(buf, 15 * s).width);
    snprintf(buf, sizeof buf, "%d wins", G.wins[order[r]]);
    ww = fmaxf(ww, measure(buf, 14 * s).width);
  }
  float fcol = 2 * fr * 1.18f + 10 * s, pw = fmaxf(250 * s, fcol + nw + 20 * s + ww + 6 * s), x = W - pw - 24 * s;
  float pbottom = H - 10 * s, ptop = pbottom - prompt_line_h(menu_prompt(), W, 16 * s);
  float y = fminf(H * 0.62f, ptop - 18 * s - ph * n);
  glass(cw_rect_make(x - 10 * s, y - 8 * s, pw + 20 * s, ph * n + 16 * s), in);
  for (int r = 0; r < n; r++) {
    int i = order[r];
    float yy = y + r * ph;
    face(x + fr * 1.18f + 2 * s, yy + ph * 0.5f, fr, G.crit[i], i, false, in);
    snprintf(buf, sizeof buf, "%d. %s", r + 1, CRIT_NAME[G.crit[i]]);
    label(buf, x + fcol, yy + (ph - hn) * 0.5f, 15 * s, rgba(TEAM_RGB[i][0] * 0.4f + 0.6f, TEAM_RGB[i][1] * 0.4f + 0.6f, TEAM_RGB[i][2] * 0.4f + 0.6f, in),
         CW_ALIGN_LEFT, 0);
    snprintf(buf, sizeof buf, "%d wins", G.wins[i]);
    label(buf, x, yy + (ph - measure(buf, 14 * s).height) * 0.5f, 14 * s, rgba(1, 0.95f, 0.85f, in), CW_ALIGN_RIGHT, pw - 6 * s);
  }
  if (t > 120) prompt_line_above(menu_prompt(), W, pbottom, 16 * s, 1);
}

/* the graphics settings from the pause screen (cartwheel/quality_menu.h): the
 * settings action or a tap opens the page, which owns the presses while it is open (the
 * pause menu's tick leaves them alone). Render-only, so it is read in
 * update, never in the simulation; the choice is kept in the save */
static cw_rect gfx_rect(float W, float H) { return pause_layout_of(W, H).gfx; }
static void gfx_input(void) {
  if (G.state != GS_PAUSE) {
    cw_quality_menu_close();
    return;
  }
  if (cw_quality_menu_is_open()) {
    cw_quality_menu_update();
    if (!cw_quality_menu_is_open()) {
      sfx(SFX_SELECT, 0.6f);
      if (cw_quality_menu_changed()) save_write();
    }
    return;
  }
  if (G.state_ticks < 5) return;
  const cw_raw_input *in = cw_input_raw();
  bool hit = any_pressed(G.a_settings, -1);
  cw_vec2 cs = cw_r2d_canvas_size();
  if ((in->mouse_pressed & 1) && in_rect(mouse_canvas(), gfx_rect(cs.x, cs.y))) hit = true;
  if (!hit) return;
  cw_quality_menu_open();
  sfx(SFX_SELECT, 0.6f);
}
/* " · 1080p": the render resolution beside the graphics preset (the host
 * sets it, RetroArch's Internal Resolution; read-only here) */
/* a label's size so that it fits maxw (shrunk, never grown): the graphics
 * line is longer at 1080p than at 540p, MEDIUM longer than LOW */
static float fit_title(const char *s, float size, float maxw) {
  float w = cw_font_measure(font_title, s, &(cw_text_style){ .size = size }).width;
  float k = w > maxw && w > 0 ? maxw / w : 1;
  return size * fmaxf(k, 14.0f / 22.0f); /* never under the body text size */
}
static void gfx_res_suffix(char *buf, size_t cap) {
  size_t n = strlen(buf);
  snprintf(buf + n, cap > n ? cap - n : 0, " \xC2\xB7 %s", cw_render_res_label());
}
/* a pause row: a panel with its text centred; the cursor's glows */
static void pause_row(cw_rect r, const char *txt, bool cur, float s) {
  if (cur) glow_cursor(r, s, 16, false);
  panel(r, 1, rgba(1, 1, 1, 0.6f), cur ? rgba(0.95f, 0.35f, 0.25f, 0.95f) : rgba(0.15f, 0.1f, 0.25f, 0.85f));
  float sz = fit_title(txt, PAUSE_ROW * s, r.w - 16 * s);
  bold(txt, r.x, r.y + (r.h - measure_bold(txt, sz).height) * 0.5f, sz, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
}
static void draw_pause(float W, float H, float s) {
  pause_layout L = pause_layout_of(W, H);
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.05f, 0.02f, 0.1f, 0.45f));
  bold("PAUSED", 0, L.title_y, 64 * s, rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, W);
  char buf[64];
  snprintf(buf, sizeof buf, "GRAPHICS: %s", cw_quality_label());
  for (char *c = buf; *c; c++) *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
  gfx_res_suffix(buf, sizeof buf); /* the render resolution: the host sets it */
  pause_row(L.gfx, buf, false, s);
  cw_prompt_text(font_ui_bold, ON_TOUCH ? "tap for settings" : "{settings} settings", 0, cw_v2(W * 0.5f, L.gfx_hint_y), 16 * s,
                 rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER);
  snprintf(buf, sizeof buf, "TEXT SIZE: %s", cw_text_scale_label());
  pause_row(L.row[0], buf, G.menu == PM_TEXT_SIZE, s);
  snprintf(buf, sizeof buf, "TEXT BACKGROUND: %s", cw_text_backdrop_label());
  for (char *c = buf; *c; c++) *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
  pause_row(L.row[1], buf, G.menu == PM_TEXT_BG, s);
  if (L.controls) prompt_line(controls_line(), W, L.controls_y, 16 * s);
  static const char *const ITEMS[2] = { "RESUME", "QUIT TO TITLE" };
  for (int i = 0; i < 2; i++) {
    cw_rect r = L.btn[i];
    if (G.menu == i) glow_cursor(r, s, 20, false);
    panel(r, 1, rgba(1, 1, 1, 0.8f), G.menu == i ? rgba(0.95f, 0.35f, 0.25f, 0.95f) : rgba(0.15f, 0.1f, 0.25f, 0.85f));
    bold(ITEMS[i], r.x, r.y + (r.h - measure_bold(ITEMS[i], PAUSE_BTN * s).height) * 0.5f, PAUSE_BTN * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
  }
}

static void render(void *u, double alpha) {
  g_rendering = true;
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
#ifdef BOMBERDUDE_AUTOPILOT
  if (getenv("BOMBERDUDE_NO_HUD")) { /* art captures: the 3D alone */
    cw_r2d_end();
    return;
  }
#endif
  switch (G.state) {
  case GS_TITLE: draw_title(W, H, s); break;
  case GS_PLAYERS: draw_players(W, H, s); break;
  case GS_ARENA: draw_arena_select(W, H, s); break;
  case GS_INTRO:
  case GS_PLAY:
    draw_hud(W, H, s);
    if (ON_TOUCH && touch_controls) draw_touch(W, H, s);
    break;
  case GS_PAUSE:
    if (pause_layout_of(W, H).hud) draw_hud(W, H, s); /* the cards stay when the menu fits under them */
    draw_pause(W, H, s);
    break;
  case GS_ROUND_END:
    draw_hud(W, H, s);
    draw_round_end(W, H, s);
    break;
  case GS_PODIUM: draw_podium(W, H, s); break;
  }
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = font_ui, .accent = rgba(1, 0.85f, 0.3f, 1) });
  cw_r2d_end();
}

static void shutdown(void *u) {
  (void)u;
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  cw_node_type_register(&arena_type);
#ifdef BOMBERDUDE_AUTOPILOT
  cw_set_raw_input_filter(autopilot, NULL);
#endif
  static cw_app_desc base = { .title = "BomberDude", .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed,
                              .update = update, .render = render, .shutdown = shutdown, .save = &SAVE, .save_size = sizeof SAVE,
                              .touch_pointer = true };
  const char *res = getenv("BOMBERDUDE_RES");
  if (res && strchr(res, 'x')) {
    base.width = atoi(res);
    base.height = atoi(strchr(res, 'x') + 1);
  }
  return cw_scene_app(NULL, &base);
}
