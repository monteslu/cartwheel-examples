/* racer: the app. main.c owns the race itself (the track, the sim, the world, the
 * cameras and the split-screen views); ui.c owns every other screen and the HUD.
 *
 * Dev aids from the environment, read once:
 *   RACER_MAP=n          straight into map n (0-based)
 *   RACER_PLAYERS=n      humans (1..4: split screen)
 *   RACER_AUTO=1         every kart driven by the CPU (a hands-off run; the _auto build sets it)
 *   RACER_LAPS=n         laps
 *   RACER_CAM=top        a fixed look from above the track (the map's overview)
 *   RACER_VIEW=n         the camera follows kart n (an all-CPU run follows the leader otherwise)
 *   RACER_TRACE=file     log every half second what each kart is doing
 *   RACER_TEST_STEER=x   kart 0 holds steering x at full throttle (a handling test) from race tick 200 (RACER_TEST_STEER_FROM=n)
 *   RACER_BOT=1|keys     the human karts are driven by the CPU driver (see human_in); the debug command `bot 0|1|2` does the same
 *                        for hosts without an environment (2: keyboard-style steering)
 *   RACER_MODE=trial     with RACER_MAP: a time trial (one kart, no items, the map's ghost); RACER_RUNS=n has the bot drive n
 *                        runs in a row, each retried by itself (a deterministic ghost record and replay)
 *   RACER_CLASS=0|1|2    speed class (Standard, Fast, Blazing) for a dev race; RACER_ASSIST=1|2|3 steering (1) and auto
 *                        accelerate (2) assists for the humans
 *   RACER_TEST_AT=s:speed  kart 0 (RACER_TEST_KART=n another) on the road at s metres when the lights go out; L<n>:speed
 *                        30 m before the n-th loop; B<n>:metres:speed on branch n (a shortcut), that far from its mouth
 *   RACER_EYE=x,y,z,tx,ty,tz  a fixed camera there looking at the target, no HUD (a still of any part of a map)
 *   RACER_JITLOG=1       log each frame the drawn distance from view 0's camera to its kart (with CW_FRAME_HZ: is it steady);
 *                        RACER_NOINTERP=1 draws the karts at the tick rate, the old way, as a control
 *   RACER_SPLIT=a:b      log each kart's time from progress a to b (metres): what a shortcut is worth
 *   RACER_AI_TAKE=0|1    every CPU driver stays out of (0) or takes (1) every shortcut; RACER_AILOG=1 logs their choices
 *   RACER_MODE=battle    with RACER_MAP=n: a battle in arena n (0 Crusher Yard, 1 Foundry Pit); RACER_KARTS=2..8 karts in it
 *                        (humans first, CPUs fill), RACER_BATTLE_TIME=secs the round's length (three minutes otherwise)
 *   RACER_VOICE_LOG=file the announcer (announcer.c) logs every line it says, drops or cuts, with the time (1: the
 *                        engine log); tools/voice-log.mjs reads the logs back (pacing, overlaps)
 *
 * Battle (sim_battle.inc, ai_battle.c, arena.c, balloons.c, ui_battle.inc): race_setup.mode RM_BATTLE puts ARENAS[map] on
 * instead of a race map. The sim knows it by map_def.arena (sim.battle); the CPU drivers switch to ai_battle_think; a view whose
 * kart is out follows the leader that is still in (spectate). Debug command `battle <arena> [humans] [secs] [karts]`; debug
 * fields battle.* and karts.balloons / karts.points / karts.out are in the racer-battle gate. RACER_UI=arena opens the arena
 * select (RACER_UI_MAP=n picks the arena). In a battle RACER_TRACE logs every kart's place, speed, balloons, points and item
 * every half second (R: its CPU driver is backing out), and with RACER_TRACE_KART=n that kart every tick with where its
 * driver aims and what its feelers see (games/racer/ai_battle.c).
 *
 * Time trial (ghost.c, ui_trial.inc): the race has one kart and no item boxes. game_retry() puts the same race back on the
 * grid without a reload (sim_restart: a short countdown). The human kart's pose is recorded every third tick (ghost_rec_*);
 * ui.c keeps the best run in the save data and passes it back in race_setup.ghost, which game_start_race loads and draws
 * as a translucent kart (render only: not a body in the sim). Debug commands: `trial <map> [runs]`, `bot <0|1|2>`;
 * debug fields trial.*, ghost.* are in the gates. Assists (assist.c) and auto accelerate are applied in human_in. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/pipeline.h"
#include "cartwheel/platform.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "arena.h"
#include "ui.h"
#include "world.h"

typedef struct rig {
  cw_node cam;
  t3_vec3 eye, eye_v, look, look_v, up, fwd, cam_eye, cam_at;
  t3_vec3 erel, lrel; /* the chase camera: eye and look point relative to the kart (springs in eye_v, look_v) */
  bool live, snap, chase;
  float spd_s; /* the chase camera's speed, smoothed: the raw one shivers with the suspension every tick */
  float base_fov, fov_k;
} rig;

static struct {
  int32_t ticks, phase, views, loaded;
  int32_t draws, tris;
  int32_t place[MAXK], lap[MAXK], prog[MAXK], falls[MAXK], cuts[MAXK], speed[MAXK], tricks[MAXK], hits[MAXK], spd_max[MAXK], finished[MAXK], best_lap[MAXK], finish_tick[MAXK];
  int32_t balloons[MAXK], points[MAXK], out[MAXK], battle_left, battle_end, battle_alive;
  int spect[CW_MAX_VIEWS]; /* battle: the kart a view watches once its own is out (-1: none yet) */
  uint32_t hash;
  cw_node scene, sun;
  track trk;
  bool have_track;
  sim S;
  ai_mem ai[MAXK];
  bool human[MAXK];
  int human_of[MAXK]; /* kart -> view, or -1 */
  int view_kart[CW_MAX_VIEWS];
  int pl[CW_MAX_VIEWS]; /* the input player behind each view */
  int nviews;
  int cam_mode;
  float cam_t;
  rig rig[CW_MAX_VIEWS];
  bool auto_all;
  race_setup setup;
  bool trial, assist_steer, assist_gas;
  int bot;            /* 0 hands, 1 the CPU driver, 2 the CPU driver with keyboard-style steering */
  uint32_t seed;
  int32_t ghost_rec_n;
  uint32_t ghost_rec_hash, ghost_play_hash;
  cw_action a_fwd, a_back, a_left, a_right, a_pup, a_pdn, a_drift, a_item, a_trick, a_look, a_reset;
} G;

static cw_node find_type(cw_node n, const char *type) {
  if (cw_node_is(n, type)) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_type(c, type);
    if (cw_node_valid(r)) return r;
  }
}

/* ── the game API the UI uses ───────────────────────────────────────── */

bool game_loaded(void) { return G.have_track; }
const sim *game_sim(void) { return &G.S; }
int game_views(void) { return G.nviews; }
int game_planned_views(void) { return G.setup.humans < 1 ? 1 : G.setup.humans > MAXV ? MAXV : G.setup.humans; }
int game_view_player(int v) { return G.pl[v]; }
void game_set_camera(int mode) {
  if (mode == 2 && G.nviews > 1) {
    /* the results: one big view of the winner, not the split screen */
    cw_pipeline *pl = cw_pipeline_of(cw_scene_app_tree());
    cw_pipeline_set_views(pl, NULL, 0);
    cw_pipeline_set_views_shared_post(pl, false);
    G.nviews = 1;
    G.views = 1;
    G.rig[0].base_fov = 64;
  }
  G.cam_mode = mode;
  G.cam_t = 0;
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.rig[v].live = false;
}

