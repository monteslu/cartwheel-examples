/* derby: a gritty demolition derby on cartwheel and three.c (three.js r186).
 *
 *   flow      title -> QUICK MATCH (one press: you and five CPU bangers) ->
 *             the bout -> the results -> the title
 *   rules     sim.c (cars on Box3D with cw_vehicle3d wheels, crashes in 3D,
 *             damage from contact impulses, deterministic); ai.c the CPU
 *             drivers. Last car running wins.
 *   input     gamepad first (RT throttle, LT brake / reverse, stick steer,
 *             A handbrake, B horn, Y look back), the keyboard (W S A D or
 *             the arrows, Space handbrake, H horn, C look back), the mouse
 *             and taps for the menus, and on-screen controls on touch
 *             screens from the first frame (a steering stick on the left,
 *             GAS / BRAKE / HANDBRAKE on the right)
 *   look      a dusk ProceduralSky (PMREM environment), three.c shadows
 *             following the play, HeightFog for the dust, GTAO, bloom, SMAA,
 *             AgX and a warm, worn LUT; Physical materials (clearcoat paint
 *             scuffed to bare metal round every dent, sheen on the crowd's
 *             clothes); GPU particles for dust, smoke, fire and sparks;
 *             Black Ops One / Russo One HUD with outlines
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
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "world.h"

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
  uint8_t colour, number, kind, pad;
} save_data;
static save_data SAVE;

#define BOUT_SECS 180
#define NCARS 6

static struct {
  /* debug fields */
  int32_t state, phase, clock, alive, winner, ticks, state_ticks, lookback;
  int32_t pos[MAXC * 2], hp[MAXC], dents[MAXC], wrecked[MAXC], slots[MAXC], points[MAXC];
  int32_t crashes, walls, wrecks, max_hit, flips;
  float wheel_yaw0, yaw_rate0;  /* car 1's drawn front-wheel angle and its turn rate (rad, rad/s; + = left) */
  int32_t steer_agrees;         /* 1 while car 1 turns and its wheels point the same way */
  uint32_t hash, state_hash;
  /* the bout */
  int slot[MAXC];
  int kind[MAXC];
  car_look look[MAXC];
  int human_of[MAXC];
  int menu;
  cw_node scene, camera, sun;
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
  cw_action a_fwd, a_back, a_left, a_right, a_hb, a_horn, a_look, a_start, a_esc;
  cw_action m_left, m_right, m_up, m_down, m_ok;
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

/* ── devices: whichever was used last drives the prompts ──────────── */

enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
static int last_device = DEV_PAD;
static bool device_used; /* until a device is used, a touch-first device starts in touch mode */
/* the one switch for the game's own on-screen touch controls (a host that
 * draws its own overlay, wasmcart's doctrine, would turn this off) */
static bool touch_controls = true;
static int TOUCH_STICK = -1, TOUCH_GAS = -1, TOUCH_BRAKE = -1, TOUCH_HB = -1;
static uint32_t prev_mouse_buttons;
static float prev_mx, prev_my;
static bool mouse_clicked, mouse_moved;

