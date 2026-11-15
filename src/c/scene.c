/*
 * The comic page.
 *
 *  +---------------------------------+
 *  | DATE caption      weather  temp |  SKY: halftone sky, skyline, and the
 *  |        (signal)  /              |  hero signal beamed from a rooftop.
 *  |   ___|^|__ ___/|__|^^|___       |  Signal cuts out -> "KRZZT!" burst.
 *  |\_______________________________ |
 *  |        12:34                    |  ACTION: speed rays + halftone in the
 *  | ________________________________|  hero colour, comic-lettered time.
 *  | [foot] 8.432     /  [bolt] 80%  |  POWER (steps vs goal) and ENERGY
 *  | [=====-----]    /   [=======--] |  (battery) meters.
 *  +---------------------------------+
 *
 * Panels are convex regions separated by white gutters; see comic.h.
 */

#include "scene.h"
#include "comic.h"

static GFont s_time_font;
static GFont s_small_font;
static GFont s_sfx_font;

#if defined(PBL_PLATFORM_GABBRO)
#define RES_TIME RESOURCE_ID_FONT_TIME_74
#define RES_SMALL RESOURCE_ID_FONT_SMALL_24
#define RES_SFX RESOURCE_ID_FONT_SFX_34
#elif defined(PBL_PLATFORM_EMERY)
#define RES_TIME RESOURCE_ID_FONT_TIME_66
#define RES_SMALL RESOURCE_ID_FONT_SMALL_22
#define RES_SFX RESOURCE_ID_FONT_SFX_30
#elif defined(PBL_PLATFORM_CHALK)
#define RES_TIME RESOURCE_ID_FONT_TIME_54
#define RES_SMALL RESOURCE_ID_FONT_SMALL_20
#define RES_SFX RESOURCE_ID_FONT_SFX_24
#else
#define RES_TIME RESOURCE_ID_FONT_TIME_50
#define RES_SMALL RESOURCE_ID_FONT_SMALL_20
#define RES_SFX RESOURCE_ID_FONT_SFX_24
#endif

void scene_init(void) {
  s_time_font = fonts_load_custom_font(resource_get_handle(RES_TIME));
  s_small_font = fonts_load_custom_font(resource_get_handle(RES_SMALL));
  s_sfx_font = fonts_load_custom_font(resource_get_handle(RES_SFX));
}

void scene_deinit(void) {
  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_small_font);
  fonts_unload_custom_font(s_sfx_font);
}

/* ---------------------------------------------------------------------------
 * Palette
 * ------------------------------------------------------------------------- */

typedef struct {
  GColor base, ray, dot;
} HeroPalette;

static HeroPalette hero_palette(uint8_t hero) {
  switch (hero) {
    case HERO_BLUE:
      return (HeroPalette){GColorBlueMoon, GColorPictonBlue, GColorDukeBlue};
    case HERO_GREEN:
      return (HeroPalette){GColorIslamicGreen, GColorMalachite, GColorDarkGreen};
    case HERO_PURPLE:
      return (HeroPalette){GColorPurple, GColorShockingPink, GColorImperialPurple};
    default:
      return (HeroPalette){GColorRed, GColorSunsetOrange, GColorDarkCandyAppleRed};
  }
}

/* ---------------------------------------------------------------------------
 * Layout
 * ------------------------------------------------------------------------- */

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
  int8_t signal_building;
} Layout;

#define PM(v, total) ((int32_t)(v) * (total) / 1000)

static int16_t line_y(GPoint a, GPoint b, int16_t x) {
  if (b.x == a.x) return a.y;
  return a.y + (int32_t)(b.y - a.y) * (x - a.x) / (b.x - a.x);
}

