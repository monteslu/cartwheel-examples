/* map 4: Cinder Refinery, an oil refinery at dusk with the sky glowing from its fires.
 *
 * The course (SPEC) is the coordinator's. Everything around it is built here from the built track, found by flags
 * (tunnels, jump gaps, boost strips) and never by piece index, so a change to the course moves the set pieces with it.
 *
 * Light: the sky is a deep teal-violet twilight; the fire is the key light. A few real point lights sit at the big
 * flames (they flicker and go dark when no kart is near, so the light count never changes), everything else that
 * glows is an emissive mesh that blooms. Animation is driven from scenery_frame (time) and scenery_tick (which
 * emitters and lights are near a kart). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../fx_int.h"
#include "../props.h"
#include "../ui.h"
#include "cartwheel/base.h"
#include "cartwheel/math.h"

static const piece SPEC[] = {
  WALLS(2, 2),
  SURFACE(SURF_METAL),
  ITEMS(),
  STRAIGHT(120),
  CHECK(),
  BOOST(30),
  STRAIGHT(40),
  SLOPE(40, 6),
  SLOPE(40, -6),
  SLOPE(40, 6),
  SLOPE(40, -6),
  SLOPE(40, 0),
  ITEMS(),
  TUNNEL(1),
  STRAIGHT(60),
  BANKED(-70, 140, -14),
  BANKED(70, 140, 14),
  STRAIGHT(40),
  TUNNEL(0),
  CHECK(),
  SURFACE(SURF_DIRT),
  ABANKED(180, 340, 20),
  ASTRAIGHT(40),
  ITEMS(),
  BOOST(30),
  STRAIGHT(40),
  SLOPE(40, 22),
  SLOPE(24, 22),
  JUMP(0, 31),
  SLOPE(90, 0),
  STRAIGHT(30),
  SURFACE(SURF_METAL),
  BANKED(40, 100, 14),
  BANKED(-80, 160, -16),
  BANKED(40, 100, 14),
  ITEMS(),
  TUNNEL(1),
  STRAIGHT(100),
  TUNNEL(0),
  CHECK(),
  SLOPE(50, -10),
  STRAIGHT(30),
  SLOPE(50, 0),
  STRAIGHT(40),
  BOOST(30),
  STRAIGHT(40),
  SLOPE(36, 20),
  SLOPE(22, 20),
  JUMP(0, 30),
  SLOPE(80, 0),
  ITEMS(),
  ASTRAIGHT(40),
  ABANKED(180, 340, 20),
  ASTRAIGHT(100),
  ASLOPE(50, 0),
  ASTRAIGHT(100),
  STRAIGHT(40),
  END(),
};

#define MAP_SPEC (&(track_spec){ .name = "refinery", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 9, .circuit = true })

/* ── helpers ─────────────────────────────────────────────────────────────────────────────────────── */

#define TAU 6.2831853f
#define MAXFL 72    /* animated flames */
#define MAXLT 40    /* real lights */
#define MAXEM 40    /* particle emitters switched by distance */
#define MAXJET 10   /* steam jets across the road */
#define MAXBCN 24   /* blinking beacons */
#define MAXGAP 4

static uint32_t hh(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u + 0x9e3779b9u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float hf(uint32_t a, uint32_t b) { return (float)(hh(a, b) & 0xffffff) / 16777216.0f; }
static float sst(float a, float b, float x) { return cw_smoothstep(a, b, x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static t3_vec3 V3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static pcol hot(float r, float g, float b) { return (pcol){ r, g, b }; } /* a linear colour, may exceed 1 (blooms) */

/* a window onto the world the callbacks share */
typedef struct flame {
  t3_object *o;      /* the group that is scaled and turned */
  float sx, sy;      /* nominal size */
  float phase, speed;
  float lean;
} flame;
typedef struct lamp {
  t3_light *l;
  float base, phase;
  int kind;          /* 0 fire flicker, 1 steady, 2 flash (lightning), 3 welding */
  float range_on;    /* metres from a kart inside which it is lit */
  float gate;        /* 0..1 now */
  t3_vec3 at;
} lamp;
typedef struct emitter {
  cw_node n;
  t3_vec3 at;
  float range;
  bool on;
  int kind;          /* 0 always while near, 1 steam jet (rhythm), 2 weld sparks (rhythm) */
  float period, duty, phase;
} emitter;
typedef struct beacon {
  t3_material *m;     /* the lamp (a basic material: its colour is the brightness) */
  t3_material *halo;
  float hb[3];        /* the halo's full colour */
  float phase, period, duty;
} beacon;
typedef struct pool {
  float x, z, r;
} pool;

static struct {
  const track *t;
  cw_node root, scene;
  t3_object *dyn;                    /* everything that moves lives under this group */
  flame fl[MAXFL];
  int nfl;
  lamp lt[MAXLT];
  int nlt;
  emitter em[MAXEM];
  int nem;
  t3_material *flame_mat[3], *flame_core_mat, *halo_mat, *halo_blue, *halo_amber;
  t3_texture *flame_tex[3];
  t3_material *lava_mat[2];
  t3_texture *lava_tex[2];
  beacon bc[MAXBCN];
  int nbc;
  t3_vec3 bpos[16];                 /* chimney tops that get a beacon */
  int nbpos;
  pool pool[10];                    /* slag pools */
  int npool;
  pool keep[48];                    /* places the scatter keeps clear (set pieces) */
  int nkeep;
  float flash;                      /* lightning, 0..1 */
  t3_material *tgt[MAXGAP];         /* the landing targets (they pulse) */
  float time;
  /* near-kart gate, refreshed on the fixed tick */
  int nk;
  t3_vec3 kp[MAXK];
  /* set pieces */
  int tun[4][2], ntun;               /* sample ranges of the tunnels */
  int gap[MAXGAP][2], ngap;
  t3_vec3 pit_c[MAXGAP];
  t3_vec3 pit_f[MAXGAP], pit_r[MAXGAP];
  float pit_floor[MAXGAP], pit_hl[MAXGAP], pit_hw[MAXGAP];
  /* lightning */
  t3_object *arc[5];
  t3_vec3 arc_top;
  int arc_light;
  t3_sprite *arc_glow;
  /* welding robots */
  struct {
    t3_object *turret, *arm1, *arm2;
    t3_vec3 base, fwd, jig, along;
    float phase;
    int em;
    t3_sprite *spark;
  } rob[2];
  int nrob;
  /* the heat and dust that follows the road's cameras */
  uint32_t seed;
} R;

static void keep_add(float x, float z, float r) {
  if (R.nkeep < 48) R.keep[R.nkeep++] = (pool){ x, z, r };
}
/* scatter mask: 0 inside a keep-out circle */
static float keep_mask(float x, float z, void *ud) {
  (void)ud;
  for (int i = 0; i < R.nkeep; i++) {
    float dx = x - R.keep[i].x, dz = z - R.keep[i].z;
    if (dx * dx + dz * dz < R.keep[i].r * R.keep[i].r) return 0.0f;
  }
  return 1.0f;
}
static t3_vec3 tp(float s, float lat, float h) { return track_point(R.t, s, lat, h); }

/* ── a tiny mesh builder for the hand-made glow meshes (vertex colours, uvs, up normals) ───────────── */

typedef struct mb {
  float *p, *uv, *c, *n;
  uint32_t *ix;
  int nv, ni, cv, ci;
} mb;

static int mb_v(mb *m, t3_vec3 p, float u, float v, pcol c) {
  if (m->nv == m->cv) {
    m->cv = m->cv ? m->cv * 2 : 256;
    m->p = realloc(m->p, sizeof(float) * 3 * (size_t)m->cv), m->n = realloc(m->n, sizeof(float) * 3 * (size_t)m->cv);
    m->uv = realloc(m->uv, sizeof(float) * 2 * (size_t)m->cv), m->c = realloc(m->c, sizeof(float) * 3 * (size_t)m->cv);
  }
  int i = m->nv++;
  m->p[i * 3] = p.x, m->p[i * 3 + 1] = p.y, m->p[i * 3 + 2] = p.z;
  m->n[i * 3] = 0, m->n[i * 3 + 1] = 1, m->n[i * 3 + 2] = 0;
  m->uv[i * 2] = u, m->uv[i * 2 + 1] = v;
  m->c[i * 3] = c.r, m->c[i * 3 + 1] = c.g, m->c[i * 3 + 2] = c.b;
  return i;
}
static int mb_vn(mb *m, t3_vec3 p, t3_vec3 n, float u, float v, pcol c) {
  int i = mb_v(m, p, u, v, c);
  m->n[i * 3] = n.x, m->n[i * 3 + 1] = n.y, m->n[i * 3 + 2] = n.z;
  return i;
}
static void mb_t(mb *m, int a, int b, int c) {
  if (m->ni + 3 > m->ci) m->ci = m->ci ? m->ci * 2 : 768, m->ix = realloc(m->ix, sizeof(uint32_t) * (size_t)m->ci);
  m->ix[m->ni++] = (uint32_t)a, m->ix[m->ni++] = (uint32_t)b, m->ix[m->ni++] = (uint32_t)c;
}
static void mb_quad(mb *m, t3_vec3 a, t3_vec3 b, t3_vec3 c, t3_vec3 d, pcol ca, pcol cb, pcol cc, pcol cd) {
  int i = mb_v(m, a, 0, 0, ca);
  mb_v(m, b, 1, 0, cb), mb_v(m, c, 1, 1, cc), mb_v(m, d, 0, 1, cd);
  mb_t(m, i, i + 1, i + 2), mb_t(m, i, i + 2, i + 3);
}
static t3_geometry *mb_done(mb *m) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, m->p, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->n, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->uv, m->nv, 2), t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->c, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, m->ix, m->ni, 1), t3_geometry_set_index(g, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(g);
  free(m->p), free(m->n), free(m->uv), free(m->c), free(m->ix);
  memset(m, 0, sizeof *m);
  return g;
}

/* a mesh with a position under a parent (the geometry's reference is handed over; the material's is kept) */
static t3_mesh *put(t3_object *parent, t3_geometry *g, t3_material *m, t3_vec3 p, bool cast, bool recv) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = recv;
  t3_object_set_position(me, p.x, p.y, p.z);
  t3_object_add(parent, me);
  t3_release(me);
  t3_release(g);
  return me;
}
static t3_object *group_at(t3_object *parent, t3_vec3 p) {
  t3_object *g = t3_group_new();
  t3_object_set_position(g, p.x, p.y, p.z);
  t3_object_add(parent, g);
  t3_release(g);
  return g;
}

/* ── textures made here (deterministic) ─────────────────────────────────────────────────────────── */

static float lat_v(int x, int y, int px, int py, uint32_t seed) {
  x = ((x % px) + px) % px, y = ((y % py) + py) % py;
  return hf((uint32_t)x * 7919u + seed, (uint32_t)y * 104729u + seed * 3u);
}
static float vn_t(float x, float y, int px, int py, uint32_t seed) {
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - (float)x0, fy = y - (float)y0;
  fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
  float a = lat_v(x0, y0, px, py, seed), b = lat_v(x0 + 1, y0, px, py, seed), c = lat_v(x0, y0 + 1, px, py, seed), d = lat_v(x0 + 1, y0 + 1, px, py, seed);
  return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
/* fractal noise that tiles: px, py lattice cells over the whole texture at the first octave */
static float fbm_t(float u, float v, int px, int py, int oct, uint32_t seed) {
  float sum = 0, amp = 1, tot = 0;
  for (int i = 0; i < oct; i++) {
    sum += vn_t(u * (float)px, v * (float)py, px, py, seed + (uint32_t)i * 17u) * amp;
    tot += amp, amp *= 0.5f, px *= 2, py *= 2;
  }
  return sum / tot;
}

static t3_texture *tex_make(int w, int h, const uint8_t *px, bool srgb) {
  t3_texture *t = t3_texture_new(w, h, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->flip_y = false;
  t->generate_mipmaps = true;
  t->min_filter = T3_LINEAR_MIPMAP_LINEAR;
  t->mag_filter = T3_LINEAR;
  t->anisotropy = 4;
  t->color_space = srgb ? T3_SRGB_COLOR_SPACE : T3_NO_COLOR_SPACE;
  return t;
}
static uint8_t b8(float x) { return (uint8_t)(cw_clampf(x, 0, 1) * 255.0f + 0.5f); }

/* the moving grain inside a flame: tall streaks that the texture scroll makes run upward */
static t3_texture *tex_flame(uint32_t seed) {
  enum { W = 64, H = 128 };
  static uint8_t px[W * H * 4];
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      float u = (float)x / W, v = (float)y / H;
      float n1 = fbm_t(u, v, 3, 3, 4, seed), n2 = fbm_t(u + 0.31f * n1, v, 6, 2, 3, seed + 9u);
      float g = sst(0.3f, 0.72f, 0.55f * n1 + 0.45f * n2);
      g = 0.08f + 0.92f * g;
      /* the sheet's own edges fade away, so a flame is a soft ragged tongue and not a cut-out polygon */
      float across = 1.0f - fabsf(2.0f * u - 1.0f);
      g *= sst(0.0f, 0.55f, across);
      uint8_t *p = px + (y * W + x) * 4;
      p[0] = p[1] = p[2] = b8(g), p[3] = 255;
    }
  return tex_make(W, H, px, false);
}

/* molten slag: a dark crust cracked by a network of bright orange fissures (cells, edge distance) */
static t3_texture *tex_lava(uint32_t seed) {
  enum { N = 128, CELLS = 7 };
  static uint8_t px[N * N * 4];
  float cx[CELLS * CELLS], cy[CELLS * CELLS];
  for (int j = 0; j < CELLS; j++)
    for (int i = 0; i < CELLS; i++) cx[j * CELLS + i] = ((float)i + 0.15f + 0.7f * hf((uint32_t)i + seed, (uint32_t)j * 5u)) / CELLS, cy[j * CELLS + i] = ((float)j + 0.15f + 0.7f * hf((uint32_t)i * 3u + seed, (uint32_t)j + 77u)) / CELLS;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float u = (float)x / N, v = (float)y / N;
      float d1 = 9, d2 = 9;
      int ci = (int)(u * CELLS), cj = (int)(v * CELLS);
      for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
          int i = ci + di, j = cj + dj;
          float ox = 0, oy = 0;
          if (i < 0) i += CELLS, ox = -1;
          if (i >= CELLS) i -= CELLS, ox = 1;
          if (j < 0) j += CELLS, oy = -1;
          if (j >= CELLS) j -= CELLS, oy = 1;
          float dx = u - (cx[j * CELLS + i] + ox), dy = v - (cy[j * CELLS + i] + oy), d = sqrtf(dx * dx + dy * dy);
          if (d < d1) d2 = d1, d1 = d;
          else if (d < d2) d2 = d;
        }
      float edge = (d2 - d1) * CELLS; /* 0 on a fissure */
      float warp = fbm_t(u, v, 4, 4, 3, seed + 5u);
      float crack = 1.0f - sst(0.0f, 0.34f + 0.2f * warp, edge);
      float glow = crack * crack * (0.55f + 0.7f * warp);
      float r = 0.05f + glow * 1.6f, g = 0.012f + glow * glow * 0.9f, b = 0.006f + glow * glow * glow * 0.35f;
      uint8_t *p = px + (y * N + x) * 4;
      p[0] = b8(r), p[1] = b8(g), p[2] = b8(b), p[3] = 255;
    }
  return tex_make(N, N, px, true);
}

static t3_texture *tex_halo(void) {
  enum { N = 64 };
  static uint8_t px[N * N * 4];
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++) {
      float dx = ((float)x + 0.5f) / N * 2 - 1, dy = ((float)y + 0.5f) / N * 2 - 1, d = sqrtf(dx * dx + dy * dy);
      float a = d >= 1 ? 0 : (1 - d) * (1 - d) * (0.6f + 0.4f * (1 - d));
      uint8_t *p = px + (y * N + x) * 4;
      p[0] = p[1] = p[2] = 255, p[3] = b8(a);
    }
  t3_texture *t = t3_texture_new(N, N, px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  return t;
}

/* ── materials ───────────────────────────────────────────────────────────────────────────────────── */

static t3_material *glow_mat(t3_texture *map, bool two_sided) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->transparent = true;
  m->blending = T3_ADDITIVE_BLENDING;
  m->depth_write = false;
  m->fog = false;
  if (two_sided) m->side = T3_DOUBLE_SIDE;
  if (map) t3_material_set_map(m, map);
  return m;
}
static t3_material *halo_material(float r, float g, float b) {
  t3_material *h = t3_sprite_material_new(0xffffff);
  h->color = (t3_color){ r, g, b };
  t3_texture *t = tex_halo();
  t3_material_set_map(h, t);
  t3_release(t);
  h->blending = T3_ADDITIVE_BLENDING;
  h->depth_write = false;
  h->fog = false;
  return h;
}

static void materials(void) {
  for (int i = 0; i < 3; i++) {
    R.flame_tex[i] = tex_flame(11u + (uint32_t)i * 31u);
    R.flame_mat[i] = glow_mat(R.flame_tex[i], true);
  }
  R.flame_core_mat = glow_mat(R.flame_tex[1], true);
  R.halo_mat = halo_material(0.55f, 0.2f, 0.05f);
  R.halo_blue = halo_material(0.55f, 0.7f, 1.0f);
  R.halo_amber = halo_material(1.0f, 0.62f, 0.1f);
  for (int i = 0; i < 2; i++) {
    R.lava_tex[i] = tex_lava(5u + (uint32_t)i * 13u);
    t3_material *m = t3_mesh_physical_material_new(0xffffff);
    m->color = (t3_color){ 0.02f, 0.012f, 0.01f };
    m->roughness = 0.55f;
    m->metalness = 0;
    m->emissive = (t3_color){ 1, 1, 1 };
    m->emissive_intensity = 2.0f;
    t3_material_set_texture(m, T3_EMISSIVE_MAP, R.lava_tex[i]);
    R.lava_mat[i] = m;
  }
}

