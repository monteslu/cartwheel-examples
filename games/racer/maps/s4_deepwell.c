/* assault map: Deepwell. An offshore rig at dawn, steel decks on legs over a grey sea; nothing round them but water (an
 * open arena: off the edge is a swim, and back to the spawn):
 *
 *   the gangway    the attackers' supply ship moored to the south, a long gangway to the rig, a boom gate across its
 *                  end: blow it
 *   the pumps      four pump decks round the central platform, north, south, east and west, bridged to it and to each
 *                  other on the diagonals: destroy all four, in any order (the defenders cannot hold them all)
 *   the wellhead   the drilling tower in the middle, its ramp shuttered until the pumps are dead: destroy the wellhead
 *                  on top
 * An assault map only (arena_def.modes). */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
/* a deck (top at y 6, 2 m thick, railed) and a bridge (the same at any heading) */
#define DECK(x, z, w, d) { AP_BOX, AL_DECK, x, z, 4, w, 2, d, 0, false, 0, 1.3f }
#define BRIDGE(x, z, w, d, yaw) { AP_BOX, AL_DECK, x, z, 4, w, 2, d, yaw, false, 0, 1.3f }
/* the legs under a deck: four columns down into the sea (drawn only) */
#define LEGS(x, z, hx, hz)                                                                                             \
  DECO(AL_FEATURE, x - hx, z - hz, -12, 2.2f, 16, 2.2f, 0, RUST), DECO(AL_FEATURE, x + hx, z - hz, -12, 2.2f, 16, 2.2f, 0, RUST), \
  DECO(AL_FEATURE, x - hx, z + hz, -12, 2.2f, 16, 2.2f, 0, RUST), DECO(AL_FEATURE, x + hx, z + hz, -12, 2.2f, 16, 2.2f, 0, RUST)
#define GBOX(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, false, col, 0, g }
#define GDECO(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, true, col, 0, g }
#define GPOST(look, x, z, y, dia, h, col, g) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col, 0, g }
#define GGLOWPOST(x, z, y, dia, h, col, g) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col, 0, g }

#define STEEL 0x5c6670
#define RUST 0x8a5034
#define YELLOW 0xe8c030
#define RED 0xff3a20
#define CYAN 0x50e0ff
#define ORANGE 0xff8a20

enum { O_BOOM, O_NEST_S1, O_NEST_S2, O_PUMP_N, O_PUMP_S, O_PUMP_E, O_PUMP_W, O_NEST_P1, O_NEST_P2, O_WELL, O_NEST_T1, O_NEST_T2 };

#define NEST(o, x, z, y)                                                                                               \
  GPOST(AL_COVER, x, z, y, 4.6f, 1.1f, YELLOW, GATE_OBJ(o)), GPOST(AL_FEATURE, x, z, y + 1.1f, 1.7f, 1.3f, STEEL, GATE_OBJ(o)), \
  GDECO(AL_FEATURE, x, z - 1.6f, y + 1.8f, 0.5f, 0.5f, 3.0f, STEEL, GATE_OBJ(o)), GGLOWPOST(x, z, y + 2.4f, 0.5f, 0.5f, RED, GATE_OBJ(o))

/* a pump: the housing, its motor and the lit gauge, gone when destroyed */
#define PUMP(o, x, z)                                                                                                  \
  GPOST(AL_FEATURE, x, z, 6, 6, 4.5f, ORANGE, GATE_OBJ(o)), GBOX(AL_FEATURE, x, z, 10.5f, 4, 1.4f, 4, STEEL, GATE_OBJ(o)),  \
  GGLOWPOST(x, z, 6.1f, 7.4f, 0.25f, CYAN, GATE_OBJ(o)), GDECO(AL_GLOW, x, z - 3.05f, 8, 1.6f, 1.0f, 0.1f, CYAN, GATE_OBJ(o))

