/* assault map: Breakwater. A coastal fortress on a misty morning: the attackers come ashore on a long beach and fight up to
 * the coil cannon on the cliff top. 660 m from the surf to the bunker:
 *
 *   the beach      landing craft run aground in the surf, hedgehogs, three concrete pillboxes, and behind them a line of
 *                  tank traps with a lane through it behind each pillbox (a pillbox down opens its lane). Any two of the
 *                  three take the beach
 *   the seawall    seven metres of concrete right across the bay, one steel gate in the middle and two gun nests in front
 *                  of it: blow the gate
 *   the cliff      three terraces stacked up the cliff, a ramp up each at alternate ends (a switchback): reach the top
 *   the cannon     a walled bunker on the cliff top, its door open once the heights are taken: destroy the cannon's core
 * The defenders start in front of the seawall and fall back up the cliff stage by stage; gun nests on the terraces fire
 * down at whoever is coming up. An assault map only (arena_def.modes). */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define TIER(z, y, d) { AP_BOX, AL_DECK, 0, z, y, 200, 6, d, 0, false, 0, 1.3f }
#define GBOX(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, false, col, 0, g }
#define GDECO(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, true, col, 0, g }
#define GPOST(look, x, z, y, dia, h, col, g) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col, 0, g }
#define GGLOWPOST(x, z, y, dia, h, col, g) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col, 0, g }

#define CONC 0x9c978c
#define STEEL 0x5c6670
#define SANDBAG 0xb59a6a
#define RUST 0x7a4a30
#define RED 0xff3a20
#define CORE 0x58d8ff

/* the objectives (their order is the list below) */
enum { O_PILL_W, O_PILL_C, O_PILL_E, O_GATE, O_NEST_W, O_NEST_E, O_HEIGHTS, O_NEST_T2, O_CORE, O_NEST_T3W, O_NEST_T3E };

/* a pillbox: a low concrete block with a lit firing slit and a roof slab, all of it gone when it is destroyed */
#define PILLBOX(o, x, z)                                                                                               \
  GBOX(AL_FEATURE, x, z, 0, 10, 3.8f, 8, CONC, GATE_OBJ(o)), GBOX(AL_FEATURE, x, z, 3.8f, 11.5f, 0.7f, 9.5f, CONC, GATE_OBJ(o)), \
  GDECO(AL_GLOW, x, z - 4.05f, 2.0f, 6, 0.45f, 0.1f, 0xffb050, GATE_OBJ(o))

/* a gun nest: a sandbag ring, the gun on its pedestal and a red lamp over it */
#define NEST(o, x, z, y)                                                                                               \
  GPOST(AL_COVER, x, z, y, 4.6f, 1.1f, SANDBAG, GATE_OBJ(o)), GPOST(AL_FEATURE, x, z, y + 1.1f, 1.7f, 1.3f, STEEL, GATE_OBJ(o)), \
  GDECO(AL_FEATURE, x, z - 1.6f, y + 1.8f, 0.5f, 0.5f, 3.0f, STEEL, GATE_OBJ(o)), GGLOWPOST(x, z, y + 2.4f, 0.5f, 0.5f, RED, GATE_OBJ(o))

