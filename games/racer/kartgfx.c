/* racer's karts as drawn: a node per kart placed from the simulation on the fixed tick (and drawn between the last
 * two ticks), and a rig of parts moved from the sim every frame: wheels posed by the vehicle controller, a body that
 * rolls into turns and dips on the brakes, squashes on landings, slews in a drift, tumbles when spun out, rolls and
 * flips in a trick, flames out of the exhausts on a boost, a shield bubble, and a driver who steers, leans, looks
 * back, flails and cheers.
 *
 * Everything here is a function of the simulation's numbers (render only, nothing feeds back): the fixed tick runs
 * the springs, the frame blends the last two poses. */
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "cartwheel/rim_light.h"
#include "models.h"

enum {
  KP_ROLL, KP_PITCH, KP_YAW, KP_SQUASH, KP_HEADYAW, KP_HEADPITCH, KP_TROLL, KP_TLEAN, KP_TTWIST, KP_STEER, KP_CHEER, KP_FLAIL, KP_BOOST, KP_BRAKE,
  KP_SHIELD, KP_TRICK, KP_FLIP, KP_SHAKE, KP_HOP, KP_DRIFT, KP_FLASH, KP_SPEED, KP_ACCEL, KP_LAT, KP_COUNT
};

typedef struct kstate {
  float cur[KP_COUNT], prev[KP_COUNT];
  float roll, roll_v, pitch, pitch_v, squash, squash_v, yaw, yaw_v, hy, hy_v, hp, hp_v, tr, tr_v, tl, tl_v, cheer, tw, tw_v;
  float trick_ang;
  int trick_kind;
  t3_vec3 pv;
  float pvup;
  int pair;
  bool first;
  float wc[4], wc_prev[4]; /* the wheels' compression under the drawn chassis (sim_wheel_compression_at), this tick and the last */
} kstate;

/* sw wheel_lag <0|1>: 1 = the wheels drawn from the vehicle controller's own casts, a tick behind the drawn chassis (the old
 * way: tests/racer-grounded.mjs's control) */
int kartgfx_wheel_lag;

static struct {
  kart_rig rig[MAXK];
  kstate st[MAXK];
  cw_node node[MAXK];
  const sim *sim;
} K;

/* where the views' eyes are, for the far karts' simpler rigs */
static t3_vec3 EYE[4];
static int NEYE;
void kartgfx_set_eyes(const t3_vec3 *e, int n) {
  NEYE = n > 4 ? 4 : n;
  for (int i = 0; i < NEYE; i++) EYE[i] = e[i];
}

cw_node kartgfx_node(int i) { return K.node[i]; }

static void ks_forget(void);
void kartgfx_reset(void) {
  ks_forget(); /* (the rigs it points into go) */
  showroom_reset();
  for (int i = 0; i < MAXK; i++)
    if (K.rig[i].root) rig_free(&K.rig[i]);
  memset(&K, 0, sizeof K);
}

void kartgfx_build(cw_node parent, const sim *s) {
  kartgfx_reset();
  K.sim = s;
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &s->k[i];
    if (!k->on) continue;
    rig_build(&K.rig[i], k->veh, k->colour, i + 1);
    /* each view draws a kart past this as its merged far version: nearer in split screen, where a view is smaller */
    rig_far_distance(&K.rig[i], game_planned_views() > 1 ? 22.0f : 45.0f);
    if (s->battle && mode_teams(s)) {
      /* a team round: an outline in the team's colour, so a kart across the field reads as red or blue whatever its
       * own paint (it fades in with distance: one alongside keeps its paint) */
      cw_rim_light_desc d = cw_rim_light_default();
      d.color = (k->team & 1) ? (t3_color){ 0.25f, 0.5f, 1.0f } : (t3_color){ 1.0f, 0.28f, 0.18f };
      d.strength = 2.4f;
      cw_rim_light_apply(K.rig[i].mat[S_PAINT], &d);
      cw_rim_light_apply(K.rig[i].mat[S_PAINT2], &d);
    }
    K.node[i] = hang(parent, "Kart", K.rig[i].root);
    /* drawn between the last two ticks, as the cameras are: a kart stepped at the tick rate under a smooth camera
     * twitched forward and back on a screen whose frames do not line up with the ticks */
    cw_node_set(K.node[i], "interpolate", cw_val_bool(!getenv("RACER_NOINTERP"))); /* RACER_NOINTERP: the old way, a control */
    t3_release(K.rig[i].root);
    cw_node3d_set_position(K.node[i], k->p);
    cw_node3d_set_quaternion(K.node[i], k->q);
    K.st[i].first = true;
  }
}

/* the colour of the tyre smoke and sparks for a drift of this charge (the particle code reads it) */
void kartgfx_drift_colour(int kart_i, float rgb[3]) {
  int lv = K.sim && kart_i >= 0 && kart_i < MAXK ? K.sim->k[kart_i].drift_level : 0;
  static const float C[4][3] = { { 0.8f, 0.8f, 0.8f }, { 0.3f, 0.6f, 1.0f }, { 1.0f, 0.6f, 0.15f }, { 0.8f, 0.3f, 1.0f } };
  memcpy(rgb, C[lv < 0 ? 0 : lv > 3 ? 3 : lv], sizeof(float) * 3);
}

