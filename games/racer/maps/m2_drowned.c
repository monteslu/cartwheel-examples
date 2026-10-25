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
  for (int i = 0; i < L.nwater; i++) cw_info("drowned: ford %d s %.0f..%.0f", i, L.water[i].s0, L.water[i].s1);
  for (int i = 0; i < L.nboost; i++) cw_info("drowned: boost %d s %.0f..%.0f", i, L.boost[i].s0, L.boost[i].s1);
}

/* ── the ground: flooded street bed, with a deep lake basin over the glass tube ──────────────────── */

#define SEA_Y (-1.6f)

static struct {
  float lake_cx, lake_cz, lake_r;
} G;

static float city_ground(float x, float z, void *ud) {
  (void)ud;
  float h = -4.0f + ground_noise(x, z, 90, 1.6f, 3, 11);
  /* the lake basin: a bowl round the dip, a few metres under the tube */
  h += ground_basin(x, z, G.lake_cx, G.lake_cz, G.lake_r, G.lake_r, -L.lowest_y + 2.0f, 40);
  return h;
}


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
        pm_move(b, 0, 1.55f + 0.3f * frand(&rs), 0);
        neon_sign(b, w, h, (int)(frand(&rs) * 4.99f), neon_pick(&rs), neon_pick(&rs), (uint32_t)(s * 31.0f) + (uint32_t)side * 7u);
        pm_pop(b);
      } else if (r < 0.58f) {
        int kind = (int)(frand(&rs) * (float)AD_N) % AD_N;
        float w = 2.6f, h = 1.3f;
        t3_vec3 p = vadd(vadd(a.p, vmul(a.r, (float)side * (a.hw - 0.05f))), vmul(a.u, 1.1f + 0.5f * frand(&rs)));
        t3_vec3 n = vmul(a.r, (float)-side);
        ad_add(kind, p, n, a.u, w, h);
      }
      /* the awning over the wall top, and the lamp */
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
        blade_sign(b, 0, 4.25f + 0.5f * frand(&rs), 1.7f, 1.7f, 2.6f + 1.2f * frand(&rs), (int)(frand(&rs) * 4.99f), c1, c2, (uint32_t)(s * 17.0f));
        pm_pop(b);
      }
      if (!clear && ((bay % 4) == 0)) {
        pm *b = wall_frame(N.c[GR_STEADY], &a, side, 0.0f, 0.0f);
        gooseneck(b, 0, 3.2f, 1.7f, frand(&rs) < 0.5f ? WARM : hex(0xffb0e0));
        pm_pop(b);
      }
      s += step;
    }
  }
}

/* ── the buildings along the road ────────────────────────────────────────────────────────────────── */

typedef struct tint_set { pcol wall0, wall1; pcol a, b; } tint_set;

static pcol window_colour(uint32_t *rs, const tint_set *ts) {
  float r = frand(rs);
  pcol c = r < 0.5f ? WARM : r < 0.64f ? ts->a : r < 0.78f ? ts->b : r < 0.9f ? hex(0xcfe8ff) : AMBER;
  return glow(c, 1.3f + 1.2f * frand(rs));
}

/* the windows of one face, in the face's own frame: x along, y up, z out of the face, face is w wide, y0..y1 */
static void windows_face(pm *b, float w, float y0, float y1, float lit, const tint_set *ts, uint32_t seed) {
  uint32_t rs = seed;
  pm_layer(b, PL_GLOW);
  const float cw = 3.4f, ch = 3.8f;
  int nx = (int)((w - 1.6f) / cw), ny = (int)((y1 - y0) / ch);
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      if (frand(&rs) > lit) continue;
      float x = -w * 0.5f + 0.8f + ((float)i + 0.5f) * (w - 1.6f) / (float)nx, y = y0 + ((float)j + 0.5f) * ch;
      float ww = 1.15f + 0.4f * frand(&rs), hh = 1.6f + 0.4f * frand(&rs);
      pcol c = window_colour(&rs, ts);
      pm_quad(b, V(x - ww, y - hh * 0.5f, 0.06f), V(x + ww, y - hh * 0.5f, 0.06f), V(x + ww, y + hh * 0.5f, 0.06f), V(x - ww, y + hh * 0.5f, 0.06f), c);
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
  pm_push(b);
  pm_move(b, 0, h, 0);
  pm_box(b, depth + 0.7f, 0.7f, len + 0.7f, dk(ts.wall1, 0.9f), ts.wall1);
  /* rooftop: a stack or two, a water tank, a mast with a red lamp */
  int nr = 1 + (int)(frand(&rs) * 2.5f);
  for (int i = 0; i < nr; i++) {
    float rx = (frand(&rs) - 0.5f) * depth * 0.5f, rz = (frand(&rs) - 0.5f) * len * 0.6f;
    pm_push(b);
    pm_move(b, rx, 0.7f, rz);
    if (frand(&rs) < 0.5f) pm_box(b, 3.0f + 2.0f * frand(&rs), 2.0f + 2.0f * frand(&rs), 3.0f + 2.5f * frand(&rs), dk(ts.wall0, 1.2f), ts.wall1);
    else pm_cyl(b, 1.6f, 1.6f, 2.6f, 10, dk(ts.wall0, 1.4f), ts.wall1);
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
      neon_sign(sb, fminf(len * 0.7f, 12.0f), 1.5f, (int)(frand(&rs) * 4.99f), neon_pick(&rs), neon_pick(&rs), seed + 99u);
      pm_pop(sb);
    }
  }
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
      float h = mode == 0 ? 20 + 36 * frand(&rs) * frand(&rs) + (frand(&rs) < 0.12f ? 30 : 0) : 7 + 7 * frand(&rs);
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

