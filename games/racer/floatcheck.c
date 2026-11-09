/* floatcheck.c: the debug command `floatcheck`. Every static prop drawn in the world (each instance of an instanced
 * mesh too; the parts of one prop, under the same engine node with their boxes touching, as one) is checked for
 * standing on something: its lowest points (within 2 cm of its lowest point) are dropped straight down onto whatever
 * is under them (a Terrain3D, the road, the ground mesh, another prop). A prop whose every base point has a gap over
 * the limit under it hovers. A base point inside another solid (a post set into a pad: that solid both above and below
 * it) or under the terrain is grounded.
 *
 *   floatcheck start [limit_mm]      starts the check; it runs a slice of work each frame (a whole map at once held the
 *                                    frame, and romdev with it, for minutes)
 *   floatcheck                       {"done":false,"phase":n} while it runs, then the report (JSON): props checked,
 *                                    hovering ones grouped by name, worst first
 *   floatcheck lift <name> <mm>      the control: every node whose name holds <name> is raised <mm> for real (drawn
 *                                    so, too), and must then be reported; floatcheck lift <name> 0 puts it back
 *
 * Karts, the item boxes and the ghost move and are left out, and so are scatter layers of thousands of copies (grass,
 * rocks and trees the terrain places on itself). What moves on its own (a drone, a flag) is checked where it is at
 * that moment. Dev aid: nothing here changes a race. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cartwheel/debug.h"
#include "cartwheel/terrain.h"
#include "game.h"
#include "world.h"

#define CELL 4.0f
#define BASE_TOL 0.02f  /* a base point: within this of the prop's lowest point */
#define TOUCH 0.03f     /* a surface this far above a base point still holds it (the two meet) */
#define ALOFT 3.0f      /* a gap over this is something hung or flying, listed apart */
#define SCATTER 2000    /* an instanced mesh of more copies than this is a scatter layer: not checked, holds nothing up */
#define BUDGET 1500000  /* work (vertices, triangles, tests) per frame */
#define NEAR_ROAD 25.0f /* metres from the road's edge: what the race camera passes close enough to see a gap under */
#define NEAR_GAP 0.6f   /* a gap up to this is "a bit above": a prop meant to stand, standing short (more is a part hung or held) */
float road_gap(const track *t, float x, float z, float *road_y);

typedef struct fitem {
  const t3_object *o;
  const t3_geometry *g;
  const char *name;
  const t3_object *owner; /* the innermost engine node's object over it: the prop as the game placed it */
  int root;               /* union-find: the prop this part belongs to */
  t3_mat4 w;
  t3_box3 bb;
  int inst, ntri;
  bool support, measure;
} fitem;
/* the world matrix of item i now (its object's, times its instance's) */
static void item_world(const fitem *it, t3_mat4 *out) {
  if (it->inst < 0) { *out = it->o->matrix_world; return; }
  const t3_instanced_mesh *im = (const t3_instanced_mesh *)it->o;
  if (!im->instance_matrix || it->inst >= im->count) { memset(out, 0, sizeof *out); return; }
  t3_mat4 inst;
  memcpy(inst.e, (const float *)im->instance_matrix->array + it->inst * 16, sizeof inst.e);
  t3_mat4_multiply(out, &it->o->matrix_world, &inst);
}

static fitem *IT;
static int nit, cap_it;
static int nterr;
static cw_terrain *TERR[8];

