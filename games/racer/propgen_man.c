/* racer's made things: drums, crates, containers, wrecks, industry, power, signs, towers, fairground, temple
 * stone, space hardware. Same conventions as propgen_nature.c: base on y = 0, +z the face a sign shows, faceted
 * with vertex colours, PL_GLOW for anything that lights up. */
#include <math.h>
#include <stdlib.h>

#include "cartwheel/math.h"
#include "props_gen.h"

static t3_vec3 P(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static pcol dk(pcol c, float k) { return pc_mul(c, k); }

/* a lattice mast: four corner legs from width w0 at the base to w1 at the top, a cross brace per bay */
static void lattice(pm *b, float w0, float w1, float h, int bays, float rod, pcol c) {
  float x0 = w0 * 0.5f, x1 = w1 * 0.5f;
  for (int s = 0; s < 4; s++) {
    float sx = (s & 1) ? 1.0f : -1.0f, sz = (s & 2) ? 1.0f : -1.0f;
    pm_beam(b, P(sx * x0, 0, sz * x0), P(sx * x1, h, sz * x1), rod * 1.4f, c);
  }
  for (int i = 0; i < bays; i++) {
    float t0 = (float)i / (float)bays, t1 = (float)(i + 1) / (float)bays;
    float a = x0 + (x1 - x0) * t0, bb = x0 + (x1 - x0) * t1;
    float y0 = h * t0, y1 = h * t1;
    for (int f = 0; f < 4; f++) {
      float ca = (f == 0) ? 1 : (f == 1) ? -1 : 0, sa = (f == 2) ? 1 : (f == 3) ? -1 : 0;
      /* face f: the plane perpendicular to (ca, sa); u runs along the face */
      float ux = -sa, uz = ca;
      t3_vec3 p00 = P(ca * a - ux * a, y0, sa * a - uz * a), p10 = P(ca * a + ux * a, y0, sa * a + uz * a);
      t3_vec3 p01 = P(ca * bb - ux * bb, y1, sa * bb - uz * bb), p11 = P(ca * bb + ux * bb, y1, sa * bb + uz * bb);
      pm_beam(b, p00, p11, rod, c);
      if (bays <= 14) pm_beam(b, p10, p01, rod, c);
      pm_beam(b, p01, p11, rod, c);
    }
  }
}

/* ── drums, crates, containers, tyres ────────────────────────────────────────────────────────────── */

GEN(gen_barrel) {
  static const unsigned cols[5] = { 0xb8281c, 0x1e5aa8, 0xe0b020, 0x8a5430, 0xb8281c };
  pcol c = hex(cols[variant]);
  pm_vary(b, 0.05f);
  float r = size * 0.29f, h = size;
  pm_push(b);
  if (variant == 4) { /* toppled, lying on its side */
    pm_move(b, 0, r, 0);
    pm_rotz(b, CW_PI * 0.5f);
    pm_move(b, 0, -h * 0.5f, 0);
    pm_roty(b, 0.5f);
  }
  pm_smooth(b, true);
  int seg = lod ? 8 : 14;
  float prof[] = { r * 0.93f, 0, r, h * 0.04f, r, h * 0.3f, r * 1.0f, h * 0.34f, r, h * 0.66f, r * 1.0f, h * 0.7f, r, h * 0.96f, r * 0.93f, h };
  pcol low = variant == 3 ? rgb(0.24f, 0.13f, 0.07f) : dk(c, 0.55f);
  pm_lathe(b, prof, 8, seg, low, c);
  pm_layer(b, PL_METAL);
  float prof2[] = { r * 0.94f, h * 0.0f, r * 1.04f, h * 0.02f, r * 1.04f, h * 0.06f, r * 0.94f, h * 0.08f };
  pm_lathe(b, prof2, 4, seg, rgb(0.5f, 0.5f, 0.52f), rgb(0.7f, 0.7f, 0.72f));
  pm_push(b);
  pm_move(b, 0, h * 0.92f, 0);
  pm_lathe(b, prof2, 4, seg, rgb(0.5f, 0.5f, 0.52f), rgb(0.7f, 0.7f, 0.72f));
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, h * 0.34f, 0);
  pm_lathe(b, prof2, 4, seg, dk(c, 0.7f), c); /* the ribs */
  pm_move(b, 0, h * 0.32f, 0);
  pm_lathe(b, prof2, 4, seg, dk(c, 0.7f), c);
  pm_pop(b);
  pm_layer(b, PL_MAIN);
  pm_smooth(b, false);
  pm_push(b);
  pm_move(b, r * 0.4f, h * 0.995f, 0);
  pm_cyl(b, r * 0.12f, r * 0.12f, h * 0.025f, 6, rgb(0.15f, 0.15f, 0.16f), rgb(0.3f, 0.3f, 0.32f));
  pm_pop(b);
  if (variant == 2 && !lod) { /* hazard label */
    pm_quad(b, P(-r * 0.4f, h * 0.38f, r * 1.01f), P(r * 0.4f, h * 0.38f, r * 1.01f), P(r * 0.4f, h * 0.6f, r * 1.01f), P(-r * 0.4f, h * 0.6f, r * 1.01f), rgb(0.08f, 0.08f, 0.08f));
    pm_tri(b, P(-r * 0.22f, h * 0.42f, r * 1.02f), P(r * 0.22f, h * 0.42f, r * 1.02f), P(0, h * 0.56f, r * 1.02f), rgb(0.95f, 0.8f, 0.1f));
  }
  if (variant == 3 && !lod) { /* rust runs */
    for (int i = 0; i < 4; i++) {
      float a = pm_rand(b) * CW_TAU;
      pm_push(b);
      pm_roty(b, a);
      pm_quad(b, P(-r * 0.12f, h * 0.8f, r * 1.005f), P(r * 0.12f, h * 0.8f, r * 1.005f), P(r * 0.05f, h * (0.3f + 0.3f * pm_rand(b)), r * 1.005f), P(-r * 0.05f, h * 0.35f, r * 1.005f), rgb(0.55f, 0.24f, 0.08f));
      pm_pop(b);
    }
  }
  pm_pop(b);
}

GEN(gen_crate) {
  pm_vary(b, 0.06f);
  pcol w0 = rgb(0.35f, 0.22f, 0.11f), w1 = rgb(0.68f, 0.5f, 0.3f), trim = rgb(0.22f, 0.14f, 0.08f);
  if (variant == 3) w0 = rgb(0.12f, 0.2f, 0.12f), w1 = rgb(0.32f, 0.44f, 0.28f), trim = rgb(0.08f, 0.12f, 0.08f);
  int n = variant == 1 ? 3 : variant == 2 ? 4 : 1;
  float s = size;
  for (int i = 0; i < n; i++) {
    float x = variant == 1 ? (i == 2 ? 0.0f : (i ? 0.55f : -0.55f)) * s : variant == 2 ? ((i & 1) ? 0.6f : -0.6f) * s : 0;
    float z = variant == 2 ? (i > 1 ? 0.6f : -0.6f) * s : 0;
    float y = variant == 1 && i == 2 ? s : 0;
    pm_push(b);
    pm_move(b, x, y, z);
    pm_roty(b, variant == 0 ? 0 : pm_rands(b) * 0.2f);
    if (variant == 2) { /* a pallet of wrapped boxes */
      pm_box(b, s * 1.1f, s * 0.14f, s * 1.1f, dk(w0, 0.8f), w0);
      pm_box_c(b, s * 1.0f, s * 0.9f, s * 1.0f, rgb(0.6f, 0.46f, 0.3f));
      pm_move(b, 0, s * 0.14f, 0);
      pm_box(b, s * 1.0f, s * 0.9f, s * 1.0f, rgb(0.55f, 0.4f, 0.24f), rgb(0.78f, 0.62f, 0.4f));
      pm_quad(b, P(-s * 0.5f, s * 0.2f, s * 0.501f), P(s * 0.5f, s * 0.2f, s * 0.501f), P(s * 0.5f, s * 0.3f, s * 0.501f), P(-s * 0.5f, s * 0.3f, s * 0.501f), rgb(0.85f, 0.85f, 0.8f));
    } else {
      pm_box(b, s, s, s, w0, w1);
      /* corner posts and cross braces */
      float t = s * 0.1f;
      for (int k = 0; k < 4; k++) {
        float sx = (k & 1) ? 1.0f : -1.0f, sz = (k & 2) ? 1.0f : -1.0f;
        pm_push(b);
        pm_move(b, sx * s * 0.5f, 0, sz * s * 0.5f);
        pm_box(b, t, s * 1.01f, t, dk(trim, 0.9f), trim);
        pm_pop(b);
      }
      if (!lod) {
        for (int f = 0; f < 2; f++) {
          pm_push(b);
          pm_roty(b, (float)f * CW_PI * 0.5f);
          pm_beam(b, P(-s * 0.45f, s * 0.1f, s * 0.505f), P(s * 0.45f, s * 0.9f, s * 0.505f), t * 0.6f, trim);
          pm_beam(b, P(-s * 0.45f, s * 0.9f, s * 0.505f), P(s * 0.45f, s * 0.1f, s * 0.505f), t * 0.6f, trim);
          pm_pop(b);
        }
      }
    }
    pm_pop(b);
  }
}

GEN(gen_container) {
  static const unsigned cols[4] = { 0xb04a2a, 0x2f5f9a, 0x3c7a4e, 0xd08a2a };
  float h = size, w = h * 0.94f, l = h * 4.6f;
  int stack = variant == 0 ? 1 : variant == 1 ? 2 : variant == 2 ? 3 : 2;
  pm_vary(b, 0.05f);
  for (int k = 0; k < stack; k++) {
    for (int side = 0; side < ((variant == 3 && k == 0) ? 2 : 1); side++) {
      pcol c = hex(cols[(k * 3 + variant + side * 2) & 3]);
      c = pc_mul(c, 0.82f + 0.2f * pm_rand(b));
      pm_push(b);
      pm_move(b, (variant == 3 && k == 0) ? (side ? 1 : -1) * (w * 0.52f) : 0, h * (float)k, 0);
      if (k > 0) pm_move(b, pm_rands(b) * h * 0.05f, 0, pm_rands(b) * h * 0.3f), pm_roty(b, pm_rands(b) * 0.03f);
      pm_layer(b, PL_METAL);
      pm_box(b, w, h, l, dk(c, 0.55f), c);
      int ribs = lod ? 0 : 26;
      for (int i = 0; i < ribs; i++) {
        float z = -l * 0.5f + l * ((float)i + 0.5f) / (float)ribs;
        pm_push(b);
        pm_move(b, 0, h * 0.05f, z);
        pm_box(b, w * 1.035f, h * 0.9f, l / (float)ribs * 0.5f, dk(c, 0.5f), dk(c, 0.92f));
        pm_pop(b);
      }
      pm_pop(b);
    }
  }
}

GEN(gen_tyre) {
  pm_vary(b, 0.04f);
  pm_push(b);
  float R = size * 0.34f, r = size * 0.19f;
  pm_move(b, 0, r, 0);
  pm_rotx(b, CW_PI * 0.5f);
  pm_ring(b, R, r, lod ? 8 : 14, lod ? 5 : 7, rgb(0.07f, 0.07f, 0.075f));
  pm_pop(b);
}

GEN(gen_tyre_stack) {
  pm_vary(b, 0.04f);
  float R = 0.34f * 0.55f * size / 0.55f * 0.55f, r = R * 0.55f;
  (void)R;
  float tr = size * 0.2f; /* a tyre's tube radius */
  float tR = tr * 1.75f;
  int n = variant == 1 ? 6 : variant == 2 ? 3 : 5;
  if (variant == 1) { /* a pile: a pyramid of three rows */
    int row[3] = { 3, 2, 1 };
    for (int j = 0; j < 3; j++)
      for (int i = 0; i < row[j]; i++) {
        pm_push(b);
        pm_move(b, (i - (row[j] - 1) * 0.5f) * tR * 2.0f, tr + j * tr * 1.9f, 0);
        pm_rotx(b, CW_PI * 0.5f * 0 + CW_PI * 0.5f);
        pm_roty(b, 0);
        pm_ring(b, tR, tr, lod ? 8 : 12, lod ? 4 : 6, rgb(0.06f, 0.06f, 0.065f));
        pm_pop(b);
      }
    (void)n;
    return;
  }
  int cols = variant == 2 ? 3 : 1;
  for (int c = 0; c < cols; c++) {
    int hgt = variant == 2 ? 2 + c : n;
    for (int i = 0; i < hgt; i++) {
      pm_push(b);
      pm_move(b, (c - (cols - 1) * 0.5f) * tR * 2.1f + pm_rands(b) * tr * 0.2f, tr + i * tr * 1.85f, pm_rands(b) * tr * 0.2f);
      pm_rotx(b, CW_PI * 0.5f);
      pm_ring(b, tR, tr, lod ? 8 : 12, lod ? 4 : 6, i & 1 ? rgb(0.07f, 0.07f, 0.075f) : rgb(0.09f, 0.085f, 0.085f));
      pm_pop(b);
    }
  }
  (void)r;
}

/* ── wrecks ──────────────────────────────────────────────────────────────────────────────────────── */

static void wheel(pm *b, float x, float y, float z, float R, float r, bool lod) {
  pm_push(b);
  pm_move(b, x, y, z);
  pm_roty(b, CW_PI * 0.5f);
  pm_layer(b, PL_MAIN);
  pm_ring(b, R, r, lod ? 8 : 12, lod ? 4 : 6, rgb(0.06f, 0.06f, 0.065f));
  pm_layer(b, PL_METAL);
  pm_cyl(b, R * 0.62f, R * 0.62f, r * 0.8f, lod ? 6 : 8, rgb(0.4f, 0.36f, 0.32f), rgb(0.55f, 0.5f, 0.46f));
  pm_pop(b);
}

GEN(gen_wreck_car) {
  static const unsigned cols[4] = { 0x8a4a2a, 0x151414, 0x5a7a74, 0x9a8a5a };
  pcol c = hex(cols[variant]);
  pm_vary(b, 0.08f);
  float s = size, W = s * 1.15f, L = s * 2.6f;
  pm_push(b);
  float roll = (variant == 2 ? 0.0f : 0.1f) * (pm_rand(b) < 0.5f ? 1 : -1);
  if (variant == 2) { /* flipped on its roof */
    pm_move(b, 0, s * 0.95f, 0);
    pm_rotz(b, CW_PI);
    pm_rotz(b, 0.06f);
  } else {
    pm_rotz(b, roll);
    pm_rotx(b, 0.04f);
    pm_move(b, 0, s * 0.05f, 0);
  }
  float body_h = s * 0.42f;
  pm_layer(b, PL_METAL);
  pcol rust = variant == 1 ? rgb(0.05f, 0.045f, 0.045f) : pc_mix(c, rgb(0.4f, 0.17f, 0.06f), 0.4f);
  if (variant == 3) { /* a van: tall box */
    pm_move(b, 0, s * 0.3f, 0);
    pm_box(b, W, s * 0.9f, L * 0.95f, dk(c, 0.55f), c);
    pm_box(b, W * 0.96f, s * 0.15f, L * 0.9f, rust, pc_mix(c, rust, 0.5f));
    pm_move(b, 0, s * 0.9f, 0);
    pm_box(b, W * 0.98f, s * 0.04f, L * 0.9f, dk(rust, 0.8f), rust);
    pm_move(b, 0, -s * 0.9f, 0);
    pm_layer(b, PL_MAIN);
    pm_quad(b, P(-W * 0.5f - 0.01f, s * 0.55f, L * 0.1f), P(-W * 0.5f - 0.01f, s * 0.55f, -L * 0.3f), P(-W * 0.5f - 0.01f, s * 0.8f, -L * 0.3f), P(-W * 0.5f - 0.01f, s * 0.8f, L * 0.1f), rgb(0.03f, 0.04f, 0.05f));
  } else {
    pm_move(b, 0, s * 0.18f, 0);
    pm_box(b, W, body_h, L, dk(c, 0.5f), pc_mix(c, rust, 0.15f)); /* lower body */
    pm_push(b);
    pm_move(b, 0, body_h, -L * 0.04f);
    pm_box(b, W * 0.84f, s * 0.38f, L * 0.5f, dk(c, 0.7f), c); /* cabin */
    pm_layer(b, PL_MAIN);
    pcol gl = rgb(0.03f, 0.05f, 0.06f);
    float cw = W * 0.84f * 0.5f + 0.012f, ch0 = s * 0.08f, ch1 = s * 0.31f, cl = L * 0.23f;
    for (int sd = -1; sd <= 1; sd += 2) {
      pm_quad(b, P(sd * cw, ch0, sd * cl * 0.9f), P(sd * cw, ch0, -sd * cl * 0.9f), P(sd * cw, ch1, -sd * cl * 0.7f), P(sd * cw, ch1, sd * cl * 0.7f), gl);
      (void)sd;
    }
    pm_quad(b, P(-W * 0.35f, ch0, L * 0.25f + 0.01f), P(W * 0.35f, ch0, L * 0.25f + 0.01f), P(W * 0.33f, ch1, L * 0.25f - 0.12f), P(-W * 0.33f, ch1, L * 0.25f - 0.12f), gl);
    pm_layer(b, PL_METAL);
    pm_pop(b);
    /* hood and boot, one dented up */
    pm_push(b);
    pm_move(b, 0, body_h, L * 0.32f);
    pm_rotx(b, -0.12f);
    pm_box(b, W * 0.96f, s * 0.05f, L * 0.3f, dk(rust, 0.8f), rust);
    pm_pop(b);
    pm_push(b);
    pm_move(b, 0, body_h, -L * 0.36f);
    pm_rotx(b, variant == 1 ? 0.5f : 0.05f);
    pm_box(b, W * 0.96f, s * 0.05f, L * 0.22f, dk(rust, 0.8f), rust);
    pm_pop(b);
    /* bumpers */
    pm_box_c(b, W * 1.04f, s * 0.07f, s * 0.08f, rgb(0.5f, 0.5f, 0.52f));
  }
  pm_pop(b);
  float wr = s * 0.24f, wt = s * 0.1f;
  pm_push(b);
  if (variant != 2) pm_rotz(b, roll);
  else pm_move(b, 0, s * 0.95f, 0), pm_rotz(b, CW_PI + 0.06f);
  for (int i = 0; i < 4; i++) {
    if (pm_rand(b) < 0.2f && variant != 0) continue; /* stripped */
    wheel(b, (i & 1 ? 1 : -1) * W * 0.5f, wr, (i & 2 ? 1 : -1) * L * 0.31f, wr, wt, lod);
  }
  pm_pop(b);
}

