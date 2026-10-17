/* pugball: car-soccer with pugs for cars and a fat, furious cat for a
 * ball, on cartwheel and three.c (three.js r186).
 *
 *   flow      title -> Exhibition: the team sheet (1v1 to 3v3, each slot a
 *             player or a CPU, every pug's coat, hat and boost trail) ->
 *             stadium (locked until the Pug Cup opens it) -> the match ->
 *             full time (score, MVP, stats)
 *             title -> Pug Cup: five fixtures in order, one per stadium,
 *             each opening the next and a cosmetic for your pug
 *   rules     sim.c (car-ball on Box3D, arena.c its static arena; deterministic); ai.c the CPUs
 *   input     gamepad first (triggers throttle, stick steer / air control,
 *             A jump, B boost, X powerslide / air roll, Y ball cam), the
 *             keyboard (W S A D, Space, Shift, Ctrl, C) and the mouse
 *             (menus; left boost, right jump on the pitch). Up to four
 *             pads; a pad pressing (A) on the team sheet takes a slot.
 *   look      per-stadium ProceduralSky (PMREM environment), three.c
 *             shadows following the play, HeightFog, GTAO, bloom, SMAA,
 *             AgX and a LUT grade; Physical materials (sheen fur on the
 *             pugs and the cat, clearcoat lacquer, iridescent boost orbs,
 *             glass walls); GPU particles; Lilita One / Bungee / Fredoka
 *             HUD with soft outlines
 *
 * Determinism: the match replays from the input stream (fixed ticks, the
 * tick action snapshot, quantised inputs); the debug fields name the state
 * the gates compare across hosts.
 *
 * Built twice: `pugball` reads devices; `pugball_auto` plays a pad script
 * itself through cw_set_raw_input_filter, so input-free hosts run it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/gl.h"
#include "cartwheel/gpu.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/input.h"
#include "cartwheel/lighting.h"
#include "cartwheel/math.h"
#include "cartwheel/platform.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "world.h"

#define STADIUMS 5
static const char *const STADIUM_TITLES[STADIUMS] = { "Barkley Park", "Neon Kennel Bowl", "Sunset Sands", "Frosty Backyard",
                                                      "Royal Rose Garden" };
static const float THEME_RGB[TH_KINDS][3] = { { 0.45f, 0.85f, 0.35f }, { 0.7f, 0.4f, 1 }, { 1, 0.65f, 0.35f }, { 0.6f, 0.85f, 1 },
                                              { 1, 0.45f, 0.6f } };
static const char *const PUG_NAMES[MAXP] = { "Biscuit", "Pickles", "Waffles", "Noodle", "Peanut", "Gizmo" };
static const char *const TEAM_NAMES[2] = { "BISCUITS", "SAUSAGES" };
/* the Pug Cup: your team size per fixture */
static const int CUP_SIZE[STADIUMS] = { 1, 2, 2, 3, 3 };

/* ── the Stadium node: every stadium scene's root ──────────────────── */

typedef struct stadium_info {
  int index;           /* 1..5; 0 = the title backdrop */
  const char *title, *blurb;
  int theme, seed, cpu_level, match_secs;
} stadium_info;

static const char *const THEME_NAMES[] = { "PARK", "NEON", "BEACH", "SNOW", "GARDEN", NULL };
static const cw_prop stadium_props[] = {
  CW_PROP_FIELD(stadium_info, index, CW_T_INT, CW_PROP_DEFAULT, "Stadium number 1..5 (0: the title backdrop)."),
  CW_PROP_FIELD(stadium_info, title, CW_T_STRING, CW_PROP_DEFAULT, "Its name."),
  CW_PROP_FIELD(stadium_info, blurb, CW_T_STRING, CW_PROP_DEFAULT, "One line about it."),
  { .name = "theme", .type = CW_T_ENUM, .flags = CW_PROP_DEFAULT, .offset = offsetof(stadium_info, theme), .size = sizeof(int),
    .enum_names = THEME_NAMES, .doc = "Look and scenery." },
  CW_PROP_FIELD(stadium_info, seed, CW_T_INT, CW_PROP_DEFAULT, "Scenery and crowd."),
  CW_PROP_FIELD(stadium_info, cpu_level, CW_T_INT, CW_PROP_DEFAULT, "Pug Cup: 0 rookie, 1 pro, 2 all-star."),
  CW_PROP_FIELD(stadium_info, match_secs, CW_T_INT, CW_PROP_DEFAULT, "The match clock."),
  CW_PROP_END,
};
/* every frame before the scene draws: the goal nets that moved, blended between their last two steps */
static void stadium_pre_render(cw_node n, void *data, double alpha) {
  (void)n, (void)data;
  net_frame((float)alpha);
}
static cw_node_type stadium_type = { .name = "Stadium", .parent = "Node3D", .data_size = sizeof(stadium_info), .props = stadium_props,
                                     .pre_render = stadium_pre_render,
                                     .doc = "Root of a pugball stadium: its name, theme and the Cup's opposition." };

/* ── state ──────────────────────────────────────────────────────────── */

typedef enum { GS_TITLE, GS_SHEET, GS_STADIUM, GS_MATCH, GS_PAUSE, GS_FULLTIME, GS_LOCKER, GS_REPLAY } game_state;
enum { MODE_EXHIBITION, MODE_CUP };
enum { SLOT_OFF, SLOT_HUMAN, SLOT_ROOKIE, SLOT_PRO, SLOT_ALLSTAR, SLOT_KINDS };
static const char *const SLOT_NAME[SLOT_KINDS] = { "Empty", "Player", "CPU Rookie", "CPU Pro", "CPU All-Star" };

typedef enum {
  SFX_HIT, SFX_HIT_BIG, SFX_MEOW, SFX_YOWL, SFX_HISS, SFX_PURR, SFX_BARK, SFX_BOOST, SFX_PAD, SFX_PAD_BIG, SFX_JUMP, SFX_DODGE,
  SFX_LAND, SFX_BUMP, SFX_DEMO, SFX_HORN, SFX_CROWD, SFX_CHEER, SFX_BEEP, SFX_GO, SFX_WHISTLE, SFX_MOVE, SFX_SELECT, SFX_LOCKED,
  SFX_UNLOCK, SFX_BOING, SFX_COUNT
} sfx_id;

/* progress, kept in cw_app_desc.save: a small versioned, checksummed layout */
typedef struct save_data {
  uint32_t magic;          /* 'PUGB' */
  uint16_t version, size;
  uint32_t checksum;       /* FNV-1a over everything after this field */
  uint8_t won[STADIUMS];   /* Cup fixtures won */
  uint8_t shown[STADIUMS]; /* its unlock has played on the select screen */
  uint8_t slot[MAXP], coat[MAXP], hat[MAXP], trail[MAXP];
  uint8_t secs_i, cup_next, pad[2]; /* pad: the graphics settings (cw_quality_saved, 16 bits little-endian) */
  uint8_t sound, spare;    /* sound: 0 speakers, 1 headphones (HRTF); the bytes that were the padding before best_margin */
  int32_t best_margin[STADIUMS];
} save_data;
_Static_assert(offsetof(save_data, best_margin) == 52, "the save keeps its layout");
static save_data SAVE;

static const int MATCH_SECS[3] = { 120, 180, 300 };

static struct {
  /* debug fields */
  int32_t state, mode, stadium, phase, clock, winner, ticks, state_ticks, ballcam;
  int32_t score[2], ball_pos[3], pos[MAXP * 3], slots[MAXP], goals[MAXP], points[MAXP], boost[MAXP];
  int32_t hits, demos, jumps, dodges, pads, bounces, mvp;
  uint32_t hash, state_hash;
  /* the match */
  int slot[MAXP];
  pug_look look[MAXP];
  int secs_i;
  int menu, cursor, col;
  int unlock_tile, unlock_ticks, bump_tile, bump_ticks;
  cw_node scene, camera, sun;
  float sun_extent, sun_extent_set; /* the scene's shadow box half size; the one set now */
  int sun_cascades;     /* the scene's cascaded sun shadows (split screen draws one shadow box instead) */
  float sun_far;        /* the scene's shadow box depth (the one box split screen draws) */
  stadium_info *info;
  sim S;
  ai_mem ai[MAXP];
  int human_of[MAXP]; /* cw player index (the pad) for a human slot, -1 */
  int pad_of[MAXP];   /* the team sheet: the pad that plays a human slot (0..3) */
  t3_vec3 cam_at, cam_eye;
  bool cam_snap;
  /* the chase camera's springs (critically damped): eye, look point, yaw */
  t3_vec3 ch_eye, ch_eye_v, ch_look, ch_look_v;
  float ch_yaw, ch_yaw_v;
  bool ch_live;
  /* the per-frame chase camera (cam_frame): the human pug and the cat
   * between the last two ticks, and the camera's own smoothed state */
  bool chase_on;
  t3_vec3 ip_prev[MAXP], ip_cur[MAXP], ib_prev, ib_cur;
  float cf_yaw[4], cf_dist[4], cf_high[4], cf_lift[4], cf_ground[4];
  double cf_last;
  bool cf_live[4];
  int ballcam_h[4]; /* each pad's ball cam */
  cw_node cams[4];  /* each view's camera (cams[0] is the scene's) */
  int nviews;       /* split screen: one view per human (0 = one camera) */
  int view_slot[4]; /* the slot each view follows, in pad order */
  int camtrace;
  int feed_ticks;
  char feed[96];
  int cup_unlock_hat;
  cw_action a_fwd, a_back, a_left, a_right, a_pfwd, a_pback, a_jump, a_boost, a_slide, a_ballcam, a_start, a_esc;
  cw_action m_left, m_right, m_up, m_down, m_ok, m_alt, m_team, m_leave;
} G;

/* ── self-timing: what a frame costs, on every host (wall clock) ──────
 * frame = render end to render end; fixed = the ticks' CPU in that frame;
 * steps = ticks that frame ran. pugball_perf waits for the GPU after each
 * frame (a 1-pixel read), so its frame time is all of the frame's work.
 * One stderr line per 120 frames: "pugball-perf ..." */
static double wall_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}
static double render_t0; /* when the frame's render began (the end of update) */
static struct {
  double last, fixed_acc, tick_t0;
  int steps, frames;
  float sum, max, fixed_sum, fixed_max, render_sum, steps_max;
  int over;
  float frame_ms, fixed_ms, render_ms, gpu_ms, max_rep; /* debug fields */
  int32_t over_rep;
} PERF;
static void perf_tick_begin(void) { PERF.tick_t0 = wall_ms(); }
static void perf_tick_end(void) {
  PERF.fixed_acc += wall_ms() - PERF.tick_t0;
  PERF.steps++;
}
/* the graphics preset (cartwheel/quality.h): the engine caps the post and
 * shadows and steps down on a slow machine (never in a deterministic run,
 * where every host must draw the same frame); the game thins its grass and
 * crowd by the preset's density. perf.tier: 0 at High, 1 Medium, 2 Low */
static cw_node find_type(cw_node n, const char *type);
static int quality_tier;
static void apply_quality(void) {
  quality_tier = CW_QUALITY_HIGH - (int)cw_quality_get();
  float k = cw_quality_density();
  /* split screen draws the scene in each view: thinner grass (the crowd
   * keeps its fans, drawn as the light ones) */
  scenery_quality(k, G.nviews && k > 0.66f ? 0.66f : k);
  scenery_light_crowd(G.nviews >= 2); /* and its views are small: the light fans */
  if (!G.nviews) world_view_detail((t3_vec3){ 0, 0, 0 }, true); /* one camera: every pug in full */
  if (!cw_node_valid(G.scene)) return;
  cw_node post = find_type(G.scene, "PostProcess");

  if (cw_node_valid(G.sun)) {
    cw_node_set(G.sun, "shadow_size", cw_val_int(2048));
    /* one view: cascades fitted to its camera, crisp shadows near and far.
     * Split screen keeps the one shadow box round every human (sun_follow):
     * the views share the scene, and cascades fitted to each view would be
     * redrawn once per view */
    bool cascades = G.nviews < 2 && G.sun_cascades > 0;
    cw_node_set(G.sun, "shadow_cascades", cw_val_int(cascades ? G.sun_cascades : 0));
    /* a cascade's camera stands 200 m up the light from its slice (r186's
     * lightMargin): its depth range has to reach past that */
    cw_node_set(G.sun, "shadow_far", cw_val_float(cascades ? 420 : G.sun_far));
  }
  scenery_flood_quality();
}
static void sound_apply(void);
static void quality_changed(cw_quality q, void *u) {
  (void)q, (void)u;
  apply_quality();
  sound_apply();
}

static void perf_frame(double render_t0) {
  cw_quality_auto_measure(G.state == GS_MATCH); /* the preset steps down when play runs slow */
#ifdef PUGBALL_PERF
  if (cw_gpu_backend() != CW_GPU_WEBGPU) {
    uint8_t px[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  }
#endif
  double now = wall_ms();
  if (PERF.last > 0) {
    float d = (float)(now - PERF.last), f = (float)PERF.fixed_acc, r = (float)(now - render_t0);
    PERF.sum += d, PERF.fixed_sum += f, PERF.render_sum += r;
    if (d > PERF.max) PERF.max = d;
    if (f > PERF.fixed_max) PERF.fixed_max = f;
    if (PERF.steps > PERF.steps_max) PERF.steps_max = (float)PERF.steps;
    PERF.over += d > 17.5f;
    PERF.frames++;
    if (PERF.frames == 120) {
      PERF.frame_ms = PERF.sum / 120, PERF.fixed_ms = PERF.fixed_sum / 120, PERF.render_ms = PERF.render_sum / 120;
      PERF.gpu_ms = cw_pipeline_gpu_ms(cw_pipeline_of(cw_scene_app_tree()));
      PERF.max_rep = PERF.max;
      PERF.over_rep = PERF.over;
      char line[900];
      int o = snprintf(line, sizeof line,
                       "pugball-perf state=%d frame_ms=%.2f max_ms=%.2f over16=%d fixed_ms=%.2f fixed_max=%.2f steps_max=%d render_cpu_ms=%.2f gpu_ms=%.2f",
                       G.state, PERF.frame_ms, PERF.max, PERF.over, PERF.fixed_ms, PERF.fixed_max, (int)PERF.steps_max, PERF.render_ms, PERF.gpu_ms);
      const cw_pass_stat *ps;
      int n = cw_pipeline_pass_stats(cw_pipeline_of(cw_scene_app_tree()), &ps);
      for (int i = 0; i < n && o < (int)sizeof line - 40; i++)
        if (ps[i].ran && ps[i].gpu_ms_avg > 0.2f) o += snprintf(line + o, sizeof line - (size_t)o, " %s=%.2f", ps[i].name, ps[i].gpu_ms_avg);
      fprintf(stderr, "%s\n", line);
      PERF.sum = PERF.max = PERF.fixed_sum = PERF.fixed_max = PERF.render_sum = PERF.steps_max = 0;
      PERF.over = PERF.frames = 0;
    }
  }
  PERF.last = now;
  PERF.fixed_acc = 0;
  PERF.steps = 0;
}

static struct { int32_t clustered, shadowed, cascades; } LIT; /* debug: the last frame's lighting */

/* ── autopilot: a gate's pad script compiled in ───────────────────── */

#ifdef PUGBALL_AUTOPILOT
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

static const char *const SFX_FILES[SFX_COUNT] = {
  "audio/hit.wav", "audio/hit_big.wav", "audio/meow.wav", "audio/yowl.wav", "audio/hiss.wav", "audio/purr.wav", "audio/bark.wav",
  "audio/boost.wav", "audio/pad.wav", "audio/pad_big.wav", "audio/jump.wav", "audio/dodge.wav", "audio/land.wav", "audio/bump.wav",
  "audio/demo.wav", "audio/horn.wav", "audio/crowd.wav", "audio/cheer.wav", "audio/beep.wav", "audio/go.wav", "audio/whistle.wav",
  "audio/move.wav", "audio/select.wav", "audio/locked.wav", "audio/unlock.wav", "audio/boing.wav" };
static const char *const MUSIC[TH_KINDS + 1] = { "audio/music_park.ogg", "audio/music_neon.ogg", "audio/music_beach.ogg",
  "audio/music_snow.ogg", "audio/music_garden.ogg", "audio/music_title.ogg" };
static struct {
  bool on;
  cw_bus music_bus, sfx_bus, crowd_bus;
  cw_res sfx_res[SFX_COUNT], music_res;
  cw_sound sfx[SFX_COUNT];
  cw_voice music, crowd, purr;
  int music_now;
  int cooldown[SFX_COUNT];
  FILE *wav;
  uint32_t wav_frames;
} A = { .music_now = -1 };

/* how long each sound waits before it can play again (ticks): a short
 * thump retriggered every few ticks reads as a buzz */
static const uint8_t COOL[SFX_COUNT] = { 8, 10, 25, 40, 30, 120, 20, 10, 4, 8, 6, 8, 8, 12, 10, 60, 1, 60, 10, 30, 30, 3, 5, 8, 30, 12 };
static void sfx_v(sfx_id id, float volume, float pitch) {
  if (!A.on || A.cooldown[id] > 0) return;
  A.cooldown[id] = COOL[id];
  cw_audio_play(A.sfx[id], &(cw_play_desc){ .bus = A.sfx_bus, .volume = volume, .pitch = pitch });
}
static void sfx(sfx_id id, float volume) { sfx_v(id, volume, 1); }
static void sfx_at(sfx_id id, t3_vec3 at, float volume, float pitch) {
  if (!A.on || A.cooldown[id] > 0) return;
  A.cooldown[id] = COOL[id];
  cw_play_desc d = { .bus = A.sfx_bus, .volume = volume, .pitch = pitch, .spatial = true };
  d.space = (cw_voice_3d){ .position = at, .model = CW_ATTEN_INVERSE, .ref_distance = 14, .max_distance = 140, .rolloff = 0.5f };
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

/* the player's output from the pause screen: headphones filter the nearest
 * spatial voices with the HRTF (a hit behind you is heard behind you),
 * speakers pan them. The HRTF's voice limit follows effects detail. */
static void sound_apply(void) {
  if (!A.on) return;
  cw_audio_set_hrtf_voice_limit(cw_quality_count(16, "hrtf_voices"));
  cw_audio_set_output(SAVE.sound ? CW_AUDIO_HEADPHONES : CW_AUDIO_SPEAKERS);
}

static void audio_init(void) {
  /* dev aid: PUG_AUDIO_WAV=file renders the mix offline, one frame of
   * samples per rendered frame, into a WAV (deterministic runs: checking the
   * game's own sound without a device or a browser in the way) */
  const char *wav = getenv("PUG_AUDIO_WAV");
  A.on = getenv("PUG_NO_AUDIO") == NULL && cw_audio_init(&(cw_audio_desc){ .offline = wav != NULL });
  if (A.on && wav) {
    A.wav = fopen(wav, "wb");
    if (A.wav) fwrite((uint8_t[44]){ 0 }, 1, 44, A.wav); /* the header is written at shutdown */
  }
  if (!A.on) return;
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  A.crowd_bus = cw_bus_create("crowd", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.3f);
  cw_bus_set_volume(A.crowd_bus, 0.45f);
  const char *mute = getenv("PUG_MUTE"); /* dev aid: music, sfx and/or crowd, to listen to the rest */
  if (mute && strstr(mute, "music")) cw_bus_set_volume(A.music_bus, 0);
  if (mute && strstr(mute, "sfx")) cw_bus_set_volume(A.sfx_bus, 0);
  if (mute && strstr(mute, "crowd")) cw_bus_set_volume(A.crowd_bus, 0);
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1, 0.1f } });
  for (int i = 0; i < SFX_COUNT; i++) {
    A.sfx_res[i] = cw_res_load(SFX_FILES[i], 0);
    A.sfx[i] = cw_res_audio_sound(A.sfx_res[i], &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = 4, .bus = A.sfx_bus });
  }
  sound_apply();
}

