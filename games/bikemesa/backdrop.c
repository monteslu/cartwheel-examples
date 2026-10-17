/* BikeMesa's distant land: a Terrain3D (cartwheel/terrain.h) behind the bank
 * for the themes whose landforms read best as real ground, in place of
 * view.c's lathed mesas and cones:
 *
 *   canyon   terraced red-rock mesas and buttes over a scrubby desert floor
 *   alpine   ridged snowy mountains, rock on the steep faces, pines below
 *   moon     grey regolith pocked with craters of every size, boulders
 *   mars     eroded red mesas, a few craters, rocks
 *
 * The heights are made here from the track data: right behind the bank they
 * carry on from its far edge (the bank hides the seam), then the theme's
 * land rises out of the smoothed downhill line, so the backdrop falls away
 * with the track. Everything is integer hashing and + - * / (sqrtf is
 * correctly rounded): the same land on every host. Render only: nothing
 * here touches the simulation, and the crash world never reaches this far
 * back (the bank and its rocks, crash.c, are where a tumble ends).
 *
 * The terrain shades and thins its scatter with the graphics settings
 * (effects detail, cw_quality_scale("scatter")). */
#include <stdlib.h>
#include <string.h>

#include "cartwheel/terrain.h"
#include "game.h"

static uint32_t hh(int x, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float h01(int x, int z, uint32_t seed) { return (float)(hh(x, z, seed) & 0xffffff) / 16777216.0f; }
static float sstep(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static float vnoise(float x, float z, uint32_t seed) {
  float fx0 = floorf(x), fz0 = floorf(z);
  int x0 = (int)fx0, z0 = (int)fz0;
  float fx = x - fx0, fz = z - fz0;
  fx = fx * fx * (3 - 2 * fx);
  fz = fz * fz * (3 - 2 * fz);
  float a = h01(x0, z0, seed), b = h01(x0 + 1, z0, seed), c = h01(x0, z0 + 1, seed), d = h01(x0 + 1, z0 + 1, seed);
  return a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz;
}
/* fBm, 0..1, features of `feature` metres */
static float fbm(float x, float z, float feature, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0, f = 1 / feature;
  for (int o = 0; o < oct; o++) {
    s += amp * vnoise(x * f, z * f, seed + (uint32_t)o * 101u);
    norm += amp;
    amp *= 0.5f;
    f *= 2.03f;
  }
  return s / norm;
}
/* ridged fBm, 0..1: sharp crests (mountain ridges) */
static float ridged(float x, float z, float feature, int oct, uint32_t seed) {
  float s = 0, amp = 0.5f, norm = 0, f = 1 / feature, w = 1;
  for (int o = 0; o < oct; o++) {
    float n = 1 - fabsf(vnoise(x * f, z * f, seed + (uint32_t)o * 131u) * 2 - 1);
    n *= n;
    s += amp * n * w;
    w = n * 1.6f;
    w = w > 1 ? 1 : w;
    norm += amp;
    amp *= 0.5f;
    f *= 2.07f;
  }
  return s / norm;
}

/* craters on a grid of `cell` metres: one in a share `p` of the cells, radius
 * r0..r1, a bowl `depth` x r deep with a raised rim */
static float craters(float x, float z, float cell, float p, float r0, float r1, uint32_t seed) {
  float s = 0;
  int cx = (int)floorf(x / cell), cz = (int)floorf(z / cell);
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      int gx = cx + i, gz = cz + j;
      if (h01(gx, gz, seed) > p) continue;
      float r = r0 + (r1 - r0) * h01(gx, gz, seed + 3);
      float ox = (gx + 0.2f + 0.6f * h01(gx, gz, seed + 1)) * cell, oz = (gz + 0.2f + 0.6f * h01(gx, gz, seed + 2)) * cell;
      float dx = x - ox, dz = z - oz, dd = dx * dx + dz * dz;
      if (dd > r * r * 2.6f) continue;
      float q = sqrtf(dd) / r, rim = 0.07f * r, depth = 0.24f * r;
      s += q < 1 ? rim + (q * q - 1) * depth : rim * (1 - sstep(0, 0.6f, q - 1));
    }
  return s;
}

enum { GEN_MESAS, GEN_PEAKS, GEN_CRATERS, GEN_RED };
typedef struct layer_look { int tex; float tint[3]; float uv; } layer_look;
typedef struct bd_theme {
  const char *name;
  int gen;
  float rise; /* the bank's far rise per metre, carried on behind it */
  layer_look layer[3]; /* 0 the floor, 1 steep faces, 2 high flats (mesa tops) or a second floor */
  int scatter_shape;
  float scatter_tint[3], scatter_density, scatter_min, scatter_max;
  uint32_t scatter_layers;
  int scatter2_shape; /* -1 none */
  float scatter2_tint[3], scatter2_density, scatter2_min, scatter2_max;
} bd_theme;
static const bd_theme BD[] = {
  { "canyon", GEN_MESAS, 0.06f,
    { { TEX_DIRT, { 0.98f, 0.74f, 0.56f }, 0.12f }, { TEX_SANDSTONE, { 1, 0.6f, 0.42f }, 0.05f }, { TEX_SANDSTONE, { 0.86f, 0.5f, 0.36f }, 0.08f } },
    CW_SCATTER_ROCK, { 0.95f, 0.5f, 0.36f }, 0.004f, 1.5f, 5.0f, 1u | 4u,
    CW_SCATTER_TREE, { 0.7f, 0.75f, 0.45f }, 0.003f, 0.3f, 0.55f },
  { "alpine", GEN_PEAKS, 0.25f,
    { { TEX_SNOW, { 1, 1, 1 }, 0.1f }, { TEX_ROCK, { 0.6f, 0.62f, 0.68f }, 0.06f }, { TEX_ROCK, { 0.78f, 0.8f, 0.86f }, 0.08f } },
    CW_SCATTER_TREE, { 0.42f, 0.55f, 0.48f }, 0.014f, 0.8f, 1.5f, 1u,
    CW_SCATTER_ROCK, { 0.9f, 0.92f, 1.0f }, 0.002f, 1.5f, 4.0f },
  { "moon", GEN_CRATERS, 0.06f,
    { { TEX_SAND, { 0.62f, 0.62f, 0.64f }, 0.1f }, { TEX_ROCK, { 0.5f, 0.5f, 0.52f }, 0.06f }, { TEX_SAND, { 0.42f, 0.42f, 0.44f }, 0.07f } },
    CW_SCATTER_ROCK, { 0.42f, 0.42f, 0.44f }, 0.006f, 1.0f, 5.0f, 0,
    -1, { 0 }, 0, 0, 0 },
  { "mars", GEN_RED, 0.06f,
    { { TEX_SAND, { 0.9f, 0.52f, 0.34f }, 0.1f }, { TEX_SANDSTONE, { 0.85f, 0.45f, 0.3f }, 0.05f }, { TEX_SANDSTONE, { 0.7f, 0.36f, 0.24f }, 0.08f } },
    CW_SCATTER_ROCK, { 0.75f, 0.36f, 0.24f }, 0.005f, 1.2f, 5.0f, 1u | 4u,
    -1, { 0 }, 0, 0, 0 },
};

/* the land: 2.5 m samples from just under the bank back BACK metres, and
 * SIDE metres past each end of the track (far enough that the view never
 * reaches an edge; the farthest rows rise, so the last edge is a skyline) */
#define SPACING 2.5f
#define BANK_END (TRACK_HALF_W + 0.3f + 62.0f) /* view.c bank(): its last row */
#define Z_NEAR (-BANK_END + 10.0f)
#define BACK 720.0f
#define SIDE 420.0f

static cw_node node;
static float *wide; /* the track's height, smoothed over +-60 m: the land's base line */
static int wide_n;
static float wide_x0;
static float wide_at(float x) {
  float f = (x - wide_x0) / SPACING;
  if (f <= 0) return wide[0];
  if (f >= wide_n - 1) return wide[wide_n - 1];
  int i = (int)f;
  return wide[i] + (wide[i + 1] - wide[i]) * (f - (float)i);
}

/* the theme's land at (x, z), d metres behind the bank's last row */
static float land(const bd_theme *b, float x, float z, float d, uint32_t seed) {
  float base = wide_at(x);
  float dd = d > 500 ? 500 : d;
  switch (b->gen) {
  case GEN_MESAS:
  case GEN_RED: {
    /* mars: lone buttes over a wide cratered plain; canyon: walls of mesas */
    bool red = b->gen == GEN_RED;
    float n = fbm(x, z, red ? 120 : 150, 4, seed), k = red ? 0.07f : 0;
    float floor_ = -3 + 5 * fbm(x, z, 45, 3, seed + 1) + dd * 0.03f;
    float amp = (1 + dd / 260) * (red ? sstep(40, 140, d) : sstep(15, 70, d));
    float talus = 7 * sstep(0.40f + k, 0.49f + k, n), wall = 26 * sstep(0.49f + k, 0.515f + k, n), top = 15 * sstep(0.6f + k, 0.625f + k, n);
    float cap = 1.6f * fbm(x, z, 14, 2, seed + 2) * sstep(0.49f + k, 0.53f + k, n);
    float h = base + floor_ + amp * (talus + wall + top + cap);
    if (red) h += craters(x, z, 70, 0.5f, 8, 26, seed + 7) * (1 - sstep(0.5f, 0.56f, n));
    return h;
  }
  case GEN_PEAKS: {
    float r = ridged(x, z, 240, 5, seed);
    float amp = 20 + dd * 0.3f;
    float foot = 6 * fbm(x, z, 60, 3, seed + 1);
    return base + 8 + dd * 0.12f + foot + sstep(10, 90, d) * amp * r;
  }
  default: { /* GEN_CRATERS */
    /* the maria's plain, then the highlands' worn ridges far back */
    float h = base + dd * 0.05f + 5 * fbm(x, z, 140, 3, seed) + 1.2f * fbm(x, z, 22, 2, seed + 1);
    h += sstep(120, 420, d) * (25 + dd * 0.12f) * sstep(0.25f, 0.8f, ridged(x, z, 300, 4, seed + 20));
    h += craters(x, z, 120, 0.7f, 16, 44, seed + 11) + craters(x, z, 42, 0.6f, 5, 13, seed + 12);
    if (d < 260) h += craters(x, z, 16, 0.45f, 1.5f, 4.5f, seed + 13) * (1 - sstep(160, 260, d));
    return h;
  }
  }
}

static void pack_layer(cw_terrain *tr, int i, const layer_look *l) {
  const tex_set *s = tex_get(l->tex);
  int w = s->map->width, h = s->map->height;
  const uint8_t *a = s->map->pixels, *n = s->normal->pixels, *ro = s->rough->pixels, *ao = s->ao->pixels;
  uint8_t *alb = malloc((size_t)w * h * 4), *nr = malloc((size_t)w * h * 4);
  for (int p = 0; p < w * h; p++) {
    for (int k = 0; k < 3; k++) {
      float v = a[p * 4 + k] * l->tint[k];
      alb[p * 4 + k] = (uint8_t)(v > 255 ? 255 : v + 0.5f);
      nr[p * 4 + k] = n[p * 4 + k];
    }
    alb[p * 4 + 3] = ao[p * 4]; /* the occlusion stands in for height (the crevices are low) */
    nr[p * 4 + 3] = ro[p * 4];
  }
  cw_terrain_set_layer_pixels(tr, i, alb, nr, w, h, l->uv);
  free(alb);
  free(nr);
}

static void scatter(cw_terrain *tr, int shape, const float tint[3], float density, float smin, float smax, uint32_t layers,
                    const uint8_t *dm, int nx, int nz, uint32_t seed) {
  cw_scatter_def sd = cw_scatter_default_def();
  cw_scatter_def_builtin(&sd, (cw_scatter_shape)shape, tint[0], tint[1], tint[2]);
  sd.density = density;
  sd.layers = layers;
  sd.density_map = dm;
  sd.dm_w = nx;
  sd.dm_h = nz;
  sd.min_slope_cos = shape == CW_SCATTER_TREE ? 0.88f : 0.75f;
  sd.scale_min = smin;
  sd.scale_max = smax;
  sd.align_to_normal = shape != CW_SCATTER_TREE;
  sd.sink = shape == CW_SCATTER_ROCK ? 0.25f : 0.1f;
  sd.visible_range = shape == CW_SCATTER_TREE ? 420 : 300;
  sd.fade = 60;
  sd.lod_distance[0] = 90;
  sd.cast_shadow = false;
  sd.density_knob = "scatter";
  sd.seed = seed;
  cw_terrain_scatter_add(tr, &sd);
  cw_scatter_def_release(&sd);
}

bool backdrop_build(cw_node parent, const track_desc *t, int track_index) {
  const bd_theme *b = NULL;
  for (int i = 0; i < (int)CW_ARRAY_LEN(BD); i++)
    if (!strcmp(BD[i].name, t->theme)) b = &BD[i];
  if (!b) return false;
  uint32_t seed = (uint32_t)(track_index + 1) * 2246822519u + 77u;

  float x0 = -SIDE, x1 = t->length + SIDE;
  int nxq = ((int)((x1 - x0) / SPACING) + 31) / 32 * 32, nzq = ((int)(BACK / SPACING) + 31) / 32 * 32;
  int nx = nxq + 1, nz = nzq + 1;
  float zfar = Z_NEAR - (float)nzq * SPACING;

  /* the base line: the track's height (the bank's ground) smoothed wide */
  free(wide);
  wide_x0 = x0;
  wide_n = nx;
  wide = malloc(sizeof(float) * (size_t)nx);
  float *tmp = malloc(sizeof(float) * (size_t)nx);
  for (int i = 0; i < nx; i++) {
    float x = x0 + (float)i * SPACING;
    tmp[i] = view_ground_at(x < 0 ? 0 : x > t->length ? t->length : x);
  }
  int rad = (int)(60 / SPACING);
  for (int i = 0; i < nx; i++) {
    float s = 0;
    int n = 0;
    for (int k = -rad; k <= rad; k++)
      if (i + k >= 0 && i + k < nx) s += tmp[i + k], n++;
    wide[i] = s / (float)n;
  }
  free(tmp);

  float *hm = malloc(sizeof(float) * (size_t)nx * nz);
  float lo = 1e9f, hi = -1e9f;
  for (int j = 0; j < nz; j++) {
    float z = zfar + (float)j * SPACING, d = -z - BANK_END;
    for (int i = 0; i < nx; i++) {
      float x = x0 + (float)i * SPACING, h;
      if (d <= 0) h = view_bank_y(x, z) - 1.5f; /* under the bank: hidden */
      else {
        float edge = view_bank_y(x, -BANK_END) - 0.15f + b->rise * d;
        h = edge + (land(b, x, z, d, seed) - edge) * sstep(0, 45, d);
      }
      hm[j * nx + i] = h;
      lo = h < lo ? h : lo;
      hi = h > hi ? h : hi;
    }
  }
  /* the land's height range, a little slack each way (quantized to steps of it) */
  lo -= 2;
  hi += 2;
  for (int k = 0; k < nx * nz; k++) hm[k] -= lo;

  node = cw_node_new("Terrain3D", "Backdrop");
  cw_node_set(node, "size_x", cw_val_int(nxq));
  cw_node_set(node, "size_z", cw_val_int(nzq));
  cw_node_set(node, "region_size", cw_val_int(64));
  cw_node_set(node, "vertex_spacing", cw_val_float(SPACING));
  cw_node_set(node, "min_height", cw_val_float(0));
  cw_node_set(node, "max_height", cw_val_float(hi - lo));
  cw_node_set(node, "grid_quads", cw_val_int(32));
  cw_node_set(node, "far_distance", cw_val_float(420));
  cw_node_set(node, "physics", cw_val_bool(false));
  cw_node_set(node, "cast_shadow", cw_val_bool(false));
  cw_node3d_set_position(node, (t3_vec3){ x0 + (float)nxq * SPACING * 0.5f, lo, zfar + (float)nzq * SPACING * 0.5f });
  cw_node_add_child(parent, node);
  cw_terrain *tr = cw_terrain_of(node);
  if (!tr) {
    cw_warn("bikemesa: the backdrop terrain did not build");
    free(hm);
    cw_node_free(node);
    node = (cw_node){ 0 };
    return false;
  }
  cw_terrain_set_heights_m(tr, 0, 0, nx, nz, hm);

  for (int i = 0; i < 3; i++) pack_layer(tr, i, &b->layer[i]);

  /* the control map: steep faces rock, high flats their own, the floor the rest;
   * the scatter's density map: none under the bank or near its edge */
  uint32_t *ctl = malloc(sizeof(uint32_t) * (size_t)nx * nz);
  uint8_t *dm = malloc((size_t)nx * nz);
  for (int j = 0; j < nz; j++)
    for (int i = 0; i < nx; i++) {
      int i0 = i > 0 ? i - 1 : i, i1 = i < nx - 1 ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < nz - 1 ? j + 1 : j;
      float gx = (hm[j * nx + i1] - hm[j * nx + i0]) / ((float)(i1 - i0) * SPACING);
      float gz = (hm[j1 * nx + i] - hm[j0 * nx + i]) / ((float)(j1 - j0) * SPACING);
      float ny = 1 / sqrtf(gx * gx + gz * gz + 1);
      float x = x0 + (float)i * SPACING, z = zfar + (float)j * SPACING, d = -z - BANK_END;
      float rel = hm[j * nx + i] + lo - wide_at(x); /* height over the base line */
      float w[3];
      w[1] = sstep(0.86f, 0.7f, ny);
      if (b->gen == GEN_PEAKS) w[2] = sstep(0.8f, 0.7f, ny) * 0.6f; /* scree under the faces */
      else if (b->gen == GEN_CRATERS) w[2] = sstep(0.45f, 0.62f, fbm(x, z, 180, 3, seed + 40));
      else w[2] = sstep(18, 26, rel - d * 0.03f);
      w[0] = 1 - w[1];
      w[2] *= 1 - w[1];
      w[0] = w[0] - w[2] < 0 ? 0 : w[0] - w[2];
      int b0 = 0;
      for (int k = 1; k < 3; k++)
        if (w[k] > w[b0]) b0 = k;
      int b1 = b0 == 0 ? 1 : 0;
      for (int k = 0; k < 3; k++)
        if (k != b0 && w[k] > w[b1]) b1 = k;
      float sum = w[b0] + w[b1];
      int blend = sum > 0 ? (int)(w[b1] / sum * 255) : 0;
      ctl[j * nx + i] = cw_terrain_control(b0, b1, blend, (int)(hh(i, j, seed) & 15), 0, false);
      float dens = sstep(8, 30, d);
      if (b->gen == GEN_PEAKS) dens *= 1 - sstep(45, 75, rel - d * 0.12f); /* the tree line */
      dm[j * nx + i] = (uint8_t)(dens * 255);
    }
  cw_terrain_set_control(tr, 0, 0, nx, nz, ctl);
  free(ctl);
  scatter(tr, b->scatter_shape, b->scatter_tint, b->scatter_density, b->scatter_min, b->scatter_max, b->scatter_layers, dm, nx, nz,
          seed + 1);
  if (b->scatter2_shape >= 0)
    scatter(tr, b->scatter2_shape, b->scatter2_tint, b->scatter2_density, b->scatter2_min, b->scatter2_max, 0, dm, nx, nz, seed + 2);
  free(dm);
  free(hm);
  cw_terrain_scatter_invalidate(tr);
  return true;
}

void backdrop_forget(void) {
  node = (cw_node){ 0 };
  free(wide);
  wide = NULL;
}