static const arena_prim PRIMS[] = {
  /* the surf line: foam on the wet sand */
  DECO(AL_PAINT, 0, -318, 0, 200, 0.03f, 4, 0, 0xd8e4e4), DECO(AL_PAINT, 0, -300, 0, 200, 0.03f, 2, 0, 0xc4d2d2),
  DECO(AL_PAINT, 0, -309, 0, 200, 0.02f, 14, 0, 0x6a7d7c),
  /* two landing craft run aground, their ramps down (cover on the beach) */
  BOX(AL_COVER, -62, -290, 0, 11, 3.2f, 18, 0, STEEL), RAMP(AL_RAMP, -62, -276, 0, 9, 1.2f, 10, 180),
  BOX(AL_COVER, 64, -296, 0, 11, 3.2f, 18, 0, STEEL), RAMP(AL_RAMP, 64, -282, 0, 9, 1.2f, 10, 180),
  /* hedgehogs and a wreck up the beach */
  POST(AL_COVER, -30, -250, 0, 2.2f, 1.6f, RUST), POST(AL_COVER, 18, -244, 0, 2.2f, 1.6f, RUST), POST(AL_COVER, 44, -222, 0, 2.2f, 1.6f, RUST),
  POST(AL_COVER, -80, -230, 0, 2.2f, 1.6f, RUST), POST(AL_COVER, 82, -238, 0, 2.2f, 1.6f, RUST), POST(AL_COVER, -14, -212, 0, 2.2f, 1.6f, RUST),
  POST(AL_COVER, -44, -200, 0, 2.2f, 1.6f, RUST), POST(AL_COVER, 26, -196, 0, 2.2f, 1.6f, RUST), POST(AL_COVER, 78, -196, 0, 2.2f, 1.6f, RUST),
  BOX(AL_COVER, -84, -180, 0, 6, 2.2f, 9, 18, RUST),
  /* a dune ridge the full width, low enough to drive over (a kicker at speed) */
  RAMP(AL_RAMP, -60, -265, 0, 40, 1.0f, 8, 0), RAMP(AL_RAMP, -60, -257, 0, 40, 1.0f, 8, 180),
  RAMP(AL_RAMP, 60, -265, 0, 40, 1.0f, 8, 0), RAMP(AL_RAMP, 60, -257, 0, 40, 1.0f, 8, 180),
  /* the pillboxes */
  PILLBOX(O_PILL_W, -55, -160), PILLBOX(O_PILL_C, 0, -175), PILLBOX(O_PILL_E, 55, -160),
  /* the tank-trap line, a lane behind each pillbox closed by a steel barrier until that pillbox is down */
  BOX(AL_COVER, -81.5f, -115, 0, 37, 2.4f, 3, 0, CONC), BOX(AL_COVER, -27.5f, -115, 0, 39, 2.4f, 3, 0, CONC),
  BOX(AL_COVER, 27.5f, -115, 0, 39, 2.4f, 3, 0, CONC), BOX(AL_COVER, 81.5f, -115, 0, 37, 2.4f, 3, 0, CONC),
  GBOX(AL_COVER, -55, -115, 0, 16, 2.2f, 2, STEEL, GATE_OBJ(O_PILL_W)), GBOX(AL_COVER, 0, -115, 0, 16, 2.2f, 2, STEEL, GATE_OBJ(O_PILL_C)),
  GBOX(AL_COVER, 55, -115, 0, 16, 2.2f, 2, STEEL, GATE_OBJ(O_PILL_E)),
  GDECO(AL_TRIM, -55, -116.05f, 1.3f, 16, 0.5f, 0.1f, 0, GATE_OBJ(O_PILL_W)), GDECO(AL_TRIM, 0, -116.05f, 1.3f, 16, 0.5f, 0.1f, 0, GATE_OBJ(O_PILL_C)),
  GDECO(AL_TRIM, 55, -116.05f, 1.3f, 16, 0.5f, 0.1f, 0, GATE_OBJ(O_PILL_E)),
  /* the seawall, its gate, the lamps over the gate */
  BOX(AL_WALL, -54.5f, -50, 0, 91, 7, 5, 0, 0), BOX(AL_WALL, 54.5f, -50, 0, 91, 7, 5, 0, 0),
  GBOX(AL_FEATURE, 0, -50, 0, 18, 7, 3, STEEL, GATE_OBJ(O_GATE)), GDECO(AL_GLOW, 0, -51.6f, 6.2f, 14, 0.4f, 0.1f, 0xffb050, GATE_OBJ(O_GATE)),
  DECO(AL_FEATURE, -11, -50, 7, 4, 3, 6, 0, CONC), DECO(AL_FEATURE, 11, -50, 7, 4, 3, 6, 0, CONC),
  NEST(O_NEST_W, -40, -72, 0), NEST(O_NEST_E, 40, -72, 0),
  /* the yard behind the seawall: sheds and containers */
  BOX(AL_COVER, -72, -10, 0, 18, 5, 10, 0, 0x6b7a5a), BOX(AL_COVER, 30, 8, 0, 12, 2.6f, 5, 0, 0x4f6a8a), BOX(AL_COVER, -24, 20, 0, 12, 2.6f, 5, 90, 0x8a5a3a),
  BOX(AL_COVER, 78, -18, 0, 6, 2.6f, 12, 0, 0x7a6a4a), POST(AL_COVER, 0, -14, 0, 3, 1.2f, SANDBAG), POST(AL_COVER, -40, 40, 0, 3, 1.2f, SANDBAG),
  /* the cliff: three terraces, a ramp up each at alternate ends */
  TIER(200, 0, 260), TIER(220, 6, 220), TIER(240, 12, 180),
  RAMP(AL_RAMP, 70, 55, 0, 16, 6, 30, 0), RAMP(AL_RAMP, -70, 95, 6, 16, 6, 30, 0), RAMP(AL_RAMP, 70, 135, 12, 16, 6, 30, 0),
  NEST(O_NEST_T2, -30, 130, 12),
  POST(AL_COVER, 10, 92, 6, 3, 1.2f, SANDBAG), POST(AL_COVER, -20, 132, 12, 3, 1.2f, SANDBAG),
  /* the cannon bunker on the top: its walls, the door that opens once the heights are taken, the core and the gun */
  BOX(AL_WALL, -25.5f, 256, 18, 31, 6, 2, 0, 0), BOX(AL_WALL, 25.5f, 256, 18, 31, 6, 2, 0, 0),
  BOX(AL_WALL, -40, 289, 18, 2, 6, 64, 0, 0), BOX(AL_WALL, 40, 289, 18, 2, 6, 64, 0, 0), BOX(AL_WALL, 0, 320, 18, 82, 6, 2, 0, 0),
  GBOX(AL_FEATURE, 0, 256, 18, 20, 6, 2, STEEL, GATE_STAGE(2)), GDECO(AL_TRIM, 0, 254.9f, 21, 20, 0.6f, 0.1f, 0, GATE_STAGE(2)),
  GPOST(AL_FEATURE, 0, 292, 18, 7, 5, STEEL, GATE_OBJ(O_CORE)), GPOST(AL_GLOW, 0, 292, 23, 4.5f, 2.2f, CORE, GATE_OBJ(O_CORE)),
  GGLOWPOST(0, 292, 18.2f, 9, 0.25f, CORE, GATE_OBJ(O_CORE)),
  DECO(AL_FEATURE, 0, 292, 25.2f, 10, 3, 10, 0, STEEL), DECO(AL_FEATURE, 0, 262, 26.5f, 3.2f, 3.2f, 52, 0, STEEL),
  DECO(AL_FEATURE, 0, 238, 26.5f, 4.2f, 4.2f, 4, 0, 0x3c4248), DECO(AL_GLOW, 0, 235.9f, 27.6f, 2.2f, 2.2f, 0.1f, 0, CORE),
  NEST(O_NEST_T3W, -55, 205, 18), NEST(O_NEST_T3E, 55, 225, 18),
  POST(AL_COVER, -20, 190, 18, 3, 1.2f, SANDBAG), POST(AL_COVER, 28, 205, 18, 3, 1.2f, SANDBAG),
};

