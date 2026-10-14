/* pugball's CPU players. Each tick a CPU picks a role from where it stands
 * against its teammates (the one nearest the cat goes for it, the next
 * supports from behind, the last minds the goal), picks a target from the
 * role, and drives at it the way a player would: one pin of throttle,
 * steering, stick and buttons, through the same physics as everyone else.
 *
 *   rookie    slow to react, never boosts far, never leaves the ground
 *             on purpose
 *   pro       boosts on the straights, jumps at bouncing cats, flips into
 *             the kickoff
 *   all-star  quick, rotates back properly, dodges into shots, and goes
 *             for the high ones with a boosted aerial
 *
 * Deterministic like the simulation: + - * / sqrtf and cartwheel math, its
 * own random stream drawn once per statement. */
#include <math.h>

#include "cartwheel/math.h"
#include "game.h"

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

void ai_reset(ai_mem *m, int me, uint32_t seed) {
  *m = (ai_mem){ 0 };
  m->rng = seed * 747796405u + (uint32_t)me * 2891336453u + 1u;
  m->target = (t3_vec3){ 0, 0, 0 };
}

static uint32_t arnd(ai_mem *m) {
  uint32_t x = m->rng ? m->rng : 1u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  m->rng = x;
  return x;
}

/* the ball a moment from now, held inside the bowl */
static t3_vec3 predict(const sim *s, float t) {
  t3_vec3 b = v3add(s->ball, v3mul(s->ball_v, t));
  b.y = s->ball.y + s->ball_v.y * t - 4.0f * t * t;
  if (b.y < RB) b.y = RB + (RB - b.y) * 0.5f;
  b.x = clampf(b.x, -AW + RB, AW - RB);
  b.z = clampf(b.z, -AL - 2, AL + 2);
  return b;
}

/* signed angle from the pug's heading to dir, about its up (right is negative) */
static float heading_to(const pug *u, t3_vec3 dir) {
  t3_vec3 f = pug_fwd(u), up = pug_up(u);
  t3_vec3 d = v3sub(dir, v3mul(up, v3dot(dir, up)));
  return cw_atan2(v3dot(v3cross(f, d), up), v3dot(f, d));
}