static void crowd_bed(bool on) {
  if (!A.on) return;
  if (on && CW_HANDLE_IS_NULL(A.crowd)) A.crowd = cw_audio_play(A.sfx[SFX_CROWD], &(cw_play_desc){ .loop = true, .fade_in = 1.5f, .bus = A.crowd_bus });
  if (!on && !CW_HANDLE_IS_NULL(A.crowd)) {
    cw_voice_stop(A.crowd, 0.8f);
    A.crowd = (cw_voice){ 0 };
  }
}

/* ── devices: whichever was used last drives the prompts ──────────── */

enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
static int last_device = DEV_PAD;
static bool device_used; /* until a device is used, a touch-first device starts in touch mode */
/* the one switch for the game's own on-screen touch controls (a host that
 * draws its own overlay, wasmcart's doctrine, would turn this off) */
static bool touch_controls = true;
static int TOUCH_STICK = -1, TOUCH_JUMP = -1, TOUCH_BOOST = -1, TOUCH_SLIDE = -1, TOUCH_CAM = -1;
static uint32_t prev_mouse_buttons;
static float prev_mx, prev_my;
static bool mouse_clicked, mouse_moved;

static void read_devices(void) {
  const cw_raw_input *in = cw_input_raw();
  int before = last_device;
  last_device = -1;
  /* the engine turns the primary finger into the mouse (cw_app_desc.touch_pointer):
   * a tap is a click in the menus; mouse_from_touch tells the prompts it was a finger */
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse_buttons & 1);
  mouse_moved = in->mouse_x != prev_mx || in->mouse_y != prev_my;
  if (mouse_clicked || (mouse_moved && in->mouse_present && prev_mx + prev_my != 0))
    last_device = in->mouse_from_touch ? DEV_TOUCH : DEV_MOUSE;
  prev_mouse_buttons = in->mouse_buttons;
  prev_mx = in->mouse_x;
  prev_my = in->mouse_y;
  for (int i = 0; i < CW_MAX_PADS; i++) {
    const cw_pad_state *p = &in->pads[i];
    if (p->connected && (p->buttons || fabsf(p->axes[CW_AXIS_LX]) > 0.4f || fabsf(p->axes[CW_AXIS_LY]) > 0.4f || p->axes[CW_AXIS_RT] > 0.4f))
      last_device = DEV_PAD;
  }
  for (int k = 0; k < CW_KEY_COUNT; k++)
    if (in->keys_pressed[k]) {
      last_device = DEV_KEYS;
      break;
    }
  /* any finger shows the on-screen controls (a second finger is not the mouse) */
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
static bool m_confirm(void) { return any_pressed(G.m_ok); }
static bool m_back(void) { return any_pressed(G.a_esc); }
static int m_dx(void) { return (any_pressed(G.m_right) ? 1 : 0) - (any_pressed(G.m_left) ? 1 : 0); }
static int m_dy(void) { return (any_pressed(G.m_down) ? 1 : 0) - (any_pressed(G.m_up) ? 1 : 0); }

/* ── save data ──────────────────────────────────────────────────────── */

#define SAVE_MAGIC 0x42475550u /* 'PUGB' */
#define SAVE_VERSION 1

static uint32_t save_sum(void) {
  const uint8_t *b = (const uint8_t *)&SAVE + offsetof(save_data, won);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof SAVE - offsetof(save_data, won); i++) h = (h ^ b[i]) * 16777619u;
  return h ^ (SAVE.version | (uint32_t)SAVE.size << 16);
}
static uint32_t gfx_saved(void) { return SAVE.pad[0] | (uint32_t)SAVE.pad[1] << 8; }
static void save_write(void) {
  uint32_t q = cw_quality_saved();
  if (q) {
    SAVE.pad[0] = (uint8_t)q;
    SAVE.pad[1] = (uint8_t)(q >> 8);
  }
  SAVE.checksum = save_sum();
  cw_save_commit();
}
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC;
  SAVE.version = SAVE_VERSION;
  SAVE.size = (uint16_t)sizeof SAVE;
  SAVE.shown[0] = 1;
  SAVE.secs_i = 2;
  static const uint8_t SL[MAXP] = { SLOT_HUMAN, SLOT_PRO, SLOT_PRO, SLOT_PRO, SLOT_OFF, SLOT_OFF };
  for (int i = 0; i < MAXP; i++) {
    SAVE.slot[i] = SL[i];
    SAVE.coat[i] = (uint8_t)(i % PUG_KINDS);
    SAVE.hat[i] = (uint8_t)(i == 0 ? HAT_CAP : (i % 3 == 1 ? HAT_BEANIE : HAT_NONE));
    SAVE.trail[i] = TRAIL_FIRE;
  }
  SAVE.checksum = save_sum();
}
static void save_check(void) {
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) {
    if (SAVE.magic) cw_warn("pugball: save data rejected (magic %08x version %u), starting fresh", SAVE.magic, SAVE.version);
    save_reset();
  }
  for (int i = 0; i < MAXP; i++) {
    if (SAVE.slot[i] >= SLOT_KINDS) SAVE.slot[i] = SLOT_PRO;
    if (SAVE.coat[i] >= PUG_KINDS) SAVE.coat[i] = 0;
    if (SAVE.hat[i] >= HAT_KINDS) SAVE.hat[i] = 0;
    if (SAVE.trail[i] >= TRAIL_KINDS) SAVE.trail[i] = 0;
  }
  if (SAVE.secs_i > 2) SAVE.secs_i = 2;
}
static bool stadium_open(int i) { return i == 0 || (i < STADIUMS && SAVE.won[i - 1]); }
/* hats unlock with the Cup: cap and beanie from the start, then one per fixture won */
static bool hat_open(int h) {
  static const int NEED[HAT_KINDS] = { 0, 0, 5, 1, 0, 3 }; /* fixtures won */
  int won = 0;
  for (int i = 0; i < STADIUMS; i++) won += SAVE.won[i];
  return won >= NEED[h];
}
static bool trail_open(int t) {
  int won = 0;
  for (int i = 0; i < STADIUMS; i++) won += SAVE.won[i];
  return t <= won;
}
static void save_preset(int n) {
  save_reset();
  for (int i = 0; i < n && i < STADIUMS; i++) SAVE.won[i] = SAVE.shown[i] = 1;
  if (n < STADIUMS) SAVE.shown[n] = 1;
  save_write();
}
static int32_t dbg_unlocked, dbg_won;
static void save_fields(void) {
  dbg_unlocked = dbg_won = 0;
  for (int i = 0; i < STADIUMS; i++) {
    dbg_unlocked += stadium_open(i);
    if (SAVE.won[i]) dbg_won |= 1 << i;
  }
}
static void cmd_progress(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3 && !strcmp(argv[1], "preset")) {
    int n = atoi(argv[2]);
    if (n < 0 || n > STADIUMS) {
      cw_reply_raw(r, "{\"error\":\"progress preset <0..5>\"}");
      return;
    }
    save_preset(n);
  }
  save_fields();
  cw_reply_printf(r, "{\"unlocked\":%d,\"won\":%d,\"checksum\":%u}", dbg_unlocked, dbg_won, SAVE.checksum);
}

/* ── scenes and matches ─────────────────────────────────────────────── */

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
  if (index) snprintf(path, sizeof path, "scenes/stadium%d.cwscene", index);
  else snprintf(path, sizeof path, "scenes/title.cwscene");
  G.scene = cw_scene_instance_path(path);
  if (CW_HANDLE_IS_NULL(G.scene)) {
    cw_error("pugball: %s failed to load", path);
    return;
  }
  cw_node_add_child(cw_tree_root(t), G.scene);
  G.info = cw_node_data(G.scene, "Stadium");
  G.camera = find_type(G.scene, "Camera3D");
  for (int k = 0; k < 4; k++) G.cams[k] = (cw_node){ 0 };
  if (G.nviews) {
    cw_pipeline_set_views(cw_pipeline_of(cw_scene_app_tree()), NULL, 0);
    G.nviews = 0;
  }
  if (cw_node_valid(G.camera)) cw_node_set(G.camera, "interpolate", cw_val_bool(true));
  G.sun = cw_node_get(G.scene, "Sun");
  cw_value ext = { 0 };
  G.sun_extent = G.sun_extent_set = cw_node_valid(G.sun) && cw_node_get_prop(G.sun, "shadow_extent", &ext) ? (float)ext.f : 28;
  G.sun_cascades = cw_node_valid(G.sun) && cw_node_get_prop(G.sun, "shadow_cascades", &ext) ? (int)ext.i : 0;
  G.sun_far = cw_node_valid(G.sun) && cw_node_get_prop(G.sun, "shadow_far", &ext) ? (float)ext.f : 140;
  G.stadium = index;
  G.cam_snap = true;
}

/* how dark the stadium's sky is (its ProceduralSky's hour): 1 at night, when
 * the floodlights carry the pitch; 0 by day */
static float night(void) {
  cw_node sky = find_type(G.scene, "ProceduralSky");
  cw_value h = { 0 };
  if (!cw_node_valid(sky) || !cw_node_get_prop(sky, "time_of_day", &h)) return 0;
  return h.f >= 19.5 || h.f < 5.5 ? 1 : 0;
}

static int sheet_pad[MAXP]; /* the team sheet's pads, kept through a Cup fixture */
static void assign_humans(void) {
  for (int i = 0; i < MAXP; i++) G.human_of[i] = G.slot[i] == SLOT_HUMAN ? G.pad_of[i] : -1;
}
/* the team sheet's pads: a slot made a player takes the lowest free pad */
static int free_pad(void) {
  for (int p = 0; p < 4; p++) {
    bool used = false;
    for (int i = 0; i < MAXP; i++) used |= G.slot[i] == SLOT_HUMAN && G.pad_of[i] == p;
    if (!used) return p;
  }
  return -1;
}
static int slot_of_pad(int p) {
  for (int i = 0; i < MAXP; i++)
    if (G.slot[i] == SLOT_HUMAN && G.pad_of[i] == p) return i;
  return -1;
}
/* pads in slot order (a save from before pads had slots, and the Cup) */
static void pads_in_order(void) {
  int k = 0;
  for (int i = 0; i < MAXP; i++) {
    G.pad_of[i] = -1;
    if (G.slot[i] == SLOT_HUMAN) {
      if (k < 4) G.pad_of[i] = k++;
      else G.slot[i] = SLOT_PRO; /* four pads at most */
    }
  }
}
static int humans(void) {
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += G.human_of[i] >= 0;
  return n;
}

/* ── the goal replay ────────────────────────────────────────────────────
 * The last 3 s before a goal (and a beat after), kept as whole sim states
 * and shown again at under half speed from cinematic cameras once the
 * celebration is over. It is presentation only: the match (G.S) is not
 * touched, the states are copies, and the in-between ticks are blended for
 * the slow motion. Any button skips it. */
#define REPLAY_N 220      /* ticks kept: 180 before the goal, 40 after */
#define REPLAY_AFTER 40
#define REPLAY_AT 170     /* PH_GOAL tick it starts on (the goal cam has had its say) */
#define REPLAY_SPEED 0.45f
static struct {
  sim hist[REPLAY_N];
  int n, head;
  sim view;              /* the state being shown */
  float t;               /* playback position in kept ticks */
  int shown;             /* the last kept tick whose events went out */
  int count;             /* replays shown this match (a gate field) */
} RP;

static const sim *replay_at(int k) { return &RP.hist[(RP.head - RP.n + k + REPLAY_N) % REPLAY_N]; }
static void replay_record(const sim *s) {
  if (s->phase == PH_KICKOFF && s->phase_ticks <= 1) RP.n = 0; /* never blend across a kickoff */
  if (s->phase == PH_GOAL && s->phase_ticks > REPLAY_AFTER) return;
  RP.hist[RP.head] = *s;
  RP.head = (RP.head + 1) % REPLAY_N;
  if (RP.n < REPLAY_N) RP.n++;
}
static t3_vec3 v3lerp(t3_vec3 a, t3_vec3 b, float t) { return v3add(a, v3mul(v3sub(b, a), t)); }
static t3_quat qnlerp(t3_quat a, t3_quat b, float t) {
  if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = (t3_quat){ -b.x, -b.y, -b.z, -b.w };
  t3_quat q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return l > 0 ? (t3_quat){ q.x / l, q.y / l, q.z / l, q.w / l } : a;
}
/* the shown state at the playback position: kept tick k blended toward k+1;
 * a tick's events go out once, when playback reaches it */
static void replay_view(void) {
  int k = (int)RP.t;
  float f = RP.t - (float)k;
  const sim *a = replay_at(k), *b = replay_at(k + 1 < RP.n ? k + 1 : k);
  RP.view = *a;
  for (int i = 0; i < MAXP; i++) {
    RP.view.p[i].p = v3lerp(a->p[i].p, b->p[i].p, f);
    RP.view.p[i].v = v3lerp(a->p[i].v, b->p[i].v, f);
    RP.view.p[i].q = qnlerp(a->p[i].q, b->p[i].q, f);
  }
  RP.view.ball = v3lerp(a->ball, b->ball, f);
  RP.view.ball_v = v3lerp(a->ball_v, b->ball_v, f);
  if (k == RP.shown) RP.view.ev_n = 0;
  RP.shown = k;
}
static void replay_start(void) {
  G.state = GS_REPLAY;
  G.state_ticks = 0;
  RP.t = 0;
  RP.shown = -1;
  RP.count++;
  replay_view();
  G.cam_snap = true;
}
static void replay_end(void) {
  G.state = GS_MATCH;
  G.state_ticks = 0;
  G.cam_snap = true;
  RP.n = 0;
}

static void start_match(int stadium) {
  load_scene(stadium);
  bool on[MAXP];
  for (int i = 0; i < MAXP; i++) on[i] = G.slot[i] != SLOT_OFF;
  assign_humans();
  int secs = MATCH_SECS[G.secs_i];
  const char *ps = getenv("PUG_SECS");
  if (ps && atoi(ps) > 0) secs = atoi(ps);
#ifdef PUGBALL_CUP_TEST
  if (G.mode == MODE_CUP) secs = 6;
#endif
  sim_init(&G.S, on, secs, 1000u + (uint32_t)stadium * 7919u);
  RP.n = RP.head = RP.count = 0;
  for (int i = 0; i < MAXP; i++) ai_reset(&G.ai[i], i, (uint32_t)stadium + 1);
#ifdef PUGBALL_DEMO_TEST
  /* the demolition gate's stage: the red pug parked 16 m dead ahead of blue 1,
   * the cat well out of the way */
  {
    const pug *a = &G.S.p[0];
    t3_vec3 f = pug_fwd(a);
    sim_place_pug(&G.S, 1, a->p.x + f.x * 16, a->p.z + f.z * 16, 0);
    sim_place_ball(&G.S, (t3_vec3){ -15, RB, 22 });
  }
#endif
  int theme = G.info ? G.info->theme : 0;
  world_build(G.scene, &G.S, theme, G.look);
  fx_init(G.scene, theme);
  for (int i = 0; i < MAXP; i++) fx_set_trail(i, G.look[i].trail);
  scenery_build(G.scene, theme, G.info ? (uint32_t)G.info->seed : 1u);
  scenery_floodlights(night());
  G.state = GS_MATCH;
  G.state_ticks = 0;
  G.mvp = -1;
  apply_quality();
  music(theme);
  crowd_bed(true);
}

/* the title backdrop: the park, the pugs on parade */
static void enter_title(void) {
  load_scene(0);
  bool on[MAXP] = { true, true, true, true, true, true };
  sim_init(&G.S, on, 300, 5);
  pug_look looks[MAXP];
  for (int i = 0; i < MAXP; i++) looks[i] = G.look[i];
  world_build(G.scene, &G.S, TH_PARK, looks);
  fx_init(G.scene, TH_PARK);
  scenery_build(G.scene, TH_PARK, 7);
  apply_quality(); /* the sun's cascades */
  G.state = GS_TITLE;
  G.state_ticks = 0;
  music(TH_KINDS);
  crowd_bed(false);
}

static void enter_stadium_select(void) {
  G.state = GS_STADIUM;
  G.state_ticks = 0;
  G.unlock_tile = -1;
  G.bump_ticks = 0;
  for (int i = 1; i < STADIUMS; i++)
    if (stadium_open(i) && !SAVE.shown[i]) {
      SAVE.shown[i] = 1;
      save_write();
      G.unlock_tile = i;
      G.unlock_ticks = 0;
      G.cursor = i;
      break;
    }
}

/* the Pug Cup fixture: you and CPU teammates against the stadium's CPUs */
static void begin_cup(int stadium) {
  G.mode = MODE_CUP;
  load_scene(stadium);
  int n = CUP_SIZE[stadium - 1], lvl = G.info ? G.info->cpu_level : 0;
  for (int i = 0; i < MAXP; i++) {
    int team = i & 1, k = i / 2;
    if (k >= n) G.slot[i] = SLOT_OFF;
    else if (i == 0) G.slot[i] = SLOT_HUMAN;
    else G.slot[i] = team == TEAM_BLUE ? SLOT_PRO : SLOT_ROOKIE + lvl;
  }
  pads_in_order();
  start_match(stadium);
}

/* ── debug channel ──────────────────────────────────────────────────── */

static int pending_stadium = -1;
static void cmd_stadium(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 0;
  if (n < 1 || n > STADIUMS) {
    cw_reply_error(r, "usage: stadium <1..5>: an exhibition there now");
    return;
  }
  pending_stadium = n;
  cw_reply_printf(r, "{\"stadium\":%d}", n);
}

/* ── the app ────────────────────────────────────────────────────────── */

static cw_font font_ui, font_ui_bold, font_title, font_score;

static void cam_frame(cw_tree *t, double alpha, void *user);
static void cmd_camtrace(int argc, const char **argv, cw_reply *r);
static void view_hook(int view, void *user);

/* the render-time camera log (debug command camtrace2): what is drawn each
 * frame, the camera object's position and the pug's place on screen */
static int camlog;
static void cam_log(cw_tree *t, double a, void *u) {
  (void)t, (void)a, (void)u;
  if (!camlog || !cw_node_valid(G.camera)) return;
  int me = -1;
  for (int i = 0; i < MAXP; i++)
    if (G.human_of[i] >= 0) { me = i; break; }
  if (me < 0 || G.state != GS_MATCH) return;
  t3_object *c = cw_node3d_object(G.camera);
  if (!c) return;
  t3_quat q = c->quaternion;
  t3_vec3 qu = { q.x, q.y, q.z };
#define QROT(v) v3add(v3add(v, v3mul(v3mul(v3cross(qu, v), 2), q.w)), v3cross(qu, v3mul(v3cross(qu, v), 2)))
  t3_vec3 fw = QROT(((t3_vec3){ 0, 0, -1 })), up = QROT(((t3_vec3){ 0, 1, 0 })), rt = v3cross(fw, up);
#undef QROT
  t3_vec3 P = v3add(world_pug_render_pos(me), (t3_vec3){ 0, 0.3f, 0 }), v = v3sub(P, c->position);
  float z = v3dot(v, fw);
  fprintf(stderr, "pcam %.4f %.4f %.4f %.4f %.5f %.5f\n", cw_time(), c->position.x, c->position.y, c->position.z,
          z > 0.01f ? v3dot(v, rt) / z : 0, z > 0.01f ? v3dot(v, up) / z : 0);
}
static void cmd_camlog(int argc, const char **argv, cw_reply *r) {
  camlog = argc > 1 ? atoi(argv[1]) : !camlog;
  cw_reply_printf(r, "{\"camtrace2\":%d}", camlog);
}

pug_dev DEV;
static void dev_env(void) {
  DEV.tt = getenv("PUG_TT");
  DEV.tt_pug = getenv("PUG_TT_PUG") ? atoi(getenv("PUG_TT_PUG")) : 2;
  DEV.cam_pug = getenv("PUG_CAM_PUG") != NULL;
  DEV.cam_crowd = getenv("PUG_CAM_CROWD") != NULL;
  DEV.cam_cat = getenv("PUG_CAM_CAT") != NULL;
  DEV.no_replay = getenv("PUG_NO_REPLAY") != NULL;
  DEV.record = getenv("PUG_RECORD");
}

