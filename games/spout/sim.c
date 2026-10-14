/* spout simulation: the spider on the pipe, the weather, the water and the
 * leaves. Fixed 60 Hz ticks and cartwheel/math.h only, so every host replays
 * the same climb from the same input (the gates compare sim_hash).
 *
 * The spider lives on the pipe's surface in (angle, height). It climbs
 * anywhere it can grip; paper wraps round the pipe turn slick while the
 * paper is wet (it slides down them unless braced); a jump leaves the
 * surface in an arc, which is how slick bands are crossed. Rain sends waves
 * down a few streams; a wave that catches an unbraced spider washes it down
 * to the last bracket it reached. The sun dries the paper again. */
#include <string.h>

#include "game.h"

#define DT (1.0f / 60)
#define CLIMB 1.15f       /* m/s along the surface */
#define SLICK_SLIDE 1.7f  /* m/s down a wet wrap */
#define WASH_SPEED 5.5f   /* m/s while washed down */
#define WAVE_SPEED 6.5f   /* m/s down a stream */
#define LEAF_SPEED 2.2f
#define STREAM_HALF 0.42f /* half-width of a stream (radians) */
#define CLEAR_TICKS 90

float wrap_pi(float a) { return cw_wrap_angle(a); }

static uint32_t rnd(sim *S) {
  uint32_t x = S->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return S->rng = x;
}
static float rnd01(sim *S) { return (float)(rnd(S) >> 8) / 16777216.0f; }

void sim_start(sim *S, const level_def *L, uint32_t seed) {
  memset(S, 0, sizeof *S);
  S->L = L;
  S->rng = seed ? seed : 0x9e3779b9u;
  S->s.th = 0;
  S->s.y = 0.05f;
  S->s.state = SP_CLING;
  S->weather = WX_SUN;
  for (int k = 0; k < L->n_streams; k++) S->stream_th[k] = L->streams[k];
  S->silk = 3;
  /* flies round the back of the pipe (worth the detour), spools up high */
  static const float FTH[MAX_FLIES] = { 2.5f, -2.2f, 1.8f }, FY[MAX_FLIES] = { 0.3f, 0.6f, 0.86f };
  for (int i = 0; i < MAX_FLIES; i++) S->fly_th[i] = FTH[i], S->fly_y[i] = L->height * FY[i];
  for (int i = 0; i < MAX_SPOOLS; i++) S->spool_th[i] = i ? -1.9f : 1.9f, S->spool_y[i] = L->checks[i + 1 < L->n_checks ? i + 1 : 0] + 1.6f;
  S->bird_ticks = 0;
}

float sim_silk_reach(const sim *S) { (void)S; return 3.2f; }

/* the next collar (every 3 m, every 2 m on bamboo) or bracket above, in reach */
bool sim_anchor_above(const sim *S, float *out) {
  const level_def *L = S->L;
  float y = S->s.y, step = L->twist == TWIST_NODES ? 2.0f : 3.0f, best = 1e9f;
  float c = (floorf(y / step) + 1) * step;
  if (c - y < 0.4f) c += step;
  if (c < L->height) best = c;
  for (int i = 0; i < L->n_checks; i++)
    if (L->checks[i] > y + 0.4f && L->checks[i] < best) best = L->checks[i];
  if (L->height - y < best - y && L->height > y + 0.4f) best = L->height; /* the gutter */
  if (best - y > sim_silk_reach(S)) return false;
  *out = best;
  return true;
}

bool sim_on_patch(const sim *S, float th, float y, int *which) {
  const level_def *L = S->L;
  for (int i = 0; i < L->n_patches; i++) {
    const patch *p = &L->patches[i];
    if (y < p->y0 || y > p->y1) continue;
    float mid = (p->th0 + p->th1) * 0.5f, half = (p->th1 - p->th0) * 0.5f;
    if (fabsf(wrap_pi(th - mid)) <= half) {
      if (which) *which = i;
      return true;
    }
  }
  return false;
}

static bool slick_here(const sim *S) {
  int i;
  if (!sim_on_patch(S, S->s.th, S->s.y, &i)) return false;
  return !S->L->patches[i].dries || S->wet > 0.5f;
}

