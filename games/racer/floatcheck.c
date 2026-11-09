/* floatcheck.c: the debug command `floatcheck`. Every static thing drawn in the world (each instance of an instanced
 * mesh too) is checked for standing on something: its lowest vertices (within 2 cm of its lowest point) are dropped
 * straight down onto whatever is under them (a Terrain3D, the road, the ground mesh, another prop). A thing whose every
 * base point has a gap over the limit under it hovers. A base point inside another solid (a post set into a pad: that
 * solid both above and below it) or under the terrain is grounded.
 *
 *   floatcheck [limit_mm]            the report (JSON): items checked, hovering ones grouped by name, worst first
 *   floatcheck lift <name> <mm>      the control: every thing whose node name holds <name> is raised <mm> for real
 *                                    (drawn so, too), and must then be reported; floatcheck lift <name> 0 puts it back
 *
 * Karts, the item boxes and the ghost move and are left out. What moves on its own (a drone, a flag) is checked where
 * it is at that moment. Dev aid: nothing here changes a race. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cartwheel/debug.h"
#include "cartwheel/terrain.h"
#include "game.h"
#include "world.h"

#define CELL 4.0f
#define BASE_TOL 0.02f /* a base point: within this of the thing's lowest point */
#define TOUCH 0.03f    /* a surface this far above a base point still holds it (the two meet) */
#define ALOFT 3.0f     /* a gap over this is something hung or flying, listed apart */

typedef struct fitem {
  const t3_object *o;
  const t3_geometry *g;
  const char *name;
  t3_mat4 w;
  t3_box3 bb;
  int inst, ntri;
  bool support;
} fitem;

static fitem *IT;
static int nit, cap_it;
static int nfl_terr;
static cw_terrain *TERR[8];

