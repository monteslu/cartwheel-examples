/* capture the flag map: Spindle. An orbital foundry 660 m long hanging over a world, in low gravity (arena_def.gravity):
 * every jump goes a long way. A team's flag on the docking platform at each end. Three ways down its length:
 *
 *   the core rail   a straight enclosed rail down the station's spine, ten metres up: the fastest way, boost strips all
 *                   along it, and nowhere to dodge inside it
 *   the hull        broken plating either side, a little lower, in five pieces with a kicker at every break: long
 *                   floating jumps over the void (miss, and you are back at your dock)
 *   the furnace     the station's heart in the middle: a ring deck round four burning pillars that the rail runs over,
 *                   between the hull's middle pieces; ramps join hull and rail a third of the way along each half
 * Red docks to the north (+z), blue to the south; every red shape has its blue twin turned half round. A capture the flag
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

#define RED 0xff4a2c
#define BLUE 0x3d86ff
#define HULL 0xb8bec8
#define FIRE 0xff7a20
#define CORE_Y 10.0f
#define HULL_Y 4.0f

/* a piece of hull plating x from x0 to x1 (centre cx), z from z0 to z1, a kicker at each break end (ke: 1 at its +z end, -1
 * at its -z end, 0 none) and rails down its sides only (its ends are the jumps) */
#define PLATE(cx, z0, z1)                                                                                              \
  BOX(AL_DECK, cx, (z0 + z1) * 0.5f, HULL_Y - 2, 40, 2, z1 - z0, 0, HULL),                                            \
  BOX(AL_TRIM, cx - 19.7f, (z0 + z1) * 0.5f, HULL_Y, 0.6f, 1.3f, z1 - z0, 0, 0),                                       \
  BOX(AL_TRIM, cx + 19.7f, (z0 + z1) * 0.5f, HULL_Y, 0.6f, 1.3f, z1 - z0, 0, 0)
#define KICK(cx, z, yaw) RAMP(AL_RAMP, cx, z, HULL_Y, 16, 1.5f, 8, yaw)

/* one dock, S = 1 red (north), -1 blue (south), TC its team's colour, Y the yaw of its ramps rising towards it */
#define DOCK(S, TC, Y)                                                                                                 \
  DECK(0, S * 330, CORE_Y - 2, 150, 2, 60),                                                                            \
  /* the airlock arch behind the flag, the team's lamps on it */                                                       \
  BOX(AL_FEATURE, -14, S * 348, CORE_Y, 5, 10, 5, 0, HULL), BOX(AL_FEATURE, 14, S * 348, CORE_Y, 5, 10, 5, 0, HULL),     \
  BOX(AL_FEATURE, 0, S * 348, CORE_Y + 8, 33, 2.4f, 5, 0, HULL),                                                       \
  DECO(AL_GLOW, 0, S * (348 - 2.55f), CORE_Y + 8.6f, 18, 0.6f, 0.1f, 0, TC),                                           \
  GLOWPOST(-14, S * 348, CORE_Y + 10, 3.2f, 0.4f, TC), GLOWPOST(14, S * 348, CORE_Y + 10, 3.2f, 0.4f, TC),             \
  /* the hull's last pieces and the ramps up from them onto the dock */                                                \
  PLATE(-55, S > 0 ? 200 : -270, S > 0 ? 270 : -200), PLATE(55, S > 0 ? 200 : -270, S > 0 ? 270 : -200),               \
  RAMP(AL_RAMP, -55, S * 285, HULL_Y, 40, CORE_Y - HULL_Y, 30, Y), RAMP(AL_RAMP, 55, S * 285, HULL_Y, 40, CORE_Y - HULL_Y, 30, Y), \
  BOX(AL_WALL, -75.4f, S * 285, HULL_Y, 0.8f, 7.5f, 30, 0, HULL), BOX(AL_WALL, -34.6f, S * 285, HULL_Y, 0.8f, 7.5f, 30, 0, HULL), \
  BOX(AL_WALL, 75.4f, S * 285, HULL_Y, 0.8f, 7.5f, 30, 0, HULL), BOX(AL_WALL, 34.6f, S * 285, HULL_Y, 0.8f, 7.5f, 30, 0, HULL),   \
  KICK(-55, S * 204, Y + 180), KICK(55, S * 204, Y + 180),                                                             \
  /* the middle pieces of each half, kicked at both ends */                                                            \
  PLATE(-55, S > 0 ? 60 : -170, S > 0 ? 170 : -60), PLATE(55, S > 0 ? 60 : -170, S > 0 ? 170 : -60),                   \
  KICK(-55, S * 166, Y), KICK(55, S * 166, Y), KICK(-55, S * 64, Y + 180), KICK(55, S * 64, Y + 180),                   \
  /* the ramps between hull and rail, a third of the way along (from the hull's inner edge up to the rail's side) */    \
  RAMP(AL_RAMP, -22, S * 115, HULL_Y, 14, CORE_Y - HULL_Y, 28, 90), RAMP(AL_RAMP, 22, S * 115, HULL_Y, 14, CORE_Y - HULL_Y, 28, -90)

