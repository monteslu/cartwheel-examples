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

/* study silk: the web and the line on the first spout, scripted, without drawing.
 *   web      climbs to 1 m above the first bracket, spins a web there ({brace} + {silk}), climbs on to 4.8 m above the
 *            bracket and waits there, never bracing, until a wave takes it: it must stop at the web, the web spent.
 *            Its control climbs the same way with no web: the wave takes it down to the bracket.
 *   line     at 5 m it lets a line down ({down} + {silk}), lets itself down 2 m, swings round the pipe a little, and
 *            reels home ({silk}): back on the paper at the anchor, no silk spent, in under 2.5 s from the drop. */
static void climb_to(sim *S, float y, int *t) {
  for (; *t < 60 * 120 && S->s.y < y; (*t)++) {
    sim_input in = { .y = 1 };
    sim_step(S, &in);
  }
}
void study_silk(study_silk_out *o) {
  static sim S;
  const level_def *L = &LEVELS[0];
  for (int web = 0; web < 2; web++) {
    sim_start(&S, L, 0x51ed0u);
    int t = 0;
    float at = L->checks[1] + 1.0f;
    climb_to(&S, at, &t);
    float web_y = -1;
    if (web) {
      sim_input in = { .brace = true, .silk = true };
      sim_step(&S, &in), t++;
      web_y = S.net_y;
    }
    climb_to(&S, L->checks[1] + 4.8f, &t);
    int washes = S.washes;
    float before = 0;
    for (; t < 60 * 180 && S.washes == washes && S.s.state != SP_WIN; t++) {
      before = S.s.y;
      sim_input in = { 0 };
      sim_step(&S, &in);
    }
    for (int k = 0; k < 120 && S.s.state == SP_WASHED; k++, t++) {
      sim_input in = { 0 };
      sim_step(&S, &in);
    }
    study_web *w = web ? &o->web : &o->none;
    *w = (study_web){ web_y, before, S.s.y, S.washes > washes, S.net_y >= 0, S.nets_caught, S.silk };
  }
  sim_start(&S, L, 0x51ed0u);
  int t = 0;
  climb_to(&S, 5.0f, &t);
  int silk0 = S.silk, t0 = t;
  float anchor = S.s.y, th0 = S.s.th;
  sim_input drop = { .y = -1, .silk = true };
  sim_step(&S, &drop), t++;
  o->line.started = S.s.state == SP_RAPPEL;
  for (int k = 0; k < 55; k++, t++) {
    sim_input in = { .y = -1, .x = 0.4f };
    sim_step(&S, &in);
  }
  o->line.lowest = S.s.y;
  sim_input home = { .silk = true };
  sim_step(&S, &home), t++;
  for (int k = 0; k < 240 && S.s.state != SP_CLING; k++, t++) {
    sim_input in = { 0 };
    sim_step(&S, &in);
  }
  o->line.anchor = anchor;
  o->line.back = S.s.y;
  o->line.swung = fabsf(wrap_pi(S.s.th - th0));
  o->line.ticks = t - t0;
  o->line.silk_spent = silk0 - S.silk;
  o->line.cling = S.s.state == SP_CLING;
}

/* study hands: the first spout climbed by the climber that braces for the waves it sees (the hazard study's tell),
 * once on the stick and twice hand over hand: a steady rhythm (a pull every 12 ticks, five a second) and a ragged one
 * (every 20 ticks, one press in four with the same hand again), and a control that presses the same hand every time
 * and never hops. Three seeds each; ticks to the top (-1: not in 3 minutes) */
void study_hands(int out[4][3]) {
  static sim S;
  for (int mode = 0; mode < 4; mode++)
    for (int seed = 0; seed < 3; seed++) {
      sim_start(&S, &LEVELS[0], 0x51ed0u + (uint32_t)seed * 977u);
      S.hands = mode > 0;
      uint32_t r = 0x2545f491u + (uint32_t)seed;
      float best = 0;
      int still = 0, t = 0, hand = 2, every = mode == 2 ? 20 : 12;
      for (; t < 60 * 180 && S.s.state != SP_WIN; t++) {
        sim_input in = { 0 };
        in.y = mode ? 0 : 1;
        if (wave_coming(&S)) in.brace = true;
        else if (S.bird_ticks > 0 && S.bird_ticks < BIRD_WARN && fabsf(wrap_pi(S.s.th - S.bird_th)) < 0.9f)
          in.x = wrap_pi(S.s.th - S.bird_th) >= 0 ? 1.0f : -1.0f, in.y = mode ? 0 : 0.2f;
        else if (mode && t % every == 0) {
          r ^= r << 13, r ^= r >> 17, r ^= r << 5;
          bool same = (mode == 2 && (r & 3) == 0) || mode == 3;
          if (!same) hand = 3 - hand;
          in.hand_l = hand == 1, in.hand_r = hand == 2;
        }
        if (S.s.y > best + 0.05f) best = S.s.y, still = 0;
        else if (++still > 60 && !in.brace && mode != 3) in.jump = true, still = 0; /* (the control never hops: hops climb too) */
        sim_step(&S, &in);
      }
      out[mode][seed] = S.s.state == SP_WIN ? t : -1;
    }
}