/* the engine's nodes by the object they own (a hash: a linear search per object took minutes on a big map) */
#define NMBITS 15
#define NMSIZE (1 << NMBITS)
static struct { const t3_object *o; const char *name; } NM[NMSIZE];
static uint32_t phash(const void *p) { uintptr_t v = (uintptr_t)p; v ^= v >> 13; v *= 0x9E3779B1u; return (uint32_t)(v >> 7) & (NMSIZE - 1); }
static void nm_put(const t3_object *o, const char *name) {
  for (uint32_t h = phash(o), k = 0; k < NMSIZE; h = (h + 1) & (NMSIZE - 1), k++)
    if (!NM[h].o || NM[h].o == o) { NM[h].o = o, NM[h].name = name; return; }
}
static int node_of(const t3_object *o) {
  for (uint32_t h = phash(o), k = 0; k < NMSIZE; h = (h + 1) & (NMSIZE - 1), k++) {
    if (!NM[h].o) return -1;
    if (NM[h].o == o) return (int)h;
  }
  return -1;
}
static const char *name_of(const t3_object *o) { int i = node_of(o); return i >= 0 ? NM[i].name : o->name; }
static void name_nodes(cw_node n) {
  t3_object *o = cw_node3d_object(n);
  if (o) nm_put(o, cw_node_name(n));
  cw_terrain *t = cw_terrain_of(n);
  if (t && nterr < 8) TERR[nterr++] = t;
  for (int i = 0; i < cw_node_child_count(n); i++) name_nodes(cw_node_child(n, i));
}

static t3_vec3 xf(const t3_mat4 *m, const float *v) {
  const float *e = m->e;
  return (t3_vec3){ e[0] * v[0] + e[4] * v[1] + e[8] * v[2] + e[12], e[1] * v[0] + e[5] * v[1] + e[9] * v[2] + e[13], e[2] * v[0] + e[6] * v[1] + e[10] * v[2] + e[14] };
}
static int idx_at(const t3_geometry *g, int q) {
  return !g->index ? q : g->index->type == T3_UINT16 ? ((const uint16_t *)g->index->array)[q] : (int)((const uint32_t *)g->index->array)[q];
}
static const t3_attribute *pos_of(const t3_geometry *g) {
  const t3_attribute *pa = g ? g->attributes[T3_ATTR_POSITION] : NULL;
  return pa && pa->type == T3_FLOAT32 && pa->item_size >= 3 ? pa : NULL;
}
static t3_vec3 vtx(const fitem *it, int k) {
  const t3_attribute *pa = pos_of(it->g);
  return xf(&it->w, (const float *)pa->array + (size_t)k * (size_t)pa->item_size);
}

static const t3_object *cur_owner;
static void add_item(const t3_object *o, const t3_geometry *g, const char *name, const t3_mat4 *w, int inst, int copies) {
  const t3_attribute *pa = pos_of(g);
  if (!pa || !pa->count) return;
  if (nit == cap_it) {
    int nc = cap_it ? cap_it * 2 : 4096;
    fitem *n = realloc(IT, (size_t)nc * sizeof *n);
    if (!n) return;
    IT = n, cap_it = nc;
  }
  fitem *it = &IT[nit++];
  memset(it, 0, sizeof *it);
  it->o = o, it->g = g, it->name = name ? name : "?", it->w = *w, it->inst = inst, it->owner = cur_owner, it->root = nit - 1;
  it->ntri = g->index ? g->index->count / 3 : pa->count / 3;
  if (!g->has_bounding_box) t3_geometry_compute_bounding_box((t3_geometry *)g);
  t3_box3 b = g->bounding_box;
  it->bb.min = (t3_vec3){ 1e30f, 1e30f, 1e30f }, it->bb.max = (t3_vec3){ -1e30f, -1e30f, -1e30f };
  for (int c = 0; c < 8; c++) {
    float p[3] = { c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z };
    t3_vec3 q = xf(w, p);
    it->bb.min.x = fminf(it->bb.min.x, q.x), it->bb.min.y = fminf(it->bb.min.y, q.y), it->bb.min.z = fminf(it->bb.min.z, q.z);
    it->bb.max.x = fmaxf(it->bb.max.x, q.x), it->bb.max.y = fmaxf(it->bb.max.y, q.y), it->bb.max.z = fmaxf(it->bb.max.z, q.z);
  }
  /* what can hold things up: every plain mesh, and instanced meshes of a few hundred copies (not the grass) */
  it->support = inst < 0 || (copies <= 600 && (long)it->ntri * copies <= 3000000);
  /* what is checked: not a ground of its own (the road, the earth), the sky or a backdrop, or a scatter layer */
  it->measure = copies <= SCATTER && pa->count <= 400000 && it->bb.max.x - it->bb.min.x <= 300 && it->bb.max.z - it->bb.min.z <= 300;
}

