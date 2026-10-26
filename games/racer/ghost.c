/* racer's time trial ghost: see ghost.h. Recording, decoding, the gap to the ghost, and the translucent kart that
 * replays a run. Render only: the sim never reads any of this. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "ghost.h"
#include "models.h"

/* ── packing ─────────────────────────────────────────────────────────── */

static int32_t iround(float v) { return (int32_t)floorf(v + 0.5f); }

static uint32_t quat_pack(t3_quat q) {
  float c[4] = { q.x, q.y, q.z, q.w };
  float n = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] + c[3] * c[3]);
  if (n < 1e-6f) c[3] = 1, n = 1;
  int m = 0;
  for (int i = 0; i < 4; i++) {
    c[i] /= n;
    if (fabsf(c[i]) > fabsf(c[m])) m = i;
  }
  float sg = c[m] < 0 ? -1.0f : 1.0f;
  uint32_t out = (uint32_t)m << 30;
  int slot = 0;
  for (int i = 0; i < 4; i++) {
    if (i == m) continue;
    float u = c[i] * sg * 1.41421356f * 0.5f + 0.5f;
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    out |= (uint32_t)iround(u * 1023.0f) << (20 - 10 * slot);
    slot++;
  }
  return out;
}

static t3_quat quat_unpack(uint32_t v) {
  int m = (int)(v >> 30);
  float c[4] = { 0, 0, 0, 0 }, sum = 0;
  int slot = 0;
  for (int i = 0; i < 4; i++) {
    if (i == m) continue;
    float u = (float)((v >> (20 - 10 * slot)) & 1023u) / 1023.0f;
    c[i] = (u - 0.5f) * 2.0f * 0.70710678f;
    sum += c[i] * c[i];
    slot++;
  }
  c[m] = sqrtf(sum < 1.0f ? 1.0f - sum : 0.0f);
  return (t3_quat){ c[0], c[1], c[2], c[3] };
}

static uint32_t fnv(uint32_t h, const uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}

/* ── recording ───────────────────────────────────────────────────────── */

static struct {
  ghost_saved g;
  int32_t px, py, pz;
  int n;
  bool on, over;
} R;

void ghost_rec_start(const kart *k) {
  memset(&R, 0, sizeof R);
  R.on = true;
  R.g.veh = (uint8_t)k->veh;
  R.g.colour = (uint8_t)k->colour;
}

static void put16(uint8_t *b, int v) {
  if (v > 32767) v = 32767;
  if (v < -32768) v = -32768;
  b[0] = (uint8_t)(v & 255), b[1] = (uint8_t)((v >> 8) & 255);
}

void ghost_rec_tick(const kart *k, int race_ticks) {
  if (!R.on || race_ticks < 0 || race_ticks % GH_STEP) return;
  if (race_ticks / GH_STEP != R.n) return; /* a missed tick (a reload mid-run): not a whole recording */
  if (R.n >= GH_MAX) {
    R.on = false, R.over = true;
    return;
  }
  int32_t x = iround(k->p.x * 32.0f), y = iround(k->p.y * 32.0f), z = iround(k->p.z * 32.0f);
  if (R.n == 0) R.g.x0 = x, R.g.y0 = y, R.g.z0 = z, R.px = x, R.py = y, R.pz = z;
  uint8_t *b = R.g.data + R.n * GH_POSE;
  put16(b, x - R.px), put16(b + 2, y - R.py), put16(b + 4, z - R.pz);
  R.px = x, R.py = y, R.pz = z;
  uint32_t q = quat_pack(k->q);
  b[6] = (uint8_t)(q & 255), b[7] = (uint8_t)((q >> 8) & 255), b[8] = (uint8_t)((q >> 16) & 255), b[9] = (uint8_t)((q >> 24) & 255);
  R.n++;
}

int ghost_rec_count(void) { return R.n; }
uint32_t ghost_rec_hash(void) { return fnv(2166136261u, R.g.data, (size_t)R.n * GH_POSE); }

