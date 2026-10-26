/* racer's announcer: a voice that calls the race (the countdown, the laps, the player's place, the hits, the big
 * moments, the finish, a record, a cup), from the lines tools/make-voice.mjs made (voice_lines.h).
 *
 * It only reads the sim (its events and the karts) on the fixed tick, like the rest of audio.c, which drives it.
 *
 * How it speaks:
 *   one voice at a time  every call goes into a small queue with a priority and a shelf life; the best fresh call
 *                        speaks once the voice is free (and a short breath after the last one), a stale call is dropped
 *   interrupts           the countdown cuts anything off; an alarm (a Shockwave coming, the wrong way) cuts off
 *                        anything below it
 *   cooldowns            each line has its own (a lead change is news, the tenth "moving up" is not); a big moment
 *                        waits 2 s after the last line started and the lesser calls 3 s (they go stale meanwhile),
 *                        so a busy patch does not turn into a stream of talk
 *   colour               when it has been quiet a while (16 to 26 s) it fills in with a line of colour; the map's
 *                        welcome comes a moment after the lights
 *   variants             a line with several takes rotates them in a shuffled order, never the same twice running
 *   ducking              while it speaks the music, engines and ambience make room (announcer_duck)
 * Split screen: the race-wide calls (the countdown, the laps of the first human to get there, the welcome, colour)
 * speak once; the player's own calls (place, hits, stunts, the finish) follow the best-placed human still racing,
 * which is also the one a "lead" call is about. An alarm (a Shockwave coming) is said for any human.
 * A battle (no laps, places or welcome) gets the countdown, the hits and pops, the alarms and colour.
 * Off in a demo race (no humans), and when the player turns it off (Options: ANNOUNCER).
 *
 * Dev aids: RACER_VOICE_LOG=file (or 1: the engine log) writes every line said, dropped or cut, with the time;
 * RACER_VOICE=0 keeps it off whatever the save says; RACER_VOICE=solo mutes everything else and =mute only the voice
 * (the race still ducks under it): with RACER_AUDIO_WAV, the voice alone and the ducked race without it, rendered
 * offline, to measure one against the other. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/audio.h"
#include "cartwheel/debug.h"
#include "cartwheel/resource.h"
#include "ui.h"
#include "voice_lines.h"
#include "world.h"

/* priorities: higher speaks first; P_MUST cuts anything off, P_ALERT cuts off what is below P_BIG */
enum { P_CHATTER = 10, P_WELCOME = 20, P_INFO = 30, P_POS = 40, P_BIG = 50, P_ALERT = 60, P_FINAL = 70, P_FINISH = 80, P_MUST = 100 };
#define QN 6
#define BIG_AIR_M 6.0f                 /* metres over the road at the top of a flight that make it big air */
#define VOICE_GAIN 1.6f               /* the voice bus, set by measuring it (RACER_VOICE=solo) over the ducked race (=mute) */
#define GAP_MINOR (3 * TICK_HZ)        /* a call below P_BIG waits this long after the last line started */
#define GAP_BIG (2 * TICK_HZ)          /* and a big moment this long (an alarm or the countdown never waits) */
#define QUIET_MIN 16                   /* colour after 16 to 26 s of quiet */
#define QUIET_RANGE 11
#define BREATH (TICK_HZ / 4)           /* between two lines */

typedef struct vreq {
  int line, prio, born, ttl;
} vreq;

/* each line's priority, shelf life and cooldown (ticks) */
typedef struct vrule {
  int prio, ttl, cool;
} vrule;
static vrule RULE[VL_N];

static struct {
  bool init, user_on, user_set, forced_off, muted, live, humans, trial;
  cw_bus bus;
  cw_res res[VL_N][VL_MAX_VARIANTS];
  cw_sound snd[VL_N][VL_MAX_VARIANTS];
  float vol;
  int clock;                           /* ticks since init (the queue's time) */
  vreq q[QN];
  int qn;
  cw_voice v;
  int v_line, v_prio, v_start;
  int words_end, last_start;           /* the clock when the last line's words end / when it started */
  int cool[VL_N];
  uint8_t order[VL_N][VL_MAX_VARIANTS];
  int pos[VL_N], last_var[VL_N];
  uint32_t rng;
  float duck;
  bool paused;
  /* race state */
  int welcome_at, quiet_target;
  int best_place, best_seen, best_hold; /* the best human place: announced, seen, ticks it has held */
  int racing_n;                        /* humans still racing */
  bool laps_said[MAX_LAPS + 2], finish_said, flood_checked;
  bool air_said[MAXK];
  int focus;                           /* the human whose own calls are spoken (-1: none) */
  FILE *log;
  bool log_engine;
  /* debug */
  int d_said, d_dropped, d_cut, d_last, d_on, d_queue;
} N;