GEN(gen_wreck_truck) {
  pm_vary(b, 0.08f);
  float s = size;
  pcol c = variant ? rgb(0.45f, 0.5f, 0.55f) : rgb(0.55f, 0.28f, 0.14f);
  pm_push(b);
  pm_rotz(b, 0.05f);
  pm_layer(b, PL_METAL);
  /* chassis, cab and bed */
  pm_move(b, 0, s * 0.3f, 0);
  pm_box(b, s * 0.9f, s * 0.2f, s * 5.2f, rgb(0.07f, 0.07f, 0.08f), rgb(0.14f, 0.14f, 0.15f));
  pm_push(b);
  pm_move(b, 0, s * 0.2f, s * 1.8f);
  pm_box(b, s * 0.95f, s * 0.75f, s * 0.9f, dk(c, 0.5f), c);
  pm_move(b, 0, s * 0.75f, 0);
  pm_box(b, s * 0.9f, s * 0.7f, s * 0.8f, dk(c, 0.7f), c);
  pm_layer(b, PL_MAIN);
  pm_quad(b, P(-s * 0.4f, s * 0.15f, s * 0.401f), P(s * 0.4f, s * 0.15f, s * 0.401f), P(s * 0.38f, s * 0.55f, s * 0.401f), P(-s * 0.38f, s * 0.55f, s * 0.401f), rgb(0.03f, 0.05f, 0.06f));
  pm_layer(b, PL_METAL);
  pm_pop(b);
  if (variant == 0) { /* flat bed with a tarp and a leaning crate */
    pm_push(b);
    pm_move(b, 0, s * 0.2f, -s * 0.6f);
    pm_box(b, s * 1.0f, s * 0.12f, s * 3.2f, rgb(0.2f, 0.14f, 0.1f), rgb(0.35f, 0.26f, 0.18f));
    pm_move(b, 0, s * 0.12f, 0);
    pm_layer(b, PL_MAIN);
    pm_ball(b, s * 0.5f, s * 0.3f, s * 1.4f, 1, 0.14f, rgb(0.14f, 0.2f, 0.12f), rgb(0.3f, 0.4f, 0.24f));
    pm_pop(b);
  } else { /* tanker */
    pm_push(b);
    pm_move(b, 0, s * 0.6f, -s * 0.5f);
    pm_rotx(b, CW_PI * 0.5f);
    pm_move(b, 0, -s * 1.5f, 0);
    pm_cyl_ex(b, s * 0.5f, s * 0.5f, s * 3.0f, 12, dk(c, 0.7f), c, true, true);
    pm_pop(b);
  }
  pm_pop(b);
  pm_push(b);
  pm_rotz(b, 0.05f);
  for (int i = 0; i < 6; i++) {
    if (i == 4 && variant == 0) continue;
    float z = i < 2 ? s * 1.8f : (i < 4 ? -s * 0.9f : -s * 1.7f);
    wheel(b, (i & 1 ? 1 : -1) * s * 0.5f, s * 0.3f, z, s * 0.3f, s * 0.12f, lod);
  }
  pm_pop(b);
}

/* ── pipes and tanks ─────────────────────────────────────────────────────────────────────────────── */

GEN(gen_pipe) {
  static const unsigned cols[3] = { 0x6a7e8c, 0xd0a020, 0x8a4a28 };
  pcol c = hex(cols[variant]);
  pm_vary(b, 0.04f);
  pm_smooth(b, true);
  float r = size * 0.22f, L = size * 3.5f, y = size * 0.5f;
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, -L * 0.5f, y, 0);
  pm_rotz(b, -CW_PI * 0.5f);
  pm_cyl_ex(b, r, r, L, lod ? 8 : 12, dk(c, 0.55f), c, false, false);
  pm_pop(b);
  pm_smooth(b, false);
  for (int i = 0; i < 4; i++) { /* flanges */
    pm_push(b);
    pm_move(b, -L * 0.5f + L * ((float)i + 0.5f) / 4.0f - r * 0.2f, y, 0);
    pm_rotz(b, -CW_PI * 0.5f);
    pm_cyl_ex(b, r * 1.25f, r * 1.25f, r * 0.4f, lod ? 8 : 10, dk(c, 0.5f), dk(c, 0.8f), true, true);
    pm_pop(b);
  }
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < 2; i++) { /* concrete saddles */
    pm_push(b);
    pm_move(b, -L * 0.3f + L * 0.6f * (float)i, 0, 0);
    pm_box(b, r * 0.9f, y - r * 0.6f, r * 2.2f, rgb(0.3f, 0.3f, 0.3f), rgb(0.5f, 0.5f, 0.5f));
    pm_pop(b);
  }
  if (variant != 2 && !lod) { /* valve wheel */
    pm_layer(b, PL_METAL);
    pm_push(b);
    pm_move(b, 0, y + r, 0);
    pm_cyl(b, r * 0.18f, r * 0.18f, r * 0.7f, 6, dk(c, 0.5f), c);
    pm_move(b, 0, r * 0.7f, 0);
    pm_ring(b, r * 0.55f, r * 0.07f, 12, 4, rgb(0.8f, 0.15f, 0.1f));
    pm_pop(b);
  }
}

GEN(gen_pipe_rack) {
  float s = size, bay = s * 1.0f;
  int bays = variant == 1 ? 4 : 3;
  pcol st = rgb(0.2f, 0.22f, 0.26f);
  pm_layer(b, PL_METAL);
  pm_vary(b, 0.04f);
  for (int i = 0; i <= bays; i++) {
    float x = (i - bays * 0.5f) * bay;
    for (int k = -1; k <= 1; k += 2) {
      pm_beam(b, P(x, 0, k * s * 0.45f), P(x, s * 0.95f, k * s * 0.45f), s * 0.07f, st);
      if (!lod) pm_beam(b, P(x, 0, k * s * 0.45f), P(x + bay * 0.5f, s * 0.9f, k * s * 0.45f), s * 0.035f, dk(st, 1.2f));
    }
    pm_beam(b, P(x, s * 0.95f, -s * 0.5f), P(x, s * 0.95f, s * 0.5f), s * 0.07f, st);
  }
  pm_beam(b, P(-bays * 0.5f * bay, s * 0.95f, -s * 0.45f), P(bays * 0.5f * bay, s * 0.95f, -s * 0.45f), s * 0.06f, st);
  pm_beam(b, P(-bays * 0.5f * bay, s * 0.95f, s * 0.45f), P(bays * 0.5f * bay, s * 0.95f, s * 0.45f), s * 0.06f, st);
  static const unsigned pc[5] = { 0xc43a2a, 0x7e8c96, 0xe0b020, 0x2f6aa8, 0x8a8a8a };
  int np = variant == 2 ? 5 : 4;
  for (int p = 0; p < np; p++) {
    float z = (p - (np - 1) * 0.5f) * s * 0.2f, r = s * (0.05f + 0.025f * (float)(p & 1)) * (p == 1 ? 1.6f : 1);
    pm_push(b);
    pm_move(b, -bays * 0.5f * bay - bay * 0.3f, s * 0.95f + r, z);
    pm_rotz(b, -CW_PI * 0.5f);
    pm_cyl_ex(b, r, r, (float)bays * bay + bay * 0.6f, lod ? 6 : 9, dk(hex(pc[p]), 0.6f), hex(pc[p]), false, false);
    pm_pop(b);
  }
  /* an elbow rising out of the end */
  pm_rod(b, P(bays * 0.5f * bay + bay * 0.3f, s * 1.0f, -s * 0.2f), P(bays * 0.5f * bay + bay * 0.3f, s * 1.9f, -s * 0.2f), s * 0.06f, 8, hex(pc[0]));
}

GEN(gen_tank) {
  pm_vary(b, 0.04f);
  pcol c = variant == 1 ? rgb(0.8f, 0.8f, 0.78f) : variant == 2 ? rgb(0.75f, 0.62f, 0.3f) : rgb(0.78f, 0.78f, 0.74f);
  pcol rust = rgb(0.45f, 0.26f, 0.14f);
  float h = size, r = h * 0.4f;
  pm_layer(b, PL_METAL);
  pm_smooth(b, true);
  if (variant == 1) { /* a sphere on legs */
    float rr = h * 0.36f;
    pm_push(b);
    pm_move(b, 0, h * 0.4f + rr, 0);
    pm_ball(b, rr, rr, rr, lod ? 1 : 2, 0.0f, pc_mix(c, rust, 0.4f), c);
    pm_pop(b);
    pm_smooth(b, false);
    for (int i = 0; i < 6; i++) {
      float a = (float)i / 6.0f * CW_TAU;
      pm_beam(b, P(cosf(a) * rr * 0.92f, h * 0.4f + rr * 0.55f, sinf(a) * rr * 0.92f), P(cosf(a) * rr * 1.05f, 0, sinf(a) * rr * 1.05f), h * 0.03f, rgb(0.3f, 0.3f, 0.32f));
    }
    pm_push(b);
    pm_move(b, 0, h * 0.4f + rr * 0.55f, 0);
    pm_cyl_ex(b, rr * 0.93f, rr * 0.93f, h * 0.012f, 12, rgb(0.3f, 0.3f, 0.32f), rgb(0.3f, 0.3f, 0.32f), false, false);
    pm_pop(b);
    return;
  }
  if (variant == 2) { /* a horizontal bullet tank on saddles */
    float L = h * 1.8f, rr = h * 0.3f;
    pm_push(b);
    pm_move(b, -L * 0.5f, rr + h * 0.1f, 0);
    pm_rotz(b, -CW_PI * 0.5f);
    float prof[] = { 0.001f, 0, rr * 0.7f, rr * 0.08f, rr, rr * 0.4f, rr, L - rr * 0.4f, rr * 0.7f, L - rr * 0.08f, 0.001f, L };
    pm_lathe(b, prof, 6, lod ? 8 : 14, pc_mix(c, rust, 0.5f), c);
    pm_pop(b);
    pm_layer(b, PL_MAIN);
    pm_smooth(b, false);
    for (int i = 0; i < 2; i++) {
      pm_push(b);
      pm_move(b, -L * 0.28f + L * 0.56f * (float)i, 0, 0);
      pm_box(b, h * 0.1f, h * 0.2f, rr * 1.6f, rgb(0.3f, 0.3f, 0.3f), rgb(0.5f, 0.5f, 0.5f));
      pm_pop(b);
    }
    return;
  }
  float prof[] = { r, h * 0.08f, r * 1.0f, h * 0.88f, r * 0.9f, h * 0.93f, r * 0.55f, h * 0.99f, 0.001f, h * 1.02f };
  pm_lathe(b, prof, 5, lod ? 10 : 18, pc_mix(c, rust, 0.35f), c);
  pm_smooth(b, false);
  pm_layer(b, PL_METAL);
  /* seam rings, a catwalk and a stair spiral */
  for (int i = 1; i < 4; i++) {
    pm_push(b);
    pm_move(b, 0, h * 0.22f * (float)i, 0);
    pm_cyl_ex(b, r * 1.012f, r * 1.012f, h * 0.02f, lod ? 10 : 18, dk(c, 0.7f), dk(c, 0.7f), false, false);
    pm_pop(b);
  }
  pm_push(b);
  pm_move(b, 0, h * 0.88f, 0);
  pm_cyl_ex(b, r * 1.12f, r * 1.12f, h * 0.012f, 16, rgb(0.25f, 0.25f, 0.28f), rgb(0.4f, 0.4f, 0.42f), true, true);
  pm_pop(b);
  for (int i = 0; i < 12 && !lod; i++) {
    float a = (float)i / 12.0f * CW_TAU;
    pm_beam(b, P(cosf(a) * r * 1.12f, h * 0.89f, sinf(a) * r * 1.12f), P(cosf(a) * r * 1.12f, h * 0.96f, sinf(a) * r * 1.12f), h * 0.008f, rgb(0.3f, 0.3f, 0.32f));
  }
  if (!lod)
    for (int i = 0; i < 24; i++) {
      float t0 = (float)i / 24.0f, t1 = (float)(i + 1) / 24.0f;
      float a0 = 0.5f + t0 * 3.6f, a1 = 0.5f + t1 * 3.6f;
      pm_beam(b, P(cosf(a0) * r * 1.03f, h * 0.05f + h * 0.83f * t0, sinf(a0) * r * 1.03f), P(cosf(a1) * r * 1.03f, h * 0.05f + h * 0.83f * t1, sinf(a1) * r * 1.03f), h * 0.014f, rgb(0.28f, 0.28f, 0.3f));
    }
  pm_layer(b, PL_MAIN);
  pm_cyl_ex(b, r * 1.1f, r * 1.1f, h * 0.08f, lod ? 10 : 18, rgb(0.25f, 0.25f, 0.25f), rgb(0.45f, 0.45f, 0.45f), false, false);
}

GEN(gen_silo) {
  pm_vary(b, 0.04f);
  float h = size, r = h * 0.2f;
  pcol c = variant == 2 ? rgb(0.78f, 0.74f, 0.66f) : rgb(0.72f, 0.74f, 0.76f);
  int cluster = variant == 1 ? 3 : 1;
  for (int k = 0; k < cluster; k++) {
    pm_push(b);
    pm_move(b, (k - (cluster - 1) * 0.5f) * r * 2.05f, 0, 0);
    float hh = h * (k == 1 ? 1.0f : 0.88f);
    pm_layer(b, variant == 2 ? PL_MAIN : PL_METAL);
    pm_smooth(b, true);
    float prof[] = { r, 0, r, hh * 0.86f, r * 0.7f, hh * 0.93f, 0.001f, hh };
    pm_lathe(b, prof, 4, lod ? 10 : 18, dk(c, 0.55f), c);
    pm_smooth(b, false);
    for (int i = 1; i * 3.2f < hh * 0.85f; i++) {
      pm_push(b);
      pm_move(b, 0, i * 3.2f * h / 18.0f, 0);
      pm_cyl_ex(b, r * 1.02f, r * 1.02f, h * 0.015f, lod ? 10 : 18, dk(c, 0.6f), dk(c, 0.8f), false, false);
      pm_pop(b);
    }
    if (variant == 2) { /* a faded stripe band */
      pm_push(b);
      pm_move(b, 0, hh * 0.55f, 0);
      pm_layer(b, PL_MAIN);
      pm_cyl_ex(b, r * 1.01f, r * 1.01f, hh * 0.1f, lod ? 10 : 18, rgb(0.7f, 0.22f, 0.12f), rgb(0.7f, 0.22f, 0.12f), false, false);
      pm_pop(b);
    }
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  if (!lod) { /* caged ladder */
    float x = (cluster == 1 ? 0 : -r * 2.05f) , rr = r * 1.04f;
    pm_beam(b, P(x - h * 0.02f, 0, rr), P(x - h * 0.02f, h * 0.84f, rr), h * 0.01f, rgb(0.25f, 0.25f, 0.28f));
    pm_beam(b, P(x + h * 0.02f, 0, rr), P(x + h * 0.02f, h * 0.84f, rr), h * 0.01f, rgb(0.25f, 0.25f, 0.28f));
    for (int i = 0; i < 20; i++) pm_beam(b, P(x - h * 0.02f, h * 0.84f * (float)i / 20.0f, rr), P(x + h * 0.02f, h * 0.84f * (float)i / 20.0f, rr), h * 0.006f, rgb(0.3f, 0.3f, 0.33f));
  }
  /* conveyor gallery to a low shed */
  pm_beam(b, P(0, h * 0.93f, 0), P(r * 3.2f, h * 0.3f, r * 0.2f), h * 0.04f, rgb(0.38f, 0.4f, 0.44f));
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, r * 3.2f, 0, r * 0.2f);
  pm_box(b, r * 1.4f, h * 0.3f, r * 1.4f, rgb(0.4f, 0.28f, 0.2f), rgb(0.6f, 0.46f, 0.34f));
  pm_pop(b);
}

