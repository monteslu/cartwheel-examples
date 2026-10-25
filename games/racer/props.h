/* racer's props and track dressing: the toolbox a map's scenery is built from.
 *
 * Everything here is merged or instanced, chunked in space so each view culls what it cannot see, built once at
 * load (nothing is allocated per frame) and driven by a seed, so two maps (or two calls) never look alike.
 *
 * ── the short way ──────────────────────────────────────────────────────────────────────────────────────
 *     prop_set *ps = ps_new(scene_root, "Scenery");
 *     ps_add(ps, PK_PINE, pos, yaw, 1.2f, seed);                  // one pine, 1.2 x its default size
 *     scatter_opts o = scatter_defaults();                         // trees down both sides of the road
 *     o.from_s = 0; o.to_s = t->length; o.spacing = 14; o.lat_min = 6; o.lat_max = 90;
 *     o.kinds[0] = PK_PINE; o.weight[0] = 3; o.kinds[1] = PK_ROCK; o.weight[1] = 1; o.nkinds = 2;
 *     scatter_along(ps, t, &o);
 *     ps_finish(ps);                                               // builds the instanced meshes
 *     dress_start_line(root, t, &style); deco_supports(root, t, ...); ground_plane(root, t, &g); ...
 *
 * ── how a prop is made ─────────────────────────────────────────────────────────────────────────────────
 * A prop is a prop_model: up to four geometries, one per layer (PL_*), because a material has one roughness and
 * one metalness: PL_MAIN (painted, rock, wood, cloth: rough), PL_METAL (steel, chrome, glass), PL_LEAF
 * (foliage: two-sided) and PL_GLOW (emissive: lit windows, neon, lamps; drawn unlit and bright enough to bloom).
 * Every layer carries vertex colours, so a model with a dozen colours is still four draws at most (usually one or
 * two) and an instanced set of them is the same four draws however many copies stand. Colours are linear.
 * A model's own origin is at the middle of its base, y up, +z "front" (the side a sign faces).
 *
 * Models are generated at a default size (prop_default_size) and ps_add scales them; prop_make takes the size.
 * variant picks a different silhouette of the same kind (prop_variants says how many), lod 1 is a cheaper model
 * for things far from the road (scatter_* choose it by distance). Same (kind, size, seed, variant, lod) always
 * gives the same geometry.
 *
 * ── textures ───────────────────────────────────────────────────────────────────────────────────────────
 * Each kind dresses its PL_MAIN material with a textures.h kind when one fits (prop_tex). Models carry uvs
 * projected by their faces' directions at about one repeat per metre, and tex_apply multiplies the vertex colour,
 * so a prop looks right with or without the texture sets.
 */
#ifndef RACER_PROPS_H
#define RACER_PROPS_H

#include "textures.h"
#include "world.h"

/* ── colours ─────────────────────────────────────────────────────────────────────────────────────────── */

typedef struct pcol { float r, g, b; } pcol;
pcol rgb(float r, float g, float b);              /* from screen (sRGB) 0..1 to the linear colours models use */
pcol hex(uint32_t rgb24);                         /* 0xRRGGBB screen colour */
pcol pc_mix(pcol a, pcol b, float t);
pcol pc_mul(pcol a, float k);
pcol glow(pcol c, float gain);                    /* a colour for PL_GLOW: gain > 1 blooms */

/* ── the mesh builder ─────────────────────────────────────────────────────────────────────────────────── */

typedef enum { PL_MAIN, PL_METAL, PL_LEAF, PL_GLOW, PL_COUNT } prop_layer;
typedef struct pm pm;                              /* a model under construction */
typedef struct prop_model {
  t3_geometry *g[PL_COUNT];                        /* NULL where a layer is empty */
  float radius;                                    /* bounding radius about the origin (xz plane), metres */
  float height;                                    /* top of the model above its origin */
} prop_model;

