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
 *   RACER_TEST_STEER=x   kart 0 holds steering x at full throttle (a handling test) */
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
#include "ui.h"
#include "world.h"

typedef struct rig {
  cw_node cam;
  t3_vec3 eye, eye_v, look, look_v, up, fwd, cam_eye, cam_at;
  bool live, snap;
  float base_fov, fov_k;
} rig;

static struct {
  int32_t ticks, phase, views, loaded;
  int32_t place[MAXK], lap[MAXK], prog[MAXK], falls[MAXK], speed[MAXK], tricks[MAXK], hits[MAXK], spd_max[MAXK], finished[MAXK], best_lap[MAXK], finish_tick[MAXK];
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

int game_view_kart(int v) {
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
  if (cw_node_valid(post)) cw_node_set(post, "ssao", cw_val_bool(false));
}

void game_start_race(const race_setup *rs) {
  unload();
  G.setup = *rs;
  int map = rs->map < 0 ? -1 : rs->map >= NMAPS ? NMAPS - 1 : rs->map;
  const map_def *md = map < 0 ? &map_test : MAPS[map];
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
  for (int i = 0; i < MAXK; i++) {
    on[i] = true;
    veh[i] = (i * 5 + 1) % NVEH;
    G.human_of[i] = -1;
  }
  int humans = G.auto_all ? 0 : rs->humans > MAXV ? MAXV : rs->humans;
  for (int i = 0; i < humans; i++) {
    G.human[i] = true;
    G.human_of[i] = i;
    veh[i] = rs->vehicle[i] % NVEH;
  }
  /* models agent, dev aid: RACER_STUDIO=<vehicle 0..5> makes kart 0 that vehicle (RACER_COLOUR=n paints it) and the camera orbits it */
  if (getenv("RACER_STUDIO") && !humans) veh[0] = atoi(getenv("RACER_STUDIO")) % NVEH;
  sim_init(&G.S, &G.trk, md, map, rs->laps > 0 ? rs->laps : md->laps, on, veh, 7u + (uint32_t)(map + 1) * 101u);
  for (int i = 0; i < MAXK; i++) {
    G.S.k[i].human = G.human[i];
    G.S.k[i].colour = i < humans ? rs->colour[i] & 7 : (i + 3) & 7;
    int skill = rs->cpu_skill >= 3 ? (i < 3 ? 2 : i < 5 ? 1 : 0) : rs->cpu_skill;
    ai_reset(&G.ai[i], i, skill, 100u + (uint32_t)i);
  }
  if (getenv("RACER_STUDIO") && getenv("RACER_COLOUR") && !humans) G.S.k[0].colour = atoi(getenv("RACER_COLOUR")) & 7;
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.pl[v] = v;
  world_build(G.scene, &G.S, md);
  items_build(G.scene, &G.S);
  fx_init(G.scene, &G.S, md);
  views_setup();
  audio_race_start(&G.S, md);
  G.cam_mode = rs->attract ? 1 : 0;
  G.cam_t = 0;
  G.loaded = 1;
  cw_info("racer: map %d (%s), %d humans, %d laps, track %.0f m", map, md->name, humans, G.S.laps, G.trk.length);
}

/* ── input ──────────────────────────────────────────────────────────── */

static int8_t q127(float v) { return (int8_t)(cw_clampf(v, -1, 1) * 127.0f); }

static float steer_smooth[CW_MAX_VIEWS];
static kin human_in(int kart) {
  int p = G.pl[G.human_of[kart]];
  kin in = { 0 };
  {
    /* dev aid: RACER_BOT=1 drives the human karts with the CPU driver through the human path (a whole split-screen race without hands) */
    static int bot = -1;
    if (bot < 0) bot = getenv("RACER_BOT") != NULL;
    if (bot) {
      kin b = ai_think(&G.S, kart, &G.ai[kart]);
      /* the CPU driver never tricks: the bot taps the button once it has been airborne a while */
      if (G.S.k[kart].air_ticks > 14 && G.S.k[kart].air_ticks < 18) b.btn |= KB_TRICK;
      if (!strcmp(getenv("RACER_BOT"), "keys")) {
        /* RACER_BOT=keys: a keyboard driver, steering all or nothing through the same easing as a held key */
        float want = fabsf(b.steer) < 20 ? 0.0f : (b.steer > 0 ? 1.0f : -1.0f), *ss = &steer_smooth[G.human_of[kart]];
        *ss += (want - *ss) * (fabsf(want) > fabsf(*ss) ? 0.28f : 0.40f);
        b.steer = q127(*ss);
        b.btn &= ~KB_DRIFT;
      }
      return b;
    }
  }
  in.thr = q127(cw_action_strength(p, G.a_fwd) - cw_action_strength(p, G.a_back));
  /* a key is all or nothing: ease it in and out so a tap is a nudge and a hold a full lock */
  float raw = cw_input_axis(p, G.a_left, G.a_right), *ss = &steer_smooth[G.human_of[kart]];
  *ss += (raw - *ss) * (fabsf(raw) > fabsf(*ss) ? 0.28f : 0.40f);
  in.steer = q127(*ss);
  in.pitch = q127(cw_action_strength(p, G.a_pup) - cw_action_strength(p, G.a_pdn));
  if (cw_action_pressed(p, G.a_drift)) in.btn |= KB_DRIFT;
  if (cw_action_pressed(p, G.a_item)) in.btn |= KB_ITEM;
  if (cw_action_pressed(p, G.a_trick)) in.btn |= KB_TRICK;
  if (cw_action_pressed(p, G.a_reset)) in.btn |= KB_RESET;
  if (cw_action_pressed(p, G.a_look)) in.btn |= KB_BACK;
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
  static int dev_top = -1;
  if (dev_top < 0) dev_top = getenv("RACER_CAM") && !strcmp(getenv("RACER_CAM"), "top");
  if (dev_top && v == 0) {
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
    float sd = k->loc.s + 26.0f;
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
    tsample m = track_at(s->track, w->loc.s);
    e = v3add(w->p, v3add(v3mul(m.r, 7.5f * cw_sin(a)), v3add(v3mul(m.f, 7.5f * cw_cos(a)), v3mul(m.u, 2.6f))));
    at = v3add(w->p, v3mul(m.u, 0.8f));
    r->live = false;
    G.cam_t += 1.0f / 60;
  } else {
    t3_vec3 up = kart_up(k), f = kart_fwd(k);
    /* the camera's own up and forward follow the road's and the kart's, smoothed: a loop turns the view with the road */
    t3_vec3 tsup = up;
    if (k->loc.on_road || k->grounded) {
      tsample m = track_at(s->track, k->loc.s);
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
    float dist = (6.6f + spd * 0.045f) * split, high = (2.5f + spd * 0.01f) * split;
    if (back) fh = v3mul(fh, -1);
    t3_vec3 eye = v3add(v3sub(k->p, v3mul(fh, dist)), v3mul(r->up, high));
    t3_vec3 look = v3add(v3add(k->p, v3mul(fh, 6.0f)), v3mul(r->up, 1.0f));
    if (!r->live) {
      r->eye = eye, r->look = look;
      r->eye_v = r->look_v = (t3_vec3){ 0, 0, 0 };
      r->live = true;
    }
    if (back) r->eye = eye, r->look = look;
    spring3(&r->eye, &r->eye_v, eye, spd > 50 ? 7.5f : 6.0f);
    spring3(&r->look, &r->look_v, look, 9.0f);
    e = r->eye, at = r->look;
    upv = r->up;
    /* the field of view opens with speed and a boost: the world rushes */
    float want_k = cw_clampf((spd - 24.0f) / 32.0f, 0, 1) + (k->boost_ticks > 0 ? 0.6f : 0);
    r->fov_k += (want_k - r->fov_k) * 0.06f;
    if (r->base_fov > 0) cw_node_set(r->cam, "fov", cw_val_float(r->base_fov + 13.0f * r->fov_k));
    float sh = fx_shake(me);
    if (sh > 0.001f) {
      e.x += sh * 0.25f * cw_sin((float)G.ticks * 1.9f + (float)v);
      e.y += sh * 0.25f * cw_sin((float)G.ticks * 2.7f + 1 + (float)v);
      e.z += sh * 0.25f * cw_sin((float)G.ticks * 2.3f + 2 + (float)v);
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
  if (!tr) return;
  const char *tk = getenv("RACER_TRACE_KART");
  if (tk) {
    int from = getenv("RACER_TRACE_FROM") ? atoi(getenv("RACER_TRACE_FROM")) : 880;
    if (G.S.tick < from || G.S.tick >= from + 220) return;
    const kart *k = &G.S.k[atoi(tk)];
    const cw_vehicle3d *c = sim_car(&G.S, atoi(tk));
    fprintf(tr, "t%d s=%.1f lat=%.2f h=%.2f spd=%.1f vy=%.1f %s i=%d pitch=%.1f falls=%d spin=%d st=%d yaw=%.2f lv=%.1f ct=%d c=%.2f/%.2f/%.2f/%.2f up.y=%.2f p=(%.1f %.1f %.1f)\n", G.S.tick,
            k->loc.s, k->loc.lat, k->loc.height, k->speed, k->v.y, k->grounded ? "G" : "air", k->loc.i, kart_fwd(k).y, k->falls, k->spin_ticks,
            (int)(k->steer * 100), v3dot(k->w, kart_up(k)), v3dot(k->v, kart_right(k)), c->contacts, c->wheel[0].compression, c->wheel[1].compression,
            c->wheel[2].compression, c->wheel[3].compression, kart_up(k).y, k->p.x, k->p.y, k->p.z);
    return;
  }
  if (G.S.tick % 30) return;
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
    for (int i = 0; i < MAXK; i++) {
      if (!G.S.k[i].on) continue;
      if (G.human[i] && !G.S.k[i].finished) in[i] = human_in(i);
      else in[i] = ai_think(&G.S, i, &G.ai[i]);
      static float test_steer = 9;
      if (test_steer == 9) test_steer = getenv("RACER_TEST_STEER") ? (float)atof(getenv("RACER_TEST_STEER")) : 0;
      if (i == 0 && test_steer != 0 && G.S.phase == PH_RACE && G.S.race_ticks > 200) in[i] = (kin){ .thr = 127, .steer = q127(test_steer) };
    }
    {
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
        } else sscanf(at, "%f:%f", &sd, &sp);
        sim_put(&G.S, tk, sd, 0, sp);
        at_done = 1;
      }
    }
    sim_step(&G.S, in);
    world_tick(&G.S);
    items_tick(&G.S);
    fx_events(&G.S);
    fx_tick(&G.S);
    audio_events(&G.S);
    t3_vec3 eyes[CW_MAX_VIEWS];
    for (int v = 0; v < G.nviews; v++) eyes[v] = G.rig[v].cam_eye;
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
      G.speed[i] = (int32_t)(k->speed * 10);
      G.tricks[i] = k->tricks;
      G.hits[i] = k->hits_taken;
      G.spd_max[i] = (int32_t)(k->top_speed_seen * 10);
      G.finished[i] = k->finished;
      G.best_lap[i] = (int32_t)(k->best_lap * 100);
      G.finish_tick[i] = k->finished ? k->finish_tick : 0;
    }
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
    ui_enter_race(&rs);
  }
}

