/* derby's shared mesh helpers (the cars themselves are in cars.c): the
 * paint colours, a mesh builder, the light-beam cone. Formerly: four boxy, original bangers (a sedan, a wagon, a pickup and
 * a van) built from rounded boxes. The painted body is one mesh per car
 * whose vertices the dents push in: every vertex near a dent sinks along the
 * dent's direction, and the paint there gives way to bare steel and rust
 * (vertex colours over a white, grimy paint texture). Race numbers on the
 * doors and roof, chunky tyres on steel wheels, bumpers that work loose and
 * hang off one bolt as that end takes a beating, headlamps with a beam in
 * the dust, tail lamps. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

const float CAR_RGB[CAR_COLOURS][3] = {
  { 0.55f, 0.12f, 0.09f }, /* faded oxblood red */
  { 0.86f, 0.64f, 0.16f }, /* ex-taxi yellow */
  { 0.24f, 0.33f, 0.44f }, /* tired navy blue */
  { 0.2f, 0.32f, 0.22f },  /* forest green */
  { 0.82f, 0.8f, 0.74f },  /* old white */
  { 0.42f, 0.41f, 0.4f },  /* primer grey */
  { 0.5f, 0.32f, 0.2f },   /* brown metallic gone flat */
  { 0.56f, 0.62f, 0.66f }, /* powder blue */
};

/* ── the mesh builder ───────────────────────────────────────────────── */

int mb_vert(mesh_build *b, t3_vec3 p, t3_vec3 n, float u, float v) {
  if (b->nv == b->cap_v) {
    b->cap_v = b->cap_v ? b->cap_v * 2 : 1024;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)(uint32_t)b->cap_v);
    b->col = realloc(b->col, sizeof(float) * 3 * (size_t)(uint32_t)b->cap_v);
  }
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *U = b->uv + b->nv * 2, *C = b->col + b->nv * 3;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x, N[1] = n.y, N[2] = n.z;
  U[0] = u, U[1] = v;
  C[0] = C[1] = C[2] = 1;
  return b->nv++;
}
void mb_color(mesh_build *b, int v, float r, float g, float bl) {
  b->colors = true;
  float *C = b->col + v * 3;
  C[0] = r, C[1] = g, C[2] = bl;
}
static void mb_idx(mesh_build *b, int n) {
  if (b->ni + n > b->cap_i) {
    b->cap_i = b->cap_i ? b->cap_i * 2 : 3072;
    if (b->cap_i < b->ni + n) b->cap_i = b->ni + n;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)(uint32_t)b->cap_i);
  }
}
void mb_tri(mesh_build *b, int a, int c, int d) {
  mb_idx(b, 3);
  b->idx[b->ni++] = (uint32_t)a, b->idx[b->ni++] = (uint32_t)c, b->idx[b->ni++] = (uint32_t)d;
}
void mb_quad(mesh_build *b, int a, int c, int d, int e) {
  mb_tri(b, a, c, d);
  mb_tri(b, a, d, e);
}
t3_geometry *mb_finish(mesh_build *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *a;
  a = t3_attribute_new(T3_FLOAT32, b->pos, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->nor, b->nv, 3);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a);
  t3_release(a);
  a = t3_attribute_new(T3_FLOAT32, b->uv, b->nv, 2);
  t3_geometry_set_attribute(g, T3_ATTR_UV, a);
  t3_release(a);
  if (b->colors) {
    a = t3_attribute_new(T3_FLOAT32, b->col, b->nv, 3);
    t3_geometry_set_attribute(g, T3_ATTR_COLOR, a);
    t3_release(a);
  }
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  free(b->pos);
  free(b->nor);
  free(b->uv);
  free(b->col);
  free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

/* append a geometry (positions, normals, uvs, index) at an offset, scaled,
 * every vertex painted rgb */
static void append(mesh_build *b, t3_geometry *g, t3_vec3 at, t3_vec3 scale, const float rgb[3], float uv_scale) {
  t3_attribute *P = g->attributes[T3_ATTR_POSITION], *Nn = g->attributes[T3_ATTR_NORMAL], *U = g->attributes[T3_ATTR_UV];
  const float *p = P->array, *n = Nn->array, *u = U ? U->array : NULL;
  int base = b->nv;
  for (int i = 0; i < P->count; i++) {
    t3_vec3 q = { at.x + p[i * 3] * scale.x, at.y + p[i * 3 + 1] * scale.y, at.z + p[i * 3 + 2] * scale.z };
    t3_vec3 nn = { n[i * 3] / scale.x, n[i * 3 + 1] / scale.y, n[i * 3 + 2] / scale.z };
    float l = sqrtf(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
    nn.x /= l, nn.y /= l, nn.z /= l;
    int v = mb_vert(b, q, nn, u ? u[i * 2] * uv_scale : 0, u ? u[i * 2 + 1] * uv_scale : 0);
    mb_color(b, v, rgb[0], rgb[1], rgb[2]);
  }
  if (g->index) {
    const uint32_t *ix = g->index->array;
    const uint16_t *ix16 = g->index->array;
    for (int i = 0; i + 2 < g->index->count; i += 3) {
      if (g->index->type == T3_UINT16) mb_tri(b, base + ix16[i], base + ix16[i + 1], base + ix16[i + 2]);
      else mb_tri(b, base + (int)ix[i], base + (int)ix[i + 1], base + (int)ix[i + 2]);
    }
  } else
    for (int i = 0; i + 2 < P->count; i += 3) mb_tri(b, base + i, base + i + 1, base + i + 2);
  t3_release(g);
}

/* a light beam: an open cone (narrow end at +y), its vertex colours fading
 * from white at the lamp to black (nothing, added) at the far end */
t3_geometry *geo_beam(float r_lamp, float r_far, float len) {
  t3_geometry *g = t3_cylinder_geometry_new(r_lamp, r_far, len, 20, 6, true);
  t3_attribute *P = g->attributes[T3_ATTR_POSITION];
  float *c = malloc(sizeof(float) * 3 * (size_t)P->count);
  for (int i = 0; i < P->count; i++) {
    float k = (((const float *)P->array)[i * 3 + 1] + len * 0.5f) / len; /* 1 at the lamp */
    k = k * k;
    c[i * 3] = c[i * 3 + 1] = c[i * 3 + 2] = k;
  }
  t3_attribute *a = t3_attribute_new(T3_FLOAT32, c, P->count, 3);
  t3_geometry_set_attribute(g, T3_ATTR_COLOR, a);
  t3_release(a);
  free(c);
  return g;
}