static uint32_t rnd(void) {
  N.rng ^= N.rng << 13, N.rng ^= N.rng >> 17, N.rng ^= N.rng << 5;
  return N.rng;
}

static void logf_(const char *what, int line, int var, const char *extra) {
  if (!N.log && !N.log_engine) return;
  const sim *s = game_loaded() ? game_sim() : NULL;
  char b[256];
  snprintf(b, sizeof b, "%8.2fs  race %7.2fs  %-5s %-16s %s%s%s", (double)N.clock / TICK_HZ, s && s->phase >= PH_RACE ? (double)s->race_ticks / TICK_HZ : 0.0,
           what, VOICE_LINES[line].id, var >= 0 ? "\"" : "", var >= 0 ? VOICE_LINES[line].text[var] : "", var >= 0 ? "\"" : "");
  if (N.log) fprintf(N.log, "%s%s%s\n", b, extra ? "  " : "", extra ? extra : ""), fflush(N.log);
  else cw_info("voice: %s%s%s", b, extra ? "  " : "", extra ? extra : "");
}

static void rule(int line, int prio, float ttl_s, float cool_s) { RULE[line] = (vrule){ prio, (int)(ttl_s * TICK_HZ), (int)(cool_s * TICK_HZ) }; }

void announcer_init(int parent) {
  N.bus = cw_bus_create("voice", parent);
  N.vol = 1.0f;
  if (!N.user_set) N.user_on = true; /* unless the save spoke first */
  N.rng = 0x9e3779b9u;
  N.focus = -1;
  for (int i = 0; i < VL_N; i++) rule(i, P_INFO, 1.5f, 8);
  rule(VL_THREE, P_MUST, 0.4f, 0), rule(VL_TWO, P_MUST, 0.4f, 0), rule(VL_ONE, P_MUST, 0.4f, 0), rule(VL_GO, P_MUST, 0.4f, 0);
  rule(VL_ROCKET_START, P_BIG, 2.5f, 0), rule(VL_FLOODED, P_BIG, 2.5f, 0);
  rule(VL_LAP2, P_INFO, 2, 0), rule(VL_LAP3, P_INFO, 2, 0), rule(VL_LAP4, P_INFO, 2, 0), rule(VL_FINAL_LAP, P_FINAL, 3, 0);
  rule(VL_WRONG_WAY, P_ALERT, 1, 8);
  rule(VL_LEAD, P_BIG, 3, 6), rule(VL_LOST_LEAD, P_BIG, 2, 10);
  rule(VL_SECOND, P_POS, 1.2f, 12), rule(VL_THIRD, P_POS, 1.2f, 12), rule(VL_MOVING_UP, P_POS, 1.2f, 20), rule(VL_DROPPING, P_POS, 1.2f, 25);
  rule(VL_SHOCK_WARN, P_ALERT, 0.6f, 4), rule(VL_SHOCK_DODGED, P_BIG, 1.2f, 15);
  rule(VL_DIRECT_HIT, P_BIG, 1.2f, 12), rule(VL_GOT_HIT, P_BIG, 1.0f, 15);
  rule(VL_SHIELD_UP, P_INFO, 1.0f, 18), rule(VL_SHIELD_SAVED, P_BIG, 1.0f, 8);
  rule(VL_HUGE_AIR, P_BIG, 0.8f, 15), rule(VL_PERFECT_LANDING, P_POS, 1.0f, 30), rule(VL_DRIFT_MAX, P_POS, 1.0f, 25), rule(VL_SLIPSTREAM, P_INFO, 1.0f, 30);
  rule(VL_FALL, P_BIG, 1.2f, 12);
  rule(VL_WINNER, P_FINISH, 3, 0), rule(VL_WHAT_A_RACE, P_FINISH - 1, 4, 0), rule(VL_SO_CLOSE, P_FINISH, 3, 0), rule(VL_PODIUM, P_FINISH, 3, 0), rule(VL_FINISHED, P_FINISH, 3, 0);
  for (int i = VL_NEW_RECORD; i <= VL_CUP_DONE; i++) rule(i, P_FINISH, 5, 0);
  for (int i = VL_MEDAL_BRONZE; i <= VL_MEDAL_AUTHOR; i++) rule(i, P_FINISH - 1, 5, 0); /* "New record!", then the medal */
  for (int i = VL_MAP_SALT; i <= VL_MAP_ORBIT; i++) rule(i, P_WELCOME, 8, 0);
  rule(VL_CHATTER, P_CHATTER, 1, 20);
  rule(VL_CLOSE_CALL, P_POS, 1.0f, 20); /* a pack fight shaves past often: one in twenty seconds is plenty */
  rule(VL_POP, P_BIG, 1.2f, 5), rule(VL_LAST_BALLOON, P_ALERT, 1.5f, 0), rule(VL_KNOCKED_OUT, P_FINAL, 2, 0);
  rule(VL_RIVAL_OUT, P_POS, 1.5f, 8), rule(VL_BATTLE_WIN, P_FINISH, 4, 0), rule(VL_BATTLE_OVER, P_FINISH, 4, 0);
  for (int i = 0; i < VL_N; i++) {
    const voice_line *l = &VOICE_LINES[i];
    for (int v = 0; v < l->n; v++) {
      /* the countdown is decoded up front (it has to land on the beat); everything else streams from its Ogg (small in memory) */
      bool sample = i == VL_THREE || i == VL_TWO || i == VL_ONE || i == VL_GO;
      N.res[i][v] = cw_res_load(l->file[v], 0);
      N.snd[i][v] = cw_res_audio_sound(N.res[i][v], &(cw_sound_desc){ .mode = sample ? CW_SOUND_SAMPLE : CW_SOUND_STREAM, .max_voices = 1, .priority = 90, .bus = N.bus });
      if (CW_HANDLE_IS_NULL(N.snd[i][v])) cw_warn("racer announcer: %s did not load", l->file[v]);
      N.order[i][v] = (uint8_t)v;
    }
    N.last_var[i] = -1;
  }
  const char *dv = getenv("RACER_VOICE");
  if (dv && !strcmp(dv, "0")) N.forced_off = true, N.user_on = false;
  if (dv && !strcmp(dv, "solo")) cw_bus_set_solo(N.bus, true);
  if (dv && !strcmp(dv, "mute")) N.muted = true;
  const char *lg = getenv("RACER_VOICE_LOG");
  if (lg && *lg) {
    if (!strcmp(lg, "1")) N.log_engine = true;
    else N.log = fopen(lg, "w");
  }
  cw_debug_i32("voice.said", &N.d_said);
  cw_debug_i32("voice.dropped", &N.d_dropped);
  cw_debug_i32("voice.cut", &N.d_cut);
  cw_debug_i32("voice.last", &N.d_last);
  cw_debug_i32("voice.on", &N.d_on);
  cw_debug_i32("voice.queue", &N.d_queue);
  N.d_last = -1;
  N.d_on = N.user_on;
  N.init = true;
  announcer_set_volume(N.vol);
}