static void spr(float *x, float *v, float target, float w, float z) {
  const float dt = 1.0f / 60.0f;
  float a = w * w * (target - *x) - 2.0f * z * w * (*v);
  *v += a * dt;
  *x += *v * dt;
}
static float clampf_(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float ease(float p) { return p * p * (3 - 2 * p); }

void kartgfx_tick(const sim *s) {
  K.sim = s;
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &s->k[i];
    if (!k->on || !cw_node_valid(K.node[i])) continue;
    cw_node n = K.node[i];
    t3_vec3 was = cw_node3d_position(n);
    cw_node3d_snapshot(n);
    cw_node3d_set_position(n, k->p);
    cw_node3d_set_quaternion(n, k->q);
    /* put back on the road: no blend across the jump */
    t3_vec3 jd = v3sub(k->p, was);
    bool cut = v3dot(jd, jd) > 6.0f * 6.0f;
    if (cut) cw_node3d_snapshot(n);
    kstate *st = &K.st[i];
    /* the wheels cast from where the chassis is drawn this tick (the controller's own casts are a tick behind it) */
    for (int w = 0; w < 4; w++) {
      st->wc_prev[w] = st->wc[w];
      st->wc[w] = sim_wheel_compression_at(i, w, (cw_xform3){ k->p, k->q });
      if (cut || st->first) st->wc_prev[w] = st->wc[w];
    }
    memcpy(st->prev, st->cur, sizeof st->cur);
    t3_vec3 up = kart_up(k), fw = kart_fwd(k);
    if (st->first) st->pv = k->v, st->first = false, memcpy(st->prev, st->cur, sizeof st->cur);
    t3_vec3 acc = v3mul(v3sub(k->v, st->pv), (float)TICK_HZ);
    float a_f = clampf_(v3dot(acc, fw), -30, 30);
    float lat = clampf_(v3dot(k->w, up) * k->speed, -45, 45);
    float vup = v3dot(k->v, up);
    bool air = !k->grounded;
    /* landings squash the body, take-offs stretch it */
    if (st->pair > 8 && k->air_ticks == 0 && k->grounded) st->squash_v += 1.5f + 4.0f * clampf_(-st->pvup / 22.0f, 0, 1);
    if (k->air_ticks == 1) st->squash_v -= 1.4f;
    st->pair = k->air_ticks;
    st->pvup = vup;
    st->pv = k->v;

    float dk = k->drifting ? 1.0f : 0.0f;
    float roll_t = clampf_(0.0075f * lat, -0.13f, 0.13f) + (float)k->drift_dir * 0.07f * dk;
    float pitch_t = clampf_(-0.004f * a_f, -0.06f, 0.075f);
    float wob = 0;
    if (k->spin_ticks > 0) {
      float ph = (float)k->spin_ticks / (70.0f * (s->battle ? BATTLE_SPIN_K : 1.0f)); /* a battle spin is shorter (sim_battle.inc) */
      wob = sinf((float)s->tick * 0.55f) * 0.2f * clampf_(ph, 0, 1);
      if (k->spin_kind == SPIN_SHOCK) wob = sinf((float)s->tick * 2.3f) * 0.12f;
    }
    if (k->bump_ticks > 0) wob += sinf((float)s->tick * 0.9f) * 0.05f * clampf_((float)k->bump_ticks / 10.0f, 0, 1);
    spr(&st->roll, &st->roll_v, roll_t + wob, 16, 0.5f);
    spr(&st->pitch, &st->pitch_v, pitch_t, 14, 0.5f);
    spr(&st->yaw, &st->yaw_v, (float)k->drift_dir * dk * (0.16f + 0.06f * (float)k->drift_level), 9, 0.7f);
    spr(&st->squash, &st->squash_v, 0, 26, 0.28f);
    st->squash = clampf_(st->squash, -0.12f, 0.16f);

    /* the driver */
    bool back = (k->last_btn & KB_BACK) != 0;
    float hy_t = clampf_(k->steer * 0.9f, -0.6f, 0.6f) + (float)k->drift_dir * dk * 0.45f;
    float tw_t = 0;
    if (back) hy_t = 2.5f, tw_t = 0.7f;
    if (k->spin_ticks > 0) hy_t = sinf((float)s->tick * 0.35f) * 1.1f;
    spr(&st->hy, &st->hy_v, hy_t, back ? 22 : 12, 0.8f);
    spr(&st->tw, &st->tw_v, tw_t, 14, 0.8f);
    bool boosting = k->boost_ticks > 0;
    float hp_t = (boosting ? -0.3f : 0) + (a_f < -5 ? 0.25f : 0) + (k->finished && k->place > 3 ? 0.4f : 0);
    spr(&st->hp, &st->hp_v, hp_t, 12, 0.6f);
    float tl_t = clampf_(k->spin_ticks > 0 ? 0 : 0.004f * a_f, -0.2f, 0.25f) + (boosting ? 0.2f : 0);
    spr(&st->tl, &st->tl_v, tl_t, 11, 0.6f);
    spr(&st->tr, &st->tr_v, -st->roll * 0.8f - k->steer * 0.2f, 12, 0.6f);
    float cheer_t = k->finished ? (k->place <= 3 ? 1.0f : 0.0f) : (k->trick_ticks > 0 ? 0.75f : 0.0f);
    st->cheer += (cheer_t - st->cheer) * 0.12f;

    /* tricks: a roll, a flip or a spin about the body's own axes, or just a bit of everything; settles to the nearest turn */
    if (k->trick_ticks > 0) {
      if (k->trick_ticks >= 39) st->trick_kind = (i + k->tricks + s->tick / 7) % 3;
      float p = 1.0f - (float)k->trick_ticks / 40.0f;
      st->trick_ang = 6.2831853f * ease(clampf_(p * 1.05f, 0, 1));
    } else if (st->trick_ang != 0) {
      float tgt = 6.2831853f * roundf(st->trick_ang / 6.2831853f);
      st->trick_ang += (tgt - st->trick_ang) * 0.35f;
      if (fabsf(tgt - st->trick_ang) < 0.01f) st->trick_ang = 0;
    }
    float flip = 0, hop = 0;
    if (k->spin_ticks > 0 && k->spin_kind == SPIN_BOOM) {
      float p = 1.0f - (float)k->spin_ticks / (85.0f * (s->battle ? BATTLE_SPIN_K : 1.0f));
      flip = 6.2831853f * ease(clampf_(p * 1.4f, 0, 1)) * (k->spin_dir < 0 ? -1.0f : 1.0f);
      hop = 0.55f * sinf(3.14159265f * clampf_(p * 1.4f, 0, 1));
    } else if (k->spin_ticks > 0 && k->spin_kind == SPIN_HIT) {
      hop = 0.12f * sinf((float)k->spin_ticks * 0.3f);
    }
    float *c = st->cur;
    c[KP_ROLL] = st->roll, c[KP_PITCH] = st->pitch, c[KP_YAW] = st->yaw, c[KP_SQUASH] = st->squash;
    c[KP_HEADYAW] = st->hy, c[KP_HEADPITCH] = st->hp, c[KP_TROLL] = st->tr, c[KP_TLEAN] = st->tl, c[KP_TTWIST] = st->tw;
    c[KP_STEER] = k->steer, c[KP_CHEER] = st->cheer, c[KP_FLAIL] = k->spin_ticks > 0 ? 1.0f : 0.0f;
    float bl = boosting ? clampf_((float)k->boost_ticks / 12.0f, 0.25f, 1.0f) : 0.0f;
    float idle = clampf_((a_f - 4.0f) / 10.0f, 0, 1) * 0.28f;
    bool pop = a_f < -5.0f && ((s->tick * 7 + i * 3) % 13) == 0;
    c[KP_BOOST] = bl > idle ? bl : idle;
    if (pop && c[KP_BOOST] < 0.3f) c[KP_BOOST] = 0.3f;
    c[KP_BRAKE] = (a_f < -4.0f && !air) ? 1.0f : 0.0f;
    c[KP_SHIELD] = (float)k->shield_ticks;
    c[KP_TRICK] = st->trick_ang, c[KP_FLIP] = flip;
    c[KP_SHAKE] = (k->spin_ticks > 0 && k->spin_kind == SPIN_SHOCK) ? 1.0f : 0.0f;
    c[KP_HOP] = hop, c[KP_DRIFT] = dk * (float)k->drift_level;
    c[KP_FLASH] = c[KP_SHAKE] * (0.5f + 0.5f * sinf((float)s->tick * 1.7f));
    c[KP_SPEED] = k->speed, c[KP_ACCEL] = a_f, c[KP_LAT] = lat;
    if (st->cur[KP_TRICK] == 0 && st->prev[KP_TRICK] == 0) st->trick_kind = st->trick_kind;
    st->trick_kind = st->trick_kind % 3;
    /* the first tick: no blend from nothing */
    if (s->tick <= 1) memcpy(st->prev, st->cur, sizeof st->cur);
  }
}

