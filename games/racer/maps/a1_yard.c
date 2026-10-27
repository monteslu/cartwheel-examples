/* battle arena: Crusher Yard. A scrapyard bowl on the salt flats at dawn: a square of salt crust inside rusted walls,
 * the crusher's mound in the middle (a flat top three metres up, every side a slope to drive, so there is no wall there
 * to be pinned on) with the crusher's magnet head hanging over it, open L-shaped walls of shipping containers in the
 * four quarters, double stacks at the middle of each side, oil drums piled in the corners and a kicker ramp along each
 * wall. Outside: container stacks, a gantry crane,
 * flood lights, wrecks, and the mesas of the salt flats on the horizon. */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
#define PAINT(x, z, y, w, d, yaw, col) { AP_BOX, AL_PAINT, x, z, y, w, 0.03f, d, yaw, true, col }
/* one side of the painted octagon (apothem 30 m) at angle a degrees round the middle */
#define RINGSIDE(a) PAINT(30.0f * SIN_##a, 30.0f * COS_##a, 0, 24.85f, 0.8f, a, 0xe59a1c)
/* a start pad: a cream border and a dark square on it */
#define PAD(x, z, yaw) PAINT(x, z, 0, 5.4f, 5.4f, yaw, 0xe8e2d6), PAINT(x, z, 0.012f, 4.6f, 4.6f, yaw, 0x3a3236)
#define SIN_0 0.0f
#define COS_0 1.0f
#define SIN_45 0.70711f
#define COS_45 0.70711f
#define SIN_90 1.0f
#define COS_90 0.0f
#define SIN_135 0.70711f
#define COS_135 -0.70711f
#define SIN_180 0.0f
#define COS_180 -1.0f
#define SIN_225 -0.70711f
#define COS_225 -0.70711f
#define SIN_270 -1.0f
#define COS_270 0.0f
#define SIN_315 -0.70711f
#define COS_315 0.70711f

#define TEAL 0x23a0a4
#define ORNG 0xea5d1c
#define RED 0xc9302c
#define BLUE 0x2c6fd0
#define MUST 0xe5a823
#define DARK 0x3a3330