GEN(gen_flare_stack) {
  pm_vary(b, 0.04f);
  float h = size;
  pcol st = rgb(0.5f, 0.52f, 0.56f);
  pm_layer(b, PL_METAL);
  lattice(b, h * 0.14f, h * 0.045f, h * 0.7f, lod ? 5 : 9, h * 0.005f, st);
  pm_smooth(b, true);
  pm_push(b);
  pm_move(b, 0, h * 0.64f, 0);
  pm_cyl_ex(b, h * 0.014f, h * 0.014f, h * 0.36f, 8, rgb(0.4f, 0.4f, 0.42f), rgb(0.6f, 0.6f, 0.64f), false, true);
  pm_pop(b);
  pm_smooth(b, false);
  /* platforms */
  for (int i = 0; i < 3; i++) {
    float y = h * (0.2f + 0.2f * (float)i), w = h * (0.1f - 0.025f * (float)i);
    pm_push(b);
    pm_move(b, 0, y, 0);
    pm_cyl_ex(b, w, w, h * 0.004f, 10, rgb(0.25f, 0.25f, 0.28f), rgb(0.4f, 0.4f, 0.42f), true, true);
    pm_pop(b);
  }
  int flames = variant == 1 ? 2 : 1;
  for (int f = 0; f < flames; f++) {
    float ox = variant == 1 ? (f ? 1 : -1) * h * 0.03f : 0;
    float fh = h * (f ? 0.13f : 0.18f);
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, ox, h * 1.0f, 0);
    float prof[] = { 0.001f, 0, h * 0.02f, fh * 0.12f, h * 0.026f, fh * 0.4f, h * 0.014f, fh * 0.72f, 0.001f, fh };
    pm_smooth(b, true);
    pm_lathe(b, prof, 5, 8, glow(rgb(1.0f, 0.35f, 0.04f), 3.0f), glow(rgb(1.0f, 0.82f, 0.4f), 2.4f));
    pm_pop(b);
    pm_push(b);
    pm_move(b, ox, h * 1.0f, 0);
    pm_scale(b, 0.55f, 0.7f, 0.55f);
    pm_lathe(b, prof, 5, 8, glow(rgb(1.0f, 0.9f, 0.6f), 5.0f), glow(rgb(1.0f, 1.0f, 0.9f), 4.0f));
    pm_pop(b);
    pm_smooth(b, false);
  }
  /* an aircraft warning light */
  pm_push(b);
  pm_move(b, 0, h * 0.7f, 0);
  pm_ball(b, h * 0.012f, h * 0.012f, h * 0.012f, 0, 0, glow(rgb(1, 0.1f, 0.05f), 4), glow(rgb(1, 0.3f, 0.2f), 5));
  pm_pop(b);
}

GEN(gen_cooling_tower) {
  pm_vary(b, 0.03f);
  float h = size;
  pcol c = variant == 1 ? rgb(0.45f, 0.43f, 0.4f) : rgb(0.72f, 0.72f, 0.7f);
  pm_layer(b, PL_MAIN);
  pm_smooth(b, true);
  int n = lod ? 5 : 9;
  float prof[24];
  for (int i = 0; i <= n; i++) {
    float t = (float)i / (float)n;
    float r = h * (0.33f - 0.12f * sinf(CW_PI * 0.7f * t) + 0.04f * t * t * t);
    prof[i * 2] = r, prof[i * 2 + 1] = h * t;
  }
  pm_lathe(b, prof, n + 1, lod ? 12 : 22, dk(c, 0.5f), c);
  pm_smooth(b, false);
  /* inlet shadow band at the base and a lip */
  pm_cyl_ex(b, h * 0.335f, h * 0.332f, h * 0.07f, lod ? 12 : 22, rgb(0.08f, 0.08f, 0.09f), rgb(0.2f, 0.2f, 0.22f), false, false);
  pm_push(b);
  pm_move(b, 0, h * 0.995f, 0);
  pm_jitter(b, variant == 1 ? 0.04f : 0.0f);
  pm_cyl_ex(b, prof[n * 2] * 1.03f, prof[n * 2] * 1.03f, h * 0.015f, lod ? 12 : 22, dk(c, 0.7f), c, false, false);
  pm_pop(b);
  if (variant == 0) { /* steam rising */
    pm_layer(b, PL_LEAF);
    for (int i = 0; i < (lod ? 3 : 6); i++) {
      float t = (float)i / 5.0f;
      pm_push(b);
      pm_move(b, pm_rands(b) * h * 0.04f * (1 + t), h * (1.02f + 0.18f * t), pm_rands(b) * h * 0.04f * (1 + t));
      pm_ball(b, h * (0.07f + 0.05f * t), h * (0.05f + 0.03f * t), h * (0.07f + 0.05f * t), 1, 0.2f, rgb(0.78f, 0.8f, 0.84f), rgb(0.97f, 0.97f, 0.99f));
      pm_pop(b);
    }
  }
}

/* ── cranes ──────────────────────────────────────────────────────────────────────────────────────── */

GEN(gen_crane) {
  pm_vary(b, 0.04f);
  float h = size;
  pcol y1 = variant == 1 ? rgb(0.92f, 0.7f, 0.12f) : variant == 2 ? rgb(0.82f, 0.22f, 0.14f) : rgb(0.88f, 0.52f, 0.1f);
  pcol dkc = rgb(0.18f, 0.18f, 0.2f);
  pm_layer(b, PL_METAL);
  if (variant == 1) { /* tower crane */
    lattice(b, h * 0.07f, h * 0.07f, h * 0.82f, lod ? 8 : 16, h * 0.004f, y1);
    pm_push(b);
    pm_move(b, 0, h * 0.82f, 0);
    pm_box(b, h * 0.09f, h * 0.05f, h * 0.09f, dkc, rgb(0.5f, 0.5f, 0.52f));
    pm_move(b, 0, h * 0.05f, 0);
    /* jib and counter jib as flat lattices */
    for (int k = -1; k <= 1; k += 2) {
      pm_beam(b, P(k * h * 0.005f, 0, h * 0.0f), P(k * h * 0.005f, 0, h * 0.62f), h * 0.005f, y1);
      pm_beam(b, P(k * h * 0.005f, h * 0.02f, 0), P(k * h * 0.005f, h * 0.02f, h * 0.62f), h * 0.004f, y1);
    }
    for (int i = 0; i < (lod ? 5 : 12); i++) {
      float z0 = h * 0.62f * (float)i / 12.0f, z1 = h * 0.62f * (float)(i + 1) / 12.0f;
      pm_beam(b, P(0, 0, z0), P(0, h * 0.02f, z1), h * 0.003f, y1);
    }
    pm_beam(b, P(0, 0, 0), P(0, 0, -h * 0.2f), h * 0.01f, y1);
    pm_move(b, 0, 0, -h * 0.2f);
    pm_box(b, h * 0.05f, h * 0.05f, h * 0.06f, rgb(0.4f, 0.4f, 0.42f), rgb(0.55f, 0.55f, 0.57f)); /* counterweight */
    pm_move(b, 0, 0, h * 0.2f);
    pm_beam(b, P(0, h * 0.0f, h * 0.35f), P(0, -h * 0.2f, h * 0.35f), h * 0.002f, dkc);
    pm_box(b, h * 0.02f, h * 0.02f, h * 0.02f, rgb(0.6f, 0.12f, 0.1f), rgb(0.8f, 0.2f, 0.15f));
    pm_pop(b);
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, 0, h * 0.9f, 0);
    pm_ball(b, h * 0.01f, h * 0.01f, h * 0.01f, 0, 0, glow(rgb(1, 0.1f, 0.05f), 4), glow(rgb(1, 0.3f, 0.2f), 5));
    pm_pop(b);
    return;
  }
  float W = h * 0.55f, Hh = h * 0.6f;
  /* a portal frame with a boom: variant 0 container gantry, 2 harbour jib */
  if (variant == 2) {
    for (int i = 0; i < 2; i++) {
      pm_beam(b, P(-W * 0.12f, 0, (i ? 1 : -1) * W * 0.09f), P(0, Hh, (i ? 1 : -1) * W * 0.04f), h * 0.02f, y1);
      pm_beam(b, P(W * 0.12f, 0, (i ? 1 : -1) * W * 0.09f), P(0, Hh, (i ? 1 : -1) * W * 0.04f), h * 0.02f, y1);
    }
    pm_push(b);
    pm_move(b, 0, Hh, 0);
    pm_box(b, h * 0.1f, h * 0.06f, h * 0.12f, dkc, rgb(0.4f, 0.4f, 0.42f));
    float ang = 0.95f;
    t3_vec3 tip = P(0, h * 0.34f * sinf(ang) + h * 0.06f, h * 0.34f * cosf(ang) + h * 0.05f);
    pm_beam(b, P(0, h * 0.04f, h * 0.02f), tip, h * 0.025f, y1);
    pm_beam(b, P(0, h * 0.03f, -h * 0.08f), tip, h * 0.006f, dkc);
    pm_rod(b, tip, P(0, tip.y - h * 0.3f, tip.z), h * 0.003f, 4, dkc);
    pm_box(b, h * 0.03f, h * 0.025f, h * 0.03f, rgb(0.55f, 0.1f, 0.08f), rgb(0.8f, 0.2f, 0.12f));
    pm_pop(b);
    return;
  }
  float L = h * 0.9f;
  for (int s = 0; s < 4; s++) {
    float sx = (s & 1) ? 1.0f : -1.0f, sz = (s & 2) ? 1.0f : -1.0f;
    pm_beam(b, P(sx * W * 0.4f, 0, sz * W * 0.22f), P(sx * W * 0.4f, Hh, sz * W * 0.22f), h * 0.022f, y1);
  }
  for (int k = 0; k < 2; k++) {
    float sx = k ? 1.0f : -1.0f;
    pm_beam(b, P(sx * W * 0.4f, Hh * 0.5f, -W * 0.22f), P(sx * W * 0.4f, Hh * 0.5f, W * 0.22f), h * 0.012f, y1);
    pm_beam(b, P(sx * W * 0.4f, 0, -W * 0.22f), P(sx * W * 0.4f, Hh * 0.5f, W * 0.22f), h * 0.01f, y1);
    pm_beam(b, P(sx * W * 0.4f, 0, W * 0.22f), P(sx * W * 0.4f, Hh * 0.5f, -W * 0.22f), h * 0.01f, y1);
  }
  pm_beam(b, P(-W * 0.4f, Hh, -W * 0.22f), P(-W * 0.4f, Hh, W * 0.22f), h * 0.03f, y1);
  /* the girder: runs out over the water (+z) and back, with a trolley */
  for (int k = -1; k <= 1; k += 2) {
    pm_beam(b, P(k * W * 0.25f, Hh + h * 0.02f, -L * 0.2f), P(k * W * 0.25f, Hh + h * 0.02f, L * 0.62f), h * 0.02f, y1);
  }
  pm_beam(b, P(-W * 0.25f, Hh + h * 0.02f, L * 0.62f), P(W * 0.25f, Hh + h * 0.02f, L * 0.62f), h * 0.02f, y1);
  pm_beam(b, P(0, Hh + h * 0.03f, -L * 0.2f), P(0, Hh + h * 0.03f, L * 0.62f), h * 0.012f, y1);
  for (int i = 0; i < (lod ? 4 : 10); i++) {
    float z = -L * 0.2f + L * 0.82f * (float)i / 10.0f;
    pm_beam(b, P(-W * 0.25f, Hh + h * 0.02f, z), P(W * 0.25f, Hh + h * 0.02f, z + L * 0.08f), h * 0.008f, y1);
  }
  pm_push(b);
  pm_move(b, 0, Hh, L * 0.35f);
  pm_box(b, W * 0.4f, h * 0.045f, h * 0.06f, dkc, rgb(0.4f, 0.4f, 0.42f));
  pm_beam(b, P(-W * 0.1f, 0, 0), P(-W * 0.1f, -Hh * 0.55f, 0), h * 0.0025f, dkc);
  pm_beam(b, P(W * 0.1f, 0, 0), P(W * 0.1f, -Hh * 0.55f, 0), h * 0.0025f, dkc);
  pm_move(b, 0, -Hh * 0.55f, 0);
  pm_box(b, W * 0.3f, h * 0.025f, h * 0.05f, rgb(0.6f, 0.6f, 0.62f), rgb(0.75f, 0.75f, 0.77f));
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, -W * 0.4f, Hh + h * 0.05f, 0);
  pm_ball(b, h * 0.012f, h * 0.012f, h * 0.012f, 0, 0, glow(rgb(1, 0.1f, 0.05f), 4), glow(rgb(1, 0.3f, 0.2f), 5));
  pm_pop(b);
}

/* ── power, light, signs ─────────────────────────────────────────────────────────────────────────── */

GEN(gen_pylon) {
  pm_vary(b, 0.03f);
  float h = size;
  pcol st = rgb(0.46f, 0.5f, 0.54f);
  pcol glass = rgb(0.55f, 0.8f, 0.75f);
  pm_layer(b, PL_METAL);
  if (variant == 1) { /* wooden H-frame */
    for (int k = -1; k <= 1; k += 2) pm_limb(b, P(k * h * 0.12f, 0, 0), P(k * h * 0.1f, h * 0.9f, 0), h * 0.018f, h * 0.014f, 6, rgb(0.2f, 0.13f, 0.08f), rgb(0.4f, 0.3f, 0.2f));
    pm_beam(b, P(-h * 0.2f, h * 0.82f, 0), P(h * 0.2f, h * 0.82f, 0), h * 0.014f, rgb(0.28f, 0.2f, 0.12f));
    pm_beam(b, P(-h * 0.12f, h * 0.55f, 0), P(h * 0.1f, h * 0.85f, 0), h * 0.01f, rgb(0.28f, 0.2f, 0.12f));
    for (int k = -1; k <= 1; k += 2) {
      pm_rod(b, P(k * h * 0.2f, h * 0.82f, 0), P(k * h * 0.2f, h * 0.88f, 0), h * 0.01f, 6, glass);
    }
    pm_rod(b, P(0, h * 0.82f, 0), P(0, h * 0.89f, 0), h * 0.01f, 6, glass);
    return;
  }
  lattice(b, h * 0.26f, h * 0.05f, h * 0.7f, lod ? 5 : 8, h * 0.005f, st);
  pm_beam(b, P(0, h * 0.7f, 0), P(0, h * 0.98f, 0), h * 0.014f, st);
  pm_beam(b, P(-h * 0.04f, h * 0.7f, 0), P(-h * 0.04f, h * 0.9f, 0), h * 0.008f, st);
  float arms[3] = { 0.58f, 0.74f, 0.9f }, ext[3] = { 0.34f, 0.28f, 0.2f };
  for (int a = 0; a < 3; a++) {
    float y = h * arms[a], e = h * ext[a] * 0.7f;
    pm_beam(b, P(-e, y, 0), P(e, y, 0), h * 0.014f, st);
    pm_beam(b, P(-e, y, 0), P(0, y + h * 0.05f, 0), h * 0.006f, st);
    pm_beam(b, P(e, y, 0), P(0, y + h * 0.05f, 0), h * 0.006f, st);
    for (int k = -1; k <= 1; k += 2) {
      float x = k * e;
      for (int d = 0; d < (lod ? 1 : 5); d++) {
        pm_push(b);
        pm_move(b, x, y - h * 0.018f * (float)(d + 1), 0);
        pm_cyl(b, h * 0.014f, h * 0.012f, h * 0.012f, 6, glass, dk(glass, 1.2f));
        pm_pop(b);
      }
      pm_rod(b, P(x, y, 0), P(x, y - h * 0.09f, 0), h * 0.011f, 6, glass);
      pm_rod(b, P(x, y - h * 0.09f, 0), P(x, y - h * 0.095f, 0), h * 0.018f, 6, rgb(0.3f, 0.3f, 0.3f));
    }
  }
}

GEN(gen_light_pole) {
  pm_vary(b, 0.03f);
  float h = size;
  pcol st = rgb(0.4f, 0.42f, 0.46f);
  pm_layer(b, PL_METAL);
  pm_smooth(b, true);
  pm_cyl_ex(b, h * 0.02f, h * 0.012f, h, lod ? 5 : 8, dk(st, 0.7f), st, false, false);
  pm_cyl_ex(b, h * 0.035f, h * 0.03f, h * 0.06f, 8, dk(st, 0.7f), dk(st, 0.9f), false, false);
  pm_smooth(b, false);
  int rows = variant == 1 ? 1 : variant == 2 ? 3 : 2, cols = variant == 1 ? 2 : variant == 2 ? 3 : 3;
  pm_push(b);
  pm_move(b, 0, h * 0.97f, 0);
  pm_rotx(b, -0.25f);
  pm_box_c(b, cols * h * 0.075f + h * 0.02f, rows * h * 0.075f + h * 0.02f, h * 0.03f, rgb(0.12f, 0.12f, 0.14f));
  pm_layer(b, PL_GLOW);
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++) {
      float x = (c - (cols - 1) * 0.5f) * h * 0.075f, y = (r - (rows - 1) * 0.5f) * h * 0.075f;
      pm_quad(b, P(x - h * 0.03f, y - h * 0.03f, h * 0.0165f), P(x + h * 0.03f, y - h * 0.03f, h * 0.0165f), P(x + h * 0.03f, y + h * 0.03f, h * 0.0165f), P(x - h * 0.03f, y + h * 0.03f, h * 0.0165f), glow(rgb(1.0f, 0.95f, 0.8f), 5.0f));
    }
  pm_pop(b);
}

