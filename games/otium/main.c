/* otium: Roman trade across Italy against two rival houses, on cartwheel and
 * three.c (three.js r186).
 *
 *   time      stands still while you choose. Every order costs days (the
 *             sundial on the wheel says how many); while they pass, the
 *             rivals act too. Hold the right trigger (Tab) to let time run
 *             without acting. An order chosen while busy waits its turn.
 *   the map   the stick or d-pad hops the gold ring from city to city;
 *             A opens the order wheel there; LB / RB pick which of your
 *             carts a move sends (else the nearest goes)
 *   orders    move, build (wherever your carts stand), harvest a region,
 *             trade at the market, recruit a cart, court a god at the
 *             temple, rest (every used order back, and coins for each)
 *   scoring   each god counts one thing (Jupiter towns, Saturn regions,
 *             Mercury kinds of goods, Mars carts, Vesta wealth) and
 *             multiplies it by your favor with him; six seasons, then
 *             the gods tally
 *
 * Determinism: the game replays identically from the input stream (fixed
 * ticks, integer sim.c, ai.c on it); the debug fields name what the gates
 * compare across hosts. otium_auto plays itself (for hosts that send no
 * input). */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/quality.h"
#include "cartwheel/quality_menu.h"
#include "cartwheel/render2d.h"
#include "cartwheel/resource.h"
#include "cartwheel/vfs.h"
#include "game.h"

enum { GS_TITLE, GS_PLAY, GS_PAUSE, GS_END };
enum { MODE_MAP, MODE_WHEEL, MODE_TRADE, MODE_TEMPLE };

/* ── save ───────────────────────────────────────────────────────────── */

#define SAVE_MAGIC 0x4d55494fu /* 'OIUM' */
#define SAVE_VERSION 1
typedef struct save_data {
  uint32_t magic;
  uint16_t version, size;
  int32_t games, wins, best;
  uint8_t quality[4];
  uint32_t checksum;
} save_data;
static save_data SAVE;
static uint32_t save_sum(void) { return cw_hash32(&SAVE.games, offsetof(save_data, checksum) - offsetof(save_data, games), CW_HASH32_INIT) ^ SAVE.version; }
static void save_reset(void) {
  memset(&SAVE, 0, sizeof SAVE);
  SAVE.magic = SAVE_MAGIC, SAVE.version = SAVE_VERSION, SAVE.size = sizeof SAVE;
  SAVE.checksum = save_sum();
}
static void save_check(void) {
  if (SAVE.magic != SAVE_MAGIC || SAVE.version != SAVE_VERSION || SAVE.size != sizeof SAVE || SAVE.checksum != save_sum()) save_reset();
}
static void save_write(void) {
  uint32_t q = cw_quality_saved();
  if (q)
    for (int i = 0; i < 4; i++) SAVE.quality[i] = (uint8_t)(q >> (8 * i));
  SAVE.checksum = save_sum();
  cw_save_commit();
}
static uint32_t gfx_saved(void) { return SAVE.quality[0] | (uint32_t)SAVE.quality[1] << 8 | (uint32_t)SAVE.quality[2] << 16 | (uint32_t)SAVE.quality[3] << 24; }

/* ── state ──────────────────────────────────────────────────────────── */

static struct {
  int32_t state, mode, state_ticks, ticks, cursor, sel_unit, wheel, trade_row, temple_sel, waiting;
  int32_t trade[GOODS];
  sim S;
  int pers[NP];
  order_req queued;
  bool has_queued;
  int32_t dbg_coins[NP], dbg_houses[NP], dbg_units[NP], dbg_total[NP], dbg_orders[NP], dbg_free[NP];
  int32_t tick, season, idle, winner;
  uint32_t state_hash;
  cw_node scene, camera, sun;
  cw_action a_left, a_right, a_up, a_down, a_ok, a_back, a_wait, a_prev, a_next, a_pause, a_zoom_in, a_zoom_out;
  /* stick hops: the repeat clock */
  int hop_hold, hop_dir;
  float zoom;
  /* the camera (render only) */
  t3_vec3 cam_at, cam_eye;
  bool cam_snap;
  int deny_ticks; /* the wheel's slot shakes when an order cannot be given */
  int auto_think;
} G;

static int32_t input_mode;
enum { DEV_MOUSE, DEV_PAD, DEV_KEYS, DEV_TOUCH };
static int last_device = DEV_PAD;
static bool device_used, mouse_clicked, mouse_right, mouse_moved;
static uint32_t prev_mouse;
static float prev_mx, prev_my;

static void read_devices(void) {
  const cw_raw_input *in = cw_input_raw();
  mouse_clicked = (in->mouse_buttons & 1) && !(prev_mouse & 1);
  mouse_right = (in->mouse_buttons & 2) && !(prev_mouse & 2);
  mouse_moved = in->mouse_x != prev_mx || in->mouse_y != prev_my;
  if (mouse_clicked || mouse_right || (mouse_moved && in->mouse_present && prev_mx + prev_my != 0)) last_device = DEV_MOUSE;
  prev_mouse = in->mouse_buttons, prev_mx = in->mouse_x, prev_my = in->mouse_y;
  for (int i = 0; i < CW_MAX_PADS; i++) {
    const cw_pad_state *p = &in->pads[i];
    if (p->connected && (p->buttons || fabsf(p->axes[CW_AXIS_LX]) > 0.4f || fabsf(p->axes[CW_AXIS_LY]) > 0.4f || p->axes[CW_AXIS_RT] > 0.4f)) last_device = DEV_PAD;
  }
  for (int k = 0; k < CW_KEY_COUNT; k++)
    if (in->keys_pressed[k]) {
      last_device = DEV_KEYS;
      break;
    }
  if (in->mouse_from_touch) last_device = DEV_TOUCH;
  if (!device_used && (mouse_clicked || last_device != DEV_PAD)) device_used = true;
  input_mode = last_device;
}

/* ── audio (made by tools/make-audio.mjs) ───────────────────────────── */

enum { SFX_HOP, SFX_OPEN, SFX_OK, SFX_DENY, SFX_BUILD, SFX_HARVEST, SFX_COINS, SFX_RECRUIT, SFX_TEMPLE, SFX_REST, SFX_SEASON, SFX_END, SFX_ARRIVE, SFX_COUNT };
static const char *const SFX_FILES[SFX_COUNT] = { "audio/hop.wav", "audio/open.wav", "audio/ok.wav", "audio/deny.wav", "audio/build.wav",
                                                  "audio/harvest.wav", "audio/coins.wav", "audio/recruit.wav", "audio/temple.wav",
                                                  "audio/rest.wav", "audio/season.wav", "audio/end.wav", "audio/arrive.wav" };
static struct {
  bool on;
  cw_bus sfx_bus, music_bus;
  cw_sound sfx[SFX_COUNT];
  cw_voice music;
  int cooldown[SFX_COUNT];
} A;
static void sfx(int id, float vol) {
  if (!A.on || !A.sfx[id].id || A.cooldown[id] > 0) return;
  A.cooldown[id] = 3;
  cw_audio_play(A.sfx[id], &(cw_play_desc){ .bus = A.sfx_bus, .volume = vol });
}
static void audio_init(void) {
  A.on = getenv("OTIUM_NO_AUDIO") == NULL && cw_vfs_exists("audio/music.ogg") && cw_audio_init(&(cw_audio_desc){ 0 });
  if (!A.on) return;
  A.sfx_bus = cw_bus_create("sfx", CW_AUDIO_MASTER);
  A.music_bus = cw_bus_create("music", CW_AUDIO_MASTER);
  cw_bus_set_volume(A.music_bus, 0.42f);
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1, 0.1f } });
  for (int i = 0; i < SFX_COUNT; i++)
    if (cw_vfs_exists(SFX_FILES[i]))
      A.sfx[i] = cw_res_audio_sound(cw_res_load(SFX_FILES[i], 0), &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = 3, .bus = A.sfx_bus });
  cw_sound m = cw_res_audio_sound(cw_res_load("audio/music.ogg", 0), &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.music_bus });
  A.music = cw_audio_play(m, &(cw_play_desc){ .loop = true, .fade_in = 2.0f, .bus = A.music_bus });
}

/* ── the match ──────────────────────────────────────────────────────── */

static void new_game(void) {
  sim_start(&G.S, 0x07105u + (uint32_t)SAVE.games * 7919u);
  G.pers[0] = PERS_BUILDER;
  G.pers[1] = (int)(SAVE.games % 2 ? PERS_GENERAL : PERS_MERCHANT);
  G.pers[2] = (int)(SAVE.games % 2 ? PERS_MERCHANT : PERS_BUILDER);
  G.cursor = CAPITAL;
  G.sel_unit = -1;
  G.mode = MODE_MAP;
  G.wheel = O_MOVE;
  G.has_queued = false;
  memset(G.trade, 0, sizeof G.trade);
  G.state = GS_PLAY;
  G.state_ticks = 0;
  G.winner = -1;
  G.cam_snap = false;
}

static void end_game(void) {
  G.state = GS_END;
  G.state_ticks = 0;
  int best = 0;
  for (int p = 1; p < NP; p++)
    if (sim_total(&G.S, p) > sim_total(&G.S, best)) best = p;
  G.winner = best;
  SAVE.games++;
  if (best == 0) SAVE.wins++;
  if (sim_total(&G.S, 0) > SAVE.best) SAVE.best = sim_total(&G.S, 0);
  save_write();
  sfx(SFX_END, 0.9f);
}

/* the order the wheel's slot would give, from the cursor */
static order_req wheel_req(int order) {
  order_req r = { .order = order, .city = G.cursor, .unit = -1, .offer = G.temple_sel };
  if (order == O_MOVE) r.unit = G.sel_unit >= 0 && G.sel_unit < G.S.p[0].n_units ? G.sel_unit : sim_nearest_unit(&G.S, 0, G.cursor);
  if (order == O_TRADE) memcpy(r.trade, G.trade, sizeof r.trade);
  return r;
}

