/* racer's battle arenas: the layout in the world, its physics shapes and its drawn geometry (see arena.h). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "cartwheel/math.h"
#include "props.h"
#include "props_gen.h"

static const piece ARENA_PIECES[] = {
  WALLS(0, 0),
  STRAIGHT(ARENA_LEN),
  END(),
};
const track_spec ARENA_TRACK = { .name = "arena", .pieces = ARENA_PIECES, .npieces = sizeof ARENA_PIECES / sizeof ARENA_PIECES[0], .hw0 = 95, .circuit = false };

#define RAMP_SKIRT 2.5f /* how far a ramp's sides flare out at its high end, per metre of height */

static arena_rt CUR;
static bool CUR_ON;
const arena_rt *arena_current(void) { return CUR_ON ? &CUR : NULL; }

static void add(arena_rt *rt, arena_prim p) {
  if (rt->n >= ARENA_MAXP) return;
  p.z += ARENA_CZ;
  rt->prim[rt->n] = p;
  cw_sincos(p.yaw * (CW_PI / 180.0f), &rt->sn[rt->n], &rt->cs[rt->n]);
  rt->n++;
}

/* the top of floor-like shape p (arena frame) at (x, z), or false when (x, z) is not on it */
static bool deck_top(const arena_prim *p, float x, float z, float *top) {
  if (p->ghost || (p->look != AL_DECK && p->look != AL_RAMP)) return false;
  float sn, cs;
  cw_sincos(p->yaw * (CW_PI / 180.0f), &sn, &cs);
  float dx = x - p->x, dz = z - p->z, lx = dx * cs - dz * sn, lz = dx * sn + dz * cs;
  if (p->kind == AP_POST) {
    if (lx * lx + lz * lz > p->w * p->w * 0.25f) return false;
  } else if (lx < -p->w * 0.5f || lx > p->w * 0.5f || lz < -p->d * 0.5f || lz > p->d * 0.5f) return false;
  *top = p->kind == AP_RAMP ? p->y + p->h * (lz / p->d + 0.5f) : p->y + p->h;
  if (p->kind == AP_MOUND) *top = p->y + fminf(p->h, fminf(p->w * 0.5f - fabsf(lx), p->d * 0.5f - fabsf(lz)) / MOUND_RUN);
  return true;
}

/* another deck (not shape self) carries on from (x, z) at about this height: the rail stops there, a way off */
static bool deck_beyond(const arena_def *def, int self, float x, float z, float top) {
  for (int j = 0; j < def->nprims; j++) {
    float t;
    if (j != self && deck_top(&def->prims[j], x, z, &t) && fabsf(t - top) < 0.6f) return true;
  }
  return false;
}

/* deck i's rails (arena_prim.rail): round its edge, inside it, stopping wherever another deck carries on from it. A box's
 * four sides in straight runs, a round deck's rim in chords */
static void add_rails(arena_rt *rt, const arena_def *def, int i) {
  const arena_prim *p = &def->prims[i];
  const float rw = 0.6f, out = 1.2f;
  float top = p->y + p->h, sn, cs;
  if (p->kind == AP_POST) {
    float r = p->w * 0.5f;
    int n = (int)(CW_TAU * r / 3.0f);
    n = n < 12 ? 12 : n;
    for (int k = 0; k < n; k++) {
      float a = ((float)k + 0.5f) * (CW_TAU / (float)n);
      cw_sincos(a, &sn, &cs);
      if (deck_beyond(def, i, p->x + sn * (r + out), p->z + cs * (r + out), top)) continue;
      float rr = r - rw * 0.5f, len = 2.0f * rr * cw_sin(CW_PI / (float)n) + 0.35f;
      add(rt, (arena_prim){ AP_BOX, AL_TRIM, p->x + sn * rr, p->z + cs * rr, top, rw, p->rail, len, a * (180.0f / CW_PI) + 90.0f, false, 0 });
    }
    return;
  }
  cw_sincos(p->yaw * (CW_PI / 180.0f), &sn, &cs);
  for (int side = 0; side < 4; side++) {
    /* the side's run in the deck's frame: along u from -half to half, at v across (vn the way out) */
    bool alongz = side < 2;
    float vn = side & 1 ? 1.0f : -1.0f;
    float half = alongz ? p->d * 0.5f : p->w * 0.5f, v = vn * (alongz ? p->w * 0.5f : p->d * 0.5f);
    int steps = (int)(half * 2.0f) + 1;
    float du = half * 2.0f / (float)steps;
    int run0 = -1;
    for (int k = 0; k <= steps; k++) {
      bool keep = false;
      if (k < steps) {
        float u = -half + ((float)k + 0.5f) * du;
        float lx = alongz ? v + vn * out : u, lz = alongz ? u : v + vn * out;
        keep = !deck_beyond(def, i, p->x + lx * cs + lz * sn, p->z - lx * sn + lz * cs, top);
      }
      if (keep && run0 < 0) run0 = k;
      if (!keep && run0 >= 0) {
        float u0 = -half + (float)run0 * du, u1 = -half + (float)k * du;
        float um = (u0 + u1) * 0.5f, vin = v - vn * rw * 0.5f;
        float lx = alongz ? vin : um, lz = alongz ? um : vin;
        add(rt, (arena_prim){ AP_BOX, AL_TRIM, p->x + lx * cs + lz * sn, p->z - lx * sn + lz * cs, top, alongz ? rw : u1 - u0, p->rail,
                              alongz ? u1 - u0 : rw, p->yaw, false, 0 });
        run0 = -1;
      }
    }
  }
}