/* ── flames ──────────────────────────────────────────────────────────────────────────────────────── */

/* A flame is a few crossed tongues: a tapered sheet whose width falls to a point, brightest and whitest at the foot,
 * orange in the middle, dark red at the tip (additive, so dark means gone). The scrolling streak texture moves
 * up the sheet and the group is stretched, turned and leaned every frame. */
static t3_geometry *flame_geo(float w, float h, int sheets, float lean, uint32_t seed, float white) {
  mb m = { 0 };
  const int rows = 12;
  for (int s = 0; s < sheets; s++) {
    float a = TAU * 0.5f * (float)s / (float)sheets + hf(seed, (uint32_t)s) * 0.4f;
    t3_vec3 d = V3(cosf(a), 0, sinf(a));
    float sw = w * (0.8f + 0.4f * hf(seed + 3u, (uint32_t)s)), sh = h * (0.82f + 0.3f * hf(seed + 5u, (uint32_t)s));
    float bend = lean * (hf(seed + 7u, (uint32_t)s) - 0.3f);
    int base = m.nv;
    for (int j = 0; j <= rows; j++) {
      float t = (float)j / rows;
      float hw = sw * 0.5f * powf(1.0f - t, 0.72f) * (1.0f + 0.22f * sinf(t * 9.0f + (float)s * 2.0f)) * sst(0.0f, 0.1f, t + 0.06f);
      float cx = bend * sh * t * t + 0.12f * sw * sinf(t * 6.0f + (float)s * 1.7f + (float)seed);
      float y = sh * t;
      /* the colour runs from near-white at the foot through orange to a dim red at the tip */
      pcol c;
      float k = 1.0f - t;
      if (t < 0.3f) c = hot(lerpf(1.5f, 1.3f, t / 0.3f), lerpf(0.4f + white, 0.26f, t / 0.3f), lerpf(0.04f + white * 0.4f, 0.015f, t / 0.3f));
      else if (t < 0.7f) c = hot(lerpf(1.3f, 0.9f, (t - 0.3f) / 0.4f), lerpf(0.26f, 0.04f, (t - 0.3f) / 0.4f), 0.0f);
      else c = hot(lerpf(0.9f, 0.0f, (t - 0.7f) / 0.3f), lerpf(0.04f, 0.0f, (t - 0.7f) / 0.3f), 0.0f);
      (void)k;
      pcol e = hot(c.r * 0.5f, c.g * 0.45f, c.b * 0.45f);
      float vv = t * 1.7f;
      mb_v(&m, V3(cx * d.x - hw * d.z, y, cx * d.z + hw * d.x), 0.0f, vv, e);
      mb_v(&m, V3(cx * d.x, y, cx * d.z), 0.5f, vv, c);
      mb_v(&m, V3(cx * d.x + hw * d.z, y, cx * d.z - hw * d.x), 1.0f, vv, e);
    }
    for (int j = 0; j < rows; j++) {
      int i0 = base + j * 3, i1 = i0 + 3;
      mb_t(&m, i0, i0 + 1, i1 + 1), mb_t(&m, i0, i1 + 1, i1);
      mb_t(&m, i0 + 1, i0 + 2, i1 + 2), mb_t(&m, i0 + 1, i1 + 2, i1 + 1);
    }
  }
  return mb_done(&m);
}

static t3_sprite *halo_at(t3_object *parent, t3_material *m, t3_vec3 p, float size) {
  t3_sprite *s = t3_sprite_new(m);
  t3_object_set_position(s, p.x, p.y, p.z);
  t3_object_set_scale(s, size, size, size);
  t3_object_add(parent, s);
  t3_release(s);
  return s;
}

/* a burning flame of a given size standing at p; returns its index for animation */
static int flame_new(t3_vec3 p, float w, float h, uint32_t seed, float tilt_x, float tilt_z, bool halo, float halo_k) {
  if (R.nfl >= MAXFL) return -1;
  t3_object *g = group_at(R.dyn, p);
  flame *f = &R.fl[R.nfl];
  f->o = g, f->sx = w, f->sy = h, f->phase = hf(seed, 1) * TAU, f->speed = 0.8f + hf(seed, 2) * 0.6f, f->lean = 0;
  put(g, flame_geo(w, h, 4, 0.18f, seed, 0.0f), R.flame_mat[seed % 3], V3(0, 0, 0), false, false);
  put(g, flame_geo(w * 0.5f, h * 0.68f, 3, 0.1f, seed + 40u, 0.55f), R.flame_core_mat, V3(0, 0, 0), false, false);
  if (halo) halo_at(g, R.halo_mat, V3(0, h * 0.42f, 0), h * 1.5f * halo_k);
  (void)tilt_x, (void)tilt_z;
  return R.nfl++;
}

static int lamp_new(t3_vec3 p, uint32_t color, float base, float range, int kind, float on_range) {
  if (R.nlt >= MAXLT) return -1;
  t3_light *l = t3_point_light_new(color, 0, range, 2);
  t3_object_set_position(l, p.x, p.y, p.z);
  t3_object_add(R.dyn, l);
  t3_release(l);
  lamp *a = &R.lt[R.nlt];
  a->l = l, a->base = base, a->kind = kind, a->range_on = on_range, a->at = p, a->phase = hf((uint32_t)R.nlt, 9) * TAU;
  return R.nlt++;
}

/* ── particle emitters ───────────────────────────────────────────────────────────────────────────── */

static const char *const REC_SMOKE[] = { "amount", "70", "lifetime", "9", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(1.5, 0.2, 1.5)", "direction", "vec3(0, 1, 0)", "spread", "10", "speed_min", "3.5", "speed_max", "6",
  "gravity", "vec3(2.2, 1.2, 0.6)", "damping", "0.2", "size", "7", "size_random", "0.4", "size_curve", "\"0 0.35, 0.35 1, 1 2.8\"",
  "color_ramp", "\"0 1 0.42 0.12 0, 0.06 0.95 0.38 0.12 0.6, 0.25 0.3 0.18 0.14 0.55, 0.7 0.1 0.09 0.11 0.4, 1 0.06 0.06 0.08 0\"",
  "blend", "ALPHA", "soft_distance", "2", "turbulence", "1.3", "turbulence_scale", "0.06", "turbulence_speed", "0.3", "local_coords", "false", NULL };
static const char *const REC_SPARKS[] = { "amount", "110", "lifetime", "1.2", "lifetime_random", "0.4", "emission_shape", "BOX",
  "emission_extents", "vec3(0.1, 0.1, 0.1)", "direction", "vec3(0, 1, 0)", "spread", "60", "speed_min", "3", "speed_max", "10",
  "gravity", "vec3(0, -14, 0)", "size", "0.13", "size_random", "0.5", "size_curve", "\"0 1, 0.7 0.8, 1 0.2\"",
  "color_ramp", "\"0 1 0.95 0.7 1, 0.3 1 0.6 0.18 1, 1 0.6 0.1 0.02 0\"", "energy", "8", "blend", "ADD", "local_coords", "false", NULL };
static const char *const REC_SHOWER[] = { "amount", "90", "lifetime", "1.5", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(8, 0.1, 0.4)", "direction", "vec3(0, -1, 0)", "spread", "14", "speed_min", "2", "speed_max", "5",
  "gravity", "vec3(0, -9, 0)", "size", "0.08", "size_random", "0.5", "size_curve", "\"0 1, 0.8 0.8, 1 0.2\"",
  "color_ramp", "\"0 1 0.9 0.6 1, 0.3 1 0.55 0.15 1, 1 0.5 0.08 0.02 0\"", "energy", "7", "blend", "ADD", "local_coords", "false", NULL };
static const char *const REC_JET[] = { "amount", "46", "lifetime", "1.5", "lifetime_random", "0.25", "emission_shape", "BOX",
  "emission_extents", "vec3(0.18, 0.1, 0.18)", "direction", "vec3(0, 1, 0)", "spread", "9", "speed_min", "9", "speed_max", "13",
  "gravity", "vec3(0, 0.7, 0)", "damping", "0.7", "size", "0.55", "size_random", "0.35", "size_curve", "\"0 0.4, 0.4 1.3, 1 3.2\"",
  "color_ramp", "\"0 1 0.95 0.9 0, 0.08 0.95 0.9 0.85 0.55, 1 0.8 0.78 0.8 0\"", "blend", "ALPHA", "soft_distance", "0.5",
  "turbulence", "1.2", "turbulence_scale", "0.4", "local_coords", "false", NULL };
static const char *const REC_VENT[] = { "amount", "30", "lifetime", "2.6", "lifetime_random", "0.3", "emission_shape", "BOX",
  "emission_extents", "vec3(0.7, 0.1, 0.3)", "direction", "vec3(0, 1, 0)", "spread", "12", "speed_min", "2", "speed_max", "3.4",
  "gravity", "vec3(0, 0.4, 0)", "damping", "0.4", "size", "0.8", "size_random", "0.4", "size_curve", "\"0 0.4, 0.4 1.2, 1 2.4\"",
  "color_ramp", "\"0 1 0.8 0.6 0, 0.1 1 0.7 0.5 0.4, 1 0.8 0.75 0.8 0\"", "blend", "ALPHA", "soft_distance", "0.5",
  "turbulence", "0.9", "turbulence_scale", "0.4", "local_coords", "false", NULL };

static int emit_new(const char *const *rec, const char *name, t3_vec3 at, int kind, float range, float period, float duty, float phase) {
  if (R.nem >= MAXEM) return -1;
  emitter *e = &R.em[R.nem];
  e->n = fx_pnode(R.root, name, rec, 7000 + R.nem * 13);
  cw_node3d_set_position(e->n, at);
  cw_node_set(e->n, "visible", cw_val_bool(false));
  e->at = at, e->kind = kind, e->range = range, e->period = period, e->duty = duty, e->phase = phase, e->on = false;
  return R.nem++;
}
/* turn an emitter so that its local up (the direction the recipes fire along) points along d */
static void emit_aim(int i, t3_vec3 d) {
  t3_vec3 up = { 0, 1, 0 };
  t3_vec3 ax = v3cross(up, d);
  float s = v3len(ax), c = v3dot(up, d);
  if (s < 1e-4f) {
    if (c < 0) cw_node3d_set_quaternion(R.em[i].n, t3_quat_from_axis_angle(V3(1, 0, 0), 3.14159265f));
    return;
  }
  cw_node3d_set_quaternion(R.em[i].n, t3_quat_from_axis_angle(v3mul(ax, 1.0f / s), atan2f(s, c)));
}

/* ── terrain ─────────────────────────────────────────────────────────────────────────────────────── */

/* The ground is flat with slow swells, falls into a cutting where the course dips below it, and drops into a pit
 * under every jump (a rectangle in the road's frame, a little longer and wider than the gap). */
static float terrain(float x, float z, void *ud) {
  (void)ud;
  const track *t = R.t;
  float ry;
  int idx;
  float gap = road_gap_idx(t, x, z, &ry, &idx);
  float h = -0.12f + (ground_noise(x, z, 120, 1.0f, 3, 41u) + 1.0f) * 0.9f * sst(14, 130, gap);
  const tsample *s = &t->s[idx];
  if (gap < 60.0f && !(s->flags & TF_GAP)) {
    /* the ground meets the road at the height of the road's own surface there (a banked road is lower at its inner
     * edge than at its middle), rising or falling to it through a bank whose width grows with the height to cover */
    float hr = sqrtf(s->r.x * s->r.x + s->r.z * s->r.z);
    float l = hr > 1e-3f ? ((x - s->p.x) * s->r.x + (z - s->p.z) * s->r.z) / hr : 0.0f;
    float ye = s->p.y + cw_clampf(l, -s->hw * hr, s->hw * hr) * (hr > 1e-3f ? s->r.y / hr : 0.0f);
    float dy = ye - h;
    float follow = dy > 0 ? 1.0f - sst(3.5f, 6.5f, dy) : 1.0f - sst(16.0f, 19.0f, -dy);
    if (follow > 0.0f) {
      float width = cw_clampf(fabsf(dy) * 2.5f, 7.0f, 42.0f);
      h = lerpf(h, lerpf(ye - 0.3f, h, sst(0.5f, width, gap)), follow);
    }
  }
  for (int i = 0; i < R.npool; i++) {
    float dx = x - R.pool[i].x, dz = z - R.pool[i].z, d = sqrtf(dx * dx + dz * dz);
    h -= 0.9f * (1.0f - sst(R.pool[i].r * 0.75f, R.pool[i].r * 1.1f, d));
  }
  for (int g = 0; g < R.ngap; g++) {
    float dx = x - R.pit_c[g].x, dz = z - R.pit_c[g].z;
    float u = dx * R.pit_f[g].x + dz * R.pit_f[g].z, v = dx * R.pit_r[g].x + dz * R.pit_r[g].z;
    float qx = fabsf(u) - R.pit_hl[g], qz = fabsf(v) - R.pit_hw[g];
    float sd = sqrtf(fmaxf(qx, 0) * fmaxf(qx, 0) + fmaxf(qz, 0) * fmaxf(qz, 0)) + fminf(fmaxf(qx, qz), 0);
    h = lerpf(R.pit_floor[g] - 2.0f, h, sst(-1, 5, sd));
  }
  return h;
}

/* ── the callbacks ───────────────────────────────────────────────────────────────────────────────── */

static void scenery(cw_node scene, const track *t, uint32_t seed);
static void scenery_tick(const sim *s, int ticks);
static void scenery_frame(float alpha, double time);

/* ── the map ─────────────────────────────────────────────────────────────────────────────────────── */

const map_def map_refinery = {
  .name = "Cinder Refinery",
  .blurb = "Pipes, flare stacks and burning pits: tunnels through tanks, fire jumps.",
  .scene = "scenes/refinery.cwscene",
  .spec = MAP_SPEC,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.75f, 0.76f, 0.8f }, .kerb_a = { 0.95f, 0.72f, 0.08f }, .kerb_b = { 0.1f, 0.1f, 0.11f }, .wall = { 0.75f, 0.7f, 0.6f },
            .wall_top = { 0.9f, 0.85f, 0.75f }, .road_tex = TEX_ROAD_METAL, .wall_tex = TEX_WALL_HAZARD, .road_rough = 0.75f },
  .laps = 3,
  .par_lap = 60,
  .scenery = scenery,
  .scenery_tick = scenery_tick,
  .scenery_frame = scenery_frame,
};

/* ── models ──────────────────────────────────────────────────────────────────────────────────────── */

/* Steel in cool greys against the warm fire: the colours are screen (sRGB) values turned linear by rgb(). */
#define STEEL rgb(0.5f, 0.52f, 0.57f)
#define STEEL_D rgb(0.24f, 0.25f, 0.29f)
#define SOOT rgb(0.1f, 0.1f, 0.12f)
#define RUST rgb(0.5f, 0.27f, 0.15f)
#define HAZARD rgb(0.95f, 0.72f, 0.08f)
#define PAINT_W rgb(0.8f, 0.8f, 0.78f)
#define PAINT_R rgb(0.72f, 0.16f, 0.12f)

/* a cracking (distillation) tower: a tall ribbed column with platforms, a ladder cage, pipes climbing its side
 * and a few lit furnace slits low down; the red lamp at the top is a glow so it blooms */
static prop_model m_crack(float H, uint32_t seed, int lod) {
  pm *b = pm_new(seed);
  float Rr = fmaxf(H * 0.05f, 1.6f);
  int seg = lod ? 8 : 14;
  pm_layer(b, PL_MAIN);
  pm_cyl(b, Rr * 1.25f, Rr * 1.05f, H * 0.28f, seg, SOOT, STEEL_D);
  pm_push(b), pm_move(b, 0, H * 0.28f, 0), pm_cyl(b, Rr * 1.05f, Rr * 0.85f, H * 0.6f, seg, STEEL_D, STEEL), pm_pop(b);
  pm_push(b), pm_move(b, 0, H * 0.88f, 0), pm_cyl(b, Rr * 0.85f, 0, H * 0.11f, seg, STEEL, STEEL_D), pm_pop(b);
  for (int k = 1; k <= 6; k++) {
    float y = H * (0.13f * (float)k);
    pm_push(b), pm_move(b, 0, y, 0), pm_cyl(b, Rr * 1.3f, Rr * 1.3f, 0.5f, seg, RUST, STEEL_D), pm_pop(b);
  }
  if (!lod) {
    for (int k = 0; k < 3; k++) {
      float y = H * (0.34f + 0.2f * (float)k);
      pm_push(b), pm_move(b, 0, y, 0), pm_cyl(b, Rr * 2.1f, Rr * 2.1f, 0.3f, 16, STEEL_D, STEEL), pm_pop(b);
      for (int p = 0; p < 12; p++) { /* rail posts */
        float a = TAU * (float)p / 12.0f;
        pm_beam(b, V3(cosf(a) * Rr * 2.05f, y, sinf(a) * Rr * 2.05f), V3(cosf(a) * Rr * 2.05f, y + 1.1f, sinf(a) * Rr * 2.05f), 0.1f, HAZARD);
      }
    }
    pm_push(b), pm_move(b, Rr * 1.1f, 0, 0), pm_box(b, 0.5f, H * 0.82f, 0.12f, STEEL_D, STEEL_D), pm_pop(b);
    pm_layer(b, PL_METAL);
    for (int p = 0; p < 3; p++) {
      float a = TAU * (0.1f + 0.31f * (float)p) + hf(seed, (uint32_t)p) * 0.5f;
      float x = cosf(a) * Rr * 1.5f, z = sinf(a) * Rr * 1.5f, top = H * (0.5f + 0.13f * (float)p);
      pm_rod(b, V3(x, 0.4f, z), V3(x, top, z), Rr * 0.12f, 8, STEEL);
      pm_rod(b, V3(x, top, z), V3(x * 0.62f, top + H * 0.05f, z * 0.62f), Rr * 0.12f, 8, STEEL);
      for (int q = 0; q < 4; q++) pm_rod(b, V3(x, top * (float)(q + 1) / 5.0f - 0.2f, z), V3(x, top * (float)(q + 1) / 5.0f + 0.2f, z), Rr * 0.19f, 8, STEEL_D);
    }
  }
  pm_layer(b, PL_GLOW);
  for (int k = 0; k < 3; k++) {
    float a = TAU * (float)k / 3.0f + 0.5f;
    pm_push(b), pm_move(b, cosf(a) * (Rr * 1.22f), 2.6f, sinf(a) * (Rr * 1.22f)), pm_roty(b, -a), pm_box_c(b, 0.2f, 2.6f, 0.9f, glow(rgb(1, 0.45f, 0.1f), 2.6f)), pm_pop(b);
  }
  pm_push(b), pm_move(b, 0, H * 0.99f + 0.3f, 0), pm_box_c(b, 0.7f, 0.7f, 0.7f, glow(rgb(1, 0.1f, 0.06f), 5)), pm_pop(b);
  return pm_finish(b);
}