void announcer_shutdown(void) {
  if (N.log) fclose(N.log);
  N.log = NULL;
}

void announcer_set_volume(float v) {
  N.vol = v;
  if (N.init) cw_bus_set_volume(N.bus, VOICE_GAIN * v * (N.user_on && !N.muted ? 1.0f : 0.0f));
}

void audio_set_announcer(bool on) {
  if (N.forced_off) on = false;
  N.user_on = on;
  N.user_set = true;
  N.d_on = on;
  if (!on && N.init && !CW_HANDLE_IS_NULL(N.v)) cw_voice_stop(N.v, 0.1f), N.v = (cw_voice){ 0 };
  if (!on) N.qn = 0;
  announcer_set_volume(N.vol);
}
bool audio_announcer(void) { return N.user_on; }

/* ── the queue ──────────────────────────────────────────────────────── */

static void drop_at(int i) {
  N.q[i] = N.q[--N.qn];
}

/* ask for a line; false when it is cooling down or the voice is off */
static bool say(int line) {
  if (!N.init || !N.user_on || line < 0 || line >= VL_N) return false;
  if (N.cool[line] > N.clock) return false;
  const vrule *r = &RULE[line];
  for (int i = 0; i < N.qn; i++)
    if (N.q[i].line == line) return false; /* already asked */
  if (N.qn == QN) {
    /* full: the least of them makes room, if it is less than this one */
    int lo = 0;
    for (int i = 1; i < N.qn; i++)
      if (N.q[i].prio < N.q[lo].prio) lo = i;
    if (N.q[lo].prio >= r->prio) return false;
    logf_("DROP", N.q[lo].line, -1, "(the queue was full)");
    N.d_dropped++;
    drop_at(lo);
  }
  N.q[N.qn++] = (vreq){ line, r->prio, N.clock, r->ttl };
  N.cool[line] = N.clock + r->cool; /* asked counts: an event that repeats while waiting does not pile up */
  return true;
}

