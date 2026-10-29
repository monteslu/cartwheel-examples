/* capture the flag map: Floodgate. Two dams face each other across a flooded canyon, 740 m apart, a team's flag up on
 * each dam's crest. Three ways across, stacked:
 *
 *   the high road   a broken highway bridge from crest to crest down the middle: boost strips on to a kicker at each end of
 *                   the gap, 36 m of air over the water (fall short and you are in the reservoir)
 *   the spillways   a covered spillway down each side of the canyon: the low road runs in its tunnel, and the tunnel's end
 *                   under the enemy dam is a pad straight up onto their crest (one way: no pad home). The roofs are a
 *                   middle road of their own, with a ramp up at each end onto the crests and one down to the reservoir in
 *                   the middle
 *   the reservoir   the canyon floor under a few hands of water: the widest way, an island with boxes in the middle, and
 *                   the bridge's piers to weave through
 * Red holds the north dam (+z), blue the south; every red shape has its blue twin turned half round. A capture the flag
 * map only (arena_def.modes). */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
#define DECK(x, z, y, w, h, d) { AP_BOX, AL_DECK, x, z, y, w, h, d, 0, false, 0, 1.3f }
#define MOUND(x, z, w, h, d) { AP_MOUND, AL_DECK, x, z, 0, w, h, d, 0, false, 0 }

#define RED 0xff4a2c
#define BLUE 0x3d86ff
#define CONC 0xb4b0a6
#define DAM_H 14.0f
#define DAM_Z 335.0f
#define ROOF_Y 8.0f

/* one dam and its works, S = 1 red (north), -1 blue (south), TC its team's colour, Y the yaw of its ramps rising towards it */
#define DAM(S, TC, Y)                                                                                                  \
  DECK(0, S * DAM_Z, 0, 220, DAM_H, 70),                                                                               \
  /* the intake towers either side of the flag and the team's lamps on them */                                         \
  BOX(AL_FEATURE, -16, S * 356, DAM_H, 7, 12, 7, 0, CONC), BOX(AL_FEATURE, 16, S * 356, DAM_H, 7, 12, 7, 0, CONC),      \
  BOX(AL_FEATURE, 0, S * 356, DAM_H + 10, 39, 2.4f, 7, 0, CONC),                                                       \
  DECO(AL_GLOW, 0, S * (356 - 3.55f), DAM_H + 10.6f, 22, 0.6f, 0.1f, 0, TC),                                           \
  GLOWPOST(-16, S * 356, DAM_H + 12, 3.4f, 0.5f, TC), GLOWPOST(16, S * 356, DAM_H + 12, 3.4f, 0.5f, TC),               \
  /* the bridge out from the crest and the kicker at its broken end */                                                 \
  BOX(AL_DECK, 0, S * 159, DAM_H - 2, 14, 2, 282, 0, 0), /* (rails down its sides only: its end is the jump) */          \
  BOX(AL_TRIM, -6.7f, S * 163, DAM_H, 0.6f, 1.3f, 274, 0, 0), BOX(AL_TRIM, 6.7f, S * 163, DAM_H, 0.6f, 1.3f, 274, 0, 0),   \
  RAMP(AL_RAMP, 0, S * 18, DAM_H, 12, 1.4f, 8, Y + 180), /* (out past the end: the rail leaves the end open for it) */ \
  POST(AL_FEATURE, 0, S * 70, 0, 6, DAM_H - 2, CONC), POST(AL_FEATURE, 0, S * 150, 0, 6, DAM_H - 2, CONC),               \
  POST(AL_FEATURE, 0, S * 230, 0, 6, DAM_H - 2, CONC),                                                                 \
  /* the spillway roofs' ramps up onto the crest, and the gates in the dam's face that pour into the spillways */        \
  RAMP(AL_RAMP, 86, S * 285, ROOF_Y, 46, DAM_H - ROOF_Y, 30, Y), RAMP(AL_RAMP, -86, S * 285, ROOF_Y, 46, DAM_H - ROOF_Y, 30, Y), \
  DECO(AL_GLOW, -86, S * 299.9f, 3, 30, 0.4f, 0.1f, 0, 0x9ad8ff), DECO(AL_GLOW, 86, S * 299.9f, 3, 30, 0.4f, 0.1f, 0, 0x9ad8ff)

