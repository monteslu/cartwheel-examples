/* capture the flag map: Twin Spires. A tribute to a classic capture-the-flag map of the late nineties: one long asteroid
 * adrift in space, a huge square stone tower on a broad rock island at each end, and between them open rock where two wide
 * paths rise to a crossing in the middle and part again, a double X with the void showing through the two diamonds
 * between them. A black sky full of stars, a great blue-white world beside it and a galaxy glowing behind the far tower.
 * Nothing is taken from it: its proportions were measured and the whole thing drawn again, scaled up for karts (the flags
 * 492 m apart, the paths 34 m wide), from our own shapes and textures.
 *
 *   the islands  broad jagged rock round each tower, the team's starts in front of its gate
 *   the towers   a gatehouse at the foot (a lit arch 24 m wide) with the flag inside in the hall, the tower rising 140 m
 *                behind it, a ledge and battlements and a turret at each corner of the roof. A teleport pad in the hall
 *                goes up to the roof (the whole asteroid below); the pad at the roof's front throws you back down onto
 *                the island at speed
 *   the paths    from each island two paths fork, bow out 55 m to each side and rise 20 m to the crossing in the middle
 *                (arena_terrain: one rock, its slopes driven), then part again to the far island. A rough rim of rock
 *                runs along every edge; over it is the void
 * The rock hangs over nothing (arena_def.open): a kart knocked off falls past the kill plane and comes back at its base.
 * Red holds the north island (+z), blue the south; every red shape has its blue twin turned half round. A capture the
 * flag map only (arena_def.modes). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../arena.h"
#include "../props.h"
#include "../props_gen.h"
#include "cartwheel/math.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
/* the battlements round the roof: over a kart's hull, which rides up anything lower */
#define RAIL_H 1.7f

#define RED 0xff3b24
#define BLUE 0x2f74ff
#define WARM 0xffb15a

#define BASE_Z 246.0f   /* the flags, each side of the middle (inside the gatehouse's hall) */
#define ROOF_Y 184.0f   /* the towers' roofs */
#define SHAFT_Z 298.0f  /* the middle of each tower */

/* ── the rock ─────────────────────────────────────────────────────────────────────────────────────────
 * Worked out on the red half (z >= 0) and turned half round for blue, so the two ends are the same and the seam at z = 0
 * (the crossing, where both paths are on the middle line and nothing else is) matches itself. Plain arithmetic and the
 * engine's own sine only: the sim builds its collision from this on every host. */

#define PATH_A 55.0f   /* how far each path bows out from the middle line */
#define PATH_L 160.0f  /* from the crossing to where a half's two paths meet at its island */
#define PATH_W 17.0f   /* half a path's width */
#define HUMP 20.0f     /* the crossing's height over the islands */
#define HUMP_L 170.0f  /* how far out from the crossing the rise starts */

