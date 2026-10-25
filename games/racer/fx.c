/* racer's effects (stub: fx.c is the place for particles, sparks, smoke, splashes and the camera shake) */
#include "world.h"

void fx_init(cw_node scene, const sim *s, const map_def *map) { (void)scene, (void)s, (void)map; }
void fx_reset(void) {}
void fx_events(const sim *s) { (void)s; }
void fx_tick(const sim *s) { (void)s; }
void fx_frame(const sim *s, float alpha) { (void)s, (void)alpha; }
float fx_shake(int kart) { (void)kart; return 0; }
float fx_speed_lines(int kart) { (void)kart; return 0; }
