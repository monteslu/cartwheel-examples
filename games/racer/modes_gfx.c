/* racer's arena rounds as drawn: capture the flag's two bases and flags, king of the mountain's hill. Render only: every
 * pose is read from the sim (sim.flag, sim.base, sim.hill, the karts) each frame, placed between the last two ticks.
 *
 *   a base     a glowing ring on the floor in the team's colour with a pedestal in the middle
 *   a flag     a pole with a waving cloth and a beam of light up from it (seen from across the arena): on its pedestal at
 *              home, on the carrier's back, or lying on the floor in a pulsing ring once dropped
 *   the hill   a ring on the floor, a faint disc and a see-through wall of light round it, gold while nobody holds it, the
 *              holder's colour, flashing red when contested; it blinks before it moves and swells up where it lands */
#include <math.h>
#include <string.h>

#include "arena.h"
#include "cartwheel/math.h"
#include "models.h"

#define PI 3.14159265f

static const float TEAM_RGB[2][3] = { { 0.95f, 0.18f, 0.14f }, { 0.18f, 0.45f, 1.0f } };

static struct {
  bool built;
  const sim *s;
  t3_object *root;
  /* capture the flag */
  t3_object *base_ring[2], *base_disc[2], *pedestal[2];
  t3_object *flag[2], *cloth[2], *beam[2], *drop_ring[2];
  t3_material *ring_mat[2], *disc_mat[2], *beam_mat[2], *drop_mat[2];
  /* king of the mountain */
  t3_object *hill, *hill_ring, *hill_disc, *hill_wall, *hill_ring2;
  t3_material *hill_ring_mat, *hill_disc_mat, *hill_wall_mat;
  int hill_i;
  float hill_pop;     /* 0..1: the swell where it lands */
  t3_geometry *geo[12];
  int ngeo;
  t3_material *mats[24];
  int nmat;
} M;

static t3_geometry *keep(t3_geometry *g) {
  if (g && M.ngeo < 12) M.geo[M.ngeo++] = g;
  return g;
}
static t3_material *keepm(t3_material *m) {
  if (m && M.nmat < 24) M.mats[M.nmat++] = m;
  return m;
}

void modes_gfx_reset(void) {
  for (int i = 0; i < M.ngeo; i++) t3_release(M.geo[i]);
  for (int i = 0; i < M.nmat; i++) t3_release(M.mats[i]);
  memset(&M, 0, sizeof M);
}

static t3_material *glow_mat(const float rgb[3], float opacity) {
  t3_material *m = t3_mesh_basic_material_new(0xffffff);
  m->color = lin(rgb[0], rgb[1], rgb[2]);
  m->transparent = true, m->depth_write = false, m->blending = T3_ADDITIVE_BLENDING, m->opacity = opacity, m->fog = false;
  m->side = T3_DOUBLE_SIDE;
  return keepm(m);
}

static t3_object *mesh_in(t3_object *parent, t3_geometry *g, t3_material *m, bool cast) {
  t3_mesh *me = t3_mesh_new(g, m);
  me->base.cast_shadow = cast;
  me->base.receive_shadow = false;
  me->base.frustum_culled = false;
  t3_object_add(parent, me);
  t3_release(me);
  return &me->base;
}

static t3_object *group_in(t3_object *parent) {
  t3_object *o = t3_group_new();
  t3_object_add(parent, o);
  t3_release(o);
  return o;
}

/* a flat ring on the floor, inner radius a, outer 1 (scaled to size) */
static t3_geometry *ring_geo(float a) {
  t3_geometry *g = t3_ring_geometry_new(a, 1.0f, 64, 1);
  t3_geometry_rotate_x(g, -PI * 0.5f);
  return keep(g);
}

