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
 * sim_ray; the HUD's map draws the shapes from above (arena_rt.prim).
 *
 * An arena may also be open (no floor or wall round it: decks over the void, a fall puts the kart back), lay rails round
 * its decks (arena_prim.rail: arena.c leaves the ways on to the next deck open), and have teleport pads (arena_tele: the
 * sim moves a kart that drives onto one to its far end, arena.c draws the pad and modes_gfx.c its light) and boost strips
 * (arena_boost). A long one gives the CPU drivers a route (points and the runs between them) and names the rounds it is
 * made for (arena_def.modes: the capture the flag maps are listed under that round alone). An open arena may also stand on
 * a rock of its own shape (arena_terrain: one continuous top with slopes, cut to any outline, a rim round its edge). */
#ifndef RACER_ARENA_H
#define RACER_ARENA_H

#include "game.h"

/* the straight the arena stands on: 800 m along +z (room for a long capture the flag map), the middle at z = ARENA_CZ */
#define ARENA_LEN 800.0f
#define ARENA_CZ 400.0f
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
  float rail;         /* a deck: a rail this high round its edge (AL_TRIM), stopping where another deck carries on, 0 none */
} arena_prim;

/* a teleport pad: drive onto it (within r of x, z, on the floor at y) and you are put at the far end, facing tyaw (degrees)
 * at `speed` m/s; then a moment (TELE_COOL ticks) before a pad takes that kart again. in_yaw is the way a driver comes onto
 * it (the CPU drivers line up on it from 16 m out) */
typedef struct arena_tele {
  float x, z, y, r;
  float tx, tz, ty, tyaw;
  float speed;
  float in_yaw;
  uint32_t col;       /* 0xRRGGBB of its light */
} arena_tele;
#define TELE_COOL 75

/* a boost strip: a kart on its wheels on it (w across, d along, turned yaw degrees, its floor at y) gets a pad's boost,
 * the way a race's boost pads give one */
typedef struct arena_boost {
  float x, z, y, w, d, yaw;
} arena_boost;

typedef struct arena_look {
  int tex;            /* textures.h TEX_* or -1 */
  float tint[3];      /* screen colour 0..1, multiplies the texture */
  float gain;         /* brightness of the paint against a dark texture (0: 1) */
  float tex_m;        /* metres per texture repeat */
  float rough, metal;
  float glow;         /* emissive intensity (AL_GLOW) */
} arena_look;

/* a rock to drive on (arena_def.terrain): one continuous top over the void, any outline and any gentle slopes, from the
 * map's own function. at(x, z, &top) says how far (x, z) is inside the rock (metres, negative off it) and how high its top
 * is there (asked anywhere in the area, so it must run on past the edge smoothly; plain arithmetic only: the sim builds its
 * collision from it, and every host must get the same numbers). arena.c samples it every `cell` metres: the top is drawn
 * and collides as triangles cut to the outline (marching squares, so a diagonal edge is a straight edge), with a rough rim
 * round the edge (a wall to the karts, arena_def.fence over it) and the rock hanging under it. The CPU drivers and the HUD
 * read its height (arena_level) and its outline (arena_terrain_runs) */
typedef struct arena_terrain {
  float x0, z0, x1, z1;                /* the area it may cover (arena frame) */
  float cell;                          /* metres between samples */
  float (*at)(float x, float z, float *top);
  float lip;                           /* the drawn rim's height, 0 none (and nothing stops a kart at the edge) */
  float depth, belly;                  /* the rock under the top: this deep at the edge, deeper by belly per metre in from it */
  uint32_t top_col, side_col;          /* 0xRRGGBB tints */
  int tex;                             /* textures.h kind for the top, and metres per repeat */
  float tex_m;
  int side_tex;                        /* textures.h kind for the rim, the face and the underside (0: the top's) */
} arena_terrain;

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
  /* an open arena has no floor and no wall round it: decks over the void (rails where it says), and a kart that drops off
   * falls to the kill plane (sim_battle.inc) and is put back */
  bool open;
  float fence;                         /* rails (AL_TRIM) collide at least this high, an unseen fence over the drawn rail: a kart
                                        * that hits one flat out at a corner otherwise bounces up over it. 0: as drawn */
  const arena_tele *teles;
  int nteles;
  const arena_boost *boosts;
  int nboosts;
  /* the CPU drivers' way round (a long map whose decks make corridors): points (x, z) on the floor and the straight runs
   * between them (pairs of indices). A driver that cannot see its goal heads for the point it can see that makes the
   * shortest way there along the runs */
  const float (*route)[2];
  int nroute;
  const uint8_t (*links)[2];
  int nlinks;
  /* the rounds it is made for (1 << AM_*): the arena select lists it under those. 0: battle and king of the mountain */
  int modes;
  /* a rock to drive on as well as (or instead of) the decks, NULL none */
  const arena_terrain *terrain;
} arena_def;

/* the rounds an arena is listed under */
static inline int arena_modes(const arena_def *d) { return d->modes ? d->modes : (1 << AM_BALLOON) | (1 << AM_KOTM); }

#define ARENA_MAXP 400
#define ARENA_MAXT 8
#define ARENA_MAXB 24
#define ARENA_MAXR 24
typedef struct arena_rt {
  const arena_def *def;
  int n;
  arena_prim prim[ARENA_MAXP];        /* in the world: x and z moved to the track's frame, the perimeter walls and the floor added */
  float sn[ARENA_MAXP], cs[ARENA_MAXP]; /* sin and cos of each yaw */
  t3_vec3 centre;
  float half_x, half_z;
  int nspawn;
  float spawn[16][3];                  /* world x, z, yaw (radians) */
  int ntele;
  struct arena_tele_rt {
    t3_vec3 at, to;                    /* the pad (its floor) and the far end (its floor), world */
    float r, yaw, speed, in_yaw;       /* yaws in radians */
    uint32_t col;
  } tele[ARENA_MAXT];
  int nboost;
  struct arena_boost_rt {
    t3_vec3 at;                        /* its middle, on its floor, world */
    float hw, hd, sn, cs;              /* half sizes, the sine and cosine of its yaw */
  } boost[ARENA_MAXB];
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
/* the same, only the shapes whose top is at most y + 1.5 (what something at height y stands on: under a deck, the floor) */
float arena_level_below(const arena_rt *rt, float x, float y, float z);

/* the arena's rock (arena_def.terrain): is the point p (world) standing on it (its top within 2.5 m under p, or over it).
 * The CPU drivers take two points both on the rock for one level, however far apart in height: its slopes are driven */
bool arena_on_terrain(const arena_rt *rt, t3_vec3 p);
/* its outline for a map drawn flat: n runs (x0, z0, x1, z1, top) in the arena's frame, rows of a coarse grid */
int arena_terrain_runs(const arena_def *d, const float (**runs)[5]);

/* the arena of the race that is loading (set by the sim at sim_init, for the scenery and the HUD) */
const arena_rt *arena_current(void);

/* modes_gfx.c: capture the flag's bases and flags (on the floor, on a carrier's back), king of the mountain's hill zone and
 * the arena's teleport pads' light (render only, from the sim's state) */
void modes_gfx_build(cw_node scene, const sim *s);
void modes_gfx_reset(void);
void modes_gfx_frame(const sim *s, float alpha);

/* balloons.c: the balloons on every kart of a battle (render only; nothing is built for a race) */
void balloons_build(cw_node scene, const sim *s); /* after the karts' nodes exist */
void balloons_reset(void);
void balloons_frame(const sim *s, float alpha);   /* every frame */

#endif