static void layout_compute(Layout *l, GRect bounds) {
  const int16_t w = bounds.size.w, h = bounds.size.h;
  l->w = w;
  l->h = h;
  l->g = w / 60 + 1;
  l->round = PBL_IF_ROUND_ELSE(true, false);
  const int16_t g = l->g, o = 6;  // o: off-screen overscan so borders vanish

  if (l->round) {
    l->a0 = GPoint(0, PM(440, h));
    l->a1 = GPoint(w, PM(360, h));
    l->b0 = GPoint(0, PM(700, h));
    l->b1 = GPoint(w, PM(760, h));
    l->c0 = GPoint(PM(520, w), 0);
    l->c1 = GPoint(PM(470, w), h);
  } else {
    l->a0 = GPoint(0, PM(460, h));
    l->a1 = GPoint(w, PM(370, h));
    l->b0 = GPoint(0, PM(745, h));
    l->b1 = GPoint(w, PM(800, h));
    l->c0 = GPoint(PM(545, w), 0);
    l->c1 = GPoint(PM(475, w), h);
  }
  l->c0.y = line_y(l->b0, l->b1, l->c0.x);

  GPoint sky[4] = {GPoint(-o, -o), GPoint(w + o, -o), GPoint(w + o, l->a1.y - g),
                   GPoint(-o, l->a0.y - g)};
  region_init(&l->sky, sky, 4);
  GPoint mid[4] = {GPoint(-o, l->a0.y + g), GPoint(w + o, l->a1.y + g),
                   GPoint(w + o, l->b1.y - g), GPoint(-o, l->b0.y - g)};
  region_init(&l->mid, mid, 4);
  int16_t cl0 = l->c0.x - g, cr0 = l->c0.x + g;
  GPoint steps[4] = {GPoint(-o, l->b0.y + g), GPoint(cl0, line_y(l->b0, l->b1, cl0) + g),
                     GPoint(l->c1.x - g, h + o), GPoint(-o, h + o)};
  region_init(&l->steps, steps, 4);
  GPoint batt[4] = {GPoint(cr0, line_y(l->b0, l->b1, cr0) + g), GPoint(w + o, l->b1.y + g),
                    GPoint(w + o, h + o), GPoint(l->c1.x + g, h + o)};
  region_init(&l->batt, batt, 4);

  const int16_t mid_top = line_y(l->a0, l->a1, w / 2) + g;
  const int16_t mid_bottom = line_y(l->b0, l->b1, w / 2) - g;
  l->time_center = GPoint(w / 2, (mid_top + mid_bottom) / 2);

  if (l->round) {
    l->caption = GPoint(w / 2, PM(80, h));
    l->weather = GPoint(PM(700, w), PM(220, h));
    l->wsize = PM(62, w);
    l->emblem = GPoint(PM(335, w), PM(255, h));
    l->erx = PM(100, w);
    l->ery = PM(56, h);
    l->content_w = PM(230, w);
    l->signal_building = 6;
    // Small round screens (chalk) need the two blocks pushed further apart.
    int16_t sx = PM(w < 200 ? 300 : 335, w), bx = PM(w < 200 ? 690 : 650, w);
    int16_t sy = line_y(l->b0, l->b1, sx) + g, by = line_y(l->b0, l->b1, bx) + g;
    l->steps_anchor = GPoint(sx, sy + (h - sy) * 30 / 100);
    l->batt_anchor = GPoint(bx, by + (h - by) * 30 / 100);
  } else {
    l->caption = GPoint(g + 1, g + 1);
    l->weather = GPoint(w - PM(150, w), PM(125, h));
    l->wsize = PM(85, w);
    l->emblem = GPoint(PM(300, w), PM(225, h));
    l->erx = PM(120, w);
    l->ery = PM(64, h);
    l->content_w = PM(400, w);
    l->signal_building = 7;
    int16_t sx = PM(255, w), bx = PM(760, w);
    int16_t sy = line_y(l->b0, l->b1, sx) + g, by = line_y(l->b0, l->b1, bx) + g;
    l->steps_anchor = GPoint(sx, (sy + h) / 2);
    l->batt_anchor = GPoint(bx, (by + h) / 2);
  }
}

/* ---------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */

static void poly_outline(GContext *ctx, Poly *p, GColor color, int width) {
  GPath path = {.num_points = p->n, .points = p->p, .rotation = 0, .offset = GPointZero};
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  gpath_draw_outline(ctx, &path);
}

static void circle(GContext *ctx, GPoint c, int r, GColor fill, int outline) {
  if (outline > 0) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    circle_safe(ctx, c, r + outline);
  }
  graphics_context_set_fill_color(ctx, fill);
  circle_safe(ctx, c, r);
}

