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
  /* the cove: the sand, the beach running down under the sea at the south (drive in and you are back at the boats), grey
   * cliffs either side and behind the fortress */
  BOX(AL_FLOOR, 0, 2, -2, 200, 2, 664, 0, 0),
  { AP_RAMP, AL_FLOOR, 0, -345, -3.2f, 200, 1.2f, 30, 0, false, 0 },
  BOX(AL_WALL, -150, 0, -4, 100, 34, 700, 0, 0), BOX(AL_WALL, 150, 0, -4, 100, 34, 700, 0, 0), BOX(AL_WALL, 0, 385, -4, 400, 34, 100, 0, 0),
  /* the cliff faces: rock ledges and strata belts standing out of the walls (drawn only: all above a kart's roof, or
   * above the terraces where the terraces rise), so the cove's sides read as layered rock, not two flat slabs */
  DECO(AL_WALL, -100.75f, -112.5f, 9.5f, 2.50f, 0.9f, 475.0f, 0, 0x6a645a),
  DECO(AL_WALL, -100.75f, -102.5f, 16.5f, 2.50f, 1.3f, 495.0f, 0, 0x8e8676),
  DECO(AL_WALL, -100.75f, 0.0f, 23.0f, 2.50f, 0.8f, 700.0f, 0, 0x5e5a52),
  DECO(AL_WALL, -99.87f, -337.1f, 13.1f, 4.26f, 1.8f, 17.4f, 0, 0x7e786e),
  DECO(AL_WALL, -100.24f, -332.3f, 15.7f, 3.52f, 1.7f, 16.0f, 0, 0x7e786e),
  DECO(AL_WALL, -100.59f, -301.6f, 18.5f, 2.82f, 3.6f, 23.4f, 0, 0x857a6a),
  DECO(AL_WALL, -100.37f, -306.9f, 5.1f, 3.25f, 4.5f, 25.5f, 0, 0x8c8578),
  DECO(AL_WALL, -100.30f, -269.0f, 17.4f, 3.41f, 3.8f, 31.2f, 0, 0x857a6a),
  DECO(AL_WALL, -99.77f, -228.9f, 21.9f, 4.45f, 3.2f, 20.5f, 0, 0x6e6a62),
  DECO(AL_WALL, -100.10f, -234.3f, 20.1f, 3.79f, 2.4f, 23.1f, 0, 0x857a6a),
  DECO(AL_WALL, -100.58f, -229.4f, 14.3f, 2.84f, 3.7f, 22.1f, 0, 0x857a6a),
  DECO(AL_WALL, -100.12f, -206.3f, 26.1f, 3.76f, 1.9f, 20.8f, 0, 0x6e6a62),
  DECO(AL_WALL, -100.20f, -163.8f, 23.3f, 3.60f, 4.8f, 22.8f, 0, 0x7e786e),
  DECO(AL_WALL, -100.01f, -122.0f, 10.5f, 3.97f, 2.9f, 44.6f, 0, 0x7e786e),
  DECO(AL_WALL, -100.44f, -123.9f, 18.1f, 3.11f, 3.3f, 27.8f, 0, 0x6e6a62),
  DECO(AL_WALL, -100.57f, -123.4f, 13.0f, 2.85f, 4.6f, 33.5f, 0, 0x9a9282),
  DECO(AL_WALL, -100.00f, -88.5f, 20.2f, 4.00f, 5.0f, 27.2f, 0, 0x9a9282),
  DECO(AL_WALL, -100.43f, -88.2f, 8.1f, 3.14f, 2.4f, 25.7f, 0, 0x9a9282),
  DECO(AL_WALL, -100.53f, -60.3f, 18.0f, 2.94f, 2.7f, 18.2f, 0, 0x857a6a),
  DECO(AL_WALL, -99.82f, -62.2f, 21.0f, 4.35f, 3.2f, 13.6f, 0, 0x857a6a),
  DECO(AL_WALL, -100.23f, -43.7f, 8.4f, 3.54f, 4.9f, 24.1f, 0, 0x7e786e),
  DECO(AL_WALL, -100.58f, -17.6f, 25.8f, 2.83f, 3.7f, 15.6f, 0, 0x8c8578),
  DECO(AL_WALL, -100.19f, 4.7f, 14.9f, 3.63f, 2.0f, 19.2f, 0, 0x9a9282),
  DECO(AL_WALL, -99.95f, 3.2f, 7.3f, 4.11f, 4.1f, 23.2f, 0, 0x9a9282),
  DECO(AL_WALL, -100.62f, 40.2f, 7.4f, 2.75f, 3.4f, 30.4f, 0, 0x857a6a),
  DECO(AL_WALL, -100.16f, 32.7f, 6.1f, 3.68f, 4.5f, 31.5f, 0, 0x8c8578),
  DECO(AL_WALL, -99.93f, 82.3f, 20.4f, 4.14f, 4.3f, 27.5f, 0, 0x8c8578),
  DECO(AL_WALL, -100.16f, 84.4f, 22.6f, 3.68f, 2.4f, 26.4f, 0, 0x6e6a62),
  DECO(AL_WALL, -100.23f, 113.6f, 21.8f, 3.55f, 4.9f, 28.5f, 0, 0x6e6a62),
  DECO(AL_WALL, -100.46f, 120.8f, 13.7f, 3.07f, 2.4f, 22.3f, 0, 0x8c8578),
  DECO(AL_WALL, -100.02f, 142.7f, 24.8f, 3.96f, 1.9f, 12.7f, 0, 0x9a9282),
  DECO(AL_WALL, -99.90f, 143.8f, 21.3f, 4.20f, 2.2f, 8.3f, 0, 0x6e6a62),
  DECO(AL_WALL, -100.53f, 168.4f, 25.6f, 2.94f, 2.2f, 23.5f, 0, 0x8c8578),
  DECO(AL_WALL, -100.13f, 209.2f, 25.3f, 3.74f, 2.8f, 18.8f, 0, 0x8c8578),
  DECO(AL_WALL, -99.94f, 210.2f, 25.6f, 4.12f, 1.9f, 25.0f, 0, 0x8c8578),
  DECO(AL_WALL, -100.41f, 208.7f, 26.1f, 3.18f, 2.3f, 26.0f, 0, 0x6e6a62),
  DECO(AL_WALL, -99.80f, 238.5f, 22.3f, 4.41f, 4.1f, 28.8f, 0, 0x857a6a),
  DECO(AL_WALL, -100.53f, 232.2f, 26.1f, 2.95f, 3.9f, 24.7f, 0, 0x8c8578),
  DECO(AL_WALL, -100.52f, 266.5f, 25.9f, 2.97f, 2.1f, 17.4f, 0, 0x857a6a),
  DECO(AL_WALL, -100.12f, 286.3f, 25.9f, 3.76f, 2.0f, 19.5f, 0, 0x8c8578),
  DECO(AL_WALL, -100.62f, 293.2f, 22.5f, 2.75f, 3.1f, 22.9f, 0, 0x7e786e),
  DECO(AL_WALL, -99.99f, 295.6f, 24.5f, 4.02f, 3.3f, 23.8f, 0, 0x9a9282),
  DECO(AL_WALL, -100.46f, 317.9f, 26.6f, 3.08f, 3.4f, 13.4f, 0, 0x9a9282),
  DECO(AL_WALL, -100.42f, 319.5f, 24.2f, 3.16f, 1.8f, 14.2f, 0, 0x7e786e),
  DECO(AL_WALL, 100.75f, -112.5f, 9.5f, 2.50f, 0.9f, 475.0f, 0, 0x6a645a),
  DECO(AL_WALL, 100.75f, -102.5f, 16.5f, 2.50f, 1.3f, 495.0f, 0, 0x8e8676),
  DECO(AL_WALL, 100.75f, 0.0f, 23.0f, 2.50f, 0.8f, 700.0f, 0, 0x5e5a52),
  DECO(AL_WALL, 99.73f, -330.1f, 7.3f, 4.54f, 4.6f, 39.0f, 0, 0x8c8578),
  DECO(AL_WALL, 100.02f, -324.2f, 24.4f, 3.97f, 2.2f, 27.4f, 0, 0x8c8578),
  DECO(AL_WALL, 100.30f, -285.9f, 12.2f, 3.40f, 1.9f, 19.9f, 0, 0x6e6a62),
  DECO(AL_WALL, 100.06f, -289.1f, 4.4f, 3.89f, 2.7f, 24.4f, 0, 0x857a6a),
  DECO(AL_WALL, 99.79f, -256.5f, 5.9f, 4.42f, 2.5f, 14.3f, 0, 0x8c8578),
  DECO(AL_WALL, 100.16f, -226.3f, 13.3f, 3.68f, 3.4f, 37.3f, 0, 0x9a9282),
  DECO(AL_WALL, 100.25f, -227.5f, 5.3f, 3.51f, 3.9f, 29.0f, 0, 0x7e786e),
  DECO(AL_WALL, 100.22f, -189.9f, 5.5f, 3.56f, 4.5f, 22.3f, 0, 0x6e6a62),
  DECO(AL_WALL, 100.61f, -185.0f, 25.1f, 2.78f, 3.7f, 21.7f, 0, 0x8c8578),
  DECO(AL_WALL, 100.45f, -171.0f, 18.5f, 3.09f, 3.4f, 15.3f, 0, 0x9a9282),
  DECO(AL_WALL, 100.41f, -168.4f, 12.0f, 3.18f, 1.7f, 11.1f, 0, 0x7e786e),
  DECO(AL_WALL, 99.87f, -133.1f, 25.5f, 4.26f, 2.0f, 25.7f, 0, 0x9a9282),
  DECO(AL_WALL, 100.36f, -135.6f, 24.4f, 3.28f, 4.9f, 30.2f, 0, 0x8c8578),
  DECO(AL_WALL, 100.60f, -90.6f, 13.3f, 2.80f, 2.8f, 20.4f, 0, 0x8c8578),
  DECO(AL_WALL, 100.60f, -90.0f, 24.2f, 2.81f, 3.1f, 34.5f, 0, 0x9a9282),
  DECO(AL_WALL, 100.39f, -59.3f, 5.0f, 3.21f, 2.2f, 14.5f, 0, 0x7e786e),
  DECO(AL_WALL, 100.42f, -66.1f, 26.4f, 3.16f, 3.5f, 14.1f, 0, 0x6e6a62),
  DECO(AL_WALL, 100.17f, -41.3f, 15.6f, 3.66f, 2.3f, 9.9f, 0, 0x7e786e),
  DECO(AL_WALL, 100.28f, -45.2f, 7.3f, 3.45f, 3.6f, 11.5f, 0, 0x6e6a62),
  DECO(AL_WALL, 99.81f, -12.7f, 19.1f, 4.37f, 4.0f, 27.7f, 0, 0x9a9282),
  DECO(AL_WALL, 100.06f, -11.3f, 15.4f, 3.88f, 2.6f, 17.6f, 0, 0x8c8578),
  DECO(AL_WALL, 100.11f, 26.7f, 24.9f, 3.78f, 4.2f, 31.4f, 0, 0x7e786e),
  DECO(AL_WALL, 99.99f, 19.3f, 24.5f, 4.02f, 3.9f, 20.0f, 0, 0x8c8578),
  DECO(AL_WALL, 100.05f, 69.7f, 24.2f, 3.89f, 3.5f, 38.3f, 0, 0x857a6a),
  DECO(AL_WALL, 99.94f, 62.6f, 10.1f, 4.12f, 4.3f, 42.6f, 0, 0x857a6a),
  DECO(AL_WALL, 100.20f, 65.1f, 18.9f, 3.60f, 4.1f, 41.6f, 0, 0x7e786e),
  DECO(AL_WALL, 100.19f, 110.5f, 18.4f, 3.61f, 2.9f, 19.1f, 0, 0x6e6a62),
  DECO(AL_WALL, 100.51f, 111.2f, 20.9f, 2.98f, 1.9f, 18.0f, 0, 0x6e6a62),
  DECO(AL_WALL, 100.19f, 111.0f, 20.6f, 3.62f, 2.1f, 14.3f, 0, 0x9a9282),
  DECO(AL_WALL, 100.21f, 144.4f, 25.5f, 3.59f, 2.6f, 35.5f, 0, 0x7e786e),
  DECO(AL_WALL, 100.20f, 148.3f, 23.6f, 3.60f, 1.9f, 26.9f, 0, 0x6e6a62),
  DECO(AL_WALL, 99.77f, 181.2f, 23.9f, 4.47f, 4.7f, 23.0f, 0, 0x7e786e),
  DECO(AL_WALL, 100.52f, 180.9f, 24.6f, 2.95f, 4.8f, 19.2f, 0, 0x857a6a),
  DECO(AL_WALL, 99.75f, 218.3f, 24.0f, 4.50f, 2.1f, 21.9f, 0, 0x9a9282),
  DECO(AL_WALL, 100.54f, 218.5f, 24.1f, 2.93f, 2.9f, 16.5f, 0, 0x6e6a62),
  DECO(AL_WALL, 99.77f, 213.0f, 26.2f, 4.46f, 2.0f, 28.6f, 0, 0x7e786e),
  DECO(AL_WALL, 100.39f, 240.2f, 23.8f, 3.22f, 3.1f, 18.0f, 0, 0x7e786e),
  DECO(AL_WALL, 100.16f, 284.6f, 23.2f, 3.67f, 2.5f, 30.8f, 0, 0x8c8578),
  DECO(AL_WALL, 99.93f, 286.8f, 24.1f, 4.15f, 1.7f, 33.0f, 0, 0x9a9282),
  DECO(AL_WALL, 100.07f, 316.9f, 26.7f, 3.87f, 3.0f, 31.1f, 0, 0x8c8578),
  /* the wet sand at the waterline, darker, and driftwood on it */
  DECO(AL_PAINT, 0, -318, 0.01f, 200, 0.02f, 24, 0, 0x8a7c5e),
  DECO(AL_COVER, -40, -296, 0, 7, 0.5f, 0.6f, 25, 0x8a6a4a), DECO(AL_COVER, 22, -282, 0, 5, 0.4f, 0.5f, -40, 0x7a5a3a),
  DECO(AL_COVER, 72, -300, 0, 6, 0.45f, 0.55f, 70, 0x8a6a4a),
  /* tide pools and lines of kelp left by the tide */
  DECO(AL_PAINT, -70, -312, 0.02f, 9, 0.02f, 5, 15, 0x4a6a70), DECO(AL_PAINT, 40, -306, 0.02f, 7, 0.02f, 4, -25, 0x4a6a70),
  DECO(AL_PAINT, -10, -320, 0.02f, 12, 0.02f, 4, 5, 0x4a6a70),
  DECO(AL_PAINT, -40, -288, 0.02f, 40, 0.02f, 0.8f, 3, 0x3a4a2a), DECO(AL_PAINT, 45, -292, 0.02f, 36, 0.02f, 0.7f, -4, 0x3a4a2a),
  DECO(AL_PAINT, 0, -270, 0.02f, 50, 0.02f, 0.6f, 2, 0x4a5a34),
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
  /* low dunes up the beach, sand you hop over (and duck behind) between the hedgehogs */
  { AP_MOUND, AL_RAMP, -36, -232, 0, 22, 1.1f, 14, 20, false, 0 },
  { AP_MOUND, AL_RAMP, 30, -210, 0, 15, 0.9f, 11, -15, false, 0 },
  { AP_MOUND, AL_RAMP, -80, -138, 0, 18, 1.2f, 12, 10, false, 0 },
  { AP_MOUND, AL_RAMP, 10, -152, 0, 16, 0.8f, 11, -30, false, 0 },
  { AP_MOUND, AL_RAMP, 66, -140, 0, 18, 1.0f, 12, 25, false, 0 },
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
  BOX(AL_FEATURE, -54.5f, -50, 0, 91, 7, 5, 0, CONC), BOX(AL_FEATURE, 54.5f, -50, 0, 91, 7, 5, 0, CONC),
  /* its parapet: merlons along the top, a dark band at the waterline of old storms, lamps */
  DECO(AL_FEATURE, -54.5f, -51.5f, 7, 91, 1.2f, 2, 0, 0x8a867c), DECO(AL_FEATURE, 54.5f, -51.5f, 7, 91, 1.2f, 2, 0, 0x8a867c),
  DECO(AL_PAINT, -54.5f, -52.55f, 0.4f, 91, 1.6f, 0.05f, 0, 0x4a4c48), DECO(AL_PAINT, 54.5f, -52.55f, 0.4f, 91, 1.6f, 0.05f, 0, 0x4a4c48),
  DECO(AL_GLOW, -30, -52.6f, 5.6f, 0.8f, 0.8f, 0.2f, 0, 0xffd890), DECO(AL_GLOW, 30, -52.6f, 5.6f, 0.8f, 0.8f, 0.2f, 0, 0xffd890),
  DECO(AL_GLOW, -75, -52.6f, 5.6f, 0.8f, 0.8f, 0.2f, 0, 0xffd890), DECO(AL_GLOW, 75, -52.6f, 5.6f, 0.8f, 0.8f, 0.2f, 0, 0xffd890),
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
  BOX(AL_FEATURE, -25.5f, 256, 18, 31, 6, 2, 0, CONC), BOX(AL_FEATURE, 25.5f, 256, 18, 31, 6, 2, 0, CONC),
  BOX(AL_FEATURE, -40, 289, 18, 2, 6, 64, 0, CONC), BOX(AL_FEATURE, 40, 289, 18, 2, 6, 64, 0, CONC), BOX(AL_FEATURE, 0, 320, 18, 82, 6, 2, 0, CONC),
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
  .half_x = 100, .half_z = 330, .open = true,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = ATT, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_ASSAULT,
  .assault = &ASSAULT,
  .look = {
    [AL_FLOOR] = { TEX_GROUND_SAND, { 0.92f, 0.84f, 0.68f }, 1.15f, 9.0f, 0.9f, 0 },
    [AL_WALL] = { TEX_GROUND_ROCK, { 0.62f, 0.62f, 0.6f }, 1.4f, 14.0f, 0.95f, 0 },
    [AL_COVER] = { TEX_WALL_CONCRETE, { 0.78f, 0.76f, 0.72f }, 1.3f, 4.0f, 0.85f, 0 },
    [AL_DECK] = { TEX_GROUND_GRASS, { 0.5f, 0.6f, 0.38f }, 1.2f, 9.0f, 0.95f, 0 },
    [AL_RAMP] = { TEX_ROAD_SAND, { 0.95f, 0.86f, 0.68f }, 1.15f, 8.0f, 0.9f, 0 }, /* sand: the dunes, the tracks up the cliff */
    [AL_TRIM] = { TEX_WALL_CONCRETE, { 0.7f, 0.7f, 0.68f }, 1.3f, 2.0f, 0.9f, 0 },
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
  float side = dx - 150.0f; /* (the land rises behind the cliffs' blocks, so its coarse slope never shows in front of them) */
  float h = -0.05f;
  if (lz < -326.0f) h = -1.5f - fminf((-326.0f - lz) * 0.08f, 6.0f); /* the sea bed past the beach (under the water from its edge) */
  if (side > -18.0f) {
    /* the headlands: level with the cliffs' tops (30) just behind them, rolling on, sinking to the sea round the bay */
    float sea = lz < -330.0f ? fmaxf(0.0f, 1.0f - (-330.0f - lz) / 160.0f) : 1.0f;
    h = fmaxf(h, (29.0f + ground_noise(x, z, 90, 10, 3, 41) * fminf(fmaxf(side, 0) / 60.0f, 1.0f)) * sea);
  }
  if (lz > 380.0f) h = fmaxf(h, 30.0f + ground_noise(x, z, 80, 8, 3, 43) * fminf((lz - 340.0f) / 60.0f, 1.0f));
  return h;
}

