/* racer's audio: engines, tyres and wind, one-shots for every sim event, the tunnel's echo, and the music.
 *
 * Every sound is a file made by tools/make-audio.mjs and tools/make-music.mjs (nothing is synthesised here: the
 * game only picks, pitches and mixes). The sim is never touched; this reads it on the fixed tick.
 *
 * Mix, by bus (all into the master, which has a compressor and a limiter):
 *   music    the track of the map (a streamed Ogg), ducked under big events and the countdown
 *   engines  one looping voice per kart: the followed karts are centred 2D voices, the nearest other karts are
 *            3D voices with doppler and distance (a budget of ENG_OTHERS), the rest one distant bed
 *   amb      tyre noise by surface, squeal, wind, water: only for the followed karts
 *   sfx      the one-shots (events); 3D when they happen to someone else
 *   ui       menu sounds, never ducked
 *   voice    the announcer (announcer.c): one line at a time; the music, engines and ambience duck under it
 *   tunnel   a reverb that engines, ambience and sfx send into while a view is inside a tunnel
 * Split screen: each view has a listener (the camera's place, the followed kart's heading); a followed kart's own
 * sounds are 2D and panned toward its view's side of the screen so four players stay apart. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/resource.h"
#include "ui.h"
#include "world.h"

#define ENG_OTHERS 6    /* other karts that get an engine voice (the nearest to any view) */
#define ENG_MAX_DIST 170.0f
#define NGEARS 5
#define MAX_MISSILE_VOICES 4

/* ── the sounds ─────────────────────────────────────────────────────── */

enum { B_MUSIC, B_TUNNEL, B_SFX, B_ENG, B_AMB, B_UI, B_COUNT };

typedef struct snd_def {
  const char *file;
  int max_voices, priority, bus, cooldown; /* cooldown: ticks before the same sound may start again */
} snd_def;

enum {
  S_COUNT, S_GO, S_MOVE, S_SELECT, S_BACK, S_LAP, S_FINAL, S_FINISH, S_WRONG, S_OVERTAKE, S_ITEM_GET, S_TICK, S_ITEM_USE,
  S_BOOST_PAD, S_BOOST_ITEM, S_MINI1, S_MINI2, S_MINI3, S_TRICK, S_CHIME, S_LAND_SOFT, S_LAND_HARD, S_SCRAPE, S_BUMP,
  S_MISSILE_LAUNCH, S_EXPLOSION, S_MINE_ARM, S_OIL, S_SHOCK, S_SHIELD_ON, S_SHIELD_POP, S_SPIN, S_FALL, S_RESPAWN, S_DROP, S_SPLASH,
  S_ENGINE0, S_ENGINE1, S_ENGINE2, S_ENGINE3, S_ENGINE4, S_ENGINE5, S_TURBO,
  S_ROAD, S_DIRT, S_ICE, S_METAL, S_GLASS, S_WOOD, S_WIND, S_WATER, S_SLIDE, S_SKID, S_MISSILE,
  S_POP, S_KO, /* battle: a balloon popped, a kart knocked out (tools/make-battle-audio.mjs) */
  S_N
};

static const snd_def SND[S_N] = {
  [S_COUNT] = { "audio/count.ogg", 2, 40, B_UI, 20 }, [S_GO] = { "audio/go.ogg", 2, 40, B_UI, 20 },
  [S_MOVE] = { "audio/move.ogg", 2, 30, B_UI, 2 }, [S_SELECT] = { "audio/select.ogg", 2, 30, B_UI, 2 }, [S_BACK] = { "audio/back.ogg", 2, 30, B_UI, 2 },
  [S_LAP] = { "audio/lap.ogg", 2, 35, B_SFX, 30 }, [S_FINAL] = { "audio/final_lap.ogg", 1, 45, B_SFX, 60 },
  [S_FINISH] = { "audio/finish.ogg", 1, 45, B_SFX, 60 }, [S_WRONG] = { "audio/wrongway.ogg", 1, 35, B_UI, 90 },
  [S_OVERTAKE] = { "audio/overtake.ogg", 2, 25, B_SFX, 40 }, [S_ITEM_GET] = { "audio/item_get.ogg", 3, 30, B_SFX, 6 },
  [S_TICK] = { "audio/tick.ogg", 2, 15, B_SFX, 2 }, [S_ITEM_USE] = { "audio/item_use.ogg", 3, 30, B_SFX, 6 },
  [S_BOOST_PAD] = { "audio/boost_pad.ogg", 3, 25, B_SFX, 12 }, [S_BOOST_ITEM] = { "audio/boost_item.ogg", 3, 28, B_SFX, 12 },
  [S_MINI1] = { "audio/mini1.ogg", 2, 28, B_SFX, 12 }, [S_MINI2] = { "audio/mini2.ogg", 2, 28, B_SFX, 12 }, [S_MINI3] = { "audio/mini3.ogg", 2, 28, B_SFX, 12 },
  [S_TRICK] = { "audio/trick.ogg", 2, 25, B_SFX, 10 }, [S_CHIME] = { "audio/drift_chime.ogg", 2, 22, B_SFX, 8 },
  [S_LAND_SOFT] = { "audio/land_soft.ogg", 3, 20, B_SFX, 6 }, [S_LAND_HARD] = { "audio/land_hard.ogg", 3, 24, B_SFX, 6 },
  [S_SCRAPE] = { "audio/scrape.ogg", 3, 18, B_SFX, 7 }, [S_BUMP] = { "audio/bump.ogg", 3, 22, B_SFX, 5 },
  [S_MISSILE_LAUNCH] = { "audio/missile_launch.ogg", 3, 30, B_SFX, 8 }, [S_EXPLOSION] = { "audio/explosion.ogg", 3, 60, B_SFX, 8 },
  [S_MINE_ARM] = { "audio/mine_arm.ogg", 2, 20, B_SFX, 10 }, [S_OIL] = { "audio/oil.ogg", 2, 24, B_SFX, 8 },
  [S_SHOCK] = { "audio/shock.ogg", 1, 60, B_SFX, 30 }, [S_SHIELD_ON] = { "audio/shield_on.ogg", 2, 26, B_SFX, 10 },
  [S_SHIELD_POP] = { "audio/shield_pop.ogg", 2, 34, B_SFX, 8 }, [S_SPIN] = { "audio/spin.ogg", 3, 28, B_SFX, 8 },
  [S_FALL] = { "audio/fall.ogg", 2, 28, B_SFX, 20 }, [S_RESPAWN] = { "audio/respawn.ogg", 2, 26, B_SFX, 10 },
  [S_DROP] = { "audio/drop.ogg", 2, 18, B_SFX, 30 }, [S_SPLASH] = { "audio/splash.ogg", 3, 16, B_SFX, 6 },
  [S_POP] = { "audio/balloon_pop.ogg", 3, 30, B_SFX, 4 }, [S_KO] = { "audio/knockout.ogg", 1, 40, B_SFX, 30 },
  [S_ENGINE0] = { "audio/engine_0.wav", 8, 8, B_ENG, 0 }, [S_ENGINE1] = { "audio/engine_1.wav", 8, 8, B_ENG, 0 },
  [S_ENGINE2] = { "audio/engine_2.wav", 8, 8, B_ENG, 0 }, [S_ENGINE3] = { "audio/engine_3.wav", 8, 8, B_ENG, 0 },
  [S_ENGINE4] = { "audio/engine_4.wav", 8, 8, B_ENG, 0 }, [S_ENGINE5] = { "audio/engine_5.wav", 8, 8, B_ENG, 0 },
  [S_TURBO] = { "audio/turbo.wav", 4, 6, B_ENG, 0 },
  [S_ROAD] = { "audio/road_road.wav", 6, 4, B_AMB, 0 }, [S_DIRT] = { "audio/road_dirt.wav", 6, 4, B_AMB, 0 }, [S_ICE] = { "audio/road_ice.wav", 6, 4, B_AMB, 0 },
  [S_METAL] = { "audio/road_metal.wav", 6, 4, B_AMB, 0 }, [S_GLASS] = { "audio/road_glass.wav", 6, 4, B_AMB, 0 }, [S_WOOD] = { "audio/road_wood.wav", 6, 4, B_AMB, 0 },
  [S_WIND] = { "audio/wind.wav", 6, 4, B_AMB, 0 }, [S_WATER] = { "audio/water.wav", 6, 4, B_AMB, 0 }, [S_SLIDE] = { "audio/slide.wav", 12, 5, B_AMB, 0 },
  [S_SKID] = { "audio/skid.wav", 12, 5, B_AMB, 0 }, [S_MISSILE] = { "audio/missile.wav", MAX_MISSILE_VOICES, 12, B_SFX, 0 },
};

