/*
 * Skylines: Vienna, New York, Paris and two made-up cities (Gargoyle City,
 * a gothic noir metropolis, and Quackville, a cartoon duck town).
 *
 * Every city is a row of houses in its own style plus five landmarks on fixed
 * slots. Slot 3 is always the landmark the hero signal beams out of, so the
 * emblem, the weather and the temperature sit in the same free sky for every
 * city. Landmarks are authored in "units" (pixels on the 228 px tall Pebble
 * Time 2), x centred on the slot, y measured UP from the panel border, scaled
 * with the screen height and clipped to the sky panel.
 *
 * Height budget per slot (units): 0 ≈ 34 (left edge), 1 ≈ 32 (under the
 * emblem), 2 ≈ 56, 3 ≈ 62 (signal), 4 ≈ 50 (partly behind the temperature).
 */

#include "skyline.h"

/* ---------------------------------------------------------------------------
 * Drawing in landmark units
 * ------------------------------------------------------------------------- */

typedef struct {
  int16_t cx, base, h;
} Anchor;

static Anchor anchor_at(const Layout *l, int16_t cx) {
  return (Anchor){cx, line_y(l->a0, l->a1, cx) - l->g, l->h};
}

static GPoint ap(const Anchor *a, int x, int y) {
  return GPoint(a->cx + x * a->h / 228, a->base - y * a->h / 228);
}

static int16_t au(const Anchor *a, int v) {
  return v * a->h / 228;
}

NOINLINE static void a_poly(GContext *ctx, const Layout *l, const Anchor *a,
                            const int8_t (*pts)[2], int n, GColor c) {
  static Poly p;
  p.n = n;
  for (int i = 0; i < n; i++) p.p[i] = ap(a, pts[i][0], pts[i][1]);
  poly_fill(ctx, &p, &l->sky, c);
}

/* Rectangle from (x0, y0) to (x1, y1) in units; y0 below 0 reaches under the slanted border. */
NOINLINE static void a_rect(GContext *ctx, const Layout *l, const Anchor *a, int x0, int y0,
                            int x1, int y1, GColor c) {
  static Poly p;
  p.n = 4;
  p.p[0] = ap(a, x0, y1);
  p.p[1] = ap(a, x1, y1);
  p.p[2] = ap(a, x1, y0);
  p.p[3] = ap(a, x0, y0);
  poly_fill(ctx, &p, &l->sky, c);
}

static void a_ellipse(GContext *ctx, const Layout *l, const Anchor *a, int x, int y, int rx,
                      int ry, GColor c) {
  ellipse_fill(ctx, ap(a, x, y), au(a, rx), au(a, ry), &l->sky, c);
}

static void a_line(GContext *ctx, const Layout *l, const Anchor *a, int x0, int y0, int x1,
                   int y1, GColor c, int width) {
  line_draw(ctx, ap(a, x0, y0), ap(a, x1, y1), &l->sky, c, width);
}

#define POLY(...) a_poly(ctx, l, a, (const int8_t[][2]){__VA_ARGS__}, \
                         sizeof((const int8_t[][2]){__VA_ARGS__}) / 2, c)

/* Lit windows on a facade: a grid of 2x3 px lights (or round ones), some on. */
NOINLINE static void windows(GContext *ctx, const Layout *l, GRect r, int seed, bool night,
                             GColor color, bool round) {
  graphics_context_set_fill_color(ctx, color);
  int k = 0;
  for (int16_t wy = r.origin.y + 3; wy < r.origin.y + r.size.h - 2; wy += 6) {
    for (int16_t wx = r.origin.x + 2; wx < r.origin.x + r.size.w - 2; wx += 5, k++) {
      bool lit = night ? ((k * 7 + seed * 3) % 5 < 2) : ((k * 5 + seed) % 4 == 0);
      if (!lit || !region_contains(&l->sky, GPoint(wx + 1, wy + 2))) continue;
      if (!on_canvas(GPoint(wx + 1, wy + 1), 2)) continue;
      if (round) {
        circle_safe(ctx, GPoint(wx + 1, wy + 1), 1);
      } else {
        graphics_fill_rect(ctx, GRect(wx, wy, 2, 3), 0, GCornerNone);
      }
    }
  }
}

