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
  /* the run-up to the crevasse stands on stilts: taller walls, so a bump there is not a fall */
  WALLS(2, 2),
  BOOST(30),
  STRAIGHT(40),
  SLOPE(36, 20),
  SLOPE(22, 20),
  JUMP(0, 31),
  SLOPE(80, 0),
  WALLS(1, 1),
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
/* The ice chute: a frozen gully straight up through the middle hairpin of the climb. Its bends are packed snow, but
 * the straight between them is bare ice with a boost strip on it: arrive at the second bend that fast on no grip and
 * the snow bank stops you; brake on the ice and it is far quicker than the hairpin. */
static const piece ICECHUTE[] = {
  SURFACE(SURF_DIRT),
  MOVE(10, -4, KEEP, 0, KEEP),
  ATURN(-72, 66),
  SURFACE(SURF_ICE),
  ASTRAIGHT(14),
  BOOST(18),
  STRAIGHT(18),
  ASLOPE(24, 5),
  ASTRAIGHT(14),
  SURFACE(SURF_DIRT),
  ATURN(-72, 66),
  ASTRAIGHT(6),
  END(),
};
static const branch_spec BRANCHES[] = {
  { .name = "ice chute", .s0 = 512, .lat0 = 4.6f, .s1 = 772, .lat1 = 4.6f, .pieces = ICECHUTE, .npieces = sizeof ICECHUTE / sizeof ICECHUTE[0],
    .hw0 = 4.2f, .walls_l = 1, .walls_r = 1, .ai_take = { 0.1f, 0.4f, 0.85f } },
};
#define MAP_SPEC (&(track_spec){ .name = "frost", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 9, .circuit = true, .branches = BRANCHES, .nbranches = 1 })

/* ── what the place is made of ─────────────────────────────────────────────────────────────────── */

#define TAU_F 6.2831853f

/* split screen draws the scene once per view: scatter counts are scaled by this (1 for one view) */
static float THIN = 1.0f;

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
  bool no_gorge;             /* ask the land as it is without the crevasse cut (where the gorge's lips stand) */
  int npads;
  struct { float x, z, r0, r1, y; } pad[6]; /* flat shelves for the station and the camps */
  float gorge_floor;         /* height of the crevasse's bottom */
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
static float far_field(float x, float z, float gap) {
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
  /* wind-sculpted drifts and, away from the roads, crags and ridges */
  float far_k = cw_smoothstep(25.0f, 90.0f, gap);
  h += ground_noise(x, z, 45.0f, 3.5f, 3, 11) * far_k;
  float gul = 1.0f - fabsf(ground_noise(x, z, 38.0f, 1.0f, 2, 23)) * 2.2f;
  h += 5.0f * powf(cw_clampf(gul, 0, 1), 2.0f) * cw_smoothstep(14.0f, 60.0f, gap);
  float rg = 1.0f - fabsf(ground_noise(x, z, 170.0f, 1.0f, 3, 17)) * 2.4f;
  h += 22.0f * powf(cw_clampf(rg, 0, 1), 2.0f) * cw_smoothstep(60.0f, 260.0f, gap) * (0.4f + 0.6f * cw_smoothstep(0.0f, 1.0f, ex * ex + ez * ez));
  return h;
}

static float terrain(float x, float z, void *ud) {
  (void)ud;
  const track *t = F.t;
  const landmarks *L = &F.lm;
  float ry = 0;
  int idx = 0;
  float gap = road_gap_idx(t, x, z, &ry, &idx);
  float H = far_field(x, z, gap > 1e8f ? 400.0f : gap);
  float g;
  if (gap > 1e8f) {
    g = H;
  } else {
    const tsample *s = &t->s[idx];
    float lim = 8.0f + gap * 0.9f;
    float rb = ry;
    /* the take-off ramp and the landing hang over the plateau: the snow keeps its own level beneath them */
    bool ramp = idx > L->ramp0 - 6 && idx < L->gap1 + 90;
    if (ramp) rb = L->base_y;
    else if (s->u.y > 0.4f) {
      /* a banked road's edges are not at its centre's height: the land follows the road's own plane near it */
      float plane = s->p.y - ((x - s->p.x) * s->u.x + (z - s->p.z) * s->u.z) / s->u.y;
      rb = ry + (plane - ry) * (1.0f - cw_smoothstep(0.0f, 18.0f, gap));
    }
    float hc = cw_clampf(H, rb - lim, rb + lim);
    float w = cw_smoothstep(1.5f, 44.0f, gap);
    g = (rb - 0.3f) * (1 - w) + hc * w;
    /* a bank of snow ploughed against the walls */
    float bd = (gap - 3.5f) / 2.6f;
    g += 0.55f * expf(-bd * bd);
    /* the mountain over the ice cave: solid rock, lower at the two mouths */
    if (L->tun0 >= 0 && idx >= L->tun0 - 14 && idx <= L->tun1 + 14) {
      float inside = fminf((float)(idx - L->tun0), (float)(L->tun1 - idx));
      float cover = (1.0f - cw_smoothstep(s->hw + 4.0f, s->hw + 46.0f, gap)) * cw_smoothstep(0.5f, 7.0f, inside);
      float top = ry + 13.0f + 3.5f * ground_noise(x, z, 24.0f, 1.0f, 2, 33);
      if (top > g) g = g + (top - g) * cover;
    }
  }
  /* the shelves the station and the camps stand on */
  for (int i = 0; i < F.npads; i++) {
    float dx = x - F.pad[i].x, dz = z - F.pad[i].z, d = sqrtf(dx * dx + dz * dz);
    if (d < F.pad[i].r1) {
      float w = 1.0f - cw_smoothstep(F.pad[i].r0, F.pad[i].r1, d);
      g += (F.pad[i].y - g) * w;
    }
  }
  /* the crevasse: a gorge cut across the pass under the jump, flat at the bottom, sheer at the lips */
  if (!F.no_gorge) {
    float ax = x - L->gmid.x, az = z - L->gmid.z;
    float a = ax * L->gfwd.x + az * L->gfwd.z, b = -ax * L->gfwd.z + az * L->gfwd.x;
    float half = L->glen * 0.5f + 1.0f;
    float wob = 1.6f * ground_noise(x, z, 14.0f, 1.0f, 2, 9);
    float prof = 1.0f - cw_smoothstep(half - 0.5f + wob, half + 1.8f + wob, fabsf(a));
    float ends = 1.0f - cw_smoothstep(75.0f, 135.0f, fabsf(b));
    float m = prof * ends;
    if (m > 0) g += (F.gorge_floor + 0.6f * ground_noise(x, z, 9.0f, 1.0f, 2, 4) - g) * m;
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
    c->nu = 72, c->nv = 6;
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
      float edge = cw_smoothstep(0.0f, 0.12f, v) * expf(-v * 2.0f) * (1.0f + 1.8f * expf(-v * 22.0f)) * (1.0f - cw_smoothstep(0.55f, 1.0f, v));
      float I = edge * ray * flick * (0.25f + 0.85f * body) * surge;
      float m = cw_clampf(cw_smoothstep(0.1f, 0.95f, v) + (hue_shift - 0.5f) * 0.45f + (c->hue - 0.5f) * 0.2f, 0, 1);
      /* green (0.12, 1, 0.4) up through teal (0.1, 0.85, 0.8) to violet (0.55, 0.3, 1) */
      float r0 = 0.12f + 0.45f * m * m, g0 = 1.0f - 0.2f * m - 0.55f * m * m, b0 = 0.4f + 0.6f * m;
      C[k] = r0 * I * 3.2f, C[k + 1] = g0 * I * 3.2f, C[k + 2] = b0 * I * 3.2f;
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
      R = R + (0.4f - R) * rut * 0.85f, G = G + (0.52f - G) * rut * 0.85f, B = B + (0.78f - B) * rut * 0.85f;
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
      float mx = rnds(&r), mz = rnds(&r);
      pm_move(b, side * (px + 0.18f * mx), y, 0.15f * mz);
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
    float my = rnds(&r), mz = rnds(&r);
    pm_move(b, x + w * 0.5f, H - 0.55f + 0.1f * my, 0.1f * mz);
    float bh = 2.0f + 0.25f * rnd1(&r), tilt = 0.06f * rnds(&r);
    ice_block(b, w, bh, 3.0f, tilt, pc_mix(ICE_DEEP, ICE_LO, 0.5f), pc_mix(ICE_LO, ICE_HI, 0.55f));
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
    float mx = rnd1(&r), mz = rnd1(&r);
    pm_move(b, -px + 2.0f * px * mx, H - 1.4f, -1.1f + 2.2f * mz);
    float ir = 0.09f + 0.1f * rnd1(&r), il = 0.6f + 1.8f * rnd1(&r);
    icicle(b, ir, il, ICE_HI);
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
  m->color = (t3_color){ 0.8f, 1.0f, 1.3f };
  m->emissive = (t3_color){ 0.04f, 0.32f, 0.7f };
  m->emissive_intensity = 0.3f;
  m->clearcoat = 0.7f;
  m->clearcoat_roughness = 0.1f;
  uint32_t r = 4242;
  /* rings every 2 m, chunked in 48 m pieces */
  int nring = (int)((float)(i1 - i0) / step) + 1;
  t3_vec3 *ring0 = malloc(sizeof(t3_vec3) * (size_t)NP), *ring1 = malloc(sizeof(t3_vec3) * (size_t)NP);
  gm g = { 0 };
  int chunk = 0;
  pmc *cr = pmc_new(60, 55);
  struct { int k, left; } vein[6] = { { 0, 0 } };
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
      /* glowing veins in the ice: thin lit seams that run along the lining for a few metres */
      for (int v = 0; v < 6; v++) {
        if (vein[v].left <= 0 && hashf2(ri, v, 41) < 0.045f) vein[v].k = 2 + (int)(hashf2(ri, v, 43) * (float)(NA - 2)), vein[v].left = 4 + (int)(hashf2(ri, v, 47) * 9.0f);
        if (vein[v].left > 0) {
          int k = vein[v].k;
          t3_vec3 a0 = vadd3(vmul3(ring0[k], 0.5f), vmul3(ring0[k + 1], 0.5f)), a1 = vadd3(vmul3(ring1[k], 0.5f), vmul3(ring1[k + 1], 0.5f));
          t3_vec3 towards = vnorm3(vsub3(vadd3(a.p, vmul3(a.u, 3.0f)), a1));
          t3_vec3 sideways = vmul3(vsub3(ring1[k + 1], ring1[k]), 0.07f);
          t3_vec3 lift = vmul3(towards, 0.06f);
          pm *b = pmc_at(cr, a1);
          pm_push(b);
          pm_layer(b, PL_GLOW);
          pm_quad(b, vadd3(vsub3(a0, sideways), lift), vadd3(vadd3(a0, sideways), lift), vadd3(vadd3(a1, sideways), lift), vadd3(vsub3(a1, sideways), lift), glow(hex(0x40e8ff), 1.7f));
          pm_quad(b, vadd3(vadd3(a0, sideways), lift), vadd3(vsub3(a0, sideways), lift), vadd3(vsub3(a1, sideways), lift), vadd3(vadd3(a1, sideways), lift), glow(hex(0x40e8ff), 1.7f));
          pm_pop(b);
          vein[v].left--;
        }
      }
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
/* the ground for a rock: lower on a slope by *ud metres per unit of slope, so its downhill side stays in the snow (a
 * rock set level on a steep face shows its rounded underside there and looks stuck on, however deep its middle is) */
static float rock_ground_cb(float x, float z, void *ud) { return terrain(x, z, NULL) - *(const float *)ud * fminf(slope_at(x, z), 1.3f); }

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
/* Loose rocks only where the snow is near level (under about 20 degrees). On a steeper face a rock set in the snow kept
 * a pale rim standing proud of the slope with no contact line and no shadow under it: a white blob that floated, from
 * the road and from close by. The steep faces show their rock as ledges shaped out of the slope (build_ledges). */
static float rock_flat_mask(float x, float z, void *ud) {
  if (rock_mask(x, z, ud) <= 0) return 0;
  return slope_at(x, z) < 0.36f ? 1.0f : 0.0f;
}
static float shore_mask(float x, float z, void *ud) {
  (void)ud;
  float lx = (x - F.lake_cx) / F.lake_rx, lz = (z - F.lake_cz) / F.lake_rz, d = sqrtf(lx * lx + lz * lz);
  return d > 0.92f && d < 1.25f ? 1.0f : 0.0f;
}

/* Rocks in snow. A rock on a steep face was set level, so its rounded underside stood out of the slope downhill and it
 * read as stuck on (rock_ground_cb now sinks it by the slope). What shows should look like rock that came up through
 * snow: dark cold slate with the snow lying on its upward faces. Done to the built sets' models in place (each model
 * is shared by every copy of it), so it costs no draws; the rock's own facet shading is kept as a brightness. */
typedef struct { t3_geometry *g[64]; int n; t3_material *m; } snowdress;
static void snow_rock_geometry(t3_geometry *g) {
  t3_attribute *na = g->attributes[T3_ATTR_NORMAL], *ca = g->attributes[T3_ATTR_COLOR];
  if (!na || !ca || ca->type != T3_FLOAT32) return;
  const float *N = t3_attribute_f32(na);
  float *C = t3_attribute_f32(ca);
  int n = ca->count, cs = ca->item_size;
  float lsum = 0;
  for (int i = 0; i < n; i++) lsum += 0.3f * C[i * cs] + 0.6f * C[i * cs + 1] + 0.1f * C[i * cs + 2];
  float lavg = lsum / (float)(n > 0 ? n : 1);
  if (lavg < 1e-4f) lavg = 1;
  for (int i = 0; i < n; i++) {
    float *c = &C[i * cs];
    float k = cw_clampf((0.3f * c[0] + 0.6f * c[1] + 0.1f * c[2]) / lavg, 0.7f, 1.25f);
    float s = cw_smoothstep(0.7f, 0.88f, N[i * 3 + 1]); /* snow on the faces that look up */
    c[0] = k * (0.042f + (0.56f - 0.042f) * s), c[1] = k * (0.048f + (0.66f - 0.048f) * s), c[2] = k * (0.07f + (0.9f - 0.07f) * s);
  }
  ca->version++;
}
static void snow_walk(t3_object *o, snowdress *sd) {
  if (!o) return;
  if (o->type == T3_INSTANCED_MESH || o->type == T3_MESH) {
    t3_mesh *me = (t3_mesh *)o;
    bool seen = false;
    for (int i = 0; i < sd->n; i++) seen |= sd->g[i] == me->geometry;
    if (!seen && sd->n < 64) sd->g[sd->n++] = me->geometry, snow_rock_geometry(me->geometry);
    for (int i = 0; i < me->material_count; i++)
      if (me->materials[i] != sd->m) {
        t3_retain(sd->m);
        t3_release(me->materials[i]);
        me->materials[i] = sd->m;
      }
  }
  for (int i = 0; i < o->child_count; i++) snow_walk(o->children[i], sd);
}
/* every model under a finished prop set's group (by name, under root) */
static void snow_dress_set(cw_node root, const char *name) {
  cw_node grp = cw_node_get(root, name);
  if (!cw_node_valid(grp)) return;
  snowdress sd = { 0 };
  /* untextured: the vertex colours carry the snow and the rock (a rock texture would grey the snow) */
  sd.m = prop_material(PL_MAIN, -1, (pcol){ 1, 1, 1 }, 1.0f);
  for (int i = 0; i < cw_node_child_count(grp); i++) snow_walk(cw_node3d_object(cw_node_child(grp, i)), &sd);
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
  o.spacing = 7.5f / THIN, o.jitter = 1.0f;
  o.lat_min = 3.0f, o.lat_max = 150.0f, o.lat_curve = 1.7f;
  o.margin = 3.0f;
  o.lod_dist = 90.0f, o.far_thin = 0.3f;
  o.seed = 5;
  int n = scatter_along(ps, t, &o);
  /* the big old ones, scattered wide: they stand over the road and make the pass feel tall */
  o.kinds[0] = PK_SNOW_PINE, o.weight[0] = 1, o.nkinds = 1;
  o.scale_min = 2.0f, o.scale_max = 3.1f;
  o.spacing = 34.0f / THIN, o.lat_min = 7.0f, o.lat_max = 70.0f, o.lat_curve = 1.0f;
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
  o.density = 0.9f * THIN;
  o.margin = 14.0f;
  o.lod_dist = 80.0f, o.far_thin = 0.4f;
  o.seed = 13;
  n += scatter_area(ps, t, &o);
  ps_finish(ps);
  /* rocks half drowned in snow (their own set: snow_dress_set restyles every model in it) */
  ps = ps_new(root, "Rocks");
  ps_style(ps, PK_ROCK, TEX_GROUND_ROCK, rgb(0.5f, 0.6f, 0.9f), 1.0f);
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, rgb(0.5f, 0.6f, 0.9f), 1.0f);
  o = scatter_defaults();
  o.align_slope = true;
  static const float ROCK_SLOPE_SINK = 2.0f;
  o.ground = rock_ground_cb, o.ground_ud = (void *)&ROCK_SLOPE_SINK;
  o.mask = rock_flat_mask;
  o.kinds[0] = PK_ROCK, o.weight[0] = 4;
  o.kinds[1] = PK_BOULDERS, o.weight[1] = 1.2f;
  o.nkinds = 2;
  o.scale_min = 0.9f, o.scale_max = 2.6f;
  o.from_s = 0, o.to_s = t->length;
  o.spacing = 22.0f / THIN, o.jitter = 1.0f;
  o.lat_min = 3.0f, o.lat_max = 60.0f, o.lat_curve = 1.5f;
  o.sink = 0.9f; /* half drowned: shallower, a rock on the steep snow showed its underside and read as hanging in the air */
  o.seed = 21;
  n += scatter_along(ps, t, &o);
  cw_info("frost: %d trees and rocks", n);
  ps_finish(ps);
  snow_dress_set(root, "Rocks");

  /* ice: spikes, boulders and glowing crystals along the glassy stretch, round the lake and about the crevasse */
  prop_set *pi = ps_new(root, "Ice");
  o = scatter_defaults();
  o.ground = ground_cb;
  o.mask = rock_flat_mask; /* ice lumps too: none sits on a steep face */
  o.kinds[0] = PK_ICE_SPIKES, o.weight[0] = 3;
  o.kinds[1] = PK_ICE_BOULDER, o.weight[1] = 2;
  o.kinds[2] = PK_CRYSTAL, o.weight[2] = 1.6f;
  o.nkinds = 3;
  o.scale_min = 0.9f, o.scale_max = 1.9f;
  o.from_s = (float)F.lm.ice0, o.to_s = (float)F.lm.ice1 + 40.0f;
  o.spacing = 11.0f / THIN, o.jitter = 1.0f;
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
  o.density = 1.1f * THIN;
  o.margin = 6.0f;
  o.seed = 37;
  scatter_area(pi, t, &o);
  ps_finish(pi);
}

static float peak_mask(float x, float z, void *ud) {
  (void)ud;
  if (in_gorge(x, z, 10.0f)) return 0;
  return cw_smoothstep(70.0f, 105.0f, terrain(x, z, NULL)) * (0.4f + 0.6f * cw_smoothstep(0.1f, 0.5f, slope_at(x, z)));
}
static float drift_mask(float x, float z, void *ud) {
  (void)ud;
  if (in_lake(x, z, 1.1f) || in_gorge(x, z, 30.0f)) return 0;
  return slope_at(x, z) < 0.16f ? 1.0f : 0.0f;
}

/* rocks breaking through the snow on the steep slopes, needles on the ridges, wind-shaped drifts on the flats */
static void build_outcrops(cw_node root, const track *t) {
  prop_set *ps = ps_new(root, "Crags");
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, rgb(0.85f, 1.0f, 1.5f), 1.0f);
  ps_style(ps, PK_ROCK, TEX_GROUND_ROCK, rgb(0.85f, 1.0f, 1.5f), 1.0f);
  ps_style(ps, PK_SPIRE, -1, rgb(0.55f, 0.75f, 1.6f), 1.0f);
  ps_style(ps, PK_CANYON_WALL, TEX_GROUND_ROCK, rgb(0.85f, 1.0f, 1.5f), 1.0f);
  /* (boulders scattered over the steep faces are gone: set into a slope they showed a pale rim with no contact line and
   * read as floating; the steep faces' rock is now the ledges shaped out of the slope itself, build_ledges) */
  scatter_opts o;
  int n = 0;
  /* needles and cliff faces on the ridges, in the cool variant of their palettes: placed by hand so every one is that variant */
  {
    uint32_t hr = 91;
    for (float zz = -1000.0f; zz < 800.0f; zz += 38.0f)
      for (float xx = -450.0f; xx < 1100.0f; xx += 38.0f) {
        float x = xx + 30.0f * rnds(&hr), z = zz + 30.0f * rnds(&hr);
        if (peak_mask(x, z, NULL) < 0.55f + 0.3f * rnd1(&hr)) continue;
        if (road_gap(t, x, z, NULL) < 40.0f) continue;
        float sc = 0.8f + 1.4f * rnd1(&hr);
        bool wall = rnd1(&hr) < 0.4f;
        t3_vec3 p = V3(x, terrain(x, z, NULL) - 1.5f * sc, z);
        ps_add_ex(ps, wall ? PK_CANYON_WALL : PK_SPIRE, 2, 0, p, t3_quat_from_axis_angle(V3(0, 1, 0), rnd1(&hr) * TAU_F), V3(sc, sc, sc), (pcol){ 1, 1, 1 }, (uint32_t)(x * 3 + z));
        n++;
      }
  }
  cw_info("frost: %d crags", n);
  ps_finish(ps);

  prop_set *pd = ps_new(root, "Drifts");
  ps_style(pd, PK_DUNE, TEX_GROUND_SNOW, rgb(0.55f, 0.66f, 0.95f), 1.0f);
  o = scatter_defaults();
  o.ground = ground_cb;
  o.mask = drift_mask;
  o.kinds[0] = PK_DUNE, o.nkinds = 1;
  o.scale_min = 2.4f, o.scale_max = 5.5f;
  o.cx = 330, o.cz = -100, o.half_x = 800, o.half_z = 900;
  o.density = 0.06f * THIN;
  o.margin = 30.0f;
  o.sink = 0.3f;
  o.lod_dist = 200.0f;
  o.seed = 57;
  scatter_area(pd, t, &o);
  ps_shadows(pd, false);
  ps_finish(pd);
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
  /* hoops of light over the run-up to the crevasse, closer together as the lip comes: the jump is seen coming */
  dress_style warn = st;
  warn.main = rgb(1.0f, 0.6f, 0.15f);
  warn.accent = rgb(1.0f, 0.85f, 0.3f);
  warn.lamp = rgb(1.0f, 0.65f, 0.2f);
  deco_banners(root, t, (float)F.lm.ramp0 - 230.0f, (float)F.lm.ramp0 - 10.0f, 55.0f, &warn);
  /* big crystals standing along the ice slide, tall enough to see from far up the road */
  {
    prop_set *pc = ps_new(root, "SlideCrystals");
    for (float s2 = (float)F.lm.slide0 + 20.0f; s2 < (float)F.lm.gap0 - 100.0f; s2 += 55.0f) {
      int side = ((int)(s2 / 55.0f)) & 1 ? 1 : -1;
      tsample a = track_at(t, s2);
      t3_vec3 p = track_point(t, s2, side * (a.hw + 16.0f + 10.0f * hashf2((int)s2, 5, 3)), 0);
      p.y = terrain(p.x, p.z, NULL) - 0.3f;
      float sc = 3.4f + 1.6f * hashf2((int)s2, 6, 3);
      ps_add_ex(pc, PK_CRYSTAL, (int)(s2 / 55.0f) & 3, 0, p, t3_quat_from_axis_angle(V3(0, 1, 0), s2), V3(sc, sc, sc), (pcol){ 1, 1, 1 }, (uint32_t)s2);
    }
    ps_shadows(pc, false);
    ps_finish(pc);
  }
  deco_supports(root, t, SUP_ICE, ground_cb, NULL, 0, 2.5f, &st);
  /* the ice chute: cold blue posts down its banks, ice spikes on the island noses, the sign at its mouth */
  branch_dress bd = branch_dress_defaults();
  bd.st = st;
  bd.st.lamp = rgb(0.4f, 0.9f, 1.0f);
  bd.lamps = LAMP_GLOW_POST, bd.lamp_spacing = 13;
  bd.tip = PK_ICE_SPIKES, bd.tip_scale = 0.9f;
  deco_branches(root, t, &bd);
}