static const arena_prim PRIMS[] = {
  /* the supply ship (its deck, the bridge house, crates) and the gangway with the boom gate at its end */
  DECK(0, -265, 80, 70), LEGS(0, -265, 34, 28),
  BOX(AL_FEATURE, 0, -292, 6, 30, 7, 12, 0, 0xd8d4cc), DECO(AL_GLOW, 0, -285.9f, 10, 22, 1.2f, 0.1f, 0, 0xffe0a0),
  BOX(AL_COVER, -26, -250, 6, 6, 2.5f, 6, 15, 0x3c6a9a), BOX(AL_COVER, 28, -246, 6, 6, 2.5f, 6, -20, 0x9a3c2c),
  BRIDGE(0, -185, 14, 90, 0),
  GBOX(AL_FEATURE, 0, -150, 6, 14, 4, 1.4f, YELLOW, GATE_OBJ(O_BOOM)), GDECO(AL_TRIM, 0, -150.8f, 8.8f, 14, 0.8f, 0.1f, 0, GATE_OBJ(O_BOOM)),
  GGLOWPOST(-6, -151, 10, 0.6f, 0.6f, RED, GATE_OBJ(O_BOOM)), GGLOWPOST(6, -151, 10, 0.6f, 0.6f, RED, GATE_OBJ(O_BOOM)),
  /* the central platform and its legs, the four pump decks and theirs, the bridges out and round */
  DECK(0, 0, 80, 80), LEGS(0, 0, 34, 34),
  DECK(0, 120, 40, 40), LEGS(0, 120, 16, 16), DECK(0, -120, 40, 40), LEGS(0, -120, 16, 16),
  DECK(100, 0, 40, 40), LEGS(100, 0, 16, 16), DECK(-100, 0, 40, 40), LEGS(-100, 0, 16, 16),
  BRIDGE(0, 70, 14, 60, 0), BRIDGE(0, -70, 14, 60, 0), BRIDGE(60, 0, 40, 14, 0), BRIDGE(-60, 0, 40, 14, 0),
  BRIDGE(50, 60, 10, 100, 143.13f), BRIDGE(-50, 60, 10, 100, -143.13f), BRIDGE(50, -60, 10, 100, 36.87f), BRIDGE(-50, -60, 10, 100, -36.87f),
  PUMP(O_PUMP_N, 0, 125), PUMP(O_PUMP_S, 0, -125), PUMP(O_PUMP_E, 105, 0), PUMP(O_PUMP_W, -105, 0),
  /* gear on the decks: pipe racks, drums, containers */
  BOX(AL_COVER, -12, 112, 6, 3, 2, 10, 0, STEEL), BOX(AL_COVER, 112, -10, 6, 10, 2, 3, 0, STEEL), BOX(AL_COVER, -112, 12, 6, 10, 2, 3, 0, STEEL),
  POST(AL_COVER, 14, -112, 6, 2.4f, 2.2f, RUST), POST(AL_COVER, 18, -108, 6, 2.4f, 2.2f, RUST),
  NEST(O_NEST_S1, -12, -132, 6), NEST(O_NEST_S2, 12, -132, 6),
  NEST(O_NEST_P1, 112, 12, 6), NEST(O_NEST_P2, -12, 132, 6),
  /* the drilling tower: its deck in the middle, the ramp up its west side (shuttered until the pumps die), the wellhead,
   * the derrick's legs and the flare boom */
  BOX(AL_DECK, 0, 12, 6, 30, 6, 30, 0, 0),
  RAMP(AL_RAMP, -27, 18, 6, 12, 6, 24, 90),
  GBOX(AL_FEATURE, -39.4f, 18, 6, 1.2f, 5, 12, YELLOW, GATE_STAGE(1)), GDECO(AL_TRIM, -40.1f, 18, 9, 0.1f, 0.6f, 12, 0, GATE_STAGE(1)),
  GPOST(AL_FEATURE, 0, 16, 12, 4.4f, 3.2f, STEEL, GATE_OBJ(O_WELL)), GPOST(AL_GLOW, 0, 16, 15.2f, 3.0f, 1.0f, ORANGE, GATE_OBJ(O_WELL)),
  GGLOWPOST(0, 16, 12.1f, 6, 0.2f, ORANGE, GATE_OBJ(O_WELL)),
  DECO(AL_FEATURE, -13, 25, 12, 1.2f, 30, 1.2f, 0, RUST), DECO(AL_FEATURE, 13, 25, 12, 1.2f, 30, 1.2f, 0, RUST),
  DECO(AL_FEATURE, -13, -1, 12, 1.2f, 30, 1.2f, 0, RUST), DECO(AL_FEATURE, 13, -1, 12, 1.2f, 30, 1.2f, 0, RUST),
  DECO(AL_FEATURE, 0, 12, 41, 28, 1.2f, 28, 0, RUST), DECO(AL_GLOW, 0, 12, 42.2f, 1.2f, 1.2f, 1.2f, 0, RED),
  NEST(O_NEST_T1, -30, 34, 6), NEST(O_NEST_T2, 30, 25, 6),
  /* the edges of every deck painted (the drop is a long one) */
  DECO(AL_PAINT, 0, -39.4f, 6.02f, 80, 0.02f, 0.8f, 0, YELLOW), DECO(AL_PAINT, 0, 39.4f, 6.02f, 80, 0.02f, 0.8f, 0, YELLOW),
};

