/* racer's six vehicles, built from code (models.h has the kit). Kart space: +z forward, +y up, +x left, the road at
 * y = -0.62. Each builder fills the material slots of one shared set of geometry and records the points the rig needs
 * (exhaust mouths, plates, seat, steering wheel). Junkyard racers: bright paint over bolted-on salvage.
 *
 * Sizes follow the vehicle table (len and wid half extents, wheel radius); the wheels stand at x = +-0.95 wid and
 * z = +-0.72 len, centred at y = -0.62 + wheel radius. */
#include <math.h>
#include <string.h>

#include "models.h"

#define PAINT_TOP (const float[3]){ 1, 1, 1 }
#define PAINT_BOT (const float[3]){ 0.5f, 0.46f, 0.42f }
#define V(x, y, z) v3((x), (y), (z))
#define PI_2 1.5707963f

static const float VINYL[3] = { 0.16f, 0.14f, 0.13f }, WOOD[3] = { 0.5f, 0.36f, 0.2f }, ORANGE[3] = { 0.95f, 0.45f, 0.08f };
static const float TYRE_BLACK[3] = { 0.07f, 0.07f, 0.075f }, BLUE[3] = { 0.2f, 0.4f, 0.8f }, SKINT[3] = { 0.22f, 0.18f, 0.16f };

static void set_color3(float o[3], float r, float g, float b) { o[0] = r, o[1] = g, o[2] = b; }

/* ── shared parts ───────────────────────────────────────────────────── */

static void flame_at(kit *k, t3_vec3 p, t3_vec3 dir) {
  veh_geo *vg = k->vg;
  if (vg->nflame < 4) vg->flame[vg->nflame++] = (anchor){ p, v3norm(dir) };
}


static void lamp(kit *k, t3_vec3 p, float r, bool front) {
  mb *bz = &k->m[S_METAL];
  mb_cyl(bz, xf_rot(p.x, p.y, p.z, PI_2, 0, 0), r * 1.18f, r * 1.25f, r * 0.7f, 12, C_CHROME);
  mb_geo(&k->m[front ? S_LAMP : S_TAIL], t3_sphere_geometry_new(1, 10, 7),
         xf_scaled(xf_at(p.x, p.y, p.z + (front ? 1 : -1) * r * 0.2f), r, r, r * 0.7f), C_WHITE);
}

/* a stack of exhaust: a pipe through the points, a flared mouth at the end, a flame anchor there */
static void stack(kit *k, const t3_vec3 *pts, int n, float r, bool chrome, t3_vec3 flame_dir) {
  const float *col = chrome ? C_CHROME : C_RUST;
  mb *m = &k->m[S_METAL];
  mb_polyline(m, pts, n, r, 8, col);
  t3_vec3 d = v3norm(v3sub(pts[n - 1], pts[n - 2]));
  mb_tube_cap(m, pts[n - 1], v3add(pts[n - 1], v3mul(d, r * 3.2f)), r * 1.0f, r * 1.6f, 10, C_STEEL);
  mb_tube(m, v3add(pts[n - 1], v3mul(d, r * 3.2f)), v3add(pts[n - 1], v3mul(d, r * 3.5f)), r * 1.4f, 10, C_DARK);
  flame_at(k, v3add(pts[n - 1], v3mul(d, r * 3.5f)), flame_dir);
  /* clamps */
  for (int i = 0; i + 1 < n; i++) {
    t3_vec3 mid = v3mul(v3add(pts[i], pts[i + 1]), 0.5f);
    mb_ball(m, mid, r * 1.25f, C_BRASS);
  }
}

static void rivets(kit *k, t3_vec3 a, t3_vec3 b, int n, t3_vec3 dir, float r) {
  for (int i = 0; i < n; i++) mb_bolt(&k->m[S_METAL], v3add(a, v3mul(v3sub(b, a), n > 1 ? (float)i / (float)(n - 1) : 0.5f)), dir, r, C_STEEL);
}

/* the driver's legs and boots (the torso and arms are animated, in models.c) */
static void legs(kit *k) {
  veh_geo *vg = k->vg;
  float ds = vg->driver_scale;
  mb *m = &k->m[S_DARK];
  for (int s = -1; s <= 1; s += 2) {
    t3_vec3 hip = v3add(vg->seat, V((float)s * 0.095f * ds, 0.03f * ds, -0.02f * ds)), knee = v3add(hip, V(0, 0.1f * ds, 0.3f * ds)),
            ank = v3add(knee, V(0, -0.2f * ds, 0.12f * ds));
    mb_tube(m, hip, knee, 0.05f * ds, 8, vg->suit2);
    mb_ball(m, knee, 0.05f * ds, vg->suit2);
    mb_tube(m, knee, ank, 0.042f * ds, 8, vg->suit2);
    mb_rbox(m, xf_at(ank.x, ank.y - 0.03f * ds, ank.z + 0.05f * ds), 0.1f * ds, 0.07f * ds, 0.2f * ds, 0.025f * ds, 1, C_BLACK);
  }
}

/* a seat: pan and back in vinyl, patched */
static void seat(kit *k, t3_vec3 hip, float w, float back_h) {
  mb *m = &k->m[S_DARK];
  mb_rbox(m, xf_at(hip.x, hip.y - 0.045f, hip.z + 0.04f), w, 0.07f, 0.36f, 0.03f, 1, VINYL);
  mb_rbox(m, xf_rot(hip.x, hip.y + back_h * 0.5f, hip.z - 0.14f, -0.12f, 0, 0), w, back_h, 0.07f, 0.03f, 1, VINYL);
  mb_rbox(m, xf_rot(hip.x + 0.06f, hip.y + back_h * 0.4f, hip.z - 0.185f, -0.12f, 0, 0), w * 0.35f, back_h * 0.35f, 0.012f, 0.004f, 0, C_RUST);
}

/* one headline racing stripe along the top of a lofted body */
static void stripe(kit *k, const sect *s, int ns, float inflate) {
  mb_loft(&k->m[S_PAINT2], s, ns, 4, -0.22f, 0.22f, inflate, NULL, C_WHITE);
}

/* ── 0 Scrapper: the all-rounder. A front engine with hot-rod stacks, a roll hoop, a bolt-on wing ─────────────── */

