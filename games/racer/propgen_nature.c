/* racer's nature props: rocks, mesas, cacti, trees, ferns, mushrooms, ice and crystals.
 * Every generator builds a faceted, vertex-coloured model with its base on y = 0 (see props_gen.h). Colours go
 * dark at the base to light at the top, which stands in for ambient occlusion and makes silhouettes read from far. */
#include <math.h>
#include <stdlib.h>

#include "cartwheel/math.h"
#include "props_gen.h"

static t3_vec3 P(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* a leaf lump: a faceted ball of foliage centred at (x, y, z) */
static void lump(pm *b, float x, float y, float z, float r, float squash, pcol c0, pcol c1, int detail) {
  pm_push(b);
  pm_move(b, x, y, z);
  pm_roty(b, pm_rand(b) * CW_TAU);
  pm_ball(b, r, r * squash, r, detail, 0.22f, c0, c1);
  pm_pop(b);
}

/* ── rocks ───────────────────────────────────────────────────────────────────────────────────────── */

static void rock_palette(int variant, pcol *lo, pcol *hi) {
  switch (variant & 3) {
  case 0: *lo = rgb(0.25f, 0.22f, 0.20f), *hi = rgb(0.58f, 0.52f, 0.45f); break;
  case 1: *lo = rgb(0.22f, 0.22f, 0.24f), *hi = rgb(0.52f, 0.53f, 0.56f); break;
  case 2: *lo = rgb(0.32f, 0.2f, 0.15f), *hi = rgb(0.66f, 0.44f, 0.30f); break;
  default: *lo = rgb(0.27f, 0.25f, 0.2f), *hi = rgb(0.62f, 0.58f, 0.46f); break;
  }
}

GEN(gen_rock) {
  pcol lo, hi;
  rock_palette(variant, &lo, &hi);
  pm_vary(b, 0.13f);
  float r = size * 0.55f;
  int det = lod ? 0 : 1;
  switch (variant) {
  case 0: pm_rock(b, r, 0.78f, 0.26f, lo, hi); break;
  case 1: pm_rock(b, r * 0.8f, 1.35f, 0.24f, lo, hi); break;
  case 2:
    pm_push(b);
    pm_scale(b, 1.5f, 1, 1);
    pm_rock(b, r * 0.8f, 0.5f, 0.22f, lo, hi);
    pm_pop(b);
    break;
  default:
    pm_rock(b, r * 0.85f, 0.8f, 0.3f, lo, hi);
    pm_push(b);
    pm_move(b, r * 0.7f, 0, r * 0.3f);
    pm_rock(b, r * 0.55f, 0.9f, 0.3f, lo, pc_mix(hi, lo, 0.2f));
    pm_pop(b);
    break;
  }
  (void)det;
}

GEN(gen_boulders) {
  pcol lo, hi;
  rock_palette(variant + 1, &lo, &hi);
  pm_vary(b, 0.13f);
  int n = lod ? 4 : 7;
  pm_rock(b, size * 0.5f, 0.7f, 0.28f, lo, hi);
  for (int i = 1; i < n; i++) {
    float a = pm_rand(b) * CW_TAU, d = size * (0.5f + 0.9f * pm_rand(b));
    float r = size * (0.14f + 0.22f * pm_rand(b));
    pm_push(b);
    pm_move(b, cosf(a) * d, 0, sinf(a) * d);
    pm_roty(b, pm_rand(b) * 6);
    pm_rock(b, r, 0.6f + 0.4f * pm_rand(b), 0.3f, pc_mul(lo, 0.9f), pc_mix(hi, lo, pm_rand(b) * 0.4f));
    pm_pop(b);
  }
}

/* strata colours for rock walls and mesas: warm desert bands by default, pale and cool for variant 2 */
static pcol strata(int variant, int i, pm *b) {
  static const unsigned warm[6] = { 0xa8553a, 0xc2774b, 0xd49a62, 0xb4604a, 0xe0b27a, 0x9b4a3b };
  static const unsigned cool[6] = { 0x6a6a74, 0x84828a, 0xa09a98, 0x777784, 0x9c968f, 0x5f6068 };
  static const unsigned sand[6] = { 0xc8a46e, 0xdcbd88, 0xb78e58, 0xe2cc9c, 0xc09a66, 0xd2b27c };
  pcol c = hex((variant == 2 ? cool : variant == 1 ? sand : warm)[(i * 5 + 1) % 6]);
  float j = 0.94f + 0.12f * pm_rand(b);
  return pc_mul(c, j);
}

GEN(gen_mesa) {
  pm_vary(b, 0.06f);
  pm_jitter(b, 0.045f);
  int layers = lod ? 4 : 6;
  float R = size * (variant == 1 ? 0.9f : 0.75f), y = 0;
  float lh = size / (float)layers * 0.85f;
  /* the scree skirt first: it hides the join with the ground */
  pcol sk = strata(variant, 2, b);
  pm_cyl_ex(b, R * 1.55f, R * 1.02f, lh * 1.3f, lod ? 7 : 11, pc_mul(sk, 0.62f), pc_mul(sk, 0.85f), false, false);
  for (int i = 0; i < layers; i++) {
    float t = (float)i / (float)layers;
    float r0 = R * (1.0f - 0.08f * t), r1 = r0 * (0.97f + 0.03f * pm_rand(b));
    if (i % 2 == 1) r0 *= 1.05f, r1 = r0 * 0.99f; /* a harder band sticks out */
    pcol c = strata(variant, i, b);
    pm_push(b);
    pm_move(b, 0, y, 0);
    pm_cyl_ex(b, r0, r1, lh * (i == layers - 1 ? 1.0f : 1.0f), lod ? 7 : 11, pc_mul(c, 0.78f), c, false, i == layers - 1);
    pm_pop(b);
    y += lh;
  }
  /* a few fallen blocks */
  if (!lod) {
    pcol lo = pc_mul(strata(variant, 0, b), 0.6f), hi = strata(variant, 3, b);
    for (int i = 0; i < 5; i++) {
      float a = pm_rand(b) * CW_TAU, d = R * (1.15f + 0.5f * pm_rand(b));
      pm_push(b);
      pm_move(b, cosf(a) * d, 0, sinf(a) * d);
      pm_rock(b, size * 0.06f * (0.6f + pm_rand(b)), 0.8f, 0.3f, lo, hi);
      pm_pop(b);
    }
  }
}

GEN(gen_spire) {
  pm_vary(b, 0.1f);
  pm_jitter(b, 0.07f);
  int segs = lod ? 3 : 5;
  float lean = 0.10f + 0.12f * pm_rand(b), dir = pm_rand(b) * CW_TAU;
  float R = size * 0.13f, y = 0, x = 0, z = 0;
  pcol c0 = strata(variant, 1, b), c1 = strata(variant, 4, b);
  pm_cyl_ex(b, R * 2.4f, R * 1.15f, size * 0.12f, lod ? 6 : 9, pc_mul(c0, 0.5f), pc_mul(c0, 0.85f), false, false);
  pm_rock(b, R * 1.6f, 0.6f, 0.3f, pc_mul(c0, 0.5f), pc_mul(c0, 0.85f));
  for (int i = 0; i < (lod ? 2 : 5); i++) {
    float a = pm_rand(b) * CW_TAU, d = R * (2.0f + 1.4f * pm_rand(b));
    pm_push(b);
    pm_move(b, cosf(a) * d, 0, sinf(a) * d);
    pm_rock(b, R * (0.5f + 0.7f * pm_rand(b)), 0.7f, 0.3f, pc_mul(c0, 0.5f), c1);
    pm_pop(b);
  }
  for (int i = 0; i < segs; i++) {
    float t = (float)i / (float)segs, t1 = (float)(i + 1) / (float)segs;
    float h = size / (float)segs;
    float r0 = R * (1 - 0.7f * t), r1 = R * (1 - 0.7f * t1) * (i == segs - 1 ? 0.35f : 1.0f);
    pm_push(b);
    pm_move(b, x, y, z);
    pm_cyl_ex(b, r0, r1, h, lod ? 5 : 8, pc_mix(c0, c1, t * 0.8f), pc_mix(c0, c1, t1), false, true);
    pm_pop(b);
    x += cosf(dir) * lean * h, z += sinf(dir) * lean * h, y += h * 0.98f;
  }
  /* a balanced cap stone on one variant only: a tall needle with a boulder on it reads as a mushroom otherwise */
  if (variant == 2) {
    pm_push(b);
    pm_move(b, x, y - size * 0.03f, z);
    pm_ball(b, R * 0.75f, R * 0.4f, R * 0.7f, 1, 0.2f, c1, pc_mul(c1, 1.15f));
    pm_pop(b);
  }
}

GEN(gen_canyon_wall) {
  pm_vary(b, 0.08f);
  float H = size, L = size * 3.0f;
  int cols = lod ? 6 : 12;
  float cw = L / (float)cols;
  for (int i = 0; i < cols; i++) {
    float x = -L * 0.5f + (i + 0.5f) * cw;
    float top = H * (0.7f + 0.3f * pm_rand(b)) * (1.0f - 0.25f * fabsf(sinf((float)i * 0.9f + (float)variant)));
    int steps = lod ? 3 : 5;
    float y = 0, depth = size * 0.55f;
    for (int s = 0; s < steps; s++) {
      float sh = top / (float)steps, dd = depth * (1 - 0.14f * (float)s) * (0.9f + 0.2f * pm_rand(b));
      pcol c = strata(variant, s + (i & 1), b);
      pm_push(b);
      pm_move(b, x + pm_rands(b) * cw * 0.05f, y, -dd * 0.5f + size * 0.28f * (float)s / (float)steps);
      pm_box(b, cw * (1.0f + 0.12f * pm_rand(b)), sh * 1.02f, dd, pc_mul(c, 0.72f), c);
      pm_pop(b);
      y += sh;
    }
    /* a ragged crown stone */
    if (!lod && (i & 1)) {
      pm_push(b);
      pm_move(b, x, y - 0.5f, -size * 0.2f);
      pm_rock(b, cw * 0.55f, 0.8f, 0.3f, strata(variant, 2, b), strata(variant, 4, b));
      pm_pop(b);
    }
  }
  /* scree along the foot */
  for (int i = 0; i < (lod ? 4 : 10); i++) {
    pm_push(b);
    pm_move(b, -L * 0.5f + L * pm_rand(b), 0, size * 0.1f + size * 0.2f * pm_rand(b));
    pm_rock(b, size * (0.04f + 0.07f * pm_rand(b)), 0.7f, 0.3f, pc_mul(strata(variant, 0, b), 0.7f), strata(variant, 3, b));
    pm_pop(b);
  }
}

GEN(gen_dune) {
  pm_vary(b, 0.02f);
  pm_smooth(b, true);
  pcol sand_lo, sand_hi, shade;
  switch (variant) {
  case 1: sand_lo = rgb(0.80f, 0.84f, 0.90f), sand_hi = rgb(0.98f, 0.99f, 1.0f), shade = rgb(0.62f, 0.70f, 0.82f); break; /* snow drift */
  case 2: sand_lo = rgb(0.30f, 0.28f, 0.27f), sand_hi = rgb(0.52f, 0.49f, 0.46f), shade = rgb(0.20f, 0.19f, 0.19f); break; /* ash */
  default: sand_lo = rgb(0.82f, 0.62f, 0.38f), sand_hi = rgb(0.97f, 0.82f, 0.56f), shade = rgb(0.62f, 0.42f, 0.26f); break;
  }
  float H = size, W = size * 4.5f, D = size * 2.6f;
  int nu = lod ? 8 : 20, nv = lod ? 4 : 10;
  float px[32][16], py[32][16], pz[32][16];
  for (int j = 0; j <= nv; j++)
    for (int i = 0; i <= nu; i++) {
      float u = (float)i / (float)nu * 2 - 1, v = (float)j / (float)nv * 2 - 1;
      float ridge = -0.28f * u * u + 0.1f * sinf(u * 2.3f + (float)variant);
      float vv = v - ridge;
      float prof = vv < 0 ? powf(fmaxf(1 + vv * 0.95f, 0), 1.7f) : powf(fmaxf(1 - vv * 1.9f, 0), 1.25f);
      float along = powf(fmaxf(1 - u * u, 0), 0.9f) * (0.8f + 0.2f * cosf(u * 3.1f));
      px[j][i] = u * W * 0.5f, pz[j][i] = v * D * 0.5f;
      py[j][i] = H * prof * along;
    }
  for (int j = 0; j < nv; j++)
    for (int i = 0; i < nu; i++) {
      int ii[4] = { i, i + 1, i + 1, i }, jj[4] = { j, j, j + 1, j + 1 };
      t3_vec3 p[4], n[4];
      pcol c[4];
      for (int k = 0; k < 4; k++) {
        int a = ii[k], bq = jj[k];
        p[k] = P(px[bq][a], py[bq][a], pz[bq][a]);
        int a0 = a > 0 ? a - 1 : a, a1 = a < nu ? a + 1 : a, b0 = bq > 0 ? bq - 1 : bq, b1 = bq < nv ? bq + 1 : bq;
        float dx = (py[bq][a1] - py[bq][a0]) / (px[bq][a1] - px[bq][a0] + 1e-6f);
        float dz = (py[b1][a] - py[b0][a]) / (pz[b1][a] - pz[b0][a] + 1e-6f);
        n[k] = P(-dx, 1, -dz);
        float l = sqrtf(dx * dx + 1 + dz * dz);
        n[k] = P(n[k].x / l, n[k].y / l, n[k].z / l);
        float ripple = 0.5f + 0.5f * sinf(pz[bq][a] * 2.4f + px[bq][a] * 0.35f);
        float lit = cw_clampf(py[bq][a] / H, 0, 1);
        pcol base = pc_mix(sand_lo, sand_hi, lit);
        if (dz > 0.25f) base = pc_mix(base, shade, cw_clampf((dz - 0.25f) * 1.2f, 0, 0.7f)); /* the lee face is in shade */
        c[k] = pc_mul(base, 0.96f + 0.06f * ripple);
      }
      /* the sheet's winding looks up (+y) */
      pm_tri_n(b, p[0], n[0], c[0], p[3], n[3], c[3], p[2], n[2], c[2]);
      pm_tri_n(b, p[0], n[0], c[0], p[2], n[2], c[2], p[1], n[1], c[1]);
    }
}

/* ── desert and dry ──────────────────────────────────────────────────────────────────────────────── */

static void saguaro_arm(pm *b, float x, float y, float rise, float reach, float r, pcol c0, pcol c1) {
  /* out sideways then up, as a pair of rounded limbs */
  float sgn = reach < 0 ? -1.0f : 1.0f;
  pm_limb(b, P(x, y, 0), P(x + reach, y, 0), r, r * 0.95f, 8, c0, c1);
  pm_push(b);
  pm_move(b, x + reach, y, 0);
  pm_cyl_ex(b, r * 0.95f, r * 0.95f, rise, 8, c0, c1, false, false);
  pm_move(b, 0, rise, 0);
  pm_ball(b, r * 0.95f, r * 0.8f, r * 0.95f, 1, 0.04f, c1, pc_mul(c1, 1.1f));
  pm_pop(b);
  (void)sgn;
}

GEN(gen_cactus) {
  pcol g0 = rgb(0.10f, 0.30f, 0.16f), g1 = rgb(0.32f, 0.58f, 0.28f);
  pm_vary(b, 0.05f);
  pm_smooth(b, true);
  int seg = lod ? 6 : 10;
  if (variant == 2) { /* barrel cactus with a crown of flowers */
    float r = size * 0.4f;
    float prof[] = { 0.001f, 0, r * 0.78f, r * 0.12f, r, r * 0.55f, r * 0.82f, r * 1.05f, r * 0.35f, r * 1.22f, 0.001f, r * 1.28f };
    pm_lathe(b, prof, 6, seg, g0, g1);
    if (!lod) {
      pm_layer(b, PL_MAIN);
      for (int i = 0; i < 5; i++) {
        float a = (float)i / 5.0f * CW_TAU;
        pm_push(b);
        pm_move(b, cosf(a) * r * 0.35f, r * 1.2f, sinf(a) * r * 0.35f);
        pm_ball(b, r * 0.14f, r * 0.1f, r * 0.14f, 0, 0.1f, rgb(0.9f, 0.3f, 0.45f), rgb(1.0f, 0.65f, 0.7f));
        pm_pop(b);
      }
    }
    return;
  }
  float h = variant == 1 ? size * 0.7f : size;
  float r = size * 0.075f;
  float prof[] = { r * 1.05f, 0, r * 1.05f, h * 0.9f, r * 0.8f, h * 0.97f, 0.001f, h };
  pm_lathe(b, prof, 4, seg, g0, g1);
  pm_smooth(b, true);
  saguaro_arm(b, 0, h * 0.42f, h * 0.30f, r * 3.6f, r * 0.7f, g0, g1);
  if (variant == 0) saguaro_arm(b, 0, h * 0.58f, h * 0.24f, -r * 3.2f, r * 0.65f, g0, g1);
  /* ribs: thin lighter strips down the trunk (only up close) */
  if (!lod) {
    pm_smooth(b, false);
    for (int i = 0; i < 6; i++) {
      float a = (float)i / 6.0f * CW_TAU;
      pm_beam(b, P(cosf(a) * r * 1.04f, h * 0.05f, sinf(a) * r * 1.04f), P(cosf(a) * r * 1.04f, h * 0.88f, sinf(a) * r * 1.04f), r * 0.1f, pc_mul(g1, 1.15f));
    }
  }
}

/* a twisting branch tree: limbs fork at random, shrinking */
static void limb_tree(pm *b, t3_vec3 base, t3_vec3 dir, float len, float r, int depth, pcol bark0, pcol bark1, int seg, float spread, int forks) {
  t3_vec3 end = P(base.x + dir.x * len, base.y + dir.y * len, base.z + dir.z * len);
  pm_limb(b, base, end, r, r * 0.62f, seg, bark0, bark1);
  if (depth <= 0) return;
  for (int i = 0; i < forks; i++) {
    float a = pm_rand(b) * CW_TAU, t = spread * (0.6f + 0.8f * pm_rand(b));
    t3_vec3 nd = P(dir.x + cosf(a) * t, dir.y * 0.9f + 0.15f + 0.3f * pm_rand(b) * (depth > 1 ? 1.0f : 0.5f), dir.z + sinf(a) * t);
    float l = sqrtf(nd.x * nd.x + nd.y * nd.y + nd.z * nd.z);
    nd = P(nd.x / l, nd.y / l, nd.z / l);
    limb_tree(b, end, nd, len * (0.62f + 0.18f * pm_rand(b)), r * 0.6f, depth - 1, bark0, bark1, seg, spread, forks);
  }
}

GEN(gen_dead_tree) {
  pm_vary(b, 0.07f);
  pcol b0 = rgb(0.17f, 0.13f, 0.10f), b1 = rgb(0.45f, 0.40f, 0.34f);
  if (variant == 2) b0 = rgb(0.10f, 0.09f, 0.09f), b1 = rgb(0.28f, 0.25f, 0.24f); /* burnt */
  if (variant == 3) b0 = rgb(0.30f, 0.27f, 0.24f), b1 = rgb(0.72f, 0.70f, 0.64f);  /* bleached */
  int seg = lod ? 4 : 6;
  /* roots flare */
  pm_cyl_ex(b, size * 0.07f, size * 0.035f, size * 0.12f, seg, pc_mul(b0, 0.7f), b0, false, false);
  float lean = 0.12f * pm_rands(b);
  t3_vec3 d0 = P(lean, 1, lean * 0.7f);
  limb_tree(b, P(0, size * 0.1f, 0), d0, size * 0.42f, size * 0.035f, lod ? 2 : 3, b0, b1, seg, 0.75f, variant == 1 ? 3 : 2);
}

/* ── conifers ────────────────────────────────────────────────────────────────────────────────────── */

static void conifer(pm *b, float size, int variant, int lod, bool snow) {
  pm_vary(b, 0.06f);
  static const unsigned greens[4][2] = { { 0x0e3a24, 0x2f7a3c }, { 0x12402e, 0x3c8a4a }, { 0x1a3b1f, 0x5a8a34 }, { 0x0c2f2b, 0x2b6f5e } };
  pcol g0 = hex(greens[variant & 3][0]), g1 = hex(greens[variant & 3][1]);
  pcol bark0 = rgb(0.16f, 0.1f, 0.07f), bark1 = rgb(0.34f, 0.24f, 0.15f);
  float trunk = size * 0.28f;
  pm_layer(b, PL_MAIN);
  pm_cyl_ex(b, size * 0.034f, size * 0.022f, trunk * 1.5f, lod ? 4 : 6, bark0, bark1, false, false);
  pm_layer(b, PL_LEAF);
  int tiers = lod ? 3 : (variant == 1 ? 7 : variant == 2 ? 4 : 6);
  float fat = variant == 2 ? 1.35f : variant == 1 ? 0.75f : 1.0f;
  float y = trunk * 0.45f, top = size - y;
  int seg = lod ? 5 : 8;
  for (int i = 0; i < tiers; i++) {
    float t = (float)i / (float)tiers;
    float th = top / (float)tiers * 1.75f;
    float r = size * 0.27f * fat * (1.0f - 0.78f * t);
    float off = variant == 3 ? 0.35f * r * pm_rands(b) : 0;
    pm_push(b);
    pm_move(b, off, y, variant == 3 ? 0.35f * r * pm_rands(b) : 0);
    pm_roty(b, pm_rand(b) * 3);
    pm_layer(b, PL_LEAF);
    pm_jitter(b, 0.12f);
    pm_cyl_ex(b, r, 0.001f, th, seg, pc_mix(g0, g1, 0.1f + 0.2f * t), pc_mix(g0, g1, 0.55f + 0.4f * t), false, false);
    if (snow) {
      pm_layer(b, PL_MAIN);
      float sy = th * 0.42f, sr = r * (1 - 0.42f) * 1.06f;
      pm_move(b, 0, sy, 0);
      pm_cyl_ex(b, sr, 0.001f, th - sy + 0.04f * size, seg, rgb(0.78f, 0.84f, 0.92f), rgb(0.98f, 0.99f, 1.0f), false, false);
    }
    pm_jitter(b, 0);
    pm_pop(b);
    y += top / (float)tiers * (0.82f - 0.2f * t);
  }
}
GEN(gen_pine) { conifer(b, size, variant, lod, false); }
GEN(gen_snow_pine) { conifer(b, size, variant, lod, true); }

/* ── palms and broadleaf ─────────────────────────────────────────────────────────────────────────── */

GEN(gen_palm) {
  pm_vary(b, 0.05f);
  pcol t0 = rgb(0.30f, 0.20f, 0.12f), t1 = rgb(0.58f, 0.44f, 0.30f);
  int ns = lod ? 4 : 8;
  float bend = (0.25f + 0.25f * pm_rand(b)) * (variant == 1 ? 1.7f : 1.0f) * (pm_rand(b) < 0.5f ? 1 : -1);
  float x = 0, y = 0, ang = 0;
  float segh = size * 0.9f / (float)ns;
  t3_vec3 top = P(0, 0, 0);
  for (int i = 0; i < ns; i++) {
    float t = (float)i / (float)ns;
    float nx = x + sinf(ang) * segh, ny = y + cosf(ang) * segh;
    pcol band = (i & 1) ? t0 : pc_mul(t0, 1.18f);
    pm_layer(b, PL_MAIN);
    pm_limb(b, P(x, y, 0), P(nx, ny, 0), size * 0.032f * (1 - 0.3f * t), size * 0.032f * (1 - 0.3f * (t + 1.0f / (float)ns)), lod ? 5 : 7, band, pc_mix(band, t1, 0.6f));
    x = nx, y = ny, ang += bend / (float)ns;
  }
  top = P(x, y, 0);
  int fronds = lod ? 7 : (variant == 2 ? 14 : 11);
  pm_layer(b, PL_LEAF);
  pcol f0 = rgb(0.10f, 0.34f, 0.14f), f1 = rgb(0.42f, 0.68f, 0.26f);
  for (int i = 0; i < fronds; i++) {
    float a = (float)i / (float)fronds * CW_TAU + pm_rands(b) * 0.2f;
    float len = size * (0.5f + 0.12f * pm_rand(b)), droop = 0.6f + 0.6f * pm_rand(b);
    t3_vec3 prev = top;
    int steps = lod ? 3 : 5;
    for (int k = 1; k <= steps; k++) {
      float t = (float)k / (float)steps;
      float fx = cosf(a) * len * t, fz = sinf(a) * len * t;
      float fy = len * (0.45f * sinf(t * 2.4f) - droop * 0.5f * t * t);
      t3_vec3 cur = P(top.x + fx, top.y + fy + len * 0.06f, top.z + fz);
      float w0 = len * 0.12f * sinf(CW_PI * (t - 1.0f / (float)steps) * 0.95f + 0.2f), w1 = len * 0.12f * sinf(CW_PI * t * 0.95f + 0.2f);
      if (k == steps) w1 = 0.001f;
      t3_vec3 side0 = P(-sinf(a) * w0, 0.02f * w0, cosf(a) * w0), side1 = P(-sinf(a) * w1, 0.02f * w1, cosf(a) * w1);
      pcol c0 = pc_mix(f0, f1, (float)(k - 1) / (float)steps), c1 = pc_mix(f0, f1, t);
      pm_quad(b, P(prev.x - side0.x, prev.y - side0.y, prev.z - side0.z), P(prev.x + side0.x, prev.y + side0.y, prev.z + side0.z),
              P(cur.x + side1.x, cur.y + side1.y, cur.z + side1.z), P(cur.x - side1.x, cur.y - side1.y, cur.z - side1.z), pc_mix(c0, c1, 0.5f));
      prev = cur;
    }
  }
  if (!lod) {
    pm_layer(b, PL_MAIN);
    for (int i = 0; i < 3; i++) {
      pm_push(b);
      pm_move(b, top.x + cosf((float)i * 2.1f) * size * 0.03f, top.y - size * 0.03f, top.z + sinf((float)i * 2.1f) * size * 0.03f);
      pm_ball(b, size * 0.03f, size * 0.03f, size * 0.03f, 0, 0.1f, rgb(0.25f, 0.16f, 0.08f), rgb(0.5f, 0.36f, 0.2f));
      pm_pop(b);
    }
  }
}

GEN(gen_broadleaf) {
  pm_vary(b, 0.06f);
  static const unsigned tints[4][3] = { { 0x1d4a22, 0x4c8c34, 0x77b04a }, { 0x7a2a12, 0xc2551c, 0xe8a02e }, { 0x123a2a, 0x2d6a46, 0x55935a }, { 0x44401a, 0x7b8030, 0xa9ae44 } };
  pcol l0 = hex(tints[variant & 3][0]), l1 = hex(tints[variant & 3][1]), l2 = hex(tints[variant & 3][2]);
  pcol b0 = rgb(0.17f, 0.11f, 0.07f), b1 = rgb(0.38f, 0.28f, 0.2f);
  pm_layer(b, PL_MAIN);
  float th = size * 0.45f;
  pm_limb(b, P(0, 0, 0), P(0, th, 0), size * 0.05f, size * 0.03f, lod ? 5 : 7, b0, b1);
  int n = lod ? 3 : 6;
  pm_layer(b, PL_MAIN);
  if (!lod)
    for (int i = 0; i < 3; i++) {
      float a = (float)i / 3.0f * CW_TAU + 0.4f;
      pm_limb(b, P(0, th * 0.8f, 0), P(cosf(a) * size * 0.2f, th * 1.25f, sinf(a) * size * 0.2f), size * 0.022f, size * 0.012f, 5, b0, b1);
    }
  pm_layer(b, PL_LEAF);
  for (int i = 0; i < n; i++) {
    float a = (float)i / (float)n * CW_TAU + pm_rand(b), d = i == 0 ? 0 : size * (0.16f + 0.1f * pm_rand(b));
    float y = th + size * (0.18f + 0.22f * pm_rand(b)) + (i == 0 ? size * 0.12f : 0);
    float r = size * (0.2f + 0.1f * pm_rand(b));
    lump(b, cosf(a) * d, y, sinf(a) * d, r, 0.85f, pc_mix(l0, l1, 0.2f), pc_mix(l1, l2, 0.5f + 0.5f * pm_rand(b)), lod ? 0 : 1);
  }
  lump(b, 0, th + size * 0.05f, 0, size * 0.2f, 0.6f, pc_mul(l0, 0.7f), l0, 0); /* a dark underside */
}

GEN(gen_jungle_tree) {
  pm_vary(b, 0.06f);
  pcol b0 = rgb(0.14f, 0.1f, 0.07f), b1 = rgb(0.38f, 0.30f, 0.2f);
  pcol l0 = rgb(0.05f, 0.24f, 0.1f), l1 = rgb(0.18f, 0.5f, 0.2f), l2 = rgb(0.4f, 0.72f, 0.28f);
  if (variant == 1) l0 = rgb(0.04f, 0.2f, 0.18f), l1 = rgb(0.1f, 0.42f, 0.34f), l2 = rgb(0.3f, 0.65f, 0.5f);
  if (variant == 2) l1 = rgb(0.34f, 0.5f, 0.12f), l2 = rgb(0.62f, 0.74f, 0.22f);
  float th = size * 0.62f, r = size * 0.04f;
  pm_layer(b, PL_MAIN);
  pm_smooth(b, false);
  pm_cyl_ex(b, r * 1.5f, r * 0.8f, th, lod ? 6 : 9, b0, b1, false, false);
  /* buttress flanges */
  int fl = lod ? 3 : 5;
  for (int i = 0; i < fl; i++) {
    float a = (float)i / (float)fl * CW_TAU + 0.3f;
    pm_push(b);
    pm_roty(b, a);
    pm_move(b, r * 1.0f, 0, 0);
    pm_wedge(b, r * 0.3f, size * 0.12f, r * 2.2f, pc_mul(b0, 0.8f), b1);
    pm_pop(b);
  }
  pm_cyl_ex(b, r * 0.8f, r * 0.55f, size * 0.5f, 6, b0, b1, false, false);
  pm_layer(b, PL_LEAF);
  int n = lod ? 5 : 11;
  for (int i = 0; i < n; i++) {
    float a = pm_rand(b) * CW_TAU, d = (i == 0 ? 0.0f : 0.25f + 0.5f * pm_rand(b)) * size * 0.42f;
    float y = th + size * (0.0f + 0.2f * pm_rand(b)) - d * 0.12f;
    float rr = size * (0.17f + 0.1f * pm_rand(b));
    pcol a0 = pc_mix(l0, l1, pm_rand(b)), a1 = pc_mix(l1, l2, pm_rand(b));
    pm_push(b);
    pm_move(b, cosf(a) * d, y, sinf(a) * d);
    pm_ball(b, rr * 1.3f, rr * 0.55f, rr * 1.3f, lod ? 0 : 1, 0.25f, a0, a1);
    pm_pop(b);
  }
  /* hanging vines with a few leaf drops */
  int vines = lod ? 0 : 9;
  for (int i = 0; i < vines; i++) {
    float a = pm_rand(b) * CW_TAU, d = size * (0.18f + 0.22f * pm_rand(b));
    float x = cosf(a) * d, z = sinf(a) * d, y = th + size * 0.04f - d * 0.1f;
    float len = size * (0.18f + 0.3f * pm_rand(b));
    pm_layer(b, PL_MAIN);
    t3_vec3 prev = P(x, y, z);
    for (int k = 1; k <= 4; k++) {
      t3_vec3 cur = P(x + 0.4f * sinf((float)k + (float)i), y - len * (float)k / 4.0f, z + 0.3f * cosf((float)k * 1.3f + (float)i));
      pm_rod(b, prev, cur, size * 0.006f, 4, rgb(0.12f, 0.3f, 0.12f));
      prev = cur;
    }
    pm_layer(b, PL_LEAF);
    pm_push(b);
    pm_move(b, prev.x, prev.y, prev.z);
    pm_ball(b, size * 0.016f, size * 0.03f, size * 0.016f, 0, 0.2f, l1, l2);
    pm_pop(b);
  }
}

GEN(gen_fern) {
  pm_layer(b, PL_LEAF);
  pm_vary(b, 0.07f);
  pcol f0 = rgb(0.06f, 0.28f, 0.1f), f1 = rgb(0.36f, 0.66f, 0.24f);
  if (variant == 1) f0 = rgb(0.1f, 0.3f, 0.14f), f1 = rgb(0.58f, 0.76f, 0.3f);
  if (variant == 2) f0 = rgb(0.05f, 0.22f, 0.2f), f1 = rgb(0.22f, 0.56f, 0.46f);
  int fronds = lod ? 7 : 12;
  for (int i = 0; i < fronds; i++) {
    float a = (float)i / (float)fronds * CW_TAU + pm_rands(b) * 0.2f;
    float len = size * (0.7f + 0.4f * pm_rand(b)), up = 0.6f + 0.7f * pm_rand(b);
    int steps = lod ? 3 : 7;
    t3_vec3 prev = P(0, size * 0.04f, 0);
    float ca = cosf(a), sa = sinf(a);
    for (int k = 1; k <= steps; k++) {
      float t = (float)k / (float)steps;
      t3_vec3 cur = P(ca * len * t, size * 0.04f + len * (up * 0.9f * t - 0.9f * t * t * (0.5f + 0.5f * up)), sa * len * t);
      float w = len * 0.2f * sinf(CW_PI * (0.12f + 0.88f * (1 - t))) * (1 - 0.3f * t);
      t3_vec3 side = P(-sa * w, -0.15f * w, ca * w);
      pcol c = pc_mix(f0, f1, t);
      if (!lod) { /* serrated leaflets: pairs of small triangles on the spine */
        pm_tri(b, prev, P(prev.x + side.x, prev.y + side.y, prev.z + side.z), cur, c);
        pm_tri(b, prev, cur, P(prev.x - side.x, prev.y - side.y, prev.z - side.z), c);
      } else {
        pm_tri(b, P(prev.x - side.x, prev.y - side.y, prev.z - side.z), P(prev.x + side.x, prev.y + side.y, prev.z + side.z), cur, c);
      }
      prev = cur;
    }
  }
}

GEN(gen_mushroom) {
  pm_vary(b, 0.05f);
  pcol stem0 = rgb(0.72f, 0.66f, 0.55f), stem1 = rgb(0.95f, 0.92f, 0.84f);
  int count = variant == 2 ? (lod ? 2 : 4) : 1;
  for (int m = 0; m < count; m++) {
    pm_push(b);
    float s = m == 0 ? 1.0f : 0.35f + 0.3f * pm_rand(b);
    if (m > 0) pm_move(b, pm_rands(b) * size * 0.45f, 0, pm_rands(b) * size * 0.45f);
    float H = size * s * (variant == 1 ? 1.2f : 0.85f), r = size * s * (variant == 1 ? 0.3f : 0.46f);
    pm_layer(b, PL_MAIN);
    pm_smooth(b, true);
    float sp[] = { H * 0.1f, 0, H * 0.07f, H * 0.5f, H * 0.06f, H * 0.82f };
    pm_lathe(b, sp, 3, lod ? 6 : 9, stem0, stem1);
    pcol cap0, cap1;
    if (variant == 0) cap0 = rgb(0.45f, 0.04f, 0.04f), cap1 = rgb(0.92f, 0.16f, 0.1f);
    else if (variant == 1) cap0 = rgb(0.6f, 0.3f, 0.06f), cap1 = rgb(0.98f, 0.66f, 0.18f);
    else cap0 = rgb(0.06f, 0.25f, 0.35f), cap1 = rgb(0.2f, 0.62f, 0.78f);
    pm_push(b);
    pm_move(b, 0, H * 0.78f, 0);
    float cp[] = { 0.001f, H * 0.5f, r * 0.4f, H * 0.46f, r * 0.8f, H * 0.34f, r, H * 0.1f, r * 0.96f, 0, r * 0.7f, -H * 0.06f };
    float flip[12];
    for (int i = 0; i < 6; i++) flip[i * 2] = cp[(5 - i) * 2], flip[i * 2 + 1] = cp[(5 - i) * 2 + 1];
    pm_layer(b, PL_LEAF); /* two-sided: the underside shows from a low kart camera */
    pm_lathe(b, flip, 6, lod ? 7 : 11, cap0, cap1);
    pm_smooth(b, false);
    if (variant == 0 && !lod) {
      pm_layer(b, PL_MAIN);
      for (int i = 0; i < 7; i++) {
        float a = pm_rand(b) * CW_TAU, rr = r * (0.2f + 0.55f * pm_rand(b));
        float yy = H * 0.5f * (1 - (rr / r) * (rr / r) * 0.9f) * 0.98f;
        pm_push(b);
        pm_move(b, cosf(a) * rr, yy, sinf(a) * rr);
        pm_ball(b, r * 0.09f, r * 0.04f, r * 0.09f, 0, 0.1f, rgb(0.9f, 0.88f, 0.8f), rgb(1, 1, 0.96f));
        pm_pop(b);
      }
    }
    if (variant == 2) { /* a glowing rim */
      pm_layer(b, PL_GLOW);
      for (int i = 0; i < 8; i++) {
        float a = (float)i / 8.0f * CW_TAU;
        pm_push(b);
        pm_move(b, cosf(a) * r * 0.9f, H * 0.08f, sinf(a) * r * 0.9f);
        pm_ball(b, r * 0.07f, r * 0.04f, r * 0.07f, 0, 0.0f, glow(rgb(0.25f, 0.9f, 0.85f), 2.2f), glow(rgb(0.5f, 1, 0.95f), 3.2f));
        pm_pop(b);
      }
    }
    pm_pop(b);
    pm_pop(b);
  }
}

/* ── ice and crystal ─────────────────────────────────────────────────────────────────────────────── */

GEN(gen_ice_spikes) {
  pm_vary(b, 0.05f);
  int n = lod ? 4 : 8;
  pcol c0 = rgb(0.22f, 0.5f, 0.78f), c1 = rgb(0.9f, 0.97f, 1.0f);
  if (variant == 1) c0 = rgb(0.3f, 0.45f, 0.7f);
  if (variant == 2) c0 = rgb(0.18f, 0.6f, 0.7f);
  for (int i = 0; i < n; i++) {
    float a = (float)i / (float)n * CW_TAU + pm_rands(b) * 0.4f;
    float d = i == 0 ? 0 : size * (0.08f + 0.2f * pm_rand(b));
    float h = size * (i == 0 ? 1.0f : 0.4f + 0.45f * pm_rand(b)), r = size * (i == 0 ? 0.11f : 0.04f + 0.05f * pm_rand(b));
    float lean = i == 0 ? 0.05f : 0.2f + 0.3f * pm_rand(b);
    pm_push(b);
    pm_move(b, cosf(a) * d, -r, sinf(a) * d);
    pm_roty(b, -a);
    pm_rotz(b, -lean);
    pm_cyl_ex(b, r, 0.001f, h + r, lod ? 4 : 5, c0, c1, false, false);
    pm_pop(b);
  }
  pm_rock(b, size * 0.14f, 0.5f, 0.2f, rgb(0.55f, 0.7f, 0.85f), rgb(0.95f, 0.98f, 1.0f));
}

GEN(gen_ice_boulder) {
  pm_vary(b, 0.1f);
  pcol lo = rgb(0.25f, 0.5f, 0.75f), hi = rgb(0.85f, 0.95f, 1.0f);
  if (variant == 1) lo = rgb(0.4f, 0.58f, 0.8f);
  float r = size * 0.55f;
  pm_rock(b, r, 0.85f, 0.2f, lo, hi);
  pm_push(b);
  pm_move(b, r * 0.1f, r * 0.62f, 0);
  pm_ball(b, r * 0.7f, r * 0.22f, r * 0.7f, lod ? 0 : 1, 0.15f, rgb(0.85f, 0.92f, 0.98f), rgb(1, 1, 1)); /* a frost cap */
  pm_pop(b);
  if (!lod && variant == 2) {
    pm_push(b);
    pm_move(b, r * 0.9f, 0, r * 0.2f);
    pm_rock(b, r * 0.5f, 1.2f, 0.2f, lo, hi);
    pm_pop(b);
  }
}

GEN(gen_crystal) {
  static const unsigned hues[4][2] = { { 0x0a5a7a, 0x5af0ff }, { 0x6a1070, 0xff5ae6 }, { 0x7a4a08, 0xffc04a }, { 0x0a6a3a, 0x6aff9a } };
  pcol d = hex(hues[variant & 3][0]), l = hex(hues[variant & 3][1]);
  pm_vary(b, 0.04f);
  pm_layer(b, PL_MAIN);
  pm_rock(b, size * 0.2f, 0.45f, 0.25f, rgb(0.14f, 0.13f, 0.16f), rgb(0.34f, 0.32f, 0.38f));
  int n = lod ? 4 : 7;
  for (int i = 0; i < n; i++) {
    float a = (float)i / (float)n * CW_TAU + pm_rands(b) * 0.3f, dd = i == 0 ? 0 : size * (0.1f + 0.1f * pm_rand(b));
    float h = size * (i == 0 ? 1.0f : 0.35f + 0.5f * pm_rand(b)), r = size * (i == 0 ? 0.1f : 0.05f + 0.04f * pm_rand(b));
    float lean = i == 0 ? 0.05f : 0.25f + 0.35f * pm_rand(b);
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, cosf(a) * dd, 0, sinf(a) * dd);
    pm_roty(b, -a);
    pm_rotz(b, -lean);
    pm_cyl_ex(b, r, r * 0.9f, h * 0.7f, 6, glow(d, 0.7f), glow(pc_mix(d, l, 0.6f), 1.5f), false, false);
    pm_move(b, 0, h * 0.7f, 0);
    pm_cyl_ex(b, r * 0.9f, 0.001f, h * 0.3f, 6, glow(pc_mix(d, l, 0.6f), 1.5f), glow(l, 2.8f), false, false);
    pm_pop(b);
  }
}

