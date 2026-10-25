/* racer's props (stub: props.c is the place for the generic scenery parts and the track dressing). */
#include "props.h"

prop_batch *pb_begin(t3_geometry *g, t3_material *m) {
  (void)g, (void)m;
  return NULL;
}
void pb_add(prop_batch *b, t3_vec3 pos, float yaw, t3_vec3 scale) { (void)b, (void)pos, (void)yaw, (void)scale; }
void pb_add_q(prop_batch *b, t3_vec3 pos, t3_quat q, t3_vec3 scale) { (void)b, (void)pos, (void)q, (void)scale; }
cw_node pb_finish(prop_batch *b, cw_node parent, const char *name, bool cast_shadow) {
  (void)b, (void)parent, (void)name, (void)cast_shadow;
  return (cw_node){ 0 };
}