/* a debug command for hosts that cannot set environment variables (carts): race <map -1..7> [humans] [laps] */
static void cmd_race(int argc, const char **argv, cw_reply *r) {
  int dm = argc > 1 ? atoi(argv[1]) : 0;
  race_setup rs = { .map = dm < 0 ? -1 : dm % NMAPS, .humans = argc > 2 ? CW_CLAMP(atoi(argv[2]), 0, MAXV) : 0, .laps = argc > 3 ? atoi(argv[3]) : 0,
                    .cpu_skill = 3, .items = true };
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = i * 2 % NVEH, rs.colour[i] = i;
  ui_enter_race(&rs);
  if (r) cw_reply_printf(r, "{\"race\":%d}", dm);
}

static void update(void *u, double dt) {
  (void)u;
  (void)dt;
  cw_quality_auto_measure(G.have_track && ui_race_runs());
}

static void render(void *u, double alpha) {
  (void)u;
  audio_frame();
  if (G.have_track) {
    world_frame(alpha);
    fx_frame(&G.S, (float)alpha);
  }
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  if (!getenv("RACER_STUDIO")) ui_render(cs.x, cs.y); /* models agent: the studio view has no HUD over the kart */
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
  cw_debug_field("karts.speed", G.speed, CW_DBG_I32, MAXK);
  cw_debug_field("karts.tricks", G.tricks, CW_DBG_I32, MAXK);
  cw_debug_field("karts.hits", G.hits, CW_DBG_I32, MAXK);
  cw_debug_field("karts.top_speed", G.spd_max, CW_DBG_I32, MAXK);
  cw_debug_field("karts.finished", G.finished, CW_DBG_I32, MAXK);
  cw_debug_field("karts.best_lap", G.best_lap, CW_DBG_I32, MAXK);
  cw_debug_field("karts.finish_tick", G.finish_tick, CW_DBG_I32, MAXK);
  cw_debug_u32("sim.hash", &G.hash);
  cw_debug_command("race", "race <map -1..7> [humans] [laps]: start a race now", cmd_race);
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