GEN(gen_street_lamp) {
  pm_vary(b, 0.03f);
  float h = size;
  pcol st = variant == 2 ? rgb(0.08f, 0.1f, 0.1f) : rgb(0.3f, 0.32f, 0.36f);
  pm_layer(b, PL_METAL);
  pm_smooth(b, true);
  float prof[] = { h * 0.035f, 0, h * 0.02f, h * 0.06f, h * 0.012f, h * 0.15f, h * 0.01f, h * 0.9f };
  pm_lathe(b, prof, 4, lod ? 6 : 9, dk(st, 0.7f), st);
  pm_smooth(b, false);
  pcol lamp = variant == 2 ? rgb(1.0f, 0.62f, 0.25f) : rgb(1.0f, 0.88f, 0.6f);
  int arms = variant == 1 ? 2 : 1;
  for (int a = 0; a < arms; a++) {
    float dir = (a ? -1.0f : 1.0f) * (variant == 1 ? 1.0f : 1.0f);
    if (variant == 2) { /* a lantern on top */
      pm_push(b);
      pm_move(b, 0, h * 0.9f, 0);
      pm_cyl(b, h * 0.045f, h * 0.02f, h * 0.03f, 6, st, st);
      pm_layer(b, PL_GLOW);
      pm_move(b, 0, h * 0.03f, 0);
      pm_cyl_ex(b, h * 0.04f, h * 0.05f, h * 0.1f, 6, glow(lamp, 2.0f), glow(lamp, 3.0f), false, false);
      pm_layer(b, PL_METAL);
      pm_move(b, 0, h * 0.1f, 0);
      pm_cyl(b, h * 0.06f, 0.001f, h * 0.045f, 6, st, st);
      pm_pop(b);
      break;
    }
    /* arc arm */
    t3_vec3 prev = P(0, h * 0.9f, 0);
    for (int i = 1; i <= 5; i++) {
      float t = (float)i / 5.0f, an = t * 1.3f;
      t3_vec3 cur = P(0, h * 0.9f + sinf(an) * h * 0.14f, dir * (1 - cosf(an)) * h * 0.14f + dir * t * h * 0.16f);
      pm_rod(b, prev, cur, h * 0.008f, 5, st);
      prev = cur;
    }
    pm_push(b);
    pm_move(b, 0, prev.y - h * 0.005f, prev.z);
    pm_box(b, h * 0.07f, h * 0.025f, h * 0.15f, dk(st, 0.7f), st);
    pm_layer(b, PL_GLOW);
    pm_quad(b, P(-h * 0.03f, -h * 0.001f, h * 0.06f), P(-h * 0.03f, -h * 0.001f, -h * 0.06f), P(h * 0.03f, -h * 0.001f, -h * 0.06f), P(h * 0.03f, -h * 0.001f, h * 0.06f), glow(lamp, 6.0f));
    pm_layer(b, PL_METAL);
    pm_pop(b);
  }
}

static pcol neon_pal(int v, int k) {
  static const unsigned pal[6][3] = { { 0xff2a9a, 0x2af0ff, 0xffd02a }, { 0x3aff6a, 0xe8ff2a, 0xff7a2a }, { 0xff3a2a, 0xffffff, 0xffb02a },
                                      { 0xa02aff, 0xff7a2a, 0x2affd0 }, { 0x2ad0ff, 0x2a5aff, 0xffffff }, { 0xff2a60, 0xffa02a, 0x2affb0 } };
  return hex(pal[v % 6][k % 3]);
}

GEN(gen_neon_sign) {
  float h = size;
  float w = h * 0.24f;
  pm_layer(b, PL_METAL);
  pm_vary(b, 0.03f);
  pcol dkc = rgb(0.06f, 0.06f, 0.08f);
  pm_beam(b, P(0, 0, 0), P(0, h * 0.35f, 0), h * 0.03f, dkc);
  pm_push(b);
  pm_move(b, 0, h * 0.35f, 0);
  pm_box(b, w, h * 0.65f, h * 0.04f, dkc, rgb(0.12f, 0.12f, 0.16f));
  pm_layer(b, PL_GLOW);
  float gain = 3.6f;
  pcol c0 = glow(neon_pal(variant, 0), gain), c1 = glow(neon_pal(variant, 1), gain), c2 = glow(neon_pal(variant, 2), gain);
  for (int side = -1; side <= 1; side += 2) {
    float z = side * h * 0.026f;
    pm_push(b);
    pm_move(b, 0, 0, z);
    if (side < 0) pm_roty(b, CW_PI);
    /* a border of tube and stacked bars, a ring and a bolt-like zig */
    float t = h * 0.008f;
    pm_beam(b, P(-w * 0.45f, h * 0.03f, 0), P(w * 0.45f, h * 0.03f, 0), t, c0);
    pm_beam(b, P(-w * 0.45f, h * 0.62f, 0), P(w * 0.45f, h * 0.62f, 0), t, c0);
    pm_beam(b, P(-w * 0.45f, h * 0.03f, 0), P(-w * 0.45f, h * 0.62f, 0), t, c0);
    pm_beam(b, P(w * 0.45f, h * 0.03f, 0), P(w * 0.45f, h * 0.62f, 0), t, c0);
    pm_push(b);
    pm_move(b, 0, h * 0.44f, 0);
    pm_ring(b, w * 0.28f, t * 0.8f, 20, 4, c1);
    pm_pop(b);
    for (int i = 0; i < 3; i++) pm_beam(b, P(-w * 0.3f, h * (0.12f + 0.055f * (float)i), 0), P(w * 0.3f - w * 0.1f * (float)i, h * (0.12f + 0.055f * (float)i), 0), t * 1.2f, i == 1 ? c2 : c1);
    pm_beam(b, P(-w * 0.12f, h * 0.5f, 0), P(w * 0.08f, h * 0.44f, 0), t, c2);
    pm_beam(b, P(w * 0.08f, h * 0.44f, 0), P(-w * 0.08f, h * 0.38f, 0), t, c2);
    pm_pop(b);
  }
  pm_pop(b);
}

GEN(gen_billboard) {
  float h = size, w = h * 1.9f, ph = h * 0.5f;
  pcol st = rgb(0.2f, 0.22f, 0.25f);
  pm_layer(b, PL_METAL);
  pm_vary(b, 0.03f);
  for (int k = -1; k <= 1; k += 2) pm_beam(b, P(k * w * 0.3f, 0, 0), P(k * w * 0.3f, h * 0.55f, 0), h * 0.04f, st);
  pm_push(b);
  pm_move(b, 0, h * 0.5f, 0);
  pm_box(b, w, ph, h * 0.06f, rgb(0.07f, 0.07f, 0.09f), rgb(0.15f, 0.15f, 0.18f));
  pm_beam(b, P(-w * 0.5f, ph * 0.5f, h * 0.05f), P(-w * 0.5f, ph * 0.5f, -h * 0.05f), h * 0.01f, st);
  pm_layer(b, PL_GLOW);
  static const unsigned pal[4][3] = { { 0xff3a8a, 0xffd02a, 0x2affe0 }, { 0x2a8aff, 0xff4a2a, 0xffe02a }, { 0x3aff7a, 0x8a2aff, 0xffffff }, { 0xffa02a, 0xff2a5a, 0x2ab0ff } };
  pcol a = glow(hex(pal[variant][0]), 2.8f), c = glow(hex(pal[variant][1]), 2.8f), d = glow(hex(pal[variant][2]), 2.8f);
  float z = h * 0.032f;
  /* a sunburst field: a colour ground, a big disc and stripes, no words */
  pm_quad(b, P(-w * 0.47f, ph * 0.04f, z), P(w * 0.47f, ph * 0.04f, z), P(w * 0.47f, ph * 0.96f, z), P(-w * 0.47f, ph * 0.96f, z), dk(a, 0.6f));
  pm_push(b);
  pm_move(b, -w * 0.18f, ph * 0.5f, z + 0.01f);
  pm_cyl_ex(b, ph * 0.38f, ph * 0.38f, 0.01f, 24, c, c, false, true);
  pm_pop(b);
  pm_rotx(b, 0);
  for (int i = 0; i < 4; i++) {
    float x0 = w * 0.1f + w * 0.075f * (float)i;
    pm_quad(b, P(x0, ph * 0.12f, z + 0.01f), P(x0 + w * 0.045f, ph * 0.12f, z + 0.01f), P(x0 + w * 0.085f, ph * 0.88f, z + 0.01f), P(x0 + w * 0.04f, ph * 0.88f, z + 0.01f), (i & 1) ? d : a);
  }
  pm_layer(b, PL_METAL);
  /* catwalk and lamps */
  pm_beam(b, P(-w * 0.5f, ph, h * 0.12f), P(w * 0.5f, ph, h * 0.12f), h * 0.012f, st);
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < 4; i++) {
    float x = (i - 1.5f) * w * 0.22f;
    pm_push(b);
    pm_move(b, x, ph + h * 0.04f, h * 0.18f);
    pm_ball(b, h * 0.02f, h * 0.02f, h * 0.02f, 0, 0, glow(rgb(1, 0.95f, 0.8f), 5), glow(rgb(1, 1, 0.9f), 6));
    pm_pop(b);
  }
  pm_pop(b);
}

/* ── buildings ───────────────────────────────────────────────────────────────────────────────────── */

typedef struct tower_pal {
  pcol wall0, wall1, win_dark, lit_a, lit_b;
} tower_pal;

static void windows(pm *b, float w, float d, float y0, float y1, float fh, float cw, float lit_prob, const tower_pal *tp, bool bands) {
  int floors = (int)((y1 - y0) / fh);
  for (int face = 0; face < 4; face++) {
    float fw = (face & 1) ? d : w;
    int cols = (int)(fw / cw);
    if (cols < 1) continue;
    float step = fw / (float)cols;
    pm_push(b);
    pm_roty(b, (float)face * CW_PI * 0.5f);
    float off = ((face & 1) ? w : d) * 0.5f;
    for (int f = 0; f < floors; f++) {
      float y = y0 + (float)f * fh + fh * 0.22f;
      if (bands) {
        bool lit = pm_rand(b) < lit_prob;
        pm_layer(b, lit ? PL_GLOW : PL_METAL);
        pm_quad(b, P(-fw * 0.5f + step * 0.2f, y, off + 0.03f), P(fw * 0.5f - step * 0.2f, y, off + 0.03f), P(fw * 0.5f - step * 0.2f, y + fh * 0.5f, off + 0.03f), P(-fw * 0.5f + step * 0.2f, y + fh * 0.5f, off + 0.03f),
                lit ? glow(tp->lit_a, 1.8f) : tp->win_dark);
        continue;
      }
      for (int c = 0; c < cols; c++) {
        float x = -fw * 0.5f + step * ((float)c + 0.5f);
        float ww = step * 0.34f, wh = fh * 0.3f;
        bool lit = pm_rand(b) < lit_prob;
        pm_layer(b, lit ? PL_GLOW : PL_METAL);
        pcol col = lit ? glow(pc_mix(tp->lit_a, tp->lit_b, pm_rand(b)), 1.4f + 1.6f * pm_rand(b)) : pc_mix(tp->win_dark, rgb(0.2f, 0.28f, 0.36f), pm_rand(b) * 0.3f);
        pm_quad(b, P(x - ww, y, off + 0.03f), P(x + ww, y, off + 0.03f), P(x + ww, y + wh * 2, off + 0.03f), P(x - ww, y + wh * 2, off + 0.03f), col);
      }
    }
    pm_pop(b);
  }
  pm_layer(b, PL_MAIN);
}

GEN(gen_skyscraper) {
  pm_vary(b, 0.04f);
  float h = size;
  static const tower_pal pals[5] = {
    { { 0.16f, 0.19f, 0.25f }, { 0.4f, 0.46f, 0.56f }, { 0.07f, 0.1f, 0.15f }, { 1.0f, 0.78f, 0.4f }, { 1.0f, 0.95f, 0.8f } },
    { { 0.2f, 0.16f, 0.26f }, { 0.5f, 0.42f, 0.62f }, { 0.08f, 0.07f, 0.15f }, { 0.3f, 0.9f, 1.0f }, { 1.0f, 0.3f, 0.8f } },
    { { 0.24f, 0.2f, 0.17f }, { 0.58f, 0.5f, 0.42f }, { 0.08f, 0.09f, 0.11f }, { 1.0f, 0.7f, 0.3f }, { 1.0f, 0.9f, 0.6f } },
    { { 0.17f, 0.17f, 0.18f }, { 0.42f, 0.42f, 0.45f }, { 0.07f, 0.09f, 0.12f }, { 0.4f, 0.8f, 1.0f }, { 1.0f, 0.9f, 0.6f } },
    { { 0.12f, 0.19f, 0.19f }, { 0.32f, 0.5f, 0.5f }, { 0.06f, 0.11f, 0.13f }, { 0.4f, 1.0f, 0.8f }, { 0.9f, 1.0f, 0.5f } },
  };
  tower_pal tp = pals[variant % 5];
  tp.wall0 = rgb(tp.wall0.r, tp.wall0.g, tp.wall0.b), tp.wall1 = rgb(tp.wall1.r, tp.wall1.g, tp.wall1.b);
  tp.win_dark = rgb(tp.win_dark.r, tp.win_dark.g, tp.win_dark.b), tp.lit_a = rgb(tp.lit_a.r, tp.lit_a.g, tp.lit_a.b), tp.lit_b = rgb(tp.lit_b.r, tp.lit_b.g, tp.lit_b.b);
  float w = h * 0.3f, d = h * 0.22f;
  float fh = 4.0f, cw = 3.4f;
  float lit = variant == 3 ? 0.07f : 0.32f;
  pm_layer(b, PL_MAIN);
  if (variant == 1) { /* stepped: three tiers */
    float y = 0;
    for (int t = 0; t < 3; t++) {
      float th = h * (t == 0 ? 0.45f : t == 1 ? 0.33f : 0.22f), s = 1.0f - 0.24f * (float)t;
      pm_push(b);
      pm_move(b, 0, y, 0);
      pm_layer(b, PL_MAIN);
      pm_box(b, w * s, th, d * s, tp.wall0, tp.wall1);
      windows(b, w * s, d * s, 1.0f, th - 1.0f, fh, cw, lit, &tp, lod);
      pm_layer(b, PL_MAIN);
      pm_box(b, w * s * 1.06f, 0.5f, d * s * 1.06f, tp.wall0, tp.wall1);
      pm_pop(b);
      y += th;
    }
    pm_layer(b, PL_METAL);
    pm_rod(b, P(0, y, 0), P(0, y + h * 0.1f, 0), 0.35f, 6, rgb(0.4f, 0.4f, 0.44f));
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, 0, y + h * 0.1f, 0);
    pm_ball(b, 0.6f, 0.6f, 0.6f, 0, 0, glow(rgb(1, 0.1f, 0.05f), 4), glow(rgb(1, 0.3f, 0.2f), 5));
    pm_pop(b);
    return;
  }
  float top = h;
  if (variant == 3) { /* the broken one: shaft, then ragged columns and loose slabs */
    top = h * 0.72f;
    pm_box(b, w, top, d, tp.wall0, tp.wall1);
    windows(b, w, d, 1.0f, top - 1.0f, fh, cw, lit, &tp, lod);
    pm_layer(b, PL_MAIN);
    int cols = lod ? 3 : 6;
    for (int i = 0; i < cols; i++) {
      float ch = h * (0.06f + 0.22f * pm_rand(b));
      float x = -w * 0.5f + w * ((float)i + 0.5f) / (float)cols, z = (pm_rand(b) - 0.5f) * d * 0.7f;
      pm_push(b);
      pm_move(b, x, top - 0.2f, z);
      pm_box(b, w / (float)cols * 0.95f, ch, d * (0.2f + 0.2f * pm_rand(b)), dk(tp.wall0, 0.8f), tp.wall1);
      pm_pop(b);
    }
    pm_layer(b, PL_METAL);
    for (int i = 0; i < (lod ? 2 : 7); i++) { /* twisted rebar */
      float x = (pm_rand(b) - 0.5f) * w * 0.8f, z = (pm_rand(b) - 0.5f) * d * 0.8f;
      pm_rod(b, P(x, top, z), P(x + pm_rands(b) * 3, top + 3 + 8 * pm_rand(b), z + pm_rands(b) * 3), 0.12f, 4, rgb(0.35f, 0.18f, 0.1f));
    }
    pm_layer(b, PL_MAIN);
    pm_push(b); /* slabs hanging at an angle */
    pm_move(b, w * 0.2f, top - h * 0.1f, d * 0.5f);
    pm_rotx(b, -0.5f);
    pm_box(b, w * 0.7f, 0.5f, d * 0.5f, tp.wall0, tp.wall1);
    pm_pop(b);
    return;
  }
  pm_box(b, w, top, d, tp.wall0, tp.wall1);
  windows(b, w, d, 1.0f, top - 1.0f, fh, cw, lit, &tp, lod);
  pm_layer(b, PL_MAIN);
  pm_box(b, w * 1.05f, 0.7f, d * 1.05f, tp.wall0, tp.wall1); /* parapet */
  pm_push(b);
  pm_move(b, 0, top, 0);
  if (variant == 2) { /* spire */
    pm_box(b, w * 0.55f, h * 0.06f, d * 0.55f, tp.wall0, tp.wall1);
    pm_layer(b, PL_METAL);
    pm_cyl(b, 0.9f, 0.05f, h * 0.22f, 6, rgb(0.4f, 0.42f, 0.46f), rgb(0.7f, 0.72f, 0.76f));
    pm_layer(b, PL_GLOW);
    pm_move(b, 0, h * 0.22f, 0);
    pm_ball(b, 0.7f, 0.7f, 0.7f, 0, 0, glow(rgb(1, 0.1f, 0.05f), 4), glow(rgb(1, 0.3f, 0.2f), 5));
  } else { /* a plant room and a mast */
    pm_push(b);
    pm_move(b, w * 0.15f, 0.5f, -d * 0.1f);
    pm_box(b, w * 0.3f, h * 0.035f, d * 0.4f, dk(tp.wall0, 1.3f), tp.wall1);
    pm_pop(b);
    pm_layer(b, PL_METAL);
    pm_rod(b, P(-w * 0.25f, 0.5f, 0), P(-w * 0.25f, h * 0.1f, 0), 0.25f, 5, rgb(0.4f, 0.4f, 0.44f));
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, -w * 0.25f, h * 0.1f, 0);
    pm_ball(b, 0.45f, 0.45f, 0.45f, 0, 0, glow(rgb(1, 0.1f, 0.05f), 4), glow(rgb(1, 0.3f, 0.2f), 5));
    pm_pop(b);
  }
  pm_pop(b);
}

