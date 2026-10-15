/* derby's cars: four original, beat-up bangers in the shapes of 1970s-80s
 * full-size cars (a sedan, a wagon, a pickup and a van), modelled to real
 * proportions (a 5.3 m sedan on a 2.9 m wheelbase, 1.95 m wide, 0.65 m
 * wheels) rather than stacked from boxes.
 *
 *   body      a loft of cross sections along the car with crisp feature
 *             lines (split normals): the rocker pinch, a character line,
 *             a hard shoulder at the waist; wheel arches cut with a lip and
 *             lined inside; flat fascias with a recessed grille, rectangular
 *             sealed-beam lamps in chrome bezels, tail lamp clusters; door
 *             and lid gaps, handles, a fuel flap
 *   glass     the greenhouse built from planes: a bowed windscreen
 *             (cracked), side and rear glass set back inside pillars with
 *             thickness and chrome trim; the driver's window is out
 *   paint     three materials split by face: clearcoated paint with
 *             scratches and swirls, flat rough primer panels, and rust (a
 *             bubbled normal map) at the arch lips, sills and door bottoms;
 *             vertex colours carry the fade, the odd-colour panel and the mud
 *   detail    chrome bumpers on brackets with rubber strips, a blank plate,
 *             an exhaust, mirrors; tyres 0.2 m wide with bulging sidewalls,
 *             steel wheels with lug nuts (a hubcap on a few); a cabin with a
 *             seat, a dash, a wheel and a roll cage
 *   damage    every body piece is a deformable mesh: dents crease it (a
 *             sharp centre, a buckle ring, radiating creases), the paint
 *             tears to bare steel and rust round them; the bonnet pops off
 *             its catch, the bumpers hang from one bracket and drag
 *   LOD       0 full (~20k triangles), 1 light (~3k: no cabin, no small
 *             parts), 2 a silhouette (~600)
 *
 * Shapes and wear are hashed from the car's number: every host builds the
 * same car. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/jobs.h"
#include "cartwheel/math.h"
#include "world.h"

/* ── per-kind shape (metres; z forward, x to the left, y up) ────────── */

typedef struct keyf { float z, v; } keyf;
typedef struct shape {
  keyf belt[10];       /* the waist / bonnet / deck height along the car */
  int nbelt;
  float sill;          /* the rocker's underside */
  float corner;        /* the plan's corner radius */
  float cowl, roof_f, roof_b, deck; /* windscreen foot, roof front, roof back, rear glass foot */
  float roof;          /* roof height */
  float tumble;        /* how far the greenhouse leans in at the roof */
  float bpillar;       /* the B pillar (z) */
  float hood_front;    /* where the bonnet lid ends (short of the grille) */
  float trunk;         /* the deck lid's front edge */
  int kind;
} shape;

static const shape SHAPES[CAR_KINDS] = {
  [CAR_SEDAN] = { { { -2.65f, 0.86f }, { -2.5f, 0.93f }, { -1.7f, 0.95f }, { -0.9f, 0.95f }, { 0.4f, 0.96f }, { 1.2f, 0.93f }, { 2.3f, 0.89f }, { 2.65f, 0.83f } }, 8,
                  0.27f, 0.16f, 0.62f, -0.12f, -1.08f, -1.55f, 1.36f, 0.16f, -0.38f, 2.45f, -1.6f, CAR_SEDAN },
  [CAR_WAGON] = { { { -2.72f, 0.88f }, { -2.6f, 0.94f }, { -1.7f, 0.96f }, { -0.9f, 0.96f }, { 0.4f, 0.97f }, { 1.2f, 0.94f }, { 2.36f, 0.9f }, { 2.72f, 0.84f } }, 8,
                  0.27f, 0.15f, 0.64f, -0.06f, -2.58f, -2.62f, 1.43f, 0.12f, -0.42f, 2.5f, -2.6f, CAR_WAGON },
  [CAR_PICKUP] = { { { -2.75f, 0.98f }, { -2.65f, 1.04f }, { -0.5f, 1.06f }, { 0.3f, 1.08f }, { 1.3f, 1.06f }, { 2.4f, 1.02f }, { 2.75f, 0.94f } }, 7,
                   0.36f, 0.14f, 0.88f, 0.38f, -0.42f, -0.48f, 1.78f, 0.1f, 0.0f, 2.55f, -0.6f, CAR_PICKUP },
  /* the van: one tall body behind the cab (its top is the box's roof), the cab's greenhouse in front */
  [CAR_VAN] = { { { -2.45f, 1.88f }, { -2.4f, 1.95f }, { 0.72f, 1.96f }, { 0.8f, 1.1f }, { 1.7f, 1.08f }, { 2.15f, 1.0f }, { 2.45f, 0.9f } }, 7,
                0.32f, 0.12f, 1.72f, 1.2f, 0.8f, 0.8f, 1.96f, 0.05f, 0.86f, 2.3f, -2.5f, CAR_VAN },
};

static float keyed(const keyf *k, int n, float z) {
  if (z <= k[0].z) return k[0].v;
  for (int i = 1; i < n; i++)
    if (z <= k[i].z) {
      float t = (z - k[i - 1].z) / (k[i].z - k[i - 1].z);
      t = t * t * (3 - 2 * t);
      return k[i - 1].v + (k[i].v - k[i - 1].v) * t;
    }
  return k[n - 1].v;
}

/* ── hashing (the car's own little noise) ───────────────────────────── */

