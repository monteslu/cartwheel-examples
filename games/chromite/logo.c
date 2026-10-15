/* chromite logo: the title's wordmark in 3D, after the original's (heavy
 * slanted chrome letters in a blue cast over a crackle of electric light).
 *
 *   letters   strokes on a 4 x 6 grid with 45 degree chamfers, each stroke a
 *             bar with bevelled edges (an octagonal section) so every edge
 *             catches the horizon in the environment; slanted forward;
 *             blue-tinted, clear-coated chrome
 *   energy    arcs of electric light that crawl behind the letters, rebuilt
 *             every frame as ribbons (HDR colour, so the bloom halos them),
 *             and a cold glow behind it all
 *   glint     the light the game's bomb uses (idle on the title) sweeps
 *             across the face of the letters
 *
 * Render-only, and only on the title. */
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define ARCS 7
#define ARC_SEGS 48

typedef struct seg { float x0, y0, x1, y1; } seg;
typedef struct glyph { char c; int n; seg s[8]; } glyph;
static const glyph GLYPHS[] = {
  { 'C', 5, { { 1, 6, 4, 6 }, { 1, 6, 0, 5 }, { 0, 5, 0, 1 }, { 0, 1, 1, 0 }, { 1, 0, 4, 0 } } },
  { 'H', 3, { { 0, 0, 0, 6 }, { 4, 0, 4, 6 }, { 0, 3, 4, 3 } } },
  { 'R', 7, { { 0, 0, 0, 6 }, { 0, 6, 3, 6 }, { 3, 6, 4, 5 }, { 4, 5, 4, 4 }, { 4, 4, 3, 3 }, { 3, 3, 0, 3 }, { 2, 3, 4, 0 } } },
  { 'O', 8, { { 1, 0, 3, 0 }, { 3, 0, 4, 1 }, { 4, 1, 4, 5 }, { 4, 5, 3, 6 }, { 3, 6, 1, 6 }, { 1, 6, 0, 5 }, { 0, 5, 0, 1 }, { 0, 1, 1, 0 } } },
  { 'M', 4, { { 0, 0, 0, 6 }, { 0, 6, 2, 3 }, { 2, 3, 4, 6 }, { 4, 6, 4, 0 } } },
  { 'I', 3, { { 1, 0, 1, 6 }, { 0, 6, 2, 6 }, { 0, 0, 2, 0 } } },
  { 'T', 2, { { 0, 6, 4, 6 }, { 2, 6, 2, 0 } } },
  { 'E', 4, { { 0, 0, 0, 6 }, { 0, 6, 4, 6 }, { 0, 3, 3, 3 }, { 0, 0, 4, 0 } } },
};

static struct {
  t3_object *root, *letters;
  t3_material *chrome, *arc_mat, *glow_mat;
  t3_mesh *arcs[ARCS];
  t3_sprite *glow;
  float width;
} L;

/* geometry builder: triangles with their own normals */
typedef struct gb { float *p, *n; int count, cap; } gb;
static void gb_v(gb *b, t3_vec3 p, t3_vec3 n) {
  if (b->count + 1 > b->cap) {
    b->cap = b->cap ? b->cap * 2 : 4096;
    b->p = realloc(b->p, sizeof(float) * 3 * (size_t)b->cap);
    b->n = realloc(b->n, sizeof(float) * 3 * (size_t)b->cap);
  }
  float *o = b->p + b->count * 3, *q = b->n + b->count * 3;
  o[0] = p.x, o[1] = p.y, o[2] = p.z, q[0] = n.x, q[1] = n.y, q[2] = n.z;
  b->count++;
}
static t3_vec3 nrm(t3_vec3 v) {
  float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
  return l > 0 ? (t3_vec3){ v.x / l, v.y / l, v.z / l } : v;
}

/* the section across a stroke: a rounded front (so the horizon in the
 * environment runs along every stroke as a dark band, as on the original's
 * chrome), bevelled sides, a flat back. s = across, z = out of the face;
 * with each point's normal in (s, z). */
#define SEC 13
static void section(float hw, float hd, float (*pt)[2], float (*nn)[2]) {
  for (int i = 0; i <= 10; i++) {
    float a = 3.1415927f * (1 - i / 10.0f); /* 180 .. 0 degrees */
    float c = cosf(a), s = sinf(a);
    pt[i][0] = hw * c, pt[i][1] = hd * 0.2f + hd * 0.8f * s;
    float nx = c / hw, nz = s / (hd * 0.8f), l = sqrtf(nx * nx + nz * nz);
    nn[i][0] = nx / l, nn[i][1] = nz / l;
  }
  pt[11][0] = hw * 0.8f, pt[11][1] = -hd, nn[11][0] = 0.7f, nn[11][1] = -0.7f;
  pt[12][0] = -hw * 0.8f, pt[12][1] = -hd, nn[12][0] = -0.7f, nn[12][1] = -0.7f;
}