static void build_scrapper(kit *k) {
  veh_geo *vg = k->vg;
  const vehicle_def *vd = k->vd;
  float wx = vd->wid * 0.95f, wz = vd->len * 0.72f, wy = -0.62f + vd->wheel_r;
  vg->wl = (wheel_look){ 0.32f, 14, 0.05f, 5, 0.62f, { 0.5f, 0.52f, 0.55f }, { 0.95f, 0.78f, 0.1f }, false };
  vg->seat = V(0, 0.2f, -0.36f);
  vg->wheel_hub = V(0, 0.54f, 0.04f);
  vg->wheel_r = 0.12f, vg->wheel_tilt = -0.95f;
  set_color3(vg->suit, 0.26f, 0.34f, 0.5f), set_color3(vg->suit2, 0.88f, 0.7f, 0.15f);
  vg->helmet = 0;
  vg->shield_r = 1.7f, vg->shield_c = V(0, 0.28f, 0);
  static const sect tub[] = { { -1.12f, 0.26f, -0.26f, -0.02f, 3, 0 }, { -1.0f, 0.38f, -0.32f, 0.1f, 3.5f, 0 }, { -0.6f, 0.43f, -0.34f, 0.17f, 4, 0 },
                              { -0.1f, 0.43f, -0.34f, 0.16f, 4, 0 }, { 0.35f, 0.41f, -0.34f, 0.12f, 3.6f, 0 }, { 0.8f, 0.35f, -0.31f, 0.05f, 3.2f, 0 },
                              { 1.12f, 0.24f, -0.26f, -0.06f, 3, 0 } };
  mb_loft(&k->m[S_PAINT], tub, 7, 16, 0, 6.2831853f, 0, PAINT_BOT, PAINT_TOP);
  stripe(k, tub, 7, 0.006f);
  /* chassis rails and the skid plate showing under the tub */
  for (int s = -1; s <= 1; s += 2) {
    mb_tube(&k->m[S_METAL], V((float)s * 0.3f, -0.36f, -1.0f), V((float)s * 0.3f, -0.36f, 1.0f), 0.04f, 7, C_STEEL);
    mb_tube(&k->m[S_METAL], V((float)s * 0.3f, -0.34f, wz), V((float)s * (wx - 0.17f), wy, wz), 0.026f, 6, C_STEEL);
    mb_tube(&k->m[S_METAL], V((float)s * 0.3f, -0.34f, -wz), V((float)s * (wx - 0.17f), wy, -wz), 0.026f, 6, C_STEEL);
  }
  /* the engine, out in the open: block, valve covers, air cleaner, carb, belt pulley */
  mb *mt = &k->m[S_METAL];
  mb_rbox(mt, xf_at(0, 0.22f, 0.78f), 0.5f, 0.3f, 0.5f, 0.05f, 1, C_RUST);
  mb_rbox(mt, xf_at(0.14f, 0.4f, 0.78f), 0.15f, 0.09f, 0.4f, 0.03f, 1, C_STEEL);
  mb_rbox(mt, xf_at(-0.14f, 0.4f, 0.78f), 0.15f, 0.09f, 0.4f, 0.03f, 1, C_STEEL);
  mb_cyl(mt, xf_at(0, 0.5f, 0.78f), 0.13f, 0.14f, 0.09f, 14, C_CHROME);
  mb_cyl(mt, xf_at(0, 0.55f, 0.78f), 0.06f, 0.06f, 0.04f, 10, C_STEEL);
  mb_cyl(mt, xf_rot(0, 0.24f, 1.06f, PI_2, 0, 0), 0.07f, 0.07f, 0.06f, 12, C_BRASS);
  for (int s = -1; s <= 1; s += 2) {
    t3_vec3 pts[3] = { V((float)s * 0.27f, 0.3f, 0.95f), V((float)s * 0.33f, 0.44f, 0.78f), V((float)s * 0.33f, 0.74f, 0.6f) };
    stack(k, pts, 3, 0.036f, true, V((float)s * 0.1f, 0.5f, -1.0f));
  }
  /* grille, headlights, bumper */
  mb_rbox(&k->m[S_DARK], xf_at(0, 0.0f, 1.12f), 0.46f, 0.2f, 0.05f, 0.02f, 1, C_BLACK);
  for (int i = 0; i < 4; i++) mb_tube(mt, V(-0.2f, -0.05f + 0.045f * (float)i, 1.16f), V(0.2f, -0.05f + 0.045f * (float)i, 1.16f), 0.01f, 5, C_CHROME);
  lamp(k, V(0.3f, 0.12f, 1.07f), 0.085f, true);
  lamp(k, V(-0.3f, 0.12f, 1.07f), 0.085f, true);
  t3_vec3 bump[4] = { V(-0.5f, -0.1f, 0.85f), V(-0.4f, -0.1f, 1.2f), V(0.4f, -0.1f, 1.2f), V(0.5f, -0.1f, 0.85f) };
  mb_polyline(mt, bump, 4, 0.032f, 8, C_STEEL);
  /* a mismatched salvage door on each side */
  for (int s = -1; s <= 1; s += 2) {
    mb_rbox(&k->m[S_PAINT2], xf_at((float)s * 0.455f, -0.04f, -0.2f), 0.04f, 0.3f, 0.6f, 0.018f, 1, C_WHITE);
    rivets(k, V((float)s * 0.48f, -0.14f, -0.46f), V((float)s * 0.48f, -0.14f, 0.06f), 4, V((float)s, 0, 0), 0.02f);
    rivets(k, V((float)s * 0.48f, 0.06f, -0.46f), V((float)s * 0.48f, 0.06f, 0.06f), 4, V((float)s, 0, 0), 0.02f);
    mb_rbox(mt, xf_rot((float)s * 0.47f, -0.2f, 0.45f, 0, 0, 0), 0.03f, 0.1f, 0.3f, 0.01f, 0, C_RUST); /* a patch plate */
  }
  /* roll hoop and brace */
  for (int s = -1; s <= 1; s += 2) {
    t3_vec3 pts[4] = { V((float)s * 0.37f, 0.12f, -0.62f), V((float)s * 0.36f, 0.8f, -0.62f), V((float)s * 0.3f, 1.08f, -0.6f), V(0, 1.1f, -0.6f) };
    mb_polyline(mt, pts, 4, 0.03f, 8, C_STEEL);
    mb_tube(mt, V((float)s * 0.34f, 0.9f, -0.62f), V((float)s * 0.28f, 0.2f, -1.0f), 0.022f, 6, C_STEEL);
    mb_ball(mt, V((float)s * 0.37f, 0.12f, -0.62f), 0.04f, C_BRASS);
  }
  /* the seat, wing and tail */
  seat(k, V(0, 0.2f, -0.36f), 0.34f, 0.4f);
  for (int s = -1; s <= 1; s += 2) {
    mb_tube(mt, V((float)s * 0.3f, 0.12f, -1.0f), V((float)s * 0.32f, 0.6f, -1.1f), 0.022f, 6, C_STEEL);
    mb_rbox(&k->m[S_PAINT2], xf_at((float)s * 0.47f, 0.62f, -1.1f), 0.03f, 0.2f, 0.32f, 0.01f, 1, C_WHITE);
    lamp(k, V((float)s * 0.26f, 0.04f, -1.14f), 0.055f, false);
  }
  mb_rbox(&k->m[S_PAINT2], xf_rot(0, 0.62f, -1.1f, -0.14f, 0, 0), 0.96f, 0.045f, 0.3f, 0.018f, 1, C_WHITE);
  mb_rbox(mt, xf_at(0.42f, 0.0f, -0.92f), 0.16f, 0.26f, 0.1f, 0.025f, 1, C_RED); /* a jerry can strapped on */
  mb_cyl(mt, xf_at(0.42f, 0.15f, -0.92f), 0.03f, 0.03f, 0.04f, 8, C_BRASS);
  vg->plate_back = (anchor){ V(0, 0.06f, -1.165f), V(0, 0, -1) }, vg->plate_back_w = 0.3f;
  vg->plate_side = (anchor){ V(0.472f, -0.02f, -0.2f), V(1, 0, 0) }, vg->plate_side_r = 0.11f;
  legs(k);
}