/* the engine's nodes name the objects they own */
#define MAXNAMED 16384
static struct { const t3_object *o; const char *name; } NM[MAXNAMED];
static int nnm;
static void name_nodes(cw_node n) {
  t3_object *o = cw_node3d_object(n);
  if (o && nnm < MAXNAMED) NM[nnm].o = o, NM[nnm].name = cw_node_name(n), nnm++;
  cw_terrain *t = cw_terrain_of(n);
  if (t && nfl_terr < 8) TERR[nfl_terr++] = t;
  for (int i = 0; i < cw_node_child_count(n); i++) name_nodes(cw_node_child(n, i));
}
static const char *name_of(const t3_object *o) {
  for (int i = 0; i < nnm; i++)
    if (NM[i].o == o) return NM[i].name;
  return o->name;
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

static void add_item(const t3_object *o, const t3_geometry *g, const char *name, const t3_mat4 *w, int inst) {
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
  it->o = o, it->g = g, it->name = name ? name : "?", it->w = *w, it->inst = inst;
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
}

static bool skip_name(const char *n) { return n && (strstr(n, "Kart") || strstr(n, "Items") || strstr(n, "Ghost")); }

static void walk(const t3_object *o, const char *name) {
  if (!o->visible) return;
  const char *nm = name_of(o);
  if (nm && nm[0]) name = nm;
  if (skip_name(name)) return;
  if (o->type == T3_MESH) add_item(o, ((const t3_mesh *)o)->geometry, name, &o->matrix_world, -1);
  else if (o->type == T3_INSTANCED_MESH) {
    const t3_instanced_mesh *im = (const t3_instanced_mesh *)o;
    const float *mm = im->instance_matrix ? im->instance_matrix->array : NULL;
    for (int i = 0; mm && i < im->count; i++) {
      t3_mat4 inst, w;
      memcpy(inst.e, mm + i * 16, sizeof inst.e);
      if (inst.e[0] == 0 && inst.e[5] == 0 && inst.e[10] == 0) continue; /* a zeroed instance: hidden (geomclean) */
      t3_mat4_multiply(&w, &o->matrix_world, &inst);
      add_item(o, im->mesh.geometry, name, &w, i);
    }
  }
  for (int i = 0; i < o->child_count; i++) walk(o->children[i], name);
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

/* the highest surface of another thing (or the terrain) under (x, z) at or below y + TOUCH; *inside: the point is in
 * another solid or under the terrain (grounded whatever the gap) */
static float support_at(int self, float x, float y, float z, bool *inside) {
  float best = -1e30f;
  *inside = false;
  for (int i = 0; i < nfl_terr; i++) {
    if (cw_terrain_is_hole(TERR[i], x, z)) continue;
    float h = cw_terrain_height_at(TERR[i], x, z);
    if (h > y) *inside = true;
    if (h <= y + TOUCH && h > best) best = h;
  }
  cellh *c = cell((int)floorf(x / CELL), (int)floorf(z / CELL), false);
  enum { MAXS = 32 };
  int sid[MAXS], sab[MAXS], sn = 0; /* per other item: bit 1 a hit below, bit 2 a hit above */
  for (int r = 0; c && r < c->n; r++) {
    long at = c->start + r;
    int j = (int)REF[at];
    if (j == self || (IT[j].o == IT[self].o && IT[j].inst == IT[self].inst)) continue;
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

typedef struct fgroup { const char *name; int n; float worst; t3_vec3 at; int aloft; } fgroup;
static fgroup GR[256];
static int ngr;
static void group(const char *name, float gap, t3_vec3 at) {
  int k = 0;
  while (k < ngr && strcmp(GR[k].name, name)) k++;
  if (k == ngr) {
    if (ngr == 256) return;
    GR[ngr].name = name, GR[ngr].n = 0, GR[ngr].worst = 0, GR[ngr].aloft = 0, ngr++;
  }
  if (gap > ALOFT) { GR[k].aloft++; if (GR[k].n == 0 && GR[k].aloft == 1) GR[k].at = at; return; }
  GR[k].n++;
  if (gap > GR[k].worst) GR[k].worst = gap, GR[k].at = at;
}
static int by_worst(const void *a, const void *b) {
  const float x = ((const fgroup *)a)->worst, y = ((const fgroup *)b)->worst;
  return (x < y) - (x > y);
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
  cw_node scene = world_scene();
  t3_object *root = cw_node_valid(scene) ? cw_node3d_object(scene) : NULL;
  if (!root) { cw_reply_error(r, "no world"); return; }
  if (argc > 1 && !strcmp(argv[1], "lift")) {
    for (int i = 0; i < nlift; i++) cw_node3d_set_position(LIFT[i].n, LIFT[i].p);
    nlift = 0;
    float mm = argc > 3 ? (float)atof(argv[3]) : 0;
    if (argc > 2 && mm != 0) lift_walk(scene, argv[2], mm / 1000.0f);
    cw_reply_printf(r, "{\"lifted\":%d}", nlift);
    return;
  }
  float limit = (argc > 1 ? (float)atof(argv[1]) : 50.0f) / 1000.0f;
  nit = 0, nnm = 0, nfl_terr = 0, nref = 0, truncated = false, ngr = 0;
  name_nodes(scene);
  t3_object_update_matrix_world(root, true);
  walk(root, NULL);
  /* what can hold things up: every plain mesh, and instanced meshes of a few hundred copies (not the grass) */
  int per_obj = 0;
  for (int i = 0; i < nit; i++) {
    if (i == 0 || IT[i].o != IT[i - 1].o) {
      per_obj = 0;
      for (int j = i; j < nit && IT[j].o == IT[i].o; j++) per_obj++;
    }
    IT[i].support = IT[i].inst < 0 || (per_obj <= 600 && (long)IT[i].ntri * per_obj <= 3000000);
  }
  HT = malloc(sizeof *HT * HSIZE);
  if (!HT) { cw_reply_error(r, "out of memory"); return; }
  for (int i = 0; i < HSIZE; i++) HT[i].n = -1;
  for (int i = 0; i < nit; i++)
    if (IT[i].support)
      for (int t = 0; t < IT[i].ntri; t++) tri_cells(&IT[i], t, 0);
  if (nref > 12000000) truncated = true, nref = 12000000;
  REF = malloc(sizeof *REF * (size_t)(nref + 1)), REF_T = malloc(sizeof *REF_T * (size_t)(nref + 1));
  if (!REF || !REF_T) { free(HT), free(REF), free(REF_T), HT = NULL, REF = NULL, REF_T = NULL; cw_reply_error(r, "out of memory (%ld refs)", nref); return; }
  long s = 0;
  for (int i = 0; i < HSIZE; i++)
    if (HT[i].n >= 0) {
      if (s + HT[i].n > nref) HT[i].n = (int)(nref - s > 0 ? nref - s : 0);
      HT[i].start = (int)s, s += HT[i].n;
    }
  for (int i = 0; i < nit; i++)
    if (IT[i].support)
      for (int t = 0; t < IT[i].ntri; t++) tri_cells(&IT[i], t, 1);
  int checked = 0, hover = 0, aloft = 0;
  for (int i = 0; i < nit; i++) {
    const fitem *it = &IT[i];
    const t3_attribute *pa = pos_of(it->g);
    if ((long)pa->count > 400000) continue; /* a ground of its own (the road, the earth) */
    if (it->bb.max.x - it->bb.min.x > 300 || it->bb.max.z - it->bb.min.z > 300) continue; /* the sky, a backdrop */
    float lo = 1e30f;
    for (int k = 0; k < pa->count; k++) { t3_vec3 p = vtx(it, k); if (p.y < lo) lo = p.y; }
    int nb = 0;
    for (int k = 0; k < pa->count; k++) if (vtx(it, k).y < lo + BASE_TOL) nb++;
    int stride = nb > 16 ? nb / 16 : 1, seen = 0;
    float gap = 1e30f;
    t3_vec3 at = { 0 };
    bool held = false;
    for (int k = 0; k < pa->count && !held; k++) {
      t3_vec3 p = vtx(it, k);
      if (p.y >= lo + BASE_TOL) continue;
      if (seen++ % stride) continue;
      bool inside;
      float sy = support_at(i, p.x, p.y, p.z, &inside);
      float g = inside ? 0 : p.y - sy;
      if (g < gap) gap = g, at = p;
      if (gap <= limit) held = true;
    }
    checked++;
    if (held) continue;
    if (gap > ALOFT) aloft++; else hover++;
    group(it->name, gap, at);
    if (gap <= ALOFT) cw_info("floatcheck: %-28s inst %d hovers %.0f mm at %.2f,%.2f,%.2f", it->name, it->inst, gap * 1000, at.x, at.y, at.z);
  }
  free(HT), free(REF), free(REF_T), HT = NULL, REF = NULL, REF_T = NULL;
  qsort(GR, (size_t)ngr, sizeof GR[0], by_worst);
  static char buf[24576];
  int o = snprintf(buf, sizeof buf, "{\"items\":%d,\"checked\":%d,\"terrains\":%d,\"refs\":%ld,\"truncated\":%s,\"limit_mm\":%.0f,\"hover\":%d,\"aloft\":%d,\"groups\":[",
                   nit, checked, nfl_terr, nref, truncated ? "true" : "false", limit * 1000, hover, aloft);
  for (int k = 0; k < ngr && o < (int)sizeof buf - 256; k++)
    o += snprintf(buf + o, sizeof buf - (size_t)o, "%s{\"name\":\"%s\",\"n\":%d,\"worst_mm\":%.0f,\"aloft\":%d,\"at\":[%.2f,%.2f,%.2f]}", k ? "," : "", GR[k].name, GR[k].n,
                  GR[k].worst * 1000, GR[k].aloft, GR[k].at.x, GR[k].at.y, GR[k].at.z);
  snprintf(buf + o, sizeof buf - (size_t)o, "]}");
  cw_reply_raw(r, buf);
}

void floatcheck_debug_init(void) {
  cw_debug_command("floatcheck", "floatcheck [limit_mm]: every static thing in the world against what is under it (hovering ones by name); floatcheck lift <name> <mm>: raise those nodes for real (the control)", cmd_floatcheck);
}
