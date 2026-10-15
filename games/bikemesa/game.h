/* BikeMesa: shared state between the simulation (bike.c), the tracks (track.c),
 * the look (view.c, models.c, textures.c) and the app (main.c). Everything
 * that feeds the simulation advances only in fixed_update, from the tick
 * action snapshot (cartwheel/input.h), cartwheel/math.h, Box2D and (a
 * crash) Box3D. */
#ifndef BIKEMESA_GAME_H
#define BIKEMESA_GAME_H

#include "cartwheel/audio.h"
#include "cartwheel/input.h"
#include "cartwheel/physics2d.h"
#include "cartwheel/physics3d.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"

/* the display name lives here only: naming is not settled */
#define GAME_TITLE "BikeMesa"

#define TICK_HZ 60
#define BIKEMESA_TRACKS_MAX 16
#define MAX_POPUPS 16
#define TRACK_HALF_W 2.0f   /* the riding surface spans z -W..W */
/* four lanes across it: lane 0 at the back (-z),
 * lane 3 at the front, nearest the camera */
#define LANES 4
#define LANE_W 0.9f
static inline float lane_z(float lane) { return (lane - (LANES - 1) * 0.5f) * LANE_W; }

typedef enum {
  GS_TITLE,  /* title over a scenic ride-by */
  GS_SELECT, /* track select */
  GS_COUNT,  /* 3-2-1 on the start line */
  GS_RIDE,   /* riding */
  GS_CRASH,  /* the rider tumbling; respawn at the checkpoint after */
  GS_FINISH, /* crossed the line: the result panel */
  GS_REPLAY, /* a wipeout replayed in slow motion (the sim waits) */
  GS_CHARACTER, /* the players pick a rider and a bike each over a rotating preview; pads join here */
} game_state;

typedef enum {
  SFX_PEDAL, SFX_LAND, SFX_CRASH, SFX_THUD, SFX_CHECK, SFX_FINISH, SFX_TRICK, SFX_SELECT, SFX_LOCKED, SFX_COUNTDOWN,
  SFX_GO, SFX_BOOST, SFX_WHOOSH, SFX_STAR, SFX_SLOWCRASH, SFX_COUNT
} sfx_id;

/* tracks.inc (tools/make-tracks.mjs) */
typedef struct track_piece { const float *pts; int count; } track_piece;
typedef struct track_desc {
  const char *name, *theme;
  bool ready;                     /* tuned: shown on the select screen (others only with BIKEMESA_ALL_TRACKS) */
  const char *scene;
  int par;                        /* seconds for the gold medal */
  const track_piece *pieces;
  int piece_count;
  const float *checks;            /* x, y pairs */
  int check_count;
  const float *boosts;            /* x0, x1 pairs */
  int boost_count;
  const float *kickers;           /* x0, x1, h triples: built ramps (planks) */
  int kicker_count;
  float finish_x, finish_y, min_y, max_y, length;
  const float *bridges;           /* x0, y0, x1, y1: plank bridges that fall behind you */
  int bridge_count;
  const float *ice;               /* x0, x1: icy patches */
  int ice_count;
  float gravity, grip, drag;      /* m/s^2 (10 on Earth), tyre grip and air drag factors */
  const char *music;              /* audio/<music>.ogg */
  const float *movers;            /* kind, x, y, len: moving set pieces (MOVER_*) */
  int mover_count;
  const float *highs;             /* x0, x1, y0, y1: the high routes' plank decks, sloping from y0 to y1 */
  int high_count;
  const float *loops;             /* x, y, r: vertical loops (the bottom at x, y; ridden clockwise as seen) */
  int loop_count;
  const float *pinches;           /* x0, x1, lo, hi: only lanes lo..hi open from x0 to x1 */
  int pinch_count;
  const float *ramps;             /* x0, up, h, top, down, lo, hi: a ramp on lanes lo..hi only */
  int ramp_count;
} track_desc;
/* the height of track t's ramps in lane `lane` above the ground at x (0: none there) */
float track_ramp_h(const track_desc *t, float x, int lane);
/* where the track narrows: lanes lo..hi open over [x0, x1); with a wall, the
 * closed lanes end at x0 in a barrier (a rider still in one crashes). The
 * explicit pinches, and each loop's: its run-in on the back lanes (0, 1),
 * its run-out on the front ones (2, 3) */