/* what each vehicle's engine file wants: loudness balance, pitch character, how much the revs show in volume */
static const struct { float gain, pitch; } ENGP[NVEH] = {
  { 0.87f, 1.00f }, /* Scrapper: balanced */
  { 0.93f, 0.92f }, /* Rhino: deep */
  { 1.05f, 1.08f }, /* Gnat: buzzy and high */
  { 0.82f, 1.00f }, /* Sidewinder: snarl */
  { 1.00f, 0.96f }, /* Longhaul: diesel */
  { 1.20f, 1.02f }, /* Pogo: sputter */
};
static const int SURF_SND[SURF_KINDS] = { [SURF_ROAD] = S_ROAD, [SURF_DIRT] = S_DIRT, [SURF_ICE] = S_ICE, [SURF_METAL] = S_METAL, [SURF_GLASS] = S_GLASS, [SURF_WOOD] = S_WOOD };
static const float SURF_GAIN[SURF_KINDS] = { [SURF_ROAD] = 0.50f, [SURF_DIRT] = 0.60f, [SURF_ICE] = 0.35f, [SURF_METAL] = 0.60f, [SURF_GLASS] = 0.40f, [SURF_WOOD] = 0.70f };

static const char *const MUSIC_FILE[10] = { "audio/music_menu.ogg", "audio/music_salt.ogg", "audio/music_drowned.ogg", "audio/music_fair.ogg",
  "audio/music_refinery.ogg", "audio/music_dam.ogg", "audio/music_frost.ogg", "audio/music_temple.ogg", "audio/music_orbit.ogg", "audio/music_results.ogg" };

/* ── state ──────────────────────────────────────────────────────────── */

/* a looping voice that starts when it is wanted, eases its level, and goes when it has faded out */
typedef struct lslot {
  cw_voice v;
  float vol;
} lslot;

typedef struct kstate {
  lslot eng, turbo, skid;
  float rpm, prev_speed, thr;
  int gear, shift;
  bool dropped;      /* the drop-away sound has played this flight */
  int rank;          /* nearest-other rank this tick (engine budget) */
  float dist;
  float slip;        /* 0..1 how hard the tyres slide */
} kstate;

static struct {
  bool on, paused, attract, humans;
  cw_bus bus[B_COUNT];
  cw_sound snd[S_N];
  cw_res res[S_N];
  float user[2];     /* options volumes: music, sfx */
  bool user_set;     /* the options screen spoke before the mixer was up */
  float duck;        /* 0..1: a big event is happening, engines and music make room */
  int tick;
  int cool[S_N];
  int nviews;
  int vk[MAXV];            /* the kart each view follows */
  lslot road[MAXV][SURF_KINDS], rough[MAXV], wind[MAXV], water[MAXV], slide[MAXV];
  kstate ks[MAXK];
  lslot bed;
  lslot missile[MAXHZ];
  int missiles;
  float tunnel, tunnel_sent;       /* 0..1 how much of the view is inside a tunnel */
  int phase_prev, last_count;
  int tick_calls;
  /* music */
  struct { cw_res res[10]; cw_sound snd[10]; cw_voice v; int track; float pitch, pitch_t, gain, gain_t; bool final; } mus;
  /* debug */
  int d_eng_voices, d_eng_mask, d_sfx_total, d_sfx_last, d_sfx_hist[S_N], d_music_track, d_music_pitch, d_music_gain, d_duck, d_tunnel, d_missiles;
  int d_own_rpm[MAXV], d_own_pitch[MAXV], d_own_vol[MAXV], d_own_skid[MAXV], d_own_wind[MAXV], d_own_road[MAXV];
  int d_paused, d_skid_voices, d_amb_voices, d_tunnel_max, d_skid_max, d_water_max, d_air_max;
  /* RACER_AUDIO_WAV=path: no device, the whole mix rendered offline to a wav (a dev aid, for measuring the mix) */
  const char *wav_path;
  float *wav;
  size_t wav_frames, wav_cap;
} A;

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static bool null_voice(cw_voice v) { return CW_HANDLE_IS_NULL(v); }

/* ── the buses ──────────────────────────────────────────────────────── */

static const float BUS_BASE[B_COUNT] = { [B_MUSIC] = 0.55f, [B_TUNNEL] = 1.0f, [B_SFX] = 0.9f, [B_ENG] = 0.14f, [B_AMB] = 0.6f, [B_UI] = 0.85f };

static void apply_volumes(void) {
  if (!A.on) return;
  float quiet = A.paused ? 0.0f : 1.0f;
  float vd = announcer_duck(), mduck = (1.0f - 0.35f * A.duck) * (1.0f - 0.6f * vd);
  cw_bus_set_volume(A.bus[B_MUSIC], BUS_BASE[B_MUSIC] * A.user[0] * mduck * (A.paused && A.mus.track > 0 && A.mus.track < 9 ? 0.6f : 1.0f));
  cw_bus_set_volume(A.bus[B_SFX], BUS_BASE[B_SFX] * A.user[1] * (1.0f - 0.25f * vd));
  cw_bus_set_volume(A.bus[B_ENG], BUS_BASE[B_ENG] * A.user[1] * (1.0f - 0.5f * A.duck) * (1.0f - 0.5f * vd) * quiet);
  cw_bus_set_volume(A.bus[B_AMB], BUS_BASE[B_AMB] * A.user[1] * (1.0f - 0.4f * A.duck) * (1.0f - 0.5f * vd) * quiet);
  cw_bus_set_volume(A.bus[B_UI], BUS_BASE[B_UI] * A.user[1]);
}