static void sim_sounds(void) {
  const sim *S = &G.S;
  for (int p = 0; p < NP; p++) {
    float v = p == 0 ? 0.9f : 0.35f;
    if (S->ev_build[p]) sfx(SFX_BUILD, v);
    if (S->ev_harvest[p]) sfx(SFX_HARVEST, v);
    if (S->ev_trade[p]) sfx(SFX_COINS, v);
    if (S->ev_recruit[p]) sfx(SFX_RECRUIT, v);
    if (S->ev_temple[p]) sfx(SFX_TEMPLE, v);
    if (S->ev_rest[p]) sfx(SFX_REST, v * 0.8f);
    if (S->ev_arrive[p] && p == 0) sfx(SFX_ARRIVE, 0.5f);
  }
  if (S->ev_season) sfx(SFX_SEASON, 0.8f);
}

/* give the player's order now (or queue it while busy) */
static bool give(const order_req *r) {
  if (!sim_idle(&G.S, 0)) {
    int why = sim_check(&G.S, 0, r, NULL);
    (void)why;
    G.queued = *r;
    G.has_queued = true;
    sfx(SFX_OK, 0.5f);
    return true;
  }
  if (sim_order(&G.S, 0, r)) {
    sim_sounds();
    sfx(SFX_OK, 0.7f);
    return true;
  }
  sfx(SFX_DENY, 0.7f);
  G.deny_ticks = 16;
  return false;
}

/* the rivals (and, in otium_auto, you) choose when free */
static void bots_act(void) {
#ifdef OTIUM_AUTOPILOT
  int first = 0;
#else
  int first = 1;
#endif
  for (int p = first; p < NP; p++) {
    if (!sim_idle(&G.S, p)) continue;
#ifdef OTIUM_AUTOPILOT
    if (p == 0 && G.auto_think++ < 24) continue; /* a beat to think, as a person would */
    if (p == 0) G.auto_think = 0;
#endif
    order_req r = ai_choose(&G.S, p, G.pers[p]);
    if (r.order == O_NONE || !sim_order(&G.S, p, &r)) sim_wait(&G.S, p, 1);
#ifdef OTIUM_AUTOPILOT
    if (p == 0 && r.order != O_NONE) G.cursor = r.city;
#endif
  }
}

/* ── map navigation: hop to the city that best lies in a direction ──── */

static int hop(int from, float dx, float dz) {
  t3_vec3 a = world_city_pos(from);
  int best = -1;
  float bs = 1e9f;
  for (int c = 0; c < N_CITIES; c++) {
    if (c == from) continue;
    t3_vec3 b = world_city_pos(c);
    float vx = b.x - a.x, vz = b.z - a.z;
    float along = vx * dx + vz * dz, perp = fabsf(vx * dz - vz * dx);
    if (along <= 0.05f || perp > along * 1.6f) continue;
    float s = along + 1.8f * perp;
    if (s < bs) bs = s, best = c;
  }
  return best < 0 ? from : best;
}

static cw_vec2 stick(void) {
  float x = cw_action_strength(0, G.a_right) - cw_action_strength(0, G.a_left);
  float y = cw_action_strength(0, G.a_down) - cw_action_strength(0, G.a_up);
  return cw_v2(x, y);
}
/* a direction press: on the edge, then repeating while held */
static bool dir_press(cw_vec2 *d) {
  cw_vec2 v = stick();
  float m = sqrtf(v.x * v.x + v.y * v.y);
  if (m < 0.5f) {
    G.hop_hold = 0;
    return false;
  }
  int dir = (int)((cw_atan2(v.y, v.x) + 3.14159265f) / 0.785398f + 0.5f) & 7;
  bool fire = G.hop_hold == 0 || dir != G.hop_dir || (G.hop_hold > 16 && (G.hop_hold - 16) % 7 == 0);
  G.hop_dir = dir;
  G.hop_hold++;
  *d = cw_v2(v.x / m, v.y / m);
  return fire;
}

static bool project(t3_vec3 p, float W, float H, cw_vec2 *out);

static void cycle_unit(int step) {
  int n = G.S.p[0].n_units;
  if (n <= 0) return;
  int u = G.sel_unit < 0 ? (step > 0 ? 0 : n - 1) : G.sel_unit + step;
  if (u >= n || u < 0) u = -1; /* past the ends: back to "the nearest" */
  G.sel_unit = u;
  sfx(SFX_HOP, 0.4f);
}

static bool pressed(cw_action a) { return cw_action_just_pressed(0, a); }

/* the wheel's slots in a ring: up is O_MOVE, clockwise */
static int wheel_slot_at(cw_vec2 d) {
  float a = cw_atan2(d.x, -d.y); /* 0 up, clockwise positive */
  if (a < 0) a += 6.2831853f;
  return (int)(a / (6.2831853f / ORDERS) + 0.5f) % ORDERS;
}

static void play_input(void) {
  bool idle = sim_idle(&G.S, 0);
  cw_vec2 d;
  cw_vec2 cs = cw_r2d_canvas_size();
  switch (G.mode) {
  case MODE_MAP:
    if (dir_press(&d)) {
      int c = hop(G.cursor, d.x, d.y);
      if (c != G.cursor) G.cursor = c, sfx(SFX_HOP, 0.35f);
    }
    if (last_device == DEV_MOUSE && mouse_moved) { /* the city under the pointer */
      cw_vec2 m = cw_r2d_window_to_canvas(cw_v2(cw_input_raw()->mouse_x, cw_input_raw()->mouse_y));
      float bd = 30 * cs.y / 540;
      for (int c = 0; c < N_CITIES; c++) {
        cw_vec2 sp;
        if (!project(world_city_pos(c), cs.x, cs.y, &sp)) continue;
        float dd = sqrtf((sp.x - m.x) * (sp.x - m.x) + (sp.y - m.y) * (sp.y - m.y));
        if (dd < bd) bd = dd, G.cursor = c;
      }
    }
    if (pressed(G.a_prev)) cycle_unit(-1);
    if (pressed(G.a_next)) cycle_unit(1);
    if (pressed(G.a_ok) || mouse_clicked) G.mode = MODE_WHEEL, sfx(SFX_OPEN, 0.6f);
    break;
  case MODE_WHEEL: {
    cw_vec2 v = stick();
    if (v.x * v.x + v.y * v.y > 0.35f) {
      int s = wheel_slot_at(v);
      if (s != G.wheel) G.wheel = s, sfx(SFX_HOP, 0.3f);
    }
    if (last_device == DEV_MOUSE && mouse_moved) {
      cw_vec2 m = cw_r2d_window_to_canvas(cw_v2(cw_input_raw()->mouse_x, cw_input_raw()->mouse_y));
      cw_vec2 c = cw_v2(cs.x * 0.5f, cs.y * 0.56f);
      cw_vec2 dm = cw_v2(m.x - c.x, m.y - c.y);
      if (dm.x * dm.x + dm.y * dm.y > 400) G.wheel = wheel_slot_at(dm);
    }
    if (pressed(G.a_prev)) cycle_unit(-1);
    if (pressed(G.a_next)) cycle_unit(1);
    if (pressed(G.a_back) || mouse_right) {
      G.mode = MODE_MAP;
      break;
    }
    if (pressed(G.a_ok) || mouse_clicked) {
      if (G.wheel == O_TRADE) {
        G.mode = MODE_TRADE, G.trade_row = 0;
        memset(G.trade, 0, sizeof G.trade);
        sfx(SFX_OPEN, 0.5f);
      } else if (G.wheel == O_TEMPLE) {
        G.mode = MODE_TEMPLE, G.temple_sel = 0;
        sfx(SFX_OPEN, 0.5f);
      } else {
        order_req r = wheel_req(G.wheel);
        int why = idle ? sim_check(&G.S, 0, &r, NULL) : WHY_OK;
        if (why == WHY_OK && give(&r)) G.mode = MODE_MAP;
        else sfx(SFX_DENY, 0.7f), G.deny_ticks = 16;
      }
    }
    break;
  }
  case MODE_TRADE: {
    if (dir_press(&d)) {
      if (fabsf(d.y) > fabsf(d.x)) G.trade_row = (G.trade_row + (d.y > 0 ? 1 : GOODS - 1)) % GOODS;
      else {
        int lots = 0;
        for (int g = 0; g < GOODS; g++) lots += abs(G.trade[G.trade_row == g ? g : g]);
        int q = G.trade[G.trade_row] + (d.x > 0 ? 1 : -1);
        int after = lots - abs(G.trade[G.trade_row]) + abs(q);
        if (after <= TRADE_LOTS && G.S.p[0].goods[G.trade_row] + q >= 0) G.trade[G.trade_row] = q;
      }
      sfx(SFX_HOP, 0.3f);
    }
    if (pressed(G.a_back) || mouse_right) G.mode = MODE_WHEEL;
    else if (pressed(G.a_ok) || mouse_clicked) {
      order_req r = wheel_req(O_TRADE);
      if ((!idle || sim_check(&G.S, 0, &r, NULL) == WHY_OK) && give(&r)) G.mode = MODE_MAP;
      else sfx(SFX_DENY, 0.7f), G.deny_ticks = 16;
    }
    break;
  }
  case MODE_TEMPLE:
    if (dir_press(&d) && fabsf(d.x) > 0.5f) G.temple_sel = (G.temple_sel + (d.x > 0 ? 1 : OFFERS - 1)) % OFFERS, sfx(SFX_HOP, 0.3f);
    if (pressed(G.a_back) || mouse_right) G.mode = MODE_WHEEL;
    else if (pressed(G.a_ok) || mouse_clicked) {
      order_req r = wheel_req(O_TEMPLE);
      if ((!idle || sim_check(&G.S, 0, &r, NULL) == WHY_OK) && give(&r)) G.mode = MODE_MAP;
      else sfx(SFX_DENY, 0.7f), G.deny_ticks = 16;
    }
    break;
  }
}

/* ── the fixed tick ─────────────────────────────────────────────────── */

static void debug_fields(void) {
  const sim *S = &G.S;
  for (int p = 0; p < NP; p++) {
    G.dbg_coins[p] = S->p[p].coins;
    G.dbg_houses[p] = S->p[p].houses;
    G.dbg_units[p] = S->p[p].n_units;
    G.dbg_total[p] = sim_total(S, p);
    G.dbg_orders[p] = S->orders_done[p];
    G.dbg_free[p] = S->p[p].free_at;
  }
  G.tick = S->tick;
  G.season = sim_season(S);
  G.idle = sim_idle(S, 0);
  G.state_hash = sim_hash(S);
}

