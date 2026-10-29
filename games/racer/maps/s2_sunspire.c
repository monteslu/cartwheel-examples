/* assault map: Sunspire Keep. A temple-fortress on a jungle hill, a crystal burning at the top of its pyramid. 660 m from
 * the attackers' camp to the crystal:
 *
 *   the cliff        a wall of rock right across the valley with one old tunnel through it, choked with rubble and a
 *                    demolition charge left at its mouth: shoot the charge and the rubble goes
 *   the valley       jungle and ruins down to the keep's moat road; the drawbridge stands up against the gatehouse,
 *                    held by two great counterweights on chains at the moat's edge: break both and it comes down
 *   the courtyard    up on the keep's terrace, an altar in the middle of it: hold it with nobody of theirs on it
 *   the sanctum      the pyramid at the back, its stair barred until the altar is held, a second tier up the east side,
 *                    and the crystal on top: destroy it
 * Gun nests at every stage. An assault map only (arena_def.modes). */
#include <math.h>

#include "../arena.h"
#include "../props.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define RAMP(look, x, z, y, w, h, d, yaw) { AP_RAMP, look, x, z, y, w, h, d, yaw, false, 0 }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
#define DECK(x, z, y, w, h, d) { AP_BOX, AL_DECK, x, z, y, w, h, d, 0, false, 0, 1.3f }
#define GBOX(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, false, col, 0, g }
#define GDECO(look, x, z, y, w, h, d, col, g) { AP_BOX, look, x, z, y, w, h, d, 0, true, col, 0, g }
#define GPOST(look, x, z, y, dia, h, col, g) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col, 0, g }
#define GGLOWPOST(x, z, y, dia, h, col, g) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col, 0, g }

#define MOSS 0x7f8a62
#define SAND 0xc9b48a
#define WOOD 0x6e4a2c
#define BRONZE 0x8a6a3a
#define RED 0xff3a20
#define EMBER 0xff7a20
#define SUN 0xffd060

enum { O_CHARGE, O_NEST_C1, O_NEST_C2, O_WEIGHT_W, O_WEIGHT_E, O_NEST_V, O_ALTAR, O_NEST_K1, O_NEST_K2, O_CRYSTAL, O_NEST_P1, O_NEST_P2 };

/* a gun nest: a stone ring, the gun and a red lamp */
#define NEST(o, x, z, y)                                                                                               \
  GPOST(AL_COVER, x, z, y, 4.6f, 1.1f, MOSS, GATE_OBJ(o)), GPOST(AL_FEATURE, x, z, y + 1.1f, 1.7f, 1.3f, BRONZE, GATE_OBJ(o)), \
  GDECO(AL_FEATURE, x, z - 1.6f, y + 1.8f, 0.5f, 0.5f, 3.0f, BRONZE, GATE_OBJ(o)), GGLOWPOST(x, z, y + 2.4f, 0.5f, 0.5f, RED, GATE_OBJ(o))

/* a counterweight: a stone block on its chain, and the chain's links up to the gatehouse */
#define WEIGHT(o, x)                                                                                                   \
  GPOST(AL_FEATURE, x, -26, 0, 6, 5, MOSS, GATE_OBJ(o)), GDECO(AL_FEATURE, x, -26, 5, 0.8f, 9, 0.8f, BRONZE, GATE_OBJ(o)),  \
  GDECO(AL_FEATURE, x * 0.6f, -12, 13, ((x) < 0 ? -(x) : (x)) * 0.8f + 1, 0.6f, 0.6f, BRONZE, GATE_OBJ(o)),                             \
  GGLOWPOST(x, -26, 2.5f, 6.2f, 0.25f, EMBER, GATE_OBJ(o))