static int pick_variant(int line) {
  const voice_line *l = &VOICE_LINES[line];
  if (l->n <= 1) return 0;
  if (N.pos[line] == 0) {
    /* a fresh shuffle, whose first is never the take just heard */
    for (int i = l->n - 1; i > 0; i--) {
      int j = (int)(rnd() % (uint32_t)(i + 1));
      uint8_t t = N.order[line][i];
      N.order[line][i] = N.order[line][j], N.order[line][j] = t;
    }
    if (N.order[line][0] == N.last_var[line]) {
      uint8_t t = N.order[line][0];
      N.order[line][0] = N.order[line][1], N.order[line][1] = t;
    }
  }
  int v = N.order[line][N.pos[line]];
  N.pos[line] = (N.pos[line] + 1) % l->n;
  N.last_var[line] = v;
  return v;
}

/* the words are still coming (the stadium's ring after them does not hold the next line back) */
static bool speaking(void) { return !CW_HANDLE_IS_NULL(N.v) && N.clock < N.words_end && cw_voice_is_playing(N.v); }

static void speak(int qi) {
  vreq r = N.q[qi];
  drop_at(qi);
  int var = pick_variant(r.line);
  if (!CW_HANDLE_IS_NULL(N.v)) cw_voice_stop(N.v, 0.12f); /* the last line's ring makes way */
  N.v = cw_audio_play(N.snd[r.line][var], &(cw_play_desc){ .volume = 1.0f, .bus = N.bus, .priority = 90 });
  if (CW_HANDLE_IS_NULL(N.v)) {
    logf_("FAIL", r.line, var, "(no voice)");
    return;
  }
  N.v_line = r.line, N.v_prio = r.prio, N.v_start = N.clock;
  N.words_end = N.clock + (VOICE_LINES[r.line].ms[var] * TICK_HZ + 999) / 1000;
  N.last_start = N.clock;
  N.d_said++;
  N.d_last = r.line;
  char ex[48];
  snprintf(ex, sizeof ex, "(prio %d, waited %.2fs)", r.prio, (double)(N.clock - r.born) / TICK_HZ);
  logf_("SAY", r.line, var, ex);
}

static void pump(void) {
  /* stale calls go */
  for (int i = 0; i < N.qn;)
    if (N.clock - N.q[i].born > N.q[i].ttl) {
      logf_("STALE", N.q[i].line, -1, NULL);
      N.d_dropped++;
      drop_at(i);
    } else i++;
  if (N.qn == 0 || N.paused) return;
  int best = 0;
  for (int i = 1; i < N.qn; i++)
    if (N.q[i].prio > N.q[best].prio || (N.q[i].prio == N.q[best].prio && N.q[i].born < N.q[best].born)) best = i;
  const vreq *b = &N.q[best];
  if (speaking()) {
    /* the countdown cuts anything (its own last beat too); an alarm cuts what is below the big moments */
    bool cut = b->prio >= P_MUST || (b->prio >= P_ALERT && N.v_prio < P_BIG && b->prio > N.v_prio);
    if (!cut) return;
    logf_("CUT", N.v_line, -1, NULL);
    N.d_cut++;
    cw_voice_stop(N.v, 0.05f);
    N.v = (cw_voice){ 0 };
  } else {
    int ended = N.words_end < N.clock ? N.words_end : N.clock;
    if (b->prio < P_MUST && N.clock - ended < BREATH) return;
    if (b->prio < P_BIG && N.clock - N.last_start < GAP_MINOR) return;
    if (b->prio < P_ALERT && N.v_prio < P_MUST && N.clock - N.last_start < GAP_BIG) return; /* "Go!" is followed at once */
  }
  speak(best);
}