/* a stroke from (x0, y0) to (x1, y1): the section swept along it, ends open
 * (a joint covers them) */
static void bar(gb *b, float x0, float y0, float x1, float y1, float hw, float hd, float slant) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  if (len < 1e-4f) return;
  float ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
  float pt[SEC][2], nn[SEC][2];
  section(hw, hd, pt, nn);
  t3_vec3 A[SEC], B[SEC], N[SEC];
  for (int i = 0; i < SEC; i++) {
    float ax = x0 + nx * pt[i][0], ay = y0 + ny * pt[i][0], bx = x1 + nx * pt[i][0], by = y1 + ny * pt[i][0];
    A[i] = (t3_vec3){ ax + ay * slant, ay, pt[i][1] };
    B[i] = (t3_vec3){ bx + by * slant, by, pt[i][1] };
    N[i] = nrm((t3_vec3){ nx * nn[i][0], ny * nn[i][0], nn[i][1] });
  }
  for (int i = 0; i < SEC; i++) {
    int j = (i + 1) % SEC;
    bool smooth = i < 10; /* the rounded front shares normals; the bevels and back are flat */
    t3_vec3 fn = nrm((t3_vec3){ N[i].x + N[j].x, N[i].y + N[j].y, N[i].z + N[j].z });
    t3_vec3 ni = smooth ? N[i] : fn, nj = smooth ? N[j] : fn;
    gb_v(b, A[i], ni), gb_v(b, B[i], ni), gb_v(b, B[j], nj);
    gb_v(b, A[i], ni), gb_v(b, B[j], nj), gb_v(b, A[j], nj);
  }
}

/* a joint: a rounded boss where strokes meet, the same section turned round
 * the face normal (so corners are smooth, never spiked) */
static void joint(gb *b, float x, float y, float hw, float hd, float slant) {
  enum { R = 20 };
  float pt[SEC][2], nn[SEC][2];
  section(hw, hd, pt, nn);
  /* only the front half of the section (s >= 0) turned round: a dome */
  for (int k = 0; k < R; k++) {
    float a0 = 6.2831853f * k / R, a1 = 6.2831853f * (k + 1) / R;
    float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
    for (int i = 5; i < 10; i++) {
      t3_vec3 p[4], n[4];
      int idx[2] = { i, i + 1 };
      for (int q = 0; q < 2; q++) {
        float r = pt[idx[q]][0], z = pt[idx[q]][1];
        float px0 = x + c0 * r, py0 = y + s0 * r, px1 = x + c1 * r, py1 = y + s1 * r;
        p[q * 2] = (t3_vec3){ px0 + py0 * slant, py0, z };
        p[q * 2 + 1] = (t3_vec3){ px1 + py1 * slant, py1, z };
        n[q * 2] = nrm((t3_vec3){ c0 * nn[idx[q]][0], s0 * nn[idx[q]][0], nn[idx[q]][1] });
        n[q * 2 + 1] = nrm((t3_vec3){ c1 * nn[idx[q]][0], s1 * nn[idx[q]][0], nn[idx[q]][1] });
      }
      gb_v(b, p[0], n[0]), gb_v(b, p[3], n[3]), gb_v(b, p[1], n[1]);
      gb_v(b, p[0], n[0]), gb_v(b, p[2], n[2]), gb_v(b, p[3], n[3]);
    }
    /* the rim and back of the boss */
    float r = hw;
    t3_vec3 q0 = { x + c0 * r + (y + s0 * r) * slant, y + s0 * r, pt[10][1] }, q1 = { x + c1 * r + (y + s1 * r) * slant, y + s1 * r, pt[10][1] };
    t3_vec3 q2 = { q1.x, q1.y, -hd }, q3 = { q0.x, q0.y, -hd };
    t3_vec3 rn = nrm((t3_vec3){ c0 + c1, s0 + s1, 0 });
    gb_v(b, q0, rn), gb_v(b, q3, rn), gb_v(b, q2, rn);
    gb_v(b, q0, rn), gb_v(b, q2, rn), gb_v(b, q1, rn);
  }
}

static t3_geometry *gb_finish(gb *b) {
  t3_geometry *g = t3_geometry_new();
  t3_attribute *p = t3_attribute_new(T3_FLOAT32, b->p, b->count, 3), *n = t3_attribute_new(T3_FLOAT32, b->n, b->count, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, p);
  t3_geometry_set_attribute(g, T3_ATTR_NORMAL, n);
  t3_release(p), t3_release(n);
  free(b->p), free(b->n);
  memset(b, 0, sizeof *b);
  return g;
}

