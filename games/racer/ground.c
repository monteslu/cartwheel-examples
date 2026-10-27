/* racer's ground and water: a ground that follows a height function out to the horizon (finest by the road,
 * coarser away from it, skirts hiding the seams), the engine's Water3D for lakes and seas, and a film of water that
 * lies on a flooded road or runs down a river.
 *
 * Why merged chunks and not the engine's Terrain3D: that node draws a heightmap with CDLOD and scatter and
 * splatting, which is great for open worlds but needs a height image per terrain, its own extension programs and
 * (for collision) bodies we do not use: racer's road is its own mesh and the ground only has to look good and sit
 * under it. A quadtree of small merged meshes here costs a few hundred draws that frustum culling thins to a few
 * dozen, works with any height function, and grades the ground up to the road so there is never a gap or a
 * z-fight where road meets earth. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/base.h"
#include "cartwheel/math.h"
#include "cartwheel/water.h"
#include "props.h"

static float hash2(int x, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)z * 19349663u ^ seed * 83492791u;
  h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
  return (float)(h & 0xffffff) / 16777215.0f;
}
static float vnoise2(float x, float z, uint32_t seed) {
  float fx = floorf(x), fz = floorf(z);
  int ix = (int)fx, iz = (int)fz;
  float u = x - fx, v = z - fz;
  u = u * u * (3 - 2 * u), v = v * v * (3 - 2 * v);
  float a = hash2(ix, iz, seed), b = hash2(ix + 1, iz, seed), c = hash2(ix, iz + 1, seed), d = hash2(ix + 1, iz + 1, seed);
  return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

float ground_noise(float x, float z, float scale, float amp, int octaves, uint32_t seed) {
  float sum = 0, a = 1, tot = 0, f = 1 / scale;
  for (int i = 0; i < octaves; i++) {
    sum += (vnoise2(x * f, z * f, seed + (uint32_t)i * 101u) * 2 - 1) * a;
    tot += a;
    a *= 0.5f, f *= 2.03f;
  }
  return sum / tot * amp;
}

float ground_basin(float x, float z, float cx, float cz, float rx, float rz, float depth, float edge) {
  float dx = (x - cx) / rx, dz = (z - cz) / rz;
  float d = sqrtf(dx * dx + dz * dz); /* 1 at the rim */
  float e = edge / fminf(rx, rz);
  float t = 1.0f - cw_smoothstep(1.0f - e, 1.0f + 0.001f, d);
  float bowl = 1.0f - fminf(d, 1.0f) * fminf(d, 1.0f) * 0.35f;
  return -depth * t * bowl;
}

float ground_hills(const track *t, float x, float z, float amp, float scale, float flat_near, float flat_far, uint32_t seed) {
  float n = (ground_noise(x, z, scale, amp, 4, seed) + amp) * 0.5f; /* 0..amp, mostly rolling */
  float gap = road_gap(t, x, z, NULL);
  return n * cw_smoothstep(flat_near, flat_far, gap);
}

float ground_basin_poly(float x, float z, const t3_vec2 *pts, int n, float depth, float edge) {
  bool in = false;
  float best = 1e9f;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    if (((pts[i].y > z) != (pts[j].y > z)) && (x < (pts[j].x - pts[i].x) * (z - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x)) in = !in;
    float ex = pts[j].x - pts[i].x, ez = pts[j].y - pts[i].y, l2 = ex * ex + ez * ez;
    float t = l2 > 1e-9f ? cw_clampf(((x - pts[i].x) * ex + (z - pts[i].y) * ez) / l2, 0, 1) : 0;
    float dx = x - (pts[i].x + ex * t), dz = z - (pts[i].y + ez * t);
    best = fminf(best, sqrtf(dx * dx + dz * dz));
  }
  /* a bank that slopes in over `edge` metres from the shore (a little beyond it too), then a flat bed */
  float d = in ? best : -best;
  float t = cw_smoothstep(-edge * 0.4f, edge, d);
  return -depth * t;
}

