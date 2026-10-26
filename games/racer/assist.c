/* racer's driving assists for the humans (never the CPU drivers). They live in the human input path (main.c
 * human_in) and cost no speed: they only change what the player's own input asks for.
 *
 *   steering assist  a gentle pull back towards the middle of the road when the kart is heading for a wall or the
 *                    edge: from the sideways speed and the room left it works out when the edge would be reached, and
 *                    from a little under a second out it adds a steering correction that grows as the edge nears. A
 *                    player who already steers away gets nothing; a drift, a jump, a loop and a spin-out are left alone.
 *   auto-accelerate  the throttle is held unless the brake is pressed (main.c); this file has no part in it.
 *
 * Pure arithmetic on the sim's state (no libm transcendentals), so a recorded input replays the same everywhere. */
#include <math.h>

#include "game.h"

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

void assist_steer(const sim *s, int me, kin *in) {
  const kart *k = &s->k[me];
  if (s->phase != PH_RACE || k->finished || !k->grounded || k->spin_ticks > 0 || k->respawn_ticks > 0 || k->drifting) return;
  const track *t = s->track;
  const tsample *ts = &t->s[k->loc.i];
  if (ts->flags & (TF_LOOP | TF_GAP | TF_RAMP)) return;
  tsample m = track_at(t, k->loc.s);
  float vf = v3dot(k->v, m.f), vl = v3dot(k->v, m.r); /* along the road, and towards its right edge */
  if (vf < 4.0f) return;
  const float margin = 1.5f, horizon = 0.85f;
  float room_r = m.hw - margin - k->loc.lat, room_l = m.hw - margin + k->loc.lat;
  float push = 0; /* + steers right, - left */
  if (vl > 0.4f) {
    float tt = room_r > 0 ? room_r / vl : 0;
    if (tt < horizon) push -= 1.0f - tt / horizon;
  } else if (vl < -0.4f) {
    float tt = room_l > 0 ? room_l / -vl : 0;
    if (tt < horizon) push += 1.0f - tt / horizon;
  }
  /* well outside the middle third and still moving outward: a light extra pull */
  float lat_frac = k->loc.lat / (m.hw > 1 ? m.hw : 1);
  if (lat_frac > 0.7f && vl > 0) push -= 0.25f * (lat_frac - 0.7f) / 0.3f;
  if (lat_frac < -0.7f && vl < 0) push += 0.25f * (-lat_frac - 0.7f) / 0.3f;
  push = clampf(push, -0.7f, 0.7f);
  float want = (float)in->steer / 127.0f;
  /* a player already steering the right way keeps their own input */
  if ((push > 0 && want >= push) || (push < 0 && want <= push)) return;
  in->steer = (int8_t)(clampf(want + push, -1.0f, 1.0f) * 127.0f);
}