static float lerp_(float a, float b, float t) { return a + (b - a) * t; }

/* two-bone arm from shoulder S to hand H, elbow bent out and down */
static void arm(kart_rig *r, int side, t3_vec3 S, t3_vec3 H, float L, float sgn) {
  t3_vec3 d = v3sub(H, S);
  float dist = v3len(d);
  float maxd = 1.96f * L;
  if (dist > maxd) H = v3add(S, v3mul(d, maxd / dist)), d = v3sub(H, S), dist = maxd;
  t3_vec3 ax = dist > 1e-5f ? v3mul(d, 1.0f / dist) : v3(0, 0, 1);
  t3_vec3 pole = v3(sgn * 0.7f, -0.7f, -0.15f);
  pole = v3norm(v3sub(pole, v3mul(ax, v3dot(pole, ax))));
  float hgt = sqrtf(fmaxf(0, L * L - dist * dist * 0.25f));
  t3_vec3 E = v3add(v3add(S, v3mul(d, 0.5f)), v3mul(pole, hgt));
  place_tube(r->arm_up[side], S, E);
  place_tube(r->arm_lo[side], E, H);
  t3_object_set_position(r->hand[side], H.x, H.y, H.z);
}

static t3_quat qaxis(float x, float y, float z, float a) { return t3_quat_from_axis_angle((t3_vec3){ x, y, z }, a); }
static t3_quat qmul(t3_quat a, t3_quat b) { return t3_quat_multiply(a, b); }

static void pose_driver(kart_rig *r, const float *P, float tsec, bool both_hands);

/* ── the drawn tyres against the road under them ──────────────────────
 * Every frame, each kart's four tyres as drawn (the node between its last two ticks, the rig's root, the wheel pivots)
 * against the road under each one (sim_ground_ray: the road's collision mesh, the same sweep trackmesh.c draws): the
 * gap from the lowest point of the tyre to the road, in mm, of the wheel nearest the road. tests/racer-grounded.mjs
 * reads it. A kart whose physics wheels touch the road is grounded; one drawn above it there hovers. */
#define GAP_NONE 99999
#define GAP_TOL_MM 30
/* what a frame is: the physics has every wheel on the road, some, none (in the air off a crest or a bump, or a jump's
 * flight: near a ramp or a gap), or the drawn kart flips, hops or tricks on purpose (kartgfx_tick) */
enum { GC_ALL, GC_SOME, GC_AIR, GC_JUMP, GC_ANIM, GC_N };
static struct {
  int32_t gap[MAXK];          /* this frame: the closest drawn tyre's gap to the road, mm (GAP_NONE: no road under it) */
  int32_t gnd[MAXK];          /* this frame: the physics wheels on the road after the last tick */
  int32_t frames[MAXK];       /* frames measured in a race since the last `gap reset` */
  int32_t n[MAXK * GC_N];     /* per kart and class: frames drawn over GAP_TOL_MM above the road */
  int32_t max[MAXK * GC_N];   /* the largest such gap, mm */
  int32_t at_s[MAXK * GC_N];  /* where it was: metres along the main road */
  int32_t at_tick[MAXK * GC_N];
  int32_t sunk_max[MAXK];     /* the deepest a tyre went into the road, mm (positive: under it; not in a GC_ANIM frame) */
  int32_t sunk_s[MAXK], sunk_ev[MAXK * 8]; /* where, and per wheel the gap and what it stands on (-1 nothing) */
  int32_t ev[MAXK * GC_N * 16]; /* at each max: per wheel the gap (mm), what the physics wheel stands on (sim_body_kind), its
                                 * compression (mm), the drawn tyre against the physics contact (mm) */
} GP;

/* `kshadow <kart> <0|1>`: that kart's shadow off and back on (tests/racer-grounded.mjs draws the same paused frame both
 * ways: what goes darker is the shadow it throws on the road) */
static struct {
  t3_object *o[1024];
  bool cast[1024];
  int n, kart;
} KS = { .kart = -1 };
static void ks_forget(void) { KS.n = 0, KS.kart = -1; }
static void ks_off(t3_object *o, void *u) {
  (void)u;
  if (KS.n < 1024) KS.o[KS.n] = o, KS.cast[KS.n] = o->cast_shadow, KS.n++;
  o->cast_shadow = false;
}
static void cmd_kshadow(int argc, const char **argv, cw_reply *r) {
  int i = argc > 1 ? atoi(argv[1]) : 0, on = argc > 2 ? atoi(argv[2]) : 1;
  if (KS.kart >= 0) {
    for (int j = 0; j < KS.n; j++) KS.o[j]->cast_shadow = KS.cast[j];
    KS.n = 0, KS.kart = -1;
  }
  if (!on && i >= 0 && i < MAXK && K.rig[i].root) KS.kart = i, t3_object_traverse(K.rig[i].root, ks_off, NULL);
  cw_reply_printf(r, "{\"kart\":%d,\"off\":%d}", KS.kart, KS.n);
}

static void cmd_gap(int argc, const char **argv, cw_reply *r) {
  if (argc > 1 && !strcmp(argv[1], "reset")) {
    int32_t keep_gap[MAXK], keep_gnd[MAXK];
    memcpy(keep_gap, GP.gap, sizeof keep_gap), memcpy(keep_gnd, GP.gnd, sizeof keep_gnd);
    memset(&GP, 0, sizeof GP);
    memcpy(GP.gap, keep_gap, sizeof keep_gap), memcpy(GP.gnd, keep_gnd, sizeof keep_gnd);
  }
  cw_reply_printf(r, "{\"tol_mm\":%d}", GAP_TOL_MM);
}

