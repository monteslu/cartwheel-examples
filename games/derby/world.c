/* derby's arena in 3D: the churned dirt floor, the ring of concrete jersey
 * barriers (the wall the cars hit), the cars, and the skid marks they lay.
 *
 * Everything is placed from the simulation (the Box3D bodies) on the fixed tick, so every host
 * draws the same frame; nothing here feeds back into the rules. The cars'
 * Node3Ds render between their last two ticks; the dents land on the bodies
 * the tick they happen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/decals.h"
#include "cartwheel/math.h"
#include "cartwheel/quality.h"
#include "world.h"

#define SKIDS 2600 /* quads in the skid-mark ring */
#define BODY_DECALS 8 /* scrapes a car keeps at most (fewer at lower effects detail) */

static struct {
  bool ready;
  int theme;
  cw_node root;
  car_model car[MAXC];
  cw_node car_node[MAXC];
  bool on[MAXC];
  int ticks;
  /* skid marks: a ring of quads on the floor */
  t3_geometry *skid_geo;
  int skid_next, skid_used;
  bool skid_live[MAXC][4];
  t3_vec3 skid_l[MAXC][4], skid_r[MAXC][4];
  /* body decals (mesh decals on the car's panels): scrapes at its dents, soot
   * where it burned; a decal near a dent that changes is clipped again */
  cw_node scrape[MAXC][BODY_DECALS], soot[MAXC][2];
  t3_vec3 scrape_at[MAXC][BODY_DECALS], soot_at[MAXC][2];
  int scrape_dent[MAXC][BODY_DECALS]; /* the dent each scrape marks (-1: none) */
  int scrape_next[MAXC], soots[MAXC];
  dent dent_seen[MAXC][MAX_DENTS];
  int ndent_seen[MAXC];
} W;

static cw_node hang(cw_node parent, const char *name, t3_object *obj) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, obj);
  cw_node3d_set_object(n, wrap);
  t3_release(wrap);
  return n;
}

/* An instanced mesh of things that never move, hung as one mesh per sector
 * round the arena (by each instance's position), so a view culls the sectors
 * it cannot see; each sector's sphere comes from its instances. Takes the
 * mesh's reference. */
static void hang_sectors(cw_node root, const char *name, t3_instanced_mesh *src, int sectors) {
  const float *mat = (const float *)src->instance_matrix->array;
  const float *col = src->instance_color ? (const float *)src->instance_color->array : NULL;
  int n = src->count;
  for (int sec = 0; sec < sectors; sec++) {
    int np = 0;
    for (int i = 0; i < n; i++)
      np += CW_CLAMP((int)((cw_atan2(mat[i * 16 + 14], mat[i * 16 + 12]) + 3.1415927f) / 6.2831853f * sectors), 0, sectors - 1) == sec;
    if (!np) continue;
    t3_instanced_mesh *im = t3_instanced_mesh_new(src->mesh.geometry, src->mesh.materials[0], np);
    for (int i = 0, j = 0; i < n; i++) {
      if (CW_CLAMP((int)((cw_atan2(mat[i * 16 + 14], mat[i * 16 + 12]) + 3.1415927f) / 6.2831853f * sectors), 0, sectors - 1) != sec) continue;
      memcpy((float *)im->instance_matrix->array + j * 16, mat + i * 16, 16 * sizeof(float));
      if (col) t3_instanced_mesh_set_color_at(im, j, (t3_color){ col[i * 3], col[i * 3 + 1], col[i * 3 + 2] });
      j++;
    }
    im->mesh.base.cast_shadow = src->mesh.base.cast_shadow;
    im->mesh.base.receive_shadow = src->mesh.base.receive_shadow;
    hang(root, name, &im->mesh.base);
    t3_release(im);
  }
  t3_release(src);
}

static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}

static void add_mesh(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool cast, bool recv) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = recv;
  hang(parent, name, &me->base);
  t3_release(me);
  t3_release(g);
  t3_release(m);
}

/* ── the floor ──────────────────────────────────────────────────────── */

