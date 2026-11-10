/* derby: a gritty demolition derby on cartwheel and three.c (three.js r186).
 *
 *   flow      title -> QUICK MATCH (one press: you and five CPU bangers) ->
 *             the bout -> the results -> the title
 *   rules     sim.c (cars on Box3D with cw_vehicle3d wheels, crashes in 3D,
 *             damage from contact impulses, deterministic); ai.c the CPU
 *             drivers. Last car running wins.
 *   input     gamepad first (right trigger or shoulder throttle, left brake / reverse, stick steer,
 *             south handbrake, east horn, north look back), the keyboard (W S A D or
 *             the arrows, Space handbrake, H horn, C look back), the mouse
 *             and taps for the menus, and on-screen controls on touch
 *             screens from the first frame (a steering stick on the left,
 *             GAS / BRAKE / HANDBRAKE on the right)
 *   look      a dusk ProceduralSky (PMREM environment), three.c shadows
 *             following the play; clustered lighting (the floodlights'
 *             shadows in the shadow atlas, a flickering light on every
 *             fire); HeightFog for the dust, volumetric from Medium effects
 *             up in one view (the floods' beams, dust and smoke palls lit
 *             in it); both follow the graphics settings (apply_gfx); GTAO,
 *             bloom, SMAA, AgX and a warm, worn LUT; Physical materials
 *             (clearcoat paint scuffed to bare metal round every dent, sheen
 *             on the crowd's clothes); GPU particles for dust, smoke, fire
 *             and sparks; Black Ops One / Russo One HUD with outlines
 *
 * Determinism: the bout replays from the input stream (fixed ticks,
 * quantised inputs); the debug fields name the state the gates compare
 * across hosts.
 *
 * Built twice: `derby` reads devices; `derby_auto` plays a pad script itself
 * through cw_set_raw_input_filter, so input-free hosts run it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/prompt.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "study.h"
#include "world.h"
#include "replay.h"
#include "debris.h"

void fx_set_colours(const int colours[MAXC]);

static const char *const DRIVER[MAXC] = { "YOU", "RUSTY", "MAGPIE", "BIG LOU", "DOC", "SPANNER", "TINY", "PATCHES" };

/* ── the Arena node: every arena scene's root ──────────────────────── */

typedef struct arena_info {
  int index;
  const char *title, *blurb;
  int theme, seed;
} arena_info;
static const char *const THEME_NAMES[] = { "DUSK", "NIGHT", NULL };
static const cw_prop arena_props[] = {
  CW_PROP_FIELD(arena_info, index, CW_T_INT, CW_PROP_DEFAULT, "Arena number (0: the title backdrop)."),
  CW_PROP_FIELD(arena_info, title, CW_T_STRING, CW_PROP_DEFAULT, "Its name."),
  CW_PROP_FIELD(arena_info, blurb, CW_T_STRING, CW_PROP_DEFAULT, "One line about it."),
  { .name = "theme", .type = CW_T_ENUM, .flags = CW_PROP_DEFAULT, .offset = offsetof(arena_info, theme), .size = sizeof(int),
    .enum_names = THEME_NAMES, .doc = "Mood." },
  CW_PROP_FIELD(arena_info, seed, CW_T_INT, CW_PROP_DEFAULT, "Scenery and crowd."),
  CW_PROP_END,
};
static cw_node_type arena_type = { .name = "Arena", .parent = "Node3D", .data_size = sizeof(arena_info), .props = arena_props,
                                   .doc = "Root of a derby arena: its name and mood." };

/* ── state ──────────────────────────────────────────────────────────── */

typedef enum { GS_TITLE, GS_BOUT, GS_PAUSE, GS_RESULTS } game_state;
enum { SLOT_OFF, SLOT_HUMAN, SLOT_ROOKIE, SLOT_PRO, SLOT_ACE, SLOT_KINDS };

typedef enum {
  SFX_CRUNCH, SFX_CRUNCH_BIG, SFX_THUD, SFX_SCRAPE, SFX_GLASS, SFX_BOOM, SFX_HORN, SFX_BEEP, SFX_GO, SFX_CHEER, SFX_CROWD,
  SFX_ENGINE, SFX_SKID, SFX_MOVE, SFX_SELECT, SFX_HORN2, SFX_COUNT
} sfx_id;

/* the save: a small versioned, checksummed layout (cw_app_desc.save) */
typedef struct save_data {
  uint32_t magic;          /* 'DRBY' */
  uint16_t version, size;
  uint32_t checksum;
  uint32_t bouts, wins, wrecks;
  uint8_t colour, number, kind, pad; /* pad: version 1's graphics preset */
  uint8_t gfx[4];          /* the graphics settings (cw_quality_saved, little-endian) */
  uint8_t text_scale;      /* Text size (cw_text_scale_saved; 0 = the default) */
  uint8_t text_bg;         /* Text background (cw_text_backdrop: Off, Outline, Panel) */
  uint8_t pad2[2];
} save_data;
static save_data SAVE;

#define BOUT_SECS 180
#define NCARS 6

static struct {
  /* debug fields */
  int32_t state, phase, clock, alive, winner, ticks, state_ticks, lookback;
  int32_t thr[4], steer[4]; /* each player's car's input on the last tick (P1-P4; 0 without that player) */
  int32_t pos[MAXC * 2], hp[MAXC], dents[MAXC], wrecked[MAXC], slots[MAXC], points[MAXC];
  int32_t crashes, walls, wrecks, max_hit, flips;
  float wheel_yaw0, yaw_rate0;  /* car 1's drawn front-wheel angle and its turn rate (rad, rad/s; + = left) */
  int32_t steer_agrees;         /* 1 while car 1 turns and its wheels point the same way */
  int32_t hint_frames[4];       /* frames each player's view drew the controls hint this bout */
  int32_t beats, beat_held;     /* big-hit beats this bout, and the ticks the bout was held for them */
  int32_t duel_tick;            /* the bout tick the last two were left (0: not yet) */
  int32_t rumbles;              /* pad rumbles asked for this bout */
  int beat, beat_cool;          /* ticks left in the beat; ticks until another may start */
  int32_t replay_f;             /* the results' replay of the biggest hit: its frame (-1 none, past the end: the table) */
  int32_t replay_k;             /* that hit, tonne m/s x 100 (debug) */
  int32_t table_t0;             /* state_ticks the results table came in */
  int32_t mud[MAXC];            /* each car's mud, percent (debug cars.mud) */
  bool duel;                    /* the last two: the camera eases back, the crowd and the music rise */
  float duel_k, swell;          /* 0..1: the duel's camera ease; the crowd's swell */
  bool gassed[4];               /* the player has given it gas since the flag dropped */
  uint32_t hash, state_hash;
  /* the bout */
  int slot[MAXC];
  int kind[MAXC];
  car_look look[MAXC];
  int human_of[MAXC];
  int menu;
  cw_node scene, camera, sun;
  bool sun_up, sun_known; /* the sun above the horizon (its shadow and fog light follow) */
  arena_info *info;
  sim S;
  ai_mem ai[MAXC];
  int rule;
  /* the chase camera (per human; one view for now) */
  int players;          /* humans in the next bout (1..4: splitscreen past 1) */
  bool joined[4];       /* on the title: which players are in (player 0 always; pads 1-3 press to join) */
  int pl[CW_MAX_VIEWS]; /* the input player behind each view */
  int32_t views;        /* debug: the views in this bout */
  int feed_ticks;
  char feed[96];
  cw_action a_fwd, a_back, a_left, a_right, a_hb, a_horn, a_look, a_start, a_esc, a_settings;
  cw_action m_left, m_right, m_up, m_down, m_ok;
  bool open_gfx; /* the pause menu chose GRAPHICS: update opens the page */
} G;

/* one view's camera rig: a chase camera on springs, a spectator pick when
 * its car is out, a smoothed eye and look point. View 0 is the scene's own
 * Camera3D; splitscreen adds one Camera3D per extra player. */
typedef struct rig {
  cw_node cam;
  t3_vec3 eye, eye_v, look, look_v, cam_at, cam_eye;
  float yaw, yaw_v;
  bool live, snap;
  int spectate, spectate_ticks;
} rig;
static rig RIG[CW_MAX_VIEWS];
static int nviews; /* humans with a view of their own in this bout (1 = full screen) */
/* a driver's name: YOU alone, P1-P4 with more than one player */
static const char *driver(int i) {
  static const char *const P[4] = { "P1", "P2", "P3", "P4" };
  return nviews > 1 && G.human_of[i] >= 0 && G.human_of[i] < 4 ? P[G.human_of[i]] : DRIVER[i];
}

/* ── autopilot: a gate's pad script compiled in ───────────────────── */

#ifdef DERBY_AUTOPILOT
static const struct { int frame, pad; uint32_t buttons; float lx, ly, lt, rt; } SCRIPT[] = {
#include "script.inc"
};
static void autopilot(cw_raw_input *in, uint64_t frame, void *user) {
  (void)user;
  memset(in->pads, 0, sizeof in->pads);
  memset(in->keys, 0, sizeof in->keys);
  memset(in->keys_pressed, 0, sizeof in->keys_pressed);
  in->mouse_buttons = in->mouse_pressed = 0;
  for (int p = 0; p < 2; p++) {
    in->pads[p].connected = true;
    int cur = -1;
    for (int i = 0; i < (int)CW_ARRAY_LEN(SCRIPT) && (uint64_t)(SCRIPT[i].frame - 1) <= frame; i++)
      if (SCRIPT[i].pad == p) cur = i;
    if (cur < 0) continue;
    in->pads[p].buttons = SCRIPT[cur].buttons;
    in->pads[p].axes[CW_AXIS_LX] = SCRIPT[cur].lx;
    in->pads[p].axes[CW_AXIS_LY] = SCRIPT[cur].ly;
    in->pads[p].axes[CW_AXIS_LT] = SCRIPT[cur].lt;
    in->pads[p].axes[CW_AXIS_RT] = SCRIPT[cur].rt;
  }
}
#endif

/* ── audio ──────────────────────────────────────────────────────────── */

static const char *const SFX_FILES[SFX_COUNT] = { "audio/crunch.wav", "audio/crunch_big.wav", "audio/thud.wav", "audio/scrape.wav",
  "audio/glass.wav", "audio/boom.wav", "audio/horn.wav", "audio/beep.wav", "audio/go.wav", "audio/cheer.wav", "audio/crowd.wav",
  "audio/engine.wav", "audio/skid.wav", "audio/move.wav", "audio/select.wav", "audio/horn2.wav" };
static struct {
  bool on;
  cw_bus music_bus, sfx_bus, crowd_bus, engine_bus;
  cw_res sfx_res[SFX_COUNT], music_res;
  cw_sound sfx[SFX_COUNT];
  cw_voice music, crowd, engine[MAXC], skid[MAXC];
  float eng_vol[MAXC], eng_pitch[MAXC], skid_vol[MAXC];
  bool music_on;
  int cooldown[SFX_COUNT];
} A;

/* how long each sound waits before it can play again (ticks): a short
 * thump retriggered every few ticks reads as a buzz */
static const uint8_t COOL[SFX_COUNT] = { 5, 8, 4, 6, 10, 20, 12, 10, 30, 60, 1, 1, 1, 3, 5, 12 };
static void sfx_at(sfx_id id, t3_vec3 at, float volume, float pitch) {
  if (!A.on || A.cooldown[id] > 0) return;
  A.cooldown[id] = COOL[id];
  cw_play_desc d = { .bus = A.sfx_bus, .volume = volume, .pitch = pitch, .spatial = true };
  d.space = (cw_voice_3d){ .position = at, .model = CW_ATTEN_INVERSE, .ref_distance = 10, .max_distance = 120, .rolloff = 0.6f };
  cw_audio_play(A.sfx[id], &d);
}
static void sfx(sfx_id id, float volume) {
  if (!A.on || A.cooldown[id] > 0) return;
  A.cooldown[id] = COOL[id];
  cw_audio_play(A.sfx[id], &(cw_play_desc){ .bus = A.sfx_bus, .volume = volume });
}

static void music(bool on) {
  if (!A.on || on == A.music_on) return;
  A.music_on = on;
  if (!on) {
    cw_voice_stop(A.music, 0.8f);
    return;
  }
  A.music_res = cw_res_load("audio/music.ogg", 0);
  cw_sound s = cw_res_audio_sound(A.music_res, &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.music_bus });
  A.music = cw_audio_play(s, &(cw_play_desc){ .loop = true, .fade_in = 1.0f, .bus = A.music_bus });
}

static void audio_init(void) {
  A.on = getenv("DERBY_NO_AUDIO") == NULL && cw_audio_init(&(cw_audio_desc){ 0 });
  if (!A.on) return;
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  A.crowd_bus = cw_bus_create("crowd", CW_AUDIO_MASTER);
  A.engine_bus = cw_bus_create("engines", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.28f);
  cw_bus_set_volume(A.crowd_bus, 0.4f);
  cw_bus_set_volume(A.engine_bus, 0.55f);
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1, 0.1f } });
  for (int i = 0; i < SFX_COUNT; i++) {
    A.sfx_res[i] = cw_res_load(SFX_FILES[i], 0);
    A.sfx[i] = cw_res_audio_sound(A.sfx_res[i], &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = i == SFX_ENGINE || i == SFX_SKID ? MAXC : 4,
                                                                   .bus = i == SFX_ENGINE ? A.engine_bus : A.sfx_bus });
  }
}

static void crowd_bed(bool on) {
  if (!A.on) return;
  if (on && CW_HANDLE_IS_NULL(A.crowd)) A.crowd = cw_audio_play(A.sfx[SFX_CROWD], &(cw_play_desc){ .loop = true, .fade_in = 1.5f, .bus = A.crowd_bus });
  if (!on && !CW_HANDLE_IS_NULL(A.crowd)) {
    cw_voice_stop(A.crowd, 0.8f);
    A.crowd = (cw_voice){ 0 };
  }
}

/* every running car hums: one looping voice each, its pitch and level eased
 * toward the engine's revs every tick (never stepped: a stepped pitch reads
 * as a buzz). The skid squeal the same way. */
static void engines(bool on) {
  if (!A.on) return;
  for (int i = 0; i < MAXC; i++) {
    bool want = on && G.S.c[i].on;
    if (want && CW_HANDLE_IS_NULL(A.engine[i])) {
      cw_play_desc d = { .bus = A.engine_bus, .volume = 0.0f, .loop = true, .spatial = true };
      d.space = (cw_voice_3d){ .position = { G.S.c[i].x, 0.6f, G.S.c[i].z }, .model = CW_ATTEN_INVERSE, .ref_distance = 6, .max_distance = 90, .rolloff = 0.8f };
      A.engine[i] = cw_audio_play(A.sfx[SFX_ENGINE], &d);
      d.bus = A.sfx_bus;
      A.skid[i] = cw_audio_play(A.sfx[SFX_SKID], &d);
      A.eng_vol[i] = A.skid_vol[i] = 0;
      A.eng_pitch[i] = 0.7f;
    }
    if (!want && !CW_HANDLE_IS_NULL(A.engine[i])) {
      cw_voice_stop(A.engine[i], 0.3f);
      cw_voice_stop(A.skid[i], 0.2f);
      A.engine[i] = A.skid[i] = (cw_voice){ 0 };
    }
  }
}
static void engines_tick(void) {
  if (!A.on) return;
  for (int i = 0; i < MAXC; i++) {
    if (CW_HANDLE_IS_NULL(A.engine[i])) continue;
    const car *c = &G.S.c[i];
    float vol = c->wrecked ? 0 : (G.human_of[i] >= 0 ? 0.9f : 0.6f) * (0.35f + 0.65f * c->rpm);
    float pitch = 0.55f + 1.1f * c->rpm;
    A.eng_vol[i] += (vol - A.eng_vol[i]) * 0.1f;
    A.eng_pitch[i] += (pitch - A.eng_pitch[i]) * 0.15f;
    cw_voice_set_volume(A.engine[i], A.eng_vol[i]);
    cw_voice_set_pitch(A.engine[i], A.eng_pitch[i]);
    cw_voice_set_position(A.engine[i], (t3_vec3){ c->x, 0.6f, c->z }, (t3_vec3){ c->vx, 0, c->vz });
    float sk = cw_clampf((fmaxf(c->slip_r, c->slip_f * 0.7f) - 2) / 6, 0, 1) * (c->wrecked ? 0 : 0.7f);
    A.skid_vol[i] += (sk - A.skid_vol[i]) * 0.2f;
    cw_voice_set_volume(A.skid[i], A.skid_vol[i]);
    cw_voice_set_position(A.skid[i], (t3_vec3){ c->x, 0.2f, c->z }, (t3_vec3){ c->vx, 0, c->vz });
  }
}

/* ── devices: the engine says which one each player used last ──────── */

/* P1's device, as the engine's prompts see it (cartwheel/prompt.h): it picks
 * the prompts, the touch controls and the pause button. Asked once a frame in
 * update (prompts are presentation, never the fixed tick); input.mode
 * publishes it (0 mouse, 1 pad, 2 keys, 3 touch). */
static cw_prompt_device p1_device = CW_PROMPT_PAD;
static int32_t input_mode = 1;
/* a pad or the keyboard reads glyphs; the mouse and touch click or tap what they see */
static bool glyphs(void) { return p1_device == CW_PROMPT_PAD || p1_device == CW_PROMPT_KEYBOARD; }
static bool on_touch(void) { return p1_device == CW_PROMPT_TOUCH; }
/* the one switch for the game's own on-screen touch controls (a host that
 * draws its own overlay, wasmcart's doctrine, would turn this off) */
static bool touch_controls = true;
static int TOUCH_STICK = -1, TOUCH_GAS = -1, TOUCH_BRAKE = -1, TOUCH_HB = -1;
static uint32_t prev_mouse_buttons;
static float prev_mx, prev_my;
static bool mouse_clicked, mouse_moved;