void modes_gfx_build(cw_node scene, const sim *s) {
  modes_gfx_reset();
  M.s = s;
  if (!s->battle || s->amode == AM_BALLOON) return;
  M.root = t3_group_new();
  hang(scene, "ArenaMode", M.root);
  t3_release(M.root);
  t3_geometry *ring = ring_geo(0.9f), *disc = keep(t3_circle_geometry_new(1.0f, 48));
  t3_geometry_rotate_x(disc, -PI * 0.5f);
  t3_geometry *wall = keep(t3_cylinder_geometry_new(1.0f, 1.0f, 1.0f, 48, 1, true));
  t3_geometry_translate(wall, 0, 0.5f, 0);
  if (s->amode == AM_CTF) {
    /* the flag: a pole, a cloth off it, a ball on top (the cloth its own mesh, so it can wave) */
    mb pb = { 0 };
    float pole[3] = { 0.75f, 0.75f, 0.78f }, knob[3] = { 1.0f, 0.85f, 0.3f };
    mb_cyl(&pb, xf_at(0, 1.5f, 0), 0.07f, 0.07f, 3.0f, 10, pole);
    mb_ball(&pb, v3(0, 3.05f, 0), 0.14f, knob);
    t3_geometry *pg = keep(mb_done(&pb));
    t3_geometry *cg = keep(t3_box_geometry_new(1.7f, 1.05f, 0.06f, 6, 3, 1));
    t3_geometry_translate(cg, 0.85f, 0, 0);
    mb sb = { 0 };
    float stone[3] = { 0.32f, 0.3f, 0.3f }, rim[3] = { 0.9f, 0.85f, 0.75f };
    mb_cyl(&sb, xf_at(0, 0.3f, 0), 0.9f, 1.1f, 0.6f, 20, stone);
    mb_cyl(&sb, xf_at(0, 0.62f, 0), 0.95f, 0.95f, 0.06f, 20, rim);
    t3_geometry *sg = keep(mb_done(&sb));
    t3_geometry *bg = keep(t3_cylinder_geometry_new(0.35f, 0.6f, 1.0f, 14, 1, true));
    t3_geometry_translate(bg, 0, 0.5f, 0);
    t3_material *pm = keepm(phys(lin(1, 1, 1), 0.4f, 0.6f));
    pm->vertex_colors = true;
    for (int t = 0; t < 2; t++) {
      const float *c = TEAM_RGB[t];
      M.ring_mat[t] = glow_mat(c, 0.85f);
      M.disc_mat[t] = glow_mat(c, 0.12f);
      M.beam_mat[t] = glow_mat(c, 0.22f);
      M.drop_mat[t] = glow_mat(c, 0.8f);
      t3_material *cm = keepm(phys(lin(c[0], c[1], c[2]), 0.6f, 0));
      cm->emissive = lin(c[0], c[1], c[2]);
      cm->emissive_intensity = 0.45f;
      M.base_ring[t] = mesh_in(M.root, ring, M.ring_mat[t], false);
      M.base_disc[t] = mesh_in(M.root, disc, M.disc_mat[t], false);
      M.pedestal[t] = mesh_in(M.root, sg, pm, true);
      M.flag[t] = group_in(M.root);
      mesh_in(M.flag[t], pg, pm, true);
      t3_object *cl = group_in(M.flag[t]);
      t3_object_set_position(cl, 0.05f, 2.4f, 0);
      mesh_in(cl, cg, cm, true);
      M.cloth[t] = cl;
      M.beam[t] = mesh_in(M.root, bg, M.beam_mat[t], false);
      M.drop_ring[t] = mesh_in(M.root, ring, M.drop_mat[t], false);
    }
  } else {
    static const float GOLD[3] = { 1.0f, 0.8f, 0.25f };
    M.hill = group_in(M.root);
    M.hill_ring_mat = glow_mat(GOLD, 0.9f);
    M.hill_disc_mat = glow_mat(GOLD, 0.1f);
    M.hill_wall_mat = glow_mat(GOLD, 0.16f);
    M.hill_ring = mesh_in(M.hill, ring, M.hill_ring_mat, false);
    M.hill_disc = mesh_in(M.hill, disc, M.hill_disc_mat, false);
    M.hill_wall = mesh_in(M.hill, wall, M.hill_wall_mat, false);
    M.hill_ring2 = mesh_in(M.hill, ring, M.hill_ring_mat, false);
    M.hill_i = -1;
  }
  M.built = true;
}

/* where kart k is drawn this frame (between its last two ticks) */
static t3_vec3 drawn_at(const kart *k, float alpha) { return v3sub(k->p, v3mul(k->v, (1.0f - alpha) / (float)TICK_HZ)); }

static void place_flat(t3_object *o, t3_vec3 p, float r) {
  t3_object_set_position(o, p.x, p.y, p.z);
  t3_object_set_scale(o, r, 1, r);
}

