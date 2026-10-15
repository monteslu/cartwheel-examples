/* BikeMesa: a downhill bike game on cartwheel and three.c (three.js r186). A
 * 2D Box2D bicycle and rider ride hand-shaped lines through 3D worlds:
 * killer jumps, rollers, drops, flips and ridiculous crashes.
 *
 *   flow      title (the rider riding by) -> player select (a rider and a
 *             bike each; pads 2-4 join here) -> track select -> 3-2-1 -> ride
 *             (crash: the rider tumbles, back at the last checkpoint) ->
 *             finish: time, medal, tricks, best
 *   input     gamepad first, nothing past a d-pad, four face buttons, Start
 *             and Select needed ((A) pedal, (B) brake / back-pedal, (X) hop,
 *             left / right lean, (Y) + a direction poses, Select checkpoint,
 *             Start pause; assets/input.cfg), keyboard (Z X C, arrows, A S D,
 *             R, Esc), on-screen touch
 *             controls from the first frame on phones (a floating lean stick;
 *             PEDAL / BRAKE / HOP and the three held poses in an arc for the
 *             right thumb; RESET and pause apart at the top; a buzz on each
 *             press, cw_platform_vibrate; cw_platform_touch_primary), menus by
 *             mouse or tap
 *   scoring   time against the track's par (gold, silver, bronze), tricks
 *             (flips, big air, wheelies, clean landings) as points; bests
 *             persist in cw_app_desc.save, tracks open in order
 *
 * Determinism: the whole run replays identically from the input stream
 * (fixed ticks, cartwheel/math.h, Box2D). The camera eases in the fixed tick
 * and is interpolated for drawing; the debug fields name the state the
 * gates compare across hosts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/environment.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/quality.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "game.h"

#include "tracks.inc"
/* the tracks a player sees: the tuned ones in order (a dev build with
 * BIKEMESA_ALL_TRACKS set shows every track) */
static int visible_tracks(void) {
  int n = 0;
  bool all = getenv("BIKEMESA_ALL_TRACKS") != NULL;
  while (n < (int)CW_ARRAY_LEN(TRACKS) && (all || TRACKS[n].ready)) n++;
  return n;
}
#define RIDER_CHARACTER_COUNT 2
#define RIDER_BIKE_COUNT 4
static const char *const RIDER_NAMES[RIDER_CHARACTER_COUNT] = { "RIDER 1", "RIDER 2" };
static const char *const BIKE_NAMES[RIDER_BIKE_COUNT] = { "GREEN FOLDING BIKE", "RED FOLDING BIKE", "BLUE FOLDING BIKE", "GOLD FOLDING BIKE" };
int TRACK_COUNT;
const track_desc *track_get(int i) { return i >= 0 && i < (int)CW_ARRAY_LEN(TRACKS) ? &TRACKS[i] : NULL; }

#define COUNT_TICKS 180
#define CRASH_TICKS 140     /* the rider tumbles this long before the replay (2.3 s: the ragdolls are worth watching) */
#define REPLAY_BEFORE 24    /* ticks replayed before the crash */
#define REPLAY_TICKS (REPLAY_BEFORE + CRASH_TICKS) /* ticks replayed in all: the run in, and the whole crash to the respawn */
#define REPLAY_SPEED 0.33f  /* slow motion */
#define RING 256
#define LANE_TICKS 18      /* a lane change: 0.3 s across */
#define RACE_GRACE (10 * TICK_HZ) /* the race ends this long after the first rider is home */
/* tricks: points pend in the air and bank on the landing (a crash loses
 * them): a frontflip 800, a backflip 500, a double x2.5, a triple x4, a
 * quad x5.5 (an air counts at most MAX_FLIPS: the bike's spin is capped,
 * bike.c AIR_SPIN_MAX, and a long low-gravity air cannot farm more); a pose held at least POSE_MIN ticks: no-hander
 * 300, no-footer 300, superman 600 (more the longer); a pose and a flip
 * together x1.5 more. The landing scales it: PERFECT x1.5 (the frame within
 * LAND_PERFECT of the slope), CLEAN x1, SKETCHY x0.5 (past LAND_CLEAN). */
#define MAX_FLIPS 4
#define LOOP_RUN_IN 40.0f /* m: a boost strip ending this close before a loop is its run-in */
#define FRONT_POINTS 800
#define BACK_POINTS 500
#define POSE_MIN 12
#define LAND_PERFECT 0.12f
#define LAND_CLEAN 0.35f
#define SAVE_MAGIC 0x534D4B42u /* 'BKMS' */
#define SAVE_VERSION 3
#define STAR2 3000 /* a run's score for the second star */
#define STAR3 8000

save_data SAVE;

/* a rider's own race: one per player (local multiplayer); solo rides RD[0] */
typedef struct rider_t {
  int32_t crashes, checkpoint, score, flips, tricks, finished;
  int32_t landings, air_max, resets, combo, combo_max;
  /* landings by quality, landings in a pose, the trick the third star wants, this air's points, the air tricks' points banked */
  int32_t perfects, sketchy, bails, star3_trick, pending, banked;
  int32_t high_lines;         /* debug field: high routes ridden out */
  uint32_t high_done;
  float respawn_pos[2];       /* debug field: where the last respawn put the bike */
  int combo_ticks;           /* a trick inside this window chains the combo */
  float check_x, check_y;    /* where a respawn puts the bike */
  int next_check;
  int shake;
  int boost_ticks;
  /* camera: eased in the fixed tick, interpolated for the frame */
  t3_vec3 cam_prev_at, cam_at;
  float cam_prev_dist, cam_dist;
  bool cam_snap;
  cw_node cam;               /* this rider's camera (a view of its own in multiplayer) */
  /* multiplayer */
  int down_ticks;            /* crashed: ticks till the replay (or the respawn) */
  /* the wipeout replay: the ring of this rider's poses (ring_n recorded), the
   * crash's index in it, frames into the replay (0: none playing) */
  int ring_n, crash_rec, replay_t;
  bool oof;
  int32_t finish_ticks, place; /* the race clock at the line; 1st, 2nd... (0: still riding) */
  int lane_from, lane_to, lane_t; /* a lane change under way: lane_t counts up to LANE_TICKS */
  int prev_ly;
  bool lean_lock;            /* a lean held through the wipeout: ignored on the new bike till let go */
  /* render only */
  bike_pose live, replay;    /* split screen: the frame's pose, and the replay's (its own view shows that) */
  popup popups[MAX_POPUPS];
  int popup_count;
  int banner_ticks;
  char banner[48];
} rider_t;
static rider_t RD[MAX_RIDERS];
static rider_t *P = RD;
static int PL; /* the player whose controls are read (rider P's) */
static int NRID = 1; /* riders in this race (1: solo) */
static void use_rider(int i);
static float rider_z(int i);
static void set_lane_now(int lane);
static void views_setup(void);
static void camera_tick(void);
static void gfx_input(void);
static void rider_finish(void);

static struct {
  /* debug fields */
  int32_t state, track, ticks, state_ticks, ride_ticks, medal, new_best;
  int32_t stars, locked_bumps;
  int bump_tile, bump_ticks;  /* a locked tile shaking */
  int32_t replays;            /* debug field: wipeout replays shown */
  int32_t replay_ticks;       /* debug field replay.ticks: the run's ticks the last replay played (REPLAY_TICKS unless skipped) */
  float bike_pos[2], cam_pos[3];
  uint32_t state_hash;
  /* local multiplayer: pads 2-4 join on the player select */
  int32_t players;           /* riders in the next race (1 = solo) */
  int32_t slot[MAX_RIDERS];  /* each player's place in the player select (SLOT_*); P1 is always in */
  int32_t chr[MAX_RIDERS], bike[MAX_RIDERS]; /* each player's rider and bike */
  int slot_ticks[MAX_RIDERS]; /* ticks since the slot last changed */
  int32_t finish_count, race_over_ticks;
  int32_t places[MAX_RIDERS]; /* debug field: each rider's place at the line (0: not finished) */
  /* flow */
  cw_node scene, camera, sun, post;
  const track_desc *t;
  bool attract;              /* the title's ride-by: the autopilot rides */
  int select_cursor;
  bool paused;
  int pause_cursor;
  /* render only */
  /* actions */
  cw_action a_up, a_down, a_pedal, a_brake, a_back, a_fwd, a_reset, a_pause, a_hop, a_pose, a_pose_hands, a_pose_feet, a_pose_super, m_left, m_right, m_up, m_down, m_ok, m_back;
} G;

/* the replay rings: each rider's every ride / crash tick's pose (presentation only) */
static bike_pose ring[MAX_RIDERS][RING];
enum { SLOT_OUT, SLOT_RIDER, SLOT_BIKE, SLOT_READY };
static int TOUCH_HOP = -1, TOUCH_PEDAL = -1, TOUCH_BRAKE = -1, TOUCH_STICK = -1, TOUCH_RESET = -1, TOUCH_PAUSE = -1, TOUCH_POSE[3] = { -1, -1, -1 };

/* ── autopilot (bikemesa_auto): a session played with no one at the pads ──
 * It presses (A) through each screen as it comes up, however many frames a
 * host spends loading first, and pedals the race. BIKEMESA_PLAYERS (2-4, a
 * native run's environment) has pads 2-4 join on the player select and race
 * too: the split screen without anyone at the pads. */
#ifdef BIKEMESA_AUTOPILOT
static int auto_players(void) {
  const char *e = getenv("BIKEMESA_PLAYERS");
  return e ? CW_CLAMP(atoi(e), 1, MAX_RIDERS) : 1;
}
static void autopilot_filter(cw_raw_input *in, uint64_t frame, void *user) {
  (void)user, (void)frame;
  memset(in->pads, 0, sizeof in->pads);
  memset(in->keys, 0, sizeof in->keys);
  memset(in->keys_pressed, 0, sizeof in->keys_pressed);
  in->mouse_buttons = in->mouse_pressed = 0;
  int phase = G.state_ticks % 20;
  uint32_t tap = phase == 12 || phase == 13 ? 1u << CW_PAD_SOUTH : 0; /* (A) down for two ticks every 20 */
  for (int p = 0; p < auto_players(); p++) {
    in->pads[p].connected = true;
    switch (G.state) {
    case GS_TITLE:
    case GS_SELECT: in->pads[p].buttons = p == 0 ? tap : 0; break; /* P1 starts and picks the first track */
    case GS_CHARACTER: in->pads[p].buttons = tap; break;          /* everyone joins, picks and is ready */
    case GS_COUNT:
    case GS_RIDE:
    case GS_CRASH:
    case GS_REPLAY:
    case GS_FINISH: in->pads[p].buttons = 1u << CW_PAD_SOUTH; break; /* pedal (held: it skips no replay) */
    default: break;
    }
  }
}
#endif

/* ── audio ──────────────────────────────────────────────────────────── */

static const char *const SFX_FILES[SFX_COUNT] = {
  "audio/pedal.wav", "audio/land.wav", "audio/crash.wav", "audio/thud.wav", "audio/check.wav", "audio/finish.wav",
  "audio/trick.wav", "audio/select.wav", "audio/locked.wav", "audio/count.wav", "audio/go.wav", "audio/boost.wav",
  "audio/whoosh.wav", "audio/star.wav", "audio/slowcrash.wav" };
static struct {
  bool on;
  cw_bus music_bus, sfx_bus;
  cw_res sfx_res[SFX_COUNT], music_res;
  cw_sound sfx[SFX_COUNT];
  int cooldown[SFX_COUNT];
  cw_voice music;
  const char *music_name;
} A;

void game_sfx(sfx_id id, float volume) {
  if (!A.on || A.cooldown[id] > 0) return;
  A.cooldown[id] = 5;
  cw_audio_play(A.sfx[id], &(cw_play_desc){ .bus = A.sfx_bus, .volume = volume });
}

static void audio_init(void) {
  A.on = getenv("BIKEMESA_NO_AUDIO") == NULL && cw_audio_init(&(cw_audio_desc){ 0 });
  if (!A.on) return;
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.38f);
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1, 0.1f } });
  for (int i = 0; i < SFX_COUNT; i++) {
    A.sfx_res[i] = cw_res_load(SFX_FILES[i], 0);
    A.sfx[i] = cw_res_audio_sound(A.sfx_res[i], &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = 3, .bus = A.sfx_bus });
  }
}

/* each track's own loop (audio/<name>.ogg), crossfaded when it changes */
static void music_play(const char *name) {
  if (!A.on || (A.music_name && !strcmp(A.music_name, name))) return;
  if (A.music_name) cw_voice_stop(A.music, 1.0f);
  if (A.music_res.id) cw_res_release(A.music_res);
  char path[64];
  snprintf(path, sizeof path, "audio/%s.ogg", name);
  A.music_res = cw_res_load(path, 0);
  cw_sound s = cw_res_audio_sound(A.music_res, &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.music_bus });
  A.music = cw_audio_play(s, &(cw_play_desc){ .loop = true, .fade_in = 1.5f, .bus = A.music_bus });
  A.music_name = name;
}

/* ── input devices: whichever was used last drives the prompts ─────── */

enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
static int last_device = DEV_MOUSE;
static int32_t input_mode;
static bool device_used;
static uint32_t prev_mouse_buttons;
static bool mouse_clicked;

static void read_devices(void) {
  const cw_raw_input *in = cw_input_raw();
  int before = last_device;
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse_buttons & 1);
  prev_mouse_buttons = in->mouse_buttons;
  if (mouse_clicked) last_device = in->mouse_from_touch ? DEV_TOUCH : DEV_MOUSE;
  for (int i = 0; i < CW_MAX_TOUCHES; i++)
    if (in->touches[i].active) last_device = DEV_TOUCH;
  for (int i = 0; i < CW_MAX_PADS; i++) {
    const cw_pad_state *p = &in->pads[i];
    if (!p->connected) continue;
    if (p->buttons || fabsf(p->axes[CW_AXIS_LX]) > 0.3f || fabsf(p->axes[CW_AXIS_LY]) > 0.3f || p->axes[CW_AXIS_RT] > 0.3f ||
        p->axes[CW_AXIS_LT] > 0.3f)
      last_device = DEV_PAD;
  }
  for (int k = 0; k < CW_KEY_COUNT; k++)
    if (in->keys_pressed[k]) {
      last_device = DEV_KEYS;
      break;
    }
  if (last_device != before) device_used = true;
  /* a phone or tablet shows its touch controls from the first frame */
  if (!device_used) last_device = cw_platform_touch_primary() ? DEV_TOUCH : DEV_MOUSE;
  input_mode = last_device;
}

static cw_vec2 mouse_canvas(void) {
  const cw_raw_input *in = cw_input_raw();
  return cw_r2d_window_to_canvas(cw_v2(in->mouse_x, in->mouse_y));
}
static bool in_rect(cw_vec2 m, cw_rect r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }
enum { PAUSE_RESUME, PAUSE_CHECKPOINT, PAUSE_RESTART, PAUSE_TRACKS, PAUSE_ITEMS };
static cw_rect pause_rect(int i, float W, float H, float s) { return cw_rect_make(W * 0.5f - 150 * s, H * 0.27f + i * 50 * s, 300 * s, 44 * s); }

/* ── save data ──────────────────────────────────────────────────────── */

static uint32_t save_sum(void) {
  const uint8_t *b = (const uint8_t *)&SAVE.tr;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof SAVE.tr; i++) h = (h ^ b[i]) * 16777619u;
  return h ^ (SAVE.version | (uint32_t)SAVE.size << 16);
}
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC;
  SAVE.version = SAVE_VERSION;
  SAVE.size = (uint16_t)sizeof SAVE;
  SAVE.tr[0].shown = 1;
  SAVE.checksum = save_sum();
}
static void save_write(void) {
  SAVE.checksum = save_sum();
  cw_save_commit();
}
/* the player's graphics preset (cartwheel/quality.h) rides in the last track
 * record's stars byte, a slot no track uses, under the checksum */
#define QUALITY_SAVE SAVE.tr[BIKEMESA_TRACKS_MAX - 1].stars
static void save_check(void) {
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) {
    if (SAVE.magic) cw_warn("bikemesa: save data rejected (magic %08x version %u), starting fresh", SAVE.magic, SAVE.version);
    save_reset();
  }
  for (int i = 0; i < BIKEMESA_TRACKS_MAX; i++)
    if (SAVE.tr[i].medal > 3) SAVE.tr[i].medal = 3;
}
static bool track_open(int i) { return i == 0 || (i < TRACK_COUNT && SAVE.tr[i - 1].cleared); }
static void save_preset(int n) {
  save_reset();
  for (int i = 0; i < n && i < BIKEMESA_TRACKS_MAX; i++) {
    SAVE.tr[i].cleared = 1;
    SAVE.tr[i].medal = 1;
    SAVE.tr[i].shown = 1;
  }
  save_write();
}
static int32_t dbg_unlocked;
static void save_fields(void) {
  dbg_unlocked = 0;
  for (int i = 0; i < TRACK_COUNT; i++) dbg_unlocked += track_open(i);
}

/* ── popups and banners ─────────────────────────────────────────────── */

static void popup_add(const char *text, int points) {
  if (P->popup_count == MAX_POPUPS) {
    memmove(P->popups, P->popups + 1, sizeof P->popups[0] * (MAX_POPUPS - 1));
    P->popup_count--;
  }
  popup *p = &P->popups[P->popup_count++];
  snprintf(p->text, sizeof p->text, "%s", text);
  p->points = points;
  p->age = 0;
  p->x = bike_get()->x;
  p->y = bike_get()->y + 2.2f;
  if (points > 0 && !G.attract) P->score += points;
}
/* a trick: tricks landed within 3 s of each other chain a combo
 * multiplier (x2 .. x5); a crash breaks it */