/* the dam's face to the reservoir, S its side: buttress ribs up it, white water spilling down two chutes into foam at its
 * foot, lamps along the crest, and the bridge's girder under its deck (drawn only) */
#define RIB(S, x) DECO(AL_FEATURE, x, S * (300 - 0.8f), 0, 2.4f, DAM_H, 1.6f, 0, 0xa8a49a)
#define LAMP(S, x) GLOWPOST(x, S * 301, DAM_H + 0.2f, 0.5f, 1.4f, 0xffe2a8)
#define DAMFACE(S)                                                                                                     \
  RIB(S, -96), RIB(S, -74), RIB(S, -52), RIB(S, -30), RIB(S, 30), RIB(S, 52), RIB(S, 74), RIB(S, 96),                  \
  DECO(AL_GLOW, -41, S * (300 - 0.25f), 0.2f, 12, DAM_H - 1.5f, 0.15f, 0, 0xc8ecff),                                   \
  DECO(AL_GLOW, 41, S * (300 - 0.25f), 0.2f, 12, DAM_H - 1.5f, 0.15f, 0, 0xc8ecff),                                    \
  DECO(AL_PAINT, -41, S * 293, 0.35f, 22, 0.02f, 12, 0, 0xf2f6f8), DECO(AL_PAINT, 41, S * 293, 0.35f, 22, 0.02f, 12, 0, 0xf2f6f8), \
  LAMP(S, -100), LAMP(S, -75), LAMP(S, -50), LAMP(S, -25), LAMP(S, 25), LAMP(S, 50), LAMP(S, 75), LAMP(S, 100),        \
  DECO(AL_FEATURE, 0, S * 159, DAM_H - 3.4f, 8, 1.4f, 282, 0, 0x5a5e64)

/* a spillway along one side, SX = 1 east, -1 west: its roof (the middle road), the inner wall with doorways, the ramp down
 * to the reservoir from the roof's middle */
#define SPILLWAY(SX)                                                                                                   \
  DECK(SX * 86, 0, ROOF_Y - 2, 48, 2, 540),                                                                            \
  BOX(AL_FEATURE, SX * 63, 180, 0, 2, ROOF_Y - 2, 60, 0, 0), BOX(AL_FEATURE, SX * 63, -180, 0, 2, ROOF_Y - 2, 60, 0, 0),      \
  BOX(AL_FEATURE, SX * 63, 70, 0, 2, ROOF_Y - 2, 40, 0, 0), BOX(AL_FEATURE, SX * 63, -70, 0, 2, ROOF_Y - 2, 40, 0, 0),        \
  POST(AL_FEATURE, SX * 75, 120, 0, 2.5f, ROOF_Y - 2, CONC), POST(AL_FEATURE, SX * 75, -120, 0, 2.5f, ROOF_Y - 2, CONC),   \
  POST(AL_FEATURE, SX * 98, 120, 0, 2.5f, ROOF_Y - 2, CONC), POST(AL_FEATURE, SX * 98, -120, 0, 2.5f, ROOF_Y - 2, CONC),   \
  RAMP(AL_RAMP, SX * 42, 0, 0, 12, ROOF_Y, 40, SX * 90),                                                               \
  DECO(AL_GLOW, SX * 64.1f, 0, 4, 0.1f, 0.4f, 500, 0, 0x9ad8ff)

static const arena_prim PRIMS[] = {
  DAM(1.0f, RED, 0), DAMFACE(1.0f),
  DAM(-1.0f, BLUE, 180), DAMFACE(-1.0f),
  SPILLWAY(1.0f),
  SPILLWAY(-1.0f),
  /* the island in the middle of the reservoir */
  MOUND(0, 0, 34, 2.5f, 26), POST(AL_COVER, -9, 4, 2.5f, 2.5f, 3, 0x6a7a5a), POST(AL_COVER, 10, -5, 2.5f, 2, 2.5f, 0x6a7a5a),
  /* wrecked cars and rubble on the reservoir floor */
  BOX(AL_COVER, -34, 120, 0, 4.5f, 1.8f, 7, 30, 0x8a3c2c), BOX(AL_COVER, 30, -110, 0, 4.5f, 1.8f, 7, -20, 0x3c5a8a),
  POST(AL_COVER, 36, 200, 0, 5, 2.2f, 0), POST(AL_COVER, -38, -205, 0, 5, 2.2f, 0),
};