/* what a prop stands on: the cliffs' tops where they are (30 m), else the land */
static float land_at(float x, float z) {
  float lz = z - ARENA_CZ, g = coast_ground(x, z, NULL);
  bool cliff = (fabsf(x) > 100.0f && fabsf(x) < 200.0f && lz > -350.0f) || (lz > 335.0f && lz < 435.0f && fabsf(x) < 200.0f);
  return cliff ? fmaxf(g, 30.0f) : g;
}

/* the ledges' tops (x, y, z, length): grass and gorse on them */
static const float LEDGE_TOPS[][4] = {
  { -98.87f, 14.85f, -337.1f, 17.4f },
  { -99.24f, 17.40f, -332.3f, 16.0f },
  { -98.77f, 25.06f, -228.9f, 20.5f },
  { -99.10f, 22.51f, -234.3f, 23.1f },
  { -99.12f, 27.99f, -206.3f, 20.8f },
  { -99.20f, 28.13f, -163.8f, 22.8f },
  { -99.01f, 13.46f, -122.0f, 44.6f },
  { -99.00f, 25.20f, -88.5f, 27.2f },
  { -98.82f, 24.17f, -62.2f, 13.6f },
  { -99.23f, 13.33f, -43.7f, 24.1f },
  { -99.19f, 16.90f, 4.7f, 19.2f },
  { -98.95f, 11.46f, 3.2f, 23.2f },
  { -99.16f, 10.57f, 32.7f, 31.5f },
  { -98.93f, 24.71f, 82.3f, 27.5f },
  { -99.16f, 24.95f, 84.4f, 26.4f },
  { -99.23f, 26.63f, 113.6f, 28.5f },
  { -99.02f, 26.68f, 142.7f, 12.7f },
  { -99.13f, 28.08f, 209.2f, 18.8f },
  { -98.94f, 27.58f, 210.2f, 25.0f },
  { -98.80f, 26.42f, 238.5f, 28.8f },
  { -99.12f, 27.88f, 286.3f, 19.5f },
  { -98.99f, 27.87f, 295.6f, 23.8f },
  { 98.73f, 11.89f, -330.1f, 39.0f },
  { 99.02f, 26.51f, -324.2f, 27.4f },
  { 99.06f, 7.14f, -289.1f, 24.4f },
  { 98.79f, 8.46f, -256.5f, 14.3f },
  { 99.16f, 16.76f, -226.3f, 37.3f },
  { 99.25f, 9.26f, -227.5f, 29.0f },
  { 99.22f, 10.07f, -189.9f, 22.3f },
  { 98.87f, 27.46f, -133.1f, 25.7f },
  { 98.81f, 23.16f, -12.7f, 27.7f },
  { 99.06f, 17.93f, -11.3f, 17.6f },
  { 99.11f, 29.09f, 26.7f, 31.4f },
  { 98.99f, 28.46f, 19.3f, 20.0f },
  { 99.05f, 27.71f, 69.7f, 38.3f },
  { 98.94f, 14.37f, 62.6f, 42.6f },
  { 99.20f, 23.08f, 65.1f, 41.6f },
  { 99.19f, 21.30f, 110.5f, 19.1f },
  { 99.19f, 22.61f, 111.0f, 14.3f },
  { 99.21f, 28.12f, 144.4f, 35.5f },
  { 99.20f, 25.45f, 148.3f, 26.9f },
  { 98.77f, 28.65f, 181.2f, 23.0f },
  { 98.75f, 26.11f, 218.3f, 21.9f },
  { 98.77f, 28.20f, 213.0f, 28.6f },
  { 99.16f, 25.75f, 284.6f, 30.8f },
  { 98.93f, 25.84f, 286.8f, 33.0f },
  { 99.07f, 29.66f, 316.9f, 31.1f },
};