static const arena_prim PRIMS[] = {
  /* the camp: tents of crates, a ridge to jump */
  BOX(AL_COVER, -50, -300, 0, 10, 2.5f, 6, 15, WOOD), BOX(AL_COVER, 52, -296, 0, 8, 2.5f, 8, -10, WOOD),
  RAMP(AL_RAMP, 0, -262, 0, 30, 1.2f, 8, 0), RAMP(AL_RAMP, 0, -254, 0, 30, 1.2f, 8, 180),
  POST(AL_COVER, -30, -236, 0, 3, 3, MOSS), POST(AL_COVER, 34, -230, 0, 3.5f, 3, MOSS), POST(AL_COVER, -62, -222, 0, 4, 3, MOSS),
  POST(AL_COVER, 64, -244, 0, 3, 3, MOSS),
  /* the cliff and its tunnel, the rubble in its mouth and the charge in front of it */
  BOX(AL_FEATURE, -49.5f, -185, 0, 81, 16, 30, 0, 0), BOX(AL_FEATURE, 49.5f, -185, 0, 81, 16, 30, 0, 0),
  BOX(AL_FEATURE, 0, -185, 7, 18, 9, 30, 0, 0),
  DECO(AL_GLOW, -8.6f, -185, 5.6f, 0.2f, 0.4f, 26, 0, EMBER), DECO(AL_GLOW, 8.6f, -185, 5.6f, 0.2f, 0.4f, 26, 0, EMBER),
  GBOX(AL_FEATURE, 0, -198.5f, 0, 18, 7, 3, MOSS, GATE_OBJ(O_CHARGE)), GBOX(AL_COVER, -3, -203, 0, 3, 1.6f, 2.4f, WOOD, GATE_OBJ(O_CHARGE)),
  GBOX(AL_COVER, 2.5f, -202.5f, 0, 2.4f, 2.2f, 2.4f, WOOD, GATE_OBJ(O_CHARGE)), GGLOWPOST(0, -203.5f, 2.4f, 0.6f, 0.6f, RED, GATE_OBJ(O_CHARGE)),
  NEST(O_NEST_C1, -40, -215, 0), NEST(O_NEST_C2, 40, -215, 0),
  /* the valley: ruins, fallen columns, a stream bed */
  POST(AL_COVER, -20, -140, 0, 3, 6, SAND), POST(AL_COVER, 24, -126, 0, 3, 4, SAND), BOX(AL_COVER, -56, -110, 0, 14, 3, 4, 30, SAND),
  BOX(AL_COVER, 52, -96, 0, 4, 2, 16, -20, SAND), POST(AL_COVER, -8, -84, 0, 3, 5, SAND), POST(AL_COVER, 38, -66, 0, 3, 3, SAND),
  RAMP(AL_RAMP, -44, -70, 0, 12, 1.4f, 8, 0), RAMP(AL_RAMP, -44, -62, 0, 12, 1.4f, 8, 180),
  DECO(AL_PAINT, 0, -100, 0, 180, 0.02f, 6, 8, 0x3c6a6a),
  NEST(O_NEST_V, -30, -50, 0),
  /* the keep's terrace, the ramp up to the gate, the gatehouse, the drawbridge standing up across it, the battlements */
  DECK(0, 165, 0, 180, 8, 330),
  RAMP(AL_RAMP, 0, -20, 0, 16, 8, 40, 0),
  BOX(AL_WALL, -13, 5, 8, 6, 12, 8, 0, 0), BOX(AL_WALL, 13, 5, 8, 6, 12, 8, 0, 0), BOX(AL_WALL, 0, 5, 18, 32, 3, 8, 0, 0),
  GLOWPOST(-13, 0.8f, 16, 1.2f, 1.2f, SUN), GLOWPOST(13, 0.8f, 16, 1.2f, 1.2f, SUN),
  GBOX(AL_FEATURE, 0, 1.6f, 8, 20, 10, 1.4f, WOOD, GATE_STAGE(1)), GDECO(AL_TRIM, 0, 0.8f, 12, 20, 0.6f, 0.1f, 0, GATE_STAGE(1)),
  BOX(AL_WALL, -53, 3, 8, 74, 3.5f, 2, 0, 0), BOX(AL_WALL, 53, 3, 8, 74, 3.5f, 2, 0, 0),
  WEIGHT(O_WEIGHT_W, -30), WEIGHT(O_WEIGHT_E, 30),
  /* the courtyard: the altar, pillars, statues on plinths */
  POST(AL_FEATURE, 0, 110, 8, 5, 1.2f, SAND), GLOWPOST(0, 110, 9.2f, 2.4f, 0.3f, SUN),
  POST(AL_COVER, -30, 70, 8, 3, 7, SAND), POST(AL_COVER, 30, 70, 8, 3, 7, SAND), POST(AL_COVER, -30, 150, 8, 3, 7, SAND), POST(AL_COVER, 30, 150, 8, 3, 7, SAND),
  BOX(AL_COVER, -62, 110, 8, 6, 3, 18, 0, MOSS), BOX(AL_COVER, 62, 110, 8, 6, 3, 18, 0, MOSS),
  NEST(O_NEST_K1, -45, 140, 8), NEST(O_NEST_K2, 45, 140, 8),
  /* the sanctum: the pyramid's lower tier, its stair (barred until the altar is held), the upper tier up the east side, the crystal */
  DECK(0, 270, 8, 80, 5, 80),
  RAMP(AL_RAMP, 0, 217.5f, 8, 16, 5, 25, 0),
  GBOX(AL_FEATURE, 0, 205.5f, 8, 18, 5, 1.5f, BRONZE, GATE_STAGE(2)), GDECO(AL_GLOW, 0, 204.6f, 11.5f, 14, 0.4f, 0.1f, SUN, GATE_STAGE(2)),
  RAMP(AL_RAMP, 30, 252, 13, 14, 5, 24, 0),
  DECK(10, 282, 13, 60, 5, 36),
  GPOST(AL_FEATURE, 0, 285, 18, 3.2f, 1.6f, BRONZE, GATE_OBJ(O_CRYSTAL)), GPOST(AL_GLOW, 0, 285, 19.6f, 2.4f, 4.4f, 0x7affd0, GATE_OBJ(O_CRYSTAL)),
  GGLOWPOST(0, 285, 18.1f, 8, 0.2f, 0x7affd0, GATE_OBJ(O_CRYSTAL)),
  NEST(O_NEST_P1, -30, 242, 13), NEST(O_NEST_P2, -32, 300, 13),
  /* the slag round the pyramid's foot (paint: the fire is the scenery's) */
  DECO(AL_GLOW, 0, 228.5f, 8.02f, 84, 0.04f, 1.2f, 0, EMBER), DECO(AL_GLOW, -41.5f, 270, 8.02f, 1.2f, 0.04f, 84, 0, EMBER),
  DECO(AL_GLOW, 41.5f, 270, 8.02f, 1.2f, 0.04f, 84, 0, EMBER),
};