static void read_mouse(void) {
  const cw_raw_input *in = cw_input_raw();
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse_buttons & 1);
  mouse_moved = (in->mouse_x != prev_mx || in->mouse_y != prev_my) && in->mouse_present && prev_mx + prev_my != 0;
  prev_mouse_buttons = in->mouse_buttons;
  prev_mx = in->mouse_x;
  prev_my = in->mouse_y;
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
static cw_vec2 mouse_canvas(void) {
  const cw_raw_input *in = cw_input_raw();
  return cw_r2d_window_to_canvas(cw_v2(in->mouse_x, in->mouse_y));
}
static bool any_pressed(cw_action a) {
  for (int p = 0; p < 4; p++)
    if (cw_action_just_pressed(p, a)) return true;
  return false;
}

/* ── save data ──────────────────────────────────────────────────────── */

#define SAVE_MAGIC 0x59425244u /* 'DRBY' */
#define SAVE_VERSION 3
#define SAVE_V1_SIZE offsetof(save_data, gfx)        /* version 1 ended there */
#define SAVE_V2_SIZE offsetof(save_data, text_scale) /* version 2 there */
static uint32_t sum_of(size_t size) {
  const uint8_t *b = (const uint8_t *)&SAVE + offsetof(save_data, bouts);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < size - offsetof(save_data, bouts); i++) h = (h ^ b[i]) * 16777619u;
  return h ^ (SAVE.version | (uint32_t)SAVE.size << 16);
}
static uint32_t save_sum(void) { return sum_of(sizeof SAVE); }
static uint32_t gfx_saved(void) {
  return SAVE.gfx[0] | (uint32_t)SAVE.gfx[1] << 8 | (uint32_t)SAVE.gfx[2] << 16 | (uint32_t)SAVE.gfx[3] << 24;
}
static void save_write(void) {
  uint32_t q = cw_quality_saved();
  if (q)
    for (int i = 0; i < 4; i++) SAVE.gfx[i] = (uint8_t)(q >> (8 * i));
  SAVE.text_scale = cw_text_scale_saved();
  SAVE.text_bg = (uint8_t)cw_text_backdrop_get();
  SAVE.checksum = save_sum();
  cw_save_commit();
}
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC;
  SAVE.version = SAVE_VERSION;
  SAVE.size = (uint16_t)sizeof SAVE;
  SAVE.colour = 1;
  SAVE.number = 7;
  SAVE.kind = CAR_SEDAN;
  SAVE.checksum = save_sum();
}
static void save_check(void) {
  /* version 1 (no gfx; its graphics preset in pad): kept, the preset moved over */
  if (SAVE.magic == SAVE_MAGIC && SAVE.version == 1 && SAVE.size == SAVE_V1_SIZE && SAVE.checksum == sum_of(SAVE_V1_SIZE)) {
    memset(SAVE.gfx, 0, sizeof SAVE.gfx);
    SAVE.gfx[0] = SAVE.pad;
    SAVE.pad = 0;
    SAVE.version = 2;
    SAVE.size = (uint16_t)SAVE_V2_SIZE;
    SAVE.checksum = sum_of(SAVE_V2_SIZE);
  }
  /* version 2 (no text settings): kept, the text settings at their defaults */
  if (SAVE.magic == SAVE_MAGIC && SAVE.version == 2 && SAVE.size == SAVE_V2_SIZE && SAVE.checksum == sum_of(SAVE_V2_SIZE)) {
    SAVE.text_scale = 0, SAVE.text_bg = 0;
    memset(SAVE.pad2, 0, sizeof SAVE.pad2);
    SAVE.version = SAVE_VERSION;
    SAVE.size = (uint16_t)sizeof SAVE;
    SAVE.checksum = save_sum();
  }
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) {
    if (SAVE.magic) cw_warn("derby: save data rejected (magic %08x version %u), starting fresh", SAVE.magic, SAVE.version);
    save_reset();
  }
  if (SAVE.colour >= CAR_COLOURS) SAVE.colour = 1;
  if (SAVE.kind >= CAR_KINDS) SAVE.kind = 0;
}

/* ── scenes and bouts ───────────────────────────────────────────────── */

static cw_node find_type(cw_node n, const char *type) {
  if (cw_node_is(n, type)) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_type(c, type);
    if (cw_node_valid(r)) return r;
  }
}

static void load_scene(int index) {
  cw_tree *t = cw_scene_app_tree();
  replay_reset();
  debris_reset();
  fx_reset();
  scenery_reset();
  world_reset();
  if (cw_node_valid(G.scene)) cw_node_free(G.scene);
  char path[64];
  snprintf(path, sizeof path, "scenes/arena%d.cwscene", index < 1 ? 1 : index);
  G.scene = cw_scene_instance_path(path);
  if (CW_HANDLE_IS_NULL(G.scene)) {
    cw_error("derby: %s failed to load", path);
    return;
  }
  cw_node_add_child(cw_tree_root(t), G.scene);
  G.info = cw_node_data(G.scene, "Arena");
  G.camera = find_type(G.scene, "Camera3D");
  if (cw_node_valid(G.camera)) cw_node_set(G.camera, "interpolate", cw_val_bool(true));
  G.sun = cw_node_get(G.scene, "Sun");
  G.sun_known = false;
  memset(RIG, 0, sizeof RIG);
  RIG[0].cam = G.camera;
  for (int v = 0; v < CW_MAX_VIEWS; v++) RIG[v].snap = true, RIG[v].spectate = -1;
  nviews = 1;
  G.views = 1;
  cw_pipeline_set_views(cw_pipeline_of(cw_scene_app_tree()), NULL, 0);
  cw_pipeline_set_view_hook(cw_pipeline_of(cw_scene_app_tree()), NULL, NULL);
  cw_pipeline_set_views_shared_post(cw_pipeline_of(cw_scene_app_tree()), false);
}

static void assign_humans(void) {
  int k = 0;
  for (int i = 0; i < MAXC; i++) G.human_of[i] = G.slot[i] == SLOT_HUMAN && k < 4 ? k++ : -1;
  /* view k is driven by the k-th player in (P3 may join without P2) */
  int v = 0;
  for (int p = 0; p < 4 && v < CW_MAX_VIEWS; p++)
    if (p == 0 || G.joined[p]) G.pl[v++] = p;
  for (; v < CW_MAX_VIEWS; v++) G.pl[v] = v;
}
static int focus_car(void) {
  for (int i = 0; i < MAXC; i++)
    if (G.human_of[i] >= 0) return i;
  return -1;
}

/* the field: the player's banger (their colour and number), the rest a mix */
static void line_up(bool with_human) {
  static const int KINDS[MAXC] = { CAR_SEDAN, CAR_PICKUP, CAR_WAGON, CAR_VAN, CAR_SEDAN, CAR_WAGON, CAR_PICKUP, CAR_VAN };
  static const int NUMS[MAXC] = { 7, 13, 42, 88, 3, 66, 21, 99 };
  int col = 0;
  for (int i = 0; i < MAXC; i++) {
    int humans = with_human ? CW_CLAMP(G.players, 1, 4) : 0;
    G.slot[i] = i < NCARS ? (i < humans ? SLOT_HUMAN : (i < 3 ? SLOT_PRO : (i < 5 ? SLOT_ROOKIE : SLOT_ACE))) : SLOT_OFF;
    G.kind[i] = KINDS[i];
    if (i == 0) {
      G.kind[i] = SAVE.kind;
      G.look[i] = (car_look){ SAVE.colour, SAVE.number };
    } else {
      if (col == SAVE.colour) col++;
      G.look[i] = (car_look){ col % CAR_COLOURS, NUMS[i] };
      col++;
    }
  }
}

/* each car's detail from this view's camera, just before the view renders
 * (a quadrant draws a car half as tall as the full screen does) */
static int car_of_view(int v);
static float view_fov = 64; /* the split views' vertical angle (views_setup) */
static void view_lod(int v, void *u) {
  (void)u;
  const cw_view *vs = NULL;
  int n = cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &vs);
  /* the car this view follows (its player's, or the one it watches once that is out) keeps full detail */
  int me = car_of_view(v), follow = me >= 0 && (!G.S.c[me].wrecked || G.S.c[me].wreck_ticks < 150) ? me : RIG[v].spectate;
  if (v >= n) return;
  /* how big a car is drawn there against one player's full screen: the view's
   * height over the tangent of its half angle (two players' views are
   * narrower vertically, so a car there is drawn bigger than in a quarter) */
  float k = vs[v].h * tanf(32 * CW_PI / 180) / tanf(view_fov * 0.5f * CW_PI / 180);
  world_lod(RIG[v].cam_eye, k < 1 ? k : 0.999f, follow);
}

/* splitscreen: a Camera3D per player and the pipeline's views. Two players
 * one above the other: each view keeps the arena's full width (the hits come
 * from the sides), with a narrower vertical field of view so the picture is
 * as wide as one player's. Three take three quadrants (the top right one is
 * the shared HUD and the map), four take all four. The costs that scale
 * with views are trimmed (GTAO off, a smaller sun shadow). */
static void views_setup(void) {
  int n = 0;
  for (int i = 0; i < MAXC; i++) n += G.human_of[i] >= 0;
  nviews = n < 1 ? 1 : n;
  G.views = nviews;
  if (nviews < 2) return;
  int ww = 0, wh = 0;
  cw_window_size(&ww, &wh);
  bool stacked = nviews == 2 && ww >= wh; /* (a tall screen splits side by side) */
  float fov = stacked ? 40 : 64;
  view_fov = fov;
  cw_view vw[CW_MAX_VIEWS];
  static const float Q[CW_MAX_VIEWS][2] = { { 0, 0 }, { 0.5f, 0 }, { 0, 0.5f }, { 0.5f, 0.5f } };
  static const int Q3[3] = { 0, 2, 3 }; /* three players: top left, bottom left, bottom right */
  for (int v = 0; v < nviews; v++) {
    cw_node c = RIG[0].cam;
    if (v > 0) {
      char name[16];
      snprintf(name, sizeof name, "Camera%d", v + 1);
      c = cw_node_new("Camera3D", name);
      cw_node_add_child(G.scene, c);
      cw_node_set(c, "near", cw_val_float(0.2f));
      cw_node_set(c, "far", cw_val_float(600));
      cw_node_set(c, "interpolate", cw_val_bool(true));
      RIG[v].cam = c;
    }
    cw_node_set(c, "fov", cw_val_float(fov));
    if (nviews == 2) vw[v] = stacked ? (cw_view){ c, 0, 0.5f * v, 1, 0.5f } : (cw_view){ c, 0.5f * v, 0, 0.5f, 1 };
    else {
      int q = nviews == 3 ? Q3[v] : v;
      vw[v] = (cw_view){ c, Q[q][0], Q[q][1], 0.5f, 0.5f };
    }
  }
  cw_pipeline *pl = cw_pipeline_of(cw_scene_app_tree());
  cw_pipeline_set_views(pl, vw, nviews);
  cw_pipeline_set_view_hook(pl, view_lod, NULL);
  cw_pipeline_set_views_shared_post(pl, true); /* bloom, the grade and AA once over the laid-out views */
  scenery_light_crowd(true);
  cw_node post = find_type(G.scene, "PostProcess");
  if (cw_node_valid(post)) cw_node_set(post, "ssao", cw_val_bool(false));
  if (cw_node_valid(G.sun)) cw_node_set(G.sun, "shadow_size", cw_val_int(1024));
}

/* The graphics settings' share of derby's lighting, render only:
 *   volumetric fog (the floods' beams in the dust, the smoke palls) where the
 *   arena has haze, effects detail is Medium or more and one view fills the
 *   screen; otherwise the analytic fog and the beam cones, as before
 *   (splitscreen keeps it off: a grid per view would cost each view a whole
 *   fog pass);
 *   floodlight shadows by the Shadows setting: on Minimal (Very Low) and Low
 *   the directional stand-ins for Flood0 and Flood2 (flood_sun_apply; a spot
 *   that casts costs Low about 0.5 ms at 1080p on a Radeon 890M, a stand-in a
 *   tenth of that), two opposite corners on Medium, all four on High and Ultra. */
/* Minimal and Low shadows at night (the sun under the horizon): Flood0 and Flood2, the two ends of one long side, as
 * casting directional lights (scenery_flood_sun; by day the sun is the caster), so the cars are shaded from both ends and
 * stand on their shadows at every preset */
static void flood_sun_apply(void) {
  bool night = cw_node_valid(G.sun) && G.sun_known && !G.sun_up;
  bool on = night && cw_gfx_get(CW_GFX_SHADOWS) <= CW_GFX_SHADOWS_LOW;
  /* the sun under the horizon lights nothing, and a plain directional light made before the casting one would stop
   * three.c drawing lit materials (a kind's shadow casters come first): it steps aside while the flood stands in */
  if (cw_node_valid(G.sun)) cw_node_set(G.sun, "visible", cw_val_bool(!on));
  scenery_flood_sun(on ? 2 : 0);
}
static void apply_gfx(void) {
  cw_node fog = find_type(G.scene, "HeightFog");
  bool on = false;
  if (cw_node_valid(fog)) {
    cw_value d;
    bool hazy = cw_node_get_prop(fog, "density", &d) && d.f > 0;
    on = hazy && cw_gfx_get(CW_GFX_EFFECTS) >= CW_QUALITY_MEDIUM && nviews < 2;
    cw_node_set(fog, "volumetric", cw_val_bool(on));
  }
  fx_volumetric(on);
  scenery_volumetric(on);
  int sh = cw_gfx_get(CW_GFX_SHADOWS);
  scenery_flood_shadows(sh <= CW_GFX_SHADOWS_LOW ? 0 : sh == CW_GFX_SHADOWS_MEDIUM ? 2 : 4);
  flood_sun_apply();
}

static void start_bout(int arena, bool with_human) {
  load_scene(arena);
  line_up(with_human);
  const char *studio = getenv("DERBY_STUDIO"); /* dev aid: one car of kind n alone, for turntables */
  if (studio) {
    for (int i = 1; i < MAXC; i++) G.slot[i] = SLOT_OFF;
    G.kind[0] = atoi(studio) % CAR_KINDS;
    const char *col = getenv("DERBY_COLOUR");
    if (col) G.look[0].colour = (uint8_t)(atoi(col) % CAR_COLOURS);
  }
  const char *cpu = getenv("DERBY_CPU_ONLY");
  if (cpu && *cpu == '1') G.slot[0] = SLOT_ACE;
#ifdef DERBY_AUTOPILOT
  G.slot[0] = SLOT_ACE; /* the self-playing build: six CPUs, a short bout to a winner */
#endif
  assign_humans();
  memset(G.thr, 0, sizeof G.thr);
  memset(G.steer, 0, sizeof G.steer);
  bool on[MAXC];
  for (int i = 0; i < MAXC; i++) on[i] = G.slot[i] != SLOT_OFF;
  int secs = BOUT_SECS;
#ifdef DERBY_AUTOPILOT
  secs = 45;
#endif
  const char *bs = getenv("DERBY_SECS");
  if (bs && atoi(bs) > 0) secs = atoi(bs);
  sim_init(&G.S, on, G.kind, G.rule, secs, 1000u + (uint32_t)arena * 7919u);
  if (studio) sim_place(&G.S, 0, 0, 0, 0);
  for (int i = 0; i < MAXC; i++) ai_reset(&G.ai[i], i, (uint32_t)arena + 1);
#ifdef DERBY_RAM_TEST
  /* the ram gate's stage: car 2 parked broadside against the far wall, 16 m
   * dead ahead of yours (pinned: every ram lands), the rest of the field
   * parked at the near end */
  {
    float bz = AL - CAR_SPEC[G.S.c[1].kind].hw - 0.05f;
    sim_place(&G.S, 1, 0, bz, 1.5707963f);
    sim_place(&G.S, 0, 0, bz - 18, 0);
    for (int i = 2; i < MAXC; i++)
      if (G.S.c[i].on) sim_place(&G.S, i, -12 + (i - 2) * 7.0f, -24, 0);
  }
#endif
  int theme = G.info ? G.info->theme : 0;
  world_build(G.scene, &G.S, theme, G.look);
  /* (the floods ahead of the fires in the tree: volumetric fog lights the first eight) */
  scenery_build(G.scene, theme, G.info ? (uint32_t)G.info->seed : 1u);
  fx_init(G.scene, theme);
  int cols[MAXC];
  for (int i = 0; i < MAXC; i++) cols[i] = G.look[i].colour;
  fx_set_colours(cols);
  G.state = GS_BOUT;
  G.state_ticks = 0;
  views_setup();
  apply_gfx();
  music(false);
  crowd_bed(true);
  engines(true);
  G.beats = G.beat_held = G.duel_tick = G.rumbles = 0;
  G.beat = G.beat_cool = 0;
  G.duel = false, G.duel_k = G.swell = 0;
}

/* the title backdrop: the arena with the field idling in it */
static void enter_title(void) {
  engines(false);
  G.feed_ticks = 0;
  G.menu = 0;
  load_scene(1);
  line_up(true);
  bool on[MAXC];
  for (int i = 0; i < MAXC; i++) on[i] = G.slot[i] != SLOT_OFF;
  sim_init(&G.S, on, G.kind, RULE_LAST_RUNNING, BOUT_SECS, 5);
  world_build(G.scene, &G.S, 0, G.look);
  scenery_build(G.scene, 0, G.info ? (uint32_t)G.info->seed : 1u);
  fx_init(G.scene, 0);
  G.state = GS_TITLE;
  apply_gfx();
  G.state_ticks = 0;
  for (int i = 0; i < MAXC; i++) G.human_of[i] = -1;
  music(true);
  crowd_bed(true);
}

/* ── debug channel ──────────────────────────────────────────────────── */

static int pending_bout = -1;
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
/* sw hint_countdown 1: the controls hint only through the countdown and only
 * on a single screen, as the game once drew it (the newcomer gate's control) */