/* ── the crevasse: a glowing gorge, a rope bridge each side, prayer flags ─────────────────────── */

/* a point in the gorge's own frame: a along the road's way across, b across it, y the height */
static t3_vec3 gorge_pt(float a, float b, float y) {
  const landmarks *L = &F.lm;
  return V3(L->gmid.x + L->gfwd.x * a - L->gfwd.z * b, y, L->gmid.z + L->gfwd.z * a + L->gfwd.x * b);
}
/* the land at a gorge point as it is without the cut */
static float rim_height(float a, float b) {
  t3_vec3 p = gorge_pt(a, b, 0);
  bool was = F.no_gorge;
  F.no_gorge = true;
  float h = terrain(p.x, p.z, NULL);
  F.no_gorge = was;
  return h;
}

#define NFLAGSTR 4
static struct {
  t3_geometry *g;
  float *base;       /* the flags at rest */
  float *dir;        /* per flag: the way it blows (unit, across its string) */
  float *amp;
  int n;
} FL;
static t3_vec3 flag_pole_top[4];

static void flag_string(gm *flags, t3_vec3 p0, t3_vec3 p1, float sag, uint32_t seed) {
  /* a string of small cloth flags along a sagging line; they are 4 vertices each, so the frame can ripple them */
  static const unsigned PC[5] = { 0x3d9bff, 0xf4f8ff, 0xff4a4a, 0x2fd67a, 0xffd23d };
  float len = sqrtf((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y) + (p1.z - p0.z) * (p1.z - p0.z));
  int n = (int)(len / 1.15f);
  uint32_t r = seed;
  for (int i = 0; i < n; i++) {
    float u0 = ((float)i + 0.12f) / (float)n, u1 = ((float)i + 0.88f) / (float)n;
    t3_vec3 a = V3(p0.x + (p1.x - p0.x) * u0, p0.y + (p1.y - p0.y) * u0 - sag * 4 * u0 * (1 - u0), p0.z + (p1.z - p0.z) * u0);
    t3_vec3 b = V3(p0.x + (p1.x - p0.x) * u1, p0.y + (p1.y - p0.y) * u1 - sag * 4 * u1 * (1 - u1), p0.z + (p1.z - p0.z) * u1);
    float h = 0.75f + 0.15f * rnd1(&r);
    pcol col = hex(PC[(i + (int)(seed % 5u)) % 5]);
    col = pc_mul(col, 0.8f);
    t3_vec3 n3 = V3(0, 1, 0);
    int v0 = gm_vtx(flags, a, n3, 0, 0, col), v1 = gm_vtx(flags, b, n3, 1, 0, col);
    int v2 = gm_vtx(flags, V3(b.x, b.y - h, b.z), n3, 1, 1, pc_mul(col, 0.85f)), v3 = gm_vtx(flags, V3(a.x, a.y - h, a.z), n3, 0, 1, pc_mul(col, 0.85f));
    gm_tri(flags, v0, v1, v2), gm_tri(flags, v0, v2, v3);
  }
}