void arena_rt_build(arena_rt *rt, const arena_def *def) {
  memset(rt, 0, sizeof *rt);
  rt->def = def;
  rt->half_x = def->half_x, rt->half_z = def->half_z;
  rt->centre = (t3_vec3){ 0, 0, ARENA_CZ };
  float hx = def->half_x, hz = def->half_z, wt = 3.0f, wh = def->wall_h > 0 ? def->wall_h : 3.0f;
  if (!def->open) {
    /* the floor, a slab under y = 0 reaching under the walls, and the four walls round it */
    add(rt, (arena_prim){ AP_BOX, AL_FLOOR, 0, 0, -2.0f, hx * 2 + wt * 2, 2.0f, hz * 2 + wt * 2, 0, false });
    add(rt, (arena_prim){ AP_BOX, AL_WALL, 0, hz + wt * 0.5f, 0, hx * 2 + wt * 2, wh, wt, 0, false });
    add(rt, (arena_prim){ AP_BOX, AL_WALL, 0, -hz - wt * 0.5f, 0, hx * 2 + wt * 2, wh, wt, 0, false });
    add(rt, (arena_prim){ AP_BOX, AL_WALL, hx + wt * 0.5f, 0, 0, wt, wh, hz * 2, 0, false });
    add(rt, (arena_prim){ AP_BOX, AL_WALL, -hx - wt * 0.5f, 0, 0, wt, wh, hz * 2, 0, false });
  }
  for (int i = 0; i < def->nprims; i++) add(rt, def->prims[i]);
  for (int i = 0; i < def->nprims; i++)
    if (def->prims[i].rail > 0) add_rails(rt, def, i);
  rt->ntele = def->nteles > ARENA_MAXT ? ARENA_MAXT : def->nteles;
  for (int i = 0; i < rt->ntele; i++) {
    const arena_tele *t = &def->teles[i];
    struct arena_tele_rt *o = &rt->tele[i];
    o->at = (t3_vec3){ t->x, t->y, t->z + ARENA_CZ };
    o->to = (t3_vec3){ t->tx, t->ty, t->tz + ARENA_CZ };
    o->r = t->r, o->speed = t->speed, o->col = t->col;
    o->yaw = t->tyaw * (CW_PI / 180.0f), o->in_yaw = t->in_yaw * (CW_PI / 180.0f);
  }
  rt->nboost = def->nboosts > ARENA_MAXB ? ARENA_MAXB : def->nboosts;
  for (int i = 0; i < rt->nboost; i++) {
    const arena_boost *b = &def->boosts[i];
    struct arena_boost_rt *o = &rt->boost[i];
    o->at = (t3_vec3){ b->x, b->y, b->z + ARENA_CZ };
    o->hw = b->w * 0.5f, o->hd = b->d * 0.5f;
    cw_sincos(b->yaw * (CW_PI / 180.0f), &o->sn, &o->cs);
  }
  rt->nspawn = def->nspawns > 16 ? 16 : def->nspawns;
  for (int i = 0; i < rt->nspawn; i++) {
    rt->spawn[i][0] = def->spawns[i][0];
    rt->spawn[i][1] = def->spawns[i][1] + ARENA_CZ;
    rt->spawn[i][2] = def->spawns[i][2] * (CW_PI / 180.0f);
  }
  if (rt == &CUR) CUR_ON = true;
}

/* the build at sim_init goes into the shared one (the scenery and the HUD read it) */
const arena_rt *arena_load(const arena_def *def) {
  CUR_ON = false;
  if (!def) return NULL;
  arena_rt_build(&CUR, def);
  return &CUR;
}

static t3_quat yaw_q(float sn, float cs) {
  /* a rotation about +y by the angle with this sine and cosine: half-angle by the identities (no libm) */
  float c2 = sqrtf((1.0f + cs) * 0.5f), s2 = sqrtf((1.0f - cs) * 0.5f);
  if (sn < 0) s2 = -s2;
  return (t3_quat){ 0, s2, 0, c2 };
}

/* a local point of shape i to the world */
static t3_vec3 to_world(const arena_rt *rt, int i, float lx, float ly, float lz) {
  const arena_prim *p = &rt->prim[i];
  float cs = rt->cs[i], sn = rt->sn[i];
  return (t3_vec3){ p->x + lx * cs + lz * sn, p->y + ly, p->z - lx * sn + lz * cs };
}

t3_vec3 arena_prim_point(const arena_rt *rt, int i, float lx, float ly, float lz) { return to_world(rt, i, lx, ly, lz); }
void arena_prim_local(const arena_rt *rt, int i, t3_vec3 p, float *lx, float *lz) {
  const arena_prim *q = &rt->prim[i];
  float dx = p.x - q->x, dz = p.z - q->z, cs = rt->cs[i], sn = rt->sn[i];
  /* the inverse of to_world's rotation */
  *lx = dx * cs - dz * sn;
  *lz = dx * sn + dz * cs;
}