/* ── the race ───────────────────────────────────────────────────────── */

void announcer_race_start(const sim *s, bool humans) {
  if (!N.init) return;
  if (speaking()) logf_("CUT", N.v_line, -1, "(the next race)"), N.d_cut++;
  if (!CW_HANDLE_IS_NULL(N.v)) cw_voice_stop(N.v, 0.1f), N.v = (cw_voice){ 0 };
  N.words_end = N.clock;
  N.qn = 0;
  N.humans = humans;
  N.live = humans; /* a demo race behind the menus is not called */
  N.trial = game_is_trial();
  /* the welcome: a moment after the lights, and only for a race from the grid (not a time trial's quick retry) */
  N.welcome_at = s->phase == PH_GRID && s->phase_ticks <= 1 && !s->battle && s->map >= 0 && s->map < NMAPS ? (int)(2.4f * TICK_HZ) : -1;
  N.quiet_target = (QUIET_MIN + (int)(rnd() % QUIET_RANGE)) * TICK_HZ;
  N.best_place = N.best_seen = 0;
  N.best_hold = 0;
  N.racing_n = -1;
  memset(N.laps_said, 0, sizeof N.laps_said);
  memset(N.air_said, 0, sizeof N.air_said);
  N.finish_said = N.flood_checked = false;
  N.focus = -1;
  N.rng ^= s->rng * 2654435761u;
  if (!N.rng) N.rng = 1;
  N.words_end = N.last_start = N.clock - 10 * TICK_HZ;
  memset(N.cool, 0, sizeof N.cool);
}

void announcer_race_stop(void) {
  if (!N.init) return;
  N.live = false;
  /* the results lines (a record, a cup) are asked after the race stops stepping: keep the queue, stop the race's own calls */
  for (int i = 0; i < N.qn;)
    if (N.q[i].prio < P_FINISH) drop_at(i);
    else i++;
}

void announcer_pause(bool paused) {
  if (!N.init || paused == N.paused) return;
  N.paused = paused;
  if (!CW_HANDLE_IS_NULL(N.v)) cw_voice_set_paused(N.v, paused);
}

static bool is_human(const sim *s, int k) { return k >= 0 && k < MAXK && s->k[k].on && s->k[k].human; }
/* a player's own call is spoken for the focus human only (in one player, that player) */
static bool mine(const sim *s, int k) { return is_human(s, k) && k == N.focus; }

static int place_line(int from, int to) {
  if (to == 1) return VL_LEAD;
  if (from == 1) return VL_LOST_LEAD;
  if (to < from) return to == 2 ? VL_SECOND : to == 3 ? VL_THIRD : VL_MOVING_UP;
  return VL_DROPPING;
}