static float sat(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
static float smooth01(float t) {
  t = sat(t);
  return t * t * (3 - 2 * t);
}

/* value noise in -1..1 on the ground's plane (integer hashing and a smooth blend: every host the same) */
static float vn_hash(int ix, int iz, uint32_t seed) {
  uint32_t h = (uint32_t)ix * 73856093u ^ (uint32_t)iz * 19349663u ^ seed * 83492791u;
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return (float)(h & 0xffffu) / 32767.5f - 1.0f;
}
static float vnoise(float x, float z, uint32_t seed) {
  float fx = floorf(x), fz = floorf(z), u = x - fx, v = z - fz;
  int ix = (int)fx, iz = (int)fz;
  u = u * u * (3 - 2 * u), v = v * v * (3 - 2 * v);
  float a = vn_hash(ix, iz, seed), b = vn_hash(ix + 1, iz, seed), c = vn_hash(ix, iz + 1, seed), d = vn_hash(ix + 1, iz + 1, seed);
  float ab = a + (b - a) * u, cd = c + (d - c) * u;
  return ab + (cd - ab) * v;
}

/* how far (x, z) is inside a convex polygon (negative outside: the nearest edge's line), either winding */
static float poly_in(const float (*p)[2], int n, float x, float z) {
  float area = 0, in = 1e9f;
  for (int i = 0; i < n; i++) area += p[i][0] * p[(i + 1) % n][1] - p[(i + 1) % n][0] * p[i][1];
  float sg = area > 0 ? 1.0f : -1.0f;
  for (int i = 0; i < n; i++) {
    const float *a = p[i], *b = p[(i + 1) % n];
    float ex = b[0] - a[0], ez = b[1] - a[1], l = sqrtf(ex * ex + ez * ez);
    in = fminf(in, sg * ((z - a[1]) * ex - (x - a[0]) * ez) / l);
  }
  return in;
}

/* the red island: a broad slab round the tower, a spur reaching forward on its east side and one out behind on the west,
 * hard straight faces and sharp corners */
static const float ISLE[][2] = {
  { -60, 158 }, { -15, 146 }, { 40, 150 }, { 88, 172 }, { 108, 228 }, { 102, 292 },
  { 70, 338 }, { 0, 352 }, { -62, 340 }, { -100, 300 }, { -112, 238 }, { -95, 185 },
};
static const float SPUR_E[][2] = { { 70, 165 }, { 118, 128 }, { 150, 150 }, { 140, 196 }, { 105, 214 } };
static const float SPUR_W[][2] = { { -80, 290 }, { -118, 262 }, { -150, 298 }, { -128, 345 }, { -82, 340 } };
#define NP(a) (int)(sizeof a / sizeof a[0])

static float half_at(float x, float z, float *top) {
  /* the paths: the middle line of each runs straight out from the crossing (z 0) on the slant, bends round (a rounded
   * corner PATH_A out to the side) and runs straight in again to the island (z PATH_L), one east, one west: a diamond
   * of void between them, and through the crossing they carry straight on into the other half's (an X). The distance
   * across a path is measured square to its line. Their edges wander a little, never at the seam */
  const float e = 0.24f, norm = sqrtf(1.0f + e * e) - e;
  float v = 2.0f * fminf(z, PATH_L) / PATH_L - 1.0f, sa = sqrtf(v * v + e * e);
  float xc = PATH_A * (1.0f - (sa - e) / norm), sl = z < PATH_L ? -PATH_A / PATH_L * 2.0f * v / sa / norm : 0;
  float k = 1.0f / sqrtf(1.0f + sl * sl), fade = smooth01(z / 24.0f);
  float wa = fade * (2.2f * vnoise(x * 0.045f, z * 0.045f, 11) + 0.9f * vnoise(x * 0.17f, z * 0.17f, 12));
  float wb = fade * (2.2f * vnoise(x * 0.045f, z * 0.045f, 21) + 0.9f * vnoise(x * 0.17f, z * 0.17f, 22));
  float in = fmaxf(PATH_W + wa - fabsf(x - xc) * k, PATH_W + wb - fabsf(x + xc) * k);
  /* the island and its spurs, their faces broken a little */
  float isl = fmaxf(poly_in(ISLE, NP(ISLE), x, z), fmaxf(poly_in(SPUR_E, NP(SPUR_E), x, z), poly_in(SPUR_W, NP(SPUR_W), x, z)));
  isl += 1.6f * vnoise(x * 0.07f, z * 0.07f, 31) + 0.6f * vnoise(x * 0.23f, z * 0.23f, 32);
  in = fmaxf(in, isl);
  /* the rise: flat on the islands, up to the crossing, flat over its top; and over it all the rock's own lumps (a hand
   * high, a few metres across), smoothed away under the tower and its gatehouse so they stand square on it */
  float lumps = 0.3f * vnoise(x * 0.13f, z * 0.13f, 41) + 0.12f * vnoise(x * 0.33f, z * 0.33f, 42);
  float keep = sat((fabsf(x) - 40.0f) / 10.0f) + sat((214.0f - z) / 10.0f) + sat((z - 340.0f) / 10.0f);
  *top = HUMP * (1.0f - smooth01(z / HUMP_L)) + lumps * sat(keep) * smooth01(z / 24.0f);
  return in;
}

static float spires_at(float x, float z, float *top) { return z >= 0 ? half_at(x, z, top) : half_at(-x, -z, top); }

static const arena_terrain ROCK = {
  .x0 = -160, .z0 = -362, .x1 = 160, .z1 = 362, .cell = 2.5f, .at = spires_at,
  .lip = 1.1f, .depth = 11.0f, .belly = 0.5f,
  .top_col = 0xa08a6c, .side_col = 0x66594b, .tex = TEX_GROUND_ASH, .tex_m = 7.0f, .side_tex = TEX_GROUND_ROCK,
};

/* ── the towers ───────────────────────────────────────────────────────────────────────────────────────
 * S = 1 red (north), -1 blue (south), TC its team's light. The gatehouse in front (z 226 to 266 on red's side): an arch
 * 24 m wide and 15 m high between two piers, its walls and its ceiling round a hall 52 m wide; behind it the tower in
 * three stages, 64 m square up to a ledge at 96 m, 56 m to another at 156 m, 48 m to the roof at 184 m, a turret at each
 * corner of the roof */
#define TOWER(S, TC)                                                                                                   \
  BOX(AL_FEATURE, 22, S * 229, 0, 20, 26, 6, 0, 0), BOX(AL_FEATURE, -22, S * 229, 0, 20, 26, 6, 0, 0),                 \
  BOX(AL_FEATURE, 0, S * 229, 15, 24, 11, 6, 0, 0),                                                                   \
  BOX(AL_FEATURE, 29, S * 249, 0, 6, 26, 34, 0, 0), BOX(AL_FEATURE, -29, S * 249, 0, 6, 26, 34, 0, 0),                 \
  BOX(AL_FEATURE, 0, S * 249, 18, 52, 8, 34, 0, 0),                                                                   \
  DECO(AL_COVER, 0, S * 246, 26, 68, 2.2f, 44, 0, 0),                                                                 \
  BOX(AL_FEATURE, 0, S * SHAFT_Z, 0, 64, 96, 64, 0, 0),                                                               \
  DECO(AL_COVER, 0, S * SHAFT_Z, 92, 70, 4, 70, 0, 0),                                                                \
  BOX(AL_FEATURE, 0, S * SHAFT_Z, 96, 56, 60, 56, 0, 0),                                                              \
  DECO(AL_COVER, 0, S * SHAFT_Z, 152, 62, 4, 62, 0, 0),                                                               \
  BOX(AL_FEATURE, 0, S * SHAFT_Z, 156, 48, 27, 48, 0, 0),                                                             \
  { AP_BOX, AL_DECK, 0, S * SHAFT_Z, ROOF_Y - 1, 50, 1, 50, 0, false, 0, RAIL_H },                                    \
  BOX(AL_FEATURE, 21.5f, S * (SHAFT_Z - 21.5f), ROOF_Y, 7, 14, 7, 0, 0), BOX(AL_FEATURE, -21.5f, S * (SHAFT_Z - 21.5f), ROOF_Y, 7, 14, 7, 0, 0), \
  BOX(AL_FEATURE, 21.5f, S * (SHAFT_Z + 21.5f), ROOF_Y, 7, 14, 7, 0, 0), BOX(AL_FEATURE, -21.5f, S * (SHAFT_Z + 21.5f), ROOF_Y, 7, 14, 7, 0, 0), \
  /* the team's light: under the cornice, round the arch, bands up the tower's front corners and under its ledges, round   \
   * the turrets, along the hall's walls */                                                                            \
  DECO(AL_GLOW, 0, S * 225.9f, 23.4f, 64, 0.7f, 0.2f, 0, TC), DECO(AL_GLOW, 0, S * 225.9f, 14.3f, 24, 0.5f, 0.2f, 0, TC), \
  DECO(AL_GLOW, 12.2f, S * 225.9f, 0, 0.5f, 14.5f, 0.2f, 0, TC), DECO(AL_GLOW, -12.2f, S * 225.9f, 0, 0.5f, 14.5f, 0.2f, 0, TC), \
  DECO(AL_GLOW, 27, S * 265.9f, 30, 1.6f, 60, 0.2f, 0, TC), DECO(AL_GLOW, -27, S * 265.9f, 30, 1.6f, 60, 0.2f, 0, TC),   \
  DECO(AL_GLOW, 23, S * 269.9f, 100, 1.4f, 50, 0.2f, 0, TC), DECO(AL_GLOW, -23, S * 269.9f, 100, 1.4f, 50, 0.2f, 0, TC), \
  DECO(AL_GLOW, 0, S * 262.9f, 90.6f, 68, 0.8f, 0.2f, 0, TC), DECO(AL_GLOW, 0, S * 266.9f, 150.6f, 60, 0.8f, 0.2f, 0, TC), \
  DECO(AL_GLOW, 21.5f, S * (SHAFT_Z - 21.5f), ROOF_Y + 11, 7.3f, 0.6f, 7.3f, 0, TC),                                  \
  DECO(AL_GLOW, -21.5f, S * (SHAFT_Z - 21.5f), ROOF_Y + 11, 7.3f, 0.6f, 7.3f, 0, TC),                                 \
  DECO(AL_GLOW, 21.5f, S * (SHAFT_Z + 21.5f), ROOF_Y + 11, 7.3f, 0.6f, 7.3f, 0, TC),                                  \
  DECO(AL_GLOW, -21.5f, S * (SHAFT_Z + 21.5f), ROOF_Y + 11, 7.3f, 0.6f, 7.3f, 0, TC),                                 \
  DECO(AL_GLOW, 25.85f, S * 249, 7, 0.2f, 0.6f, 30, 0, TC), DECO(AL_GLOW, -25.85f, S * 249, 7, 0.2f, 0.6f, 30, 0, TC)

static const arena_prim PRIMS[] = {
  TOWER(1.0f, RED),
  TOWER(-1.0f, BLUE),
};

/* the boost strips: one into the head of each path where it leaves its island */
static const arena_boost BOOSTS[] = {
  { 9, 172, 0, 7, 11, 133 }, { -9, 172, 0, 7, 11, -133 },
  { -9, -172, 0, 7, 11, -47 }, { 9, -172, 0, 7, 11, 47 },
};

/* the teleport pads: up from the back west (red) or east (blue) corner of each hall to the back of its roof, facing the
 * asteroid; and
 * from the roof's front down onto the front of the island, at speed towards the middle */
static const arena_tele TELES[] = {
  { -20, 261, 0, 2.6f, 0, SHAFT_Z + 18, ROOF_Y, 180, 5, 0, RED },
  { 0, SHAFT_Z - 16, ROOF_Y, 2.6f, 0, 178, 0, 180, 24, 180, RED },
  { 20, -261, 0, 2.6f, 0, -SHAFT_Z - 18, ROOF_Y, 0, 5, 180, BLUE },
  { 0, -SHAFT_Z + 16, ROOF_Y, 2.6f, 0, -178, 0, 0, 24, 0, BLUE },
};

/* capture the flag: red before its gate, blue before its own, facing the paths (the battle starts, should anything ask,
 * are the same) */
static const float TEAM_SPAWNS[8][3] = {
  { -9, 206, 180 }, { 9, 206, 180 }, { -26, 210, 180 }, { 26, 210, 180 },
  { 9, -206, 0 }, { -9, -206, 0 }, { 26, -210, 0 }, { -26, -210, 0 },
};

/* the item boxes: on the islands, at the outer bend of each path (10.9 m up the rise), on the crossing and on the roofs */
static const float BOXES[][3] = {
  { 30, 186, 0 }, { -30, 186, 0 }, { -30, -186, 0 }, { 30, -186, 0 },
  { 55, 80, 10.9f }, { -55, 80, 10.9f }, { -55, -80, 10.9f }, { 55, -80, 10.9f },
  { 0, 0, HUMP },
  { 12, SHAFT_Z + 6, ROOF_Y }, { -12, SHAFT_Z + 6, ROOF_Y }, { -12, -SHAFT_Z - 6, ROOF_Y }, { 12, -SHAFT_Z - 6, ROOF_Y },
};

/* the CPU drivers' way round: each hall and its gate, the fork, the bends of the four paths, the crossing */
static const float ROUTE[][2] = {
  { 0, BASE_Z }, { 0, 212 }, { 0, 160 }, { 55, 80 }, { -55, 80 }, { 0, 0 },
  { -55, -80 }, { 55, -80 }, { 0, -160 }, { 0, -212 }, { 0, -BASE_Z },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 1, 2 }, { 2, 3 }, { 2, 4 }, { 3, 5 }, { 4, 5 }, { 5, 6 }, { 5, 7 }, { 6, 8 }, { 7, 8 }, { 8, 9 }, { 9, 10 },
};