static int sw_hint_countdown, sw_no_handling_damage, sw_no_beat, sw_no_classes;
static void cmd_sw(int argc, const char **argv, cw_reply *r) {
  if (argc > 2 && !strcmp(argv[1], "hint_countdown")) sw_hint_countdown = atoi(argv[2]);
  /* no_classes 1: every body drives the same (the class gate's control) */
  if (argc > 2 && !strcmp(argv[1], "no_classes")) sw_no_classes = atoi(argv[2]), sim_no_classes(sw_no_classes);
  /* no_beat 1: a big hit runs on at full speed (the beat gate's control) */
  if (argc > 2 && !strcmp(argv[1], "no_beat")) sw_no_beat = atoi(argv[2]);
  /* no_handling_damage 1: hits leave the steering, grip and top speed alone (the pull gate's control) */
  if (argc > 2 && !strcmp(argv[1], "no_handling_damage")) sw_no_handling_damage = atoi(argv[2]), sim_no_handling_damage(sw_no_handling_damage);
  cw_reply_printf(r, "{\"hint_countdown\":%d,\"no_handling_damage\":%d,\"no_beat\":%d,\"no_classes\":%d}", sw_hint_countdown,
                  sw_no_handling_damage, sw_no_beat, sw_no_classes);
}
/* study opening <n> | study pull: bouts and test drives run without drawing (study.c), for the gates */
static void cmd_study(int argc, const char **argv, cw_reply *r) {
  if (argc > 1 && !strcmp(argv[1], "opening")) {
    static study_bout B[40];
    int n = argc > 2 ? atoi(argv[2]) : 20;
    n = n < 1 ? 1 : n > 40 ? 40 : n;
    study_opening(n, B);
    cw_reply_raw(r, "{\"bouts\":[");
    for (int b = 0; b < n; b++)
      cw_reply_printf(r, "%s{\"hp10\":%.1f,\"hunters10\":%.2f,\"wrecked\":%d}", b ? "," : "", B[b].hp10, B[b].hunters10, B[b].wrecked_tick);
    cw_reply_raw(r, "]}");
  } else if (argc > 1 && !strcmp(argv[1], "pull")) {
    static const char *const ZN[ZONES + 1] = { "none", "front", "rear", "left", "right" };
    cw_reply_raw(r, "{");
    for (int z = -1; z < ZONES; z++) {
      study_pull_result p;
      study_pull(z, &p);
      cw_reply_printf(r, "%s\"%s\":{\"turned\":%.2f,\"top\":%.2f,\"hp\":%.1f,\"bend\":%.4f,\"grip_l\":%.3f,\"grip_r\":%.3f}", z < 0 ? "" : ",",
                      ZN[z + 1], p.turned_deg, p.top, p.hp, p.bend, p.grip_l, p.grip_r);
    }
    cw_reply_raw(r, "}");
  } else if (argc > 1 && !strcmp(argv[1], "classes")) {
    int n = argc > 2 ? atoi(argv[2]) : 400, wins[CAR_KINDS], starts[CAR_KINDS];
    study_kind_drive d[CAR_KINDS];
    study_classes(n < 1 ? 1 : n > 1000 ? 1000 : n, wins, starts, d);
    cw_reply_raw(r, "{\"kinds\":[");
    for (int k = 0; k < CAR_KINDS; k++)
      cw_reply_printf(r, "%s{\"name\":\"%s\",\"class\":\"%s\",\"wins\":%d,\"starts\":%d,\"speed2\":%.2f,\"top\":%.2f,\"hp_after_hit\":%.1f}", k ? "," : "",
                      CAR_SPEC[k].name, CAR_CLASS[k].name, wins[k], starts[k], d[k].speed2, d[k].top, d[k].hp_after_hit);
    cw_reply_raw(r, "]}");
  } else
    cw_reply_error(r, "study opening <n> | study pull | study classes <n>");
}
/* the controls hint stays up from the countdown until that player gives it
 * gas (or ten seconds into the bout): a newcomer sees which button drives */
static bool show_controls(int player) {
  const sim *S = &G.S;
  if (S->phase == PH_COUNTDOWN) return true;
  if (sw_hint_countdown) return false;
  return S->phase == PH_GO && S->phase_ticks < 600 && player >= 0 && player < 4 && !G.gassed[player];
}
/* players n: n humans in the next bout (the splitscreen gate; extra players without pads idle) */
static void cmd_players(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 1;
  n = n < 1 ? 1 : n > 4 ? 4 : n;
  for (int p = 1; p < 4; p++) G.joined[p] = p < n;
  G.players = n;
  if (r) cw_reply_printf(r, "{\"players\":%d}", n);
}
/* dev aid (key art): the DERBY_CAM_* look and DERBY_NO_HUD for a cart, which has no environment before its first frame */
static int keyart_car = -1, keyart_nohud;
static float keyart_a = 0.8f, keyart_d = 5.2f, keyart_h = 1.7f;
static void cmd_keyart(int argc, const char **argv, cw_reply *r) {
  if (argc < 2) { cw_reply_error(r, "usage: keyart <car> [angle distance height] [hud 0|1] | keyart off"); return; }
  if (!strcmp(argv[1], "off")) keyart_car = -1, keyart_nohud = 0;
  else {
    keyart_car = atoi(argv[1]) % MAXC;
    if (argc > 4) keyart_a = (float)atof(argv[2]), keyart_d = (float)atof(argv[3]), keyart_h = (float)atof(argv[4]);
    keyart_nohud = argc > 5 && atoi(argv[5]) == 0;
  }
  cw_reply_printf(r, "{\"car\":%d,\"hud\":%d}", keyart_car, !keyart_nohud);
}
static void cmd_bout(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 1;
  pending_bout = n < 1 ? 1 : n;
  cw_reply_printf(r, "{\"bout\":%d}", pending_bout);
}

/* ── the app ────────────────────────────────────────────────────────── */

static cw_font font_hud, font_title, font_small;

static double wall_now(void);
/* a graphics setting changed: thin or thicken the crowd */
static void quality_changed(cw_quality q, void *u) {
  (void)q, (void)u;
  scenery_density(cw_quality_density());
  if (cw_node_valid(G.scene)) apply_gfx();
}

static void init(void *u) {
  (void)u;
  double t_init = wall_now();
  save_check();
  cw_quality_on_change(quality_changed, NULL);
  cw_quality_full_resolution(true); /* Auto gives up detail, never pixels */
  cw_quality_restore(gfx_saved()); /* the player's graphics settings, if they chose any; else Auto */
  cw_text_scale_restore(SAVE.text_scale); /* the player's Text size and Text background */
  cw_text_backdrop_set(SAVE.text_bg <= CW_TEXT_BACKDROP_PANEL ? (cw_text_backdrop)SAVE.text_bg : CW_TEXT_BACKDROP_OFF);
  cw_quality_auto(true, 0);
  scenery_density(cw_quality_density());
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("derby: input.cfg had errors (see above)");
  cw_input_commit_defaults();
  G.a_fwd = cw_action_find("thr_fwd");
  G.a_back = cw_action_find("thr_back");
  G.a_left = cw_action_find("steer_l");
  G.a_right = cw_action_find("steer_r");
  G.a_hb = cw_action_find("handbrake");
  G.a_horn = cw_action_find("horn");
  G.a_look = cw_action_find("look");
  G.a_start = cw_action_find("start");
  G.a_esc = cw_action_find("back");
  G.a_settings = cw_action_find("settings");
  G.m_left = cw_action_find("m_left");
  G.m_right = cw_action_find("m_right");
  G.m_up = cw_action_find("m_up");
  G.m_down = cw_action_find("m_down");
  G.m_ok = cw_action_find("m_ok");
  /* on-screen touch controls (logical window coordinates): a floating
   * steering stick on the left half; GAS, BRAKE and HANDBRAKE on the right */
  {
    int ww, wh;
    cw_window_size(&ww, &wh);
    float W = (float)ww, H = (float)wh, k = H / 540.0f;
    TOUCH_STICK = cw_touch_add_stick(0, H * 0.3f, W * 0.45f, H * 0.7f, 64 * k, true);
    TOUCH_GAS = cw_touch_add_button(W - 170 * k, H - 230 * k, 150 * k, 200 * k);
    TOUCH_BRAKE = cw_touch_add_button(W - 330 * k, H - 150 * k, 140 * k, 120 * k);
    TOUCH_HB = cw_touch_add_button(W - 330 * k, H - 290 * k, 140 * k, 110 * k);
    char b[24];
    snprintf(b, sizeof b, "touch:%d.left", TOUCH_STICK);
    cw_action_bind_text(G.a_left, b);
    snprintf(b, sizeof b, "touch:%d.right", TOUCH_STICK);
    cw_action_bind_text(G.a_right, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_GAS);
    cw_action_bind_text(G.a_fwd, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_BRAKE);
    cw_action_bind_text(G.a_back, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_HB);
    cw_action_bind_text(G.a_hb, b);
  }
  cw_action_set_deadzone(G.a_left, 0.1f);
  cw_action_set_deadzone(G.a_right, 0.1f);
  cw_action_set_deadzone(G.a_fwd, 0.05f);
  cw_action_set_deadzone(G.a_back, 0.05f);
  audio_init();
  /* SDF with room for the outlines (up to 7 % of the size, plus the shadow's spread) */
  const cw_font_desc sdf = { .mode = CW_FONT_SDF, .sdf_padding = 8 };
  font_title = cw_font_load("fonts/BlackOpsOne-Regular.ttf", &sdf);
  font_hud = cw_font_load("fonts/RussoOne-Regular.ttf", &sdf);
  font_small = cw_font_load("fonts/Oswald-VF.ttf", &sdf);
  /* Prepare printable English UI glyphs while loading. SDF glyph generation
   * must not interrupt gameplay when a new score or result first appears. */
  const cw_font ui_fonts[] = {font_title, font_hud, font_small};
  for (unsigned f = 0; f < sizeof ui_fonts / sizeof ui_fonts[0]; f++)
    cw_font_preload_range(ui_fonts[f], 32, 126);

  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("bout.phase", &G.phase);
  cw_debug_i32("bout.clock", &G.clock);
  cw_debug_i32("bout.alive", &G.alive);
  cw_debug_i32("bout.winner", &G.winner);
  cw_debug_field("cars.pos", G.pos, CW_DBG_I32, MAXC * 2);
  cw_debug_field("cars.hp", G.hp, CW_DBG_I32, MAXC);
  cw_debug_field("cars.dents", G.dents, CW_DBG_I32, MAXC);
  cw_debug_field("cars.wrecked", G.wrecked, CW_DBG_I32, MAXC);
  cw_debug_field("cars.slot", G.slots, CW_DBG_I32, MAXC);
  cw_debug_field("cars.points", G.points, CW_DBG_I32, MAXC);
  cw_debug_field("players.thr", G.thr, CW_DBG_I32, 4);
  cw_debug_field("players.steer", G.steer, CW_DBG_I32, 4);
  cw_debug_field("hint.frames", G.hint_frames, CW_DBG_I32, 4);
  cw_debug_i32("beat.count", &G.beats);
  cw_debug_i32("beat.held", &G.beat_held);
  cw_debug_i32("bout.duel_tick", &G.duel_tick);
  cw_debug_i32("count.rumbles", &G.rumbles);
  cw_debug_i32("count.crashes", &G.crashes);
  cw_debug_i32("count.walls", &G.walls);
  cw_debug_i32("count.wrecks", &G.wrecks);
  cw_debug_i32("count.max_hit", &G.max_hit);
  cw_debug_i32("replay.frame", &G.replay_f);
  cw_debug_i32("replay.k", &G.replay_k);
  cw_debug_i32("debris.count", &debris_count_dbg);
  cw_debug_i32("debris.float_mm", &debris_float_dbg);
  cw_debug_field("cars.mud", G.mud, CW_DBG_I32, MAXC);
  cw_debug_i32("cars.mud_verts", &car_mud_verts);
  cw_debug_i32("count.flips", &G.flips);
  cw_debug_f32("car0.wheel_yaw", &G.wheel_yaw0);
  cw_debug_f32("car0.yaw_rate", &G.yaw_rate0);
  cw_debug_i32("check.steer_agrees", &G.steer_agrees);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("cam.lookback", &G.lookback);
  cw_debug_u32("sim.hash", &G.hash);
  cw_debug_i32("input.mode", &input_mode); /* 0 mouse, 1 pad, 2 keys, 3 touch */
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  {
    extern uint32_t tex_each[TEX_KINDS + 2];
    cw_debug_field("tex.each", tex_each, CW_DBG_U32, TEX_KINDS + 2);
  }
  cw_debug_u32("scenery.hash", &scenery_hash);
  cw_debug_bytes("save.bytes", &SAVE, sizeof SAVE);
  cw_debug_command("bout", "bout [arena]: a quick match now", cmd_bout);
  cw_debug_command("keyart", "keyart <car> [angle distance height] [hud 0|1] | keyart off: key art (a three-quarter look at a car, the HUD off)", cmd_keyart);
  cw_debug_command("bind", "bind <action> <bindings>: replace an action's bindings (binding text, as input.cfg writes it)", cmd_bind);
  cw_debug_command("players", "players <1..4>: humans in the next bout (splitscreen)", cmd_players);
  cw_debug_command("sw", "sw hint_countdown|no_handling_damage|no_beat|no_classes <0|1>: the old behaviour back (the gates' controls)", cmd_sw);
  cw_debug_command("study", "study opening <n> | study pull | study classes <n>: bouts and test drives run without drawing (the gates)", cmd_study);
  cw_debug_i32("views", &G.views);
  G.state_hash = 2166136261u;
  G.rule = RULE_LAST_RUNNING;
  G.players = 1;
  if (getenv("DERBY_WARM_TIMES")) fprintf(stderr, "init: %.0f ms\n", (wall_now() - t_init) * 1000); /* dev aid */
}