static bool skip_name(const char *n) { return n && (strstr(n, "Kart") || strstr(n, "Items") || strstr(n, "Ghost")); }

static void walk(const t3_object *o, const char *name) {
  if (!o->visible) return;
  const t3_object *keep = cur_owner;
  if (node_of(o) >= 0) cur_owner = o;
  const char *nm = name_of(o);
  if (nm && nm[0]) name = nm;
  if (skip_name(name)) { cur_owner = keep; return; }
  if (o->type == T3_MESH) add_item(o, ((const t3_mesh *)o)->geometry, name, &o->matrix_world, -1, 1);
  else if (o->type == T3_INSTANCED_MESH) {
    const t3_instanced_mesh *im = (const t3_instanced_mesh *)o;
    const float *mm = im->instance_matrix ? im->instance_matrix->array : NULL;
    for (int i = 0; mm && i < im->count; i++) {
      t3_mat4 inst, w;
      memcpy(inst.e, mm + i * 16, sizeof inst.e);
      if (inst.e[0] == 0 && inst.e[5] == 0 && inst.e[10] == 0) continue; /* a zeroed instance: hidden (geomclean) */
      t3_mat4_multiply(&w, &o->matrix_world, &inst);
      add_item(o, im->mesh.geometry, name, &w, i, im->count);
    }
  }
  for (int i = 0; i < o->child_count; i++) walk(o->children[i], name);
  cur_owner = keep;
}

/* the parts of one prop: under the same engine node, the same instance, their boxes touching */
static int find(int i) { while (IT[i].root != i) IT[i].root = IT[IT[i].root].root, i = IT[i].root; return i; }
static bool touch(const t3_box3 *a, const t3_box3 *b) {
  const float e = 0.05f;
  return a->min.x <= b->max.x + e && b->min.x <= a->max.x + e && a->min.y <= b->max.y + e && b->min.y <= a->max.y + e && a->min.z <= b->max.z + e && b->min.z <= a->max.z + e;
}
static int *ORD;
static int by_owner(const void *a, const void *b) {
  const fitem *x = &IT[*(const int *)a], *y = &IT[*(const int *)b];
  if (x->owner != y->owner) return x->owner < y->owner ? -1 : 1;
  if (x->inst != y->inst) return x->inst < y->inst ? -1 : 1;
  return *(const int *)a - *(const int *)b;
}

/* ── the triangles of everything that can hold something up, in a grid of CELL metre cells ──────────────────── */
typedef struct { int cx, cz, start, n, fill; } cellh;
#define HBITS 18
#define HSIZE (1 << HBITS)
static cellh *HT;
static uint32_t *REF; /* each ref: the item (REF) and its triangle (REF_T) */
static int *REF_T;
static long nref;
static bool truncated;

static uint32_t hkey(int cx, int cz) { return ((uint32_t)cx * 73856093u ^ (uint32_t)cz * 19349663u) & (HSIZE - 1); }
static cellh *cell(int cx, int cz, bool make) {
  for (uint32_t h = hkey(cx, cz), k = 0; k < HSIZE; h = (h + 1) & (HSIZE - 1), k++) {
    cellh *c = &HT[h];
    if (c->n < 0) {
      if (!make) return NULL;
      c->cx = cx, c->cz = cz, c->n = 0, c->start = 0, c->fill = 0;
      return c;
    }
    if (c->cx == cx && c->cz == cz) return c;
  }
  return NULL;
}
static void tri_cells(const fitem *it, int t, int pass) {
  t3_vec3 a = vtx(it, idx_at(it->g, t * 3)), b = vtx(it, idx_at(it->g, t * 3 + 1)), c = vtx(it, idx_at(it->g, t * 3 + 2));
  int x0 = (int)floorf(fminf(a.x, fminf(b.x, c.x)) / CELL), x1 = (int)floorf(fmaxf(a.x, fmaxf(b.x, c.x)) / CELL);
  int z0 = (int)floorf(fminf(a.z, fminf(b.z, c.z)) / CELL), z1 = (int)floorf(fmaxf(a.z, fmaxf(b.z, c.z)) / CELL);
  if ((long)(x1 - x0 + 1) * (z1 - z0 + 1) > 4096) return; /* a sheet hundreds of metres across: the sky, a backdrop */
  for (int cx = x0; cx <= x1; cx++)
    for (int cz = z0; cz <= z1; cz++) {
      cellh *h = cell(cx, cz, pass == 0);
      if (!h) continue;
      if (pass == 0) { h->n++, nref++; continue; }
      if (h->fill >= h->n) continue; /* past the cap (truncated) */
      long at = h->start + h->fill++;
      REF[at] = (uint32_t)(it - IT), REF_T[at] = t;
    }
}

