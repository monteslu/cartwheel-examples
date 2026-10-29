/* otium map: Roman Italy. Cities at their real places (degrees), grouped
 * into regions; roads between neighbours and sea lanes between ports. The
 * days on each edge come from its length (map_init): galleys sail faster
 * than carts roll. The coast outlines are traced by hand at about a
 * quarter of a degree, enough for a table map. */
#include "game.h"

const city_def CITIES[] = {
  /* 0 */ { "Roma", 12.50f, 41.90f, -1, -1, true },
  /* Gallia Cisalpina */
  /* 1 */ { "Mediolanum", 9.19f, 45.46f, 0, GD_BRICK, false },
  /* 2 */ { "Verona", 10.99f, 45.44f, 0, GD_FOOD, false },
  /* 3 */ { "Aquileia", 13.37f, 45.77f, 0, GD_CLOTH, true },
  /* 4 */ { "Bononia", 11.34f, 44.49f, 0, GD_TOOLS, false },
  /* Etruria et Liguria */
  /* 5 */ { "Genua", 8.93f, 44.41f, 1, GD_WINE, true },
  /* 6 */ { "Pisae", 10.40f, 43.72f, 1, GD_BRICK, true },
  /* 7 */ { "Florentia", 11.25f, 43.77f, 1, GD_FOOD, false },
  /* Umbria et Picenum */
  /* 8 */ { "Ariminum", 12.57f, 44.06f, 2, GD_WINE, true },
  /* 9 */ { "Ancona", 13.52f, 43.62f, 2, GD_TOOLS, true },
  /* Campania */
  /* 10 */ { "Neapolis", 14.25f, 40.85f, 3, GD_WINE, true },
  /* 11 */ { "Beneventum", 14.78f, 41.13f, 3, GD_BRICK, false },
  /* Apulia et Calabria */
  /* 12 */ { "Luceria", 15.33f, 41.50f, 4, GD_FOOD, false },
  /* 13 */ { "Tarentum", 17.24f, 40.47f, 4, GD_TOOLS, true },
  /* 14 */ { "Brundisium", 17.94f, 40.64f, 4, GD_CLOTH, true },
  /* Bruttium */
  /* 15 */ { "Croton", 17.12f, 39.08f, 5, GD_CLOTH, true },
  /* 16 */ { "Rhegium", 15.65f, 38.11f, 5, GD_FOOD, true },
  /* Sicilia */
  /* 17 */ { "Panormus", 13.36f, 38.12f, 6, GD_BRICK, true },
  /* 18 */ { "Agrigentum", 13.58f, 37.31f, 6, GD_WINE, true },
  /* 19 */ { "Syracusae", 15.29f, 37.08f, 6, GD_TOOLS, true },
  /* Sardinia */
  /* 20 */ { "Carales", 9.11f, 39.22f, 7, GD_FOOD, true },
  /* 21 */ { "Turris", 8.40f, 40.84f, 7, GD_CLOTH, true },
};
const int N_CITIES = sizeof CITIES / sizeof CITIES[0];

const region_def REGIONS[] = {
  { "Gallia Cisalpina", GD_CLOTH }, { "Etruria", GD_WINE }, { "Umbria", GD_TOOLS }, { "Campania", GD_WINE },
  { "Apulia", GD_CLOTH },           { "Bruttium", GD_FOOD }, { "Sicilia", GD_TOOLS }, { "Sardinia", GD_CLOTH },
};
const int N_REGIONS = sizeof REGIONS / sizeof REGIONS[0];

/* roads (sea = false) and sea lanes; days filled in by map_init */
edge_def EDGES[MAX_EDGES] = {
  /* the north */
  { 1, 2, 0, false }, { 1, 5, 0, false }, { 1, 4, 0, false }, { 2, 3, 0, false }, { 2, 4, 0, false },
  { 4, 7, 0, false }, { 4, 8, 0, false }, { 5, 6, 0, false }, { 6, 7, 0, false },
  /* the middle */
  { 6, 0, 0, false }, { 7, 0, 0, false }, { 8, 9, 0, false }, { 9, 0, 0, false }, { 8, 7, 0, false }, { 9, 12, 0, false },
  /* the south */
  { 0, 10, 0, false }, { 0, 11, 0, false }, { 10, 11, 0, false }, { 11, 12, 0, false }, { 12, 13, 0, false }, { 12, 14, 0, false },
  { 13, 14, 0, false }, { 13, 15, 0, false }, { 15, 16, 0, false }, { 10, 16, 0, false },
  /* the islands */
  { 17, 18, 0, false }, { 18, 19, 0, false }, { 17, 19, 0, false }, { 20, 21, 0, false },
  /* sea lanes */
  { 6, 21, 0, true }, { 5, 21, 0, true }, { 0, 20, 0, true }, { 20, 17, 0, true }, { 10, 17, 0, true }, { 16, 19, 0, true },
  { 14, 9, 0, true }, { 9, 3, 0, true }, { 8, 3, 0, true }, { 19, 15, 0, true }, { 0, 17, 0, true }, { 18, 20, 0, true },
};
int N_EDGES = 41;