static void fixed(void *u, double dt) {
  (void)u, (void)dt;
  G.ticks++;
  G.state_ticks++;
  read_devices();
  for (int i = 0; i < SFX_COUNT; i++)
    if (A.cooldown[i] > 0) A.cooldown[i]--;
  if (G.deny_ticks > 0) G.deny_ticks--;
  /* zoom: the right stick or + / - */
  float zin = cw_action_strength(0, G.a_zoom_in), zout = cw_action_strength(0, G.a_zoom_out);
  G.zoom = cw_clampf(G.zoom + (zout - zin) * 0.02f, 0.55f, 1.5f);
  switch (G.state) {
  case GS_TITLE:
#ifdef OTIUM_AUTOPILOT
    if (G.state_ticks > 60) new_game();
#endif
    if (G.state_ticks > 20 && (pressed(G.a_ok) || mouse_clicked)) new_game(), sfx(SFX_OK, 0.8f);
    break;
  case GS_PLAY: {
    if (pressed(G.a_pause)) {
      G.state = GS_PAUSE, G.state_ticks = 0;
      break;
    }
#ifndef OTIUM_AUTOPILOT
    play_input();
#endif
    G.waiting = cw_action_pressed(0, G.a_wait);
    /* the rivals act at this instant, then time runs while anyone is busy
     * and you are too, or while you hold the wait */
    sim_clear_events(&G.S);
    bots_act();
    bool run = !sim_idle(&G.S, 0) || G.waiting;
    if (run && !G.S.over) {
      sim_tick(&G.S);
      /* a queued order goes the moment you are free */
      if (G.has_queued && sim_idle(&G.S, 0)) {
        G.has_queued = false;
        if (!sim_order(&G.S, 0, &G.queued)) sfx(SFX_DENY, 0.6f);
      }
      bots_act();
    }
    sim_sounds();
    if (G.S.over) end_game();
    break;
  }
  case GS_PAUSE:
    if (cw_quality_menu_is_open()) break;
    if (G.state_ticks > 5 && (pressed(G.a_pause) || pressed(G.a_ok) || mouse_clicked)) G.state = GS_PLAY, G.state_ticks = 0;
    else if (G.state_ticks > 5 && pressed(G.a_back)) G.state = GS_TITLE, G.state_ticks = 0;
    break;
  case GS_END:
#ifdef OTIUM_AUTOPILOT
    if (G.state_ticks > 420) new_game();
#endif
    if (G.state_ticks > 60 && (pressed(G.a_ok) || mouse_clicked)) G.state = GS_TITLE, G.state_ticks = 0;
    break;
  }
  debug_fields();
}

/* ── per frame: the camera and the world ────────────────────────────── */

static float frame_t, frame_tick;
static t3_vec3 v3lerp(t3_vec3 a, t3_vec3 b, float k) { return (t3_vec3){ a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k }; }

static void sync_hook(cw_tree *t, double alpha, void *u) {
  (void)t, (void)u;
  float a = cw_clampf((float)alpha, 0, 1);
  bool running = G.state == GS_PLAY && (!sim_idle(&G.S, 0) || G.waiting);
  frame_tick = (float)G.S.tick + (running ? a : 0);
  frame_t = (G.ticks + a) / 60.0f;
  int path[MAX_PATH], pn = 0;
  bool show_path = G.state == GS_PLAY && (G.mode == MODE_MAP || (G.mode == MODE_WHEEL && G.wheel == O_MOVE));
  int su = G.sel_unit >= 0 && G.sel_unit < G.S.p[0].n_units ? G.sel_unit : sim_nearest_unit(&G.S, 0, G.cursor);
  if (show_path && su >= 0 && sim_idle(&G.S, 0)) sim_path(G.S.p[0].units[su].city, G.cursor, path, &pn);
  world_view v = { .cursor = G.cursor, .sel_unit = G.state == GS_PLAY ? su : -1, .preview_path = pn > 1 ? path : NULL, .preview_n = pn,
                   .title = G.state == GS_TITLE };
  world_sync(&G.S, frame_tick, frame_t, &v);
  /* the camera: high over the whole map on the title; over the cursor in play */
  t3_vec3 at, eye;
  if (G.state == GS_TITLE) {
    float o = frame_t * 0.05f;
    at = (t3_vec3){ 0.4f, 0, 0.3f };
    eye = (t3_vec3){ at.x + 4.5f * cw_sin(o), 9.5f, at.z + 7.5f * cw_cos(o) * 0.6f + 5.0f };
  } else {
    t3_vec3 c = world_city_pos(G.cursor);
    at = v3lerp(c, (t3_vec3){ 0.2f, 0, 0.3f }, 0.2f);
    at.y = 0;
    float z = G.zoom > 0 ? G.zoom : 1;
    eye = (t3_vec3){ at.x, 5.4f * z, at.z + 4.6f * z };
    if (G.state == GS_END) eye = (t3_vec3){ 0.3f, 11.5f, 7.5f }, at = (t3_vec3){ 0.3f, 0, 0.4f };
  }
  if (!G.cam_snap) G.cam_at = at, G.cam_eye = eye, G.cam_snap = true;
  else {
    float k = 0.07f;
    G.cam_at = v3lerp(G.cam_at, at, k), G.cam_eye = v3lerp(G.cam_eye, eye, k);
  }
  if (cw_node_valid(G.camera)) {
    cw_node3d_set_position(G.camera, G.cam_eye);
    cw_node3d_look_at(G.camera, G.cam_at, (t3_vec3){ 0, 1, 0 });
  }
  /* the sun's shadow square follows the view */
  if (cw_node_valid(G.sun)) {
    const t3_mat4 *m = cw_node3d_global_transform(G.sun);
    t3_vec3 zv = { m->e[8], m->e[9], m->e[10] };
    cw_node3d_set_position(G.sun, (t3_vec3){ G.cam_at.x + zv.x * 20, zv.y * 20, G.cam_at.z + zv.z * 20 });
  }
}

static void gfx_input(void);
static void update(void *u, double dt) {
  (void)u, (void)dt;
  gfx_input();
  cw_quality_auto_measure(G.state == GS_PLAY);
  if (A.on) cw_audio_update();
}

/* ── HUD: icons on parchment, numbers only ──────────────────────────── */

static cw_font font_title, font_num;
static float S_; /* the HUD's scale: canvas height / 540 */

static cw_color rgba(float r, float g, float b, float a) { return (cw_color){ r, g, b, a }; }
static const cw_color INK = { 0.24f, 0.16f, 0.10f, 1 };
static cw_color pcol(int p, float a) { return rgba(PLAYER_COLOR[p][0] * 1.15f, PLAYER_COLOR[p][1] * 1.15f, PLAYER_COLOR[p][2] * 1.15f, a); }

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

static void num(int n, float x, float y, float size, cw_color c, cw_text_align al) {
  char b[16];
  snprintf(b, sizeof b, "%d", n);
  cw_text_style st = { .size = size, .color = c, .align = al, .outline_width = size * 0.1f, .outline_color = rgba(1, 0.96f, 0.86f, 0.85f * c.a) };
  cw_r2d_text(font_num, b, cw_v2(x, y - size * 0.55f), &st);
}
static void label(const char *s, float x, float y, float size, cw_color c, cw_text_align al, float wrap) {
  cw_text_style st = { .size = size, .color = c, .align = al, .wrap_width = wrap, .outline_width = size * 0.05f, .outline_color = rgba(1, 0.95f, 0.85f, 0.6f * c.a) };
  cw_r2d_text(font_title, s, cw_v2(x, y), &st);
}

/* a parchment panel: a soft shadow, a cream fill, a sepia rule */
static void parch(cw_rect r, float a) {
  float rad = fminf(r.h * 0.2f, 12 * S_);
  cw_r2d_round_rect(cw_rect_make(r.x + 3 * S_, r.y + 4 * S_, r.w, r.h), rad, 8, rgba(0.1f, 0.06f, 0.03f, 0.28f * a));
  cw_r2d_round_rect(r, rad, 8, rgba(0.95f, 0.89f, 0.74f, 0.94f * a));
  cw_r2d_round_rect_lines(cw_rect_make(r.x + 3 * S_, r.y + 3 * S_, r.w - 6 * S_, r.h - 6 * S_), rad * 0.7f, 1.2f * S_, 8, rgba(0.45f, 0.3f, 0.18f, 0.7f * a));
}

static void coin(cw_vec2 c, float r, float a) {
  cw_r2d_circle(c, r, 20, rgba(0.62f, 0.45f, 0.12f, a));
  cw_r2d_circle(c, r * 0.86f, 20, rgba(0.95f, 0.78f, 0.30f, a));
  cw_r2d_circle_lines(c, r * 0.6f, r * 0.12f, 16, rgba(0.75f, 0.55f, 0.15f, a));
}