/* ---------------------------------------------------------------------------
 * Houses (the row behind the landmarks)
 * ------------------------------------------------------------------------- */

typedef enum {
  HOUSES_ALTBAU,     // Vienna: low Gruenderzeit blocks, gables and mansards
  HOUSES_TOWERS,     // New York: tall flat blocks, some with water towers
  HOUSES_HAUSSMANN,  // Paris: even blocks with mansard roofs and chimneys
  HOUSES_GOTHIC,     // Gargoyle City: tall narrow blocks with steep spikes
  HOUSES_CARTOON,    // Quackville: wonky little houses with round windows
} HouseStyle;

/* Houses along the whole panel; none within gap_r px of gap_x (gap_r 0: no gap). */
NOINLINE static void houses(GContext *ctx, const Layout *l, const SkyPalette *pal, bool night,
                            uint8_t style, int16_t gap_x, int16_t gap_r) {
  const GColor c = pal->building;
  const Anchor unit = {0, 0, l->h};
  int16_t x = -6;
  for (int i = 0; x < l->w + 6; i++) {
    int wu, hu;
    switch (style) {
      case HOUSES_TOWERS: wu = 9 + (i * 7 % 4) * 2; hu = 10 + (i * 5 % 5) * 4; break;
      case HOUSES_HAUSSMANN: wu = 14 + (i * 7 % 3) * 3; hu = 9 + (i * 5 % 3) * 2; break;
      case HOUSES_GOTHIC: wu = 8 + (i * 7 % 4) * 2; hu = 12 + (i * 5 % 5) * 4; break;
      case HOUSES_CARTOON: wu = 12 + (i * 7 % 3) * 3; hu = 7 + (i * 5 % 3) * 3; break;
      default: wu = 12 + (i * 7 % 4) * 3; hu = 6 + (i * 5 % 4) * 2; break;
    }
    const int L = -wu / 2, R = wu - wu / 2;
    Anchor anchor = anchor_at(l, x + au(&unit, wu) / 2);
    const Anchor *a = &anchor;
    if (gap_r > 0 && anchor.cx > gap_x - gap_r && anchor.cx < gap_x + gap_r) {
      x += au(a, wu) + 1;
      continue;
    }
    a_rect(ctx, l, a, L, -10, R, hu, c);
    switch (style) {
      case HOUSES_TOWERS:
        if (i % 3 == 1) {  // water tower on legs
          a_line(ctx, l, a, -2, hu, -2, hu + 3, c, 1);
          a_line(ctx, l, a, 2, hu, 2, hu + 3, c, 1);
          a_rect(ctx, l, a, -3, hu + 3, 3, hu + 7, c);
          POLY({-3, hu + 7}, {3, hu + 7}, {0, hu + 9});
        }
        break;
      case HOUSES_HAUSSMANN:
        POLY({L, hu}, {R, hu}, {R - 3, hu + 5}, {L + 3, hu + 5});
        a_rect(ctx, l, a, L + 3, hu + 4, L + 5, hu + 8, c);
        a_rect(ctx, l, a, R - 5, hu + 4, R - 3, hu + 7, c);
        break;
      case HOUSES_GOTHIC:
        POLY({L, hu}, {R, hu}, {0, hu + 8});
        if (i % 2 == 0) a_line(ctx, l, a, 0, hu + 8, 0, hu + 13, c, 1);
        break;
      case HOUSES_CARTOON: {
        int tilt = (i % 3) - 1;  // crooked roofs
        POLY({L - 1, hu}, {R + 1, hu}, {tilt * 3, hu + 7});
        a_rect(ctx, l, a, R - 4 + tilt, hu + 2, R - 2 + tilt, hu + 8, c);
        break;
      }
      default:
        if (i % 3 == 0) {
          POLY({L, hu}, {R, hu}, {0, hu + 5});
        } else if (i % 3 == 1) {
          POLY({L, hu}, {R, hu}, {wu / 2 - 3, hu + 4}, {-wu / 2 + 3, hu + 4});
          a_rect(ctx, l, a, 2, hu + 3, 4, hu + 7, c);
        }
        break;
    }
    GPoint tl = ap(a, L, hu);
    windows(ctx, l, GRect(tl.x, tl.y, au(a, wu), au(a, hu) + 6), i, night, pal->window,
            style == HOUSES_CARTOON);
    x += au(a, wu) + 1;
  }
}