static float level_under(const arena_rt *rt, float x, float z, float y_max) {
  float best = 0;
  for (int i = 0; i < rt->n; i++) {
    const arena_prim *p = &rt->prim[i];
    if (p->ghost || (p->look != AL_DECK && p->look != AL_RAMP)) continue;
    float lx, lz;
    arena_prim_local(rt, i, (t3_vec3){ x, 0, z }, &lx, &lz);
    if (lx < -p->w * 0.5f || lx > p->w * 0.5f || lz < -p->d * 0.5f || lz > p->d * 0.5f) continue;
    float top = p->kind == AP_RAMP ? p->y + p->h * (lz / p->d + 0.5f) : p->y + p->h;
    if (p->kind == AP_MOUND) {
      float e = fminf(p->w * 0.5f - fabsf(lx), p->d * 0.5f - fabsf(lz));
      top = p->y + fminf(p->h, e / MOUND_RUN);
    }
    if (top > best && top <= y_max) best = top;
  }
  return best;
}

/* ── the rock (arena_def.terrain) ─────────────────────────────────────────────────────────────────────
 * Sampled once per arena (the sim and the scenery share it): the top cut to the outline as triangles, the edge as short
 * segments (each with the way out), all in the arena's frame. Marching squares: a cell's corners inside the rock (at() >
 * 0) and the points where its sides cross the edge (at() interpolated along the side) make its piece of the top, and the
 * crossings pair up into the edge. A side's crossing is worked out from its two ends in one order whichever cell asks, so
 * neighbouring cells share it exactly and the top has no cracks. */

static struct terr {
  const arena_terrain *def;
  int ntri, nseg, nruns;
  float *tri;       /* ntri x 9: three corners (x, y, z) wound so the face looks up */
  float *seg;       /* nseg x 8: two ends (x, y, z), then the way out (x, z) */
  float (*runs)[5];
  const arena_terrain *runs_def;
} T;

static bool terr_in(float v) { return v > 0; }

/* where the side between samples a and b (a first, the lower index) crosses the edge */
static t3_vec3 terr_cross(const arena_terrain *d, float xa, float za, float va, float xb, float zb, float vb) {
  float t = va / (va - vb), x = xa + (xb - xa) * t, z = za + (zb - za) * t, top = 0;
  d->at(x, z, &top);
  return (t3_vec3){ x, top, z };
}

static void terr_build(const arena_terrain *d) {
  if (T.def == d) return;
  free(T.tri), free(T.seg);
  T.tri = T.seg = NULL;
  T.ntri = T.nseg = 0;
  T.def = d;
  if (!d) return;
  int nx = (int)((d->x1 - d->x0) / d->cell) + 2, nz = (int)((d->z1 - d->z0) / d->cell) + 2;
  float *v = malloc(sizeof(float) * (size_t)(nx * nz)), *h = malloc(sizeof(float) * (size_t)(nx * nz));
  for (int j = 0; j < nz; j++)
    for (int i = 0; i < nx; i++) v[j * nx + i] = d->at(d->x0 + (float)i * d->cell, d->z0 + (float)j * d->cell, &h[j * nx + i]);
  int cap_t = 4096, cap_s = 1024;
  T.tri = malloc(sizeof(float) * 9 * (size_t)cap_t);
  T.seg = malloc(sizeof(float) * 8 * (size_t)cap_s);
  for (int j = 0; j + 1 < nz; j++)
    for (int i = 0; i + 1 < nx; i++) {
      /* the corners round the cell, and each side's two ends in the canonical (lower index first) order */
      static const int CI[4] = { 0, 1, 1, 0 }, CJ[4] = { 0, 0, 1, 1 };
      bool in[4];
      int nin = 0;
      for (int k = 0; k < 4; k++) nin += in[k] = terr_in(v[(j + CJ[k]) * nx + i + CI[k]]);
      if (nin == 0) continue;
      t3_vec3 poly[8], X[4];
      int np = 0, nxc = 0;
      for (int k = 0; k < 4; k++) {
        int ia = i + CI[k], ja = j + CJ[k], ib = i + CI[(k + 1) & 3], jb = j + CJ[(k + 1) & 3];
        if (in[k]) poly[np++] = (t3_vec3){ d->x0 + (float)ia * d->cell, h[ja * nx + ia], d->z0 + (float)ja * d->cell };
        if (in[k] != in[(k + 1) & 3]) {
          if (ib < ia || jb < ja) { int t = ia; ia = ib, ib = t; t = ja; ja = jb, jb = t; }
          X[nxc] = terr_cross(d, d->x0 + (float)ia * d->cell, d->z0 + (float)ja * d->cell, v[ja * nx + ia], d->x0 + (float)ib * d->cell,
                              d->z0 + (float)jb * d->cell, v[jb * nx + ib]);
          poly[np++] = X[nxc++];
        }
      }
      for (int k = 1; k + 1 < np; k++) {
        if (T.ntri >= cap_t) cap_t *= 2, T.tri = realloc(T.tri, sizeof(float) * 9 * (size_t)cap_t);
        t3_vec3 a = poly[0], b = poly[k], c = poly[k + 1];
        /* up: (b - a) x (c - a) has +y */
        if ((b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z) < 0) { t3_vec3 t = b; b = c, c = t; }
        float *o = &T.tri[T.ntri++ * 9];
        o[0] = a.x, o[1] = a.y, o[2] = a.z, o[3] = b.x, o[4] = b.y, o[5] = b.z, o[6] = c.x, o[7] = c.y, o[8] = c.z;
      }
      /* the edge: two crossings make one piece, four (a saddle, the two inside corners joined) two, each cutting off an
       * outside corner */
      int pairs = nxc / 2;
      for (int p = 0; p < pairs; p++) {
        t3_vec3 A, B;
        if (nxc == 2) A = X[0], B = X[1];
        else if (in[0]) A = X[p * 2], B = X[p * 2 + 1];
        else A = X[p * 2 + 1], B = X[(p * 2 + 2) & 3];
        float dx = B.x - A.x, dz = B.z - A.z, l = sqrtf(dx * dx + dz * dz);
        if (l < 1e-4f) continue;
        float nxo = dz / l, nzo = -dx / l, mx = (A.x + B.x) * 0.5f, mz = (A.z + B.z) * 0.5f, t0, t1;
        /* the way out is towards the lower inside measure */
        if (d->at(mx + nxo * 0.3f, mz + nzo * 0.3f, &t0) > d->at(mx - nxo * 0.3f, mz - nzo * 0.3f, &t1)) nxo = -nxo, nzo = -nzo;
        if (T.nseg >= cap_s) cap_s *= 2, T.seg = realloc(T.seg, sizeof(float) * 8 * (size_t)cap_s);
        float *o = &T.seg[T.nseg++ * 8];
        o[0] = A.x, o[1] = A.y, o[2] = A.z, o[3] = B.x, o[4] = B.y, o[5] = B.z, o[6] = nxo, o[7] = nzo;
      }
    }
  free(v), free(h);
}

