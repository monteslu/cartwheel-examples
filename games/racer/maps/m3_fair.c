/* map 3: Rustwheel Fairgrounds, an abandoned amusement park at golden sunset; the course is its coaster */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/particles.h"
#include "../props.h"

static const piece SPEC[] = {
  WALLS(2, 2),
  ITEMS(),
  STRAIGHT(110),
  CHECK(),
  BOOST(30),
  STRAIGHT(40),
  LOOP(28, 1),
  STRAIGHT(40),
  ITEMS(),
  BANKED(-30, 90, -20),
  BANKED(30, 90, 20),
  WIDTH(30, 4.6f),
  CORKSCREW(190, 1),
  WIDTH(30, 7.5f),
  STRAIGHT(30),
  CHECK(),
  ABANKED(180, 330, 26),
  ASTRAIGHT(40),
  ITEMS(),
  BOOST(30),
  STRAIGHT(30),
  SLOPE(70, 24),
  SLOPE(40, 24),
  SLOPE(36, 0),
  STRAIGHT(20),
  /* tall walls from the top of the drop round the whole helix: a kart comes off the drop at up to 74 m/s (boosted, fast
   * class) and must bounce along them, never ride over onto the grass under the helix deck */
  WALLS(3, 3),
  SLOPE(50, -28),
  SLOPE(50, -28),
  SLOPE(60, 0),
  ASTRAIGHT(30),
  ITEMS(),
  /* the helix comes straight off the big drop at up to 74 m/s: banked into the turn so that speed holds the deck (flat,
   * karts flew off its outside) */
  MOVE(24, 0, KEEP, 24, KEEP),
  HILL(320, 360, 8),
  MOVE(20, 0, KEEP, 0, KEEP),
  WALLS(2, 2),
  CHECK(),
  SLOPE(140, -4),
  SLOPE(80, 0),
  STRAIGHT(30),
  BOOST(30),
  STRAIGHT(30),
  SLOPE(36, 20),
  SLOPE(20, 20),
  JUMP(0, 29),
  SLOPE(70, 0),
  ASTRAIGHT(40),
  ITEMS(),
  LOOP(24, -1),
  ASTRAIGHT(40),
  ABANKED(180, 330, 26),
  ATURN(20, 90),
  ATURN(-20, 90),
  ASTRAIGHT(100),
  ASLOPE(50, 0),
  ASTRAIGHT(60),
  STRAIGHT(60),
  END(),
};

/* The boardwalk: a raised wooden walk straight across the first hairpin, between the big tops and the haunted house.
 * Walled through its two bends, but the raised middle has no rails at all: a wobble up there is a fall. */
static const piece BOARDWALK[] = {
  SURFACE(SURF_WOOD),
  MOVE(12, 5, KEEP, 0, KEEP),
  ATURN(85, 62),
  SLOPE(24, 18),
  WALLS(0, 0),
  SLOPE(24, 0),
  ASTRAIGHT(60),
  SLOPE(24, -18),
  WALLS(1, 1),
  ASLOPE(24, 0),
  ATURN(85, 62),
  ASTRAIGHT(6),
  END(),
};
static const branch_spec BRANCHES[] = {
  { .name = "boardwalk", .s0 = 840, .lat0 = -3.9f, .s1 = 1206, .lat1 = -3.9f, .pieces = BOARDWALK, .npieces = sizeof BOARDWALK / sizeof BOARDWALK[0],
    .hw0 = 3.6f, .walls_l = 1, .walls_r = 1, .ai_take = { 0.1f, 0.4f, 0.85f } },
};
#define MAP_SPEC (&(track_spec){ .name = "fair", .pieces = SPEC, .npieces = sizeof SPEC / sizeof SPEC[0], .hw0 = 7.5f, .circuit = true, .branches = BRANCHES, .nbranches = 1 })

/* ── where things are ──────────────────────────────────────────────────────────────────────────────
 * Everything is placed from the built track (its pieces are found by kind, never by index) so a change to the
 * course moves the set pieces with it. */

typedef struct fair_pos {
  t3_vec3 loop1_c, loop2_c;   /* the middle of each loop's circle */
  float loop1_r, loop2_r;
  t3_vec3 helix_c;            /* the axis of the helix, at ground level */
  float helix_r, helix_top;   /* radius of the road round it, the road's height where it ends */
  float helix_s0, helix_s1;
  float crest_s;              /* the top of the lift hill */
  float drop_s;               /* the foot of the drop */
  float jump_s0, jump_s1;
  int jump_i;
  t3_vec3 cork_c;             /* the middle of the corkscrew (ground level) */
  float cork_hl;              /* half its length */
} fair_pos;

static fair_pos P;
static const track *TR;
static int THIN; /* 1 when the race has split views: the clutter far from the road and the bulbs are thinned */
static cw_node find_type(cw_node n, const char *type);
static void report_meshes(cw_node root);
static bool room(const track *t, float x, float z, float r);

static t3_vec3 V3(float x, float y, float z) { return (t3_vec3){ x, y, z }; }

/* the sample range a piece of the spec covers */
static bool piece_range(const track *t, int piece, int *i0, int *i1) {
  *i0 = -1, *i1 = -1;
  for (int i = 0; i < t->n; i++)
    if (t->s[i].seg == piece) {
      if (*i0 < 0) *i0 = i;
      *i1 = i;
    }
  return *i0 >= 0;
}

