/* racer: the app. Title and map select, the race with one to four views, pause,
 * results. The rules are sim.c's; the picture is world.c's and the maps'.
 *
 * Dev aids from the environment, read once:
 *   RACER_MAP=n        straight into map n (0-based)
 *   RACER_PLAYERS=n    humans (1..4: split screen)
 *   RACER_AUTO=1       every kart driven by the CPU (a hands-off run; the _auto build sets it)
 *   RACER_LAPS=n       laps
 *   RACER_CAM=top      a fixed look from above the track (the map's overview) */
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
#include "world.h"

enum { GS_TITLE, GS_RACE, GS_PAUSE, GS_RESULTS };

static cw_font font_title, font_hud, font_small;

typedef struct rig {
  cw_node cam;
  t3_vec3 eye, eye_v, look, look_v, up, fwd, cam_eye, cam_at;
  bool live, snap;
} rig;

static struct {
  int32_t state, ticks, state_ticks, phase, views;
  int32_t place[MAXK];
  int32_t lap[MAXK];
  int32_t prog[MAXK];
  uint32_t hash;
  int menu_map, menu_laps, players;
  cw_node scene, sun;
  track trk;
  bool have_track;
  sim S;
  ai_mem ai[MAXK];
  bool human[MAXK];
  int human_of[MAXK]; /* kart -> view, or -1 */
  int pl[CW_MAX_VIEWS]; /* the input player behind each view */
  int nviews;
  rig rig[CW_MAX_VIEWS];
  bool auto_all;
  cw_action a_fwd, a_back, a_left, a_right, a_pup, a_pdn, a_drift, a_item, a_trick, a_look, a_reset, a_start, a_esc, m_left, m_right, m_up, m_down, m_ok;
} G;

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }

static cw_node find_type(cw_node n, const char *type) {
  if (cw_node_is(n, type)) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_type(c, type);
    if (cw_node_valid(r)) return r;
  }
}

static bool any_pressed(cw_action a) {
  for (int p = 0; p < 4; p++)
    if (cw_action_just_pressed(p, a)) return true;
  return false;
}

/* ── starting a race ────────────────────────────────────────────────── */

