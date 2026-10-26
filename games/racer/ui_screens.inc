/* ui_screens.inc: the menu screens, their input and their drawing (included by ui.c, never built on its own).
 *
 * Each screen has a layout function of the canvas size that the input and the drawing both use, so a click lands
 * on exactly what was drawn. Everything is laid out for 960x540 (or 540x960 portrait) and scaled. */

/* ── a kart seen from the side: the vehicle select, the podium ──────── */

static void poly_m(float cx, float gy, float u, const float *xy, int n, cw_color fill, cw_color line, float lw) {
  cw_vec2 p[20];
  for (int i = 0; i < n && i < 20; i++) p[i] = cw_v2(cx + xy[i * 2] * u, gy - xy[i * 2 + 1] * u);
  cw_r2d_polygon(p, n, fill);
  if (lw > 0) cw_r2d_polygon_lines(p, n, lw, line);
}
static void wheel_m(float cx, float gy, float u, float x, float r, float spin) {
  cw_vec2 c = cw_v2(cx + x * u, gy - r * u);
  cw_r2d_circle(c, r * u + 1.5f, 20, INK);
  cw_r2d_circle(c, r * u, 20, rgba(0.16f, 0.15f, 0.17f, 1));
  cw_r2d_circle(c, r * u * 0.6f, 16, rgba(0.62f, 0.6f, 0.62f, 1));
  for (int i = 0; i < 5; i++) {
    float a = spin + (float)i * CW_TAU / 5.0f;
    cw_r2d_line(c, cw_v2(c.x + cw_cos(a) * r * u * 0.58f, c.y + cw_sin(a) * r * u * 0.58f), r * u * 0.11f + 1, rgba(0.28f, 0.27f, 0.3f, 1));
  }
  cw_r2d_circle(c, r * u * 0.16f, 8, INK);
}

/* veh: a VEH index; flame 0..1 is the exhaust; t the time, for the wheels' turn and the bob */
static void draw_kart_side(float cx, float gy, float u, int veh, int colour, float flame, float t) {
  const vehicle_def *vd = &VEH[veh % NVEH];
  float L = vd->len, r = vd->wheel_r;
  cw_color c = kart_col(colour), dark = shade(c, 0.6f), lite = mix(c, rgba(1, 1, 1, 1), 0.4f);
  float spin = t * 9.0f;
  float wf = 0.62f * L, wr = -0.62f * L;
  float rr = r;
  if (veh % NVEH == 1) rr = r * 1.12f;
  if (veh % NVEH == 5) rr = r * 1.18f;
  /* the ground shadow */
  cw_r2d_round_rect(cw_rect_make(cx - L * 1.25f * u, gy - 0.05f * u, L * 2.5f * u, 0.16f * u), 0.08f * u, 4, rgba(0, 0, 0, 0.35f));
  /* the exhaust flame at the back */
  if (flame > 0.01f) {
    float fl = (0.55f + 0.45f * cw_sin(t * 40)) * flame;
    float f[] = { -L * 1.0f, r * 1.25f, -L * 1.0f - (0.5f + 0.7f * fl), r * 1.4f, -L * 1.0f, r * 1.55f };
    poly_m(cx, gy, u, f, 3, MUSTARD, INK, 0);
    float f2[] = { -L * 1.0f, r * 1.32f, -L * 1.0f - (0.25f + 0.4f * fl), r * 1.4f, -L * 1.0f, r * 1.48f };
    poly_m(cx, gy, u, f2, 3, CREAM, INK, 0);
  }
  switch (veh % NVEH) {
  case 0: { /* Scrapper: an open buggy with a roll cage */
    float body[] = { -L, r * 0.9f, L * 1.02f, r * 0.9f, L * 1.0f, r * 1.7f, L * 0.45f, r * 2.0f, -L * 0.25f, r * 2.0f, -L * 0.95f, r * 1.8f };
    poly_m(cx, gy, u, body, 6, c, INK, 2.5f);
    float strip[] = { -L, r * 0.9f, L * 1.02f, r * 0.9f, L * 1.0f, r * 1.15f, -L * 0.98f, r * 1.15f };
    poly_m(cx, gy, u, strip, 4, dark, INK, 0);
    cw_r2d_circle(cw_v2(cx - 0.02f * u, gy - r * 3.0f * u), r * 0.62f * u, 14, INK);
    cw_r2d_circle(cw_v2(cx - 0.02f * u, gy - r * 3.0f * u), r * 0.52f * u, 14, CREAM);
    cw_vec2 cage[5] = { cw_v2(cx - L * 0.35f * u, gy - r * 2.0f * u), cw_v2(cx - L * 0.22f * u, gy - r * 3.7f * u), cw_v2(cx + L * 0.2f * u, gy - r * 3.7f * u),
                        cw_v2(cx + L * 0.3f * u, gy - r * 2.0f * u), cw_v2(cx + L * 0.3f * u, gy - r * 2.0f * u) };
    cw_r2d_polyline(cage, 4, 0.1f * u + 3, false, INK);
    cw_r2d_polyline(cage, 4, 0.1f * u, false, lite);
    float hood[] = { L * 0.45f, r * 2.0f, L * 1.0f, r * 1.7f, L * 0.98f, r * 1.4f, L * 0.4f, r * 1.5f };
    poly_m(cx, gy, u, hood, 4, lite, INK, 0);
    wheel_m(cx, gy, u, wr, rr, spin);
    wheel_m(cx, gy, u, wf, rr, spin);
    break;
  }
  case 1: { /* Rhino: tall, heavy, a plough on the front */
    float body[] = { -L, r * 0.9f, L, r * 0.9f, L, r * 2.3f, L * 0.55f, r * 2.55f, L * 0.1f, r * 3.2f, -L * 0.7f, r * 3.2f, -L, r * 2.6f };
    poly_m(cx, gy, u, body, 7, c, INK, 2.5f);
    float win[] = { L * 0.35f, r * 2.55f, L * 0.05f, r * 3.05f, -L * 0.4f, r * 3.05f, -L * 0.4f, r * 2.55f };
    poly_m(cx, gy, u, win, 4, rgba(0.55f, 0.85f, 0.95f, 1), INK, 2);
    float plough[] = { L, r * 0.7f, L * 1.3f, r * 0.55f, L * 1.22f, r * 2.1f, L, r * 2.3f };
    poly_m(cx, gy, u, plough, 4, rgba(0.62f, 0.6f, 0.62f, 1), INK, 2.5f);
    float strip[] = { -L, r * 0.9f, L, r * 0.9f, L, r * 1.25f, -L, r * 1.25f };
    poly_m(cx, gy, u, strip, 4, dark, INK, 0);
    wheel_m(cx, gy, u, wr, rr, spin);
    wheel_m(cx, gy, u, wf, rr, spin);
    break;
  }
  case 2: { /* Gnat: tiny, a tall wing behind */
    float post[] = { -L * 0.78f, r * 1.7f, -L * 0.66f, r * 1.7f, -L * 0.66f, r * 3.2f, -L * 0.78f, r * 3.2f };
    poly_m(cx, gy, u, post, 4, dark, INK, 2);
    float wing[] = { -L * 1.12f, r * 3.2f, -L * 0.3f, r * 3.2f, -L * 0.3f, r * 3.55f, -L * 1.12f, r * 3.55f };
    poly_m(cx, gy, u, wing, 4, lite, INK, 2.5f);
    float body[] = { -L * 0.92f, r * 1.0f, L * 0.98f, r * 1.0f, L * 1.02f, r * 1.45f, L * 0.35f, r * 1.9f, -L * 0.6f, r * 1.9f, -L * 0.95f, r * 1.5f };
    poly_m(cx, gy, u, body, 6, c, INK, 2.5f);
    cw_r2d_circle(cw_v2(cx - L * 0.05f * u, gy - r * 2.7f * u), r * 0.55f * u, 14, INK);
    cw_r2d_circle(cw_v2(cx - L * 0.05f * u, gy - r * 2.7f * u), r * 0.46f * u, 14, CREAM);
    float nose[] = { L * 0.95f, r * 0.85f, L * 1.3f, r * 0.85f, L * 1.25f, r * 1.1f, L * 0.95f, r * 1.2f };
    poly_m(cx, gy, u, nose, 4, lite, INK, 2);
    wheel_m(cx, gy, u, wr, rr, spin);
    wheel_m(cx, gy, u, wf, rr, spin);
    break;
  }
  case 3: { /* Sidewinder: a low wedge with side pipes */
    float body[] = { -L, r * 0.85f, L * 1.08f, r * 0.8f, L * 0.95f, r * 1.4f, L * 0.1f, r * 2.0f, -L * 0.55f, r * 1.95f, -L, r * 1.55f };
    poly_m(cx, gy, u, body, 6, c, INK, 2.5f);
    float stripe[] = { -L * 0.95f, r * 1.2f, L * 0.9f, r * 1.1f, L * 0.88f, r * 1.3f, -L * 0.95f, r * 1.4f };
    poly_m(cx, gy, u, stripe, 4, lite, INK, 0);
    float fin[] = { -L * 1.0f, r * 1.5f, -L * 0.78f, r * 1.9f, -L * 1.05f, r * 2.7f, -L * 1.15f, r * 2.7f, -L * 1.1f, r * 1.7f };
    poly_m(cx, gy, u, fin, 5, dark, INK, 2);
    float pipe[] = { -L * 0.6f, r * 0.95f, L * 0.25f, r * 0.95f, L * 0.25f, r * 1.12f, -L * 0.6f, r * 1.12f };
    poly_m(cx, gy, u, pipe, 4, rgba(0.7f, 0.7f, 0.74f, 1), INK, 1.5f);
    float win[] = { L * 0.3f, r * 1.55f, L * 0.05f, r * 1.95f, -L * 0.3f, r * 1.95f, -L * 0.3f, r * 1.6f };
    poly_m(cx, gy, u, win, 4, rgba(0.55f, 0.85f, 0.95f, 1), INK, 2);
    wheel_m(cx, gy, u, wr, rr, spin);
    wheel_m(cx, gy, u, wf, rr, spin);
    break;
  }
  case 4: { /* Longhaul: a long hood, a tall cab at the back, two stacks */
    float body[] = { -L, r * 0.9f, L * 1.05f, r * 0.9f, L * 1.05f, r * 1.9f, L * 0.4f, r * 2.1f, L * 0.2f, r * 2.1f, -L, r * 2.1f };
    poly_m(cx, gy, u, body, 6, c, INK, 2.5f);
    float cab[] = { -L, r * 2.1f, -L * 0.92f, r * 3.3f, -L * 0.1f, r * 3.3f, L * 0.2f, r * 2.1f };
    poly_m(cx, gy, u, cab, 4, lite, INK, 2.5f);
    float win[] = { -L * 0.8f, r * 2.35f, -L * 0.78f, r * 3.05f, -L * 0.2f, r * 3.05f, L * 0.0f, r * 2.35f };
    poly_m(cx, gy, u, win, 4, rgba(0.55f, 0.85f, 0.95f, 1), INK, 2);
    for (int i = 0; i < 2; i++) {
      float x0 = L * (0.25f + 0.2f * (float)i);
      float st[] = { x0, r * 2.1f, x0 + 0.1f, r * 2.1f, x0 + 0.1f, r * 3.6f, x0, r * 3.6f };
      poly_m(cx, gy, u, st, 4, rgba(0.72f, 0.72f, 0.76f, 1), INK, 1.5f);
    }
    float grille[] = { L * 1.05f, r * 1.0f, L * 1.1f, r * 1.0f, L * 1.1f, r * 1.8f, L * 1.05f, r * 1.8f };
    poly_m(cx, gy, u, grille, 4, rgba(0.62f, 0.6f, 0.62f, 1), INK, 1.5f);
    wheel_m(cx, gy, u, wr, rr, spin);
    wheel_m(cx, gy, u, wf, rr, spin);
    break;
  }
  default: { /* Pogo: a round body on a spring */
    float bob = 0.1f * cw_sin(t * 6);
    cw_vec2 sp[7];
    for (int i = 0; i < 7; i++) sp[i] = cw_v2(cx + (((i & 1) ? 0.22f : -0.22f)) * u, gy - (r * 1.1f + (r * 1.3f + bob) * (float)i / 6.0f) * u);
    cw_r2d_polyline(sp, 7, 0.1f * u + 3, false, INK);
    cw_r2d_polyline(sp, 7, 0.1f * u, false, rgba(0.72f, 0.72f, 0.76f, 1));
    float body[18];
    for (int i = 0; i < 9; i++) {
      float a = (float)i / 9.0f * CW_TAU;
      body[i * 2] = cw_cos(a) * L * 0.95f;
      body[i * 2 + 1] = r * 3.0f + bob + cw_sin(a) * r * 1.0f;
    }
    poly_m(cx, gy, u, body, 9, c, INK, 2.5f);
    float win[] = { L * 0.55f, r * 3.2f + bob, L * 0.15f, r * 3.6f + bob, -L * 0.15f, r * 3.6f + bob, -L * 0.1f, r * 3.2f + bob };
    poly_m(cx, gy, u, win, 4, rgba(0.55f, 0.85f, 0.95f, 1), INK, 2);
    cw_r2d_line(cw_v2(cx - L * 0.7f * u, gy - (r * 3.8f + bob) * u), cw_v2(cx - L * 0.95f * u, gy - (r * 4.7f + bob) * u), 0.05f * u + 1.5f, INK);
    cw_r2d_circle(cw_v2(cx - L * 0.95f * u, gy - (r * 4.7f + bob) * u), 0.1f * u, 10, RED);
    wheel_m(cx, gy, u, wr, rr, spin);
    wheel_m(cx, gy, u, wf, rr, spin);
    break;
  }
  }
}

/* ── a gamepad glyph ────────────────────────────────────────────────── */