static void weather_step(sim *S) {
  const level_def *L = S->L;
  S->weather_ticks++;
  switch (S->weather) {
  case WX_SUN:
    /* winter: the soaked wraps freeze and the sun takes much longer */
    S->wet = cw_clampf(S->wet - DT / (L->twist == TWIST_FREEZE ? 9.0f : 3.0f), 0, 1);
    if (S->weather_ticks >= L->sun_ticks) S->weather = WX_WARN, S->weather_ticks = 0;
    break;
  case WX_WARN:
    if (S->weather_ticks >= L->warn_ticks) S->weather = WX_RAIN, S->weather_ticks = 0, S->ev_rain_start = 1;
    break;
  case WX_RAIN:
    S->wet = cw_clampf(S->wet + DT / 1.2f, 0, 1);
    if (L->wave_every > 0 && S->weather_ticks % L->wave_every == L->wave_every / 2 && L->n_streams > 0) {
      int k = (int)(rnd(S) % (uint32_t)L->n_streams);
      for (int i = 0; i < 8; i++)
        if (!S->waves[i].live) {
          S->waves[i] = (wave){ .y = L->height + 0.5f, .live = true, .stream = k };
          S->ev_wave = 1;
          break;
        }
    }
    if (S->weather_ticks >= L->rain_ticks) S->weather = WX_CLEAR, S->weather_ticks = 0;
    break;
  case WX_CLEAR:
    if (S->weather_ticks >= CLEAR_TICKS) S->weather = WX_SUN, S->weather_ticks = 0, S->ev_sun_start = 1;
    break;
  }
  /* the streams wander a little round the pipe */
  for (int k = 0; k < L->n_streams; k++) S->stream_th[k] = L->streams[k] + 0.22f * cw_sin(S->tick * 0.011f + k * 2.1f);
}

static void wash_down(sim *S, float metres, bool wave);

/* the paper bird: every so often it lands its mark near you (a little above,
 * a little round), the mark shows, then it pecks there; be elsewhere */
static void bird_step(sim *S) {
  spider *s = &S->s;
  if (S->tick < 400) return;
  if (S->bird_ticks == 0) {
    if (S->tick % BIRD_EVERY != 0) return;
    S->bird_ticks = 1;
    float dth = (rnd01(S) - 0.5f) * 0.6f;
    float dy = 0.3f + rnd01(S) * 0.6f;
    S->bird_th = wrap_pi(s->th + dth);
    S->bird_y = s->y + dy;
    S->ev_bird = 1;
    return;
  }
  S->bird_ticks++;
  if (S->bird_ticks == BIRD_WARN) {
    S->ev_peck = 1;
    bool hit = s->state != SP_JUMP && s->state != SP_ZIP && fabsf(wrap_pi(s->th - S->bird_th)) * PIPE_R < 0.32f && fabsf(s->y - S->bird_y) < 0.32f;
    if (hit) S->pecks++, wash_down(S, 2.0f, false);
  }
  if (S->bird_ticks >= BIRD_STAY) S->bird_ticks = 0;
}

static void wash_down(sim *S, float metres, bool wave) {
  spider *s = &S->s;
  s->state = SP_WASHED;
  s->slide = metres;
  s->r = 0;
  s->vr = s->vth = s->vy = 0;
  s->stun = 30;
  if (wave) S->washes++, S->ev_wash = 1;
  else S->knocks++, S->ev_knock = 1;
}

bool sim_gusting(const sim *S) { return S->L->twist == TWIST_WIND && S->gust_ticks >= GUST_EVERY - GUST_LEN; }
bool sim_gust_warn(const sim *S) {
  return S->L->twist == TWIST_WIND && S->gust_ticks >= GUST_EVERY - GUST_LEN - GUST_WARN && S->gust_ticks < GUST_EVERY - GUST_LEN;
}

