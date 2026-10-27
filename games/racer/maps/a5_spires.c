/* capture the flag map: Twin Spires. A tribute to a classic capture-the-flag map of the late nineties: two tall stone
 * towers on rock islands at the two ends of a long asteroid adrift in space, joined by two walkways that cross in an X on a
 * round rock in the middle, under a black sky full of stars with a great blue world hanging beside it. Nothing is taken
 * from it: the layout is drawn again for karts and for racing (the flags 480 m apart, walkways 12 m wide with balustrades)
 * and every shape and texture is made here.
 *
 *   the islands  a plaza in front of each tower, the base and its flag on it; a walkway leaves from each front corner
 *   the towers   a hall at the foot (an arch in its front) with a teleport pad up to the roof: battlements, a turret at
 *                each corner and the whole asteroid below. A pad at the roof's front throws you back out at speed onto the
 *                head of your east (red) or west (blue) walkway
 *   the routes   each walkway swings out to a round rock off to the side, then cuts in to the middle rock, where the two
 *                cross and carry on to the far rocks and the far island: two ways there, each with a boost strip as it
 *                leaves the island and as it nears the middle, and a crest on the long run in to jump
 * The decks hang over nothing (arena_def.open): a kart that is knocked off falls past the kill plane and comes back at its
 * base. Red holds the north island (+z), blue the south; every red shape has its blue twin turned half round. A capture
 * the flag map only (arena_def.modes). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../arena.h"
#include "../props.h"
#include "../props_gen.h"

#define BOX(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, false, col }
#define DECO(look, x, z, y, w, h, d, yaw, col) { AP_BOX, look, x, z, y, w, h, d, yaw, true, col }
#define POST(look, x, z, y, dia, h, col) { AP_POST, look, x, z, y, dia, h, dia, 0, false, col }
#define GLOWPOST(x, z, y, dia, h, col) { AP_POST, AL_GLOW, x, z, y, dia, h, dia, 0, true, col }
/* the balustrade round every deck (arena.c lays it, leaving the ways on): over a kart's hull, which rides up anything lower */
#define RAIL_H 1.7f
/* a walkway: a deck 12 m wide and 1.5 m deep, its top at 0 */
#define WALK(x, z, len, yaw) { AP_BOX, AL_DECK, x, z, -1.5f, 12, 1.5f, len, yaw, false, 0, RAIL_H }
/* a round rock: a deck dia across, its top at 0 */
#define ROCK(x, z, dia) { AP_POST, AL_DECK, x, z, -3, dia, 3, dia, 0, false, 0, RAIL_H }
/* half a crest across a walkway: a ramp 6 m wide, 7 m long, 1.2 m up, centred at x, z rising towards yaw */
#define CREST(x, z, yaw) { AP_RAMP, AL_RAMP, x, z, 0, 6, 1.2f, 7, yaw, false, 0 }

#define RED 0xff4630
#define BLUE 0x3d7dff
#define STAR 0xbfe4ff
#define DARK 0x5b5862

#define ROOF_Y 34.0f /* the towers' roofs (their tops) */
#define BASE_Z 238.0f /* the bases, each side of the middle */