/* the highest surface of another prop (or the terrain) under (x, z) at or below y + TOUCH; *inside: the point is in
 * another solid or under the terrain (grounded whatever the gap) */
static long work;
static float support_at(int self, float x, float y, float z, bool *inside) {
  float best = -1e30f;
  *inside = false;
  for (int i = 0; i < nterr; i++) {
    if (cw_terrain_is_hole(TERR[i], x, z)) continue;
    float h = cw_terrain_height_at(TERR[i], x, z);
    if (h > y) *inside = true;
    if (h <= y + TOUCH && h > best) best = h;
  }
  cellh *c = cell((int)floorf(x / CELL), (int)floorf(z / CELL), false);
  enum { MAXS = 32 };
  int sid[MAXS], sab[MAXS], sn = 0; /* per other item: bit 1 a hit below, bit 2 a hit above */
  int me = find(self);
  for (int r = 0; c && r < c->n; r++) {
    long at = c->start + r;
    int j = (int)REF[at];
    work++;
    if (j == self || find(j) == me) continue; /* the prop's own parts never hold it up */
    const fitem *it = &IT[j];
    int t = REF_T[at];
    t3_vec3 a = vtx(it, idx_at(it->g, t * 3)), b = vtx(it, idx_at(it->g, t * 3 + 1)), d = vtx(it, idx_at(it->g, t * 3 + 2));
    float den = (b.z - d.z) * (a.x - d.x) + (d.x - b.x) * (a.z - d.z);
    if (fabsf(den) < 1e-9f) continue;
    float u = ((b.z - d.z) * (x - d.x) + (d.x - b.x) * (z - d.z)) / den, v = ((d.z - a.z) * (x - d.x) + (a.x - d.x) * (z - d.z)) / den, w = 1 - u - v;
    if (u < -1e-4f || v < -1e-4f || w < -1e-4f) continue;
    float hy = u * a.y + v * b.y + w * d.y;
    int k = 0;
    while (k < sn && sid[k] != j) k++;
    if (k == sn && sn < MAXS) sid[sn] = j, sab[sn] = 0, sn++;
    if (hy <= y + TOUCH) {
      if (hy > best) best = hy;
      if (k < sn) sab[k] |= 1;
    } else if (hy < y + 4.0f && k < sn) sab[k] |= 2;
  }
  for (int k = 0; k < sn; k++)
    if (sab[k] == 3) *inside = true;
  return best;
}

typedef struct fgroup { const char *name; int n; float worst; t3_vec3 at; int aloft; int near; float nworst; t3_vec3 nat; } fgroup;
static fgroup GR[256];
static int ngr;
static void group(const char *name, float gap, t3_vec3 at, bool near) {
  int k = 0;
  while (k < ngr && strcmp(GR[k].name, name)) k++;
  if (k == ngr) {
    if (ngr == 256) return;
    GR[ngr].name = name, GR[ngr].n = 0, GR[ngr].worst = 0, GR[ngr].aloft = 0, GR[ngr].near = 0, GR[ngr].nworst = 0, ngr++;
  }
  if (gap > ALOFT) { GR[k].aloft++; if (GR[k].n == 0 && GR[k].aloft == 1) GR[k].at = at; return; }
  GR[k].n++;
  if (near) { GR[k].near++; if (gap > GR[k].nworst) GR[k].nworst = gap, GR[k].nat = at; }
  if (gap > GR[k].worst) GR[k].worst = gap, GR[k].at = at;
}
static int by_worst(const void *a, const void *b) {
  const float x = ((const fgroup *)a)->worst, y = ((const fgroup *)b)->worst;
  return (x < y) - (x > y);
}

