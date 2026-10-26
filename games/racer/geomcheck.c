/* geomcheck.c: dev aid RACER_GEOMCHECK=1. Once the map is up, every visible mesh in the world (instances too) and the
 * collision walls are checked against the road: a vertex or a triangle's middle that stands inside the lanes (more than
 * half a metre in from the edge) between 0.3 and 2.2 m above the deck is something a kart drives into. The log lists
 * each object with where along the road it intrudes, worst first. Karts and the item boxes are meant to be there. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "world.h"

const track_phys *sim_wall_phys(void);

#define MAXREP 256
static struct rep { char name[48]; int n; float s0, s1, lat, h; t3_vec3 p; } R[MAXREP];
static int nrep, hint = -1, hint_main = -1;

static void hit1(const char *name, const track *t, t3_vec3 p, bool main_only);
/* each point against the road it is nearest and against the main road alone: where a shortcut forks off, what lines
 * the branch's edge stands inside the main road's lanes, and the nearest road there is the branch */
static void hit(const char *name, const track *t, t3_vec3 p) { hit1(name, t, p, false), hit1(name, t, p, true); }
static void hit1(const char *name, const track *t, t3_vec3 p, bool main_only) {
  track_loc l;
  if (main_only) {
    if (t->nbranch == 0) return;
    track_locate_main(t, p, -1, &l);
  } else track_locate(t, p, hint, &l), hint = l.i;
  if (l.i < 0) return;
  const tsample *a = &t->s[l.i];
  if (a->flags & TF_GAP) return;
  if (fabsf(l.lat) > a->hw - 0.5f || l.height < 0.3f || l.height > 2.2f) return;
  struct rep *r = NULL;
  for (int i = 0; i < nrep; i++)
    if (!strcmp(R[i].name, name) && fabsf(R[i].s1 - l.ms) < 40) r = &R[i];
  if (!r) {
    if (nrep >= MAXREP) return;
    r = &R[nrep++];
    snprintf(r->name, sizeof r->name, "%s", name);
    r->s0 = r->s1 = l.ms, r->lat = l.lat, r->h = l.height, r->p = p;
  }
  r->n++;
  if (l.ms < r->s0) r->s0 = l.ms;
  if (l.ms > r->s1) r->s1 = l.ms;
}

/* the cleaner's test, stricter than the report: well inside the lanes (0.9 m in from the edge), on any path */
static bool in_lane1(const track *t, t3_vec3 p, bool main_only) {
  track_loc l;
  if (main_only) {
    if (t->nbranch == 0) return false;
    track_locate_main(t, p, hint_main, &l);
    hint_main = l.i;
  } else track_locate(t, p, hint, &l), hint = l.i;
  if (l.i < 0) return false;
  const tsample *a = &t->s[l.i];
  if (a->flags & TF_GAP) return false;
  return fabsf(l.lat) < a->hw - 0.9f && l.height > 0.25f && l.height < 2.2f;
}
static bool in_lane(const track *t, t3_vec3 p) { return in_lane1(t, p, false) || in_lane1(t, p, true); }

static t3_vec3 xf(const t3_mat4 *m, t3_vec3 v) {
  const float *e = m->e;
  return (t3_vec3){ e[0] * v.x + e[4] * v.y + e[8] * v.z + e[12], e[1] * v.x + e[5] * v.y + e[9] * v.z + e[13], e[2] * v.x + e[6] * v.y + e[10] * v.z + e[14] };
}