static void good_icon(int g, cw_vec2 c, float r, float a) {
  cw_color col = rgba(GOOD_COLOR[g][0], GOOD_COLOR[g][1], GOOD_COLOR[g][2], a), dark = rgba(0.2f, 0.12f, 0.08f, a);
  switch (g) {
  case GD_BRICK:
    for (int i = 0; i < 2; i++) {
      cw_rect b = cw_rect_make(c.x - r * (i ? 0.55f : 0.9f), c.y - r * (i ? 0.75f : 0.05f), r * (i ? 1.1f : 1.8f), r * 0.65f);
      cw_r2d_rect(b, col);
      cw_r2d_rect_lines(b, r * 0.12f, dark);
    }
    break;
  case GD_FOOD:
    cw_r2d_line(cw_v2(c.x, c.y + r), cw_v2(c.x, c.y - r * 0.9f), r * 0.14f, rgba(0.6f, 0.45f, 0.15f, a));
    for (int i = 0; i < 3; i++) {
      float y = c.y - r * 0.7f + i * r * 0.45f;
      cw_r2d_circle(cw_v2(c.x - r * 0.28f, y), r * 0.25f, 10, col);
      cw_r2d_circle(cw_v2(c.x + r * 0.28f, y), r * 0.25f, 10, col);
    }
    cw_r2d_circle(cw_v2(c.x, c.y - r * 0.95f), r * 0.22f, 10, col);
    break;
  case GD_TOOLS:
    cw_r2d_line(cw_v2(c.x - r * 0.6f, c.y + r * 0.8f), cw_v2(c.x + r * 0.35f, c.y - r * 0.35f), r * 0.24f, rgba(0.5f, 0.32f, 0.18f, a));
    {
      cw_vec2 h[4] = { cw_v2(c.x - r * 0.05f, c.y - r * 0.95f), cw_v2(c.x + r * 0.95f, c.y + r * 0.05f), cw_v2(c.x + r * 0.6f, c.y + r * 0.4f),
                       cw_v2(c.x - r * 0.4f, c.y - r * 0.6f) };
      cw_r2d_polygon(h, 4, col);
      cw_r2d_polygon_lines(h, 4, r * 0.1f, dark);
    }
    break;
  case GD_WINE:
    cw_r2d_circle(cw_v2(c.x, c.y + r * 0.15f), r * 0.62f, 18, col);
    cw_r2d_rect(cw_rect_make(c.x - r * 0.2f, c.y - r * 0.95f, r * 0.4f, r * 0.6f), col);
    cw_r2d_line(cw_v2(c.x - r * 0.2f, c.y - r * 0.7f), cw_v2(c.x - r * 0.55f, c.y - r * 0.15f), r * 0.12f, col);
    cw_r2d_line(cw_v2(c.x + r * 0.2f, c.y - r * 0.7f), cw_v2(c.x + r * 0.55f, c.y - r * 0.15f), r * 0.12f, col);
    cw_r2d_circle_lines(cw_v2(c.x, c.y + r * 0.15f), r * 0.62f, r * 0.1f, 18, dark);
    break;
  case GD_CLOTH: {
    cw_rect b = cw_rect_make(c.x - r * 0.85f, c.y - r * 0.6f, r * 1.7f, r * 1.2f);
    cw_r2d_round_rect(b, r * 0.3f, 6, col);
    for (int i = 0; i < 3; i++)
      cw_r2d_line(cw_v2(c.x - r * 0.5f + i * r * 0.5f, c.y - r * 0.55f), cw_v2(c.x - r * 0.75f + i * r * 0.5f, c.y + r * 0.55f), r * 0.1f, rgba(0.85f, 0.9f, 1, a * 0.8f));
    cw_r2d_round_rect_lines(b, r * 0.3f, r * 0.1f, 6, dark);
    break;
  }
  }
}

static void order_icon(int o, cw_vec2 c, float r, cw_color col) {
  switch (o) {
  case O_MOVE: { /* a cart wheel and an arrow */
    cw_vec2 ar[3] = { cw_v2(c.x + r, c.y - r * 0.2f), cw_v2(c.x + r * 0.35f, c.y - r * 0.75f), cw_v2(c.x + r * 0.35f, c.y + r * 0.35f) };
    cw_r2d_line(cw_v2(c.x - r * 0.9f, c.y - r * 0.2f), cw_v2(c.x + r * 0.4f, c.y - r * 0.2f), r * 0.28f, col);
    cw_r2d_polygon(ar, 3, col);
    cw_r2d_circle_lines(cw_v2(c.x - r * 0.35f, c.y + r * 0.55f), r * 0.32f, r * 0.13f, 14, col);
    break;
  }
  case O_BUILD: {
    cw_vec2 roof[3] = { cw_v2(c.x - r, c.y - r * 0.05f), cw_v2(c.x, c.y - r * 0.95f), cw_v2(c.x + r, c.y - r * 0.05f) };
    cw_r2d_polygon(roof, 3, col);
    cw_r2d_rect(cw_rect_make(c.x - r * 0.65f, c.y - r * 0.05f, r * 1.3f, r * 0.95f), col);
    cw_r2d_rect(cw_rect_make(c.x - r * 0.18f, c.y + r * 0.35f, r * 0.36f, r * 0.55f), rgba(0.95f, 0.89f, 0.74f, col.a));
    break;
  }
  case O_HARVEST: /* a sickle */
    cw_r2d_arc(cw_v2(c.x + r * 0.1f, c.y - r * 0.05f), r * 0.6f, r * 0.85f, 3.4f, 6.6f, 16, col);
    cw_r2d_line(cw_v2(c.x - r * 0.6f, c.y + r * 0.15f), cw_v2(c.x - r * 0.9f, c.y + r * 0.95f), r * 0.24f, col);
    break;
  case O_TRADE: /* scales */
    cw_r2d_line(cw_v2(c.x, c.y - r * 0.9f), cw_v2(c.x, c.y + r * 0.8f), r * 0.16f, col);
    cw_r2d_line(cw_v2(c.x - r * 0.85f, c.y - r * 0.6f), cw_v2(c.x + r * 0.85f, c.y - r * 0.6f), r * 0.16f, col);
    cw_r2d_arc(cw_v2(c.x - r * 0.6f, c.y - r * 0.05f), 0, r * 0.38f, 0, 3.14159f, 10, col);
    cw_r2d_arc(cw_v2(c.x + r * 0.6f, c.y - r * 0.05f), 0, r * 0.38f, 0, 3.14159f, 10, col);
    cw_r2d_rect(cw_rect_make(c.x - r * 0.45f, c.y + r * 0.75f, r * 0.9f, r * 0.2f), col);
    break;
  case O_RECRUIT: { /* a banner on a pole */
    cw_r2d_line(cw_v2(c.x - r * 0.5f, c.y - r * 0.95f), cw_v2(c.x - r * 0.5f, c.y + r * 0.95f), r * 0.16f, col);
    cw_vec2 f[4] = { cw_v2(c.x - r * 0.45f, c.y - r * 0.9f), cw_v2(c.x + r * 0.85f, c.y - r * 0.9f), cw_v2(c.x + r * 0.5f, c.y - r * 0.45f),
                     cw_v2(c.x + r * 0.85f, c.y), };
    cw_vec2 f2[4] = { f[0], f[1], f[2], cw_v2(c.x - r * 0.45f, c.y - r * 0.0f) };
    (void)f;
    cw_r2d_polygon(f2, 4, col);
    cw_r2d_line(cw_v2(c.x + r * 0.5f, c.y + r * 0.35f), cw_v2(c.x + r * 0.5f, c.y + r * 0.95f), r * 0.16f, col);
    cw_r2d_line(cw_v2(c.x + r * 0.2f, c.y + r * 0.65f), cw_v2(c.x + r * 0.8f, c.y + r * 0.65f), r * 0.16f, col);
    break;
  }
  case O_TEMPLE: {
    cw_vec2 ped[3] = { cw_v2(c.x - r, c.y - r * 0.45f), cw_v2(c.x, c.y - r * 0.95f), cw_v2(c.x + r, c.y - r * 0.45f) };
    cw_r2d_polygon(ped, 3, col);
    for (int i = 0; i < 4; i++) cw_r2d_rect(cw_rect_make(c.x - r * 0.8f + i * r * 0.48f, c.y - r * 0.35f, r * 0.17f, r * 1.0f), col);
    cw_r2d_rect(cw_rect_make(c.x - r, c.y + r * 0.7f, r * 2, r * 0.22f), col);
    break;
  }
  case O_REST: /* the sun going round: an arc and its arrowhead */
    cw_r2d_arc(c, r * 0.55f, r * 0.8f, 0.6f, 5.6f, 20, col);
    {
      float a = 0.6f;
      cw_vec2 tip = cw_v2(c.x + r * 0.68f * cw_cos(a), c.y + r * 0.68f * cw_sin(a));
      cw_vec2 tri[3] = { cw_v2(tip.x + r * 0.35f, tip.y - r * 0.05f), cw_v2(tip.x - r * 0.25f, tip.y - r * 0.25f), cw_v2(tip.x - r * 0.05f, tip.y + r * 0.4f) };
      cw_r2d_polygon(tri, 3, col);
    }
    cw_r2d_circle(c, r * 0.25f, 12, col);
    break;
  }
}

static void god_icon(int g, cw_vec2 c, float r, float a) {
  cw_color col = rgba(GOD_COLOR[g][0], GOD_COLOR[g][1], GOD_COLOR[g][2], a), dark = rgba(0.25f, 0.15f, 0.08f, a);
  cw_r2d_circle(c, r, 24, rgba(0.98f, 0.93f, 0.80f, a));
  cw_r2d_circle_lines(c, r, r * 0.1f, 24, col);
  float k = r * 0.62f;
  switch (g) {
  case GOD_JUPITER: { /* the thunderbolt */
    cw_vec2 b[6] = { cw_v2(c.x + k * 0.2f, c.y - k), cw_v2(c.x - k * 0.55f, c.y + k * 0.1f), cw_v2(c.x - k * 0.02f, c.y + k * 0.1f),
                     cw_v2(c.x - k * 0.25f, c.y + k), cw_v2(c.x + k * 0.6f, c.y - k * 0.2f), cw_v2(c.x + k * 0.05f, c.y - k * 0.2f) };
    cw_r2d_polygon(b, 6, col);
    cw_r2d_polygon_lines(b, 6, r * 0.06f, dark);
    break;
  }
  case GOD_SATURN: /* the sickle of the harvest */
    cw_r2d_arc(cw_v2(c.x + k * 0.1f, c.y), k * 0.65f, k * 0.95f, 3.5f, 6.5f, 16, col);
    cw_r2d_line(cw_v2(c.x - k * 0.6f, c.y + k * 0.15f), cw_v2(c.x - k * 0.85f, c.y + k), r * 0.15f, dark);
    break;
  case GOD_MERCURY: { /* the winged hat */
    cw_r2d_arc(cw_v2(c.x, c.y + k * 0.25f), 0, k * 0.55f, 3.14159f, 6.2832f, 14, col);
    cw_vec2 wl[3] = { cw_v2(c.x - k * 0.4f, c.y), cw_v2(c.x - k * 1.0f, c.y - k * 0.7f), cw_v2(c.x - k * 0.75f, c.y + k * 0.15f) };
    cw_vec2 wr[3] = { cw_v2(c.x + k * 0.4f, c.y), cw_v2(c.x + k * 0.75f, c.y + k * 0.15f), cw_v2(c.x + k * 1.0f, c.y - k * 0.7f) };
    cw_r2d_polygon(wl, 3, col);
    cw_r2d_polygon(wr, 3, col);
    cw_r2d_rect(cw_rect_make(c.x - k * 0.8f, c.y + k * 0.25f, k * 1.6f, k * 0.18f), dark);
    break;
  }
  case GOD_MARS: /* the shield and spear */
    cw_r2d_line(cw_v2(c.x - k * 0.8f, c.y + k * 0.8f), cw_v2(c.x + k * 0.85f, c.y - k * 0.85f), r * 0.1f, dark);
    cw_r2d_circle(cw_v2(c.x - k * 0.1f, c.y + k * 0.1f), k * 0.62f, 18, col);
    cw_r2d_circle(cw_v2(c.x - k * 0.1f, c.y + k * 0.1f), k * 0.2f, 10, dark);
    break;
  case GOD_VESTA: { /* the hearth flame */
    cw_vec2 f[7] = { cw_v2(c.x, c.y - k), cw_v2(c.x + k * 0.35f, c.y - k * 0.35f), cw_v2(c.x + k * 0.6f, c.y + k * 0.25f), cw_v2(c.x + k * 0.35f, c.y + k * 0.8f),
                     cw_v2(c.x - k * 0.35f, c.y + k * 0.8f), cw_v2(c.x - k * 0.6f, c.y + k * 0.25f), cw_v2(c.x - k * 0.2f, c.y - k * 0.3f) };
    cw_r2d_polygon(f, 7, col);
    cw_r2d_circle(cw_v2(c.x, c.y + k * 0.4f), k * 0.28f, 12, rgba(1, 0.9f, 0.5f, a));
    break;
  }
  }
}

