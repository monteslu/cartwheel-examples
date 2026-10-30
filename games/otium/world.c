/* otium world: the map on a walnut table, built with three.c (three.js r186)
 * under one cartwheel node. Render-only: nothing here feeds the simulation.
 *
 *   the board   the parchment map in relief: the land raised off the sea,
 *               hills inland, the Alps and the Apennines in little faceted
 *               peaks, olive groves; a glassy sea laid over the painted one
 *   cities      travertine plinths, a town on each and a crate of the good
 *               it makes; Roma with its arena and temple. Each house's
 *               cottage stands on a plinth in its colour, popping up when built
 *   roads       dashed ink tracks; sea lanes dotted across the water
 *   units       a covered cart on the roads, a galley at sea, in the house
 *               colour, rolling and sailing along their path
 *   the cursor  a gold ring round the city it rests on, and a trail of
 *               gold beads along the path a move would take */
#include <stdlib.h>
#include <string.h>

#include "game.h"

uint32_t world_hash = 2166136261u;

const float PLAYER_COLOR[NP][3] = { { 0.72f, 0.12f, 0.10f }, { 0.16f, 0.30f, 0.62f }, { 0.20f, 0.50f, 0.26f } };
const float GOOD_COLOR[GOODS][3] = { { 0.72f, 0.33f, 0.20f }, { 0.90f, 0.75f, 0.30f }, { 0.55f, 0.58f, 0.62f }, { 0.50f, 0.18f, 0.40f }, { 0.30f, 0.50f, 0.78f } };
const float GOD_COLOR[GODS][3] = { { 0.95f, 0.80f, 0.25f }, { 0.55f, 0.70f, 0.35f }, { 0.35f, 0.75f, 0.80f }, { 0.85f, 0.25f, 0.20f }, { 0.95f, 0.55f, 0.20f } };

#define PATH_BEADS 96
#define TREES 420
#define PEAKS 320

static struct {
  cw_node node;
  t3_object *root;
  t3_object *houses[MAX_CITIES][NP];
  float house_pop[MAX_CITIES][NP]; /* 0..1, the cottage rising when built */
  t3_object *unit_root[NP][MAX_UNITS], *cart[NP][MAX_UNITS], *galley[NP][MAX_UNITS], *wheels[NP][MAX_UNITS][2], *oars[NP][MAX_UNITS];
  float unit_heading[NP][MAX_UNITS];
  t3_object *cursor, *sel_ring;
  t3_instanced_mesh *beads;
  t3_material *bead_mat;
  float last_t;
} W;

/* ── helpers ─────────────────────────────────────────────────────────── */

float world_height(float x, float z) {
  float lon = x + 13.0f, lat = 41.5f - z / 1.3f;
  return board_height(lon, lat, NULL);
}
t3_vec3 world_city_pos(int c) {
  float x = map_x(CITIES[c].lon), z = map_z(CITIES[c].lat);
  float h = world_height(x, z);
  return (t3_vec3){ x, (h < 0.03f ? 0.03f : h) + 0.004f, z };
}

static t3_material *std_mat(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_standard_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}
static t3_mesh *add(t3_object *parent, t3_geometry *g, t3_material *m, float x, float y, float z, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.position = (t3_vec3){ x, y, z };
  me->base.cast_shadow = cast;
  me->base.receive_shadow = true;
  t3_object_add(parent, me);
  t3_release(me);
  return me;
}
static t3_object *group(t3_object *parent, float x, float y, float z) {
  t3_object *g = t3_group_new();
  g->position = (t3_vec3){ x, y, z };
  t3_object_add(parent, g);
  t3_release(g);
  return g;
}
static t3_geometry *flat(t3_geometry *g) { /* faceted: each face its own normal */
  t3_geometry *n = t3_geometry_to_non_indexed(g);
  t3_release(g);
  t3_geometry_compute_vertex_normals(n);
  return n;
}

