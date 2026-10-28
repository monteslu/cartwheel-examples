/* map 2: Drowned Downtown, a skyscraper city half under the sea, at night in a rainstorm.
 * Pink, cyan and violet neon over wet streets and flood water, towers standing out of the tide, a glass tube
 * through the lake bed, a collapsed overpass to jump and a corkscrew through its ruins. Everything is made by code
 * (props.h generators, procedural textures below); the scene file has the sky, fog and post. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/base.h"
#include "cartwheel/math.h"
#include "../props.h"

static const piece SPEC[] = {
  WALLS(3, 3),
  ITEMS(),
  STRAIGHT(120),
  CHECK(),
  BOOST(30),
  STRAIGHT(40),
  BANKED(90, 120, 12),
  STRAIGHT(30),
  ITEMS(),
  WATER(130),
  STRAIGHT(70),
  BANKED(-40, 90, -10),
  BANKED(40, 90, 10),
  STRAIGHT(30),
  TUNNEL(2),
  SLOPE(60, -12),
  STRAIGHT(40),
  SLOPE(60, 0),
  STRAIGHT(30),
  SLOPE(60, 12),
  STRAIGHT(30),
  SLOPE(60, 0),
  TUNNEL(0),
  CHECK(),
  STRAIGHT(30),
  ABANKED(90, 120, 12),
  ASTRAIGHT(50),
  ITEMS(),
  BOOST(30),
  STRAIGHT(30),
  SLOPE(36, 20),
  SLOPE(22, 20),
  JUMP(0, 30),
  SLOPE(80, 0),
  STRAIGHT(30),
  WIDTH(30, 4.6f),
  CORKSCREW(190, 1),
  WIDTH(30, 9.0f),
  STRAIGHT(30),
  BANKED(90, 120, 12),
  STRAIGHT(40),
  ITEMS(),
  WATER(90),
  BANKED(60, 130, 12),
  BANKED(-60, 130, -12),
  STRAIGHT(100),
  BOOST(30),
  STRAIGHT(80),
  BANKED(-50, 120, -12),
  BANKED(50, 120, 12),
  STRAIGHT(40),
  CHECK(),
  ABANKED(90, 120, 12),
  ASTRAIGHT(100),
  ASLOPE(50, 0),
  ASTRAIGHT(80),
  STRAIGHT(40),
  END(),
};

/* ── small helpers ───────────────────────────────────────────────────────────────────────────────── */