static t3_color lin(float r, float g, float b) {
  float c[3] = { r, g, b };
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : cw_pow((c[i] + 0.055f) / 1.055f, 2.4f);
  return (t3_color){ c[0], c[1], c[2] };
}

void logo_build(t3_object *parent, t3_texture *halo) {
  logo_free();
  L.root = t3_group_new();
  L.letters = t3_group_new();
  /* the word, one geometry: 0.42-unit grid cells */
  const char *word = CHROMITE_TITLE;
  gb b = { 0 };
  float k = 0.42f, x = 0, hw = 0.82f * k, hd = 0.7f * k;
  for (const char *c = word; *c; c++) {
    const glyph *g = NULL;
    for (size_t i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++)
      if (GLYPHS[i].c == *c) g = &GLYPHS[i];
    if (!g) continue;
    float w = 0;
    for (int i = 0; i < g->n; i++) {
      const seg *s = &g->s[i];
      bar(&b, x + s->x0 * k, s->y0 * k, x + s->x1 * k, s->y1 * k, hw, hd, 0.22f);
      joint(&b, x + s->x0 * k, s->y0 * k, hw, hd, 0.22f);
      joint(&b, x + s->x1 * k, s->y1 * k, hw, hd, 0.22f);
      if (s->x0 > w) w = s->x0;
      if (s->x1 > w) w = s->x1;
    }
    x += (w + 2.6f) * k;
  }
  L.width = x - 2.6f * k;
  t3_geometry *g = gb_finish(&b);
  t3_geometry_translate(g, -L.width * 0.5f - 0.3f, -3 * k, 0);
  L.chrome = t3_mesh_physical_material_new(0xffffff);
  L.chrome->color = lin(0.5f, 0.64f, 1.0f);
  L.chrome->metalness = 1;
  L.chrome->roughness = 0.06f;
  L.chrome->clearcoat = 1;
  L.chrome->clearcoat_roughness = 0.02f;
  t3_material_set_texture(L.chrome, T3_ENV_MAP, env_logo_texture());
  L.chrome->env_map_intensity = 1.25f;
  t3_mesh *m = t3_mesh_new(g, L.chrome);
  t3_release(g);
  T3_OBJ(m)->cast_shadow = false;
  t3_object_add(L.letters, m);
  t3_release(m);
  t3_object_add(L.root, L.letters);
  t3_release(L.letters);
  /* the cold glow behind */
  L.glow_mat = t3_sprite_material_new(0xffffff);
  L.glow_mat->color = (t3_color){ 0.12f, 0.28f, 1.4f };
  t3_material_set_map(L.glow_mat, halo);
  L.glow_mat->blending = T3_ADDITIVE_BLENDING;
  L.glow_mat->depth_write = false;
  L.glow = t3_sprite_new(L.glow_mat);
  t3_object_set_position(L.glow, 0, 0, -3.5f);
  t3_object_set_scale(L.glow, L.width * 1.5f, L.width * 0.55f, 1);
  t3_object_add(L.root, L.glow);
  t3_release(L.glow);
  /* the arcs: ribbons rebuilt every frame */
  L.arc_mat = t3_mesh_basic_material_new(0xffffff);
  L.arc_mat->color = (t3_color){ 0.9f, 1.6f, 5.0f };
  L.arc_mat->blending = T3_ADDITIVE_BLENDING;
  L.arc_mat->transparent = true;
  L.arc_mat->depth_write = false;
  L.arc_mat->side = T3_DOUBLE_SIDE;
  for (int i = 0; i < ARCS; i++) {
    t3_geometry *ag = t3_geometry_new();
    float *z = calloc(ARC_SEGS * 6 * 3, sizeof(float));
    t3_attribute *p = t3_attribute_new(T3_FLOAT32, z, ARC_SEGS * 6, 3);
    free(z);
    t3_geometry_set_attribute(ag, T3_ATTR_POSITION, p);
    t3_release(p);
    L.arcs[i] = t3_mesh_new(ag, L.arc_mat);
    t3_release(ag);
    T3_OBJ(L.arcs[i])->frustum_culled = false;
    t3_object_add(L.root, L.arcs[i]);
    t3_release(L.arcs[i]);
  }
  L.root->visible = false;
  t3_object_add(parent, L.root);
  t3_release(L.root);
}

void logo_free(void) {
  if (L.chrome) t3_release(L.chrome);
  if (L.arc_mat) t3_release(L.arc_mat);
  if (L.glow_mat) t3_release(L.glow_mat);
  memset(&L, 0, sizeof L);
}

