/* bomberdude's 3D side: everything here only reads the simulation (game.h)
 * and draws it. Tile (x, y) is centred at world (x - 7, 0, y - 6): one metre
 * tiles, +x east, +z toward the camera (south), y up. */
#ifndef BOMBERDUDE_WORLD_H
#define BOMBERDUDE_WORLD_H

#include "game.h"
#include "three.h"

static inline float wx(int sub_x) { return (float)sub_x / SUB - GW * 0.5f; }
static inline float wz(int sub_y) { return (float)sub_y / SUB - GH * 0.5f; }
static inline float tx_w(int cx) { return cx + 0.5f - GW * 0.5f; }
static inline float tz_w(int cy) { return cy + 0.5f - GH * 0.5f; }

/* textures.c: procedural PBR sets (albedo sRGB, normal, roughness, ao), made
 * on first use, deterministic integer hashing (debug field tex.hash) */
enum {
  TEX_GRASS, TEX_BRICK, TEX_CRATE, TEX_ICE, TEX_SNOW, TEX_METAL, TEX_BASALT, TEX_MOSS, TEX_JUNGLE, TEX_CANDY, TEX_FROSTING,
  TEX_ASPHALT, TEX_PANEL, TEX_DECK, TEX_HULL, TEX_SAND, TEX_SANDSTONE, TEX_FUR, TEX_RUBBER, TEX_LEAF, TEX_BARK, TEX_KINDS
};
typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
const tex_set *tex_get(int kind);
void tex_apply(t3_material *m, int kind, float repeat, float normal_scale, float ao_intensity);
void tex_free_all(void);
extern uint32_t tex_hash;
t3_color lin(float r, float g, float b);           /* screen colour -> linear */
t3_geometry *geo_rounded_box(float w, float h, float d, float r, int seg);

/* models.c: critters, bombs, power-ups */
enum { CRIT_FOX, CRIT_BUN, CRIT_BEAR, CRIT_FROG, CRIT_KINDS };
extern const char *const CRIT_NAME[CRIT_KINDS];
extern const float TEAM_RGB[MAXP][3];
typedef struct critter {
  t3_object *root;          /* placed by world.c */
  t3_object *body, *head, *ear[2], *arm[2], *foot[2], *tail, *eyes, *lids[2], *mouth_o, *mouth_smile, *cap;
  t3_object *bubble;        /* the bubble shield */
  t3_material *team_mat;
  int kind;
} critter;
void critter_build(critter *c, int kind, int team);
t3_object *model_bomb(int team, t3_material **glow_out, t3_material **band_out, t3_object **spark_out);
t3_object *model_item(int kind);

/* fx.c: blasts and everything that flies */
void fx_init(cw_node scene, int theme);
void fx_reset(void);
void fx_events(const sim *s);       /* this tick's sim events -> effects */
void fx_tick(const sim *s);         /* fixed tick */
int fx_shake(void);                 /* camera shake ticks */
void fx_confetti(t3_vec3 at);       /* a celebration */

/* scenery.c: the arena's surroundings and weather */
void scenery_build(cw_node scene, int theme, uint32_t seed);
void scenery_tick(const sim *s, int ticks);
void scenery_reset(void);
void scenery_density(float k); /* share of the grass and crowd drawn, 0..1 (cw_quality_density) */
extern uint32_t scenery_hash;

/* world.c: the arena itself, players, bombs, items */
void world_build(cw_node scene, const sim *s, const int crit[MAXP]);
void world_reset(void);
void world_tick(const sim *s);      /* fixed tick: animation state */
void world_title_pose(int ticks);   /* the title: critters on parade */
void world_set_humans(uint8_t mask); /* bit p: player p is a person (the ring under their feet) */
void world_podium(const sim *s, const int wins[MAXP], int winner); /* the match is over: the podium */

#endif
