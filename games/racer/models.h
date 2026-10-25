/* racer's kart modelling kit and the shared kart rig (models.c, models_veh.c, kartgfx.c, items_gfx.c).
 *
 * A vehicle's geometry is built once (kit_geo) as a few merged meshes, one per material slot, and shared by every
 * kart of that vehicle; only the materials (paint) and the small per-kart parts (number plates, driver) are per kart.
 * Kart space: +z forward, +y up, +x LEFT (the sim's right is -x). */
#ifndef RACER_MODELS_H
#define RACER_MODELS_H

#include "world.h"

/* ── the mesh builder ───────────────────────────────────────────────── */

typedef struct mb {
  float *p, *n, *c;
  uint32_t *i;
  int nv, ni, cv, ci;
  float jit; /* per-vertex colour wobble 0..1 (grime) */
} mb;

typedef struct xf {
  t3_vec3 p;
  t3_quat q;
  t3_vec3 s;
} xf;

xf xf_at(float x, float y, float z);
xf xf_rot(float x, float y, float z, float rx, float ry, float rz); /* position, then euler rotation (radians, XYZ) */
xf xf_scaled(xf a, float sx, float sy, float sz);
xf xf_between(t3_vec3 a, t3_vec3 b, float radius_scale); /* maps a unit y-axis cylinder (height 1, radius 1) onto a..b */
t3_vec3 xf_apply(const xf *x, t3_vec3 p);

int mb_vert(mb *b, t3_vec3 p, t3_vec3 n, const float rgb[3]);
void mb_tri(mb *b, int a, int c, int d);
t3_geometry *mb_done(mb *b); /* NULL when empty; frees the builder's arrays */

/* primitives: all append to b, painted rgb (times the builder's jitter) */
void mb_geo(mb *b, t3_geometry *g, xf x, const float rgb[3]); /* takes the geometry */
void mb_rbox(mb *b, xf x, float w, float h, float d, float r, int bevel_steps, const float rgb[3]);
void mb_tube(mb *b, t3_vec3 a, t3_vec3 c, float r, int seg, const float rgb[3]);
void mb_tube_cap(mb *b, t3_vec3 a, t3_vec3 c, float r0, float r1, int seg, const float rgb[3]);
void mb_ball(mb *b, t3_vec3 c, float r, const float rgb[3]);
void mb_ballxyz(mb *b, t3_vec3 c, t3_vec3 r, const float rgb[3]);
void mb_cyl(mb *b, xf x, float r0, float r1, float h, int seg, const float rgb[3]);   /* axis +y, centred; r0 top, r1 bottom */
void mb_lathe(mb *b, xf x, const t3_vec2 *pts, int n, int seg, const float rgb[3]);   /* axis +y: (radius, y) points */
void mb_cone(mb *b, xf x, float r, float h, int seg, const float rgb[3]);
void mb_torus(mb *b, xf x, float r, float tube, int rs, int ts, const float rgb[3]);
void mb_bolt(mb *b, t3_vec3 at, t3_vec3 dir, float r, const float rgb[3]);           /* a rivet head or a bolt */
void mb_polyline(mb *b, const t3_vec3 *pts, int n, float r, int seg, const float rgb[3]); /* tubes with ball joints */

/* a lofted body: rings through the sections along z, smooth, capped at both ends */
typedef struct sect {
  float z, w, yb, yt; /* position, half width, bottom, top */
  float n;            /* squareness: 2 an ellipse, 4 a rounded box */
  float x;            /* side shift */
} sect;
void mb_loft(mb *b, const sect *s, int ns, int ring, float a0, float a1, float inflate, const float bottom_rgb[3], const float top_rgb[3]);

/* a hole-less arch (fender) over a wheel: partial pipe about the x axis, outer radius r, from angle a0 to a1 (0 = up) */
void mb_arch(mb *b, t3_vec3 c, float r, float x0, float x1, float a0, float a1, int seg, const float rgb[3]);