/* a spherical storage tank on legs with a stair and a ring brace */
static prop_model m_sphere(float Rr, uint32_t seed, int lod) {
  pm *b = pm_new(seed);
  float leg = Rr * 0.6f, cy = leg + Rr * 0.9f;
  pm_layer(b, PL_MAIN);
  pm_smooth(b, true);
  pm_push(b), pm_move(b, 0, cy, 0), pm_ball(b, Rr, Rr, Rr, lod ? 1 : 2, 0, rgb(0.55f, 0.56f, 0.6f), rgb(0.86f, 0.86f, 0.88f)), pm_pop(b);
  pm_smooth(b, false);
  pm_push(b), pm_move(b, 0, cy + Rr * 0.95f, 0), pm_cyl(b, Rr * 0.14f, Rr * 0.11f, Rr * 0.12f, 8, STEEL_D, STEEL), pm_pop(b);
  pm_layer(b, PL_METAL);
  int nl = lod ? 4 : 8;
  for (int i = 0; i < nl; i++) {
    float a = TAU * (float)i / (float)nl, a2 = TAU * (float)(i + 1) / (float)nl;
    t3_vec3 lo = V3(cosf(a) * Rr * 0.7f, 0, sinf(a) * Rr * 0.7f), hi = V3(cosf(a) * Rr * 0.97f, cy - Rr * 0.12f, sinf(a) * Rr * 0.97f);
    pm_rod(b, lo, hi, Rr * 0.03f + 0.08f, 6, STEEL_D);
    if (!lod) pm_rod(b, V3(lo.x, leg * 0.55f, lo.z), V3(cosf(a2) * Rr * 0.7f, leg * 0.55f, sinf(a2) * Rr * 0.7f), 0.07f, 5, STEEL_D);
  }
  if (!lod) {
    pm_beam(b, V3(Rr * 1.1f, 0, 0), V3(Rr * 0.28f, cy + Rr * 0.92f, 0), 0.5f, HAZARD);
  }
  return pm_finish(b);
}

/* a squat cylindrical tank with a domed roof, a rim catwalk, a spiral stair and a pipe down the side */
static prop_model m_cyltank(float Rr, float H, uint32_t seed, int lod) {
  pm *b = pm_new(seed);
  int seg = lod ? 10 : 20;
  pcol skin = hf(seed, 3) < 0.5f ? rgb(0.62f, 0.62f, 0.64f) : rgb(0.6f, 0.45f, 0.35f);
  pm_layer(b, PL_MAIN);
  pm_cyl(b, Rr, Rr, H, seg, pc_mul(skin, 0.5f), skin);
  pm_push(b), pm_move(b, 0, H, 0), pm_cyl(b, Rr, Rr * 0.2f, Rr * 0.28f, seg, skin, pc_mul(skin, 0.7f)), pm_pop(b);
  for (int k = 1; k < 3; k++) pm_push(b), pm_move(b, 0, H * (float)k / 3.0f, 0), pm_cyl(b, Rr * 1.02f, Rr * 1.02f, 0.35f, seg, STEEL_D, STEEL_D), pm_pop(b);
  pm_push(b), pm_move(b, 0, 0, 0), pm_cyl(b, Rr * 1.12f, Rr * 1.12f, 0.8f, seg, SOOT, STEEL_D), pm_pop(b);
  if (!lod) {
    int steps = 14;
    for (int i = 0; i < steps; i++) { /* spiral stair: treads climbing round the shell */
      float a = TAU * 0.8f * (float)i / (float)steps, y = H * (float)(i + 1) / (float)steps;
      pm_push(b), pm_move(b, cosf(a) * (Rr + 0.5f), y, sinf(a) * (Rr + 0.5f)), pm_roty(b, -a), pm_box_c(b, 1.0f, 0.12f, 1.6f, HAZARD), pm_pop(b);
    }
    pm_layer(b, PL_METAL);
    pm_rod(b, V3(-Rr * 1.08f, 0.4f, 0), V3(-Rr * 1.08f, H * 0.9f, 0), 0.28f, 8, STEEL);
    pm_rod(b, V3(-Rr * 1.08f, H * 0.9f, 0), V3(-Rr * 0.5f, H + Rr * 0.2f, 0), 0.28f, 8, STEEL);
  }
  return pm_finish(b);
}

/* a chimney: tapered, banded red and white, with two work platforms (the beacon is separate so it can blink) */
static prop_model m_stack(float H, uint32_t seed, int lod) {
  pm *b = pm_new(seed);
  float r0 = H * 0.045f, r1 = H * 0.028f;
  int bands = 7, seg = lod ? 8 : 14;
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < bands; i++) {
    float t0 = (float)i / (float)bands, t1 = (float)(i + 1) / (float)bands;
    pcol c = (i & 1) ? PAINT_R : PAINT_W;
    if (i == 0) c = STEEL_D;
    pm_push(b), pm_move(b, 0, H * t0, 0), pm_cyl(b, lerpf(r0, r1, t0), lerpf(r0, r1, t1), H / (float)bands, seg, pc_mul(c, 0.6f), c), pm_pop(b);
  }
  pm_push(b), pm_move(b, 0, H, 0), pm_cyl(b, r1 * 1.2f, r1 * 1.05f, 1.2f, seg, SOOT, STEEL_D), pm_pop(b);
  if (!lod)
    for (int k = 0; k < 2; k++) pm_push(b), pm_move(b, 0, H * (0.55f + 0.3f * (float)k), 0), pm_cyl(b, r1 * 2.0f, r1 * 2.0f, 0.25f, 14, STEEL_D, STEEL), pm_pop(b);
  return pm_finish(b);
}

/* a lattice flare mast: four tapering legs, rings and cross braces, a work platform and the flare pipe at the top */
static prop_model m_mast(float H, uint32_t seed, int lod) {
  pm *b = pm_new(seed);
  float w0 = H * 0.085f, w1 = H * 0.022f;
  int tiers = lod ? 4 : 9;
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? 1.0f : -1.0f;
    pm_rod(b, V3(sx * w0, 0, sz * w0), V3(sx * w1, H, sz * w1), 0.2f + H * 0.003f, 6, STEEL);
  }
  for (int k = 0; k < tiers; k++) {
    float y0 = H * (float)k / (float)tiers, y1 = H * (float)(k + 1) / (float)tiers;
    float a0 = lerpf(w0, w1, y0 / H), a1 = lerpf(w0, w1, y1 / H);
    float th = 0.1f + H * 0.0015f;
    pm_beam(b, V3(-a1, y1, -a1), V3(a1, y1, -a1), th, STEEL_D), pm_beam(b, V3(-a1, y1, a1), V3(a1, y1, a1), th, STEEL_D);
    pm_beam(b, V3(-a1, y1, -a1), V3(-a1, y1, a1), th, STEEL_D), pm_beam(b, V3(a1, y1, -a1), V3(a1, y1, a1), th, STEEL_D);
    if (!lod) {
      pm_beam(b, V3(-a0, y0, -a0), V3(a1, y1, -a1), th, STEEL_D), pm_beam(b, V3(a0, y0, a0), V3(-a1, y1, a1), th, STEEL_D);
      pm_beam(b, V3(-a0, y0, a0), V3(-a1, y1, -a1), th, STEEL_D), pm_beam(b, V3(a0, y0, -a0), V3(a1, y1, a1), th, STEEL_D);
    }
  }
  pm_layer(b, PL_MAIN);
  pm_push(b), pm_move(b, 0, H * 0.78f, 0), pm_box(b, w1 * 5.0f, 0.3f, w1 * 5.0f, STEEL_D, STEEL), pm_pop(b);
  pm_push(b), pm_move(b, 0, H, 0), pm_cyl(b, w1 * 1.1f, w1 * 0.8f, H * 0.045f, 12, SOOT, STEEL_D), pm_pop(b);
  pm_push(b), pm_move(b, 0, H * 1.045f, 0), pm_cyl(b, w1 * 0.8f, w1 * 1.3f, H * 0.05f, 12, STEEL_D, SOOT), pm_pop(b);
  return pm_finish(b);
}

/* a rail tanker: a barrel on a frame with two bogies and a dome hatch; local +z is its length */
static prop_model m_tanker(uint32_t seed) {
  pm *b = pm_new(seed);
  pcol skin[4] = { SOOT, rgb(0.5f, 0.2f, 0.12f), rgb(0.55f, 0.56f, 0.58f), rgb(0.2f, 0.25f, 0.22f) };
  pcol c = skin[hh(seed, 4) & 3];
  pm_layer(b, PL_MAIN);
  pm_push(b), pm_move(b, 0, 2.2f, -5.8f), pm_rotx(b, 1.5707963f), pm_cyl(b, 1.55f, 1.55f, 11.6f, 14, pc_mul(c, 0.6f), c), pm_pop(b);
  for (int k = 0; k < 3; k++) pm_push(b), pm_move(b, 0, 2.2f, -3.2f + 3.2f * (float)k), pm_rotx(b, 1.5707963f), pm_cyl(b, 1.6f, 1.6f, 0.3f, 14, STEEL_D, STEEL_D), pm_pop(b);
  pm_push(b), pm_move(b, 0, 3.6f, 0), pm_cyl(b, 0.6f, 0.5f, 0.5f, 10, STEEL_D, STEEL), pm_pop(b);
  pm_push(b), pm_move(b, 0, 0.8f, 0), pm_box_c(b, 1.9f, 0.3f, 12.4f, STEEL_D), pm_pop(b);
  pm_layer(b, PL_METAL);
  for (int s = -1; s <= 1; s += 2) {
    for (int ax = -1; ax <= 1; ax += 2)
      for (int side = -1; side <= 1; side += 2)
        pm_push(b), pm_move(b, (float)side * 0.72f, 0.5f, (float)s * 4.2f + (float)ax * 0.9f), pm_roty(b, 1.5707963f), pm_rotx(b, 1.5707963f), pm_move(b, 0, -0.1f, 0), pm_cyl(b, 0.5f, 0.5f, 0.2f, 10, STEEL_D, STEEL), pm_pop(b);
    pm_beam(b, V3(-0.8f, 0.45f, (float)s * 4.2f), V3(0.8f, 0.45f, (float)s * 4.2f), 0.18f, STEEL_D);
  }
  return pm_finish(b);
}

/* ── instanced sets ──────────────────────────────────────────────────────────────────────────────── */

typedef struct iset {
  prop_model m[2];
  prop_batch *b[2][PL_COUNT];
  int n;
} iset;
static void iset_begin(iset *s, prop_model m0, prop_model m1) {
  memset(s, 0, sizeof *s);
  s->m[0] = m0, s->m[1] = m1;
  for (int k = 0; k < 2; k++)
    for (int l = 0; l < PL_COUNT; l++)
      if (s->m[k].g[l]) {
        s->b[k][l] = pb_begin(s->m[k].g[l], prop_material(l, -1, (pcol){ 1, 1, 1 }, 1));
        pb_chunk(s->b[k][l], k ? 320.0f : 170.0f); /* few big cells: each is a draw per layer, and the far ones are drawn through fog anyway */
      }
}
static void iset_add(iset *s, int lod, t3_vec3 pos, float yaw, float sc) {
  if (!s->b[lod][PL_MAIN] && !s->b[lod][PL_METAL]) lod = 0;
  for (int l = 0; l < PL_COUNT; l++)
    if (s->b[lod][l]) pb_add(s->b[lod][l], pos, yaw, V3(sc, sc, sc));
  s->n++;
}
static void iset_end(iset *s, const char *name, bool shadows) {
  for (int k = 0; k < 2; k++)
    for (int l = 0; l < PL_COUNT; l++) {
      if (!s->b[k][l]) continue;
      char nm[48];
      snprintf(nm, sizeof nm, "%s%d.%d", name, k, l);
      if (pb_count(s->b[k][l]) > 0) pb_finish(s->b[k][l], R.root, nm, shadows && k == 0 && l != PL_GLOW);
      else pb_finish(s->b[k][l], R.root, nm, false);
      s->b[k][l] = NULL;
    }
  prop_model_free(&s->m[0]), prop_model_free(&s->m[1]);
}

/* ── set pieces and scatter go here ──────────────────────────────────────────────────────────────── */

/* ── small placement helpers ─────────────────────────────────────────────────────────────────────── */

static float yaw_of(t3_vec3 f) { return atan2f(f.x, f.z); }
/* is s inside any tunnel or jump (plus a margin)? Set pieces and clutter keep clear of them */
static bool near_special(float s, float margin) {
  for (int i = 0; i < R.ntun; i++)
    if (s > (float)R.tun[i][0] - margin && s < (float)R.tun[i][1] + margin) return true;
  for (int i = 0; i < R.ngap; i++)
    if (s > (float)R.gap[i][0] - margin - 70 && s < (float)R.gap[i][1] + margin + 50) return true;
  return false;
}
static bool near_gap(float s, float m) {
  for (int i = 0; i < R.ngap; i++)
    if (s > (float)R.gap[i][0] - m && s < (float)R.gap[i][1] + m) return true;
  return false;
}
static bool in_pit(float x, float z, float pad) {
  for (int g = 0; g < R.ngap; g++) {
    float dx = x - R.pit_c[g].x, dz = z - R.pit_c[g].z;
    float u = dx * R.pit_f[g].x + dz * R.pit_f[g].z, v = dx * R.pit_r[g].x + dz * R.pit_r[g].z;
    if (fabsf(u) < R.pit_hl[g] + pad && fabsf(v) < R.pit_hw[g] + pad) return true;
  }
  return false;
}
/* a lit, flat road-frame position: a point on the road surface plus a lift along the road's own up */
static t3_vec3 road_pt(float s, float lat, float lift) { return track_point(R.t, s, lat, lift); }

/* ── the start: a giant flare stack beside the grid whose boom carries burners and a sheet of flame over the line ─── */

/* a lattice mast on the ground at base, as one group of meshes; its flame is the caller's */
static t3_object *mast_at(t3_vec3 base, float H, uint32_t seed) {
  prop_model mast = m_mast(H, seed, 0);
  t3_object *grp = group_at(R.dyn, base);
  for (int l = 0; l < PL_COUNT; l++)
    if (mast.g[l]) {
      t3_mesh *me = t3_mesh_new(mast.g[l], prop_material(l, -1, (pcol){ 1, 1, 1 }, 1));
      me->base.cast_shadow = l != PL_GLOW;
      me->base.receive_shadow = true;
      t3_object_add(grp, me);
      t3_release(me);
    }
  prop_model_free(&mast);
  return grp;
}

static t3_vec3 ground_pt(float s, float lat) {
  t3_vec3 p = road_pt(s, lat, 0);
  p.y = terrain(p.x, p.z, NULL);
  return p;
}

static void start_stack(void) {
  /* two giant masts frame the grid, one each side, each with a roaring flame and a light that warms the road */
  t3_vec3 la = ground_pt(58.0f, -34.0f), lb = ground_pt(80.0f, 41.0f);
  float Ha = 48.0f, Hb = 40.0f;
  keep_add(la.x, la.z, 18), keep_add(lb.x, lb.z, 18);
  mast_at(la, Ha, 5u);
  mast_at(lb, Hb, 9u);
  flame_new(v3add(la, V3(0, Ha * 1.1f, 0)), 12.0f, 30.0f, 133u, 0, 0, true, 0.8f);
  flame_new(v3add(lb, V3(0, Hb * 1.1f, 0)), 10.0f, 26.0f, 151u, 0, 0, true, 0.8f);
  lamp_new(v3add(la, V3(8, Ha * 0.8f, 0)), 0xff8a40, 2600.0f, 170.0f, 0, 260.0f);
  lamp_new(v3add(lb, V3(-8, Hb * 0.8f, 0)), 0xff8a40, 2000.0f, 150.0f, 0, 260.0f);

  /* further on, a flare boom reaches out over the road with three burners: a sheet of flame over the course */
  const track *t = R.t;
  float s0 = 150.0f;
  t3_vec3 base = ground_pt(s0, -40.0f);
  keep_add(base.x, base.z, 20);
  float H = 38.0f;
  mast_at(base, H, 6u);
  float topy = H * 1.045f;
  t3_vec3 tgt = road_pt(s0 + 6.0f, 2.0f, 0);
  t3_vec3 span = { tgt.x - base.x, 0, tgt.z - base.z };
  pm *b = pm_new(77u);
  pm_layer(b, PL_METAL);
  t3_vec3 p0 = V3(0, topy - 2.2f, 0), p1 = V3(span.x, topy - 2.2f - 1.2f, span.z);
  pm_rod(b, p0, p1, 0.85f, 10, STEEL);
  pm_rod(b, V3(0, topy - 12.0f, 0), v3add(p1, V3(0, -1.0f, 0)), 0.22f, 6, STEEL_D); /* the brace */
  pm_rod(b, V3(0, topy - 20.0f, 0), v3add(v3mul(p1, 0.55f), V3(0, -1.7f, 0)), 0.2f, 6, STEEL_D);
  pm_layer(b, PL_MAIN);
  for (int k = 0; k < 3; k++) { /* burners: short stand pipes with a flared lip */
    t3_vec3 q = v3add(v3mul(p1, 0.55f + 0.225f * (float)k), V3(0, -1.2f * (0.55f + 0.225f * (float)k) - 2.2f + topy, 0));
    pm_push(b), pm_move(b, q.x, q.y, q.z), pm_cyl(b, 0.8f, 0.8f, 3.0f, 10, STEEL_D, STEEL), pm_move(b, 0, 3.0f, 0), pm_cyl(b, 0.8f, 1.5f, 1.0f, 10, STEEL, SOOT), pm_pop(b);
  }
  prop_model bm = pm_finish(b);
  t3_object *grp = group_at(R.dyn, base);
  for (int l = 0; l < PL_COUNT; l++)
    if (bm.g[l]) {
      t3_mesh *me = t3_mesh_new(bm.g[l], prop_material(l, -1, (pcol){ 1, 1, 1 }, 1));
      me->base.cast_shadow = true;
      t3_object_add(grp, me);
      t3_release(me);
    }
  prop_model_free(&bm);
  for (int k = 0; k < 3; k++) {
    float f = 0.55f + 0.225f * (float)k;
    t3_vec3 q = v3add(base, V3(span.x * f, topy - 2.2f - 1.2f * f + 4.0f, span.z * f));
    flame_new(q, k == 1 ? 12.0f : 9.0f, k == 1 ? 32.0f : 24.0f, 100u + (uint32_t)k * 7u, 0, 0, true, 0.7f);
  }
  flame_new(v3add(base, V3(0, H * 1.1f, 0)), 8.0f, 20.0f, 163u, 0, 0, true, 0.8f);
  lamp_new(v3add(base, V3(span.x * 0.7f, topy - 6.0f, span.z * 0.7f)), 0xff8a40, 2400.0f, 170.0f, 0, 260.0f);
  (void)t;
}