pm *pm_new(uint32_t seed);
prop_model pm_finish(pm *b);                       /* frees the builder; the model's geometries are yours */
void prop_model_free(prop_model *m);
void pm_layer(pm *b, prop_layer l);                /* what the next primitives are made of */
void pm_vary(pm *b, float amount);                 /* faceted brightness jitter per face (0.04 default; rocks like 0.12) */
void pm_jitter(pm *b, float amount);               /* wobble the radii of cylinders and cones (0 off): rock columns, torn towers */
void pm_uv_scale(pm *b, float metres);            /* metres per texture repeat on this model (default 1; props set it from their texture kind) */
void pm_smooth(pm *b, bool on);                    /* round primitives with smooth normals (default: flat facets) */
float pm_rand(pm *b);                              /* 0..1 from the model's seed */
float pm_rands(pm *b);                             /* -1..1 */

/* the transform stack: every primitive is placed by the current matrix; moves compose in the local frame */
void pm_push(pm *b);
void pm_pop(pm *b);
void pm_move(pm *b, float x, float y, float z);
void pm_rotx(pm *b, float rad);
void pm_roty(pm *b, float rad);
void pm_rotz(pm *b, float rad);
void pm_scale(pm *b, float x, float y, float z);
void pm_basis(pm *b, t3_vec3 pos, t3_vec3 right, t3_vec3 up, t3_vec3 fwd); /* local x/y/z become these (road frames) */

/* primitives: sizes in metres, colour c0 at the base fading to c1 at the top (a cheap ambient occlusion) */
void pm_box(pm *b, float w, float h, float d, pcol c0, pcol c1);              /* base centre at the origin */
void pm_box_c(pm *b, float w, float h, float d, pcol c);                      /* centred on the origin */
void pm_cyl(pm *b, float r0, float r1, float h, int seg, pcol c0, pcol c1);   /* base at 0, capped at the top; r1 = 0 is a cone */
void pm_cyl_ex(pm *b, float r0, float r1, float h, int seg, pcol c0, pcol c1, bool cap_bottom, bool cap_top);
void pm_ball(pm *b, float rx, float ry, float rz, int detail, float lumpy, pcol c0, pcol c1); /* faceted lump, centre at origin */
void pm_rock(pm *b, float r, float squash, float lumpy, pcol c0, pcol c1);    /* a ball with a flat base on y = 0 */
void pm_tri_n(pm *b, t3_vec3 p0, t3_vec3 n0, pcol c0, t3_vec3 p1, t3_vec3 n1, pcol c1, t3_vec3 p2, t3_vec3 n2, pcol c2); /* with your own normals */
void pm_quad(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, t3_vec3 p3, pcol c);  /* counter-clockwise seen from the front */
void pm_tri(pm *b, t3_vec3 p0, t3_vec3 p1, t3_vec3 p2, pcol c);
void pm_prism(pm *b, float w, float h, float d, pcol c0, pcol c1);            /* triangular roof section: ridge along z */
void pm_wedge(pm *b, float w, float h, float d, pcol c0, pcol c1);            /* a ramp rising to +z */
void pm_beam(pm *b, t3_vec3 a, t3_vec3 c, float thick, pcol col);             /* a square bar between two points */
void pm_rod(pm *b, t3_vec3 a, t3_vec3 c, float r, int seg, pcol col);         /* a round bar between two points */
void pm_limb(pm *b, t3_vec3 a, t3_vec3 c, float r0, float r1, int seg, pcol c0, pcol c1); /* a tapering round limb between two points */
void pm_cable(pm *b, t3_vec3 a, t3_vec3 c, float sag, float r, pcol col);     /* a hanging cable (catenary-ish, 8 segments) */
void pm_lathe(pm *b, const float *rz, int n, int seg, pcol c0, pcol c1);      /* a turned profile of (radius, y) pairs, smooth */
void pm_ring(pm *b, float R, float r, int segR, int segr, pcol c);            /* a torus about z (wheels, hoops) */
void pm_geo(pm *b, t3_geometry *g, pcol c0, pcol c1);                         /* any three.c geometry, through the matrix (g kept) */