#define COMBO_WINDOW 180
static void trick(const char *text, int base) {
  P->combo = P->combo_ticks > 0 ? (P->combo < 5 ? P->combo + 1 : 5) : 1;
  P->combo_ticks = COMBO_WINDOW;
  if (P->combo > P->combo_max && !G.attract) P->combo_max = P->combo;
  P->tricks += !G.attract;
  char s[40];
  if (P->combo > 1) snprintf(s, sizeof s, "%s  x%d", text, P->combo);
  else snprintf(s, sizeof s, "%s", text);
  popup_add(s, base * P->combo);
}
/* the flips an air counts: whole turns (within 0.9 rad), at most MAX_FLIPS */
static int count_flips(float rot) { return CW_MIN((int)((fabsf(rot) + 0.9f) / 6.2831853f), MAX_FLIPS); }
/* an air's points so far (and its name): rot the rotation (+ back), pose ticks per POSE_* */
static int air_points(float rot, const int pose[4], char *name, size_t n) {
  int flips = count_flips(rot);
  float mult = flips == 1 ? 1 : flips == 2 ? 2.5f : flips == 3 ? 4 : 5.5f;
  int fp = flips ? (int)((rot < 0 ? FRONT_POINTS : BACK_POINTS) * mult) : 0, pp = 0, best = 0;
  static const int POSE_PTS[4] = { 0, 300, 300, 600 };
  static const char *const POSE_NAME[4] = { "", "NO-HANDER", "NO-FOOTER", "SUPERMAN" };
  for (int k = 1; k < 4; k++)
    if (pose[k] >= POSE_MIN) {
      pp += (int)(POSE_PTS[k] * cw_clampf(pose[k] / 30.0f, 1, 2));
      if (!best || pose[k] > pose[best]) best = k;
    }
  int total = fp + pp;
  if (fp && pp) total = (int)(total * 1.5f);
  if (name) {
    char flip[24] = "";
    if (flips == 1) snprintf(flip, sizeof flip, "%s", rot < 0 ? "FRONTFLIP" : "BACKFLIP");
    else if (flips > 1) snprintf(flip, sizeof flip, "%s %s", flips == 2 ? "DOUBLE" : flips == 3 ? "TRIPLE" : "QUAD", rot < 0 ? "FRONTFLIP" : "BACKFLIP");
    snprintf(name, n, "%s%s%s", best ? POSE_NAME[best] : "", best && flips ? " " : "", flip);
  }
  return total;
}
static void banner(const char *s, int ticks) {
  snprintf(P->banner, sizeof P->banner, "%s", s);
  P->banner_ticks = ticks;
}

/* ── tracks ─────────────────────────────────────────────────────────── */

static bool find_nodes(cw_node n, void *user) {
  (void)user;
  if (CW_HANDLE_IS_NULL(G.camera) && cw_node_is(n, "Camera3D")) G.camera = n;
  if (CW_HANDLE_IS_NULL(G.sun) && cw_node_is(n, "DirectionalLight3D")) G.sun = n;
  if (CW_HANDLE_IS_NULL(G.post) && cw_node_is(n, "PostProcess")) G.post = n;
  return true;
}

static void minimap_build(void);
static float sun_extent, sun_shown; /* the scene's sun's shadow_extent, and the one split screen widened it to (to take in the field) */
/* index: the track; attract: no scoring (the title's ride-by, the previews);
 * riders: a bike each (local multiplayer), 1 solo */
static bool load_track(int index, bool attract, int riders) {
  const track_desc *next_track = track_get(index);
  cw_node next_scene = cw_scene_instance_path(next_track->scene);
  if (CW_HANDLE_IS_NULL(next_scene)) {
    cw_error("bikemesa: scene '%s' failed to load", next_track->scene);
    return false;
  }
  cw_tree *t = cw_scene_app_tree();
  if (cw_node_valid(G.scene)) cw_node_free(G.scene);
  view_forget();
  models_forget();
  bike_world_destroy();
  G.camera = G.sun = G.post = (cw_node){ 0 };
  G.t = next_track;
  G.track = index + 1;
  G.scene = next_scene;
  cw_node_add_child(cw_tree_root(t), G.scene);
  cw_node_walk(G.scene, find_nodes, NULL);
  music_play(G.t->music);
  bike_world_create(G.t); /* first: the scenery asks it for the ground's height */
  view_build(G.scene, G.t, index);
  crash_world_build(G.t); /* after the scenery: a crash tumbles against the bank and the rocks it placed */
  for (int i = 0; i < MAX_RIDERS; i++) models_set_selection(i, G.chr[i], G.bike[i]);
  models_build(G.scene);
  NRID = CW_CLAMP(riders, 1, MAX_RIDERS);
  memset(RD, 0, sizeof RD);
  memset(G.places, 0, sizeof G.places);
  G.finish_count = G.race_over_ticks = 0;
  bike_set_riders(NRID);
  for (int i = 0; i < NRID; i++) {
    use_rider(i);
    if (i) models_build_rider(G.scene, i);
    P->check_x = 2;
    P->check_y = 0;
    P->next_check = 0;
    set_lane_now(NRID > 1 ? i : getenv("BIKEMESA_LANE") ? atoi(getenv("BIKEMESA_LANE")) : 1); /* solo: the second lane from the back */
    bike_spawn(P->check_x, P->check_y, attract ? 4 : 0);
    P->cam_snap = true;
  }
  use_rider(0);
  G.attract = attract;
  G.ride_ticks = 0;
  G.medal = G.new_best = 0;
  G.stars = 0;
  G.paused = false;
  sun_extent = 0;
  cw_value ext;
  if (cw_node_valid(G.sun) && cw_node_get_prop(G.sun, "shadow_extent", &ext)) sun_extent = (float)ext.f;
  sun_shown = sun_extent;
  minimap_build();
  views_setup();
  if (NRID > 1) { /* each view's camera on its rider from the first frame */
    for (int i = 0; i < NRID; i++) use_rider(i), camera_tick();
    use_rider(0);
  }
  cw_info("bikemesa: track %d %s (%s)", index + 1, G.t->name, G.t->theme);
  return true;
}

static void enter(int state) {
  G.state = state;
  G.state_ticks = 0;
}

static int joined_count(void) {
  int n = 0;
  for (int p = 0; p < MAX_RIDERS; p++) n += p == 0 || G.slot[p] != SLOT_OUT;
  return n;
}
/* the player select's preview: each player's rider and bike standing on the
 * start straight, a few metres apart, a view each when more than one plays */
#define PREVIEW_GAP 7.0f
static void preview_load(void) {
  load_track(0, true, joined_count());
  for (int i = 0; i < NRID; i++) {
    use_rider(i);
    bike_spawn(2 + i * PREVIEW_GAP, 0, 0);
    const bike_state *b = bike_get();
    P->cam_at = P->cam_prev_at = (t3_vec3){ b->x + 0.1f, b->y + (NRID > 1 ? 0.55f : 0.9f), rider_z(i) };
    P->cam_dist = P->cam_prev_dist = 4.2f;
    P->cam_snap = true;
  }
  use_rider(0);
}

static void start_track(int index) {
  if (load_track(index, false, G.players)) enter(GS_COUNT);
}

static bool lane_free(int lane, float x, float room);
/* a lane a bike can be put down in at x: open from just behind it on past the next narrow's taper */
static bool lane_spawnable(int lane, float x) {
  for (float ax = -1; ax <= NARROW_TAPER; ax += 0.5f) /* nor closing ahead (the cones' fair warning is behind it) */
    if (!track_lane_open(G.t, x + ax, lane)) return false;
  return true;
}
static void respawn(void) {
  /* back on in the rider's lane, or the nearest one that is open there (a
   * lane that has closed behind a pinch ends in a barrier) and, in a race,
   * free of other riders */
  int pick = -1;
  for (int pass = 0; pass < 2 && pick < 0; pass++)
    for (int d = 0; d < LANES && pick < 0; d++)
      for (int sg = 1; sg >= -1 && pick < 0; sg -= 2) {
        int l = P->lane_to + d * sg;
        if (l < 0 || l >= LANES || !lane_spawnable(l, P->check_x)) continue;
        if (pass == 0 && NRID > 1 && !lane_free(l, P->check_x, 2.5f)) continue;
        pick = l;
      }
  if (pick >= 0 && pick != P->lane_to) set_lane_now(pick);
  P->lean_lock = true;
  bike_spawn(P->check_x, P->check_y, 4);
  P->respawn_pos[0] = P->check_x;
  P->respawn_pos[1] = P->check_y;
  P->cam_snap = false;
}

static int32_t dbg_hops, dbg_loops, dbg_pose, dbg_poses, dbg_quality;

/* ── local multiplayer: the riders, their lanes and views ────────────────
 * Pads 2-4 join on the player select with (A) and leave with (B); the
 * keyboard (and touch) is P1's. Each rider has a bike in the one
 * world on a lane of its own across the track (a collision layer: riders in
 * one lane bump, in different lanes pass through), a camera and a view of
 * the screen, a HUD in it, and an audio listener. Up / down change lane on
 * the ground (a short slide across); not in the air. */
static int32_t dbg_lanes[MAX_RIDERS], dbg_bumps, dbg_lane_changes;
static float dbg_race_x[MAX_RIDERS];
static int32_t dbg_race_crashes[MAX_RIDERS];
static void use_rider(int i) {
  P = &RD[i];
  bike_select(i);
  PL = i;
}
/* where rider i is drawn across the track: its lane, or between two while changing */
static float rider_z(int i) {
  const rider_t *r = &RD[i];
  int keep = (int)(P - RD);
  bike_select(i);
  float shift = bike_loop_shift(); /* round a loop: two lanes across, with the loop */
  bike_select(keep);
  if (shift > 0) return lane_z(r->lane_to + 2 * shift);
  if (r->lane_t <= 0) return lane_z((float)r->lane_to);
  float u = cw_smoothstep(0, 1, r->lane_t / (float)LANE_TICKS);
  return lane_z(r->lane_from + (r->lane_to - r->lane_from) * u);
}
static float rider_x(int i) {
  int keep = (int)(P - RD);
  bike_select(i);
  float x = bike_get()->x;
  bike_select(keep);
  return x;
}
/* no other rider in the lane within room metres of x */
static bool lane_free(int lane, float x, float room) {
  for (int j = 0; j < NRID; j++)
    if (&RD[j] != P && RD[j].lane_to == lane && fabsf(rider_x(j) - x) < room) return false;
  return true;
}
static void set_lane_now(int lane) {
  P->lane_from = P->lane_to = lane;
  P->lane_t = 0;
  bike_set_lane(lane);
}
/* up / down: the next lane back / forward, on the ground, with room there */
static void lane_input(void) {
  const bike_state *b = bike_get();
  int ly = cw_action_pressed(PL, G.a_up) ? -1 : cw_action_pressed(PL, G.a_down) ? 1 : 0; /* up: a lane further back */
  if (cw_action_pressed(PL, G.a_pose)) ly = 0; /* (Y) + up is a pose */
  bool fresh = ly != 0 && ly != P->prev_ly;
  P->prev_ly = ly;
  if (!fresh || P->lane_t > 0 || !b->alive) return;
  if (!b->grounded_rear && !b->grounded_front) return; /* not in the air */
  int to = P->lane_to + ly;
  bool closed = false; /* a lane that is shut here, or shuts just ahead */
  for (float ax = 0; ax <= 3; ax += 1) closed |= !track_lane_open(G.t, b->x + ax, to);
  for (float dx = -0.6f; dx <= 0.8f; dx += 0.35f) /* not sideways into a ramp's flank: onto it only at its foot (off it is fine: you drop) */
    if (track_ramp_h(G.t, b->x + dx, to) > track_ramp_h(G.t, b->x + dx, P->lane_to) + 0.12f) closed = true;
  for (int l = 0; l < G.t->loop_count; l++) /* not round a loop */
    if (fabsf(b->x - G.t->loops[l * 3]) < G.t->loops[l * 3 + 2] + 1) closed = true;
  if (to < 0 || to >= LANES || closed || !lane_free(to, b->x, 1.6f)) {
    game_sfx(SFX_LOCKED, 0.4f);
    return;
  }
  P->lane_from = P->lane_to;
  P->lane_to = to;
  P->lane_t = 1;
  bike_set_lane(to);
  dbg_lane_changes++;
  game_sfx(SFX_WHOOSH, 0.5f);
}

/* a rider whose lane shuts ahead moves toward an open one in time: the
 * autopilot's riders (bikemesa_auto), and a dev aid (BIKEMESA_AUTOLANE) */
static void auto_lane(void) {
#ifndef BIKEMESA_AUTOPILOT
  if (!getenv("BIKEMESA_AUTOLANE")) return;
#endif
  if (P->lane_t > 0) return;
  const bike_state *b = bike_get();
  if (!b->alive || (!b->grounded_rear && !b->grounded_front)) return;
  int l = P->lane_to;
  bool shut = false;
  for (float ax = 0; ax <= 30; ax += 2) shut |= !track_lane_open(G.t, b->x + ax, l);
  for (int k = 0; k < G.t->loop_count; k++)
    if (fabsf(b->x - G.t->loops[k * 3]) < G.t->loops[k * 3 + 2] + 2) shut = false; /* round the loop: stay */
  if (!shut) return;
  for (int d = 1; d < LANES; d++)
    for (int sg = -1; sg <= 1; sg += 2) {
      int to = l + d * sg;
      if (to < 0 || to >= LANES) continue;
      bool ok = true;
      for (float ax = 0; ax <= 30; ax += 2) ok &= track_lane_open(G.t, b->x + ax, to);
      if (!ok) continue;
      int step = sg;
      if (!lane_free(l + step, b->x, 1.6f) || !track_lane_open(G.t, b->x, l + step)) continue;
      P->lane_from = l;
      P->lane_to = l + step;
      P->lane_t = 1;
      bike_set_lane(l + step);
      return;
    }
}

/* the race order: home riders by place, then the rest by distance along the track */
static int race_order(int *order) {
  for (int i = 0; i < NRID; i++) order[i] = i;
  for (int a = 1; a < NRID; a++)
    for (int k = a; k > 0; k--) {
      int i = order[k], j = order[k - 1];
      bool before = RD[i].place && RD[j].place ? RD[i].place < RD[j].place
                  : RD[i].place || RD[j].place  ? RD[i].place != 0
                                                 : rider_x(i) > rider_x(j) + 1e-4f;
      if (!before) break;
      order[k] = j, order[k - 1] = i;
    }
  return NRID;
}
static const char *ordinal(int n) { return n == 1 ? "1ST" : n == 2 ? "2ND" : n == 3 ? "3RD" : "4TH"; }

static void rider_finish(void) {
  P->finished = 1;
  P->finish_ticks = G.ride_ticks;
  P->place = ++G.finish_count;
  G.places[P - RD] = P->place;
  char b[32];
  snprintf(b, sizeof b, "%s!", ordinal(P->place));
  banner(b, 120);
  game_sfx(P->place == 1 ? SFX_FINISH : SFX_CHECK, 1);
}

/* the race ends once every rider is home, or RACE_GRACE after the first; the rest are placed by distance */
static void race_check_end(void) {
  if (G.state != GS_RIDE || G.finish_count == 0) return;
  if (G.finish_count < NRID && ++G.race_over_ticks < RACE_GRACE) return;
  int order[MAX_RIDERS];
  race_order(order);
  for (int k = 0; k < NRID; k++)
    if (!RD[order[k]].place) RD[order[k]].place = G.places[order[k]] = ++G.finish_count;
  enter(GS_FINISH);
}

/* a view per rider: two stacked (the track runs across, so wide views),
 * four as quadrants; three take the left two and the bottom right, leaving
 * the top right to the race's shared HUD and the mini map */
static void views_setup(void) {
  cw_pipeline *pl = cw_pipeline_of(cw_scene_app_tree());
  RD[0].cam = G.camera;
  /* split screen: the post chain past the views' own passes (bloom, tone
   * mapping, the grade, anti-aliasing) runs once over the whole screen */
  cw_pipeline_set_views_shared_post(pl, NRID > 1);
  if (NRID > 1 && cw_node_valid(G.post)) { /* and what is left per view, scaled to small views (each track load sets the scene's own again) */
    cw_node_set(G.post, "ssao", cw_val_bool(false)); /* contact shadows too fine to see at this size */
    static const char *const LENS[] = { "vignette", "grain", "chromatic_aberration" }; /* a lens darkens a screen's corners, not a view's */
    for (int k = 0; k < 3; k++) cw_node_set(G.post, LENS[k], cw_val_bool(false));
  }
  if (NRID < 2 || !cw_node_valid(G.camera)) {
    cw_pipeline_set_views(pl, NULL, 0);
    return;
  }
  cw_view v[CW_MAX_VIEWS];
  for (int i = 0; i < NRID; i++) {
    if (i) {
      char name[24];
      snprintf(name, sizeof name, "Camera%d", i + 1);
      cw_node c = cw_node_new("Camera3D", name);
      cw_node_add_child(G.scene, c);
      static const char *const PROPS[] = { "fov", "near", "far" };
      for (int k = 0; k < 3; k++) {
        cw_value val;
        if (cw_node_get_prop(G.camera, PROPS[k], &val)) cw_node_set(c, PROPS[k], val);
      }
      RD[i].cam = c;
    }
    v[i].camera = RD[i].cam;
    static const float Q3[3][2] = { { 0, 0 }, { 0, 0.5f }, { 0.5f, 0.5f } };
    if (NRID == 2) v[i] = (cw_view){ RD[i].cam, 0, i * 0.5f, 1, 0.5f };
    else if (NRID == 3) v[i] = (cw_view){ RD[i].cam, Q3[i][0], Q3[i][1], 0.5f, 0.5f };
    else v[i] = (cw_view){ RD[i].cam, (i & 1) * 0.5f, (i >> 1) * 0.5f, 0.5f, 0.5f };
  }
  cw_pipeline_set_views(pl, v, NRID);
}

static bool players_set; /* the players command chose the riders: the pads' joins stand aside */

/* ── debug channel ──────────────────────────────────────────────────── */