static void build_crevasse(cw_node root, const track *t) {
  const landmarks *L = &F.lm;
  float half = L->glen * 0.5f + 0.4f, fy = F.gorge_floor;
  uint32_t r = 909;
  (void)t;
  /* the sheer walls: faceted ice with a glow that climbs up from the depths */
  t3_material *wall = phys((t3_color){ 1, 1, 1 }, 0.35f, 0.0f);
  wall->vertex_colors = true;
  wall->side = T3_DOUBLE_SIDE;
  tex_apply(wall, TEX_WALL_ICE, 1, 1, 1.0f, 1.0f);
  wall->color = (t3_color){ 0.9f, 1.1f, 1.3f };
  wall->emissive = (t3_color){ 0.02f, 0.28f, 0.6f };
  wall->emissive_intensity = 0.6f;
  gm gw = { 0 };
  for (int side = -1; side <= 1; side += 2) {
    float a0 = side * half;
    for (float b = -100.0f; b < 100.0f; b += 3.0f) {
      float h0 = rim_height(a0, b), h1 = rim_height(a0, b + 3.0f);
      const int rows = 5;
      for (int k = 0; k < rows; k++) {
        float f0 = (float)k / (float)rows, f1 = (float)(k + 1) / (float)rows;
        /* each row steps in and out a little, so the wall is a stack of ledges */
        float o0 = (k == 0 ? 0 : 1.1f * (hashf2((int)(b * 0.33f), k, 17) - 0.5f) * 2), o1 = (k + 1 == rows ? 0 : 1.1f * (hashf2((int)(b * 0.33f), k + 1, 17) - 0.5f) * 2);
        float o0b = (k == 0 ? 0 : 1.1f * (hashf2((int)(b * 0.33f) + 1, k, 17) - 0.5f) * 2), o1b = (k + 1 == rows ? 0 : 1.1f * (hashf2((int)(b * 0.33f) + 1, k + 1, 17) - 0.5f) * 2);
        float y00 = fy + (h0 - fy) * f0, y01 = fy + (h0 - fy) * f1, y10 = fy + (h1 - fy) * f0, y11 = fy + (h1 - fy) * f1;
        t3_vec3 q0 = gorge_pt(a0 - side * o0, b, y00), q1 = gorge_pt(a0 - side * o0b, b + 3.0f, y10);
        t3_vec3 q2 = gorge_pt(a0 - side * o1b, b + 3.0f, y11), q3 = gorge_pt(a0 - side * o1, b, y01);
        float fade = cw_smoothstep(75.0f, 130.0f, fabsf(b));
        if (fade > 0.97f) continue;
        float v = 0.5f + 0.5f * hashf2((int)(b * 0.33f), k, 19);
        pcol c = pc_mix(pc_mix(hex(0x1e78c8), hex(0xcfeeff), f1 * 0.7f + 0.3f * v), hex(0x3de8ff), (1 - f1) * 0.5f);
        gm_quad(&gw, q0, q1, q2, q3, vsub3(gorge_pt(0, b, fy + 6), q0), c, 5.0f);
      }
    }
  }
  gm_finish(&gw, root, "GorgeWalls", wall, false, true);
  t3_release(wall);
  /* the glow: an emissive floor, and sheets of cold light standing up from it, additive */
  t3_material *glowm = t3_mesh_basic_material_new(0xffffff);
  glowm->vertex_colors = true;
  glowm->transparent = true;
  glowm->blending = T3_ADDITIVE_BLENDING;
  glowm->depth_write = false;
  glowm->side = T3_DOUBLE_SIDE;
  AN.crev = glowm;
  gm gg = { 0 };
  t3_vec3 up = V3(0, 1, 0);
  for (float b = -80.0f; b < 80.0f; b += 8.0f)
    for (float a = -half; a < half; a += 8.0f) {
      float cb = 0.55f + 0.45f * sinf(b * 0.21f + a * 0.35f), cc = 0.5f + 0.5f * sinf(b * 0.11f - a * 0.5f + 1.0f);
      pcol c0 = pc_mix(hex(0x0a5a9a), hex(0x3de8ff), cb * 0.8f);
      pcol c1 = pc_mix(hex(0x0a5a9a), hex(0x8affff), cc * 0.8f);
      int v0 = gm_vtx(&gg, gorge_pt(a, b, fy + 0.7f), up, 0, 0, pc_mul(c0, 1.6f)), v1 = gm_vtx(&gg, gorge_pt(a + 8, b, fy + 0.7f), up, 1, 0, pc_mul(c1, 1.6f));
      int v2 = gm_vtx(&gg, gorge_pt(a + 8, b + 8, fy + 0.7f), up, 1, 1, pc_mul(c0, 1.6f)), v3 = gm_vtx(&gg, gorge_pt(a, b + 8, fy + 0.7f), up, 0, 1, pc_mul(c1, 1.6f));
      gm_tri(&gg, v0, v1, v2), gm_tri(&gg, v0, v2, v3);
    }
  /* the sheets: they hang along both walls and rise out of the depths */
  for (int side = -1; side <= 1; side += 2)
    for (float b = -64.0f; b <= 64.0f; b += 16.0f) {
      float a = side * (half - 0.8f);
      float hh = 20.0f + 14.0f * hashf2((int)b, side + 3, 23);
      pcol lo = pc_mul(hex(0x35e0ff), 1.2f), hi = (pcol){ 0, 0, 0 };
      int v0 = gm_vtx(&gg, gorge_pt(a, b, fy), up, 0, 0, lo), v1 = gm_vtx(&gg, gorge_pt(a, b + 16, fy), up, 1, 0, lo);
      int v2 = gm_vtx(&gg, gorge_pt(a, b + 16, fy + hh), up, 1, 1, hi), v3 = gm_vtx(&gg, gorge_pt(a, b, fy + hh), up, 0, 1, hi);
      gm_tri(&gg, v0, v1, v2), gm_tri(&gg, v0, v2, v3);
    }
  gm_finish(&gg, root, "GorgeGlow", glowm, false, false);
  point_light(root, "CrevasseLight", gorge_pt(0, 0, fy + 12.0f), 0.25f, 0.85f, 1.0f, 14.0f, 70.0f);

  /* ice spikes standing along both lips, leaving the road and the bridges clear */
  prop_set *ps = ps_new(root, "GorgeLips");
  for (int side = -1; side <= 1; side += 2)
    for (float b = -100.0f; b <= 100.0f; b += 5.5f) {
      if (fabsf(b) < 14.0f || fabsf(fabsf(b) - 26.0f) < 4.5f) continue;
      float a = side * (half + 1.5f + 1.5f * hashf2((int)b, side, 29));
      t3_vec3 p = gorge_pt(a, b + 2.0f * rnds(&r), 0);
      p.y = rim_height(a, b) - 0.25f;
      float yaw = rnd1(&r) * TAU_F, sc = 0.8f + 1.3f * rnd1(&r);
      ps_add(ps, hashf2((int)b, side, 31) > 0.35f ? PK_ICE_SPIKES : PK_ICE_BOULDER, p, yaw, sc, (uint32_t)(b * 7 + side));
    }
  ps_finish(ps);

  /* the rope bridges, one each side of the road */
  pmc *br = pmc_new(80, 33);
  for (int side = -1; side <= 1; side += 2) {
    float b0 = side * 26.0f;
    float y0 = rim_height(-half - 3.0f, b0) - 0.1f, y1 = rim_height(half + 3.0f, b0) - 0.1f;
    float span = 2 * (half + 3.0f);
    int planks = (int)(span / 0.95f);
    t3_vec3 e0 = gorge_pt(-half - 3.0f, b0, y0), e1 = gorge_pt(half + 3.0f, b0, y1);
    t3_vec3 dirv = vnorm3(vsub3(e1, e0));
    t3_vec3 side_v = vnorm3(V3(-dirv.z, 0, dirv.x));
    pm *b = pmc_at(br, e0);
    pm_push(b);
    pm_layer(b, PL_MAIN);
    pcol wood0 = hex(0x4a3524), wood1 = hex(0x9a7a52), rope = hex(0xc8b080);
    for (int i = 0; i <= planks; i++) {
      float u = (float)i / (float)planks;
      float sag = 2.6f * 4.0f * u * (1 - u);
      t3_vec3 c = V3(e0.x + (e1.x - e0.x) * u, e0.y + (e1.y - e0.y) * u - sag, e0.z + (e1.z - e0.z) * u);
      pm_push(b);
      pm_basis(b, c, side_v, V3(0, 1, 0), dirv);
      pm_roty(b, 0.04f * rnds(&r));
      pm_box_c(b, 3.1f, 0.11f, 0.78f, pc_mix(wood0, wood1, 0.3f + 0.6f * rnd1(&r)));
      pm_pop(b);
      /* handrails and their hangers */
      if (i < planks) {
        float u2 = (float)(i + 1) / (float)planks;
        float sag2 = 2.6f * 4.0f * u2 * (1 - u2);
        t3_vec3 c2 = V3(e0.x + (e1.x - e0.x) * u2, e0.y + (e1.y - e0.y) * u2 - sag2, e0.z + (e1.z - e0.z) * u2);
        for (int k = -1; k <= 1; k += 2) {
          t3_vec3 o = vmul3(side_v, k * 1.65f);
          pm_rod(b, vadd3(vadd3(c, o), V3(0, 1.25f, 0)), vadd3(vadd3(c2, o), V3(0, 1.25f, 0)), 0.045f, 4, rope);
          if (i % 3 == 0) pm_rod(b, vadd3(c, o), vadd3(vadd3(c, o), V3(0, 1.25f, 0)), 0.03f, 4, rope);
        }
      }
    }
    for (int e = 0; e < 2; e++) {
      t3_vec3 c = e ? e1 : e0;
      for (int k = -1; k <= 1; k += 2) {
        t3_vec3 pp = vadd3(c, vmul3(side_v, k * 1.65f));
        pm_rod(b, vadd3(pp, V3(0, -0.6f, 0)), vadd3(pp, V3(0, 3.0f, 0)), 0.12f, 6, wood0);
        pm_push(b);
        pm_move(b, pp.x, pp.y + 3.1f, pp.z);
        pm_ball(b, 0.2f, 0.18f, 0.2f, 0, 0.1f, wood0, wood1);
        pm_pop(b);
      }
    }
    pm_pop(b);
  }
  pmc_finish(br, root, "RopeBridges", true, TEX_WALL_WOOD);

  /* the masts and the prayer flags: poles at the take-off and the landing, strings between over the gorge */
  pmc *masts = pmc_new(80, 34);
  gm flags = { 0 };
  float mast_h = 17.0f;
  for (int side = -1; side <= 1; side += 2) {
    for (int row = 0; row < 2; row++) {
      float bb = side * (row ? 26.0f : 14.5f);
      t3_vec3 top[2];
      for (int e = 0; e < 2; e++) {
        float a = (e ? 1 : -1) * (half + 6.0f);
        float y = rim_height(a, bb);
        t3_vec3 base = gorge_pt(a, bb, y - 0.5f);
        pm *b = pmc_at(masts, base);
        pm_push(b);
        pm_layer(b, PL_MAIN);
        pm_move(b, base.x, base.y, base.z);
        pm_roty(b, rnd1(&r) * 3);
        pm_vary(b, 0.05f);
        pm_cyl_ex(b, 0.34f, 0.2f, mast_h, 7, hex(0x4a3524), hex(0x9a7a52), false, true);
        pm_push(b);
        pm_ball(b, 1.9f, 0.8f, 1.9f, 1, 0.12f, SNOW_C, SNOW_C);
        pm_pop(b);
        for (int k = 0; k < 3; k++) {
          pm_push(b);
          pm_move(b, 0, 1.0f + 0.4f * (float)k, 0);
          ice_block(b, 1.1f, 1.0f, 1.1f, 0.3f * (float)(k + 1), ICE_DEEP, ICE_HI);
          pm_pop(b);
        }
        pm_pop(b);
        top[e] = V3(base.x, base.y + mast_h - 0.4f - 1.1f * (float)row, base.z);
      }
      flag_string(&flags, top[0], top[1], 3.6f - 0.6f * (float)row, (uint32_t)(11 + row + (side > 0 ? 3 : 0)));
      if (row == 0) {
        /* a second string, higher up, to the opposite mast across the gorge */
        flag_string(&flags, V3(top[0].x, top[0].y + 1.3f, top[0].z), V3(top[1].x, top[1].y + 1.3f, top[1].z), 3.0f, (uint32_t)(5 + (side > 0 ? 4 : 0)));
      }
      flag_pole_top[0] = top[0];
    }
  }
  pmc_finish(masts, root, "FlagMasts", true, TEX_WALL_WOOD);
  /* the flags go in a dynamic mesh: the frame ripples them */
  if (flags.ni > 0) {
    t3_material *fm = t3_mesh_basic_material_new(0xffffff);
    fm->vertex_colors = true;
    fm->side = T3_DOUBLE_SIDE;
    FL.n = flags.nv;
    FL.base = malloc(sizeof(float) * 3 * (size_t)flags.nv);
    memcpy(FL.base, flags.p, sizeof(float) * 3 * (size_t)flags.nv);
    t3_geometry *g = t3_geometry_new();
    t3_attribute *a;
    a = t3_attribute_new(T3_FLOAT32, flags.p, flags.nv, 3), a->usage = T3_DYNAMIC_DRAW, t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
    a = t3_attribute_new(T3_FLOAT32, flags.n, flags.nv, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
    a = t3_attribute_new(T3_FLOAT32, flags.c, flags.nv, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
    a = t3_attribute_new(T3_UINT32, flags.ix, flags.ni, 1), t3_geometry_set_index(g, a), t3_release(a);
    t3_geometry_compute_bounding_sphere(g);
    free(flags.p), free(flags.n), free(flags.uv), free(flags.c), free(flags.ix);
    FL.g = g;
    t3_mesh *me = t3_mesh_new(g, fm);
    me->base.frustum_culled = false;
    me->base.cast_shadow = false;
    hang(root, "PrayerFlags", &me->base);
    t3_release(me);
    t3_release(fm);
  }
}

static void flags_frame(float t) {
  if (!FL.g) return;
  float *P = t3_attribute_f32(FL.g->attributes[T3_ATTR_POSITION]);
  for (int i = 0; i + 3 < FL.n; i += 4) {
    /* each flag: its two lower corners swing in and out of the string's line with the wind, out of step */
    float ph = (float)i * 0.37f;
    float w = 0.5f * sinf(t * 3.1f + ph) + 0.25f * sinf(t * 5.3f + ph * 1.7f);
    float *a = &FL.base[i * 3];
    for (int k = 0; k < 4; k++) {
      float x = a[k * 3], y = a[k * 3 + 1], z = a[k * 3 + 2];
      float low = (k >= 2) ? 1.0f : 0.0f;
      float sway = w * low * 0.45f;
      /* blow across the gorge's width */
      P[(i + k) * 3] = x + F.lm.gfwd.z * sway;
      P[(i + k) * 3 + 1] = y + 0.12f * low * sinf(t * 4.0f + ph + (float)k);
      P[(i + k) * 3 + 2] = z - F.lm.gfwd.x * sway;
    }
  }
  FL.g->attributes[T3_ATTR_POSITION]->version++;
}

/* ── the frozen waterfalls at the cave's two mouths ─────────────────────────────────────────────── */

/* dir +1: the mouth the road enters by (the mountain lies ahead); -1: the mouth it leaves by (the mountain lies behind) */
static void build_portal(cw_node root, const track *t, int idx, int dir, uint32_t seed) {
  const tsample *a0 = &t->s[idx];
  tsample a = *a0;
  if (dir < 0) a.r = vmul3(a.r, -1.0f), a.f = vmul3(a.f, -1.0f);
  float Rr = a.hw, hc = 1.6f, Rt = 6.4f;
  uint32_t r = seed;
  pmc *ice = pmc_new(60, seed + 1);
  pm *b = frame_at(ice, &a, 0, 0);
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.07f);
  /* the ring of ice blocks round the mouth */
  const int nb = 13;
  for (int k = 0; k <= nb; k++) {
    float ang = CW_PI * (float)k / (float)nb;
    float x = -cosf(ang) * (Rr + 1.5f), y = hc + sinf(ang) * (Rt + 1.4f);
    pm_push(b);
    pm_move(b, x, y, 0.4f * rnds(&r));
    pm_rotz(b, ang - CW_PI * 0.5f);
    float bx = 2.5f + 0.5f * rnd1(&r), by = 2.8f + 0.5f * rnd1(&r), bk = 0.3f + 0.4f * rnd1(&r);
    pm_box_c(b, bx, by, 3.0f, pc_mix(ICE_DEEP, ICE_LO, bk));
    pm_pop(b);
  }
  for (int side = -1; side <= 1; side += 2)
    for (int k = 0; k < 2; k++) {
      pm_push(b);
      float mx = rnds(&r), mz = rnds(&r);
      pm_move(b, side * (Rr + 1.5f + 0.2f * mx), -0.6f + 1.7f * (float)k, 0.3f * mz);
      ice_block(b, 2.8f, 2.1f, 3.0f, 0.15f * rnds(&r), ICE_DEEP, ICE_HI);
      pm_pop(b);
    }
  /* the waterfall: fluted columns poured down the face above and beside the mouth, thick at the foot */
  for (int i = 0; i < 26; i++) {
    float f = (float)i / 25.0f;
    float x = (f - 0.5f) * 2.0f * (Rr + 9.0f) + 0.6f * rnds(&r);
    float roof = 6.4f + 1.6f + 1.4f;
    float ax = fabsf(x) / (Rr + 1.5f);
    float base_y = ax < 1.0f ? hc + sqrtf(1 - ax * ax) * roof : -0.4f;
    float top_y = 13.5f + 1.5f * rnds(&r);
    float rad = 0.7f + 0.9f * rnd1(&r);
    float zz = 1.6f + 3.4f * rnd1(&r);
    pm_push(b);
    pm_move(b, x, base_y - 0.3f, zz);
    pm_rotx(b, 0.04f * rnds(&r));
    pm_rotz(b, 0.05f * rnds(&r));
    pm_jitter(b, 0.18f);
    float k0 = rnd1(&r), k1 = 0.55f + 0.4f * rnd1(&r);
    pm_cyl_ex(b, rad, rad * 0.62f, top_y - base_y, 7, pc_mix(ICE_DEEP, ICE_LO, k0), pc_mix(ICE_LO, ICE_HI, k1), false, false);
    pm_jitter(b, 0);
    /* a bulb of ice where it landed */
    pm_ball(b, rad * 1.5f, rad * 1.1f, rad * 1.5f, 1, 0.2f, pc_mix(ICE_LO, ICE_HI, 0.3f), ICE_HI);
    pm_pop(b);
  }
  /* icicles fringing the lintel */
  for (int k = 0; k < 30; k++) {
    float ang = CW_PI * rnd1(&r);
    float x = -cosf(ang) * (Rr + 0.6f), y = hc + sinf(ang) * (Rt + 0.5f);
    pm_push(b);
    pm_move(b, x, y, 0.5f * rnds(&r));
    float ir = 0.1f + 0.1f * rnd1(&r), il = 0.7f + 2.2f * rnd1(&r);
    icicle(b, ir, il, ICE_HI);
    pm_pop(b);
  }
  pm_pop(b);
  pmc_finish(ice, root, dir > 0 ? "WaterfallIn" : "WaterfallOut", true, TEX_WALL_ICE);
  /* glowing veins inside the frozen falls */
  pmc *gl = pmc_new(60, seed + 2);
  b = frame_at(gl, &a, 0, 0);
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < 14; i++) {
    float x = (rnd1(&r) - 0.5f) * 2.0f * (Rr + 8.0f);
    float ax = fabsf(x) / (Rr + 1.5f);
    float y0 = (ax < 1.0f ? hc + sqrtf(1 - ax * ax) * 8.4f : 0.0f) + 0.5f;
    pm_push(b);
    pm_move(b, x, y0, 0.8f);
    pm_cyl_ex(b, 0.07f, 0.04f, 12.0f - y0 * 0.2f, 4, glow(hex(0x0a8ad0), 1.2f), glow(hex(0x7aeaff), 2.4f), false, false);
    pm_pop(b);
  }
  pm_pop(b);
  pmc_finish(gl, root, "WaterfallGlow", false, -1);
}