/* ---------------------------------------------------------------------------
 * Landmarks
 * ------------------------------------------------------------------------- */

#define LANDMARK(name) \
  NOINLINE static void name(GContext *ctx, const Layout *l, const Anchor *a, \
                            const SkyPalette *pal, bool night)
#define LANDMARK_BODY const GColor c = pal->building; (void)night; (void)c

/* --- Vienna --------------------------------------------------------------- */

LANDMARK(riesenrad) {
  LANDMARK_BODY;
  GPoint hub = ap(a, 0, 24);
  int r = au(a, 16);
  line_draw(ctx, ap(a, -10, -4), hub, &l->sky, c, 2);
  line_draw(ctx, ap(a, 10, -4), hub, &l->sky, c, 2);
  ellipse_outline(ctx, hub, r, r, c, 2);
  for (int i = 0; i < 6; i++) {
    int32_t ang = TRIG_MAX_ANGLE * i / 12;
    int dx = r * cos_lookup(ang) / TRIG_MAX_RATIO, dy = r * sin_lookup(ang) / TRIG_MAX_RATIO;
    line_draw(ctx, GPoint(hub.x - dx, hub.y - dy), GPoint(hub.x + dx, hub.y + dy), &l->sky, c, 1);
  }
  graphics_context_set_fill_color(ctx, c);
  for (int i = 0; i < 10; i++) {  // gondolas
    int32_t ang = TRIG_MAX_ANGLE * i / 10;
    GPoint g = GPoint(hub.x + r * cos_lookup(ang) / TRIG_MAX_RATIO,
                      hub.y + r * sin_lookup(ang) / TRIG_MAX_RATIO);
    if (on_canvas(g, 3)) graphics_fill_rect(ctx, GRect(g.x - 1, g.y, 3, 3), 0, GCornerNone);
  }
  a_rect(ctx, l, a, -12, -10, 12, 2, c);
}

LANDMARK(karlskirche) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -12, -10, 12, 11, c);  // church
  a_rect(ctx, l, a, -6, 11, 6, 19, c);     // drum
  a_ellipse(ctx, l, a, 0, 19, 7, 7, c);    // dome
  a_rect(ctx, l, a, -1, 25, 1, 31, c);     // lantern
  POLY({-9, 11}, {9, 11}, {0, 16});        // pediment
  for (int side = -1; side <= 1; side += 2) {  // the two columns
    a_rect(ctx, l, a, side * 15 - 2, -10, side * 15 + 2, 23, c);
    a_rect(ctx, l, a, side * 15 - 3, 23, side * 15 + 3, 25, c);
    a_rect(ctx, l, a, side * 15 - 1, 25, side * 15 + 1, 27, c);
  }
}

LANDMARK(stephansdom) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -17, -10, 11, 10, c);         // nave
  POLY({-17, 10}, {11, 10}, {6, 22}, {-12, 22});  // the steep roof
  a_rect(ctx, l, a, 10, -10, 18, 28, c);          // south tower
  POLY({10, 28}, {18, 28}, {14, 50});             // spire
  a_rect(ctx, l, a, -20, -10, -14, 16, c);        // north tower stump
}

/* Slim shaft, a stacked cylindrical head (no funnel underneath, or it reads as
 * a wine glass), and the long antenna mast that makes it the Donauturm. */
LANDMARK(donauturm) {
  LANDMARK_BODY;
  POLY({-3, -10}, {3, -10}, {2, 36}, {-2, 36});
  a_rect(ctx, l, a, -5, 35, 5, 38, c);  // lower ring
  a_rect(ctx, l, a, -7, 38, 7, 43, c);  // restaurant drum, the widest part
  a_rect(ctx, l, a, -5, 43, 5, 45, c);  // roof
  a_rect(ctx, l, a, -1, 45, 1, 52, c);  // mast
  a_line(ctx, l, a, 0, 52, 0, 58, c, 1);
}

