/* assault map: Breakout. A quarry prison at the head of a red canyon, at dusk under floodlights. The attackers are the
 * prisoners: they start in the pens and have to get out, 650 m to the pad where the lift waits:
 *
 *   the yard       the pens open onto the exercise yard; the guard post in its middle holds the switch for the yard gate:
 *                  get to it
 *   the quarry     terraces, a crane and a conveyor jump, up to the main wall: its portcullis hangs on two winches at
 *                  the wall's foot. Break both
 *   the gatehouse  a courtyard between the main wall and the outer gate, the gate lever in its middle: hold it
 *   the canyon     the outer gate opens on the canyon road, winding between rock buttresses with jumps on it, the pad at
 *                  its end: get one kart onto the pad and the escape is made
 * The guards fall back stage by stage; floodlit gun towers cover each stretch. An assault map only (arena_def.modes). */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define MOUND(x, z, w, h, d) { AP_MOUND, AL_DECK, x, z, 0, w, h, d, 0, false, 0 }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
#define GBOX(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, false, col, 0, g }
#define GDECO(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, true, col, 0, g }
#define GPOST(look, x, z, y, dia, h, col, g) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col, 0, g }
#define GGLOWPOST(x, z, y, dia, h, col, g) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col, 0, g }

#define CONC 0x8e8a84
#define STEEL 0x58606a
#define RUST 0x8a4a2a
#define WOOD 0x6e4a2c
#define RED 0xff3a20
#define LAMP 0xffe0a0
#define GREEN 0x60ff90

enum { O_DOOR_W, O_DOOR_E, O_SWITCH, O_TOWER_W, O_TOWER_E, O_WINCH_W, O_WINCH_E, O_NEST_Q1, O_NEST_Q2, O_LEVER, O_NEST_G1, O_NEST_G2, O_PAD, O_NEST_C1, O_NEST_C2 };

/* a gun nest: a concrete ring, the gun, a red lamp */
#define NEST(o, x, z)                                                                                                  \
  GPOST(AL_COVER, x, z, 0, 4.6f, 1.1f, CONC, GATE_OBJ(o)), GPOST(AL_FEATURE, x, z, 1.1f, 1.7f, 1.3f, STEEL, GATE_OBJ(o)),   \
  GDECO(AL_FEATURE, x, z - 1.6f, 1.8f, 0.5f, 0.5f, 3.0f, STEEL, GATE_OBJ(o)), GGLOWPOST(x, z, 2.4f, 0.5f, 0.5f, RED, GATE_OBJ(o))

/* a floodlight mast: the pole, the lamp head lit */
#define MAST(x, z) POST(AL_FEATURE, x, z, 0, 0.7f, 12, STEEL), DECO(AL_GLOW, x, z, 12, 2.4f, 0.9f, 1.2f, 0, LAMP)

/* a winch at the wall's foot: the drum on its frame, the chain up the wall, all gone when broken */
#define WINCH(o, x)                                                                                                    \
  GPOST(AL_FEATURE, x, 72, 0, 4.4f, 3.2f, RUST, GATE_OBJ(o)), GBOX(AL_COVER, x, 72, 0, 6, 1.0f, 3, STEEL, GATE_OBJ(o)),   \
  GDECO(AL_FEATURE, x, 76, 3.0f, 0.7f, 7, 0.7f, STEEL, GATE_OBJ(o)), GGLOWPOST(x, 72, 3.2f, 4.6f, 0.2f, RED, GATE_OBJ(o))

/* a guard tower (drawn only): four legs, a cabin with lit windows, a roof */
#define TOWER(x, z)                                                                                                    \
  DECO(AL_FEATURE, x - 2, z - 2, 0, 0.6f, 12, 0.6f, 0, STEEL), DECO(AL_FEATURE, x + 2, z - 2, 0, 0.6f, 12, 0.6f, 0, STEEL),   \
  DECO(AL_FEATURE, x - 2, z + 2, 0, 0.6f, 12, 0.6f, 0, STEEL), DECO(AL_FEATURE, x + 2, z + 2, 0, 0.6f, 12, 0.6f, 0, STEEL),   \
  DECO(AL_FEATURE, x, z, 12, 6, 3.2f, 6, 0, 0x6a625a), DECO(AL_GLOW, x, z, 13.2f, 6.2f, 0.9f, 6.2f, 0, LAMP),            \
  DECO(AL_FEATURE, x, z, 15.2f, 7.4f, 0.5f, 7.4f, 0, 0x3a3a3c)