/* A unit lightning bolt (coordinates in 1/100 of its half-height). */
static const GPoint BOLT[] = {{18, -100}, {-52, 12}, {-6, 12}, {-24, 100},
                              {52, -18},  {6, -18}};

NOINLINE static void bolt(GContext *ctx, GPoint c, int half_h, GColor fill, int outline) {
  static Poly p;
  p.n = ARRAY_LENGTH(BOLT);
  for (int i = 0; i < p.n; i++) {
    p.p[i] = GPoint(c.x + BOLT[i].x * half_h / 100, c.y + BOLT[i].y * half_h / 100);
  }
  poly_fill(ctx, &p, NULL, fill);
  if (outline > 0) poly_outline(ctx, &p, GColorBlack, outline);
}

/* ---------------------------------------------------------------------------
 * SKY panel: sky, weather, skyline, hero signal, date caption
 * ------------------------------------------------------------------------- */

typedef struct {
  GColor sky, dot, building, window;
} SkyPalette;

static bool cond_is_wet(uint8_t c) {
  return c == COND_RAIN || c == COND_POURING || c == COND_SLEET || c == COND_THUNDER ||
         c == COND_HAIL;
}

static SkyPalette sky_palette(const Scene *s) {
  if (!s->is_day) {
    if (s->cond == COND_THUNDER)
      return (SkyPalette){GColorBlack, GColorImperialPurple, GColorOxfordBlue, GColorIcterine};
    return (SkyPalette){GColorOxfordBlue, GColorDukeBlue, GColorBlack, GColorIcterine};
  }
  if (s->cond == COND_THUNDER || s->cond == COND_POURING)
    return (SkyPalette){GColorDarkGray, GColorBlack, GColorOxfordBlue, GColorIcterine};
  if (cond_is_wet(s->cond) || s->cond == COND_CLOUDY || s->cond == COND_FOG ||
      s->cond == COND_SNOW)
    return (SkyPalette){GColorCadetBlue, GColorTiffanyBlue, GColorOxfordBlue, GColorCeleste};
  return (SkyPalette){GColorPictonBlue, GColorCeleste, GColorOxfordBlue, GColorCeleste};
}

NOINLINE static void cloud(GContext *ctx, GPoint c, int s, GColor fill, const Region *clip) {
  for (int pass = 0; pass < 2; pass++) {
    GColor col = pass ? fill : GColorBlack;
    int gr = pass ? 0 : 2;
    ellipse_fill(ctx, GPoint(c.x - s * 55 / 100, c.y + s * 20 / 100), s * 45 / 100 + gr,
                 s * 30 / 100 + gr, clip, col);
    ellipse_fill(ctx, GPoint(c.x - s * 5 / 100, c.y - s * 5 / 100), s * 55 / 100 + gr,
                 s * 55 / 100 + gr, clip, col);
    ellipse_fill(ctx, GPoint(c.x + s * 55 / 100, c.y + s * 15 / 100), s * 45 / 100 + gr,
                 s * 35 / 100 + gr, clip, col);
    rect_fill(ctx, GRect(c.x - s * 55 / 100, c.y + s * 10 / 100 - gr, s * 110 / 100,
                         s * 40 / 100 + 2 * gr),
              clip, col);
  }
}

NOINLINE static void sun(GContext *ctx, GPoint c, int s) {
  static Poly rays_poly;
  burst_points(&rays_poly, c, s * 165 / 100, s * 105 / 100, 10, 9, 100);
  poly_fill(ctx, &rays_poly, NULL, GColorChromeYellow);
  poly_outline(ctx, &rays_poly, GColorBlack, 2);
  circle(ctx, c, s * 85 / 100, GColorYellow, 2);
}

NOINLINE static void moon(GContext *ctx, GPoint c, int s, GColor sky) {
  circle(ctx, c, s * 80 / 100, GColorPastelYellow, 2);
  graphics_context_set_fill_color(ctx, sky);
  circle_safe(ctx, GPoint(c.x + s * 45 / 100, c.y - s * 30 / 100), s * 62 / 100);
}

static void sparkle(GContext *ctx, GPoint p, int r, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(p.x - r, p.y), GPoint(p.x + r, p.y));
  graphics_draw_line(ctx, GPoint(p.x, p.y - r), GPoint(p.x, p.y + r));
}