static int pending_track = -1;
/* respawn_sweep [ticks] [raw]: a test's check that no checkpoint loops a respawn */
static bool sweeping;         /* the sweep is riding: no dust, no wind (presentation) */
static int pending_sweep = 0; /* ticks each respawn must ride clean (0: none asked) */
static bool sweep_raw;        /* held leans reach the new bike (the gate's control) */
static int32_t dbg_sweep_cases, dbg_sweep_fails, dbg_sweep_fail[5]; /* the first failure: track, checkpoint, lane, inputs, tick */
static void cmd_respawn_sweep(int argc, const char **argv, cw_reply *r) {
  pending_sweep = argc > 1 ? CW_CLAMP(atoi(argv[1]), 1, 600) : 120;
  sweep_raw = argc > 2 && !strcmp(argv[2], "raw");
  cw_reply_printf(r, "{\"ticks\":%d}", pending_sweep);
}
static void cmd_track(int argc, const char **argv, cw_reply *r) {
  int n = argc > 1 ? atoi(argv[1]) : 0;
  if (n < 1 || n > TRACK_COUNT) {
    cw_reply_error(r, "usage: track <1..n>");
    return;
  }
  pending_track = n - 1;
  cw_reply_printf(r, "{\"track\":%d}", n);
}
/* players n: riders in the next race (a test's way in to 3 and 4; riders without a pad idle) */
static void cmd_players(int argc, const char **argv, cw_reply *r) {
  if (argc > 1) {
    int n = CW_CLAMP(atoi(argv[1]), 1, MAX_RIDERS);
    for (int p = 1; p < MAX_RIDERS; p++) G.slot[p] = p < n ? SLOT_READY : SLOT_OUT;
    G.players = n;
    players_set = true;
  }
  cw_reply_printf(r, "{\"players\":%d}", G.players);
}
static void cmd_progress(int argc, const char **argv, cw_reply *r) {
  if (argc >= 3 && !strcmp(argv[1], "preset")) save_preset(atoi(argv[2]));
  save_fields();
  cw_reply_printf(r, "{\"unlocked\":%d,\"checksum\":%u}", dbg_unlocked, SAVE.checksum);
}

/* ── the camera ─────────────────────────────────────────────────────── */

static void camera_tick(void) {
  const bike_state *b = bike_get();
  float speed = sqrtf(b->vx * b->vx + b->vy * b->vy);
  bool air = !b->grounded_rear && !b->grounded_front;
  /* look ahead along the motion, pull back with speed and in big air */
  t3_vec3 at = { b->x + cw_clampf(b->vx * 0.32f, -3, 6), b->y + 1.1f + cw_clampf(b->vy * 0.08f, -1.5f, 1.5f), 0 };
  /* a crash: the camera follows the tumbling rider, across the track and off it too */
  t3_vec3 down_at;
  if (bike_crash_focus(&down_at, NULL)) at.z = down_at.z;
  float dist = 7.2f + cw_clampf(speed * 0.22f, 0, 4.5f) + (air ? cw_clampf(b->air_ticks * 0.025f, 0, 3.0f) : 0);
  if (getenv("BIKEMESA_CAM_CLOSE")) { /* dev aid: a close look at the bike */
    dist = 2.6f;
    at = (t3_vec3){ b->x + 0.1f, b->y + 0.95f, 0 };
  }
  if (G.state == GS_TITLE || G.state == GS_SELECT) {
    at.y += 0.4f;
    dist = 8.5f;
  }
  if (G.state == GS_CHARACTER) {
    at = (t3_vec3){ b->x + 0.1f, b->y + (NRID > 1 ? 0.55f : 0.9f), rider_z((int)(P - RD)) }; /* split: the rider higher, clear of the labels */
    dist = 4.2f;
  }
  if (G.state == GS_FINISH) dist = 8;
  P->cam_prev_at = P->cam_at;
  P->cam_prev_dist = P->cam_dist;
  if (P->cam_snap) {
    P->cam_at = at;
    P->cam_dist = dist;
    P->cam_prev_at = at;
    P->cam_prev_dist = dist;
    P->cam_snap = false;
  } else {
    P->cam_at.x += (at.x - P->cam_at.x) * 0.12f;
    P->cam_at.y += (at.y - P->cam_at.y) * 0.08f;
    P->cam_at.z += (at.z - P->cam_at.z) * 0.08f;
    P->cam_dist += (dist - P->cam_dist) * 0.04f;
  }
  if (P->shake > 0) P->shake--;
  if (P != RD) return; /* the first rider's camera is the debug field; the race places the sun itself */
  G.cam_pos[0] = P->cam_at.x;
  G.cam_pos[1] = P->cam_at.y;
  G.cam_pos[2] = P->cam_dist;
  if (NRID > 1) return;
  /* the sun's shadow camera follows the rider: its light runs along the node's
   * -Z, so it sits back up the sunbeam from the rider */
  if (cw_node_valid(G.sun)) {
    const t3_mat4 *m = cw_node3d_global_transform(G.sun);
    t3_vec3 z = { m->e[8], m->e[9], m->e[10] };
    cw_node3d_set_position(G.sun, (t3_vec3){ b->x + 4 + z.x * 60, b->y + z.y * 60, z.z * 60 });
  }
}

/* drawing: the eased camera between the last two ticks, a little shake */
static void camera_frame(double alpha) {
  cw_node cam = NRID > 1 ? P->cam : G.camera;
  if (!cw_node_valid(cam)) return;
  float a = (float)alpha;
  t3_vec3 at = { P->cam_prev_at.x + (P->cam_at.x - P->cam_prev_at.x) * a, P->cam_prev_at.y + (P->cam_at.y - P->cam_prev_at.y) * a,
                 P->cam_prev_at.z + (P->cam_at.z - P->cam_prev_at.z) * a };
  float d = P->cam_prev_dist + (P->cam_dist - P->cam_prev_dist) * a;
  t3_vec3 eye = { at.x - d * 0.16f, at.y + d * 0.36f, at.z + d }; /* tilted down a little: the four lanes read */
  if (getenv("BIKEMESA_CAM_34")) eye = (t3_vec3){ at.x - d * 0.75f, at.y + d * 0.5f, d * 0.7f }; /* dev aid: a three-quarter view from ahead */
  if (getenv("BIKEMESA_CAM_TOP")) eye = (t3_vec3){ at.x, at.y + 16, 0.5f }; /* dev aid: from above */
  if (P->shake > 0) {
    float k = 0.01f * P->shake;
    eye.x += k * cw_sin(G.ticks * 1.9f);
    eye.y += k * cw_sin(G.ticks * 2.7f + 1);
  }
  cw_node3d_set_position(cam, eye);
  cw_node3d_look_at(cam, at, (t3_vec3){ 0, 1, 0 });
}

/* the replay: rider i's ring of poses at a fractional tick */
static void replay_pose(int i, float f, bike_pose *out) {
  const rider_t *r = &RD[i];
  int lo = CW_MAX(r->crash_rec - REPLAY_BEFORE, 0), hi = r->ring_n - 1;
  float c = cw_clampf(f, (float)lo, (float)hi);
  int k = (int)floorf(c);
  float t = c - k;
  const bike_pose *a = &ring[i][k % RING], *b = &ring[i][(k + 1 <= hi ? k + 1 : k) % RING];
  *out = *b;
  out->frame = cw_xform2_lerp(a->frame, b->frame, t);
  out->rear = cw_xform2_lerp(a->rear, b->rear, t);
  out->front = cw_xform2_lerp(a->front, b->front, t);
  out->cap = cw_xform2_lerp(a->cap, b->cap, t);
  out->z = a->z + (b->z - a->z) * t;
  if (a->ragdoll != b->ragdoll) return;
  if (b->ragdoll) {
    for (int k2 = 0; k2 < RAG_COUNT; k2++) out->limb3[k2] = cw_xform3_lerp(a->limb3[k2], b->limb3[k2], t);
    out->frame3 = cw_xform3_lerp(a->frame3, b->frame3, t);
    out->rear3 = cw_xform3_lerp(a->rear3, b->rear3, t);
    out->front3 = cw_xform3_lerp(a->front3, b->front3, t);
    out->cap3 = cw_xform3_lerp(a->cap3, b->cap3, t);
  } else
    for (int k2 = 0; k2 < RAG_COUNT; k2++) out->limb[k2] = cw_xform2_lerp(a->limb[k2], b->limb[k2], t);
}
/* where the replay is: the run's tick, and how far through it is (0..1) */
static float replay_tick(const rider_t *r, double alpha) {
  return (float)(r->crash_rec - REPLAY_BEFORE) + ((float)r->replay_t + (float)alpha) * REPLAY_SPEED;
}
static float replay_u(const rider_t *r, double alpha) {
  return cw_clampf(((float)r->replay_t + (float)alpha) * REPLAY_SPEED / REPLAY_TICKS, 0, 1);
}
/* a low dolly beside the tumble, a short orbit, then a quick push in on the
 * head at the worst moment */
static void replay_camera(cw_node cam, const bike_pose *p, float u) {
  if (!cw_node_valid(cam)) return;
  /* the torso and the head: in the crash world once the rider is off, else the riding pose at its depth */
  t3_vec3 c = p->ragdoll ? p->limb3[RAG_TORSO].p : (t3_vec3){ p->limb[RAG_TORSO].p.x, p->limb[RAG_TORSO].p.y, p->z };
  t3_vec3 h = p->ragdoll ? p->limb3[RAG_HEAD].p : (t3_vec3){ p->limb[RAG_HEAD].p.x, p->limb[RAG_HEAD].p.y, p->z };
  float push = cw_smoothstep(0.55f, 0.75f, u) * (1 - cw_smoothstep(0.9f, 1.0f, u));
  float yaw = -0.7f + 1.3f * cw_smoothstep(0, 0.6f, u);
  float d = 3.6f - 1.3f * push;
  t3_vec3 at = { c.x + (h.x - c.x) * push, c.y + (h.y - c.y) * push, c.z + (h.z - c.z) * push };
  float sy, cy;
  cw_sincos(yaw, &sy, &cy);
  t3_vec3 eye = { at.x + sy * d, at.y + 0.25f - 0.15f * push, at.z + cy * d };
  cw_node3d_set_position(cam, eye);
  cw_node3d_look_at(cam, at, (t3_vec3){ 0, 1, 0 });
}

/* split screen: before each view draws, its own rider is drawn in full (two
 * players; in a quarter of the screen it too is far) and the others in their
 * far look (each view draws every rider: this keeps it cheap); a rider whose wipeout replays is posed in the replay in its own
 * view and where it really is in the others */
static void view_hook(int view, void *u) {
  (void)u;
  for (int i = 0; i < NRID; i++) models_far(i, i != view || NRID > 2); /* a quarter of the screen: every rider far */
  for (int i = 0; i < NRID; i++)
    if (RD[i].replay_t > 0) models_sync_rider(i, i == view ? &RD[i].replay : &RD[i].live, i == view ? RD[i].replay.z : RD[i].live.z);
}

static void frame_hook(cw_tree *t, double alpha, void *u) {
  (void)t;
  (void)u;
  bike_pose p;
  if (G.state == GS_REPLAY) {
    replay_pose(0, replay_tick(P, alpha), &p);
    models_sync(&p);
    view_frame((float)alpha);
    replay_camera(G.camera, &p, replay_u(P, alpha));
    view_grass(&G.camera, 1);
    return;
  }
  if (NRID > 1) {
    cw_node cams[MAX_RIDERS];
    for (int i = 0; i < NRID; i++) {
      use_rider(i);
      bike_pose_get((float)alpha, &P->live);
      P->live.z = rider_z(i);
      models_sync_rider(i, &P->live, P->live.z);
      if (G.state == GS_CHARACTER) models_turntable(i, -1.45f + G.state_ticks * 0.014f, P->live.frame.p.x + 0.05f);
      if (P->replay_t > 0) {
        replay_pose(i, replay_tick(P, alpha), &P->replay);
        replay_camera(P->cam, &P->replay, replay_u(P, alpha));
      } else camera_frame(alpha);
      cams[i] = P->cam;
    }
    use_rider(0);
    view_frame((float)alpha);
    view_grass(cams, NRID);
    return;
  }
  bike_pose_get((float)alpha, &p);
  models_sync_rider(0, &p, rider_z(0));
  if (G.state == GS_CHARACTER) models_turntable(0, -1.45f + G.state_ticks * 0.014f, p.frame.p.x + 0.05f);
  view_frame((float)alpha);
  camera_frame(alpha);
  view_grass(&G.camera, 1);
}

/* ── the app ────────────────────────────────────────────────────────── */

static cw_font font_ui, font_ui_bold, font_title, font_num;

static void bind_touch(cw_action a, int id) {
  char b[24];
  snprintf(b, sizeof b, "touch:%d", id);
  cw_action_bind_text(a, b);
}

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

/* the touch controls, in window coordinates. Left thumb: a floating lean
 * stick anywhere in the lower left. Right thumb, in an arc round its rest:
 * a big PEDAL in the corner, BRAKE inside it, a big HOP above it and the
 * three poses (held) further round; RESET small and apart at the top right,
 * PAUSE at the top left. Nothing smaller than 56 px, and the small ones are 82 or more so a
 * phone held sideways (the 960 x 540 canvas at ~0.7x) still shows 56. */
typedef struct touch_btn { int *id; const char *label; int dir; float x, y, w, h; cw_color c; } touch_btn;
static int touch_table(float W, float H, touch_btn *out) {
  float k = H / 540.0f;
#define SZ(v) fmaxf((v) * k, 56.0f)
  float pw = SZ(160), ph = SZ(168), bw = SZ(124), bh = SZ(112), hw = SZ(160), hh = SZ(104), ps = SZ(84), sm = SZ(82);
  float px = W - pw - 16 * k, py = H - ph - 16 * k;
  touch_btn t[] = {
    { &TOUCH_PEDAL, "PEDAL", 0, px, py, pw, ph, rgba(0.35f, 0.95f, 0.45f, 1) },
    { &TOUCH_BRAKE, "BRAKE", 1, px - bw - 12 * k, H - bh - 16 * k, bw, bh, rgba(1, 0.35f, 0.3f, 1) },
    { &TOUCH_HOP, "HOP", 2, px, py - hh - 12 * k, hw, hh, rgba(1, 0.85f, 0.3f, 1) },
    /* the poses round the arc, from above BRAKE up to the left of HOP */
    { &TOUCH_POSE[2], "SUPER", -2, px - ps - 14 * k, py - hh - 12 * k, ps, ps, rgba(0.75f, 0.5f, 1, 1) },
    { &TOUCH_POSE[0], "NO HANDS", -2, px - 2 * ps - 24 * k, py - ps * 0.85f, ps, ps, rgba(0.75f, 0.5f, 1, 1) },
    { &TOUCH_POSE[1], "NO FEET", -2, px - bw - ps - 26 * k, H - bh - 16 * k - ps * 0.45f, ps, ps, rgba(0.75f, 0.5f, 1, 1) },
    { &TOUCH_RESET, "RESET", -1, W - sm - 16 * k, 16 * k, sm, sm, rgba(1, 0.8f, 0.3f, 1) },
    { &TOUCH_PAUSE, "II", -1, 16 * k, 16 * k, sm, sm, rgba(1, 1, 1, 1) },
  };
#undef SZ
  memcpy(out, t, sizeof t);
  return (int)CW_ARRAY_LEN(t);
}

static void touch_layout(void) {
  int ww, wh;
  cw_window_size(&ww, &wh);
  float W = (float)ww, H = (float)wh, k = H / 540.0f;
  if (TOUCH_PEDAL < 0) {
    TOUCH_STICK = cw_touch_add_stick(0, 0, 1, 1, 60 * k, true);
    TOUCH_BRAKE = cw_touch_add_button(0, 0, 1, 1);
    TOUCH_PEDAL = cw_touch_add_button(0, 0, 1, 1);
    TOUCH_RESET = cw_touch_add_button(0, 0, 1, 1);
    TOUCH_PAUSE = cw_touch_add_button(0, 0, 1, 1);
    TOUCH_HOP = cw_touch_add_button(0, 0, 1, 1);
    for (int i = 0; i < 3; i++) TOUCH_POSE[i] = cw_touch_add_button(0, 0, 1, 1);
    bind_touch(G.a_hop, TOUCH_HOP);
    char b[48];
    /* a small dead zone round the stick's centre: a resting thumb does not lean */
    snprintf(b, sizeof b, "touch:%d.left, deadzone:0.18", TOUCH_STICK);
    cw_action_bind_text(G.a_back, b);
    snprintf(b, sizeof b, "touch:%d.right, deadzone:0.18", TOUCH_STICK);
    cw_action_bind_text(G.a_fwd, b);
    /* and up / down on it change lane */
    snprintf(b, sizeof b, "touch:%d.up, deadzone:0.55", TOUCH_STICK);
    cw_action_bind_text(G.a_up, b);
    snprintf(b, sizeof b, "touch:%d.down, deadzone:0.55", TOUCH_STICK);
    cw_action_bind_text(G.a_down, b);
    bind_touch(G.a_brake, TOUCH_BRAKE);
    bind_touch(G.a_pedal, TOUCH_PEDAL);
    bind_touch(G.a_reset, TOUCH_RESET);
    bind_touch(G.a_pose_hands, TOUCH_POSE[0]);
    bind_touch(G.a_pose_feet, TOUCH_POSE[1]);
    bind_touch(G.a_pose_super, TOUCH_POSE[2]);
  }
  /* the stick takes a touch anywhere in the lower left (it centres where the thumb lands) */
  cw_touch_set_rect(TOUCH_STICK, 0, H * 0.3f, W * 0.42f, H * 0.7f);
  touch_btn t[12];
  int n = touch_table(W, H, t);
  for (int i = 0; i < n; i++) cw_touch_set_rect(*t[i].id, t[i].x, t[i].y, t[i].w, t[i].h);
}

/* a short buzz as a touch button goes down (navigator.vibrate on a phone) */
static void touch_haptics(void) {
  static bool was[12];
  touch_btn t[12];
  int ww, wh;
  cw_window_size(&ww, &wh);
  int n = touch_table((float)ww, (float)wh, t);
  for (int i = 0; i < n; i++) {
    bool on = cw_touch_control_active(*t[i].id);
    if (on && !was[i] && last_device == DEV_TOUCH) cw_platform_vibrate(12);
    was[i] = on;
  }
}

/* the graphics preset changed: thin or thicken the grass */
static void quality_changed(cw_quality q, void *u) {
  (void)q, (void)u;
  view_density(cw_quality_density());
}