static void draw_pad_icon(cw_vec2 c, float w, cw_color col, float s) {
  float h = w * 0.62f;
  cw_r2d_round_rect(cw_rect_make(c.x - w * 0.5f, c.y - h * 0.5f, w, h), h * 0.42f, 8, INK);
  cw_r2d_round_rect(cw_rect_make(c.x - w * 0.5f + 2.5f * s, c.y - h * 0.5f + 2.5f * s, w - 5 * s, h - 5 * s), h * 0.4f, 8, col);
  cw_r2d_circle(cw_v2(c.x - w * 0.24f, c.y - h * 0.05f), w * 0.09f, 12, INK);
  cw_r2d_circle(cw_v2(c.x + w * 0.12f, c.y + h * 0.18f), w * 0.09f, 12, INK);
  cw_r2d_rect(cw_rect_make(c.x - w * 0.24f - w * 0.02f, c.y + h * 0.1f, w * 0.04f, w * 0.13f), INK);
  cw_r2d_rect(cw_rect_make(c.x - w * 0.31f, c.y + h * 0.145f, w * 0.14f, w * 0.04f), INK);
  static const cw_color BC[4] = { { 0.4f, 0.85f, 0.35f, 1 }, { 0.95f, 0.3f, 0.25f, 1 }, { 0.3f, 0.55f, 0.95f, 1 }, { 1.0f, 0.82f, 0.2f, 1 } };
  static const float bx[4] = { 0, 1, -1, 0 }, by[4] = { 1, 0, 0, -1 };
  for (int i = 0; i < 4; i++) cw_r2d_circle(cw_v2(c.x + w * 0.24f + bx[i] * w * 0.08f, c.y - h * 0.05f + by[i] * w * 0.08f), w * 0.04f, 8, BC[i]);
}