static void find_places(const track *t) {
  memset(&P, 0, sizeof P);
  int nl = 0;
  for (int k = 0; k < (int)(sizeof SPEC / sizeof SPEC[0]); k++) {
    const piece *pc = &SPEC[k];
    int i0, i1;
    if (!piece_range(t, k, &i0, &i1)) continue;
    if (pc->kind == P_LOOP && nl < 2) {
      /* the circle: the samples with the loop flag, centre at the middle of their bounding box */
      float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
      for (int i = i0; i <= i1; i++)
        if (t->s[i].flags & TF_LOOP) {
          const float p[3] = { t->s[i].p.x, t->s[i].p.y, t->s[i].p.z };
          for (int a = 0; a < 3; a++) mn[a] = fminf(mn[a], p[a]), mx[a] = fmaxf(mx[a], p[a]);
        }
      t3_vec3 c = V3((mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f);
      if (nl == 0) P.loop1_c = c, P.loop1_r = pc->a;
      else P.loop2_c = c, P.loop2_r = pc->a;
      nl++;
    } else if (pc->kind == P_MOVE && fabsf(pc->b) >= 350.0f) {
      /* the helix: a full turn while climbing; its axis is the mean of a whole turn's samples */
      double sx = 0, sz = 0;
      for (int i = i0; i <= i1; i++) sx += t->s[i].p.x, sz += t->s[i].p.z;
      P.helix_c = V3((float)(sx / (i1 - i0 + 1)), 0, (float)(sz / (i1 - i0 + 1)));
      float r = 0;
      for (int i = i0; i <= i1; i++) r += hypotf(t->s[i].p.x - P.helix_c.x, t->s[i].p.z - P.helix_c.z);
      P.helix_r = r / (float)(i1 - i0 + 1);
      P.helix_top = t->s[i1].p.y;
      P.helix_c.y = t->s[i0].p.y;
      P.helix_s0 = t->s[i0].s, P.helix_s1 = t->s[i1].s;
    } else if (pc->kind == P_CORK) {
      P.cork_c = V3((t->s[i0].p.x + t->s[i1].p.x) * 0.5f, 0, (t->s[i0].p.z + t->s[i1].p.z) * 0.5f);
      P.cork_hl = (t->s[i1].s - t->s[i0].s) * 0.5f;
    } else if (pc->kind == P_JUMP) {
      P.jump_i = k;
      P.jump_s0 = t->s[i0].s, P.jump_s1 = t->s[i1].s;
    }
  }
  /* the crest of the lift hill: the highest road between the first boost strip after the first loops and the helix */
  float best = -1e9f;
  for (int i = 0; i < t->n; i++) {
    float s = t->s[i].s;
    if (s > 900 && s < P.helix_s0 && t->s[i].p.y > best && !(t->s[i].flags & TF_LOOP)) best = t->s[i].p.y, P.crest_s = s;
  }
  best = 1e9f;
  for (int i = 0; i < t->n; i++) {
    float s = t->s[i].s;
    if (s > P.crest_s && s < P.helix_s0 + 40 && t->s[i].p.y < best) best = t->s[i].p.y, P.drop_s = s;
  }
}

/* ── ground ──────────────────────────────────────────────────────────────────────────────────────── */

/* the park lies on a plain with soft hills, and the drop and the helix sink into a sunken basin (the old quarry the
 * wooden tower was built in) so the road dives under the plain and climbs back out */
static float fair_ground(float x, float z, void *ud) {
  (void)ud;
  float h = ground_hills(TR, x, z, 7.0f, 160.0f, 25.0f, 180.0f, 41);
  /* the park sits in a wide bowl: low hills rise toward the horizon */
  float dd = hypotf(x - 90.0f, z - 100.0f);
  h += (95.0f + ground_noise(x, z, 380.0f, 55.0f, 3, 77)) * cw_smoothstep(520.0f, 1500.0f, dd);
  float basin = ground_basin(x, z, P.helix_c.x + 20, P.helix_c.z + 20, 150, 170, 12.5f, 60);
  /* a sunken channel along the corkscrew: the road rolls half below its own axis, so the earth has to be out of the way */
  float pit = ground_basin(x, z, P.cork_c.x, P.cork_c.z, 19.0f, P.cork_hl + 8.0f, 9.0f, 15.0f);
  return h + basin + pit;
}

static ground_opts GO;
static struct { cw_node scene, cam; } F;

/* ── helpers ─────────────────────────────────────────────────────────────────────────────────────── */

float tex_metres(int tex); /* props.c: metres per texture repeat of a kind */

static uint32_t RS = 1;
static float rf(void) {
  RS = RS * 1664525u + 1013904223u;
  return (float)(RS >> 8) / 16777216.0f;
}
static float rr(float a, float b) { return a + (b - a) * rf(); }
static float sat(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

/* faded candy colours over rust */
#define RED hex(0xc2453b)
#define CREAM hex(0xeadfc4)
#define TEAL hex(0x3a9a93)
#define MUSTARD hex(0xe2b13e)
#define PINK hex(0xd9809b)
#define SKYB hex(0x4f86b8)
#define RUSTC hex(0x8f4a2c)
#define DRUST hex(0x4a2a1e)
#define WOODC hex(0x6a4a34)
#define ORANGE hex(0xdd7a30)
#define PURPLE hex(0x7a4a9a)
#define NAVY hex(0x2a3a5a)
#define BLACK hex(0x1c1a1c)

/* a part's own frame: a place in the world and a turn about the vertical, so a set piece is designed once, facing +z,
 * and stood anywhere */
typedef struct frame {
  t3_vec3 o;
  float yaw, cy, sy;
} frame;
static frame fr(float x, float y, float z, float yaw) { return (frame){ { x, y, z }, yaw, cosf(yaw), sinf(yaw) }; }
static t3_vec3 fw(const frame *f, float x, float y, float z) {
  return V3(f->o.x + x * f->cy + z * f->sy, f->o.y + y, f->o.z - x * f->sy + z * f->cy);
}
/* the cell builder holding a point of the part, set in the part's frame at that point (pm_pop it when done) */
static pm *fb(pmc *c, const frame *f, float x, float y, float z, prop_layer l) {
  t3_vec3 w = fw(f, x, y, z);
  pm *b = pmc_at(c, w);
  pm_layer(b, l);
  pm_push(b);
  pm_move(b, w.x, w.y, w.z);
  pm_roty(b, f->yaw);
  return b;
}

/* ── glow cells: emissive geometry cut into cells, drawn with a material the map animates ─────────── */

typedef struct gcell {
  int64_t key;
  pm *b;
} gcell;
typedef struct gcells {
  gcell *c;
  int n, cap;
  float chunk;
  uint32_t seed;
} gcells;

static gcells *gc_new(float chunk, uint32_t seed) {
  gcells *g = calloc(1, sizeof *g);
  g->chunk = chunk, g->seed = seed;
  return g;
}
static pm *gc_pm(gcells *g, t3_vec3 p) {
  float inv = 1.0f / g->chunk;
  int64_t cx = (int64_t)floorf(p.x * inv) + 4096, cy = (int64_t)floorf(p.y * inv * 0.5f) + 4096, cz = (int64_t)floorf(p.z * inv) + 4096;
  int64_t key = (cx << 40) | (cy << 20) | cz;
  for (int i = 0; i < g->n; i++)
    if (g->c[i].key == key) return g->c[i].b;
  if (g->n == g->cap) g->cap = g->cap ? g->cap * 2 : 16, g->c = realloc(g->c, sizeof *g->c * (size_t)g->cap);
  pm *b = pm_new(g->seed + (uint32_t)g->n * 7919u);
  pm_layer(b, PL_GLOW);
  g->c[g->n++] = (gcell){ key, b };
  return b;
}

static t3_material *GM[8];
enum { GM_PHASE_A = 0, GM_PHASE_B = 3, GM_FLICK = 6 };

static t3_material *glow_mat_new(void) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->vertex_colors = true;
  return m;
}

static cw_node gc_finish(gcells *g, cw_node parent, const char *name, t3_material *mat) {
  t3_object *group = t3_group_new();
  int tris = 0;
  for (int i = 0; i < g->n; i++) {
    prop_model m = pm_finish(g->c[i].b);
    if (m.g[PL_GLOW]) {
      t3_mesh *me = t3_mesh_new(m.g[PL_GLOW], mat);
      me->base.cast_shadow = false;
      me->base.receive_shadow = false;
      tris += m.g[PL_GLOW]->index->count / 3;
      t3_object_add(group, me);
      t3_release(me);
    }
    prop_model_free(&m);
  }
  cw_node n = hang(parent, name, group);
  t3_release(group);
  free(g->c), free(g);
  cw_info("%s: %d glow triangles", name, tris);
  return n;
}

/* one bulb: a small lump, colour at gain (above 1 it blooms) */
static void bulb(gcells *g, t3_vec3 p, float r, pcol c, float gain) {
  pm *b = gc_pm(g, p);
  pm_push(b);
  pm_move(b, p.x, p.y, p.z);
  /* a small round bulb with a hot core: bright enough that the tone curve takes it to white and bloom spreads it */
  r *= 0.62f;
  pm_ball(b, r, r, r, 0, 0, glow(c, gain * 2.4f), glow(c, gain * 3.0f));
  pm_pop(b);
}
static pcol bulb_col(int i) {
  static const uint32_t pal[6] = { 0xffd070, 0xff7a5a, 0xfff0b0, 0x7fe0d0, 0xff8ab8, 0xffc060 };
  return hex(pal[i % 6]);
}

/* a model built into one builder, as a node of its own (it turns or moves each frame): a mesh per layer */
static t3_object *obj_from_pm(pm *b, t3_material *glowm, int tex, bool cast) {
  if (tex >= 0) pm_uv_scale(b, tex_metres(tex));
  prop_model m = pm_finish(b);
  t3_object *grp = t3_group_new();
  for (int l = 0; l < PL_COUNT; l++) {
    if (!m.g[l]) continue;
    t3_material *mat = l == PL_GLOW ? (glowm ? glowm : prop_material(PL_GLOW, -1, (pcol){ 1, 1, 1 }, 1)) : prop_material(l, l == PL_MAIN ? tex : -1, (pcol){ 1, 1, 1 }, 1);
    t3_mesh *me = t3_mesh_new(m.g[l], mat);
    me->base.cast_shadow = cast && l != PL_GLOW;
    me->base.receive_shadow = true;
    t3_object_add(grp, me);
    t3_release(me);
  }
  prop_model_free(&m);
  return grp;
}
static cw_node node_of(cw_node parent, const char *name, t3_object *grp) {
  cw_node n = hang(parent, name, grp);
  t3_release(grp);
  return n;
}

/* things that turn */
typedef struct spinner {
  cw_node n;
  t3_vec3 axis;
  float rate, phase;
} spinner;
static spinner SPIN[8];
static int nspin;
static void spin_add(cw_node n, t3_vec3 axis, float rate, float phase) {
  if (nspin < 8) SPIN[nspin++] = (spinner){ n, axis, rate, phase };
}

/* a flat-sided ring wall (a tier of a tower, a drum): panels alternate between two colours */
static void drum(pmc *c, const frame *f, float r, float y0, float y1, int n, pcol a, pcol b2, prop_layer l) {
  for (int i = 0; i < n; i++) {
    float a0 = CW_TAU * (float)i / (float)n, a1 = CW_TAU * (float)(i + 1) / (float)n;
    float am = (a0 + a1) * 0.5f;
    pm *b = fb(c, f, cosf(am) * r, y0, -sinf(am) * r, l);
    pcol col = (i & 1) ? b2 : a;
    t3_vec3 p0 = V3(cosf(a0) * r - cosf(am) * r, 0, -sinf(a0) * r + sinf(am) * r), p1 = V3(cosf(a1) * r - cosf(am) * r, 0, -sinf(a1) * r + sinf(am) * r);
    pm_quad(b, p0, p1, V3(p1.x, y1 - y0, p1.z), V3(p0.x, y1 - y0, p0.z), col);
    pm_pop(b);
  }
}
/* a flat disc (annulus when r0 > 0) lying at y */
static void disc(pmc *c, const frame *f, float r0, float r1, float y, int n, pcol col, prop_layer l) {
  for (int i = 0; i < n; i++) {
    float a0 = CW_TAU * (float)i / (float)n, a1 = CW_TAU * (float)(i + 1) / (float)n, am = (a0 + a1) * 0.5f;
    float rm = (r0 + r1) * 0.5f;
    pm *b = fb(c, f, cosf(am) * rm, y, -sinf(am) * rm, l);
    t3_vec3 o = V3(cosf(am) * rm, 0, -sinf(am) * rm);
    t3_vec3 q0 = V3(cosf(a0) * r0 - o.x, 0, -sinf(a0) * r0 - o.z), q1 = V3(cosf(a1) * r0 - o.x, 0, -sinf(a1) * r0 - o.z);
    t3_vec3 q2 = V3(cosf(a1) * r1 - o.x, 0, -sinf(a1) * r1 - o.z), q3 = V3(cosf(a0) * r1 - o.x, 0, -sinf(a0) * r1 - o.z);
    pm_quad(b, q3, q2, q1, q0, col); /* faces up */
    pm_pop(b);
  }
}


/* ── set piece 1: the clown mouth the start straight runs through ────────────────────────────────── */

/* A giant clown's face standing across the road 38 m past the line, looking back at the grid. Built facing +z in its
 * own frame (x right as the driver sees it), then turned round. The mouth is the way through: 24 m wide, 11.5 high. */
static void build_clown(pmc *st, gcells *chase[3], gcells *steady, const frame *f) {
  pcol face0 = pc_mix(CREAM, RUSTC, 0.28f), face1 = CREAM;
  pm *b;
  for (int sd = -1; sd <= 1; sd += 2) {
    b = fb(st, f, sd * 21.0f, 0, -5, PL_MAIN);
    pm_vary(b, 0.05f);
    pm_box(b, 18, 36, 10, face0, face1);
    pm_pop(b);
    /* the red lip beside the opening */
    b = fb(st, f, sd * 13.1f, 0, 0, PL_MAIN);
    pm_box(b, 2.2f, 13.7f, 2.2f, hex(0x8a2a22), RED);
    pm_pop(b);
  }
  b = fb(st, f, 0, 11.5f, -5, PL_MAIN);
  pm_box(b, 24, 24.5f, 10, face0, face1);
  pm_pop(b);
  b = fb(st, f, 0, 11.5f, 0, PL_MAIN); /* the top lip, over the opening */
  pm_box(b, 28.4f, 2.2f, 2.2f, hex(0x8a2a22), RED);
  pm_pop(b);
  /* the throat: a dark red lining inside the opening */
  b = fb(st, f, 0, 11.1f, -5, PL_MAIN);
  pm_box(b, 24, 0.4f, 10, hex(0x4a1410), hex(0x6a1c16));
  pm_pop(b);
  for (int sd = -1; sd <= 1; sd += 2) {
    b = fb(st, f, sd * 11.8f, 0, -5, PL_MAIN);
    pm_box(b, 0.4f, 11.5f, 10, hex(0x4a1410), hex(0x6a1c16));
    pm_pop(b);
  }
  /* teeth hanging in the opening */
  for (int i = 0; i < 9; i++) {
    b = fb(st, f, -10.4f + 2.6f * (float)i, 9.1f, -0.6f, PL_MAIN);
    pm_box(b, 2.3f, 2.4f, 1.0f, hex(0xcdbf9d), hex(0xf4ecd6));
    pm_pop(b);
  }
  /* the nose, above the mouth between the eyes */
  b = fb(st, f, 0, 19.2f, 3.2f, PL_MAIN);
  pm_ball(b, 3.9f, 3.9f, 3.9f, 2, 0.04f, hex(0xa82c20), hex(0xe05a40));
  pm_pop(b);
  b = fb(st, f, -1.2f, 20.6f, 6.4f, PL_MAIN);
  pm_ball(b, 0.9f, 0.7f, 0.5f, 1, 0, CREAM, CREAM);
  pm_pop(b);
  /* eyes, brows, cheeks */
  for (int sd = -1; sd <= 1; sd += 2) {
    float ex = sd * 13.5f, ey = 26.0f;
    b = fb(st, f, ex, ey, 0, PL_MAIN);
    pm_rotx(b, CW_PI * 0.5f);
    pm_cyl(b, 6.2f, 6.2f, 1.2f, 20, hex(0xd8d0bc), hex(0xf6efdc));
    pm_pop(b);
    b = fb(st, f, ex + sd * 0.4f, ey - 0.6f, 1.1f, PL_MAIN);
    pm_rotx(b, CW_PI * 0.5f);
    pm_cyl(b, 3.3f, 3.3f, 0.4f, 16, hex(0x2f6f9a), hex(0x4f9ac8));
    pm_pop(b);
    b = fb(st, f, ex + sd * 0.4f, ey - 0.6f, 1.5f, PL_MAIN);
    pm_rotx(b, CW_PI * 0.5f);
    pm_cyl(b, 1.5f, 1.5f, 0.3f, 12, BLACK, BLACK);
    pm_pop(b);
    /* the brow: a raised arch */
    pm *bb = fb(st, f, ex, 22.5f, 1.2f, PL_MAIN);
    t3_vec3 prev = V3(-9.0f, 1.0f, 0);
    for (int i = 1; i <= 8; i++) {
      float a = CW_PI * (0.18f + 0.64f * (float)i / 8.0f);
      float arch = sd > 0 ? 1.0f : 1.0f;
      t3_vec3 p = V3(-cosf(a) * 9.5f * arch, sinf(a) * 8.5f + 1.0f - (float)sd * (cosf(a)) * 1.6f, 0);
      pm_beam(bb, prev, p, 1.5f, i < 5 ? NAVY : BLACK);
      prev = p;
    }
    pm_pop(bb);
    b = fb(st, f, sd * 22.0f, 14.5f, 0.2f, PL_MAIN);
    pm_rotx(b, CW_PI * 0.5f);
    pm_cyl(b, 3.8f, 3.8f, 0.7f, 14, hex(0xc86a7a), hex(0xe8909e));
    pm_pop(b);
  }
  /* hair: two orange puffs and a striped hat */
  for (int sd = -1; sd <= 1; sd += 2) {
    static const float hp[5][3] = { { 27, 31, -4, }, { 30, 25, -4 }, { 25, 37, -5 }, { 31, 36, -6 }, { 29, 19, -5 } };
    static const float hr[5] = { 8.5f, 6.5f, 6.0f, 5.0f, 5.5f };
    for (int i = 0; i < 5; i++) {
      b = fb(st, f, sd * hp[i][0], hp[i][1], hp[i][2], PL_MAIN);
      pm_ball(b, hr[i], hr[i], hr[i], 2, 0.1f, hex(0xb85a20), ORANGE);
      pm_pop(b);
    }
  }
  {
    int n = 10;
    float r = 11.0f, h = 15.0f;
    for (int i = 0; i < n; i++) {
      float a0 = CW_TAU * (float)i / (float)n, a1 = CW_TAU * (float)(i + 1) / (float)n;
      b = fb(st, f, 0, 36, -5, PL_MAIN);
      t3_vec3 p0 = V3(cosf(a0) * r, 0, -sinf(a0) * r), p1 = V3(cosf(a1) * r, 0, -sinf(a1) * r), tip = V3(3.0f, h, -3.0f);
      pm_tri(b, p0, p1, tip, (i & 1) ? RED : CREAM);
      pm_pop(b);
    }
    b = fb(st, f, 3.0f, 36 + h + 1.6f, -8, PL_MAIN);
    pm_ball(b, 2.6f, 2.6f, 2.6f, 2, 0, MUSTARD, hex(0xf6d878));
    pm_pop(b);
  }
  /* bulbs: the rim of the face, the opening, the eyes, a smile up each cheek, a row on the throat's roof */
  int k = 0;
  for (float x = -29; x <= 29.01f; x += 2.9f, k++) bulb(chase[k % 3], fw(f, x, 35.2f, 0.5f), 0.4f, bulb_col(k), 4.0f);
  for (float y = 2; y < 35; y += 2.9f, k++) {
    bulb(chase[k % 3], fw(f, -29.2f, y, 0.5f), 0.4f, bulb_col(k), 4.0f);
    bulb(chase[(k + 1) % 3], fw(f, 29.2f, y, 0.5f), 0.4f, bulb_col(k + 3), 4.0f);
  }
  for (float x = -13; x <= 13.01f; x += 1.7f, k++) bulb(steady, fw(f, x, 13.9f, 2.4f), 0.34f, hex(0xffe9a0), 4.5f);
  for (float y = 0.8f; y < 13.5f; y += 1.7f) {
    bulb(steady, fw(f, -13.1f, y, 2.4f), 0.34f, hex(0xffe9a0), 4.5f);
    bulb(steady, fw(f, 13.1f, y, 2.4f), 0.34f, hex(0xffe9a0), 4.5f);
  }
  for (int sd = -1; sd <= 1; sd += 2) {
    for (int i = 0; i < 14; i++) {
      float a = CW_TAU * (float)i / 14.0f;
      bulb(steady, fw(f, sd * 13.5f + cosf(a) * 7.4f, 26.0f + sinf(a) * 7.4f, 0.6f), 0.3f, hex(0xffd878), 3.6f);
    }
    for (int i = 0; i <= 7; i++) {
      float t = (float)i / 7.0f;
      bulb(chase[i % 3], fw(f, sd * (15.0f + t * 12.0f), 3.0f + 11.0f * t * t, 0.5f), 0.5f, hex(0xff7a8a), 4.5f);
    }
  }
  for (int row = 0; row < 3; row++)
    for (int i = 0; i < 4; i++) bulb(chase[(row + i) % 3], fw(f, -7.5f + 7.5f * (float)row, 11.0f, -1.5f - 2.4f * (float)i), 0.4f, hex(0xffcf70), 4.5f);
}

/* ── set piece 2: the big wheel the first loop climbs into ───────────────────────────────────────── */

typedef struct ferris {
  cw_node wheel;
  cw_node gon[20];
  t3_vec3 hub;
  float R;
} ferris;
static ferris WHEEL;
static cw_node LIGHT_CAROUSEL, LIGHT_HAUNTED, CHAIN;
static t3_vec3 CHAIN_DIR;

static t3_object *obj_from_model(const prop_model *m, t3_material *glowm, int tex, bool cast) {
  t3_object *grp = t3_group_new();
  for (int l = 0; l < PL_COUNT; l++) {
    if (!m->g[l]) continue;
    t3_material *mat = l == PL_GLOW ? (glowm ? glowm : prop_material(PL_GLOW, -1, (pcol){ 1, 1, 1 }, 1)) : prop_material(l, l == PL_MAIN ? tex : -1, (pcol){ 1, 1, 1 }, 1);
    t3_mesh *me = t3_mesh_new(m->g[l], mat);
    me->base.cast_shadow = cast && l != PL_GLOW;
    me->base.receive_shadow = true;
    t3_object_add(grp, me);
    t3_release(me);
  }
  return grp;
}

static void build_ferris(cw_node root, pmc *st, gcells *steady, t3_vec3 base, float R, float hubY) {
  frame f = fr(base.x, base.y, base.z, 0);
  pcol steel = hex(0xd9cdb0), rust = hex(0x9a5a3a);
  pm *b;
  /* the A frames and the axle housing */
  for (int zs = -1; zs <= 1; zs += 2) {
    float z = zs * 4.6f;
    for (int sd = -1; sd <= 1; sd += 2) {
      b = fb(st, &f, 0, 0, 0, PL_METAL);
      pm_beam(b, V3(sd * R * 0.72f, 0, z * 1.5f), V3(sd * 1.4f, hubY, z), 1.5f, steel);
      pm_beam(b, V3(sd * R * 0.62f, 0.4f * hubY, z * 1.45f), V3(sd * R * 0.12f, 0.55f * hubY, z), 0.7f, rust);
      pm_pop(b);
      b = fb(st, &f, sd * R * 0.72f, 0, z * 1.5f, PL_MAIN);
      pm_box_c(b, 4.2f, 1.6f, 4.2f, hex(0x7a756c));
      pm_pop(b);
    }
    b = fb(st, &f, 0, 0, 0, PL_METAL);
    pm_beam(b, V3(-R * 0.55f, 0.3f * hubY, z * 1.3f), V3(R * 0.55f, 0.3f * hubY, z * 1.3f), 0.8f, steel);
    pm_beam(b, V3(-R * 0.3f, 0.62f * hubY, z * 1.1f), V3(R * 0.3f, 0.62f * hubY, z * 1.1f), 0.7f, steel);
    pm_pop(b);
  }
  b = fb(st, &f, 0, hubY, -7, PL_METAL);
  pm_rotx(b, CW_PI * 0.5f);
  pm_cyl(b, 2.2f, 2.2f, 14, 12, hex(0x7a7468), hex(0xb0a890));
  pm_pop(b);
  /* the wheel: rims, spokes, struts, bulbs, all about the hub */
  pm *w = pm_new(77);
  pm_layer(w, PL_METAL);
  for (int zz = -1; zz <= 1; zz += 2) {
    pm_push(w);
    pm_move(w, 0, 0, zz * 3.4f);
    pm_ring(w, R, 0.6f, 64, 5, steel);
    pm_ring(w, R * 0.5f, 0.42f, 40, 4, steel);
    pm_pop(w);
    for (int i = 0; i < 20; i++) {
      float a = CW_TAU * (float)i / 20.0f;
      pm_beam(w, V3(0, 0, zz * 3.4f), V3(cosf(a) * R, sinf(a) * R, zz * 3.4f), 0.38f, (i & 1) ? rust : steel);
    }
  }
  for (int i = 0; i < 20; i++) {
    float a = CW_TAU * (float)i / 20.0f;
    pm_beam(w, V3(cosf(a) * R, sinf(a) * R, -3.4f), V3(cosf(a) * R, sinf(a) * R, 3.4f), 0.55f, steel);
    pm_beam(w, V3(cosf(a) * R * 0.5f, sinf(a) * R * 0.5f, -3.4f), V3(cosf(a) * R * 0.5f, sinf(a) * R * 0.5f, 3.4f), 0.4f, steel);
  }
  pm_push(w);
  pm_move(w, 0, 0, -5.5f);
  pm_rotx(w, CW_PI * 0.5f);
  pm_cyl(w, 3.0f, 3.0f, 11, 14, hex(0x8a8478), hex(0xc8c0a8));
  pm_pop(w);
  pm_layer(w, PL_MAIN);
  pm_push(w);
  pm_move(w, 0, 0, 5.6f);
  pm_rotx(w, CW_PI * 0.5f);
  pm_cyl(w, 4.2f, 3.0f, 0.8f, 14, RED, CREAM);
  pm_pop(w);
  WHEEL.R = R, WHEEL.hub = V3(base.x, base.y + hubY, base.z);
  t3_object *wo = obj_from_pm(w, NULL, -1, true);
  WHEEL.wheel = node_of(root, "FerrisWheel", wo);
  cw_node3d_set_position(WHEEL.wheel, WHEEL.hub);
  /* rim bulbs ride the wheel: three sets that chase */
  for (int ph = 0; ph < 3; ph++) {
    pm *g = pm_new(88 + (uint32_t)ph);
    pm_layer(g, PL_GLOW);
    for (int i = ph; i < 96; i += 3) {
      float a = CW_TAU * (float)i / 96.0f;
      pcol c = bulb_col(i / 3);
      for (int zz = -1; zz <= 1; zz += 2) {
        pm_push(g);
        pm_move(g, cosf(a) * (R + 1.1f), sinf(a) * (R + 1.1f), zz * 3.4f);
        pm_ball(g, 0.42f, 0.42f, 0.42f, 0, 0, glow(c, 4.0f), glow(c, 5.0f));
        pm_pop(g);
      }
    }
    for (int i = ph; i < 20; i += 3) {
      float a = CW_TAU * (float)i / 20.0f;
      for (int r = 1; r <= 3; r++) {
        pm_push(g);
        pm_move(g, cosf(a) * R * (0.5f + 0.16f * (float)r), sinf(a) * R * (0.5f + 0.16f * (float)r), 3.9f);
        pm_ball(g, 0.34f, 0.34f, 0.34f, 0, 0, glow(bulb_col(r + i), 4.0f), glow(bulb_col(r + i), 5.0f));
        pm_pop(g);
      }
    }
    t3_object *go = obj_from_pm(g, GM[GM_PHASE_A + ph], -1, false);
    char nm[24];
    snprintf(nm, sizeof nm, "WheelBulbs%d", ph);
    cw_node gn = node_of(WHEEL.wheel, nm, go);
    (void)gn;
  }
  /* gondolas hang from the struts and stay upright: nodes of their own, shared geometry in four colours */
  static const uint32_t gcol[4] = { 0xc2453b, 0x3a9a93, 0xe2b13e, 0xd9809b };
  for (int v = 0; v < 4; v++) {
    pm *g = pm_new(99 + (uint32_t)v);
    pm_layer(g, PL_METAL);
    pm_beam(g, V3(0, 0, 0), V3(0, -2.4f, 0), 0.3f, hex(0x6a6258));
    pm_layer(g, PL_MAIN);
    pm_push(g);
    pm_move(g, 0, -4.6f, 0);
    pm_box(g, 3.0f, 2.3f, 4.8f, pc_mix(hex(gcol[v]), BLACK, 0.35f), hex(gcol[v]));
    pm_pop(g);
    pm_push(g);
    pm_move(g, 0, -2.3f, 0);
    pm_prism(g, 3.4f, 1.0f, 5.2f, CREAM, CREAM);
    pm_pop(g);
    pm_layer(g, PL_GLOW);
    for (int sd = -1; sd <= 1; sd += 2)
      for (int i = -1; i <= 1; i += 2) {
        pm_push(g);
        pm_move(g, sd * 1.52f, -3.7f, i * 1.25f);
        pm_box_c(g, 0.1f, 0.9f, 1.4f, glow(hex(0xffd890), 2.6f));
        pm_pop(g);
      }
    prop_model m = pm_finish(g);
    for (int k = v; k < 20; k += 4) {
      char nm[24];
      snprintf(nm, sizeof nm, "Gondola%d", k);
      t3_object *o = obj_from_model(&m, NULL, -1, true);
      WHEEL.gon[k] = node_of(root, nm, o);
    }
    prop_model_free(&m);
  }
}


/* ── set piece 4: the tower the helix winds up, with a carousel on top ─────────────────────────────── */

/* a lit rectangle on a round wall: centre c, outward direction (nx, nz), width w, from y0 up h */
static void wall_rect(gcells *g, t3_vec3 c, float nx, float nz, float w, float h, pcol col) {
  float tx = nz, tz = -nx;
  pm *b = gc_pm(g, c);
  pm_push(b);
  pm_move(b, c.x, c.y, c.z);
  t3_vec3 p0 = V3(-tx * w * 0.5f, 0, -tz * w * 0.5f), p1 = V3(tx * w * 0.5f, 0, tz * w * 0.5f);
  pm_quad(b, p0, p1, V3(p1.x, h, p1.z), V3(p0.x, h, p0.z), col);
  pm_pop(b);
}

typedef struct tiers {
  float r, h;
  uint32_t a, b;
} tiers;

static void build_carousel(cw_node root, t3_vec3 at, float R) {
  pm *m = pm_new(501);
  pm_layer(m, PL_MAIN);
  pm_cyl_ex(m, R, R, 0.9f, 36, hex(0x5a2c20), hex(0x8a4a30), true, true);
  /* the painted floor: rings of colour */
  for (int ring = 0; ring < 6; ring++) {
    float r0 = R - 3.4f * (float)(ring + 1), r1 = R - 3.4f * (float)ring;
    if (r0 < 3.4f) r0 = 3.4f;
    pcol col = (ring & 1) ? CREAM : hex(0xa83a30);
    for (int i = 0; i < 36; i++) {
      float a0 = CW_TAU * (float)i / 36.0f, a1 = CW_TAU * (float)(i + 1) / 36.0f;
      pm_quad(m, V3(cosf(a0) * r1, 0.93f, -sinf(a0) * r1), V3(cosf(a1) * r1, 0.93f, -sinf(a1) * r1), V3(cosf(a1) * r0, 0.93f, -sinf(a1) * r0), V3(cosf(a0) * r0, 0.93f, -sinf(a0) * r0), col);
    }
  }
  /* the middle column */
  for (int i = 0; i < 16; i++) {
    float a0 = CW_TAU * (float)i / 16.0f, a1 = CW_TAU * (float)(i + 1) / 16.0f, r = 3.4f;
    pcol col = (i & 1) ? CREAM : hex(0xa83a30);
    pm_quad(m, V3(cosf(a0) * r, 0.9f, -sinf(a0) * r), V3(cosf(a1) * r, 0.9f, -sinf(a1) * r), V3(cosf(a1) * r, 11.8f, -sinf(a1) * r), V3(cosf(a0) * r, 11.8f, -sinf(a0) * r), col);
  }
  /* horses on brass poles, three rings */
  const float ringr[3] = { 8.2f, 13.6f, R - 2.2f };
  const int ringn[3] = { 8, 12, 16 };
  static const uint32_t hc[4] = { 0xeadfc4, 0x3a9a93, 0x9a5a3a, 0xe2b13e };
  for (int ring = 0; ring < 3; ring++)
    for (int i = 0; i < ringn[ring]; i++) {
      float a = CW_TAU * (float)i / (float)ringn[ring] + (float)ring * 0.4f;
      float x = cosf(a) * ringr[ring], z = -sinf(a) * ringr[ring];
      float lift = (i & 1) ? 3.9f : 2.9f;
      pm_layer(m, PL_METAL);
      pm_rod(m, V3(x, 0.9f, z), V3(x, 11.8f, z), 0.16f, 5, hex(0xd8b050));
      pm_layer(m, PL_MAIN);
      pcol body = hex(hc[(i + ring) & 3]);
      pm_push(m);
      pm_move(m, x, lift, z);
      pm_roty(m, a + CW_PI * 0.5f); /* local z along the way round */
      pm_box_c(m, 1.0f, 1.3f, 2.6f, body);
      pm_beam(m, V3(0, 0.3f, 1.1f), V3(0, 1.4f, 1.7f), 0.55f, body);
      pm_push(m);
      pm_move(m, 0, 1.7f, 2.1f);
      pm_box_c(m, 0.55f, 0.6f, 1.0f, pc_mix(body, CREAM, 0.2f));
      pm_pop(m);
      for (int lx = -1; lx <= 1; lx += 2)
        for (int lz = -1; lz <= 1; lz += 2) pm_beam(m, V3(lx * 0.3f, -0.4f, lz * 0.9f), V3(lx * 0.3f, -1.5f + (lz > 0 ? 0.5f : 0), lz * 1.0f), 0.2f, body);
      pm_beam(m, V3(0, 0.4f, -1.3f), V3(0, -0.8f, -1.9f), 0.25f, hex(0x2a1a14));
      pm_pop(m);
    }
  /* canopy: a striped cone with a scalloped skirt, and an underside */
  float y0 = 11.8f, y1 = 18.5f, y2 = 24.0f, rc = R + 2.0f;
  int n = 24;
  for (int i = 0; i < n; i++) {
    float a0 = CW_TAU * (float)i / (float)n, a1 = CW_TAU * (float)(i + 1) / (float)n, am = (a0 + a1) * 0.5f;
    pcol col = (i & 1) ? hex(0xc2453b) : CREAM;
    t3_vec3 e0 = V3(cosf(a0) * rc, y0, -sinf(a0) * rc), e1 = V3(cosf(a1) * rc, y0, -sinf(a1) * rc);
    t3_vec3 u0 = V3(cosf(a0) * 4.0f, y1, -sinf(a0) * 4.0f), u1 = V3(cosf(a1) * 4.0f, y1, -sinf(a1) * 4.0f);
    pm_quad(m, e0, e1, u1, u0, col);
    pm_tri(m, u0, u1, V3(0, y2, 0), col);
    pm_tri(m, V3(cosf(a0) * rc, y0, -sinf(a0) * rc), V3(cosf(am) * rc * 1.01f, y0 - 1.5f, -sinf(am) * rc * 1.01f), V3(cosf(a1) * rc, y0, -sinf(a1) * rc), (i & 1) ? CREAM : hex(0xc2453b));
    pm_tri(m, V3(0, y0 - 0.1f, 0), V3(cosf(a1) * rc, y0 - 0.1f, -sinf(a1) * rc), V3(cosf(a0) * rc, y0 - 0.1f, -sinf(a0) * rc), pc_mix(hex(0xe8d8b0), RUSTC, 0.4f));
  }
  pm_layer(m, PL_METAL);
  pm_rod(m, V3(0, y2, 0), V3(0, y2 + 7.0f, 0), 0.2f, 6, hex(0xd8b050));
  pm_layer(m, PL_MAIN);
  pm_push(m);
  pm_move(m, 0, y2 + 7.4f, 0);
  pm_ball(m, 0.9f, 0.9f, 0.9f, 1, 0, hex(0xd8b050), hex(0xf6e090));
  pm_pop(m);
  pm_tri(m, V3(0, y2 + 6.8f, 0), V3(4.2f, y2 + 5.8f, 0.4f), V3(0, y2 + 4.6f, 0), RED);
  pm_tri(m, V3(0, y2 + 4.6f, 0), V3(4.2f, y2 + 5.8f, 0.4f), V3(0, y2 + 6.8f, 0), RED);
  t3_object *o = obj_from_pm(m, NULL, -1, true);
  cw_node n0 = node_of(root, "Carousel", o);
  cw_node3d_set_position(n0, at);
  spin_add(n0, V3(0, 1, 0), 0.36f, 0);
  for (int ph = 0; ph < 3; ph++) {
    pm *g = pm_new(520 + (uint32_t)ph);
    pm_layer(g, PL_GLOW);
    for (int i = ph; i < 72; i += 3) {
      float a = CW_TAU * (float)i / 72.0f;
      pcol c = bulb_col(i / 3);
      pm_push(g);
      pm_move(g, cosf(a) * (rc + 0.3f), y0 - 0.6f, -sinf(a) * (rc + 0.3f));
      pm_ball(g, 0.4f, 0.4f, 0.4f, 0, 0, glow(c, 4.0f), glow(c, 5.0f));
      pm_pop(g);
    }
    for (int i = ph; i < 24; i += 3) {
      float a = CW_TAU * ((float)i + 0.5f) / 24.0f;
      pm_push(g);
      pm_move(g, cosf(a) * 4.4f, y1 + 0.2f, -sinf(a) * 4.4f);
      pm_ball(g, 0.35f, 0.35f, 0.35f, 0, 0, glow(hex(0xffe090), 4.0f), glow(hex(0xffe090), 5.0f));
      pm_pop(g);
    }
    for (int ring = 0; ring < 3; ring++)
      for (int i = ph; i < ringn[ring]; i += 3) {
        float a = CW_TAU * (float)i / (float)ringn[ring] + (float)ring * 0.4f;
        pm_push(g);
        pm_move(g, cosf(a) * ringr[ring], y0 - 0.6f, -sinf(a) * ringr[ring]);
        pm_ball(g, 0.32f, 0.32f, 0.32f, 0, 0, glow(hex(0xffe4a0), 3.6f), glow(hex(0xffe4a0), 4.4f));
        pm_pop(g);
      }
    char nm[24];
    snprintf(nm, sizeof nm, "CarouselBulbs%d", ph);
    node_of(n0, nm, obj_from_pm(g, GM[GM_PHASE_A + ph], -1, false));
  }
}

static void build_tower(cw_node root, pmc *st, gcells *chase[3], gcells *steady, t3_vec3 c) {
  static const tiers T[3] = { { 30.0f, 11.0f, 0xc2453b, 0xeadfc4 }, { 25.0f, 10.5f, 0x3a9a93, 0xeadfc4 }, { 20.0f, 6.0f, 0xe2b13e, 0xd9809b } };
  frame f = fr(c.x, c.y, c.z, 0);
  float y = -1.0f;
  /* the plinth */
  {
    pm *b = fb(st, &f, 0, -2.0f, 0, PL_MAIN);
    pm_cyl_ex(b, 35.0f, 33.0f, 3.2f, 40, hex(0x6a645a), hex(0x9a9484), true, true);
    pm_pop(b);
  }
  y = 1.2f;
  int bi = 0;
  for (int k = 0; k < 3; k++) {
    int n = k == 0 ? 28 : k == 1 ? 24 : 20;
    drum(st, &f, T[k].r, y, y + T[k].h, n, hex(T[k].a), hex(T[k].b), PL_MAIN);
    /* the cornice: a collar a metre proud of the wall, and a flat roof */
    drum(st, &f, T[k].r + 1.3f, y + T[k].h - 1.1f, y + T[k].h + 0.2f, n, hex(0xd8c8a0), hex(0xb8a478), PL_MAIN);
    disc(st, &f, k < 2 ? T[k + 1].r : 0.0f, T[k].r + 1.3f, y + T[k].h + 0.2f, n, hex(0x5a2c20), PL_MAIN);
    /* a row of lit windows on the wall */
    for (int i = 0; i < n; i++) {
      float a = CW_TAU * ((float)i + 0.5f) / (float)n;
      float nx = cosf(a), nz = -sinf(a);
      float rr2 = T[k].r + 0.15f;
      wall_rect(steady, V3(c.x + nx * rr2, c.y + y + 2.2f, c.z + nz * rr2), nx, nz, 2.2f, T[k].h - 5.0f, glow(hex((i & 1) ? 0xffc870 : 0xffb050), 1.5f));
    }
    /* bulbs along the cornice */
    int nb = (int)(CW_TAU * (T[k].r + 1.6f) / (THIN ? 5.0f : 3.0f));
    for (int i = 0; i < nb; i++, bi++) {
      float a = CW_TAU * (float)i / (float)nb;
      bulb(chase[bi % 3], V3(c.x + cosf(a) * (T[k].r + 1.7f), c.y + y + T[k].h + 0.5f, c.z - sinf(a) * (T[k].r + 1.7f)), 0.38f, bulb_col(i), 4.0f);
    }
    y += T[k].h + 0.2f;
  }
  build_carousel(root, V3(c.x, c.y + y, c.z), 18.0f);
}

/* ── bulb strings and bunting over the road ──────────────────────────────────────────────────────── */

static void bunting(pmc *wire, t3_vec3 a, t3_vec3 b, float sag, uint32_t seed) {
  static const uint32_t pal[6] = { 0xc2453b, 0xe2b13e, 0x3a9a93, 0xd9809b, 0xeadfc4, 0x4f86b8 };
  float len = hypotf(hypotf(b.x - a.x, b.z - a.z), b.y - a.y);
  int n = (int)(len / 2.4f);
  t3_vec3 d = V3(b.x - a.x, 0, b.z - a.z);
  float dl = hypotf(d.x, d.z);
  if (dl < 0.01f) return;
  d.x /= dl, d.z /= dl;
  for (int i = 0; i < n; i++) {
    float t = (i + 0.5f) / (float)n;
    t3_vec3 p = V3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t - sag * 4.0f * t * (1 - t), a.z + (b.z - a.z) * t);
    pm *bb = pmc_at(wire, p);
    pm_layer(bb, PL_MAIN);
    pcol col = hex(pal[(i + seed) % 6]);
    float w = 0.75f;
    t3_vec3 p0 = V3(p.x - d.x * w, p.y, p.z - d.z * w), p1 = V3(p.x + d.x * w, p.y, p.z + d.z * w), tip = V3(p.x, p.y - 1.5f, p.z);
    pm_tri(bb, p0, p1, tip, col);
    pm_tri(bb, p1, p0, tip, col);
  }
}