void audio_set_volume(int bus, float v) {
  if (!A.user_set) A.user[0] = A.user[1] = 1.0f;
  A.user_set = true;
  A.user[bus == ABUS_MUSIC ? 0 : 1] = clampf(v, 0, 1);
  announcer_set_volume(A.user[1]); /* the voice follows the effects volume */
  apply_volumes();
}

/* ── init ───────────────────────────────────────────────────────────── */

void audio_init(void) {
  if (!A.user_set) A.user[0] = A.user[1] = 1.0f;
  A.mus.track = -1;
  A.mus.pitch = A.mus.pitch_t = 1.0f;
  A.mus.gain = A.mus.gain_t = 1.0f;
  A.d_music_track = -1;
  A.wav_path = getenv("RACER_AUDIO_WAV");
  A.on = getenv("RACER_NO_AUDIO") == NULL && cw_audio_init(&(cw_audio_desc){ .max_voices = 64, .command_capacity = 8192, .offline = A.wav_path != NULL });
  if (!A.on) return;
  static const char *const NAMES[B_COUNT] = { "music", "tunnel", "sfx", "engines", "ambience", "ui" };
  for (int i = 0; i < B_COUNT; i++) A.bus[i] = cw_bus_create(NAMES[i], CW_AUDIO_MASTER);
  /* the tunnel's echo: a small hard room (concrete), only wet, fed by sends while a view is in a tunnel */
  cw_bus_add_fx(A.bus[B_TUNNEL], &(cw_fx_desc){ .type = CW_FX_FDN_REVERB, .fdn = { .decay_s = 1.9f, .hf_damp_hz = 4500, .predelay_s = 0.012f, .size = 0.55f, .wet = 1.0f, .dry = 0.0f } });
  /* the engines: two low-pass stages take the rasp off the top, so they sit under everything as a soft hum */
  cw_bus_add_fx(A.bus[B_ENG], &(cw_fx_desc){ .type = CW_FX_LOWPASS, .filter = { 1500, 0.6f } });
  cw_bus_add_fx(A.bus[B_ENG], &(cw_fx_desc){ .type = CW_FX_LOWPASS, .filter = { 1900, 0.6f } });
  /* the master: a gentle squeeze so a busy moment stays readable, then a hard ceiling */
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_COMPRESSOR, .compressor = { .threshold_db = -14, .ratio = 2.5f, .attack_s = 0.01f, .release_s = 0.25f, .makeup_db = 2.0f } });
  cw_bus_add_fx(CW_AUDIO_MASTER, &(cw_fx_desc){ .type = CW_FX_LIMITER, .limiter = { -1.0f, 0.12f } });
  cw_audio_set_speed_of_sound(343.0f);
  for (int i = 0; i < S_N; i++) {
    bool loop = strstr(SND[i].file, ".wav") != NULL;
    A.res[i] = cw_res_load(SND[i].file, 0);
    A.snd[i] = cw_res_audio_sound(A.res[i], &(cw_sound_desc){ .mode = CW_SOUND_SAMPLE, .max_voices = SND[i].max_voices, .priority = SND[i].priority, .bus = A.bus[SND[i].bus] });
    if (CW_HANDLE_IS_NULL(A.snd[i])) cw_warn("racer audio: %s did not load%s", SND[i].file, loop ? " (a loop)" : "");
  }
  announcer_init(CW_AUDIO_MASTER);
  announcer_set_volume(A.user[1]);
  apply_volumes();
  cw_debug_i32("snd.eng_voices", &A.d_eng_voices);
  cw_debug_i32("snd.eng_mask", &A.d_eng_mask);
  cw_debug_i32("snd.sfx_total", &A.d_sfx_total);
  cw_debug_i32("snd.sfx_last", &A.d_sfx_last);
  cw_debug_field("snd.sfx_hist", A.d_sfx_hist, CW_DBG_I32, S_N);
  cw_debug_i32("snd.music_track", &A.d_music_track);
  cw_debug_i32("snd.music_pitch_x1000", &A.d_music_pitch);
  cw_debug_i32("snd.music_gain_x1000", &A.d_music_gain);
  cw_debug_i32("snd.duck_x1000", &A.d_duck);
  cw_debug_i32("snd.tunnel_x1000", &A.d_tunnel);
  cw_debug_i32("snd.missile_voices", &A.d_missiles);
  cw_debug_i32("snd.paused", &A.d_paused);
  cw_debug_i32("snd.tunnel_max_x1000", &A.d_tunnel_max);
  cw_debug_i32("snd.skid_max_x1000", &A.d_skid_max);
  cw_debug_i32("snd.water_max_x1000", &A.d_water_max);
  cw_debug_i32("snd.air_wind_max_x1000", &A.d_air_max);
  cw_debug_i32("snd.skid_voices", &A.d_skid_voices);
  cw_debug_i32("snd.amb_voices", &A.d_amb_voices);
  cw_debug_field("snd.own_rpm_x1000", A.d_own_rpm, CW_DBG_I32, MAXV);
  cw_debug_field("snd.own_pitch_x1000", A.d_own_pitch, CW_DBG_I32, MAXV);
  cw_debug_field("snd.own_vol_x1000", A.d_own_vol, CW_DBG_I32, MAXV);
  cw_debug_field("snd.own_skid_x1000", A.d_own_skid, CW_DBG_I32, MAXV);
  cw_debug_field("snd.own_wind_x1000", A.d_own_wind, CW_DBG_I32, MAXV);
  cw_debug_field("snd.own_road_x1000", A.d_own_road, CW_DBG_I32, MAXV);
}

static void wav_write(void) {
  if (!A.wav || !A.wav_path) return;
  FILE *f = fopen(A.wav_path, "wb");
  if (!f) return;
  uint32_t n = (uint32_t)A.wav_frames, rate = (uint32_t)cw_audio_sample_rate(), data = n * 4, riff = 36 + data, br = rate * 4, fmt = 16;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (size_t i = 0; i < A.wav_frames * 2; i++) {
    float x = clampf(A.wav[i], -1, 1);
    int16_t q = (int16_t)lrintf(x * 32767.0f);
    fwrite(&q, 2, 1, f);
  }
  fclose(f);
  cw_info("racer audio: wrote %zu frames to %s", A.wav_frames, A.wav_path);
}

void audio_shutdown(void) {
  announcer_shutdown();
  wav_write();
  free(A.wav);
  A.wav = NULL;
  if (A.on) cw_audio_shutdown();
  A.on = false;
}

void audio_frame(void) {
  if (A.on && !A.wav_path) cw_audio_update();
}
/* offline: one tick's worth of the mix (1/60 s) appended to the wav */
static void wav_tick(void) {
  int frames = cw_audio_sample_rate() / TICK_HZ;
  if (A.wav_frames + (size_t)frames > A.wav_cap) {
    A.wav_cap = A.wav_cap ? A.wav_cap * 2 : (size_t)48000 * 30;
    A.wav = realloc(A.wav, A.wav_cap * 2 * sizeof(float));
  }
  cw_audio_render(A.wav + A.wav_frames * 2, frames);
  A.wav_frames += (size_t)frames;
}

