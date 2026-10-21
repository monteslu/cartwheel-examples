/* rider's simulation: the track as Box2D chains and a bicycle with its rider
 * in one 2D world (x along the track, y up, z = 0 is the riding line).
 *
 *   bike    a frame body carrying the rider's torso and head (so the rider's
 *           weight sits high, where leaning moves it), two wheels on wheel
 *           joints (soft suspension), the rear one driven by the pedals
 *   lean    a torque on the frame: pull wheelies, flip in the air, recover
 *   crash   the torso or head touching the ground: the rider comes off.
 *           The rider (a ten-piece limp ragdoll), the bike and the cap leave
 *           the plane for the 3D crash world (crash.c), taking the motion
 *           they had, thrown on harder the faster the crash, and tumble
 *           there against the scenery till the respawn
 *   ground  each wheel is grounded when a short ray under its axle hits the
 *           track; air time, the rotation summed in the air and wheelies are
 *           measured from that
 *
 * Fixed step, cartwheel/math.h only, Box2D's and Box3D's deterministic
 * solvers: every host steps the same worlds (debug field bike.hash). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "cartwheel/debug.h"

/* the loops' two halves sit on their own layers: the bike meets the climb
 * (A) until it is past the top, then the descent (B) until it is out the
 * other side, so the run-in and the run-out pass through the other half */
enum { L_TRACK, L_BIKE, L_LOOP_A, L_LOOP_B, L_LANE0, L_RAMP0 = L_LANE0 + 4 };
/* each lane's part of a partial ramp sits on L_RAMP0 + lane: only that
 * lane's bikes meet it */
/* the riders' bikes sit on their lane's layer (L_LANE0 + lane): they meet
 * the track, the loop half in use and the bikes in their own lane */
#define LANE_BITS (((1u << MAX_RIDERS) - 1) << L_LANE0)
#define MAX_LOOPS 4
/* a folding bike: 16-inch wheels, a 1.02 m wheelbase, the bottom bracket a
 * little above the axles; the rider sits as on a full-size bike */
#define WHEEL_R 0.2f
#define HOP_V 5.5f   /* the hop: the upward speed it gives the bike, m/s */
#define SUBSTEPS 3        /* Box2D steps a tick (see bike_step) */
#define TOP_SPIN 80.0f    /* rad/s the legs can turn the rear wheel: 16 m/s */
#define GEAR 0.26f        /* crank turns per wheel turn (a big ring, a small sprocket) */
/* riders meeting: below this closing speed (m/s) a touch to the rider is a
 * shove, not a wipeout; a hard landing on someone or a fast rear-end crashes */
#define BUMP_CRASH_V 6.0f
#define AIR_SPIN_MAX 11.0f /* rad/s: the most the bike turns in the air (1.75 turns a second) */
static const cw_vec2 REAR_AXLE = { -0.46f, -0.05f }, FRONT_AXLE = { 0.56f, -0.05f };
/* the rider's collision shapes on the frame (frame-local) */
static const cw_vec2 HIP = { -0.12f, 0.84f }, SHOULDER = { 0.2f, 1.3f }, HEAD = { 0.32f, 1.55f };
#define TORSO_R 0.14f
#define HEAD_R 0.14f

#define MAX_PLANKS 96
#define MAX_MOVERS 8
#define MAX_LOGS 6
static const float DEER_X[DEER_CROSSINGS]={106.0f,334.0f};
typedef struct mover {
  int kind;
  float x, y, len;
  cw_body2d body;                 /* the deck, plank or ball */
  cw_body2d logs[MAX_LOGS];
  int log_n, next_log;
} mover;
typedef struct plank {
  cw_body2d body;
  float x, half_w;
  cw_xform2 home;  /* where it was built (put back on a respawn) */
  int timer; /* ticks since the rider passed; it drops at PLANK_DROP */
} plank;
#define PLANK_DROP 30

/* the world and what every rider shares: the track, the bridges, the set pieces */
static struct {
  cw_world2d world;
  cw_body2d ground;
  float hop_v;               /* the hop's speed on this track: the same height under any gravity */
  float kill_y;
  plank planks[MAX_PLANKS];
  int plank_count;
  mover movers[MAX_MOVERS];
  int mover_count;
  int tick;                       /* steps since the world was made: drives the set pieces */
  int deer_start[DEER_CROSSINGS];
  int bumps;                      /* contacts begun between two riders' bikes */
  /* the planks and set pieces mirrored in the crash world (kinematic, made at the first crash) */
  bool mirrored;
  cw_body3d plank3[MAX_PLANKS], mover3[MAX_MOVERS], log3[MAX_MOVERS][MAX_LOGS];
} WD;
static int deer_hits;

/* one bicycle and its rider (up to MAX_RIDERS share the world, each on its
 * own lane: a collision layer, so riders in one lane bump and riders in
 * different lanes pass through each other) */
typedef struct bk {
  int idx, lane;
  cw_body2d frame, rear, front;
  cw_joint2d j_rear, j_front;
  cw_shape2d torso, head;
  /* a crash, in the 3D crash world: the rider, the bike, the cap */
  bool down3;
  cw_body3d rag[RAG_COUNT], frame3, rear3, front3, cap;
  cw_joint3d grip;           /* one hand still on the bars a moment, dragged by the bike till it tears free */
  int grip_ticks;
  float draw_z;              /* the depth the rider is drawn at (bike_set_z): the crash starts there */
  cw_vec2 crash_v;           /* the velocity the rider carries into the crash */
  bike_state st;
  bool crashed_now;
  int landed_now;
  float landed_rot;
  int landed_air;
  float landed_err;           /* the frame against the slope as it came down (radians) */
  int landed_pose[4];         /* ticks each pose was held fully in that air */
  int pose_kind;              /* POSE_*: the pose shown (held, or easing back) */
  float pose_amt;             /* 0 riding .. 1 in the pose */
  int pose_ticks[4];          /* this air's ticks fully in each pose */
  bool bailed_now;            /* came down still in a pose */
  float prev_angle;
  bool loop_b[MAX_LOOPS];    /* past the top of loop i: riding its descent */
  int loop_layer;            /* the loop layer the bike collides with now */
  bool loop_zone;            /* over a loop's footprint: the ground assists stand down */
  cw_shape2d frame_sh, rear_sh, front_sh;
  bool air_leaned;
  int hop_cool, hop_phase, hop_buf; /* ticks until the next hop; the hop's second beat; a press waiting for both wheels */
  float crank;
  cw_vec2 v_pre;             /* the frame's velocity before this half step (contact severity) */
  float loop_t;              /* 0..1 round the loop being ridden (0: not in one) */
} bk;
static bk BK[MAX_RIDERS];
static bk *cur = BK;
static int NB = 1; /* riders in this world */
#define B (*cur)

static cw_xform2 xmul(cw_xform2 a, cw_vec2 p) {
  return (cw_xform2){ { a.p.x + a.q.c * p.x - a.q.s * p.y, a.p.y + a.q.s * p.x + a.q.c * p.y }, a.q };
}
static cw_vec2 xpt(cw_xform2 a, cw_vec2 p) { return xmul(a, p).p; }
static float cw_fmodf_pos(float a, float m) { float r = a - m * floorf(a / m); return r; }

/* ── the track ───────────────────────────────────────────────────────── */

static const track_desc *T;
static float ramp_profile(const float *r, float x);

static bool on_ice(float x) {
  for (int i = 0; i < T->ice_count; i++)
    if (x >= T->ice[i * 2] && x <= T->ice[i * 2 + 1]) return true;
  return false;
}