static double wall_now(void) { /* dev timing only (DERBY_WARM_TIMES) */
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* the warm-up (cw_app_desc.warmup), a step a frame under the engine's
 * loading screen: the texture sets, then the title field's cars one by one,
 * then the arena itself; the engine's shader phase follows */
static int warm_tex, warm_car;
static bool warm_crack;
static bool warmup_step(void);
static bool warmup(void *u) {
  (void)u;
  double t0 = wall_now();
  bool done = warmup_step();
  if (getenv("DERBY_WARM_TIMES")) fprintf(stderr, "warm step: %.0f ms\n", (wall_now() - t0) * 1000); /* dev aid */
  return done;
}
static bool warmup_step(void) {
  const int total = TEX_KINDS + MAXC + 1;
  if (warm_tex < TEX_KINDS) { /* the texture sets, a band of rows a frame */
    if (tex_warm(warm_tex)) warm_tex++;
    cw_loading_set(warm_tex, total, CW_LOAD_TEXTURES);
    return false;
  }
  if (warm_car < MAXC) { /* the windscreen crack, then the title field's cars, one a frame */
    if (warm_car == 0 && !warm_crack) {
      warm_crack = tex_crack_warm();
      if (warm_crack) line_up(true);
      return false;
    }
    world_prebuild_car(warm_car, G.kind[warm_car], &G.look[warm_car]);
    warm_car++;
    cw_loading_set(TEX_KINDS + warm_car, total, CW_LOAD_MESHES);
    return false;
  }
  enter_title();
  const char *pls = getenv("DERBY_PLAYERS"); /* dev aid: n humans (splitscreen) */
  if (pls) {
    const char *argv2[2] = { "players", pls };
    cmd_players(2, argv2, NULL);
  }
  const char *a = getenv("DERBY_BOUT"); /* dev aid: straight into a bout */
  if (a && atoi(a) >= 1) start_bout(atoi(a), true);
  cw_loading_set(total, total, CW_LOAD_SCENE);
  return true;
}

/* ── the player's input ─────────────────────────────────────────────── */

static int8_t q127(float v) { return (int8_t)(cw_clampf(v, -1, 1) * 127.0f); }

static cin human_cin(int slot) {
  int p = G.pl[G.human_of[slot]];
  cin in = { 0 };
  in.thr = q127(cw_action_strength(p, G.a_fwd) - cw_action_strength(p, G.a_back));
  in.steer = q127(cw_input_axis(p, G.a_left, G.a_right));
  if (cw_action_pressed(p, G.a_hb)) in.btn |= CB_HANDBRAKE;
  if (cw_action_pressed(p, G.a_horn)) in.btn |= CB_HORN;
  return in;
}

/* ── camera (fixed tick: identical on every host) ─────────────────── */

static void spring3(t3_vec3 *x, t3_vec3 *v, t3_vec3 target, float w) {
  const float dt = 1.0f / 60;
  t3_vec3 a = { (target.x - x->x) * w * w - v->x * 2 * w, (target.y - x->y) * w * w - v->y * 2 * w, (target.z - x->z) * w * w - v->z * 2 * w };
  v->x += a.x * dt, v->y += a.y * dt, v->z += a.z * dt;
  x->x += v->x * dt, x->y += v->y * dt, x->z += v->z * dt;
}
static float wrap_pi(float a) {
  while (a > 3.1415927f) a -= 6.2831853f;
  while (a < -3.1415927f) a += 6.2831853f;
  return a;
}



/* the chase camera: behind and above the car along its heading (its travel
 * when it slides backwards, so a spin does not whip the view round); held
 * Y / C looks back over the boot. Pulled in a little in a split view. */
/* the highest roof (or side, for a car lying on it) of the cars an eye would
 * sit inside or just over (1.5 m round them, up to 1.8 m above): -1 for none */
static float pile_top(t3_vec3 eye, int skip) {
  float over = -1;
  for (int i = 0; i < MAXC; i++) {
    const car *o = &G.S.c[i];
    if (i == skip || !o->on) continue;
    const car_spec *ks = &CAR_SPEC[o->kind];
    float f, sd, top = o->y + (o->up_y > 0.5f ? ks->height : 2 * ks->hw);
    car_local(o, eye.x, eye.z, &f, &sd);
    if (fabsf(f) < ks->hl + 1.5f && fabsf(sd) < ks->hw + 1.5f && eye.y < top + 1.8f && top > over) over = top;
  }
  return over;
}

static void chase_tick(rig *r, int me, t3_vec3 *eye_out, t3_vec3 *at_out, bool lookback) {
  const car *c = &G.S.c[me];
  float want = c->h;
  float v = car_speed(c), speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
  if (v < -3) want = cw_atan2(-c->vx, -c->vz) + 3.1415927f; /* reversing hard: stay behind the travel */
  if (!r->live) r->yaw = want, r->yaw_v = 0;
  const float dt = 1.0f / 60, wy = 4.2f;
  float diff = wrap_pi(want - r->yaw);
  r->yaw_v += (wy * wy * diff - 2 * wy * r->yaw_v) * dt;
  r->yaw = wrap_pi(r->yaw + r->yaw_v * dt);
  float yaw = r->yaw + (lookback ? 3.1415927f : 0);
  float sy, cy;
  cw_sincos(yaw, &sy, &cy);
  float k = nviews > 1 ? 0.9f : 1;
  /* low and heavy: close behind the boot, a little above the roof */
  float dist = (6.3f + speed * 0.05f) * k, high = (2.15f + speed * 0.012f) * k;
  /* the last two: the camera eases back and up a little, so the duel and the room round it both read */
  dist *= 1 + 0.18f * G.duel_k, high *= 1 + 0.15f * G.duel_k;
  t3_vec3 look = { c->x + sy * 5, c->y + 1.0f, c->z + cy * 5 };
  /* in a pile the spot behind the boot is another car: come in closer behind
   * this one until it is clear, and only when nothing is clear, go up high
   * over the pile (never just over a roof) */
  t3_vec3 eye = { c->x - sy * dist, c->y + high, c->z - cy * dist };
  float over = -1;
  for (float d = dist; d >= 3.4f; d -= 0.5f) {
    eye = (t3_vec3){ c->x - sy * d, c->y + high, c->z - cy * d };
    if ((over = pile_top(eye, me)) < 0) break;
  }
  if (over >= 0) eye = (t3_vec3){ c->x - sy * dist, over + 2.2f, c->z - cy * dist };
  if (!r->live) {
    r->eye = eye, r->look = look;
    r->eye_v = r->look_v = (t3_vec3){ 0, 0, 0 };
    r->live = true;
  }
  if (lookback) r->eye = eye, r->look = look; /* a mirror is instant */
  spring3(&r->eye, &r->eye_v, eye, 5.5f);
  spring3(&r->look, &r->look_v, look, 7.5f);
  *eye_out = r->eye;
  *at_out = r->look;
  /* (the camera never shakes: the engine and the hits are felt in the pad, rumble()) */
}

/* who to watch when no human is driving (or yours is a wreck): the car in
 * the thick of it, held for a few seconds at a time */
static int spectate_pick(rig *r) {
  const sim *s = &G.S;
  if (r->spectate >= 0 && !s->c[r->spectate].wrecked && --r->spectate_ticks > 0) return r->spectate;
  int best = -1;
  float bs = -1;
  for (int i = 0; i < MAXC; i++) {
    const car *c = &s->c[i];
    if (!c->on || c->wrecked) continue;
    float near = 0;
    for (int j = 0; j < MAXC; j++) {
      const car *o = &s->c[j];
      if (j == i || !o->on || o->wrecked) continue;
      float dx = o->x - c->x, dz = o->z - c->z, d = dx * dx + dz * dz;
      near += 1 / (1 + d * 0.05f);
    }
    float score = near + sqrtf(c->vx * c->vx + c->vz * c->vz) * 0.03f;
    if (score > bs) bs = score, best = i;
  }
  if (best != r->spectate) r->live = false;
  r->spectate = best;
  r->spectate_ticks = 360;
  return best;
}

static int car_of_view(int v) {
  for (int i = 0; i < MAXC; i++)
    if (G.human_of[i] == v) return i;
  return -1;
}

static void rig_tick(int v) {
  rig *r = &RIG[v];
  if (!cw_node_valid(r->cam)) return;
  t3_vec3 at = { 0, 1, 0 }, eye = { 0, 12, -40 };
  float k_eye = 0.12f, k_at = 0.2f;
  const sim *s = &G.S;
  int me = car_of_view(v);
  bool lookback = me >= 0 && G.state == GS_BOUT && cw_action_pressed(G.pl[G.human_of[me]], G.a_look);
  if (v == 0) G.lookback = lookback;
  if (G.state == GS_TITLE) {
    /* a slow crane round the field, low and close */
    float a = G.ticks * 0.0018f + 0.6f;
    at = (t3_vec3){ 0, 0.8f, 0 };
    eye = (t3_vec3){ 17 * cw_sin(a), 3.6f + 1.2f * cw_sin(a * 0.7f), 21 * cw_cos(a) };
    k_eye = k_at = 0.04f;
  } else if (G.state == GS_RESULTS && G.replay_f >= 0 && G.replay_f < replay_frames()) {
    /* the replay: a slow walk round the hit, low, from the side the first car came from */
    const replay_hit *h = replay_the_hit();
    float a = (float)G.replay_f * 0.003f + v * 1.5707963f + 0.7f;
    at = (t3_vec3){ h->at.x, 0.9f, h->at.z };
    eye = (t3_vec3){ h->at.x + 8.5f * cw_sin(a), 2.4f, h->at.z + 8.5f * cw_cos(a) };
    float over = pile_top(eye, h->a);
    if (over >= 0) eye.y = over + 2.2f;
    if (G.replay_f == 0) r->snap = true;
    k_eye = k_at = 0.2f;
  } else if (G.state == GS_RESULTS) {
    int w = s->winner >= 0 ? s->winner : 0;
    const car *c = &s->c[w];
    float a = G.state_ticks * 0.004f + v * 1.5707963f;
    at = (t3_vec3){ c->x, c->y + 0.9f, c->z };
    eye = (t3_vec3){ c->x + 7 * cw_sin(a), c->y + 2.6f, c->z + 7 * cw_cos(a) };
    float over = pile_top(eye, w);
    if (over >= 0) eye.y = over + 2.2f;
    k_eye = k_at = 0.05f;
  } else {
    int who = me >= 0 && !s->c[me].wrecked ? me : -1;
    if (me >= 0 && s->c[me].wrecked && s->c[me].wreck_ticks < 150) {
      /* yours is done: a slow orbit round the wreck first */
      const car *c = &s->c[me];
      float a = c->wreck_ticks * 0.01f + c->h;
      at = (t3_vec3){ c->x, c->y + 0.8f, c->z };
      eye = (t3_vec3){ c->x + 8 * cw_sin(a), c->y + 3.2f, c->z + 8 * cw_cos(a) };
      float over = pile_top(eye, me);
      if (over >= 0) eye.y = over + 2.2f;
      k_eye = k_at = 0.06f;
      r->live = false;
    } else {
      if (who < 0) who = spectate_pick(r);
      if (who >= 0) {
        chase_tick(r, who, &eye, &at, lookback);
        k_eye = k_at = 1;
      }
    }
  }
  /* dev aid: DERBY_CAM_CAR=n, a three-quarter look at car n (DERBY_CAM_A angle, _D distance, _H height); read once */
  static int cam_car = -2;
  static float cam_a = 0.8f, cam_d = 5.2f, cam_h = 1.7f;
  if (cam_car == -2) {
    const char *cc = getenv("DERBY_CAM_CAR"), *ca = getenv("DERBY_CAM_A"), *cd = getenv("DERBY_CAM_D"), *ch = getenv("DERBY_CAM_H");
    cam_car = cc ? atoi(cc) % MAXC : -1;
    if (ca) cam_a = (float)atof(ca);
    if (cd) cam_d = (float)atof(cd);
    if (ch) cam_h = (float)atof(ch);
  }
  { /* dev aid: DERBY_CAM_DEBRIS=1, a low look at the newest part lying on the dirt (the grounding sheet) */
    static int cam_deb = -1;
    if (cam_deb < 0) cam_deb = getenv("DERBY_CAM_DEBRIS") != NULL;
    t3_vec3 d;
    if (cam_deb && v == 0 && debris_resting(&d)) {
      at = (t3_vec3){ d.x, d.y, d.z };
      eye = (t3_vec3){ d.x + 2.6f, 1.1f, d.z + 2.6f };
      r->snap = true;
    }
  }
  if (keyart_car >= 0) cam_car = keyart_car, cam_a = keyart_a, cam_d = keyart_d, cam_h = keyart_h;
  if (cam_car >= 0 && v == 0) {
    const car *c = &s->c[cam_car];
    float a = c->h + cam_a;
    at = (t3_vec3){ c->x, c->y + 0.75f, c->z };
    eye = (t3_vec3){ c->x + cam_d * cw_sin(a), cam_h, c->z + cam_d * cw_cos(a) };
    r->snap = true;
  }
  if (r->snap) {
    r->cam_at = at;
    r->cam_eye = eye;
  } else {
    r->cam_at = (t3_vec3){ r->cam_at.x + (at.x - r->cam_at.x) * k_at, r->cam_at.y + (at.y - r->cam_at.y) * k_at, r->cam_at.z + (at.z - r->cam_at.z) * k_at };
    r->cam_eye = (t3_vec3){ r->cam_eye.x + (eye.x - r->cam_eye.x) * k_eye, r->cam_eye.y + (eye.y - r->cam_eye.y) * k_eye,
                            r->cam_eye.z + (eye.z - r->cam_eye.z) * k_eye };
  }
  bool snapped = r->snap;
  r->snap = false;
  t3_vec3 e = r->cam_eye;
  /* no shake: hits are felt in the pad (rumble) and seen in the sparks, the dust ring and the beat */
  if (e.y < 0.6f) e.y = 0.6f;
  cw_node3d_snapshot(r->cam);
  cw_node3d_set_position(r->cam, e);
  cw_node3d_look_at(r->cam, r->cam_at, (t3_vec3){ 0, 1, 0 });
  if (snapped) cw_node3d_snapshot(r->cam);
}

static void camera_tick(void) {
  int n = G.state == GS_TITLE ? 1 : nviews;
  for (int v = 0; v < n; v++) rig_tick(v);
  /* the sun's shadow box follows the play (the middle of the views) */
  if (cw_node_valid(G.sun)) {
    const t3_mat4 *m = cw_node3d_global_transform(G.sun);
    float fx = -m->e[8], fy = -m->e[9], fz = -m->e[10], l = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= l, fy /= l, fz /= l;
    t3_vec3 c = { 0, 0, 0 };
    for (int v = 0; v < n; v++) c.x += (RIG[v].cam_at.x * 0.6f + RIG[v].cam_eye.x * 0.4f) / n, c.z += (RIG[v].cam_at.z * 0.6f + RIG[v].cam_eye.z * 0.4f) / n;
    cw_node3d_set_position(G.sun, (t3_vec3){ c.x - fx * 70, c.y - fy * 70, c.z - fz * 70 });
    /* a sun under the horizon (the dusk arena: the sky gives it no energy)
     * lights nothing: no shadow map for it, and no place among the
     * volumetric fog's lights */
    cw_value e;
    bool up = cw_node_get_prop(G.sun, "energy", &e) && e.f > 0;
    if (up != G.sun_up || !G.sun_known) {
      G.sun_up = up, G.sun_known = true;
      cw_node_set(G.sun, "shadow", cw_val_bool(up));
      cw_node_set(G.sun, "fog_energy", cw_val_float(up ? 1 : 0));
      flood_sun_apply();
    }
    scenery_flood_sun_follow(c);
  }
}

/* ── layout shared by the update (mouse hit tests) and the HUD ─────── */

static cw_rect title_button(int i, float W, float H); /* (laid out with the title, by measured text) */
static cw_rect title_car_drawn;                          /* the car line, where the title last drew it */
static cw_rect pause_row(int i, float W, float H);    /* (laid out with the pause screen) */
#define PAUSE_N 5 /* RESUME, GRAPHICS, TEXT SIZE, TEXT BACKGROUND, QUIT TO TITLE */
/* the on-screen pause button (mouse and touch), top left */
static cw_rect pause_rect(float W, float H) {
  (void)W;
  float s = H / 540.0f;
  return cw_rect_make(10 * s, 10 * s, 64 * s, 44 * s);
}
static bool in_rect(cw_vec2 m, cw_rect r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }

/* ── the fixed tick ─────────────────────────────────────────────────── */

/* a human's car took a knock: their pad thumps (heavy motor with the hit, a light buzz on top), never the camera */
static void rumble(int car_i, float k, float scale) {
  if (car_i < 0 || car_i >= MAXC || G.human_of[car_i] < 0 || G.human_of[car_i] >= 4) return;
  int p = G.pl[G.human_of[car_i]];
  float lo = fminf(1, k * 0.07f) * scale, hi = fminf(0.6f, k * 0.035f) * scale;
  if (lo < 0.05f) return;
  cw_input_rumble(p, lo, hi, (int)fminf(380, 90 + k * 14));
  G.rumbles++;
}
/* the crowd's roar rises with the size of a hit and settles over a couple of seconds */
static void crowd_swell(float k) {
  if (k > G.swell) G.swell = fminf(1, k);
}

static void sim_sounds(void) {
  const sim *s = &G.S;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    switch (e->kind) {
    case EV_CRASH: {
      float k = e->k;
      rumble(e->a, k, 1), rumble(e->b, k, 1);
      if (k > 5) crowd_swell((k - 5) / 10);
      if (k > 11) sfx(SFX_CHEER, fminf(0.8f, 0.3f + (k - 11) * 0.05f));
      if (k > 7) sfx_at(SFX_CRUNCH_BIG, e->at, fminf(1, 0.6f + k * 0.03f), 1.05f - fminf(0.25f, k * 0.01f));
      else if (k > 2.5f) sfx_at(SFX_CRUNCH, e->at, 0.4f + k * 0.07f, 1.1f - k * 0.02f);
      else sfx_at(SFX_THUD, e->at, 0.3f + k * 0.1f, 1);
      if (k > 8) sfx_at(SFX_GLASS, e->at, 0.6f, 1);
      break;
    }
    case EV_WALL: sfx_at(e->k > 5 ? SFX_CRUNCH : SFX_THUD, e->at, 0.4f + fminf(0.5f, e->k * 0.05f), 0.85f), rumble(e->a, e->k, 0.8f); break;
    case EV_LAND:
      sfx_at(e->k > 6 ? SFX_CRUNCH : SFX_THUD, e->at, 0.4f + fminf(0.5f, e->k * 0.05f), 0.8f), rumble(e->a, e->k, 0.7f);
      if (e->k > 6) crowd_swell(0.6f);
      break;
    case EV_RIGHT: sfx_at(SFX_THUD, e->at, 0.35f, 1.2f); break;
    case EV_SCRAPE: sfx_at(SFX_SCRAPE, e->at, fminf(0.7f, e->k * 0.06f), 0.9f + fminf(0.3f, e->k * 0.02f)); break;
    case EV_WRECK: {
      sfx_at(SFX_BOOM, e->at, 1, 1);
      rumble(e->a, 14, 1), rumble(e->b, 6, 1);
      crowd_swell(1);
      sfx(SFX_CHEER, 0.9f);
      if (e->b >= 0) snprintf(G.feed, sizeof G.feed, "#%d %s WRECKED #%d %s!", G.look[e->b].number, driver(e->b), G.look[e->a].number, driver(e->a));
      else snprintf(G.feed, sizeof G.feed, "#%d %s IS DONE FOR!", G.look[e->a].number, driver(e->a));
      G.feed_ticks = 200;
      break;
    }
    case EV_HORN: sfx_at(e->a % 2 ? SFX_HORN2 : SFX_HORN, e->at, 0.8f, 1); break;
    case EV_BEEP: sfx(SFX_BEEP, 0.8f); break;
    case EV_GO: sfx(SFX_GO, 1); break;
    case EV_OVER: sfx(SFX_CHEER, 1); break;
    default: break;
    }
  }
}

#define BEAT_K 9.0f    /* tonne m/s: a hit this hard on a human's car gets the beat */
#define BEAT_TICKS 6   /* 100 ms at a third of the speed */
static void big_hit_beat(void) {
  if (sw_no_beat || G.beat_cool > 0) return;
  const sim *s = &G.S;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    if (e->kind != EV_CRASH || e->k < BEAT_K) continue;
    if (G.human_of[e->a] < 0 && G.human_of[e->b] < 0) continue;
    G.beat = BEAT_TICKS;
    G.beat_cool = 90;
    G.beats++;
    return;
  }
}
/* the last two: a call over the tannoy, the crowd up, the music up, the chase camera eased back */
static void duel_check(void) {
  const sim *s = &G.S;
  if (G.duel || s->rule != RULE_LAST_RUNNING || s->phase != PH_GO || s->ncars <= 2 || s->alive != 2) return;
  G.duel = true;
  G.duel_tick = s->tick;
  crowd_swell(1);
  sfx(SFX_CHEER, 1);
  snprintf(G.feed, sizeof G.feed, "LAST TWO STANDING!");
  G.feed_ticks = 220;
}

