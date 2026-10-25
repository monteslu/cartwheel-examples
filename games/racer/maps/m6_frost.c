/* map: Frostbite Pass. A high mountain pass in the polar twilight: a start valley, a long climb up three
 * switchbacks on packed snow, a frozen waterfall with a corkscrew inside it at the summit, a glassy ice slide
 * down the far side, a rope-bridge crevasse jump, and the way home along the plateau. The sky is a star field
 * with a green and violet aurora that sways above jagged peaks catching the last pink light.
 *
 * The course (SPEC) is the coordinator's. Everything else here is the place around it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "../game.h"
#include "../props.h"
#include "../textures.h"

static const piece SPEC[] = {
  WALLS(1, 1),
  SURFACE(SURF_DIRT),
  ITEMS(),
  STRAIGHT(140),
  CHECK(),
  BOOST(30),
  STRAIGHT(30),
  SLOPE(60, 3.5),
  STRAIGHT(40),
  BANKED(150, 215, 20),
  STRAIGHT(50),
  ITEMS(),
  BANKED(-150, 215, -20),
  STRAIGHT(50),
  BANKED(150, 215, 20),
  SLOPE(50, 0),
  CHECK(),
  TUNNEL(1),
  STRAIGHT(30),
  WIDTH(30, 4.6f),
  CORKSCREW(190, 1),
  WIDTH(30, 9.0f),
  STRAIGHT(30),
  TUNNEL(0),
  SURFACE(SURF_ICE),
  BANKED(30, 200, 10),
  SLOPE(60, -12),
  STRAIGHT(120),
  ITEMS(),
  SLOPE(60, 0),
  BOOST(30),
  STRAIGHT(40),
  SLOPE(36, 20),
  SLOPE(22, 20),
  JUMP(0, 31),
  SLOPE(80, 0),
  SURFACE(SURF_DIRT),
  STRAIGHT(40),
  CHECK(),
  ABANKED(90, 140, 14),
  ASTRAIGHT(60),
  ITEMS(),
  BANKED(40, 120, 12),
  BANKED(-40, 120, -12),
  ABANKED(90, 140, 14),
  ASTRAIGHT(100),
  ASLOPE(50, 0),
  ASTRAIGHT(100),
  STRAIGHT(40),
  END(),
};

/* ── what the place is made of ─────────────────────────────────────────────────────────────────── */

#define TAU_F 6.2831853f

static float hashf2(int x, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)z * 19349663u ^ seed * 83492791u;
  h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
  return (float)(h & 0xffffff) / 16777215.0f;
}
static float rnd1(uint32_t *s) {
  *s = *s * 1664525u + 1013904223u;
  return (float)(*s >> 8) / 16777216.0f;
}
static float rnds(uint32_t *s) { return rnd1(s) * 2.0f - 1.0f; }
static t3_vec3 V3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }

/* the road landmarks the scenery hangs from, found by scanning the built track (so they follow the course) */
typedef struct landmarks {
  int tun0, tun1;            /* the ice cave: first and last tunnel sample */
  int cork0, cork1;          /* the corkscrew inside it */
  int gap0, gap1;            /* the crevasse jump: the flight */
  int ramp0;                 /* where the take-off ramp starts to climb */
  int ice0, ice1;            /* the glassy stretch */
  int slide0;                /* where the long ice slope starts */
  int nboost, boost0[6], boost1[6];
  float base_y;              /* the level the crevasse is cut into */
  t3_vec3 gmid, gfwd;        /* middle of the crevasse and its horizontal heading */
  float glen;
  t3_vec3 centre;            /* middle of the course */
} landmarks;

#define RF_CELL 28.0f
#define RF_W 64
#define RF_H 80

static struct {
  const track *t;
  landmarks lm;
  float rf[RF_W * RF_H];     /* the course's own heights, blurred: the lie of the land under and between the roads */
  float rf_x0, rf_z0;
  ground_opts g;
  float lake_cx, lake_cz, lake_rx, lake_rz, lake_y;
} F;