static t3_vec3 V(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 vadd(t3_vec3 a, t3_vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 vmul(t3_vec3 a, float k) { return V(a.x * k, a.y * k, a.z * k); }

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
  return x;
}
static float frand(uint32_t *s) {
  *s = hash32(*s + 0x9e3779b9u);
  return (float)(*s & 0xffffff) / 16777215.0f;
}
static float fclampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* ── where things are on the course (read from the track's flags, so a changed course follows) ──── */

#define MAXRUN 8
typedef struct run { float s0, s1; } run;
static struct {
  const track *t;
  int nwater;
  run water[MAXRUN];
  run plate;      /* the glass tube */
  run dip;        /* the part of the tube below the sea */
  run cork;
  run gap;        /* the collapsed overpass the kart flies over */
  int nboost;
  run boost[MAXRUN];
  float start_s;
  float lowest_y, cx, cz;  /* the lake bed's depth and the middle of the circuit */
  float x0, x1, z0, z1;    /* extent of the course */
} L;

static void scan_course(const track *t) {
  memset(&L, 0, sizeof L);
  L.t = t;
  L.x0 = L.z0 = 1e9f, L.x1 = L.z1 = -1e9f;
  uint32_t prev = 0;
  for (int i = 0; i < t->n; i++) {
    const tsample *a = &t->s[i];
    uint32_t f = a->flags, d = f & ~prev, u = prev & ~f;
    if (a->p.x < L.x0) L.x0 = a->p.x;
    if (a->p.x > L.x1) L.x1 = a->p.x;
    if (a->p.z < L.z0) L.z0 = a->p.z;
    if (a->p.z > L.z1) L.z1 = a->p.z;
    if (a->p.y < L.lowest_y) L.lowest_y = a->p.y;
    if ((d & TF_WATER) && L.nwater < MAXRUN) L.water[L.nwater].s0 = a->s;
    if ((u & TF_WATER) && L.nwater < MAXRUN) L.water[L.nwater++].s1 = a->s;
    if (d & TF_PLATE) L.plate.s0 = a->s;
    if (u & TF_PLATE) L.plate.s1 = a->s;
    if (d & TF_LOOP) L.cork.s0 = a->s;
    if (u & TF_LOOP) L.cork.s1 = a->s;
    if ((d & TF_BOOST) && L.nboost < MAXRUN) L.boost[L.nboost].s0 = a->s;
    if ((u & TF_BOOST) && L.nboost < MAXRUN) L.boost[L.nboost++].s1 = a->s;
    prev = f;
  }
  if (t->ngaps > 0) L.gap.s0 = t->s[t->gap_from[0]].s, L.gap.s1 = t->s[t->gap_to[0]].s;
  L.start_s = t->nchecks > 0 ? t->s[t->check_at[0]].s : 0;
  L.cx = 0.5f * (L.x0 + L.x1), L.cz = 0.5f * (L.z0 + L.z1);
  /* the part of the tube that is under the sea */
  L.dip.s0 = L.dip.s1 = 0;
  for (int i = 0; i < t->n; i++)
    if ((t->s[i].flags & TF_PLATE) && t->s[i].p.y < -2.0f) {
      if (L.dip.s0 == 0) L.dip.s0 = t->s[i].s;
      L.dip.s1 = t->s[i].s;
    }
  cw_info("drowned: course x %.0f..%.0f z %.0f..%.0f, lowest %.1f, plate %.0f..%.0f dip %.0f..%.0f gap %.0f..%.0f cork %.0f..%.0f",
          L.x0, L.x1, L.z0, L.z1, L.lowest_y, L.plate.s0, L.plate.s1, L.dip.s0, L.dip.s1, L.gap.s0, L.gap.s1, L.cork.s0, L.cork.s1);
  { float lo = 1e9f, hi = -1e9f, mh = 1e9f;
    for (int i = 0; i < t->n; i++) if (t->s[i].s >= L.cork.s0 && t->s[i].s <= L.cork.s1) { lo = fminf(lo, t->s[i].p.y), hi = fmaxf(hi, t->s[i].p.y), mh = fminf(mh, t->s[i].hw); }
    cw_info("drowned: corkscrew road y %.1f..%.1f, narrowest half width %.1f", lo, hi, mh); }
  for (int i = 0; i < L.nwater; i++) cw_info("drowned: ford %d s %.0f..%.0f", i, L.water[i].s0, L.water[i].s1);
  for (int i = 0; i < L.nboost; i++) cw_info("drowned: boost %d s %.0f..%.0f", i, L.boost[i].s0, L.boost[i].s1);
}

/* ── the ground: flooded street bed, with a deep lake basin over the glass tube ──────────────────── */

static int PLAN_VIEWS = 1;
#define SEA_Y (-6.6f)       /* the tide: well under the road (the corkscrew rolls the camera under the road, and it must not meet the sea) */
#define STREET_BED (-11.0f) /* the drowned streets */

/* ── colours: the neon palette ───────────────────────────────────────────────────────────────────── */

static pcol PINK, CYAN, VIOLET, AMBER, WARM, RED, LIME, BLUE;
static void init_colours(void) {
  PINK = hex(0xff2d95), CYAN = hex(0x18e6ff), VIOLET = hex(0x9a4dff), AMBER = hex(0xffa22e);
  WARM = hex(0xffe0a8), RED = hex(0xff2838), LIME = hex(0x9dff4a), BLUE = hex(0x3a7bff);
}
static pcol neon_pick(uint32_t *rs) {
  float r = frand(rs);
  return r < 0.28f ? PINK : r < 0.55f ? CYAN : r < 0.72f ? VIOLET : r < 0.84f ? AMBER : r < 0.92f ? BLUE : r < 0.97f ? LIME : RED;
}

/* ── neon groups: glowing geometry split by how it flickers, so each group's material can be driven alone ──
 * The structure (painted metal, panels) of every group is ordinary prop geometry; only the glow layer is swapped
 * for the group's own unlit material once the builder is finished. */
enum { GR_STEADY, GR_FLICK, GR_BUZZ, GR_PULSE, GR_N };
static struct {
  pmc *c[GR_N];
  t3_material *mat[GR_N];
  float level[GR_N];
} N;

static void neon_begin(void) {
  for (int g = 0; g < GR_N; g++) N.c[g] = pmc_new(70, 500u + (uint32_t)g * 31u), N.level[g] = 1;
}

/* the glow layer's own material for group g: swapped onto every mesh that carries the shared glow material */
static void retarget_glow(t3_object *o, t3_material *shared, t3_material *own) {
  if (o->type == T3_MESH) {
    t3_mesh *m = (t3_mesh *)o;
    if (m->material_count > 0 && m->materials[0] == shared) {
      t3_retain(own);
      t3_release(m->materials[0]);
      m->materials[0] = own;
    }
  }
  for (int i = 0; i < o->child_count; i++) retarget_glow(o->children[i], shared, own);
}
static void neon_finish(cw_node parent) {
  t3_material *shared = prop_material(PL_GLOW, -1, (pcol){ 1, 1, 1 }, 1);
  for (int g = 0; g < GR_N; g++) {
    char name[24];
    snprintf(name, sizeof name, "Neon%d", g);
    cw_node n = pmc_finish(N.c[g], parent, name, g == GR_STEADY, -1);
    N.c[g] = NULL;
    if (g == GR_STEADY) continue;
    N.mat[g] = t3_mesh_basic_material_new(0xffffff);
    N.mat[g]->vertex_colors = true;
    retarget_glow(cw_node3d_object(n), shared, N.mat[g]);
  }
}

/* ── frames: parts are designed once in a local frame and set on the road ───────────────────────── */

static pm *frame_in(pmc *c, t3_vec3 p, t3_vec3 x, t3_vec3 y, t3_vec3 z) {
  pm *b = pmc_at(c, p);
  pm_push(b);
  pm_basis(b, p, x, y, z);
  return b;
}
/* on a wall's inner face: x along the road, y up from the road surface, z toward the road; side -1 left, +1 right */
static pm *wall_frame(pmc *c, const tsample *a, int side, float lift, float inset) {
  t3_vec3 p = vadd(vadd(a->p, vmul(a->r, (float)side * (a->hw - inset))), vmul(a->u, lift));
  if (side < 0) return frame_in(c, p, vmul(a->f, -1), a->u, a->r);
  return frame_in(c, p, a->f, a->u, vmul(a->r, -1));
}
/* standing upright beside the road: y is world up, x the road's right flattened, z forward flattened */
static pm *upright_frame(pmc *c, t3_vec3 p, const tsample *a) {
  t3_vec3 r = V(a->r.x, 0, a->r.z), f = V(a->f.x, 0, a->f.z);
  float lr = sqrtf(r.x * r.x + r.z * r.z), lf = sqrtf(f.x * f.x + f.z * f.z);
  r = vmul(r, 1.0f / lr), f = vmul(f, 1.0f / lf);
  return frame_in(c, p, r, V(0, 1, 0), f);
}

/* ── ad pictures: made here, no words: each is a flat quad set drawn unlit with its own picture ─── */

enum { AD_SUN, AD_FISH, AD_BURST, AD_BARS, AD_WAVE, AD_TARGET, AD_HEX, AD_CHEVRON, AD_N };
static struct {
  t3_texture *tex[AD_N];
  t3_material *mat[AD_N];
  float *pos[AD_N], *uv[AD_N], *col[AD_N];
  int nq[AD_N], cap[AD_N];
} AD;

static float sm01(float a, float b, float x) {
  float t = fclampf((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}
typedef struct rgb8 { float r, g, b; } rgb8;
static rgb8 mix3(rgb8 a, rgb8 b, float t) { return (rgb8){ lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t) }; }

/* one picture, 256 x 128 texels; u, v in 0..1 (v up) */
static rgb8 ad_pixel(int kind, float u, float v) {
  rgb8 bg = { 0.03f, 0.02f, 0.08f }, c = bg;
  float x = (u - 0.5f) * 2.0f, y = v - 0.5f; /* x -1..1, y -0.5..0.5 */
  switch (kind) {
  case AD_SUN: { /* a banded sun over a grid floor */
    rgb8 top = { 0.12f, 0.02f, 0.3f }, hor = { 0.9f, 0.15f, 0.5f };
    c = mix3(top, hor, sm01(0.95f, 0.45f, v));
    float r = sqrtf(x * x * 0.25f + (y - 0.05f) * (y - 0.05f)) * 2.0f; /* in aspect */
    float d = sqrtf((x * 0.5f) * (x * 0.5f) + (y - 0.08f) * (y - 0.08f));
    if (d < 0.3f && v > 0.34f) {
      float band = sinf((v - 0.34f) * 62.0f) * 0.5f + 0.5f;
      float cut = sm01(0.5f, 0.0f, (v - 0.34f) * 1.2f);
      if (band > cut * 0.9f) c = mix3((rgb8){ 1.0f, 0.55f, 0.1f }, (rgb8){ 1.0f, 0.1f, 0.5f }, sm01(0.62f, 0.34f, v));
    }
    (void)r;
    if (v < 0.34f) { /* the floor: a cyan grid running to the horizon */
      float depth = (0.34f - v) / 0.34f, gx = fabsf(sinf(x * (3.0f + 40.0f * depth * depth) * 2.0f));
      float gy = fabsf(sinf(1.0f / (depth + 0.08f) * 1.6f));
      float line = fmaxf(sm01(0.9f, 1.0f, gx), sm01(0.9f, 1.0f, gy));
      c = mix3((rgb8){ 0.04f, 0.02f, 0.12f }, (rgb8){ 0.1f, 0.9f, 1.0f }, line * (0.4f + 0.6f * depth));
    }
    break; }
  case AD_FISH: { /* a big stylised fish and bubbles on deep blue */
    c = mix3((rgb8){ 0.0f, 0.1f, 0.25f }, (rgb8){ 0.0f, 0.3f, 0.5f }, v);
    float fx = (x + 0.15f) * 1.0f, fy = y * 2.2f;
    float body = fx * fx / 0.35f + fy * fy / 0.2f;
    float tx = x - 0.38f, tail = (tx > 0 && tx < 0.34f && fabsf(y) < 0.04f + tx * 0.5f) ? 1.0f : 0.0f;
    if (body < 1.0f || tail > 0) c = mix3((rgb8){ 1.0f, 0.5f, 0.1f }, (rgb8){ 1.0f, 0.15f, 0.5f }, sm01(-0.3f, 0.3f, y + x * 0.2f));
    float eye = (fx + 0.45f) * (fx + 0.45f) + (fy - 0.12f) * (fy - 0.12f);
    if (eye < 0.012f) c = (rgb8){ 1, 1, 1 };
    if (eye < 0.004f) c = (rgb8){ 0, 0, 0 };
    if (body < 1.0f && body > 0.82f) c = (rgb8){ 1.0f, 0.9f, 0.4f };
    for (int i = 0; i < 6; i++) {
      float bx = -0.9f + 0.28f * (float)i, by = -0.4f + 0.14f * (float)((i * 5) % 7), br = 0.03f + 0.01f * (float)(i % 3);
      float dd = sqrtf((x - bx) * (x - bx) * 0.25f + (y - by * 0.7f) * (y - by * 0.7f));
      if (fabsf(dd - br) < 0.007f) c = (rgb8){ 0.6f, 1.0f, 1.0f };
    }
    break; }
  case AD_BURST: { /* a sunburst with rings */
    float ang = atan2f(y, x * 0.5f), d = sqrtf(x * x * 0.25f + y * y);
    float rays = sinf(ang * 16.0f) > 0 ? 1.0f : 0.0f;
    c = mix3((rgb8){ 0.5f, 0.0f, 0.4f }, (rgb8){ 1.0f, 0.75f, 0.1f }, rays * (1.0f - d));
    float ring = sinf(d * 36.0f);
    if (ring > 0.82f) c = (rgb8){ 0.1f, 0.95f, 1.0f };
    if (d < 0.1f) c = (rgb8){ 1, 1, 1 };
    break; }
  case AD_BARS: { /* an equaliser */
    int col = (int)(u * 20.0f);
    float h = 0.15f + 0.8f * (0.5f + 0.5f * sinf((float)col * 1.7f + 1.0f) * cosf((float)col * 0.6f));
    float cu = u * 20.0f - (float)col;
    if (cu > 0.12f && cu < 0.88f && v < h) c = mix3((rgb8){ 0.1f, 0.9f, 1.0f }, (rgb8){ 1.0f, 0.15f, 0.7f }, sm01(0.1f, 0.9f, v));
    else c = mix3(bg, (rgb8){ 0.12f, 0.04f, 0.2f }, 0.5f * sm01(0.0f, 1.0f, v));
    break; }
  case AD_WAVE: { /* stacked waves */
    c = mix3((rgb8){ 0.02f, 0.02f, 0.12f }, (rgb8){ 0.25f, 0.05f, 0.4f }, v);
    for (int i = 0; i < 4; i++) {
      float w = 0.5f - 0.13f * (float)i + 0.07f * sinf(u * 9.0f + (float)i * 1.3f) + 0.03f * sinf(u * 23.0f + (float)i);
      if (v < w) c = mix3((rgb8){ 0.0f, 0.5f - 0.1f * (float)i, 0.8f }, (rgb8){ 0.0f, 0.9f, 0.9f }, 0.2f + 0.2f * (float)i);
      if (fabsf(v - w) < 0.012f) c = (rgb8){ 0.9f, 1.0f, 1.0f };
    }
    break; }
  case AD_TARGET: { /* a target */
    float d = sqrtf(x * x * 0.25f + y * y) * 2.0f;
    c = (rgb8){ 0.02f, 0.02f, 0.05f };
    float ring = fmodf(d * 4.0f, 1.0f);
    if (d < 0.95f) c = ring < 0.5f ? (rgb8){ 1.0f, 0.3f, 0.05f } : (rgb8){ 0.05f, 0.03f, 0.08f };
    if (d < 0.12f) c = (rgb8){ 1.0f, 0.9f, 0.3f };
    if (fabsf(y) < 0.012f || fabsf(x * 0.5f) < 0.006f) c = (rgb8){ 0.2f, 1.0f, 1.0f };
    break; }
  case AD_HEX: { /* a honeycomb */
    float hx = u * 18.0f, hy = v * 9.0f * 1.1547f;
    float row = floorf(hy), off = fmodf(row, 2.0f) * 0.5f;
    float fx = fmodf(hx + off, 1.0f) - 0.5f, fy = fmodf(hy, 1.0f) - 0.5f;
    float e = fmaxf(fabsf(fx) * 1.7f, fabsf(fx) * 0.85f + fabsf(fy) * 1.45f);
    c = mix3((rgb8){ 0.05f, 0.02f, 0.1f }, (rgb8){ 1.0f, 0.2f, 0.65f }, sm01(0.82f, 0.95f, e));
    float pulse = 0.5f + 0.5f * sinf(floorf(hx + off) * 2.3f + row * 1.7f);
    if (e < 0.7f && pulse > 0.7f) c = (rgb8){ 0.1f, 0.5f, 0.6f };
    break; }
  case AD_CHEVRON: { /* stacked chevrons */
    float yy = y + fabsf(x) * 0.5f * 0.7f;
    float k = fmodf(yy * 7.0f + 8.0f, 1.0f);
    c = k < 0.5f ? (rgb8){ 1.0f, 0.82f, 0.05f } : (rgb8){ 0.03f, 0.03f, 0.05f };
    break; }
  }
  return c;
}
static void ad_make(void) {
  for (int k = 0; k < AD_N; k++) {
    const int W = 256, H = 128;
    uint8_t *px = malloc((size_t)W * H * 4);
    for (int j = 0; j < H; j++)
      for (int i = 0; i < W; i++) {
        rgb8 c = ad_pixel(k, ((float)i + 0.5f) / (float)W, 1.0f - ((float)j + 0.5f) / (float)H);
        float edge = fminf(fminf((float)i, (float)(W - 1 - i)), fminf((float)j, (float)(H - 1 - j)));
        float fr = sm01(1.5f, 4.5f, edge); /* a bright rim */
        uint8_t *p = px + ((size_t)j * W + i) * 4;
        p[0] = (uint8_t)(fclampf(lerpf(1.0f, c.r, fr), 0, 1) * 255.0f);
        p[1] = (uint8_t)(fclampf(lerpf(1.0f, c.g, fr), 0, 1) * 255.0f);
        p[2] = (uint8_t)(fclampf(lerpf(1.0f, c.b, fr), 0, 1) * 255.0f);
        p[3] = 255;
      }
    t3_texture *t = t3_texture_new(W, H, px);
    free(px);
    t->color_space = T3_SRGB_COLOR_SPACE;
    t->wrap_s = t->wrap_t = T3_CLAMP_TO_EDGE;
    AD.tex[k] = t;
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    t3_material_set_map(m, t);
    m->color = (t3_color){ 1.4f, 1.4f, 1.4f };
    m->fog = true;
    AD.mat[k] = m;
  }
}
/* a picture standing at p, facing `n` (horizontal), w wide, h high, bottom edge at p: tilted slightly back */
static void ad_add(int kind, t3_vec3 p, t3_vec3 n, t3_vec3 up, float w, float h) {
  if (AD.nq[kind] == AD.cap[kind]) {
    AD.cap[kind] = AD.cap[kind] ? AD.cap[kind] * 2 : 16;
    AD.pos[kind] = realloc(AD.pos[kind], sizeof(float) * 12 * (size_t)AD.cap[kind]);
    AD.uv[kind] = realloc(AD.uv[kind], sizeof(float) * 8 * (size_t)AD.cap[kind]);
  }
  /* the quad's right is up x n ... chosen so the picture reads left to right seen from the front */
  t3_vec3 r = V(up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x);
  float lr = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
  r = vmul(r, 1.0f / lr);
  t3_vec3 c[4] = { vadd(p, vmul(r, -0.5f * w)), vadd(p, vmul(r, 0.5f * w)), vadd(vadd(p, vmul(r, 0.5f * w)), vmul(up, h)), vadd(vadd(p, vmul(r, -0.5f * w)), vmul(up, h)) };
  float *P = AD.pos[kind] + AD.nq[kind] * 12, *U = AD.uv[kind] + AD.nq[kind] * 8;
  static const float uu[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
  for (int i = 0; i < 4; i++) P[i * 3] = c[i].x, P[i * 3 + 1] = c[i].y, P[i * 3 + 2] = c[i].z, U[i * 2] = uu[i][0], U[i * 2 + 1] = uu[i][1];
  AD.nq[kind]++;
}
static void ad_finish(cw_node parent) {
  for (int k = 0; k < AD_N; k++) {
    int nq = AD.nq[k];
    if (!nq) continue;
    float *pos = malloc(sizeof(float) * 12 * (size_t)nq), *nor = malloc(sizeof(float) * 12 * (size_t)nq);
    uint32_t *ix = malloc(sizeof(uint32_t) * 6 * (size_t)nq);
    for (int q = 0; q < nq; q++) {
      memcpy(pos + q * 12, AD.pos[k] + q * 12, sizeof(float) * 12);
      const float *P = AD.pos[k] + q * 12;
      t3_vec3 e1 = V(P[3] - P[0], P[4] - P[1], P[5] - P[2]), e2 = V(P[9] - P[0], P[10] - P[1], P[11] - P[2]);
      t3_vec3 n = V(e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);
      float ln = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
      for (int i = 0; i < 4; i++) nor[q * 12 + i * 3] = n.x / ln, nor[q * 12 + i * 3 + 1] = n.y / ln, nor[q * 12 + i * 3 + 2] = n.z / ln;
      uint32_t b0 = (uint32_t)q * 4;
      uint32_t *I = ix + q * 6;
      I[0] = b0, I[1] = b0 + 1, I[2] = b0 + 2, I[3] = b0, I[4] = b0 + 2, I[5] = b0 + 3;
    }
    t3_geometry *g = t3_geometry_new();
    t3_attribute *a;
    a = t3_attribute_new(T3_FLOAT32, pos, nq * 4, 3), t3_geometry_set_attribute(g, T3_ATTR_POSITION, a), t3_release(a);
    a = t3_attribute_new(T3_FLOAT32, nor, nq * 4, 3), t3_geometry_set_attribute(g, T3_ATTR_NORMAL, a), t3_release(a);
    a = t3_attribute_new(T3_FLOAT32, AD.uv[k], nq * 4, 2), t3_geometry_set_attribute(g, T3_ATTR_UV, a), t3_release(a);
    a = t3_attribute_new(T3_UINT32, ix, nq * 6, 1), t3_geometry_set_index(g, a), t3_release(a);
    free(pos), free(nor), free(ix);
    t3_mesh *me = t3_mesh_new(g, AD.mat[k]);
    t3_release(g);
    char name[16];
    snprintf(name, sizeof name, "Ads%d", k);
    hang(parent, name, &me->base);
    t3_release(me);
  }
}

/* ── neon pieces ─────────────────────────────────────────────────────────────────────────────────── */

static pcol dk(pcol c, float k) { return pc_mul(c, k); }

/* thin glowing bars forming a rectangle outline in the plane z = z0 (x from -w/2 to w/2, y from y0 up) */
static void neon_frame(pm *b, float w, float h, float y0, float z0, float th, pcol col) {
  pm_layer(b, PL_GLOW);
  pm_beam(b, V(-w * 0.5f, y0, z0), V(w * 0.5f, y0, z0), th, col);
  pm_beam(b, V(-w * 0.5f, y0 + h, z0), V(w * 0.5f, y0 + h, z0), th, col);
  pm_beam(b, V(-w * 0.5f, y0, z0), V(-w * 0.5f, y0 + h, z0), th, col);
  pm_beam(b, V(w * 0.5f, y0, z0), V(w * 0.5f, y0 + h, z0), th, col);
}

/* invented glyphs: a few random strokes through a 3 x 3 grid of points, so a sign reads as writing without a word */
static void neon_glyphs(pm *b, int n, float cw, float ch, float x0, float y0, float z0, float th, pcol col, uint32_t seed) {
  pm_layer(b, PL_GLOW);
  for (int g = 0; g < n; g++) {
    uint32_t rs = seed + (uint32_t)g * 977u;
    int strokes = 2 + (int)(frand(&rs) * 2.5f);
    float gx = x0 + (float)g * cw * 1.25f;
    for (int s = 0; s < strokes; s++) {
      int a = (int)(frand(&rs) * 9.0f) % 9, c = (int)(frand(&rs) * 9.0f) % 9;
      if (a == c) c = (c + 4) % 9;
      t3_vec3 pa = V(gx + (float)(a % 3) * 0.5f * cw, y0 + (float)(a / 3) * 0.5f * ch, z0);
      t3_vec3 pc = V(gx + (float)(c % 3) * 0.5f * cw, y0 + (float)(c / 3) * 0.5f * ch, z0);
      pm_beam(b, pa, pc, th, col);
    }
  }
}

/* a sign in the local frame: x along, y up from the sign's lower edge, z out of the mounting plane.
 * kinds: 0 glyph line, 1 ring and bars, 2 arrow, 3 zigzag, 4 stacked bars */
static void neon_sign(pm *b, float w, float h, int kind, pcol c1, pcol c2, uint32_t seed) {
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_move(b, 0, 0, 0);
  pm_box(b, w + 0.25f, h + 0.25f, 0.16f, rgb(0.03f, 0.03f, 0.06f), rgb(0.06f, 0.05f, 0.1f));
  pm_pop(b);
  float z = 0.2f, th = 0.09f;
  neon_frame(b, w, h, 0.1f, z, th, glow(c1, 2.6f));
  switch (kind) {
  case 0: {
    int n = (int)(w / (h * 0.5f));
    if (n < 2) n = 2;
    if (n > 8) n = 8;
    float cw = (w - 0.6f) / ((float)n * 1.25f);
    neon_glyphs(b, n, cw, h * 0.6f, -w * 0.5f + 0.4f, h * 0.2f, z, th, glow(c2, 2.8f), seed);
    break;
  }
  case 1: {
    pm_push(b);
    pm_move(b, -w * 0.25f, 0.1f + h * 0.5f, z);
    pm_ring(b, h * 0.32f, th * 0.7f, 20, 5, glow(c2, 3.0f));
    pm_pop(b);
    for (int i = 0; i < 3; i++) pm_beam(b, V(0.0f, h * (0.28f + 0.22f * (float)i), z), V(w * 0.4f - (float)i * 0.25f, h * (0.28f + 0.22f * (float)i), z), th, glow(i == 1 ? c1 : c2, 2.6f));
    break;
  }
  case 2: {
    float cx = 0, cy = 0.1f + h * 0.5f, a = h * 0.32f;
    for (int i = 0; i < 3; i++) {
      float ox = -w * 0.3f + (float)i * w * 0.2f;
      pm_beam(b, V(cx + ox - a * 0.5f, cy + a, z), V(cx + ox + a * 0.5f, cy, z), th, glow(i == 1 ? c1 : c2, 3.0f));
      pm_beam(b, V(cx + ox - a * 0.5f, cy - a, z), V(cx + ox + a * 0.5f, cy, z), th, glow(i == 1 ? c1 : c2, 3.0f));
    }
    break;
  }
  case 3: {
    int n = 7;
    for (int i = 0; i < n; i++) {
      float x0 = -w * 0.4f + w * 0.8f * (float)i / (float)n, x1 = -w * 0.4f + w * 0.8f * (float)(i + 1) / (float)n;
      pm_beam(b, V(x0, 0.1f + h * (i & 1 ? 0.75f : 0.25f), z), V(x1, 0.1f + h * (i & 1 ? 0.25f : 0.75f), z), th, glow(c2, 3.0f));
    }
    break;
  }
  default: {
    for (int i = 0; i < 4; i++) {
      float len = w * (0.3f + 0.6f * frand(&seed));
      pm_beam(b, V(-w * 0.45f, 0.1f + h * (0.2f + 0.2f * (float)i), z), V(-w * 0.45f + len, 0.1f + h * (0.2f + 0.2f * (float)i), z), th * 1.2f, glow(i & 1 ? c1 : c2, 2.8f));
    }
    break;
  }
  }
  pm_layer(b, PL_MAIN);
}

/* a striped awning from a wall: along x for `len`, `out` metres out, hanging from height hgt down by `drop` */
static void awning(pm *b, float x0, float len, float out, float hgt, float drop, pcol a, pcol c, int stripes) {
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < stripes; i++) {
    float xa = x0 + len * (float)i / (float)stripes, xb = x0 + len * (float)(i + 1) / (float)stripes;
    pcol col = (i & 1) ? c : a;
    t3_vec3 p0 = V(xa, hgt, 0), p1 = V(xb, hgt, 0), p2 = V(xb, hgt - drop, out), p3 = V(xa, hgt - drop, out);
    pm_quad(b, p0, p3, p2, p1, col); /* seen from above and the road side */
    pm_quad(b, p0, p1, p2, p3, dk(col, 0.7f)); /* and from under */
    /* the valance */
    pm_quad(b, V(xa, hgt - drop, out), V(xa, hgt - drop - 0.28f, out), V(xb, hgt - drop - 0.28f, out), V(xb, hgt - drop, out), col);
    pm_quad(b, V(xa, hgt - drop, out), V(xb, hgt - drop, out), V(xb, hgt - drop - 0.28f, out), V(xa, hgt - drop - 0.28f, out), dk(col, 0.7f));
  }
}

/* a lamp on a gooseneck from the wall top: glowing head over the road */
static void gooseneck(pm *b, float x, float y, float reach, pcol lamp) {
  pm_layer(b, PL_METAL);
  pm_rod(b, V(x, y, -0.15f), V(x, y + 1.3f, -0.15f), 0.06f, 5, rgb(0.12f, 0.12f, 0.16f));
  pm_rod(b, V(x, y + 1.3f, -0.15f), V(x, y + 1.7f, reach * 0.55f), 0.05f, 5, rgb(0.12f, 0.12f, 0.16f));
  pm_rod(b, V(x, y + 1.7f, reach * 0.55f), V(x, y + 1.55f, reach), 0.05f, 5, rgb(0.12f, 0.12f, 0.16f));
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, x, y + 1.46f, reach);
  pm_box_c(b, 0.5f, 0.12f, 0.7f, glow(lamp, 4.5f));
  pm_pop(b);
  pm_layer(b, PL_MAIN);
}

/* a sign blade standing out from a wall over the road, face along the road: both faces lit */
static void blade_sign(pm *b, float x, float y0, float reach, float w, float h, int kind, pcol c1, pcol c2, uint32_t seed) {
  pm_layer(b, PL_METAL);
  pm_beam(b, V(x, y0 + h * 0.8f, 0), V(x, y0 + h * 0.8f, reach), 0.07f, rgb(0.1f, 0.1f, 0.14f));
  pm_beam(b, V(x, y0 + h * 0.2f, 0), V(x, y0 + h * 0.2f, reach), 0.07f, rgb(0.1f, 0.1f, 0.14f));
  pm_layer(b, PL_MAIN);
  for (int side = -1; side <= 1; side += 2) {
    pm_push(b);
    pm_move(b, x + (float)side * 0.12f, y0, reach - w * 0.5f);
    pm_roty(b, side > 0 ? 1.5707963f : -1.5707963f);
    neon_sign(b, w, h, kind, c1, c2, seed + (uint32_t)(side + 1) * 13u);
    pm_pop(b);
  }
}



/* ── the buildings along the road ────────────────────────────────────────────────────────────────── */

typedef struct tint_set { pcol wall0, wall1; pcol a, b; } tint_set;

static pcol window_colour(uint32_t *rs, const tint_set *ts) {
  float r = frand(rs);
  pcol c = r < 0.5f ? WARM : r < 0.64f ? ts->a : r < 0.78f ? ts->b : r < 0.9f ? hex(0xcfe8ff) : AMBER;
  return glow(c, 1.3f + 1.2f * frand(rs));
}

/* the windows of one face, in the face's own frame: x along, y up, z out of the face, face is w wide, y0..y1. Every cell has a
 * dark pane and a frame; a share of them are lit, floor by floor (an office keeps a whole floor on), with a cross of mullions. */
static void windows_face(pm *b, float w, float y0, float y1, float lit, const tint_set *ts, uint32_t seed) {
  uint32_t rs = seed;
  const float cw = 3.4f, ch = 3.8f;
  int nx = (int)((w - 1.6f) / cw), ny = (int)((y1 - y0) / ch);
  pcol glass = rgb(0.012f, 0.02f, 0.045f), frame = rgb(0.05f, 0.055f, 0.08f);
  for (int j = 0; j < ny; j++) {
    float floor_k = frand(&rs); /* this floor's share of lit windows */
    float p = fclampf(lit * (0.5f + 1.6f * floor_k), 0, 1);
    float y = y0 + ((float)j + 0.5f) * ch;
    for (int i = 0; i < nx; i++) {
      float x = -w * 0.5f + 0.8f + ((float)i + 0.5f) * (w - 1.6f) / (float)nx;
      bool on = frand(&rs) < p;
      float ww = 1.05f, hh = 1.3f;
      pm_layer(b, PL_MAIN);
      pm_quad(b, V(x - ww - 0.12f, y - hh - 0.12f, 0.03f), V(x + ww + 0.12f, y - hh - 0.12f, 0.03f), V(x + ww + 0.12f, y + hh + 0.12f, 0.03f), V(x - ww - 0.12f, y + hh + 0.12f, 0.03f), frame);
      pm_quad(b, V(x - ww, y - hh, 0.05f), V(x + ww, y - hh, 0.05f), V(x + ww, y + hh, 0.05f), V(x - ww, y + hh, 0.05f), glass);
      if (on) {
        pm_layer(b, PL_GLOW);
        pcol c = window_colour(&rs, ts);
        /* a curtain or a blind across part of the pane keeps the glow from being a flat tile */
        float drop = frand(&rs) < 0.4f ? 0.2f + 0.6f * frand(&rs) : 0.0f;
        pm_quad(b, V(x - ww, y - hh, 0.07f), V(x + ww, y - hh, 0.07f), V(x + ww, y + hh - drop * 2.0f * hh, 0.07f), V(x - ww, y + hh - drop * 2.0f * hh, 0.07f), c);
        pm_layer(b, PL_MAIN);
        pm_quad(b, V(x - 0.05f, y - hh, 0.09f), V(x + 0.05f, y - hh, 0.09f), V(x + 0.05f, y + hh, 0.09f), V(x - 0.05f, y + hh, 0.09f), frame);
        pm_quad(b, V(x - ww, y - 0.05f + 0.25f, 0.09f), V(x + ww, y - 0.05f + 0.25f, 0.09f), V(x + ww, y + 0.05f + 0.25f, 0.09f), V(x - ww, y + 0.05f + 0.25f, 0.09f), frame);
      }
    }
  }
  pm_layer(b, PL_MAIN);
}

/* one building: centre of its base footprint p, upright frame from the road sample a, len along the road, depth across,
 * height h from p up; the face toward the road looks along -side */
static void building(const tsample *a, t3_vec3 p, int side, float len, float depth, float h, uint32_t seed) {
  uint32_t rs = seed;
  static const tint_set sets[4] = {
    { { 0.10f, 0.11f, 0.17f }, { 0.20f, 0.21f, 0.30f }, { 0 }, { 0 } }, { { 0.14f, 0.09f, 0.18f }, { 0.28f, 0.18f, 0.34f }, { 0 }, { 0 } },
    { { 0.07f, 0.12f, 0.16f }, { 0.14f, 0.26f, 0.32f }, { 0 }, { 0 } }, { { 0.13f, 0.12f, 0.12f }, { 0.26f, 0.24f, 0.23f }, { 0 }, { 0 } } };
  int si = (int)(frand(&rs) * 3.99f);
  tint_set ts = sets[si];
  ts.wall0 = rgb(ts.wall0.r, ts.wall0.g, ts.wall0.b), ts.wall1 = rgb(ts.wall1.r, ts.wall1.g, ts.wall1.b);
  ts.a = si == 1 ? PINK : si == 2 ? CYAN : frand(&rs) < 0.5f ? CYAN : PINK;
  ts.b = si == 1 ? VIOLET : si == 2 ? LIME : VIOLET;
  int g = frand(&rs) < 0.07f ? GR_FLICK : GR_STEADY;
  pm *b = upright_frame(N.c[g], p, a);
  pm_layer(b, PL_MAIN);
  pm_box(b, depth, h, len, ts.wall0, ts.wall1);
  /* a plinth and a cornice give the box some scale */
  pm_push(b);
  pm_move(b, 0, 0, 0);
  pm_box(b, depth + 0.5f, 4.4f, len + 0.5f, dk(ts.wall0, 0.8f), dk(ts.wall1, 0.8f));
  pm_pop(b);
  /* a ledge every second floor, and a rib up the road face at each end */
  for (float ly = 7.0f + 4.6f; ly < h - 2.0f; ly += 7.6f) {
    pm_push(b);
    pm_move(b, 0, ly, 0);
    pm_box(b, depth + 0.35f, 0.3f, len + 0.35f, dk(ts.wall1, 0.8f), ts.wall1);
    pm_pop(b);
  }
  pm_push(b);
  pm_move(b, 0, h, 0);
  pm_box(b, depth + 0.7f, 0.7f, len + 0.7f, dk(ts.wall1, 0.9f), ts.wall1);
  /* rooftop: a stack or two, a water tank, a mast with a red lamp */
  int nr = 1 + (int)(frand(&rs) * 2.5f);
  for (int i = 0; i < nr; i++) {
    float rx = (frand(&rs) - 0.5f) * depth * 0.5f, rz = (frand(&rs) - 0.5f) * len * 0.6f;
    pm_push(b);
    pm_move(b, rx, 0.7f, rz);
    if (frand(&rs) < 0.5f) {
      float bx = 3.0f + 2.0f * frand(&rs), by = 2.0f + 2.0f * frand(&rs), bz = 3.0f + 2.5f * frand(&rs);
      pm_box(b, bx, by, bz, dk(ts.wall0, 1.2f), ts.wall1);
    } else pm_cyl(b, 1.6f, 1.6f, 2.6f, 10, dk(ts.wall0, 1.4f), ts.wall1);
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  float mx = (frand(&rs) - 0.5f) * depth * 0.4f, mz = (frand(&rs) - 0.5f) * len * 0.4f, mh = 5.0f + 9.0f * frand(&rs);
  pm_rod(b, V(mx, 0.7f, mz), V(mx, 0.7f + mh, mz), 0.12f, 5, rgb(0.15f, 0.15f, 0.2f));
  pm_pop(b);
  pm_pop(b);
  /* lit windows on the face toward the road and on the back, shop front and a sign band on the road face */
  const float lift = 7.0f; /* the block's base is this far under the road: the shop fronts start at the road */
  for (int face = 0; face < 2; face++) {
    pm *f = upright_frame(N.c[g], p, a);
    pm_roty(f, (face == 0 ? -(float)side : (float)side) * 1.5707963f);
    pm_move(f, 0, 0, depth * 0.5f);
    windows_face(f, len, lift + 4.8f, h - 1.0f, face == 0 ? 0.5f : 0.22f, &ts, seed + 311u * (uint32_t)(face + 1));
    if (face == 0) {
      /* ground floor: a lit shop front, dark piers between its panes */
      pm_layer(f, PL_GLOW);
      pcol shop = glow(frand(&rs) < 0.5f ? WARM : ts.a, 1.6f);
      float x0 = -len * 0.5f + 1.2f, x1 = len * 0.5f - 1.2f;
      pm_quad(f, V(x0, lift + 0.5f, 0.06f), V(x1, lift + 0.5f, 0.06f), V(x1, lift + 3.6f, 0.06f), V(x0, lift + 3.6f, 0.06f), shop);
      pm_layer(f, PL_MAIN);
      for (int k = 0; k <= 4; k++) {
        pm_push(f);
        pm_move(f, x0 + (x1 - x0) * (float)k / 4.0f, lift, 0.0f);
        pm_box(f, 0.4f, 3.8f, 0.35f, rgb(0.05f, 0.05f, 0.08f), rgb(0.08f, 0.08f, 0.12f));
        pm_pop(f);
      }
    }
    pm_pop(f);
    if (face == 0) {
      pm *sb = upright_frame(N.c[g == GR_STEADY && frand(&rs) < 0.3f ? GR_BUZZ : g], p, a);
      pm_roty(sb, -(float)side * 1.5707963f);
      pm_move(sb, 0, lift + 4.0f, depth * 0.5f);
      int style = (int)(frand(&rs) * 4.99f);
      pcol n1 = neon_pick(&rs), n2 = neon_pick(&rs);
      neon_sign(sb, fminf(len * 0.7f, 12.0f), 1.5f, style, n1, n2, seed + 99u);
      pm_pop(sb);
    }
  }
}

/* ── glow on the wet road: soft streaks under the signs, drawn additive just above the surface ───── */

typedef struct gbuf {
  float *p, *c;
  uint32_t *ix;
  int nv, ni, capv, capi;
} gbuf;
#define POOL_CHUNKS 24
static struct {
  gbuf b[GR_N][POOL_CHUNKS];
  t3_material *mat[GR_N];
} PO;

static void gbuf_quad_grid(gbuf *g, const t3_vec3 *pts, const pcol *cols) { /* a 5 x 5 grid of vertices, row-major */
  if (g->nv + 25 > g->capv) {
    g->capv = g->capv ? g->capv * 2 : 512;
    g->p = realloc(g->p, sizeof(float) * 3 * (size_t)g->capv), g->c = realloc(g->c, sizeof(float) * 3 * (size_t)g->capv);
  }
  if (g->ni + 96 > g->capi) g->capi = g->capi ? g->capi * 2 : 768, g->ix = realloc(g->ix, sizeof(uint32_t) * (size_t)g->capi);
  int b0 = g->nv;
  for (int i = 0; i < 25; i++) {
    g->p[(g->nv) * 3] = pts[i].x, g->p[g->nv * 3 + 1] = pts[i].y, g->p[g->nv * 3 + 2] = pts[i].z;
    g->c[g->nv * 3] = cols[i].r, g->c[g->nv * 3 + 1] = cols[i].g, g->c[g->nv * 3 + 2] = cols[i].b;
    g->nv++;
  }
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      uint32_t a = (uint32_t)(b0 + r * 5 + c), b = a + 1, d = a + 5, e = a + 6;
      g->ix[g->ni++] = a, g->ix[g->ni++] = d, g->ix[g->ni++] = b, g->ix[g->ni++] = b, g->ix[g->ni++] = d, g->ix[g->ni++] = e;
    }
}

/* a smear lying on the road beside the wall at sample a: a wet road mirrors a light as a streak that runs toward the
 * viewer, so it is long along the road and a few metres wide across it. half_w is the lateral reach, len the length. */
static void pool_add(int g, const tsample *a, int side, float x_along, float half_w, float len, pcol col, float gain) {
  int chunk = (int)(a->s / 180.0f);
  if (chunk >= POOL_CHUNKS) chunk = POOL_CHUNKS - 1;
  t3_vec3 base = vadd(vadd(a->p, vmul(a->u, 0.045f)), vmul(a->f, x_along));
  t3_vec3 pts[25];
  pcol cols[25];
  static const float along_at[5] = { -1.0f, -0.62f, -0.28f, 0.0f, 0.3f }, along_k[5] = { 0.0f, 0.22f, 0.62f, 1.0f, 0.0f };
  static const float lat_at[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, lat_k[5] = { 0.35f, 0.8f, 1.0f, 0.5f, 0.0f };
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 5; c++) {
      float lat = (float)side * (a->hw - 0.15f - half_w * lat_at[c]);
      pts[r * 5 + c] = vadd(vadd(base, vmul(a->r, lat)), vmul(a->f, len * along_at[r]));
      float k = along_k[r] * lat_k[c] * gain;
      cols[r * 5 + c] = (pcol){ col.r * k, col.g * k, col.b * k };
    }
  gbuf_quad_grid(&PO.b[g][chunk], pts, cols);
}

static void pools_finish(cw_node parent) {
  for (int g = 0; g < GR_N; g++) {
    t3_material *m = t3_mesh_basic_material_new(0xffffff);
    m->vertex_colors = true;
    m->transparent = true;
    m->blending = T3_ADDITIVE_BLENDING;
    m->depth_write = false;
    m->fog = true;
    PO.mat[g] = m;
    for (int c = 0; c < POOL_CHUNKS; c++) {
      gbuf *b = &PO.b[g][c];
      if (!b->nv) continue;
      t3_geometry *geo = t3_geometry_new();
      t3_attribute *at = t3_attribute_new(T3_FLOAT32, b->p, b->nv, 3);
      t3_geometry_set_attribute(geo, T3_ATTR_POSITION, at), t3_release(at);
      at = t3_attribute_new(T3_FLOAT32, b->c, b->nv, 3), t3_geometry_set_attribute(geo, T3_ATTR_COLOR, at), t3_release(at);
      at = t3_attribute_new(T3_UINT32, b->ix, b->ni, 1), t3_geometry_set_index(geo, at), t3_release(at);
      t3_mesh *me = t3_mesh_new(geo, m);
      me->base.render_order = 3;
      t3_release(geo);
      char name[24];
      snprintf(name, sizeof name, "Glow%d_%d", g, c);
      hang(parent, name, &me->base);
      t3_release(me);
      free(b->p), free(b->c), free(b->ix);
      *b = (gbuf){ 0 }; /* the next race builds into empty buffers */
    }
  }
}

/* ── the walls: shop fronts, posters, awnings, blades and lamps along both sides ─────────────────── */

static bool walled(const tsample *a, int side) {
  return !(a->flags & (TF_GAP | TF_TUNNEL)) && (a->flags & (side < 0 ? TF_WALL_L : TF_WALL_R)) != 0;
}
static bool in_run(run r, float s, float pad) { return s >= r.s0 - pad && s <= r.s1 + pad; }

/* a stretch with fixtures hanging over the road would clutter the corkscrew and the jump: they keep clear */
static bool keep_clear(float s) { return in_run(L.cork, s, 30) || in_run(L.gap, s, 40); }

static void dress_walls(const track *t, uint32_t seed) {
  uint32_t rs = seed ^ 0x51ed;
  for (int side = -1; side <= 1; side += 2) {
    float s = 8 + frand(&rs) * 4;
    int bay = 0;
    while (s < t->length - 8) {
      tsample a = track_at(t, s);
      float step = 9.0f + 6.0f * frand(&rs);
      bay++;
      if (!walled(&a, side)) {
        s += 6;
        continue;
      }
      float r = frand(&rs);
      bool clear = keep_clear(s);
      /* flush things on the wall face: a neon sign, or a poster */
      if (r < 0.34f) {
        int g = frand(&rs) < 0.2f ? GR_FLICK : frand(&rs) < 0.5f ? GR_BUZZ : GR_STEADY;
        pm *b = wall_frame(N.c[g], &a, side, 0.0f, 0.02f);
        float w = 2.6f + 3.0f * frand(&rs), h = 0.9f + 0.5f * frand(&rs);
        pcol c1 = neon_pick(&rs), c2 = neon_pick(&rs);
        pm_move(b, 0, 1.55f + 0.3f * frand(&rs), 0);
        neon_sign(b, w, h, (int)(frand(&rs) * 4.99f), c1, c2, (uint32_t)(s * 31.0f) + (uint32_t)side * 7u);
        pm_pop(b);
        pool_add(g, &a, side, 0, 2.2f, 6.0f + 4.0f * frand(&rs), c1, 0.5f);
      } else if (r < 0.58f) {
        int kind = (int)(frand(&rs) * (float)AD_N) % AD_N;
        float w = 2.6f, h = 1.3f;
        t3_vec3 p = vadd(vadd(a.p, vmul(a.r, (float)side * (a.hw - 0.05f))), vmul(a.u, 1.1f + 0.5f * frand(&rs)));
        t3_vec3 n = vmul(a.r, (float)-side);
        ad_add(kind, p, n, a.u, w, h);
      }
      /* the awning over the wall top, and the blade sign */
      if (!clear && frand(&rs) < 0.45f) {
        pm *b = wall_frame(N.c[GR_STEADY], &a, side, 0.0f, 0.0f);
        pcol c1 = frand(&rs) < 0.5f ? hex(0xd02070) : hex(0x1aa6c0), c2 = frand(&rs) < 0.5f ? hex(0xe8e0f0) : hex(0x20204a);
        awning(b, -3.2f, 6.4f, 1.15f, 3.55f, 0.55f, c1, c2, 8);
        pm_pop(b);
      }
      if (!clear && frand(&rs) < 0.30f) {
        int g = frand(&rs) < 0.3f ? GR_FLICK : GR_BUZZ;
        pm *b = wall_frame(N.c[g], &a, side, 0.0f, 0.0f);
        pcol c1 = neon_pick(&rs), c2 = neon_pick(&rs);
        float by = 4.25f + 0.5f * frand(&rs), bh = 2.6f + 1.2f * frand(&rs);
        int style = (int)(frand(&rs) * 4.99f);
        blade_sign(b, 0, by, 1.7f, 1.7f, bh, style, c1, c2, (uint32_t)(s * 17.0f));
        pm_pop(b);
        pool_add(g, &a, side, 0, 2.4f, 8.0f, c2, 0.45f);
      }
      if (!clear && ((bay % 4) == 0)) {
        pm *b = wall_frame(N.c[GR_STEADY], &a, side, 0.0f, 0.0f);
        pcol lamp = frand(&rs) < 0.5f ? WARM : hex(0xffb0e0);
        gooseneck(b, 0, 3.2f, 1.7f, lamp);
        pm_pop(b);
        pool_add(GR_STEADY, &a, side, 0, 2.8f, 9.0f, lamp, 0.4f);
      }
      s += step;
    }
  }
}

/* ── placing finished models ─────────────────────────────────────────────────────────────────────── */

static cw_node model_node(cw_node parent, const char *name, prop_model m, bool shadow) {
  t3_object *grp = t3_group_new();
  for (int l = 0; l < PL_COUNT; l++) {
    if (!m.g[l]) continue;
    t3_mesh *me = t3_mesh_new(m.g[l], prop_material(l, -1, (pcol){ 1, 1, 1 }, 1));
    me->base.cast_shadow = shadow && l != PL_GLOW;
    me->base.receive_shadow = true;
    t3_object_add(grp, me);
    t3_release(me);
  }
  prop_model_free(&m);
  cw_node n = hang(parent, name, grp);
  t3_release(grp);
  return n;
}


/* where the blocks stand: per 70 m of road and per side one of: a street canyon of tall blocks close to the wall,
 * a row of low shops, or open water (nothing near, the sea and the far towers show) */
static int zone_mode(int side, float s) {
  uint32_t h = hash32((uint32_t)(side + 3) * 7919u + (uint32_t)(s / 70.0f) * 104729u + 17u);
  float r = (float)(h & 0xffff) / 65535.0f;
  return r < 0.55f ? 0 : r < 0.72f ? 1 : 2;
}

static bool near_other_road(const track *t, t3_vec3 p, float s, float need) {
  int idx;
  float d2;
  idx = road_nearest(t, p.x, p.z, &d2);
  float ds = fabsf(t->s[idx].s - s);
  if (ds > t->length * 0.5f) ds = t->length - ds;
  return ds > 60.0f && d2 < need;
}

static void street_blocks(const track *t, uint32_t seed) {
  uint32_t rs = seed ^ 0xb10c;
  for (int side = -1; side <= 1; side += 2) {
    float s = 6;
    while (s < t->length - 30) {
      int mode = zone_mode(side, s);
      float len = 22 + 18 * frand(&rs);
      if (mode == 2 || in_run(L.plate, s + len * 0.5f, 40) || in_run(L.cork, s + len * 0.5f, 60) || in_run(L.gap, s + len * 0.5f, 50)) {
        s += 30;
        continue;
      }
      tsample a = track_at(t, s + len * 0.5f);
      if (!walled(&a, side)) {
        s += 20;
        continue;
      }
      float depth = mode == 0 ? 12 + 8 * frand(&rs) : 9 + 4 * frand(&rs);
      float off = mode == 0 ? 2.2f + 1.5f * frand(&rs) : 3.0f + 5.0f * frand(&rs);
      float h;
      if (mode == 0) {
        float h1 = frand(&rs), h2 = frand(&rs), h3 = frand(&rs);
        h = 20 + 36 * h1 * h2 + (h3 < 0.12f ? 30 : 0);
      } else h = 7 + 7 * frand(&rs);
      t3_vec3 p = vadd(a.p, vmul(a.r, (float)side * (a.hw + off + depth * 0.5f)));
      float base_y = a.p.y - 7.0f;
      p.y = base_y;
      /* never over another stretch of road, nor too near it */
      bool ok = true;
      for (int k = 0; k < 5 && ok; k++) {
        float lat = (float)side * (a.hw + off + depth * (float)(k % 2));
        float along = (k < 2 ? -len * 0.5f : len * 0.5f);
        if (k == 4) along = 0;
        tsample c = track_at(t, s + len * 0.5f + along);
        t3_vec3 q = vadd(c.p, vmul(c.r, lat));
        if (near_other_road(t, q, s, 12 * 12)) ok = false;
      }
      if (!ok) {
        s += 24;
        continue;
      }
      building(&a, p, side, len, depth, h + (a.p.y - base_y), seed + (uint32_t)(s * 13.0f) + (uint32_t)side);
      s += len + (frand(&rs) < 0.3f ? 5.0f + 8.0f * frand(&rs) : 0.6f);
    }
  }
}

/* ── lights: a few real ones at the set pieces, everything else glows ────────────────────────────── */

static cw_node add_omni(cw_node parent, const char *name, t3_vec3 at, pcol c, float energy, float range) {
  cw_node l = cw_node_new("OmniLight3D", name);
  cw_node_add_child(parent, l);
  cw_node3d_set_position(l, at);
  /* the colours here are linear already (pcol): the node wants screen colours, so pass them back through the curve */
  cw_node_set(l, "color", cw_val_color(c.r, c.g, c.b, 1));
  cw_node_set(l, "energy", cw_val_float(energy));
  cw_node_set(l, "range", cw_val_float(range));
  cw_node_set(l, "decay", cw_val_float(2));
  cw_node_set(l, "fog_energy", cw_val_float(0));
  return l;
}

/* ── set piece 1: the start line under a giant neon arch ─────────────────────────────────────────── */

/* an elliptical tube in the road frame (x across, y up, z along): angles a0..a1 round centre (0, cy), semi axes ax, ay */
static void arch_tube(pm *b, float cy, float ax, float ay, float r, float a0, float a1, int segs, pcol col) {
  for (int i = 0; i < segs; i++) {
    float t0 = lerpf(a0, a1, (float)i / (float)segs), t1 = lerpf(a0, a1, (float)(i + 1) / (float)segs);
    pm_rod(b, V(cosf(t0) * ax, cy + sinf(t0) * ay, 0), V(cosf(t1) * ax, cy + sinf(t1) * ay, 0), r, 6, col);
  }
}

static void chequer_banner(pm *b, float x0, float x1, float y0, float y1, float z, float cell, bool front, pcol a, pcol c) {
  int nx = (int)((x1 - x0) / cell + 0.5f), ny = (int)((y1 - y0) / cell + 0.5f);
  float dx = (x1 - x0) / (float)nx, dy = (y1 - y0) / (float)ny;
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      pcol col = ((i + j) & 1) ? a : c;
      float xa = x0 + dx * (float)i, xb = xa + dx, ya = y0 + dy * (float)j, yb = ya + dy;
      if (front) pm_quad(b, V(xa, ya, z), V(xb, ya, z), V(xb, yb, z), V(xa, yb, z), col);
      else pm_quad(b, V(xb, ya, z), V(xa, ya, z), V(xa, yb, z), V(xb, yb, z), col);
    }
}

static void start_arch(const track *t, cw_node root) {
  tsample a = track_at(t, L.start_s + 26.0f);
  float W = a.hw + 5.0f;
  pm *b = frame_in(N.c[GR_STEADY], a.p, a.r, a.u, a.f);
  /* two pylons: dark ribbed towers with a lit seam up each face */
  for (int k = -1; k <= 1; k += 2) {
    pm_layer(b, PL_MAIN);
    pm_push(b);
    pm_move(b, (float)k * W, -2.0f, 0);
    pm_box(b, 3.2f, 27.0f, 3.2f, rgb(0.06f, 0.06f, 0.1f), rgb(0.14f, 0.12f, 0.22f));
    pm_box(b, 4.2f, 1.2f, 4.2f, rgb(0.1f, 0.1f, 0.16f), rgb(0.18f, 0.16f, 0.26f));
    pm_pop(b);
    pm_layer(b, PL_GLOW);
    for (int s2 = -1; s2 <= 1; s2 += 2) {
      pm_beam(b, V((float)k * W + (float)s2 * 1.62f, 0.5f, -0.8f), V((float)k * W + (float)s2 * 1.62f, 24.0f, -0.8f), 0.22f, glow(CYAN, 3.0f));
      pm_beam(b, V((float)k * W + (float)s2 * 1.62f, 0.5f, 0.8f), V((float)k * W + (float)s2 * 1.62f, 24.0f, 0.8f), 0.22f, glow(CYAN, 3.0f));
    }
    pm_beam(b, V((float)k * W, 0.5f, -1.62f), V((float)k * W, 24.0f, -1.62f), 0.3f, glow(PINK, 3.0f));
    pm_beam(b, V((float)k * W, 0.5f, 1.62f), V((float)k * W, 24.0f, 1.62f), 0.3f, glow(PINK, 3.0f));
  }
  pm_pop(b);
  /* the arch itself is two rings that breathe together */
  pm *c = frame_in(N.c[GR_PULSE], a.p, a.r, a.u, a.f);
  pm_layer(c, PL_GLOW);
  arch_tube(c, 22.0f, W, 8.0f, 0.62f, 0.0f, 3.14159265f, 40, glow(PINK, 3.4f));
  arch_tube(c, 22.0f, W - 1.8f, 6.2f, 0.34f, 0.0f, 3.14159265f, 36, glow(CYAN, 3.2f));
  arch_tube(c, 22.0f, W + 1.6f, 9.6f, 0.2f, 0.0f, 3.14159265f, 44, glow(VIOLET, 3.0f));
  pm_pop(c);
  /* a chequered banner across the springing line, both faces, in dark and bright glass */
  pm *d = frame_in(N.c[GR_STEADY], a.p, a.r, a.u, a.f);
  pm_layer(d, PL_MAIN);
  pm_beam(d, V(-W, 21.0f, 0), V(W, 21.0f, 0), 0.7f, rgb(0.08f, 0.08f, 0.12f));
  chequer_banner(d, -W + 1.2f, W - 1.2f, 22.0f, 25.2f, -0.45f, 1.6f, false, rgb(0.03f, 0.03f, 0.05f), rgb(0.9f, 0.9f, 0.95f));
  pm_layer(d, PL_GLOW);
  chequer_banner(d, -W + 1.2f, W - 1.2f, 22.0f, 25.2f, -0.46f, 1.6f, false, glow(rgb(0.02f, 0.02f, 0.04f), 0.1f), glow(rgb(1.0f, 0.95f, 1.0f), 1.3f));
  pm_pop(d);
  /* the screen under the banner: a picture facing the grid */
  ad_add(AD_SUN, vadd(vadd(a.p, vmul(a.u, 28.2f)), vmul(a.f, -0.8f)), vmul(a.f, -1), a.u, 20.0f, 10.0f);
  ad_add(AD_BURST, vadd(vadd(a.p, vmul(a.u, 28.2f)), vmul(a.f, 0.8f)), a.f, a.u, 20.0f, 10.0f);
  /* the lights: a pink and a cyan from the pylon tops, over the grid and the first metres */
  add_omni(root, "ArchPink", vadd(vadd(a.p, vmul(a.r, -W + 3.0f)), V(0, 16, -6)), PINK, 520, 46);
  add_omni(root, "ArchCyan", vadd(vadd(a.p, vmul(a.r, W - 3.0f)), V(0, 16, -6)), CYAN, 520, 46);
}

/* ── the giant neon wheel: a ferris wheel for a sign, turning slowly over the flooded middle ───────── */

static cw_node WHEEL;
static t3_vec3 WHEEL_AT; /* the middle of the big wheel (the towers keep clear of it) */

static void build_wheel(const track *t, cw_node root) {
  /* find a spot in the middle that is far from every road, facing the start line */
  t3_vec3 c = V(L.cx, 0, L.cz);
  float gap = road_gap(t, c.x, c.z, NULL);
  const float R = 60.0f, H = 74.0f;
  tsample st = track_at(t, L.start_s);
  if (gap < R + 40) {
    /* slide toward the start line's side of the middle until there is room */
    for (int i = 0; i < 20 && gap < R + 40; i++) c.x += (st.p.x - c.x) * -0.05f, c.z += (st.p.z - c.z) * -0.05f, gap = road_gap(t, c.x, c.z, NULL);
  }
  float yaw = atan2f(st.p.x - c.x, st.p.z - c.z); /* the wheel's +z faces the start */
  WHEEL_AT = c;
  cw_info("drowned: wheel at %.0f %.0f, road gap %.0f", c.x, c.z, gap);
  /* the turning part */
  pm *b = pm_new(77);
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_ring(b, R, 1.5f, 72, 8, glow(PINK, 3.2f));
  pm_pop(b);
  pm_push(b);
  pm_ring(b, R * 0.66f, 0.95f, 56, 6, glow(CYAN, 3.0f));
  pm_pop(b);
  pm_push(b);
  pm_ring(b, R * 0.30f, 0.8f, 36, 6, glow(VIOLET, 3.2f));
  pm_pop(b);
  const int nsp = 18;
  for (int i = 0; i < nsp; i++) {
    float an = 6.2831853f * (float)i / (float)nsp;
    pm_beam(b, V(0, 0, 0.0f), V(cosf(an) * R, sinf(an) * R, 0), 0.6f, glow(i & 1 ? CYAN : PINK, 2.8f));
    /* a pod at the rim: a lit bubble */
    pm_push(b);
    pm_move(b, cosf(an + 0.17f) * R, sinf(an + 0.17f) * R, 0);
    pm_ball(b, 3.4f, 3.4f, 3.4f, 1, 0.0f, glow(neon_pick(&(uint32_t){ 100u + (uint32_t)i * 7u }), 2.4f), glow(WARM, 2.2f));
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  pm_push(b);
  pm_move(b, 0, 0, 0);
  pm_ball(b, 4.5f, 4.5f, 4.5f, 1, 0.0f, rgb(0.2f, 0.2f, 0.28f), rgb(0.3f, 0.3f, 0.4f));
  pm_pop(b);
  prop_model wheel = pm_finish(b);
  /* the frame it stands in: two A legs and a base, under the sea and above */
  pm *f = pm_new(78);
  pm_layer(f, PL_METAL);
  for (int k = -1; k <= 1; k += 2) {
    pm_beam(f, V((float)k * 3.0f, 0, 3.4f), V((float)k * 34.0f, -H - 6.0f, 14.0f), 2.4f, rgb(0.16f, 0.16f, 0.22f));
    pm_beam(f, V((float)k * 3.0f, 0, 3.4f), V((float)k * 34.0f, -H - 6.0f, -14.0f), 2.4f, rgb(0.16f, 0.16f, 0.22f));
    pm_beam(f, V((float)k * 3.0f, 0, -3.4f), V((float)k * 34.0f, -H - 6.0f, 14.0f), 2.4f, rgb(0.16f, 0.16f, 0.22f));
  }
  pm_layer(f, PL_GLOW);
  for (int k = -1; k <= 1; k += 2) pm_beam(f, V((float)k * 3.0f, -2.0f, 4.8f), V((float)k * 33.0f, -H - 4.0f, 15.0f), 0.5f, glow(CYAN, 2.4f));
  prop_model frame = pm_finish(f);
  cw_node root2 = cw_node_new("Node3D", "Wheel");
  cw_node_add_child(root, root2);
  cw_node3d_set_position(root2, V(c.x, H - 4.0f, c.z));
  cw_node3d_set_rotation(root2, V(0, yaw, 0));
  model_node(root2, "WheelFrame", frame, true);
  WHEEL = model_node(root2, "WheelTurn", wheel, false);
  cw_node3d_set_position(WHEEL, V(0, 0, 0));
}


/* ── set piece 2: the fords. Standing water with neon in it, tram wires over it, drowned traffic beside it ── */

static void tram_wires(const track *t, float s0, float s1) {
  pcol steel = rgb(0.1f, 0.1f, 0.14f), wire = rgb(0.02f, 0.02f, 0.03f);
  t3_vec3 prev = V(0, 0, 0);
  bool have = false;
  for (float s = s0; s <= s1; s += 22.0f) {
    tsample a = track_at(t, s);
    float H = 9.0f;
    pm *b = frame_in(N.c[GR_STEADY], a.p, a.r, a.u, a.f);
    pm_layer(b, PL_METAL);
    for (int k = -1; k <= 1; k += 2) {
      pm_beam(b, V((float)k * (a.hw + 0.6f), 0, 0), V((float)k * (a.hw + 0.6f), H, 0), 0.3f, steel);
      pm_beam(b, V((float)k * (a.hw + 0.6f), H, 0), V((float)k * (a.hw - 0.4f), H + 0.5f, 0), 0.16f, steel);
    }
    /* the span across the road, and two hanging wires along it */
    pm_cable(b, V(-(a.hw - 0.4f), H + 0.5f, 0), V(a.hw - 0.4f, H + 0.5f, 0), 0.5f, 0.05f, wire);
    pm_layer(b, PL_GLOW);
    for (int k = -1; k <= 1; k += 2) { /* a lamp on each pole head */
      pm_push(b);
      pm_move(b, (float)k * (a.hw + 0.6f), H + 0.2f, 0);
      pm_ball(b, 0.35f, 0.35f, 0.35f, 0, 0, glow(k < 0 ? CYAN : PINK, 4.0f), glow(WARM, 4.0f));
      pm_pop(b);
    }
    pm_pop(b);
    t3_vec3 top = vadd(vadd(a.p, vmul(a.u, H + 0.5f)), vmul(a.r, 0.0f));
    if (have) {
      pm *c = pmc_at(N.c[GR_STEADY], top);
      pm_push(c);
      pm_layer(c, PL_METAL);
      pm_cable(c, prev, top, 0.7f, 0.04f, wire);
      pm_pop(c);
    }
    prev = top, have = true;
  }
}

/* a drowned tram on its side in the water beside the road, windows still lit */
static void tram_wreck(const tsample *a, int side, float lat, uint32_t seed) {
  uint32_t rs = seed;
  t3_vec3 p = vadd(a->p, vmul(a->r, (float)side * (a->hw + lat)));
  p.y = SEA_Y - 1.0f;
  pm *b = upright_frame(N.c[GR_STEADY], p, a);
  pm_roty(b, (frand(&rs) - 0.5f) * 0.4f);
  pm_rotz(b, (float)side * (0.28f + 0.2f * frand(&rs)));
  pm_layer(b, PL_MAIN);
  pm_box(b, 3.2f, 3.3f, 15.0f, rgb(0.55f, 0.1f, 0.25f), rgb(0.7f, 0.2f, 0.35f));
  pm_push(b);
  pm_move(b, 0, 3.3f, 0);
  pm_box(b, 3.0f, 0.3f, 14.6f, rgb(0.5f, 0.5f, 0.55f), rgb(0.6f, 0.6f, 0.65f));
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  for (int sd = -1; sd <= 1; sd += 2)
    for (int i = 0; i < 6; i++) {
      float z = -6.0f + 2.4f * (float)i;
      if (frand(&rs) < 0.25f) continue;
      pm_quad(b, V((float)sd * 1.62f, 1.6f, z - 0.9f), V((float)sd * 1.62f, 1.6f, z + 0.9f), V((float)sd * 1.62f, 2.7f, z + 0.9f), V((float)sd * 1.62f, 2.7f, z - 0.9f), glow(frand(&rs) < 0.6f ? WARM : CYAN, 1.8f));
    }
  pm_layer(b, PL_METAL);
  pm_beam(b, V(0, 3.5f, -2), V(0, 5.6f, -1), 0.14f, rgb(0.15f, 0.15f, 0.2f));
  pm_beam(b, V(-1.3f, 5.6f, -1), V(1.3f, 5.6f, -1), 0.12f, rgb(0.15f, 0.15f, 0.2f));
  pm_pop(b);
}

static float open_zone_mask(float x, float z, void *ud) {
  (void)ud;
  const track *t = L.t;
  float d;
  int i = road_nearest(t, x, z, &d);
  const tsample *a = &t->s[i];
  float dx = x - a->p.x, dz = z - a->p.z;
  int side = (dx * a->r.x + dz * a->r.z) < 0 ? -1 : 1;
  return zone_mode(side, a->s) == 2 ? 1.0f : 0.0f;
}

static void flood_dressing(const track *t, cw_node root, uint32_t seed) {
  /* fords: a film of water with a restless surface that holds the neon */
  water_opts wf = water_defaults();
  wf.color = rgb(0.2f, 0.45f, 0.6f);
  wf.deep = rgb(0.03f, 0.06f, 0.14f);
  wf.clarity = 0.45f;
  wf.reflect = 1.0f;
  wf.foam = 0.7f;
  wf.flow_x = 0.2f, wf.flow_z = 0.05f;
  water_along_track(root, t, 0.2f, 0.12f, &wf);
  for (int i = 0; i < L.nwater; i++) {
    tram_wires(t, L.water[i].s0 - 20, L.water[i].s1 + 20);
    /* the drowned traffic: a tram or two in the water behind the wall, a chain of lamps through the ford */
    for (int k = 0; k < 2; k++) {
      tsample a = track_at(t, L.water[i].s0 + 25 + 55 * (float)k);
      tram_wreck(&a, k ? 1 : -1, 8.0f + 6.0f * (float)k, seed + (uint32_t)(i * 5 + k));
    }
  }
  /* the sunken street: things standing and floating in the sea wherever the road is open to it */
  prop_set *ps = ps_new(root, "Sunken");
  scatter_opts so = scatter_defaults();
  so.kinds[0] = PK_WRECK_CAR, so.weight[0] = 3;
  so.kinds[1] = PK_DEBRIS, so.weight[1] = 4;
  so.kinds[2] = PK_STREET_LAMP, so.weight[2] = 3;
  so.kinds[3] = PK_BOLLARD, so.weight[3] = 2;
  so.kinds[4] = PK_BARREL, so.weight[4] = 1;
  so.kinds[5] = PK_NEON_SIGN, so.weight[5] = 2;
  so.kinds[6] = PK_CONTAINER, so.weight[6] = 0.6f;
  so.nkinds = 7;
  so.spacing = 16, so.lat_min = 3, so.lat_max = 38, so.lat_curve = 1.6f;
  so.margin = 3, so.base_y = SEA_Y - 0.45f, so.sink = 0.0f, so.seed = seed ^ 0x5eed;
  so.mask = open_zone_mask;
  so.scale_min = 0.9f, so.scale_max = 1.5f;
  scatter_along(ps, t, &so);
  ps_finish(ps);
}


/* ── set piece 3: the glass tube under the lake ────────────────────────────────────────────────────
 * The road drops through the sea into a glass tube lying on the lake bed. Outside the glass: murky green water with a
 * storm-lit ceiling of rippling light, shafts down from it, fish, a manta overhead, jellyfish, a drowned bus with its
 * windows still lit, neon signs standing on the bed. Inside: ribs and lamps, and the gates at either end. */

static t3_material *basic_add(float r, float g, float b, bool vcol, t3_texture *map) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = (t3_color){ r, g, b };
  m->transparent = true;
  m->blending = T3_ADDITIVE_BLENDING;
  m->depth_write = false;
  m->vertex_colors = vcol;
  m->side = T3_DOUBLE_SIDE;
  if (map) t3_material_set_map(m, map);
  return m;
}

/* soft caustic light: bright wandering lines, tiles seamlessly */
static t3_texture *caustic_texture(void) {
  const int W = 256;
  uint8_t *px = malloc((size_t)W * W * 4);
  for (int j = 0; j < W; j++)
    for (int i = 0; i < W; i++) {
      float u = (float)i / (float)W * 6.2831853f, v = (float)j / (float)W * 6.2831853f;
      /* three layers of ridged interference, each periodic */
      float a = sinf(u * 2 + 1.7f * sinf(v * 1 + 0.5f)) + sinf(v * 2 + 1.3f * sinf(u * 2 + 1.0f));
      float b = sinf(u * 3 - v * 2 + 2.0f * sinf(v * 3)) + sinf(v * 3 + u * 1 + 1.5f * sinf(u * 1 + 2.0f));
      float r1 = 1.0f - fabsf(a * 0.5f), r2 = 1.0f - fabsf(b * 0.5f);
      float c = powf(fclampf(r1 * r2, 0, 1), 5.0f) * 1.6f + 0.05f;
      uint8_t k = (uint8_t)(fclampf(c, 0, 1) * 255.0f);
      uint8_t *p = px + ((size_t)j * W + i) * 4;
      p[0] = k, p[1] = k, p[2] = k, p[3] = 255;
    }
  t3_texture *t = t3_texture_new(W, W, px);
  free(px);
  t->wrap_s = t->wrap_t = T3_REPEAT;
  return t;
}

static struct {
  t3_vec3 a, b;        /* the dip's two ends */
  float prof[65];      /* road height along the axis, 0..1 */
  float bed_below;     /* the bed lies this far under the road */
  float r0, r1;        /* flat bed within r0 of the axis, back to street level at r1 */
  t3_texture *caustic;
  t3_material *ceil_mat, *shaft_mat;
  cw_node shaft[8];
  int nshaft;
  float shaft_ph[8];
  cw_node school[8], manta, jelly[5], bus_light;
  int nschool, njelly;
  struct { float cx, cz, r, y, w, ph, sz; } sc[8];
} LK;

static float lake_prof(float u) {
  float f = fclampf(u, 0, 1) * 64.0f;
  int i = (int)f;
  if (i >= 64) return LK.prof[64];
  return lerpf(LK.prof[i], LK.prof[i + 1], f - (float)i);
}

static float lake_axis_dist(float x, float z, float *road_y) {
  float abx = LK.b.x - LK.a.x, abz = LK.b.z - LK.a.z, l2 = abx * abx + abz * abz;
  float t = l2 > 1e-6f ? ((x - LK.a.x) * abx + (z - LK.a.z) * abz) / l2 : 0;
  t = fclampf(t, 0, 1);
  float px = LK.a.x + abx * t, pz = LK.a.z + abz * t;
  if (road_y) *road_y = lake_prof(t);
  return sqrtf((x - px) * (x - px) + (z - pz) * (z - pz));
}

static float city_ground(float x, float z, void *ud) {
  (void)ud;
  float h = STREET_BED + ground_noise(x, z, 90, 1.6f, 3, 11);
  float ry;
  float d = lake_axis_dist(x, z, &ry);
  float k = 1.0f - cw_smoothstep(LK.r0, LK.r1, d);
  if (k > 0) {
    float bed = fminf(L.lowest_y - LK.bed_below, STREET_BED);
    /* the bed follows the road down and up, a good way below it */
    float follow = fminf(ry - LK.bed_below, STREET_BED);
    bed = lerpf(follow, bed, 0.35f) + ground_noise(x, z, 40, 0.7f, 2, 29);
    h = lerpf(h, bed, k);
  }
  return h;
}

static float lake_mask(float x, float z, void *ud) {
  (void)ud;
  float d = lake_axis_dist(x, z, NULL);
  return d < LK.r0 + 18.0f ? 1.0f : 0.0f;
}

/* an open cone of light, bright at the top and gone at the bottom (vertex colour) */
static t3_geometry *shaft_geometry(float rtop, float rbot, float h, int segs) {
  float *pos = malloc(sizeof(float) * 3 * 2 * (size_t)(segs + 1)), *col = malloc(sizeof(float) * 3 * 2 * (size_t)(segs + 1));
  uint32_t *ix = malloc(sizeof(uint32_t) * 6 * (size_t)segs);
  int n = 0;
  for (int i = 0; i <= segs; i++) {
    float an = 6.2831853f * (float)i / (float)segs, c = cosf(an), s = sinf(an);
    pos[n * 3] = c * rtop, pos[n * 3 + 1] = 0, pos[n * 3 + 2] = s * rtop;
    col[n * 3] = col[n * 3 + 1] = col[n * 3 + 2] = 1.0f, n++;
    pos[n * 3] = c * rbot, pos[n * 3 + 1] = -h, pos[n * 3 + 2] = s * rbot;
    col[n * 3] = col[n * 3 + 1] = col[n * 3 + 2] = 0.0f, n++;
  }
  int ni = 0;
  for (int i = 0; i < segs; i++) {
    uint32_t a = (uint32_t)(i * 2);
    ix[ni++] = a, ix[ni++] = a + 1, ix[ni++] = a + 2, ix[ni++] = a + 1, ix[ni++] = a + 3, ix[ni++] = a + 2;
  }
  t3_geometry *g = t3_geometry_new();
  t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, n, 3);
  t3_geometry_set_attribute(g, T3_ATTR_POSITION, at), t3_release(at);
  at = t3_attribute_new(T3_FLOAT32, col, n, 3), t3_geometry_set_attribute(g, T3_ATTR_COLOR, at), t3_release(at);
  at = t3_attribute_new(T3_UINT32, ix, ni, 1), t3_geometry_set_index(g, at), t3_release(at);
  free(pos), free(col), free(ix);
  return g;
}