void bike_world_create(const track_desc *t) {
  bike_world_destroy();
  T = t;
  cw_world2d_def wd = cw_world2d_default_def();
  wd.gravity = (cw_vec2){ 0, -t->gravity };
  WD.hop_v = HOP_V * sqrtf(t->gravity / 9.81f);
  wd.substeps = 4;
  wd.workers = 0; /* every job-pool thread: Box2D gives the same result at any worker count */
  wd.hit_speed = 1.5f;
  WD.world = cw_world2d_create(&wd);
  cw_body2d_def bd = cw_body2d_default_def();
  bd.type = CW_BODY_STATIC;
  bd.name = "track";
  WD.ground = cw_body2d_create(WD.world, &bd);
  cw_shape2d_def sd = cw_shape2d_default_def();
  sd.layer = L_TRACK;
  sd.material.friction = 0.95f;
  float bottom = t->min_y - 40;
  for (int i = 0; i < t->piece_count; i++) {
    const track_piece *pc = &t->pieces[i];
    int n = pc->count;
    const float *p = pc->pts;
    /* runs of points with the same surface (dirt or ice), each its own
     * one-sided chain, right to left so the solid side is below; the
     * neighbouring points are its ghosts, so the runs join smoothly */
    int k0 = 0;
    while (k0 < n - 1) {
      bool icy = on_ice(p[k0 * 2] + 0.1f);
      int k1 = k0 + 1;
      while (k1 < n - 1 && on_ice(p[k1 * 2] + 0.1f) == icy) k1++;
      int m = k1 - k0 + 1;
      cw_vec2 *pts = malloc(sizeof *pts * (size_t)(m + 2));
      pts[0] = k1 + 1 < n ? (cw_vec2){ p[(k1 + 1) * 2], p[(k1 + 1) * 2 + 1] } : (cw_vec2){ p[k1 * 2] + 1, p[k1 * 2 + 1] };
      for (int k = 0; k < m; k++) pts[k + 1] = (cw_vec2){ p[(k1 - k) * 2], p[(k1 - k) * 2 + 1] };
      pts[m + 1] = k0 > 0 ? (cw_vec2){ p[(k0 - 1) * 2], p[(k0 - 1) * 2 + 1] } : (cw_vec2){ p[0] - 1, p[1] };
      sd.material.friction = (icy ? 0.12f : 0.95f) * t->grip;
      cw_chain2d_create(WD.ground, &sd, pts, m + 2, false);
      free(pts);
      k0 = k1;
    }
    sd.material.friction = 0.95f * t->grip;
    /* the piece's two cliff faces, solid both ways */
    cw_shape2d_segment(WD.ground, &sd, (cw_vec2){ p[0], p[1] }, (cw_vec2){ p[0], bottom });
    cw_shape2d_segment(WD.ground, &sd, (cw_vec2){ p[(n - 1) * 2], p[(n - 1) * 2 + 1] }, (cw_vec2){ p[(n - 1) * 2], bottom });
  }
  /* plank bridges over the pits: static until the rider has gone by, then
   * each plank drops */
  WD.plank_count = 0;
  for (int i = 0; i < t->bridge_count; i++) {
    const float *br = &t->bridges[i * 4];
    float len = br[2] - br[0];
    int n = (int)(len / 0.6f) + 1;
    for (int k = 0; k < n && WD.plank_count < MAX_PLANKS; k++) {
      float u = (k + 0.5f) / n, x = br[0] + len * u, y = br[1] + (br[3] - br[1]) * u - 0.05f;
      float ang = cw_atan2(br[3] - br[1], len);
      cw_body2d_def pb = cw_body2d_default_def();
      pb.type = CW_BODY_STATIC;
      pb.position = (cw_pos2){ x, y };
      pb.rotation = cw_rot2_from_angle(ang);
      pb.name = "plank";
      cw_body2d body = cw_body2d_create(WD.world, &pb);
      cw_shape2d_def ps = cw_shape2d_default_def();
      ps.layer = L_TRACK;
      ps.density = 8;
      ps.material.friction = 0.9f * t->grip;
      /* planks butt together with rounded edges: no lips for a tyre to catch */
      float hw = len / n * 0.5f + 0.005f;
      const cw_vec2 box[4] = { { -hw + 0.02f, -0.03f }, { hw - 0.02f, -0.03f }, { hw - 0.02f, 0.03f }, { -hw + 0.02f, 0.03f } };
      cw_shape2d_polygon(body, &ps, box, 4, 0.02f);
      WD.planks[WD.plank_count] = (plank){ body, x, hw, { { x, y }, pb.rotation }, 0 };
      WD.plank_count++;
    }
  }
  /* the high routes: a flat plank deck each, solid from above */
  for (int i = 0; i < t->high_count; i++) {
    float x0 = t->highs[i * 4], x1 = t->highs[i * 4 + 1], y0 = t->highs[i * 4 + 2], y1 = t->highs[i * 4 + 3];
    float dy = (y1 - y0) / (x1 - x0);
    sd.material.friction = 0.95f * t->grip;
    cw_vec2 deck[4] = { { x1 + 1, y1 + dy }, { x1, y1 }, { x0, y0 }, { x0 - 1, y0 } };
    cw_chain2d_create(WD.ground, &sd, deck, 4, false);
    cw_shape2d_segment(WD.ground, &sd, (cw_vec2){ x1, y1 }, (cw_vec2){ x1, y1 - 0.35f }); /* the deck's end face */
  }
  /* the loops: two one-sided arcs each, solid on the inside (wound
   * clockwise), with a ghost point past each end so the ends join smoothly */
  for (int i = 0; i < t->loop_count && i < MAX_LOOPS; i++) {
    float cx = t->loops[i * 3], cy = t->loops[i * 3 + 1] + t->loops[i * 3 + 2], r = t->loops[i * 3 + 2];
    int N = (int)ceilf(24*fmaxf(1,r/5.6f));
    if(N>128)N=128;
    cw_vec2 a[131], b[131];
    /* A, the climb: from just past the top round the right side to the bottom, then along the ground */
    a[0] = (cw_vec2){ cx + r * cw_cos(1.7453293f), cy + r * cw_sin(1.7453293f) };
    for (int k = 0; k <= N; k++) {
      float th = 1.5707963f - 3.1415927f * k / N;
      a[k + 1] = (cw_vec2){ cx + r * cw_cos(th), cy + r * cw_sin(th) };
    }
    a[N + 2] = (cw_vec2){ cx - 1, cy - r };
    /* B, the descent: from the ground right of the bottom, round the left side to the top, then past it */
    b[0] = (cw_vec2){ cx + 1, cy - r };
    for (int k = 0; k <= N; k++) {
      float th = 4.712389f - 3.1415927f * k / N;
      b[k + 1] = (cw_vec2){ cx + r * cw_cos(th), cy + r * cw_sin(th) };
    }
    b[N + 2] = (cw_vec2){ cx + r * cw_cos(1.3962634f), cy + r * cw_sin(1.3962634f) };
    cw_shape2d_def ld = sd;
    ld.material.friction = 0.95f * t->grip;
    ld.layer = L_LOOP_A;
    ld.mask = LANE_BITS;
    cw_chain2d_create(WD.ground, &ld, a, N + 3, false);
    ld.layer = L_LOOP_B;
    cw_chain2d_create(WD.ground, &ld, b, N + 3, false);
  }
  for (int i = 0; i < MAX_RIDERS; i++) BK[i].loop_layer = L_LOOP_A;
  /* partial ramps: the same profile once for each lane they cover, on that lane's ramp layer */
  for (int i = 0; i < t->ramp_count; i++) {
    const float *r = &t->ramps[i * 7];
    float len = r[1] + r[3] + r[4];
    int n = (int)(len / 0.25f) + 1;
    cw_vec2 *pts = malloc(sizeof *pts * (size_t)(n + 3));
    /* right to left (solid below), a ghost each end */
    pts[0] = (cw_vec2){ r[0] + len + 1, track_height_at(r[0] + len + 1, NULL) };
    for (int k = 0; k <= n; k++) {
      float x = r[0] + len - len * k / n;
      pts[k + 1] = (cw_vec2){ x, track_height_at(x, NULL) + ramp_profile(r, x) + (k == 0 || k == n ? 0 : 0) };
    }
    pts[n + 2] = (cw_vec2){ r[0] - 1, track_height_at(r[0] - 1, NULL) };
    for (int l = (int)r[5]; l <= (int)r[6] && l < LANES; l++) {
      cw_shape2d_def rd = sd;
      rd.material.friction = 0.95f * t->grip;
      rd.layer = L_RAMP0 + l;
      rd.mask = 1u << (L_LANE0 + l);
      cw_chain2d_create(WD.ground, &rd, pts, n + 3, false);
    }
    free(pts);
  }
  /* moving set pieces */
  WD.mover_count = 0;
  for (int i = 0; i < t->mover_count && i < MAX_MOVERS; i++) {
    mover *m = &WD.movers[WD.mover_count++];
    memset(m, 0, sizeof *m);
    m->kind = (int)t->movers[i * 4];
    m->x = t->movers[i * 4 + 1], m->y = t->movers[i * 4 + 2], m->len = t->movers[i * 4 + 3];
    cw_body2d_def mb = cw_body2d_default_def();
    cw_shape2d_def ms = cw_shape2d_default_def();
    ms.layer = L_TRACK;
    ms.material.friction = 0.9f * t->grip;
    if (m->kind == MOVER_DRAWBRIDGE || m->kind == MOVER_WRECK) {
      mb.type = CW_BODY_KINEMATIC;
      mb.position = (cw_pos2){ m->x, m->y };
      mb.name = "mover";
      m->body = cw_body2d_create(WD.world, &mb);
      if (m->kind == MOVER_DRAWBRIDGE) {
        const cw_vec2 deck[4] = { { 0.02f, -0.12f }, { m->len - 0.02f, -0.12f }, { m->len - 0.02f, -0.02f }, { 0.02f, -0.02f } };
        cw_shape2d_polygon(m->body, &ms, deck, 4, 0.02f);
      } else cw_shape2d_circle(m->body, &ms, (cw_vec2){ 0, 0 }, 0.6f);
    } else if (m->kind == MOVER_SEESAW) {
      mb.position = (cw_pos2){ m->x, m->y + 0.18f };
      mb.rotation = cw_rot2_from_angle(-0.1f);
      mb.name = "seesaw";
      m->body = cw_body2d_create(WD.world, &mb);
      ms.density = 6;
      const cw_vec2 board[4] = { { -m->len * 0.5f, -0.04f }, { m->len * 0.5f, -0.04f }, { m->len * 0.5f, 0.03f }, { -m->len * 0.5f, 0.03f } };
      cw_shape2d_polygon(m->body, &ms, board, 4, 0.01f);
      cw_joint2d_def jd = cw_joint2d_default_def(CW_JOINT_REVOLUTE);
      jd.a = (cw_body2d){ 0 };
      jd.b = m->body;
      cw_joint2d_def_anchor(&jd, (cw_pos2){ m->x, m->y + 0.18f }, (cw_vec2){ 0, 0 });
      jd.limit = true;
      jd.lower = -0.1f;
      jd.upper = 0.1f;
      cw_joint2d_create(WD.world, &jd);
    }
  }
  WD.tick = 0;
  for(int i=0;i<DEER_CROSSINGS;i++)WD.deer_start[i]=-1;
  deer_hits = 0;
  cw_debug_i32("deer.hits", &deer_hits);
  /* a wall past the run-out */
  const track_piece *last = &t->pieces[t->piece_count - 1];
  float ex = last->pts[(last->count - 1) * 2], ey = last->pts[(last->count - 1) * 2 + 1];
  cw_shape2d_box(WD.ground, &sd, (cw_vec2){ 0.5f, 20 }, cw_xform2_at(ex + 0.5f, ey + 18));
  /* and one behind the start */
  cw_shape2d_box(WD.ground, &sd, (cw_vec2){ 0.5f, 20 }, cw_xform2_at(t->pieces[0].pts[0] - 6, t->pieces[0].pts[1] + 18));
  WD.kill_y = t->min_y - 12;
}

void bike_world_destroy(void) {
  crash_world_destroy(); /* the crash world goes with the track (crash_world_build makes it again) */
  if (cw_world2d_valid(WD.world)) cw_world2d_destroy(WD.world);
  memset(&WD, 0, sizeof WD);
  memset(BK, 0, sizeof BK);
  for (int i = 0; i < MAX_RIDERS; i++) BK[i].idx = BK[i].lane = i;
  cur = BK;
  NB = 1;
  T = NULL;
}

void bike_set_riders(int n) { NB = CW_CLAMP(n, 1, MAX_RIDERS); }
int bike_riders(void) { return NB; }
int bike_bumps(void) { return WD.bumps; }
void bike_select(int i) { cur = &BK[CW_CLAMP(i, 0, MAX_RIDERS - 1)]; }

/* the layer and mask of the riding bike's shapes */
static uint32_t bike_mask(void) { return 1u << L_TRACK | 1u << B.loop_layer | 1u << (L_LANE0 + B.lane) | 1u << (L_RAMP0 + B.lane); }
static void bike_relayer(void) {
  cw_shape2d sh[5] = { B.frame_sh, B.rear_sh, B.front_sh, B.torso, B.head };
  for (int k = 0; k < 5; k++)
    if (sh[k].id && cw_shape2d_valid(sh[k])) cw_shape2d_set_layer(sh[k], L_LANE0 + B.lane, bike_mask());
}
void bike_set_lane(int lane) {
  lane = CW_CLAMP(lane, 0, MAX_RIDERS - 1);
  if (lane == B.lane) return;
  B.lane = lane;
  bike_relayer();
}
int bike_lane(void) { return B.lane; }
float bike_loop_shift(void) { return cw_smoothstep(0.25f, 0.75f, B.loop_t); }

/* the narrows: each pinch, and each loop's run-in (back lanes, a barrier
 * closing the front ones) and run-out (front lanes; the loop itself fills
 * the back ones) */
int track_narrows(const track_desc *t, narrow *out) {
  int n = 0;
  if (!t) return 0;
  for (int i = 0; i < t->pinch_count && n < MAX_NARROWS; i++) {
    const float *p = &t->pinches[i * 4];
    out[n++] = (narrow){ p[0], p[1], (int)p[2], (int)p[3], true };
  }
  for (int i = 0; i < t->loop_count && n + 1 < MAX_NARROWS; i++) {
    float cx = t->loops[i * 3], r = t->loops[i * 3 + 2];
    out[n++] = (narrow){ cx - r - 4, cx, 0, 1, true };
    out[n++] = (narrow){ cx, cx + r + 4, 2, 3, false };
  }
  return n;
}
bool track_lane_open(const track_desc *t, float x, int lane) {
  narrow nw[MAX_NARROWS];
  int n = track_narrows(t, nw);
  for (int i = 0; i < n; i++)
    if (x >= nw[i].x0 && x < nw[i].x1 && (lane < nw[i].lo || lane > nw[i].hi)) return false;
  return true;
}

static float ramp_profile(const float *r, float x) {
  float x0 = r[0], up = r[1], h = r[2], top = r[3], dn = r[4];
  if (x <= x0 || x >= x0 + up + top + dn) return 0;
  if (x < x0 + up) return h * (1 - cw_cos(3.1415927f * (x - x0) / up)) * 0.5f;
  if (x <= x0 + up + top) return h;
  return h * (1 + cw_cos(3.1415927f * (x - x0 - up - top) / dn)) * 0.5f;
}
float track_ramp_h(const track_desc *t, float x, int lane) {
  float h = 0;
  if (!t) return 0;
  for (int i = 0; i < t->ramp_count; i++) {
    const float *r = &t->ramps[i * 7];
    if (lane >= (int)r[5] && lane <= (int)r[6]) h = fmaxf(h, ramp_profile(r, x));
  }
  return h;
}
/* the ground under the selected rider: the track, and a ramp in its lane */
static float ground_at(float x, bool *found) { return track_height_at(x, found) + track_ramp_h(T, x, B.lane); }

/* ground height under x (the highest track point at x), for spawning and the camera */
float track_height_at(float x, bool *found) {
  float best = -1e9f;
  bool any = false;
  if (T)
    for (int i = 0; i < T->piece_count; i++) {
      const track_piece *pc = &T->pieces[i];
      const float *p = pc->pts;
      if (x < p[0] || x > p[(pc->count - 1) * 2]) continue;
      /* samples are 0.25 m apart and sorted by x */
      int lo = 0, hi = pc->count - 1;
      while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (p[mid * 2] <= x) lo = mid;
        else hi = mid;
      }
      float x0 = p[lo * 2], x1 = p[hi * 2], t = x1 > x0 ? (x - x0) / (x1 - x0) : 0;
      float y = p[lo * 2 + 1] + (p[hi * 2 + 1] - p[lo * 2 + 1]) * t;
      if (y > best) best = y;
      any = true;
    }
  if (T)
    for (int i = 0; i < T->bridge_count; i++) {
      const float *br = &T->bridges[i * 4];
      if (x < br[0] || x > br[2]) continue;
      float y = br[1] + (br[3] - br[1]) * (x - br[0]) / (br[2] - br[0]);
      if (y > best) best = y;
      any = true;
    }
  if (found) *found = any;
  return any ? best : 0;
}

/* ── the bike ────────────────────────────────────────────────────────── */

static void crash_destroy(void);
static void destroy_bike(void) {
  crash_destroy();
  if (cw_body2d_valid(B.frame)) cw_body2d_destroy(B.frame);
  if (cw_body2d_valid(B.rear)) cw_body2d_destroy(B.rear);
  if (cw_body2d_valid(B.front)) cw_body2d_destroy(B.front);
  B.frame = B.rear = B.front = (cw_body2d){ 0 };
  B.torso = B.head = (cw_shape2d){ 0 };
  B.frame_sh = B.rear_sh = B.front_sh = (cw_shape2d){ 0 };
  memset(B.loop_b, 0, sizeof B.loop_b);
  B.loop_layer = L_LOOP_A;
  B.loop_zone = false;
  B.loop_t = 0;
}

static cw_body2d wheel(cw_vec2 at, float spin) {
  cw_body2d_def bd = cw_body2d_default_def();
  bd.position = cw_pos2_from(at);
  bd.angular_velocity = spin;
  bd.name = "wheel";
  cw_body2d w = cw_body2d_create(WD.world, &bd);
  cw_shape2d_def sd = cw_shape2d_default_def();
  sd.layer = L_LANE0 + B.lane;
  sd.mask = bike_mask();
  sd.group = -1 - 2 * B.idx;
  sd.density = 9.5f; /* about 1.2 kg */
  sd.material.friction = 1.4f;
  sd.material.restitution = 0.05f;
  sd.material.rolling_resistance = 0.004f;
  sd.hit_events = true;
  cw_shape2d sh = cw_shape2d_circle(w, &sd, (cw_vec2){ 0, 0 }, WHEEL_R);
  if (B.rear_sh.id) B.front_sh = sh;
  else B.rear_sh = sh;
  return w;
}