static const assault_obj OBJS[] = {
  [O_BOOM] = { AO_DESTROY, 0, 0, -150, 6, 6.0f, 4.0f, 45, "THE BOOM GATE" },
  [O_NEST_S1] = { AO_TURRET, 0, -12, -132, 6, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_NEST_S2] = { AO_TURRET, 0, 12, -132, 6, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_PUMP_N] = { AO_DESTROY, 1, 0, 125, 6, 3.4f, 5.0f, 40, "THE NORTH PUMP" },
  [O_PUMP_S] = { AO_DESTROY, 1, 0, -125, 6, 3.4f, 5.0f, 40, "THE SOUTH PUMP" },
  [O_PUMP_E] = { AO_DESTROY, 1, 105, 0, 6, 3.4f, 5.0f, 40, "THE EAST PUMP" },
  [O_PUMP_W] = { AO_DESTROY, 1, -105, 0, 6, 3.4f, 5.0f, 40, "THE WEST PUMP" },
  [O_NEST_P1] = { AO_TURRET, 1, 112, 12, 6, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_NEST_P2] = { AO_TURRET, 1, -12, 132, 6, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_WELL] = { AO_DESTROY, 2, 0, 16, 12, 2.6f, 4.5f, 80, "THE WELLHEAD" },
  [O_NEST_T1] = { AO_TURRET, 2, -30, 34, 6, 2.4f, 3.0f, 40, "A GUN NEST", 60 },
  [O_NEST_T2] = { AO_TURRET, 2, 30, 25, 6, 2.4f, 3.0f, 40, "A GUN NEST", 60 },
};

static const float ATT[3 * 4][3] = {
  { -20, -272, 0 }, { 20, -272, 0 }, { -24, -256, 0 }, { 24, -256, 0 },
  { -3, -168, 0 }, { 3, -168, 0 }, { -3, -180, 0 }, { 3, -180, 0 },
  { -24, -30, 0 }, { 24, -30, 0 }, { -32, -20, 0 }, { 32, -20, 0 },
};
static const float DEF[3 * 4][3] = {
  { -10, -118, 180 }, { 10, -118, 180 }, { -14, -104, 180 }, { 14, -104, 180 },
  { -30, -30, 180 }, { 30, 30, 0 }, { 30, -30, 180 }, { -30, 30, 0 },
  { -25, -14, 180 }, { 25, 32, 180 }, { -12, 34, 180 }, { 33, 14, 180 },
};

static const assault_def ASSAULT = {
  .time_s = 360,
  .objs = OBJS, .nobjs = sizeof OBJS / sizeof OBJS[0],
  .nstages = 3,
  .need = { 1, 4, 1 },
  .stage_name = { "BLOW THE BOOM GATE", "DESTROY ALL FOUR PUMPS", "DESTROY THE WELLHEAD" },
  .att_spawns = ATT, .def_spawns = DEF,
};

static const float BOXES[][3] = {
  { -20, -240, 6 }, { 20, -240, 6 }, { 0, -200, 6 }, { 0, -170, 6 },
  { -10, -110, 6 }, { 10, 110, 6 }, { 110, 10, 6 }, { -110, -10, 6 },
  { -30, 0, 6 }, { 30, 0, 6 }, { 0, -32, 6 }, { 50, 60, 6 }, { -50, 60, 6 }, { 50, -60, 6 }, { -50, -60, 6 }, { 0, 20, 12 },
};

static const arena_boost BOOSTS[] = { { 0, -205, 6, 8, 12, 0 }, { 0, 70, 6, 8, 12, 0 }, { 60, 0, 6, 8, 12, 90 }, { -60, 0, 6, 8, 12, -90 } };

static const float ROUTE[][2] = {
  { 0, -262 }, { 0, -225 }, { 0, -160 }, { 0, -120 }, { 0, -75 }, { 0, -35 }, { -30, 0 }, { 30, 0 }, { 0, 40 }, { 0, 75 },
  { 0, 115 }, { 60, 0 }, { 95, 0 }, { -60, 0 }, { -95, 0 }, { 50, 60 }, { -50, 60 }, { 50, -60 }, { -50, -60 }, { -38, 18 }, { -8, 18 },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 5, 7 }, { 6, 8 }, { 7, 8 }, { 8, 9 }, { 9, 10 }, { 7, 11 }, { 11, 12 },
  { 6, 13 }, { 13, 14 }, { 10, 15 }, { 15, 12 }, { 10, 16 }, { 16, 14 }, { 3, 17 }, { 17, 12 }, { 3, 18 }, { 18, 14 }, { 5, 19 }, { 19, 20 },
};