static void init(void *u) {
  dev_env();
  (void)u;
  save_check();
  cw_quality_on_change(quality_changed, NULL);
  cw_quality_restore(gfx_saved()); /* the player's graphics settings, if they chose any; else Auto */
  cw_quality_auto(true, 19);
#ifdef PUGBALL_PRESET
  save_preset(PUGBALL_PRESET);
#endif
  const char *pp = getenv("PUG_PROGRESS");
  if (pp && atoi(pp) >= 0 && atoi(pp) <= STADIUMS) save_preset(atoi(pp));
  save_fields();
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("pugball: input.cfg had errors (see above)");
  cw_input_commit_defaults();
  G.a_fwd = cw_action_find("thr_fwd");
  G.a_back = cw_action_find("thr_back");
  G.a_left = cw_action_find("steer_l");
  G.a_right = cw_action_find("steer_r");
  G.a_pfwd = cw_action_find("pitch_fwd");
  G.a_pback = cw_action_find("pitch_back");
  G.a_jump = cw_action_find("jump");
  G.a_boost = cw_action_find("boost");
  G.a_slide = cw_action_find("slide");
  G.a_ballcam = cw_action_find("ballcam");
  G.a_start = cw_action_find("start");
  G.a_esc = cw_action_find("back");
  G.m_left = cw_action_find("m_left");
  G.m_right = cw_action_find("m_right");
  G.m_up = cw_action_find("m_up");
  G.m_down = cw_action_find("m_down");
  G.m_ok = cw_action_find("m_ok");
  G.m_alt = cw_action_find("m_alt");
  G.m_team = cw_action_find("m_team");
  G.m_leave = cw_action_find("m_leave");
  /* on-screen touch controls (logical window coordinates): a floating steer
   * stick on the left half; jump, boost, powerslide and ball cam on the
   * right, thumb-sized and clear of the scoreboard. The stick also throttles
   * (pushed: go; pulled straight back: reverse): human_pin */
  {
    int ww, wh;
    cw_window_size(&ww, &wh);
    float W = (float)ww, H = (float)wh, k = H / 540.0f;
    TOUCH_STICK = cw_touch_add_stick(0, H * 0.3f, W * 0.45f, H * 0.7f, 64 * k, true);
    TOUCH_JUMP = cw_touch_add_button(W - 168 * k, H - 178 * k, 150 * k, 150 * k);
    TOUCH_BOOST = cw_touch_add_button(W - 318 * k, H - 140 * k, 140 * k, 130 * k);
    TOUCH_SLIDE = cw_touch_add_button(W - 168 * k, H - 318 * k, 150 * k, 128 * k);
    TOUCH_CAM = cw_touch_add_button(W - 100 * k, 76 * k, 92 * k, 70 * k);
    char b[24];
    snprintf(b, sizeof b, "touch:%d.left", TOUCH_STICK);
    cw_action_bind_text(G.a_left, b);
    snprintf(b, sizeof b, "touch:%d.right", TOUCH_STICK);
    cw_action_bind_text(G.a_right, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_JUMP);
    cw_action_bind_text(G.a_jump, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_BOOST);
    cw_action_bind_text(G.a_boost, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_SLIDE);
    cw_action_bind_text(G.a_slide, b);
    snprintf(b, sizeof b, "touch:%d", TOUCH_CAM);
    cw_action_bind_text(G.a_ballcam, b);
  }
  cw_action_set_deadzone(G.a_left, 0.12f);
  cw_action_set_deadzone(G.a_right, 0.12f);
  cw_action_set_deadzone(G.a_fwd, 0.05f);
  cw_action_set_deadzone(G.a_back, 0.05f);
  audio_init();
  /* a 10 px distance range at the 48 px bake: room for the HUD's outlines
   * (up to 0.14 of the size on small text) and the titles' under-glow */
  const cw_font_desc fd = { .mode = CW_FONT_SDF, .sdf_padding = 10 };
  font_ui = cw_font_load("fonts/Fredoka-SemiBold.ttf", &fd);
  font_ui_bold = cw_font_load("fonts/Fredoka-Bold.ttf", &fd);
  font_title = cw_font_load("fonts/LilitaOne-Regular.ttf", &fd);
  font_score = cw_font_load("fonts/Bungee-Regular.ttf", &fd);
  for (int i = 0; i < MAXP; i++) {
    G.slot[i] = SAVE.slot[i];
    G.look[i] = (pug_look){ SAVE.coat[i], SAVE.hat[i], 0, SAVE.trail[i] };
  }
  pads_in_order();
  memcpy(sheet_pad, G.pad_of, sizeof sheet_pad);
  G.secs_i = SAVE.secs_i;
  G.ballcam = 15;
  for (int k = 0; k < 4; k++) G.ballcam_h[k] = 1;
  G.mvp = -1;
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.mode", &G.mode);
  cw_debug_i32("game.stadium", &G.stadium);
  cw_debug_i32("match.phase", &G.phase);
  cw_debug_i32("match.clock", &G.clock);
  cw_debug_field("match.score", G.score, CW_DBG_I32, 2);
  cw_debug_i32("match.winner", &G.winner);
  cw_debug_i32("match.mvp", &G.mvp);
  cw_debug_i32("match.replays", &RP.count);
  /* the lighting the last frame drew (render side, not gate fields) */
  cw_debug_i32("light.clustered", &LIT.clustered);
  cw_debug_i32("light.shadowed", &LIT.shadowed);
  cw_debug_i32("light.cascades", &LIT.cascades);
  cw_debug_field("ball.pos", G.ball_pos, CW_DBG_I32, 3);
  cw_debug_field("players.pos", G.pos, CW_DBG_I32, MAXP * 3);
  cw_debug_field("players.slot", G.slots, CW_DBG_I32, MAXP);
  cw_debug_field("players.goals", G.goals, CW_DBG_I32, MAXP);
  cw_debug_field("players.points", G.points, CW_DBG_I32, MAXP);
  cw_debug_field("players.boost", G.boost, CW_DBG_I32, MAXP);
  cw_debug_i32("count.hits", &G.hits);
  cw_debug_i32("count.demos", &G.demos);
  cw_debug_i32("count.jumps", &G.jumps);
  cw_debug_i32("count.dodges", &G.dodges);
  cw_debug_i32("count.pads", &G.pads);
  cw_debug_i32("count.bounces", &G.bounces);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("cam.ballcam", &G.ballcam); /* bit p: pad p's ball cam */
  cw_debug_i32("cam.views", &G.nviews);  /* split screen: the views drawn (0: one camera) */
  cw_debug_u32("sim.hash", &G.hash);
  cw_debug_f32("perf.frame_ms", &PERF.frame_ms);
  cw_debug_f32("perf.fixed_ms", &PERF.fixed_ms);
  cw_debug_f32("perf.render_ms", &PERF.render_ms);
  cw_debug_f32("perf.gpu_ms", &PERF.gpu_ms);
  cw_debug_f32("perf.max_ms", &PERF.max_rep);
  cw_debug_i32("perf.over16", &PERF.over_rep);
  cw_debug_i32("perf.tier", &quality_tier);
  cw_debug_i32("input.mode", &last_device); /* 0 mouse, 1 pad, 2 keys, 3 touch */
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  cw_debug_u32("scenery.hash", &scenery_hash);
  cw_debug_i32("save.unlocked", &dbg_unlocked);
  cw_debug_i32("save.won", &dbg_won);
  cw_debug_bytes("save.bytes", &SAVE, sizeof SAVE);
  cw_debug_command("stadium", "stadium <1..5>: an exhibition there now", cmd_stadium);
  cw_debug_command("camtrace2", "camtrace2 [0|1]: log the drawn camera and the pug's screen place every frame", cmd_camlog);
  cw_tree_add_hook(cw_scene_app_tree(), 2, 10, cam_log, NULL); /* after the nodes write their objects */
  cw_debug_command("camtrace", "camtrace [0|1]: print the chase camera every frame (pcam t eye.xyz pug.sx pug.sy)", cmd_camtrace);
  cw_tree_add_hook(cw_scene_app_tree(), 2, -10, cam_frame, NULL); /* before the nodes write their objects */
  cw_pipeline_set_view_hook(cw_pipeline_of(cw_scene_app_tree()), view_hook, NULL);
  cw_pipeline_set_views_shared_post(cw_pipeline_of(cw_scene_app_tree()), true); /* split screen: one post chain over the views */
  cw_debug_command("progress", "progress [show | preset <0..5>]: the save data; preset n = Cup fixtures 1..n won", cmd_progress);
  G.state_hash = 2166136261u;
  enter_title();
  const char *a = getenv("PUG_STADIUM"); /* dev aid: straight into an exhibition */
  if (a && atoi(a) >= 1 && atoi(a) <= STADIUMS) {
    const char *ts = getenv("PUG_TEAM"); /* pugs a side */
    int n = ts ? CW_CLAMP(atoi(ts), 1, 3) : 2;
    for (int i = 0; i < MAXP; i++) G.slot[i] = i / 2 < n ? (i == 0 && !getenv("PUG_CPU_ONLY") ? SLOT_HUMAN : SLOT_ALLSTAR) : SLOT_OFF;
    pads_in_order();
    G.mode = MODE_EXHIBITION;
    start_match(atoi(a));
  }
}

/* ── player input on the pitch ──────────────────────────────────────── */

static int8_t q127(float v) { return (int8_t)(cw_clampf(v, -1, 1) * 127.0f); }

static pin human_pin(int slot) {
  int p = G.human_of[slot];
  pin in = { 0 };
  in.thr = q127(cw_action_strength(p, G.a_fwd) - cw_action_strength(p, G.a_back));
  in.steer = q127(cw_input_axis(p, G.a_left, G.a_right));
  in.pitch = q127(cw_input_axis(p, G.a_pfwd, G.a_pback));
  if (cw_action_pressed(p, G.a_jump)) in.btn |= PB_JUMP;
  if (cw_action_pressed(p, G.a_boost)) in.btn |= PB_BOOST;
  if (cw_action_pressed(p, G.a_slide)) in.btn |= PB_SLIDE;
  /* the touch stick throttles too: pushed anywhere goes, pulled straight back
   * reverses; it only pitches the instant a jump is pressed (the dodge's
   * direction), so pushing it to drive never noses a jump into the floor */
  if (p == 0 && cw_touch_control_active(TOUCH_STICK)) {
    cw_vec2 v = cw_touch_stick_value(TOUCH_STICK);
    float mag = sqrtf(v.x * v.x + v.y * v.y);
    if (in.thr == 0 && mag > 0.3f) in.thr = v.y > 0.7f && fabsf(v.x) < 0.55f ? -127 : 127;
    const pug *u = &G.S.p[slot];
    bool jump_now = (in.btn & PB_JUMP) && !(u->last_btn & PB_JUMP);
    in.pitch = jump_now && mag > 0.35f ? q127(v.y) : 0;
  }
  return in;
}

/* ── camera (fixed tick: identical on every host) ─────────────────── */

static int focus_slot(void) {
  for (int i = 0; i < MAXP; i++)
    if (G.human_of[i] >= 0) return i;
  return -1;
}

/* a critically damped spring: x chases target with angular frequency w */
static void spring3(t3_vec3 *x, t3_vec3 *v, t3_vec3 target, float w) {
  const float dt = 1.0f / 60;
  t3_vec3 a = v3sub(v3mul(v3sub(target, *x), w * w), v3mul(*v, 2 * w));
  *v = v3add(*v, v3mul(a, dt));
  *x = v3add(*x, v3mul(*v, dt));
}
static float wrap_pi(float a) {
  while (a > 3.1415927f) a -= 6.2831853f;
  while (a < -3.1415927f) a += 6.2831853f;
  return a;
}
/* two or more humans: one steep camera that frames every human and the cat.
 * It looks down from above the near touchline; the distance is the least
 * that puts every point inside a 26 degree cone round the view */
static void fit_tick(t3_vec3 *eye_out, t3_vec3 *at_out) {
  const sim *s = &G.S;
  t3_vec3 pts[MAXP + 1];
  int n = 0;
  pts[n++] = s->ball;
  for (int i = 0; i < MAXP; i++)
    if (G.human_of[i] >= 0) pts[n++] = s->p[i].dead ? world_pug_pos(i) : s->p[i].p;
  t3_vec3 c = { 0, 0, 0 };
  for (int i = 0; i < n; i++) c = v3add(c, pts[i]);
  c = v3mul(c, 1.0f / n);
  c.y = fminf(c.y, 3);
  const t3_vec3 v = v3norm((t3_vec3){ -0.42f, -1, 0 });
  const float tan_t = 0.4877f; /* tan 26 degrees */
  float d = 14;
  for (int i = 0; i < n; i++) {
    t3_vec3 r = v3sub(pts[i], c);
    float along = v3dot(r, v), lat = v3len(v3sub(r, v3mul(v, along)));
    float need = (lat + 1.5f) / tan_t - along;
    if (need > d) d = need;
  }
  if (d > 70) d = 70;
  *eye_out = v3sub(c, v3mul(v, d));
  *at_out = c;
}

static void sun_follow(void) {
  /* the sun's shadow box follows the play; split screen: one box round every
   * human (the views share the one shadow map), grown to hold them all */
  if (!cw_node_valid(G.sun)) return;
  const t3_mat4 *m = cw_node3d_global_transform(G.sun);
  t3_vec3 f = v3norm((t3_vec3){ -m->e[8], -m->e[9], -m->e[10] });
  t3_vec3 c = v3add(v3mul(G.cam_at, 0.6f), v3mul(G.cam_eye, 0.4f));
  float ext = G.sun_extent;
  if (G.nviews >= 2) {
    t3_vec3 lo = { 1e9f, 0, 1e9f }, hi = { -1e9f, 0, -1e9f };
    for (int i = 0; i < MAXP; i++) {
      if (G.human_of[i] < 0) continue;
      t3_vec3 p = G.S.p[i].p;
      lo.x = fminf(lo.x, p.x), lo.z = fminf(lo.z, p.z), hi.x = fmaxf(hi.x, p.x), hi.z = fmaxf(hi.z, p.z);
    }
    c = v3mul(v3add(lo, hi), 0.5f);
    float need = 0.5f * fmaxf(hi.x - lo.x, hi.z - lo.z) + 0.6f * G.sun_extent; /* the extent reaches ahead of each pug */
    ext = cw_clampf(need, G.sun_extent, AL + 10);
    ext = 4 * (float)(int)(ext / 4 + 0.999f); /* in steps, so the box is not redrawn at a new size every tick */
  }
  if (ext != G.sun_extent_set) {
    G.sun_extent_set = ext;
    cw_node_set(G.sun, "shadow_extent", cw_val_float(ext));
  }
  c.y = 0;
  cw_node3d_set_position(G.sun, v3sub(c, v3mul(f, 70)));
}

static void camera_tick(void) {
  if (!cw_node_valid(G.camera)) return;
  bool was_chase = G.chase_on;
  G.chase_on = false;
  t3_vec3 at = { 0, 1, 0 }, eye = { 0, 12, -40 };
  float k_eye = 0.12f, k_at = 0.2f;
  const sim *s = &G.S;
  int me = focus_slot();
  if (G.state == GS_TITLE || G.state == GS_SHEET || G.state == GS_STADIUM || G.state == GS_LOCKER) {
    at = (t3_vec3){ 0, 1.3f, -AL + 10 };
    float a = G.ticks * 0.0025f;
    eye = (t3_vec3){ 2.4f * cw_sin(a), 1.7f, -AL + 9 - 5.2f - 0.4f * cw_cos(a) };
    if (G.state == GS_SHEET) { /* the pug being edited, up close */
      int c = G.cursor < MAXP ? G.cursor : 2;
      float x = (c - 2.5f) * 1.6f;
      at = (t3_vec3){ x, 0.5f, -AL + 9 };
      eye = (t3_vec3){ x * 0.7f, 1.3f, -AL + 9 - 3.4f };
    }
    k_eye = k_at = 0.05f;
  } else if (G.state == GS_FULLTIME) {
    at = (t3_vec3){ 0, 1.2f, 0 };
    float a = G.ticks * 0.003f;
    eye = (t3_vec3){ 30 * cw_sin(a), 14, 30 * cw_cos(a) };
    k_eye = k_at = 0.03f;
  } else if (G.state == GS_REPLAY) {
    /* the replay's cameras: low alongside the cat as the move builds, then
     * from inside the net as it arrives (a cut between them) */
    const sim *r = &RP.view;
    float side = s->goal_team == TEAM_BLUE ? 1.0f : -1.0f;
    bool net = RP.t > (float)(RP.n - REPLAY_AFTER - 70);
    static bool was_net;
    if (net != was_net) G.cam_snap = true;
    was_net = net;
    at = r->ball;
    if (net) {
      eye = (t3_vec3){ cw_clampf(r->ball.x * 0.3f, -GW + 1, GW - 1), 2.2f, side * (AL + GD - 0.8f) };
      k_eye = 0.05f, k_at = 0.25f;
    } else {
      /* from the pitch side of the cat (never squashed against a wall),
       * sweeping slowly round it */
      float dx = -r->ball.x, dz = side * (AL - 8) * 0.5f - r->ball.z, l = sqrtf(dx * dx + dz * dz);
      if (l < 0.01f) dx = 1, dz = 0, l = 1;
      float sw = 0.5f * cw_sin(RP.t * 0.012f), c = cw_cos(sw), sn = cw_sin(sw);
      float ux = (dx * c - dz * sn) / l, uz = (dx * sn + dz * c) / l;
      at = v3lerp(r->ball, (t3_vec3){ 0, 2, side * AL }, 0.2f); /* lead toward the goal it is going into */
      eye = (t3_vec3){ r->ball.x + ux * 10, cw_clampf(r->ball.y + 2.4f, 2, AH - 2), r->ball.z + uz * 10 };
      k_eye = 0.08f, k_at = 0.2f;
    }
  } else if (s->phase == PH_GOAL && s->phase_ticks < 170) {
    float side = s->goal_team == TEAM_BLUE ? 1.0f : -1.0f; /* the goal it went into */
    at = (t3_vec3){ 0, 2.2f, side * (AL + 1.5f) };
    float a = s->phase_ticks * 0.004f - 0.35f;
    eye = (t3_vec3){ 9 * cw_sin(a), 4.5f + s->phase_ticks * 0.01f, side * (AL - 12 + 2 * cw_cos(a)) };
    k_eye = 0.04f;
    k_at = 0.08f;
  } else if (me >= 0) {
    /* the chase cameras run per render frame (cam_frame): nothing to do on the tick */
    if (!was_chase) cw_node_set(G.camera, "interpolate", cw_val_bool(false)); /* cam_frame sets it every frame */
    G.chase_on = true;
    if (!was_chase || (s->phase == PH_KICKOFF && s->phase_ticks <= 1)) memset(G.cf_live, 0, sizeof G.cf_live);
    sun_follow();
    return;
  } else if (me >= 0) {
    fit_tick(&eye, &at);
    k_eye = 0.1f;
    k_at = 0.14f;
  } else {
    /* the broadcast camera: high in the side stand, panning with the cat */
    float bz = cw_clampf(s->ball.z, -AL + 6, AL - 6);
    at = (t3_vec3){ s->ball.x * 0.35f, 1.5f + s->ball.y * 0.3f, bz };
    eye = (t3_vec3){ AW - 1.5f, 13.5f, bz * 0.8f };
    k_eye = 0.06f;
    k_at = 0.1f;
  }
  if (DEV.tt && G.state == GS_TITLE) { /* dev aid: a turntable of pug 2 on parade (front34, side, face, head, profile, back) */
    const char *v = DEV.tt;
    t3_vec3 c = world_pug_pos(DEV.tt_pug);
    /* the pugs on parade face -z: "front" is from -z */
    if (!strcmp(v, "side")) eye = v3add(c, (t3_vec3){ 1.9f, 0.25f, 0 }), at = v3add(c, (t3_vec3){ 0, 0.05f, 0 });
    else if (!strcmp(v, "face")) eye = v3add(c, (t3_vec3){ 0.15f, 0.25f, -1.05f }), at = v3add(c, (t3_vec3){ 0, 0.2f, -0.35f });
    else if (!strcmp(v, "head")) eye = v3add(c, (t3_vec3){ 0.3f, 0.3f, -0.85f }), at = v3add(c, (t3_vec3){ 0, 0.24f, -0.33f });
    else if (!strcmp(v, "profile")) eye = v3add(c, (t3_vec3){ 0.85f, 0.26f, -0.36f }), at = v3add(c, (t3_vec3){ 0, 0.23f, -0.36f });
    else if (!strcmp(v, "back")) eye = v3add(c, (t3_vec3){ -0.9f, 0.8f, 1.5f }), at = v3add(c, (t3_vec3){ 0, 0.05f, 0 });
    else eye = v3add(c, (t3_vec3){ -1.1f, 0.55f, -1.5f }), at = v3add(c, (t3_vec3){ 0, 0.05f, 0 });
    G.cam_snap = true;
  }
  if (DEV.cam_pug) { /* dev aid: a close look at blue 1 from its side */
    t3_vec3 p = s->p[0].p, f = pug_fwd(&s->p[0]);
    at = p;
    eye = v3add(p, (t3_vec3){ -f.z * 4.5f - f.x * 1.5f, 1.4f, f.x * 4.5f - f.z * 1.5f });
    G.cam_snap = true;
  }
  if (DEV.cam_crowd) { /* dev aid: the side stand from the touchline */
    at = (t3_vec3){ AW + 12, AH - 1.5f, 1 };
    eye = (t3_vec3){ AW + 2, AH, -3 };
    G.cam_snap = true;
  }
  if (DEV.cam_cat) { /* dev aid: a close look at the cat */
    at = s->ball;
    eye = v3add(s->ball, (t3_vec3){ 2.6f, 1.2f, -4.4f });
    G.cam_snap = true;
  }
  /* the chase camera restarts behind the pug at every kickoff and after a celebration */
  if (!(me >= 0 && G.state == GS_MATCH && !(s->phase == PH_GOAL && s->phase_ticks < 170)) ||
      (s->phase == PH_KICKOFF && s->phase_ticks <= 1))
    G.ch_live = false;
  if (was_chase) { /* leaving the chase camera: blend on from where it was */
    cw_node_set(G.camera, "interpolate", cw_val_bool(true));
    if (G.nviews) {
      cw_pipeline_set_views(cw_pipeline_of(cw_scene_app_tree()), NULL, 0);
      G.nviews = 0;
      apply_quality();
    }
    G.cam_eye = cw_node3d_position(G.camera);
    cw_node3d_snapshot(G.camera);
  }
  bool snapped = G.cam_snap;
  if (G.cam_snap) {
    G.cam_at = at;
    G.cam_eye = eye;
    G.cam_snap = false;
  } else {
    G.cam_at = v3add(G.cam_at, v3mul(v3sub(at, G.cam_at), k_at));
    G.cam_eye = v3add(G.cam_eye, v3mul(v3sub(eye, G.cam_eye), k_eye));
  }
  t3_vec3 e = G.cam_eye;
  float sh = fx_shake();
  if (sh > 0.001f) {
    e.x += sh * 0.35f * cw_sin(G.ticks * 1.9f);
    e.y += sh * 0.35f * cw_sin(G.ticks * 2.7f + 1);
    e.z += sh * 0.35f * cw_sin(G.ticks * 2.3f + 2);
  }
  /* the camera node renders between this tick's view and the last (a cut snaps) */
  cw_node3d_snapshot(G.camera);
  cw_node3d_set_position(G.camera, e);
  cw_node3d_look_at(G.camera, G.cam_at, (t3_vec3){ 0, 1, 0 });
  if (snapped) cw_node3d_snapshot(G.camera);
  sun_follow();
}