static void find_landmarks(const track *t) {
  landmarks *L = &F.lm;
  memset(L, 0, sizeof *L);
  L->tun0 = L->tun1 = L->cork0 = L->cork1 = -1;
  for (int i = 0; i < t->n; i++)
    if (t->s[i].flags & TF_TUNNEL) {
      if (L->tun0 < 0) L->tun0 = i;
      L->tun1 = i;
    }
  if (t->nloops > 0) L->cork0 = t->loop_from[0], L->cork1 = t->loop_to[0];
  if (t->ngaps > 0) L->gap0 = t->gap_from[0], L->gap1 = t->gap_to[0] < t->n ? t->gap_to[0] : t->n - 1;
  L->ice0 = L->ice1 = -1;
  for (int i = 0; i < t->n; i++)
    if (t->s[i].surf == SURF_ICE && !(t->s[i].flags & TF_GAP)) {
      if (L->ice0 < 0) L->ice0 = i;
      L->ice1 = i;
    }
  /* the ramp: walk back from the lip until the road stops climbing */
  int i = L->gap0;
  while (i > 0 && t->s[i].f.y > 0.02f) i--;
  L->ramp0 = i;
  L->base_y = t->s[i > 8 ? i - 8 : 0].p.y;
  /* the long slope: the first steep fall on ice */
  L->slide0 = L->ice0;
  for (int k = L->ice0; k >= 0 && k < t->n; k++)
    if (t->s[k].f.y < -0.15f) {
      L->slide0 = k;
      break;
    }
  bool in = false;
  for (int k = 0; k < t->n; k++) {
    bool b = (t->s[k].flags & TF_BOOST) != 0;
    if (b && !in && L->nboost < 6) L->boost0[L->nboost] = k, in = true;
    if (!b && in) L->boost1[L->nboost++] = k - 1, in = false;
  }
  const tsample *a = &t->s[(L->gap0 + L->gap1) / 2 < t->n ? (L->gap0 + L->gap1) / 2 : 0];
  const tsample *p0 = &t->s[L->gap0], *p1 = &t->s[L->gap1];
  L->gmid = V3((p0->p.x + p1->p.x) * 0.5f, L->base_y, (p0->p.z + p1->p.z) * 0.5f);
  float fx = p1->p.x - p0->p.x, fz = p1->p.z - p0->p.z, fl = sqrtf(fx * fx + fz * fz);
  if (fl < 1e-3f) fx = a->f.x, fz = a->f.z, fl = sqrtf(fx * fx + fz * fz);
  L->gfwd = V3(fx / fl, 0, fz / fl);
  L->glen = fl;
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (int k = 0; k < t->n; k++) {
    x0 = fminf(x0, t->s[k].p.x), x1 = fmaxf(x1, t->s[k].p.x), z0 = fminf(z0, t->s[k].p.z), z1 = fmaxf(z1, t->s[k].p.z);
  }
  L->centre = V3((x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f);
}

/* the blurred height of the road network: each cell is the average road height within about 120 m */
static void build_rf(const track *t) {
  float x0 = 1e9f, z0 = 1e9f;
  for (int k = 0; k < t->n; k++) x0 = fminf(x0, t->s[k].p.x), z0 = fminf(z0, t->s[k].p.z);
  F.rf_x0 = x0 - 400, F.rf_z0 = z0 - 400;
  const float sig2 = 2.0f * 120.0f * 120.0f;
  for (int j = 0; j < RF_H; j++)
    for (int i = 0; i < RF_W; i++) {
      float x = F.rf_x0 + (float)i * RF_CELL, z = F.rf_z0 + (float)j * RF_CELL;
      float sw = 0, sy = 0;
      for (int k = 0; k < t->n; k += 6) {
        float dx = t->s[k].p.x - x, dz = t->s[k].p.z - z, d2 = dx * dx + dz * dz;
        if (d2 > 9 * sig2) continue;
        float w = expf(-d2 / sig2);
        sw += w, sy += w * t->s[k].p.y;
      }
      F.rf[j * RF_W + i] = (sy + 0.02f * 6.0f) / (sw + 0.02f);
    }
}
static float rf_at(float x, float z) {
  float fx = (x - F.rf_x0) / RF_CELL, fz = (z - F.rf_z0) / RF_CELL;
  fx = cw_clampf(fx, 0, RF_W - 1.001f), fz = cw_clampf(fz, 0, RF_H - 1.001f);
  int ix = (int)fx, iz = (int)fz;
  float u = fx - (float)ix, v = fz - (float)iz;
  const float *p = &F.rf[iz * RF_W + ix];
  return (p[0] * (1 - u) + p[1] * u) * (1 - v) + (p[RF_W] * (1 - u) + p[RF_W + 1] * u) * v;
}

/* the land far from the road: the blurred course, a massif behind the climb, a wall of mountains all round and a
 * frozen lake in the middle of the circuit */
static float far_field(float x, float z) {
  float h = rf_at(x, z);
  const landmarks *L = &F.lm;
  /* the massif the climb goes up */
  float dx = (x - 430.0f) / 330.0f, dz = (z - 300.0f) / 300.0f;
  h += 62.0f * expf(-(dx * dx + dz * dz));
  /* the ring of mountains: they rise out of the pass walls */
  float ex = (x - L->centre.x) / 560.0f, ez = (z - L->centre.z) / 760.0f;
  float e = sqrtf(ex * ex + ez * ez);
  float ridge = 0.5f + 0.5f * ground_noise(x, z, 340.0f, 1.0f, 3, 71);
  h += (60.0f + 230.0f * ridge) * powf(cw_smoothstep(1.05f, 2.0f, e), 1.2f);
  h += 14.0f * cw_smoothstep(0.9f, 1.3f, e) * ground_noise(x, z, 90.0f, 1.0f, 3, 5);
  /* the lake */
  h += ground_basin(x, z, F.lake_cx, F.lake_cz, F.lake_rx, F.lake_rz, 22.0f, 55.0f);
  /* rolling snow: gentle drifts */
  h += ground_noise(x, z, 55.0f, 1.8f, 3, 11);
  return h;
}

static float terrain(float x, float z, void *ud) {
  (void)ud;
  const track *t = F.t;
  const landmarks *L = &F.lm;
  float ry = 0;
  int idx = 0;
  float gap = road_gap_idx(t, x, z, &ry, &idx);
  float H = far_field(x, z);
  float g;
  if (gap > 1e8f) {
    g = H;
  } else {
    const tsample *s = &t->s[idx];
    float lim = 8.0f + gap * 0.9f;
    float rb = ry;
    /* the take-off ramp and the landing hang over the plateau: the snow keeps its own level beneath them */
    if (idx > L->ramp0 - 6 && idx < L->gap1 + 90) rb = L->base_y;
    float hc = cw_clampf(H, rb - lim, rb + lim);
    float w = cw_smoothstep(1.5f, 44.0f, gap);
    g = (rb - 0.3f) * (1 - w) + hc * w;
    /* a bank of snow ploughed against the walls */
    float bd = (gap - 3.5f) / 2.6f;
    g += 0.55f * expf(-bd * bd);
    /* the mountain over the ice cave: solid rock, lower at the two mouths */
    if (L->tun0 >= 0 && idx >= L->tun0 - 14 && idx <= L->tun1 + 14) {
      float inside = fminf((float)(idx - L->tun0), (float)(L->tun1 - idx));
      float cover = (1.0f - cw_smoothstep(s->hw + 4.0f, s->hw + 46.0f, gap)) * cw_smoothstep(4.0f, 20.0f, inside);
      float top = ry + 13.0f + 3.5f * ground_noise(x, z, 24.0f, 1.0f, 2, 33);
      if (top > g) g = g + (top - g) * cover;
    }
  }
  /* the crevasse: a gorge cut across the pass under the jump */
  {
    float ax = x - L->gmid.x, az = z - L->gmid.z;
    float a = ax * L->gfwd.x + az * L->gfwd.z, b = -ax * L->gfwd.z + az * L->gfwd.x;
    float half = L->glen * 0.5f + 1.0f;
    float wob = 2.2f * ground_noise(x, z, 14.0f, 1.0f, 2, 9);
    float prof = 1.0f - cw_smoothstep(half - 1.0f + wob, half + 3.5f + wob, fabsf(a));
    float ends = 1.0f - cw_smoothstep(70.0f, 135.0f, fabsf(b));
    float d = 40.0f * prof * ends;
    /* the ground there is the plateau, not the mountain behind it */
    g -= d;
  }
  return g;
}

/* ── the sky: stars and the aurora ─────────────────────────────────────────────────────────────── */

#define STARS 2600
typedef struct curtain {
  t3_geometry *g;
  int nu, nv;
  float a0, a1;      /* where it stands round the course (radians) */
  float R, h0, H;    /* distance, height of its lower edge, how tall */
  float phase, speed;
  float hue;         /* 0 green, 1 violet, how far up the colours run */
} curtain;
#define NCURT 5
static struct {
  curtain c[NCURT];
  t3_material *aur_mat;
  t3_material *star_mat[3];
  cw_node env;
  float cx, cz;
} SKY;

static t3_object *make_stars(uint32_t seed, int first, int count, t3_material *m) {
  float *pos = malloc(sizeof(float) * 3 * (size_t)count), *col = malloc(sizeof(float) * 3 * (size_t)count);
  uint32_t r = seed;
  for (int i = 0; i < count; i++) {
    /* uniform on the sky above and a little below the horizon (the mountains hide it) */
    float y = rnd1(&r) * 1.08f - 0.08f, a = rnd1(&r) * TAU_F;
    float rr = sqrtf(1 - fminf(y, 1) * fminf(y, 1));
    float R = 1900.0f;
    pos[i * 3] = SKY.cx + cosf(a) * rr * R, pos[i * 3 + 1] = y * R, pos[i * 3 + 2] = SKY.cz + sinf(a) * rr * R;
    float b = 0.35f + 0.65f * powf(rnd1(&r), 3.0f);
    float warm = rnd1(&r);
    /* most stars are cold white, a few blue or gold */
    float cr = warm > 0.85f ? 1.0f : 0.78f + 0.2f * warm, cb = warm < 0.3f ? 1.0f : 0.9f;
    col[i * 3] = b * cr * 1.4f, col[i * 3 + 1] = b * 1.4f, col[i * 3 + 2] = b * cb * 1.4f;
    (void)first;
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, pos, count, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, col, count, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(g);
  free(pos), free(col);
  t3_mesh *pts = t3_points_new(g, m);
  pts->base.frustum_culled = false;
  t3_release(g);
  return &pts->base;
}

static void build_aurora(cw_node parent) {
  SKY.aur_mat = t3_mesh_basic_material_new(0xffffff);
  SKY.aur_mat->vertex_colors = true;
  SKY.aur_mat->transparent = true;
  SKY.aur_mat->blending = T3_ADDITIVE_BLENDING;
  SKY.aur_mat->depth_write = false;
  SKY.aur_mat->side = T3_DOUBLE_SIDE;
  SKY.aur_mat->fog = false;
  static const float A0[NCURT] = { 0.2f, 1.45f, 2.7f, 3.9f, 5.1f }, SPAN[NCURT] = { 1.5f, 1.3f, 1.4f, 1.2f, 1.4f };
  static const float R[NCURT] = { 1250, 1500, 1200, 1450, 1350 }, H0[NCURT] = { 330, 420, 290, 380, 350 }, HH[NCURT] = { 520, 600, 480, 560, 540 };
  for (int k = 0; k < NCURT; k++) {
    curtain *c = &SKY.c[k];
    c->nu = 120, c->nv = 9;
    c->a0 = A0[k], c->a1 = A0[k] + SPAN[k], c->R = R[k], c->h0 = H0[k], c->H = HH[k];
    c->phase = (float)k * 1.7f, c->speed = 0.7f + 0.18f * (float)k, c->hue = (float)k / (float)NCURT;
    int nv = (c->nu + 1) * (c->nv + 1);
    float *p = calloc((size_t)nv * 3, sizeof(float)), *col = calloc((size_t)nv * 3, sizeof(float));
    uint32_t *ix = malloc(sizeof(uint32_t) * 6 * (size_t)c->nu * (size_t)c->nv);
    int n = 0;
    for (int j = 0; j < c->nv; j++)
      for (int i = 0; i < c->nu; i++) {
        uint32_t a = (uint32_t)(j * (c->nu + 1) + i), b = a + 1, d = a + (uint32_t)(c->nu + 1), e = d + 1;
        ix[n++] = a, ix[n++] = d, ix[n++] = b, ix[n++] = b, ix[n++] = d, ix[n++] = e;
      }
    t3_geometry *g = t3_geometry_new();
    t3_attribute *at = t3_attribute_new(T3_FLOAT32, p, nv, 3);
    at->usage = T3_DYNAMIC_DRAW;
    t3_geometry_set_attribute(g, T3_ATTR_POSITION, at), t3_release(at);
    at = t3_attribute_new(T3_FLOAT32, col, nv, 3);
    at->usage = T3_DYNAMIC_DRAW;
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, at), t3_release(at);
    at = t3_attribute_new(T3_UINT32, ix, n, 1);
    t3_geometry_set_index(g, at), t3_release(at);
    free(p), free(col), free(ix);
    c->g = g;
    t3_mesh *me = t3_mesh_new(g, SKY.aur_mat);
    me->base.frustum_culled = false;
    me->base.cast_shadow = false;
    me->base.receive_shadow = false;
    me->base.render_order = -5;
    hang(parent, "Aurora", &me->base);
    t3_release(me);
  }
}

/* one curtain at time t: it drifts and bends, rays flicker along it, the lower edge is a bright seam and the
 * glow fades upward, from green at the bottom to violet and pink above */
static void aurora_update(curtain *c, float t) {
  float *P = t3_attribute_f32(c->g->attributes[T3_ATTR_POSITION]), *C = t3_attribute_f32(c->g->attributes[T3_ATTR_COLOR]);
  float surge = 0.75f + 0.45f * sinf(t * 0.11f + c->phase * 2.0f) * sinf(t * 0.047f + c->phase);
  for (int i = 0; i <= c->nu; i++) {
    float u = (float)i / (float)c->nu;
    float a = c->a0 + (c->a1 - c->a0) * u;
    float bend = 110.0f * sinf(u * 5.3f + t * 0.09f * c->speed + c->phase) + 45.0f * sinf(u * 12.7f - t * 0.21f * c->speed + c->phase * 3.0f);
    float r = c->R + bend;
    float x = SKY.cx + cosf(a) * r, z = SKY.cz + sinf(a) * r;
    /* rays: narrow bright columns that come and go */
    float ray = 0.5f + 0.5f * sinf(u * 83.0f + t * 0.8f * c->speed + sinf(u * 7.0f + t * 0.3f) * 2.0f);
    ray = 0.25f + 0.75f * ray * ray;
    float flick = 0.7f + 0.3f * sinf(u * 41.0f - t * 1.9f + c->phase);
    float body = 0.5f + 0.5f * sinf(u * 3.1f + t * 0.06f + c->phase * 2.3f);
    float lean = 70.0f * sinf(t * 0.13f * c->speed + u * 2.0f + c->phase);
    float hue_shift = 0.5f + 0.5f * sinf(u * 2.4f + t * 0.05f + c->phase);
    for (int j = 0; j <= c->nv; j++) {
      float v = (float)j / (float)c->nv;
      int k = (j * (c->nu + 1) + i) * 3;
      float hgt = c->h0 + c->H * powf(v, 1.15f);
      float ox = lean * v * cosf(a + 1.57f), oz = lean * v * sinf(a + 1.57f);
      P[k] = x + ox, P[k + 1] = hgt, P[k + 2] = z + oz;
      /* brightness: a sharp lower edge and a long fade up */
      float edge = cw_smoothstep(0.0f, 0.12f, v) * expf(-v * 2.3f) * (1.0f + 1.8f * expf(-v * 22.0f));
      float I = edge * ray * flick * (0.25f + 0.85f * body) * surge;
      float m = cw_clampf(v * 1.5f + (hue_shift - 0.5f) * 0.5f + c->hue * 0.15f, 0, 1);
      /* green (0.15, 1, 0.45) up through teal to violet (0.62, 0.25, 1) and a touch of pink */
      float r0 = 0.15f + 0.9f * m * m, g0 = 1.0f - 0.85f * m, b0 = 0.45f + 0.6f * m;
      C[k] = r0 * I * 1.5f, C[k + 1] = g0 * I * 1.5f, C[k + 2] = b0 * I * 1.5f;
    }
  }
  c->g->attributes[T3_ATTR_POSITION]->version++;
  c->g->attributes[T3_ATTR_COLOR]->version++;
  t3_geometry_compute_bounding_sphere(c->g);
}

/* ── the far mountains: faceted peaks whose tops hold the last pink light ────────────────────── */

static void peak_model(pm *b, float R, float Hh, uint32_t seed) {
  uint32_t r = seed;
  const int rings = 9, sect = 12;
  float ang0 = rnd1(&r) * TAU_F;
  /* each ridge line has its own height so the summit is a crown of crags, not a cone */
  float ridge[16];
  for (int i = 0; i < sect; i++) ridge[i] = 0.55f + 0.45f * rnd1(&r);
  t3_vec3 P[10][16];
  pcol col[10];
  for (int k = 0; k <= rings; k++) {
    float f = (float)k / (float)rings;
    float rad = R * powf(1.0f - f, 0.9f);
    for (int i = 0; i < sect; i++) {
      float a = ang0 + TAU_F * (float)i / (float)sect + 0.16f * rnds(&r);
      float rr = rad * (1.0f + 0.22f * rnds(&r) * (1.0f - f));
      float hy = Hh * powf(f, 0.8f) * (k == rings ? 1.0f : (0.78f + 0.22f * ridge[i]) + 0.05f * rnds(&r));
      if (k == rings) rr = 0, hy = Hh;
      P[k][i] = V3(cosf(a) * rr, hy, sinf(a) * rr);
    }
    /* rock at the foot, snow above, a blush of pink on the very top */
    pcol rock = hex(0x252c4a), snow = hex(0xc8d4f0), pink = hex(0xffb4c8);
    pcol c = pc_mix(rock, snow, cw_smoothstep(0.1f, 0.5f, f));
    c = pc_mix(c, pink, cw_smoothstep(0.74f, 1.0f, f) * 0.85f);
    col[k] = c;
  }
  for (int k = 0; k < rings; k++)
    for (int i = 0; i < sect; i++) {
      int j = (i + 1) % sect;
      pcol c = pc_mul(pc_mix(col[k], col[k + 1], 0.5f), 0.85f + 0.3f * rnd1(&r));
      pm_quad(b, P[k][i], P[k][j], P[k + 1][j], P[k + 1][i], c);
    }
}

/* ── the scenery ────────────────────────────────────────────────────────────────────────────────── */

static void build_peaks(cw_node parent) {
  pmc *c = pmc_new(600.0f, 41);
  pm_vary(pmc_at(c, V3(0, 0, 0)), 0.1f);
  uint32_t r = 5;
  for (int i = 0; i < 46; i++) {
    float a = TAU_F * (float)i / 46.0f + 0.07f * rnds(&r);
    float d = 1250.0f + 520.0f * rnd1(&r);
    float Hh = 260.0f + 360.0f * powf(rnd1(&r), 1.5f);
    float Rb = Hh * (0.8f + 0.5f * rnd1(&r));
    t3_vec3 p = V3(F.lm.centre.x + cosf(a) * d * 0.85f, -20.0f, F.lm.centre.z + sinf(a) * d * 1.1f);
    pm *b = pmc_at(c, p);
    pm_push(b);
    pm_move(b, p.x, p.y, p.z);
    pm_roty(b, rnd1(&r) * TAU_F);
    peak_model(b, Rb, Hh + 20.0f, 1000u + (uint32_t)i * 17u);
    pm_pop(b);
  }
  pmc_finish(c, parent, "Peaks", false, -1);
}

/* ── packed snow: our own road texture ───────────────────────────────────────────────────────────── */

/* value noise that wraps every px cells across and py cells down, so the texture tiles along the road */
static float vwrap(float x, float y, int px, int py, uint32_t seed) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float u = x - (float)ix, v = y - (float)iy;
  u = u * u * (3 - 2 * u), v = v * v * (3 - 2 * v);
  int x0 = ((ix % px) + px) % px, x1 = (x0 + 1) % px, y0 = ((iy % py) + py) % py, y1 = (y0 + 1) % py;
  float a = hashf2(x0, y0, seed), b = hashf2(x1, y0, seed), c = hashf2(x0, y1, seed), d = hashf2(x1, y1, seed);
  return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}