/* one tower and its island, S = 1 red (north), -1 blue (south), TC its team's light */
#define TOWER(S, TC)                                                                                                   \
  /* the island's deck (its balustrade leaves the two walkways on at the front corners) */                             \
  { AP_BOX, AL_DECK, 0, S * 250, -4, 60, 4, 46, 0, false, 0, RAIL_H },                                                \
  /* the hall at the foot: its back wall, its sides and the piers of its arch */                                       \
  BOX(AL_FEATURE, 0, S * 269.5f, 0, 24, 7, 3, 0, 0), BOX(AL_FEATURE, 10, S * 257.5f, 0, 4, 7, 21, 0, 0),               \
  BOX(AL_FEATURE, -10, S * 257.5f, 0, 4, 7, 21, 0, 0), BOX(AL_FEATURE, 6.5f, S * 248.5f, 0, 3, 7, 3, 0, 0),            \
  BOX(AL_FEATURE, -6.5f, S * 248.5f, 0, 3, 7, 3, 0, 0),                                                               \
  /* the tower over it, the roof (a deck a little wider than the tower, its balustrade the battlements), the turrets */ \
  BOX(AL_FEATURE, 0, S * 259, 7, 24, ROOF_Y - 8, 24, 0, DARK),                                                        \
  { AP_BOX, AL_DECK, 0, S * 259, ROOF_Y - 1, 28, 1, 28, 0, false, 0, RAIL_H },                                        \
  POST(AL_FEATURE, 12.5f, S * 246.5f, ROOF_Y, 3.6f, 9, DARK), POST(AL_FEATURE, -12.5f, S * 246.5f, ROOF_Y, 3.6f, 9, DARK), \
  POST(AL_FEATURE, 12.5f, S * 271.5f, ROOF_Y, 3.6f, 9, DARK), POST(AL_FEATURE, -12.5f, S * 271.5f, ROOF_Y, 3.6f, 9, DARK), \
  /* the team's lights: a band round each turret, slit windows up the tower's face, the arch's lintel, the hall */      \
  GLOWPOST(12.5f, S * 246.5f, ROOF_Y + 6.5f, 3.9f, 0.45f, TC), GLOWPOST(-12.5f, S * 246.5f, ROOF_Y + 6.5f, 3.9f, 0.45f, TC), \
  GLOWPOST(12.5f, S * 271.5f, ROOF_Y + 6.5f, 3.9f, 0.45f, TC), GLOWPOST(-12.5f, S * 271.5f, ROOF_Y + 6.5f, 3.9f, 0.45f, TC), \
  DECO(AL_GLOW, -7, S * 246.93f, 10, 1.1f, 6, 0.12f, 0, TC), DECO(AL_GLOW, 0, S * 246.93f, 11, 1.1f, 7, 0.12f, 0, TC),  \
  DECO(AL_GLOW, 7, S * 246.93f, 10, 1.1f, 6, 0.12f, 0, TC), DECO(AL_GLOW, 0, S * 246.93f, 6.6f, 10, 0.35f, 0.12f, 0, TC), \
  DECO(AL_GLOW, -7, S * 246.93f, 20, 1.1f, 7, 0.12f, 0, TC), DECO(AL_GLOW, 7, S * 246.93f, 20, 1.1f, 7, 0.12f, 0, TC),  \
  DECO(AL_GLOW, 0, S * 246.93f, 21, 1.1f, 9, 0.12f, 0, TC),                                                           \
  DECO(AL_GLOW, 7.95f, S * 258, 4.6f, 0.1f, 0.3f, 15, 0, TC), DECO(AL_GLOW, -7.95f, S * 258, 4.6f, 0.1f, 0.3f, 15, 0, TC)

static const arena_prim PRIMS[] = {
  TOWER(1.0f, RED),
  TOWER(-1.0f, BLUE),
  /* the rocks: the middle one and the four off to the sides */
  ROCK(0, 0, 44),
  ROCK(62, 120, 32), ROCK(-62, 120, 32), ROCK(62, -120, 32), ROCK(-62, -120, 32),
  /* the walkways: from each island's front corners (x 22) out to the side rocks, each 3 m into its island and 2 m onto
   * its rock; then in from the side rocks to the middle one, where the line from a red corner runs on to the opposite
   * blue corner */
  WALK(39.024f, 181.462f, 103.232f, 159.503f), WALK(-39.024f, 181.462f, 103.232f, -159.503f),
  WALK(-39.024f, -181.462f, 103.232f, -20.497f), WALK(39.024f, -181.462f, 103.232f, 20.497f),
  WALK(32.377f, 62.665f, 101.07f, -152.676f), WALK(-32.377f, 62.665f, 101.07f, 152.676f),
  WALK(-32.377f, -62.665f, 101.07f, 27.324f), WALK(32.377f, -62.665f, 101.07f, -27.324f),
  /* the crests half way in to the middle: a ramp up from each side to a ridge across the walkway (6 m of the 12: go over
   * it for the air or round it) */
  CREST(33.984f, 65.774f, -152.676f), CREST(30.77f, 59.556f, 27.324f), CREST(-33.984f, 65.774f, 152.676f), CREST(-30.77f, 59.556f, -27.324f),
  CREST(-33.984f, -65.774f, 27.324f), CREST(-30.77f, -59.556f, -152.676f), CREST(33.984f, -65.774f, -27.324f), CREST(30.77f, -59.556f, 152.676f),
  /* a ring of stone let into the middle rock round a lit rim, and lamps on the side rocks */
  { AP_POST, AL_WALL, 0, 0, 0, 15, 0.05f, 15, 0, true, 0 }, GLOWPOST(0, 0, 0, 15.6f, 0.03f, STAR),
  GLOWPOST(62, 120, 0, 9.6f, 0.03f, STAR), GLOWPOST(-62, 120, 0, 9.6f, 0.03f, STAR),
  GLOWPOST(62, -120, 0, 9.6f, 0.03f, STAR), GLOWPOST(-62, -120, 0, 9.6f, 0.03f, STAR),
};