bool ghost_rec_commit(ghost_saved *dst, uint32_t time_cs) {
  if (R.over || R.n < 4 || R.n > GH_MAX) return false;
  memset(dst, 0, sizeof *dst);
  dst->time_cs = time_cs;
  dst->n = (uint16_t)R.n;
  dst->veh = R.g.veh, dst->colour = R.g.colour;
  dst->x0 = R.g.x0, dst->y0 = R.g.y0, dst->z0 = R.g.z0;
  memcpy(dst->data, R.g.data, (size_t)R.n * GH_POSE);
  return true;
}

/* ── playback ────────────────────────────────────────────────────────── */

static struct {
  bool on;
  int n;
  uint8_t veh, colour;
  uint32_t time_cs, hash;
  float px[GH_MAX], py[GH_MAX], pz[GH_MAX];
  t3_quat q[GH_MAX];
  float prog[GH_MAX]; /* metres along the run at each pose, never decreasing */
} P;

bool ghost_loaded(void) { return P.on; }
uint32_t ghost_time_cs(void) { return P.time_cs; }
uint32_t ghost_play_hash(void) { return P.on ? P.hash : 0; }
void ghost_unload(void) { P.on = false, P.n = 0; }

bool ghost_load(const ghost_saved *g, const track *t) {
  ghost_unload();
  if (!g || g->time_cs == 0 || g->n < 4 || g->n > GH_MAX || g->veh >= NVEH || g->colour > 7) return false;
  int32_t x = g->x0, y = g->y0, z = g->z0;
  int hint = -1;
  float L = t->length, prev_s = 0, run = 0;
  for (int i = 0; i < g->n; i++) {
    const uint8_t *b = g->data + i * GH_POSE;
    x += (int16_t)(b[0] | b[1] << 8), y += (int16_t)(b[2] | b[3] << 8), z += (int16_t)(b[4] | b[5] << 8);
    P.px[i] = (float)x / 32.0f, P.py[i] = (float)y / 32.0f, P.pz[i] = (float)z / 32.0f;
    P.q[i] = quat_unpack((uint32_t)b[6] | (uint32_t)b[7] << 8 | (uint32_t)b[8] << 16 | (uint32_t)b[9] << 24);
    track_loc loc;
    track_locate(t, (t3_vec3){ P.px[i], P.py[i], P.pz[i] }, hint, &loc);
    hint = loc.i;
    if (i == 0) {
      /* the grid is behind the line: the first pose counts as a little before zero */
      run = t->circuit && loc.ms > L * 0.5f ? loc.ms - L : loc.ms;
    } else {
      float ds = loc.ms - prev_s;
      if (t->circuit) {
        if (ds > L * 0.5f) ds -= L;
        if (ds < -L * 0.5f) ds += L;
      }
      if (fabsf(ds) < 60.0f) run += ds;
    }
    prev_s = loc.ms;
    P.prog[i] = i && run < P.prog[i - 1] ? P.prog[i - 1] : run;
  }
  P.n = g->n, P.veh = g->veh, P.colour = g->colour, P.time_cs = g->time_cs;
  P.hash = fnv(2166136261u, g->data, (size_t)g->n * GH_POSE) ^ (uint32_t)g->x0 ^ ((uint32_t)g->z0 << 7);
  P.on = true;
  return true;
}

static t3_vec3 catmull(t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, float t) {
  float t2 = t * t, t3 = t2 * t;
  t3_vec3 r;
  r.x = 0.5f * ((2 * b.x) + (-a.x + c.x) * t + (2 * a.x - 5 * b.x + 4 * c.x - d.x) * t2 + (-a.x + 3 * b.x - 3 * c.x + d.x) * t3);
  r.y = 0.5f * ((2 * b.y) + (-a.y + c.y) * t + (2 * a.y - 5 * b.y + 4 * c.y - d.y) * t2 + (-a.y + 3 * b.y - 3 * c.y + d.y) * t3);
  r.z = 0.5f * ((2 * b.z) + (-a.z + c.z) * t + (2 * a.z - 5 * b.z + 4 * c.z - d.z) * t2 + (-a.z + 3 * b.z - 3 * c.z + d.z) * t3);
  return r;
}
static t3_vec3 pt(int i) {
  if (i < 0) i = 0;
  if (i >= P.n) i = P.n - 1;
  return (t3_vec3){ P.px[i], P.py[i], P.pz[i] };
}