static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  G.ticks++;
  G.state_ticks++;
  read_mouse();
  for (int i = 0; i < SFX_COUNT; i++)
    if (A.cooldown[i] > 0) A.cooldown[i]--;
  if (G.feed_ticks > 0) G.feed_ticks--;
  G.swell *= 0.985f;
  G.duel_k += ((G.duel && G.state != GS_TITLE ? 1.0f : 0.0f) - G.duel_k) * 0.02f;
  if (A.on && !CW_HANDLE_IS_NULL(A.crowd)) cw_voice_set_volume(A.crowd, 1 + 1.4f * G.swell + 0.4f * G.duel_k);
  if (A.on) cw_bus_set_volume(A.music_bus, 0.28f * (1 + 0.5f * G.duel_k));
  cw_vec2 cs = cw_r2d_canvas_size(), m = mouse_canvas();
  bool confirm = any_pressed(G.m_ok) || any_pressed(G.a_start), back = any_pressed(G.a_esc);
  int dy = (any_pressed(G.m_down) ? 1 : 0) - (any_pressed(G.m_up) ? 1 : 0);
  switch (G.state) {
  case GS_TITLE: {
    /* pads 2-4 join with south / Start and leave with east; the menu is P1's */
    for (int p = 1; p < 4; p++) {
      if (cw_input_player_pad(p) < 0 || !cw_input_raw()->pads[cw_input_player_pad(p)].connected) continue;
      if (!G.joined[p] && (cw_action_just_pressed(p, G.m_ok) || cw_action_just_pressed(p, G.a_start))) G.joined[p] = true, sfx(SFX_SELECT, 0.7f);
      else if (G.joined[p] && cw_action_just_pressed(p, G.a_esc)) G.joined[p] = false, sfx(SFX_MOVE, 0.5f);
    }
    G.players = 1 + G.joined[1] + G.joined[2] + G.joined[3];
    confirm = cw_action_just_pressed(0, G.m_ok) || cw_action_just_pressed(0, G.a_start);
    dy = (cw_action_just_pressed(0, G.m_down) ? 1 : 0) - (cw_action_just_pressed(0, G.m_up) ? 1 : 0);
    if (dy) G.menu = (G.menu + dy + 2) % 2, sfx(SFX_MOVE, 0.5f);
    int hover = -1;
    for (int i = 0; i < 2; i++)
      if (in_rect(m, title_button(i, cs.x, cs.y))) hover = i;
    if (hover >= 0 && mouse_moved) G.menu = hover; /* moving the mouse is using it */
    /* your car: left and right change it, a click or a tap on its line steps it on (kept in the save) */
    int cdx = (cw_action_just_pressed(0, G.m_right) ? 1 : 0) - (cw_action_just_pressed(0, G.m_left) ? 1 : 0);
    if (mouse_clicked && in_rect(m, title_car_drawn)) cdx = 1;
    if (cdx && G.state_ticks > 20) {
      SAVE.kind = (uint8_t)((SAVE.kind + cdx + CAR_KINDS) % CAR_KINDS);
      sfx(SFX_MOVE, 0.6f);
      save_write();
    }
    bool go = G.state_ticks > 20 && (confirm || (mouse_clicked && hover >= 0));
    if (go && mouse_clicked && hover >= 0) G.menu = hover;
    if (go) {
      sfx(SFX_SELECT, 1);
      G.rule = G.menu == 0 ? RULE_LAST_RUNNING : RULE_POINTS;
      start_bout(1, true);
    }
    break;
  }
  case GS_BOUT: {
    /* the big-hit beat: for BEAT_TICKS after a big hit on a human's car the bout steps one tick in three (the sound
     * runs on, the camera only follows), so the hit lands and is seen; the held ticks fire no events */
    bool held = G.beat > 0 && (G.beat-- % 3) != 0;
    if (G.beat_cool > 0) G.beat_cool--;
    fx_slow(G.beat > 0);
    if (held) {
      G.S.ev_n = 0;
      G.beat_held++;
    } else {
      cin in[MAXC] = { 0 };
      for (int i = 0; i < MAXC; i++) {
        if (G.slot[i] == SLOT_HUMAN) in[i] = human_cin(i);
        else if (G.slot[i] >= SLOT_ROOKIE) in[i] = ai_think(&G.S, i, G.slot[i] - SLOT_ROOKIE, &G.ai[i]);
#ifdef DERBY_RAM_TEST
        if (i >= 1) in[i] = (cin){ 0 }; /* the stage holds still: the parked car waits to be wrecked */
#endif
      }
      sim_step(&G.S, in);
      replay_record(&G.S);
      for (int i = 0; i < MAXC; i++)
        if (G.human_of[i] >= 0 && G.human_of[i] < 4) {
          int h = G.human_of[i];
          G.thr[h] = in[i].thr, G.steer[h] = in[i].steer;
          if (G.S.phase == PH_COUNTDOWN) G.gassed[h] = false, G.hint_frames[h] = G.S.phase_ticks ? G.hint_frames[h] : 0;
          else if (in[i].thr > 0) G.gassed[h] = true;
        }
      static FILE *rec; /* dev aid: DERBY_RECORD=file logs every tick's input (tools/make-gate-script.mjs) */
      static bool rec_checked;
      if (!rec_checked) {
        rec_checked = true;
        if (getenv("DERBY_RECORD")) rec = fopen(getenv("DERBY_RECORD"), "w");
      }
      if (rec) fprintf(rec, "%d %d %d %d\n", G.S.tick, in[0].thr, in[0].steer, in[0].btn), fflush(rec);
      fx_events(&G.S);
      sim_sounds();
      big_hit_beat();
      duel_check();
    }
    if (G.S.phase == PH_OVER && G.S.phase_ticks > 200) {
      G.state = GS_RESULTS;
      G.state_ticks = 0;
      replay_finish();
      G.replay_f = replay_have() ? 0 : -1;
      G.replay_k = replay_have() ? (int32_t)(replay_the_hit()->k * 100) : 0;
      G.table_t0 = replay_have() ? 1 << 30 : 0;
      SAVE.bouts++;
      int me = focus_car();
      if (me >= 0 && G.S.winner == me) SAVE.wins++;
      if (me >= 0) SAVE.wrecks += (uint32_t)G.S.c[me].wrecks;
      save_write();
      engines(false);
    } else if ((any_pressed(G.a_start) || (mouse_clicked && in_rect(m, pause_rect(cs.x, cs.y)))) && G.S.phase != PH_OVER) {
      G.state = GS_PAUSE;
      G.menu = 0;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_PAUSE: {
    if (cw_quality_menu_is_open() || G.open_gfx) break; /* the graphics page has the presses */
    if (dy) G.menu = (G.menu + dy + PAUSE_N) % PAUSE_N, sfx(SFX_MOVE, 0.5f);
    int hover = -1;
    for (int i = 0; i < PAUSE_N; i++)
      if (in_rect(m, pause_row(i, cs.x, cs.y))) hover = i;
    if (hover >= 0 && mouse_moved) G.menu = hover; /* moving the mouse is using it */
    bool pick = G.state_ticks > 5 && (any_pressed(G.m_ok) || (mouse_clicked && hover >= 0));
    if (pick && mouse_clicked && hover >= 0) G.menu = hover;
    int dx = G.state_ticks > 5 ? (any_pressed(G.m_right) ? 1 : 0) - (any_pressed(G.m_left) ? 1 : 0) : 0;
    if (G.state_ticks > 5 && (any_pressed(G.a_start) || back || (pick && G.menu == 0))) G.state = GS_BOUT;
    else if (pick && G.menu == 1) G.open_gfx = true; /* the graphics page opens in update (gfx_input) */
    else if ((pick || dx) && G.menu == 2) { /* Text size: render-only, kept in the save */
      if (dx < 0) cw_text_scale_cycle_back();
      else cw_text_scale_cycle();
      sfx(SFX_SELECT, 0.6f);
      save_write();
    } else if ((pick || dx) && G.menu == 3) { /* Text background */
      cw_text_backdrop_cycle();
      if (dx < 0) cw_text_backdrop_cycle(), cw_text_backdrop_cycle(); /* three steps: two forward is one back */
      sfx(SFX_SELECT, 0.6f);
      save_write();
    } else if (pick && G.menu == 4) enter_title();
    break;
  }
  case GS_RESULTS:
    if (G.replay_f >= 0 && G.replay_f < replay_frames()) { /* the replay: a press skips to the table */
      if (G.state_ticks > 20 && (confirm || mouse_clicked)) G.replay_f = replay_frames();
    } else if (G.state_ticks - G.table_t0 > 120 && (confirm || mouse_clicked)) enter_title();
    break;
  default: break;
  }
  if (pending_bout > 0) {
    int a = pending_bout;
    pending_bout = -1;
    start_bout(a, true);
  }
  if (G.state != GS_PAUSE) {
    if (G.state == GS_RESULTS || G.state == GS_TITLE) G.S.ev_n = 0;
    if (G.state == GS_RESULTS && G.replay_f >= 0 && G.replay_f < replay_frames()) { /* the replay poses the cars */
      replay_pose(G.replay_f);
      fx_slow(replay_slow(G.replay_f));
      if (G.replay_f == 60) { /* the hit itself: its sparks, chips and dust ring again, and its crunch */
        static sim hit;
        const replay_hit *h = replay_the_hit();
        hit.ev_n = 1;
        hit.ev[0] = (struct sim_event){ EV_CRASH, h->a, h->b, h->at, h->k };
        fx_events(&hit);
        sfx_at(SFX_CRUNCH_BIG, h->at, 1, 0.8f);
        sfx(SFX_CHEER, 0.7f);
      }
      if (++G.replay_f >= replay_frames()) { G.table_t0 = G.state_ticks; fx_slow(false); }
    } else world_tick(&G.S);
    debris_tick(&G.S);
    fx_tick(&G.S);
    scenery_tick(&G.S, G.ticks, G.state == GS_RESULTS && G.state_ticks < 90);
  }
  engines_tick();
  camera_tick();
  {
    t3_vec3 eyes[CW_MAX_VIEWS];
    int n = G.state == GS_TITLE ? 1 : nviews;
    for (int v = 0; v < n; v++) eyes[v] = RIG[v].cam_eye;
    if (n == 1) world_lod(eyes[0], 1, -1); /* splitscreen: per view (view_lod) */
    scenery_beams(eyes, n);
  }
  const sim *s = &G.S;
  world_contact_shadows(s);
  G.phase = s->phase;
  G.clock = s->clock;
  G.alive = s->alive;
  G.winner = s->winner;
  G.hash = s->hash;
  for (int i = 0; i < MAXC; i++) {
    const car *c = &s->c[i];
    G.pos[i * 2] = (int32_t)(c->x * 100), G.pos[i * 2 + 1] = (int32_t)(c->z * 100);
    G.hp[i] = (int32_t)(c->hp * 10);
    G.dents[i] = c->ndent;
    G.wrecked[i] = c->wrecked;
    G.slots[i] = G.slot[i];
    G.points[i] = c->points;
    G.mud[i] = (int32_t)(world_mud(i) * 100);
  }
  {
    const car *c0 = &s->c[0];
    G.wheel_yaw0 = car_wheel_yaw(c0, 0);
    G.yaw_rate0 = c0->w;
    /* the wheels point the way the car turns (both left, or both right) */
    if (fabsf(c0->w) > 0.2f && fabsf(G.wheel_yaw0) > 0.05f) G.steer_agrees = (c0->w > 0) == (G.wheel_yaw0 > 0) ? 1 : 0;
  }
  G.crashes = s->crashes, G.walls = s->wall_hits, G.wrecks = s->wrecks, G.max_hit = (int32_t)(s->max_hit * 100), G.flips = s->flips;
  int32_t ints[] = { G.state, G.phase, G.alive, G.winner, G.clock };
  for (size_t k = 0; k < sizeof ints / sizeof ints[0]; k++) G.state_hash = (G.state_hash ^ (uint32_t)ints[k]) * 16777619u;
  G.state_hash = (G.state_hash ^ G.hash) * 16777619u;
}

static void gfx_input(void);
static void update(void *u, double dt) {
  read_device();
  gfx_input();
  cw_quality_auto_measure(G.state == GS_BOUT); /* the preset steps down when play runs slow */
  (void)u;
  (void)dt;
  if (!A.on) return;
  /* a listener per view (each sound heard from the nearest player's camera) */
  int n = G.state == GS_TITLE ? 1 : nviews;
  for (int v = 0; v < n; v++) {
    cw_node cam = v == 0 ? cw_render3d_camera(cw_scene_app_tree()) : RIG[v].cam;
    if (v == 0 && cw_node_valid(RIG[0].cam)) cam = RIG[0].cam;
    if (!cw_node_valid(cam)) continue;
    const t3_mat4 *mm = cw_node3d_global_transform(cam);
    t3_vec3 p = { mm->e[12], mm->e[13], mm->e[14] }, f = { -mm->e[8], -mm->e[9], -mm->e[10] }, up = { mm->e[4], mm->e[5], mm->e[6] };
    cw_audio_set_listener_at(v, p, (t3_vec3){ 0, 0, 0 }, f, up);
  }
  cw_audio_set_listener_count(n);
  cw_audio_update();
}

/* ── HUD (render2d over the 3D frame; render-only) ─────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

/* a translucent line drawn as 12 overlapping stamps read about twice as dense */
static cw_color stamped(cw_color c) { return rgba(c.r, c.g, c.b, 1 - (1 - c.a) * (1 - c.a)); }
/* text with a round outline (and a drop shadow), one layout, one command */
static void outlined(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_color line, float radius, cw_text_align align,
                     float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap, .outline_width = radius, .outline_color = stamped(line) };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  outlined(font_hud, s, x, y, size, c, rgba(0.03f, 0.025f, 0.02f, 0.85f * c.a), size * 0.07f + 0.6f, align, wrap);
}
static void small(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_font f = CW_HANDLE_IS_NULL(font_small) ? font_hud : font_small;
  outlined(f, s, x, y, size, c, rgba(0.03f, 0.025f, 0.02f, 0.8f * c.a), size * 0.06f + 0.5f, align, wrap);
}
/* the stencilled titles: an outline, and the dark copy under it as a drop shadow */
static void stencil(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap, .outline_width = size * 0.05f,
                       .outline_color = stamped(rgba(0.04f, 0.03f, 0.02f, c.a)), .shadow_color = stamped(rgba(0.02f, 0.015f, 0.01f, 0.55f * c.a)),
                       .shadow_offset = cw_v2(size * 0.04f, size * 0.06f), .shadow_spread = size * 0.06f };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}
/* the title logo: art, drawn as asked (never the player's Text size) */
static void logo(const char *s, float x, float y, float size, cw_color c, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = CW_ALIGN_CENTER, .wrap_width = wrap, .outline_width = size * 0.05f, .fixed_size = true,
                       .outline_color = stamped(rgba(0.04f, 0.03f, 0.02f, c.a)), .shadow_color = stamped(rgba(0.02f, 0.015f, 0.01f, 0.55f * c.a)),
                       .shadow_offset = cw_v2(size * 0.04f, size * 0.06f), .shadow_spread = size * 0.06f };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}

/* measuring (cw_font_measure grows with the player's Text size, as drawing does) */
static cw_font small_font(void) { return CW_HANDLE_IS_NULL(font_small) ? font_hud : font_small; }
static float tw(cw_font f, const char *s, float size) { return cw_font_measure(f, s, &(cw_text_style){ .size = size }).width; }
static float tsz(cw_font f, float size) { return cw_text_drawn_size(f, size); } /* the size `size` draws at now */

/* a hint with button prompts ({m_ok}, {thr_fwd}: cartwheel/prompt.h, for
 * hint_player's device, P1's but in a split view), broken at its triple spaces
 * into lines that fit the canvas (or the view from hint_x, W wide) */
#define HINT_LINES 6
static int hint_player;
static float hint_x;
static int hint_lines(const char *s, float W, float size, char out[HINT_LINES][200], float *wmax, float *hsum) {
  int n = 0;
  *wmax = 0, *hsum = 0;
  char line[200] = "";
  const char *p = s;
  while (*p && n < HINT_LINES) {
    const char *e = strstr(p, "   ");
    size_t len = e ? (size_t)(e - p) : strlen(p);
    char cand[200];
    snprintf(cand, sizeof cand, "%s%s%.*s", line, *line ? "   " : "", (int)len, p);
    if (*line && cw_prompt_text_size(font_hud, cand, hint_player, size).x > W - 4 * size) {
      snprintf(out[n++], 200, "%s", line);
      snprintf(line, sizeof line, "%.*s", (int)len, p);
    } else snprintf(line, sizeof line, "%s", cand);
    p += len;
    while (*p == ' ') p++;
  }
  if (*line && n < HINT_LINES) snprintf(out[n++], 200, "%s", line);
  for (int i = 0; i < n; i++) {
    cw_vec2 m = cw_prompt_text_size(font_hud, out[i], hint_player, size);
    *wmax = fmaxf(*wmax, m.x), *hsum += m.y;
  }
  return n;
}
/* the height prompt_line takes (its band included) */
static float prompt_height(const char *s, float W, float size) {
  char l[HINT_LINES][200];
  float w, h;
  if (!*s || !hint_lines(s, W, size, l, &w, &h)) return 0;
  return h + size * 0.6f;
}
/* a centred hint on a dark band; y is the top of its first line */
static void prompt_line(const char *s, float W, float y, float size) {
  char l[HINT_LINES][200];
  float w, h;
  int n = *s ? hint_lines(s, W, size, l, &w, &h) : 0;
  if (!n) return;
  cw_r2d_rect(cw_rect_make(hint_x + W * 0.5f - w * 0.5f - size, y - size * 0.3f, w + 2 * size, h + size * 0.6f), rgba(0.04f, 0.03f, 0.02f, 0.6f));
  for (int i = 0; i < n; i++) {
    cw_prompt_text(font_hud, l[i], hint_player, cw_v2(hint_x + W * 0.5f, y), size, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER);
    y += cw_prompt_text_size(font_hud, l[i], hint_player, size).y;
  }
}
/* the same, its band's bottom at `bottom` */
static void prompt_bottom(const char *s, float W, float bottom, float size) {
  prompt_line(s, W, bottom - prompt_height(s, W, size) + size * 0.3f, size);
}

/* a plate: a dark, slightly worn panel with a hard edge */
static void plate(cw_rect r, float alpha, cw_color edge) {
  cw_r2d_rect(cw_rect_make(r.x + 3, r.y + 4, r.w, r.h), rgba(0, 0, 0, 0.35f * alpha));
  cw_r2d_rect(cw_rect_make(r.x - 2, r.y - 2, r.w + 4, r.h + 4), rgba(edge.r, edge.g, edge.b, edge.a * alpha));
  cw_r2d_rect(r, rgba(0.08f, 0.07f, 0.06f, 0.82f * alpha));
  /* rivets */
  cw_color rv = rgba(0.55f, 0.52f, 0.48f, 0.8f * alpha);
  float k = fminf(r.h * 0.08f, 4);
  cw_r2d_circle(cw_v2(r.x + 6, r.y + 6), k, 8, rv);
  cw_r2d_circle(cw_v2(r.x + r.w - 6, r.y + 6), k, 8, rv);
  cw_r2d_circle(cw_v2(r.x + 6, r.y + r.h - 6), k, 8, rv);
  cw_r2d_circle(cw_v2(r.x + r.w - 6, r.y + r.h - 6), k, 8, rv);
}
/* a worn steel strap across a rect: dark iron, a rusty edge, bolt heads */
static void hazard(cw_rect r, float alpha) {
  cw_r2d_rect(r, rgba(0.16f, 0.15f, 0.14f, 0.92f * alpha));
  cw_r2d_rect(cw_rect_make(r.x, r.y, r.w, r.h * 0.25f), rgba(0.38f, 0.2f, 0.1f, 0.7f * alpha));
  cw_r2d_rect(cw_rect_make(r.x, r.y + r.h * 0.8f, r.w, r.h * 0.2f), rgba(0.05f, 0.045f, 0.04f, 0.9f * alpha));
  for (float x = r.x + r.h; x < r.x + r.w; x += r.h * 9) cw_r2d_circle(cw_v2(x, r.y + r.h * 0.5f), r.h * 0.28f, 8, rgba(0.5f, 0.47f, 0.42f, 0.9f * alpha));
}