static void init(void *u) {
  (void)u;
  TRACK_COUNT = visible_tracks();
  save_check();
  cw_quality_restore(QUALITY_SAVE); /* the player's graphics preset, if they chose one */
  cw_quality_on_change(quality_changed, NULL);
  view_density(cw_quality_density());
#ifdef BIKEMESA_PRESET
  save_preset(BIKEMESA_PRESET);
#endif
  save_fields();
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("bikemesa: input.cfg had errors (see above)");
  cw_input_commit_defaults();
  G.a_pedal = cw_action_find("pedal");
  G.a_brake = cw_action_find("brake");
  G.a_back = cw_action_find("lean_back");
  G.a_fwd = cw_action_find("lean_fwd");
  G.a_reset = cw_action_find("reset");
  G.a_hop = cw_action_find("hop");
  G.a_pose = cw_action_find("pose");
  G.a_pose_hands = cw_action_find("pose_hands");
  G.a_pose_feet = cw_action_find("pose_feet");
  G.a_pose_super = cw_action_find("pose_superman");
  G.a_up = cw_action_find("dir_up");
  G.a_down = cw_action_find("dir_down");
  G.a_pause = cw_action_find("pause");
  G.m_left = cw_action_find("m_left");
  G.m_right = cw_action_find("m_right");
  G.m_up = cw_action_find("m_up");
  G.m_down = cw_action_find("m_down");
  G.m_ok = cw_action_find("m_ok");
  G.m_back = cw_action_find("m_back");
  touch_layout();
  audio_init();
  /* a 10 px distance range at the 48 px bake: room for the HUD's outlines
   * (up to 0.11 of the size on small text) and the titles' shadow */
  const cw_font_desc fd = { .mode = CW_FONT_SDF, .sdf_padding = 10 };
  font_ui = cw_font_load("fonts/Fredoka-SemiBold.ttf", &fd);
  font_ui_bold = cw_font_load("fonts/Fredoka-Bold.ttf", &fd);
  font_title = cw_font_load("fonts/Bungee-Regular.ttf", &fd);
  font_num = cw_font_load("fonts/LilitaOne-Regular.ttf", &fd);

  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.track", &G.track);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("ride.ticks", &G.ride_ticks);
  cw_debug_i32("crashes", &P->crashes);
  cw_debug_i32("checkpoint", &P->checkpoint);
  cw_debug_i32("score", &P->score);
  cw_debug_i32("flips", &P->flips);
  cw_debug_i32("tricks", &P->tricks);
  cw_debug_i32("landings", &P->landings);
  cw_debug_i32("air.max", &P->air_max);
  cw_debug_i32("resets", &P->resets);
  cw_debug_i32("combo.max", &P->combo_max);
  cw_debug_i32("perfects", &P->perfects);
  cw_debug_i32("sketchy", &P->sketchy);
  cw_debug_i32("bails", &P->bails);
  cw_debug_i32("trick.banked", &P->banked);
  cw_debug_i32("star3.trick", &P->star3_trick);
  cw_debug_i32("stars", &G.stars);
  cw_debug_i32("select.locked_bumps", &G.locked_bumps);
  cw_debug_i32("replays", &G.replays);
  cw_debug_i32("replay.ticks", &G.replay_ticks);
  cw_debug_i32("high.lines", &P->high_lines);
  cw_debug_i32("hops", &dbg_hops);
  cw_debug_i32("loops", &dbg_loops);
  cw_debug_i32("pose", &dbg_pose);
  cw_debug_i32("poses", &dbg_poses);
  cw_debug_i32("players", &G.players);
  cw_debug_i32("gfx.quality", &dbg_quality);
  cw_debug_field("race.places", G.places, CW_DBG_I32, MAX_RIDERS);
  cw_debug_field("race.lanes", dbg_lanes, CW_DBG_I32, MAX_RIDERS);
  cw_debug_i32("race.bumps", &dbg_bumps);
  cw_debug_f32_array("race.x", dbg_race_x, MAX_RIDERS);
  cw_debug_field("race.crashes", dbg_race_crashes, CW_DBG_I32, MAX_RIDERS);
  cw_debug_i32("race.lane_changes", &dbg_lane_changes);

  cw_debug_f32_array("respawn.pos", P->respawn_pos, 2);
  cw_debug_i32("medal", &G.medal);
  cw_debug_i32("new_best", &G.new_best);
  cw_debug_i32("finished", &P->finished);
  cw_debug_i32("input.mode", &input_mode);
  cw_debug_f32_array("bike.pos", G.bike_pos, 2);
  cw_debug_f32_array("cam.pos", G.cam_pos, 3);
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  cw_debug_u32("wind.hash", &wind_hash);
  cw_debug_i32("save.unlocked", &dbg_unlocked);
  cw_debug_bytes("save.bytes", &SAVE, sizeof SAVE);
  cw_debug_command("track", "track <1..n>: ride a track now", cmd_track);
  cw_debug_command("respawn_sweep", "respawn_sweep [ticks] [raw]: respawn at every checkpoint of every track, in every lane, with inputs held through the wipeout; each must ride clean for ticks (sweep.*)", cmd_respawn_sweep);
  cw_debug_i32("sweep.cases", &dbg_sweep_cases);
  cw_debug_i32("sweep.fails", &dbg_sweep_fails);
  cw_debug_field("sweep.fail", dbg_sweep_fail, CW_DBG_I32, 5);
  cw_debug_command("players", "players <1..4>: riders in the next race (split screen)", cmd_players);
  cw_debug_command("progress", "progress [show | preset <n>]: the save data", cmd_progress);
  cw_tree_add_hook(cw_scene_app_tree(), 2, 0, frame_hook, NULL);
  G.state_hash = CW_HASH32_INIT;
  G.players = 1;
  for (int p = 0; p < MAX_RIDERS; p++) G.bike[p] = p;
  cw_pipeline_set_view_hook(cw_pipeline_of(cw_scene_app_tree()), view_hook, NULL);
  load_track(0, true, 1);
  enter(GS_TITLE);
  const char *st = getenv("BIKEMESA_START_TRACK"); /* dev aid; the gates go through the menus */
  if (st && atoi(st) >= 1 && atoi(st) <= (int)CW_ARRAY_LEN(TRACKS)) start_track(atoi(st) - 1);
}

/* the title's autopilot: pedal steadily, keep the frame level */
static bike_input autopilot(void) {
  const bike_state *b = bike_get();
  bike_input in = { .pedal = 0.7f };
  float slope = 0;
  bool f1, f2;
  float y1 = track_height_at(b->x + 0.6f, &f1), y0 = track_height_at(b->x - 0.4f, &f2);
  if (f1 && f2) slope = cw_atan2(y1 - y0, 1.0f);
  float err = cw_wrap_angle(b->angle - slope);
  in.lean = fabsf(err) < 0.15f ? 0 : cw_clampf(err * 2.5f, -1, 1);
  if (b->speed > 9) in.pedal = 0, in.brake = 0.3f;
  return in;
}

static float act(cw_action a) { return cw_action_strength(PL, a); }

/* the pose held: (Y) + a direction pressed picks it (left no-hands, right
 * no-feet, up superman) and it holds while (Y) does; LB / RB / both and the
 * keys A / S / D pick and hold it directly */
static int pose_held_p[MAX_RIDERS];
#define pose_held pose_held_p[PL]
static int read_pose(void) {
  bool hands = cw_action_pressed(PL, G.a_pose_hands), feet = cw_action_pressed(PL, G.a_pose_feet);
  if (cw_action_pressed(PL, G.a_pose_super) || (hands && feet)) return POSE_SUPERMAN;
  if (hands) return POSE_NO_HANDS;
  if (feet) return POSE_NO_FEET;
  if (!cw_action_pressed(PL, G.a_pose)) return pose_held = POSE_NONE;
  if (!pose_held) { /* the direction pressed with (Y), or already held as (Y) goes down */
    bool fresh = cw_action_just_pressed(PL, G.a_pose);
    if (cw_action_just_pressed(PL, G.m_left) || (fresh && cw_action_pressed(PL, G.m_left))) pose_held = POSE_NO_HANDS;
    else if (cw_action_just_pressed(PL, G.m_right) || (fresh && cw_action_pressed(PL, G.m_right))) pose_held = POSE_NO_FEET;
    else if (cw_action_just_pressed(PL, G.a_up) || (fresh && cw_action_pressed(PL, G.a_up))) pose_held = POSE_SUPERMAN;
  }
  return pose_held;
}

/* (A) pedals, (B) brakes, left / right lean on the ground and in the air alike */
static bike_input player_input(void) {
  bike_input in = { 0 };
  in.pedal = act(G.a_pedal);
  in.brake = act(G.a_brake);
  in.lean = act(G.a_fwd) - act(G.a_back);
  in.hop = cw_action_just_pressed(PL, G.a_hop);
  in.pose = (uint8_t)read_pose();
  if (PL == 0) {
    if (in.pose && !dbg_pose) dbg_poses++;
    dbg_pose = in.pose;
  }
  return in;
}
#undef pose_held

/* the controls on a bike just put back on: a lean still held from before the
 * wipeout (a stick or d-pad never let go through the replay) does nothing till
 * it is let go, so the new bike is not leaned straight over again */
static bike_input held_through(bike_input in) {
  if (P->lean_lock) {
    if (fabsf(in.lean) < 0.1f) P->lean_lock = false;
    else in.lean = 0;
  }
  return in;
}

static void finish_track(void) {
  P->finished = 1;
  int idx = G.track - 1;
  float secs = G.ride_ticks / (float)TICK_HZ;
  int par = G.t->par;
  G.medal = secs <= par ? 3 : secs <= par * 1.25f ? 2 : 1;
  /* the time bonus: every second under twice the par is 100 */
  int bonus = (int)cw_clampf((par * 2 - secs) * 100, 0, 1e6f);
  P->score += bonus;
  save_track *s = &SAVE.tr[idx];
  G.new_best = s->best_ticks == 0 || G.ride_ticks < s->best_ticks;
  if (G.new_best) s->best_ticks = G.ride_ticks;
  if (P->score > s->best_score) s->best_score = P->score;
  if (G.medal > s->medal) s->medal = (uint8_t)G.medal;
  /* the third star wants the score and a PERFECT frontflip or a pose and a flip in one air */
  G.stars = 1 + (P->score >= STAR2) + (P->score >= STAR3 && P->star3_trick);
  if (G.stars > s->stars) s->stars = (uint8_t)G.stars;
  s->cleared = 1;
  save_write();
  save_fields();
  game_sfx(SFX_FINISH, 1);
  enter(GS_FINISH);
}


/* the selected rider's part of a tick before the world steps: the high routes, the boost */
static bool ride_pre(const bike_input *inp) {
  bike_input in = *inp;
  const bike_state *before = bike_get();
  bool boosting = false;
  /* the high routes: a bonus for riding the deck out */
  for (int i = 0; i < G.t->high_count; i++) {
    const float *h = &G.t->highs[i * 4];
    float x0 = h[0], x1 = h[1], hy = h[2] + (h[3] - h[2]) * cw_clampf((before->x - x0) / (x1 - x0), 0, 1);
    bool on = before->alive && before->x > x0 && before->x < x1 && before->y > hy - 0.3f;
    if (on && before->x > x1 - 0.6f && !(P->high_done & (1u << i)) && !G.attract) {
      P->high_done |= 1u << i;
      P->high_lines++;
      trick("HIGH LINE", 1500);
    }
  }
  for (int i = 0; i < G.t->boost_count; i++)
    if (before->x >= G.t->boosts[i * 2] && before->x <= G.t->boosts[i * 2 + 1] && before->alive) {
      /* a strip on a loop's run-in only boosts pedalling: the loop wants a
       * committed run-up (coasting or braking into it drops off the top) */
      bool run_in = false;
      for (int l = 0; l < G.t->loop_count; l++)
        if (G.t->loops[l * 3] > G.t->boosts[i * 2 + 1] && G.t->loops[l * 3] - G.t->boosts[i * 2 + 1] < LOOP_RUN_IN) run_in = true;
      if (!run_in || in.pedal > 0.05f) boosting = true;
    }
  if (boosting && P->boost_ticks == 0) game_sfx(SFX_BOOST, 0.8f);
  P->boost_ticks = boosting ? P->boost_ticks + 1 : 0;
  return boosting;
}

/* and after: crashes, landings and their tricks, checkpoints, the line */
static void ride_post(void) {
  const bike_state *b = bike_get();
  if (bike_lane() != P->lane_to) { /* a loop moved the rider across */
    P->lane_from = P->lane_to = bike_lane();
    P->lane_t = 0;
  }
  if (bike_crashed_this_tick()) {
    P->crashes += !G.attract;
    if (bike_bailed_this_tick() && !G.attract) P->bails++;
    if (P->pending > 0 && !G.attract) popup_add("LOST", 0);
    P->combo = P->combo_ticks = 0;
    game_sfx(SFX_CRASH, 1);
    P->shake = 24;
    fx_burst(b->x, b->y, 0);
    if (!G.attract) banner("WIPEOUT!", 90);
    P->crash_rec = P->ring_n;
    if (NRID > 1) P->down_ticks = CRASH_TICKS; /* split screen: the tumble, then the replay in the rider's own view */
    else if (G.state == GS_RIDE) enter(GS_CRASH);
  }
  float rot;
  int air;
  if (bike_landed_this_tick(&rot, &air)) {
    P->landings++;
    if (air > P->air_max) P->air_max = air;
    int flips = count_flips(rot);
    if (air > 12) {
      game_sfx(SFX_LAND, cw_clampf(air / 60.0f, 0.3f, 1));
      fx_burst(b->x - 0.4f, b->y - 0.3f, 1);
      P->shake = air > 50 ? 10 : 4;
    }
    float err;
    int pose[4];
    bike_landing_more(&err, pose);
    int pts = air_points(rot, pose, NULL, 0);
    if (pts > 0) {
      /* the landing: the frame against the slope as it came down */
      float e = fabsf(err), q = e < LAND_PERFECT ? 1.5f : e < LAND_CLEAN ? 1 : 0.5f;
      const char *how = e < LAND_PERFECT ? "PERFECT" : e < LAND_CLEAN ? "CLEAN" : "SKETCHY";
      char name[48], s[64];
      air_points(rot, pose, name, sizeof name);
      snprintf(s, sizeof s, "%s  %s", name, how);
      int before = P->score;
      trick(s, (int)(pts * q));
      P->banked += P->score - before;
      P->flips += flips;
      P->perfects += e < LAND_PERFECT && !G.attract;
      P->sketchy += e >= LAND_CLEAN && !G.attract;
      bool posed = pose[1] + pose[2] + pose[3] >= POSE_MIN;
      if (!G.attract && ((flips > 0 && rot < 0 && e < LAND_PERFECT) || (flips > 0 && posed))) P->star3_trick = 1;
      game_sfx(SFX_TRICK, e < LAND_CLEAN ? 1 : 0.6f);
    } else if (air > 70) {
      trick("BIG AIR", 250 + (air - 70) * 5);
      game_sfx(SFX_TRICK, 0.7f);
    }
  }
  /* this air's points so far, pending till the landing */
  if (b->alive && !b->grounded_rear && !b->grounded_front) {
    int pose[4];
    bike_air_poses(pose);
    P->pending = air_points(b->air_rot, pose, NULL, 0);
  } else P->pending = 0;
  if (b->wheelie_ticks == 90) {
    trick("WHEELIE", 300);
  }
  if (b->wheelie_ticks > 90 && b->wheelie_ticks % 60 == 0) trick("WHEELIE+", 150);
  if (P->combo_ticks > 0 && --P->combo_ticks == 0) P->combo = 0;
  /* checkpoints and the line */
  if (b->alive)
    while (P->next_check < G.t->check_count && b->x >= G.t->checks[P->next_check * 2]) {
      P->check_x = G.t->checks[P->next_check * 2];
      P->check_y = G.t->checks[P->next_check * 2 + 1];
      P->next_check++;
      if (!G.attract) {
        P->checkpoint = P->next_check;
        banner("CHECKPOINT", 70);
        game_sfx(SFX_CHECK, 0.9f);
      }
    }
  if (b->alive && b->x >= G.t->finish_x) {
    if (G.attract) {
      P->check_x = 2, P->check_y = 0, P->next_check = 0;
      respawn();
      P->cam_snap = true;
    } else if (NRID > 1) {
      if (!P->finished && G.state == GS_RIDE) rider_finish();
    } else if (G.state == GS_RIDE) finish_track();
  }
  if (P != RD || sweeping) return; /* the dust and the scenery's wind follow the first rider (not a test's sweep: nothing is drawn) */
  bool ground = b->grounded_rear;
  fx_dust(b->x - 0.42f, track_height_at(b->x - 0.42f, NULL), b->speed);
  view_tick(b->x, b->y, fabsf(b->speed), ground && b->alive);
}

/* each rider's drawn depth (its lane, easing across, or round a loop) to the
 * sim: a crash starts from where the rider is drawn */
static void bike_depths(void) {
  int keep = (int)(P - RD);
  for (int i = 0; i < NRID; i++) {
    float z = rider_z(i);
    bike_select(i);
    bike_set_z(z);
  }
  bike_select(keep);
}

static void ride_tick(bike_input in) {
  bool boosting = ride_pre(&in);
  bike_depths();
  bike_step(&in, boosting);
  ride_post();
}

/* every track, every checkpoint (the start too), every lane the rider could
 * have been in, and the inputs a player may still be holding as the replay
 * ends (pedal, brake, a lean, together): back on the bike, the rider must ride
 * `ticks` without a crash. Then the title again. */
static void respawn_sweep(int ticks, bool raw) {
  static const bike_input held[] = {
    { 0 }, { .pedal = 1 }, { .brake = 1 }, { .lean = -1 }, { .lean = 1 },
    { .pedal = 1, .lean = -1 }, { .pedal = 1, .lean = 1 }, { .brake = 1, .lean = -1 },
  };
  dbg_sweep_cases = dbg_sweep_fails = 0;
  sweeping = true;
  memset(dbg_sweep_fail, 0, sizeof dbg_sweep_fail);
  for (int t = 0; t < TRACK_COUNT; t++) {
    load_track(t, false, G.players);
    enter(GS_RIDE);
    for (int c = 0; c <= G.t->check_count; c++)
      for (int l = 0; l < LANES; l++)
        for (int k = 0; k < (int)CW_ARRAY_LEN(held); k++) {
          P->check_x = c ? G.t->checks[(c - 1) * 2] : 2;
          P->check_y = c ? G.t->checks[(c - 1) * 2 + 1] : 0;
          P->next_check = c;
          set_lane_now(l);
          respawn();
          enter(GS_RIDE);
          int crash = -1;
          for (int i = 0; i < ticks && crash < 0; i++) {
            ride_tick(raw ? held[k] : held_through(held[k]));
            if (bike_crashed_this_tick()) crash = i;
          }
          dbg_sweep_cases++;
          if (crash < 0) continue;
          if (!dbg_sweep_fails++) {
            int32_t f[5] = { t + 1, c, l, k, crash };
            memcpy(dbg_sweep_fail, f, sizeof f);
          }
          cw_info("bikemesa: respawn sweep: track %d checkpoint %d lane %d inputs %d: crashed after %d ticks at x %.2f", t + 1, c, l, k, crash, bike_get()->x);
        }
  }
  sweeping = false;
  cw_info("bikemesa: respawn sweep: %d respawns, %d crashed", dbg_sweep_cases, dbg_sweep_fails);
  load_track(0, true, 1);
  enter(GS_TITLE);
}

