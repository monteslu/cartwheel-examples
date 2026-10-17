/* chromite: a vertical shooter over a chrome fortress, after Chromium
 * B.S.U. (2000), on cartwheel and three.c (three.js r186).
 * Shared state between the files of the game. */
#ifndef CHROMITE_GAME_H
#define CHROMITE_GAME_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "cartwheel/audio.h"
#include "cartwheel/input.h"
#include "cartwheel/math.h"
#include "cartwheel/platform.h"
#include "cartwheel/render3d.h"
#include "cartwheel/scene.h"
#include "cartwheel/scene_file.h"
#include "three.h"

#define CHROMITE_TITLE "CHROMITE" /* the display name: one place to rename the game */

/* ── the simulation (sim.c): fixed 60 Hz ticks, cartwheel/math.h only ──
 *
 * The rules are the original's, in its units: the play plane runs x -11..11
 * and y -9..9 (+y up the screen), and its numbers are per 50 fps frame. A
 * 60 Hz tick is 5/6 of one of those frames (K below): rates scale by K,
 * per-frame decays d become d^K, and script times are 50 fps frames. */

#define K (50.0f / 60.0f)

#define FIELD_X 11.0f
#define FIELD_Y 9.0f
#define HERO_X 10.0f
#define HERO_Y 7.5f

enum { EK_STRAIGHT, EK_OMNI, EK_RAYGUN, EK_TANK, EK_GNAT, EK_BOSS0, EK_BOSS1, EK_KINDS };
enum { GUN_DEFAULT, GUN_GREEN, GUN_PURPLE, GUNS };
enum { PU_SHIELDS, PU_SUPER, PU_REPAIR, PU_AMMO0, PU_AMMO1, PU_AMMO2, PU_KINDS };
enum { EA_HEAVY, EA_FLAK, EA_SLUG, EA_BEAM, EA_NEEDLE, EA_KINDS }; /* enemy ammo 0..4 */

#define MAX_ENEMIES 256
#define MAX_EBULLETS 1536
#define MAX_HBULLETS 384
#define MAX_POWERUPS 32
#define MAX_SCRIPT 4096
#define MAX_EVENTS 128

#define AMMO_MAX 150.0f
#define HERO_HULL 500.0f
#define HERO_SHIELDS 500.0f
#define LIFE_SCORE 50000

typedef struct enemy {
  bool live;
  int kind;
  uint32_t id;
  float x, y;
  float lmx, lmy;   /* the steering velocity (the original's lastMoveX/Y) */
  float smx, smy;   /* knockback (secondaryMove), decays */
  float vel;        /* tanks: the eased forward speed */
  float hp;         /* the original's damage: negative, dead above 0 */
  float max_hp;
  float age;        /* in 50 fps frames */
  float shoot_interval, pre_fire;
  float rand_fact, rand_move;
  float aim_x, aim_y;
  int target;       /* gnats: -1 the hero, else an enemy slot (a boss) */
  int swap;
  float phase;      /* render-only spin phase seed */
  float hit_flash;  /* presentation: 1 when hit, fades */
  float smoke;      /* bosses: time to the next damage puff */
} enemy;

typedef struct ebullet { bool live; int kind; float x, y, vx, vy; } ebullet;
typedef struct hbullet { bool live; int gun; float x, y, vy, dmg; float age; } hbullet;
typedef struct powerup { bool live; int kind; float x, y, vx, vy, age; } powerup;

typedef struct script_item { int32_t at; int16_t kind, arrow; float x, y; } script_item;

/* things that happened this tick, for sound and effects */
enum {
  EV_KILL,       /* an enemy died: a = kind */
  EV_HIT,        /* a hero bullet hit: a = gun */
  EV_HERO_HIT,   /* the hero took a hit: a = ammo kind */
  EV_HERO_DIE,
  EV_PICKUP,     /* a = power-up kind */
  EV_SHOT,       /* the hero fired: a = gun */
  EV_ESHOT,      /* an enemy fired: a = ammo kind */
  EV_LIFE,       /* an extra life */
  EV_LOSE,       /* an enemy got past: a life gone */
  EV_BOMB,       /* the super bomb went off */
  EV_BOSS_DIE,
  EV_RESPAWN,
  EV_SMOKE,      /* a damaged boss puffs smoke */
  EV_ARMED,      /* the self-destruct is armed */
};
typedef struct sim_event { int type, a; float x, y; } sim_event;

typedef struct hero {
  float x, y, smx, smy;
  float damage, shields; /* damage -500 full hull; shields 500, super 1000 */
  int lives;
  float ammo[GUNS];      /* stock 0..150 (gun 0's is the extra pair) */
  bool gun_on[GUNS];
  float cool[GUNS];
  int gun2_side;
  float dont_show;       /* frames invisible after a death */
  float bomb;            /* the super bomb's ring: 0 off, grows to 300 */
  float armed;           /* the self-destruct's window */
  float engine_heat;     /* presentation: red as the hull goes */
  float shield_flash, damage_flash;
} hero;

typedef struct sim_input { float x, y; bool fire, item; } sim_input;