static void lake_surface_and_shafts(cw_node root) {
  /* the underside of the sea: a ceiling of moving light that only shows from below */
  LK.caustic = caustic_texture();
  LK.caustic->repeat = (t3_vec2){ 22, 22 };
  LK.ceil_mat = basic_add(0.28f, 0.75f, 0.65f, false, LK.caustic);
  LK.ceil_mat->side = T3_FRONT_SIDE;
  float cx = 0.5f * (LK.a.x + LK.b.x), cz = 0.5f * (LK.a.z + LK.b.z);
  float len = sqrtf((LK.b.x - LK.a.x) * (LK.b.x - LK.a.x) + (LK.b.z - LK.a.z) * (LK.b.z - LK.a.z)) + 2.0f * LK.r1;
  t3_geometry *g = t3_plane_geometry_new(len, len, 1, 1);
  t3_mesh *me = t3_mesh_new(g, LK.ceil_mat);
  me->base.render_order = 4;
  cw_node n = hang(root, "LakeCeiling", &me->base);
  t3_release(me), t3_release(g);
  cw_node3d_set_position(n, V(cx, SEA_Y - 0.1f, cz));
  cw_node3d_set_rotation(n, V(1.5707963f, 0, 0));
  /* shafts from the surface to the bed, slanted all the same way, on both sides of the tube */
  LK.shaft_mat = basic_add(0.3f, 0.7f, 0.7f, true, NULL);
  uint32_t rs = 9001;
  t3_vec3 ax = V(LK.b.x - LK.a.x, 0, LK.b.z - LK.a.z);
  float al = sqrtf(ax.x * ax.x + ax.z * ax.z);
  ax = vmul(ax, 1.0f / al);
  t3_vec3 side = V(-ax.z, 0, ax.x);
  for (int i = 0; i < 8; i++) {
    float along = (0.15f + 0.7f * (float)i / 7.0f) * al, lat = ((i & 1) ? 1.0f : -1.0f) * (14.0f + 20.0f * frand(&rs));
    t3_vec3 p = vadd(vadd(V(LK.a.x, SEA_Y - 0.2f, LK.a.z), vmul(ax, along)), vmul(side, lat));
    float h = 36.0f;
    t3_geometry *sg = shaft_geometry(3.0f + 3.0f * frand(&rs), 0.8f, h, 12);
    t3_mesh *sm = t3_mesh_new(sg, LK.shaft_mat);
    sm->base.frustum_culled = true;
    LK.shaft[i] = hang(root, "Shaft", &sm->base);
    t3_release(sm), t3_release(sg);
    cw_node3d_set_position(LK.shaft[i], p);
    cw_node3d_set_rotation(LK.shaft[i], V(0.18f, frand(&rs) * 6.0f, 0.12f));
    LK.shaft_ph[i] = frand(&rs) * 6.28f;
    LK.nshaft++;
  }
}