/* the rail's walls, in runs with a gap where a ramp joins it (z 100..130 each half) and over the furnace */
#define RAILWALL(SX)                                                                                                   \
  BOX(AL_WALL, SX * 8.6f, 215, CORE_Y, 1.2f, 4, 170, 0, HULL), BOX(AL_WALL, SX * 8.6f, -215, CORE_Y, 1.2f, 4, 170, 0, HULL), \
  BOX(AL_WALL, SX * 8.6f, 65, CORE_Y, 1.2f, 4, 70, 0, HULL), BOX(AL_WALL, SX * 8.6f, -65, CORE_Y, 1.2f, 4, 70, 0, HULL)

static const arena_prim PRIMS[] = {
  DOCK(1.0f, RED, 0),
  DOCK(-1.0f, BLUE, 180),
  /* the core rail down the spine, its walls, the light rings round it */
  BOX(AL_DECK, 0, 0, CORE_Y - 2, 16, 2, 600, 0, HULL), RAILWALL(1.0f), RAILWALL(-1.0f),
  DECO(AL_GLOW, 0, 250, CORE_Y + 6, 16, 0.3f, 0.4f, 0, 0x7ae0ff), DECO(AL_GLOW, 0, -250, CORE_Y + 6, 16, 0.3f, 0.4f, 0, 0x7ae0ff),
  DECO(AL_GLOW, 0, 170, CORE_Y + 6, 16, 0.3f, 0.4f, 0, 0x7ae0ff), DECO(AL_GLOW, 0, -170, CORE_Y + 6, 16, 0.3f, 0.4f, 0, 0x7ae0ff),
  DECO(AL_GLOW, 0, 40, CORE_Y + 6, 16, 0.3f, 0.4f, 0, 0x7ae0ff), DECO(AL_GLOW, 0, -40, CORE_Y + 6, 16, 0.3f, 0.4f, 0, 0x7ae0ff),
  /* the furnace: the ring deck under the rail, four pillars of fire round it, their glow */
  { AP_POST, AL_DECK, 0, 0, HULL_Y - 2, 76, 2, 76, 0, false, 0, 1.3f },
  POST(AL_FEATURE, -24, 24, HULL_Y, 6, 14, 0x4a4040), POST(AL_FEATURE, 24, 24, HULL_Y, 6, 14, 0x4a4040),
  POST(AL_FEATURE, -24, -24, HULL_Y, 6, 14, 0x4a4040), POST(AL_FEATURE, 24, -24, HULL_Y, 6, 14, 0x4a4040),
  GLOWPOST(-24, 24, HULL_Y + 14, 4.4f, 6, FIRE), GLOWPOST(24, 24, HULL_Y + 14, 4.4f, 6, FIRE),
  GLOWPOST(-24, -24, HULL_Y + 14, 4.4f, 6, FIRE), GLOWPOST(24, -24, HULL_Y + 14, 4.4f, 6, FIRE),
  GLOWPOST(0, 0, HULL_Y + 0.05f, 30, 0.1f, FIRE),
  /* the hull's middle pieces either side of the furnace, kicked at both ends */
  PLATE(-55, -30, 30), PLATE(55, -30, 30), KICK(-55, 26, 0), KICK(55, 26, 0), KICK(-55, -26, 180), KICK(55, -26, 180),
  /* the rail's supports down to the furnace deck */
  POST(AL_FEATURE, 0, 30, HULL_Y, 3, CORE_Y - HULL_Y - 2, HULL), POST(AL_FEATURE, 0, -30, HULL_Y, 3, CORE_Y - HULL_Y - 2, HULL),
};

static const arena_boost BOOSTS[] = {
  { 0, 200, CORE_Y, 8, 12, 180 }, { 0, -200, CORE_Y, 8, 12, 0 }, { 0, 20, CORE_Y, 8, 12, 180 }, { 0, -20, CORE_Y, 8, 12, 0 },
  { -55, 230, HULL_Y, 8, 12, 180 }, { 55, -230, HULL_Y, 8, 12, 0 }, { 55, 120, HULL_Y, 8, 12, 180 }, { -55, -120, HULL_Y, 8, 12, 0 },
};

static const float TEAM_SPAWNS[8][3] = {
  { -24, 322, 180 }, { 24, 322, 180 }, { -50, 330, 180 }, { 50, 330, 180 },
  { 24, -322, 0 }, { -24, -322, 0 }, { 50, -330, 0 }, { -50, -330, 0 },
};

static const float BOXES[][3] = {
  { 0, 0, CORE_Y }, { 0, 120, CORE_Y }, { 0, -120, CORE_Y }, { -20, 10, HULL_Y }, { 20, -10, HULL_Y },
  { -55, 110, HULL_Y }, { 55, -110, HULL_Y }, { 55, 235, HULL_Y }, { -55, -235, HULL_Y }, { -40, 315, CORE_Y }, { 40, -315, CORE_Y },
};