ground_opts ground_defaults(void) {
  ground_opts o = { 0 };
  o.cx = o.cz = 1e9f;
  o.half = 1800;
  o.base_y = -0.05f;
  o.tex = TEX_GROUND_ROCK;
  o.tex_repeat = 8;
  o.tint = o.tint2 = rgb(0.5f, 0.45f, 0.38f);
  o.patch_scale = 60;
  o.rough = 0.95f;
  o.near_cell = 32;
  o.detail_range = 24;
  o.follow_road = true;
  o.shoulder = 6;
  o.follow_max = 4.5f;
  o.road_drop = 0.25f;
  o.darken_edge = 0.25f;
  o.lumpy = 0.25f;
  o.seed = 1;
  return o;
}

/* the ground the last ground_plane built, so scatter and dressing can ask the same questions */
static struct {
  const track *t;
  ground_opts o;
  bool have;
} G;

float road_gap_idx(const track *t, float x, float z, float *road_y, int *idx);
float road_low_y(const track *t, float x, float z, float reach);
float road_cap_y(const track *t, float x, float z, float reach);

static float base_height(const ground_opts *o, float x, float z) { return o->height ? o->height(x, z, o->height_ud) : o->base_y; }

float ground_height_at(const ground_opts *o, float x, float z) {
  if (!o) o = G.have ? &G.o : NULL;
  if (!o) return 0;
  float g = base_height(o, x, z);
  if (o->follow_road && G.have && G.t) {
    float ry;
    int idx;
    float gap = road_gap_idx(G.t, x, z, &ry, &idx);
    const tsample *a = &G.t->s[idx];
    if (gap < o->shoulder + 12.0f && (a->flags & TF_LOOP)) {
      /* round a loop or a corkscrew the earth stays under the lowest of its road: it stood up inside the tube */
      float fl = track_loop_floor(G.t, idx) - o->road_drop - 0.3f;
      if (g > fl) g = fl;
    } else if (gap < o->shoulder && !(a->flags & TF_TUNNEL)) {
      /* the road's own surface at the nearest lateral place (its edge, once past it): a banked road's outer edge is
       * higher than its middle, and the earth has to meet that */
      float lat = (x - a->p.x) * a->r.x + (z - a->p.z) * a->r.z;
      float rl = sqrtf(a->r.x * a->r.x + a->r.z * a->r.z);
      lat = rl > 1e-4f ? lat / rl : 0;
      lat = cw_clampf(lat, -a->hw, a->hw);
      float surf = a->p.y + a->r.y * lat / (rl > 1e-4f ? rl : 1.0f);
      /* a road high over the ground (a bridge, a ramp on stilts) leaves it alone; ground above the road is always cut
       * down to it, however deep (a dip in the road under higher land drew the earth over the deck) */
      if (surf - g < o->follow_max) {
        float t = 1.0f - cw_smoothstep(0.0f, o->shoulder, gap);
        g = g + (surf - o->road_drop - g) * t;
      }
    }
  }
  /* on every map, whatever shaped the land (its own height function, or following the road): the ground never stands
   * above the lowest road beside it. Where roads cross the nearest road can be the one overhead, and the land rose to
   * it and stood across the road beneath as a slab; a map's own terrain put mounds through the deck */
  if (G.have && G.t) {
    float cap = road_cap_y(G.t, x, z, 12.0f);
    if (g > cap) g = cap;
  }
  return g;
}

float ground_height_cb(float x, float z, void *ud) {
  (void)ud;
  return ground_height_at(NULL, x, z);
}

/* ── the mesh ────────────────────────────────────────────────────────────────────────────────────── */

typedef struct gmesh {
  float *p, *n, *uv, *c;
  uint32_t *ix;
  int nv, ni, capv, capi;
} gmesh;