/* ── the creatures ───────────────────────────────────────────────────────────────────────────────── */

static prop_model make_fish_school(uint32_t seed, int count, pcol body, pcol belly, float size) {
  pm *b = pm_new(seed);
  uint32_t rs = seed * 7u + 1u;
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < count; i++) {
    float x = (frand(&rs) - 0.5f) * 9.0f, y = (frand(&rs) - 0.5f) * 4.0f, z = (frand(&rs) - 0.5f) * 7.0f, sc = size * (0.75f + 0.5f * frand(&rs));
    pm_push(b);
    pm_move(b, x, y, z);
    pm_roty(b, (frand(&rs) - 0.5f) * 0.5f);
    pm_scale(b, sc, sc, sc);
    pm_ball(b, 0.2f, 0.28f, 0.7f, 0, 0.0f, glow(belly, 1.0f), glow(body, 1.5f));
    pm_tri(b, V(0, 0, -0.55f), V(0, 0.35f, -1.1f), V(0, -0.35f, -1.1f), glow(body, 1.3f));
    pm_tri(b, V(0, 0, -0.55f), V(0, -0.35f, -1.1f), V(0, 0.35f, -1.1f), glow(body, 1.3f));
    pm_pop(b);
  }
  return pm_finish(b);
}

static prop_model make_manta(void) {
  pm *b = pm_new(321);
  pm_layer(b, PL_MAIN);
  pcol top = rgb(0.02f, 0.05f, 0.1f), under = rgb(0.5f, 0.6f, 0.7f);
  t3_vec3 nose = V(0, 0, 3.2f), wl = V(-5.5f, 0.1f, -0.2f), wr = V(5.5f, 0.1f, -0.2f), tail = V(0, 0, -2.4f), ct = V(0, 0.55f, 0.4f), cb = V(0, -0.35f, 0.4f);
  /* the upper faces then the lower faces of a flattened diamond */
  pm_tri(b, nose, wr, ct, top), pm_tri(b, ct, wr, tail, top), pm_tri(b, nose, ct, wl, top), pm_tri(b, ct, tail, wl, top);
  pm_tri(b, nose, cb, wr, under), pm_tri(b, cb, tail, wr, under), pm_tri(b, nose, wl, cb, under), pm_tri(b, cb, wl, tail, under);
  pm_rod(b, V(0, 0, -2.2f), V(0, -0.1f, -8.0f), 0.1f, 5, top);
  pm_tri(b, V(-0.5f, 0.2f, 3.0f), V(-1.1f, 0.0f, 4.0f), V(-0.2f, 0.1f, 3.4f), top);
  pm_tri(b, V(0.5f, 0.2f, 3.0f), V(0.2f, 0.1f, 3.4f), V(1.1f, 0.0f, 4.0f), top);
  return pm_finish(b);
}