static void tri_check(const char *name, const track *t, t3_vec3 a, t3_vec3 b, t3_vec3 c) {
  hit(name, t, a), hit(name, t, b), hit(name, t, c);
  hit(name, t, (t3_vec3){ (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3 });
}

static void mesh_check(const char *name, const track *t, const t3_mesh *m, const t3_mat4 *mw) {
  const t3_geometry *g = m->geometry;
  if (!g || !g->attributes[T3_ATTR_POSITION]) return;
  const t3_attribute *pa = g->attributes[T3_ATTR_POSITION];
  if (pa->type != T3_FLOAT32 || pa->item_size < 3) return;
  const float *pv = pa->array;
  int nt = g->index ? g->index->count / 3 : pa->count / 3;
  for (int i = 0; i < nt; i++) {
    int k[3];
    for (int j = 0; j < 3; j++) {
      int q = i * 3 + j;
      k[j] = !g->index ? q : g->index->type == T3_UINT16 ? ((const uint16_t *)g->index->array)[q] : (int)((const uint32_t *)g->index->array)[q];
    }
    t3_vec3 v[3];
    for (int j = 0; j < 3; j++) v[j] = xf(mw, (t3_vec3){ pv[k[j] * pa->item_size], pv[k[j] * pa->item_size + 1], pv[k[j] * pa->item_size + 2] });
    tri_check(name, t, v[0], v[1], v[2]);
  }
}

/* the engine's nodes name the objects they own: the three.c objects carry no names of their own */
#define MAXNAMED 8192
static struct { const t3_object *o; const char *name; } NM[MAXNAMED];
static int nnm;
static void name_nodes(cw_node n) {
  t3_object *o = cw_node3d_object(n);
  if (o && nnm < MAXNAMED) NM[nnm].o = o, NM[nnm].name = cw_node_name(n), nnm++;
  for (int i = 0; i < cw_node_child_count(n); i++) name_nodes(cw_node_child(n, i));
}
static const char *name_of(const t3_object *o) {
  for (int i = 0; i < nnm; i++)
    if (NM[i].o == o) return NM[i].name;
  return o->name;
}

static void walk(const t3_object *o, const track *t, const char *name) {
  if (!o->visible) return;
  const char *nm = name_of(o);
  if (nm && nm[0]) name = nm;
  if (name && (strstr(name, "Kart") || strstr(name, "Items") || strstr(name, "Ghost"))) return;
  if (o->type == T3_MESH) mesh_check(name ? name : "?", t, (const t3_mesh *)o, &o->matrix_world);
  else if (o->type == T3_INSTANCED_MESH) {
    const t3_instanced_mesh *im = (const t3_instanced_mesh *)o;
    const float *mm = im->instance_matrix ? im->instance_matrix->array : NULL;
    for (int i = 0; mm && i < im->count; i++) {
      t3_mat4 inst, w;
      memcpy(inst.e, mm + i * 16, sizeof inst.e);
      t3_mat4_multiply(&w, &o->matrix_world, &inst);
      mesh_check(name ? name : "?", t, &im->mesh, &w);
    }
  }
  for (int i = 0; i < o->child_count; i++) walk(o->children[i], t, name);
}

static int by_n(const void *a, const void *b) { return ((const struct rep *)b)->n - ((const struct rep *)a)->n; }

/* ── the cleaner: what stands in the lanes is taken out at load ───────────── */

static int cut_tris, hid_obj, hid_inst;

/* what moves is left alone: a snapshot of every mesh's place (and its instances) a moment before the clean, and only
 * what has not moved since is cut (a drone or an eagle where it happened to be at load was cut for the whole race) */
#define MAXSNAP 16384
static struct { const t3_object *o; float m[16]; uint32_t inst; } SN[MAXSNAP];
static int nsn;
static uint32_t inst_hash(const t3_object *o) {
  if (o->type != T3_INSTANCED_MESH) return 0;
  const t3_instanced_mesh *im = (const t3_instanced_mesh *)o;
  if (!im->instance_matrix) return 0;
  const uint8_t *b = im->instance_matrix->array;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < (size_t)im->count * 64; i++) h = (h ^ b[i]) * 16777619u;
  return h;
}
static void snap_walk(const t3_object *o) {
  if ((o->type == T3_MESH || o->type == T3_INSTANCED_MESH) && nsn < MAXSNAP) {
    SN[nsn].o = o, SN[nsn].inst = inst_hash(o);
    memcpy(SN[nsn].m, o->matrix_world.e, sizeof SN[nsn].m);
    nsn++;
  }
  for (int i = 0; i < o->child_count; i++) snap_walk(o->children[i]);
}
void geomclean_snapshot(t3_object *root) {
  nsn = 0;
  t3_object_update_matrix_world(root, true);
  snap_walk(root);
}
static bool still(const t3_object *o) {
  for (int i = 0; i < nsn; i++)
    if (SN[i].o == o) return !memcmp(SN[i].m, o->matrix_world.e, sizeof SN[i].m) && SN[i].inst == inst_hash(o);
  return false;
}