/* ── 1 Rhino: the armoured bruiser. A dozer plough, plate armour, a caged driver, a roaring rear engine ──────── */

static void build_rhino(kit *k) {
  veh_geo *vg = k->vg;
  const vehicle_def *vd = k->vd;
  float wx = vd->wid * 0.95f, wz = vd->len * 0.72f, wy = -0.62f + vd->wheel_r;
  vg->wl = (wheel_look){ 0.4f, 12, 0.075f, 6, 0.58f, { 0.32f, 0.33f, 0.35f }, { 0.95f, 0.5f, 0.1f }, true };
  vg->seat = V(0, 0.34f, -0.18f);
  vg->wheel_hub = V(0, 0.7f, 0.22f);
  vg->wheel_r = 0.14f, vg->wheel_tilt = -0.9f;
  vg->driver_scale = 1.12f;
  set_color3(vg->suit, 0.42f, 0.14f, 0.1f), set_color3(vg->suit2, 0.12f, 0.12f, 0.13f);
  vg->helmet = 1;
  vg->shield_r = 2.0f, vg->shield_c = V(0, 0.3f, 0.1f);
  vg->scarf_len = 0.12f;
  static const sect tub[] = { { -1.2f, 0.4f, -0.26f, 0.1f, 3, 0 }, { -1.05f, 0.52f, -0.32f, 0.28f, 3.8f, 0 }, { -0.5f, 0.57f, -0.34f, 0.3f, 4.2f, 0 },
                              { 0.2f, 0.57f, -0.34f, 0.27f, 4.2f, 0 }, { 0.75f, 0.5f, -0.33f, 0.2f, 3.8f, 0 }, { 1.2f, 0.38f, -0.28f, 0.06f, 3, 0 } };
  mb_loft(&k->m[S_PAINT], tub, 6, 16, 0, 6.2831853f, 0, PAINT_BOT, PAINT_TOP);
  mb *mt = &k->m[S_METAL];
  mb *pm = &k->m[S_PAINT2];
  /* plate armour bolted along the flanks and over the nose */
  for (int s = -1; s <= 1; s += 2) {
    mb_rbox(mt, xf_rot((float)s * 0.6f, 0.02f, -0.05f, 0, 0, (float)s * -0.12f), 0.07f, 0.4f, 1.1f, 0.02f, 1, C_STEEL);
    rivets(k, V((float)s * 0.645f, 0.14f, -0.5f), V((float)s * 0.645f, 0.14f, 0.4f), 5, V((float)s, 0, 0), 0.03f);
    rivets(k, V((float)s * 0.645f, -0.1f, -0.5f), V((float)s * 0.645f, -0.1f, 0.4f), 5, V((float)s, 0, 0), 0.03f);
    mb_rbox(pm, xf_rot((float)s * 0.31f, 0.28f, 0.85f, 0.35f, 0, 0), 0.4f, 0.04f, 0.55f, 0.015f, 1, C_WHITE);
    for (int i = 0; i < 3; i++) mb_cone(mt, xf_rot((float)s * (wx + 0.2f), wy + vd->wheel_r + 0.12f - 0.05f * (float)(i % 2), wz - 0.28f * (float)i + 0.2f, 0, 0, (float)s * -PI_2 * 0.6f), 0.05f, 0.16f, 6, C_RUST);
  }
  /* the plough: hazard stripes on a leaning blade, two push arms, a lip and a row of spikes */
  for (int i = 0; i < 9; i++) {
    float x = -0.74f + 0.185f * (float)i;
    mb_rbox(mt, xf_rot(x, 0.02f, 1.42f, -0.4f, 0, 0), 0.185f, 0.62f, 0.09f, 0.02f, 1, i & 1 ? C_BLACK : C_YELLOW);
  }
  mb_rbox(mt, xf_rot(0, -0.28f, 1.52f, -0.4f, 0, 0), 1.5f, 0.07f, 0.12f, 0.02f, 1, C_STEEL);
  for (int s = -1; s <= 1; s += 2) {
    mb_tube(mt, V((float)s * 0.7f, 0.0f, 1.4f), V((float)s * 0.4f, -0.1f, 0.5f), 0.045f, 8, C_STEEL);
    mb_ball(mt, V((float)s * 0.7f, 0.0f, 1.4f), 0.06f, C_BRASS);
  }
  for (int i = 0; i < 6; i++) mb_cone(mt, xf_rot(-0.6f + 0.24f * (float)i, 0.33f, 1.3f, 0.5f, 0, 0), 0.05f, 0.18f, 6, C_RUST);
  /* lamps on brackets behind the blade, in cages */
  for (int s = -1; s <= 1; s += 2) {
    lamp(k, V((float)s * 0.42f, 0.45f, 0.98f), 0.1f, true);
    mb_tube(mt, V((float)s * 0.42f, 0.2f, 0.9f), V((float)s * 0.42f, 0.4f, 0.96f), 0.02f, 6, C_STEEL);
    for (int i = 0; i < 3; i++) mb_tube(mt, V((float)s * 0.42f - 0.1f + 0.1f * (float)i, 0.34f, 1.05f), V((float)s * 0.42f - 0.1f + 0.1f * (float)i, 0.56f, 1.05f), 0.01f, 5, C_STEEL);
  }
  /* the cage over the driver: four posts, rails, cross-braces */
  t3_vec3 p0[5] = { V(0.5f, 0.1f, 0.3f), V(0.46f, 1.25f, 0.26f), V(0.46f, 1.25f, -0.55f), V(0.5f, 0.1f, -0.6f), V(0.5f, 0.1f, 0.3f) };
  t3_vec3 p1[5] = { V(-0.5f, 0.1f, 0.3f), V(-0.46f, 1.25f, 0.26f), V(-0.46f, 1.25f, -0.55f), V(-0.5f, 0.1f, -0.6f), V(-0.5f, 0.1f, 0.3f) };
  mb_polyline(mt, p0, 5, 0.04f, 8, C_STEEL);
  mb_polyline(mt, p1, 5, 0.04f, 8, C_STEEL);
  mb_tube(mt, V(0.46f, 1.25f, 0.26f), V(-0.46f, 1.25f, 0.26f), 0.036f, 8, C_STEEL);
  mb_tube(mt, V(0.46f, 1.25f, -0.55f), V(-0.46f, 1.25f, -0.55f), 0.036f, 8, C_STEEL);
  mb_tube(mt, V(0.48f, 0.3f, 0.28f), V(-0.46f, 1.2f, 0.26f), 0.022f, 6, C_STEEL);
  mb_tube(mt, V(-0.48f, 0.3f, 0.28f), V(0.46f, 1.2f, 0.26f), 0.022f, 6, C_STEEL);
  mb_tube(mt, V(0.46f, 0.4f, -0.58f), V(0.0f, 1.25f, -0.55f), 0.022f, 6, C_STEEL);
  mb_tube(mt, V(-0.46f, 0.4f, -0.58f), V(0.0f, 1.25f, -0.55f), 0.022f, 6, C_STEEL);
  seat(k, V(0, 0.34f, -0.18f), 0.4f, 0.46f);
  /* the rear engine: a blower sticking up out of the block, twin stacks */
  mb_rbox(mt, xf_at(0, 0.5f, -0.88f), 0.62f, 0.32f, 0.5f, 0.05f, 1, C_RUST);
  mb_rbox(mt, xf_at(0.2f, 0.7f, -0.88f), 0.18f, 0.1f, 0.42f, 0.03f, 1, C_STEEL);
  mb_rbox(mt, xf_at(-0.2f, 0.7f, -0.88f), 0.18f, 0.1f, 0.42f, 0.03f, 1, C_STEEL);
  mb_cyl(mt, xf_at(0, 0.78f, -0.88f), 0.17f, 0.2f, 0.16f, 14, C_CHROME);
  mb_cone(mt, xf_rot(0, 0.96f, -0.88f, 3.14159265f, 0, 0), 0.19f, 0.22f, 14, C_STEEL);
  for (int i = 0; i < 4; i++) mb_tube(mt, V(-0.19f + 0.126f * (float)i, 0.78f, -0.88f), V(-0.19f + 0.126f * (float)i, 0.78f, -1.1f), 0.012f, 5, C_STEEL);
  for (int s = -1; s <= 1; s += 2) {
    t3_vec3 pts[3] = { V((float)s * 0.45f, 0.52f, -0.8f), V((float)s * 0.56f, 0.9f, -1.0f), V((float)s * 0.56f, 1.4f, -1.06f) };
    stack(k, pts, 3, 0.055f, false, V(0, 0.8f, -0.6f));
    lamp(k, V((float)s * 0.36f, 0.2f, -1.26f), 0.07f, false);
  }
  mb_rbox(pm, xf_at(0, 0.12f, -1.23f), 0.9f, 0.3f, 0.07f, 0.03f, 1, C_WHITE); /* tail plate backing */
  vg->plate_back = (anchor){ V(0, 0.12f, -1.27f), V(0, 0, -1) }, vg->plate_back_w = 0.4f;
  vg->plate_side = (anchor){ V(0.64f, 0.05f, 0.2f), V(1, 0, 0) }, vg->plate_side_r = 0.15f;
  legs(k);
}