void kartgfx_debug_init(void) {
  cw_debug_field("karts.gap_mm", GP.gap, CW_DBG_I32, MAXK);
  cw_debug_field("karts.gnd", GP.gnd, CW_DBG_I32, MAXK);
  cw_debug_field("gap.frames", GP.frames, CW_DBG_I32, MAXK);
  cw_debug_field("gap.n", GP.n, CW_DBG_I32, MAXK * GC_N);           /* [kart * 5 + class]: all down, some, air, a jump, a drawn flip, hop or trick */
  cw_debug_field("gap.max_mm", GP.max, CW_DBG_I32, MAXK * GC_N);
  cw_debug_field("gap.at_s", GP.at_s, CW_DBG_I32, MAXK * GC_N);
  cw_debug_field("gap.at_tick", GP.at_tick, CW_DBG_I32, MAXK * GC_N);
  cw_debug_field("gap.sunk_max_mm", GP.sunk_max, CW_DBG_I32, MAXK);
  cw_debug_field("gap.sunk_s", GP.sunk_s, CW_DBG_I32, MAXK);
  cw_debug_field("gap.sunk_ev", GP.sunk_ev, CW_DBG_I32, MAXK * 8);
  cw_debug_field("gap.ev", GP.ev, CW_DBG_I32, MAXK * GC_N * 16);
  cw_debug_command("gap", "gap [reset]: the drawn tyres' gap to the road (karts.gap_mm, gap.*); reset clears the counts", cmd_gap);
  cw_debug_command("kshadow", "kshadow <kart> <0|1>: that kart throws no shadow (0) or its own again (1)", cmd_kshadow);
}

/* a jump's flight: a ramp or a gap from 80 samples back to 10 ahead (on the main road, or on the branch it is on) */
static bool near_jump(const track *t, int i) {
  if (!t || i < 0 || i >= t->nall) return false;
  int first = 0, n = t->n;
  for (int b = 0; b < t->nbranch; b++)
    if (i >= t->br[b].first && i < t->br[b].first + t->br[b].n) first = t->br[b].first, n = t->br[b].n;
  for (int d = -80; d <= 10; d++) {
    int j = first + ((i - first + d) % n + n) % n;
    if (t->s[j].flags & (TF_GAP | TF_RAMP)) return true;
  }
  return false;
}

static void ground_probe(const sim *s, int i, const kart_rig *r, float alpha) {
  const kart *k = &s->k[i];
  cw_node3d *d = cw_node_data(K.node[i], "Node3D");
  if (!d || !r->root) return;
  t3_vec3 p = d->position;
  t3_quat q = d->quaternion;
  if (d->interpolate && alpha < 1) p = t3_vec3_lerp(d->prev_position, d->position, alpha), q = t3_quat_slerp(d->prev_quaternion, d->quaternion, alpha);
  const t3_object *root = r->root;
  float R = VEH[k->veh % NVEH].wheel_r;
  t3_vec3 up = t3_vec3_apply_quat((t3_vec3){ 0, 1, 0 }, q);
  float best = 1e9f, sunk = -1e9f, wg[4] = { 99, 99, 99, 99 }, wp[4] = { 99, 99, 99, 99 };
  const cw_vehicle3d *car = sim_car(s, i);
  for (int w = 0; w < 4; w++) {
    const t3_object *wo = r->wheel[w];
    t3_vec3 c = v3add(root->position, t3_vec3_apply_quat(wo->position, root->quaternion));
    c = v3add(p, t3_vec3_apply_quat(c, q));
    t3_vec3 ax = t3_vec3_apply_quat((t3_vec3){ 1, 0, 0 }, qmul(q, qmul(root->quaternion, wo->quaternion)));
    t3_vec3 hit, n;
    if (!sim_ground_ray(v3add(c, v3mul(up, 1.5f)), v3mul(up, -4.0f), &hit, &n)) continue;
    /* the tyre's lowest point towards the road: down the road's normal, in the wheel's own plane */
    t3_vec3 dn = v3mul(n, -1.0f);
    dn = v3norm(v3sub(dn, v3mul(ax, v3dot(dn, ax))));
    float g = v3dot(v3sub(v3add(c, v3mul(dn, R)), hit), n);
    best = fminf(best, g);
    sunk = fmaxf(sunk, -g);
    wg[w] = g;
    /* the physics wheel's own contact against the drawn tyre */
    if (car->wheel[w].contact) wp[w] = v3dot(v3sub(v3add(c, v3mul(dn, R)), car->wheel[w].point), n);
  }
  /* the wheels the physics has on the road (or an arena floor): a wheel on a wall or another kart is not standing on the
   * road, and the kart is in the air over it as far as this goes */
  int on_road = 0;
  for (int w = 0; w < 4; w++) on_road += car->wheel[w].contact && sim_body_kind(car->wheel[w].ground) == 1;
  GP.gnd[i] = on_road;
  GP.gap[i] = best > 1e8f ? GAP_NONE : (int32_t)lrintf(best * 1000.0f);
  /* a drawn flip, hop or trick (kartgfx_tick: a bomb's flip, a hit's hop, a trick's roll) lifts the drawn kart on purpose */
  const kstate *st = &K.st[i];
  bool anim = false;
  for (int j = 0; j < 2; j++) {
    const float *P = j ? st->prev : st->cur;
    anim = anim || P[KP_HOP] != 0 || P[KP_FLIP] != 0 || P[KP_TRICK] != 0;
  }
  if (s->phase != PH_RACE || best > 1e8f || k->respawn_ticks > 0) return;
  GP.frames[i]++;
  if (GP.gap[i] > GAP_TOL_MM) {
    int c = anim ? GC_ANIM : on_road == 4 ? GC_ALL : on_road > 0 ? GC_SOME : s->battle || !near_jump(s->track, k->loc.i) ? GC_AIR : GC_JUMP;
    int x = i * GC_N + c;
    GP.n[x]++;
    if (GP.gap[i] > GP.max[x]) {
      GP.max[x] = GP.gap[i], GP.at_s[x] = (int32_t)k->loc.ms, GP.at_tick[x] = s->tick;
      int32_t *e = GP.ev + x * 16;
      for (int w = 0; w < 4; w++)
        e[w] = (int32_t)lrintf(wg[w] * 1000), e[4 + w] = sim_body_kind(car->wheel[w].ground), e[8 + w] = (int32_t)lrintf(car->wheel[w].compression * 1000),
        e[12 + w] = (int32_t)lrintf(wp[w] * 1000);
    }
  }
  int32_t sk = (int32_t)lrintf(sunk * 1000.0f);
  if (!anim && sk > GP.sunk_max[i]) {
    GP.sunk_max[i] = sk, GP.sunk_s[i] = (int32_t)k->loc.ms;
    int32_t *e = GP.sunk_ev + i * 8;
    for (int w = 0; w < 4; w++) e[w] = (int32_t)lrintf(wg[w] * 1000), e[4 + w] = car->wheel[w].contact ? sim_body_kind(car->wheel[w].ground) : -1;
  }
}