static float fwrap(float u, float v, int px, int py, int oct, uint32_t seed) {
  float sum = 0, a = 1, tot = 0;
  for (int i = 0; i < oct; i++) {
    sum += a * vwrap(u * (float)px, v * (float)py, px, py, seed + (uint32_t)i * 17u);
    tot += a, a *= 0.5f, px *= 2, py *= 2;
  }
  return sum / tot;
}
static uint8_t q8(float x) { return (uint8_t)(cw_clampf(x, 0, 1) * 255.0f + 0.5f); }

/* trodden snow: soft drifts, two tyre ruts packed hard and blue, a polished middle, wind ripples along the road
 * and a few glints. u runs across the road (one repeat), v along it (one repeat per 8 m). */
static void make_snow_road(t3_texture **map, t3_texture **nrm, t3_texture **rgh) {
  const int W = 512, H = 256;
  uint8_t *a = malloc((size_t)W * H * 4), *n = malloc((size_t)W * H * 4), *r = malloc((size_t)W * H * 4);
  float *ht = malloc(sizeof(float) * (size_t)W * H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float u = ((float)x + 0.5f) / W, v = ((float)y + 0.5f) / H;
      float drift = fwrap(u, v, 3, 2, 4, 5);
      float ripple = fwrap(u, v, 44, 3, 2, 9);
      float fine = hashf2(x, y, 3);
      float rut = 0;
      for (int k = 0; k < 2; k++) {
        float c = k ? 0.73f : 0.27f, d = (u - c) / 0.05f;
        float wob = 0.012f * sinf(v * TAU_F * 2.0f + (float)k * 2.0f);
        d = (u - c - wob) / 0.05f;
        rut += expf(-d * d);
      }
      rut = cw_clampf(rut, 0, 1) * (0.7f + 0.3f * fwrap(u, v, 8, 4, 2, 21));
      float centre = expf(-powf((u - 0.5f) / 0.1f, 2.0f));
      float edge = cw_smoothstep(0.1f, 0.0f, u) + cw_smoothstep(0.9f, 1.0f, u);
      float tread = 0.5f + 0.5f * sinf(v * TAU_F * 16.0f + u * 40.0f);
      float h = 0.5f + 0.18f * (drift - 0.5f) + 0.07f * (ripple - 0.5f) - 0.3f * rut - 0.04f * rut * tread + 0.1f * edge;
      float k = 0.9f + 0.14f * (drift - 0.5f) + 0.05f * (ripple - 0.5f);
      float R = 0.88f * k, G = 0.93f * k, B = 1.0f * k;
      /* ruts: compressed, shadowed, bluer */
      R = R + (0.56f - R) * rut * 0.75f, G = G + (0.67f - G) * rut * 0.75f, B = B + (0.86f - B) * rut * 0.75f;
      /* the middle is polished by passing karts, the verges are fluffy drifts */
      R += 0.04f * centre, G += 0.04f * centre, B += 0.03f * centre;
      R += 0.06f * edge, G += 0.06f * edge, B += 0.05f * edge;
      float glint = hashf2(x * 7 + 1, y * 3 + 2, 77) > 0.9965f ? 0.5f : 0.0f;
      R += glint, G += glint, B += glint;
      size_t i = ((size_t)y * W + x) * 4;
      a[i] = q8(R), a[i + 1] = q8(G), a[i + 2] = q8(B), a[i + 3] = 255;
      float rough = 0.82f - 0.28f * rut - 0.12f * centre - 0.5f * glint + 0.08f * (fine - 0.5f);
      r[i] = r[i + 1] = r[i + 2] = q8(rough), r[i + 3] = 255;
      ht[y * W + x] = h;
    }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float hx = ht[y * W + (x + 1) % W] - ht[y * W + (x + W - 1) % W], hy = ht[((y + 1) % H) * W + x] - ht[((y + H - 1) % H) * W + x];
      float nx = -hx * 14.0f, ny = -hy * 14.0f, l = sqrtf(nx * nx + ny * ny + 1);
      size_t i = ((size_t)y * W + x) * 4;
      n[i] = q8(nx / l * 0.5f + 0.5f), n[i + 1] = q8(ny / l * 0.5f + 0.5f), n[i + 2] = q8(1 / l * 0.5f + 0.5f), n[i + 3] = 255;
    }
  t3_texture **out[3] = { map, nrm, rgh };
  uint8_t *src[3] = { a, n, r };
  if (getenv("FROST_DUMP")) {
    for (int k = 0; k < 3; k++) {
      char path[256];
      snprintf(path, sizeof path, "%s-%d.ppm", getenv("FROST_DUMP"), k);
      FILE *f = fopen(path, "wb");
      if (!f) continue;
      fprintf(f, "P6 %d %d 255\n", W, H);
      for (int i = 0; i < W * H; i++) fwrite(src[k] + i * 4, 1, 3, f);
      fclose(f);
    }
  }
  for (int k = 0; k < 3; k++) {
    t3_texture *t = t3_texture_new(W, H, src[k]);
    t->wrap_s = t->wrap_t = T3_REPEAT;
    t->flip_y = false;
    t->generate_mipmaps = true;
    t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
    t->mag_filter = T3_LINEAR;
    t->anisotropy = 16;
    t->color_space = k == 0 ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
    *out[k] = t;
  }
  free(a), free(n), free(r), free(ht);
}