static const arena_def ARENA = {
  .half_x = 150, .half_z = 352, .wall_h = 0,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = TEAM_SPAWNS, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { 0, BASE_Z }, { 0, -BASE_Z } },
  .team_spawns = TEAM_SPAWNS,
  .open = true,
  .fence = 2.6f,
  .teles = TELES, .nteles = sizeof TELES / sizeof TELES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_CTF,
  .terrain = &ROCK,
  .look = {
    [AL_FLOOR] = { TEX_GROUND_ROCK, { 0.42f, 0.38f, 0.34f }, 1.3f, 9.0f, 0.95f, 0 },
    [AL_WALL] = { TEX_WALL_STONE, { 0.45f, 0.42f, 0.4f }, 1.4f, 4.0f, 0.9f, 0 },
    [AL_COVER] = { TEX_WALL_CONCRETE, { 0.5f, 0.45f, 0.4f }, 1.3f, 4.0f, 0.9f, 0 },
    [AL_DECK] = { TEX_ROAD_STONE, { 0.5f, 0.47f, 0.44f }, 1.3f, 6.0f, 0.88f, 0 },
    [AL_RAMP] = { TEX_ROAD_STONE, { 0.5f, 0.47f, 0.44f }, 1.3f, 6.0f, 0.88f, 0 },
    [AL_TRIM] = { TEX_WALL_STONE, { 0.5f, 0.47f, 0.44f }, 1.4f, 2.0f, 0.9f, 0 },
    [AL_FEATURE] = { TEX_WALL_CONCRETE, { 0.4f, 0.35f, 0.31f }, 1.25f, 9.0f, 0.92f, 0 },
    [AL_GLOW] = { -1, { 1.0f, 0.75f, 0.45f }, 1, 1, 1, 0, 3.2f },
    [AL_PAINT] = { -1, { 1, 1, 1 }, 1.0f, 1, 0.8f, 0 },
  },
};
/* ── the sky ──────────────────────────────────────────────────────────────────────────────────────── */
/* Space, as Wreck Orbit draws it: a dome painted with a faint nebula, the stars as real points, the sun as additive
 * flare quads, and real spheres for the worlds, all about the arena's middle. Every texture is made here at load from
 * integer hashes and the shared value noise, so every host draws the same sky. */

#define SKY_R 1800.0f
static const t3_vec3 SKY_C = { 0, 0, ARENA_CZ };
/* towards the sun (the scene's Sun light shines the other way): high over the red end, a little west */
static const t3_vec3 SUN_DIR = { -0.45f, 0.62f, 0.64f };
/* the world: huge and low over the blue end, off to the east (red's view as it attacks, the sun behind it, so it is
 * nearly full); the galaxy low over the red end, behind red's tower (blue's view as it attacks) */
static const t3_vec3 WORLD_DIR = { 0.82f, 0.2f, -0.54f };
#define WORLD_D 1400.0f
#define WORLD_R 640.0f
static const t3_vec3 GALAXY_DIR = { 0.1f, 0.2f, 1.0f };