/* every frame: pose the parts of each kart between the last two ticks */
void kartgfx_frame(const sim *s, float alpha) {
  if (!s) s = K.sim;
  if (!s) return;
  for (int i = 0; i < MAXK; i++) {
    const kart *k = &s->k[i];
    kart_rig *r = &K.rig[i];
    if (!k->on || !r->root) continue;
    const kstate *st = &K.st[i];
    const veh_geo *vg = r->vg;
    float P[KP_COUNT];
    for (int j = 0; j < KP_COUNT; j++) P[j] = lerp_(st->prev[j], st->cur[j], alpha);
    float tsec = ((float)s->tick + alpha) / (float)TICK_HZ;

    /* a kart far from every view loses what cannot be seen at that size: the arms and the scarf first, then the driver */
    float near2 = 1e12f;
    for (int e = 0; e < NEYE; e++) {
      float dx = k->p.x - EYE[e].x, dy = k->p.y - EYE[e].y, dz = k->p.z - EYE[e].z;
      near2 = fminf(near2, dx * dx + dy * dy + dz * dz);
    }
    bool mid = NEYE == 0 || near2 < 60.0f * 60.0f, close = NEYE == 0 || near2 < 120.0f * 120.0f;
    for (int a = 0; a < 2; a++) {
      if (r->arm_up[a]) r->arm_up[a]->visible = mid;
      if (r->arm_lo[a]) r->arm_lo[a]->visible = mid;
      if (r->hand[a]) r->hand[a]->visible = mid;
    }
    for (int a = 0; a < 4; a++) {
      if (r->scarf[a]) r->scarf[a]->visible = mid;
      if (r->strut[a]) r->strut[a]->visible = mid;
    }
    if (r->antenna) r->antenna->visible = mid;
    if (r->steer_wheel) r->steer_wheel->visible = mid;
    for (int w = 0; w < 4; w++)
      for (int p = 0; p < 2; p++) {
        r->wheel_hi[w][p]->base.visible = near2 < 40.0f * 40.0f || NEYE == 0;
        r->wheel_lo[w][p]->base.visible = !r->wheel_hi[w][p]->base.visible;
      }
    if (r->torso) r->torso->visible = close;
    if (r->head) r->head->visible = close;

    /* flicker while it comes back */
    r->root->visible = !(k->respawn_ticks > 0 && ((k->respawn_ticks / 4) & 1));
    r->shield_mat->opacity = 0;

    /* the root: trick and tumble about the body's middle */
    t3_vec3 cen = v3(0, 0.05f, 0);
    t3_quat qr = { 0, 0, 0, 1 };
    if (P[KP_TRICK] != 0) {
      switch (st->trick_kind) {
      case 0: qr = qaxis(0, 0, 1, P[KP_TRICK]); break;
      case 1: qr = qaxis(1, 0, 0, -P[KP_TRICK]); break;
      default: qr = qmul(qaxis(0, 1, 0, P[KP_TRICK]), qaxis(0, 0, 1, P[KP_TRICK] * 0.25f * sinf(P[KP_TRICK] * 0.5f))); break;
      }
    }
    if (P[KP_FLIP] != 0) qr = qmul(qr, qaxis(1, 0, 0, P[KP_FLIP]));
    t3_vec3 sh = v3(0, 0, 0);
    if (P[KP_SHAKE] > 0.5f) sh = v3(sinf(tsec * 91.0f) * 0.03f, sinf(tsec * 77.0f) * 0.03f, sinf(tsec * 83.0f) * 0.03f);
    t3_vec3 rp = v3add(v3sub(cen, t3_vec3_apply_quat(cen, qr)), v3add(v3(0, P[KP_HOP], 0), sh));
    t3_object_set_position(r->root, rp.x, rp.y, rp.z);
    t3_object_set_quaternion(r->root, qr);

    /* the body: roll, pitch, drift slew, squash, about a low axis */
    t3_quat qb = qmul(qaxis(0, 1, 0, P[KP_YAW]), qmul(qaxis(1, 0, 0, P[KP_PITCH]), qaxis(0, 0, 1, P[KP_ROLL])));
    t3_vec3 bc = v3(0, -0.3f, 0);
    float sq = P[KP_SQUASH];
    xf bx = { v3(0, 0, 0), qb, v3(1 + sq * 0.4f, 1 - sq, 1 + sq * 0.4f) };
    bx.p = v3add(v3sub(bc, t3_vec3_apply_quat(v3(bc.x * bx.s.x, bc.y * bx.s.y, bc.z * bx.s.z), qb)), v3(0, 0, 0));
    t3_object_set_position(r->body, bx.p.x, bx.p.y, bx.p.z);
    t3_object_set_quaternion(r->body, qb);
    t3_object_set_scale(r->body, bx.s.x, bx.s.y, bx.s.z);

    /* wheels: steer and spin from the vehicle controller */
    const cw_vehicle3d *car = sim_car(s, i);
    t3_vec3 wp[4];
    for (int w = 0; w < 4; w++) {
      cw_xform3 lw = cw_vehicle3d_wheel_local(car, w, alpha);
      /* its height from the casts under the drawn chassis (kartgfx_tick), not the controller's, which are a tick behind
       * what is drawn (sw wheel_lag 1: the controller's, the old way, the control of tests/racer-grounded.mjs) */
      float comp = car->wheel[w].compression;
      if (!kartgfx_wheel_lag) {
        const cw_wheel3d_def *wd = &car->def.wheels[w];
        comp = lerp_(st->wc_prev[w], st->wc[w], alpha);
        lw.p = v3sub(wd->anchor, v3mul(car->def.up, wd->travel - comp));
      }
      /* the tyre never climbs into its arch: a hard landing squeezes the springs up to 0.4 m, the arch has room for
       * about the first 0.16 of it, so the drawn wheel stops there (the physics keeps the whole travel) */
      float cex = comp - 0.16f;
      if (cex > 0) lw.p.y -= cex;
      t3_object_set_position(r->wheel[w], lw.p.x, lw.p.y, lw.p.z);
      t3_object_set_quaternion(r->wheel[w], lw.q);
      wp[w] = lw.p;
    }
    /* Pogo's springs: from the pod's corners down to each hub */
    if (vg->struts) {
      for (int w = 0; w < 4; w++) {
        t3_vec3 top = vg->strut_top[w < 2 ? 0 : 1];
        if (w & 1) top.x = -top.x;
        place_tube(r->strut[w], xf_apply(&bx, top), v3add(wp[w], v3(0, 0.02f, 0)));
      }
    }

    pose_driver(r, P, tsec, st->trick_kind == 0 || k->finished);
    rig_sync_parts(r);
    ground_probe(s, i, r, alpha);
    /* the shield bubble: pulses, flickers out in its last seconds */
    int sht = (int)P[KP_SHIELD];
    bool shield_on = sht > 0 && (sht > 90 || ((sht / 6) & 1));
    r->shield->visible = shield_on;
    if (shield_on) {
      float pulse = 1.0f + 0.03f * sinf(tsec * 7.0f);
      t3_object_set_scale(r->shield, pulse, pulse * 0.92f, pulse);
      r->shield_mat->opacity = 0.2f + 0.07f * sinf(tsec * 5.0f);
    }
  }
}