static void menu_select(void) {
  int n = TRACK_COUNT, cols = 4;
  int c = G.select_cursor;
  if (cw_action_just_pressed(0, G.m_left) && c % cols > 0) c--;
  if (cw_action_just_pressed(0, G.m_right) && c % cols < cols - 1 && c + 1 < n) c++;
  if (cw_action_just_pressed(0, G.m_up) && c >= cols) c -= cols;
  if (cw_action_just_pressed(0, G.m_down) && c + cols < n) c += cols;
  if (c != G.select_cursor) game_sfx(SFX_SELECT, 0.5f);
  G.select_cursor = c;
}

static cw_rect tile_rect(int i, float W, float H) {
  float s = H / 540.0f, tw = 200 * s, th = 86 * s, gap = 12 * s;
  int cols = 4;
  float x0 = (W - cols * tw - (cols - 1) * gap) * 0.5f, y0 = H * 0.21f;
  return cw_rect_make(x0 + (i % cols) * (tw + gap), y0 + (i / cols) * (th + gap), tw, th);
}

static cw_rect choice_rect(int i, float W, float H) {
  float s = H / 540.0f;
  return cw_rect_make(W * 0.5f + (i ? 220 : -460) * s, H * 0.48f, 240 * s, 56 * s);
}

static cw_rect confirm_rect(float W, float H) {
  float s = H / 540.0f;
  return cw_rect_make(W * 0.5f + 240 * s, H * 0.67f, 180 * s, 50 * s);
}

static void pick_track(int i) {
  if (!track_open(i)) {
    game_sfx(SFX_LOCKED, 1);
    G.locked_bumps++;
    G.bump_tile = i;
    G.bump_ticks = 24;
    return;
  }
  game_sfx(SFX_SELECT, 1);
  start_track(i);
}

/* split screen: the rider down. The tumble (down_ticks), then its slow
 * motion replay in the rider's own view while the others ride on (any of
 * (A), pedal or checkpoint skips it), then back on at the checkpoint */
static void rider_replay(void) {
  if (P->down_ticks > 0) {
    if (--P->down_ticks > 0) return;
    if (G.attract || P->ring_n - (P->crash_rec - REPLAY_BEFORE) > RING) {
      respawn();
      return;
    }
    P->replay_t = 1;
    P->oof = false;
    game_sfx(SFX_SLOWCRASH, 0.7f);
    return;
  }
  if (P->replay_t <= 0) return;
  P->replay_t++;
  bool skip = P->replay_t > 6 && (cw_action_just_pressed(PL, G.m_ok) || cw_action_just_pressed(PL, G.a_reset) || cw_action_just_pressed(PL, G.a_pedal));
  if (!P->oof && P->replay_t * REPLAY_SPEED >= REPLAY_BEFORE) {
    P->oof = true;
    banner("OOF!", 80);
    game_sfx(SFX_THUD, 0.8f);
  }
  if (skip || P->replay_t * REPLAY_SPEED >= REPLAY_TICKS) {
    G.replays++;
    P->replay_t = 0;
    P->banner_ticks = 0;
    respawn();
    P->cam_snap = true;
  }
}

/* a multiplayer tick: every rider's controls (or the count's feet down, or
 * coasting home), one world step for all, then each rider's tricks,
 * checkpoints, line, respawn, lane slide and camera */
static void mp_tick(void) {
  bike_input in[MAX_RIDERS];
  bool boost[MAX_RIDERS];
  if (G.state == GS_RIDE) G.ride_ticks++;
  for (int i = 0; i < NRID; i++) {
    use_rider(i);
    const bike_state *b = bike_get();
    if (G.state == GS_COUNT) in[i] = (bike_input){ .hold = true };
    else if (P->finished || G.state != GS_RIDE) in[i] = (bike_input){ .brake = 1, .hold = b->speed < 0.6f };
    else if (!b->alive) in[i] = (bike_input){ 0 };
    else {
      in[i] = held_through(player_input());
      lane_input();
      auto_lane();
      if (cw_action_just_pressed(PL, G.a_reset)) {
        P->resets++;
        respawn();
      }
    }
    boost[i] = ride_pre(&in[i]);
  }
  bike_depths();
  bike_step_all(in, boost);
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < NRID; i++) {
    use_rider(i);
    ride_post();
    if (P->replay_t == 0) { /* the replay ring: every tick but the replay's own */
      bike_pose_get(1, &ring[i][P->ring_n % RING]);
      P->ring_n++;
    }
    rider_replay();
    if (P->lane_t > 0 && ++P->lane_t > LANE_TICKS) P->lane_t = 0;
    camera_tick();
    dbg_lanes[i] = P->lane_to;
    dbg_race_x[i] = bike_get()->x;
    dbg_race_crashes[i] = P->crashes;
    x0 = fminf(x0, bike_get()->x), x1 = fmaxf(x1, bike_get()->x);
    y0 = fminf(y0, bike_get()->y), y1 = fmaxf(y1, bike_get()->y);
  }
  use_rider(0);
  race_check_end();
  dbg_bumps = bike_bumps();
  /* one shadow map for every view (drawn once a frame): the sun's shadow
   * camera sits over the middle of the field and widens to take it all in,
   * in steps so the map is not redrawn at a new size every tick */
  if (cw_node_valid(G.sun)) {
    const t3_mat4 *m = cw_node3d_global_transform(G.sun);
    t3_vec3 z = { m->e[8], m->e[9], m->e[10] };
    float sx = (x0 + x1) * 0.5f, sy = (y0 + y1) * 0.5f;
    cw_node3d_set_position(G.sun, (t3_vec3){ sx + 4 + z.x * 60, sy + z.y * 60, z.z * 60 });
    if (sun_extent > 0) {
      float want = fmaxf(sun_extent, ceilf((fmaxf(x1 - x0, y1 - y0) * 0.5f + 10) / 4) * 4);
      want = fminf(want, sun_extent * 3);
      if (want != sun_shown) cw_node_set(G.sun, "shadow_extent", cw_val_float(want)), sun_shown = want;
    }
  }
}

/* the player select: each player picks a rider, then a bike, then is ready;
 * pads 2-4 join with (A) and leave with (B) from the rider pick, P1's (B)
 * there goes back to the title. P1 has the keyboard, and alone on the
 * screen the mouse and taps too. Everyone ready: P1 picks the track. */
static void slot_set(int p, int st) {
  G.slot[p] = st;
  G.slot_ticks[p] = 0;
}
static void players_tick(cw_vec2 m, cw_vec2 cs, bool ok1) {
  bool changed = false;
  for (int p = 0; p < MAX_RIDERS; p++) {
    G.slot_ticks[p]++;
    if (p > 0) {
      if (players_set) continue; /* the players command chose them */
      int pad = cw_input_player_pad(p);
      if (pad < 0 || !cw_input_raw()->pads[pad].connected) {
        if (G.slot[p] != SLOT_OUT) slot_set(p, SLOT_OUT), changed = true;
        continue;
      }
    }
    bool ok = p == 0 ? ok1 : cw_action_just_pressed(p, G.m_ok), back = cw_action_just_pressed(p, G.m_back);
    int step = cw_action_just_pressed(p, G.m_right) - cw_action_just_pressed(p, G.m_left);
    if (p == 0 && NRID == 1 && mouse_clicked) {
      if (in_rect(m, choice_rect(0, cs.x, cs.y))) step = -1;
      else if (in_rect(m, choice_rect(1, cs.x, cs.y))) step = 1;
      else if (in_rect(m, confirm_rect(cs.x, cs.y))) ok = true;
    }
    ok = ok && G.slot_ticks[p] > 10;
    switch (G.slot[p]) {
    case SLOT_OUT:
      if (ok) {
        slot_set(p, SLOT_RIDER);
        G.bike[p] = p; /* each player's own colour to begin with */
        changed = true;
        game_sfx(SFX_SELECT, 0.7f);
      }
      break;
    case SLOT_RIDER:
      if (step) {
        G.chr[p] = (G.chr[p] + step + RIDER_CHARACTER_COUNT) % RIDER_CHARACTER_COUNT;
        changed = true;
        game_sfx(SFX_SELECT, 0.5f);
      } else if (ok) {
        slot_set(p, SLOT_BIKE);
        if (p == 0) G.bike[0] = G.chr[0] == 1 ? 1 : 0, changed = true; /* P1's riders each have a bike of their own */
        game_sfx(SFX_SELECT, 1);
      } else if (back && p == 0) {
        for (int q = 1; q < MAX_RIDERS; q++)
          if (!players_set) G.slot[q] = SLOT_OUT;
        load_track(0, true, 1);
        enter(GS_TITLE);
        return;
      } else if (back) {
        slot_set(p, SLOT_OUT);
        changed = true;
        game_sfx(SFX_LOCKED, 0.5f);
      }
      break;
    case SLOT_BIKE:
      if (step) {
        G.bike[p] = (G.bike[p] + step + RIDER_BIKE_COUNT) % RIDER_BIKE_COUNT;
        changed = true;
        game_sfx(SFX_SELECT, 0.5f);
      } else if (ok) {
        slot_set(p, SLOT_READY);
        game_sfx(SFX_SELECT, 1);
      } else if (back) slot_set(p, SLOT_RIDER);
      break;
    case SLOT_READY:
      if (back) slot_set(p, SLOT_BIKE);
      break;
    }
  }
  if (!players_set) G.players = joined_count();
  bool ready = true;
  for (int p = 0; p < MAX_RIDERS; p++) ready &= G.slot[p] == SLOT_OUT || G.slot[p] == SLOT_READY;
  if (ready) {
    load_track(0, true, 1);
    enter(GS_SELECT);
  } else if (changed) preview_load();
}


static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  G.ticks++;
  G.state_ticks++;
  read_devices();
  touch_layout();
  touch_haptics();
  gfx_input();
  for (int i = 0; i < SFX_COUNT; i++)
    if (A.cooldown[i] > 0) A.cooldown[i]--;
  for (int r = 0; r < NRID; r++) {
    rider_t *q = &RD[r];
    for (int i = 0; i < q->popup_count; i++) q->popups[i].age++;
    while (q->popup_count > 0 && q->popups[0].age > 100) {
      memmove(q->popups, q->popups + 1, sizeof q->popups[0] * (size_t)(q->popup_count - 1));
      q->popup_count--;
    }
    if (q->banner_ticks > 0) q->banner_ticks--;
  }
  if (G.bump_ticks > 0) G.bump_ticks--;
  if (pending_track >= 0) {
    start_track(pending_track);
    pending_track = -1;
  }
  if (pending_sweep > 0) {
    respawn_sweep(pending_sweep, sweep_raw);
    pending_sweep = 0;
  }
  cw_vec2 cs = cw_r2d_canvas_size(), m = mouse_canvas();
  bool ok = cw_action_just_pressed(0, G.m_ok);
  switch (G.state) {
  case GS_TITLE:
    ride_tick(autopilot());
    if (G.state_ticks > 20 && (ok || mouse_clicked || cw_action_just_pressed(0, G.a_pause))) {
      game_sfx(SFX_SELECT, 1);
      for (int p = 0; p < MAX_RIDERS; p++) { /* P1 picks; the other pads join here */
        if (p == 0 || !players_set) G.slot[p] = p == 0 ? SLOT_RIDER : SLOT_OUT;
        G.slot_ticks[p] = 0;
      }
      if (!players_set) G.players = 1;
      enter(GS_CHARACTER);
      preview_load();
    }
    break;
  case GS_CHARACTER:
    if (NRID > 1) mp_tick();
    else ride_tick((bike_input){ .hold = true });
    players_tick(m, cs, ok);
    break;
  case GS_SELECT: {
    ride_tick(autopilot());
    menu_select();
    if (mouse_clicked)
      for (int i = 0; i < TRACK_COUNT; i++)
        if (in_rect(m, tile_rect(i, cs.x, cs.y))) {
          G.select_cursor = i;
          pick_track(i);
          return;
        }
    if (G.state_ticks > 10 && ok) {
      pick_track(G.select_cursor);
      return;
    }
    if (cw_action_just_pressed(0, G.m_back)) { /* back to the bikes, everyone */
      for (int p = 0; p < MAX_RIDERS; p++)
        if (G.slot[p] == SLOT_READY && (p == 0 || !players_set)) G.slot[p] = SLOT_BIKE, G.slot_ticks[p] = 0;
      enter(GS_CHARACTER);
      preview_load();
    }
    break;
  }
  case GS_COUNT: {
    bike_input hold = { .hold = true }; /* feet down on the line */
    if (NRID > 1) mp_tick();
    else ride_tick(hold);
    if (G.state_ticks % 60 == 1 && G.state_ticks < COUNT_TICKS) game_sfx(SFX_COUNTDOWN, 0.8f);
    if (G.state_ticks >= COUNT_TICKS) {
      game_sfx(SFX_GO, 1);
      banner("GO!", 45);
      enter(GS_RIDE);
    }
    break;
  }
  case GS_RIDE:
  case GS_CRASH: {
    bool pause_hit = cw_action_just_pressed(0, G.a_pause) || cw_action_just_pressed(0, cw_action_find("pause"));
    const cw_raw_input *in = cw_input_raw();
    (void)in;
    { /* the on-screen pause button: a press on it (a finger on a touch control is not a mouse click) */
      static bool was;
      bool on = cw_touch_control_active(TOUCH_PAUSE);
      if (on && !was && G.state_ticks > 5) pause_hit = true;
      was = on;
    }
    if (G.paused) {
      int dir = cw_action_just_pressed(0, G.m_down) - cw_action_just_pressed(0, G.m_up);
      G.pause_cursor = (G.pause_cursor + dir + PAUSE_ITEMS) % PAUSE_ITEMS;
      float W = cs.x, H = cs.y, s = H / 540.0f;
      int clicked = -1;
      if (mouse_clicked)
        for (int i = 0; i < PAUSE_ITEMS; i++)
          if (in_rect(m, pause_rect(i, W, H, s))) clicked = i;
      if (clicked >= 0) G.pause_cursor = clicked;
      if (ok || clicked >= 0) {
        game_sfx(SFX_SELECT, 1);
        G.paused = false;
        if (G.pause_cursor == PAUSE_CHECKPOINT && G.state == GS_RIDE) {
          P->resets++;
          respawn();
        } else if (G.pause_cursor == PAUSE_RESTART) start_track(G.track - 1);
        else if (G.pause_cursor == PAUSE_TRACKS) {
          load_track(0, true, 1);
          enter(GS_SELECT);
        }
      } else if (pause_hit || cw_action_just_pressed(0, G.m_back)) G.paused = false;
      break;
    }
    if (pause_hit) {
      G.paused = true;
      G.pause_cursor = 0;
      break;
    }
    if (NRID > 1) {
      mp_tick();
      break;
    }
    G.ride_ticks++;
    bike_input in2 = G.state == GS_RIDE ? held_through(player_input()) : (bike_input){ 0 };
    if (G.state == GS_RIDE && getenv("BIKEMESA_AUTORIDE")) { /* dev aid: the title's autopilot rides the track */
      in2 = (bike_input){ .pedal = 1 }; /* pedal only: what a first-time player does */
      if (!strcmp(getenv("BIKEMESA_AUTORIDE"), "brake")) /* ...or a nervous one: brakes hard before every take-off
                                                         (and so may fall off a loop: its sweep rides clean but for the loops) */
        for (int k = 0; k < G.t->kicker_count; k++) {
          float x0 = G.t->kickers[k * 3], bx = bike_get()->x;
          if (G.t->kickers[k * 3 + 2] > 0 && bx > x0 - 14 && bx < x0 - 6 && bike_get()->vx > 3) in2 = (bike_input){ .brake = 1 };
        }
      if (!strcmp(getenv("BIKEMESA_AUTORIDE"), "hop")) /* ...or one who hops off every lip */
        for (int k = 0; k < G.t->kicker_count; k++) {
          float x1 = G.t->kickers[k * 3 + 1], bx = bike_get()->x;
          if (G.t->kickers[k * 3 + 2] > 0 && bx > x1 - 1.6f && bx < x1 - 0.2f) in2.hop = true;
        }
      if (!strcmp(getenv("BIKEMESA_AUTORIDE"), "low")) /* ...or one who rolls gently off every take-off (the low routes) */
        for (int k = 0; k < G.t->kicker_count; k++) {
          float x0 = G.t->kickers[k * 3], x1 = G.t->kickers[k * 3 + 1], bx = bike_get()->x;
          if (G.t->kickers[k * 3 + 2] > 0 && bx > x0 - 30 && bx < x1 && bike_get()->vx > 5) in2 = (bike_input){ .brake = 1 };
        }
    }
    if (G.state == GS_RIDE && cw_action_just_pressed(0, G.a_reset) && G.state_ticks > 10) {
      P->resets++;
      respawn();
    }
    if (G.state == GS_RIDE) lane_input(), auto_lane();
    ride_tick(in2);
    if (P->lane_t > 0 && ++P->lane_t > LANE_TICKS) P->lane_t = 0;
    bike_pose_get(1, &ring[0][P->ring_n % RING]);
    P->ring_n++;
    if (G.state == GS_CRASH && G.state_ticks >= CRASH_TICKS) {
      if (G.attract || P->ring_n - (P->crash_rec - REPLAY_BEFORE) > RING) {
        respawn();
        enter(GS_RIDE);
      } else {
        enter(GS_REPLAY); /* the sim waits while the wipeout replays */
        P->replay_t = 0;
        P->oof = false;
        game_sfx(SFX_SLOWCRASH, 1);
      }
    }
    break;
  }
  case GS_REPLAY: {
    P->replay_t++;
    bool skip = P->replay_t > 6 && (ok || mouse_clicked || cw_action_just_pressed(0, G.a_reset) || cw_action_just_pressed(0, G.a_pedal) ||
                                    cw_action_just_pressed(0, G.a_pause));
    if (!P->oof && P->replay_t * REPLAY_SPEED >= REPLAY_BEFORE) {
      P->oof = true;
      banner("OOF!", 80);
      game_sfx(SFX_THUD, 1);
    }
    if (skip || P->replay_t * REPLAY_SPEED >= REPLAY_TICKS) {
      G.replays++;
      G.replay_ticks = (int32_t)fminf(P->replay_t * REPLAY_SPEED, REPLAY_TICKS);
      P->replay_t = 0;
      P->banner_ticks = 0;
      respawn();
      enter(GS_RIDE);
      P->cam_snap = true;
    }
    break;
  }
  case GS_FINISH: {
    bike_input coast = { .brake = 1, .hold = bike_get()->speed < 0.6f }; /* stopped: feet down, no rolling back */
    if (NRID > 1) mp_tick();
    else ride_tick(coast);
    if (G.state_ticks > 60) {
      float W = cs.x, H = cs.y, s = H / 540.0f;
      bool next_hit = mouse_clicked && in_rect(m, cw_rect_make(W * 0.5f + 10 * s, H * 0.72f, 170 * s, 50 * s));
      bool retry_hit = mouse_clicked && in_rect(m, cw_rect_make(W * 0.5f - 180 * s, H * 0.72f, 170 * s, 50 * s));
      if (retry_hit || cw_action_just_pressed(0, G.a_pause) || cw_input_raw()->keys_pressed[21 /* R */]) {
        start_track(G.track - 1);
        return;
      }
      if (next_hit || ok) {
        int nx = G.track < TRACK_COUNT ? G.track : 0;
        load_track(0, true, 1);
        G.select_cursor = nx;
        enter(GS_SELECT);
        return;
      }
    }
    break;
  }
  default:
    break;
  }
  if (NRID == 1) camera_tick();
  const bike_state *b = bike_get();
  static FILE *trace;
  if (!trace && getenv("BIKEMESA_TRACE")) trace = fopen(getenv("BIKEMESA_TRACE"), "w"); /* dev aid (native): a tick log */
  if (trace && (G.ticks % 6 == 0 || bike_crashed_this_tick()))
    fprintf(trace, "t=%d st=%d x=%.2f y=%.2f a=%.2f v=%.1f,%.1f gr=%d%d air=%d rot=%.2f alive=%d%s", G.ticks, G.state, b->x, b->y, b->angle, b->vx,
            b->vy, b->grounded_rear, b->grounded_front, b->air_ticks, b->air_rot, b->alive, bike_crashed_this_tick() ? " CRASH\n" : "\n");
  dbg_hops = b->hops;
  dbg_loops = b->loops;
  G.bike_pos[0] = b->x;
  G.bike_pos[1] = b->y;
  int32_t ints[] = { G.state, G.track, G.ride_ticks, P->crashes, P->checkpoint, P->score, P->flips };
  G.state_hash = cw_hash32(ints, sizeof ints, G.state_hash);
  for (int i = 1; i < NRID; i++) { /* every other rider's race too */
    int32_t more[] = { RD[i].crashes, RD[i].checkpoint, RD[i].score, RD[i].place, RD[i].lane_to };
    G.state_hash = cw_hash32(more, sizeof more, G.state_hash);
  }
  G.state_hash = bike_hash(G.state_hash);
}