/* Rain/snow/hail falling across the whole sky panel (behind the skyline). */
NOINLINE static void precipitation(GContext *ctx, const Layout *l, const Scene *s) {
  const bool rain = s->cond == COND_RAIN || s->cond == COND_POURING || s->cond == COND_SLEET ||
                    s->cond == COND_THUNDER;
  const bool snow = s->cond == COND_SNOW || s->cond == COND_SLEET;
  const bool hail = s->cond == COND_HAIL;
  if (!rain && !snow && !hail) return;
  const int step = s->cond == COND_POURING ? 7 : 11;
  int row = 0;
  for (int y = 4; y < l->a0.y; y += step + 3, row++) {
    for (int x = (row & 1) ? step / 2 : 0; x < l->w + 8; x += step) {
      GPoint p = GPoint(x + (row * 5) % 7, y);
      if (rain && ((x / step + row) % 2 == 0 || !snow)) {
        line_draw(ctx, p, GPoint(p.x - 3, p.y + 7), &l->sky,
                  s->is_day ? GColorCeleste : GColorVividCerulean, 1);
      } else if (snow && region_contains(&l->sky, p)) {
        graphics_context_set_fill_color(ctx, GColorWhite);
        circle_safe(ctx, p, 1);
      } else if (hail && region_contains(&l->sky, p)) {
        circle(ctx, p, 1, GColorWhite, 1);
      }
    }
  }
}

NOINLINE static void weather_icon(GContext *ctx, const Layout *l, const Scene *s, GColor sky) {
  const GPoint c = l->weather;
  const int z = l->wsize;
  switch (s->cond) {
    case COND_CLEAR:
      if (s->is_day) {
        sun(ctx, c, z);
      } else {
        moon(ctx, c, z, sky);
        sparkle(ctx, GPoint(c.x - z * 18 / 10, c.y - z * 4 / 10), 2, GColorWhite);
        sparkle(ctx, GPoint(c.x - z * 9 / 10, c.y + z * 9 / 10), 1, GColorWhite);
      }
      break;
    case COND_PARTLY:
      if (s->is_day) {
        sun(ctx, GPoint(c.x + z * 4 / 10, c.y - z * 3 / 10), z * 8 / 10);
      } else {
        moon(ctx, GPoint(c.x + z * 4 / 10, c.y - z * 3 / 10), z * 9 / 10, sky);
      }
      cloud(ctx, GPoint(c.x - z * 3 / 10, c.y + z * 3 / 10), z * 12 / 10, GColorWhite, NULL);
      break;
    case COND_CLOUDY:
    case COND_FOG:
      cloud(ctx, GPoint(c.x + z * 5 / 10, c.y - z * 4 / 10), z, GColorLightGray, NULL);
      cloud(ctx, GPoint(c.x - z * 2 / 10, c.y + z * 2 / 10), z * 13 / 10, GColorWhite, NULL);
      if (s->cond == COND_FOG) {
        for (int i = 0; i < 3; i++) {
          int y = c.y + z * 9 / 10 + i * 4;
          line_draw(ctx, GPoint(c.x - z + i * 3, y), GPoint(c.x + z - i * 2, y), NULL,
                    GColorWhite, 2);
        }
      }
      break;
    case COND_RAIN:
    case COND_POURING:
    case COND_SNOW:
    case COND_SLEET:
    case COND_HAIL:
      cloud(ctx, c, z * 13 / 10, s->cond == COND_POURING ? GColorLightGray : GColorWhite, NULL);
      break;
    case COND_THUNDER:
      cloud(ctx, c, z * 13 / 10, GColorLightGray, NULL);
      bolt(ctx, GPoint(c.x + z / 10, c.y + z * 9 / 10), z * 6 / 10, GColorYellow, 2);
      break;
    case COND_WIND:
      for (int i = 0; i < 3; i++) {
        int y = c.y - z / 2 + i * z / 2;
        int x0 = c.x - z + (i == 1 ? 0 : z / 3), x1 = c.x + z - (i == 2 ? z / 2 : 0);
        line_draw(ctx, GPoint(x0, y), GPoint(x1, y), NULL, GColorBlack, 5);
        line_draw(ctx, GPoint(x0, y), GPoint(x1, y), NULL, GColorWhite, 3);
      }
      break;
    default:
      break;
  }
}

