/* racer's audio (stub: audio.c is the place for engines, tyres, boosts, items, ambience and the music) */
#include "world.h"

void audio_init(void) {}
void audio_race_start(const sim *s, const map_def *map) { (void)s, (void)map; }
void audio_race_stop(void) {}
void audio_events(const sim *s) { (void)s; }
void audio_tick(const sim *s, const t3_vec3 *eye, int neyes) { (void)s, (void)eye, (void)neyes; }
void audio_ui(int sfx) { (void)sfx; }
void audio_music(int track) { (void)track; }
void audio_shutdown(void) {}