static cw_joint2d axle(cw_body2d w, cw_vec2 local) {
  cw_joint2d_def jd = cw_joint2d_default_def(CW_JOINT_WHEEL);
  jd.a = B.frame;
  jd.b = w;
  /* travel along the frame's y (frame a's x is rotated a quarter turn) */
  jd.frame_a = (cw_xform2){ local, cw_rot2_from_angle(1.5707963f) };
  jd.frame_b = cw_xform2_identity();
  jd.spring = true;
  jd.hertz = 4.5f;
  jd.damping_ratio = 0.8f;
  jd.limit = true;
  jd.lower = -0.06f;
  jd.upper = 0.12f;
  jd.motor = true;
  jd.motor_speed = 0;
  jd.max_motor = 0.2f;
  return cw_joint2d_create(WD.world, &jd);
}

void bike_spawn(float x, float y, float v0) {
  destroy_bike();
  /* the bridges are whole again */
  for (int i = 0; i < WD.plank_count; i++) {
    plank *pk = &WD.planks[i];
    cw_body2d_set_type(pk->body, CW_BODY_STATIC);
    cw_body2d_set_transform(pk->body, pk->home);
    pk->timer = 0;
  }
  bool found;
  float gy = ground_at(x, &found); /* the track, and a ramp in this lane */
  if (found) y = gy;
  /* level, wheels just on the ground */
  float base = y + WHEEL_R - REAR_AXLE.y + 0.02f;
  cw_body2d_def bd = cw_body2d_default_def();
  bd.position = (cw_pos2){ x, base };
  bd.name = "frame";
  bd.angular_damping = 0.05f;
  B.frame = cw_body2d_create(WD.world, &bd);
  cw_shape2d_def sd = cw_shape2d_default_def();
  sd.layer = L_LANE0 + B.lane;
  sd.mask = bike_mask();
  sd.group = -1 - 2 * B.idx;
  sd.density = 6;
  sd.material.friction = 0.5f;
  const cw_vec2 hull[] = { { -0.42f, 0.02f }, { 0.0f, -0.02f }, { 0.52f, 0.02f }, { 0.46f, 0.62f }, { -0.2f, 0.58f } };
  B.frame_sh = cw_shape2d_polygon(B.frame, &sd, hull, 5, 0.02f);
  sd.density = 30;
  B.torso = cw_shape2d_capsule(B.frame, &sd, HIP, SHOULDER, TORSO_R);
  sd.density = 30;
  B.head = cw_shape2d_circle(B.frame, &sd, HEAD, HEAD_R);
  /* v0: a rolling start (respawns), wheels turning to match */
  cw_body2d_set_velocity(B.frame, (cw_vec2){ v0, 0 });
  B.rear = wheel((cw_vec2){ x + REAR_AXLE.x, base + REAR_AXLE.y }, -v0 / WHEEL_R);
  B.front = wheel((cw_vec2){ x + FRONT_AXLE.x, base + FRONT_AXLE.y }, -v0 / WHEEL_R);
  cw_body2d_set_velocity(B.rear, (cw_vec2){ v0, 0 });
  cw_body2d_set_velocity(B.front, (cw_vec2){ v0, 0 });
  B.j_rear = axle(B.rear, REAR_AXLE);
  B.j_front = axle(B.front, FRONT_AXLE);
  int hops = B.st.hops, loops = B.st.loops; /* the ride's hops and loops count on through respawns (a new track's world starts at 0) */
  memset(&B.st, 0, sizeof B.st);
  B.st.hops = hops;
  B.st.loops = loops;
  B.st.alive = true;
  /* where it is now: bike_post's barrier test takes the last step's x as where
   * the bike came from (left at 0, a respawn past a lane's barrier was a fresh
   * hit on it, thrown back off the bike on the first tick, every time) */
  B.st.x = x;
  B.st.y = base;
  B.pose_kind = POSE_NONE;
  B.pose_amt = 0;
  memset(B.pose_ticks, 0, sizeof B.pose_ticks);
  B.prev_angle = 0;
  B.crashed_now = false;
  B.landed_now = 0;
}

/* ── the crash, in 3D ──────────────────────────────────────────────────
 * The rider comes off: the rider, the bike and the cap are rebuilt in the
 * crash world (crash.c) where they were drawn (the riding pose, at the
 * rider's depth), the bike moving as it was, the rider with the crash's
 * momentum, and the 2D bike goes. The rider is ten limp bodies: ball
 * joints at the neck, shoulders and hips and hinges at the elbows and knees,
 * each held only by an anatomical limit (and a little friction); the bike a
 * frame and two wheels on axles. The faster the crash, the harder the
 * throw: on along the line of travel, up, and out to one side, with a tumble
 * over the bars; a tip-over at walking pace just slumps. */

