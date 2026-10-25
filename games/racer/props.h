/* racer's props and track dressing (props.c): the parts a map's scenery is built from. Instanced meshes
 * for anything repeated, merged geometry for anything one-off. */
#ifndef RACER_PROPS_H
#define RACER_PROPS_H

#include "world.h"

/* a batch of one geometry and material drawn many times (an InstancedMesh node under `parent`) */
typedef struct prop_batch prop_batch;
prop_batch *pb_begin(t3_geometry *g, t3_material *m);
void pb_add(prop_batch *b, t3_vec3 pos, float yaw, t3_vec3 scale);
void pb_add_q(prop_batch *b, t3_vec3 pos, t3_quat q, t3_vec3 scale);
cw_node pb_finish(prop_batch *b, cw_node parent, const char *name, bool cast_shadow);

#endif
