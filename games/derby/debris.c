/* derby: parts that come off. A bumper hanging by one bracket is knocked off by the next hard hit at its end; a
 * bonnet sprung off its catch is torn off by a big hit to the nose. The part on the car is hidden and a copy of it
 * (the same geometry and material) flies off, tumbles and lies on the dirt where it lands, a record of the fight.
 * Cars that drive into a lying part shove it along. Past MAX_DEBRIS the oldest part sinks into the dirt and goes.
 * Looking only: the parts are not in the physics world, so a bout replays exactly as before.
 *
 * Grounding: a part at rest lies on its flat side with only its yaw left, its lowest point on the floor (y 0): a
 * bumper on the bottom edge of its bar, a bonnet on its rim. debris.float_mm (debug) is the largest gap seen under a
 * resting part, which is zero by construction; the grounding sweep judges the pixels. */
#include <math.h>
#include <string.h>

#include "debris.h"
#include "world.h"

#define MAX_DEBRIS 12
enum { PART_BUMPER_F, PART_BUMPER_R, PART_HOOD, PARTS };

typedef struct piece {
  bool on, resting;
  int car, part, age, rest_ticks, sink;
  cw_node node;
  t3_vec3 p, v;          /* position (the part's pivot), velocity m/s */
  t3_quat q, spin_q;     /* orientation, and its turn per tick while it flies */
  float rest_y, yaw;     /* the pivot's height lying on the dirt; the yaw it settles to */
  int settle;            /* ticks of easing into the lying pose */
} piece;

static struct {
  piece d[MAX_DEBRIS];
  bool off[MAXC][PARTS];
  float zone_was[MAXC][ZONES];
  uint32_t rng;
  int32_t count, float_mm;
} D;

int32_t debris_count_dbg, debris_float_dbg;

void debris_reset(void) {
  memset(&D, 0, sizeof D);
  D.rng = 0x9e3779b9u;
  debris_count_dbg = debris_float_dbg = 0;
}

static float rnd(void) { /* 0..1, render-side (its own stream: the sim's is never touched) */
  D.rng ^= D.rng << 13, D.rng ^= D.rng >> 17, D.rng ^= D.rng << 5;
  return (float)(D.rng & 0xffffff) / 16777216.0f;
}

/* a copy of a part's meshes (shared geometry and material) under a new group */
static t3_object *clone_part(t3_object *src) {
  t3_object *g = t3_group_new();
  for (int i = 0; i < src->child_count; i++) {
    t3_object *ch = src->children[i];
    if (ch->type != T3_MESH) continue;
    t3_mesh *m = (t3_mesh *)ch, *c = t3_mesh_new(m->geometry, m->materials[0]);
    t3_object_set_position(c, ch->position.x, ch->position.y, ch->position.z);
    t3_object_set_quaternion(c, ch->quaternion);
    t3_object_set_scale(c, ch->scale.x, ch->scale.y, ch->scale.z);
    c->base.cast_shadow = c->base.receive_shadow = true;
    t3_object_add(g, c);
    t3_release(c);
  }
  return g;
}

/* the lowest point of a part's meshes in its own frame (its pivot at the origin, unrotated) */
static float part_bottom(t3_object *src) {
  float lo = 1e9f;
  for (int i = 0; i < src->child_count; i++) {
    t3_object *ch = src->children[i];
    if (ch->type != T3_MESH) continue;
    const t3_attribute *P = ((t3_mesh *)ch)->geometry->attributes[T3_ATTR_POSITION];
    const float *p = P->array;
    for (int v = 0; v < P->count; v++) {
      t3_vec3 w = t3_vec3_apply_quat((t3_vec3){ p[v * 3], p[v * 3 + 1], p[v * 3 + 2] }, ch->quaternion);
      lo = fminf(lo, w.y + ch->position.y);
    }
  }
  return lo < 1e8f ? lo : 0;
}