/* ── the job: one phase after another, a slice of each a frame ─────────────────────────────────────────────── */
enum { F_IDLE, F_COUNT, F_FILL, F_JOIN, F_LOW, F_MEASURE, F_DONE };
static struct {
  int phase, i, t, k;
  float limit;
  bool *skip;
  float *plo, *pgap;
  t3_vec3 *pat;
  int *pseen;
  int checked, hover, aloft, near, moving;
  char out[24576];
} J;

static void job_free(void) {
  free(HT), free(REF), free(REF_T), free(J.skip), free(J.plo), free(J.pgap), free(J.pat), free(J.pseen), free(ORD);
  HT = NULL, REF = NULL, REF_T = NULL, J.skip = NULL, J.plo = J.pgap = NULL, J.pat = NULL, J.pseen = NULL, ORD = NULL;
}
static void job_fail(const char *why) {
  job_free();
  snprintf(J.out, sizeof J.out, "{\"done\":true,\"error\":\"%s\"}", why);
  J.phase = F_DONE;
}

static bool job_start(float limit) {
  job_free();
  cw_node scene = world_scene();
  t3_object *root = cw_node_valid(scene) ? cw_node3d_object(scene) : NULL;
  if (!root) return false;
  memset(NM, 0, sizeof NM);
  nit = 0, nterr = 0, nref = 0, truncated = false, ngr = 0, cur_owner = NULL;
  J.limit = limit, J.checked = J.hover = J.aloft = J.near = J.moving = 0;
  name_nodes(scene);
  t3_object_update_matrix_world(root, true);
  walk(root, NULL);
  HT = malloc(sizeof *HT * HSIZE);
  if (!HT) { job_fail("out of memory"); return true; }
  for (int i = 0; i < HSIZE; i++) HT[i].n = -1;
  J.phase = F_COUNT, J.i = J.t = 0;
  return true;
}