/* ── 2 Gnat: a tiny tube-frame buggy on huge knobbly wheels, a twin engine out the back, a whip aerial ───────── */

static void build_gnat(kit *k) {
  veh_geo *vg = k->vg;
  const vehicle_def *vd = k->vd;
  float wx = vd->wid * 0.95f, wz = vd->len * 0.72f, wy = -0.62f + vd->wheel_r;
  vg->wl = (wheel_look){ 0.3f, 18, 0.07f, 4, 0.5f, { 0.9f, 0.88f, 0.8f }, { 0.9f, 0.2f, 0.15f }, false };
  vg->seat = V(0, 0.14f, -0.12f);
  vg->wheel_hub = V(0, 0.48f, 0.22f);
  vg->wheel_r = 0.1f, vg->wheel_tilt = -0.85f;
  vg->driver_scale = 0.86f;
  vg->scarf_len = 0.24f;
  set_color3(vg->suit, 0.82f, 0.78f, 0.7f), set_color3(vg->suit2, 0.8f, 0.2f, 0.18f);
  vg->helmet = 2;
  vg->shield_r = 1.45f, vg->shield_c = V(0, 0.2f, 0);
  vg->antenna = (anchor){ V(0.2f, 0.12f, -0.62f), V(0, 1.1f, 0) };
  static const sect tub[] = { { -0.5f, 0.15f, -0.25f, 0.0f, 3, 0 }, { -0.3f, 0.25f, -0.3f, 0.1f, 3.6f, 0 }, { 0.15f, 0.26f, -0.3f, 0.08f, 3.6f, 0 },
                              { 0.55f, 0.2f, -0.27f, 0.0f, 3, 0 }, { 0.92f, 0.12f, -0.22f, -0.07f, 2.6f, 0 } };
  mb_loft(&k->m[S_PAINT], tub, 5, 14, 0, 6.2831853f, 0, PAINT_BOT, PAINT_TOP);
  stripe(k, tub, 5, 0.006f);
  mb *mt = &k->m[S_METAL];
  /* tube chassis, suspension arms out to the hubs, and the cage */
  for (int s = -1; s <= 1; s += 2) {
    float sx = (float)s;
    mb_tube(mt, V(sx * 0.2f, -0.3f, -0.7f), V(sx * 0.2f, -0.3f, 0.85f), 0.03f, 6, C_STEEL);
    for (int e = 0; e < 2; e++) {
      float z = e ? -wz : wz;
      mb_tube(mt, V(sx * 0.2f, -0.25f, z), V(sx * (wx - 0.14f), wy + 0.02f, z), 0.024f, 6, C_STEEL);
      mb_tube(mt, V(sx * 0.2f, -0.34f, z - 0.1f), V(sx * (wx - 0.14f), wy - 0.04f, z), 0.02f, 6, C_STEEL);
      mb_tube(mt, V(sx * (wx - 0.14f), wy + 0.02f, z), V(sx * (wx - 0.14f), wy - 0.04f, z), 0.034f, 6, C_BRASS);
    }
    t3_vec3 cage[5] = { V(sx * 0.25f, 0.05f, 0.28f), V(sx * 0.2f, 0.78f, 0.12f), V(sx * 0.24f, 0.8f, -0.4f), V(sx * 0.27f, 0.0f, -0.55f), V(sx * 0.24f, -0.2f, -0.2f) };
    mb_polyline(mt, cage, 5, 0.026f, 7, C_STEEL);
    mb_tube(mt, V(sx * 0.2f, 0.78f, 0.12f), V(sx * 0.32f, -0.1f, 0.78f), 0.02f, 6, C_STEEL);
  }
  mb_tube(mt, V(0.2f, 0.78f, 0.12f), V(-0.2f, 0.78f, 0.12f), 0.026f, 7, C_STEEL);
  mb_tube(mt, V(0.24f, 0.8f, -0.4f), V(-0.24f, 0.8f, -0.4f), 0.026f, 7, C_STEEL);
  mb_tube(mt, V(0.24f, 0.8f, -0.4f), V(-0.24f, 0.05f, -0.55f), 0.016f, 6, C_STEEL);
  mb_tube(mt, V(-0.24f, 0.8f, -0.4f), V(0.24f, 0.05f, -0.55f), 0.016f, 6, C_STEEL);
  /* a V-twin behind the seat: two finned cylinders, a crankcase, an exposed chain, two megaphones */
  mb_rbox(mt, xf_at(0, 0.0f, -0.52f), 0.3f, 0.2f, 0.26f, 0.05f, 1, C_RUST);
  for (int s = -1; s <= 1; s += 2) {
    xf cx = xf_rot((float)s * 0.1f, 0.2f, -0.52f, 0, 0, (float)s * -0.45f);
    mb_cyl(mt, cx, 0.065f, 0.07f, 0.22f, 10, C_STEEL);
    for (int i = 0; i < 4; i++) {
      xf fx = cx;
      fx.p = xf_apply(&cx, V(0, -0.07f + 0.045f * (float)i, 0));
      mb_cyl(mt, fx, 0.085f, 0.085f, 0.014f, 10, C_STEEL);
    }
    mb_cyl(mt, xf_rot((float)s * 0.16f, 0.32f, -0.52f, 0, 0, (float)s * -0.45f), 0.05f, 0.05f, 0.05f, 8, C_BRASS);
    t3_vec3 pts[3] = { V((float)s * 0.17f, 0.26f, -0.58f), V((float)s * 0.26f, 0.32f, -0.8f), V((float)s * 0.24f, 0.4f, -0.98f) };
    stack(k, pts, 3, 0.03f, true, V((float)s * 0.1f, 0.2f, -1.0f));
  }
  /* nose: a big round lamp and a cow-catcher bar */
  lamp(k, V(0, 0.0f, 0.93f), 0.1f, true);
  t3_vec3 bar[4] = { V(-0.22f, -0.2f, 0.7f), V(-0.18f, -0.22f, 1.0f), V(0.18f, -0.22f, 1.0f), V(0.22f, -0.2f, 0.7f) };
  mb_polyline(mt, bar, 4, 0.025f, 7, C_STEEL);
  for (int s = -1; s <= 1; s += 2) {
    lamp(k, V((float)s * 0.12f, 0.02f, -0.4f), 0.04f, false);
  }
  seat(k, V(0, 0.14f, -0.12f), 0.28f, 0.34f);
  mb_rbox(&k->m[S_PAINT2], xf_at(0, 0.32f, 0.5f), 0.24f, 0.05f, 0.24f, 0.02f, 1, C_WHITE); /* a little cowl */
  vg->plate_back = (anchor){ V(0, 0.0f, -0.36f), V(0, 0, -1) }, vg->plate_back_w = 0.0f;
  vg->plate_side = (anchor){ V(0.265f, 0.02f, 0.0f), V(1, 0, 0) }, vg->plate_side_r = 0.1f;
  legs(k);
}