LANDMARK(dc_tower) {
  LANDMARK_BODY;
  POLY({-7, -10}, {7, -10}, {7, 36}, {0, 40}, {-7, 38});
  GPoint tl = ap(a, -7, 34);
  windows(ctx, l, GRect(tl.x, tl.y, au(a, 14), au(a, 34)), 7, night, pal->window, false);
}

/* --- New York ------------------------------------------------------------- */

LANDMARK(liberty) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -9, -10, 9, 3, c);        // star fort / island
  POLY({-5, 3}, {5, 3}, {4, 14}, {-4, 14});    // pedestal
  POLY({-3, 14}, {3, 14}, {2, 25}, {-2, 25});  // robe
  a_ellipse(ctx, l, a, 0, 27, 2, 3, c);       // head
  POLY({-2, 29}, {0, 32}, {2, 29});            // crown
  a_rect(ctx, l, a, 1, 23, 3, 33, c);         // raised arm
  a_rect(ctx, l, a, 0, 33, 4, 35, c);         // torch
  POLY({1, 35}, {3, 35}, {2, 38});             // flame
  a_rect(ctx, l, a, -5, 18, -3, 22, c);       // tablet
}

LANDMARK(brooklyn_bridge) {
  LANDMARK_BODY;
  a_line(ctx, l, a, -26, 2, 26, 2, c, 2);  // deck
  a_line(ctx, l, a, -6, 27, -24, 3, c, 1);  // main cables
  a_line(ctx, l, a, 6, 27, 24, 3, c, 1);
  for (int x = 10; x <= 20; x += 5) {  // suspenders
    int y = 27 - (x - 6) * 24 / 18;
    a_line(ctx, l, a, x, y, x, 2, c, 1);
    a_line(ctx, l, a, -x, y, -x, 2, c, 1);
  }
  a_rect(ctx, l, a, -6, -10, -2, 20, c);  // the tower's two legs ...
  a_rect(ctx, l, a, 2, -10, 6, 20, c);
  // ... joined by the gothic arch on top
  POLY({-6, 18}, {-2, 18}, {0, 22}, {2, 18}, {6, 18}, {6, 29}, {-6, 29});
}

LANDMARK(chrysler) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -6, -10, 6, 26, c);
  a_rect(ctx, l, a, -5, 26, 5, 32, c);
  POLY({-5, 32}, {5, 32}, {4, 36}, {-4, 36});  // the stacked crown
  POLY({-4, 36}, {4, 36}, {3, 40}, {-3, 40});
  POLY({-3, 40}, {3, 40}, {2, 44}, {-2, 44});
  POLY({-2, 44}, {2, 44}, {1, 47}, {-1, 47});
  a_line(ctx, l, a, 0, 47, 0, 56, c, 1);  // needle
  if (night) {  // the lit sunburst windows
    const GColor w = pal->window;
    a_poly(ctx, l, a, (const int8_t[][2]){{-1, 37}, {1, 37}, {0, 39}}, 3, w);
    a_poly(ctx, l, a, (const int8_t[][2]){{-1, 41}, {1, 41}, {0, 43}}, 3, w);
  }
}

#define EMPIRE_SIGNAL 46

LANDMARK(empire_state) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -9, -10, 9, 16, c);
  a_rect(ctx, l, a, -7, 16, 7, 28, c);
  a_rect(ctx, l, a, -5, 28, 5, 38, c);
  a_rect(ctx, l, a, -3, 38, 3, 44, c);
  a_rect(ctx, l, a, -2, 44, 2, 48, c);
  a_rect(ctx, l, a, -1, 48, 1, 54, c);  // mooring mast
  a_line(ctx, l, a, 0, 54, 0, 62, c, 1);
  GPoint tl = ap(a, -9, 28);
  windows(ctx, l, GRect(tl.x, tl.y, au(a, 18), au(a, 30)), 3, night, pal->window, false);
}

LANDMARK(one_wtc) {
  LANDMARK_BODY;
  POLY({-8, -10}, {8, -10}, {5, 44}, {-5, 44});
  a_rect(ctx, l, a, -1, 44, 1, 48, c);
  a_line(ctx, l, a, 0, 48, 0, 56, c, 1);
}

/* --- Paris ------------------------------------------------------------------ */