static float terr_top(const arena_terrain *d, float x, float z, float *top) { return d->at(x, z - ARENA_CZ, top); }

bool arena_on_terrain(const arena_rt *rt, t3_vec3 p) {
  const arena_terrain *d = rt->def->terrain;
  float top = 0;
  if (!d || terr_top(d, p.x, p.z, &top) < -0.5f) return false;
  return p.y > top - 1.0f && p.y < top + 2.5f;
}

int arena_terrain_runs(const arena_def *def, const float (**runs)[5]) {
  const arena_terrain *d = def->terrain;
  *runs = NULL;
  if (!d) return 0;
  if (T.runs_def != d) {
    /* rows 4 m apart, runs along them of 4 m cells whose middles are on the rock */
    free(T.runs);
    T.runs_def = d, T.nruns = 0;
    int cap = 256;
    T.runs = malloc(sizeof(float[5]) * (size_t)cap);
    const float c = 4.0f;
    for (float z = d->z0; z < d->z1; z += c) {
      float x0 = 0, hs = 0;
      int n = 0;
      for (float x = d->x0; x <= d->x1 + c; x += c) {
        float top = 0;
        bool on = x <= d->x1 && d->at(x + c * 0.5f, z + c * 0.5f, &top) > 0;
        if (on) {
          if (!n) x0 = x, hs = 0;
          n++, hs += top;
        } else if (n) {
          if (T.nruns >= cap) cap *= 2, T.runs = realloc(T.runs, sizeof(float[5]) * (size_t)cap);
          float *r = T.runs[T.nruns++];
          r[0] = x0, r[1] = z, r[2] = x, r[3] = z + c, r[4] = hs / (float)n;
          n = 0;
        }
      }
    }
  }
  *runs = (const float (*)[5])T.runs;
  return T.nruns;
}

float arena_level(const arena_rt *rt, float x, float z) { return arena_level_below(rt, x, 1e9f, z); }
float arena_level_below(const arena_rt *rt, float x, float y, float z) {
  float best = level_under(rt, x, z, y + 1.5f), top = 0;
  const arena_terrain *d = rt->def ? rt->def->terrain : NULL;
  if (d && terr_top(d, x, z, &top) > 0 && top <= y + 1.5f && top > best) best = top;
  return best;
}

/* the rock's collision: its top as one triangle mesh (floor), its rim as a short block on each piece of the edge (wall) */
static cw_mesh3d TERR_MESH;

static void terr_phys(const arena_rt *rt, cw_body3d b, int track_layer, int wall_layer) {
  /* the last arena's mesh: its world is gone (the sim frees the world before it makes the next) */
  if (TERR_MESH.id) cw_mesh3d_destroy(TERR_MESH);
  TERR_MESH = (cw_mesh3d){ 0 };
  const arena_terrain *d = rt->def->terrain;
  if (!d) return;
  terr_build(d);
  int nv = T.ntri * 3;
  t3_vec3 *vs = malloc(sizeof(t3_vec3) * (size_t)nv);
  int32_t *ix = malloc(sizeof(int32_t) * (size_t)nv);
  for (int k = 0; k < nv; k++) {
    const float *p = &T.tri[k * 3];
    vs[k] = (t3_vec3){ p[0], p[1], p[2] + ARENA_CZ };
    ix[k] = k;
  }
  TERR_MESH = cw_mesh3d_create(vs, nv, ix, T.ntri);
  free(vs), free(ix);
  cw_shape3d_def sd = cw_shape3d_default_def();
  sd.layer = track_layer;
  sd.material.friction = 0.9f;
  sd.material.restitution = 0.0f;
  cw_shape3d_mesh(b, &sd, TERR_MESH, (t3_vec3){ 1, 1, 1 });
  if (d->lip <= 0) return;
  sd.layer = wall_layer;
  sd.material.friction = 0.0f;
  float up = fmaxf(rt->def->fence, d->lip), in = 0.9f;
  for (int s = 0; s < T.nseg; s++) {
    const float *g = &T.seg[s * 8];
    float ox = g[6], oz = g[7], z0 = g[2] + ARENA_CZ, z1 = g[5] + ARENA_CZ;
    t3_vec3 pts[8] = {
      { g[0], g[1] - 1.5f, z0 }, { g[3], g[4] - 1.5f, z1 }, { g[0] - ox * in, g[1] - 1.5f, z0 - oz * in }, { g[3] - ox * in, g[4] - 1.5f, z1 - oz * in },
      { g[0], g[1] + up, z0 }, { g[3], g[4] + up, z1 }, { g[0] - ox * in, g[1] + up, z0 - oz * in }, { g[3] - ox * in, g[4] + up, z1 - oz * in },
    };
    cw_shape3d_hull(b, &sd, pts, 8);
  }
}