static const assault_obj OBJS[] = {
  [O_CHARGE] = { AO_DESTROY, 0, 0, -203, 0, 4.0f, 3.5f, 50, "THE DEMOLITION CHARGE" },
  [O_NEST_C1] = { AO_TURRET, 0, -40, -215, 0, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_NEST_C2] = { AO_TURRET, 0, 40, -215, 0, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_WEIGHT_W] = { AO_DESTROY, 1, -30, -26, 0, 3.2f, 5.5f, 45, "THE WEST COUNTERWEIGHT" },
  [O_WEIGHT_E] = { AO_DESTROY, 1, 30, -26, 0, 3.2f, 5.5f, 45, "THE EAST COUNTERWEIGHT" },
  [O_NEST_V] = { AO_TURRET, 1, -30, -50, 0, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_ALTAR] = { AO_HOLD, 2, 0, 110, 8, 11.0f, 0, 8, "THE ALTAR" },
  [O_NEST_K1] = { AO_TURRET, 2, -45, 140, 8, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_NEST_K2] = { AO_TURRET, 2, 45, 140, 8, 2.4f, 3.0f, 40, "A GUN NEST", 70 },
  [O_CRYSTAL] = { AO_DESTROY, 3, 0, 285, 18, 2.8f, 6.0f, 70, "THE SUN CRYSTAL" },
  [O_NEST_P1] = { AO_TURRET, 3, -30, 242, 13, 2.4f, 3.0f, 40, "A GUN NEST", 60 },
  [O_NEST_P2] = { AO_TURRET, 3, -32, 300, 13, 2.4f, 3.0f, 40, "A GUN NEST", 60 },
};

static const float ATT[4 * 4][3] = {
  { -20, -310, 0 }, { 20, -310, 0 }, { -45, -300, 0 }, { 45, -300, 0 },
  { -20, -150, 0 }, { 20, -150, 0 }, { -45, -140, 0 }, { 45, -140, 0 },
  { -20, 25, 0 }, { 20, 25, 0 }, { -42, 30, 0 }, { 42, 30, 0 },
  { -25, 170, 0 }, { 25, 170, 0 }, { -50, 180, 0 }, { 50, 180, 0 },
};
static const float DEF[4 * 4][3] = {
  { -20, -150, 180 }, { 20, -150, 180 }, { -50, -140, 180 }, { 50, -140, 180 },
  { -20, -60, 180 }, { 20, -60, 180 }, { -55, -50, 180 }, { 55, -50, 180 },
  { -20, 196, 180 }, { 20, 196, 180 }, { -50, 190, 180 }, { 50, 190, 180 },
  { -62, 250, 180 }, { 62, 250, 180 }, { -62, 290, 180 }, { 62, 290, 180 },
};

