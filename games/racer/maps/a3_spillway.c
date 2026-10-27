/* battle arena: Spillway Basin. The stilling basin at the foot of a colossal dam in a red canyon at midday: a long pale
 * concrete floor between tall basin walls, a raised apron at each end (ramps up at the sides, the front edge open to drop
 * off), two staggered rows of baffle blocks across the middle to hide behind, a bar of silt in the middle you drive up
 * and over, the training-wall piers either side of it, shallow water running down both side channels and a kicker in
 * each. Outside: the dam's face towering over the north end with the spill mist at its foot, the canyon walls and mesas.
 * Capture the flag: a base on each apron. */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define STRIP(x, z, w, d, col) { AP_BOX, AL_GLOW, x, z, 0.0f, w, 0.04f, d, 0, true, col }
#define PAINT(x, z, y, w, d, yaw, col) { AP_BOX, AL_PAINT, x, z, y, w, 0.03f, d, yaw, true, col }

#define SLAB 0xb8b0a4
#define STAIN 0x8f8579
#define CHANNEL 0x4fc8ff
#define AMBER 0xffb63a

#define APRON_H 2.5f

static const arena_prim PRIMS[] = {
  /* the aprons at both ends, and the ramps up to them at the sides */
  BOX(AL_DECK, 0, 64, 0, 100, APRON_H, 20, 0, 0),
  BOX(AL_DECK, 0, -64, 0, 100, APRON_H, 20, 0, 0),
  RAMP(AL_RAMP, -30, 48, 0, 14, APRON_H, 12, 0),
  RAMP(AL_RAMP, 30, 48, 0, 14, APRON_H, 12, 0),
  RAMP(AL_RAMP, -30, -48, 0, 14, APRON_H, 12, 180),
  RAMP(AL_RAMP, 30, -48, 0, 14, APRON_H, 12, 180),
  /* hazard paint along the aprons' open edges */
  DECO(AL_TRIM, 0, 54.5f, APRON_H, 44, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, 0, -54.5f, APRON_H, 44, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, 44, 54.5f, APRON_H, 12, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, -44, 54.5f, APRON_H, 12, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, 44, -54.5f, APRON_H, 12, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, -44, -54.5f, APRON_H, 12, 0.05f, 1.0f, 0, 0),
  /* the silt bar in the middle: drive up and over it */
  { AP_MOUND, AL_DECK, 0, 0, 0, 32, 2.5f, 32, 0, false, 0xc9b28a },
  /* the training-wall piers beside it */
  POST(AL_COVER, -27, 0, 0, 3.4f, 4.5f, STAIN),
  POST(AL_COVER, 27, 0, 0, 3.4f, 4.5f, STAIN),
  /* the baffle blocks: two staggered rows each side of the middle */
  BOX(AL_COVER, -34, 24, 0, 4, 1.8f, 3, 0, SLAB), BOX(AL_COVER, -22, 24, 0, 4, 1.8f, 3, 0, SLAB),
  BOX(AL_COVER, 22, 24, 0, 4, 1.8f, 3, 0, SLAB), BOX(AL_COVER, 34, 24, 0, 4, 1.8f, 3, 0, SLAB),
  BOX(AL_COVER, -28, 33, 0, 4, 1.8f, 3, 0, STAIN), BOX(AL_COVER, -16, 33, 0, 4, 1.8f, 3, 0, STAIN),
  BOX(AL_COVER, 16, 33, 0, 4, 1.8f, 3, 0, STAIN), BOX(AL_COVER, 28, 33, 0, 4, 1.8f, 3, 0, STAIN),
  BOX(AL_COVER, -34, -24, 0, 4, 1.8f, 3, 0, SLAB), BOX(AL_COVER, -22, -24, 0, 4, 1.8f, 3, 0, SLAB),
  BOX(AL_COVER, 22, -24, 0, 4, 1.8f, 3, 0, SLAB), BOX(AL_COVER, 34, -24, 0, 4, 1.8f, 3, 0, SLAB),
  BOX(AL_COVER, -28, -33, 0, 4, 1.8f, 3, 0, STAIN), BOX(AL_COVER, -16, -33, 0, 4, 1.8f, 3, 0, STAIN),
  BOX(AL_COVER, 16, -33, 0, 4, 1.8f, 3, 0, STAIN), BOX(AL_COVER, 28, -33, 0, 4, 1.8f, 3, 0, STAIN),
  /* kickers in the side channels, one each way */
  RAMP(AL_RAMP, 43, -12, 0, 7, 1.3f, 6, 0),
  RAMP(AL_RAMP, -43, 12, 0, 7, 1.3f, 6, 180),
  /* shallow water down the side channels, and the outfall grilles in the aprons' faces */
  STRIP(-44, 0, 6, 104, CHANNEL),
  STRIP(44, 0, 6, 104, CHANNEL),
  DECO(AL_GLOW, -14, 53.95f, 0.6f, 8, 1.0f, 0.1f, 0, AMBER),
  DECO(AL_GLOW, 14, 53.95f, 0.6f, 8, 1.0f, 0.1f, 0, AMBER),
  DECO(AL_GLOW, -14, -53.95f, 0.6f, 8, 1.0f, 0.1f, 0, AMBER),
  DECO(AL_GLOW, 14, -53.95f, 0.6f, 8, 1.0f, 0.1f, 0, AMBER),
  /* joints in the floor slabs */
  PAINT(0, 16, 0, 80, 0.4f, 0, 0x6d665e), PAINT(0, -16, 0, 80, 0.4f, 0, 0x6d665e),
  PAINT(0, 44, 0, 80, 0.4f, 0, 0x6d665e), PAINT(0, -44, 0, 80, 0.4f, 0, 0x6d665e),
  PAINT(-20, 0, 0, 0.4f, 108, 0, 0x6d665e), PAINT(20, 0, 0, 0.4f, 108, 0, 0x6d665e),
  /* the dam's face beyond the north wall, and the spill gate's lip on it (scenery only) */
  DECO(AL_FEATURE, 0, 92, -2, 190, 58, 14, 0, 0),
  DECO(AL_TRIM, 0, 84.8f, 40, 60, 2, 0.6f, 0, 0),
};