static uint32_t hsh(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hsh(x) & 0xffffff) / 16777215.0f; }
static float clamp01_(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float sstep(float a, float b, float x) {
  float t = clamp01_((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static uint8_t byte_(float x) { return (uint8_t)(clamp01_(x) * 255.0f + 0.5f); }
static t3_vec3 V3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V3(a.x * k, a.y * k, a.z * k); }
static t3_vec3 vnorm(t3_vec3 a) {
  float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  return l > 1e-6f ? vmul(a, 1.0f / l) : a;
}

/* octave noise on a sphere's direction (seamless where a flat texture would show its seam) */
static float fbm3(float x, float y, float z, int oct, uint32_t seed) {
  float a = 0.5f, s = 0, n = 0;
  for (int i = 0; i < oct; i++) {
    s += a * pm_noise3(x, y, z, seed + (uint32_t)i * 131u);
    n += a;
    x *= 2.03f, y *= 2.03f, z *= 2.03f, a *= 0.5f;
  }
  return s / n;
}

static t3_texture *tex_from(int w, int h, const uint8_t *px, bool srgb) {
  t3_texture *t = t3_texture_new(w, h, px);
  t->wrap_s = T3_CLAMP_TO_EDGE;
  t->wrap_t = T3_CLAMP_TO_EDGE;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 8;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}

/* the dome: a blue-violet nebula drifting across one side and a faint band of the galaxy, painted small and stretched */
static t3_texture *make_dome(void) {
  enum { W = 512, H = 256 };
  static uint8_t px[W * H * 4];
  const t3_vec3 gn = { -0.3f, 0.82f, 0.48f }; /* the galaxy's plane */
  for (int j = 0; j < H; j++) {
    float lat = ((float)j + 0.5f) / H * 3.14159f - 1.5708f;
    for (int i = 0; i < W; i++) {
      float lon = ((float)i + 0.5f) / W * 6.28318f;
      float dx = cosf(lat) * cosf(lon), dy = sinf(lat), dz = cosf(lat) * sinf(lon);
      float plane = fabsf(dx * gn.x + dy * gn.y + dz * gn.z);
      float band = expf(-plane * plane * 30.0f) * (0.4f + 0.8f * (0.5f + 0.5f * fbm3(dx * 3.3f, dy * 3.3f, dz * 3.3f, 4, 17)));
      float neb = sstep(0.25f, 0.85f, 0.5f + 0.5f * fbm3(dx * 1.6f + 2, dy * 1.6f, dz * 1.6f, 5, 43)) * sstep(-0.2f, 0.6f, -dx * 0.6f + dz * 0.5f);
      float wisp = sstep(0.4f, 0.9f, 0.5f + 0.5f * fbm3(dx * 4.0f, dy * 4.0f + 5, dz * 4.0f, 4, 71));
      uint8_t *p = &px[(j * W + i) * 4];
      p[0] = byte_(0.42f * (band * 0.3f + neb * (0.16f + 0.1f * wisp)));
      p[1] = byte_(0.42f * (band * 0.29f + neb * (0.05f + 0.06f * wisp)));
      p[2] = byte_(0.42f * (band * 0.38f + neb * (0.3f + 0.12f * wisp)));
      p[3] = 255;
    }
  }
  return tex_from(W, H, px, true);
}

/* the world: deep blue seas, green and ochre land, ice at the poles; and (a second texture) its clouds */
static void make_world(t3_texture **albedo, t3_texture **clouds) {
  enum { W = 1024, H = 512 };
  static uint8_t a[W * H * 4], c[W * H * 4];
  for (int j = 0; j < H; j++) {
    float lat = ((float)j + 0.5f) / H * 3.14159f - 1.5708f;
    for (int i = 0; i < W; i++) {
      float lon = ((float)i + 0.5f) / W * 6.28318f;
      float dx = cosf(lat) * cosf(lon), dy = sinf(lat), dz = cosf(lat) * sinf(lon);
      float el = fbm3(dx * 1.9f, dy * 1.9f, dz * 1.9f, 6, 211) - 0.14f; /* mostly sea: a blue world */
      float dry = 0.5f + 0.5f * fbm3(dx * 2.6f + 4, dy * 2.6f, dz * 2.6f, 3, 223);
      float ice = sstep(1.12f, 1.32f, fabsf(lat) + 0.2f * fbm3(dx * 5, dy * 5, dz * 5, 3, 229));
      float r, g, b;
      if (el < 0) {
        float d = sstep(0, -0.4f, el);
        r = mixf(0.06f, 0.015f, d), g = mixf(0.28f, 0.07f, d), b = mixf(0.62f, 0.3f, d);
      } else {
        float hgt = sstep(0.0f, 0.45f, el);
        r = mixf(mixf(0.12f, 0.55f, dry), 0.44f, hgt), g = mixf(mixf(0.3f, 0.42f, dry), 0.38f, hgt), b = mixf(mixf(0.09f, 0.22f, dry), 0.32f, hgt);
      }
      r = mixf(r, 0.92f, ice), g = mixf(g, 0.95f, ice), b = mixf(b, 1.0f, ice);
      uint8_t *p = &a[(j * W + i) * 4];
      p[0] = byte_(r), p[1] = byte_(g), p[2] = byte_(b), p[3] = 255;
      float wx = dx + 0.4f * fbm3(dx * 2 + 1, dy * 2, dz * 2, 3, 241), wy = dy + 0.4f * fbm3(dx * 2, dy * 2 + 3, dz * 2, 3, 251);
      float wz = dz + 0.4f * fbm3(dx * 2, dy * 2, dz * 2 + 5, 3, 257);
      float cd = 0.5f + 0.5f * fbm3(wx * 3.0f, wy * 4.6f, wz * 3.0f, 5, 263);
      float ca = sstep(0.44f, 0.74f, cd); /* plenty of cloud: blue and white from here */
      uint8_t *w = &c[(j * W + i) * 4];
      w[0] = w[1] = w[2] = 255, w[3] = byte_(ca * 0.9f);
    }
  }
  *albedo = tex_from(W, H, a, true);
  *clouds = tex_from(W, H, c, true);
}

/* the galaxy: a spiral seen a little side-on, a hot gold core and two dusty golden arms wound round it, blue knots of
 * young stars along them (drawn additive, so black is nothing) */
static t3_texture *make_galaxy(void) {
  enum { N = 512 };
  static uint8_t px[N * N * 4];
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float x = ((float)i + 0.5f) / N * 2 - 1, y = ((float)j + 0.5f) / N * 2 - 1;
      /* tilted and squashed: the disc seen at an angle */
      float rx = x * 0.94f + y * 0.34f, ry = (-x * 0.34f + y * 0.94f) / 0.62f;
      float r = sqrtf(rx * rx + ry * ry), th = atan2f(ry, rx);
      float wind = th - 3.2f * logf(r + 0.04f);
      float arm = 0.5f + 0.5f * cosf(2.0f * wind), dust = 0.5f + 0.5f * fbm3(rx * 5, ry * 5, 1.3f, 4, 911);
      arm = powf(arm, 3.0f) * (0.55f + 0.7f * dust);
      float disc = expf(-r * 3.4f), core = expf(-r * r * 90.0f), edge = 1 - sstep(0.62f, 0.98f, r);
      float knots = sstep(0.72f, 0.95f, 0.5f + 0.5f * fbm3(rx * 14, ry * 14, 4.2f, 3, 919)) * arm;
      float v = (arm * disc * 1.5f + disc * 0.25f) * edge;
      uint8_t *p = &px[(j * N + i) * 4];
      p[0] = byte_(v * 1.0f + core * 1.0f + knots * 0.1f * edge);
      p[1] = byte_(v * 0.72f + core * 0.92f + knots * 0.25f * edge);
      p[2] = byte_(v * 0.42f + core * 0.75f + knots * 0.55f * edge);
      p[3] = 255;
    }
  return tex_from(N, N, px, true);
}

/* the sun's flare pieces: a soft glow with a hot core, and an anamorphic streak */
static t3_texture *make_glow(int kind) {
  enum { N = 256 };
  static uint8_t px[N * N * 4];
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float x = ((float)i + 0.5f) / N * 2 - 1, y = ((float)j + 0.5f) / N * 2 - 1, r = sqrtf(x * x + y * y), v;
      if (kind == 0) v = expf(-r * r * 9.0f) * 0.55f + 0.45f / (1.0f + r * r * 160.0f);
      else v = expf(-fabsf(x) * 3.2f) * expf(-y * y * 700.0f) * (1 - sstep(0.85f, 1, fabsf(x)));
      v *= 1 - sstep(0.92f, 1.0f, r);
      uint8_t g = byte_(v), *p = &px[(j * N + i) * 4];
      p[0] = p[1] = p[2] = g, p[3] = 255;
    }
  return tex_from(N, N, px, false);
}

static struct {
  t3_object *world, *clouds;
} SKY;

static t3_material *unlit(t3_texture *map, t3_color col, bool additive, bool backside) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = col;
  if (map) t3_material_set_map(m, map);
  m->fog = false;
  m->depth_write = false;
  m->side = backside ? T3_BACK_SIDE : T3_DOUBLE_SIDE;
  if (additive) m->transparent = true, m->blending = T3_ADDITIVE_BLENDING;
  return m;
}