static const arena_prim PRIMS[] = {
  /* the crusher's mound: a flat top three metres up, every side a slope to drive (no walls to be pinned on) */
  { AP_MOUND, AL_DECK, 0, 0, 0, 38, 3, 38, 0, false, 0 },
  /* hazard paint round the top's edge */
  DECO(AL_TRIM, 0, 9.6f, 3.0f, 20, 0.05f, 0.8f, 0, 0),
  DECO(AL_TRIM, 0, -9.6f, 3.0f, 20, 0.05f, 0.8f, 0, 0),
  DECO(AL_TRIM, 9.6f, 0, 3.0f, 0.8f, 0.05f, 18.4f, 0, 0),
  DECO(AL_TRIM, -9.6f, 0, 3.0f, 0.8f, 0.05f, 18.4f, 0, 0),
  /* the crusher: a magnet head hung from the yard crane on a cable, the ram under it with a lit band, all high over the top
   * (nothing up there to drive into) */
  { AP_POST, AL_FEATURE, 0, 0, 11.8f, 0.5f, 30.0f, 0.5f, 0, true, 0 },
  { AP_POST, AL_FEATURE, 0, 0, 7.6f, 3.2f, 3.4f, 3.2f, 0, true, 0 },
  GLOWPOST(0, 0, 7.4f, 3.45f, 0.35f, 0xff9a3a),
  DECO(AL_FEATURE, 0, 0, 11.0f, 9, 1.6f, 9, 0, 0),
  DECO(AL_TRIM, 0, 0, 10.8f, 9.4f, 0.2f, 9.4f, 0, 0),
  /* the quarters: L-shaped walls of containers */
  BOX(AL_COVER, 28, 40, 0, 10, 2.6f, 2.5f, 0, TEAL),
  BOX(AL_COVER, 40, 28, 0, 2.5f, 2.6f, 10, 0, ORNG),
  BOX(AL_COVER, -28, 40, 0, 10, 2.6f, 2.5f, 0, RED),
  BOX(AL_COVER, -40, 28, 0, 2.5f, 2.6f, 10, 0, BLUE),
  BOX(AL_COVER, 28, -40, 0, 10, 2.6f, 2.5f, 0, BLUE),
  BOX(AL_COVER, 40, -28, 0, 2.5f, 2.6f, 10, 0, MUST),
  BOX(AL_COVER, -28, -40, 0, 10, 2.6f, 2.5f, 0, ORNG),
  BOX(AL_COVER, -40, -28, 0, 2.5f, 2.6f, 10, 0, TEAL),
  /* the middle of each side: two high */
  BOX(AL_COVER, 0, 47, 0, 12, 2.6f, 2.5f, 0, RED),
  BOX(AL_COVER, 0, 47, 2.6f, 12, 2.6f, 2.5f, 0, MUST),
  BOX(AL_COVER, 0, -47, 0, 12, 2.6f, 2.5f, 0, TEAL),
  BOX(AL_COVER, 0, -47, 2.6f, 12, 2.6f, 2.5f, 0, RED),
  BOX(AL_COVER, 47, 0, 0, 2.5f, 2.6f, 12, 0, BLUE),
  BOX(AL_COVER, 47, 0, 2.6f, 2.5f, 2.6f, 12, 0, ORNG),
  BOX(AL_COVER, -47, 0, 0, 2.5f, 2.6f, 12, 0, MUST),
  BOX(AL_COVER, -47, 0, 2.6f, 2.5f, 2.6f, 12, 0, BLUE),
  /* oil drums piled into the corners, out of the lanes along the walls */
  POST(AL_COVER, 61.5f, 61.5f, 0, 1.5f, 1.2f, DARK), POST(AL_COVER, 59.6f, 61.8f, 0, 1.5f, 1.2f, RED),
  POST(AL_COVER, -61.5f, -61.5f, 0, 1.5f, 1.2f, DARK), POST(AL_COVER, -59.6f, -61.8f, 0, 1.5f, 1.2f, BLUE),
  POST(AL_COVER, 61.5f, -61.5f, 0, 1.5f, 1.2f, DARK), POST(AL_COVER, 61.8f, -59.6f, 0, 1.5f, 1.2f, MUST),
  POST(AL_COVER, -61.5f, 61.5f, 0, 1.5f, 1.2f, DARK), POST(AL_COVER, -61.8f, 59.6f, 0, 1.5f, 1.2f, TEAL),
  /* kickers along the walls, all running clockwise */
  RAMP(AL_RAMP, -26, 52, 0, 7, 1.3f, 6, 90),
  RAMP(AL_RAMP, 52, 26, 0, 7, 1.3f, 6, 180),
  RAMP(AL_RAMP, 26, -52, 0, 7, 1.3f, 6, -90),
  RAMP(AL_RAMP, -52, -26, 0, 7, 1.3f, 6, 0),
  /* paint on the floor: an octagon round the deck, a start pad under each spawn, hazard stripes along the foot of the walls */
  RINGSIDE(0), RINGSIDE(45), RINGSIDE(90), RINGSIDE(135), RINGSIDE(180), RINGSIDE(225), RINGSIDE(270), RINGSIDE(315),
  PAD(19.1f, 46.2f, 202.5f), PAD(-19.1f, -46.2f, 22.5f), PAD(46.2f, -19.1f, -67.5f), PAD(-46.2f, 19.1f, 112.5f),
  PAD(46.2f, 19.1f, 247.5f), PAD(-46.2f, -19.1f, 67.5f), PAD(-19.1f, 46.2f, 157.5f), PAD(19.1f, -46.2f, -22.5f),
  DECO(AL_TRIM, 0, 63.4f, 0, 128, 0.04f, 1.2f, 0, 0),
  DECO(AL_TRIM, 0, -63.4f, 0, 128, 0.04f, 1.2f, 0, 0),
  DECO(AL_TRIM, 63.4f, 0, 0, 1.2f, 0.04f, 125.6f, 0, 0),
  DECO(AL_TRIM, -63.4f, 0, 0, 1.2f, 0.04f, 125.6f, 0, 0),
};

static const float SPAWNS[][3] = {
  { 19.1f, 46.2f, 202.5f }, { -19.1f, -46.2f, 22.5f }, { 46.2f, -19.1f, -67.5f }, { -46.2f, 19.1f, 112.5f },
  { 46.2f, 19.1f, 247.5f }, { -46.2f, -19.1f, 67.5f }, { -19.1f, 46.2f, 157.5f }, { 19.1f, -46.2f, -22.5f },
};