/* a triangle-soup builder for the merged meshes (roads, lanes, relief) */
typedef struct mb { float *p, *n, *uv; int count, cap; } mb;
static void mb_vert(mb *b, t3_vec3 p, t3_vec3 n, float u, float v) {
  if (b->count == b->cap) {
    b->cap = b->cap ? b->cap * 2 : 1024;
    b->p = realloc(b->p, sizeof(float) * 3 * b->cap);
    b->n = realloc(b->n, sizeof(float) * 3 * b->cap);
    b->uv = realloc(b->uv, sizeof(float) * 2 * b->cap);
  }
  int k = b->count++;
  b->p[k * 3] = p.x, b->p[k * 3 + 1] = p.y, b->p[k * 3 + 2] = p.z;
  b->n[k * 3] = n.x, b->n[k * 3 + 1] = n.y, b->n[k * 3 + 2] = n.z;
  b->uv[k * 2] = u, b->uv[k * 2 + 1] = v;
}
static t3_geometry *mb_finish(mb *b) {
  world_hash = cw_hash32(b->p, sizeof(float) * 3 * (size_t)b->count, world_hash);
  t3_geometry *g = t3_geometry_new();
  t3_attribute *p = t3_attribute_new(T3_FLOAT32, b->p, b->count, 3), *n = t3_attribute_new(T3_FLOAT32, b->n, b->count, 3),
               *uv = t3_attribute_new(T3_FLOAT32, b->uv, b->count, 2);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, p);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, n);
  t3_geometry_set_attribute(g, T3_ATTR_UV, uv);
  t3_release(p), t3_release(n), t3_release(uv);
  free(b->p), free(b->n), free(b->uv);
  memset(b, 0, sizeof *b);
  return g;
}
static void mb_flat_quad(mb *b, t3_vec3 a, t3_vec3 c, t3_vec3 d, t3_vec3 e) {
  t3_vec3 up = { 0, 1, 0 };
  mb_vert(b, a, up, 0, 0), mb_vert(b, c, up, 1, 0), mb_vert(b, d, up, 1, 1);
  mb_vert(b, a, up, 0, 0), mb_vert(b, d, up, 1, 1), mb_vert(b, e, up, 0, 1);
}

static uint32_t rs = 0x5eedu;
static float rnd01(void) {
  rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5;
  return (float)(rs & 0xffffff) / 16777216.0f;
}

/* ── the table and the board ─────────────────────────────────────────── */

#define BOARD_X0 (map_x(MAP_LON0))
#define BOARD_X1 (map_x(MAP_LON1))
#define BOARD_Z0 (map_z(MAP_LAT1))
#define BOARD_Z1 (map_z(MAP_LAT0))

static void build_table(t3_object *root) {
  t3_texture *wood = tex_wood(NULL);
  wood->repeat = (t3_vec2){ 5, 5 };
  t3_material *m = std_mat(lin(1, 1, 1), 0.55f, 0);
  t3_material_set_map(m, wood);
  t3_release(wood);
  t3_geometry *g = t3_plane_geometry_new(70, 70, 1, 1);
  t3_mesh *me = add(root, g, m, 0, -0.095f, 0.3f, false);
  t3_object_set_rotation(me, -1.5707963f, 0, 0.12f);
  t3_release(g), t3_release(m);
  /* the board's card under the map, a little proud of the table */
  t3_material *card = std_mat(lin(0.62f, 0.52f, 0.36f), 0.9f, 0);
  float w = BOARD_X1 - BOARD_X0, d = BOARD_Z1 - BOARD_Z0;
  g = t3_box_geometry_new(w + 0.16f, 0.06f, d + 0.16f, 1, 1, 1);
  add(root, g, card, (BOARD_X0 + BOARD_X1) * 0.5f, -0.062f, (BOARD_Z0 + BOARD_Z1) * 0.5f, true);
  t3_release(g), t3_release(card);
}