static void update(void *u, double dt) {
  dbg_quality = (int32_t)cw_quality_get();
  cw_quality_auto_measure(G.state == GS_RIDE && !G.paused); /* the preset steps down when play runs slow */
  (void)u;
  (void)dt;
  if (A.on && NRID > 1) { /* a listener per view: each sound heard from the nearest rider's camera */
    for (int v = 0; v < NRID; v++) {
      if (!cw_node_valid(RD[v].cam)) continue;
      const t3_mat4 *mm = cw_node3d_global_transform(RD[v].cam);
      t3_vec3 p = { mm->e[12], mm->e[13], mm->e[14] }, f = { -mm->e[8], -mm->e[9], -mm->e[10] }, up = { mm->e[4], mm->e[5], mm->e[6] };
      cw_audio_set_listener_at(v, p, (t3_vec3){ 0, 0, 0 }, f, up);
    }
    cw_audio_set_listener_count(NRID);
    cw_audio_update();
  } else if (A.on) {
    cw_audio_set_listener_count(1);
    cw_node cam = cw_render3d_camera(cw_scene_app_tree());
    if (cw_node_valid(cam)) {
      const t3_mat4 *mm = cw_node3d_global_transform(cam);
      t3_vec3 p = { mm->e[12], mm->e[13], mm->e[14] }, f = { -mm->e[8], -mm->e[9], -mm->e[10] }, up = { mm->e[4], mm->e[5], mm->e[6] };
      cw_audio_set_listener(p, (t3_vec3){ 0, 0, 0 }, f, up);
    }
    cw_audio_update();
  }
}

/* ── HUD (render2d over the 3D frame; render-only) ───────────────────── */


/* the outline alpha: the 12 offset stamps these outlines started as
 * overlapped about twice over each spot, so they read denser than one */
static float line_alpha(float a) { return 1 - (1 - a) * (1 - a); }
/* text with an outline of `radius` px in `line`: one layout, one draw */
static void outlined(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_color line, float radius,
                     cw_text_align align, float wrap) {
  line.a = line_alpha(line.a);
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap, .outline_color = line, .outline_width = radius };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}
static void text(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_font f = size < 20 ? font_ui_bold : font_ui;
  outlined(f, s, x, y, size, c, rgba(0.05f, 0.04f, 0.06f, 0.85f * c.a), size * 0.08f + 0.6f, align, wrap);
}
static void num(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  outlined(font_num, s, x, y, size, c, rgba(0.05f, 0.03f, 0.04f, 0.9f * c.a), size * 0.07f + 0.6f, align, wrap);
}
/* display text: an outline and a soft shadow below */
static void title_text(const char *s, float x, float y, float size, cw_color c, cw_text_align align, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = align, .wrap_width = wrap,
                       .outline_width = size * 0.055f, .outline_color = rgba(0.08f, 0.04f, 0.06f, c.a),
                       .shadow_color = rgba(0, 0, 0, line_alpha(0.45f * c.a)), .shadow_offset = cw_v2(0, size * 0.06f),
                       .shadow_spread = size * 0.09f };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}

static void round_rect(cw_rect r, float rad, cw_color c) { cw_r2d_round_rect(r, rad, 8, c); }
static void panel(cw_rect r, float alpha) {
  float rad = fminf(r.h * 0.2f, 16);
  round_rect(cw_rect_make(r.x + 3, r.y + 6, r.w, r.h), rad, rgba(0, 0, 0, 0.3f * alpha));
  round_rect(cw_rect_make(r.x - 3, r.y - 3, r.w + 6, r.h + 6), rad + 3, rgba(1, 0.5f, 0.12f, 0.95f * alpha));
  round_rect(r, rad, rgba(0.07f, 0.07f, 0.1f, 0.88f * alpha));
}
static void chip(const char *s, float cx, float y, float size, float alpha) {
  cw_text_style st = { .size = size };
  cw_text_metrics mm = cw_font_measure(font_ui_bold, s, &st);
  float padx = size * 0.8f, pady = size * 0.32f;
  round_rect(cw_rect_make(cx - mm.width * 0.5f - padx, y - pady, mm.width + 2 * padx, mm.height + 2 * pady), size * 0.7f,
             rgba(0.04f, 0.04f, 0.06f, 0.55f * alpha));
}

static void fmt_time(char *out, size_t n, int ticks) {
  int cs = ticks * 100 / TICK_HZ;
  snprintf(out, n, "%d:%02d.%02d", cs / 6000, cs / 100 % 60, cs % 100);
}

static const char *prompt(void) {
  if (G.state == GS_TITLE)
    return last_device == DEV_TOUCH ? "tap to ride" : last_device == DEV_MOUSE ? "click to ride" : last_device == DEV_PAD ? "press (A) to ride" : "press Enter to ride";
  if (G.state == GS_CHARACTER)
    return last_device == DEV_TOUCH ? "tap the arrows, then select" : last_device == DEV_MOUSE ? "click the arrows, then select"
         : last_device == DEV_PAD ? "left / right: choose   (A): select   (B): back"
                                  : "left / right: choose   Enter: select   Esc: back";
  if (G.state == GS_SELECT)
    return last_device == DEV_TOUCH ? "tap a track" : last_device == DEV_MOUSE ? "click a track" : last_device == DEV_PAD ? "d-pad: choose   (A): ride   (B): back"
                                                                               : "arrows: choose   Enter: ride   Esc: back";
  if ((G.state == GS_RIDE || G.state == GS_COUNT) && !G.paused)
    return last_device == DEV_TOUCH ? ""
         : last_device == DEV_PAD ? "(A) pedal  (B) brake  left / right: lean  (X) hop  (Y) + left / right / up: pose  SELECT: checkpoint"
                                  : "Z: pedal  X: brake  Left / Right: lean  C: hop  A / S / D: pose  R: checkpoint";
  return "";
}

/* the controls for the device in use, two lines on a panel */
static void controls_card(float x, float y, float w, float s) {
  const char *a, *b;
  if (last_device == DEV_TOUCH) {
    a = "left thumb: drag left / right to lean, up / down to change lane";
    b = "right thumb: PEDAL   BRAKE (stopped: back-pedal)   HOP   hold a pose";
  } else if (last_device == DEV_PAD) {
    a = "(A) pedal   (B) brake / back-pedal   (X) hop   left / right: lean, flip   up / down: lane";
    b = "hold (Y) + left / right / up, or LB / RB / both: the poses   SELECT: checkpoint";
  } else {
    a = "Z: pedal   X: brake / back-pedal   C: hop   Left / Right: lean, flip   Up / Down: lane";
    b = "hold A: no-hander   S: no-footer   D: superman   R: checkpoint   Esc: pause";
  }
  round_rect(cw_rect_make(x, y, w, 58 * s), 12 * s, rgba(0.05f, 0.05f, 0.08f, 0.72f));
  text(a, x, y + 8 * s, 16 * s, rgba(1, 1, 1, 0.95f), CW_ALIGN_CENTER, w);
  text(b, x, y + 31 * s, 16 * s, rgba(1, 0.85f, 0.6f, 0.95f), CW_ALIGN_CENTER, w);
}

/* each rider's colour (as their bike's paint) */
static cw_color rider_col(int i, float a) {
  static const float C[MAX_RIDERS][3] = { { 0.35f, 0.9f, 0.45f }, { 1, 0.35f, 0.3f }, { 0.4f, 0.65f, 1 }, { 1, 0.85f, 0.25f } };
  return rgba(C[i][0], C[i][1], C[i][2], a);
}
/* the player select: a tag per connected pad that has not joined, how to
 * join, stacked down from (x, y) */
static void draw_joins(float x, float y, float s) {
  for (int p = 1; p < MAX_RIDERS; p++) {
    int pad = cw_input_player_pad(p);
    if (pad < 0 || !cw_input_raw()->pads[pad].connected || G.slot[p] != SLOT_OUT || players_set) continue;
    char b[40];
    snprintf(b, sizeof b, "P%d: (A) to join", p + 1);
    round_rect(cw_rect_make(x, y, 186 * s, 28 * s), 8 * s, rgba(0.05f, 0.05f, 0.08f, 0.75f));
    text(b, x, y + 4 * s, 16 * s, rider_col(p, 1), CW_ALIGN_CENTER, 186 * s);
    y += 34 * s;
  }
}

static void draw_title(float W, float H, float s) {
  float bob = 4 * s * cw_sin(G.ticks * 0.05f);
  title_text(GAME_TITLE, 0, H * 0.12f + bob, 112 * s, rgba(1, 0.62f, 0.15f, 1), CW_ALIGN_CENTER, W);
  text("ride hard. land clean. fly further.", 0, H * 0.12f + 128 * s, 22 * s, rgba(1, 0.95f, 0.88f, 0.95f), CW_ALIGN_CENTER, W);
  float pulse = 0.65f + 0.35f * cw_sin(G.ticks * 0.08f);
  const char *p = prompt();
  chip(p, W * 0.5f, H * 0.82f, 24 * s, pulse);
  text(p, 0, H * 0.82f, 24 * s, rgba(1, 1, 1, pulse), CW_ALIGN_CENTER, W);
  controls_card(W * 0.5f - 340 * s, H * 0.885f, 680 * s, s);
}

static void draw_carousel(float W, float H, float s) {
  bool rider = G.slot[0] == SLOT_RIDER;
  int choice = rider ? G.chr[0] : G.bike[0];
  const char *name = rider ? RIDER_NAMES[choice] : BIKE_NAMES[choice];
  title_text(rider ? "CHOOSE YOUR RIDER" : "CHOOSE YOUR BIKE", 0, H * 0.055f, 44 * s,
             rgba(1, 0.67f, 0.22f, 1), CW_ALIGN_CENTER, W);
  chip(name, W * 0.18f, H * 0.26f, 28 * s, 1);
  text(name, W * 0.02f, H * 0.26f, 28 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W * 0.32f);
  cw_rect confirm = confirm_rect(W, H);
  round_rect(confirm, 14 * s, rgba(1, 0.5f, 0.12f, 0.95f));
  text("SELECT", confirm.x, confirm.y + 10 * s, 24 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, confirm.w);
  for (int i = 0; i < 2; i++) {
    cw_rect r = choice_rect(i, W, H);
    round_rect(r, 13 * s, rgba(0.07f, 0.075f, 0.1f, 0.88f));
    text(i ? "NEXT  >" : "<  PREVIOUS", r.x, r.y + 12 * s, 22 * s,
         rgba(1, 0.8f, 0.43f, 1), CW_ALIGN_CENTER, r.w);
  }
  const char *p = prompt();
  chip(p, W * 0.5f, H * 0.96f, 18 * s, 1);
  text(p, 0, H * 0.96f, 18 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
  draw_joins(W - 200 * s, 14 * s, s);
}

/* the player select with more than one player: in each player's view their
 * rider turning, what they are choosing and how far they are */
static void draw_player_views(float W, float H, float s) {
  const cw_view *views;
  int nv = cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &views);
  float k = s * (NRID == 2 ? 0.8f : 0.66f);
  for (int v = 0; v < nv && v < NRID; v++) {
    cw_rect r = cw_rect_make(views[v].x * W, views[v].y * H, views[v].w * W, views[v].h * H);
    int st = G.slot[v];
    char b[48];
    snprintf(b, sizeof b, "P%d", v + 1);
    title_text(b, r.x + 14 * k, r.y + 10 * k, 40 * k, rider_col(v, 1), CW_ALIGN_LEFT, 0);
    const char *head = st == SLOT_RIDER ? "CHOOSE YOUR RIDER" : st == SLOT_BIKE ? "CHOOSE YOUR BIKE" : "READY!";
    title_text(head, r.x, r.y + 14 * k, 30 * k, st == SLOT_READY ? rgba(0.45f, 1, 0.5f, 1) : rgba(1, 0.67f, 0.22f, 1), CW_ALIGN_CENTER, r.w);
    const char *name = st == SLOT_RIDER ? RIDER_NAMES[G.chr[v]] : BIKE_NAMES[G.bike[v]];
    if (st == SLOT_READY) snprintf(b, sizeof b, "%s", BIKE_NAMES[G.bike[v]]);
    else snprintf(b, sizeof b, "<   %s   >", name);
    chip(b, r.x + r.w * 0.5f, r.y + r.h - 76 * k, 24 * k, 1);
    text(b, r.x, r.y + r.h - 76 * k, 24 * k, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
    const char *hint = st == SLOT_READY ? "(B) change" : v == 0 ? "(A) select   (B) back" : st == SLOT_RIDER ? "(A) select   (B) leave" : "(A) select   (B) back";
    if (v == 0 && last_device == DEV_KEYS) hint = st == SLOT_READY ? "Esc: change" : "Enter: select   Esc: back";
    text(hint, r.x, r.y + r.h - 36 * k, 18 * k, rgba(1, 0.85f, 0.6f, 0.95f), CW_ALIGN_CENTER, r.w);
  }
  if (NRID == 3) { /* the free quadrant */
    cw_rect q = cw_rect_make(W * 0.5f, 0, W * 0.5f, H * 0.5f);
    cw_r2d_rect(q, rgba(0.04f, 0.04f, 0.06f, 1));
    title_text("CHOOSE YOUR RIDERS", q.x, q.y + 30 * s, 30 * s, rgba(1, 0.62f, 0.15f, 1), CW_ALIGN_CENTER, q.w);
    text("everyone ready: P1 picks the track", q.x, q.y + 80 * s, 17 * s, rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, q.w);
    draw_joins(q.x + q.w * 0.5f - 93 * s, q.y + 130 * s, s);
  } else if (NRID == 2) draw_joins(W - 200 * s, H * 0.5f + 14 * s, s * 0.9f);
}

static void medal_dot(float x, float y, float r, int medal, float a) {
  static const float C[4][3] = { { 0.3f, 0.3f, 0.32f }, { 0.8f, 0.5f, 0.25f }, { 0.78f, 0.8f, 0.85f }, { 1, 0.8f, 0.2f } };
  cw_r2d_arc(cw_v2(x, y), 0, r, 0, 6.2831853f, 24, rgba(0.05f, 0.03f, 0.02f, 0.7f * a));
  cw_r2d_arc(cw_v2(x, y), 0, r * 0.8f, 0, 6.2831853f, 24, rgba(C[medal][0], C[medal][1], C[medal][2], a));
}

static void star(float x, float y, float r, bool on, float a) {
  cw_vec2 p[10];
  for (int i = 0; i < 10; i++) {
    float ang = -1.5707963f + i * 0.6283185f, rr = i & 1 ? r * 0.45f : r;
    p[i] = cw_v2(x + rr * cw_cos(ang), y + rr * cw_sin(ang));
  }
  cw_r2d_fan(cw_v2(x, y), p, 10, true, on ? rgba(1, 0.82f, 0.2f, a) : rgba(0.25f, 0.25f, 0.3f, 0.8f * a));
}
static void padlock(float x, float y, float sz, float a) {
  cw_vec2 arc[13];
  for (int i = 0; i <= 12; i++) {
    float ang = 3.1415927f * i / 12;
    arc[i] = cw_v2(x + sz * 0.3f * cw_cos(ang), y - sz * 0.15f - sz * 0.32f * cw_sin(ang));
  }
  cw_r2d_polyline(arc, 13, sz * 0.12f, false, rgba(0.85f, 0.85f, 0.9f, a));
  round_rect(cw_rect_make(x - sz * 0.45f, y - sz * 0.18f, sz * 0.9f, sz * 0.7f), sz * 0.12f, rgba(0.85f, 0.85f, 0.9f, a));
  round_rect(cw_rect_make(x - sz * 0.06f, y + sz * 0.02f, sz * 0.12f, sz * 0.26f), sz * 0.05f, rgba(0.2f, 0.2f, 0.25f, a));
}

static void draw_select(float W, float H, float s) {
  for (int p = 0; p < G.players && G.players > 1; p++) { /* who is racing */
    char b[8];
    snprintf(b, sizeof b, "P%d", p + 1);
    float x = W - (G.players - p) * 58 * s - 8 * s;
    round_rect(cw_rect_make(x, 14 * s, 50 * s, 30 * s), 8 * s, rider_col(p, 0.9f));
    title_text(b, x, 17 * s, 22 * s, rgba(0.05f, 0.05f, 0.06f, 1), CW_ALIGN_CENTER, 50 * s);
  }
  title_text("PICK A TRACK", 0, H * 0.06f, 42 * s, rgba(1, 0.62f, 0.15f, 1), CW_ALIGN_CENTER, W);
  for (int i = 0; i < TRACK_COUNT; i++) {
    cw_rect r = tile_rect(i, W, H);
    if (i == G.bump_tile && G.bump_ticks > 0) r.x += 6 * s * cw_sin(G.bump_ticks * 1.6f) * G.bump_ticks / 24.0f;
    bool open = track_open(i), on = i == G.select_cursor;
    if (on) round_rect(cw_rect_make(r.x - 6 * s, r.y - 6 * s, r.w + 12 * s, r.h + 12 * s), 18 * s, rgba(1, 0.75f, 0.3f, 0.5f + 0.3f * cw_sin(G.ticks * 0.12f)));
    panel(r, open ? 1 : 0.6f);
    char b[48];
    snprintf(b, sizeof b, "%d", i + 1);
    num(b, r.x + 10 * s, r.y + 4 * s, 28 * s, open ? rgba(1, 0.65f, 0.2f, 1) : rgba(0.5f, 0.5f, 0.5f, 1), CW_ALIGN_LEFT, 0);
    text(TRACKS[i].name, r.x + 10 * s, r.y + 38 * s, 15 * s, open ? rgba(1, 1, 1, 1) : rgba(0.6f, 0.6f, 0.6f, 1), CW_ALIGN_LEFT, r.w - 20 * s);
    if (!open) padlock(r.x + r.w - 26 * s, r.y + 22 * s, 28 * s, 1);
    else {
      for (int k = 0; k < 3; k++) star(r.x + r.w - 70 * s + k * 22 * s, r.y + 18 * s, 9 * s, SAVE.tr[i].stars > k, 1);
      if (SAVE.tr[i].best_ticks) {
        fmt_time(b, sizeof b, SAVE.tr[i].best_ticks);
        text(b, r.x + 10 * s, r.y + r.h - 24 * s, 15 * s, rgba(0.85f, 0.95f, 1, 1), CW_ALIGN_LEFT, 0);
        medal_dot(r.x + r.w - 22 * s, r.y + r.h - 16 * s, 10 * s, SAVE.tr[i].medal, 1);
      } else text("new", r.x + 10 * s, r.y + r.h - 24 * s, 14 * s, rgba(0.7f, 1, 0.7f, 1), CW_ALIGN_LEFT, 0);
    }
  }
  const char *p = prompt();
  chip(p, W * 0.5f, H * 0.88f, 20 * s, 1);
  text(p, 0, H * 0.88f, 20 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
}

static bool project(t3_vec3 p, float W, float H, cw_vec2 *out) {
  cw_node cam = NRID > 1 ? P->cam : cw_render3d_camera(cw_scene_app_tree());
  if (!cw_node_valid(cam)) return false;
  t3_camera *c = (t3_camera *)cw_node3d_object(cam);
  if (!c) return false;
  t3_vec3 v = t3_vec3_apply_mat4(p, &c->matrix_world_inverse);
  if (v.z > -0.1f) return false;
  t3_vec3 n = t3_vec3_apply_mat4(v, &c->projection_matrix);
  *out = cw_v2((n.x * 0.5f + 0.5f) * W, (0.5f - n.y * 0.5f) * H);
  return true;
}

/* a filled arrow (dir 0 right, 1 left, 2 up, 3 down) centred at (x, y) */
static void arrow(float x, float y, float r, int dir, cw_color c) {
  static const float D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, -1 }, { 0, 1 } };
  float dx = D[dir][0], dy = D[dir][1], px = -dy, py = dx;
  cw_vec2 t[3] = { cw_v2(x + dx * r, y + dy * r), cw_v2(x - dx * r * 0.6f + px * r * 0.85f, y - dy * r * 0.6f + py * r * 0.85f),
                   cw_v2(x - dx * r * 0.6f - px * r * 0.85f, y - dy * r * 0.6f - py * r * 0.85f) };
  cw_r2d_polygon(t, 3, c);
}