static void bulb_string(pmc *wire, gcells *chase[3], t3_vec3 a, t3_vec3 b, float sag, float spacing, uint32_t seed) {
  pm *w = pmc_at(wire, V3((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f));
  pm_layer(w, PL_MAIN);
  pm_cable(w, a, b, sag, 0.05f, hex(0x241c18));
  float len = hypotf(hypotf(b.x - a.x, b.z - a.z), b.y - a.y);
  if (THIN) spacing *= 1.9f;
  int n = (int)(len / spacing);
  for (int i = 0; i < n; i++) {
    float t = (i + 0.5f) / (float)n;
    t3_vec3 p = V3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t - sag * 4.0f * t * (1 - t) - 0.18f, a.z + (b.z - a.z) * t);
    bulb(chase[(i + seed) % 3], p, 0.24f, bulb_col((int)(i + seed)), 4.0f);
  }
}

/* strings across the road every so often, and between the posts they hang from. Only where the road is level and open. */
static void build_strings(cw_node root, const track *t) {
  pmc *wood = pmc_new(150, 31), *wire = pmc_new(150, 32);
  gcells *chase[3] = { gc_new(150, 41), gc_new(150, 42), gc_new(150, 43) };
  float next = 60.0f;
  int count = 0;
  t3_vec3 lastL = { 0 }, lastR = { 0 };
  bool have = false;
  for (float s = 40.0f; s < t->length - 30.0f; s += 6.0f) {
    tsample a = track_at(t, s);
    bool bad = (a.flags & (TF_LOOP | TF_GAP | TF_TUNNEL)) || (a.s > P.crest_s - 100.0f && a.s < P.crest_s + 70.0f) || fabsf(a.f.y) > 0.14f || fabsf(a.bank) > 0.17f || fabsf(a.u.y) < 0.97f;
    for (int k = 0; k < 3 && !bad; k++) {
      tsample q = track_at(t, s + (float)(k - 1) * 22.0f);
      if (q.flags & (TF_LOOP | TF_GAP)) bad = true;
    }
    if (s < next) continue;
    if (bad) {
      have = false;
      continue;
    }
    next = s + 41.0f;
    float lat = a.hw + 2.4f;
    t3_vec3 bl = track_point(t, s, -lat, -0.5f), br = track_point(t, s, lat, -0.5f);
    float hgt = 12.5f;
    t3_vec3 tl = V3(bl.x, bl.y + hgt, bl.z), tr = V3(br.x, br.y + hgt, br.z);
    for (int sd = 0; sd < 2; sd++) {
      t3_vec3 base = sd ? br : bl, top = sd ? tr : tl;
      pm *p = pmc_at(wood, base);
      pm_layer(p, PL_MAIN);
      pm_push(p);
      pm_move(p, base.x, base.y - 1.0f, base.z);
      pm_cyl(p, 0.38f, 0.26f, hgt + 1.0f, 7, hex(0x4a3224), hex(0x7a5a40));
      pm_pop(p);
      pm_layer(p, PL_METAL);
      pm_push(p);
      pm_move(p, top.x, top.y, top.z);
      pm_ball(p, 0.4f, 0.4f, 0.4f, 0, 0, hex(0xd8b050), hex(0xf0d080));
      pm_pop(p);
    }
    bulb_string(wire, chase, tl, tr, 1.6f, 1.7f, (uint32_t)count);
    bunting(wire, tl, tr, 1.6f, (uint32_t)count);
    if (have) {
      bulb_string(wire, chase, lastL, tl, 1.8f, 1.9f, (uint32_t)count + 1);
      bunting(wire, lastL, tl, 1.8f, (uint32_t)count);
      bulb_string(wire, chase, lastR, tr, 1.8f, 1.9f, (uint32_t)count + 2);
      bunting(wire, lastR, tr, 1.8f, (uint32_t)count + 3);
    }
    lastL = tl, lastR = tr, have = true;
    count++;
  }
  pmc_finish(wood, root, "StringPosts", true, TEX_WALL_WOOD);
  pmc_finish(wire, root, "Strings", false, -1);
  for (int k = 0; k < 3; k++) {
    char nm[24];
    snprintf(nm, sizeof nm, "StringBulbs%d", k);
    gc_finish(chase[k], root, nm, GM[GM_PHASE_A + k]);
  }
  cw_info("fair: %d string crossings", count);
}