static void build_floor(cw_node root) {
  /* the arena: dirt to just under the barriers (the corners hide beneath them) */
  float w = AW + 1.2f, l = AL + 1.2f;
  mesh_build b = { 0 };
  int nx = 24, nz = 32;
  int base = 0;
  for (int j = 0; j <= nz; j++)
    for (int i = 0; i <= nx; i++) {
      float x = -w + 2 * w * i / nx, z = -l + 2 * l * j / nz;
      int v = mb_vert(&b, (t3_vec3){ x, 0, z }, (t3_vec3){ 0, 1, 0 }, x / 6.0f, z / 6.0f);
      if (i == 0 && j == 0) base = v;
    }
  for (int j = 0; j < nz; j++)
    for (int i = 0; i < nx; i++) {
      int a = base + j * (nx + 1) + i;
      mb_quad(&b, a, a + nx + 1, a + nx + 2, a + 1);
    }
  t3_material *m = phys(lin(1, 1, 1), 0.9f, 0);
  tex_apply(m, TEX_DIRT, 1, 1.4f, 1);
  add_mesh(root, "Dirt", mb_finish(&b), m, false, true);
  /* the lot round it: gravel out to the fence and beyond */
  mesh_build g = { 0 };
  float R = 140;
  int a = mb_vert(&g, (t3_vec3){ -R, -0.02f, -R }, (t3_vec3){ 0, 1, 0 }, -R / 8, -R / 8);
  int c = mb_vert(&g, (t3_vec3){ R, -0.02f, -R }, (t3_vec3){ 0, 1, 0 }, R / 8, -R / 8);
  int d = mb_vert(&g, (t3_vec3){ R, -0.02f, R }, (t3_vec3){ 0, 1, 0 }, R / 8, R / 8);
  int e = mb_vert(&g, (t3_vec3){ -R, -0.02f, R }, (t3_vec3){ 0, 1, 0 }, -R / 8, R / 8);
  mb_quad(&g, a, e, d, c);
  t3_material *gm = phys(lin(1, 1, 1), 0.9f, 0);
  tex_apply(gm, TEX_GRAVEL, 1, 1.2f, 1);
  add_mesh(root, "Lot", mb_finish(&g), gm, false, true);
}

/* ── the wall: a ring of jersey barriers ───────────────────────────── */

#define NJ JERSEY_N

/* one block, length len along +x, its inner face on z = 0 facing -z (the
 * profile's d runs +z) */
static t3_geometry *jersey_block(float len) {
  mesh_build b = { 0 };
  float v = 0;
  for (int k = 0; k + 1 < NJ; k++) {
    float d0 = JERSEY[k][0], y0 = JERSEY[k][1], d1 = JERSEY[k + 1][0], y1 = JERSEY[k + 1][1];
    float ty = y1 - y0, tz = d1 - d0, tl = sqrtf(ty * ty + tz * tz);
    /* the outward normal of this strip: perpendicular to (tz, ty) in (z, y), facing away from the core */
    t3_vec3 n = { 0, tz / tl, -ty / tl };
    float vn = v + tl;
    int a = mb_vert(&b, (t3_vec3){ -len * 0.5f, y0, d0 }, n, 0, v), c = mb_vert(&b, (t3_vec3){ len * 0.5f, y0, d0 }, n, len, v);
    int d = mb_vert(&b, (t3_vec3){ len * 0.5f, y1, d1 }, n, len, vn), e = mb_vert(&b, (t3_vec3){ -len * 0.5f, y1, d1 }, n, 0, vn);
    mb_quad(&b, a, e, d, c);
    v = vn;
  }
  /* the end caps (fans of the profile) */
  for (int s = -1; s <= 1; s += 2) {
    t3_vec3 n = { (float)s, 0, 0 };
    int c0 = mb_vert(&b, (t3_vec3){ s * len * 0.5f, 0.55f, 0.32f }, n, 0.32f, 0.55f);
    for (int k = 0; k < NJ; k++) {
      int p = mb_vert(&b, (t3_vec3){ s * len * 0.5f, JERSEY[k][1], JERSEY[k][0] }, n, JERSEY[k][0], JERSEY[k][1]);
      int q = mb_vert(&b, (t3_vec3){ s * len * 0.5f, JERSEY[(k + 1) % NJ][1], JERSEY[(k + 1) % NJ][0] }, n, JERSEY[(k + 1) % NJ][0],
                      JERSEY[(k + 1) % NJ][1]);
      if (s > 0) mb_tri(&b, c0, q, p);
      else mb_tri(&b, c0, p, q);
    }
  }
  return mb_finish(&b);
}