/* ── 3 Sidewinder: a low wedge, side pipes that flame, twin tail fins, a reclined driver ─────────────────────── */

static void build_sidewinder(kit *k) {
  veh_geo *vg = k->vg;
  const vehicle_def *vd = k->vd;
  float wx = vd->wid * 0.95f, wz = vd->len * 0.72f, wy = -0.62f + vd->wheel_r;
  vg->wl = (wheel_look){ 0.32f, 12, 0.032f, 7, 0.66f, { 0.28f, 0.29f, 0.32f }, { 0.9f, 0.15f, 0.12f }, false };
  vg->seat = V(0, 0.06f, -0.34f);
  vg->torso_lean = 0.42f;
  vg->wheel_hub = V(0, 0.36f, 0.04f);
  vg->wheel_r = 0.11f, vg->wheel_tilt = -1.15f;
  set_color3(vg->suit, 0.92f, 0.92f, 0.9f), set_color3(vg->suit2, 0.8f, 0.12f, 0.1f);
  vg->helmet = 3;
  vg->shield_r = 1.8f, vg->shield_c = V(0, 0.1f, 0);
  static const sect tub[] = { { -1.15f, 0.34f, -0.28f, 0.02f, 3.5f, 0 }, { -0.9f, 0.45f, -0.32f, 0.14f, 4, 0 }, { -0.3f, 0.5f, -0.34f, 0.12f, 4.4f, 0 },
                              { 0.4f, 0.44f, -0.33f, 0.02f, 4, 0 }, { 0.9f, 0.3f, -0.3f, -0.08f, 3.4f, 0 }, { 1.28f, 0.12f, -0.26f, -0.17f, 3, 0 } };
  mb_loft(&k->m[S_PAINT], tub, 6, 16, 0, 6.2831853f, 0, PAINT_BOT, PAINT_TOP);
  stripe(k, tub, 6, 0.006f);
  mb *mt = &k->m[S_METAL];
  mb *pm = &k->m[S_PAINT2];
  /* a front splitter and a diffuser */
  mb_rbox(mt, xf_at(0, -0.34f, 1.05f), 0.95f, 0.03f, 0.4f, 0.012f, 1, C_DARK);
  mb_rbox(mt, xf_rot(0, -0.3f, -1.1f, 0.25f, 0, 0), 0.7f, 0.03f, 0.3f, 0.012f, 1, C_DARK);
  for (int i = -2; i <= 2; i++) mb_rbox(mt, xf_at((float)i * 0.15f, -0.29f, -1.14f), 0.015f, 0.1f, 0.26f, 0, 0, C_DARK);
  /* the windscreen: a low wedge of glass in a frame, and a seat sunk in the tub */
  {
    mb *g = &k->m[S_GLASS];
    mb_quad(g, V(-0.3f, 0.1f, 0.18f), V(0.3f, 0.1f, 0.18f), V(0.24f, 0.36f, -0.04f), V(-0.24f, 0.36f, -0.04f), C_WHITE);
    mb_quad(g, V(-0.24f, 0.36f, -0.04f), V(0.24f, 0.36f, -0.04f), V(0.3f, 0.1f, 0.18f), V(-0.3f, 0.1f, 0.18f), C_WHITE);
    mb_tube(mt, V(-0.3f, 0.1f, 0.18f), V(-0.24f, 0.36f, -0.04f), 0.014f, 5, C_STEEL);
    mb_tube(mt, V(0.3f, 0.1f, 0.18f), V(0.24f, 0.36f, -0.04f), 0.014f, 5, C_STEEL);
    mb_tube(mt, V(-0.24f, 0.36f, -0.04f), V(0.24f, 0.36f, -0.04f), 0.014f, 5, C_STEEL);
  }
  seat(k, V(0, 0.06f, -0.34f), 0.34f, 0.36f);
  /* fins: two swept blades at the tail with lamps on the trailing edge */
  for (int s = -1; s <= 1; s += 2) {
    xf fx = xf_rot((float)s * 0.3f, 0.36f, -0.98f, -0.32f, 0, (float)s * -0.1f);
    mb_rbox(pm, fx, 0.04f, 0.56f, 0.4f, 0.02f, 1, C_WHITE);
    lamp(k, V((float)s * 0.3f, 0.18f, -1.19f), 0.05f, false);
    /* side pipes along the sills, flared at the back */
    t3_vec3 pts[3] = { V((float)s * 0.56f, -0.2f, 0.6f), V((float)s * 0.57f, -0.2f, -0.2f), V((float)s * 0.57f, -0.2f, -0.82f) };
    stack(k, pts, 3, 0.05f, true, V((float)s * 0.05f, 0.04f, -1.0f));
    mb_rbox(mt, xf_at((float)s * 0.5f, -0.24f, 0.2f), 0.07f, 0.04f, 0.1f, 0.01f, 0, C_STEEL);
    mb_rbox(mt, xf_at((float)s * 0.5f, -0.24f, -0.4f), 0.07f, 0.04f, 0.1f, 0.01f, 0, C_STEEL);
    /* slit headlights across the nose */
    mb_rbox(&k->m[S_LAMP], xf_rot((float)s * 0.24f, -0.07f, 0.98f, 0, (float)s * 0.45f, 0), 0.2f, 0.04f, 0.03f, 0.012f, 1, C_WHITE);
  }
  /* the intake scoop behind the driver, a hot engine hump */
  mb_rbox(mt, xf_at(0, 0.24f, -0.78f), 0.4f, 0.14f, 0.34f, 0.05f, 1, C_RUST);
  mb_cone(mt, xf_rot(0, 0.36f, -0.7f, 1.2f, 0, 0), 0.14f, 0.3f, 12, C_STEEL);
  vg->plate_back = (anchor){ V(0, 0.04f, -1.19f), V(0, 0, -1) }, vg->plate_back_w = 0.26f;
  vg->plate_side = (anchor){ V(0.505f, 0.0f, 0.15f), V(1, 0, 0) }, vg->plate_side_r = 0.12f;
  legs(k);
}