/* A chunked builder: primitives are placed in WORLD space (pmc_at gives the builder of the cell that holds the point;
 * use its push / basis / primitives as usual) and pmc_finish makes one mesh per cell and layer, so a long fence or
 * a hundred pylons cull by cell. tex is a textures.h kind for the PL_MAIN layer or -1. */
typedef struct pmc pmc;
pmc *pmc_new(float chunk_metres, uint32_t seed);
pm *pmc_at(pmc *c, t3_vec3 world_pos);
void pmc_texture_metres(pmc *c, float metres);   /* metres per texture repeat (default 1) */
cw_node pmc_finish(pmc *c, cw_node parent, const char *name, bool cast_shadow, int tex);
/* The shared material of a layer (cached by layer, texture kind, tint and roughness scale; do not release it). */
t3_material *prop_material(int layer, int tex, pcol tint, float rough_scale);

/* append geometries with a transform each into one (frees nothing). The merge helper for hand-made models. */
t3_geometry *geo_merge(t3_geometry *const *g, const t3_mat4 *m, int n);

/* ── the kinds ───────────────────────────────────────────────────────────────────────────────────────── */

typedef enum {
  /* nature */
  PK_ROCK,           /* low-poly boulder, variants: round, tall, slab, chunky */
  PK_BOULDERS,       /* a cluster of rocks of mixed sizes */
  PK_MESA,           /* flat-topped butte with banded strata */
  PK_SPIRE,          /* a leaning rock needle with a cap rock */
  PK_CANYON_WALL,    /* a long cliff face: size is its height, 3 x as long; stepped ledges and strata */
  PK_DUNE,           /* a long soft crescent of sand (or snow) */
  PK_CACTUS,         /* saguaro with arms, variants: tall, short, barrel */
  PK_DEAD_TREE,      /* bare twisted tree */
  PK_PINE,           /* tiered conifer */
  PK_PALM,           /* curved trunk and a fan of fronds */
  PK_BROADLEAF,      /* round-crowned tree, variant tints */
  PK_JUNGLE_TREE,    /* buttressed trunk, wide canopy, hanging vines */
  PK_FERN,           /* giant fern, arching fronds */
  PK_MUSHROOM,       /* big cap on a stem (variant: spotted, tall, clump) */
  PK_ICE_SPIKES,     /* a cluster of ice spikes */
  PK_ICE_BOULDER,    /* faceted ice lump with a glassy sheen */
  PK_SNOW_PINE,      /* conifer loaded with snow */
  PK_CRYSTAL,        /* glowing crystal cluster (variant picks the hue) */
  PK_GRASS,          /* a tuft of tall grass or reeds */
  PK_BUSH,           /* a low shrub */
  /* made things */
  PK_BARREL,         /* oil drum, variants: red, blue, yellow hazard, rusty, toppled */
  PK_CRATE,          /* wooden crate / pallet stack */
  PK_CONTAINER,      /* shipping container, variants stack one to three high */
  PK_TYRE,           /* a tyre lying down */
  PK_TYRE_STACK,     /* a column of tyres */
  PK_WRECK_CAR,      /* rusted car shell, listing; variants: sedan, burnt, flipped, van */
  PK_WRECK_TRUCK,    /* a truck cab and trailer */
  PK_PIPE,           /* a straight pipe run with flanges and a valve */
  PK_PIPE_RACK,      /* an elevated rack carrying several pipes */
  PK_TANK,           /* storage tank on legs */
  PK_SILO,           /* tall grain silo / storage cylinder with a ladder */
  PK_FLARE_STACK,    /* tall stack with a burning flare (glow) */
  PK_COOLING_TOWER,  /* hyperboloid concrete tower */
  PK_CRANE,          /* port gantry or tower crane, variant */
  PK_PYLON,          /* lattice power pylon with insulators */
  PK_LIGHT_POLE,     /* tall flood light pole (glow lamps) */
  PK_STREET_LAMP,    /* curved street lamp with a glowing head */
  PK_NEON_SIGN,      /* tall blade neon sign, variant picks colours */
  PK_BILLBOARD,      /* big board on legs, glowing colour field (no words) */
  PK_SKYSCRAPER,     /* tower with lit windows hashed by seed; variants: slab, stepped, spire, ruined (broken top) */
  PK_RUIN_BUILDING,  /* shattered low block, exposed floors, rubble */
  PK_WATER_TOWER,    /* tank on a leg frame */
  PK_WINDMILL,       /* tower windmill with sails */
  PK_WIND_PUMP,      /* farm wind pump: lattice tower with a fan wheel */
  PK_TENT,           /* striped tent / stall */
  PK_BIGTOP,         /* circus big top */
  PK_FERRIS_WHEEL,   /* a big wheel on A frames with gondolas */
  PK_CAROUSEL,       /* canopy carousel with horses (posts) */
  PK_COASTER_LIFT,   /* a coaster lift hill as backdrop: steel lattice slope with a track and a cart train */
  PK_TEMPLE_BLOCK,   /* big carved stone block, stepped */
  PK_PILLAR,         /* stone pillar, variants: whole, broken, vine wrapped */
  PK_STATUE,         /* a stone idol / warrior statue on a plinth */
  PK_RUIN_ARCH,      /* a ruined arch of stone */
  PK_STAIRS,         /* a flight of stone steps */
  PK_BRIDGE_POST,    /* rope bridge post with a rope stub and lantern */
  PK_DISH,           /* satellite dish on a mount */
  PK_SOLAR_PANEL,    /* a solar panel table */
  PK_STATION_MODULE, /* orbital station module: cylinder with ribs, ports, windows */
  PK_ANTENNA,        /* comms mast / antenna array */
  PK_DEBRIS,         /* floating junk: plates, struts, a torn panel */
  PK_CONE,           /* traffic cone (hazard marker) */
  PK_HAY,            /* hay bale */
  PK_BOLLARD,        /* low post with a glowing cap ring (roadside light that blooms) */
  PK_TORCH,          /* iron torch on a pole with a flame (glow) */
  PK_KINDS
} prop_kind;

