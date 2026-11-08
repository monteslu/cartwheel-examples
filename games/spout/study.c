/* spout's study: climbs run without drawing, for the gates (the debug command `study hazards <level>`). Each is the
 * game's own simulation, deterministic like a climb.
 *
 * Three climbers, three seeds each:
 *   hold   holds up the whole way and hops every four seconds (the climb that once won the first spout untouched)
 *   tell   climbs, and braces when a wave it can see is coming down a stream it is in, within 1.6 m above it;
 *          steps round when the bird's mark is near; hops over a band it cannot climb
 *   none   holds up and never hops (a reference: it shows the slick bands still need a hop or a walk round) */
#include <math.h>

#include "study.h"

static bool wave_coming(const sim *S) {
  for (int i = 0; i < 8; i++) {
    const wave *w = &S->waves[i];
    if (!w->live) continue;
    float d = fabsf(wrap_pi(S->s.th - S->stream_th[w->stream]));
    if (d < 0.6f && w->y > S->s.y - 0.1f && w->y - S->s.y < 1.6f) return true;
  }
  return false;
}

void study_hazards(int level, study_climb out[3][3]) {
  static sim S;
  for (int bot = 0; bot < 3; bot++)
    for (int seed = 0; seed < 3; seed++) {
      sim_start(&S, &LEVELS[level], 0x51ed0u + (uint32_t)level + (uint32_t)seed * 977u);
      float best = 0;
      int still = 0, t = 0;
      for (; t < 60 * 180 && S.s.state != SP_WIN; t++) {
        sim_input in = { 0 };
        in.y = 1;
        if (bot == 0 && t % 240 == 120) in.jump = true;
        if (bot == 1) {
          if (wave_coming(&S)) in.brace = true;
          else if (S.bird_ticks > 0 && S.bird_ticks < BIRD_WARN && fabsf(wrap_pi(S.s.th - S.bird_th)) < 0.9f)
            in.x = wrap_pi(S.s.th - S.bird_th) >= 0 ? 1.0f : -1.0f, in.y = 0.2f;
          if (S.s.y > best + 0.05f) best = S.s.y, still = 0;
          else if (++still > 60 && !in.brace) in.jump = true, still = 0;
        }
        sim_step(&S, &in);
      }
      out[bot][seed] = (study_climb){ S.s.state == SP_WIN ? t : -1, S.washes, S.knocks, S.pecks, S.braced_waves };
    }
}