/* ── the chase camera, per render frame ─────────────────────────────
 * Presentation only, so it runs on the frame's own clock: the pug and the
 * cat are taken between the last two ticks (alpha), the eye sits RIGIDLY
 * behind the pug at the smoothed heading (no position spring: the pug holds
 * its place on screen), and only three things are smoothed, all with
 * frame-rate independent exponentials: the heading, the camera's distance
 * (pulled in by the glass, the walls and the ceiling) and its height.
 * The heading's target is itself continuous: the ground heading and the
 * flight heading blend by smooth weights of speed and of a smoothed
 * on-the-ground value, and ball cam blends toward the cat with a smooth
 * dead zone near the pug. Nothing switches, so nothing catches up. */
static float smooth_k(float k, float dt) { return 1 - cw_exp(-k * dt); }
static t3_vec3 lerp3(t3_vec3 a, t3_vec3 b, float t) { return v3add(a, v3mul(v3sub(b, a), t)); }

static void track_tick(void) {
  for (int i = 0; i < MAXP; i++) {
    t3_vec3 p = G.S.p[i].dead ? world_pug_pos(i) : G.S.p[i].p;
    t3_vec3 d = v3sub(p, G.ip_cur[i]);
    G.ip_prev[i] = v3dot(d, d) > 16 ? p : G.ip_cur[i]; /* a respawn or kickoff: no blend */
    G.ip_cur[i] = p;
  }
  t3_vec3 d = v3sub(G.S.ball, G.ib_cur);
  G.ib_prev = v3dot(d, d) > 16 ? G.S.ball : G.ib_cur;
  G.ib_cur = G.S.ball;
}

static void chase_view(int h, int me, cw_node cam, double alpha, double now, float dt) {
  const sim *s = &G.S;
  const pug *u = &s->p[me];
  float a = (float)cw_clampf((float)alpha, 0, 1);
  t3_vec3 P = lerp3(G.ip_prev[me], G.ip_cur[me], a), B = lerp3(G.ib_prev, G.ib_cur, a);
  if (!G.cf_live[h]) dt = 0;
  /* the target heading, continuous: ground forward and flight velocity, blended */
  t3_vec3 f = pug_fwd(u);
  float hf = sqrtf(f.x * f.x + f.z * f.z), hv = sqrtf(u->v.x * u->v.x + u->v.z * u->v.z);
  float gnd = u->grounded && !u->dead && u->p.y < 2.6f ? 1.0f : 0.0f;
  G.cf_ground[h] += (gnd - G.cf_ground[h]) * smooth_k(6, dt);
  float wf = G.cf_ground[h] * cw_smoothstep(0.15f, 0.6f, hf);
  float wv = cw_smoothstep(1.0f, 6.0f, hv) * (0.35f + 0.65f * G.cf_ground[h]);
  t3_vec3 dir = { 0, 0, 0 };
  if (hf > 1e-4f) dir = v3add(dir, v3mul((t3_vec3){ f.x / hf, 0, f.z / hf }, wf));
  if (hv > 1e-4f) dir = v3add(dir, v3mul((t3_vec3){ u->v.x / hv, 0, u->v.z / hv }, wv * (1 - 0.6f * wf)));
  float wcar = v3len(dir);
  float sy, cy;
  cw_sincos(G.cf_yaw[h], &sy, &cy);
  t3_vec3 cur = { sy, 0, cy };
  /* with no clear heading (stopped, tumbling) the target is where it looks now */
  t3_vec3 car = wcar > 1e-4f ? lerp3(cur, v3mul(dir, 1 / wcar), cw_clampf(wcar, 0, 1)) : cur;
  t3_vec3 want = car;
  if (G.ballcam_h[h]) {
    t3_vec3 tb = { B.x - P.x, 0, B.z - P.z };
    float db = v3len(tb);
    float wb = cw_smoothstep(2.5f, 7.0f, db); /* close to the cat: let the car heading lead */
    if (db > 1e-4f) want = lerp3(car, v3mul(tb, 1 / db), wb);
  }
  float wl = v3len(want);
  float target = wl > 1e-4f ? cw_atan2(want.x, want.z) : G.cf_yaw[h];
  if (!G.cf_live[h]) {
    G.cf_yaw[h] = wl > 1e-4f ? target : (u->team == TEAM_BLUE ? 0 : 3.1415927f);
    G.cf_dist[h] = 5.2f;
    G.cf_high[h] = 1.8f;
    G.cf_lift[h] = 0;
    G.cf_ground[h] = gnd;
    G.cf_live[h] = true;
  }
  G.cf_yaw[h] = wrap_pi(G.cf_yaw[h] + wrap_pi(target - G.cf_yaw[h]) * smooth_k(G.ballcam_h[h] ? 4.0f : 5.0f, dt));
  cw_sincos(G.cf_yaw[h], &sy, &cy);
  t3_vec3 back = { -sy, 0, -cy };
  /* how far back and how high it may sit: the glass, walls and ceiling pull it in */
  float high_want = cw_clampf(AH - 1.4f - P.y, 0.6f, 1.8f);
  float dist_want = 1.6f;
  for (int k = 0; k <= 12; k++) {
    float dd = 5.2f - k * 0.3f;
    t3_vec3 e = v3add(P, v3add(v3mul(back, dd), (t3_vec3){ 0, high_want, 0 }));
    if (arena_clear(P, e, 0.9f)) {
      dist_want = dd;
      break;
    }
  }
  G.cf_dist[h] += (dist_want - G.cf_dist[h]) * smooth_k(dist_want < G.cf_dist[h] ? 12.0f : 3.0f, dt);
  G.cf_high[h] += (high_want - G.cf_high[h]) * smooth_k(6, dt);
  float lift_want = G.ballcam_h[h] ? cw_clampf((B.y - P.y) * 0.25f, 0, 2.2f) : 0;
  G.cf_lift[h] += (lift_want - G.cf_lift[h]) * smooth_k(3, dt);
  t3_vec3 eye = v3add(P, v3add(v3mul(back, G.cf_dist[h]), (t3_vec3){ 0, G.cf_high[h], 0 }));
  /* a fixed look point ahead of and above the pug (lifted a little toward a high cat) */
  t3_vec3 look = v3add(P, v3add(v3mul(back, -2.5f), (t3_vec3){ 0, 0.9f + G.cf_lift[h], 0 }));
  float sh = fx_shake();
  if (sh > 0.001f) {
    eye.x += sh * 0.3f * cw_sin((float)now * 31);  /* a rumble, not a rattle */
    eye.y += sh * 0.3f * cw_sin((float)now * 23 + 1);
  }
  cw_node3d_set_position(cam, eye);
  cw_node3d_look_at(cam, look, (t3_vec3){ 0, 1, 0 });
  if (cam.id == G.camera.id) {
    G.cam_eye = eye;
    G.cam_at = look;
  }
  if (G.camtrace && cam.id == G.camera.id) {
    /* the pug's place on screen (tangent units) and the eye, for jerk checks */
    t3_vec3 fw = v3norm(v3sub(look, eye)), rt = v3norm(v3cross(fw, (t3_vec3){ 0, 1, 0 })), up = v3cross(rt, fw);
    t3_vec3 v = v3sub(v3add(P, (t3_vec3){ 0, 0.3f, 0 }), eye);
    float z = v3dot(v, fw);
    fprintf(stderr, "pcam %.4f %.4f %.4f %.4f %.5f %.5f\n", now, eye.x, eye.y, eye.z, z > 0.01f ? v3dot(v, rt) / z : 0, z > 0.01f ? v3dot(v, up) / z : 0);
  }
}
/* every human's chase camera this frame; with two or more, split screen */
static cw_node extra_camera(int k) {
  if (cw_node_valid(G.cams[k])) return G.cams[k];
  cw_node c = cw_node_new("Camera3D", "SplitCam");
  cw_node_add_child(G.scene, c);
  cw_node_set(c, "fov", cw_val_float(62));
  cw_node_set(c, "near", cw_val_float(0.2f));
  cw_node_set(c, "far", cw_val_float(600));
  G.cams[k] = c;
  return c;
}
/* the views' rectangles (fractions of the output, from the top left): 2
 * players top and bottom; 3 the upper left, lower left and lower right
 * quadrants (the upper right holds the shared scoreboard and the mini map);
 * 4 the four quadrants */
static const float VIEW_RECT[5][4][4] = {
  { { 0 } }, { { 0, 0, 1, 1 } },
  { { 0, 0, 1, 0.5f }, { 0, 0.5f, 1, 0.5f } },
  { { 0, 0, 0.5f, 0.5f }, { 0, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f } },
  { { 0, 0, 0.5f, 0.5f }, { 0.5f, 0, 0.5f, 0.5f }, { 0, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f } },
};
static void cam_frame(cw_tree *t, double alpha, void *user) {
  (void)user;
  if (!G.chase_on || !cw_node_valid(G.camera)) return;
  double now = cw_time();
  float dt = (float)cw_clampf((float)(now - G.cf_last), 0, 0.1f);
  G.cf_last = now;
  /* the humans in pad order: P1's view first */
  int k = 0;
  for (int p = 0; p < 4; p++)
    for (int i = 0; i < MAXP; i++)
      if (G.human_of[i] == p) G.view_slot[k++] = i;
  cw_view views[4];
  for (int v = 0; v < k; v++) {
    int i = G.view_slot[v];
    cw_node cam = v == 0 ? G.camera : extra_camera(v);
    chase_view(G.human_of[i], i, cam, alpha, now, dt);
    const float *r = VIEW_RECT[k][v];
    views[v] = (cw_view){ cam, r[0], r[1], r[2], r[3] };
  }
  int nv = k >= 2 ? k : 0;
  cw_pipeline_set_views(cw_pipeline_of(t), views, nv);
  if (nv != G.nviews) {
    G.nviews = nv;
    apply_quality();
  }
}
/* before each split-screen view draws: the pugs and the cat far from its
 * camera in their light meshes, the grass and the crowd thinned with distance */
static void view_hook(int view, void *user) {
  (void)user;
  const cw_view *vs = NULL;
  int n = cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &vs);
  if (view >= n || !cw_node_valid(vs[view].camera)) return;
  const t3_mat4 *m = cw_node3d_global_transform(vs[view].camera);
  t3_vec3 eye = { m->e[12], m->e[13], m->e[14] };
  world_view_detail(eye, false);
  scenery_view_detail(eye);
}
static void cmd_camtrace(int argc, const char **argv, cw_reply *r) {
  G.camtrace = argc > 1 ? atoi(argv[1]) : !G.camtrace;
  cw_reply_printf(r, "{\"camtrace\":%d}", G.camtrace);
}

/* ── layout shared by the update (mouse hit tests) and the HUD ─────── */

static cw_rect title_button(int i, float W, float H) {
  float s = H / 540.0f, w = 230 * s, h = 58 * s;
  return cw_rect_make(W * 0.5f + (i ? 12 * s : -w - 12 * s), H * 0.78f, w, h);
}
/* the team sheet: two columns of three slots */
static cw_rect slot_rect(int i, float W, float H) {
  float s = H / 540.0f, w = 290 * s, h = 92 * s, gap = 10 * s;
  int team = i & 1, row = i / 2;
  float x = team == TEAM_BLUE ? 18 * s : W - w - 18 * s; /* the middle shows the pug being dressed */
  return cw_rect_make(x, H * 0.17f + row * (h + gap), w, h);
}
static cw_rect go_rect(float W, float H) {
  float s = H / 540.0f;
  return cw_rect_make(W * 0.5f - 170 * s, H * 0.8f, 340 * s, 52 * s);
}
static cw_rect tile_rect(int i, float W, float H) {
  float s = H / 540.0f, tw = 168 * s, th = 120 * s, gx = 12 * s;
  float x0 = (W - (STADIUMS * tw + (STADIUMS - 1) * gx)) * 0.5f;
  return cw_rect_make(x0 + i * (tw + gx), H * 0.3f, tw, th);
}
static bool in_rect(cw_vec2 m, cw_rect r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }

/* ── the fixed tick ─────────────────────────────────────────────────── */

static void sim_sounds(void) {
  const sim *s = &G.S;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    switch (e->kind) {
    case EV_HIT: {
      float k = e->k;
      sfx_at(k > 0.45f ? SFX_HIT_BIG : SFX_HIT, e->at, 0.5f + 0.5f * k, 0.9f + 0.2f * k);
      /* the cat has opinions: a meow on a tap, a yowl on a whack, a hiss at a boosted pug */
      const pug *u = &s->p[e->a];
      if (u->boosting && k > 0.3f) sfx_at(SFX_HISS, e->at, 0.9f, 1);
      else if (k > 0.5f) sfx_at(SFX_YOWL, e->at, 0.6f + 0.4f * k, 1.15f - 0.3f * k);
      else if (k > 0.1f) sfx_at(SFX_MEOW, e->at, 0.4f + 0.6f * k, 1.2f - 0.3f * k);
      break;
    }
    case EV_BOUNCE: sfx_at(SFX_BOING, e->at, 0.3f + 0.5f * e->k, 1.1f - 0.3f * e->k); break;
    case EV_JUMP: case EV_DOUBLE: sfx_at(SFX_JUMP, e->at, 0.5f, e->kind == EV_DOUBLE ? 1.2f : 1); break;
    case EV_DODGE: sfx_at(SFX_DODGE, e->at, 0.6f, 1); break;
    case EV_LAND: if (e->k > 0.2f) sfx_at(SFX_LAND, e->at, 0.3f + 0.5f * e->k, 1); break;
    case EV_PAD: sfx_at(e->b ? SFX_PAD_BIG : SFX_PAD, e->at, 0.6f, 1); break;
    case EV_BUMP: sfx_at(SFX_BUMP, e->at, 0.4f + 0.6f * e->k, 1); break;
    case EV_DEMO:
      sfx_at(SFX_DEMO, e->at, 1, 1);
      sfx_at(SFX_BARK, e->at, 0.8f, 1.4f);
      snprintf(G.feed, sizeof G.feed, "%s flattened %s!", PUG_NAMES[e->a], PUG_NAMES[e->b]);
      G.feed_ticks = 180;
      break;
    case EV_GOAL:
      sfx(SFX_HORN, 1);
      sfx(SFX_CHEER, 1);
      sfx_at(SFX_YOWL, e->at, 1, 0.8f);
      break;
    case EV_KICKOFF: sfx_v(SFX_PURR, 0.35f, 1); break;
    case EV_GO: sfx(SFX_GO, 1); sfx_v(SFX_BARK, 0.7f, 1); break;
    case EV_OVERTIME: case EV_FINAL: sfx(SFX_WHISTLE, 1); break;
    default: break;
    }
  }
  if (s->phase == PH_KICKOFF && (s->phase_ticks == 1 || s->phase_ticks == 60 || s->phase_ticks == 120)) sfx(SFX_BEEP, 0.8f);
}

static void finish_match(void) {
  G.state = GS_FULLTIME;
  G.state_ticks = 0;
  /* the MVP: most points on the winning side */
  int best = -1;
  for (int i = 0; i < MAXP; i++)
    if (G.S.p[i].on && (G.S.winner < 0 || G.S.p[i].team == G.S.winner) && (best < 0 || G.S.p[i].points > G.S.p[best].points)) best = i;
  G.mvp = best;
  if (G.mode == MODE_CUP && G.S.winner == TEAM_BLUE && G.stadium >= 1) {
    int i = G.stadium - 1;
    bool first = !SAVE.won[i];
    SAVE.won[i] = 1;
    int margin = G.S.score[0] - G.S.score[1];
    if (margin > SAVE.best_margin[i]) SAVE.best_margin[i] = margin;
    if (first && i + 1 < STADIUMS) SAVE.shown[i + 1] = 0;
    save_write();
  }
  crowd_bed(false);
  sfx(SFX_CHEER, 1);
}