/* the boost strips: along the bridge to each kicker, and down the reservoir's middle */
static const arena_boost BOOSTS[] = {
  { 0, 60, DAM_H, 8, 12, 180 }, { 0, -60, DAM_H, 8, 12, 0 }, { 0, 110, DAM_H, 8, 12, 180 }, { 0, -110, DAM_H, 8, 12, 0 },
  { -30, 40, 0, 8, 12, 180 }, { 30, -40, 0, 8, 12, 0 },
};

/* the pads at the far ends of the spillway tunnels: up onto the enemy crest beside their flag (one way) */
static const arena_tele TELES[] = {
  { 86, 280, 0, 2.8f, 40, 330, DAM_H, 270, 6, 0, BLUE },   /* blue's way in, under the red dam: up beside red's flag */
  { -86, -280, 0, 2.8f, -40, -330, DAM_H, 90, 6, 180, RED }, /* red's way in, under the blue dam */
};

static const float TEAM_SPAWNS[8][3] = {
  { -24, 330, 180 }, { 24, 330, 180 }, { -55, 336, 180 }, { 55, 336, 180 },
  { 24, -330, 0 }, { -24, -330, 0 }, { 55, -336, 0 }, { -55, -336, 0 },
};

static const float BOXES[][3] = {
  { 0, 4, 2.5f }, { -6, -6, 2.5f }, { 6, 6, 2.5f }, { -40, 60, 0 }, { 40, -60, 0 }, { 86, 150, ROOF_Y }, { -86, -150, ROOF_Y },
  { 86, -60, 0 }, { -86, 60, 0 }, { 0, 200, DAM_H }, { 0, -200, DAM_H }, { -60, 300, DAM_H }, { 60, -300, DAM_H },
  { 40, 250, 0 }, { -40, -250, 0 },
};

static const float ROUTE[][2] = {
  { 0, 330 }, { 0, 296 }, { 0, 30 }, { 0, -30 }, { 0, -296 }, { 0, -330 },
  { 86, 296 }, { 86, 240 }, { 86, 0 }, { 86, -240 }, { 86, -296 },
  { -86, 296 }, { -86, 240 }, { -86, 0 }, { -86, -240 }, { -86, -296 },
  { 14, 0 }, { -14, 0 }, { 0, 70 }, { 0, -70 },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 0, 6 }, { 6, 7 }, { 7, 8 }, { 8, 9 }, { 9, 10 }, { 10, 5 },
  { 0, 11 }, { 11, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 5 }, { 8, 16 }, { 13, 17 }, { 16, 18 }, { 16, 19 },
  { 17, 18 }, { 17, 19 },
};