/* The world builder gives every road surface one stock material per chunk. Packed snow is the dirt road's
 * ruts and ridges lit blue-white, ice is the glossy road with a clear coat, and the cave lining and its lamp
 * strip take the cold colours of the place. Only colours and finish change; nothing is added. */
static t3_mesh *chunk_mesh(cw_node world, int i, int kind) {
  char name[24];
  snprintf(name, sizeof name, "T%d_%d", kind, i);
  cw_node n = cw_node_get(world, name);
  if (!cw_node_valid(n)) return NULL;
  t3_object *o = cw_node3d_object(n);
  if (!o || o->child_count < 1) return NULL;
  t3_object *c = o->children[0];
  return c->type == T3_MESH ? (t3_mesh *)c : NULL;
}

static void restyle_world(cw_node scene) {
  cw_node world = cw_node_get(scene, "World");
  const track_geo *geo = world_geo();
  if (!cw_node_valid(world) || !geo) return;
  for (int i = 0; i < geo->n; i++) {
    const track_chunk *c = &geo->c[i];
    t3_mesh *me = chunk_mesh(world, i, c->kind);
    if (!me || me->material_count < 1) continue;
    t3_material *m = me->materials[0];
    switch (c->kind) {
    case TM_ROAD:
      if (c->surf == SURF_DIRT) {
        /* packed snow: the snow texture, a little blue, firm under the wheels */
        static t3_texture *sm, *sn, *sr;
        if (!sm) make_snow_road(&sm, &sn, &sr);
        t3_material_set_texture(m, T3_MAP, sm);
        t3_material_set_texture(m, T3_NORMAL_MAP, sn);
        t3_material_set_texture(m, T3_ROUGHNESS_MAP, sr);
        t3_material_set_texture(m, T3_AO_MAP, NULL);
        m->normal_scale = (t3_vec2){ 1.0f, 1.0f };
        m->color = (t3_color){ 0.78f, 0.84f, 0.95f };
        m->roughness = 1.0f;
      } else if (c->surf == SURF_ICE) {
        m->color = (t3_color){ 0.72f, 1.0f, 1.35f };
        m->roughness = 0.5f;
        m->clearcoat = 1.0f;
        m->clearcoat_roughness = 0.04f;
        m->specular_intensity = 1.0f;
      }
      break;
    case TM_TUNNEL:
      m->color = (t3_color){ 0.35f, 0.75f, 1.25f };
      m->emissive = (t3_color){ 0.12f, 0.62f, 1.0f };
      m->emissive_intensity = 2.6f;
      break;
    default: break;
    }
  }
}

/* ── helpers for building in the road's frame ───────────────────────────────────────────────────── */

static t3_vec3 vsub3(t3_vec3 a, t3_vec3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 vadd3(t3_vec3 a, t3_vec3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vmul3(t3_vec3 a, float k) { return V3(a.x * k, a.y * k, a.z * k); }
static t3_vec3 vnorm3(t3_vec3 a) {
  float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  return l > 1e-6f ? vmul3(a, 1.0f / l) : V3(0, 1, 0);
}

/* the builder of the cell that holds p, put into the road's local frame at lateral lat and height lift (x right, y up, z forward) */
static pm *frame_at(pmc *c, const tsample *a, float lat, float lift) {
  t3_vec3 p = vadd3(vadd3(a->p, vmul3(a->r, lat)), vmul3(a->u, lift));
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_basis(b, p, a->r, a->u, a->f);
  return b;
}
/* the same but standing upright: y is world up, z the road's forward flattened */
static pm *upright_at(pmc *c, t3_vec3 p, const tsample *a, float yaw_extra) {
  pm *b = pmc_at(c, p);
  pm_push(b);
  t3_vec3 r = vnorm3(V3(a->r.x, 0, a->r.z)), f = vnorm3(V3(a->f.x, 0, a->f.z));
  pm_basis(b, p, r, V3(0, 1, 0), f);
  if (yaw_extra != 0) pm_roty(b, yaw_extra);
  return b;
}
/* a builder standing at world point p, turned yaw about the vertical */
static pm *stand_at(pmc *c, t3_vec3 p, float yaw) {
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_move(b, p.x, p.y, p.z);
  pm_roty(b, yaw);
  return b;
}

static pcol ICE_LO, ICE_HI, ICE_DEEP, SNOW_C, LANT_C, CYAN_G;
static void init_colours(void) {
  ICE_LO = hex(0x5ea8d8), ICE_HI = hex(0xe6f6ff), ICE_DEEP = hex(0x2d6fa6);
  SNOW_C = hex(0xf2f8ff), LANT_C = hex(0xffa63a), CYAN_G = hex(0x3de8ff);
}

/* a chunky ice block, a little off true, with a frost cap */
static void ice_block(pm *b, float w, float h, float d, float tilt, pcol lo, pcol hi) {
  pm_push(b);
  pm_rotz(b, tilt * 0.4f);
  pm_roty(b, tilt);
  pm_box(b, w, h, d, lo, hi);
  pm_pop(b);
}

/* an icicle hanging down from the origin */
static void icicle(pm *b, float r, float len, pcol hi) {
  pm_push(b);
  pm_rotx(b, CW_PI);
  pm_cyl_ex(b, r, 0.001f, len, 5, pc_mul(hi, 0.8f), hi, false, false);
  pm_pop(b);
}

/* a hanging lantern: a glass cage with a warm flame inside. Built once per call into the builder's layers. */
static void lantern(pm *b, float s, pcol flame, float gain) {
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_scale(b, s, s, s);
  pm_box(b, 0.5f, 0.07f, 0.5f, hex(0x2a2a30), hex(0x3a3a42));
  pm_move(b, 0, 0.95f, 0);
  pm_box(b, 0.5f, 0.07f, 0.5f, hex(0x2a2a30), hex(0x3a3a42));
  pm_cyl_ex(b, 0.28f, 0.02f, 0.3f, 4, hex(0x2a2a30), hex(0x44444c), false, false);
  pm_pop(b);
  pm_push(b);
  pm_scale(b, s, s, s);
  for (int i = 0; i < 4; i++) {
    float x = (i & 1) ? 0.22f : -0.22f, z = (i & 2) ? 0.22f : -0.22f;
    pm_beam(b, V3(x, 0.04f, z), V3(x, 0.96f, z), 0.045f, hex(0x2a2a30));
  }
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_scale(b, s, s, s);
  pm_move(b, 0, 0.5f, 0);
  pm_ball(b, 0.2f, 0.34f, 0.2f, 1, 0.0f, glow(flame, gain), glow(pc_mix(flame, hex(0xffe9b0), 0.5f), gain * 1.25f));
  pm_pop(b);
  pm_layer(b, PL_MAIN);
}

/* a pennant: a cloth triangle on a pole, two faces */
static void pennant(pm *b, float len, pcol c) {
  pm_tri(b, V3(0, 0, 0), V3(0, -len * 0.4f, 0), V3(len, -len * 0.2f, 0), c);
  pm_tri(b, V3(0, -len * 0.4f, 0), V3(0, 0, 0), V3(len, -len * 0.2f, 0), c);
}

static void chequer_quads(pm *b, float x0, float x1, float y0, float y1, float z, float cell, bool front, pcol a, pcol c) {
  int nx = (int)((x1 - x0) / cell + 0.5f), ny = (int)((y1 - y0) / cell + 0.5f);
  if (nx < 1) nx = 1;
  if (ny < 1) ny = 1;
  float dx = (x1 - x0) / (float)nx, dy = (y1 - y0) / (float)ny;
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      pcol col = ((i + j) & 1) ? a : c;
      float xa = x0 + dx * (float)i, xb = xa + dx, ya = y0 + dy * (float)j, yb = ya + dy;
      if (front) pm_quad(b, V3(xa, ya, z), V3(xb, ya, z), V3(xb, yb, z), V3(xa, yb, z), col);
      else pm_quad(b, V3(xb, ya, z), V3(xa, ya, z), V3(xa, yb, z), V3(xb, yb, z), col);
    }
}

/* ── things that move: lanterns, the mast light, flags ──────────────────────────────────────────── */

#define MAXANIM 24
static struct {
  t3_material *flame[3];        /* the three lantern groups: they flicker out of step */
  t3_material *beacon;          /* the radio mast's red light */
  t3_material *crev;            /* the crevasse's cyan glow */
  t3_object *flags[MAXANIM];    /* strings of prayer flags that flutter */
  float flag_ph[MAXANIM];
  int nflags;
  cw_node lights[4];
  float light_e[4];
  int nlights;
  cw_node gantry_lamp;
} AN;

static t3_material *glow_material(void) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  return m;
}
/* puts a built model's glow layer on a node with its own animated material; the other layers go to the shared ones */
static t3_object *model_mesh(t3_geometry *g, t3_material *m, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = true;
  return &me->base;
}