static const arena_def ARENA = {
  .half_x = 130, .half_z = 310, .open = true, .fence = 2.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = ATT, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_ASSAULT,
  .assault = &ASSAULT,
  .look = {
    [AL_FLOOR] = { TEX_ROAD_METAL, { 0.6f, 0.62f, 0.64f }, 1.3f, 6.0f, 0.6f, 0.6f },
    [AL_WALL] = { TEX_WALL_RUST, { 0.7f, 0.6f, 0.55f }, 1.3f, 6.0f, 0.7f, 0.5f },
    [AL_COVER] = { TEX_METAL, { 0.72f, 0.72f, 0.72f }, 1.3f, 4.0f, 0.5f, 0.6f },
    [AL_DECK] = { TEX_ROAD_METAL, { 0.62f, 0.64f, 0.66f }, 1.35f, 6.0f, 0.6f, 0.6f },
    [AL_RAMP] = { TEX_ROAD_METAL, { 0.7f, 0.7f, 0.66f }, 1.35f, 6.0f, 0.6f, 0.6f },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.6f, 0.3f },
    [AL_FEATURE] = { TEX_METAL, { 0.8f, 0.78f, 0.74f }, 1.3f, 5.0f, 0.5f, 0.6f },
    [AL_GLOW] = { -1, { 1.0f, 0.7f, 0.4f }, 1, 1, 1, 0, 3.5f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the sea round it ─────────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static void deepwell_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  (void)t;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  /* the sea, out to the horizon: deep, choppy, whitecaps */
  water_opts w = water_defaults();
  w.color = rgb(0.1f, 0.3f, 0.36f);
  w.deep = rgb(0.01f, 0.07f, 0.1f);
  w.clarity = 0.25f;
  w.wave_height = 0.7f;
  w.wave_length = 26;
  w.wave_dir = 60;
  w.choppy = 0.6f;
  w.foam = 0.9f;
  w.reflect = 0.65f;
  w.refract = !split;
  w.horizon = 3000.0f;
  water_surface(scene, 0, ARENA_CZ, 2600.0f, 2600.0f, -2.0f, &w);

  prop_set *ps = ps_new(scene, "RigProps");
  float cz = ARENA_CZ;
  /* the rig's tall gear: the flare stack, cranes on the pump decks, tanks, and other rigs far off */
  ps_add(ps, PK_FLARE_STACK, (t3_vec3){ 60, 6, cz + 80 }, 0.0f, 1.6f, 5101u);
  ps_add(ps, PK_CRANE, (t3_vec3){ 112, 6, cz + 14 }, 2.6f, 0.9f, 5102u);
  ps_add(ps, PK_CRANE, (t3_vec3){ -112, 6, cz - 14 }, -0.6f, 0.9f, 5103u);
  ps_add(ps, PK_TANK, (t3_vec3){ 14, 6, cz + 112 }, 0.0f, 0.7f, 5104u);
  ps_add(ps, PK_PIPE_RACK, (t3_vec3){ -14, 6, cz - 112 }, 0.0f, 0.8f, 5105u);
  for (int i = 0; i < (split ? 4 : 8); i++) {
    float ang = hf(5200u + (uint32_t)i) * 6.2831853f, r = 900.0f + 700.0f * hf(5210u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i & 1 ? PK_FLARE_STACK : PK_COOLING_TOWER, (t3_vec3){ x, -2, z }, hf(5220u + (uint32_t)i) * 6.28f, 2.0f + 1.5f * hf(5230u + (uint32_t)i), 5240u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.1f : 0.2f);
}

const map_def map_deepwell = {
  .name = "Deepwell",
  .blurb = "An oil rig over open sea: blow the boom gate, destroy all four pumps in any order, then the wellhead.",
  .scene = "scenes/deepwell.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.62f, 0.64f, 0.66f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.7f, 0.6f, 0.55f },
            .wall_top = { 0.78f, 0.7f, 0.64f }, .road_tex = TEX_ROAD_METAL, .wall_tex = TEX_WALL_RUST, .road_rough = 0.6f },
  .laps = 1,
  .par_lap = 360,
  .scenery = deepwell_scenery,
  .arena = &ARENA,
  .music = 7,
};