/* ── the pits under the jumps ────────────────────────────────────────────────────────────────────── */

static int beacon_new(t3_vec3 p, float size, int kind, float phase, float period, float duty);

static void pit_build(int g) {
  const track *t = R.t;
  t3_vec3 C = R.pit_c[g], F = R.pit_f[g], Rt = R.pit_r[g];
  float fl = R.pit_floor[g], hl = (float)(R.gap[g][1] - R.gap[g][0]) * 0.5f + 0.4f, hw = R.pit_hw[g] - 2.0f;
  float top_lip = t->s[R.gap[g][0]].p.y, top_land = t->s[R.gap[g][1]].p.y;
  t3_vec3 corner[4];
  for (int i = 0; i < 4; i++) {
    float u = (i == 0 || i == 3) ? -hl : hl, v = (i < 2) ? -hw : hw;
    corner[i] = V3(C.x + F.x * u + Rt.x * v, 0, C.z + F.z * u + Rt.z * v);
  }
  /* four inner walls, dark scorched plate with a heat gradient painted on (a lit mesh) and an additive glow over it */
  t3_material *wall = phys(lin(1, 1, 1), 0.8f, 0.2f);
  wall->vertex_colors = true;
  wall->side = T3_DOUBLE_SIDE;
  tex_apply(wall, TEX_WALL_RUST, 1, 1, 0.8f, 1.0f);
  t3_material *gl = glow_mat(NULL, true);
  mb m = { 0 }, mg = { 0 };
  for (int i = 0; i < 4; i++) {
    t3_vec3 a = corner[i], c = corner[(i + 1) % 4];
    float top = (i == 0 || i == 2) ? 0.0f : 0.0f;
    /* sides (i = 1, 3 run along the road) top out at the ground; ends (0, 2) under the ramp and landing */
    bool end = (i == 3 || i == 1);
    (void)top;
    float yt_a = end ? (i == 3 ? top_lip - 0.95f : top_land - 0.95f) : 0.2f;
    float yt_c = yt_a;
    float len = sqrtf((c.x - a.x) * (c.x - a.x) + (c.z - a.z) * (c.z - a.z));
    int cols = (int)(len / 4.0f) + 1;
    for (int k = 0; k < cols; k++) {
      float f0 = (float)k / (float)cols, f1 = (float)(k + 1) / (float)cols;
      t3_vec3 p0 = V3(lerpf(a.x, c.x, f0), 0, lerpf(a.z, c.z, f0)), p1 = V3(lerpf(a.x, c.x, f1), 0, lerpf(a.z, c.z, f1));
      float h0 = yt_a - fl, u1 = len * f1 / 4.0f, u0 = len * f0 / 4.0f;
      pcol cb = rgb(0.8f, 0.5f, 0.35f), ct = rgb(0.5f, 0.42f, 0.4f);
      t3_vec3 nin = v3norm(V3(C.x - (p0.x + p1.x) * 0.5f, 0, C.z - (p0.z + p1.z) * 0.5f));
      int i0 = mb_vn(&m, V3(p0.x, fl, p0.z), nin, 0, u0, cb);
      mb_vn(&m, V3(p1.x, fl, p1.z), nin, 0, u1, cb), mb_vn(&m, V3(p1.x, yt_c, p1.z), nin, h0 / 4.0f, u1, ct), mb_vn(&m, V3(p0.x, yt_a, p0.z), nin, h0 / 4.0f, u0, ct);
      mb_t(&m, i0, i0 + 1, i0 + 2), mb_t(&m, i0, i0 + 2, i0 + 3);
      /* the glow: bright orange at the foot, gone by 45 percent of the height */
      float gy = fl + h0 * 0.5f;
      pcol hotc = hot(1.2f, 0.4f, 0.07f), none = hot(0, 0, 0);
      mb_quad(&mg, V3(p0.x, fl, p0.z), V3(p1.x, fl, p1.z), V3(p1.x, gy, p1.z), V3(p0.x, gy, p0.z), hotc, hotc, none, none);
    }
  }
  t3_geometry *gm = mb_done(&m);
  put(R.dyn, gm, wall, V3(0, 0, 0), false, true);
  put(R.dyn, mb_done(&mg), gl, V3(0, 0.0f, 0), false, false);
  t3_release(wall);
  t3_release(gl);
  /* the slag floor */
  mb f = { 0 };
  float fx = hl + 2.0f, fz = hw + 1.0f;
  t3_vec3 q[4] = { V3(C.x - F.x * fx - Rt.x * fz, fl + 0.2f, C.z - F.z * fx - Rt.z * fz), V3(C.x + F.x * fx - Rt.x * fz, fl + 0.2f, C.z + F.z * fx - Rt.z * fz),
                   V3(C.x + F.x * fx + Rt.x * fz, fl + 0.2f, C.z + F.z * fx + Rt.z * fz), V3(C.x - F.x * fx + Rt.x * fz, fl + 0.2f, C.z - F.z * fx + Rt.z * fz) };
  pcol w = hot(1, 1, 1);
  int i0 = mb_v(&f, q[0], 0, 0, w);
  mb_v(&f, q[1], fx * 2 / 11.0f, 0, w), mb_v(&f, q[2], fx * 2 / 11.0f, fz * 2 / 11.0f, w), mb_v(&f, q[3], 0, fz * 2 / 11.0f, w);
  mb_t(&f, i0, i0 + 2, i0 + 1), mb_t(&f, i0, i0 + 3, i0 + 2);
  put(R.dyn, mb_done(&f), R.lava_mat[g & 1], V3(0, 0, 0), false, false);
  /* flames licking up from the floor: a loose grid, taller toward the middle */
  int n = 0;
  for (int k = 0; k < 11; k++) {
    float u = (hf(40u + (uint32_t)g, (uint32_t)k) * 2 - 1) * (hl - 1.5f), v = (hf(60u + (uint32_t)g, (uint32_t)k) * 2 - 1) * (hw - 2.0f);
    float mid = 1.0f - fabsf(u) / hl * 0.6f;
    float h = (17.0f + 14.0f * hf(70u + (uint32_t)g, (uint32_t)k)) * mid;
    t3_vec3 p = V3(C.x + F.x * u + Rt.x * v, fl + 0.3f, C.z + F.z * u + Rt.z * v);
    if (flame_new(p, 4.0f + 3.0f * hf(80u, (uint32_t)k), h, 300u + (uint32_t)(g * 20 + k), 0, 0, k % 2 == 0, 0.8f) >= 0) n++;
  }
  lamp_new(V3(C.x, fl + 10.0f, C.z), 0xff7a30, 2600.0f, 95.0f, 0, 190.0f);
  /* amber warning lamps blink along the lip and the landing edge */
  for (int e = 0; e < 2; e++)
    for (int sd = -1; sd <= 1; sd += 2) {
      int idx = e ? R.gap[g][1] + 2 : R.gap[g][0] - 2;
      t3_vec3 bp = v3add(v3add(t->s[idx].p, v3mul(t->s[idx].r, (float)sd * (t->s[idx].hw + 1.3f))), V3(0, 2.4f, 0));
      beacon_new(bp, 0.32f, 1, (float)(e + (sd > 0)) * 0.2f, 0.8f, 0.5f);
    }
  int sm = emit_new(REC_SMOKE, "PitSmoke", V3(C.x, fl + 8.0f, C.z), 0, 320.0f, 0, 0, 0);
  (void)sm;
  fx_ambient_at(R.scene, FXA_EMBERS, V3(C.x, fl + 6.0f, C.z), V3(hl, 5.0f, hw), 2.2f, 240.0f);
  cw_info("refinery: pit %d floor %.1f flames %d", g, fl, n);
}

/* ── the refinery's body: process units on a loose grid, tall and sparse far out ───────────────────── */

static void yards(void) {
  const track *t = R.t;
  iset crack_a, crack_b, sph, cyl, stk;
  iset_begin(&crack_a, m_crack(60, 3u, 0), m_crack(60, 3u, 1));
  iset_begin(&crack_b, m_crack(100, 8u, 0), m_crack(100, 8u, 1));
  iset_begin(&sph, m_sphere(10, 5u, 0), m_sphere(10, 5u, 1));
  iset_begin(&cyl, m_cyltank(8, 14, 6u, 0), m_cyltank(8, 14, 6u, 1));
  iset_begin(&stk, m_stack(70, 2u, 0), m_stack(70, 2u, 1));
  prop_set *ps = ps_new(R.root, "YardProps");
  float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
  for (int i = 0; i < t->n; i += 4) {
    minx = fminf(minx, t->s[i].p.x), maxx = fmaxf(maxx, t->s[i].p.x);
    minz = fminf(minz, t->s[i].p.z), maxz = fmaxf(maxz, t->s[i].p.z);
  }
  const float step = 46.0f, ext = 760.0f;
  int placed = 0;
  for (float z = minz - ext; z < maxz + ext; z += step)
    for (float x = minx - ext; x < maxx + ext; x += step) {
      uint32_t h = hh((uint32_t)(int)(x / step + 500), (uint32_t)(int)(z / step + 500));
      float px = x + (hf(h, 1) - 0.5f) * step * 0.8f, pz = z + (hf(h, 2) - 0.5f) * step * 0.8f;
      float ry;
      float gap = road_gap(t, px, pz, &ry);
      if (gap < 17.0f) continue;
      if (in_pit(px, pz, 28.0f) || keep_mask(px, pz, NULL) == 0.0f) continue;
      float y = terrain(px, pz, NULL);
      if (y < -1.5f && gap < 70.0f) continue; /* the cutting's slopes */
      float pick = hf(h, 3), yaw = hf(h, 4) * TAU;
      int lod = gap > 220.0f ? 1 : 0;
      t3_vec3 pos = V3(px, y - 0.3f, pz);
      if (gap < 75.0f) {
        if (pick > 0.3f) continue;
        float q = pick / 0.3f;
        if (q < 0.36f) iset_add(&cyl, lod, pos, yaw, 0.7f + 0.6f * hf(h, 5)), placed++;
        else if (q < 0.62f) iset_add(&sph, lod, pos, yaw, 0.55f + 0.5f * hf(h, 5)), placed++;
        else if (q < 0.8f) ps_add(ps, PK_CONTAINER, pos, yaw, 1.0f, h), placed++;
        else ps_add(ps, PK_PIPE_RACK, pos, yaw, 1.3f, h), placed++;
      } else if (gap < 260.0f) {
        if (pick > 0.36f) continue;
        float q = pick / 0.36f;
        if (q < 0.26f) iset_add(&crack_a, lod, pos, yaw, 0.6f + 0.9f * hf(h, 5)), placed++;
        else if (q < 0.42f) iset_add(&crack_b, lod, pos, yaw, 0.5f + 0.6f * hf(h, 5)), placed++;
        else if (q < 0.6f) iset_add(&sph, lod, pos, yaw, 0.8f + 0.8f * hf(h, 5)), placed++;
        else if (q < 0.78f) iset_add(&cyl, lod, pos, yaw, 1.0f + 0.9f * hf(h, 5)), placed++;
        else if (q < 0.88f) {
          float sc = 0.7f + 0.7f * hf(h, 5);
          iset_add(&stk, lod, pos, yaw, sc), placed++;
          if (R.nbpos < 16 && lod == 0) R.bpos[R.nbpos++] = v3add(pos, V3(0, 71.5f * sc, 0));
        }
        else if (q < 0.94f) ps_add(ps, PK_SILO, pos, yaw, 1.3f, h), placed++;
        else ps_add(ps, PK_COOLING_TOWER, pos, yaw, 1.1f, h), placed++;
      } else {
        if (pick > 0.26f) continue;
        float q = pick / 0.26f;
        if (q < 0.34f) iset_add(&crack_b, 1, pos, yaw, 0.8f + 0.9f * hf(h, 5)), placed++;
        else if (q < 0.58f) iset_add(&stk, 1, pos, yaw, 0.9f + 1.0f * hf(h, 5)), placed++;
        else if (q < 0.78f) iset_add(&crack_a, 1, pos, yaw, 1.0f + 1.0f * hf(h, 5)), placed++;
        else iset_add(&sph, 1, pos, yaw, 1.2f + 1.0f * hf(h, 5)), placed++;
      }
    }
  cw_info("refinery: %d yard pieces", placed);
  iset_end(&crack_a, "CrackA", true), iset_end(&crack_b, "CrackB", true), iset_end(&sph, "Sphere", true), iset_end(&cyl, "CylTank", true), iset_end(&stk, "Stack", false);
  ps_finish(ps);
}

/* the near zone beside the road: barrels, pipe runs, containers, cones, pylons, low enough to keep the course readable */
static void roadside(void) {
  const track *t = R.t;
  prop_set *ps = ps_new(R.root, "Roadside");
  scatter_opts o = scatter_defaults();
  o.from_s = 0, o.to_s = t->length, o.spacing = 24, o.jitter = 0.8f, o.lat_min = 3.0f, o.lat_max = 26.0f;
  o.kinds[0] = PK_BARREL, o.weight[0] = 3, o.kinds[1] = PK_CONTAINER, o.weight[1] = 1.4f, o.kinds[2] = PK_CONE, o.weight[2] = 1.0f;
  o.kinds[3] = PK_TYRE_STACK, o.weight[3] = 0.6f;
  o.nkinds = 4, o.scale_min = 0.9f, o.scale_max = 1.5f, o.lat_curve = 1.8f, o.ground = terrain, o.mask = keep_mask, o.margin = 2.5f, o.seed = 21;
  scatter_along(ps, t, &o);
  /* the odd crane and pylon among the process units */
  o.spacing = 120, o.lat_min = 30, o.lat_max = 110, o.lat_curve = 1.0f, o.margin = 6;
  o.kinds[0] = PK_CRANE, o.weight[0] = 1, o.kinds[1] = PK_ANTENNA, o.weight[1] = 0.6f;
  o.nkinds = 2, o.scale_min = 0.9f, o.scale_max = 1.3f, o.seed = 22, o.far_thin = 0.3f, o.mask = keep_mask;
  scatter_along(ps, t, &o);
  ps_finish(ps);
}

/* pipes that run beside the road on low sleepers: a bundle of two or three, in steel, hazard yellow and rust red.
 * They give a sense of speed along the walls and keep the sides from being empty lots. */
static void pipe_runs(void) {
  const track *t = R.t;
  pmc *c = pmc_new(60, 91u);
  int made = 0;
  for (int side = -1; side <= 1; side += 2)
    for (float s0 = 20; s0 + 120 < t->length; s0 += 120) {
      uint32_t h = hh((uint32_t)(s0 / 120.0f), (uint32_t)(side + 7));
      if (hf(h, 1) > 0.5f || near_special(s0 + 60, 0)) continue;
      int bundle = 2 + (int)(hf(h, 2) * 2);
      for (float s = s0; s < s0 + 120; s += 4.0f) {
        tsample a = track_at(t, s);
        if (a.flags & (TF_GAP | TF_TUNNEL)) continue;
        for (int k = 0; k < bundle; k++) {
          float lat = (float)side * (a.hw + 4.6f + 1.1f * (float)k);
          float r = 0.42f + 0.12f * (float)((int)(hf(h, 5 + (uint32_t)k) * 3));
          pcol col = k == 0 ? STEEL : k == 1 ? HAZARD : RUST;
          if (hf(h, 9 + (uint32_t)k) > 0.6f) col = STEEL_D;
          t3_vec3 p0 = road_pt(s, lat, r + 0.35f), p1 = road_pt(s + 4.2f, lat, r + 0.35f);
          pm *b = pmc_at(c, p0);
          pm_push(b);
          pm_layer(b, PL_METAL);
          pm_rod(b, p0, p1, r, 8, col);
          if (((int)(s / 4.0f)) % 3 == 0) {
            pm_rod(b, v3add(p0, V3(0, -r - 0.35f, 0)), v3add(p0, V3(0, r * 0.4f, 0)), 0.12f, 5, STEEL_D); /* the post */
            pm_rod(b, p0, v3add(p0, v3mul(v3sub(p1, p0), 0.08f)), r * 1.25f, 8, STEEL_D);                  /* a flange band */
          }
          pm_pop(b);
        }
        made++;
      }
    }
  pmc_finish(c, R.root, "PipeRuns", true, -1);
  cw_info("refinery: %d pipe run steps", made);
}