int arena_gates(const arena_rt *rt, int *gates, int max) {
  int n = 0;
  for (int i = 0; i < rt->n; i++) {
    int g = rt->prim[i].gate, k = 0;
    if (!g || rt->prim[i].ghost) continue;
    while (k < n && gates[k] != g) k++;
    if (k == n && n < max) gates[n++] = g;
  }
  return n;
}

void arena_phys_build(const arena_rt *rt, cw_body3d b, int track_layer, int wall_layer, int gate) {
  if (!gate) terr_phys(rt, b, track_layer, wall_layer);
  for (int i = 0; i < rt->n; i++) {
    const arena_prim *p = &rt->prim[i];
    if (p->ghost || p->gate != gate) continue;
    bool floorish = p->look == AL_FLOOR || p->look == AL_DECK || p->look == AL_RAMP;
    cw_shape3d_def sd = cw_shape3d_default_def();
    sd.layer = floorish ? track_layer : wall_layer;
    sd.material.friction = floorish ? 0.9f : 0.0f;
    sd.material.restitution = 0.0f;
    if (p->kind == AP_BOX) {
      float h = p->look == AL_TRIM && rt->def->fence > p->h ? rt->def->fence : p->h;
      cw_xform3 x = { { p->x, p->y + h * 0.5f, p->z }, yaw_q(rt->sn[i], rt->cs[i]) };
      cw_shape3d_box(b, &sd, (t3_vec3){ p->w * 0.5f, h * 0.5f, p->d * 0.5f }, x);
    } else if (p->kind == AP_RAMP) {
      /* the sides flare out at the foot (RAMP_SKIRT): a kart running along the edge slides off it instead of hanging on it */
      float hx = p->w * 0.5f, hz = p->d * 0.5f, e = RAMP_SKIRT * p->h;
      t3_vec3 pts[6] = { to_world(rt, i, -hx, 0, -hz), to_world(rt, i, hx, 0, -hz), to_world(rt, i, -hx - e, 0, hz),
                         to_world(rt, i, hx + e, 0, hz), to_world(rt, i, -hx, p->h, hz), to_world(rt, i, hx, p->h, hz) };
      cw_shape3d_hull(b, &sd, pts, 6);
    } else if (p->kind == AP_MOUND) {
      float bx = p->w * 0.5f, bz = p->d * 0.5f, in = p->h * MOUND_RUN;
      t3_vec3 pts[8] = { to_world(rt, i, -bx, 0, -bz), to_world(rt, i, bx, 0, -bz), to_world(rt, i, -bx, 0, bz), to_world(rt, i, bx, 0, bz),
                         to_world(rt, i, -bx + in, p->h, -bz + in), to_world(rt, i, bx - in, p->h, -bz + in), to_world(rt, i, -bx + in, p->h, bz - in),
                         to_world(rt, i, bx - in, p->h, bz - in) };
      cw_shape3d_hull(b, &sd, pts, 8);
    } else {
      t3_vec3 pts[24];
      float r = p->w * 0.5f;
      for (int k = 0; k < 12; k++) {
        float sn, cs;
        cw_sincos((float)k * (CW_TAU / 12.0f), &sn, &cs);
        pts[k] = (t3_vec3){ p->x + r * cs, p->y, p->z + r * sn };
        pts[k + 12] = (t3_vec3){ p->x + r * cs, p->y + p->h, p->z + r * sn };
      }
      cw_shape3d_hull(b, &sd, pts, 24);
    }
  }
}

/* ── the drawn arena ──────────────────────────────────────────────────────────────────────────────── */

static pcol prim_col(const arena_prim *p, const arena_look *L, float k) {
  pcol c = p->col ? hex(p->col) : rgb(L->tint[0], L->tint[1], L->tint[2]);
  c = pc_mul(c, k * (L->gain > 0 ? L->gain : 1.0f));
  /* a floor is the biggest thing on screen: it never goes brighter than this, or a pale one (salt) glares in every view */
  float peak = fmaxf(c.r, fmaxf(c.g, c.b));
  if (p->look == AL_FLOOR && peak > 1.45f) c = pc_mul(c, 1.45f / peak);
  return c;
}

