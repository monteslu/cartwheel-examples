/* racer's track meshes: the path (track.c) swept with cross-section profiles.
 *
 * A profile is a short polyline in the road's own frame (lateral to the right,
 * up) with a normal and a texture coordinate on every point; sweeping it from
 * one sample to the next makes a strip. Road, kerbs, walls, tunnels and the
 * rest are profiles, so a banked turn, a loop and a corkscrew need nothing
 * special: the frame carries them.
 *
 * The visual geometry is cut into chunks along the track, one geometry per
 * chunk and kind, so the views cull what they cannot see. The physics meshes
 * are welded (a vertex per profile point per sample) so a kart crossing a
 * seam never catches an edge. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/debug.h"
#include "cartwheel/math.h"
#include "track.h"

#define MAX_RP 40
#define CHUNK 80
#define ARC_N 14
#define WALL_T 0.55f

typedef struct rp {
  float lat, up;   /* position in the road's frame */
  float nl, nu;    /* normal in the road's frame */
  float uc;        /* the cross texture coordinate */
  bool brk;        /* not joined to the previous point */
} rp;

static t3_vec3 v3_(float x, float y, float z) { return (t3_vec3){ x, y, z }; }
static t3_vec3 add(t3_vec3 a, t3_vec3 b) { return v3_(a.x + b.x, a.y + b.y, a.z + b.z); }
static t3_vec3 sub(t3_vec3 a, t3_vec3 b) { return v3_(a.x - b.x, a.y - b.y, a.z - b.z); }
static t3_vec3 mul(t3_vec3 a, float k) { return v3_(a.x * k, a.y * k, a.z * k); }
static float dot(t3_vec3 a, t3_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static t3_vec3 cross(t3_vec3 a, t3_vec3 b) { return v3_(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

/* a point of a profile at a sample */
static t3_vec3 place(const tsample *s, float lat, float up) { return add(s->p, add(mul(s->r, lat), mul(s->u, up))); }
static t3_vec3 place_n(const tsample *s, float nl, float nu) { return add(mul(s->r, nl), mul(s->u, nu)); }

static void pt(rp *o, int *n, float lat, float up, float nl, float nu, float uc, bool brk) {
  if (*n < MAX_RP) o[(*n)++] = (rp){ lat, up, nl, nu, uc, brk };
}

/* The profile of `kind` at sample s. Returns the point count and writes which
 * sides it has (bit 0 left, bit 1 right) to *mask; 0 points when this kind
 * is absent here. */
static int ring(int kind, const tsample *s, const track_mesh_opts *o, rp *out, int *mask) {
  int n = 0;
  *mask = 0;
  uint32_t f = s->flags;
  float hw = s->hw;
  if (f & TF_GAP) return 0;
  switch (kind) {
  case TM_ROAD:
    *mask = 3;
    pt(out, &n, -hw, 0, 0, 1, 0, false);
    pt(out, &n, hw, 0, 0, 1, 1, false);
    return n;
  case TM_KERB:
    if (o->curb_w <= 0) return 0;
    *mask = 3;
    {
      float w = o->curb_w, h = 0.11f;
      pt(out, &n, -hw + w, 0, 1, 0, 0, false);
      pt(out, &n, -hw + w, h, 1, 0, 0.5f, false);
      pt(out, &n, -hw + w, h, 0, 1, 0.5f, false);
      pt(out, &n, -hw, h, 0, 1, 1, false);
      pt(out, &n, hw, h, 0, 1, 0, true);
      pt(out, &n, hw - w, h, 0, 1, 0.5f, false);
      pt(out, &n, hw - w, h, -1, 0, 0.5f, false);
      pt(out, &n, hw - w, 0, -1, 0, 1, false);
    }
    return n;
  case TM_WALL:
    if (f & TF_TUNNEL) return 0;
    if (!(f & (TF_WALL_L | TF_WALL_R))) return 0;
    {
      /* the wall class of the sample picks the height; the flags say which sides */
      float H = o->wall_h[s->walls < 4 ? s->walls : 3];
      if (H <= 0) return 0;
      bool first = true;
      if (f & TF_WALL_L) {
        pt(out, &n, -hw, 0, 1, 0, 0, first);
        pt(out, &n, -hw, H, 1, 0, H / 4, false);
        *mask |= 1;
        first = false;
      }
      if (f & TF_WALL_R) {
        pt(out, &n, hw, 0, -1, 0, 0, true);
        pt(out, &n, hw, H, -1, 0, H / 4, false);
        *mask |= 2;
      }
      (void)first;
    }
    return n;
  case TM_WALLTOP:
    if (f & TF_TUNNEL) return 0;
    if (!(f & (TF_WALL_L | TF_WALL_R))) return 0;
    {
      float H = o->wall_h[s->walls < 4 ? s->walls : 3];
      if (H <= 0) return 0;
      if (f & TF_WALL_L) {
        pt(out, &n, -hw, H, 0, 1, 0, true);
        pt(out, &n, -hw - WALL_T, H, 0, 1, 0.3f, false);
        pt(out, &n, -hw - WALL_T, H, -1, 0, 0.3f, false);
        pt(out, &n, -hw - WALL_T, -0.4f, -1, 0, 1, false);
        *mask |= 1;
      }
      if (f & TF_WALL_R) {
        pt(out, &n, hw, H, 0, 1, 0, true);
        pt(out, &n, hw + WALL_T, H, 0, 1, 0.3f, false);
        pt(out, &n, hw + WALL_T, H, 1, 0, 0.3f, false);
        pt(out, &n, hw + WALL_T, -0.4f, 1, 0, 1, false);
        *mask |= 2;
      }
    }
    return n;
  case TM_TUNNEL:
  case TM_PLATE: {
    bool want = (f & TF_TUNNEL) != 0, plate = (f & TF_PLATE) != 0;
    if (!want || (kind == TM_PLATE) != plate) return 0;
    float Rr = hw, hc = 1.6f, Rt = o->tunnel_clear - hc;
    if (Rt < 1) Rt = 1;
    *mask = 3;
    /* from the left road edge up the side, over the arch, down the right side; faces look into the tube */
    pt(out, &n, -Rr, 0, 1, 0, 0, false);
    float px = -Rr, py = hc, len = hc;
    for (int k = 0; k <= ARC_N; k++) {
      float a = 3.14159265f * (float)k / ARC_N, ca = cw_cos(a), sa = cw_sin(a);
      float x = -Rr * ca, y = hc + Rt * sa;
      len += k ? sqrtf((x - px) * (x - px) + (y - py) * (y - py)) : 0;
      float nx = -x / (Rr * Rr), ny = -(y - hc) / (Rt * Rt), nl = sqrtf(nx * nx + ny * ny);
      pt(out, &n, x, y, nx / nl, ny / nl, len / 8, false);
      px = x, py = y;
    }
    pt(out, &n, Rr, 0, -1, 0, (len + hc) / 8, false);
    return n;
  }
  case TM_BOOST:
    if (!(f & TF_BOOST)) return 0;
    *mask = 3;
    pt(out, &n, -hw * 0.62f, 0.04f, 0, 1, 0, false);
    pt(out, &n, hw * 0.62f, 0.04f, 0, 1, 1, false);
    return n;
  case TM_UNDER:
    *mask = 3;
    /* the slab: left side down, the bottom, right side up */
    pt(out, &n, -hw, 0, -1, 0, 0, false);
    pt(out, &n, -hw, -0.9f, -1, 0, 0.1f, false);
    pt(out, &n, -hw, -0.9f, 0, -1, 0.1f, false);
    pt(out, &n, hw, -0.9f, 0, -1, 0.9f, false);
    pt(out, &n, hw, -0.9f, 1, 0, 0.9f, false);
    pt(out, &n, hw, 0, 1, 0, 1, false);
    return n;
  }
  return 0;
}

/* ── a mesh under construction ──────────────────────────────────────── */

typedef struct mbuf {
  float *pos, *nor, *uv;
  uint32_t *idx;
  int nv, ni, cap_v, cap_i;
} mbuf;

static int mb_v(mbuf *b, t3_vec3 p, t3_vec3 n, float u, float v) {
  if (b->nv == b->cap_v) {
    b->cap_v = b->cap_v ? b->cap_v * 2 : 512;
    b->pos = realloc(b->pos, sizeof(float) * 3 * (size_t)b->cap_v);
    b->nor = realloc(b->nor, sizeof(float) * 3 * (size_t)b->cap_v);
    b->uv = realloc(b->uv, sizeof(float) * 2 * (size_t)b->cap_v);
  }
  float *P = b->pos + b->nv * 3, *N = b->nor + b->nv * 3, *U = b->uv + b->nv * 2;
  P[0] = p.x, P[1] = p.y, P[2] = p.z;
  N[0] = n.x, N[1] = n.y, N[2] = n.z;
  U[0] = u, U[1] = v;
  return b->nv++;
}
static void mb_tri(mbuf *b, int a, int c, int d, t3_vec3 face) {
  /* wind (a, c, d) so its geometric normal agrees with `face` */
  t3_vec3 pa = v3_(b->pos[a * 3], b->pos[a * 3 + 1], b->pos[a * 3 + 2]);
  t3_vec3 pc = v3_(b->pos[c * 3], b->pos[c * 3 + 1], b->pos[c * 3 + 2]);
  t3_vec3 pd = v3_(b->pos[d * 3], b->pos[d * 3 + 1], b->pos[d * 3 + 2]);
  t3_vec3 g = cross(sub(pc, pa), sub(pd, pa));
  if (dot(g, g) < 1e-12f) return;
  if (b->ni + 3 > b->cap_i) {
    b->cap_i = b->cap_i ? b->cap_i * 2 : 1536;
    b->idx = realloc(b->idx, sizeof(uint32_t) * (size_t)b->cap_i);
  }
  if (dot(g, face) < 0) {
    int t = c;
    c = d;
    d = t;
  }
  b->idx[b->ni++] = (uint32_t)a;
  b->idx[b->ni++] = (uint32_t)c;
  b->idx[b->ni++] = (uint32_t)d;
}
static t3_geometry *mb_finish(mbuf *b) {
  if (!b->ni) {
    free(b->pos), free(b->nor), free(b->uv), free(b->idx);
    memset(b, 0, sizeof *b);
    return NULL;
  }
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
  a = t3_attribute_new(T3_UINT32, b->idx, b->ni, 1);
  t3_geometry_set_index(g, a);
  t3_release(a);
  free(b->pos), free(b->nor), free(b->uv), free(b->idx);
  memset(b, 0, sizeof *b);
  return g;
}

static int next_i(const track *t, int i) {
  if (i + 1 < t->n) return i + 1;
  return t->circuit ? 0 : -1;
}

static float tile_for(int kind, const track_mesh_opts *o) {
  return kind == TM_ROAD || kind == TM_UNDER ? o->tile_v : kind == TM_BOOST ? 6.0f : kind == TM_KERB ? 2.0f : 4.0f;
}

/* sweep `kind` over samples [i0, i1) into b */
static void sweep_visual(mbuf *b, const track *t, int kind, int i0, int i1, const track_mesh_opts *o) {
  rp ra[MAX_RP], rb[MAX_RP];
  float tv = tile_for(kind, o);
  for (int i = i0; i < i1; i++) {
    int j = next_i(t, i);
    if (j < 0) break;
    const tsample *A = &t->s[i], *B = &t->s[j];
    int ma, mb;
    int na = ring(kind, A, o, ra, &ma), nb = ring(kind, B, o, rb, &mb);
    if (!na || na != nb || ma != mb) continue;
    float sA = A->s, sB = j > i ? B->s : t->length;
    int idx_a[MAX_RP], idx_b[MAX_RP];
    for (int k = 0; k < na; k++) {
      idx_a[k] = mb_v(b, place(A, ra[k].lat, ra[k].up), place_n(A, ra[k].nl, ra[k].nu), ra[k].uc, sA / tv);
      idx_b[k] = mb_v(b, place(B, rb[k].lat, rb[k].up), place_n(B, rb[k].nl, rb[k].nu), rb[k].uc, sB / tv);
    }
    for (int k = 0; k + 1 < na; k++) {
      if (ra[k + 1].brk) continue;
      t3_vec3 face = add(place_n(A, ra[k].nl, ra[k].nu), place_n(A, ra[k + 1].nl, ra[k + 1].nu));
      mb_tri(b, idx_a[k], idx_a[k + 1], idx_b[k + 1], face);
      mb_tri(b, idx_a[k], idx_b[k + 1], idx_b[k], face);
    }
  }
}

void track_geo_build(track_geo *g, const track *t, const track_mesh_opts *o) {
  memset(g, 0, sizeof *g);
  int cap = 0;
  for (int i0 = 0; i0 < t->n; i0 += CHUNK) {
    int i1 = i0 + CHUNK < t->n ? i0 + CHUNK : t->n;
    /* a chunk overlaps the next by one sample so no seam opens (a circuit's last chunk joins the first) */
    int hi = i1 < t->n || t->circuit ? i1 : i1;
    for (int kind = 0; kind < TM_KINDS; kind++) {
      /* the road and kerbs split by surface kind where it changes inside the chunk */
      int run = i0;
      while (run < hi) {
        int surf = t->s[run].surf, end = run;
        while (end < hi && (kind != TM_ROAD && kind != TM_KERB && kind != TM_UNDER ? true : t->s[end].surf == surf)) end++;
        mbuf b = { 0 };
        sweep_visual(&b, t, kind, run, end, o);
        t3_geometry *geo = mb_finish(&b);
        if (geo) {
          if (g->n == cap) {
            cap = cap ? cap * 2 : 256;
            g->c = realloc(g->c, sizeof(track_chunk) * (size_t)cap);
          }
          t3_vec3 c = t->s[run].p, d = t->s[end > run ? end - 1 : run].p;
          track_chunk *ch = &g->c[g->n++];
          ch->kind = kind;
          ch->surf = surf;
          ch->first = run;
          ch->last = end;
          ch->centre = mul(add(c, d), 0.5f);
          t3_vec3 e = sub(d, c);
          ch->radius = sqrtf(dot(e, e)) * 0.5f + 30.0f;
          ch->geo = geo;
        }
        run = end;
        if (kind != TM_ROAD && kind != TM_KERB && kind != TM_UNDER) break;
      }
    }
  }
}

void track_geo_free(track_geo *g) {
  for (int i = 0; i < g->n; i++) t3_release(g->c[i].geo);
  free(g->c);
  memset(g, 0, sizeof *g);
}

/* ── physics ────────────────────────────────────────────────────────── */

typedef struct pbuf {
  t3_vec3 *v;
  int32_t *idx;
  int nv, nt, cap_v, cap_t;
} pbuf;

static int pv(pbuf *b, t3_vec3 p) {
  if (b->nv == b->cap_v) {
    b->cap_v = b->cap_v ? b->cap_v * 2 : 4096;
    b->v = realloc(b->v, sizeof(t3_vec3) * (size_t)b->cap_v);
  }
  b->v[b->nv] = p;
  return b->nv++;
}
static void ptri(pbuf *b, int a, int c, int d, t3_vec3 face) {
  t3_vec3 g = cross(sub(b->v[c], b->v[a]), sub(b->v[d], b->v[a]));
  if (dot(g, g) < 1e-10f) return;
  if (b->nt == b->cap_t) {
    b->cap_t = b->cap_t ? b->cap_t * 2 : 8192;
    b->idx = realloc(b->idx, sizeof(int32_t) * 3 * (size_t)b->cap_t);
  }
  if (dot(g, face) < 0) {
    int t = c;
    c = d;
    d = t;
  }
  b->idx[b->nt * 3] = a, b->idx[b->nt * 3 + 1] = c, b->idx[b->nt * 3 + 2] = d;
  b->nt++;
}

/* sweeps `kind` for the whole track into b with a welded vertex per profile point per sample */
static void sweep_phys(pbuf *b, const track *t, int kind, const track_mesh_opts *o) {
  rp ra[MAX_RP], rb[MAX_RP];
  int *vi = malloc(sizeof(int) * (size_t)t->n * MAX_RP);
  for (int i = 0; i < t->n * MAX_RP; i++) vi[i] = -1;
  for (int i = 0; i < t->n; i++) {
    int j = next_i(t, i);
    if (j < 0) break;
    const tsample *A = &t->s[i], *B = &t->s[j];
    int ma, mb;
    int na = ring(kind, A, o, ra, &ma), nb = ring(kind, B, o, rb, &mb);
    if (!na || na != nb || ma != mb) continue;
    for (int k = 0; k < na; k++) {
      if (vi[i * MAX_RP + k] < 0) vi[i * MAX_RP + k] = pv(b, place(A, ra[k].lat, ra[k].up));
      if (vi[j * MAX_RP + k] < 0) vi[j * MAX_RP + k] = pv(b, place(B, rb[k].lat, rb[k].up));
    }
    for (int k = 0; k + 1 < na; k++) {
      if (ra[k + 1].brk) continue;
      t3_vec3 face = add(place_n(A, ra[k].nl, ra[k].nu), place_n(A, ra[k + 1].nl, ra[k + 1].nu));
      int a0 = vi[i * MAX_RP + k], a1 = vi[i * MAX_RP + k + 1], b0 = vi[j * MAX_RP + k], b1 = vi[j * MAX_RP + k + 1];
      ptri(b, a0, a1, b1, face);
      ptri(b, a0, b1, b0, face);
    }
  }
  free(vi);
}

void track_phys_build(track_phys *road, track_phys *walls, const track *t, const track_mesh_opts *o) {
  pbuf r = { 0 }, w = { 0 };
  sweep_phys(&r, t, TM_ROAD, o);
  sweep_phys(&w, t, TM_WALL, o);
  sweep_phys(&w, t, TM_WALLTOP, o);
  sweep_phys(&w, t, TM_TUNNEL, o);
  sweep_phys(&w, t, TM_PLATE, o);
  *road = (track_phys){ r.v, r.idx, r.nv, r.nt };
  *walls = (track_phys){ w.v, w.idx, w.nv, w.nt };
}

void track_phys_free(track_phys *p) {
  free(p->v);
  free(p->idx);
  memset(p, 0, sizeof *p);
}