static void read_devices(void) {
  const cw_raw_input *in = cw_input_raw();
  int before = last_device;
  last_device = -1;
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse_buttons & 1);
  mouse_moved = in->mouse_x != prev_mx || in->mouse_y != prev_my;
  if (mouse_clicked || (mouse_moved && in->mouse_present && prev_mx + prev_my != 0)) last_device = in->mouse_from_touch ? DEV_TOUCH : DEV_MOUSE;
  prev_mouse_buttons = in->mouse_buttons;
  prev_mx = in->mouse_x;
  prev_my = in->mouse_y;
  for (int i = 0; i < CW_MAX_PADS; i++) {
    const cw_pad_state *p = &in->pads[i];
    if (p->connected && (p->buttons || fabsf(p->axes[CW_AXIS_LX]) > 0.4f || fabsf(p->axes[CW_AXIS_LY]) > 0.4f || p->axes[CW_AXIS_RT] > 0.4f ||
                         p->axes[CW_AXIS_LT] > 0.4f))
      last_device = DEV_PAD;
  }
  for (int k = 0; k < CW_KEY_COUNT; k++)
    if (in->keys_pressed[k]) {
      last_device = DEV_KEYS;
      break;
    }
  for (int t = 0; t < CW_MAX_TOUCHES; t++)
    if (in->touches[t].active || in->touches[t].tapped) last_device = DEV_TOUCH;
  if (last_device >= 0) device_used = true;
  else last_device = device_used ? before : cw_platform_touch_primary() ? DEV_TOUCH : DEV_PAD;
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
#define SAVE_VERSION 1
static uint32_t save_sum(void) {
  const uint8_t *b = (const uint8_t *)&SAVE + offsetof(save_data, bouts);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof SAVE - offsetof(save_data, bouts); i++) h = (h ^ b[i]) * 16777619u;
  return h ^ (SAVE.version | (uint32_t)SAVE.size << 16);
}
static void save_write(void) {
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
  memset(RIG, 0, sizeof RIG);
  RIG[0].cam = G.camera;
  for (int v = 0; v < CW_MAX_VIEWS; v++) RIG[v].snap = true, RIG[v].spectate = -1;
  nviews = 1;
  G.views = 1;
  cw_pipeline_set_views(cw_pipeline_of(cw_scene_app_tree()), NULL, 0);
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

/* splitscreen: a Camera3D per extra player, the pipeline's views split
 * between them, and the costs that scale with views trimmed (GTAO off, a
 * smaller sun shadow) */
static void views_setup(void) {
  int n = 0;
  for (int i = 0; i < MAXC; i++) n += G.human_of[i] >= 0;
  nviews = n < 1 ? 1 : n;
  G.views = nviews;
  if (nviews < 2) return;
  cw_node cams[CW_MAX_VIEWS];
  cams[0] = RIG[0].cam;
  for (int v = 1; v < nviews; v++) {
    char name[16];
    snprintf(name, sizeof name, "Camera%d", v + 1);
    cw_node c = cw_node_new("Camera3D", name);
    cw_node_add_child(G.scene, c);
    cw_node_set(c, "fov", cw_val_float(64));
    cw_node_set(c, "near", cw_val_float(0.2f));
    cw_node_set(c, "far", cw_val_float(600));
    cw_node_set(c, "interpolate", cw_val_bool(true));
    RIG[v].cam = c;
    cams[v] = c;
  }
  cw_pipeline_split(cw_pipeline_of(cw_scene_app_tree()), cams, nviews);
  cw_node post = find_type(G.scene, "PostProcess");
  if (cw_node_valid(post)) cw_node_set(post, "ssao", cw_val_bool(false));
  if (cw_node_valid(G.sun)) cw_node_set(G.sun, "shadow_size", cw_val_int(1024));
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
  fx_init(G.scene, theme);
  int cols[MAXC];
  for (int i = 0; i < MAXC; i++) cols[i] = G.look[i].colour;
  fx_set_colours(cols);
  scenery_build(G.scene, theme, G.info ? (uint32_t)G.info->seed : 1u);
  G.state = GS_BOUT;
  G.state_ticks = 0;
  views_setup();
  music(false);
  crowd_bed(true);
  engines(true);
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
  fx_init(G.scene, 0);
  scenery_build(G.scene, 0, G.info ? (uint32_t)G.info->seed : 1u);
  G.state = GS_TITLE;
  G.state_ticks = 0;
  for (int i = 0; i < MAXC; i++) G.human_of[i] = -1;
  music(true);
  crowd_bed(true);
}

/* ── debug channel ──────────────────────────────────────────────────── */

static int pending_bout = -1;
/* players n: n humans in the next bout (the splitscreen gate; extra players without pads idle) */
static void cmd_players(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 1;
  n = n < 1 ? 1 : n > 4 ? 4 : n;
  for (int p = 1; p < 4; p++) G.joined[p] = p < n;
  G.players = n;
  if (r) cw_reply_printf(r, "{\"players\":%d}", n);
}
static void cmd_bout(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 1;
  pending_bout = n < 1 ? 1 : n;
  cw_reply_printf(r, "{\"bout\":%d}", pending_bout);
}

/* ── the app ────────────────────────────────────────────────────────── */

static cw_font font_hud, font_title, font_small;

static double wall_now(void);
/* the graphics preset changed: thin or thicken the crowd */
static void quality_changed(cw_quality q, void *u) {
  (void)q, (void)u;
  scenery_density(cw_quality_density());
}

static void init(void *u) {
  (void)u;
  double t_init = wall_now();
  save_check();
  cw_quality_restore(SAVE.pad); /* the player's graphics preset, if they chose one */
  cw_quality_on_change(quality_changed, NULL);
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
  cw_debug_i32("count.crashes", &G.crashes);
  cw_debug_i32("count.walls", &G.walls);
  cw_debug_i32("count.wrecks", &G.wrecks);
  cw_debug_i32("count.max_hit", &G.max_hit);
  cw_debug_i32("count.flips", &G.flips);
  cw_debug_f32("car0.wheel_yaw", &G.wheel_yaw0);
  cw_debug_f32("car0.yaw_rate", &G.yaw_rate0);
  cw_debug_i32("check.steer_agrees", &G.steer_agrees);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("cam.lookback", &G.lookback);
  cw_debug_u32("sim.hash", &G.hash);
  cw_debug_i32("input.mode", &last_device); /* 0 mouse, 1 pad, 2 keys, 3 touch */
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  {
    extern uint32_t tex_each[TEX_KINDS + 2];
    cw_debug_field("tex.each", tex_each, CW_DBG_U32, TEX_KINDS + 2);
  }
  cw_debug_u32("scenery.hash", &scenery_hash);
  cw_debug_bytes("save.bytes", &SAVE, sizeof SAVE);
  cw_debug_command("bout", "bout [arena]: a quick match now", cmd_bout);
  cw_debug_command("players", "players <1..4>: humans in the next bout (splitscreen)", cmd_players);
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
  /* the engine through the seat: a fine shake with the revs */
  float rum = 0.006f + 0.01f * c->rpm * (c->wrecked ? 0 : 1);
  eye_out->y += rum * cw_sin(G.ticks * 2.9f + me);
  eye_out->x += rum * 0.6f * cw_sin(G.ticks * 3.7f + 1);
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
  float sh = fx_shake();
  if (sh > 0.001f) {
    e.x += sh * 0.3f * cw_sin(G.ticks * 1.9f + v);
    e.y += sh * 0.3f * cw_sin(G.ticks * 2.7f + 1 + v);
    e.z += sh * 0.3f * cw_sin(G.ticks * 2.3f + 2 + v);
  }
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
  }
}

/* ── layout shared by the update (mouse hit tests) and the HUD ─────── */