int game_view_own_kart(int v) { return v >= 0 && v < G.nviews ? G.view_kart[v] : -1; }

int game_view_kart(int v) {
  /* battle: a view whose kart is out watches another (spectate; camera_tick picks it) */
  if (v >= 0 && v < G.nviews && G.view_kart[v] >= 0 && G.S.battle && G.S.k[G.view_kart[v]].parked && G.spect[v] >= 0) return G.spect[v];
  if (v >= 0 && v < G.nviews && G.view_kart[v] >= 0) return G.view_kart[v];
  static int dev_view = -2;
  if (dev_view == -2) dev_view = getenv("RACER_VIEW") ? atoi(getenv("RACER_VIEW")) % MAXK : -1;
  if (dev_view >= 0) return dev_view;
  return G.S.order[0];
}

void game_view_rect(int v, float W, float H, float *x, float *y, float *w, float *h) {
  int n = G.nviews;
  if (n < 2) {
    *x = *y = 0, *w = W, *h = H;
    return;
  }
  if (n == 2) {
    bool stacked = W >= H;
    *x = stacked ? 0 : W * 0.5f * (float)v;
    *y = stacked ? H * 0.5f * (float)v : 0;
    *w = stacked ? W : W * 0.5f;
    *h = stacked ? H * 0.5f : H;
    return;
  }
  static const int Q3[3] = { 0, 2, 3 };
  int q = n == 3 ? Q3[v] : v;
  *x = (q & 1) ? W * 0.5f : 0;
  *y = (q >> 1) ? H * 0.5f : 0;
  *w = W * 0.5f;
  *h = H * 0.5f;
}

/* ── loading a race ─────────────────────────────────────────────────── */

static void unload(void) {
  if (G.have_track) {
    audio_race_stop();
    balloons_reset();
    ghost_gfx_reset();
    ghost_unload();
    fx_reset();
    items_reset();
    sim_free(&G.S);
    world_reset();
    track_free(&G.trk);
    G.have_track = false;
  }
  if (cw_node_valid(G.scene)) cw_node_free(G.scene);
  G.scene = (cw_node){ 0 };
  G.loaded = 0;
}

void game_end_race(void) { unload(); }
bool game_is_trial(void) { return G.have_track && G.trial; }
bool game_commit_ghost(ghost_saved *dst, uint32_t time_cs) { return G.trial && ghost_rec_commit(dst, time_cs); }

static float steer_smooth[CW_MAX_VIEWS];

void game_set_ghost(const ghost_saved *g) {
  if (!G.have_track || !G.trial) return;
  ghost_gfx_reset();
  ghost_unload();
  if (g && ghost_load(g, &G.trk)) ghost_gfx_build(G.scene);
}

void game_retry(void) {
  if (!G.have_track || !G.trial) return;
  sim_restart(&G.S, G.seed, 60);
  memset(steer_smooth, 0, sizeof steer_smooth);
  for (int i = 0; i < MAXK; i++) ai_reset(&G.ai[i], i, 2, 100u + (uint32_t)i);
  ghost_rec_start(&G.S.k[0]);
  world_tick(&G.S);
  ghost_gfx_tick(&G.S);
  for (int i = 0; i < MAXK; i++)
    if (G.S.k[i].on && cw_node_valid(kartgfx_node(i))) cw_node3d_snapshot(kartgfx_node(i));
  audio_race_start(&G.S, G.S.md);
  game_set_camera(0);
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.rig[v].snap = true;
  G.phase = G.S.phase;
}

static void views_setup(void) {
  int n = 0;
  for (int i = 0; i < MAXK; i++) n += G.human_of[i] >= 0;
  G.nviews = n < 1 ? 1 : n;
  G.views = G.nviews;
  cw_node cam0 = find_type(G.scene, "Camera3D");
  memset(G.rig, 0, sizeof G.rig);
  G.rig[0].cam = cam0;
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.rig[v].snap = true;
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.view_kart[v] = -1;
  for (int i = 0; i < MAXK; i++)
    if (G.human_of[i] >= 0) G.view_kart[G.human_of[i]] = i;
  cw_pipeline *pl = cw_pipeline_of(cw_scene_app_tree());
  if (G.nviews < 2) {
    G.rig[0].base_fov = 64;
    cw_pipeline_set_views(pl, NULL, 0);
    cw_pipeline_set_views_shared_post(pl, false);
    return;
  }
  int ww = 0, wh = 0;
  cw_window_size(&ww, &wh);
  bool stacked = G.nviews == 2 && ww >= wh;
  float fov = stacked ? 48 : 64;
  cw_view vw[CW_MAX_VIEWS];
  static const float Q[CW_MAX_VIEWS][2] = { { 0, 0 }, { 0.5f, 0 }, { 0, 0.5f }, { 0.5f, 0.5f } };
  static const int Q3[3] = { 0, 2, 3 };
  for (int v = 0; v < G.nviews; v++) {
    cw_node c = cam0;
    if (v > 0) {
      char name[16];
      snprintf(name, sizeof name, "Camera%d", v + 1);
      c = cw_node_new("Camera3D", name);
      cw_node_add_child(G.scene, c);
      cw_node_set(c, "near", cw_val_float(0.2f));
      cw_node_set(c, "far", cw_val_float(2600));
      cw_node_set(c, "interpolate", cw_val_bool(true));
      G.rig[v].cam = c;
    }
    cw_node_set(c, "fov", cw_val_float(fov));
    G.rig[v].base_fov = fov;
    if (G.nviews == 2) vw[v] = stacked ? (cw_view){ c, 0, 0.5f * v, 1, 0.5f } : (cw_view){ c, 0.5f * v, 0, 0.5f, 1 };
    else {
      int q = G.nviews == 3 ? Q3[v] : v;
      vw[v] = (cw_view){ c, Q[q][0], Q[q][1], 0.5f, 0.5f };
    }
  }
  cw_pipeline_set_views(pl, vw, G.nviews);
  cw_pipeline_set_views_shared_post(pl, true);
  cw_node post = find_type(G.scene, "PostProcess");
  /* no ambient occlusion in split screen, and so no depth prepass to feed it: that pass only doubles the draw calls of the views */
  if (cw_node_valid(post)) cw_node_set(post, "ssao", cw_val_bool(false)), cw_node_set(post, "depth_prepass", cw_val_bool(false));
}