void map_init(void) {
  static bool done;
  if (done) return;
  done = true;
  for (int i = 0; i < N_EDGES; i++) {
    edge_def *e = &EDGES[i];
    const city_def *a = &CITIES[e->a], *b = &CITIES[e->b];
    float dx = map_x(a->lon) - map_x(b->lon), dz = map_z(a->lat) - map_z(b->lat);
    float d = sqrtf(dx * dx + dz * dz);
    /* carts about a metre of board in 2.6 days, galleys in 1.6 */
    int days = (int)(d * (e->sea ? 1.6f : 2.6f) + 0.5f);
    e->days = days < 2 ? 2 : days;
  }
}

/* ── coasts ─────────────────────────────────────────────────────────── */

static const float ITALY[] = {
  6.0f, 43.6f, 7.5f, 43.78f, 8.2f, 43.92f, 8.6f, 44.3f, 8.93f, 44.42f, 9.6f, 44.12f, 10.2f, 43.92f, 10.3f, 43.5f,
  10.52f, 42.95f, 11.1f, 42.42f, 11.8f, 42.06f, 12.25f, 41.74f, 12.9f, 41.42f, 13.6f, 41.22f, 14.05f, 40.82f,
  14.35f, 40.6f, 14.75f, 40.66f, 15.0f, 40.22f, 15.65f, 40.02f, 16.05f, 39.45f, 16.1f, 38.95f, 15.85f, 38.62f,
  15.65f, 38.24f, 15.66f, 37.98f, 16.1f, 37.95f, 16.55f, 38.42f, 17.15f, 38.92f, 17.1f, 39.4f, 16.6f, 39.65f,
  16.95f, 40.35f, 17.24f, 40.44f, 17.9f, 40.25f, 18.38f, 39.8f, 18.52f, 40.15f, 18.0f, 40.68f, 17.1f, 41.12f,
  16.2f, 41.38f, 15.92f, 41.6f, 16.18f, 41.9f, 15.6f, 41.95f, 14.9f, 42.05f, 14.2f, 42.45f, 13.8f, 42.92f,
  13.62f, 43.56f, 12.6f, 44.08f, 12.28f, 44.5f, 12.32f, 45.0f, 12.42f, 45.45f, 13.1f, 45.66f, 13.75f, 45.7f,
  14.3f, 47.0f, 6.0f, 47.0f,
};
/* the far shore of the Adriatic */
static const float BALKANS[] = {
  13.75f, 45.7f, 13.55f, 45.3f, 13.9f, 44.85f, 14.35f, 45.25f, 14.95f, 44.6f, 15.9f, 43.62f, 16.5f, 43.5f,
  17.4f, 43.0f, 18.5f, 42.45f, 19.4f, 41.9f, 19.45f, 40.9f, 19.3f, 40.4f, 20.0f, 39.7f, 21.0f, 39.0f, 21.0f, 47.0f, 14.3f, 47.0f,
};
static const float SICILY[] = {
  12.42f, 37.8f, 12.7f, 38.15f, 13.35f, 38.22f, 14.0f, 38.04f, 14.7f, 38.15f, 15.25f, 38.26f, 15.6f, 38.28f,
  15.25f, 37.8f, 15.1f, 37.48f, 15.32f, 37.05f, 15.1f, 36.68f, 14.45f, 36.78f, 13.5f, 37.2f, 12.6f, 37.6f,
};
static const float SARDINIA[] = {
  8.4f, 40.95f, 9.1f, 41.25f, 9.8f, 40.95f, 9.72f, 40.1f, 9.66f, 39.4f, 9.1f, 39.18f, 8.6f, 38.9f, 8.4f, 39.1f,
  8.45f, 39.9f, 8.4f, 40.6f, 8.18f, 40.9f,
};
static const float CORSICA[] = {
  9.4f, 43.0f, 9.56f, 42.4f, 9.4f, 41.6f, 9.2f, 41.36f, 8.8f, 41.6f, 8.56f, 42.0f, 8.66f, 42.6f, 9.2f, 42.78f,
};
static const float AFRICA[] = { 8.0f, 36.0f, 9.8f, 37.32f, 10.4f, 36.9f, 11.1f, 37.05f, 10.6f, 36.3f, 10.6f, 35.0f, 6.0f, 35.0f, 6.0f, 36.0f };
static const float ELBA[] = { 10.1f, 42.82f, 10.45f, 42.86f, 10.44f, 42.72f, 10.12f, 42.74f };

const outline LANDS[] = {
  { ITALY, sizeof ITALY / sizeof(float) / 2 },       { BALKANS, sizeof BALKANS / sizeof(float) / 2 },
  { SICILY, sizeof SICILY / sizeof(float) / 2 },     { SARDINIA, sizeof SARDINIA / sizeof(float) / 2 },
  { CORSICA, sizeof CORSICA / sizeof(float) / 2 },   { AFRICA, sizeof AFRICA / sizeof(float) / 2 },
  { ELBA, sizeof ELBA / sizeof(float) / 2 },
};
const int N_LANDS = sizeof LANDS / sizeof LANDS[0];

static bool in_poly(const float *p, int n, float x, float y) {
  bool in = false;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float xi = p[i * 2], yi = p[i * 2 + 1], xj = p[j * 2], yj = p[j * 2 + 1];
    if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
  }
  return in;
}
bool map_on_land(float lon, float lat) {
  for (int i = 0; i < N_LANDS; i++)
    if (in_poly(LANDS[i].pts, LANDS[i].n, lon, lat)) return true;
  return false;
}