static void breakwater_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1400.0f;
  g.height = coast_ground;
  g.tex = TEX_GROUND_GRASS;
  g.tex_repeat = 10.0f;
  g.tint = pc_mul(hex(0x6a8a48), 1.3f);
  g.tint2 = pc_mul(hex(0x8a8a60), 1.25f);
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
  water_surface(scene, 0, ARENA_CZ - 950.0f, 2400.0f, 1300.0f, -0.6f, &w); /* (in over the shallows, under the beach) */

  prop_set *ps = ps_new(scene, "CoastProps");
  ps_style(ps, PK_BOULDERS, TEX_GROUND_ROCK, hex(0xa8a49a), 1.0f);
  ps_style(ps, PK_ROCK, TEX_GROUND_ROCK, hex(0x9c9a92), 1.0f);
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  /* rocks along the headlands and the cliff's foot outside the walls */
  for (int i = 0; i < (split ? 14 : 28); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 20 + (float)(i / 2) * 48.0f + 16 * hf(2100u + (uint32_t)i), x = side * (hx + 8 + 20 * hf(2110u + (uint32_t)i));
    ps_add(ps, i % 3 ? PK_BOULDERS : PK_ROCK, (t3_vec3){ x, land_at(x, z + cz) - 0.4f, z + cz }, hf(2120u + (uint32_t)i) * 6.28f, 1.4f + 1.2f * hf(2130u + (uint32_t)i), 2140u + (uint32_t)i);
  }
  /* gorse and grass along the cliffs' rims */
  for (int i = 0; i < (split ? 30 : 70); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 10 + (float)(i / 2) * (2 * hz / (float)((split ? 30 : 70) / 2)) + 6 * hf(2500u + (uint32_t)i);
    float x = side * (hx + 2.0f + 14.0f * hf(2510u + (uint32_t)i));
    ps_add(ps, i % 3 ? PK_BUSH : PK_GRASS, (t3_vec3){ x, 30.0f, z + cz }, hf(2520u + (uint32_t)i) * 6.28f, 1.2f + 0.9f * hf(2530u + (uint32_t)i), 2540u + (uint32_t)i);
  }
  /* grass and gorse on the cliff faces' wider ledges */
  for (int i = 0; i < (int)(sizeof LEDGE_TOPS / sizeof LEDGE_TOPS[0]); i += split ? 2 : 1) {
    const float *L = LEDGE_TOPS[i];
    for (int k = 0; k < 3; k++) {
      float z = L[2] + (hf(2700u + (uint32_t)(i * 3 + k)) - 0.5f) * L[3] * 0.8f;
      ps_add(ps, k == 1 ? PK_BUSH : PK_GRASS, (t3_vec3){ L[0], L[1], z + cz }, hf(2710u + (uint32_t)(i * 3 + k)) * 6.28f, 0.6f + 0.4f * hf(2720u + (uint32_t)(i * 3 + k)), 2730u + (uint32_t)(i * 3 + k));
    }
  }
  /* gorse and rocks along the backs of the terraces */
  for (int i = 0; i < (split ? 12 : 30); i++) {
    int tier = i % 3;
    float z = (tier == 0 ? 106.0f : tier == 1 ? 146.0f : 320.0f) + cz, x = -92.0f + 184.0f * hf(2600u + (uint32_t)i);
    if (tier == 2 && fabsf(x) < 46.0f) x += x < 0 ? -46.0f : 46.0f; /* (not in the bunker) */
    ps_add(ps, i % 4 ? PK_BUSH : PK_ROCK, (t3_vec3){ x, 6.0f * (float)(tier + 1), z }, hf(2610u + (uint32_t)i) * 6.28f, 1.0f + 0.8f * hf(2620u + (uint32_t)i), 2630u + (uint32_t)i);
  }
  /* boulders fallen at the cliffs' feet, inside the cove */
  for (int i = 0; i < (split ? 10 : 22); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + 30 + (float)(i / 2) * 58.0f + 20 * hf(2150u + (uint32_t)i);
    ps_add(ps, i % 2 ? PK_BOULDERS : PK_ROCK, (t3_vec3){ side * (hx - 1.0f), -0.3f, z + cz }, hf(2160u + (uint32_t)i) * 6.28f, 0.9f + 0.7f * hf(2170u + (uint32_t)i), 2180u + (uint32_t)i);
  }
  /* the breakwater itself: a long rubble arm out into the sea, and wrecks and buoys off the beach */
  for (int i = 0; i < (split ? 10 : 20); i++) {
    float x = 120.0f + (float)i * 9.0f, z = -hz - 30.0f - (float)i * 14.0f + cz;
    ps_add(ps, PK_BOULDERS, (t3_vec3){ x, -1.4f, z }, hf(2200u + (uint32_t)i) * 6.28f, 1.6f + 0.6f * hf(2210u + (uint32_t)i), 2220u + (uint32_t)i);
  }
  ps_add(ps, PK_WRECK_TRUCK, (t3_vec3){ -40, coast_ground(-40, -hz - 80 + cz, NULL) - 0.3f, -hz - 80 + cz }, 0.7f, 2.2f, 2301u);
  ps_add(ps, PK_WRECK_TRUCK, (t3_vec3){ 70, coast_ground(70, -hz - 140 + cz, NULL) - 0.3f, -hz - 140 + cz }, 2.4f, 2.6f, 2302u);
  /* the lighthouse and the radio masts on the plateau, pines along the headlands */
  ps_add(ps, PK_SILO, (t3_vec3){ -150, land_at(-150, cz - 260), cz - 260 }, 0.0f, 1.2f, 2310u);
  for (int i = 0; i < 4; i++) {
    float x = -60.0f + (float)i * 40.0f, z = hz + 30 + cz;
    ps_add(ps, PK_ANTENNA, (t3_vec3){ x, land_at(x, z) - 0.3f, z }, 0, 1.6f, 2320u + (uint32_t)i);
  }
  for (int i = 0; i < (split ? 18 : 40); i++) {
    float side = i & 1 ? 1.0f : -1.0f, x = side * (hx + 30 + 140 * hf(2400u + (uint32_t)i)), z = (hf(2410u + (uint32_t)i) * 2 - 1) * hz * 0.9f + cz + 60;
    ps_add(ps, PK_PINE, (t3_vec3){ x, land_at(x, z) - 0.2f, z }, hf(2420u + (uint32_t)i) * 6.28f, 1.0f + 0.5f * hf(2430u + (uint32_t)i), 2440u + (uint32_t)i);
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