void game_start_race(const race_setup *rs) {
  unload();
  G.setup = *rs;
  bool battle = rs->mode == RM_BATTLE && !rs->attract;
  int map = rs->map < 0 ? -1 : rs->map >= NMAPS ? NMAPS - 1 : rs->map;
  if (battle) map = rs->map < 0 ? 0 : rs->map % NARENAS;
  const map_def *md = battle ? ARENAS[map] : map < 0 ? &map_test : MAPS[map];
  cw_tree *t = cw_scene_app_tree();
  G.scene = cw_scene_instance_path(md->scene);
  if (CW_HANDLE_IS_NULL(G.scene)) {
    cw_error("racer: %s failed to load", md->scene);
    return;
  }
  cw_node_add_child(cw_tree_root(t), G.scene);
  G.sun = cw_node_get(G.scene, "Sun");
  cw_node cam = find_type(G.scene, "Camera3D");
  if (cw_node_valid(cam)) cw_node_set(cam, "interpolate", cw_val_bool(true));
  if (!track_build(&G.trk, md->spec)) {
    cw_error("racer: the track of map %d did not build", map);
    return;
  }
  G.have_track = true;
  bool on[MAXK];
  int veh[MAXK];
  memset(G.human, 0, sizeof G.human);
  G.trial = rs->mode == RM_TRIAL && !rs->attract;
  G.assist_steer = rs->assist_steer, G.assist_gas = rs->assist_gas;
  int humans = G.auto_all ? 0 : rs->humans > MAXV ? MAXV : rs->humans;
  /* a battle can have fewer karts: the humans and enough CPUs to make it up */
  int nk = battle && rs->karts > 0 ? CW_CLAMP(rs->karts, humans > 2 ? humans : 2, MAXK) : MAXK;
  for (int i = 0; i < MAXK; i++) {
    on[i] = (!G.trial || i == 0) && i < nk; /* a time trial has one kart */
    veh[i] = (i * 5 + 1) % NVEH;
    G.human_of[i] = -1;
  }
  if (G.trial) humans = G.auto_all ? 0 : 1;
  for (int i = 0; i < humans; i++) {
    G.human[i] = true;
    G.human_of[i] = i;
    veh[i] = rs->vehicle[i] % NVEH;
  }
  /* models agent, dev aid: RACER_STUDIO=<vehicle 0..5> makes kart 0 that vehicle (RACER_COLOUR=n paints it) and the camera orbits it */
  if (getenv("RACER_STUDIO") && !humans) veh[0] = atoi(getenv("RACER_STUDIO")) % NVEH;
  G.seed = 7u + (uint32_t)(map + 1) * 101u + (battle ? 5000u : 0u);
  /* a battle's sim.map is 100 + the arena, so nothing that keys on a race map (best laps, the outlines) takes it for one */
  sim_init(&G.S, &G.trk, md, battle ? 100 + map : map, G.trial ? 1 : rs->laps > 0 ? rs->laps : md->laps, on, veh, G.seed);
  sim_set_class(&G.S, G.trial ? CLASS_STANDARD : rs->speed_class);
  if (battle) sim_set_battle_time(&G.S, rs->battle_secs * TICK_HZ);
  /* the item boxes option (and the time trial) leaves the road without boxes; nothing is built for them (a battle always has them) */
  if ((!rs->items && !battle) || G.trial) G.S.nbox = 0;
  G.S.item_set = rs->turbos_only || getenv("RACER_TURBOS") ? 1 : 0; /* dev aid: RACER_TURBOS=1 forces the turbos-only preset */
  for (int i = 0; i < MAXK; i++) {
    G.S.k[i].human = G.human[i];
    G.S.k[i].colour = i < humans ? rs->colour[i] & 7 : (i + 3) & 7;
    /* Mixed, the default: one Ace, two Pros, the rest Rookies (three Aces made the default too hard) */
    int skill = rs->cpu_skill >= 3 ? (i < 2 ? 2 : i < 4 ? 1 : 0) : rs->cpu_skill;
    ai_reset(&G.ai[i], i, skill, 100u + (uint32_t)i);
  }
  if (getenv("RACER_STUDIO") && getenv("RACER_COLOUR") && !humans) G.S.k[0].colour = atoi(getenv("RACER_COLOUR")) & 7;
  if (battle) {
    /* a battle tells karts apart by their balloons: the CPU karts take the paints no human has */
    bool used[8] = { false };
    for (int i = 0; i < humans; i++) used[G.S.k[i].colour & 7] = true;
    int c = 0;
    for (int i = humans; i < MAXK; i++) {
      while (c < 8 && used[c]) c++;
      G.S.k[i].colour = c < 8 ? c : (i + 3) & 7;
      if (c < 8) used[c] = true;
    }
  }
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.pl[v] = v;
  world_build(G.scene, &G.S, md);
  if (G.trial && rs->ghost && ghost_load(rs->ghost, &G.trk)) ghost_gfx_build(G.scene);
  if (G.trial) ghost_rec_start(&G.S.k[0]);
  items_build(G.scene, &G.S);
  balloons_build(G.scene, &G.S);
  fx_init(G.scene, &G.S, md);
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.spect[v] = -1;
  views_setup();
  audio_race_start(&G.S, md);
  G.cam_mode = rs->attract ? 1 : 0;
  G.cam_t = 0;
  G.loaded = 1;
  cw_info("racer: map %d (%s), %d humans, %d laps, track %.0f m", map, md->name, humans, G.S.laps, G.trk.length);
}

/* ── input ──────────────────────────────────────────────────────────── */

static int8_t q127(float v) { return (int8_t)(cw_clampf(v, -1, 1) * 127.0f); }

/* the CPU driver for kart i: the racing line, or the battle driver in an arena */
static kin cpu_think(int i) { return G.S.battle ? ai_battle_think(&G.S, i, &G.ai[i]) : ai_think(&G.S, i, &G.ai[i]); }

static kin human_in(int kart) {
  int p = G.pl[G.human_of[kart]];
  kin in = { 0 };
  {
    /* dev aid: RACER_BOT=1 drives the human karts with the CPU driver through the human path (a whole split-screen race without hands) */
    if (G.bot) {
      kin b = cpu_think(kart);
      /* the CPU driver never tricks: the bot taps the button once it has been airborne a while */
      if (G.S.k[kart].air_ticks > 14 && G.S.k[kart].air_ticks < 18) b.btn |= KB_TRICK;
      if (G.bot == 2) {
        /* RACER_BOT=keys: a keyboard driver, steering all or nothing through the same easing as a held key */
        float want = fabsf(b.steer) < 20 ? 0.0f : (b.steer > 0 ? 1.0f : -1.0f), *ss = &steer_smooth[G.human_of[kart]];
        *ss += (want - *ss) * (fabsf(want) > fabsf(*ss) ? 0.13f : 0.24f);
        b.steer = q127(*ss);
        b.btn &= ~KB_DRIFT;
      }
      return b;
    }
  }
  in.thr = q127(cw_action_strength(p, G.a_fwd) - cw_action_strength(p, G.a_back));
  if (G.assist_gas) {
    /* auto accelerate: the throttle is held unless the brake is pressed. Before the lights it stays the player's own (a
     * held throttle would flood the engine), but the rocket start's moment is given: it costs no speed to use the assist */
    const sim *S = &G.S;
    float br = cw_action_strength(p, G.a_back);
    if (S->phase == PH_RACE) in.thr = br > 0.05f ? q127(-br) : 127;
    else if (S->phase == PH_COUNT && S->phase_ticks >= 166 && br < 0.05f) in.thr = 127;
  }
  /* a key is all or nothing: ease it in and out so a tap is a nudge and a hold a full lock (about a fifth of a second
   * to full lock); a stick gets a softer centre, so small corrections stay small */
  float raw = cw_input_axis(p, G.a_left, G.a_right), *ss = &steer_smooth[G.human_of[kart]];
  raw = raw * (0.45f + 0.55f * fabsf(raw));
  *ss += (raw - *ss) * (fabsf(raw) > fabsf(*ss) ? 0.13f : 0.24f);
  in.steer = q127(*ss);
  in.pitch = q127(cw_action_strength(p, G.a_pup) - cw_action_strength(p, G.a_pdn));
  if (cw_action_pressed(p, G.a_drift)) in.btn |= KB_DRIFT;
  if (cw_action_pressed(p, G.a_item)) in.btn |= KB_ITEM;
  if (cw_action_pressed(p, G.a_trick)) in.btn |= KB_TRICK;
  if (cw_action_pressed(p, G.a_reset)) in.btn |= KB_RESET;
  if (cw_action_pressed(p, G.a_look)) in.btn |= KB_BACK;
  if (G.assist_steer) assist_steer(&G.S, kart, &in);
  return in;
}

