/* racer's prop generators (propgen_nature.c, propgen_man.c): one function per kind, called by prop_make.
 * Each builds into b with the pm_* primitives, centred on x/z = 0 with its base at y = 0. size is the
 * height (or nominal size) asked for; lod 1 asks for a cheaper model. Internal to props. */
#ifndef RACER_PROPS_GEN_H
#define RACER_PROPS_GEN_H

#include "props.h"

typedef void (*gen_fn)(pm *b, float size, int variant, int lod);

#define GEN(name) void name(pm *b, float size, int variant, int lod)
GEN(gen_rock); GEN(gen_boulders); GEN(gen_mesa); GEN(gen_spire); GEN(gen_canyon_wall); GEN(gen_dune); GEN(gen_cactus);
GEN(gen_dead_tree); GEN(gen_pine); GEN(gen_palm); GEN(gen_broadleaf); GEN(gen_jungle_tree); GEN(gen_fern);
GEN(gen_mushroom); GEN(gen_ice_spikes); GEN(gen_ice_boulder); GEN(gen_snow_pine); GEN(gen_crystal); GEN(gen_grass);
GEN(gen_bush);
GEN(gen_barrel); GEN(gen_crate); GEN(gen_container); GEN(gen_tyre); GEN(gen_tyre_stack); GEN(gen_wreck_car);
GEN(gen_wreck_truck); GEN(gen_pipe); GEN(gen_pipe_rack); GEN(gen_tank); GEN(gen_silo); GEN(gen_flare_stack);
GEN(gen_cooling_tower); GEN(gen_crane); GEN(gen_pylon); GEN(gen_light_pole); GEN(gen_street_lamp); GEN(gen_neon_sign);
GEN(gen_billboard); GEN(gen_skyscraper); GEN(gen_ruin_building); GEN(gen_water_tower); GEN(gen_windmill);
GEN(gen_wind_pump); GEN(gen_tent); GEN(gen_bigtop); GEN(gen_ferris_wheel); GEN(gen_carousel); GEN(gen_coaster_lift);
GEN(gen_temple_block); GEN(gen_pillar); GEN(gen_statue); GEN(gen_ruin_arch); GEN(gen_stairs); GEN(gen_bridge_post);
GEN(gen_dish); GEN(gen_solar_panel); GEN(gen_station_module); GEN(gen_antenna); GEN(gen_debris); GEN(gen_cone);
GEN(gen_hay); GEN(gen_bollard); GEN(gen_torch);

/* a value noise for generators: -1..1 smooth noise of a point */
float pm_noise3(float x, float y, float z, uint32_t seed);

#endif