/* razor wire along the top of a wall run (drawn only): a coil of thin bars */
#define WIRE(x, z, y, w, d) DECO(AL_FEATURE, x, z, y, w, 0.25f, d, 0, 0x9aa0a8), DECO(AL_FEATURE, x, z, y + 0.5f, w, 0.25f, d, 0, 0x8a9098)

static const arena_prim PRIMS[] = {
  /* the prison's dressing: guard towers at the corners of each stretch, razor wire along the walls' tops */
  TOWER(-84, -84), TOWER(84, -84), TOWER(-84, 80), TOWER(84, 80), TOWER(-84, 148), TOWER(84, 148), TOWER(-84, -325), TOWER(84, -325),
  WIRE(-51, -80, 8, 78, 0.6f), WIRE(51, -80, 8, 78, 0.6f), WIRE(-52, 85, 10, 76, 0.6f), WIRE(52, 85, 10, 76, 0.6f),
  WIRE(-52, 153, 9, 76, 0.6f), WIRE(52, 153, 9, 76, 0.6f),
  /* the pens: low walls round two yards at the south end, open to the north, the guard block between them */
  BOX(AL_WALL, -75, -299, 0, 2, 4, 62, 0, 0), BOX(AL_WALL, -35, -299, 0, 2, 4, 62, 0, 0),
  BOX(AL_WALL, -68, -268, 0, 14, 4, 2, 0, 0), BOX(AL_WALL, -42, -268, 0, 14, 4, 2, 0, 0),
  BOX(AL_WALL, 75, -299, 0, 2, 4, 62, 0, 0), BOX(AL_WALL, 35, -299, 0, 2, 4, 62, 0, 0),
  BOX(AL_WALL, 68, -268, 0, 14, 4, 2, 0, 0), BOX(AL_WALL, 42, -268, 0, 14, 4, 2, 0, 0),
  /* the pen doors: bars across each opening, the lock lit red; blast either and both locks fail */
  GBOX(AL_FEATURE, -55, -268, 0, 12, 4, 1.2f, STEEL, GATE_STAGE(0)), GGLOWPOST(-55, -268.9f, 2.0f, 0.6f, 0.6f, RED, GATE_STAGE(0)),
  GBOX(AL_FEATURE, 55, -268, 0, 12, 4, 1.2f, STEEL, GATE_STAGE(0)), GGLOWPOST(55, -268.9f, 2.0f, 0.6f, 0.6f, RED, GATE_STAGE(0)),
  BOX(AL_FEATURE, 0, -300, 0, 66, 9, 56, 0, CONC), DECO(AL_GLOW, 0, -271.9f, 6, 40, 0.5f, 0.1f, 0, LAMP),
  /* the yard: the guard post (four short walls with a way in on each side, the switch console in the middle), towers */
  BOX(AL_COVER, -7, -167, 0, 6, 1.3f, 1, 0, CONC), BOX(AL_COVER, 7, -167, 0, 6, 1.3f, 1, 0, CONC),
  BOX(AL_COVER, -7, -153, 0, 6, 1.3f, 1, 0, CONC), BOX(AL_COVER, 7, -153, 0, 6, 1.3f, 1, 0, CONC),
  POST(AL_FEATURE, 0, -160, 0, 1.2f, 1.6f, STEEL), GLOWPOST(0, -160, 1.6f, 1.0f, 0.3f, GREEN),
  NEST(O_TOWER_W, -55, -200), NEST(O_TOWER_E, 55, -200),
  BOX(AL_COVER, -40, -225, 0, 10, 2.4f, 4, 25, RUST), BOX(AL_COVER, 46, -128, 0, 4, 2.4f, 10, -10, RUST),
  RAMP(AL_RAMP, -50, -140, 0, 12, 1.5f, 9, 0), RAMP(AL_RAMP, 30, -210, 0, 12, 1.5f, 9, 180),
  MAST(-80, -240), MAST(80, -240), MAST(-80, -100), MAST(80, -100),
  /* the yard gate */
  BOX(AL_WALL, -51, -80, 0, 78, 8, 4, 0, 0), BOX(AL_WALL, 51, -80, 0, 78, 8, 4, 0, 0),
  GBOX(AL_FEATURE, 0, -80, 0, 24, 8, 2.5f, STEEL, GATE_STAGE(1)), GDECO(AL_TRIM, 0, -81.4f, 6, 24, 0.6f, 0.1f, 0, GATE_STAGE(1)),
  /* the quarry: two worked-stone terraces, boulders, the conveyor jump, the crane */
  MOUND(-50, -20, 34, 3.5f, 40), MOUND(52, 20, 30, 4, 36),
  POST(AL_COVER, -10, -40, 0, 5, 3, 0), POST(AL_COVER, 18, -6, 0, 4, 2.5f, 0), POST(AL_COVER, -24, 30, 0, 6, 3.5f, 0),
  RAMP(AL_RAMP, 0, 10, 0, 10, 2.2f, 12, 0),
  POST(AL_FEATURE, 70, -40, 0, 2, 22, RUST), DECO(AL_FEATURE, 52, -40, 22, 40, 1.4f, 1.4f, 0, RUST),
  NEST(O_NEST_Q1, -40, 40), NEST(O_NEST_Q2, 40, 52),
  MAST(-82, 0), MAST(82, -60),
  /* the main wall, the portcullis in its gap and the winches that hold it */
  BOX(AL_WALL, -52, 85, 0, 76, 10, 10, 0, 0), BOX(AL_WALL, 52, 85, 0, 76, 10, 10, 0, 0),
  GBOX(AL_FEATURE, 0, 85, 0, 28, 10, 2, STEEL, GATE_STAGE(2)),
  GDECO(AL_FEATURE, -9, 83.9f, 0, 0.5f, 10, 0.2f, STEEL, GATE_STAGE(2)), GDECO(AL_FEATURE, 0, 83.9f, 0, 0.5f, 10, 0.2f, STEEL, GATE_STAGE(2)),
  GDECO(AL_FEATURE, 9, 83.9f, 0, 0.5f, 10, 0.2f, STEEL, GATE_STAGE(2)),
  WINCH(O_WINCH_W, -24), WINCH(O_WINCH_E, 24),
  /* the gatehouse courtyard: the lever in its middle, pillars, the guards' side rooms */
  POST(AL_FEATURE, 0, 120, 0, 1.0f, 2.2f, STEEL), GLOWPOST(0, 120, 2.2f, 0.9f, 0.4f, LAMP),
  POST(AL_COVER, -28, 105, 0, 3, 5, CONC), POST(AL_COVER, 28, 105, 0, 3, 5, CONC), POST(AL_COVER, -28, 140, 0, 3, 5, CONC), POST(AL_COVER, 28, 140, 0, 3, 5, CONC),
  BOX(AL_WALL, -60, 110, 0, 2, 5, 30, 0, 0), BOX(AL_WALL, 60, 110, 0, 2, 5, 30, 0, 0),
  NEST(O_NEST_G1, -38, 138), NEST(O_NEST_G2, 38, 132),
  /* the outer gate */
  BOX(AL_WALL, -52, 153, 0, 76, 9, 6, 0, 0), BOX(AL_WALL, 52, 153, 0, 76, 9, 6, 0, 0),
  GBOX(AL_FEATURE, 0, 153, 0, 28, 9, 2, WOOD, GATE_STAGE(3)), GDECO(AL_TRIM, 0, 151.9f, 7, 28, 0.6f, 0.1f, 0, GATE_STAGE(3)),
  /* the canyon road: rock buttresses from alternate sides, a kicker in each opening, the pad at the end */
  BOX(AL_FEATURE, -55, 205, 0, 70, 14, 30, 0, 0), BOX(AL_FEATURE, 55, 245, 0, 70, 14, 30, 0, 0), BOX(AL_FEATURE, -55, 285, 0, 70, 14, 30, 0, 0),
  RAMP(AL_RAMP, 40, 188, 0, 14, 1.6f, 9, 0), RAMP(AL_RAMP, -40, 228, 0, 14, 1.6f, 9, 0), RAMP(AL_RAMP, 40, 268, 0, 14, 1.6f, 9, 0),
  POST(AL_COVER, 10, 225, 0, 4, 3, 0), POST(AL_COVER, -12, 262, 0, 4, 3, 0),
  NEST(O_NEST_C1, 60, 210), NEST(O_NEST_C2, -62, 250),
  DECO(AL_PAINT, 0, 318, 0, 22, 0.03f, 22, 45, 0xf0d040), DECO(AL_PAINT, 0, 318, 0.01f, 16, 0.03f, 16, 45, 0x303034),
  GLOWPOST(-11, 318, 0, 0.6f, 0.4f, GREEN), GLOWPOST(11, 318, 0, 0.6f, 0.4f, GREEN), GLOWPOST(0, 307, 0, 0.6f, 0.4f, GREEN), GLOWPOST(0, 329, 0, 0.6f, 0.4f, GREEN),
};