/* a light that stays: a warm or cold pool round a landmark */
static cw_node point_light(cw_node parent, const char *name, t3_vec3 at, float r, float g, float b, float energy, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(parent, l);
  cw_node3d_set_position(l, at);
  cw_node_set(l, "color", cw_val_color(r, g, b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  cw_node_set(l, "fog_energy", cw_val_float(0));
  if (AN.nlights < 4) AN.lights[AN.nlights] = l, AN.light_e[AN.nlights++] = energy;
  return l;
}

/* ── the start: an ice-block gantry with lanterns ───────────────────────────────────────────────── */

static void build_start(cw_node root, const track *t) {
  const tsample *a = &t->s[t->nchecks > 0 ? t->check_at[0] : 0];
  float hw = a->hw, px = hw + 2.3f, H = 9.4f;
  uint32_t r = 31;
  /* the ice: two leaning pillars of stacked blocks and a lintel of blocks across the road */
  pmc *ice = pmc_new(80, 91);
  pm *b = frame_at(ice, a, 0, 0);
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.07f);
  for (int side = -1; side <= 1; side += 2) {
    float y = -1.0f;
    for (int k = 0; k < 5; k++) {
      float w = 3.7f - 0.22f * (float)k, h = 2.15f + 0.2f * rnd1(&r);
      pm_push(b);
      pm_move(b, side * (px + 0.18f * rnds(&r)), y, 0.15f * rnds(&r));
      ice_block(b, w, h, 3.1f - 0.1f * (float)k, 0.1f * rnds(&r), pc_mix(ICE_DEEP, ICE_LO, (float)k / 5.0f), pc_mix(ICE_LO, ICE_HI, 0.35f + 0.12f * (float)k));
      pm_pop(b);
      y += h * 0.93f;
    }
    /* a snow cap and a sunk base of drift */
    pm_push(b);
    pm_move(b, side * px, y - 0.15f, 0);
    pm_ball(b, 2.0f, 0.7f, 1.8f, 1, 0.12f, SNOW_C, SNOW_C);
    pm_pop(b);
    pm_push(b);
    pm_move(b, side * px, -1.4f, 0);
    pm_ball(b, 3.6f, 1.2f, 3.2f, 1, 0.14f, pc_mix(SNOW_C, ICE_LO, 0.3f), SNOW_C);
    pm_pop(b);
  }
  float x = -px;
  while (x < px + 0.5f) {
    float w = 2.5f + 0.4f * rnd1(&r);
    pm_push(b);
    pm_move(b, x + w * 0.5f, H - 0.55f + 0.1f * rnds(&r), 0.1f * rnds(&r));
    ice_block(b, w, 2.0f + 0.25f * rnd1(&r), 3.0f, 0.06f * rnds(&r), pc_mix(ICE_DEEP, ICE_LO, 0.5f), pc_mix(ICE_LO, ICE_HI, 0.55f));
    pm_pop(b);
    x += w * 0.92f;
  }
  /* snow heaped on the lintel, icicles under it */
  for (float xx = -px + 1.2f; xx < px; xx += 2.4f) {
    pm_push(b);
    pm_move(b, xx, H + 1.5f, 0);
    pm_ball(b, 1.5f + 0.3f * rnd1(&r), 0.55f, 1.6f, 1, 0.14f, SNOW_C, SNOW_C);
    pm_pop(b);
  }
  for (int i = 0; i < 26; i++) {
    pm_push(b);
    pm_move(b, -px + 2.0f * px * rnd1(&r), H - 1.4f, -1.1f + 2.2f * rnd1(&r));
    icicle(b, 0.09f + 0.1f * rnd1(&r), 0.6f + 1.8f * rnd1(&r), ICE_HI);
    pm_pop(b);
  }
  pm_pop(b);
  pmc_finish(ice, root, "StartIce", true, TEX_WALL_ICE);

  /* the cloth: a chequered banner under the lintel, pennants in the aurora's colours on the pillars */
  pmc *cloth = pmc_new(80, 92);
  b = frame_at(cloth, a, 0, 0);
  pm_layer(b, PL_MAIN);
  pcol blk = rgb(0.06f, 0.07f, 0.1f), wht = rgb(0.95f, 0.97f, 1.0f);
  chequer_quads(b, -hw, hw, H - 3.1f, H - 0.7f, 0.0f, 0.5f, false, blk, wht);
  chequer_quads(b, -hw, hw, H - 3.1f, H - 0.7f, 0.0f, 0.5f, true, blk, wht);
  static const uint32_t FLAGC[5] = { 0x3dd6a0, 0x7a5cff, 0xff6ec7, 0x3de8ff, 0xffd166 };
  for (int side = -1; side <= 1; side += 2) {
    pm_layer(b, PL_METAL);
    pm_beam(b, V3(side * px, H + 2.2f, 0), V3(side * px, H + 8.0f, 0), 0.14f, hex(0x5a6070));
    pm_layer(b, PL_MAIN);
    for (int k = 0; k < 4; k++) {
      pm_push(b);
      pm_move(b, side * px + (side > 0 ? 0.07f : -0.07f), H + 7.6f - 1.3f * (float)k, 0);
      if (side < 0) pm_roty(b, CW_PI);
      pennant(b, 2.6f - 0.3f * (float)k, hex(FLAGC[(k + (side > 0 ? 2 : 0)) % 5]));
      pm_pop(b);
    }
  }
  /* the painted line across the road and the grid slots behind it */
  int nc = (int)(2 * hw / 0.5f);
  for (int i = 0; i < nc; i++)
    for (int j = 0; j < 2; j++) {
      float x0 = -hw + 0.5f * (float)i, z0 = -0.5f + 0.5f * (float)j;
      pm_quad(b, V3(x0, 0.035f, z0 + 0.5f), V3(x0 + 0.5f, 0.035f, z0 + 0.5f), V3(x0 + 0.5f, 0.035f, z0), V3(x0, 0.035f, z0), ((i + j) & 1) ? blk : wht);
    }
  pm_pop(b);
  if (t->circuit) {
    float lat = fminf(hw * 0.42f, 3.4f);
    for (int k = 0; k < 8; k++) {
      int row = k / 2, col = k & 1;
      float back = 8.0f + (float)row * 8.0f + (col ? 4.0f : 0.0f);
      float ss = fmodf(a->s - back + t->length, t->length);
      tsample sl = track_at(t, ss);
      if (sl.flags & (TF_GAP | TF_LOOP)) continue;
      float xs = (col ? 1.0f : -1.0f) * lat;
      pm *g = frame_at(cloth, &sl, 0, 0);
      float w = 1.25f, l = 2.3f, lw = 0.12f;
      pcol line = rgb(0.9f, 0.95f, 1.0f);
      pm_quad(g, V3(xs - w, 0.03f, -l + lw), V3(xs + w, 0.03f, -l + lw), V3(xs + w, 0.03f, -l), V3(xs - w, 0.03f, -l), line);
      pm_quad(g, V3(xs - w, 0.03f, l * 0.35f), V3(xs - w + lw, 0.03f, l * 0.35f), V3(xs - w + lw, 0.03f, -l), V3(xs - w, 0.03f, -l), line);
      pm_quad(g, V3(xs + w - lw, 0.03f, l * 0.35f), V3(xs + w, 0.03f, l * 0.35f), V3(xs + w, 0.03f, -l), V3(xs + w - lw, 0.03f, -l), line);
      pm_pop(g);
    }
  }
  pmc_finish(cloth, root, "StartCloth", true, -1);

  /* the lanterns: their own material so they can flicker */
  pm *lb = pm_new(5);
  for (int i = 0; i < 5; i++) {
    float lx = -px + 1.0f + (2 * px - 2.0f) * (float)i / 4.0f;
    pm_push(lb);
    pm_move(lb, lx, H - 3.5f, 1.3f);
    pm_layer(lb, PL_METAL);
    pm_beam(lb, V3(0, 1.2f, 0), V3(0, 2.4f, 0), 0.04f, hex(0x2a2a30));
    lantern(lb, 1.0f, LANT_C, 3.2f);
    pm_pop(lb);
  }
  prop_model m = pm_finish(lb);
  /* put the model in the road's frame: build a group node placed and turned to the road */
  t3_object *grp = t3_group_new();
  if (m.g[PL_METAL]) {
    t3_mesh *me = t3_mesh_new(m.g[PL_METAL], prop_material(PL_METAL, -1, (pcol){ 1, 1, 1 }, 1.0f));
    t3_object_add(grp, &me->base);
    t3_release(me);
  }
  AN.flame[0] = glow_material();
  if (m.g[PL_GLOW]) {
    t3_mesh *me = t3_mesh_new(m.g[PL_GLOW], AN.flame[0]);
    t3_object_add(grp, &me->base);
    t3_release(me);
  }
  cw_node n = cw_node_new("Node3D", "StartLanterns");
  cw_node_add_child(root, n);
  cw_node3d_set_object(n, grp);
  t3_release(grp);
  cw_node3d_set_position(n, a->p);
  {
    /* aim the node's +z at the road's forward and its x at the road's right */
    t3_vec3 f = vnorm3(V3(a->f.x, 0, a->f.z));
    cw_node3d_set_rotation(n, V3(0, atan2f(f.x, f.z), 0));
  }
  point_light(root, "StartLamp", vadd3(a->p, V3(0, H - 2.5f, 0)), 1.0f, 0.62f, 0.28f, 6.0f, 34.0f);
}

/* ── a small mesh builder for the shapes here that are not props: the lake, the cave lining, glowing curtains ── */

typedef struct gm {
  float *p, *n, *uv, *c;
  uint32_t *ix;
  int nv, ni, capv, capi;
} gm;

static int gm_vert(gm *g, t3_vec3 p, t3_vec3 n, float u, float v, pcol c) {
  if (g->nv == g->capv) {
    g->capv = g->capv ? g->capv * 2 : 256;
    g->p = realloc(g->p, sizeof(float) * 3 * (size_t)g->capv), g->n = realloc(g->n, sizeof(float) * 3 * (size_t)g->capv);
    g->uv = realloc(g->uv, sizeof(float) * 2 * (size_t)g->capv), g->c = realloc(g->c, sizeof(float) * 3 * (size_t)g->capv);
  }
  int i = g->nv++;
  g->p[i * 3] = p.x, g->p[i * 3 + 1] = p.y, g->p[i * 3 + 2] = p.z;
  g->n[i * 3] = n.x, g->n[i * 3 + 1] = n.y, g->n[i * 3 + 2] = n.z;
  g->uv[i * 2] = u, g->uv[i * 2 + 1] = v;
  g->c[i * 3] = c.r, g->c[i * 3 + 1] = c.g, g->c[i * 3 + 2] = c.b;
  return i;
}
static void gm_tri(gm *g, int a, int b, int c) {
  if (g->ni + 3 > g->capi) g->capi = g->capi ? g->capi * 2 : 768, g->ix = realloc(g->ix, sizeof(uint32_t) * (size_t)g->capi);
  g->ix[g->ni++] = (uint32_t)a, g->ix[g->ni++] = (uint32_t)b, g->ix[g->ni++] = (uint32_t)c;
}
/* a flat quad (p0..p3 round it) lit on the side `face` points to; uv is the point projected along its dominant axis, one repeat per uvm metres */
static void gm_quad(gm *g, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, t3_vec3 face, pcol c, float uvm) {
  t3_vec3 n = vnorm3(V3((p1.y - p0.y) * (p3.z - p0.z) - (p1.z - p0.z) * (p3.y - p0.y), (p1.z - p0.z) * (p3.x - p0.x) - (p1.x - p0.x) * (p3.z - p0.z),
                        (p1.x - p0.x) * (p3.y - p0.y) - (p1.y - p0.y) * (p3.x - p0.x)));
  bool flip = n.x * face.x + n.y * face.y + n.z * face.z < 0;
  if (flip) n = vmul3(n, -1.0f);
  float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
  t3_vec3 q[4] = { p0, p1, p2, p3 };
  int v[4];
  for (int i = 0; i < 4; i++) {
    float u, w;
    if (ay >= ax && ay >= az) u = q[i].x, w = q[i].z;
    else if (ax >= az) u = q[i].z, w = q[i].y;
    else u = q[i].x, w = q[i].y;
    v[i] = gm_vert(g, q[i], n, u / uvm, w / uvm, c);
  }
  if (flip) gm_tri(g, v[0], v[3], v[2]), gm_tri(g, v[0], v[2], v[1]);
  else gm_tri(g, v[0], v[1], v[2]), gm_tri(g, v[0], v[2], v[3]);
}
/* smooth shaded: the caller gives each corner's normal and uv */
static int gm_vtx(gm *g, t3_vec3 p, t3_vec3 n, float u, float v, pcol c) { return gm_vert(g, p, n, u, v, c); }

static cw_node gm_finish(gm *g, cw_node parent, const char *name, t3_material *m, bool cast, bool recv) {
  cw_node node = { 0 };
  if (g->ni == 0) {
    free(g->p), free(g->n), free(g->uv), free(g->c), free(g->ix);
    memset(g, 0, sizeof *g);
    return node;
  }
  t3_geometry *geo = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, g->p, g->nv, 3), t3_geometry_set_attribute(geo, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, g->n, g->nv, 3), t3_geometry_set_attribute(geo, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, g->uv, g->nv, 2), t3_geometry_set_attribute(geo, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, g->c, g->nv, 3), t3_geometry_set_attribute(geo, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, g->ix, g->ni, 1), t3_geometry_set_index(geo, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(geo);
  free(g->p), free(g->n), free(g->uv), free(g->c), free(g->ix);
  memset(g, 0, sizeof *g);
  t3_mesh *me = t3_mesh_new(geo, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = recv;
  node = hang(parent, name, &me->base);
  t3_release(me);
  t3_release(geo);
  return node;
}

/* ── the frozen lake ───────────────────────────────────────────────────────────────────────────── */

static void build_lake(cw_node root) {
  gm g = { 0 };
  const int rings = 14, seg = 96;
  float cx = F.lake_cx, cz = F.lake_cz, rx = F.lake_rx * 1.03f, rz = F.lake_rz * 1.03f, y = F.lake_y;
  uint32_t r = 77;
  t3_vec3 up = V3(0, 1, 0);
  int centre = gm_vtx(&g, V3(cx, y, cz), up, cx / 34.0f, cz / 34.0f, (pcol){ 1, 1, 1 });
  int prev0 = -1;
  for (int k = 1; k <= rings; k++) {
    float f = (float)k / (float)rings;
    int first = g.nv;
    for (int i = 0; i < seg; i++) {
      float a = TAU_F * (float)i / (float)seg;
      /* the shore wobbles so the lake is not an ellipse */
      float wob = 1.0f + 0.045f * sinf(a * 3.0f + 1.0f) + 0.03f * sinf(a * 7.0f + 2.0f) + 0.02f * sinf(a * 13.0f);
      float x = cx + cosf(a) * rx * f * wob, z = cz + sinf(a) * rz * f * wob;
      /* patches of old snow and clear black ice, by noise */
      float n = 0.5f + 0.5f * ground_noise(x, z, 60.0f, 1.0f, 3, 3);
      pcol c = pc_mix(hex(0x6fb4e8), hex(0xc8e6ff), cw_smoothstep(0.45f, 0.75f, n));
      c = pc_mix(c, hex(0x2c6aa8), cw_smoothstep(0.35f, 0.15f, n) * 0.7f);
      gm_vtx(&g, V3(x, y, z), up, x / 34.0f, z / 34.0f, c);
    }
    for (int i = 0; i < seg; i++) {
      int a0 = first + i, a1 = first + (i + 1) % seg;
      if (k == 1) gm_tri(&g, centre, a1, a0);
      else {
        int b0 = prev0 + i, b1 = prev0 + (i + 1) % seg;
        gm_tri(&g, b0, b1, a1), gm_tri(&g, b0, a1, a0);
      }
    }
    prev0 = first;
  }
  (void)r;
  t3_material *m = phys((t3_color){ 1, 1, 1 }, 0.16f, 0.0f);
  m->vertex_colors = true;
  tex_apply(m, TEX_WALL_ICE, 1, 1, 0.9f, 1.0f);
  m->roughness = 0.45f;
  m->clearcoat = 1.0f;
  m->clearcoat_roughness = 0.03f;
  m->color = (t3_color){ 1.0f, 1.05f, 1.2f };
  gm_finish(&g, root, "LakeIce", m, false, true);
}

/* ── the ice cave: a lining of fluted ice over the tunnel, crystals, icicles, lights ───────────── */

/* a cluster of glowing crystals standing on y = 0 of the builder's frame */
static void crystal_cluster(pm *b, float size, uint32_t seed, pcol dark, pcol light) {
  uint32_t r = seed;
  int n = 4 + (int)(rnd1(&r) * 3.0f);
  for (int i = 0; i < n; i++) {
    float a = TAU_F * (float)i / (float)n + 0.4f * rnds(&r), dd = i == 0 ? 0 : size * (0.12f + 0.12f * rnd1(&r));
    float h = size * (i == 0 ? 1.0f : 0.4f + 0.5f * rnd1(&r)), rad = size * (i == 0 ? 0.12f : 0.06f + 0.05f * rnd1(&r));
    float lean = i == 0 ? 0.06f : 0.25f + 0.4f * rnd1(&r);
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, cosf(a) * dd, 0, sinf(a) * dd);
    pm_roty(b, -a);
    pm_rotz(b, -lean);
    pm_cyl_ex(b, rad, rad * 0.9f, h * 0.68f, 6, glow(dark, 0.9f), glow(pc_mix(dark, light, 0.6f), 1.8f), false, false);
    pm_move(b, 0, h * 0.68f, 0);
    pm_cyl_ex(b, rad * 0.9f, 0.001f, h * 0.32f, 6, glow(pc_mix(dark, light, 0.6f), 1.8f), glow(light, 3.2f), false, false);
    pm_pop(b);
  }
}

typedef struct cprof {
  float hc, Rt;
} cprof;

/* where the cave's lining meets the road frame: arc point k of NA (0 left base .. NA+2 right base), pulled inward by d metres */
static t3_vec3 cave_point(const tsample *a, const cprof *cp, int k, int NA, float d) {
  float Rr = a->hw;
  float x, y;
  if (k == 0) x = -Rr, y = 0;
  else if (k == NA + 2) x = Rr, y = 0;
  else {
    float ang = CW_PI * (float)(k - 1) / (float)NA;
    x = -(Rr - d) * cosf(ang), y = cp->hc + (cp->Rt - d) * sinf(ang);
  }
  return vadd3(a->p, vadd3(vmul3(a->r, x), vmul3(a->u, y)));
}

static void build_cave(cw_node root, const track *t) {
  const landmarks *L = &F.lm;
  if (L->tun0 < 0) return;
  const int NA = 18, NP = NA + 3;
  cprof cp = { 1.6f, 8.0f - 1.6f };
  float step = 2.0f;
  int i0 = L->tun0, i1 = L->tun1;
  t3_material *m = phys((t3_color){ 1, 1, 1 }, 0.3f, 0.0f);
  m->vertex_colors = true;
  m->side = T3_DOUBLE_SIDE;
  tex_apply(m, TEX_WALL_ICE, 1, 1, 1.0f, 1.0f);
  m->color = (t3_color){ 0.95f, 1.15f, 1.35f };
  m->emissive = (t3_color){ 0.04f, 0.32f, 0.7f };
  m->emissive_intensity = 0.55f;
  m->clearcoat = 0.7f;
  m->clearcoat_roughness = 0.1f;
  uint32_t r = 4242;
  /* rings every 2 m, chunked in 48 m pieces */
  int nring = (int)((float)(i1 - i0) / step) + 1;
  t3_vec3 *ring0 = malloc(sizeof(t3_vec3) * (size_t)NP), *ring1 = malloc(sizeof(t3_vec3) * (size_t)NP);
  gm g = { 0 };
  int chunk = 0;
  pmc *cr = pmc_new(60, 55);
  for (int ri = 0; ri < nring; ri++) {
    float s = t->s[i0].s + step * (float)ri;
    tsample a = track_at(t, s);
    for (int k = 0; k < NP; k++) {
      float ang = k == 0 ? 0 : k == NP - 1 ? CW_PI : CW_PI * (float)(k - 1) / (float)NA;
      float flute = 0.5f + 0.5f * sinf(ang * 11.0f + 2.2f * sinf(s * 0.045f) + 1.7f);
      float blob = ground_noise(s, ang * 12.0f, 9.0f, 1.0f, 2, 61) * 0.5f + 0.5f;
      float high = cw_smoothstep(0.1f, 0.5f, sinf(ang));
      float d = (k == 0 || k == NP - 1) ? 0.0f : (0.1f + 0.5f * flute + 0.55f * blob) * (0.25f + 0.75f * high);
      ring1[k] = cave_point(&a, &cp, k, NA, d);
    }
    if (ri > 0) {
      for (int k = 0; k + 1 < NP; k++) {
        t3_vec3 mid = vmul3(vadd3(vadd3(ring0[k], ring0[k + 1]), vadd3(ring1[k], ring1[k + 1])), 0.25f);
        t3_vec3 face = vsub3(V3(a.p.x, a.p.y, a.p.z), mid);
        float f = 0.5f + 0.5f * hashf2(ri, k, 6);
        float h = cw_clampf((float)k / (float)NP, 0, 1);
        pcol c = pc_mix(pc_mix(hex(0x3a86c4), hex(0x9bd8ff), f), hex(0xe2f6ff), cw_smoothstep(0.78f, 1.0f, f) * 0.7f);
        c = pc_mul(c, 0.8f + 0.5f * sinf(h * CW_PI));
        gm_quad(&g, ring0[k], ring0[k + 1], ring1[k + 1], ring1[k], face, c, 6.0f);
      }
    }
    memcpy(ring0, ring1, sizeof(t3_vec3) * (size_t)NP);
    if (ri > 0 && ri % 24 == 0) {
      char nm[24];
      snprintf(nm, sizeof nm, "CaveIce%d", chunk++);
      gm_finish(&g, root, nm, m, true, true);
    }
    /* crystals set into the lining every so often, pointing at the road's axis, in the aurora's colours */
    if (ri % 6 == 3) {
      static const unsigned HU[4][2] = { { 0x0a6a8a, 0x5af0ff }, { 0x0a6a3a, 0x6aff9a }, { 0x6a2a9a, 0xd08aff }, { 0x0a5a7a, 0x8affff } };
      int hue = (int)(hashf2(ri, 1, 8) * 4.0f) & 3;
      int kk = 2 + (int)(hashf2(ri, 2, 9) * (float)(NA - 2));
      t3_vec3 pos = ring1[kk];
      t3_vec3 inward = vnorm3(vsub3(vadd3(a.p, vmul3(a.u, 2.5f)), pos));
      t3_vec3 right = vnorm3(V3(inward.y * a.f.z - inward.z * a.f.y, inward.z * a.f.x - inward.x * a.f.z, inward.x * a.f.y - inward.y * a.f.x));
      t3_vec3 fwd = vnorm3(V3(right.y * inward.z - right.z * inward.y, right.z * inward.x - right.x * inward.z, right.x * inward.y - right.y * inward.x));
      pm *b = pmc_at(cr, pos);
      pm_push(b);
      pm_basis(b, pos, right, inward, fwd);
      crystal_cluster(b, 1.3f + 1.5f * hashf2(ri, 3, 10), (uint32_t)ri * 7u + 5u, hex(HU[hue][0]), hex(HU[hue][1]));
      pm_pop(b);
    }
    /* icicles from the crown */
    if (ri % 3 == 1) {
      int kk = 6 + (int)(hashf2(ri, 5, 11) * (float)(NA - 12));
      t3_vec3 pos = ring1[kk];
      t3_vec3 inward = vnorm3(vsub3(vadd3(a.p, vmul3(a.u, 1.5f)), pos));
      t3_vec3 right = vnorm3(V3(inward.y * a.f.z - inward.z * a.f.y, inward.z * a.f.x - inward.x * a.f.z, inward.x * a.f.y - inward.y * a.f.x));
      t3_vec3 fwd = vnorm3(V3(right.y * inward.z - right.z * inward.y, right.z * inward.x - right.x * inward.z, right.x * inward.y - right.y * inward.x));
      pm *b = pmc_at(cr, pos);
      pm_push(b);
      pm_basis(b, pos, right, inward, fwd);
      pm_layer(b, PL_MAIN);
      float len = 0.7f + 2.0f * hashf2(ri, 6, 12);
      pm_cyl_ex(b, 0.14f + 0.08f * hashf2(ri, 7, 13), 0.001f, len, 5, hex(0x9bd0f0), hex(0xf0fbff), false, false);
      pm_pop(b);
    }
  }
  {
    char nm[24];
    snprintf(nm, sizeof nm, "CaveIce%d", chunk);
    gm_finish(&g, root, nm, m, true, true);
  }
  t3_release(m);
  pmc_finish(cr, root, "CaveCrystals", false, -1);
  free(ring0), free(ring1);
  (void)r;
  /* the light inside: cold pools along the tube */
  int nl = 4;
  for (int k = 0; k < nl; k++) {
    float s = t->s[i0].s + (t->s[i1].s - t->s[i0].s) * ((float)k + 0.6f) / (float)nl;
    tsample a = track_at(t, s);
    char nm[16];
    snprintf(nm, sizeof nm, "CaveLamp%d", k);
    point_light(root, nm, vadd3(a.p, vmul3(a.u, 3.0f)), 0.28f, 0.7f, 1.0f, 5.0f, 38.0f);
  }
}

/* ── the forest, the rocks, the ice ────────────────────────────────────────────────────────────── */

/* how far a point is into the crevasse's gorge (0 outside) */
static bool in_gorge(float x, float z, float margin) {
  const landmarks *L = &F.lm;
  float ax = x - L->gmid.x, az = z - L->gmid.z;
  float a = ax * L->gfwd.x + az * L->gfwd.z, b = -ax * L->gfwd.z + az * L->gfwd.x;
  return fabsf(a) < L->glen * 0.5f + margin && fabsf(b) < 140.0f;
}
static bool in_lake(float x, float z, float k) {
  float lx = (x - F.lake_cx) / (F.lake_rx * k), lz = (z - F.lake_cz) / (F.lake_rz * k);
  return lx * lx + lz * lz < 1.0f;
}
static float slope_at(float x, float z) {
  const float e = 3.0f;
  float dx = terrain(x + e, z, NULL) - terrain(x - e, z, NULL), dz = terrain(x, z + e, NULL) - terrain(x, z - e, NULL);
  return sqrtf(dx * dx + dz * dz) / (2 * e);
}
static float ground_cb(float x, float z, void *ud) { return terrain(x, z, ud); }

/* where forest may stand: below the tree line, off the lake and the gorge, thinned into clearings */
static float tree_mask(float x, float z, void *ud) {
  (void)ud;
  if (in_lake(x, z, 1.1f) || in_gorge(x, z, 14.0f)) return 0;
  float h = terrain(x, z, NULL);
  float w = 1.0f - cw_smoothstep(22.0f, 62.0f, h);
  if (w <= 0) return 0;
  float n = 0.5f + 0.5f * ground_noise(x, z, 95.0f, 1.0f, 2, 7);
  w *= cw_smoothstep(0.3f, 0.64f, n);
  if (slope_at(x, z) > 0.8f) return 0;
  return w;
}
static float rock_mask(float x, float z, void *ud) {
  (void)ud;
  if (in_lake(x, z, 1.02f) || in_gorge(x, z, 6.0f)) return 0;
  return 1.0f;
}
static float shore_mask(float x, float z, void *ud) {
  (void)ud;
  float lx = (x - F.lake_cx) / F.lake_rx, lz = (z - F.lake_cz) / F.lake_rz, d = sqrtf(lx * lx + lz * lz);
  return d > 0.92f && d < 1.25f ? 1.0f : 0.0f;
}

static void build_forest(cw_node root, const track *t) {
  prop_set *ps = ps_new(root, "Forest");
  ps_style(ps, PK_SNOW_PINE, -1, rgb(0.62f, 0.86f, 1.0f), 1.0f);
  ps_style(ps, PK_PINE, -1, rgb(0.55f, 0.8f, 1.0f), 1.0f);
  ps_style(ps, PK_DEAD_TREE, -1, rgb(0.6f, 0.7f, 0.95f), 1.0f);
  ps_style(ps, PK_ROCK, TEX_GROUND_ROCK, rgb(0.5f, 0.6f, 0.9f), 1.0f);
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, rgb(0.5f, 0.6f, 0.9f), 1.0f);
  scatter_opts o = scatter_defaults();
  o.ground = ground_cb;
  o.mask = tree_mask;
  o.kinds[0] = PK_SNOW_PINE, o.weight[0] = 7;
  o.kinds[1] = PK_PINE, o.weight[1] = 2;
  o.kinds[2] = PK_DEAD_TREE, o.weight[2] = 0.4f;
  o.nkinds = 3;
  o.scale_min = 0.75f, o.scale_max = 1.5f;
  o.from_s = 0, o.to_s = t->length;
  o.spacing = 7.5f, o.jitter = 1.0f;
  o.lat_min = 3.0f, o.lat_max = 150.0f, o.lat_curve = 1.7f;
  o.margin = 3.0f;
  o.lod_dist = 90.0f, o.far_thin = 0.3f;
  o.seed = 5;
  int n = scatter_along(ps, t, &o);
  /* the big old ones, scattered wide: they stand over the road and make the pass feel tall */
  o.kinds[0] = PK_SNOW_PINE, o.weight[0] = 1, o.nkinds = 1;
  o.scale_min = 2.0f, o.scale_max = 3.1f;
  o.spacing = 34.0f, o.lat_min = 7.0f, o.lat_max = 70.0f, o.lat_curve = 1.0f;
  o.lod_dist = 160.0f, o.far_thin = 0;
  o.seed = 9;
  n += scatter_along(ps, t, &o);
  /* the forest between the roads and out on the slopes */
  o = scatter_defaults();
  o.ground = ground_cb;
  o.mask = tree_mask;
  o.kinds[0] = PK_SNOW_PINE, o.weight[0] = 6;
  o.kinds[1] = PK_PINE, o.weight[1] = 3;
  o.nkinds = 2;
  o.scale_min = 0.8f, o.scale_max = 1.7f;
  o.cx = 330, o.cz = -100, o.half_x = 900, o.half_z = 1000;
  o.density = 0.9f;
  o.margin = 14.0f;
  o.lod_dist = 80.0f, o.far_thin = 0.4f;
  o.seed = 13;
  n += scatter_area(ps, t, &o);
  /* rocks half drowned in snow */
  o = scatter_defaults();
  o.ground = ground_cb;
  o.mask = rock_mask;
  o.kinds[0] = PK_ROCK, o.weight[0] = 4;
  o.kinds[1] = PK_BOULDERS, o.weight[1] = 1.2f;
  o.nkinds = 2;
  o.scale_min = 0.9f, o.scale_max = 2.6f;
  o.from_s = 0, o.to_s = t->length;
  o.spacing = 22.0f, o.jitter = 1.0f;
  o.lat_min = 3.0f, o.lat_max = 60.0f, o.lat_curve = 1.5f;
  o.sink = 0.55f;
  o.seed = 21;
  n += scatter_along(ps, t, &o);
  cw_info("frost: %d trees and rocks", n);
  ps_finish(ps);

  /* ice: spikes, boulders and glowing crystals along the glassy stretch, round the lake and about the crevasse */
  prop_set *pi = ps_new(root, "Ice");
  o = scatter_defaults();
  o.ground = ground_cb;
  o.mask = rock_mask;
  o.kinds[0] = PK_ICE_SPIKES, o.weight[0] = 3;
  o.kinds[1] = PK_ICE_BOULDER, o.weight[1] = 2;
  o.kinds[2] = PK_CRYSTAL, o.weight[2] = 1.6f;
  o.nkinds = 3;
  o.scale_min = 0.9f, o.scale_max = 1.9f;
  o.from_s = (float)F.lm.ice0, o.to_s = (float)F.lm.ice1 + 40.0f;
  o.spacing = 11.0f, o.jitter = 1.0f;
  o.lat_min = 2.5f, o.lat_max = 38.0f, o.lat_curve = 1.8f;
  o.sink = 0.2f;
  o.seed = 31;
  scatter_along(pi, t, &o);
  /* the shore of the lake */
  o = scatter_defaults();
  o.ground = ground_cb;
  o.mask = shore_mask;
  o.kinds[0] = PK_ICE_SPIKES, o.weight[0] = 3;
  o.kinds[1] = PK_ICE_BOULDER, o.weight[1] = 2;
  o.kinds[2] = PK_CRYSTAL, o.weight[2] = 0.7f;
  o.nkinds = 3;
  o.scale_min = 1.0f, o.scale_max = 2.4f;
  o.cx = F.lake_cx, o.cz = F.lake_cz, o.half_x = F.lake_rx * 1.3f, o.half_z = F.lake_rz * 1.3f;
  o.density = 1.1f;
  o.margin = 6.0f;
  o.seed = 37;
  scatter_area(pi, t, &o);
  ps_finish(pi);
}