static void unload(void) {
  if (G.have_track) {
    sim_free(&G.S);
    world_reset();
    track_free(&G.trk);
    G.have_track = false;
  }
  if (cw_node_valid(G.scene)) cw_node_free(G.scene);
  G.scene = (cw_node){ 0 };
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
  cw_pipeline *pl = cw_pipeline_of(cw_scene_app_tree());
  if (G.nviews < 2) {
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

static void start_race(int map, int players, int laps) {
  unload();
  const map_def *md = MAPS[map];
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
    veh[i] = i % NVEH;
    G.human_of[i] = -1;
  }
  int humans = G.auto_all ? 0 : players < 1 ? 1 : players > MAXV ? MAXV : players;
  for (int i = 0; i < humans; i++) G.human[i] = true, G.human_of[i] = i;
  sim_init(&G.S, &G.trk, map, laps > 0 ? laps : md->laps, on, veh, 7u + (uint32_t)map);
  for (int i = 0; i < MAXK; i++) {
    G.S.k[i].human = G.human[i];
    G.S.k[i].colour = i;
    ai_reset(&G.ai[i], i, i < 3 ? 2 : i < 5 ? 1 : 0, 100u + (uint32_t)i);
  }
  for (int v = 0; v < CW_MAX_VIEWS; v++) G.pl[v] = v;
  world_build(G.scene, &G.S, md);
  views_setup();
  G.state = GS_RACE;
  G.state_ticks = 0;
}

static void enter_title(void) {
  G.state = GS_TITLE;
  G.state_ticks = 0;
}

/* ── input ──────────────────────────────────────────────────────────── */

static int8_t q127(float v) { return (int8_t)(cw_clampf(v, -1, 1) * 127.0f); }

static kin human_in(int kart) {
  int p = G.pl[G.human_of[kart]];
  kin in = { 0 };
  in.thr = q127(cw_action_strength(p, G.a_fwd) - cw_action_strength(p, G.a_back));
  in.steer = q127(cw_input_axis(p, G.a_left, G.a_right));
  in.pitch = q127(cw_action_strength(p, G.a_pup) - cw_action_strength(p, G.a_pdn));
  if (cw_action_pressed(p, G.a_drift)) in.btn |= KB_DRIFT;
  if (cw_action_pressed(p, G.a_item)) in.btn |= KB_ITEM;
  if (cw_action_pressed(p, G.a_trick)) in.btn |= KB_TRICK;
  if (cw_action_pressed(p, G.a_reset)) in.btn |= KB_RESET;
  if (cw_action_pressed(p, G.a_look)) in.btn |= KB_BACK;
  return in;
}

/* ── the chase camera (fixed tick) ──────────────────────────────────── */

static void spring3(t3_vec3 *x, t3_vec3 *v, t3_vec3 target, float w) {
  const float dt = 1.0f / 60;
  t3_vec3 a = { (target.x - x->x) * w * w - v->x * 2 * w, (target.y - x->y) * w * w - v->y * 2 * w, (target.z - x->z) * w * w - v->z * 2 * w };
  v->x += a.x * dt, v->y += a.y * dt, v->z += a.z * dt;
  x->x += v->x * dt, x->y += v->y * dt, x->z += v->z * dt;
}

static int view_kart(int v) {
  for (int i = 0; i < MAXK; i++)
    if (G.human_of[i] == v) return i;
  /* an all-CPU run (or a view with no human): follow the leader */
  return G.S.order[0];
}

static void rig_tick(int v) {
  rig *r = &G.rig[v];
  if (!cw_node_valid(r->cam)) return;
  const sim *s = &G.S;
  int me = view_kart(v);
  const kart *k = &s->k[me];
  t3_vec3 up = kart_up(k), f = kart_fwd(k);
  /* the camera's own up and forward follow the kart's, smoothed: a loop turns the view with the road */
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
  float ku = 0.07f, kf = 0.09f;
  r->up = v3norm(v3add(v3mul(r->up, 1 - ku), v3mul(tsup, ku)));
  r->fwd = v3norm(v3add(v3mul(r->fwd, 1 - kf), v3mul(want_f, kf)));
  /* orthogonalise the forward to the up */
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
  float we = spd > 50 ? 7.5f : 6.0f;
  spring3(&r->eye, &r->eye_v, eye, we);
  spring3(&r->look, &r->look_v, look, 9.0f);
  t3_vec3 e = r->eye, at = r->look;
  static int dev_top = -1;
  if (dev_top < 0) dev_top = getenv("RACER_CAM") && !strcmp(getenv("RACER_CAM"), "top");
  if (dev_top && v == 0) {
    const track *t = s->track;
    float cx = 0, cz = 0, minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (int i = 0; i < t->n; i += 8) {
      minx = fminf(minx, t->s[i].p.x), maxx = fmaxf(maxx, t->s[i].p.x);
      minz = fminf(minz, t->s[i].p.z), maxz = fmaxf(maxz, t->s[i].p.z);
    }
    cx = (minx + maxx) * 0.5f, cz = (minz + maxz) * 0.5f;
    float ext = fmaxf(maxx - minx, maxz - minz);
    e = (t3_vec3){ cx, ext * 1.05f, cz - 1 };
    at = (t3_vec3){ cx, 0, cz };
    r->snap = true;
  }
  if (r->snap) r->cam_eye = e, r->cam_at = at;
  else r->cam_eye = e, r->cam_at = at;
  bool snapped = r->snap;
  r->snap = false;
  cw_node3d_snapshot(r->cam);
  cw_node3d_set_position(r->cam, r->cam_eye);
  cw_node3d_look_at(r->cam, r->cam_at, dev_top ? (t3_vec3){ 0, 0, 1 } : r->up);
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

static void fixed(void *u, double dt) {
  (void)u;
  (void)dt;
  G.ticks++;
  G.state_ticks++;
  switch (G.state) {
  case GS_TITLE: {
    int dy = (cw_action_just_pressed(0, G.m_down) ? 1 : 0) - (cw_action_just_pressed(0, G.m_up) ? 1 : 0);
    int dx = (cw_action_just_pressed(0, G.m_right) ? 1 : 0) - (cw_action_just_pressed(0, G.m_left) ? 1 : 0);
    G.menu_map = (G.menu_map + dy + NMAPS) % NMAPS;
    G.menu_laps = CW_CLAMP(G.menu_laps + dx, 1, MAX_LAPS);
    if (G.state_ticks > 20 && (cw_action_just_pressed(0, G.m_ok) || cw_action_just_pressed(0, G.a_start))) start_race(G.menu_map, G.players, G.menu_laps);
    break;
  }
  case GS_RACE: {
    kin in[MAXK] = { { 0 } };
    for (int i = 0; i < MAXK; i++) {
      if (!G.S.k[i].on) continue;
      if (G.human[i]) in[i] = human_in(i);
      else in[i] = ai_think(&G.S, i, &G.ai[i]);
    }
    sim_step(&G.S, in);
    world_tick(&G.S);
    if (G.S.phase == PH_DONE && G.S.phase_ticks > 240) {
      G.state = GS_RESULTS;
      G.state_ticks = 0;
    } else if (any_pressed(G.a_start)) {
      G.state = GS_PAUSE;
      G.state_ticks = 0;
    }
    break;
  }
  case GS_PAUSE:
    if (G.state_ticks > 5 && any_pressed(G.a_start)) G.state = GS_RACE;
    else if (G.state_ticks > 5 && any_pressed(G.a_esc)) enter_title();
    break;
  case GS_RESULTS:
    if (G.state_ticks > 120 && (any_pressed(G.m_ok) || any_pressed(G.a_start))) enter_title();
    break;
  }
  camera_tick();
  if (G.have_track) {
    for (int i = 0; i < MAXK; i++) {
      G.place[i] = G.S.k[i].place;
      G.lap[i] = G.S.k[i].lap;
      G.prog[i] = (int32_t)(G.S.k[i].prog * 10);
    }
    G.phase = G.S.phase;
    G.hash = G.S.hash;
  }
  if (G.state == GS_TITLE) {
    const char *m = getenv("RACER_MAP");
    if (m && G.state_ticks == 1) start_race(atoi(m) % NMAPS, G.players, G.menu_laps);
  }
}

static void update(void *u, double dt) {
  (void)u;
  (void)dt;
  cw_quality_auto_measure(G.state == GS_RACE);
}

/* ── HUD ────────────────────────────────────────────────────────────── */

static void text(cw_font f, const char *s, float x, float y, float size, cw_color c, cw_text_align align) {
  cw_text_style st = { .size = size, .color = c, .align = align, .outline_width = size * 0.07f + 0.6f, .outline_color = rgba(0.02f, 0.02f, 0.03f, 0.9f) };
  cw_r2d_text(f, s, cw_v2(x, y), &st);
}

static void draw_hud_for(int ki, float x0, float y0, float W, float H, float s) {
  const sim *S = &G.S;
  const kart *k = &S->k[ki];
  char b[96];
  snprintf(b, sizeof b, "%d", k->place);
  text(font_title, b, x0 + 24 * s, y0 + 8 * s, 64 * s, rgba(1, 0.85f, 0.2f, 1), CW_ALIGN_LEFT);
  snprintf(b, sizeof b, "LAP %d/%d", k->lap < 1 ? 1 : k->lap, S->laps);
  text(font_hud, b, x0 + W - 20 * s, y0 + 12 * s, 30 * s, rgba(1, 1, 1, 1), CW_ALIGN_RIGHT);
  snprintf(b, sizeof b, "%d", (int)(k->speed * 3.6f));
  text(font_title, b, x0 + W - 24 * s, y0 + H - 70 * s, 48 * s, rgba(1, 1, 1, 1), CW_ALIGN_RIGHT);
  if (k->item) text(font_hud, ITEM_NAME[k->item], x0 + 24 * s, y0 + 80 * s, 26 * s, rgba(0.6f, 1, 0.7f, 1), CW_ALIGN_LEFT);
  if (S->phase == PH_COUNT) {
    int n = 3 - S->phase_ticks / 60;
    if (n >= 1) {
      snprintf(b, sizeof b, "%d", n);
      text(font_title, b, x0 + W * 0.5f, y0 + H * 0.3f, 120 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    }
  }
}

static void render(void *u, double alpha) {
  (void)u;
  (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y, s = H / 540.0f;
  switch (G.state) {
  case GS_TITLE: {
    text(font_title, GAME_TITLE, W * 0.5f, H * 0.12f, 90 * s, rgba(1, 0.85f, 0.2f, 1), CW_ALIGN_CENTER);
    for (int i = 0; i < NMAPS; i++) {
      char b[96];
      snprintf(b, sizeof b, "%s%s", i == G.menu_map ? "> " : "  ", MAPS[i]->name);
      text(font_hud, b, W * 0.5f, H * (0.32f + 0.055f * (float)i), 28 * s, i == G.menu_map ? rgba(1, 1, 1, 1) : rgba(0.7f, 0.7f, 0.75f, 1), CW_ALIGN_CENTER);
    }
    char b[64];
    snprintf(b, sizeof b, "LAPS %d  (left / right)", G.menu_laps);
    text(font_small, b, W * 0.5f, H * 0.82f, 22 * s, rgba(0.9f, 0.9f, 0.9f, 1), CW_ALIGN_CENTER);
    break;
  }
  case GS_RACE:
  case GS_PAUSE:
    if (G.nviews == 1) draw_hud_for(view_kart(0), 0, 0, W, H, s);
    else
      for (int v = 0; v < G.nviews; v++) {
        float x = G.nviews == 2 ? (W >= H ? 0 : W * 0.5f * (float)v) : (v & 1) ? W * 0.5f : 0;
        float y = G.nviews == 2 ? (W >= H ? H * 0.5f * (float)v : 0) : (v >> 1) ? H * 0.5f : 0;
        float w = G.nviews == 2 ? (W >= H ? W : W * 0.5f) : W * 0.5f, h = G.nviews == 2 ? (W >= H ? H * 0.5f : H) : H * 0.5f;
        draw_hud_for(view_kart(v), x, y, w, h, s * 0.6f);
      }
    if (G.state == GS_PAUSE) text(font_title, "PAUSED", W * 0.5f, H * 0.4f, 80 * s, rgba(1, 1, 1, 1), CW_ALIGN_CENTER);
    break;
  case GS_RESULTS: {
    text(font_title, "RESULTS", W * 0.5f, H * 0.1f, 70 * s, rgba(1, 0.85f, 0.2f, 1), CW_ALIGN_CENTER);
    for (int i = 0; i < MAXK; i++) {
      int kk = G.S.order[i];
      char b[64];
      snprintf(b, sizeof b, "%d  %s%s", i + 1, G.human[kk] ? "PLAYER " : "CPU ", G.human[kk] ? "" : "");
      text(font_hud, b, W * 0.5f, H * (0.25f + 0.07f * (float)i), 30 * s, G.human[kk] ? rgba(1, 1, 0.5f, 1) : rgba(0.85f, 0.85f, 0.9f, 1), CW_ALIGN_CENTER);
    }
    break;
  }
  }
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = font_hud, .accent = rgba(0.95f, 0.78f, 0.5f, 1) });
  cw_r2d_end();
}

/* ── app ────────────────────────────────────────────────────────────── */

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
  G.a_start = cw_action_find("start");
  G.a_esc = cw_action_find("back");
  G.m_left = cw_action_find("m_left");
  G.m_right = cw_action_find("m_right");
  G.m_up = cw_action_find("m_up");
  G.m_down = cw_action_find("m_down");
  G.m_ok = cw_action_find("m_ok");
  const cw_font_desc sdf = { .mode = CW_FONT_SDF, .sdf_padding = 8 };
  font_title = cw_font_load("fonts/Bungee-Regular.ttf", &sdf);
  font_hud = cw_font_load("fonts/Fredoka-Bold.ttf", &sdf);
  font_small = cw_font_load("fonts/Oswald-VF.ttf", &sdf);
  const cw_font fonts[] = { font_title, font_hud, font_small };
  for (unsigned f = 0; f < sizeof fonts / sizeof fonts[0]; f++) cw_font_preload_range(fonts[f], 32, 126);
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("race.phase", &G.phase);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("views", &G.views);
  cw_debug_field("karts.place", G.place, CW_DBG_I32, MAXK);
  cw_debug_field("karts.lap", G.lap, CW_DBG_I32, MAXK);
  cw_debug_field("karts.prog", G.prog, CW_DBG_I32, MAXK);
  cw_debug_u32("sim.hash", &G.hash);
  const char *pl = getenv("RACER_PLAYERS");
  G.players = pl ? CW_CLAMP(atoi(pl), 1, MAXV) : 1;
  const char *lp = getenv("RACER_LAPS");
  G.menu_laps = lp ? CW_CLAMP(atoi(lp), 1, MAX_LAPS) : 3;
#ifdef RACER_AUTOPILOT
  G.auto_all = true;
#endif
  if (getenv("RACER_AUTO")) G.auto_all = true;
  enter_title();
}

static void shutdown(void *u) {
  (void)u;
  unload();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = GAME_TITLE, .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed,
                              .update = update, .render = render, .shutdown = shutdown, .touch_pointer = true, .max_substeps = 3 };
  return cw_scene_app(NULL, &base);
}