/* ── tunnels ─────────────────────────────────────────────────────────────────────────────────────── */

/* a builder standing in the road's own frame at a sample: x right, y up, z forward */
static pm *frame_pm(pmc *c, const tsample *a, float lat, float lift) {
  t3_vec3 p = v3add(v3add(a->p, v3mul(a->r, lat)), v3mul(a->u, lift));
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_basis(b, p, a->r, a->u, a->f);
  return b;
}
/* the tube's half-section: the point of its outline at fraction k of the arch, pushed out by `off` metres */
static void tube_pt(float hw, float k, float off, float *x, float *y) {
  float Rt = 8.0f - 1.6f, ang = 3.14159265f * k;
  float cx = -hw * cosf(ang), cy = 1.6f + Rt * sinf(ang);
  float nx = -cx / (hw * hw), ny = (cy - 1.6f) / (Rt * Rt) * -1.0f;
  (void)ny;
  /* the outward normal of the ellipse (x/hw)^2 + ((y-1.6)/Rt)^2 = 1 */
  nx = cx / (hw * hw);
  float nyv = (cy - 1.6f) / (Rt * Rt), nl = sqrtf(nx * nx + nyv * nyv);
  *x = cx + off * nx / nl, *y = cy + off * nyv / nl;
}

/* a heavy hazard-striped collar round a tunnel mouth, a lit amber lip and two columns; the mouth faces `dir` (+1 the
 * way the road runs out of it, -1 into it) */
static void portal(pmc *c, int sample, int dir) {
  const track *t = R.t;
  tsample a = t->s[sample];
  const int N = 18;
  pm *b = frame_pm(c, &a, 0, 0);
  for (int k = 0; k < N; k++) {
    float x0, y0, x1, y1, gx0, gy0, gx1, gy1;
    tube_pt(a.hw, (float)k / N, 0.8f, &x0, &y0), tube_pt(a.hw, (float)(k + 1) / N, 0.8f, &x1, &y1);
    pm_layer(b, PL_MAIN);
    pm_beam(b, V3(x0, y0, 0.6f * (float)dir), V3(x1, y1, 0.6f * (float)dir), 1.3f, (k & 1) ? HAZARD : SOOT);
    tube_pt(a.hw, (float)k / N, 0.08f, &gx0, &gy0), tube_pt(a.hw, (float)(k + 1) / N, 0.08f, &gx1, &gy1);
    pm_layer(b, PL_GLOW);
    pm_beam(b, V3(gx0, gy0, 1.3f * (float)dir), V3(gx1, gy1, 1.3f * (float)dir), 0.2f, glow(rgb(1, 0.5f, 0.1f), 3.2f));
  }
  pm_layer(b, PL_MAIN);
  for (int side = -1; side <= 1; side += 2) {
    pm_push(b), pm_move(b, (float)side * (a.hw + 0.8f), 0, 0.6f * (float)dir), pm_box(b, 1.3f, 1.8f, 1.3f, SOOT, HAZARD), pm_pop(b);
  }
  pm_pop(b);
}

/* Tunnel interiors have only their own lamps and slits, so the metal floor would read black. A string of small
 * clustered point lights down the middle of each tunnel (dark unless a kart is near, so the light count never
 * changes) makes the floor's plate shine and lights the lining; bends follow the road. */
static void tunnel_lights(int ti, float spacing, float base, uint32_t colour) {
  const track *t = R.t;
  int a = R.tun[ti][0], b = R.tun[ti][1];
  for (float s = (float)a + spacing * 0.5f; s < (float)b; s += spacing) lamp_new(road_pt(s, 0, 5.2f), colour, base, spacing * 0.9f, 1, 70.0f);
  (void)t;
}

/* the first tunnel runs through a bank of storage tanks: spheres and drums stand shoulder to shoulder either side,
 * pipe bridges with drums on them cross over the roof, steam rises through them; inside, furnace slits glow */
static void tunnel_tanks(int ti) {
  const track *t = R.t;
  int a = R.tun[ti][0], b = R.tun[ti][1];
  iset sph, cyl;
  iset_begin(&sph, m_sphere(10, 15u, 0), m_sphere(10, 15u, 1));
  iset_begin(&cyl, m_cyltank(8, 14, 16u, 0), m_cyltank(8, 14, 16u, 1));
  pmc *c = pmc_new(60, 520u);
  int ns = 0;
  for (float s = (float)a + 12.0f; s < (float)b - 12.0f; s += 31.0f, ns++) {
    tsample m = track_at(t, s);
    for (int side = -1; side <= 1; side += 2) {
      uint32_t h = hh((uint32_t)ns * 3u + (uint32_t)(side + 2), 17u);
      bool ball = hf(h, 1) < 0.58f;
      float sc = ball ? 1.0f + 0.4f * hf(h, 2) : 1.0f + 0.35f * hf(h, 2);
      float rad = (ball ? 10.0f : 8.0f) * sc;
      float lat = (float)side * (m.hw + 1.4f + rad);
      t3_vec3 p = road_pt(s, lat, 0);
      float ry, gap = road_gap(t, p.x, p.z, &ry);
      if (gap < rad - 3.0f && fabsf(ry - p.y) < 6.0f) {
        /* a bend brings another part of the road close: push the tank outward */
        lat += (float)side * (rad - gap + 1.0f);
        p = road_pt(s, lat, 0);
      }
      p.y = fminf(p.y, terrain(p.x, p.z, NULL)) - 0.3f;
      if (ball) iset_add(&sph, 0, p, hf(h, 3) * TAU, sc);
      else iset_add(&cyl, 0, p, hf(h, 3) * TAU, sc);
    }
    /* a pipe bridge over the tube: a drum lying across the roof on two A-frames, a catwalk on top */
    if (ns % 2 == 0) {
      tsample q = m;
      pm *p = frame_pm(c, &q, 0, 0);
      float half = q.hw + 7.0f, ty = 8.2f;
      pm_layer(p, PL_MAIN);
      pm_push(p), pm_move(p, -half, ty + 2.2f, 0), pm_rotz(p, -1.5707963f), pm_cyl(p, 2.2f, 2.2f, 2.0f * half, 14, rgb(0.5f, 0.52f, 0.56f), rgb(0.82f, 0.82f, 0.84f)), pm_pop(p);
      pm_layer(p, PL_METAL);
      for (int side = -1; side <= 1; side += 2) {
        float x = (float)side * (half - 1.0f);
        pm_rod(p, V3(x, -1.0f, -2.6f), V3(x, ty + 1.0f, -0.4f), 0.3f, 6, STEEL_D);
        pm_rod(p, V3(x, -1.0f, 2.6f), V3(x, ty + 1.0f, 0.4f), 0.3f, 6, STEEL_D);
      }
      pm_beam(p, V3(-half, ty + 4.7f, -1.0f), V3(half, ty + 4.7f, -1.0f), 0.12f, HAZARD);
      pm_beam(p, V3(-half, ty + 4.7f, 1.0f), V3(half, ty + 4.7f, 1.0f), 0.12f, HAZARD);
      pm_layer(p, PL_GLOW);
      for (int k = -2; k <= 2; k++) pm_push(p), pm_move(p, (float)k * half * 0.4f, ty + 4.0f, 0), pm_box_c(p, 0.45f, 0.3f, 0.45f, glow(rgb(1, 0.6f, 0.2f), 3.0f)), pm_pop(p);
      pm_pop(p);
      emit_new(REC_VENT, "TankSteam", road_pt(s, (ns % 4 == 0 ? -1.0f : 1.0f) * 5.0f, ty + 4.8f), 1, 200.0f, 4.6f, 0.5f, hf((uint32_t)ns, 5) * 4.0f);
    }
  }
  iset_end(&sph, "BankSphere", true), iset_end(&cyl, "BankDrum", true);
  /* portals */
  pmc *pc = pmc_new(60, 521u);
  portal(pc, a, -1), portal(pc, b, 1);
  pmc_finish(pc, R.root, "Portals", true, -1);
  /* the furnace slits inside, and floor vents that puff steam */
  pm *sl = NULL;
  int slits = 0;
  for (float s = (float)a + 9.0f; s < (float)b - 6.0f; s += 13.0f, slits++) {
    tsample m = track_at(t, s);
    for (int side = -1; side <= 1; side += 2) {
      if (((slits + (side > 0)) & 1) && slits % 3) continue;
      sl = frame_pm(c, &m, (float)side * (m.hw - 0.24f), 2.9f);
      pm_layer(sl, PL_GLOW);
      pm_rotz(sl, (float)side * 0.2f);
      pm_box_c(sl, 0.14f, 2.1f, 2.6f, glow(rgb(1, 0.34f, 0.05f), 2.0f));
      pm_pop(sl);
    }
    for (int side = -1; side <= 1; side += 2) {
      pm *fl = frame_pm(c, &m, (float)side * (m.hw - 1.3f), 0.05f);
      pm_layer(fl, PL_GLOW);
      pm_box_c(fl, 0.5f, 0.05f, 1.8f, glow(rgb(1, 0.55f, 0.12f), 2.4f));
      pm_pop(fl);
    }
    if (slits % 3 == 1) emit_new(REC_VENT, "TunnelVent", road_pt(s, ((slits / 3) & 1) ? 6.5f : -6.5f, 0.2f), 1, 150.0f, 3.8f, 0.42f, (float)slits * 0.7f);
  }
  pmc_finish(c, R.root, "TankBank", true, -1);
  cw_info("refinery: tank tunnel %d bays %d slits", ns, slits);
}

/* the second tunnel hides under a bridge of fat pipes: bundles on the roof running its length, a rack across the
 * middle, red-hot expansion joints and a curtain of sparks falling over each mouth */
static void tunnel_pipes(int ti) {
  const track *t = R.t;
  int a = R.tun[ti][0], b = R.tun[ti][1];
  pmc *c = pmc_new(50, 610u);
  float pr[5] = { 1.5f, 1.2f, 1.0f, 1.3f, 0.8f }, plat[5] = { -6.2f, -3.0f, 0.2f, 3.6f, 6.6f }, ph[5] = { 10.4f, 11.6f, 10.0f, 11.2f, 10.4f };
  pcol pcl[5] = { STEEL, HAZARD, STEEL_D, RUST, STEEL };
  for (int k = 0; k < 5; k++) {
    for (float s = (float)a - 14.0f; s < (float)b + 14.0f; s += 6.0f) {
      tsample m0 = track_at(t, s), m1 = track_at(t, s + 6.2f);
      t3_vec3 p0 = v3add(v3add(m0.p, v3mul(m0.r, plat[k])), V3(0, ph[k], 0)), p1 = v3add(v3add(m1.p, v3mul(m1.r, plat[k])), V3(0, ph[k], 0));
      pm *p = pmc_at(c, p0);
      pm_push(p);
      pm_layer(p, PL_METAL);
      pm_rod(p, p0, p1, pr[k], 10, pcl[k]);
      int bay = (int)((s - (float)a + 14.0f) / 6.0f);
      if (bay % 3 == 0) pm_rod(p, p0, v3add(p0, v3mul(v3sub(p1, p0), 0.1f)), pr[k] * 1.3f, 10, STEEL_D); /* a flange */
      if (bay % 5 == k) { /* a red-hot expansion joint: glows */
        pm_layer(p, PL_GLOW);
        pm_rod(p, v3add(p0, v3mul(v3sub(p1, p0), 0.3f)), v3add(p0, v3mul(v3sub(p1, p0), 0.65f)), pr[k] * 1.05f, 10, glow(rgb(1, 0.3f, 0.05f), 1.8f));
      }
      pm_pop(p);
    }
  }
  /* A-frame trestles every 18 m carrying the bundle */
  for (float s = (float)a - 12.0f; s < (float)b + 14.0f; s += 18.0f) {
    tsample m = track_at(t, s);
    for (int side = -1; side <= 1; side += 2) {
      pm *p = frame_pm(c, &m, (float)side * (m.hw + 3.2f), 0);
      pm_layer(p, PL_METAL);
      pm_rod(p, V3(0, -0.5f, -1.5f), V3((float)-side * 3.0f, 12.2f, 0), 0.35f, 6, STEEL_D);
      pm_rod(p, V3(0, -0.5f, 1.5f), V3((float)-side * 3.0f, 12.2f, 0), 0.35f, 6, STEEL_D);
      pm_pop(p);
    }
    pm *q = frame_pm(c, &m, 0, 0);
    pm_layer(q, PL_METAL);
    pm_beam(q, V3(-9.0f, 9.0f, 0), V3(9.0f, 9.0f, 0), 0.5f, STEEL_D);
    pm_layer(q, PL_GLOW);
    pm_push(q), pm_move(q, 0, 9.0f - 0.5f, 0), pm_box_c(q, 0.5f, 0.3f, 0.5f, glow(rgb(1, 0.6f, 0.2f), 3.0f)), pm_pop(q);
    pm_pop(q);
  }
  /* the mouths */
  portal(c, a, -1), portal(c, b, 1);
  /* sparks falling over the mouths and an arc of them at the bridge's heart */
  for (int e = 0; e < 2; e++) {
    int idx = e ? b : a;
    tsample m = t->s[idx];
    t3_vec3 at = v3add(v3add(m.p, v3mul(m.f, e ? 1.5f : -1.5f)), V3(0, 8.8f, 0));
    int em = emit_new(REC_SHOWER, "MouthSparks", at, 1, 220.0f, 2.4f, 0.7f, (float)e * 0.9f);
    if (em >= 0) {
      /* the box of the shower lies across the road: turn it to the road's right */
      cw_node3d_set_quaternion(R.em[em].n, t3_quat_from_axis_angle(V3(0, 1, 0), yaw_of(m.f)));
    }
  }
  int mid = (a + b) / 2;
  tsample m = t->s[mid];
  emit_new(REC_SPARKS, "BridgeSparks", v3add(v3add(m.p, v3mul(m.r, 4.0f)), V3(0, 13.0f, 0)), 1, 200.0f, 1.7f, 0.5f, 0.3f);
  emit_new(REC_SPARKS, "BridgeSparks2", v3add(v3add(m.p, v3mul(m.r, -5.0f)), V3(0, 12.4f, 0)), 1, 200.0f, 2.1f, 0.4f, 1.1f);
  pmc_finish(c, R.root, "PipeBridge", true, -1);
  /* inside: slits like the first tunnel but red, fewer */
  pmc *ic = pmc_new(60, 611u);
  int n = 0;
  for (float s = (float)a + 8.0f; s < (float)b - 4.0f; s += 11.0f, n++) {
    tsample q = track_at(t, s);
    for (int side = -1; side <= 1; side += 2) {
      pm *sl = frame_pm(ic, &q, (float)side * (q.hw - 0.24f), 2.9f);
      pm_layer(sl, PL_GLOW);
      pm_rotz(sl, (float)side * 0.2f);
      pm_box_c(sl, 0.14f, 2.1f, 2.4f, glow(rgb(1, 0.14f, 0.04f), 2.0f));
      pm_pop(sl);
      pm *fl = frame_pm(ic, &q, (float)side * (q.hw - 1.3f), 0.05f);
      pm_layer(fl, PL_GLOW);
      pm_box_c(fl, 0.5f, 0.05f, 1.8f, glow(rgb(1, 0.3f, 0.1f), 2.4f));
      pm_pop(fl);
    }
  }
  pmc_finish(ic, R.root, "PipeSlits", false, -1);
}

/* ── pipe racks that cross over the road at height, on lattice towers ─────────────────────────────── */

static void lattice(pm *b, float x, float z, float y0, float y1, float w0, float w1) {
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 4; i++) {
    float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? 1.0f : -1.0f;
    pm_rod(b, V3(x + sx * w0, y0, z + sz * w0), V3(x + sx * w1, y1, z + sz * w1), 0.28f, 6, STEEL);
  }
  int tiers = (int)((y1 - y0) / 4.5f) + 1;
  for (int k = 1; k <= tiers; k++) {
    float ya = y0 + (y1 - y0) * (float)(k - 1) / (float)tiers, yb = y0 + (y1 - y0) * (float)k / (float)tiers;
    float wa = lerpf(w0, w1, (float)(k - 1) / (float)tiers), wbv = lerpf(w0, w1, (float)k / (float)tiers);
    pm_beam(b, V3(x - wbv, yb, z - wbv), V3(x + wbv, yb, z - wbv), 0.16f, STEEL_D), pm_beam(b, V3(x - wbv, yb, z + wbv), V3(x + wbv, yb, z + wbv), 0.16f, STEEL_D);
    pm_beam(b, V3(x - wa, ya, z - wa), V3(x + wbv, yb, z - wbv), 0.14f, STEEL_D), pm_beam(b, V3(x + wa, ya, z + wa), V3(x - wbv, yb, z + wbv), 0.14f, STEEL_D);
    pm_beam(b, V3(x - wa, ya, z + wa), V3(x - wbv, yb, z - wbv), 0.14f, STEEL_D), pm_beam(b, V3(x + wa, ya, z - wa), V3(x + wbv, yb, z + wbv), 0.14f, STEEL_D);
  }
}