/* ── 4 Longhaul: a hauler. Open cab and a bonnet with tall stacks, a flat bed piled with junk ───────────────── */

static void build_longhaul(kit *k) {
  veh_geo *vg = k->vg;
  const vehicle_def *vd = k->vd;
  float wx = vd->wid * 0.95f, wz = vd->len * 0.72f, wy = -0.62f + vd->wheel_r;
  vg->wl = (wheel_look){ 0.38f, 12, 0.065f, 8, 0.56f, { 0.55f, 0.56f, 0.58f }, { 0.95f, 0.5f, 0.1f }, true };
  vg->seat = V(0, 0.28f, 0.34f);
  vg->wheel_hub = V(0, 0.64f, 0.7f);
  vg->wheel_r = 0.15f, vg->wheel_tilt = -0.75f;
  set_color3(vg->suit, 0.3f, 0.45f, 0.28f), set_color3(vg->suit2, 0.55f, 0.4f, 0.22f);
  vg->helmet = 2;
  vg->scarf_len = 0.2f;
  vg->shield_r = 2.2f, vg->shield_c = V(0, 0.3f, -0.1f);
  mb *mt = &k->m[S_METAL];
  mb *pm = &k->m[S_PAINT2];
  /* ladder frame the whole length, cross members, the fuel tank on one side */
  for (int s = -1; s <= 1; s += 2) {
    float sx = (float)s;
    mb_rbox(mt, xf_at(sx * 0.34f, -0.3f, 0.0f), 0.08f, 0.1f, 2.75f, 0.02f, 1, C_STEEL);
    mb_tube(mt, V(sx * 0.34f, -0.3f, wz), V(sx * (wx - 0.19f), wy, wz), 0.03f, 6, C_STEEL);
    mb_tube(mt, V(sx * 0.34f, -0.3f, -wz), V(sx * (wx - 0.19f), wy, -wz), 0.03f, 6, C_STEEL);
  }
  for (int i = 0; i < 5; i++) mb_rbox(mt, xf_at(0, -0.3f, -1.1f + 0.6f * (float)i), 0.7f, 0.07f, 0.08f, 0.015f, 1, C_STEEL);
  mb_cyl(mt, xf_rot(-0.5f, -0.32f, -0.2f, PI_2, 0, 0) /* axis along z via rotation about x */, 0.13f, 0.13f, 0.55f, 14, C_RUST);
  /* the bonnet: a long loft, grille, big bumper */
  static const sect hood[] = { { 0.4f, 0.5f, -0.28f, 0.3f, 4.2f, 0 }, { 0.8f, 0.49f, -0.3f, 0.34f, 4.2f, 0 }, { 1.2f, 0.42f, -0.3f, 0.28f, 3.6f, 0 },
                               { 1.36f, 0.34f, -0.28f, 0.14f, 3, 0 } };
  mb_loft(&k->m[S_PAINT], hood, 4, 16, 0, 6.2831853f, 0, PAINT_BOT, PAINT_TOP);
  stripe(k, hood, 4, 0.006f);
  for (int i = 0; i < 5; i++) mb_tube(mt, V(-0.22f, -0.08f + 0.07f * (float)i, 1.38f), V(0.22f, -0.08f + 0.07f * (float)i, 1.38f), 0.012f, 5, C_CHROME);
  mb_rbox(&k->m[S_DARK], xf_at(0, 0.06f, 1.35f), 0.54f, 0.4f, 0.04f, 0.02f, 1, C_BLACK);
  t3_vec3 bump[4] = { V(-0.62f, -0.14f, 1.05f), V(-0.55f, -0.14f, 1.46f), V(0.55f, -0.14f, 1.46f), V(0.62f, -0.14f, 1.05f) };
  mb_polyline(mt, bump, 4, 0.05f, 8, C_STEEL);
  for (int s = -1; s <= 1; s += 2) {
    float sx = (float)s;
    lamp(k, V(sx * 0.4f, 0.28f, 1.3f), 0.1f, true);
    /* tall twin stacks up the cab's flanks */
    t3_vec3 pts[3] = { V(sx * 0.54f, 0.2f, 0.5f), V(sx * 0.58f, 0.7f, 0.4f), V(sx * 0.58f, 1.4f, 0.38f) };
    stack(k, pts, 3, 0.05f, true, V(0, 0.9f, -0.5f));
    /* door panel (paint2, a different truck's) with a handle */
    mb_rbox(pm, xf_at(sx * 0.5f, 0.0f, 0.34f), 0.05f, 0.42f, 0.7f, 0.02f, 1, C_WHITE);
    mb_rbox(mt, xf_at(sx * 0.54f, 0.08f, 0.55f), 0.03f, 0.03f, 0.12f, 0.01f, 1, C_CHROME);
    /* windscreen post */
    mb_tube(mt, V(sx * 0.5f, 0.2f, 0.74f), V(sx * 0.46f, 0.98f, 0.62f), 0.03f, 7, C_STEEL);
  }
  /* cab: back wall, floor, seat, windscreen glass and a sun visor */
  mb_rbox(&k->m[S_PAINT], xf_at(0, 0.02f, -0.2f), 1.0f, 0.62f, 0.1f, 0.04f, 1, PAINT_TOP);
  mb_rbox(&k->m[S_DARK], xf_at(0, -0.15f, 0.34f), 0.9f, 0.06f, 0.9f, 0.02f, 1, VINYL);
  seat(k, V(0, 0.28f, 0.34f), 0.4f, 0.46f);
  mb_quad(&k->m[S_GLASS], V(-0.46f, 0.98f, 0.62f), V(-0.5f, 0.2f, 0.74f), V(0.5f, 0.2f, 0.74f), V(0.46f, 0.98f, 0.62f), C_WHITE);
  mb_quad(&k->m[S_GLASS], V(0.46f, 0.98f, 0.62f), V(0.5f, 0.2f, 0.74f), V(-0.5f, 0.2f, 0.74f), V(-0.46f, 0.98f, 0.62f), C_WHITE);
  mb_tube(mt, V(-0.46f, 0.98f, 0.62f), V(0.46f, 0.98f, 0.62f), 0.03f, 7, C_STEEL);
  mb_rbox(pm, xf_rot(0, 1.02f, 0.66f, 0.3f, 0, 0), 1.04f, 0.04f, 0.3f, 0.015f, 1, C_WHITE);
  t3_vec3 rl[3] = { V(0.5f, 0.3f, -0.25f), V(0.46f, 1.05f, -0.2f), V(-0.46f, 1.05f, -0.2f) };
  mb_polyline(mt, rl, 3, 0.03f, 7, C_STEEL);
  mb_tube(mt, V(-0.46f, 1.05f, -0.2f), V(-0.5f, 0.3f, -0.25f), 0.03f, 7, C_STEEL);
  /* the flat bed: planks, stakes, a tailgate, and a load of junk */
  mb *pl = &k->m[S_DARK];
  mb_rbox(pl, xf_at(0, -0.14f, -0.84f), 1.04f, 0.07f, 1.3f, 0.02f, 1, WOOD);
  for (int i = 0; i < 5; i++) mb_rbox(pl, xf_at(0, -0.1f, -0.3f - 0.25f * (float)i), 1.0f, 0.006f, 0.012f, 0, 0, C_DARK);
  for (int s = -1; s <= 1; s += 2)
    for (int e = 0; e < 3; e++) {
      float z = -0.28f - 0.62f * (float)e;
      mb_tube(mt, V((float)s * 0.5f, -0.1f, z), V((float)s * 0.5f, 0.32f, z), 0.022f, 6, C_STEEL);
    }
  mb_tube(mt, V(0.5f, 0.32f, -0.28f), V(0.5f, 0.32f, -1.52f), 0.016f, 5, C_STEEL);
  mb_tube(mt, V(-0.5f, 0.32f, -0.28f), V(-0.5f, 0.32f, -1.52f), 0.016f, 5, C_STEEL);
  mb_rbox(pl, xf_at(0, 0.1f, -1.52f), 1.04f, 0.4f, 0.05f, 0.02f, 1, WOOD);
  mb_rbox(pm, xf_at(0, 0.12f, -1.55f), 0.7f, 0.22f, 0.02f, 0.01f, 1, C_WHITE);
  /* load: a stack of tyres, two barrels, a crate with a strap, an armchair, a bundle of pipe, a traffic cone */
  for (int i = 0; i < 3; i++) mb_torus(pl, xf_rot(-0.26f, -0.02f + 0.1f * (float)i, -0.5f, PI_2, 0, 0), 0.17f, 0.06f, 6, 14, TYRE_BLACK);
  mb_cyl(pl, xf_at(0.26f, 0.12f, -0.52f), 0.14f, 0.14f, 0.42f, 14, C_RED);
  for (int i = 0; i < 2; i++) mb_cyl(mt, xf_at(0.26f, -0.02f + 0.28f * (float)i, -0.52f), 0.148f, 0.148f, 0.025f, 14, C_STEEL);
  mb_cyl(pl, xf_at(0.24f, 0.1f, -0.9f), 0.13f, 0.13f, 0.38f, 14, BLUE);
  mb_rbox(pl, xf_rot(-0.22f, 0.14f, -1.02f, 0, 0.3f, 0), 0.38f, 0.38f, 0.38f, 0.02f, 1, WOOD);
  mb_rbox(mt, xf_rot(-0.22f, 0.14f, -1.02f, 0, 0.3f, 0), 0.4f, 0.04f, 0.4f, 0.01f, 0, C_STEEL);
  mb_rbox(pl, xf_rot(0.12f, 0.1f, -1.25f, 0, -0.25f, 0.05f), 0.42f, 0.2f, 0.4f, 0.07f, 2, (const float[3]){ 0.6f, 0.25f, 0.2f });
  mb_rbox(pl, xf_rot(0.1f, 0.34f, -1.34f, -0.2f, -0.25f, 0.05f), 0.4f, 0.4f, 0.1f, 0.05f, 2, (const float[3]){ 0.6f, 0.25f, 0.2f });
  for (int i = 0; i < 3; i++) mb_cyl(mt, xf_rot(-0.2f + 0.1f * (float)i, 0.3f, -0.12f - 0.0f, PI_2, 0, 0), 0.045f, 0.045f, 0.5f, 8, C_STEEL);
  mb_cone(pl, xf_at(-0.34f, 0.2f, -1.4f), 0.07f, 0.28f, 10, ORANGE);
  mb_rbox(pl, xf_at(-0.34f, 0.04f, -1.4f), 0.2f, 0.03f, 0.2f, 0.01f, 1, ORANGE);
  for (int i = 0; i < 2; i++) mb_tube(pl, V(0.5f, 0.1f, -0.5f - 0.5f * (float)i), V(-0.5f, 0.1f, -0.5f - 0.5f * (float)i), 0.01f, 4, C_YELLOW); /* strap lines */
  lamp(k, V(0.4f, -0.02f, -1.58f), 0.055f, false);
  lamp(k, V(-0.4f, -0.02f, -1.58f), 0.055f, false);
  vg->plate_back = (anchor){ V(0, 0.0f, -1.6f), V(0, 0, -1) }, vg->plate_back_w = 0.36f;
  vg->plate_side = (anchor){ V(0.53f, 0.14f, 0.36f), V(1, 0, 0) }, vg->plate_side_r = 0.13f;
  legs(k);
}

