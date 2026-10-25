/* host-side probe for the refinery map: prints where the set pieces fall on the built track.
 * Build and run: tools/refinery_probe.sh */
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
#include "../maps/m4_refinery.c"

void cw_log(cw_log_level level, const char *f, ...) { (void)level; (void)f; }

int main(void) {
  track t;
  if (!track_build(&t, MAP_SPEC)) return 1;
  printf("length %.0f\n", t.length);
  uint32_t prev = 0;
  int start = 0;
  const char *names[] = { "gap", "wallL", "wallR", "tunnel", "boost", "water", "loop", "check", "items", "open", "plate", "drifty", "rough", "finish", "ramp" };
  for (int b = 0; b < 15; b++) {
    uint32_t bit = 1u << b;
    if (b == 1 || b == 2 || b == 7 || b == 8 || b == 9 || b == 10 || b == 11) continue;
    int on = 0;
    for (int i = 0; i <= t.n; i++) {
      int now = i < t.n && (t.s[i].flags & bit);
      if (now && !on) start = i;
      if (!now && on) printf("%-7s %4d .. %4d  y %.1f..%.1f\n", names[b], start, i - 1, t.s[start].p.y, t.s[i - 1].p.y);
      on = now;
    }
  }
  (void)prev;
  int surf = -1;
  for (int i = 0; i < t.n; i++)
    if (t.s[i].surf != surf) surf = t.s[i].surf, printf("surf %d from %d\n", surf, i);
  for (int g = 0; g < t.ngaps; g++) {
    int a = t.gap_from[g], b = t.gap_to[g];
    float ymax = -1e9;
    for (int i = a - 40; i < b + 40; i++) if (i >= 0 && i < t.n && t.s[i].p.y > ymax) ymax = t.s[i].p.y;
    printf("gap %d: %d..%d  lip y %.1f (x %.0f z %.0f) land y %.1f peak y %.1f\n", g, a, b, t.s[a].p.y, t.s[a].p.x, t.s[a].p.z, t.s[b].p.y, ymax);
  }
  float minx = 1e9, maxx = -1e9, minz = 1e9, maxz = -1e9, maxy = -1e9;
  for (int i = 0; i < t.n; i++) {
    minx = fminf(minx, t.s[i].p.x), maxx = fmaxf(maxx, t.s[i].p.x), minz = fminf(minz, t.s[i].p.z), maxz = fmaxf(maxz, t.s[i].p.z), maxy = fmaxf(maxy, t.s[i].p.y);
  }
  printf("bounds x %.0f..%.0f z %.0f..%.0f maxy %.1f\n", minx, maxx, minz, maxz, maxy);
  for (int i = 0; i < t.n; i += 100) printf("s=%d p=(%.0f,%.1f,%.0f) f=(%.2f,%.2f,%.2f) hw %.1f bank %.1f\n", i, t.s[i].p.x, t.s[i].p.y, t.s[i].p.z, t.s[i].f.x, t.s[i].f.y, t.s[i].f.z, t.s[i].hw, t.s[i].bank * 57.3f);
  return 0;
}