/* ── the summit: an abandoned research station and a radio mast with a red light ────────────────── */

static t3_object *BEACON;
static t3_object *ANEMO;

static void radio_mast(pm *b, float H, pcol steel, pcol paint) {
  /* four legs tapering to the top, cross braced every 3 m, red and white sections */
  float w0 = 1.9f, w1 = 0.35f;
  int segs = (int)(H / 3.0f);
  for (int k = 0; k < segs; k++) {
    float f0 = (float)k / (float)segs, f1 = (float)(k + 1) / (float)segs;
    float y0 = H * f0, y1 = H * f1, a0 = w0 + (w1 - w0) * f0, a1 = w0 + (w1 - w0) * f1;
    pcol c = ((k / 2) & 1) ? paint : steel;
    for (int i = 0; i < 4; i++) {
      float sx0 = (i & 1) ? a0 : -a0, sz0 = (i & 2) ? a0 : -a0, sx1 = (i & 1) ? a1 : -a1, sz1 = (i & 2) ? a1 : -a1;
      pm_beam(b, V3(sx0, y0, sz0), V3(sx1, y1, sz1), 0.12f, c);
      int j = (i & 1) ? (i + 1) & 3 : (i + 3) & 3; /* the neighbour to brace to */
      (void)j;
    }
    /* the braces: an X on each of the four faces */
    for (int face = 0; face < 4; face++) {
      float sx = face == 0 ? -1 : face == 1 ? 1 : 0, sz = face == 2 ? -1 : face == 3 ? 1 : 0;
      t3_vec3 ax = face < 2 ? V3(0, 0, 1) : V3(1, 0, 0);
      t3_vec3 c0 = V3(sx * a0 + ax.x * -a0, y0, sz * a0 + ax.z * -a0), c1 = V3(sx * a0 + ax.x * a0, y0, sz * a0 + ax.z * a0);
      t3_vec3 d0 = V3(sx * a1 + ax.x * -a1, y1, sz * a1 + ax.z * -a1), d1 = V3(sx * a1 + ax.x * a1, y1, sz * a1 + ax.z * a1);
      pm_beam(b, c0, d1, 0.07f, steel);
      pm_beam(b, c1, d0, 0.07f, steel);
      pm_beam(b, c0, c1, 0.07f, steel);
    }
  }
}

static void build_summit(cw_node root, const track *t) {
  if (F.npads < 1) return;
  float px = F.pad[0].x, pz = F.pad[0].z, py = F.pad[0].y;
  /* turn the station to face back down the road toward the first switchbacks */
  float yaw = atan2f(F.lm.centre.x - px, F.lm.centre.z - pz);
  uint32_t r = 7001;
  (void)t;
  pmc *c = pmc_new(70, 77);
  pm *b = stand_at(c, V3(px, py - 0.2f, pz), yaw);
  pcol hut0 = hex(0x7a2f1c), hut1 = hex(0xd9663a), teal0 = hex(0x1f4a52), teal1 = hex(0x4f9aa5), snow = SNOW_C;
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.05f);
  /* the main hut: boxy, rust red, a pitched roof under a heap of snow, a lean-to on its flank */
  pm_push(b);
  pm_move(b, 0, 0, 0);
  pm_box(b, 11.0f, 4.2f, 6.6f, hut0, hut1);
  pm_move(b, 0, 4.2f, 0);
  pm_prism(b, 7.4f, 2.4f, 11.4f, pc_mix(hut0, hut1, 0.3f), hut1);
  pm_push(b);
  pm_roty(b, CW_PI * 0.5f);
  pm_move(b, 0, 0.04f, 0);
  pm_prism(b, 7.9f, 2.55f, 11.9f, snow, snow);
  pm_pop(b);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 9.5f, 0, 1.2f);
  pm_box(b, 8.0f, 3.1f, 3.4f, teal0, teal1);
  pm_move(b, 0, 3.1f, 0);
  pm_box(b, 8.2f, 0.45f, 3.6f, snow, snow);
  pm_pop(b);
  pm_push(b);
  pm_move(b, -8.8f, 0, -1.0f);
  pm_box(b, 6.0f, 2.8f, 3.0f, hex(0x3a3d46), hex(0x5a5f6c));
  pm_move(b, 0, 2.8f, 0);
  pm_box(b, 6.2f, 0.5f, 3.2f, snow, snow);
  pm_pop(b);
  /* drifts piled against the windward side and half over the doorway */
  for (int i = 0; i < 9; i++) {
    pm_push(b);
    float mx = rnds(&r), mz = rnd1(&r);
    pm_move(b, -7.0f + 1.8f * (float)i + 0.5f * mx, 0, 3.6f + 0.8f * mz);
    float bx = 2.3f + rnd1(&r), by = 0.9f + 0.5f * rnd1(&r), bz = 1.6f + 0.6f * rnd1(&r);
    pm_ball(b, bx, by, bz, 1, 0.15f, pc_mix(snow, ICE_LO, 0.25f), snow);
    pm_pop(b);
  }
  /* a stair, a door */
  pm_push(b);
  pm_move(b, 0.5f, 0, 3.4f);
  pm_box(b, 1.6f, 2.6f, 0.2f, hex(0x2a1c18), hex(0x4a342c));
  pm_pop(b);
  /* the radio mast's footing and its guy wires */
  pm_push(b);
  pm_move(b, 0, 0, -14.0f);
  pm_box(b, 5.2f, 0.8f, 5.2f, hex(0x7a7f88), hex(0xa0a6b0));
  pm_move(b, 0, 0.8f, 0);
  radio_mast(b, 36.0f, hex(0xe8ecf4), hex(0xd23a2a));
  pm_pop(b);
  pm_layer(b, PL_METAL);
  for (int k = 0; k < 3; k++) {
    float an = TAU_F * (float)k / 3.0f + 0.5f;
    t3_vec3 top = V3(0, 22.0f, -14.0f), foot = V3(cosf(an) * 17.0f, 0.0f, -14.0f + sinf(an) * 17.0f);
    pm_rod(b, top, foot, 0.04f, 4, hex(0x30343c));
    pm_push(b);
    pm_move(b, foot.x, 0, foot.z);
    pm_box_c(b, 1.2f, 0.8f, 1.2f, hex(0x6a6f7a));
    pm_pop(b);
  }
  /* a lit window or two: the generator still runs */
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < 3; i++) {
    pm_push(b);
    pm_move(b, -3.6f + 3.4f * (float)i, 1.9f, 3.31f);
    pm_box_c(b, 1.5f, 1.1f, 0.08f, glow(i == 1 ? hex(0xffb050) : hex(0x203040), i == 1 ? 1.8f : 0.4f));
    pm_pop(b);
  }
  pm_layer(b, PL_MAIN);
  pm_pop(b);
  pmc_finish(c, root, "Station", true, -1);
  /* the beacon at the mast's top */
  t3_vec3 top = V3(px, py + 37.3f, pz);
  {
    float cy = cosf(yaw), sy = sinf(yaw);
    /* local (0, y, -14) turned by yaw about the vertical */
    top.x = px + (0 * cy + (-14.0f) * sy);
    top.z = pz + (-0 * sy + (-14.0f) * cy);
  }
  t3_material *bm = t3_mesh_basic_material_new(0xffffff);
  bm->fog = false;
  bm->color = (t3_color){ 4.0f, 0.05f, 0.03f };
  AN.beacon = bm;
  t3_mesh *me = t3_mesh_new(t3_sphere_geometry_new(0.7f, 12, 8), bm);
  me->base.cast_shadow = false;
  cw_node n = hang(root, "Beacon", &me->base);
  cw_node3d_set_position(n, top);
  BEACON = &me->base;
  t3_release(me);
  /* the anemometer on the mast: three cups that spin in the wind */
  {
    pm *ab = pm_new(33);
    pm_layer(ab, PL_METAL);
    for (int k = 0; k < 3; k++) {
      float an = TAU_F * (float)k / 3.0f;
      pm_rod(ab, V3(0, 0, 0), V3(cosf(an) * 0.9f, 0, sinf(an) * 0.9f), 0.04f, 4, hex(0xd8dce8));
      pm_push(ab);
      pm_move(ab, cosf(an) * 0.95f, 0, sinf(an) * 0.95f);
      pm_roty(ab, -an);
      pm_ball(ab, 0.22f, 0.22f, 0.22f, 1, 0, hex(0xd23a2a), hex(0xff7a5a));
      pm_pop(ab);
    }
    pm_rod(ab, V3(0, -0.6f, 0), V3(0, 0.1f, 0), 0.05f, 4, hex(0xd8dce8));
    prop_model am = pm_finish(ab);
    if (am.g[PL_METAL]) {
      t3_mesh *ame = t3_mesh_new(am.g[PL_METAL], prop_material(PL_METAL, -1, (pcol){ 1, 1, 1 }, 1.0f));
      ame->base.cast_shadow = false;
      cw_node an = hang(root, "Anemometer", &ame->base);
      cw_node3d_set_position(an, V3(top.x, py + 31.0f, top.z));
      ANEMO = &ame->base;
      t3_release(ame);
    }
  }
  /* a dish on a pad and a few drums */
  prop_set *ps = ps_new(root, "StationProps");
  float cyw = cosf(yaw), syw = sinf(yaw);
  #define LOCAL(x, z) V3(px + (x) * cyw + (z) * syw, py - 0.15f, pz - (x) * syw + (z) * cyw)
  ps_add(ps, PK_DISH, LOCAL(-6.0f, 8.0f), yaw + 0.6f, 1.1f, 1);
  ps_add(ps, PK_SOLAR_PANEL, LOCAL(6.0f, 9.0f), yaw, 1.0f, 2);
  ps_add(ps, PK_SOLAR_PANEL, LOCAL(9.4f, 9.4f), yaw, 1.0f, 3);
  for (int i = 0; i < 4; i++) ps_add(ps, PK_BARREL, LOCAL(-12.0f + 1.1f * (float)i, 3.0f - 0.7f * (float)i), 0.7f * (float)i, 1.0f, 10u + (uint32_t)i);
  ps_add(ps, PK_CRATE, LOCAL(-12.5f, 6.5f), 0.4f, 1.2f, 20);
  ps_add(ps, PK_CRATE, LOCAL(-11.0f, 7.4f), 1.0f, 1.0f, 21);
  #undef LOCAL
  ps_finish(ps);
}