/* x position (permille), width (permille), height (permille of screen height) */
static const int16_t SKYLINE[][3] = {
    {-20, 110, 70},  {80, 85, 115},  {160, 115, 60},  {270, 70, 125}, {335, 95, 85},
    {425, 95, 55},   {515, 105, 90}, {615, 85, 95},   {695, 115, 70}, {805, 75, 120},
    {875, 140, 80},
};

static int16_t building_top(const Layout *l, int i, int16_t *x, int16_t *bw) {
  *x = PM(SKYLINE[i][0], l->w);
  *bw = PM(SKYLINE[i][1], l->w);
  int16_t base = line_y(l->a0, l->a1, *x + *bw / 2) - l->g;
  int16_t height =
      i == l->signal_building ? PM(l->round ? 120 : 110, l->h) : PM(SKYLINE[i][2], l->h);
  return base - height;
}

NOINLINE static void skyline(GContext *ctx, const Layout *l, const SkyPalette *pal, bool night) {
  for (unsigned i = 0; i < ARRAY_LENGTH(SKYLINE); i++) {
    int16_t x, bw;
    int16_t top = building_top(l, i, &x, &bw);
    int16_t bottom = l->a0.y + l->g + 4;
    rect_fill(ctx, GRect(x, top, bw, bottom - top), &l->sky, pal->building);
    // Windows
    graphics_context_set_fill_color(ctx, pal->window);
    int k = 0;
    for (int16_t wy = top + 4; wy < bottom; wy += 7) {
      for (int16_t wx = x + 3; wx < x + bw - 3; wx += 6, k++) {
        bool lit = night ? ((k * 7 + i * 3) % 5 < 2) : ((k * 5 + i) % 4 == 0);
        if (!lit) continue;
        if (!region_contains(&l->sky, GPoint(wx + 2, wy + 3))) continue;
        graphics_fill_rect(ctx, GRect(wx, wy, 2, 3), 0, GCornerNone);
      }
    }
  }
}

static GPoint signal_lamp(const Layout *l) {
  int16_t x, bw;
  int16_t top = building_top(l, l->signal_building, &x, &bw);
  return GPoint(x + bw / 2, top - 3);
}

static int32_t isqrt32(int32_t v) {
  if (v <= 0) return 0;
  int32_t x = v, y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + v / x) / 2;
  }
  return x;
}

NOINLINE static void signal_beam(GContext *ctx, const Layout *l) {
  GPoint o = signal_lamp(l);
  GPoint e = l->emblem;
  int32_t dx = e.x - o.x, dy = e.y - o.y;
  int32_t len = isqrt32(dx * dx + dy * dy);
  if (len == 0) return;
  // Perpendicular to the beam, scaled to the emblem's half width.
  int32_t px = -dy * l->erx * 9 / 10 / len, py = dx * l->ery * 9 / 10 / len;
  static Poly beam;
  beam.n = 4;
  beam.p[0] = GPoint(o.x - 2, o.y);
  beam.p[1] = GPoint(o.x + 2, o.y);
  beam.p[2] = GPoint(e.x + px, e.y + py);
  beam.p[3] = GPoint(e.x - px, e.y - py);
  poly_fill(ctx, &beam, &l->sky, GColorPastelYellow);
}

NOINLINE static void signal_lamp_draw(GContext *ctx, const Layout *l, bool on) {
  GPoint p = signal_lamp(l);
  int r = l->w / 40 + 1;
  static Poly lamp;
  lamp.n = 4;
  lamp.p[0] = GPoint(p.x - r - 1, p.y + r + 1);
  lamp.p[1] = GPoint(p.x + r + 1, p.y + r + 1);
  lamp.p[2] = GPoint(p.x + r, p.y - r);
  lamp.p[3] = GPoint(p.x - r, p.y - r);
  poly_fill(ctx, &lamp, NULL, GColorDarkGray);
  poly_outline(ctx, &lamp, GColorBlack, 1);
  graphics_context_set_fill_color(ctx, on ? GColorYellow : GColorBlack);
  graphics_fill_rect(ctx, GRect(p.x - r + 1, p.y - r + 1, 2 * r - 2, 2), 0, GCornerNone);
}