static int gv(gmesh *m, float x, float y, float z, float nx, float ny, float nz, float u, float v, pcol c) {
  if (m->nv == m->capv) {
    m->capv = m->capv ? m->capv * 2 : 512;
    m->p = realloc(m->p, sizeof(float) * 3 * (size_t)m->capv), m->n = realloc(m->n, sizeof(float) * 3 * (size_t)m->capv);
    m->uv = realloc(m->uv, sizeof(float) * 2 * (size_t)m->capv), m->c = realloc(m->c, sizeof(float) * 3 * (size_t)m->capv);
  }
  int i = m->nv++;
  m->p[i * 3] = x, m->p[i * 3 + 1] = y, m->p[i * 3 + 2] = z;
  m->n[i * 3] = nx, m->n[i * 3 + 1] = ny, m->n[i * 3 + 2] = nz;
  m->uv[i * 2] = u, m->uv[i * 2 + 1] = v;
  m->c[i * 3] = c.r, m->c[i * 3 + 1] = c.g, m->c[i * 3 + 2] = c.b;
  return i;
}
static void gt(gmesh *m, int a, int b, int c) {
  if (m->ni + 3 > m->capi) m->capi = m->capi ? m->capi * 2 : 1536, m->ix = realloc(m->ix, sizeof(uint32_t) * (size_t)m->capi);
  m->ix[m->ni++] = (uint32_t)a, m->ix[m->ni++] = (uint32_t)b, m->ix[m->ni++] = (uint32_t)c;
}
static t3_geometry *gfinish(gmesh *m, bool uvs) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, m->p, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->n, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
  if (uvs) a = t3_attribute_new(T3_FLOAT32, m->uv, m->nv, 2), t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, m->c, m->nv, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, a), t3_release(a);
  a = t3_attribute_new(T3_UINT32, m->ix, m->ni, 1), t3_geometry_set_index(g, a), t3_release(a);
  t3_geometry_compute_bounding_sphere(g);
  free(m->p), free(m->n), free(m->uv), free(m->c), free(m->ix);
  memset(m, 0, sizeof *m);
  return g;
}

typedef struct gbuild {
  const track *t;
  const ground_opts *o;
  t3_object *group;
  t3_material *mat;
  int cells, tris;
  /* cells whose centres share a 128 m block are one mesh: a draw per cell was most of the ground's cost */
  struct { int64_t key; gmesh m; } blk[256];
  int nblk;
} gbuild;

static pcol ground_colour(const gbuild *B, float x, float z, float h, float slope, float gap) {
  const ground_opts *o = B->o;
  float patch = vnoise2(x / o->patch_scale, z / o->patch_scale, o->seed) * 0.65f + vnoise2(x / (o->patch_scale * 0.27f), z / (o->patch_scale * 0.27f), o->seed + 5) * 0.35f;
  pcol c = pc_mix(o->tint, o->tint2, cw_smoothstep(0.3f, 0.7f, patch));
  /* fine speckle so a big flat area is never a flat colour */
  float fine = vnoise2(x * 0.7f, z * 0.7f, o->seed + 9) - 0.5f;
  c = pc_mul(c, 1.0f + o->lumpy * (0.5f * (patch - 0.5f) + 0.5f * fine));
  /* steep earth shows the darker, rockier side of the mix */
  c = pc_mix(c, pc_mul(pc_mix(o->tint, o->tint2, 0.5f), 0.55f), cw_clampf((slope - 0.35f) * 1.4f, 0, 0.7f));
  /* a little shade where the ground meets the road, as if the verge were worn and dusty */
  if (o->darken_edge > 0) c = pc_mul(c, 1.0f - o->darken_edge * (1.0f - cw_smoothstep(0.0f, 9.0f, gap)));
  (void)h;
  return c;
}