/* ── the loops and the corkscrew: bulbs along the road, steel bracing, hoops ──────────────────────── */

typedef struct span {
  int i0, i1;
} span;

/* the sample range of the n-th loop, and of the corkscrew */
static span loop_span(const track *t, int n) {
  span sp = { -1, -1 };
  int k = -1;
  bool in = false;
  for (int i = 0; i < t->n; i++) {
    bool f = (t->s[i].flags & TF_LOOP) && SPEC[t->s[i].seg].kind == P_LOOP;
    if (f && !in) k++;
    in = f;
    if (f && k == n) {
      if (sp.i0 < 0) sp.i0 = i;
      sp.i1 = i;
    }
  }
  return sp;
}

static void dress_loop(pmc *st, gcells *chase[3], const track *t, int n) {
  span sp = loop_span(t, n);
  if (sp.i0 < 0) return;
  t3_vec3 c = n == 0 ? P.loop1_c : P.loop2_c;
  float r = n == 0 ? P.loop1_r : P.loop2_r;
  for (int i = sp.i0; i <= sp.i1; i += 3) {
    const tsample *a = &t->s[i];
    for (int sd = -1; sd <= 1; sd += 2) {
      t3_vec3 p = V3(a->p.x + a->r.x * sd * (a->hw + 1.5f) - a->u.x * 0.1f, a->p.y + a->r.y * sd * (a->hw + 1.5f) - a->u.y * 0.1f, a->p.z + a->r.z * sd * (a->hw + 1.5f) - a->u.z * 0.1f);
      bulb(chase[(i / 3 + (sd > 0)) % 3], p, 0.34f, bulb_col(i / 3), 4.2f);
    }
  }
  /* painted ribs round the belly of the loop, like the bands on a coaster car */
  for (int i = sp.i0 + 2; i <= sp.i1; i += 5) {
    const tsample *a = &t->s[i];
    t3_vec3 p = V3(a->p.x - a->u.x * 0.95f, a->p.y - a->u.y * 0.95f, a->p.z - a->u.z * 0.95f);
    pm *b = pmc_at(st, p);
    pm_layer(b, PL_MAIN);
    pm_push(b);
    pm_basis(b, p, a->r, a->u, a->f);
    pm_box_c(b, 2.0f * (a->hw + 0.9f), 0.34f, 1.5f, ((i / 5) & 1) ? hex(n == 0 ? 0xc2453b : 0x3a9a93) : hex(0xeadfc4));
    pm_pop(b);
  }
  /* four struts from the shoulders of the circle down to the ground, as a roller coaster loop is held */
  pcol steel = n == 0 ? hex(0xc2453b) : hex(0x3a9a93), band = CREAM;
  for (int sd = -1; sd <= 1; sd += 2)
    for (int hh = 0; hh < 2; hh++) {
      float want = c.y + r * (hh ? 0.15f : 0.72f);
      int best = -1;
      float bd = 1e9f;
      for (int i = sp.i0; i <= sp.i1; i++) {
        const tsample *a = &t->s[i];
        if ((a->p.z - c.z) * sd < 0) continue;
        float d = fabsf(a->p.y - want);
        if (d < bd) bd = d, best = i;
      }
      if (best < 0) continue;
      const tsample *a = &t->s[best];
      t3_vec3 top = V3(a->p.x - a->u.x * 1.1f, a->p.y - a->u.y * 1.1f, a->p.z - a->u.z * 1.1f);
      float gy = fair_ground(top.x, top.z + sd * (hh ? 14.0f : 26.0f), NULL);
      t3_vec3 foot = V3(top.x, gy - 0.5f, top.z + sd * (hh ? 14.0f : 26.0f));
      pm *b = pmc_at(st, V3((top.x + foot.x) * 0.5f, (top.y + foot.y) * 0.5f, (top.z + foot.z) * 0.5f));
      pm_layer(b, PL_METAL);
      pm_beam(b, foot, top, 1.0f, steel);
      /* a second beam beside it makes an A of the pair; cross ties between */
      t3_vec3 off = V3(3.2f, 0, 0);
      pm_beam(b, V3(foot.x + off.x, foot.y, foot.z), V3(top.x + off.x * 0.2f, top.y, top.z), 0.8f, steel);
      for (int k = 1; k < 5; k++) {
        float f = (float)k / 5.0f;
        t3_vec3 a0 = V3(foot.x + (top.x - foot.x) * f, foot.y + (top.y - foot.y) * f, foot.z + (top.z - foot.z) * f);
        pm_beam(b, a0, V3(a0.x + off.x * (1 - f * 0.8f), a0.y, a0.z), 0.5f, band);
      }
      pm_pop(b);
      b = pmc_at(st, foot);
      pm_layer(b, PL_MAIN);
      pm_push(b);
      pm_move(b, foot.x + 1.4f, foot.y, foot.z);
      pm_box_c(b, 7.5f, 1.8f, 4.5f, hex(0x7a756c));
      pm_pop(b);
    }
}

static void dress_corkscrew(pmc *st, gcells *chase[3], const track *t) {
  int k0 = -1, k1 = -1;
  for (int i = 0; i < t->n; i++)
    if (SPEC[t->s[i].seg].kind == P_CORK) {
      if (k0 < 0) k0 = i;
      k1 = i;
    }
  if (k0 < 0) return;
  int ring = 0;
  for (float s = t->s[k0].s + 6.0f; s < t->s[k1].s - 4.0f; s += 19.0f, ring++) {
    tsample a = track_at(t, s);
    float R = a.hw + 3.4f;
    pcol col = (ring % 3 == 0) ? hex(0xc2453b) : (ring % 3 == 1) ? hex(0xe9dcc0) : hex(0x3a9a93);
    pm *b = pmc_at(st, a.p);
    pm_layer(b, PL_METAL);
    pm_push(b);
    pm_basis(b, a.p, V3(1, 0, 0), V3(0, 1, 0), a.f);
    pm_ring(b, R, 0.5f, 28, 5, col);
    pm_pop(b);
    float gy = fair_ground(a.p.x, a.p.z, NULL);
    for (int sd = -1; sd <= 1; sd += 2) {
      t3_vec3 top = V3(a.p.x + sd * R * 0.7f, a.p.y - R * 0.7f, a.p.z);
      t3_vec3 foot = V3(a.p.x + sd * (R + 4.0f), gy - 0.4f, a.p.z);
      pm *lb = pmc_at(st, top);
      pm_layer(lb, PL_METAL);
      pm_beam(lb, foot, top, 0.7f, col);
    }
    for (int i = 0; i < 28; i++) {
      float an = CW_TAU * (float)i / 28.0f;
      t3_vec3 p = V3(a.p.x + cosf(an) * (R + 0.7f), a.p.y + sinf(an) * (R + 0.7f), a.p.z);
      bulb(chase[(i + ring) % 3], p, 0.3f, bulb_col(i + ring), 4.2f);
    }
  }
}


/* ── the park's buildings ────────────────────────────────────────────────────────────────────────── */

/* a ticket, popcorn or sweet booth facing +z in its frame: a counter box, a striped awning, a lit board */
static void build_booth(pmc *wood, gcells *steady, const frame *f, uint32_t seed) {
  static const uint32_t body[4] = { 0x3a9a93, 0xd9809b, 0xe2b13e, 0x4f86b8 };
  static const uint32_t awn[3] = { 0xc2453b, 0xeadfc4, 0x3a9a93 };
  pcol bc = hex(body[seed & 3]), a0 = hex(awn[0]), a1 = hex(awn[1 + (seed >> 2) % 2]);
  pm *b = fb(wood, f, 0, 0, 0, PL_MAIN);
  pm_vary(b, 0.06f);
  pm_box(b, 5.0f, 3.2f, 3.4f, pc_mix(bc, BLACK, 0.4f), bc);
  pm_push(b);
  pm_move(b, 0, 1.45f, 2.0f);
  pm_box(b, 5.4f, 0.22f, 1.0f, WOODC, pc_mix(WOODC, CREAM, 0.3f)); /* the counter */
  pm_pop(b);
  for (int sd = -1; sd <= 1; sd += 2) pm_beam(b, V3(sd * 2.5f, 0, 2.7f), V3(sd * 2.5f, 3.3f, 2.7f), 0.2f, WOODC);
  /* the awning: five stripes sloping down to the front, then a scalloped edge */
  for (int i = 0; i < 6; i++) {
    float x0 = -2.9f + 0.97f * (float)i, x1 = x0 + 0.97f;
    pcol c = (i & 1) ? a1 : a0;
    pm_quad(b, V3(x0, 3.5f, 1.0f), V3(x1, 3.5f, 1.0f), V3(x1, 2.75f, 3.2f), V3(x0, 2.75f, 3.2f), c);
    pm_quad(b, V3(x0, 2.75f, 3.2f), V3(x1, 2.75f, 3.2f), V3(x1, 3.5f, 1.0f), V3(x0, 3.5f, 1.0f), pc_mix(c, BLACK, 0.3f));
    pm_tri(b, V3(x0, 2.75f, 3.2f), V3(x1, 2.75f, 3.2f), V3((x0 + x1) * 0.5f, 2.3f, 3.25f), c);
  }
  pm_box_c(b, 5.0f, 0.18f, 0.18f, pc_mix(WOODC, BLACK, 0.2f));
  pm_pop(b);
  /* the board above the roof: a lit panel with bulbs round it */
  b = fb(wood, f, 0, 3.5f, 0.3f, PL_MAIN);
  pm_box(b, 4.4f, 1.7f, 0.25f, pc_mix(bc, BLACK, 0.5f), pc_mix(bc, CREAM, 0.3f));
  pm_pop(b);
  t3_vec3 w = fw(f, 0, 4.35f, 0.5f);
  (void)w;
  for (int i = 0; i < 6; i++) bulb(steady, fw(f, -2.0f + 0.8f * (float)i, 5.35f, 0.5f), 0.14f, bulb_col(i + (int)seed), 3.6f);
  for (int i = 0; i < 6; i++) bulb(steady, fw(f, -2.4f + 0.96f * (float)i, 2.72f, 3.3f), 0.13f, hex(0xffe9a0), 3.4f);
  pm *g = gc_pm(steady, fw(f, 0, 2.0f, 2.0f));
  pm_push(g);
  t3_vec3 q = fw(f, 0, 2.2f, 1.8f);
  pm_move(g, q.x, q.y, q.z);
  pm_roty(g, f->yaw);
  pm_box_c(g, 3.4f, 0.9f, 0.1f, glow(hex(0xffcf80), 2.0f));
  pm_pop(g);
}