static void draw_touch(float W, float H, float s) {
  if (last_device != DEV_TOUCH) return;
  int ww, wh;
  cw_window_size(&ww, &wh);
  float kx = W / (float)ww, ky = H / (float)wh, k = (float)wh / 540.0f;
  touch_btn B[12];
  int n = touch_table((float)ww, (float)wh, B);
  { /* the lean stick: a ring where the thumb landed (a resting ring bottom left), the knob where it is */
    bool on = cw_touch_control_active(TOUCH_STICK);
    cw_vec2 c = on ? cw_touch_stick_center(TOUCH_STICK) : cw_v2(fmaxf(110 * k, 80), wh - fmaxf(120 * k, 90)), v = cw_touch_stick_value(TOUCH_STICK);
    float R = 60 * k;
    cw_vec2 ring[32];
    for (int j = 0; j < 32; j++) ring[j] = cw_v2((c.x + R * cw_cos(j * 0.19635f)) * kx, (c.y + R * cw_sin(j * 0.19635f)) * ky);
    cw_r2d_polygon(ring, 32, rgba(0.3f, 0.65f, 1, on ? 0.22f : 0.12f));
    for (int j = 0; j < 32; j++) /* the dead zone */
      ring[j] = cw_v2((c.x + R * 0.18f * cw_cos(j * 0.19635f)) * kx, (c.y + R * 0.18f * cw_sin(j * 0.19635f)) * ky);
    cw_r2d_polygon(ring, 32, rgba(1, 1, 1, on ? 0.12f : 0.06f));
    for (int j = 0; j < 32; j++)
      ring[j] = cw_v2((c.x + v.x * R + R * 0.45f * cw_cos(j * 0.19635f)) * kx, (c.y + v.y * R + R * 0.45f * cw_sin(j * 0.19635f)) * ky);
    cw_r2d_polygon(ring, 32, rgba(0.3f, 0.65f, 1, on ? 0.75f : 0.4f));
    arrow((c.x - R * 0.7f) * kx, c.y * ky, R * 0.2f * kx, 1, rgba(1, 1, 1, 0.7f));
    arrow((c.x + R * 0.7f) * kx, c.y * ky, R * 0.2f * kx, 0, rgba(1, 1, 1, 0.7f));
    arrow(c.x * kx, (c.y - R * 0.7f) * ky, R * 0.14f * kx, 2, rgba(1, 1, 1, 0.5f)); /* up / down: lanes */
    arrow(c.x * kx, (c.y + R * 0.7f) * ky, R * 0.14f * kx, 3, rgba(1, 1, 1, 0.5f));
    if (!on) text("LEAN", (c.x - R) * kx, (c.y + R + 6 * k) * ky, 16 * s, rgba(1, 1, 1, 0.75f), CW_ALIGN_CENTER, 2 * R * kx);
  }
  for (int i = 0; i < n; i++) {
    bool on = cw_touch_control_active(*B[i].id);
    float shrink = on ? 0.06f : 0; /* pressed: it sinks a little and lights up */
    cw_rect r = cw_rect_make((B[i].x + B[i].w * shrink * 0.5f) * kx, (B[i].y + B[i].h * shrink * 0.5f) * ky, B[i].w * (1 - shrink) * kx,
                             B[i].h * (1 - shrink) * ky);
    cw_color c = B[i].c;
    round_rect(r, fminf(r.w, r.h) * 0.3f, rgba(c.r * 0.25f, c.g * 0.25f, c.b * 0.25f, on ? 0.8f : 0.45f));
    round_rect(cw_rect_make(r.x + 3 * s, r.y + 3 * s, r.w - 6 * s, r.h - 6 * s), fminf(r.w, r.h) * 0.27f,
               rgba(c.r, c.g, c.b, on ? 0.65f : 0.18f));
    if (B[i].dir >= 0) {
      arrow(r.x + r.w * 0.5f, r.y + r.h * 0.42f, fminf(r.w, r.h) * 0.22f, B[i].dir, rgba(1, 1, 1, on ? 1 : 0.85f));
      text(B[i].label, r.x, r.y + r.h - 34 * s, 16 * s, rgba(1, 1, 1, on ? 1 : 0.8f), CW_ALIGN_CENTER, r.w);
    } else text(B[i].label, r.x, r.y + r.h * 0.5f - (B[i].dir == -2 ? 8 : 10) * s, (B[i].dir == -2 ? 13 : 18) * s, rgba(1, 1, 1, on ? 1 : 0.85f),
                CW_ALIGN_CENTER, r.w);
  }
}

/* a race view's tag: the rider's number and place, bottom left */
static void draw_race_tag(float W, float H, float s) {
  int i = (int)(P - RD), order[MAX_RIDERS], pos = 0;
  race_order(order);
  for (int k = 0; k < NRID; k++)
    if (order[k] == i) pos = k + 1;
  char b[64];
  round_rect(cw_rect_make(14 * s, H - 70 * s, 150 * s, 56 * s), 12 * s, rgba(0.05f, 0.05f, 0.08f, 0.7f));
  snprintf(b, sizeof b, "P%d", i + 1);
  title_text(b, 24 * s, H - 64 * s, 30 * s, rider_col(i, 1), CW_ALIGN_LEFT, 0);
  title_text(P->place ? ordinal(P->place) : ordinal(pos), 78 * s, H - 64 * s, 30 * s, rgba(1, 1, 1, 1), CW_ALIGN_LEFT, 0);
  if (P->finished && G.state == GS_RIDE) {
    char t[32];
    fmt_time(t, sizeof t, P->finish_ticks);
    snprintf(b, sizeof b, "HOME %s  %s", ordinal(P->place), t);
    chip(b, W * 0.5f, H * 0.42f, 22 * s, 1);
    text(b, 0, H * 0.42f, 22 * s, rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER, W);
  }
}
/* the results: place, rider, time home (or not home), score, wipeouts */
static void draw_results(cw_rect r, float s, float a) {
  panel(r, a);
  title_text("RESULTS", r.x, r.y + 10 * s, 40 * s, rgba(1, 0.65f, 0.2f, a), CW_ALIGN_CENTER, r.w);
  int order[MAX_RIDERS];
  race_order(order);
  float y = r.y + 70 * s, row = 46 * s, fs = 26 * s;
  for (int k = 0; k < NRID; k++) {
    int i = order[k];
    const rider_t *q = &RD[i];
    char b[64], t[32];
    title_text(ordinal(q->place ? q->place : k + 1), r.x + 20 * s, y, fs, rgba(1, 1, 1, a), CW_ALIGN_LEFT, 0);
    snprintf(b, sizeof b, "P%d", i + 1);
    title_text(b, r.x + r.w * 0.24f, y, fs, rider_col(i, a), CW_ALIGN_LEFT, 0);
    if (q->finished) fmt_time(t, sizeof t, q->finish_ticks);
    else snprintf(t, sizeof t, "--");
    snprintf(b, sizeof b, "%s   %d   %dx", t, q->score, q->crashes);
    num(b, r.x, y + 2 * s, fs * 0.9f, rgba(1, 0.9f, 0.7f, a), CW_ALIGN_RIGHT, r.w - 20 * s);
    y += row;
  }
  text("time   score   wipeouts", r.x, y, 14 * s, rgba(1, 1, 1, 0.6f * a), CW_ALIGN_RIGHT, r.w - 20 * s);
}

/* ── the mini map: the course's line seen side on, the riders on it ──── */
#define MM_N 192
static float mm_y[MM_N];   /* the ground's height at each sample (NAN: none there, a gap) */
static float mm_x0, mm_x1, mm_lo, mm_hi;
static void minimap_build(void) {
  mm_x0 = 0;
  mm_x1 = G.t->finish_x + 6;
  mm_lo = 1e9f, mm_hi = -1e9f;
  for (int i = 0; i < MM_N; i++) {
    bool found;
    float y = track_height_at(mm_x0 + (mm_x1 - mm_x0) * i / (MM_N - 1), &found);
    mm_y[i] = found ? y : NAN;
    if (found) mm_lo = fminf(mm_lo, y), mm_hi = fmaxf(mm_hi, y);
  }
  if (mm_hi <= mm_lo) mm_hi = mm_lo + 1;
}
static cw_vec2 mm_at(cw_rect r, float x, float y) {
  return cw_v2(r.x + (x - mm_x0) / (mm_x1 - mm_x0) * r.w, r.y + r.h - (y - mm_lo) / (mm_hi - mm_lo) * r.h);
}
static void draw_minimap(cw_rect r, float s) {
  cw_vec2 pts[MM_N];
  int n = 0;
  for (int i = 0; i <= MM_N; i++) { /* the line, broken at the gaps */
    if (i < MM_N && !isnan(mm_y[i])) {
      pts[n++] = mm_at(r, mm_x0 + (mm_x1 - mm_x0) * i / (MM_N - 1), mm_y[i]);
      continue;
    }
    if (n > 1) cw_r2d_polyline(pts, n, 3 * s, false, rgba(0.85f, 0.8f, 0.72f, 0.9f));
    n = 0;
  }
  for (int c = 0; c < G.t->check_count; c++) { /* the checkpoints: a post */
    cw_vec2 at = mm_at(r, G.t->checks[c * 2], G.t->checks[c * 2 + 1]);
    cw_r2d_rect(cw_rect_make(at.x - 1.5f * s, at.y - 14 * s, 3 * s, 14 * s), rgba(1, 0.85f, 0.4f, 1));
  }
  cw_vec2 f = mm_at(r, G.t->finish_x, G.t->finish_y); /* the finish: a chequered flag */
  cw_r2d_rect(cw_rect_make(f.x - 1.5f * s, f.y - 22 * s, 3 * s, 22 * s), rgba(1, 1, 1, 1));
  for (int k = 0; k < 6; k++)
    cw_r2d_rect(cw_rect_make(f.x + 1.5f * s + (k % 3) * 5 * s, f.y - 22 * s + (k / 3) * 5 * s, 5 * s, 5 * s),
                (k + k / 3) & 1 ? rgba(0.05f, 0.05f, 0.06f, 1) : rgba(1, 1, 1, 1));
  for (int i = NRID - 1; i >= 0; i--) { /* the riders, P1 on top */
    int keep = (int)(P - RD);
    bike_select(i);
    const bike_state *b = bike_get();
    cw_vec2 at = mm_at(r, cw_clampf(b->x, mm_x0, mm_x1), b->y);
    bool up = b->alive;
    bike_select(keep);
    cw_r2d_arc(at, 0, 8 * s, 0, 6.2831853f, 20, rgba(0.03f, 0.03f, 0.04f, 0.9f));
    cw_r2d_arc(at, 0, 6 * s, 0, 6.2831853f, 20, rider_col(i, up ? 1 : 0.45f));
    char b2[12];
    snprintf(b2, sizeof b2, "%d", i + 1);
    text(b2, at.x - 20 * s, at.y - 26 * s, 15 * s, rider_col(i, 1), CW_ALIGN_CENTER, 40 * s);
  }
}

/* three players: the top right quadrant is the race's own. The clock, the
 * track, the mini map and the standings */
static void draw_shared(cw_rect q, float s) {
  cw_r2d_rect(q, rgba(0.04f, 0.04f, 0.06f, 1));
  char b[64], t[32];
  fmt_time(b, sizeof b, G.ride_ticks);
  num(b, q.x, q.y + 8 * s, 40 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, q.w);
  text(G.t ? G.t->name : "", q.x, q.y + 52 * s, 15 * s, rgba(1, 0.9f, 0.75f, 0.9f), CW_ALIGN_CENTER, q.w);
  draw_minimap(cw_rect_make(q.x + 28 * s, q.y + 96 * s, q.w - 56 * s, 64 * s), s);
  int order[MAX_RIDERS];
  race_order(order);
  float y = q.y + 178 * s;
  for (int k = 0; k < NRID; k++, y += 28 * s) {
    int i = order[k];
    const rider_t *r = &RD[i];
    title_text(ordinal(r->place ? r->place : k + 1), q.x + 40 * s, y, 20 * s, rgba(1, 1, 1, 1), CW_ALIGN_LEFT, 0);
    snprintf(b, sizeof b, "P%d", i + 1);
    title_text(b, q.x + 120 * s, y, 20 * s, rider_col(i, 1), CW_ALIGN_LEFT, 0);
    if (r->finished) fmt_time(t, sizeof t, r->finish_ticks);
    else snprintf(t, sizeof t, "%s", r->replay_t > 0 || r->down_ticks > 0 ? "down" : "riding");
    num(t, q.x, y + 2 * s, 18 * s, rgba(1, 0.9f, 0.7f, 1), CW_ALIGN_RIGHT, q.w - 40 * s);
  }
}