/* the boost strips: 30 m out from each island along its walkways, and 32 m out from the middle along each */
static const arena_boost BOOSTS[] = {
  { 32.506f, 198.899f, 0, 6, 10, 159.503f }, { -32.506f, 198.899f, 0, 6, 10, -159.503f },
  { -32.506f, -198.899f, 0, 6, 10, -20.497f }, { 32.506f, -198.899f, 0, 6, 10, 20.497f },
  { 14.69f, 28.42f, 0, 6, 10, -152.676f }, { -14.69f, 28.42f, 0, 6, 10, 152.676f },
  { -14.69f, -28.42f, 0, 6, 10, 27.324f }, { 14.69f, -28.42f, 0, 6, 10, -27.324f },
};

/* the teleport pads: up from each hall to its roof (arriving at the back, facing the asteroid), and down from the roof's
 * front to the head of the team's east (red) or west (blue) walkway, at speed along it */
static const arena_tele TELES[] = {
  { 0, 263, 0, 2.6f, 0, 268.5f, ROOF_Y, 180, 5, 0, RED },
  { 0, 249.5f, ROOF_Y, 2.6f, 19.9f, 232.6f, 0, 159.503f, 18, 180, RED },
  { 0, -263, 0, 2.6f, 0, -268.5f, ROOF_Y, 0, 5, 180, BLUE },
  { 0, -249.5f, ROOF_Y, 2.6f, -19.9f, -232.6f, 0, -20.497f, 18, 0, BLUE },
};

/* capture the flag: red on the north plaza, blue on the south, facing the walkways (the battle starts, should anything
 * ask, are the same) */
static const float TEAM_SPAWNS[8][3] = {
  { -6, 232, 180 }, { 6, 232, 180 }, { -14, 234, 180 }, { 14, 234, 180 },
  { 6, -232, 0 }, { -6, -232, 0 }, { 14, -234, 0 }, { -14, -234, 0 },
};

static const float BOXES[][3] = {
  { 62, 120, 0 }, { -62, 120, 0 }, { 62, -120, 0 }, { -62, -120, 0 },
  { -20, 244, 0 }, { 20, 244, 0 }, { 20, -244, 0 }, { -20, -244, 0 },
  { -8, 259, ROOF_Y }, { 8, 259, ROOF_Y }, { 8, -259, ROOF_Y }, { -8, -259, ROOF_Y },
};

/* the CPU drivers' way round: each plaza, the walkway heads, the side rocks and the middle */
static const float ROUTE[][2] = {
  { 0, 236 }, { 20, 230 }, { -20, 230 }, { 62, 120 }, { -62, 120 }, { 0, 0 },
  { 62, -120 }, { -62, -120 }, { 20, -230 }, { -20, -230 }, { 0, -236 },
};
static const uint8_t LINKS[][2] = {
  { 0, 1 }, { 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 5 }, { 4, 5 }, { 5, 6 }, { 5, 7 }, { 6, 8 }, { 7, 9 }, { 8, 10 }, { 9, 10 },
};