static prop_model make_jelly(uint32_t seed, pcol c) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_GLOW);
  pm_ball(b, 1.6f, 1.1f, 1.6f, 1, 0.05f, glow(c, 1.2f), glow(pc_mix(c, WARM, 0.4f), 2.4f));
  uint32_t rs = seed;
  for (int i = 0; i < 9; i++) {
    float an = 6.2831853f * (float)i / 9.0f, r = 0.9f;
    pm_rod(b, V(cosf(an) * r, -0.3f, sinf(an) * r), V(cosf(an) * r * 1.5f, -4.5f - 2.0f * frand(&rs), sinf(an) * r * 1.5f), 0.07f, 4, glow(c, 1.2f));
  }
  return pm_finish(b);
}

/* the drowned bus: tipped on the bed, windows lit from inside, a sign still burning on its roof */
static void sunken_bus(cw_node root, t3_vec3 at, float yaw) {
  pm *b = pm_new(555);
  pm_layer(b, PL_MAIN);
  pm_box(b, 3.4f, 3.3f, 13.0f, rgb(0.12f, 0.42f, 0.5f), rgb(0.18f, 0.55f, 0.6f));
  pm_push(b);
  pm_move(b, 0, 3.3f, 0);
  pm_box(b, 3.2f, 0.3f, 12.8f, rgb(0.6f, 0.62f, 0.66f), rgb(0.7f, 0.72f, 0.76f));
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, 0, 0);
  for (int sd = -1; sd <= 1; sd += 2)
    for (int k = 0; k < 2; k++) {
      pm_push(b);
      pm_move(b, (float)sd * 1.7f, 0, -3.8f + 7.6f * (float)k);
      pm_roty(b, 1.5707963f);
      pm_cyl(b, 0.62f, 0.62f, 0.3f, 12, rgb(0.04f, 0.04f, 0.05f), rgb(0.08f, 0.08f, 0.09f));
      pm_pop(b);
    }
  pm_pop(b);
  pm_layer(b, PL_GLOW);
  for (int sd = -1; sd <= 1; sd += 2)
    for (int i = 0; i < 6; i++) {
      float z = -5.4f + 2.1f * (float)i;
      if (i == 3 && sd < 0) continue;
      pm_quad(b, V((float)sd * 1.72f, 1.4f, z - 0.8f), V((float)sd * 1.72f, 1.4f, z + 0.8f), V((float)sd * 1.72f, 2.8f, z + 0.8f), V((float)sd * 1.72f, 2.8f, z - 0.8f), glow(i == 2 ? PINK : WARM, 1.5f));
    }
  pm_quad(b, V(-1.2f, 0.9f, 6.52f), V(1.2f, 0.9f, 6.52f), V(1.2f, 2.6f, 6.52f), V(-1.2f, 2.6f, 6.52f), glow(CYAN, 1.2f));
  for (int sd = -1; sd <= 1; sd += 2) pm_ball(b, 0.3f, 0.3f, 0.3f, 0, 0, glow(AMBER, 3.0f), glow(WARM, 3.0f));
  pm_push(b);
  pm_move(b, 0, 3.8f, -1.0f);
  neon_sign(b, 3.8f, 1.6f, 2, PINK, CYAN, 4u);
  pm_pop(b);
  prop_model m = pm_finish(b);
  cw_node n = model_node(root, "SunkenBus", m, false);
  cw_node3d_set_position(n, at);
  cw_node3d_set_rotation(n, V(0.12f, yaw, 0.38f));
}

static void creatures(cw_node root) {
  uint32_t rs = 1717;
  t3_vec3 ax = V(LK.b.x - LK.a.x, 0, LK.b.z - LK.a.z);
  float al = sqrtf(ax.x * ax.x + ax.z * ax.z);
  ax = vmul(ax, 1.0f / al);
  t3_vec3 side = V(-ax.z, 0, ax.x);
  /* schools of fish circling over the bed on both sides of the tube */
  static const uint32_t cols[4] = { 0x18e6ff, 0xffa22e, 0xff2d95, 0x9dff4a };
  for (int i = 0; i < (PLAN_VIEWS > 1 ? 4 : 7); i++) {
    float along = (0.12f + 0.76f * (float)i / 6.0f) * al, lat = ((i & 1) ? 1.0f : -1.0f) * (22.0f + 12.0f * frand(&rs));
    t3_vec3 c = vadd(vadd(V(LK.a.x, 0, LK.a.z), vmul(ax, along)), vmul(side, lat));
    float roadY = lake_prof(along / al);
    pcol body = hex(cols[i % 4]);
    prop_model m = make_fish_school(100u + (uint32_t)i, PLAN_VIEWS > 1 ? 9 : 16, body, pc_mix(body, WARM, 0.5f), 1.3f);
    LK.school[i] = model_node(root, "School", m, false);
    LK.sc[i].cx = c.x, LK.sc[i].cz = c.z, LK.sc[i].r = 9.0f + 7.0f * frand(&rs), LK.sc[i].y = roadY + 2.0f + 5.0f * frand(&rs);
    LK.sc[i].w = (0.18f + 0.1f * frand(&rs)) * ((i & 2) ? 1.0f : -1.0f), LK.sc[i].ph = frand(&rs) * 6.28f;
    LK.nschool++;
  }
  /* a manta gliding over the roof of the tube */
  LK.manta = model_node(root, "Manta", make_manta(), false);
  /* jellyfish drifting, lit from inside */
  for (int i = 0; i < (PLAN_VIEWS > 1 ? 3 : 5); i++) {
    float along = (0.2f + 0.6f * frand(&rs)) * al, lat = ((i & 1) ? 1.0f : -1.0f) * (16.0f + 14.0f * frand(&rs));
    t3_vec3 c = vadd(vadd(V(LK.a.x, 0, LK.a.z), vmul(ax, along)), vmul(side, lat));
    float roadY = lake_prof(along / al);
    LK.jelly[i] = model_node(root, "Jelly", make_jelly(200u + (uint32_t)i, i % 2 ? VIOLET : CYAN), false);
    cw_node3d_set_position(LK.jelly[i], V(c.x, roadY + 3.0f + 5.0f * frand(&rs), c.z));
    cw_node3d_set_quaternion(LK.jelly[i], t3_quat_from_axis_angle(V(0, 1, 0), frand(&rs) * 6.0f));
    LK.njelly++;
  }
  /* the bus, halfway down on the near side */
  float along = 0.5f * al, lat = 24.0f;
  t3_vec3 c = vadd(vadd(V(LK.a.x, 0, LK.a.z), vmul(ax, along)), vmul(side, lat));
  float bed = city_ground(c.x, c.z, NULL);
  sunken_bus(root, V(c.x, bed + 1.0f, c.z), atan2f(ax.x, ax.z) + 0.5f);
  /* plankton: little lights wandering through the water over the tube */
  for (int i = 0; i < 6; i++) {
    float a2 = (0.1f + 0.16f * (float)i) * al;
    t3_vec3 p = vadd(V(LK.a.x, lake_prof(a2 / al) + 3.0f, LK.a.z), vmul(ax, a2));
    fx_ambient_at(root, FXA_FIREFLIES, p, V(26, 8, 26), 1.4f, 90);
  }
}

static void creatures_frame(double time) {
  float tt = (float)time;
  for (int i = 0; i < LK.nschool; i++) {
    float an = LK.sc[i].ph + tt * LK.sc[i].w;
    float x = LK.sc[i].cx + cosf(an) * LK.sc[i].r, z = LK.sc[i].cz + sinf(an) * LK.sc[i].r;
    float y = LK.sc[i].y + 1.2f * sinf(tt * 0.5f + (float)i);
    cw_node3d_set_position(LK.school[i], V(x, y, z));
    /* heading along the circle's tangent */
    float dir = LK.sc[i].w > 0 ? 1.0f : -1.0f;
    float hx = -sinf(an) * dir, hz = cosf(an) * dir;
    cw_node3d_set_rotation(LK.school[i], V(0, atan2f(hx, hz), 0));
  }
  if (cw_node_valid(LK.manta)) {
    /* up and down the deep middle of the lake over the tube roof, turning at either end */
    float al = sqrtf((LK.b.x - LK.a.x) * (LK.b.x - LK.a.x) + (LK.b.z - LK.a.z) * (LK.b.z - LK.a.z));
    float leg = 0.4f * al, cyc = 2.0f * leg + 50.0f, ph = fmodf(tt * 4.5f, cyc);
    bool back = ph > leg + 25.0f;
    float u = 0.3f + 0.4f * (back ? fclampf((cyc - ph) / leg, 0, 1) : fminf(ph, leg) / leg);
    t3_vec3 ax = V((LK.b.x - LK.a.x) / al, 0, (LK.b.z - LK.a.z) / al);
    t3_vec3 side = V(-ax.z, 0, ax.x);
    float lat = 7.0f * sinf(tt * 0.21f);
    t3_vec3 p = vadd(vadd(V(LK.a.x, 0, LK.a.z), vmul(ax, u * al)), vmul(side, lat));
    p.y = fminf(lake_prof(u) + 10.5f, SEA_Y - 3.0f) + 0.8f * sinf(tt * 0.8f);
    cw_node3d_set_position(LK.manta, p);
    cw_node3d_set_rotation(LK.manta, V(0.05f * sinf(tt * 1.6f), atan2f(ax.x, ax.z) + (back ? 3.14159265f : 0), 0.18f * sinf(tt * 1.6f)));
  }
  for (int i = 0; i < LK.njelly; i++) {
    t3_vec3 p = cw_node3d_position(LK.jelly[i]);
    p.y += 0.004f * sinf(tt * 1.2f + (float)i * 2.0f);
    cw_node3d_set_position(LK.jelly[i], p);
    float k = 1.0f + 0.14f * sinf(tt * 2.4f + (float)i);
    cw_node3d_set_scale(LK.jelly[i], V(1.0f / sqrtf(k), k, 1.0f / sqrtf(k)));
  }
  /* the ceiling's ripples slide, and brighten with the lightning */
  if (LK.caustic) {
    LK.caustic->offset = (t3_vec2){ tt * 0.012f, tt * 0.007f };
    LK.caustic->version++;
  }
  for (int i = 0; i < LK.nshaft; i++) {
    if (!cw_node_valid(LK.shaft[i])) continue;
    cw_node3d_set_rotation(LK.shaft[i], V(0.18f + 0.04f * sinf(tt * 0.3f + LK.shaft_ph[i]), tt * 0.05f + LK.shaft_ph[i], 0.12f));
  }
}

/* ── the tube itself: ribs, lamps and the gates at its two mouths ────────────────────────────────── */

static void tube_dressing(const track *t) {
  pcol rib = rgb(0.07f, 0.09f, 0.12f);
  for (float s = L.plate.s0 + 4.0f; s < L.plate.s1 - 2.0f; s += 9.0f) {
    tsample a = track_at(t, s);
    float Rr = a.hw, hc = 1.6f, Rt = 8.0f - hc;
    pm *b = frame_in(N.c[GR_STEADY], a.p, a.r, a.u, a.f);
    pm_layer(b, PL_METAL);
    /* a rib: up the side, over the arch, down the other side, just inside the glass */
    for (int k = 0; k < 14; k++) {
      float t0 = 3.14159265f * (float)k / 14.0f, t1 = 3.14159265f * (float)(k + 1) / 14.0f;
      pm_rod(b, V(-cosf(t0) * (Rr - 0.15f), hc + sinf(t0) * (Rt - 0.15f), 0), V(-cosf(t1) * (Rr - 0.15f), hc + sinf(t1) * (Rt - 0.15f), 0), 0.17f, 5, rib);
    }
    pm_rod(b, V(-(Rr - 0.15f), 0, 0), V(-(Rr - 0.15f), hc, 0), 0.17f, 5, rib);
    pm_rod(b, V(Rr - 0.15f, 0, 0), V(Rr - 0.15f, hc, 0), 0.17f, 5, rib);
    pm_layer(b, PL_GLOW);
    /* a lit strip along the inside of the rib at the crown, and a lamp at each foot */
    for (int k = 2; k < 12; k++) {
      float t0 = 3.14159265f * (float)k / 14.0f, t1 = 3.14159265f * (float)(k + 1) / 14.0f;
      pm_rod(b, V(-cosf(t0) * (Rr - 0.4f), hc + sinf(t0) * (Rt - 0.4f), 0.1f), V(-cosf(t1) * (Rr - 0.4f), hc + sinf(t1) * (Rt - 0.4f), 0.1f), 0.07f, 4, glow(((int)(s / 9.0f)) & 1 ? CYAN : VIOLET, 2.6f));
    }
    for (int k = -1; k <= 1; k += 2) {
      pm_push(b);
      pm_move(b, (float)k * (Rr - 0.6f), 0.5f, 0);
      pm_box_c(b, 0.4f, 0.4f, 0.9f, glow(((int)(s / 9.0f) + (k > 0)) & 1 ? PINK : WARM, 3.0f));
      pm_pop(b);
    }
    pm_pop(b);
  }
  /* the gates: a thick lit ring round each mouth, and a plinth under it */
  for (int e = 0; e < 2; e++) {
    tsample a = track_at(t, e ? L.plate.s1 + 1.0f : L.plate.s0 - 1.0f);
    float Rr = a.hw + 1.0f, hc = 1.6f, Rt = 8.0f - hc + 1.0f;
    pm *b = frame_in(N.c[GR_PULSE], a.p, a.r, a.u, a.f);
    pm_layer(b, PL_METAL);
    arch_tube(b, hc, Rr, Rt, 0.7f, 0.0f, 3.14159265f, 36, rgb(0.09f, 0.1f, 0.14f));
    for (int k = -1; k <= 1; k += 2) pm_rod(b, V((float)k * Rr, -0.3f, 0), V((float)k * Rr, hc, 0), 0.7f, 6, rgb(0.09f, 0.1f, 0.14f));
    pm_layer(b, PL_GLOW);
    arch_tube(b, hc, Rr - 0.3f, Rt - 0.3f, 0.26f, 0.0f, 3.14159265f, 36, glow(e ? PINK : CYAN, 3.4f));
    arch_tube(b, hc, Rr + 1.3f, Rt + 1.3f, 0.16f, 0.0f, 3.14159265f, 40, glow(VIOLET, 3.0f));
    pm_pop(b);
    /* a fish for a sign over the gate, facing the way you arrive */
    t3_vec3 up = a.u, face = e ? vmul(a.f, 1.0f) : vmul(a.f, -1.0f);
    ad_add(AD_FISH, vadd(vadd(a.p, vmul(up, Rt + hc + 2.0f)), vmul(a.f, e ? 0.5f : -0.5f)), e ? a.f : vmul(a.f, -1), up, 12.0f, 6.0f);
    (void)face;
  }
}

/* what lies on the bed round the tube: the stumps of towers, wrecks, signs still lit */
static void lakebed(const track *t, cw_node root, uint32_t seed) {
  prop_set *ps = ps_new(root, "LakeBed");
  scatter_opts so = scatter_defaults();
  so.kinds[0] = PK_RUIN_BUILDING, so.weight[0] = 2;
  so.kinds[1] = PK_WRECK_CAR, so.weight[1] = 3;
  so.kinds[2] = PK_WRECK_TRUCK, so.weight[2] = 1;
  so.kinds[3] = PK_NEON_SIGN, so.weight[3] = 4;
  so.kinds[4] = PK_DEBRIS, so.weight[4] = 2;
  so.kinds[5] = PK_SKYSCRAPER, so.weight[5] = 1;
  so.nkinds = 6;
  so.cx = 0.5f * (LK.a.x + LK.b.x), so.cz = 0.5f * (LK.a.z + LK.b.z), so.half_x = so.half_z = 0.5f * (LK.r0 + 160.0f);
  so.density = PLAN_VIEWS > 1 ? 0.3f : 0.55f, so.scale_min = 0.8f, so.scale_max = 1.5f;
  so.margin = 7.0f, so.seed = seed ^ 0xbed;
  so.ground = city_ground;
  so.mask = lake_mask;
  so.sink = 0.3f;
  scatter_area(ps, t, &so);
  ps_finish(ps);
}