static cw_color paint_col(int i, float a) {
  const float *c = CAR_RGB[G.look[i].colour];
  return rgba(c[0], c[1], c[2], a);
}
static cw_color hp_col(float hp, float a) {
  float k = hp / 100;
  return k > 0.6f ? rgba(0.45f, 0.85f, 0.3f, a) : k > 0.3f ? rgba(0.95f, 0.7f, 0.15f, a) : rgba(0.95f, 0.25f, 0.12f, a);
}

/* a car from above: its body in its paint, each side tinted by its damage */
static void car_diagram(float cx, float cy, float s, int i) {
  const car *c = &G.S.c[i];
  float w = 34 * s, h = 70 * s;
  cw_r2d_rect(cw_rect_make(cx - w * 0.5f - 2, cy - h * 0.5f - 2, w + 4, h + 4), rgba(0.02f, 0.02f, 0.02f, 0.9f));
  cw_r2d_rect(cw_rect_make(cx - w * 0.5f, cy - h * 0.5f, w, h), paint_col(i, 0.9f));
  cw_r2d_rect(cw_rect_make(cx - w * 0.38f, cy - h * 0.2f, w * 0.76f, h * 0.36f), rgba(0.08f, 0.09f, 0.1f, 0.9f));
  /* the zones: front at the top */
  float zf = c->zone[ZONE_FRONT], zr = c->zone[ZONE_REAR], zl = c->zone[ZONE_LEFT], zrt = c->zone[ZONE_RIGHT];
  cw_r2d_rect(cw_rect_make(cx - w * 0.5f, cy - h * 0.5f, w, h * 0.16f), rgba(0.95f, 0.2f, 0.08f, zf * 0.9f));
  cw_r2d_rect(cw_rect_make(cx - w * 0.5f, cy + h * 0.34f, w, h * 0.16f), rgba(0.95f, 0.2f, 0.08f, zr * 0.9f));
  cw_r2d_rect(cw_rect_make(cx - w * 0.5f, cy - h * 0.34f, w * 0.2f, h * 0.68f), rgba(0.95f, 0.2f, 0.08f, zl * 0.9f));
  cw_r2d_rect(cw_rect_make(cx + w * 0.3f, cy - h * 0.34f, w * 0.2f, h * 0.68f), rgba(0.95f, 0.2f, 0.08f, zrt * 0.9f));
  for (int k = 0; k < 4; k++) {
    float wx = cx + (k & 1 ? w * 0.5f : -w * 0.5f - 5 * s), wy = cy + (k < 2 ? -h * 0.32f : h * 0.2f);
    cw_r2d_rect(cw_rect_make(wx, wy, 5 * s, h * 0.14f), rgba(0.05f, 0.05f, 0.05f, 1));
  }
}

static const char *prompt(void) {
  switch (G.state) {
  case GS_TITLE:
    return glyphs() ? "{m_ok} drive   {m_up|m_down} choose   {m_left|m_right} car" : on_touch() ? "tap to drive" : "click to drive";
  case GS_RESULTS: return glyphs() ? "{m_ok} to go again" : on_touch() ? "tap to go again" : "click to go again";
  default: return "";
  }
}

/* the title, laid out from the size its text draws at: the hint at the
 * bottom, the record over it, the two buttons, the join tags, the tag line,
 * and the logo (art, drawn as asked) in what is left at the top */
#define TITLE_ITEM 30.0f
#define TITLE_SUB 14.0f
static const char *const TITLE_ITEMS[2] = { "QUICK MATCH", "POINTS FOR HITS" };
static const char *const TITLE_SUBS[2] = { "you and five bangers: last car running", "three minutes: points for every hit" };
static const char *const TITLE_TAG = "six bangers.  one dirt bowl.  last car running wins.";
typedef struct title_lay {
  cw_rect button[2];
  cw_rect car;          /* your car: its body and class, changed with left and right (or a tap) */
  float record_y, tags_y, tags_h, tag_y, hazard2_y, logo_size;
  bool tags;
} title_lay;
static void title_seats(bool here[4], bool *others) {
  here[0] = true, *others = false;
  for (int p = 1; p < 4; p++) {
    int pad = cw_input_player_pad(p);
    here[p] = (pad >= 0 && cw_input_raw()->pads[pad].connected) || G.joined[p];
    *others |= here[p];
  }
}
/* the car line on the title: "< SEDAN  LIGHT: quick and nimble, but it dents >" */
static const char *const CAR_TITLE[CAR_KINDS] = { "SEDAN", "WAGON", "PICKUP", "VAN" };
static const char *car_line(int k, char *buf, size_t n) {
  if (glyphs()) snprintf(buf, n, "{m_left}  %s  %s: %s  {m_right}", CAR_TITLE[k], CAR_CLASS[k].name, CAR_CLASS[k].blurb);
  else snprintf(buf, n, "%s  %s: %s", CAR_TITLE[k], CAR_CLASS[k].name, CAR_CLASS[k].blurb);
  return buf;
}
static title_lay title_layout(float W, float H) {
  float s = H / 540.0f;
  title_lay L = { 0 };
  bool here[4];
  title_seats(here, &L.tags);
  float y = H - 6 * s - prompt_height(prompt(), W, 15 * s);
  if (SAVE.bouts) y -= tsz(small_font(), 14 * s) + 6 * s, L.record_y = y;
  {
    float ch = tsz(small_font(), 16 * s) + 12 * s, cw = 0;
    char buf[160];
    for (int k = 0; k < CAR_KINDS; k++) cw = fmaxf(cw, cw_prompt_text_size(small_font(), car_line(k, buf, sizeof buf), 0, 16 * s).x);
    cw = fminf(cw + 28 * s, W - 24 * s);
    y -= ch + 8 * s;
    L.car = cw_rect_make(W * 0.5f - cw * 0.5f, y, cw, ch);
  }
  y -= 10 * s;
  float bh = 8 * s + tsz(font_title, TITLE_ITEM * s) + 2 * s + tsz(small_font(), TITLE_SUB * s) + 10 * s, gap = 14 * s, bw = 300 * s;
  for (int i = 0; i < 2; i++) bw = fmaxf(bw, fmaxf(tw(font_title, TITLE_ITEMS[i], TITLE_ITEM * s), tw(small_font(), TITLE_SUBS[i], TITLE_SUB * s)) + 32 * s);
  bw = fminf(bw, W - 24 * s);
  float b0 = fminf(H * 0.6f, y - 2 * bh - gap);
  for (int i = 0; i < 2; i++) L.button[i] = cw_rect_make(W * 0.5f - bw * 0.5f, b0 + i * (bh + gap), bw, bh);
  y = b0 - 10 * s;
  if (L.tags) {
    L.tags_h = cw_font_measure(small_font(), "P1  IN", &(cw_text_style){ .size = 16 * s }).height + 12 * s;
    L.tags_y = fminf(H * 0.505f, y - L.tags_h);
    y = L.tags_y - 10 * s;
  }
  float band = prompt_height(TITLE_TAG, W, 17 * s);
  L.tag_y = fminf(H * 0.45f, y - band + 17 * s * 0.3f);
  L.hazard2_y = fminf(H * 0.37f, L.tag_y - 17 * s * 0.3f - 16 * s);
  /* the logo between the two straps: as large as fits, up to its size */
  float room = L.hazard2_y - H * 0.115f - 6 * s;
  float h112 = cw_font_measure(font_title, GAME_TITLE, &(cw_text_style){ .size = 112 * s, .wrap_width = W, .fixed_size = true }).height;
  L.logo_size = 112 * s * (h112 > room && h112 > 0 ? room / h112 : 1);
  return L;
}
/* the buttons where the title last drew them (the layout measures prompts, which only render may do; the
 * fixed tick hit-tests these) */
static cw_rect title_drawn[2];
static cw_rect title_button(int i, float W, float H) {
  (void)W, (void)H;
  return title_drawn[i & 1];
}

static void draw_title(float W, float H, float s) {
  title_lay L = title_layout(W, H);
  title_drawn[0] = L.button[0], title_drawn[1] = L.button[1], title_car_drawn = L.car;
  {
    char cb[160];
    cw_r2d_rect(L.car, rgba(0.06f, 0.05f, 0.04f, 0.72f));
    float th = tsz(small_font(), 16 * s);
    cw_prompt_text(small_font(), car_line(SAVE.kind % CAR_KINDS, cb, sizeof cb), 0, cw_v2(L.car.x + L.car.w * 0.5f, L.car.y + (L.car.h - th) * 0.5f), 16 * s,
                   rgba(0.95f, 0.88f, 0.74f, 1), CW_ALIGN_CENTER);
  }
  hazard(cw_rect_make(W * 0.2f, H * 0.08f, W * 0.6f, 8 * s), 0.9f);
  logo(GAME_TITLE, 0, H * 0.115f, L.logo_size, rgba(0.88f, 0.85f, 0.78f, 1), W);
  hazard(cw_rect_make(W * 0.2f, L.hazard2_y, W * 0.6f, 8 * s), 0.9f);
  prompt_line(TITLE_TAG, W, L.tag_y, 17 * s);
  float ti = tsz(font_title, TITLE_ITEM * s);
  for (int i = 0; i < 2; i++) {
    cw_rect r = L.button[i];
    bool cur = G.menu == i;
    if (cur) cw_r2d_rect(cw_rect_make(r.x - 6 * s, r.y - 6 * s, r.w + 12 * s, r.h + 12 * s), rgba(0.84f, 0.56f, 0.24f, 0.5f + 0.3f * cw_sin(G.ticks * 0.15f)));
    plate(r, 1, cur ? rgba(0.84f, 0.56f, 0.24f, 1) : rgba(0.4f, 0.37f, 0.33f, 1));
    stencil(TITLE_ITEMS[i], r.x, r.y + 8 * s, TITLE_ITEM * s, cur ? rgba(0.95f, 0.78f, 0.5f, 1) : rgba(0.85f, 0.82f, 0.78f, 1), CW_ALIGN_CENTER, r.w);
    small(TITLE_SUBS[i], r.x, r.y + 10 * s + ti, TITLE_SUB * s, rgba(0.9f, 0.86f, 0.8f, 0.9f), CW_ALIGN_CENTER, r.w);
  }
  prompt_bottom(prompt(), W, H - 6 * s, 15 * s);
  char buf[64];
  /* the players: P1 always; another pad shows a join tag, a joined one splits the screen */
  bool here[4], others;
  title_seats(here, &others);
  if (others) {
    float tws = 120 * s;
    for (int p = 0; p < 4; p++) {
      snprintf(buf, sizeof buf, "P%d  {m_ok} JOIN", p + 1);
      tws = fmaxf(tws, cw_prompt_text_size(small_font(), buf, p, 16 * s).x + 16 * s);
    }
    tws = fminf(tws, (W - 24 * s) / 4 - 10 * s);
    float x0 = W * 0.5f - 2 * tws - 15 * s;
    float tb = cw_font_measure(small_font(), "P1  IN", &(cw_text_style){ .size = 16 * s }).height;
    for (int p = 0; p < 4; p++) {
      if (!here[p]) continue;
      cw_rect r = cw_rect_make(x0 + p * (tws + 10 * s), L.tags_y, tws, L.tags_h);
      bool in = p == 0 || G.joined[p];
      cw_r2d_rect(r, in ? rgba(0.84f, 0.56f, 0.24f, 0.85f) : rgba(0.06f, 0.05f, 0.04f, 0.7f));
      snprintf(buf, sizeof buf, in ? "P%d  IN" : "P%d  {m_ok} JOIN", p + 1);
      if (in) small(buf, r.x, r.y + (r.h - tb) * 0.5f, 16 * s, rgba(0.08f, 0.06f, 0.04f, 1), CW_ALIGN_CENTER, r.w);
      else /* the join button, drawn for that seat's pad */
        cw_prompt_text(small_font(), buf, p, cw_v2(r.x + r.w * 0.5f, r.y + (r.h - tb) * 0.5f), 16 * s, rgba(1, 0.95f, 0.85f, 0.85f), CW_ALIGN_CENTER);
    }
  }
  if (SAVE.bouts) {
    snprintf(buf, sizeof buf, "bouts %u   wins %u   wrecks %u", SAVE.bouts, SAVE.wins, SAVE.wrecks);
    small(buf, 0, L.record_y, 14 * s, rgba(0.9f, 0.85f, 0.75f, 0.8f), CW_ALIGN_CENTER, W);
  }
}

/* the on-screen controls, while touch is the device in use */
static cw_vec2 touch_canvas(float x, float y) {
  int ww, wh;
  cw_window_size(&ww, &wh);
  cw_vec2 cs = cw_r2d_canvas_size();
  return cw_v2(x * cs.x / (float)ww, y * cs.y / (float)wh);
}
static void touch_button(int id, float x, float y, float w, float h, const char *label, cw_color c) {
  if (id < 0) return;
  bool on = cw_touch_control_active(id);
  cw_vec2 a = touch_canvas(x, y), b = touch_canvas(x + w, y + h);
  cw_rect r = cw_rect_make(a.x, a.y, b.x - a.x, b.y - a.y);
  cw_r2d_rect(cw_rect_make(r.x - 3, r.y - 3, r.w + 6, r.h + 6), rgba(0.02f, 0.02f, 0.02f, on ? 0.75f : 0.45f));
  cw_r2d_rect(r, rgba(c.r, c.g, c.b, on ? 0.85f : 0.38f));
  stencil(label, r.x, r.y + r.h * 0.5f - r.h * 0.14f, fmaxf(15 * cw_r2d_canvas_size().y / 540.0f, r.h * 0.2f), rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER, r.w);
}
static void draw_touch(float s) {
  if (TOUCH_STICK < 0) return;
  int ww, wh;
  cw_window_size(&ww, &wh);
  float W = (float)ww, H = (float)wh, k = H / 540.0f;
  bool on = cw_touch_control_active(TOUCH_STICK);
  cw_vec2 c = cw_touch_stick_center(TOUCH_STICK);
  if (!on) c = cw_v2(150 * k, H - 150 * k);
  cw_vec2 cc = touch_canvas(c.x, c.y);
  cw_vec2 rr = touch_canvas(64 * k, 0);
  float r = rr.x;
  cw_r2d_circle(cc, r + 4, 40, rgba(0.02f, 0.02f, 0.02f, 0.35f));
  cw_r2d_circle_lines(cc, r, 3 * s, 40, rgba(1, 1, 1, on ? 0.8f : 0.45f));
  cw_vec2 v = on ? cw_touch_stick_value(TOUCH_STICK) : cw_v2(0, 0);
  cw_r2d_circle(cw_v2(cc.x + v.x * r, cc.y), r * 0.42f, 28, rgba(0.84f, 0.56f, 0.24f, on ? 0.9f : 0.5f));
  if (!on) small("steer", cc.x - r, cc.y + r + 6 * s, 14 * s, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r * 2);
  touch_button(TOUCH_GAS, W - 170 * k, H - 230 * k, 150 * k, 200 * k, "GAS", rgba(0.3f, 0.65f, 0.25f, 1));
  touch_button(TOUCH_BRAKE, W - 330 * k, H - 150 * k, 140 * k, 120 * k, "BRAKE", rgba(0.75f, 0.2f, 0.12f, 1));
  touch_button(TOUCH_HB, W - 330 * k, H - 290 * k, 140 * k, 110 * k, "SLIDE", rgba(0.85f, 0.6f, 0.12f, 1));
}

/* the field: everyone's number and engine, wrecks struck through; the
 * humans tagged P1-P4 (left of x). Laid out from the size its text draws at;
 * returns its height */
#define FIELD_TEXT 15.0f
#define TAG_TEXT 14.0f
static float field_row_h(float s) { return tsz(font_hud, FIELD_TEXT * s) + 10 * s; }
static float field_height(float s) {
  int k = 0;
  for (int i = 0; i < MAXC; i++) k += G.S.c[i].on;
  return k * (field_row_h(s) + 4 * s);
}
static float field_bar = 92; /* the engine bars' length (canvas units at 540 lines; shorter where the room is tight) */
static float field_tag_w(float s) { return tw(small_font(), "P4", TAG_TEXT * s) + 8 * s; }
static float field_width(float s) {
  bool pts = G.S.rule == RULE_POINTS;
  return 10 * s + tw(font_hud, "#88", FIELD_TEXT * s) + 8 * s + (pts ? 50 * s : field_bar * s) + 8 * s + (pts ? tw(font_hud, "888", FIELD_TEXT * s) + 8 * s : 0);
}
static float field_list(float lx, float ly, float s) {
  const sim *S = &G.S;
  char buf[96];
  float rh = field_row_h(s), fh = tsz(font_hud, FIELD_TEXT * s), th = tsz(small_font(), TAG_TEXT * s), w = field_width(s);
  float bx = 10 * s + tw(font_hud, "#88", FIELD_TEXT * s) + 8 * s;
  int k = 0;
  for (int i = 0; i < MAXC; i++) {
    const car *c = &S->c[i];
    if (!c->on) continue;
    float y = ly + k * (rh + 4 * s);
    cw_rect r = cw_rect_make(lx, y, w, rh);
    cw_r2d_rect(r, rgba(0.06f, 0.05f, 0.04f, 0.72f));
    cw_r2d_rect(cw_rect_make(r.x, r.y, 6 * s, r.h), paint_col(i, 1));
    snprintf(buf, sizeof buf, "#%d", G.look[i].number);
    text(buf, r.x + 10 * s, r.y + (rh - fh) * 0.5f, FIELD_TEXT * s, rgba(1, 0.95f, 0.85f, c->wrecked ? 0.45f : 1), CW_ALIGN_LEFT, 0);
    if (S->rule == RULE_POINTS) {
      snprintf(buf, sizeof buf, "%d", c->points);
      text(buf, r.x, r.y + (rh - fh) * 0.5f, FIELD_TEXT * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_RIGHT, r.w - 8 * s);
    }
    float bw = S->rule == RULE_POINTS ? 50 * s : field_bar * s;
    cw_rect bar = cw_rect_make(r.x + bx, r.y + (rh - 9 * s) * 0.5f, bw, 9 * s);
    cw_r2d_rect(bar, rgba(0, 0, 0, 0.8f));
    cw_r2d_rect(cw_rect_make(bar.x + 1, bar.y + 1, (bar.w - 2) * cw_clampf(c->hp / 100, 0, 1), bar.h - 2), hp_col(c->hp, 1));
    if (c->wrecked) cw_r2d_line(cw_v2(r.x + 4, r.y + r.h * 0.5f), cw_v2(r.x + r.w - 4, r.y + r.h * 0.5f), 2.5f * s, rgba(0.95f, 0.25f, 0.12f, 0.9f));
    if (G.human_of[i] >= 0) {
      snprintf(buf, sizeof buf, "P%d", G.human_of[i] + 1);
      float tgw = field_tag_w(s);
      small(buf, r.x - 4 * s - tgw, r.y + (rh - th) * 0.5f, TAG_TEXT * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_RIGHT, tgw);
    }
    k++;
  }
  return k * (rh + 4 * s);
}

