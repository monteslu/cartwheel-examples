/* battle arena: Frost Lake. A frozen lake under a cold evening sky, ringed by snow banks: blue-white ice to slide across,
 * a snowy island in the middle to drive up and over, a camp of ice-fishing huts in the four quarters for cover, ice
 * boulders, a snow kicker at each side, and a low snow wall in front of each end with a gap to drive through. Outside:
 * snow-loaded pines, ice spikes and crags, falling snow, the huts' lanterns glowing. Capture the flag: a base at each end
 * (west red, east blue). */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
#define PAINT(x, z, y, w, d, yaw, col) { AP_BOX, AL_PAINT, x, z, y, w, 0.03f, d, yaw, true, col }
/* a fishing hut: the shed, a roof cap and a lantern by its door (yaw degrees) */
#define HUT(x, z, yaw, col) BOX(AL_COVER, x, z, 0, 6, 3.0f, 5, yaw, col), DECO(AL_COVER, x, z, 3.0f, 6.6f, 0.5f, 5.6f, yaw, 0x3a2f2a), GLOWPOST(x + 3.4f, z, 1.9f, 0.4f, 0.5f, 0xffc070)

#define REDW 0xb3402f
#define TEALW 0x2f8c8c
#define YELW 0xd6a53a
#define ICEB 0xcfe6f5

static const arena_prim PRIMS[] = {
  /* the snowy island */
  { AP_MOUND, AL_DECK, 0, 0, 0, 36, 3.0f, 30, 0, false, 0 },
  /* the camp: a hut in each quarter */
  HUT(-30, 22, 15, REDW), HUT(30, -22, 15, TEALW), HUT(30, 22, -15, YELW), HUT(-30, -22, -15, TEALW),
  /* ice boulders */
  POST(AL_FEATURE, -18, 34, 0, 3.6f, 2.2f, ICEB), POST(AL_FEATURE, 18, -34, 0, 3.6f, 2.2f, ICEB),
  POST(AL_FEATURE, 18, 36, 0, 2.6f, 1.6f, ICEB), POST(AL_FEATURE, -18, -36, 0, 2.6f, 1.6f, ICEB),
  POST(AL_FEATURE, -44, 30, 0, 3.0f, 1.8f, ICEB), POST(AL_FEATURE, 44, -30, 0, 3.0f, 1.8f, ICEB),
  /* a low snow wall before each end, with a gap in the middle */
  BOX(AL_COVER, -44, 12, 0, 2.0f, 1.4f, 9, 0, 0xeef4fa), BOX(AL_COVER, -44, -12, 0, 2.0f, 1.4f, 9, 0, 0xeef4fa),
  BOX(AL_COVER, 44, 12, 0, 2.0f, 1.4f, 9, 0, 0xeef4fa), BOX(AL_COVER, 44, -12, 0, 2.0f, 1.4f, 9, 0, 0xeef4fa),
  /* snow kickers, one at each side, launching towards the island */
  RAMP(AL_RAMP, 20, 40, 0, 7, 1.3f, 6, 180),
  RAMP(AL_RAMP, -20, -40, 0, 7, 1.3f, 6, 0),
  /* cracks and the old rink lines in the ice */
  PAINT(0, 0, 0, 0.5f, 98, 0, 0x7fa9c8),
  PAINT(-38, 0, 0, 0.5f, 98, 0, 0x9a6070), PAINT(38, 0, 0, 0.5f, 98, 0, 0x6070a0),
  PAINT(-10, 26, 0, 22, 0.3f, 32, 0x8fb4cc), PAINT(12, -27, 0, 26, 0.3f, -28, 0x8fb4cc), PAINT(-48, -36, 0, 14, 0.3f, 70, 0x8fb4cc),
};

static const float SPAWNS[][3] = {
  { 0, 40, 180 }, { 0, -40, 0 }, { -40, 0, 90 }, { 40, 0, -90 },
  { -42, 36, 135 }, { 42, -36, -45 }, { -42, -36, 45 }, { 42, 36, -135 },
};

/* capture the flag: red at the west end, blue at the east, behind their snow walls */
static const float TEAM_SPAWNS[8][3] = {
  { -56, 6, 90 }, { -56, -6, 90 }, { -50, 22, 110 }, { -50, -22, 70 },
  { 56, -6, -90 }, { 56, 6, -90 }, { 50, -22, -70 }, { 50, 22, -110 },
};

/* king of the mountain: the island first, then out on the open ice */
static const float HILLS[][2] = { { 0, 0 }, { -28, 0 }, { 28, 0 }, { 0, 36 }, { 0, -34 } };

static const float BOXES[][3] = {
  { -6, 0, 3 }, { 6, 0, 3 },
  { -18, 16, 0 }, { 18, 16, 0 }, { -18, -16, 0 }, { 18, -16, 0 },
  { -40, 40, 0 }, { 40, -40, 0 }, { 40, 40, 0 }, { -40, -40, 0 },
  { 0, 24, 0 }, { 0, -24, 0 },
};