static t3_vec3 v3add(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static t3_vec3 v3sub(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static t3_vec3 v3scale(t3_vec3 a, float k) { return (t3_vec3){ a.x * k, a.y * k, a.z * k }; }
static t3_vec3 v3cross(t3_vec3 a, t3_vec3 b) { return (t3_vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static float v3dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static t3_vec3 v3norm(t3_vec3 a) {
  float l = sqrtf(v3dot(a, a));
  return l > 1e-9f ? v3scale(a, 1 / l) : (t3_vec3){ 1, 0, 0 };
}
static t3_quat qmul(t3_quat a, t3_quat b) {
  return (t3_quat){ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static t3_quat qconj(t3_quat q) { return (t3_quat){ -q.x, -q.y, -q.z, q.w }; }
static t3_vec3 qrot(t3_quat q, t3_vec3 v) {
  t3_vec3 u = { q.x, q.y, q.z }, t = v3scale(v3cross(u, v), 2);
  return v3add(v3add(v, v3scale(t, q.w)), v3cross(u, t));
}
/* the shortest rotation taking unit a to unit b */
static t3_quat qbetween(t3_vec3 a, t3_vec3 b) {
  float d = v3dot(a, b);
  if (d < -0.99999f) { /* opposite: half a turn about any perpendicular */
    t3_vec3 p = fabsf(a.x) < 0.9f ? v3cross(a, (t3_vec3){ 1, 0, 0 }) : v3cross(a, (t3_vec3){ 0, 1, 0 });
    p = v3norm(p);
    return (t3_quat){ p.x, p.y, p.z, 0 };
  }
  t3_vec3 c = v3cross(a, b);
  t3_quat q = { c.x, c.y, c.z, 1 + d };
  float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return (t3_quat){ q.x / l, q.y / l, q.z / l, q.w / l };
}
/* a 2D pose at depth z: the same rotation, about z */
static cw_xform3 lift3(cw_xform2 x, float z) {
  float s, c;
  cw_sincos(cw_atan2(x.q.s, x.q.c) * 0.5f, &s, &c);
  return (cw_xform3){ { x.p.x, x.p.y, z }, { 0, 0, s, c } };
}
static t3_vec3 xf3(cw_xform3 x, t3_vec3 p) { return v3add(x.p, qrot(x.q, p)); }
static float capsule_vol(float len, float r) { return 3.1415927f * r * r * len + 4.18879f * r * r * r; }

/* the rider's pieces: each body's frame is the drawn limb group's (origin at
 * the joint, the limb along +x; the head's +x is the face, +y up), and its
 * shapes sit where models.c draws the limb (left limbs toward +z) */
static const float RAG_MASS[RAG_COUNT] = { 36, 5.5f, 2.4f, 1.8f, 2.4f, 1.8f, 8.5f, 4.2f, 8.5f, 4.2f }; /* kg: 75 in all */
static const float RAG_SIDE_Z[RAG_COUNT] = { 0, 0, 0.2f, 0.22f, -0.2f, -0.22f, 0.1f, 0.12f, -0.1f, -0.12f };

static cw_body3d body3(cw_xform3 x, t3_vec3 v, t3_vec3 w, const char *name, float damping) {
  cw_body3d_def bd = cw_body3d_default_def();
  bd.position = x.p;
  bd.rotation = x.q;
  bd.linear_velocity = v;
  bd.angular_velocity = w;
  bd.angular_damping = damping;
  bd.linear_damping = 0.02f;
  bd.name = name;
  return cw_body3d_create(crash_world(), &bd);
}

/* a ball joint (neck, shoulder, hip): the limb's axis (child-local) may swing
 * up to `cone` from a rest direction (parent-local), twist +-twist about
 * itself. The frames start untwisted, so the twist limit counts from the pose
 * it came off in; a rest direction the limb already lies beyond is moved in
 * just far enough to hold it */
static void ball(cw_body3d a, cw_body3d b, t3_vec3 anchor, t3_vec3 rest, t3_vec3 axis_b, float cone, float twist, float friction) {
  cw_xform3 A = cw_body3d_transform(a), Bx = cw_body3d_transform(b);
  t3_vec3 n = v3norm(qrot(A.q, rest)), d = v3norm(qrot(Bx.q, axis_b));
  float ang = cw_acos(cw_clampf(v3dot(n, d), -1, 1)), room = cone - 0.08f;
  if (ang > room) { /* tip the rest direction toward the limb */
    t3_quat sw = qbetween(n, d);
    float k = (ang - room) / ang, h = cw_acos(cw_clampf(sw.w, -1, 1)) * k, sh, ch;
    t3_vec3 ax = v3norm((t3_vec3){ sw.x, sw.y, sw.z });
    cw_sincos(h, &sh, &ch);
    n = v3norm(qrot((t3_quat){ ax.x * sh, ax.y * sh, ax.z * sh, ch }, n));
  }
  t3_quat fa_w = qbetween((t3_vec3){ 0, 0, 1 }, n);
  t3_quat fb_w = qmul(qbetween(n, d), fa_w);
  cw_joint3d_def jd = cw_joint3d_default_def(CW_JOINT_SPHERICAL);
  jd.a = a;
  jd.b = b;
  jd.frame_a = (cw_xform3){ qrot(qconj(A.q), v3sub(anchor, A.p)), qmul(qconj(A.q), fa_w) };
  jd.frame_b = (cw_xform3){ qrot(qconj(Bx.q), v3sub(anchor, Bx.p)), qmul(qconj(Bx.q), fb_w) };
  jd.limit = true;
  jd.cone_angle = cone;
  jd.lower = -twist;
  jd.upper = twist;
  jd.spring = false;
  jd.motor = friction > 0;
  jd.motor_speed = 0;
  jd.max_motor = friction;
  cw_joint3d_create(crash_world(), &jd);
}
/* a hinge (elbow, knee) about the side axis: its 2D range (child angle less
 * parent angle), counted from the bend it came off in */
static void hinge(cw_body3d a, cw_body3d b, t3_vec3 anchor, float rel, float lo, float hi, float friction) {
  cw_joint3d_def jd = cw_joint3d_default_def(CW_JOINT_REVOLUTE);
  jd.a = a;
  jd.b = b;
  cw_joint3d_def_anchor(&jd, anchor, (t3_vec3){ 0, 0, 1 });
  jd.limit = true;
  jd.lower = fminf(lo, rel) - rel;
  jd.upper = fmaxf(hi, rel) - rel;
  jd.spring = false;
  jd.motor = friction > 0;
  jd.motor_speed = 0;
  jd.max_motor = friction;
  cw_joint3d_create(crash_world(), &jd);
}

/* the bridge planks and the set pieces, mirrored in the crash world as
 * kinematic bodies that follow their 2D selves (made at the first crash) */
static cw_body3d mirror_body(cw_xform2 x, const char *name) {
  cw_body3d_def bd = cw_body3d_default_def();
  bd.type = CW_BODY_KINEMATIC;
  cw_xform3 x3 = lift3(x, 0);
  bd.position = x3.p;
  bd.rotation = x3.q;
  bd.name = name;
  bd.interpolate = false;
  return cw_body3d_create(crash_world(), &bd);
}
static void mirror_make(void) {
  if (WD.mirrored || !cw_world3d_valid(crash_world())) return;
  WD.mirrored = true;
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = L3_SCENE;
  sd.material.friction = 0.85f;
  sd.contact_events = false;
  for (int i = 0; i < WD.plank_count; i++) {
    WD.plank3[i] = mirror_body(cw_body2d_transform(WD.planks[i].body), "plank");
    cw_shape3d_box(WD.plank3[i], &sd, (t3_vec3){ WD.planks[i].half_w, 0.05f, TRACK_HALF_W - 0.1f }, cw_xform3_identity());
  }
  for (int i = 0; i < WD.mover_count; i++) {
    mover *m = &WD.movers[i];
    if (m->kind == MOVER_DRAWBRIDGE) {
      WD.mover3[i] = mirror_body(cw_body2d_transform(m->body), "drawbridge");
      cw_shape3d_box(WD.mover3[i], &sd, (t3_vec3){ m->len * 0.5f, 0.05f, TRACK_HALF_W - 0.15f }, cw_xform3_at(m->len * 0.5f, -0.07f, 0));
    } else if (m->kind == MOVER_SEESAW) {
      WD.mover3[i] = mirror_body(cw_body2d_transform(m->body), "seesaw");
      cw_shape3d_box(WD.mover3[i], &sd, (t3_vec3){ m->len * 0.5f, 0.06f, TRACK_HALF_W - 0.15f }, cw_xform3_identity());
    } else if (m->kind == MOVER_WRECK) {
      WD.mover3[i] = mirror_body(cw_body2d_transform(m->body), "wrecking ball");
      cw_shape3d_sphere(WD.mover3[i], &sd, (t3_vec3){ 0, 0, 0 }, 0.6f);
    }
  }
}
/* each tick of a crash: the mirrors to where their 2D selves are now (a log
 * appears and goes with its 2D body) */
static void mirror_sync(float dt) {
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = L3_SCENE;
  sd.material.friction = 0.8f;
  sd.contact_events = false;
  for (int i = 0; i < WD.plank_count; i++) cw_body3d_set_target(WD.plank3[i], lift3(cw_body2d_transform(WD.planks[i].body), 0), dt);
  for (int i = 0; i < WD.mover_count; i++) {
    mover *m = &WD.movers[i];
    if (cw_body3d_valid(WD.mover3[i])) cw_body3d_set_target(WD.mover3[i], lift3(cw_body2d_transform(m->body), 0), dt);
    if (m->kind != MOVER_LOGS) continue;
    for (int k = 0; k < MAX_LOGS; k++) {
      bool live = cw_body2d_valid(m->logs[k]);
      if (!live && cw_body3d_valid(WD.log3[i][k])) cw_body3d_destroy(WD.log3[i][k]), WD.log3[i][k] = (cw_body3d){ 0 };
      if (!live) continue;
      cw_xform2 x = cw_body2d_transform(m->logs[k]);
      if (!cw_body3d_valid(WD.log3[i][k])) {
        WD.log3[i][k] = mirror_body(x, "log");
        cw_shape3d_capsule(WD.log3[i][k], &sd, (t3_vec3){ 0, 0, -TRACK_HALF_W * 0.7f + 0.12f }, (t3_vec3){ 0, 0, TRACK_HALF_W * 0.7f - 0.12f }, 0.12f);
      }
      cw_body3d_set_target(WD.log3[i][k], lift3(x, 0), dt);
    }
  }
}

static void crash_destroy(void) {
  if (cw_joint3d_valid(B.grip)) cw_joint3d_destroy(B.grip);
  B.grip = (cw_joint3d){ 0 };
  for (int i = 0; i < RAG_COUNT; i++)
    if (cw_body3d_valid(B.rag[i])) cw_body3d_destroy(B.rag[i]);
  cw_body3d *more[4] = { &B.frame3, &B.rear3, &B.front3, &B.cap };
  for (int i = 0; i < 4; i++)
    if (cw_body3d_valid(*more[i])) cw_body3d_destroy(*more[i]);
  memset(B.rag, 0, sizeof B.rag);
  B.frame3 = B.rear3 = B.front3 = B.cap = (cw_body3d){ 0 };
  B.down3 = false;
}

/* a 32-bit hash of the crash's place and rider: which way and how hard it
 * throws sideways (the same on every host) */
static uint32_t crash_seed(cw_vec2 at) {
  int32_t k[3] = { (int32_t)(at.x * 64), (int32_t)(at.y * 64), B.idx };
  return cw_hash32(k, sizeof k, 0x9e3779b9u);
}

static void make_ragdoll(void) {
  if (!cw_world3d_valid(crash_world())) return;
  mirror_make();
  bike_pose p;
  bike_pose_get(1, &p); /* the riding pose: the limbs where they were drawn */
  float z = B.draw_z;
  cw_xform2 fx = cw_body2d_transform(B.frame);
  float w2 = cw_body2d_angular_velocity(B.frame);
  cw_vec2 cv = B.crash_v;
  float speed = sqrtf(cv.x * cv.x + cv.y * cv.y);
  t3_vec3 fwd = speed > 0.5f ? (t3_vec3){ cv.x / speed, cv.y / speed, 0 } : (t3_vec3){ fx.q.c, fx.q.s, 0 };
  uint32_t h = crash_seed(fx.p);
  /* out to the side: mostly back onto the bank, now and then toward the
   * camera; the faster the crash the further */
  float side = ((h & 7) < 6 ? -1.0f : 0.5f) * (0.3f + 0.11f * speed) * (0.7f + 0.3f * (float)((h >> 8) & 255) / 255.0f);
  /* the body's own launch: whatever the hit took out of the bike, the rider
   * goes on along the line of travel (no slower than most of the crash's
   * speed, a hit into the ground turned forward) and up off the ground, and out
   * to the side */
  float lift = cw_clampf(0.15f + 0.32f * speed, 0.2f, 7.0f);
  float along = (cv.x < 0 ? -1.0f : 1.0f) * fmaxf(fabsf(cv.x), 0.75f * speed);
  t3_vec3 launch = { along, fmaxf(cv.y, 0) + lift, side };
  /* the tumble: over the bars (a forward somersault), with a roll toward the side thrown */
  float ahead = cv.x < 0 ? -1.0f : 1.0f; /* which way along the track it was going */
  /* half the turn it had (a flip's spin carries on), plus the throw's
   * somersault over the bars, no faster than 9 rad/s */
  t3_vec3 spin = { (side < 0 ? -1.0f : 1.0f) * ahead * 0.08f * speed, 0,
                   cw_clampf(0.5f * w2 - ahead * cw_clampf(0.3f + 0.3f * speed, 0.3f, 6.5f), -9, 9) };

  /* the rider */
  cw_xform3 X[RAG_COUNT];
  for (int i = 0; i < RAG_COUNT; i++) X[i] = lift3(p.limb[i], z);
  t3_vec3 com = { 0, 0, 0 };
  float mass = 0;
  for (int i = 0; i < RAG_COUNT; i++) com = v3add(com, v3scale(xf3(X[i], (t3_vec3){ i == RAG_HEAD ? 0 : 0.2f, 0, RAG_SIDE_Z[i] }), RAG_MASS[i])), mass += RAG_MASS[i];
  com = v3scale(com, 1 / mass);
  float torso_len = sqrtf((p.limb[RAG_UARM_L].p.x - p.limb[RAG_TORSO].p.x) * (p.limb[RAG_UARM_L].p.x - p.limb[RAG_TORSO].p.x) +
                          (p.limb[RAG_UARM_L].p.y - p.limb[RAG_TORSO].p.y) * (p.limb[RAG_UARM_L].p.y - p.limb[RAG_TORSO].p.y));
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.group = -1 - B.idx;
  sd.material.friction = 0.5f;
  sd.material.restitution = 0.25f;
  sd.material.rolling_resistance = 0.02f;
  sd.hit_events = true;
  sd.contact_events = false;
  for (int i = 0; i < RAG_COUNT; i++) {
    /* each piece goes with the launch, turning about the body's centre of mass */
    t3_vec3 at = xf3(X[i], (t3_vec3){ 0, 0, RAG_SIDE_Z[i] });
    t3_vec3 v = v3add(launch, v3cross(spin, v3sub(at, com)));
    B.rag[i] = body3(X[i], v, spin, "limb", 0.08f);
    float zs = RAG_SIDE_Z[i];
    bool arm = i == RAG_UARM_L || i == RAG_UARM_R || i == RAG_FARM_L || i == RAG_FARM_R;
    /* the arms never meet the bike they held (the hands start on its bars); the rest do */
    sd.layer = arm ? L3_HAND : L3_RIDER;
    sd.mask = 1u << L3_TOY | 1u << L3_SCENE | 1u << L3_RIDER | 1u << L3_HAND | (arm ? 0 : 1u << L3_BIKE);
    float vol;
    if (i == RAG_HEAD) {
      vol = 4.18879f * 0.12f * 0.12f * 0.12f;
      sd.density = RAG_MASS[i] / vol;
      cw_shape3d_sphere(B.rag[i], &sd, (t3_vec3){ 0.01f, -0.01f, 0 }, 0.12f);
    } else if (i == RAG_TORSO) {
      float a = 0.14f, c = fmaxf(a + 0.05f, torso_len - 0.04f);
      sd.density = RAG_MASS[i] / capsule_vol(c - a, 0.15f);
      cw_shape3d_capsule(B.rag[i], &sd, (t3_vec3){ a, 0, 0 }, (t3_vec3){ c, 0, 0 }, 0.15f);
    } else if (i == RAG_SHIN_L || i == RAG_SHIN_R) { /* the shin and the foot */
      vol = capsule_vol(0.38f, 0.055f) + capsule_vol(0.16f, 0.045f);
      sd.density = RAG_MASS[i] / vol;
      cw_shape3d_capsule(B.rag[i], &sd, (t3_vec3){ 0.05f, 0, zs }, (t3_vec3){ 0.43f, 0, zs }, 0.055f);
      cw_shape3d_capsule(B.rag[i], &sd, (t3_vec3){ 0.48f, -0.05f, zs }, (t3_vec3){ 0.48f, 0.11f, zs }, 0.045f);
    } else {
      bool thigh = i == RAG_THIGH_L || i == RAG_THIGH_R, upper = i == RAG_UARM_L || i == RAG_UARM_R;
      float len = thigh ? 0.36f : upper ? 0.24f : 0.27f, r = thigh ? 0.075f : upper ? 0.05f : 0.045f, a = thigh ? 0.04f : 0.03f;
      sd.density = RAG_MASS[i] / capsule_vol(len, r);
      cw_shape3d_capsule(B.rag[i], &sd, (t3_vec3){ a, 0, zs }, (t3_vec3){ a + len, 0, zs }, r);
    }
  }
  /* the joints: torso-local x runs up the spine, -y is the chest's side, +z the left */
  for (int s2 = 0; s2 < 2; s2++) {
    float zs = s2 ? -1.0f : 1.0f;
    int ua = s2 ? RAG_UARM_R : RAG_UARM_L, fa = s2 ? RAG_FARM_R : RAG_FARM_L, th = s2 ? RAG_THIGH_R : RAG_THIGH_L, sn = s2 ? RAG_SHIN_R : RAG_SHIN_L;
    ball(B.rag[RAG_TORSO], B.rag[ua], xf3(X[ua], (t3_vec3){ 0, 0, RAG_SIDE_Z[ua] }), (t3_vec3){ -0.65f, -0.62f, 0.4f * zs }, (t3_vec3){ 1, 0, 0 },
         1.7f, 1.1f, 0.4f);
    hinge(B.rag[ua], B.rag[fa], xf3(X[fa], (t3_vec3){ 0, 0, RAG_SIDE_Z[fa] }),
          cw_wrap_angle(cw_atan2(p.limb[fa].q.s, p.limb[fa].q.c) - cw_atan2(p.limb[ua].q.s, p.limb[ua].q.c)), 0.0f, 2.5f, 0.2f);
    ball(B.rag[RAG_TORSO], B.rag[th], xf3(X[th], (t3_vec3){ 0, 0, RAG_SIDE_Z[th] }), (t3_vec3){ -0.5f, -0.85f, 0.14f * zs }, (t3_vec3){ 1, 0, 0 },
         1.35f, 0.5f, 1.2f);
    hinge(B.rag[th], B.rag[sn], xf3(X[sn], (t3_vec3){ 0, 0, RAG_SIDE_Z[sn] }),
          cw_wrap_angle(cw_atan2(p.limb[sn].q.s, p.limb[sn].q.c) - cw_atan2(p.limb[th].q.s, p.limb[th].q.c)), -2.5f, 0.0f, 0.6f);
  }
  ball(B.rag[RAG_TORSO], B.rag[RAG_HEAD], xf3(X[RAG_TORSO], (t3_vec3){ torso_len + 0.08f, 0, 0 }), (t3_vec3){ 1, 0, 0 }, (t3_vec3){ 0, 1, 0 }, 0.9f,
       0.7f, 0.25f);

  /* the bike: the frame and its wheels on axles, the frame's 2D outline given
   * depth, the stem and the bars across; tumbling over toward the far side */
  float roll = (side < 0 ? -1.0f : 1.0f) * (1.2f + 0.12f * speed);
  t3_vec3 bspin = v3add((t3_vec3){ 0, 0, w2 }, v3scale(fwd, roll));
  cw_vec2 fv = cw_body2d_velocity(B.frame);
  cw_xform3 F3 = lift3(fx, z);
  B.frame3 = body3(F3, (t3_vec3){ fv.x, fv.y, side * 0.25f }, bspin, "bike", 0.05f);
  cw_shape3d_def bs = cw_shape3d_default_def();
  bs.layer = L3_BIKE;
  bs.mask = 1u << L3_TOY | 1u << L3_SCENE | 1u << L3_RIDER | 1u << L3_BIKE;
  bs.group = -1 - MAX_RIDERS - B.idx;
  bs.material.friction = 0.5f;
  bs.material.restitution = 0.2f;
  bs.hit_events = true;
  bs.contact_events = false;
  bs.density = 330;
  static const cw_vec2 HULL[] = { { -0.42f, 0.02f }, { 0.0f, -0.02f }, { 0.52f, 0.02f }, { 0.46f, 0.62f }, { -0.2f, 0.58f } };
  t3_vec3 hp[10];
  for (int k = 0; k < 5; k++) hp[k * 2] = (t3_vec3){ HULL[k].x, HULL[k].y, -0.025f }, hp[k * 2 + 1] = (t3_vec3){ HULL[k].x, HULL[k].y, 0.025f };
  cw_shape3d_hull(B.frame3, &bs, hp, 10);
  bs.density = 1500;
  cw_shape3d_capsule(B.frame3, &bs, (t3_vec3){ 0.47f, 0.3f, 0 }, (t3_vec3){ 0.42f, 0.98f, 0 }, 0.025f);  /* head tube and stem */
  cw_shape3d_capsule(B.frame3, &bs, (t3_vec3){ 0.42f, 1.0f, -0.28f }, (t3_vec3){ 0.42f, 1.0f, 0.28f }, 0.022f); /* the bars */
  cw_shape3d_capsule(B.frame3, &bs, (t3_vec3){ -0.1f, 0.4f, 0 }, (t3_vec3){ -0.2f, 0.74f, 0 }, 0.02f);     /* the seat post */
  cw_body2d wb[2] = { B.rear, B.front };
  cw_body3d *w3[2] = { &B.rear3, &B.front3 };
  t3_vec3 rim[32];
  for (int k = 0; k < 16; k++) {
    float sa, ca;
    cw_sincos(6.2831853f * k / 16, &sa, &ca);
    rim[k * 2] = (t3_vec3){ WHEEL_R * ca, WHEEL_R * sa, -0.022f }, rim[k * 2 + 1] = (t3_vec3){ WHEEL_R * ca, WHEEL_R * sa, 0.022f };
  }
  for (int k = 0; k < 2; k++) {
    cw_xform2 wx = cw_body2d_transform(wb[k]);
    cw_vec2 wv = cw_body2d_velocity(wb[k]);
    *w3[k] = body3(lift3(wx, z), (t3_vec3){ wv.x, wv.y, side * 0.25f }, v3add((t3_vec3){ 0, 0, cw_body2d_angular_velocity(wb[k]) }, v3scale(fwd, roll)),
                   "wheel", 0.05f);
    bs.density = 650;
    bs.material.friction = 0.9f;
    cw_shape3d_hull(*w3[k], &bs, rim, 32);
    cw_joint3d_def jd = cw_joint3d_default_def(CW_JOINT_REVOLUTE);
    jd.a = B.frame3;
    jd.b = *w3[k];
    cw_joint3d_def_anchor(&jd, (t3_vec3){ wx.p.x, wx.p.y, z }, (t3_vec3){ 0, 0, 1 });
    jd.spring = false;
    jd.motor = true;
    jd.max_motor = 0.15f; /* a little drag in the hub */
    cw_joint3d_create(crash_world(), &jd);
  }

  /* one hand keeps its grip on the bars a moment, dragged by the tumbling
   * bike till it is torn free (the harder the hit, the sooner); a no-hands
   * pose has both hands off already */
  bool hands_on = !(B.pose_kind == POSE_NO_HANDS && B.pose_amt > 0.5f);
  if (hands_on) {
    cw_joint3d_def jd = cw_joint3d_default_def(CW_JOINT_SPHERICAL);
    jd.a = B.frame3;
    jd.b = B.rag[RAG_FARM_R];
    cw_joint3d_def_anchor(&jd, xf3(X[RAG_FARM_R], (t3_vec3){ 0.3f, 0, RAG_SIDE_Z[RAG_FARM_R] }), (t3_vec3){ 0, 0, 1 });
    jd.spring = false;
    B.grip = cw_joint3d_create(crash_world(), &jd);
    B.grip_ticks = (int)cw_clampf(16 - speed * 1.2f, 3, 14);
  }
  /* the cap flies off the head and tumbles on its own */
  {
    cw_xform3 cx = lift3(p.cap, z);
    t3_vec3 at = cx.p;
    t3_vec3 v = { launch.x * 0.9f, launch.y + 3.0f, side * 1.3f };
    v = v3add(v, v3cross(spin, v3sub(at, com)));
    B.cap = body3(cx, v, (t3_vec3){ side * 4, 3.0f, w2 - 9 }, "cap", 0.6f);
    t3_vec3 shell[17];
    int n = 0;
    for (int k = 0; k < 8; k++) {
      float sa, ca;
      cw_sincos(6.2831853f * k / 8, &sa, &ca);
      shell[n++] = (t3_vec3){ 0.115f * ca, -0.02f, 0.115f * sa };
    }
    for (int k = 0; k < 6; k++) {
      float sa, ca;
      cw_sincos(6.2831853f * k / 6, &sa, &ca);
      shell[n++] = (t3_vec3){ 0.085f * ca, 0.05f, 0.085f * sa };
    }
    shell[n++] = (t3_vec3){ 0, 0.09f, 0 };
    shell[n++] = (t3_vec3){ -0.24f, -0.04f, 0.07f };
    shell[n++] = (t3_vec3){ -0.24f, -0.04f, -0.07f };
    cw_shape3d_def cs = cw_shape3d_default_def();
    cs.layer = L3_HAND;
    cs.mask = 1u << L3_SCENE;
    cs.density = 40;
    cs.material.friction = 0.6f;
    cs.material.restitution = 0.3f;
    cs.contact_events = false;
    cw_shape3d_hull(B.cap, &cs, shell, n);
  }
  /* the 2D bike goes: the crash is the 3D world's now */
  if (cw_body2d_valid(B.frame)) cw_body2d_destroy(B.frame);
  if (cw_body2d_valid(B.rear)) cw_body2d_destroy(B.rear);
  if (cw_body2d_valid(B.front)) cw_body2d_destroy(B.front);
  B.frame = B.rear = B.front = (cw_body2d){ 0 };
  B.torso = B.head = (cw_shape2d){ 0 };
  B.frame_sh = B.rear_sh = B.front_sh = (cw_shape2d){ 0 };
  B.down3 = true;
}

/* A celebratory tumble after timing is locked, not a scored wipeout. */
void bike_finish_tumble(void) {
  if(!B.st.alive||B.down3)return;
  B.crash_v=cw_body2d_velocity(B.frame);
  B.crash_v.x=cw_clampf(B.crash_v.x,5,8);B.crash_v.y=1.4f;
  if(T&&!strcmp(T->theme,"toybox")){B.crash_v.x=6.4f;B.crash_v.y=9;}
  B.st.alive=false;make_ragdoll();
  if(T&&!strcmp(T->theme,"toybox")) {
    /* A springboard launches the whole bike/rider assembly. Ordinary crash
     * ejection adds lift and side throw, which would overshoot this pit. */
    t3_vec3 v={6,8.5f,0};
    for(int i=0;i<RAG_COUNT;i++)cw_body3d_set_velocity(B.rag[i],v);
    cw_body3d_set_velocity(B.frame3,v);cw_body3d_set_velocity(B.rear3,v);
    cw_body3d_set_velocity(B.front3,v);cw_body3d_set_velocity(B.cap,v);
  }
}

/* a tick of a crash after the world stepped: the grip, and what the game reads */
static void crash_post(void) {
  if (cw_joint3d_valid(B.grip) && --B.grip_ticks <= 0) { /* the hand on the bars torn free */
    cw_joint3d_destroy(B.grip);
    B.grip = (cw_joint3d){ 0 };
  }
  bike_state *s = &B.st;
  t3_vec3 c = cw_body3d_transform(B.rag[RAG_TORSO]).p, v = cw_body3d_velocity(B.rag[RAG_TORSO]);
  s->alive = false;
  s->grounded_rear = s->grounded_front = false;
  s->air_ticks = s->wheelie_ticks = 0;
  s->speed = 0;
  s->x = c.x;
  s->y = c.y;
  s->vx = v.x;
  s->vy = v.y;
}

void bike_set_z(float z) { B.draw_z = z; }
bool bike_crash_focus(t3_vec3 *at, t3_vec3 *vel) {
  if (!B.down3 || !cw_body3d_valid(B.rag[RAG_TORSO])) return false;
  if (at) *at = cw_body3d_transform(B.rag[RAG_TORSO]).p;
  if (vel) *vel = cw_body3d_velocity(B.rag[RAG_TORSO]);
  return true;
}

static bool grounded(cw_body2d w) {
  cw_xform2 f = cw_body2d_transform(B.frame);
  /* down along the frame (so a wheelie's rear and a nose-dive's front still count) and straight down */
  cw_vec2 d1 = { f.q.s * (WHEEL_R + 0.08f), -f.q.c * (WHEEL_R + 0.08f) }, d2 = { 0, -(WHEEL_R + 0.08f) };
  cw_phys_filter flt = { .layers = 1u << L_TRACK | 1u << B.loop_layer | 1u << (L_RAMP0 + B.lane) };
  cw_ray_hit2d hit;
  cw_pos2 origin = cw_body2d_position(w);
  return cw_world2d_raycast(WD.world, origin, d1, &flt, &hit) || cw_world2d_raycast(WD.world, origin, d2, &flt, &hit);
}


/* ── moving set pieces: a function of the world's tick, so every host
 * moves them alike ── */
static void movers_tick(void) {
  float t = (float)WD.tick / TICK_HZ, dt = 1.0f / TICK_HZ;
  WD.tick++;
  for (int i = 0; i < WD.mover_count; i++) {
    mover *m = &WD.movers[i];
    if (m->kind == MOVER_DRAWBRIDGE) {
      /* the deck lifts its far end up to 0.32 rad and back every 5 s: a
       * ramp when it is up, a bridge when it is down */
      float a = 0.16f * (1 - cw_cos(6.2831853f * (t + dt) / 5.0f));
      cw_body2d_set_target(m->body, (cw_xform2){ { m->x, m->y }, cw_rot2_from_angle(a) }, dt);
    } else if (m->kind == MOVER_WRECK) {
      /* a pendulum from 9.2 m up; the bottom of its swing clears the
       * rider's head, so only a jump meets it */
      float a = 1.0f * cw_sin(6.2831853f * (t + dt) / 4.0f), sa, ca;
      cw_sincos(a, &sa, &ca);
      cw_body2d_set_target(m->body, cw_xform2_at(m->x + 6.2f * sa, m->y + 9.2f - 6.2f * ca), dt);
    } else if (m->kind == MOVER_LOGS) {
      /* a log sets off down the slope every 2.5 s */
      if (WD.tick >= m->next_log) {
        m->next_log = WD.tick + 150;
        int k = m->log_n % MAX_LOGS;
        if (cw_body2d_valid(m->logs[k])) cw_body2d_destroy(m->logs[k]);
        cw_body2d_def lb = cw_body2d_default_def();
        lb.position = (cw_pos2){ m->x + 1.5f, m->y + 0.3f };
        lb.linear_velocity = (cw_vec2){ 1.5f, 0 };
        lb.name = "log";
        m->logs[k] = cw_body2d_create(WD.world, &lb);
        cw_shape2d_def ls = cw_shape2d_default_def();
        ls.layer = L_TRACK;
        ls.density = 3;
        ls.material.friction = 0.8f;
        cw_shape2d_circle(m->logs[k], &ls, (cw_vec2){ 0, 0 }, 0.12f);
        m->log_n++;
      }
      for (int k = 0; k < MAX_LOGS; k++)
        if (cw_body2d_valid(m->logs[k]) && cw_body2d_transform(m->logs[k]).p.y < WD.kill_y) {
          cw_body2d_destroy(m->logs[k]);
          m->logs[k] = (cw_body2d){ 0 };
        }
    }
  }
}

/* Deer run across depth, not along the 2D riding line. Use the same fixed
 * tick phase for collision and rendering so every multiplayer view agrees. */
int bike_deer_poses(float alpha,t3_vec3 out[DEER_CROSSINGS]) {
  if(!T || strcmp(T->theme,"jungle"))return 0;
  int active=0;
  for(int i=0;i<DEER_CROSSINGS;i++) {
    if(WD.deer_start[i]<0)continue;
    float time=((float)(WD.tick-WD.deer_start[i]-1)+alpha)/TICK_HZ;
    if(time<0)continue;
    float phase=time;
    if(phase>=3.0f)continue;
    float run=phase/3.0f;
    out[i]=(t3_vec3){DEER_X[i],track_height_at(DEER_X[i],NULL),-7.0f+11.0f*run};
    active|=1<<i;
  }
  return active;
}

static void deer_tick(void) {
  if(!T||strcmp(T->theme,"jungle"))return;
  for(int i=0;i<DEER_CROSSINGS;i++) {
    if(WD.deer_start[i]>=0)continue;
    for(int r=0;r<NB;r++) {
      bike_state *s=&BK[r].st;
      float lead=cw_clampf(fabsf(s->vx)*1.8f,16.0f,32.0f);
      if(!s->alive||s->x<DEER_X[i]-lead||s->x>DEER_X[i]+1)continue;
      WD.deer_start[i]=WD.tick;break;
    }
  }
}

int bike_movers(float alpha, mover_pose *out, int cap) {
  int n = 0;
  float t = ((float)WD.tick - 1 + alpha) / TICK_HZ;
  for (int i = 0; i < WD.mover_count && n < cap; i++) {
    mover *m = &WD.movers[i];
    if (m->kind == MOVER_LOGS) {
      for (int k = 0; k < MAX_LOGS && n < cap; k++)
        if (cw_body2d_valid(m->logs[k])) out[n++] = (mover_pose){ MOVER_LOGS, cw_body2d_interpolated(m->logs[k], alpha), 0.12f, 0 };
      continue;
    }
    if (m->kind == MOVER_TRAIN) { /* a train crossing under the tabletop, along z (drawn only) */
      float z = -60 + cw_fmodf_pos(t * 18, 140);
      out[n++] = (mover_pose){ MOVER_TRAIN, cw_xform2_at(m->x, m->y - 3.2f), z, m->len };
      continue;
    }
    if (m->kind == MOVER_WRECK) out[n++] = (mover_pose){ m->kind, cw_body2d_interpolated(m->body, alpha), m->x, m->y + 9.2f };
    else out[n++] = (mover_pose){ m->kind, cw_body2d_interpolated(m->body, alpha), m->len, 0 };
  }
  return n;
}

/* the frame's pitch against the ground under its wheels (+ = nose up) */
static float pitch_err(void) {
  cw_xform2 fx = cw_body2d_transform(B.frame);
  cw_vec2 ra = xpt(fx, REAR_AXLE), fa = xpt(fx, FRONT_AXLE);
  bool f1, f2;
  float y1 = ground_at(fa.x, &f1), y0 = ground_at(ra.x, &f2);
  if (!f1 || !f2 || fa.x - ra.x < 0.1f) return 0;
  return cw_wrap_angle(cw_rot2_angle(fx.q) - cw_atan2(y1 - y0, fa.x - ra.x));
}

/* one half step's controls: motors, lean, assists and the boost (Box2D
 * clears forces after every step, so they go in again each half step) */
float chocolate_amount(const track_desc *t,float x) {
  if(strcmp(t->theme,"candy"))return 0;
  float d=fminf(fabsf(x-t->finish_x*.34f),fabsf(x-t->finish_x*.73f));
  return 1-cw_smoothstep(1.8f,3.f,d);
}
static void controls(const bike_input *in, bool boosting, bool air) {
  bike_state *s = &B.st;
  if (s->alive) {
    /* drive: the pedals spin the rear wheel toward a top speed */
    float pedal = cw_clampf(in->pedal, 0, 1), brake = cw_clampf(in->brake, 0, 1);
    if (in->hold) {
      cw_joint2d_set_motor(B.j_rear, true, 0, 40);
      cw_joint2d_set_motor(B.j_front, true, 0, 40);
    } else if (brake > 0.05f && s->speed < 0.6f && !air) {
      /* stopped and still holding brake: roll back slowly */
      cw_joint2d_set_motor(B.j_rear, true, 10 * brake, 6);
      cw_joint2d_set_motor(B.j_front, false, 0, 0);
    } else if (brake > 0.05f) {
      cw_joint2d_set_motor(B.j_rear, true, 0, 9.4f * brake);
      cw_joint2d_set_motor(B.j_front, true, 0, 6.5f * brake);
    } else if (pedal > 0.05f) {
      /* a freewheel: the pedals only ever drive; a wheel already spinning
       * faster than the legs can turn it just rolls */
      float spin = -(cw_body2d_angular_velocity(B.rear) - cw_body2d_angular_velocity(B.frame));
      float target = TOP_SPIN * pedal * (boosting ? 1.25f : 1);
      /* the rider eases off as the front lifts (traction behind and below the
       * rider's weight pulls the nose up), unless leaning back for a wheelie */
      float ease = in->lean < -0.1f || air || B.loop_zone ? 1 : cw_clampf(1 - (pitch_err() - 0.1f) * 3, 0.15f, 1);
      if (spin < target) cw_joint2d_set_motor(B.j_rear, true, -target, (boosting ? 23.5f : 16.5f) * ease);
      else cw_joint2d_set_motor(B.j_rear, true, -spin, 0.05f);
      cw_joint2d_set_motor(B.j_front, true, 0, 0.0f);
    } else {
      cw_joint2d_set_motor(B.j_rear, false, 0, 0); /* coasting: free wheels */
      cw_joint2d_set_motor(B.j_front, false, 0, 0);
    }
    /* lean: forward (+) noses down = clockwise = negative torque */
    float lean = cw_clampf(in->lean, -1, 1);
    float torque = (air ? 48.0f : 52.0f) * -lean;
    /* in the air the spin builds toward AIR_SPIN_MAX and no further: full
     * torque to two thirds of it, then less and less (the same on every
     * track: low gravity gives more air time, not a faster spin) */
    if (air) {
      float w = cw_body2d_angular_velocity(B.frame);
      if (torque * w > 0) torque *= cw_clampf((AIR_SPIN_MAX - fabsf(w)) / (AIR_SPIN_MAX * 0.35f), 0, 1);
    }
    cw_body2d_apply_torque(B.frame, torque);
    /* hands off on the ground: past a deadband, the frame is eased back
     * toward the slope under it (no accidental loop-outs on a climb; leaning
     * back still pulls a wheelie) */
    if (!air && fabsf(lean) < 0.1f && !B.loop_zone) {
      cw_xform2 fx = cw_body2d_transform(B.frame);
      cw_vec2 ra = xpt(fx, REAR_AXLE), fa = xpt(fx, FRONT_AXLE);
      bool f1, f2;
      float y1 = ground_at(fa.x, &f1), y0 = ground_at(ra.x, &f2);
      if (f1 && f2) {
        float slope = cw_atan2(y1 - y0, fa.x - ra.x);
        float err = cw_wrap_angle(cw_rot2_angle(fx.q) - slope);
        float over = err > 0.12f ? err - 0.12f : err < -0.18f ? err + 0.18f : 0;
        float w = cw_body2d_angular_velocity(B.frame);
        cw_body2d_apply_torque(B.frame, -over * 260 - w * (over != 0 ? 14.0f : 2.5f));

      }
    }
    /* the pedals' kick lifts the front: a rider leans into it; cancel most of
     * it unless they lean back (a wheelie) or the nose is already down */
    if (!air && pedal > 0.05f && lean > -0.1f && !B.loop_zone) {
      cw_xform2 fx = cw_body2d_transform(B.frame);
      cw_vec2 ra = xpt(fx, REAR_AXLE), fa = xpt(fx, FRONT_AXLE);
      bool f1, f2;
      float y1 = ground_at(fa.x, &f1), y0 = ground_at(ra.x, &f2);
      float err = f1 && f2 ? cw_wrap_angle(cw_rot2_angle(fx.q) - cw_atan2(y1 - y0, fa.x - ra.x)) : 0;
      /* the motor's torque falls off toward its top speed; so does its kick */
      float spin = -(cw_body2d_angular_velocity(B.rear) - cw_body2d_angular_velocity(B.frame));
      float load = cw_clampf(1 - spin / (TOP_SPIN * pedal), 0, 1);
      if (err > -0.1f) cw_body2d_apply_torque(B.frame, -20 * pedal * load);
    }
    /* hands off in the air: the bike eases its nose along the flight path
     * (so a plain jump lands), unless it is already flipping */
    if (air && fabsf(lean) >= 0.1f) B.air_leaned = true;
    if (!air) B.air_leaned = false;
    if (air && fabsf(lean) < 0.1f && (!B.air_leaned || fabsf(s->air_rot) < 1.2f)) {
      cw_xform2 fx = cw_body2d_transform(B.frame);
      cw_vec2 v = cw_body2d_velocity(B.frame);
      float target = cw_clampf(cw_atan2(v.y, cw_clampf(v.x, 0.5f, 100)) * 0.5f, -0.4f, 0.35f);
      float err = cw_wrap_angle(target - cw_rot2_angle(fx.q));
      float w = cw_body2d_angular_velocity(B.frame);
      cw_body2d_apply_torque(B.frame, err * 40 - w * 10.0f);
    } else if (air && fabsf(lean) < 0.1f) {
      /* let go mid-flip: the spin slows and, once the wheels come round, the
       * bike settles toward level to stick the landing */
      cw_xform2 fx = cw_body2d_transform(B.frame);
      float w = cw_body2d_angular_velocity(B.frame);
      float a = cw_wrap_angle(cw_rot2_angle(fx.q));
      float settle = fabsf(a) < 1.2f ? -a * 12 : 0;
      cw_body2d_apply_torque(B.frame, settle - w * 4.5f);
    }
    /* air drag on the whole bike (k v |v|): downhill and boost speeds level
     * off in the low 20s m/s, where the tracks' jumps are shaped */
    {
      cw_body2d parts[3] = { B.frame, B.rear, B.front };
      for (int i = 0; i < 3; i++) {
        cw_vec2 v = cw_body2d_velocity(parts[i]);
        float sp = sqrtf(v.x * v.x + v.y * v.y), k = 0.012f * T->drag * cw_body2d_mass(parts[i]);
        if(!air) {
          float sticky=chocolate_amount(T,s->x)*1.8f*cw_body2d_mass(parts[i]);
          cw_body2d_apply_force(parts[i],(cw_vec2){-sticky*v.x,-sticky*v.y},NULL);
        }
        cw_body2d_apply_force(parts[i], (cw_vec2){ -k * v.x * sp, -k * v.y * sp }, NULL);
      }
    }
    if (boosting && !air) {
      cw_xform2 f = cw_body2d_transform(B.frame);
      /* every body pushed in proportion to its mass: the whole bike speeds up
       * as one (a push on the frame alone pitched it over the front wheel) */
      cw_body2d parts[3] = { B.frame, B.rear, B.front };
      float total = 0;
      for (int i = 0; i < 3; i++) total += cw_body2d_mass(parts[i]);
      float assist = 1;
      /* Large powered loops need sustained thrust, not unlimited entry speed:
       * excessive centrifugal load bottoms out the bike's suspension. */
      for (int l = 0; l < T->loop_count; l++) {
        float cx = T->loops[l*3], r = T->loops[l*3+2];
        if (r >= 10 && f.p.x > cx-r-3 && f.p.x < cx+r+3) {
          cw_vec2 v = cw_body2d_velocity(B.frame);
          float speed = v.x*f.q.c + v.y*f.q.s;
          assist = cw_clampf((22-speed)*0.5f, 0, 1);
        }
      }
      for (int i = 0; i < 3; i++) {
        float k = 260 * assist * cw_body2d_mass(parts[i]) / total;
        cw_body2d_apply_force(parts[i], (cw_vec2){ f.q.c * k, f.q.s * k }, NULL);
      }
    }
    /* the crank turns with the rear wheel while pedalling (coasting holds it) */
    float wspin = cw_body2d_angular_velocity(B.rear) - cw_body2d_angular_velocity(B.frame);
    /* pedalling turns them with the wheel, on the ground and in the air (the
     * motor spins the back wheel up there too); back-pedalling, stopped on the
     * ground, turns them backward; braking in the air turns them backward for
     * show (the wheel freewheels) */
    bool backpedal = brake > 0.05f && s->speed < 0.6f && !in->hold && !air;
    if (pedal > 0.05f || backpedal) B.crank += wspin * GEAR / (SUBSTEPS * TICK_HZ); /* forward: clockwise, as the wheel */
    else if (air && brake > 0.05f) B.crank += TOP_SPIN * 0.5f * brake * GEAR / (SUBSTEPS * TICK_HZ);
    s->lean_shown += (lean - s->lean_shown) * (0.2f / SUBSTEPS);
  }

}

/* which rider's bike (or rider) a shape is, or -1 */
static int rider_of(cw_shape2d sh) {
  if (!sh.id) return -1;
  for (int j = 0; j < NB; j++) {
    const bk *b = &BK[j];
    if (sh.id == b->frame_sh.id || sh.id == b->rear_sh.id || sh.id == b->front_sh.id || sh.id == b->torso.id || sh.id == b->head.id) return j;
  }
  return -1;
}

/* one half step's events: a rider's torso or head touching anything is their crash */
static void events(void) {
  int n = 0;
  const cw_phys_event2d *ev = cw_world2d_events(WD.world, &n);
  bk *keep = cur;
  for (int i = 0; i < n; i++) {
    const cw_phys_event2d *e = &ev[i];
    if (e->type == CW_PHYS_CONTACT_BEGIN) {
      if (NB > 1) { /* two riders' bikes meeting (same lane) */
        int ra = rider_of(e->shape_a), rb = rider_of(e->shape_b);
        if (ra >= 0 && rb >= 0 && ra != rb) WD.bumps++;
      }
      for (int j = 0; j < NB; j++) {
        cur = &BK[j];
        if (!B.st.alive || !cw_body2d_valid(B.frame)) continue;
        bool on_a = e->shape_a.id && (e->shape_a.id == B.torso.id || e->shape_a.id == B.head.id);
        bool rider = on_a || (e->shape_b.id && (e->shape_b.id == B.torso.id || e->shape_b.id == B.head.id));
        if (rider && NB > 1) { /* touched by another rider: only a hard hit counts */
          int o = rider_of(on_a ? e->shape_b : e->shape_a);
          if (o >= 0 && o != j) {
            cw_vec2 d = { B.v_pre.x - BK[o].v_pre.x, B.v_pre.y - BK[o].v_pre.y };
            if (d.x * d.x + d.y * d.y < BUMP_CRASH_V * BUMP_CRASH_V) rider = false;
          }
        }
        if (rider) {
          B.st.alive = false;
          B.crashed_now = true;
          B.crash_v = B.v_pre; /* the rider carries on as the bike was going before the hit */
        }
      }
    }
  }
  cur = keep;
}

/* one rider's part of the tick before the world steps: poses, the hop, the loops' layers */
static void bike_pre(const bike_input *in, bool *air_out) {
  B.crashed_now = false;
  B.landed_now = 0;
  bike_state *s = &B.st;
  bool air = !s->grounded_rear && !s->grounded_front;
  *air_out = air;
  B.bailed_now = false;
  /* poses, in the air only: in over 6 ticks, back over 8 once let go */
  int want = air && s->alive ? in->pose : POSE_NONE;
  if (want && (want == B.pose_kind || B.pose_amt == 0)) {
    B.pose_kind = want;
    B.pose_amt = fminf(1, B.pose_amt + 1.0f / 6);
  } else B.pose_amt = fmaxf(0, B.pose_amt - 1.0f / 8);
  if (B.pose_amt == 0) B.pose_kind = POSE_NONE;
  if (air && B.pose_amt >= 0.99f) B.pose_ticks[B.pose_kind]++;
  /* SUBSTEPS Box2D steps a tick: Box2D turns a body at most a quarter turn
   * per step, which at 60 Hz capped a small wheel near 9 m/s (it skidded and
   * pitched the bike over the bars); at 180 Hz a 16-inch wheel rolls to 28 m/s */
  /* the hop: both wheels down, a pull up on the bars (rear first, then the
   * front a beat later); leaning as you hop pre-rotates for tricks */
  if (B.hop_cool > 0) B.hop_cool--;
  /* a press is kept 8 ticks, so one a moment before the wheels are both down still hops */
  if (in->hop) B.hop_buf = 8;
  else if (B.hop_buf > 0) B.hop_buf--;
  if (s->alive && B.hop_buf > 0 && B.hop_cool == 0 && s->grounded_rear && s->grounded_front) {
    B.hop_buf = 0;
    float up = WD.hop_v * (cw_body2d_mass(B.frame) + cw_body2d_mass(B.rear) * 0.5f);
    cw_body2d_apply_impulse(B.frame, (cw_vec2){ 0, up }, NULL);
    cw_body2d_apply_impulse(B.rear, (cw_vec2){ 0, WD.hop_v * cw_body2d_mass(B.rear) }, NULL);
    B.hop_phase = 3;
    B.hop_cool = 30;
    s->hops++;
  }
  if (B.hop_phase > 0 && --B.hop_phase == 0 && cw_body2d_valid(B.front))
    cw_body2d_apply_impulse(B.front, (cw_vec2){ 0, WD.hop_v * cw_body2d_mass(B.front) }, NULL);
  /* the loops: past the top, swap to the descent's layer; out either side, back */
  {
    cw_vec2 fp = cw_body2d_transform(B.frame).p;
    bool b_on = false;
    B.loop_zone = false;
    for (int i = 0; i < T->loop_count && i < MAX_LOOPS; i++) {
      float cx = T->loops[i * 3], r = T->loops[i * 3 + 2], cy = T->loops[i * 3 + 1] + r;
      if (B.loop_b[i]) {
        if (fp.x > cx + r + 1 || fp.x < cx - r - 2) {
          if (fp.x > cx + r + 1 && s->alive) { /* round: out on the front lanes, two along from the way in */
            s->loops++;
            B.lane = CW_MIN(B.lane + 2, LANES - 1);
            B.loop_t = 0;
            bike_relayer();
          }
          B.loop_b[i] = false;
        }
      } else if (s->alive && fp.x < cx && fp.x > cx - r - 1 && fp.y > cy) B.loop_b[i] = true;
      /* how far round (the drawn rider shifts across with the loop): the climb up the right, then the left back down */
      if (s->alive && fp.x > cx - r - 1 && fp.x < cx + r + 1) {
        float a = cw_atan2(fp.y - cy, fp.x - cx) + 1.5707963f; /* 0 at the bottom, round anticlockwise */
        if (a < 0) a += 6.2831853f;
        if (B.loop_b[i]) B.loop_t = fp.x >= cx && fp.y < cy ? 1 : cw_clampf(a / 6.2831853f, 0.5f, 1);
        else B.loop_t = fp.x > cx && B.loop_layer == L_LOOP_A && fp.y > cy - r + 0.5f ? cw_clampf(a / 6.2831853f, 0, 0.5f) : 0;
      } else if (s->alive && !B.loop_b[i] && fp.x <= cx - r - 1) B.loop_t = 0;
      b_on |= B.loop_b[i];
      if (fp.x > cx - r - 0.3f && fp.x < cx + r + 0.3f) B.loop_zone = true;
    }
    int want = b_on ? L_LOOP_B : L_LOOP_A;
    if (want != B.loop_layer) {
      B.loop_layer = want;
      bike_relayer();
    }
  }
}

/* and after: a fall into a gap, the ragdoll, the measurements */
static void bike_post(bool air) {
  bike_state *s = &B.st;
  cw_xform2 f = cw_body2d_transform(B.frame);
  /* a barrier where a lane closes: still in it, the rider hits it and comes off, thrown back from it */
  if (s->alive) {
    narrow nw[MAX_NARROWS];
    int nn = track_narrows(T, nw);
    for (int i = 0; i < nn; i++) {
      float nose = f.p.x + 0.75f;
      if (!nw[i].wall || (B.lane >= nw[i].lo && B.lane <= nw[i].hi) || nose < nw[i].x0 || s->x + 0.75f >= nw[i].x0 + 1.5f) continue;
      cw_body2d parts[3] = { B.frame, B.rear, B.front };
      for (int k = 0; k < 3; k++) cw_body2d_set_velocity(parts[k], (cw_vec2){ -2.0f, 1.2f });
      cw_body2d_set_angular_velocity(B.frame, 2.5f);
      s->alive = false;
      B.crashed_now = true;
      B.crash_v = (cw_vec2){ -2.0f, 1.2f };
      break;
    }
  }
  if (s->alive) {
    t3_vec3 deer[DEER_CROSSINGS];
    int active=bike_deer_poses(1,deer);
    for(int i=0;i<DEER_CROSSINGS;i++) {
      if(!(active&(1<<i)) || fabsf(f.p.x-deer[i].x)>1.05f || fabsf(B.draw_z-deer[i].z)>.85f ||
         fabsf(f.p.y-deer[i].y)>2.2f)continue;
      cw_vec2 v=cw_body2d_velocity(B.frame);
      s->alive=false;B.crashed_now=true;
      B.crash_v=(cw_vec2){v.x*.45f,fmaxf(v.y,3.5f)};
      deer_hits++;
      break;
    }
  }
  /* falling into a gap */
  if (s->alive && f.p.y < WD.kill_y) {
    s->alive = false;
    B.crashed_now = true;
    B.crash_v = cw_body2d_velocity(B.frame);
  }

  /* measurements */
  bool was_air = air;
  s->grounded_rear = grounded(B.rear);
  s->grounded_front = grounded(B.front);
  air = !s->grounded_rear && !s->grounded_front;
  float ang = cw_rot2_angle(f.q);
  float d = cw_wrap_angle(ang - B.prev_angle);
  B.prev_angle = ang;
  if (air) {
    s->air_ticks++;
    s->air_rot += d;
  } else {
    if (was_air && s->alive && s->air_ticks > 0) {
      B.landed_now = 1;
      B.landed_rot = s->air_rot;
      B.landed_air = s->air_ticks;
      B.landed_err = B.loop_zone ? 0 : pitch_err();
      memcpy(B.landed_pose, B.pose_ticks, sizeof B.pose_ticks);
      if (B.pose_amt > 0.35f) { /* still posing as it lands: the rider bails */
        s->alive = false;
        B.crashed_now = B.bailed_now = true;
        B.crash_v = B.v_pre;
        B.landed_now = 0;
      }
    }
    s->air_ticks = 0;
    s->air_rot = 0;
    memset(B.pose_ticks, 0, sizeof B.pose_ticks);
  }
  if (s->grounded_rear && !s->grounded_front && s->alive) s->wheelie_ticks++;
  else s->wheelie_ticks = 0;
  cw_vec2 v = cw_body2d_velocity(B.frame);
  s->speed = v.x * f.q.c + v.y * f.q.s;
  s->x = f.p.x;
  s->y = f.p.y;
  s->angle = ang;
  s->vx = v.x;
  s->vy = v.y;
  s->crank = B.crank;
  if (B.crashed_now) make_ragdoll(); /* the rider comes off: the crash goes 3D (crash_post fills the state in) */
}

/* a tick for every rider in the world: in[i] and boosting[i] are rider i's */
void bike_step_all(const bike_input *in, const bool *boosting) {
  if (!cw_world2d_valid(WD.world)) return;
  /* the world steps SUBSTEPS times a tick: drawing interpolates each body
   * over the whole tick, as the camera is (over the last step alone, the
   * rider slid against the camera as the frame's alpha changed: faint
   * copies of him ahead and behind at speed, at any frame rate that is not
   * one frame a tick) */
  cw_world2d_tick_begin(WD.world);
  bk *keep = cur;
  /* act: on the bike (2D); down: crashed, tumbling in the crash world */
  bool act[MAX_RIDERS], down[MAX_RIDERS], air[MAX_RIDERS] = { 0 }, any = false;
  for (int i = 0; i < NB; i++) {
    any |= act[i] = cw_body2d_valid(BK[i].frame);
    any |= down[i] = BK[i].down3;
    if (down[i]) BK[i].crashed_now = BK[i].bailed_now = false, BK[i].landed_now = 0;
  }
  if (!any) return;
  /* SUBSTEPS Box2D steps a tick: Box2D turns a body at most a quarter turn
   * per step, which at 60 Hz capped a small wheel near 9 m/s (it skidded and
   * pitched the bike over the bars); at 180 Hz a 16-inch wheel rolls to 28 m/s */
  movers_tick();
  deer_tick();
  for (int i = 0; i < NB; i++)
    if (act[i]) cur = &BK[i], bike_pre(&in[i], &air[i]);
  for (int h = 0; h < SUBSTEPS; h++) {
    for (int i = 0; i < NB; i++) {
      if (act[i] && cw_body2d_valid(BK[i].frame) && BK[i].st.alive) cur = &BK[i], controls(&in[i], boosting[i], air[i]);
      if (act[i] && cw_body2d_valid(BK[i].frame)) BK[i].v_pre = cw_body2d_velocity(BK[i].frame);
    }
    cw_world2d_step(WD.world, 1.0f / (SUBSTEPS * TICK_HZ));
    for (int i = 0; i < NB; i++) {
      cur = &BK[i];
      if (act[i] && B.st.alive && air[i]) { /* and a hard stop at the cap (a pedal's kick in the air turns the frame too) */
        float w = cw_body2d_angular_velocity(B.frame);
        if (fabsf(w) > AIR_SPIN_MAX) cw_body2d_set_angular_velocity(B.frame, w > 0 ? AIR_SPIN_MAX : -AIR_SPIN_MAX);
      }
    }
    events();
  }
  /* bridges: a plank every rider has passed drops a moment later */
  for (int i = 0; i < WD.plank_count; i++) {
    plank *pk = &WD.planks[i];
    bool passed = true;
    for (int j = 0; j < NB; j++) passed &= act[j] && BK[j].st.alive && BK[j].st.x > pk->x + pk->half_w + 0.4f;
    if (pk->timer == 0 && passed) pk->timer = 1;
    else if (pk->timer > 0 && ++pk->timer == PLANK_DROP) {
      cw_body2d_set_type(pk->body, CW_BODY_DYNAMIC);
      cw_body2d_set_angular_velocity(pk->body, (i & 1) ? 1.5f : -1.2f);
    }
  }
  for (int i = 0; i < NB; i++)
    if (act[i]) cur = &BK[i], bike_post(air[i]);
  for(int i=0;i<NB;i++)if(act[i]) {
    bk *b=&BK[i];cw_xform2 wheel=cw_body2d_transform(b->rear);
    view_chocolate_tick(i,wheel.p.x,wheel.p.y,b->draw_z,b->st.alive&&b->st.grounded_rear,b->crashed_now);
  }
  /* the crash world steps while anyone is down (a rider who came off this
   * tick too: its bodies start where the 2D ones ended) */
  bool step3 = crash_toys_count()>0;
  if(step3)for(int i=0;i<NB;i++) {
    bk *b=&BK[i];
    if(cw_body2d_valid(b->rear))crash_toy_wheels(i,cw_body2d_transform(b->rear),cw_body2d_transform(b->front),b->draw_z,act[i]&&b->st.alive);
  }
  for (int i = 0; i < NB; i++) step3 |= BK[i].down3;
  if (step3) {
    float dt = 1.0f / TICK_HZ;
    mirror_sync(dt);
    cw_world3d_step(crash_world(), dt);
    int n = 0;
    const cw_phys_event3d *ev = cw_world3d_events(crash_world(), &n);
    crash_toy_sounds(ev,n);
    float thud = 0;
    for (int i = 0; i < n; i++)
      if (ev[i].type == CW_PHYS_HIT && ev[i].material_a < 101 && ev[i].material_b < 101 && ev[i].speed > thud) thud = ev[i].speed;
    if (thud > 3) game_sfx(SFX_THUD, cw_clampf(thud / 10, 0.2f, 1));
    for (int i = 0; i < NB; i++)
      if (BK[i].down3) {
        cur = &BK[i];crash_post();
        t3_vec3 p=cw_body3d_transform(BK[i].frame3).p;
        view_chocolate_tick(i,p.x,p.y,p.z,false,true);
        for(int limb=0;limb<RAG_COUNT;limb++) {
          t3_vec3 p=cw_body3d_transform(BK[i].rag[limb]).p;
          view_chocolate_tick(i,p.x,p.y,p.z,false,true);
        }
      }
  }
  cur = keep;
}

void bike_step(const bike_input *in, bool boosting) {
  bike_input ins[MAX_RIDERS] = { *in };
  bool bo[MAX_RIDERS] = { boosting };
  bike_step_all(ins, bo);
}

const bike_state *bike_get(void) { return &B.st; }
bool bike_rider_near(float x,float radius) {
  for(int i=0;i<NB;i++)if(fabsf(BK[i].st.x-x)<radius)return true;
  return false;
}
bool bike_crashed_this_tick(void) { return B.crashed_now; }
bool bike_bailed_this_tick(void) { return B.bailed_now; }
void bike_landing_more(float *err, int pose_ticks[4]) {
  *err = B.landed_err;
  memcpy(pose_ticks, B.landed_pose, sizeof B.landed_pose);
}
void bike_air_poses(int pose_ticks[4]) { memcpy(pose_ticks, B.pose_ticks, sizeof B.pose_ticks); }
int bike_landed_this_tick(float *rotation, int *air_ticks) {
  if (rotation) *rotation = B.landed_rot;
  if (air_ticks) *air_ticks = B.landed_air;
  return B.landed_now;
}

/* where the riding rider's limbs are (frame-local 2D, then placed by the
 * frame): feet on the pedals, hands on the bars, hips on the saddle; lean
 * slides the hips back and the shoulders forward */
static cw_vec2 lerp2(cw_vec2 a, cw_vec2 b, float t) { return (cw_vec2){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t }; }
static void two_bone(cw_vec2 a, cw_vec2 c, float l1, float l2, float bend, cw_vec2 *knee) {
  float dx = c.x - a.x, dy = c.y - a.y, d = sqrtf(dx * dx + dy * dy);
  d = cw_clampf(d, 0.05f, l1 + l2 - 0.001f);
  float ca = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d);
  float ang = cw_atan2(dy, dx) + bend * cw_acos(ca);
  float s, cc;
  cw_sincos(ang, &s, &cc);
  *knee = (cw_vec2){ a.x + l1 * cc, a.y + l1 * s };
}
static cw_xform2 seg(cw_xform2 frame, cw_vec2 a, cw_vec2 b) {
  cw_vec2 wa = xpt(frame, a), wb = xpt(frame, b);
  return (cw_xform2){ wa, cw_rot2_from_angle(cw_atan2(wb.y - wa.y, wb.x - wa.x)) };
}

void bike_pose_get(float alpha, bike_pose *out) {
  memset(out, 0, sizeof *out);
  out->z = B.draw_z;
  if (B.down3) { /* the crash, in 3D */
    out->ragdoll = true;
    out->frame = out->rear = out->front = out->cap = cw_xform2_identity();
    for (int i = 0; i < RAG_COUNT; i++) out->limb3[i] = cw_body3d_interpolated(B.rag[i], alpha);
    out->frame3 = cw_body3d_interpolated(B.frame3, alpha);
    out->rear3 = cw_body3d_interpolated(B.rear3, alpha);
    out->front3 = cw_body3d_interpolated(B.front3, alpha);
    out->cap3 = cw_body3d_interpolated(B.cap, alpha);
    out->crank = B.crank;
    return;
  }
  if (!cw_body2d_valid(B.frame)) {
    out->frame = cw_xform2_identity();
    return;
  }
  out->frame = cw_body2d_interpolated(B.frame, alpha);
  out->rear = cw_body2d_interpolated(B.rear, alpha);
  out->front = cw_body2d_interpolated(B.front, alpha);
  out->crank = B.crank;
  out->lean = B.st.lean_shown;
  float L = out->lean;
  cw_vec2 hip = { HIP.x - 0.1f * L, HIP.y - 0.02f * cw_clampf(L, 0, 1) };
  cw_vec2 sh = { SHOULDER.x + 0.18f * L, SHOULDER.y - 0.12f * cw_clampf(L, 0, 1) + 0.04f * cw_clampf(-L, 0, 1) };
  cw_vec2 bars = { 0.42f, 1.0f };
  float cs, cc;
  cw_sincos(B.crank, &cs, &cc);
  cw_vec2 pedal_l = { 0.17f * cc, 0.17f * cs }, pedal_r = { -0.17f * cc, -0.17f * cs };
  /* the poses: no-hands throws the arms up, no-feet kicks the legs off the
   * pedals fore and aft, superman flies the body out straight behind the bars */
  cw_vec2 hand = bars, foot_l = pedal_l, foot_r = pedal_r;
  float a = B.pose_amt;
  if (a > 0) {
    a = a * a * (3 - 2 * a);
    if (B.pose_kind == POSE_NO_HANDS) hand = lerp2(bars, (cw_vec2){ sh.x + 0.12f, sh.y + 0.56f }, a);
    else if (B.pose_kind == POSE_NO_FEET) {
      foot_l = lerp2(pedal_l, (cw_vec2){ hip.x + 0.66f, hip.y - 0.3f }, a);
      foot_r = lerp2(pedal_r, (cw_vec2){ hip.x - 0.6f, hip.y - 0.36f }, a);
    } else if (B.pose_kind == POSE_SUPERMAN) {
      hip = lerp2(hip, (cw_vec2){ -0.62f, 1.12f }, a);
      sh = lerp2(sh, (cw_vec2){ 0.1f, 1.2f }, a);
      foot_l = lerp2(pedal_l, (cw_vec2){ hip.x - 0.86f, hip.y + 0.08f }, a);
      foot_r = lerp2(pedal_r, (cw_vec2){ hip.x - 0.84f, hip.y - 0.04f }, a);
    }
  }
  /* The IK target is the ankle, above the pedal spindle: the shoe's sole
   * reaches the pedal, rather than passing through its middle. */
  pedal_l = foot_l, pedal_r = foot_r;
  pedal_l.y += 0.105f;
  pedal_r.y += 0.105f;
  cw_vec2 knee_l, knee_r, elbow;
  two_bone(hip, pedal_l, 0.44f, 0.44f, 1, &knee_l);
  two_bone(hip, pedal_r, 0.44f, 0.44f, 1, &knee_r);
  two_bone(sh, hand, 0.3f, 0.3f, -1, &elbow);
  bars = hand;
  cw_xform2 F = out->frame;
  out->limb[RAG_TORSO] = seg(F, hip, sh);
  cw_vec2 head_at = { sh.x + 0.1f + 0.04f * L, sh.y + 0.25f - 0.03f * cw_clampf(L, 0, 1) };
  head_at.y -= 0.08f;
  out->limb[RAG_HEAD] = (cw_xform2){ xpt(F, head_at), F.q };
  out->cap = (cw_xform2){ xpt(F, (cw_vec2){ head_at.x - 0.005f, head_at.y + 0.025f }), F.q };
  out->limb[RAG_UARM_L] = seg(F, sh, elbow);
  out->limb[RAG_FARM_L] = seg(F, elbow, bars);
  out->limb[RAG_UARM_R] = out->limb[RAG_UARM_L];
  out->limb[RAG_FARM_R] = out->limb[RAG_FARM_L];
  out->limb[RAG_THIGH_L] = seg(F, hip, knee_l);
  out->limb[RAG_SHIN_L] = seg(F, knee_l, pedal_l);
  out->limb[RAG_THIGH_R] = seg(F, hip, knee_r);
  out->limb[RAG_SHIN_R] = seg(F, knee_r, pedal_r);
}

uint32_t bike_hash(uint32_t h) {
  h=crash_toys_hash(h);
  if (!cw_body2d_valid(BK[0].frame) && !BK[0].down3) return h;
  for (int j = 0; j < NB; j++) { /* every rider's bodies */
    const bk *b = &BK[j];
    if (b->down3) { /* a crash: the rider, the bike and the cap in the crash world */
      cw_xform3 x[RAG_COUNT + 4];
      for (int i = 0; i < RAG_COUNT; i++) x[i] = cw_body3d_transform(b->rag[i]);
      x[RAG_COUNT] = cw_body3d_transform(b->frame3);
      x[RAG_COUNT + 1] = cw_body3d_transform(b->rear3);
      x[RAG_COUNT + 2] = cw_body3d_transform(b->front3);
      x[RAG_COUNT + 3] = cw_body3d_transform(b->cap);
      h = cw_hash32(x, sizeof x, h);
      continue;
    }
    if (!cw_body2d_valid(b->frame)) continue;
    cw_xform2 x[3] = { cw_body2d_transform(b->frame), cw_body2d_transform(b->rear), cw_body2d_transform(b->front) };
    h = cw_hash32(x, sizeof x, h);
  }
  for (int i = 0; i < WD.plank_count; i++) {
    cw_xform2 r = cw_body2d_transform(WD.planks[i].body);
    h = cw_hash32(&r, sizeof r, h);
  }
  mover_pose mp[MAX_MOVERS * MAX_LOGS];
  int nm = bike_movers(1, mp, MAX_MOVERS * MAX_LOGS);
  h = cw_hash32(mp, sizeof mp[0] * (size_t)nm, h);
  return h;
}

/* the bridge planks, for drawing: count, and each one's pose and half width */
int bike_planks(float alpha, cw_xform2 *out, float *half_w, int cap) {
  int n = 0;
  for (int i = 0; i < WD.plank_count && n < cap; i++, n++) {
    out[n] = cw_body2d_interpolated(WD.planks[i].body, alpha);
    half_w[n] = WD.planks[i].half_w;
  }
  return n;
}

int bike_rider_for_body(cw_body3d body) {
  if(!body.id)return -1;
  for(int i=0;i<NB;i++)if(BK[i].down3) {
    if(body.id==BK[i].frame3.id||body.id==BK[i].rear3.id||body.id==BK[i].front3.id)return i;
    for(int j=0;j<RAG_COUNT;j++)if(body.id==BK[i].rag[j].id)return i;
  }
  return -1;
}

/* Upright riders live in Box2D. Small swept sphere proxies let chocolate hit
 * their posed limbs and wheels too, without adding another physics world. */
int bike_chocolate_raycast(t3_vec3 origin,t3_vec3 delta,cw_ray_hit3d *hit) {
  float aa=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
  if(aa<1e-10f)return -1;
  bk *saved=cur;int rider=-1;
  for(int r=0;r<NB;r++)if(!BK[r].down3&&BK[r].st.alive) {
    if(fabsf(origin.x-BK[r].st.x)>3+fabsf(delta.x)||fabsf(origin.z-BK[r].draw_z)>.6f+fabsf(delta.z))continue;
    cur=&BK[r];bike_pose p;bike_pose_get(1,&p);
    for(int j=0;j<RAG_COUNT+3;j++) {
      cw_xform2 pose=j<RAG_COUNT?p.limb[j]:j==RAG_COUNT?p.frame:j==RAG_COUNT+1?p.rear:p.front;
      float radius=j==RAG_TORSO?.26f:j==RAG_HEAD?.21f:j>=RAG_COUNT?.34f:.13f;
      t3_vec3 center={pose.p.x,pose.p.y,p.z};
      t3_vec3 v={origin.x-center.x,origin.y-center.y,origin.z-center.z};
      float b=v.x*delta.x+v.y*delta.y+v.z*delta.z;
      float c=v.x*v.x+v.y*v.y+v.z*v.z-radius*radius,disc=b*b-aa*c;
      if(disc<0)continue;
      float f=(-b-sqrtf(disc))/aa;
      if(f<0||f>=hit->fraction)continue;
      hit->fraction=f;hit->point=(t3_vec3){origin.x+f*delta.x,origin.y+f*delta.y,origin.z+f*delta.z};
      hit->normal=(t3_vec3){(hit->point.x-center.x)/radius,(hit->point.y-center.y)/radius,(hit->point.z-center.z)/radius};rider=r;
    }
  }
  cur=saved;return rider;
}