static void draw_prim(pm *b, const arena_prim *p, const arena_look *L, int i) {
  pm_push(b);
  pm_move(b, p->x, p->y, p->z);
  pm_roty(b, p->yaw * (CW_PI / 180.0f));
  /* darker at the foot than the top: a cheap ambient occlusion */
  pcol lo = prim_col(p, L, 0.62f), hi = prim_col(p, L, 1.0f);
  if (p->look == AL_FLOOR) lo = hi;
  if (p->kind == AP_BOX) pm_box(b, p->w, p->h, p->d, lo, hi);
  else if (p->kind == AP_RAMP) {
    pm_wedge(b, p->w, p->h, p->d, prim_col(p, L, 0.8f), hi);
    /* the flared sides (both windings: a thin skirt seen from either side) */
    float hx = p->w * 0.5f, hz = p->d * 0.5f, e = RAMP_SKIRT * p->h;
    pcol sc = prim_col(p, L, 0.7f);
    for (int sd = -1; sd <= 1; sd += 2) {
      float x0 = (float)sd * hx, x1 = (float)sd * (hx + e);
      t3_vec3 a = { x0, 0, -hz }, c = { x1, 0, hz }, t = { x0, p->h, hz }, f = { x0, 0, hz };
      pm_tri(b, a, c, t, sc), pm_tri(b, a, t, c, sc);
      pm_tri(b, c, f, t, sc), pm_tri(b, c, t, f, sc);
    }
  }
  else if (p->kind == AP_MOUND) {
    /* the top and four sloping sides (both windings, so neither depends on which way round a side was listed) */
    float bx = p->w * 0.5f, bz = p->d * 0.5f, tx = bx - p->h * MOUND_RUN, tz = bz - p->h * MOUND_RUN, h = p->h;
    t3_vec3 B[4] = { { -bx, 0, -bz }, { bx, 0, -bz }, { bx, 0, bz }, { -bx, 0, bz } }, T[4] = { { -tx, h, -tz }, { tx, h, -tz }, { tx, h, tz }, { -tx, h, tz } };
    pcol slope = prim_col(p, L, 0.82f);
    for (int k = 0; k < 4; k++) {
      int n = (k + 1) & 3;
      pm_quad(b, B[k], B[n], T[n], T[k], slope);
      pm_quad(b, B[k], T[k], T[n], B[n], slope);
    }
    pm_quad(b, T[0], T[1], T[2], T[3], hi);
    pm_quad(b, T[0], T[3], T[2], T[1], hi);
  } else pm_cyl(b, p->w * 0.5f, p->w * 0.5f, p->h, p->w > 6 ? 20 : 12, lo, hi);
  (void)i;
  pm_pop(b);
}

/* the drawn rock: its top in faceted triangles (a little lighter and darker in drifts), a rough rim along the edge, the
 * face below it falling away jagged, and its underside sagging deeper the farther in (the belly of an asteroid) */
static float terr_n(float x, float z, float f, uint32_t seed) { return pm_noise3(x * f, 0.37f, z * f, seed); }

static void terr_gfx(cw_node root, const arena_def *ad, uint32_t seed) {
  const arena_terrain *d = ad->terrain;
  if (!d) return;
  terr_build(d);
  pcol top = hex(d->top_col ? d->top_col : 0x5a524a), side = hex(d->side_col ? d->side_col : 0x3e3934);
  pmc *c = pmc_new(64.0f, seed + 701u);
  pmc_texture_metres(c, d->tex_m > 0 ? d->tex_m : 8.0f);
  float cz = ARENA_CZ;
  for (int t = 0; t < T.ntri; t++) {
    const float *p = &T.tri[t * 9];
    t3_vec3 a = { p[0], p[1], p[2] + cz }, b = { p[3], p[4], p[5] + cz }, e = { p[6], p[7], p[8] + cz };
    t3_vec3 u = { b.x - a.x, b.y - a.y, b.z - a.z }, w = { e.x - a.x, e.y - a.y, e.z - a.z };
    t3_vec3 n = { u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };
    float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (l < 1e-6f) continue;
    n = (t3_vec3){ n.x / l, n.y / l, n.z / l };
    pcol k[3];
    const t3_vec3 *v3[3] = { &a, &b, &e };
    for (int q = 0; q < 3; q++) {
      float x = v3[q]->x, z = v3[q]->z;
      float m = 0.72f + 0.3f * terr_n(x, z, 0.03f, seed + 3u) + 0.2f * terr_n(x, z, 0.12f, seed + 5u) + 0.1f * terr_n(x, z, 0.4f, seed + 7u);
      k[q] = pc_mix(pc_mul(top, m), pc_mul(side, m), 0.5f + 0.5f * terr_n(x, z, 0.05f, seed + 9u));
    }
    pm *pb = pmc_at(c, (t3_vec3){ (a.x + b.x + e.x) / 3, (a.y + b.y + e.y) / 3, (a.z + b.z + e.z) / 3 });
    pm_tri_n(pb, a, n, k[0], b, n, k[1], e, n, k[2]);
  }
  pmc_finish(c, root, "ArenaRockTop", false, d->tex);

  /* the rim, the face and the underside: one rock below the top */
  c = pmc_new(64.0f, seed + 709u);
  pmc_texture_metres(c, d->tex_m > 0 ? d->tex_m : 8.0f);
  for (int s = 0; s < T.nseg; s++) {
    const float *g = &T.seg[s * 8];
    float ox = g[6], oz = g[7];
    t3_vec3 P[2] = { { g[0], g[1], g[2] + cz }, { g[3], g[4], g[5] + cz } }, foot[2], crest[2], rim[2], mid[2], low[2];
    for (int q = 0; q < 2; q++) {
      float x = P[q].x, z = P[q].z, ft = 0;
      float kh = 0.75f + 0.45f * terr_n(x, z, 0.35f, seed + 21u), jag = 1.6f + 1.6f * terr_n(x, z, 0.09f, seed + 23u);
      float dep = d->depth * (0.75f + 0.4f * terr_n(x, z, 0.04f, seed + 25u));
      terr_top(d, x - ox * 2.4f, z - oz * 2.4f, &ft);
      foot[q] = (t3_vec3){ x - ox * 2.4f, ft + 0.02f, z - oz * 2.4f };
      crest[q] = (t3_vec3){ x - ox * 0.7f, P[q].y + d->lip * kh, z - oz * 0.7f };
      rim[q] = (t3_vec3){ x + ox * 0.35f, P[q].y + d->lip * kh * 0.45f, z + oz * 0.35f };
      mid[q] = (t3_vec3){ x + ox * jag, P[q].y - dep * 0.4f, z + oz * jag };
      low[q] = (t3_vec3){ x, P[q].y - dep, z };
    }
    /* the rim only where there is one; the face always */
    pm *pb = pmc_at(c, P[0]);
    pcol sc = pc_mul(side, 0.85f + 0.2f * terr_n(P[0].x, P[0].z, 0.2f, seed + 27u));
    if (d->lip > 0) {
      pm_quad(pb, foot[0], foot[1], crest[1], crest[0], pc_mul(top, 0.92f));
      pm_quad(pb, foot[0], crest[0], crest[1], foot[1], pc_mul(top, 0.92f));
      pm_quad(pb, crest[0], crest[1], rim[1], rim[0], sc);
      pm_quad(pb, crest[0], rim[0], rim[1], crest[1], sc);
    } else rim[0] = P[0], rim[1] = P[1];
    pm_quad(pb, rim[0], rim[1], mid[1], mid[0], sc);
    pm_quad(pb, rim[0], mid[0], mid[1], rim[1], sc);
    pm_quad(pb, mid[0], mid[1], low[1], low[0], pc_mul(sc, 0.8f));
    pm_quad(pb, mid[0], low[0], low[1], mid[1], pc_mul(sc, 0.8f));
  }
  /* the underside: every piece of the top again, lower by the face's depth at the edge and the belly in from it */
  for (int t = 0; t < T.ntri; t++) {
    const float *p = &T.tri[t * 9];
    t3_vec3 v[3];
    for (int q = 0; q < 3; q++) {
      float x = p[q * 3], z = p[q * 3 + 2] + cz, ft = 0;
      float inside = fmaxf(terr_top(d, x, z, &ft), 0);
      float dep = d->depth * (0.75f + 0.4f * terr_n(x, z, 0.04f, seed + 25u)) + d->belly * fminf(inside, 60.0f) * (0.8f + 0.4f * terr_n(x, z, 0.02f, seed + 29u));
      v[q] = (t3_vec3){ x, p[q * 3 + 1] - dep, z };
    }
    pm *pb = pmc_at(c, v[0]);
    pm_tri(pb, v[0], v[2], v[1], pc_mul(side, 0.6f));
  }
  pmc_finish(c, root, "ArenaRock", true, d->side_tex ? d->side_tex : d->tex);
}