/* the arena from above in r (z up the screen): the wall, the cars as
 * little boxes along their headings; each player's car in its paint with
 * its tag, the CPUs plain, the wrecks greyed */
static void mini_map(cw_rect r, float s) {
  const sim *S = &G.S;
  static arena_pt line[160];
  static int nline;
  if (!nline) nline = arena_line(line, 160, 1.5f, 0);
  float k = fminf(r.w / (2 * AW + 4), r.h / (2 * AL + 4)), cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
  cw_vec2 pts[160];
  for (int i = 0; i < nline; i++) pts[i] = cw_v2(cx + line[i].x * k, cy - line[i].z * k);
  cw_r2d_polygon(pts, nline, rgba(0.2f, 0.16f, 0.12f, 0.92f));
  cw_r2d_polygon_lines(pts, nline, 2.5f * s, rgba(0.75f, 0.72f, 0.66f, 1));
  char buf[16];
  for (int pass = 0; pass < 2; pass++) /* the players on top */
    for (int i = 0; i < MAXC; i++) {
      const car *c = &S->c[i];
      bool human = G.human_of[i] >= 0;
      if (!c->on || human != (pass == 1)) continue;
      const car_spec *ks = &CAR_SPEC[c->kind];
      float sh, ch;
      cw_sincos(c->h, &sh, &ch);
      /* forward (sin h, cos h) and side (cos h, -sin h) in the world; screen y is -z */
      float fl = ks->hl * k * 1.25f, sw = ks->hw * k * 1.25f;
      cw_vec2 f = cw_v2(sh * fl, -ch * fl), d = cw_v2(ch * sw, sh * sw), o = cw_v2(cx + c->x * k, cy - c->z * k);
      cw_vec2 q[4] = { cw_v2(o.x + f.x + d.x, o.y + f.y + d.y), cw_v2(o.x + f.x - d.x, o.y + f.y - d.y),
                       cw_v2(o.x - f.x - d.x, o.y - f.y - d.y), cw_v2(o.x - f.x + d.x, o.y - f.y + d.y) };
      cw_color col = c->wrecked ? rgba(0.32f, 0.31f, 0.3f, 0.9f) : human ? paint_col(i, 1) : rgba(0.86f, 0.84f, 0.8f, 1);
      cw_r2d_polygon(q, 4, col);
      if (human) {
        cw_r2d_polygon_lines(q, 4, 2 * s, c->wrecked ? rgba(0.5f, 0.48f, 0.45f, 1) : rgba(1, 0.95f, 0.85f, 1));
        snprintf(buf, sizeof buf, "P%d", G.human_of[i] + 1);
        small(buf, o.x - 30 * s, o.y - tsz(small_font(), 14 * s) - 10 * s, 14 * s, c->wrecked ? rgba(0.6f, 0.58f, 0.55f, 1) : rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER, 60 * s);
      } else
        cw_r2d_line(o, cw_v2(o.x + f.x, o.y + f.y), 1.5f * s, rgba(0.1f, 0.1f, 0.1f, 0.8f)); /* which way it faces */
    }
}


/* the clock and the running count (or the mode, on points) in a plate, its
 * size from its text */
#define CLOCK_TEXT 24.0f
#define CLOCK_LABEL 17.0f
static void clock_strings(char *clock, char *label) {
  const sim *S = &G.S;
  int secs = (S->clock + 59) / 60;
  sprintf(clock, "%d:%02d", secs / 60, secs % 60);
  sprintf(label, "%d LEFT", S->alive);
}
static cw_vec2 clock_plate_size(float s) {
  char a[16], b[16];
  clock_strings(a, b);
  return cw_v2(fmaxf(160 * s, 10 * s + tw(font_hud, "0:00", CLOCK_TEXT * s) + 18 * s + tw(font_title, b, CLOCK_LABEL * s) + 10 * s),
               fmaxf(36 * s, fmaxf(tsz(font_hud, CLOCK_TEXT * s), tsz(font_title, CLOCK_LABEL * s)) + 12 * s));
}
static void clock_plate(cw_rect top, float s) {
  const sim *S = &G.S;
  char buf[16], label[16];
  clock_strings(buf, label);
  plate(top, 1, rgba(0.4f, 0.37f, 0.33f, 1));
  int secs = (S->clock + 59) / 60;
  text(buf, top.x + 10 * s, top.y + (top.h - tsz(font_hud, CLOCK_TEXT * s)) * 0.5f, CLOCK_TEXT * s,
       secs <= 20 && S->phase == PH_GO && (G.ticks / 20) % 2 ? rgba(1, 0.4f, 0.2f, 1) : rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_LEFT, 0);
  stencil(label, top.x, top.y + (top.h - tsz(font_title, CLOCK_LABEL * s)) * 0.5f, CLOCK_LABEL * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_RIGHT,
          top.w - 10 * s);
}

/* the splitscreen HUD. Each view: its player's plate (tag, number, speed,
 * the car's damage from above, the engine bar; the points on POINTS FOR
 * HITS) and WRECKED when that car is out. Shared: two players, the clock
 * where the views meet; three, the spare top right quadrant holds the clock,
 * the mode, the cars running, the standings and the map; four, a small clock
 * where the four views meet. The plates keep the body text size in every
 * split (their size comes from their text, not from the view). */
#define SPLIT_TAG 20.0f
#define SPLIT_NUM 18.0f
#define SPLIT_MPH 15.0f
#define SPLIT_LABEL 14.0f
static void draw_split_hud(float W, float H, float s) {
  const sim *S = &G.S;
  const cw_view *vs = NULL;
  int n = cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &vs);
  char buf[64];
  float row1 = fmaxf(tsz(font_title, SPLIT_TAG * s), fmaxf(tsz(font_hud, SPLIT_NUM * s), tsz(small_font(), SPLIT_MPH * s)));
  float lab = tsz(small_font(), SPLIT_LABEL * s), barh = 12 * s;
  float ph = 8 * s + row1 + 4 * s + lab + 4 * s + barh + 8 * s;
  float wc = fmaxf(150 * s, tw(font_title, "P4", SPLIT_TAG * s) + 8 * s + tw(font_hud, "#88", SPLIT_NUM * s) + 12 * s +
                                tw(small_font(), "188 MPH", SPLIT_MPH * s));
  wc = fmaxf(wc, tw(small_font(), "WRECKED", SPLIT_LABEL * s));
  for (int v = 0; v < n; v++) {
    cw_rect R = cw_rect_make(vs[v].x * W, vs[v].y * H, vs[v].w * W, vs[v].h * H);
    int me = car_of_view(v);
    if (me < 0) continue;
    const car *c = &S->c[me];
    /* bottom left, or bottom right in a right-hand view: clear of the clock where the views meet */
    bool right = R.x > 1 && R.w < W * 0.75f;
    float pw = fminf(44 * s + wc + 10 * s, R.w - 20 * s);
    cw_rect pr = cw_rect_make(right ? R.x + R.w - pw - 10 * s : R.x + 10 * s, R.y + R.h - ph - 10 * s, pw, ph);
    plate(pr, 1, paint_col(me, 1));
    car_diagram(pr.x + 22 * s, pr.y + pr.h * 0.5f, fminf(0.61f * s, (ph - 12 * s) / 70), me);
    float tx = pr.x + 44 * s, tw_ = pr.w - 54 * s, y1 = pr.y + 8 * s;
    snprintf(buf, sizeof buf, "P%d", v + 1);
    stencil(buf, tx, y1 + row1 - tsz(font_title, SPLIT_TAG * s), SPLIT_TAG * s, rgba(0.95f, 0.78f, 0.5f, 1), CW_ALIGN_LEFT, 0);
    float nx = tx + tw(font_title, buf, SPLIT_TAG * s) + 8 * s;
    snprintf(buf, sizeof buf, "#%d", G.look[me].number);
    text(buf, nx, y1 + row1 - tsz(font_hud, SPLIT_NUM * s), SPLIT_NUM * s, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_LEFT, 0);
    float mph = fabsf(car_speed(c)) * 2.237f;
    snprintf(buf, sizeof buf, "%d MPH", (int)(mph + 0.5f));
    small(buf, tx, y1 + row1 - tsz(small_font(), SPLIT_MPH * s), SPLIT_MPH * s, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_RIGHT, tw_);
    float y2 = y1 + row1 + 4 * s;
    small(c->wrecked ? "WRECKED" : "ENGINE", tx, y2, SPLIT_LABEL * s, rgba(0.9f, 0.85f, 0.75f, 1), CW_ALIGN_LEFT, 0);
    cw_rect bar = cw_rect_make(tx, y2 + lab + 4 * s, tw_, barh);
    cw_r2d_rect(bar, rgba(0.02f, 0.02f, 0.02f, 0.9f));
    cw_r2d_rect(cw_rect_make(bar.x + 2, bar.y + 2, (bar.w - 4) * cw_clampf(c->hp / 100, 0, 1), bar.h - 4), hp_col(c->hp, 1));
    if (S->rule == RULE_POINTS) {
      snprintf(buf, sizeof buf, "%d PTS", c->points);
      text(buf, pr.x, pr.y - tsz(font_hud, 16 * s) - 6 * s, 16 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_LEFT, 0);
    }
    /* the controls on this player's device, over their plate, from the countdown until they give it gas */
    int pl = G.pl[v];
    if (!sw_hint_countdown && show_controls(pl)) {
      const char *how = "{thr_fwd} gas   {thr_back} brake / reverse   {steer_l|steer_r} steer   {handbrake} handbrake   {look} look back";
      float hs = fmaxf(14 * s, SPLIT_LABEL * s);
      hint_player = pl, hint_x = R.x;
      prompt_line(how, R.w, pr.y - 10 * s - prompt_height(how, R.w, hs) + hs * 0.3f, hs);
      hint_player = 0, hint_x = 0;
      if (pl >= 0 && pl < 4) G.hint_frames[pl]++;
    }
    if (c->wrecked && S->phase == PH_GO) {
      float a = cw_clampf(c->wreck_ticks / 20.0f, 0, 1);
      stencil("WRECKED", R.x, R.y + R.h * 0.3f, 36 * s, rgba(0.95f, 0.3f, 0.15f, a), CW_ALIGN_CENTER, R.w);
    }
  }
  cw_vec2 cp = clock_plate_size(s);
  if (n == 3) {
    /* the top right quadrant: the bout at a glance. The clock and the map on
     * the right, the standings on the left (the mode and the count over them
     * when there is room) */
    cw_rect Q = cw_rect_make(W * 0.5f, 0, W * 0.5f, H * 0.5f);
    cw_r2d_rect(Q, rgba(0.07f, 0.06f, 0.05f, 1));
    float pad = 12 * s, col = Q.w * 0.5f;
    cw_rect top = cw_rect_make(Q.x + Q.w - pad - cp.x, Q.y + pad, cp.x, cp.y);
    clock_plate(top, s);
    float y = Q.y + pad, lx = Q.x + pad + field_tag_w(s);
    float mode = tsz(font_title, 17 * s), count = tsz(small_font(), 14 * s);
    if (y + mode + 6 * s + count + 10 * s + field_height(s) <= Q.y + Q.h - pad) {
      stencil(S->rule == RULE_POINTS ? "POINTS FOR HITS" : "QUICK MATCH", Q.x + pad, y, 17 * s, rgba(0.88f, 0.85f, 0.78f, 0.9f), CW_ALIGN_LEFT, 0);
      snprintf(buf, sizeof buf, "%d OF %d CARS RUNNING", S->alive, S->ncars);
      small(buf, Q.x + pad, y + mode + 6 * s, 14 * s, rgba(0.9f, 0.86f, 0.8f, 0.9f), CW_ALIGN_LEFT, 0);
      y += mode + 6 * s + count + 10 * s;
    }
    field_bar = 92;
    float over = lx + field_width(s) + pad - top.x; /* the list stops short of the clock: shorter bars */
    if (over > 0) field_bar = fmaxf(36, 92 - over / s);
    field_list(lx, y, s);
    float mx = fmaxf(Q.x + col, lx + field_width(s) + pad);
    field_bar = 92;
    mini_map(cw_rect_make(mx, top.y + top.h + 8 * s, Q.x + Q.w - pad - mx, Q.y + Q.h - pad - (top.y + top.h + 8 * s)), s);
  } else if (n == 4) {
    clock_plate(cw_rect_make(W * 0.5f - cp.x * 0.5f, H * 0.5f - cp.y * 0.5f, cp.x, cp.y), s);
  } else {
    bool stacked = vs[1].y > 0;
    clock_plate(cw_rect_make(W * 0.5f - cp.x * 0.5f, stacked ? H * 0.5f - cp.y * 0.5f : 6 * s, cp.x, cp.y), s);
  }
}