static void build_board(t3_object *root) {
  enum { GX = 330, GZ = 330 };
  float x0 = BOARD_X0, x1 = BOARD_X1, z0 = BOARD_Z0, z1 = BOARD_Z1;
  /* heights on a grid, then normals from neighbours, then triangles */
  float *h = malloc(sizeof(float) * (GX + 1) * (GZ + 1));
  for (int j = 0; j <= GZ; j++)
    for (int i = 0; i <= GX; i++) {
      float x = x0 + (x1 - x0) * i / GX, z = z0 + (z1 - z0) * j / GZ;
      float e = fminf(fminf((float)i, (float)(GX - i)), fminf((float)j, (float)(GZ - j)));
      h[j * (GX + 1) + i] = e < 2 ? 0 : world_height(x, z); /* the frame lies flat */
    }
  int nv = (GX + 1) * (GZ + 1);
  float *pos = malloc(sizeof(float) * 3 * nv), *nor = malloc(sizeof(float) * 3 * nv), *uv = malloc(sizeof(float) * 2 * nv);
  float sx = (x1 - x0) / GX, sz = (z1 - z0) / GZ;
  for (int j = 0; j <= GZ; j++)
    for (int i = 0; i <= GX; i++) {
      int k = j * (GX + 1) + i;
      pos[k * 3] = x0 + sx * i, pos[k * 3 + 1] = h[k], pos[k * 3 + 2] = z0 + sz * j;
      float hl = h[j * (GX + 1) + (i > 0 ? i - 1 : i)], hr = h[j * (GX + 1) + (i < GX ? i + 1 : i)];
      float hu = h[(j > 0 ? j - 1 : j) * (GX + 1) + i], hd = h[(j < GZ ? j + 1 : j) * (GX + 1) + i];
      float nx = (hl - hr) / (2 * sx), nz = (hu - hd) / (2 * sz), l = sqrtf(nx * nx + 1 + nz * nz);
      nor[k * 3] = nx / l, nor[k * 3 + 1] = 1 / l, nor[k * 3 + 2] = nz / l;
      uv[k * 2] = (float)i / GX, uv[k * 2 + 1] = (float)j / GZ;
    }
  uint32_t *idx = malloc(sizeof(uint32_t) * GX * GZ * 6);
  int ni = 0;
  for (int j = 0; j < GZ; j++)
    for (int i = 0; i < GX; i++) {
      uint32_t a = (uint32_t)(j * (GX + 1) + i), b = a + 1, c = a + GX + 1, d = c + 1;
      idx[ni++] = a, idx[ni++] = c, idx[ni++] = b, idx[ni++] = b, idx[ni++] = c, idx[ni++] = d;
    }
  world_hash = cw_hash32(h, sizeof(float) * nv, world_hash);
  t3_geometry *g = t3_geometry_new();
  t3_attribute *ap = t3_attribute_new(T3_FLOAT32, pos, nv, 3), *an = t3_attribute_new(T3_FLOAT32, nor, nv, 3), *au = t3_attribute_new(T3_FLOAT32, uv, nv, 2);
  t3_attribute *ai = t3_attribute_new(T3_UINT32, idx, ni, 1);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, ap);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, an);
  t3_geometry_set_attribute(g, T3_ATTR_UV, au);
  t3_geometry_set_index(g, ai);
  t3_release(ap), t3_release(an), t3_release(au), t3_release(ai);
  free(pos), free(nor), free(uv), free(idx), free(h);
  t3_texture *map = tex_board(NULL);
  t3_material *m = std_mat(lin(1, 1, 1), 0.92f, 0);
  t3_material_set_map(m, map);
  t3_release(map);
  add(root, g, m, 0, 0, 0, false)->base.receive_shadow = true;
  t3_release(g), t3_release(m);
}

/* peaks on the Alps and the Apennines, olive groves on the lowlands */
static void build_scenery(t3_object *root) {
  t3_geometry *peak = flat(t3_cone_geometry_new(0.09f, 0.16f, 6, 1, false));
  t3_material *rock = std_mat(lin(0.74f, 0.64f, 0.50f), 0.95f, 0);
  t3_instanced_mesh *pk = t3_instanced_mesh_new(peak, rock, PEAKS);
  int n = 0;
  rs = 0x9e11u;
  for (int tries = 0; tries < 6000 && n < PEAKS; tries++) {
    float lon = MAP_LON0 + 0.4f + rnd01() * (MAP_LON1 - MAP_LON0 - 0.8f), lat = MAP_LAT0 + 0.3f + rnd01() * (MAP_LAT1 - MAP_LAT0 - 0.6f);
    float mtn;
    float hh = board_height(lon, lat, &mtn);
    if (mtn < 0.45f) continue;
    float x = map_x(lon), z = map_z(lat), near = 9;
    for (int c = 0; c < N_CITIES; c++) {
      float dx = x - map_x(CITIES[c].lon), dz = z - map_z(CITIES[c].lat);
      near = fminf(near, dx * dx + dz * dz);
    }
    if (near < 0.08f) continue;
    float s = 0.6f + mtn * 0.8f + rnd01() * 0.35f;
    t3_mat4 mm;
    t3_mat4_compose(&mm, (t3_vec3){ x, hh - 0.01f + 0.08f * s, z }, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, rnd01() * 6.28f),
                    (t3_vec3){ s, s * (0.9f + rnd01() * 0.4f), s });
    t3_instanced_mesh_set_matrix_at(pk, n, &mm);
    float k = 0.85f + rnd01() * 0.2f;
    t3_instanced_mesh_set_color_at(pk, n, (t3_color){ k, k, k });
    n++;
  }
  pk->count = n;
  pk->mesh.base.cast_shadow = true;
  pk->mesh.base.receive_shadow = true;
  t3_object_add(root, pk);
  t3_release(pk), t3_release(peak), t3_release(rock);
  /* olive trees: a dark round crown on a stub */
  t3_geometry *crown = flat(t3_icosahedron_geometry_new(0.045f, 0));
  t3_material *leaf = std_mat(lin(0.36f, 0.42f, 0.24f), 0.9f, 0);
  t3_instanced_mesh *tr = t3_instanced_mesh_new(crown, leaf, TREES);
  n = 0;
  for (int tries = 0; tries < 20000 && n < TREES; tries++) {
    float lon = MAP_LON0 + 0.4f + rnd01() * (MAP_LON1 - MAP_LON0 - 0.8f), lat = MAP_LAT0 + 0.3f + rnd01() * (MAP_LAT1 - MAP_LAT0 - 0.6f);
    float mtn, hh = board_height(lon, lat, &mtn);
    float d = board_coast_dist(lon, lat);
    if (d < 0.12f || mtn > 0.35f || lon > 18.9f) continue;
    float x = map_x(lon), z = map_z(lat), near = 9;
    for (int c = 0; c < N_CITIES; c++) {
      float dx = x - map_x(CITIES[c].lon), dz = z - map_z(CITIES[c].lat);
      near = fminf(near, dx * dx + dz * dz);
    }
    if (near < 0.05f || near > 1.6f) continue; /* groves round the towns */
    float s = 0.8f + rnd01() * 0.6f;
    t3_mat4 mm;
    t3_mat4_compose(&mm, (t3_vec3){ x, hh + 0.035f * s, z }, t3_quat_from_axis_angle((t3_vec3){ 0, 1, 0 }, rnd01() * 6.28f), (t3_vec3){ s, s * 0.85f, s });
    t3_instanced_mesh_set_matrix_at(tr, n, &mm);
    float k = 0.8f + rnd01() * 0.35f;
    t3_instanced_mesh_set_color_at(tr, n, (t3_color){ k, k, k * 0.9f });
    n++;
  }
  tr->count = n;
  tr->mesh.base.cast_shadow = true;
  tr->mesh.base.receive_shadow = true;
  t3_object_add(root, tr);
  t3_release(tr), t3_release(crown), t3_release(leaf);
}