/* ── cameras (fixed tick) ───────────────────────────────────────────── */

static void spring3(t3_vec3 *x, t3_vec3 *v, t3_vec3 target, float w) {
  const float dt = 1.0f / 60;
  t3_vec3 a = { (target.x - x->x) * w * w - v->x * 2 * w, (target.y - x->y) * w * w - v->y * 2 * w, (target.z - x->z) * w * w - v->z * 2 * w };
  v->x += a.x * dt, v->y += a.y * dt, v->z += a.z * dt;
  x->x += v->x * dt, x->y += v->y * dt, x->z += v->z * dt;
}

static void rig_tick(int v) {
  rig *r = &G.rig[v];
  if (!cw_node_valid(r->cam)) return;
  const sim *s = &G.S;
  int me = game_view_kart(v);
  const kart *k = &s->k[me];
  t3_vec3 e, at, upv = { 0, 1, 0 };
  bool was_chase = r->chase;
  r->chase = false;
  static int dev_top = -1;
  if (dev_top < 0) dev_top = getenv("RACER_CAM") && !strcmp(getenv("RACER_CAM"), "top");
  static int dev_eye = -1;
  static t3_vec3 eye_at[2];
  if (dev_eye < 0) /* dev aid: RACER_EYE=x,y,z,tx,ty,tz holds the camera there looking at the target (no HUD) */
    dev_eye = getenv("RACER_EYE") && sscanf(getenv("RACER_EYE"), "%f,%f,%f,%f,%f,%f", &eye_at[0].x, &eye_at[0].y, &eye_at[0].z, &eye_at[1].x, &eye_at[1].y, &eye_at[1].z) == 6;
  if (dev_eye && v == 0) {
    e = eye_at[0], at = eye_at[1];
    r->snap = true;
  } else if (dev_top && v == 0) {
    const track *t = s->track;
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (int i = 0; i < t->n; i += 8) {
      minx = fminf(minx, t->s[i].p.x), maxx = fmaxf(maxx, t->s[i].p.x);
      minz = fminf(minz, t->s[i].p.z), maxz = fmaxf(maxz, t->s[i].p.z);
    }
    float cx = (minx + maxx) * 0.5f, cz = (minz + maxz) * 0.5f, ext = fmaxf(maxx - minx, maxz - minz);
    e = (t3_vec3){ cx, ext * 1.05f, cz - 1 };
    at = (t3_vec3){ cx, 0, cz };
    upv = (t3_vec3){ 0, 0, 1 };
    r->snap = true;
  } else if (getenv("RACER_STUDIO") && v == 0 && G.cam_mode != 2) {
    /* models agent, dev aid: orbit kart 0 in its own frame. RACER_STUDIO_CAM=azimuth,distance,height (degrees: 0 behind, 90 left,
     * 180 front, 270 right; metres) holds a view, otherwise a slow turntable */
    static float studio_t;
    float az = studio_t * 24.0f, dist = 4.2f, hgt = 1.3f;
    if (getenv("RACER_STUDIO_CAM")) sscanf(getenv("RACER_STUDIO_CAM"), "%f,%f,%f", &az, &dist, &hgt);
    studio_t += 1.0f / 60;
    const kart *sk = &s->k[0];
    float a = az * 0.0174533f;
    t3_vec3 off = v3add(v3mul(kart_fwd(sk), -cw_cos(a) * dist), v3mul(kart_right(sk), -cw_sin(a) * dist));
    e = v3add(v3add(sk->p, off), v3mul(kart_up(sk), hgt));
    at = v3add(sk->p, v3mul(kart_up(sk), 0.25f));
    upv = kart_up(sk);
    r->snap = true;
  } else if (G.cam_mode == 1) {
    /* a demo: a camera flying along the track a little ahead of the leader, to one side and above it */
    float sd = k->loc.ms + 26.0f;
    float side = 7.0f + 4.0f * cw_sin(G.cam_t * 0.31f);
    tsample m = track_at(s->track, sd);
    t3_vec3 base = v3add(m.p, v3add(v3mul(m.r, side), v3mul(m.u, 4.0f + 2.0f * cw_sin(G.cam_t * 0.5f))));
    if (!r->live) r->eye = base, r->eye_v = (t3_vec3){ 0, 0, 0 }, r->look = k->p, r->look_v = (t3_vec3){ 0, 0, 0 }, r->live = true;
    spring3(&r->eye, &r->eye_v, base, 2.2f);
    spring3(&r->look, &r->look_v, v3add(k->p, v3mul(kart_fwd(k), 6)), 3.5f);
    e = r->eye, at = r->look;
    G.cam_t += 1.0f / 60;
  } else if (G.cam_mode == 2) {
    /* the results: round the winner */
    float a = G.cam_t * 0.35f;
    const kart *w = &s->k[s->order[0]];
    tsample m = track_at_loc(s->track, &w->loc, 0);
    e = v3add(w->p, v3add(v3mul(m.r, 7.5f * cw_sin(a)), v3add(v3mul(m.f, 7.5f * cw_cos(a)), v3mul(m.u, 2.6f))));
    at = v3add(w->p, v3mul(m.u, 0.8f));
    r->live = false;
    G.cam_t += 1.0f / 60;
  } else {
    t3_vec3 up = kart_up(k), f = kart_fwd(k);
    /* the camera's own up and forward follow the road's and the kart's, smoothed: a loop turns the view with the road */
    t3_vec3 tsup = up;
    if (k->loc.on_road || k->grounded) {
      tsample m = track_at_loc(s->track, &k->loc, 0);
      tsup = v3norm(v3add(v3mul(m.u, 0.7f), v3mul(up, 0.3f)));
    }
    float spd = v3len(k->v);
    t3_vec3 want_f = f;
    if (spd > 6) {
      t3_vec3 vf = v3norm(k->v);
      want_f = v3norm(v3add(v3mul(f, 0.55f), v3mul(vf, 0.45f)));
    }
    bool back = (k->last_btn & KB_BACK) != 0;
    if (!r->live) r->up = tsup, r->fwd = want_f;
    r->up = v3norm(v3add(v3mul(r->up, 1 - 0.07f), v3mul(tsup, 0.07f)));
    r->fwd = v3norm(v3add(v3mul(r->fwd, 1 - 0.09f), v3mul(want_f, 0.09f)));
    t3_vec3 fh = v3norm(v3sub(r->fwd, v3mul(r->up, v3dot(r->fwd, r->up))));
    float split = G.nviews > 1 ? 0.88f : 1.0f;
    if (!r->live) r->spd_s = spd;
    r->spd_s += (spd - r->spd_s) * 0.04f;
    float dist = (4.9f + r->spd_s * 0.03f) * split, high = (2.0f + r->spd_s * 0.008f) * split;
    if (back) fh = v3mul(fh, -1);
    /* the springs run on the eye and look point relative to the kart: a world-space spring trails a moving target by
     * 2v/w (ten metres at 30 m/s), so only the swing round corners and over crests lags, never the distance */
    t3_vec3 erel = v3add(v3mul(fh, -dist), v3mul(r->up, high));
    t3_vec3 lrel = v3add(v3mul(fh, 5.0f), v3mul(r->up, 0.9f));
    if (!r->live || !was_chase) {
      r->erel = erel, r->lrel = lrel;
      r->eye_v = r->look_v = (t3_vec3){ 0, 0, 0 };
      r->live = true;
    }
    r->chase = true;
    if (back) r->erel = erel, r->lrel = lrel;
    spring3(&r->erel, &r->eye_v, erel, 6.0f);
    spring3(&r->lrel, &r->look_v, lrel, 9.0f);
    r->eye = v3add(k->p, r->erel), r->look = v3add(k->p, r->lrel);
    e = r->eye, at = r->look;
    upv = r->up;
    /* the field of view opens with speed and a boost: the world rushes */
    float want_k = cw_clampf((spd - 24.0f) / 32.0f, 0, 1) + (k->boost_ticks > 0 ? 0.6f : 0);
    r->fov_k += (want_k - r->fov_k) * 0.06f;
    if (r->base_fov > 0) cw_node_set(r->cam, "fov", cw_val_float(r->base_fov + 13.0f * r->fov_k));
    /* the shake: across the view and up and down only, a few times a second. It was 0.25 m at 15 to 26 Hz in all three
     * axes, near what a 60 Hz tick can show at all: along the view it made the kart twitch toward and away */
    float sh = fx_shake(me);
    if (sh > 0.001f) {
      t3_vec3 rt = v3norm(v3cross(fh, r->up));
      float tt = (float)G.ticks;
      e = v3add(e, v3mul(rt, sh * 0.07f * cw_sin(tt * 0.47f + (float)v)));
      e = v3add(e, v3mul(r->up, sh * 0.06f * cw_sin(tt * 0.61f + 1 + (float)v)));
    }
  }
  r->cam_eye = e, r->cam_at = at;
  bool snapped = r->snap;
  r->snap = false;
  cw_node3d_snapshot(r->cam);
  cw_node3d_set_position(r->cam, e);
  cw_node3d_look_at(r->cam, at, upv);
  if (snapped) cw_node3d_snapshot(r->cam);
}