void modes_gfx_frame(const sim *s, float alpha) {
  if (!M.built || !s) return;
  float t = ((float)s->tick + alpha) / (float)TICK_HZ;
  if (s->amode == AM_CTF) {
    for (int tm = 0; tm < 2; tm++) {
      const struct ctf_flag *f = &s->flag[tm];
      t3_vec3 b = s->base[tm];
      float pulse = 0.85f + 0.15f * sinf(t * 3.0f + (float)tm);
      place_flat(M.base_ring[tm], v3add(b, v3(0, 0.06f, 0)), 7.0f * pulse);
      place_flat(M.base_disc[tm], v3add(b, v3(0, 0.05f, 0)), 7.0f);
      M.ring_mat[tm]->opacity = 0.55f + 0.35f * (f->state == FL_HOME ? 1.0f : 0.5f + 0.5f * sinf(t * 8.0f));
      t3_object_set_position(M.pedestal[tm], b.x, b.y, b.z);
      /* the flag: home on its pedestal, on its carrier's back, or on the floor */
      t3_vec3 at = v3add(b, v3(0, 0.62f, 0));
      t3_quat q = { 0, 0, 0, 1 };
      float sc = 1.0f;
      if (f->state == FL_CARRIED && f->carrier >= 0 && f->carrier < MAXK) {
        const kart *k = &s->k[f->carrier];
        t3_vec3 kf = kart_fwd(k), ku = kart_up(k);
        at = v3add(v3add(drawn_at(k, alpha), v3mul(kf, -VEH[k->veh].len * 0.75f)), v3mul(ku, 0.2f));
        float yaw = atan2f(kf.x, kf.z);
        q = t3_quat_from_axis_angle(v3(0, 1, 0), yaw - PI * 0.5f);
        sc = 0.75f;
      } else if (f->state == FL_DROPPED) {
        at = v3add(f->p, v3(0, 0.05f + 0.15f * fabsf(sinf(t * 4.0f)), 0));
        q = t3_quat_from_axis_angle(v3(0, 0, 1), 0.25f);
      }
      t3_object_set_position(M.flag[tm], at.x, at.y, at.z);
      t3_object_set_quaternion(M.flag[tm], q);
      t3_object_set_scale(M.flag[tm], sc, sc, sc);
      /* the cloth waves (faster on the move) */
      float flap = f->state == FL_CARRIED ? 9.0f : 3.0f;
      t3_object_set_rotation(M.cloth[tm], 0, 0.35f * sinf(t * flap + (float)tm), 0.05f * sinf(t * flap * 1.7f));
      t3_object_set_scale(M.cloth[tm], 1.0f + 0.06f * sinf(t * flap * 1.3f), 1, 1);
      /* the beam: up from wherever the flag is */
      t3_object_set_position(M.beam[tm], at.x, at.y, at.z);
      t3_object_set_scale(M.beam[tm], 1, f->state == FL_CARRIED ? 14.0f : 22.0f, 1);
      M.beam_mat[tm]->opacity = f->state == FL_HOME ? 0.16f : 0.22f + 0.12f * sinf(t * 6.0f);
      M.drop_ring[tm]->visible = f->state == FL_DROPPED;
      if (f->state == FL_DROPPED) {
        float u = fmodf(t * 1.2f, 1.0f);
        place_flat(M.drop_ring[tm], v3add(f->p, v3(0, 0.07f, 0)), 1.0f + 2.6f * u);
        M.drop_mat[tm]->opacity = 0.9f * (1.0f - u);
      }
    }
    return;
  }
  /* the hill */
  if (s->hill_i != M.hill_i) M.hill_i = s->hill_i, M.hill_pop = 0;
  M.hill_pop = fminf(M.hill_pop + 1.0f / 40.0f, 1.0f);
  float grow = M.hill_pop < 1 ? 1.0f - (1.0f - M.hill_pop) * (1.0f - M.hill_pop) * (1.0f - M.hill_pop) : 1.0f;
  t3_object_set_position(M.hill, s->hill.x, s->hill.y + 0.06f, s->hill.z);
  float r = HILL_R * (0.3f + 0.7f * grow);
  t3_object_set_scale(M.hill_ring, r, 1, r);
  t3_object_set_scale(M.hill_disc, r, 1, r);
  float wall_h = 2.2f + 0.4f * sinf(t * 2.0f);
  t3_object_set_scale(M.hill_wall, r, wall_h * grow + 0.01f, r);
  float u = fmodf(t * 0.6f, 1.0f);
  t3_object_set_position(M.hill_ring2, 0, u * 4.0f, 0);
  t3_object_set_scale(M.hill_ring2, r * (1.0f - 0.08f * u), 1, r * (1.0f - 0.08f * u));
  float c[3] = { 1.0f, 0.8f, 0.25f };
  if (s->hill_holder >= 0 && s->hill_holder < MAXK) memcpy(c, KART_RGB[s->k[s->hill_holder].colour & 7], sizeof c);
  else if (s->hill_holder == -2) {
    bool on = fmodf(t * 4.0f, 1.0f) < 0.5f;
    c[0] = 1.0f, c[1] = on ? 0.15f : 0.5f, c[2] = on ? 0.1f : 0.2f;
  }
  /* about to move: it blinks */
  float blink = s->hill_left < 5 * TICK_HZ && fmodf(t * 3.0f, 1.0f) < 0.4f ? 0.35f : 1.0f;
  t3_color lc = lin(c[0], c[1], c[2]);
  M.hill_ring_mat->color = lc, M.hill_disc_mat->color = lc, M.hill_wall_mat->color = lc;
  M.hill_ring_mat->opacity = 0.9f * blink;
  M.hill_disc_mat->opacity = 0.12f * blink;
  M.hill_wall_mat->opacity = (0.14f + 0.04f * sinf(t * 3.0f)) * blink;
}