/* the shapes of one look (and one gate: 0 the ones that never go) as one batch under parent */
static void draw_look(cw_node parent, const arena_rt *rt, int look, int gate, uint32_t seed) {
  const arena_look *L = &rt->def->look[look];
  pmc *c = pmc_new(look == AL_FLOOR ? 30.0f : 48.0f, seed + (uint32_t)look * 31u + (uint32_t)(gate + 64) * 977u);
  if (L->tex_m > 0) pmc_texture_metres(c, L->tex_m);
  int count = 0;
  for (int i = 0; i < rt->n; i++) {
    const arena_prim *p = &rt->prim[i];
    if (p->look != look || p->gate != gate) continue;
    if (look == AL_FLOOR && p->kind == AP_BOX && p->w > 40) {
      /* the floor in tiles, so the views cull it and the texture keeps its scale */
      float tile = 20.0f;
      int nx = (int)ceilf(p->w / tile), nz = (int)ceilf(p->d / tile);
      for (int a = 0; a < nx; a++)
        for (int z = 0; z < nz; z++) {
          float x0 = p->x - p->w * 0.5f + (float)a * tile, z0 = p->z - p->d * 0.5f + (float)z * tile;
          float w = fminf(tile, p->x + p->w * 0.5f - x0), dd = fminf(tile, p->z + p->d * 0.5f - z0);
          arena_prim t = *p;
          t.x = x0 + w * 0.5f, t.z = z0 + dd * 0.5f, t.w = w, t.d = dd;
          pm *b = pmc_at(c, (t3_vec3){ t.x, 0, t.z });
          if (L->metal > 0.5f) pm_layer(b, PL_METAL);
          draw_prim(b, &t, L, i);
          count++;
        }
      continue;
    }
    pm *b = pmc_at(c, (t3_vec3){ p->x, p->y + p->h * 0.5f, p->z });
    pm_layer(b, look == AL_GLOW ? PL_GLOW : L->metal > 0.5f ? PL_METAL : PL_MAIN);
    arena_prim q = *p;
    if (look == AL_GLOW) {
      pcol g = glow(q.col ? hex(q.col) : rgb(L->tint[0], L->tint[1], L->tint[2]), L->glow > 0 ? L->glow : 3.0f);
      pm_push(b);
      pm_move(b, q.x, q.y, q.z);
      pm_roty(b, q.yaw * (CW_PI / 180.0f));
      if (q.kind == AP_POST) pm_cyl(b, q.w * 0.5f, q.w * 0.5f, q.h, 10, g, g);
      else pm_box(b, q.w, q.h, q.d, g, g);
      pm_pop(b);
    } else draw_prim(b, &q, L, i);
    count++;
  }
  static const char *const NAME[AL_KINDS] = { "ArenaFloor", "ArenaWall", "ArenaCover", "ArenaDeck", "ArenaRamp", "ArenaTrim", "ArenaFeature", "ArenaGlow", "ArenaPaint" };
  if (count) pmc_finish(c, parent, NAME[look], look != AL_FLOOR && look != AL_PAINT, L->tex);
  else pmc_finish(c, parent, NAME[look], false, -1);
}