/* a sundial: the share of a season the order takes, and its days */
static void sundial(cw_vec2 c, float r, int days, float a, cw_color tint) {
  cw_r2d_circle(c, r, 32, rgba(0.98f, 0.93f, 0.8f, a));
  float k = cw_clampf(days / (float)SEASON_DAYS, 0, 1);
  if (days > 0) cw_r2d_arc(c, 0, r * 0.92f, -1.5707963f, -1.5707963f + k * 6.2831853f, 32, rgba(tint.r, tint.g, tint.b, 0.55f * a));
  for (int i = 0; i < 12; i++) {
    float an = i * 0.5235988f;
    cw_r2d_line(cw_v2(c.x + r * 0.78f * cw_cos(an), c.y + r * 0.78f * cw_sin(an)), cw_v2(c.x + r * 0.92f * cw_cos(an), c.y + r * 0.92f * cw_sin(an)), r * 0.05f,
                rgba(0.4f, 0.28f, 0.18f, a));
  }
  cw_r2d_circle_lines(c, r, r * 0.08f, 32, rgba(0.45f, 0.3f, 0.18f, a));
  num(days, c.x, c.y, r * 0.95f, rgba(INK.r, INK.g, INK.b, a), CW_ALIGN_CENTER);
}

static const char *const ROMAN[SEASONS] = { "I", "II", "III", "IV", "V", "VI" };

/* the year across the top: seasons, now, when each house is next free */
static void draw_timeline(float W, int preview_days) {
  const sim *S = &G.S;
  float x0 = W * 0.27f, x1 = W * 0.73f, y = 22 * S_, h = 14 * S_;
  parch(cw_rect_make(x0 - 14 * S_, y - 14 * S_, x1 - x0 + 28 * S_, 44 * S_), 1);
  float span = x1 - x0;
#define TX(tk) (x0 + span * cw_clampf((float)(tk) / END_TICK, 0, 1))
  for (int s = 0; s < SEASONS; s++) {
    float a = TX(s * SEASON_DAYS * DAY), b = TX((s + 1) * SEASON_DAYS * DAY);
    cw_r2d_rect(cw_rect_make(a + 1, y, b - a - 2, h), s % 2 ? rgba(0.82f, 0.74f, 0.55f, 1) : rgba(0.86f, 0.79f, 0.6f, 1));
    label(ROMAN[s], (a + b) * 0.5f, y + h + 1 * S_, 9 * S_, rgba(0.4f, 0.28f, 0.18f, 0.9f), CW_ALIGN_CENTER, 0);
  }
  float now = TX(frame_tick);
  cw_r2d_rect(cw_rect_make(x0, y, now - x0, h), rgba(0.45f, 0.3f, 0.18f, 0.55f));
  /* the preview: where you would be free after this order */
  if (preview_days > 0) {
    float e = TX(S->tick + preview_days * DAY);
    cw_r2d_rect(cw_rect_make(now, y + h * 0.3f, e - now, h * 0.4f), pcol(0, 0.6f));
    cw_r2d_line(cw_v2(e, y - 3 * S_), cw_v2(e, y + h + 3 * S_), 2 * S_, pcol(0, 1));
  }
  /* each house's marker: when it is next free */
  for (int p = NP - 1; p >= 0; p--) {
    float fx = TX(S->p[p].free_at > S->tick ? S->p[p].free_at : S->tick);
    float yy = y - 6 * S_ - (p == 0 ? 0 : 0);
    cw_vec2 tri[3] = { cw_v2(fx - 6 * S_, yy - 8 * S_), cw_v2(fx + 6 * S_, yy - 8 * S_), cw_v2(fx, yy + 1 * S_) };
    cw_r2d_polygon(tri, 3, pcol(p, 1));
    cw_r2d_polygon_lines(tri, 3, 1.2f * S_, rgba(0.15f, 0.1f, 0.05f, 0.8f));
  }
  cw_r2d_line(cw_v2(now, y - 2 * S_), cw_v2(now, y + h + 2 * S_), 2.5f * S_, INK);
#undef TX
}

static void draw_purse(float W, float H) {
  (void)W, (void)H;
  const player *pl = &G.S.p[0];
  cw_rect r = cw_rect_make(12 * S_, 10 * S_, 200 * S_, 102 * S_);
  parch(r, 1);
  coin(cw_v2(r.x + 24 * S_, r.y + 24 * S_), 11 * S_, 1);
  num(pl->coins, r.x + 42 * S_, r.y + 24 * S_, 20 * S_, INK, CW_ALIGN_LEFT);
  /* carts */
  order_icon(O_MOVE, cw_v2(r.x + 116 * S_, r.y + 24 * S_), 11 * S_, pcol(0, 1));
  num(pl->n_units, r.x + 134 * S_, r.y + 24 * S_, 20 * S_, INK, CW_ALIGN_LEFT);
  order_icon(O_BUILD, cw_v2(r.x + 164 * S_, r.y + 24 * S_), 10 * S_, pcol(0, 1));
  num(pl->houses, r.x + 180 * S_, r.y + 24 * S_, 20 * S_, INK, CW_ALIGN_LEFT);
  for (int g = 0; g < GOODS; g++) {
    float x = r.x + 22 * S_ + g * 37 * S_;
    good_icon(g, cw_v2(x, r.y + 58 * S_), 10 * S_, 1);
    num(pl->goods[g], x + 12 * S_, r.y + 66 * S_, 15 * S_, INK, CW_ALIGN_LEFT);
  }
  /* the orders: spent ones dim until a rest */
  for (int o = 0; o < ORDERS; o++) {
    float x = r.x + 20 * S_ + o * 27 * S_;
    bool spent = o != O_MOVE && (pl->spent >> o & 1);
    order_icon(o, cw_v2(x, r.y + 88 * S_), 8 * S_, spent ? rgba(0.45f, 0.35f, 0.25f, 0.3f) : INK);
  }
}

static void draw_gods(float W, float H) {
  (void)H;
  const sim *S = &G.S;
  cw_rect r = cw_rect_make(W - 196 * S_, 10 * S_, 184 * S_, 232 * S_);
  parch(r, 1);
  for (int g = 0; g < GODS; g++) {
    float y = r.y + 26 * S_ + g * 36 * S_;
    god_icon(g, cw_v2(r.x + 22 * S_, y), 14 * S_, 1);
    /* each house's favor as pips, its count beside */
    for (int p = 0; p < NP; p++) {
      float x = r.x + 46 * S_ + p * 46 * S_;
      int f = S->p[p].favor[g];
      for (int i = 0; i < f && i < 6; i++) cw_r2d_circle(cw_v2(x + (i % 3) * 7 * S_, y - 6 * S_ + (i / 3) * 7 * S_), 2.8f * S_, 8, pcol(p, 1));
      num(sim_score(S, p, g), x + 24 * S_, y + 1 * S_, 13 * S_, p == 0 ? INK : rgba(0.35f, 0.27f, 0.2f, 0.85f), CW_ALIGN_LEFT);
    }
  }
  /* totals */
  for (int p = 0; p < NP; p++) {
    cw_vec2 c = cw_v2(r.x + 34 * S_ + p * 58 * S_, r.y + 206 * S_);
    cw_r2d_circle(c, 18 * S_, 24, pcol(p, 1));
    cw_r2d_circle_lines(c, 18 * S_, 2 * S_, 24, rgba(0.95f, 0.85f, 0.5f, 1));
    char b[16];
    snprintf(b, sizeof b, "%d", sim_total(S, p));
    cw_text_style st = { .size = 17 * S_, .color = rgba(1, 0.97f, 0.9f, 1), .align = CW_ALIGN_CENTER, .wrap_width = 40 * S_ };
    cw_r2d_text(font_num, b, cw_v2(c.x - 20 * S_, c.y - 10 * S_), &st);
  }
}