LANDMARK(sacre_coeur) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -11, -10, 10, 10, c);
  a_ellipse(ctx, l, a, -7, 11, 3, 3, c);  // side domes
  a_ellipse(ctx, l, a, 6, 11, 3, 3, c);
  a_rect(ctx, l, a, -4, 10, 4, 16, c);    // drum
  a_ellipse(ctx, l, a, 0, 16, 5, 8, c);   // the tall main dome
  a_rect(ctx, l, a, -1, 22, 1, 28, c);    // lantern
  a_rect(ctx, l, a, 10, -10, 13, 22, c);  // bell tower
  a_ellipse(ctx, l, a, 11, 23, 2, 3, c);
}

LANDMARK(arc_de_triomphe) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -11, -10, -4, 10, c);  // piers
  a_rect(ctx, l, a, 4, -10, 11, 10, c);
  POLY({-4, 6}, {-3, 9}, {3, 9}, {4, 6}, {4, 11}, {-4, 11});  // arch top
  a_rect(ctx, l, a, -11, 10, 11, 18, c);   // attic
  a_rect(ctx, l, a, -12, 18, 12, 20, c);   // cornice
}

LANDMARK(notre_dame) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -14, -10, 26, 12, c);   // facade and nave
  a_rect(ctx, l, a, -14, 12, -5, 27, c);    // the two square towers
  a_rect(ctx, l, a, 5, 12, 14, 27, c);
  a_rect(ctx, l, a, -5, 12, 5, 17, c);      // gallery between them
  POLY({14, 12}, {26, 12}, {20, 18});       // nave roof
  POLY({19, 16}, {21, 16}, {20, 34});       // spire over the crossing
  if (night) a_ellipse(ctx, l, a, 0, 7, 2, 2, pal->window);  // rose window
}

#define EIFFEL_SIGNAL 52

LANDMARK(eiffel_tower) {
  LANDMARK_BODY;
  POLY({-16, -10}, {-14, 0}, {-7, 16}, {-4, 28}, {-3, 38}, {-1, 50}, {0, 58},
       {1, 50}, {3, 38}, {4, 28}, {7, 16}, {14, 0}, {16, -10});
  a_ellipse(ctx, l, a, 0, 2, 7, 9, pal->sky);   // the arch between the legs
  a_rect(ctx, l, a, -9, 15, 9, 18, c);          // first platform
  a_rect(ctx, l, a, -5, 36, 5, 38, c);          // second platform
  a_line(ctx, l, a, 0, 58, 0, 63, c, 1);        // antenna
}

LANDMARK(pantheon) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -12, -10, 12, 12, c);
  POLY({-12, 12}, {12, 12}, {0, 17});  // portico
  a_rect(ctx, l, a, -6, 12, 6, 22, c);  // colonnaded drum
  a_ellipse(ctx, l, a, 0, 22, 7, 7, c);
  a_rect(ctx, l, a, -1, 28, 1, 34, c);  // lantern
}

/* --- Gargoyle City (gothic noir) ------------------------------------------ */

LANDMARK(gargoyle) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -7, -10, 7, 8, c);                            // ledge
  POLY({-5, 8}, {5, 8}, {6, 13}, {3, 17}, {-2, 17}, {-5, 13});   // hunched body
  a_ellipse(ctx, l, a, -3, 19, 2, 2, c);                          // head
  POLY({-2, 15}, {-12, 27}, {-6, 13});                            // wings
  POLY({2, 15}, {11, 28}, {6, 13});
  a_line(ctx, l, a, -4, 20, -6, 24, c, 1);                        // horns
  a_line(ctx, l, a, -2, 20, -1, 24, c, 1);
}

LANDMARK(gothic_cathedral) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -14, -10, 14, 12, c);
  POLY({-14, 12}, {14, 12}, {0, 22});        // steep roof
  a_rect(ctx, l, a, -14, -10, -9, 22, c);    // towers with spires
  POLY({-14, 22}, {-9, 22}, {-12, 32});
  a_rect(ctx, l, a, 9, -10, 14, 22, c);
  POLY({9, 22}, {14, 22}, {11, 32});
  a_line(ctx, l, a, 0, 22, 0, 29, c, 1);
  if (night) a_ellipse(ctx, l, a, 0, 6, 3, 3, pal->window);  // rose window
}