static void camera_tick(void) {
  if (!G.have_track) return;
  /* battle: a view whose kart is out watches the leader still in, and keeps watching it until that one is out too */
  if (G.S.battle)
    for (int v = 0; v < G.nviews; v++) {
      int own = G.view_kart[v];
      if (own < 0 || !G.S.k[own].parked) continue;
      int cur = G.spect[v];
      if (cur >= 0 && !G.S.k[cur].out) continue;
      int pick = -1;
      for (int r = 0; r < MAXK && pick < 0; r++) {
        int ki = G.S.order[r];
        if (G.S.k[ki].on && !G.S.k[ki].out) pick = ki;
      }
      if (pick >= 0 && pick != cur) G.spect[v] = pick, G.rig[v].live = false, G.rig[v].snap = true;
    }
  for (int v = 0; v < G.nviews; v++) rig_tick(v);
  /* the sun's shadow box follows the play */
  if (cw_node_valid(G.sun)) {
    const t3_mat4 *m = cw_node3d_global_transform(G.sun);
    float fx = -m->e[8], fy = -m->e[9], fz = -m->e[10], l = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= l, fy /= l, fz /= l;
    t3_vec3 c = { 0, 0, 0 };
    for (int v = 0; v < G.nviews; v++) {
      c.x += G.rig[v].cam_at.x / (float)G.nviews;
      c.y += G.rig[v].cam_at.y / (float)G.nviews;
      c.z += G.rig[v].cam_at.z / (float)G.nviews;
    }
    cw_node3d_set_position(G.sun, (t3_vec3){ c.x - fx * 80, c.y - fy * 80, c.z - fz * 80 });
  }
}

/* ── the fixed tick ─────────────────────────────────────────────────── */

static void trace(void) {
  static FILE *tr;
  static bool tr_checked;
  if (!tr_checked) {
    tr_checked = true;
    if (getenv("RACER_TRACE")) tr = fopen(getenv("RACER_TRACE"), "w");
  }
  {
    /* dev aid: RACER_SPLIT=a:b logs each kart's time from progress a to progress b (metres; a shortcut's worth) */
    static float sa = -1, sb = -1, last[MAXK];
    static int t0[MAXK];
    if (sa < 0 && getenv("RACER_SPLIT")) sscanf(getenv("RACER_SPLIT"), "%f:%f", &sa, &sb);
    if (sa >= 0)
      for (int i = 0; i < MAXK; i++) {
        const kart *k = &G.S.k[i];
        if (!k->on) continue;
        if (last[i] < sa && k->prog >= sa) t0[i] = G.S.tick;
        if (last[i] < sb && k->prog >= sb && t0[i]) cw_info("split kart %d: %.2f s (shortcuts %d)", i, (float)(G.S.tick - t0[i]) / TICK_HZ, k->cuts), t0[i] = 0;
        last[i] = k->prog;
      }
  }
  if (!tr) return;
  const char *tk = getenv("RACER_TRACE_KART");
  if (tk && !G.S.battle) {
    int from = getenv("RACER_TRACE_FROM") ? atoi(getenv("RACER_TRACE_FROM")) : 880;
    if (G.S.tick < from || G.S.tick >= from + 220) return;
    const kart *k = &G.S.k[atoi(tk)];
    const cw_vehicle3d *c = sim_car(&G.S, atoi(tk));
    fprintf(tr, "t%d br=%d ms=%.1f s=%.1f lat=%.2f h=%.2f spd=%.1f vy=%.1f %s i=%d pitch=%.1f falls=%d spin=%d st=%d yaw=%.2f lv=%.1f ct=%d c=%.2f/%.2f/%.2f/%.2f up.y=%.2f p=(%.1f %.1f %.1f)\n", G.S.tick,
            k->loc.br, k->loc.ms, k->loc.s, k->loc.lat, k->loc.height, k->speed, k->v.y, k->grounded ? "G" : "air", k->loc.i, kart_fwd(k).y, k->falls, k->spin_ticks,
            (int)(k->steer * 100), v3dot(k->w, kart_up(k)), v3dot(k->v, kart_right(k)), c->contacts, c->wheel[0].compression, c->wheel[1].compression,
            c->wheel[2].compression, c->wheel[3].compression, kart_up(k).y, k->p.x, k->p.y, k->p.z);
    return;
  }
  if (G.S.battle && getenv("RACER_TRACE_KART")) {
    /* one kart of a battle every tick: where it is and where its driver aims */
    int ti = atoi(getenv("RACER_TRACE_KART")) & 7;
    const kart *k = &G.S.k[ti];
    const ai_mem *m = &G.ai[ti];
    fprintf(tr, "t%d p=(%.1f %.1f %.1f) spd=%.1f fwd=(%.2f %.2f) aim=(%.1f %.1f %.1f) feel=%.2f/%.2f/%.2f rev=%d slow=%d frust=%d roam=%d box=%d tgt=%d btn=%d spin=%d thr=%d\n", G.S.tick, k->p.x, k->p.z - 100, k->p.y, k->speed,
            kart_fwd(k).x, kart_fwd(k).z, m->b_aim.x, m->b_aim.z - 100, m->b_aim.y, m->b_feel[0], m->b_feel[1], m->b_feel[2], m->b_reverse, m->b_slow, m->b_frust, m->b_roam, m->b_box, m->b_target, (int)G.S.k[ti].last_btn, k->spin_ticks, m->b_thr);
    return;
  }
  /* the last tick each kart was spinning, backing out or put back (for the causes below) */
  static int last_spin[MAXK], last_rev[MAXK], last_back[MAXK];
  for (int i = 0; i < MAXK && G.S.battle; i++) {
    if (G.S.k[i].spin_ticks > 0) last_spin[i] = G.S.tick;
    if (G.ai[i].b_reverse > 0) last_rev[i] = G.S.tick;
    if (G.S.k[i].respawn_ticks > 0) last_back[i] = G.S.tick;
  }
  if (G.S.tick % 30) return;
  if (G.S.battle) {
    /* a battle: where each kart is, its speed, balloons, points, item and whether its driver is backing out */
    fprintf(tr, "%d left %d ph%d", G.S.tick, G.S.battle_left / TICK_HZ, G.S.phase);
    for (int i = 0; i < MAXK; i++) {
      const kart *k = &G.S.k[i];
      if (!k->on) continue;
      /* f= what is going on round it: S spinning, K another kart within 4 m, W a wall within a third of its feeler, G its aim
       * within 8 m, H getting going after a spin (1.5 s), C after backing out (1 s), P the start or a put-back (2.5 s), then its
       * throttle + 0 - */
      char fl[12] = "", *fp = fl;
      if (k->spin_ticks == 0 && G.S.tick - last_spin[i] < 90 && last_spin[i] > 0) *fp++ = 'H';
      if (G.ai[i].b_reverse == 0 && G.S.tick - last_rev[i] < 60 && last_rev[i] > 0) *fp++ = 'C';
      if (G.S.race_ticks < 150 || (last_back[i] > 0 && G.S.tick - last_back[i] < 150)) *fp++ = 'P';
      bool near_k = false;
      for (int j = 0; j < MAXK; j++)
        if (j != i && G.S.k[j].on && !G.S.k[j].out && v3len(v3sub(G.S.k[j].p, k->p)) < 4.0f) near_k = true;
      if (k->spin_ticks > 0) *fp++ = 'S';
      if (near_k) *fp++ = 'K';
      if (G.ai[i].b_feel[1] < 0.33f) *fp++ = 'W';
      if (v3len(v3sub(G.ai[i].b_aim, k->p)) < 8.0f) *fp++ = 'G';
      *fp++ = G.ai[i].b_thr > 0 ? '+' : G.ai[i].b_thr < 0 ? '-' : '0';
      *fp = 0;
      fprintf(tr, " | %.0f,%.0f,%.0f %.0f b%d p%d i%d f=%s%s%s", k->p.x, k->p.z - 100, k->p.y, k->speed, k->balloons, k->points, k->item, fl, G.ai[i].b_reverse ? " R" : "",
              k->out ? " OUT" : "");
    }
    fprintf(tr, "\n");
    fflush(tr);
    return;
  }
  fprintf(tr, "%d", G.S.tick);
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &G.S.k[i];
    fprintf(tr, " | %.0f %.1f%s%d f%d", k->prog, k->speed, k->grounded ? "" : "^", k->place, k->falls);
  }
  fprintf(tr, "\n");
  fflush(tr);
}

