/* racer's 3D side: everything here only reads the simulation (game.h) and
 * draws it, on the fixed tick, so every host draws the same frame. */
#ifndef RACER_WORLD_H
#define RACER_WORLD_H

#include "cartwheel/render3d.h"
#include "cartwheel/scene_file.h"
#include "game.h"

t3_color lin(float r, float g, float b); /* screen colour -> linear */
t3_material *phys(t3_color c, float rough, float metal);
cw_node hang(cw_node parent, const char *name, t3_object *obj);
void add_mesh(cw_node parent, const char *name, t3_geometry *g, t3_material *m, bool cast, bool recv);

/* models.c: the karts */
typedef struct kart_model {
  t3_object *root;
  t3_object *wheel[4];   /* pivots at the wheel centres: steer about y, spin about x (set by world.c) */
  t3_object *body;       /* the shell: rolls and pitches a little on its own */
  t3_object *shield, *flame[2];
  t3_material *paint;
  int veh, colour;
} kart_model;
extern const float KART_RGB[8][3];
void kart_build(kart_model *m, int veh, int colour);

/* How many views the race being built will have (1..4; set before a map's scenery() runs). A map thins its detail
 * when this is above 1: every view draws the whole scene. */
int game_planned_views(void);

/* kartgfx.c: the karts as drawn: nodes, wheels, body motion, effects that belong to a kart (shield, flames) */
void kartgfx_build(cw_node parent, const sim *s);
void kartgfx_reset(void);
void kartgfx_set_eyes(const t3_vec3 *e, int n); /* the views eyes: far karts draw simpler */
void world_set_eyes(const t3_vec3 *e, int n);     /* the views eyes: far karts draw simpler */
void kartgfx_tick(const sim *s);               /* fixed tick: pose from the sim */
cw_node kartgfx_node(int i);
void kartgfx_frame(const sim *s, float alpha); /* every frame: body, driver, wheels, flames, shield posed between the last two ticks (s may be NULL: the last sim) */
void kartgfx_drift_colour(int kart, float rgb[3]); /* the tyre smoke and spark colour of this kart's drift charge: grey, blue, orange, purple */

/* world.c */
void world_build(cw_node scene, const sim *s, const map_def *map);
void world_reset(void);
void world_tick(const sim *s);
void world_frame(double alpha);   /* every frame: animated materials (boost strips, water), map scenery_frame */
const track_geo *world_geo(void);

/* fx.c: everything that flies and flashes (particles, sparks, smoke, splashes, speed lines, shake) */
void fx_init(cw_node scene, const sim *s, const map_def *map);
void fx_reset(void);
void fx_events(const sim *s);        /* fixed tick: this tick's sim events become effects */
void fx_tick(const sim *s);          /* fixed tick */
void fx_frame(const sim *s, float alpha); /* every frame: emitters follow the karts as drawn */
float fx_shake(int kart);            /* camera shake for the view following this kart, 0..1 */
float fx_speed_lines(int kart);      /* 0..1: how hard the view following this kart should show speed (HUD uses it) */

/* Ambient particle emitters a map can place from its scenery() callback (fx_amb.c). All are render only.
 *   FXA_RAIN      fast thin streaks that fall past the camera
 *   FXA_SNOW      slow drifting flakes
 *   FXA_EMBERS    glowing sparks rising and flickering out
 *   FXA_STEAM     a plume of white vapour rising and spreading
 *   FXA_DUST      low drifting dust, a haze of motes (a dust devil when the box is tall)
 *   FXA_MIST      big soft pale clouds drifting slowly at ground level
 *   FXA_FIREFLIES small glowing dots wandering and pulsing
 * density scales how many particles (1 = the default for that kind). */
enum { FXA_RAIN, FXA_SNOW, FXA_EMBERS, FXA_STEAM, FXA_DUST, FXA_MIST, FXA_FIREFLIES, FXA_KINDS };
/* An emitter that follows EVERY view's camera (one for each of up to four views, tuned down in split screen): the box
 * rides above the camera and slightly ahead of it, so it rains or snows wherever the player looks. Best for
 * RAIN, SNOW and DUST. scene is the node scenery() was given. */