/* ── voices ─────────────────────────────────────────────────────────── */

static const cw_voice_3d SPACE_ONESHOT = { .model = CW_ATTEN_INVERSE, .ref_distance = 8, .max_distance = 170, .rolloff = 1.0f, .air_absorption = 0.01f, .reverb_send = -1 };
static const cw_voice_3d SPACE_ENGINE = { .model = CW_ATTEN_INVERSE, .ref_distance = 10, .max_distance = 190, .rolloff = 1.15f, .doppler = 0.65f, .air_absorption = 0.012f, .reverb_send = -1 };

static float nearest_eye(const t3_vec3 *eye, int n, t3_vec3 p) {
  float best = 1e9f;
  for (int v = 0; v < n; v++) {
    float d = v3len(v3sub(p, eye[v]));
    if (d < best) best = d;
  }
  return best;
}
static t3_vec3 EYES[MAXV];

static int own_view(int kart) {
  for (int v = 0; v < A.nviews; v++) if (A.vk[v] == kart) return v;
  return -1;
}
/* a followed kart's sounds sit toward its view's side of the screen */
static float view_pan(int v) {
  int n = A.nviews;
  if (n < 2) return 0;
  if (n == 2) return v ? 0.25f : -0.25f;
  static const int Q3[3] = { 0, 2, 3 };
  int q = n == 3 ? Q3[v] : v;
  return (q & 1) ? 0.5f : -0.5f;
}
static float view_gain(void) { return A.nviews > 1 ? 0.8f : 1.0f; }

static bool ready(int id) { return A.on && A.cool[id] <= 0; }
static void note_played(int id) {
  A.cool[id] = SND[id].cooldown;
  A.d_sfx_total++;
  A.d_sfx_last = id;
  A.d_sfx_hist[id]++;
}
static cw_voice play2d(int id, float vol, float pitch, float pan) {
  if (!ready(id)) return (cw_voice){ 0 };
  note_played(id);
  return cw_audio_play(A.snd[id], &(cw_play_desc){ .volume = vol, .pitch = pitch, .pan = pan });
}
static cw_voice play3d(int id, t3_vec3 at, t3_vec3 vel, float vol, float pitch) {
  if (!ready(id)) return (cw_voice){ 0 };
  if (A.nviews > 0 && nearest_eye(EYES, A.nviews, at) > SPACE_ONESHOT.max_distance) return (cw_voice){ 0 };
  note_played(id);
  cw_play_desc d = { .volume = vol, .pitch = pitch, .spatial = true, .space = SPACE_ONESHOT };
  d.space.position = at;
  d.space.velocity = vel;
  return cw_audio_play(A.snd[id], &d);
}
/* a sound that belongs to a kart: centred in its view when a view follows it, out in the world otherwise */
static void kart_sfx(int id, int kart, t3_vec3 at, float vol, float pitch) {
  int v = kart >= 0 ? own_view(kart) : -1;
  if (v >= 0) play2d(id, vol * view_gain(), pitch, view_pan(v));
  else play3d(id, at, (t3_vec3){ 0, 0, 0 }, vol, pitch);
}
/* a sound for everyone's ears (a shockwave, a fanfare) */
static void global_sfx(int id, float vol, float pitch) { play2d(id, vol, pitch, 0); }

static void duck_to(float d) {
  if (d > A.duck) A.duck = d;
}
/* a big event makes room if it is on a followed kart or within earshot */
static void duck_event(t3_vec3 at, int kart, float amount) {
  if ((kart >= 0 && own_view(kart) >= 0) || (A.nviews > 0 && nearest_eye(EYES, A.nviews, at) < 45.0f)) duck_to(amount);
}

/* a looping voice that follows a level: starts at need, stops once it has faded away */
static void lslot_run(lslot *l, int id, float target, float pitch, const cw_play_desc *base, float rate) {
  l->vol += (target - l->vol) * rate;
  if (target < 0.004f && l->vol < 0.006f) {
    if (!null_voice(l->v)) cw_voice_stop(l->v, 0.05f);
    l->v = (cw_voice){ 0 };
    l->vol = 0;
    return;
  }
  if (!null_voice(l->v) && !cw_voice_is_playing(l->v)) l->v = (cw_voice){ 0 };
  if (null_voice(l->v)) {
    cw_play_desc d = *base;
    d.loop = true;
    d.volume = l->vol;
    d.pitch = pitch;
    d.fade_in = 0.05f;
    l->v = cw_audio_play(A.snd[id], &d);
    return;
  }
  cw_voice_set_volume(l->v, l->vol);
  cw_voice_set_pitch(l->v, pitch);
}
static void lslot_kill(lslot *l, float fade) {
  if (!null_voice(l->v)) cw_voice_stop(l->v, fade);
  l->v = (cw_voice){ 0 };
  l->vol = 0;
}

/* ── music ──────────────────────────────────────────────────────────── */

void audio_music(int track) {
  if (track < -1 || track > 9) track = -1;
  if (track == A.mus.track) return;
  A.mus.track = track;
  A.d_music_track = track;
  if (!A.on) return;
  if (!null_voice(A.mus.v)) cw_voice_stop(A.mus.v, 0.9f);
  A.mus.v = (cw_voice){ 0 };
  A.mus.final = false;
  A.mus.pitch = A.mus.pitch_t = 1.0f;
  if (track < 0) return;
  if (CW_HANDLE_IS_NULL(A.mus.res[track])) {
    A.mus.res[track] = cw_res_load(MUSIC_FILE[track], 0);
    A.mus.snd[track] = cw_res_audio_sound(A.mus.res[track], &(cw_sound_desc){ .mode = CW_SOUND_STREAM, .bus = A.bus[B_MUSIC] });
  }
  A.mus.v = cw_audio_play(A.mus.snd[track], &(cw_play_desc){ .loop = true, .fade_in = 0.9f, .volume = A.mus.gain, .bus = A.bus[B_MUSIC] });
}

static void music_tick(const sim *s) {
  /* quiet under the countdown, full at the lights; the final lap runs a little faster (the same file, pitched) */
  if (s->phase == PH_COUNT || s->phase == PH_GRID) A.mus.gain_t = 0.4f;
  else A.mus.gain_t = 1.0f;
  A.mus.gain = lerpf(A.mus.gain, A.mus.gain_t, 0.03f);
  A.mus.pitch = lerpf(A.mus.pitch, A.mus.pitch_t, 0.012f);
  if (!null_voice(A.mus.v)) {
    cw_voice_set_volume(A.mus.v, A.mus.gain);
    cw_voice_set_pitch(A.mus.v, A.mus.pitch);
  }
}

/* ── a race ─────────────────────────────────────────────────────────── */

