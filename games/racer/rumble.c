/* Pad rumble for the human karts: a function of the sim's events and state (render side only, nothing feeds back).
 * It needs the engine's cw_input_rumble (cartwheel/input.h, CW_HAVE_INPUT_RUMBLE); against an older SDK it compiles to
 * nothing. Each human's pad is the one their player is assigned, so in split screen only the player who was hit feels it. */
#include <math.h>

#include "cartwheel/input.h"
#include "ui.h"
#include "world.h"

#ifdef CW_HAVE_INPUT_RUMBLE
static bool ENABLED = true;
static int sustain_t[MAXV]; /* ticks between re-issues of the held rumbles (boost, drift, rough road) */

void rumble_set_enabled(bool on) {
  if (ENABLED && !on)
    for (int v = 0; v < game_views(); v++) cw_input_rumble_stop(game_view_player(v));
  ENABLED = on;
}
bool rumble_enabled(void) { return ENABLED; }

static int view_of_kart(int kart) {
  for (int v = 0; v < game_views(); v++)
    if (game_view_kart(v) == kart) return v;
  return -1;
}
static void buzz(int v, float low, float high, int ms) {
  if (ENABLED && v >= 0) cw_input_rumble(game_view_player(v), low, high, ms);
}

void rumble_events(const sim *s) {
  if (!ENABLED || s->phase == PH_DONE) return;
  for (int i = 0; i < s->ev_n; i++) {
    const struct sim_event *e = &s->ev[i];
    int v = view_of_kart(e->a);
    if (v < 0) continue;
    switch (e->kind) {
    case EV_LAND:
      if (e->k > 0.3f) buzz(v, 0.25f + 0.6f * e->k, 0.1f + 0.2f * e->k, 90 + (int)(110 * e->k));
      break;
    case EV_BUMP: buzz(v, 0.35f + 0.3f * fminf(e->k, 1.0f), 0.3f, 110); break;
    case EV_WALL: buzz(v, 0.3f + 0.4f * fminf(e->k, 1.0f), 0.15f, 100); break;
    case EV_SPIN: buzz(v, 0.9f, 0.6f, 380); break;
    case EV_FALL: buzz(v, 0.7f, 0.2f, 260); break;
    case EV_TRICK: buzz(v, 0.1f, 0.35f, 70); break;
    case EV_ITEM_GET: buzz(v, 0.0f, 0.3f, 60); break;
    case EV_DRIFT_LEVEL: buzz(v, 0.05f, 0.25f + 0.1f * (float)(e->b & 3), 70); break;
    case EV_SHIELD: buzz(v, 0.5f, 0.6f, 160); break;
    case EV_WARN: buzz(v, 0.2f, 0.9f, 260); break;
    case EV_BOOST: buzz(v, 0.45f, 0.25f, 150); break;
    default: break;
    }
  }
}

/* every tick: the rumbles that last while something goes on (a boost, a drift, a rough road), re-issued in short pieces so
 * a pad that is unplugged or a race that ends never leaves a motor running */
void rumble_tick(const sim *s) {
  if (!ENABLED || s->phase != PH_RACE) return;
  for (int v = 0; v < game_views() && v < MAXV; v++) {
    if (--sustain_t[v] > 0) continue;
    sustain_t[v] = 6;
    const kart *k = &s->k[game_view_kart(v)];
    if (!k->on || k->finished || k->spin_ticks > 0) continue;
    float low = 0, high = 0;
    if (k->boost_ticks > 0) low = 0.18f, high = 0.28f;
    if (k->drifting) low = fmaxf(low, 0.08f + 0.04f * (float)k->drift_level), high = fmaxf(high, 0.12f + 0.06f * (float)k->drift_level);
    if (k->grounded && fabsf(k->speed) > 20 && ((s->track->s[k->loc.i].flags & TF_ROUGH) || s->track->s[k->loc.i].surf == SURF_DIRT)) low = fmaxf(low, 0.12f), high = fmaxf(high, 0.1f);
    if (low > 0 || high > 0) buzz(v, low, high, 110);
  }
}
#else
void rumble_set_enabled(bool on) { (void)on; }
bool rumble_enabled(void) { return false; }
void rumble_events(const sim *s) { (void)s; }
void rumble_tick(const sim *s) { (void)s; }
#endif