static const assault_obj OBJS[] = {
  [O_PILL_W] = { AO_DESTROY, 0, -55, -160, 0, 5.5f, 4.5f, 45, "THE WEST PILLBOX" },
  [O_PILL_C] = { AO_DESTROY, 0, 0, -175, 0, 5.5f, 4.5f, 45, "THE MIDDLE PILLBOX" },
  [O_PILL_E] = { AO_DESTROY, 0, 55, -160, 0, 5.5f, 4.5f, 45, "THE EAST PILLBOX" },
  [O_GATE] = { AO_DESTROY, 1, 0, -50, 0, 7.0f, 7.0f, 90, "THE SEAWALL GATE" },
  [O_NEST_W] = { AO_TURRET, 1, -40, -72, 0, 2.4f, 3.0f, 40, "A GUN NEST", 75 },
  [O_NEST_E] = { AO_TURRET, 1, 40, -72, 0, 2.4f, 3.0f, 40, "A GUN NEST", 75 },
  [O_HEIGHTS] = { AO_REACH, 2, 70, 163, 18, 9.0f, 0, 0, "THE CLIFF TOP" },
  [O_NEST_T2] = { AO_TURRET, 2, -30, 130, 12, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_CORE] = { AO_DESTROY, 3, 0, 292, 18, 3.6f, 7.0f, 110, "THE CANNON CORE" },
  [O_NEST_T3W] = { AO_TURRET, 3, -55, 205, 18, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_NEST_T3E] = { AO_TURRET, 3, 55, 225, 18, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
};

/* each stage's starts, four a team: attackers (facing up the beach), defenders (facing down it) */
static const float ATT[4 * 4][3] = {
  { -24, -306, 0 }, { 24, -306, 0 }, { -44, -300, 0 }, { 44, -300, 0 },
  { -25, -140, 0 }, { 25, -140, 0 }, { -82, -140, 0 }, { 82, -140, 0 },
  { -30, -30, 0 }, { 30, -30, 0 }, { -62, -25, 0 }, { 62, -25, 0 },
  { 40, 176, 0 }, { 62, 182, 0 }, { 84, 178, 0 }, { 50, 194, 0 },
};
static const float DEF[4 * 4][3] = {
  { -20, -82, 180 }, { 20, -82, 180 }, { -75, -80, 180 }, { 75, -80, 180 },
  { -30, -8, 180 }, { 30, -8, 180 }, { -60, 4, 180 }, { 60, 4, 180 },
  { -30, 90, 180 }, { 30, 90, 180 }, { -40, 132, 180 }, { 20, 130, 180 },
  { -30, 228, 180 }, { 30, 228, 180 }, { -62, 240, 180 }, { 62, 240, 180 },
};

static const assault_def ASSAULT = {
  .time_s = 360,
  .objs = OBJS, .nobjs = sizeof OBJS / sizeof OBJS[0],
  .nstages = 4,
  .need = { 2, 1, 1, 1 },
  .stage_name = { "TAKE THE BEACH", "BREACH THE SEAWALL", "TAKE THE CLIFF TOP", "DESTROY THE CANNON" },
  .att_spawns = ATT, .def_spawns = DEF,
};

static const float BOXES[][3] = {
  { -70, -236, 0 }, { -35, -232, 0 }, { 0, -236, 0 }, { 35, -232, 0 }, { 70, -236, 0 },
  { -30, -132, 0 }, { 30, -132, 0 }, { -88, -128, 0 }, { 88, -128, 0 },
  { -50, -22, 0 }, { 0, 2, 0 }, { 50, -22, 0 }, { 20, 44, 0 },
  { -20, 90, 6 }, { 20, 90, 6 }, { 0, 130, 12 }, { -30, 190, 18 }, { 30, 190, 18 }, { 0, 230, 18 },
};

static const arena_boost BOOSTS[] = {
  { -30, -270, 0, 8, 12, 0 }, { 30, -270, 0, 8, 12, 0 }, { 70, 20, 0, 8, 12, 0 },
};

/* the CPU drivers' way up: the beach, the lanes, the gate, the switchback */
static const float ROUTE[][2] = {
  { -55, -250 }, { 0, -250 }, { 55, -250 }, { -55, -128 }, { 0, -128 }, { 55, -128 }, { -55, -100 }, { 0, -100 }, { 55, -100 },
  { 0, -66 }, { 0, -34 }, { 70, 30 }, { 70, 78 }, { -70, 76 }, { -70, 117 }, { 70, 115 }, { 70, 158 }, { 0, 205 }, { 0, 244 },
  { 0, 275 },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 1, 2 }, { 0, 3 }, { 1, 4 }, { 2, 5 }, { 3, 4 }, { 4, 5 }, { 3, 6 }, { 4, 7 }, { 5, 8 }, { 6, 7 }, { 7, 8 },
  { 6, 9 }, { 7, 9 }, { 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 }, { 16, 17 },
  { 17, 18 }, { 18, 19 },
};