/* ── roads and sea lanes ─────────────────────────────────────────────── */

static void build_routes(t3_object *root) {
  mb road = { 0 }, lane = { 0 };
  for (int e = 0; e < N_EDGES; e++) {
    t3_vec3 a = world_city_pos(EDGES[e].a), b = world_city_pos(EDGES[e].b);
    float dx = b.x - a.x, dz = b.z - a.z, len = sqrtf(dx * dx + dz * dz);
    float ux = dx / len, uz = dz / len, nx = -uz, nz = ux;
    float start = 0.17f, stop = len - 0.17f;
    if (!EDGES[e].sea) { /* dashes, draped on the relief */
      float w = 0.026f;
      for (float t = start; t < stop; t += 0.09f) {
        float t1 = fminf(t + 0.055f, stop);
        float xa = a.x + ux * t, za = a.z + uz * t, xb = a.x + ux * t1, zb = a.z + uz * t1;
        float ha = fmaxf(world_height(xa, za), 0.03f) + 0.004f, hb = fmaxf(world_height(xb, zb), 0.03f) + 0.004f;
        mb_flat_quad(&road, (t3_vec3){ xa - nx * w, ha, za - nz * w }, (t3_vec3){ xb - nx * w, hb, zb - nz * w }, (t3_vec3){ xb + nx * w, hb, zb + nz * w },
                     (t3_vec3){ xa + nx * w, ha, za + nz * w });
      }
    } else { /* dots across the water */
      for (float t = start; t < stop; t += 0.11f) {
        float x = a.x + ux * t, z = a.z + uz * t, r = 0.014f;
        float y = fmaxf(world_height(x, z), 0.0f) + 0.016f;
        t3_vec3 c = { x, y, z };
        for (int s = 0; s < 6; s++) {
          float a0 = s * 1.0471976f, a1 = (s + 1) * 1.0471976f;
          t3_vec3 up = { 0, 1, 0 };
          mb_vert(&lane, c, up, 0.5f, 0.5f);
          mb_vert(&lane, (t3_vec3){ x + r * cw_cos(a1), y, z + r * cw_sin(a1) }, up, 0, 0);
          mb_vert(&lane, (t3_vec3){ x + r * cw_cos(a0), y, z + r * cw_sin(a0) }, up, 0, 0);
        }
      }
    }
  }
  t3_material *ink = std_mat(lin(0.22f, 0.13f, 0.07f), 0.85f, 0);
  t3_geometry *g = mb_finish(&road);
  add(root, g, ink, 0, 0, 0, false);
  t3_release(g), t3_release(ink);
  t3_material *foam = std_mat(lin(0.92f, 0.90f, 0.82f), 0.6f, 0);
  g = mb_finish(&lane);
  add(root, g, foam, 0, 0, 0, false);
  t3_release(g), t3_release(foam);
}

/* ── cities ──────────────────────────────────────────────────────────── */