static void kill_race_voices(float fade) {
  for (int i = 0; i < MAXK; i++) {
    lslot_kill(&A.ks[i].eng, fade);
    lslot_kill(&A.ks[i].turbo, fade);
    lslot_kill(&A.ks[i].skid, fade);
  }
  for (int v = 0; v < MAXV; v++) {
    for (int k = 0; k < SURF_KINDS; k++) lslot_kill(&A.road[v][k], fade);
    lslot_kill(&A.rough[v], fade);
    lslot_kill(&A.wind[v], fade);
    lslot_kill(&A.water[v], fade);
    lslot_kill(&A.slide[v], fade);
  }
  for (int i = 0; i < MAXHZ; i++) lslot_kill(&A.missile[i], fade);
  lslot_kill(&A.bed, fade);
}

void audio_race_start(const sim *s, const map_def *map) {
  (void)map;
  if (!A.on) return;
  kill_race_voices(0.1f);
  memset(A.ks, 0, sizeof A.ks);
  A.duck = 0;
  A.paused = false;
  A.tunnel = A.tunnel_sent = 0;
  A.phase_prev = -1;
  A.humans = false;
  for (int i = 0; i < MAXK; i++)
    if (s->k[i].on && s->k[i].human) A.humans = true;
  if (getenv("RACER_AUDIO_AS_HUMAN")) A.humans = true; /* dev aid: an all-CPU run sounds like a real race (music, fanfares) */
  A.attract = !A.humans;
  A.nviews = 0;
  announcer_race_start(s, A.humans);
  /* a demo race behind the menus leaves the menu music alone; a real race brings the map's own */
  if (A.humans) {
    audio_music(s->md && s->md->music ? s->md->music : s->map >= 0 && s->map < NMAPS ? s->map + 1 : 1); /* a battle arena names its own */
    A.mus.gain = 0.4f;
  }
  apply_volumes();
}

void audio_race_stop(void) {
  if (!A.on) return;
  kill_race_voices(0.25f);
  announcer_race_stop();
  A.duck = 0;
  A.paused = false;
  A.nviews = 0;
  A.mus.final = false;
  A.mus.pitch_t = 1.0f;
  apply_volumes();
}

void audio_pause(bool paused) {
  if (!A.on || paused == A.paused) return;
  A.paused = paused;
  A.d_paused = paused;
  announcer_pause(paused);
  apply_volumes();
}

/* ── the fixed tick ─────────────────────────────────────────────────── */

static float surf_pitch(float speedf) { return 0.7f + 0.7f * speedf; }

/* the engine's revs from speed: a rising whine in five gears, with a dip at each upshift (arcade: no clutch, no stall) */
static void rev_update(const sim *s, int i) {
  const kart *k = &s->k[i];
  kstate *e = &A.ks[i];
  float sp = fabsf(k->speed), top = VEH[k->veh % NVEH].top * 1.12f;
  float f = clampf(sp / top, 0, 1);
  float acc = (sp - e->prev_speed) * 60.0f;
  e->prev_speed = sp;
  float thr = acc > 0.8f ? 1.0f : acc < -2.5f ? 0.0f : 0.65f;
  if (s->phase != PH_RACE) thr = 0.1f;
  int g = (int)(f * NGEARS);
  if (g >= NGEARS) g = NGEARS - 1;
  float within = f * NGEARS - (float)g;
  if (g > e->gear) e->shift = 9;
  e->gear = g;
  if (e->shift > 0) e->shift--;
  float t = 0.25f + 0.5f * f + 0.22f * within;
  if (e->shift > 0) t -= 0.14f * (float)e->shift / 9.0f;
  if (thr < 0.2f) t -= 0.12f;
  if (sp < 6.0f && s->phase == PH_RACE) t = fmaxf(t, 0.30f + 0.25f * thr);
  /* the countdown: idle, then the driver gets on the throttle for the last second */
  if (s->phase == PH_COUNT) {
    float go = clampf((float)(s->phase_ticks - 120) / 60.0f, 0, 1);
    t = 0.26f + 0.04f * sinf((float)s->tick * 0.31f + (float)i) + 0.38f * go;
  } else if (s->phase == PH_GRID) t = 0.26f;
  if (k->air_ticks > 4 && !k->grounded && thr > 0.3f) t += 0.12f; /* wheels free in the air spin up */
  e->rpm += (t - e->rpm) * (t > e->rpm ? 0.30f : 0.12f);
  e->rpm = clampf(e->rpm, 0.0f, 1.1f);
  e->thr = thr;
}

/* engine parameters for a kart this tick */
static void engine_params(const sim *s, int i, float *vol, float *pitch) {
  const kart *k = &s->k[i];
  const kstate *e = &A.ks[i];
  int vi = k->veh % NVEH;
  float boost = k->boost_ticks > 0 ? 1.0f : 0.0f;
  /* a narrow pitch range: a soft hum that rises a little, never a whine at the top */
  *pitch = ENGP[vi].pitch * (0.62f + 0.72f * e->rpm) * (1.0f + 0.06f * boost);
  if (k->spin_ticks > 0) *pitch *= 0.85f;
  float v = 0.36f + 0.3f * e->rpm + 0.1f * e->thr;
  if (e->shift > 0) v *= 0.82f;
  v *= ENGP[vi].gain * (1.0f + 0.15f * boost);
  if (!k->grounded && k->air_ticks > 6) v *= 0.85f;
  if (k->respawn_ticks > 0) v = 0;
  if (k->finished) v *= 0.55f;
  *vol = fminf(v, 0.95f);
}

static float slip_of(const sim *s, int i) {
  const kart *k = &s->k[i];
  if (!k->grounded) return 0;
  float sl = 0;
  const cw_vehicle3d *car = sim_car(s, i);
  if (car) {
    int n = car->def.wheel_count;
    for (int w = 0; w < n && w < CW_VEHICLE3D_WHEELS; w++) {
      const cw_wheel3d *wh = &car->wheel[w];
      if (!wh->contact) continue;
      float a = fabsf(wh->slip_side);
      if (wh->sliding) a += 1.5f;
      if (a > sl) sl = a;
    }
  }
  float amt = clampf((sl - 1.5f) / 6.0f, 0, 1);
  if (k->drifting) amt = fmaxf(amt, 0.5f + 0.15f * (float)k->drift_level);
  if (k->spin_ticks > 0) amt = fmaxf(amt, 0.8f);
  return clampf(amt, 0, 1);
}

static void set_listeners(const sim *s, const t3_vec3 *eye, int neyes) {
  for (int v = 0; v < neyes; v++) {
    int kn = game_view_kart(v);
    if (kn < 0 || kn >= MAXK || !s->k[kn].on) kn = -1;
    A.vk[v] = kn;
    t3_vec3 f = (t3_vec3){ 0, 0, 1 }, up = (t3_vec3){ 0, 1, 0 }, vel = (t3_vec3){ 0, 0, 0 };
    if (kn >= 0) {
      const kart *k = &s->k[kn];
      f = kart_fwd(k);
      vel = k->v;
      /* the listener keeps the world's up: a kart in a loop turns upside down, the picture and the ears stay level */
      f.y *= 0.3f;
      f = v3norm(f);
    }
    cw_audio_set_listener_at(v, eye[v], vel, f, up);
  }
  cw_audio_set_listener_count(neyes > 0 ? neyes : 1);
}