typedef struct narrow { float x0, x1; int lo, hi; bool wall; } narrow;
#define MAX_NARROWS 12
#define NARROW_TAPER 24.0f /* m: the lines and cones close in over this before a narrow (fair warning) */
#define WIDEN_TAPER 10.0f  /* and open out over this after */
int track_narrows(const track_desc *t, narrow *out);
bool track_lane_open(const track_desc *t, float x, int lane);
extern int TRACK_COUNT;
const track_desc *track_get(int i);

/* a floating trick / bonus label (render only) */
typedef struct popup {
  char text[40];
  int points, age;
  float x, y;
} popup;

/* save data in cw_app_desc.save */
typedef struct save_track {
  int32_t best_ticks;  /* 0 = never finished */
  int32_t best_score;
  uint8_t medal;       /* 0 none, 1 bronze, 2 silver, 3 gold */
  uint8_t cleared, shown, stars; /* stars: 1 finished, 2 and 3 for trick scores (STAR2, STAR3) */
} save_track;
typedef struct save_data {
  uint32_t magic;
  uint16_t version, size;
  uint32_t checksum;
  save_track tr[BIKEMESA_TRACKS_MAX];
} save_data;

/* bike.c: the bicycle and its rider in a Box2D world (a crash: in crash.c's Box3D world) */
typedef struct bike_input {
  float pedal, brake, lean; /* 0..1, 0..1, -1 back .. +1 forward */
  bool hold;                /* feet down: the wheels locked (the start line) */
  bool hop;                 /* the hop button went down this tick */
  uint8_t pose;             /* POSE_*: the pose held (the trick system's) */
} bike_input;
enum { POSE_NONE, POSE_NO_HANDS, POSE_NO_FEET, POSE_SUPERMAN };

typedef struct bike_state {
  /* pose for the renderer: bodies' interpolated transforms come from bike_pose */
  bool alive;                /* false while the rider is down */
  bool grounded_rear, grounded_front;
  int air_ticks, wheelie_ticks;
  float air_rot;             /* summed frame rotation while airborne (rad) */
  float speed;               /* m/s along the frame */
  float crank;               /* pedal crank angle (rad) */
  float lean_shown;          /* the rider's lean, eased (render) */
  float x, y, angle;         /* frame, last step */
  float vx, vy;
  int hops;
  int loops;                 /* loops ridden all the way round (this ride) */
} bike_state;

typedef struct bike_pose {
  cw_xform2 frame, rear, front;
  float crank, lean;
  float z;                   /* riding: the lane's depth the rider is drawn at (main.c fills it in) */
  cw_xform2 limb[10];        /* RAG_*: the riding pose, in the frame's plane */
  cw_xform2 cap;             /* the cap on the head */
  /* a crash: the rider, the bike and the cap tumbling in the 3D world (world
   * space, z included); the 2D fields above are not used */
  bool ragdoll;
  cw_xform3 limb3[10], frame3, rear3, front3, cap3;
} bike_pose;
enum { RAG_TORSO, RAG_HEAD, RAG_UARM_L, RAG_FARM_L, RAG_UARM_R, RAG_FARM_R, RAG_THIGH_L, RAG_SHIN_L, RAG_THIGH_R, RAG_SHIN_R, RAG_COUNT };

#define MAX_RIDERS 4
void bike_world_create(const track_desc *t);
void bike_world_destroy(void);
/* several riders share one world (local multiplayer): set the count after
 * the world is made, then pick a rider before bike_spawn / bike_get / the
 * other per-rider calls; bike_step_all steps them all, in[i] being rider i's */
void bike_set_riders(int n);
int bike_riders(void);
int bike_bumps(void); /* contacts begun between two riders' bikes this world */
void bike_select(int i);
void bike_set_lane(int lane); /* the selected rider's lane: riders in one lane bump, other lanes pass through */
int bike_lane(void);
void bike_step_all(const bike_input *in, const bool *boosting);
float bike_loop_shift(void); /* 0..1: how far across the selected rider is in a loop's sideways shift (two lanes) */
void bike_spawn(float x, float y, float v0); /* v0: a rolling start (m/s) */
void bike_step(const bike_input *in, bool boosting);
const bike_state *bike_get(void);
bool bike_crashed_this_tick(void);
bool bike_bailed_this_tick(void);                       /* the crash was landing in a pose */
void bike_landing_more(float *err, int pose_ticks[4]);  /* the last landing: frame vs slope, ticks in each pose */
void bike_air_poses(int pose_ticks[4]);                 /* this air so far: ticks in each pose */
int bike_landed_this_tick(float *rotation, int *air_ticks); /* 1 on a landing */
void bike_pose_get(float alpha, bike_pose *out);
void bike_set_z(float z); /* the selected rider's drawn depth this tick: a crash starts from it */
bool bike_crash_focus(t3_vec3 *at, t3_vec3 *vel); /* down: where the tumbling rider is (the camera frames it) */
float track_height_at(float x, bool *found);
uint32_t bike_hash(uint32_t h);
int bike_planks(float alpha, cw_xform2 *out, float *half_w, int cap);
enum { MOVER_NONE, MOVER_DRAWBRIDGE, MOVER_SEESAW, MOVER_WRECK, MOVER_LOGS, MOVER_TRAIN };
/* a set piece's pose for drawing; a, b: its size, or the wrecking ball's
 * pivot, or the train's depth z and length */