static void racks(void) {
  const track *t = R.t;
  pmc *c = pmc_new(60, 701u);
  float last = -1000;
  int n = 0;
  for (float s = 150; s < t->length - 150; s += 10) {
    tsample a = track_at(t, s);
    /* near a tunnel or a jump nothing crosses the road, except in the cutting (the road is far below the ground there) */
    if (s - last < 260 || (near_special(s, 60) && !(a.p.y < -8.0f && !near_gap(s, 70.0f)))) continue;
    if (a.flags & (TF_GAP | TF_TUNNEL | TF_LOOP) || fabsf(track_curvature(t, s - 40, 80)) > 0.0009f) continue;
    if (fabsf(a.f.y) > 0.08f) continue;
    last = s;
    n++;
    t3_vec3 rr = v3norm(V3(a.r.x, 0, a.r.z)), ff = v3norm(V3(a.f.x, 0, a.f.z));
    pm *b = pmc_at(c, a.p);
    pm_push(b);
    pm_basis(b, a.p, rr, V3(0, 1, 0), ff);
    float half = a.hw + 6.0f, H = 15.5f + 1.5f * (float)(n & 1);
    for (int side = -1; side <= 1; side += 2) {
      t3_vec3 tw = v3add(a.p, v3mul(rr, (float)side * half));
      float low = fminf(terrain(tw.x, tw.z, NULL) - a.p.y, -0.4f) - 0.4f;
      lattice(b, (float)side * half, 0, low, H, 1.9f, 1.2f);
      pm_layer(b, PL_MAIN);
      pm_push(b), pm_move(b, (float)side * half, low, 0), pm_box(b, 4.6f, 0.9f, 4.6f, SOOT, STEEL_D), pm_pop(b);
    }
    pm_layer(b, PL_METAL);
    for (int z = -1; z <= 1; z += 2) pm_beam(b, V3(-half - 1.8f, H, (float)z * 1.5f), V3(half + 1.8f, H, (float)z * 1.5f), 0.5f, STEEL_D);
    /* the pipes: five bores, steel, yellow, rust, one lagged white */
    pcol pcl[5] = { STEEL, HAZARD, RUST, PAINT_W, STEEL_D };
    float pr[5] = { 0.75f, 0.55f, 0.9f, 0.62f, 0.5f }, pz[5] = { -2.4f, -1.2f, 0.0f, 1.3f, 2.5f };
    for (int k = 0; k < 5; k++) {
      pm_rod(b, V3(-half - 2.2f, H + 0.9f + pr[k] * 0.4f, pz[k]), V3(half + 2.2f, H + 0.9f + pr[k] * 0.4f, pz[k]), pr[k], 10, pcl[k]);
      for (int j = -2; j <= 2; j++) pm_rod(b, V3((float)j * half * 0.4f - 0.2f, H + 0.9f + pr[k] * 0.4f, pz[k]), V3((float)j * half * 0.4f + 0.2f, H + 0.9f + pr[k] * 0.4f, pz[k]), pr[k] * 1.3f, 10, STEEL_D);
    }
    pm_layer(b, PL_MAIN);
    pm_push(b), pm_move(b, 0, H - 1.2f, 0), pm_box_c(b, 2.0f * half, 0.15f, 1.4f, STEEL_D), pm_pop(b);
    for (int k = -3; k <= 3; k++) { /* hazard flags hanging as a height warning */
      pm_push(b), pm_move(b, (float)k * half * 0.3f, H - 2.2f, -1.6f), pm_box_c(b, 0.9f, 0.5f, 0.1f, (k & 1) ? HAZARD : SOOT), pm_pop(b);
    }
    pm_layer(b, PL_GLOW);
    for (int k = -3; k <= 3; k++) pm_push(b), pm_move(b, (float)k * half * 0.3f, H - 1.35f, 0.9f), pm_box_c(b, 0.5f, 0.25f, 0.5f, glow(rgb(1, 0.62f, 0.22f), 3.2f)), pm_pop(b);
    pm_pop(b);
    /* a puff of steam from a vent on its crown */
    emit_new(REC_VENT, "RackSteam", v3add(a.p, V3(0, H + 2.4f, 0)), 1, 200.0f, 5.0f, 0.4f, (float)n * 1.3f);
  }
  pmc_finish(c, R.root, "Racks", true, -1);
  cw_info("refinery: %d pipe racks", n);
}

/* ── blinking lamps ──────────────────────────────────────────────────────────────────────────────── */

/* a small lamp with a halo that blinks on a rhythm: on while fmod(time + phase, period) < duty * period.
 * kind 0 red warning, 1 amber */
static int beacon_new(t3_vec3 p, float size, int kind, float phase, float period, float duty) {
  if (R.nbc >= MAXBCN) return -1;
  beacon *b = &R.bc[R.nbc];
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  m->fog = false;
  pcol c = kind == 0 ? hot(2.2f, 0.12f, 0.08f) : hot(2.4f, 1.1f, 0.2f);
  mb g = { 0 };
  /* an octahedron lamp */
  t3_vec3 v[6] = { V3(size, 0, 0), V3(-size, 0, 0), V3(0, size, 0), V3(0, -size, 0), V3(0, 0, size), V3(0, 0, -size) };
  int i0 = mb_v(&g, v[0], 0, 0, c);
  for (int i = 1; i < 6; i++) mb_v(&g, v[i], 0, 0, c);
  int f[8][3] = { { 0, 2, 4 }, { 4, 2, 1 }, { 1, 2, 5 }, { 5, 2, 0 }, { 0, 4, 3 }, { 4, 1, 3 }, { 1, 5, 3 }, { 5, 0, 3 } };
  for (int i = 0; i < 8; i++) mb_t(&g, i0 + f[i][0], i0 + f[i][1], i0 + f[i][2]);
  put(R.dyn, mb_done(&g), m, p, false, false);
  b->m = m;
  b->hb[0] = 1.0f, b->hb[1] = kind == 0 ? 0.12f : 0.6f, b->hb[2] = kind == 0 ? 0.08f : 0.12f;
  b->halo = halo_material(b->hb[0], b->hb[1], b->hb[2]);
  halo_at(R.dyn, b->halo, p, size * 9.0f);
  b->phase = phase, b->period = period, b->duty = duty;
  return R.nbc++;
}

/* ── lightning on a tall tower ───────────────────────────────────────────────────────────────────── */

static t3_geometry *arc_geo(uint32_t seed, float height, float reach) {
  mb m = { 0 };
  pcol core = hot(2.4f, 2.6f, 3.4f), edge = hot(0.5f, 0.7f, 1.6f);
  /* a trunk from the cloud (up, offset) down to the tip at the origin, with forks; each strand is two crossed ribbons */
  struct { t3_vec3 a, b; float w; } seg[40];
  int ns = 0;
  t3_vec3 top = V3((hf(seed, 1) - 0.5f) * reach, height, (hf(seed, 2) - 0.5f) * reach);
  t3_vec3 prev = top;
  int steps = 11;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps;
    t3_vec3 p = V3(lerpf(top.x, 0, t) + (hf(seed, 10 + (uint32_t)i) - 0.5f) * 11.0f * (1.0f - t * 0.9f), lerpf(top.y, 0, t), lerpf(top.z, 0, t) + (hf(seed, 40 + (uint32_t)i) - 0.5f) * 11.0f * (1.0f - t * 0.9f));
    if (i == steps) p = V3(0, 0, 0);
    seg[ns++] = (typeof(seg[0])){ prev, p, 1.0f + 1.6f * t };
    if (i % 3 == 0 && ns < 34) { /* a fork */
      t3_vec3 q = v3add(p, V3((hf(seed, 70 + (uint32_t)i) - 0.5f) * 40.0f, -hf(seed, 90 + (uint32_t)i) * 16.0f, (hf(seed, 110 + (uint32_t)i) - 0.5f) * 40.0f));
      seg[ns++] = (typeof(seg[0])){ p, v3add(p, v3mul(v3sub(q, p), 0.5f)), 0.55f };
      seg[ns++] = (typeof(seg[0])){ v3add(p, v3mul(v3sub(q, p), 0.5f)), q, 0.32f };
    }
    prev = p;
  }
  for (int i = 0; i < ns; i++) {
    t3_vec3 d = v3norm(v3sub(seg[i].b, seg[i].a));
    t3_vec3 s1 = v3norm(v3cross(d, V3(0, 0, 1))), s2 = v3norm(v3cross(d, s1));
    if (v3len(v3cross(d, V3(0, 0, 1))) < 0.1f) s1 = V3(1, 0, 0), s2 = V3(0, 0, 1);
    for (int k = 0; k < 2; k++) {
      t3_vec3 s = k ? s2 : s1;
      float w = seg[i].w;
      int i0 = mb_v(&m, v3add(seg[i].a, v3mul(s, -w)), 0, 0, edge);
      mb_v(&m, v3add(seg[i].a, v3mul(s, 0)), 0.5f, 0, core), mb_v(&m, v3add(seg[i].a, v3mul(s, w)), 1, 0, edge);
      mb_v(&m, v3add(seg[i].b, v3mul(s, -w)), 0, 1, edge), mb_v(&m, v3add(seg[i].b, v3mul(s, 0)), 0.5f, 1, core), mb_v(&m, v3add(seg[i].b, v3mul(s, w)), 1, 1, edge);
      mb_t(&m, i0, i0 + 1, i0 + 4), mb_t(&m, i0, i0 + 4, i0 + 3), mb_t(&m, i0 + 1, i0 + 2, i0 + 5), mb_t(&m, i0 + 1, i0 + 5, i0 + 4);
    }
  }
  return mb_done(&m);
}

static void lightning_tower(void) {
  const track *t = R.t;
  /* the middle of the first long turn after the first tunnel: the tower stands on its inside */
  float s = (float)R.tun[0][1] + 190.0f;
  tsample a = track_at(t, s);
  float curv = track_curvature(t, s - 30, 60);
  float side = curv > 0 ? -1.0f : 1.0f;
  float lat = side * (a.hw + 78.0f);
  t3_vec3 base = road_pt(s, lat, 0);
  float gap = road_gap(t, base.x, base.z, NULL);
  if (gap < 50) lat = side * (a.hw + 60.0f + (50 - gap)), base = road_pt(s, lat, 0);
  base.y = terrain(base.x, base.z, NULL) - 0.3f;
  float H = 116.0f;
  keep_add(base.x, base.z, 45);
  prop_model m = m_crack(H, 31u, 0);
  t3_object *g = group_at(R.dyn, base);
  for (int l = 0; l < PL_COUNT; l++)
    if (m.g[l]) {
      t3_mesh *me = t3_mesh_new(m.g[l], prop_material(l, -1, (pcol){ 1, 1, 1 }, 1));
      me->base.cast_shadow = l != PL_GLOW;
      me->base.receive_shadow = true;
      t3_object_add(g, me);
      t3_release(me);
    }
  prop_model_free(&m);
  t3_vec3 tip = v3add(base, V3(0, H * 1.0f + 1.0f, 0));
  R.arc_top = tip;
  t3_material *am = glow_mat(NULL, true);
  for (int i = 0; i < 5; i++) {
    t3_mesh *me = put(R.dyn, arc_geo(500u + (uint32_t)i * 13u, 95.0f, 46.0f), am, tip, false, false);
    me->base.visible = false;
    R.arc[i] = &me->base;
  }
  R.arc_glow = halo_at(R.dyn, R.halo_blue, tip, 60.0f);
  T3_OBJ(R.arc_glow)->visible = false;
  R.arc_light = lamp_new(v3add(tip, V3(0, -8.0f, 0)), 0xaac4ff, 14000.0f, 300.0f, 2, 360.0f);
  /* the lit foot: furnace glow and a halo so the tower reads at a distance */
  halo_at(R.dyn, R.halo_mat, v3add(base, V3(0, 3.0f, 0)), 26.0f);
  /* a lightning rod mast and a small fenced compound round the foot */
  cw_info("refinery: lightning tower at s=%.0f (%.0f, %.0f)", s, base.x, base.z);
}

/* ── welding robots on a jig beside the start straight ───────────────────────────────────────────── */

static void robots(void) {
  const track *t = R.t;
  float s = 118.0f;
  float lat = t->s[(int)s].hw + 30.0f;
  t3_vec3 c = road_pt(s, lat, 0);
  c.y = terrain(c.x, c.z, NULL);
  keep_add(c.x, c.z, 16);
  tsample a = track_at(t, s);
  t3_vec3 along = v3norm(V3(a.f.x, 0, a.f.z)), across = v3norm(V3(a.r.x, 0, a.r.z));
  /* the work piece: a fat pipe on two cradles, a seam along its top */
  pm *b = pm_new(61u);
  pm_layer(b, PL_MAIN);
  pm_push(b), pm_move(b, 0, 1.5f, -9.0f), pm_rotx(b, 1.5707963f), pm_cyl(b, 1.3f, 1.3f, 18.0f, 14, rgb(0.4f, 0.4f, 0.43f), rgb(0.62f, 0.62f, 0.66f)), pm_pop(b);
  for (int k = -1; k <= 1; k += 2) pm_push(b), pm_move(b, 0, 0, (float)k * 6.0f), pm_box(b, 3.4f, 1.0f, 1.6f, STEEL_D, HAZARD), pm_pop(b);
  pm_push(b), pm_move(b, 0, 0, 0), pm_box(b, 7.0f, 0.4f, 22.0f, SOOT, STEEL_D), pm_pop(b);
  prop_model wp = pm_finish(b);
  t3_object *jig = group_at(R.dyn, c);
  t3_object_set_rotation(jig, 0, yaw_of(along), 0);
  for (int l = 0; l < PL_COUNT; l++)
    if (wp.g[l]) {
      t3_mesh *me = t3_mesh_new(wp.g[l], prop_material(l, -1, (pcol){ 1, 1, 1 }, 1));
      me->base.cast_shadow = true;
      me->base.receive_shadow = true;
      t3_object_add(jig, me);
      t3_release(me);
    }
  prop_model_free(&wp);
  /* two robots, one each side of the piece, their parts in a hierarchy: turret > upper arm > forearm */
  for (int r = 0; r < 2; r++) {
    float sd = r ? 1.0f : -1.0f;
    t3_vec3 rb = v3add(c, v3mul(across, sd * 5.7f));
    t3_object *root = group_at(R.dyn, rb);
    t3_vec3 face = v3norm(V3(c.x - rb.x, 0, c.z - rb.z));
    t3_object_set_rotation(root, 0, yaw_of(face), 0); /* faces the piece */
    R.rob[r].fwd = face;
    pm *p = pm_new(70u + (uint32_t)r);
    pm_layer(p, PL_MAIN);
    pm_cyl(p, 1.5f, 1.2f, 0.9f, 12, SOOT, HAZARD);
    prop_model base_m = pm_finish(p);
    p = pm_new(72u);
    pm_layer(p, PL_MAIN);
    pm_cyl(p, 1.0f, 0.9f, 1.4f, 12, HAZARD, rgb(0.9f, 0.55f, 0.05f));
    pm_push(p), pm_move(p, 0, 1.4f, 0), pm_ball(p, 0.8f, 0.8f, 0.8f, 1, 0, STEEL_D, STEEL), pm_pop(p);
    prop_model turret_m = pm_finish(p);
    p = pm_new(73u);
    pm_layer(p, PL_MAIN);
    pm_box(p, 0.7f, 3.4f, 0.8f, rgb(0.9f, 0.55f, 0.05f), HAZARD); /* upper arm, length along y */
    pm_push(p), pm_move(p, 0, 3.4f, 0), pm_ball(p, 0.6f, 0.6f, 0.6f, 1, 0, STEEL_D, STEEL), pm_pop(p);
    prop_model arm_m = pm_finish(p);
    p = pm_new(74u);
    pm_layer(p, PL_MAIN);
    pm_box(p, 0.5f, 3.0f, 0.6f, HAZARD, rgb(0.9f, 0.55f, 0.05f));
    pm_layer(p, PL_METAL);
    pm_rod(p, V3(0, 3.0f, 0), V3(0, 3.9f, 0.5f), 0.14f, 6, STEEL);
    pm_layer(p, PL_GLOW);
    pm_push(p), pm_move(p, 0, 3.92f, 0.52f), pm_box_c(p, 0.2f, 0.2f, 0.2f, glow(rgb(0.7f, 0.85f, 1.0f), 5.0f)), pm_pop(p);
    prop_model fore_m = pm_finish(p);
    t3_object *turret = group_at(root, V3(0, 0.9f, 0));
    t3_object *arm1 = group_at(turret, V3(0, 1.4f, 0));
    t3_object *arm2 = group_at(arm1, V3(0, 3.4f, 0));
    prop_model *parts[4] = { &base_m, &turret_m, &arm_m, &fore_m };
    t3_object *hosts[4] = { root, turret, arm1, arm2 };
    t3_vec3 offs[4] = { V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0), V3(0, 0, 0) };
    for (int k = 0; k < 4; k++) {
      for (int l = 0; l < PL_COUNT; l++)
        if (parts[k]->g[l]) {
          t3_mesh *me = t3_mesh_new(parts[k]->g[l], prop_material(l, -1, (pcol){ 1, 1, 1 }, 1));
          me->base.cast_shadow = l != PL_GLOW;
          t3_object_set_position(me, offs[k].x, offs[k].y, offs[k].z);
          t3_object_add(hosts[k], me);
          t3_release(me);
        }
      prop_model_free(parts[k]);
    }
    R.rob[r].turret = turret, R.rob[r].arm1 = arm1, R.rob[r].arm2 = arm2;
    R.rob[r].base = rb, R.rob[r].phase = (float)r * 2.1f;
    R.rob[r].jig = c, R.rob[r].along = along;
    R.rob[r].spark = halo_at(R.dyn, R.halo_blue, c, 5.0f);
    int em = emit_new(REC_SPARKS, "WeldSparks", c, 2, 160.0f, 3.5f, 0.7f, (float)r * 1.2f);
    R.rob[r].em = em;
  }
  R.nrob = 2;
  /* a weld light that jitters (blue-white) while a robot is working */
  lamp_new(v3add(c, V3(0, 3.0f, 0)), 0xbcd8ff, 380.0f, 55.0f, 3, 90.0f);
}

/* ── a rail siding of tankers ────────────────────────────────────────────────────────────────────── */

