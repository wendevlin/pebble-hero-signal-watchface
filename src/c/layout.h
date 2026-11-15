#pragma once
/*
 * Page geometry shared by the scene (scene.c) and the skylines (skyline.c).
 */

#include <pebble.h>
#include "comic.h"

typedef struct {
  int16_t w, h, g;
  bool round;
  GPoint a0, a1;  // SKY / ACTION border, at x = 0 and x = w
  GPoint b0, b1;  // ACTION / bottom border
  GPoint c0, c1;  // POWER / ENERGY border (top on line B, bottom at screen edge)
  Region sky, mid, steps, batt;
  GPoint caption;  // top-left of the date caption (round: top-centre)
  GPoint weather;
  int16_t wsize;
  GPoint emblem;
  int16_t erx, ery;
  GPoint time_center;
  GPoint steps_anchor, batt_anchor;
  int16_t content_w;
} Layout;

/* Colours of the sky panel, chosen from the weather and the time of day. */
typedef struct {
  GColor sky, dot, building, window;
} SkyPalette;

#define PM(v, total) ((int32_t)(v) * (total) / 1000)

static inline int16_t line_y(GPoint a, GPoint b, int16_t x) {
  if (b.x == a.x) return a.y;
  return a.y + (int32_t)(b.y - a.y) * (x - a.x) / (b.x - a.x);
}