static const float BOXES[][3] = {
  { 6, 6, 3 }, { -6, 6, 3 }, { 6, -6, 3 }, { -6, -6, 3 },
  { 22, 22, 0 }, { -22, 22, 0 }, { 22, -22, 0 }, { -22, -22, 0 },
  { -3, 38, 0 }, { 3, 38, 0 }, { -3, -38, 0 }, { 3, -38, 0 }, { 38, -3, 0 }, { 38, 3, 0 }, { -38, 3, 0 }, { -38, -3, 0 },
  { 44, 44, 0 }, { -44, 44, 0 }, { 44, -44, 0 }, { -44, -44, 0 },
};

/* capture the flag: red's base in the nook behind the north double stack, blue's behind the south one; each team starts
 * either side of its stack, facing the middle */
static const float TEAM_SPAWNS[8][3] = {
  { -14, 53, 180 }, { 14, 53, 180 }, { -24, 46, 160 }, { 24, 46, 200 },
  { 14, -53, 0 }, { -14, -53, 0 }, { 24, -46, 20 }, { -24, -46, -20 },
};
/* king of the mountain: the crusher's mound first, then the four quarters inside the container walls */
static const float HILLS[][2] = { { 0, 0 }, { 40, 40 }, { -40, -40 }, { 40, -40 }, { -40, 40 } };

static const arena_def ARENA = {
  .half_x = 64, .half_z = 64, .wall_h = 3.4f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = SPAWNS, .nspawns = sizeof SPAWNS / sizeof SPAWNS[0],
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { 0, 56 }, { 0, -56 } },
  .team_spawns = TEAM_SPAWNS,
  .hills = HILLS, .nhills = sizeof HILLS / sizeof HILLS[0],
  .look = {
    [AL_FLOOR] = { TEX_GROUND_SALT, { 1.0f, 0.9f, 0.84f }, 2.05f, 9.0f, 0.85f, 0 },
    [AL_WALL] = { TEX_WALL_RUST, { 0.62f, 0.33f, 0.22f }, 1.6f, 4.0f, 0.9f, 0 },
    [AL_COVER] = { TEX_WALL_RUST, { 0.6f, 0.6f, 0.6f }, 1.5f, 3.0f, 0.8f, 0 },
    [AL_DECK] = { TEX_METAL, { 0.55f, 0.56f, 0.6f }, 1.5f, 4.0f, 0.7f, 0 },
    [AL_RAMP] = { TEX_ROAD_METAL, { 0.85f, 0.85f, 0.88f }, 1.3f, 6.0f, 0.6f, 0 },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.8f, 0 },
    [AL_FEATURE] = { TEX_METAL, { 0.9f, 0.62f, 0.12f }, 1.4f, 3.0f, 0.6f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.6f, 0.2f }, 1, 1, 1, 0, 3.5f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the world round it ───────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static ground_opts GO;
static float yard_ground(float x, float z, void *ud) {
  (void)ud;
  float dx = fabsf(x), dz = fabsf(z - ARENA_CZ), d = fmaxf(dx, dz);
  /* flat round the yard, a low berm of spoil, then the open pan and far rises */
  float berm = d > 90 && d < 150 ? 2.6f * sinf((d - 90) / 60.0f * 3.14159f) : 0;
  float far = d > 300 ? ground_noise(x, z, 260, 30, 3, 7) * fminf((d - 300) / 300.0f, 1.0f) : 0;
  return -0.05f + berm + far;
}