static void sidings(float s0, float s1, float lat, uint32_t seed) {
  const track *t = R.t;
  pmc *c = pmc_new(60, seed);
  iset tk;
  iset_begin(&tk, m_tanker(seed), m_tanker(seed + 1));
  int n = 0;
  for (float s = s0; s < s1; s += 1.6f) {
    tsample a = track_at(t, s), e = track_at(t, s + 1.7f);
    float la = (lat > 0 ? 1.0f : -1.0f) * (a.hw + fabsf(lat)), le = (lat > 0 ? 1.0f : -1.0f) * (e.hw + fabsf(lat));
    t3_vec3 p0 = road_pt(s, la, 0), p1 = road_pt(s + 1.7f, le, 0);
    p0.y = terrain(p0.x, p0.z, NULL) + 0.25f, p1.y = terrain(p1.x, p1.z, NULL) + 0.25f;
    t3_vec3 d = v3norm(v3sub(p1, p0));
    t3_vec3 rr = v3norm(v3cross(V3(0, 1, 0), d));
    pm *b = pmc_at(c, p0);
    pm_push(b);
    pm_layer(b, PL_MAIN);
    pm_beam(b, v3add(p0, v3mul(rr, -1.2f)), v3add(p0, v3mul(rr, 1.2f)), 0.38f, rgb(0.28f, 0.2f, 0.16f)); /* a sleeper */
    pm_layer(b, PL_METAL);
    for (int k = -1; k <= 1; k += 2)
      pm_beam(b, v3add(v3add(p0, v3mul(rr, 0.75f * (float)k)), V3(0, 0.2f, 0)), v3add(v3add(p1, v3mul(rr, 0.75f * (float)k)), V3(0, 0.2f, 0)), 0.14f, STEEL);
    pm_pop(b);
    n++;
  }
  for (float s = s0 + 9.0f; s < s1 - 9.0f; s += 13.0f) {
    tsample a = track_at(t, s);
    float la = (lat > 0 ? 1.0f : -1.0f) * (a.hw + fabsf(lat));
    t3_vec3 p = road_pt(s, la, 0);
    p.y = terrain(p.x, p.z, NULL) + 0.25f;
    iset_add(&tk, 0, p, yaw_of(a.f), 1.0f);
    keep_add(p.x, p.z, 14);
  }
  iset_end(&tk, "Tankers", true);
  pmc_finish(c, R.root, "Rails", false, -1);
  cw_info("refinery: siding %d sleepers", n);
}

/* ── cooling towers that vent steam ──────────────────────────────────────────────────────────────── */

static void cooling_towers(void) {
  const track *t = R.t;
  prop_set *ps = ps_new(R.root, "Cooling");
  static const float S[5] = { 260.0f, 880.0f, 1450.0f, 2330.0f, 2760.0f };
  for (int i = 0; i < 5; i++) {
    float sd = (i & 1) ? 1.0f : -1.0f;
    for (float off = 120.0f; off < 260.0f; off += 20.0f) {
      tsample a = track_at(t, S[i]);
      t3_vec3 p = road_pt(S[i], sd * (a.hw + off), 0);
      float ry, gap = road_gap(t, p.x, p.z, &ry);
      float y = terrain(p.x, p.z, NULL);
      if (gap < 90.0f || y < -1.0f) continue;
      p.y = y - 0.3f;
      ps_add(ps, PK_COOLING_TOWER, p, hf((uint32_t)i, 3) * TAU, 1.2f, 700u + (uint32_t)i);
      float top = prop_default_size(PK_COOLING_TOWER) * 1.2f;
      fx_ambient_at(R.scene, FXA_STEAM, V3(p.x, p.y + top, p.z), V3(9, 1, 9), 3.5f, 520.0f);
      halo_at(R.dyn, R.halo_mat, V3(p.x, p.y + 2.0f, p.z), 24.0f);
      break;
    }
  }
  ps_finish(ps);
}

/* ── smoke columns, lit orange from below, and the glow on the horizon ───────────────────────────── */

static void smoke_columns(void) {
  const track *t = R.t;
  static const float S[8] = { 130.0f, 520.0f, 960.0f, 1300.0f, 1650.0f, 2050.0f, 2450.0f, 2800.0f };
  t3_material *sm = t3_mesh_basic_material_new(0xffffff);
  sm->vertex_colors = true;
  sm->transparent = true;
  sm->opacity = 0.9f;
  sm->depth_write = false;
  sm->side = T3_DOUBLE_SIDE;
  for (int i = 0; i < 8; i++) {
    float sd = (i & 1) ? 1.0f : -1.0f;
    tsample a = track_at(t, S[i]);
    float off = 170.0f + 120.0f * hf((uint32_t)i, 11);
    t3_vec3 base = road_pt(S[i], sd * (a.hw + off), 0);
    float gap = road_gap(t, base.x, base.z, NULL);
    if (gap < 100.0f) base = road_pt(S[i], -sd * (a.hw + off), 0);
    base.y = terrain(base.x, base.z, NULL);
    mb m = { 0 };
    int blobs = 15;
    float x = 0, z = 0;
    for (int k = 0; k < blobs; k++) {
      float t01 = (float)k / (float)(blobs - 1);
      float rad = lerpf(7.0f, 34.0f, powf(t01, 0.8f)) * (0.85f + 0.3f * hf((uint32_t)i * 31u + (uint32_t)k, 5));
      float y = 6.0f + t01 * 170.0f;
      x += (hf((uint32_t)i * 17u + (uint32_t)k, 6) - 0.2f) * 9.0f, z += (hf((uint32_t)i * 13u + (uint32_t)k, 7) - 0.5f) * 6.0f;
      /* orange where the fire lights its underside, soot higher up */
      pcol low = hot(0.55f, 0.18f, 0.06f), high = hot(0.05f, 0.045f, 0.06f);
      pcol col = pc_mix(low, high, cw_clampf(t01 * 2.2f, 0, 1));
      /* a lumpy icosa-ish ball: three rings of eight */
      int i0 = m.nv;
      for (int rI = 0; rI < 4; rI++) {
        float la = -1.2f + 2.4f * (float)rI / 3.0f;
        for (int q = 0; q < 8; q++) {
          float an = TAU * ((float)q + 0.5f * (float)(rI & 1)) / 8.0f;
          float rr2 = rad * cosf(la) * (0.85f + 0.3f * hf((uint32_t)(k * 64 + rI * 8 + q), 9u + (uint32_t)i));
          float shade = 0.7f + 0.5f * (sinf(la) * 0.5f + 0.5f);
          mb_v(&m, V3(x + rr2 * cosf(an), y + rad * 0.9f * sinf(la), z + rr2 * sinf(an)), 0, 0, pc_mul(col, shade * (rI == 0 ? 1.15f : 1.0f)));
        }
      }
      for (int rI = 0; rI < 3; rI++)
        for (int q = 0; q < 8; q++) {
          int a0 = i0 + rI * 8 + q, a1 = i0 + rI * 8 + (q + 1) % 8, b0 = i0 + (rI + 1) * 8 + q, b1 = i0 + (rI + 1) * 8 + (q + 1) % 8;
          mb_t(&m, a0, a1, b1), mb_t(&m, a0, b1, b0);
        }
    }
    put(R.dyn, mb_done(&m), sm, base, false, false);
    /* the fire at its foot: a flame and a glow */
    flame_new(v3add(base, V3(0, 0.5f, 0)), 7.0f + 4.0f * hf((uint32_t)i, 15), 16.0f + 10.0f * hf((uint32_t)i, 16), 900u + (uint32_t)i, 0, 0, true, 1.2f);
  }
  t3_release(sm);
}

static void horizon_glow(void) {
  const track *t = R.t;
  float cx = 0, cz = 0;
  for (int i = 0; i < t->n; i += 8) cx += t->s[i].p.x, cz += t->s[i].p.z;
  cx /= (float)((t->n + 7) / 8), cz /= (float)((t->n + 7) / 8);
  mb m = { 0 };
  const int N = 72;
  float rad = 1500.0f;
  float rows_y[5] = { -30, 0, 60, 190, 420 };
  float rows_k[5] = { 0.0f, 1.0f, 0.5f, 0.14f, 0.0f };
  pcol tint[5] = { hot(1, 0.3f, 0.08f), hot(1.0f, 0.34f, 0.1f), hot(0.85f, 0.2f, 0.22f), hot(0.35f, 0.12f, 0.3f), hot(0, 0, 0) };
  for (int rI = 0; rI < 5; rI++)
    for (int k = 0; k <= N; k++) {
      float an = TAU * (float)k / (float)N;
      /* bright where flames stand: every flame adds a lobe at its bearing */
      float lobe = 0.22f;
      for (int f = 0; f < R.nfl; f++) {
        t3_vec3 fp;
        t3_object *o = R.fl[f].o;
        fp = V3(o->position.x, o->position.y, o->position.z);
        float bear = atan2f(fp.z - cz, fp.x - cx), dd = fabsf(fmodf(bear - an + 9.42477796f, TAU) - 3.14159265f);
        lobe += 0.4f * R.fl[f].sy / 20.0f * expf(-dd * dd / 0.05f);
      }
      lobe = fminf(lobe, 1.6f);
      float k2 = rows_k[rI] * lobe * 2.0f;
      mb_v(&m, V3(cx + rad * cosf(an), rows_y[rI], cz + rad * sinf(an)), (float)k / N, 0, pc_mul(tint[rI], k2));
    }
  for (int rI = 0; rI < 4; rI++)
    for (int k = 0; k < N; k++) {
      int a0 = rI * (N + 1) + k, b0 = (rI + 1) * (N + 1) + k;
      mb_t(&m, a0, a0 + 1, b0 + 1), mb_t(&m, a0, b0 + 1, b0);
    }
  t3_material *gm = glow_mat(NULL, true);
  put(R.dyn, mb_done(&m), gm, V3(0, 0, 0), false, false);
  t3_release(gm);
}

/* ── the pits' landing targets, jump signage and floodlight gantries ───────────────────────────────── */

static void landing_targets(void) {
  const track *t = R.t;
  for (int g = 0; g < R.ngap; g++) {
    float s0 = (float)R.gap[g][1] + 16.0f;
    mb m = { 0 };
    pcol cc = hot(2.4f, 0.9f, 0.12f);
    for (int ring = 0; ring < 3; ring++) {
      float r0 = 2.2f + 2.1f * (float)ring, r1 = r0 + 0.5f;
      int N = 36;
      int i0 = m.nv;
      for (int k = 0; k <= N; k++) {
        float an = TAU * (float)k / (float)N;
        float lat0 = r0 * cosf(an), ds0 = r0 * sinf(an), lat1 = r1 * cosf(an), ds1 = r1 * sinf(an);
        mb_v(&m, road_pt(s0 + ds0, lat0, 0.1f), 0, 0, cc), mb_v(&m, road_pt(s0 + ds1, lat1, 0.1f), 0, 0, cc);
      }
      for (int k = 0; k < N; k++) mb_t(&m, i0 + k * 2, i0 + k * 2 + 1, i0 + k * 2 + 3), mb_t(&m, i0 + k * 2, i0 + k * 2 + 3, i0 + k * 2 + 2);
    }
    t3_material *tm = glow_mat(NULL, true);
    put(R.dyn, mb_done(&m), tm, V3(0, 0, 0), false, false);
    R.tgt[g] = tm;
  }
  (void)t;
}

/* a gantry of floodlights over the verge: a post and a bar of lamps aimed at the road, a halo on the bar */
static void floodlights(void) {
  const track *t = R.t;
  pmc *c = pmc_new(60, 811u);
  int n = 0;
  for (float s = 60; s < t->length - 40; s += 150 + (float)((int)s % 40)) {
    if (near_special(s, 20)) continue;
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_TUNNEL | TF_LOOP)) continue;
    for (int side = -1; side <= 1; side += 2) {
      if ((n + (side > 0)) & 1) continue;
      t3_vec3 p = road_pt(s, (float)side * (a.hw + 5.0f), 0);
      p.y = fminf(terrain(p.x, p.z, NULL), p.y) - 0.2f;
      pm *b = pmc_at(c, p);
      pm_push(b);
      t3_vec3 rr = v3norm(V3(a.r.x, 0, a.r.z)), ff = v3norm(V3(a.f.x, 0, a.f.z));
      pm_basis(b, p, rr, V3(0, 1, 0), ff);
      float h = 20.0f;
      pm_layer(b, PL_METAL);
      pm_rod(b, V3(0, 0, 0), V3(0, h, 0), 0.35f, 8, STEEL);
      pm_beam(b, V3(-(float)side * 0.0f - 3.4f, h, 0), V3(3.4f, h, 0), 0.25f, STEEL_D);
      pm_layer(b, PL_GLOW);
      for (int k = -2; k <= 2; k++) pm_push(b), pm_move(b, (float)k * 1.3f, h - 0.4f, 0), pm_box_c(b, 1.0f, 0.5f, 0.7f, glow(rgb(1, 0.8f, 0.55f), 4.2f)), pm_pop(b);
      pm_pop(b);
      halo_at(R.dyn, R.halo_amber, V3(p.x, p.y + h - 0.4f, p.z), 16.0f);
    }
    n++;
  }
  pmc_finish(c, R.root, "Floods", true, -1);
}

/* steam jets that puff across the road on a rhythm, each with an amber lamp that lights just before */
static void steam_jets(void) {
  const track *t = R.t;
  pmc *c = pmc_new(60, 901u);
  float last = -1000;
  int n = 0;
  for (float s = 200; s < t->length - 100 && n < MAXJET; s += 10) {
    if (s - last < 190 || near_special(s, 45)) continue;
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_TUNNEL | TF_LOOP | TF_BOOST) || fabsf(track_curvature(t, s - 20, 40)) > 0.002f) continue;
    last = s;
    int side = (n & 1) ? 1 : -1;
    float lat = (float)side * (a.hw + 1.7f);
    t3_vec3 p = road_pt(s, lat, 0);
    p.y -= 0.2f;
    pm *b = pmc_at(c, p);
    pm_push(b);
    pm_basis(b, p, v3norm(V3(a.r.x, 0, a.r.z)), V3(0, 1, 0), v3norm(V3(a.f.x, 0, a.f.z)));
    pm_layer(b, PL_METAL);
    pm_rod(b, V3(0, 0, 0), V3(0, 3.1f, 0), 0.3f, 8, STEEL);
    pm_rod(b, V3(0, 3.1f, 0), V3((float)-side * 0.9f, 3.1f, 0), 0.26f, 8, STEEL);
    pm_layer(b, PL_MAIN);
    pm_push(b), pm_move(b, 0, 0, 0), pm_box(b, 1.1f, 0.6f, 1.1f, SOOT, HAZARD), pm_pop(b);
    pm_push(b), pm_move(b, (float)-side * 0.9f, 3.1f, 0), pm_rotz(b, (float)side * 1.5707963f), pm_cyl(b, 0.34f, 0.5f, 0.5f, 8, STEEL_D, STEEL_D), pm_pop(b);
    pm_pop(b);
    float period = 3.6f, duty = 0.3f, phase = (float)n * 0.83f;
    int em = emit_new(REC_JET, "SteamJet", v3add(p, V3((float)-side * a.r.x * 1.3f, 3.1f, (float)-side * a.r.z * 1.3f)), 1, 200.0f, period, duty, phase);
    if (em >= 0) emit_aim(em, v3norm(V3((float)-side * a.r.x, 0.05f, (float)-side * a.r.z)));
    beacon_new(v3add(p, V3(0, 3.7f, 0)), 0.28f, 1, phase + 0.7f, period, duty + 0.7f / period);
    n++;
  }
  pmc_finish(c, R.root, "Jets", true, -1);
  cw_info("refinery: %d steam jets", n);
}

/* dark, glossy tar puddles that catch the firelight, beside the road and on its shoulders */
static void puddles(void) {
  const track *t = R.t;
  t3_material *pm_ = phys(lin(0.015f, 0.014f, 0.018f), 0.07f, 0.1f);
  pm_->vertex_colors = false;
  mb m = { 0 };
  int n = 0;
  for (float s = 15; s < t->length - 10; s += 23.0f) {
    uint32_t h = hh((uint32_t)s, 211u);
    if (hf(h, 1) > 0.5f) continue;
    tsample a = track_at(t, s);
    if (a.flags & (TF_GAP | TF_TUNNEL | TF_LOOP)) continue;
    float side = hf(h, 2) < 0.5f ? -1.0f : 1.0f;
    float lat = side * (a.hw + 3.5f + 9.0f * hf(h, 3));
    t3_vec3 p = road_pt(s, lat, 0);
    p.y = terrain(p.x, p.z, NULL);
    if (p.y < -0.5f && fabsf(p.y - a.p.y) > 0.5f) continue;
    float rad = 1.5f + 3.5f * hf(h, 4);
    int i0 = mb_v(&m, V3(p.x, p.y + 0.04f, p.z), 0, 0, hot(1, 1, 1));
    const int K = 14;
    for (int k = 0; k <= K; k++) {
      float an = TAU * (float)k / K, rr = rad * (0.75f + 0.5f * hf(h * 3u + (uint32_t)(k % K), 6));
      mb_v(&m, V3(p.x + rr * cosf(an), p.y + 0.04f, p.z + rr * sinf(an) * 0.7f), 0, 0, hot(1, 1, 1));
    }
    for (int k = 0; k < K; k++) mb_t(&m, i0, i0 + 2 + k, i0 + 1 + k);
    n++;
  }
  put(R.dyn, mb_done(&m), pm_, V3(0, 0, 0), false, true);
  t3_release(pm_);
  cw_info("refinery: %d puddles", n);
}