/* the road's own dressing: arches at the checkpoints, marks on the jump, signs for the hairpins, posts of light */
static void build_dressing(cw_node root, const track *t) {
  dress_style st = dress_defaults();
  st.main = rgb(0.55f, 0.72f, 0.92f);
  st.accent = rgb(1.0f, 0.55f, 0.15f);
  st.lamp = rgb(0.7f, 0.95f, 1.0f);
  st.has_colours = true;
  st.seed = 17;
  dress_checkpoints(root, t, &st);
  deco_jump_marks(root, t, &st);
  deco_corner_arrows(root, t, 0.011f, &st);
  deco_corner_signs(root, t, 0.011f, &st);
  dress_style lamps = st;
  lamps.lamp = rgb(1.0f, 0.7f, 0.3f);
  deco_lights(root, t, 24.0f, LAMP_GLOW_POST, 150.0f, (float)F.lm.tun0 - 10.0f, &lamps);
  lamps.lamp = rgb(0.4f, 0.9f, 1.0f);
  deco_lights(root, t, 30.0f, LAMP_GLOW_POST, (float)F.lm.ice0, (float)F.lm.gap0 - 70.0f, &lamps);
  deco_supports(root, t, SUP_ICE, ground_cb, NULL, 0, 2.5f, &st);
}