static t3_geometry *roof_geo(float w, float d, float h) { /* a gable: a triangular prism along x */
  mb b = { 0 };
  t3_vec3 p[6] = { { -w, 0, -d }, { w, 0, -d }, { w, h, 0 }, { -w, h, 0 }, { -w, 0, d }, { w, 0, d } };
  t3_vec3 nf = { 0, d, -h }, nb = { 0, d, h };
  float l = sqrtf(d * d + h * h);
  nf.y /= l, nf.z /= l, nb.y /= l, nb.z /= l;
  mb_vert(&b, p[0], nf, 0, 0), mb_vert(&b, p[3], nf, 0, 1), mb_vert(&b, p[2], nf, 1, 1);
  mb_vert(&b, p[0], nf, 0, 0), mb_vert(&b, p[2], nf, 1, 1), mb_vert(&b, p[1], nf, 1, 0);
  mb_vert(&b, p[4], nb, 0, 0), mb_vert(&b, p[5], nb, 1, 0), mb_vert(&b, p[2], nb, 1, 1);
  mb_vert(&b, p[4], nb, 0, 0), mb_vert(&b, p[2], nb, 1, 1), mb_vert(&b, p[3], nb, 0, 1);
  t3_vec3 l1 = { -1, 0, 0 }, r1 = { 1, 0, 0 };
  mb_vert(&b, p[0], l1, 0, 0), mb_vert(&b, p[4], l1, 1, 0), mb_vert(&b, p[3], l1, 0.5f, 1);
  mb_vert(&b, p[1], r1, 0, 0), mb_vert(&b, p[2], r1, 0.5f, 1), mb_vert(&b, p[5], r1, 1, 0);
  return mb_finish(&b);
}

static struct { t3_material *stone, *plaster, *tile, *crate[GOODS], *player[NP], *player_roof[NP], *dark, *sail[NP], *wood, *gold; t3_geometry *roof, *wall, *house_wall, *house_roof; } M;

static void build_city(t3_object *root, int c) {
  t3_vec3 p = world_city_pos(c);
  const city_def *cd = &CITIES[c];
  t3_object *g = group(root, p.x, p.y, p.z);
  g->scale = (t3_vec3){ 1.6f, 1.6f, 1.6f }; /* a table model: towns bigger than the map's scale */
  bool capital = c == CAPITAL;
  float R = capital ? 0.2f : 0.13f;
  t3_geometry *pl = t3_cylinder_geometry_new(R, R * 1.08f, 0.05f, 24, 1, false);
  add(g, pl, M.stone, 0, 0.0f, 0, true);
  t3_release(pl);
  if (capital) { /* an arena and a temple */
    t3_geometry *ring = t3_cylinder_geometry_new(0.085f, 0.09f, 0.06f, 20, 1, true);
    t3_material *m = M.plaster;
    t3_mesh *ar = add(g, ring, m, -0.06f, 0.055f, 0.02f, true);
    (void)ar;
    t3_release(ring);
    t3_geometry *floor = t3_cylinder_geometry_new(0.07f, 0.07f, 0.01f, 20, 1, false);
    add(g, floor, M.tile, -0.06f, 0.03f, 0.02f, false);
    t3_release(floor);
    t3_geometry *base = t3_box_geometry_new(0.09f, 0.02f, 0.12f, 1, 1, 1);
    add(g, base, M.plaster, 0.09f, 0.035f, -0.02f, true);
    t3_release(base);
    t3_geometry *col = t3_cylinder_geometry_new(0.006f, 0.006f, 0.06f, 6, 1, false);
    for (int i = 0; i < 4; i++)
      for (int s = 0; s < 2; s++) add(g, col, M.plaster, 0.09f + (s ? 0.035f : -0.035f), 0.075f, -0.02f - 0.05f + i * 0.033f, true);
    t3_release(col);
    t3_mesh *rf = add(g, M.roof, M.tile, 0.09f, 0.105f, -0.02f, true);
    rf->base.scale = (t3_vec3){ 1.0f, 1.0f, 1.0f };
    t3_object_set_rotation(rf, 0, 1.5707963f, 0);
  } else { /* a little town: two blocks and a roof each */
    for (int i = 0; i < 2; i++) {
      float ox = i ? 0.03f : -0.03f, oz = i ? 0.02f : -0.025f, s = i ? 0.8f : 1.0f;
      t3_mesh *w = add(g, M.wall, M.plaster, ox, 0.025f + 0.025f * s, oz, true);
      w->base.scale = (t3_vec3){ s, s, s };
      t3_mesh *r = add(g, M.roof, M.tile, ox, 0.025f + 0.05f * s, oz, true);
      r->base.scale = (t3_vec3){ s * 0.55f, s * 0.6f, s * 0.6f };
    }
    /* the good it makes: a crate, sheaf, barrel... in its colour */
    t3_geometry *cg = t3_box_geometry_new(0.035f, 0.03f, 0.035f, 1, 1, 1);
    add(g, cg, M.crate[cd->good], 0.07f, 0.04f, 0.06f, true);
    t3_release(cg);
  }
  /* the houses' cottages, one slot per player round the rim */
  for (int i = 0; i < NP; i++) {
    float a = 2.4f + i * 2.0943951f;
    float r = capital ? 0.25f : 0.17f;
    t3_object *hg = group(g, r * cw_cos(a), 0.0f, r * cw_sin(a));
    t3_geometry *pad = t3_cylinder_geometry_new(0.045f, 0.05f, 0.03f, 12, 1, false);
    add(hg, pad, M.player[i], 0, 0.0f, 0, true);
    t3_release(pad);
    add(hg, M.house_wall, M.plaster, 0, 0.04f, 0, true);
    add(hg, M.house_roof, M.player_roof[i], 0, 0.065f, 0, true);
    hg->visible = false;
    hg->scale = (t3_vec3){ 0.001f, 0.001f, 0.001f };
    W.houses[c][i] = hg;
  }
}