static void build_wall(cw_node root) {
  static arena_pt P[256];
  const float blk = 2.4f;
  int n = arena_line(P, 256, blk, 0);
  t3_material *m = phys(lin(1, 1, 1), 0.9f, 0);
  tex_apply_uv(m, TEX_CONCRETE, 1, 1, 1.2f, 1);
  t3_geometry *g = jersey_block(blk - 0.06f);
  t3_instanced_mesh *im = t3_instanced_mesh_new(g, m, n);
  t3_release(g);
  t3_release(m);
  uint32_t h = 77;
  for (int i = 0; i < n; i++) {
    const arena_pt *p = &P[i];
    /* the block's +x runs along the tangent; its profile's +z points out of the arena */
    float yaw = cw_atan2(p->tz, -p->tx);
    h = h * 1664525u + 1013904223u;
    float jit = ((float)(h >> 24) / 255.0f - 0.5f) * 0.03f;
    t3_mat4 mm;
    t3_mat4_compose(&mm, (t3_vec3){ p->x, 0, p->z }, t3_quat_from_euler((t3_euler){ 0, yaw + jit, 0, T3_XYZ }), (t3_vec3){ 1, 1, 1 });
    t3_instanced_mesh_set_matrix_at(im, i, &mm);
    /* weathered concrete, the odd block painted in faded hazard stripes of the arena's colours */
    float tone = 0.85f + 0.15f * (float)((h >> 8) & 255) / 255.0f;
    int paint = (i % 9 == 0) ? 1 : (i % 9 == 4) ? 2 : 0;
    t3_color c = paint == 1 ? lin(0.85f * tone, 0.62f * tone, 0.2f * tone) : paint == 2 ? lin(0.7f * tone, 0.2f * tone, 0.16f * tone) : lin(tone, tone, tone * 0.97f);
    t3_instanced_mesh_set_color_at(im, i, c);
  }
  im->mesh.base.cast_shadow = im->mesh.base.receive_shadow = true;
  hang_sectors(root, "Wall", im, 4);
  /* stacks of old tyres bolted along the top of the corners: soft bits for the eye */
  static arena_pt T[256];
  int nt = arena_line(T, 256, 1.1f, 0.33f);
  int corners = 0;
  for (int i = 0; i < nt; i++)
    if (fabsf(T[i].tx) > 0.15f && fabsf(T[i].tz) > 0.15f) corners++;
  t3_material *rm = phys(lin(0.12f, 0.12f, 0.12f), 0.9f, 0);
  tex_apply(rm, TEX_RUBBER, 1, 1, 1);
  t3_geometry *tg = t3_torus_geometry_new(0.36f, 0.16f, 10, 18, CW_TAU);
  t3_instanced_mesh *tm = t3_instanced_mesh_new(tg, rm, corners * 2);
  t3_release(tg);
  t3_release(rm);
  int k = 0;
  for (int i = 0; i < nt && k < corners * 2; i++) {
    if (!(fabsf(T[i].tx) > 0.15f && fabsf(T[i].tz) > 0.15f)) continue;
    for (int lvl = 0; lvl < 2; lvl++) {
      t3_mat4 mm;
      float jx = (float)((i * 7 + lvl * 3) % 5 - 2) * 0.03f;
      t3_mat4_compose(&mm, (t3_vec3){ T[i].x + jx, 1.26f + lvl * 0.3f, T[i].z }, t3_quat_from_euler((t3_euler){ 1.5707963f, 0, 0, T3_XYZ }),
                      (t3_vec3){ 1, 1, 1.6f });
      t3_instanced_mesh_set_matrix_at(tm, k++, &mm);
    }
  }
  tm->mesh.base.cast_shadow = tm->mesh.base.receive_shadow = true;
  tm->count = k; /* the stacks placed */
  hang_sectors(root, "Tyres", tm, 1);
}