enum { PLAY_ON, PLAY_LEVEL_OVER, PLAY_DEAD };

typedef struct sim {
  int level;              /* 1.. */
  float skill;
  int tick;
  float frame;            /* the level's clock in 50 fps frames */
  int mode, mode_ticks;
  cw_rng rng;
  hero h;
  int64_t score, next_life;
  enemy en[MAX_ENEMIES];
  ebullet eb[MAX_EBULLETS];
  hbullet hb[MAX_HBULLETS];
  powerup pu[MAX_POWERUPS];
  script_item script[MAX_SCRIPT];
  int script_n, script_i;
  uint32_t next_id;
  int boss;               /* the boss's slot, or -1 */
  float success;          /* level over: counts down */
  float past_warn;        /* an enemy low on the screen: the red bar */
  float hero_death;       /* game over zoom */
  int kills, kills_by[EK_KINDS], losses, deaths, shots;
  float boss_hp;          /* 0..1 for the HUD, -1 no boss */
  sim_event ev[MAX_EVENTS];
  int ev_n;
} sim;

void sim_new_game(sim *S, int level, float base_skill, uint32_t seed);
void sim_next_level(sim *S);
void sim_step(sim *S, const sim_input *in);
uint32_t sim_hash(const sim *S);
bool sim_hero_visible(const sim *S);
float enemy_half_w(int kind);
float enemy_half_h(int kind);
extern const int ENEMY_SCORE[EK_KINDS];

/* script.c: the level's wave schedule, the original's generators */
void script_build(sim *S);

/* ── presentation ──────────────────────────────────────────────────── */

/* textures.c */
enum { MT_CHROME, MT_RUST, MT_DECK, MT_GRATE, MT_KINDS };
void tex_apply(t3_material *m, int kind, float ru, float rv);
t3_texture *env_texture(void);
t3_texture *env_logo_texture(void);
void env_radiance(float x, float y, float z, float *rgb);
t3_texture *tex_map_copy(int kind, float ru, float rv);
void tex_free_all(void);
bool tex_stale(void); /* the graphics preset changed since the maps were made */
extern uint32_t tex_hash;

/* models.c */
enum { CK_HERO, CK_STRAIGHT, CK_OMNI, CK_RAYGUN, CK_TANK, CK_GNAT, CK_BOSS0, CK_BOSS1, CRAFT_KINDS };
enum { M_CHROME, M_LILAC, M_RUST, M_RUST_DARK, M_BLUE, M_TEAL, M_BLACK, M_RED_GLOW, M_PINK_GLOW, M_ENGINE, M_AMBER, M_GREEN_GLOW, M_CANOPY, M_FLAME, MATS_N };
void models_init(void);
void models_free(void);
t3_object *model_new(int kind, t3_object *parent, t3_object **spin, int *n_spin);
float model_radius(int kind);
t3_material *model_mat(int which);
extern uint32_t model_hash;

/* world.c: the fortress below, the craft, the bullets */
#define PLAY_Y 0.0f       /* the play plane's world height */
/* the camera: over the play plane, tipped forward a little, framing the
 * original's view (y -8.5 .. 8.5 of the field) on any aspect; it eases
 * sideways after the ship by CAM_FOLLOW of the ship's x */
#define CAM_FOV 36.0f
#define CAM_H 25.5f
#define CAM_Z 6.8f
#define CAM_AT_Z 0.0f
#define CAM_FOLLOW 0.12f
#define GROUND_Y -14.0f   /* the fortress deck */
void world_build(cw_node root, int variation);
void world_free(void);
void world_sync(const sim *prev, const sim *cur, float alpha, float t, int state);
void world_camera(cw_node cam, const sim *S, float t, int state);
extern uint32_t world_hash;
t3_texture *world_halo_texture(void);
/* sim plane to world: x right, +y up the screen is -Z */
static inline t3_vec3 to_world(float x, float y) { return (t3_vec3){ x, PLAY_Y, -y }; }

/* logo.c: the title's 3D wordmark */
void logo_build(t3_object *parent, t3_texture *halo);
void logo_free(void);
void logo_sync(bool visible, float t, t3_vec3 eye, t3_vec3 at, t3_light *glint);

/* hud3d.c: the gauges and spare ships as 3D instruments */
void hud3d_build(t3_object *parent);
void hud3d_free(void);
void hud3d_sync(const sim *S, float t, t3_vec3 eye, t3_vec3 at, float fov_deg, float aspect, bool visible);

/* fx.c: explosions, sparks, flashes, smoke */
void fx_init(cw_node root);
void fx_free(void);
void fx_events(const sim *S);
void fx_hero(const sim *S);
void fx_update(float dt);
extern int fx_lights_live;
int fx_flashes(t3_vec3 *pos, t3_color *col, float *energy, int max);
void fx_cheat_blasts(int n); /* n blasts across the screen (cheat.blasts) */

/* probe.c: the live reflection probe the chrome mirrors (a ReflectionProbe3D) */
void probe_build(cw_node parent, t3_object *root);
void probe_free(void);
void probe_sync(float travel, bool title);
extern int probe_refreshes;
extern int32_t probe_every;

#endif