/* a slice of the job; true when done */
static bool job_step(void) {
  work = 0;
  while (work < BUDGET && J.phase != F_DONE && J.phase != F_IDLE) {
    switch (J.phase) {
    case F_COUNT:
    case F_FILL: {
      int pass = J.phase == F_COUNT ? 0 : 1;
      if (J.i >= nit) {
        if (pass == 0) {
          if (nref > 12000000) truncated = true, nref = 12000000;
          REF = malloc(sizeof *REF * (size_t)(nref + 1)), REF_T = malloc(sizeof *REF_T * (size_t)(nref + 1));
          if (!REF || !REF_T) { job_fail("out of memory"); break; }
          long s = 0;
          for (int i = 0; i < HSIZE; i++)
            if (HT[i].n >= 0) {
              if (s + HT[i].n > nref) HT[i].n = (int)(nref - s > 0 ? nref - s : 0);
              HT[i].start = (int)s, s += HT[i].n;
            }
          J.phase = F_FILL;
        } else J.phase = F_JOIN;
        J.i = J.t = 0;
        break;
      }
      if (!IT[J.i].support || J.t >= IT[J.i].ntri) { J.i++, J.t = 0; continue; }
      tri_cells(&IT[J.i], J.t++, pass);
      work += 4;
      break;
    }
    case F_JOIN: {
      ORD = malloc(sizeof *ORD * (size_t)(nit + 1));
      if (!ORD) { job_fail("out of memory"); break; }
      for (int i = 0; i < nit; i++) ORD[i] = i;
      qsort(ORD, (size_t)nit, sizeof *ORD, by_owner);
      for (int a = 0; a < nit;) {
        int b = a;
        while (b < nit && IT[ORD[b]].owner == IT[ORD[a]].owner && IT[ORD[b]].inst == IT[ORD[a]].inst) b++;
        if (b - a <= 1500)
          for (int i = a; i < b; i++)
            for (int j = i + 1; j < b; j++)
              if (touch(&IT[ORD[i]].bb, &IT[ORD[j]].bb)) { int ri = find(ORD[i]), rj = find(ORD[j]); if (ri != rj) IT[rj].root = ri; }
        work += (long)(b - a) * (b - a) / 4;
        a = b;
      }
      free(ORD), ORD = NULL;
      J.skip = calloc((size_t)nit + 1, 1), J.plo = malloc(sizeof *J.plo * ((size_t)nit + 1)), J.pgap = malloc(sizeof *J.pgap * ((size_t)nit + 1));
      J.pat = malloc(sizeof *J.pat * ((size_t)nit + 1)), J.pseen = calloc((size_t)nit + 1, sizeof *J.pseen);
      if (!J.skip || !J.plo || !J.pgap || !J.pat || !J.pseen) { job_fail("out of memory"); break; }
      for (int i = 0; i < nit; i++) J.plo[i] = 1e30f, J.pgap[i] = 1e30f;
      for (int i = 0; i < nit; i++) if (!IT[i].measure) J.skip[find(i)] = true;
      J.phase = F_LOW, J.i = J.k = 0;
      break;
    }
    case F_LOW: { /* the exact lowest point of each prop */
      if (J.i >= nit) { J.phase = F_MEASURE, J.i = J.k = 0; break; }
      int p = find(J.i);
      const t3_attribute *pa = pos_of(IT[J.i].g);
      if (J.skip[p] || J.k >= pa->count) { J.i++, J.k = 0; continue; }
      float y = vtx(&IT[J.i], J.k++).y;
      if (y < J.plo[p]) J.plo[p] = y;
      work++;
      break;
    }
    case F_MEASURE: { /* its base points dropped onto what is under them: one that touches holds it */
      if (J.i >= nit) { J.phase = F_DONE; break; }
      int p = find(J.i);
      const t3_attribute *pa = pos_of(IT[J.i].g);
      if (J.skip[p] || J.pgap[p] <= J.limit || J.pseen[p] > 48 || J.k >= pa->count) { J.i++, J.k = 0; continue; }
      t3_vec3 q = vtx(&IT[J.i], J.k++);
      work++;
      if (q.y >= J.plo[p] + BASE_TOL) break;
      J.pseen[p]++;
      bool inside;
      float sy = support_at(J.i, q.x, q.y, q.z, &inside);
      float g = inside ? 0 : q.y - sy;
      if (g < J.pgap[p]) J.pgap[p] = g, J.pat[p] = q;
      break;
    }
    }
  }
  if (J.phase != F_DONE || J.out[0]) return J.phase == F_DONE;
  /* the report. What moved while the check ran (a bouncing crowd, a pump's beam, a drone) is left out: it was
   * measured at one moment of its motion */
  cw_node scene = world_scene();
  t3_object *sr = cw_node_valid(scene) ? cw_node3d_object(scene) : NULL;
  if (sr) t3_object_update_matrix_world(sr, true);
  for (int i = 0; i < nit; i++) {
    t3_mat4 now;
    item_world(&IT[i], &now);
    for (int e = 0; e < 16; e++)
      if (fabsf(now.e[e] - IT[i].w.e[e]) > 1e-4f) { J.skip[find(i)] = true, J.moving++; break; }
  }
  for (int i = 0; i < nit; i++) {
    if (find(i) != i || J.skip[i]) continue;
    J.checked++;
    float gap = J.pgap[i];
    if (gap <= J.limit) continue;
    if (gap > ALOFT) J.aloft++; else J.hover++;
    const track *t = world_track();
    bool near = gap <= NEAR_GAP && (!t || road_gap(t, J.pat[i].x, J.pat[i].z, NULL) < NEAR_ROAD);
    if (near) J.near++;
    const char *nm = IT[i].owner ? name_of(IT[i].owner) : IT[i].name;
    group(nm && nm[0] ? nm : IT[i].name, gap, J.pat[i], near);
    if (gap <= ALOFT) cw_info("floatcheck: %-28s (%s) inst %d hovers %.0f mm at %.2f,%.2f,%.2f", nm ? nm : "?", IT[i].name, IT[i].inst, gap * 1000, J.pat[i].x, J.pat[i].y, J.pat[i].z);
  }
  qsort(GR, (size_t)ngr, sizeof GR[0], by_worst);
  int o = snprintf(J.out, sizeof J.out, "{\"done\":true,\"items\":%d,\"checked\":%d,\"terrains\":%d,\"refs\":%ld,\"truncated\":%s,\"limit_mm\":%.0f,\"hover\":%d,\"near\":%d,\"aloft\":%d,\"moving\":%d,\"groups\":[",
                   nit, J.checked, nterr, nref, truncated ? "true" : "false", J.limit * 1000, J.hover, J.near, J.aloft, J.moving);
  for (int k = 0; k < ngr && o < (int)sizeof J.out - 256; k++)
    o += snprintf(J.out + o, sizeof J.out - (size_t)o, "%s{\"name\":\"%s\",\"n\":%d,\"worst_mm\":%.0f,\"aloft\":%d,\"near\":%d,\"near_mm\":%.0f,\"at\":[%.2f,%.2f,%.2f],\"near_at\":[%.2f,%.2f,%.2f]}", k ? "," : "", GR[k].name, GR[k].n,
                  GR[k].worst * 1000, GR[k].aloft, GR[k].near, GR[k].nworst * 1000, GR[k].at.x, GR[k].at.y, GR[k].at.z, GR[k].nat.x, GR[k].nat.y, GR[k].nat.z);
  snprintf(J.out + o, sizeof J.out - (size_t)o, "]}");
  job_free();
  return true;
}