LANDMARK(deco_tower) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -9, -10, 9, 22, c);
  a_rect(ctx, l, a, -7, 22, 7, 32, c);
  a_rect(ctx, l, a, -5, 32, 5, 40, c);
  a_rect(ctx, l, a, -3, 40, 3, 46, c);
  POLY({-3, 46}, {3, 46}, {0, 52});
  a_line(ctx, l, a, 0, 52, 0, 58, c, 1);
  POLY({-7, 32}, {-11, 29}, {-7, 30});  // gargoyle ledges
  POLY({7, 32}, {11, 29}, {7, 30});
  GPoint tl = ap(a, -9, 22);
  windows(ctx, l, GRect(tl.x, tl.y, au(a, 18), au(a, 30)), 5, night, pal->window, false);
}

#define GOTHIC_SIGNAL 44

LANDMARK(gothic_tower) {
  LANDMARK_BODY;
  POLY({-6, -10}, {6, -10}, {5, 38}, {-5, 38});
  a_rect(ctx, l, a, -6, 38, 6, 42, c);
  POLY({-6, 42}, {-4, 42}, {-5, 50});  // pinnacles
  POLY({4, 42}, {6, 42}, {5, 50});
  POLY({-2, 42}, {2, 42}, {0, 60});    // central spire
}

LANDMARK(noir_bridge) {
  LANDMARK_BODY;
  a_line(ctx, l, a, -26, 4, 26, 4, c, 2);   // deck
  a_line(ctx, l, a, -4, 34, -26, 8, c, 1);  // cables
  a_line(ctx, l, a, 4, 34, 26, 8, c, 1);
  a_rect(ctx, l, a, -5, -10, -2, 34, c);    // tower legs
  a_rect(ctx, l, a, 2, -10, 5, 34, c);
  a_rect(ctx, l, a, -5, 18, 5, 21, c);      // cross beams
  a_rect(ctx, l, a, -5, 30, 5, 34, c);
  POLY({-5, 34}, {5, 34}, {0, 40});
}

/* --- Quackville (cartoon duck town) ---------------------------------------- */

LANDMARK(windmill) {
  LANDMARK_BODY;
  POLY({-6, -10}, {6, -10}, {4, 18}, {-4, 18});
  POLY({-5, 18}, {5, 18}, {0, 24});
  GPoint hub = ap(a, 0, 19);
  int r = au(a, 14);
  for (int i = 0; i < 4; i++) {  // sails
    int32_t ang = DEG_TO_TRIGANGLE(20 + 90 * i);
    GPoint tip = GPoint(hub.x + r * cos_lookup(ang) / TRIG_MAX_RATIO,
                        hub.y + r * sin_lookup(ang) / TRIG_MAX_RATIO);
    line_draw(ctx, hub, tip, &l->sky, c, 3);
  }
}

LANDMARK(duck_water_tower) {
  LANDMARK_BODY;
  a_line(ctx, l, a, -6, -4, -4, 16, c, 2);  // legs
  a_line(ctx, l, a, 6, -4, 4, 16, c, 2);
  a_ellipse(ctx, l, a, 0, 20, 8, 5, c);     // the tank is the duck's body ...
  POLY({-8, 21}, {-12, 26}, {-6, 24});      // ... with a tail
  a_ellipse(ctx, l, a, 3, 28, 4, 4, c);     // head
  a_poly(ctx, l, a, (const int8_t[][2]){{6, 29}, {11, 28}, {6, 26}}, 3, GColorChromeYellow);
}

LANDMARK(town_hall) {
  LANDMARK_BODY;
  a_rect(ctx, l, a, -11, -10, 11, 14, c);
  POLY({-12, 14}, {12, 14}, {0, 20});
  a_rect(ctx, l, a, -3, 14, 3, 30, c);     // clock tower
  POLY({-4, 30}, {4, 30}, {0, 40});
  graphics_context_set_fill_color(ctx, pal->window);
  GPoint clock = ap(a, 0, 25);
  if (region_contains(&l->sky, clock)) circle_safe(ctx, clock, au(a, 2));
}

#define LIGHTHOUSE_SIGNAL 41