/* ── units ───────────────────────────────────────────────────────────── */

static void build_unit(t3_object *root, int p, int u) {
  t3_object *g = group(root, 0, -5, 0);
  g->scale = (t3_vec3){ 1.7f, 1.7f, 1.7f };
  g->visible = false;
  W.unit_root[p][u] = g;
  /* the cart: a bed, a cloth hood in the house colour, two wheels */
  t3_object *cart = group(g, 0, 0, 0);
  t3_geometry *bed = t3_box_geometry_new(0.05f, 0.02f, 0.08f, 1, 1, 1);
  add(cart, bed, M.wood, 0, 0.03f, 0, true);
  t3_release(bed);
  t3_geometry *hd = t3_cylinder_geometry_new(0.027f, 0.027f, 0.075f, 10, 1, false);
  t3_mesh *h = add(cart, hd, M.sail[p], 0, 0.045f, 0, true);
  t3_object_set_rotation(h, 1.5707963f, 0, 0);
  t3_release(hd);
  t3_geometry *wh = t3_cylinder_geometry_new(0.018f, 0.018f, 0.008f, 12, 1, false);
  for (int s = 0; s < 2; s++) {
    t3_object *piv = group(cart, s ? 0.03f : -0.03f, 0.018f, 0);
    t3_mesh *w = add(piv, wh, M.dark, 0, 0, 0, true);
    t3_object_set_rotation(w, 0, 0, 1.5707963f);
    W.wheels[p][u][s] = piv;
  }
  t3_release(wh);
  W.cart[p][u] = cart;
  /* the galley: a long hull, a mast and a square sail */
  t3_object *gal = group(g, 0, 0, 0);
  t3_geometry *hull = t3_sphere_geometry_new(1, 12, 6);
  t3_mesh *hm = add(gal, hull, M.wood, 0, 0.012f, 0, true);
  hm->base.scale = (t3_vec3){ 0.028f, 0.016f, 0.085f };
  t3_release(hull);
  t3_geometry *deck = t3_box_geometry_new(0.04f, 0.006f, 0.13f, 1, 1, 1);
  add(gal, deck, M.dark, 0, 0.024f, 0, true);
  t3_release(deck);
  t3_geometry *mast = t3_cylinder_geometry_new(0.003f, 0.003f, 0.1f, 5, 1, false);
  add(gal, mast, M.dark, 0, 0.07f, 0, true);
  t3_release(mast);
  t3_geometry *sail = t3_box_geometry_new(0.07f, 0.055f, 0.003f, 1, 1, 1);
  add(gal, sail, M.sail[p], 0, 0.08f, 0.004f, true);
  t3_release(sail);
  t3_geometry *oar = t3_box_geometry_new(0.11f, 0.003f, 0.006f, 1, 1, 1);
  t3_object *oars = group(gal, 0, 0.02f, 0);
  for (int i = 0; i < 4; i++) add(oars, oar, M.dark, 0, 0, -0.045f + i * 0.03f, false);
  t3_release(oar);
  W.oars[p][u] = oars;
  W.galley[p][u] = gal;
}

/* ── the cursor and the path preview ─────────────────────────────────── */

static void build_cursor(t3_object *root) {
  t3_material *gold = t3_mesh_standard_material_new(0xffffff);
  gold->color = lin(1, 0.78f, 0.3f);
  gold->emissive = lin(1, 0.7f, 0.25f);
  gold->emissive_intensity = 0.9f;
  gold->roughness = 0.35f;
  gold->metalness = 0.6f;
  t3_geometry *ring = t3_torus_geometry_new(0.36f, 0.016f, 8, 56, 6.2831853f);
  t3_mesh *c = t3_mesh_new(ring, gold);
  t3_object_set_rotation(c, 1.5707963f, 0, 0);
  t3_object_add(root, c);
  W.cursor = (t3_object *)c;
  t3_release(c), t3_release(ring);
  t3_geometry *small = t3_torus_geometry_new(0.11f, 0.008f, 6, 28, 6.2831853f);
  t3_mesh *s = t3_mesh_new(small, gold);
  t3_object_set_rotation(s, 1.5707963f, 0, 0);
  t3_object_add(root, s);
  W.sel_ring = (t3_object *)s;
  t3_release(s), t3_release(small);
  t3_geometry *bead = t3_sphere_geometry_new(0.012f, 8, 6);
  W.beads = t3_instanced_mesh_new(bead, gold, PATH_BEADS);
  W.beads->count = 0;
  t3_object_add(root, W.beads);
  t3_release(W.beads), t3_release(bead);
  t3_release(gold);
}