/* ── skid marks ─────────────────────────────────────────────────────── */

static void build_skids(cw_node root) {
  mesh_build b = { 0 };
  for (int i = 0; i < SKIDS; i++) {
    t3_vec3 z = { 0, -5, 0 }, up = { 0, 1, 0 };
    int a = mb_vert(&b, z, up, 0, 0), c = mb_vert(&b, z, up, 1, 0), d = mb_vert(&b, z, up, 1, 1), e = mb_vert(&b, z, up, 0, 1);
    mb_quad(&b, a, e, d, c);
  }
  W.skid_geo = mb_finish(&b);
  W.skid_geo->attributes[T3_ATTR_POSITION]->usage = T3_DYNAMIC_DRAW;
  t3_material *m = phys(lin(0.07f, 0.055f, 0.045f), 0.7f, 0);
  m->transparent = true;
  m->opacity = 0.55f;
  m->depth_write = false;
  t3_mesh *me = t3_mesh_new(W.skid_geo, m);
  me->base.receive_shadow = true;
  me->base.frustum_culled = false;
  me->base.render_order = 5;
  hang(root, "Skids", &me->base);
  t3_release(me);
  t3_release(m);
}

static void skid_quad(t3_vec3 l0, t3_vec3 r0, t3_vec3 l1, t3_vec3 r1) {
  float *p = W.skid_geo->attributes[T3_ATTR_POSITION]->array + W.skid_next * 12;
  const float y = 0.014f;
  t3_vec3 v[4] = { l0, r0, r1, l1 };
  for (int k = 0; k < 4; k++) p[k * 3] = v[k].x, p[k * 3 + 1] = y, p[k * 3 + 2] = v[k].z;
  t3_attribute_add_update_range(W.skid_geo->attributes[T3_ATTR_POSITION], W.skid_next * 4, 4); /* only the quads laid go up */
  W.skid_next = (W.skid_next + 1) % SKIDS;
  if (W.skid_used < SKIDS) W.skid_used++;
}

/* each wheel lays a mark where it touches the dirt while it skids (or
 * locks, or spins up) */
static void skids_tick(const sim *s) {
  bool dirty = false;
  for (int i = 0; i < MAXC; i++) {
    const car *c = &s->c[i];
    if (!c->on) continue;
    float fx, fz;
    cw_sincos(c->h, &fx, &fz);
    float sx = fz, sz = -fx;
    float speed = sqrtf(c->vx * c->vx + c->vz * c->vz);
    for (int w = 0; w < 4; w++) {
      const cw_wheel3d *wh = &c->veh.wheel[w];
      bool front = w < 2;
      float slip = wh->locked ? fmaxf(wh->slip_side, wh->slip_long) : wh->slip_side;
      bool mark = wh->contact && wh->ground.id == s->floor.id && speed > 2 && (slip > (front ? 3.0f : 2.2f) || (c->wrecked && speed > 3));
      float cx = wh->point.x, cz = wh->point.z, hw = 0.13f;
      t3_vec3 L = { cx + sx * hw, 0, cz + sz * hw }, R = { cx - sx * hw, 0, cz - sz * hw };
      if (mark && W.skid_live[i][w]) {
        skid_quad(W.skid_l[i][w], W.skid_r[i][w], L, R);
        dirty = true;
      }
      W.skid_live[i][w] = mark;
      W.skid_l[i][w] = L, W.skid_r[i][w] = R;
    }
  }
  if (dirty) W.skid_geo->attributes[T3_ATTR_POSITION]->version++;
}

/* ── puddles: standing water in the ruts, mirror-dark ───────────────── */

