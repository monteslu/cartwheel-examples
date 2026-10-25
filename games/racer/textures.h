/* racer's procedural textures: every surface gets a full PBR set made here at load time (no
 * image files): albedo (sRGB), a tangent-space normal map from a height field, roughness and
 * ambient occlusion. Integer hashing and cartwheel/math.h only, so every host makes the same
 * texels. Sets are made once, on first use, and shared (tex_warm builds them a band of rows at a
 * time, for the loading screen). */
#ifndef RACER_TEXTURES_H
#define RACER_TEXTURES_H

#include <stdbool.h>

#include "three.h"

/* The road kinds are laid out for a road mesh: u runs across the road (0 left edge .. 1 right
 * edge, one repeat across) and v along it (one repeat per track_mesh_opts.tile_v metres), so
 * lane lines, edge lines and joints are painted into the texture at fixed u. The rest tile
 * freely (wall and ground kinds). */
enum {
  /* roads */
  TEX_ROAD_ASPHALT,   /* worn black top, white edge lines, dashed centre */
  TEX_ROAD_CONCRETE,  /* pale slabs with joints and cracks, faded paint */
  TEX_ROAD_DIRT,      /* packed earth with tyre ruts */
  TEX_ROAD_SALT,      /* cracked white salt crust, hex plates, faint tyre marks */
  TEX_ROAD_ICE,       /* blue-white ice, scratches and frost */
  TEX_ROAD_METAL,     /* diamond plate or grating with hazard edges */
  TEX_ROAD_WOOD,      /* planks along the road, nailed, weathered */
  TEX_ROAD_STONE,     /* temple flagstones, moss in the joints */
  TEX_ROAD_TECH,      /* dark panels with glowing lane strips (emissive via the albedo hint) */
  TEX_ROAD_SAND,      /* wind-rippled sand with wheel tracks */
  TEX_ROAD_WET,       /* asphalt, dark and glossy (low roughness), puddle patches */
  TEX_ROAD_GLASS,     /* a clear plate: faint grid, reflections do the rest */
  /* walls, barriers, structures */
  TEX_WALL_CONCRETE,  /* jersey-barrier concrete, stained */
  TEX_WALL_HAZARD,    /* diagonal yellow and black, scuffed */
  TEX_WALL_RUST,      /* corrugated, rusted sheet metal */
  TEX_WALL_BRICK,
  TEX_WALL_STONE,     /* big carved blocks */
  TEX_WALL_ICE,
  TEX_WALL_TECH,      /* sci-fi panels with rivets */
  TEX_WALL_WOOD,      /* planks, painted and chipped */
  /* ground and general */
  TEX_GROUND_SAND,
  TEX_GROUND_ROCK,
  TEX_GROUND_GRASS,
  TEX_GROUND_SNOW,
  TEX_GROUND_MUD,
  TEX_GROUND_ASH,
  TEX_METAL,          /* worn steel plate, rivets */
  TEX_RUST,           /* orange rust, streaks */
  TEX_PAINT_STRIPES,  /* kerb: red and white (the colours come from map_look) */
  TEX_ARROWS,         /* boost chevrons, alpha-ish: bright arrows on dark */
  TEX_NOISE,          /* soft grey noise: a generic detail map */
  TEX_KINDS
};

typedef struct tex_set { t3_texture *map, *normal, *rough, *ao; } tex_set;
const tex_set *tex_get(int kind);
/* Builds a band of kind's rows; true once the set is complete (loading screens call this a frame at a time). */
bool tex_warm(int kind);
/* Dresses a physical material in a set. repeat_u / repeat_v multiply the mesh's uv. */
void tex_apply(t3_material *m, int kind, float repeat_u, float repeat_v, float normal_scale, float ao_intensity);
void tex_free_all(void);
extern uint32_t tex_hash; /* a checksum of everything made, a debug field */

#endif