const char *prop_name(prop_kind k);
int prop_variants(prop_kind k);
float prop_default_size(prop_kind k);              /* the height (or, for walls and dunes, the nominal size) used by ps_add at scale 1 */
int prop_tex(prop_kind k);                         /* the textures.h kind its PL_MAIN wears, or -1 */
/* One model. size 0 = prop_default_size. */
prop_model prop_make(prop_kind k, float size, uint32_t seed, int variant, int lod);

/* ── instancing ──────────────────────────────────────────────────────────────────────────────────────── */

/* A batch of one geometry and one material drawn many times, chunked into InstancedMesh nodes by place. */
typedef struct prop_batch prop_batch;
prop_batch *pb_begin(t3_geometry *g, t3_material *m);
void pb_add(prop_batch *b, t3_vec3 pos, float yaw, t3_vec3 scale);
void pb_add_q(prop_batch *b, t3_vec3 pos, t3_quat q, t3_vec3 scale);
void pb_add_c(prop_batch *b, t3_vec3 pos, t3_quat q, t3_vec3 scale, pcol tint); /* tint multiplies the vertex colours */
void pb_chunk(prop_batch *b, float metres);        /* cell size for culling (default 70) */
int pb_count(const prop_batch *b);
/* builds the nodes under parent; consumes the batch (and the references it held). Returns the group node. */
cw_node pb_finish(prop_batch *b, cw_node parent, const char *name, bool cast_shadow);

/* A prop set: any number of kinds placed any number of times; models are built the first time a kind is used and
 * shared by every copy. */
typedef struct prop_set prop_set;
prop_set *ps_new(cw_node parent, const char *name);
/* seed picks the variant and the small colour/size jitter of this copy */
void ps_add(prop_set *s, prop_kind k, t3_vec3 pos, float yaw, float scale, uint32_t seed);
void ps_add_ex(prop_set *s, prop_kind k, int variant, int lod, t3_vec3 pos, t3_quat q, t3_vec3 scale, pcol tint, uint32_t seed);
/* Gives a kind its own look for this set: a texture kind (-1: no texture), a tint and a size to build at. Call before
 * the first ps_add of that kind. */