static t3_object *sphere(cw_node parent, const char *name, t3_vec3 at, float r, int seg, t3_material *m, int order) {
  t3_geometry *g = t3_sphere_geometry_new(r, seg, seg / 2);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.frustum_culled = false, me->base.cast_shadow = false, me->base.receive_shadow = false, me->base.render_order = order;
  t3_object_set_position(me, at.x, at.y, at.z);
  hang(parent, name, &me->base);
  t3_object *o = &me->base;
  t3_release(me), t3_release(g), t3_release(m);
  return o;
}

/* a flat quad dist metres out towards dir, facing the middle */
static void sky_quad(cw_node parent, const char *name, t3_vec3 dir, float dist, float w, t3_material *m, float roll) {
  t3_geometry *g = t3_plane_geometry_new(w, w, 1, 1);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = false, me->base.frustum_culled = false, me->base.render_order = -5;
  t3_vec3 p = vadd(SKY_C, vmul(dir, dist));
  t3_object_set_position(me, p.x, p.y, p.z);
  t3_object_look_at(me, SKY_C.x, SKY_C.y, SKY_C.z);
  if (roll != 0) t3_object_set_quaternion(me, t3_quat_multiply(me->base.quaternion, t3_quat_from_axis_angle(V3(0, 0, 1), roll)));
  hang(parent, name, &me->base);
  t3_release(me), t3_release(g), t3_release(m);
}

/* the world's air: thin shells seen from behind, so its rim glows, bright where the sun lights the limb and warm at the
 * line between day and night */
static void world_air(cw_node parent, t3_vec3 c, float R) {
  static const float THICK[3] = { 0.005f, 0.014f, 0.034f }, GAIN[3] = { 2.4f, 0.9f, 0.25f };
  t3_vec3 sun = vnorm(SUN_DIR);
  for (int s = 0; s < 3; s++) {
    t3_geometry *g = t3_sphere_geometry_new(R * (1.0f + THICK[s]), 96, 48);
    t3_attribute *pos = g->attributes[T3_ATTR_POSITION];
    int n = pos->count;
    float *col = (float *)malloc((size_t)n * 3 * sizeof(float));
    const float *pp = t3_attribute_f32(pos);
    for (int i = 0; i < n; i++) {
      t3_vec3 d = vnorm(V3(pp[i * 3], pp[i * 3 + 1], pp[i * 3 + 2]));
      float lit = d.x * sun.x + d.y * sun.y + d.z * sun.z;
      float day = sstep(-0.35f, 0.5f, lit), term = expf(-lit * lit * 12.0f), k = GAIN[s] * (0.05f + day);
      col[i * 3 + 0] = k * (0.16f + 0.9f * term), col[i * 3 + 1] = k * (0.48f + 0.2f * term), col[i * 3 + 2] = k * (1.0f - 0.45f * term);
    }
    t3_attribute *ca = t3_attribute_new(T3_FLOAT32, col, n, 3);
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, ca);
    t3_release(ca);
    free(col);
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->vertex_colors = true, m->fog = false, m->side = T3_BACK_SIDE, m->depth_write = false, m->transparent = true, m->blending = T3_ADDITIVE_BLENDING;
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.frustum_culled = false, me->base.cast_shadow = false, me->base.render_order = -4;
    t3_object_set_position(me, c.x, c.y, c.z);
    hang(parent, s == 0 ? "Air0" : s == 1 ? "Air1" : "Air2", &me->base);
    t3_release(me), t3_release(g), t3_release(m);
  }
}

/* the stars: n points on the dome a pixel or two across, tinted by temperature, brightness by a power law (the bright
 * few carry a value over one, so they bloom) */