static void hazards_step(sim *S) {
  const level_def *L = S->L;
  spider *s = &S->s;
  bool exposed = s->state == SP_CLING;
  /* city wind: a gust every few seconds pushes round the pipe unless braced */
  if (L->twist == TWIST_WIND) {
    if (++S->gust_ticks >= GUST_EVERY) S->gust_ticks = 0, S->gust_dir = -S->gust_dir;
    if (S->gust_dir == 0) S->gust_dir = 1;
    if (S->gust_ticks == GUST_EVERY - GUST_LEN) S->ev_gust = 1;
    if (sim_gusting(S) && exposed && !s->brace) s->th = wrap_pi(s->th + S->gust_dir * 0.75f * DT);
    if (sim_gusting(S) && s->state == SP_JUMP) s->th = wrap_pi(s->th + S->gust_dir * 1.2f * DT);
  }
  /* waves */
  for (int i = 0; i < 8; i++) {
    wave *w = &S->waves[i];
    if (!w->live) continue;
    float before = w->y;
    w->y -= WAVE_SPEED * DT;
    if (w->y < -1) {
      w->live = false;
      continue;
    }
    if (!exposed) continue;
    float d = fabsf(wrap_pi(s->th - S->stream_th[w->stream]));
    if (d < STREAM_HALF && before >= s->y + 0.05f && w->y < s->y + 0.05f) {
      if (s->brace) {
        S->braced_waves++;
        S->ev_brace_hold = 1;
      } else {
        float floor_y = L->checks[S->checkpoint];
        /* bamboo: every node is a ledge the water cannot wash you past */
        if (L->twist == TWIST_NODES) {
          float node = floorf(s->y / 2.0f) * 2.0f;
          if (node > floor_y) floor_y = node;
        }
        float drop = s->y - floor_y;
        wash_down(S, drop > 0.4f ? drop : 0.4f, true);
        exposed = false;
      }
    }
  }
  /* leaves slide down the pipe (any weather) */
  if (L->leaf_every > 0 && S->tick % L->leaf_every == 0 && S->tick > 120) {
    for (int i = 0; i < MAX_LEAVES; i++)
      if (!S->leaves[i].live) {
        float th = rnd01(S) * 6.2831853f - 3.1415927f;
        S->leaves[i] = (leaf){ .th = th, .y = L->height + 0.3f, .spin = 0, .live = true };
        break;
      }
  }
  for (int i = 0; i < MAX_LEAVES; i++) {
    leaf *f = &S->leaves[i];
    if (!f->live) continue;
    f->y -= LEAF_SPEED * DT;
    f->spin += 0.07f;
    f->th = wrap_pi(f->th + 0.004f * cw_sin(f->spin));
    if (f->y < -0.5f) {
      f->live = false;
      continue;
    }
    if (exposed && fabsf(wrap_pi(s->th - f->th)) * PIPE_R < 0.28f && fabsf(s->y - f->y) < 0.22f) {
      f->live = false;
      if (!s->brace) {
        wash_down(S, 1.4f, false);
        exposed = false;
      }
    }
  }
}