static const assault_def ASSAULT = {
  .time_s = 360,
  .objs = OBJS, .nobjs = sizeof OBJS / sizeof OBJS[0],
  .nstages = 4,
  .need = { 1, 2, 1, 1 },
  .stage_name = { "BLOW THE TUNNEL", "DROP THE DRAWBRIDGE", "HOLD THE ALTAR", "DESTROY THE CRYSTAL" },
  .att_spawns = ATT, .def_spawns = DEF,
};

static const float BOXES[][3] = {
  { -40, -270, 0 }, { 0, -280, 0 }, { 40, -270, 0 }, { -20, -225, 0 }, { 20, -225, 0 },
  { -30, -120, 0 }, { 30, -110, 0 }, { 0, -70, 0 }, { -55, -35, 0 }, { 55, -35, 0 },
  { -20, 40, 8 }, { 20, 40, 8 }, { -45, 90, 8 }, { 45, 90, 8 }, { 0, 180, 8 }, { -20, 200, 8 }, { 20, 200, 8 },
  { 20, 236, 13 }, { 10, 275, 18 },
};

static const arena_boost BOOSTS[] = { { 0, -235, 0, 8, 12, 0 }, { 0, -110, 0, 8, 12, 0 } };

static const float ROUTE[][2] = {
  { 0, -280 }, { -40, -240 }, { 40, -240 }, { 0, -210 }, { 0, -160 }, { 0, -120 }, { -40, -80 }, { 40, -80 }, { 0, -62 },
  { 0, -46 }, { 0, 8 }, { 0, 40 }, { -40, 80 }, { 40, 80 }, { 0, 150 }, { 0, 200 }, { 0, 233 }, { 30, 236 }, { 30, 268 }, { 5, 272 },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 0, 2 }, { 0, 3 }, { 1, 3 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 }, { 5, 7 }, { 6, 8 }, { 7, 8 }, { 8, 9 }, { 9, 10 },
  { 10, 11 }, { 11, 12 }, { 11, 13 }, { 12, 14 }, { 13, 14 }, { 11, 14 }, { 14, 15 }, { 15, 16 }, { 16, 17 }, { 17, 18 }, { 18, 19 },
};

static const arena_def ARENA = {
  .half_x = 90, .half_z = 330, .wall_h = 9.0f,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = ATT, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_ASSAULT,
  .assault = &ASSAULT,
  .look = {
    [AL_FLOOR] = { TEX_GROUND_GRASS, { 0.6f, 0.78f, 0.42f }, 1.2f, 9.0f, 0.95f, 0 },
    [AL_WALL] = { TEX_WALL_STONE, { 0.74f, 0.7f, 0.58f }, 1.35f, 6.0f, 0.95f, 0 },
    [AL_COVER] = { TEX_WALL_STONE, { 0.8f, 0.76f, 0.62f }, 1.35f, 4.0f, 0.95f, 0 },
    [AL_DECK] = { TEX_ROAD_STONE, { 0.82f, 0.76f, 0.6f }, 1.35f, 8.0f, 0.95f, 0 },
    [AL_RAMP] = { TEX_ROAD_STONE, { 0.86f, 0.8f, 0.64f }, 1.35f, 6.0f, 0.95f, 0 },
    [AL_TRIM] = { TEX_WALL_STONE, { 0.66f, 0.62f, 0.5f }, 1.3f, 2.0f, 0.95f, 0 },
    [AL_FEATURE] = { TEX_GROUND_ROCK, { 0.66f, 0.66f, 0.56f }, 1.4f, 9.0f, 0.95f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.7f, 0.4f }, 1, 1, 1, 0, 3.0f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};

/* ── the jungle round it ──────────────────────────────────────────────────────────────────────────── */

static uint32_t hash_u(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hash_u(x) & 0xffffff) / 16777216.0f; }

static float jungle_ground(float x, float z, void *ud) {
  (void)ud;
  float lz = z - ARENA_CZ, side = fmaxf(fabsf(x) - 93.0f, fabsf(lz) - 333.0f);
  if (side <= 0) return -0.05f;
  /* the valley's sides up to the keep's terrace and on into hills, the cliff band running out across them */
  float h = fminf(side * 0.6f, 14.0f) + ground_noise(x, z, 90, 14, 3, 53) * fminf(side / 60.0f, 1.0f);
  if (fabsf(lz + 185.0f) < 20.0f) h = fmaxf(h, 16.0f - fabsf(lz + 185.0f) * 0.3f);
  if (lz > 0) h = fmaxf(h, 8.0f + fminf(side * 0.4f, 10.0f));
  return h;
}