void audio_tick(const sim *s, const t3_vec3 *eye, int neyes) {
  if (!A.on) return;
  A.tick++;
  if (A.paused) audio_pause(false);
  if (neyes > MAXV) neyes = MAXV;
  A.nviews = neyes;
  for (int v = 0; v < neyes; v++) EYES[v] = eye[v];
  for (int i = 0; i < S_N; i++) if (A.cool[i] > 0) A.cool[i]--;
  set_listeners(s, eye, neyes);
  A.duck = fmaxf(0, A.duck - 0.022f);

  /* phase changes: the results theme once the race is over */
  if (s->phase != A.phase_prev) {
    if (s->phase == PH_DONE && A.humans) audio_music(9);
    A.phase_prev = s->phase;
  }
  music_tick(s);

  /* revs for every kart */
  for (int i = 0; i < MAXK; i++) if (s->k[i].on) rev_update(s, i);

  /* ── the followed karts: engine, tyres, surface, wind ── */
  float tunnel_in = 0;
  A.d_amb_voices = 0;
  bool owned[MAXK] = { false };
  for (int v = 0; v < neyes; v++) {
    int i = A.vk[v];
    if (i < 0) continue;
    const kart *k = &s->k[i];
    kstate *e = &A.ks[i];
    owned[i] = true;
    float pan = view_pan(v), g = view_gain() * (A.attract ? 0.55f : 1.0f);
    tsample ts = track_at_loc(s->track, &k->loc, 0);
    bool in_tunnel = (ts.flags & TF_TUNNEL) && k->loc.on_road;
    if (in_tunnel) tunnel_in += 1.0f / (float)neyes;
    float tun = 1.0f + 0.25f * A.tunnel;
    float speedf = clampf(fabsf(k->speed) / VEH[k->veh % NVEH].top, 0, 1.4f);

    /* engine: a centred 2D voice */
    float ev, ep;
    engine_params(s, i, &ev, &ep);
    int vi = k->veh % NVEH;
    cw_play_desc d2 = { .pan = pan, .bus = A.bus[B_ENG] };
    lslot_run(&e->eng, S_ENGINE0 + vi, ev * g * tun, ep, &d2, 0.35f);
    if (vi == 4) { /* Longhaul's turbo whistle spools with the revs and the throttle */
      float tv = powf(clampf(e->rpm, 0, 1), 3.0f) * (0.2f + 0.8f * e->thr) * 0.4f * g;
      lslot_run(&e->turbo, S_TURBO, k->respawn_ticks > 0 ? 0 : tv, 0.55f + 0.9f * e->rpm, &d2, 0.2f);
    }
    A.d_own_rpm[v] = (int)(e->rpm * 1000);
    A.d_own_pitch[v] = (int)(ep * 1000);
    A.d_own_vol[v] = (int)(ev * 1000);

    /* tyres on the surface */
    int surf = ts.surf < SURF_KINDS ? ts.surf : SURF_ROAD;
    bool ground = k->grounded && k->loc.on_road && k->respawn_ticks <= 0;
    float roadg = ground ? clampf(speedf, 0, 1.2f) : 0;
    cw_play_desc da = { .pan = pan, .bus = A.bus[B_AMB] };
    for (int su = 0; su < SURF_KINDS; su++)
      lslot_run(&A.road[v][su], SURF_SND[su], su == surf ? SURF_GAIN[su] * powf(roadg, 0.8f) * g : 0, surf_pitch(speedf), &da, 0.12f);
    lslot_run(&A.rough[v], S_DIRT, (ts.flags & TF_ROUGH) && surf != SURF_DIRT ? 0.4f * roadg * g : 0, surf_pitch(speedf), &da, 0.12f);
    A.d_own_road[v] = (int)(SURF_GAIN[surf] * powf(roadg, 0.8f) * 1000);
    bool in_water = ground && (ts.flags & TF_WATER);
    lslot_run(&A.water[v], S_WATER, in_water ? (0.35f + 0.55f * clampf(speedf, 0, 1)) * g : 0, 0.8f + 0.5f * speedf, &da, 0.15f);

    /* slides: squeal on hard ground, a rough hiss on dirt and ice */
    e->slip = lerpf(e->slip, slip_of(s, i), 0.25f);
    float sl = e->slip;
    bool loose = surf == SURF_DIRT || surf == SURF_ICE;
    lslot_run(&e->skid, S_SKID, !loose && sl > 0.05f ? powf(sl, 1.2f) * 0.75f * g : 0, 0.85f + 0.3f * sl + 0.15f * speedf, &da, 0.3f);
    lslot_run(&A.slide[v], S_SLIDE, loose && sl > 0.05f ? sl * 0.7f * g : 0, 0.8f + 0.4f * speedf, &da, 0.25f);
    A.d_own_skid[v] = (int)(sl * 1000);
    if (A.d_own_skid[v] > A.d_skid_max) A.d_skid_max = A.d_own_skid[v];
    if (in_water && (int)(A.water[v].vol * 1000) > A.d_water_max) A.d_water_max = (int)(A.water[v].vol * 1000);

    /* wind: more with speed, a lot more in the air and under a boost */
    float air = (!k->grounded && k->air_ticks > 3) ? 1.0f : 0.0f;
    float wv = 0.04f + 0.4f * speedf * speedf + 0.28f * air * clampf(speedf + 0.2f, 0, 1) + (k->boost_ticks > 0 ? 0.12f : 0);
    lslot_run(&A.wind[v], S_WIND, wv * g, 0.6f + 0.7f * speedf + 0.2f * air, &da, 0.1f);
    A.d_own_wind[v] = (int)(wv * 1000);
    if (air > 0 && (int)(wv * 1000) > A.d_air_max) A.d_air_max = (int)(wv * 1000);

    /* the world dropping away: once per flight, when it has been in the air long enough to matter */
    if (!k->grounded && k->air_ticks == 22 && !e->dropped) {
      e->dropped = true;
      play2d(S_DROP, 0.7f * view_gain(), 1.0f, pan);
    }
    if (k->grounded) e->dropped = false;

    /* the item roulette ticks, rising as it settles */
    if (k->item_wait > 0 && k->item_wait % 5 == 0 && !A.attract) play2d(S_TICK, 0.55f * view_gain(), 0.85f + 0.5f * (1.0f - clampf((float)k->item_wait / 90.0f, 0, 1)), pan);
  }
  for (int v = neyes; v < MAXV; v++) {
    for (int su = 0; su < SURF_KINDS; su++) lslot_kill(&A.road[v][su], 0.1f);
    lslot_kill(&A.rough[v], 0.1f);
    lslot_kill(&A.wind[v], 0.1f);
    lslot_kill(&A.water[v], 0.1f);
    lslot_kill(&A.slide[v], 0.1f);
  }
  A.tunnel = lerpf(A.tunnel, tunnel_in, 0.08f);
  announcer_tick(s);

  /* ── the other karts: the nearest few get a 3D engine, the rest a distant bed ── */
  int order[MAXK], nord = 0;
  for (int i = 0; i < MAXK; i++) {
    kstate *e = &A.ks[i];
    e->rank = 99;
    if (!s->k[i].on || owned[i]) continue;
    e->dist = neyes > 0 ? nearest_eye(EYES, neyes, s->k[i].p) : 1e9f;
    int j = nord++;
    while (j > 0 && A.ks[order[j - 1]].dist > e->dist) { order[j] = order[j - 1]; j--; }
    order[j] = i;
  }
  int unvoiced = 0;
  A.d_eng_voices = 0;
  A.d_eng_mask = 0;
  A.d_skid_voices = 0;
  for (int r = 0; r < nord; r++) {
    int i = order[r];
    kstate *e = &A.ks[i];
    e->rank = r;
    const kart *k = &s->k[i];
    bool want = e->dist < ENG_MAX_DIST && r < ENG_OTHERS;
    if (!want) {
      lslot_kill(&e->eng, 0.2f);
      lslot_kill(&e->turbo, 0.2f);
      lslot_kill(&e->skid, 0.15f);
      if (k->respawn_ticks <= 0) unvoiced++;
      continue;
    }
    float ev, ep;
    engine_params(s, i, &ev, &ep);
    int vi = k->veh % NVEH;
    cw_play_desc d3 = { .bus = A.bus[B_ENG], .spatial = true, .space = SPACE_ENGINE };
    d3.space.position = k->p;
    d3.space.velocity = k->v;
    lslot_run(&e->eng, S_ENGINE0 + vi, ev * (A.attract ? 0.8f : 1.0f), ep, &d3, 0.35f);
    if (!null_voice(e->eng.v)) cw_voice_set_position(e->eng.v, k->p, k->v);
    if (vi == 4) {
      float tv = powf(clampf(e->rpm, 0, 1), 3.0f) * (0.2f + 0.8f * e->thr) * 0.35f;
      lslot_run(&e->turbo, S_TURBO, k->respawn_ticks > 0 ? 0 : tv, 0.55f + 0.9f * e->rpm, &d3, 0.2f);
      if (!null_voice(e->turbo.v)) cw_voice_set_position(e->turbo.v, k->p, k->v);
    }
    /* squeal from the other karts too, but only the clear cases */
    e->slip = lerpf(e->slip, slip_of(s, i), 0.25f);
    cw_play_desc ds = { .bus = A.bus[B_AMB], .spatial = true, .space = SPACE_ENGINE };
    ds.space.position = k->p;
    ds.space.velocity = k->v;
    lslot_run(&e->skid, S_SKID, e->slip > 0.4f ? e->slip * 0.6f : 0, 0.85f + 0.3f * e->slip, &ds, 0.3f);
    if (!null_voice(e->skid.v)) { cw_voice_set_position(e->skid.v, k->p, k->v); A.d_skid_voices++; }
    A.d_eng_voices++;
    A.d_eng_mask |= 1 << i;
  }
  for (int i = 0; i < MAXK; i++) {
    if (!owned[i]) continue;
    if (!null_voice(A.ks[i].eng.v)) { A.d_eng_voices++; A.d_eng_mask |= 1 << i; }
    if (!null_voice(A.ks[i].skid.v)) A.d_skid_voices++;
  }
  /* karts out of the budget add up to one far-off hum */
  {
    cw_play_desc db = { .bus = A.bus[B_ENG], .pan = 0 };
    lslot_run(&A.bed, S_ENGINE1, unvoiced > 0 ? fminf(0.04f * (float)unvoiced, 0.16f) : 0, 0.62f, &db, 0.05f);
  }

  /* ── missiles in flight: a whine with doppler ── */
  A.missiles = 0;
  for (int h = 0; h < MAXHZ; h++) {
    const hazard *z = &s->hz[h];
    bool fly = (z->kind == HZ_MISSILE || z->kind == HZ_HOMING);
    if (fly && A.missiles < MAX_MISSILE_VOICES && (neyes == 0 || nearest_eye(EYES, neyes, z->p) < 150.0f)) {
      A.missiles++;
      cw_play_desc dm = { .bus = A.bus[B_SFX], .spatial = true, .space = SPACE_ENGINE };
      dm.space.doppler = 1.0f;
      dm.space.position = z->p;
      dm.space.velocity = v3mul(z->f, z->speed);
      dm.space.ref_distance = 6;
      lslot_run(&A.missile[h], S_MISSILE, 0.55f, 0.95f + 0.1f * clampf(z->speed / 80.0f, 0, 1), &dm, 0.4f);
      if (!null_voice(A.missile[h].v)) cw_voice_set_position(A.missile[h].v, z->p, v3mul(z->f, z->speed));
    } else lslot_kill(&A.missile[h], 0.05f);
  }
  A.d_missiles = A.missiles;

  /* ── the tunnel's echo: the buses send into it in step with how much of the view is inside ── */
  if (fabsf(A.tunnel - A.tunnel_sent) > 0.02f || (A.tunnel < 0.01f && A.tunnel_sent > 0)) {
    float t = A.tunnel < 0.01f ? 0 : A.tunnel;
    A.tunnel_sent = t;
    cw_bus_set_send(A.bus[B_ENG], 0, A.bus[B_TUNNEL], 0.5f * t);
    cw_bus_set_send(A.bus[B_AMB], 0, A.bus[B_TUNNEL], 0.7f * t);
    cw_bus_set_send(A.bus[B_SFX], 0, A.bus[B_TUNNEL], 0.45f * t);
  }

  apply_volumes();
  if (A.wav_path) wav_tick();
  else cw_audio_update();
  A.d_music_pitch = (int)(A.mus.pitch * 1000);
  A.d_music_gain = (int)(A.mus.gain * 1000);
  A.d_duck = (int)(A.duck * 1000);
  A.d_tunnel = (int)(A.tunnel * 1000);
  if (A.d_tunnel > A.d_tunnel_max) A.d_tunnel_max = A.d_tunnel;
}