GEN(gen_grass) {
  pm_layer(b, PL_LEAF);
  pm_vary(b, 0.08f);
  pcol g0 = rgb(0.15f, 0.34f, 0.12f), g1 = rgb(0.58f, 0.74f, 0.28f);
  if (variant == 1) g0 = rgb(0.4f, 0.32f, 0.14f), g1 = rgb(0.86f, 0.74f, 0.4f); /* dry */
  if (variant == 2) g0 = rgb(0.2f, 0.3f, 0.1f), g1 = rgb(0.5f, 0.45f, 0.2f);    /* reeds */
  int n = lod ? 7 : 16;
  for (int i = 0; i < n; i++) {
    float a = pm_rand(b) * CW_TAU, d = size * 0.18f * pm_rand(b);
    float h = size * (0.55f + 0.6f * pm_rand(b)), w = size * 0.04f, bend = size * (0.1f + 0.25f * pm_rand(b));
    float ca = cosf(a), sa = sinf(a);
    t3_vec3 p0 = P(ca * d, 0, sa * d), p1 = P(ca * (d + bend * 0.35f), h * 0.6f, sa * (d + bend * 0.35f)), p2 = P(ca * (d + bend), h, sa * (d + bend));
    t3_vec3 s = P(-sa * w, 0, ca * w);
    pm_quad(b, P(p0.x - s.x, 0, p0.z - s.z), P(p0.x + s.x, 0, p0.z + s.z), P(p1.x + s.x * 0.6f, p1.y, p1.z + s.z * 0.6f), P(p1.x - s.x * 0.6f, p1.y, p1.z - s.z * 0.6f), pc_mix(g0, g1, 0.3f));
    pm_tri(b, P(p1.x - s.x * 0.6f, p1.y, p1.z - s.z * 0.6f), P(p1.x + s.x * 0.6f, p1.y, p1.z + s.z * 0.6f), p2, g1);
  }
}

GEN(gen_bush) {
  pm_layer(b, PL_LEAF);
  pm_vary(b, 0.07f);
  pcol l0 = rgb(0.07f, 0.26f, 0.12f), l1 = rgb(0.32f, 0.58f, 0.24f);
  if (variant == 1) l0 = rgb(0.4f, 0.14f, 0.1f), l1 = rgb(0.72f, 0.3f, 0.18f);
  if (variant == 2) l0 = rgb(0.2f, 0.26f, 0.1f), l1 = rgb(0.6f, 0.62f, 0.3f);
  int n = lod ? 3 : 7;
  for (int i = 0; i < n; i++) {
    float a = (float)i / (float)n * CW_TAU + pm_rand(b), d = i == 0 ? 0 : size * (0.28f + 0.2f * pm_rand(b));
    float r = size * (i == 0 ? 0.36f : 0.2f + 0.1f * pm_rand(b));
    lump(b, cosf(a) * d, r * 0.75f + size * 0.04f * pm_rand(b), sinf(a) * d, r, 0.8f, pc_mul(l0, 0.8f + 0.3f * pm_rand(b)), pc_mix(l0, l1, 0.3f + 0.7f * pm_rand(b)), 0);
  }
}