void world_build(cw_node scene) {
  world_free();
  map_init();
  W.node = cw_node_new("Node3D", "World");
  cw_node_add_child(scene, W.node);
  W.root = t3_group_new();
  /* shared materials and parts */
  t3_texture *stone = tex_stone(NULL);
  M.stone = std_mat(lin(1, 1, 1), 0.8f, 0);
  t3_material_set_map(M.stone, stone);
  t3_release(stone);
  M.plaster = std_mat(lin(0.93f, 0.89f, 0.80f), 0.85f, 0);
  M.tile = std_mat(lin(0.72f, 0.34f, 0.20f), 0.75f, 0);
  M.dark = std_mat(lin(0.22f, 0.15f, 0.10f), 0.8f, 0);
  M.wood = std_mat(lin(0.50f, 0.34f, 0.20f), 0.75f, 0);
  for (int g = 0; g < GOODS; g++) M.crate[g] = std_mat(lin(GOOD_COLOR[g][0], GOOD_COLOR[g][1], GOOD_COLOR[g][2]), 0.7f, 0);
  for (int p = 0; p < NP; p++) {
    M.player[p] = std_mat(lin(PLAYER_COLOR[p][0], PLAYER_COLOR[p][1], PLAYER_COLOR[p][2]), 0.55f, 0);
    M.player_roof[p] = std_mat(lin(PLAYER_COLOR[p][0] * 1.15f, PLAYER_COLOR[p][1] * 1.15f, PLAYER_COLOR[p][2] * 1.15f), 0.6f, 0);
    M.sail[p] = std_mat(lin(PLAYER_COLOR[p][0] * 1.1f + 0.05f, PLAYER_COLOR[p][1] * 1.1f + 0.05f, PLAYER_COLOR[p][2] * 1.1f + 0.05f), 0.9f, 0);
  }
  M.roof = roof_geo(0.03f, 0.03f, 0.022f);
  M.wall = t3_box_geometry_new(0.05f, 0.05f, 0.05f, 1, 1, 1);
  M.house_wall = t3_box_geometry_new(0.045f, 0.035f, 0.04f, 1, 1, 1);
  M.house_roof = roof_geo(0.026f, 0.025f, 0.022f);
  build_table(W.root);
  build_board(W.root);
  build_scenery(W.root);
  build_routes(W.root);
  for (int c = 0; c < N_CITIES; c++) build_city(W.root, c);
  for (int p = 0; p < NP; p++)
    for (int u = 0; u < MAX_UNITS; u++) build_unit(W.root, p, u);
  build_cursor(W.root);
  cw_node3d_set_object(W.node, W.root);
  t3_release(W.root);
  W.last_t = -1;
}

void world_free(void) {
  if (cw_node_valid(W.node)) cw_node_free(W.node);
  t3_material **ms[] = { &M.stone, &M.plaster, &M.tile, &M.dark, &M.wood };
  for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++)
    if (*ms[i]) t3_release(*ms[i]), *ms[i] = NULL;
  for (int g = 0; g < GOODS; g++)
    if (M.crate[g]) t3_release(M.crate[g]), M.crate[g] = NULL;
  for (int p = 0; p < NP; p++) {
    if (M.player[p]) t3_release(M.player[p]), M.player[p] = NULL;
    if (M.player_roof[p]) t3_release(M.player_roof[p]), M.player_roof[p] = NULL;
    if (M.sail[p]) t3_release(M.sail[p]), M.sail[p] = NULL;
  }
  t3_geometry **gs[] = { &M.roof, &M.wall, &M.house_wall, &M.house_roof };
  for (unsigned i = 0; i < sizeof gs / sizeof gs[0]; i++)
    if (*gs[i]) t3_release(*gs[i]), *gs[i] = NULL;
  memset(&W, 0, sizeof W);
}

/* ── per frame ───────────────────────────────────────────────────────── */