static const arena_def ARENA = {
  .half_x = 100, .half_z = 330, .wall_h = 5.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = ATT, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_ASSAULT,
  .assault = &ASSAULT,
  .look = {
    [AL_FLOOR] = { TEX_GROUND_SAND, { 0.86f, 0.78f, 0.62f }, 1.15f, 9.0f, 0.9f, 0 },
    [AL_WALL] = { TEX_WALL_CONCRETE, { 0.72f, 0.72f, 0.7f }, 1.3f, 6.0f, 0.9f, 0 },
    [AL_COVER] = { TEX_WALL_CONCRETE, { 0.78f, 0.76f, 0.72f }, 1.3f, 4.0f, 0.85f, 0 },
    [AL_DECK] = { TEX_GROUND_ROCK, { 0.62f, 0.62f, 0.58f }, 1.4f, 9.0f, 0.95f, 0 },
    [AL_RAMP] = { TEX_ROAD_CONCRETE, { 0.8f, 0.78f, 0.74f }, 1.3f, 6.0f, 0.9f, 0 },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.8f, 0 },
    [AL_FEATURE] = { TEX_WALL_CONCRETE, { 0.8f, 0.8f, 0.78f }, 1.3f, 5.0f, 0.85f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.7f, 0.4f }, 1, 1, 1, 0, 3.0f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the coast round it ───────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float coast_ground(float x, float z, void *ud) {
  (void)ud;
  float lz = z - ARENA_CZ, dx = fabsf(x);
  /* the bay's floor, the headlands either side climbing to the cliff's height and beyond, the plateau behind it */
  float side = dx - 104.0f;
  float h = -0.05f;
  if (lz < -334.0f) h = -0.05f - fminf((-334.0f - lz) * 0.08f, 6.0f); /* the sand running into the sea */
  if (side > 0) {
    float rise = fminf(side * 0.9f, 30.0f) * (lz < -300.0f ? fmaxf(0.0f, 1.0f - (-300.0f - lz) / 120.0f) : 1.0f);
    h = fmaxf(h, rise + ground_noise(x, z, 60, 6, 3, 41) * fminf(side / 40.0f, 1.0f));
  }
  if (lz > 334.0f) h = fmaxf(h, 18.0f + ground_noise(x, z, 80, 8, 3, 43) * fminf((lz - 334.0f) / 60.0f, 1.0f));
  return h;
}

static void breakwater_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1400.0f;
  g.height = coast_ground;
  g.tex = TEX_GROUND_SAND;
  g.tex_repeat = 10.0f;
  g.tint = pc_mul(hex(0xc8b890), 1.25f);
  g.tint2 = pc_mul(hex(0x6e7462), 1.2f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 71;
  ground_plane(scene, t, &g);
  /* the sea: grey-green, choppy, out to the horizon */
  water_opts w = water_defaults();
  w.color = rgb(0.16f, 0.36f, 0.38f);
  w.deep = rgb(0.03f, 0.1f, 0.12f);
  w.clarity = 0.3f;
  w.wave_height = 0.5f;
  w.wave_length = 22;
  w.wave_dir = 95;
  w.choppy = 0.55f;
  w.foam = 0.8f;
  w.reflect = 0.6f;
  w.refract = !split;
  w.horizon = 2400.0f;
  water_surface(scene, 0, ARENA_CZ - 900.0f, 2400.0f, 1100.0f, -0.6f, &w);

  prop_set *ps = ps_new(scene, "CoastProps");
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, hex(0xa8a49a), 1.0f);
  ps_style(ps, PK_ROCK, TEX_GROUND_ROCK, hex(0x9c9a92), 1.0f);
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  /* rocks along the headlands and the cliff's foot outside the walls */
  for (int i = 0; i < (split ? 14 : 28); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 20 + (float)(i / 2) * 48.0f + 16 * hf(2100u + (uint32_t)i), x = side * (hx + 8 + 20 * hf(2110u + (uint32_t)i));
    ps_add(ps, i % 3 ? PK_BOULDERS : PK_ROCK, (t3_vec3){ x, coast_ground(x, z + cz, NULL) - 0.4f, z + cz }, hf(2120u + (uint32_t)i) * 6.28f, 1.4f + 1.2f * hf(2130u + (uint32_t)i), 2140u + (uint32_t)i);
  }
  /* the breakwater itself: a long rubble arm out into the sea, and wrecks and buoys off the beach */
  for (int i = 0; i < (split ? 10 : 20); i++) {
    float x = 120.0f + (float)i * 9.0f, z = -hz - 30.0f - (float)i * 14.0f + cz;
    ps_add(ps, PK_BOULDERS, (t3_vec3){ x, -1.4f, z }, hf(2200u + (uint32_t)i) * 6.28f, 1.6f + 0.6f * hf(2210u + (uint32_t)i), 2220u + (uint32_t)i);
  }
  ps_add(ps, PK_WRECK_TRUCK, (t3_vec3){ -40, -1.6f, -hz - 80 + cz }, 0.7f, 2.2f, 2301u);
  ps_add(ps, PK_WRECK_TRUCK, (t3_vec3){ 70, -1.8f, -hz - 140 + cz }, 2.4f, 2.6f, 2302u);
  /* the lighthouse and the radio masts on the plateau, pines along the headlands */
  ps_add(ps, PK_SILO, (t3_vec3){ -150, coast_ground(-150, cz - 260, NULL), cz - 260 }, 0.0f, 1.2f, 2310u);
  for (int i = 0; i < 4; i++) ps_add(ps, PK_ANTENNA, (t3_vec3){ -60.0f + (float)i * 40.0f, 18.0f, hz + 30 + cz }, 0, 1.6f, 2320u + (uint32_t)i);
  for (int i = 0; i < (split ? 18 : 40); i++) {
    float side = i & 1 ? 1.0f : -1.0f, x = side * (hx + 30 + 140 * hf(2400u + (uint32_t)i)), z = (hf(2410u + (uint32_t)i) * 2 - 1) * hz * 0.9f + cz + 60;
    ps_add(ps, PK_PINE, (t3_vec3){ x, coast_ground(x, z, NULL) - 0.2f, z }, hf(2420u + (uint32_t)i) * 6.28f, 1.0f + 0.5f * hf(2430u + (uint32_t)i), 2440u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.15f : 0.3f);
}

const map_def map_breakwater = {
  .name = "Breakwater",
  .blurb = "Storm the beach, blow the seawall gate, fight up the cliff and destroy the coastal cannon.",
  .scene = "scenes/breakwater.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.9f, 0.84f, 0.7f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.72f, 0.72f, 0.7f },
            .wall_top = { 0.8f, 0.8f, 0.78f }, .road_tex = TEX_ROAD_SAND, .wall_tex = TEX_WALL_CONCRETE, .road_rough = 0.9f },
  .laps = 1,
  .par_lap = 360,
  .scenery = breakwater_scenery,
  .arena = &ARENA,
  .music = 5,
};