/* ── set pieces 4 and 5: the collapsed overpass (a gap to jump, a billboard to land under) and the corkscrew through
 * its ruins ──────────────────────────────────────────────────────────────────────────────────────────── */

static pcol concrete_dark, concrete_light, rust_col;

/* a slab of fallen deck: concrete with a broken edge, rebar and a stub of railing, in the current frame */
static void ruin_slab(pm *b, float w, float len, uint32_t seed) {
  uint32_t rs = seed;
  pm_layer(b, PL_MAIN);
  pm_push(b);
  pm_box(b, w, 1.5f, len, concrete_dark, concrete_light);
  /* a broken end: a stepped break of smaller blocks */
  for (int i = 0; i < 4; i++) {
    pm_push(b);
    float mx = frand(&rs), mz = frand(&rs);
    pm_move(b, (mx - 0.5f) * w * 0.8f, 0, len * 0.5f + 0.2f + mz * 0.8f);
    pm_rotx(b, (frand(&rs) - 0.5f) * 0.5f);
    float bx = 1.5f + 2.0f * frand(&rs), bz = 1.2f + 1.6f * frand(&rs);
    pm_box(b, bx, 1.1f, bz, dk(concrete_dark, 0.9f), concrete_light);
    pm_pop(b);
  }
  /* the kerb and the railing stump along one side */
  pm_push(b);
  pm_move(b, w * 0.5f - 0.3f, 1.5f, 0);
  pm_box(b, 0.5f, 1.0f, len * 0.9f, concrete_dark, concrete_light);
  pm_pop(b);
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 8; i++) {
    float x = (frand(&rs) - 0.5f) * w * 0.9f, z = len * 0.5f + 0.2f;
    float ex = x + pm_rands(b) * 0.4f, ey = 0.6f + frand(&rs) * 0.8f, ez = z + 1.0f + 2.8f * frand(&rs);
    pm_rod(b, V(x, 0.7f, z), V(ex, ey, ez), 0.07f, 4, rust_col);
  }
  pm_layer(b, PL_GLOW);
  /* the last metres of a strip light along the kerb, still on */
  pm_push(b);
  pm_move(b, w * 0.5f - 0.3f, 2.1f, 0);
  pm_beam(b, V(0, 0, -len * 0.4f), V(0, 0, len * 0.35f), 0.12f, glow(CYAN, 2.4f));
  pm_pop(b);
  pm_pop(b);
  pm_layer(b, PL_MAIN);
}

static void stump(pm *b, float h, float r, uint32_t seed) {
  uint32_t rs = seed;
  pm_layer(b, PL_MAIN);
  pm_box(b, r * 2.0f, h, r * 2.0f, concrete_dark, concrete_light);
  pm_push(b);
  pm_move(b, 0, h, 0);
  for (int i = 0; i < 3; i++) {
    pm_push(b);
    float mx = frand(&rs), mz = frand(&rs);
    pm_move(b, (mx - 0.5f) * r, 0, (mz - 0.5f) * r);
    float bx = r * (0.5f + 0.5f * frand(&rs)), by = 0.8f + 1.8f * frand(&rs), bz = r * (0.5f + 0.5f * frand(&rs));
    pm_box(b, bx, by, bz, concrete_dark, concrete_light);
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 5; i++) {
    float ax = frand(&rs), az = frand(&rs), bx = frand(&rs), by = frand(&rs), bz = frand(&rs);
    pm_rod(b, V((ax - 0.5f) * r, 0, (az - 0.5f) * r), V((bx - 0.5f) * r * 2, 2.5f + 2.5f * by, (bz - 0.5f) * r * 2), 0.07f, 4, rust_col);
  }
  pm_pop(b);
  pm_layer(b, PL_MAIN);
}

#define NDEBRIS 6
static struct {
  cw_node chunk[NDEBRIS];
  t3_vec3 at[NDEBRIS];
  float fall[NDEBRIS], per[NDEBRIS], ph[NDEBRIS];
  cw_node spark[4];
  t3_vec3 spark_at[4];
  int nspark;
} RU;

static prop_model make_chunk(uint32_t seed) {
  pm *b = pm_new(seed);
  pm_layer(b, PL_MAIN);
  pm_box_c(b, 1.6f, 1.0f, 1.2f, concrete_dark);
  pm_push(b);
  pm_move(b, 0.7f, 0.2f, 0.2f);
  pm_box_c(b, 0.9f, 0.7f, 0.8f, concrete_light);
  pm_pop(b);
  pm_layer(b, PL_METAL);
  pm_rod(b, V(-0.8f, 0.2f, 0), V(-1.7f, 0.5f, 0.3f), 0.06f, 4, rust_col);
  return pm_finish(b);
}

/* a falling streak of sparks: a few thin glowing bars in a column, moved down by the frame function */
static prop_model make_spark_column(uint32_t seed) {
  pm *b = pm_new(seed);
  uint32_t rs = seed;
  pm_layer(b, PL_GLOW);
  for (int i = 0; i < 9; i++) {
    float x = (frand(&rs) - 0.5f) * 1.2f, y = -frand(&rs) * 8.0f, z = (frand(&rs) - 0.5f) * 1.2f;
    pm_beam(b, V(x, y, z), V(x + 0.05f, y - 0.5f - frand(&rs) * 0.6f, z), 0.07f, glow(i & 1 ? AMBER : WARM, 5.0f));
  }
  return pm_finish(b);
}

static void corkscrew_ruins(const track *t, cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0xc0c;
  pmc *c = N.c[GR_STEADY];
  float s0 = L.cork.s0 - 30, s1 = L.cork.s1 + 30;
  int k = 0;
  for (float s = s0; s < s1; s += 34.0f + 8.0f * frand(&rs), k++) {
    tsample a = track_at(t, s);
    int side = (k & 1) ? 1 : -1;
    float lat = 17.0f + 12.0f * frand(&rs);
    t3_vec3 p = vadd(a.p, vmul(a.r, (float)side * lat));
    float g = city_ground(p.x, p.z, NULL);
    /* an overpass has fallen across this block: two stumps and a tipped deck between them */
    t3_vec3 base = V(p.x, g - 1.0f, p.z);
    pm *b = upright_frame(c, base, &a);
    pm_push(b);
    pm_layer(b, PL_MAIN);
    float h = a.p.y - g + 4.0f + 8.0f * frand(&rs);
    stump(b, h, 1.6f, seed + (uint32_t)k * 3u);
    pm_pop(b);
    pm_pop(b);
    if (frand(&rs) < 0.75f) {
      pm *d = upright_frame(c, V(p.x, a.p.y + 1.0f + 3.0f * frand(&rs), p.z), &a);
      float mx = frand(&rs), mz = frand(&rs);
      pm_move(d, (mx - 0.5f) * 4.0f, 0, 6.0f * (mz - 0.5f));
      pm_roty(d, (frand(&rs) - 0.5f) * 0.9f);
      pm_rotz(d, (float)side * (0.12f + 0.3f * frand(&rs)));
      pm_rotx(d, (frand(&rs) - 0.5f) * 0.3f);
      float sw = 8.0f + 5.0f * frand(&rs), sl = 14.0f + 10.0f * frand(&rs);
      ruin_slab(d, sw, sl, seed + (uint32_t)k * 11u);
      pm_pop(d);
    }
    /* a broken sign hanging by one corner, one half of it still burning */
    if (frand(&rs) < 0.5f) {
      pm *e = upright_frame(N.c[GR_FLICK], V(p.x, a.p.y + 5.5f + 3.0f * frand(&rs), p.z), &a);
      pm_move(e, (float)-side * (1.0f + 3.0f * frand(&rs)), 0, 0);
      pm_roty(e, (float)-side * 1.57f);
      pm_rotz(e, 0.5f + 0.5f * frand(&rs));
      pm_layer(e, PL_METAL);
      pm_rod(e, V(0, 0, 0), V(0, 7, 0), 0.08f, 4, rgb(0.1f, 0.1f, 0.12f));
      int style = (int)(frand(&rs) * 4.99f);
      pcol n1 = neon_pick(&rs), n2 = neon_pick(&rs);
      neon_sign(e, 5.0f, 2.0f, style, n1, n2, seed + (uint32_t)k * 5u);
      pm_pop(e);
    }
    /* sparks falling from a cut cable beside it */
    if (RU.nspark < 4 && frand(&rs) < 0.7f) {
      RU.spark_at[RU.nspark] = V(p.x + (float)-side * 5.0f, a.p.y + 9.0f, p.z);
      RU.nspark++;
    }
  }
  /* falling rubble: chunks of deck letting go, over and over, a good way off the road */
  for (int i = 0; i < NDEBRIS; i++) {
    float sd = lerpf(L.cork.s0, L.cork.s1, (float)i / (float)(NDEBRIS - 1));
    tsample a = track_at(t, sd);
    int side = (i & 1) ? 1 : -1;
    float lat = 22.0f + 6.0f * frand(&rs), up = 10.0f + 4.0f * frand(&rs);
    RU.at[i] = vadd(vadd(a.p, vmul(a.r, (float)side * lat)), V(0, up, 0));
    RU.fall[i] = 16.0f + 4.0f * frand(&rs), RU.per[i] = 1.6f + 0.8f * frand(&rs), RU.ph[i] = frand(&rs) * 4.0f;
    RU.chunk[i] = model_node(root, "Rubble", make_chunk(30u + (uint32_t)i), false);
    cw_node3d_set_position(RU.chunk[i], RU.at[i]);
  }
  for (int i = 0; i < RU.nspark; i++) {
    RU.spark[i] = model_node(root, "Sparks", make_spark_column(60u + (uint32_t)i), false);
    cw_node3d_set_position(RU.spark[i], RU.spark_at[i]);
    fx_ambient_at(root, FXA_EMBERS, vadd(RU.spark_at[i], V(0, -3, 0)), V(1.5f, 1.0f, 1.5f), 0.8f, 110);
  }
  /* a flickering orange light where the cables are cut, one for the whole stretch (it follows the nearest spark) */
  if (RU.nspark > 0) add_omni(root, "SparkLight", vadd(RU.spark_at[0], V(0, -3, 0)), AMBER, 160, 28);
}

static void ruins_frame(double time) {
  float tt = (float)time;
  for (int i = 0; i < NDEBRIS; i++) {
    if (!cw_node_valid(RU.chunk[i])) continue;
    float u = fmodf(tt + RU.ph[i], RU.per[i]) / RU.per[i]; /* 0..1 through one fall */
    float y = -RU.fall[i] * u * u;
    cw_node3d_set_position(RU.chunk[i], vadd(RU.at[i], V(0, y, 0)));
    cw_node3d_set_rotation(RU.chunk[i], V(tt * 1.3f + (float)i, tt * 0.9f, tt * 1.7f));
    /* hidden for the first moments so it does not pop in full view */
    t3_object *o = cw_node3d_object(RU.chunk[i]);
    if (o) o->visible = u > 0.04f;
  }
  for (int i = 0; i < RU.nspark; i++) {
    if (!cw_node_valid(RU.spark[i])) continue;
    float u = fmodf(tt * 0.9f + (float)i * 0.37f, 1.0f);
    uint32_t h = hash32((uint32_t)(tt * 0.9f + (float)i * 0.37f) * 977u + (uint32_t)i);
    bool on = (h & 3) != 0;
    cw_node3d_set_position(RU.spark[i], vadd(RU.spark_at[i], V(0, -u * 7.0f, 0)));
    t3_object *o = cw_node3d_object(RU.spark[i]);
    if (o) o->visible = on && u < 0.9f;
  }
}

/* the gap: a broken deck at the lip, wreckage in the water under it, and a billboard on a gantry beyond the landing */
static void jump_set_piece(const track *t, cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0x1a4b;
  tsample lip = track_at(t, L.gap.s0 - 1.0f), land = track_at(t, L.gap.s1 + 1.0f);
  pmc *c = N.c[GR_STEADY];
  /* ragged concrete past each edge of the road at the lip and at the landing */
  for (int e = 0; e < 2; e++) {
    const tsample *a = e ? &land : &lip;
    float dir = e ? 1.0f : -1.0f; /* which way the broken edge points along the road */
    pm *b = frame_in(c, a->p, a->r, a->u, a->f);
    pm_layer(b, PL_MAIN);
    for (int i = 0; i < 7; i++) {
      pm_push(b);
      float mx = frand(&rs), my = frand(&rs), mz = frand(&rs);
      pm_move(b, (mx - 0.5f) * (a->hw * 2.0f), -0.8f - my * 1.4f, dir * (0.4f + mz * 3.2f));
      pm_rotx(b, dir * (0.1f + 0.5f * frand(&rs)));
      float bx = 1.6f + 3.2f * frand(&rs), bz = 1.2f + 2.0f * frand(&rs);
      pm_box(b, bx, 1.0f, bz, concrete_dark, concrete_light);
      pm_pop(b);
    }
    pm_layer(b, PL_METAL);
    for (int i = 0; i < 12; i++) {
      float x = (frand(&rs) - 0.5f) * (a->hw * 2.0f);
      float ex = x + pm_rands(b) * 0.7f, ey = -0.3f - frand(&rs) * 1.5f, ez = dir * (2.5f + 3.5f * frand(&rs));
      pm_rod(b, V(x, -0.4f, dir * 0.6f), V(ex, ey, ez), 0.08f, 4, rust_col);
    }
    pm_layer(b, PL_GLOW);
    /* the warning lights along the broken edge */
    for (int i = -3; i <= 3; i += 2) {
      pm_push(b);
      pm_move(b, (float)i * a->hw / 3.4f, 0.4f, dir * 0.2f);
      pm_box_c(b, 0.4f, 0.4f, 0.4f, glow(RED, 5.0f));
      pm_pop(b);
    }
    pm_pop(b);
  }
  /* wreckage in the water between the two edges: fallen deck sections and a bus nosed in */
  float mid = 0.5f * (L.gap.s0 + L.gap.s1);
  tsample m = track_at(t, mid);
  for (int i = 0; i < 4; i++) {
    float lat = (frand(&rs) - 0.5f) * 14.0f, along = (frand(&rs) - 0.5f) * 20.0f;
    t3_vec3 p = vadd(vadd(m.p, vmul(m.r, lat)), vmul(m.f, along));
    p.y = SEA_Y + 0.3f - frand(&rs) * 1.0f;
    pm *b = upright_frame(c, p, &m);
    pm_roty(b, frand(&rs) * 3.0f);
    pm_rotz(b, (frand(&rs) - 0.5f) * 0.6f);
    pm_rotx(b, (frand(&rs) - 0.5f) * 0.5f);
    float sw = 7.0f + 3.0f * frand(&rs), sl = 10.0f + 6.0f * frand(&rs);
    ruin_slab(b, sw, sl, seed + (uint32_t)i * 9u);
    pm_pop(b);
  }
  /* the billboard: two towers and a gantry over the road 60 m past the landing, a big lit picture facing the ramp */
  tsample g = track_at(t, L.gap.s1 + 62.0f);
  float W = g.hw + 7.0f;
  pm *b = frame_in(N.c[GR_PULSE], vadd(g.p, V(0, -6.0f, 0)), g.r, V(0, 1, 0), g.f);
  pm_layer(b, PL_METAL);
  for (int sd = -1; sd <= 1; sd += 2) {
    pm_beam(b, V((float)sd * W, 0, -1.2f), V((float)sd * W, 38.0f, -1.2f), 1.2f, rgb(0.08f, 0.08f, 0.12f));
    pm_beam(b, V((float)sd * W, 0, 1.2f), V((float)sd * W, 38.0f, 1.2f), 1.2f, rgb(0.08f, 0.08f, 0.12f));
    for (int i = 0; i < 9; i++) {
      float y0 = 4.0f * (float)i, y1 = y0 + 4.0f;
      pm_beam(b, V((float)sd * W, y0, -1.2f), V((float)sd * W, y1, 1.2f), 0.35f, rgb(0.08f, 0.08f, 0.12f));
    }
  }
  pm_beam(b, V(-W, 20.0f, -1.2f), V(W, 20.0f, -1.2f), 0.9f, rgb(0.08f, 0.08f, 0.12f));
  pm_beam(b, V(-W, 38.0f, -1.2f), V(W, 38.0f, -1.2f), 0.9f, rgb(0.08f, 0.08f, 0.12f));
  pm_layer(b, PL_GLOW);
  /* a lit frame round the picture and a row of chasing arrows over it, pointing down at the landing */
  pm_beam(b, V(-W + 1.0f, 21.2f, -1.9f), V(W - 1.0f, 21.2f, -1.9f), 0.35f, glow(PINK, 3.4f));
  pm_beam(b, V(-W + 1.0f, 37.0f, -1.9f), V(W - 1.0f, 37.0f, -1.9f), 0.35f, glow(PINK, 3.4f));
  pm_beam(b, V(-W + 1.0f, 21.2f, -1.9f), V(-W + 1.0f, 37.0f, -1.9f), 0.35f, glow(CYAN, 3.4f));
  pm_beam(b, V(W - 1.0f, 21.2f, -1.9f), V(W - 1.0f, 37.0f, -1.9f), 0.35f, glow(CYAN, 3.4f));
  pm_pop(b);
  pm *ar = frame_in(N.c[GR_FLICK], vadd(g.p, V(0, -6.0f, 0)), g.r, V(0, 1, 0), g.f);
  pm_layer(ar, PL_GLOW);
  for (int i = 0; i < 3; i++) {
    float x = (float)(i - 1) * 8.0f;
    pm_beam(ar, V(x - 3.0f, 42.0f, -1.9f), V(x, 39.2f, -1.9f), 0.55f, glow(AMBER, 4.0f));
    pm_beam(ar, V(x + 3.0f, 42.0f, -1.9f), V(x, 39.2f, -1.9f), 0.55f, glow(AMBER, 4.0f));
  }
  pm_pop(ar);
  ad_add(AD_SUN, vadd(g.p, V(0, 15.5f, 0)), vmul(g.f, -1), V(0, 1, 0), 2.0f * (W - 1.5f), 15.0f);
  ad_add(AD_BURST, vadd(vadd(g.p, V(0, 15.5f, 0)), vmul(g.f, 0.1f)), g.f, V(0, 1, 0), 2.0f * (W - 1.5f), 15.0f);
  /* its light on the road and the water */
  add_omni(root, "BillboardLight", vadd(vadd(g.p, V(0, 18.0f, 0)), vmul(g.f, -16.0f)), PINK, 900, 70);
}


/* ── the sky life: drones, searchlights, holograms, billboards, rafts, steam ──────────────────────── */

static float tower_mask(float x, float z, void *ud) {
  (void)ud;
  float dx = x - WHEEL_AT.x, dz = z - WHEEL_AT.z;
  return dx * dx + dz * dz < 150.0f * 150.0f ? 0.0f : 1.0f;
}