bool ghost_pose(float ticks, t3_vec3 *p, t3_quat *q) {
  if (!P.on) return false;
  float f = ticks < 0 ? 0 : ticks / (float)GH_STEP;
  if (f > (float)(P.n - 1)) return false;
  int i = (int)f;
  float t = f - (float)i;
  *p = catmull(pt(i - 1), pt(i), pt(i + 1), pt(i + 2), t);
  t3_quat a = P.q[i], b = P.q[i + 1 < P.n ? i + 1 : i];
  if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = (t3_quat){ -b.x, -b.y, -b.z, -b.w };
  t3_quat r = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
  float n = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
  if (n < 1e-6f) n = 1, r = a;
  *q = (t3_quat){ r.x / n, r.y / n, r.z / n, r.w / n };
  return true;
}

bool ghost_gap(float prog, int ticks, float *gap_secs) {
  if (!P.on) return false;
  if (prog < P.prog[0]) {
    *gap_secs = 0;
    return true;
  }
  if (prog > P.prog[P.n - 1]) return false;
  int lo = 0, hi = P.n - 1;
  while (hi - lo > 1) {
    int mid = (lo + hi) / 2;
    if (P.prog[mid] <= prog) lo = mid;
    else hi = mid;
  }
  float span = P.prog[hi] - P.prog[lo], u = span > 1e-4f ? (prog - P.prog[lo]) / span : 0;
  float tg = ((float)lo + u) * (float)GH_STEP / (float)TICK_HZ;
  *gap_secs = (float)ticks / (float)TICK_HZ - tg;
  return true;
}

/* ── drawing ─────────────────────────────────────────────────────────── */

static struct {
  kart_rig rig;
  cw_node node;
  bool built, shown;
  t3_vec3 last_p, fwd_last;
  float spin, spin_prev, steer, steer_prev;
  int ticks_seen;
} Z;

void ghost_gfx_reset(void) {
  if (Z.built) {
    if (cw_node_valid(Z.node)) cw_node_free(Z.node); /* (the scene frees what is left of it) */
    rig_free(&Z.rig);
  }
  memset(&Z, 0, sizeof Z);
}

static void ghostify(t3_object *o) {
  o->cast_shadow = false;
  o->receive_shadow = false;
  for (int i = 0; i < o->child_count; i++) ghostify(o->children[i]);
}

void ghost_gfx_build(cw_node parent) {
  ghost_gfx_reset();
  if (!P.on) return;
  rig_build(&Z.rig, P.veh, P.colour, 1);
  /* the ghost look: every part the same pale glassy blue, see through, without shadows */
  t3_color tint = lin(0.55f, 0.82f, 1.0f);
  for (int i = 0; i < S_COUNT; i++) {
    t3_material *m = Z.rig.mat[i];
    if (!m) continue;
    m->color = tint;
    m->emissive = tint;
    m->emissive_intensity = i == S_DARK ? 0.45f : 1.1f;
    m->transparent = true;
    m->opacity = i == S_DARK ? 0.62f : 0.55f;
    m->depth_write = true;
    m->clearcoat = 0;
    m->metalness = 0;
    m->roughness = 0.7f;
  }
  ghostify(Z.rig.root);
  Z.node = hang(parent, "Ghost", Z.rig.root);
  t3_release(Z.rig.root);
  Z.built = true;
  Z.shown = true;
  Z.rig.root->visible = false;
  /* the arms and the scarf are posed by the kart's own animation; the ghost drives with its hands off */
  for (int a = 0; a < 2; a++) {
    if (Z.rig.arm_up[a]) Z.rig.arm_up[a]->visible = false;
    if (Z.rig.arm_lo[a]) Z.rig.arm_lo[a]->visible = false;
    if (Z.rig.hand[a]) Z.rig.hand[a]->visible = false;
  }
  for (int a = 0; a < 4; a++)
    if (Z.rig.scarf[a]) Z.rig.scarf[a]->visible = false;
  const veh_geo *vg = Z.rig.vg;
  float lean = vg->torso_lean;
  if (Z.rig.torso) t3_object_set_quaternion(Z.rig.torso, t3_quat_from_axis_angle(v3(1, 0, 0), -lean));
  if (Z.rig.head) t3_object_set_quaternion(Z.rig.head, t3_quat_from_axis_angle(v3(1, 0, 0), lean * 0.7f));
  if (Z.rig.steer_wheel) t3_object_set_quaternion(Z.rig.steer_wheel, t3_quat_from_axis_angle(v3(1, 0, 0), vg->wheel_tilt));
  for (int w = 0; w < 4; w++) {
    Z.rig.wheel_lo[w][0]->base.visible = Z.rig.wheel_lo[w][1]->base.visible = false;
    Z.rig.wheel_hi[w][0]->base.visible = Z.rig.wheel_hi[w][1]->base.visible = true;
  }
}