void sim_step(sim *S, const sim_input *in) {
  const level_def *L = S->L;
  spider *s = &S->s;
  S->ev_jump = S->ev_land = S->ev_wash = S->ev_knock = S->ev_check = S->ev_dew = S->ev_win = 0;
  S->ev_rain_start = S->ev_sun_start = S->ev_wave = S->ev_brace_hold = S->ev_gust = 0;
  S->ev_zip = S->ev_zip_fizzle = S->ev_peck = S->ev_bird = S->ev_fly = S->ev_spool = 0;
  S->tick++;
  if (s->state == SP_WIN) return;
  weather_step(S);
  if (s->stun > 0) s->stun--;

  float ix = cw_clampf(in->x, -1, 1), iy = cw_clampf(in->y, -1, 1);
  float mag = sqrtf(ix * ix + iy * iy);
  if (mag > 1) ix /= mag, iy /= mag, mag = 1;
  s->brace = in->brace && s->state == SP_CLING;

  switch (s->state) {
  case SP_CLING: {
    if (in->jump) {
      s->state = SP_JUMP;
      s->vr = 3.2f;
      s->vy = 4.0f + 1.4f * iy;
      s->vth = 2.0f * ix / PIPE_R;
      S->jumps++;
      S->ev_jump = 1;
      break;
    }
    if (in->silk) {
      float ay;
      if (S->silk > 0 && sim_anchor_above(S, &ay)) {
        s->state = SP_ZIP;
        S->zip_y = ay;
        S->zip_swing = 0;
        S->silk--;
        S->zips++;
        S->ev_zip = 1;
      } else S->ev_zip_fizzle = 1;
      break;
    }
    if (s->brace) break;
    bool slick = slick_here(S);
    float up = iy, side = ix;
    if (slick) {
      if (up > 0) up = 0;
      side *= 0.4f;
    }
    /* climbing against a stream while it rains is slow going */
    if (S->weather == WX_RAIN)
      for (int k = 0; k < L->n_streams; k++)
        if (fabsf(wrap_pi(s->th - S->stream_th[k])) < STREAM_HALF && up > 0) up *= 0.55f;
    float dy = up * CLIMB * DT, ds = side * CLIMB * DT;
    if (slick) dy -= SLICK_SLIDE * DT;
    s->y += dy;
    s->th = wrap_pi(s->th + ds / PIPE_R);
    if (mag > 0.15f) s->face = cw_atan2(ix, iy);
    s->walk += sqrtf(dy * dy + ds * ds);
    break;
  }
  case SP_JUMP:
    s->r += s->vr * DT;
    s->vr -= 16.0f * DT;
    s->y += s->vy * DT;
    s->vy -= 6.0f * DT;
    s->th = wrap_pi(s->th + s->vth * DT);
    s->walk += 0.02f;
    if (s->r <= 0) {
      s->r = 0;
      s->state = SP_CLING;
      s->vr = s->vy = s->vth = 0;
      S->ev_land = 1;
    }
    break;
  case SP_ZIP:
    /* reeling up the line: over wet wraps and through waves; the stick
     * swings you round the pipe as you go */
    s->y += 4.2f * DT;
    S->zip_swing += (ix * 2.4f / PIPE_R - S->zip_swing) * 0.08f;
    s->th = wrap_pi(s->th + S->zip_swing * DT);
    s->face = cw_atan2(S->zip_swing * PIPE_R * 0.3f, 1);
    s->walk += 0.01f;
    if (s->y >= S->zip_y + 0.12f) s->y = S->zip_y + 0.12f, s->state = SP_CLING, S->ev_land = 1;
    break;
  case SP_WASHED: {
    float d = WASH_SPEED * DT;
    if (d > s->slide) d = s->slide;
    s->y -= d;
    s->slide -= d;
    if (s->slide <= 0.0001f) s->state = SP_CLING, s->slide = 0;
    break;
  }
  }
  if (s->y < 0.05f) {
    s->y = 0.05f;
    if (s->state == SP_JUMP && s->vy < 0) s->state = SP_CLING, s->r = 0, s->vr = s->vy = s->vth = 0, S->ev_land = 1;
    if (s->state == SP_WASHED) s->state = SP_CLING, s->slide = 0;
  }
  if (s->y > L->height + 0.3f) s->y = L->height + 0.3f;

  hazards_step(S);

  bird_step(S);
  for (int i = S->checkpoint + 1; i < L->n_checks; i++)
    if (s->state == SP_CLING && s->y >= L->checks[i]) {
      S->checkpoint = i, S->ev_check = 1;
      if (S->silk < 3) S->silk = 3; /* brackets restock the silk */
    }
  for (int i = 0; i < MAX_FLIES; i++) {
    if (S->fly_got >> i & 1) continue;
    if (fabsf(wrap_pi(s->th - S->fly_th[i])) * PIPE_R < 0.3f && fabsf(s->y - S->fly_y[i]) < 0.3f) S->fly_got |= 1u << i, S->flies++, S->ev_fly = 1;
  }
  for (int i = 0; i < MAX_SPOOLS; i++) {
    if (S->spool_got >> i & 1) continue;
    if (fabsf(wrap_pi(s->th - S->spool_th[i])) * PIPE_R < 0.28f && fabsf(s->y - S->spool_y[i]) < 0.28f) {
      S->spool_got |= 1u << i, S->ev_spool = 1;
      if (S->silk < SILK_MAX) S->silk++;
    }
  }
  for (int i = 0; i < L->n_dews; i++) {
    if (S->dew_got >> i & 1) continue;
    if (fabsf(wrap_pi(s->th - L->dew_th[i])) * PIPE_R < 0.24f && fabsf(s->y - L->dew_y[i]) < 0.24f) {
      S->dew_got |= 1u << i;
      S->dews++;
      S->ev_dew = 1;
    }
  }
  if (s->state == SP_CLING && s->y >= L->height - 0.25f) {
    s->state = SP_WIN;
    S->win_tick = S->tick;
    S->ev_win = 1;
  }
}

/* field by field: struct padding is never hashed (it can differ by host) */
#define HF(x) h = cw_hash32(&(x), sizeof(x), h)
uint32_t sim_hash(const sim *S) {
  uint32_t h = CW_HASH32_INIT;
  const spider *s = &S->s;
  int32_t flags = s->state | (s->brace ? 16 : 0);
  HF(s->th), HF(s->y), HF(s->r), HF(s->vth), HF(s->vy), HF(s->vr), HF(s->slide), HF(s->face), HF(s->walk), HF(s->stun), HF(flags);
  HF(S->gust_ticks), HF(S->gust_dir), HF(S->silk), HF(S->zip_y), HF(S->zip_swing), HF(S->bird_ticks), HF(S->bird_th), HF(S->bird_y), HF(S->fly_got), HF(S->spool_got), HF(S->tick), HF(S->weather), HF(S->weather_ticks), HF(S->wet), HF(S->checkpoint), HF(S->rng), HF(S->dew_got);
  for (int i = 0; i < 8; i++) {
    int32_t live = S->waves[i].live;
    HF(live);
    if (live) HF(S->waves[i].y), HF(S->waves[i].stream);
  }
  for (int i = 0; i < MAX_LEAVES; i++) {
    int32_t live = S->leaves[i].live;
    HF(live);
    if (live) HF(S->leaves[i].th), HF(S->leaves[i].y), HF(S->leaves[i].spin);
  }
  return h;
}
