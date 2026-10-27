/* capture the flag map: Mesa Run. A red-rock canyon at sundown, 560 m long, a flat-topped mesa across each end with a
 * team's base on top and a butte standing in the middle of the canyon floor. Racing it end to end:
 *
 *   the mesas    the base, its flag and a gateway on top; a long ramp down each side towards the middle, and the front a
 *                six-metre cliff: drop straight off it (the quick way out) but come home up a ramp
 *   the wash     the west side of the butte: open canyon floor, two crests to jump and boost strips down it
 *   the causeway the east side: a raised road along the canyon wall with boost strips on top, ramps at its ends, and the
 *                floor beside it below for whoever drops off
 * Red holds the north mesa (+z), blue the south; every red shape has its blue twin turned half round. A capture the flag
 * map only (arena_def.modes). */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }

#define RED 0xff4a2c
#define BLUE 0x3d86ff
#define SAND 0xd9a066
#define ADOBE 0xc98a5a

#define MESA_H 6.0f
#define BASE_Z 252.0f

/* one mesa, S = 1 red (north), -1 blue (south), TC its team's colour, Y its ramps' yaw (rising towards the mesa) */
#define MESA(S, TC, Y)                                                                                                 \
  /* the mesa and the ramps down its sides */                                                                          \
  BOX(AL_DECK, 0, S * 258, 0, 140, MESA_H, 44, 0, 0),                                                                  \
  RAMP(AL_RAMP, -50, S * 221, 0, 16, MESA_H, 30, Y), RAMP(AL_RAMP, 50, S * 221, 0, 16, MESA_H, 30, Y),                 \
  /* the gateway behind the base: two adobe towers and a lintel, the team's lamps on them */                           \
  BOX(AL_FEATURE, -11, S * 270, MESA_H, 5, 9, 5, 0, ADOBE), BOX(AL_FEATURE, 11, S * 270, MESA_H, 5, 9, 5, 0, ADOBE),   \
  BOX(AL_FEATURE, 0, S * 270, MESA_H + 7, 27, 2.4f, 5.6f, 0, ADOBE),                                                   \
  DECO(AL_GLOW, 0, S * (270 - 2.85f), MESA_H + 7.6f, 14, 0.5f, 0.1f, 0, TC),                                           \
  GLOWPOST(-11, S * 270, MESA_H + 9, 3.2f, 0.4f, TC), GLOWPOST(11, S * 270, MESA_H + 9, 3.2f, 0.4f, TC),              \
  /* a low wall along the cliff edge each side of the base, leaving the ramps and the middle (open to drop off) */      \
  BOX(AL_WALL, -28.5f, S * 236.6f, MESA_H, 25, 1.2f, 1.2f, 0, 0), BOX(AL_WALL, 28.5f, S * 236.6f, MESA_H, 25, 1.2f, 1.2f, 0, 0), \
  BOX(AL_WALL, -64.5f, S * 236.6f, MESA_H, 11, 1.2f, 1.2f, 0, 0), BOX(AL_WALL, 64.5f, S * 236.6f, MESA_H, 11, 1.2f, 1.2f, 0, 0), \
  /* hazard paint on the open edge */                                                                                  \
  DECO(AL_TRIM, 0, S * 236.6f, MESA_H, 32, 0.04f, 1.2f, 0, 0)

/* a crest across the wash: up and down, each half 14 m wide and 8 m long, 1.6 m up at the ridge */
#define CREST(z)                                                                                                       \
  RAMP(AL_RAMP, -44, z - 4, 0, 14, 1.6f, 8, 0), RAMP(AL_RAMP, -44, z + 4, 0, 14, 1.6f, 8, 180)

