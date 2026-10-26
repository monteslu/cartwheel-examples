/* racer's battle arenas: the layout in the world, its physics shapes and its drawn geometry (see arena.h). */
#include <math.h>
#include <string.h>

#include "arena.h"
#include "cartwheel/math.h"
#include "props.h"

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

void arena_rt_build(arena_rt *rt, const arena_def *def) {
  memset(rt, 0, sizeof *rt);
  rt->def = def;
  rt->half_x = def->half_x, rt->half_z = def->half_z;
  rt->centre = (t3_vec3){ 0, 0, ARENA_CZ };
  float hx = def->half_x, hz = def->half_z, wt = 3.0f, wh = def->wall_h > 0 ? def->wall_h : 3.0f;
  /* the floor, a slab under y = 0 reaching under the walls, and the four walls round it */
  add(rt, (arena_prim){ AP_BOX, AL_FLOOR, 0, 0, -2.0f, hx * 2 + wt * 2, 2.0f, hz * 2 + wt * 2, 0, false });
  add(rt, (arena_prim){ AP_BOX, AL_WALL, 0, hz + wt * 0.5f, 0, hx * 2 + wt * 2, wh, wt, 0, false });
  add(rt, (arena_prim){ AP_BOX, AL_WALL, 0, -hz - wt * 0.5f, 0, hx * 2 + wt * 2, wh, wt, 0, false });
  add(rt, (arena_prim){ AP_BOX, AL_WALL, hx + wt * 0.5f, 0, 0, wt, wh, hz * 2, 0, false });
  add(rt, (arena_prim){ AP_BOX, AL_WALL, -hx - wt * 0.5f, 0, 0, wt, wh, hz * 2, 0, false });
  for (int i = 0; i < def->nprims; i++) add(rt, def->prims[i]);
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

float arena_level(const arena_rt *rt, float x, float z) {
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
    if (top > best) best = top;
  }
  return best;
}

void arena_phys_build(const arena_rt *rt, cw_body3d b, int track_layer, int wall_layer) {
  for (int i = 0; i < rt->n; i++) {
    const arena_prim *p = &rt->prim[i];
    if (p->ghost) continue;
    bool floorish = p->look == AL_FLOOR || p->look == AL_DECK || p->look == AL_RAMP;
    cw_shape3d_def sd = cw_shape3d_default_def();
    sd.layer = floorish ? track_layer : wall_layer;
    sd.material.friction = floorish ? 0.9f : 0.0f;
    sd.material.restitution = 0.0f;
    if (p->kind == AP_BOX) {
      cw_xform3 x = { { p->x, p->y + p->h * 0.5f, p->z }, yaw_q(rt->sn[i], rt->cs[i]) };
      cw_shape3d_box(b, &sd, (t3_vec3){ p->w * 0.5f, p->h * 0.5f, p->d * 0.5f }, x);
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

void arena_gfx_build(cw_node scene, const arena_rt *rt, uint32_t seed) {
  const arena_def *d = rt->def;
  cw_node root = cw_node_new("Node3D", "Arena");
  cw_node_add_child(scene, root);
  for (int look = 0; look < AL_KINDS; look++) {
    const arena_look *L = &d->look[look];
    pmc *c = pmc_new(look == AL_FLOOR ? 30.0f : 48.0f, seed + (uint32_t)look * 31u);
    if (L->tex_m > 0) pmc_texture_metres(c, L->tex_m);
    int count = 0;
    for (int i = 0; i < rt->n; i++) {
      const arena_prim *p = &rt->prim[i];
      if (p->look != look) continue;
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
    if (count) pmc_finish(c, root, NAME[look], look != AL_FLOOR && look != AL_PAINT, L->tex);
    else pmc_finish(c, root, NAME[look], false, -1);
  }
}