static void build_cell(gbuild *B, float x0, float z0, float S, int n) {
  const ground_opts *o = B->o;
  float d = S / (float)n;
  int W = n + 3;
  float *hh = malloc(sizeof(float) * (size_t)W * (size_t)W);
  float *gp = malloc(sizeof(float) * (size_t)W * (size_t)W);
  for (int j = 0; j < W; j++)
    for (int i = 0; i < W; i++) {
      float x = x0 + (float)(i - 1) * d, z = z0 + (float)(j - 1) * d;
      hh[j * W + i] = ground_height_at(o, x, z);
      float ry;
      gp[j * W + i] = (o->darken_edge > 0 && B->t) ? road_gap(B->t, x, z, &ry) : 100.0f;
    }
  int64_t key = ((int64_t)floorf((x0 + S * 0.5f) / 128.0f) + 4096) * 100000 + (int64_t)floorf((z0 + S * 0.5f) / 128.0f) + 4096;
  int bi = 0;
  while (bi < B->nblk && B->blk[bi].key != key) bi++;
  if (bi == B->nblk) {
    if (bi == 256) bi = 255;
    else B->blk[B->nblk++].key = key;
  }
  gmesh *mp = &B->blk[bi].m;
  int base = 0, ni0 = mp->ni;
  for (int j = 0; j <= n; j++)
    for (int i = 0; i <= n; i++) {
      int k = (j + 1) * W + (i + 1);
      float x = x0 + (float)i * d, z = z0 + (float)j * d;
      float dx = (hh[k + 1] - hh[k - 1]) / (2 * d), dz = (hh[k + W] - hh[k - W]) / (2 * d);
      float l = sqrtf(dx * dx + 1 + dz * dz);
      pcol c = ground_colour(B, x, z, hh[k], sqrtf(dx * dx + dz * dz), gp[k]);
      int v = gv(mp, x, hh[k], z, -dx / l, 1 / l, -dz / l, x / o->tex_repeat, z / o->tex_repeat, c);
      if (i == 0 && j == 0) base = v;
    }
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      int a = base + j * (n + 1) + i, b = a + 1, c = a + n + 1, e = c + 1;
      /* the diagonal runs the way the surface bends least, so ridges stay crisp */
      if (fabsf(hh[(j + 1) * W + (i + 1)] - hh[(j + 2) * W + (i + 2)]) < fabsf(hh[(j + 1) * W + (i + 2)] - hh[(j + 2) * W + (i + 1)])) {
        gt(mp, a, c, e), gt(mp, a, e, b);
      } else {
        gt(mp, a, c, b), gt(mp, b, c, e);
      }
    }
  /* skirts: a strip dropped along every edge, so a seam against a coarser cell never shows sky */
  float drop = 1.5f + S * 0.012f;
  for (int side = 0; side < 4; side++) {
    int first = -1, prev = -1, prevb = -1;
    for (int s = 0; s <= n; s++) {
      int i = side == 0 ? s : side == 1 ? n : side == 2 ? n - s : 0, j = side == 0 ? 0 : side == 1 ? s : side == 2 ? n : n - s;
      int vtx = base + j * (n + 1) + i;
      float nx = side == 0 ? 0 : side == 1 ? 1 : side == 2 ? 0 : -1, nz = side == 0 ? -1 : side == 2 ? 1 : 0;
      int lo = gv(mp, mp->p[vtx * 3], mp->p[vtx * 3 + 1] - drop, mp->p[vtx * 3 + 2], nx, 0, nz, mp->uv[vtx * 2], mp->uv[vtx * 2 + 1] + 0.5f,
                  (pcol){ mp->c[vtx * 3] * 0.6f, mp->c[vtx * 3 + 1] * 0.6f, mp->c[vtx * 3 + 2] * 0.6f });
      if (first < 0) first = vtx;
      if (prev >= 0) gt(mp, prev, prevb, lo), gt(mp, prev, lo, vtx);
      prev = vtx, prevb = lo;
    }
    (void)first;
  }
  B->tris += (mp->ni - ni0) / 3;
  free(hh), free(gp);
  B->cells++;
}

static void split(gbuild *B, float x0, float z0, float S) {
  const ground_opts *o = B->o;
  float cx = x0 + S * 0.5f, cz = z0 + S * 0.5f;
  bool sub = false;
  if (S > o->near_cell * 1.01f && B->t) {
    float range = o->detail_range * (S / o->near_cell - 1.0f);
    /* any road point inside the cell (grown by the detail range) splits it. The nearest-road lookup from the cell's
     * middle gave up past ~150 m, so a big cell with a road along its edge stayed coarse and its huge triangles rose
     * over the deck (the Refinery's straight before the line) */
    float m = range + 12.0f;
    for (int i = 0; i < B->t->nall && !sub; i += 2) {
      const tsample *q = &B->t->s[i];
      sub = q->p.x > x0 - m && q->p.x < x0 + S + m && q->p.z > z0 - m && q->p.z < z0 + S + m;
    }
    (void)cx, (void)cz;
  }
  if (sub) {
    float h = S * 0.5f;
    split(B, x0, z0, h), split(B, x0 + h, z0, h), split(B, x0, z0 + h, h), split(B, x0 + h, z0 + h, h);
  } else {
    build_cell(B, x0, z0, S, 8);
  }
}