static const arena_def ARENA = {
  .half_x = 80, .half_z = 275, .wall_h = 0,
  .prims = PRIMS, .nprims = sizeof PRIMS / sizeof PRIMS[0],
  .spawns = TEAM_SPAWNS, .nspawns = 8,
  .boxes = BOXES, .nboxes = sizeof BOXES / sizeof BOXES[0],
  .base = { { 0, BASE_Z }, { 0, -BASE_Z } },
  .team_spawns = TEAM_SPAWNS,
  .open = true,
  .fence = 3.2f,
  .teles = TELES, .nteles = sizeof TELES / sizeof TELES[0],
  .boosts = BOOSTS, .nboosts = sizeof BOOSTS / sizeof BOOSTS[0],
  .route = ROUTE, .nroute = sizeof ROUTE / sizeof ROUTE[0], .links = LINKS, .nlinks = sizeof LINKS / sizeof LINKS[0],
  .modes = 1 << AM_CTF,
  .look = {
    [AL_FLOOR] = { TEX_ROAD_STONE, { 0.7f, 0.7f, 0.74f }, 1.4f, 8.0f, 0.9f, 0 },
    [AL_WALL] = { TEX_WALL_STONE, { 0.74f, 0.73f, 0.78f }, 1.5f, 3.0f, 0.9f, 0 },
    [AL_COVER] = { TEX_WALL_STONE, { 0.55f, 0.54f, 0.6f }, 1.4f, 3.0f, 0.9f, 0 },
    [AL_DECK] = { TEX_ROAD_STONE, { 0.7f, 0.62f, 0.8f }, 1.45f, 6.0f, 0.85f, 0 },
    [AL_RAMP] = { TEX_ROAD_STONE, { 0.78f, 0.7f, 0.86f }, 1.5f, 6.0f, 0.85f, 0 },
    [AL_TRIM] = { TEX_WALL_STONE, { 0.6f, 0.6f, 0.68f }, 1.4f, 2.0f, 0.9f, 0 },
    [AL_FEATURE] = { TEX_WALL_STONE, { 0.62f, 0.6f, 0.64f }, 1.35f, 6.0f, 0.92f, 0 },
    [AL_GLOW] = { -1, { 0.75f, 0.9f, 1.0f }, 1, 1, 1, 0, 3.0f },
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
/* the world: low over the blue end, off to the east, half the sky's height across (red's view as it attacks, the sun
 * behind it, so it is nearly full) */
static const t3_vec3 WORLD_DIR = { 0.55f, 0.06f, -0.83f };
#define WORLD_D 1400.0f
#define WORLD_R 560.0f

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
      float el = fbm3(dx * 1.9f, dy * 1.9f, dz * 1.9f, 6, 211) - 0.04f;
      float dry = 0.5f + 0.5f * fbm3(dx * 2.6f + 4, dy * 2.6f, dz * 2.6f, 3, 223);
      float ice = sstep(1.12f, 1.32f, fabsf(lat) + 0.2f * fbm3(dx * 5, dy * 5, dz * 5, 3, 229));
      float r, g, b;
      if (el < 0) {
        float d = sstep(0, -0.4f, el);
        r = mixf(0.04f, 0.01f, d), g = mixf(0.22f, 0.05f, d), b = mixf(0.48f, 0.22f, d);
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
      float ca = sstep(0.52f, 0.8f, cd);
      uint8_t *w = &c[(j * W + i) * 4];
      w[0] = w[1] = w[2] = 255, w[3] = byte_(ca * 0.9f);
    }
  }
  *albedo = tex_from(W, H, a, true);
  *clouds = tex_from(W, H, c, true);
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

  /* a small grey moon high on the other side */
  t3_material *mm = phys((t3_color){ 0.33f, 0.32f, 0.31f }, 0.95f, 0);
  tex_apply(mm, TEX_GROUND_ROCK, 3, 2, 1.0f, 1.0f);
  sphere(root, "Moon", vadd(SKY_C, vmul(vnorm(V3(-0.62f, 0.34f, 0.7f)), 1500)), 55, 48, mm, 0);

  /* the sun: a hot core with bloom, a halo and two streaks */
  t3_vec3 sd = vnorm(SUN_DIR);
  t3_texture *gl = make_glow(0), *st = make_glow(1);
  sky_quad(root, "SunCore", sd, 1700, 140, unlit(gl, (t3_color){ 13, 13, 14 }, true, false), 0);
  sky_quad(root, "SunHalo", sd, 1700, 900, unlit(gl, (t3_color){ 0.42f, 0.46f, 0.55f }, true, false), 0);
  sky_quad(root, "SunStreak", sd, 1690, 2200, unlit(st, (t3_color){ 0.55f, 0.68f, 0.95f }, true, false), 0);
  sky_quad(root, "SunStreak2", sd, 1690, 1300, unlit(st, (t3_color){ 0.3f, 0.36f, 0.6f }, true, false), 1.5708f);
  t3_release(gl), t3_release(st);
}

/* ── the rock and the towers' dressing ────────────────────────────────────────────────────────────── */

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

/* a crag hanging under a deck: a jagged cone from the deck's underside (radius r at y top) down to a point, squashed to
 * the deck's shape, lumps on its flanks */