LANDMARK(lighthouse) {
  LANDMARK_BODY;
  POLY({-6, -10}, {6, -10}, {4, 36}, {-4, 36});
  const GColor red = GColorRed;  // cartoon stripes
  a_poly(ctx, l, a, (const int8_t[][2]){{-5, 6}, {5, 6}, {5, 12}, {-5, 12}}, 4, red);
  a_poly(ctx, l, a, (const int8_t[][2]){{-5, 20}, {5, 20}, {4, 26}, {-4, 26}}, 4, red);
  a_rect(ctx, l, a, -6, 36, 6, 38, c);  // gallery
  a_rect(ctx, l, a, -3, 38, 3, 44, pal->window);  // lamp room
  POLY({-4, 44}, {4, 44}, {0, 50});
}

LANDMARK(hill_castle) {
  LANDMARK_BODY;
  a_ellipse(ctx, l, a, 0, -2, 18, 10, c);  // hill
  a_rect(ctx, l, a, -6, 5, 6, 20, c);      // keep
  a_rect(ctx, l, a, -10, 4, -6, 25, c);    // towers with pointy roofs
  a_rect(ctx, l, a, 6, 4, 10, 25, c);
  POLY({-11, 25}, {-5, 25}, {-8, 32});
  POLY({5, 25}, {11, 25}, {8, 32});
  a_line(ctx, l, a, 8, 32, 8, 37, c, 1);
  a_poly(ctx, l, a, (const int8_t[][2]){{8, 37}, {12, 36}, {8, 35}}, 3, GColorRed);
}

/* ---------------------------------------------------------------------------
 * Cities
 * ------------------------------------------------------------------------- */

typedef void (*LandmarkFn)(GContext *, const Layout *, const Anchor *, const SkyPalette *, bool);

typedef struct {
  uint8_t houses;
  LandmarkFn landmarks[5];  // slot 3 carries the signal
  int8_t signal_y;          // units above the base of slot 3
} CityDef;

static const CityDef CITIES[CITY_COUNT] = {
    [CITY_VIENNA] = {HOUSES_ALTBAU,
                     {riesenrad, karlskirche, stephansdom, donauturm, dc_tower}, 40},
    [CITY_NEW_YORK] = {HOUSES_TOWERS,
                       {liberty, brooklyn_bridge, chrysler, empire_state, one_wtc},
                       EMPIRE_SIGNAL},
    [CITY_PARIS] = {HOUSES_HAUSSMANN,
                    {sacre_coeur, arc_de_triomphe, notre_dame, eiffel_tower, pantheon},
                    EIFFEL_SIGNAL},
    [CITY_GARGOYLE] = {HOUSES_GOTHIC,
                       {gargoyle, gothic_cathedral, deco_tower, gothic_tower, noir_bridge},
                       GOTHIC_SIGNAL},
    [CITY_QUACKVILLE] = {HOUSES_CARTOON,
                         {windmill, duck_water_tower, town_hall, lighthouse, hill_castle},
                         LIGHTHOUSE_SIGNAL},
};

static const int16_t SLOT_RECT[5] = {100, 300, 450, 600, 900};
static const int16_t SLOT_ROUND[5] = {130, 300, 440, 560, 820};

static const CityDef *city_def(uint8_t city) {
  return &CITIES[city < CITY_COUNT ? city : CITY_VIENNA];
}

static Anchor slot_anchor(const Layout *l, int slot) {
  return anchor_at(l, PM(l->round ? SLOT_ROUND[slot] : SLOT_RECT[slot], l->w));
}

void skyline_draw(GContext *ctx, const Layout *l, const SkyPalette *pal, bool night, uint8_t city) {
  const CityDef *def = city_def(city);
  // Paris keeps the Champ de Mars free so the Eiffel Tower stands on its own.
  Anchor signal = slot_anchor(l, 3);
  int16_t gap = city == CITY_PARIS ? au(&signal, 20) : 0;
  houses(ctx, l, pal, night, def->houses, signal.cx, gap);
  for (int i = 0; i < 5; i++) {
    Anchor a = slot_anchor(l, i);
    def->landmarks[i](ctx, l, &a, pal, night);
  }
}

GPoint skyline_signal_source(const Layout *l, uint8_t city) {
  Anchor a = slot_anchor(l, 3);
  return ap(&a, 0, city_def(city)->signal_y);
}
