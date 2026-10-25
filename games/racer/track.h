/* racer's tracks: a path through space made of pieces, sampled every metre.
 *
 * A track is a list of pieces (a map's spec[]). Building it walks a turtle
 * along them. The turtle's pose is three angles, not a rotation it keeps
 * multiplying: heading (about the world's up), pitch (nose up) and bank (about
 * the road's own forward), so a vertical loop is a pitch of 2 pi, a corkscrew is
 * a bank of 2 pi, and neither drifts or locks. Position moves by the forward
 * axis each metre. Every metre becomes a tsample: centre, the road's frame
 * (right, up, forward), half width and flags. Everything else reads those:
 * the road and wall meshes (trackmesh.c), the physics mesh, the karts' progress
 * and lateral place (track_locate), the AI's racing line, the items.
 *
 * Gaps (jumps) are ballistic: the frame follows the arc a kart thrown off the
 * lip at the design speed would, the road resumes at the far end.
 *
 * A circuit closes with a solve: pieces marked ADJ (a length or an angle) are
 * nudged by Newton's method until the end meets the start in position and
 * heading. Pieces after the first ADJ run repeat nothing; mark at least four
 * (two turns and two straights, one of them with a slope) in the closing run. */
#ifndef RACER_TRACK_H
#define RACER_TRACK_H

#include <stdbool.h>
#include <stdint.h>

#include "three.h"

#define STEP_M 1.0f /* the sample spacing, metres */
#define KEEP 9999.0f

/* where a sample sits and what it is */
enum {
  TF_GAP = 1u << 0,      /* no road: a jump's flight */
  TF_WALL_L = 1u << 1,   /* guard wall on the left */
  TF_WALL_R = 1u << 2,
  TF_TUNNEL = 1u << 3,   /* a roof over the road */
  TF_BOOST = 1u << 4,    /* a boost strip lies here */
  TF_WATER = 1u << 5,    /* standing water over the road (a ford): drag and spray */
  TF_LOOP = 1u << 6,     /* inside a loop or corkscrew: the road is the kart's floor whatever way up it is */
  TF_CHECK = 1u << 7,    /* a checkpoint line */
  TF_ITEMS = 1u << 8,    /* a row of item boxes */
  TF_OPEN = 1u << 9,     /* an unwalled edge: you can fall off here */
  TF_PLATE = 1u << 10,   /* a clear plate over the road (a glass tube) rather than a stone roof */
  TF_DRIFTY = 1u << 11,  /* slippery: ice, oil, wet */
  TF_ROUGH = 1u << 12,   /* dirt, sand, gravel: slower, bumpier */
  TF_FINISH = 1u << 13,  /* the start/finish line */
  TF_RAMP = 1u << 14,    /* a jump's take-off: the AI wants speed here */
};

/* road surface kinds (the material the road mesh wears and the tyres feel) */
enum { SURF_ROAD, SURF_DIRT, SURF_ICE, SURF_METAL, SURF_GLASS, SURF_WOOD, SURF_KINDS };

typedef struct tsample {
  t3_vec3 p;       /* centre of the road */
  t3_vec3 r, u, f; /* right, up, forward (unit, orthogonal) */
  float hw;        /* half width of the road */
  float s;         /* metres from the start */
  float bank;      /* roll about f, radians (for the camera and the HUD) */
  uint32_t flags;
  uint8_t surf;
  uint8_t walls;   /* wall height class 0 none, 1 low, 2 high, 3 tall */
  uint16_t seg;    /* index of the piece that made it */
} tsample;

/* ── the piece list ─────────────────────────────────────────────────── */