void announcer_events(const sim *s) {
  if (!N.init || !N.live || !N.user_on) return;
  for (int n = 0; n < s->ev_n; n++) {
    const struct sim_event *e = &s->ev[n];
    switch (e->kind) {
    case EV_COUNT: say(e->a == 3 ? VL_THREE : e->a == 2 ? VL_TWO : VL_ONE); break;
    case EV_GO: {
      say(VL_GO);
      /* a flooded start for the focus human (the grid order decides it in split screen) */
      int f = -1;
      for (int i = 0; i < MAXK && f < 0; i++)
        if (is_human(s, s->order[i])) f = s->order[i];
      if (f >= 0 && s->k[f].launch == -1) say(VL_FLOODED);
      break;
    }
    case EV_LAP:
      if (is_human(s, e->a) && e->b >= 2 && e->b <= MAX_LAPS && !N.laps_said[e->b]) {
        N.laps_said[e->b] = true;
        if (e->b >= s->laps) say(VL_FINAL_LAP);
        else say(e->b == 2 ? VL_LAP2 : e->b == 3 ? VL_LAP3 : VL_LAP4);
      }
      break;
    case EV_FINISH: {
      if (s->battle) { /* the round is over: a = the winner */
        if (!N.finish_said) say(is_human(s, e->a) ? VL_BATTLE_WIN : VL_BATTLE_OVER);
        N.finish_said = true;
        break;
      }
      if (!is_human(s, e->a) || N.finish_said || N.trial) break;
      N.finish_said = true;
      int place = e->b;
      const kart *k = &s->k[e->a];
      if (place == 1) {
        say(VL_WINNER);
        /* close behind: the next kart within a second and a half of the line */
        int nx = s->nkarts > 1 ? s->order[1] : -1;
        if (nx >= 0 && nx < MAXK && s->k[nx].on) {
          float gap_m = (float)s->laps * s->track->length - s->k[nx].prog, sp = s->k[nx].speed > 5 ? s->k[nx].speed : 5;
          if (gap_m / sp < 1.5f) say(VL_WHAT_A_RACE);
        }
      } else if (place == 2) {
        int w = s->order[0];
        say(s->k[w].finished && k->finish_tick - s->k[w].finish_tick < 90 ? VL_SO_CLOSE : VL_PODIUM);
      } else if (place == 3) say(VL_PODIUM);
      else say(VL_FINISHED);
      break;
    }
    case EV_BOOST:
      if (!mine(s, e->a)) break;
      if (e->b == 3) say(VL_ROCKET_START);
      else if (e->b == 2 && e->k / VEH[s->k[e->a].veh % NVEH].boost >= 1.29f) say(VL_DRIFT_MAX);
      else if (e->b == 5) say(VL_SLIPSTREAM);
      else if (e->b == 4) {
        /* a trick's landing, unless this boost is a Shockwave hopped (its EV_SHIELD came just before) */
        bool dodge = n > 0 && s->ev[n - 1].kind == EV_SHIELD && s->ev[n - 1].a == e->a;
        if (dodge) say(VL_SHOCK_DODGED);
        else if (rnd() % 3 == 0) say(VL_PERFECT_LANDING);
      }
      break;
    case EV_SHIELD: {
      bool dodge = n + 1 < s->ev_n && s->ev[n + 1].kind == EV_BOOST && s->ev[n + 1].a == e->a && s->ev[n + 1].b == 4;
      if (mine(s, e->a) && !dodge) say(VL_SHIELD_SAVED);
      break;
    }
    case EV_ITEM_USE:
      if (mine(s, e->a) && e->b == IT_SHIELD) say(VL_SHIELD_UP);
      break;
    case EV_WARN:
      if (is_human(s, e->a)) say(VL_SHOCK_WARN);
      break;
    case EV_HIT: {
      if (s->battle) break; /* a battle calls the pop that follows */
      int hz = (int)(e->k + 0.5f);
      if (hz != HZ_MISSILE && hz != HZ_HOMING && hz != HZ_MINE) break;
      if (mine(s, e->b) && e->a != e->b) say(VL_DIRECT_HIT);
      else if (mine(s, e->a)) say(VL_GOT_HIT);
      break;
    }
    case EV_FALL:
      if (mine(s, e->a)) say(VL_FALL);
      break;
    case EV_NEARMISS: /* a close pass by the player */
      if (mine(s, e->a)) say(VL_CLOSE_CALL);
      break;
    case EV_POP: /* battle: a balloon popped (k: the balloons it has left) */
      if (e->b >= 0 && e->b != e->a && mine(s, e->b)) say(VL_POP);
      else if (mine(s, e->a)) say(e->k > 0.5f && e->k < 1.5f ? VL_LAST_BALLOON : e->k >= 1.5f ? VL_GOT_HIT : -1);
      break;
    case EV_OUT: /* battle: out of the round. Any human's is news (it ends that player's round); a rival's is colour */
      if (is_human(s, e->a)) say(VL_KNOCKED_OUT);
      else say(VL_RIVAL_OUT);
      break;
    default: break;
    }
  }
}

void announcer_wrong_way(void) {
  if (N.live) say(VL_WRONG_WAY);
}

void audio_announce(int what) {
  switch (what) {
  case ANN_RECORD: say(VL_NEW_RECORD); break;
  case ANN_MEDAL_BRONZE: say(VL_MEDAL_BRONZE); break;
  case ANN_MEDAL_SILVER: say(VL_MEDAL_SILVER); break;
  case ANN_MEDAL_GOLD: say(VL_MEDAL_GOLD); break;
  case ANN_MEDAL_AUTHOR: say(VL_MEDAL_AUTHOR); break;
  case ANN_CUP_WIN: say(VL_CUP_WINNER); break;
  case ANN_CUP_TROPHY: say(VL_CUP_TROPHY); break;
  case ANN_CUP_DONE: say(VL_CUP_DONE); break;
  default: break;
  }
}

