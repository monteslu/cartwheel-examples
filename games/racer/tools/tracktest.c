/* host-side track checker: builds a map's track on this machine (libm for the maths, no engine)
 * and writes a top view, a side view and the numbers. Build and run: tools/tracktest.sh <map file> */
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

void cw_log(cw_log_level level, const char *f, ...) {
  va_list a;
  va_start(a, f);
  fprintf(level >= CW_LOG_ERROR ? stderr : stdout, level >= CW_LOG_ERROR ? "ERROR: " : "");
  vfprintf(level >= CW_LOG_ERROR ? stderr : stdout, f, a);
  fprintf(level >= CW_LOG_ERROR ? stderr : stdout, "\n");
  va_end(a);
}


extern int track_debug;
int main(int argc, char **argv) {
  track_debug = getenv("TRACK_DEBUG") != NULL;
  const char *out = argc > 1 ? argv[1] : "/tmp/track";
  track t;
  if (!track_build(&t, MAP_SPEC)) {
    /* show where the open path ends, to tune the closing run by hand */
    track_spec open = *MAP_SPEC;
    open.circuit = false;
    track o;
    if (track_build(&o, &open)) {
      const tsample *e = &o.s[o.n - 1];
      printf("open path: %d samples, %.0f m; ends at (%.0f, %.0f, %.0f) heading %.0f deg (from +z, left +), pitch %.1f deg\n", o.n, o.length, e->p.x, e->p.y, e->p.z,
             atan2f(e->f.x, e->f.z) * 57.2958f, asinf(e->f.y) * 57.2958f);
    }
    fprintf(stderr, "build failed\n");
    return 1;
  }
  float minx = 1e9, maxx = -1e9, minz = 1e9, maxz = -1e9, miny = 1e9, maxy = -1e9;
  for (int i = 0; i < t.n; i++) {
    const tsample *s = &t.s[i];
    if (s->p.x < minx) minx = s->p.x;
    if (s->p.x > maxx) maxx = s->p.x;
    if (s->p.z < minz) minz = s->p.z;
    if (s->p.z > maxz) maxz = s->p.z;
    if (s->p.y < miny) miny = s->p.y;
    if (s->p.y > maxy) maxy = s->p.y;
  }
  printf("samples %d  length %.1f m  circuit %d  closure error %.3f m\n", t.n, t.length, t.circuit, t.closure_error);
  printf("extent x %.0f..%.0f  z %.0f..%.0f  height %.1f..%.1f\n", minx, maxx, minz, maxz, miny, maxy);
  printf("checks %d  item rows %d  gaps %d  loops %d\n", t.nchecks, t.nitem_rows, t.ngaps, t.nloops);
  for (int g = 0; g < t.ngaps; g++) {
    const tsample *a = &t.s[t.gap_from[g]], *b = &t.s[t.gap_to[g] < t.n ? t.gap_to[g] : t.n - 1];
    printf("  gap %d at s=%.0f: %.1f m long, drop %.1f m, lands pitch %.1f deg\n", g, a->s, b->s - a->s, a->p.y - b->p.y,
           asinf(b->f.y) * 57.2958f);
  }
  /* min distance between non-adjacent parts of the track (overlap check) */
  float mind = 1e9;
  int mi = 0, mj = 0;
  for (int i = 0; i < t.n; i += 2)
    for (int j = i + 1; j < t.n; j += 2) {
      float ds = t.s[j].s - t.s[i].s;
      if (ds < 60 || (t.circuit && t.length - ds < 60)) continue;
      float dx = t.s[i].p.x - t.s[j].p.x, dy = t.s[i].p.y - t.s[j].p.y, dz = t.s[i].p.z - t.s[j].p.z;
      float d = sqrtf(dx * dx + dy * dy + dz * dz) - t.s[i].hw - t.s[j].hw;
      if (d < mind) mind = d, mi = i, mj = j;
    }
  printf("closest approach of distant parts: %.1f m (at s=%.0f and s=%.0f)\n", mind, t.s[mi].s, t.s[mj].s);
  /* rasters: a top view coloured by height, and a side profile */
  if (getenv("TRACKTEST_DUMP")) {
    int a = atoi(getenv("TRACKTEST_DUMP")), b = a + 12;
    for (int i = a; i < b && i < t.n; i++) {
      const tsample *s = &t.s[i];
      printf("i=%d s=%.2f p=(%.2f %.2f %.2f) f=(%.3f %.3f %.3f) u=(%.3f %.3f %.3f) bank=%.1f hw=%.2f flags=%x\n", i, s->s, s->p.x, s->p.y, s->p.z, s->f.x, s->f.y, s->f.z, s->u.x, s->u.y, s->u.z, s->bank * 57.3f, s->hw, s->flags);
    }
  }
  char path[512];
  {
    float W = maxx - minx + 80, H = maxz - minz + 80, sc = 1000.0f / (W > H ? W : H);
    int w = (int)(W * sc), h = (int)(H * sc);
    unsigned char *img = calloc((size_t)w * h, 3);
    for (int i = 0; i < w * h; i++) img[i * 3] = 20, img[i * 3 + 1] = 24, img[i * 3 + 2] = 31;
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < t.n; i++) {
        const tsample *a = &t.s[i];
        float hgt = (a->p.y - miny) / (maxy - miny + 1e-3f);
        int r = (int)(60 + 195 * hgt), g = (int)(210 - 140 * hgt), bl = (int)(255 - 215 * hgt);
        if (a->flags & TF_GAP) r = 255, g = 60, bl = 60;
        else if (a->flags & TF_LOOP) r = 255, g = 220, bl = 40;
        if (a->flags & TF_TUNNEL) r = g = bl = 150;
        float cx = (a->p.x - minx + 40) * sc, cy = (a->p.z - minz + 40) * sc, rad = a->hw * sc * (pass ? 0.25f : 1.0f);
        if (pass) r = g = bl = 255 * (i == 0 ? 1 : 0.1f);
        if (pass && !(a->flags & (TF_CHECK | TF_ITEMS | TF_FINISH))) continue;
        if (pass) rad = 4;
        if (pass && (a->flags & TF_CHECK)) r = 255, g = 255, bl = 255;
        if (pass && (a->flags & TF_ITEMS) && !(a->flags & TF_CHECK)) r = 255, g = 120, bl = 255;
        for (int y = (int)(cy - rad); y <= (int)(cy + rad); y++)
          for (int x = (int)(cx - rad); x <= (int)(cx + rad); x++) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            float dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > rad * rad) continue;
            unsigned char *p = img + ((size_t)y * w + x) * 3;
            p[0] = (unsigned char)r, p[1] = (unsigned char)g, p[2] = (unsigned char)bl;
          }
      }
    snprintf(path, sizeof path, "%s-top.ppm", out);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(img, 3, (size_t)w * h, f);
    fclose(f);
    free(img);
  }
  {
    int w = 1400, h = 320;
    float L = t.length, hs = (w - 40.0f) / L, vs = (h - 40.0f) / (maxy - miny + 10);
    unsigned char *img = calloc((size_t)w * h, 3);
    for (int i = 0; i < w * h; i++) img[i * 3] = 20, img[i * 3 + 1] = 24, img[i * 3 + 2] = 31;
    for (int i = 0; i < t.n; i++) {
      int x = 20 + (int)(t.s[i].s * hs), y = h - 20 - (int)((t.s[i].p.y - miny + 5) * vs);
      unsigned char r = 100, g = 200, b = 255;
      if (t.s[i].flags & TF_GAP) r = 255, g = 60, b = 60;
      if (t.s[i].flags & TF_LOOP) r = 255, g = 220, b = 40;
      for (int dy = -1; dy <= 1; dy++)
        if (x >= 0 && x < w && y + dy >= 0 && y + dy < h) {
          unsigned char *p = img + ((size_t)(y + dy) * w + x) * 3;
          p[0] = r, p[1] = g, p[2] = b;
        }
    }
    snprintf(path, sizeof path, "%s-side.ppm", out);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(img, 3, (size_t)w * h, f);
    fclose(f);
    free(img);
  }
  return 0;
}