GEN(gen_ruin_building) {
  pm_vary(b, 0.09f);
  float h = size;
  static const unsigned cols[4][2] = { { 0x6a5448, 0xb08a70 }, { 0x4a4a50, 0x8a8a92 }, { 0x7a5a3a, 0xc4a070 }, { 0x3c4448, 0x7a8890 } };
  pcol c0 = hex(cols[variant & 3][0]), c1 = hex(cols[variant & 3][1]);
  pcol slab = rgb(0.46f, 0.45f, 0.43f);
  float w = h * 1.3f, d = h * 0.95f, fh = h / 3.0f;
  int gx = 4, gz = 3;
  pm_layer(b, PL_MAIN);
  /* a rubble heap swallows the foot */
  pm_ball(b, w * 0.62f, h * 0.1f, d * 0.6f, 1, 0.25f, dk(c0, 0.5f), dk(c1, 0.7f));
  /* columns on a grid, snapped off at random heights */
  float colh[5][4];
  for (int j = 0; j <= gz; j++)
    for (int i = 0; i <= gx; i++) {
      float x = -w * 0.5f + w * (float)i / (float)gx, z = -d * 0.5f + d * (float)j / (float)gz;
      float ch = fh * (float)(1 + (int)(pm_rand(b) * 2.99f)) - pm_rand(b) * fh * 0.4f;
      colh[i][j] = ch;
      pm_push(b);
      pm_move(b, x, 0, z);
      pm_box(b, h * 0.075f, ch, h * 0.075f, dk(c0, 0.7f), pc_mix(c0, c1, 0.4f));
      pm_pop(b);
    }
  /* floors: slabs over some bays, cracked and tilted */
  for (int f = 1; f <= 3; f++)
    for (int j = 0; j < gz; j++)
      for (int i = 0; i < gx; i++) {
        if (pm_rand(b) < 0.28f * (float)f) continue;
        if (colh[i][j] < fh * (float)f - 0.5f && colh[i + 1][j + 1] < fh * (float)f - 0.5f) continue;
        float x = -w * 0.5f + w * ((float)i + 0.5f) / (float)gx, z = -d * 0.5f + d * ((float)j + 0.5f) / (float)gz;
        pm_push(b);
        pm_move(b, x, fh * (float)f - h * 0.02f, z);
        pm_rotz(b, pm_rands(b) * 0.05f);
        pm_rotx(b, pm_rands(b) * 0.05f);
        pm_box(b, w / (float)gx * 0.97f, h * 0.035f, d / (float)gz * 0.97f, dk(slab, 0.7f), slab);
        pm_pop(b);
      }
  /* wall panels on the rim: pier, lintel and sill round a window hole, missing where it came down */
  for (int wall = 0; wall < 4; wall++) {
    int n = (wall & 1) ? gz : gx;
    float len = (wall & 1) ? d : w;
    pm_push(b);
    pm_roty(b, (float)wall * CW_PI * 0.5f);
    pm_move(b, 0, 0, (wall & 1) ? w * 0.5f : d * 0.5f);
    for (int f = 0; f < 3; f++)
      for (int i = 0; i < n; i++) {
        if (pm_rand(b) < 0.22f + 0.2f * (float)f) continue;
        float px = -len * 0.5f + len * ((float)i + 0.5f) / (float)n, pw = len / (float)n;
        float y = fh * (float)f;
        pm_push(b);
        pm_move(b, px, y, 0);
        pcol pc = pc_mix(c0, c1, pm_rand(b));
        pm_box(b, pw * 0.2f, fh * 0.95f, h * 0.05f, dk(pc, 0.7f), pc);
        pm_push(b);
        pm_move(b, pw * 0.4f, 0, 0);
        pm_box(b, pw * 0.2f, fh * 0.95f, h * 0.05f, dk(pc, 0.7f), pc);
        pm_pop(b);
        pm_push(b);
        pm_move(b, 0, 0, 0);
        pm_box(b, pw * 0.98f, fh * 0.3f, h * 0.05f, dk(pc, 0.7f), pc);
        pm_move(b, 0, fh * 0.7f, 0);
        pm_box(b, pw * 0.98f, fh * 0.25f, h * 0.05f, dk(pc, 0.8f), pc);
        pm_pop(b);
        pm_pop(b);
      }
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  for (int i = 0; i < (lod ? 3 : 14); i++) {
    int ci = (int)(pm_rand(b) * 4.99f), cj = (int)(pm_rand(b) * 3.99f);
    float x = -w * 0.5f + w * (float)ci / (float)gx, z = -d * 0.5f + d * (float)cj / (float)gz;
    float top = colh[ci][cj];
    pm_rod(b, P(x, top, z), P(x + pm_rands(b) * 1.2f, top + 1.5f + 3.0f * pm_rand(b), z + pm_rands(b) * 1.2f), 0.07f, 4, rgb(0.38f, 0.19f, 0.1f));
  }
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < (lod ? 3 : 9); i++) {
    float a = pm_rand(b) * CW_TAU, r = 0.5f + 0.5f * pm_rand(b);
    pm_push(b);
    pm_move(b, cosf(a) * w * 0.55f * r, 0, sinf(a) * d * 0.55f * r);
    pm_rock(b, h * (0.04f + 0.08f * pm_rand(b)), 0.6f, 0.3f, dk(c0, 0.6f), c1);
    pm_pop(b);
  }
  /* faded paint on one wall */
  pm_layer(b, PL_MAIN);
  pm_quad(b, P(-w * 0.3f, fh * 0.2f, d * 0.5f + 0.1f), P(-w * 0.1f, fh * 0.2f, d * 0.5f + 0.1f), P(-w * 0.1f, fh * 0.45f, d * 0.5f + 0.1f), P(-w * 0.3f, fh * 0.45f, d * 0.5f + 0.1f), rgb(0.7f, 0.25f, 0.2f));
}

GEN(gen_water_tower) {
  pm_vary(b, 0.04f);
  float h = size;
  pcol st = rgb(0.3f, 0.32f, 0.36f), wood = variant == 1 ? rgb(0.78f, 0.78f, 0.74f) : rgb(0.46f, 0.3f, 0.18f);
  pm_layer(b, PL_METAL);
  float lw = h * 0.22f;
  for (int s = 0; s < 6; s++) {
    float a = (float)s / 6.0f * CW_TAU;
    pm_beam(b, P(cosf(a) * lw * 1.1f, 0, sinf(a) * lw * 1.1f), P(cosf(a) * lw, h * 0.62f, sinf(a) * lw), h * 0.018f, st);
    float a2 = a + CW_TAU / 6.0f;
    for (int k = 0; k < 2; k++) {
      float y0 = h * (0.1f + 0.25f * (float)k), y1 = y0 + h * 0.25f;
      pm_beam(b, P(cosf(a) * lw * (1.1f - 0.1f * y0 / (h * 0.62f)), y0, sinf(a) * lw * (1.1f - 0.1f * y0 / (h * 0.62f))),
              P(cosf(a2) * lw * (1.1f - 0.1f * y1 / (h * 0.62f)), y1, sinf(a2) * lw * (1.1f - 0.1f * y1 / (h * 0.62f))), h * 0.008f, st);
    }
  }
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, h * 0.62f, 0);
  pm_cyl_ex(b, h * 0.27f, h * 0.27f, h * 0.28f, lod ? 10 : 16, dk(wood, 0.6f), wood, true, false);
  pm_move(b, 0, h * 0.28f, 0);
  pm_cyl(b, h * 0.285f, 0.001f, h * 0.12f, lod ? 10 : 16, dk(wood, 0.8f), pc_mul(wood, 1.15f));
  pm_pop(b);
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 3; i++) {
    pm_push(b);
    pm_move(b, 0, h * (0.66f + 0.09f * (float)i), 0);
    pm_cyl_ex(b, h * 0.275f, h * 0.275f, h * 0.012f, 16, st, st, false, false);
    pm_pop(b);
  }
  pm_beam(b, P(h * 0.27f, h * 0.06f, 0), P(h * 0.27f, h * 0.88f, 0), h * 0.008f, st);
}

GEN(gen_windmill) {
  pm_vary(b, 0.04f);
  float h = size;
  pcol brick = variant == 1 ? rgb(0.75f, 0.74f, 0.7f) : rgb(0.58f, 0.3f, 0.2f), cap = rgb(0.2f, 0.16f, 0.14f);
  pm_layer(b, PL_MAIN);
  pm_cyl_ex(b, h * 0.2f, h * 0.12f, h * 0.7f, lod ? 8 : 12, dk(brick, 0.6f), brick, false, false);
  pm_push(b);
  pm_move(b, 0, h * 0.7f, 0);
  pm_cyl_ex(b, h * 0.14f, h * 0.13f, h * 0.04f, 12, cap, cap, false, true);
  pm_cyl(b, h * 0.13f, 0.001f, h * 0.14f, 12, cap, pc_mul(cap, 1.5f));
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, h * 0.5f, h * 0.15f);
  pm_box(b, h * 0.05f, h * 0.08f, h * 0.02f, rgb(0.05f, 0.05f, 0.06f), rgb(0.1f, 0.1f, 0.12f));
  pm_pop(b);
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, 0, h * 0.74f, h * 0.2f);
  pm_rotz(b, 0.35f + (variant == 1 ? 0.4f : 0.0f));
  for (int s = 0; s < 4; s++) {
    pm_push(b);
    pm_rotz(b, (float)s * CW_PI * 0.5f);
    pcol wd = rgb(0.4f, 0.28f, 0.18f);
    pm_beam(b, P(0, 0, 0), P(0, h * 0.5f, 0), h * 0.016f, wd);
    pm_beam(b, P(h * 0.1f, h * 0.08f, 0), P(h * 0.1f, h * 0.48f, 0), h * 0.01f, wd);
    if (variant == 1 && s == 2) { pm_pop(b); continue; } /* a broken sail */
    for (int i = 0; i < (lod ? 3 : 7); i++) {
      float y = h * 0.1f + h * 0.055f * (float)i;
      pm_beam(b, P(0, y, 0), P(h * 0.1f, y, 0), h * 0.01f, wd);
    }
    pm_layer(b, PL_MAIN);
    pm_quad(b, P(0, h * 0.1f, -0.01f), P(h * 0.1f, h * 0.1f, -0.01f), P(h * 0.1f, h * 0.45f, -0.01f), P(0, h * 0.45f, -0.01f), rgb(0.82f, 0.78f, 0.66f));
    pm_layer(b, PL_METAL);
    pm_pop(b);
  }
  pm_pop(b);
}

GEN(gen_wind_pump) {
  pm_vary(b, 0.04f);
  float h = size;
  pcol st = rgb(0.5f, 0.52f, 0.56f);
  pm_layer(b, PL_METAL);
  lattice(b, h * 0.3f, h * 0.12f, h * 0.7f, lod ? 3 : 5, h * 0.008f, st);
  pm_push(b);
  pm_move(b, 0, h * 0.72f, h * 0.06f);
  pm_box(b, h * 0.06f, h * 0.06f, h * 0.18f, rgb(0.3f, 0.3f, 0.32f), rgb(0.45f, 0.45f, 0.47f));
  pm_push(b);
  pm_move(b, 0, h * 0.03f, h * 0.1f);
  int blades = lod ? 10 : 18;
  for (int i = 0; i < blades; i++) {
    pm_push(b);
    pm_rotz(b, (float)i / (float)blades * CW_TAU);
    pm_quad(b, P(-h * 0.012f, h * 0.05f, 0), P(h * 0.012f, h * 0.05f, 0), P(h * 0.02f, h * 0.22f, h * 0.01f), P(-h * 0.02f, h * 0.22f, h * 0.01f), rgb(0.62f, 0.64f, 0.68f));
    pm_pop(b);
  }
  pm_ring(b, h * 0.22f, h * 0.004f, 20, 3, st);
  pm_ring(b, h * 0.1f, h * 0.004f, 14, 3, st);
  pm_pop(b);
  pm_beam(b, P(0, h * 0.03f, -h * 0.05f), P(0, h * 0.03f, -h * 0.3f), h * 0.008f, st);
  pm_quad(b, P(0, h * 0.0f, -h * 0.26f), P(0, h * 0.0f, -h * 0.4f), P(0, h * 0.12f, -h * 0.4f), P(0, h * 0.1f, -h * 0.26f), rgb(0.7f, 0.2f, 0.12f));
  pm_quad(b, P(0, h * 0.0f, -h * 0.4f), P(0, h * 0.0f, -h * 0.26f), P(0, h * 0.1f, -h * 0.26f), P(0, h * 0.12f, -h * 0.4f), rgb(0.7f, 0.2f, 0.12f));
  pm_pop(b);
}

/* ── fairground ──────────────────────────────────────────────────────────────────────────────────── */

/* a striped cone roof of n segments, alternating colours, apex at height h */
static void striped_cone(pm *b, float r, float h, int n, pcol a, pcol c, float yb) {
  for (int i = 0; i < n; i++) {
    float a0 = CW_TAU * (float)i / (float)n, a1 = CW_TAU * (float)(i + 1) / (float)n;
    pcol col = (i & 1) ? a : c;
    t3_vec3 p0 = P(cosf(a0) * r, yb, -sinf(a0) * r), p1 = P(cosf(a1) * r, yb, -sinf(a1) * r), ap = P(0, yb + h, 0);
    pm_tri(b, p0, p1, ap, col);
  }
}
static void striped_wall(pm *b, float r, float h, int n, pcol a, pcol c, float yb) {
  for (int i = 0; i < n; i++) {
    float a0 = CW_TAU * (float)i / (float)n, a1 = CW_TAU * (float)(i + 1) / (float)n;
    pcol col = (i & 1) ? a : c;
    t3_vec3 p0 = P(cosf(a0) * r, yb, -sinf(a0) * r), p1 = P(cosf(a1) * r, yb, -sinf(a1) * r);
    pm_quad(b, p0, p1, P(p1.x, yb + h, p1.z), P(p0.x, yb + h, p0.z), col);
  }
}

GEN(gen_tent) {
  pm_vary(b, 0.05f);
  static const unsigned pal[4][2] = { { 0xd8302a, 0xf4ecd8 }, { 0x2a6ad8, 0xf4ecd8 }, { 0xe8a820, 0x8a2a8a }, { 0x2aa86a, 0xf4ecd8 } };
  pcol a = hex(pal[variant][0]), c = hex(pal[variant][1]);
  float h = size;
  pm_layer(b, PL_MAIN);
  if (variant == 3) { /* a stall: counter and awning */
    pm_box(b, h * 1.4f, h * 0.35f, h * 0.5f, rgb(0.3f, 0.2f, 0.12f), rgb(0.55f, 0.4f, 0.26f));
    for (int k = -1; k <= 1; k += 2) pm_beam(b, P(k * h * 0.65f, 0, h * 0.2f), P(k * h * 0.65f, h * 0.75f, h * 0.2f), h * 0.04f, rgb(0.3f, 0.2f, 0.12f));
    for (int i = 0; i < 7; i++) {
      float x0 = -h * 0.7f + h * 0.2f * (float)i;
      pm_quad(b, P(x0, h * 0.75f, h * 0.2f), P(x0 + h * 0.2f, h * 0.75f, h * 0.2f), P(x0 + h * 0.2f, h * 0.55f, h * 0.55f), P(x0, h * 0.55f, h * 0.55f), (i & 1) ? a : c);
      pm_quad(b, P(x0, h * 0.55f, h * 0.55f), P(x0 + h * 0.2f, h * 0.55f, h * 0.55f), P(x0 + h * 0.2f, h * 0.75f, h * 0.2f), P(x0, h * 0.75f, h * 0.2f), (i & 1) ? a : c);
    }
    return;
  }
  int n = lod ? 8 : 12;
  float r = h * 0.55f;
  striped_wall(b, r, h * 0.3f, n, a, c, 0);
  striped_cone(b, r * 1.08f, h * 0.55f, n, a, c, h * 0.3f);
  /* the scalloped valance */
  if (!lod)
    for (int i = 0; i < n; i++) {
      float a0 = CW_TAU * (float)i / (float)n, a1 = CW_TAU * (float)(i + 1) / (float)n, am = (a0 + a1) * 0.5f;
      pm_tri(b, P(cosf(a0) * r, h * 0.3f, -sinf(a0) * r), P(cosf(a1) * r, h * 0.3f, -sinf(a1) * r), P(cosf(am) * r * 1.01f, h * 0.2f, -sinf(am) * r * 1.01f), (i & 1) ? c : a);
    }
  pm_push(b);
  pm_move(b, 0, h * 0.85f, 0);
  pm_beam(b, P(0, 0, 0), P(0, h * 0.2f, 0), h * 0.02f, rgb(0.3f, 0.2f, 0.12f));
  pm_quad(b, P(0, h * 0.2f, 0), P(h * 0.14f, h * 0.18f, 0), P(h * 0.14f, h * 0.1f, 0), P(0, h * 0.12f, 0), rgb(0.95f, 0.8f, 0.1f));
  pm_quad(b, P(0, h * 0.12f, 0), P(h * 0.14f, h * 0.1f, 0), P(h * 0.14f, h * 0.18f, 0), P(0, h * 0.2f, 0), rgb(0.95f, 0.8f, 0.1f));
  pm_pop(b);
}