void fx_ambient_follow(cw_node scene, int kind, float density);
/* An emitter fixed in the world: particles spawn in a box of half sizes `extents` round `at`. It switches off beyond
 * fade_dist metres from every view's camera and fades in over the last 40 percent of that range (0: always on).
 * Returns the node (for a map that wants to move it, e.g. a steam vent that pulses). */
cw_node fx_ambient_at(cw_node scene, int kind, t3_vec3 at, t3_vec3 extents, float density, float fade_dist);

/* audio.c: engines, tyres, boosts, items, ambience and the music */
void audio_init(void);
void audio_race_start(const sim *s, const map_def *map);
void audio_race_stop(void);
void rumble_events(const sim *s);     /* fixed tick: pad rumble for this tick's sim events (needs the engine's cw_input_rumble; else nothing) */
void geomcheck_run(cw_node scene, t3_object *root, const track *t);
void geomclean_begin(cw_node scene, t3_object *root, const track *t); /* at load: scenery in the lanes is cut out over */
bool geomclean_step(int budget);                                       /* the next frames, budget triangles a frame (geomcheck.c) */
void geomclean_snapshot(t3_object *root); /* the places of everything on the first frame: what moves is not cleaned */
void stats_events(const sim *s);      /* fixed tick: dev aid RACER_STATS=1 logs the race's dynamics when it ends (stats.c) */
void rumble_tick(const sim *s);       /* fixed tick: the rumbles that last (boost, drift, rough road) */
void rumble_set_enabled(bool on);
bool rumble_enabled(void);
void audio_events(const sim *s);     /* fixed tick: this tick's sim events */
void audio_tick(const sim *s, const t3_vec3 *eye, int neyes); /* fixed tick: engines follow the karts; one listener per view */
void audio_ui(int sfx);              /* a menu sound: AUI_* */
void audio_music(int track);         /* -1 stops; 0 menu, 1.. a map's music is NMAP+1 */
void audio_shutdown(void);
enum { AUI_MOVE, AUI_SELECT, AUI_BACK, AUI_COUNT, AUI_GO, AUI_WRONG_WAY, AUI_FINAL_LAP, AUI_COUNT_KINDS };
void audio_frame(void);              /* every rendered frame: keeps the music streaming (cw_audio_update); cheap, safe to call when paused */
void audio_pause(bool paused);       /* the race is paused: engines, tyres and wind go quiet (the music and menu sounds go on) */
/* the player's volumes from the options screen (ui.c): v is 0..1; the audio module applies it to every sound on that bus */
enum { ABUS_MUSIC, ABUS_SFX };
void audio_set_volume(int bus, float v);
/* the announcer (announcer.c): on or off (the options page; saved), and the calls the menus make after a race */
void audio_set_announcer(bool on);
bool audio_announcer(void);
enum { ANN_RECORD, ANN_MEDAL_BRONZE, ANN_MEDAL_SILVER, ANN_MEDAL_GOLD, ANN_MEDAL_AUTHOR, ANN_CUP_WIN, ANN_CUP_TROPHY, ANN_CUP_DONE };
void audio_announce(int what);
/* announcer.c's side of audio.c (audio.c drives it on the fixed tick; parent is the bus it speaks into) */
void announcer_init(int parent);
void announcer_shutdown(void);
void announcer_set_volume(float v);
void announcer_race_start(const sim *s, bool humans);
void announcer_race_stop(void);
void announcer_pause(bool paused);
void announcer_events(const sim *s);
void announcer_tick(const sim *s);
void announcer_wrong_way(void);
float announcer_duck(void);       /* 0..1: how much it is speaking (the music and the engines make room) */

/* items_gfx.c: what the hazards and boxes look like */
void items_build(cw_node scene, const sim *s);
void items_reset(void);
void items_tick(const sim *s);       /* fixed tick: boxes, missiles, oil, mines placed from the sim */
void items_frame(const sim *s, float alpha); /* every frame: boxes, hazards and blasts posed between the last two ticks (s may be NULL) */
void item_icon_color(int item, float rgb[3]); /* the colour of an item (IT_*), for icons and trails */

#endif