static uint32_t hh(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(uint32_t a, uint32_t b) { return (float)(hh(a, b) & 0xffff) / 65535.0f; }
static float vn(float x, float y, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - x0, fy = y - y0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = h01((uint32_t)(x0 * 92821 + y0 * 7) ^ seed, 1), b = h01((uint32_t)((x0 + 1) * 92821 + y0 * 7) ^ seed, 1);
  float c = h01((uint32_t)(x0 * 92821 + (y0 + 1) * 7) ^ seed, 1), d = h01((uint32_t)((x0 + 1) * 92821 + (y0 + 1) * 7) ^ seed, 1);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

/* ── the car's wear: what each piece of skin is ─────────────────────── */

typedef struct wear {
  float paint[3], primer[3], alt[3];
  int panel_a, panel_b; /* primer and odd-colour panels */
  float two[3];         /* the upper body's colour on a two-tone car */
  bool two_tone;
  uint32_t seed;
} wear;
enum { MAT_PAINT, MAT_PRIMER, MAT_RUST, MATS };

/* which panel a point is on: 0 bonnet, 1 left front wing, 2 right doors, 3
 * boot / tailgate, 4 left doors, 5 right front wing, -1 none */
static int panel_of(const shape *sh, float x, float y, float z) {
  float belt = keyed(sh->belt, sh->nbelt, z);
  if (y > belt - 0.02f && z > sh->cowl + 0.04f) return 0;
  if (y > belt - 0.02f && z < sh->trunk) return 3;
  if (y > belt - 0.02f) return -1;
  if (z > sh->cowl - 0.05f) return x > 0 ? 1 : 5;
  if (z > sh->deck) return x > 0 ? 4 : 2;
  return -1;
}

/* the material of a face at (x, y, z): rust where water sits, primer on
 * the patched panel, paint elsewhere */
static int mat_of(const wear *W, const car_spec *k, const shape *sh, float x, float y, float z) {
  float n = vn(x * 3.1f + z * 1.7f, y * 3.3f, W->seed + 7);
  float arch = 9;
  float ax[2] = { k->af, k->ar };
  for (int a = 0; a < 2; a++) {
    float dz = z - ax[a], dy = y - k->wr;
    arch = fminf(arch, fabsf(sqrtf(dz * dz + dy * dy) - (k->wr + 0.06f)));
  }
  /* the rocker panel, the arch lips, and a patch or two low on a door */
  float low = sh->sill + 0.05f + 0.06f * n;
  bool rust = y < low || (arch < 0.035f + 0.03f * n && y < 0.8f) || (y < sh->sill + 0.2f && n > 0.8f);
  if (rust) return MAT_RUST;
  int p = panel_of(sh, x, y, z);
  if (p >= 0 && p == W->panel_a) return MAT_PRIMER;
  return MAT_PAINT;
}

/* the colour at a point: paint (faded on top), the odd-colour panel, rust
 * tones, mud up the sides */
static void tint_of(const wear *W, const shape *sh, int mat, float x, float y, float z, float ny, float out[3]) {
  float c[3];
  if (mat == MAT_PRIMER) memcpy(c, W->primer, sizeof c);
  else if (mat == MAT_RUST) {
    float t = vn(x * 8 + z * 5, y * 8, W->seed + 13);
    t3_color a = lin(0.42f, 0.22f, 0.1f), b = lin(0.25f, 0.14f, 0.08f);
    c[0] = a.r + (b.r - a.r) * t, c[1] = a.g + (b.g - a.g) * t, c[2] = a.b + (b.b - a.b) * t;
  } else {
    memcpy(c, panel_of(sh, x, y, z) == W->panel_b ? W->alt : W->paint, sizeof c);
    /* two-tone: the roof and upper body in a second colour, split at a line below the belt */
    if (W->two_tone && y > keyed(sh->belt, sh->nbelt, z) - 0.2f) memcpy(c, W->two, sizeof c);
    /* oxidised: chalky where the sun hits, finely mottled */
    if (ny > 0.55f) {
      float o = (ny - 0.55f) * 1.4f * (0.65f + 0.35f * vn(x * 14, z * 14, W->seed + 3));
      for (int i = 0; i < 3; i++) c[i] = c[i] * (1 - 0.25f * o) + 0.1f * o;
    }
  }
  /* rust bleeding out of the shut lines, worst low down */
  if (mat == MAT_PAINT || mat == MAT_PRIMER) {
    float belt = keyed(sh->belt, sh->nbelt, z), edge = 9;
    float seams[4] = { sh->cowl - 0.03f, sh->bpillar, sh->kind == CAR_VAN ? sh->bpillar - 0.06f : sh->deck + 0.02f, sh->kind == CAR_VAN ? -0.62f : 99 };
    for (int i = 0; i < 4; i++) edge = fminf(edge, fabsf(z - seams[i]));
    float low = cw_clampf((belt - y) * 1.4f, 0, 1), reach = 0.03f + 0.09f * low * vn(y * 14, z * 3, W->seed + 9);
    float r = y < belt - 0.03f ? cw_clampf(1 - edge / reach, 0, 1) * (0.5f + 0.5f * low) : 0;
    t3_color rc = lin(0.36f, 0.17f, 0.08f);
    for (int i = 0; i < 3; i++) c[i] += ((&rc.r)[i] - c[i]) * r * 0.85f;
  }
  /* mud: spatter up the lower sides, thinning with height */
  float mud = cw_clampf((0.72f - y) * 2.4f, 0, 1) * cw_clampf(vn(x * 31 + z * 27, y * 36, W->seed + 21) * 2.4f - 0.95f, 0, 1);
  t3_color mc = lin(0.24f, 0.18f, 0.12f);
  for (int i = 0; i < 3; i++) out[i] = c[i] + ((&mc.r)[i] - c[i]) * mud * 0.9f;
}

/* ── mesh pieces: one builder per material, each a deformable shell ─── */

typedef struct pieces { mesh_build mb[MATS]; } pieces;

/* a quad of skin: its material from its middle, its corners tinted */
static void skin_quad(pieces *P, const wear *W, const car_spec *k, const shape *sh, const t3_vec3 q[4], const float uv[4][2], bool dark) {
  float cx = (q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f, cy = (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f, cz = (q[0].z + q[1].z + q[2].z + q[3].z) * 0.25f;
  int mat = mat_of(W, k, sh, cx, cy, cz);
  mesh_build *b = &P->mb[mat];
  t3_vec3 e1 = { q[2].x - q[0].x, q[2].y - q[0].y, q[2].z - q[0].z }, e2 = { q[3].x - q[1].x, q[3].y - q[1].y, q[3].z - q[1].z };
  t3_vec3 n = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
  float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
  if (l < 1e-9f) return;
  n.x /= l, n.y /= l, n.z /= l;
  int v[4];
  for (int i = 0; i < 4; i++) {
    v[i] = mb_vert(b, q[i], n, uv[i][0], uv[i][1]);
    float c[3];
    tint_of(W, sh, mat, q[i].x, q[i].y, q[i].z, n.y, c);
    if (dark) c[0] = c[1] = c[2] = 0.025f;
    mb_color(b, v[i], c[0], c[1], c[2]);
  }
  mb_quad(b, v[0], v[1], v[2], v[3]);
}

/* ── the lower body loft ────────────────────────────────────────────── */

#define SEC 11 /* points on one half of a section, sill to the middle of the top */
typedef struct sec { float x[SEC], y[SEC]; } sec;

/* the cross section at z (left half; the right mirrors it). Points: 0 rocker
 * underside, 1 rocker outer, 2 the pinch, 3-5 the door skin up to the
 * character line, 6 above it, 7 the shoulder (crease), 8-10 across the top */
static void section(const car_spec *k, const shape *sh, float z, sec *s) {
  float hl = k->hl, hw = k->hw;
  float w = hw, rc = sh->corner, ez = fabsf(z) - (hl - rc);
  if (ez > 0) w = hw - rc + sqrtf(fmaxf(0, rc * rc - ez * ez));
  float belt = keyed(sh->belt, sh->nbelt, z), sill = sh->sill;
  float ch = sill + (belt - sill) * 0.66f;
  /* arches: the skin's lower edge follows the arch, with a lip flared out */
  float arch_y = -1, flare = 0;
  float ax[2] = { k->af, k->ar };
  for (int a = 0; a < 2; a++) {
    float dz = z - ax[a], R = k->wr + 0.06f;
    if (fabsf(dz) < R) arch_y = fmaxf(arch_y, k->wr + sqrtf(R * R - dz * dz));
    float fz = 1 - fabsf(dz) / (R + 0.12f);
    if (fz > 0) flare = fmaxf(flare, 0.022f * fz);
  }
  static const float PX[SEC] = { -0.06f, -0.012f, 0, 0.008f, 0.014f, 0.02f, 0.012f, -0.012f, -0.075f, 0, 0 };
  float ys[SEC] = { sill, sill + 0.035f, sill + 0.13f, sill + 0.24f, ch - 0.12f, ch, ch + 0.08f, belt, belt + 0.012f, belt + 0.022f, belt + 0.026f };
  for (int i = 0; i < SEC; i++) {
    float x = i < 9 ? w + PX[i] + (i >= 2 && i <= 6 ? flare : 0) : (i == 9 ? w * 0.5f : 0);
    float y = ys[i];
    if (arch_y > 0 && i <= 4) {
      float lift = arch_y + 0.012f * i;
      if (y < lift) y = fminf(lift, ch - 0.02f - 0.01f * (4 - i));
    }
    s->x[i] = x, s->y[i] = y;
  }
}

/* stations along z: denser at the ends, round the arches and either side of each seam */
static int stations(const car_spec *k, const shape *sh, int lod, float *zs) {
  int n = 0;
  int base = lod == 0 ? 64 : lod == 1 ? 18 : 6;
  for (int i = 0; i <= base; i++) {
    float t = (float)i / base, e = t * 2 - 1;
    zs[n++] = k->hl * (e * (1.12f - 0.12f * e * e));
  }
  float ax[2] = { k->af, k->ar };
  if (lod == 0) {
    float seams[7] = { sh->cowl - 0.03f, sh->bpillar, sh->deck + 0.02f, sh->cowl + 0.04f, sh->trunk, sh->bpillar - 0.06f, -0.62f };
    for (int s = 0; s < (sh->kind == CAR_VAN ? 7 : 5); s++)
      for (int d = -1; d <= 1; d += 2) zs[n++] = seams[s] + d * 0.005f;
    for (int a = 0; a < 2; a++)
      for (int d = -6; d <= 6; d++) zs[n++] = ax[a] + d * (k->wr + 0.06f) / 6.5f;
  } else if (lod == 1)
    for (int a = 0; a < 2; a++)
      for (int d = -2; d <= 2; d++) zs[n++] = ax[a] + d * (k->wr + 0.06f) / 2.5f;
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && zs[j] < zs[j - 1]; j--) {
      float t = zs[j];
      zs[j] = zs[j - 1], zs[j - 1] = t;
    }
  int m = 0;
  for (int i = 0; i < n; i++)
    if (!m || zs[i] - zs[m - 1] > 0.002f) zs[m++] = zs[i];
  return m;
}

/* is a point on the skin on a seam line (a dark gap)? */
static bool seam_at(const shape *sh, float y, float z, float belt, float sill) {
  bool side = y < belt - 0.03f && y > sill + 0.1f, top = y > belt - 0.005f;
  const float g = 0.0062f;
  /* the van: front door, the sliding door behind it, and the rear doors' shut line */
  if (side && sh->kind == CAR_VAN && (fabsf(z - (sh->cowl - 0.03f)) < g || fabsf(z - sh->bpillar) < g || fabsf(z - (sh->bpillar - 0.06f)) < g || fabsf(z + 0.62f) < g)) return true;
  if (side && sh->kind != CAR_VAN && (fabsf(z - (sh->cowl - 0.03f)) < g || fabsf(z - sh->bpillar) < g || fabsf(z - (sh->deck + 0.02f)) < g)) return true;
  if (top && (fabsf(z - (sh->cowl + 0.04f)) < g || (sh->kind == CAR_SEDAN && fabsf(z - sh->trunk) < g))) return true;
  return false;
}

static void flip(t3_vec3 q[4]) {
  t3_vec3 t = q[1];
  q[1] = q[3], q[3] = t;
}

static void loft(pieces *P, const wear *W, const car_spec *k, const shape *sh, int lod) {
  float zs[200];
  int ns = stations(k, sh, lod, zs);
  static sec S[200];
  for (int i = 0; i < ns; i++) section(k, sh, zs[i], &S[i]);
  bool pickup = sh->kind == CAR_PICKUP;
  for (int i = 0; i + 1 < ns; i++) {
    float z0 = zs[i], z1 = zs[i + 1];
    for (int side = -1; side <= 1; side += 2)
      for (int j = 0; j + 1 < SEC; j++) {
        int j1 = j + 1;
        if (lod == 2) {
          if (!(j == 0 || j == 2 || j == 6 || j == 8 || j == 9)) continue;
          j1 = j == 0 ? 2 : j == 2 ? 6 : j == 6 ? 8 : j == 8 ? 9 : 10;
        }
        if (pickup && j >= 8 && z1 <= sh->deck && z0 > -k->hl + 0.1f) continue; /* the open bed */
        /* u runs the section's arc length in metres, so tall sides take the paint unstretched */
        float ua = 0, ub;
        for (int t = 0; t < j; t++) ua += hypotf(S[i].x[t + 1] - S[i].x[t], S[i].y[t + 1] - S[i].y[t]);
        ub = ua;
        for (int t = j; t < j1; t++) ub += hypotf(S[i].x[t + 1] - S[i].x[t], S[i].y[t + 1] - S[i].y[t]);
        /* tall faces (the van's sides) split into rows, so the per-vertex wear doesn't streak */
        int rows = lod == 0 ? (int)ceilf((ub - ua) / 0.16f) : 1;
        if (rows < 1) rows = 1;
        for (int r = 0; r < rows; r++) {
          float ta = (float)r / rows, tb = (float)(r + 1) / rows;
          t3_vec3 a0 = { side * (S[i].x[j] + (S[i].x[j1] - S[i].x[j]) * ta), S[i].y[j] + (S[i].y[j1] - S[i].y[j]) * ta, z0 };
          t3_vec3 a1 = { side * (S[i + 1].x[j] + (S[i + 1].x[j1] - S[i + 1].x[j]) * ta), S[i + 1].y[j] + (S[i + 1].y[j1] - S[i + 1].y[j]) * ta, z1 };
          t3_vec3 b1 = { side * (S[i + 1].x[j] + (S[i + 1].x[j1] - S[i + 1].x[j]) * tb), S[i + 1].y[j] + (S[i + 1].y[j1] - S[i + 1].y[j]) * tb, z1 };
          t3_vec3 b0 = { side * (S[i].x[j] + (S[i].x[j1] - S[i].x[j]) * tb), S[i].y[j] + (S[i].y[j1] - S[i].y[j]) * tb, z0 };
          t3_vec3 q[4] = { a0, a1, b1, b0 };
          if (side > 0) flip(q);
          float u0 = ua + (ub - ua) * ta, u1 = ua + (ub - ua) * tb;
          float uv[4][2] = { { u0, z0 }, { u0, z1 }, { u1, z1 }, { u1, z0 } };
          float mz = (z0 + z1) * 0.5f, my = (a0.y + b0.y) * 0.5f;
          bool dark = lod == 0 && seam_at(sh, my, mz, keyed(sh->belt, sh->nbelt, mz), sh->sill);
          skin_quad(P, W, k, sh, q, uv, dark);
        }
      }
  }
  /* the pickup's bed: floor, inner walls, rail caps, the cab's back */
  if (pickup) {
    float zb = -k->hl + 0.1f, zf = sh->deck, fy = 0.66f;
    float w = k->hw - 0.09f, top = keyed(sh->belt, sh->nbelt, -1.0f);
    float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    t3_vec3 fl[4] = { { -w, fy, zb }, { -w, fy, zf }, { w, fy, zf }, { w, fy, zb } };
    skin_quad(P, W, k, sh, fl, uv, true);
    for (int s = -1; s <= 1; s += 2) {
      t3_vec3 wl[4] = { { s * w, fy, zb }, { s * w, top, zb }, { s * w, top, zf }, { s * w, fy, zf } };
      if (s > 0) flip(wl);
      skin_quad(P, W, k, sh, wl, uv, true);
      t3_vec3 cap[4] = { { s * (k->hw - 0.01f), top + 0.01f, zb }, { s * (k->hw - 0.01f), top + 0.01f, zf }, { s * w, top + 0.01f, zf }, { s * w, top + 0.01f, zb } };
      if (s < 0) flip(cap);
      skin_quad(P, W, k, sh, cap, uv, false);
    }
    for (int e = 0; e < 2; e++) {
      float z = e ? zf : zb;
      t3_vec3 wl[4] = { { -w, fy, z }, { w, fy, z }, { w, top, z }, { -w, top, z } };
      if (e) flip(wl);
      skin_quad(P, W, k, sh, wl, uv, e == 0);
    }
  }
  /* the fascias: each end section filled flat to its middle */
  for (int e = 0; e < 2; e++) {
    const sec *s = &S[e ? ns - 1 : 0];
    float z = e ? zs[ns - 1] : zs[0];
    for (int side = -1; side <= 1; side += 2)
      for (int j = 0; j + 1 < SEC; j++) {
        if (lod == 2 && (j % 2)) continue;
        int j1 = lod == 2 ? CW_MIN(j + 2, SEC - 1) : j + 1;
        float mid_a = j >= 7 ? s->y[j] : s->y[2], mid_b = j1 >= 7 ? s->y[j1] : s->y[2];
        t3_vec3 q[4] = { { side * s->x[j], s->y[j], z }, { side * s->x[j1], s->y[j1], z }, { 0, fmaxf(mid_b, s->y[j1]), z }, { 0, fmaxf(mid_a, s->y[j]), z } };
        if ((side > 0) == (e == 1)) flip(q);
        float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        skin_quad(P, W, k, sh, q, uv, false);
      }
  }
}

/* ── the greenhouse: planes, pillars, trim ──────────────────────────── */

typedef struct gh { float wb, wr, yb_f, yb_b, yr; } gh;
static gh gh_of(const car_spec *k, const shape *sh) {
  gh g;
  g.wb = k->hw - 0.1f;
  g.wr = g.wb - sh->tumble;
  g.yb_f = keyed(sh->belt, sh->nbelt, sh->cowl) + 0.03f;
  g.yb_b = keyed(sh->belt, sh->nbelt, sh->deck) + 0.03f;
  g.yr = sh->roof;
  return g;
}

/* a bar of square-ish section from a to b (pillars, rails, cage tubes) */
static void bar(mesh_build *b, t3_vec3 a, t3_vec3 c, float wx, float wy, const float rgb[3], int R) {
  t3_vec3 d = { c.x - a.x, c.y - a.y, c.z - a.z };
  float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
  if (l < 1e-4f) return;
  d.x /= l, d.y /= l, d.z /= l;
  t3_vec3 up = fabsf(d.y) > 0.9f ? (t3_vec3){ 1, 0, 0 } : (t3_vec3){ 0, 1, 0 };
  t3_vec3 s1 = { d.y * up.z - d.z * up.y, d.z * up.x - d.x * up.z, d.x * up.y - d.y * up.x };
  float sl = sqrtf(s1.x * s1.x + s1.y * s1.y + s1.z * s1.z);
  s1.x /= sl, s1.y /= sl, s1.z /= sl;
  t3_vec3 s2 = { d.y * s1.z - d.z * s1.y, d.z * s1.x - d.x * s1.z, d.x * s1.y - d.y * s1.x };
  int base = b->nv;
  for (int e = 0; e < 2; e++) {
    t3_vec3 p = e ? c : a;
    for (int i = 0; i < R; i++) {
      float ang = CW_TAU * (i + 0.5f) / R, ca = cw_cos(ang), sa = cw_sin(ang);
      float px = ca * wx * (R == 4 ? 1.41f : 0.8f + 0.2f * fabsf(ca)), py = sa * wy * (R == 4 ? 1.41f : 0.8f + 0.2f * fabsf(sa));
      t3_vec3 n = { s1.x * ca + s2.x * sa, s1.y * ca + s2.y * sa, s1.z * ca + s2.z * sa };
      int v = mb_vert(b, (t3_vec3){ p.x + s1.x * px + s2.x * py, p.y + s1.y * px + s2.y * py, p.z + s1.z * px + s2.z * py }, n, (float)i / R, e * l);
      mb_color(b, v, rgb[0], rgb[1], rgb[2]);
    }
  }
  for (int i = 0; i < R; i++) mb_quad(b, base + i, base + (i + 1) % R, base + R + (i + 1) % R, base + R + i);
}

/* a painted bar (pillars, rails), tinted like the skin it joins */
static void paint_bar(pieces *P, const wear *W, const car_spec *k, const shape *sh, t3_vec3 a, t3_vec3 c, float wx, float wy, int R) {
  float mx = (a.x + c.x) * 0.5f, my = (a.y + c.y) * 0.5f, mz = (a.z + c.z) * 0.5f;
  int mat = mat_of(W, k, sh, mx, my, mz);
  if (mat == MAT_RUST) mat = MAT_PAINT;
  float col[3];
  tint_of(W, sh, mat, mx, my, mz, 0.5f, col);
  bar(&P->mb[mat], a, c, wx, wy, col, R);
}

/* a flat panel between four corners, subdivided (it dents); bowed up by `bow` across u */
static void panel(pieces *P, const wear *W, const car_spec *k, const shape *sh, t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, int nu, int nv, float bow) {
  for (int j = 0; j < nv; j++)
    for (int i = 0; i < nu; i++) {
      t3_vec3 q[4];
      float us[4] = { (float)i / nu, (float)(i + 1) / nu, (float)(i + 1) / nu, (float)i / nu }, vs[4] = { (float)j / nv, (float)j / nv, (float)(j + 1) / nv, (float)(j + 1) / nv };
      float uv[4][2];
      for (int t = 0; t < 4; t++) {
        float u = us[t], v = vs[t];
        t3_vec3 p = { a.x + (b.x - a.x) * u + (d.x - a.x) * v + (a.x - b.x + c.x - d.x) * u * v, a.y + (b.y - a.y) * u + (d.y - a.y) * v + (a.y - b.y + c.y - d.y) * u * v,
                      a.z + (b.z - a.z) * u + (d.z - a.z) * v + (a.z - b.z + c.z - d.z) * u * v };
        p.y += bow * 4 * u * (1 - u);
        q[t] = p;
        uv[t][0] = p.x, uv[t][1] = p.z + p.y;
      }
      skin_quad(P, W, k, sh, q, uv, false);
    }
}

static void glass_quad(mesh_build *g, t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d) {
  t3_vec3 e1 = { c.x - a.x, c.y - a.y, c.z - a.z }, e2 = { d.x - b.x, d.y - b.y, d.z - b.z };
  t3_vec3 n = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
  int i0 = mb_vert(g, a, n, 0, 0), i1 = mb_vert(g, b, n, 1, 0), i2 = mb_vert(g, c, n, 1, 1), i3 = mb_vert(g, d, n, 0, 1);
  for (int q = 0; q < 4; q++) mb_color(g, i0 + q, 1, 1, 1);
  mb_quad(g, i0, i1, i2, i3);
}

static void greenhouse(pieces *P, mesh_build *glass, mesh_build *trim, const wear *W, const car_spec *k, const shape *sh, int lod) {
  gh g = gh_of(k, sh);
  int nu = lod == 0 ? 8 : 2, nv = lod == 0 ? 6 : 1;
  /* the roof: a crowned panel (and its headliner underneath, dark) */
  panel(P, W, k, sh, (t3_vec3){ g.wr, g.yr, sh->roof_b }, (t3_vec3){ -g.wr, g.yr, sh->roof_b }, (t3_vec3){ -g.wr, g.yr, sh->roof_f }, (t3_vec3){ g.wr, g.yr, sh->roof_f },
        nu, nv, 0.035f);
  {
    float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    t3_vec3 q[4] = { { g.wr, g.yr - 0.02f, sh->roof_b }, { g.wr, g.yr - 0.02f, sh->roof_f }, { -g.wr, g.yr - 0.02f, sh->roof_f }, { -g.wr, g.yr - 0.02f, sh->roof_b } };
    skin_quad(P, W, k, sh, q, uv, true);
  }
  if (lod == 2) {
    for (int s = -1; s <= 1; s += 2) {
      t3_vec3 a = { s * g.wb, g.yb_f, sh->cowl }, b = { s * g.wb, g.yb_b, sh->deck }, c = { s * g.wr, g.yr, sh->roof_b }, d = { s * g.wr, g.yr, sh->roof_f };
      if (s > 0) glass_quad(glass, a, d, c, b);
      else glass_quad(glass, a, b, c, d);
    }
    glass_quad(glass, (t3_vec3){ -g.wb, g.yb_f, sh->cowl }, (t3_vec3){ g.wb, g.yb_f, sh->cowl }, (t3_vec3){ g.wr, g.yr, sh->roof_f }, (t3_vec3){ -g.wr, g.yr, sh->roof_f });
    glass_quad(glass, (t3_vec3){ g.wb, g.yb_b, sh->deck }, (t3_vec3){ -g.wb, g.yb_b, sh->deck }, (t3_vec3){ -g.wr, g.yr, sh->roof_b }, (t3_vec3){ g.wr, g.yr, sh->roof_b });
    return;
  }
  const int R = lod == 0 ? 6 : 4;
  for (int s = -1; s <= 1; s += 2) {
    paint_bar(P, W, k, sh, (t3_vec3){ s * g.wb, g.yb_f, sh->cowl }, (t3_vec3){ s * g.wr, g.yr - 0.02f, sh->roof_f }, 0.045f, 0.05f, R);
    float bz = sh->bpillar, by = keyed(sh->belt, sh->nbelt, bz) + 0.03f;
    paint_bar(P, W, k, sh, (t3_vec3){ s * g.wb, by, bz }, (t3_vec3){ s * g.wr, g.yr - 0.02f, bz + 0.02f }, 0.05f, 0.04f, R);
    if (sh->kind == CAR_SEDAN) {
      /* the C pillar: a broad sail from the roof's back corner to the deck */
      t3_vec3 a = { s * g.wb, g.yb_b, sh->deck + 0.02f }, b = { s * g.wb, g.yb_b, sh->roof_b + 0.22f }, c = { s * g.wr, g.yr - 0.01f, sh->roof_b + 0.12f }, d = { s * g.wr, g.yr - 0.01f, sh->roof_b - 0.02f };
      if (s > 0) panel(P, W, k, sh, a, b, c, d, 3, 3, 0);
      else panel(P, W, k, sh, b, a, d, c, 3, 3, 0);
    } else if (sh->kind != CAR_VAN)
      paint_bar(P, W, k, sh, (t3_vec3){ s * g.wb, g.yb_b, sh->deck }, (t3_vec3){ s * g.wr, g.yr - 0.02f, sh->roof_b }, 0.06f, 0.05f, R);
    paint_bar(P, W, k, sh, (t3_vec3){ s * g.wr, g.yr - 0.025f, sh->roof_f }, (t3_vec3){ s * g.wr, g.yr - 0.025f, sh->roof_b }, 0.032f, 0.03f, R);
    if (sh->kind == CAR_WAGON) {
      float qz = (sh->bpillar + sh->roof_b) * 0.5f - 0.2f, qy = keyed(sh->belt, sh->nbelt, qz) + 0.03f;
      paint_bar(P, W, k, sh, (t3_vec3){ s * g.wb, qy, qz }, (t3_vec3){ s * g.wr, g.yr - 0.02f, qz }, 0.04f, 0.035f, R);
    }
  }
  /* glass, set back 2 cm inside the pillars: side windows (not the driver's) and the rear glass */
  const float in = 0.022f;
  static const float TR[3] = { 0.62f, 0.6f, 0.57f };
  for (int s = -1; s <= 1; s += 2) {
    float zs[4] = { sh->cowl - 0.04f, sh->bpillar + 0.05f, sh->bpillar - 0.05f, sh->kind == CAR_SEDAN ? sh->roof_b + 0.24f : sh->deck + 0.05f };
    for (int w = 0; w < 2; w++) {
      if (sh->kind == CAR_VAN && w == 1) continue;
      if (s > 0 && w == 0) continue; /* the driver's front window is out */
      float z0 = zs[w * 2], z1 = zs[w * 2 + 1];
      float y0a = keyed(sh->belt, sh->nbelt, z0) + 0.03f, y0b = keyed(sh->belt, sh->nbelt, z1) + 0.03f;
      float zt0 = w == 0 ? sh->roof_f - 0.02f : z0, zt1 = w == 1 && sh->kind == CAR_SEDAN ? sh->roof_b + 0.1f : z1;
      t3_vec3 a = { s * (g.wb - in), y0a, z0 }, b = { s * (g.wb - in), y0b, z1 }, c = { s * (g.wr - in), g.yr - 0.05f, zt1 }, d = { s * (g.wr - in), g.yr - 0.05f, zt0 };
      if (s > 0) glass_quad(glass, a, d, c, b);
      else glass_quad(glass, a, b, c, d);
      if (lod == 0) bar(trim, (t3_vec3){ s * (g.wb + 0.004f), y0a - 0.008f, z0 }, (t3_vec3){ s * (g.wb + 0.004f), y0b - 0.008f, z1 }, 0.01f, 0.01f, TR, 4);
    }
  }
  if (sh->kind == CAR_VAN) {
    /* the back doors' windows, a pair */
    for (int s2 = -1; s2 <= 1; s2 += 2) {
      float x0 = s2 * 0.06f, x1 = s2 * (k->hw - 0.2f), z = -k->hl - 0.012f;
      if (s2 > 0) glass_quad(glass, (t3_vec3){ x0, 1.3f, z }, (t3_vec3){ x0, 1.75f, z }, (t3_vec3){ x1, 1.75f, z }, (t3_vec3){ x1, 1.3f, z });
      else glass_quad(glass, (t3_vec3){ x0, 1.3f, z }, (t3_vec3){ x1, 1.3f, z }, (t3_vec3){ x1, 1.75f, z }, (t3_vec3){ x0, 1.75f, z });
    }
  } else {
    float zg = sh->kind == CAR_PICKUP ? sh->roof_b - in : sh->deck;
    glass_quad(glass, (t3_vec3){ g.wb - 0.05f, g.yb_b, zg }, (t3_vec3){ -(g.wb - 0.05f), g.yb_b, zg }, (t3_vec3){ -(g.wr - 0.03f), g.yr - 0.04f, sh->roof_b + in },
               (t3_vec3){ g.wr - 0.03f, g.yr - 0.04f, sh->roof_b + in });
  }
}

/* ── wheels: tyre and steel wheel from lathes ───────────────────────── */

static void lathe_x(mesh_build *b, const t3_vec2 *pts, int n, int seg, float uscale, float rgb) {
  t3_geometry *g = t3_lathe_geometry_new(pts, n, seg, 0, CW_TAU);
  t3_attribute *P = g->attributes[T3_ATTR_POSITION], *N = g->attributes[T3_ATTR_NORMAL], *U = g->attributes[T3_ATTR_UV];
  const float *p = P->array, *nn = N->array, *uv = U ? U->array : NULL;
  int base = b->nv;
  for (int i = 0; i < P->count; i++) {
    int v = mb_vert(b, (t3_vec3){ p[i * 3 + 1], -p[i * 3], p[i * 3 + 2] }, (t3_vec3){ nn[i * 3 + 1], -nn[i * 3], nn[i * 3 + 2] }, uv ? uv[i * 2] * uscale : 0,
                    uv ? uv[i * 2 + 1] : 0);
    mb_color(b, v, rgb, rgb, rgb);
  }
  const t3_attribute *I = g->index;
  for (int i = 0; I && i + 2 < I->count; i += 3) {
    uint32_t a, c, d;
    if (I->type == T3_UINT16) {
      const uint16_t *x = I->array;
      a = x[i], c = x[i + 1], d = x[i + 2];
    } else {
      const uint32_t *x = I->array;
      a = x[i], c = x[i + 1], d = x[i + 2];
    }
    mb_tri(b, base + (int)a, base + (int)c, base + (int)d);
  }
  t3_release(g);
}

static t3_geometry *tyre_geo(float r, int lod) {
  /* the section, outside in: bead, sidewall bulging widest at mid height,
   * the shoulder, a slightly crowned tread, the other side */
  const float w = 0.112f, rim = r * 0.6f;
  t3_vec2 HI[] = { { rim, -w * 0.82f }, { rim + 0.02f, -w * 0.92f }, { r * 0.78f, -w * 1.1f }, { r * 0.86f, -w * 1.09f }, { r * 0.93f, -w * 0.96f }, { r - 0.012f, -w * 0.86f },
                   { r, -w * 0.7f }, { r + 0.003f, -w * 0.3f }, { r + 0.004f, 0 }, { r + 0.003f, w * 0.3f }, { r, w * 0.7f }, { r - 0.012f, w * 0.86f }, { r * 0.93f, w * 0.96f },
                   { r * 0.86f, w * 1.09f }, { r * 0.78f, w * 1.1f }, { rim + 0.02f, w * 0.92f }, { rim, w * 0.82f } };
  t3_vec2 LO[] = { { rim, -w * 0.9f }, { r * 0.9f, -w * 1.02f }, { r, -w * 0.6f }, { r, w * 0.6f }, { r * 0.9f, w * 1.02f }, { rim, w * 0.9f } };
  mesh_build b = { 0 };
  if (lod == 0) lathe_x(&b, HI, (int)(sizeof HI / sizeof HI[0]), 48, 6, 1);
  else lathe_x(&b, LO, (int)(sizeof LO / sizeof LO[0]), lod == 1 ? 16 : 8, 6, 1);
  return mb_finish(&b);
}

static void cyl_into(mesh_build *b, t3_vec3 c, float r, float len, int seg, float shade) {
  t3_vec2 pts[] = { { 0.0001f, -len * 0.5f }, { r, -len * 0.5f }, { r, len * 0.5f }, { 0.0001f, len * 0.5f } };
  mesh_build t = { 0 };
  lathe_x(&t, pts, 4, seg, 1, shade);
  int base = b->nv;
  for (int i = 0; i < t.nv; i++) {
    const float *p = t.pos + i * 3, *n = t.nor + i * 3;
    int v = mb_vert(b, (t3_vec3){ c.x + p[0], c.y + p[1], c.z + p[2] }, (t3_vec3){ n[0], n[1], n[2] }, 0, 0);
    mb_color(b, v, shade, shade, shade);
  }
  for (int i = 0; i < t.ni; i += 3) mb_tri(b, base + (int)t.idx[i], base + (int)t.idx[i + 1], base + (int)t.idx[i + 2]);
  free(t.pos), free(t.nor), free(t.uv), free(t.col), free(t.idx);
}

static t3_geometry *rim_geo(float r, int lod, bool hubcap) {
  const float rr = r * 0.6f;
  t3_vec2 HI[] = { { 0.0005f, 0.045f }, { 0.04f, 0.045f }, { 0.055f, 0.03f }, { rr * 0.55f, 0.02f }, { rr * 0.62f, 0.035f }, { rr * 0.78f, 0.03f }, { rr * 0.9f, 0.055f },
                   { rr - 0.006f, 0.07f }, { rr + 0.004f, 0.07f }, { rr + 0.004f, 0.05f }, { rr, -0.09f } };
  t3_vec2 LO[] = { { 0.0005f, 0.045f }, { rr * 0.7f, 0.025f }, { rr, 0.07f }, { rr, -0.09f } };
  t3_vec2 CAP[] = { { 0.0005f, 0.09f }, { rr * 0.45f, 0.085f }, { rr * 0.8f, 0.075f }, { rr * 0.95f, 0.06f } };
  mesh_build b = { 0 };
  if (lod == 0) lathe_x(&b, HI, (int)(sizeof HI / sizeof HI[0]), 36, 1, 1);
  else lathe_x(&b, LO, (int)(sizeof LO / sizeof LO[0]), lod == 1 ? 12 : 6, 1, 1);
  if (lod == 0 && hubcap) lathe_x(&b, CAP, (int)(sizeof CAP / sizeof CAP[0]), 36, 1, 1);
  else if (lod == 0) {
    cyl_into(&b, (t3_vec3){ 0.05f, 0, 0 }, 0.035f, 0.03f, 16, 0.6f); /* the hub */
    for (int i = 0; i < 5; i++) {
      float a = CW_TAU * i / 5;
      cyl_into(&b, (t3_vec3){ 0.058f, 0.055f * cw_cos(a), 0.055f * cw_sin(a) }, 0.011f, 0.025f, 6, 0.75f); /* lug nuts */
    }
  }
  return mb_finish(&b);
}

/* ── small parts (chrome, rubber, lamps) ────────────────────────────── */

static void box_into(mesh_build *b, t3_vec3 c, t3_vec3 size, float r, const float rgb[3]) {
  t3_geometry *g = geo_rounded_box(size.x, size.y, size.z, fminf(r, fminf(size.x, fminf(size.y, size.z)) * 0.45f), r > 0.004f ? 2 : 1);
  t3_attribute *P = g->attributes[T3_ATTR_POSITION], *N = g->attributes[T3_ATTR_NORMAL], *U = g->attributes[T3_ATTR_UV];
  int base = b->nv;
  for (int i = 0; i < P->count; i++) {
    const float *p = (const float *)P->array + i * 3, *n = (const float *)N->array + i * 3, *u = (const float *)U->array + i * 2;
    int v = mb_vert(b, (t3_vec3){ c.x + p[0], c.y + p[1], c.z + p[2] }, (t3_vec3){ n[0], n[1], n[2] }, u[0], u[1]);
    mb_color(b, v, rgb[0], rgb[1], rgb[2]);
  }
  const t3_attribute *I = g->index;
  for (int t = 0; I && t + 2 < I->count; t += 3) {
    const uint16_t *x16 = I->array;
    const uint32_t *x32 = I->array;
    uint32_t A = I->type == T3_UINT16 ? x16[t] : x32[t], B = I->type == T3_UINT16 ? x16[t + 1] : x32[t + 1], C = I->type == T3_UINT16 ? x16[t + 2] : x32[t + 2];
    mb_tri(b, base + (int)A, base + (int)B, base + (int)C);
  }
  t3_release(g);
}

static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}
static t3_mesh *mesh_in(t3_object *parent, t3_geometry *g, t3_material *m, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = true;
  t3_object_add(parent, me);
  t3_release(me);
  t3_release(g);
  return me;
}

/* the cabin: a bucket seat, the dash, the wheel, the roll cage (one mesh) */
static t3_geometry *cabin_geo(const car_spec *k, const shape *sh) {
  mesh_build b = { 0 };
  static const float SEAT[3] = { 0.06f, 0.055f, 0.05f }, DASH[3] = { 0.035f, 0.035f, 0.035f }, CAGE[3] = { 0.28f, 0.27f, 0.25f };
  gh g = gh_of(k, sh);
  float fl = sh->sill + 0.12f, front = sh->cowl - 0.04f, mid = sh->bpillar;
  box_into(&b, (t3_vec3){ 0.4f, fl + 0.22f, mid + 0.25f }, (t3_vec3){ 0.52f, 0.12f, 0.52f }, 0.04f, SEAT);
  box_into(&b, (t3_vec3){ 0.4f, fl + 0.58f, mid + 0.02f }, (t3_vec3){ 0.52f, 0.7f, 0.12f }, 0.04f, SEAT);
  box_into(&b, (t3_vec3){ 0, g.yb_f - 0.06f, front - 0.2f }, (t3_vec3){ k->hw * 1.75f, 0.2f, 0.34f }, 0.04f, DASH);
  {
    t3_geometry *t = t3_torus_geometry_new(0.19f, 0.018f, 8, 24, CW_TAU);
    t3_attribute *P = t->attributes[T3_ATTR_POSITION], *N = t->attributes[T3_ATTR_NORMAL];
    int base = b.nv;
    float ca = cw_cos(0.5f), sa = cw_sin(0.5f);
    for (int i = 0; i < P->count; i++) {
      const float *p = (const float *)P->array + i * 3, *n = (const float *)N->array + i * 3;
      int v = mb_vert(&b, (t3_vec3){ 0.4f + p[0], g.yb_f + 0.08f + p[1] * ca, front - 0.5f - p[1] * sa + p[2] }, (t3_vec3){ n[0], n[1] * ca, -n[1] * sa + n[2] }, 0, 0);
      mb_color(&b, v, DASH[0], DASH[1], DASH[2]);
    }
    const t3_attribute *I = t->index;
    for (int q = 0; I && q + 2 < I->count; q += 3) {
      const uint16_t *x16 = I->array;
      const uint32_t *x32 = I->array;
      uint32_t A = I->type == T3_UINT16 ? x16[q] : x32[q], B = I->type == T3_UINT16 ? x16[q + 1] : x32[q + 1], C = I->type == T3_UINT16 ? x16[q + 2] : x32[q + 2];
      mb_tri(&b, base + (int)A, base + (int)B, base + (int)C);
    }
    t3_release(t);
  }
  /* the driver: overalls, a scuffed white helmet and arms out to the wheel, a shape through the glass */
  {
    static const float SUIT[3] = { 0.1f, 0.09f, 0.075f }, LID[3] = { 0.62f, 0.6f, 0.55f }, VISOR[3] = { 0.02f, 0.02f, 0.022f };
    float sz = mid + 0.16f, sy = fminf(fl + 0.3f, g.yr - 1.1f);
    box_into(&b, (t3_vec3){ 0.4f, sy + 0.3f, sz }, (t3_vec3){ 0.4f, 0.56f, 0.24f }, 0.09f, SUIT);
    box_into(&b, (t3_vec3){ 0.4f, sy + 0.66f, sz + 0.02f }, (t3_vec3){ 0.11f, 0.12f, 0.11f }, 0.04f, SUIT);
    box_into(&b, (t3_vec3){ 0.4f, sy + 0.84f, sz + 0.03f }, (t3_vec3){ 0.27f, 0.29f, 0.3f }, 0.12f, LID);
    box_into(&b, (t3_vec3){ 0.4f, sy + 0.84f, sz + 0.17f }, (t3_vec3){ 0.21f, 0.09f, 0.04f }, 0.02f, VISOR);
    for (int s = -1; s <= 1; s += 2)
      bar(&b, (t3_vec3){ 0.4f + s * 0.19f, sy + 0.5f, sz + 0.02f }, (t3_vec3){ 0.4f + s * 0.16f, g.yb_f + 0.1f, front - 0.5f }, 0.045f, 0.04f, SUIT, 6);
  }
  float w0 = g.wb - 0.1f, w1 = g.wr - 0.08f, top = g.yr - 0.08f, hoop = mid - 0.3f;
  bar(&b, (t3_vec3){ -w0, fl, hoop }, (t3_vec3){ -w1, top, hoop }, 0.024f, 0.024f, CAGE, 8);
  bar(&b, (t3_vec3){ w0, fl, hoop }, (t3_vec3){ w1, top, hoop }, 0.024f, 0.024f, CAGE, 8);
  bar(&b, (t3_vec3){ -w1, top, hoop }, (t3_vec3){ w1, top, hoop }, 0.024f, 0.024f, CAGE, 8);
  for (int s = -1; s <= 1; s += 2) {
    bar(&b, (t3_vec3){ s * w1, top, hoop }, (t3_vec3){ s * w1, top, sh->roof_f - 0.05f }, 0.022f, 0.022f, CAGE, 8);
    bar(&b, (t3_vec3){ s * w1, top, sh->roof_f - 0.05f }, (t3_vec3){ s * w0, g.yb_f - 0.05f, front }, 0.022f, 0.022f, CAGE, 8);
    bar(&b, (t3_vec3){ s * (w0 + 0.02f), fl + 0.1f, front - 0.1f }, (t3_vec3){ s * (w0 + 0.02f), g.yb_f - 0.08f, hoop }, 0.02f, 0.02f, CAGE, 8);
    bar(&b, (t3_vec3){ s * (w0 + 0.02f), g.yb_f - 0.08f, front - 0.1f }, (t3_vec3){ s * (w0 + 0.02f), fl + 0.1f, hoop }, 0.02f, 0.02f, CAGE, 8);
  }
  box_into(&b, (t3_vec3){ 0, fl - 0.02f, (front + sh->deck) * 0.5f }, (t3_vec3){ k->hw * 1.85f, 0.03f, front - sh->deck }, 0.005f, DASH);
  return mb_finish(&b);
}

/* the arch liners (dark half drums inside each opening) and the underbody */
static t3_geometry *liners_geo(const car_spec *k) {
  mesh_build b = { 0 };
  float ax[2] = { k->af, k->ar };
  const int R = 18;
  for (int a = 0; a < 2; a++)
    for (int s = -1; s <= 1; s += 2) {
      float x0 = s * (k->hw - 0.03f), x1 = s * (k->hw - 0.36f), rad = k->wr + 0.05f;
      int base = b.nv;
      for (int i = 0; i <= R; i++) {
        float t = 3.1415927f * i / R, cz = cw_cos(t), cy = cw_sin(t);
        t3_vec3 n = { 0, -cy, -cz };
        int v0 = mb_vert(&b, (t3_vec3){ x0, k->wr + cy * rad, ax[a] + cz * rad }, n, 0, 0), v1 = mb_vert(&b, (t3_vec3){ x1, k->wr + cy * rad, ax[a] + cz * rad }, n, 1, 0);
        mb_color(&b, v0, 0.03f, 0.028f, 0.025f);
        mb_color(&b, v1, 0.02f, 0.02f, 0.02f);
      }
      for (int i = 0; i < R; i++) {
        int a0 = base + i * 2;
        if (s > 0) mb_quad(&b, a0, a0 + 2, a0 + 3, a0 + 1);
        else mb_quad(&b, a0, a0 + 1, a0 + 3, a0 + 2);
      }
    }
  float y = 0.24f;
  int base = b.nv;
  t3_vec3 dn = { 0, -1, 0 };
  int v0 = mb_vert(&b, (t3_vec3){ -k->hw + 0.06f, y, -k->hl + 0.15f }, dn, 0, 0), v1 = mb_vert(&b, (t3_vec3){ k->hw - 0.06f, y, -k->hl + 0.15f }, dn, 1, 0);
  int v2 = mb_vert(&b, (t3_vec3){ k->hw - 0.06f, y, k->hl - 0.15f }, dn, 1, 1), v3 = mb_vert(&b, (t3_vec3){ -k->hw + 0.06f, y, k->hl - 0.15f }, dn, 0, 1);
  for (int i = base; i < b.nv; i++) mb_color(&b, i, 0.015f, 0.014f, 0.013f);
  mb_quad(&b, v0, v1, v2, v3);
  mb_quad(&b, v0, v3, v2, v1);
  return mb_finish(&b);
}

/* ── the build ──────────────────────────────────────────────────────── */

static void shell_take(car_shell *s, mesh_build *b) {
  s->nv = b->nv;
  s->rest = malloc(sizeof(float) * 3 * (size_t)(b->nv ? b->nv : 1));
  memcpy(s->rest, b->pos, sizeof(float) * 3 * (size_t)b->nv);
  s->paint = malloc(sizeof(float) * 3 * (size_t)(b->nv ? b->nv : 1));
  memcpy(s->paint, b->col, sizeof(float) * 3 * (size_t)b->nv);
  /* each vertex's triangles, in index order (its normal re-summed the way
   * t3_geometry_compute_vertex_normals sums it) */
  int nt = b->ni / 3;
  s->adj_start = calloc((size_t)b->nv + 1, sizeof(int));
  s->adj_tri = malloc(sizeof(int) * (size_t)(b->ni ? b->ni : 1));
  s->touched = malloc(sizeof(int) * (size_t)(b->nv ? b->nv : 1));
  s->mark = calloc((size_t)(b->nv ? b->nv : 1), 1);
  for (int i = 0; i < b->ni; i++) s->adj_start[b->idx[i] + 1]++;
  for (int v = 0; v < b->nv; v++) s->adj_start[v + 1] += s->adj_start[v];
  int *fill = calloc((size_t)(b->nv ? b->nv : 1), sizeof(int));
  for (int t = 0; t < nt; t++)
    for (int k = 0; k < 3; k++) {
      int v = (int)b->idx[t * 3 + k];
      s->adj_tri[s->adj_start[v] + fill[v]++] = t;
    }
  free(fill);
  s->geo = mb_finish(b);
  t3_geometry_compute_vertex_normals(s->geo);
  s->geo->attributes[T3_ATTR_POSITION]->usage = T3_DYNAMIC_DRAW;
  s->geo->attributes[T3_ATTR_NORMAL]->usage = T3_DYNAMIC_DRAW;
  if (s->geo->attributes[T3_ATTR_COLOR]) s->geo->attributes[T3_ATTR_COLOR]->usage = T3_DYNAMIC_DRAW;
}

/* the bonnet lid: the bonnet's top skin as its own piece on a hinge at the cowl */
static void hood_into(mesh_build *b, const wear *W, const car_spec *k, const shape *sh, t3_vec3 hinge, int nz, int nx) {
  float z0 = sh->cowl + 0.05f, z1 = sh->hood_front;
  pieces tmp;
  memset(&tmp, 0, sizeof tmp);
  for (int i = 0; i < nz; i++) {
    float za = z0 + (z1 - z0) * i / nz, zb = z0 + (z1 - z0) * (i + 1) / nz;
    sec sa, sb;
    section(k, sh, za, &sa);
    section(k, sh, zb, &sb);
    for (int j = 0; j < nx; j++) {
      float ua = (float)j / nx * 2 - 1, ub = (float)(j + 1) / nx * 2 - 1;
      float wa = sa.x[8] - 0.012f, wb2 = sb.x[8] - 0.012f;
      t3_vec3 q[4] = { { ua * wa, sa.y[9] + 0.004f - 0.006f * ua * ua, za }, { ua * wb2, sb.y[9] + 0.004f - 0.006f * ua * ua, zb },
                       { ub * wb2, sb.y[9] + 0.004f - 0.006f * ub * ub, zb }, { ub * wa, sa.y[9] + 0.004f - 0.006f * ub * ub, za } };
      float uv[4][2] = { { q[0].x, za }, { q[1].x, zb }, { q[2].x, zb }, { q[3].x, za } };
      skin_quad(&tmp, W, k, sh, q, uv, false);
    }
  }
  for (int m = 0; m < MATS; m++) {
    mesh_build *s = &tmp.mb[m];
    int base = b->nv;
    for (int v = 0; v < s->nv; v++) {
      const float *p = s->pos + v * 3, *n = s->nor + v * 3, *u = s->uv + v * 2, *c = s->col + v * 3;
      int q = mb_vert(b, (t3_vec3){ p[0] - hinge.x, p[1] - hinge.y, p[2] - hinge.z }, (t3_vec3){ n[0], n[1], n[2] }, u[0], u[1]);
      mb_color(b, q, c[0], c[1], c[2]);
    }
    for (int i = 0; i < s->ni; i += 3) mb_tri(b, base + (int)s->idx[i], base + (int)s->idx[i + 1], base + (int)s->idx[i + 2]);
    free(s->pos), free(s->nor), free(s->uv), free(s->col), free(s->idx);
  }
}

static const float WHITE[3] = { 1, 1, 1 };

/* what every car shares: the materials that are the same on all of them and
 * the parts that are the same for a kind (wheels, wheel-arch liners, the
 * cabin). The renderer draws the meshes that share a geometry and a material
 * as one instanced draw, so the six cars' tyres are a draw or two a view,
 * not twenty-four. Made with the first car, kept for the program's life. */
static struct {
  bool made;
  t3_material *tyre, *wheel, *chrome, *black, *dark;
  t3_geometry *tyres[CAR_KINDS][3], *rims[CAR_KINDS][3][2], *liners[CAR_KINDS], *cabin[CAR_KINDS];
} SHARED;

static void shared_make(void) {
  if (SHARED.made) return;
  SHARED.made = true;
  SHARED.chrome = phys(lin(0.7f, 0.68f, 0.65f), 0.24f, 1);
  tex_apply(SHARED.chrome, TEX_STEEL, 1, 0.5f, 1);
  SHARED.chrome->vertex_colors = true;
  SHARED.black = phys(lin(1, 1, 1), 0.75f, 0);
  SHARED.black->vertex_colors = true;
  SHARED.dark = phys(lin(1, 1, 1), 0.9f, 0);
  SHARED.dark->vertex_colors = true;
  SHARED.tyre = phys(lin(1, 1, 1), 0.85f, 0);
  tex_apply_uv(SHARED.tyre, TEX_RUBBER, 1, 1, 1.3f, 1);
  SHARED.tyre->vertex_colors = true;
  SHARED.wheel = phys(lin(0.45f, 0.43f, 0.4f), 0.55f, 0.8f);
  tex_apply(SHARED.wheel, TEX_RUST, 1, 0.6f, 1);
  SHARED.wheel->vertex_colors = true;
  for (int kind = 0; kind < CAR_KINDS; kind++) {
    const car_spec *k = &CAR_SPEC[kind];
    for (int lod = 0; lod < 3; lod++) {
      SHARED.tyres[kind][lod] = tyre_geo(k->wr, lod);
      for (int h = 0; h < 2; h++) SHARED.rims[kind][lod][h] = rim_geo(k->wr, lod, h == 1);
    }
    SHARED.liners[kind] = liners_geo(k);
    SHARED.cabin[kind] = cabin_geo(k, &SHAPES[kind]);
  }
}

void car_build(car_model *m, int kind, int colour, int number) {
  shared_make();
  memset(m, 0, sizeof *m);
  m->kind = kind;
  m->colour = colour;
  m->number = number;
  const car_spec *k = &CAR_SPEC[kind];
  const shape *sh = &SHAPES[kind];
  m->root = t3_group_new();
  m->body = t3_group_new();
  t3_object_add(m->root, m->body);
  wear W;
  W.seed = (uint32_t)number * 2654435761u + (uint32_t)kind;
  {
    const float *c = CAR_RGB[colour];
    float l = (c[0] + c[1] + c[2]) / 3;
    t3_color pc = lin(c[0] * 0.7f + l * 0.3f, c[1] * 0.7f + l * 0.3f, c[2] * 0.7f + l * 0.3f);
    W.paint[0] = pc.r, W.paint[1] = pc.g, W.paint[2] = pc.b;
    static const float PRIMERS[3][3] = { { 0.4f, 0.4f, 0.38f }, { 0.38f, 0.19f, 0.14f }, { 0.18f, 0.19f, 0.18f } };
    const float *pr = PRIMERS[hh(W.seed, 1) % 3];
    t3_color p2 = lin(pr[0], pr[1], pr[2]);
    W.primer[0] = p2.r, W.primer[1] = p2.g, W.primer[2] = p2.b;
    const float *alt = CAR_RGB[(colour + 3 + (int)(hh(W.seed, 2) % 3)) % CAR_COLOURS];
    t3_color p3 = lin(alt[0] * 0.75f + 0.05f, alt[1] * 0.75f + 0.05f, alt[2] * 0.75f + 0.05f);
    W.alt[0] = p3.r, W.alt[1] = p3.g, W.alt[2] = p3.b;
    W.panel_a = (int)(hh(W.seed, 3) % 6);
    W.panel_b = hh(W.seed, 4) % 2 == 0 ? (int)(hh(W.seed, 5) % 6) : -1;
    if (W.panel_b == W.panel_a) W.panel_b = -1;
    /* the navy and the brown cars wear a cream upper half */
    W.two_tone = colour == 2 || colour == 6;
    t3_color p4 = lin(0.74f, 0.71f, 0.62f);
    W.two[0] = p4.r, W.two[1] = p4.g, W.two[2] = p4.b;
    if (W.two_tone) W.panel_b = -1;
  }
  /* materials: clearcoated paint, flat primer, bubbled rust, chrome, rubber, glass */
  t3_material *mat[MATS];
  mat[MAT_PAINT] = phys(lin(1, 1, 1), 0.42f, 0);
  tex_apply(mat[MAT_PAINT], TEX_PAINT, 1, 0.35f, 1);
  mat[MAT_PAINT]->clearcoat = 0.6f;
  mat[MAT_PAINT]->clearcoat_roughness = 0.28f;
  mat[MAT_PRIMER] = phys(lin(1, 1, 1), 0.9f, 0);
  tex_apply(mat[MAT_PRIMER], TEX_PRIMER, 1, 0.5f, 1);
  mat[MAT_RUST] = phys(lin(1, 1, 1), 0.85f, 0.25f);
  tex_apply(mat[MAT_RUST], TEX_RUST, 1, 1.4f, 1);
  for (int i = 0; i < MATS; i++) mat[i]->vertex_colors = true;
  t3_material *chrome = SHARED.chrome, *black = SHARED.black, *dark = SHARED.dark;
  /* the bodies, three levels of detail */
  for (int lod = 0; lod < 3; lod++) {
    pieces P;
    memset(&P, 0, sizeof P);
    mesh_build glass = { 0 }, trim = { 0 };
    loft(&P, &W, k, sh, lod);
    greenhouse(&P, &glass, &trim, &W, k, sh, lod);
    m->bodies[lod] = t3_group_new();
    if (lod > 0) {
      /* the lighter levels draw their skin in fewer meshes: the primer and
       * the rust patches, the glass (dark, opaque) and the trim with the
       * paint's level-2 silhouette, or in a second, flat mesh at level 1; the
       * bumpers ride along (a car's draws count once per view) */
      static const float GLASS_C[3] = { 0.07f, 0.08f, 0.08f }, CHROME_C[3] = { 0.5f, 0.49f, 0.46f };
      for (int v = 0; v < glass.nv; v++) mb_color(&glass, v, GLASS_C[0], GLASS_C[1], GLASS_C[2]);
      for (int v = 0; v < trim.nv; v++) mb_color(&trim, v, CHROME_C[0], CHROME_C[1], CHROME_C[2]);
      int into = lod == 1 ? MAT_PRIMER : MAT_PAINT;
      if (lod == 2) mb_merge(&P.mb[MAT_PAINT], &P.mb[MAT_PRIMER]);
      mb_merge(&P.mb[into], &P.mb[MAT_RUST]);
      mb_merge(&P.mb[into], &glass);
      mb_merge(&P.mb[into], &trim);
      t3_mat4 one;
      t3_mat4_identity(&one);
      mb_append(&P.mb[into], t3_retain(SHARED.liners[kind]), &one); /* the arch liners too */
      float bump_y = sh->sill + 0.2f;
      for (int e = 0; e < 2; e++)
        box_into(&P.mb[into], (t3_vec3){ 0, bump_y, e == 0 ? k->hl + 0.07f : -k->hl - 0.07f }, (t3_vec3){ 2 * k->hw + 0.04f, 0.17f, 0.1f }, 0.03f, CHROME_C);
    }
    for (int i = 0; i < MATS; i++) {
      car_shell *s = &m->shell[lod][i];
      if (!P.mb[i].nv) continue;
      shell_take(s, &P.mb[i]);
      s->mesh = t3_mesh_new(s->geo, mat[i]);
      s->mesh->base.cast_shadow = s->mesh->base.receive_shadow = true;
      t3_object_add(m->bodies[lod], s->mesh);
    }
    if (glass.nv) {
      car_shell *s = &m->glass[lod];
      shell_take(s, &glass);
      /* tinted, dirty, see-through: the cabin shows behind it, the sky in it */
      /* (the windscreen's program: a colour map, clearcoat, both sides) */
      t3_material *g = phys(lin(0.32f, 0.36f, 0.36f), 0.05f, 0);
      t3_material_set_texture(g, T3_MAP, tex_crack_clean());
      g->transparent = true;
      g->opacity = 0.5f;
      g->depth_write = false;
      g->side = T3_DOUBLE_SIDE;
      g->clearcoat = 1;
      g->clearcoat_roughness = 0.08f;
      s->mesh = t3_mesh_new(s->geo, g);
      s->mesh->base.receive_shadow = true;
      s->mesh->base.render_order = 10;
      t3_object_add(m->bodies[lod], s->mesh);
      t3_release(g);
    }
    if (trim.nv) { /* (level 0's: the lighter levels carry theirs in the skin) */
      car_shell *s = &m->trim;
      shell_take(s, &trim);
      s->mesh = t3_mesh_new(s->geo, chrome);
      s->mesh->base.receive_shadow = true;
      t3_object_add(m->bodies[lod], s->mesh);
    }
    t3_object_add(m->body, m->bodies[lod]);
    t3_release(m->bodies[lod]);
    m->bodies[lod]->visible = lod == 0;
  }
  mesh_in(m->bodies[0], t3_retain(SHARED.liners[kind]), dark, false); /* (the lighter levels carry them in their skin) */
  /* the bonnet lid on its hinge (dents ride it too) */
  {
    t3_vec3 hinge = { 0, keyed(sh->belt, sh->nbelt, sh->cowl) + 0.02f, sh->cowl + 0.05f };
    mesh_build b = { 0 };
    hood_into(&b, &W, k, sh, hinge, 14, 12);
    shell_take(&m->lid, &b);
    m->hood = t3_group_new();
    m->hood->position = hinge;
    m->lid_origin = hinge;
    t3_material *ld = phys(lin(1, 1, 1), 0.42f, 0);
    tex_apply(ld, TEX_PAINT, 1, 0.35f, 1);
    ld->vertex_colors = true;
    ld->clearcoat = 0.6f;
    ld->clearcoat_roughness = 0.28f;
    ld->side = T3_DOUBLE_SIDE;
    m->lid.mesh = t3_mesh_new(m->lid.geo, ld);
    m->lid.mesh->base.cast_shadow = m->lid.mesh->base.receive_shadow = true;
    t3_object_add(m->hood, m->lid.mesh);
    t3_object_add(m->body, m->hood);
    t3_release(m->hood);
    t3_release(ld);
  }
  /* the number, sprayed on both doors and the roof: deformable quads on the skin */
  {
    mesh_build b = { 0 };
    float y = (sh->sill + keyed(sh->belt, sh->nbelt, sh->bpillar)) * 0.5f + 0.06f, z = (sh->cowl + sh->bpillar) * 0.5f - 0.05f, r = 0.3f;
    if (kind == CAR_VAN) z = sh->bpillar - 0.7f, y += 0.35f;
    for (int s = -1; s <= 1; s += 2) {
      const int G = 6;
      int base = b.nv;
      for (int j = 0; j <= G; j++)
        for (int i = 0; i <= G; i++) {
          float u = (float)i / G, v = (float)j / G;
          float zz = z + (u - 0.5f) * 2 * r * (s > 0 ? -1 : 1), yy = y + (0.5f - v) * 2 * r * 0.85f;
          sec S;
          section(k, sh, zz, &S);
          float x = S.x[5] + 0.004f;
          if (kind == CAR_VAN && yy > keyed(sh->belt, sh->nbelt, zz)) x = gh_of(k, sh).wb + 0.004f;
          for (int q = 2; q < 7; q++)
            if (yy >= S.y[q] && yy <= S.y[q + 1]) x = S.x[q] + (S.x[q + 1] - S.x[q]) * (yy - S.y[q]) / fmaxf(1e-4f, S.y[q + 1] - S.y[q]) + 0.004f;
          int vv = mb_vert(&b, (t3_vec3){ s * x, yy, zz }, (t3_vec3){ (float)s, 0, 0 }, u, v);
          mb_color(&b, vv, 1, 1, 1);
        }
      for (int j = 0; j < G; j++)
        for (int i = 0; i < G; i++) {
          int a = base + j * (G + 1) + i;
          if (s > 0) mb_quad(&b, a, a + G + 1, a + G + 2, a + 1);
          else mb_quad(&b, a, a + 1, a + G + 2, a + G + 1);
        }
    }
    gh g = gh_of(k, sh);
    float rz = (sh->roof_f + sh->roof_b) * 0.5f, rr = fminf(0.42f, (sh->roof_f - sh->roof_b) * 0.4f);
    int base = b.nv;
    for (int j = 0; j <= 1; j++)
      for (int i = 0; i <= 1; i++) {
        int vv = mb_vert(&b, (t3_vec3){ (i ? 1 : -1) * rr, g.yr + 0.04f, rz + (j ? -1 : 1) * rr }, (t3_vec3){ 0, 1, 0 }, (float)i, (float)j);
        mb_color(&b, vv, 1, 1, 1);
      }
    mb_quad(&b, base, base + 2, base + 3, base + 1);
    shell_take(&m->numbers, &b);
    t3_material *mt = phys(lin(1, 1, 1), 0.8f, 0);
    t3_material_set_texture(mt, T3_MAP, tex_number(number, CAR_RGB[colour]));
    mt->alpha_test = 0.5f;
    mt->vertex_colors = true;
    m->numbers.mesh = t3_mesh_new(m->numbers.geo, mt);
    m->numbers.mesh->base.receive_shadow = true;
    t3_object_add(m->body, m->numbers.mesh);
    t3_release(mt);
  }
  m->cabin = mesh_in(m->bodies[0], t3_retain(SHARED.cabin[kind]), dark, false);
  /* the windscreen, cracked, bowed a little (its own glass: partly see-through) */
  {
    gh g = gh_of(k, sh);
    mesh_build b = { 0 };
    const int NU = 8;
    for (int i = 0; i <= NU; i++) {
      float u = (float)i / NU * 2 - 1, bow = 0.035f * (1 - u * u);
      int a = mb_vert(&b, (t3_vec3){ u * (g.wb - 0.05f), g.yb_f, sh->cowl - 0.01f + bow }, (t3_vec3){ 0, 0.6f, 1 }, (u + 1) * 0.5f, 0);
      int c = mb_vert(&b, (t3_vec3){ u * (g.wr - 0.04f), g.yr - 0.04f, sh->roof_f + 0.01f + bow }, (t3_vec3){ 0, 0.6f, 1 }, (u + 1) * 0.5f, 1);
      mb_color(&b, a, 1, 1, 1);
      mb_color(&b, c, 1, 1, 1);
      if (i) mb_quad(&b, a - 2, a, c, c - 2);
    }
    shell_take(&m->screen, &b);
    t3_material *gm = phys(lin(0.5f, 0.55f, 0.53f), 0.06f, 0);
    t3_material_set_texture(gm, T3_MAP, tex_crack());
    gm->transparent = true;
    gm->opacity = 0.42f;
    gm->depth_write = false;
    gm->side = T3_DOUBLE_SIDE;
    gm->clearcoat = 1;
    gm->clearcoat_roughness = 0.12f;
    m->screen.mesh = t3_mesh_new(m->screen.geo, gm);
    m->screen.mesh->base.render_order = 10;
    m->screen.mesh->base.receive_shadow = true;
    t3_object_add(m->bodies[0], m->screen.mesh);
    t3_release(gm);
  }
  /* bumpers: chrome bars with a rubber strip on two brackets, pivoted at one bracket */
  float bump_y = sh->sill + 0.2f;
  for (int e = 0; e < 2; e++) {
    float z = e == 0 ? k->hl + 0.07f : -k->hl - 0.07f, dir = e == 0 ? 1.0f : -1.0f;
    t3_object *pivot = t3_group_new();
    pivot->position = (t3_vec3){ -k->hw + 0.25f, bump_y, z };
    mesh_build bb = { 0 }, rb = { 0 };
    static const float RB[3] = { 0.03f, 0.03f, 0.03f };
    float L = 2 * k->hw + 0.04f, x0 = k->hw - 0.25f;
    box_into(&bb, (t3_vec3){ x0, 0, 0 }, (t3_vec3){ L, 0.17f, 0.1f }, 0.04f, WHITE);
    box_into(&rb, (t3_vec3){ x0, 0.0f, dir * 0.052f }, (t3_vec3){ L - 0.12f, 0.05f, 0.02f }, 0.01f, RB);
    for (int s = 0; s < 2; s++) box_into(&rb, (t3_vec3){ s ? 2 * x0 : 0, 0, -dir * 0.1f }, (t3_vec3){ 0.06f, 0.08f, 0.14f }, 0.005f, RB);
    mb_merge(&bb, &rb); /* the rubber strip and the brackets in the bar's mesh (their own dark colour) */
    mesh_in(pivot, mb_finish(&bb), chrome, true);
    t3_object_add(m->bodies[0], pivot);
    t3_release(pivot);
    m->bumper[e] = pivot;
  }
  /* the front: a recessed black grille with chrome slats, quad rectangular
   * sealed beams in chrome bezels (one side taped over), amber parking
   * lamps; the back: tail-lamp clusters in chrome, a blank plate, an
   * exhaust; door handles, mirrors, the fuel flap */
  m->lamp_mat = phys(lin(1, 0.97f, 0.88f), 0.2f, 0);
  m->lamp_mat->emissive = lin(1, 0.86f, 0.62f);
  m->lamp_mat->emissive_intensity = 3;
  m->lamp_mat->vertex_colors = true;
  m->tail_mat = phys(lin(0.45f, 0.04f, 0.035f), 0.25f, 0);
  m->tail_mat->emissive = lin(1, 0.1f, 0.06f);
  m->tail_mat->emissive_intensity = 1.5f;
  m->tail_mat->vertex_colors = true;
  {
    sec fs;
    section(k, sh, k->hl - 0.01f, &fs);
    float fy0 = sh->sill + 0.3f, fy1 = fs.y[7] - 0.04f, fz = k->hl + 0.004f;
    float gw = k->hw - 0.45f;
    mesh_build g = { 0 }, ch = { 0 }, lamp = { 0 };
    /* the parking lamps ride in the black details' mesh, in their own colour */
    t3_color ac = lin(0.8f, 0.42f, 0.06f);
    const float AMBER[3] = { ac.r, ac.g, ac.b };
    static const float BLK[3] = { 0.02f, 0.02f, 0.02f }, TP[3] = { 0.35f, 0.35f, 0.33f };
    box_into(&g, (t3_vec3){ 0, (fy0 + fy1) * 0.5f, fz - 0.03f }, (t3_vec3){ 2 * gw, fy1 - fy0, 0.04f }, 0.01f, BLK);
    for (int sl = 0; sl < 5; sl++) box_into(&ch, (t3_vec3){ 0, fy0 + (fy1 - fy0) * (sl + 0.5f) / 5, fz }, (t3_vec3){ 2 * gw, 0.012f, 0.012f }, 0.003f, WHITE);
    box_into(&ch, (t3_vec3){ 0, (fy0 + fy1) * 0.5f, fz + 0.005f }, (t3_vec3){ 0.03f, fy1 - fy0, 0.012f }, 0.003f, WHITE);
    int taped = (int)(hh(W.seed, 9) % 3);
    for (int s = -1; s <= 1; s += 2) {
      float lx = s * (gw + 0.18f), ly = (fy0 + fy1) * 0.5f + 0.01f;
      box_into(&ch, (t3_vec3){ lx, ly, fz - 0.005f }, (t3_vec3){ 0.3f, 0.17f, 0.03f }, 0.012f, WHITE);
      bool tape = (taped == 0 && s > 0) || (taped == 1 && s < 0);
      for (int q = 0; q < 2; q++) {
        float qx = lx + (q ? 0.07f : -0.07f);
        if (tape) box_into(&g, (t3_vec3){ qx, ly, fz + 0.012f }, (t3_vec3){ 0.13f, 0.1f, 0.01f }, 0.01f, BLK);
        else box_into(&lamp, (t3_vec3){ qx, ly, fz + 0.012f }, (t3_vec3){ 0.12f, 0.09f, 0.012f }, 0.012f, WHITE);
      }
      if (tape) box_into(&ch, (t3_vec3){ lx, ly, fz + 0.022f }, (t3_vec3){ 0.3f, 0.035f, 0.006f }, 0.002f, TP);
      box_into(&g, (t3_vec3){ s * (k->hw - 0.32f), bump_y - 0.02f, k->hl + 0.13f }, (t3_vec3){ 0.14f, 0.05f, 0.012f }, 0.01f, AMBER);
    }
    /* the lamps show at levels 0 and 1 (a split view draws the other cars at 1) */
    m->lamps = t3_group_new();
    t3_object_add(m->body, m->lamps);
    t3_release(m->lamps);
    mesh_in(m->lamps, mb_finish(&lamp), m->lamp_mat, false);
    mesh_build tl = { 0 }, bk = { 0 }, cr = { 0 };
    sec rs;
    section(k, sh, -k->hl + 0.01f, &rs);
    float ty = sh->kind == CAR_VAN ? 0.92f : rs.y[7] - 0.14f, tz = -k->hl - 0.004f;
    static const float PLATE[3] = { 0.5f, 0.48f, 0.42f };
    for (int s = -1; s <= 1; s += 2) {
      box_into(&cr, (t3_vec3){ s * (k->hw - 0.3f), ty, tz + 0.004f }, (t3_vec3){ 0.42f, 0.17f, 0.02f }, 0.012f, WHITE);
      box_into(&tl, (t3_vec3){ s * (k->hw - 0.3f), ty, tz - 0.01f }, (t3_vec3){ 0.38f, 0.13f, 0.02f }, 0.01f, WHITE);
    }
    box_into(&bk, (t3_vec3){ 0, bump_y + 0.17f, tz - 0.004f }, (t3_vec3){ 0.32f, 0.16f, 0.012f }, 0.004f, PLATE);
    mesh_in(m->lamps, mb_finish(&tl), m->tail_mat, false);
    mb_merge(&ch, &cr); /* the chrome and the black details, one mesh each */
    mb_merge(&g, &bk);
    mesh_build ex = { 0 }, sm = { 0 };
    {
      t3_vec2 pts[] = { { 0.025f, -0.2f }, { 0.03f, -0.2f }, { 0.03f, 0.2f }, { 0.025f, 0.2f } };
      mesh_build t = { 0 };
      lathe_x(&t, pts, 4, 12, 1, 0.18f);
      int base = ex.nv;
      for (int i = 0; i < t.nv; i++) {
        const float *p = t.pos + i * 3, *n = t.nor + i * 3;
        /* the lathe runs along x: turn it to run along z */
        int v = mb_vert(&ex, (t3_vec3){ -k->hw + 0.45f + p[2], sh->sill - 0.02f + p[1], -k->hl + 0.05f + p[0] }, (t3_vec3){ n[2], n[1], n[0] }, 0, 0);
        mb_color(&ex, v, 0.18f, 0.13f, 0.1f);
      }
      for (int i = 0; i < t.ni; i += 3) mb_tri(&ex, base + (int)t.idx[i], base + (int)t.idx[i + 2], base + (int)t.idx[i + 1]);
      free(t.pos), free(t.nor), free(t.uv), free(t.col), free(t.idx);
    }
    gh gg = gh_of(k, sh);
    for (int s = -1; s <= 1; s += 2) {
      float hz[2] = { sh->bpillar + 0.12f, sh->deck + 0.28f };
      for (int d = 0; d < (kind == CAR_SEDAN || kind == CAR_WAGON ? 2 : 1); d++) {
        float zz = hz[d], yy = keyed(sh->belt, sh->nbelt, zz) - 0.13f;
        sec S;
        section(k, sh, zz, &S);
        box_into(&sm, (t3_vec3){ s * (S.x[6] + 0.012f), yy, zz }, (t3_vec3){ 0.03f, 0.03f, 0.16f }, 0.01f, WHITE);
      }
      box_into(&sm, (t3_vec3){ s * (gg.wb + 0.1f), gg.yb_f + 0.1f, sh->cowl - 0.12f }, (t3_vec3){ 0.05f, 0.1f, 0.15f }, 0.02f, WHITE);
      box_into(&sm, (t3_vec3){ s * (gg.wb + 0.04f), gg.yb_f + 0.04f, sh->cowl - 0.1f }, (t3_vec3){ 0.08f, 0.02f, 0.03f }, 0.005f, WHITE);
    }
    {
      float fz2 = sh->deck - 0.35f, fy = keyed(sh->belt, sh->nbelt, fz2) - 0.15f;
      sec S;
      section(k, sh, fz2, &S);
      box_into(&sm, (t3_vec3){ -(S.x[6] + 0.004f), fy, fz2 }, (t3_vec3){ 0.012f, 0.11f, 0.11f }, 0.03f, WHITE);
    }
    mb_merge(&g, &ex); /* the exhaust with the black details */
    mb_merge(&ch, &sm);
    mesh_in(m->bodies[0], mb_finish(&ch), chrome, false);
    m->details = mesh_in(m->bodies[0], mb_finish(&g), black, false);
  }
  /* the beams: soft additive cones out front, front faces only */
  t3_material *beam = t3_mesh_basic_material_new(0xffffff);
  beam->color = lin(1, 0.88f, 0.65f);
  beam->transparent = true;
  beam->opacity = 0.035f;
  beam->vertex_colors = true;
  beam->blending = T3_ADDITIVE_BLENDING;
  beam->depth_write = false;
  beam->side = T3_FRONT_SIDE;
  m->beams = t3_group_new();
  {
    sec fs;
    section(k, sh, k->hl - 0.01f, &fs);
    float ly = (sh->sill + 0.3f + fs.y[7] - 0.04f) * 0.5f;
    /* both cones in one mesh */
    mesh_build bm = { 0 };
    for (int s = -1; s <= 1; s += 2) {
      t3_mat4 cm;
      t3_mat4_compose(&cm, (t3_vec3){ s * (k->hw - 0.27f), ly - 0.1f, k->hl + 4.1f },
                      t3_quat_from_euler((t3_euler){ -1.5707963f + 0.05f, 0, 0, T3_XYZ }), (t3_vec3){ 1, 1, 1 });
      mb_append(&bm, geo_beam(0.08f, 1.1f, 8), &cm);
    }
    t3_mesh *c = t3_mesh_new(mb_finish(&bm), beam);
    t3_release(c->geometry);
    c->base.render_order = 30;
    t3_object_add(m->beams, c);
    t3_release(c);
  }
  t3_release(beam);
  t3_object_add(m->bodies[0], m->beams);
  t3_release(m->beams);
  /* wheels: tyres with sidewalls and tread, steel wheels (a hubcap on some), three LODs */
  t3_material *tyre = SHARED.tyre, *wheelm = SHARED.wheel;
  t3_geometry *(*tg) = SHARED.tyres[kind], *(*rgs)[2] = SHARED.rims[kind];
  for (int w = 0; w < 4; w++) {
    float side = w & 1 ? -1.0f : 1.0f;
    t3_object *spin = t3_group_new();
    bool hubcap = hh(W.seed, 20 + (uint32_t)w) % 4 == 0;
    for (int lod = 0; lod < 3; lod++) {
      t3_object *wh = t3_group_new();
      t3_mesh *t = t3_mesh_new(tg[lod], tyre);
      t->base.cast_shadow = lod < 2;
      t->base.receive_shadow = true;
      t3_object_add(wh, t);
      t3_release(t);
      t3_mesh *r = t3_mesh_new(rgs[lod][hubcap ? 1 : 0], hubcap ? chrome : wheelm);
      r->base.receive_shadow = true;
      t3_object_add(wh, r);
      t3_release(r);
      if (side < 0) t3_object_set_rotation(wh, 0, 3.1415927f, 0);
      wh->visible = lod == 0;
      t3_object_add(spin, wh);
      t3_release(wh);
      m->wheel_lod[w][lod] = wh;
    }
    spin->position = (t3_vec3){ side * (k->hw - 0.12f), k->wr, w < 2 ? k->af : k->ar };
    t3_object_add(m->root, spin);
    t3_release(spin);
    m->wheel[w] = spin;
  }
  for (int i = 0; i < MATS; i++) t3_release(mat[i]);
  t3_release(m->body);
  m->lod = 0;
}

static void shell_free(car_shell *s) {
  free(s->rest);
  free(s->paint);
  free(s->adj_start);
  free(s->adj_tri);
  free(s->touched);
  free(s->mark);
  if (s->mesh) t3_release(s->mesh);
  if (s->geo) t3_release(s->geo);
  memset(s, 0, sizeof *s);
}

void car_free(car_model *m) {
  for (int l = 0; l < 3; l++) {
    for (int i = 0; i < 3; i++) shell_free(&m->shell[l][i]);
    shell_free(&m->glass[l]);
  }
  shell_free(&m->trim);
  shell_free(&m->lid);
  shell_free(&m->numbers);
  shell_free(&m->screen);
  if (m->lamp_mat) t3_release(m->lamp_mat);
  if (m->tail_mat) t3_release(m->tail_mat);
  memset(m, 0, sizeof *m);
}

/* ── dents ──────────────────────────────────────────────────────────
 * A changed dent (new, deepened or replaced) moves only the vertices within
 * its reach, old and new: those are re-dented from rest by every dent, the
 * normals re-summed round them, and only that span of each buffer goes to
 * the GPU. Only the level of detail on show is kept up; another catches up
 * when it is shown. The pieces of one car run in parallel on the job pool. */

static float dent_reach(const dent *d) { return 0.4f + d->depth * 2.4f; }

/* one vertex at (x, y, z) in the body's frame: where every dent pushes it, and how bare it is */
static void dent_vertex(const dent *ds, int n, const car_spec *k, float x, float y, float z, float out[3], float *bare_out) {
  float px = 0, py = 0, pz = 0, bare = 0;
  for (int i = 0; i < n; i++) {
    const dent *d = &ds[i];
    float dx = x - d->s, dz = z - d->f, dy = (y - d->y) * 0.75f;
    float r = dent_reach(d), dd = dx * dx + dy * dy + dz * dz;
    if (dd > r * r) continue;
    float q = sqrtf(dd) / r, f = 1 - q;
    /* crumpled sheet: a sharp centre, a buckle ring, creases radiating */
    float ang = cw_atan2(dy, dz);
    f = f * f * (1.6f - 0.6f * f) + 0.2f * cw_sin(q * 9.4f) * (1 - q) * (1 - q) + 0.12f * cw_sin(ang * 5 + d->f * 7) * f * (1 - f) * 4;
    float nx = d->s / k->hw, nz = d->f / k->hl, nl = sqrtf(nx * nx + nz * nz);
    if (nl < 1e-3f) continue;
    nx /= nl, nz /= nl;
    px -= nx * d->depth * f;
    pz -= nz * d->depth * f;
    py -= d->depth * f * 0.3f * (y > 0.9f ? 1 : 0.2f);
    bare = fmaxf(bare, (1 - q) * cw_clampf(d->depth * 5, 0, 1));
  }
  out[0] = px, out[1] = py, out[2] = pz;
  *bare_out = bare;
}

typedef struct dent_job {
  car_shell *sh;
  const dent *ds, *chg; /* every dent; the spheres that changed (old and new) */
  int nd, nchg;
  const car_spec *k;
  t3_vec3 origin;
  bool repaint;
  int lo, hi, nlo, nhi; /* out: the vertex spans of positions / colours and of normals */
} dent_job;

static void dent_run(dent_job *j) {
  car_shell *sh = j->sh;
  j->lo = j->nlo = sh->nv, j->hi = j->nhi = -1;
  t3_attribute *P = sh->geo->attributes[T3_ATTR_POSITION], *C = sh->geo->attributes[T3_ATTR_COLOR], *N = sh->geo->attributes[T3_ATTR_NORMAL];
  float *p = P->array, *col = C ? (float *)C->array : NULL, *nor = N->array;
  t3_color steel = lin(0.46f, 0.45f, 0.43f), rust = lin(0.36f, 0.18f, 0.08f);
  int nt = 0;
  for (int v = 0; v < sh->nv; v++) {
    float x = sh->rest[v * 3] + j->origin.x, y = sh->rest[v * 3 + 1] + j->origin.y, z = sh->rest[v * 3 + 2] + j->origin.z;
    bool in = false;
    for (int i = 0; i < j->nchg && !in; i++) {
      const dent *d = &j->chg[i];
      float dx = x - d->s, dz = z - d->f, dy = (y - d->y) * 0.75f, r = dent_reach(d);
      in = dx * dx + dy * dy + dz * dz <= r * r;
    }
    if (!in) continue;
    float off[3], bare;
    dent_vertex(j->ds, j->nd, j->k, x, y, z, off, &bare);
    p[v * 3] = sh->rest[v * 3] + off[0];
    p[v * 3 + 1] = sh->rest[v * 3 + 1] + off[1];
    p[v * 3 + 2] = sh->rest[v * 3 + 2] + off[2];
    if (col) {
      float pr = sh->paint[v * 3], pg = sh->paint[v * 3 + 1], pb = sh->paint[v * 3 + 2];
      float r = pr, g = pg, b = pb;
      if (j->repaint && bare > 0.5f) r = steel.r, g = steel.g, b = steel.b;
      else if (j->repaint && bare > 0.15f) {
        float t = (bare - 0.15f) / 0.35f;
        r = pr + (rust.r - pr) * t, g = pg + (rust.g - pg) * t, b = pb + (rust.b - pb) * t;
      }
      col[v * 3] = r, col[v * 3 + 1] = g, col[v * 3 + 2] = b;
    }
    if (v < j->lo) j->lo = v;
    if (v > j->hi) j->hi = v;
    sh->mark[v] = 1;
    sh->touched[nt++] = v;
  }
  if (!nt) return;
  /* the normals of every vertex on a triangle that moved */
  const t3_attribute *I = sh->geo->index;
  int moved = nt;
  for (int m = 0; m < moved; m++) {
    int v = sh->touched[m];
    for (int a = sh->adj_start[v]; a < sh->adj_start[v + 1]; a++) {
      int t = sh->adj_tri[a];
      for (int c = 0; c < 3; c++) {
        int u = I->type == T3_UINT16 ? ((const uint16_t *)I->array)[t * 3 + c] : (int)((const uint32_t *)I->array)[t * 3 + c];
        if (!sh->mark[u]) sh->mark[u] = 1, sh->touched[nt++] = u;
      }
    }
  }
  for (int m = 0; m < nt; m++) {
    int v = sh->touched[m];
    t3_vec3 s = { 0, 0, 0 };
    for (int a = sh->adj_start[v]; a < sh->adj_start[v + 1]; a++) {
      int t = sh->adj_tri[a], ix[3];
      for (int c = 0; c < 3; c++) ix[c] = I->type == T3_UINT16 ? ((const uint16_t *)I->array)[t * 3 + c] : (int)((const uint32_t *)I->array)[t * 3 + c];
      t3_vec3 A = { p[ix[0] * 3], p[ix[0] * 3 + 1], p[ix[0] * 3 + 2] }, B = { p[ix[1] * 3], p[ix[1] * 3 + 1], p[ix[1] * 3 + 2] },
              Cc = { p[ix[2] * 3], p[ix[2] * 3 + 1], p[ix[2] * 3 + 2] };
      t3_vec3 cb = t3_vec3_cross(t3_vec3_sub(Cc, B), t3_vec3_sub(A, B));
      s.x += cb.x, s.y += cb.y, s.z += cb.z;
    }
    s = t3_vec3_normalize(s);
    nor[v * 3] = s.x, nor[v * 3 + 1] = s.y, nor[v * 3 + 2] = s.z;
    sh->mark[v] = 0;
    if (v < j->nlo) j->nlo = v;
    if (v > j->nhi) j->nhi = v;
  }
}

static void dent_range(int begin, int end, int worker, void *ctx) {
  (void)worker;
  dent_job *jobs = ctx;
  for (int i = begin; i < end; i++) dent_run(&jobs[i]);
}

/* the dents that differ between what a level shows and now, old and new (the old spot must spring back) */
static int dent_changes(const dent *was, int nwas, const dent *now, int nnow, dent *out) {
  int n = 0;
  for (int i = 0; i < nnow || i < nwas; i++) {
    bool have_was = i < nwas, have_now = i < nnow;
    if (have_was && have_now && !memcmp(&was[i], &now[i], sizeof(dent))) continue;
    if (have_was) out[n++] = was[i];
    if (have_now) out[n++] = now[i];
  }
  return n;
}

/* brings the pieces of level `lod` (3: the shared pieces) up to the car's dents */
static void dents_catch_up(car_model *m, int lod) {
  dent chg[2 * MAX_DENTS];
  int nchg = dent_changes(m->seen[lod], m->nseen[lod], m->cur, m->ncur, chg);
  if (!nchg) return;
  const car_spec *k = &CAR_SPEC[m->kind];
  dent_job jobs[5];
  int nj = 0;
  t3_vec3 o = { 0, 0, 0 };
  if (lod < 3) {
    for (int i = 0; i < 3; i++) jobs[nj++] = (dent_job){ &m->shell[lod][i], m->cur, chg, m->ncur, nchg, k, o, true };
    jobs[nj++] = (dent_job){ &m->glass[lod], m->cur, chg, m->ncur, nchg, k, o, false };
  } else {
    jobs[nj++] = (dent_job){ &m->trim, m->cur, chg, m->ncur, nchg, k, o, false };
    jobs[nj++] = (dent_job){ &m->lid, m->cur, chg, m->ncur, nchg, k, m->lid_origin, true };
    jobs[nj++] = (dent_job){ &m->numbers, m->cur, chg, m->ncur, nchg, k, o, false };
    jobs[nj++] = (dent_job){ &m->screen, m->cur, chg, m->ncur, nchg, k, o, false };
  }
  int live = 0;
  for (int i = 0; i < nj; i++)
    if (jobs[i].sh->geo) jobs[live++] = jobs[i];
  cw_parallel_for(live, 1, dent_range, jobs);
  for (int i = 0; i < live; i++) {
    const dent_job *j = &jobs[i];
    if (j->hi < 0) continue;
    t3_geometry *g = j->sh->geo;
    t3_attribute *P = g->attributes[T3_ATTR_POSITION], *C = g->attributes[T3_ATTR_COLOR], *N = g->attributes[T3_ATTR_NORMAL];
    P->version++;
    t3_attribute_add_update_range(P, j->lo, j->hi - j->lo + 1);
    if (C) C->version++, t3_attribute_add_update_range(C, j->lo, j->hi - j->lo + 1);
    N->version++;
    t3_attribute_add_update_range(N, j->nlo, j->nhi - j->nlo + 1);
    g->has_bounding_sphere = g->has_bounding_box = false;
  }
  memcpy(m->seen[lod], m->cur, sizeof m->cur);
  m->nseen[lod] = m->ncur;
}

void car_apply_dents(car_model *m, const car *c) {
  if (c->ndent == m->ncur && !memcmp(c->dents, m->cur, sizeof(dent) * (size_t)c->ndent)) return;
  memcpy(m->cur, c->dents, sizeof(dent) * (size_t)c->ndent);
  m->ncur = c->ndent;
  dents_catch_up(m, m->lod);
  dents_catch_up(m, 3);
}

/* a car its own split view follows: the small parts a quarter of the
 * screen cannot show from behind (the grille and lamp bezels, the parking
 * lamps, the exhaust, the headlamp beams) are left out */
void car_set_trimmed(car_model *m, bool trimmed) {
  if (m->details) m->details->base.visible = !trimmed;
  if (m->beams_on) m->beams->visible = !trimmed;
  m->trimmed = trimmed;
}

void car_set_lod(car_model *m, int lod) {
  if (m->lod == lod) return;
  m->lod = lod;
  dents_catch_up(m, lod); /* the dents it missed while another level was drawn */
  for (int l = 0; l < 3; l++) {
    if (m->bodies[l]) m->bodies[l]->visible = l == lod;
    for (int w = 0; w < 4; w++)
      if (m->wheel_lod[w][l]) m->wheel_lod[w][l]->visible = l == lod;
  }
  if (m->hood) m->hood->visible = lod < 2;
  if (m->numbers.mesh) m->numbers.mesh->base.visible = lod < 2;
  if (m->lamps) m->lamps->visible = lod < 2;
}

/* the front wheels' angle as drawn (the sim's, with a little Ackermann:
 * the inside wheel turns more), for the debug field */
float car_wheel_yaw(const car *c, int w) {
  if (w >= 2) return 0;
  /* + steer turns the heading toward +x (left): the left wheel (+x) is the inside one */
  bool inside = (c->steer > 0) == (w == 0);
  return c->steer * (inside ? 1.1f : 0.9f);
}

/* the fixed tick: wheels roll, steer and ride their springs, the bumpers
 * hang and drag, the bonnet pops, the lamps go out on a wreck (the body's
 * pitch, roll and bounce are the physics' own: world.c poses the car) */
void car_pose(car_model *m, const car *c, int ticks) {
  for (int w = 0; w < 4; w++) {
    if (!m->wheel[w]) continue;
    /* where the suspension holds it and how far it has rolled (the sim's
     * wheels); the yaw in the car's frame turns +z toward +x for a positive
     * angle, as the sim's steer does */
    t3_vec3 at;
    t3_quat q;
    sim_wheel(c, w, &at, &q);
    float yaw = car_wheel_yaw(c, w);
    float camber = c->wrecked ? ((w & 1) ? -0.2f : 0.2f) * (w < 2 ? 1 : 0.6f) : 0;
    m->wheel[w]->position = at;
    t3_object_set_quaternion(m->wheel[w], t3_quat_from_euler((t3_euler){ c->veh.wheel[w].spin, yaw, camber, T3_YXZ }));
  }
  for (int e = 0; e < 2; e++) {
    float dmg = c->zone[e == 0 ? ZONE_FRONT : ZONE_REAR];
    float want = dmg > 0.5f ? -0.3f - (dmg - 0.5f) * 1.4f : 0;
    float kick = (c->ax * (e == 0 ? 1 : -1)) * 0.002f;
    m->bumper_v[e] += (want - m->bumper_a[e]) * 0.04f - m->bumper_v[e] * 0.06f + kick;
    m->bumper_a[e] += m->bumper_v[e];
    if (m->bumper_a[e] > 0.02f) m->bumper_a[e] = 0.02f, m->bumper_v[e] = 0;
    if (m->bumper_a[e] < -0.75f) m->bumper_a[e] = -0.75f, m->bumper_v[e] = 0; /* its free end on the dirt */
    t3_object_set_rotation(m->bumper[e], 0, 0, m->bumper_a[e]);
  }
  {
    float dmg = c->zone[ZONE_FRONT];
    float want = dmg > 0.45f ? -(0.22f + (dmg - 0.45f) * 1.1f) : 0;
    m->hood_v += (want - m->hood_a) * 0.05f - m->hood_v * 0.08f + (dmg > 0.45f ? c->ax * 0.0015f : 0);
    m->hood_a += m->hood_v;
    if (m->hood_a > 0) m->hood_a = 0, m->hood_v = 0;
    t3_object_set_rotation(m->hood, m->hood_a, 0, 0);
  }
  bool lit = !c->wrecked;
  float flicker = c->hp < 35 && lit ? 0.6f + 0.4f * cw_sin(ticks * 0.9f + m->number) : 1;
  m->lamp_mat->emissive_intensity = lit ? 3 * flicker : 0;
  m->tail_mat->emissive_intensity = lit ? 1.5f : 0;
  m->beams_on = lit;
  m->beams->visible = lit && !m->trimmed;
}
