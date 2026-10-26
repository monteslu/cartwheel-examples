/* the map list */
#include "game.h"

extern const map_def map_salt;
extern const map_def map_drowned;
extern const map_def map_fair;
extern const map_def map_refinery;
extern const map_def map_dam;
extern const map_def map_frost;
extern const map_def map_temple;
extern const map_def map_orbit;
extern const map_def map_test;
const map_def *const MAPS[NMAPS] = { &map_salt, &map_drowned, &map_fair, &map_refinery, &map_dam, &map_frost, &map_temple, &map_orbit };

/* the battle arenas (battle mode: maps/a*.c, arena.h) */
extern const map_def map_yard;
extern const map_def map_foundry;
const map_def *const ARENAS[NARENAS] = { &map_yard, &map_foundry };