static void sheet_rebuild_pugs(void) {
  int keep = G.cursor, st = G.state_ticks, col = G.col;
  enter_title();
  G.state = GS_SHEET;
  G.cursor = keep;
  G.col = col;
  G.state_ticks = st;
}

/* change one setting of a slot on the team sheet: col 0 who plays, 1 coat,
 * 2 hat (unlocked ones), 3 boost trail (unlocked ones) */
static void sheet_change(int i, int col, int dx) {
  bool rebuild = false;
  if (col == 0) {
    int was = G.slot[i];
    G.slot[i] = SLOT_OFF; /* its pad is free while it changes */
    int v = was, pad = free_pad();
    for (int k = 0; k < SLOT_KINDS; k++) {
      v = (v + dx + SLOT_KINDS) % SLOT_KINDS;
      if (v == SLOT_OFF && i == 0) continue; /* the Biscuits always have a pug */
      if (v == SLOT_HUMAN && pad < 0) continue; /* four players already */
      break;
    }
    G.slot[i] = v;
    if (v == SLOT_HUMAN) G.pad_of[i] = was == SLOT_HUMAN ? G.pad_of[i] : pad;
    rebuild = true;
  } else if (col == 1) {
    G.look[i].coat = (G.look[i].coat + dx + PUG_KINDS) % PUG_KINDS;
    rebuild = true;
  } else if (col == 2) {
    int h = G.look[i].hat;
    for (int k = 0; k < HAT_KINDS; k++) {
      h = (h + dx + HAT_KINDS) % HAT_KINDS;
      if (hat_open(h)) break;
    }
    G.look[i].hat = h;
    rebuild = true;
  } else {
    int t = G.look[i].trail;
    for (int k = 0; k < TRAIL_KINDS; k++) {
      t = (t + dx + TRAIL_KINDS) % TRAIL_KINDS;
      if (trail_open(t)) break;
    }
    G.look[i].trail = t;
  }
  sfx(SFX_MOVE, 0.6f);
  if (rebuild) sheet_rebuild_pugs();
}
/* a setting's cell on a slot's card (shared by the HUD and the hit tests) */
static cw_rect cell_rect(cw_rect card, int c, float s) {
  return cw_rect_make(card.x + 73 * s + (c % 2) * 108 * s, card.y + 34 * s + (c / 2) * 26 * s, 104 * s, 24 * s);
}


/* pad p takes a pug on the side with fewer players: a CPU's first, else an
 * empty slot (then it plays a side one bigger) */
static bool sheet_join(int p) {
  int hum[2] = { 0, 0 };
  for (int i = 0; i < MAXP; i++) hum[i & 1] += G.slot[i] == SLOT_HUMAN;
  int first = hum[TEAM_RED] < hum[TEAM_BLUE] ? TEAM_RED : TEAM_BLUE;
  for (int pass = 0; pass < 4; pass++) {
    int team = pass & 1 ? 1 - first : first;
    bool want_cpu = pass < 2;
    for (int i = team; i < MAXP; i += 2)
      if (want_cpu ? G.slot[i] >= SLOT_ROOKIE : G.slot[i] == SLOT_OFF) {
        G.slot[i] = SLOT_HUMAN;
        G.pad_of[i] = p;
        sheet_rebuild_pugs();
        return true;
      }
  }
  return false;
}
/* a player's pug changes sides: it trades places (and looks) with the other
 * side's first CPU, else its first empty slot */
static bool sheet_switch(int me) {
  int other = 1 - (me & 1), to = -1;
  for (int i = other; i < MAXP && to < 0; i += 2)
    if (G.slot[i] >= SLOT_ROOKIE) to = i;
  for (int i = other; i < MAXP && to < 0; i += 2)
    if (G.slot[i] == SLOT_OFF) to = i;
  if (to < 0) return false;
  int was = G.slot[to];
  pug_look lk = G.look[to];
  G.slot[to] = SLOT_HUMAN;
  G.pad_of[to] = G.pad_of[me];
  G.look[to] = G.look[me];
  G.look[me] = lk;
  G.slot[me] = was;
  /* a side left with nobody keeps a CPU */
  int left = 0;
  for (int i = me & 1; i < MAXP; i += 2) left += G.slot[i] != SLOT_OFF;
  if (!left) G.slot[me] = SLOT_PRO;
  sheet_rebuild_pugs();
  return true;
}