static const arena_def ARENA = {
  .half_x = 110, .half_z = 370, .wall_h = 22.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = TEAM_SPAWNS, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { 0, 348 }, { 0, -348 } },
  .team_spawns = TEAM_SPAWNS,
  .teles = TELES, .nteles = sizeof TELES / sizeof TELES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_CTF,
  .look = {
    [AL_FLOOR] = { TEX_GROUND_MUD, { 0.5f, 0.52f, 0.44f }, 1.2f, 9.0f, 0.9f, 0 },
    [AL_WALL] = { TEX_GROUND_ROCK, { 0.6f, 0.56f, 0.5f }, 1.4f, 12.0f, 0.95f, 0 }, /* (the canyon's sides) */
    [AL_COVER] = { TEX_WALL_CONCRETE, { 0.76f, 0.74f, 0.7f }, 1.3f, 4.0f, 0.9f, 0 },
    [AL_DECK] = { TEX_ROAD_CONCRETE, { 0.8f, 0.78f, 0.74f }, 1.3f, 8.0f, 0.9f, 0 },
    [AL_RAMP] = { TEX_ROAD_ASPHALT, { 0.72f, 0.72f, 0.72f }, 1.3f, 6.0f, 0.85f, 0 },
    [AL_TRIM] = { TEX_WALL_CONCRETE, { 0.66f, 0.66f, 0.64f }, 1.3f, 2.0f, 0.9f, 0 },
    [AL_FEATURE] = { TEX_WALL_CONCRETE, { 0.82f, 0.8f, 0.76f }, 1.3f, 6.0f, 0.9f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.7f, 0.4f }, 1, 1, 1, 0, 3.0f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the canyon round it ──────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float flood_ground(float x, float z, void *ud) {
  (void)ud;
  float lz = z - ARENA_CZ, side = fmaxf(fabsf(x) - 113.0f, fabsf(lz) - 373.0f);
  if (side <= 0) return -0.05f;
  /* the canyon's sides up past the dams' crests to forested rims; beyond each dam the lake behind it (the ground under
   * the lake's water) */
  float h = fminf(side * 1.4f, 34.0f) + ground_noise(x, z, 80, 12, 3, 71) * fminf(side / 40.0f, 1.0f);
  if (fabsf(lz) > 373.0f && fabsf(x) < 200.0f) h = fminf(h, 8.0f + ground_noise(x, z, 60, 3, 2, 73));
  return h;
}

static void floodgate_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1400.0f;
  g.height = flood_ground;
  g.tex = TEX_GROUND_GRASS;
  g.tex_repeat = 10.0f;
  g.tint = pc_mul(hex(0x5a7040), 1.3f);
  g.tint2 = pc_mul(hex(0x7a7060), 1.3f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 79;
  ground_plane(scene, t, &g);
  /* the reservoir: a few hands of water over the canyon floor between the dams (and the lakes behind them, high) */
  water_opts w = water_defaults();
  w.color = rgb(0.18f, 0.42f, 0.4f);
  w.deep = rgb(0.03f, 0.14f, 0.14f);
  w.clarity = 0.55f;
  w.wave_height = 0.08f;
  w.wave_length = 14;
  w.foam = 0.4f;
  w.reflect = 0.7f;
  w.flow_z = 0.3f;
  w.refract = !split;
  water_surface(scene, 0, ARENA_CZ, 220.0f, 600.0f, 0.3f, &w);
  water_surface(scene, 0, ARENA_CZ + 560.0f, 420.0f, 380.0f, DAM_H - 1.0f, &w);
  water_surface(scene, 0, ARENA_CZ - 560.0f, 420.0f, 380.0f, DAM_H - 1.0f, &w);

  prop_set *ps = ps_new(scene, "FloodProps");
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, hex(0x9c9a8e), 1.0f);
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  for (int i = 0; i < (split ? 24 : 56); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = (hf(6100u + (uint32_t)i) * 2 - 1) * hz, x = side * (hx + 30 + 140 * hf(6110u + (uint32_t)i));
    ps_add(ps, i % 5 ? PK_PINE : PK_BOULDERS, (t3_vec3){ x, flood_ground(x, z + cz, NULL) - 0.2f, z + cz }, hf(6120u + (uint32_t)i) * 6.28f, 1.0f + 0.8f * hf(6130u + (uint32_t)i), 6140u + (uint32_t)i);
  }
  /* pylons marching over the canyon, and the dams' cranes */
  for (int i = 0; i < 4; i++) ps_add(ps, PK_PYLON, (t3_vec3){ i & 1 ? 190.0f : -190.0f, flood_ground(i & 1 ? 190.0f : -190.0f, cz + (i < 2 ? 120.0f : -160.0f), NULL), cz + (i < 2 ? 120.0f : -160.0f) }, 0, 1.6f, 6200u + (uint32_t)i);
  ps_add(ps, PK_CRANE, (t3_vec3){ 70, DAM_H, cz + 360 }, 3.1f, 1.0f, 6210u);
  ps_add(ps, PK_CRANE, (t3_vec3){ -70, DAM_H, cz - 360 }, 0.0f, 1.0f, 6211u);
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.1f : 0.2f);
}

const map_def map_floodgate = {
  .name = "Floodgate",
  .blurb = "Two dams across a flooded canyon: the broken bridge's jump, the spillway tunnels and their pads, or the reservoir.",
  .scene = "scenes/dam.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.8f, 0.78f, 0.74f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.74f, 0.72f, 0.68f },
            .wall_top = { 0.82f, 0.8f, 0.76f }, .road_tex = TEX_ROAD_CONCRETE, .wall_tex = TEX_WALL_CONCRETE, .road_rough = 0.9f },
  .laps = 1,
  .par_lap = 240,
  .scenery = floodgate_scenery,
  .arena = &ARENA,
  .music = 4,
};