pin ai_think(const sim *s, int me, int level, ai_mem *m) {
  const pug *u = &s->p[me];
  pin out = { 0 };
  if (!u->on || u->dead || s->phase == PH_OVER) return out;
  float A = u->team == TEAM_BLUE ? 1.0f : -1.0f; /* the goal it attacks is at z = A * AL */
  t3_vec3 ball = s->ball;
  float spd = v3len(u->v);
  float dist = v3len(v3sub(ball, u->p));

  /* role: by closeness to the cat among the teammates on the right side of
   * it (between it and their own goal); a pug caught upfield rotates back */
  bool goal_side = (u->p.z - ball.z) * A < 1.5f;
  int closer = 0;
  for (int i = 0; i < MAXP; i++) {
    const pug *o = &s->p[i];
    if (i == me || !o->on || o->dead || o->team != u->team) continue;
    if ((o->p.z - ball.z) * A >= 1.5f) continue;
    float od = v3len(v3sub(ball, o->p));
    if (od < dist - 0.01f || (fabsf(od - dist) <= 0.01f && i < me)) closer++;
  }
  int role = !goal_side && level > 0 && s->phase == PH_PLAY ? 2 : closer == 0 ? 0 : closer == 1 ? 1 : 2;
  m->role = role;

  int lag = level == 0 ? 16 : level == 1 ? 7 : 2;
  if (m->react > 0) m->react--;
  else {
    m->react = lag;
    float t = clampf(dist / (spd + 6), 0, level == 2 ? 1.4f : 0.8f);
    t3_vec3 bp = predict(s, t);
    t3_vec3 goal = { 0, 0, A * (AL + 3) };
    t3_vec3 to_goal = v3norm((t3_vec3){ goal.x - bp.x, 0, goal.z - bp.z });
    if (role == 0) {
      /* line up behind the cat, toward their goal; once lined up, charge it */
      t3_vec3 behind = v3sub(bp, v3mul(to_goal, RB + 1.6f));
      t3_vec3 me_to = v3norm((t3_vec3){ bp.x - u->p.x, 0, bp.z - u->p.z });
      m->target = v3dot(me_to, to_goal) > 0.55f ? bp : behind;
      /* never charge it toward our own goal: go round instead */
      if (v3dot(me_to, (t3_vec3){ 0, 0, -A }) > 0.6f && (bp.z * -A) > AL - 20) m->target = v3add(behind, (t3_vec3){ bp.x > u->p.x ? -3.0f : 3.0f, 0, 0 });
    } else if (role == 1) {
      m->target = (t3_vec3){ bp.x * 0.5f, 0, bp.z - A * 14 };
    } else {
      float side = bp.x > 0 ? 1.0f : -1.0f;
      m->target = (t3_vec3){ side * 2.5f, 0, -A * (AL - 3.5f) };
      if ((ball.z * -A) > AL - 12 && fabsf(ball.x) < GW + 4) {
        /* it is at the door: clear it, from the goal side so it goes away */
        t3_vec3 own = { 0, 0, -A * AL };
        t3_vec3 away = v3norm((t3_vec3){ bp.x - own.x, 0, bp.z - own.z });
        t3_vec3 me_to = v3norm((t3_vec3){ bp.x - u->p.x, 0, bp.z - u->p.z });
        m->target = v3dot(me_to, away) > 0.3f ? bp : v3sub(bp, v3mul(away, RB + 1.8f));
      }
    }
    m->target.x = clampf(m->target.x, -AW + 2, AW - 2);
    m->target.z = clampf(m->target.z, -AL + 1.5f, AL - 1.5f);
  }

  t3_vec3 to = v3sub(m->target, u->p);
  float tdist = sqrtf(to.x * to.x + to.z * to.z);
  float ang = heading_to(u, to);
  float steer = clampf(-ang * 2.4f, -1, 1);
  float thr = level == 0 ? 0.78f : 1;
  if (role == 2 && tdist < 3) thr = 0.2f; /* parked in goal: creep */
  if (fabsf(ang) > 2.2f && tdist < 7) { /* it is right behind: back up into a turn */
    thr = -1;
    steer = -steer;
  }
  /* nosed into a wall (or another pug) and going nowhere: back out of it, turning the other way */
  if (m->unstick > 0) {
    m->unstick--;
    thr = -1;
    steer = steer > 0 ? -1.0f : 1.0f;
  } else if (u->grounded && thr > 0.5f && spd < 1.0f && s->phase == PH_PLAY) {
    if (++m->stuck > 40) m->stuck = 0, m->unstick = 35;
  } else m->stuck = 0;
  out.thr = (int8_t)(thr * 127);
  out.steer = (int8_t)(steer * 127);
  if (u->grounded && fabsf(ang) > 1.3f && spd > 9) out.btn |= PB_SLIDE;

  /* boost on the straights */
  bool far = tdist > (level == 2 ? 8 : 14);
  if (fabsf(ang) < 0.25f && far && u->boost > (level == 0 ? 60 : 8) && spd < 21.5f && level > 0) out.btn |= PB_BOOST;
  if (s->phase == PH_PLAY && s->phase_ticks < 120 && level > 0 && role == 0 && fabsf(ang) < 0.3f) out.btn |= PB_BOOST;

  /* jumps: at a bouncing cat in front, an aerial at a high one, a flip for speed */
  t3_vec3 fb = v3sub(ball, u->p);
  float hd = sqrtf(fb.x * fb.x + fb.z * fb.z);
  float front = fabsf(heading_to(u, fb));
  if (m->jump_hold > 0) {
    m->jump_hold--;
    out.btn |= PB_JUMP;
    if (level == 2 && ball.y > 4.5f) {
      out.pitch = 90; /* nose up toward it */
      out.btn |= PB_BOOST;
    }
  } else if (u->grounded && role == 0 && level > 0 && hd < 5 + ball.y * 0.4f && ball.y > 2.4f && ball.y < 7.5f && front < 0.35f) {
    m->jump_hold = level == 2 && ball.y > 4.5f ? 30 : 12;
    out.btn |= PB_JUMP;
  } else if (!u->grounded && u->second && level > 0 && u->jump_ticks > 14 && u->jump_ticks < 60 && dist < RB + 2.4f) {
    out.btn |= PB_JUMP; /* dodge into it */
    out.pitch = -127;
    out.steer = (int8_t)clampf(steer * 127, -127, 127);
  } else if (u->grounded && level > 0 && s->phase == PH_PLAY && s->phase_ticks < 200 && role == 0 && dist > 6 && dist < 9 &&
             front < 0.15f && spd > 10) {
    m->dodge_wait = 8; /* the kickoff flip */
    out.btn |= PB_JUMP;
  } else if (m->dodge_wait > 0) {
    if (--m->dodge_wait == 0) {
      out.btn |= PB_JUMP;
      out.pitch = -127;
    }
  }
  if (level == 0 && (arnd(m) & 15u) == 0) out.btn &= (uint8_t)~PB_BOOST; /* rookies fumble */
  if (!u->grounded && !(out.btn & PB_JUMP) && m->jump_hold == 0) {
    /* land on its feet: level out toward the ground */
    t3_vec3 up = pug_up(u);
    if (up.y < 0.7f) out.pitch = (int8_t)clampf(-pug_fwd(u).y * 300, -127, 127);
  }
  return out;
}