static void build_puddles(cw_node root) {
  mesh_build b = { 0 };
  uint32_t h = 911;
  for (int p = 0; p < 7; p++) {
    h = h * 1664525u + 1013904223u;
    float x = ((float)((h >> 8) & 1023) / 1023.0f - 0.5f) * 2 * (AW - 4);
    h = h * 1664525u + 1013904223u;
    float z = ((float)((h >> 8) & 1023) / 1023.0f - 0.5f) * 2 * (AL - 4);
    h = h * 1664525u + 1013904223u;
    float r = 0.6f + 1.4f * (float)((h >> 8) & 255) / 255.0f, ang = (float)((h >> 16) & 255) / 255.0f * CW_TAU;
    const int R = 20;
    t3_vec3 up = { 0, 1, 0 };
    int c0 = mb_vert(&b, (t3_vec3){ x, 0.012f, z }, up, x, z);
    for (int i = 0; i < R; i++) {
      float a = CW_TAU * i / R;
      /* a lumpy outline, stretched along a rut */
      float k = 1 + 0.25f * cw_sin(a * 3 + p) + 0.15f * cw_sin(a * 5 + 2 * p);
      float lx = cw_cos(a) * r * 1.8f * k, lz = cw_sin(a) * r * 0.8f * k, ca = cw_cos(ang), sa = cw_sin(ang);
      mb_vert(&b, (t3_vec3){ x + lx * ca - lz * sa, 0.012f, z + lx * sa + lz * ca }, up, x, z);
    }
    for (int i = 0; i < R; i++) mb_tri(&b, c0, c0 + 1 + (i + 1) % R, c0 + 1 + i);
  }
  t3_material *m = phys(lin(0.12f, 0.1f, 0.085f), 0.06f, 0);
  m->clearcoat = 1;
  m->clearcoat_roughness = 0.03f;
  add_mesh(root, "Puddles", mb_finish(&b), m, false, true);
}

/* ── body decals ────────────────────────────────────────────────────
 * Mesh decals (Decal3D mode MESH) stamped on a car's own meshes, so they ride
 * with it and are lit like its paint: a scrape (through the paint to primer
 * and steel, smeared rubber) at each dent, soot on the bonnet when the engine
 * starts to burn and over the front when it goes up. A mesh decal does not
 * follow the surface once stamped, and the panels move when a dent changes:
 * every decal within reach of a changed dent is clipped again
 * (cw_decal_restamp). Render only: nothing here feeds the rules. */

static int scrapes_cap(void) {
  int fx = cw_gfx_get(CW_GFX_EFFECTS);
  return fx <= CW_QUALITY_LOW ? 2 : fx == CW_QUALITY_MEDIUM ? 4 : BODY_DECALS;
}

/* a decal on car i's meshes: centre at (car frame), facing (-Z into the body) along -dir */
static cw_node body_decal(int i, cw_node d, t3_vec3 at, t3_vec3 rot, t3_vec3 size, int cell, bool soot) {
  if (!cw_node_valid(d)) {
    d = cw_node_new("Decal3D", soot ? "Soot" : "Scrape");
    cw_node_add_child(W.car_node[i], d);
    cw_node_set(d, "mode", cw_val_enum("MESH"));
    cw_node_set(d, "target", cw_val_string(".."));
    cw_node_set(d, "texture", cw_val_string("decals/body.png"));
    cw_node_set(d, "blend", cw_val_enum(soot ? "MULTIPLY" : "MIX"));
    cw_node_set(d, "color", cw_val_color(1, 1, 1, soot ? 0.9f : 0.95f));
    cw_node_set(d, "normal_fade", cw_val_float(0.45f));
    cw_node_set(d, "edge_fade", cw_val_float(0.2f));
    cw_node_set(d, "max_triangles", cw_val_int(2048));
  }
  cw_value r = { CW_T_VEC4 };
  r.v[0] = (float)(cell & 1) * 256, r.v[1] = (float)(cell >> 1) * 256, r.v[2] = r.v[3] = 256;
  cw_node_set(d, "region", r);
  cw_node_set(d, "size", cw_val_vec3(size.x, size.y, size.z));
  cw_node3d_set_position(d, at);
  cw_node3d_set_rotation(d, rot);
  return d;
}