void ps_style(prop_set *s, prop_kind k, int tex, pcol tint, float rough_scale);
/* Where models cast a shadow: default true for tall things (the set decides by the model's height). */
void ps_shadows(prop_set *s, bool on);
int ps_count(const prop_set *s);
void ps_finish(prop_set *s);                       /* creates the meshes (once) */

/* ── scatter ─────────────────────────────────────────────────────────────────────────────────────────── */

/* Ground height under (x, z): maps with hills give the ground function they built their ground with. */
typedef float (*ground_fn)(float x, float z, void *ud);
/* mask: return false to keep a spot empty (a lake, a clearing). weight 0..1 thins instead (a density map). */
typedef float (*scatter_mask_fn)(float x, float z, void *ud);

#define SCATTER_KINDS 8
enum { SIDE_LEFT = 1, SIDE_RIGHT = 2, SIDE_BOTH = 3 };
typedef struct scatter_opts {
  /* the choice of props */
  prop_kind kinds[SCATTER_KINDS];
  float weight[SCATTER_KINDS];
  int nkinds;
  float scale_min, scale_max;      /* 0.8..1.3 */
  /* along the track (scatter_along) */
  float from_s, to_s;              /* metres along; to_s <= from_s means the whole track */
  float spacing;                   /* mean metres between props on each side */
  float jitter;                    /* 0..1 of the spacing, along (1) */
  float lat_min, lat_max;          /* metres out from the road EDGE (the half width plus the wall/kerb margin is added for you) */
  int sides;                       /* SIDE_BOTH */
  float lat_curve;                 /* >1 packs props near lat_min, <1 pushes them out (1 even) */
  /* over an area (scatter_area) */
  float cx, cz, half_x, half_z;    /* box centre and half extents; or a ring: */
  float ring_min, ring_max;        /* distance from (cx, cz); ring_max > 0 selects the ring */
  float density;                   /* per 1000 square metres */
  /* the world */
  float margin;                    /* extra metres kept clear of the road beyond its edge and the prop's own radius (3) */
  bool allow_under;                /* props may stand under/over road above or below them (default false: kept clear) */
  float base_y;                    /* flat ground height when no ground fn (0) */
  ground_fn ground;
  void *ground_ud;
  scatter_mask_fn mask;
  void *mask_ud;
  float lod_dist;                  /* beyond this distance from the road use the cheap model (0: 120) */
  float far_thin;                  /* beyond lod_dist drop this share of props (0..1) so far fields stay light */
  float sink;                      /* metres to sink into the ground (0.1) */
  bool align_slope;                /* lean to the ground's normal (false) */
  uint32_t seed;
} scatter_opts;
scatter_opts scatter_defaults(void);
/* returns the number placed. Nothing lands on, over or under any part of the course (the whole track is searched). */
int scatter_along(prop_set *s, const track *t, const scatter_opts *o);
int scatter_area(prop_set *s, const track *t, const scatter_opts *o);

/* How far a world point is from the road (horizontally, to the nearest road edge, walls included; negative on the
 * road) and the road's height there. Searches every part of the course. */
float road_gap(const track *t, float x, float z, float *road_y);
float road_gap_idx(const track *t, float x, float z, float *road_y, int *sample_index);
/* the nearest road sample horizontally, for those that want more */
int road_nearest(const track *t, float x, float z, float *dist2d);

/* ── dressing the track (dressing.c) ───────────────────────────────────────────────────────────────── */

typedef enum { SUP_TRUSS, SUP_CONCRETE, SUP_TRESTLE, SUP_ICE, SUP_ARCHES, SUP_STRUTS, SUP_STYLES } support_style;
typedef enum { LAMP_POLE, LAMP_STREET, LAMP_GLOW_POST, LAMP_TORCH, LAMP_STYLES } lamp_style;
typedef enum { RAIL_GUARD, RAIL_FENCE, RAIL_ROPE, RAIL_CONCRETE, RAIL_ICE, RAIL_STYLES } rail_style;

