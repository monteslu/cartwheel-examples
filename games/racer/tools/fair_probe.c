/* host-side probe for the fair map: where every piece of the course begins (s, position, heading, height).
 * Build: see tools/fair_probe.sh */
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
#include "../maps/m3_fair.c"

void cw_log(cw_log_level level, const char *f, ...) { (void)level; (void)f; }

int main(void) {
  track t;
  if (!track_build(&t, MAP_SPEC)) return 1;
  int last = -1;
  for (int i = 0; i < t.n; i++) {
    if (t.s[i].seg == last) continue;
    last = t.s[i].seg;
    const tsample *a = &t.s[i];
    printf("piece %2d kind %d  s %6.0f  p (%7.1f %6.1f %7.1f)  hdg %6.1f  pitch %6.1f\n", a->seg, SPEC[a->seg].kind, a->s, a->p.x, a->p.y, a->p.z,
           atan2f(a->f.x, a->f.z) * 57.2958f, asinf(a->f.y) * 57.2958f);
  }
  return 0;
}