static const float ROUTE[][2] = {
  { 0, 320 }, { 0, 296 }, { 0, 115 }, { 0, -115 }, { 0, -296 }, { 0, -320 },
  { 55, 296 }, { 55, 235 }, { 55, 115 }, { 55, -115 }, { 55, -235 }, { 55, -296 },
  { -55, 296 }, { -55, 235 }, { -55, 115 }, { -55, -115 }, { -55, -235 }, { -55, -296 },
  { 30, 0 }, { -30, 0 },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 0, 6 }, { 6, 7 }, { 7, 8 }, { 8, 18 }, { 18, 9 }, { 9, 10 }, { 10, 11 },
  { 11, 5 }, { 0, 12 }, { 12, 13 }, { 13, 14 }, { 14, 19 }, { 19, 15 }, { 15, 16 }, { 16, 17 }, { 17, 5 }, { 2, 8 }, { 2, 14 },
  { 3, 9 }, { 3, 15 },
};

static const arena_def ARENA = {
  .half_x = 100, .half_z = 365, .open = true, .fence = 3.0f, .gravity = 0.5f, /* (a kart bounces higher off a rail up here) */
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = TEAM_SPAWNS, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { 0, 335 }, { 0, -335 } },
  .team_spawns = TEAM_SPAWNS,
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_CTF,
  .look = {
    [AL_FLOOR] = { TEX_ROAD_TECH, { 0.6f, 0.62f, 0.66f }, 1.3f, 6.0f, 0.5f, 0.6f },
    [AL_WALL] = { TEX_WALL_TECH, { 0.72f, 0.74f, 0.8f }, 1.3f, 6.0f, 0.5f, 0.6f },
    [AL_COVER] = { TEX_METAL, { 0.72f, 0.74f, 0.78f }, 1.3f, 4.0f, 0.5f, 0.6f },
    [AL_DECK] = { TEX_ROAD_TECH, { 0.7f, 0.72f, 0.78f }, 1.35f, 6.0f, 0.5f, 0.6f },
    [AL_RAMP] = { TEX_ROAD_METAL, { 0.74f, 0.76f, 0.8f }, 1.35f, 6.0f, 0.5f, 0.6f },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.6f, 0.3f },
    [AL_FEATURE] = { TEX_WALL_TECH, { 0.76f, 0.78f, 0.84f }, 1.3f, 5.0f, 0.5f, 0.6f },
    [AL_GLOW] = { -1, { 1.0f, 0.7f, 0.4f }, 1, 1, 1, 0, 3.5f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

static void spindle_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  (void)t;
  arena_gfx_build(scene, a, seed);
  space_sky_build(scene);
  /* the foundry's bulk: the station's tall gear along the docks and the solar wings off the sides */
  prop_set *ps = ps_new(scene, "StationProps");
  float cz = ARENA_CZ;
  for (int i = 0; i < 6; i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = (i < 2 ? 330.0f : i < 4 ? 0.0f : -330.0f) + cz;
    ps_add(ps, PK_SOLAR_PANEL, (t3_vec3){ side * 130.0f, CORE_Y, z }, side > 0 ? 1.57f : -1.57f, 3.0f, 7100u + (uint32_t)i);
    ps_add(ps, PK_STATION_MODULE, (t3_vec3){ side * 95.0f, CORE_Y - 12.0f, z + 40.0f }, 0.0f, 1.6f, 7110u + (uint32_t)i);
  }
  ps_add(ps, PK_DISH, (t3_vec3){ 60, CORE_Y, cz + 352 }, 3.1f, 1.2f, 7120u);
  ps_add(ps, PK_DISH, (t3_vec3){ -60, CORE_Y, cz - 352 }, 0.0f, 1.2f, 7121u);
  ps_add(ps, PK_ANTENNA, (t3_vec3){ -60, CORE_Y, cz + 352 }, 0.0f, 1.4f, 7122u);
  ps_add(ps, PK_ANTENNA, (t3_vec3){ 60, CORE_Y, cz - 352 }, 0.0f, 1.4f, 7123u);
  ps_finish(ps);
}

const map_def map_spindle = {
  .name = "Spindle",
  .blurb = "An orbital foundry in low gravity: the fast core rail, the broken hull's floating jumps, the furnace in the middle.",
  .scene = "scenes/spires.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.7f, 0.72f, 0.78f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.72f, 0.74f, 0.8f },
            .wall_top = { 0.8f, 0.82f, 0.86f }, .road_tex = TEX_ROAD_TECH, .wall_tex = TEX_WALL_TECH, .road_rough = 0.6f },
  .laps = 1,
  .par_lap = 240,
  .scenery = spindle_scenery,
  .arena = &ARENA,
  .music = 7,
};
