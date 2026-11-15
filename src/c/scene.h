#pragma once
#include <pebble.h>

/* Weather conditions (shared with the phone: src/pkjs/weather.ts). */
typedef enum {
  COND_UNKNOWN = 0,
  COND_CLEAR = 1,
  COND_PARTLY = 2,
  COND_CLOUDY = 3,
  COND_FOG = 4,
  COND_RAIN = 5,
  COND_POURING = 6,
  COND_SNOW = 7,
  COND_SLEET = 8,
  COND_THUNDER = 9,
  COND_HAIL = 10,
  COND_WIND = 11,
  COND_COUNT
} WeatherCond;

typedef enum {
  HERO_RED = 0,
  HERO_BLUE = 1,
  HERO_GREEN = 2,
  HERO_PURPLE = 3,
  HERO_COUNT
} HeroColor;

/* Everything the face shows; owned by main.c, read by the renderer. */
typedef struct {
  struct tm now;
  bool h24;
  bool german;
  bool connected;

  bool has_weather;
  int16_t temp_tenths_c;
  /* Optional second temperature from Home Assistant, shown as "12°/21°". */
  bool has_ha_temp;
  int16_t ha_tenths_c;
  uint8_t cond;
  bool is_day;
  bool fahrenheit;

  bool has_steps;
  int32_t steps;
  int32_t step_goal;

  uint8_t battery;
  bool charging;

  uint8_t hero;
  uint8_t city;  /* skyline, see City in skyline.h */
} Scene;

void scene_init(void);
void scene_deinit(void);
void scene_draw(GContext *ctx, GRect bounds, const Scene *s);