/* a scrape over dent j of car i: on the panel, the slide along the car */
static void scrape_dent(int i, int j, const dent *dn, const car_spec *k) {
  int slot = -1;
  for (int q = 0; q < BODY_DECALS; q++)
    if (W.scrape_dent[i][q] == j && cw_node_valid(W.scrape[i][q])) slot = q;
  if (slot < 0) {
    int cap = scrapes_cap();
    if (cap <= 0) return;
    slot = W.scrape_next[i]++ % cap;
  }
  /* the panel's outward normal, as the dent pushes in (cars.c dent_vertex) */
  float nx = dn->s / k->hw, nz = dn->f / k->hl, nl = sqrtf(nx * nx + nz * nz);
  if (nl < 1e-3f) return;
  nx /= nl, nz /= nl;
  float reach = 0.4f + dn->depth * 2.4f;
  t3_vec3 at = { dn->s + nx * 0.1f, cw_clampf(dn->y, 0.35f, k->belt_end + 0.2f), dn->f + nz * 0.1f }; /* the box reaches 0.4 m in: the deepest dent */
  float yaw = cw_atan2(nx, nz), roll = ((j * 7 + i * 3) % 9 - 4) * 0.06f;
  W.scrape[i][slot] = body_decal(i, W.scrape[i][slot], at, (t3_vec3){ 0, yaw, roll },
                                 (t3_vec3){ reach * 1.7f, reach * 0.75f, 1.0f }, (j + i) & 1, false);
  W.scrape_at[i][slot] = at;
  W.scrape_dent[i][slot] = j;
}

/* soot on the bonnet (n 0, the engine burning) or over the front (n 1, a wreck) */
static void soot_on(int i, int n, const car_spec *k) {
  t3_vec3 at = { 0, k->belt_end + 0.15f, k->hl * (n ? 0.35f : 0.55f) };
  float w = n ? k->hw * 2.4f : k->hw * 1.6f;
  W.soot[i][n] = body_decal(i, W.soot[i][n], at, (t3_vec3){ -1.5707963f, 0, n ? 0.4f : -0.2f },
                            (t3_vec3){ w, w * 1.1f, 0.8f }, 2 + ((i + n) & 1), true);
  W.soot_at[i][n] = at;
}

static void body_decals_tick(int i, const car *c) {
  const car_spec *k = &CAR_SPEC[c->kind];
  /* the dents that changed since the decals were placed: mark them, then clip
   * again every decal the panels round them moved under */
  dent chg[2 * MAX_DENTS];
  int nchg = 0;
  for (int j = 0; j < c->ndent || j < W.ndent_seen[i]; j++) {
    bool have_was = j < W.ndent_seen[i], have_now = j < c->ndent;
    if (have_was && have_now && !memcmp(&W.dent_seen[i][j], &c->dents[j], sizeof(dent))) continue;
    if (have_was) chg[nchg++] = W.dent_seen[i][j];
    if (have_now) {
      chg[nchg++] = c->dents[j];
      if (c->dents[j].depth > 0.03f) scrape_dent(i, j, &c->dents[j], k);
    }
  }
  if (nchg) {
    memcpy(W.dent_seen[i], c->dents, sizeof(dent) * (size_t)c->ndent);
    W.ndent_seen[i] = c->ndent;
    for (int q = 0; q < BODY_DECALS + 2; q++) {
      cw_node d = q < BODY_DECALS ? W.scrape[i][q] : W.soot[i][q - BODY_DECALS];
      t3_vec3 p = q < BODY_DECALS ? W.scrape_at[i][q] : W.soot_at[i][q - BODY_DECALS];
      if (!cw_node_valid(d)) continue;
      for (int m = 0; m < nchg; m++) {
        float dx = p.x - chg[m].s, dz = p.z - chg[m].f, r = 0.4f + chg[m].depth * 2.4f + 1.6f; /* the decal's half size too */
        if (dx * dx + dz * dz < r * r) { cw_decal_restamp(d); break; }
      }
    }
  }
  if (W.soots[i] < 1 && !c->wrecked && c->hp < 30) soot_on(i, 0, k), W.soots[i] = 1;
  if (W.soots[i] < 2 && c->wrecked) {
    if (W.soots[i] < 1) soot_on(i, 0, k);
    soot_on(i, 1, k), W.soots[i] = 2;
  }
}