/* ── the animation: every frame ──────────────────────────────────────────────────────────────────── */

static struct {
  cw_node sun, env, fog, post;
  cw_node cams[4];
  float sun_e, amb_e;
  t3_vec3 fog_col;
  float fog_density;
  float flash;
} A;

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
static float storm_flash(double time, int *which) {
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


static float smooth_to(float cur, float want, float k) { return cur + (want - cur) * k; }

static void scenery_frame(float alpha, double time) {
  (void)alpha;
  for (int g = 1; g < GR_N; g++)
    if (N.mat[g]) {
      float k = flicker_level(g, time);
      N.mat[g]->color = (t3_color){ k, k, k };
    }
  water_frame(time);
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
  /* the clouds creep round */
  SKY.yaw = (float)time * 0.0035f;
  if (cw_node_valid(SKY.dome)) cw_node3d_set_rotation(SKY.dome, V(0, SKY.yaw, 0));
}


/* ── the scenery ─────────────────────────────────────────────────────────────────────────────────── */

static void scenery(cw_node scene, const track *t, uint32_t seed) {
  scan_course(t);
  init_colours();
  cw_node root = cw_node_new("Node3D", "Drowned");
  cw_node_add_child(scene, root);
  /* the lake sits over the middle of the dip */
  {
    int a = (int)(0.5f * (L.dip.s0 + L.dip.s1));
    const tsample *m = &t->s[a < 0 ? 0 : a >= t->n ? t->n - 1 : a];
    G.lake_cx = m->p.x, G.lake_cz = m->p.z, G.lake_r = 0.5f * (L.dip.s1 - L.dip.s0) + 40.0f;
  }
  ground_opts go = ground_defaults();
  go.height = city_ground;
  go.tex = TEX_GROUND_ASH;
  go.tint = go.tint2 = rgb(0.16f, 0.17f, 0.22f);
  go.seed = seed;
  go.base_y = -4.0f;
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
  water_surface(root, L.cx, L.cz, 4200, 4200, SEA_Y, &wo);

  fx_ambient_follow(scene, FXA_RAIN, 1.0f);

  ad_make();
  neon_begin();
  dress_walls(t, seed);
  street_blocks(t, seed);

  /* the skyline */
  prop_set *ps = ps_new(root, "Towers");
  scatter_opts so = scatter_defaults();
  so.kinds[0] = PK_SKYSCRAPER, so.weight[0] = 1, so.nkinds = 1;
  so.cx = L.cx, so.cz = L.cz, so.half_x = 0.5f * (L.x1 - L.x0) + 40, so.half_z = 0.5f * (L.z1 - L.z0) + 40;
  so.density = 0.12f, so.scale_min = 0.9f, so.scale_max = 2.2f;
  so.margin = 22, so.base_y = -4.0f, so.seed = seed;
  scatter_area(ps, t, &so);
  so.half_x = so.half_z = 0, so.ring_min = 0.5f * (L.x1 - L.x0) + 80, so.ring_max = 1500, so.density = 0.012f, so.scale_max = 3.0f;
  so.far_thin = 0.0f;
  scatter_area(ps, t, &so);
  ps_finish(ps);

  build_sky(root);
  A.sun = cw_node_get(scene, "Sun"), A.env = cw_node_get(scene, "Env");
  A.sun_e = 0.5f, A.amb_e = 0.1f;
  neon_finish(root);
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