typedef struct mover_pose { int kind; cw_xform2 x; float a, b; } mover_pose;
int bike_movers(float alpha, mover_pose *out, int cap);

/* models.c: the 3D bike and rider, driven by bike_pose */
/* a new attribute onto a geometry, which keeps the only reference */
static inline void set_attr(t3_geometry *g, t3_attr_slot slot, t3_attribute *a) {
  t3_geometry_set_attribute(g, slot, a);
  t3_release(a);
}
static inline void set_index(t3_geometry *g, t3_attribute *a) {
  t3_geometry_set_index(g, a);
  t3_release(a);
}
void models_build(cw_node parent);
void models_set_selection(int idx, int character, int bike); /* rider idx's pick, before models_build_rider */
void models_turntable(int idx, float angle, float center_x);
void models_sync(const bike_pose *p);
void models_build_rider(cw_node parent, int idx); /* rider idx's bike and rider, in its colours */
void models_sync_rider(int idx, const bike_pose *p, float z);
void models_far(int idx, bool far); /* the far look: one draw (split screen's other riders) */
void models_refresh(int idx); /* rider idx's world matrices, after posing it again mid-frame */
void models_forget(void);
/* geometries placed by their matrices merged into one (positions, normals,
 * uvs, colours: the attributes in mask, geo_merge_mask's bits) */
bool geo_mergeable(const t3_geometry *g);
unsigned geo_merge_mask(const t3_geometry *g);
t3_geometry *geo_merge(const t3_geometry *const *gs, const t3_mat4 *const *mats, int n, unsigned mask);

/* crash.c: the 3D world a crash tumbles in, built once a track's scenery is:
 * static shapes from the same track data view.c draws (the ribbon and its
 * cliff faces, the ramps, the bank, the rocks, the ground, the decks, loops
 * and barriers); bike.c adds the rider, the bike and the moving pieces */
void crash_world_build(const track_desc *t);
void crash_world_destroy(void);
cw_world3d crash_world(void);
enum { L3_SCENE, L3_RIDER, L3_BIKE, L3_HAND }; /* the crash world's layers */

/* view.c: the track ribbon and the scenery around it, per theme */
void view_build(cw_node parent, const track_desc *t, int track_index);
void view_tick(float rider_x, float rider_y, float speed, bool grounded);
void view_forget(void);
void view_density(float k); /* share of the grass tufts drawn, 0..1 (cw_quality_density) */
void view_frame(float alpha); /* moving scenery (the bridge planks) at the frame's alpha */
void view_grass(const cw_node *cameras, int n); /* the grass the cameras see, posed once for the frame (after they are placed); none: no grass */
void fx_dust(float x, float y, float amount);
void fx_burst(float x, float y, int kind);
float view_bank_y(float x, float z); /* the bank behind the track (z <= -TRACK_HALF_W - 0.3) */
#define VIEW_ROCK_PTS 14
typedef struct view_rock { t3_vec3 p[VIEW_ROCK_PTS]; } view_rock; /* a rock's hull points, in the world */
int view_rocks(const view_rock **out);
extern uint32_t wind_hash;

/* textures.c: procedural PBR texture sets */
enum { TEX_ROCK, TEX_DIRT, TEX_GRASS, TEX_SANDSTONE, TEX_SNOW, TEX_ASPHALT, TEX_WOOD, TEX_BARK, TEX_LEAVES, TEX_SAND,
       TEX_MOSS, TEX_METAL, TEX_FABRIC, TEX_FLAKE, TEX_COTTON, TEX_GRANITE, TEX_BASALT, TEX_MOSSROCK, TEX_CONCRETE, TEX_KINDS };
typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
const tex_set *tex_get(int kind);
void tex_apply(t3_material *m, int kind, float rx, float ry, float normal_scale, float ao_intensity);
void tex_free_all(void);
extern uint32_t tex_hash;

/* main.c */
void game_sfx(sfx_id id, float volume);

#endif