cw_node ground_plane(cw_node parent, const track *t, const ground_opts *in) {
  ground_opts o = in ? *in : ground_defaults();
  if (o.cx > 1e8f) {
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (int i = 0; i < t->n; i += 4) {
      x0 = fminf(x0, t->s[i].p.x), x1 = fmaxf(x1, t->s[i].p.x), z0 = fminf(z0, t->s[i].p.z), z1 = fmaxf(z1, t->s[i].p.z);
    }
    o.cx = (x0 + x1) * 0.5f, o.cz = (z0 + z1) * 0.5f;
  }
  if (o.near_cell < 8) o.near_cell = 64;
  G.t = t, G.o = o, G.have = true;
  /* the root: the smallest power-of-two multiple of near_cell that covers 2 * half */
  float S = o.near_cell;
  while (S < o.half * 2) S *= 2;
  /* snap to the cell grid so two builds from different centres agree */
  float x0 = floorf((o.cx - S * 0.5f) / o.near_cell) * o.near_cell, z0 = floorf((o.cz - S * 0.5f) / o.near_cell) * o.near_cell;
  t3_material *m = phys((t3_color){ 1, 1, 1 }, o.rough, 0);
  m->vertex_colors = true;
  if (o.tex >= 0) tex_apply(m, o.tex, 1, 1, 0.9f, 0.8f);
  gbuild B = { t, &G.o, t3_group_new(), m, 0, 0 };
  split(&B, x0, z0, S);
  for (int i = 0; i < B.nblk; i++) {
    t3_geometry *g = gfinish(&B.blk[i].m, true);
    t3_mesh *me = t3_mesh_new(g, B.mat);
    me->base.cast_shadow = false;
    me->base.receive_shadow = true;
    t3_object_add(B.group, me);
    t3_release(me);
    t3_release(g);
  }
  cw_node node = hang(parent, "Ground", B.group);
  t3_release(B.group);
  t3_release(m);
  cw_info("ground: %d cells, %d triangles, half %.0f m", B.cells, B.tris, o.half);
  return node;
}

/* ── water ───────────────────────────────────────────────────────────────────────────────────────── */

water_opts water_defaults(void) {
  water_opts w = { 0 };
  w.color = rgb(0.55f, 0.78f, 0.82f);
  w.deep = rgb(0.02f, 0.12f, 0.16f);
  w.clarity = 0.35f;
  w.wave_height = 0.25f;
  w.wave_length = 18;
  w.wave_dir = 30;
  w.choppy = 0.4f;
  w.foam = 0.5f;
  w.reflect = 0.5f;
  w.flow_x = 0.05f, w.flow_z = 0.02f;
  w.refract = true;
  w.grid = 200;
  return w;
}

static cw_value vec4v(float a, float b, float c, float d) {
  cw_value v;
  memset(&v, 0, sizeof v);
  v.type = CW_T_VEC4;
  v.v[0] = a, v.v[1] = b, v.v[2] = c, v.v[3] = d;
  return v;
}