static const arena_prim PRIMS[] = {
  MESA(1.0f, RED, 0),
  MESA(-1.0f, BLUE, 180),
  /* the butte in the middle of the canyon, and its fallen rocks */
  BOX(AL_FEATURE, 0, 0, 0, 22, 16, 96, 0, 0),
  POST(AL_COVER, -13, 56, 0, 5, 3.5f, 0), POST(AL_COVER, 12, -58, 0, 6, 4, 0), POST(AL_COVER, 14, 54, 0, 3.5f, 2.6f, 0),
  POST(AL_COVER, -14, -54, 0, 4, 3, 0),
  /* the wash: two crests */
  CREST(48), CREST(-48),
  /* the causeway along the east wall, the ramps at its ends */
  BOX(AL_DECK, 60, 0, 0, 20, 4, 200, 0, 0),
  RAMP(AL_RAMP, 60, 112, 0, 20, 4, 24, 180), RAMP(AL_RAMP, 60, -112, 0, 20, 4, 24, 0),
  DECO(AL_TRIM, 50.4f, 0, 4, 0.8f, 0.04f, 200, 0, 0),
};

/* the boost strips: down the wash, along the causeway's top */
static const arena_boost BOOSTS[] = {
  { -44, 130, 0, 8, 12, 0 }, { -44, 0, 0, 8, 12, 0 }, { -44, -130, 0, 8, 12, 0 },
  { 60, 60, 4, 8, 12, 0 }, { 60, -60, 4, 8, 12, 0 },
};

/* capture the flag: red on the north mesa, blue on the south, facing the canyon */
static const float TEAM_SPAWNS[8][3] = {
  { -8, 244, 180 }, { 8, 244, 180 }, { -22, 246, 180 }, { 22, 246, 180 },
  { 8, -244, 0 }, { -8, -244, 0 }, { 22, -246, 0 }, { -22, -246, 0 },
};

static const float BOXES[][3] = {
  { -44, 90, 0 }, { -44, -90, 0 }, { -36, 0, 0 }, { -52, 0, 0 },
  { 60, 20, 4 }, { 60, -20, 4 }, { 30, 90, 0 }, { 30, -90, 0 },
  { -50, 160, 0 }, { 31, 160, 0 }, { 50, -160, 0 }, { -31, -160, 0 },
};

/* the CPU drivers' way round the butte and up the mesas */
static const float ROUTE[][2] = {
  { 0, 250 }, { -50, 240 }, { 50, 240 }, { -50, 200 }, { 50, 200 }, { -44, 70 }, { -44, -70 }, { 31, 70 }, { 31, -70 },
  { 60, 130 }, { 60, 0 }, { 60, -130 },
  { 0, -250 }, { 50, -240 }, { -50, -240 }, { 50, -200 }, { -50, -200 },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 4 }, { 3, 5 }, { 4, 7 }, { 4, 9 }, { 5, 6 }, { 7, 8 }, { 5, 7 }, { 6, 8 },
  { 9, 10 }, { 10, 11 }, { 6, 16 }, { 8, 15 }, { 11, 15 }, { 15, 16 }, { 15, 13 }, { 16, 14 }, { 13, 12 }, { 14, 12 },
};

static const arena_def ARENA = {
  .half_x = 70, .half_z = 280, .wall_h = 12.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = TEAM_SPAWNS, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { 0, BASE_Z }, { 0, -BASE_Z } },
  .team_spawns = TEAM_SPAWNS,
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_CTF,
  .look = {
    [AL_FLOOR] = { TEX_GROUND_SAND, { 0.92f, 0.62f, 0.42f }, 1.2f, 9.0f, 0.9f, 0 },
    [AL_WALL] = { TEX_GROUND_ROCK, { 0.82f, 0.48f, 0.32f }, 1.5f, 8.0f, 0.95f, 0 },
    [AL_COVER] = { TEX_GROUND_ROCK, { 0.78f, 0.46f, 0.32f }, 1.5f, 4.0f, 0.95f, 0 },
    [AL_DECK] = { TEX_GROUND_ROCK, { 0.86f, 0.56f, 0.38f }, 1.5f, 8.0f, 0.95f, 0 },
    [AL_RAMP] = { TEX_ROAD_DIRT, { 0.92f, 0.68f, 0.48f }, 1.5f, 6.0f, 0.9f, 0 },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.8f, 0 },
    [AL_FEATURE] = { TEX_GROUND_ROCK, { 0.8f, 0.46f, 0.3f }, 1.45f, 10.0f, 0.95f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.7f, 0.4f }, 1, 1, 1, 0, 3.0f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the world round it ───────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float mesa_ground(float x, float z, void *ud) {
  (void)ud;
  float dx = fabsf(x), dz = fabsf(z - ARENA_CZ);
  /* the plateau the canyon is cut into: level with the tops of its walls just outside them, rolling away beyond */
  float out = fmaxf(dx - 73.0f, dz - 283.0f);
  if (out <= 0) return -0.05f;
  float rim = fminf(out * 4.0f, 12.0f);
  return -0.05f + rim + ground_noise(x, z, 220, 18, 3, 31) * fminf(out / 200.0f, 1.0f);
}