static void fixed_body(void);
static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  perf_tick_begin();
  fixed_body();
  perf_tick_end();
}
static void fixed_body(void) {
  G.ticks++;
  G.state_ticks++;
  read_devices();
  for (int i = 0; i < SFX_COUNT; i++)
    if (A.cooldown[i] > 0) A.cooldown[i]--;
  if (G.feed_ticks > 0) G.feed_ticks--;
  cw_vec2 cs = cw_r2d_canvas_size(), m = mouse_canvas();
  bool confirm = m_confirm(), back = m_back();
  int dx = m_dx(), dy = m_dy();
  switch (G.state) {
  case GS_TITLE: {
    if (dy || dx) {
      G.menu = (G.menu + dy + dx + 2) % 2;
      sfx(SFX_MOVE, 0.5f);
    }
    int hover = -1;
    for (int i = 0; i < 2; i++)
      if (in_rect(m, title_button(i, cs.x, cs.y))) hover = i;
    if ((last_device == DEV_MOUSE || last_device == DEV_TOUCH) && hover >= 0) G.menu = hover;
    if (G.state_ticks > 20 && (confirm || any_pressed(G.a_start) || (mouse_clicked && hover >= 0))) {
      sfx(SFX_SELECT, 1);
      sfx_v(SFX_BARK, 0.6f, 1.1f);
      G.mode = G.menu == 0 ? MODE_EXHIBITION : MODE_CUP;
      G.cursor = 0;
      G.col = 0;
      if (G.mode == MODE_EXHIBITION) G.state = GS_SHEET;
      else {
        for (int i = 0; i < STADIUMS; i++)
          if (stadium_open(i) && !SAVE.won[i]) {
            G.cursor = i;
            break;
          }
        enter_stadium_select();
      }
      G.state_ticks = 0;
    }
    break;
  }
  case GS_SHEET: {
    /* rows: the six slots (cursor 0..5 walks blue / red, row by row), then
     * the clock and KICK OFF. Left / right on a slot changes the column
     * value under the column cursor: who plays, coat, hat, trail. Player 1
     * (pad 1, the keyboard, the mouse) works the cursor; pads 2-4 join. */
    int rows = MAXP + 1;
    dx = (cw_action_just_pressed(0, G.m_right) ? 1 : 0) - (cw_action_just_pressed(0, G.m_left) ? 1 : 0);
    dy = (cw_action_just_pressed(0, G.m_down) ? 1 : 0) - (cw_action_just_pressed(0, G.m_up) ? 1 : 0);
    confirm = cw_action_just_pressed(0, G.m_ok);
    back = cw_action_just_pressed(0, G.a_esc);
    if (dy) { /* the slots in reading order (blue 1, red 1, blue 2, ...), then KICK OFF */
      G.cursor = (G.cursor + dy + rows) % rows;
      sfx(SFX_MOVE, 0.5f);
    }
    if (cw_action_just_pressed(0, G.m_alt) && G.cursor < MAXP) {
      G.col = (G.col + 1) % 4;
      sfx(SFX_MOVE, 0.6f);
    }
    if (dx && G.cursor < MAXP) sheet_change(G.cursor, G.col, dx);
    if (dx && G.cursor == MAXP) {
      G.secs_i = CW_CLAMP(G.secs_i + dx, 0, 2);
      sfx(SFX_MOVE, 0.6f);
    }
    /* every pad: (A) joins (pads 2-4), a bumper (or left / right on pads
     * 2-4) takes its pug to the other side, (B) leaves (pads 2-4) */
    for (int p = 0; p < 4; p++) {
      int me = slot_of_pad(p);
      bool side = cw_action_just_pressed(p, G.m_team) ||
                  (p > 0 && (cw_action_just_pressed(p, G.m_left) || cw_action_just_pressed(p, G.m_right)));
      if (p > 0 && me < 0 && cw_action_just_pressed(p, G.m_ok)) {
        if (sheet_join(p)) sfx(SFX_SELECT, 0.8f);
        else sfx(SFX_LOCKED, 0.8f);
      } else if (me >= 0 && side) {
        if (sheet_switch(me)) sfx(SFX_SELECT, 0.7f);
        else sfx(SFX_LOCKED, 0.8f);
      } else if (p > 0 && me >= 0 && cw_action_just_pressed(p, G.m_leave)) {
        G.slot[me] = SLOT_PRO; /* a CPU takes the pug back */
        sfx(SFX_MOVE, 0.7f);
        sheet_rebuild_pugs();
      }
    }
    int hover = -1;
    for (int i = 0; i < MAXP; i++)
      if (in_rect(m, slot_rect(i, cs.x, cs.y))) hover = i;
    if (in_rect(m, go_rect(cs.x, cs.y))) hover = MAXP;
    if ((last_device == DEV_MOUSE || last_device == DEV_TOUCH) && hover >= 0 && mouse_moved) G.cursor = hover;
    /* a click or tap picks a card; on the picked card it steps the setting it lands on */
    if (mouse_clicked && hover >= 0 && hover < MAXP) {
      if (hover != G.cursor) {
        G.cursor = hover;
        sfx(SFX_MOVE, 0.5f);
      } else {
        cw_rect card = slot_rect(hover, cs.x, cs.y);
        for (int c = 0; c < 4; c++)
          if (in_rect(m, cell_rect(card, c, cs.y / 540.0f))) {
            G.col = c;
            sheet_change(hover, c, 1);
            break;
          }
      }
    }
    int blue = 0, red = 0;
    for (int i = 0; i < MAXP; i++)
      if (G.slot[i] != SLOT_OFF) {
        if (i & 1) red++;
        else blue++;
      }
    bool go = G.state_ticks > 10 &&
              (any_pressed(G.a_start) || (confirm && G.cursor == MAXP && G.state_ticks > 10) || (mouse_clicked && hover == MAXP));
    if (go && blue >= 1 && red >= 1) {
      for (int i = 0; i < MAXP; i++) {
        SAVE.slot[i] = (uint8_t)G.slot[i];
        SAVE.coat[i] = (uint8_t)G.look[i].coat;
        SAVE.hat[i] = (uint8_t)G.look[i].hat;
        SAVE.trail[i] = (uint8_t)G.look[i].trail;
      }
      SAVE.secs_i = (uint8_t)G.secs_i;
      save_write();
      memcpy(sheet_pad, G.pad_of, sizeof sheet_pad);
      sfx(SFX_SELECT, 1);
      G.cursor = 0;
      enter_stadium_select();
    } else if (go) sfx(SFX_LOCKED, 1);
    if (back && G.state_ticks > 5) {
      G.state = GS_TITLE;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_STADIUM: {
    if (dx) {
      int c = G.cursor + dx;
      if (c >= 0 && c < STADIUMS) {
        G.cursor = c;
        sfx(SFX_MOVE, 0.5f);
      }
    }
    int hover = -1;
    for (int i = 0; i < STADIUMS; i++)
      if (in_rect(m, tile_rect(i, cs.x, cs.y))) hover = i;
    if ((last_device == DEV_MOUSE || last_device == DEV_TOUCH) && hover >= 0 && mouse_moved) G.cursor = hover;
    if (G.bump_ticks > 0) G.bump_ticks--;
    if (G.unlock_tile >= 0 && ++G.unlock_ticks == 24) sfx(SFX_UNLOCK, 1);
    bool pick = G.state_ticks > 10 && (confirm || any_pressed(G.a_start) || (mouse_clicked && hover >= 0));
    if (pick && mouse_clicked && hover >= 0) G.cursor = hover;
    if (pick && !stadium_open(G.cursor)) {
      G.bump_tile = G.cursor;
      G.bump_ticks = 18;
      sfx(SFX_LOCKED, 1);
    } else if (pick) {
      sfx(SFX_SELECT, 1);
      if (G.mode == MODE_CUP) begin_cup(G.cursor + 1);
      else start_match(G.cursor + 1);
    } else if (back && G.state_ticks > 5) {
      G.state = G.mode == MODE_EXHIBITION ? GS_SHEET : GS_TITLE;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_MATCH: {
    pin in[MAXP] = { 0 };
    for (int i = 0; i < MAXP; i++) {
      if (G.slot[i] == SLOT_HUMAN) in[i] = human_pin(i);
      else if (G.slot[i] >= SLOT_ROOKIE) in[i] = ai_think(&G.S, i, G.slot[i] - SLOT_ROOKIE, &G.ai[i]);
#ifdef PUGBALL_DEMO_TEST
      if (i == 1) in[i] = (pin){ 0 }; /* the parked pug waits to be flattened */
#endif
    }
    for (int i = 0; i < MAXP; i++) /* each human toggles their own ball cam */
      if (G.human_of[i] >= 0 && cw_action_just_pressed(G.human_of[i], G.a_ballcam)) G.ballcam_h[G.human_of[i]] ^= 1;
    G.ballcam = 0;
    for (int p = 0; p < 4; p++) G.ballcam |= G.ballcam_h[p] << p;
    sim_step(&G.S, in);
    replay_record(&G.S);
    static FILE *rec; /* dev aid: PUG_RECORD=file logs every tick's input (tools/make-gate-script.mjs) */
    if (!rec && DEV.record) rec = fopen(DEV.record, "w");
    if (rec) fprintf(rec, "%d %d %d %d %d\n", G.S.tick, in[0].thr, in[0].steer, in[0].pitch, in[0].btn), fflush(rec);
    fx_events(&G.S);
    sim_sounds();
    if (G.S.phase == PH_OVER && G.S.phase_ticks > 150) finish_match();
    else if (G.S.phase == PH_GOAL && G.S.phase_ticks == REPLAY_AT && RP.n > 60 && !DEV.no_replay) replay_start();
    else if (any_pressed(G.a_start)) {
      G.state = GS_PAUSE;
      G.menu = 0;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_REPLAY: {
    RP.t += REPLAY_SPEED;
    bool skip = G.state_ticks > 20 && (confirm || back || any_pressed(G.a_start) || any_pressed(G.a_jump) || any_pressed(G.a_boost) ||
                                       any_pressed(G.a_ballcam) || mouse_clicked);
    if (skip || RP.t >= (float)(RP.n - 1)) replay_end();
    else replay_view();
    break;
  }
  case GS_PAUSE:
    if (cw_quality_menu_is_open()) break; /* the graphics page has the presses */
    if (dy || dx) G.menu = (G.menu + dy + dx + 2) % 2, sfx(SFX_MOVE, 0.5f);
    if (G.state_ticks > 5 && (any_pressed(G.a_start) || back || (confirm && G.menu == 0))) G.state = GS_MATCH;
    else if (G.state_ticks > 5 && confirm && G.menu == 1) enter_title();
    break;
  case GS_FULLTIME:
    if (G.state_ticks > 150 && (confirm || any_pressed(G.a_start) || mouse_clicked)) {
      bool cup = G.mode == MODE_CUP, won = cup && G.S.winner == TEAM_BLUE;
      int st = G.stadium;
      for (int i = 0; i < MAXP; i++) G.slot[i] = SAVE.slot[i];
      memcpy(G.pad_of, sheet_pad, sizeof G.pad_of);
      enter_title();
      if (cup) {
        G.cursor = won && st < STADIUMS ? st : st - 1;
        enter_stadium_select();
      } else {
        G.state = GS_STADIUM;
        G.state_ticks = 0;
        G.cursor = st - 1;
      }
    }
    break;
  default: break;
  }
  if (pending_stadium > 0) {
    int a = pending_stadium;
    pending_stadium = -1;
    G.mode = MODE_EXHIBITION;
    start_match(a);
  }
  if (G.state == GS_REPLAY) {
    net_time_scale(REPLAY_SPEED); /* the nets in slow motion too */
    world_tick(&RP.view);
    net_time_scale(1);
    fx_tick(&RP.view);
    fx_events(&RP.view);
    scenery_tick(&RP.view, G.ticks, 0);
  } else if (G.state == GS_MATCH || G.state == GS_FULLTIME) {
    if (G.state == GS_FULLTIME) G.S.ev_n = 0;
    world_tick(&G.S);
    track_tick();
    fx_tick(&G.S);
    scenery_tick(&G.S, G.ticks, G.S.phase == PH_GOAL ? G.S.goal_team + 1 : 0); /* the scorers' fans leap */
  } else if (G.state != GS_PAUSE) {
    world_showcase(G.ticks, G.look, G.state == GS_SHEET && G.cursor < MAXP ? G.cursor : -1);
    world_tick(&G.S); /* the cat and pads idle */
    fx_tick(&G.S);
    scenery_tick(&G.S, G.ticks, 0);
  }
  camera_tick();
  save_fields();
  const sim *s = &G.S;
  G.phase = s->phase;
  G.clock = s->clock;
  G.score[0] = s->score[0], G.score[1] = s->score[1];
  G.winner = s->winner;
  G.hash = s->hash;
  G.ball_pos[0] = (int32_t)(s->ball.x * 100), G.ball_pos[1] = (int32_t)(s->ball.y * 100), G.ball_pos[2] = (int32_t)(s->ball.z * 100);
  for (int i = 0; i < MAXP; i++) {
    G.pos[i * 3] = (int32_t)(s->p[i].p.x * 100), G.pos[i * 3 + 1] = (int32_t)(s->p[i].p.y * 100), G.pos[i * 3 + 2] = (int32_t)(s->p[i].p.z * 100);
    G.slots[i] = G.slot[i];
    G.goals[i] = s->p[i].goals;
    G.points[i] = s->p[i].points;
    G.boost[i] = (int32_t)s->p[i].boost;
  }
  G.hits = s->hits, G.demos = s->demos, G.jumps = s->jumps, G.dodges = s->dodges, G.pads = s->pads_taken, G.bounces = s->bounces;
  int32_t ints[] = { G.state, G.mode, G.stadium, G.phase, G.score[0], G.score[1], G.clock };
  for (size_t k = 0; k < sizeof ints / sizeof ints[0]; k++) G.state_hash = (G.state_hash ^ (uint32_t)ints[k]) * 16777619u;
  G.state_hash = (G.state_hash ^ G.hash) * 16777619u;
}

static void gfx_input(void);
static void update(void *u, double dt) {
  (void)u;
  (void)dt;
  render_t0 = wall_ms();
  gfx_input();
  cw_lighting_stats ls;
  cw_lighting_get_stats(cw_scene_app_tree(), &ls);
  LIT.clustered = ls.clustered, LIT.shadowed = ls.shadowed, LIT.cascades = ls.cascades;
  if (!A.on) return;
  /* a listener per view (split screen: each spatial sound heard from the nearest pug's camera) */
  const cw_view *vs = NULL;
  int nv = G.nviews ? cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &vs) : 0;
  int nl = nv >= 2 ? nv : 1;
  for (int k = 0; k < nl; k++) {
    cw_node cam = nv >= 2 ? vs[k].camera : cw_render3d_camera(cw_scene_app_tree());
    if (!cw_node_valid(cam)) continue;
    const t3_mat4 *mm = cw_node3d_global_transform(cam);
    t3_vec3 p = { mm->e[12], mm->e[13], mm->e[14] }, f = { -mm->e[8], -mm->e[9], -mm->e[10] }, up = { mm->e[4], mm->e[5], mm->e[6] };
    cw_audio_set_listener_at(k, p, (t3_vec3){ 0, 0, 0 }, f, up);
  }
  cw_audio_set_listener_count(nl);
  cw_audio_update();
  if (A.wav) { /* offline: this frame's 1/60 s of the mix */
    static float buf[2 * 800];
    cw_audio_render(buf, 800);
    int16_t pcm[2 * 800];
    for (int i = 0; i < 1600; i++) pcm[i] = (int16_t)(cw_clampf(buf[i], -1, 1) * 32767);
    fwrite(pcm, 2, 1600, A.wav);
    A.wav_frames += 800;
  }
}

/* ── HUD (render2d over the 3D frame; render-only) ─────────────────── */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

/* the outline alpha: the 12 offset stamps these outlines started as
 * overlapped about twice over each spot, so they read denser than one */
static float line_alpha(float a) { return 1 - (1 - a) * (1 - a); }
/* text with a soft outline, dark round light text and light round dark: one
 * layout, one draw (the outline is the SDF's, cw_text_style) */
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_font f = size < 20 ? font_ui_bold : font_ui;
  bool light = c.r * 0.3f + c.g * 0.6f + c.b * 0.1f > 0.5f;
  cw_color line = light ? rgba(0.06f, 0.04f, 0.12f, line_alpha(0.85f * c.a)) : rgba(1, 0.98f, 0.95f, line_alpha(0.7f * c.a));
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap, .outline_color = line,
                       .outline_width = light ? size * 0.09f + 0.6f : size * 0.05f + 0.4f };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}
/* display text: Lilita One, a thick outline and a soft dark under-glow */
static void bold(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap,
                       .outline_width = size * 0.07f, .outline_color = rgba(0.08f, 0.05f, 0.16f, c.a),
                       .shadow_color = rgba(0.05f, 0.03f, 0.12f, line_alpha(0.5f * c.a)), .shadow_offset = cw_v2(0, size * 0.05f),
                       .shadow_spread = size * 0.11f };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}
static void digits(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap, .outline_width = size * 0.06f,
                       .outline_color = rgba(0.04f, 0.03f, 0.1f, line_alpha(0.9f * c.a)) };
  cw_r2d_text(font_score, s, cw_v2(x, y), &st);
}
static void round_rect(cw_rect r, float rad, cw_color c) { cw_r2d_round_rect(r, rad, 8, c); }
static void panel(cw_rect r, float alpha, cw_color border, cw_color fill) {
  float rad = fminf(r.h * 0.25f, 20);
  round_rect(cw_rect_make(r.x + 3, r.y + 6, r.w, r.h), rad, rgba(0.03f, 0.02f, 0.08f, 0.3f * alpha));
  border.a *= alpha;
  fill.a *= alpha;
  round_rect(cw_rect_make(r.x - 3, r.y - 3, r.w + 6, r.h + 6), rad + 3, border);
  round_rect(r, rad, fill);
}
static void glass(cw_rect r, float alpha) { panel(r, alpha, rgba(1, 1, 1, 0.55f), rgba(0.07f, 0.06f, 0.16f, 0.66f)); }
static void chip(const char *s, float cx, float y, float size, float alpha) {
  cw_text_style st = { .size = size };
  cw_text_metrics mm = cw_font_measure(size < 20 ? font_ui_bold : font_ui, s, &st);
  float padx = size * 0.8f, pady = size * 0.3f;
  round_rect(cw_rect_make(cx - mm.width * 0.5f - padx, y - pady, mm.width + 2 * padx, mm.height + 2 * pady), size * 0.7f,
             rgba(0.06f, 0.04f, 0.12f, 0.55f * alpha));
}
static void prompt_line(const char *s, float W, float y, float size) {
  chip(s, W * 0.5f, y, size, 1);
  text(s, 0, y, size, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
}
static void star(float x, float y, float r, cw_color fill, float rot) {
  cw_vec2 p[10];
  for (int i = 0; i < 10; i++) {
    float a = rot - 1.5707963f + i * 0.6283185f, rr = i & 1 ? r * 0.45f : r;
    p[i] = cw_v2(x + rr * cw_cos(a), y + rr * cw_sin(a));
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
  cw_rect body = cw_rect_make(x - sz * 0.42f, y - sz * 0.12f, sz * 0.84f, sz * 0.66f);
  round_rect(cw_rect_make(body.x - 1.5f, body.y - 1.5f, body.w + 3, body.h + 3), sz * 0.14f, dark);
  round_rect(body, sz * 0.12f, rgba(1, 0.75f, 0.25f, alpha));
  cw_vec2 c = cw_v2(body.x + body.w * 0.5f, body.y + body.h * 0.45f);
  cw_r2d_circle(c, sz * 0.08f, 12, dark);
}

/* a pug's face, flat (portraits) */
static const float COAT2D[PUG_KINDS][3] = { { 0.86f, 0.71f, 0.5f }, { 0.16f, 0.15f, 0.15f }, { 0.92f, 0.62f, 0.36f }, { 0.74f, 0.74f, 0.76f } };
static void pug_face(float x, float y, float r, int coat, int team, bool sad, float a) {
  cw_color furc = rgba(COAT2D[coat][0], COAT2D[coat][1], COAT2D[coat][2], a), mask = rgba(0.1f, 0.08f, 0.08f, a);
  cw_color tc = rgba(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2], a);
  cw_r2d_circle(cw_v2(x, y), r * 1.18f, 28, tc);
  cw_r2d_circle(cw_v2(x, y), r * 1.06f, 28, rgba(0.08f, 0.06f, 0.14f, a));
  for (int s = -1; s <= 1; s += 2) cw_r2d_circle(cw_v2(x + s * r * 0.78f, y - r * 0.45f), r * 0.32f, 14, mask);
  cw_r2d_circle(cw_v2(x, y + r * 0.05f), r * 0.9f, 28, furc);
  cw_r2d_circle(cw_v2(x, y + r * 0.38f), r * 0.5f, 22, mask);
  for (int s = -1; s <= 1; s += 2) {
    float ex = x + s * r * 0.38f, ey = y - r * 0.08f;
    if (sad) {
      cw_r2d_line(cw_v2(ex - r * 0.14f, ey - r * 0.14f), cw_v2(ex + r * 0.14f, ey + r * 0.14f), r * 0.08f + 1, mask);
      cw_r2d_line(cw_v2(ex - r * 0.14f, ey + r * 0.14f), cw_v2(ex + r * 0.14f, ey - r * 0.14f), r * 0.08f + 1, mask);
    } else {
      cw_r2d_circle(cw_v2(ex, ey), r * 0.24f, 16, rgba(1, 1, 1, a));
      cw_r2d_circle(cw_v2(ex, ey + r * 0.02f), r * 0.16f, 14, rgba(0.25f, 0.15f, 0.07f, a));
      cw_r2d_circle(cw_v2(ex, ey + r * 0.02f), r * 0.09f, 12, rgba(0.02f, 0.02f, 0.02f, a));
      cw_r2d_circle(cw_v2(ex + r * 0.07f, ey - r * 0.07f), r * 0.05f, 8, rgba(1, 1, 1, a));
    }
  }
  cw_r2d_circle(cw_v2(x, y + r * 0.28f), r * 0.13f, 12, rgba(0.02f, 0.02f, 0.02f, a));
  cw_r2d_circle(cw_v2(x + r * 0.06f, y + r * 0.62f), r * 0.1f, 10, rgba(1, 0.45f, 0.55f, a));
}

static const char *menu_prompt(void) {
  switch (G.state) {
  case GS_TITLE: return last_device == DEV_TOUCH ? "tap to choose" : last_device == DEV_MOUSE ? "click to choose" : last_device == DEV_PAD ? "d-pad: choose   (A): go" : "A / D: choose   Space: go";
  case GS_SHEET:
    return last_device == DEV_MOUSE || last_device == DEV_TOUCH ? "pick a pug, then click / tap a setting to change it   KICK OFF to play"
         : last_device == DEV_PAD   ? "up / down: pug   left / right: change   (X): next setting   LB / RB: switch sides   START: kick off"
                                    : "W / S: pug   A / D: change   E: next setting   Enter: kick off   Esc: back";
  case GS_STADIUM:
    return last_device == DEV_TOUCH ? "tap a stadium" : last_device == DEV_MOUSE ? "click a stadium" : last_device == DEV_PAD ? "left / right: choose   (A): go   BACK: back"
                                                                               : "A / D: choose   Space: go   Esc: back";
 case GS_REPLAY: return last_device == DEV_TOUCH ? "tap to skip" : last_device == DEV_MOUSE ? "click to skip" : "any button to skip";
  case GS_FULLTIME: return last_device == DEV_TOUCH ? "tap to continue" : last_device == DEV_MOUSE ? "click to continue" : last_device == DEV_PAD ? "(A) to continue" : "Space to continue";
  default: return "";
  }
}

static void glow_cursor(cw_rect r, float s, float rad, bool dim) {
  float g = 4 * s + 2 * s * cw_sin(G.ticks * 0.15f);
  round_rect(cw_rect_make(r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g), rad + g, dim ? rgba(0.7f, 0.7f, 0.75f, 0.9f) : rgba(1, 0.85f, 0.3f, 0.95f));
}

static void draw_title(float W, float H, float s) {
  float bob = 4 * s * cw_sin(G.ticks * 0.05f);
  const char *logo = "PUGBALL";
  static const float LC[7][3] = { { 1, 0.78f, 0.3f }, { 1, 0.6f, 0.25f }, { 1, 0.42f, 0.35f }, { 0.35f, 0.7f, 1 },
                                  { 0.4f, 0.85f, 0.55f }, { 1, 0.78f, 0.3f }, { 1, 0.55f, 0.7f } };
  float size = 104 * s;
  cw_text_style st = { .size = size };
  float total = cw_font_measure(font_title, logo, &st).width, x = (W - total) * 0.5f;
  for (int i = 0; logo[i]; i++) {
    char ch[2] = { logo[i], 0 };
    float w = cw_font_measure(font_title, ch, &st).width;
    float jump = fabsf(cw_sin(G.ticks * 0.06f - i * 0.5f)) * 12 * s;
    bold(ch, x, H * 0.05f + bob - jump, size, rgba(LC[i][0], LC[i][1], LC[i][2], 1), CW_ALIGN_LEFT, 0);
    x += w;
  }
  chip("pugs with jet packs. one very fat, very cross cat.", W * 0.5f, H * 0.3f, 19 * s, 0.8f);
  text("pugs with jet packs. one very fat, very cross cat.", 0, H * 0.3f, 19 * s, rgba(1, 0.97f, 0.92f, 1), CW_ALIGN_CENTER, W);
  static const char *const ITEMS[2] = { "EXHIBITION", "PUG CUP" };
  static const char *const SUBS[2] = { "1v1 to 3v3, players and CPUs", "five stadiums, in order" };
  for (int i = 0; i < 2; i++) {
    cw_rect r = title_button(i, W, H);
    bool cur = G.menu == i;
    if (cur) glow_cursor(r, s, 20, false);
    panel(r, 1, rgba(1, 1, 1, 0.8f), cur ? rgba(0.95f, 0.45f, 0.2f, 0.95f) : rgba(0.12f, 0.1f, 0.28f, 0.85f));
    bold(ITEMS[i], r.x, r.y + 4 * s, 30 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
    text(SUBS[i], r.x, r.y + 38 * s, 13 * s, rgba(1, 0.92f, 0.8f, 0.9f), CW_ALIGN_CENTER, r.w);
  }
  prompt_line(menu_prompt(), W, H * 0.935f, 16 * s);
}

static void draw_sheet(float W, float H, float s) {
  bold("THE TEAM SHEET", 0, H * 0.02f, 44 * s, rgba(1, 0.8f, 0.3f, 1), CW_ALIGN_CENTER, W);
  char buf[96];
  for (int t = 0; t < 2; t++) {
    cw_rect r0 = slot_rect(t, W, H);
    digits(TEAM_NAMES[t], r0.x, r0.y - 28 * s, 20 * s, rgba(TEAM_RGB[t][0] * 0.6f + 0.4f, TEAM_RGB[t][1] * 0.6f + 0.4f, TEAM_RGB[t][2] * 0.6f + 0.4f, 1),
           CW_ALIGN_CENTER, r0.w);
  }
  static const char *const COL[4] = { "WHO", "COAT", "HAT", "TRAIL" };
  for (int i = 0; i < MAXP; i++) {
    cw_rect r = slot_rect(i, W, H);
    int team = i & 1;
    bool cur = G.cursor == i, off = G.slot[i] == SLOT_OFF;
    cw_color tc = rgba(TEAM_RGB[team][0], TEAM_RGB[team][1], TEAM_RGB[team][2], 1);
    if (cur) glow_cursor(r, s, 18, false);
    panel(r, off ? 0.55f : 1, tc, rgba(0.08f, 0.06f, 0.16f, 0.84f));
    pug_face(r.x + 40 * s, r.y + r.h * 0.5f, 25 * s, G.look[i].coat, team, off, off ? 0.45f : 1);
    text(PUG_NAMES[i], r.x + 78 * s, r.y + 6 * s, 22 * s, rgba(1, 1, 1, off ? 0.5f : 1), CW_ALIGN_LEFT, 0);
    cw_color sc = G.slot[i] == SLOT_HUMAN ? rgba(0.55f, 1, 0.6f, 1) : off ? rgba(0.65f, 0.65f, 0.7f, 1) : rgba(0.75f, 0.85f, 1, 1);
    const char *vals[4] = { SLOT_NAME[G.slot[i]], PUG_COAT_NAME[G.look[i].coat], HAT_NAME[G.look[i].hat], TRAIL_NAME[G.look[i].trail] };
    for (int c = 0; c < 4; c++) {
      cw_rect cr = cell_rect(r, c, s);
      float cx = cr.x + 5 * s, cy = cr.y + 2 * s;
      bool sel = cur && G.col == c;
      if (sel) round_rect(cr, 8 * s, rgba(1, 0.8f, 0.3f, 0.3f));
      snprintf(buf, sizeof buf, "%s%s%s", sel ? "< " : "", vals[c], sel ? " >" : "");
      text(buf, cx, cy, 14 * s, c == 0 ? sc : rgba(1, 0.95f, 0.85f, off ? 0.4f : 0.95f), CW_ALIGN_LEFT, 0);
      (void)COL;
    }
    if (G.slot[i] == SLOT_HUMAN) {
      int k = G.pad_of[i];
      snprintf(buf, sizeof buf, k == 0 ? "P1  pad / keys" : "P%d  pad %d", k + 1, k + 1);
      text(buf, r.x, r.y + 8 * s, 12 * s, rgba(0.6f, 1, 0.65f, 0.95f), CW_ALIGN_RIGHT, r.w - 10 * s);
    }
  }
  cw_rect gr = go_rect(W, H);
  if (G.cursor == MAXP) glow_cursor(gr, s, 18, false);
  panel(gr, 1, rgba(1, 1, 1, 0.75f), rgba(0.95f, 0.45f, 0.2f, 0.94f));
  snprintf(buf, sizeof buf, "KICK OFF THE CAT   %d:00", MATCH_SECS[G.secs_i] / 60);
  bold(buf, gr.x, gr.y + 9 * s, 24 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, gr.w);
  if (last_device == DEV_PAD || last_device == DEV_KEYS)
    prompt_line("pads 2-4: (A) joins   left / right: switch sides   (B) leaves", W, H * 0.17f + 3 * 102 * s + 10 * s, 12 * s);
  prompt_line(menu_prompt(), W, H * 0.935f, 13 * s);
}

static void draw_stadium_select(float W, float H, float s) {
  bold(G.mode == MODE_CUP ? "THE PUG CUP" : "PICK A STADIUM", 0, H * 0.06f, 50 * s, rgba(1, 0.8f, 0.3f, 1), CW_ALIGN_CENTER, W);
  char buf[96];
  for (int i = 0; i < STADIUMS; i++) {
    cw_rect r = tile_rect(i, W, H);
    bool cur = i == G.cursor;
    bool unlocking = i == G.unlock_tile && G.unlock_ticks < 80;
    float ut = unlocking ? (float)G.unlock_ticks : 999;
    bool locked = !stadium_open(i) || (unlocking && ut < 24);
    if (i == G.bump_tile && G.bump_ticks > 0) r.x += cw_sin(G.bump_ticks * 1.7f) * 7 * s * G.bump_ticks / 18.0f;
    if (unlocking && ut >= 24) {
      float k = (ut - 24) / 56, pop = 1 + 0.14f * cw_sin(k * 3.1415927f) * (1 - k);
      r = cw_rect_make(r.x - r.w * (pop - 1) * 0.5f, r.y - r.h * (pop - 1) * 0.5f, r.w * pop, r.h * pop);
    }
    const float *tc = THEME_RGB[i];
    if (cur) glow_cursor(r, s, 20, locked);
    if (locked) panel(r, 1, rgba(0.4f, 0.4f, 0.45f, 1), rgba(0.17f, 0.16f, 0.22f, 0.92f));
    else panel(r, 1, rgba(tc[0], tc[1], tc[2], 1), rgba(tc[0] * 0.32f, tc[1] * 0.32f, tc[2] * 0.32f + 0.06f, 0.92f));
    snprintf(buf, sizeof buf, "%d", i + 1);
    digits(buf, r.x + 10 * s, r.y + 6 * s, 28 * s, locked ? rgba(0.55f, 0.55f, 0.6f, 1) : rgba(tc[0], tc[1], tc[2], 1), CW_ALIGN_LEFT, 0);
    text(STADIUM_TITLES[i], r.x + 8 * s, r.y + 44 * s, 17 * s, rgba(1, 1, 1, locked ? 0.45f : 1), CW_ALIGN_LEFT, r.w - 14 * s);
    if (locked) {
      padlock(r.x + r.w * 0.5f, r.y + r.h - 22 * s, 26 * s, 0, 1);
      continue;
    }
    if (unlocking && ut < 70) {
      float k = (ut - 24) / 46;
      padlock(r.x + r.w * 0.5f + k * 30 * s, r.y + r.h - 22 * s - k * k * 90 * s, 26 * s, fminf(1, k * 4), 1 - k);
      for (int p = 0; p < 8; p++) {
        float a = p * 0.785f + 0.3f, d = (10 + k * 70) * s;
        star(r.x + r.w * 0.5f + d * cw_cos(a), r.y + r.h * 0.5f + d * cw_sin(a), (6 - 4 * k) * s, rgba(1, 0.9f, 0.3f, 1 - k), k * 3);
      }
    }
    if (G.mode == MODE_CUP) {
      if (SAVE.won[i]) star(r.x + r.w - 20 * s, r.y + 20 * s, 13 * s, rgba(1, 0.82f, 0.15f, 1), 0);
      snprintf(buf, sizeof buf, "%dv%d", CUP_SIZE[i], CUP_SIZE[i]);
      text(buf, r.x + 8 * s, r.y + r.h - 26 * s, 14 * s, rgba(1, 0.95f, 0.85f, 0.85f), CW_ALIGN_LEFT, 0);
    }
  }
  if (G.unlock_tile >= 0 && G.unlock_ticks >= 24 && G.unlock_ticks < 110) {
    snprintf(buf, sizeof buf, "%s IS OPEN!", STADIUM_TITLES[G.unlock_tile]);
    float a = cw_clampf((110 - G.unlock_ticks) / 20.0f, 0, 1);
    bold(buf, 0, H * 0.2f, 24 * s, rgba(1, 0.92f, 0.4f, a), CW_ALIGN_CENTER, W);
  }
  static const char *const BLURB[STADIUMS] = { "Sunshine, picnic blankets and a very grumpy cat.", "Floodlights, neon and a crowd that never sleeps.",
    "The sun goes down, the cat stays up.", "Snow on the fence, steam on your breath.", "Hedges, roses and a fountain. Mind the topiary." };
  const char *b = stadium_open(G.cursor) ? BLURB[G.cursor] : G.mode == MODE_CUP ? "Win the fixture before it to open this one."
                                                                                : "Win it in the Pug Cup to play here.";
  prompt_line(b, W, H * 0.62f, 18 * s);
  if (G.mode == MODE_CUP && stadium_open(G.cursor)) {
    static const char *const LV[3] = { "rookies", "pros", "all-stars" };
    stadium_info dummy = { 0 };
    (void)dummy;
    snprintf(buf, sizeof buf, "fixture %d: %dv%d against the %s", G.cursor + 1, CUP_SIZE[G.cursor], CUP_SIZE[G.cursor],
             LV[G.cursor < 1 ? 0 : G.cursor < 3 ? 1 : 2]);
    prompt_line(buf, W, H * 0.7f, 15 * s);
  }
  prompt_line(menu_prompt(), W, H * 0.92f, 15 * s);
}

/* a boost gauge: a ring of segments filling clockwise, the number inside */
static void boost_gauge(float cx, float cy, float r, float boost, int team, float s) {
  enum { SEGS = 32 };
  static float SA[SEGS][2], CA[SEGS][2]; /* each segment's two edges */
  if (CA[0][0] == 0)
    for (int k = 0; k < SEGS; k++) {
      cw_sincos(-2.35f + 4.7f * k / SEGS, &SA[k][0], &CA[k][0]);
      cw_sincos(-2.35f + 4.7f * (k + 0.8f) / SEGS, &SA[k][1], &CA[k][1]);
    }
  int lit = (int)(boost / 100 * SEGS + 0.5f);
  cw_r2d_circle(cw_v2(cx, cy), r * 1.12f, 40, rgba(0.04f, 0.03f, 0.1f, 0.55f));
  /* the ring of segments: one draw call */
  cw_vertex2d v[SEGS * 4];
  uint32_t idx[SEGS * 6];
  for (int k = 0; k < SEGS; k++) {
    float hk = (float)k / SEGS;
    cw_color c = k < lit ? rgba(1, 0.45f + 0.4f * hk, 0.12f + 0.2f * hk, 1) : rgba(0.3f, 0.28f, 0.36f, 0.6f);
    cw_vec2 q[4] = { cw_v2(cx + r * SA[k][0], cy - r * CA[k][0]), cw_v2(cx + r * SA[k][1], cy - r * CA[k][1]),
                     cw_v2(cx + r * 0.78f * SA[k][1], cy - r * 0.78f * CA[k][1]), cw_v2(cx + r * 0.78f * SA[k][0], cy - r * 0.78f * CA[k][0]) };
    for (int j = 0; j < 4; j++) v[k * 4 + j] = (cw_vertex2d){ q[j], cw_v2(0, 0), c };
    uint32_t b0 = (uint32_t)k * 4;
    uint32_t t[6] = { b0, b0 + 1, b0 + 2, b0, b0 + 2, b0 + 3 };
    memcpy(idx + k * 6, t, sizeof t);
  }
  cw_r2d_triangles((cw_texture){ 0 }, v, SEGS * 4, idx, SEGS * 6);
  char buf[8];
  snprintf(buf, sizeof buf, "%d", (int)boost);
  digits(buf, cx - r, cy - r * 0.32f, r * 0.55f, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r * 2);
  text("BOOST", cx - r, cy + r * 0.3f, fmaxf(10 * s, r * 0.2f), rgba(TEAM_RGB[team][0] * 0.5f + 0.5f, TEAM_RGB[team][1] * 0.5f + 0.5f, TEAM_RGB[team][2] * 0.5f + 0.5f, 0.9f),
       CW_ALIGN_CENTER, r * 2);
}

/* the view the HUD draws into (split screen: one human's camera and
 * rectangle; otherwise the scene's camera over the whole canvas) */
static struct { cw_node cam; float x, y, w, h; } HV;
/* world -> canvas through the HUD view's camera; false when it is behind the camera */
static bool project(t3_vec3 p, float W, float H, cw_vec2 *out) {
  cw_node cam = cw_node_valid(HV.cam) ? HV.cam : cw_render3d_camera(cw_scene_app_tree());
  if (!cw_node_valid(cam)) return false;
  float ox = 0, oy = 0;
  if (cw_node_valid(HV.cam)) ox = HV.x, oy = HV.y, W = HV.w, H = HV.h;
  t3_camera *c = (t3_camera *)cw_node3d_object(cam);
  if (!c) return false;
  t3_vec3 v = t3_vec3_apply_mat4(p, &c->matrix_world_inverse);
  if (v.z > -0.1f) {
    /* behind: mirror it so the edge arrow still points the right way */
    *out = cw_v2(ox + W * 0.5f - v.x * 1000, oy + H * 0.5f + v.y * 1000);
    return false;
  }
  t3_vec3 n = t3_vec3_apply_mat4(v, &c->projection_matrix);
  *out = cw_v2(ox + (n.x * 0.5f + 0.5f) * W, oy + (0.5f - n.y * 0.5f) * H);
  return true;
}

/* a marker over something: a chevron with a label when it is on screen, an
 * arrow pinned to the screen's edge pointing at it when it is not */
static void marker(t3_vec3 at, const char *label, cw_color c, float W, float H, float s, bool always_chevron) {
  cw_vec2 sp;
  bool front = project(at, W, H, &sp);
  float m = 34 * s, X0 = 0, Y0 = 0;
  if (cw_node_valid(HV.cam)) X0 = HV.x, Y0 = HV.y, W = HV.w, H = HV.h;
  bool on = front && sp.x > X0 + m && sp.x < X0 + W - m && sp.y > Y0 + m && sp.y < Y0 + H - m;
  cw_color dark = rgba(0.05f, 0.03f, 0.1f, 0.85f);
  if (on) {
    if (!always_chevron) return;
    float bob = 3 * s * cw_sin(G.ticks * 0.15f), y = sp.y - 8 * s + bob, w = 11 * s;
    cw_vec2 t[3] = { cw_v2(sp.x - w, y - w), cw_v2(sp.x + w, y - w), cw_v2(sp.x, y + w * 0.4f) };
    cw_vec2 o[3] = { cw_v2(sp.x - w - 3 * s, y - w - 2 * s), cw_v2(sp.x + w + 3 * s, y - w - 2 * s), cw_v2(sp.x, y + w * 0.4f + 3.5f * s) };
    cw_r2d_polygon(o, 3, dark);
    cw_r2d_polygon(t, 3, c);
    text(label, sp.x - 40 * s, y - w - 22 * s, 15 * s, c, CW_ALIGN_CENTER, 80 * s);
    return;
  }
  /* pin to the edge along the line from the centre */
  float cx = X0 + W * 0.5f, cy = Y0 + H * 0.5f, dx = sp.x - cx, dy = sp.y - cy;
  float l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-3f) dx = 0, dy = 1, l = 1;
  dx /= l, dy /= l;
  float kx = dx != 0 ? (W * 0.5f - m) / fabsf(dx) : 1e9f, ky = dy != 0 ? (H * 0.5f - m) / fabsf(dy) : 1e9f, k = fminf(kx, ky);
  float x = cx + dx * k, y = cy + dy * k, r = 16 * s;
  cw_r2d_circle(cw_v2(x, y), r + 3 * s, 20, dark);
  cw_r2d_circle(cw_v2(x, y), r, 20, c);
  cw_vec2 tip = cw_v2(x + dx * (r + 12 * s), y + dy * (r + 12 * s)), sd = cw_v2(-dy * r * 0.6f, dx * r * 0.6f);
  cw_vec2 tri[3] = { tip, cw_v2(x + dx * r + sd.x, y + dy * r + sd.y), cw_v2(x + dx * r - sd.x, y + dy * r - sd.y) };
  cw_r2d_polygon(tri, 3, c);
  text(label, x - r, y - 8 * s, 13 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r * 2);
}

/* the on-screen controls, while touch is the device in use */
static void touch_button(int id, const char *label, cw_color c, float s) {
  (void)s;
  if (id < 0) return;
  bool on = cw_touch_control_active(id);
  /* the control's rect lives in window coordinates: its centre and size on the canvas */
  int ww, wh;
  cw_window_size(&ww, &wh);
  cw_vec2 cs = cw_r2d_canvas_size();
  float kx = cs.x / (float)ww, ky = cs.y / (float)wh;
  float H = (float)wh, k = H / 540.0f;
  float x = 0, y = 0, w = 0, h = 0;
  if (id == TOUCH_JUMP) x = ww - 168 * k, y = H - 178 * k, w = 150 * k, h = 150 * k;
  else if (id == TOUCH_BOOST) x = ww - 318 * k, y = H - 140 * k, w = 140 * k, h = 130 * k;
  else if (id == TOUCH_SLIDE) x = ww - 168 * k, y = H - 318 * k, w = 150 * k, h = 128 * k;
  else x = ww - 100 * k, y = 76 * k, w = 92 * k, h = 70 * k;
  cw_vec2 ctr = cw_v2((x + w * 0.5f) * kx, (y + h * 0.5f) * ky);
  float r = fminf(w * kx, h * ky) * 0.42f;
  cw_r2d_circle(ctr, r + 3, 32, rgba(0.04f, 0.03f, 0.1f, on ? 0.75f : 0.45f));
  cw_r2d_circle(ctr, r, 32, rgba(c.r, c.g, c.b, on ? 0.9f : 0.42f));
  text(label, ctr.x - r, ctr.y - r * 0.3f, fmaxf(12, r * 0.42f), rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER, r * 2);
}
static void draw_touch(float W, float H, float s) {
  (void)W, (void)H;
  if (TOUCH_STICK < 0) return;
  int ww, wh;
  cw_window_size(&ww, &wh);
  cw_vec2 cs = cw_r2d_canvas_size();
  float kx = cs.x / (float)ww, ky = cs.y / (float)wh, k = (float)wh / 540.0f;
  bool on = cw_touch_control_active(TOUCH_STICK);
  cw_vec2 c = cw_touch_stick_center(TOUCH_STICK);
  if (!on) c = cw_v2(150 * k, wh - 150 * k); /* where a thumb starts: drawn there until it lands */
  c = cw_v2(c.x * kx, c.y * ky);
  float r = 64 * k * ky;
  cw_r2d_circle(c, r + 4, 40, rgba(0.04f, 0.03f, 0.1f, 0.35f));
  cw_r2d_circle_lines(c, r, 3 * s, 40, rgba(1, 1, 1, on ? 0.8f : 0.45f));
  cw_vec2 v = on ? cw_touch_stick_value(TOUCH_STICK) : cw_v2(0, 0);
  cw_r2d_circle(cw_v2(c.x + v.x * r, c.y + v.y * r), r * 0.42f, 28, rgba(1, 0.85f, 0.35f, on ? 0.9f : 0.5f));
  if (!on) text("steer + go", c.x - r, c.y + r + 6 * s, 12 * s, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r * 2);
  touch_button(TOUCH_JUMP, "JUMP", rgba(0.3f, 0.75f, 0.35f, 1), s);
  touch_button(TOUCH_BOOST, "BOOST", rgba(1, 0.5f, 0.15f, 1), s);
  touch_button(TOUCH_SLIDE, "SLIDE", rgba(0.35f, 0.55f, 1, 1), s);
  touch_button(TOUCH_CAM, "CAM", rgba(0.6f, 0.45f, 0.9f, 1), s);
}

/* the scoreboard: blue score | clock | red score, centred on cx from y down
 * (k scales it: 1 alone on the screen, smaller where the split views meet) */
static void scoreboard(float cx, float y, float k) {
  const sim *S = &G.S;
  char buf[32];
  float bw = 120 * k, bh = 54 * k, cwid = 130 * k, x0 = cx - cwid * 0.5f - bw;
  for (int t = 0; t < 2; t++) {
    cw_rect r = cw_rect_make(t ? cx + cwid * 0.5f : x0, y, bw, bh);
    round_rect(r, 12 * k, rgba(TEAM_RGB[t][0] * 0.8f, TEAM_RGB[t][1] * 0.8f, TEAM_RGB[t][2] * 0.8f, 0.92f));
    snprintf(buf, sizeof buf, "%d", S->score[t]);
    digits(buf, r.x, r.y + 4 * k, 38 * k, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
    text(TEAM_NAMES[t], r.x, r.y + bh + 2 * k, fmaxf(12 * k, 9), rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER, r.w);
  }
  cw_rect cr = cw_rect_make(cx - cwid * 0.5f, y - 2 * k, cwid, bh + 4 * k);
  round_rect(cr, 12 * k, rgba(0.06f, 0.05f, 0.14f, 0.9f));
  int secs = S->overtime ? S->clock / 60 : (S->clock + 59) / 60;
  snprintf(buf, sizeof buf, "%s%d:%02d", S->overtime ? "+" : "", secs / 60, secs % 60);
  bool hurry = !S->overtime && secs <= 30 && S->phase == PH_PLAY;
  digits(buf, cr.x, cr.y + 10 * k, 30 * k, hurry && (G.ticks / 20) % 2 ? rgba(1, 0.5f, 0.3f, 1) : rgba(1, 1, 1, 1), CW_ALIGN_CENTER, cr.w);
  if (S->overtime) text("OVERTIME", cr.x, cr.y + cr.h + 2 * k, fmaxf(12 * k, 9), rgba(1, 0.8f, 0.3f, 1), CW_ALIGN_CENTER, cr.w);
}

/* the pitch from above in r: both goals, the cat and every pug in its team's
 * colour (the players' labelled). The pitch's length runs across, the
 * Biscuits' goal on the left */
static void minimap(cw_rect r, float s) {
  const sim *S = &G.S;
  float k = fminf(r.w / (2 * (AL + GD) + 2), r.h / (2 * AW + 2));
  float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
#define MAP(p) cw_v2(cx + (p).z * k, cy - (p).x * k)
  const float *tc = THEME_RGB[G.info ? G.info->theme : 0];
  cw_color line = rgba(1, 1, 1, 0.55f);
  /* the goals behind their lines, then the pitch over them */
  for (int t = 0; t < 2; t++) {
    float side = t == TEAM_BLUE ? -1.0f : 1.0f;
    float gx = side < 0 ? cx - (AL + GD) * k : cx + AL * k;
    round_rect(cw_rect_make(gx, cy - GW * k, GD * k, 2 * GW * k), 3 * s,
               rgba(TEAM_RGB[t][0] * 0.7f, TEAM_RGB[t][1] * 0.7f, TEAM_RGB[t][2] * 0.7f, 0.95f));
  }
  cw_rect pr = cw_rect_make(cx - AL * k, cy - AW * k, 2 * AL * k, 2 * AW * k);
  round_rect(cw_rect_make(pr.x - 2 * s, pr.y - 2 * s, pr.w + 4 * s, pr.h + 4 * s), AC * k + 2 * s, rgba(0.9f, 0.92f, 1, 0.75f));
  round_rect(pr, AC * k, rgba(0.1f + tc[0] * 0.18f, 0.22f + tc[1] * 0.2f, 0.12f + tc[2] * 0.15f, 0.95f));
  cw_r2d_line(cw_v2(cx, pr.y), cw_v2(cx, pr.y + pr.h), 1.5f * s, line);
  cw_r2d_circle_lines(cw_v2(cx, cy), 6 * k, 1.5f * s, 32, line);
  for (int t = 0; t < 2; t++) { /* the goal mouths */
    float gx = t == TEAM_BLUE ? pr.x : pr.x + pr.w;
    cw_r2d_line(cw_v2(gx, cy - GW * k), cw_v2(gx, cy + GW * k), 3 * s, rgba(TEAM_RGB[t][0] * 0.5f + 0.5f, TEAM_RGB[t][1] * 0.5f + 0.5f, TEAM_RGB[t][2] * 0.5f + 0.5f, 1));
  }
  /* the pugs: a dot with a nose pointing where it faces */
  cw_color dark = rgba(0.04f, 0.03f, 0.1f, 0.9f);
  for (int i = 0; i < MAXP; i++) {
    const pug *u = &S->p[i];
    if (!u->on || u->dead) continue;
    cw_vec2 c = MAP(u->p);
    t3_vec3 f = pug_fwd(u);
    float fl = sqrtf(f.x * f.x + f.z * f.z), rr = (G.human_of[i] >= 0 ? 6.5f : 5) * s;
    cw_color col = rgba(TEAM_RGB[u->team][0], TEAM_RGB[u->team][1], TEAM_RGB[u->team][2], 1);
    if (fl > 0.1f) {
      cw_vec2 tip = cw_v2(c.x + f.z / fl * rr * 1.9f, c.y - f.x / fl * rr * 1.9f);
      cw_r2d_line(c, tip, 3.5f * s, dark);
      cw_r2d_line(c, tip, 2 * s, col);
    }
    cw_r2d_circle(c, rr + 1.5f * s, 16, G.human_of[i] >= 0 ? rgba(1, 1, 1, 1) : dark);
    cw_r2d_circle(c, rr, 16, col);
    if (G.human_of[i] >= 0) {
      char lb[12];
      snprintf(lb, sizeof lb, "P%d", G.human_of[i] + 1);
      text(lb, c.x - 20 * s, c.y - rr - 17 * s, 13 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, 40 * s);
    }
  }
  /* the cat, over everything */
  cw_vec2 b = MAP(S->ball);
  float br = fmaxf(RB * k, 5 * s);
  cw_r2d_circle(b, br + 2 * s, 20, dark);
  cw_r2d_circle(b, br, 20, rgba(1, 0.68f, 0.3f, 1));
#undef MAP
}

/* one human's corner of the HUD in its view: the boost gauge and which
 * camera it has at the bottom, its name at the top, on the view's outer side
 * (clear of the scoreboard where the views meet) */
static void view_hud(int i, float W, float s) {
  const sim *S = &G.S;
  char buf[16];
  int t = S->p[i].team, pad = G.human_of[i];
  cw_color tc = rgba(TEAM_RGB[t][0] * 0.6f + 0.4f, TEAM_RGB[t][1] * 0.6f + 0.4f, TEAM_RGB[t][2] * 0.6f + 0.4f, 1);
  snprintf(buf, sizeof buf, "P%d", pad + 1);
  bool left = HV.x < 1 && HV.w < W * 0.9f; /* the left column of the quadrants */
  float r = 34 * s, gx = left ? HV.x + r + 14 * s : HV.x + HV.w - r - 14 * s, gy = HV.y + HV.h - r - 12 * s;
  boost_gauge(gx, gy, r, S->p[i].boost, t, s);
  bool bc = G.ballcam_h[pad];
  const char *cam = bc ? "BALL CAM" : "CAR CAM";
  chip(cam, gx, gy - r - 24 * s, 11 * s, 1);
  text(cam, gx - r - 10 * s, gy - r - 24 * s, 11 * s, bc ? rgba(1, 0.85f, 0.35f, 1) : rgba(0.75f, 0.9f, 1, 1), CW_ALIGN_CENTER, 2 * r + 20 * s);
  if (left) text(buf, HV.x + 12 * s, HV.y + 8 * s, 22 * s, tc, CW_ALIGN_LEFT, 0);
  else text(buf, HV.x, HV.y + 8 * s, 22 * s, tc, CW_ALIGN_RIGHT, HV.w - 12 * s);
}

static void draw_hud(float W, float H, float s) {
  const sim *S = &G.S;
  char buf[96];
  const cw_view *vs = NULL;
  int nv = G.nviews ? cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &vs) : 0;
  bool split = nv >= 2 && (G.state == GS_MATCH || G.state == GS_PAUSE);
  if (split) {
    /* each view: its own pug's chevron, the cat's edge arrow, its corner of
     * the HUD and, at a kickoff, the countdown */
    for (int v = 0; v < nv; v++) {
      int i = G.view_slot[v];
      HV.cam = vs[v].camera, HV.x = vs[v].x * W, HV.y = vs[v].y * H, HV.w = vs[v].w * W, HV.h = vs[v].h * H;
      int t = S->p[i].team;
      cw_color tc = rgba(TEAM_RGB[t][0] * 0.6f + 0.4f, TEAM_RGB[t][1] * 0.6f + 0.4f, TEAM_RGB[t][2] * 0.6f + 0.4f, 1);
      snprintf(buf, sizeof buf, "P%d", G.human_of[i] + 1);
      if (!S->p[i].dead) marker(v3add(S->p[i].p, (t3_vec3){ 0, 1.15f, 0 }), buf, tc, W, H, s, true);
      marker(S->ball, "CAT", rgba(1, 0.68f, 0.3f, 1), W, H, s, false);
      view_hud(i, W, s);
      float vk = HV.h / H; /* the view's share of the height */
      if (S->phase == PH_KICKOFF) {
        int n = 3 - S->phase_ticks / 60;
        float f = (S->phase_ticks % 60) / 60.0f, pop = 1 + 0.35f * (1 - f) * (1 - f);
        snprintf(buf, sizeof buf, "%d", n < 1 ? 1 : n);
        digits(buf, HV.x, HV.y + HV.h * 0.2f, 96 * s * vk * pop, rgba(1, 0.9f, 0.4f, 1 - f * 0.3f), CW_ALIGN_CENTER, HV.w);
      } else if (S->phase == PH_PLAY && S->phase_ticks < 50 && S->tick > 200) {
        float f = S->phase_ticks / 50.0f;
        bold("GO!", HV.x, HV.y + HV.h * 0.16f, 110 * s * vk * (1 + 0.3f * (1 - f)), rgba(1, 0.75f, 0.2f, 1 - f), CW_ALIGN_CENTER, HV.w);
      }
      HV.cam = (cw_node){ 0 };
    }
    cw_r2d_rect(cw_rect_make(0, H * 0.5f - 1.5f * s, W, 3 * s), rgba(0.05f, 0.03f, 0.1f, 0.9f));
    if (nv >= 3) cw_r2d_rect(cw_rect_make(W * 0.5f - 1.5f * s, 0, 3 * s, H), rgba(0.05f, 0.03f, 0.1f, 0.9f));
    /* the shared scoreboard: on the seam (2), atop the free quadrant with the
     * mini map under it (3), where the four views meet (4) */
    if (nv == 2) scoreboard(W * 0.5f, H * 0.5f - 27 * 0.62f * s, 0.62f * s);
    else if (nv == 4) scoreboard(W * 0.5f, H * 0.5f - 27 * 0.55f * s, 0.55f * s);
    else {
      cw_rect q = cw_rect_make(W * 0.5f + 1.5f * s, 0, W * 0.5f - 1.5f * s, H * 0.5f - 1.5f * s);
      cw_r2d_rect(q, rgba(0.07f, 0.06f, 0.16f, 1));
      scoreboard(q.x + q.w * 0.5f, q.y + 10 * s, 0.75f * s);
      minimap(cw_rect_make(q.x + 12 * s, q.y + 72 * s, q.w - 24 * s, q.h - 108 * s), s); /* the news feed under it */
    }
  } else {
    scoreboard(W * 0.5f, 10 * s, s);
    if (G.state == GS_MATCH || G.state == GS_PAUSE) {
      /* the humans' markers (always: they read through everything), and the
       * cat's edge arrow when it is off screen */
      for (int i = 0; i < MAXP; i++) {
        if (G.human_of[i] < 0 || S->p[i].dead) continue;
        snprintf(buf, sizeof buf, "P%d", G.human_of[i] + 1);
        int t = S->p[i].team;
        marker(v3add(S->p[i].p, (t3_vec3){ 0, 1.15f, 0 }), buf, rgba(TEAM_RGB[t][0] * 0.6f + 0.4f, TEAM_RGB[t][1] * 0.6f + 0.4f, TEAM_RGB[t][2] * 0.6f + 0.4f, 1),
               W, H, s, true);
      }
      marker(S->ball, "CAT", rgba(1, 0.68f, 0.3f, 1), W, H, s, false);
    }
    /* the boost gauges (one human: big, with the camera it has) */
    int nh = humans(), k = 0;
    for (int i = 0; i < MAXP; i++) {
      if (G.human_of[i] < 0) continue;
      float r = nh == 1 ? 58 * s : 38 * s;
      float cx = nh == 1 ? W - 84 * s : (k % 2 ? W - 60 * s : 60 * s), cy = nh == 1 ? H - 84 * s : H - 60 * s - (k / 2) * 92 * s;
      if (last_device == DEV_TOUCH && nh == 1) cx = 70 * s, cy = 120 * s, r = 46 * s; /* the thumbs have the bottom corners */
      boost_gauge(cx, cy, r, S->p[i].boost, S->p[i].team, s);
      if (nh == 1) {
        bool bc = G.ballcam_h[G.human_of[i]];
        const char *cam = bc ? "BALL CAM" : "CAR CAM";
        chip(cam, cx, cy - r - 30 * s, 12 * s, 1);
        text(cam, cx - r, cy - r - 30 * s, 12 * s, bc ? rgba(1, 0.85f, 0.35f, 1) : rgba(0.75f, 0.9f, 1, 1), CW_ALIGN_CENTER, r * 2);
      } else {
        snprintf(buf, sizeof buf, "P%d", G.human_of[i] + 1);
        text(buf, cx - r, cy - r - 20 * s, 14 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r * 2);
      }
      k++;
    }
  }
  /* the kickoff countdown (split screen: in each view, above) */
  if (!split && S->phase == PH_KICKOFF) {
    int n = 3 - S->phase_ticks / 60;
    float f = (S->phase_ticks % 60) / 60.0f, pop = 1 + 0.35f * (1 - f) * (1 - f);
    snprintf(buf, sizeof buf, "%d", n < 1 ? 1 : n);
    digits(buf, 0, H * 0.17f, 96 * s * pop, rgba(1, 0.9f, 0.4f, 1 - f * 0.3f), CW_ALIGN_CENTER, W);
    prompt_line(S->overtime ? "golden goal: next one wins" : "kick off the cat!", W, H * 0.4f, 18 * s);
  } else if (!split && S->phase == PH_PLAY && S->phase_ticks < 50 && S->tick > 200) {
    float f = S->phase_ticks / 50.0f;
    bold("GO!", 0, H * 0.15f, 110 * s * (1 + 0.3f * (1 - f)), rgba(1, 0.75f, 0.2f, 1 - f), CW_ALIGN_CENTER, W);
  }
  /* the goal banner */
  if (S->phase == PH_GOAL) {
    int t = S->phase_ticks;
    float in = cw_clampf(t / 10.0f, 0, 1), out = cw_clampf((200 - t) / 20.0f, 0, 1), a = in * out;
    int team = S->goal_team;
    float bounce = 1 + 0.25f * cw_clampf(1 - t / 14.0f, 0, 1);
    cw_r2d_rect(cw_rect_make(0, H * 0.06f + 70 * s, W, 118 * s), rgba(TEAM_RGB[team][0] * 0.4f, TEAM_RGB[team][1] * 0.4f, TEAM_RGB[team][2] * 0.4f, 0.4f * a));
    bold(S->scorer < 0 ? "OWN GOAL!" : "GOAL!", 0, H * 0.06f + 70 * s, 80 * s * bounce, rgba(1, 0.92f, 0.45f, a), CW_ALIGN_CENTER, W);
    if (S->scorer >= 0) {
      if (S->assister >= 0) snprintf(buf, sizeof buf, "%s scores!   assist: %s", PUG_NAMES[S->scorer], PUG_NAMES[S->assister]);
      else snprintf(buf, sizeof buf, "%s scores for the %s!", PUG_NAMES[S->scorer], team ? "Sausages" : "Biscuits");
    } else snprintf(buf, sizeof buf, "the cat bounced in off the %s", team ? "Biscuits" : "Sausages");
    text(buf, 0, H * 0.06f + 160 * s, 19 * s, rgba(1, 1, 1, a), CW_ALIGN_CENTER, W);
  }
  if (S->phase == PH_OVER) {
    float a = cw_clampf(S->phase_ticks / 15.0f, 0, 1);
    bold("FULL TIME!", 0, H * 0.32f, 84 * s, rgba(1, 0.9f, 0.4f, a), CW_ALIGN_CENTER, W);
  }
  if (G.feed_ticks > 0) { /* split screen: under the scoreboard */
    float a = cw_clampf(G.feed_ticks / 20.0f, 0, 1), fx = 0, fw = W, fy = H * 0.2f;
    if (split && nv == 3) fx = W * 0.5f, fw = W * 0.5f, fy = H * 0.5f - 28 * s;
    else if (split) fy = H * 0.5f + 30 * s;
    chip(G.feed, fx + fw * 0.5f, fy, 16 * s, a);
    text(G.feed, fx, fy, 16 * s, rgba(1, 0.85f, 0.6f, a), CW_ALIGN_CENTER, fw);
  }
  if (S->phase == PH_KICKOFF && S->tick < 200 && !split) {
    const char *how = last_device == DEV_TOUCH ? "left thumb: steer and go   right thumb: jump, boost, slide"
                    : last_device == DEV_PAD ? "RT: go   LT: back   (A): jump   (B): boost   (X): powerslide   (Y): ball cam"
                                             : "W / S: go   A / D: steer   Space: jump   Shift: boost   Ctrl: powerslide   C: ball cam";
    chip(how, W * 0.5f, H * 0.9f, 14 * s, 1);
    text(how, 0, H * 0.9f, 14 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
  }
}

static void draw_fulltime(float W, float H, float s) {
  const sim *S = &G.S;
  char buf[96];
  float in = cw_clampf(G.state_ticks / 20.0f, 0, 1);
  const char *head = S->winner == TEAM_BLUE ? "BISCUITS WIN!" : S->winner == TEAM_RED ? "SAUSAGES WIN!" : "A DRAW!";
  if (G.mode == MODE_CUP) head = S->winner == TEAM_BLUE ? "FIXTURE WON!" : "SO CLOSE...";
  bold(head, 0, H * 0.03f, 58 * s, S->winner >= 0 ? rgba(TEAM_RGB[S->winner][0] * 0.5f + 0.5f, TEAM_RGB[S->winner][1] * 0.5f + 0.5f, TEAM_RGB[S->winner][2] * 0.5f + 0.5f, in)
                                                   : rgba(1, 1, 1, in), CW_ALIGN_CENTER, W);
  snprintf(buf, sizeof buf, "%d  -  %d", S->score[0], S->score[1]);
  digits(buf, 0, H * 0.16f, 40 * s, rgba(1, 1, 1, in), CW_ALIGN_CENTER, W);
  if (G.mode == MODE_CUP && S->winner == TEAM_BLUE && G.stadium < STADIUMS) {
    snprintf(buf, sizeof buf, "%s is open!", STADIUM_TITLES[G.stadium]);
    prompt_line(buf, W, H * 0.26f, 18 * s);
  }
  /* the stats table, MVP starred */
  float tw = 560 * s, rh = 30 * s, x = (W - tw) * 0.5f, y = H * 0.36f;
  int n = 0;
  for (int i = 0; i < MAXP; i++) n += S->p[i].on;
  glass(cw_rect_make(x - 12 * s, y - 34 * s, tw + 24 * s, rh * n + 44 * s), in);
  static const char *const HEAD[6] = { "PTS", "GOALS", "ASSISTS", "SAVES", "SHOTS", "DEMOS" };
  for (int c = 0; c < 6; c++) text(HEAD[c], x + 200 * s + c * 60 * s, y - 28 * s, 12 * s, rgba(1, 0.9f, 0.6f, in), CW_ALIGN_CENTER, 60 * s);
  int order[MAXP], m = 0;
  for (int i = 0; i < MAXP; i++)
    if (S->p[i].on) order[m++] = i;
  for (int a = 0; a < m; a++)
    for (int b = a + 1; b < m; b++)
      if (S->p[order[b]].points > S->p[order[a]].points) {
        int t = order[a];
        order[a] = order[b];
        order[b] = t;
      }
  for (int r = 0; r < m; r++) {
    int i = order[r];
    const pug *u = &S->p[i];
    float yy = y + r * rh;
    pug_face(x + 16 * s, yy + rh * 0.45f, 11 * s, G.look[i].coat, u->team, false, in);
    cw_color nc = rgba(TEAM_RGB[u->team][0] * 0.45f + 0.55f, TEAM_RGB[u->team][1] * 0.45f + 0.55f, TEAM_RGB[u->team][2] * 0.45f + 0.55f, in);
    text(PUG_NAMES[i], x + 36 * s, yy + 4 * s, 17 * s, nc, CW_ALIGN_LEFT, 0);
    if (i == G.mvp) {
      star(x + 150 * s, yy + rh * 0.45f, 10 * s, rgba(1, 0.82f, 0.15f, in), G.ticks * 0.03f);
      text("MVP", x + 162 * s, yy + 8 * s, 11 * s, rgba(1, 0.85f, 0.3f, in), CW_ALIGN_LEFT, 0);
    }
    int vals[6] = { u->points, u->goals, u->assists, u->saves, u->shots, u->demos };
    for (int c = 0; c < 6; c++) {
      snprintf(buf, sizeof buf, "%d", vals[c]);
      text(buf, x + 200 * s + c * 60 * s, yy + 5 * s, 16 * s, rgba(1, 1, 1, in), CW_ALIGN_CENTER, 60 * s);
    }
  }
  if (G.state_ticks > 150) prompt_line(menu_prompt(), W, H * 0.93f, 16 * s);
}

static void draw_replay(float W, float H, float s) {
  float bar = H * 0.09f;
  cw_r2d_rect(cw_rect_make(0, 0, W, bar), rgba(0, 0, 0, 0.85f));
  cw_r2d_rect(cw_rect_make(0, H - bar, W, bar), rgba(0, 0, 0, 0.85f));
  float blink = (G.ticks / 20) % 2 ? 1 : 0.55f;
  bold("REPLAY", 24 * s, bar * 0.5f - 14 * s, 28 * s, rgba(1, 0.85f, 0.3f, blink), CW_ALIGN_LEFT, W);
  bold(menu_prompt(), 0, H - bar * 0.5f - 9 * s, 18 * s, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, W);
}

/* the graphics settings from the pause screen (cartwheel/quality_menu.h): Y,
 * G or a tap opens the page, which owns the presses while it is open (the
 * pause menu's tick leaves them alone). Render-only, so it is read in
 * update, never in the simulation; the choice is kept in the save */
static cw_rect gfx_rect(float W, float H) {
  float s = H / 540.0f;
  return cw_rect_make(W * 0.5f - 170 * s, H * 0.5f, 340 * s, 46 * s);
}
/* the sound row under it: X, H or a tap switches speakers / headphones */
static cw_rect sound_rect(float W, float H) {
  float s = H / 540.0f;
  return cw_rect_make(W * 0.5f - 130 * s, H * 0.5f + 76 * s, 260 * s, 34 * s);
}
static void sound_input(void) {
  const cw_raw_input *in = cw_input_raw();
  bool hit = in->keys_pressed[11]; /* H */
  for (int i = 0; i < CW_MAX_PADS; i++) hit |= (in->pads[i].pressed & (1u << CW_PAD_WEST)) != 0;
  cw_vec2 cs = cw_r2d_canvas_size();
  if ((in->mouse_pressed & 1) && in_rect(mouse_canvas(), sound_rect(cs.x, cs.y))) hit = true;
  if (!hit) return;
  SAVE.sound ^= 1;
  sound_apply();
  save_write();
  sfx(SFX_SELECT, 0.6f);
}
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
  sound_input();
  const cw_raw_input *in = cw_input_raw();
  bool hit = in->keys_pressed[10]; /* G */
  for (int i = 0; i < CW_MAX_PADS; i++) hit |= (in->pads[i].pressed & (1u << CW_PAD_NORTH)) != 0;
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
  return w > maxw && w > 0 ? size * maxw / w : size;
}
static void gfx_res_suffix(char *buf, size_t cap) {
  size_t n = strlen(buf);
  snprintf(buf + n, cap > n ? cap - n : 0, " \xC2\xB7 %s", cw_render_res_label());
}
static void draw_gfx(float W, float H, float s) {
  cw_rect r = gfx_rect(W, H);
  panel(r, 1, rgba(1, 1, 1, 0.6f), rgba(0.12f, 0.1f, 0.28f, 0.85f));
  char buf[64];
  snprintf(buf, sizeof buf, "GRAPHICS: %s", cw_quality_label());
  for (char *c = buf; *c; c++) *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
  gfx_res_suffix(buf, sizeof buf); /* the render resolution: the host sets it */
  float sz = fit_title(buf, 22 * s, r.w - 16 * s);
  bold(buf, r.x, r.y + 10 * s + (22 * s - sz) * 0.5f, sz, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
  bold(last_device == DEV_TOUCH ? "tap for settings" : "Y / G for settings", r.x, r.y + r.h + 6 * s, 15 * s, rgba(1, 1, 1, 0.7f), CW_ALIGN_CENTER, r.w);
}

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.04f, 0.02f, 0.1f, 0.45f));
  bold("PAUSED", 0, H * 0.22f, 64 * s, rgba(1, 0.85f, 0.3f, 1), CW_ALIGN_CENTER, W);
  static const char *const ITEMS[2] = { "RESUME", "QUIT TO TITLE" };
  for (int i = 0; i < 2; i++) {
    cw_rect r = title_button(i, W, H);
    if (G.menu == i) glow_cursor(r, s, 20, false);
    panel(r, 1, rgba(1, 1, 1, 0.8f), G.menu == i ? rgba(0.95f, 0.45f, 0.2f, 0.95f) : rgba(0.12f, 0.1f, 0.28f, 0.85f));
    bold(ITEMS[i], r.x, r.y + 12 * s, 26 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
  }
  draw_gfx(W, H, s);
  cw_rect r = sound_rect(W, H);
  panel(r, 1, rgba(1, 1, 1, 0.45f), rgba(0.12f, 0.1f, 0.28f, 0.75f));
  bold(SAVE.sound ? "SOUND: HEADPHONES" : "SOUND: SPEAKERS", r.x, r.y + 7 * s, 18 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
  bold(last_device == DEV_TOUCH ? "tap to switch" : "X / H to switch", r.x, r.y + r.h + 4 * s, 13 * s, rgba(1, 1, 1, 0.6f),
       CW_ALIGN_CENTER, r.w);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  if (DEV.tt) { /* the turntable: no HUD */
    cw_r2d_end();
    return;
  }
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  switch (G.state) {
  case GS_TITLE: draw_title(W, H, s); break;
  case GS_SHEET: draw_sheet(W, H, s); break;
  case GS_STADIUM: draw_stadium_select(W, H, s); break;
  case GS_MATCH:
    draw_hud(W, H, s);
    if (touch_controls && last_device == DEV_TOUCH && humans() == 1) draw_touch(W, H, s);
    break;
  case GS_PAUSE:
    draw_hud(W, H, s);
    draw_pause(W, H, s);
    break;
  case GS_FULLTIME: draw_fulltime(W, H, s); break;
  case GS_REPLAY: draw_replay(W, H, s); break;
  default: break;
  }
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = font_ui, .accent = rgba(1, 0.85f, 0.3f, 1) });
  cw_r2d_end();
  perf_frame(render_t0);
}