static void sunspire_scenery(cw_node scene, const track *t, uint32_t seed) {
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  arena_gfx_build(scene, a, seed);
  ground_opts g = ground_defaults();
  g.cx = 0, g.cz = ARENA_CZ;
  g.half = 1400.0f;
  g.height = jungle_ground;
  g.tex = TEX_GROUND_GRASS;
  g.tex_repeat = 9.0f;
  g.tint = pc_mul(hex(0x6a8a3a), 1.3f);
  g.tint2 = pc_mul(hex(0x4a6a2a), 1.3f);
  g.near_cell = split ? 96.0f : 64.0f;
  g.detail_range = 40.0f;
  g.follow_road = false;
  g.darken_edge = 0;
  g.seed = 83;
  ground_plane(scene, t, &g);

  prop_set *ps = ps_new(scene, "JungleProps");
  float hx = a->half_x, hz = a->half_z, cz = ARENA_CZ;
  /* the jungle on the slopes either side, thick, and ferns at its edge */
  for (int i = 0; i < (split ? 30 : 70); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = -hz + (float)(i / 2) * (2 * hz / (float)((split ? 30 : 70) / 2)) + 8 * hf(3100u + (uint32_t)i);
    float x = side * (hx + 8 + 60 * hf(3110u + (uint32_t)i));
    ps_add(ps, i % 3 ? PK_JUNGLE_TREE : PK_PALM, (t3_vec3){ x, jungle_ground(x, z + cz, NULL) - 0.3f, z + cz }, hf(3120u + (uint32_t)i) * 6.28f, 1.1f + 0.7f * hf(3130u + (uint32_t)i), 3140u + (uint32_t)i);
  }
  for (int i = 0; i < (split ? 20 : 50); i++) {
    float side = i & 1 ? 1.0f : -1.0f, z = (hf(3200u + (uint32_t)i) * 2 - 1) * hz, x = side * (hx + 4 + 6 * hf(3210u + (uint32_t)i));
    ps_add(ps, PK_FERN, (t3_vec3){ x, jungle_ground(x, z + cz, NULL), z + cz }, hf(3220u + (uint32_t)i) * 6.28f, 1.0f + 0.6f * hf(3230u + (uint32_t)i), 3240u + (uint32_t)i);
  }
  /* ruins in the valley and statues on the terrace's edge, torches along the battlements */
  ps_add(ps, PK_RUIN_ARCH, (t3_vec3){ -70, 0, -150 + cz }, 0.3f, 1.4f, 3301u);
  ps_add(ps, PK_RUIN_ARCH, (t3_vec3){ 72, 0, -40 + cz }, -0.2f, 1.2f, 3302u);
  for (int i = 0; i < 4; i++) ps_add(ps, PK_STATUE, (t3_vec3){ i & 1 ? 80.0f : -80.0f, 8.0f, 60.0f + (float)(i / 2) * 120.0f + cz }, i & 1 ? -1.57f : 1.57f, 1.6f, 3310u + (uint32_t)i);
  for (int i = 0; i < 8; i++) ps_add(ps, PK_TORCH, (t3_vec3){ -84.0f + (float)i * 24.0f, 11.5f, 3.0f + cz }, 0, 1.0f, 3320u + (uint32_t)i);
  for (int i = 0; i < (split ? 6 : 12); i++) {
    float ang = hf(3400u + (uint32_t)i) * 6.2831853f, r = 500.0f + 400.0f * hf(3410u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r + cz;
    ps_add(ps, i & 1 ? PK_TEMPLE_BLOCK : PK_JUNGLE_TREE, (t3_vec3){ x, jungle_ground(x, z, NULL) - 1.0f, z }, hf(3420u + (uint32_t)i) * 6.28f, 2.0f + 2.0f * hf(3430u + (uint32_t)i), 3440u + (uint32_t)i);
  }
  ps_finish(ps);
  fx_ambient_follow(scene, FXA_DUST, split ? 0.2f : 0.4f);
}

const map_def map_sunspire = {
  .name = "Sunspire Keep",
  .blurb = "Blow the tunnel, drop the drawbridge, hold the altar and destroy the crystal on the pyramid.",
  .scene = "scenes/temple.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.8f, 0.76f, 0.6f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.74f, 0.7f, 0.58f },
            .wall_top = { 0.8f, 0.76f, 0.62f }, .road_tex = TEX_ROAD_STONE, .wall_tex = TEX_WALL_STONE, .road_rough = 0.9f },
  .laps = 1,
  .par_lap = 360,
  .scenery = sunspire_scenery,
  .arena = &ARENA,
  .music = 6,
};
