/* dev aid for the dam map: prints the road's world position every 25 m (host build, no engine).
 * cc -DMAPFILE='"maps/m5_dam.c"' ... see tools/dam_pts.sh */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
float cw_sin(float x) { return sinf(x); }
float cw_cos(float x) { return cosf(x); }
void cw_sincos(float x, float *s, float *c) { *s = sinf(x), *c = cosf(x); }
float cw_atan2(float y, float x) { return atan2f(y, x); }
#include "../track.h"
#include MAPFILE
void cw_log(cw_log_level level, const char *f, ...) { va_list a; va_start(a, f); vfprintf(stderr, f, a); fputc(10, stderr); va_end(a); }
extern int track_debug;
int main(int argc, char **argv) {
  int step = argc > 1 ? atoi(argv[1]) : 25;
  track t;
  if (!track_build(&t, MAP_SPEC)) return 1;
  for (int i = 0; i < t.n; i += step) {
    const tsample *s = &t.s[i];
    printf("s=%4.0f p=(%7.1f %6.1f %7.1f) f=(%5.2f %5.2f %5.2f) hw=%.0f fl=%x\n", s->s, s->p.x, s->p.y, s->p.z, s->f.x, s->f.y, s->f.z, s->hw, s->flags);
  }
  return 0;
}