static void crag(pmc *c, float x, float z, float top, float r, float depth, float sx, float sz, uint32_t seed) {
  pm *b = pmc_at(c, V3(x, top - depth * 0.5f, z));
  pcol hi = hex(0x77716f), lo = hex(0x3b3940);
  pm_vary(b, 0.14f);
  pm_jitter(b, 0.22f);
  pm_push(b);
  pm_move(b, x, top - depth, z);
  pm_scale(b, sx, 1, sz);
  pm_cyl(b, r * 0.12f, r, depth, 14, lo, hi);
  pm_pop(b);
  pm_jitter(b, 0);
  for (int k = 0; k < 7; k++) {
    float a = hf(seed + (uint32_t)k * 7u) * 6.2832f, d = 0.25f + 0.55f * hf(seed + (uint32_t)k * 7u + 1u);
    float rr = r * (1.0f - d) * 0.85f, y = top - depth * d;
    pm_push(b);
    pm_move(b, x + sinf(a) * rr * sx, y, z + cosf(a) * rr * sz);
    pm_ball(b, r * 0.22f, r * 0.18f, r * 0.22f, 1, 0.3f, lo, hi);
    pm_pop(b);
  }
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

  /* the asteroid: the crags under the islands and the middle rock, the long body far below joining them, and loose
   * rocks drifting round it */
  pmc *rock = pmc_new(60.0f, seed + 11u);
  pmc_texture_metres(rock, 9.0f);
  for (int s = -1; s <= 1; s += 2) {
    crag(rock, 0, cz + (float)s * 250.0f, -3.9f, 31.0f, 50.0f, 1.0f, 0.78f, 300u + (uint32_t)(s + 1));
    for (int x = -1; x <= 1; x += 2) crag(rock, 62.0f * (float)x, cz + (float)s * 120.0f, -2.9f, 15.5f, 26.0f, 1.0f, 1.0f, 310u + (uint32_t)(s + 1) * 3u + (uint32_t)(x + 1));
  }
  crag(rock, 0, cz, -2.9f, 21.5f, 34.0f, 1.0f, 1.0f, 320u);
  for (int i = 0; i < 24; i++) {
    float z = -322.0f + 28.0f * (float)i, r = 24.0f + 16.0f * hf(330u + (uint32_t)i) - 8.0f * fabsf(z) / 322.0f;
    pm *b = pmc_at(rock, V3(0, -80, z + cz));
    pm_vary(b, 0.12f);
    pm_push(b);
    pm_move(b, 30.0f * (hf(340u + (uint32_t)i) - 0.5f), -82.0f - 10.0f * hf(350u + (uint32_t)i), z + cz);
    pm_ball(b, r * 1.1f, r * 0.55f, r * 1.2f, 2, 0.45f, hex(0x1d1c22), hex(0x4a4642));
    pm_pop(b);
  }
  for (int i = 0; i < (split ? 30 : 60); i++) {
    float ang = hf(400u + (uint32_t)i) * 6.2832f, r = 110.0f + 220.0f * hf(410u + (uint32_t)i);
    float x = sinf(ang) * r, z = cosf(ang) * r * 1.6f + cz, y = -70.0f + 120.0f * hf(420u + (uint32_t)i), sz = 1.0f + 6.0f * hf(430u + (uint32_t)i) * hf(431u + (uint32_t)i);
    if (fabsf(x) < 100 && y > -30) y -= 50; /* never in the way over the decks */
    pm *b = pmc_at(rock, V3(x, y, z));
    pm_vary(b, 0.14f);
    pm_push(b);
    pm_move(b, x, y, z);
    pm_roty(b, ang * 3.0f);
    pm_rotx(b, hf(440u + (uint32_t)i) * 3.0f);
    pm_ball(b, sz * 1.2f, sz * 0.8f, sz, 1, 0.4f, hex(0x403d44), hex(0x7a746f));
    pm_pop(b);
  }
  pmc_finish(rock, root, "SpiresRock", true, TEX_GROUND_ROCK);

  /* the towers' stone: a pointed cap on each turret and a spike between them on the back wall */
  pmc *cap = pmc_new(60.0f, seed + 13u);
  pmc_texture_metres(cap, 4.0f);
  for (int s = -1; s <= 1; s += 2)
    for (int k = 0; k < 4; k++) {
      float x = k & 1 ? 12.5f : -12.5f, z = cz + (float)s * (k & 2 ? 271.5f : 246.5f), y = ROOF_Y + 9.0f;
      pm *b = pmc_at(cap, V3(x, y, z));
      pm_push(b);
      pm_move(b, x, y, z);
      pm_cyl(b, 2.4f, 2.4f, 0.6f, 12, hex(0x5b5862), hex(0x6b6870));
      pm_move(b, 0, 0.6f, 0);
      pm_cyl(b, 2.2f, 0, 7.5f, 12, hex(0x4a4750), hex(0x7a7680));
      pm_pop(b);
    }
  /* the halls' ceilings (the tower's block over each is drawn from outside only) */
  for (int s = -1; s <= 1; s += 2) {
    float z0 = cz + (float)s * 250.0f, z1 = cz + (float)s * 268.0f, y = 6.95f;
    pm *b = pmc_at(cap, V3(0, y, (z0 + z1) * 0.5f));
    pcol c = hex(0x4a4750);
    t3_vec3 p0 = V3(-8, y, z0), p1 = V3(8, y, z0), p2 = V3(8, y, z1), p3 = V3(-8, y, z1);
    pm_quad(b, p0, p1, p2, p3, c);
    pm_quad(b, p0, p3, p2, p1, c);
  }
  pmc_finish(cap, root, "SpiresCaps", true, TEX_WALL_STONE);

  /* lit strips along the top of every rail, the team's banners down the towers' faces */
  pmc *lit = pmc_new(48.0f, seed + 17u);
  pcol rail_lit = glow(hex(0x8fd8ff), 2.4f);
  for (int i = 0; i < a->n; i++) {
    const arena_prim *p = &a->prim[i];
    if (p->look != AL_TRIM || p->ghost) continue;
    pm *b = pmc_at(lit, V3(p->x, p->y + p->h, p->z));
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, p->x, p->y + p->h, p->z);
    pm_roty(b, p->yaw * (CW_PI / 180.0f));
    pm_box(b, 0.22f, 0.06f, p->d - 0.3f, rail_lit, rail_lit);
    pm_pop(b);
  }
  for (int s = -1; s <= 1; s += 2) {
    pcol team = hex(s > 0 ? RED : BLUE), deep = pc_mul(team, 0.45f);
    for (int k = -1; k <= 1; k += 2) {
      float x = 3.6f * (float)k, z = cz + (float)s * 246.85f;
      pm *b = pmc_at(lit, V3(x, 14, z));
      pm_layer(b, PL_MAIN);
      pm_push(b);
      pm_move(b, x, ROOF_Y - 17.0f, z);
      pm_box(b, 2.4f, 16.0f, 0.08f, deep, team);
      pm_pop(b);
      pm_layer(b, PL_GLOW);
      pm_push(b);
      pm_move(b, x, ROOF_Y - 1.8f, z - (float)s * 0.06f);
      pm_box(b, 2.8f, 0.3f, 0.08f, glow(team, 2.5f), glow(team, 2.5f));
      pm_pop(b);
    }
  }
  pmc_finish(lit, root, "SpiresLit", false, -1);

  /* the light: each base lit in its colour, the halls, the roofs, the standing stones */
  for (int s = -1; s <= 1; s += 2) {
    pcol team = hex(s > 0 ? RED : BLUE);
    point_light(root, s > 0 ? "RedBase" : "BlueBase", V3(0, 6.0f, cz + (float)s * BASE_Z), team, 450.0f, 30.0f);
    point_light(root, s > 0 ? "RedHall" : "BlueHall", V3(0, 5.0f, cz + (float)s * 261.0f), pc_mix(team, hex(0xffffff), 0.3f), 300.0f, 16.0f);
    if (!split) point_light(root, s > 0 ? "RedRoof" : "BlueRoof", V3(0, ROOF_Y + 7.0f, cz + (float)s * 259.0f), team, 700.0f, 26.0f);
  }
  point_light(root, "MiddleLight", V3(0, 9.0f, cz), hex(STAR), 500.0f, 34.0f);
  for (int k = 0; k < 4 && !split; k++)
    point_light(root, "RockLight", V3(k & 1 ? 62.0f : -62.0f, 7.0f, cz + (k & 2 ? 120.0f : -120.0f)), hex(STAR), 300.0f, 26.0f);
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
  .blurb = "Two stone towers at the ends of an asteroid in space: two long walkways race between them, crossing in the middle.",
  .scene = "scenes/spires.cwscene",
  .spec = &ARENA_TRACK,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.66f, 0.68f, 0.74f }, .kerb_a = { 0.95f, 0.3f, 0.2f }, .kerb_b = { 0.25f, 0.5f, 1.0f }, .wall = { 0.7f, 0.7f, 0.75f },
            .wall_top = { 0.8f, 0.8f, 0.85f }, .road_tex = TEX_ROAD_STONE, .wall_tex = TEX_WALL_STONE, .road_rough = 0.85f },
  .laps = 1,
  .par_lap = 180,
  .scenery = spires_scenery,
  .scenery_frame = spires_frame,
  .arena = &ARENA,
  .music = 8,
};