NOINLINE static void emblem(GContext *ctx, const Layout *l, bool on) {
  GPoint c = l->emblem;
  if (on) {
    ellipse_fill(ctx, c, l->erx, l->ery, NULL, GColorIcterine);
    ellipse_outline(ctx, c, l->erx, l->ery, GColorBlack, 2);
    bolt(ctx, c, l->ery * 8 / 10, GColorBlack, 0);
  }
}

NOINLINE static void sfx_burst(GContext *ctx, const Layout *l) {
  GPoint c = GPoint(l->emblem.x, l->emblem.y + l->ery / 3);
  static Poly outer;
  burst_points(&outer, c, l->erx * 23 / 10, l->erx * 155 / 100, 12, 4, 72);
  poly_fill(ctx, &outer, NULL, GColorYellow);
  poly_outline(ctx, &outer, GColorBlack, 2);
  static Poly inner;
  burst_points(&inner, c, l->erx * 17 / 10, l->erx * 125 / 100, 12, 19, 66);
  poly_fill(ctx, &inner, NULL, GColorWhite);
  const char *text = "KRZZT!";
  GSize sz = text_size(text, s_sfx_font);
  GRect box = GRect(c.x - sz.w / 2 - 8, c.y - sz.h / 2 - sz.h / 6, sz.w + 16, sz.h + 8);
  comic_text(ctx, text, s_sfx_font, box, GTextAlignmentCenter, GColorRed, GColorBlack, 2,
             GPoint(2, 2));
}