static cw_rect title_button(int i, float W, float H) {
  float s = H / 540.0f, w = 300 * s, h = 64 * s;
  return cw_rect_make(W * 0.5f - w * 0.5f, H * 0.6f + i * (h + 14 * s), w, h);
}
/* the on-screen pause button (mouse and touch), top left */
static cw_rect pause_rect(float W, float H) {
  (void)W;
  float s = H / 540.0f;
  return cw_rect_make(10 * s, 10 * s, 64 * s, 44 * s);
}
static bool in_rect(cw_vec2 m, cw_rect r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }

/* ── the fixed tick ─────────────────────────────────────────────────── */

static void sim_sounds(void) {
  const sim *s = &G.S;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    switch (e->kind) {
    case EV_CRASH: {
      float k = e->k;
      if (k > 7) sfx_at(SFX_CRUNCH_BIG, e->at, fminf(1, 0.6f + k * 0.03f), 1.05f - fminf(0.25f, k * 0.01f));
      else if (k > 2.5f) sfx_at(SFX_CRUNCH, e->at, 0.4f + k * 0.07f, 1.1f - k * 0.02f);
      else sfx_at(SFX_THUD, e->at, 0.3f + k * 0.1f, 1);
      if (k > 8) sfx_at(SFX_GLASS, e->at, 0.6f, 1);
      break;
    }
    case EV_WALL: sfx_at(e->k > 5 ? SFX_CRUNCH : SFX_THUD, e->at, 0.4f + fminf(0.5f, e->k * 0.05f), 0.85f); break;
    case EV_LAND: sfx_at(e->k > 6 ? SFX_CRUNCH : SFX_THUD, e->at, 0.4f + fminf(0.5f, e->k * 0.05f), 0.8f); break;
    case EV_RIGHT: sfx_at(SFX_THUD, e->at, 0.35f, 1.2f); break;
    case EV_SCRAPE: sfx_at(SFX_SCRAPE, e->at, fminf(0.7f, e->k * 0.06f), 0.9f + fminf(0.3f, e->k * 0.02f)); break;
    case EV_WRECK: {
      sfx_at(SFX_BOOM, e->at, 1, 1);
      sfx(SFX_CHEER, 0.9f);
      if (e->b >= 0) snprintf(G.feed, sizeof G.feed, "#%d %s WRECKED #%d %s!", G.look[e->b].number, DRIVER[e->b], G.look[e->a].number, DRIVER[e->a]);
      else snprintf(G.feed, sizeof G.feed, "#%d %s IS DONE FOR!", G.look[e->a].number, DRIVER[e->a]);
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

static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  G.ticks++;
  G.state_ticks++;
  read_devices();
  for (int i = 0; i < SFX_COUNT; i++)
    if (A.cooldown[i] > 0) A.cooldown[i]--;
  if (G.feed_ticks > 0) G.feed_ticks--;
  cw_vec2 cs = cw_r2d_canvas_size(), m = mouse_canvas();
  bool confirm = any_pressed(G.m_ok) || any_pressed(G.a_start), back = any_pressed(G.a_esc);
  int dy = (any_pressed(G.m_down) ? 1 : 0) - (any_pressed(G.m_up) ? 1 : 0);
  switch (G.state) {
  case GS_TITLE: {
    /* pads 2-4 join with (A) / START and leave with (B); the menu is P1's */
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
    if ((last_device == DEV_MOUSE || last_device == DEV_TOUCH) && hover >= 0 && mouse_moved) G.menu = hover;
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
    cin in[MAXC] = { 0 };
    for (int i = 0; i < MAXC; i++) {
      if (G.slot[i] == SLOT_HUMAN) in[i] = human_cin(i);
      else if (G.slot[i] >= SLOT_ROOKIE) in[i] = ai_think(&G.S, i, G.slot[i] - SLOT_ROOKIE, &G.ai[i]);
#ifdef DERBY_RAM_TEST
      if (i >= 1) in[i] = (cin){ 0 }; /* the stage holds still: the parked car waits to be wrecked */
#endif
    }
    sim_step(&G.S, in);
    static FILE *rec; /* dev aid: DERBY_RECORD=file logs every tick's input (tools/make-gate-script.mjs) */
    static bool rec_checked;
    if (!rec_checked) {
      rec_checked = true;
      if (getenv("DERBY_RECORD")) rec = fopen(getenv("DERBY_RECORD"), "w");
    }
    if (rec) fprintf(rec, "%d %d %d %d\n", G.S.tick, in[0].thr, in[0].steer, in[0].btn), fflush(rec);
    fx_events(&G.S);
    sim_sounds();
    if (G.S.phase == PH_OVER && G.S.phase_ticks > 200) {
      G.state = GS_RESULTS;
      G.state_ticks = 0;
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
    if (dy) G.menu = (G.menu + dy + 2) % 2, sfx(SFX_MOVE, 0.5f);
    int hover = -1;
    for (int i = 0; i < 2; i++)
      if (in_rect(m, title_button(i, cs.x, cs.y))) hover = i;
    if ((last_device == DEV_MOUSE || last_device == DEV_TOUCH) && hover >= 0 && mouse_moved) G.menu = hover;
    bool pick = G.state_ticks > 5 && (any_pressed(G.m_ok) || (mouse_clicked && hover >= 0));
    if (pick && mouse_clicked && hover >= 0) G.menu = hover;
    if (G.state_ticks > 5 && (any_pressed(G.a_start) || back || (pick && G.menu == 0))) G.state = GS_BOUT;
    else if (pick && G.menu == 1) enter_title();
    break;
  }
  case GS_RESULTS:
    if (G.state_ticks > 120 && (confirm || mouse_clicked)) enter_title();
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
    world_tick(&G.S);
    fx_tick(&G.S);
    scenery_tick(&G.S, G.ticks, G.state == GS_RESULTS && G.state_ticks < 90);
  }
  engines_tick();
  camera_tick();
  {
    t3_vec3 eyes[CW_MAX_VIEWS];
    int n = G.state == GS_TITLE ? 1 : nviews;
    for (int v = 0; v < n; v++) eyes[v] = RIG[v].cam_eye;
    world_lod(eyes, n, n > 1);
    scenery_beams(eyes, n);
  }
  const sim *s = &G.S;
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
    return last_device == DEV_TOUCH ? "tap to drive" : last_device == DEV_MOUSE ? "click to drive" : last_device == DEV_PAD ? "(A) drive   up / down: choose" : "Enter: drive   W / S: choose";
  case GS_RESULTS: return last_device == DEV_TOUCH ? "tap to go again" : last_device == DEV_MOUSE ? "click to go again" : last_device == DEV_PAD ? "(A) to go again" : "Enter to go again";
  default: return "";
  }
}
static void prompt_line(const char *s, float W, float y, float size) {
  cw_text_style st = { .size = size };
  cw_text_metrics mm = cw_font_measure(font_hud, s, &st);
  cw_r2d_rect(cw_rect_make(W * 0.5f - mm.width * 0.5f - size, y - size * 0.3f, mm.width + 2 * size, mm.height + size * 0.6f), rgba(0.04f, 0.03f, 0.02f, 0.6f));
  text(s, 0, y, size, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_CENTER, W);
}

static void draw_title(float W, float H, float s) {
  hazard(cw_rect_make(W * 0.2f, H * 0.08f, W * 0.6f, 8 * s), 0.9f);
  stencil(GAME_TITLE, 0, H * 0.115f, 112 * s, rgba(0.88f, 0.85f, 0.78f, 1), CW_ALIGN_CENTER, W);
  hazard(cw_rect_make(W * 0.2f, H * 0.37f, W * 0.6f, 8 * s), 0.9f);
  prompt_line("six bangers.  one dirt bowl.  last car running wins.", W, H * 0.45f, 17 * s);
  static const char *const ITEMS[2] = { "QUICK MATCH", "POINTS FOR HITS" };
  static const char *const SUBS[2] = { "you and five bangers: last car running", "three minutes: points for every hit" };
  for (int i = 0; i < 2; i++) {
    cw_rect r = title_button(i, W, H);
    bool cur = G.menu == i;
    if (cur) cw_r2d_rect(cw_rect_make(r.x - 6 * s, r.y - 6 * s, r.w + 12 * s, r.h + 12 * s), rgba(0.84f, 0.56f, 0.24f, 0.5f + 0.3f * cw_sin(G.ticks * 0.15f)));
    plate(r, 1, cur ? rgba(0.84f, 0.56f, 0.24f, 1) : rgba(0.4f, 0.37f, 0.33f, 1));
    stencil(ITEMS[i], r.x, r.y + 6 * s, 30 * s, cur ? rgba(0.95f, 0.78f, 0.5f, 1) : rgba(0.85f, 0.82f, 0.78f, 1), CW_ALIGN_CENTER, r.w);
    small(SUBS[i], r.x, r.y + 40 * s, 14 * s, rgba(0.9f, 0.86f, 0.8f, 0.9f), CW_ALIGN_CENTER, r.w);
  }
  prompt_line(prompt(), W, H * 0.93f, 15 * s);
  char buf[64];
  /* the players: P1 always; another pad shows a join tag, a joined one splits the screen */
  bool here[4] = { true, false, false, false }, others = false;
  for (int p = 1; p < 4; p++) {
    int pad = cw_input_player_pad(p);
    here[p] = (pad >= 0 && cw_input_raw()->pads[pad].connected) || G.joined[p];
    others |= here[p];
  }
  if (others) {
    float tw = 120 * s, x0 = W * 0.5f - 2 * tw - 15 * s;
    for (int p = 0; p < 4; p++) {
      if (!here[p]) continue;
      cw_rect r = cw_rect_make(x0 + p * (tw + 10 * s), H * 0.505f, tw, 30 * s);
      bool in = p == 0 || G.joined[p];
      cw_r2d_rect(r, in ? rgba(0.84f, 0.56f, 0.24f, 0.85f) : rgba(0.06f, 0.05f, 0.04f, 0.7f));
      snprintf(buf, sizeof buf, in ? "P%d  IN" : "P%d  (A) JOIN", p + 1);
      small(buf, r.x, r.y + 6 * s, 15 * s, in ? rgba(0.08f, 0.06f, 0.04f, 1) : rgba(1, 0.95f, 0.85f, 0.85f), CW_ALIGN_CENTER, r.w);
    }
  }
  if (SAVE.bouts) {
    snprintf(buf, sizeof buf, "bouts %u   wins %u   wrecks %u", SAVE.bouts, SAVE.wins, SAVE.wrecks);
    small(buf, 0, H * 0.88f, 13 * s, rgba(0.9f, 0.85f, 0.75f, 0.8f), CW_ALIGN_CENTER, W);
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
  stencil(label, r.x, r.y + r.h * 0.5f - r.h * 0.14f, fmaxf(13, r.h * 0.2f), rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER, r.w);
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
  if (!on) small("steer", cc.x - r, cc.y + r + 6 * s, 13 * s, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r * 2);
  touch_button(TOUCH_GAS, W - 170 * k, H - 230 * k, 150 * k, 200 * k, "GAS", rgba(0.3f, 0.65f, 0.25f, 1));
  touch_button(TOUCH_BRAKE, W - 330 * k, H - 150 * k, 140 * k, 120 * k, "BRAKE", rgba(0.75f, 0.2f, 0.12f, 1));
  touch_button(TOUCH_HB, W - 330 * k, H - 290 * k, 140 * k, 110 * k, "SLIDE", rgba(0.85f, 0.6f, 0.12f, 1));
}

/* the field: everyone's number and engine, wrecks struck through; the
 * humans tagged P1-P4 */
static void field_list(float lx, float ly, float s) {
  const sim *S = &G.S;
  char buf[96];
  {
    int k = 0;
    for (int i = 0; i < MAXC; i++) {
      const car *c = &S->c[i];
      if (!c->on) continue;
      float y = ly + k * 30 * s;
      cw_rect r = cw_rect_make(lx, y, 156 * s, 26 * s);
      cw_r2d_rect(r, rgba(0.06f, 0.05f, 0.04f, 0.72f));
      cw_r2d_rect(cw_rect_make(r.x, r.y, 6 * s, r.h), paint_col(i, 1));
      snprintf(buf, sizeof buf, "#%d", G.look[i].number);
      text(buf, r.x + 10 * s, r.y + 3 * s, 15 * s, rgba(1, 0.95f, 0.85f, c->wrecked ? 0.45f : 1), CW_ALIGN_LEFT, 0);
      if (S->rule == RULE_POINTS) {
        snprintf(buf, sizeof buf, "%d", c->points);
        text(buf, r.x, r.y + 3 * s, 15 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_RIGHT, r.w - 8 * s);
      }
      float bw = S->rule == RULE_POINTS ? 50 * s : 92 * s;
      cw_rect bar = cw_rect_make(r.x + 52 * s, r.y + 9 * s, bw, 9 * s);
      cw_r2d_rect(bar, rgba(0, 0, 0, 0.8f));
      cw_r2d_rect(cw_rect_make(bar.x + 1, bar.y + 1, (bar.w - 2) * cw_clampf(c->hp / 100, 0, 1), bar.h - 2), hp_col(c->hp, 1));
      if (c->wrecked) cw_r2d_line(cw_v2(r.x + 4, r.y + r.h * 0.5f), cw_v2(r.x + r.w - 4, r.y + r.h * 0.5f), 2.5f * s, rgba(0.95f, 0.25f, 0.12f, 0.9f));
      if (G.human_of[i] >= 0) {
        snprintf(buf, sizeof buf, "P%d", G.human_of[i] + 1);
        small(buf, r.x - 22 * s, r.y + 4 * s, 13 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_LEFT, 0);
      }
      k++;
    }
  }
}

/* the splitscreen HUD: a plate per view (player tag, number, engine, speed),
 * WRECKED inside a wrecked player's view, the clock where the views meet,
 * and the field in the spare quadrant of a three-way split */
static void draw_split_hud(float W, float H, float s) {
  const sim *S = &G.S;
  const cw_view *vs = NULL;
  int n = cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &vs);
  char buf[64];
  float sv = s * (n > 2 ? 0.72f : 0.85f);
  for (int v = 0; v < n; v++) {
    cw_rect R = cw_rect_make(vs[v].x * W, vs[v].y * H, vs[v].w * W, vs[v].h * H);
    /* the seams between views */
    cw_r2d_rect(cw_rect_make(R.x, R.y, R.w, 2 * s), rgba(0.02f, 0.02f, 0.02f, 1));
    cw_r2d_rect(cw_rect_make(R.x, R.y, 2 * s, R.h), rgba(0.02f, 0.02f, 0.02f, 1));
    int me = car_of_view(v);
    if (me < 0) continue;
    const car *c = &S->c[me];
    cw_rect pr = cw_rect_make(R.x + 10 * sv, R.y + R.h - 78 * sv, 214 * sv, 68 * sv);
    plate(pr, 1, paint_col(me, 1));
    snprintf(buf, sizeof buf, "P%d", v + 1);
    stencil(buf, pr.x + 8 * sv, pr.y + 6 * sv, 26 * sv, rgba(0.95f, 0.78f, 0.5f, 1), CW_ALIGN_LEFT, 0);
    snprintf(buf, sizeof buf, "#%d", G.look[me].number);
    text(buf, pr.x + 56 * sv, pr.y + 6 * sv, 24 * sv, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_LEFT, 0);
    float mph = fabsf(car_speed(c)) * 2.237f;
    snprintf(buf, sizeof buf, "%d MPH", (int)(mph + 0.5f));
    small(buf, pr.x, pr.y + 12 * sv, 15 * sv, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_RIGHT, pr.w - 10 * sv);
    cw_rect bar = cw_rect_make(pr.x + 8 * sv, pr.y + 42 * sv, pr.w - 16 * sv, 14 * sv);
    cw_r2d_rect(bar, rgba(0.02f, 0.02f, 0.02f, 0.9f));
    cw_r2d_rect(cw_rect_make(bar.x + 2, bar.y + 2, (bar.w - 4) * cw_clampf(c->hp / 100, 0, 1), bar.h - 4), hp_col(c->hp, 1));
    if (S->rule == RULE_POINTS) {
      snprintf(buf, sizeof buf, "%d PTS", c->points);
      text(buf, pr.x, pr.y - 24 * sv, 18 * sv, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_LEFT, 0);
    }
    if (c->wrecked && S->phase == PH_GO) {
      float a = cw_clampf(c->wreck_ticks / 20.0f, 0, 1);
      stencil("WRECKED", R.x, R.y + R.h * 0.36f, 48 * sv, rgba(0.95f, 0.3f, 0.15f, a), CW_ALIGN_CENTER, R.w);
    }
  }
  /* the clock and the running count where the views meet */
  cw_rect top = cw_rect_make(W * 0.5f - 90 * s, n == 2 && vs[1].y > 0 ? H * 0.5f - 22 * s : 6 * s, 180 * s, 40 * s);
  plate(top, 1, rgba(0.4f, 0.37f, 0.33f, 1));
  int secs = (S->clock + 59) / 60;
  snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
  text(buf, top.x + 10 * s, top.y + 6 * s, 24 * s, secs <= 20 && S->phase == PH_GO && (G.ticks / 20) % 2 ? rgba(1, 0.4f, 0.2f, 1) : rgba(1, 0.95f, 0.85f, 1),
       CW_ALIGN_LEFT, 0);
  if (S->rule == RULE_LAST_RUNNING) snprintf(buf, sizeof buf, "%d LEFT", S->alive);
  else snprintf(buf, sizeof buf, "POINTS");
  stencil(buf, top.x, top.y + 9 * s, 17 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_RIGHT, top.w - 10 * s);
  if (n == 3) {
    cw_r2d_rect(cw_rect_make(W * 0.5f, H * 0.5f, W * 0.5f, H * 0.5f), rgba(0.07f, 0.06f, 0.05f, 1));
    stencil(GAME_TITLE, W * 0.5f, H * 0.53f, 40 * s, rgba(0.88f, 0.85f, 0.78f, 0.5f), CW_ALIGN_CENTER, W * 0.5f);
    field_list(W * 0.5f + 40 * s, H * 0.62f, s * 0.62f);
  }
}

static void draw_hud(float W, float H, float s) {
  const sim *S = &G.S;
  char buf[96];
  int me = focus_car();
  bool touch = touch_controls && last_device == DEV_TOUCH;
  bool split = nviews > 1;
  if (split) draw_split_hud(W, H, s), me = -1; /* the per-view plates stand in for P1's */
  /* top centre: running cars and the clock */
  if (!split) {
  cw_rect top = cw_rect_make(W * 0.5f - 130 * s, 8 * s, 260 * s, 52 * s);
  plate(top, 1, rgba(0.4f, 0.37f, 0.33f, 1));
  int secs = (S->clock + 59) / 60;
  snprintf(buf, sizeof buf, "%d:%02d", secs / 60, secs % 60);
  text(buf, top.x + 12 * s, top.y + 8 * s, 30 * s, secs <= 20 && S->phase == PH_GO && (G.ticks / 20) % 2 ? rgba(1, 0.4f, 0.2f, 1) : rgba(1, 0.95f, 0.85f, 1),
       CW_ALIGN_LEFT, 0);
  if (S->rule == RULE_LAST_RUNNING) snprintf(buf, sizeof buf, "%d RUNNING", S->alive);
  else snprintf(buf, sizeof buf, "POINTS");
  stencil(buf, top.x, top.y + 12 * s, 22 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_RIGHT, top.w - 12 * s);
  }
  /* the player's plate: number, engine bar, the car from above */
  if (me >= 0) {
    const car *c = &S->c[me];
    float px = touch ? 84 * s : 12 * s, py = touch ? 10 * s : H - 112 * s;
    cw_rect pr = cw_rect_make(px, py, 250 * s, 100 * s);
    plate(pr, 1, paint_col(me, 1));
    car_diagram(pr.x + 32 * s, pr.y + 50 * s, s, me);
    snprintf(buf, sizeof buf, "#%d", G.look[me].number);
    stencil(buf, pr.x + 64 * s, pr.y + 6 * s, 30 * s, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_LEFT, 0);
    small(c->wrecked ? "WRECKED" : "ENGINE", pr.x + 64 * s, pr.y + 44 * s, 13 * s, rgba(0.9f, 0.85f, 0.75f, 1), CW_ALIGN_LEFT, 0);
    cw_rect bar = cw_rect_make(pr.x + 64 * s, pr.y + 64 * s, 170 * s, 18 * s);
    cw_r2d_rect(bar, rgba(0.02f, 0.02f, 0.02f, 0.9f));
    float k = cw_clampf(c->hp / 100, 0, 1);
    cw_r2d_rect(cw_rect_make(bar.x + 2, bar.y + 2, (bar.w - 4) * k, bar.h - 4), hp_col(c->hp, 1));
    for (int t = 1; t < 10; t++) cw_r2d_rect(cw_rect_make(bar.x + bar.w * t / 10, bar.y, 1.5f, bar.h), rgba(0, 0, 0, 0.6f));
    float mph = fabsf(car_speed(c)) * 2.237f;
    snprintf(buf, sizeof buf, "%d", (int)(mph + 0.5f));
    text(buf, pr.x + 150 * s, pr.y + 8 * s, 30 * s, rgba(1, 0.95f, 0.85f, 1), CW_ALIGN_RIGHT, 84 * s);
    small("MPH", pr.x + 150 * s, pr.y + 42 * s, 12 * s, rgba(0.9f, 0.85f, 0.75f, 0.9f), CW_ALIGN_RIGHT, 84 * s);
    if (S->rule == RULE_POINTS) {
      snprintf(buf, sizeof buf, "%d PTS", c->points);
      text(buf, pr.x, pr.y - 28 * s, 20 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_LEFT, 0);
    }
  }
  /* the field: everyone's number and engine, wrecks struck through */
  float lx = W - 168 * s, ly = 70 * s;
  if (touch) ly = 8 * s, lx = W - 168 * s;
  if (!split && (!touch || G.state == GS_PAUSE)) field_list(lx, ly, s);
  /* the countdown */
  if (S->phase == PH_COUNTDOWN) {
    int n = 3 - S->phase_ticks / 60;
    float f = (S->phase_ticks % 60) / 60.0f, pop = 1 + 0.4f * (1 - f) * (1 - f);
    snprintf(buf, sizeof buf, "%d", n < 1 ? 1 : n);
    stencil(buf, 0, H * 0.2f, 110 * s * pop, rgba(0.84f, 0.56f, 0.24f, 1 - f * 0.3f), CW_ALIGN_CENTER, W);
    const char *how = split ? "" : touch ? "left thumb: steer   right thumb: gas, brake, slide"
                    : last_device == DEV_PAD ? "RT: gas   LT: brake / reverse   (A): handbrake   (B): horn   (Y): look back"
                                             : "W / S: gas, brake   A / D: steer   Space: handbrake   H: horn   C: look back";
    if (*how) prompt_line(how, W, H * 0.62f, 14 * s);
  } else if (S->phase == PH_GO && S->phase_ticks < 50) {
    float f = S->phase_ticks / 50.0f;
    stencil("WRECK 'EM!", 0, H * 0.2f, 84 * s * (1 + 0.3f * (1 - f)), rgba(0.84f, 0.56f, 0.24f, 1 - f), CW_ALIGN_CENTER, W);
  }
  if (me >= 0 && S->c[me].wrecked && S->phase == PH_GO) {
    float a = cw_clampf(S->c[me].wreck_ticks / 20.0f, 0, 1);
    hazard(cw_rect_make(0, H * 0.3f, W, 8 * s), a * 0.9f);
    stencil("WRECKED", 0, H * 0.31f + 10 * s, 70 * s, rgba(0.95f, 0.3f, 0.15f, a), CW_ALIGN_CENTER, W);
    hazard(cw_rect_make(0, H * 0.31f + 92 * s, W, 8 * s), a * 0.9f);
    if (S->c[me].wreck_ticks > 150) prompt_line("watching the rest of the bout", W, H * 0.52f, 14 * s);
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
      snprintf(buf, sizeof buf, "#%d %s takes it", G.look[S->winner].number, DRIVER[S->winner]);
      prompt_line(buf, W, H * 0.38f, 18 * s);
    }
  }
  if (G.feed_ticks > 0) {
    float a = cw_clampf(G.feed_ticks / 20.0f, 0, 1);
    cw_text_style st = { .size = 18 * s };
    cw_text_metrics mm = cw_font_measure(font_hud, G.feed, &st);
    cw_r2d_rect(cw_rect_make(W * 0.5f - mm.width * 0.5f - 14 * s, H * 0.14f - 4 * s, mm.width + 28 * s, mm.height + 8 * s), rgba(0.55f, 0.12f, 0.05f, 0.75f * a));
    text(G.feed, 0, H * 0.14f, 18 * s, rgba(1, 0.92f, 0.75f, a), CW_ALIGN_CENTER, W);
  }
  if (touch && G.state == GS_BOUT) draw_touch(s);
  /* a pause button for the mouse and touch (pads and keys have START / Esc) */
  if (G.state == GS_BOUT && (last_device == DEV_MOUSE || last_device == DEV_TOUCH)) {
    cw_rect pr = pause_rect(W, H);
    plate(pr, 0.9f, rgba(0.4f, 0.37f, 0.33f, 1));
    for (int k = 0; k < 2; k++) cw_r2d_rect(cw_rect_make(pr.x + pr.w * (0.36f + 0.18f * k), pr.y + pr.h * 0.25f, pr.w * 0.1f, pr.h * 0.5f), rgba(1, 0.95f, 0.85f, 1));
  }
}

static void draw_results(float W, float H, float s) {
  const sim *S = &G.S;
  char buf[96];
  float in = cw_clampf(G.state_ticks / 20.0f, 0, 1);
  int me = focus_car();
  bool won = me >= 0 && S->winner == me;
  hazard(cw_rect_make(0, H * 0.04f, W, 9 * s), in);
  stencil(won ? "YOU WON THE BOUT!" : "RESULTS", 0, H * 0.07f, 56 * s, rgba(0.84f, 0.56f, 0.24f, in), CW_ALIGN_CENTER, W);
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
  float tw = 600 * s, rh = 34 * s, x = (W - tw) * 0.5f, y = H * 0.26f;
  plate(cw_rect_make(x - 12 * s, y - 36 * s, tw + 24 * s, rh * m + 48 * s), in, rgba(0.4f, 0.37f, 0.33f, 1));
  static const char *const HEAD[4] = { "DEALT", "TAKEN", "WRECKS", "POINTS" };
  for (int c = 0; c < 4; c++) small(HEAD[c], x + 290 * s + c * 78 * s, y - 30 * s, 14 * s, rgba(0.84f, 0.56f, 0.24f, in), CW_ALIGN_CENTER, 78 * s);
  for (int r = 0; r < m; r++) {
    int i = order[r];
    const car *c = &S->c[i];
    float yy = y + r * rh;
    snprintf(buf, sizeof buf, "%d.", r + 1);
    text(buf, x, yy + 4 * s, 18 * s, rgba(1, 0.95f, 0.85f, in), CW_ALIGN_LEFT, 0);
    cw_r2d_rect(cw_rect_make(x + 34 * s, yy + 6 * s, 8 * s, 20 * s), paint_col(i, in));
    snprintf(buf, sizeof buf, "#%d %s", G.look[i].number, DRIVER[i]);
    text(buf, x + 50 * s, yy + 4 * s, 18 * s, G.human_of[i] >= 0 ? rgba(0.84f, 0.56f, 0.24f, in) : rgba(1, 0.95f, 0.85f, in), CW_ALIGN_LEFT, 0);
    int vals[4] = { (int)(c->dealt + 0.5f), (int)(c->taken + 0.5f), c->wrecks, c->points };
    for (int k = 0; k < 4; k++) {
      snprintf(buf, sizeof buf, "%d", vals[k]);
      text(buf, x + 290 * s + k * 78 * s, yy + 4 * s, 18 * s, rgba(1, 0.95f, 0.85f, in), CW_ALIGN_CENTER, 78 * s);
    }
  }
  if (G.state_ticks > 120) prompt_line(prompt(), W, H * 0.92f, 16 * s);
}

/* the graphics preset on the pause screen (cartwheel/quality.h): Y, G or a
 * tap steps Low, Medium, High, Ultra. Render-only, so it is read in
 * update, never in the simulation; the choice is kept in the save */
static cw_rect gfx_rect(float W, float H) {
  float s = H / 540.0f;
  return cw_rect_make(W * 0.5f - 170 * s, H * 0.44f, 340 * s, 46 * s);
}
static void gfx_input(void) {
  if (G.state != GS_PAUSE || G.state_ticks < 5) return;
  const cw_raw_input *in = cw_input_raw();
  bool hit = in->keys_pressed[10]; /* G */
  for (int i = 0; i < CW_MAX_PADS; i++) hit |= (in->pads[i].pressed & (1u << CW_PAD_NORTH)) != 0;
  cw_vec2 cs = cw_r2d_canvas_size();
  if ((in->mouse_pressed & 1) && in_rect(mouse_canvas(), gfx_rect(cs.x, cs.y))) hit = true;
  if (!hit) return;
  cw_quality_cycle();
  SAVE.pad = cw_quality_saved();
  save_write();
}
/* " · 1080p": the render resolution beside the graphics preset (the host
 * sets it, RetroArch's Internal Resolution; read-only here) */
/* a label's size so that it fits maxw (shrunk, never grown): the graphics
 * line is longer at 1080p than at 540p, MEDIUM longer than LOW */
static float fit_title(const char *s, float size, float maxw) {
  float w = cw_font_measure(font_title, s, &(cw_text_style){ .size = size }).width;
  return w > maxw && w > 0 ? size * maxw / w : size;
}
static void gfx_res_suffix(char *buf, size_t cap) {
  size_t n = strlen(buf);
  snprintf(buf + n, cap > n ? cap - n : 0, " \xC2\xB7 %s", cw_render_res_label());
}
static void draw_gfx(float W, float H, float s) {
  cw_rect r = gfx_rect(W, H);
  plate(r, 1, rgba(0.4f, 0.37f, 0.33f, 1));
  char buf[64];
  snprintf(buf, sizeof buf, "GRAPHICS: %s", cw_quality_label());
  for (char *c = buf; *c; c++) *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
  gfx_res_suffix(buf, sizeof buf); /* the render resolution: the host sets it */
  float sz = fit_title(buf, 24 * s, r.w - 16 * s);
  stencil(buf, r.x, r.y + 10 * s + (24 * s - sz) * 0.5f, sz, rgba(0.85f, 0.82f, 0.78f, 1), CW_ALIGN_CENTER, r.w);
  text(last_device == DEV_TOUCH ? "tap to change" : "Y / G to change", r.x, r.y + r.h + 4 * s, 14 * s, rgba(0.85f, 0.82f, 0.78f, 0.8f), CW_ALIGN_CENTER, r.w);
}

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.03f, 0.02f, 0.01f, 0.5f));
  stencil("PAUSED", 0, H * 0.3f, 70 * s, rgba(0.84f, 0.56f, 0.24f, 1), CW_ALIGN_CENTER, W);
  static const char *const ITEMS[2] = { "RESUME", "QUIT TO TITLE" };
  for (int i = 0; i < 2; i++) {
    cw_rect r = title_button(i, W, H);
    plate(r, 1, G.menu == i ? rgba(0.84f, 0.56f, 0.24f, 1) : rgba(0.4f, 0.37f, 0.33f, 1));
    stencil(ITEMS[i], r.x, r.y + 14 * s, 28 * s, G.menu == i ? rgba(0.95f, 0.78f, 0.5f, 1) : rgba(0.85f, 0.82f, 0.78f, 1), CW_ALIGN_CENTER, r.w);
  }
  const char *how = last_device == DEV_PAD ? "(A): choose   START / (B): resume" : last_device == DEV_KEYS ? "W / S: move   Enter: choose   Esc: resume"
                  : last_device == DEV_TOUCH ? "tap to choose" : "click to choose";
  prompt_line(how, W, H * 0.9f, 14 * s);
  draw_gfx(W, H, s);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  static int no_hud = -1; /* dev aid: DERBY_NO_HUD=1 for clean looks at the 3D */
  if (no_hud < 0) no_hud = getenv("DERBY_NO_HUD") != NULL;
  switch (no_hud ? -1 : (int)G.state) {
  case GS_TITLE: draw_title(W, H, s); break;
  case GS_BOUT: draw_hud(W, H, s); break;
  case GS_PAUSE:
    draw_hud(W, H, s);
    draw_pause(W, H, s);
    break;
  case GS_RESULTS: draw_results(W, H, s); break;
  default: break;
  }
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