/* the driver, the scarf, the antenna, the flames and the lamps of one rig from its blended pose numbers (both_hands: a
 * cheer raises both arms, else only the left, a salute mid trick) */
static void pose_driver(kart_rig *r, const float *P, float tsec, bool both_hands) {
  {
    const veh_geo *vg = r->vg;
    float ds = vg->driver_scale;
    /* the driver: torso leans, head turns, hands on the wheel (or up in the air) */
    float lean = vg->torso_lean + P[KP_TLEAN];
    t3_quat qt = qmul(qaxis(0, 1, 0, P[KP_TTWIST]), qmul(qaxis(1, 0, 0, -lean), qaxis(0, 0, 1, P[KP_TROLL])));
    t3_vec3 tpos = v3add(vg->seat, v3(0, 0.1f * ds, -0.02f * ds));
    t3_object_set_quaternion(r->torso, qt);
    t3_quat qh = qmul(qaxis(0, 1, 0, P[KP_HEADYAW] - P[KP_TTWIST] * 0.6f), qmul(qaxis(1, 0, 0, P[KP_HEADPITCH] + lean * 0.7f), qaxis(0, 0, 1, -P[KP_TROLL] * 0.7f)));
    t3_object_set_quaternion(r->head, qh);
    float sw_ang = -P[KP_STEER] * 2.4f;
    t3_quat qw = qmul(qaxis(1, 0, 0, vg->wheel_tilt), qaxis(0, 0, 1, sw_ang));
    t3_object_set_quaternion(r->steer_wheel, qw);
    float L = 0.25f * ds;
    for (int side = 0; side < 2; side++) {
      float sg = side == 0 ? 1.0f : -1.0f; /* +x is the left hand */
      t3_vec3 S = v3add(tpos, t3_vec3_apply_quat(v3(sg * 0.19f * ds, 0.34f * ds, 0), qt));
      float phi = sg * 0.95f;
      t3_vec3 hl = v3(sinf(phi) * vg->wheel_r, cosf(phi) * vg->wheel_r, 0.0f);
      t3_vec3 H = v3add(vg->wheel_hub, t3_vec3_apply_quat(hl, qw));
      float ch = P[KP_CHEER];
      float wave = sinf(tsec * 13.0f + sg * 1.7f);
      if (ch > 0.01f) {
        t3_vec3 Hc = v3add(S, t3_vec3_apply_quat(v3(sg * (0.12f + 0.05f * wave) * ds, (0.5f + 0.04f * wave) * ds, 0.06f * ds), qt));
        if (both_hands) H = v3add(v3mul(H, 1 - ch), v3mul(Hc, ch));
        else if (side == 0) H = v3add(v3mul(H, 1 - ch), v3mul(Hc, ch)); /* a one-handed salute mid trick */
      }
      float fl = P[KP_FLAIL];
      if (fl > 0.01f) {
        t3_vec3 Hf = v3add(S, t3_vec3_apply_quat(v3(sg * (0.22f + 0.12f * sinf(tsec * 17.0f + sg)) * ds, (0.4f + 0.2f * sinf(tsec * 21.0f + sg * 2)) * ds, 0.1f * ds), qt));
        H = v3add(v3mul(H, 1 - fl), v3mul(Hf, fl));
      }
      arm(r, side, S, H, L, sg);
    }
    /* scarf */
    float spd = fabsf(P[KP_SPEED]);
    float stream = clampf_(spd / 28.0f, 0, 1);
    for (int j = 0; j < 4; j++) {
      float base = lerp_(-0.9f, 0.18f, stream);
      float rx = base * (j == 0 ? 0.6f : 1.0f) + sinf(tsec * (9 + 5 * stream) - (float)j * 1.0f) * 0.22f * (0.4f + stream);
      float rz = sinf(tsec * 7.0f - (float)j * 0.9f + 1.0f) * 0.25f * stream - P[KP_STEER] * 0.5f;
      t3_object_set_quaternion(r->scarf[j], qmul(qaxis(0, 0, 1, rz), qaxis(1, 0, 0, rx)));
    }
    if (r->prop) t3_object_set_rotation(r->prop, 0, tsec * (18.0f + spd * 0.7f), 0);
    if (r->antenna) {
      float a = clampf_(-P[KP_ACCEL] * 0.012f, -0.35f, 0.35f);
      t3_object_set_quaternion(r->antenna, qmul(qaxis(0, 0, 1, -P[KP_ROLL] * 1.5f + sinf(tsec * 11.0f) * 0.07f * stream), qaxis(1, 0, 0, a + sinf(tsec * 9.0f + 1) * 0.05f)));
    }

    /* flames: nested cones that stretch and flicker with the boost */
    float bo = P[KP_BOOST];
    for (int f = 0; f < vg->nflame; f++) {
      bool on = bo > 0.02f;
      float fk = 0.8f + 0.2f * sinf(tsec * 61.0f + (float)f * 2.1f) + 0.12f * sinf(tsec * 97.0f + (float)f);
      float len = (0.12f + 1.15f * bo) * fk;
      float rad = 0.07f + 0.1f * bo;
      for (int l = 0; l < 2; l++) {
        t3_object *fo = r->flame[f][l];
        if (!fo) continue;
        fo->visible = on;
        t3_object_set_scale(fo, rad * (l ? 0.55f : 1.0f) * 2.0f, rad * (l ? 0.55f : 1.0f) * 2.0f, len * (l ? 0.62f : 1.0f));
      }
    }
    /* tail lights brighten on the brakes */
    r->mat[S_TAIL]->emissive_intensity = 1.0f + 2.8f * P[KP_BRAKE] + 1.5f * (bo > 0.5f ? 1.0f : 0.0f);
    /* a shock lights the paint */
    r->mat[S_PAINT]->emissive_intensity = 2.2f * P[KP_FLASH];
    r->mat[S_PAINT2]->emissive = r->mat[S_PAINT]->emissive;
    r->mat[S_PAINT2]->emissive_intensity = 2.2f * P[KP_FLASH];
  }
}