static const char *const DAYS_EN[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const DAYS_DE[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
static const char *const MONTHS_EN[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char *const MONTHS_DE[] = {"JAN", "FEB", "MÄR", "APR", "MAI", "JUN",
                                        "JUL", "AUG", "SEP", "OKT", "NOV", "DEZ"};

NOINLINE static void date_caption(GContext *ctx, const Layout *l, const Scene *s) {
  char text[24];
  const struct tm *t = &s->now;
  if (s->german) {
    snprintf(text, sizeof(text), "%s %d. %s", DAYS_DE[t->tm_wday], t->tm_mday,
             MONTHS_DE[t->tm_mon]);
  } else {
    snprintf(text, sizeof(text), "%s %d %s", DAYS_EN[t->tm_wday], t->tm_mday,
             MONTHS_EN[t->tm_mon]);
  }
  GSize sz = text_size(text, s_small_font);
  const int pad_x = 5, pad_y = 2;
  const int box_h = sz.h * 70 / 100 + 2 * pad_y + 2;
  int16_t x = l->round ? l->caption.x - (sz.w + 2 * pad_x) / 2 : l->caption.x;
  GRect box = GRect(x, l->caption.y, sz.w + 2 * pad_x, box_h);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(box.origin.x + 2, box.origin.y + 2, box.size.w, box.size.h), 0,
                     GCornerNone);
  graphics_fill_rect(ctx, box, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorIcterine);
  graphics_fill_rect(ctx, grect_inset(box, GEdgeInsets(2)), 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorBlack);
  GRect tbox = GRect(box.origin.x, box.origin.y - sz.h * 26 / 100 + pad_y, box.size.w, sz.h);
  graphics_draw_text(ctx, text, s_small_font, tbox, GTextOverflowModeWordWrap, GTextAlignmentCenter,
                     NULL);
}

NOINLINE static void temperature(GContext *ctx, const Layout *l, const Scene *s) {
  char text[12];
  if (s->has_weather) {
    int32_t t10 = s->temp_tenths_c;
    if (s->fahrenheit) t10 = t10 * 9 / 5 + 320;
    int32_t deg = (t10 >= 0 ? t10 + 5 : t10 - 5) / 10;
    snprintf(text, sizeof(text), "%d°", (int)deg);
  } else {
    snprintf(text, sizeof(text), "--°");
  }
  GSize sz = text_size(text, s_small_font);
  GPoint c = l->weather;
  int16_t ty = c.y + l->wsize * (l->round ? 12 : 11) / 10;
  GRect box = GRect(c.x - sz.w / 2 - 10, ty - sz.h * 30 / 100, sz.w + 20, sz.h + 4);
  comic_text(ctx, text, s_small_font, box, GTextAlignmentCenter, GColorWhite, GColorBlack, 2,
             GPoint(2, 2));
}

NOINLINE static void draw_sky(GContext *ctx, const Layout *l, const Scene *s) {
  SkyPalette pal = sky_palette(s);
  region_fill(ctx, &l->sky, pal.sky);
  halftone(ctx, &l->sky, 6, GPoint(l->w / 2, l->a1.y), l->h / 2, 2, 0, pal.dot);
  if (!s->is_day && s->cond != COND_THUNDER && s->cond != COND_POURING) {
    // A few stars
    static const int16_t STARS[][2] = {{120, 120}, {300, 60}, {560, 90}, {640, 210},
                                       {880, 300}, {220, 290}, {760, 40}};
    for (unsigned i = 0; i < ARRAY_LENGTH(STARS); i++) {
      GPoint p = GPoint(PM(STARS[i][0], l->w), PM(STARS[i][1], l->h));
      if (region_contains(&l->sky, p)) sparkle(ctx, p, (i % 3) ? 1 : 2, GColorWhite);
    }
  }
  precipitation(ctx, l, s);
  if (s->connected) signal_beam(ctx, l);
  weather_icon(ctx, l, s, pal.sky);
  skyline(ctx, l, &pal, !s->is_day);
  signal_lamp_draw(ctx, l, s->connected);
  emblem(ctx, l, s->connected);
  temperature(ctx, l, s);
}

/* ---------------------------------------------------------------------------
 * ACTION panel: the time
 * ------------------------------------------------------------------------- */

NOINLINE static void draw_action(GContext *ctx, const Layout *l, const Scene *s) {
  HeroPalette hp = hero_palette(s->hero);
  region_fill(ctx, &l->mid, hp.base);
  rays(ctx, &l->mid, l->time_center, 18, 3, hp.ray);
  halftone(ctx, &l->mid, 6, l->time_center, l->w * 6 / 10, 0, 2, hp.dot);

  char text[8];
  int hour = s->now.tm_hour;
  if (!s->h24) {
    hour %= 12;
    if (hour == 0) hour = 12;
    snprintf(text, sizeof(text), "%d:%02d", hour, s->now.tm_min);
  } else {
    snprintf(text, sizeof(text), "%02d:%02d", hour, s->now.tm_min);
  }
  GSize sz = text_size(text, s_time_font);
  const int o = l->w >= 200 ? 3 : 2;
  GRect box = GRect(l->time_center.x - sz.w / 2 - 12, l->time_center.y - sz.h * 62 / 100,
                    sz.w + 24, sz.h + 12);
  comic_text(ctx, text, s_time_font, box, GTextAlignmentCenter, GColorYellow, GColorBlack, o,
             GPoint(o + 1, o + 1));
}

/* ---------------------------------------------------------------------------
 * POWER (steps) and ENERGY (battery) panels
 * ------------------------------------------------------------------------- */

static void foot(GContext *ctx, GPoint c, int z) {
  ellipse_fill(ctx, GPoint(c.x, c.y - z * 14 / 100), z * 20 / 100, z * 30 / 100, NULL,
               GColorBlack);
  ellipse_fill(ctx, GPoint(c.x, c.y + z * 32 / 100), z * 16 / 100, z * 14 / 100, NULL,
               GColorBlack);
}

/* Two footprints, one a step ahead of the other. */
NOINLINE static void footprint(GContext *ctx, GPoint c, int z) {
  foot(ctx, GPoint(c.x - z * 22 / 100, c.y + z * 12 / 100), z);
  foot(ctx, GPoint(c.x + z * 22 / 100, c.y - z * 12 / 100), z);
}

NOINLINE static void meter(GContext *ctx, GRect r, int filled, int n, GColor color) {
  const int gap = 2;
  int seg = (r.size.w - (n - 1) * gap) / n;
  for (int i = 0; i < n; i++) {
    GRect sr = GRect(r.origin.x + i * (seg + gap), r.origin.y, seg, r.size.h);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, sr, 0, GCornerNone);
    graphics_context_set_fill_color(ctx, i < filled ? color : GColorWhite);
    graphics_fill_rect(ctx, grect_inset(sr, GEdgeInsets(1)), 0, GCornerNone);
  }
}

static void format_thousands(char *buf, size_t len, int32_t v, bool german) {
  if (v >= 1000) {
    snprintf(buf, len, "%d%c%03d", (int)(v / 1000), german ? '.' : ',', (int)(v % 1000));
  } else {
    snprintf(buf, len, "%d", (int)v);
  }
}

/* Icon on the left, value on the right, meter underneath, centred on anchor. */
NOINLINE static void stat_block(GContext *ctx, const Layout *l, GPoint anchor, const char *text,
                       int filled, GColor meter_color, bool steps, bool charging) {
  const int cw = l->content_w;
  const int z = l->w < 200 ? l->w / 12 : l->w / 9;  // icon size
  const int bar_h = l->w >= 200 ? 7 : 6;
  GSize sz = text_size(text, s_small_font);
  const int row_h = z + 2;
  const int total_h = row_h + 4 + bar_h;
  const int top = anchor.y - total_h / 2;
  const int row_w = z + 4 + sz.w;
  const int left = anchor.x - row_w / 2;
  GPoint icon = GPoint(left + z / 2, top + row_h / 2);
  if (steps) {
    footprint(ctx, icon, z);
  } else {
    bolt(ctx, icon, z * 55 / 100, charging ? GColorGreen : GColorYellow, 2);
  }
  GRect tbox = GRect(left + z + 4 - 4, top + row_h / 2 - sz.h * 62 / 100, sz.w + 8, sz.h + 4);
  comic_text(ctx, text, s_small_font, tbox, GTextAlignmentLeft, GColorWhite, GColorBlack, 2,
             GPoint(0, 0));
  meter(ctx, GRect(anchor.x - cw / 2, top + row_h + 4, cw, bar_h), filled, 5, meter_color);
}

NOINLINE static void draw_bottom(GContext *ctx, const Layout *l, const Scene *s) {
  region_fill(ctx, &l->steps, GColorIcterine);
  halftone(ctx, &l->steps, 6, GPoint(l->c0.x, l->c0.y), l->w / 2, 2, 0, GColorChromeYellow);
  region_fill(ctx, &l->batt, GColorVividCerulean);
  halftone(ctx, &l->batt, 6, GPoint(l->c0.x, l->c0.y), l->w / 2, 2, 0, GColorBlueMoon);

  char text[16];
  if (s->has_steps) {
    format_thousands(text, sizeof(text), s->steps, s->german);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  int goal = s->step_goal > 0 ? s->step_goal : 8000;
  int filled = s->has_steps ? (int)(s->steps * 5 / goal) : 0;
  if (filled > 5) filled = 5;
  stat_block(ctx, l, l->steps_anchor, text, filled,
             filled >= 5 ? GColorGreen : GColorRed, true, false);

  snprintf(text, sizeof(text), "%d%%", s->battery);
  int bfill = (s->battery + 10) / 20;
  if (bfill > 5) bfill = 5;
  GColor bc = s->battery <= 20 ? GColorRed : s->battery <= 40 ? GColorChromeYellow : GColorGreen;
  stat_block(ctx, l, l->batt_anchor, text, bfill, bc, false, s->charging);
}

/* ---------------------------------------------------------------------------
 * Page
 * ------------------------------------------------------------------------- */

void scene_draw(GContext *ctx, GRect bounds, const Scene *s) {
  static Layout l;  // static: keep the (small) app stack free for the firmware
  layout_compute(&l, bounds);
  comic_set_canvas(bounds.size);
  graphics_context_set_antialiased(ctx, true);

  // Gutters: the white paper under the panels.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  draw_action(ctx, &l, s);
  draw_bottom(ctx, &l, s);
  draw_sky(ctx, &l, s);

  region_outline(ctx, &l.sky, GColorBlack, 2);
  region_outline(ctx, &l.mid, GColorBlack, 2);
  region_outline(ctx, &l.steps, GColorBlack, 2);
  region_outline(ctx, &l.batt, GColorBlack, 2);

  // Lettering and effects that break out of the panel borders go last.
  date_caption(ctx, &l, s);
  if (!s->connected) sfx_burst(ctx, &l);
}