/* small marks over the cities: the good each makes, a fallow sickle, the rivals' busy orders */
static void draw_city_marks(float W, float H) {
  const sim *S = &G.S;
  for (int c = 0; c < N_CITIES; c++) {
    cw_vec2 sp;
    t3_vec3 p = world_city_pos(c);
    p.y += 0.26f;
    if (!project(p, W, H, &sp)) continue;
    if (CITIES[c].good >= 0) {
      cw_r2d_circle(sp, 9 * S_, 16, rgba(0.98f, 0.93f, 0.8f, 0.85f));
      good_icon(CITIES[c].good, sp, 6 * S_, 1);
      if (S->fallow[CITIES[c].region]) {
        cw_r2d_line(cw_v2(sp.x - 8 * S_, sp.y - 8 * S_), cw_v2(sp.x + 8 * S_, sp.y + 8 * S_), 2 * S_, rgba(0.5f, 0.2f, 0.1f, 0.85f));
      }
    }
  }
  /* what each rival is doing, at its order's city, with a ring that fills as it ends */
  for (int pi = 1; pi < NP; pi++) {
    const player *pl = &S->p[pi];
    if (pl->order == O_NONE || pl->free_at <= S->tick) continue;
    int c = pl->order == O_MOVE ? pl->order_city : pl->order_city;
    cw_vec2 sp;
    t3_vec3 p = world_city_pos(c >= 0 ? c : CAPITAL);
    p.y += 0.3f;
    if (!project(p, W, H, &sp)) continue;
    sp.x += (pi == 1 ? -1 : 1) * 14 * S_;
    sp.y -= 12 * S_;
    int start = pl->free_at - (pl->order == O_MOVE ? pl->units[pl->order_unit >= 0 ? pl->order_unit : 0].t1 - pl->units[pl->order_unit >= 0 ? pl->order_unit : 0].t0 : ORDER_DAYS[pl->order] * DAY);
    float k = cw_clampf((frame_tick - start) / (float)(pl->free_at - start), 0, 1);
    cw_r2d_circle(sp, 13 * S_, 20, rgba(0.98f, 0.93f, 0.8f, 0.92f));
    cw_r2d_arc(sp, 11 * S_, 14 * S_, -1.5707963f, -1.5707963f + k * 6.2831853f, 24, pcol(pi, 1));
    order_icon(pl->order, sp, 7 * S_, pcol(pi, 1));
  }
}

/* the cursor's city: what a house there costs, who has one */
static void draw_city_card(float W, float H) {
  const sim *S = &G.S;
  int c = G.cursor;
  cw_vec2 sp;
  t3_vec3 p = world_city_pos(c);
  if (!project(p, W, H, &sp)) return;
  float w = 150 * S_, h = 64 * S_;
  cw_rect r = cw_rect_make(cw_clampf(sp.x + 26 * S_, 8 * S_, W - w - 200 * S_), cw_clampf(sp.y - h - 20 * S_, 60 * S_, H - h - 40 * S_), w, h);
  parch(r, 0.95f);
  label(CITIES[c].name, r.x + 10 * S_, r.y + 6 * S_, 14 * S_, INK, CW_ALIGN_LEFT, 0);
  for (int i = 0; i < NP; i++) {
    cw_rect hr = cw_rect_make(r.x + w - 52 * S_ + i * 15 * S_, r.y + 9 * S_, 11 * S_, 11 * S_);
    if (S->house[c] >> i & 1) cw_r2d_rect(hr, pcol(i, 1));
    else cw_r2d_rect_lines(hr, 1.2f * S_, pcol(i, 0.6f));
  }
  if (CITIES[c].good >= 0) {
    good_icon(CITIES[c].good, cw_v2(r.x + 20 * S_, r.y + 42 * S_), 10 * S_, 1);
    /* a house's price here */
    int goods[GOODS], coins = sim_house_cost(S, 0, c, goods);
    float x = r.x + 48 * S_;
    order_icon(O_BUILD, cw_v2(x, r.y + 42 * S_), 8 * S_, rgba(0.4f, 0.28f, 0.18f, 1));
    x += 16 * S_;
    for (int g = 0; g < GOODS; g++)
      for (int k = 0; k < goods[g]; k++) good_icon(g, cw_v2(x, r.y + 42 * S_), 8 * S_, 1), x += 18 * S_;
    coin(cw_v2(x + 2 * S_, r.y + 42 * S_), 7 * S_, 1);
    num(coins, x + 12 * S_, r.y + 43 * S_, 14 * S_, S->p[0].coins >= coins ? INK : rgba(0.7f, 0.15f, 0.1f, 1), CW_ALIGN_LEFT);
    if (S->fallow[CITIES[c].region]) order_icon(O_HARVEST, cw_v2(r.x + w - 16 * S_, r.y + 42 * S_), 8 * S_, rgba(0.55f, 0.2f, 0.1f, 0.8f));
  } else {
    order_icon(O_TEMPLE, cw_v2(r.x + 22 * S_, r.y + 42 * S_), 10 * S_, rgba(0.4f, 0.28f, 0.18f, 1));
  }
}

static int wheel_days(int o, int *why) {
  order_req r = wheel_req(o);
  int d = 0;
  int w = sim_check(&G.S, 0, &r, &d);
  if (w == WHY_BUSY) { /* while busy: the check as if free at the end */
    sim T = G.S;
    T.tick = T.p[0].free_at;
    w = sim_check(&T, 0, &r, &d);
  }
  if (why) *why = w;
  if (o == O_TRADE || o == O_TEMPLE) *why = G.S.p[0].spent >> o & 1 ? WHY_SPENT : WHY_OK;
  return d;
}

static void draw_wheel(float W, float H) {
  cw_vec2 c = cw_v2(W * 0.5f, H * 0.56f);
  float R = 88 * S_;
  cw_r2d_circle(c, R + 36 * S_, 48, rgba(0.1f, 0.06f, 0.03f, 0.25f));
  cw_r2d_circle(c, R + 32 * S_, 48, rgba(0.95f, 0.89f, 0.74f, 0.9f));
  cw_r2d_circle_lines(c, R + 27 * S_, 1.5f * S_, 48, rgba(0.45f, 0.3f, 0.18f, 0.7f));
  int sel_days = 0, sel_why = WHY_OK;
  for (int o = 0; o < ORDERS; o++) {
    float a = o * 6.2831853f / ORDERS;
    cw_vec2 p = cw_v2(c.x + R * cw_sin(a), c.y - R * cw_cos(a));
    int why;
    int d = wheel_days(o, &why);
    bool sel = o == G.wheel, ok = why == WHY_OK;
    if (sel) {
      sel_days = d, sel_why = why;
      if (G.deny_ticks > 0) p.x += 3 * S_ * cw_sin(G.deny_ticks * 1.7f);
    }
    cw_r2d_circle(p, (sel ? 27 : 22) * S_, 24, sel ? rgba(0.98f, 0.82f, 0.4f, 1) : rgba(0.92f, 0.85f, 0.68f, 1));
    cw_r2d_circle_lines(p, (sel ? 27 : 22) * S_, 2 * S_, 24, rgba(0.45f, 0.3f, 0.18f, 1));
    order_icon(o, p, (sel ? 15 : 12) * S_, ok ? INK : rgba(0.45f, 0.35f, 0.25f, 0.35f));
    if (!ok) cw_r2d_line(cw_v2(p.x - 10 * S_, p.y + 10 * S_), cw_v2(p.x + 10 * S_, p.y - 10 * S_), 2.5f * S_, rgba(0.6f, 0.2f, 0.1f, 0.6f));
  }
  /* the middle: the days it costs, and what it pays */
  sundial(c, 34 * S_, sel_days, 1, pcol(0, 1));
  /* what the selected order costs or gives, under the wheel */
  const sim *S = &G.S;
  float y = c.y + R + 50 * S_, x = c.x;
  if (G.wheel == O_BUILD && sel_why == WHY_OK) {
    int list[MAX_UNITS], n = sim_buildable(S, 0, list);
    float xx = x - n * 14 * S_;
    for (int i = 0; i < n; i++) order_icon(O_BUILD, cw_v2(xx + i * 28 * S_, y), 10 * S_, pcol(0, 1));
  } else if (G.wheel == O_RECRUIT) {
    good_icon(GD_FOOD, cw_v2(x - 40 * S_, y), 9 * S_, 1);
    good_icon(GD_TOOLS, cw_v2(x - 16 * S_, y), 9 * S_, 1);
    coin(cw_v2(x + 10 * S_, y), 8 * S_, 1);
    num(3, x + 22 * S_, y, 15 * S_, INK, CW_ALIGN_LEFT);
  } else if (G.wheel == O_REST) {
    int n = 0;
    for (int o = 0; o < ORDERS; o++) n += S->p[0].spent >> o & 1;
    coin(cw_v2(x - 10 * S_, y), 8 * S_, 1);
    num(n + 1, x + 2 * S_, y, 15 * S_, INK, CW_ALIGN_LEFT);
  } else if (G.wheel == O_HARVEST && CITIES[G.cursor].region >= 0) {
    int reg = CITIES[G.cursor].region;
    int gain[GOODS] = { 0 };
    for (int cc = 0; cc < N_CITIES; cc++)
      if (CITIES[cc].region == reg && (S->house[cc] & 1)) gain[CITIES[cc].good]++;
    gain[REGIONS[reg].bonus]++;
    float xx = x - 40 * S_;
    for (int g = 0; g < GOODS; g++)
      for (int k = 0; k < gain[g]; k++) good_icon(g, cw_v2(xx, y), 9 * S_, 1), xx += 22 * S_;
  }
}