/* ── the camps and the wrecks ───────────────────────────────────────────────────────────────────── */

static void build_camp(cw_node root, int pad, uint32_t seed) {
  if (pad >= F.npads) return;
  float px = F.pad[pad].x, pz = F.pad[pad].z, py = F.pad[pad].y;
  uint32_t r = seed;
  prop_set *ps = ps_new(root, "CampProps");
  for (int i = 0; i < 5; i++) {
    float a = rnd1(&r) * TAU_F, d = 5.5f + 7.0f * rnd1(&r);
    float yaw = rnd1(&r) * 6, sc = 1.0f + 0.3f * rnd1(&r);
    ps_add(ps, i & 1 ? PK_CRATE : PK_BARREL, V3(px + cosf(a) * d, py - 0.05f, pz + sinf(a) * d), yaw, sc, seed + 40u + (uint32_t)i);
  }
  ps_finish(ps);
  /* the fire in the middle, a ring of stones, logs, sleds, and the tents round them */
  pmc *c = pmc_new(60, seed);
  pm *b = stand_at(c, V3(px, py, pz), 0);
  pm_layer(b, PL_MAIN);
  {
    static const unsigned TENT[4] = { 0xf0762a, 0x2fa6b5, 0xffc12e, 0xd8402e };
    for (int i = 0; i < 4; i++) {
      float a = TAU_F * (float)i / 4.0f + 0.5f, d = 9.0f + 2.0f * rnd1(&r);
      pm_push(b);
      pm_move(b, cosf(a) * d, -0.05f, sinf(a) * d);
      pm_roty(b, -a + 1.57f + 0.3f * rnds(&r));
      pcol tc = hex(TENT[i]);
      /* an A-frame tent: two sloping flaps, a dark doorway, snow banked on its skirts */
      pm_prism(b, 3.6f, 2.5f, 4.6f, pc_mul(tc, 0.55f), tc);
      pm_push(b);
      pm_move(b, 0, 0, 2.31f);
      pm_quad(b, V3(-0.55f, 0.0f, 0), V3(0.55f, 0.0f, 0), V3(0.0f, 1.7f, 0), V3(0.0f, 1.7f, 0), hex(0x14161c));
      pm_pop(b);
      for (int k = -1; k <= 1; k += 2) {
        pm_push(b);
        pm_move(b, k * 1.8f, 0, 0);
        pm_ball(b, 0.7f, 0.45f, 2.5f, 1, 0.12f, pc_mix(SNOW_C, ICE_LO, 0.25f), SNOW_C);
        pm_pop(b);
      }
      pm_beam(b, V3(0, 2.5f, -2.3f), V3(0, 2.95f, -2.3f), 0.05f, hex(0x30343c));
      pm_pop(b);
    }
  }
  for (int i = 0; i < 9; i++) {
    float a = TAU_F * (float)i / 9.0f;
    pm_push(b);
    pm_move(b, cosf(a) * 1.6f, 0, sinf(a) * 1.6f);
    pm_rock(b, 0.45f, 0.7f, 0.25f, hex(0x2a2e3a), hex(0x6a7080));
    pm_pop(b);
  }
  for (int i = 0; i < 4; i++) {
    pm_push(b);
    pm_roty(b, 0.8f * (float)i);
    pm_move(b, 0, 0.35f, 0);
    pm_rotz(b, 0.3f);
    pm_cyl_ex(b, 0.16f, 0.14f, 2.2f, 6, hex(0x2a1a10), hex(0x6a4a30), true, true);
    pm_pop(b);
  }
  /* two sleds, runners up */
  for (int k = 0; k < 2; k++) {
    pm_push(b);
    pm_move(b, -6.0f + 2.4f * (float)k, 0.1f, -4.5f - 1.5f * (float)k);
    pm_roty(b, 0.5f + (float)k);
    pm_box(b, 1.1f, 0.18f, 3.2f, hex(0x5a3a22), hex(0x9a7a52));
    pm_beam(b, V3(-0.5f, 0, -1.6f), V3(-0.5f, 0.2f, 1.9f), 0.08f, hex(0x30343c));
    pm_beam(b, V3(0.5f, 0, -1.6f), V3(0.5f, 0.2f, 1.9f), 0.08f, hex(0x30343c));
    pm_pop(b);
  }
  pm_pop(b);
  pmc_finish(c, root, "CampBits", true, -1);
  /* the flame, animated, and the lantern posts, lit */
  pm *fb = pm_new(seed + 5);
  pm_layer(fb, PL_GLOW);
  for (int i = 0; i < 5; i++) {
    float a = TAU_F * (float)i / 5.0f;
    pm_push(fb);
    pm_move(fb, cosf(a) * 0.35f, 0.35f, sinf(a) * 0.35f);
    pm_rotz(fb, cosf(a) * 0.35f);
    pm_rotx(fb, sinf(a) * 0.35f);
    pm_cyl_ex(fb, 0.3f - 0.03f * (float)(i & 1), 0.001f, 1.5f + 0.35f * (float)(i % 3), 6, glow(hex(0xff5a10), 2.8f), glow(hex(0xffd070), 4.2f), false, false);
    pm_pop(fb);
  }
  prop_model fm = pm_finish(fb);
  if (fm.g[PL_GLOW]) {
    t3_material *m = glow_material();
    AN.flame[1] = m;
    t3_mesh *me = t3_mesh_new(fm.g[PL_GLOW], m);
    me->base.cast_shadow = false;
    cw_node n = hang(root, "Bonfire", &me->base);
    cw_node3d_set_position(n, V3(px, py, pz));
    t3_release(me);
  }
  point_light(root, "CampFire", V3(px, py + 2.2f, pz), 1.0f, 0.55f, 0.22f, 9.0f, 30.0f);
  fx_ambient_at(root, FXA_EMBERS, V3(px, py + 1.2f, pz), V3(0.5f, 0.2f, 0.5f), 0.6f, 120.0f);
  /* lantern posts round the camp */
  pm *lb = pm_new(seed + 9);
  for (int i = 0; i < 4; i++) {
    float a = TAU_F * ((float)i + 0.5f) / 4.0f;
    pm_push(lb);
    pm_move(lb, cosf(a) * 14.0f, 0, sinf(a) * 14.0f);
    pm_layer(lb, PL_MAIN);
    pm_cyl_ex(lb, 0.1f, 0.07f, 3.2f, 5, hex(0x3a2818), hex(0x7a5a3a), false, true);
    pm_push(lb);
    pm_move(lb, 0.5f, 3.0f, 0);
    pm_rod(lb, V3(-0.5f, 0.2f, 0), V3(0, 0, 0), 0.04f, 4, hex(0x3a2818));
    pm_move(lb, 0, -1.0f, 0);
    lantern(lb, 1.0f, LANT_C, 3.4f);
    pm_pop(lb);
    pm_pop(lb);
  }
  prop_model lm = pm_finish(lb);
  t3_object *grp = t3_group_new();
  if (lm.g[PL_MAIN]) {
    t3_mesh *me = t3_mesh_new(lm.g[PL_MAIN], prop_material(PL_MAIN, -1, (pcol){ 1, 1, 1 }, 1.0f));
    t3_object_add(grp, &me->base), t3_release(me);
  }
  if (lm.g[PL_METAL]) {
    t3_mesh *me = t3_mesh_new(lm.g[PL_METAL], prop_material(PL_METAL, -1, (pcol){ 1, 1, 1 }, 1.0f));
    t3_object_add(grp, &me->base), t3_release(me);
  }
  if (lm.g[PL_GLOW]) {
    t3_material *m = glow_material();
    AN.flame[2] = AN.flame[2] ? AN.flame[2] : m;
    t3_mesh *me = t3_mesh_new(lm.g[PL_GLOW], AN.flame[2]);
    t3_object_add(grp, &me->base), t3_release(me);
    if (AN.flame[2] != m) t3_release(m);
  }
  cw_node n = cw_node_new("Node3D", "CampLanterns");
  cw_node_add_child(root, n);
  cw_node3d_set_object(n, grp);
  t3_release(grp);
  cw_node3d_set_position(n, V3(px, py, pz));
}

/* snow vehicles that did not make it: half under the drifts */
static void build_wrecks(cw_node root, const track *t) {
  prop_set *ps = ps_new(root, "Wrecks");
  ps_style(ps, PK_WRECK_CAR, TEX_RUST, rgb(0.62f, 0.72f, 1.0f), 1.0f);
  ps_style(ps, PK_WRECK_TRUCK, TEX_RUST, rgb(0.62f, 0.72f, 1.0f), 1.0f);
  pmc *drift = pmc_new(80, 99);
  struct { float s, lat; int kind, variant; float scale, tilt; } W[] = {
    { 120, 26, 1, 0, 1.6f, 0.0f }, { 450, -30, 0, 3, 1.5f, 0.0f }, { 905, 24, 0, 2, 1.5f, 0.0f },
    { 1470, -28, 1, 1, 1.6f, 0.0f }, { 1695, 30, 0, 1, 1.5f, 0.0f }, { 2300, -34, 1, 0, 1.7f, 0.0f }, { 2860, 28, 0, 3, 1.5f, 0.0f },
  };
  uint32_t r = 4001;
  for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) {
    tsample a = track_at(t, W[i].s);
    /* the first flat enough spot out from the road on that side */
    t3_vec3 p = { 0 };
    bool found = false;
    for (float lat = fabsf(W[i].lat); lat < fabsf(W[i].lat) + 60.0f && !found; lat += 3.0f) {
      p = track_point(t, W[i].s, (W[i].lat > 0 ? 1.0f : -1.0f) * lat, 0);
      p.y = terrain(p.x, p.z, NULL);
      if (road_gap(t, p.x, p.z, NULL) > 9.0f && slope_at(p.x, p.z) < 0.17f && !in_lake(p.x, p.z, 1.05f) && !in_gorge(p.x, p.z, 20.0f)) found = true;
    }
    if (!found) continue;
    float yaw = atan2f(a.f.x, a.f.z) + 0.5f * rnds(&r) + (W[i].lat > 0 ? 0.8f : -0.8f);
    float sc = W[i].scale;
    t3_quat q = t3_quat_from_axis_angle(V3(0, 1, 0), yaw);
    p.y -= 0.45f * sc;
    ps_add_ex(ps, W[i].kind ? PK_WRECK_TRUCK : PK_WRECK_CAR, W[i].variant, 0, p, q, V3(sc, sc, sc), (pcol){ 1, 1, 1 }, (uint32_t)i + 1);
    /* the drift they sank into */
    pm *b = stand_at(drift, V3(p.x, p.y + 0.3f * sc, p.z), yaw);
    pm_ball(b, 3.2f * sc, 1.1f * sc, 2.4f * sc, 1, 0.16f, pc_mix(SNOW_C, ICE_LO, 0.3f), SNOW_C);
    pm_pop(b);
  }
  ps_finish(ps);
  pmc_finish(drift, root, "WreckDrifts", true, -1);
}

/* ── lantern posts along the road, the lake's details, the lookout ──────────────────────────────── */

static t3_material *flame_material(int k) {
  if (!AN.flame[k]) AN.flame[k] = glow_material();
  return AN.flame[k];
}

static void build_lamps(cw_node root, const track *t) {
  /* one post model, instanced: a wooden pole, an iron arm and a hanging lantern that flickers (its glow is on its own material) */
  pm *b = pm_new(17);
  pm_layer(b, PL_MAIN);
  pm_cyl_ex(b, 0.12f, 0.08f, 3.5f, 5, hex(0x3a2818), hex(0x8a6a48), false, true);
  pm_push(b);
  pm_move(b, 0, 3.15f, 0);
  pm_beam(b, V3(0, 0, 0), V3(0.7f, 0.12f, 0), 0.07f, hex(0x3a2818));
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0, 0);
  pm_ball(b, 0.7f, 0.35f, 0.7f, 1, 0.12f, pc_mix(SNOW_C, ICE_LO, 0.2f), SNOW_C);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0.7f, 2.05f, 0);
  lantern(b, 0.95f, LANT_C, 3.4f);
  pm_pop(b);
  prop_model m = pm_finish(b);
  prop_batch *bp[PL_COUNT] = { 0 };
  const int use[3] = { PL_MAIN, PL_METAL, PL_GLOW };
  for (int u = 0; u < 3; u++)
    if (m.g[use[u]]) {
      t3_material *mat = use[u] == PL_GLOW ? flame_material(2) : prop_material(use[u], -1, (pcol){ 1, 1, 1 }, 1.0f);
      bp[use[u]] = pb_begin(m.g[use[u]], mat);
      pb_chunk(bp[use[u]], 120.0f);
    }
  struct { float s0, s1, step; } R[] = { { 40, 150, 20 }, { 180, 1040, 28 }, { 2060, 3350, 36 } };
  int n = 0;
  for (unsigned k = 0; k < sizeof R / sizeof R[0]; k++)
    for (float s = R[k].s0; s < R[k].s1; s += R[k].step)
      for (int side = -1; side <= 1; side += 2) {
        tsample a = track_at(t, s);
        if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
        if (((int)(s / R[k].step) + (side > 0)) & 1 && k != 0) continue; /* alternate sides on the long stretches */
        t3_vec3 p = track_point(t, s, side * (a.hw + 2.1f + (a.walls ? 0.8f : 0.0f)), 0);
        p.y -= 0.6f;
        float yaw = atan2f(a.r.z * side, -a.r.x * side);
        for (int u = 0; u < 3; u++)
          if (bp[use[u]]) pb_add(bp[use[u]], p, yaw, V3(1, 1, 1));
        n++;
      }
  for (int u = 0; u < 3; u++)
    if (bp[use[u]]) pb_finish(bp[use[u]], root, use[u] == PL_GLOW ? "LampFlames" : "LampPosts", use[u] == PL_MAIN);
  cw_info("frost: %d lantern posts", n);
}

