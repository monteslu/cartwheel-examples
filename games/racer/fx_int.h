/* racer's effects: what fx.c and fx_amb.c share (not part of the module API in world.h) */
#ifndef RACER_FX_INT_H
#define RACER_FX_INT_H

#include "world.h"

/* a GPUParticles3D from a key/value recipe (NULL ended), parked: not emitting, with this seed */
cw_node fx_pnode(cw_node parent, const char *name, const char *const *kv, int seed);
void fx_set_str(cw_node n, const char *prop, const char *s);

/* fx_amb.c: ambient emitters a map asked for */
void fx_amb_reset(void);
void fx_amb_init(int nviews);                                       /* once the views are known: sets the amounts */
void fx_amb_frame(const t3_vec3 *cam, const t3_vec3 *vel, int ncams); /* every frame: followers, distance fade */

#endif