/* shared: the race clock and the track's name are on the shared quadrant (three players) */
static void draw_hud(float W, float H, float s, bool shared) {
  char b[64];
  /* the clock, top centre */
  if (!shared) {
    fmt_time(b, sizeof b, G.ride_ticks);
    num(b, 0, 12 * s, 44 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, W);
    snprintf(b, sizeof b, "%s", G.t ? G.t->name : "");
    text(b, 0, 62 * s, 16 * s, rgba(1, 0.9f, 0.75f, 0.9f), CW_ALIGN_CENTER, W);
  }
  /* left: checkpoints, crashes; right: score */
  snprintf(b, sizeof b, "CHECKPOINT %d/%d", P->checkpoint, G.t ? G.t->check_count : 0);
  text(b, (last_device == DEV_TOUCH ? 112 : 18) * s, 16 * s, 17 * s, rgba(1, 0.85f, 0.4f, 1), CW_ALIGN_LEFT, 0);
  snprintf(b, sizeof b, "WIPEOUTS %d", P->crashes);
  text(b, (last_device == DEV_TOUCH ? 112 : 18) * s, 40 * s, 17 * s, rgba(1, 0.6f, 0.55f, 1), CW_ALIGN_LEFT, 0);
  snprintf(b, sizeof b, "%d", P->score);
  num(b, 0, 14 * s, 34 * s, rgba(1, 0.75f, 0.25f, 1), CW_ALIGN_RIGHT, W - (last_device == DEV_TOUCH ? 120 : 20) * s);
  /* the combo: its multiplier and the window running out */
  if (P->combo > 1 && P->combo_ticks > 0) {
    float k = P->combo_ticks / (float)COMBO_WINDOW;
    snprintf(b, sizeof b, "COMBO x%d", P->combo);
    title_text(b, 0, 86 * s, 24 * s, rgba(1, 0.55f + 0.1f * P->combo, 0.2f, 1), CW_ALIGN_CENTER, W);
    round_rect(cw_rect_make(W * 0.5f - 90 * s, 118 * s, 180 * s, 7 * s), 3.5f * s, rgba(0, 0, 0, 0.45f));
    round_rect(cw_rect_make(W * 0.5f - 90 * s, 118 * s, 180 * s * k, 7 * s), 3.5f * s, rgba(1, 0.7f, 0.2f, 0.95f));
  }
  /* the air's trick so far: its name and the points riding on the landing */
  if (P->pending > 0 && G.state == GS_RIDE) {
    const bike_state *bs = bike_get();
    int pose[4];
    char name[48];
    bike_air_poses(pose);
    air_points(bs->air_rot, pose, name, sizeof name);
    snprintf(b, sizeof b, "%s  %d", name, P->pending);
    chip(b, W * 0.5f, H * 0.27f, 22 * s, 1);
    text(b, 0, H * 0.27f, 22 * s, rgba(1, 0.9f, 0.5f, 1), CW_ALIGN_CENTER, W);
  }
  /* speed */
  const bike_state *bk = bike_get();
  snprintf(b, sizeof b, "%d km/h", (int)(fabsf(bk->speed) * 3.6f));
  text(b, 0, H - 44 * s, 18 * s, rgba(1, 1, 1, 0.85f), CW_ALIGN_CENTER, W);
  /* trick popups over the rider */
  for (int i = 0; i < P->popup_count; i++) {
    const popup *p = &P->popups[i];
    cw_vec2 at;
    if (!project((t3_vec3){ p->x, p->y + p->age * 0.02f, 0 }, W, H, &at)) continue;
    float a = p->age < 70 ? 1 : 1 - (p->age - 70) / 30.0f;
    float pop = p->age < 8 ? 0.6f + 0.05f * p->age : 1;
    title_text(p->text, at.x - 200 * s, at.y, 30 * s * pop, rgba(1, 0.85f, 0.2f, a), CW_ALIGN_CENTER, 400 * s);
    if (p->points > 0) {
      snprintf(b, sizeof b, "+%d", p->points);
      num(b, at.x - 200 * s, at.y + 34 * s, 24 * s, rgba(1, 1, 1, a), CW_ALIGN_CENTER, 400 * s);
    }
  }
  /* countdown */
  if (G.state == GS_COUNT) {
    int n = 3 - G.state_ticks / 60;
    float f = (G.state_ticks % 60) / 60.0f;
    snprintf(b, sizeof b, "%d", n < 1 ? 1 : n);
    title_text(b, 0, H * 0.14f, (140 - 40 * f) * s, rgba(1, 0.75f, 0.2f, 1 - f * 0.6f), CW_ALIGN_CENTER, W);
  }
  /* the flags' words, on the cloth: START in white on green; FINISH on a white band over the checks */
  if (G.t) {
    const float fx[2] = { -1.0f + 0.95f, G.t->finish_x + 0.6f + 1.15f }, fyo[2] = { 3.95f, 3.82f };
    for (int i = 0; i < 2; i++) {
      cw_vec2 at, at2;
      float gy = (i ? G.t->finish_y : track_height_at(0, NULL)) + fyo[i];
      if (!project((t3_vec3){ fx[i], gy, -TRACK_HALF_W - 0.45f }, W, H, &at) || at.x < -200 || at.x > W + 200) continue;
      if (!project((t3_vec3){ fx[i] + 1, gy, -TRACK_HALF_W - 0.45f }, W, H, &at2)) continue;
      float px = fabsf(at2.x - at.x), fs = cw_clampf(px * (i ? 0.4f : 0.42f), 12, 60);
      if (i) round_rect(cw_rect_make(at.x - fs * 2.0f, at.y - fs * 0.62f, fs * 4.0f, fs * 1.24f), fs * 0.2f, rgba(1, 1, 1, 0.95f));
      outlined(font_title, i ? "FINISH" : "START", at.x - 150 * s, at.y - fs * 0.55f, fs, i ? rgba(0.05f, 0.05f, 0.06f, 1) : rgba(1, 1, 1, 1),
               i ? rgba(1, 1, 1, 1) : rgba(0.02f, 0.2f, 0.06f, 1), fs * 0.06f, CW_ALIGN_CENTER, 300 * s);
    }
  }
  if (P->banner_ticks > 0) {
    float a = P->banner_ticks > 15 ? 1 : P->banner_ticks / 15.0f;
    title_text(P->banner, 0, H * 0.16f, 64 * s, rgba(1, 0.5f, 0.2f, a), CW_ALIGN_CENTER, W);
  }
  const char *p = prompt();
  if (NRID > 1) {
    draw_race_tag(W, H, s);
    return;
  }
  if (*p && G.ride_ticks < 600) {
    chip(p, W * 0.5f, H - 80 * s, 16 * s, 1);
    text(p, 0, H - 80 * s, 16 * s, rgba(1, 1, 1, 0.9f), CW_ALIGN_CENTER, W);
  }
  draw_touch(W, H, s);

}

static void draw_finish(float W, float H, float s) {
  float a = cw_clampf(G.state_ticks / 30.0f, 0, 1);
  cw_rect r = cw_rect_make(W * 0.5f - 230 * s, H * 0.2f, 460 * s, H * 0.62f);
  panel(r, a);
  title_text("FINISH!", r.x, r.y + 14 * s, 54 * s, rgba(1, 0.65f, 0.2f, a), CW_ALIGN_CENTER, r.w);
  char b[64], t[32];
  fmt_time(t, sizeof t, G.ride_ticks);
  snprintf(b, sizeof b, "%s%s", t, G.new_best ? "   NEW BEST!" : "");
  num(b, r.x, r.y + 84 * s, 34 * s, rgba(1, 1, 1, a), CW_ALIGN_CENTER, r.w);
  static const char *const MED[4] = { "", "BRONZE", "SILVER", "GOLD" };
  medal_dot(r.x + r.w * 0.5f - 90 * s, r.y + 150 * s, 20 * s, G.medal, a);
  text(MED[G.medal], r.x + r.w * 0.5f - 60 * s, r.y + 136 * s, 26 * s, rgba(1, 0.9f, 0.6f, a), CW_ALIGN_LEFT, 0);
  snprintf(b, sizeof b, "par %d s   tricks %d   flips %d   wipeouts %d", G.t->par, P->tricks, P->flips, P->crashes);
  text(b, r.x, r.y + 186 * s, 16 * s, rgba(0.85f, 0.9f, 1, a), CW_ALIGN_CENTER, r.w);
  for (int k = 0; k < 3; k++) star(r.x + r.w * 0.5f + 60 * s + k * 30 * s, r.y + 150 * s, 13 * s, G.stars > k, a);
  snprintf(b, sizeof b, "SCORE %d", P->score);
  num(b, r.x, r.y + 214 * s, 32 * s, rgba(1, 0.75f, 0.25f, a), CW_ALIGN_CENTER, r.w);
  if (G.stars < 3 && P->score >= STAR3 && !P->star3_trick) /* the score is there; the trick is not */
    text("third star: land a PERFECT frontflip, or a pose and a flip in one air", r.x, r.y + 254 * s, 14 * s, rgba(1, 0.85f, 0.5f, a),
         CW_ALIGN_CENTER, r.w);
  if (G.state_ticks > 60) {
    cw_rect retry = cw_rect_make(W * 0.5f - 180 * s, H * 0.72f, 170 * s, 50 * s), next = cw_rect_make(W * 0.5f + 10 * s, H * 0.72f, 170 * s, 50 * s);
    round_rect(retry, 14 * s, rgba(0.25f, 0.25f, 0.3f, 0.95f));
    round_rect(next, 14 * s, rgba(1, 0.5f, 0.12f, 0.95f));
    const char *rl = last_device == DEV_PAD ? "(START) RETRY" : last_device == DEV_KEYS ? "R  RETRY" : "RETRY";
    const char *nl = last_device == DEV_PAD ? "(A) TRACKS" : last_device == DEV_KEYS ? "Enter  TRACKS" : "TRACKS";
    text(rl, retry.x, retry.y + 12 * s, 20 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, retry.w);
    text(nl, next.x, next.y + 12 * s, 20 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, next.w);
  }
}

/* the graphics preset on the pause screen (cartwheel/quality.h): Y, G or a
 * tap steps Low, Medium, High, Ultra. Render-only, so it is read in
 * update, never in the simulation; kept in the save (QUALITY_SAVE) */
static cw_rect gfx_rect(float W, float H) {
  float s = H / 540.0f;
  return cw_rect_make(W - 236 * s, H * 0.27f, 224 * s, 40 * s);
}
static void gfx_input(void) {
  /* read in the fixed tick (the raw presses are there), once per frame however many ticks it runs */
  static uint64_t last_frame = UINT64_MAX;
  if (!G.paused || cw_frame_index() == last_frame) return;
  last_frame = cw_frame_index();
  const cw_raw_input *in = cw_input_raw();
  bool hit = in->keys_pressed[10]; /* G */
  for (int i = 0; i < CW_MAX_PADS; i++) hit |= (in->pads[i].pressed & (1u << CW_PAD_NORTH)) != 0;
  cw_vec2 cs = cw_r2d_canvas_size();
  if ((in->mouse_pressed & 1) && in_rect(mouse_canvas(), gfx_rect(cs.x, cs.y))) hit = true;
  if (!hit) return;
  cw_quality_cycle();
  QUALITY_SAVE = cw_quality_saved();
  save_write();
}
/* " · 1080p": the render resolution beside the graphics preset (the host
 * sets it, RetroArch's Internal Resolution; read-only here) */
/* a label's size so that it fits maxw (shrunk, never grown; text()'s font
 * for that size): the graphics line is longer at 1080p than at 540p */
static float fit_text(const char *s, float size, float maxw) {
  float w = cw_font_measure(size < 20 ? font_ui_bold : font_ui, s, &(cw_text_style){ .size = size }).width;
  return w > maxw && w > 0 ? size * maxw / w : size;
}
static void gfx_res_suffix(char *buf, size_t cap) {
  size_t n = strlen(buf);
  snprintf(buf + n, cap > n ? cap - n : 0, " \xC2\xB7 %s", cw_render_res_label());
}
static void draw_gfx(float W, float H, float s) {
  cw_rect r = gfx_rect(W, H);
  round_rect(r, 12 * s, rgba(0.12f, 0.12f, 0.16f, 0.9f));
  char buf[64];
  snprintf(buf, sizeof buf, "GRAPHICS: %s", cw_quality_label());
  for (char *c = buf; *c; c++) *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
  gfx_res_suffix(buf, sizeof buf); /* the render resolution: the host sets it */
  float sz = fit_text(buf, 18 * s, r.w - 14 * s);
  text(buf, r.x, r.y + 9 * s + (18 * s - sz) * 0.5f, sz, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
  text(last_device == DEV_TOUCH ? "tap to change" : "Y / G to change", r.x, r.y + r.h + 2 * s, 13 * s, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r.w);
}

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0, 0, 0, 0.45f));
  title_text("PAUSED", 0, H * 0.12f, 56 * s, rgba(1, 0.65f, 0.2f, 1), CW_ALIGN_CENTER, W);
  static const char *const items[PAUSE_ITEMS] = { "RESUME", "CHECKPOINT", "RESTART", "TRACKS" };
  for (int i = 0; i < PAUSE_ITEMS; i++) {
    cw_rect r = pause_rect(i, W, H, s);
    round_rect(r, 14 * s, i == G.pause_cursor ? rgba(1, 0.5f, 0.12f, 0.95f) : rgba(0.12f, 0.12f, 0.16f, 0.9f));
    text(items[i], r.x, r.y + 8 * s, 22 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER, r.w);
  }
  controls_card(W * 0.5f - 320 * s, H * 0.27f + PAUSE_ITEMS * 50 * s + 10 * s, 640 * s, s);
  draw_gfx(W, H, s);
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  char b[16];
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  if (G.state == GS_TITLE) draw_title(W, H, s);
  else if (G.state == GS_CHARACTER && NRID > 1) draw_player_views(W, H, s);
  else if (G.state == GS_CHARACTER) draw_carousel(W, H, s);
  else if (G.state == GS_SELECT) draw_select(W, H, s);
  else {
    if (G.state == GS_REPLAY) {
      cw_r2d_rect(cw_rect_make(0, 0, W, H * 0.11f), rgba(0, 0, 0, 0.85f));
      cw_r2d_rect(cw_rect_make(0, H * 0.89f, W, H * 0.11f), rgba(0, 0, 0, 0.85f));
      if ((P->replay_t / 20) & 1) title_text("REPLAY", 24 * s, 14 * s, 26 * s, rgba(1, 0.3f, 0.25f, 1), CW_ALIGN_LEFT, 0);
      text(last_device == DEV_TOUCH ? "tap to skip" : "any button to skip", 0, H * 0.91f, 16 * s, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, W);
      if (P->banner_ticks > 0) title_text(P->banner, 0, H * 0.3f, 84 * s, rgba(1, 0.75f, 0.2f, 1), CW_ALIGN_CENTER, W);
    } else if (NRID > 1) {
      /* a HUD in each rider's view */
      const cw_view *views;
      int nv = cw_pipeline_views(cw_pipeline_of(cw_scene_app_tree()), &views);
      for (int v = 0; v < nv && v < NRID; v++) {
        cw_rect r = cw_rect_make(views[v].x * W, views[v].y * H, views[v].w * W, views[v].h * H);
        cw_xform2d m = { 1, 0, 0, 1, r.x, r.y };
        cw_r2d_set_transform(&m);
        cw_r2d_push_clip(r);
        use_rider(v);
        float k = s * (NRID == 2 ? 0.72f : 0.62f);
        if (P->replay_t > 0) { /* this rider's wipeout replaying in its view */
          cw_r2d_rect(cw_rect_make(0, 0, r.w, r.h * 0.11f), rgba(0, 0, 0, 0.85f));
          cw_r2d_rect(cw_rect_make(0, r.h * 0.89f, r.w, r.h * 0.11f), rgba(0, 0, 0, 0.85f));
          if ((P->replay_t / 20) & 1) title_text("REPLAY", 24 * k, 8 * k, 26 * k, rgba(1, 0.3f, 0.25f, 1), CW_ALIGN_LEFT, 0);
          snprintf(b, sizeof b, "P%d", v + 1);
          title_text(b, 0, 8 * k, 26 * k, rider_col(v, 1), CW_ALIGN_RIGHT, r.w - 24 * k);
          text(v == 0 && last_device == DEV_KEYS ? "Enter to skip" : "(A) to skip", 0, r.h * 0.91f, 16 * k, rgba(1, 1, 1, 0.8f), CW_ALIGN_CENTER, r.w);
          if (P->banner_ticks > 0) title_text(P->banner, 0, r.h * 0.3f, 84 * k, rgba(1, 0.75f, 0.2f, 1), CW_ALIGN_CENTER, r.w);
        } else draw_hud(r.w, r.h, k, NRID == 3);
        cw_r2d_pop_clip();
        cw_r2d_set_transform(NULL);
        /* the splits' seams */
        cw_r2d_rect(cw_rect_make(r.x, r.y + r.h - 1.5f * s, r.w, 3 * s), rgba(0, 0, 0, 1));
        cw_r2d_rect(cw_rect_make(r.x + r.w - 1.5f * s, r.y, 3 * s, r.h), rgba(0, 0, 0, 1));
      }
      use_rider(0);
      if (NRID == 3) draw_shared(cw_rect_make(W * 0.5f, 0, W * 0.5f, H * 0.5f), s);
    } else draw_hud(W, H, s, false);
    if (G.state == GS_FINISH && NRID > 1) {
      float a = cw_clampf(G.state_ticks / 30.0f, 0, 1);
      draw_results(cw_rect_make(W * 0.5f - 250 * s, H * 0.16f, 500 * s, (110 + NRID * 46) * s), s, a);
      if (G.state_ticks > 60) {
        const char *rl = last_device == DEV_PAD ? "(START) RETRY      (A) TRACKS" : "R  RETRY      Enter  TRACKS";
        chip(rl, W * 0.5f, H * 0.16f + (130 + NRID * 46) * s, 20 * s, a);
        text(rl, 0, H * 0.16f + (130 + NRID * 46) * s, 20 * s, rgba(1, 1, 1, a), CW_ALIGN_CENTER, W);
      }
    } else if (G.state == GS_FINISH) draw_finish(W, H, s);
    if (G.paused) draw_pause(W, H, s);
  }
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
#ifdef BIKEMESA_AUTOPILOT
  cw_set_raw_input_filter(autopilot_filter, NULL);
#endif
  static cw_app_desc base = { .title = GAME_TITLE, .width = 960, .height = 540, .debug = true, .init = init,
                              .fixed_update = fixed, .update = update, .render = render, .shutdown = shutdown,
                              .save = &SAVE, .save_size = sizeof SAVE, .touch_pointer = true };
  const char *res = getenv("BIKEMESA_RES");
  if (res && strchr(res, 'x')) {
    base.width = atoi(res);
    base.height = atoi(strchr(res, 'x') + 1);
  }
  return cw_scene_app(NULL, &base);
}
