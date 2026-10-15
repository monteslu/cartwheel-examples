/* derby's surroundings: a chain-link fence on steel posts round the wall,
 * timber bleachers down both long sides with a crowd on them (they bob,
 * and jump up on a big hit), floodlight towers at the corners, stacks of
 * crushed cars and a tree line out in the dusk.
 *
 * Placement is integer-hashed from the arena's seed (debug field
 * scenery.hash), so every host builds the same yard. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/math.h"
#include "world.h"

#define CROWD_MAX 700

static struct {
  bool ready;
  cw_node root;
  /* the crowd, one pair of meshes per bleacher (side 0 the +x one), so a view
   * culls the stand it cannot see: fans [side_start[s], side_start[s + 1]) */
  t3_instanced_mesh *body[2], *head[2];
  t3_geometry *fig[2], *hd[2]; /* the crowd's figure and head: full, and lighter for splitscreen's smaller views */
  int side_start[3];
  t3_object *shaft[4];       /* the floodlight beams */
  t3_vec3 shaft_a[4], shaft_b[4]; /* each beam's axis: the lamp, where it lands */
  int crowd;
  t3_vec3 at[CROWD_MAX];
  float yaw[CROWD_MAX], phase[CROWD_MAX], jump[CROWD_MAX];
  int cheer;
  uint32_t seed;
} S;
uint32_t scenery_hash;

static uint32_t hs(uint32_t *h) {
  *h = *h * 1664525u + 1013904223u;
  scenery_hash = (scenery_hash ^ *h) * 16777619u;
  return *h >> 8;
}
static float hf(uint32_t *h) { return (float)(hs(h) & 0xffff) / 65535.0f; }