/* a flat quad strip: corners given in order around the quad */
void mb_quad(mb *b, t3_vec3 a, t3_vec3 c, t3_vec3 d, t3_vec3 e, const float rgb[3]);
void mb_digit(mb *b, xf x, int digit, float h, const float rgb[3]); /* a seven-segment number, drawn in the xy plane facing +z */

extern const float C_STEEL[3], C_RUST[3], C_DARK[3], C_CHROME[3], C_BRASS[3], C_WHITE[3], C_BLACK[3], C_YELLOW[3], C_RED[3];
void rgb_mix(float out[3], const float a[3], const float b[3], float t);
void rgb_mul(float out[3], const float a[3], float k);

/* ── material slots and a vehicle's shared geometry ─────────────────── */

enum { S_PAINT, S_PAINT2, S_METAL, S_DARK, S_LAMP, S_TAIL, S_GLASS, S_COUNT };

typedef struct wheel_look {
  float width;      /* tyre width in metres */
  int lugs;         /* tread blocks round the tyre */
  float lug;        /* tread block height */
  int spokes;
  float rim_r;      /* rim radius as a share of the wheel radius */
  float rim[3];     /* rim colour */
  float mark[3];    /* the one spoke painted so the spin reads */
  bool wide_lug;    /* chunky blocks (a bruiser) or fine ones */
} wheel_look;

/* a point on a vehicle some other part wants to know about */
typedef struct anchor {
  t3_vec3 p, dir;
} anchor;

typedef struct veh_geo {
  t3_geometry *g[S_COUNT];
  t3_geometry *tyre, *rim[2];            /* rim[0] for the +x (left) wheels, rim[1] the right */
  wheel_look wl;
  anchor flame[4];                       /* exhaust mouths: position and the way the flame points */
  int nflame;
  anchor plate_back;                     /* number plate at the tail: p, dir = its facing */
  float plate_back_w;
  anchor plate_side;                     /* number roundel on the left flank (mirrored to the right) */
  float plate_side_r;
  t3_vec3 seat;                          /* where the driver's hip sits */
  float driver_scale;
  int helmet;                            /* 0 dome, 1 spiked, 2 flap cap, 3 visor, 4 propeller beanie */
  float suit[3], suit2[3];
  t3_vec3 wheel_hub;                     /* steering wheel centre */
  float wheel_r;
  float wheel_tilt;                      /* steering column pitch, radians */
  t3_vec3 strut_top[2];                  /* Pogo: where the spring legs meet the body (front, rear; x mirrored); y<0 none */
  bool struts;
  anchor antenna;                        /* optional whip: p valid when dir.y > 0 */
  float scarf_len;
  float shield_r;                        /* radius of the shield bubble */
  t3_vec3 shield_c;
  float torso_lean;                      /* the driver sits back by this much (radians) */
} veh_geo;

const veh_geo *kit_geo(int veh); /* built on first use, kept for the life of the app */
void kit_free(void);

/* each vehicle's builder (models_veh.c): fills the slot builders and the anchors */
typedef struct kit { mb m[S_COUNT]; veh_geo *vg; const vehicle_def *vd; } kit;
void veh_build(int veh, kit *k);

/* ── a kart's rig: every part the animation moves ───────────────────── */

typedef struct kart_rig {
  t3_object *root;        /* trick and tumble pivot (the sim node's child) */
  t3_object *body;        /* roll / pitch / drift / squash pivot */
  t3_object *wheel[4];    /* steer + spin from the vehicle controller */
  t3_object *torso, *head, *arm_up[2], *arm_lo[2], *hand[2], *steer_wheel;
  t3_object *scarf[4];
  t3_object *prop;
  t3_object *strut[4];
  t3_object *flame[4][2]; /* outer and core cone */
  t3_object *shield;
  t3_object *antenna;
  t3_material *mat[S_COUNT];
  t3_material *flame_mat[2], *shield_mat;
  const veh_geo *vg;
  int veh, colour, number;
} kart_rig;

void rig_build(kart_rig *r, int veh, int colour, int number);
void rig_free(kart_rig *r);
void place_tube(t3_object *o, t3_vec3 a, t3_vec3 b); /* a unit y cylinder object stretched from a to b */

#endif