static const assault_obj OBJS[] = {
  [O_DOOR_W] = { AO_DESTROY, 0, -55, -268, 0, 6.5f, 4.0f, 35, "THE WEST PEN DOOR" },
  [O_DOOR_E] = { AO_DESTROY, 0, 55, -268, 0, 6.5f, 4.0f, 35, "THE EAST PEN DOOR" },
  [O_SWITCH] = { AO_REACH, 1, 0, -160, 0, 5.5f, 0, 0, "THE YARD SWITCH" },
  [O_TOWER_W] = { AO_TURRET, 1, -55, -200, 0, 2.4f, 3.0f, 40, "A GUN TOWER", 70 },
  [O_TOWER_E] = { AO_TURRET, 1, 55, -200, 0, 2.4f, 3.0f, 40, "A GUN TOWER", 70 },
  [O_WINCH_W] = { AO_DESTROY, 2, -24, 72, 0, 3.0f, 3.5f, 35, "THE WEST WINCH" },
  [O_WINCH_E] = { AO_DESTROY, 2, 24, 72, 0, 3.0f, 3.5f, 35, "THE EAST WINCH" },
  [O_NEST_Q1] = { AO_TURRET, 2, -40, 40, 0, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_NEST_Q2] = { AO_TURRET, 2, 40, 52, 0, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_LEVER] = { AO_HOLD, 3, 0, 120, 0, 10.0f, 0, 6, "THE GATE LEVER" },
  [O_NEST_G1] = { AO_TURRET, 3, -38, 138, 0, 2.4f, 3.0f, 40, "A GUN NEST", 60 },
  [O_NEST_G2] = { AO_TURRET, 3, 38, 132, 0, 2.4f, 3.0f, 40, "A GUN NEST", 60 },
  [O_PAD] = { AO_HOLD, 4, 0, 318, 0, 10.0f, 0, 4, "THE EXTRACTION PAD" },
  [O_NEST_C1] = { AO_TURRET, 4, 60, 210, 0, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_NEST_C2] = { AO_TURRET, 4, -62, 250, 0, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
};

static const float ATT[5 * 4][3] = {
  { -62, -306, 0 }, { -48, -294, 0 }, { 62, -306, 0 }, { 48, -294, 0 },
  { -62, -306, 0 }, { -48, -294, 0 }, { 62, -306, 0 }, { 48, -294, 0 },
  { -30, -100, 0 }, { 30, -100, 0 }, { -60, -110, 0 }, { 60, -110, 0 },
  { -30, 60, 0 }, { 30, 60, 0 }, { -60, 50, 0 }, { 60, 50, 0 },
  { -18, 128, 0 }, { 18, 128, 0 }, { -44, 122, 0 }, { 44, 122, 0 },
};
static const float DEF[5 * 4][3] = {
  { -40, -240, 180 }, { 40, -240, 180 }, { -70, -220, 180 }, { 70, -220, 180 },
  { -70, -128, 180 }, { 70, -128, 180 }, { -24, -108, 180 }, { 24, -108, 180 },
  { -56, 40, 180 }, { 56, 34, 180 }, { -16, 30, 180 }, { 16, 34, 180 },
  { -72, 108, 180 }, { 72, 108, 180 }, { -76, 138, 180 }, { 76, 138, 180 },
  { -22, 300, 180 }, { 22, 296, 180 }, { -40, 318, 180 }, { 40, 318, 180 },
};

static const assault_def ASSAULT = {
  .time_s = 300,
  .objs = OBJS, .nobjs = sizeof OBJS / sizeof OBJS[0],
  .nstages = 5,
  .need = { 1, 1, 2, 1, 1 },
  .stage_name = { "BREAK OUT OF THE PENS", "OPEN THE YARD GATE", "BREAK THE WINCHES", "HOLD THE GATE LEVER", "HOLD THE PAD FOR THE LIFT" },
  .att_spawns = ATT, .def_spawns = DEF,
};

static const float BOXES[][3] = {
  { -55, -255, 0 }, { 55, -255, 0 }, { 0, -230, 0 }, { -30, -185, 0 }, { 30, -185, 0 }, { 0, -120, 0 },
  { -30, -55, 0 }, { 30, -55, 0 }, { 0, -20, 0 }, { -10, 55, 0 }, { 10, 55, 0 },
  { -15, 100, 0 }, { 15, 100, 0 }, { 0, 140, 0 }, { 30, 180, 0 }, { -30, 240, 0 }, { 30, 280, 0 },
};

static const arena_boost BOOSTS[] = { { 0, -40, 0, 8, 12, 0 }, { 40, 170, 0, 8, 12, 0 }, { -40, 212, 0, 8, 12, 0 }, { 40, 252, 0, 8, 12, 0 } };

static const float ROUTE[][2] = {
  { -55, -290 }, { 55, -290 }, { -55, -255 }, { 55, -255 }, { 0, -205 }, { -30, -160 }, { 30, -160 }, { 0, -110 }, { 0, -60 },
  { -20, 0 }, { 20, 0 }, { 0, 55 }, { 0, 100 }, { 0, 140 }, { 0, 170 }, { 35, 205 }, { -35, 245 }, { 35, 285 }, { 0, 315 },
};
static const uint8_t LINKS[][2] = {
  { 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 4 }, { 2, 5 }, { 3, 6 }, { 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 }, { 7, 8 }, { 8, 9 }, { 8, 10 },
  { 9, 11 }, { 10, 11 }, { 11, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 }, { 16, 17 }, { 17, 18 },
};

static const arena_def ARENA = {
  .half_x = 90, .half_z = 330, .wall_h = 12.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = ATT, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_ASSAULT,
  .assault = &ASSAULT,
  .look = {
    [AL_FLOOR] = { TEX_GROUND_ROCK, { 0.78f, 0.56f, 0.42f }, 1.25f, 9.0f, 0.95f, 0 },
    [AL_WALL] = { TEX_WALL_CONCRETE, { 0.68f, 0.66f, 0.64f }, 1.3f, 6.0f, 0.9f, 0 },
    [AL_COVER] = { TEX_WALL_CONCRETE, { 0.74f, 0.7f, 0.66f }, 1.3f, 4.0f, 0.9f, 0 },
    [AL_DECK] = { TEX_GROUND_ROCK, { 0.82f, 0.6f, 0.44f }, 1.35f, 8.0f, 0.95f, 0 },
    [AL_RAMP] = { TEX_ROAD_DIRT, { 0.86f, 0.66f, 0.5f }, 1.3f, 6.0f, 0.9f, 0 },
    [AL_TRIM] = { TEX_WALL_HAZARD, { 1, 1, 1 }, 1.2f, 2.0f, 0.8f, 0 },
    [AL_FEATURE] = { TEX_GROUND_ROCK, { 0.8f, 0.5f, 0.36f }, 1.4f, 10.0f, 0.95f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.85f, 0.6f }, 1, 1, 1, 0, 3.5f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the canyon round it ──────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float canyon_ground(float x, float z, void *ud) {
  (void)ud;
  float lz = z - ARENA_CZ, side = fmaxf(fabsf(x) - 93.0f, fabsf(lz) - 333.0f);
  if (side <= 0) return -0.05f;
  /* the canyon walls: straight up to a plateau 40 m over the floor, broken at the top */
  return fminf(side * 2.2f, 40.0f) + ground_noise(x, z, 70, 10, 3, 61) * fminf(side / 30.0f, 1.0f);
}

static void breakout_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1400.0f;
  g.height = canyon_ground;
  g.tex = TEX_GROUND_ROCK;
  g.tex_repeat = 12.0f;
  g.tint = pc_mul(hex(0xc0663a), 1.25f);
  g.tint2 = pc_mul(hex(0x8a4a30), 1.25f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 97;
  ground_plane(scene, t, &g);

  prop_set *ps = ps_new(scene, "CanyonProps");
  ps_style(ps, PK_SPIRE, -1, hex(0xe89a70), 1.0f);
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, hex(0xd8946a), 1.0f);
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  /* spires and boulders along the rims, the prison's fence and lamps along the top of the walls */
  for (int i = 0; i < (split ? 12 : 26); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 20 + (float)(i / 2) * 52.0f + 18 * hf(4100u + (uint32_t)i), x = side * (hx + 24 + 40 * hf(4110u + (uint32_t)i));
    ps_add(ps, i % 3 ? PK_BOULDERS : PK_SPIRE, (t3_vec3){ x, canyon_ground(x, z + cz, NULL) - 0.3f, z + cz }, hf(4120u + (uint32_t)i) * 6.28f, 1.3f + 0.9f * hf(4130u + (uint32_t)i), 4140u + (uint32_t)i);
  }
  for (int i = 0; i < 12; i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 30.0f + (float)(i / 2) * 80.0f;
    ps_add(ps, PK_LIGHT_POLE, (t3_vec3){ side * (hx + 2.5f), 12.0f, z + cz }, side > 0 ? -1.57f : 1.57f, 1.2f, 4200u + (uint32_t)i);
  }
  /* the lift on the plateau beyond the pad, the quarry's machinery on the rim */
  ps_add(ps, PK_CRANE, (t3_vec3){ 130, canyon_ground(130, cz - 20, NULL), cz - 20 }, 2.2f, 1.6f, 4301u);
  ps_add(ps, PK_TANK, (t3_vec3){ -125, canyon_ground(-125, cz - 260, NULL), cz - 260 }, 0.3f, 1.4f, 4302u);
  ps_add(ps, PK_WATER_TOWER, (t3_vec3){ 120, canyon_ground(120, cz - 290, NULL), cz - 290 }, 0.0f, 1.3f, 4303u);
  for (int i = 0; i < (split ? 8 : 16); i++) {
    float ang = hf(4400u + (uint32_t)i) * 6.2831853f, r = 600.0f + 500.0f * hf(4410u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i & 1 ? PK_MESA : PK_SPIRE, (t3_vec3){ x, canyon_ground(x, z, NULL) - 3.0f, z }, hf(4420u + (uint32_t)i) * 6.28f, 2.4f + 2.0f * hf(4430u + (uint32_t)i), 4440u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.3f : 0.5f);
}

const map_def map_breakout = {
  .name = "Breakout",
  .blurb = "Prisoners attack: open the yard, break the winches, hold the lever and race down the canyon to the pad.",
  .scene = "scenes/breakout.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.86f, 0.66f, 0.5f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.68f, 0.66f, 0.64f },
            .wall_top = { 0.78f, 0.74f, 0.7f }, .road_tex = TEX_ROAD_DIRT, .wall_tex = TEX_WALL_CONCRETE, .road_rough = 0.9f },
  .laps = 1,
  .par_lap = 300,
  .scenery = breakout_scenery,
  .arena = &ARENA,
  .music = 5,
};