cw_node water_surface(cw_node parent, float cx, float cz, float size_x, float size_z, float level, const water_opts *in) {
  water_opts w = in ? *in : water_defaults();
  cw_node n = cw_node_new("Water3D", "Water");
  cw_node_add_child(parent, n);
  cw_node3d_set_position(n, (t3_vec3){ cx, level, cz });
  float amp = w.wave_height * 0.5f, st = cw_clampf(w.choppy, 0, 1);
  cw_node_set(n, "size", cw_val_vec2(size_x, size_z));
  cw_node_set(n, "subdivisions", cw_val_int(w.grid));
  cw_node_set(n, "wave_count", cw_val_int(3));
  cw_node_set(n, "wave0", vec4v(w.wave_dir, w.wave_length, amp, st));
  cw_node_set(n, "wave1", vec4v(w.wave_dir + 55.0f, w.wave_length * 0.52f, amp * 0.45f, st * 0.8f));
  cw_node_set(n, "wave2", vec4v(w.wave_dir - 38.0f, w.wave_length * 0.24f, amp * 0.2f, st * 0.6f));
  cw_node_set(n, "color", cw_val_color(w.color.r, w.color.g, w.color.b, 1));
  cw_node_set(n, "deep_color", cw_val_color(w.deep.r, w.deep.g, w.deep.b, 1));
  cw_node_set(n, "murk", cw_val_float(1.0f - cw_clampf(w.clarity, 0, 1)));
  cw_node_set(n, "reflectivity", cw_val_float(0.02f + 0.16f * cw_clampf(w.reflect, 0, 1)));
  cw_node_set(n, "roughness", cw_val_float(0.12f - 0.1f * cw_clampf(w.reflect, 0, 1)));
  cw_node_set(n, "detail", cw_val_float(0.12f + 0.6f * cw_clampf(w.foam, 0, 1)));
  cw_node_set(n, "distortion", cw_val_float(0.02f + 0.04f * cw_clampf(w.foam, 0, 1)));
  float fl = sqrtf(w.flow_x * w.flow_x + w.flow_z * w.flow_z);
  if (fl > 1e-5f) cw_node_set(n, "flow_direction", cw_val_vec2(w.flow_x / fl, w.flow_z / fl));
  cw_node_set(n, "flow_speed", cw_val_float(fl));
  cw_node_set(n, "reflection", cw_val_bool(true));
  cw_node_set(n, "refraction", cw_val_bool(w.refract));
  if (w.horizon > 0 && !cw_node_set(n, "horizon_distance", cw_val_float(w.horizon))) cw_warn("water: this engine has no horizon_distance");
  return n;
}

cw_node water_polygon(cw_node parent, const t3_vec2 *pts, int n, float level, const water_opts *o) {
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (int i = 0; i < n; i++) x0 = fminf(x0, pts[i].x), x1 = fmaxf(x1, pts[i].x), z0 = fminf(z0, pts[i].y), z1 = fmaxf(z1, pts[i].y);
  return water_surface(parent, (x0 + x1) * 0.5f, (z0 + z1) * 0.5f, (x1 - x0) * 1.05f, (z1 - z0) * 1.05f, level, o);
}

cw_node water_lake(cw_node parent, float cx, float cz, float rx, float rz, float level, const water_opts *o) {
  return water_surface(parent, cx, cz, rx * 2.1f, rz * 2.1f, level, o);
}

/* the film: a transparent glossy ribbon with a scrolling ripple normal map */
static struct {
  t3_texture *normal;
  t3_material *mat[4];
  int nmat;
  float flow[4][2];
} WF;

static t3_texture *ripple_texture(void) {
  if (WF.normal) return WF.normal;
  const int N = 128;
  float *h = malloc(sizeof(float) * N * N);
  /* a seamless sum of plane waves with whole-number frequencies */
  static const int fx[8] = { 3, 5, 2, 7, 4, 9, 6, 11 }, fz[8] = { 2, 3, 5, 1, 8, 4, 7, 5 };
  static const float amp[8] = { 1.0f, 0.8f, 0.7f, 0.55f, 0.5f, 0.3f, 0.3f, 0.2f }, ph[8] = { 0.3f, 1.7f, 2.9f, 4.4f, 0.9f, 3.6f, 5.5f, 2.1f };
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float s = 0;
      for (int k = 0; k < 8; k++) s += amp[k] * sinf(CW_TAU * ((float)(fx[k] * i + fz[k] * j) / (float)N) + ph[k]);
      h[j * N + i] = s;
    }
  uint8_t *px = malloc((size_t)N * N * 4);
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float dx = h[j * N + (i + 1) % N] - h[j * N + (i + N - 1) % N], dz = h[((j + 1) % N) * N + i] - h[((j + N - 1) % N) * N + i];
      float nx = -dx * 0.45f, nz = -dz * 0.45f, ny = 1.0f, l = sqrtf(nx * nx + ny * ny + nz * nz);
      uint8_t *p = px + (j * N + i) * 4;
      p[0] = (uint8_t)(255.0f * (0.5f + 0.5f * nx / l)), p[1] = (uint8_t)(255.0f * (0.5f + 0.5f * nz / l)), p[2] = (uint8_t)(255.0f * (0.5f + 0.5f * ny / l)), p[3] = 255;
    }
  t3_texture *t = t3_texture_new(N, N, px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  t->color_space = T3_NO_COLOR_SPACE;
  t->flip_y = false;
  free(h), free(px);
  WF.normal = t;
  return t;
}