static const float SPAWNS[][3] = {
  { 0, 40, 180 }, { 0, -40, 0 }, { 38, 0, -90 }, { -38, 0, 90 },
  { 40, 36, 220 }, { -40, -36, 40 }, { -40, 36, 140 }, { 40, -36, -40 },
};

/* capture the flag: red on the north apron, blue on the south */
static const float TEAM_SPAWNS[8][3] = {
  { -12, 66, 180 }, { 12, 66, 180 }, { -30, 62, 180 }, { 30, 62, 180 },
  { 12, -66, 0 }, { -12, -66, 0 }, { 30, -62, 0 }, { -30, -62, 0 },
};

/* king of the mountain: the silt bar first, then the side channels and the aprons */
static const float HILLS[][2] = { { 0, 0 }, { -36, -10 }, { 36, 10 }, { 0, 64 }, { 0, -64 } };

static const float BOXES[][3] = {
  { -5, 0, 2.5f }, { 5, 0, 2.5f },
  { -20, 14, 0 }, { 20, 14, 0 }, { -20, -14, 0 }, { 20, -14, 0 },
  { 0, 40, 0 }, { 0, -40, 0 }, { -38, 0, 0 }, { 38, 0, 0 },
  { -24, 62, APRON_H }, { 24, 62, APRON_H }, { -24, -62, APRON_H }, { 24, -62, APRON_H },
};