/* a long bumper-car hall facing +z: a plain body, a saw-tooth roof, a tall false front with a lit board */
static void build_hall(pmc *wood, gcells *chase[3], gcells *steady, const frame *f, float W, float D, uint32_t seed) {
  pcol wall = hex(seed & 1 ? 0xd9809b : 0x4f86b8), trim = CREAM;
  float H = 9.0f, FH = 16.0f;
  pm *b = fb(wood, f, 0, 0, -D * 0.5f, PL_MAIN);
  pm_vary(b, 0.05f);
  pm_box(b, W, H, D, pc_mix(wall, BLACK, 0.45f), wall);
  pm_pop(b);
  /* saw-tooth roof */
  int teeth = (int)(D / 7.0f);
  for (int i = 0; i < teeth; i++) {
    float z0 = -D + 7.0f * (float)i + 3.5f;
    b = fb(wood, f, 0, H, z0, PL_MAIN);
    pm_prism(b, W * 1.02f, 3.2f, 7.0f, hex(0x6a3a2c), hex(0x9a5a3a));
    pm_pop(b);
    t3_vec3 gp = fw(f, 0, H + 1.2f, z0 + 3.4f);
    (void)gp;
  }
  /* the false front and its board */
  b = fb(wood, f, 0, 0, 0.2f, PL_MAIN);
  pm_box(b, W + 2.0f, FH, 1.0f, pc_mix(trim, RUSTC, 0.4f), trim);
  pm_pop(b);
  b = fb(wood, f, 0, FH, 0.2f, PL_MAIN);
  pm_prism(b, W + 2.0f, 3.0f, 1.0f, hex(0x8a3a2c), hex(0xc2453b));
  pm_pop(b);
  /* arches: dark doorways with lit insides */
  int na = (int)(W / 9.0f);
  for (int i = 0; i < na; i++) {
    float x = -W * 0.5f + W * ((float)i + 0.5f) / (float)na;
    b = fb(wood, f, x, 0, 0.75f, PL_MAIN);
    pm_box(b, 4.6f, 6.0f, 0.2f, BLACK, hex(0x2a2024));
    pm_pop(b);
    wall_rect(steady, fw(f, x, 0.2f, 0.9f), f->sy * 0 + sinf(f->yaw), cosf(f->yaw), 3.6f, 4.6f, glow(hex(i & 1 ? 0xff9a50 : 0xffc870), 1.3f));
  }
  /* the board: a field of glow with a ring of bulbs and a chasing arrow */
  wall_rect(steady, fw(f, 0, 9.0f, 0.78f), sinf(f->yaw), cosf(f->yaw), W * 0.8f, 5.0f, glow(hex(0xffb458), 1.5f));
  int nb = (int)(W * 0.8f / 1.6f);
  for (int i = 0; i <= nb; i++) {
    float x = -W * 0.4f + W * 0.8f * (float)i / (float)nb;
    bulb(chase[i % 3], fw(f, x, 14.4f, 1.0f), 0.28f, bulb_col(i + (int)seed), 4.2f);
    bulb(chase[(i + 1) % 3], fw(f, x, 8.7f, 1.0f), 0.28f, bulb_col(i), 4.2f);
  }
  for (int sd = -1; sd <= 1; sd += 2)
    for (int k = 0; k < 4; k++) bulb(chase[k % 3], fw(f, sd * W * 0.4f, 9.2f + 1.7f * (float)k, 1.0f), 0.28f, bulb_col(k), 4.2f);
  /* a row of lit windows down both long sides, so the flank facing a road is not a blank wall */
  for (int sd = -1; sd <= 1; sd += 2)
    for (float z = -D + 4.0f; z < -2.0f; z += 6.0f) {
      t3_vec3 c = fw(f, sd * (W * 0.5f + 0.1f), 3.2f, z);
      wall_rect(steady, c, sd * f->cy, -sd * f->sy, 3.0f, 2.6f, glow(hex(0xffb458), 1.3f));
    }
  /* little cars lined up under the front: boxes in candy colours on the dark floor */
}

/* the haunted house: a gabled body, two towers, windows that flicker */
static void build_haunted(pmc *wood, gcells *flick[2], gcells *steady, const frame *f) {
  pcol wall = hex(0x5a4468), wall2 = hex(0x7a6088), roof = hex(0x2a2230), trim = hex(0xd8c8a0);
  pm *b = fb(wood, f, 0, 0, -9, PL_MAIN);
  pm_vary(b, 0.08f);
  pm_box(b, 38, 15, 18, pc_mix(wall, BLACK, 0.5f), wall2);
  pm_pop(b);
  b = fb(wood, f, 0, 15, -9, PL_MAIN);
  pm_prism(b, 38.6f, 12.0f, 19.0f, roof, pc_mix(roof, wall, 0.3f));
  pm_pop(b);
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = sd * 15.5f;
    b = fb(wood, f, x, 0, -6, PL_MAIN);
    pm_box(b, 9, sd > 0 ? 28.0f : 22.0f, 9, pc_mix(wall, BLACK, 0.5f), wall2);
    pm_pop(b);
    b = fb(wood, f, x, sd > 0 ? 28.0f : 22.0f, -6, PL_MAIN);
    pm_cyl(b, 7.0f, 0.0f, sd > 0 ? 15.0f : 11.0f, 8, roof, pc_mix(roof, wall, 0.4f));
    pm_pop(b);
    b = fb(wood, f, x, (sd > 0 ? 28.0f : 22.0f) + (sd > 0 ? 15.0f : 11.0f), -6, PL_METAL);
    pm_rod(b, V3(0, 0, 0), V3(0, 4.5f, 0), 0.12f, 5, hex(0x1c1a1c));
    pm_pop(b);
  }
  /* porch and door */
  b = fb(wood, f, 0, 0, 0.8f, PL_MAIN);
  pm_box(b, 8.4f, 0.6f, 4.0f, hex(0x4a3a30), hex(0x6a5646));
  pm_pop(b);
  b = fb(wood, f, 0, 0.6f, 0.2f, PL_MAIN);
  pm_box(b, 4.2f, 7.0f, 0.4f, BLACK, hex(0x2a1c20));
  pm_pop(b);
  b = fb(wood, f, 0, 7.6f, 0.2f, PL_MAIN);
  pm_prism(b, 5.4f, 1.8f, 0.5f, trim, trim);
  pm_pop(b);
  /* windows: green and amber, two sets that flicker apart */
  for (int row = 0; row < 2; row++)
    for (int i = 0; i < 4; i++) {
      float x = -13.5f + 9.0f * (float)i;
      if (fabsf(x) < 3.0f && row == 0) continue;
      pcol c = (i + row) & 1 ? hex(0x9aff70) : hex(0xffb050);
      wall_rect(flick[(i + row) & 1], fw(f, x, 3.5f + 7.5f * (float)row, 0.15f), sinf(f->yaw), cosf(f->yaw), 2.4f, 4.0f, glow(c, 2.2f));
    }
  wall_rect(flick[0], fw(f, 15.5f, 14.0f, 3.0f), sinf(f->yaw), cosf(f->yaw), 2.4f, 5.0f, glow(hex(0x9aff70), 2.4f));
  wall_rect(flick[1], fw(f, 15.5f, 21.5f, 3.0f), sinf(f->yaw), cosf(f->yaw), 2.0f, 3.5f, glow(hex(0xff7a30), 2.4f));
  wall_rect(flick[0], fw(f, -15.5f, 11.0f, 3.0f), sinf(f->yaw), cosf(f->yaw), 2.4f, 4.5f, glow(hex(0xffb050), 2.2f));
  /* a crooked fence of bulbs along the eaves */
  for (int i = 0; i < 14; i++) bulb(steady, fw(f, -17.0f + 2.6f * (float)i, 15.1f, 0.4f), 0.2f, hex(0xff9a40), 3.0f);
}

/* the observation needle: a slim shaft, a ring of cabins that turns */
static void build_needle(cw_node root, pmc *st, gcells *chase[3], t3_vec3 at, float H) {
  frame f = fr(at.x, at.y, at.z, 0);
  for (int k = 0; k < 6; k++) {
    float y0 = H * (float)k / 6.0f, y1 = H * (float)(k + 1) / 6.0f;
    float r0 = 4.2f - 1.6f * (float)k / 6.0f, r1 = 4.2f - 1.6f * (float)(k + 1) / 6.0f;
    pm *b = fb(st, &f, 0, y0, 0, PL_MAIN);
    pm_cyl_ex(b, r0, r1, y1 - y0, 10, (k & 1) ? hex(0xb8ae98) : hex(0x2a7a76), (k & 1) ? hex(0xeadfc4) : hex(0x3a9a93), false, false);
    pm_pop(b);
  }
  pm *b = fb(st, &f, 0, 0, 0, PL_MAIN);
  pm_cyl_ex(b, 9.0f, 4.4f, 6.0f, 10, hex(0x5a554c), hex(0x8a8474), true, true);
  pm_pop(b);
  /* the pod: a turning disc with a ring of windows and bulbs */
  pm *m = pm_new(601);
  pm_layer(m, PL_MAIN);
  pm_cyl_ex(m, 10.0f, 12.0f, 3.0f, 18, hex(0xb83a30), hex(0xe2705a), true, true);
  pm_push(m);
  pm_move(m, 0, 3.0f, 0);
  pm_cyl_ex(m, 12.0f, 11.0f, 4.4f, 18, hex(0xdad0b8), hex(0xf4ecd6), false, false);
  pm_pop(m);
  pm_push(m);
  pm_move(m, 0, 7.4f, 0);
  pm_cyl(m, 12.4f, 2.0f, 4.2f, 18, hex(0xc2453b), hex(0xeadfc4));
  pm_pop(m);
  pm_layer(m, PL_GLOW);
  for (int i = 0; i < 18; i++) {
    float a = CW_TAU * (float)i / 18.0f;
    pm_push(m);
    pm_move(m, cosf(a) * 11.6f, 5.2f, -sinf(a) * 11.6f);
    pm_roty(m, a + CW_PI * 0.5f);
    pm_box_c(m, 3.4f, 2.2f, 0.3f, glow(hex(0xffd890), 1.8f));
    pm_pop(m);
  }
  pm_layer(m, PL_METAL);
  pm_rod(m, V3(0, 11.6f, 0), V3(0, 22.0f, 0), 0.2f, 5, hex(0xb0a890));
  cw_node n = node_of(root, "NeedlePod", obj_from_pm(m, NULL, -1, true));
  cw_node3d_set_position(n, V3(at.x, at.y + H, at.z));
  spin_add(n, V3(0, 1, 0), 0.08f, 0);
  for (int ph = 0; ph < 3; ph++) {
    pm *g = pm_new(610 + (uint32_t)ph);
    pm_layer(g, PL_GLOW);
    for (int i = ph; i < 36; i += 3) {
      float a = CW_TAU * (float)i / 36.0f;
      pm_push(g);
      pm_move(g, cosf(a) * 12.6f, 3.0f, -sinf(a) * 12.6f);
      pm_ball(g, 0.5f, 0.5f, 0.5f, 0, 0, glow(bulb_col(i / 3), 4.4f), glow(bulb_col(i / 3), 5.2f));
      pm_pop(g);
    }
    pm_push(g);
    pm_move(g, 0, 22.0f, 0);
    pm_ball(g, 0.9f, 0.9f, 0.9f, 0, 0, glow(hex(0xff5040), 5.0f), glow(hex(0xff5040), 6.0f));
    pm_pop(g);
    char nm[24];
    snprintf(nm, sizeof nm, "NeedleBulbs%d", ph);
    node_of(n, nm, obj_from_pm(g, GM[GM_PHASE_A + ph], -1, false));
  }
  (void)chase;
}

/* a row of chevrons made of bulbs, pointing along the frame's +x or -x: the corner signs the park's way */
static void build_chevrons(pmc *wood, gcells *chase[3], const frame *f, float dir) {
  pm *b = fb(wood, f, 0, 0, 0, PL_MAIN);
  pm_cyl(b, 0.4f, 0.3f, 8.0f, 6, WOODC, pc_mix(WOODC, CREAM, 0.3f));
  pm_box(b, 0.5f, 0.5f, 0.5f, WOODC, WOODC);
  pm_push(b);
  pm_move(b, 0, 8.0f, -0.2f);
  pm_box(b, 11.8f, 7.8f, 0.2f, hex(0xeadfc4), hex(0xeadfc4));
  pm_move(b, 0, 0.5f, 0.12f);
  pm_box(b, 10.8f, 6.8f, 0.2f, pc_mix(PURPLE, BLACK, 0.45f), pc_mix(PURPLE, BLACK, 0.15f));
  pm_pop(b);
  pm_pop(b);
  for (int c = 0; c < 3; c++)
    for (int arm = -4; arm <= 4; arm++) {
      float x = dir * (-3.4f + 2.4f * (float)c + (float)(4 - abs(arm)) * 0.7f);
      float y = 11.9f + (float)arm * 0.7f;
      bulb(chase[c], fw(f, x, y, 0.2f), 0.32f, hex(0xffd070), 4.4f);
    }
}