static void wav_close(void) {
  if (!A.wav) return;
  uint32_t data = A.wav_frames * 4, rate = 48000;
  uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0 };
  uint32_t riff = 36 + data, br = rate * 4;
  memcpy(h + 4, &riff, 4);
  memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &br, 4);
  h[32] = 4, h[34] = 16;
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &data, 4);
  fseek(A.wav, 0, SEEK_SET);
  fwrite(h, 1, 44, A.wav);
  fclose(A.wav);
  A.wav = NULL;
}
static void shutdown(void *u) {
  (void)u;
  wav_close();
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  cw_node_type_register(&stadium_type);
#ifdef PUGBALL_AUTOPILOT
  cw_set_raw_input_filter(autopilot, NULL);
#endif
  static cw_app_desc base = { .title = "Pugball", .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed,
                              .update = update, .render = render, .shutdown = shutdown, .save = &SAVE, .save_size = sizeof SAVE,
                              .touch_pointer = true, /* taps work the menus (the engine's primary-finger pointer) */
                              .max_substeps = 3 /* a slow frame slows the game rather than spiralling */ };
  const char *res = getenv("PUG_RES");
  if (res && strchr(res, 'x')) {
    base.width = atoi(res);
    base.height = atoi(strchr(res, 'x') + 1);
  }
  return cw_scene_app(NULL, &base);
}