/* deterministic jitter for the arcs (render-only) */
static float hn(uint32_t a, uint32_t b) {
  uint32_t h = a * 374761393u + b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (h & 0xffff) / 32767.5f - 1;
}

/* faces the camera from eye toward at, lifted into the upper screen */
void logo_sync(bool visible, float t, t3_vec3 eye, t3_vec3 at, t3_light *glint) {
  if (!L.root) return;
  L.root->visible = visible;
  if (!visible) return;
  t3_vec3 f = { at.x - eye.x, at.y - eye.y, at.z - eye.z };
  float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  f.x /= fl, f.y /= fl, f.z /= fl;
  /* screen up: the camera's up projected off its forward (world -Z is up the screen) */
  t3_vec3 up = { 0, 0, -1 };
  float d = up.x * f.x + up.y * f.y + up.z * f.z;
  up = (t3_vec3){ up.x - f.x * d, up.y - f.y * d, up.z - f.z * d };
  float ul = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
  up.x /= ul, up.y /= ul, up.z /= ul;
  float dist = 26, lift = 3.4f;
  t3_vec3 p = { eye.x + f.x * dist + up.x * lift, eye.y + f.y * dist + up.y * lift, eye.z + f.z * dist + up.z * lift };
  t3_object_set_position(L.root, p.x, p.y, p.z);
  L.root->up = up;
  t3_object_look_at(L.root, eye.x, eye.y, eye.z);
  /* a slow sway so the reflections travel across the chrome */
  t3_object_set_rotation(L.letters, 0.12f * sinf(t * 0.37f), 0.22f * sinf(t * 0.23f), 0);
  /* the glint: a light sweeping left to right in front of the face */
  if (glint) {
    float sx = fmodf(t * 0.28f, 1.0f) * 2 - 1;
    t3_vec3 right = { up.y * f.z - up.z * f.y, up.z * f.x - up.x * f.z, up.x * f.y - up.y * f.x };
    t3_object_set_position(glint, p.x - f.x * 2.5f + right.x * sx * L.width * 0.7f, p.y - f.y * 2.5f + right.y * sx * L.width * 0.7f,
                           p.z - f.z * 2.5f + right.z * sx * L.width * 0.7f);
    glint->color = (t3_color){ 0.7f, 0.8f, 1 };
    glint->intensity = 60;
  }
  /* the arcs: noisy curves crawling behind and between the letters,
   * re-struck a few times a second */
  int strike = (int)(t * 7);
  for (int a = 0; a < ARCS; a++) {
    t3_attribute *pa = L.arcs[a]->geometry->attributes[T3_ATTR_POSITION];
    float *v = (float *)pa->array;
    uint32_t sd = (uint32_t)(strike * 31 + a * 977);
    float x0 = -L.width * 0.55f + hn(sd, 1) * 0.6f, x1 = L.width * 0.55f + hn(sd, 2) * 0.6f;
    float y0 = hn(sd, 3) * 1.2f, y1 = hn(sd, 4) * 1.2f, bulge = hn(sd, 5) * 1.6f;
    float wid = 0.018f + 0.022f * (0.5f + 0.5f * hn(sd, 6));
    bool live = hn(sd, 7) > 0.1f;
    for (int i = 0; i < ARC_SEGS; i++) {
      float s0 = (float)i / ARC_SEGS, s1 = (float)(i + 1) / ARC_SEGS;
      float pts[2][2];
      for (int q = 0; q < 2; q++) {
        float s = q ? s1 : s0;
        int k2 = i + q;
        float j = (hn(sd, 100 + k2) * 0.18f + hn(sd + 1, 200 + k2 / 4) * 0.35f) * sinf(s * 3.1415927f);
        pts[q][0] = x0 + (x1 - x0) * s;
        pts[q][1] = y0 + (y1 - y0) * s + bulge * sinf(s * 3.1415927f) + j;
      }
      float z = -2.2f - 0.8f * (0.5f + 0.5f * hn(sd, 8));
      float w = live ? wid : 0;
      float *o = v + i * 18;
      float tri[6][3] = { { pts[0][0], pts[0][1] - w, z }, { pts[1][0], pts[1][1] - w, z }, { pts[1][0], pts[1][1] + w, z },
                          { pts[0][0], pts[0][1] - w, z }, { pts[1][0], pts[1][1] + w, z }, { pts[0][0], pts[0][1] + w, z } };
      memcpy(o, tri, sizeof tri);
    }
    pa->version++;
  }
  L.glow_mat->color = (t3_color){ 0.12f, 0.28f, 1.3f + 0.25f * sinf(t * 5.3f) };
}