static const arena_def ARENA = {
  .half_x = 64, .half_z = 50, .wall_h = 2.6f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = SPAWNS, .nspawns = sizeof SPAWNS / sizeof SPAWNS[0],
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { -55, 0 }, { 55, 0 } },
  .team_spawns = TEAM_SPAWNS,
  .hills = HILLS, .nhills = sizeof HILLS / sizeof HILLS[0],
  .look = {
    [AL_FLOOR] = { TEX_ROAD_ICE, { 0.66f, 0.8f, 0.98f }, 1.15f, 10.0f, 0.25f, 0 },
    [AL_WALL] = { TEX_GROUND_SNOW, { 0.9f, 0.94f, 1.0f }, 1.4f, 5.0f, 0.9f, 0 },
    [AL_COVER] = { TEX_WALL_WOOD, { 0.8f, 0.8f, 0.8f }, 1.4f, 3.0f, 0.85f, 0 },
    [AL_DECK] = { TEX_GROUND_SNOW, { 0.92f, 0.95f, 1.0f }, 1.35f, 6.0f, 0.9f, 0 },
    [AL_RAMP] = { TEX_GROUND_SNOW, { 0.85f, 0.9f, 1.0f }, 1.3f, 4.0f, 0.9f, 0 },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.8f, 0 },
    [AL_FEATURE] = { TEX_WALL_ICE, { 0.8f, 0.92f, 1.0f }, 1.4f, 3.0f, 0.2f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.75f, 0.4f }, 1, 1, 1, 0, 3.0f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.5f, 0 },
  },
};

/* ── the world round it ───────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float lake_ground(float x, float z, void *ud) {
  (void)ud;
  float dx = fabsf(x) - 64, dz = fabsf(z - ARENA_CZ) - 50, d = fmaxf(dx, dz);
  /* the lake's flat ice, a snowy shore, then hills of snow rising to the mountains */
  float shore = d > 8 ? fminf((d - 8) * 0.06f, 3.0f) : 0;
  float far = d > 120 ? ground_noise(x, z, 240, 40, 3, 29) * fminf((d - 120) / 300.0f, 1.0f) : 0;
  return -0.05f + shore + far;
}

static void lake_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1000.0f;
  g.height = lake_ground;
  g.tex = TEX_GROUND_SNOW;
  g.tex_repeat = 10.0f;
  g.tint = rgb(0.62f, 0.72f, 0.95f);
  g.tint2 = rgb(0.42f, 0.5f, 0.78f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 61;
  ground_plane(scene, t, &g);

  prop_set *ps = ps_new(scene, "FrostLakeProps");
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  /* a ring of snow-loaded pines round the shore, ice spikes and boulders among them */
  int n = split ? 20 : 40;
  for (int i = 0; i < n; i++) {
    float u = (float)i / (float)n, ang = u * 6.2831853f, r = 24.0f + 36.0f * hf(1300u + (uint32_t)i);
    float x = sinf(ang) * (hx + r), z = cosf(ang) * (hz + r);
    int kind = i % 5 == 4 ? PK_ICE_SPIKES : i % 7 == 3 ? PK_ICE_BOULDER : PK_SNOW_PINE;
    ps_add(ps, kind, (t3_vec3){ x, lake_ground(x, z + cz, NULL) - 0.2f, z + cz }, hf(1310u + (uint32_t)i) * 6.28f, 1.2f + 0.8f * hf(1320u + (uint32_t)i), 1330u + (uint32_t)i);
  }
  /* the camp's gear on the shore: barrels, a crate stack, lantern posts at the corners */
  for (int c = 0; c < 4; c++) {
    float x = (c & 1 ? 1.0f : -1.0f) * (hx + 6), z = (c & 2 ? 1.0f : -1.0f) * (hz + 6);
    ps_add(ps, PK_LIGHT_POLE, (t3_vec3){ x, -0.05f, z + cz }, atan2f(-x, -z), 1.5f, 1340u + (uint32_t)c);
    ps_add(ps, PK_BARREL, (t3_vec3){ x * 0.95f + 4, -0.05f, z * 0.95f + cz }, hf(1350u + (uint32_t)c) * 6.28f, 1.0f, 1355u + (uint32_t)c);
  }
  ps_add(ps, PK_CRATE, (t3_vec3){ -hx - 14, -0.05f, 20 + cz }, 0.3f, 1.3f, 1360);
  ps_add(ps, PK_TENT, (t3_vec3){ hx + 18, -0.05f, -24 + cz }, 1.2f, 1.2f, 1361);
  ps_add(ps, PK_WIND_PUMP, (t3_vec3){ -hx - 30, -0.05f, -34 + cz }, 0.6f, 1.2f, 1362);
  /* the far shore: big pines and ice spikes on the snow hills */
  for (int i = 0; i < (split ? 8 : 16); i++) {
    float ang = hf(1370u + (uint32_t)i) * 6.2831853f, r = 380.0f + 380.0f * hf(1380u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i & 1 ? PK_ICE_SPIKES : PK_SNOW_PINE, (t3_vec3){ x, lake_ground(x, z, NULL) - 4.0f, z }, hf(1390u + (uint32_t)i) * 6.28f, 2.2f + 2.0f * hf(1400u + (uint32_t)i), 1410u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_SNOW, split ? 0.5f : 1.0f);
  fx_ambient_at(scene, FXA_MIST, (t3_vec3){ 0, 1.0f, cz }, (t3_vec3){ hx * 0.8f, 1.0f, hz * 0.8f }, split ? 0.5f : 0.9f, 300.0f);
}

const map_def map_frostlake = {
  .name = "Frost Lake",
  .blurb = "A frozen lake in the snow: a camp of fishing huts, a snowy island, a base behind a snow wall at each end.",
  .scene = "scenes/frost.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.72f, 0.86f, 1.0f }, .kerb_a = { 0.8f, 0.2f, 0.2f }, .kerb_b = { 0.9f, 0.95f, 1.0f }, .wall = { 0.9f, 0.94f, 1.0f },
            .wall_top = { 0.95f, 0.97f, 1.0f }, .road_tex = TEX_ROAD_ICE, .wall_tex = TEX_WALL_ICE, .road_rough = 0.25f },
  .laps = 1,
  .par_lap = 180,
  .scenery = lake_scenery,
  .arena = &ARENA,
  .music = 6,
};
