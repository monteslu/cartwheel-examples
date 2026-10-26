/* map: Wreck Orbit. A wrecked orbital station and its debris ring: the road runs along the outside of the hulls and
 * through them, under a black star-filled sky, with a lit planet below and a gas giant with rings above.
 *
 * Nothing here is a lit sky: space has no atmosphere, so the "sky" is a dome painted with stars and a nebula, a sun
 * made of additive flare quads, and real spheres for the worlds. Everything sits in world space round the middle of the
 * course; the dome is 1850 m out so the view's far plane (2600 m) still reaches it from the far end of the course.
 * Every texture is made here at load from integer hashes and the shared value noise, so every host draws the same sky. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game.h"
#include "../props.h"
#include "../props_gen.h"

static const piece SPEC[] = {
  WALLS(1, 1),
  ITEMS(),
  STRAIGHT(110),
  CHECK(),
  BOOST(30),
  STRAIGHT(40),
  LOOP(30, 1),
  STRAIGHT(40),
  ITEMS(),
  BANKED(-35, 100, -22),
  BANKED(35, 100, 22),
  TUNNEL(2),
  STRAIGHT(90),
  TUNNEL(0),
  WIDTH(30, 4.6f),
  CORKSCREW(190, -1),
  WIDTH(30, 8.0f),
  ASTRAIGHT(40),
  CHECK(),
  ABANKED(180, 340, 28),
  ASTRAIGHT(40),
  ITEMS(),
  BOOST(30),
  STRAIGHT(30),
  SLOPE(36, 20),
  SLOPE(22, 20),
  JUMP(0, 30),
  SLOPE(80, 0),
  STRAIGHT(40),
  CHECK(),
  BANKED(40, 110, 24),
  BANKED(-40, 110, -24),
  TUNNEL(2),
  STRAIGHT(80),
  TUNNEL(0),
  ITEMS(),
  WIDTH(30, 4.6f),
  CORKSCREW(190, 1),
  WIDTH(30, 8.0f),
  STRAIGHT(30),
  LOOP(26, -1),
  ASTRAIGHT(40),
  ABANKED(180, 340, 28),
  ASTRAIGHT(100),
  ASLOPE(50, 0),
  ASTRAIGHT(100),
  STRAIGHT(60),
  END(),
};

/* The spacewalk: a bare maintenance gantry straight across the mouth of the first hairpin. It saves about a hundred
 * metres, but it is narrow, has no walls, and its two bends are tight: miss one and the kart drifts off into space. */
static const piece SPACEWALK[] = {
  SURFACE(SURF_METAL),
  MOVE(8, 6, KEEP, 0, KEEP),
  ATURN(80, 56),
  ASTRAIGHT(20),
  BOOST(20),
  STRAIGHT(20),
  ASTRAIGHT(70),
  ATURN(80, 56),
  ASTRAIGHT(8),
  END(),
};
static const branch_spec BRANCHES[] = {
  { .name = "spacewalk", .s0 = 984, .lat0 = -4.6f, .s1 = 1296, .lat1 = -4.6f, .pieces = SPACEWALK, .npieces = sizeof SPACEWALK / sizeof SPACEWALK[0],
    .hw0 = 3.4f, .walls_l = 0, .walls_r = 0, .ai_take = { 0.08f, 0.4f, 0.85f } },
};
#define MAP_SPEC (&(track_spec){ .name = "orbit", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 8, .circuit = true, .branches = BRANCHES, .nbranches = 1 })

/* the textures and the sky use the engine's deterministic maths so every host paints the same picture */
#define sinf(x) cw_sin(x)
#define cosf(x) cw_cos(x)
#define expf(x) cw_exp(x)
#define powf(x, y) cw_pow(x, y)
#define asinf(x) cw_asin(x)

/* ── small helpers ───────────────────────────────────────────────────────────────────────────────────── */