static cw_node hang(cw_node parent, const char *name, t3_object *obj) {
  cw_node n = cw_node_new("Node3D", name);
  cw_node_add_child(parent, n);
  t3_object *wrap = t3_group_new();
  t3_object_add(wrap, obj);
  cw_node3d_set_object(n, wrap);
  t3_release(wrap);
  return n;
}
static t3_material *phys(t3_color c, float rough, float metal) {
  t3_material *m = t3_mesh_physical_material_new(0xffffff);
  m->color = c;
  m->roughness = rough;
  m->metalness = metal;
  return m;
}
/* a translation, for mb_append */
static t3_mat4 at_m;
static const t3_mat4 *at_(float x, float y, float z) {
  t3_mat4_compose(&at_m, (t3_vec3){ x, y, z }, (t3_quat){ 0, 0, 0, 1 }, (t3_vec3){ 1, 1, 1 });
  return &at_m;
}
#define at(x, y, z) (*at_(x, y, z))
/* a merged part into a group (takes the geometry) */
static void add_part(t3_object *g, t3_geometry *geo, t3_material *m, bool cast, bool recv) {
  t3_mesh *mesh = t3_mesh_new(geo, m);
  mesh->base.cast_shadow = cast;
  mesh->base.receive_shadow = recv;
  t3_object_add(g, mesh);
  t3_release(geo);
  t3_release(mesh);
}
static void put(t3_instanced_mesh *im, int i, t3_vec3 p, float yaw, t3_vec3 s) {
  t3_mat4 m;
  t3_mat4_compose(&m, p, t3_quat_from_euler((t3_euler){ 0, yaw, 0, T3_XYZ }), s);
  t3_instanced_mesh_set_matrix_at(im, i, &m);
}
static void finish_im(cw_node root, const char *name, t3_instanced_mesh *im, bool cast) {
  im->mesh.base.cast_shadow = cast;
  im->mesh.base.receive_shadow = true;
  hang(root, name, &im->mesh.base);
  t3_release(im);
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
static void finish_sectors(cw_node root, const char *name, t3_instanced_mesh *im, bool cast, int sectors) {
  im->mesh.base.cast_shadow = cast;
  im->mesh.base.receive_shadow = true;
  hang_sectors(root, name, im, sectors);
}

/* the fence: posts every 3 m on a rounded rectangle `out` metres beyond the wall */
static void fence(cw_node root, float out) {
  const float step = 3.0f, hgt = 3.4f;
  float hx = AW - AC, hz = AL - AC, r = AC + out;
  float per = 4 * hx + 4 * hz + CW_TAU * r;
  int n = (int)(per / step);
  t3_vec3 *P = malloc(sizeof *P * (size_t)n);
  for (int i = 0; i < n; i++) {
    float s = per * i / n, x, z;
    float L1 = 2 * hz, A = CW_TAU * r / 4, L2 = 2 * hx;
    if (s < L1) x = hx + r, z = -hz + s;
    else if ((s -= L1) < A) x = hx + r * cw_cos(s / r), z = hz + r * cw_sin(s / r);
    else if ((s -= A) < L2) x = hx - s, z = hz + r;
    else if ((s -= L2) < A) x = -hx + r * cw_cos(1.5707963f + s / r), z = hz + r * cw_sin(1.5707963f + s / r);
    else if ((s -= A) < L1) x = -hx - r, z = hz - s;
    else if ((s -= L1) < A) x = -hx + r * cw_cos(3.1415927f + s / r), z = -hz + r * cw_sin(3.1415927f + s / r);
    else if ((s -= A) < L2) x = -hx + s, z = -hz - r;
    else s -= L2, x = hx + r * cw_cos(4.712389f + s / r), z = -hz + r * cw_sin(4.712389f + s / r);
    P[i] = (t3_vec3){ x, 0, z };
  }
  t3_material *st = phys(lin(0.6f, 0.6f, 0.62f), 0.45f, 1);
  tex_apply(st, TEX_STEEL, 1, 0.6f, 1);
  t3_instanced_mesh *posts = t3_instanced_mesh_new(t3_cylinder_geometry_new(0.05f, 0.05f, hgt, 8, 1, false), st, n);
  t3_release(posts->mesh.geometry);
  for (int i = 0; i < n; i++) put(posts, i, (t3_vec3){ P[i].x, hgt * 0.5f, P[i].z }, 0, (t3_vec3){ 1, 1, 1 });
  finish_sectors(root, "Posts", posts, true, 8);
  /* the mesh between the posts, and a rail along the top */
  mesh_build b = { 0 }, rail = { 0 };
  float u = 0;
  for (int i = 0; i < n; i++) {
    t3_vec3 a = P[i], c = P[(i + 1) % n];
    float dx = c.x - a.x, dz = c.z - a.z, l = sqrtf(dx * dx + dz * dz);
    t3_vec3 nn = { dz / l, 0, -dx / l };
    int v0 = mb_vert(&b, (t3_vec3){ a.x, 0.05f, a.z }, nn, u, 0), v1 = mb_vert(&b, (t3_vec3){ c.x, 0.05f, c.z }, nn, u + l / 1.5f, 0);
    int v2 = mb_vert(&b, (t3_vec3){ c.x, hgt - 0.1f, c.z }, nn, u + l / 1.5f, (hgt - 0.15f) / 1.5f),
        v3 = mb_vert(&b, (t3_vec3){ a.x, hgt - 0.1f, a.z }, nn, u, (hgt - 0.15f) / 1.5f);
    mb_quad(&b, v0, v1, v2, v3);
    u += l / 1.5f;
    for (int k = 0; k < 4; k++) { /* the rail: a thin box strip */
      float ang = k * 1.5707963f, ry = hgt + 0.03f * cw_sin(ang), ro = 0.03f * cw_cos(ang);
      float ry2 = hgt + 0.03f * cw_sin(ang + 1.5707963f), ro2 = 0.03f * cw_cos(ang + 1.5707963f);
      t3_vec3 rn = { nn.x * cw_cos(ang + 0.785f), cw_sin(ang + 0.785f), nn.z * cw_cos(ang + 0.785f) };
      int r0 = mb_vert(&rail, (t3_vec3){ a.x + nn.x * ro, ry, a.z + nn.z * ro }, rn, 0, 0), r1 = mb_vert(&rail, (t3_vec3){ c.x + nn.x * ro, ry, c.z + nn.z * ro }, rn, 1, 0);
      int r2 = mb_vert(&rail, (t3_vec3){ c.x + nn.x * ro2, ry2, c.z + nn.z * ro2 }, rn, 1, 1),
          r3 = mb_vert(&rail, (t3_vec3){ a.x + nn.x * ro2, ry2, a.z + nn.z * ro2 }, rn, 0, 1);
      mb_quad(&rail, r0, r1, r2, r3);
    }
  }
  free(P);
  t3_material *ch = phys(lin(0.75f, 0.75f, 0.78f), 0.5f, 1);
  tex_apply(ch, TEX_CHAIN, 1, 0.5f, 1);
  ch->alpha_test = 0.5f;
  ch->side = T3_DOUBLE_SIDE;
  t3_mesh *cm = t3_mesh_new(mb_finish(&b), ch);
  cm->base.cast_shadow = true;
  cm->base.receive_shadow = true;
  hang(root, "Fence", &cm->base);
  t3_release(cm->geometry);
  t3_release(cm);
  t3_release(ch);
  t3_mesh *rm = t3_mesh_new(mb_finish(&rail), st);
  rm->base.receive_shadow = true;
  hang(root, "Rail", &rm->base);
  t3_release(rm->geometry);
  t3_release(rm);
  t3_release(st);
}

/* bleachers down a long side (side = +1 / -1 on x): planks stepping up, the
 * crowd on them */
static int bleachers(cw_node root, float side, uint32_t *h, int crowd0) {
  const int rows = 7;
  const float x0 = AW + 6.5f, depth = 0.9f, rise = 0.55f, len = 2 * AL - 10;
  t3_material *wood = phys(lin(1, 1, 1), 0.85f, 0);
  tex_apply_uv(wood, TEX_WOOD, len / 4, 1, 1, 1);
  t3_material *st = phys(lin(0.45f, 0.45f, 0.47f), 0.5f, 1);
  tex_apply(st, TEX_STEEL, 1, 0.5f, 1);
  /* the seats, the risers and the frame each one mesh (a draw apiece in a view) */
  t3_object *g = t3_group_new();
  mesh_build seats = { 0 }, risers = { 0 }, legs = { 0 };
  for (int r = 0; r < rows; r++) {
    float x = side * (x0 + r * depth), y = 0.5f + r * rise;
    mb_append(&seats, t3_box_geometry_new(depth * 0.95f, 0.08f, len, 1, 1, 1), &at(x, y, 0));
    mb_append(&risers, t3_box_geometry_new(0.04f, rise, len, 1, 1, 1), &at(x - side * depth * 0.47f, y - rise * 0.5f, 0));
  }
  /* the frame under it */
  for (float z = -len * 0.5f; z <= len * 0.5f + 0.01f; z += 4) {
    float top = 0.5f + (rows - 1) * rise;
    mb_append(&legs, t3_box_geometry_new(0.1f, top, 0.1f, 1, 1, 1), &at(side * (x0 + (rows - 1) * depth), top * 0.5f, z));
  }
  add_part(g, mb_finish(&seats), wood, true, true);
  add_part(g, mb_finish(&risers), wood, false, true);
  add_part(g, mb_finish(&legs), st, true, false);
  hang(root, side > 0 ? "StandsL" : "StandsR", g);
  t3_release(g);
  t3_release(wood);
  t3_release(st);
  /* the crowd: seats filled two in three, a gap or two */
  int n = crowd0;
  for (int r = 0; r < rows && n < CROWD_MAX; r++)
    for (float z = -len * 0.5f + 0.4f; z < len * 0.5f - 0.4f && n < CROWD_MAX; z += 0.62f) {
      float fill = hf(h);
      if (fill > 0.72f) continue;
      float jz = (hf(h) - 0.5f) * 0.2f;
      S.at[n] = (t3_vec3){ side * (x0 + r * depth + 0.15f), 0.5f + r * rise + 0.04f, z + jz };
      S.yaw[n] = side > 0 ? -1.5707963f : 1.5707963f;
      S.phase[n] = hf(h) * CW_TAU;
      n++;
    }
  return n;
}

static void crowd_build(cw_node root, uint32_t *h) {
  int n1 = bleachers(root, 1, h, 0);
  int n = bleachers(root, -1, h, n1);
  S.crowd = n;
  S.side_start[0] = 0, S.side_start[1] = n1, S.side_start[2] = n;
  t3_material *cloth = phys(lin(1, 1, 1), 0.85f, 0);
  cloth->sheen = 0.4f;
  cloth->sheen_color = lin(0.8f, 0.8f, 0.8f);
  cloth->sheen_roughness = 0.6f;
  /* a standing figure: legs, hips, a waist, shoulders (a lathe, flattened front to back by the instance) */
  static const t3_vec2 FIG[] = { { 0.001f, 0 }, { 0.11f, 0.02f }, { 0.12f, 0.42f }, { 0.155f, 0.5f }, { 0.13f, 0.62f }, { 0.15f, 0.82f },
                                 { 0.2f, 0.9f }, { 0.17f, 0.97f }, { 0.06f, 1.0f }, { 0.05f, 1.04f }, { 0.001f, 1.04f } };
  t3_geometry *fig = t3_lathe_geometry_new(FIG, (int)(sizeof FIG / sizeof FIG[0]), 10, 0, CW_TAU);
  t3_material *skin = phys(lin(1, 1, 1), 0.7f, 0);
  t3_geometry *hd = t3_sphere_geometry_new(0.1f, 10, 8);
  S.fig[0] = t3_retain(fig), S.hd[0] = t3_retain(hd);
  S.fig[1] = t3_lathe_geometry_new(FIG, (int)(sizeof FIG / sizeof FIG[0]), 6, 0, CW_TAU);
  S.hd[1] = t3_sphere_geometry_new(0.1f, 6, 4);
  for (int s = 0; s < 2; s++) {
    int ns = S.side_start[s + 1] - S.side_start[s];
    S.body[s] = t3_instanced_mesh_new(fig, cloth, ns ? ns : 1);
    S.head[s] = t3_instanced_mesh_new(hd, skin, ns ? ns : 1);
    S.body[s]->count = S.head[s]->count = ns;
    /* the stand's sphere: its seats, raised for a fan standing and jumping
     * (set once, not recomputed as they move) */
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    for (int f = S.side_start[s]; f < S.side_start[s + 1]; f++) {
      float q[3] = { S.at[f].x, S.at[f].y, S.at[f].z };
      for (int c = 0; c < 3; c++) lo[c] = fminf(lo[c], q[c]), hi[c] = fmaxf(hi[c], q[c]);
    }
    if (!ns) lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0;
    float dx = (hi[0] - lo[0]) * 0.5f + 0.4f, dy = (hi[1] - lo[1]) * 0.5f + 1.0f, dz = (hi[2] - lo[2]) * 0.5f + 0.4f;
    t3_sphere sph = { { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f + 0.5f, (lo[2] + hi[2]) * 0.5f }, sqrtf(dx * dx + dy * dy + dz * dz) };
    t3_instanced_mesh_set_bounding_sphere(S.body[s], &sph);
    t3_instanced_mesh_set_bounding_sphere(S.head[s], &sph);
  }
  t3_release(fig);
  t3_release(hd);
  /* work clothes: denim, flannel, khaki, black, a few hi-vis */
  static const float SHIRT[8][3] = { { 0.3f, 0.34f, 0.45f }, { 0.42f, 0.18f, 0.14f }, { 0.48f, 0.44f, 0.34f }, { 0.12f, 0.12f, 0.13f },
                                     { 0.22f, 0.26f, 0.22f }, { 0.55f, 0.52f, 0.48f }, { 0.2f, 0.24f, 0.34f }, { 0.75f, 0.6f, 0.15f } };
  static const float SKIN[4][3] = { { 0.85f, 0.65f, 0.52f }, { 0.62f, 0.44f, 0.32f }, { 0.4f, 0.27f, 0.18f }, { 0.92f, 0.76f, 0.64f } };
  for (int i = 0; i < n; i++) {
    int sh = (int)(hs(h) % 8u), sk = (int)(hs(h) % 4u);
    int s = i >= S.side_start[1], li = i - S.side_start[s];
    t3_instanced_mesh_set_color_at(S.body[s], li, lin(SHIRT[sh][0], SHIRT[sh][1], SHIRT[sh][2]));
    t3_instanced_mesh_set_color_at(S.head[s], li, lin(SKIN[sk][0], SKIN[sk][1], SKIN[sk][2]));
  }
  for (int s = 0; s < 2; s++) {
    finish_im(root, "Crowd", S.body[s], true);
    finish_im(root, "Heads", S.head[s], false);
  }
  t3_release(cloth);
  t3_release(skin);
}

/* the share of the crowd drawn (cw_quality_density): slot j shows person
 * j * crowd / drawn, so a thinned crowd still fills both stands */
static float density = 1;
static void crowd_pose(int ticks) {
  float k = density < 0.05f ? 0.05f : density > 1 ? 1 : density;
  for (int s = 0; s < 2; s++) {
    if (!S.body[s]) continue;
    int n = S.side_start[s + 1] - S.side_start[s], drawn = (int)((float)n * k);
    S.body[s]->count = S.head[s]->count = drawn;
    for (int j = 0; j < drawn; j++) {
      int i = S.side_start[s] + (drawn == n ? j : (int)((int64_t)j * n / drawn));
      float bob = 0.02f * cw_sin(ticks * 0.08f + S.phase[i]);
      float up = S.jump[i];
      t3_vec3 p = S.at[i];
      /* sitting: a figure set low on the plank (the legs hang behind the riser), standing on a big hit */
      float stand = up > 0.06f ? 1 : 0;
      float base = p.y - 0.45f + stand * 0.4f;
      put(S.body[s], j, (t3_vec3){ p.x, base + bob + up * 0.5f, p.z }, S.yaw[i], (t3_vec3){ 1, 1.02f, 0.62f });
      put(S.head[s], j, (t3_vec3){ p.x, base + 1.13f + bob * 1.3f + up * 0.5f, p.z }, 0, (t3_vec3){ 1, 1.1f, 1 });
    }
  }
}

/* floodlight towers at the corners: a pole, a bank of lamps aimed in, a
 * beam through the haze */
static void floodlights(cw_node root) {
  t3_material *st = phys(lin(0.5f, 0.5f, 0.52f), 0.5f, 1);
  tex_apply(st, TEX_STEEL, 1, 0.5f, 1);
  t3_material *lamp = phys(lin(1, 0.98f, 0.9f), 0.2f, 0);
  lamp->emissive = lin(1, 0.72f, 0.42f);
  lamp->emissive_intensity = 14;
  t3_material *beam = t3_mesh_basic_material_new(0xffffff);
  beam->color = lin(1, 0.7f, 0.42f);
  beam->transparent = true;
  beam->opacity = 0.015f;
  beam->vertex_colors = true;
  beam->blending = T3_ADDITIVE_BLENDING;
  beam->depth_write = false;
  beam->side = T3_FRONT_SIDE; /* from inside a cone, nothing: it never fills the screen */
  /* the four towers' steel and their lamps are one mesh each (two draws a view) */
  mesh_build steel = { 0 }, lamps = { 0 };
  for (int k = 0; k < 4; k++) {
    float sx = k & 1 ? 1.0f : -1.0f, sz = k & 2 ? 1.0f : -1.0f;
    float x = sx * (AW + 4), z = sz * (AL + 4), H = 17;
    t3_object *g = t3_group_new();
    mb_append(&steel, t3_cylinder_geometry_new(0.18f, 0.32f, H, 12, 1, false), &at(x, H * 0.5f, z));
    /* the head, turned to face the middle */
    t3_object *head = t3_group_new();
    head->position = (t3_vec3){ x, H, z };
    float yaw = cw_atan2(-x, -z);
    t3_object_set_rotation(head, 0, yaw, 0);
    t3_mat4 hm, m;
    t3_mat4_compose(&hm, head->position, t3_quat_from_euler((t3_euler){ 0, yaw, 0, T3_XYZ }), (t3_vec3){ 1, 1, 1 });
    mb_append(&steel, t3_box_geometry_new(3.2f, 1.7f, 0.3f, 1, 1, 1), &hm);
    for (int r = 0; r < 2; r++)
      for (int c = 0; c < 4; c++) {
        t3_mat4_multiply(&m, &hm, &at(-1.17f + c * 0.78f, -0.38f + r * 0.76f, 0.17f));
        mb_append(&lamps, t3_box_geometry_new(0.62f, 0.62f, 0.06f, 1, 1, 1), &m);
      }
    /* the beam: a long cone toward the arena floor */
    float dist = sqrtf(x * x + z * z + H * H);
    t3_mesh *b = t3_mesh_new(geo_beam(1.1f, 5, dist * 0.8f), beam);
    float pitch = cw_atan2(H, sqrtf(x * x + z * z));
    b->base.position = (t3_vec3){ 0, -dist * 0.4f * cw_sin(pitch), dist * 0.4f * cw_cos(pitch) };
    t3_object_set_rotation(&b->base, 1.5707963f + pitch, 0, 0);
    b->base.render_order = 40;
    b->base.frustum_culled = false;
    t3_object_add(head, b);
    S.shaft[k] = &b->base;
    S.shaft_a[k] = (t3_vec3){ x, H, z };
    S.shaft_b[k] = (t3_vec3){ x * 0.12f, 0, z * 0.12f };
    t3_release(b->geometry);
    t3_release(b);
    t3_object_add(g, head);
    t3_release(head);
    char name[16];
    snprintf(name, sizeof name, "Tower%d", k);
    hang(root, name, g);
    t3_release(g);
    /* the light itself: a sodium flood (no shadow: the sun's map covers the play) */
    snprintf(name, sizeof name, "Flood%d", k);
    cw_node sp = cw_node_new("SpotLight3D", name);
    cw_node_add_child(root, sp);
    cw_node_set(sp, "color", cw_val_color(1, 0.66f, 0.36f, 1));
    cw_node_set(sp, "energy", cw_val_float(9000));
    cw_node_set(sp, "angle", cw_val_float(0.72f));
    cw_node_set(sp, "penumbra", cw_val_float(0.55f));
    cw_node_set(sp, "decay", cw_val_float(2));
    cw_node3d_set_position(sp, (t3_vec3){ x * 0.97f, H - 0.4f, z * 0.97f });
    cw_node3d_look_at(sp, (t3_vec3){ x * 0.12f, 0, z * 0.12f }, (t3_vec3){ 0, 1, 0 });
  }
  t3_object *g = t3_group_new();
  add_part(g, mb_finish(&steel), st, true, true);
  add_part(g, mb_finish(&lamps), lamp, false, false);
  hang(root, "Towers", g);
  t3_release(g);
  t3_release(st);
  t3_release(lamp);
  t3_release(beam);
}

/* stacks of crushed cars past the ends of the arena */
static void car_stacks(cw_node root, uint32_t *h) {
  const int stacks = 10, per = 4;
  t3_material *m = phys(lin(1, 1, 1), 0.7f, 0.4f);
  tex_apply(m, TEX_RUST, 1, 1, 1);
  t3_instanced_mesh *im = t3_instanced_mesh_new(geo_rounded_box(4.1f, 0.62f, 1.75f, 0.12f, 2), m, stacks * per);
  t3_release(im->mesh.geometry);
  t3_release(m);
  int k = 0;
  for (int s = 0; s < stacks; s++) {
    float side = s < stacks / 2 ? -1.0f : 1.0f;
    int j = s % (stacks / 2);
    float x = -16 + j * 8 + (hf(h) - 0.5f) * 2, z = side * (AL + 12 + hf(h) * 4);
    float yaw0 = (hf(h) - 0.5f) * 0.6f;
    for (int l = 0; l < per; l++) {
      float yaw = yaw0 + (hf(h) - 0.5f) * 0.3f;
      put(im, k, (t3_vec3){ x + (hf(h) - 0.5f) * 0.3f, 0.31f + l * 0.6f, z }, yaw, (t3_vec3){ 1, 1 - 0.1f * hf(h), 1 });
      static const float C[6][3] = { { 0.55f, 0.2f, 0.15f }, { 0.25f, 0.3f, 0.45f }, { 0.6f, 0.55f, 0.35f }, { 0.3f, 0.35f, 0.25f },
                                     { 0.7f, 0.68f, 0.62f }, { 0.4f, 0.25f, 0.2f } };
      int c = (int)(hs(h) % 6u);
      t3_instanced_mesh_set_color_at(im, k, lin(C[c][0], C[c][1], C[c][2]));
      k++;
    }
  }
  finish_sectors(root, "Crushed", im, true, 8);
}

/* a tree line and a few sheds far out, dark against the sky */
static void horizon(cw_node root, uint32_t *h) {
  const int trees = 90;
  t3_material *m = phys(lin(0.1f, 0.14f, 0.08f), 0.9f, 0);
  /* each tree a lumpy canopy (three squashed spheres) on a short trunk, dark against the sky */
  t3_instanced_mesh *im = t3_instanced_mesh_new(t3_sphere_geometry_new(3.4f, 9, 7), m, trees * 3);
  t3_release(im->mesh.geometry);
  t3_release(m);
  for (int i = 0; i < trees; i++) {
    float a = CW_TAU * i / trees + hf(h) * 0.05f, r = 120 + hf(h) * 30, s = 0.8f + hf(h) * 0.9f;
    float x = r * cw_sin(a), z = r * cw_cos(a);
    for (int k = 0; k < 3; k++) {
      float ox = (hf(h) - 0.5f) * 4 * s, oz = (hf(h) - 0.5f) * 4 * s, oy = (5 + hf(h) * 3) * s, sc = s * (0.7f + 0.35f * hf(h));
      put(im, i * 3 + k, (t3_vec3){ x + ox, oy, z + oz }, 0, (t3_vec3){ sc, sc * 0.85f, sc });
    }
  }
  finish_sectors(root, "Trees", im, false, 8);
}

void scenery_reset(void) {
  for (int i = 0; i < 2; i++) {
    if (S.fig[i]) t3_release(S.fig[i]);
    if (S.hd[i]) t3_release(S.hd[i]);
  }
  memset(&S, 0, sizeof S);
}
/* the crowd's lighter figures (splitscreen: a quarter of the screen shows a
 * fan a few pixels tall, and every view draws both stands' worth) */
void scenery_light_crowd(bool light) {
  for (int s = 0; s < 2; s++) {
    if (!S.body[s]) continue;
    t3_geometry *f = S.fig[light], *h = S.hd[light];
    if (S.body[s]->mesh.geometry != f) t3_retain(f), t3_release(S.body[s]->mesh.geometry), S.body[s]->mesh.geometry = f;
    if (S.head[s]->mesh.geometry != h) t3_retain(h), t3_release(S.head[s]->mesh.geometry), S.head[s]->mesh.geometry = h;
  }
}
void scenery_density(float k) {
  density = k;
  if (S.ready) crowd_pose(0);
}

void scenery_build(cw_node scene, int theme, uint32_t seed) {
  (void)theme;
  scenery_reset();
  scenery_hash = 2166136261u;
  uint32_t h = seed * 2654435761u + 1;
  S.root = cw_node_new("Node3D", "Scenery");
  cw_node_add_child(scene, S.root);
  fence(S.root, 3.0f);
  crowd_build(S.root, &h);
  floodlights(S.root);
  car_stacks(S.root, &h);
  horizon(S.root, &h);
  crowd_pose(0);
  S.ready = true;
}

/* a beam is only drawn from a distance: near its axis (under a tower, in
 * its pool of light) the camera would be in the haze, and the haze is
 * the fog's job */
void scenery_beams(const t3_vec3 *eyes, int n) {
  if (!S.ready) return;
  for (int k = 0; k < 4; k++) {
    if (!S.shaft[k]) continue;
    t3_vec3 a = S.shaft_a[k], b = S.shaft_b[k];
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, l2 = dx * dx + dy * dy + dz * dz, best = 1e9f;
    for (int v = 0; v < n; v++) {
      float t = ((eyes[v].x - a.x) * dx + (eyes[v].y - a.y) * dy + (eyes[v].z - a.z) * dz) / l2;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float qx = a.x + dx * t - eyes[v].x, qy = a.y + dy * t - eyes[v].y, qz = a.z + dz * t - eyes[v].z;
      float d = qx * qx + qy * qy + qz * qz;
      if (d < best) best = d;
    }
    S.shaft[k]->visible = best > 16 * 16;
  }
}

void scenery_tick(const sim *s, int ticks, int cheer) {
  if (!S.ready) return;
  /* a hard hit or a wreck gets them on their feet */
  if (s)
    for (int e = 0; e < s->ev_n; e++)
      if ((s->ev[e].kind == EV_CRASH && s->ev[e].k > 6) || s->ev[e].kind == EV_WRECK) S.cheer = s->ev[e].kind == EV_WRECK ? 90 : 40;
  if (cheer) S.cheer = 60;
  if (S.cheer > 0) S.cheer--;
  for (int i = 0; i < S.crowd; i++) {
    float want = S.cheer > 0 ? 0.12f + 0.1f * cw_sin(ticks * 0.5f + S.phase[i] * 3) : 0;
    S.jump[i] += (want - S.jump[i]) * 0.25f;
  }
  /* the crowd moves (and goes to the GPU) every other tick, every tick while it cheers */
  if ((ticks & 1) == 0 || S.cheer > 0) {
    crowd_pose(ticks);
    for (int k = 0; k < 2; k++)
      if (S.body[k]) {
        S.body[k]->instance_matrix->version++;
        S.head[k]->instance_matrix->version++;
      }
  }
}