/* ── build, tick ────────────────────────────────────────────────────── */

void world_reset(void) {
  if (W.ready)
    for (int i = 0; i < MAXC; i++) car_free(&W.car[i]);
  memset(&W, 0, sizeof W);
}

static car_model PRE[MAXC];
static bool pre_ok[MAXC];

void world_prebuild_car(int i, int kind, const car_look *look) {
  if (pre_ok[i]) car_free(&PRE[i]);
  car_build(&PRE[i], kind, look->colour, look->number);
  pre_ok[i] = true;
}

void world_build(cw_node scene, const sim *s, int theme, const car_look looks[MAXC]) {
  world_reset();
  W.theme = theme;
  for (int i = 0; i < MAXC; i++)
    for (int q = 0; q < BODY_DECALS; q++) W.scrape_dent[i][q] = -1;
  W.root = cw_node_new("Node3D", "World");
  cw_node_add_child(scene, W.root);
  build_floor(W.root);
  build_wall(W.root);
  build_skids(W.root);
  build_puddles(W.root);
  for (int i = 0; i < MAXC; i++) {
    W.on[i] = s->c[i].on;
    if (pre_ok[i] && PRE[i].kind == s->c[i].kind && PRE[i].colour == looks[i].colour && PRE[i].number == looks[i].number) {
      W.car[i] = PRE[i];
      pre_ok[i] = false;
    } else
      car_build(&W.car[i], s->c[i].kind, looks[i].colour, looks[i].number);
    char name[16];
    snprintf(name, sizeof name, "Car%d", i);
    W.car_node[i] = hang(W.root, name, W.car[i].root);
    t3_release(W.car[i].root);
    cw_node_set(W.car_node[i], "interpolate", cw_val_bool(true));
    W.car[i].root->visible = W.on[i];
  }
  W.ready = true;
  world_tick(s);
  for (int i = 0; i < MAXC; i++) cw_node3d_snapshot(W.car_node[i]);
}

void world_lod(t3_vec3 eye, float k, int own) {
  if (!W.ready) return;
  /* full detail close up, the silhouette far off; distances in a view k of
   * the screen's height tall (a car there is drawn k times as big) */
  float near = 25 * k, far = (k < 1 ? 20 : 60) * k; /* a split view: the silhouette from 10 m (a quarter screen's car is small) */
  for (int i = 0; i < MAXC; i++) {
    if (!W.on[i]) continue;
    t3_vec3 p = cw_node3d_position(W.car_node[i]);
    float dx = p.x - eye.x, dz = p.z - eye.z, d = dx * dx + dz * dz;
    int lod = d > far * far ? 2 : d > near * near ? 1 : 0;
    if (k < 1 && i != own && lod == 0 && d > 12 * k * 12 * k) lod = 1; /* a split view: full detail only close by (6 m in a quarter) */
    car_set_trimmed(&W.car[i], k < 1 && i == own);
    car_set_lod(&W.car[i], lod);
  }
}

t3_vec3 world_car_pos(int i) { return W.ready ? cw_node3d_position(W.car_node[i]) : (t3_vec3){ 0, 0, 0 }; }

void world_tick(const sim *s) {
  if (!W.ready) return;
  W.ticks++;
  for (int i = 0; i < MAXC; i++) {
    const car *c = &s->c[i];
    if (!c->on) continue;
    cw_node n = W.car_node[i];
    t3_vec3 was = cw_node3d_position(n);
    float dx = c->x - was.x, dz = c->z - was.z;
    cw_node3d_snapshot(n);
    /* the body's pose (its origin is on the floor under its middle at rest) */
    cw_node3d_set_position(n, (t3_vec3){ c->x, c->y, c->z });
    cw_node3d_set_quaternion(n, c->q);
    if (dx * dx + dz * dz > 16) cw_node3d_snapshot(n); /* set down somewhere else: no sweep across */
    car_apply_dents(&W.car[i], c);
    body_decals_tick(i, c);
    car_pose(&W.car[i], c, W.ticks);
  }
  skids_tick(s);
}