static uint32_t hsh(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static float hf(uint32_t x) { return (float)(hsh(x) & 0xffffff) / 16777215.0f; }
static float clampf_(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float sstep(float a, float b, float x) {
  float t = clampf_((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}
static float mixf(float a, float b, float t) { return a + (b - a) * t; }
static uint8_t byte_(float x) { return (uint8_t)(clampf_(x, 0, 1) * 255.0f + 0.5f); }
static t3_vec3 V3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V3(a.x * k, a.y * k, a.z * k); }
static float vlen(t3_vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

/* octave noise on a sphere's direction: seamless where a flat texture would show its seam */
static float fbm3(float x, float y, float z, int oct, uint32_t seed) {
  float a = 0.5f, s = 0, n = 0;
  for (int i = 0; i < oct; i++) {
    s += a * pm_noise3(x, y, z, seed + (uint32_t)i * 131u);
    n += a;
    x *= 2.03f, y *= 2.03f, z *= 2.03f, a *= 0.5f;
  }
  return s / n; /* -1..1 */
}

/* ── where the worlds are ────────────────────────────────────────────────────────────────────────────── */

#define SKY_R 1850.0f
static const t3_vec3 SKY_C = { 65, 0, 285 };           /* the middle of the course */
/* the sun's direction from the course (towards the sun); the scene's Sun light shines the opposite way. Azimuth 195
 * degrees from +z, 26 degrees up: behind the start straight's driver, ahead of the whole return leg (a low sun over the dark
 * limb of the world, with the second loop silhouetted against it). */
static const t3_vec3 SUN_DIR = { -0.2326f, 0.4384f, -0.8682f };
/* the world beside and below the station: 2300 m round, its near surface about 640 m from the road, half lit: seen side on from
 * both legs, its inner limb about 40 degrees off the way you drive */
static const t3_vec3 PLANET_C = { -2375, -1830, 285 };
#define PLANET_R 2300.0f
static const t3_vec3 GIANT_C = { 760, 396, 1923 };     /* the ringed gas giant, dead ahead on the start straight */
#define GIANT_R 430.0f

/* ── sky textures ────────────────────────────────────────────────────────────────────────────────────── */

static t3_texture *tex_from(int w, int h, const uint8_t *px, bool srgb, bool repeat_u) {
  t3_texture *t = t3_texture_new(w, h, px);
  (void)repeat_u; /* the generated shader path takes clamped maps only; the seam at longitude 0 is one texel */
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

/* The nebula dome: a faint coloured nebula and a milky-way band painted at low resolution then stretched. The stars are real
 * points (below): a texture cannot make a star a single crisp pixel. */
#define DOME_W 1024
#define DOME_H 512
#define NEB 0.5f /* how bright the painted nebula is against the stars */
static t3_texture *make_dome(void) {
  enum { NW = 256, NH = 128 };
  static float neb[NW * NH * 3];
  /* the galactic plane: a tilted great circle */
  const t3_vec3 gn = { 0.34f, 0.86f, -0.38f };
  for (int j = 0; j < NH; j++) {
    float lat = ((float)j + 0.5f) / NH * 3.14159f - 1.5708f;
    for (int i = 0; i < NW; i++) {
      float lon = ((float)i + 0.5f) / NW * 6.28318f;
      float dx = cosf(lat) * cosf(lon), dy = sinf(lat), dz = cosf(lat) * sinf(lon);
      float plane = fabsf(dx * gn.x + dy * gn.y + dz * gn.z); /* 0 on the band */
      float band = expf(-plane * plane * 38.0f);
      float lump = 0.5f + 0.5f * fbm3(dx * 3.1f, dy * 3.1f, dz * 3.1f, 4, 11);
      float lane = sstep(0.35f, 0.7f, 0.5f + 0.5f * fbm3(dx * 7.0f, dy * 7.0f, dz * 7.0f, 4, 29)); /* dust lanes cut the band */
      float milky = band * (0.35f + 0.9f * lump) * (1.0f - 0.7f * lane * band);
      /* coloured clouds away from the band: magenta and cyan, patchy */
      float m1 = sstep(0.18f, 0.75f, 0.5f + 0.5f * fbm3(dx * 1.9f + 3, dy * 1.9f, dz * 1.9f, 5, 47));
      float m2 = sstep(0.22f, 0.8f, 0.5f + 0.5f * fbm3(dx * 2.3f, dy * 2.3f + 8, dz * 2.3f, 5, 83));
      float *p = &neb[(j * NW + i) * 3];
      p[0] = NEB * (milky * 0.34f + m1 * 0.16f + m2 * 0.02f);
      p[1] = NEB * (milky * 0.3f + m1 * 0.02f + m2 * 0.09f);
      p[2] = NEB * (milky * 0.42f + m1 * 0.15f + m2 * 0.17f);
    }
  }
  static uint8_t px[DOME_W * DOME_H * 4];
  static float img[DOME_W * DOME_H * 3];
  for (int j = 0; j < DOME_H; j++) {
    float fy = ((float)j + 0.5f) / DOME_H * NH - 0.5f;
    int y0 = (int)floorf(fy);
    float ty = fy - (float)y0;
    int ya = y0 < 0 ? 0 : y0, yb = y0 + 1 >= NH ? NH - 1 : y0 + 1;
    for (int i = 0; i < DOME_W; i++) {
      float fx = ((float)i + 0.5f) / DOME_W * NW - 0.5f;
      int x0 = (int)floorf(fx);
      float tx = fx - (float)x0;
      int xa = (x0 + NW) % NW, xb = (x0 + 1 + NW) % NW;
      for (int c = 0; c < 3; c++) {
        float a = mixf(neb[(ya * NW + xa) * 3 + c], neb[(ya * NW + xb) * 3 + c], tx);
        float b = mixf(neb[(yb * NW + xa) * 3 + c], neb[(yb * NW + xb) * 3 + c], tx);
        img[(j * DOME_W + i) * 3 + c] = mixf(a, b, ty);
      }
    }
  }
  for (int i = 0; i < DOME_W * DOME_H; i++) {
    px[i * 4 + 0] = byte_(img[i * 3 + 0]), px[i * 4 + 1] = byte_(img[i * 3 + 1]), px[i * 4 + 2] = byte_(img[i * 3 + 2]), px[i * 4 + 3] = 255;
  }
  return tex_from(DOME_W, DOME_H, px, true, true);
}

/* The world below: seas, land, ice caps and (a second texture) swirled clouds. City lights are points, not texels: see build_cities. */
#define PL_W 1024
#define PL_H 512
static void make_planet(t3_texture **albedo, t3_texture **clouds) {
  static uint8_t a[PL_W * PL_H * 4], c[PL_W * PL_H * 4];
  for (int j = 0; j < PL_H; j++) {
    float lat = ((float)j + 0.5f) / PL_H * 3.14159f - 1.5708f;
    for (int i = 0; i < PL_W; i++) {
      float lon = ((float)i + 0.5f) / PL_W * 6.28318f;
      float dx = cosf(lat) * cosf(lon), dy = sinf(lat), dz = cosf(lat) * sinf(lon);
      float el = fbm3(dx * 2.3f, dy * 2.3f, dz * 2.3f, 6, 5) + 0.06f;
      float r, g, b;
      float dry = 0.5f + 0.5f * fbm3(dx * 3.0f + 4, dy * 3.0f, dz * 3.0f, 3, 61);
      float ice = sstep(1.05f, 1.3f, fabsf(lat) + 0.22f * fbm3(dx * 5, dy * 5, dz * 5, 3, 9));
      float land = 0;
      if (el < 0.0f) { /* sea: deeper is darker */
        float d = sstep(0, -0.45f, el);
        r = mixf(0.05f, 0.012f, d), g = mixf(0.28f, 0.07f, d), b = mixf(0.5f, 0.24f, d);
      } else {
        land = 1;
        float hgt = sstep(0.0f, 0.5f, el);
        r = mixf(mixf(0.13f, 0.5f, dry), 0.42f, hgt), g = mixf(mixf(0.33f, 0.4f, dry), 0.36f, hgt), b = mixf(mixf(0.1f, 0.22f, dry), 0.3f, hgt);
        float snow = sstep(0.38f, 0.55f, el);
        r = mixf(r, 0.92f, snow), g = mixf(g, 0.94f, snow), b = mixf(b, 0.98f, snow);
      }
      r = mixf(r, 0.9f, ice), g = mixf(g, 0.94f, ice), b = mixf(b, 1.0f, ice);
      uint8_t *p = &a[(j * PL_W + i) * 4];
      p[0] = byte_(r), p[1] = byte_(g), p[2] = byte_(b), p[3] = 255;
      /* clouds: swirled bands, white with alpha */
      float wx = dx + 0.35f * fbm3(dx * 2 + 1, dy * 2, dz * 2, 3, 91), wy = dy + 0.35f * fbm3(dx * 2, dy * 2 + 3, dz * 2, 3, 93), wz = dz + 0.35f * fbm3(dx * 2, dy * 2, dz * 2 + 5, 3, 95);
      float cd = 0.5f + 0.5f * fbm3(wx * 3.2f, wy * 5.0f, wz * 3.2f, 5, 101);
      float bands = 0.5f + 0.5f * cosf(lat * 7.0f + 2.0f * fbm3(dx * 3, dy * 3, dz * 3, 2, 3));
      float ca = sstep(0.5f, 0.78f, cd * (0.7f + 0.5f * bands));
      uint8_t *w = &c[(j * PL_W + i) * 4];
      w[0] = w[1] = 255, w[2] = 255, w[3] = byte_(ca * 0.92f);
    }
  }
  *albedo = tex_from(PL_W, PL_H, a, true, true);
  *clouds = tex_from(PL_W, PL_H, c, true, true);
}

/* The gas giant: horizontal bands that wobble, and a ring with gaps. */
static t3_texture *make_giant(void) {
  enum { W = 512, H = 256 };
  static uint8_t px[W * H * 4];
  for (int j = 0; j < H; j++) {
    float lat = ((float)j + 0.5f) / H * 3.14159f - 1.5708f;
    for (int i = 0; i < W; i++) {
      float lon = ((float)i + 0.5f) / W * 6.28318f;
      float dx = cosf(lat) * cosf(lon), dy = sinf(lat), dz = cosf(lat) * sinf(lon);
      float warp = 0.12f * fbm3(dx * 5, dy * 3, dz * 5, 4, 7);
      float band = 0.5f + 0.35f * sinf((dy + warp) * 24.0f) + 0.15f * sinf((dy + warp) * 61.0f);
      float t = clampf_(band, 0, 1);
      float spot = expf(-(((lon - 3.0f) * 2.2f) * ((lon - 3.0f) * 2.2f) + ((lat + 0.35f) * 4.0f) * ((lat + 0.35f) * 4.0f)));
      float r = mixf(0.62f, 0.95f, t), g = mixf(0.34f, 0.78f, t), b = mixf(0.2f, 0.58f, t);
      r = mixf(r, 0.85f, spot), g = mixf(g, 0.3f, spot), b = mixf(b, 0.2f, spot);
      uint8_t *p = &px[(j * W + i) * 4];
      p[0] = byte_(r), p[1] = byte_(g), p[2] = byte_(b), p[3] = 255;
    }
  }
  return tex_from(W, H, px, true, true);
}
static t3_texture *make_ring(void) {
  enum { N = 512 };
  static uint8_t px[N * N * 4];
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float x = ((float)i + 0.5f) / N * 2 - 1, y = ((float)j + 0.5f) / N * 2 - 1;
      float r = sqrtf(x * x + y * y); /* 1 at the outer edge; the inner edge is 0.52 */
      float u = (r - 0.52f) / 0.48f;
      float a = 0, shade = 1;
      if (u > 0 && u < 1) {
        /* a few broad bands with soft edges and two dark gaps; a little fine grain on top */
        float band = 0.35f + 0.65f * sstep(0.0f, 0.12f, u) * (1 - sstep(0.3f, 0.34f, u)) + 0.9f * sstep(0.38f, 0.42f, u) * (1 - sstep(0.62f, 0.66f, u)) +
                     0.7f * sstep(0.7f, 0.73f, u) * (1 - sstep(0.93f, 0.97f, u));
        float grain = 0.88f + 0.12f * sinf(u * 140.0f);
        a = clampf_(band, 0, 1) * grain * sstep(0.0f, 0.03f, u) * (1 - sstep(0.97f, 1.0f, u));
        shade = 0.75f + 0.25f * sinf(u * 11.0f + 1.0f);
      }
      uint8_t *p = &px[(j * N + i) * 4];
      p[0] = byte_(0.96f * shade), p[1] = byte_(0.86f * shade), p[2] = byte_(0.68f * shade), p[3] = byte_(a * 0.9f);
    }
  return tex_from(N, N, px, true, false);
}

/* flare pieces for the sun: a soft glow, an anamorphic streak and a thin ring */
static t3_texture *make_glow(int kind) {
  enum { N = 256 };
  static uint8_t px[N * N * 4];
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float x = ((float)i + 0.5f) / N * 2 - 1, y = ((float)j + 0.5f) / N * 2 - 1;
      float r = sqrtf(x * x + y * y), v;
      if (kind == 0) v = expf(-r * r * 9.0f) * 0.55f + 0.45f / (1.0f + r * r * 160.0f); /* glow with a hot core */
      else if (kind == 1) v = expf(-fabsf(x) * 3.2f) * expf(-y * y * 700.0f) * (1 - sstep(0.85f, 1, fabsf(x))); /* streak */
      else v = expf(-powf((r - 0.8f) / 0.07f, 2)) * (1 - sstep(0.9f, 1.0f, r)) * 0.6f + expf(-r * r * 4.0f) * 0.1f; /* ghost ring */
      v *= 1 - sstep(0.92f, 1.0f, r);
      uint8_t g = byte_(v);
      uint8_t *p = &px[(j * N + i) * 4];
      p[0] = p[1] = p[2] = g, p[3] = 255;
    }
  return tex_from(N, N, px, false, false);
}

/* ── the sky builder ─────────────────────────────────────────────────────────────────────────────────── */

typedef struct sky_state {
  t3_object *planet, *clouds, *giant, *ring;
  cw_node root, cam; /* the sky rides on the first view's camera so it never drifts: it is infinitely far */
} sky_state;
static sky_state SKY;

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

/* a flat quad `dist` metres from the course in direction dir, facing the middle, w by h metres */
static t3_object *sky_quad(cw_node parent, const char *name, t3_vec3 dir, float dist, float w, float h, t3_material *m, float roll) {
  t3_geometry *g = t3_plane_geometry_new(w, h, 1, 1);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = false;
  me->base.frustum_culled = false;
  t3_vec3 p = vadd(SKY_C, vmul(dir, dist));
  t3_object_set_position(me, p.x, p.y, p.z);
  t3_object_look_at(me, SKY_C.x, SKY_C.y, SKY_C.z);
  if (roll != 0) { /* spin about its own axis after facing the middle */
    t3_quat q = me->base.quaternion, r = t3_quat_from_axis_angle(V3(0, 0, 1), roll);
    t3_object_set_quaternion(me, t3_quat_multiply(q, r));
  }
  me->base.render_order = -5;
  hang(parent, name, &me->base);
  t3_object *o = &me->base;
  t3_release(me), t3_release(g), t3_release(m);
  return o;
}

static void sky_atmosphere(cw_node parent, t3_vec3 c, float R, const float thick[3], const float gain[3], const char *name) {
  /* thin shells seen from behind: round the planet's edge the shell's far side shows, so the rim glows; the vertex colours
   * are bright where the sun lights the limb and warm near the terminator */
  for (int s = 0; s < 3; s++) {
    t3_geometry *g = t3_sphere_geometry_new(R * (1.0f + thick[s]), 96, 48);
    t3_attribute *pos = g->attributes[T3_ATTR_POSITION];
    int n = pos->count;
    float *col = (float *)malloc((size_t)n * 3 * sizeof(float));
    const float *pp = t3_attribute_f32(pos);
    for (int i = 0; i < n; i++) {
      t3_vec3 d = V3(pp[i * 3], pp[i * 3 + 1], pp[i * 3 + 2]);
      float l = vlen(d);
      d = vmul(d, 1.0f / l);
      float lit = d.x * SUN_DIR.x + d.y * SUN_DIR.y + d.z * SUN_DIR.z;
      float day = sstep(-0.35f, 0.5f, lit);
      float term = expf(-lit * lit * 12.0f); /* warm band at the terminator */
      float k = gain[s] * (0.05f + day);
      col[i * 3 + 0] = k * (0.18f + 0.9f * term), col[i * 3 + 1] = k * (0.5f + 0.2f * term), col[i * 3 + 2] = k * (1.0f - 0.45f * term);
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
    char nm[32];
    snprintf(nm, sizeof nm, "%s%d", name, s);
    hang(parent, nm, &me->base);
    t3_release(me), t3_release(g), t3_release(m);
  }
}


/* Stars as points one or two pixels across: 'n' of them on the dome, tinted by temperature, brightness by a power law. The
 * bright few are bigger and carry a value above one so they bloom. */
static void build_stars(cw_node parent, const char *name, int n, uint32_t seed, float size, float gain, float dim) {
  float *pos = (float *)malloc((size_t)n * 3 * sizeof(float)), *col = (float *)malloc((size_t)n * 3 * sizeof(float));
  for (int k = 0; k < n; k++) {
    uint32_t h = hsh((uint32_t)k * 2654435761u + seed);
    float u = hf(h) * 6.28318f, z = hf(h ^ 0x9e3779b9u) * 2 - 1, rr = sqrtf(1 - z * z);
    pos[k * 3 + 0] = SKY_C.x + cosf(u) * rr * SKY_R * 0.97f, pos[k * 3 + 1] = SKY_C.y + z * SKY_R * 0.97f, pos[k * 3 + 2] = SKY_C.z + sinf(u) * rr * SKY_R * 0.97f;
    float b = hf(h ^ 0x51ed270bu);
    b = dim + (1 - dim) * b * b * b;
    float t = hf(h ^ 0x7f4a7c15u);
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


/* City lights on the world's night side: points scattered on low land in clusters (a texture's texels would be fifteen metres
 * wide up close). They are children of the planet so they turn with it. */
static void build_cities(t3_object *planet) {
  const int N = 7000;
  float *pos = (float *)malloc((size_t)N * 3 * sizeof(float)), *col = (float *)malloc((size_t)N * 3 * sizeof(float));
  int n = 0;
  for (int k = 0; k < 60000 && n < N; k++) {
    uint32_t h = hsh((uint32_t)k * 2654435761u + 31337u);
    float u = hf(h) * 6.28318f, z = hf(h ^ 0x9e3779b9u) * 2 - 1, rr = sqrtf(1 - z * z);
    float dx = cosf(u) * rr, dy = z, dz = sinf(u) * rr;
    /* where it ends up after the planet's tilt (0.3 rad about x), for the night-side test */
    float wy = dy * cosf(0.3f) - dz * sinf(0.3f), wz = dy * sinf(0.3f) + dz * cosf(0.3f);
    if (dx * SUN_DIR.x + wy * SUN_DIR.y + wz * SUN_DIR.z > 0.12f) continue;
    float el = fbm3(dx * 2.3f, dy * 2.3f, dz * 2.3f, 6, 5) + 0.06f;
    float lat = asinf(dy);
    float ice = sstep(1.05f, 1.3f, fabsf(lat) + 0.22f * fbm3(dx * 5, dy * 5, dz * 5, 3, 9));
    if (el < 0.0f || el > 0.3f || ice > 0.2f) continue;
    float cl = sstep(0.1f, 0.5f, 0.5f + 0.5f * fbm3(dx * 9.0f, dy * 9.0f, dz * 9.0f, 4, 71));
    if (hf(h ^ 0x68e31da4u) > cl * 0.9f) continue;
    float q = PLANET_R * 1.002f, b = 0.5f + 0.9f * hf(h ^ 0x1b873593u);
    pos[n * 3 + 0] = dx * q, pos[n * 3 + 1] = dy * q, pos[n * 3 + 2] = dz * q;
    col[n * 3 + 0] = 2.4f * b, col[n * 3 + 1] = 1.5f * b, col[n * 3 + 2] = 0.6f * b;
    n++;
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, pos, n, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, col, n, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  free(pos), free(col);
  t3_material *m = t3_points_material_new(0xffffff);
  m->vertex_colors = true, m->size = 2.2f, m->size_attenuation = false, m->fog = false, m->depth_write = false;
  t3_mesh *me = t3_points_new(g, m);
  me->base.frustum_culled = false, me->base.cast_shadow = false;
  t3_object_add(planet, me);
  t3_release(me), t3_release(g), t3_release(m);
}

/* dev aid: ORBIT_OFF=deck,props,... leaves a part out so its cost can be measured (draw calls in the run's state file) */
static bool off(const char *part) {
  const char *e = getenv("ORBIT_OFF");
  return e && strstr(e, part);
}

static void build_sky(cw_node scene) {
  cw_node root = cw_node_new("Node3D", "Sky");
  cw_node_add_child(scene, root);
  SKY.root = root;
  SKY.cam = cw_node_get(scene, "Camera");

  /* the dome */
  if (!off("dome")) {
    t3_texture *dome = make_dome();
    t3_material *m = unlit(dome, (t3_color){ 1.0f, 1.0f, 1.0f }, false, true);
    t3_release(dome);
    t3_geometry *g = t3_sphere_geometry_new(SKY_R, 64, 32);
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.frustum_culled = false, me->base.cast_shadow = false, me->base.receive_shadow = false, me->base.render_order = -10;
    t3_object_set_position(me, SKY_C.x, SKY_C.y, SKY_C.z);
    hang(root, "Dome", &me->base);
    t3_release(me), t3_release(g), t3_release(m);
  }

  if (!off("stars")) build_stars(root, "Stars", 6500, 77u, 1.8f, 1.7f, 0.25f);
  if (!off("stars")) build_stars(root, "BrightStars", 200, 4242u, 3.4f, 5.0f, 0.5f);

  /* the world below, its night lights, its clouds and its atmosphere */
  if (!off("planet")) {
    t3_texture *alb, *clouds;
    make_planet(&alb, &clouds);
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_map(m, alb);
    m->roughness = 0.7f, m->metalness = 0;
    t3_geometry *g = t3_sphere_geometry_new(PLANET_R, 128, 64);
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.frustum_culled = false, me->base.cast_shadow = false, me->base.receive_shadow = false;
    t3_object_set_position(me, PLANET_C.x, PLANET_C.y, PLANET_C.z);
    hang(root, "Planet", &me->base);
    SKY.planet = &me->base;
    build_cities(&me->base);
    t3_release(me), t3_release(g), t3_release(m), t3_release(alb);

    t3_material *cm = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_map(cm, clouds);
    cm->roughness = 1, cm->metalness = 0, cm->transparent = true, cm->depth_write = false;
    t3_geometry *cg = t3_sphere_geometry_new(PLANET_R * 1.006f, 128, 64);
    t3_mesh *ce = t3_mesh_new(cg, cm);
    ce->base.frustum_culled = false, ce->base.cast_shadow = false, ce->base.receive_shadow = false;
    t3_object_set_position(ce, PLANET_C.x, PLANET_C.y, PLANET_C.z);
    hang(root, "Clouds", &ce->base);
    SKY.clouds = &ce->base;
    t3_release(ce), t3_release(cg), t3_release(cm), t3_release(clouds);

    static const float thick[3] = { 0.004f, 0.012f, 0.03f }, gain[3] = { 2.6f, 0.9f, 0.22f };
    sky_atmosphere(root, PLANET_C, PLANET_R * 1.006f, thick, gain, "Air");
  }

  /* the gas giant and its rings, a pale moon in front of it */
  if (!off("giant")) {
    t3_texture *gt = make_giant(), *rt = make_ring();
    t3_material *m = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_map(m, gt);
    m->roughness = 0.9f;
    t3_geometry *g = t3_sphere_geometry_new(GIANT_R, 96, 48);
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.frustum_culled = false, me->base.cast_shadow = false, me->base.receive_shadow = false;
    t3_object_set_position(me, GIANT_C.x, GIANT_C.y, GIANT_C.z);
    hang(root, "Giant", &me->base);
    SKY.giant = &me->base;
    t3_release(me), t3_release(g), t3_release(m), t3_release(gt);

    t3_material *rm = t3_mesh_standard_material_new(0xffffff);
    t3_material_set_map(rm, rt);
    rm->roughness = 1, rm->transparent = true, rm->side = T3_DOUBLE_SIDE, rm->depth_write = false;
    t3_geometry *rg = t3_plane_geometry_new(GIANT_R * 2 * 2.1f, GIANT_R * 2 * 2.1f, 1, 1);
    t3_mesh *re = t3_mesh_new(rg, rm);
    re->base.frustum_culled = false, re->base.cast_shadow = false, re->base.receive_shadow = false;
    t3_object_set_position(re, GIANT_C.x, GIANT_C.y, GIANT_C.z);
    t3_object_look_at(re, SKY_C.x, SKY_C.y, SKY_C.z); /* facing the course, then tipped about its own x axis so it reads as a ring */
    t3_object_set_quaternion(re, t3_quat_multiply(re->base.quaternion, t3_quat_from_axis_angle(V3(1, 0, 0), 1.05f)));
    hang(root, "Ring", &re->base);
    SKY.ring = &re->base;
    t3_release(re), t3_release(rg), t3_release(rm), t3_release(rt);

    static const float thick[3] = { 0.004f, 0.012f, 0.035f }, gain[3] = { 0.7f, 0.35f, 0.12f };
    sky_atmosphere(root, GIANT_C, GIANT_R * 1.004f, thick, gain, "GiantAir");

    t3_material *mm = phys((t3_color){ 0.35f, 0.34f, 0.33f }, 0.95f, 0);
    tex_apply(mm, TEX_GROUND_ROCK, 3, 2, 1.0f, 1.0f);
    t3_geometry *mg = t3_sphere_geometry_new(70, 48, 24);
    t3_mesh *mo = t3_mesh_new(mg, mm);
    mo->base.frustum_culled = false, mo->base.cast_shadow = false, mo->base.receive_shadow = false;
    t3_object_set_position(mo, -265, 300, 1615);
    hang(root, "Moon", &mo->base);
    t3_release(mo), t3_release(mg), t3_release(mm);
  }

  /* the sun: a bright core with bloom, a halo, an anamorphic streak and a ghost ring (the real lens flare moves with the
   * camera; this one is a fixed picture in the sky, which still reads as one) */
  if (!off("sun")) {
    t3_texture *glow = make_glow(0), *streak = make_glow(1), *ring = make_glow(2);
    sky_quad(root, "SunCore", SUN_DIR, 1700, 150, 150, unlit(glow, (t3_color){ 14, 12, 9 }, true, false), 0);
    sky_quad(root, "SunHalo", SUN_DIR, 1700, 900, 900, unlit(glow, (t3_color){ 0.5f, 0.45f, 0.4f }, true, false), 0);
    sky_quad(root, "SunStreak", SUN_DIR, 1690, 2400, 2400, unlit(streak, (t3_color){ 0.9f, 0.7f, 0.45f }, true, false), 0);
    sky_quad(root, "SunStreak2", SUN_DIR, 1690, 1500, 1500, unlit(streak, (t3_color){ 0.35f, 0.45f, 0.8f }, true, false), 1.5708f);
    sky_quad(root, "SunRing", SUN_DIR, 1695, 1300, 1300, unlit(ring, (t3_color){ 0.25f, 0.2f, 0.3f }, true, false), 0);
    t3_release(glow), t3_release(streak), t3_release(ring);
  }
}

/* ── palette and builders ────────────────────────────────────────────────────────────────────────────── */

static struct {
  pcol hull, hull2, dark, panel, orange, yellow, cyan, magenta, red, white, cell, warm;
} K;
static void palette_init(void) {
  K.hull = rgb(0.82f, 0.84f, 0.9f);
  K.hull2 = rgb(0.52f, 0.56f, 0.66f);
  K.dark = rgb(0.13f, 0.14f, 0.18f);
  K.panel = rgb(0.3f, 0.33f, 0.4f);
  K.orange = rgb(0.95f, 0.42f, 0.08f);
  K.yellow = rgb(0.98f, 0.78f, 0.15f);
  K.cyan = rgb(0.06f, 0.88f, 1.0f);
  K.magenta = rgb(1.0f, 0.1f, 0.68f);
  K.red = rgb(1.0f, 0.1f, 0.07f);
  K.white = rgb(1, 1, 1);
  K.cell = rgb(0.03f, 0.09f, 0.3f);
  K.warm = rgb(1.0f, 0.82f, 0.5f);
}
static pcol dk(pcol c, float k) { return pc_mul(c, k); }
static t3_vec3 vsub(t3_vec3 a, t3_vec3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 vcross(t3_vec3 a, t3_vec3 b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float vdot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static t3_vec3 vnorm(t3_vec3 a) {
  float l = vlen(a);
  return l > 1e-9f ? vmul(a, 1.0f / l) : V3(0, 1, 0);
}
static t3_vec3 vlerp(t3_vec3 a, t3_vec3 b, float t) { return V3(mixf(a.x, b.x, t), mixf(a.y, b.y, t), mixf(a.z, b.z, t)); }

/* a point in a road sample's frame: lat to the right, lift along its up */
static t3_vec3 tp(const tsample *a, float lat, float lift) { return vadd(vadd(a->p, vmul(a->r, lat)), vmul(a->u, lift)); }

/* the builder of the cell holding p, pushed into a frame (x right, y up, z forward are the given axes) */
static pm *pose(pmc *c, t3_vec3 p, t3_vec3 r, t3_vec3 u, t3_vec3 f) {
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_basis(b, p, r, u, f);
  return b;
}
static pm *pose_road(pmc *c, const tsample *a, float lat, float lift) { return pose(c, tp(a, lat, lift), a->r, a->u, a->f); }
/* a frame whose y axis runs along d (a cylinder or truss laid along d) */
static void axis_frame(t3_vec3 d, t3_vec3 *r, t3_vec3 *u, t3_vec3 *f) {
  *u = vnorm(d);
  t3_vec3 ref = fabsf(u->y) < 0.95f ? V3(0, 1, 0) : V3(1, 0, 0);
  *r = vnorm(vcross(ref, *u));
  *f = vcross(*r, *u);
}
static pm *pose_axis(pmc *c, t3_vec3 p, t3_vec3 dir) {
  t3_vec3 r, u, f;
  axis_frame(dir, &r, &u, &f);
  return pose(c, p, r, u, f);
}

/* is the road at sample i an ordinary upright deck (not a loop, a jump or a wild roll) */
static bool deck_ok(const track *t, int i) {
  if (i < 0 || i >= t->n) return false;
  const tsample *a = &t->s[i];
  return !(a->flags & (TF_GAP | TF_LOOP)) && a->u.y > 0.8f && fabsf(a->f.y) < 0.5f;
}

/* glowing windows in a ring round a cylinder on y: n of them between y0 and y1, some dark, some blinking cyan or warm */
static void window_band(pm *b, float r, float y0, float y1, int n, float arc, pcol col, float gain, uint32_t seed, float lit) {
  pm_layer(b, PL_GLOW);
  for (int k = 0; k < n; k++) {
    if (hf(seed + (uint32_t)k * 31u) > lit) continue;
    float a = CW_TAU * (float)k / (float)n;
    float ca = cosf(a), sa = sinf(a);
    t3_vec3 tg = V3(-sa, 0, ca), nr = V3(ca, 0, sa);
    float w = arc * r * 0.5f, rr = r * 1.012f;
    t3_vec3 P0 = V3(nr.x * rr, 0, nr.z * rr);
    pcol c = hf(seed + (uint32_t)k * 77u) < 0.2f ? K.cyan : col;
    t3_vec3 q0 = V3(P0.x + tg.x * w, y0, P0.z + tg.z * w), q1 = V3(P0.x - tg.x * w, y0, P0.z - tg.z * w);
    t3_vec3 q2 = V3(P0.x - tg.x * w, y1, P0.z - tg.z * w), q3 = V3(P0.x + tg.x * w, y1, P0.z + tg.z * w);
    pm_quad(b, q0, q1, q2, q3, glow(c, gain));
  }
}

/* the world-space builders of the station: hull plating wears the sci-fi panel texture, PLAIN carries untextured paint,
 * metal, glow and the thin stuff */
static pmc *HULL, *PLAIN;
void ps_chunk(prop_set *s, float metres); /* props.c: the cell size instanced props are culled in */

static void glow_seam(pm *b, float r, float y0, float y1, float a, float w, pcol col, float gain) {
  pm_layer(b, PL_GLOW);
  float ca = cosf(a), sa = sinf(a), rr = r * 1.012f;
  t3_vec3 tg = V3(-sa, 0, ca), nr = V3(ca * rr, 0, sa * rr);
  pm_quad(b, V3(nr.x + tg.x * w, y0, nr.z + tg.z * w), V3(nr.x - tg.x * w, y0, nr.z - tg.z * w), V3(nr.x - tg.x * w, y1, nr.z - tg.z * w),
          V3(nr.x + tg.x * w, y1, nr.z + tg.z * w), glow(col, gain));
}

/* a small box standing on a cylinder hull (axis y) at angle a and height y, its up pointing out of the hull */
static void greeble(pm *b, float r, float y, float a, float w, float h, float d, pcol c) {
  pm_push(b);
  pm_roty(b, -a);
  pm_move(b, r - 0.06f, y, 0);
  pm_rotz(b, -CW_PI * 0.5f);
  pm_box(b, w, h, d, dk(c, 0.7f), c);
  pm_pop(b);
}

/* a hull torn open at y (s = +1: the end at +y, -1: the end at 0): jagged petals of plate bent outward, a dark recessed
 * bulkhead, ribs showing, and wire ends that glow */
static void torn_end(pm *b, float r, float y, float s, uint32_t seed) {
  pm_layer(b, PL_METAL);
  const int np = 15;
  for (int k = 0; k < np; k++) {
    float a0 = CW_TAU * (float)k / np, a1 = CW_TAU * (float)(k + 1) / np, am = (a0 + a1) * 0.5f;
    float len = 1.2f + 0.55f * r * hf(seed + (uint32_t)k * 13u), flare = 1.0f + 0.35f * hf(seed + (uint32_t)k * 29u);
    float tw = 0.2f + 0.3f * hf(seed + (uint32_t)k * 41u);
    t3_vec3 b0 = V3(cosf(a0) * r, y, sinf(a0) * r), b1 = V3(cosf(a1) * r, y, sinf(a1) * r);
    float ta0 = am - (am - a0) * tw, ta1 = am + (a1 - am) * tw;
    t3_vec3 t0 = V3(cosf(ta0) * r * flare, y + s * len, sinf(ta0) * r * flare), t1 = V3(cosf(ta1) * r * flare, y + s * len, sinf(ta1) * r * flare);
    pcol c = pc_mix(K.hull2, K.dark, 0.35f + 0.4f * hf(seed + (uint32_t)k * 53u));
    pm_quad(b, b0, b1, t1, t0, c);
    pm_quad(b, b1, b0, t0, t1, dk(c, 0.6f));
  }
  pm_push(b);
  pm_move(b, 0, y - s * r * 0.35f, 0);
  pm_cyl_ex(b, r * 0.97f, r * 0.97f, 0.3f, 22, K.dark, K.dark, true, true);
  pm_pop(b);
  for (int k = 0; k < 4; k++) { /* ribs across the opening */
    float a = CW_PI * (float)k / 4.0f + hf(seed + 5u * (uint32_t)k) * 0.3f;
    t3_vec3 e0 = V3(cosf(a) * r, y - s * 0.4f, sinf(a) * r), e1 = V3(-cosf(a) * r, y - s * 0.4f, -sinf(a) * r);
    pm_rod(b, e0, e1, 0.28f, 6, dk(K.hull2, 0.8f));
  }
  pm_layer(b, PL_GLOW);
  for (int k = 0; k < 7; k++) { /* cut wires: tips that spark */
    float a = CW_TAU * hf(seed + 100u + (uint32_t)k), rr = r * (0.2f + 0.7f * hf(seed + 120u + (uint32_t)k));
    pm_push(b);
    pm_move(b, cosf(a) * rr, y + s * (0.5f + 2.0f * hf(seed + 140u + (uint32_t)k)), sinf(a) * rr);
    pm_ball(b, 0.22f, 0.22f, 0.22f, 0, 0, glow(k & 1 ? K.cyan : K.orange, 5.0f), glow(K.white, 6.0f));
    pm_pop(b);
  }
}

/* a docking port at one end of a hull (y = 0 end: s = -1): a collar, a hatch ring, two amber lamps */
static void dock_port(pm *b, float r, float y, float s) {
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, 0, y, 0);
  if (s < 0) pm_scale(b, 1, -1, 1);
  pm_cyl_ex(b, r * 0.5f, r * 0.42f, r * 0.34f, 16, dk(K.hull2, 0.7f), K.hull2, false, true);
  pm_move(b, 0, r * 0.34f, 0);
  pm_ring(b, r * 0.42f, r * 0.04f, 16, 4, K.yellow);
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  for (int k = 0; k < 2; k++) {
    pm_push(b);
    pm_move(b, (k ? 1 : -1) * r * 0.62f, y + s * r * 0.02f, 0);
    pm_ball(b, 0.35f, 0.35f, 0.35f, 0, 0, glow(K.yellow, 4.0f), glow(K.white, 5.0f));
    pm_pop(b);
  }
}

/* a pressure module from p along dir: plating, hazard bands, ribs, window rows, a lit seam, pipes and boxes; torn bit 0 = the
 * start end is ripped open, bit 1 = the far end; kind picks the paint (0 white, 1 cargo orange, 2 lab cyan) */
static void hull_module(t3_vec3 p, t3_vec3 dir, float len, float r, int kind, uint32_t seed, int torn) {
  pm *b = pose_axis(HULL, p, dir);
  pcol paint = kind == 1 ? pc_mix(K.hull, K.orange, 0.4f) : kind == 2 ? pc_mix(K.hull, K.cyan, 0.16f) : K.hull;
  pm_layer(b, PL_MAIN);
  pm_cyl_ex(b, r, r, len, 24, paint, paint, false, false);
  /* painted bands, wider at the ends */
  int nb = (int)(len / (r * 1.6f)) + 2;
  for (int i = 0; i < nb; i++) {
    pm_push(b);
    pm_move(b, 0, (len - r * 0.5f) * (float)i / (float)(nb - 1), 0);
    pm_cyl_ex(b, r * 1.012f, r * 1.012f, r * 0.5f, 24, kind == 1 ? K.yellow : K.orange, kind == 1 ? K.yellow : K.orange, false, false);
    pm_pop(b);
  }
  for (int e = 0; e < 2; e++) { /* the caps (a dished end) unless torn */
    if (torn & (1 << e)) continue;
    pm_push(b);
    pm_move(b, 0, e ? len : 0, 0);
    if (!e) pm_scale(b, 1, -1, 1);
    pm_cyl_ex(b, r, r * 0.55f, r * 0.2f, 24, paint, dk(paint, 0.8f), false, true);
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  int nrib = (int)(len / (r * 0.9f)) + 1;
  for (int i = 0; i <= nrib; i++) {
    pm_push(b);
    pm_move(b, 0, len * (float)i / (float)nrib - r * 0.06f, 0);
    pm_cyl_ex(b, r * 1.04f, r * 1.04f, r * 0.12f, 24, dk(K.hull2, 0.7f), K.hull2, false, false);
    pm_pop(b);
  }
  for (int k = 0; k < 2; k++) { /* two pipe runs along the hull */
    float a = CW_TAU * hf(seed + 3u * (uint32_t)k + 1u);
    t3_vec3 q0 = V3(cosf(a) * r * 1.07f, len * 0.05f, sinf(a) * r * 1.07f), q1 = V3(q0.x, len * 0.95f, q0.z);
    pm_rod(b, q0, q1, 0.38f, 7, dk(K.hull2, 0.8f));
  }
  int ng = (int)(len * r / 90.0f) + 3;
  for (int i = 0; i < ng; i++)
    greeble(b, r, len * hf(seed + 200u + (uint32_t)i), CW_TAU * hf(seed + 300u + (uint32_t)i), 0.8f + 2.2f * hf(seed + 400u + (uint32_t)i), 0.5f + 1.1f * hf(seed + 500u + (uint32_t)i),
            0.8f + 2.0f * hf(seed + 600u + (uint32_t)i), hf(seed + 700u + (uint32_t)i) < 0.3f ? K.orange : K.hull2);
  for (int i = 0; i < nrib; i++) { /* window rows between the ribs */
    float y0 = len * ((float)i + 0.38f) / (float)nrib, y1 = y0 + r * 0.2f;
    if (y1 > len - 1) continue;
    pm_push(b);
    window_band(b, r, y0, y1, (int)(r * 1.4f) + 8, 0.085f, kind == 2 ? K.cyan : K.warm, 3.4f, seed + (uint32_t)i * 977u, 0.8f);
    pm_pop(b);
  }
  glow_seam(b, r, len * 0.04f, len * 0.96f, CW_TAU * hf(seed + 11u), 0.1f, kind == 1 ? K.orange : K.cyan, 3.2f);
  if (torn & 1) torn_end(b, r, 0, -1, seed + 1000u);
  else dock_port(b, r, 0, -1);
  if (torn & 2) torn_end(b, r, len, 1, seed + 2000u);
  else dock_port(b, r, len, 1);
  pm_pop(b);
}

/* a sphere node: four docking ports, a belt of windows and an equatorial ring (a junction) */
static void hull_node(t3_vec3 p, float r, uint32_t seed) {
  pm *b = pmc_at(HULL, p);
  pm_push(b);
  pm_move(b, p.x, p.y, p.z);
  pm_layer(b, PL_MAIN);
  pm_smooth(b, true);
  pm_ball(b, r, r, r, 2, 0.0f, K.hull, K.hull);
  pm_smooth(b, false);
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_rotx(b, CW_PI * 0.5f);
  pm_ring(b, r * 1.02f, r * 0.07f, 40, 6, K.orange);
  pm_pop(b);
  for (int k = 0; k < 6; k++) {
    pm_push(b);
    if (k < 4) pm_roty(b, CW_PI * 0.5f * (float)k), pm_rotx(b, CW_PI * 0.5f), pm_move(b, 0, r * 0.88f, 0);
    else pm_move(b, 0, k == 4 ? r * 0.9f : -r * 0.9f, 0), pm_scale(b, 1, k == 4 ? 1.0f : -1.0f, 1);
    pm_cyl_ex(b, r * 0.3f, r * 0.26f, r * 0.34f, 12, dk(K.hull2, 0.7f), K.hull2, false, true);
    pm_pop(b);
  }
  pm_layer(b, PL_GLOW);
  for (int k = 0; k < 18; k++) {
    float a = CW_TAU * (float)k / 18.0f + 0.1f;
    if (hf(seed + (uint32_t)k * 31u) > 0.7f) continue;
    pm_push(b);
    pm_roty(b, -a);
    pm_move(b, r * 0.985f, r * 0.12f, 0);
    pm_quad(b, V3(0, -r * 0.05f, r * 0.07f), V3(0, -r * 0.05f, -r * 0.07f), V3(0, r * 0.05f, -r * 0.07f), V3(0, r * 0.05f, r * 0.07f), glow(K.warm, 3.4f));
    pm_pop(b);
  }
  pm_pop(b);
}

/* a square lattice girder between two points: four chords, a frame and a diagonal on each face per bay */
static void truss_span(pmc *c, t3_vec3 a, t3_vec3 e, float w, float bay, pcol col) {
  t3_vec3 d = vsub(e, a);
  float L = vlen(d);
  int nb = (int)(L / bay);
  if (nb < 1) nb = 1;
  pm *b = pose_axis(c, a, d);
  pm_layer(b, PL_METAL);
  float h = w * 0.5f, st = L / (float)nb;
  for (int k = 0; k < 4; k++) {
    float sx = (k & 1) ? h : -h, sz = (k & 2) ? h : -h;
    pm_beam(b, V3(sx, 0, sz), V3(sx, L, sz), w * 0.09f, col);
  }
  for (int i = 0; i <= nb; i++) {
    float y = st * (float)i;
    pm_beam(b, V3(-h, y, -h), V3(h, y, -h), w * 0.05f, dk(col, 0.85f));
    pm_beam(b, V3(-h, y, h), V3(h, y, h), w * 0.05f, dk(col, 0.85f));
    pm_beam(b, V3(-h, y, -h), V3(-h, y, h), w * 0.05f, dk(col, 0.85f));
    pm_beam(b, V3(h, y, -h), V3(h, y, h), w * 0.05f, dk(col, 0.85f));
    if (i < nb) {
      float y1 = y + st;
      pm_beam(b, V3(-h, y, -h), V3(h, y1, -h), w * 0.04f, dk(col, 0.75f));
      pm_beam(b, V3(h, y, h), V3(-h, y1, h), w * 0.04f, dk(col, 0.75f));
      pm_beam(b, V3(-h, y, h), V3(-h, y1, -h), w * 0.04f, dk(col, 0.75f));
      pm_beam(b, V3(h, y, -h), V3(h, y1, h), w * 0.04f, dk(col, 0.75f));
    }
  }
  pm_pop(b);
}

/* a solar wing: a boom from root along boom_dir, then panels across it, spanning along span_dir; some panels missing or
 * bent, a lit edge so the shadow side still reads */
static void solar_wing(t3_vec3 root, t3_vec3 boom_dir, t3_vec3 span_dir, float boom_len, float pw, float pl, int npan, uint32_t seed) {
  t3_vec3 bd = vnorm(boom_dir), sd = vnorm(span_dir), nrm = vnorm(vcross(sd, bd));
  truss_span(PLAIN, root, vadd(root, vmul(bd, boom_len)), 1.4f, 5.0f, K.hull2);
  for (int i = 0; i < npan; i++) {
    uint32_t hs = seed + (uint32_t)i * 4177u;
    if (i > 0 && hf(hs) < 0.14f) continue; /* gone */
    t3_vec3 c0 = vadd(root, vmul(bd, boom_len * (0.2f + 0.8f * ((float)i + 0.5f) / (float)npan) * 0.98f));
    for (int side = -1; side <= 1; side += 2) {
      pm *b = pose(PLAIN, vadd(c0, vmul(sd, side * (pw * 0.5f + 1.0f))), sd, nrm, bd);
      float bend = hf(hs + 9u) < 0.25f ? (hf(hs + 13u) - 0.5f) * 0.5f : (hf(hs + 17u) - 0.5f) * 0.04f;
      pm_rotz(b, bend * (float)side);
      pm_layer(b, PL_MAIN);
      pm_box_c(b, pw, 0.25f, pl, pc_mix(K.hull2, K.dark, 0.3f));
      pm_layer(b, PL_METAL); /* the back: ribs and a cross brace */
      for (int k = 0; k <= 4; k++) pm_beam(b, V3(-pw * 0.5f + pw * (float)k / 4.0f, -0.22f, -pl * 0.5f), V3(-pw * 0.5f + pw * (float)k / 4.0f, -0.22f, pl * 0.5f), 0.32f, dk(K.hull2, 0.6f));
      pm_beam(b, V3(-pw * 0.5f, -0.22f, -pl * 0.5f), V3(pw * 0.5f, -0.22f, pl * 0.5f), 0.28f, dk(K.hull2, 0.55f));
      pm_layer(b, PL_MAIN);
      int cx = (int)(pw / 3.0f), cz = (int)(pl / 3.0f);
      for (int ix = 0; ix < cx; ix++)
        for (int iz = 0; iz < cz; iz++) {
          float x0 = -pw * 0.5f + pw * ((float)ix + 0.07f) / (float)cx, x1 = -pw * 0.5f + pw * ((float)ix + 0.93f) / (float)cx;
          float z0 = -pl * 0.5f + pl * ((float)iz + 0.07f) / (float)cz, z1 = -pl * 0.5f + pl * ((float)iz + 0.93f) / (float)cz;
          pcol cc = pc_mix(K.cell, rgb(0.1f, 0.26f, 0.62f), 0.25f + 0.75f * hf(hs + (uint32_t)(ix * 31 + iz * 7)));
          pm_quad(b, V3(x0, 0.14f, z1), V3(x1, 0.14f, z1), V3(x1, 0.14f, z0), V3(x0, 0.14f, z0), cc);
        }
      pm_layer(b, PL_GLOW);
      float ex = side > 0 ? pw * 0.5f : -pw * 0.5f;
      pm_quad(b, V3(ex - 0.12f, 0.16f, pl * 0.5f), V3(ex + 0.12f, 0.16f, pl * 0.5f), V3(ex + 0.12f, 0.16f, -pl * 0.5f), V3(ex - 0.12f, 0.16f, -pl * 0.5f), glow(K.cyan, 2.6f));
      pm_pop(b);
    }
  }
}

/* ── the deck: a catwalk each side of the road and a keel girder under it, so the road reads as a station's floor ── */

static void build_deck(cw_node root, const track *t) {
  pmc *c = pmc_new(180, 5);
  pmc_texture_metres(c, 4.0f);
  for (int i = 0; i + 6 < t->n; i += 6) {
    if (!deck_ok(t, i) || !deck_ok(t, i + 6) || !deck_ok(t, i + 3)) continue;
    const tsample *a = &t->s[i], *e = &t->s[i + 6];
    uint32_t h = hsh((uint32_t)(i / 6) * 2654435761u + 5u);
    float hw = a->hw;
    /* catwalks: in long runs with gaps, a different one each side */
    for (int side = -1; side <= 1; side += 2) {
      int blk = (i / 60) * 2 + (side > 0);
      float on = hf((uint32_t)blk * 40503u + 17u);
      if (on < 0.18f) continue;
      float l0 = side * (hw + 0.9f), l1 = side * (hw + 4.2f), ly = -0.32f;
      pm *b = pmc_at(c, a->p);
      pm_push(b);
      pm_layer(b, PL_MAIN);
      /* the walkway: dark plate with a lighter top */
      t3_vec3 A = tp(a, l0, ly), B = tp(a, l1, ly), Cc = tp(e, l1, ly), D = tp(e, l0, ly);
      if (side > 0) pm_quad(b, A, B, Cc, D, pc_mix(K.panel, K.dark, 0.3f)); /* seen from above */
      else pm_quad(b, B, A, D, Cc, pc_mix(K.panel, K.dark, 0.3f));
      /* the fascia below its outer edge */
      t3_vec3 B2 = tp(a, l1, ly - 1.1f), C2 = tp(e, l1, ly - 1.1f);
      if (side > 0) pm_quad(b, B2, C2, Cc, B, K.dark);
      else pm_quad(b, C2, B2, B, Cc, K.dark);
      pm_layer(b, PL_GLOW);
      /* an edge light, cyan on one side and magenta on the other, in dashes */
      {
        t3_vec3 g0 = tp(a, l1 - side * 0.2f, ly + 0.02f), g1 = tp(a, l1, ly + 0.02f), g2 = tp(e, l1, ly + 0.02f), g3 = tp(e, l1 - side * 0.2f, ly + 0.02f);
        pcol gc = side > 0 ? K.cyan : K.magenta;
        if (side > 0) pm_quad(b, g0, g3, g2, g1, glow(gc, 4.2f));
        else pm_quad(b, g3, g0, g1, g2, glow(gc, 4.2f));
      }
      pm_pop(b);
      /* a post with a lamp every 18 m, leaning out a little */
      if ((i % 18) == 0) {
        pm *pb = pose_road(c, a, side * (hw + 3.9f), ly);
        pm_layer(pb, PL_METAL);
        pm_box(pb, 0.18f, 1.3f, 0.18f, dk(K.hull2, 0.6f), K.hull2);
        pm_layer(pb, PL_GLOW);
        pm_push(pb);
        pm_move(pb, 0, 1.38f, 0);
        pm_ball(pb, 0.17f, 0.17f, 0.17f, 0, 0, glow(side > 0 ? K.cyan : K.magenta, 4.0f), glow(K.white, 5.0f));
        pm_pop(pb);
        pm_pop(pb);
      }
    }
    /* the keel: a triangular lattice hung under the deck on five metre bays */
    if (i % 12 == 0 && (h & 7) != 0) continue;
    if (((i / 30) & 3) == 3) continue; /* torn away here */
    pm *b = pmc_at(c, a->p);
    pm_push(b);
    pm_layer(b, PL_METAL);
    t3_vec3 bt0 = tp(a, 0, -4.4f), bt1 = tp(e, 0, -4.4f);
    pcol st = K.hull2;
    pm_beam(b, bt0, bt1, 0.3f, st);
    for (int sd = -1; sd <= 1; sd += 2) {
      t3_vec3 t0 = tp(a, sd * (hw - 1.5f), -0.5f), t1 = tp(e, sd * (hw - 1.5f), -0.5f);
      pm_beam(b, t0, t1, 0.24f, dk(st, 0.85f));
      pm_beam(b, t0, bt0, 0.14f, dk(st, 0.7f));
      pm_beam(b, t1, bt0, 0.14f, dk(st, 0.7f));
    }
    pm_beam(b, tp(a, -(hw - 1.5f), -0.5f), tp(a, hw - 1.5f, -0.5f), 0.2f, dk(st, 0.8f));
    pm_pop(b);
  }
  pmc_finish(c, root, "Deck", true, TEX_METAL);
}

/* ── animated things: a registry the per-frame callback walks ──────────────────────────────────────── */

typedef struct spinner {
  t3_object *o;
  t3_vec3 axis;
  float rate, phase;
} spinner;
static spinner SPIN[32];
static int nspin;
static void spin_add(t3_object *o, t3_vec3 axis, float rate, float phase) {
  if (nspin < (int)(sizeof SPIN / sizeof SPIN[0])) SPIN[nspin++] = (spinner){ o, axis, rate, phase };
}

/* a finished builder as one group of meshes (one per layer), hung under parent; the group is returned so a map can move it */
static t3_object *model_group(cw_node parent, const char *name, pm *b, int tex, bool cast) {
  prop_model m = pm_finish(b);
  t3_object *g = t3_group_new();
  for (int l = 0; l < PL_COUNT; l++) {
    if (!m.g[l]) continue;
    t3_material *mat = prop_material(l, l == PL_MAIN ? tex : -1, (pcol){ 1, 1, 1 }, 1);
    t3_mesh *me = t3_mesh_new(m.g[l], mat);
    me->base.cast_shadow = cast && l != PL_GLOW;
    me->base.receive_shadow = true;
    t3_object_add(g, me);
    t3_release(me);
  }
  prop_model_free(&m);
  hang(parent, name, g);
  t3_release(g);
  return g;
}

/* ── the wheel: a habitat ring round the spine, turning slowly ─────────────────────────────────────── */

static void build_wheel(cw_node root, t3_vec3 c, float R, float r) {
  pm *b = pm_new(777);
  pm_vary(b, 0.03f);
  pm_layer(b, PL_MAIN);
  pm_ring(b, R, r, 84, 14, K.hull);
  /* paint: two orange bands on the rim, thinner rings of dark plate between */
  for (int k = -1; k <= 1; k += 2) {
    pm_push(b);
    pm_move(b, 0, 0, k * r * 0.62f);
    pm_ring(b, R + r * 0.78f, r * 0.2f, 84, 5, K.orange);
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  /* the hub and six spokes with a bracing ring half way */
  pm_push(b);
  pm_rotx(b, CW_PI * 0.5f);
  pm_move(b, 0, -r * 3.2f, 0);
  pm_cyl_ex(b, r * 1.5f, r * 1.5f, r * 6.4f, 18, dk(K.hull2, 0.8f), K.hull2, true, true);
  pm_pop(b);
  float rin = r * 1.5f;
  for (int k = 0; k < 6; k++) {
    float a = CW_TAU * (float)k / 6.0f + 0.2f, ca = cosf(a), sa = sinf(a), pa = a + 0.5f;
    t3_vec3 h0 = V3(ca * rin, sa * rin, 0), h1 = V3(ca * (R - r * 0.9f), sa * (R - r * 0.9f), 0);
    pm_beam(b, h0, h1, r * 0.4f, dk(K.hull2, 0.85f));
    for (int sd = -1; sd <= 1; sd += 2) {
      t3_vec3 o0 = V3(ca * rin, sa * rin, sd * r * 0.4f), o1 = V3(ca * (R - r * 0.9f), sa * (R - r * 0.9f), sd * r * 0.4f);
      pm_beam(b, o0, o1, r * 0.12f, dk(K.hull2, 0.7f));
    }
    (void)pa;
  }
  pm_ring(b, R * 0.52f, r * 0.12f, 60, 5, dk(K.hull2, 0.8f));
  /* lit windows on both faces of the rim, a band of them every few degrees, some dark */
  pm_layer(b, PL_GLOW);
  const int nw = 150;
  for (int k = 0; k < nw; k++) {
    if (hf(900u + (uint32_t)k * 31u) > 0.78f) continue;
    float a0 = CW_TAU * ((float)k + 0.18f) / nw, a1 = CW_TAU * ((float)k + 0.82f) / nw;
    for (int row = 0; row < 2; row++) {
      float d0 = (row ? 0.1f : -0.62f) * r, d1 = (row ? 0.62f : -0.1f) * r;
      float z0 = sqrtf(fmaxf(r * r - d0 * d0, 0)) * 1.012f, z1 = sqrtf(fmaxf(r * r - d1 * d1, 0)) * 1.012f;
      t3_vec3 A = V3((R + d0) * cosf(a0), (R + d0) * sinf(a0), z0), B = V3((R + d1) * cosf(a0), (R + d1) * sinf(a0), z1);
      t3_vec3 C = V3((R + d1) * cosf(a1), (R + d1) * sinf(a1), z1), D = V3((R + d0) * cosf(a1), (R + d0) * sinf(a1), z0);
      pcol col = hf(1900u + (uint32_t)(k * 3 + row)) < 0.16f ? K.cyan : K.warm;
      pm_quad(b, A, B, C, D, glow(col, 3.2f));
      t3_vec3 A2 = V3(A.x, A.y, -A.z), B2 = V3(B.x, B.y, -B.z), C2 = V3(C.x, C.y, -C.z), D2 = V3(D.x, D.y, -D.z);
      pm_quad(b, A2, D2, C2, B2, glow(col, 3.2f));
    }
  }
  t3_object *g = model_group(root, "Wheel", b, TEX_WALL_TECH, true);
  t3_object_set_position(g, c.x, c.y, c.z);
  spin_add(g, V3(0, 0, 1), 0.035f, 0);
}

/* ── the station: modules, nodes and trusses in a line, a break in the middle, wings and dishes ───── */

typedef struct modspec {
  char kind;       /* 'm' module, 'n' node, 't' truss */
  float z, len, r; /* z where it starts, its length, its radius */
  int style, torn;
} modspec;

static void build_station(cw_node root, prop_set *ps) {
  static const modspec M[] = {
    { 'm', -120, 52, 15, 1, 1 }, { 'm', -62, 46, 19, 0, 0 }, { 'm', -10, 58, 22, 2, 0 },  { 't', 52, 46, 7, 0, 0 },
    { 'm', 104, 54, 18, 0, 0 },  { 'n', 190, 0, 19, 0, 0 },  { 'm', 214, 52, 17, 1, 0 },  { 't', 270, 20, 6, 0, 0 },
    { 'm', 294, 90, 14, 2, 0 },  { 'm', 392, 52, 17, 0, 2 }, { 'm', 466, 50, 18, 0, 1 },  { 'n', 540, 0, 20, 0, 0 },
    { 'm', 564, 44, 20, 1, 0 },  { 't', 612, 30, 7, 0, 0 },  { 'm', 646, 62, 21, 0, 0 },  { 'm', 712, 56, 16, 2, 2 },
  };
  const float X = 75, Y = 22;
  for (int i = 0; i < (int)(sizeof M / sizeof M[0]); i++) {
    const modspec *m = &M[i];
    bool far = m->z > 440; /* past the break the line is sheared and tilted */
    t3_vec3 p = V3(X + (far ? 11.0f : 0.0f), Y + (far ? 5.0f : 0.0f), m->z);
    t3_vec3 dir = far ? vnorm(V3(0.03f, -0.015f, 1)) : V3(0, 0, 1);
    uint32_t sd = 31u * (uint32_t)i + 5u;
    if (m->kind == 'm') hull_module(p, dir, m->len, m->r, m->style, sd, m->torn);
    else if (m->kind == 'n') hull_node(p, m->r, sd);
    else truss_span(PLAIN, p, vadd(p, vmul(dir, m->len)), m->r * 1.1f, 6.0f, K.hull2);
  }
  /* the break: loose girders hanging across the gap, one end bare */
  truss_span(PLAIN, V3(X, Y + 6, 444), V3(X + 5, Y + 9, 462), 3.5f, 4.0f, K.hull2);
  truss_span(PLAIN, V3(X - 6, Y - 4, 446), V3(X + 2, Y - 10, 470), 3.0f, 4.0f, dk(K.hull2, 0.8f));
  /* solar wings off the two nodes, up and down, panels facing the sun's side */
  for (int n = 0; n < 2; n++) {
    float z = n ? 540 : 190, rr = n ? 20 : 19;
    float x = X + (n ? 11.0f : 0.0f), y = Y + (n ? 5.0f : 0.0f);
    for (int sd = -1; sd <= 1; sd += 2)
      solar_wing(V3(x, y + sd * rr, z), V3(0, sd, 0), V3(1, 0, 0), n ? 70.0f : 95.0f, 20.0f, 14.0f, 6, 3000u + (uint32_t)(n * 50 + (sd + 1) * 10));
  }
  /* dishes and antennas on the roofs of the big modules (instanced props) */
  static const struct { float z, r; int n; } F[] = { { -39, 22, 3 }, { 120, 18, 2 }, { 674, 21, 5 }, { 628, 21, 4 }, { 330, 14, 2 } };
  for (int i = 0; i < (int)(sizeof F / sizeof F[0]); i++)
    for (int k = 0; k < F[i].n; k++) {
      uint32_t h = hsh((uint32_t)(i * 17 + k) * 2654435761u + 9u);
      float a = (hf(h) - 0.5f) * 1.6f, z = F[i].z + (hf(h ^ 7u) - 0.5f) * 24.0f;
      float far = z > 440 ? 1.0f : 0.0f;
      t3_vec3 pos = V3(X + far * 11 + sinf(a) * F[i].r * 0.97f, Y + far * 5 + cosf(a) * F[i].r * 0.97f, z);
      prop_kind kind = (k & 1) ? PK_DISH : PK_ANTENNA;
      float sc = kind == PK_DISH ? 1.6f + hf(h ^ 99u) : 1.3f + 0.7f * hf(h ^ 55u);
      t3_quat q = t3_quat_from_axis_angle(V3(0, 0, 1), -a);
      ps_add_ex(ps, kind, (int)(h & 3) % prop_variants(kind), 0, pos, q, V3(sc, sc, sc), (pcol){ 1, 1, 1 }, h);
    }
}

/* ── lights, signs, banners ─────────────────────────────────────────────────────────────────────────── */

/* a shadowless clustered point light (candela-like energy, falls to nothing at range) */
static cw_node point_light(cw_node root, const char *name, t3_vec3 p, pcol c, float energy, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(root, l);
  cw_node3d_set_position(l, p);
  cw_node_set(l, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  return l;
}

/* holographic sign patterns, white on black (the material colours them): 0 chevrons, 1 ring with a bolt, 2 hazard bars,
 * 3 gear, 4 arrows pair */
static t3_texture *make_holo(int kind) {
  enum { W = 256, H = 128 };
  static uint8_t px[W * H * 4];
  for (int j = 0; j < H; j++)
    for (int i = 0; i < W; i++) {
      float x = ((float)i + 0.5f) / W * 2 - 1, y = ((float)j + 0.5f) / H * 2 - 1; /* x -1..1, y -1..1 */
      float v = 0;
      float edge = fmaxf(fabsf(x) * 0.5f, fabsf(y)); /* a frame, the board is twice as wide as tall */
      float border = sstep(0.86f, 0.9f, edge) * (1 - sstep(0.96f, 1.0f, edge));
      if (kind == 0) { /* >>> */
        float xs = x * 2.0f, d = fabsf(y) * 1.1f;
        float k = fmodf(xs * 2.2f - d * 1.2f + 8.0f, 1.0f);
        v = sstep(0.0f, 0.06f, k) * (1 - sstep(0.34f, 0.4f, k)) * (1 - sstep(0.78f, 0.9f, fabsf(y)));
      } else if (kind == 1) { /* a ring with a lightning bolt */
        float r = sqrtf(x * x * 4.0f + y * y);
        float ring = sstep(0.66f, 0.7f, r) * (1 - sstep(0.8f, 0.84f, r));
        float bx = x * 2.0f, by = y;
        float bolt = 0;
        float yy = by * 1.1f;
        float cx = 0.18f * (yy > 0 ? 1.0f : -1.0f) * 0 + (-0.45f * yy);
        float lx = bx - cx - (yy > 0 ? 0.12f : -0.12f);
        bolt = (1 - sstep(0.06f, 0.1f, fabsf(lx))) * (1 - sstep(0.5f, 0.56f, fabsf(yy)));
        v = fmaxf(ring, bolt);
      } else if (kind == 2) { /* hazard bars */
        float k = fmodf((x * 3.0f + y * 1.5f) * 2.0f + 8.0f, 1.0f);
        v = sstep(0.0f, 0.05f, k) * (1 - sstep(0.45f, 0.5f, k)) * (1 - sstep(0.7f, 0.76f, fabsf(y)));
      } else if (kind == 3) { /* a gear */
        float r = sqrtf(x * x * 4.0f + y * y), a = atan2f(y, x * 2.0f);
        float teeth = 0.78f + 0.1f * (sinf(a * 8.0f) > 0 ? 1.0f : 0.0f);
        float body = sstep(teeth, teeth - 0.04f, r) * sstep(0.36f, 0.4f, r);
        v = body;
      } else { /* two arrows pointing right, one over the other */
        float xs = x * 2.0f;
        for (int a = -1; a <= 1; a += 2) {
          float yy = y - (float)a * 0.42f;
          float shaft = (1 - sstep(0.07f, 0.1f, fabsf(yy))) * (1 - sstep(0.55f, 0.6f, fabsf(xs + 0.4f)));
          float head = (1 - sstep(0.0f, 0.04f, fabsf(yy) - (0.5f - xs) * 0.5f)) * sstep(0.2f, 0.24f, xs) * (1 - sstep(0.7f, 0.74f, xs));
          v = fmaxf(v, fmaxf(shaft, head * (xs > 0.2f ? 1.0f : 0.0f)));
        }
      }
      v = fmaxf(v, border);
      float scan = 0.78f + 0.22f * sinf((float)j * 1.9f);
      uint8_t g = byte_(v * scan);
      uint8_t *p = &px[(j * W + i) * 4];
      p[0] = p[1] = p[2] = g, p[3] = 255;
    }
  t3_texture *t = tex_from(W, H, px, false, false);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  return t;
}

/* a registry of hologram materials: flicker and scan scroll in the per-frame callback */
typedef struct holo {
  t3_material *m;
  t3_texture *tex;
  t3_color base;
  float phase, rate;
} holo;
static holo HOLO[24];
static int nholo;
static t3_texture *HOLO_TEX[6];

/* a hologram board: a glowing frame of beams on a pole or two, and a translucent picture. p is the base centre, yaw turns
 * it (its picture faces +z of the yawed frame), w by h metres, lifted `lift` m above p */
static void holo_sign(cw_node root, t3_vec3 p, float yaw, float w, float h, float lift, int kind, pcol col, float gain, bool pole) {
  pm *b = pmc_at(PLAIN, p);
  pm_push(b);
  pm_move(b, p.x, p.y, p.z);
  pm_roty(b, yaw);
  pm_layer(b, PL_METAL);
  float hw = w * 0.5f;
  if (pole) {
    pm_beam(b, V3(-hw * 0.7f, 0, 0), V3(-hw * 0.7f, lift, 0), 0.4f, K.hull2);
    pm_beam(b, V3(hw * 0.7f, 0, 0), V3(hw * 0.7f, lift, 0), 0.4f, K.hull2);
    pm_beam(b, V3(-hw * 0.7f, lift * 0.55f, 0), V3(hw * 0.7f, lift * 0.55f, 0), 0.25f, dk(K.hull2, 0.8f));
  }
  pm_beam(b, V3(-hw, lift, 0), V3(hw, lift, 0), 0.3f, K.hull2);
  pm_beam(b, V3(-hw, lift + h, 0), V3(hw, lift + h, 0), 0.3f, K.hull2);
  pm_beam(b, V3(-hw, lift, 0), V3(-hw, lift + h, 0), 0.3f, K.hull2);
  pm_beam(b, V3(hw, lift, 0), V3(hw, lift + h, 0), 0.3f, K.hull2);
  pm_layer(b, PL_GLOW);
  pm_quad(b, V3(-hw, lift - 0.12f, 0.16f), V3(hw, lift - 0.12f, 0.16f), V3(hw, lift - 0.12f, -0.16f), V3(-hw, lift - 0.12f, -0.16f), glow(col, 3.0f));
  pm_pop(b);
  if (nholo >= (int)(sizeof HOLO / sizeof HOLO[0])) return;
  if (!HOLO_TEX[kind]) HOLO_TEX[kind] = make_holo(kind);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_material_set_map(m, HOLO_TEX[kind]);
  t3_color base = { col.r * gain, col.g * gain, col.b * gain };
  m->color = base, m->transparent = true, m->blending = T3_ADDITIVE_BLENDING, m->depth_write = false, m->side = T3_DOUBLE_SIDE, m->fog = false;
  t3_geometry *g = t3_plane_geometry_new(w * 0.96f, h * 0.96f, 1, 1);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false, me->base.receive_shadow = false;
  t3_object_set_position(me, p.x + sinf(yaw) * 0.0f, p.y + lift + h * 0.5f, p.z);
  t3_object_set_rotation(me, 0, yaw, 0);
  hang(root, "Holo", &me->base);
  HOLO[nholo++] = (holo){ m, HOLO_TEX[kind], base, hf((uint32_t)nholo * 977u) * 6.28f, 0.6f + 0.8f * hf((uint32_t)nholo * 331u) };
  t3_release(me), t3_release(g);
}

/* a torn league banner hanging by two chains: stripes of two colours, a zig-zag lower edge, both faces */
static void banner(pm *b, float w, float h, pcol c1, pcol c2, uint32_t seed) {
  pm_layer(b, PL_METAL);
  pm_beam(b, V3(-w * 0.5f - 0.2f, 0, 0), V3(w * 0.5f + 0.2f, 0, 0), 0.25f, K.hull2);
  pm_layer(b, PL_MAIN);
  int ns = (int)(w / 1.1f);
  if (ns < 3) ns = 3;
  for (int i = 0; i < ns; i++) {
    float x0 = -w * 0.5f + w * (float)i / (float)ns, x1 = -w * 0.5f + w * (float)(i + 1) / (float)ns;
    float d0 = h * (0.7f + 0.3f * hf(seed + (uint32_t)i * 7u)), d1 = h * (0.7f + 0.3f * hf(seed + (uint32_t)(i + 1) * 7u));
    pcol c = (i & 1) ? c1 : c2;
    pm_quad(b, V3(x0, 0, 0), V3(x1, 0, 0), V3(x1, -d1, 0), V3(x0, -d0, 0), c);
    pm_quad(b, V3(x1, 0, 0), V3(x0, 0, 0), V3(x0, -d0, 0), V3(x1, -d1, 0), c);
  }
  pm_layer(b, PL_GLOW);
  pm_quad(b, V3(-w * 0.5f, -0.15f, 0.04f), V3(w * 0.5f, -0.15f, 0.04f), V3(w * 0.5f, -0.35f, 0.04f), V3(-w * 0.5f, -0.35f, 0.04f), glow(c1, 2.0f));
  pm_quad(b, V3(w * 0.5f, -0.15f, -0.04f), V3(-w * 0.5f, -0.15f, -0.04f), V3(-w * 0.5f, -0.35f, -0.04f), V3(w * 0.5f, -0.35f, -0.04f), glow(c1, 2.0f));
}


/* a light curtain: bright along its top edge and sides, fading down to nothing, with a few scan lines */
static t3_texture *make_gate(void) {
  enum { W = 64, H = 128 };
  static uint8_t px[W * H * 4];
  for (int j = 0; j < H; j++)
    for (int i = 0; i < W; i++) {
      float x = ((float)i + 0.5f) / W * 2 - 1, y = (float)j / (float)(H - 1); /* y 0 top .. 1 bottom */
      float v = (1 - y) * (1 - y) * 0.55f + 0.45f * expf(-y * 40.0f);
      v *= 0.55f + 0.45f * sstep(0.5f, 1.0f, fabsf(x)) + 0.0f * x;
      v *= 0.8f + 0.2f * sinf((float)j * 1.4f);
      uint8_t g = byte_(v);
      uint8_t *p = &px[(j * W + i) * 4];
      p[0] = p[1] = p[2] = g, p[3] = 255;
    }
  t3_texture *t = tex_from(W, H, px, false, false);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  return t;
}

/* a checkpoint: two pylons, a lit crossbeam, a light curtain across the road and two torn banners */
static void build_checkpoints(cw_node root, const track *t) {
  for (int i = 1; i < t->nchecks; i++) {
    const tsample *a = &t->s[t->check_at[i]];
    if (a->flags & (TF_TUNNEL | TF_GAP | TF_LOOP | TF_PLATE)) continue;
    float half = a->hw + 2.0f, top = 10.0f;
    pm *b = pose_road(PLAIN, a, 0, 0);
    pm_layer(b, PL_METAL);
    for (int sd = -1; sd <= 1; sd += 2) {
      pm_beam(b, V3(sd * half, -3.5f, 0), V3(sd * half, top, 0), 1.2f, dk(K.hull2, 0.85f));
      pm_beam(b, V3(sd * (half + 0.9f), -3.5f, 0), V3(sd * (half + 0.9f), top - 3, 0), 0.5f, K.hull2);
    }
    pm_beam(b, V3(-half - 1.0f, top, 0), V3(half + 1.0f, top, 0), 1.7f, K.hull2);
    pm_layer(b, PL_GLOW);
    for (int f = -1; f <= 1; f += 2) {
      float z = f * 0.9f;
      pm_quad(b, V3(f > 0 ? -half : half, top - 0.45f, z), V3(f > 0 ? half : -half, top - 0.45f, z), V3(f > 0 ? half : -half, top + 0.2f, z), V3(f > 0 ? -half : half, top + 0.2f, z), glow(K.magenta, 3.4f));
    }
    for (int sd = -1; sd <= 1; sd += 2) { /* lamps up the pylons */
      for (int k = 0; k < 4; k++) {
        pm_push(b);
        pm_move(b, sd * (half - 0.65f), 1.0f + 2.4f * (float)k, 0);
        pm_ball(b, 0.2f, 0.2f, 0.2f, 0, 0, glow(K.cyan, 4.0f), glow(K.white, 5.0f));
        pm_pop(b);
      }
    }
    pm_layer(b, PL_MAIN);
    for (int sd = -1; sd <= 1; sd += 2) {
      pm_push(b);
      pm_move(b, sd * half * 0.55f, top - 0.9f, 0);
      banner(b, 5.5f, 5.0f, sd > 0 ? K.cyan : K.magenta, K.dark, 70u + (uint32_t)(i * 3 + (sd > 0)));
      pm_pop(b);
    }
    pm_pop(b);
    if (nholo < (int)(sizeof HOLO / sizeof HOLO[0])) { /* the light curtain */
      if (!HOLO_TEX[5]) HOLO_TEX[5] = make_gate();
      t3_material *m = t3_mesh_basic_material_new(0xffffff);
      t3_material_set_map(m, HOLO_TEX[5]);
      t3_color base = { K.cyan.r * 1.3f, K.cyan.g * 1.3f, K.cyan.b * 1.3f };
      m->color = base, m->transparent = true, m->blending = T3_ADDITIVE_BLENDING, m->depth_write = false, m->side = T3_DOUBLE_SIDE, m->fog = false;
      t3_geometry *g = t3_plane_geometry_new(half * 2 - 1.0f, top + 3.0f, 1, 1);
      t3_mesh *me = t3_mesh_new(g, m);
      me->base.cast_shadow = false, me->base.receive_shadow = false;
      t3_vec3 p = tp(a, 0, top * 0.5f - 1.7f);
      t3_vec3 d = V3(a->f.x, 0, a->f.z);
      t3_object_set_position(me, p.x, p.y, p.z);
      t3_object_set_rotation(me, 0, atan2f(d.x, d.z), 0);
      hang(root, "Gate", &me->base);
      HOLO[nholo++] = (holo){ m, HOLO_TEX[5], base, hf((uint32_t)i * 977u) * 6.28f, 0.5f };
      t3_release(me), t3_release(g);
    }
  }
}

/* ── the loops: a drum of rings and struts round each, a reactor core in the middle ─────────────────── */

typedef struct reactor {
  t3_vec3 c, axis;
  float R;
} reactor;
static reactor REACT[4];
static int nreact;

static bool loop_geometry(const track *t, int L, reactor *out) {
  if (L >= t->nloops) return false;
  int i0 = t->loop_from[L], i1 = t->loop_to[L];
  if (i1 >= t->n) i1 = t->n - 1;
  const tsample *a = &t->s[i0];
  t3_vec3 axis = vnorm(V3(a->r.x, 0, a->r.z)), fw = vnorm(V3(a->f.x, 0, a->f.z));
  /* the loop is a teardrop that drifts forward and sideways while it climbs: its middle is the middle of its extremes */
  float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
  for (int i = i0; i <= i1; i++) {
    float v[3] = { vdot(t->s[i].p, axis), t->s[i].p.y, vdot(t->s[i].p, fw) };
    for (int k = 0; k < 3; k++) lo[k] = fminf(lo[k], v[k]), hi[k] = fmaxf(hi[k], v[k]);
  }
  out->R = ((hi[1] - lo[1]) + (hi[2] - lo[2])) * 0.25f;
  out->axis = axis;
  float m[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
  /* back from (axis, up, forward) coordinates to the world: the frame is orthonormal */
  out->c = vadd(vadd(vmul(axis, m[0]), V3(0, m[1], 0)), vmul(fw, m[2]));
  return out->R > 5;
}

static void build_reactor(cw_node root, const track *t, int L, pcol c1, pcol c2) {
  reactor r;
  if (!loop_geometry(t, L, &r)) return;
  REACT[nreact++] = r;
  int i0 = t->loop_from[L], i1 = t->loop_to[L];
  /* the outside of the loop: ribs across its belly, lit seams along them, and a hazard clamp every sixth */
  pmc *c = PLAIN;
  for (int i = i0 + 3; i + 3 < i1; i += 5) {
    const tsample *a = &t->s[i];
    pm *b = pose_road(c, a, 0, -1.1f);
    float hw = a->hw + 0.5f;
    bool clamp = ((i - i0) / 5) % 6 == 0;
    pm_layer(b, PL_METAL);
    pm_box_c(b, hw * 2, clamp ? 0.9f : 0.55f, clamp ? 2.2f : 0.9f, clamp ? K.orange : dk(K.hull2, 0.8f));
    pm_layer(b, PL_GLOW);
    float y = clamp ? -0.5f : -0.3f, z = clamp ? 1.15f : 0.5f;
    pm_quad(b, V3(-hw, y, z), V3(hw, y, z), V3(hw, y, -z), V3(-hw, y, -z), glow(((i - i0) / 5) & 1 ? c1 : c2, 2.6f));
    pm_pop(b);
  }
  /* the core: a bright ball, three gimbal rings turning different ways, rods of light from the ball to the rings */
  pm *cb = pm_new(500u + (uint32_t)L);
  pm_layer(cb, PL_GLOW);
  pm_smooth(cb, true);
  float rc = r.R * 0.27f;
  pm_ball(cb, rc, rc, rc, 2, 0, glow(pc_mix(c1, K.white, 0.5f), 6.0f), glow(K.white, 7.0f));
  pm_smooth(cb, false);
  t3_object *core = model_group(root, "Core", cb, -1, false);
  t3_object_set_position(core, r.c.x, r.c.y, r.c.z);
  for (int g = 0; g < 3; g++) {
    pm *gb = pm_new(600u + (uint32_t)(L * 3 + g));
    float rr = r.R * (0.5f + 0.15f * (float)g);
    pm_layer(gb, PL_METAL);
    pm_ring(gb, rr, 0.9f, 56, 6, K.hull2);
    for (int k = 0; k < 4; k++) { /* clamps round the ring */
      float an = CW_TAU * (float)k / 4.0f;
      pm_push(gb);
      pm_move(gb, cosf(an) * rr, sinf(an) * rr, 0);
      pm_box_c(gb, 1.3f, 1.3f, 1.3f, dk(K.hull2, 0.8f));
      pm_pop(gb);
    }
    pm_layer(gb, PL_GLOW);
    pm_ring(gb, rr, 0.4f, 56, 5, glow(g == 1 ? c2 : c1, 4.6f));
    for (int k = 0; k < 4; k++) { /* spokes of light to the ball */
      float an = CW_TAU * (float)k / 4.0f + 0.4f;
      pm_rod(gb, V3(cosf(an) * rc * 0.9f, sinf(an) * rc * 0.9f, 0), V3(cosf(an) * rr, sinf(an) * rr, 0), 0.12f, 4, glow(c1, 3.0f));
    }
    t3_object *go = model_group(root, "Gimbal", gb, -1, false);
    t3_object_set_position(go, r.c.x, r.c.y, r.c.z);
    spin_add(go, vnorm(V3(g == 0 ? 0.2f : g == 1 ? 0.9f : 0.5f, g == 0 ? 0.9f : g == 1 ? 0.1f : 0.5f, g == 2 ? 0.8f : 0.3f)), (g & 1 ? -0.8f : 0.6f) * (1.0f + 0.2f * (float)g), (float)g * 1.7f);
  }
  point_light(root, "ReactorLight", r.c, pc_mix(c1, K.white, 0.25f), 1400.0f, 60.0f);
}

/* ── the corkscrews: ring frames and twisted ribbons round the rolling road ─────────────────────────── */

static void build_cage(cw_node root, const track *t, int i0, int i1, pcol c1, pcol c2) {
  (void)root;
  float Rr = 8.4f;
  const int NS = 6;
  for (int i = i0 + 4; i + 12 <= i1; i += 12) {
    const tsample *a = &t->s[i], *e = &t->s[i + 12];
    /* a ring frame at a, in the road's own frame */
    pm *b = pose(PLAIN, a->p, a->r, a->u, a->f);
    pm_layer(b, PL_METAL);
    pm_ring(b, Rr, 0.42f, 28, 6, K.hull2);
    pm_layer(b, PL_GLOW);
    pm_ring(b, Rr - 0.5f, 0.12f, 28, 4, glow(((i / 12) & 1) ? c1 : c2, 3.4f));
    pm_pop(b);
    /* ribbons: the same angle in the road's frame at both ends, so they twist with the road */
    pm *rb = pmc_at(PLAIN, a->p);
    pm_push(rb);
    for (int k = 0; k < NS; k++) {
      float an = CW_TAU * (float)k / (float)NS;
      t3_vec3 p0 = vadd(a->p, vadd(vmul(a->r, cosf(an) * Rr), vmul(a->u, sinf(an) * Rr)));
      t3_vec3 p1 = vadd(e->p, vadd(vmul(e->r, cosf(an) * Rr), vmul(e->u, sinf(an) * Rr)));
      pm_layer(rb, PL_METAL);
      pm_beam(rb, p0, p1, 0.3f, dk(K.hull2, 0.8f));
      if (k % 3 == 0) {
        pm_layer(rb, PL_GLOW);
        pm_rod(rb, vadd(p0, vmul(vsub(p1, p0), 0.1f)), vadd(p0, vmul(vsub(p1, p0), 0.9f)), 0.1f, 4, glow(k ? c1 : c2, 3.4f));
      }
    }
    pm_pop(rb);
  }
}

/* ── ships: a little fleet of the same hull in different paint ─────────────────────────────────────── */

/* a small ship with its nose along +z: a rounded body, swept wings, a fin, a dark canopy, engines that glow and wingtip lights */
static void ship_model(pm *b, float L, int variant, pcol paint, pcol accent) {
  float w = L * 0.17f;
  pm_vary(b, 0.04f);
  pm_layer(b, PL_MAIN);
  pm_smooth(b, true);
  pm_ball(b, w, w * 0.7f, L * 0.42f, 2, 0.0f, dk(paint, 0.7f), paint);
  pm_push(b);
  pm_move(b, 0, 0, L * 0.34f);
  pm_ball(b, w * 0.6f, w * 0.45f, L * 0.2f, 1, 0.0f, paint, pc_mix(paint, K.white, 0.3f));
  pm_pop(b);
  pm_smooth(b, false);
  float sweep = variant == 1 ? 0.2f : 0.5f;
  for (int sd = -1; sd <= 1; sd += 2) {
    pm_push(b);
    pm_move(b, sd * w * 0.9f, -w * 0.1f, -L * 0.1f);
    pm_roty(b, sd * sweep);
    pm_box_c(b, L * 0.4f, L * 0.02f, L * 0.2f, accent);
    pm_pop(b);
    pm_push(b);
    pm_move(b, sd * (w + L * 0.17f), w * 0.1f, -L * 0.28f);
    pm_box_c(b, L * 0.015f, L * 0.1f, L * 0.1f, accent);
    pm_pop(b);
  }
  pm_push(b);
  pm_move(b, 0, w * 0.6f, -L * 0.34f);
  pm_box_c(b, L * 0.015f, L * 0.2f, L * 0.18f, accent);
  pm_pop(b);
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, 0, w * 0.5f, L * 0.2f);
  pm_ball(b, w * 0.42f, w * 0.3f, L * 0.1f, 1, 0.0f, rgb(0.05f, 0.1f, 0.18f), rgb(0.1f, 0.2f, 0.32f));
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  for (int sd = -1; sd <= 1; sd += 2) {
    pm_push(b);
    pm_move(b, sd * w * 0.5f, 0, -L * 0.43f);
    pm_ball(b, w * 0.34f, w * 0.34f, w * 0.5f, 1, 0.0f, glow(K.cyan, 5.0f), glow(K.white, 6.0f));
    pm_pop(b);
    pm_push(b);
    pm_move(b, sd * (w + L * 0.38f), 0, -L * 0.16f);
    pm_ball(b, 0.35f, 0.35f, 0.35f, 0, 0, glow(sd < 0 ? K.red : rgb(0.1f, 1, 0.3f), 5.0f), glow(K.white, 5.0f));
    pm_pop(b);
  }
}

/* ── the hangar round the first glass tube and the promenade round the second ───────────────────────── */

static int plate_runs(const track *t, int *from, int *to, int max) {
  int n = 0;
  bool in = false;
  for (int i = 0; i < t->n; i++) {
    bool p = (t->s[i].flags & TF_PLATE) != 0;
    if (p && !in && n < max) from[n] = i, in = true;
    if (!p && in) to[n++] = i - 1, in = false;
  }
  if (in && n < max) to[n++] = t->n - 1;
  return n;
}

/* a portal frame over the road at sample i: legs from under the deck up to a faceted arch, with a lit inner edge */
static void portal(const tsample *a, float half, float top, float legdrop, pcol frame, pcol lamp) {
  float pts[7][2] = { { -half, -legdrop }, { -half, top * 0.55f }, { -half * 0.72f, top * 0.9f }, { 0, top }, { half * 0.72f, top * 0.9f }, { half, top * 0.55f }, { half, -legdrop } };
  pm *b = pose_road(PLAIN, a, 0, 0);
  pm_layer(b, PL_METAL);
  for (int k = 0; k < 6; k++) pm_beam(b, V3(pts[k][0], pts[k][1], 0), V3(pts[k + 1][0], pts[k + 1][1], 0), 0.95f, frame);
  pm_layer(b, PL_GLOW);
  for (int k = 1; k < 5; k++) { /* lit inner edge of the arch */
    t3_vec3 p0 = V3(pts[k][0] * 0.94f, pts[k][1] - 0.7f, 0.3f), p1 = V3(pts[k + 1][0] * 0.94f, pts[k + 1][1] - 0.7f, 0.3f);
    pm_rod(b, p0, p1, 0.14f, 4, glow(lamp, 3.4f));
    pm_rod(b, V3(p0.x, p0.y, -0.3f), V3(p1.x, p1.y, -0.3f), 0.14f, 4, glow(lamp, 3.4f));
  }
  pm_pop(b);
}

static void build_hangar(cw_node root, const track *t, int i0, int i1) {
  for (int i = i0 + 3; i <= i1 - 3; i += 14) {
    const tsample *a = &t->s[i];
    portal(a, 15.5f, 17.0f, 7.0f, K.hull2, K.warm);
    if (i + 14 > i1 - 3) continue;
    const tsample *e = &t->s[i + 14];
    /* purlins along the arch's corners */
    pm *b = pmc_at(PLAIN, a->p);
    pm_push(b);
    pm_layer(b, PL_METAL);
    float lats[5] = { -15.5f, -11.2f, 0, 11.2f, 15.5f }, lifts[5] = { 9.4f, 15.3f, 17.0f, 15.3f, 9.4f };
    for (int k = 0; k < 5; k++) pm_beam(b, tp(a, lats[k], lifts[k]), tp(e, lats[k], lifts[k]), 0.5f, dk(K.hull2, 0.8f));
    /* roof plates: dark panels in between, a lot of them missing so stars show */
    pm_layer(b, PL_MAIN);
    for (int k = 0; k < 4; k++) {
      uint32_t h = hsh((uint32_t)(i * 5 + k) * 2654435761u + 3u);
      if (hf(h) < 0.5f) continue;
      t3_vec3 q0 = tp(a, lats[k], lifts[k] + 0.1f), q1 = tp(a, lats[k + 1], lifts[k + 1] + 0.1f), q2 = tp(e, lats[k + 1], lifts[k + 1] + 0.1f), q3 = tp(e, lats[k], lifts[k] + 0.1f);
      pcol c = pc_mix(K.panel, K.dark, 0.5f);
      pm_quad(b, q0, q1, q2, q3, c);
      pm_quad(b, q1, q0, q3, q2, dk(c, 0.6f));
    }
    /* side bulkheads with openings, glowing seams along their tops */
    for (int sd = -1; sd <= 1; sd += 2) {
      uint32_t h = hsh((uint32_t)(i * 3 + (sd > 0)) * 2654435761u + 77u);
      if (hf(h) < 0.45f) continue;
      float lt = sd * 15.9f;
      t3_vec3 w0 = tp(a, lt, -6.0f), w1 = tp(e, lt, -6.0f), w2 = tp(e, lt, 8.0f), w3 = tp(a, lt, 8.0f);
      pcol c = pc_mix(K.panel, K.dark, 0.35f);
      pm_quad(b, w0, w1, w2, w3, c);
      pm_quad(b, w1, w0, w3, w2, dk(c, 0.7f));
    }
    pm_layer(b, PL_GLOW);
    for (int sd = -1; sd <= 1; sd += 2) {
      t3_vec3 g0 = tp(a, sd * 15.3f, 8.9f), g1 = tp(e, sd * 15.3f, 8.9f);
      pm_rod(b, g0, g1, 0.14f, 4, glow(sd > 0 ? K.cyan : K.magenta, 3.2f));
    }
    /* floodlights hung at the crown */
    t3_vec3 f0 = tp(a, 0, 16.2f), f1 = tp(e, 0, 16.2f);
    pm_rod(b, vlerp(f0, f1, 0.3f), vlerp(f0, f1, 0.7f), 0.5f, 6, glow(K.warm, 3.0f));
    pm_pop(b);
  }
  /* the hangar's lights: three warm point lights down the tube */
  for (int k = 0; k < 3; k++) {
    const tsample *a = &t->s[i0 + (i1 - i0) * (k + 1) / 4];
    point_light(root, "HangarLight", tp(a, 0, 12.0f), K.warm, 260.0f, 38.0f);
  }
  /* ships parked in bays behind the walls, nose out */
  for (int sd = -1; sd <= 1; sd += 2)
    for (int k = 0; k < 3; k++) {
      int i = i0 + 10 + k * 28 + (sd > 0 ? 9 : 0);
      if (i >= i1 - 6) continue;
      const tsample *a = &t->s[i];
      t3_vec3 p = tp(a, sd * 27.0f, -4.0f);
      pm *b = pose(PLAIN, p, V3(a->r.x, 0, a->r.z), V3(0, 1, 0), vmul(V3(a->f.x, 0, a->f.z), 1.0f));
      pm_roty(b, (sd > 0 ? 0.9f : -0.9f) + (float)k * 0.3f);
      ship_model(b, 15.0f + 5.0f * (float)k, k & 1, pc_mix(K.hull, (k & 1) ? K.orange : K.cyan, 0.5f), K.hull2);
      pm_pop(b);
      /* a landing pad under it with lit corners */
      pm *pb = pose(PLAIN, vadd(p, V3(0, -2.2f, 0)), V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1));
      pm_layer(pb, PL_MAIN);
      pm_cyl_ex(pb, 14, 14, 0.8f, 20, K.panel, K.panel, true, true);
      pm_layer(pb, PL_GLOW);
      for (int c = 0; c < 6; c++) {
        pm_push(pb);
        pm_move(pb, cosf((float)c * 1.047f) * 13.0f, 0.9f, sinf((float)c * 1.047f) * 13.0f);
        pm_ball(pb, 0.4f, 0.3f, 0.4f, 0, 0, glow(K.yellow, 4.0f), glow(K.white, 4.0f));
        pm_pop(pb);
      }
      pm_pop(pb);
    }
}

/* the second tube: circular frames of light, alternating colours, with a ring of fins */
static void build_promenade(cw_node root, const track *t, int i0, int i1) {
  (void)root;
  for (int i = i0 + 2; i <= i1 - 2; i += 10) {
    const tsample *a = &t->s[i];
    pm *b = pose(PLAIN, tp(a, 0, 4.2f), a->r, a->u, a->f);
    pm_layer(b, PL_METAL);
    pm_ring(b, 15.0f, 0.7f, 40, 6, K.hull2);
    pm_layer(b, PL_GLOW);
    pm_ring(b, 14.2f, 0.2f, 40, 5, glow(((i / 10) & 1) ? K.cyan : K.magenta, 3.6f));
    pm_pop(b);
  }
}

/* ── light arches over the straights, and broken power conduits that spit sparks ───────────────────── */

typedef struct flicker {
  cw_node n;
  float base, phase;
} flicker;
static flicker FLK[12];
static int nflk;

static void build_arches(const track *t) {
  int n = 0;
  for (int i = 45; i + 45 < t->n; i += 55) {
    if (!deck_ok(t, i - 14) || !deck_ok(t, i) || !deck_ok(t, i + 14)) continue;
    const tsample *a = &t->s[i];
    if (a->flags & (TF_PLATE | TF_TUNNEL | TF_BOOST | TF_RAMP)) continue;
    bool near = false;
    for (int c = 0; c < t->nchecks; c++)
      if (abs(t->check_at[c] - i) < 26) near = true;
    for (int g = 0; g < t->ngaps; g++)
      if (i > t->gap_from[g] - 110 && i < t->gap_to[g] + 110) near = true;
    if (near || fabsf(track_curvature(t, (float)i, 20)) > 0.012f) continue;
    float R = a->hw + 3.8f;
    pm *b = pose_road(PLAIN, a, 0, -0.4f);
    pcol col = (n++ & 1) ? K.magenta : K.cyan;
    pm_layer(b, PL_GLOW);
    t3_vec3 prev = V3(-R, 0, 0);
    for (int k = 1; k <= 14; k++) {
      float an = CW_PI * (1.0f - (float)k / 14.0f);
      t3_vec3 cur = V3(cosf(an) * R, sinf(an) * R * 0.95f, 0);
      pm_rod(b, prev, cur, 0.17f, 4, glow(col, 2.8f));
      prev = cur;
    }
    pm_layer(b, PL_METAL);
    for (int sd = -1; sd <= 1; sd += 2) {
      pm_push(b);
      pm_move(b, sd * R, -0.6f, 0);
      pm_box(b, 0.9f, 1.4f, 0.9f, dk(K.hull2, 0.7f), K.hull2);
      pm_pop(b);
    }
    pm_pop(b);
  }
}

/* a cable ripped from a bracket on the catwalk, hanging out over the edge, its torn end showering sparks */
static void build_conduit(cw_node root, const track *t, float s, int side) {
  tsample a = track_at(t, s);
  if (a.flags & (TF_GAP | TF_LOOP)) return;
  t3_vec3 p0 = tp(&a, side * (a.hw + 3.2f), -0.3f), p1 = tp(&a, side * (a.hw + 10.0f), 3.2f);
  pm *b = pmc_at(PLAIN, p0);
  pm_push(b);
  pm_layer(b, PL_METAL);
  pm_box(b, 1.4f, 1.2f, 1.4f, K.dark, dk(K.hull2, 0.7f));
  pm_cable(b, vadd(p0, V3(0, 1.0f, 0)), p1, 3.0f, 0.4f, K.dark);
  pm_rod(b, p1, vadd(p1, vmul(vnorm(vadd(vmul(a.r, (float)side), V3(0, -0.4f, 0))), 1.6f)), 0.5f, 6, dk(K.hull2, 0.8f));
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, p1.x, p1.y, p1.z);
  pm_ball(b, 0.55f, 0.55f, 0.55f, 1, 0, glow(K.cyan, 7.0f), glow(K.white, 8.0f));
  pm_pop(b);
  pm_pop(b);
  cw_node l = point_light(root, "ConduitLight", p1, pc_mix(K.cyan, K.white, 0.4f), 120.0f, 20.0f);
  if (nflk < (int)(sizeof FLK / sizeof FLK[0])) FLK[nflk++] = (flicker){ l, 120.0f, hf((uint32_t)s * 31u) * 6.0f };
  cw_node n = fx_ambient_at(root, FXA_EMBERS, p1, V3(0.3f, 0.3f, 0.3f), 2.5f, 150.0f);
  if (cw_node_valid(n)) {
    t3_vec3 d = vnorm(vadd(vmul(a.r, (float)side), V3(0, -0.5f, 0)));
    cw_node_set(n, "direction", cw_val_vec3(d.x, d.y, d.z));
    cw_node_set(n, "speed_min", cw_val_float(4.0f));
    cw_node_set(n, "speed_max", cw_val_float(11.0f));
    cw_node_set(n, "spread", cw_val_float(45.0f));
    cw_node_set(n, "lifetime", cw_val_float(1.1f));
    cw_node_set(n, "gravity", cw_val_vec3(0, -3.0f, 0));
    cw_node_set(n, "size", cw_val_float(0.03f));
    cw_node_set(n, "amount", cw_val_int(90));
  }
}

/* ── the jump between two station modules ───────────────────────────────────────────────────────────── */

static void build_jump(cw_node root, const track *t, prop_set *ps) {
  (void)ps;
  for (int g = 0; g < t->ngaps; g++) {
    int lip = t->gap_from[g] - 1, land = t->gap_to[g] + 1;
    if (lip < 100 || land + 100 >= t->n) continue;
    const tsample *a = &t->s[lip - 90], *e = &t->s[lip - 10], *c = &t->s[land + 10], *d = &t->s[land + 100];
    t3_vec3 fa = vnorm(V3(a->f.x, 0, a->f.z)), fc = vnorm(V3(c->f.x, 0, c->f.z));
    float r = 12.0f, yc = -r - 0.7f;
    /* module A under the take-off, its far end ripped open; module B under the landing, its near end ripped open */
    hull_module(V3(a->p.x, yc, a->p.z), fa, vlen(vsub(V3(e->p.x, 0, e->p.z), V3(a->p.x, 0, a->p.z))), r, 1, 8800u, 2);
    hull_module(V3(c->p.x, yc, c->p.z), fc, vlen(vsub(V3(d->p.x, 0, d->p.z), V3(c->p.x, 0, c->p.z))), r, 2, 8900u, 1);
    /* trusses holding the ramp and the landing slope up off the hulls */
    for (int i = lip - 74; i < lip - 1; i += 8) {
      const tsample *s = &t->s[i];
      float top = s->p.y - 1.2f, bot = -0.8f;
      if (top - bot < 2.0f) continue;
      for (int sd = -1; sd <= 1; sd += 2) truss_span(PLAIN, V3(tp(s, sd * 5.0f, 0).x, bot, tp(s, sd * 5.0f, 0).z), V3(tp(s, sd * 5.0f, 0).x, top, tp(s, sd * 5.0f, 0).z), 1.7f, 3.0f, K.hull2);
    }
    for (int i = land + 6; i < land + 96; i += 8) {
      const tsample *s = &t->s[i];
      float top = s->p.y - 1.2f, bot = -0.8f;
      if (top - bot < 2.0f) continue;
      for (int sd = -1; sd <= 1; sd += 2) truss_span(PLAIN, V3(tp(s, sd * 5.0f, 0).x, bot, tp(s, sd * 5.0f, 0).z), V3(tp(s, sd * 5.0f, 0).x, top, tp(s, sd * 5.0f, 0).z), 1.7f, 3.0f, K.hull2);
    }
    /* what is left of the link between the hulls: a bent girder, two cables and an arc between the cut ends */
    t3_vec3 m0 = V3(e->p.x, yc, e->p.z), m1 = V3(c->p.x, yc, c->p.z);
    pm *b = pmc_at(PLAIN, vlerp(m0, m1, 0.5f));
    pm_push(b);
    pm_layer(b, PL_METAL);
    pm_cable(b, vadd(m0, V3(0, 2.0f, 0)), vadd(m1, V3(0, 3.0f, 0)), 3.0f, 0.22f, dk(K.hull2, 0.8f));
    pm_cable(b, vadd(m0, V3(2.5f, -1.0f, 0)), vadd(m1, V3(2.0f, -2.0f, 0)), 5.0f, 0.18f, K.dark);
    pm_layer(b, PL_GLOW);
    pm_cable(b, vadd(m0, V3(-2.0f, 0.5f, 1.0f)), vadd(m1, V3(-2.0f, 0.2f, 1.0f)), 1.5f, 0.1f, glow(K.cyan, 4.0f));
    pm_pop(b);
    truss_span(PLAIN, vadd(m0, V3(-3, 4, 0)), vadd(vlerp(m0, m1, 0.45f), V3(-6, 1, 2)), 3.0f, 4.0f, K.hull2);
    point_light(root, "ArcLight", vlerp(m0, m1, 0.5f), K.cyan, 400.0f, 40.0f);
    /* sparks from the cut ends */
    t3_vec3 ext = V3(2.5f, 2.5f, 2.5f);
    for (int k = 0; k < 2; k++) {
      cw_node n = fx_ambient_at(root, FXA_EMBERS, k ? vadd(m1, V3(0, 1.5f, 0)) : vadd(m0, V3(0, 1.5f, 0)), ext, 2.0f, 160.0f);
      if (cw_node_valid(n)) {
        cw_node_set(n, "speed_min", cw_val_float(3.0f));
        cw_node_set(n, "speed_max", cw_val_float(9.0f));
        cw_node_set(n, "spread", cw_val_float(70.0f));
        cw_node_set(n, "lifetime", cw_val_float(1.6f));
        cw_node_set(n, "gravity", cw_val_vec3(0, -1.5f, 0));
        cw_node_set(n, "direction", cw_val_vec3(k ? 0.4f : -0.4f, 0.6f, k ? -1.0f : 1.0f));
      }
    }
  }
}

/* ── the U-turns: a row of tall flood masts and signs on the outside of each curve ──────────────────── */

static void build_uturns(cw_node root, const track *t, prop_set *ps) {
  int i = 0;
  while (i < t->n) {
    /* a long run of one sign of turning */
    float c0 = track_curvature(t, (float)i, 12);
    if (fabsf(c0) < 0.004f) {
      i += 6;
      continue;
    }
    int j = i;
    while (j < t->n && fabsf(track_curvature(t, (float)j, 12)) > 0.004f && track_curvature(t, (float)j, 12) * c0 > 0) j++;
    if (j - i > 120) {
      float side = c0 > 0 ? 1.0f : -1.0f; /* outside of a left turn is the right */
      int k = 0;
      for (int s = i + 10; s < j - 10; s += 26, k++) {
        const tsample *a = &t->s[s];
        float lat = side * (a->hw + 16.0f + 10.0f * hf((uint32_t)s));
        t3_vec3 p = tp(a, lat, -0.5f);
        p.y = fmaxf(p.y, -0.5f);
        float yaw = atan2f(-side * a->r.x, -side * a->r.z);
        ps_add_ex(ps, PK_LIGHT_POLE, 0, 0, p, t3_quat_from_axis_angle(V3(0, 1, 0), yaw), V3(1.5f, 1.5f, 1.5f), (pcol){ 1, 1, 1 }, (uint32_t)s);
        if (k % 3 == 1) holo_sign(root, tp(a, side * (a->hw + 20.0f), -0.5f), yaw, 16.0f, 7.0f, 10.0f, (k / 3) % 5, (k & 1) ? K.cyan : K.magenta, 2.2f, true);
      }
    }
    i = j + 1;
  }
}

/* ── things that stand on the catwalks: crates, barrels, antenna posts with blinking beacons ────────── */

static bool catwalk_on(int i, int side) {
  int blk = (i / 60) * 2 + (side > 0);
  return hf((uint32_t)blk * 40503u + 17u) >= 0.18f;
}

#define MAXBEACON 24
typedef struct beacon {
  t3_material *m;
  t3_color c;
  float period, phase;
} beacon;
static beacon BEAC[MAXBEACON];
static int nbeac;

static void beacon_add(cw_node root, t3_vec3 p, pcol col, float period, float phase) {
  if (nbeac >= MAXBEACON) return;
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_color c = { col.r * 7.0f, col.g * 7.0f, col.b * 7.0f };
  m->color = c, m->fog = false;
  t3_geometry *g = t3_sphere_geometry_new(0.42f, 10, 8);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = false;
  t3_object_set_position(me, p.x, p.y, p.z);
  hang(root, "Beacon", &me->base);
  BEAC[nbeac++] = (beacon){ m, c, period, phase };
  t3_release(me), t3_release(g), t3_release(m);
}

static void build_deck_props(cw_node root, const track *t, prop_set *ps) {
  int nb = 0;
  for (int i = 20; i + 20 < t->n; i += 30) {
    for (int side = -1; side <= 1; side += 2) {
      uint32_t h = hsh((uint32_t)i * 2654435761u + (uint32_t)(side + 1) * 77u);
      if (!deck_ok(t, i) || !deck_ok(t, i + 8) || !deck_ok(t, i - 8)) continue;
      if (t->s[i].flags & (TF_PLATE | TF_TUNNEL)) continue;
      if (!catwalk_on(i, side) || hf(h) < 0.42f) continue;
      const tsample *a = &t->s[i];
      float lat = side * (a->hw + 2.6f);
      t3_vec3 p = tp(a, lat, -0.3f);
      float yaw = atan2f(a->f.x, a->f.z) + (hf(h ^ 1u) - 0.5f) * 0.5f;
      t3_quat q = t3_quat_from_axis_angle(V3(0, 1, 0), yaw);
      float roll = hf(h ^ 2u);
      if (roll < 0.28f) ps_add_ex(ps, PK_CONTAINER, (int)(h & 3) % prop_variants(PK_CONTAINER), 0, p, q, V3(0.5f, 0.5f, 0.5f), (pcol){ 1, 1, 1 }, h);
      else if (roll < 0.5f) ps_add_ex(ps, PK_CRATE, (int)(h & 3) % prop_variants(PK_CRATE), 0, p, q, V3(0.8f, 0.8f, 0.8f), (pcol){ 1, 1, 1 }, h);
      else if (roll < 0.7f) ps_add_ex(ps, PK_BARREL, 2 + (int)(h & 1), 0, p, q, V3(0.9f, 0.9f, 0.9f), (pcol){ 1, 1, 1 }, h);
      else if (roll < 0.86f) {
        ps_add_ex(ps, PK_ANTENNA, (int)(h >> 3) % 3, 0, p, q, V3(0.7f, 0.7f, 0.7f), (pcol){ 1, 1, 1 }, h);
        if ((nb++ & 1) == 0) beacon_add(root, vadd(p, V3(0, 11.3f, 0)), (nb & 2) ? K.red : K.orange, 1.1f + 0.7f * hf(h ^ 9u), hf(h ^ 5u) * 3.0f);
      } else ps_add_ex(ps, PK_DISH, (int)(h >> 5) & 1, 0, p, q, V3(0.55f, 0.55f, 0.55f), (pcol){ 1, 1, 1 }, h);
    }
  }
}

/* ── drifting wreckage: instanced pieces that tumble and creep, fading at the ends of their paths ──── */

#define MAXDEB 3
typedef struct debris {
  t3_instanced_mesh *im[PL_COUNT];
  int n;
  struct {
    t3_vec3 home, vel, axis;
    float rate, phase, cycle, scale;
  } *it;
} debris;
static debris DEB[MAXDEB];
static int ndeb;

static void debris_add(cw_node root, const track *t, prop_kind kind, int variant, float size, int n, uint32_t seed, float lo, float hi) {
  if (ndeb >= MAXDEB) return;
  debris *d = &DEB[ndeb++];
  prop_model m = prop_make(kind, size, seed, variant, 0);
  d->n = n;
  d->it = calloc((size_t)n, sizeof *d->it);
  t3_object *grp = t3_group_new();
  for (int l = 0; l < PL_COUNT; l++) {
    if (!m.g[l]) continue;
    t3_material *mat = prop_material(l, -1, (pcol){ 1, 1, 1 }, 1);
    d->im[l] = t3_instanced_mesh_new(m.g[l], mat, n);
    d->im[l]->mesh.base.cast_shadow = l != PL_GLOW;
    d->im[l]->mesh.base.receive_shadow = true;
    t3_object_add(grp, d->im[l]);
    t3_release(d->im[l]);
  }
  prop_model_free(&m);
  hang(root, "Debris", grp);
  t3_release(grp);
  for (int i = 0; i < n; i++) {
    uint32_t h = hsh(seed + (uint32_t)i * 2654435761u);
    t3_vec3 home = { 0, 0, 0 };
    bool placed = false;
    for (int tries = 0; tries < 300 && !placed; tries++) {
      int s = (int)(hf(h + (uint32_t)tries * 13u) * (float)(t->n - 1));
      const tsample *a = &t->s[s];
      float lat = (hf(h ^ (uint32_t)(tries * 31 + 7)) * 2 - 1) * hi;
      float lift = (hf(h ^ (uint32_t)(tries * 57 + 3)) * 2 - 0.55f) * hi * 0.55f;
      t3_vec3 p = vadd(vadd(a->p, vmul(V3(a->r.x, 0, a->r.z), lat)), V3(0, lift, 0));
      float gap = road_gap(t, p.x, p.z, NULL);
      if (fabsf(lat) < lo || (gap < 12 && fabsf(lift) < 30)) continue;
      home = p;
      placed = true;
    }
    d->it[i].home = home;
    t3_vec3 ax = vnorm(V3(hf(h ^ 11u) - 0.5f, hf(h ^ 13u) - 0.5f, hf(h ^ 17u) - 0.5f));
    d->it[i].axis = ax;
    /* it creeps along the road (never across it), a little outward and up or down */
    t3_vec3 along = V3(0, 0, 1), outward = V3(0, 0, 0);
    {
      int ri = road_nearest(t, home.x, home.z, NULL);
      const tsample *ra = &t->s[ri];
      along = vnorm(V3(ra->f.x, 0, ra->f.z));
      float side = vdot(vsub(home, ra->p), V3(ra->r.x, 0, ra->r.z)) > 0 ? 1.0f : -1.0f;
      outward = vmul(V3(ra->r.x, 0, ra->r.z), side * 0.12f);
    }
    d->it[i].vel = vadd(vadd(vmul(along, (hf(h ^ 19u) - 0.5f) * 1.8f), outward), V3(0, (hf(h ^ 23u) - 0.5f) * 0.3f, 0));
    d->it[i].rate = 0.08f + 0.5f * hf(h ^ 31u);
    d->it[i].phase = hf(h ^ 37u) * 40.0f;
    d->it[i].cycle = 40.0f + 30.0f * hf(h ^ 41u);
    d->it[i].scale = placed ? 0.6f + 1.1f * hf(h ^ 43u) : 0.0f; /* no clear spot found: leave it out, never at the origin */
  }
  t3_sphere sph = { { SKY_C.x, SKY_C.y, SKY_C.z }, 1500.0f };
  for (int l = 0; l < PL_COUNT; l++)
    if (d->im[l]) t3_instanced_mesh_set_bounding_sphere(d->im[l], &sph);
}

static void debris_frame(double time) {
  for (int k = 0; k < ndeb; k++) {
    debris *d = &DEB[k];
    for (int i = 0; i < d->n; i++) {
      float cyc = d->it[i].cycle, u = fmodf((float)time + d->it[i].phase, cyc) / cyc; /* 0..1 along its path */
      float fade = sstep(0.0f, 0.12f, u) * (1 - sstep(0.88f, 1.0f, u));
      t3_vec3 p = vadd(d->it[i].home, vmul(d->it[i].vel, (u - 0.5f) * cyc));
      t3_quat q = t3_quat_from_axis_angle(d->it[i].axis, d->it[i].rate * (float)time + d->it[i].phase);
      float sc = d->it[i].scale * fade + 0.0001f;
      t3_mat4 m;
      t3_mat4_compose(&m, p, q, V3(sc, sc, sc));
      for (int l = 0; l < PL_COUNT; l++)
        if (d->im[l]) t3_instanced_mesh_set_matrix_at(d->im[l], i, &m);
    }
    for (int l = 0; l < PL_COUNT; l++)
      if (d->im[l]) d->im[l]->instance_matrix->version++; /* the upload happens when the version moves */
  }
}

/* ── ships and satellites that pass ─────────────────────────────────────────────────────────────────── */

#define MAXPASS 4
typedef struct passer {
  t3_object *o;
  t3_vec3 a, b;
  float period, phase, scale;
} passer;
static passer PASS[MAXPASS];
static int npass;

static void passer_add(cw_node root, t3_vec3 a, t3_vec3 b, float L, int variant, pcol paint, float period, float phase) {
  if (npass >= MAXPASS) return;
  pm *m = pm_new(7000u + (uint32_t)npass);
  ship_model(m, L, variant, paint, K.hull2);
  t3_object *g = model_group(root, "Ship", m, -1, true);
  t3_vec3 d = vnorm(vsub(b, a));
  t3_object_set_position(g, a.x, a.y, a.z);
  t3_object_look_at(g, a.x + d.x, a.y + d.y, a.z + d.z); /* the nose (+z) along its path */
  PASS[npass++] = (passer){ g, a, b, period, phase, 1.0f };
}

static void passers_frame(double time) {
  for (int i = 0; i < npass; i++) {
    float u = fmodf((float)time / PASS[i].period + PASS[i].phase, 1.0f);
    t3_vec3 p = vlerp(PASS[i].a, PASS[i].b, u);
    float sc = sstep(0.0f, 0.06f, u) * (1 - sstep(0.94f, 1.0f, u)) + 0.001f;
    t3_object_set_position(PASS[i].o, p.x, p.y, p.z);
    t3_object_set_scale(PASS[i].o, sc, sc, sc);
  }
}

/* a satellite: a boxy body, two panels that turn to follow the sun, a dish; it tumbles slowly on a drifting orbit */
static t3_object *SAT, *SAT_PANELS[2];
static void build_satellite(cw_node root) {
  pm *b = pm_new(321);
  pm_layer(b, PL_MAIN);
  pm_box_c(b, 4.0f, 4.0f, 6.0f, K.yellow);
  pm_box_c(b, 4.4f, 1.2f, 6.4f, dk(K.hull2, 0.8f));
  pm_layer(b, PL_METAL);
  pm_cyl_ex(b, 0.15f, 0.15f, 3.0f, 6, K.hull2, K.hull2, true, true);
  pm_push(b);
  pm_move(b, 0, 2.2f, 0);
  pm_rotx(b, -0.8f);
  float prof[] = { 0.001f, 0, 1.0f, 0.4f, 2.0f, 1.2f, 2.8f, 2.4f };
  pm_lathe(b, prof, 4, 14, K.hull, K.hull);
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, 2.2f, 3.1f);
  pm_ball(b, 0.2f, 0.2f, 0.2f, 0, 0, glow(K.red, 4.0f), glow(K.white, 5.0f));
  pm_pop(b);
  SAT = model_group(root, "Satellite", b, -1, true);
  for (int sd = 0; sd < 2; sd++) {
    pm *pb = pm_new(330u + (uint32_t)sd);
    pm_layer(pb, PL_MAIN);
    pm_box_c(pb, 9.0f, 0.12f, 3.6f, K.cell);
    pm_layer(pb, PL_GLOW);
    pm_quad(pb, V3(-4.5f, 0.1f, 1.8f), V3(4.5f, 0.1f, 1.8f), V3(4.5f, 0.1f, 1.65f), V3(-4.5f, 0.1f, 1.65f), glow(K.cyan, 2.0f));
    SAT_PANELS[sd] = model_group(root, "SatPanel", pb, -1, true);
  }
}
static void satellite_frame(double time) {
  if (!SAT) return;
  float a = (float)time * 0.05f;
  t3_vec3 c = V3(-140.0f + cosf(a) * 38.0f, 74.0f + sinf(a * 1.7f) * 10.0f, 215.0f + sinf(a) * 38.0f);
  t3_quat q = t3_quat_multiply(t3_quat_from_axis_angle(V3(0, 1, 0), a * 2.0f + 1.0f), t3_quat_from_axis_angle(V3(1, 0, 0), 0.4f));
  t3_object_set_position(SAT, c.x, c.y, c.z);
  t3_object_set_quaternion(SAT, q);
  for (int sd = 0; sd < 2; sd++) {
    t3_vec3 off = t3_vec3_apply_quat(V3(sd ? 6.8f : -6.8f, 0, 0), q);
    t3_object_set_position(SAT_PANELS[sd], c.x + off.x, c.y + off.y, c.z + off.z);
    t3_object_set_quaternion(SAT_PANELS[sd], t3_quat_multiply(q, t3_quat_from_axis_angle(V3(1, 0, 0), (float)time * 0.12f)));
  }
}

/* a radar dish on a mast that turns round and round */
static void build_radar(cw_node root, t3_vec3 p, float size) {
  pm *b = pm_new(444);
  pm_layer(b, PL_METAL);
  pm_cyl_ex(b, size * 0.12f, size * 0.09f, size * 0.5f, 10, K.hull2, K.hull2, true, true);
  pm_push(b);
  pm_move(b, 0, size * 0.5f, 0);
  pm_rotx(b, -0.45f);
  float R = size * 0.55f;
  float prof[] = { 0.001f, 0, R * 0.35f, R * 0.04f, R * 0.7f, R * 0.14f, R, R * 0.3f };
  pm_lathe(b, prof, 4, 16, K.hull, K.hull);
  pm_beam(b, V3(0, 0, 0), V3(0, R * 0.7f, 0), size * 0.025f, K.hull2);
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0, size * 0.62f, size * 0.3f);
  pm_ball(b, size * 0.04f, size * 0.04f, size * 0.04f, 0, 0, glow(K.red, 4.0f), glow(K.white, 5.0f));
  pm_pop(b);
  t3_object *g = model_group(root, "Radar", b, -1, true);
  t3_object_set_position(g, p.x, p.y, p.z);
  spin_add(g, V3(0, 1, 0), 0.7f, hf((uint32_t)(p.x * 31)) * 6.0f);
}

/* ── the start: a row of pit bays down the right of the grid and a control tower on the left ────────── */

static void build_pits(cw_node root, const track *t) {
  float L = t->length;
  const tsample *g0 = &t->s[t->check_at[0]];
  float hw = g0->hw;
  /* the platform: catwalk widened out to the bays, with a fascia and hanging struts */
  for (int k = 0; k < 5; k++) {
    float s0 = L - 70.0f + 13.0f * (float)k;
    tsample a = track_at(t, s0 + 6.5f);
    pcol sign = k == 0 ? K.cyan : k == 1 ? K.magenta : k == 2 ? K.yellow : k == 3 ? K.orange : K.cyan;
    pm *b = pose_road(PLAIN, &a, hw + 11.5f, -0.35f);
    float W = 11.0f, D = 8.0f, H = 6.5f;
    pm_layer(b, PL_MAIN);
    pm_box_c(b, D + 4, 0.5f, W + 1.5f, pc_mix(K.panel, K.dark, 0.3f)); /* floor slab */
    pm_push(b);
    pm_move(b, 0, 0.25f, 0);
    /* back wall, side walls and a roof that overhangs the front; open towards the road (-x) */
    pm_push(b);
    pm_move(b, D * 0.5f, 0, 0);
    pm_box(b, 0.5f, H, W, dk(K.panel, 0.7f), K.panel);
    pm_pop(b);
    for (int sd = -1; sd <= 1; sd += 2) {
      pm_push(b);
      pm_move(b, 0, 0, sd * (W * 0.5f - 0.25f));
      pm_box(b, D, H, 0.5f, dk(K.panel, 0.7f), K.panel);
      pm_pop(b);
    }
    pm_push(b);
    pm_move(b, -1.2f, H, 0);
    pm_box(b, D + 2.4f, 0.6f, W + 1.0f, dk(K.hull2, 0.7f), K.hull2);
    pm_pop(b);
    pm_layer(b, PL_GLOW);
    /* the back wall glows warm, a bright strip lights the roof's underside, the sign shines above the opening */
    pm_quad(b, V3(D * 0.5f - 0.3f, 0.6f, -W * 0.4f), V3(D * 0.5f - 0.3f, 0.6f, W * 0.4f), V3(D * 0.5f - 0.3f, H - 0.8f, W * 0.4f), V3(D * 0.5f - 0.3f, H - 0.8f, -W * 0.4f), glow(K.warm, 2.0f));
    pm_quad(b, V3(-D * 0.5f - 1.0f, H - 0.05f, -W * 0.45f), V3(D * 0.5f, H - 0.05f, -W * 0.45f), V3(D * 0.5f, H - 0.05f, W * 0.45f), V3(-D * 0.5f - 1.0f, H - 0.05f, W * 0.45f), glow(K.white, 2.4f));
    pm_quad(b, V3(-D * 0.5f - 1.25f, H + 0.2f, -W * 0.4f), V3(-D * 0.5f - 1.25f, H + 0.2f, W * 0.4f), V3(-D * 0.5f - 1.25f, H + 1.4f, W * 0.4f), V3(-D * 0.5f - 1.25f, H + 1.4f, -W * 0.4f), glow(sign, 3.6f));
    pm_pop(b);
    /* a kart on a stand and a tool rack inside */
    pm_layer(b, PL_MAIN);
    pm_push(b);
    pm_move(b, 0.4f, 0.6f, 0);
    pm_box(b, 3.4f, 0.9f, 2.0f, hf((uint32_t)k * 77u) < 0.5f ? K.cyan : K.magenta, K.hull2);
    pm_pop(b);
    pm_layer(b, PL_METAL);
    for (int c = 0; c < 4; c++) {
      pm_push(b);
      pm_move(b, 0.4f + (c & 1 ? 1.2f : -1.2f), 0.2f, c & 2 ? 1.0f : -1.0f);
      pm_cyl(b, 0.42f, 0.42f, 0.8f, 8, K.dark, dk(K.hull2, 0.6f));
      pm_pop(b);
    }
    pm_beam(b, V3(D * 0.5f - 1.0f, 2.0f, -W * 0.4f), V3(D * 0.5f - 1.0f, 2.0f, W * 0.4f), 0.2f, K.hull2);
    pm_pop(b);
    /* struts under the slab */
    pm *u = pose_road(PLAIN, &a, hw + 11.5f, -0.6f);
    pm_layer(u, PL_METAL);
    pm_beam(u, V3(-D * 0.5f, 0, -W * 0.45f), V3(-D * 0.5f - 6, -6.5f, -W * 0.45f), 0.5f, dk(K.hull2, 0.8f));
    pm_beam(u, V3(-D * 0.5f, 0, W * 0.45f), V3(-D * 0.5f - 6, -6.5f, W * 0.45f), 0.5f, dk(K.hull2, 0.8f));
    pm_beam(u, V3(D * 0.5f, 0, 0), V3(D * 0.5f + 3, -7.5f, 0), 0.7f, dk(K.hull2, 0.7f));
    pm_pop(u);
  }
  /* the control tower behind the pit bays: a lit shaft, a glass cab, a mast with a radar dish and a beacon */
  {
    tsample a = track_at(t, L - 30.0f);
    t3_vec3 p = tp(&a, hw + 29.0f, -0.35f);
    pm *b = pose_road(PLAIN, &a, hw + 29.0f, -0.35f);
    pm_layer(b, PL_MAIN);
    pm_box_c(b, 14, 0.5f, 14, pc_mix(K.panel, K.dark, 0.3f));
    pm_push(b);
    pm_move(b, 0, 0.25f, 0);
    pm_box(b, 5.2f, 22.0f, 5.2f, dk(K.panel, 0.8f), K.panel);
    pm_pop(b);
    pm_layer(b, PL_GLOW);
    for (int row = 0; row < 7; row++) {
      float y = 3.0f + 2.8f * (float)row;
      for (int f = 0; f < 4; f++) {
        pm_push(b);
        pm_roty(b, CW_PI * 0.5f * (float)f);
        pm_quad(b, V3(-1.8f, y, 2.63f), V3(1.8f, y, 2.63f), V3(1.8f, y + 1.1f, 2.63f), V3(-1.8f, y + 1.1f, 2.63f), glow(hf((uint32_t)(row * 5 + f)) < 0.2f ? K.cyan : K.warm, 2.6f));
        pm_pop(b);
      }
    }
    pm_layer(b, PL_METAL);
    pm_push(b);
    pm_move(b, 0, 22.2f, 0);
    pm_cyl_ex(b, 5.8f, 5.0f, 1.0f, 8, K.hull2, K.hull2, true, true); /* the cab's floor ring */
    pm_move(b, 0, 1.0f, 0);
    pm_cyl_ex(b, 4.6f, 5.4f, 3.4f, 8, rgb(0.05f, 0.1f, 0.16f), rgb(0.1f, 0.2f, 0.3f), false, true); /* tinted glass */
    pm_pop(b);
    pm_rod(b, V3(0, 27, 0), V3(0, 34, 0), 0.18f, 5, K.hull2);
    pm_layer(b, PL_GLOW);
    pm_push(b);
    pm_move(b, 0, 23.2f, 0);
    pm_cyl_ex(b, 4.65f, 5.45f, 1.2f, 8, glow(K.cyan, 1.4f), glow(K.warm, 1.8f), false, false);
    pm_pop(b);
    pm_pop(b);
    pm_pop(b);
    beacon_add(root, vadd(p, V3(0, 34.4f, 0)), K.red, 1.4f, 0.0f);
    build_radar(root, vadd(p, V3(0, 26.6f, 2.5f)), 7.0f);
  }
}

/* ── docking arms, league banners and far-off pieces of the station ─────────────────────────────────── */

/* a truss arm from the spine toward a leg of the road, ending in a cradle that holds a ship (nose along the road) */
static void docking_arm(t3_vec3 from, t3_vec3 to, int variant, float shipL, pcol paint, uint32_t seed) {
  truss_span(PLAIN, from, to, 4.2f, 6.0f, K.hull2);
  /* a cradle: a ring of metal and two lit claws, then the ship sitting in it */
  pm *b = pose(PLAIN, to, V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1));
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_roty(b, CW_PI * 0.5f);
  pm_ring(b, 5.0f, 0.5f, 24, 6, K.hull2);
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  for (int c = -1; c <= 1; c += 2) pm_ball(b, 0.4f, 0.4f, 0.4f, 0, 0, glow(c < 0 ? K.cyan : K.magenta, 4.0f), glow(K.white, 5.0f));
  pm_pop(b);
  pm *sb = pose(PLAIN, vadd(to, V3(variant & 1 ? 6.0f : -6.0f, 0, 0)), V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, variant & 2 ? -1.0f : 1.0f));
  pm_rotz(sb, (hf(seed) - 0.5f) * 0.3f);
  ship_model(sb, shipL, variant & 1, paint, K.hull2);
  pm_pop(sb);
}

static void build_arms(void) {
  const float X = 75, Y = 22;
  struct { float z; float r; float side; float reach; } A[] = {
    { 30, 22, -1, 36 }, { 150, 18, 1, 52 }, { 245, 17, -1, 38 }, { 590, 20, 1, 50 }, { 700, 16, -1, 80 }, { 480, 18, 1, 54 },
  };
  for (int i = 0; i < 6; i++) {
    float far = A[i].z > 440 ? 1.0f : 0.0f;
    float x0 = X + far * 11 + A[i].side * A[i].r, y0 = Y + far * 5 - 3 + (float)(i % 3) * 3.0f;
    float x1 = x0 + A[i].side * A[i].reach;
    docking_arm(V3(x0, y0, A[i].z), V3(x1, y0 - 2.0f, A[i].z + 2.0f), i, 13.0f + 3.0f * (float)(i % 3), pc_mix(K.hull, i & 1 ? K.orange : K.cyan, 0.45f), 900u + (uint32_t)i);
  }
}

/* big torn banners on masts above the spine, facing the drivers on both legs */
static void build_flags(void) {
  const float X = 75, Y = 22;
  static const struct { float z, r, h; int a, b; } F[] = {
    { 118, 18, 28, 0, 1 }, { 240, 17, 30, 1, 0 }, { 420, 17, 26, 0, 1 }, { 655, 21, 32, 1, 0 }, { 6, 22, 26, 1, 2 },
  };
  for (int i = 0; i < 5; i++) {
    float far = F[i].z > 440 ? 1.0f : 0.0f;
    t3_vec3 base = V3(X + far * 11, Y + far * 5 + F[i].r - 1.0f, F[i].z);
    pm *b = pose(PLAIN, base, V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1));
    pm_layer(b, PL_METAL);
    pm_beam(b, V3(0, 0, 0), V3(0, F[i].h, 0), 0.9f, K.hull2);
    pm_beam(b, V3(-8, F[i].h, 0), V3(8, F[i].h, 0), 0.5f, K.hull2);
    pm_push(b);
    pm_move(b, 0, F[i].h - 0.5f, 0);
    pcol c1 = F[i].a == 0 ? K.magenta : F[i].a == 1 ? K.cyan : K.yellow, c2 = F[i].b == 0 ? K.magenta : F[i].b == 1 ? K.dark : K.orange;
    banner(b, 14.0f, 24.0f, c1, c2, 400u + (uint32_t)i * 13u);
    pm_pop(b);
    pm_pop(b);
  }
}

/* three pieces of the station far off, so every direction has depth: long modules with lit windows, a lone wheel */
static void build_far(void) {
  hull_module(V3(-430, 150, 760), vnorm(V3(0.25f, 0.02f, 1)), 220, 30, 0, 5001u, 2);
  hull_module(V3(-400, 150, 1010), vnorm(V3(0.25f, 0.02f, 1)), 120, 24, 2, 5002u, 1);
  hull_module(V3(640, -40, -520), vnorm(V3(-0.3f, 0.0f, 1)), 260, 34, 1, 5003u, 3);
  hull_module(V3(600, -40, -250), vnorm(V3(-0.3f, 0.0f, 1)), 150, 26, 0, 5004u, 1);
  truss_span(PLAIN, V3(-430, 150, 760), V3(-480, 180, 600), 9.0f, 12.0f, K.hull2);
  solar_wing(V3(-380, 150, 880), V3(0, 1, 0), V3(1, 0, 0), 180.0f, 46.0f, 30.0f, 5, 6100u);
  solar_wing(V3(640, -40, -380), V3(0, -1, 0), V3(1, 0, 0), 160.0f, 40.0f, 28.0f, 4, 6200u);
}

/* ── the scenery callback ────────────────────────────────────────────────────────────────────────────── */

/* a hologram beside the road at distance s, facing the drivers who come at it */
static void holo_at(cw_node root, const track *t, float s, int side, int kind, pcol col, float w, float h, float lift) {
  tsample a = track_at(t, s);
  t3_vec3 p = tp(&a, side * (a.hw + 9.5f), -0.3f);
  float yaw = atan2f(-a.f.x, -a.f.z);
  holo_sign(root, p, yaw, w, h, lift, kind, col, 2.4f, true);
}

static void orbit_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  memset(&SKY, 0, sizeof SKY);
  nspin = nholo = nbeac = ndeb = npass = nreact = nflk = 0;
  SAT = NULL;
  palette_init();
  build_sky(scene);
  cw_node root = cw_node_new("Node3D", "Station");
  cw_node_add_child(scene, root);
  if (!off("deck")) build_deck(root, t);
  HULL = pmc_new(200, 9);
  pmc_texture_metres(HULL, 6.0f);
  PLAIN = pmc_new(170, 12);
  prop_set *farm = ps_new(root, "Farm"), *deck = ps_new(root, "DeckProps"), *masts = ps_new(root, "Masts");
  ps_chunk(farm, 300), ps_chunk(deck, 320), ps_chunk(masts, 320);
  ps_style(deck, PK_CONTAINER, TEX_WALL_TECH, rgb(0.55f, 0.75f, 0.95f), 1.0f);
  ps_style(deck, PK_CRATE, TEX_WALL_TECH, rgb(0.95f, 0.7f, 0.4f), 1.0f);
  if (!off("station")) {
    build_station(root, farm);
    build_arms();
    build_flags();
    build_far();
  }
  if (!off("wheel")) build_wheel(root, V3(75, 22, 337), 64, 8);
  if (!off("reactor")) build_reactor(root, t, 0, K.cyan, K.magenta);
  if (!off("reactor")) build_reactor(root, t, 3, K.magenta, K.orange);
  if (t->nloops > 2 && !off("cage")) {
    build_cage(root, t, t->loop_from[1], t->loop_to[1], K.cyan, K.magenta);
    build_cage(root, t, t->loop_from[2], t->loop_to[2], K.magenta, K.cyan);
  }
  int pf[4], pt[4], np = plate_runs(t, pf, pt, 4);
  if (np > 0 && !off("hangar")) build_hangar(root, t, pf[0], pt[0]);
  if (np > 1 && !off("hangar")) build_hangar(root, t, pf[1], pt[1]);
  if (!off("jump")) build_jump(root, t, farm);
  if (!off("uturn")) build_uturns(root, t, masts);
  if (!off("props")) build_deck_props(root, t, deck);
  build_checkpoints(root, t);
  if (!off("pits")) build_pits(root, t);
  if (!off("arch")) build_arches(t);
  build_conduit(root, t, 320, 1);
  build_conduit(root, t, 1700, -1);
  build_conduit(root, t, 2640, 1);
  /* signs: warnings and chevrons ahead of each set piece */
  struct { float s; int side, kind; int col; } H[] = {
    { 40, 1, 4, 0 }, { 104, -1, 2, 1 }, { 130, 1, 0, 0 }, { 400, 1, 1, 1 }, { 590, -1, 3, 0 }, { 725, 1, 0, 1 }, { 1330, -1, 1, 0 }, { 1380, 1, 2, 1 },
    { 1600, 1, 0, 0 }, { 1815, -1, 3, 1 }, { 1940, 1, 0, 0 }, { 2150, -1, 2, 1 }, { 2360, 1, 1, 0 }, { 2800, 1, 4, 1 }, { 2960, -1, 0, 0 },
  };
  for (int i = 0; i < (int)(sizeof H / sizeof H[0]) && !off("holo"); i++) holo_at(root, t, H[i].s, H[i].side, H[i].kind, H[i].col ? K.magenta : K.cyan, 12.0f, 5.2f, 5.0f);
  /* things that move */
  if (!off("debris")) {
    debris_add(root, t, PK_DEBRIS, 0, 5.0f, 44, 1001u, 22.0f, 200.0f);
    debris_add(root, t, PK_DEBRIS, 3, 4.0f, 44, 2002u, 22.0f, 220.0f);
    if (!off("cdeb")) debris_add(root, t, PK_CONTAINER, 1, 3.0f, 22, 3003u, 30.0f, 240.0f);
  }
  passer_add(root, V3(430, 78, 440), V3(-170, 66, 30), 62.0f, 0, pc_mix(K.hull, K.cyan, 0.4f), 170.0f, 0.15f);
  passer_add(root, V3(-260, 15, 80), V3(-330, 25, 760), 34.0f, 1, pc_mix(K.hull, K.orange, 0.5f), 130.0f, 0.55f);
  build_satellite(root);
  build_radar(root, V3(86, 44, 150), 13.0f);
  build_radar(root, V3(60, 45, 640), 12.0f);
  if (np > 0) {
    const tsample *a = &t->s[(pf[0] + pt[0]) / 2];
    build_radar(root, tp(a, 21.0f, 2.0f), 9.0f);
  }
  ps_finish(farm);
  ps_finish(deck);
  ps_finish(masts);
  pmc_finish(HULL, root, "Hulls", true, TEX_WALL_TECH);
  pmc_finish(PLAIN, root, "Parts", true, -1);
  dress_style ds = dress_defaults();
  if (off("dress")) return;
  ds.main = K.hull2, ds.accent = K.cyan, ds.lamp = K.cyan, ds.has_colours = true, ds.seed = 21;
  dress_start_line(root, t, &ds);
  ds.accent = K.yellow, ds.lamp = K.yellow;
  deco_jump_marks(root, t, &ds);
  ds.accent = K.cyan, ds.lamp = K.magenta;
  deco_banners(root, t, 60, t->length - 40, 210, &ds);
}

static void orbit_frame(float alpha, double time) {
  (void)alpha;
  float tf = (float)time;
  if (SKY.root.id && cw_node_valid(SKY.cam)) {
    t3_vec3 c = cw_node3d_global_position(SKY.cam);
    cw_node3d_set_position(SKY.root, V3(c.x - SKY_C.x, c.y - SKY_C.y, c.z - SKY_C.z));
  }
  /* the world turns slowly under the course, its clouds a little faster */
  if (SKY.planet) t3_object_set_rotation(SKY.planet, 0.3f, tf * 0.004f, 0);
  if (SKY.clouds) t3_object_set_rotation(SKY.clouds, 0.3f, tf * 0.0055f, 0);
  if (SKY.giant) t3_object_set_rotation(SKY.giant, 0, tf * 0.006f, 0);
  for (int i = 0; i < nspin; i++) t3_object_set_quaternion(SPIN[i].o, t3_quat_from_axis_angle(SPIN[i].axis, SPIN[i].phase + SPIN[i].rate * tf));
  debris_frame(time);
  passers_frame(time);
  satellite_frame(time);
  /* holograms breathe, glitch now and then, and their scan lines roll */
  for (int i = 0; i < nholo; i++) {
    float k = 0.82f + 0.18f * sinf(tf * HOLO[i].rate * 3.0f + HOLO[i].phase);
    if (hf((uint32_t)(tf * 7.0f) * 31u + (uint32_t)i * 977u) < 0.05f) k *= 0.3f;
    HOLO[i].m->color = (t3_color){ HOLO[i].base.r * k, HOLO[i].base.g * k, HOLO[i].base.b * k };
  }
  for (int k = 0; k < 5; k++) /* the gate's texture (5) stays put */
    if (HOLO_TEX[k]) HOLO_TEX[k]->offset.y = fmodf(tf * 0.07f, 1.0f);
  for (int i = 0; i < nflk; i++) {
    float k = 0.55f + 0.45f * hf((uint32_t)(tf * 14.0f) * 7919u + (uint32_t)i * 131u);
    cw_node_set(FLK[i].n, "energy", cw_val_float(FLK[i].base * k));
  }
  for (int i = 0; i < nbeac; i++) {
    float u = fmodf(tf / BEAC[i].period + BEAC[i].phase, 1.0f);
    float k = u < 0.14f ? 1.0f : 0.05f;
    BEAC[i].m->color = (t3_color){ BEAC[i].c.r * k, BEAC[i].c.g * k, BEAC[i].c.b * k };
  }
}

const map_def map_orbit = {
  .name = "Wreck Orbit",
  .blurb = "A broken station in orbit: loops and corkscrews round the ring.",
  .scene = "scenes/orbit.cwscene",
  .spec = MAP_SPEC,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.85f, 0.9f, 1.0f }, .kerb_a = { 0.05f, 0.85f, 1.0f }, .kerb_b = { 0.95f, 0.2f, 0.8f }, .wall = { 0.55f, 0.6f, 0.7f },
            .wall_top = { 0.8f, 0.85f, 0.95f }, .road_tex = TEX_ROAD_TECH, .wall_tex = TEX_WALL_TECH, .road_rough = 0.7f,
            .wall_emissive = true, .glow = { 0.1f, 0.85f, 1.0f } },
  .laps = 3,
  .par_lap = 60,
  .scenery = orbit_scenery,
  .scenery_frame = orbit_frame,
};
