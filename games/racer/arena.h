/* racer's battle arenas: an open floor with walls, cover, ramps and decks, made of a few solid shapes.
 *
 * An arena is data (arena_def): boxes, ramps (wedges) and posts (round columns), each a static physics shape and a
 * piece of drawn geometry from the same numbers, so what a kart bumps into is what it sees. The arena's track is a
 * plain straight under the floor (ARENA_TRACK): it gives the sim and the effects the track coordinates they read
 * (flat, up is +y), and is never drawn or collided with.
 *
 * Coordinates in an arena_def are the arena's own: x across, z along, y up, the floor's top at y = 0 and the middle
 * at (0, 0). arena_rt holds them in the world (the arena's middle is the track's middle), with the yaw's sine and
 * cosine worked out once (cw_sincos: the same on every host).
 *
 * The CPU drivers read the boxes and spawns from the sim (sim.box[], arena_rt.spawn) and feel the walls with
 * sim_ray; the HUD's map draws the shapes from above (arena_rt.prim). */
#ifndef RACER_ARENA_H
#define RACER_ARENA_H

#include "game.h"

/* the straight the arena stands on: 200 m along +z, the middle at z = ARENA_CZ */
#define ARENA_LEN 200.0f
#define ARENA_CZ 100.0f
extern const track_spec ARENA_TRACK;

enum {
  AP_BOX,  /* a block: centre x, z, base y, full sizes w (x) h d (z), yaw */
  AP_RAMP, /* a wedge rising from base y at its -z end to base y + h at its +z end (local axes, before the yaw) */
  AP_POST, /* a round column: w is the diameter */
  AP_MOUND, /* a flat-topped mound: base w x d, top h up, every side a slope you drive (MOUND_RUN metres along per metre up) */
};

/* what a shape is dressed as (the arena's look table picks the texture and paint) */
enum {
  AL_FLOOR,   /* the floor itself (the arena makes this one) */
  AL_WALL,    /* the outer walls */
  AL_COVER,   /* blocks to hide behind: crates, containers, slabs */
  AL_DECK,    /* raised floors to drive on */
  AL_RAMP,    /* the ramps up to them */
  AL_TRIM,    /* paint and edging: hazard stripes, kerbs */
  AL_FEATURE, /* the centrepiece and its parts */
  AL_GLOW,    /* lit strips (emissive) */
  AL_PAINT,   /* flat paint on the floor: lines, pads (no texture, never solid) */
  AL_KINDS
};

#define MOUND_RUN 3.0f

typedef struct arena_prim {
  int kind;           /* AP_* */
  int look;           /* AL_* */
  float x, z, y;      /* centre in x and z, the base's height */
  float w, h, d;      /* full sizes */
  float yaw;          /* degrees, about +y */
  bool ghost;         /* drawn only: no collision (a sign, a light) */
  uint32_t col;       /* 0xRRGGBB paint for this one (0: the look's tint) */
} arena_prim;

typedef struct arena_look {
  int tex;            /* textures.h TEX_* or -1 */
  float tint[3];      /* screen colour 0..1, multiplies the texture */
  float gain;         /* brightness of the paint against a dark texture (0: 1) */
  float tex_m;        /* metres per texture repeat */
  float rough, metal;
  float glow;         /* emissive intensity (AL_GLOW) */
} arena_look;

typedef struct arena_def {
  float half_x, half_z;                /* the floor inside the walls */
  float wall_h;                        /* the perimeter wall's height (arena.c builds it round the floor) */
  const arena_prim *prims;
  int nprims;
  const float (*spawns)[3];            /* x, z, yaw (degrees: 0 faces +z) */
  int nspawns;
  const float (*boxes)[3];             /* x, z, the floor height under the box */
  int nboxes;
  arena_look look[AL_KINDS];
  /* capture the flag: each team's base (x, z) at opposite ends, and eight starts (x, z, yaw): the first four red's, round
   * its base, the last four blue's */
  float base[2][2];
  const float (*team_spawns)[3];
  /* king of the mountain: the spots the hill stands on (x, z), the first where it starts; it moves to another every HILL_MOVE */
  const float (*hills)[2];
  int nhills;
} arena_def;

#define ARENA_MAXP 96
typedef struct arena_rt {
  const arena_def *def;
  int n;
  arena_prim prim[ARENA_MAXP];        /* in the world: x and z moved to the track's frame, the perimeter walls and the floor added */
  float sn[ARENA_MAXP], cs[ARENA_MAXP]; /* sin and cos of each yaw */
  t3_vec3 centre;
  float half_x, half_z;
  int nspawn;
  float spawn[16][3];                  /* world x, z, yaw (radians) */
} arena_rt;

/* fills rt from def (world space) */
void arena_rt_build(arena_rt *rt, const arena_def *def);
/* the sim's arena: built from def into the shared layout arena_current() returns (NULL forgets it: a race map) */
const arena_rt *arena_load(const arena_def *def);
/* the static physics shapes on body b (floor and decks on layer track_layer, the rest on wall_layer) */
void arena_phys_build(const arena_rt *rt, cw_body3d b, int track_layer, int wall_layer);
/* the drawn arena: floor, walls, cover, ramps, decks, markings (a map's scenery() calls it first) */
void arena_gfx_build(cw_node scene, const arena_rt *rt, uint32_t seed);
/* a point given in shape i's own frame (x across, y up from its base, z along) in the world, and back (x and z only) */
t3_vec3 arena_prim_point(const arena_rt *rt, int i, float lx, float ly, float lz);
void arena_prim_local(const arena_rt *rt, int i, t3_vec3 p, float *lx, float *lz);
/* the floor height under (x, z) from the shapes alone (the top of the highest floor-like shape there, else 0): the CPU drivers
 * and the HUD map use it to tell the levels apart */
float arena_level(const arena_rt *rt, float x, float z);

/* the arena of the race that is loading (set by the sim at sim_init, for the scenery and the HUD) */
const arena_rt *arena_current(void);

/* modes_gfx.c: capture the flag's bases and flags (on the floor, on a carrier's back) and king of the mountain's hill zone
 * (render only, from the sim's state) */
void modes_gfx_build(cw_node scene, const sim *s);
void modes_gfx_reset(void);
void modes_gfx_frame(const sim *s, float alpha);

/* balloons.c: the balloons on every kart of a battle (render only; nothing is built for a race) */
void balloons_build(cw_node scene, const sim *s); /* after the karts' nodes exist */
void balloons_reset(void);
void balloons_frame(const sim *s, float alpha);   /* every frame */

#endif