static void stars(cw_node parent, const char *name, int n, uint32_t seed, float size, float gain, float dim) {
  float *pos = (float *)malloc((size_t)n * 3 * sizeof(float)), *col = (float *)malloc((size_t)n * 3 * sizeof(float));
  for (int k = 0; k < n; k++) {
    uint32_t h = hsh((uint32_t)k * 2654435761u + seed);
    float u = hf(h) * 6.28318f, z = hf(h ^ 0x9e3779b9u) * 2 - 1, rr = sqrtf(1 - z * z);
    pos[k * 3 + 0] = SKY_C.x + cosf(u) * rr * SKY_R * 0.97f, pos[k * 3 + 1] = SKY_C.y + z * SKY_R * 0.97f;
    pos[k * 3 + 2] = SKY_C.z + sinf(u) * rr * SKY_R * 0.97f;
    float b = hf(h ^ 0x51ed270bu), t = hf(h ^ 0x7f4a7c15u);
    b = dim + (1 - dim) * b * b * b;
    float cr = t < 0.25f ? 0.7f : 1.0f, cg = t < 0.25f ? 0.82f : t < 0.75f ? 0.95f : 0.8f, cb = t < 0.25f ? 1.0f : t < 0.75f ? 0.9f : 0.62f;
    col[k * 3 + 0] = cr * b * gain, col[k * 3 + 1] = cg * b * gain, col[k * 3 + 2] = cb * b * gain;
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, pos, n, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, col, n, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  free(pos), free(col);
  t3_material *m = t3_points_material_new(0xffffff);
  m->vertex_colors = true, m->size = size, m->size_attenuation = false, m->fog = false, m->depth_write = false;
  t3_mesh *me = t3_points_new(g, m);
  me->base.frustum_culled = false, me->base.cast_shadow = false, me->base.render_order = -9;
  hang(parent, name, &me->base);
  t3_release(me), t3_release(g), t3_release(m);
}

static void build_sky(cw_node scene) {
  cw_node root = cw_node_new("Node3D", "Sky");
  cw_node_add_child(scene, root);
  t3_texture *dome = make_dome();
  sphere(root, "Dome", SKY_C, SKY_R, 64, unlit(dome, (t3_color){ 1, 1, 1 }, false, true), -10);
  t3_release(dome);
  stars(root, "Stars", 6000, 91u, 1.8f, 1.7f, 0.25f);
  stars(root, "BrightStars", 180, 5151u, 3.4f, 5.0f, 0.5f);

  /* the world, its clouds and its air */
  t3_vec3 wc = vadd(SKY_C, vmul(vnorm(WORLD_DIR), WORLD_D));
  t3_texture *alb, *cl;
  make_world(&alb, &cl);
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  t3_material_set_map(m, alb);
  m->roughness = 0.75f, m->metalness = 0;
  SKY.world = sphere(root, "World", wc, WORLD_R, 128, m, 0);
  t3_material *cm = t3_mesh_standard_material_new(0xffffff);
  t3_material_set_map(cm, cl);
  cm->roughness = 1, cm->metalness = 0, cm->transparent = true, cm->depth_write = false;
  SKY.clouds = sphere(root, "WorldClouds", wc, WORLD_R * 1.007f, 128, cm, 0);
  t3_release(alb), t3_release(cl);
  world_air(root, wc, WORLD_R * 1.007f);

  /* a grey moon high over the west */
  t3_material *mm = phys((t3_color){ 0.36f, 0.35f, 0.34f }, 0.95f, 0);
  tex_apply(mm, TEX_GROUND_ROCK, 3, 2, 1.0f, 1.0f);
  sphere(root, "Moon", vadd(SKY_C, vmul(vnorm(V3(-0.8f, 0.42f, -0.25f)), 1500)), 85, 48, mm, 0);

  /* the galaxy behind red's tower, and a wide soft glow round it */
  t3_texture *gx = make_galaxy();
  sky_quad(root, "Galaxy", vnorm(GALAXY_DIR), 1650, 1100, unlit(gx, (t3_color){ 1.6f, 1.5f, 1.4f }, true, false), 0.35f);
  sky_quad(root, "GalaxyHaze", vnorm(GALAXY_DIR), 1660, 2200, unlit(gx, (t3_color){ 0.22f, 0.18f, 0.16f }, true, false), 0.35f);
  t3_release(gx);

  /* the sun: a hot core with bloom, a halo and two streaks */
  t3_vec3 sd = vnorm(SUN_DIR);
  t3_texture *gl = make_glow(0), *st = make_glow(1);
  sky_quad(root, "SunCore", sd, 1700, 140, unlit(gl, (t3_color){ 13, 13, 14 }, true, false), 0);
  sky_quad(root, "SunHalo", sd, 1700, 900, unlit(gl, (t3_color){ 0.42f, 0.46f, 0.55f }, true, false), 0);
  sky_quad(root, "SunStreak", sd, 1690, 2200, unlit(st, (t3_color){ 0.55f, 0.68f, 0.95f }, true, false), 0);
  sky_quad(root, "SunStreak2", sd, 1690, 1300, unlit(st, (t3_color){ 0.3f, 0.36f, 0.6f }, true, false), 1.5708f);
  t3_release(gl), t3_release(st);
}

/* ── the asteroid's bulk and the towers' dressing ─────────────────────────────────────────────────── */

/* a shadowless clustered point light */
static void point_light(cw_node root, const char *name, t3_vec3 p, pcol c, float energy, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(root, l);
  cw_node3d_set_position(l, p);
  cw_node_set(l, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
}

/* a crag hanging under the rock: a jagged cone from (x, top, z) (radius r, squashed sx by sz) down to a point, lumps on
 * its flanks */
static void crag(pmc *c, float x, float z, float top, float r, float depth, float sx, float sz, uint32_t seed) {
  pm *b = pmc_at(c, V3(x, top - depth * 0.5f, z));
  pcol hi = hex(0x5c5248), lo = hex(0x2a2622);
  pm_vary(b, 0.14f);
  pm_jitter(b, 0.25f);
  pm_push(b);
  pm_move(b, x, top - depth, z);
  pm_scale(b, sx, 1, sz);
  pm_cyl(b, r * 0.1f, r, depth, 11, lo, hi);
  pm_pop(b);
  pm_jitter(b, 0);
  for (int k = 0; k < 7; k++) {
    float a = hf(seed + (uint32_t)k * 7u) * 6.2832f, d = 0.2f + 0.6f * hf(seed + (uint32_t)k * 7u + 1u);
    float rr = r * (1.0f - d) * 0.9f, y = top - depth * d;
    pm_push(b);
    pm_move(b, x + sinf(a) * rr * sx, y, z + cosf(a) * rr * sz);
    pm_ball(b, r * 0.25f, r * 0.2f, r * 0.25f, 1, 0.35f, lo, hi);
    pm_pop(b);
  }
}

/* a merlon on a battlement: a stone block w wide, h high, d deep, standing at (x, y, z) turned yaw radians */
static void merlon(pmc *c, float x, float y, float z, float w, float h, float d, float yaw) {
  pm *b = pmc_at(c, V3(x, y, z));
  pm_push(b);
  pm_move(b, x, y, z);
  pm_roty(b, yaw);
  pm_box(b, w, h, d, hex(0x4c4640), hex(0x6a625a));
  pm_pop(b);
}

/* a row of merlons along a parapet from (x0, z0) to (x1, z1) at height y, every `step` metres */
static void merlons(pmc *c, float x0, float z0, float x1, float z1, float y, float step, float w, float h) {
  float dx = x1 - x0, dz = z1 - z0, l = sqrtf(dx * dx + dz * dz);
  int n = (int)(l / step);
  float yaw = atan2f(dx, dz);
  for (int k = 0; k <= n; k++) {
    float t = n ? (float)k / (float)n : 0.5f;
    merlon(c, x0 + dx * t, y, z0 + dz * t, 1.2f, h, w, yaw);
  }
}

/* a lit window let into a tower's face: a dark stone frame, the warm glass in it (or dark: nobody home) */
static void window(pmc *c, float x, float y, float z, float nx, float nz, float w, float h, bool lit, pcol warm) {
  pm *b = pmc_at(c, V3(x, y, z));
  pm_push(b);
  pm_move(b, x + nx * 0.12f, y, z + nz * 0.12f);
  pm_roty(b, atan2f(nx, nz));
  pm_layer(b, PL_MAIN);
  pm_box(b, w + 1.0f, h + 1.0f, 0.3f, hex(0x2c2824), hex(0x3a342e));
  pm_move(b, 0, 0.5f, 0.17f);
  pm_layer(b, lit ? PL_GLOW : PL_MAIN);
  pm_box(b, w, h, 0.05f, lit ? warm : hex(0x0c0d12), lit ? warm : hex(0x0c0d12));
  pm_pop(b);
}

static void spires_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)t;
  const arena_rt *a = arena_current();
  if (!a) return;
  bool split = game_planned_views() > 1;
  memset(&SKY, 0, sizeof SKY);
  arena_gfx_build(scene, a, seed);
  build_sky(scene);
  cw_node root = cw_node_new("Node3D", "SpiresDressing");
  cw_node_add_child(scene, root);
  float cz = ARENA_CZ;

  /* the asteroid's bulk: great crags hanging under the islands, under the crossing and under the paths' bends, and loose
   * rocks drifting round it */
  pmc *rock = pmc_new(80.0f, seed + 11u);
  pmc_texture_metres(rock, 12.0f);
  for (int s = -1; s <= 1; s += 2) {
    float S = (float)s;
    crag(rock, -10 * S, cz + S * 262.0f, -28.0f, 88.0f, 150.0f, 1.0f, 0.9f, 300u + (uint32_t)(s + 1));
    crag(rock, 105 * S, cz + S * 170.0f, -14.0f, 30.0f, 60.0f, 1.0f, 1.0f, 304u + (uint32_t)(s + 1));
    crag(rock, -115 * S, cz + S * 305.0f, -14.0f, 28.0f, 55.0f, 1.0f, 1.0f, 308u + (uint32_t)(s + 1));
    for (int x = -1; x <= 1; x += 2) crag(rock, 55.0f * (float)x, cz + S * 80.0f, -6.0f, 22.0f, 46.0f, 1.0f, 1.3f, 312u + (uint32_t)(s + 1) * 3u + (uint32_t)(x + 1));
  }
  crag(rock, 0, cz, 0.0f, 30.0f, 80.0f, 1.0f, 1.6f, 330u);
  /* the long body of the asteroid under the paths, joining the two islands' crags: one rock, not stepping stones */
  for (int i = 0; i < 9; i++) {
    float z = -200.0f + 50.0f * (float)i, r = 46.0f - 10.0f * fabsf(z) / 200.0f + 10.0f * hf(350u + (uint32_t)i);
    float x = 14.0f * (hf(360u + (uint32_t)i) - 0.5f), y = -42.0f - 12.0f * hf(370u + (uint32_t)i);
    pm *b = pmc_at(rock, V3(x, y, z + cz));
    pm_vary(b, 0.14f);
    pm_push(b);
    pm_move(b, x, y, z + cz);
    pm_roty(b, hf(380u + (uint32_t)i) * 3.0f);
    pm_ball(b, r * 1.25f, r * 0.62f, r * 1.1f, 2, 0.45f, hex(0x221e1b), hex(0x4f463d));
    pm_pop(b);
  }
  for (int i = 0; i < (split ? 40 : 90); i++) {
    float ang = hf(400u + (uint32_t)i) * 6.2832f, r = 190.0f + 380.0f * hf(410u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r * 1.5f + cz, y = -150.0f + 260.0f * hf(420u + (uint32_t)i);
    float sz = 2.0f + 12.0f * hf(430u + (uint32_t)i) * hf(431u + (uint32_t)i);
    if (fabsf(x) < 180 && fabsf(z - cz) < 380 && y > -60) y -= 120; /* never over the rock or in the way */
    pm *b = pmc_at(rock, V3(x, y, z));
    pm_vary(b, 0.16f);
    pm_push(b);
    pm_move(b, x, y, z);
    pm_roty(b, ang * 3.0f);
    pm_rotx(b, hf(440u + (uint32_t)i) * 3.0f);
    pm_ball(b, sz * 1.3f, sz * 0.8f, sz, 1, 0.45f, hex(0x2f2a26), hex(0x5e554c));
    pm_pop(b);
  }
  pmc_finish(rock, root, "SpiresRock", true, TEX_GROUND_ROCK);

  /* the towers' stone: battlements on the gatehouse, the ledge and the roof, a pyramid on each turret, the undersides of
   * the arch and the hall's ceiling (a box is drawn from outside and above only) */
  pmc *st = pmc_new(80.0f, seed + 13u);
  pmc_texture_metres(st, 4.0f);
  for (int s = -1; s <= 1; s += 2) {
    float S = (float)s, zc = cz + S * SHAFT_Z;
    /* the gatehouse's parapet: its front and sides */
    merlons(st, -33, cz + S * 224.6f, 33, cz + S * 224.6f, 28.2f, 5.5f, 2.4f, 2.6f);
    merlons(st, -33, cz + S * 226.0f, -33, cz + S * 266.0f, 28.2f, 5.5f, 2.4f, 2.6f);
    merlons(st, 33, cz + S * 226.0f, 33, cz + S * 266.0f, 28.2f, 5.5f, 2.4f, 2.6f);
    /* the ledges round the tower at 96 m and 156 m */
    for (int q = 0; q < 2; q++)
      for (int k = 0; k < 4; k++) {
        float sx = k & 1 ? 1.0f : -1.0f, e = q ? 30.0f : 34.0f, y = q ? 156.0f : 96.0f;
        if (k < 2) merlons(st, sx * e, zc - e, sx * e, zc + e, y, 6.0f, 3.0f, 3.2f);
        else merlons(st, -e, zc + sx * e, e, zc + sx * e, y, 6.0f, 3.0f, 3.2f);
      }
    /* the roof: merlons on the parapet (the rails are its wall), between the turrets */
    for (int k = 0; k < 4; k++) {
      float sx = k & 1 ? 1.0f : -1.0f;
      if (k < 2) merlons(st, sx * 24.6f, zc - 15, sx * 24.6f, zc + 15, ROOF_Y + RAIL_H, 5.0f, 1.0f, 1.8f);
      else merlons(st, -15, zc + sx * 24.6f, 15, zc + sx * 24.6f, ROOF_Y + RAIL_H, 5.0f, 1.0f, 1.8f);
    }
    for (int k = 0; k < 4; k++) {
      float x = k & 1 ? 21.5f : -21.5f, z = zc + (k & 2 ? 21.5f : -21.5f), y = ROOF_Y + 14.0f;
      pm *b = pmc_at(st, V3(x, y, z));
      pm_push(b);
      pm_move(b, x, y, z);
      pm_box(b, 8.6f, 1.0f, 8.6f, hex(0x4c4640), hex(0x655d55));
      pm_move(b, 0, 1.0f, 0);
      pm_roty(b, CW_PI * 0.25f);
      pm_cyl(b, 5.6f, 0, 9.0f, 4, hex(0x3e3833), hex(0x5c544c));
      pm_pop(b);
    }
    /* undersides: the lintel over the arch, the hall's ceiling */
    pcol c = hex(0x2e2a27);
    float ys[2] = { 14.97f, 17.97f }, x0[2] = { -12, -26 }, x1[2] = { 12, 26 }, z0[2] = { 226, 232 }, z1[2] = { 232, 266 };
    for (int q = 0; q < 2; q++) {
      t3_vec3 p0 = V3(x0[q], ys[q], cz + S * z0[q]), p1 = V3(x1[q], ys[q], cz + S * z0[q]), p2 = V3(x1[q], ys[q], cz + S * z1[q]),
              p3 = V3(x0[q], ys[q], cz + S * z1[q]);
      pm *b = pmc_at(st, V3(0, ys[q], (p0.z + p2.z) * 0.5f));
      pm_quad(b, p0, p1, p2, p3, c);
      pm_quad(b, p0, p3, p2, p1, c);
    }
  }
  pmc_finish(st, root, "SpiresStone", true, TEX_WALL_CONCRETE);

  /* the lit windows up the towers' faces (a few dark), and warm lamps in the hall */
  pmc *lit = pmc_new(80.0f, seed + 17u);
  pcol warm = glow(hex(WARM), 2.2f);
  for (int s = -1; s <= 1; s += 2) {
    float S = (float)s, zc = cz + S * SHAFT_Z;
    uint32_t h = 500u + (uint32_t)(s + 1) * 97u;
    for (int face = 0; face < 4; face++) {
      /* the face's outward normal; on each stage tall slit windows, the front's lowest starting above the gatehouse */
      float nx = face == 2 ? 1.0f : face == 3 ? -1.0f : 0, nz = face == 0 ? -S : face == 1 ? S : 0;
      static const float STAGE_Y[5] = { 40, 64, 110, 132, 164 }, STAGE_E[5] = { 32, 32, 28, 28, 24 }, STAGE_N[5] = { 3, 3, 2, 2, 1 };
      for (int row = 0; row < 5; row++)
        for (int col = 0; col < (int)STAGE_N[row]; col++) {
          float u = STAGE_N[row] == 1 ? 0 : (-1.0f + 2.0f * (float)col / (STAGE_N[row] - 1)) * (STAGE_N[row] == 3 ? 13.0f : 9.0f);
          float x = nx != 0 ? nx * STAGE_E[row] : u, z = nz != 0 ? zc + nz * STAGE_E[row] : zc + u;
          bool on = hf(h++) > 0.18f;
          window(lit, x, STAGE_Y[row], z, nx, nz, 2.0f, 8.0f, on, warm);
        }
    }
    /* lamps in the hall: a warm panel high on each side wall either side of the team's strip */
    for (int k = -1; k <= 1; k += 2)
      for (int q = 0; q < 2; q++) window(lit, 25.7f * (float)k, 10.0f, cz + S * (240.0f + 16.0f * (float)q), -(float)k, 0, 3.0f, 2.0f, true, glow(hex(WARM), 3.0f));
  }
  pmc_finish(lit, root, "SpiresLit", false, -1);

  /* the light: each hall in its team's colour and warm at the gate, the roofs, warm lamps over the forks, the bends and
   * the crossing */
  for (int s = -1; s <= 1; s += 2) {
    float S = (float)s;
    pcol team = hex(s > 0 ? RED : BLUE);
    point_light(root, s > 0 ? "RedHall" : "BlueHall", V3(0, 12.0f, cz + S * 250.0f), pc_mix(team, hex(0xffffff), 0.15f), 900.0f, 34.0f);
    point_light(root, s > 0 ? "RedGate" : "BlueGate", V3(0, 22.0f, cz + S * 214.0f), hex(WARM), 1500.0f, 48.0f);
    point_light(root, s > 0 ? "RedTowerLight" : "BlueTowerLight", V3(0, 60.0f, cz + S * 246.0f), team, 2600.0f, 70.0f);
    point_light(root, s > 0 ? "RedFork" : "BlueFork", V3(0, 12.0f, cz + S * 158.0f), hex(WARM), 900.0f, 40.0f);
    if (!split) point_light(root, s > 0 ? "RedRoof" : "BlueRoof", V3(0, ROOF_Y + 9.0f, cz + S * SHAFT_Z), team, 900.0f, 34.0f);
  }
  point_light(root, "Crossing", V3(0, HUMP + 12.0f, cz), hex(WARM), 1100.0f, 44.0f);
  for (int k = 0; k < 4 && !split; k++)
    point_light(root, "BendLight", V3(k & 1 ? 62.0f : -62.0f, 22.0f, cz + (k & 2 ? 80.0f : -80.0f)), hex(WARM), 700.0f, 36.0f);
}

/* the world turns slowly beside the asteroid, its clouds a little faster */
static void spires_frame(float alpha, double time) {
  (void)alpha;
  float tf = (float)time;
  if (SKY.world) t3_object_set_rotation(SKY.world, 0.35f, tf * 0.006f, 0);
  if (SKY.clouds) t3_object_set_rotation(SKY.clouds, 0.35f, tf * 0.008f, 0);
}

const map_def map_spires = {
  .name = "Twin Spires",
  .blurb = "Two great stone towers at the ends of an asteroid in space: two wide paths rise between them, crossing in the middle over the void.",
  .scene = "scenes/spires.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.5f, 0.46f, 0.42f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.5f, 0.47f, 0.44f },
            .wall_top = { 0.6f, 0.56f, 0.52f }, .road_tex = TEX_GROUND_ROCK, .wall_tex = TEX_WALL_STONE, .road_rough = 0.9f },
  .laps = 1,
  .par_lap = 180,
  .scenery = spires_scenery,
  .scenery_frame = spires_frame,
  .arena = &ARENA,
  .music = 8,
};