/* ── the showroom: real karts held in front of a camera, where a menu draws them ── */

#define SHOW_RIDE 0.62f /* sim.c's RIDE: the body origin's height over the ground at rest */
#define SHOW_DEPTH 3.0f      /* metres in front of the eye: close, so the world seldom stands between (scaled down to fit) */
#define SHOW_BACK_DEPTH 4.2f /* the backdrop behind it */

typedef struct show_slot {
  kart_rig rig;
  cw_node node, back;
  t3_material *back_mat;
  show_kart want;
  bool wanted, built, stand;
  float pop; /* 0..1 since the kart was (re)built: it pops in */
} show_slot;

static struct {
  show_slot s[SHOW_MAX];
  cw_node cam;
} SR;

static void show_no_shadows(t3_object *o, void *u) {
  (void)u;
  o->cast_shadow = false; /* it still receives them: the same materials (and programs) as the race's karts */
}

static void show_drop(show_slot *sl) {
  if (cw_node_valid(sl->node)) cw_node_free(sl->node);
  if (sl->rig.root) rig_free(&sl->rig);
  sl->node = (cw_node){ 0 };
  sl->built = false;
}

static void show_drop_back(show_slot *sl) {
  if (cw_node_valid(sl->back)) cw_node_free(sl->back);
  if (sl->back_mat) t3_release(sl->back_mat);
  sl->back = (cw_node){ 0 };
  sl->back_mat = NULL;
}

/* The backdrop behind a kart on show: a unit pane facing the eye, unlit, warm where a spotlight falls on it low in the
 * middle and dark at the edges (the old 2D stage's colours). It is opaque and writes depth: the scene's fog and sky are
 * drawn after the transparent things and by depth, so a see-through pane darkening the scene was washed out again. */
static cw_node show_backdrop(cw_node cam, t3_material **mat_out) {
  enum { GX = 12, GY = 8 };
  mb m = { 0 };
  for (int y = 0; y <= GY; y++)
    for (int x = 0; x <= GX; x++) {
      float u = (float)x / GX - 0.5f, v = (float)y / GY - 0.5f;
      float dx = u * 1.3f, dy = (v + 0.12f) * 1.6f, k = clampf_(1.0f - sqrtf(dx * dx + dy * dy) * 1.25f, 0, 1);
      k = k * k * (3 - 2 * k);
      float floor = v < -0.28f ? clampf_((-0.28f - v) * 6.0f, 0, 1) : 0; /* a darker floor band along the bottom */
      float rgb[3] = { 0.1f + 0.2f * k, 0.08f + 0.15f * k, 0.09f + 0.1f * k };
      for (int c = 0; c < 3; c++) rgb[c] *= 1.0f - 0.45f * floor;
      t3_color l = lin(rgb[0], rgb[1], rgb[2]);
      float lc[3] = { l.r, l.g, l.b };
      mb_vert(&m, v3(u, v, 0), v3(0, 0, 1), lc);
    }
  for (int y = 0; y < GY; y++)
    for (int x = 0; x < GX; x++) {
      int a = y * (GX + 1) + x;
      mb_tri(&m, a, a + 1, a + GX + 2);
      mb_tri(&m, a, a + GX + 2, a + GX + 1);
    }
  t3_geometry *g = mb_done(&m);
  t3_material *mat = t3_mesh_basic_material_new(0xffffff);
  mat->vertex_colors = true, mat->fog = false;
  t3_mesh *pane = t3_mesh_new(g, mat);
  pane->base.cast_shadow = pane->base.receive_shadow = false;
  pane->base.frustum_culled = false; /* its bounds are the unit pane's, before the node scales it */
  t3_release(g);
  cw_node n = hang(cam, "ShowroomBack", &pane->base);
  cw_node_set(n, "interpolate", cw_val_bool(false));
  t3_release(pane);
  *mat_out = mat;
  return n;
}

/* the turntable under a kart on show: a dark steel plate on a rim in its paint, under the wheels */
static void show_turntable(kart_rig *r, const vehicle_def *vd) {
  float rad = vd->len * 1.3f, y = -SHOW_RIDE;
  static const float plate[3] = { 0.2f, 0.19f, 0.21f }, white[3] = { 1, 1, 1 };
  mb m = { 0 };
  mb_cyl(&m, xf_at(0, y - 0.03f, 0), rad, rad, 0.06f, 40, plate);
  t3_geometry *g = mb_done(&m);
  t3_mesh *me = t3_mesh_new(g, r->mat[S_METAL]);
  me->base.receive_shadow = true; /* as every part of the kart: the same programs */
  t3_object_add(r->root, me);
  t3_release(me), t3_release(g);
  mb rim = { 0 };
  mb_cyl(&rim, xf_at(0, y - 0.05f, 0), rad * 1.05f, rad * 1.07f, 0.07f, 40, white);
  g = mb_done(&rim);
  me = t3_mesh_new(g, r->mat[S_PAINT]);
  me->base.receive_shadow = true;
  t3_object_add(r->root, me);
  t3_release(me), t3_release(g);
}

void showroom_reset(void) {
  for (int i = 0; i < SHOW_MAX; i++) show_drop(&SR.s[i]), show_drop_back(&SR.s[i]);
  memset(&SR, 0, sizeof SR);
}

void showroom_want(int slot, const show_kart *k) {
  if (slot < 0 || slot >= SHOW_MAX) return;
  SR.s[slot].want = *k;
  SR.s[slot].wanted = true;
}