void ghost_gfx_show(bool on) { Z.shown = on; }

void ghost_gfx_tick(const sim *s) {
  if (!Z.built) return;
  float ticks = s->phase == PH_RACE ? (float)s->race_ticks : 0.0f;
  t3_vec3 p;
  t3_quat q;
  bool alive = Z.shown && ghost_pose(ticks, &p, &q);
  Z.rig.root->visible = alive;
  if (!alive) return;
  cw_node3d_snapshot(Z.node);
  cw_node3d_set_position(Z.node, p);
  cw_node3d_set_quaternion(Z.node, q);
  if (s->phase != PH_RACE || Z.ticks_seen == 0) {
    /* the grid, or the first sight: no blend from wherever it was */
    cw_node3d_snapshot(Z.node);
    Z.last_p = p;
    Z.fwd_last = t3_vec3_apply_quat(v3(0, 0, 1), q);
    Z.ticks_seen = 1;
    Z.spin = Z.spin_prev = Z.steer = Z.steer_prev = 0;
    return;
  }
  /* the wheels roll with the distance covered and the front pair steers with the heading's turn */
  float dx = p.x - Z.last_p.x, dy = p.y - Z.last_p.y, dz = p.z - Z.last_p.z;
  float dist = sqrtf(dx * dx + dy * dy + dz * dz);
  t3_vec3 f = t3_vec3_apply_quat(v3(0, 0, 1), q), up = t3_vec3_apply_quat(v3(0, 1, 0), q);
  float yaw_rate = v3dot(v3cross(Z.fwd_last, f), up) * (float)TICK_HZ;
  float speed = dist * (float)TICK_HZ;
  float wb = VEH[P.veh].len * 1.44f;
  float st = speed > 2.0f ? wb * yaw_rate / speed : 0;
  if (st > 0.5f) st = 0.5f;
  if (st < -0.5f) st = -0.5f;
  Z.spin_prev = Z.spin, Z.steer_prev = Z.steer;
  Z.spin += dist / VEH[P.veh].wheel_r;
  Z.steer += (st - Z.steer) * 0.25f;
  Z.last_p = p;
  Z.fwd_last = f;
}

void ghost_gfx_frame(float alpha) {
  if (!Z.built || !Z.rig.root->visible) return;
  const vehicle_def *vd = &VEH[P.veh];
  const veh_geo *vg = Z.rig.vg;
  float wb = vd->len * 0.72f, tw = vd->wid * 0.95f, hub = vd->wheel_r - 0.62f;
  float spin = Z.spin_prev + (Z.spin - Z.spin_prev) * alpha, steer = Z.steer_prev + (Z.steer - Z.steer_prev) * alpha;
  t3_vec3 wp[4];
  for (int w = 0; w < 4; w++) {
    wp[w] = v3((w & 1) ? -tw : tw, hub, w < 2 ? wb : -wb);
    t3_quat qs = t3_quat_from_axis_angle(v3(1, 0, 0), spin);
    if (w < 2) qs = t3_quat_multiply(t3_quat_from_axis_angle(v3(0, 1, 0), steer), qs);
    t3_object_set_position(Z.rig.wheel[w], wp[w].x, wp[w].y, wp[w].z);
    t3_object_set_quaternion(Z.rig.wheel[w], qs);
  }
  if (vg->struts)
    for (int w = 0; w < 4; w++) {
      t3_vec3 top = vg->strut_top[w < 2 ? 0 : 1];
      if (w & 1) top.x = -top.x;
      place_tube(Z.rig.strut[w], top, v3add(wp[w], v3(0, 0.02f, 0)));
    }
}