static void knock_off(const car *c, int ci, int part) {
  car_model *m = world_model(ci);
  t3_object *src = part == PART_HOOD ? m->hood : m->bumper[part];
  if (!src || D.off[ci][part]) return;
  /* the slot: a free one, else the oldest part sinks to make room */
  int k = -1, oldest = -1;
  for (int i = 0; i < MAX_DEBRIS; i++) {
    if (!D.d[i].on) { k = i; break; }
    if (!D.d[i].sink && (oldest < 0 || D.d[i].age > D.d[oldest].age)) oldest = i;
  }
  if (k < 0) {
    if (oldest >= 0) D.d[oldest].sink = 1;
    return; /* this part stays on until a slot frees (the car keeps it a while longer) */
  }
  D.off[ci][part] = true;
  piece *d = &D.d[k];
  memset(d, 0, sizeof *d);
  d->on = true, d->car = ci, d->part = part;
  /* where it was: the car's pose times the part's place on the body (the body sits at the car's origin) */
  t3_quat local = src->quaternion;
  d->q = t3_quat_multiply(c->q, local);
  d->p = t3_vec3_apply_quat(src->position, c->q);
  d->p.x += c->x, d->p.y += c->y, d->p.z += c->z;
  /* off it goes: the car's speed, away from the body, up, tumbling */
  float sh = cw_sin(c->h), ch = cw_cos(c->h), out = part == PART_BUMPER_R ? -1.0f : 1.0f;
  d->v = (t3_vec3){ c->vx * 0.6f + sh * out * 2.2f + (rnd() - 0.5f) * 1.5f, 2.2f + rnd() * 1.8f, c->vz * 0.6f + ch * out * 2.2f + (rnd() - 0.5f) * 1.5f };
  t3_vec3 ax = t3_vec3_normalize((t3_vec3){ rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f });
  d->spin_q = t3_quat_from_axis_angle(ax, 0.06f + rnd() * 0.1f);
  /* lying on the dirt: the bar on its bottom edge, the bonnet right side up; its lowest point at y 0 */
  d->rest_y = -part_bottom(src);
  d->yaw = c->h + (rnd() - 0.5f) * 1.6f;
  t3_object *g = clone_part(src);
  d->node = world_hang("Debris", g);
  t3_release(g);
  cw_node_set(d->node, "interpolate", cw_val_bool(true));
  cw_node3d_set_position(d->node, d->p);
  cw_node3d_set_quaternion(d->node, d->q);
  cw_node3d_snapshot(d->node);
  src->visible = false;
  if (part == PART_HOOD) m->lid_off = true;
  D.count++;
}

/* this tick's hits: a hanging bumper is knocked off by a hard hit at its end, a sprung bonnet by a big one at the nose */
static void hits(const sim *s) {
  for (int e = 0; e < s->ev_n; e++) {
    const struct sim_event *v = &s->ev[e];
    if (v->kind != EV_CRASH && v->kind != EV_WALL) continue;
    int who[2] = { v->a, v->kind == EV_CRASH ? v->b : -1 };
    for (int w = 0; w < 2; w++) {
      int i = who[w];
      if (i < 0 || i >= MAXC || !s->c[i].on) continue;
      const car *c = &s->c[i];
      car_model *m = world_model(i);
      float f, sd;
      car_local(c, v->at.x, v->at.z, &f, &sd);
      if (v->k > 4.5f && f > 0.5f && m->bumper_a[0] < -0.6f) knock_off(c, i, PART_BUMPER_F);
      if (v->k > 4.5f && f < -0.5f && m->bumper_a[1] < -0.6f) knock_off(c, i, PART_BUMPER_R);
      if (v->k > 7.0f && f > 0.6f && c->zone[ZONE_FRONT] > 0.8f && m->hood_a < -0.25f) knock_off(c, i, PART_HOOD);
    }
  }
}