static float dist3(t3_vec3 a, t3_vec3 b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return sqrtf(x * x + y * y + z * z); }
/* points across the triangle about a metre apart (a big panel can cross the lanes with every corner outside them) */
float road_gap(const track *t, float x, float z, float *road_y);
static bool tri_in_lane(const track *t, t3_vec3 a, t3_vec3 b, t3_vec3 c) {
  float e = fmaxf(dist3(a, b), fmaxf(dist3(b, c), dist3(c, a)));
  /* the quick no: nowhere near a road (the grid lookup is cheap, the exact place on the road is not) */
  t3_vec3 m = { (a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3, (a.z + b.z + c.z) / 3 };
  float rad = fmaxf(dist3(m, a), fmaxf(dist3(m, b), dist3(m, c)));
  if (road_gap(t, m.x, m.z, NULL) > rad + 1.0f) return false;
  int n = (int)ceilf(e / 1.0f);
  if (n < 1) n = 1;
  if (n > 24) n = 24;
  for (int i = 0; i <= n; i++)
    for (int j = 0; j <= n - i; j++) {
      float u = (float)i / (float)n, v = (float)j / (float)n, w = 1 - u - v;
      t3_vec3 p = { a.x * w + b.x * u + c.x * v, a.y * w + b.y * u + c.y * v, a.z * w + b.z * u + c.z * v };
      if (in_lane(t, p)) return true;
    }
  return false;
}

static int idx_at(const t3_geometry *g, int q) {
  return !g->index ? q : g->index->type == T3_UINT16 ? ((const uint16_t *)g->index->array)[q] : (int)((const uint32_t *)g->index->array)[q];
}

/* the cleaner works through a queue a few thousand triangles a frame, under the countdown (all at once it held the
 * first frame for seconds in a cart): one job per mesh, one per instance of an instanced mesh */
typedef struct cjob { t3_object *o; const char *name; int inst; int next; int cut; } cjob;
#define MAXJOB 16384
static cjob JQ[MAXJOB];
static int njob, jcur;
static const track *jt;

/* the triangles of job j from its cursor, at most `budget` of them: those standing in the lanes are cut (made
 * degenerate) when the geometry is this mesh's alone; a mesh whose geometry is shared with copies elsewhere is hidden
 * whole, an instance has its matrix zeroed. Returns the triangles looked at; the job is done when next < 0 */
static int clean_job(cjob *j, int budget) {
  t3_mesh *m = j->o->type == T3_INSTANCED_MESH ? &((t3_instanced_mesh *)j->o)->mesh : (t3_mesh *)j->o;
  t3_geometry *g = m->geometry;
  t3_attribute *pa = g ? g->attributes[T3_ATTR_POSITION] : NULL;
  if (!pa || pa->type != T3_FLOAT32 || pa->item_size < 3) return j->next = -1, 1;
  t3_mat4 mw = j->o->matrix_world;
  float *mm = NULL;
  if (j->inst >= 0) {
    t3_instanced_mesh *im = (t3_instanced_mesh *)j->o;
    mm = im->instance_matrix->array;
    t3_mat4 inst;
    memcpy(inst.e, mm + j->inst * 16, sizeof inst.e);
    t3_mat4_multiply(&mw, &j->o->matrix_world, &inst);
  }
  float *pv = pa->array;
  int nt = g->index ? g->index->count / 3 : pa->count / 3, done = 0;
  for (int i = j->next; i < nt && done < budget; i++, done++) {
    j->next = i + 1;
    int k[3];
    t3_vec3 v[3];
    for (int q = 0; q < 3; q++) k[q] = idx_at(g, i * 3 + q), v[q] = xf(&mw, (t3_vec3){ pv[k[q] * pa->item_size], pv[k[q] * pa->item_size + 1], pv[k[q] * pa->item_size + 2] });
    if (!tri_in_lane(jt, v[0], v[1], v[2])) continue;
    if (j->inst >= 0) {
      memset(mm + j->inst * 16, 0, sizeof(float) * 16);
      ((t3_instanced_mesh *)j->o)->instance_matrix->version++;
      hid_inst++;
      cw_info("geomclean: hid an instance of %s", j->name);
      return j->next = -1, done + 1;
    }
    if (g->_refs > 1) {
      j->o->visible = false;
      hid_obj++;
      cw_info("geomclean: hid %s (its geometry is shared)", j->name);
      return j->next = -1, done + 1;
    }
    if (g->index) {
      if (g->index->type == T3_UINT16) { uint16_t *ix = g->index->array; ix[i * 3 + 1] = ix[i * 3 + 2] = ix[i * 3]; }
      else { uint32_t *ix = g->index->array; ix[i * 3 + 1] = ix[i * 3 + 2] = ix[i * 3]; }
      g->index->version++;
    } else {
      for (int q = 1; q < 3; q++) memcpy(pv + (i * 3 + q) * pa->item_size, pv + i * 3 * pa->item_size, sizeof(float) * 3);
      pa->version++;
    }
    j->cut++, cut_tris++;
  }
  if (j->next >= nt) {
    if (j->cut) cw_info("geomclean: cut %d triangles from %s", j->cut, j->name);
    j->next = -1;
  }
  return done;
}

static void queue_walk(t3_object *o, const char *name) {
  if (!o->visible) return;
  const char *nm = name_of(o);
  if (nm && nm[0]) name = nm;
  if (name && (strstr(name, "Kart") || strstr(name, "Items") || strstr(name, "Ghost") || strstr(name, "Water") || (name[0] == 'T' && name[1] >= '0' && name[1] <= '9' && name[2] == '_')))
    return; /* the karts, the item boxes, water a road runs through, and the road itself */
  if (name && (!strcmp(name, "Ground") || !strcmp(name, "Terrain"))) return; /* kept under the road where it is built */
  bool mesh = o->type == T3_MESH || o->type == T3_INSTANCED_MESH;
  if (mesh && still(o)) {
    if (o->type == T3_MESH && njob < MAXJOB) JQ[njob++] = (cjob){ o, name ? name : "?", -1, 0, 0 };
    else if (o->type == T3_INSTANCED_MESH) {
      t3_instanced_mesh *im = (t3_instanced_mesh *)o;
      for (int i = 0; im->instance_matrix && i < im->count && njob < MAXJOB; i++) JQ[njob++] = (cjob){ o, name ? name : "?", i, 0, 0 };
    }
  }
  for (int i = 0; i < o->child_count; i++) queue_walk(o->children[i], name);
}

void geomclean_begin(cw_node scene, t3_object *root, const track *t) {
  nnm = 0, njob = 0, jcur = 0, cut_tris = hid_obj = hid_inst = 0, jt = t;
  name_nodes(scene);
  t3_object_update_matrix_world(root, true);
  queue_walk(root, NULL);
}

/* a frame's share: true once the queue is empty */
bool geomclean_step(int budget) {
  while (jcur < njob && budget > 0) {
    budget -= clean_job(&JQ[jcur], budget);
    if (JQ[jcur].next < 0) jcur++;
  }
  if (jcur >= njob && njob >= 0) {
    cw_info("geomclean: %d triangles cut, %d objects and %d instances hidden from the lanes", cut_tris, hid_obj, hid_inst);
    njob = -1;
    return true;
  }
  return njob < 0;
}

void geomcheck_run(cw_node scene, t3_object *root, const track *t) {
  nrep = 0, nnm = 0;
  name_nodes(scene);
  t3_object_update_matrix_world(root, true);
  walk(root, t, NULL);
  const track_phys *w = sim_wall_phys();
  for (int i = 0; w && i < w->nt; i++) tri_check("(collision wall)", t, w->v[w->idx[i * 3]], w->v[w->idx[i * 3 + 1]], w->v[w->idx[i * 3 + 2]]);
  qsort(R, (size_t)nrep, sizeof R[0], by_n);
  cw_info("geomcheck: %d intrusions into the lanes", nrep);
  for (int i = 0; i < nrep; i++)
    cw_info("geomcheck: %-28s s %.0f..%.0f lat %.1f h %.1f (%d points) at %.1f,%.1f,%.1f", R[i].name, R[i].s0, R[i].s1, R[i].lat, R[i].h, R[i].n, R[i].p.x, R[i].p.y, R[i].p.z);
}
