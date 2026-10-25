/* tools/textest.sh: dump racer's procedural textures to PNG on the host, without the engine, so each can be
 * looked at: textest <kind> <map> <out.ppm> [tiles]. map: 0 albedo, 1 normal, 2 roughness, 3 occlusion, 4 emissive.
 * The three.c texture calls are stubbed; textures.c is included whole. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../textures.c"

float cw_sin(float x) { return sinf(x); }
float cw_cos(float x) { return cosf(x); }
float cw_pow(float x, float y) { return powf(x, y); }

t3_texture *t3_texture_new(int w, int h, const uint8_t *rgba) {
  t3_texture *t = calloc(1, sizeof *t);
  t->width = w, t->height = h;
  t->pixels = malloc((size_t)w * h * 4);
  memcpy(t->pixels, rgba, (size_t)w * h * 4);
  return t;
}
void t3_release(void *p) { (void)p; }
void *t3_retain(void *p) { return p; }
void t3_material_set_texture(t3_material *m, t3_map_slot s, t3_texture *t) { (void)m, (void)s, (void)t; }

int main(int argc, char **argv) {
  if (argc < 4) return 1;
  int kind = atoi(argv[1]), map = atoi(argv[2]), tiles = argc > 4 ? atoi(argv[4]) : 1;
  const tex_set *s = tex_get(kind);
  t3_texture *t = map == 0 ? s->map : map == 1 ? s->normal : map == 2 ? s->rough : map == 3 ? s->ao : s->emit;
  if (!t) {
    fprintf(stderr, "no such map\n");
    return 2;
  }
  FILE *f = fopen(argv[3], "wb");
  fprintf(f, "P6\n%d %d\n255\n", t->width * tiles, t->height * tiles);
  for (int y = 0; y < t->height * tiles; y++)
    for (int x = 0; x < t->width * tiles; x++) fwrite(t->pixels + ((y % t->height) * t->width + x % t->width) * 4, 1, 3, f);
  fclose(f);
  printf("%s %dx%d hash %08x\n", INFO[kind].name, t->width, t->height, tex_hash);
  return 0;
}