static t3_material *film_material(const water_opts *w) {
  t3_material *m = phys((t3_color){ w->deep.r * 2 + 0.02f, w->deep.g * 2 + 0.05f, w->deep.b * 2 + 0.06f }, 0.06f - 0.04f * w->reflect, 0);
  m->transparent = true;
  m->opacity = 0.45f + 0.4f * (1.0f - w->clarity);
  m->depth_write = false;
  m->specular_intensity = 1.0f;
  m->env_map_intensity = 1.0f + 1.5f * w->reflect;
  t3_texture *nt = ripple_texture();
  m->normal_map = t3_retain(nt);
  m->normal_scale = (t3_vec2){ 0.35f + 0.5f * w->foam, 0.35f + 0.5f * w->foam };
  if (WF.nmat < 4) {
    WF.mat[WF.nmat] = m;
    WF.flow[WF.nmat][0] = w->flow_x * 3.0f + 0.04f, WF.flow[WF.nmat][1] = w->flow_z * 3.0f + 0.03f;
    WF.nmat++;
  }
  return m;
}

/* A ribbon along a list of (centre, right, up) frames: lateral half width w, `lift` along up. */
typedef struct rframe {
  t3_vec3 p, r, u;
  float s, w;
} rframe;
static t3_geometry *ribbon_geometry(const rframe *f, int n, float lift) {
  gmesh m = { 0 };
  pcol white = { 1, 1, 1 };
  for (int i = 0; i < n; i++) {
    t3_vec3 c = { f[i].p.x + f[i].u.x * lift, f[i].p.y + f[i].u.y * lift, f[i].p.z + f[i].u.z * lift };
    for (int k = 0; k < 3; k++) {
      float lat = (k - 1) * f[i].w;
      gv(&m, c.x + f[i].r.x * lat, c.y + f[i].r.y * lat, c.z + f[i].r.z * lat, f[i].u.x, f[i].u.y, f[i].u.z, f[i].s / 5.0f, lat / 5.0f, white);
    }
  }
  for (int i = 0; i + 1 < n; i++)
    for (int k = 0; k < 2; k++) {
      int a = i * 3 + k, b = a + 1, c = a + 3, d = c + 1;
      gt(&m, a, b, c), gt(&m, b, d, c); /* up: right x forward */
    }
  return gfinish(&m, true);
}