static void frost_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  memset(&F, 0, sizeof F);
  memset(&SKY, 0, sizeof SKY);
  F.t = t;
  find_landmarks(t);
  F.lake_cx = 345.0f, F.lake_cz = -235.0f, F.lake_rx = 215.0f, F.lake_rz = 300.0f, F.lake_y = 0.9f;
  build_rf(t);
  if (getenv("FROST_DEBUG")) {
    const landmarks *L = &F.lm;
    cw_info("frost: tunnel samples %d..%d cork %d..%d gap %d..%d ramp0 %d base_y %.1f", L->tun0, L->tun1, L->cork0, L->cork1, L->gap0, L->gap1, L->ramp0, L->base_y);
    for (int k = 0; k < t->n; k += 25) {
      const tsample *a = &t->s[k];
      float g = terrain(a->p.x, a->p.z, NULL);
      if (g > a->p.y + 0.0f) cw_info("frost: BURIED s=%d road y=%.2f terrain=%.2f", k, a->p.y, g);
    }
    for (int k = 990; k < 1070; k += 8) {
      const tsample *a = &t->s[k];
      for (int side = -1; side <= 1; side++) {
        t3_vec3 q = vadd3(a->p, vmul3(a->r, (float)side * 7.0f));
        cw_info("frost: s=%d side %d road y=%.2f terrain=%.2f", k, side, a->p.y, terrain(q.x, q.z, NULL));
      }
    }
    for (int k = L->tun0 - 20; k <= L->tun1 + 20; k += 30) {
      const tsample *a = &t->s[k];
      cw_info("frost: s=%d road y=%.1f terrain=%.1f far=%.1f gap=%.1f", k, a->p.y, terrain(a->p.x, a->p.z, NULL), far_field(a->p.x, a->p.z), road_gap(t, a->p.x, a->p.z, NULL));
    }
  }
  SKY.cx = F.lm.centre.x, SKY.cz = F.lm.centre.z;
  cw_node root = cw_node_new("Node3D", "Frost");
  cw_node_add_child(scene, root);

  /* the ground */
  F.g = ground_defaults();
  F.g.half = 1500;
  F.g.height = terrain;
  F.g.tex = TEX_GROUND_SNOW;
  F.g.tex_repeat = 10;
  F.g.tint = rgb(0.7f, 0.8f, 1.0f);
  F.g.tint2 = rgb(0.58f, 0.68f, 0.95f);
  F.g.patch_scale = 90;
  F.g.near_cell = 48;
  F.g.detail_range = 130;
  F.g.follow_road = false;
  F.g.darken_edge = 0.12f;
  F.g.lumpy = 0.12f;
  F.g.rough = 0.8f;
  ground_plane(root, t, &F.g);

  /* the sky */
  build_aurora(root);
  {
    static const int N[3] = { 900, 900, 800 };
    for (int k = 0; k < 3; k++) {
      t3_material *m = t3_points_material_new(0xffffff);
      m->vertex_colors = true;
      m->size = 2.2f;
      m->size_attenuation = false;
      m->transparent = true;
      m->blending = T3_ADDITIVE_BLENDING;
      m->depth_write = false;
      m->fog = false;
      SKY.star_mat[k] = m;
      t3_object *o = make_stars(77u + (uint32_t)k * 31u, 0, N[k], m);
      o->render_order = -6;
      hang(root, "Stars", o);
      t3_release(o);
    }
  }
  build_peaks(root);
  restyle_world(scene);
  init_colours();
  build_start(root, t);
  build_dressing(root, t);
  build_forest(root, t);
  build_lake(root);
  if (!getenv("FROST_NOCAVE")) build_cave(root, t);
  SKY.env = cw_node_get(scene, "Env");
}