typedef enum {
  P_MOVE,    /* a: length   b: heading change (deg, + left)  c: pitch to (deg)  d: bank to (deg)  e: half width to */
  P_LOOP,    /* a: radius   b: side the loop steps to (+1 right, -1 left)  c: pitch the lead-in has */
  P_CORK,    /* a: length   b: turns (+ rolls to the left over the top)  */
  P_JUMP,    /* a: horizontal distance of the gap (0: where the arc lands level with the lip)   b: design speed m/s, the SLOWEST
             a kart is expected to take off at: faster ones fly further and land on the slope after the gap */
  P_SURF,    /* a: surface kind from here on */
  P_WALLS,   /* a: left class  b: right class  (0 none, 1 low, 2 high, 3 tall) */
  P_TUNNEL,  /* a: 1 roof on, 0 off, 2 glass plate on */
  P_BOOST,   /* a: length of the boost strip that starts here */
  P_WATER,   /* a: length of the ford that starts here */
  P_CHECK,   /* a checkpoint line at this point */
  P_ITEMS,   /* a row of item boxes here */
  P_FLAG,    /* a: TF_* flags to set on the next b metres (b = 0: from here on) */
  P_CLEAR,   /* a: TF_* flags to clear from here on */
  P_END
} piece_kind;

typedef struct piece {
  piece_kind kind;
  float a, b, c, d, e;
  bool adj;  /* the closing solve may nudge a (and b, c for P_MOVE) */
  bool peak; /* P_MOVE: the bank rises to d and falls back to where it began, instead of easing to d */
  const char *note;
} piece;

/* shorthand for map specs */
#define PC(k, ...) { .kind = (k), __VA_ARGS__ }
#define STRAIGHT(len) PC(P_MOVE, .a = (len), .b = 0, .c = KEEP, .d = KEEP, .e = KEEP)
#define TURN(deg, len) PC(P_MOVE, .a = (len), .b = (deg), .c = KEEP, .d = KEEP, .e = KEEP)
/* a banked turn: bank peaks at `bank` degrees (signed like the turn) */
#define BANKED(deg, len, bank) PC(P_MOVE, .a = (len), .b = (deg), .c = KEEP, .d = (bank), .e = KEEP, .peak = true)
#define SLOPE(len, pitch) PC(P_MOVE, .a = (len), .b = 0, .c = (pitch), .d = KEEP, .e = KEEP)
#define HILL(len, deg, pitch) PC(P_MOVE, .a = (len), .b = (deg), .c = (pitch), .d = KEEP, .e = KEEP)
#define MOVE(len, deg, pitch, bank, hw) PC(P_MOVE, .a = (len), .b = (deg), .c = (pitch), .d = (bank), .e = (hw))
#define WIDTH(len, hw) PC(P_MOVE, .a = (len), .b = 0, .c = KEEP, .d = KEEP, .e = (hw))
#define LOOP(radius, side) PC(P_LOOP, .a = (radius), .b = (side))
#define CORKSCREW(len, turns) PC(P_CORK, .a = (len), .b = (turns))
#define JUMP(gap, speed) PC(P_JUMP, .a = (gap), .b = (speed))
#define SURFACE(k) PC(P_SURF, .a = (k))
#define WALLS(l, r) PC(P_WALLS, .a = (l), .b = (r))
#define TUNNEL(on) PC(P_TUNNEL, .a = (on))
#define BOOST(len) PC(P_BOOST, .a = (len))
#define WATER(len) PC(P_WATER, .a = (len))
#define CHECK() PC(P_CHECK)
#define ITEMS() PC(P_ITEMS)
#define FLAG(f, len) PC(P_FLAG, .a = (float)(f), .b = (len))
#define CLEAR(f) PC(P_CLEAR, .a = (float)(f))
/* the closing run: the same pieces with their length (and angle, pitch) free for the solve */
#define ASTRAIGHT(len) PC(P_MOVE, .a = (len), .b = 0, .c = KEEP, .d = KEEP, .e = KEEP, .adj = true)
#define ATURN(deg, len) PC(P_MOVE, .a = (len), .b = (deg), .c = KEEP, .d = KEEP, .e = KEEP, .adj = true)
#define ABANKED(deg, len, bank) PC(P_MOVE, .a = (len), .b = (deg), .c = KEEP, .d = (bank), .e = KEEP, .peak = true, .adj = true)
#define ASLOPE(len, pitch) PC(P_MOVE, .a = (len), .b = 0, .c = (pitch), .d = KEEP, .e = KEEP, .adj = true)
#define END() PC(P_END)