static const arena_def ARENA = {
  .half_x = 50, .half_z = 74, .wall_h = 5.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = SPAWNS, .nspawns = sizeof SPAWNS / sizeof SPAWNS[0],
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { 0, 66 }, { 0, -66 } },
  .team_spawns = TEAM_SPAWNS,
  .hills = HILLS, .nhills = sizeof HILLS / sizeof HILLS[0],
  .look = {
    [AL_FLOOR] = { TEX_ROAD_CONCRETE, { 0.86f, 0.82f, 0.76f }, 1.6f, 8.0f, 0.85f, 0 },
    [AL_WALL] = { TEX_WALL_CONCRETE, { 0.78f, 0.72f, 0.66f }, 1.5f, 5.0f, 0.9f, 0 },
    [AL_COVER] = { TEX_WALL_CONCRETE, { 0.7f, 0.7f, 0.7f }, 1.4f, 3.0f, 0.9f, 0 },
    [AL_DECK] = { TEX_ROAD_CONCRETE, { 0.74f, 0.7f, 0.64f }, 1.5f, 6.0f, 0.85f, 0 },
    [AL_RAMP] = { TEX_ROAD_METAL, { 0.95f, 0.8f, 0.45f }, 1.3f, 5.0f, 0.6f, 0 },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.8f, 0 },
    [AL_FEATURE] = { TEX_WALL_CONCRETE, { 0.82f, 0.77f, 0.7f }, 1.3f, 12.0f, 0.95f, 0 },
    [AL_GLOW] = { -1, { 0.3f, 0.8f, 1.0f }, 1, 1, 1, 0, 2.2f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the world round it ───────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float spill_ground(float x, float z, void *ud) {
  (void)ud;
  float dx = fabsf(x), dz = fabsf(z - ARENA_CZ), d = fmaxf(dx, dz);
  /* the canyon floor, rising to its walls well out */
  return -0.05f + (d > 160 ? ground_noise(x, z, 200, 26, 3, 23) * fminf((d - 160) / 220.0f, 1.0f) + (d - 160) * 0.08f : 0);
}

static void spill_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1100.0f;
  g.height = spill_ground;
  g.tex = TEX_GROUND_ROCK;
  g.tex_repeat = 10.0f;
  g.tint = pc_mul(hex(0xc87a52), 1.5f);
  g.tint2 = pc_mul(hex(0x9c5a3e), 1.5f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 53;
  ground_plane(scene, t, &g);

  prop_set *ps = ps_new(scene, "SpillwayProps");
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  /* the canyon walls down both long sides and across the south end */
  for (int i = 0; i < (split ? 6 : 10); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz - 40 + (float)(i / 2) * 60.0f;
    ps_add(ps, PK_CANYON_WALL, (t3_vec3){ side * (hx + 70 + 20 * hf(1100u + (uint32_t)i)), -1.0f, z + cz }, side > 0 ? 1.5708f : -1.5708f, 2.2f + 0.6f * hf(1110u + (uint32_t)i), 1120u + (uint32_t)i);
  }
  ps_add(ps, PK_CANYON_WALL, (t3_vec3){ 0, -1.0f, -hz - 110 + cz }, 0, 2.6f, 1130);
  /* the spillway's gear: flood lights, the gantry over the gates, pipes and pylons along the rim */
  for (int c = 0; c < 4; c++) {
    float x = (c & 1 ? 1.0f : -1.0f) * (hx + 6), z = (c & 2 ? 1.0f : -1.0f) * (hz + 6);
    ps_add(ps, PK_LIGHT_POLE, (t3_vec3){ x, -0.05f, z + cz }, atan2f(-x, -z), 1.6f, 1140u + (uint32_t)c);
  }
  ps_add(ps, PK_CRANE, (t3_vec3){ 0, 54.0f, hz + 18 + cz }, 0.0f, 1.4f, 1150);
  for (int i = 0; i < 4; i++) {
    float side = i & 1 ? 1.0f : -1.0f;
    ps_add(ps, PK_PIPE_RACK, (t3_vec3){ side * (hx + 12), -0.1f, (i < 2 ? -40.0f : 30.0f) + cz }, 0, 1.2f, 1160u + (uint32_t)i);
  }
  for (int i = 0; i < (split ? 3 : 6); i++) {
    float side = i & 1 ? 1.0f : -1.0f;
    ps_add(ps, PK_PYLON, (t3_vec3){ side * (hx + 34), -0.1f, -hz + 30 + (float)(i / 2) * 50.0f + cz }, 0, 1.3f, 1170u + (uint32_t)i);
  }
  for (int i = 0; i < (split ? 6 : 14); i++) {
    float ang = hf(1180u + (uint32_t)i) * 6.2831853f, r = 260.0f + 300.0f * hf(1190u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i & 1 ? PK_MESA : PK_BOULDERS, (t3_vec3){ x, spill_ground(x, z, NULL) - 2.0f, z }, hf(1200u + (uint32_t)i) * 6.28f, 1.6f + 1.4f * hf(1210u + (uint32_t)i), 1220u + (uint32_t)i);
  }
  ps_finish(ps);
  /* the spill's mist at the foot of the dam, and dust on the canyon wind */
  fx_ambient_at(scene, FXA_MIST, (t3_vec3){ 0, 4.0f, hz + 4 + cz }, (t3_vec3){ 40.0f, 3.0f, 4.0f }, split ? 0.6f : 1.0f, 260.0f);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.2f : 0.35f);
}

const map_def map_spillway = {
  .name = "Spillway Basin",
  .blurb = "The dam's stilling basin: an apron at each end, baffle blocks to hide behind, a silt bar in the middle.",
  .scene = "scenes/dam.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.86f, 0.82f, 0.76f }, .kerb_a = { 0.95f, 0.75f, 0.2f }, .kerb_b = { 0.9f, 0.9f, 0.88f }, .wall = { 0.78f, 0.72f, 0.66f },
            .wall_top = { 0.85f, 0.8f, 0.75f }, .road_tex = TEX_ROAD_CONCRETE, .wall_tex = TEX_WALL_CONCRETE, .road_rough = 0.85f },
  .laps = 1,
  .par_lap = 180,
  .scenery = spill_scenery,
  .arena = &ARENA,
  .music = 5,
};
