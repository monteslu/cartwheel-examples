/* battle arena: Foundry Pit. A steel works at dusk, two floors: the casting floor below with the furnace burning in
 * the middle (a ring of neon round it, tanks and pipe runs for cover, a kicker along each long wall), and the loading
 * deck above at the north end, four metres up, reached by two wide ramps at the sides and a steep one in the middle;
 * its front edge is open, so a kart can drop off it anywhere. Outside: cooling towers, flare stacks and tanks under a
 * dark violet sky, embers over the furnace. */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
#define STRIP(x, z, w, d, yaw, col) { AP_BOX, AL_GLOW, x, z, 0.0f, w, 0.035f, d, yaw, true, col }

#define CYAN 0x38e8ff
#define MAGENTA 0xff3fb8
#define AMBER 0xffa02a
#define STEEL 0x6b7078
#define OXIDE 0x8c3a2c
#define OLIVE 0x5b6b3a


static const arena_prim PRIMS[] = {
  /* the loading deck and the ramps up to it */
  BOX(AL_DECK, 0, 54, 0, 116, 4, 32, 0, 0),
  RAMP(AL_RAMP, -42, 27, 0, 14, 4, 22, 0),
  RAMP(AL_RAMP, 42, 27, 0, 14, 4, 22, 0),
  RAMP(AL_RAMP, 0, 29, 0, 8, 4, 18, 0),
  /* the deck's open edge: hazard paint, and lights under the lip */
  DECO(AL_TRIM, -21.5f, 38.5f, 4.0f, 21, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, 21.5f, 38.5f, 4.0f, 21, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, -53, 38.5f, 4.0f, 8, 0.05f, 1.0f, 0, 0),
  DECO(AL_TRIM, 53, 38.5f, 4.0f, 8, 0.05f, 1.0f, 0, 0),
  DECO(AL_GLOW, -21.5f, 37.95f, 3.3f, 21, 0.25f, 0.1f, 0, CYAN),
  DECO(AL_GLOW, 21.5f, 37.95f, 3.3f, 21, 0.25f, 0.1f, 0, CYAN),
  /* cover on the deck: crates and the control booth */
  BOX(AL_COVER, -22, 52, 4, 6, 2.4f, 6, 15, OLIVE),
  BOX(AL_COVER, 22, 52, 4, 6, 2.4f, 6, -15, OXIDE),
  BOX(AL_FEATURE, 0, 62, 4, 12, 3.6f, 7, 0, 0),
  DECO(AL_GLOW, 0, 58.45f, 5.4f, 9, 1.0f, 0.1f, 0, AMBER),
  BOX(AL_COVER, -44, 60, 4, 3, 2.2f, 8, 0, STEEL),
  BOX(AL_COVER, 44, 60, 4, 3, 2.2f, 8, 0, STEEL),
  /* the furnace, its burning top and the ring of neon */
  POST(AL_FEATURE, 0, -18, 0, 12, 5, 0),
  GLOWPOST(0, -18, 5.0f, 9.5f, 0.12f, 0xff6a1a),
  GLOWPOST(0, -18, 3.6f, 12.3f, 0.4f, AMBER),
  STRIP(4.975f, -5.99f, 10.0f, 0.35f, 22.5f, MAGENTA),
  STRIP(12.01f, -13.03f, 10.0f, 0.35f, 67.5f, MAGENTA),
  STRIP(12.01f, -22.98f, 10.0f, 0.35f, 112.5f, MAGENTA),
  STRIP(4.975f, -30.01f, 10.0f, 0.35f, 157.5f, MAGENTA),
  STRIP(-4.975f, -30.01f, 10.0f, 0.35f, 202.5f, MAGENTA),
  STRIP(-12.01f, -22.98f, 10.0f, 0.35f, 247.5f, MAGENTA),
  STRIP(-12.01f, -13.03f, 10.0f, 0.35f, 292.5f, MAGENTA),
  STRIP(-4.975f, -5.99f, 10.0f, 0.35f, 337.5f, MAGENTA),
  /* tanks and pipe runs on the casting floor */
  POST(AL_COVER, -32, -44, 0, 7, 4.5f, STEEL),
  POST(AL_COVER, 32, -44, 0, 7, 4.5f, STEEL),
  POST(AL_COVER, -30, 4, 0, 5, 3.5f, OXIDE),
  POST(AL_COVER, 30, 4, 0, 5, 3.5f, OXIDE),
  BOX(AL_COVER, 0, -52, 0, 26, 1.3f, 1.8f, 0, OLIVE),
  BOX(AL_COVER, -44, -14, 0, 1.8f, 1.3f, 20, 0, OLIVE),
  BOX(AL_COVER, 44, -14, 0, 1.8f, 1.3f, 20, 0, OLIVE),
  BOX(AL_COVER, -16, 14, 0, 8, 2.4f, 2.4f, 30, STEEL),
  BOX(AL_COVER, 16, 14, 0, 8, 2.4f, 2.4f, -30, STEEL),
  /* kickers along the long walls, and lit lines along the casting floor */
  RAMP(AL_RAMP, -52, -36, 0, 7, 1.4f, 6, 0),
  RAMP(AL_RAMP, 52, -36, 0, 7, 1.4f, 6, 0),
  STRIP(-24, -30, 0.3f, 60, 0, CYAN),
  STRIP(24, -30, 0.3f, 60, 0, CYAN),
};