typedef struct track_spec {
  const char *name;
  const piece *pieces;
  int npieces;
  float hw0;       /* half width at the start */
  bool circuit;    /* closes on itself (laps) */
  float start_len; /* the straight the grid sits on is the first piece; unused */
} track_spec;

/* ── the built track ─────────────────────────────────────────────────── */

typedef struct track {
  int n;
  tsample *s;
  float length;
  bool circuit;
  float closure_error; /* metres the solve left between the end and the start (0 for a point to point) */
  int nchecks, check_at[32];  /* sample index of each checkpoint, the finish first (index 0 = the line) */
  int nitem_rows, item_row_at[48];
  int ngaps, gap_from[24], gap_to[24];  /* sample ranges of the jumps */
  int nloops, loop_from[16], loop_to[16];
} track;

/* Builds the path from a spec (no meshes). False and a log line when the
 * spec is empty or the closing solve fails. */
bool track_build(track *t, const track_spec *spec);
void track_free(track *t);

/* the sample at a distance along the track (wraps on a circuit), linearly
 * blended between the two samples round it */
tsample track_at(const track *t, float s);

/* where a world point is against the track */
typedef struct track_loc {
  int i;           /* nearest sample (not a gap sample unless nothing else is near) */
  float frac;      /* 0..1 towards sample i+1 */
  float s;         /* metres along the track */
  float lat;       /* metres to the right of the centre line */
  float height;    /* metres above the road plane along its up */
  float dist;      /* straight distance to the centre line point */
  bool on_road;    /* within the road's width and a little above/below it, not in a gap */
} track_loc;
/* hint: a sample index to search near (the last answer); -1 searches the whole track */
void track_locate(const track *t, t3_vec3 p, int hint, track_loc *out);

/* the world point at metres s along the track, lat to the right, height above the road */
t3_vec3 track_point(const track *t, float s, float lat, float height);

/* signed turn rate (radians per metre, + left) seen from above along the road at s, over `span` metres */
float track_curvature(const track *t, float s, float span);

/* ── meshes ─────────────────────────────────────────────────────────── */

typedef struct track_mesh_opts {
  float wall_h[4];   /* wall heights by class (0 unused) */
  float curb_w;      /* a raised kerb strip at the road's edge, metres (0 none) */
  float tile_v;      /* metres of road per texture repeat along its length */
  float tunnel_clear;/* tunnel roof height above the road centre */
} track_mesh_opts;

/* The visual geometry, chunked along the track so the views cull it. kinds:
 * 0 road, 1 kerb, 2 wall (inner face), 3 wall top and outer, 4 tunnel
 * (inside), 5 tunnel plate (glass), 6 boost strip, 7 underside (the road's
 * bottom and sides, seen from below on loops and jumps). Each chunk is its
 * own geometry; surf selects the road/kerb material. */
enum { TM_ROAD, TM_KERB, TM_WALL, TM_WALLTOP, TM_TUNNEL, TM_PLATE, TM_BOOST, TM_UNDER, TM_KINDS };
typedef struct track_chunk {
  int kind;
  int surf;
  int first, last;           /* sample range */
  t3_vec3 centre;
  float radius;
  t3_geometry *geo;
} track_chunk;
typedef struct track_geo {
  int n;
  track_chunk *c;
} track_geo;
void track_geo_build(track_geo *g, const track *t, const track_mesh_opts *o);
void track_geo_free(track_geo *g);

/* The physics meshes (one for the driving surface, one for the walls and roofs), as welded triangle soup. */
typedef struct track_phys {
  t3_vec3 *v;
  int32_t *idx;
  int nv, nt;
} track_phys;
void track_phys_build(track_phys *road, track_phys *walls, const track *t, const track_mesh_opts *o);
void track_phys_free(track_phys *p);

#endif