/* the fixed tick: the race's standing calls (place, air, the welcome, colour), then the queue */
void announcer_tick(const sim *s) {
  if (!N.init) return;
  N.clock++;
  if (N.live && N.user_on && s) {
    /* the focus: the best-placed human still racing (in a battle: still in the round) */
    N.focus = -1;
    for (int i = 0; i < s->nkarts && i < MAXK && N.focus < 0; i++) {
      int k = s->order[i];
      if (is_human(s, k) && !s->k[k].finished && !s->k[k].out) N.focus = k;
    }
    if (s->phase == PH_RACE && s->battle) {
      /* a battle has no laps, places or jumps to call: the hits (EV_POP), the alarms and colour */
      if (N.focus >= 0 && s->race_ticks > 8 * TICK_HZ && N.qn == 0 && !speaking() && N.clock - N.last_start > N.quiet_target) {
        if (say(VL_CHATTER)) N.quiet_target = (QUIET_MIN + (int)(rnd() % QUIET_RANGE)) * TICK_HZ;
      }
    } else if (s->phase == PH_RACE) {
      /* the best human place, once it has held for most of a second (a pass and a pass back is not news) */
      int best = 0, racing_n = 0;
      for (int i = 0; i < MAXK; i++)
        if (is_human(s, i) && !s->k[i].finished && (racing_n++, best == 0 || s->k[i].place < best)) best = s->k[i].place;
      /* a human crossing the line changes whose place this is: start over from the new one, nothing to say */
      if (racing_n != N.racing_n) N.racing_n = racing_n, N.best_place = 0;
      if (best > 0) {
        if (best != N.best_seen) N.best_seen = best, N.best_hold = 0;
        else if (N.best_hold < 1000) N.best_hold++;
        if (N.best_place == 0 && s->race_ticks > 2) N.best_place = best; /* the grid place: nothing to say */
        if (N.best_place > 0 && best != N.best_place && N.best_hold == 45 && !N.trial && s->nkarts > 1) {
          /* the lead won and lost inside a few seconds is one story, not two */
          int pl = place_line(N.best_place, best);
          if (say(pl) && (pl == VL_LEAD || pl == VL_LOST_LEAD)) {
            int other = pl == VL_LEAD ? VL_LOST_LEAD : VL_LEAD, until = N.clock + 6 * TICK_HZ;
            if (N.cool[other] < until) N.cool[other] = until;
          }
          N.best_place = best;
        }
      }
      /* big air: once a flight, for the focus human, more than BIG_AIR_M over the road (a fall drops below it) */
      if (N.focus >= 0) {
        const kart *k = &s->k[N.focus];
        if (k->grounded) N.air_said[N.focus] = false;
        else if (!N.air_said[N.focus] && k->air_peak > BIG_AIR_M && k->respawn_ticks <= 0) {
          N.air_said[N.focus] = true;
          say(VL_HUGE_AIR);
        }
      }
      /* the welcome, then colour when it has been quiet a while */
      if (N.welcome_at >= 0 && s->race_ticks == N.welcome_at) {
        say(VL_MAP_SALT + s->map);
        N.welcome_at = -1;
      }
      bool racing = N.focus >= 0;
      /* not in the run to the line: the finish is about to speak */
      if (racing && s->k[N.focus].prog < (float)s->laps * s->track->length - 300.0f && s->race_ticks > 8 * TICK_HZ && N.qn == 0 && !speaking() && N.clock - N.last_start > N.quiet_target) {
        if (say(VL_CHATTER)) N.quiet_target = (QUIET_MIN + (int)(rnd() % QUIET_RANGE)) * TICK_HZ;
      }
    }
  }
  pump();
  bool talk = speaking();
  N.duck += ((talk ? 1.0f : 0.0f) - N.duck) * (talk ? 0.25f : 0.06f);
  N.d_queue = N.qn;
}

float announcer_duck(void) { return N.init && N.user_on ? N.duck : 0.0f; }