GEN(gen_bigtop) {
  pm_vary(b, 0.04f);
  float h = size;
  static const unsigned pal[3][2] = { { 0xd8302a, 0xf4ecd8 }, { 0x6a2ad8, 0xf0c830 }, { 0x2a8ad8, 0xf4ecd8 } };
  pcol a = hex(pal[variant][0]), c = hex(pal[variant][1]);
  float r = h * 0.9f, wall = h * 0.28f;
  int n = lod ? 10 : 20;
  pm_layer(b, PL_MAIN);
  striped_wall(b, r, wall, n, a, c, 0);
  /* the roof sags between poles: a lower and an upper cone */
  striped_cone(b, r * 1.05f, h * 0.2f, n, a, c, wall);
  pm_push(b);
  pm_move(b, 0, wall + h * 0.2f, 0);
  striped_cone(b, r * 0.45f, h * 0.2f, n / 2, a, c, 0);
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, wall + h * 0.4f, 0);
  pm_beam(b, P(0, 0, 0), P(0, h * 0.3f, 0), h * 0.02f, rgb(0.35f, 0.25f, 0.18f));
  for (int i = 0; i < 2; i++) {
    pm_quad(b, P(0, h * 0.3f, 0), P(h * 0.22f, h * 0.27f, 0), P(h * 0.22f, h * 0.18f, 0), P(0, h * 0.2f, 0), i ? a : rgb(0.95f, 0.85f, 0.15f));
    pm_quad(b, P(0, h * 0.2f, 0), P(h * 0.22f, h * 0.18f, 0), P(h * 0.22f, h * 0.27f, 0), P(0, h * 0.3f, 0), i ? a : rgb(0.95f, 0.85f, 0.15f));
    break;
  }
  pm_pop(b);
  /* the entrance: a dark mouth with a gable */
  pm_push(b);
  pm_move(b, 0, 0, r * 0.995f);
  pm_quad(b, P(-h * 0.14f, 0, 0), P(h * 0.14f, 0, 0), P(h * 0.14f, wall * 0.9f, 0), P(-h * 0.14f, wall * 0.9f, 0), rgb(0.04f, 0.03f, 0.04f));
  pm_pop(b);
  /* string lights round the eave */
  pm_layer(b, PL_GLOW);
  int lights = lod ? 10 : 24;
  for (int i = 0; i < lights; i++) {
    float an = CW_TAU * (float)i / (float)lights;
    pm_push(b);
    pm_move(b, cosf(an) * r * 1.01f, wall * 1.0f + 0.05f * sinf((float)i * 3), -sinf(an) * r * 1.01f);
    pm_ball(b, h * 0.014f, h * 0.014f, h * 0.014f, 0, 0, glow((i % 3 == 0) ? rgb(1, 0.4f, 0.3f) : (i % 3 == 1) ? rgb(1, 0.9f, 0.4f) : rgb(0.4f, 0.8f, 1), 4.0f),
            glow((i % 3 == 0) ? rgb(1, 0.6f, 0.5f) : (i % 3 == 1) ? rgb(1, 1, 0.7f) : rgb(0.6f, 0.9f, 1), 5.0f));
    pm_pop(b);
  }
}

GEN(gen_ferris_wheel) {
  pm_vary(b, 0.04f);
  float h = size, R = h * 0.4f, hubY = h * 0.5f;
  pcol st = variant == 1 ? rgb(0.55f, 0.3f, 0.2f) : rgb(0.85f, 0.85f, 0.88f);
  pm_layer(b, PL_METAL);
  for (int z = -1; z <= 1; z += 2) {
    float zz = z * h * 0.07f;
    pm_beam(b, P(-R * 0.55f, 0, zz * 1.4f), P(0, hubY, zz), h * 0.018f, st);
    pm_beam(b, P(R * 0.55f, 0, zz * 1.4f), P(0, hubY, zz), h * 0.018f, st);
  }
  pm_beam(b, P(-R * 0.55f, h * 0.02f, -h * 0.1f), P(-R * 0.55f, h * 0.02f, h * 0.1f), h * 0.016f, st);
  pm_beam(b, P(R * 0.55f, h * 0.02f, -h * 0.1f), P(R * 0.55f, h * 0.02f, h * 0.1f), h * 0.016f, st);
  pm_push(b);
  pm_move(b, 0, hubY, 0);
  if (variant == 1) pm_rotz(b, 0.3f);
  pm_cyl_ex(b, h * 0.03f, h * 0.03f, h * 0.16f, 8, rgb(0.3f, 0.3f, 0.34f), rgb(0.5f, 0.5f, 0.54f), true, true);
  pm_move(b, 0, 0, 0);
  for (int z = -1; z <= 1; z += 2) {
    pm_push(b);
    pm_move(b, 0, 0, z * h * 0.07f);
    pm_ring(b, R, h * 0.01f, lod ? 18 : 36, 4, st);
    pm_ring(b, R * 0.7f, h * 0.006f, lod ? 12 : 24, 3, st);
    int spokes = lod ? 8 : 16;
    for (int i = 0; i < spokes; i++) {
      float an = (float)i / (float)spokes * CW_TAU;
      pm_beam(b, P(0, 0, 0), P(cosf(an) * R, sinf(an) * R, 0), h * 0.005f, st);
    }
    pm_pop(b);
  }
  int gond = 12;
  static const unsigned gc[4] = { 0xe0382a, 0xf0b820, 0x2a8ad8, 0x3ab86a };
  for (int i = 0; i < gond; i++) {
    if (variant == 1 && (i == 3 || i == 7 || i == 8)) continue;
    float an = (float)i / (float)gond * CW_TAU + 0.26f;
    pm_push(b);
    pm_move(b, cosf(an) * R, sinf(an) * R, 0);
    pm_rotz(b, variant == 1 ? 0.3f : 0.0f);
    pm_layer(b, PL_METAL);
    pm_beam(b, P(0, 0, 0), P(0, -h * 0.03f, 0), h * 0.006f, st);
    pm_layer(b, PL_MAIN);
    pm_move(b, 0, -h * 0.065f, 0);
    pcol gc0 = hex(gc[i & 3]);
    pm_box_c(b, h * 0.05f, h * 0.05f, h * 0.12f, gc0);
    pm_pop(b);
  }
  pm_pop(b);
  if (variant == 0) {
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, 0, hubY, h * 0.1f);
    int bulbs = lod ? 12 : 24;
    for (int i = 0; i < bulbs; i++) {
      float an = (float)i / (float)bulbs * CW_TAU;
      pm_push(b);
      pm_move(b, cosf(an) * R, sinf(an) * R, 0);
      pm_ball(b, h * 0.01f, h * 0.01f, h * 0.01f, 0, 0, glow((i & 1) ? rgb(1, 0.5f, 0.2f) : rgb(0.5f, 0.8f, 1), 4.0f), glow((i & 1) ? rgb(1, 0.8f, 0.5f) : rgb(0.8f, 0.95f, 1), 5.0f));
      pm_pop(b);
    }
    pm_pop(b);
  }
}

GEN(gen_carousel) {
  pm_vary(b, 0.05f);
  float h = size, r = h * 0.5f;
  static const unsigned pal[2][2] = { { 0xd8302a, 0xf4ecd8 }, { 0x2a6ad8, 0xf0c830 } };
  pcol a = hex(pal[variant][0]), c = hex(pal[variant][1]);
  pm_layer(b, PL_MAIN);
  pm_cyl_ex(b, r * 1.05f, r * 1.05f, h * 0.08f, 20, rgb(0.25f, 0.2f, 0.16f), rgb(0.5f, 0.42f, 0.34f), true, true);
  pm_layer(b, PL_METAL);
  pm_cyl(b, h * 0.04f, h * 0.04f, h * 0.7f, 8, rgb(0.7f, 0.6f, 0.2f), rgb(0.95f, 0.85f, 0.4f));
  int horses = lod ? 6 : 8;
  for (int i = 0; i < horses; i++) {
    float an = (float)i / (float)horses * CW_TAU;
    float x = cosf(an) * r * 0.72f, z = sinf(an) * r * 0.72f;
    float bob = 0.12f * h * ((i & 1) ? 1.0f : 0.0f);
    pm_layer(b, PL_METAL);
    pm_rod(b, P(x, h * 0.08f, z), P(x, h * 0.75f, z), h * 0.008f, 5, rgb(0.9f, 0.8f, 0.3f));
    pm_layer(b, PL_MAIN);
    pm_push(b);
    pm_move(b, x, h * 0.2f + bob, z);
    pm_roty(b, -an + CW_PI * 0.5f);
    pcol hc = (i & 1) ? rgb(0.95f, 0.92f, 0.85f) : rgb(0.5f, 0.3f, 0.18f);
    pm_box(b, h * 0.09f, h * 0.1f, h * 0.2f, dk(hc, 0.7f), hc);
    pm_push(b);
    pm_move(b, 0, h * 0.05f, h * 0.1f);
    pm_rotx(b, -0.5f);
    pm_box(b, h * 0.06f, h * 0.14f, h * 0.06f, dk(hc, 0.8f), hc);
    pm_move(b, 0, h * 0.14f, h * 0.0f);
    pm_rotx(b, 0.8f);
    pm_box(b, h * 0.05f, h * 0.07f, h * 0.05f, dk(hc, 0.8f), hc);
    pm_pop(b);
    for (int l = 0; l < 4; l++) pm_beam(b, P((l & 1 ? 1 : -1) * h * 0.03f, 0, (l & 2 ? 1 : -1) * h * 0.08f), P((l & 1 ? 1 : -1) * h * 0.03f, -h * 0.1f, (l & 2 ? 1 : -1) * h * 0.08f), h * 0.016f, dk(hc, 0.6f));
    pm_pop(b);
  }
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, h * 0.75f, 0);
  pm_cyl_ex(b, r * 1.1f, r * 1.1f, h * 0.06f, 20, c, c, false, false);
  striped_cone(b, r * 1.2f, h * 0.22f, 16, a, c, h * 0.06f);
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < (lod ? 8 : 20); i++) {
    float an = (float)i / (lod ? 8.0f : 20.0f) * CW_TAU;
    pm_push(b);
    pm_move(b, cosf(an) * r * 1.1f, h * 0.745f, sinf(an) * r * 1.1f);
    pm_ball(b, h * 0.012f, h * 0.012f, h * 0.012f, 0, 0, glow(rgb(1, 0.85f, 0.4f), 4.0f), glow(rgb(1, 0.95f, 0.7f), 5.0f));
    pm_pop(b);
  }
}

GEN(gen_coaster_lift) {
  pm_vary(b, 0.04f);
  float h = size;
  static const unsigned pal[3][2] = { { 0xe03020, 0xe8e8e0 }, { 0xf0b020, 0x30343c }, { 0x20a090, 0xd8d8d0 } };
  pcol rail = hex(pal[variant][0]), sup = hex(pal[variant][1]);
  if (variant == 1) sup = rgb(0.2f, 0.2f, 0.24f);
  float L = h * 2.0f, up = h, tw = h * 0.07f;
  pm_layer(b, PL_METAL);
  /* the ramp: height h at z = +L/2 falling away in a crest and a drop on the far side */
  int bays = lod ? 6 : 14;
  for (int i = 0; i <= bays; i++) {
    float t = (float)i / (float)bays;
    float z = -L * 0.5f + L * 0.8f * t, y = up * t * 0.95f;
    /* support bent: two legs and cross members */
    for (int k = -1; k <= 1; k += 2) pm_beam(b, P(k * tw * 1.2f, 0, z + k * h * 0.02f), P(k * tw * 0.6f, y, z), h * 0.012f, sup);
    if (i > 0) {
      float zp = -L * 0.5f + L * 0.8f * (float)(i - 1) / (float)bays, yp = up * (t - 1.0f / (float)bays) * 0.95f;
      for (int k = -1; k <= 1; k += 2) {
        pm_beam(b, P(k * tw * 0.6f, yp, zp), P(k * tw * 0.6f, y, z), h * 0.01f, rail); /* the rails */
        if (!lod && (i & 1)) pm_beam(b, P(k * tw * 1.1f, yp * 0.4f, zp), P(k * tw * 0.7f, y * 0.7f, z), h * 0.006f, sup);
      }
      pm_beam(b, P(0, yp, zp), P(0, y, z), h * 0.01f, rgb(0.15f, 0.15f, 0.17f)); /* the chain */
    }
    pm_beam(b, P(-tw * 0.7f, y, z), P(tw * 0.7f, y, z), h * 0.01f, dk(sup, 0.9f));
  }
  /* the crest and the first drop */
  float zc = -L * 0.5f + L * 0.8f, yc = up * 0.95f;
  t3_vec3 prev = P(0, yc, zc);
  for (int i = 1; i <= (lod ? 3 : 6); i++) {
    float t = (float)i / (lod ? 3.0f : 6.0f);
    t3_vec3 cur = P(0, yc - h * 0.9f * t * t - h * 0.05f * t, zc + L * 0.2f * t);
    for (int k = -1; k <= 1; k += 2) pm_beam(b, P(k * tw * 0.6f, prev.y, prev.z), P(k * tw * 0.6f, cur.y, cur.z), h * 0.01f, rail);
    for (int k = -1; k <= 1; k += 2) pm_beam(b, P(k * tw * 0.6f, cur.y, cur.z), P(k * tw * 1.1f, 0, cur.z), h * 0.008f, sup);
    prev = cur;
  }
  /* the train, near the top of the lift */
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < 3; i++) {
    float t = 0.78f - 0.07f * (float)i;
    float z = -L * 0.5f + L * 0.8f * t, y = up * t * 0.95f;
    pm_push(b);
    pm_move(b, 0, y + h * 0.015f, z);
    pm_rotx(b, -atanf(up * 0.95f / (L * 0.8f)));
    pm_box(b, h * 0.085f, h * 0.045f, h * 0.075f, dk(hex(pal[(variant + i) % 3][0]), 0.7f), hex(pal[(variant + i) % 3][0]));
    pm_pop(b);
  }
}

/* ── stone and temple ────────────────────────────────────────────────────────────────────────────── */

static void moss(pm *b, float x, float y, float z, float r) {
  pm_layer(b, PL_LEAF);
  pm_push(b);
  pm_move(b, x, y, z);
  pm_ball(b, r, r * 0.3f, r, 1, 0.3f, rgb(0.08f, 0.26f, 0.1f), rgb(0.3f, 0.55f, 0.2f));
  pm_pop(b);
  pm_layer(b, PL_MAIN);
}

static pcol stone_lo(int v) { return v == 1 ? rgb(0.26f, 0.27f, 0.25f) : rgb(0.3f, 0.27f, 0.22f); }
static pcol stone_hi(int v) { return v == 1 ? rgb(0.62f, 0.64f, 0.6f) : rgb(0.72f, 0.66f, 0.54f); }

GEN(gen_temple_block) {
  pm_vary(b, 0.08f);
  float h = size;
  pcol lo = stone_lo(variant & 1), hi = stone_hi(variant & 1);
  pm_layer(b, PL_MAIN);
  int tiers = variant == 1 ? 4 : 2;
  float w = h * (variant == 1 ? 1.8f : 1.1f), y = 0;
  for (int i = 0; i < tiers; i++) {
    float th = h * (variant == 1 ? 0.22f : 0.5f);
    pm_push(b);
    pm_move(b, 0, y, 0);
    pm_roty(b, pm_rands(b) * 0.04f);
    pm_box(b, w, th, w, lo, hi);
    /* carved panels: inset darker rectangles on the faces */
    if (!lod)
      for (int f = 0; f < 4; f++) {
        pm_push(b);
        pm_roty(b, (float)f * CW_PI * 0.5f);
        pm_quad(b, P(-w * 0.35f, th * 0.2f, w * 0.502f), P(w * 0.35f, th * 0.2f, w * 0.502f), P(w * 0.35f, th * 0.8f, w * 0.502f), P(-w * 0.35f, th * 0.8f, w * 0.502f), dk(lo, 0.8f));
        if (variant == 2) { /* a carved face: two eye hollows and a mouth bar */
          pm_quad(b, P(-w * 0.2f, th * 0.55f, w * 0.503f), P(-w * 0.07f, th * 0.55f, w * 0.503f), P(-w * 0.07f, th * 0.7f, w * 0.503f), P(-w * 0.2f, th * 0.7f, w * 0.503f), rgb(0.04f, 0.04f, 0.04f));
          pm_quad(b, P(w * 0.07f, th * 0.55f, w * 0.503f), P(w * 0.2f, th * 0.55f, w * 0.503f), P(w * 0.2f, th * 0.7f, w * 0.503f), P(w * 0.07f, th * 0.7f, w * 0.503f), rgb(0.04f, 0.04f, 0.04f));
          pm_quad(b, P(-w * 0.18f, th * 0.3f, w * 0.503f), P(w * 0.18f, th * 0.3f, w * 0.503f), P(w * 0.18f, th * 0.4f, w * 0.503f), P(-w * 0.18f, th * 0.4f, w * 0.503f), rgb(0.05f, 0.05f, 0.05f));
        }
        pm_pop(b);
      }
    pm_pop(b);
    y += th;
    w *= 0.78f;
  }
  if (variant == 3) { /* toppled top block */
    pm_push(b);
    pm_move(b, w * 0.9f, 0, w * 0.5f);
    pm_rotz(b, 0.5f);
    pm_box(b, w * 0.8f, h * 0.35f, w * 0.8f, lo, hi);
    pm_pop(b);
  }
  moss(b, h * 0.2f, y, -h * 0.1f, h * 0.3f);
  moss(b, -h * 0.35f, h * 0.05f, h * 0.3f, h * 0.18f);
}

