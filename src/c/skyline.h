#pragma once
#include "layout.h"

/* Skylines; keep in sync with CITIES in src/pkjs/config.html. */
typedef enum {
  CITY_VIENNA = 0,
  CITY_NEW_YORK = 1,
  CITY_PARIS = 2,
  CITY_GARGOYLE = 3,
  CITY_QUACKVILLE = 4,
  CITY_COUNT
} City;

void skyline_draw(GContext *ctx, const Layout *l, const SkyPalette *pal, bool night, uint8_t city);

/* Where the hero signal beams out of the city's signal landmark. */
GPoint skyline_signal_source(const Layout *l, uint8_t city);