typedef struct dress_style {
  pcol main, accent;               /* structure colour, trim/paint colour (0 means the style's own) */
  pcol lamp;                       /* glow colour of lamps and stripes */
  bool has_colours;
  uint32_t seed;
} dress_style;
dress_style dress_defaults(void);

/* the start line: gantry across the road with a chequered banner, a painted chequered line, grid boxes behind it */
void dress_start_line(cw_node parent, const track *t, const dress_style *st);
/* an arch (a lit hoop with a banner) over every checkpoint line but the finish */
void dress_checkpoints(cw_node parent, const track *t, const dress_style *st);
/* pylons or trusses under every stretch of road above the ground (never in jumps' gaps, nor loops' lateral shifts);
 * ground may be NULL (flat at base_y). min_clear: only where the road is higher than this (2). */
void deco_supports(cw_node parent, const track *t, support_style style, ground_fn ground, void *ud, float base_y, float min_clear, const dress_style *st);
/* light poles or glowing posts along both roadsides between s0 and s1 (to <= from: the whole track) */
void deco_lights(cw_node parent, const track *t, float spacing, lamp_style style, float s0, float s1, const dress_style *st);
/* guard rails and fences on every unwalled edge (TF_OPEN samples); other_edges true also rails walled-less edges that fall away */
void deco_rails(cw_node parent, const track *t, rail_style style, const dress_style *st);
/* chevrons at every jump's lip and a landing target at its far end, hazard stripes on the lip */
void deco_jump_marks(cw_node parent, const track *t, const dress_style *st);
/* big painted arrows on the road before every corner sharper than min_curv (radians per metre, 0.012) */
void deco_corner_arrows(cw_node parent, const track *t, float min_curv, const dress_style *st);
/* chevron boards on the outside of every sharp corner (the signpost system); returns how many */
int deco_corner_signs(cw_node parent, const track *t, float min_curv, const dress_style *st);
/* hoops with a banner strip over the road every `spacing` metres from s0 to s1 (no words; stripes of the style's colours) */
void deco_banners(cw_node parent, const track *t, float s0, float s1, float spacing, const dress_style *st);
/* one sign beside the road at s: kind SIGN_* */
enum { SIGN_ARROW_L, SIGN_ARROW_R, SIGN_CHEVRON_L, SIGN_CHEVRON_R, SIGN_WARN, SIGN_FINISH, SIGN_KINDS };
void deco_sign(cw_node parent, const track *t, float s, int side, int kind, const dress_style *st);
/* a string of cables between two points of the world, sagging, carried on nothing (power lines between pylons) */
void deco_cables(cw_node parent, const t3_vec3 *pts, int n, float sag, float r, pcol col);

/* ── ground and water (ground.c) ─────────────────────────────────────────────────────────────────────── */

typedef struct ground_opts {
  float cx, cz;                    /* centre (default: the course's middle) */
  float half;                      /* half extent to the horizon (1800) */
  float base_y;                    /* the flat height when no function (-0.05) */
  ground_fn height;                /* NULL: flat */
  void *height_ud;
  int tex;                         /* textures.h kind or -1 */
  float tex_repeat;                /* metres per texture repeat (8) */
  pcol tint;                       /* base colour (the texture is multiplied by it) */
  pcol tint2;                      /* a second colour mixed in patches by noise (same as tint: none) */
  float patch_scale;               /* metres per colour patch (60) */
  float rough;                     /* 0.95 */
  float near_cell;                 /* finest cell, metres (32: a 4 m grid); the mesh coarsens away from the road */
  float detail_range;              /* how far from the road the finest cells reach, about 3 x this + 45 m (24) */
  bool follow_road;                /* grade the ground to the road where it runs on it (true) */
  float shoulder;                  /* metres past the road edge the grade spreads over (6) */
  float road_drop;                 /* ground kept this far under the road surface (0.25) */
  float darken_edge;               /* shade ground near the road, 0..1 (0.25) */
  float lumpy;                     /* uv-free colour noise strength, 0..1 (0.25) */
  uint32_t seed;
} ground_opts;
ground_opts ground_defaults(void);
/* A big ground following a height function out to the horizon, finest by the road, coarser away from it, with
 * skirts at the seams; returns the node (a Node3D holding the chunks). Heights it uses are what ground_height_at
 * returns, so scatter and dressing can agree with it. */