void floatcheck_frame(void) {
  if (J.phase != F_IDLE && J.phase != F_DONE) job_step();
}

/* the lift control: objects raised for real (their node's position), remembered so lift 0 puts them back */
static struct { cw_node n; t3_vec3 p; } LIFT[512];
static int nlift;
static void lift_walk(cw_node n, const char *sub, float dy) {
  const char *nm = cw_node_name(n);
  if (nm && strstr(nm, sub) && cw_node3d_object(n) && nlift < 512) {
    t3_vec3 p = cw_node3d_position(n);
    LIFT[nlift].n = n, LIFT[nlift].p = p, nlift++;
    cw_node3d_set_position(n, (t3_vec3){ p.x, p.y + dy, p.z });
    return;
  }
  for (int i = 0; i < cw_node_child_count(n); i++) lift_walk(cw_node_child(n, i), sub, dy);
}

static void cmd_floatcheck(int argc, const char **argv, cw_reply *r) {
  if (argc > 1 && !strcmp(argv[1], "lift")) {
    for (int i = 0; i < nlift; i++) cw_node3d_set_position(LIFT[i].n, LIFT[i].p);
    nlift = 0;
    float mm = argc > 3 ? (float)atof(argv[3]) : 0;
    cw_node scene = world_scene();
    if (argc > 2 && mm != 0 && cw_node_valid(scene)) lift_walk(scene, argv[2], mm / 1000.0f);
    cw_reply_printf(r, "{\"lifted\":%d}", nlift);
    return;
  }
  if (argc > 1 && !strcmp(argv[1], "start")) {
    J.out[0] = 0;
    if (!job_start((argc > 2 ? (float)atof(argv[2]) : 50.0f) / 1000.0f)) { cw_reply_error(r, "no world"); return; }
    cw_reply_printf(r, "{\"started\":true,\"items\":%d}", nit);
    return;
  }
  if (J.phase == F_DONE) cw_reply_raw(r, J.out);
  else cw_reply_printf(r, "{\"done\":false,\"phase\":%d,\"at\":%d,\"of\":%d}", J.phase, J.i, nit);
}

void floatcheck_debug_init(void) {
  cw_debug_command("floatcheck", "floatcheck start [limit_mm] | floatcheck: the report once done (every static prop in the world against what is under it, hovering ones by name); floatcheck lift <name> <mm>: raise those nodes for real (the control)", cmd_floatcheck);
}