static void draw_trade(float W, float H) {
  const sim *S = &G.S;
  cw_rect r = cw_rect_make(W * 0.5f - 160 * S_, H * 0.3f, 320 * S_, 230 * S_);
  parch(r, 1);
  order_icon(O_TRADE, cw_v2(r.x + 24 * S_, r.y + 22 * S_), 12 * S_, INK);
  int lots = 0;
  for (int g = 0; g < GOODS; g++) lots += abs(G.trade[g]);
  for (int i = 0; i < TRADE_LOTS; i++) cw_r2d_circle(cw_v2(r.x + r.w - 60 * S_ + i * 18 * S_, r.y + 22 * S_), 6 * S_, 12, i < lots ? pcol(0, 1) : rgba(0.5f, 0.4f, 0.3f, 0.3f));
  for (int g = 0; g < GOODS; g++) {
    float y = r.y + 54 * S_ + g * 32 * S_;
    if (g == G.trade_row) cw_r2d_round_rect(cw_rect_make(r.x + 10 * S_, y - 14 * S_, r.w - 20 * S_, 28 * S_), 6 * S_, 6, rgba(0.98f, 0.82f, 0.4f, 0.6f));
    good_icon(g, cw_v2(r.x + 30 * S_, y), 10 * S_, 1);
    num(S->p[0].goods[g], r.x + 48 * S_, y, 15 * S_, INK, CW_ALIGN_LEFT);
    coin(cw_v2(r.x + 96 * S_, y), 7 * S_, 1);
    num(S->price[g], r.x + 108 * S_, y, 15 * S_, INK, CW_ALIGN_LEFT);
    int q = G.trade[g];
    /* arrows: left sells, right buys */
    cw_vec2 la[3] = { cw_v2(r.x + 170 * S_, y), cw_v2(r.x + 180 * S_, y - 7 * S_), cw_v2(r.x + 180 * S_, y + 7 * S_) };
    cw_vec2 ra[3] = { cw_v2(r.x + 250 * S_, y), cw_v2(r.x + 240 * S_, y - 7 * S_), cw_v2(r.x + 240 * S_, y + 7 * S_) };
    cw_r2d_polygon(la, 3, rgba(0.4f, 0.28f, 0.18f, g == G.trade_row ? 1 : 0.4f));
    cw_r2d_polygon(ra, 3, rgba(0.4f, 0.28f, 0.18f, g == G.trade_row ? 1 : 0.4f));
    if (q) num(q > 0 ? q : -q, r.x + 210 * S_, y, 17 * S_, q > 0 ? rgba(0.15f, 0.45f, 0.2f, 1) : rgba(0.7f, 0.2f, 0.1f, 1), CW_ALIGN_CENTER);
    if (q) {
      /* a little + (buy) or - (sell) */
      float sx = r.x + 226 * S_;
      cw_r2d_line(cw_v2(sx - 4 * S_, y), cw_v2(sx + 4 * S_, y), 2 * S_, INK);
      if (q > 0) cw_r2d_line(cw_v2(sx, y - 4 * S_), cw_v2(sx, y + 4 * S_), 2 * S_, INK);
    }
  }
  int delta = 0;
  sim_trade_cost(S, G.trade, &delta);
  coin(cw_v2(r.x + r.w - 80 * S_, r.y + r.h - 22 * S_), 9 * S_, 1);
  char b[16];
  snprintf(b, sizeof b, "%+d", delta);
  cw_text_style st = { .size = 18 * S_, .color = delta >= 0 ? rgba(0.15f, 0.45f, 0.2f, 1) : rgba(0.7f, 0.2f, 0.1f, 1) };
  cw_r2d_text(font_num, b, cw_v2(r.x + r.w - 64 * S_, r.y + r.h - 33 * S_), &st);
}

static void draw_temple(float W, float H) {
  const sim *S = &G.S;
  float cw = 120 * S_, gap = 16 * S_, x0 = W * 0.5f - (cw * OFFERS + gap * (OFFERS - 1)) * 0.5f, y = H * 0.32f;
  for (int i = 0; i < OFFERS; i++) {
    const offer *o = &S->offers[i];
    float x = x0 + i * (cw + gap);
    bool sel = i == G.temple_sel;
    cw_rect r = cw_rect_make(x, y - (sel ? 8 * S_ : 0), cw, 170 * S_);
    parch(r, 1);
    if (sel) cw_r2d_round_rect_lines(cw_rect_make(r.x - 3 * S_, r.y - 3 * S_, r.w + 6 * S_, r.h + 6 * S_), 12 * S_, 3 * S_, 8, rgba(0.95f, 0.7f, 0.25f, 1));
    god_icon(o->god, cw_v2(x + cw * 0.5f, r.y + 48 * S_), 30 * S_, 1);
    /* what you have with this god now, and what one more favor adds */
    int now = S->p[0].favor[o->god], cnt = sim_score(S, 0, o->god);
    for (int k = 0; k < now + 1 && k < 7; k++) cw_r2d_circle(cw_v2(x + cw * 0.5f - now * 5 * S_ + k * 10 * S_, r.y + 90 * S_), 3.5f * S_, 10, k < now ? pcol(0, 1) : rgba(0.95f, 0.7f, 0.25f, 1));
    char b[16];
    snprintf(b, sizeof b, "+%d", cnt);
    cw_text_style st = { .size = 18 * S_, .color = rgba(0.15f, 0.45f, 0.2f, 1), .align = CW_ALIGN_CENTER, .wrap_width = cw };
    cw_r2d_text(font_num, b, cw_v2(x, r.y + 100 * S_), &st);
    float gx = x + 16 * S_;
    for (int g = 0; g < GOODS; g++)
      for (int k = 0; k < o->cost[g]; k++) good_icon(g, cw_v2(gx, r.y + 146 * S_), 9 * S_, 1), gx += 22 * S_;
    coin(cw_v2(gx + 4 * S_, r.y + 146 * S_), 8 * S_, 1);
    num(o->coins, gx + 16 * S_, r.y + 147 * S_, 15 * S_, INK, CW_ALIGN_LEFT);
  }
}

/* the controls, as button glyphs */
static void glyph(const char *k, cw_vec2 c, float r) {
  cw_r2d_circle(c, r, 18, rgba(0.25f, 0.17f, 0.1f, 0.85f));
  cw_text_style st = { .size = r * 1.15f, .color = rgba(1, 0.95f, 0.85f, 1), .align = CW_ALIGN_CENTER, .wrap_width = r * 4 };
  cw_r2d_text(font_num, k, cw_v2(c.x - r * 2, c.y - r * 0.62f), &st);
}
static void draw_help(float W, float H) {
  bool pad = last_device == DEV_PAD;
  float x = W - 16 * S_, y = H - 22 * S_, r = 10 * S_;
  struct { const char *k; int icon; } items[4];
  int n = 0;
  if (G.mode == MODE_MAP) {
    items[n++] = (typeof(items[0])){ pad ? "A" : "\xe2\x86\xb5", -1 };
    items[n++] = (typeof(items[0])){ pad ? "RT" : "Tab", -2 };
    items[n++] = (typeof(items[0])){ pad ? "RB" : "E", O_MOVE };
  } else {
    items[n++] = (typeof(items[0])){ pad ? "A" : "\xe2\x86\xb5", -3 };
    items[n++] = (typeof(items[0])){ pad ? "B" : "Esc", -4 };
  }
  for (int i = 0; i < n; i++) {
    float cx = x - 22 * S_ - i * 70 * S_;
    glyph(items[i].k, cw_v2(cx - 22 * S_, y), r);
    cw_vec2 ic = cw_v2(cx, y);
    if (items[i].icon == -1) cw_r2d_circle_lines(ic, 9 * S_, 2.5f * S_, 20, INK), cw_r2d_circle(ic, 3 * S_, 8, INK); /* the wheel */
    else if (items[i].icon == -2) sundial(ic, 10 * S_, 0, 1, INK);
    else if (items[i].icon == -3) cw_r2d_polyline((cw_vec2[]){ cw_v2(ic.x - 7 * S_, ic.y), cw_v2(ic.x - 2 * S_, ic.y + 6 * S_), cw_v2(ic.x + 8 * S_, ic.y - 6 * S_) }, 3, 3 * S_, false, INK);
    else if (items[i].icon == -4) cw_r2d_line(cw_v2(ic.x - 6 * S_, ic.y - 6 * S_), cw_v2(ic.x + 6 * S_, ic.y + 6 * S_), 3 * S_, INK), cw_r2d_line(cw_v2(ic.x + 6 * S_, ic.y - 6 * S_), cw_v2(ic.x - 6 * S_, ic.y + 6 * S_), 3 * S_, INK);
    else order_icon(items[i].icon, ic, 9 * S_, INK);
  }
  /* a queued order waits beside your marker */
  if (G.has_queued) {
    cw_vec2 c = cw_v2(W * 0.73f + 32 * S_, 30 * S_);
    cw_r2d_circle(c, 13 * S_, 20, pcol(0, 0.9f));
    order_icon(G.queued.order, c, 8 * S_, rgba(1, 0.95f, 0.85f, 1));
  }
  /* waiting: the sundial turning */
  if (G.waiting && sim_idle(&G.S, 0)) {
    cw_vec2 c = cw_v2(W * 0.5f, H * 0.86f);
    sundial(c, 22 * S_, (G.S.tick / DAY) % SEASON_DAYS, 0.9f, INK);
  }
}

static void draw_title(float W, float H) {
  float in = cw_clampf(G.state_ticks / 40.0f, 0, 1);
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.08f, 0.05f, 0.02f, 0.25f * in));
  cw_rect r = cw_rect_make(W * 0.5f - 230 * S_, H * 0.16f, 460 * S_, 150 * S_);
  parch(r, in);
  cw_text_style st = { .size = 96 * S_, .color = rgba(0.55f, 0.12f, 0.08f, in), .align = CW_ALIGN_CENTER, .wrap_width = r.w, .outline_width = 2 * S_,
                       .outline_color = rgba(0.3f, 0.18f, 0.08f, in) };
  cw_r2d_text(font_title, OTIUM_TITLE, cw_v2(r.x, r.y + 14 * S_), &st);
  /* a laurel of the five gods under the name */
  for (int g = 0; g < GODS; g++) god_icon(g, cw_v2(W * 0.5f + (g - 2) * 44 * S_, r.y + 124 * S_), 15 * S_, in);
  float p = 0.6f + 0.4f * cw_sin(frame_t * 3);
  glyph(last_device == DEV_PAD ? "A" : "\xe2\x86\xb5", cw_v2(W * 0.5f, H * 0.84f), 18 * S_ * (0.9f + 0.1f * p));
  if (SAVE.games > 0) {
    order_icon(O_TEMPLE, cw_v2(W * 0.5f - 40 * S_, H * 0.93f), 9 * S_, rgba(1, 0.95f, 0.85f, 0.9f));
    num(SAVE.wins, W * 0.5f - 26 * S_, H * 0.93f, 16 * S_, rgba(0.2f, 0.13f, 0.08f, 1), CW_ALIGN_LEFT);
    coin(cw_v2(W * 0.5f + 20 * S_, H * 0.93f), 8 * S_, 0.9f);
    num(SAVE.best, W * 0.5f + 32 * S_, H * 0.93f, 16 * S_, rgba(0.2f, 0.13f, 0.08f, 1), CW_ALIGN_LEFT);
  }
}