/* ── 5 Pogo: a compact pod hung on four coil springs, a boost tank, a propeller beanie ───────────────────────── */

static void build_pogo(kit *k) {
  veh_geo *vg = k->vg;
  const vehicle_def *vd = k->vd;
  float wx = vd->wid * 0.95f, wz = vd->len * 0.72f, wy = -0.62f + vd->wheel_r;
  vg->wl = (wheel_look){ 0.26f, 12, 0.04f, 8, 0.72f, { 0.9f, 0.85f, 0.3f }, { 0.9f, 0.15f, 0.55f }, false };
  vg->seat = V(0, 0.36f, -0.2f);
  vg->wheel_hub = V(0, 0.72f, 0.1f);
  vg->wheel_r = 0.12f, vg->wheel_tilt = -1.0f;
  set_color3(vg->suit, 0.9f, 0.35f, 0.6f), set_color3(vg->suit2, 0.3f, 0.75f, 0.85f);
  vg->helmet = 4;
  vg->scarf_len = 0.18f;
  vg->shield_r = 1.7f, vg->shield_c = V(0, 0.3f, 0);
  vg->struts = true;
  vg->strut_top[0] = V(0.34f, 0.12f, wz);
  vg->strut_top[1] = V(0.34f, 0.12f, -wz);
  vg->antenna = (anchor){ V(-0.22f, 0.3f, -0.62f), V(0, 1.0f, 0) };
  static const sect tub[] = { { -0.75f, 0.2f, -0.05f, 0.26f, 3, 0 }, { -0.5f, 0.34f, -0.12f, 0.34f, 3.6f, 0 }, { 0.1f, 0.36f, -0.14f, 0.36f, 3.6f, 0 },
                              { 0.55f, 0.28f, -0.12f, 0.26f, 3.2f, 0 }, { 0.88f, 0.14f, -0.08f, 0.12f, 2.8f, 0 } };
  mb_loft(&k->m[S_PAINT], tub, 5, 16, 0, 6.2831853f, 0, PAINT_BOT, PAINT_TOP);
  stripe(k, tub, 5, 0.006f);
  mb *mt = &k->m[S_METAL];
  mb *pm = &k->m[S_PAINT2];
  /* the axle beams the springs stand on (the wheels hang below the pod) */
  mb_rbox(mt, xf_at(0, -0.14f, 0.3f), 0.5f, 0.07f, 0.8f, 0.02f, 1, C_STEEL);
  for (int s = -1; s <= 1; s += 2) {
    float sx = (float)s;
    mb_tube(mt, V(sx * 0.3f, -0.12f, wz), V(sx * (wx - 0.13f), wy, wz), 0.026f, 6, C_STEEL);
    mb_tube(mt, V(sx * 0.3f, -0.12f, -wz), V(sx * (wx - 0.13f), wy, -wz), 0.026f, 6, C_STEEL);
    mb_ball(mt, V(sx * 0.34f, 0.12f, wz), 0.06f, C_BRASS);
    mb_ball(mt, V(sx * 0.34f, 0.12f, -wz), 0.06f, C_BRASS);
    lamp(k, V(sx * 0.22f, 0.26f, 0.8f), 0.1f, true);
  }
  /* a roll bar and a handle for good measure; seat */
  seat(k, V(0, 0.36f, -0.2f), 0.32f, 0.4f);
  t3_vec3 rb[5] = { V(0.34f, 0.3f, -0.45f), V(0.3f, 1.05f, -0.45f), V(0, 1.12f, -0.45f), V(-0.3f, 1.05f, -0.45f), V(-0.34f, 0.3f, -0.45f) };
  mb_polyline(mt, rb, 5, 0.026f, 7, C_STEEL);
  /* the boost tank across the back with two nozzles that flame */
  mb_cyl(mt, xf_rot(0, 0.22f, -0.7f, 0, 0, PI_2), 0.12f, 0.12f, 0.7f, 14, C_STEEL);
  for (int i = 0; i < 2; i++) mb_cyl(mt, xf_rot(-0.24f + 0.48f * (float)i, 0.22f, -0.7f, 0, 0, PI_2), 0.125f, 0.125f, 0.03f, 14, C_BRASS);
  for (int s = -1; s <= 1; s += 2) {
    float sx = (float)s;
    mb_tube_cap(mt, V(sx * 0.2f, 0.22f, -0.8f), V(sx * 0.2f, 0.2f, -0.98f), 0.05f, 0.09f, 12, C_STEEL);
    mb_tube(mt, V(sx * 0.2f, 0.2f, -0.98f), V(sx * 0.2f, 0.2f, -1.0f), 0.095f, 12, C_DARK);
    flame_at(k, V(sx * 0.2f, 0.2f, -1.0f), V(sx * 0.04f, 0.04f, -1.0f));
    lamp(k, V(sx * 0.1f, 0.0f, -0.82f), 0.04f, false);
    mb_rbox(pm, xf_at(sx * 0.34f, 0.0f, -0.1f), 0.04f, 0.3f, 0.5f, 0.018f, 1, C_WHITE);
  }
  mb_rbox(pm, xf_at(0, 0.4f, 0.62f), 0.36f, 0.04f, 0.26f, 0.015f, 1, C_WHITE);
  t3_vec3 bump[4] = { V(-0.4f, -0.02f, 0.75f), V(-0.3f, -0.02f, 1.0f), V(0.3f, -0.02f, 1.0f), V(0.4f, -0.02f, 0.75f) };
  mb_polyline(mt, bump, 4, 0.03f, 7, C_BRASS);
  vg->plate_back = (anchor){ V(0, 0.1f, -0.9f), V(0, 0, -1) }, vg->plate_back_w = 0.0f;
  vg->plate_side = (anchor){ V(0.36f, 0.1f, 0.2f), V(1, 0, 0) }, vg->plate_side_r = 0.11f;
  legs(k);
  (void)SKINT;
}

void veh_build(int veh, kit *k) {
  switch (veh % NVEH) {
  case 0: build_scrapper(k); break;
  case 1: build_rhino(k); break;
  case 2: build_gnat(k); break;
  case 3: build_sidewinder(k); break;
  case 4: build_longhaul(k); break;
  default: build_pogo(k); break;
  }
}
