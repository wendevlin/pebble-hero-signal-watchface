#pragma once
/*
 * Comic drawing toolkit.
 *
 * The face is a comic page: a few convex panels separated by white gutters.
 * Everything inside a panel is drawn as a polygon that is clipped to that
 * panel's convex region (Sutherland-Hodgman against its edges), so artwork can
 * run right up to a slanted panel border without bleeding into the gutter or
 * the neighbouring panel. All art is procedural, so it scales to every screen
 * size without bitmap resources.
 */

#include <pebble.h>

#define POLY_MAX 32

/*
 * Stack budget: the app stack is small on older watches (basalt/chalk), and
 * the firmware's text renderer needs a good share of it while we are deep in a
 * draw call. Polygon buffers are therefore static (drawing is single threaded
 * and never re-entrant) and the bigger helpers are kept out of line, so their
 * frames never pile up in one caller.
 */
#define NOINLINE __attribute__((noinline))

typedef struct {
  int16_t n;
  GPoint p[POLY_MAX];
} Poly;

/* One edge of a convex region: keep points with a*x + b*y + c >= 0. */
typedef struct {
  int32_t a, b, c;
} HalfPlane;

/* A convex region (a panel), built from its corners in clockwise screen order. */
typedef struct {
  int8_t n;
  HalfPlane h[6];
  GPoint corner[6];
} Region;

/* Screen size; circles are only drawn when fully on it (see comic.c). */
void comic_set_canvas(GSize size);
bool on_canvas(GPoint p, int r);
void circle_safe(GContext *ctx, GPoint p, int r);

void region_init(Region *r, const GPoint *corners, int n);
bool region_contains(const Region *r, GPoint p);

/* Fill a polygon clipped to `clip` (NULL = unclipped). */
void poly_fill(GContext *ctx, const Poly *poly, const Region *clip, GColor color);
/* Fill an axis-aligned rectangle clipped to `clip`. */
void rect_fill(GContext *ctx, GRect rect, const Region *clip, GColor color);
/* Fill (and optionally outline) an ellipse approximated by a polygon. */
void ellipse_fill(GContext *ctx, GPoint c, int rx, int ry, const Region *clip, GColor fill);
void ellipse_outline(GContext *ctx, GPoint c, int rx, int ry, GColor stroke, int width);
/* A line clipped to `clip`. */
void line_draw(GContext *ctx, GPoint a, GPoint b, const Region *clip, GColor color, int width);

/* Fill the region itself, then draw its black border. */
void region_fill(GContext *ctx, const Region *r, GColor color);
void region_outline(GContext *ctx, const Region *r, GColor color, int width);

/*
 * Ben-Day halftone: a staggered dot grid over the region. Dot radius grows
 * from r_min to r_max with distance from `focus` (up to `reach` px), the
 * classic comic print shading.
 */
void halftone(GContext *ctx, const Region *r, int spacing, GPoint focus, int reach,
              int r_min, int r_max, GColor color);

/* Alternating speed rays fanning out of `center`, clipped to the region. */
void rays(GContext *ctx, const Region *r, GPoint center, int count, int rotation_deg,
          GColor color);

/* A spiky "sound effect" burst. */
void burst_points(Poly *out, GPoint c, int r_out, int r_in, int spikes, int rotation_deg,
                  int squash_pct);

/*
 * Comic lettering: `fill` text with an `outline` stroke of `outline_px` and a
 * hard drop shadow offset by `shadow` (both in the outline colour).
 */
void comic_text(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
                GColor fill, GColor outline, int outline_px, GPoint shadow);

/* Size of the ink of `text` in `font` (no outline). */
GSize text_size(const char *text, GFont font);