static void frost_frame(float alpha, double time) {
  (void)alpha;
  float t = (float)time;
  /* a development aid: FROST_CAM=x,y,z,tx,ty,tz puts the first view's camera there looking at the target */
  static float dcam[6];
  static int dcam_state = -1;
  if (dcam_state < 0) dcam_state = getenv("FROST_CAM") && sscanf(getenv("FROST_CAM"), "%f,%f,%f,%f,%f,%f", &dcam[0], &dcam[1], &dcam[2], &dcam[3], &dcam[4], &dcam[5]) == 6;
  if (dcam_state > 0 && cw_node_valid(SKY.env)) {
    cw_node cam = cw_node_get(SKY.env, "../Camera");
    if (cw_node_valid(cam)) {
      cw_node3d_set_position(cam, V3(dcam[0], dcam[1], dcam[2]));
      cw_node3d_look_at(cam, V3(dcam[3], dcam[4], dcam[5]), V3(0, 1, 0));
      cw_node3d_snapshot(cam);
    }
  }
  static int dbg;
  if (getenv("FROST_DEBUG") && ++dbg == 5) {
    cw_value v;
    cw_node sky = cw_node_get(SKY.env, "../Sky"), ss = cw_node_get(SKY.env, "../ZSkySun");
    if (cw_node_valid(sky) && cw_node_get_prop(sky, "sun_elevation", &v)) cw_info("frost: sun elevation %.2f", v.f);
    if (cw_node_valid(ss)) {
      const t3_mat4 *m = cw_node3d_global_transform(ss);
      cw_info("frost: sun light shines toward (%.2f %.2f %.2f)", -m->e[8], -m->e[9], -m->e[10]);
    }
  }
  for (int k = 0; k < NCURT; k++)
    if (SKY.c[k].g) aurora_update(&SKY.c[k], t);
  for (int k = 0; k < 3; k++)
    if (SKY.star_mat[k]) SKY.star_mat[k]->opacity = 0.62f + 0.38f * sinf(t * (0.9f + 0.55f * (float)k) + (float)k * 2.1f);
}

const map_def map_frost = {
  .name = "Frostbite Pass",
  .blurb = "Ice, ice caves and the aurora: slippery and steep.",
  .scene = "scenes/frost.cwscene",
  .spec = &(track_spec){ .name = "frost", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 9, .circuit = true },
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.62f, 0.72f, 0.95f }, .kerb_a = { 0.98f, 0.45f, 0.12f }, .kerb_b = { 0.95f, 0.97f, 1.0f }, .wall = { 0.6f, 0.82f, 1.0f },
            .wall_top = { 0.92f, 0.96f, 1.0f }, .road_tex = TEX_ROAD_DIRT, .wall_tex = TEX_WALL_ICE, .road_rough = 0.75f },
  .laps = 3,
  .par_lap = 60,
  .scenery = frost_scenery,
  .scenery_frame = frost_frame,
};