static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  G.ticks++;
  ui_fixed();
  audio_pause(G.have_track && !ui_race_runs());
  if (G.have_track && ui_race_runs()) {
    kin in[MAXK] = { { 0 } };
    /* the bot starts every run from the same driving state, at the same moment of the countdown (a retry joins it at 60 of 180), so
     * runs repeat; the CPU karts are left as they were */
    if (G.bot && G.S.phase == PH_COUNT && G.S.phase_ticks == 100)
      for (int i = 0; i < MAXK; i++)
        if (G.S.k[i].on && G.human[i]) ai_reset(&G.ai[i], i, 2, 100u + (uint32_t)i);
    for (int i = 0; i < MAXK; i++) {
      if (!G.S.k[i].on) continue;
      if (G.human[i] && !G.S.k[i].finished) in[i] = human_in(i);
      else in[i] = cpu_think(i);
      static float test_steer = 9;
      if (test_steer == 9) test_steer = getenv("RACER_TEST_STEER") ? (float)atof(getenv("RACER_TEST_STEER")) : 0;
      /* dev aid: RACER_TEST_HOP=t presses kart 0's hop (the drift button) on race tick t (a missile dodge test) */
      static int hop_at = -2;
      if (hop_at == -2) hop_at = getenv("RACER_TEST_HOP") ? atoi(getenv("RACER_TEST_HOP")) : -1;
      if (i == 0 && hop_at >= 0 && G.S.phase == PH_RACE && G.S.race_ticks == hop_at) in[i].btn |= KB_DRIFT;
      static int steer_from = -1;
      if (steer_from < 0) steer_from = getenv("RACER_TEST_STEER_FROM") ? atoi(getenv("RACER_TEST_STEER_FROM")) : 200;
      if (i == 0 && test_steer != 0 && G.S.phase == PH_RACE && G.S.race_ticks > steer_from) in[i] = (kin){ .thr = 127, .steer = q127(test_steer) };
    }
    {
      /* dev aid: RACER_USE=kart:item:tick makes that kart use that item (IT_*) at that race tick */
      if (getenv("RACER_USE") && G.S.phase == PH_RACE) {
        int uk = 0, ui = 0, ut = 0;
        sscanf(getenv("RACER_USE"), "%d:%d:%d", &uk, &ui, &ut);
        if (G.S.race_ticks == ut) sim_use(&G.S, uk & 7, ui);
      }
      /* dev aid: RACER_TEST_AT=s:speed puts kart 0 on the road there, moving, when the lights go out */
      static int at_done;
      if (!at_done && getenv("RACER_TEST_AT") && G.S.phase == PH_RACE && G.S.race_ticks == 1) {
        float sd = 0, sp = 30;
        const char *at = getenv("RACER_TEST_AT");
        int tk = getenv("RACER_TEST_KART") ? atoi(getenv("RACER_TEST_KART")) & 7 : 0;
        if (*at == 'L') { /* L<n>:speed: 30 m before the n-th loop or corkscrew */
          int n = atoi(at + 1);
          sd = G.S.track->s[G.S.track->loop_from[n]].s - 30;
          sscanf(strchr(at, ':') + 1, "%f", &sp);
        } else if (*at == 'B') { /* B<n>:metres:speed: on branch n (a shortcut), that far from its mouth */
          int n = atoi(at + 1);
          sscanf(strchr(at, ':') + 1, "%f:%f", &sd, &sp);
          sim_put_branch(&G.S, tk, n, sd, sp);
          at_done = 1;
        } else sscanf(at, "%f:%f", &sd, &sp);
        if (!at_done) sim_put(&G.S, tk, sd, 0, sp);
        at_done = 1;
      }
    }
    sim_step(&G.S, in);
    if (G.trial) {
      if (G.S.phase == PH_RACE && !G.S.k[0].finished) ghost_rec_tick(&G.S.k[0], G.S.race_ticks);
      ghost_gfx_tick(&G.S);
      G.ghost_rec_n = ghost_rec_count(), G.ghost_rec_hash = ghost_rec_hash(), G.ghost_play_hash = ghost_play_hash();
    }
    world_tick(&G.S);
    items_tick(&G.S);
    fx_events(&G.S);
    fx_tick(&G.S);
    audio_events(&G.S);
    rumble_events(&G.S);
    stats_events(&G.S);
    bumpcheck_events(&G.S);
    rumble_tick(&G.S);
    t3_vec3 eyes[CW_MAX_VIEWS];
    for (int v = 0; v < G.nviews; v++) eyes[v] = cw_node_valid(G.rig[v].cam) ? cw_node3d_global_position(G.rig[v].cam) : G.rig[v].cam_eye;
    audio_tick(&G.S, eyes, G.nviews);
    trace();
    if (G.S.phase == PH_DONE && G.S.phase_ticks == 1) ui_race_over();
  }
  camera_tick();
  if (G.have_track) {
    for (int i = 0; i < MAXK; i++) {
      const kart *k = &G.S.k[i];
      G.place[i] = k->place;
      G.lap[i] = k->lap;
      G.prog[i] = (int32_t)(k->prog * 10);
      G.falls[i] = k->falls;
      G.cuts[i] = k->cuts;
      G.speed[i] = (int32_t)(k->speed * 10);
      G.tricks[i] = k->tricks;
      G.hits[i] = k->hits_taken;
      G.spd_max[i] = (int32_t)(k->top_speed_seen * 10);
      G.finished[i] = k->finished;
      G.best_lap[i] = (int32_t)(k->best_lap * 100);
      G.finish_tick[i] = k->finished ? k->finish_tick : 0;
      G.balloons[i] = k->balloons, G.points[i] = k->points, G.out[i] = k->out;
    }
    G.battle_left = G.S.battle_left, G.battle_end = G.S.battle_end, G.battle_alive = G.S.alive;
    G.phase = G.S.phase;
    G.hash = G.S.hash;
  }
  static bool dev_started;
  {
    /* dev aid: RACER_RELOAD=n starts the same race over again every n ticks (a leak or a stale pointer shows up) */
    static int reload_at = -1;
    if (reload_at < 0) reload_at = getenv("RACER_RELOAD") ? atoi(getenv("RACER_RELOAD")) : 0;
    if (reload_at > 0 && G.ticks > 2 && (G.ticks - 2) % reload_at == 0 && G.have_track) {
      race_setup again = G.setup;
      ui_enter_race(&again);
    }
  }
