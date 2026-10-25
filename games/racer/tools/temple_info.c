/* host-side: prints where each piece of the temple course starts (s, world position, heading, pitch, flags).
 * tools/temple_info.sh builds and runs it; used to place set pieces. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float cw_sin(float x) { return sinf(x); }
float cw_cos(float x) { return cosf(x); }
void cw_sincos(float x, float *s, float *c) { *s = sinf(x), *c = cosf(x); }
float cw_atan2(float y, float x) { return atan2f(y, x); }
#include "../track.h"
#include MAPFILE

void cw_log(cw_log_level level, const char *f, ...) { (void)level; (void)f; }
extern int track_debug;
int main(int argc, char **argv) {
  track t;
  if (!track_build(&t, MAP_SPEC)) return 1;
  int every = argc > 1 ? atoi(argv[1]) : 0;
  int seg = -1;
  for (int i = 0; i < t.n; i++) {
    const tsample *s = &t.s[i];
    if (s->seg != seg || (every && i % every == 0)) {
      seg = s->seg;
      printf("s=%6.0f seg=%2d p=(%7.1f %6.1f %7.1f) head=%6.1f pitch=%5.1f bank=%5.1f hw=%.1f fl=%x walls=%d surf=%d\n", s->s, s->seg, s->p.x, s->p.y, s->p.z,
             atan2f(s->f.x, s->f.z) * 57.2958f, asinf(s->f.y) * 57.2958f, s->bank * 57.2958f, s->hw, s->flags, s->walls, s->surf);
    }
  }
  return 0;
}