static void draw_end(float W, float H) {
  const sim *S = &G.S;
  float in = cw_clampf(G.state_ticks / 30.0f, 0, 1);
  cw_rect r = cw_rect_make(W * 0.5f - 250 * S_, H * 0.2f, 500 * S_, 300 * S_);
  parch(r, in);
  int order[NP] = { 0, 1, 2 };
  for (int i = 0; i < NP; i++)
    for (int j = i + 1; j < NP; j++)
      if (sim_total(S, order[j]) > sim_total(S, order[i])) {
        int t = order[i];
        order[i] = order[j], order[j] = t;
      }
  for (int g = 0; g < GODS; g++) god_icon(g, cw_v2(r.x + 150 * S_ + g * 66 * S_, r.y + 40 * S_), 17 * S_, in);
  for (int i = 0; i < NP; i++) {
    int p = order[i];
    float y = r.y + 100 * S_ + i * 62 * S_;
    cw_vec2 m = cw_v2(r.x + 60 * S_, y);
    cw_r2d_circle(m, (i == 0 ? 26 : 21) * S_, 28, pcol(p, in));
    if (i == 0) { /* the laurel */
      for (int k = 0; k < 7; k++) {
        float a = 2.3f + k * 0.28f;
        cw_r2d_circle(cw_v2(m.x + 32 * S_ * cw_cos(a), m.y + 32 * S_ * cw_sin(a)), 4 * S_, 8, rgba(0.45f, 0.6f, 0.25f, in));
        cw_r2d_circle(cw_v2(m.x - 32 * S_ * cw_cos(a), m.y + 32 * S_ * cw_sin(a)), 4 * S_, 8, rgba(0.45f, 0.6f, 0.25f, in));
      }
    }
    cw_text_style st = { .size = 20 * S_, .color = rgba(1, 0.97f, 0.9f, in), .align = CW_ALIGN_CENTER, .wrap_width = 60 * S_ };
    char b[16];
    snprintf(b, sizeof b, "%d", sim_total(S, p));
    cw_r2d_text(font_num, b, cw_v2(m.x - 30 * S_, m.y - 12 * S_), &st);
    for (int g = 0; g < GODS; g++) {
      int f = S->p[p].favor[g], c = sim_score(S, p, g);
      float x = r.x + 150 * S_ + g * 66 * S_;
      for (int k = 0; k < f && k < 6; k++) cw_r2d_circle(cw_v2(x - 14 * S_ + (k % 3) * 7 * S_, y - 9 * S_ + (k / 3) * 7 * S_), 2.6f * S_, 8, pcol(p, in));
      num(f * c, x + 14 * S_, y, 16 * S_, rgba(INK.r, INK.g, INK.b, in), CW_ALIGN_LEFT);
    }
  }
}

static void draw_pause(float W, float H) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.08f, 0.05f, 0.02f, 0.4f));
  cw_rect r = cw_rect_make(W * 0.5f - 130 * S_, H * 0.32f, 260 * S_, 150 * S_);
  parch(r, 1);
  cw_r2d_rect(cw_rect_make(W * 0.5f - 22 * S_, r.y + 24 * S_, 14 * S_, 48 * S_), INK);
  cw_r2d_rect(cw_rect_make(W * 0.5f + 8 * S_, r.y + 24 * S_, 14 * S_, 48 * S_), INK);
  bool pad = last_device == DEV_PAD;
  glyph(pad ? "A" : "\xe2\x86\xb5", cw_v2(W * 0.5f - 60 * S_, r.y + 110 * S_), 12 * S_);
  cw_vec2 ar[3] = { cw_v2(W * 0.5f - 40 * S_, r.y + 102 * S_), cw_v2(W * 0.5f - 40 * S_, r.y + 118 * S_), cw_v2(W * 0.5f - 26 * S_, r.y + 110 * S_) };
  cw_r2d_polygon(ar, 3, INK);
  glyph(pad ? "B" : "Esc", cw_v2(W * 0.5f + 30 * S_, r.y + 110 * S_), 12 * S_);
  order_icon(O_TEMPLE, cw_v2(W * 0.5f + 56 * S_, r.y + 110 * S_), 10 * S_, INK);
  glyph(pad ? "Y" : "G", cw_v2(W * 0.5f + 100 * S_, r.y + 26 * S_), 9 * S_);
}

static void render(void *u, double alpha) {
  (void)u, (void)alpha;
  cw_r2d_begin(&(cw_r2d_pass_desc){ .clear = false });
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y;
  S_ = H / 540.0f;
  if (G.state == GS_TITLE) draw_title(W, H);
  else {
    int preview = 0;
    if (G.state == GS_PLAY && G.mode == MODE_WHEEL) {
      int why;
      preview = wheel_days(G.wheel, &why);
    }
    draw_city_marks(W, H);
    if (G.state == GS_PLAY && G.mode == MODE_MAP) draw_city_card(W, H);
    draw_timeline(W, preview);
    draw_purse(W, H);
    draw_gods(W, H);
    if (G.state == GS_PLAY) {
      if (G.mode == MODE_WHEEL) draw_wheel(W, H);
      if (G.mode == MODE_TRADE) draw_trade(W, H);
      if (G.mode == MODE_TEMPLE) draw_temple(W, H);
      draw_help(W, H);
    }
    if (G.state == GS_PAUSE) draw_pause(W, H);
    if (G.state == GS_END) draw_end(W, H);
  }
  cw_quality_menu_draw(&(cw_quality_menu_style){ .font = font_num, .accent = rgba(0.75f, 0.2f, 0.12f, 1) });
  cw_r2d_end();
}

/* the graphics page from the pause card: Y / G opens it */
static void gfx_input(void) {
  if (G.state != GS_PAUSE) {
    cw_quality_menu_close();
    return;
  }
  if (cw_quality_menu_is_open()) {
    cw_quality_menu_update();
    if (!cw_quality_menu_is_open() && cw_quality_menu_changed()) save_write();
    return;
  }
  if (G.state_ticks < 5) return;
  const cw_raw_input *in = cw_input_raw();
  bool hit = in->keys_pressed[10]; /* G */
  for (int i = 0; i < CW_MAX_PADS; i++) hit |= (in->pads[i].pressed & (1u << CW_PAD_NORTH)) != 0;
  if (hit) cw_quality_menu_open();
}

/* ── the app ────────────────────────────────────────────────────────── */

static void init(void *u) {
  (void)u;
  save_check();
  cw_quality_restore(gfx_saved());
  cw_quality_auto(true, 0);
  if (!cw_input_load_bindings_asset("input.cfg")) cw_warn("otium: input.cfg had errors");
  cw_input_commit_defaults();
  G.a_left = cw_action_find("left");
  G.a_right = cw_action_find("right");
  G.a_up = cw_action_find("up");
  G.a_down = cw_action_find("down");
  G.a_ok = cw_action_find("ok");
  G.a_back = cw_action_find("back");
  G.a_wait = cw_action_find("wait");
  G.a_prev = cw_action_find("prev");
  G.a_next = cw_action_find("next");
  G.a_pause = cw_action_find("pause");
  G.a_zoom_in = cw_action_find("zoom_in");
  G.a_zoom_out = cw_action_find("zoom_out");
  G.zoom = 0.85f;
  audio_init();
  const cw_font_desc fd = { .mode = CW_FONT_SDF, .sdf_padding = 8 };
  font_title = cw_font_load("fonts/Cinzel-VF.ttf", &fd);
  font_num = cw_font_load("fonts/Lato-Bold.ttf", &fd);
  cw_font_preload_range(font_title, 32, 126);
  cw_font_preload_range(font_num, 32, 126);
  G.scene = cw_scene_instance_path("scenes/table.cwscene");
  cw_node_add_child(cw_tree_root(cw_scene_app_tree()), G.scene);
  G.camera = cw_node_get(G.scene, "Camera");
  G.sun = cw_node_get(G.scene, "Sun");
  world_build(G.scene);
  sim_start(&G.S, 1);
  G.cursor = CAPITAL;
  G.sel_unit = -1;
  cw_debug_i32("game.state", &G.state);
  cw_debug_i32("game.mode", &G.mode);
  cw_debug_i32("ticks", &G.ticks);
  cw_debug_i32("sim.tick", &G.tick);
  cw_debug_i32("sim.season", &G.season);
  cw_debug_i32("human.idle", &G.idle);
  cw_debug_i32("cursor", &G.cursor);
  cw_debug_i32("wheel", &G.wheel);
  cw_debug_i32("winner", &G.winner);
  cw_debug_field("players.coins", G.dbg_coins, CW_DBG_I32, NP);
  cw_debug_field("players.houses", G.dbg_houses, CW_DBG_I32, NP);
  cw_debug_field("players.units", G.dbg_units, CW_DBG_I32, NP);
  cw_debug_field("players.total", G.dbg_total, CW_DBG_I32, NP);
  cw_debug_field("players.orders", G.dbg_orders, CW_DBG_I32, NP);
  cw_debug_field("players.free_at", G.dbg_free, CW_DBG_I32, NP);
  cw_debug_u32("state.hash", &G.state_hash);
  cw_debug_u32("tex.hash", &tex_hash);
  cw_debug_u32("world.hash", &world_hash);
  cw_debug_i32("input.mode", &input_mode);
  cw_debug_i32("save.games", &SAVE.games);
  cw_tree_add_hook(cw_scene_app_tree(), 2, 0, sync_hook, NULL);
  G.state = GS_TITLE;
  if (getenv("OTIUM_PLAY")) new_game(); /* dev aid: straight into a game */
  debug_fields();
}

static void shutdown(void *u) {
  (void)u;
  world_free();
  if (A.on) cw_audio_shutdown();
}

cw_app_desc cw_main(int argc, char **argv) {
  (void)argc, (void)argv;
  cw_scene_register_builtin_types();
  static cw_app_desc base = { .title = "Otium", .width = 960, .height = 540, .debug = true, .init = init, .fixed_update = fixed, .update = update,
                              .render = render, .shutdown = shutdown, .save = &SAVE, .save_size = sizeof SAVE, .touch_pointer = true };
  return cw_scene_app(NULL, &base);
}