static void build_lake_details(cw_node root) {
  uint32_t r = 8101;
  float cx = F.lake_cx, cz = F.lake_cz, rx = F.lake_rx, rz = F.lake_rz, y = F.lake_y + 0.04f;
  /* long cracks: random walks over the ice, glowing faintly where the light comes up through them */
  pmc *cr = pmc_new(120, 71);
  for (int k = 0; k < 16; k++) {
    float a = rnd1(&r) * TAU_F, d = 0.2f + 0.6f * rnd1(&r);
    float x = cx + cosf(a) * rx * d, z = cz + sinf(a) * rz * d, dir = rnd1(&r) * TAU_F;
    int len = 20 + (int)(rnd1(&r) * 30.0f);
    for (int i = 0; i < len; i++) {
      float step = 3.0f + 3.0f * rnd1(&r);
      float nx = x + cosf(dir) * step, nz = z + sinf(dir) * step;
      if (!in_lake(nx, nz, 0.96f)) break;
      pm *b = pmc_at(cr, V3(x, y, z));
      pm_push(b);
      pm_layer(b, PL_GLOW);
      float w = 0.16f * (1.0f - (float)i / (float)len * 0.6f);
      t3_vec3 px = V3(-sinf(dir) * w, 0, cosf(dir) * w);
      pcol c = glow(hex(0x2fd0ff), 0.75f);
      pm_quad(b, V3(x - px.x, y, z - px.z), V3(x + px.x, y, z + px.z), V3(nx + px.x, y, nz + px.z), V3(nx - px.x, y, nz - px.z), c);
      pm_pop(b);
      /* a branch now and then */
      if (rnd1(&r) < 0.14f) {
        float bd = dir + (rnd1(&r) < 0.5f ? 0.7f : -0.7f);
        float bx = nx, bz = nz;
        for (int j = 0; j < 4; j++) {
          float bs = 2.5f + 2.0f * rnd1(&r), ex = bx + cosf(bd) * bs, ez = bz + sinf(bd) * bs;
          if (!in_lake(ex, ez, 0.96f)) break;
          pm *bb = pmc_at(cr, V3(bx, y, bz));
          pm_push(bb);
          pm_layer(bb, PL_GLOW);
          t3_vec3 q = V3(-sinf(bd) * 0.07f, 0, cosf(bd) * 0.07f);
          pm_quad(bb, V3(bx - q.x, y, bz - q.z), V3(bx + q.x, y, bz + q.z), V3(ex + q.x, y, ez + q.z), V3(ex - q.x, y, ez - q.z), glow(hex(0x2fd0ff), 0.6f));
          pm_pop(bb);
          bx = ex, bz = ez, bd += 0.35f * rnds(&r);
        }
      }
      x = nx, z = nz, dir += 0.3f * rnds(&r);
    }
  }
  pmc_finish(cr, root, "LakeCracks", false, -1);
  /* the ice-fisher's hut and its lantern, a sledge, a heap of drift, out on the ice */
  pmc *hut = pmc_new(60, 72);
  float hx = cx + rx * 0.45f, hz = cz + rz * 0.5f;
  pm *b = stand_at(hut, V3(hx, y - 0.05f, hz), 0.7f);
  pm_layer(b, PL_MAIN);
  pm_box(b, 3.2f, 2.0f, 2.6f, hex(0x6a3a22), hex(0xb8683a));
  pm_push(b);
  pm_move(b, 0, 2.0f, 0);
  pm_roty(b, CW_PI * 0.5f);
  pm_prism(b, 3.0f, 1.0f, 3.5f, hex(0x4a2a1a), hex(0x8a5a3a));
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 2.3f, 0);
  pm_roty(b, CW_PI * 0.5f);
  pm_prism(b, 3.1f, 0.8f, 3.6f, SNOW_C, SNOW_C);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0.4f, 0, 1.31f);
  pm_box_c(b, 0.8f, 1.5f, 0.08f, hex(0x2a1c14));
  pm_pop(b);
  pm_push(b);
  pm_move(b, 2.3f, 0, 0.3f);
  pm_ball(b, 1.1f, 0.5f, 1.4f, 1, 0.14f, SNOW_C, SNOW_C);
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, -0.7f, 1.2f, 1.31f);
  pm_box_c(b, 0.7f, 0.6f, 0.06f, glow(LANT_C, 2.6f));
  pm_pop(b);
  pm_pop(b);
  pmc_finish(hut, root, "FisherHut", true, -1);
  point_light(root, "HutLamp", V3(hx, y + 2.0f, hz), 1.0f, 0.66f, 0.3f, 3.0f, 18.0f);
  /* soft cold light welling up through the ice in a few places, and the lake's own wreck */
  t3_material *gm_mat = t3_mesh_basic_material_new(0xffffff);
  gm_mat->vertex_colors = true;
  gm_mat->transparent = true;
  gm_mat->blending = T3_ADDITIVE_BLENDING;
  gm_mat->depth_write = false;
  gm gl = { 0 };
  t3_vec3 up = V3(0, 1, 0);
  for (int k = 0; k < 7; k++) {
    float a = rnd1(&r) * TAU_F, d = 0.15f + 0.65f * rnd1(&r);
    float gx = cx + cosf(a) * rx * d, gz = cz + sinf(a) * rz * d, R = 18.0f + 22.0f * rnd1(&r);
    int centre = gm_vtx(&gl, V3(gx, y + 0.02f, gz), up, 0, 0, pc_mul(hex(0x1aa8e8), 0.5f));
    int prev = -1, first = -1;
    for (int i = 0; i < 20; i++) {
      float an = TAU_F * (float)i / 20.0f;
      int v = gm_vtx(&gl, V3(gx + cosf(an) * R, y + 0.02f, gz + sinf(an) * R), up, 0, 0, (pcol){ 0, 0, 0 });
      if (first < 0) first = v;
      if (prev >= 0) gm_tri(&gl, centre, prev, v);
      prev = v;
    }
    gm_tri(&gl, centre, prev, first);
  }
  gm_finish(&gl, root, "IceGlow", gm_mat, false, false);
  t3_release(gm_mat);
}

/* a lookout on the outside of the second hairpin: a deck on piles over the drop, a telescope, lanterns, a sign */
static void build_lookout(cw_node root, const track *t) {
  float s = 655.0f;
  tsample a = track_at(t, s);
  float lat = a.hw + 8.5f;
  pmc *c = pmc_new(60, 81);
  t3_vec3 p = track_point(t, s, lat, 0);
  float deck_y = a.p.y - 0.8f;
  p.y = deck_y;
  t3_vec3 f = vnorm3(V3(a.f.x, 0, a.f.z)), rr = vnorm3(V3(a.r.x, 0, a.r.z));
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_basis(b, p, rr, V3(0, 1, 0), f);
  pm_layer(b, PL_MAIN);
  pm_vary(b, 0.06f);
  float W = 9.0f, D = 6.0f;
  pm_push(b);
  pm_move(b, 0, -0.2f, 0);
  pm_box(b, W, 0.22f, D, hex(0x4a3524), hex(0xa07a50));
  pm_pop(b);
  /* piles down to the snow below */
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 2; j++) {
      float lx = -W * 0.42f + W * 0.42f * (float)i, lz = -D * 0.42f + D * 0.84f * (float)j;
      t3_vec3 w = vadd3(p, vadd3(vmul3(rr, lx), vmul3(f, lz)));
      float gy = terrain(w.x, w.z, NULL);
      pm_push(b);
      pm_move(b, lx, gy - deck_y - 0.4f, lz);
      pm_cyl_ex(b, 0.22f, 0.18f, deck_y - gy + 0.4f, 6, hex(0x3a2818), hex(0x7a5a38), false, false);
      pm_pop(b);
    }
  /* railings on three sides: posts and two ropes */
  pcol rope = hex(0xc8b080), wood = hex(0x5a4028);
  for (int side = 0; side < 3; side++) {
    int n = side == 1 ? 6 : 4;
    for (int i = 0; i <= n; i++) {
      float u = (float)i / (float)n;
      t3_vec3 q0, q1;
      if (side == 0) q0 = V3(-W * 0.5f, 0, -D * 0.5f + D * u), q1 = V3(-W * 0.5f, 0, -D * 0.5f + D * (u + 1.0f / (float)n));
      else if (side == 1) q0 = V3(-W * 0.5f + W * u, 0, D * 0.5f), q1 = V3(-W * 0.5f + W * (u + 1.0f / (float)n), 0, D * 0.5f);
      else q0 = V3(W * 0.5f, 0, -D * 0.5f + D * u), q1 = V3(W * 0.5f, 0, -D * 0.5f + D * (u + 1.0f / (float)n));
      if (side == 1) { /* the far side faces the view: leave a gap for the telescope */
        if (u > 0.4f && u < 0.6f && i < n) continue;
      }
      pm_beam(b, V3(q0.x, -0.1f, q0.z), V3(q0.x, 1.25f, q0.z), 0.12f, wood);
      if (i < n) {
        pm_rod(b, V3(q0.x, 1.1f, q0.z), V3(q1.x, 1.1f, q1.z), 0.04f, 4, rope);
        pm_rod(b, V3(q0.x, 0.6f, q0.z), V3(q1.x, 0.6f, q1.z), 0.04f, 4, rope);
      }
    }
  }
  /* a telescope on a tripod, aimed down the valley at the lake */
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, 0.4f, 0, D * 0.35f);
  for (int k = 0; k < 3; k++) {
    float an = TAU_F * (float)k / 3.0f;
    pm_beam(b, V3(0, 1.3f, 0), V3(cosf(an) * 0.5f, 0, sinf(an) * 0.5f), 0.04f, hex(0x6a6f7a));
  }
  pm_push(b);
  pm_move(b, 0, 1.35f, 0);
  pm_rotx(b, 0.5f);
  pm_cyl_ex(b, 0.1f, 0.14f, 1.0f, 8, hex(0x8a4a2a), hex(0xd0a050), true, true);
  pm_pop(b);
  pm_pop(b);
  /* a sign post: an arrow showing the way down to the lake, in the aurora's colours */
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, -W * 0.5f + 0.7f, 0, -D * 0.5f + 0.7f);
  pm_beam(b, V3(0, 0, 0), V3(0, 2.6f, 0), 0.1f, wood);
  pm_push(b);
  pm_move(b, 0, 2.2f, 0);
  pm_box_c(b, 1.7f, 0.5f, 0.06f, hex(0x2fa6b5));
  pm_pop(b);
  pm_pop(b);
  /* lanterns on the two front posts */
  pm_layer(b, PL_METAL);
  for (int k = -1; k <= 1; k += 2) {
    pm_push(b);
    pm_move(b, k * W * 0.5f, 1.25f, D * 0.5f);
    lantern(b, 0.8f, LANT_C, 3.2f);
    pm_pop(b);
  }
  pm_layer(b, PL_MAIN);
  pm_pop(b);
  pmc_finish(c, root, "Lookout", true, TEX_WALL_WOOD);
  point_light(root, "LookoutLamp", V3(p.x, deck_y + 2.0f, p.z), 1.0f, 0.62f, 0.28f, 3.0f, 16.0f);
}

/* ── frozen waterfalls poured down the mountainsides ───────────────────────────────────────────── */

/* a ribbon of ice that follows the steepest way down from (x0, z0): thick in the middle, glowing at its edges */
static void ice_fall(gm *g, pmc *glowc, const track *tk, float x0, float z0, float width, uint32_t seed) {
  uint32_t r = seed;
  float x = x0, z = z0;
  float px[40], pz[40], pw[40];
  int n = 0;
  for (int i = 0; i < 38; i++) {
    if (road_gap(tk, x, z, NULL) < 9.0f) break;
    px[n] = x, pz[n] = z, pw[n] = width * (0.55f + 0.45f * sinf(CW_PI * (float)i / 37.0f + 0.3f)) * (0.85f + 0.3f * rnd1(&r));
    n++;
    const float e = 2.5f;
    float gx = terrain(x + e, z, NULL) - terrain(x - e, z, NULL), gz = terrain(x, z + e, NULL) - terrain(x, z - e, NULL);
    float gl = sqrtf(gx * gx + gz * gz);
    if (gl < 0.05f * 2 * e && i > 6) break;
    x -= gx / (gl + 1e-4f) * 3.4f, z -= gz / (gl + 1e-4f) * 3.4f;
    x += 0.5f * rnds(&r), z += 0.5f * rnds(&r);
  }
  t3_vec3 up = V3(0, 1, 0);
  for (int i = 0; i + 1 < n; i++) {
    t3_vec3 d = V3(px[i + 1] - px[i], 0, pz[i + 1] - pz[i]);
    float dl = sqrtf(d.x * d.x + d.z * d.z) + 1e-4f;
    t3_vec3 side = V3(-d.z / dl, 0, d.x / dl);
    float w0 = pw[i] * 0.5f, w1 = pw[i + 1] * 0.5f;
    float lift = 0.35f;
    t3_vec3 a0 = V3(px[i] - side.x * w0, terrain(px[i] - side.x * w0, pz[i] - side.z * w0, NULL) + lift, pz[i] - side.z * w0);
    t3_vec3 a1 = V3(px[i] + side.x * w0, terrain(px[i] + side.x * w0, pz[i] + side.z * w0, NULL) + lift, pz[i] + side.z * w0);
    t3_vec3 b0 = V3(px[i + 1] - side.x * w1, terrain(px[i + 1] - side.x * w1, pz[i + 1] - side.z * w1, NULL) + lift, pz[i + 1] - side.z * w1);
    t3_vec3 b1 = V3(px[i + 1] + side.x * w1, terrain(px[i + 1] + side.x * w1, pz[i + 1] + side.z * w1, NULL) + lift, pz[i + 1] + side.z * w1);
    float f = 0.5f + 0.5f * hashf2(i, (int)seed, 3);
    pcol c = pc_mix(pc_mix(hex(0x3a86c4), hex(0xcfeeff), f), hex(0xf4fbff), (float)i / (float)n * 0.3f);
    gm_quad(g, a0, a1, b1, b0, up, c, 4.0f);
    /* a glowing seam down each edge */
    pm *b = pmc_at(glowc, a0);
    pm_push(b);
    pm_layer(b, PL_GLOW);
    pcol gc = glow(hex(0x34d8ff), 1.4f);
    pm_quad(b, V3(a0.x, a0.y + 0.05f, a0.z), V3(a0.x + side.x * 0.35f, a0.y + 0.05f, a0.z + side.z * 0.35f), V3(b0.x + side.x * 0.35f, b0.y + 0.05f, b0.z + side.z * 0.35f), V3(b0.x, b0.y + 0.05f, b0.z), gc);
    pm_quad(b, V3(a1.x - side.x * 0.35f, a1.y + 0.05f, a1.z - side.z * 0.35f), V3(a1.x, a1.y + 0.05f, a1.z), V3(b1.x, b1.y + 0.05f, b1.z), V3(b1.x - side.x * 0.35f, b1.y + 0.05f, b1.z - side.z * 0.35f), gc);
    pm_pop(b);
  }
}