cw_node ground_plane(cw_node parent, const track *t, const ground_opts *o);
float ground_height_at(const ground_opts *o, float x, float z);
/* A ready basin for lakes: depth below the rim inside an ellipse, smooth edges. Add it to your height function. */
float ground_basin(float x, float z, float cx, float cz, float rx, float rz, float depth, float edge);
/* Rolling hills that stay off the road: 0..amp metres of noise (features about `scale` metres across), flattened to
 * nothing within flat_near metres of the road edge and full strength beyond flat_far. Use it inside your ground fn. */
float ground_hills(const track *t, float x, float z, float amp, float scale, float flat_near, float flat_far, uint32_t seed);
/* value noise in metres for hills: sum of octaves, amplitude amp, features of size scale */
float ground_noise(float x, float z, float scale, float amp, int octaves, uint32_t seed);

typedef struct water_opts {
  pcol color;                      /* what shows through the water (linear) */
  pcol deep;                       /* the body colour where nothing refracts */
  float clarity;                   /* 0 murky .. 1 clear */
  float wave_height;               /* metres, 0..2 (0.25) */
  float wave_length;               /* metres (18) */
  float wave_dir;                  /* degrees, 0 = +x (30) */
  float choppy;                    /* 0..1 how sharp the crests are (0.4) */
  float foam;                      /* 0..1 whitecaps and ripple strength (0.5) */
  float reflect;                   /* 0..1 how mirror-like (0.5) */
  float flow_x, flow_z;            /* ripple drift, metres per second (0.05, 0.02) */
  bool refract;                    /* planar refraction on (costs a render): default true */
  int grid;                        /* subdivisions (200) */
} water_opts;
water_opts water_defaults(void);
/* A sheet of the engine's water: reflects the scene, refracts, waves moved on the GPU. size_x by size_z metres about
 * (cx, cz) at height level. Terrain above `level` hides the parts that should be dry (carve a basin with
 * ground_basin for a lake of any outline). Returns the Water3D node. */
cw_node water_surface(cw_node parent, float cx, float cz, float size_x, float size_z, float level, const water_opts *o);
/* A lake: water_surface sized to an ellipse's bounding box. */
cw_node water_lake(cw_node parent, float cx, float cz, float rx, float rz, float level, const water_opts *o);
/* A flooded road: a gleaming film of water lying on every TF_WATER stretch (ripples drift, splashes come from the
 * fx module), `extra` metres wider than the road each side, `depth` above the road surface. Call water_frame
 * every frame (map scenery_frame) to move the ripples. */
cw_node water_along_track(cw_node parent, const track *t, float extra, float depth, const water_opts *o);
/* The same film over any stretch s0..s1 of the track. */
cw_node water_along_range(cw_node parent, const track *t, float s0, float s1, float extra, float depth, const water_opts *o);
/* A river: a ribbon of the same film through a list of points (width each end metres, flows along the list). */
cw_node water_ribbon(cw_node parent, const t3_vec3 *pts, int n, float width, const water_opts *o);
void water_frame(double time);                     /* scrolls the film's ripples; cheap */

/* ── the showcase ────────────────────────────────────────────────────────────────────────────────────── */

/* A row of every prop along the start straight (RACER_PROPS=1 in the proving ground). */
void props_gallery(cw_node parent, const track *t);
/* Free the cached models and materials (a map change). */
void props_reset(void);

#endif