/* a button chip: a round face button, a key cap or a trigger */
static void chip(cw_vec2 c, const char *label, cw_color col, float h, float s, bool round) {
  float w = fmaxf(h, tx_width(U.f_hud, label, h * 0.62f) + h * 0.5f);
  cw_rect r = cw_rect_make(c.x, c.y - h * 0.5f, w, h);
  if (round) {
    cw_r2d_circle(cw_v2(c.x + h * 0.5f, c.y), h * 0.5f + 1.5f * s, 18, INK);
    cw_r2d_circle(cw_v2(c.x + h * 0.5f, c.y), h * 0.5f - 0.5f, 18, col);
    tx_o(U.f_hud, label, c.x + h * 0.5f, c.y, h * 0.6f, INK, (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
  } else {
    cw_r2d_round_rect(cw_rect_make(r.x - 1.5f * s, r.y - 1.5f * s, r.w + 3 * s, r.h + 3 * s), h * 0.22f, 4, INK);
    cw_r2d_round_rect(r, h * 0.2f, 4, col);
    cw_r2d_round_rect(cw_rect_make(r.x + 1.5f * s, r.y + 1.5f * s, r.w - 3 * s, r.h * 0.55f), h * 0.18f, 4, rgba(1, 1, 1, 0.22f));
    tx_o(U.f_hud, label, r.x + r.w * 0.5f, c.y - h * 0.02f, h * 0.55f, INK, (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
  }
}
static float chip_w(const char *label, float h) { return fmaxf(h, tx_width(U.f_hud, label, h * 0.62f) + h * 0.5f); }

/* ── shared screen pieces ───────────────────────────────────────────── */

static const char *dev_hint(const char *pad, const char *keys, const char *ptr, const char *touch) {
  return U.device == DEV_PAD ? pad : U.device == DEV_KEYS ? keys : U.device == DEV_TOUCH ? touch : ptr;
}

static void hint_line(float W, float H, float s, const char *str) {
  float w = tx_width(U.f_hud, str, 18 * s) + 50 * s;
  cw_rect r = cw_rect_make(W * 0.5f - w * 0.5f, H - 58 * s, w, 34 * s);
  slab(r, 10 * s, rgba(0.08f, 0.06f, 0.08f, 0.8f), s * 0.8f);
  tx(U.f_hud, str, W * 0.5f, r.y + r.h * 0.5f, 18 * s, alpha(CREAM, 0.92f), CW_ALIGN_CENTER);
}

static void header(float W, float H, float s, const char *title, const char *sub) {
  float t = ease_back((float)U.mt / 16.0f);
  float maxw = W - W * 0.06f - 170 * s;
  float fs = 38 * s, tw0 = tx_width(U.f_title, title, fs);
  if (tw0 + 80 * s > maxw) fs *= (maxw - 80 * s) / tw0;
  float w = tx_width(U.f_title, title, fs) + 80 * s;
  cw_rect r = cw_rect_make(W * 0.03f - (1 - t) * 300 * s, 22 * s, w, 60 * s);
  slab(r, 16 * s, rgba(0.1f, 0.07f, 0.09f, 0.92f), s);
  para(cw_rect_make(r.x + 4 * s, r.y + r.h - 7 * s, r.w - 8 * s, 5 * s), 0, RUST);
  tx(U.f_title, title, r.x + 40 * s, r.y + 29 * s, fs, MUSTARD, CW_ALIGN_LEFT);
  if (sub && r.x + r.w + 24 * s + tx_width(U.f_hud, sub, 20 * s) < W - 170 * s) tx(U.f_hud, sub, r.x + r.w + 24 * s, r.y + 30 * s, 20 * s, alpha(CREAM, 0.85f * t), CW_ALIGN_LEFT);
}

static void backdrop(float W, float H, float s, float strength) {
  if (!game_loaded()) {
    vgrad(cw_rect_make(0, 0, W, H), rgba(0.2f, 0.12f, 0.1f, 1), rgba(0.05f, 0.04f, 0.08f, 1));
  }
  scrim(W, H, strength);
  tape(cw_rect_make(-10, -2, W + 20, 13 * s), s, U.time * 18 * s);
  tape(cw_rect_make(-10, H - 11 * s, W + 20, 13 * s), s, -U.time * 18 * s);
}

static void draw_button(cw_rect r, const char *label, bool sel, bool enabled, float s, float appear) {
  float a = clamp01(appear * 1.4f);
  if (a <= 0) return;
  r.x += (1 - ease_back(appear)) * 120 * s;
  float pulse = sel ? 1 + 0.015f * cw_sin(U.time * 7) : 1;
  cw_rect q = cw_rect_make(r.x - r.w * (pulse - 1) * 0.5f, r.y - r.h * (pulse - 1) * 0.5f, r.w * pulse, r.h * pulse);
  cw_color fill = !enabled ? rgba(0.2f, 0.18f, 0.2f, 0.8f * a) : sel ? alpha(MUSTARD, a) : alpha(PANEL, a);
  slab(q, 14 * s, fill, s);
  if (sel) para(cw_rect_make(q.x + 6 * s, q.y + q.h - 8 * s, q.w - 12 * s, 5 * s), 0, alpha(RUST, a));
  cw_color tc = !enabled ? alpha(DIM, a * 0.6f) : sel ? alpha(INK, a) : alpha(CREAM, a);
  tx_o(U.f_title, label, q.x + q.w * 0.5f + 6 * s, q.y + q.h * 0.5f, q.h * 0.42f, tc, (txopt){ .align = CW_ALIGN_CENTER, .outline = sel ? 0.01f : 0, .no_shadow = sel });
  if (sel) {
    float bob = 3 * s * cw_sin(U.time * 8);
    tri(cw_v2(q.x - 22 * s + bob, q.y + q.h * 0.5f), 11 * s, 0, INK);
    tri(cw_v2(q.x - 22 * s + bob, q.y + q.h * 0.5f), 8 * s, 0, MUSTARD);
  }
}

static void draw_logo(float cx, float cy, float size, float s, float appear) {
  float t = ease_back(appear);
  cw_xform2d x = cw_xform2d_trs(cw_v2(cx, cy), -0.045f + 0.012f * cw_sin(U.time * 1.3f), cw_v2(t, t));
  cw_r2d_set_transform(&x);
  float w = fminf(tx_width(U.f_title, GAME_TITLE, size), 0.92f * fmaxf(size, 1));
  (void)w;
  float off = size * 0.055f;
  tx_o(U.f_title, GAME_TITLE, off * 1.4f, off * 1.6f, size, rgba(0.0f, 0.0f, 0.0f, 0.5f), (txopt){ .align = CW_ALIGN_CENTER, .outline = size * 0.06f, .no_shadow = true });
  tx_o(U.f_title, GAME_TITLE, off * 0.7f, off * 0.8f, size, RUST, (txopt){ .align = CW_ALIGN_CENTER, .outline = size * 0.06f, .no_shadow = true });
  tx_o(U.f_title, GAME_TITLE, 0, 0, size, MUSTARD, (txopt){ .align = CW_ALIGN_CENTER, .outline = size * 0.05f, .no_shadow = true });
  cw_r2d_set_transform(NULL);
  (void)s;
}

/* ── layouts ────────────────────────────────────────────────────────── */

static cw_rect list_rect_n(int i, int n, float W, float H, float top_frac) {
  float s = lscale(W, H), bw = 340 * s, bh = 50 * s, gap = 11 * s;
  if (n > 5) bh = 40 * s, gap = 7 * s; /* six entries: tighter, to stay clear of the hint line */
  else if (n > 4) bh = 44 * s, gap = 9 * s;
  return cw_rect_make((W - bw) * 0.5f, H * top_frac + (float)i * (bh + gap), bw, bh);
}
static cw_rect list_rect(int i, float W, float H, float top_frac) { return list_rect_n(i, 4, W, H, top_frac); }
static int title_count(void) {
#ifdef __wasm__
  return 5;
#else
  return 6;
#endif
}
enum { TI_RACE, TI_TRIAL, TI_CUP, TI_SPLIT, TI_OPTIONS, TI_QUIT };
static const char *const TITLE_ITEMS[6] = { "RACE", "TIME TRIAL", "CUP", "SPLIT SCREEN", "OPTIONS", "QUIT" };
static const char *const PAUSE_ITEMS[5] = { "RESUME", "RESTART RACE", "GRAPHICS", "CONTROLS", "QUIT TO MENU" };
static const char *const OPTPAGE_ITEMS[7] = { "GRAPHICS", "MUSIC", "EFFECTS", "STEERING ASSIST", "AUTO-ACCELERATE", "CONTROLS", "BACK" };
enum { OI_GFX, OI_MUSIC, OI_SFX, OI_ASSIST_STEER, OI_ASSIST_GAS, OI_CONTROLS, OI_BACK, OI_COUNT };

/* the vehicle panel of one player */
typedef struct vlay {
  float sp, ox, oy;
  bool wide;
  cw_rect tag, name, arrow_l, arrow_r, stage, dots_c, stats, blurb, ready;
  cw_rect sw[N_COLOURS];
  cw_vec2 dot[NVEH];
  float dot_r;
} vlay;

static void vlayout(cw_rect P, vlay *l) {
  l->wide = P.w / P.h >= 1.45f;
  float dw = l->wide ? 420 : 300, dh = l->wide ? 236 : 432;
  l->sp = fminf(P.w / dw, P.h / dh);
  l->ox = P.x + (P.w - dw * l->sp) * 0.5f;
  l->oy = P.y + (P.h - dh * l->sp) * 0.5f;
#define DR(x, y, w, h) cw_rect_make(l->ox + (x)*l->sp, l->oy + (y)*l->sp, (w)*l->sp, (h)*l->sp)
  if (l->wide) {
    l->tag = DR(0, 0, 46, 26);
    l->name = DR(60, 0, 300, 28);
    l->arrow_l = DR(52, 0, 30, 28);
    l->arrow_r = DR(338, 0, 30, 28);
    l->stage = DR(6, 36, 246, 118);
    l->stats = DR(262, 38, 154, 108);
    l->blurb = DR(8, 156, 404, 34);
    for (int i = 0; i < N_COLOURS; i++) l->sw[i] = DR(10 + 25.0f * (float)i, 200, 20, 20);
    l->ready = DR(232, 196, 180, 36);
    l->dot_r = 3.4f * l->sp;
    for (int i = 0; i < NVEH; i++) l->dot[i] = cw_v2(l->ox + (129 + ((float)i - 2.5f) * 16) * l->sp, l->oy + 147 * l->sp);
  } else {
    l->tag = DR(0, 0, 46, 26);
    l->name = DR(78, 0, 196, 28);
    l->arrow_l = DR(48, 0, 30, 28);
    l->arrow_r = DR(270, 0, 30, 28);
    l->stage = DR(4, 36, 292, 128);
    l->stats = DR(14, 190, 272, 122);
    l->blurb = DR(10, 318, 280, 44);
    for (int i = 0; i < N_COLOURS; i++) l->sw[i] = DR(12 + 35.0f * (float)i, 366, 24, 24);
    l->ready = DR(40, 396, 220, 32);
    l->dot_r = 3.6f * l->sp;
    for (int i = 0; i < NVEH; i++) l->dot[i] = cw_v2(l->ox + (150 + ((float)i - 2.5f) * 18) * l->sp, l->oy + 176 * l->sp);
  }
#undef DR
}

/* where each human's panel goes on the vehicle screen */
static cw_rect vpanel_rect(int i, int n, float W, float H) {
  float s = lscale(W, H);
  float top = (is_portrait(W, H) ? 96 : 100) * s, bot = 70 * s;
  float x0 = W * 0.02f, y0 = top, w = W * 0.96f, h = H - top - bot;
  float g = 10 * s;
  if (n == 1) return cw_rect_make(x0 + w * 0.06f, y0, w * 0.88f, h);
  if (is_portrait(W, H)) {
    if (n == 2) return cw_rect_make(x0, y0 + (float)i * (h * 0.5f), w, h * 0.5f - g);
    int row = i / 2, col = i % 2;
    if (n == 3 && i == 2) return cw_rect_make(x0, y0 + h * 0.66f, w, h * 0.34f - g);
    float rh = n == 3 ? h * 0.33f : h * 0.25f;
    if (n == 3) return cw_rect_make(x0 + (float)col * (w * 0.5f), y0 + (float)row * rh, w * 0.5f - g, rh - g);
    return cw_rect_make(x0 + (float)col * (w * 0.5f), y0 + (float)row * (h * 0.5f), w * 0.5f - g, h * 0.5f - g);
  }
  if (n == 2) return cw_rect_make(x0 + (float)i * (w * 0.5f), y0, w * 0.5f - g, h);
  if (n == 3) return cw_rect_make(x0 + (float)i * (w / 3), y0, w / 3 - g, h);
  return cw_rect_make(x0 + (float)(i % 2) * (w * 0.5f), y0 + (float)(i / 2) * (h * 0.5f), w * 0.5f - g, h * 0.5f - g);
}

/* ── the options of a race setup ────────────────────────────────────── */

/* the rows the setup offers depend on what is being set up: a race, a time trial (no CPU karts, no boxes, always the
 * standard class) or a cup (its own laps and boxes) */
enum { OPT_LAPS, OPT_CPU, OPT_ITEMS, OPT_CLASS, OPT_ASSIST, OPT_GHOST, OPT_KINDS };
#define MAX_OPT_ROWS 5
static const char *const OPT_LABEL[OPT_KINDS] = { "LAPS", "CPU DRIVERS", "ITEM BOXES", "SPEED CLASS", "ASSISTS", "GHOST" };
static const char *const CLASS_NAME[CLASS_COUNT] = { "Standard", "Fast", "Blazing" };
static const char *const ASSIST_NAME[4] = { "Off", "Steering", "Auto gas", "Both" };
static int option_rows(int rows[MAX_OPT_ROWS]) {
  if (U.gmode == RM_TRIAL) {
    rows[0] = OPT_GHOST, rows[1] = OPT_ASSIST;
    return 2;
  }
  if (U.gmode == RM_CUP) {
    rows[0] = OPT_CPU, rows[1] = OPT_CLASS, rows[2] = OPT_ASSIST;
    return 3;
  }
  rows[0] = OPT_LAPS, rows[1] = OPT_CPU, rows[2] = OPT_ITEMS, rows[3] = OPT_CLASS, rows[4] = OPT_ASSIST;
  return 5;
}
static void opt_text(int opt, char *b, size_t n) {
  switch (opt) {
  case OPT_LAPS: snprintf(b, n, "%d", U.laps); break;
  case OPT_CPU: snprintf(b, n, "%s", SKILL_NAME[U.skill]); break;
  case OPT_ITEMS: snprintf(b, n, "%s", U.items ? "ON" : "OFF"); break;
  case OPT_CLASS: snprintf(b, n, "%s", CLASS_NAME[U.cls]); break;
  case OPT_ASSIST: snprintf(b, n, "%s", ASSIST_NAME[U.assist & 3]); break;
  default: snprintf(b, n, "%s", U.ghost_on ? "ON" : "OFF"); break;
  }
}
static void opt_bump(int opt, int dir) {
  switch (opt) {
  case OPT_LAPS: U.laps = CW_CLAMP(U.laps + dir, 1, MAX_LAPS); break;
  case OPT_CPU: U.skill = (U.skill + dir + 4) % 4; break;
  case OPT_ITEMS: U.items = !U.items; break;
  case OPT_CLASS: U.cls = (U.cls + dir + CLASS_COUNT) % CLASS_COUNT; break;
  case OPT_ASSIST: U.assist = (U.assist + dir + 4) & 3; break;
  default: U.ghost_on = !U.ghost_on; break;
  }
  sfx_move();
}

typedef struct maplay {
  float s;
  bool portrait;
  int cols, nrows;
  cw_rect card[NMAPS];
  cw_rect detail, big, optrow[MAX_OPT_ROWS], start, back;
} maplay;

static void map_layout(float W, float H, maplay *m) {
  float s = lscale(W, H);
  m->s = s;
  m->portrait = is_portrait(W, H);
  int rows[MAX_OPT_ROWS];
  int nr = option_rows(rows);
  m->nrows = nr;
  float gx = W * 0.03f, gw = W * 0.94f, gap = 10 * s;
  if (!m->portrait) {
    m->cols = 4;
    float cw = (gw - 3 * gap) / 4, ch = 80 * s, y0 = 88 * s;
    for (int i = 0; i < NMAPS; i++) m->card[i] = cw_rect_make(gx + (float)(i % 4) * (cw + gap), y0 + (float)(i / 4) * (ch + gap), cw, ch);
    float by = y0 + 2 * ch + gap + 8 * s, bh = H - by - 64 * s;
    m->detail = cw_rect_make(gx, by, gw * 0.56f, bh);
    float bs = fminf(bh - 12 * s, gw * 0.56f * 0.42f);
    m->big = cw_rect_make(gx + 8 * s, by + 6 * s, bs, bs);
    float ox = gx + gw * 0.56f + gap, ow = gw * 0.44f - gap;
    float sh = 38 * s, rg = 4 * s;
    float rh = fminf(40 * s, (bh - sh - rg * (float)(nr + 1)) / (float)nr);
    for (int i = 0; i < nr; i++) m->optrow[i] = cw_rect_make(ox, by + (float)i * (rh + rg), ow, rh);
    float sy = by + (float)nr * (rh + rg) + rg;
    m->start = cw_rect_make(ox, sy, ow, fmaxf(fminf(50 * s, by + bh - sy), 28 * s));
  } else {
    m->cols = 2;
    float cw = (gw - gap) / 2, ch = 76 * s, y0 = 92 * s;
    for (int i = 0; i < NMAPS; i++) m->card[i] = cw_rect_make(gx + (float)(i % 2) * (cw + gap), y0 + (float)(i / 2) * (ch + gap), cw, ch);
    float by = y0 + 4 * (ch + gap) + 4 * s;
    float bh = H - by - 70 * s;
    m->detail = cw_rect_make(gx, by, gw, bh * 0.5f);
    m->big = cw_rect_make(gx + 8 * s, by + 6 * s, bh * 0.5f - 12 * s, bh * 0.5f - 12 * s);
    float oy = by + bh * 0.5f + 8 * s, rg = 4 * s;
    float rh = fminf(42 * s, (bh * 0.5f - 8 * s - 44 * s - rg * (float)(nr + 1)) / (float)nr);
    for (int i = 0; i < nr; i++) m->optrow[i] = cw_rect_make(gx, oy + (float)i * (rh + rg), gw, rh);
    m->start = cw_rect_make(gx, oy + (float)nr * (rh + rg) + rg, gw, 44 * s);
  }
  m->back = cw_rect_make(W - 150 * s - gx, 22 * s, 150 * s, 40 * s);
}

static cw_rect back_button_rect(float W, float H) {
  float s = lscale(W, H);
  return cw_rect_make(W - 150 * s - W * 0.03f, 22 * s, 150 * s, 44 * s);
}

/* ── setup helpers ──────────────────────────────────────────────────── */

static void draw_option_row_fwd(cw_rect r, const char *label, const char *value, bool sel, float s, float ap);
static void restore_pads(void) {
  for (int p = 0; p < MAXV; p++) cw_input_assign(p, p, p == 0 ? (CW_DEVICE_KEYBOARD | CW_DEVICE_MOUSE | CW_DEVICE_TOUCH) : 0);
}

static void scr_init(void) {
  U.title_menu = 0;
  U.mapfocus = 0;
  U.res_n = 0;
  U.joined[0] = true;
}

static bool colour_taken(int p, int c, int n) {
  for (int i = 0; i < n; i++)
    if (i != p && U.col[i] == c) return true;
  return false;
}
static void unique_colours(int n) {
  for (int i = 0; i < n; i++) {
    int tries = 0;
    while (colour_taken(i, U.col[i], n) && tries++ < N_COLOURS) U.col[i] = (U.col[i] + 1) % N_COLOURS;
  }
}

/* humans are the pads that joined, in order; player i is read from the i-th of them */
static void assign_humans(int n) {
  int k = 0;
  for (int p = 0; p < MAXV && k < n; p++)
    if (U.joined[p]) U.slot_pad[k++] = p;
  for (int i = 0; i < MAXV; i++) cw_input_assign(i, i < n ? U.slot_pad[i] : -1, i == 0 ? (CW_DEVICE_KEYBOARD | CW_DEVICE_MOUSE | CW_DEVICE_TOUCH) : 0);
}

static void enter_vehicle(int n) {
  U.setup.humans = n;
  unique_colours(n);
  for (int i = 0; i < n; i++) U.ready[i] = false, U.veh_anim[i] = 30, U.veh_dir[i] = 1;
  memset(U.pnav, 0, sizeof U.pnav);
  goto_mode(M_VEHICLE);
}

static void start_race_from_menu(void) {
  race_setup rs = { .map = U.map, .laps = U.laps, .humans = U.setup.humans, .cpu_skill = U.skill, .items = U.items != 0 };
  U.cup.on = false;
  for (int i = 0; i < MAXV; i++) rs.vehicle[i] = U.veh[i], rs.colour[i] = U.col[i];
  rs.speed_class = U.cls, rs.assist_steer = (U.assist & 1) != 0, rs.assist_gas = (U.assist & 2) != 0;
  if (U.gmode == RM_TRIAL) {
    /* a time trial: you alone, one lap from the grid, no boxes, the standard class, and the map's best run as a ghost */
    rs.mode = RM_TRIAL, rs.humans = 1, rs.laps = 1, rs.items = false, rs.speed_class = CLASS_STANDARD;
    rs.ghost = trial_ghost_for(rs.map);
  }
  U.setup = rs;
  save_write();
  sfx_ok();
  begin_pending(PD_START_RACE);
}

/* ── results ────────────────────────────────────────────────────────── */

static const int POINTS[MAXK] = { 10, 8, 6, 5, 4, 3, 2, 1 };

static void results_build(void) {
  const sim *S = game_sim();
  if (U.tr.on) {
    trial_results_build();
    return;
  }
  U.res_n = 0;
  U.res_winner_human = 0;
  bool any_record = false;
  for (int i = 0; i < S->nkarts && i < MAXK; i++) {
    int ki = S->order[i];
    const kart *k = &S->k[ki];
    result_row *r = &U.res[U.res_n++];
    memset(r, 0, sizeof *r);
    r->kart = ki, r->place = i + 1, r->veh = k->veh, r->colour = k->colour;
    r->human = k->human ? ki : -1;
    r->finished = k->finished;
    r->time = k->finished ? (float)k->finish_tick / TICK_HZ : 0;
    float best = k->best_lap;
    /* the sim records a lap when the next one starts, so the last lap is worked out here */
    if (k->finished && k->lap_start_tick < k->finish_tick) {
      float last = (float)(k->finish_tick - k->lap_start_tick) / TICK_HZ;
      if (S->laps > 1 && (best <= 0 || last < best)) best = last;
      else if (S->laps == 1) best = last;
    }
    r->best = best;
    r->hits = k->hits_given;
    r->tricks = k->tricks;
    r->points = POINTS[i];
    if (k->human) {
      if (S->map >= 0 && S->map < NMAPS && best > 0 && S->speed_class == CLASS_STANDARD) {
        uint32_t cs = (uint32_t)(best * 100 + 0.5f);
        uint32_t old = SAVE.best_lap_cs[S->map];
        if (old == 0 || cs < old) {
          SAVE.best_lap_cs[S->map] = cs;
          r->record = true;
          any_record = true;
        }
      }
      if (!U.res_winner_human) U.res_winner_human = i + 1;
    }
  }
  if (!U.saved_this_race) {
    U.saved_this_race = true;
    SAVE.races++;
    if (U.res_winner_human == 1) SAVE.wins++;
    save_write();
  }
  (void)any_record;
  if (U.cup.on) cup_results_add();
  goto_mode(M_RESULTS);
  U.res_ticks = 0;
  game_set_camera(2);
}

/* a dev aid: a finished race written into the sim so the results table can be looked at */
static void fake_results(void) {
  sim *S = (sim *)game_sim();
  static const int perm[MAXK] = { 3, 0, 5, 1, 6, 2, 7, 4 };
  for (int i = 0; i < MAXK; i++) {
    kart *k = &S->k[perm[i]];
    k->finished = true;
    k->finish_tick = 3600 + i * 180 + 17 * i * i;
    k->lap_start_tick = k->finish_tick - 1200 - i * 20;
    k->best_lap = 56.4f + 1.37f * (float)i;
    k->hits_given = (7 - i) % 4;
    k->tricks = 2 + (i * 5) % 7;
    k->place = i + 1;
    S->order[i] = perm[i];
  }
  S->phase = PH_DONE;
  results_build();
  U.res[0].record = true;
}

/* ── input per screen ───────────────────────────────────────────────── */

static bool move_vehicle(int p, int dir) {
  U.veh[p] = (U.veh[p] + dir + NVEH) % NVEH;
  U.veh_dir[p] = dir;
  U.veh_anim[p] = 0;
  sfx_move();
  return true;
}
static void move_colour(int p, int dir, int n) {
  int c = U.col[p];
  for (int tries = 0; tries < N_COLOURS; tries++) {
    c = (c + dir + N_COLOURS) % N_COLOURS;
    if (!colour_taken(p, c, n)) break;
  }
  U.col[p] = c;
  sfx_move();
}

static void tick_title(float W, float H) {
  if (U.title_menu == 0) {
    if (U.mt > 18 && (N.any || N.ok || N.start || N.click)) {
      U.title_menu = 1;
      U.sel = 0;
      U.mt = 0;
      sfx_ok();
    }
    return;
  }
  int n = title_count();
  cw_rect rects[6];
  for (int i = 0; i < n; i++) rects[i] = list_rect_n(i, n, W, H, is_portrait(W, H) ? 0.36f : 0.335f);
  if (N.back) {
    U.title_menu = 0;
    U.mt = 0;
    sfx_back();
    return;
  }
  if (N.dy) U.sel = (U.sel + N.dy + n) % n, sfx_move();
  if (pointer_mode() && N.moved)
    for (int i = 0; i < n; i++)
      if (in_rect(N.m, rects[i]) && U.sel != i) U.sel = i, sfx_move();
  int act = -1;
  if (N.click)
    for (int i = 0; i < n; i++)
      if (in_rect(N.m, rects[i])) U.sel = i, act = i;
  if (N.ok || N.start) act = U.sel;
  if (act < 0 || U.mt < 14) return;
  switch (act) {
  case TI_RACE:
  case TI_TRIAL:
  case TI_CUP:
    restore_pads();
    memset(U.joined, 0, sizeof U.joined);
    U.joined[0] = true;
    U.gmode = act == TI_TRIAL ? RM_TRIAL : act == TI_CUP ? RM_CUP : RM_RACE;
    sfx_ok();
    enter_vehicle(1);
    U.from = M_TITLE;
    break;
  case TI_SPLIT:
    restore_pads();
    memset(U.joined, 0, sizeof U.joined);
    U.joined[0] = true;
    if (U.gmode == RM_TRIAL) U.gmode = RM_RACE;
    sfx_ok();
    goto_mode(M_JOIN);
    break;
  case TI_OPTIONS:
    U.from = M_TITLE;
    sfx_ok();
    U.opt_sel = 0;
    goto_mode(M_OPTIONS);
    break;
  case TI_QUIT: cw_quit(); break;
  }
}

static int joined_count(void) {
  int n = 0;
  for (int p = 0; p < MAXV; p++) n += U.joined[p];
  return n;
}
static cw_rect join_card(int i, float W, float H) {
  float s = lscale(W, H);
  if (is_portrait(W, H)) {
    float h = 150 * s, g = 12 * s, y0 = 110 * s;
    return cw_rect_make(W * 0.08f, y0 + (float)i * (h + g), W * 0.84f, h);
  }
  float w = (W * 0.92f - 3 * 14 * s) / 4, x0 = W * 0.04f;
  return cw_rect_make(x0 + (float)i * (w + 14 * s), H * 0.2f, w, H * 0.5f);
}
static cw_rect join_continue(float W, float H) {
  float s = lscale(W, H);
  return cw_rect_make(W * 0.5f - 190 * s, H - 150 * s, 380 * s, 58 * s);
}

/* the mode pill beside the continue button: a split screen race, or a cup */
static cw_rect join_mode_rect(float W, float H) {
  float s = lscale(W, H);
  cw_rect c = join_continue(W, H);
  if (is_portrait(W, H)) return cw_rect_make(c.x, c.y - 56 * s, c.w, 46 * s);
  return cw_rect_make(c.x + c.w + 18 * s, c.y + 4 * s, fminf(W - (c.x + c.w + 18 * s) - W * 0.03f, 290 * s), 50 * s);
}

static void tick_join(float W, float H) {
  if ((N.dx || (N.click && in_rect(N.m, join_mode_rect(W, H)))) && U.mt > 10) U.gmode = U.gmode == RM_CUP ? RM_RACE : RM_CUP, sfx_move();
  for (int p = 1; p < MAXV; p++) {
    if (!pad_present(p)) {
      U.joined[p] = false;
      continue;
    }
    if (!U.joined[p] && cw_action_just_pressed(p, U.m_ok) && U.mt > 10) U.joined[p] = true, sfx_ok();
    else if (U.joined[p] && cw_action_just_pressed(p, U.a_back)) U.joined[p] = false, sfx_back();
  }
  U.joined[0] = true;
  int n = joined_count();
  bool go = false;
  cw_rect cr = join_continue(W, H);
  if (cw_action_just_pressed(0, U.m_ok) || (cw_action_just_pressed(0, U.a_start) && !cw_action_just_pressed(0, U.a_back))) go = true;
  if (N.click && in_rect(N.m, cr)) go = true;
  if (go && U.mt > 12) {
    if (n >= 2) {
      sfx_ok();
      assign_humans(n);
      enter_vehicle(n);
    } else {
      sfx_back();
      U.shake = 24; /* the hint shakes */
    }
  }
  if (cw_action_just_pressed(0, U.a_back) || (N.click && in_rect(N.m, back_button_rect(W, H)))) {
    sfx_back();
    restore_pads();
    goto_mode(M_TITLE);
    U.title_menu = 1;
    U.sel = 1;
  }
  if (U.shake > 0) U.shake--;
}

static void tick_vehicle(float W, float H) {
  int n = U.setup.humans;
  bool all = true;
  for (int i = 0; i < n; i++) {
    int hx = (cw_action_pressed(i, U.m_right) ? 1 : 0) - (cw_action_pressed(i, U.m_left) ? 1 : 0);
    int hy = (cw_action_pressed(i, U.m_down) ? 1 : 0) - (cw_action_pressed(i, U.m_up) ? 1 : 0);
    int dx, dy;
    nav_step(&U.pnav[i], hx, hy, &dx, &dy);
    if (U.veh_anim[i] < 60) U.veh_anim[i]++;
    if (!U.ready[i]) {
      if (dx) move_vehicle(i, dx);
      if (dy) move_colour(i, dy, n);
      /* Escape is both back and start: back wins */
      bool back = cw_action_just_pressed(i, U.a_back);
      if (!back && (cw_action_just_pressed(i, U.m_ok) || (i == 0 && cw_action_just_pressed(0, U.a_start))) && U.mt > 10) U.ready[i] = true, sfx_ok();
      else if (back && U.mt > 10) {
        if (i == 0) {
          sfx_back();
          if (n > 1) goto_mode(M_JOIN), U.mt = 20;
          else goto_mode(M_TITLE), U.title_menu = 1, U.sel = 0;
          if (n > 1) restore_pads();
          return;
        }
      }
    } else if (cw_action_just_pressed(i, U.a_back)) U.ready[i] = false, sfx_back();
    if (!U.ready[i]) all = false;
  }
  /* the mouse and taps run player 1's panel */
  vlay l;
  vlayout(vpanel_rect(0, n, W, H), &l);
  if (N.click && !U.ready[0]) {
    if (in_rect(N.m, l.arrow_l)) move_vehicle(0, -1);
    else if (in_rect(N.m, l.arrow_r)) move_vehicle(0, 1);
    for (int v = 0; v < NVEH; v++)
      if (hypotf(N.m.x - l.dot[v].x, N.m.y - l.dot[v].y) < l.dot_r * 2.6f && U.veh[0] != v) {
        move_vehicle(0, v > U.veh[0] ? 1 : -1);
        U.veh[0] = v;
      }
    for (int c = 0; c < N_COLOURS; c++)
      if (in_rect(N.m, l.sw[c]) && !colour_taken(0, c, n)) U.col[0] = c, sfx_move();
    if (in_rect(N.m, l.stage)) {
      /* a tap on the picture steps to the next ride */
      move_vehicle(0, N.m.x > l.stage.x + l.stage.w * 0.5f ? 1 : -1);
    }
  }
  if (N.click && in_rect(N.m, l.ready)) {
    U.ready[0] = !U.ready[0];
    sfx_ok();
  }
  all = true;
  for (int i = 0; i < n; i++) all = all && U.ready[i];
  if (N.click && in_rect(N.m, back_button_rect(W, H))) {
    sfx_back();
    if (n > 1) goto_mode(M_JOIN);
    else goto_mode(M_TITLE), U.title_menu = 1, U.sel = 0;
    if (n > 1) restore_pads();
    return;
  }
  if (all && U.mt > 24) {
    U.sel = 0;
    U.mapfocus = 0;
    U.cup_focus = 0;
    goto_mode(U.gmode == RM_CUP ? M_CUP : M_MAP);
    sfx_ok();
  }
}


static void tick_map(float W, float H) {
  maplay m;
  map_layout(W, H, &m);
  int dx = 0, dy = 0;
  {
    int hx = (cw_action_pressed(0, U.m_right) ? 1 : 0) - (cw_action_pressed(0, U.m_left) ? 1 : 0);
    int hy = (cw_action_pressed(0, U.m_down) ? 1 : 0) - (cw_action_pressed(0, U.m_up) ? 1 : 0);
    nav_step(&U.nav, hx, hy, &dx, &dy);
  }
  bool ok = cw_action_just_pressed(0, U.m_ok) && U.mt > 12;
  bool back = cw_action_just_pressed(0, U.a_back) && U.mt > 8;
  bool start = false;
  int rows[MAX_OPT_ROWS];
  int nr = option_rows(rows);
  if (U.mapfocus > nr + 1) U.mapfocus = nr + 1;
  if (U.mapfocus == 0) {
    if (dx) U.map = (U.map + dx + NMAPS) % NMAPS, sfx_move();
    if (dy) {
      int nm = U.map + dy * m.cols;
      if (nm >= NMAPS) U.mapfocus = 1, sfx_move();
      else if (nm >= 0) U.map = nm, sfx_move();
    }
    if (ok) start = true;
  } else {
    if (dy) {
      int nf = U.mapfocus + dy;
      if (nf < 1) U.mapfocus = 0, U.map = CW_CLAMP(NMAPS - m.cols + (U.map % m.cols), 0, NMAPS - 1);
      else U.mapfocus = CW_MIN(nf, nr + 1);
      sfx_move();
    }
    if (U.mapfocus >= 1 && U.mapfocus <= nr) {
      if (dx) opt_bump(rows[U.mapfocus - 1], dx);
      if (ok) opt_bump(rows[U.mapfocus - 1], 1);
    } else if (U.mapfocus == nr + 1 && ok) start = true;
  }
  /* the mouse */
  if (pointer_mode() && N.moved)
    for (int i = 0; i < NMAPS; i++)
      if (in_rect(N.m, m.card[i]) && (U.map != i || U.mapfocus != 0)) U.map = i, U.mapfocus = 0, sfx_move();
  if (N.click) {
    for (int i = 0; i < NMAPS; i++)
      if (in_rect(N.m, m.card[i])) {
        if (U.map == i && U.mapfocus == 0 && U.device != DEV_TOUCH) start = true;
        else if (U.map != i) sfx_move();
        U.map = i;
        U.mapfocus = 0;
      }
    for (int r = 0; r < nr; r++)
      if (in_rect(N.m, m.optrow[r])) {
        U.mapfocus = r + 1;
        float t = (N.m.x - m.optrow[r].x) / m.optrow[r].w;
        opt_bump(rows[r], t < 0.4f ? -1 : 1);
      }
    if (in_rect(N.m, m.start)) start = true;
    if (in_rect(N.m, back_button_rect(W, H))) back = true;
  }
  if (start) {
    start_race_from_menu();
    return;
  }
  if (back) {
    sfx_back();
    for (int i = 0; i < U.setup.humans; i++) U.ready[i] = false;
    goto_mode(M_VEHICLE);
    U.mt = 30;
  }
}

static cw_rect opt_panel(float W, float H) {
  float s = lscale(W, H);
  float w = fminf(W * 0.92f, 620 * s), h = (float)OI_COUNT * 46 * s + 24 * s;
  return cw_rect_make((W - w) * 0.5f, fmaxf(98 * s, H * 0.5f - h * 0.5f), w, h);
}
static cw_rect opt_row(int i, float W, float H) {
  float s = lscale(W, H);
  cw_rect p = opt_panel(W, H);
  return cw_rect_make(p.x + 18 * s, p.y + 12 * s + (float)i * 46 * s, p.w - 36 * s, 40 * s);
}
/* the slider's track inside a row */
static cw_rect opt_slider(cw_rect row, float s) { return cw_rect_make(row.x + row.w * 0.42f, row.y + row.h * 0.5f - 11 * s, row.w * 0.5f, 22 * s); }

static void tick_options(float W, float H) {
  float s = lscale(W, H);
  int hx = (any_held(U.m_right) ? 1 : 0) - (any_held(U.m_left) ? 1 : 0);
  int hy = (any_held(U.m_down) ? 1 : 0) - (any_held(U.m_up) ? 1 : 0);
  int dx, dy;
  nav_step(&U.nav, hx, hy, &dx, &dy);
  if (dy) U.opt_sel = (U.opt_sel + dy + OI_COUNT) % OI_COUNT, sfx_move();
  if (pointer_mode() && N.moved)
    for (int i = 0; i < OI_COUNT; i++)
      if (in_rect(N.m, opt_row(i, W, H)) && U.opt_sel != i) U.opt_sel = i, sfx_move();
  bool act = false;
  int row = U.opt_sel;
  if (N.click)
    for (int i = 0; i < OI_COUNT; i++)
      if (in_rect(N.m, opt_row(i, W, H))) row = i, U.opt_sel = i, act = true;
  if (N.ok && U.mt > 10) act = true;
  /* sliders: left / right, or a click or drag on the bar */
  bool changed = false;
  if (U.opt_sel == OI_ASSIST_STEER && (dx || (act && N.click))) U.assist ^= 1, sfx_move(), act = false, changed = true;
  if (U.opt_sel == OI_ASSIST_GAS && (dx || (act && N.click))) U.assist ^= 2, sfx_move(), act = false, changed = true;
  if (U.opt_sel == OI_MUSIC || U.opt_sel == OI_SFX) {
    uint8_t *v = U.opt_sel == OI_MUSIC ? &SAVE.music : &SAVE.sfx;
    if (dx) {
      int nv = CW_CLAMP((int)*v + dx, 0, 10);
      if (nv != *v) *v = (uint8_t)nv, changed = true;
    }
  }
  if (N.down && pointer_mode())
    for (int i = OI_MUSIC; i <= OI_SFX; i++) {
      cw_rect sl = opt_slider(opt_row(i, W, H), s);
      if (in_rect(N.m, cw_rect_make(sl.x - 10 * s, sl.y - 8 * s, sl.w + 20 * s, sl.h + 16 * s)) && (N.click || U.opt_sel == i)) {
        uint8_t *v = i == OI_MUSIC ? &SAVE.music : &SAVE.sfx;
        int nv = CW_CLAMP((int)((N.m.x - sl.x) / sl.w * 10 + 0.5f), 0, 10);
        if (nv != *v) *v = (uint8_t)nv, changed = true;
        U.opt_sel = i;
        act = false;
      }
    }
  if (changed) {
    apply_volumes();
    if (U.opt_sel == OI_ASSIST_STEER || U.opt_sel == OI_ASSIST_GAS) save_write();
    else if (U.ticks - U.rec_dummy > 6) sfx_move(), U.rec_dummy = U.ticks;
  }
  if (act && U.mt > 8) {
    if (row == OI_GFX) {
      cw_quality_menu_open();
      sfx_ok();
    } else if (row == OI_ASSIST_STEER || row == OI_ASSIST_GAS) {
      U.assist ^= row == OI_ASSIST_STEER ? 1 : 2;
      sfx_move();
      save_write();
    } else if (row == OI_CONTROLS) {
      sfx_ok();
      U.cfrom = M_OPTIONS;
      goto_mode(M_CONTROLS);
    } else if (row == OI_BACK) {
      sfx_back();
      save_write();
      goto_mode(U.from);
      if (U.from == M_TITLE) U.title_menu = 1, U.sel = 2;
    }
  }
  if ((N.back || N.start) && U.mt > 8) {
    sfx_back();
    save_write();
    goto_mode(U.from);
    if (U.from == M_TITLE) U.title_menu = 1, U.sel = 2;
  }
}

static void tick_controls(float W, float H) {
  bool leave = (N.back || N.ok || N.start) && U.mt > 10;
  if (N.click && U.mt > 10) leave = true;
  (void)W, (void)H;
  if (leave) {
    sfx_back();
    goto_mode(U.cfrom);
    if (U.cfrom == M_PAUSE) U.sel = 3;
    if (U.cfrom == M_OPTIONS) U.opt_sel = OI_CONTROLS;
  }
}

static void tick_race(float W, float H) {
  (void)W, (void)H;
  const sim *S = game_sim();
  if (S->phase == PH_DONE) return;
  int nv = game_views();
  bool pause = false;
  for (int v = 0; v < nv; v++)
    if (cw_action_just_pressed(game_view_player(v), U.a_start)) pause = true, U.pauser = v;
  if (nv == 1 && N.click) {
    hv g = { 0 };
    float vx, vy, vw, vh;
    game_view_rect(0, W, H, &vx, &vy, &vw, &vh);
    g.x = vx, g.y = vy, g.w = vw, g.h = vh;
    g.s = fminf(vw / 960.0f, vh / 540.0f) * (is_portrait(W, H) ? 1.8f : 1.0f);
    if (in_rect(N.m, pause_button_rect(&g))) pause = true;
  }
  if (pause && U.mt > 20 && U.pending == PD_NONE) {
    goto_mode(M_PAUSE);
    sfx_ok();
  }
  /* a time trial: the item button is the retry, a tap restarts the run at once */
  if (game_is_trial() && U.pending == PD_NONE && cw_action_just_pressed(0, U.a_item) && U.mt > 12) trial_retry();
}

static void tick_pause(float W, float H) {
  if (U.pending != PD_NONE) return;
  cw_rect rects[5];
  for (int i = 0; i < 5; i++) rects[i] = list_rect(i, W, H, is_portrait(W, H) ? 0.3f : 0.3f);
  bool resume = (N.start || N.back) && U.mt > 8;
  if (N.dy) U.sel = (U.sel + N.dy + 5) % 5, sfx_move();
  if (pointer_mode() && N.moved)
    for (int i = 0; i < 5; i++)
      if (in_rect(N.m, rects[i]) && U.sel != i) U.sel = i, sfx_move();
  int act = -1;
  if (N.click)
    for (int i = 0; i < 5; i++)
      if (in_rect(N.m, rects[i])) U.sel = i, act = i;
  if (N.ok) act = U.sel;
  if (U.mt < 8) act = -1;
  if (resume || act == 0) {
    sfx_back();
    goto_mode(M_RACE);
    U.mt = 30;
    return;
  }
  switch (act) {
  case 1:
    sfx_ok();
    if (game_is_trial()) trial_retry();
    else begin_pending(PD_RESTART);
    break;
  case 2:
    sfx_ok();
    cw_quality_menu_open();
    break;
  case 3:
    sfx_ok();
    U.cfrom = M_PAUSE;
    goto_mode(M_CONTROLS);
    break;
  case 4:
    sfx_ok();
    begin_pending(PD_TO_MENU);
    break;
  default: break;
  }
}

/* the results buttons: n of them (3 for a race, 2 in a cup) */
static cw_rect res_button_n(int i, int n, float W, float H) {
  float s = lscale(W, H);
  if (is_portrait(W, H)) {
    float bw = W * 0.8f, bh = 52 * s;
    return cw_rect_make((W - bw) * 0.5f, H - 190 * s + (float)i * (bh + 8 * s) - 10 * s, bw, bh);
  }
  float bw = 210 * s, bh = 52 * s, g = 12 * s;
  float x0 = W * 0.97f - (float)n * bw - (float)(n - 1) * g;
  return cw_rect_make(x0 + (float)i * (bw + g), H - bh - 30 * s, bw, bh);
}
static cw_rect res_button(int i, float W, float H) { return res_button_n(i, 3, W, H); }
static int res_nbuttons(void) { return U.cup.on ? 2 : 3; }

static void tick_trial_results(float W, float H);
static void tick_results(float W, float H) {
  U.res_ticks++;
  if (U.pending != PD_NONE) return;
  if (U.tr.on) {
    tick_trial_results(W, H);
    return;
  }
  if (U.res_ticks < 110) {
    if ((N.ok || N.start || N.click) && U.res_ticks > 20) U.res_ticks = 110;
    return;
  }
  int nb = res_nbuttons();
  int hx = (any_held(U.m_right) ? 1 : 0) - (any_held(U.m_left) ? 1 : 0);
  int dx, dy;
  nav_step(&U.nav, hx, 0, &dx, &dy);
  if (U.sel >= nb) U.sel = 0;
  if (dx) U.sel = (U.sel + dx + nb) % nb, sfx_move();
  if (is_portrait(W, H)) {
    int hy = (any_held(U.m_down) ? 1 : 0) - (any_held(U.m_up) ? 1 : 0);
    int ddx, ddy;
    nav_step(&U.pnav[0], 0, hy, &ddx, &ddy);
    if (ddy) U.sel = (U.sel + ddy + nb) % nb, sfx_move();
  }
  int act = -1;
  if (pointer_mode() && N.moved)
    for (int i = 0; i < nb; i++)
      if (in_rect(N.m, res_button_n(i, nb, W, H)) && U.sel != i) U.sel = i, sfx_move();
  if (N.click)
    for (int i = 0; i < nb; i++)
      if (in_rect(N.m, res_button_n(i, nb, W, H))) U.sel = i, act = i;
  if (N.ok && U.res_ticks > 125) act = U.sel;
  if (U.cup.on) {
    /* a cup: on to the standings, or give it up */
    if (U.auto_cup && U.res_ticks > 150) act = 0;
    if (act == 0) sfx_ok(), cup_open_standings();
    else if (act == 1) sfx_ok(), U.cup.on = false, begin_pending(PD_TO_MENU);
    return;
  }
  if (act == 0) sfx_ok(), begin_pending(PD_RESTART);
  else if (act == 1) sfx_ok(), begin_pending(PD_NEXT_MAP);
  else if (act == 2) sfx_ok(), begin_pending(PD_TO_MENU);
}

static void scr_tick(void) {
  cw_vec2 cs = cw_r2d_canvas_size();
  float W = cs.x, H = cs.y;
  /* this tick's directions with key repeat, from every player's pad and the keys */
  {
    int hx = (any_held(U.m_right) ? 1 : 0) - (any_held(U.m_left) ? 1 : 0);
    int hy = (any_held(U.m_down) ? 1 : 0) - (any_held(U.m_up) ? 1 : 0);
    nav_step(&U.nav, hx, hy, &N.dx, &N.dy);
  }
  N.ok = any_pressed(U.m_ok);
  N.back = any_pressed(U.a_back);
  N.start = any_pressed(U.a_start);
  if (U.pending != PD_NONE || U.fade_dir > 0) {
    N.dx = N.dy = 0;
    N.ok = N.back = N.start = N.click = false;
  }
  /* the tracks build a few at a time while the title is up, so the map screen is ready */
  if (U.ticks % 6 == 0)
    for (int i = 0; i < NMAPS; i++)
      if (!U.mm[i].ok) {
        mm_get(i);
        break;
      }
  switch (U.mode) {
  case M_TITLE: tick_title(W, H); break;
  case M_JOIN: tick_join(W, H); break;
  case M_VEHICLE: tick_vehicle(W, H); break;
  case M_MAP: tick_map(W, H); break;
  case M_OPTIONS: tick_options(W, H); break;
  case M_CONTROLS: tick_controls(W, H); break;
  case M_RACE: tick_race(W, H); break;
  case M_PAUSE: tick_pause(W, H); break;
  case M_RESULTS: tick_results(W, H); break;
  case M_CUP: tick_cup(W, H); break;
  case M_STANDINGS: tick_standings(W, H); break;
  case M_CUPEND: tick_cupend(W, H); break;
  }
}

/* ── drawing: title ─────────────────────────────────────────────────── */

static void draw_title(float W, float H, float s) {
  bool menu = U.title_menu == 1;
  backdrop(W, H, s, menu ? 0.6f : 0.3f);
  bool por = is_portrait(W, H);
  float logo_size = (menu ? 96 : 150) * s;
  float maxw = W * 0.9f;
  float lw = tx_width(U.f_title, GAME_TITLE, logo_size);
  if (lw > maxw) logo_size *= maxw / lw;
  float ly = menu ? H * (por ? 0.2f : 0.19f) : H * (por ? 0.3f : 0.36f);
  float appear = clamp01((float)U.ticks / 40.0f);
  draw_logo(W * 0.5f, ly, logo_size, s, appear);
  if (!menu) {
    float t = clamp01(((float)U.ticks - 25) / 20.0f);
    para(cw_rect_make(W * 0.5f - 250 * s, ly + logo_size * 0.62f, 500 * s, 34 * s), 12 * s, alpha(rgba(0.1f, 0.07f, 0.09f, 0.88f), t));
    tx(U.f_hud, "Loops, jumps and junk. Eight tracks.", W * 0.5f, ly + logo_size * 0.62f + 17 * s, 20 * s, alpha(CREAM, t), CW_ALIGN_CENTER);
    const char *p = dev_hint("Press START", "Press ENTER", "Click to start", "Tap to start");
    float blink = 0.8f + 0.2f * cw_sin(U.time * 4.5f);
    float py = H * (por ? 0.7f : 0.78f);
    float bw = fmaxf(tx_width(U.f_title, p, 36 * s) + 90 * s, 300 * s);
    cw_rect r = cw_rect_make(W * 0.5f - bw * 0.5f, py - 30 * s, bw, 60 * s);
    slab(r, 16 * s, alpha(PANEL, t), s);
    tx_o(U.f_title, p, W * 0.5f, py, 36 * s, alpha(MUSTARD, t * blink), (txopt){ .align = CW_ALIGN_CENTER });
    if (SAVE.races > 0) {
      char rb[64];
      snprintf(rb, sizeof rb, "Races %u    Wins %u", SAVE.races, SAVE.wins);
      tx(U.f_hud, rb, W * 0.5f, H - 40 * s, 18 * s, alpha(CREAM, 0.8f * t), CW_ALIGN_CENTER);
    }
    return;
  }
  int n = title_count();
  for (int i = 0; i < n; i++) {
    float ap = clamp01(((float)U.mt - (float)i * 4) / 18.0f);
    draw_button(list_rect_n(i, n, W, H, por ? 0.36f : 0.335f), TITLE_ITEMS[i], U.sel == i, true, s, ap);
  }
  hint_line(W, H, s, dev_hint("D-pad move   (A) choose   (B) back", "Arrows move   Enter choose   Esc back", "Click to choose", "Tap to choose"));
}

/* ── drawing: join ──────────────────────────────────────────────────── */

static void draw_join(float W, float H, float s) {
  backdrop(W, H, s, 0.8f);
  header(W, H, s, "SPLIT SCREEN", "Two to four players");
  int n = joined_count();
  for (int i = 0; i < MAXV; i++) {
    float ap = ease_back(((float)U.mt - (float)i * 5) / 18.0f);
    cw_rect r = join_card(i, W, H);
    r.y += (1 - ap) * 80 * s;
    bool on = U.joined[i];
    bool present = i == 0 || pad_present(i);
    cw_color edge = on ? kart_col(i) : DIM;
    slab(r, 0, on ? mix(PANEL, shade(edge, 0.5f), 0.55f) : PANEL, s);
    cw_r2d_rect_lines(cw_rect_make(r.x + 4 * s, r.y + 4 * s, r.w - 8 * s, r.h - 8 * s), 3 * s, alpha(edge, on ? 1.0f : 0.3f));
    char b[32];
    snprintf(b, sizeof b, "PLAYER %d", i + 1);
    tx(U.f_title, b, r.x + r.w * 0.5f, r.y + 34 * s, 24 * s, on ? CREAM : DIM, CW_ALIGN_CENTER);
    bool por = is_portrait(W, H);
    cw_vec2 ic = por ? cw_v2(r.x + r.w * 0.22f, r.y + r.h * 0.6f) : cw_v2(r.x + r.w * 0.5f, r.y + r.h * 0.42f);
    float bob = on ? 4 * s * cw_sin(U.time * 5 + (float)i) : 0;
    draw_pad_icon(cw_v2(ic.x, ic.y + bob), (por ? 110 : 120) * s, on ? kart_col(i) : rgba(0.35f, 0.33f, 0.36f, 1), s);
    const char *dev = i == 0 ? "Keyboard or pad" : present ? "Gamepad" : "No gamepad";
    float tx_x = por ? r.x + r.w * 0.58f : r.x + r.w * 0.5f;
    float ty = por ? r.y + r.h * 0.45f : r.y + r.h * 0.66f;
    tx(U.f_small, dev, tx_x, ty, 20 * s, DIM, CW_ALIGN_CENTER);
    const char *st = on ? (i == 0 ? "READY" : "JOINED") : present ? dev_hint("Press (A) to join", "Press (A) to join", "Press (A) to join", "Press (A) to join") : "Plug one in";
    float blink = on ? 1 : present ? 0.55f + 0.45f * cw_sin(U.time * 6) : 0.5f;
    tx(U.f_hud, st, tx_x, ty + 34 * s, 22 * s, alpha(on ? LIME : present ? MUSTARD : DIM, blink), CW_ALIGN_CENTER);
    if (on && i > 0) tx(U.f_small, "(B) leave", tx_x, ty + 60 * s, 16 * s, DIM, CW_ALIGN_CENTER);
  }
  cw_rect cr = join_continue(W, H);
  bool can = n >= 2;
  float shake = U.shake > 0 ? 6 * s * cw_sin((float)U.shake * 2.5f) : 0;
  cr.x += shake;
  draw_button(cr, can ? "CONTINUE" : "WAITING FOR PLAYER 2", can, can, s, clamp01((float)U.mt / 20.0f));
  char b[128];
  snprintf(b, sizeof b, "%d player%s. Player 1 uses the keyboard or the first pad; the rest need a pad each.", n, n == 1 ? "" : "s");
  tx_fit(U.f_small, b, W * 0.5f, H - 78 * s, 18 * s, W * 0.92f, alpha(CREAM, 0.85f), CW_ALIGN_CENTER);
  draw_option_row_fwd(join_mode_rect(W, H), "MODE", U.gmode == RM_CUP ? "CUP" : "RACE", false, s, clamp01((float)U.mt / 20.0f));
  cw_rect br = back_button_rect(W, H);
  draw_button(br, "BACK", false, true, s * 0.8f, 1);
}

/* ── drawing: vehicle select ────────────────────────────────────────── */

static void stat_bar(cw_rect r, float f, cw_color c, float sp) {
  cw_r2d_round_rect(cw_rect_make(r.x + 1.5f * sp, r.y + 2 * sp, r.w, r.h), r.h * 0.4f, 3, rgba(0, 0, 0, 0.4f));
  cw_r2d_round_rect(r, r.h * 0.4f, 3, rgba(0.1f, 0.08f, 0.1f, 0.95f));
  cw_rect fr = cw_rect_make(r.x, r.y, r.w * clamp01(f), r.h);
  if (fr.w > 1) {
    cw_r2d_round_rect(fr, r.h * 0.4f, 3, c);
    cw_r2d_round_rect(cw_rect_make(fr.x + 1, fr.y + 1, fr.w - 2, fr.h * 0.4f), r.h * 0.2f, 3, rgba(1, 1, 1, 0.22f));
  }
  cw_r2d_round_rect_lines(r, r.h * 0.4f, 1.8f * sp, 3, INK);
}

static void draw_vpanel(int i, int n, float W, float H, float s) {
  cw_rect P = vpanel_rect(i, n, W, H);
  vlay l;
  vlayout(P, &l);
  float sp = l.sp;
  float ap = ease_back(((float)U.mt - (float)i * 4) / 18.0f);
  if (ap <= 0) return;
  bool ready = U.ready[i];
  const vehicle_def *vd = &VEH[U.veh[i]];
  cw_color pc = kart_col(U.col[i]);
  /* the card */
  cw_rect card = P;
  card.y += (1 - ap) * 60 * s;
  slab(card, 0, rgba(0.1f, 0.075f, 0.09f, 0.9f), s);
  cw_r2d_rect_lines(cw_rect_make(card.x + 3 * s, card.y + 3 * s, card.w - 6 * s, card.h - 6 * s), 3 * s, alpha(ready ? LIME : pc, ready ? 1 : 0.75f));
  /* offset every part by the slide */
  float dy = (1 - ap) * 60 * s;
#define SHIFT(r) ((cw_rect){ (r).x, (r).y + dy, (r).w, (r).h })
  char b[48];
  cw_rect tag = SHIFT(l.tag);
  para(tag, 8 * sp, pc);
  para_lines(tag, 8 * sp, 2 * sp, INK);
  snprintf(b, sizeof b, "P%d", i + 1);
  tx_o(U.f_title, b, tag.x + tag.w * 0.5f + 3 * sp, tag.y + tag.h * 0.5f, 15 * sp, INK, (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
  cw_rect nm = SHIFT(l.name);
  tx_fit(U.f_title, vd->name, nm.x + nm.w * 0.5f, nm.y + nm.h * 0.5f, 24 * sp, nm.w - 70 * sp, CREAM, CW_ALIGN_CENTER);
  if (!ready) {
    float wob = 2 * sp * cw_sin(U.time * 6);
    arrow(cw_v2(SHIFT(l.arrow_l).x + 15 * sp + wob, nm.y + nm.h * 0.5f), 9 * sp, -1, MUSTARD, sp);
    arrow(cw_v2(SHIFT(l.arrow_r).x + 15 * sp - wob, nm.y + nm.h * 0.5f), 9 * sp, 1, MUSTARD, sp);
  }
  /* the stage: a spotlight, the floor, the ride sliding in */
  cw_rect st = SHIFT(l.stage);
  cw_r2d_round_rect(st, 10 * sp, 5, rgba(0.17f, 0.13f, 0.15f, 1));
  cw_r2d_push_clip(st);
  vgrad(cw_rect_make(st.x, st.y, st.w, st.h * 0.7f), rgba(0.3f, 0.24f, 0.2f, 0.9f), rgba(0.13f, 0.1f, 0.12f, 1));
  cw_r2d_rect(cw_rect_make(st.x, st.y + st.h * 0.78f, st.w, st.h * 0.22f), rgba(0.09f, 0.07f, 0.09f, 1));
  cw_r2d_set_blend(CW_BLEND_ADD);
  cw_r2d_circle(cw_v2(st.x + st.w * 0.5f, st.y + st.h * 0.62f), st.h * 0.62f, 28, rgba(0.35f, 0.24f, 0.12f, 0.35f));
  cw_r2d_circle(cw_v2(st.x + st.w * 0.5f, st.y + st.h * 0.62f), st.h * 0.38f, 24, rgba(0.35f, 0.24f, 0.12f, 0.3f));
  cw_r2d_set_blend(CW_BLEND_ALPHA);
  float slide = (1 - ease_out((float)U.veh_anim[i] / 16.0f)) * (float)U.veh_dir[i] * st.w * 0.5f;
  float u = fminf(st.w / 3.9f, st.h / 2.15f);
  float bob = ready ? 0 : 1.2f * sp * cw_sin(U.time * 3.2f + (float)i);
  draw_kart_side(st.x + st.w * 0.5f + slide, st.y + st.h * 0.8f + bob, u, U.veh[i], U.col[i], ready ? 1.0f : 0.25f, U.time + (float)i);
  cw_r2d_pop_clip();
  cw_r2d_round_rect_lines(st, 10 * sp, 2.5f * sp, 5, INK);
  for (int v = 0; v < NVEH; v++) {
    cw_vec2 d = l.dot[v];
    d.y += dy;
    cw_r2d_circle(d, l.dot_r + 1.4f * sp, 10, INK);
    cw_r2d_circle(d, l.dot_r, 10, v == U.veh[i] ? MUSTARD : rgba(0.45f, 0.4f, 0.4f, 1));
  }
  /* the numbers behind the ride */
  static const char *const STAT[7] = { "SPEED", "ACCEL", "HANDLING", "GRIP", "WEIGHT", "DRIFT", "BOOST" };
  float val[7] = { vd->top, vd->accel, vd->turn, vd->grip, vd->mass, vd->drift, vd->boost };
  cw_rect sr = SHIFT(l.stats);
  float rowh = sr.h / 7;
  for (int k = 0; k < 7; k++) {
    float f = 0.12f + 0.88f * clamp01((val[k] - U.stat_lo[k]) / (U.stat_hi[k] - U.stat_lo[k]));
    float cy = sr.y + (float)k * rowh + rowh * 0.5f;
    float lw = sr.w * 0.38f;
    tx_o(U.f_small, STAT[k], sr.x, cy, fmaxf(rowh * 0.74f, 11), CREAM, (txopt){ .align = CW_ALIGN_LEFT, .outline = 0.01f, .no_shadow = true });
    cw_color c = f < 0.5f ? mix(RUST, MUSTARD, f * 2) : mix(MUSTARD, LIME, (f - 0.5f) * 2);
    stat_bar(cw_rect_make(sr.x + lw, cy - rowh * 0.27f, sr.w - lw, rowh * 0.54f), f, c, sp);
  }
  cw_rect br = SHIFT(l.blurb);
  tx_para(U.f_hud, vd->blurb, br, fmaxf(13 * sp, 11), alpha(CREAM, 0.92f), CW_ALIGN_LEFT);
  /* paint */
  for (int c = 0; c < N_COLOURS; c++) {
    cw_rect q = SHIFT(l.sw[c]);
    cw_vec2 ctr = cw_v2(q.x + q.w * 0.5f, q.y + q.h * 0.5f);
    bool taken = colour_taken(i, c, n);
    cw_r2d_circle(ctr, q.w * 0.5f + 1.5f * sp, 14, INK);
    cw_r2d_circle(ctr, q.w * 0.5f - 0.5f * sp, 14, alpha(kart_col(c), taken ? 0.25f : 1));
    if (taken) cw_r2d_line(cw_v2(ctr.x - q.w * 0.28f, ctr.y - q.w * 0.28f), cw_v2(ctr.x + q.w * 0.28f, ctr.y + q.w * 0.28f), 1.6f * sp, alpha(CREAM, 0.5f));
    if (c == U.col[i]) {
      cw_r2d_circle_lines(ctr, q.w * 0.5f + 3.2f * sp, 2.4f * sp, 16, CREAM);
      cw_r2d_circle_lines(ctr, q.w * 0.5f + 5.2f * sp, 1.2f * sp, 16, INK);
    }
  }
  /* ready */
  cw_rect rb = SHIFT(l.ready);
  slab(rb, 10 * sp, ready ? LIME : (i == 0 && pointer_mode() && in_rect(N.m, l.ready)) ? alpha(MUSTARD, 0.95f) : alpha(MUSTARD, 0.85f), sp);
  const char *rl = ready ? "READY!" : (i == 0 ? dev_hint("(A) READY", "ENTER: READY", "READY", "READY") : "(A) READY");
  tx_o(U.f_title, rl, rb.x + rb.w * 0.5f + 4 * sp, rb.y + rb.h * 0.5f, 18 * sp, INK, (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
#undef SHIFT
}

static void draw_vehicle(float W, float H, float s) {
  backdrop(W, H, s, 0.82f);
  int n = U.setup.humans;
  header(W, H, s, n > 1 ? "CHOOSE YOUR RIDES" : "CHOOSE YOUR RIDE", NULL);
  for (int i = 0; i < n; i++) draw_vpanel(i, n, W, H, s);
  if (n == 3 && !is_portrait(W, H)) {
    /* nothing in the fourth place: keep the room clear */
  }
  draw_button(back_button_rect(W, H), "BACK", false, true, s * 0.8f, 1);
  hint_line(W, H, s,
            n > 1 ? "Left / right: ride    Up / down: paint    (A) ready    (B) undo"
                  : dev_hint("D-pad: ride and paint    (A) ready    (B) back", "Arrows: ride and paint   Enter: ready   Esc: back", "Click a ride, a paint, then READY", "Tap a ride, a paint, then READY"));
}

/* ── drawing: map select ────────────────────────────────────────────── */

static void draw_map_card(const maplay *m, int i, float s) {
  cw_rect r = m->card[i];
  bool sel = U.map == i;
  float ap = ease_back(((float)U.mt - (float)i * 3) / 16.0f);
  if (ap <= 0) return;
  float pop = sel && U.mapfocus == 0 ? 1.035f + 0.01f * cw_sin(U.time * 7) : 1;
  r.y += (1 - ap) * 70 * s;
  cw_rect q = cw_rect_make(r.x - r.w * (pop - 1) * 0.5f, r.y - r.h * (pop - 1) * 0.5f, r.w * pop, r.h * pop);
  slab(q, 0, sel ? rgba(0.2f, 0.15f, 0.1f, 0.95f) : rgba(0.1f, 0.075f, 0.09f, 0.88f), s);
  cw_r2d_rect_lines(cw_rect_make(q.x + 3 * s, q.y + 3 * s, q.w - 6 * s, q.h - 6 * s), 3 * s, sel ? (U.mapfocus == 0 ? MUSTARD : alpha(MUSTARD, 0.55f)) : rgba(0.4f, 0.36f, 0.36f, 0.6f));
  cw_rect mr = cw_rect_make(q.x + 8 * s, q.y + 6 * s, q.w - 16 * s, q.h - 30 * s);
  if (U.mm[i].ok) mm_draw(&U.mm[i], mr, 6 * s, s, 2.6f * s + 0.5f, false);
  else tx(U.f_small, "...", mr.x + mr.w * 0.5f, mr.y + mr.h * 0.5f, 22 * s, DIM, CW_ALIGN_CENTER);
  tx_fit(U.f_hud, MAPS[i]->name, q.x + q.w * 0.5f, q.y + q.h - 14 * s, 16 * s, q.w - 16 * s, sel ? MUSTARD : CREAM, CW_ALIGN_CENTER);
  if (U.gmode == RM_TRIAL) {
    /* the trial's medal for the map: a ring until one is won */
    cw_vec2 c = cw_v2(q.x + q.w - 17 * s, q.y + 17 * s);
    int md = trial_medal_of(i, SAVE.trial_cs[i]);
    if (md > 0) draw_medal(c, 11 * s, md, s, U.time);
    else cw_r2d_circle_lines(c, 8 * s, 2 * s, 14, alpha(DIM, 0.5f));
  } else if (SAVE.best_lap_cs[i]) {
    cw_vec2 c = cw_v2(q.x + q.w - 14 * s, q.y + 14 * s);
    cw_r2d_circle(c, 7 * s, 12, INK);
    cw_r2d_circle(c, 5.5f * s, 12, MUSTARD);
  }
}

static void draw_option_row(cw_rect r, const char *label, const char *value, bool sel, float s, float ap) {
  if (ap <= 0) return;
  r.x += (1 - ease_back(ap)) * 100 * s;
  slab(r, 10 * s, sel ? alpha(MUSTARD, 0.95f) : PANEL, s * 0.8f);
  cw_color tc = sel ? INK : CREAM;
  tx_o(U.f_hud, label, r.x + 22 * s, r.y + r.h * 0.5f, fminf(r.h * 0.5f, 21 * s), tc, (txopt){ .align = CW_ALIGN_LEFT, .outline = sel ? 0.01f : 0, .no_shadow = sel });
  float sz = fminf(r.h * 0.5f, 24 * s);
  float vx = r.x + r.w * 0.66f;
  tx_o(U.f_title, value, vx, r.y + r.h * 0.5f, sz, tc, (txopt){ .align = CW_ALIGN_CENTER, .outline = sel ? 0.01f : 0, .no_shadow = sel });
  arrow(cw_v2(r.x + r.w * 0.66f - fmaxf(tx_width(U.f_title, value, sz) * 0.5f + 20 * s, 46 * s), r.y + r.h * 0.5f), 8 * s, -1, sel ? INK : MUSTARD, s);
  arrow(cw_v2(r.x + r.w * 0.66f + fmaxf(tx_width(U.f_title, value, sz) * 0.5f + 20 * s, 46 * s), r.y + r.h * 0.5f), 8 * s, 1, sel ? INK : MUSTARD, s);
}

static void draw_option_row_fwd(cw_rect r, const char *label, const char *value, bool sel, float s, float ap) { draw_option_row(r, label, value, sel, s, ap); }

static void draw_map(float W, float H, float s) {
  backdrop(W, H, s, 0.86f);
  header(W, H, s, U.gmode == RM_TRIAL ? "TIME TRIAL" : "CHOOSE A TRACK", U.gmode == RM_TRIAL ? "One lap, you against your best" : NULL);
  maplay m;
  map_layout(W, H, &m);
  for (int i = 0; i < NMAPS; i++) draw_map_card(&m, i, s);
  /* the selected track in detail */
  float ap = clamp01(((float)U.mt - 10) / 14.0f);
  cw_rect d = m.detail;
  d.y += (1 - ease_out(ap)) * 40 * s;
  slab(d, 0, rgba(0.1f, 0.075f, 0.09f, 0.9f), s);
  const map_def *md = MAPS[U.map];
  cw_rect big = m.big;
  big.y += (1 - ease_out(ap)) * 40 * s;
  cw_r2d_round_rect(big, 10 * s, 5, rgba(0.05f, 0.04f, 0.06f, 0.9f));
  if (U.mm[U.map].ok) mm_draw(&U.mm[U.map], big, 14 * s, s, 4.4f * s + 0.8f, true);
  cw_r2d_round_rect_lines(big, 10 * s, 2 * s, 5, INK);
  float tx0 = big.x + big.w + 16 * s, tw = d.x + d.w - tx0 - 14 * s;
  tx_fit(U.f_title, md->name, tx0, d.y + 28 * s, 28 * s, tw, MUSTARD, CW_ALIGN_LEFT);
  float bh = tx_para(U.f_hud, md->blurb, cw_rect_make(tx0, d.y + 50 * s, tw, 70 * s), 17 * s, CREAM, CW_ALIGN_LEFT);
  const mmap_t *mm = &U.mm[U.map];
  float iy = d.y + 50 * s + bh + 16 * s;
  char b[96];
  if (mm->ok && iy < d.y + d.h - 24 * s) {
    snprintf(b, sizeof b, "%.2f km per lap", mm->length / 1000.0f);
    tx(U.f_small, b, tx0, iy, 18 * s, CREAM, CW_ALIGN_LEFT);
    iy += 26 * s;
  }
  if (mm->ok && iy < d.y + d.h - 18 * s && U.gmode != RM_TRIAL) {
    float lx = tx0;
    for (int k = 0; k < 3; k++) {
      int cnt = k == 0 ? mm->njumps : k == 1 ? mm->nloops : mm->ntunnels;
      snprintf(b, sizeof b, "%d %s", cnt, k == 0 ? (cnt == 1 ? "jump" : "jumps") : k == 1 ? (cnt == 1 ? "loop" : "loops") : (cnt == 1 ? "tunnel" : "tunnels"));
      cw_vec2 dc = cw_v2(lx + 7 * s, iy);
      if (k == 0) cw_r2d_circle(dc, 6 * s, 10, rgba(1.0f, 0.22f, 0.22f, 1));
      else if (k == 1) cw_r2d_circle_lines(dc, 6 * s, 2.5f * s, 12, MUSTARD);
      else cw_r2d_circle(dc, 6 * s, 10, rgba(0.7f, 0.7f, 0.75f, 1));
      tx(U.f_small, b, lx + 18 * s, iy, 17 * s, CREAM, CW_ALIGN_LEFT);
      lx += 18 * s + tx_width(U.f_small, b, 17 * s) + 14 * s;
    }
    iy += 26 * s;
  }
  if (U.gmode == RM_TRIAL) {
    if (iy < d.y + d.h - 8 * s) trial_detail(tx0, iy, d.y + d.h, tw, U.map, s);
  } else if (iy < d.y + d.h - 10 * s) {
    if (SAVE.best_lap_cs[U.map]) {
      char t[24];
      fmt_time(t, sizeof t, (float)SAVE.best_lap_cs[U.map] / 100.0f);
      snprintf(b, sizeof b, "Best lap  %s", t);
      tx(U.f_hud, b, tx0, iy, 19 * s, LIME, CW_ALIGN_LEFT);
    } else tx(U.f_small, "No lap record yet", tx0, iy, 18 * s, DIM, CW_ALIGN_LEFT);
  }
  /* the legend of the outline, under the big map */
  /* the race options */
  int rows[MAX_OPT_ROWS];
  int nr = option_rows(rows);
  for (int r = 0; r < nr; r++) {
    char v[24];
    opt_text(rows[r], v, sizeof v);
    draw_option_row(m.optrow[r], OPT_LABEL[rows[r]], v, U.mapfocus == r + 1, s, clamp01(((float)U.mt - 14 - (float)r * 3) / 14.0f));
    if (rows[r] == OPT_ASSIST && (U.assist & 3) == 0 && SAVE.races < 3) assist_tag(m.optrow[r], s);
  }
  bool sf = U.mapfocus == 0 || U.mapfocus == nr + 1;
  draw_button(m.start, U.gmode == RM_TRIAL ? "START TRIAL" : "START RACE", sf, true, s * 0.82f, clamp01(((float)U.mt - 22) / 14.0f));
  draw_button(back_button_rect(W, H), "BACK", false, true, s * 0.8f, 1);
  hint_line(W, H, s, dev_hint("D-pad: choose   (A) go   down: options   (B) back", "Arrows choose   Enter: go   Down: options   Esc: back", "Click a track, again to go", "Tap a track, then START"));
}

/* ── drawing: options and the controls page ─────────────────────────── */

static void draw_slider(cw_rect r, int v, float s, bool sel) {
  for (int i = 0; i < 10; i++) {
    float gap = 3 * s, w = (r.w - 9 * gap) / 10;
    cw_rect q = cw_rect_make(r.x + (float)i * (w + gap), r.y + (i < v ? 0 : r.h * 0.3f), w, i < v ? r.h : r.h * 0.7f);
    cw_color c = i < v ? mix(TEAL, MUSTARD, (float)i / 9.0f) : rgba(0.25f, 0.22f, 0.25f, 1);
    if (sel && i < v) c = mix(c, rgba(1, 1, 1, 1), 0.15f);
    cw_r2d_round_rect(q, 3 * s, 3, c);
    cw_r2d_round_rect_lines(q, 3 * s, 1.5f * s + 0.3f, 3, INK);
  }
}

static void draw_options(float W, float H, float s) {
  backdrop(W, H, s, 0.85f);
  header(W, H, s, "OPTIONS", NULL);
  cw_rect p = opt_panel(W, H);
  float ap = ease_out((float)U.mt / 14.0f);
  p.y += (1 - ap) * 40 * s;
  slab(p, 0, rgba(0.09f, 0.065f, 0.08f, 0.92f), s);
  for (int i = 0; i < OI_COUNT; i++) {
    cw_rect r = opt_row(i, W, H);
    r.y += (1 - ap) * 40 * s;
    bool sel = U.opt_sel == i;
    float a = clamp01(((float)U.mt - (float)i * 3) / 12.0f);
    if (a <= 0) continue;
    slab(r, 10 * s, sel ? alpha(MUSTARD, 0.95f) : PANEL, s * 0.8f);
    cw_color tc = sel ? INK : CREAM;
    txopt o = { .align = CW_ALIGN_LEFT, .outline = sel ? 0.01f : 0, .no_shadow = sel };
    tx_o(U.f_title, OPTPAGE_ITEMS[i], r.x + 24 * s, r.y + r.h * 0.5f, 20 * s, tc, o);
    if (i == OI_ASSIST_STEER || i == OI_ASSIST_GAS) {
      bool on = (U.assist & (i == OI_ASSIST_STEER ? 1 : 2)) != 0;
      o.align = CW_ALIGN_RIGHT;
      tx_o(U.f_title, on ? "ON" : "OFF", r.x + r.w - 24 * s, r.y + r.h * 0.5f, 20 * s, sel ? INK : (on ? LIME : DIM), o);
    } else if (i == OI_GFX) {
      char b[64];
      snprintf(b, sizeof b, "%s  -  %s", cw_quality_label(), cw_render_res_label());
      o.align = CW_ALIGN_RIGHT;
      tx_o(U.f_hud, b, r.x + r.w - 40 * s, r.y + r.h * 0.5f, 20 * s, tc, o);
      tri(cw_v2(r.x + r.w - 22 * s, r.y + r.h * 0.5f), 9 * s, 0, sel ? INK : MUSTARD);
    } else if (i == OI_MUSIC || i == OI_SFX) {
      int v = i == OI_MUSIC ? SAVE.music : SAVE.sfx;
      draw_slider(opt_slider(r, s), v, s, sel);
      char b[16];
      snprintf(b, sizeof b, "%d", v);
      o.align = CW_ALIGN_RIGHT;
      tx_o(U.f_hud, b, r.x + r.w - 6 * s, r.y + r.h * 0.5f, 18 * s, tc, o);
    } else if (i == OI_CONTROLS) {
      tri(cw_v2(r.x + r.w - 22 * s, r.y + r.h * 0.5f), 9 * s, 0, sel ? INK : MUSTARD);
    }
  }
  hint_line(W, H, s, dev_hint("D-pad: move and adjust   (A) choose   (B) back", "Arrows: move and adjust   Enter: choose   Esc: back", "Click to choose, drag the bars", "Tap to choose, drag the bars"));
  /* what an assist does, for the row picked */
  if (U.opt_sel == OI_ASSIST_STEER || U.opt_sel == OI_ASSIST_GAS) {
    const char *d = U.opt_sel == OI_ASSIST_STEER ? "A gentle pull back to the road when a wall is close. Costs no speed."
                                                    : "The throttle stays down unless you brake. Costs no speed; the rocket start is still yours.";
    cw_rect pp = opt_panel(W, H);
    tx_fit(U.f_small, d, W * 0.5f, pp.y + pp.h + 16 * s, 17 * s, W * 0.92f, alpha(CREAM, 0.9f), CW_ALIGN_CENTER);
  }
}

typedef struct ctl_row {
  const char *chips[3];
  const char *what;
} ctl_row;

static void draw_controls(float W, float H, float s) {
  backdrop(W, H, s, 0.88f);
  header(W, H, s, "CONTROLS", NULL);
  bool por = is_portrait(W, H);
  static const ctl_row PADROWS[] = {
    { { "A", "RT" }, "Accelerate" }, { { "B", "LT" }, "Brake and reverse" }, { { "STICK" }, "Steer, and tilt in the air" }, { { "LB", "RB" }, "Drift: hold to slide, let go for a boost" },
    { { "X" }, "Use your item" }, { { "Y" }, "Do a trick in the air" }, { { "R-STICK" }, "Look behind" }, { { "BACK" }, "Hold to get back on the track" }, { { "START" }, "Pause" },
  };
  static const ctl_row KEYROWS[] = {
    { { "W", "UP" }, "Accelerate" }, { { "S", "DOWN" }, "Brake and reverse" }, { { "A D", "LEFT RIGHT" }, "Steer" }, { { "R", "F" }, "Tilt in the air" },
    { { "SHIFT" }, "Drift: hold to slide, let go for a boost" }, { { "E", "CTRL" }, "Use your item" }, { { "Q" }, "Do a trick in the air" }, { { "C" }, "Look behind" },
    { { "BACKSPACE" }, "Hold to get back on the track" }, { { "ESC", "P" }, "Pause" },
  };
  int np = (int)(sizeof PADROWS / sizeof PADROWS[0]), nk = (int)(sizeof KEYROWS / sizeof KEYROWS[0]);
  float gx = W * 0.03f, gw = W * 0.94f, gap = 14 * s;
  float y0 = 96 * s, hh = H - y0 - 104 * s;
  cw_rect pr, kr;
  if (por) {
    pr = cw_rect_make(gx, y0, gw, hh * 0.5f - gap * 0.5f);
    kr = cw_rect_make(gx, y0 + hh * 0.5f + gap * 0.5f, gw, hh * 0.5f - gap * 0.5f);
  } else {
    pr = cw_rect_make(gx, y0, gw * 0.5f - gap * 0.5f, hh);
    kr = cw_rect_make(gx + gw * 0.5f + gap * 0.5f, y0, gw * 0.5f - gap * 0.5f, hh);
  }
  float ap = ease_out((float)U.mt / 14.0f);
  for (int side = 0; side < 2; side++) {
    cw_rect r = side ? kr : pr;
    r.y += (1 - ap) * 40 * s;
    slab(r, 0, rgba(0.09f, 0.065f, 0.08f, 0.92f), s);
    const ctl_row *rows = side ? KEYROWS : PADROWS;
    int n = side ? nk : np;
    tx(U.f_title, side ? "KEYBOARD" : "GAMEPAD", r.x + 20 * s, r.y + 26 * s, 22 * s, MUSTARD, CW_ALIGN_LEFT);
    if (!side) draw_pad_icon(cw_v2(r.x + r.w - 52 * s, r.y + 28 * s), 64 * s, TEAL, s);
    float top = r.y + 52 * s, avail = r.h - 52 * s - 12 * s;
    float rh = fminf(avail / (float)n, 40 * s);
    float cwid = r.w * 0.36f;
    for (int i = 0; i < n; i++) {
      float cy = top + (float)i * rh + rh * 0.5f;
      float x = r.x + 18 * s;
      float ch = rh * 0.74f;
      for (int c = 0; c < 3 && rows[i].chips[c]; c++) {
        const char *lb = rows[i].chips[c];
        bool round = !side && strlen(lb) == 1;
        static const cw_color BC[4] = { { 0.4f, 0.85f, 0.35f, 1 }, { 0.95f, 0.3f, 0.25f, 1 }, { 0.3f, 0.55f, 0.95f, 1 }, { 1.0f, 0.82f, 0.2f, 1 } };
        cw_color col = CREAM;
        if (round) col = lb[0] == 'A' ? BC[0] : lb[0] == 'B' ? BC[1] : lb[0] == 'X' ? BC[2] : BC[3];
        else if (!side) col = rgba(0.75f, 0.78f, 0.85f, 1);
        else col = rgba(0.92f, 0.9f, 0.84f, 1);
        float cwv = chip_w(lb, ch);
        if (x + cwv > r.x + cwid) break;
        chip(cw_v2(x, cy), lb, col, ch, s, round);
        x += cwv + 6 * s;
      }
      tx_fit(U.f_hud, rows[i].what, r.x + cwid + 8 * s, cy, 17 * s, r.w - cwid - 24 * s, CREAM, CW_ALIGN_LEFT);
    }
  }
  tx_fit(U.f_small, "Players 2 to 4 each use a gamepad. On a touch screen: left thumb steers, the buttons on the right do the rest.", W * 0.5f, H - 74 * s, 17 * s, W * 0.94f,
         alpha(CREAM, 0.85f), CW_ALIGN_CENTER);
  hint_line(W, H, s, dev_hint("(A) or (B) to go back", "Any key to go back", "Click to go back", "Tap to go back"));
}

/* ── drawing: pause ─────────────────────────────────────────────────── */

static void draw_pause(float W, float H, float s) {
  cw_r2d_rect(cw_rect_make(0, 0, W, H), rgba(0.02f, 0.01f, 0.03f, 0.62f * ease_out((float)U.mt / 10.0f)));
  float t = ease_back((float)U.mt / 16.0f);
  cw_rect b = cw_rect_make(W * 0.5f - 200 * s, H * 0.1f - (1 - t) * 120 * s, 400 * s, 70 * s);
  slab(b, 18 * s, rgba(0.1f, 0.07f, 0.09f, 0.95f), s);
  para(cw_rect_make(b.x + 6 * s, b.y + b.h - 9 * s, b.w - 12 * s, 6 * s), 0, MUSTARD);
  tx_o(U.f_title, "PAUSED", b.x + b.w * 0.5f + 8 * s, b.y + 32 * s, 44 * s, MUSTARD, (txopt){ .align = CW_ALIGN_CENTER });
  if (game_views() > 1) {
    char p[32];
    snprintf(p, sizeof p, "Player %d paused", U.pauser + 1);
    tx(U.f_hud, p, W * 0.5f, b.y + b.h + 24 * s, 20 * s, CREAM, CW_ALIGN_CENTER);
  }
  for (int i = 0; i < 5; i++)
    draw_button(list_rect(i, W, H, is_portrait(W, H) ? 0.3f : 0.3f), i == 1 && game_is_trial() ? "RETRY" : PAUSE_ITEMS[i], U.sel == i, true, s, clamp01(((float)U.mt - (float)i * 3) / 14.0f));
  hint_line(W, H, s, dev_hint("D-pad move   (A) choose   START resume", "Arrows move   Enter choose   Esc resume", "Click to choose", "Tap to choose"));
}

/* ── drawing: results ───────────────────────────────────────────────── */

static const char *driver_name(const result_row *r, char *buf, size_t n) {
  if (r->human >= 0) snprintf(buf, n, "PLAYER %d", r->human + 1);
  else snprintf(buf, n, "%s", CPU_NAME[r->kart % MAXK]);
  return buf;
}

static void draw_confetti(cw_rect area, float s, float t) {
  uint32_t seed = 99u;
  for (int i = 0; i < 46; i++) {
    seed = seed * 1664525u + 1013904223u;
    float x = (float)(seed >> 8) / 16777216.0f;
    seed = seed * 1664525u + 1013904223u;
    float ph = (float)(seed >> 8) / 16777216.0f;
    seed = seed * 1664525u + 1013904223u;
    int col = (int)(seed >> 12) % 8;
    float y = fmodf(ph + t * (0.18f + 0.1f * (float)(i % 4)), 1.0f);
    float px = area.x + area.w * (x + 0.04f * cw_sin(t * 3 + (float)i));
    float py = area.y + area.h * y;
    float a = t * 5 + (float)i;
    cw_vec2 c = cw_v2(px, py);
    cw_vec2 p[4];
    for (int k = 0; k < 4; k++) {
      float ka = a + (float)k * 1.5708f;
      p[k] = cw_v2(c.x + cw_cos(ka) * 5 * s * (k & 1 ? 0.4f : 1), c.y + cw_sin(ka) * 5 * s * (k & 1 ? 0.4f : 1));
    }
    cw_r2d_polygon(p, 4, alpha(kart_col(col), 0.9f));
  }
}

static void draw_trial_results(float W, float H, float s);
static void draw_results(float W, float H, float s) {
  if (!game_loaded()) return;
  if (U.tr.on) {
    draw_trial_results(W, H, s);
    return;
  }
  bool por = is_portrait(W, H);
  int t = U.res_ticks;
  /* a clean look at the winner first, then the board */
  float head = ease_back((float)t / 20.0f);
  float panel = ease_out(((float)t - 90) / 24.0f);
  const result_row *w = &U.res[0];
  char nm[32];
  driver_name(w, nm, sizeof nm);
  {
    bool human_won = U.res_winner_human == 1;
    char b[64];
    if (human_won) snprintf(b, sizeof b, "%s WINS!", nm);
    else snprintf(b, sizeof b, "%s TAKES IT", nm);
    float bw = fminf(W * 0.94f, tx_width(U.f_title, b, 46 * s) + 110 * s);
    float by = 22 * s - (1 - head) * 120 * s;
    cw_rect r = cw_rect_make(W * 0.5f - bw * 0.5f, by, bw, 72 * s);
    slab(r, 18 * s, rgba(0.1f, 0.07f, 0.09f, 0.94f), s);
    para(cw_rect_make(r.x + 6 * s, r.y + r.h - 9 * s, r.w - 12 * s, 6 * s), 0, PLACE_COL[0]);
    tx_fit(U.f_title, b, r.x + r.w * 0.5f + 8 * s, r.y + 33 * s, 46 * s, r.w - 90 * s, PLACE_COL[0], CW_ALIGN_CENTER);
  }
  if (panel > 0) {
    /* the table, left (or top in portrait) */
    float gx = W * 0.025f;
    cw_rect tab;
    if (por) tab = cw_rect_make(gx, 110 * s + (1 - panel) * 200 * s, W - 2 * gx, 10 * 36 * s * 0.0f + 8 * 30 * s + 44 * s);
    else tab = cw_rect_make(gx - (1 - panel) * 500 * s, 108 * s, W * 0.56f, H - 108 * s - 28 * s);
    float pa = panel;
    slab(tab, 0, rgba(0.09f, 0.065f, 0.08f, 0.92f * pa), s);
    bool wide_cols = tab.w > 520 * s;
    float rowh = por ? 30 * s : fminf(36 * s, (tab.h - 100 * s) / 8.0f);
    float cx_rank = tab.x + 22 * s, cx_name = tab.x + 48 * s;
    float cx_time = tab.x + tab.w * (wide_cols ? 0.47f : 0.56f), cx_best = tab.x + tab.w * (wide_cols ? 0.645f : 0.76f);
    float cx_hits = tab.x + tab.w * 0.775f, cx_tr = tab.x + tab.w * 0.865f, cx_pts = tab.x + tab.w - 18 * s;
    float hy = tab.y + 22 * s;
    float hsz = 15 * s;
    tx(U.f_small, "DRIVER", cx_name, hy, hsz, DIM, CW_ALIGN_LEFT);
    tx(U.f_small, "TIME", cx_time, hy, hsz, DIM, CW_ALIGN_CENTER);
    tx(U.f_small, "BEST LAP", cx_best, hy, hsz, DIM, CW_ALIGN_CENTER);
    if (wide_cols) {
      tx(U.f_small, "HITS", cx_hits, hy, hsz, DIM, CW_ALIGN_CENTER);
      tx(U.f_small, "TRICKS", cx_tr, hy, hsz, DIM, CW_ALIGN_CENTER);
    }
    tx(U.f_small, "PTS", cx_pts, hy, hsz, DIM, CW_ALIGN_RIGHT);
    if (U.cup.on) {
      char cb[40];
      snprintf(cb, sizeof cb, "%s  -  RACE %d OF %d", CUPS[U.cup.cup].name, U.cup.race + 1, cup_nraces());
      tx(U.f_title, cb, tab.x + 24 * s, tab.y + tab.h - 40 * s, 13 * s, alpha(MUSTARD, pa), CW_ALIGN_LEFT);
    }
    for (int i = 0; i < U.res_n; i++) {
      const result_row *r = &U.res[i];
      float ra = clamp01(((float)t - 100 - (float)i * 5) / 16.0f);
      if (ra <= 0) continue;
      float y = tab.y + 40 * s + (float)i * rowh;
      float off = (1 - ease_out(ra)) * 120 * s;
      cw_rect rr = cw_rect_make(tab.x + 10 * s + off, y, tab.w - 20 * s, rowh - 3 * s);
      bool human = r->human >= 0;
      para(rr, 6 * s, human ? alpha(mix(PANEL, kart_col(r->colour), 0.28f), ra) : alpha(rgba(0.14f, 0.11f, 0.13f, 0.8f), ra));
      if (human) para_lines(rr, 6 * s, 2 * s, alpha(kart_col(r->colour), ra));
      float cy = rr.y + rr.h * 0.5f;
      char b[48];
      snprintf(b, sizeof b, "%d", r->place);
      cw_color pc = r->place <= 3 ? PLACE_COL[r->place - 1] : CREAM;
      tx(U.f_title, b, rr.x + 14 * s, cy, rowh * 0.5f, alpha(pc, ra), CW_ALIGN_CENTER);
      cw_r2d_circle(cw_v2(rr.x + 40 * s, cy), 6 * s, 12, alpha(INK, ra));
      cw_r2d_circle(cw_v2(rr.x + 40 * s, cy), 4.8f * s, 12, alpha(kart_col(r->colour), ra));
      driver_name(r, nm, sizeof nm);
      float nx = rr.x + 54 * s;
      tx_fit(U.f_hud, nm, nx, cy, rowh * 0.5f, cx_time - nx - 40 * s, alpha(human ? CREAM : CREAM, ra), CW_ALIGN_LEFT);
      if (wide_cols && rowh > 28 * s) {
        float nw = tx_width(U.f_hud, nm, rowh * 0.5f);
        if (nx + nw + 70 * s < cx_time - 30 * s) tx(U.f_small, VEH[r->veh].name, nx + nw + 10 * s, cy + 1 * s, rowh * 0.4f, alpha(DIM, ra), CW_ALIGN_LEFT);
      }
      if (r->finished) fmt_time(b, sizeof b, r->time);
      else snprintf(b, sizeof b, "DNF");
      tx(U.f_hud, b, cx_time, cy, rowh * 0.46f, alpha(r->finished ? CREAM : DIM, ra), CW_ALIGN_CENTER);
      fmt_time(b, sizeof b, r->best);
      tx(U.f_hud, b, cx_best, cy, rowh * 0.46f, alpha(r->record ? LIME : CREAM, ra), CW_ALIGN_CENTER);
      if (r->record) {
        /* a star for a new record */
        cw_vec2 sc = cw_v2(cx_best + tx_width(U.f_hud, b, rowh * 0.46f) * 0.5f + 12 * s, cy);
        float pulse = 1 + 0.15f * cw_sin(U.time * 8);
        cw_vec2 sp[10];
        for (int k = 0; k < 10; k++) {
          float a = -CW_PI * 0.5f + (float)k * CW_PI / 5.0f, rr2 = (k & 1 ? 3.6f : 8.0f) * s * pulse;
          sp[k] = cw_v2(sc.x + cw_cos(a) * rr2, sc.y + cw_sin(a) * rr2);
        }
        cw_r2d_fan(sc, sp, 10, true, alpha(MUSTARD, ra));
        cw_r2d_polygon_lines(sp, 10, 1.4f * s, alpha(INK, ra));
      }
      if (wide_cols) {
        snprintf(b, sizeof b, "%d", r->hits);
        tx(U.f_hud, b, cx_hits, cy, rowh * 0.46f, alpha(CREAM, ra), CW_ALIGN_CENTER);
        snprintf(b, sizeof b, "%d", r->tricks);
        tx(U.f_hud, b, cx_tr, cy, rowh * 0.46f, alpha(CREAM, ra), CW_ALIGN_CENTER);
      }
      snprintf(b, sizeof b, "%d", r->points);
      tx(U.f_title, b, cx_pts, cy, rowh * 0.46f, alpha(MUSTARD, ra), CW_ALIGN_RIGHT);
    }
    bool rec = false;
    for (int i = 0; i < U.res_n; i++) rec = rec || U.res[i].record;
    if (rec) {
      float pulse = 0.7f + 0.3f * cw_sin(U.time * 6);
      tx(U.f_title, "NEW LAP RECORD", tab.x + 24 * s, tab.y + tab.h - 62 * s, 14 * s, alpha(LIME, pulse * pa), CW_ALIGN_LEFT);
    }
    /* the podium */
    if (!por) {
      cw_rect pod = cw_rect_make(W * 0.6f + (1 - panel) * 400 * s, 108 * s, W * 0.37f, H - 108 * s - 28 * s - 52 * s - 18 * s);
      float baseY = pod.y + pod.h - 6 * s;
      static const int ord[3] = { 1, 0, 2 };
      static const float hgt[3] = { 80, 118, 56 };
      float bw = pod.w / 3 - 6 * s;
      draw_confetti(cw_rect_make(pod.x, pod.y - 60 * s, pod.w, pod.h + 60 * s), s, U.time);
      for (int c = 0; c < 3 && c < U.res_n; c++) {
        int pl = ord[c];
        const result_row *r = &U.res[pl];
        float bx = pod.x + (float)c * (bw + 9 * s);
        float bh2 = hgt[c] * s * ease_out(((float)t - 120 - (float)c * 6) / 20.0f);
        cw_rect blk = cw_rect_make(bx, baseY - bh2, bw, bh2);
        if (bh2 > 1) {
          slab(blk, 0, shade(PLACE_COL[pl], 0.9f), s);
          char b[16];
          snprintf(b, sizeof b, "%d", pl + 1);
          tx_o(U.f_title, b, bx + bw * 0.5f, blk.y + fminf(bh2 * 0.5f, 34 * s), 40 * s, INK, (txopt){ .align = CW_ALIGN_CENTER, .outline = 0.01f, .no_shadow = true });
        }
        float u = bw / 4.2f;
        float hop = pl == 0 ? fabsf(cw_sin(U.time * 4)) * 8 * s : 0;
        draw_kart_side(bx + bw * 0.5f, baseY - bh2 - hop, u, r->veh, r->colour, pl == 0 ? 0.8f : 0.2f, U.time * 0.6f);
        driver_name(r, nm, sizeof nm);
        tx_fit(U.f_hud, nm, bx + bw * 0.5f, baseY - bh2 - u * 1.9f - hop - 6 * s, 15 * s, bw + 4 * s, CREAM, CW_ALIGN_CENTER);
      }
    }
    /* the buttons */
    static const char *const BL[3] = { "RACE AGAIN", "NEXT MAP", "MENU" };
    static const char *const CL[2] = { "STANDINGS", "QUIT CUP" };
    int nb = res_nbuttons();
    for (int i = 0; i < nb; i++)
      draw_button(res_button_n(i, nb, W, H), U.cup.on ? CL[i] : BL[i], U.sel == i && t > 110, true, s * 0.8f, clamp01(((float)t - 130 - (float)i * 4) / 14.0f));
  } else {
    tx(U.f_hud, dev_hint("(A) to see the results", "Enter to see the results", "Click to see the results", "Tap to see the results"), W * 0.5f, H - 50 * s, 22 * s, alpha(CREAM, 0.5f + 0.4f * cw_sin(U.time * 5)),
       CW_ALIGN_CENTER);
  }
}

static void scr_draw(float W, float H) {
  float s = lscale(W, H);
  switch (U.mode) {
  case M_TITLE: draw_title(W, H, s); break;
  case M_JOIN: draw_join(W, H, s); break;
  case M_VEHICLE: draw_vehicle(W, H, s); break;
  case M_MAP: draw_map(W, H, s); break;
  case M_OPTIONS: draw_options(W, H, s); break;
  case M_CONTROLS: draw_controls(W, H, s); break;
  case M_PAUSE: draw_pause(W, H, s); break;
  case M_RESULTS: draw_results(W, H, s); break;
  case M_CUP: draw_cup(W, H, s); break;
  case M_STANDINGS: draw_standings(W, H, s); break;
  case M_CUPEND: draw_cupend(W, H, s); break;
  default: break;
  }
}