/* shallow pools of molten slag on the ground near the road (the ground has a basin for each) */
static void slag_pools(void) {
  /* chosen before the ground is built, so the terrain can carve the basins: see pick_pools */
  for (int i = 0; i < R.npool; i++) {
    float cx = R.pool[i].x, cz = R.pool[i].z, r = R.pool[i].r;
    float rim = -0.12f;
    mb m = { 0 };
    int i0 = mb_v(&m, V3(0, 0, 0), 0.5f, 0.5f, hot(1, 1, 1));
    const int K = 20;
    for (int k = 0; k <= K; k++) {
      float an = TAU * (float)k / K, rr = r * (0.72f + 0.2f * hf((uint32_t)(i * 31 + k % K), 3));
      mb_v(&m, V3(rr * cosf(an), 0, rr * sinf(an)), 0.5f + rr * cosf(an) / 11.0f, 0.5f + rr * sinf(an) / 11.0f, hot(1, 1, 1));
    }
    for (int k = 0; k < K; k++) mb_t(&m, i0, i0 + 1 + k, i0 + 2 + k);
    (void)rim;
    float gy = terrain(cx + r * 1.4f, cz, NULL);
    (void)gy;
    put(R.dyn, mb_done(&m), R.lava_mat[1], V3(cx, terrain(cx, cz, NULL) + 0.28f, cz), false, false);
    halo_at(R.dyn, R.halo_mat, V3(cx, terrain(cx, cz, NULL) + 1.0f, cz), r * 2.6f);
  }
}
static void pick_pools(void) {
  const track *t = R.t;
  static const float S[8] = { 205.0f, 640.0f, 1010.0f, 1380.0f, 1560.0f, 2250.0f, 2620.0f, 2880.0f };
  for (int i = 0; i < 8; i++) {
    tsample a = track_at(t, S[i]);
    float sd = (i % 3 == 0) ? 1.0f : -1.0f;
    float off = 14.0f + 20.0f * hf((uint32_t)i, 8);
    t3_vec3 p = road_pt(S[i], sd * (a.hw + off), 0);
    float gap = road_gap(t, p.x, p.z, NULL);
    if (gap < 9.0f) continue;
    R.pool[R.npool++] = (pool){ p.x, p.z, 5.0f + 5.0f * hf((uint32_t)i, 9) };
  }
}

/* ── flare stacks out in the plant, each with a live flame ────────────────────────────────────────── */

static void far_flares(void) {
  const track *t = R.t;
  iset mast;
  iset_begin(&mast, m_mast(60, 12u, 0), m_mast(60, 12u, 1));
  int n = 0;
  for (float s = 70.0f; s < t->length - 60.0f && n < 14; s += 215.0f + 40.0f * hf((uint32_t)s, 4)) {
    tsample a = track_at(t, s);
    float sd = (n & 1) ? 1.0f : -1.0f;
    for (float off = 95.0f + 40.0f * hf((uint32_t)n, 9); off < 380.0f; off += 25.0f) {
      t3_vec3 p = road_pt(s, sd * (a.hw + off), 0);
      float gap = road_gap(t, p.x, p.z, NULL), y = terrain(p.x, p.z, NULL);
      if (gap < 70.0f || y < -1.0f || in_pit(p.x, p.z, 40.0f)) continue;
      float sc = 0.8f + 0.6f * hf((uint32_t)n, 5);
      p.y = y - 0.3f;
      iset_add(&mast, gap > 220.0f ? 1 : 0, p, hf((uint32_t)n, 2) * TAU, sc);
      float tipy = 60.0f * 1.1f * sc;
      flame_new(v3add(p, V3(0, tipy, 0)), 6.0f * sc, 20.0f * sc, 1200u + (uint32_t)n, 0, 0, true, 1.1f);
      n++;
      break;
    }
  }
  iset_end(&mast, "FlareMast", false);
  cw_info("refinery: %d flare stacks", n);
}

/* a development aid: RACER_REFINERY_OFF=yards,pipes,... leaves those parts out of a run (to find what costs draws) */
static bool off(const char *part) {
  const char *e = getenv("RACER_REFINERY_OFF");
  return e && strstr(e, part);
}

static void scenery(cw_node scene, const track *t, uint32_t seed) {
  memset(&R, 0, sizeof R);
  R.t = t, R.scene = scene, R.seed = seed;
  R.root = cw_node_new("Node3D", "Refinery");
  cw_node_add_child(scene, R.root);
  t3_object *g = t3_group_new();
  hang(R.root, "Dyn", g);
  R.dyn = g;
  t3_release(g);
  materials();
  /* where the set pieces fall */
  for (int i = 0, in = 0, a = 0; i <= t->n; i++) {
    bool now = i < t->n && (t->s[i].flags & TF_TUNNEL);
    if (now && !in) a = i;
    if (!now && in && R.ntun < 4) R.tun[R.ntun][0] = a, R.tun[R.ntun][1] = i - 1, R.ntun++;
    in = now;
  }
  for (int g2 = 0; g2 < t->ngaps && g2 < MAXGAP; g2++) {
    int a = t->gap_from[g2], b = t->gap_to[g2], m = (a + b) / 2;
    tsample c = track_at(t, (float)m);
    R.gap[g2][0] = a, R.gap[g2][1] = b;
    R.pit_c[g2] = c.p;
    R.pit_f[g2] = v3norm(V3(c.f.x, 0, c.f.z)), R.pit_r[g2] = v3norm(V3(c.r.x, 0, c.r.z));
    R.pit_hl[g2] = (float)(b - a) * 0.5f + 3.0f, R.pit_hw[g2] = c.hw + 7.0f;
    R.pit_floor[g2] = -9.0f;
    R.ngap++;
  }
  cw_info("refinery: %d tunnels, %d gaps", R.ntun, R.ngap);
  for (int i = 0; i < R.ntun; i++) cw_info("refinery: tunnel %d..%d", R.tun[i][0], R.tun[i][1]);
  for (int i = 0; i < R.ngap; i++) cw_info("refinery: gap %d..%d", R.gap[i][0], R.gap[i][1]);
  pick_pools();

  ground_opts go = ground_defaults();
  go.height = terrain;
  go.tex = TEX_GROUND_ROCK;
  go.tex_repeat = 7;
  go.tint = rgb(0.74f, 0.6f, 0.54f);
  go.tint2 = rgb(0.5f, 0.36f, 0.33f);
  go.patch_scale = 50;
  go.follow_road = false;
  go.darken_edge = 0.35f;
  go.seed = 9;
  if (!off("ground")) ground_plane(R.root, t, &go);

  dress_style st = dress_defaults();
  st.main = rgb(0.5f, 0.52f, 0.56f);
  st.accent = rgb(0.95f, 0.72f, 0.08f);
  st.lamp = rgb(1.0f, 0.62f, 0.2f);
  dress_start_line(R.root, t, &st);
  dress_checkpoints(R.root, t, &st);
  deco_jump_marks(R.root, t, &st);
  if (!off("deco")) {
    deco_supports(R.root, t, SUP_TRUSS, terrain, NULL, 0, 2.0f, &st);
    deco_lights(R.root, t, 110, LAMP_POLE, 0, 0, &st);
    deco_lights(R.root, t, 48, LAMP_GLOW_POST, 0, 0, &st);
    deco_corner_arrows(R.root, t, 0.012f, &st);
    deco_corner_signs(R.root, t, 0.012f, &st);
  }
  start_stack();
  for (int i = 0; i < R.ntun; i++) deco_sign(R.root, t, (float)R.tun[i][0] - 50.0f, i & 1 ? 1 : -1, SIGN_WARN, &st);
  for (int g2 = 0; g2 < R.ngap; g2++) {
    deco_sign(R.root, t, (float)R.gap[g2][0] - 85.0f, -1, SIGN_WARN, &st);
    deco_sign(R.root, t, (float)R.gap[g2][0] - 85.0f, 1, SIGN_WARN, &st);
  }
  lamp_new(road_pt(6, 0, 10.0f), 0xffb070, 420.0f, 70.0f, 1, 150.0f);
  for (int g2 = 0; g2 < R.ngap; g2++) pit_build(g2);
  if (R.ntun > 0) tunnel_tanks(0), tunnel_lights(0, 26.0f, 90.0f, 0xff9a50);
  if (R.ntun > 1) tunnel_pipes(1), tunnel_lights(1, 25.0f, 90.0f, 0xff7040);
  lightning_tower();
  robots();
  if (R.ngap > 0) sidings((float)R.gap[0][1] + 40.0f, (float)R.gap[0][1] + 190.0f, -40.0f, 331u);
  if (!off("yards")) yards();
  if (!off("roadside")) roadside();
  if (!off("pipes")) pipe_runs();
  if (!off("racks")) racks();
  if (!off("flares")) far_flares();
  if (!off("cooling")) cooling_towers();
  if (!off("smoke")) smoke_columns();
  landing_targets();
  if (!off("floods")) floodlights();
  if (!off("jets")) steam_jets();
  if (!off("puddles")) puddles();
  slag_pools();
  for (int i = 0; i < R.nbpos; i++) beacon_new(R.bpos[i], 0.9f, 0, hf((uint32_t)i, 4) * 1.6f, 1.6f, 0.3f);
  horizon_glow();
  /* embers drifting round every camera, and a haze of ash */
  fx_ambient_follow(scene, FXA_EMBERS, 1.6f);
  cw_info("refinery: %d flames, %d lights, %d emitters, %d beacons", R.nfl, R.nlt, R.nem, R.nbc);
}

/* nearest kart to a point (a kart is any of the karts in the race: cameras follow them) */
static float near_kart(t3_vec3 p) {
  float best = 1e9f;
  for (int i = 0; i < R.nk; i++) {
    float dx = R.kp[i].x - p.x, dy = R.kp[i].y - p.y, dz = R.kp[i].z - p.z, d = dx * dx + dy * dy + dz * dz;
    if (d < best) best = d;
  }
  return sqrtf(best);
}

static void tick_state(const sim *s) {
  R.nk = 0;
  for (int i = 0; i < MAXK; i++)
    if (s->k[i].on) R.kp[R.nk++] = s->k[i].p;
  if (R.nk == 0) return;
  for (int i = 0; i < R.nlt; i++) {
    lamp *l = &R.lt[i];
    float d = near_kart(l->at);
    l->gate = 1.0f - sst(l->range_on * 0.6f, l->range_on, d);
  }
  for (int i = 0; i < R.nem; i++) {
    emitter *e = &R.em[i];
    float d = near_kart(e->at);
    bool near = e->on ? d < e->range * 1.1f : d < e->range;
    bool want = near;
    if (near && e->kind != 0) {
      float ph = fmodf(R.time + e->phase, e->period);
      want = ph < e->period * e->duty;
    }
    if (want != e->on) {
      e->on = want;
      cw_node_set(e->n, "visible", cw_val_bool(want));
      cw_node_set(e->n, "emitting", cw_val_bool(want));
    }
  }
}

/* two-link reach in a robot's vertical plane: the angles of its upper arm and forearm (from vertical, toward the work) */
static void scenery_tick(const sim *s, int ticks) {
  (void)ticks;
  tick_state(s);
}

static void reach2(float d, float h, float L1, float L2, float *a1, float *a2) {
  float D = sqrtf(d * d + h * h);
  D = cw_clampf(D, fabsf(L1 - L2) + 0.15f, L1 + L2 - 0.05f);
  float phi = atan2f(h, d);
  float al = acosf(cw_clampf((L1 * L1 + D * D - L2 * L2) / (2 * L1 * D), -1, 1));
  float be = acosf(cw_clampf((L2 * L2 + D * D - L1 * L1) / (2 * L2 * D), -1, 1));
  float up = phi + al, fo = phi - be;
  *a1 = 1.5707963f - up;
  *a2 = (1.5707963f - fo) - *a1;
}

static cw_node find_node_type(cw_node n, const char *type) {
  if (cw_node_is(n, type)) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_node_type(c, type);
    if (cw_node_valid(r)) return r;
  }
}

/* a development aid: RACER_REFINERY_CAM=x,y,z,tx,ty,tz holds the first camera at a point looking at another, so a
 * set piece can be looked at from anywhere (a run puts a kart where it is wanted with RACER_TEST_AT as usual) */
static void dev_camera(void) {
  static int on = -1;
  static float v[6];
  if (on < 0) {
    const char *e = getenv("RACER_REFINERY_CAM");
    on = e && sscanf(e, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6;
  }
  if (!on) return;
  cw_node cam = find_node_type(R.scene, "Camera3D");
  if (!cw_node_valid(cam)) return;
  cw_node3d_set_position(cam, V3(v[0], v[1], v[2]));
  cw_node3d_look_at(cam, V3(v[3], v[4], v[5]), V3(0, 1, 0));
  cw_node3d_snapshot(cam);
}

static void scenery_frame(float alpha, double time) {
  (void)alpha;
  R.time = (float)time;
  float t = R.time;
  dev_camera();
  /* world.c does not call scenery_tick, so the gating runs here, a few times a second, from the sim the app holds */
  if (((int)(t * 60.0f + 0.5f)) % 4 == 0 && game_loaded()) tick_state(game_sim());
  for (int i = 0; i < R.nfl; i++) {
    flame *f = &R.fl[i];
    float sp = f->speed;
    float sy = 1.0f + 0.16f * sinf(t * sp * 5.1f + f->phase) + 0.09f * sinf(t * sp * 11.3f + f->phase * 2.0f) + 0.05f * sinf(t * 23.0f + f->phase * 5.0f);
    float sx = 1.0f + 0.1f * sinf(t * sp * 7.7f + f->phase * 1.7f);
    t3_object_set_scale(f->o, sx, sy, sx);
    t3_object_set_rotation(f->o, 0.06f * sinf(t * 1.9f * sp + f->phase), t * 0.6f * sp + f->phase, 0.06f * sinf(t * 1.3f * sp + f->phase * 2.0f));
  }
  for (int i = 0; i < 3; i++) R.flame_tex[i]->offset = (t3_vec2){ 0.0f, -fmodf(t * (0.55f + 0.3f * (float)i), 1.0f) };
  for (int i = 0; i < 2; i++) {
    R.lava_tex[i]->offset = (t3_vec2){ fmodf(t * 0.006f * (float)(i + 1), 1.0f), fmodf(t * 0.011f, 1.0f) };
    R.lava_mat[i]->emissive_intensity = 2.0f + 0.5f * sinf(t * 1.1f + (float)i * 2.0f);
  }
  /* lightning: a flash every few seconds, three quick strokes, a different bolt each time */
  {
    const float cyc = 3.3f;
    int slot = (int)floorf(t / cyc);
    float ph = t - (float)slot * cyc;
    uint32_t h = hh((uint32_t)slot, 99u);
    bool on = (ph < 0.07f) || (ph > 0.12f && ph < 0.17f) || (ph > 0.23f && ph < 0.33f);
    if (hf(h, 2) < 0.18f) on = false;
    int v = (int)(h % 5u);
    for (int i = 0; i < 5; i++)
      if (R.arc[i]) R.arc[i]->visible = on && i == v;
    R.flash = on ? 0.65f + 0.35f * hf(h, (uint32_t)(ph * 60.0f)) : 0.0f;
    if (R.arc_glow) {
      T3_OBJ(R.arc_glow)->visible = on;
      t3_object_set_scale(R.arc_glow, 40.0f + 40.0f * R.flash, 40.0f + 40.0f * R.flash, 1);
    }
  }
  for (int i = 0; i < R.nlt; i++) {
    lamp *l = &R.lt[i];
    float k = 1.0f;
    if (l->kind == 0) k = 0.84f + 0.16f * sinf(t * 11.0f + l->phase) + 0.07f * sinf(t * 27.0f + l->phase * 3.0f) + 0.04f * sinf(t * 53.0f + l->phase);
    else if (l->kind == 2) k = R.flash;
    else if (l->kind == 3) {
      float on = fmodf(t, 3.5f) < 3.5f * 0.7f ? 1.0f : 0.0f;
      k = on * (0.6f + 0.4f * sinf(t * 61.0f) * sinf(t * 37.0f));
    }
    l->l->intensity = l->base * l->gate * k;
    l->l->base.visible = l->gate > 0.01f && (l->kind != 2 || R.flash > 0.0f) && (l->kind != 3 || k > 0.0f); /* a dark light costs nothing */
  }
  for (int i = 0; i < R.nbc; i++) {
    beacon *b = &R.bc[i];
    bool on = fmodf(t + b->phase, b->period) < b->period * b->duty;
    float v = on ? 1.0f : 0.05f;
    b->m->color = (t3_color){ v, v, v };
    b->halo->color = (t3_color){ b->hb[0] * v, b->hb[1] * v, b->hb[2] * v };
  }
  for (int g = 0; g < R.ngap; g++)
    if (R.tgt[g]) {
      float v = 0.55f + 0.45f * sinf(t * 5.0f - (float)g);
      R.tgt[g]->color = (t3_color){ v, v, v };
    }
  /* the welding robots: each reaches along the seam on the piece, the torch flickers while the arc is struck */
  for (int r = 0; r < R.nrob; r++) {
    float wp = t * 0.45f + R.rob[r].phase;
    float along = sinf(wp) * 5.0f;
    t3_vec3 tgt = v3add(v3add(R.rob[r].jig, v3mul(R.rob[r].along, along)), V3(0, 2.95f, 0));
    t3_vec3 rb = R.rob[r].base, fwd = R.rob[r].fwd;
    t3_vec3 rel = V3(tgt.x - rb.x, 0, tgt.z - rb.z);
    t3_vec3 perp = V3(fwd.z, 0, -fwd.x);
    float fo = v3dot(rel, fwd), la = v3dot(rel, perp);
    float yaw = atan2f(la, fo), reach = sqrtf(fo * fo + la * la);
    float a1, a2;
    reach2(reach, tgt.y - (rb.y + 0.9f + 1.4f), 3.4f, 3.9f, &a1, &a2);
    t3_object_set_rotation(R.rob[r].turret, 0, yaw, 0);
    t3_object_set_rotation(R.rob[r].arm1, a1, 0, 0);
    t3_object_set_rotation(R.rob[r].arm2, a2, 0, 0);
    bool welding = fmodf(t + (float)r * 1.2f, 3.5f) < 3.5f * 0.7f;
    float f = welding ? 0.7f + 0.3f * sinf(t * 70.0f + (float)r) : 0.0f;
    t3_object_set_position(R.rob[r].spark, tgt.x, tgt.y + 0.2f, tgt.z);
    t3_object_set_scale(R.rob[r].spark, 2.0f + 2.2f * f, 2.0f + 2.2f * f, 1);
    T3_OBJ(R.rob[r].spark)->visible = welding;
    if (R.rob[r].em >= 0) cw_node3d_set_position(R.em[R.rob[r].em].n, V3(tgt.x, tgt.y + 0.3f, tgt.z)), R.em[R.rob[r].em].at = tgt;
  }
}