#ifdef RACER_AUTOPILOT
  /* the _auto build: no menus, eight CPU karts on map 0 for one lap, for hosts that send no input */
  if (!dev_started && G.ticks == 1) { /* before the front end starts its own demo race on its second tick */
    dev_started = true;
    race_setup rs = { .map = 0, .laps = 1, .humans = 0, .cpu_skill = 3, .items = true };
    ui_enter_race(&rs);
  }
#endif
  if (!dev_started && G.ticks == 2 && getenv("RACER_MAP")) {
    dev_started = true;
    int dm = atoi(getenv("RACER_MAP"));
    race_setup rs = { .map = dm < 0 ? -1 : dm % NMAPS, .laps = getenv("RACER_LAPS") ? atoi(getenv("RACER_LAPS")) : 0,
                      .humans = getenv("RACER_PLAYERS") ? CW_CLAMP(atoi(getenv("RACER_PLAYERS")), 1, MAXV) : 1, .cpu_skill = 3, .items = true };
    for (int i = 0; i < MAXV; i++) rs.vehicle[i] = i * 2 % NVEH, rs.colour[i] = i;
    if (getenv("RACER_CLASS")) rs.speed_class = CW_CLAMP(atoi(getenv("RACER_CLASS")), 0, CLASS_COUNT - 1);
    if (getenv("RACER_ASSIST")) rs.assist_steer = (atoi(getenv("RACER_ASSIST")) & 1) != 0, rs.assist_gas = (atoi(getenv("RACER_ASSIST")) & 2) != 0;
    if (getenv("RACER_MODE") && !strcmp(getenv("RACER_MODE"), "battle")) {
      rs.mode = RM_BATTLE, rs.map = (dm < 0 ? 0 : dm) % NARENAS;
      rs.karts = getenv("RACER_KARTS") ? atoi(getenv("RACER_KARTS")) : 0;
      rs.battle_secs = getenv("RACER_BATTLE_TIME") ? atoi(getenv("RACER_BATTLE_TIME")) : 0;
    }
    if (getenv("RACER_MODE") && !strcmp(getenv("RACER_MODE"), "trial") && dm >= 0) {
      rs.mode = RM_TRIAL, rs.items = false, rs.humans = 1;
      rs.ghost = ui_trial_ghost(rs.map);
      if (getenv("RACER_RUNS")) ui_set_auto_runs(atoi(getenv("RACER_RUNS")));
    }
    ui_enter_race(&rs);
  }
}

/* a debug command for hosts that cannot set environment variables (carts): race <map -1..7> [humans] [laps] [class] [assists] */
static void cmd_race(int argc, const char **argv, cw_reply *r) {
  int dm = argc > 1 ? atoi(argv[1]) : 0;
  race_setup rs = { .map = dm < 0 ? -1 : dm % NMAPS, .humans = argc > 2 ? CW_CLAMP(atoi(argv[2]), 0, MAXV) : 0, .laps = argc > 3 ? atoi(argv[3]) : 0,
                    .cpu_skill = 3, .items = true };
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = i * 2 % NVEH, rs.colour[i] = i;
  if (argc > 4) rs.speed_class = CW_CLAMP(atoi(argv[4]), 0, CLASS_COUNT - 1);
  if (argc > 5) rs.assist_steer = (atoi(argv[5]) & 1) != 0, rs.assist_gas = (atoi(argv[5]) & 2) != 0;
  ui_enter_race(&rs);
  if (r) cw_reply_printf(r, "{\"race\":%d}", dm);
}

/* trial <map 0..7> [runs]: a time trial now; with the bot on (`bot 1`) runs drives that many runs, retrying after each */
static void cmd_trial(int argc, const char **argv, cw_reply *r) {
  int dm = argc > 1 ? CW_CLAMP(atoi(argv[1]), 0, NMAPS - 1) : 0;
  race_setup rs = { .map = dm, .humans = 1, .laps = 1, .cpu_skill = 3, .mode = RM_TRIAL };
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = i * 2 % NVEH, rs.colour[i] = i;
  rs.ghost = ui_trial_ghost(dm);
  ui_set_auto_runs(argc > 2 ? atoi(argv[2]) : 0);
  ui_enter_race(&rs);
  if (r) cw_reply_printf(r, "{\"trial\":%d}", dm);
}

/* cup <cup 0..1> [humans] [laps] [races]: a cup now, clicking through its own results and standings (use with `bot 1`) */
static void cmd_cup(int argc, const char **argv, cw_reply *r) {
  ui_debug_cup(argc > 1 ? atoi(argv[1]) : 0, argc > 2 ? atoi(argv[2]) : 1, argc > 3 ? atoi(argv[3]) : 0, argc > 4 ? atoi(argv[4]) : 0);
  if (r) cw_reply_printf(r, "{\"cup\":1}");
}