/* big pictures on stilts standing in the water at the open stretches, facing the road: the skyline's own adverts */
static void billboards(const track *t, cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0xb111;
  pmc *c = N.c[GR_STEADY];
  int placed = 0;
  for (float s = 40; s < t->length - 40 && placed < 22; s += 110.0f + 80.0f * frand(&rs)) {
    int side = frand(&rs) < 0.5f ? -1 : 1;
    if (zone_mode(side, s) != 2 || in_run(L.plate, s, 30) || in_run(L.cork, s, 30) || in_run(L.gap, s, 60)) continue;
    tsample a = track_at(t, s);
    float lat = 28.0f + 36.0f * frand(&rs), w = 22.0f + 12.0f * frand(&rs), h = w * 0.5f, y0 = 11.0f + 8.0f * frand(&rs);
    t3_vec3 p = vadd(a.p, vmul(a.r, (float)side * (a.hw + lat)));
    if (near_other_road(t, p, s, 40.0f * 40.0f)) continue;
    p.y = SEA_Y - 3.0f;
    t3_vec3 face = vmul(a.r, (float)-side); /* toward the road */
    float yaw = atan2f(face.x, face.z);
    pm *b = pmc_at(c, p);
    pm_push(b);
    pm_move(b, p.x, p.y, p.z);
    pm_roty(b, yaw);
    pm_layer(b, PL_METAL);
    float top = y0 + h + 1.0f + 3.0f - p.y + SEA_Y; /* legs from under the water to the board's top */
    (void)top;
    for (int k = -1; k <= 1; k += 2) {
      pm_beam(b, V((float)k * w * 0.32f, 0, -0.6f), V((float)k * w * 0.32f, y0 + h + 3.0f + 3.0f, -0.6f), 0.7f, rgb(0.08f, 0.08f, 0.12f));
      pm_beam(b, V((float)k * w * 0.32f, y0 - 1.0f, -0.6f), V((float)k * w * 0.42f, y0 - 6.0f, -0.6f), 0.35f, rgb(0.08f, 0.08f, 0.12f));
    }
    pm_beam(b, V(-w * 0.52f, y0 - 0.5f, -0.5f), V(w * 0.52f, y0 - 0.5f, -0.5f), 0.4f, rgb(0.08f, 0.08f, 0.12f));
    pm_beam(b, V(-w * 0.52f, y0 + h + 0.5f, -0.5f), V(w * 0.52f, y0 + h + 0.5f, -0.5f), 0.4f, rgb(0.08f, 0.08f, 0.12f));
    pm_layer(b, PL_GLOW);
    pcol fc = neon_pick(&rs);
    pm_beam(b, V(-w * 0.52f, y0 - 0.5f, 0.05f), V(w * 0.52f, y0 - 0.5f, 0.05f), 0.28f, glow(fc, 3.2f));
    pm_beam(b, V(-w * 0.52f, y0 + h + 0.5f, 0.05f), V(w * 0.52f, y0 + h + 0.5f, 0.05f), 0.28f, glow(fc, 3.2f));
    pm_beam(b, V(-w * 0.52f, y0 - 0.5f, 0.05f), V(-w * 0.52f, y0 + h + 0.5f, 0.05f), 0.28f, glow(fc, 3.2f));
    pm_beam(b, V(w * 0.52f, y0 - 0.5f, 0.05f), V(w * 0.52f, y0 + h + 0.5f, 0.05f), 0.28f, glow(fc, 3.2f));
    /* two floodlights over the board */
    for (int k = -1; k <= 1; k += 2) {
      pm_push(b);
      pm_move(b, (float)k * w * 0.3f, y0 + h + 2.0f, 1.0f);
      pm_box_c(b, 1.0f, 0.4f, 0.8f, glow(WARM, 4.0f));
      pm_pop(b);
    }
    pm_pop(b);
    t3_vec3 pos = vadd(p, V(0, y0, 0));
    ad_add((int)(frand(&rs) * AD_N) % AD_N, vadd(pos, vmul(face, 0.2f)), face, V(0, 1, 0), w, h);
    placed++;
  }
}

/* ── holograms: pictures made of light floating over the water, flickering and turning ─────────────── */

#define NHOLO 6
static struct {
  cw_node n[NHOLO];
  t3_material *m[NHOLO];
  float spin[NHOLO], base_y[NHOLO], ph[NHOLO];
  int count;
} HO;

static void holograms(const track *t, cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0x401;
  for (float s = 90; s < t->length - 40 && HO.count < NHOLO; s += 260.0f + 120.0f * frand(&rs)) {
    int side = frand(&rs) < 0.5f ? -1 : 1;
    if (zone_mode(side, s) != 2 || in_run(L.plate, s, 30) || in_run(L.cork, s, 30) || in_run(L.gap, s, 60)) continue;
    tsample a = track_at(t, s);
    t3_vec3 p = vadd(a.p, vmul(a.r, (float)side * (a.hw + 16.0f + 10.0f * frand(&rs))));
    if (near_other_road(t, p, s, 30.0f * 30.0f)) continue;
    int k = HO.count;
    int kind = (k * 3 + 1) % AD_N;
    t3_material *m = basic_add(0.9f, 0.9f, 0.9f, false, AD.tex[kind]);
    t3_geometry *g = t3_plane_geometry_new(10.0f, 5.0f, 1, 1);
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.render_order = 5;
    HO.n[k] = hang(root, "Holo", &me->base);
    t3_release(me), t3_release(g);
    HO.m[k] = m;
    HO.base_y[k] = 7.5f + 3.0f * frand(&rs);
    cw_node3d_set_position(HO.n[k], V(p.x, HO.base_y[k], p.z));
    float rate = 0.25f + 0.2f * frand(&rs);
    HO.spin[k] = rate * (frand(&rs) < 0.5f ? 1 : -1);
    HO.ph[k] = frand(&rs) * 6.28f;
    HO.count++;
  }
}

static void holograms_frame(double time) {
  float tt = (float)time;
  for (int i = 0; i < HO.count; i++) {
    cw_node3d_set_rotation(HO.n[i], V(0, tt * HO.spin[i] + HO.ph[i], 0));
    cw_node3d_set_position(HO.n[i], V(cw_node3d_position(HO.n[i]).x, HO.base_y[i] + 0.5f * sinf(tt * 0.7f + HO.ph[i]), cw_node3d_position(HO.n[i]).z));
    /* flicker like a bad projector: mostly steady, now and then a dropout and a scan band */
    uint32_t h = hash32((uint32_t)(tt * 9.0f) + (uint32_t)i * 7919u);
    float k = (h & 15) == 0 ? 0.15f : (h & 7) == 1 ? 0.55f : 0.85f + 0.15f * sinf(tt * 40.0f);
    HO.m[i]->color = (t3_color){ 0.9f * k, 0.9f * k, 0.9f * k };
  }
}

/* ── drones: little lit machines buzzing over the streets; one has a searchlight it sweeps over the water ── */

#define NDRONE 6
static struct {
  cw_node n[NDRONE];
  cw_node beam;
  float cx[NDRONE], cz[NDRONE], r[NDRONE], y[NDRONE], w[NDRONE], ph[NDRONE];
  int count;
} DR;

static prop_model make_drone(void) {
  pm *b = pm_new(808);
  pm_layer(b, PL_MAIN);
  pm_box_c(b, 1.4f, 0.5f, 1.4f, rgb(0.1f, 0.1f, 0.14f));
  pm_layer(b, PL_METAL);
  for (int i = 0; i < 4; i++) {
    float x = (i & 1 ? 1.0f : -1.0f) * 1.0f, z = (i & 2 ? 1.0f : -1.0f) * 1.0f;
    pm_beam(b, V(0, 0, 0), V(x, 0.1f, z), 0.1f, rgb(0.15f, 0.15f, 0.2f));
    pm_push(b);
    pm_move(b, x, 0.25f, z);
    pm_cyl(b, 0.7f, 0.7f, 0.05f, 10, rgb(0.2f, 0.2f, 0.26f), rgb(0.3f, 0.3f, 0.36f));
    pm_pop(b);
  }
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, 0.6f, 0, 0.9f);
  pm_ball(b, 0.2f, 0.2f, 0.2f, 0, 0, glow(RED, 6.0f), glow(RED, 6.0f));
  pm_pop(b);
  pm_push(b);
  pm_move(b, -0.6f, 0, 0.9f);
  pm_ball(b, 0.2f, 0.2f, 0.2f, 0, 0, glow(LIME, 6.0f), glow(LIME, 6.0f));
  pm_pop(b);
  pm_push(b);
  pm_move(b, 0, -0.35f, 0);
  pm_ball(b, 0.3f, 0.2f, 0.3f, 0, 0, glow(WARM, 5.0f), glow(WARM, 5.0f));
  pm_pop(b);
  return pm_finish(b);
}

static void drones(const track *t, cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0xd0e;
  (void)t;
  for (int i = 0; i < NDRONE; i++) {
    DR.n[i] = model_node(root, "Drone", make_drone(), false);
    float ang = 6.2831853f * frand(&rs);
    DR.cx[i] = L.cx + cosf(ang) * 90.0f * frand(&rs), DR.cz[i] = L.cz + sinf(ang) * 60.0f * frand(&rs);
    DR.r[i] = 40.0f + 90.0f * frand(&rs), DR.y[i] = 22.0f + 30.0f * frand(&rs);
    DR.w[i] = (0.05f + 0.06f * frand(&rs)) * (i & 1 ? 1 : -1), DR.ph[i] = frand(&rs) * 6.28f;
    DR.count++;
  }
  /* the searchlight under drone 0: a pale cone to the water */
  t3_material *m = basic_add(0.16f, 0.2f, 0.24f, true, NULL);
  t3_geometry *g = shaft_geometry(0.5f, 6.0f, 40.0f, 14);
  t3_mesh *me = t3_mesh_new(g, m);
  DR.beam = hang(DR.n[0], "DroneBeam", &me->base);
  t3_release(me), t3_release(g), t3_release(m);
  cw_node3d_set_position(DR.beam, V(0, -0.4f, 0));
}

static void drones_frame(double time) {
  float tt = (float)time;
  for (int i = 0; i < DR.count; i++) {
    float an = DR.ph[i] + tt * DR.w[i];
    float x = DR.cx[i] + cosf(an) * DR.r[i], z = DR.cz[i] + sinf(an) * DR.r[i] * 0.7f;
    float y = DR.y[i] + 1.5f * sinf(tt * 1.3f + (float)i);
    cw_node3d_set_position(DR.n[i], V(x, y, z));
    float dir = DR.w[i] > 0 ? 1.0f : -1.0f;
    cw_node3d_set_rotation(DR.n[i], V(0.12f * sinf(tt * 2.0f + (float)i), atan2f(-sinf(an) * dir, cosf(an) * 0.7f * dir), 0.1f * sinf(tt * 1.7f)));
  }
  if (cw_node_valid(DR.beam)) cw_node3d_set_rotation(DR.beam, V(0.35f * sinf(tt * 0.9f), 0, 0.35f * cosf(tt * 0.7f)));
}

/* ── searchlights from the far rooftops, sweeping the clouds ─────────────────────────────────────── */

#define NSEARCH 4
static struct {
  cw_node n[NSEARCH];
  float ph[NSEARCH], w[NSEARCH];
} SL;

static void searchlights(cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0x5ea;
  t3_material *m = basic_add(0.35f, 0.4f, 0.55f, true, NULL);
  m->fog = true;
  for (int i = 0; i < NSEARCH; i++) {
    float ang = 6.2831853f * ((float)i + 0.3f * frand(&rs)) / (float)NSEARCH, d = 650 + 250 * frand(&rs);
    t3_geometry *g = shaft_geometry(1.5f, 34.0f, 700.0f, 16);
    t3_mesh *me = t3_mesh_new(g, m);
    me->base.frustum_culled = false;
    SL.n[i] = hang(root, "Searchlight", &me->base);
    t3_release(me), t3_release(g);
    cw_node3d_set_position(SL.n[i], V(L.cx + cosf(ang) * d, 60.0f, L.cz + sinf(ang) * d));
    SL.ph[i] = frand(&rs) * 6.28f, SL.w[i] = (0.12f + 0.1f * frand(&rs)) * (i & 1 ? 1 : -1);
    /* the lamp itself, a hot spot on a pole */
    cw_node3d_set_rotation(SL.n[i], V(3.14159265f, 0, 0));
  }
  t3_release(m);
}

static void searchlights_frame(double time) {
  float tt = (float)time;
  for (int i = 0; i < NSEARCH; i++) {
    if (!cw_node_valid(SL.n[i])) continue;
    /* beams point up (the cone hangs down from its node, so flip it) and swing round a cone of directions */
    float an = SL.ph[i] + tt * SL.w[i], tilt = 0.45f + 0.25f * sinf(tt * 0.31f + (float)i);
    t3_quat q = t3_quat_multiply(t3_quat_from_axis_angle(V(0, 1, 0), an), t3_quat_multiply(t3_quat_from_axis_angle(V(1, 0, 0), tilt), t3_quat_from_axis_angle(V(1, 0, 0), 3.14159265f)));
    cw_node3d_set_quaternion(SL.n[i], q);
  }
}

/* ── rafts: junk-built boats drifting by with a lantern, bobbing on the swell ─────────────────────── */

#define NRAFT 5
static struct {
  cw_node n[NRAFT];
  t3_vec3 at[NRAFT];
  float ph[NRAFT], yaw[NRAFT], drift[NRAFT];
  int count;
} RF;

static prop_model make_raft(uint32_t seed) {
  pm *b = pm_new(seed);
  uint32_t rs = seed;
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < 6; i++) {
    pm_push(b);
    pm_move(b, -1.5f + 0.6f * (float)i, 0, 0);
    pm_box(b, 0.55f, 0.2f, 4.0f + 0.4f * frand(&rs), rgb(0.35f, 0.22f, 0.12f), rgb(0.45f, 0.3f, 0.18f));
    pm_pop(b);
  }
  for (int i = 0; i < 3; i++) {
    pm_push(b);
    pm_move(b, -1.0f + 1.0f * (float)i, 0.2f, -1.0f + frand(&rs) * 2.0f);
    pm_cyl(b, 0.4f, 0.4f, 0.9f, 8, i == 1 ? rgb(0.7f, 0.2f, 0.15f) : rgb(0.2f, 0.35f, 0.5f), rgb(0.3f, 0.4f, 0.55f));
    pm_pop(b);
  }
  pm_layer(b, PL_METAL);
  pm_rod(b, V(1.4f, 0.2f, 1.2f), V(1.4f, 3.2f, 1.2f), 0.06f, 5, rgb(0.15f, 0.15f, 0.2f));
  pm_layer(b, PL_MAIN);
  pm_tri(b, V(1.4f, 3.1f, 1.2f), V(1.4f, 2.3f, 1.2f), V(2.6f, 2.7f, 1.2f), rgb(0.9f, 0.2f, 0.5f));
  pm_tri(b, V(1.4f, 3.1f, 1.2f), V(2.6f, 2.7f, 1.2f), V(1.4f, 2.3f, 1.2f), rgb(0.9f, 0.2f, 0.5f));
  pm_layer(b, PL_GLOW);
  pm_push(b);
  pm_move(b, -1.2f, 1.3f, 1.4f);
  pm_ball(b, 0.28f, 0.34f, 0.28f, 0, 0, glow(AMBER, 4.0f), glow(WARM, 5.0f));
  pm_pop(b);
  return pm_finish(b);
}

static void rafts(const track *t, cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0xaf7;
  for (float s = 120; s < t->length - 40 && RF.count < NRAFT; s += 330.0f + 90.0f * frand(&rs)) {
    int side = frand(&rs) < 0.5f ? -1 : 1;
    if (zone_mode(side, s) != 2 || in_run(L.plate, s, 30) || in_run(L.cork, s, 30) || in_run(L.gap, s, 60)) continue;
    tsample a = track_at(t, s);
    t3_vec3 p = vadd(a.p, vmul(a.r, (float)side * (a.hw + 10.0f + 8.0f * frand(&rs))));
    if (near_other_road(t, p, s, 14.0f * 14.0f)) continue;
    int k = RF.count++;
    RF.n[k] = model_node(root, "Raft", make_raft(900u + (uint32_t)k), true);
    RF.at[k] = V(p.x, SEA_Y, p.z);
    RF.ph[k] = frand(&rs) * 6.28f, RF.yaw[k] = frand(&rs) * 6.28f, RF.drift[k] = 0.0f;
    cw_node3d_set_position(RF.n[k], RF.at[k]);
  }
}

static void rafts_frame(double time) {
  float tt = (float)time;
  for (int i = 0; i < RF.count; i++) {
    float y = RF.at[i].y + 0.12f + 0.15f * sinf(tt * 0.9f + RF.ph[i]);
    cw_node3d_set_position(RF.n[i], V(RF.at[i].x, y, RF.at[i].z));
    cw_node3d_set_rotation(RF.n[i], V(0.05f * sinf(tt * 0.8f + RF.ph[i]), RF.yaw[i] + 0.08f * sinf(tt * 0.2f + RF.ph[i]), 0.07f * sinf(tt * 0.7f + RF.ph[i] * 2.0f)));
  }
}

/* ── steam from the street grates, rising past the walls ─────────────────────────────────────────── */

static void steam_vents(const track *t, cw_node root, uint32_t seed) {
  uint32_t rs = seed ^ 0x57e;
  int n = 0;
  for (float s = 60; s < t->length - 30 && n < 14; s += 150.0f + 100.0f * frand(&rs)) {
    int side = frand(&rs) < 0.5f ? -1 : 1;
    if (in_run(L.plate, s, 20) || in_run(L.cork, s, 40) || in_run(L.gap, s, 60)) continue;
    tsample a = track_at(t, s);
    if (!walled(&a, side)) continue;
    t3_vec3 p = vadd(vadd(a.p, vmul(a.r, (float)side * (a.hw + 1.2f))), V(0, 0.2f, 0));
    fx_ambient_at(root, FXA_STEAM, p, V(0.6f, 0.2f, 0.6f), 0.8f, 90);
    n++;
  }
}


/* ── the animation: every frame ──────────────────────────────────────────────────────────────────── */

static struct {
  cw_node scene, sun, env, fog, post;
  cw_node cams[4];
  int ncams;
  bool rain_off[4];
  float sun_e, amb_e;
  float uw;  /* how far under the sea the nearest view is, 0..1 (the fog turns green) */
} A;

/* a soft coloured light that rides above and ahead of each view, so the karts catch the neon colours of the street:
 * it slides between pink and cyan along the course. One light per planned view (a few, cheap, no shadows). */
static cw_node RIM[4];
static void rim_lights(cw_node root) {
  for (int i = 0; i < PLAN_VIEWS && i < 4; i++) {
    char name[12];
    snprintf(name, sizeof name, "Rim%d", i);
    RIM[i] = add_omni(root, name, V(0, 0, 0), PINK, 70, 16);
  }
}
static void rim_frame(double time) {
  for (int i = 0; i < PLAN_VIEWS && i < 4 && i < A.ncams; i++) {
    if (!cw_node_valid(RIM[i]) || !cw_node_valid(A.cams[i])) continue;
    const t3_mat4 *m = cw_node3d_global_transform(A.cams[i]);
    t3_vec3 p = V(m->e[12], m->e[13], m->e[14]), f = V(-m->e[8], -m->e[9], -m->e[10]);
    cw_node3d_set_position(RIM[i], vadd(vadd(p, vmul(f, 6.0f)), V(0, 3.0f, 0)));
    float k = 0.5f + 0.5f * sinf((float)time * 0.35f + p.x * 0.01f + p.z * 0.013f);
    cw_node_set(RIM[i], "color", cw_val_color(lerpf(1.0f, 0.1f, k), lerpf(0.2f, 0.85f, k), lerpf(0.55f, 1.0f, k), 1));
  }
}

static float flicker_level(int g, double time) {
  switch (g) {
  case GR_FLICK: { /* a tube on the way out: stutters and drops */
    uint32_t h = hash32((uint32_t)(time * 11.0));
    float r = (float)(h & 0xff) / 255.0f;
    float slow = hash32((uint32_t)(time * 0.7)) & 1 ? 1.0f : 0.8f;
    return (r < 0.1f ? 0.1f : r < 0.2f ? 0.55f : 1.0f) * slow;
  }
  case GR_BUZZ: { /* steady with a hum, and now and then a blink */
    uint32_t h = hash32((uint32_t)(time * 3.0) + 77u);
    float blink = (h & 0x3f) == 0 ? 0.15f : 1.0f;
    return blink * (0.9f + 0.1f * sinf((float)time * 31.0f));
  }
  case GR_PULSE: return 0.7f + 0.3f * sinf((float)time * 2.2f);
  default: return 1.0f;
  }
}


/* ── the sky: a dome of storm cloud lit from underneath by the city, and bolts of lightning ──────── */