cw_node water_along_range(cw_node parent, const track *t, float s0, float s1, float extra, float depth, const water_opts *in) {
  water_opts w = in ? *in : water_defaults();
  w.clarity = in ? w.clarity : 0.6f;
  cw_node node = cw_node_new("Node3D", "FloodedRoad");
  cw_node_add_child(parent, node);
  t3_object *group = t3_group_new();
  t3_material *mat = film_material(&w);
  int i0 = (int)floorf(s0 / STEP_M), i1 = (int)ceilf(s1 / STEP_M);
  if (i0 < 0) i0 = 0;
  if (i1 >= t->n) i1 = t->n - 1;
  rframe *buf = malloc(sizeof *buf * 64);
  int cnt = 0;
  for (int i = i0; i <= i1; i++) {
    const tsample *sm = &t->s[i];
    bool gap = (sm->flags & TF_GAP) != 0;
    if (!gap) {
      buf[cnt++] = (rframe){ sm->p, sm->r, sm->u, sm->s, sm->hw + extra };
    }
    if ((gap || cnt == 48 || i == i1) && cnt >= 2) {
      t3_geometry *g = ribbon_geometry(buf, cnt, depth);
      t3_mesh *me = t3_mesh_new(g, mat);
      me->base.receive_shadow = true;
      me->base.render_order = 2;
      t3_object_add(group, me);
      t3_release(me), t3_release(g);
      /* keep the last frame so chunks join */
      if (cnt == 48 && !gap) buf[0] = buf[cnt - 1], cnt = 1;
      else cnt = 0;
    } else if (gap) {
      cnt = 0;
    }
  }
  free(buf);
  cw_info("water film: %.0f..%.0f m, %d chunks", s0, s1, group->child_count);
  cw_node n2 = hang(node, "Film", group);
  t3_release(group);
  t3_release(mat);
  (void)n2;
  return node;
}

cw_node water_along_track(cw_node parent, const track *t, float extra, float depth, const water_opts *in) {
  water_opts w = in ? *in : water_defaults();
  w.clarity = in ? w.clarity : 0.6f;
  cw_node node = cw_node_new("Node3D", "FloodedRoads");
  cw_node_add_child(parent, node);
  int run0 = -1;
  for (int i = 0; i <= t->n; i++) {
    bool wet = i < t->n && (t->s[i].flags & TF_WATER);
    if (wet && run0 < 0) run0 = i;
    if (!wet && run0 >= 0) {
      water_along_range(node, t, t->s[run0].s - 2.0f, t->s[i - 1].s + 2.0f, extra, depth, &w);
      run0 = -1;
    }
  }
  return node;
}

cw_node water_ribbon(cw_node parent, const t3_vec3 *pts, int n, float width, const water_opts *in) {
  water_opts w = in ? *in : water_defaults();
  cw_node node = cw_node_new("Node3D", "River");
  cw_node_add_child(parent, node);
  if (n < 2) return node;
  rframe *f = malloc(sizeof *f * (size_t)n);
  float s = 0;
  for (int i = 0; i < n; i++) {
    t3_vec3 a = pts[i > 0 ? i - 1 : 0], b = pts[i < n - 1 ? i + 1 : n - 1];
    t3_vec3 d = { b.x - a.x, 0, b.z - a.z };
    float l = sqrtf(d.x * d.x + d.z * d.z);
    if (l < 1e-5f) d = (t3_vec3){ 0, 0, 1 }, l = 1;
    d = (t3_vec3){ d.x / l, 0, d.z / l };
    if (i > 0) s += sqrtf((pts[i].x - pts[i - 1].x) * (pts[i].x - pts[i - 1].x) + (pts[i].z - pts[i - 1].z) * (pts[i].z - pts[i - 1].z));
    f[i] = (rframe){ pts[i], (t3_vec3){ -d.z, 0, d.x }, (t3_vec3){ 0, 1, 0 }, s, width * 0.5f };
  }
  t3_material *mat = film_material(&w);
  t3_object *group = t3_group_new();
  t3_geometry *g = ribbon_geometry(f, n, 0);
  t3_mesh *me = t3_mesh_new(g, mat);
  me->base.receive_shadow = true;
  me->base.render_order = 2;
  t3_object_add(group, me);
  t3_release(me), t3_release(g), t3_release(mat);
  hang(node, "Ribbon", group);
  t3_release(group);
  free(f);
  return node;
}

/* a map is unloaded: the film's materials went with its scene */
void water_reset(void) { WF.nmat = 0; }

void water_frame(double time) {
  for (int i = 0; i < WF.nmat; i++) {
    t3_material *m = WF.mat[i];
    if (!m->normal_map) continue;
    m->normal_map->offset.x = (float)fmod(time * WF.flow[i][0] * 0.35, 1.0);
    m->normal_map->offset.y = (float)fmod(time * WF.flow[i][1] * 0.35, 1.0);
  }
}