/* battle <arena 0..1> [humans] [secs] [karts]: a battle now (secs 0: three minutes; karts 0: eight) */
static void cmd_battle(int argc, const char **argv, cw_reply *r) {
  int a = argc > 1 ? CW_CLAMP(atoi(argv[1]), 0, NARENAS - 1) : 0;
  race_setup rs = { .map = a, .humans = argc > 2 ? CW_CLAMP(atoi(argv[2]), 0, MAXV) : 0, .laps = 1, .cpu_skill = 3, .items = true, .mode = RM_BATTLE,
                    .battle_secs = argc > 3 ? atoi(argv[3]) : 0, .karts = argc > 4 ? atoi(argv[4]) : 0 };
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = i * 2 % NVEH, rs.colour[i] = i;
  ui_enter_race(&rs);
  if (r) cw_reply_printf(r, "{\"battle\":%d}", a);
}

static void cmd_bot(int argc, const char **argv, cw_reply *r) {
  G.bot = argc > 1 ? CW_CLAMP(atoi(argv[1]), 0, 2) : 1;
  if (r) cw_reply_printf(r, "{\"bot\":%d}", G.bot);
}

static void update(void *u, double dt) {
  (void)u;
  (void)dt;
  cw_quality_auto_measure(G.have_track && ui_race_runs());
}

static void render(void *u, double alpha) {
  (void)u;
  audio_frame();
  /* dev aid: RACER_JITLOG=1 logs, each frame, the distance from view 0's camera to its kart as last drawn (both blended
   * between ticks): steady at a render rate off the tick rate (CW_FRAME_HZ=144) when the two are drawn alike */
  if (getenv("RACER_JITLOG") && G.have_track && cw_node_valid(G.rig[0].cam)) {
    t3_object *ko = cw_node3d_object(kartgfx_node(game_view_kart(0))), *co = cw_node3d_object(G.rig[0].cam);
    if (ko && co) cw_info("jit %d %.4f %.3f", G.S.tick, v3len(v3sub(ko->position, co->position)), alpha);
  }
  if (G.have_track) {
    {
      t3_vec3 eyes[CW_MAX_VIEWS];
      for (int v = 0; v < G.nviews; v++) eyes[v] = cw_node_valid(G.rig[v].cam) ? cw_node3d_global_position(G.rig[v].cam) : G.rig[v].cam_eye;
      world_set_eyes(eyes, G.nviews);
    }
    world_frame(alpha);
    balloons_frame(&G.S, (float)alpha);
    {
      cw_render3d_stats rs;
      cw_render3d_get_stats(cw_scene_app_tree(), &rs);
      G.draws = rs.draw_calls, G.tris = rs.triangles;
    }
    fx_frame(&G.S, (float)alpha);
  }
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  if (!getenv("RACER_STUDIO") && !getenv("RACER_EYE")) ui_render(cs.x, cs.y); /* models agent: the studio view has no HUD over the kart */
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = ui_menu_font() /* ui agent */, .accent = (cw_color){ 1.0f, 0.80f, 0.15f, 1 } });
  cw_r2d_end();
}

static void init(void *u) {
  (void)u;
  cw_quality_restore(0);
  cw_quality_auto(true, 0);
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("racer: input.cfg had errors (see above)");
  cw_input_commit_defaults();
  G.a_fwd = cw_action_find("thr_fwd");
  G.a_back = cw_action_find("thr_back");
  G.a_left = cw_action_find("steer_l");
  G.a_right = cw_action_find("steer_r");
  G.a_pup = cw_action_find("pitch_up");
  G.a_pdn = cw_action_find("pitch_dn");
  G.a_drift = cw_action_find("drift");
  G.a_item = cw_action_find("item");
  G.a_trick = cw_action_find("trick");
  G.a_look = cw_action_find("look");
  G.a_reset = cw_action_find("reset");
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("race.phase", &G.phase);
  cw_debug_i32("race.loaded", &G.loaded);
  cw_debug_i32("views", &G.views);
  cw_debug_field("karts.place", G.place, CW_DBG_I32, MAXK);
  cw_debug_field("karts.lap", G.lap, CW_DBG_I32, MAXK);
  cw_debug_field("karts.prog", G.prog, CW_DBG_I32, MAXK);
  cw_debug_field("karts.falls", G.falls, CW_DBG_I32, MAXK);
  cw_debug_field("karts.cuts", G.cuts, CW_DBG_I32, MAXK); /* shortcuts taken (branches driven to their end) */
  cw_debug_field("karts.speed", G.speed, CW_DBG_I32, MAXK);
  cw_debug_field("karts.tricks", G.tricks, CW_DBG_I32, MAXK);
  cw_debug_field("render.draws", &G.draws, CW_DBG_I32, 1);
  cw_debug_field("render.tris", &G.tris, CW_DBG_I32, 1);
  cw_debug_f32_array("cam.eye", (float *)&G.rig[0].cam_eye, 3);
  cw_debug_f32_array("cam.at", (float *)&G.rig[0].cam_at, 3);
  cw_debug_field("karts.hits", G.hits, CW_DBG_I32, MAXK);
  cw_debug_field("karts.top_speed", G.spd_max, CW_DBG_I32, MAXK);
  cw_debug_field("karts.finished", G.finished, CW_DBG_I32, MAXK);
  cw_debug_field("karts.best_lap", G.best_lap, CW_DBG_I32, MAXK);
  cw_debug_field("karts.finish_tick", G.finish_tick, CW_DBG_I32, MAXK);
  cw_debug_u32("sim.hash", &G.hash);
  cw_debug_command("race", "race <map -1..7> [humans] [laps] [class] [assists]: start a race now", cmd_race);
  cw_debug_command("trial", "trial <map 0..7> [runs]: start a time trial now (the bot drives <runs> runs when it is on)", cmd_trial);
  cw_debug_command("cup", "cup <0..1> [humans] [laps] [races]: a cup now, clicking through its own results (with `bot 1`)", cmd_cup);
  cw_debug_command("bot", "bot <0|1|2>: the human karts driven by the CPU driver (2: keyboard-style steering)", cmd_bot);
  cw_debug_command("battle", "battle <arena 0..1> [humans] [secs] [karts]: a battle now (secs 0: three minutes)", cmd_battle);
  cw_debug_field("karts.balloons", G.balloons, CW_DBG_I32, MAXK);
  cw_debug_field("karts.points", G.points, CW_DBG_I32, MAXK);
  cw_debug_field("karts.out", G.out, CW_DBG_I32, MAXK);
  cw_debug_i32("battle.left", &G.battle_left);
  cw_debug_i32("battle.end", &G.battle_end);
  cw_debug_i32("battle.alive", &G.battle_alive);
  cw_debug_field("ghost.rec_n", &G.ghost_rec_n, CW_DBG_I32, 1);
  cw_debug_u32("ghost.rec_hash", &G.ghost_rec_hash);
  cw_debug_u32("ghost.play_hash", &G.ghost_play_hash);
  if (getenv("RACER_BOT")) G.bot = !strcmp(getenv("RACER_BOT"), "keys") ? 2 : 1;
#ifdef RACER_AUTOPILOT
  G.auto_all = true;
#endif
  if (getenv("RACER_AUTO")) G.auto_all = true;
  audio_init();
  ui_init();
}

static void shutdown(void *u) {
  (void)u;
  unload();
  audio_shutdown();
  ui_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = GAME_TITLE, .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed,
                              .update = update, .render = render, .shutdown = shutdown, .touch_pointer = true, .max_substeps = 3 };
  /* ui agent: the save data and RACER_RES (pure getters from ui.c) */
  base.save = ui_save_ptr();
  base.save_size = ui_save_size();
  int rw, rh;
  if (ui_dev_resolution(&rw, &rh)) base.width = rw, base.height = rh;
  return cw_scene_app(NULL, &base);
}