static float vn3(float x, float y, float z, uint32_t seed) {
  float fx = floorf(x), fy = floorf(y), fz = floorf(z);
  int ix = (int)fx, iy = (int)fy, iz = (int)fz;
  float u = x - fx, v = y - fy, w = z - fz;
  u = u * u * (3 - 2 * u), v = v * v * (3 - 2 * v), w = w * w * (3 - 2 * w);
  float c[8];
  for (int k = 0; k < 8; k++) {
    uint32_t h = hash32((uint32_t)(ix + (k & 1)) * 73856093u ^ (uint32_t)(iy + ((k >> 1) & 1)) * 19349663u ^ (uint32_t)(iz + ((k >> 2) & 1)) * 83492791u ^ seed);
    c[k] = (float)(h & 0xffffff) / 16777215.0f;
  }
  float a = lerpf(lerpf(c[0], c[1], u), lerpf(c[2], c[3], u), v), b = lerpf(lerpf(c[4], c[5], u), lerpf(c[6], c[7], u), v);
  return lerpf(a, b, w);
}
static float fbm3(float x, float y, float z, int oct, uint32_t seed) {
  float s = 0, a = 0.5f, tot = 0;
  for (int i = 0; i < oct; i++) {
    s += a * vn3(x, y, z, seed + (uint32_t)i * 131u), tot += a;
    x *= 2.03f, y *= 2.03f, z *= 2.03f, a *= 0.5f;
  }
  return s / tot;
}

static t3_texture *cloud_texture(void) {
  const int W = 1024, H = 256;
  uint8_t *px = malloc((size_t)W * H * 4);
  for (int j = 0; j < H; j++) {
    float v = 1.0f - ((float)j + 0.5f) / (float)H; /* 1 at the zenith, 0 at the horizon */
    float e = v * 1.5707963f * 1.15f - 0.12f;      /* elevation, a little below the horizon at v = 0 */
    float ce = cosf(e), se = sinf(e);
    for (int i = 0; i < W; i++) {
      float th = ((float)i + 0.5f) / (float)W * 6.2831853f;
      float dx = cosf(th) * ce, dz = sinf(th) * ce, dy = se;
      /* the clouds: big slow billows with torn edges, thinner toward the zenith */
      float n = fbm3(dx * 3.0f + 7, dy * 6.0f, dz * 3.0f, 5, 3);
      float n2 = fbm3(dx * 9.0f, dy * 18.0f + 3, dz * 9.0f, 3, 91);
      float dens = sm01(0.38f, 0.72f, n + (n2 - 0.5f) * 0.25f);
      float hz = powf(fclampf(1.0f - e / 1.2f, 0, 1), 2.2f); /* city glow, strongest at the horizon */
      rgb8 clear = mix3((rgb8){ 0.012f, 0.014f, 0.05f }, (rgb8){ 0.10f, 0.05f, 0.2f }, hz);
      /* a cloud is lit from below: pink and violet at its underside, black on top */
      rgb8 lit = mix3((rgb8){ 0.05f, 0.04f, 0.1f }, (rgb8){ 0.55f, 0.16f, 0.45f }, hz * (0.35f + 0.65f * (1.0f - n2)));
      rgb8 c = mix3(clear, lit, dens);
      if (e < 0) c = mix3(c, (rgb8){ 0.08f, 0.04f, 0.14f }, sm01(0.0f, -0.12f, e));
      uint8_t *p = px + ((size_t)j * W + i) * 4;
      p[0] = (uint8_t)(fclampf(c.r, 0, 1) * 255.0f), p[1] = (uint8_t)(fclampf(c.g, 0, 1) * 255.0f), p[2] = (uint8_t)(fclampf(c.b, 0, 1) * 255.0f), p[3] = 255;
    }
  }
  t3_texture *t = t3_texture_new(W, H, px);
  free(px);
  t->color_space = T3_SRGB_COLOR_SPACE;
  t->wrap_s = t->wrap_t = T3_REPEAT;
  return t;
}

#define NBOLT 7
static struct {
  cw_node dome;
  t3_material *dome_mat, *bolt_mat;
  cw_node bolt[NBOLT];
  float yaw;
} SKY;

static void build_sky(cw_node root) {
  t3_geometry *g = t3_sphere_geometry_new(1900, 48, 24);
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  t3_texture *tex = cloud_texture();
  t3_material_set_map(m, tex);
  t3_release(tex);
  m->side = T3_BACK_SIDE;
  m->fog = false;
  m->depth_write = false;
  SKY.dome_mat = t3_retain(m);
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.frustum_culled = false;
  me->base.render_order = -100;
  SKY.dome = hang(root, "SkyDome", &me->base);
  cw_node3d_set_position(SKY.dome, V(L.cx, 0, L.cz));
  t3_release(me), t3_release(g), t3_release(m);
  /* bolts: a jagged ribbon each, far out round the horizon, hidden until a flash picks one */
  SKY.bolt_mat = t3_mesh_basic_material_new(0xffffff);
  SKY.bolt_mat->color = (t3_color){ 5.0f, 4.6f, 7.0f };
  SKY.bolt_mat->side = T3_DOUBLE_SIDE;
  SKY.bolt_mat->fog = false;
  uint32_t rs = 4242;
  for (int k = 0; k < NBOLT; k++) {
    float ang = 6.2831853f * ((float)k + 0.3f * frand(&rs)) / (float)NBOLT, dist = 900 + 500 * frand(&rs);
    float top = 700 + 200 * frand(&rs), bottom = 30;
    const int segs = 14;
    float *pos = malloc(sizeof(float) * 3 * 2 * (size_t)(segs + 1) * 3);
    uint32_t *ix = malloc(sizeof(uint32_t) * 6 * (size_t)segs * 3);
    int nv = 0, ni = 0;
    /* the main stroke and two forks, each a strip of quads facing the middle of the course */
    for (int br = 0; br < 3; br++) {
      float x = 0, y = br == 0 ? top : top * (0.45f + 0.2f * frand(&rs)), w = br == 0 ? 11.0f : 6.0f;
      float yend = br == 0 ? bottom : y * 0.35f, dx = br == 0 ? 0 : (br == 1 ? 1.0f : -1.0f);
      int base = nv;
      for (int i = 0; i <= segs; i++) {
        float t = (float)i / (float)segs;
        float yy = lerpf(y, yend, t);
        x += (frand(&rs) - 0.5f) * 90.0f + dx * 25.0f;
        float ww = w * (1.0f - 0.4f * t);
        pos[nv * 3] = x - ww, pos[nv * 3 + 1] = yy, pos[nv * 3 + 2] = 0, nv++;
        pos[nv * 3] = x + ww, pos[nv * 3 + 1] = yy, pos[nv * 3 + 2] = 0, nv++;
      }
      for (int i = 0; i < segs; i++) {
        uint32_t a = (uint32_t)(base + i * 2);
        ix[ni++] = a, ix[ni++] = a + 1, ix[ni++] = a + 2, ix[ni++] = a + 1, ix[ni++] = a + 3, ix[ni++] = a + 2;
      }
    }
    t3_geometry *bg = t3_geometry_new();
    t3_attribute *at = t3_attribute_new(T3_FLOAT32, pos, nv, 3);
    t3_geometry_set_attribute(bg, T3_ATTR_POSITION, at), t3_release(at);
    at = t3_attribute_new(T3_UINT32, ix, ni, 1), t3_geometry_set_index(bg, at), t3_release(at);
    free(pos), free(ix);
    t3_mesh *bm = t3_mesh_new(bg, SKY.bolt_mat);
    bm->base.frustum_culled = false;
    bm->base.visible = false;
    SKY.bolt[k] = hang(root, "Bolt", &bm->base);
    t3_release(bm), t3_release(bg);
    cw_node3d_set_position(SKY.bolt[k], V(L.cx + cosf(ang) * dist, 0, L.cz + sinf(ang) * dist));
    cw_node3d_set_rotation(SKY.bolt[k], V(0, -ang + 1.5707963f, 0));
  }
}

/* lightning: some of every few seconds, a stroke and its flickering echoes; returns the flash 0..1 and the bolt to show */
/* RACER_DROWNED_FLASH=1 holds the first stroke at its peak so a screenshot shows it */
static float storm_flash(double time, int *which) {
  static int hold = -1;
  if (hold < 0) hold = getenv("RACER_DROWNED_FLASH") ? 1 : 0;
  if (hold) time = 1.66 + 0.02;
  const double per = 6.3;
  int k = (int)floor(time / per);
  uint32_t h = hash32((uint32_t)k * 2654435761u + 9u);
  *which = (int)((h >> 12) % NBOLT);
  if ((h & 7) < 2) return 0;
  double t = time - (double)k * per - 0.6 - (double)((h >> 4) & 255) / 255.0 * 3.0;
  if (t < 0) return 0;
  static const float T[4] = { 0, 0.13f, 0.31f, 0.58f }, P[4] = { 1.0f, 0.6f, 0.85f, 0.4f };
  float f = 0;
  for (int i = 0; i < 4; i++) {
    float dt = (float)t - T[i];
    if (dt >= 0) f = fmaxf(f, P[i] * expf(-dt * 8.5f));
  }
  return f;
}


/* under the sea the fog thickens and turns to deep green water: no height fall-off, a short sight line */
static void underwater_frame(void) {
  float uw = 0;
  for (int i = 0; i < A.ncams; i++) {
    if (!cw_node_valid(A.cams[i])) continue;
    t3_vec3 cp = cw_node3d_global_position(A.cams[i]);
    float y = cp.y;
    /* only over the lake: elsewhere a rolled camera under the road is not under the sea */
    float k = lake_axis_dist(cp.x, cp.z, NULL) < LK.r0 ? 1.0f - cw_smoothstep(SEA_Y - 3.5f, SEA_Y - 0.2f, y) : 0.0f;
    uw = fmaxf(uw, k);
    /* no rain falls under the sea: that view's follower stays off (the followers are named Rain, Rain2 .. in view order) */
    bool under = k > 0.6f;
    if (under != A.rain_off[i]) {
      char name[12];
      snprintf(name, sizeof name, i ? "Rain%d" : "Rain", i + 1);
      cw_node r = cw_node_get(A.scene, name);
      if (cw_node_valid(r)) cw_node_set(r, "visible", cw_val_bool(!under));
      A.rain_off[i] = under;
    }
  }
  if (fabsf(uw - A.uw) < 0.004f || !cw_node_valid(A.fog)) return;
  A.uw = uw;
  cw_node_set(A.fog, "density", cw_val_float(lerpf(0.0112f, 0.024f, uw)));
  cw_node_set(A.fog, "height_falloff", cw_val_float(lerpf(0.06f, 0.0f, uw)));
  cw_node_set(A.fog, "color", cw_val_color(lerpf(0.05f, 0.0f, uw), lerpf(0.02f, 0.08f, uw), lerpf(0.09f, 0.1f, uw), 1));
}


/* dev aid: RACER_DROWNED_CAMW=x,y,z,tx,ty,tz does the same in world coordinates.
 * RACER_DROWNED_CAM=s,lat,height,ts,tlat,theight puts the first view at that place in road coordinates (s metres along,
 * lat to the right, height above the road) looking at the second, whatever the race is doing: for looking at set pieces */
static void dev_camera(void) {
  static int mode = -1;
  static float v[6];
  if (mode < 0) {
    const char *e = getenv("RACER_DROWNED_CAM"), *w = getenv("RACER_DROWNED_CAMW");
    mode = e && sscanf(e, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6 ? 1 : 0;
    if (!mode && w && sscanf(w, "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) mode = 2;
  }
  if (!mode || A.ncams < 1 || !cw_node_valid(A.cams[0])) return;
  t3_vec3 eye = mode == 1 ? track_point(L.t, v[0], v[1], v[2]) : V(v[0], v[1], v[2]), at = mode == 1 ? track_point(L.t, v[3], v[4], v[5]) : V(v[3], v[4], v[5]);
  cw_node3d_set_position(A.cams[0], eye);
  cw_node3d_look_at(A.cams[0], at, V(0, 1, 0));
  cw_node3d_snapshot(A.cams[0]);
}

static void scenery_frame(float alpha, double time) {
  (void)alpha;
  static int frame_no;
  if ((frame_no++ & 63) == 0) { /* the views come and go with the player count: look again now and then */
    static const char *const names[4] = { "Camera", "Camera2", "Camera3", "Camera4" };
    A.ncams = 0;
    for (int i = 0; i < 4; i++) {
      cw_node c = cw_node_get(A.scene, names[i]);
      if (cw_node_valid(c)) A.cams[A.ncams++] = c;
    }
  }
  for (int g = 1; g < GR_N; g++) {
    float k = flicker_level(g, time);
    if (N.mat[g]) N.mat[g]->color = (t3_color){ k, k, k };
    if (PO.mat[g]) PO.mat[g]->color = (t3_color){ k, k, k };
  }
  water_frame(time);
  creatures_frame(time);
  ruins_frame(time);
  holograms_frame(time);
  drones_frame(time);
  searchlights_frame(time);
  rafts_frame(time);
  underwater_frame();
  dev_camera();
  rim_frame(time);
  /* the storm */
  int bolt = 0;
  float f = storm_flash(time, &bolt);
  if (cw_node_valid(A.sun)) {
    float e = A.sun_e + 7.0f * f;
    cw_node_set(A.sun, "energy", cw_val_float(e));
  }
  if (cw_node_valid(A.env)) cw_node_set(A.env, "ambient_energy", cw_val_float(A.amb_e + 0.5f * f));
  if (SKY.dome_mat) {
    float k = 1.0f + 3.2f * f;
    SKY.dome_mat->color = (t3_color){ k, k, k * 1.1f };
  }
  for (int i = 0; i < NBOLT; i++)
    if (SKY.bolt[i].id || cw_node_valid(SKY.bolt[i])) {
      t3_object *o = cw_node3d_object(SKY.bolt[i]);
      if (o) o->visible = (i == bolt && f > 0.2f);
    }
  if (cw_node_valid(WHEEL)) cw_node3d_set_rotation(WHEEL, V(0, 0, (float)time * 0.06f));
  /* the clouds creep round */
  SKY.yaw = (float)time * 0.0035f;
  if (cw_node_valid(SKY.dome)) cw_node3d_set_rotation(SKY.dome, V(0, SKY.yaw, 0));
}


/* ── the scenery ─────────────────────────────────────────────────────────────────────────────────── */

static void scenery(cw_node scene, const track *t, uint32_t seed) {
  /* a race loaded before left handles into a freed scene in these: start every load from nothing */
  memset(&N, 0, sizeof N), memset(&AD, 0, sizeof AD), memset(&LK, 0, sizeof LK), memset(&RU, 0, sizeof RU);
  memset(&HO, 0, sizeof HO), memset(&DR, 0, sizeof DR), memset(&SL, 0, sizeof SL), memset(&RF, 0, sizeof RF), memset(&PO, 0, sizeof PO);
  scan_course(t);
  init_colours();
  cw_node root = cw_node_new("Node3D", "Drowned");
  cw_node_add_child(scene, root);
  /* the lake lies along the glass tube: its axis runs from one mouth to the other */
  {
    float s0 = L.plate.s0, s1 = L.plate.s1;
    tsample a0 = track_at(t, s0), a1 = track_at(t, s1);
    LK.a = a0.p, LK.b = a1.p;
    for (int i = 0; i <= 64; i++) LK.prof[i] = track_at(t, lerpf(s0, s1, (float)i / 64.0f)).p.y;
    LK.bed_below = 6.0f, LK.r0 = 70.0f, LK.r1 = 150.0f;
  }
  ground_opts go = ground_defaults();
  go.height = city_ground;
  go.tex = TEX_GROUND_ASH;
  go.tint = go.tint2 = rgb(0.16f, 0.17f, 0.22f);
  go.seed = seed;
  go.base_y = STREET_BED;
  ground_plane(root, t, &go);

  water_opts wo = water_defaults();
  wo.color = rgb(0.10f, 0.25f, 0.42f);
  wo.deep = rgb(0.02f, 0.05f, 0.12f);
  wo.clarity = 0.25f;
  wo.wave_height = 0.5f;
  wo.wave_length = 26;
  wo.reflect = 0.9f;
  wo.refract = false;
  wo.grid = 300;
  int nv = game_planned_views();
  /* split screen: the sea reflection draws the whole city again for every view, so with two or more views the sea is a flat
   * tinted film lit by the environment instead (the film over the fords keeps its own look) */
  cw_node sea = water_surface(root, L.cx, L.cz, 4200, 4200, SEA_Y, &wo);
  if (nv > 1) cw_node_set(sea, "reflection", cw_val_bool(false));
  /* the glass tube runs under this sea, and the engine draws a camera under the surface as seen through the water: with
   * the default 5 m of clarity the tube was a wall of teal haze (the kart ahead half gone, nothing outside the glass). The
   * tube is air, so the water reads clear enough to see the ribs ahead, the jellyfish and the drowned blocks, and its
   * light shafts are softened so they streak the view instead of whiting it out. */
  cw_node_set(sea, "clarity", cw_val_float(60.0f));
  cw_node_set(sea, "caustics", cw_val_float(0.28f));
  PLAN_VIEWS = nv;

  fx_ambient_follow(scene, FXA_RAIN, 1.0f);

  ad_make();
  neon_begin();
  dress_walls(t, seed);
  street_blocks(t, seed);
  concrete_dark = rgb(0.16f, 0.16f, 0.2f), concrete_light = rgb(0.34f, 0.32f, 0.38f), rust_col = rgb(0.4f, 0.2f, 0.1f);
  start_arch(t, root);
  billboards(t, root, seed);
  holograms(t, root, seed);
  drones(t, root, seed);
  searchlights(root, seed);
  rafts(t, root, seed);
  steam_vents(t, root, seed);
  jump_set_piece(t, root, seed);
  corkscrew_ruins(t, root, seed);
  flood_dressing(t, root, seed);
  build_wheel(t, root);
  {
    dress_style ds = dress_defaults();
    ds.main = rgb(0.12f, 0.12f, 0.18f), ds.accent = PINK, ds.lamp = CYAN, ds.has_colours = true, ds.seed = seed;
    dress_start_line(root, t, &ds);
    dress_checkpoints(root, t, &ds);
    deco_jump_marks(root, t, &ds);
  }

  /* the skyline */
  prop_set *ps = ps_new(root, "Towers");
  scatter_opts so = scatter_defaults();
  so.kinds[0] = PK_SKYSCRAPER, so.weight[0] = 1, so.nkinds = 1;
  so.cx = L.cx, so.cz = L.cz, so.half_x = 0.5f * (L.x1 - L.x0) + 40, so.half_z = 0.5f * (L.z1 - L.z0) + 40;
  so.density = nv > 1 ? 0.1f : 0.2f, so.scale_min = 0.9f, so.scale_max = 2.2f;
  so.margin = 22, so.ground = city_ground, so.mask = tower_mask, so.seed = seed;
  scatter_area(ps, t, &so);
  so.half_x = so.half_z = 0, so.ring_min = 0.5f * (L.x1 - L.x0) + 80, so.ring_max = nv > 1 ? 900 : 1500, so.density = nv > 1 ? 0.008f : 0.012f, so.scale_max = 3.0f;
  so.far_thin = 0.0f;
  scatter_area(ps, t, &so);
  ps_finish(ps);

  tube_dressing(t);
  lake_surface_and_shafts(root);
  creatures(root);
  lakebed(t, root, seed);
  rim_lights(root);
  build_sky(root);
  A.scene = scene;
  A.sun = cw_node_get(scene, "Sun"), A.env = cw_node_get(scene, "Env"), A.fog = cw_node_get(scene, "Fog");
  A.sun_e = 0.5f, A.amb_e = 0.1f;
  neon_finish(root);
  pools_finish(root);
  ad_finish(root);
}

const map_def map_drowned = {
  .name = "Drowned Downtown",
  .blurb = "A neon city under the tide: fords, glass tunnels, rain.",
  .scene = "scenes/drowned.cwscene",
  .spec = &(track_spec){ .name = "drowned", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 9, .circuit = true },
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.5f, 0.52f, 0.62f }, .kerb_a = { 1.0f, 0.12f, 0.55f }, .kerb_b = { 0.1f, 0.1f, 0.2f }, .wall = { 0.55f, 0.58f, 0.75f },
            .wall_top = { 0.3f, 0.32f, 0.45f }, .road_tex = TEX_ROAD_WET, .wall_tex = TEX_WALL_TECH, .road_rough = 0.4f,
            .wall_emissive = true, .glow = { 0.1f, 0.85f, 1.0f } },
  .laps = 3,
  .par_lap = 60,
  .scenery = scenery,
  .scenery_frame = scenery_frame,
};