static void build_falls(cw_node root, const track *t) {
  gm g = { 0 };
  pmc *gl = pmc_new(100, 83);
  uint32_t r = 6003;
  int made = 0;
  /* look for steep, tall faces within sight of the road, and pour a fall from the top of each */
  for (int tries = 0; tries < 3000 && made < 9; tries++) {
    float s = rnd1(&r) * t->length;
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_LOOP | TF_TUNNEL)) continue;
    float lside = rnd1(&r) < 0.5f ? 1.0f : -1.0f;
    float lat = lside * (a.hw + 30.0f + 70.0f * rnd1(&r));
    t3_vec3 p = track_point(t, s, lat, 0);
    if (in_lake(p.x, p.z, 1.2f) || in_gorge(p.x, p.z, 40.0f)) continue;
    float h = terrain(p.x, p.z, NULL);
    if (slope_at(p.x, p.z) < 0.6f || slope_at(p.x, p.z) > 1.6f) continue;
    /* it must drop a good way from here */
    float x = p.x, z = p.z, y0 = h;
    for (int i = 0; i < 12; i++) {
      const float e = 2.5f;
      float gx = terrain(x + e, z, NULL) - terrain(x - e, z, NULL), gz = terrain(x, z + e, NULL) - terrain(x, z - e, NULL);
      float gl2 = sqrtf(gx * gx + gz * gz) + 1e-4f;
      x -= gx / gl2 * 3.4f, z -= gz / gl2 * 3.4f;
    }
    if (y0 - terrain(x, z, NULL) < 18.0f) continue;
    bool near_fall = false;
    (void)near_fall;
    for (int k = 0; k < 3; k++) ice_fall(&g, gl, t, p.x + (float)(k - 1) * 4.5f, p.z, 8.0f + 3.5f * rnd1(&r), (uint32_t)(tries * 7 + k));
    if (getenv("FROST_DEBUG")) cw_info("frost: fall at s=%.0f (%.0f, %.0f, %.0f)", s, p.x, h, p.z);
    made++;
  }
  t3_material *m = phys((t3_color){ 1, 1, 1 }, 0.25f, 0.0f);
  m->vertex_colors = true;
  tex_apply(m, TEX_WALL_ICE, 1, 1, 1.0f, 1.0f);
  m->color = (t3_color){ 0.9f, 1.1f, 1.3f };
  m->emissive = (t3_color){ 0.04f, 0.3f, 0.7f };
  m->emissive_intensity = 1.1f;
  m->clearcoat = 0.8f;
  m->clearcoat_roughness = 0.08f;
  gm_finish(&g, root, "IceFalls", m, false, true);
  t3_release(m);
  pmc_finish(gl, root, "IceFallEdges", false, -1);
  cw_info("frost: %d frozen waterfalls", made);
}

/* ── weather: falling snow, blown spindrift, breath-like mist, steam ──────────────────────────── */

static void set_ramp(cw_node n, const char *ramp) {
  cw_value v;
  char buf[200];
  snprintf(buf, sizeof buf, "\"%s\"", ramp);
  if (cw_node_valid(n) && cw_value_parse(buf, CW_T_STRING, &v)) cw_node_set(n, "color_ramp", v);
}

static void build_weather(cw_node scene, cw_node root, const track *t) {
  (void)root;
  int before = cw_node_child_count(scene);
  fx_ambient_follow(scene, FXA_SNOW, 1.0f);
  if (THIN >= 1.0f) fx_ambient_follow(scene, FXA_DUST, 0.8f);
  /* the dust emitters are the ones just made: they carry the camera's box; recolour them cold and make them blow across the road */
  for (int i = before; i < cw_node_child_count(scene); i++) {
    cw_node n = cw_node_child(scene, i);
    const char *nm = cw_node_name(n);
    if (nm && strncmp(nm, "Dust", 4) == 0) {
      set_ramp(n, "0 0.75 0.86 1 0, 0.2 0.8 0.9 1 0.22, 1 0.8 0.9 1 0");
      cw_node_set(n, "direction", cw_val_vec3(1, 0.08f, 0.35f));
      cw_node_set(n, "speed_min", cw_val_float(3.0f));
      cw_node_set(n, "speed_max", cw_val_float(7.0f));
      cw_node_set(n, "size", cw_val_float(1.1f));
    }
  }
  /* a mist lying on the lake, and one boiling up out of the crevasse */
  cw_node m1 = fx_ambient_at(scene, FXA_MIST, V3(F.lake_cx, F.lake_y + 1.0f, F.lake_cz), V3(F.lake_rx * 0.7f, 1.0f, F.lake_rz * 0.7f), 1.3f, 420.0f);
  set_ramp(m1, "0 0.55 0.72 1 0, 0.25 0.6 0.78 1 0.13, 1 0.6 0.78 1 0");
  cw_node m2 = fx_ambient_at(scene, FXA_MIST, V3(F.lm.gmid.x, F.gorge_floor + 8.0f, F.lm.gmid.z), V3(14.0f, 6.0f, 60.0f), 1.0f, 200.0f);
  set_ramp(m2, "0 0.2 0.85 1 0, 0.3 0.25 0.85 1 0.3, 1 0.3 0.9 1 0");
  cw_node_set(m2, "direction", cw_val_vec3(0, 1, 0));
  cw_node_set(m2, "speed_min", cw_val_float(1.5f));
  cw_node_set(m2, "speed_max", cw_val_float(3.0f));
  /* snow blown off the high ridges in long white plumes */
  {
    uint32_t pr = 9091;
    int made = 0;
    for (int tries = 0; tries < 4000 && made < 7; tries++) {
      float x = F.lm.centre.x + (rnd1(&pr) - 0.5f) * 1800.0f, z = F.lm.centre.z + (rnd1(&pr) - 0.5f) * 2000.0f;
      float h = terrain(x, z, NULL);
      float gp = road_gap(t, x, z, NULL);
      if (h < 95.0f || gp > 380.0f || gp < 70.0f) continue;
      cw_node n = fx_ambient_at(scene, FXA_DUST, V3(x, h + 3.0f, z), V3(14.0f, 5.0f, 14.0f), 1.6f, 520.0f);
      set_ramp(n, "0 0.9 0.95 1 0, 0.15 0.92 0.96 1 0.35, 1 0.9 0.95 1 0");
      cw_node_set(n, "direction", cw_val_vec3(0.8f, 0.25f, 0.6f));
      cw_node_set(n, "speed_min", cw_val_float(10.0f));
      cw_node_set(n, "speed_max", cw_val_float(16.0f));
      cw_node_set(n, "size", cw_val_float(6.0f));
      cw_node_set(n, "lifetime", cw_val_float(5.0f));
      made++;
    }
  }
  /* breath of the mountain: steam from vents by the road, pale in the cold */
  static const float VS[] = { 560, 905, 1700, 2250 };
  for (unsigned i = 0; i < sizeof VS / sizeof VS[0]; i++) {
    tsample a = track_at(t, VS[i]);
    t3_vec3 p = track_point(t, VS[i], (i & 1 ? -1.0f : 1.0f) * (a.hw + 7.0f), 0);
    p.y = terrain(p.x, p.z, NULL) + 0.2f;
    cw_node n = fx_ambient_at(scene, FXA_STEAM, p, V3(0.8f, 0.1f, 0.8f), 0.8f, 160.0f);
    set_ramp(n, "0 0.8 0.92 1 0, 0.1 0.85 0.95 1 0.4, 1 0.9 0.97 1 0");
  }
}

/* a development aid for timing: FROST_SKIP=forest,crags,cave,... leaves those parts out */
static bool skip(const char *what) {
  const char *e = getenv("FROST_SKIP");
  return e && strstr(e, what);
}

/* ── the snowfield's surface: the wind-ripple texture laid unevenly ────────────────────────────────── */

/* The snow texture repeated every 10 m on a regular grid, and its ripples ran the same way over the whole pass: from
 * any height the field read as one stamp. The ground's texture coordinates are bent through a slow two-level warp
 * (the ripples curve, crowd together and spread apart, so their size changes from place to place) and its colours take
 * a broad field of their own: bluer and darker in hollows and shade, a lavender cast in drifts, brighter on the crests.
 * Only the ground's own attributes change: no draws, no new material. */
static void snow_warp(float x, float z, float *du, float *dv) {
  /* strong enough to turn the ripples by tens of degrees and halve or double their spacing, not enough to fold them */
  *du = ground_noise(x, z, 70.0f, 15.0f, 2, 401) + ground_noise(x, z, 23.0f, 3.5f, 1, 402);
  *dv = ground_noise(x, z, 70.0f, 15.0f, 2, 403) + ground_noise(x, z, 23.0f, 3.5f, 1, 404);
}
typedef struct { t3_geometry *g[2048]; int n; } geoset;
static void rework_geo(t3_geometry *g, float repeat) {
  t3_attribute *pa = g->attributes[T3_ATTR_POSITION], *ua = g->attributes[T3_ATTR_UV], *ca = g->attributes[T3_ATTR_COLOR];
  if (!pa || !ua || pa->type != T3_FLOAT32 || ua->type != T3_FLOAT32) return;
  const float *P = t3_attribute_f32(pa);
  float *U = t3_attribute_f32(ua);
  float *C = ca && ca->type == T3_FLOAT32 ? t3_attribute_f32(ca) : NULL;
  int cs = C ? ca->item_size : 0;
  for (int i = 0; i < pa->count; i++) {
    float x = P[i * 3], y = P[i * 3 + 1], z = P[i * 3 + 2], du, dv;
    snow_warp(x, z, &du, &dv);
    U[i * 2] += du / repeat, U[i * 2 + 1] += dv / repeat;
    if (C) {
      float n1 = 0.5f + 0.5f * ground_noise(x, z, 120.0f, 1.0f, 2, 405), n2 = 0.5f + 0.5f * ground_noise(x, z, 260.0f, 1.0f, 2, 406);
      float hollow = cw_clampf((rf_at(x, z) - y) * 0.08f, -1.0f, 1.0f); /* below the lie of the land: a hollow */
      float k = 0.84f + 0.32f * n1 - 0.1f * hollow;
      float *c = &C[i * cs];
      float r = c[0] * k, gg = c[1] * k, b = c[2] * k;
      /* lavender drifts and cyan shade, in broad patches */
      float lav = cw_smoothstep(0.55f, 0.85f, n2) * 0.22f, cy = cw_smoothstep(0.45f, 0.15f, n2) * 0.18f;
      c[0] = r * (1.0f + lav - cy * 0.6f), c[1] = gg * (1.0f - lav * 0.3f + cy * 0.15f), c[2] = b * (1.0f + lav * 0.4f + cy * 0.3f);
    }
  }
  ua->version++;
  if (C) ca->version++;
}
static void rework_obj(t3_object *o, geoset *gs, float repeat) {
  if (!o) return;
  if (o->type == T3_MESH) {
    t3_geometry *g = ((t3_mesh *)o)->geometry;
    bool seen = false;
    for (int i = 0; i < gs->n && !seen; i++) seen = gs->g[i] == g;
    if (!seen && gs->n < 2048) gs->g[gs->n++] = g, rework_geo(g, repeat);
    /* the ripples' relief, softer: at a low sun the full normal map striped every slope like corduroy */
    t3_mesh *me = (t3_mesh *)o;
    for (int i = 0; i < me->material_count; i++) me->materials[i]->normal_scale = (t3_vec2){ 0.5f, 0.5f };
  }
  for (int i = 0; i < o->child_count; i++) rework_obj(o->children[i], gs, repeat);
}
static void rework_node(cw_node n, geoset *gs, float repeat) {
  rework_obj(cw_node3d_object(n), gs, repeat);
  for (int i = 0; i < cw_node_child_count(n); i++) rework_node(cw_node_child(n, i), gs, repeat);
}

/* ── rock ledges and frozen seeps on the steep faces ────────────────────────────────────────────── */

/* The steep slopes were bare once the boulders that stood on them were sunk into the snow (set level on a slope, they
 * hung in the air). Rock shows on a real face as ledges: a band of strata along the contour with a sheer front, snow
 * on its tread and icicles under its lip. Each ledge here is a patch of the slope itself lifted into that shape: its
 * rim is the ground's own height (so it cannot float or hang), it rises over a cliff of rock at its front and runs
 * back into the hill; faces that look up wear snow, the rest is cold slate (or blue ice for a frozen seep). Built in
 * world space straight into one chunked builder: a few draws for all of them. */