/* ── events ─────────────────────────────────────────────────────────── */

void audio_events(const sim *s) {
  if (!A.on) return;
  announcer_events(s);
  for (int n = 0; n < s->ev_n; n++) {
    const struct sim_event *e = &s->ev[n];
    const kart *ka = (e->a >= 0 && e->a < MAXK) ? &s->k[e->a] : NULL;
    int ov = e->a >= 0 && e->a < MAXK ? own_view(e->a) : -1;
    switch (e->kind) {
    case EV_COUNT:
      if (A.attract) break;
      global_sfx(S_COUNT, 0.8f, 1.0f);
      duck_to(0.3f);
      break;
    case EV_GO:
      if (!A.attract) global_sfx(S_GO, 0.9f, 1.0f);
      break;
    case EV_LAP:
      if (ov >= 0 && !A.attract) {
        if (e->b >= s->laps) {
          global_sfx(S_FINAL, 1.0f, 1.0f);
          A.mus.final = true;
          A.mus.pitch_t = 1.07f;
        } else global_sfx(S_LAP, 0.8f, 1.0f);
      }
      break;
    case EV_FINISH:
      if (ov >= 0 && !A.attract) {
        global_sfx(S_FINISH, e->b == 1 ? 1.0f : 0.8f, e->b == 1 ? 1.0f : 0.92f);
        duck_to(0.4f);
      }
      break;
    case EV_BOOST: {
      int src = e->b;
      if (src == 0) kart_sfx(S_BOOST_PAD, e->a, e->at, 0.7f, 0.95f + 0.1f * e->k);
      else if (src == 1) kart_sfx(S_BOOST_ITEM, e->a, e->at, 0.9f, 1.0f);
      else if (src == 2) {
        float ratio = ka ? e->k / VEH[ka->veh % NVEH].boost : 1.0f;
        int lv = ratio < 1.09f ? 1 : ratio < 1.29f ? 2 : 3;
        kart_sfx(S_MINI1 + lv - 1, e->a, e->at, 0.8f + 0.05f * (float)lv, 1.0f);
      } else if (src == 3) kart_sfx(S_BOOST_ITEM, e->a, e->at, 0.9f, 0.85f);
      else kart_sfx(S_BOOST_PAD, e->a, e->at, 0.55f, 1.2f);
      break;
    }
    case EV_DRIFT_LEVEL:
      if (ov >= 0 && !A.attract) play2d(S_CHIME, 0.55f * view_gain(), e->b == 1 ? 1.0f : e->b == 2 ? 1.26f : 1.5f, view_pan(ov));
      break;
    case EV_LAND: {
      float hard = clampf(e->k, 0, 1);
      if (hard < 0.12f) break;
      if (hard < 0.5f) kart_sfx(S_LAND_SOFT, e->a, e->at, 0.5f + hard, 1.1f - 0.3f * hard);
      else kart_sfx(S_LAND_HARD, e->a, e->at, 0.6f + 0.4f * hard, 1.15f - 0.35f * hard);
      break;
    }
    case EV_TRICK: kart_sfx(S_TRICK, e->a, e->at, 0.75f, 1.0f); break;
    case EV_BUMP: {
      bool own = own_view(e->a) >= 0 || own_view(e->b) >= 0;
      if (own) play2d(S_BUMP, (0.45f + 0.55f * e->k) * view_gain(), 0.9f + 0.3f * (1.0f - e->k), 0);
      else play3d(S_BUMP, e->at, (t3_vec3){ 0, 0, 0 }, 0.5f + 0.7f * e->k, 0.9f + 0.3f * (1.0f - e->k));
      break;
    }
    case EV_WALL: kart_sfx(S_SCRAPE, e->a, e->at, 0.35f + 0.65f * e->k, 0.85f + 0.3f * e->k); break;
    case EV_BOX: kart_sfx(S_ITEM_GET, e->a, e->at, 0.8f, 1.0f); break;
    case EV_ITEM_GET:
      if (ov >= 0 && !A.attract) play2d(S_CHIME, 0.7f * view_gain(), 1.5f, view_pan(ov));
      break;
    case EV_ITEM_USE:
      switch (e->b) {
      case IT_MISSILE:
      case IT_HOMING: kart_sfx(S_MISSILE_LAUNCH, e->a, e->at, 0.95f, e->b == IT_HOMING ? 1.12f : 1.0f); break;
      case IT_MINE: kart_sfx(S_MINE_ARM, e->a, e->at, 0.8f, 1.0f); break;
      case IT_OIL: kart_sfx(S_ITEM_USE, e->a, e->at, 0.7f, 0.8f); break;
      case IT_SHIELD: kart_sfx(S_SHIELD_ON, e->a, e->at, 0.8f, 1.0f); break;
      case IT_SHOCK: global_sfx(S_SHOCK, 1.0f, 1.0f); duck_to(0.8f); break;
      default: kart_sfx(S_ITEM_USE, e->a, e->at, 0.7f, 1.0f); break;
      }
      break;
    case EV_HIT: {
      int hz = (int)(e->k + 0.5f);
      if (hz == HZ_MISSILE || hz == HZ_HOMING || hz == HZ_MINE) {
        kart_sfx(S_EXPLOSION, e->a, e->at, own_view(e->a) >= 0 ? 1.0f : 1.2f, hz == HZ_MINE ? 0.9f : 1.0f);
        duck_event(e->at, e->a, 0.75f);
      } else if (hz == HZ_OIL) kart_sfx(S_OIL, e->a, e->at, 0.9f, 1.0f);
      break;
    }
    case EV_SPIN:
      if (e->k != (float)SPIN_BOOM) kart_sfx(S_SPIN, e->a, e->at, 0.6f, 1.0f);
      break;
    case EV_FALL: kart_sfx(S_FALL, e->a, e->at, 0.8f, 1.0f); break;
    case EV_RESPAWN: kart_sfx(S_RESPAWN, e->a, e->at, 0.7f, 1.0f); break;
    case EV_NEARMISS: kart_sfx(S_BOOST_PAD, e->a, e->at, 0.45f, 1.45f); break;
    case EV_WARN:
      if (ov >= 0 && !A.attract) play2d(S_SHOCK, 0.5f * view_gain(), 1.6f, view_pan(ov)), play2d(S_CHIME, 0.7f * view_gain(), 1.5f, view_pan(ov));
      break;
    case EV_SHIELD: kart_sfx(S_SHIELD_POP, e->a, e->at, 0.9f, 1.0f); duck_event(e->at, e->a, 0.25f); break;
    case EV_POP: kart_sfx(S_POP, e->a, e->at, 1.0f, 0.95f + 0.05f * e->k); break;
    case EV_OUT: global_sfx(S_KO, 0.9f, 1.0f); break;
    case EV_OVERTAKE:
      if (ov >= 0 && !A.attract) play2d(S_OVERTAKE, 0.6f * view_gain(), 1.0f, view_pan(ov));
      break;
    case EV_WATER:
      kart_sfx(S_SPLASH, e->a, e->at, 0.3f + 0.6f * clampf(e->k, 0, 1), 0.9f + 0.3f * (1.0f - clampf(e->k, 0, 1)));
      break;
    default: break;
    }
  }
}

/* ── menu sounds ────────────────────────────────────────────────────── */

void audio_ui(int sfx) {
  if (!A.on) return;
  /* a menu sound while a race is loaded but not stepping means it is paused (or over): hush the race's loops */
  if (game_loaded() && !ui_race_runs()) audio_pause(true);
  switch (sfx) {
  case AUI_MOVE: play2d(S_MOVE, 0.7f, 1.0f, 0); break;
  case AUI_SELECT: play2d(S_SELECT, 0.85f, 1.0f, 0); break;
  case AUI_BACK: play2d(S_BACK, 0.8f, 1.0f, 0); break;
  case AUI_COUNT: global_sfx(S_COUNT, 0.8f, 1.0f); break;
  case AUI_GO: global_sfx(S_GO, 0.9f, 1.0f); break;
  case AUI_WRONG_WAY: play2d(S_WRONG, 0.8f, 1.0f, 0), announcer_wrong_way(); break;
  case AUI_FINAL_LAP: global_sfx(S_FINAL, 1.0f, 1.0f); break;
  default: break;
  }
}
