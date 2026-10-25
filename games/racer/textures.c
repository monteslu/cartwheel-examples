/* racer's textures (stub: textures.c is the place for the procedural PBR sets listed in textures.h). */
#include "textures.h"

uint32_t tex_hash;
const tex_set *tex_get(int kind) {
  (void)kind;
  return NULL;
}
bool tex_warm(int kind) {
  (void)kind;
  return true;
}
void tex_apply(t3_material *m, int kind, float repeat_u, float repeat_v, float normal_scale, float ao_intensity) {
  (void)m, (void)kind, (void)repeat_u, (void)repeat_v, (void)normal_scale, (void)ao_intensity;
}
void tex_free_all(void) {}