static t3_vec3 vcross_(t3_vec3 a, t3_vec3 b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static void ledge(pm *b, float x, float z, float ux, float uz, float W, float D, float A, bool ice, uint32_t seed) {
  enum { NU = 9, NV = 7 };
  /* across the slope (cx) and down it (dx); v runs from the back (-D) to the foot (+D) */
  float cxx = -uz, czz = ux, dxx = -ux, dzz = -uz;
  static const float VS[NV] = { -1.0f, -0.55f, -0.1f, 0.22f, 0.3f, 0.65f, 1.0f };
  static const float HS[NV] = { 0.0f, 0.55f, 0.9f, 1.0f, 0.35f, 0.08f, 0.0f }; /* back slope, tread, the cliff, a scree foot */
  t3_vec3 P[NV][NU];
  for (int j = 0; j < NV; j++)
    for (int i = 0; i < NU; i++) {
      float u = -0.5f + (float)i / (float)(NU - 1), v = VS[j];
      float edge = 1.0f - cw_smoothstep(0.4f, 0.5f, fabsf(u) + 0.08f * (hashf2((int)seed, i * 7 + j, 31) - 0.5f));
      float h = A * HS[j] * edge * (0.8f + 0.4f * hashf2((int)seed, i, 32));
      float px = x + cxx * u * W + dxx * v * D, pz = z + czz * u * W + dzz * v * D;
      if (j > 0 && j < NV - 1 && i > 0 && i < NU - 1) px += 0.4f * (hashf2((int)seed, i, j * 3 + 33) - 0.5f), pz += 0.4f * (hashf2((int)seed, j, i * 3 + 34) - 0.5f);
      P[j][i] = V3(px, terrain(px, pz, NULL) + h - (edge < 0.05f ? 0.4f : 0.0f), pz);
    }
  pcol rockA = rgb(0.26f, 0.25f, 0.29f), rockB = rgb(0.46f, 0.44f, 0.5f) /* grey slate, lighter than the blue shade so a face reads as rock, not a hole */, snow = rgb(0.66f, 0.76f, 1.0f), icec = rgb(0.4f, 0.78f, 1.15f);
  pm_layer(b, PL_MAIN);
  for (int j = 0; j + 1 < NV; j++)
    for (int i = 0; i + 1 < NU; i++) {
      t3_vec3 a = P[j][i], bq = P[j][i + 1], c = P[j + 1][i + 1], d = P[j + 1][i];
      t3_vec3 n = vnorm3(vcross_(vsub3(c, a), vsub3(bq, d)));
      if (n.y < 0) n = vmul3(n, -1.0f);
      pcol col = n.y > 0.86f ? snow : pc_mix(rockA, rockB, hashf2((int)seed, i + j * 13, 35));
      if (ice && j >= 3 && j <= 4 && n.y <= 0.86f) col = icec;
      /* wound so the face looks up and out of the hill */
      t3_vec3 wn = vcross_(vsub3(bq, a), vsub3(d, a));
      if (wn.y >= 0) pm_quad(b, a, d, c, bq, col);
      else pm_quad(b, a, bq, c, d, col);
    }
  /* icicles under the lip of the cliff */
  for (int i = 1; i + 1 < NU; i++) {
    if (hashf2((int)seed, i, 36) < 0.4f) continue;
    t3_vec3 lip = P[3][i];
    float len = 0.5f + 1.6f * hashf2((int)seed, i, 37);
    pm_push(b);
    pm_move(b, lip.x + dxx * 0.35f * D * 0.1f, lip.y - 0.1f, lip.z + dzz * 0.35f * D * 0.1f);
    pm_rotx(b, 3.14159f);
    pm_cyl(b, 0.14f + 0.08f * hashf2((int)seed, i, 38), 0.0f, len, 4, icec, icec);
    pm_pop(b);
  }
}

static void build_ledges(cw_node root, const track *t) {
  pmc *c = pmc_new(700.0f, 9100u); /* very big cells: the ledges are cheap in triangles, each cell a draw per view and per shadow pass */
  uint32_t hs = 9101;
  int n = 0;
  for (float zz = -1100.0f; zz < 900.0f; zz += 24.0f)
    for (float xx = -560.0f; xx < 1240.0f; xx += 24.0f) {
      float x = xx + 20.0f * rnds(&hs), z = zz + 20.0f * rnds(&hs);
      float r1 = rnd1(&hs), r2 = rnd1(&hs), r3 = rnd1(&hs);
      if (in_lake(x, z, 1.08f) || in_gorge(x, z, 16.0f)) continue;
      const float e = 4.0f;
      float gx = (terrain(x + e, z, NULL) - terrain(x - e, z, NULL)) / (2 * e), gz = (terrain(x, z + e, NULL) - terrain(x, z - e, NULL)) / (2 * e);
      float g = sqrtf(gx * gx + gz * gz);
      /* the steep faces only, and mostly the steepest: rock shows where snow cannot lie */
      if (g < 0.3f || r1 > cw_smoothstep(0.3f, 0.75f, g) * 0.95f) continue;
      g = fminf(g, 1.1f);
      float W = 10.0f + 12.0f * r2, D = 3.0f + 2.0f * r3;
      if (road_gap(t, x, z, NULL) < W * 0.65f + D * 1.6f + 4.0f) continue;
      /* downhill is against the gradient; the ledge's +z looks down the slope */
      float yaw = atan2f(-gx, -gz);
      t3_vec3 p = V3(x, terrain(x, z, NULL), z);
      /* the slope must carry on uphill behind it and downhill in front of it: on a ridge's lip the land flattens behind
       * and the ledge stood proud of it, a black block against the sky */
      float gl = sqrtf(gx * gx + gz * gz), ux = gx / gl, uz = gz / gl, back = D * 2.2f;
      if (terrain(x + ux * back, z + uz * back, NULL) - p.y < 0.6f * g * back) continue;
      if (p.y - terrain(x - ux * D, z - uz * D, NULL) < 0.6f * g * D) continue;
      (void)yaw;
      pm *b = pmc_at(c, p);
      pm_push(b);
      ledge(b, x, z, ux, uz, W * 1.3f, D * 1.6f, 3.5f + 3.0f * r3 + 2.0f * g, r3 > 0.8f, (uint32_t)n * 7u + 3u);
      pm_pop(b);
      n++;
    }
  /* The banks graded between the climb's switchbacks are steep but hemmed in by road above and below, so the open
   * search above passes them by: walk the road and set ledges along its banks, lying with the contour (parallel to the
   * road), each one's whole footprint clear of every lane. */
  int nb = 0;
  for (float s = 0; s < t->length; s += 16.0f) {
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_TUNNEL | TF_LOOP)) continue;
    for (int side = -1; side <= 1; side += 2) {
      float r1 = rnd1(&hs), r2 = rnd1(&hs), r3 = rnd1(&hs);
      float D = (3.0f + 2.0f * r3) * 1.6f, W = (10.0f + 10.0f * r2) * 1.3f;
      t3_vec3 q = track_point(t, s, (float)side * (a.hw + 4.0f + D + 16.0f * r1), 0);
      float x = q.x, z = q.z;
      if (in_lake(x, z, 1.08f) || in_gorge(x, z, 16.0f)) continue;
      const float e = 4.0f;
      float gx = (terrain(x + e, z, NULL) - terrain(x - e, z, NULL)) / (2 * e), gz = (terrain(x, z + e, NULL) - terrain(x, z - e, NULL)) / (2 * e);
      float g = sqrtf(gx * gx + gz * gz);
      if (g < 0.32f || r1 > 0.85f) continue;
      float ux = gx / g, uz = gz / g;
      /* the footprint's corners and edge middles, all off the roads with room to spare */
      bool ok = true;
      for (int k = 0; k < 8 && ok; k++) {
        float cu = (k & 1 ? 0.5f : -0.5f) * (k < 4 ? W : 0.0f), cv = (k & 2 ? 1.0f : -1.0f) * D;
        ok = road_gap(t, x - uz * cu - ux * cv, z + ux * cu - uz * cv, NULL) > 2.5f;
      }
      if (!ok) continue;
      pm *b = pmc_at(c, V3(x, 0, z));
      pm_push(b);
      ledge(b, x, z, ux, uz, W, D, 3.0f + 2.5f * r3 + 2.0f * fminf(g, 1.1f), r3 > 0.85f, 50000u + (uint32_t)nb * 7u);
      pm_pop(b);
      nb++;
    }
  }
  cw_info("frost: %d ledges along the road's banks", nb);
  pmc_finish(c, root, "Ledges", true, -1);
  cw_info("frost: %d rock ledges on the steep faces", n);
}

static void frost_scenery(cw_node scene, const track *t, uint32_t seed) {
  THIN = game_planned_views() > 1 ? 0.5f : 1.0f;
  (void)seed;
  memset(&F, 0, sizeof F);
  memset(&SKY, 0, sizeof SKY);
  free(FL.base);
  memset(&FL, 0, sizeof FL);
  memset(&AN, 0, sizeof AN);
  BEACON = NULL;
  F.t = t;
  find_landmarks(t);
  F.gorge_floor = F.lm.base_y - 38.0f;
  F.lake_cx = 345.0f, F.lake_cz = -235.0f, F.lake_rx = 215.0f, F.lake_rz = 300.0f, F.lake_y = 0.9f;
  build_rf(t);
  /* the shelves: the station on the roof of the cave, a camp by the start, one on the plateau */
  {
    const landmarks *L = &F.lm;
    int mid = L->tun0 >= 0 ? (L->tun0 + L->tun1) / 2 : 0;
    float xs[3] = { t->s[mid].p.x, 0, 0 }, zs[3] = { t->s[mid].p.z, 0, 0 };
    t3_vec3 c1 = track_point(t, 70.0f, 50.0f, 0), c2 = track_point(t, 2420.0f, 46.0f, 0);
    xs[1] = c1.x, zs[1] = c1.z, xs[2] = c2.x, zs[2] = c2.z;
    float r0[3] = { 24, 16, 16 }, r1[3] = { 50, 34, 34 };
    for (int i = 0; i < 3; i++) {
      F.pad[i].x = xs[i], F.pad[i].z = zs[i], F.pad[i].r0 = r0[i], F.pad[i].r1 = r1[i];
      F.pad[i].y = terrain(xs[i], zs[i], NULL);
      F.npads = i + 1;
    }
  }
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
      cw_info("frost: s=%d road y=%.1f terrain=%.1f far=%.1f gap=%.1f", k, a->p.y, terrain(a->p.x, a->p.z, NULL), far_field(a->p.x, a->p.z, 0), road_gap(t, a->p.x, a->p.z, NULL));
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
  F.g.tint = rgb(0.3f, 0.42f, 0.78f);
  F.g.tint2 = rgb(0.16f, 0.22f, 0.42f);
  F.g.patch_scale = 46;
  F.g.near_cell = 48;
  F.g.detail_range = 130;
  F.g.follow_road = false;
  F.g.darken_edge = 0.12f;
  F.g.lumpy = 0.22f;
  F.g.rough = 0.8f;
  {
    cw_node gn = ground_plane(root, t, &F.g);
    static geoset gs;
    gs.n = 0;
    if (!skip("warp")) rework_node(gn, &gs, F.g.tex_repeat);
  }

  /* the sky */
  build_aurora(root);
  {
    static const int N[3] = { 1200, 1000, 900 };
    if (THIN < 1.0f) {} 
    for (int k = 0; k < 3; k++) {
      t3_material *m = t3_points_material_new(0xffffff);
      m->vertex_colors = true;
      m->size = 2.8f;
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
  if (!skip("peaks")) {
  build_peaks(root);
  }
  restyle_world(scene);
  init_colours();
  build_start(root, t);
  if (!skip("dress")) {
  build_dressing(root, t);
  }
  if (!skip("forest")) {
  build_forest(root, t);
  }
  if (!skip("crags")) {
  build_outcrops(root, t);
  }
  if (!skip("ledges")) build_ledges(root, t);
  if (!skip("lake")) {
  build_lake(root);
  }
  if (!skip("crev")) {
  build_crevasse(root, t);
  }
  if (F.lm.tun0 >= 0) {
    build_portal(root, t, F.lm.tun0, 1, 301);
    build_portal(root, t, F.lm.tun1, -1, 311);
  }
  if (!skip("summit")) {
  build_summit(root, t);
  }
  if (!skip("camps")) {
  build_camp(root, 1, 501);
    build_camp(root, 2, 601);
  }
  if (!skip("falls")) build_falls(root, t);
  if (!skip("lamps")) {
  build_lamps(root, t);
  build_lake_details(root);
  build_lookout(root, t);
  }
  if (!skip("wrecks")) {
  build_wrecks(root, t);
  }
  if (!skip("weather")) {
  build_weather(scene, root, t);
  }
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
      cw_node3d_set_position(cam, V3(dcam[0], fmaxf(dcam[1], terrain(dcam[0], dcam[2], NULL) + 3.0f), dcam[2]));
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
  flags_frame(t);
  /* the lanterns and the fire flicker out of step, the mast's red light blinks */
  for (int k = 0; k < 3; k++)
    if (AN.flame[k]) {
      float f = 0.88f + 0.12f * sinf(t * (7.0f + 2.3f * (float)k) + (float)k) + 0.08f * sinf(t * (17.0f + 3.1f * (float)k));
      if (k == 1) f = 0.8f + 0.25f * sinf(t * 9.0f) + 0.15f * sinf(t * 23.0f + 1.0f);
      AN.flame[k]->color = (t3_color){ f, f, f };
    }
  if (ANEMO) {
    ANEMO->rotation.y = t * 3.4f;
    ANEMO->quaternion = t3_quat_from_axis_angle(V3(0, 1, 0), t * 3.4f);
  }
  if (AN.beacon) {
    float ph = fmodf(t, 1.6f);
    float on = ph < 0.18f ? 1.0f : ph < 0.3f ? 0.0f : ph < 0.42f ? 1.0f : 0.0f;
    float k = 0.04f + on;
    AN.beacon->color = (t3_color){ 5.0f * k, 0.06f * k, 0.04f * k };
  }
  for (int k = 0; k < AN.nlights; k++) {
    float e = AN.light_e[k] * (0.9f + 0.1f * sinf(t * (5.0f + (float)k * 1.3f) + (float)k * 2.0f));
    if (cw_node_valid(AN.lights[k])) cw_node_set(AN.lights[k], "energy", cw_val_float(e));
  }
  if (AN.crev) {
    float p = 0.8f + 0.25f * sinf(t * 1.3f) + 0.1f * sinf(t * 3.1f);
    AN.crev->color = (t3_color){ p, p, p };
  }
  for (int k = 0; k < 3; k++)
    if (SKY.star_mat[k]) SKY.star_mat[k]->opacity = 0.62f + 0.38f * sinf(t * (0.9f + 0.55f * (float)k) + (float)k * 2.1f);
}

const map_def map_frost = {
  .name = "Frostbite Pass",
  .blurb = "Ice, ice caves and the aurora: slippery and steep.",
  .scene = "scenes/frost.cwscene",
  .spec = MAP_SPEC,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.62f, 0.72f, 0.95f }, .kerb_a = { 0.98f, 0.45f, 0.12f }, .kerb_b = { 0.95f, 0.97f, 1.0f }, .wall = { 0.6f, 0.82f, 1.0f },
            .wall_top = { 0.92f, 0.96f, 1.0f }, .road_tex = TEX_ROAD_DIRT, .wall_tex = TEX_WALL_ICE, .road_rough = 0.75f },
  .laps = 3,
  .par_lap = 60,
  .scenery = frost_scenery,
  .scenery_frame = frost_frame,
};