GEN(gen_pillar) {
  pm_vary(b, 0.07f);
  float h = size, r = h * 0.075f;
  pcol lo = stone_lo(variant & 1), hi = stone_hi(variant & 1);
  pm_layer(b, PL_MAIN);
  pm_box(b, r * 3.0f, h * 0.05f, r * 3.0f, dk(lo, 0.8f), lo);
  pm_push(b);
  pm_move(b, 0, h * 0.05f, 0);
  pm_cyl_ex(b, r * 1.15f, r, h * 0.04f, 10, lo, hi, false, false);
  pm_jitter(b, variant == 1 ? 0.0f : 0.0f);
  float shaft = h * (variant == 1 ? 0.55f : 0.78f);
  pm_move(b, 0, h * 0.04f, 0);
  pm_cyl_ex(b, r, r * 0.88f, shaft, lod ? 8 : 12, lo, hi, false, variant == 1);
  if (variant == 1) { /* the broken top: a few ragged stubs and fallen drums */
    for (int i = 0; i < 5; i++) {
      float a = (float)i / 5.0f * CW_TAU;
      pm_push(b);
      pm_move(b, cosf(a) * r * 0.5f, shaft, sinf(a) * r * 0.5f);
      pm_box(b, r * 0.7f, h * 0.05f * (1 + (float)(i % 3)), r * 0.5f, hi, dk(hi, 1.1f));
      pm_pop(b);
    }
  } else {
    pm_move(b, 0, shaft, 0);
    pm_cyl_ex(b, r * 0.9f, r * 1.2f, h * 0.04f, 10, hi, hi, false, false);
    pm_move(b, 0, h * 0.04f, 0);
    pm_box(b, r * 3.0f, h * 0.05f, r * 3.0f, hi, pc_mul(hi, 1.1f));
  }
  pm_pop(b);
  if (variant == 1) {
    pm_push(b);
    pm_move(b, r * 3.5f, r, 0);
    pm_rotz(b, CW_PI * 0.5f);
    pm_cyl_ex(b, r, r, h * 0.3f, 10, lo, hi, true, true);
    pm_pop(b);
  }
  if (variant == 2) { /* vine wrapped */
    pm_layer(b, PL_LEAF);
    int n = lod ? 10 : 22;
    t3_vec3 prev = P(r * 1.02f, h * 0.1f, 0);
    for (int i = 1; i <= n; i++) {
      float t = (float)i / (float)n, an = t * 14.0f;
      t3_vec3 cur = P(cosf(an) * r * (1.03f - 0.1f * t), h * 0.1f + shaft * 0.95f * t, sinf(an) * r * (1.03f - 0.1f * t));
      pm_rod(b, prev, cur, h * 0.007f, 4, rgb(0.1f, 0.3f, 0.12f));
      if (!lod && (i & 1)) {
        pm_push(b);
        pm_move(b, cur.x, cur.y, cur.z);
        pm_ball(b, h * 0.022f, h * 0.012f, h * 0.022f, 0, 0.3f, rgb(0.1f, 0.4f, 0.14f), rgb(0.34f, 0.62f, 0.22f));
        pm_pop(b);
      }
      prev = cur;
    }
  }
  moss(b, r * 1.4f, 0.05f, 0, r * 0.9f);
}

GEN(gen_statue) {
  pm_vary(b, 0.07f);
  float h = size;
  pcol lo = stone_lo(variant & 1), hi = stone_hi(variant & 1);
  pm_layer(b, PL_MAIN);
  pm_box(b, h * 0.46f, h * 0.08f, h * 0.46f, dk(lo, 0.7f), lo);
  pm_push(b);
  pm_move(b, 0, h * 0.08f, 0);
  pm_box(b, h * 0.38f, h * 0.1f, h * 0.38f, lo, hi);
  pm_move(b, 0, h * 0.1f, 0);
  int seg = lod ? 6 : 8;
  if (variant == 2) { /* a seated idol with glowing eyes: broad, heavy, carved */
    pm_box(b, h * 0.34f, h * 0.18f, h * 0.3f, lo, hi);
    pm_move(b, 0, h * 0.18f, 0);
    pm_cyl_ex(b, h * 0.19f, h * 0.15f, h * 0.3f, seg, lo, hi, false, true);
    pm_push(b);
    for (int k = -1; k <= 1; k += 2) { /* arms resting on the knees */
      pm_limb(b, P(k * h * 0.17f, h * 0.27f, 0), P(k * h * 0.2f, h * 0.05f, h * 0.1f), h * 0.05f, h * 0.045f, 6, hi, lo);
    }
    pm_pop(b);
    pm_move(b, 0, h * 0.3f, 0);
    pm_ball(b, h * 0.1f, h * 0.11f, h * 0.1f, 1, 0.06f, lo, hi);
    pm_move(b, 0, h * 0.1f, 0);
    pm_cyl_ex(b, h * 0.13f, h * 0.06f, h * 0.12f, seg, hi, hi, false, true); /* a tall crown */
    pm_layer(b, PL_GLOW);
    for (int k = -1; k <= 1; k += 2)
      pm_quad(b, P(k * h * 0.045f - h * 0.022f, -h * 0.075f, h * 0.097f), P(k * h * 0.045f + h * 0.022f, -h * 0.075f, h * 0.097f), P(k * h * 0.045f + h * 0.022f, -h * 0.045f, h * 0.097f), P(k * h * 0.045f - h * 0.022f, -h * 0.045f, h * 0.097f), glow(rgb(1.0f, 0.75f, 0.2f), 4.0f));
    pm_pop(b);
    return;
  }
  /* a standing guardian: legs, kilt, chest, shoulders, arms, head and a crested helm */
  for (int k = -1; k <= 1; k += 2) pm_limb(b, P(k * h * 0.05f, 0, 0), P(k * h * 0.055f, h * 0.22f, 0), h * 0.045f, h * 0.04f, seg, lo, hi);
  pm_move(b, 0, h * 0.2f, 0);
  pm_cyl_ex(b, h * 0.09f, h * 0.12f, h * 0.1f, seg, lo, hi, false, false);
  pm_move(b, 0, h * 0.1f, 0);
  pm_cyl_ex(b, h * 0.1f, h * 0.085f, h * 0.2f, seg, lo, hi, false, true);
  for (int k = -1; k <= 1; k += 2) {
    pm_push(b);
    pm_move(b, k * h * 0.12f, h * 0.18f, 0);
    pm_ball(b, h * 0.055f, h * 0.05f, h * 0.055f, 0, 0.05f, lo, hi);
    pm_limb(b, P(0, 0, 0), P(k * h * 0.035f, -h * 0.18f, h * (variant == 1 && k > 0 ? 0.07f : 0.03f)), h * 0.035f, h * 0.03f, 6, hi, lo);
    pm_pop(b);
  }
  pm_move(b, 0, h * 0.24f, 0);
  pm_ball(b, h * 0.055f, h * 0.065f, h * 0.055f, 1, 0.05f, lo, hi);
  pm_move(b, 0, h * 0.045f, 0);
  pm_cyl_ex(b, h * 0.06f, h * 0.045f, h * 0.05f, seg, hi, hi, false, true);
  pm_prism(b, h * 0.02f, h * 0.07f, h * 0.12f, hi, hi);
  pm_pop(b);
  if (variant == 1) { /* a spear and a round shield */
    pm_rod(b, P(h * 0.16f, h * 0.18f, h * 0.07f), P(h * 0.16f, h * 0.96f, h * 0.07f), h * 0.01f, 5, hi);
    pm_tri(b, P(h * 0.145f, h * 0.96f, h * 0.07f), P(h * 0.175f, h * 0.96f, h * 0.07f), P(h * 0.16f, h * 1.08f, h * 0.07f), hi);
    pm_tri(b, P(h * 0.175f, h * 0.96f, h * 0.07f), P(h * 0.145f, h * 0.96f, h * 0.07f), P(h * 0.16f, h * 1.08f, h * 0.07f), hi);
    pm_push(b);
    pm_move(b, -h * 0.17f, h * 0.45f, h * 0.05f);
    pm_rotx(b, CW_PI * 0.5f);
    pm_cyl_ex(b, h * 0.09f, h * 0.09f, h * 0.02f, 10, lo, hi, true, true);
    pm_pop(b);
  }
  moss(b, h * 0.1f, h * 0.18f, h * 0.15f, h * 0.08f);
}

/* a half ring of stone: radius r_in to r_out, depth d, from angle a0 to a1 over n blocks (faces, so no gaps show) */
static void arch_ring(pm *b, float r_in, float r_out, float d, float a0, float a1, int n, pcol c0, pcol c1, bool jag) {
  for (int i = 0; i < n; i++) {
    float aa = a0 + (a1 - a0) * (float)i / (float)n, bb = a0 + (a1 - a0) * (float)(i + 1) / (float)n;
    float pad = jag ? 0.0f : 0.0f;
    float ro = r_out + (jag ? 0.18f * pm_rand(b) * (r_out - r_in) : 0), ri = r_in;
    float ca = cosf(aa), sa = sinf(aa), cb = cosf(bb), sb = sinf(bb), z = d * 0.5f;
    pcol c = pc_mix(c0, c1, pm_rand(b));
    t3_vec3 ia = P(ri * ca, ri * sa, 0), ib = P(ri * cb, ri * sb, 0), oa = P(ro * ca, ro * sa, 0), ob = P(ro * cb, ro * sb, 0);
    (void)pad;
    #define ZZ(p, zz) P((p).x, (p).y, zz)
    pm_quad(b, ZZ(oa, -z), ZZ(ob, -z), ZZ(ob, z), ZZ(oa, z), c);             /* outer */
    pm_quad(b, ZZ(ia, z), ZZ(ib, z), ZZ(ib, -z), ZZ(ia, -z), dk(c, 0.8f));   /* inner */
    pm_quad(b, ZZ(ia, z), ZZ(oa, z), ZZ(ob, z), ZZ(ib, z), pc_mul(c, 1.05f));   /* front */
    pm_quad(b, ZZ(ib, -z), ZZ(ob, -z), ZZ(oa, -z), ZZ(ia, -z), dk(c, 0.9f));  /* back */
    pm_quad(b, ZZ(ia, -z), ZZ(oa, -z), ZZ(oa, z), ZZ(ia, z), dk(c, 0.75f));  /* the joint faces seen where blocks are missing */
    pm_quad(b, ZZ(ob, -z), ZZ(ib, -z), ZZ(ib, z), ZZ(ob, z), dk(c, 0.75f));
    #undef ZZ
  }
}

GEN(gen_ruin_arch) {
  pm_vary(b, 0.08f);
  float h = size;
  pcol lo = stone_lo(variant & 1), hi = stone_hi(variant & 1);
  pm_layer(b, PL_MAIN);
  float span = h * 0.62f, pw = h * 0.15f, pd = h * 0.2f, ph = h * 0.5f;
  for (int k = -1; k <= 1; k += 2) {
    float pheight = ph * ((variant == 1 && k > 0) ? 0.7f : 1.0f);
    pm_push(b);
    pm_move(b, k * span * 0.5f, 0, 0);
    pm_box(b, pw * 1.2f, h * 0.05f, pd * 1.2f, dk(lo, 0.7f), lo);
    pm_move(b, 0, h * 0.05f, 0);
    pm_box(b, pw, pheight, pd, lo, hi);
    pm_pop(b);
  }
  int n = lod ? 7 : 12;
  pm_push(b);
  pm_move(b, 0, ph + h * 0.05f, 0);
  float r_in = span * 0.5f - pw * 0.5f, r_out = span * 0.5f + pw * 0.5f;
  if (variant == 1) { /* the arch has lost its right shoulder and keystone */
    arch_ring(b, r_in, r_out, pd, CW_PI * 0.42f, CW_PI, n * 3 / 5, lo, hi, true);
    arch_ring(b, r_in, r_out, pd, 0.0f, CW_PI * 0.18f, 2, lo, hi, true);
  } else {
    arch_ring(b, r_in, r_out, pd, 0.0f, CW_PI, n, lo, hi, false);
    /* a keystone, slightly proud */
    pm_push(b);
    pm_move(b, 0, r_out - pw * 0.05f, 0);
    pm_box(b, pw * 0.5f, pw * 0.7f, pd * 1.08f, hi, pc_mul(hi, 1.1f));
    pm_pop(b);
  }
  pm_pop(b);
  if (variant == 1) {
    for (int i = 0; i < 4; i++) {
      pm_push(b);
      pm_move(b, span * (0.1f + 0.15f * (float)i) - span * 0.1f, 0, pd * (0.9f + 0.5f * pm_rand(b)));
      pm_roty(b, pm_rands(b));
      pm_box(b, pw * 0.9f, pw * (0.5f + 0.5f * pm_rand(b)), pd * 0.8f, lo, hi);
      pm_pop(b);
    }
  }
  moss(b, -span * 0.5f, ph + h * 0.05f, 0, h * 0.08f);
  moss(b, span * 0.3f, ph + h * 0.05f + r_out * 0.9f, 0, h * 0.06f);
}

GEN(gen_stairs) {
  pm_vary(b, 0.07f);
  float h = size;
  pcol lo = stone_lo(variant & 1), hi = stone_hi(variant & 1);
  pm_layer(b, PL_MAIN);
  int steps = lod ? 8 : 14;
  float w = variant == 1 ? h * 1.4f : h * 0.8f, run = h * 2.0f / (float)steps, rise = h / (float)steps;
  for (int i = 0; i < steps; i++) {
    if (variant == 2 && (i == 6 || i == 7)) continue;
    pm_push(b);
    pm_move(b, 0, 0, -h + (float)i * run);
    pm_box(b, w, rise * (float)(i + 1), run * 1.02f, dk(lo, 0.8f), pc_mix(lo, hi, (float)i / (float)steps));
    pm_pop(b);
  }
  for (int k = -1; k <= 1; k += 2) {
    pm_push(b);
    pm_move(b, k * (w * 0.5f + h * 0.04f), 0, 0);
    pm_roty(b, 0);
    pm_wedge(b, h * 0.1f, h * 1.1f, h * 2.0f, dk(lo, 0.7f), hi);
    pm_pop(b);
    if (variant == 1 && !lod)
      for (int i = 0; i < 6; i++) {
        pm_push(b);
        pm_move(b, k * (w * 0.5f + h * 0.04f), h * 1.1f * (float)(i + 1) / 6.0f, -h + h * 2.0f * (float)i / 6.0f);
        pm_cyl(b, h * 0.03f, h * 0.025f, h * 0.18f, 6, lo, hi);
        pm_pop(b);
      }
  }
  moss(b, 0, h * 0.5f, h * 0.2f, h * 0.2f);
}

GEN(gen_bridge_post) {
  pm_vary(b, 0.07f);
  float h = size;
  pcol w0 = rgb(0.25f, 0.17f, 0.1f), w1 = rgb(0.5f, 0.38f, 0.24f);
  pm_layer(b, PL_MAIN);
  pm_box(b, h * 0.3f, h * 0.12f, h * 0.3f, rgb(0.3f, 0.3f, 0.28f), rgb(0.55f, 0.55f, 0.5f));
  pm_limb(b, P(0, h * 0.1f, 0), P(0, h, 0), h * 0.07f, h * 0.055f, 7, w0, w1);
  pm_push(b);
  pm_move(b, 0, h * 0.97f, 0);
  pm_ball(b, h * 0.07f, h * 0.06f, h * 0.07f, 0, 0.08f, w0, w1);
  pm_pop(b);
  pm_beam(b, P(0, h * 0.7f, -h * 0.06f), P(0, h * 0.7f, h * 0.3f), h * 0.05f, w0);
  pm_layer(b, PL_MAIN);
  pm_cable(b, P(0, h * 0.78f, h * 0.07f), P(0, h * 0.55f, h * 1.6f), h * 0.12f, h * 0.018f, rgb(0.55f, 0.45f, 0.28f));
  pm_cable(b, P(0, h * 0.45f, h * 0.07f), P(0, h * 0.2f, h * 1.6f), h * 0.1f, h * 0.014f, rgb(0.5f, 0.4f, 0.25f));
  if (variant == 0) {
    pm_layer(b, PL_METAL);
    pm_push(b);
    pm_move(b, h * 0.1f, h * 0.88f, 0);
    pm_rod(b, P(0, 0, 0), P(0, h * 0.07f, 0), h * 0.008f, 4, rgb(0.2f, 0.2f, 0.2f));
    pm_move(b, 0, -h * 0.01f, 0);
    pm_layer(b, PL_GLOW);
    pm_cyl_ex(b, h * 0.035f, h * 0.045f, h * 0.09f, 6, glow(rgb(1, 0.6f, 0.2f), 2.6f), glow(rgb(1, 0.85f, 0.5f), 3.4f), false, false);
    pm_pop(b);
  }
}

/* ── hardware: dishes, solar, station, antennas, debris ──────────────────────────────────────────── */