/* big tops are old: rips in the canvas (dark gaps where the sky shows through) and flaps that hang from the eaves */
static void tent_tears(pmc *c, t3_vec3 p, float h, uint32_t seed) {
  float r = 0.9f * h, wall = 0.28f * h, apex = 0.48f * h;
  uint32_t k = seed * 2654435761u;
  for (int i = 0; i < 4; i++) {
    k = k * 1664525u + 1013904223u;
    float th = CW_TAU * (float)(k >> 8) / 16777216.0f;
    float ya = wall + (0.1f + 0.2f * (float)((k >> 3) & 7) / 7.0f) * h * 0.2f * 4.0f * 0.5f;
    float yb = fminf(ya + 0.55f * (apex - ya), apex - 0.05f * h);
    float ra = 1.05f * r * (apex - ya) / (apex - wall) + 0.12f, rb = 1.05f * r * (apex - yb) / (apex - wall) + 0.12f;
    float w = 0.16f + 0.05f * (float)(i & 1);
    pm *b = pmc_at(c, V3(p.x + cosf(th) * ra, p.y + ya, p.z - sinf(th) * ra));
    pm_layer(b, PL_MAIN);
    t3_vec3 A = V3(p.x + cosf(th - w) * ra, p.y + ya, p.z - sinf(th - w) * ra), B = V3(p.x + cosf(th + w) * ra, p.y + ya, p.z - sinf(th + w) * ra);
    t3_vec3 C = V3(p.x + cosf(th + w * 0.3f) * rb, p.y + yb, p.z - sinf(th + w * 0.3f) * rb), D = V3(p.x + cosf(th - w * 0.5f) * rb, p.y + yb, p.z - sinf(th - w * 0.5f) * rb);
    pcol dark = hex(0x1a1010);
    pm_quad(b, A, B, C, D, dark);
    pm_quad(b, D, C, B, A, dark);
    pm_pop(b);
  }
  /* a flap hanging off the eave */
  for (int i = 0; i < 3; i++) {
    k = k * 1664525u + 1013904223u;
    float th = CW_TAU * (float)(k >> 8) / 16777216.0f;
    float rr0 = r * 1.04f;
    t3_vec3 a0 = V3(p.x + cosf(th) * rr0, p.y + wall + 0.3f, p.z - sinf(th) * rr0), a1 = V3(p.x + cosf(th + 0.2f) * rr0, p.y + wall + 0.3f, p.z - sinf(th + 0.2f) * rr0);
    t3_vec3 a2 = V3(p.x + cosf(th + 0.1f) * (rr0 + 0.6f), p.y + wall - 3.0f - 0.8f * (float)i, p.z - sinf(th + 0.1f) * (rr0 + 0.6f));
    pm *b = pmc_at(c, a0);
    pm_layer(b, PL_MAIN);
    pcol col = (i & 1) ? hex(0xb83a30) : hex(0xd8cfb4);
    pm_tri(b, a0, a1, a2, col);
    pm_tri(b, a1, a0, a2, pc_mix(col, BLACK, 0.4f));
    pm_pop(b);
  }
}


/* ── scatter in a few big batches: dead trees, shrubs, boulders, hay, so the lawn is not a clean sheet ─── */

typedef struct sbatch {
  prop_model m;
  prop_batch *b[PL_COUNT];
  prop_kind k;
} sbatch;

static void sb_init(sbatch *s, prop_kind k, int variant, int lod) {
  memset(s, 0, sizeof *s);
  s->k = k;
  s->m = prop_make(k, 0, 5, variant, lod);
  for (int l = 0; l < PL_COUNT; l++) {
    if (!s->m.g[l]) continue;
    t3_material *mat = prop_material(l, l == PL_MAIN ? prop_tex(k) : -1, (pcol){ 1, 1, 1 }, 1);
    s->b[l] = pb_begin(s->m.g[l], mat);
    pb_chunk(s->b[l], 300.0f);
  }
}
static void sb_add(sbatch *s, t3_vec3 p, float yaw, float scale, pcol tint) {
  for (int l = 0; l < PL_COUNT; l++)
    if (s->b[l]) pb_add_c(s->b[l], p, t3_quat_from_axis_angle(V3(0, 1, 0), yaw), V3(scale, scale, scale), l == PL_GLOW ? (pcol){ 1, 1, 1 } : tint);
}
static void sb_finish(sbatch *s, cw_node parent, const char *name) {
  for (int l = 0; l < PL_COUNT; l++) {
    if (!s->b[l] || pb_count(s->b[l]) == 0) continue;
    char nm[48];
    snprintf(nm, sizeof nm, "%s.%d", name, l);
    pb_finish(s->b[l], parent, nm, false);
  }
  prop_model_free(&s->m);
}

static void build_scatter(cw_node root, const track *t) {
  cw_node group = cw_node_new("Node3D", "Scatter");
  cw_node_add_child(root, group);
  sbatch bush[2], tree[2], rock[2], hay;
  for (int lod = 0; lod < 2; lod++) {
    sb_init(&bush[lod], PK_BUSH, 0, lod);
    sb_init(&tree[lod], PK_DEAD_TREE, 0, lod);
    sb_init(&rock[lod], PK_ROCK, 0, lod);
  }
  sb_init(&hay, PK_HAY, 0, 0);
  RS = 9091;
  int n = 0;
  for (float s = 0; s < t->length; s += THIN ? 40.0f : 17.0f)
    for (int side = -1; side <= 1; side += 2) {
      tsample a = track_at(t, s + rr(0, 14));
      float lat = a.hw + 13.0f + powf(rf(), 1.6f) * 95.0f;
      t3_vec3 p = track_point(t, a.s, side * lat, 0);
      if (!room(t, p.x, p.z, 4.0f)) continue;
      p.y = fair_ground(p.x, p.z, NULL) - 0.1f;
      int lod = lat > 55.0f ? 1 : 0;
      float pick = rf();
      float yaw = rr(0, CW_TAU);
      pcol tint = pc_mul((pcol){ 1, 1, 1 }, rr(0.85f, 1.15f));
      if (pick < 0.42f) sb_add(&bush[lod], p, yaw, rr(0.8f, 1.5f), tint);
      else if (pick < 0.7f) sb_add(&tree[lod], p, yaw, rr(0.9f, 1.5f), tint);
      else if (pick < 0.88f) sb_add(&rock[lod], p, yaw, rr(0.7f, 1.8f), tint);
      else if (lat < 40.0f) sb_add(&hay, p, yaw, rr(0.8f, 1.2f), tint);
      n++;
    }
  for (int lod = 0; lod < 2; lod++) {
    sb_finish(&bush[lod], group, lod ? "BushFar" : "Bush");
    sb_finish(&tree[lod], group, lod ? "TreeFar" : "Tree");
    sb_finish(&rock[lod], group, lod ? "RockFar" : "Rock");
  }
  sb_finish(&hay, group, "Hay");
  cw_info("fair: %d scatter props", n);
}


/* ── placing the park ────────────────────────────────────────────────────────────────────────────── */

static float yaw_to(t3_vec3 from, t3_vec3 to) { return atan2f(to.x - from.x, to.z - from.z); }

/* is there room for something of radius r at (x, z): off every part of the course, over or under? */
static bool room(const track *t, float x, float z, float r) {
  float ry;
  return road_gap(t, x, z, &ry) > r + 4.0f;
}

/* a balloon bunch tied to a point: a handful of bright lumps on strings */
static void balloons(pmc *c, t3_vec3 at, int n, uint32_t seed) {
  static const uint32_t pal[6] = { 0xc2453b, 0xe2b13e, 0x3a9a93, 0xd9809b, 0x4f86b8, 0xeadfc4 };
  pm *b = pmc_at(c, V3(at.x, at.y + 3.0f, at.z));
  pm_layer(b, PL_MAIN);
  for (int i = 0; i < n; i++) {
    float a = CW_TAU * (float)(i + seed % 7) / (float)n, rad = 0.5f + 0.4f * (float)(i % 3);
    t3_vec3 p = V3(at.x + cosf(a) * rad, at.y + 2.6f + 0.7f * (float)(i % 3) + (float)((seed >> 3) % 3) * 0.3f, at.z + sinf(a) * rad);
    pcol col = hex(pal[(i + seed) % 6]);
    pm_rod(b, at, V3(p.x, p.y - 0.8f, p.z), 0.015f, 3, hex(0xd8d0c0));
    pm_push(b);
    pm_move(b, p.x, p.y, p.z);
    pm_ball(b, 0.62f, 0.78f, 0.62f, 1, 0, pc_mix(col, BLACK, 0.25f), pc_mix(col, CREAM, 0.15f));
    pm_pop(b);
  }
}

static void build_park(cw_node root, pmc *st, pmc *wood, gcells *chase[3], gcells *steady, gcells *flick[2], const track *t) {
  RS = 4242;
  prop_set *ps = ps_new(root, "ParkProps");
  ps_style(ps, PK_BIGTOP, -1, (pcol){ 0.9f, 0.82f, 0.74f }, 1.0f);
  ps_style(ps, PK_TENT, -1, (pcol){ 0.9f, 0.84f, 0.78f }, 1.0f);
  uint32_t sd = 1;

  /* booths along the start straight and round the wheel */
  static const float booth_s[] = { 64, 92, 120, 250, 270, 20 };
  for (int i = 0; i < 6; i++)
    for (int side = -1; side <= 1; side += 2) {
      float s = booth_s[i] + (side > 0 ? 9.0f : 0.0f);
      tsample a = track_at(t, s);
      float lat = side * (a.hw + 11.0f + (float)(i & 1) * 3.5f);
      t3_vec3 p = track_point(t, s, lat, 0);
      if (!room(t, p.x, p.z, 4.0f)) continue;
      float gy = fair_ground(p.x, p.z, NULL);
      frame f = fr(p.x, gy, p.z, yaw_to(p, track_point(t, s, 0, 0)));
      build_booth(wood, steady, &f, sd++);
      balloons(st, V3(p.x + 2.2f * (float)side, gy + 3.5f, p.z), 5, sd);
    }

  /* bumper-car halls flanking the start straight, a big one at the heart of the park */
  struct { float x, z, W, D; float face; uint32_t seed; } halls[] = {
    { -62, 78, 40, 26, CW_PI * 0.5f, 0 }, { 66, 88, 36, 24, -CW_PI * 0.5f, 1 }, { 112, -150, 56, 26, CW_PI * 0.5f, 1 }, { 120, -50, 44, 24, CW_PI * 0.5f, 0 },
  };
  for (int i = 0; i < 4; i++) {
    if (!room(t, halls[i].x, halls[i].z, hypotf(halls[i].W, halls[i].D) * 0.5f)) continue;
    float gy = fair_ground(halls[i].x, halls[i].z, NULL);
    frame f = fr(halls[i].x, gy, halls[i].z, halls[i].face);
    build_hall(wood, chase, steady, &f, halls[i].W, halls[i].D, halls[i].seed);
  }

  /* the big tops: the pair at the jump, and the rest across the park */
  struct { float x, z, sc; int v; } tops[] = {
    { 164, -120, 1.1f, 0 }, { 252, -120, 1.1f, 2 }, { 70, -215, 1.0f, 1 }, { 130, 10, 0.95f, 2 }, { 36, 560, 1.0f, 0 }, { -5, 650, 0.9f, 1 }, { 96, 650, 0.9f, 2 },
    { 60, -300, 1.1f, 1 }, { 120, -330, 0.9f, 0 },
  };
  for (int i = 0; i < 9; i++) {
    float r = 0.9f * 24.0f * tops[i].sc;
    if (!room(t, tops[i].x, tops[i].z, r)) {
      cw_info("fair: big top %d has no room", i);
      continue;
    }
    float gy = fair_ground(tops[i].x, tops[i].z, NULL);
    t3_vec3 p = V3(tops[i].x, gy, tops[i].z);
    float s0 = t->s[road_nearest(t, p.x, p.z, NULL)].s;
    t3_vec3 rp = track_point(t, s0, 0, 0);
    tent_tears(st, p, 24.0f * tops[i].sc, 70u + (uint32_t)i);
    ps_add_ex(ps, PK_BIGTOP, tops[i].v, 0, p, t3_quat_from_axis_angle(V3(0, 1, 0), yaw_to(p, rp)), V3(tops[i].sc, tops[i].sc, tops[i].sc), (pcol){ 0.9f, 0.84f, 0.78f }, 900u + (uint32_t)i);
  }

  /* the haunted house and the needle */
  {
    t3_vec3 hp = V3(41, 0, 735);
    hp.y = fair_ground(hp.x, hp.z, NULL);
    frame f = fr(hp.x, hp.y, hp.z, yaw_to(hp, V3(-66, 0, 560)));
    build_haunted(wood, flick, steady, &f);
    t3_vec3 np = V3(97, 0, -395);
    np.y = fair_ground(np.x, np.z, NULL);
    build_needle(root, st, chase, np, 92.0f);
  }

  /* chevron boards on the outside of the sharp corners, pointing the way round */
  {
    int n = 0;
    float last = -1000;
    for (float s = 30; s < t->length - 30 && n < 8; s += 5.0f) {
      float k = track_curvature(t, s + 30.0f, 40.0f);
      tsample a = track_at(t, s);
      if (fabsf(k) > 0.0075f && s - last > 200.0f && !(a.flags & (TF_LOOP | TF_GAP))) {
        last = s;
        float lat = k > 0 ? a.hw + 10.0f : -(a.hw + 10.0f); /* outside of the turn: + curvature turns left, so the outside is the right */
        t3_vec3 p = track_point(t, s + 12.0f, lat, 0);
        tsample at = track_at(t, s + 12.0f);
        /* stand on the ground under the road's edge (the road may be high) */
        p.y = fair_ground(p.x, p.z, NULL);
        if (!room(t, p.x, p.z, 8.0f) && at.p.y - p.y > 4.0f) continue;
        t3_vec3 tow = track_point(t, s + 12.0f, 0, 0);
        frame f = fr(p.x, p.y, p.z, yaw_to(p, V3(tow.x - at.f.x * 40, 0, tow.z - at.f.z * 40)));
        /* local +x is the viewer's right; the arrows point the way the road bends */
        build_chevrons(wood, chase, &f, k > 0 ? -1.0f : 1.0f);
        n++;
      }
    }
    cw_info("fair: %d chevron boards", n);
  }

  ps_finish(ps);
}


/* ── life: rising balloons, fireworks, crows, a few real lights ───────────────────────────────────── */

static cw_node pnode(cw_node parent, const char *name, const char *const *kv, int seed) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_add_child(parent, n);
  for (int i = 0; kv[i]; i += 2) {
    cw_value v;
    const cw_prop *p = cw_node_find_prop(n, kv[i], NULL);
    if (p && p->type == CW_T_ENUM) {
      cw_node_set(n, kv[i], cw_val_enum(kv[i + 1]));
      continue;
    }
    if (!p || !cw_value_parse(kv[i + 1], p->type, &v)) {
      cw_warn("fair: bad particle setting %s = %s", kv[i], kv[i + 1]);
      continue;
    }
    cw_node_set(n, kv[i], v);
  }
  cw_node_set(n, "seed", cw_val_int(seed));
  return n;
}

typedef struct firework {
  cw_node n;
  float next, period;
} firework;
static firework FW[8];
static int nfw;

typedef struct crow {
  cw_node n;
  t3_vec3 c;
  float r, h, speed, phase, flap;
} crow;
static crow CR[28];
static int ncr;