void world_sync(const sim *S, float tick_f, float t, const world_view *v) {
  float dt = W.last_t < 0 ? 0 : cw_clampf(t - W.last_t, 0, 0.1f);
  W.last_t = t;
  /* houses rise when built */
  for (int c = 0; c < N_CITIES; c++)
    for (int p = 0; p < NP; p++) {
      t3_object *h = W.houses[c][p];
      if (!h) continue;
      bool on = S->house[c] >> p & 1;
      float *k = &W.house_pop[c][p];
      *k = on ? fminf(*k + dt * 2.5f, 1) : 0;
      h->visible = on;
      float e = *k < 1 ? 1 + 0.25f * cw_sin(*k * 3.14159f) : 1; /* a little overshoot */
      float s = fmaxf(*k * e, 0.001f);
      h->scale = (t3_vec3){ s, s, s };
    }
  /* units */
  for (int p = 0; p < NP; p++)
    for (int u = 0; u < MAX_UNITS; u++) {
      t3_object *g = W.unit_root[p][u];
      if (!g) continue;
      if (u >= S->p[p].n_units) {
        g->visible = false;
        continue;
      }
      g->visible = true;
      float x, z, heading;
      bool sea;
      float k = sim_unit_pos(S, p, u, tick_f, &x, &z, &sea, &heading);
      bool moving = k < 1 && S->p[p].units[u].path_n > 1;
      if (!moving) { /* parked: round the city, out of each other's way */
        float a = 0.6f + p * 2.0943951f + u * 0.42f, r = 0.36f + 0.05f * (u & 1);
        if (CITIES[S->p[p].units[u].city].region < 0) r = 0.5f;
        x += r * cw_cos(a), z += r * cw_sin(a);
        heading = a + 1.5707963f;
        sea = false;
      }
      float h = world_height(x, z);
      float y = sea ? 0.012f : fmaxf(h, 0.03f);
      if (sea && h > 0.03f) sea = false, y = h; /* a lane's ends run over the harbour */
      g->position = (t3_vec3){ x, y, z };
      /* turn smoothly toward the heading */
      float cur = W.unit_heading[p][u], d = heading - cur;
      while (d > 3.14159265f) d -= 6.2831853f;
      while (d < -3.14159265f) d += 6.2831853f;
      cur += d * fminf(1, dt * 8);
      W.unit_heading[p][u] = cur;
      t3_object_set_rotation(g, 0, cur, 0);
      W.cart[p][u]->visible = !sea;
      W.galley[p][u]->visible = sea;
      if (moving && !sea)
        for (int s = 0; s < 2; s++) t3_object_set_rotation(W.wheels[p][u][s], t * 9.0f, 0, 0);
      if (sea) {
        W.galley[p][u]->position.y = 0.004f * cw_sin(t * 2.2f + u + p);
        t3_object_set_rotation(W.oars[p][u], 0.35f * cw_sin(t * 6.0f + u), 0, 0);
      }
      W.cart[p][u]->position.y = moving && !sea ? 0.002f * cw_sin(t * 23.0f + u) : 0;
    }
  /* the cursor */
  if (W.cursor) {
    bool show = !v->title && v->cursor >= 0;
    W.cursor->visible = show;
    if (show) {
      t3_vec3 c = world_city_pos(v->cursor);
      float r = v->cursor == CAPITAL ? 1.35f : 1.0f;
      float pulse = 1 + 0.06f * cw_sin(t * 4.0f);
      W.cursor->position = (t3_vec3){ c.x, c.y + 0.03f + 0.01f * cw_sin(t * 3.0f), c.z };
      W.cursor->scale = (t3_vec3){ r * pulse, r * pulse, r * pulse };
    }
  }
  if (W.sel_ring) {
    bool show = !v->title && v->sel_unit >= 0 && v->sel_unit < S->p[0].n_units;
    W.sel_ring->visible = show;
    if (show) {
      t3_vec3 p = W.unit_root[0][v->sel_unit]->position;
      W.sel_ring->position = (t3_vec3){ p.x, p.y + 0.006f, p.z };
      float s = 1 + 0.12f * cw_sin(t * 6.0f);
      W.sel_ring->scale = (t3_vec3){ s, s, s };
    }
  }
  /* the path a move would take: beads flowing along it */
  if (W.beads) {
    int n = 0;
    if (v->preview_path && v->preview_n > 1)
      for (int i = 0; i + 1 < v->preview_n && n < PATH_BEADS; i++) {
        t3_vec3 a = world_city_pos(v->preview_path[i]), b = world_city_pos(v->preview_path[i + 1]);
        float dx = b.x - a.x, dz = b.z - a.z, len = sqrtf(dx * dx + dz * dz);
        float flow = fmodf(t * 0.25f, 0.12f);
        for (float s = 0.1f + flow; s < len - 0.08f && n < PATH_BEADS; s += 0.12f) {
          float x = a.x + dx * s / len, z = a.z + dz * s / len;
          float y = fmaxf(world_height(x, z), 0.012f) + 0.03f;
          t3_mat4 m;
          t3_mat4_compose(&m, (t3_vec3){ x, y, z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
          t3_instanced_mesh_set_matrix_at(W.beads, n++, &m);
        }
      }
    W.beads->count = n;
    W.beads->mesh.base.visible = n > 0;
  }
}