/* the rotation whose columns are the axes x, y, z */
static t3_quat quat_basis(t3_vec3 x, t3_vec3 y, t3_vec3 z) {
  float tr = x.x + y.y + z.z;
  t3_quat q;
  if (tr > 0) {
    float s = 0.5f / sqrtf(tr + 1.0f);
    q = (t3_quat){ (y.z - z.y) * s, (z.x - x.z) * s, (x.y - y.x) * s, 0.25f / s };
  } else if (x.x > y.y && x.x > z.z) {
    float s = 2.0f * sqrtf(1.0f + x.x - y.y - z.z);
    q = (t3_quat){ 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s };
  } else if (y.y > z.z) {
    float s = 2.0f * sqrtf(1.0f + y.y - x.x - z.z);
    q = (t3_quat){ (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s };
  } else {
    float s = 2.0f * sqrtf(1.0f + z.z - x.x - y.y);
    q = (t3_quat){ (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s };
  }
  return t3_quat_normalize(q);
}

void showroom_frame(cw_node cam, float fov_deg, float aspect) {
  /* a new camera (another scene): the old karts went with the old one */
  if (memcmp(&cam, &SR.cam, sizeof cam) != 0) {
    for (int i = 0; i < SHOW_MAX; i++) show_drop(&SR.s[i]), show_drop_back(&SR.s[i]);
    SR.cam = cam;
  }
  bool live = cw_node_valid(cam);
  float th = tanf(fov_deg * 0.5f * 0.0174533f);
  for (int i = 0; i < SHOW_MAX; i++) {
    show_slot *sl = &SR.s[i];
    const show_kart *w = &sl->want;
    if (!sl->wanted || !live) {
      if (cw_node_valid(sl->node)) cw_node_set(sl->node, "visible", cw_val_bool(false));
      if (cw_node_valid(sl->back)) cw_node_set(sl->back, "visible", cw_val_bool(false));
      sl->wanted = false;
      continue;
    }
    sl->wanted = false;
    /* the backdrop: an opaque pane behind it, lit like a stage (see show_backdrop) */
    if (w->back) {
      if (!cw_node_valid(sl->back)) {
        show_drop_back(sl);
        sl->back = show_backdrop(cam, &sl->back_mat);
      }
      float D = SHOW_BACK_DEPTH;
      t3_vec3 lo = v3(w->back_lo[0] * D * th * aspect, w->back_lo[1] * D * th, -D), hi = v3(w->back_hi[0] * D * th * aspect, w->back_hi[1] * D * th, -D);
      cw_node3d_set_position(sl->back, v3mul(v3add(lo, hi), 0.5f));
      cw_node3d_set_scale(sl->back, v3(fabsf(hi.x - lo.x), fabsf(hi.y - lo.y), 1));
      cw_node_set(sl->back, "visible", cw_val_bool(true));
    } else if (cw_node_valid(sl->back)) cw_node_set(sl->back, "visible", cw_val_bool(false));
    if (sl->built && (!cw_node_valid(sl->node) || sl->rig.veh != w->veh % NVEH || sl->rig.colour != (w->colour & 7) || sl->rig.number != w->number || sl->stand != w->stand))
      show_drop(sl);
    if (!sl->built) {
      rig_build(&sl->rig, w->veh, w->colour, w->number);
      if (w->stand) show_turntable(&sl->rig, &VEH[sl->rig.veh]);
      sl->node = hang(cam, "Showroom", sl->rig.root);
      cw_node_set(sl->node, "interpolate", cw_val_bool(false)); /* placed each frame in the camera's own frame */
      t3_release(sl->rig.root);
      /* it throws no shadow: it hangs in the air in front of the eye, it would throw one on the world below */
      t3_object_traverse(sl->rig.root, show_no_shadows, NULL);
      sl->built = true;
      sl->stand = w->stand;
      sl->pop = 0;
    }
    cw_node_set(sl->node, "visible", cw_val_bool(true));
    kart_rig *r = &sl->rig;
    const vehicle_def *vd = &VEH[r->veh];
    /* where it stands: the ground point on the view, SHOW_DEPTH in front of the eye (camera space: -z ahead, y up) */
    t3_vec3 g = v3(w->nx * SHOW_DEPTH * th * aspect, w->ny * SHOW_DEPTH * th, -SHOW_DEPTH);
    sl->pop = fminf(sl->pop + 1.0f / 14.0f, 1.0f);
    float p = sl->pop, pk = 1.0f + 0.12f * sinf(p * 3.14159265f); /* swells a little as it arrives */
    float sc = w->half * SHOW_DEPTH * th / (vd->len * 1.3f) * (0.55f + 0.45f * ease(p)) * pk;
    /* turned to face the eye along its own line of sight, so a kart to the side of the view is framed like one in the
     * middle; then side on (nose to the right) turned by yaw, its top tipped toward the eye by tilt */
    t3_vec3 bz = v3norm(v3mul(g, -1.0f));
    t3_vec3 by = v3norm(v3sub(v3(0, 1, 0), v3mul(bz, bz.y)));
    t3_vec3 bx = v3cross(by, bz);
    t3_quat q = qmul(quat_basis(bx, by, bz), qmul(qaxis(1, 0, 0, w->tilt), qaxis(0, 1, 0, 1.5707963f + w->yaw)));
    t3_vec3 up = t3_vec3_apply_quat(v3(0, 1, 0), q);
    cw_node3d_set_position(sl->node, v3add(g, v3mul(up, SHOW_RIDE * sc)));
    cw_node3d_set_quaternion(sl->node, q);
    cw_node3d_set_scale(sl->node, v3(sc, sc, sc));
    /* the pose: standing on its wheels, the driver at the wheel (or cheering), the exhaust lit for a boost */
    t3_object_set_position(r->root, 0, 0, 0);
    t3_object_set_quaternion(r->root, (t3_quat){ 0, 0, 0, 1 });
    t3_object_set_position(r->body, 0, 0, 0);
    t3_object_set_quaternion(r->body, (t3_quat){ 0, 0, 0, 1 });
    t3_vec3 wp[4];
    for (int k = 0; k < 4; k++) {
      float wb = vd->len * 0.72f, tw = vd->wid * 0.95f; /* where sim.c's car_make hangs the wheels */
      wp[k] = v3(k & 1 ? -tw : tw, -SHOW_RIDE + vd->wheel_r, k < 2 ? wb : -wb);
      t3_object_set_position(r->wheel[k], wp[k].x, wp[k].y, wp[k].z);
      t3_object_set_quaternion(r->wheel[k], qaxis(1, 0, 0, w->spin));
    }
    if (r->vg->struts)
      for (int k = 0; k < 4; k++) {
        t3_vec3 top = r->vg->strut_top[k < 2 ? 0 : 1];
        if (k & 1) top.x = -top.x;
        place_tube(r->strut[k], top, v3add(wp[k], v3(0, 0.02f, 0)));
      }
    r->shield->visible = false;
    float P[KP_COUNT] = { 0 };
    P[KP_CHEER] = w->cheer, P[KP_BOOST] = w->boost, P[KP_HEADYAW] = w->look;
    pose_driver(r, P, w->t, true);
    rig_sync_parts(r);
  }
}