static void mesa_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1400.0f;
  g.height = mesa_ground;
  g.tex = TEX_GROUND_SAND;
  g.tex_repeat = 10.0f;
  g.tint = pc_mul(hex(0xd88a58), 1.4f);
  g.tint2 = pc_mul(hex(0xa45a3a), 1.4f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 67;
  ground_plane(scene, t, &g);

  prop_set *ps = ps_new(scene, "MesaProps");
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  /* the butte's rock over its block: three banded stacks along it */
  for (int i = -1; i <= 1; i++)
    ps_add_ex(ps, PK_MESA, i & 1, 0, (t3_vec3){ 0, -0.5f, (float)i * 36.0f + cz }, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, 0.4f * (float)i), (t3_vec3){ 0.5f, 0.8f, 0.5f }, (pcol){ 1, 1, 1 }, 1700u + (uint32_t)(i + 1));
  /* spires and boulders on the plateau along the rims (from the canyon floor their tops stand over the walls) */
  for (int i = 0; i < (split ? 10 : 18); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 40 + (float)(i / 2) * 60.0f + 20 * hf(1540u + (uint32_t)i), x = side * (hx + 12 + 14 * hf(1545u + (uint32_t)i));
    ps_add(ps, i % 3 ? PK_BOULDERS : PK_SPIRE, (t3_vec3){ x, mesa_ground(x, z + cz, NULL) - 0.2f, z + cz }, hf(1550u + (uint32_t)i) * 6.28f, 1.2f + 0.8f * hf(1560u + (uint32_t)i), 1570u + (uint32_t)i);
  }
  for (int i = 0; i < (split ? 16 : 32); i++) {
    float side = i & 1 ? 1.0f : -1.0f, x = side * (hx + 40 + 120 * hf(1580u + (uint32_t)i)), z = (hf(1590u + (uint32_t)i) * 2 - 1) * (hz + 60) + cz;
    ps_add(ps, i % 4 ? PK_CACTUS : PK_DEAD_TREE, (t3_vec3){ x, mesa_ground(x, z, NULL) - 0.1f, z }, hf(1600u + (uint32_t)i) * 6.28f, 1.0f + 0.6f * hf(1610u + (uint32_t)i), 1620u + (uint32_t)i);
  }
  /* the far desert: mesas and spires on the skyline */
  for (int i = 0; i < (split ? 10 : 20); i++) {
    float ang = hf(1630u + (uint32_t)i) * 6.2831853f, r = 600.0f + 500.0f * hf(1640u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i & 1 ? PK_MESA : PK_SPIRE, (t3_vec3){ x, mesa_ground(x, z, NULL) - 3.0f, z }, hf(1650u + (uint32_t)i) * 6.28f, 2.4f + 2.4f * hf(1660u + (uint32_t)i), 1670u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.25f : 0.45f);
}

const map_def map_mesa = {
  .name = "Mesa Run",
  .blurb = "A red canyon at sundown: a base on a mesa at each end, the wash and the causeway between them, a butte in the middle.",
  .scene = "scenes/mesa.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.95f, 0.72f, 0.5f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.82f, 0.48f, 0.32f },
            .wall_top = { 0.9f, 0.6f, 0.4f }, .road_tex = TEX_ROAD_SAND, .wall_tex = TEX_WALL_STONE, .road_rough = 0.9f },
  .laps = 1,
  .par_lap = 180,
  .scenery = mesa_scenery,
  .arena = &ARENA,
  .music = 5,
};