/* the gates' nodes (each gate's shapes under a node of its own, for modes_gfx to take away) */
static int NGATE_NODE;
static int GATE_KEY[32];
static cw_node GATE_NODE[32];

cw_node arena_gate_node(int gate) {
  for (int k = 0; k < NGATE_NODE; k++)
    if (GATE_KEY[k] == gate) return GATE_NODE[k];
  return (cw_node){ 0 };
}

void arena_gfx_build(cw_node scene, const arena_rt *rt, uint32_t seed) {
  const arena_def *d = rt->def;
  cw_node root = cw_node_new("Node3D", "Arena");
  cw_node_add_child(scene, root);
  terr_gfx(root, d, seed);
  for (int look = 0; look < AL_KINDS; look++) draw_look(root, rt, look, 0, seed);
  /* the gates: every distinct key, drawn or not (a ghost's glow goes with its gate too) */
  NGATE_NODE = 0;
  for (int i = 0; i < rt->n; i++) {
    int g = rt->prim[i].gate;
    if (!g || cw_node_valid(arena_gate_node(g)) || NGATE_NODE >= 32) continue;
    cw_node n = cw_node_new("Node3D", "ArenaGate");
    cw_node_add_child(root, n);
    GATE_KEY[NGATE_NODE] = g, GATE_NODE[NGATE_NODE++] = n;
    bool has[AL_KINDS] = { 0 };
    for (int j = 0; j < rt->n; j++)
      if (rt->prim[j].gate == g) has[rt->prim[j].look] = true;
    for (int look = 0; look < AL_KINDS; look++)
      if (has[look]) draw_look(n, rt, look, g, seed);
  }
  if (rt->nboost > 0) {
    /* the boost strips: a dark plate and lit chevrons on it pointing the way it throws you */
    pmc *c = pmc_new(48.0f, seed + 971u);
    pcol hot = glow(hex(0xff9a2e), 3.0f), plate = hex(0x2a2a30);
    for (int i = 0; i < rt->nboost; i++) {
      const struct arena_boost_rt *o = &rt->boost[i];
      pm *b = pmc_at(c, o->at);
      pm_push(b);
      pm_move(b, o->at.x, o->at.y, o->at.z);
      pm_roty(b, cw_atan2(o->sn, o->cs));
      pm_layer(b, PL_METAL);
      pm_box(b, o->hw * 2, 0.05f, o->hd * 2, plate, plate);
      pm_layer(b, PL_GLOW);
      int n = (int)(o->hd * 2 / 2.2f);
      for (int k = 0; k < n; k++) {
        float z = -o->hd + 1.1f + (float)k * 2.2f;
        for (int sd = -1; sd <= 1; sd += 2) {
          pm_push(b);
          pm_move(b, (float)sd * o->hw * 0.27f, 0.05f, z);
          pm_roty(b, (float)sd * 0.75f);
          pm_box(b, 0.32f, 0.03f, o->hw * 0.72f, hot, hot);
          pm_pop(b);
        }
      }
      pm_pop(b);
    }
    pmc_finish(c, root, "ArenaBoosts", false, -1);
  }
  if (rt->ntele <= 0) return;
  /* the teleport pads: a dark plate a hand high, a lit rim and a ring of studs round it (modes_gfx.c adds the light over it) */
  pmc *c = pmc_new(48.0f, seed + 977u);
  for (int i = 0; i < rt->ntele; i++) {
    const struct arena_tele_rt *t = &rt->tele[i];
    pcol lit = glow(hex(t->col ? t->col : 0x7fe8ff), 3.2f), plate = hex(0x23262e), edge = hex(0x5d6370);
    pm *b = pmc_at(c, t->at);
    pm_push(b);
    pm_move(b, t->at.x, t->at.y, t->at.z);
    pm_layer(b, PL_METAL);
    pm_cyl(b, t->r + 0.7f, t->r + 0.5f, 0.12f, 28, edge, edge);
    pm_cyl(b, t->r + 0.1f, t->r + 0.1f, 0.14f, 28, plate, plate);
    pm_layer(b, PL_GLOW);
    for (int k = 0; k < 16; k++) {
      float sn, cs;
      cw_sincos((float)k * (CW_TAU / 16.0f), &sn, &cs);
      pm_push(b);
      pm_move(b, sn * (t->r + 0.32f), 0.12f, cs * (t->r + 0.32f));
      pm_roty(b, (float)k * (CW_TAU / 16.0f));
      pm_box(b, 0.7f, 0.05f, 0.16f, lit, lit);
      pm_pop(b);
    }
    pm_cyl(b, t->r * 0.35f, t->r * 0.35f, 0.16f, 20, lit, lit);
    pm_pop(b);
  }
  pmc_finish(c, root, "ArenaPads", false, -1);
}