static const float SPAWNS[][3] = {
  { -24, -64, 20 }, { 24, 62, 180 }, { 24, -64, -20 }, { -24, 62, 180 },
  { -52, 6, 100 }, { 52, 6, -100 }, { -50, 48, 120 }, { 50, 48, -120 },
};

static const float BOXES[][3] = {
  { 9, -18, 0 }, { -9, -18, 0 }, { 0, -8, 0 }, { 0, -28, 0 },
  { -32, -32, 0 }, { 32, -32, 0 }, { -32, -58, 0 }, { 32, -58, 0 },
  { -42, 8, 0 }, { 42, 8, 0 }, { 0, 4, 0 },
  { -30, 46, 4 }, { 30, 46, 4 }, { 0, 48, 4 }, { -50, 66, 4 }, { 50, 66, 4 },
};

static const arena_def ARENA = {
  .half_x = 58, .half_z = 70, .wall_h = 7.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = SPAWNS, .nspawns = sizeof SPAWNS / sizeof SPAWNS[0],
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .look = {
    [AL_FLOOR] = { TEX_ROAD_METAL, { 0.42f, 0.42f, 0.46f }, 1.6f, 6.0f, 0.7f, 0 },
    [AL_WALL] = { TEX_WALL_TECH, { 0.5f, 0.5f, 0.58f }, 1.4f, 4.0f, 0.8f, 0 },
    [AL_COVER] = { TEX_METAL, { 0.6f, 0.6f, 0.6f }, 1.3f, 3.0f, 0.7f, 0 },
    [AL_DECK] = { TEX_WALL_CONCRETE, { 0.55f, 0.53f, 0.52f }, 1.4f, 4.0f, 0.9f, 0 },
    [AL_RAMP] = { TEX_ROAD_METAL, { 0.95f, 0.82f, 0.5f }, 1.2f, 5.0f, 0.6f, 0 },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.8f, 0 },
    [AL_FEATURE] = { TEX_RUST, { 0.55f, 0.36f, 0.3f }, 1.3f, 3.0f, 0.8f, 0 },
    [AL_GLOW] = { -1, { 0.3f, 0.9f, 1.0f }, 1, 1, 1, 0, 4.0f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float foundry_ground(float x, float z, void *ud) {
  (void)ud;
  float d = fmaxf(fabsf(x), fabsf(z - ARENA_CZ));
  return -0.05f + (d > 260 ? ground_noise(x, z, 220, 24, 3, 11) * fminf((d - 260) / 260.0f, 1.0f) : 0);
}

static void foundry_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1000.0f;
  g.height = foundry_ground;
  g.tex = TEX_GROUND_ASH;
  g.tex_repeat = 8.0f;
  g.tint = pc_mul(hex(0x6a5a58), 1.6f);
  g.tint2 = pc_mul(hex(0x4a3e44), 1.6f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 41;
  ground_plane(scene, t, &g);

  prop_set *ps = ps_new(scene, "FoundryProps");
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  ps_add(ps, PK_COOLING_TOWER, (t3_vec3){ -hx - 70, -0.2f, 40 + cz }, 0, 1.4f, 900);
  ps_add(ps, PK_COOLING_TOWER, (t3_vec3){ -hx - 95, -0.2f, -40 + cz }, 0, 1.2f, 901);
  ps_add(ps, PK_FLARE_STACK, (t3_vec3){ hx + 60, -0.2f, hz + 40 + cz }, 0.5f, 1.3f, 902);
  ps_add(ps, PK_FLARE_STACK, (t3_vec3){ hx + 80, -0.2f, -hz + cz }, 2.0f, 1.1f, 903);
  for (int i = 0; i < (split ? 6 : 12); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 20 + (float)(i / 2) * 26.0f;
    ps_add(ps, i % 3 ? PK_TANK : PK_SILO, (t3_vec3){ side * (hx + 22 + 8 * hf(950u + (uint32_t)i)), -0.2f, z + cz }, hf(960u + (uint32_t)i) * 6.28f, 1.2f, 970u + (uint32_t)i);
  }
  for (int i = 0; i < 4; i++) {
    float side = i & 1 ? 1.0f : -1.0f;
    ps_add(ps, PK_PIPE_RACK, (t3_vec3){ side * (hx + 10), -0.2f, (i < 2 ? -30.0f : 30.0f) + cz }, 0, 1.3f, 980u + (uint32_t)i);
  }
  for (int c = 0; c < 4; c++) {
    float x = (c & 1 ? 1.0f : -1.0f) * (hx + 5), z = (c & 2 ? 1.0f : -1.0f) * (hz + 5);
    ps_add(ps, PK_LIGHT_POLE, (t3_vec3){ x, -0.05f, z + cz }, atan2f(-x, -z), 1.6f, 990u + (uint32_t)c);
  }
  for (int i = 0; i < (split ? 8 : 18); i++) {
    float ang = hf(1000u + (uint32_t)i) * 6.2831853f, r = 140.0f + 180.0f * hf(1010u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i % 4 == 0 ? PK_PYLON : i % 4 == 1 ? PK_TANK : i % 4 == 2 ? PK_CONTAINER : PK_SILO, (t3_vec3){ x, foundry_ground(x, z, NULL) - 0.2f, z }, hf(1020u + (uint32_t)i) * 6.28f,
           1.1f + 0.4f * hf(1030u + (uint32_t)i), 1040u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_at(scene, FXA_EMBERS, (t3_vec3){ 0, 7.0f, -18 + cz }, (t3_vec3){ 4.0f, 2.0f, 4.0f }, split ? 0.8f : 1.4f, 260.0f);
  fx_ambient_at(scene, FXA_STEAM, (t3_vec3){ -hx - 70, 60.0f, 40 + cz }, (t3_vec3){ 8.0f, 4.0f, 8.0f }, 0.8f, 900.0f);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.2f : 0.35f);
}

const map_def map_foundry = {
  .name = "Foundry Pit",
  .blurb = "Two floors of a steel works: the furnace below, the loading deck above.",
  .scene = "scenes/refinery.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.42f, 0.42f, 0.46f }, .kerb_a = { 0.9f, 0.2f, 0.6f }, .kerb_b = { 0.2f, 0.9f, 1.0f }, .wall = { 0.5f, 0.5f, 0.58f },
            .wall_top = { 0.6f, 0.6f, 0.65f }, .road_tex = TEX_ROAD_METAL, .wall_tex = TEX_WALL_TECH, .road_rough = 0.7f },
  .laps = 1,
  .par_lap = 180,
  .scenery = foundry_scenery,
  .arena = &ARENA,
  .music = 4,
};