static void draw_hud(float W, float H, float s) {
  const sim *S = &G.S;
  char buf[96];
  int me = focus_car();
  bool touch = touch_controls && on_touch();
  bool split = nviews > 1;
  if (split) draw_split_hud(W, H, s), me = -1; /* the per-view plates stand in for P1's */
  float plate_top = H; /* where the player's plate starts (the countdown's hint stays above it) */
  /* top centre: running cars and the clock, the plate as wide as its text */
  if (!split) {
    int secs = (S->clock + 59) / 60;
    char lab[32];
    if (S->rule == RULE_LAST_RUNNING) snprintf(lab, sizeof lab, "%d RUNNING", S->alive);
    else snprintf(lab, sizeof lab, "POINTS");
    float ch = tsz(font_hud, 30 * s), lh = tsz(font_title, 22 * s);
    float w = fmaxf(260 * s, 12 * s + tw(font_hud, "0:00", 30 * s) + 20 * s + tw(font_title, lab, 22 * s) + 12 * s), h = fmaxf(52 * s, fmaxf(ch, lh) + 18 * s);
    cw_rect top = cw_rect_make(W * 0.5f - w * 0.5f, 8 * s, w, h);
    plate(top, 1, rgba(0.4f, 0.37f, 0.33f, 1));
    snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
    text(buf, top.x + 12 * s, top.y + (h - ch) * 0.5f, 30 * s,
         secs <= 20 && S->phase == PH_GO && (G.ticks / 20) % 2 ? rgba(1, 0.4f, 0.2f, 1) : rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_LEFT, 0);
    stencil(lab, top.x, top.y + (h - lh) * 0.5f, 22 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_RIGHT, top.w - 12 * s);
  }
  /* the player's plate: number, speed, engine bar, the car from above; its
   * size from its text */
  if (me >= 0) {
    const car *c = &S->c[me];
    float row1 = fmaxf(tsz(font_title, 30 * s), tsz(font_hud, 30 * s)), lab = tsz(small_font(), 14 * s), barh = 18 * s;
    float ph = fmaxf(100 * s, 8 * s + row1 + 4 * s + lab + 4 * s + barh + 10 * s);
    float mw = tw(small_font(), "MPH", 14 * s);
    float wc = fmaxf(170 * s, tw(font_title, "#88", 30 * s) + 16 * s + tw(font_hud, "188", 30 * s) + 6 * s + mw);
    wc = fmaxf(wc, tw(small_font(), "WRECKED", 14 * s));
    float px = touch ? 84 * s : 12 * s, py = touch ? 10 * s : H - ph - 12 * s;
    cw_rect pr = cw_rect_make(px, py, 64 * s + wc + 16 * s, ph);
    if (!touch) plate_top = py;
    plate(pr, 1, paint_col(me, 1));
    car_diagram(pr.x + 32 * s, pr.y + ph * 0.5f, s, me);
    float x0 = pr.x + 64 * s, y1 = pr.y + 8 * s;
    snprintf(buf, sizeof buf, "#%d", G.look[me].number);
    stencil(buf, x0, y1, 30 * s, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_LEFT, 0);
    float mph = fabsf(car_speed(c)) * 2.237f;
    snprintf(buf, sizeof buf, "%d", (int)(mph + 0.5f));
    text(buf, x0, y1 + row1 - tsz(font_hud, 30 * s), 30 * s, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_RIGHT, wc - mw - 6 * s);
    small("MPH", x0, y1 + row1 - lab - 2 * s, 14 * s, rgba(0.9f, 0.85f, 0.75f, 0.9f), CW_ALIGN_RIGHT, wc);
    float y2 = y1 + row1 + 4 * s;
    small(c->wrecked ? "WRECKED" : "ENGINE", x0, y2, 14 * s, rgba(0.9f, 0.85f, 0.75f, 1), CW_ALIGN_LEFT, 0);
    cw_rect bar = cw_rect_make(x0, y2 + lab + 4 * s, wc, barh);
    cw_r2d_rect(bar, rgba(0.02f, 0.02f, 0.02f, 0.9f));
    float k = cw_clampf(c->hp / 100, 0, 1);
    cw_r2d_rect(cw_rect_make(bar.x + 2, bar.y + 2, (bar.w - 4) * k, bar.h - 4), hp_col(c->hp, 1));
    for (int t = 1; t < 10; t++) cw_r2d_rect(cw_rect_make(bar.x + bar.w * t / 10, bar.y, 1.5f, bar.h), rgba(0, 0, 0, 0.6f));
    if (S->rule == RULE_POINTS) {
      snprintf(buf, sizeof buf, "%d PTS", c->points);
      float py2 = touch ? pr.y + ph + 6 * s : pr.y - tsz(font_hud, 20 * s) - 8 * s;
      text(buf, pr.x, py2, 20 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_LEFT, 0);
    }
  }
  /* the field: everyone's number and engine, wrecks struck through */
  float lx = W - field_width(s) - 12 * s, ly = 70 * s;
  if (touch) ly = 8 * s;
  if (!split && (!touch || G.state == GS_PAUSE)) field_list(lx, ly, s);
  /* the countdown */
  if (S->phase == PH_COUNTDOWN) {
    int n = 3 - S->phase_ticks / 60;
    float f = (S->phase_ticks % 60) / 60.0f, pop = 1 + 0.4f * (1 - f) * (1 - f);
    snprintf(buf, sizeof buf, "%d", n < 1 ? 1 : n);
    stencil(buf, 0, H * 0.2f, 110 * s * pop, rgba(0.84f, 0.56f, 0.24f, 1 - f * 0.3f), CW_ALIGN_CENTER, W);
  } else if (S->phase == PH_GO && S->phase_ticks < 50) {
    float f = S->phase_ticks / 50.0f;
    stencil("WRECK 'EM!", 0, H * 0.2f, 84 * s * (1 + 0.3f * (1 - f)), rgba(0.84f, 0.56f, 0.24f, 1 - f), CW_ALIGN_CENTER, W);
  }
  /* the controls, from the countdown until the player gives it gas (the split views draw their own) */
  if (!split && show_controls(0)) {
    const char *how = touch ? "left thumb: steer   right thumb: gas, brake, slide"
                    : "{thr_fwd} gas   {thr_back} brake / reverse   {steer_l|steer_r} steer   {handbrake} handbrake   {horn} horn   {look} look back";
    prompt_line(how, W, fminf(H * 0.62f, plate_top - 10 * s - prompt_height(how, W, 16 * s) + 16 * s * 0.3f), 16 * s);
    G.hint_frames[0]++;
  }
  if (me >= 0 && S->c[me].wrecked && S->phase == PH_GO) {
    float a = cw_clampf(S->c[me].wreck_ticks / 20.0f, 0, 1);
    hazard(cw_rect_make(0, H * 0.3f, W, 8 * s), a * 0.9f);
    stencil("WRECKED", 0, H * 0.31f + 10 * s, 70 * s, rgba(0.95f, 0.3f, 0.15f, a), CW_ALIGN_CENTER, W);
    hazard(cw_rect_make(0, H * 0.31f + 92 * s, W, 8 * s), a * 0.9f);
    if (S->c[me].wreck_ticks > 150) prompt_line("watching the rest of the bout", W, H * 0.31f + 112 * s, 14 * s);
  }
  if (S->phase == PH_OVER) {
    float a = cw_clampf(S->phase_ticks / 15.0f, 0, 1);
    bool human_won = S->winner >= 0 && (split ? G.human_of[S->winner] >= 0 : S->winner == me && me >= 0);
    const char *head = human_won && !split ? "LAST CAR RUNNING!" : S->rule == RULE_POINTS ? "TIME!" : "BOUT OVER";
    if (human_won && split) {
      snprintf(buf, sizeof buf, "P%d WINS!", G.human_of[S->winner] + 1);
      head = buf;
    }
    stencil(head, 0, H * 0.24f, 66 * s, rgba(0.84f, 0.56f, 0.24f, a), CW_ALIGN_CENTER, W);
    if (S->winner >= 0) {
      snprintf(buf, sizeof buf, "#%d %s takes it", G.look[S->winner].number, driver(S->winner));
      prompt_line(buf, W, H * 0.24f + tsz(font_title, 66 * s) + 14 * s, 18 * s);
    }
  }
  if (G.feed_ticks > 0) {
    float a = cw_clampf(G.feed_ticks / 20.0f, 0, 1);
    /* clear of the standings on the right (and so of the left side too: it is centred) */
    float fw = split || touch ? W - 60 * s : W - 2 * (field_width(s) + field_tag_w(s) + 24 * s);
    cw_text_style st = { .size = 18 * s, .wrap_width = fw };
    cw_text_metrics mm = cw_font_measure(font_hud, G.feed, &st);
    float fy = fmaxf(H * 0.14f, split ? 0 : 8 * s + fmaxf(52 * s, fmaxf(tsz(font_hud, 30 * s), tsz(font_title, 22 * s)) + 18 * s) + 10 * s);
    cw_r2d_rect(cw_rect_make(W * 0.5f - mm.width * 0.5f - 14 * s, fy - 4 * s, mm.width + 28 * s, mm.height + 8 * s), rgba(0.55f, 0.12f, 0.05f, 0.75f * a));
    text(G.feed, (W - fw) * 0.5f, fy, 18 * s, rgba(1, 0.92f, 0.75f, a), CW_ALIGN_CENTER, fw);
  }
  if (touch && G.state == GS_BOUT) draw_touch(s);
  /* a pause button for the mouse and touch (pads and keys have Start / Esc) */
  if (G.state == GS_BOUT && !glyphs()) {
    cw_rect pr = pause_rect(W, H);
    plate(pr, 0.9f, rgba(0.4f, 0.37f, 0.33f, 1));
    for (int k = 0; k < 2; k++) cw_r2d_rect(cw_rect_make(pr.x + pr.w * (0.36f + 0.18f * k), pr.y + pr.h * 0.25f, pr.w * 0.1f, pr.h * 0.5f), rgba(1, 0.95f, 0.85f, 1));
  }
}

static void draw_results(float W, float H, float s) {
  const sim *S = &G.S;
  char buf[96];
  if (G.replay_f >= 0 && G.replay_f < replay_frames()) { /* the replay's caption: whose hit, and how big */
    const replay_hit *h = replay_the_hit();
    float a = cw_clampf(G.state_ticks / 20.0f, 0, 1);
    hazard(cw_rect_make(0, H * 0.04f, W, 9 * s), a);
    stencil("BIGGEST HIT", 0, H * 0.07f, 48 * s, rgba(0.84f, 0.56f, 0.24f, a), CW_ALIGN_CENTER, W);
    snprintf(buf, sizeof buf, "#%d %s INTO #%d %s", G.look[h->a].number, driver(h->a), G.look[h->b].number, driver(h->b));
    text(buf, 0, H * 0.07f + tsz(font_title, 48 * s) + 8 * s, 20 * s, rgba(1, 0.95f, 0.85f, a), CW_ALIGN_CENTER, W);
    small("REPLAY", 0, H - 40 * s, 16 * s, rgba(1, 0.95f, 0.85f, 0.6f + 0.4f * a), CW_ALIGN_RIGHT, W - 20 * s);
    if (G.state_ticks > 20) prompt_bottom(glyphs() ? "{m_ok} skip" : on_touch() ? "tap to skip" : "click to skip", W, H - 8 * s, 16 * s);
    return;
  }
  float in = cw_clampf((G.state_ticks - G.table_t0) / 20.0f, 0, 1);
  int me = focus_car();
  bool won = me >= 0 && S->winner == me;
  const char *head = won ? "YOU WON THE BOUT!" : "RESULTS";
  if (nviews > 1 && S->winner >= 0 && G.human_of[S->winner] >= 0) {
    snprintf(buf, sizeof buf, "%s WON THE BOUT!", driver(S->winner));
    head = buf;
  }
  hazard(cw_rect_make(0, H * 0.04f, W, 9 * s), in);
  stencil(head, 0, H * 0.07f, 56 * s, rgba(0.84f, 0.56f, 0.24f, in), CW_ALIGN_CENTER, W);
  /* the table: order by finish (the winner, then most damage dealt) */
  int order[MAXC], m = 0;
  for (int i = 0; i < MAXC; i++)
    if (S->c[i].on) order[m++] = i;
  for (int a = 0; a < m; a++)
    for (int b = a + 1; b < m; b++) {
      const car *x = &S->c[order[a]], *y = &S->c[order[b]];
      float kx = (order[a] == S->winner ? 1e6f : 0) + (S->rule == RULE_POINTS ? (float)x->points : x->dealt + (x->wrecked ? 0 : 500));
      float ky = (order[b] == S->winner ? 1e6f : 0) + (S->rule == RULE_POINTS ? (float)y->points : y->dealt + (y->wrecked ? 0 : 500));
      if (ky > kx) {
        int t = order[a];
        order[a] = order[b];
        order[b] = t;
      }
    }
  /* the columns from their measured widths */
  static const char *const HEAD[4] = { "DEALT", "TAKEN", "WRECKS", "POINTS" };
  float rt = tsz(font_hud, 18 * s), ht = tsz(small_font(), 14 * s), rh = rt + 16 * s;
  float rankw = tw(font_hud, "8.", 18 * s) + 12 * s, namew = 0, colw = 78 * s;
  for (int r = 0; r < m; r++) {
    snprintf(buf, sizeof buf, "#%d %s", G.look[order[r]].number, driver(order[r]));
    namew = fmaxf(namew, tw(font_hud, buf, 18 * s) + 20 * s);
  }
  for (int c = 0; c < 4; c++) colw = fmaxf(colw, fmaxf(tw(small_font(), HEAD[c], 14 * s), tw(font_hud, "8888", 18 * s)) + 14 * s);
  float tw_ = rankw + 16 * s + namew + 4 * colw;
  if (tw_ < 600 * s) namew += 600 * s - tw_, tw_ = 600 * s;
  float x = (W - tw_) * 0.5f, cx = x + rankw + 16 * s + namew;
  float hy = fmaxf(H * 0.26f - 30 * s, H * 0.07f + tsz(font_title, 56 * s) + 16 * s), y = hy + fmaxf(30 * s, ht + 12 * s);
  plate(cw_rect_make(x - 12 * s, hy - 6 * s, tw_ + 24 * s, (y - hy) + rh * m + 18 * s), in, rgba(0.4f, 0.37f, 0.33f, 1));
  for (int c = 0; c < 4; c++) small(HEAD[c], cx + c * colw, hy, 14 * s, rgba(0.84f, 0.56f, 0.24f, in), CW_ALIGN_CENTER, colw);
  for (int r = 0; r < m; r++) {
    int i = order[r];
    const car *c = &S->c[i];
    float yy = y + r * rh, ty = yy + (rh - rt) * 0.5f;
    snprintf(buf, sizeof buf, "%d.", r + 1);
    text(buf, x, ty, 18 * s, rgba(1, 0.95f, 0.85f, in), CW_ALIGN_LEFT, 0);
    cw_r2d_rect(cw_rect_make(x + rankw, yy + (rh - 20 * s) * 0.5f, 8 * s, 20 * s), paint_col(i, in));
    snprintf(buf, sizeof buf, "#%d %s", G.look[i].number, driver(i));
    text(buf, x + rankw + 16 * s, ty, 18 * s, G.human_of[i] >= 0 ? rgba(0.84f, 0.56f, 0.24f, in) : rgba(1, 0.95f, 0.85f, in), CW_ALIGN_LEFT, 0);
    int vals[4] = { (int)(c->dealt + 0.5f), (int)(c->taken + 0.5f), c->wrecks, c->points };
    for (int k = 0; k < 4; k++) {
      snprintf(buf, sizeof buf, "%d", vals[k]);
      text(buf, cx + k * colw, ty, 18 * s, rgba(1, 0.95f, 0.85f, in), CW_ALIGN_CENTER, colw);
    }
  }
  if (G.state_ticks - G.table_t0 > 120) prompt_bottom(prompt(), W, H - 8 * s, 16 * s);
}

/* the pause screen: RESUME, GRAPHICS (the graphics page, cartwheel/quality_menu.h;
 * the settings action opens it too), TEXT SIZE, TEXT BACKGROUND, QUIT TO TITLE.
 * Render-only settings, kept in the save. Laid out from the size its text
 * draws at, so the player's Text size grows it and nothing overlaps. */
static void upper(char *b) {
  for (char *c = b; *c; c++) *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
}
static void pause_label(int i, char *buf, size_t cap) {
  switch (i) {
  case 0: snprintf(buf, cap, "RESUME"); break;
  case 1: /* the preset and " · 1080p", the render resolution (the host sets it; read-only here) */
    snprintf(buf, cap, "GRAPHICS: %s", cw_quality_label());
    upper(buf);
    {
      size_t n = strlen(buf);
      snprintf(buf + n, cap > n ? cap - n : 0, " \xC2\xB7 %s", cw_render_res_label());
    }
    break;
  case 2: snprintf(buf, cap, "TEXT SIZE: %s", cw_text_scale_label()); break;
  case 3: snprintf(buf, cap, "TEXT BACKGROUND: %s", cw_text_backdrop_label()), upper(buf); break;
  default: snprintf(buf, cap, "QUIT TO TITLE"); break;
  }
}
#define PAUSE_ROW_SIZE 24.0f /* the rows' text, in canvas units at 540 lines */
#define PAUSE_HEAD_SIZE 60.0f
static const char *pause_hint(void) {
  return glyphs() ? "{m_up|m_down} move   {m_left|m_right} change   {m_ok} choose   {start} resume" : on_touch() ? "tap to choose" : "click to choose";
}
/* the rows' rects (the heading above them, the hint under them), centred on the canvas */
static float pause_head_y(float W, float H, cw_rect *rows) {
  float s = H / 540.0f, rh = tsz(font_title, PAUSE_ROW_SIZE * s) + 16 * s, gap = 8 * s, w = 300 * s;
  char buf[96];
  for (int i = 0; i < PAUSE_N; i++) {
    pause_label(i, buf, sizeof buf);
    w = fmaxf(w, tw(font_title, buf, PAUSE_ROW_SIZE * s) + 40 * s);
  }
  w = fminf(w, W - 24 * s);
  float head = tsz(font_title, PAUSE_HEAD_SIZE * s), hint = prompt_height(pause_hint(), W, 16 * s);
  float total = head + 14 * s + PAUSE_N * rh + (PAUSE_N - 1) * gap + 18 * s + hint;
  float y0 = fmaxf(6 * s, (H - total) * 0.5f), y = y0 + head + 14 * s;
  for (int i = 0; i < PAUSE_N && rows; i++) rows[i] = cw_rect_make(W * 0.5f - w * 0.5f, y + i * (rh + gap), w, rh);
  return y0;
}
static cw_rect pause_drawn[PAUSE_N]; /* where render last drew the rows (the fixed tick hit-tests these) */
static cw_rect pause_row(int i, float W, float H) {
  (void)W, (void)H;
  return pause_drawn[i < 0 ? 0 : i >= PAUSE_N ? PAUSE_N - 1 : i];
}
static void gfx_input(void) {
  if (G.state != GS_PAUSE) {
    cw_quality_menu_close();
    G.open_gfx = false;
    return;
  }
  if (cw_quality_menu_is_open()) { /* the graphics page (cartwheel/quality_menu.h) owns the presses */
    cw_quality_menu_update();
    if (!cw_quality_menu_is_open()) {
      sfx(SFX_SELECT, 0.6f);
      if (cw_quality_menu_changed()) save_write();
    }
    return;
  }
  if (G.state_ticks < 5) return;
  bool hit = any_pressed(G.a_settings) || G.open_gfx;
  G.open_gfx = false;
  if (!hit) return;
  G.menu = 1;
  cw_quality_menu_open();
  sfx(SFX_SELECT, 0.6f);
}

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.03f, 0.02f, 0.01f, 0.62f));
  cw_rect rows[PAUSE_N];
  float y0 = pause_head_y(W, H, rows);
  memcpy(pause_drawn, rows, sizeof rows);
  stencil("PAUSED", 0, y0, PAUSE_HEAD_SIZE * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_CENTER, W);
  char buf[96];
  float th = tsz(font_title, PAUSE_ROW_SIZE * s);
  for (int i = 0; i < PAUSE_N; i++) {
    cw_rect r = rows[i];
    bool cur = G.menu == i;
    plate(r, 1, cur ? rgba(0.84f, 0.56f, 0.24f, 1) : rgba(0.4f, 0.37f, 0.33f, 1));
    pause_label(i, buf, sizeof buf);
    stencil(buf, r.x, r.y + (r.h - th) * 0.5f, PAUSE_ROW_SIZE * s, cur ? rgba(0.95f, 0.78f, 0.5f, 1) : rgba(0.85f, 0.82f, 0.78f, 1), CW_ALIGN_CENTER, r.w);
  }
  cw_rect last = rows[PAUSE_N - 1];
  prompt_line(pause_hint(), W, last.y + last.h + 18 * s + 16 * s * 0.3f, 16 * s);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  static int no_hud = -1; /* dev aid: DERBY_NO_HUD=1 for clean looks at the 3D */
  if (no_hud < 0) no_hud = getenv("DERBY_NO_HUD") != NULL;
  switch (no_hud || keyart_nohud ? -1 : (int)G.state) {
  case GS_TITLE: draw_title(W, H, s); break;
  case GS_BOUT: draw_hud(W, H, s); break;
  case GS_PAUSE: /* the pause screen over the arena alone (its rows would cross the HUD at a large Text size); the graphics page alone */
    if (!cw_quality_menu_is_open()) draw_pause(W, H, s);
    break;
  case GS_RESULTS: draw_results(W, H, s); break;
  default: break;
  }
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = font_hud, .accent = rgba(0.95f, 0.78f, 0.5f, 1) });
  cw_r2d_end();
}

static void shutdown(void *u) {
  (void)u;
  sim_free(&G.S);
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  cw_node_type_register(&arena_type);
#ifdef DERBY_AUTOPILOT
  cw_set_raw_input_filter(autopilot, NULL);
#endif
  static cw_app_desc base = { .title = GAME_TITLE, .width = 960, .height = 540, .debug = true, .init = init, .warmup = warmup, .fixed_update = fixed,
                              .update = update, .render = render, .shutdown = shutdown, .save = &SAVE, .save_size = sizeof SAVE,
                              .touch_pointer = true, /* taps work the menus (the engine's primary-finger pointer) */
                              .max_substeps = 3 /* a slow frame slows the game rather than spiralling */ };
  const char *res = getenv("DERBY_RES");
  if (res && strchr(res, 'x')) {
    base.width = atoi(res);
    base.height = atoi(strchr(res, 'x') + 1);
  }
  return cw_scene_app(NULL, &base);
}