static void build_life(cw_node root, const track *t) {
  (void)t;
  /* balloons that got away: five colours drifting up over the park */
  static const char *const cols[5] = { "0.86 0.2 0.16", "0.95 0.72 0.15", "0.15 0.62 0.58", "0.92 0.42 0.58", "0.25 0.5 0.85" };
  for (int i = 0; i < 5; i++) {
    char ramp[160], ext[48], nm[24];
    snprintf(ramp, sizeof ramp, "\"0 %s 0, 0.06 %s 1, 0.85 %s 1, 1 %s 0\"", cols[i], cols[i], cols[i], cols[i]);
    snprintf(nm, sizeof nm, "Balloons%d", i);
    snprintf(ext, sizeof ext, "vec3(%d, 0.5, %d)", 150, 330);
    const char *kv[] = { "amount", THIN ? "9" : "26", "lifetime", "34", "lifetime_random", "0.2", "emission_shape", "BOX", "emission_extents", ext, "direction", "vec3(0, 1, 0)",
      "spread", "6", "speed_min", "2.2", "speed_max", "3.4", "gravity", "vec3(0.35, 0.02, 0.1)", "size", "1.5", "size_random", "0.25", "color_ramp", ramp,
      "energy", "1.1", "blend", "ALPHA", "local_coords", "false", "preprocess", "30", NULL };
    cw_node n = pnode(root, nm, kv, 400 + i);
    cw_node3d_set_position(n, V3(95, 2, -60 + 40.0f * (float)i));
    cw_node_set(n, "emitting", cw_val_bool(true));
  }
  /* fireworks: a burst now and then, each in its own colour */
  static const float fp[7][3] = { { 60, 130, -150 }, { 160, 150, -40 }, { 100, 120, 110 }, { 208, 85, -120 }, { -30, 140, 300 }, { 110, 160, 420 }, { 60, 110, -380 } };
  static const char *const fc[7] = { "1 0.35 0.2", "1 0.85 0.3", "0.4 0.9 1", "1 0.3 0.7", "0.5 1 0.4", "1 0.6 0.2", "0.7 0.5 1" };
  for (int i = 0; i < 7; i++) {
    char ramp[200], nm[24];
    snprintf(ramp, sizeof ramp, "\"0 1 1 0.9 1, 0.15 %s 1, 0.7 %s 0.8, 1 %s 0\"", fc[i], fc[i], fc[i]);
    snprintf(nm, sizeof nm, "Firework%d", i);
    const char *kv[] = { "amount", "260", "lifetime", "3.0", "emission_shape", "POINT", "explosiveness", "1", "one_shot", "true", "direction", "vec3(0, 1, 0)", "spread", "180",
      "speed_min", "20", "speed_max", "38", "gravity", "vec3(0, -7, 0)", "damping", "1.5", "size", "1.8", "size_random", "0.3", "color_ramp", ramp, "energy", "14", "blend", "ADD",
      "trail_length", "14", "trail_width", "0.6", "local_coords", "false", NULL };
    cw_node n = pnode(root, nm, kv, 500 + i);
    cw_node3d_set_position(n, V3(fp[i][0], fp[i][1], fp[i][2]));
    cw_node_set(n, "emitting", cw_val_bool(false));
    FW[nfw++] = (firework){ n, 2.0f + 1.3f * (float)i, 6.0f + 0.9f * (float)i };
  }
  /* crows round the wheel and the tower: a bird is a body and two bent wings, flapping by squashing */
  pm *b = pm_new(700);
  pm_layer(b, PL_MAIN);
  pcol bk = hex(0x1c1a1e);
  pm_tri(b, V3(-0.25f, 0, -0.5f), V3(0.25f, 0, -0.5f), V3(0, 0, 0.7f), bk);
  pm_tri(b, V3(0.25f, 0, -0.5f), V3(-0.25f, 0, -0.5f), V3(0, 0, 0.7f), bk);
  for (int sd = -1; sd <= 1; sd += 2) {
    pm_tri(b, V3(0, 0, 0.35f), V3(0, 0, -0.35f), V3(sd * 1.5f, 0.45f, -0.15f), bk);
    pm_tri(b, V3(0, 0, -0.35f), V3(0, 0, 0.35f), V3(sd * 1.5f, 0.45f, -0.15f), bk);
  }
  prop_model bird = pm_finish(b);
  static const float cc[3][4] = { { -20, 92, 212, 52 }, { 131, 52, 242, 62 }, { 60, 60, -150, 80 } };
  for (int k = 0; k < 3; k++)
    for (int i = 0; i < (THIN ? 3 : 9) && ncr < 28; i++) {
      t3_object *o = obj_from_model(&bird, NULL, -1, false);
      char nm[24];
      snprintf(nm, sizeof nm, "Crow%d", ncr);
      cw_node cn = node_of(root, nm, o);
      float c0 = rf(), c1 = rf(), c2 = rf(), c3 = rf(), c4 = rf();
      CR[ncr] = (crow){ cn, V3(cc[k][0], cc[k][1], cc[k][2]), cc[k][3] * (0.7f + 0.5f * c0), (float)(c1 * 14.0f - 7.0f), (0.12f + 0.06f * c2) * (k == 1 ? -1.0f : 1.0f),
                        CW_TAU * c3, 6.0f + 3.0f * c4 };
      ncr++;
    }
  prop_model_free(&bird);
  /* three real lights, all the rest of the glow is emissive: the clown's throat, the carousel, the haunted porch (it flickers) */
  struct { const char *name; t3_vec3 at; float r, g, b, energy, range; cw_node *keep; } lights[3] = {
    { "ThroatLight", V3(0, 6.0f, 44), 1.0f, 0.62f, 0.3f, 900, 40, NULL },
    { "CarouselLight", V3(P.helix_c.x, P.helix_c.y + 36.0f, P.helix_c.z), 1.0f, 0.7f, 0.4f, 2600, 80, &LIGHT_CAROUSEL },
    { "HauntedLight", V3(41 - 22.0f, 6.0f, 735 - 30.0f), 0.45f, 1.0f, 0.5f, 700, 36, &LIGHT_HAUNTED },
  };
  for (int i = 0; i < 3; i++) {
    cw_node l = cw_node_new("OmniLight3D", lights[i].name);
    cw_node_add_child(root, l);
    cw_node_set(l, "color", cw_val_color(lights[i].r, lights[i].g, lights[i].b, 1));
    cw_node_set(l, "energy", cw_val_float(lights[i].energy));
    cw_node_set(l, "range", cw_val_float(lights[i].range));
    cw_node3d_set_position(l, lights[i].at);
    if (lights[i].keep) *lights[i].keep = l;
  }
}

static void life_frame(float t) {
  for (int i = 0; i < nfw; i++)
    if (t >= FW[i].next) {
      cw_particles_restart(FW[i].n);
      FW[i].next = t + FW[i].period * (0.8f + 0.5f * (float)((int)(t * 7.0f + (float)i) % 5) / 5.0f);
    }
  for (int i = 0; i < ncr; i++) {
    crow *c = &CR[i];
    float a = c->phase + c->speed * t;
    t3_vec3 p = V3(c->c.x + cosf(a) * c->r, c->c.y + c->h + 2.0f * sinf(t * 0.5f + c->phase), c->c.z + sinf(a) * c->r);
    cw_node3d_set_position(c->n, p);
    float dir = c->speed > 0 ? 1.0f : -1.0f;
    float yaw = atan2f(-sinf(a) * dir, cosf(a) * dir);
    cw_node3d_set_quaternion(c->n, t3_quat_from_axis_angle(V3(0, 1, 0), yaw));
    float fl = 0.35f + 0.65f * fabsf(sinf(t * c->flap + c->phase * 3.0f));
    cw_node3d_set_scale(c->n, V3(1.3f, fl * 1.3f, 1.3f));
  }
}


/* ── the climb, the helix, the jump ──────────────────────────────────────────────────────────────── */

/* bulbs along both edges of the road from s0 to s1, a pulse running the way the road goes */
static void edge_bulbs(const track *t, gcells *chase[3], float s0, float s1, float step, float lat_extra, float up) {
  int k = 0;
  if (THIN) step *= 1.8f;
  for (float s = s0; s < s1; s += step, k++) {
    tsample a = track_at(t, s);
    if (a.flags & TF_GAP) continue;
    for (int sd = -1; sd <= 1; sd += 2) {
      t3_vec3 p = track_point(t, s, sd * (a.hw + lat_extra), up);
      bulb(chase[k % 3], p, 0.3f, bulb_col(k), 4.2f);
    }
  }
}

/* a candy arch over the road: two legs and a half hoop, striped, with bulbs */
static void arch(pmc *st, gcells *chase[3], const track *t, float s, float rise, uint32_t seed) {
  tsample a = track_at(t, s);
  float R = a.hw + 2.6f;
  t3_vec3 c = V3(a.p.x + a.u.x * 0.2f, a.p.y + a.u.y * 0.2f, a.p.z + a.u.z * 0.2f);
  pm *b = pmc_at(st, V3(c.x, c.y + R * 0.5f, c.z));
  pm_layer(b, PL_MAIN);
  int n = 14;
  t3_vec3 prev = V3(c.x + a.r.x * R, c.y + a.r.y * R, c.z + a.r.z * R);
  for (int i = 1; i <= n; i++) {
    float an = CW_PI * (float)i / (float)n;
    float ca = cosf(an), sa = sinf(an);
    t3_vec3 p = V3(c.x + a.r.x * R * ca + a.u.x * (R + rise) * sa, c.y + a.r.y * R * ca + a.u.y * (R + rise) * sa, c.z + a.r.z * R * ca + a.u.z * (R + rise) * sa);
    pm_rod(b, prev, p, 0.42f, 5, (i & 1) ? hex(0xc2453b) : hex(0xeadfc4));
    if (i < n)
      bulb(chase[(i + (int)seed) % 3], V3(p.x + a.f.x * 0.5f, p.y + a.f.y * 0.5f, p.z + a.f.z * 0.5f), 0.3f, bulb_col(i + (int)seed), 4.2f);
    prev = p;
  }
  pm_pop(b);
  (void)seed;
}

static void dress_climb(const track *t, pmc *st, gcells *chase[3]) {
  /* the chain: links down the middle of the lift hill */
  float s0 = 0, s1 = 0;
  for (int i = 0; i < t->n; i++) {
    const tsample *a = &t->s[i];
    if (a->s > P.helix_s0 - 600 && a->f.y > 0.35f && a->s < P.crest_s + 5) {
      if (s0 == 0) s0 = a->s;
      s1 = a->s;
    }
  }
  if (s1 <= s0) return;
  /* the chain: one node of links that the frame slides up the hill by two links, over and over (the hill is a straight) */
  {
    pm *cm = pm_new(640);
    pm_layer(cm, PL_METAL);
    int k = 0;
    for (float s = s0 - 6; s < s1 + 6; s += 0.9f, k++) {
      tsample a = track_at(t, s);
      t3_vec3 p = V3(a.p.x + a.u.x * 0.09f, a.p.y + a.u.y * 0.09f, a.p.z + a.u.z * 0.09f);
      pm_push(cm);
      pm_basis(cm, p, a.r, a.u, a.f);
      pm_box_c(cm, 0.42f, 0.1f, 0.3f, (k & 1) ? hex(0x5a4a38) : hex(0x9a8a6a));
      pm_pop(cm);
    }
    CHAIN = node_of(F.scene, "Chain", obj_from_pm(cm, NULL, -1, false));
    tsample m = track_at(t, (s0 + s1) * 0.5f);
    CHAIN_DIR = m.f;
  }
  edge_bulbs(t, chase, s0 - 20, s1 + 20, 2.4f, 0.55f, 1.95f);
  for (float s = s0 + 10; s < s1; s += 26.0f) arch(st, chase, t, s, 2.0f, (uint32_t)s);
  /* the summit arch is the biggest */
  arch(st, chase, t, P.crest_s - 8.0f, 5.0f, 7);
  cw_info("fair: lift hill s %.0f..%.0f", s0, s1);
}

static void dress_helix(const track *t, gcells *chase[3]) { edge_bulbs(t, chase, P.helix_s0, P.helix_s1, 3.2f, 0.55f, 1.95f); }

/* the jump's two tents: masts above their tops and a string of lights and bunting between them over the road */
static void dress_jump(const track *t, pmc *st, pmc *wire, gcells *chase[3]) {
  tsample a = track_at(t, (P.jump_s0 + P.jump_s1) * 0.5f);
  t3_vec3 m[2];
  for (int sd = 0; sd < 2; sd++) {
    float x = a.p.x + (sd ? 41.0f : -41.0f), z = a.p.z;
    float gy = fair_ground(x, z, NULL);
    m[sd] = V3(x, a.p.y + 17.0f, z);
    pm *b = pmc_at(st, V3(x, gy + 20, z));
    pm_layer(b, PL_METAL);
    pm_rod(b, V3(x, gy + 26, z), m[sd], 0.28f, 5, hex(0xd8c8a8));
    pm_pop(b);
    b = pmc_at(st, m[sd]);
    pm_layer(b, PL_MAIN);
    pm_push(b);
    pm_move(b, m[sd].x, m[sd].y, m[sd].z);
    pm_ball(b, 0.8f, 0.8f, 0.8f, 1, 0, hex(0xd8b050), hex(0xf6e090));
    pm_pop(b);
  }
  bulb_string(wire, chase, m[0], m[1], 5.5f, 2.0f, 3);
  bunting(wire, m[0], m[1], 5.5f, 5);
  bulb_string(wire, chase, V3(m[0].x, m[0].y - 6.0f, m[0].z), V3(m[1].x, m[1].y - 6.0f, m[1].z), 3.0f, 2.2f, 1);
}


/* low picket fences a few steps back from the road on the stretches that run at ground level: a rhythm that makes speed
 * readable, in the park's candy colours */
static void build_fences(cw_node root, const track *t) {
  pmc *c = pmc_new(160, 91);
  int n = 0;
  for (int side = -1; side <= 1; side += 2) {
    bool have = false;
    t3_vec3 prev = { 0 };
    for (float s = 0; s < t->length; s += THIN ? 8.0f : 4.0f) {
      tsample a = track_at(t, s);
      bool ok = a.p.y < 1.0f && !(a.flags & (TF_LOOP | TF_GAP)) && fabsf(a.f.y) < 0.05f;
      t3_vec3 p = track_point(t, s, side * (a.hw + 13.0f), 0);
      if (ok && !room(t, p.x, p.z, 2.0f)) ok = false;
      if (!ok) {
        have = false;
        continue;
      }
      p.y = fair_ground(p.x, p.z, NULL) - 0.1f;
      pm *b = pmc_at(c, p);
      pm_layer(b, PL_MAIN);
      pcol col = ((int)(s / 4.0f) & 1) ? hex(0xc2453b) : hex(0xeadfc4);
      pm_push(b);
      pm_move(b, p.x, p.y, p.z);
      pm_box(b, 0.24f, 1.25f, 0.24f, pc_mix(col, BLACK, 0.3f), col);
      pm_pop(b);
      if (have) {
        for (int r = 0; r < 2; r++) {
          float h = 0.45f + 0.5f * (float)r;
          pm_beam(b, V3(prev.x, prev.y + h, prev.z), V3(p.x, p.y + h, p.z), 0.1f, hex(0xd8cdb0));
        }
      }
      prev = p, have = true, n++;
    }
  }
  pmc_finish(c, root, "Fences", true, -1);
  cw_info("fair: %d fence posts", n);
}


/* ── the horizon and the paving ──────────────────────────────────────────────────────────────────── */

/* the far hills and the landmarks standing on them: a coaster's lift hill, water towers, a wheel, mesas, so every way
 * you look something worth seeing sits in the haze */