GEN(gen_dish) {
  pm_vary(b, 0.03f);
  float h = size;
  pcol w = rgb(0.86f, 0.87f, 0.88f), g = rgb(0.4f, 0.42f, 0.46f);
  pm_layer(b, PL_METAL);
  pm_box(b, h * 0.18f, h * 0.06f, h * 0.18f, g, w);
  pm_cyl(b, h * 0.03f, h * 0.025f, h * 0.35f, 8, g, w);
  pm_push(b);
  pm_move(b, 0, h * 0.4f, 0);
  pm_rotx(b, variant == 1 ? -0.9f : -0.6f);
  float R = h * (variant == 1 ? 0.5f : 0.38f);
  pm_smooth(b, true);
  pm_push(b);
  pm_rotx(b, CW_PI * 0.5f);
  /* a shallow bowl: inner surface lighter, outer darker */
  float inner[] = { 0.001f, 0, R * 0.35f, R * 0.025f, R * 0.7f, R * 0.1f, R, R * 0.22f };
  float outer[] = { R, R * 0.2f, R * 0.7f, R * 0.08f, R * 0.35f, R * 0.0f, 0.001f, -R * 0.02f };
  pm_lathe(b, inner, 4, lod ? 10 : 18, w, pc_mul(w, 1.05f));
  pm_lathe(b, outer, 4, lod ? 10 : 18, g, dk(g, 1.1f));
  pm_pop(b);
  pm_smooth(b, false);
  pm_beam(b, P(R * 0.6f, R * 0.1f, 0), P(0, R * 0.0f, R * 0.6f), h * 0.01f, g);
  pm_beam(b, P(-R * 0.6f, R * 0.1f, 0), P(0, R * 0.0f, R * 0.6f), h * 0.01f, g);
  pm_push(b);
  pm_move(b, 0, R * 0.0f, R * 0.6f);
  pm_box(b, h * 0.05f, h * 0.05f, h * 0.06f, g, w);
  pm_pop(b);
  pm_pop(b);
  if (variant == 1) { /* the red warning lamp */
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, 0, h * 0.36f, 0);
    pm_ball(b, h * 0.015f, h * 0.015f, h * 0.015f, 0, 0, glow(rgb(1, 0.1f, 0.05f), 4), glow(rgb(1, 0.3f, 0.2f), 5));
    pm_pop(b);
  }
}

GEN(gen_solar_panel) {
  pm_vary(b, 0.02f);
  float h = size;
  int arr = variant == 1 ? 3 : variant == 2 ? 2 : 1;
  pcol frame = rgb(0.6f, 0.62f, 0.66f), cell0 = rgb(0.03f, 0.06f, 0.16f), cell1 = rgb(0.08f, 0.16f, 0.34f);
  if (variant == 2) cell1 = rgb(0.2f, 0.24f, 0.3f); /* dusty */
  float pw = h * 1.5f, pl = h * 0.9f;
  for (int a = 0; a < arr; a++) {
    pm_push(b);
    pm_move(b, (a - (arr - 1) * 0.5f) * (pw + h * 0.1f), 0, 0);
    pm_layer(b, PL_METAL);
    for (int k = -1; k <= 1; k += 2) pm_beam(b, P(k * pw * 0.4f, 0, -pl * 0.3f), P(k * pw * 0.4f, h * 0.7f, -pl * 0.3f), h * 0.04f, frame);
    pm_beam(b, P(-pw * 0.4f, 0, pl * 0.3f), P(-pw * 0.4f, h * 0.3f, pl * 0.3f), h * 0.04f, frame);
    pm_beam(b, P(pw * 0.4f, 0, pl * 0.3f), P(pw * 0.4f, h * 0.3f, pl * 0.3f), h * 0.04f, frame);
    pm_push(b);
    pm_move(b, 0, h * 0.5f, 0);
    pm_rotx(b, 0.5f);
    pm_box_c(b, pw, h * 0.04f, pl, frame);
    int cx = lod ? 3 : 6, cz = lod ? 2 : 4;
    for (int i = 0; i < cx; i++)
      for (int j = 0; j < cz; j++) {
        float x0 = -pw * 0.5f + pw * ((float)i + 0.06f) / (float)cx, x1 = -pw * 0.5f + pw * ((float)i + 0.94f) / (float)cx;
        float z0 = -pl * 0.5f + pl * ((float)j + 0.06f) / (float)cz, z1 = -pl * 0.5f + pl * ((float)j + 0.94f) / (float)cz;
        pm_quad(b, P(x0, h * 0.0225f, z1), P(x1, h * 0.0225f, z1), P(x1, h * 0.0225f, z0), P(x0, h * 0.0225f, z0), pc_mix(cell0, cell1, 0.5f + 0.5f * pm_rands(b)));
      }
    pm_pop(b);
    pm_pop(b);
  }
}

GEN(gen_station_module) {
  pm_vary(b, 0.03f);
  float h = size, r = h * 0.25f, L = h * 1.1f;
  static const unsigned pal[4] = { 0xd8d8d4, 0xc8ccd4, 0xd4c8b0, 0xb8c0c8 };
  pcol hull = hex(pal[variant & 3]);
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, 0, r * 1.1f, 0);
  pm_rotz(b, -CW_PI * 0.5f);
  pm_move(b, 0, -L * 0.5f, 0);
  pm_smooth(b, true);
  if (variant == 2) { /* a node: a sphere with four ports */
    pm_pop(b);
    pm_push(b);
    pm_move(b, 0, r * 1.2f, 0);
    pm_ball(b, r * 1.1f, r * 1.1f, r * 1.1f, lod ? 1 : 2, 0.0f, dk(hull, 0.7f), hull);
    pm_smooth(b, false);
    for (int p = 0; p < 4; p++) {
      pm_push(b);
      pm_roty(b, (float)p * CW_PI * 0.5f);
      pm_move(b, 0, 0, r * 1.05f);
      pm_rotx(b, CW_PI * 0.5f);
      pm_cyl_ex(b, r * 0.5f, r * 0.45f, r * 0.45f, 10, dk(hull, 0.6f), hull, false, true);
      pm_move(b, 0, r * 0.45f, 0);
      pm_ring(b, r * 0.5f, r * 0.05f, 12, 4, rgb(0.9f, 0.7f, 0.2f));
      pm_pop(b);
    }
    pm_pop(b);
    return;
  }
  float prof[] = { r * 0.8f, 0, r, r * 0.08f, r, L - r * 0.08f, r * 0.8f, L };
  pm_lathe(b, prof, 4, lod ? 10 : 18, dk(hull, 0.6f), hull);
  pm_smooth(b, false);
  /* ribs, hatches and docking ports */
  for (int i = 1; i < (lod ? 3 : 7); i++) {
    pm_push(b);
    pm_move(b, 0, L * (float)i / 7.0f, 0);
    pm_cyl_ex(b, r * 1.03f, r * 1.03f, L * 0.025f, lod ? 10 : 18, dk(hull, 0.65f), dk(hull, 0.85f), false, false);
    pm_pop(b);
  }
  for (int e = 0; e < 2; e++) {
    pm_push(b);
    pm_move(b, 0, e ? L : 0, 0);
    if (!e) pm_scale(b, 1, -1, 1);
    pm_cyl_ex(b, r * 0.5f, r * 0.42f, r * 0.4f, 10, dk(hull, 0.6f), hull, false, true);
    pm_move(b, 0, r * 0.4f, 0);
    pm_ring(b, r * 0.42f, r * 0.04f, 12, 4, rgb(0.9f, 0.7f, 0.2f));
    pm_pop(b);
  }
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < (lod ? 3 : 6); i++) {
    pm_push(b);
    pm_move(b, 0, L * (0.12f + 0.15f * (float)i), 0);
    pm_roty(b, 0.3f);
    pm_quad(b, P(r * 1.01f, 0, -r * 0.1f), P(r * 1.01f, 0, r * 0.1f), P(r * 1.01f, L * 0.06f, r * 0.1f), P(r * 1.01f, L * 0.06f, -r * 0.1f), glow(rgb(0.7f, 0.9f, 1.0f), 2.4f));
    pm_pop(b);
  }
  pm_pop(b);
  pm_layer(b, PL_METAL);
  if (variant == 1 || variant == 3) { /* solar wings or a radiator */
    for (int k = -1; k <= 1; k += 2) {
      pm_push(b);
      pm_move(b, 0, r * 1.1f, k * (r * 1.2f));
      pm_beam(b, P(0, 0, -k * r * 0.2f), P(0, 0, k * r * 0.2f), h * 0.02f, dk(hull, 0.7f));
      pm_move(b, 0, 0, k * r * 0.2f);
      for (int s = 0; s < (lod ? 2 : 4); s++) {
        pm_push(b);
        pm_move(b, 0, 0, k * (float)s * h * 0.26f);
        pm_box_c(b, L * 0.8f, h * 0.012f, h * 0.24f, variant == 1 ? rgb(0.05f, 0.12f, 0.3f) : rgb(0.8f, 0.8f, 0.82f));
        pm_pop(b);
      }
      pm_pop(b);
    }
  }
  pm_box_c(b, 0.001f, 0.001f, 0.001f, hull);
}

GEN(gen_antenna) {
  pm_vary(b, 0.03f);
  float h = size;
  pcol st = rgb(0.78f, 0.78f, 0.8f), red = rgb(0.85f, 0.15f, 0.1f);
  pm_layer(b, PL_METAL);
  if (variant == 1) { /* a phased array on a post */
    pm_cyl(b, h * 0.03f, h * 0.025f, h * 0.5f, 8, dk(st, 0.7f), st);
    pm_push(b);
    pm_move(b, 0, h * 0.62f, 0);
    pm_rotx(b, -0.3f);
    pm_box_c(b, h * 0.5f, h * 0.5f, h * 0.04f, dk(st, 0.7f));
    for (int i = 0; i < (lod ? 4 : 8); i++)
      for (int j = 0; j < (lod ? 4 : 8); j++)
        pm_box_c(b, h * 0.045f, h * 0.045f, h * 0.012f, rgb(0.2f + 0.04f * (float)((i + j) & 1), 0.24f, 0.3f)), pm_move(b, 0, 0, 0);
    pm_pop(b);
    return;
  }
  if (variant == 2) { /* a hedgehog of spikes on a mast */
    pm_cyl(b, h * 0.02f, h * 0.015f, h * 0.55f, 8, dk(st, 0.7f), st);
    for (int i = 0; i < (lod ? 8 : 20); i++) {
      float y = h * (0.3f + 0.6f * pm_rand(b)), a = pm_rand(b) * CW_TAU, l = h * (0.1f + 0.2f * pm_rand(b));
      pm_rod(b, P(0, y, 0), P(cosf(a) * l, y + l * 0.2f * pm_rands(b), sinf(a) * l), h * 0.004f, 4, st);
    }
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, 0, h * 1.0f, 0);
    pm_ball(b, h * 0.015f, h * 0.015f, h * 0.015f, 0, 0, glow(red, 4), glow(rgb(1, 0.3f, 0.2f), 5));
    pm_pop(b);
    return;
  }
  lattice(b, h * 0.12f, h * 0.04f, h * 0.8f, lod ? 5 : 10, h * 0.004f, st);
  pm_rod(b, P(0, h * 0.8f, 0), P(0, h, 0), h * 0.006f, 5, st);
  for (int i = 0; i < 3; i++) {
    float y = h * (0.45f + 0.15f * (float)i), a = (float)i * 2.1f;
    pm_push(b);
    pm_move(b, cosf(a) * h * 0.06f, y, sinf(a) * h * 0.06f);
    pm_roty(b, -a);
    pm_rotz(b, 0.0f);
    pm_cyl_ex(b, h * 0.06f, h * 0.06f, h * 0.012f, 12, dk(st, 0.7f), st, true, true);
    pm_pop(b);
  }
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < 2; i++) {
    pm_push(b);
    pm_move(b, 0, h * (i ? 1.0f : 0.5f), 0);
    pm_ball(b, h * 0.014f, h * 0.014f, h * 0.014f, 0, 0, glow(red, 4), glow(rgb(1, 0.3f, 0.2f), 5));
    pm_pop(b);
  }
}

GEN(gen_debris) {
  pm_vary(b, 0.08f);
  float h = size;
  pcol hull = rgb(0.5f, 0.52f, 0.56f), burnt = rgb(0.18f, 0.16f, 0.15f), gold = rgb(0.8f, 0.62f, 0.2f);
  pm_layer(b, PL_METAL);
  int n = lod ? 3 : 6;
  for (int i = 0; i < n; i++) {
    pm_push(b);
    pm_move(b, pm_rands(b) * h * 0.5f, h * (0.3f + 0.6f * pm_rand(b)), pm_rands(b) * h * 0.5f);
    pm_rotx(b, pm_rand(b) * CW_TAU);
    pm_roty(b, pm_rand(b) * CW_TAU);
    pm_rotz(b, pm_rand(b) * CW_TAU);
    switch ((variant + i) % 5) {
    case 0: pm_box_c(b, h * (0.3f + 0.3f * pm_rand(b)), h * 0.02f, h * (0.2f + 0.2f * pm_rand(b)), pc_mix(hull, burnt, pm_rand(b) * 0.6f)); break;
    case 1: pm_rod(b, P(-h * 0.25f, 0, 0), P(h * 0.25f, 0, 0), h * 0.012f, 5, hull); break;
    case 2: pm_cyl_ex(b, h * 0.08f, h * 0.06f, h * 0.2f, 7, burnt, hull, true, true); break;
    case 3: pm_box_c(b, h * 0.25f, h * 0.2f, h * 0.2f, gold); break;
    default:
      pm_tri(b, P(-h * 0.2f, 0, 0), P(h * 0.2f, 0, h * 0.05f), P(0, 0.01f, h * 0.22f), burnt);
      pm_tri(b, P(-h * 0.2f, 0, 0), P(0, 0.01f, h * 0.22f), P(h * 0.2f, 0, h * 0.05f), hull);
      break;
    }
    pm_pop(b);
  }
}

GEN(gen_cone) {
  float h = size;
  pm_vary(b, 0.03f);
  pm_layer(b, PL_MAIN);
  pm_box(b, h * 0.55f, h * 0.05f, h * 0.55f, rgb(0.08f, 0.08f, 0.08f), rgb(0.12f, 0.12f, 0.12f));
  pm_push(b);
  pm_move(b, 0, h * 0.04f, 0);
  pm_cyl_ex(b, h * 0.2f, h * 0.05f, h * 0.9f, lod ? 6 : 10, rgb(0.9f, 0.3f, 0.04f), rgb(1.0f, 0.5f, 0.1f), false, true);
  pm_move(b, 0, h * 0.4f, 0);
  pm_cyl_ex(b, h * 0.136f, h * 0.103f, h * 0.17f, lod ? 6 : 10, rgb(0.92f, 0.92f, 0.9f), rgb(1, 1, 1), false, false);
  pm_pop(b);
}

GEN(gen_hay) {
  pm_vary(b, 0.06f);
  float h = size;
  pcol c0 = rgb(0.62f, 0.5f, 0.2f), c1 = rgb(0.9f, 0.78f, 0.38f);
  pm_layer(b, PL_MAIN);
  if (variant == 1) { /* square bales stacked */
    for (int i = 0; i < 3; i++) {
      pm_push(b);
      pm_move(b, (i == 2 ? 0.0f : (i ? 0.5f : -0.5f)) * h, i == 2 ? h * 0.5f : 0, 0);
      pm_roty(b, pm_rands(b) * 0.06f);
      pm_box(b, h * 0.95f, h * 0.5f, h * 0.55f, c0, c1);
      pm_pop(b);
    }
    return;
  }
  pm_push(b);
  pm_move(b, 0, h * 0.5f, 0);
  pm_rotz(b, CW_PI * 0.5f);
  pm_move(b, 0, -h * 0.5f, 0);
  pm_cyl_ex(b, h * 0.5f, h * 0.5f, h, lod ? 8 : 12, c0, c1, true, true);
  pm_pop(b);
}

GEN(gen_bollard) {
  float h = size;
  static const unsigned cols[3] = { 0x2ad0ff, 0xff7a2a, 0xff3a8a };
  pcol c = hex(cols[variant % 3]);
  pm_vary(b, 0.03f);
  pm_layer(b, PL_METAL);
  pm_smooth(b, true);
  float prof[] = { h * 0.1f, 0, h * 0.075f, h * 0.06f, h * 0.06f, h * 0.7f, h * 0.075f, h * 0.76f };
  pm_lathe(b, prof, 4, lod ? 6 : 8, rgb(0.12f, 0.12f, 0.15f), rgb(0.3f, 0.3f, 0.34f));
  pm_smooth(b, false);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, h * 0.74f, 0);
  pm_cyl_ex(b, h * 0.07f, h * 0.085f, h * 0.2f, lod ? 6 : 8, glow(c, 1.8f), glow(pc_mix(c, rgb(1, 1, 1), 0.4f), 3.4f), false, true);
  pm_pop(b);
}

GEN(gen_torch) {
  float h = size;
  pcol iron = rgb(0.1f, 0.1f, 0.11f);
  pm_vary(b, 0.03f);
  pm_layer(b, PL_METAL);
  pm_smooth(b, true);
  float prof[] = { h * 0.06f, 0, h * 0.025f, h * 0.1f, h * 0.018f, h * 0.7f, h * 0.03f, h * 0.76f };
  pm_lathe(b, prof, 4, lod ? 5 : 8, dk(iron, 0.8f), rgb(0.26f, 0.26f, 0.3f));
  pm_smooth(b, false);
  pm_push(b);
  pm_move(b, 0, h * 0.74f, 0);
  pm_cyl_ex(b, h * 0.06f, h * 0.1f, h * 0.12f, lod ? 6 : 8, iron, rgb(0.3f, 0.3f, 0.34f), false, false);
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, h * 0.84f, 0);
  pm_smooth(b, true);
  float fl[] = { 0.001f, 0, h * 0.065f, h * 0.04f, h * 0.075f, h * 0.1f, h * 0.04f, h * 0.2f, 0.001f, h * 0.3f };
  pm_lathe(b, fl, 5, 7, glow(rgb(1.0f, 0.38f, 0.06f), 3.0f), glow(rgb(1.0f, 0.85f, 0.4f), 2.6f));
  pm_pop(b);
}