static void yard_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  GO = ground_defaults();
  GO.cx = 0, GO.cz = ARENA_CZ;
  GO.half = 1100.0f;
  GO.height = yard_ground;
  GO.tex = TEX_GROUND_SALT;
  GO.tex_repeat = 9.0f;
  GO.tint = pc_mul(hex(0xfff0ea), 2.4f);
  GO.tint2 = pc_mul(hex(0xf0c2bc), 2.2f);
  GO.patch_scale = 60.0f;
  GO.rough = 0.85f;
  GO.near_cell = split ? 96.0f : 64.0f;
  GO.detail_range = 40.0f;
  GO.follow_road = false;
  GO.darken_edge = 0;
  GO.lumpy = 0.2f;
  GO.seed = 31;
  ground_plane(scene, t, &GO);

  prop_set *ps = ps_new(scene, "YardProps");
  float W = a->half_x + 3, cz = ARENA_CZ;
  /* stacks of containers round the outside of the walls, a ring of them */
  int n = split ? 18 : 30;
  for (int i = 0; i < n; i++) {
    float u = (float)i / (float)n, ang = u * 6.2831853f, r = W + 12.0f + 10.0f * hf(100u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r;
    /* square it up to the walls */
    float k = fmaxf(fabsf(x), fabsf(z)) / r;
    x /= k, z /= k;
    x = fminf(fmaxf(x, -(W + 26)), W + 26), z = fminf(fmaxf(z, -(W + 26)), W + 26);
    ps_add(ps, i % 4 == 3 ? PK_WRECK_TRUCK : PK_CONTAINER, (t3_vec3){ x, -0.1f, z + cz }, (fabsf(x) > fabsf(z) ? 0.0f : 1.5708f) + 0.2f * hf(200u + (uint32_t)i), 1.3f, 300u + (uint32_t)i);
  }
  /* wrecks in heaps between them */
  for (int i = 0; i < (split ? 10 : 22); i++) {
    float ang = hf(400u + (uint32_t)i) * 6.2831853f, r = W + 30.0f + 30.0f * hf(410u + (uint32_t)i);
    ps_add(ps, i % 3 ? PK_WRECK_CAR : PK_TYRE_STACK, (t3_vec3){ sinf(ang) * r, -0.05f, cosf(ang) * r + cz }, hf(420u + (uint32_t)i) * 6.28f, 1.2f, 430u + (uint32_t)i);
  }
  /* flood lights at the corners and the crane over the north side */
  for (int c = 0; c < 4; c++) {
    float x = (c & 1 ? 1.0f : -1.0f) * (W + 6), z = (c & 2 ? 1.0f : -1.0f) * (W + 6);
    ps_add(ps, PK_LIGHT_POLE, (t3_vec3){ x, -0.05f, z + cz }, atan2f(-x, -z), 1.6f, 500u + (uint32_t)c);
  }
  ps_add(ps, PK_CRANE, (t3_vec3){ 0, -0.1f, W + 34 + cz }, 0.0f, 1.5f, 600);
  ps_add(ps, PK_WATER_TOWER, (t3_vec3){ -W - 40, -0.1f, -30 + cz }, 0.4f, 1.3f, 601);
  ps_add(ps, PK_BILLBOARD, (t3_vec3){ W + 30, -0.1f, -W - 20 + cz }, -2.3f, 1.2f, 602);
  ps_add(ps, PK_SILO, (t3_vec3){ W + 46, -0.1f, 40 + cz }, 0.0f, 1.2f, 603);
  /* the horizon: mesas and spires of the flats */
  for (int i = 0; i < (split ? 8 : 16); i++) {
    float ang = hf(700u + (uint32_t)i) * 6.2831853f, r = 520.0f + 360.0f * hf(710u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i & 1 ? PK_MESA : PK_SPIRE, (t3_vec3){ x, yard_ground(x, z, NULL) - 2.0f, z }, hf(720u + (uint32_t)i) * 6.28f, 1.4f + 1.6f * hf(730u + (uint32_t)i), 740u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.25f : 0.45f);
}

const map_def map_yard = {
  .name = "Crusher Yard",
  .blurb = "A scrapyard bowl: the crusher's mound in the middle, container walls to hide behind.",
  .scene = "scenes/salt.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.9f, 0.82f, 0.78f }, .kerb_a = { 0.93f, 0.36f, 0.1f }, .kerb_b = { 0.96f, 0.94f, 0.9f }, .wall = { 0.62f, 0.33f, 0.22f },
            .wall_top = { 0.8f, 0.6f, 0.5f }, .road_tex = TEX_ROAD_SALT, .wall_tex = TEX_WALL_RUST, .road_rough = 0.85f },
  .laps = 1,
  .par_lap = 180,
  .scenery = yard_scenery,
  .arena = &ARENA,
  .music = 1,
};