static void build_backdrop(cw_node root, const track *t) {
  prop_set *ps = ps_new(root, "Backdrop");
  ps_shadows(ps, false);
  ps_style(ps, PK_MESA, -1, (pcol){ 1.3f, 0.8f, 0.7f }, 1.0f);
  struct { prop_kind k; float ang, dist, size; int variant; } far_[] = {
    { PK_MESA, 10, 1150, 130, 0 }, { PK_MESA, 120, 1100, 110, 0 }, { PK_MESA, 215, 1200, 140, 0 }, { PK_MESA, 310, 1250, 150, 0 },
    { PK_COASTER_LIFT, 30, 620, 90, 0 }, { PK_COASTER_LIFT, 250, 640, 100, 0 }, { PK_WATER_TOWER, 195, 600, 70, 0 }, { PK_WATER_TOWER, 295, 540, 55, 0 },
    { PK_FERRIS_WHEEL, 105, 800, 110, 0 }, { PK_WINDMILL, 20, 520, 50, 0 },
  };
  const float cx = 90, cz = 100;
  for (int i = 0; i < (int)(sizeof far_ / sizeof far_[0]); i++) {
    float a = far_[i].ang * CW_PI / 180.0f;
    t3_vec3 p = V3(cx + sinf(a) * far_[i].dist, 0, cz + cosf(a) * far_[i].dist);
    p.y = fair_ground(p.x, p.z, NULL) - 1.0f;
    float sc = far_[i].size / prop_default_size(far_[i].k);
    ps_add_ex(ps, far_[i].k, far_[i].variant, 1, p, t3_quat_from_axis_angle(V3(0, 1, 0), yaw_to(p, V3(cx, 0, cz)) + CW_PI * 0.08f * (float)(i % 3 - 1)), V3(sc, sc, sc), (pcol){ 1, 1, 1 }, 1300u + (uint32_t)i);
  }
  ps_finish(ps);
  (void)t;
}

/* flat paving round the attractions: cracked concrete laid in cells, left out wherever the road is near */
static void build_paving(cw_node root, const track *t) {
  pmc *c = pmc_new(160, 55);
  struct { float x, z, hx, hz; } areas[] = {
    { -20, 212, 55, 40 }, { 0, 60, 52, 38 }, { P.helix_c.x, P.helix_c.z, 40, 40 }, { 112, -100, 44, 90 }, { 41, 735, 40, 28 }, { 97, -395, 36, 36 }, { 36, 560, 44, 44 },
  };
  pcol a = hex(0x9a8e78), b2 = hex(0x8a7e6a);
  int n = 0;
  for (int k = 0; k < 7; k++)
    for (float x = areas[k].x - areas[k].hx; x < areas[k].x + areas[k].hx; x += 10.0f)
      for (float z = areas[k].z - areas[k].hz; z < areas[k].z + areas[k].hz; z += 10.0f) {
        float cxx = x + 5.0f, czz = z + 5.0f;
        float ry;
        if (road_gap(t, cxx, czz, &ry) < 8.0f) continue;
        if (hypotf(cxx - areas[k].x, czz - areas[k].z) > fmaxf(areas[k].hx, areas[k].hz)) continue;
        float gy = fair_ground(cxx, czz, NULL) + 0.05f;
        pm *p = pmc_at(c, V3(cxx, gy, czz));
        pm_layer(p, PL_MAIN);
        pcol col = a;
        t3_vec3 q0 = V3(x, gy, z + 10.0f), q1 = V3(x + 10.0f, gy, z + 10.0f), q2 = V3(x + 10.0f, gy, z), q3 = V3(x, gy, z);
        pm_quad(p, q0, q1, q2, q3, col);
        n++;
      }
  pmc_finish(c, root, "Paving", false, TEX_ROAD_CONCRETE);
  cw_info("fair: %d paving cells", n);
}


/*@@PARTS_MORE@@*/

/*@@PARTS@@*/

/* ── the scenery ─────────────────────────────────────────────────────────────────────────────────── */

static dress_style fair_dress(void) {
  dress_style st = dress_defaults();
  st.main = rgb(0.72f, 0.28f, 0.2f);
  st.accent = rgb(0.98f, 0.82f, 0.3f);
  st.lamp = rgb(1.0f, 0.78f, 0.45f);
  st.has_colours = true;
  st.seed = 303;
  return st;
}

static void fair_scenery(cw_node scene, const track *t, uint32_t seed) {
  (void)seed;
  TR = t;
  find_places(t);
  THIN = game_planned_views() > 1;
  F.scene = scene;
  F.cam = find_type(scene, "Camera3D");
  fx_ambient_follow(scene, FXA_DUST, 0.5f);
  fx_ambient_at(scene, FXA_MIST, V3(P.helix_c.x, P.helix_c.y + 1.0f, P.helix_c.z), V3(46, 1.5f, 46), 0.5f, 220);
  fx_ambient_at(scene, FXA_FIREFLIES, V3(41, 3.0f, 735), V3(48, 3.0f, 30), 1.4f, 140);
  fx_ambient_at(scene, FXA_FIREFLIES, V3(P.helix_c.x, P.helix_c.y + 4.0f, P.helix_c.z), V3(46, 4.0f, 46), 1.0f, 140);
  cw_info("fair: loop1 c (%.1f %.1f %.1f) r %.0f  loop2 c (%.1f %.1f %.1f) r %.0f  helix c (%.1f %.1f %.1f) r %.1f top %.1f s %.0f..%.0f  crest s %.0f  drop s %.0f  jump s %.0f..%.0f",
          P.loop1_c.x, P.loop1_c.y, P.loop1_c.z, P.loop1_r, P.loop2_c.x, P.loop2_c.y, P.loop2_c.z, P.loop2_r, P.helix_c.x, P.helix_c.y, P.helix_c.z, P.helix_r,
          P.helix_top, P.helix_s0, P.helix_s1, P.crest_s, P.drop_s, P.jump_s0, P.jump_s1);
  cw_node root = cw_node_new("Node3D", "Fair");
  cw_node_add_child(scene, root);
  nspin = 0, nfw = 0, ncr = 0;
  memset(&WHEEL, 0, sizeof WHEEL);
  LIGHT_CAROUSEL = LIGHT_HAUNTED = CHAIN = (cw_node){ 0 };
  RS = 77;
  for (int i = 0; i < 8; i++) GM[i] = glow_mat_new();

  GO = ground_defaults();
  GO.height = fair_ground;
  GO.tex = TEX_GROUND_GRASS;
  GO.tex_repeat = 7;
  GO.tint = (pcol){ 1.9f, 1.15f, 0.34f };
  GO.tint2 = (pcol){ 1.8f, 1.1f, 0.33f };
  GO.lumpy = 0.08f;
  GO.patch_scale = 45;
  GO.base_y = 0;
  GO.seed = 5;
  ground_plane(root, t, &GO);

  dress_style st = fair_dress();
  dress_start_line(root, t, &st);
  dress_checkpoints(root, t, &st);
  deco_supports(root, t, SUP_TRESTLE, fair_ground, NULL, 0, 2.0f, &st);
  deco_jump_marks(root, t, &st);
  deco_rails(root, t, RAIL_FENCE, &st);

  pmc *str = pmc_new(90, 11);
  gcells *chase[3] = { gc_new(150, 21), gc_new(150, 22), gc_new(150, 23) };
  gcells *steady = gc_new(150, 24);
  frame clown = fr(0, 0, 38, CW_PI);
  build_clown(str, chase, steady, &clown);
  build_ferris(root, str, steady, V3(-20, 0, 212), 40.0f, 56.0f);
  build_tower(root, str, chase, steady, V3(P.helix_c.x, fair_ground(P.helix_c.x, P.helix_c.z, NULL), P.helix_c.z));
  pmc *wood = pmc_new(90, 12);
  gcells *flick[2] = { gc_new(150, 25), gc_new(150, 26) };
  build_park(root, str, wood, chase, steady, flick, t);
  dress_loop(str, chase, t, 0);
  dress_loop(str, chase, t, 1);
  dress_corkscrew(str, chase, t);
  dress_climb(t, str, chase);
  dress_helix(t, chase);
  /* the boardwalk: trestles under its raised middle, runway bulbs down both edges, hay bales on the island noses and
   * the sign at its mouth */
  for (int b = 0; b < t->nbranch; b++) {
    track v = track_branch_view(t, b);
    deco_supports(root, &v, SUP_TRESTLE, fair_ground, NULL, 0, 1.2f, &st);
    edge_bulbs(&v, chase, 0, v.length, 2.6f, 0.15f, 0.2f);
  }
  {
    branch_dress bd = branch_dress_defaults();
    bd.st = st;
    bd.lamp_spacing = 0;
    bd.tip = PK_HAY, bd.tip_scale = 1.2f;
    bd.sign_size = 4.0f;
    bd.approach = 32; /* after the corkscrew, before the checkpoint arch that hides the mouth */
    deco_branches(root, t, &bd);
  }
  {
    pmc *wire = pmc_new(150, 33);
    dress_jump(t, str, wire, chase);
    pmc_finish(wire, root, "JumpBunting", false, -1);
  }
  build_strings(root, t);
  pmc_finish(str, root, "Structures", true, -1);
  pmc_finish(wood, root, "Painted", true, TEX_WALL_WOOD);
  build_life(root, t);
  build_scatter(root, t);
  build_paving(root, t);
  build_fences(root, t);
  build_backdrop(root, t);
  for (int k = 0; k < 2; k++) gc_finish(flick[k], root, k ? "FlickerB" : "FlickerA", GM[GM_FLICK + k]);
  for (int k = 0; k < 3; k++) {
    char nm[24];
    snprintf(nm, sizeof nm, "Chase%d", k);
    gc_finish(chase[k], root, nm, GM[GM_PHASE_A + k]);
  }
  gc_finish(steady, root, "Bulbs", prop_material(PL_GLOW, -1, (pcol){ 1, 1, 1 }, 1));
  report_meshes(root);
}

/* dev aid: how many meshes (draws, before culling) each group of the map holds */
static int count_meshes(t3_object *o) {
  int n = (o->type == T3_MESH || o->type == T3_INSTANCED_MESH) ? 1 : 0;
  for (int i = 0; i < o->child_count; i++) n += count_meshes(o->children[i]);
  return n;
}
static void report_meshes(cw_node root) {
  int tot = 0;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(root, i);
    if (!cw_node_valid(c)) break;
    t3_object *o = cw_node3d_object(c);
    int n = o ? count_meshes(o) : 0;
    for (int k = 0;; k++) {
      cw_node cc = cw_node_child(c, k);
      if (!cw_node_valid(cc)) break;
      t3_object *oo = cw_node3d_object(cc);
      if (oo) n += count_meshes(oo);
    }
    tot += n;
    if (n > 20) cw_info("fair: group %s: %d meshes", cw_node_name(c), n);
  }
  cw_info("fair: %d meshes in the map", tot);
}

static cw_node find_type(cw_node n, const char *type) {
  if (cw_node_is(n, type)) return n;
  for (int i = 0;; i++) {
    cw_node c = cw_node_child(n, i);
    if (!cw_node_valid(c)) return (cw_node){ 0 };
    cw_node r = find_type(c, type);
    if (cw_node_valid(r)) return r;
  }
}

static void fair_tick(const sim *s, int ticks) {
  (void)s, (void)ticks;
}

static void fair_frame(float alpha, double time) {
  (void)alpha;
  float t = (float)time;
  /* the chasing bulbs: three phases, a pulse that runs round them */
  for (int set = 0; set < 2; set++)
    for (int k = 0; k < 3; k++) {
      float f = t * (set ? -2.1f : 1.7f) - (float)k / 3.0f;
      f -= floorf(f);
      float v = 0.16f + 0.84f * expf(-5.5f * f);
      GM[set * 3 + k]->color = (t3_color){ v, v, v };
    }
  for (int k = 0; k < 2; k++) {
    float v = sinf(t * (31.0f + 7.0f * (float)k)) * sinf(t * 17.3f + (float)k) > 0.82f ? 0.15f : 0.95f;
    GM[GM_FLICK + k]->color = (t3_color){ v, v, v };
  }
  life_frame(t);
  if (cw_node_valid(CHAIN)) {
    float off = fmodf(t * 4.0f, 1.8f); /* links climb at 4 m/s */
    cw_node3d_set_position(CHAIN, V3(CHAIN_DIR.x * off, CHAIN_DIR.y * off, CHAIN_DIR.z * off));
  }
  if (cw_node_valid(LIGHT_HAUNTED)) {
    float fl = 0.5f + 0.5f * (sinf(t * 23.0f) * sinf(t * 9.1f) > 0.55f ? 0.1f : 1.0f);
    cw_node_set(LIGHT_HAUNTED, "energy", cw_val_float(700.0f * fl));
  }
  for (int i = 0; i < nspin; i++) cw_node3d_set_quaternion(SPIN[i].n, t3_quat_from_axis_angle(SPIN[i].axis, SPIN[i].rate * t + SPIN[i].phase));
  if (cw_node_valid(WHEEL.wheel)) {
    float ang = 0.075f * t;
    cw_node3d_set_quaternion(WHEEL.wheel, t3_quat_from_axis_angle(V3(0, 0, 1), ang));
    for (int k = 0; k < 20; k++) {
      float a = CW_TAU * (float)k / 20.0f + ang;
      cw_node3d_set_position(WHEEL.gon[k], V3(WHEEL.hub.x + cosf(a) * WHEEL.R, WHEEL.hub.y + sinf(a) * WHEEL.R, WHEEL.hub.z));
    }
  }
  /* dev aid: RACER_FAIR_CAM=x,y,z,tx,ty,tz[,fov] holds the first view still, for looking at set pieces */
  static int dev = -1;
  static float c[7];
  if (dev < 0) {
    const char *e = getenv("RACER_FAIR_CAM");
    dev = e && sscanf(e, "%f,%f,%f,%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6]) >= 6;
    if (dev && c[6] <= 0) c[6] = 64;
  }
  /* RACER_FAIR_AT=s,back,up,ahead[,fov[,lat]]: a camera standing on the road behind s, looking ahead */
  static int at = -1;
  static float a[6];
  if (at < 0) {
    const char *e = getenv("RACER_FAIR_AT");
    a[4] = 70;
    at = e && sscanf(e, "%f,%f,%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) >= 4;
  }
  if (at && !dev && cw_node_valid(F.cam) && TR) {
    t3_vec3 e = track_point(TR, a[0] - a[1], a[5], a[2]), l = track_point(TR, a[0] + a[3], a[5] * 0.5f, a[2] * 0.4f + 1.0f);
    cw_node3d_set_position(F.cam, e);
    cw_node3d_look_at(F.cam, l, V3(0, 1, 0));
    cw_node_set(F.cam, "fov", cw_val_float(a[4]));
    cw_node3d_snapshot(F.cam);
  }
  if (dev && cw_node_valid(F.cam)) {
    cw_node3d_set_position(F.cam, V3(c[0], c[1], c[2]));
    cw_node3d_look_at(F.cam, V3(c[3], c[4], c[5]), V3(0, 1, 0));
    cw_node_set(F.cam, "fov", cw_val_float(c[6]));
    cw_node3d_snapshot(F.cam);
  }
}

const map_def map_fair = {
  .name = "Rustwheel Fairgrounds",
  .blurb = "The old park's coaster is the course: loops, corkscrews, a ferris wheel.",
  .scene = "scenes/fair.cwscene",
  .spec = MAP_SPEC,
  .mesh = { .wall_h = { 0, 0.9f, 1.6f, 3.2f }, .curb_w = 0.9f, .tile_v = 8.0f, .tunnel_clear = 8.0f },
  .look = { .road = { 0.95f, 0.82f, 0.7f }, .kerb_a = { 0.86f, 0.22f, 0.18f }, .kerb_b = { 0.96f, 0.92f, 0.8f }, .wall = { 0.75f, 1.0f, 0.92f },
            .wall_top = { 0.96f, 0.9f, 0.76f }, .road_tex = TEX_ROAD_WOOD, .wall_tex = TEX_WALL_WOOD, .road_rough = 0.8f },
  .laps = 3,
  .par_lap = 60,
  .scenery = fair_scenery,
  .scenery_tick = fair_tick,
  .scenery_frame = fair_frame,
};