void debris_tick(const sim *s) {
  hits(s);
  const float dt = 1.0f / TICK_HZ;
  int32_t worst = 0;
  for (int k = 0; k < MAX_DEBRIS; k++) {
    piece *d = &D.d[k];
    if (!d->on) continue;
    d->age++;
    cw_node3d_snapshot(d->node);
    if (d->sink) { /* into the dirt, then gone */
      d->p.y -= 0.004f;
      if (++d->sink > 120) { cw_node_free(d->node); d->on = false; continue; }
    } else if (!d->resting) {
      d->v.y -= 9.81f * dt;
      d->p.x += d->v.x * dt, d->p.y += d->v.y * dt, d->p.z += d->v.z * dt;
      d->q = t3_quat_normalize(t3_quat_multiply(d->spin_q, d->q));
      /* the wall throws it back */
      float nx, nz, sd = arena_sd(d->p.x, d->p.z, &nx, &nz);
      if (sd > -0.4f) { d->p.x -= nx * (sd + 0.4f), d->p.z -= nz * (sd + 0.4f); float vn = d->v.x * nx + d->v.z * nz; if (vn > 0) d->v.x -= 1.6f * vn * nx, d->v.z -= 1.6f * vn * nz; }
      if (d->p.y <= d->rest_y + 0.05f && d->v.y < 0) { /* down: a little skid, then it settles flat */
        d->p.y = d->rest_y;
        d->v.y = 0;
        d->v.x *= 0.5f, d->v.z *= 0.5f;
        d->resting = true;
        d->settle = 10;
      }
    } else {
      /* sliding to a stop on the dirt */
      d->p.x += d->v.x * dt, d->p.z += d->v.z * dt;
      d->v.x *= 0.88f, d->v.z *= 0.88f;
      t3_quat flat = t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, d->yaw);
      if (d->settle > 0) d->q = t3_quat_slerp(d->q, flat, 1.0f / (float)d->settle--);
      else d->q = flat;
      d->p.y = d->rest_y;
      /* a car driving into it shoves it along (and turns it) */
      for (int i = 0; i < MAXC; i++) {
        const car *c = &s->c[i];
        if (!c->on) continue;
        const car_spec *sp = &CAR_SPEC[c->kind];
        float f, sd;
        car_local(c, d->p.x, d->p.z, &f, &sd);
        if (fabsf(f) < sp->hl + 0.25f && fabsf(sd) < sp->hw + 0.25f && c->vx * c->vx + c->vz * c->vz > 1) {
          float side = sd >= 0 ? 1.0f : -1.0f, ch = cw_cos(c->h), shh = cw_sin(c->h);
          d->v.x = c->vx * 1.05f + ch * side * 1.2f, d->v.z = c->vz * 1.05f - shh * side * 1.2f;
          d->yaw += (rnd() - 0.5f) * 0.6f;
          d->settle = 6;
        }
      }
      float nx, nz, sd = arena_sd(d->p.x, d->p.z, &nx, &nz);
      if (sd > -0.4f) d->p.x -= nx * (sd + 0.4f), d->p.z -= nz * (sd + 0.4f), d->v.x = d->v.z = 0;
      /* the gap under it (0 when it lies flat): what the grounding check reads */
      if (d->settle == 0) { int32_t mm = (int32_t)(fabsf(d->p.y - d->rest_y) * 1000); if (mm > worst) worst = mm; }
    }
    cw_node3d_set_position(d->node, d->p);
    cw_node3d_set_quaternion(d->node, d->q);
  }
  if (worst > D.float_mm) D.float_mm = worst;
  debris_count_dbg = D.count;
  debris_float_dbg = D.float_mm;
}

/* the newest part lying on the dirt (a dev camera's target): false when none */
bool debris_resting(t3_vec3 *at) {
  int best = -1;
  for (int k = 0; k < MAX_DEBRIS; k++)
    if (D.d[k].on && D.d[k].resting && !D.d[k].sink && (best < 0 || D.d[k].age < D.d[best].age)) best = k;
  if (best < 0) return false;
  *at = D.d[best].p;
  return true;
}
