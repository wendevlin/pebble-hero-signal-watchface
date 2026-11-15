#include "comic.h"

/* ---------------------------------------------------------------------------
 * Convex regions + polygon clipping
 * ------------------------------------------------------------------------- */

void region_init(Region *r, const GPoint *corners, int n) {
  r->n = n;
  for (int i = 0; i < n; i++) {
    GPoint p = corners[i];
    GPoint q = corners[(i + 1) % n];
    // Clockwise on screen (y down): the inside is where the cross product of
    // the edge and (point - p) is non-negative.
    int32_t a = -(q.y - p.y);
    int32_t b = q.x - p.x;
    r->h[i].a = a;
    r->h[i].b = b;
    r->h[i].c = -(a * p.x + b * p.y);
    r->corner[i] = p;
  }
}

static inline int32_t side(const HalfPlane *h, GPoint p) {
  return h->a * p.x + h->b * p.y + h->c;
}

bool region_contains(const Region *r, GPoint p) {
  for (int i = 0; i < r->n; i++) {
    if (side(&r->h[i], p) < 0) return false;
  }
  return true;
}

static void clip_half(const Poly *in, Poly *out, const HalfPlane *h) {
  out->n = 0;
  if (in->n == 0) return;
  GPoint s = in->p[in->n - 1];
  int32_t ds = side(h, s);
  for (int i = 0; i < in->n; i++) {
    GPoint e = in->p[i];
    int32_t de = side(h, e);
    if ((ds >= 0) != (de >= 0)) {
      int32_t den = ds - de;
      if (den != 0 && out->n < POLY_MAX) {
        out->p[out->n++] = GPoint(s.x + (int32_t)(e.x - s.x) * ds / den,
                                  s.y + (int32_t)(e.y - s.y) * ds / den);
      }
    }
    if (de >= 0 && out->n < POLY_MAX) out->p[out->n++] = e;
    s = e;
    ds = de;
  }
}

static Region s_canvas;
static void clip_half(const Poly *in, Poly *out, const HalfPlane *h);

static void fill_raw(GContext *ctx, const Poly *poly, GColor color) {
  if (poly->n < 3) return;
  // Clip to the screen as well: like circles (below), polygons that cross the
  // screen edge are not clipped by older firmware and write out of bounds.
  static Poly s_c1, s_c2;
  if (s_canvas.n == 4) {
    Poly *cur = &s_c1, *next = &s_c2;
    *cur = *poly;
    for (int i = 0; i < 4; i++) {
      clip_half(cur, next, &s_canvas.h[i]);
      Poly *t = cur;
      cur = next;
      next = t;
      if (cur->n < 3) return;
    }
    poly = cur;
  }
  // Remove consecutive duplicates (see ellipse_points).
  static Poly s_clean;
  s_clean.n = 0;
  for (int i = 0; i < poly->n; i++) {
    GPoint p = poly->p[i];
    if (s_clean.n > 0 && gpoint_equal(&p, &s_clean.p[s_clean.n - 1])) continue;
    s_clean.p[s_clean.n++] = p;
  }
  while (s_clean.n > 1 && gpoint_equal(&s_clean.p[0], &s_clean.p[s_clean.n - 1])) s_clean.n--;
  if (s_clean.n < 3) return;
  poly = &s_clean;
  GPath path = {
      .num_points = poly->n,
      .points = (GPoint *)poly->p,
      .rotation = 0,
      .offset = GPointZero,
  };
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, &path);
}

NOINLINE void poly_fill(GContext *ctx, const Poly *poly, const Region *clip, GColor color) {
  if (!clip) {
    fill_raw(ctx, poly, color);
    return;
  }
  // Scratch buffers live in static memory: the app stack is small on older
  // watches (basalt/chalk) and the firmware's own path filler needs it too.
  static Poly s_a, s_b;
  Poly *cur = &s_a, *next = &s_b;
  *cur = *poly;
  for (int i = 0; i < clip->n; i++) {
    clip_half(cur, next, &clip->h[i]);
    Poly *t = cur;
    cur = next;
    next = t;
    if (cur->n < 3) return;
  }
  fill_raw(ctx, cur, color);
}

NOINLINE void rect_fill(GContext *ctx, GRect rect, const Region *clip, GColor color) {
  static Poly p;
  p.n = 4;
  p.p[0] = GPoint(rect.origin.x, rect.origin.y);
  p.p[1] = GPoint(rect.origin.x + rect.size.w, rect.origin.y);
  p.p[2] = GPoint(rect.origin.x + rect.size.w, rect.origin.y + rect.size.h);
  p.p[3] = GPoint(rect.origin.x, rect.origin.y + rect.size.h);
  poly_fill(ctx, &p, clip, color);
}

static void ellipse_points(Poly *out, GPoint c, int rx, int ry) {
  // Few points for small ellipses, and no repeated points: the path filler on
  // older firmware (basalt/chalk) hangs on degenerate polygons.
  int n = (rx + ry) * 2;
  if (n < 8) n = 8;
  if (n > 28) n = 28;
  out->n = 0;
  for (int i = 0; i < n; i++) {
    int32_t angle = TRIG_MAX_ANGLE * i / n;
    GPoint p = GPoint(c.x + rx * cos_lookup(angle) / TRIG_MAX_RATIO,
                      c.y + ry * sin_lookup(angle) / TRIG_MAX_RATIO);
    if (out->n > 0 && gpoint_equal(&p, &out->p[out->n - 1])) continue;
    out->p[out->n++] = p;
  }
  if (out->n > 1 && gpoint_equal(&out->p[0], &out->p[out->n - 1])) out->n--;
}

NOINLINE void ellipse_fill(GContext *ctx, GPoint c, int rx, int ry, const Region *clip,
                           GColor fill) {
  static Poly p;
  ellipse_points(&p, c, rx, ry);
  poly_fill(ctx, &p, clip, fill);
}

NOINLINE void ellipse_outline(GContext *ctx, GPoint c, int rx, int ry, GColor stroke,
                              int width) {
  static Poly p;
  ellipse_points(&p, c, rx, ry);
  GPath path = {.num_points = p.n, .points = p.p, .rotation = 0, .offset = GPointZero};
  graphics_context_set_stroke_color(ctx, stroke);
  graphics_context_set_stroke_width(ctx, width);
  gpath_draw_outline(ctx, &path);
}

void line_draw(GContext *ctx, GPoint a, GPoint b, const Region *clip, GColor color, int width) {
  if (clip) {
    // Parametric clip of the segment against every edge.
    int32_t t0 = 0, t1 = 1024;
    for (int i = 0; i < clip->n; i++) {
      int32_t da = side(&clip->h[i], a);
      int32_t db = side(&clip->h[i], b);
      if (da < 0 && db < 0) return;
      if (da < 0) {
        int32_t t = da * 1024 / (da - db);
        if (t > t0) t0 = t;
      } else if (db < 0) {
        int32_t t = da * 1024 / (da - db);
        if (t < t1) t1 = t;
      }
    }
    if (t0 >= t1) return;
    GPoint na = GPoint(a.x + (b.x - a.x) * t0 / 1024, a.y + (b.y - a.y) * t0 / 1024);
    GPoint nb = GPoint(a.x + (b.x - a.x) * t1 / 1024, a.y + (b.y - a.y) * t1 / 1024);
    a = na;
    b = nb;
  }
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  graphics_draw_line(ctx, a, b);
}

NOINLINE void region_fill(GContext *ctx, const Region *r, GColor color) {
  static Poly p;
  p.n = r->n;
  for (int i = 0; i < r->n; i++) p.p[i] = r->corner[i];
  fill_raw(ctx, &p, color);
}

void region_outline(GContext *ctx, const Region *r, GColor color, int width) {
  GPath path = {
      .num_points = r->n, .points = (GPoint *)r->corner, .rotation = 0, .offset = GPointZero};
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  gpath_draw_outline(ctx, &path);
}

/* ---------------------------------------------------------------------------
 * Canvas bounds
 *
 * Older firmware (basalt/chalk) does not clip circles that cross the screen
 * edge: it writes past the framebuffer and corrupts app memory (the custom font
 * cache, in practice, which then hangs the next text draw). Every circle goes
 * through circle_safe(), which drops anything not fully on screen.
 * ------------------------------------------------------------------------- */

static int16_t s_canvas_w = 144, s_canvas_h = 168;

void comic_set_canvas(GSize size) {
  s_canvas_w = size.w;
  s_canvas_h = size.h;
  GPoint c[4] = {GPoint(0, 0), GPoint(size.w - 1, 0), GPoint(size.w - 1, size.h - 1),
                 GPoint(0, size.h - 1)};
  region_init(&s_canvas, c, 4);
}

bool on_canvas(GPoint p, int r) {
  return p.x - r >= 0 && p.y - r >= 0 && p.x + r < s_canvas_w && p.y + r < s_canvas_h;
}

void circle_safe(GContext *ctx, GPoint p, int r) {
  if (!on_canvas(p, r)) return;
  if (r <= 0) {
    graphics_fill_rect(ctx, GRect(p.x, p.y, 1, 1), 0, GCornerNone);
  } else {
    graphics_fill_circle(ctx, p, r);
  }
}

/* ---------------------------------------------------------------------------
 * Print effects
 * ------------------------------------------------------------------------- */

static GRect region_bounds(const Region *r) {
  int16_t x0 = 32767, y0 = 32767, x1 = -32767, y1 = -32767;
  for (int i = 0; i < r->n; i++) {
    GPoint p = r->corner[i];
    if (p.x < x0) x0 = p.x;
    if (p.y < y0) y0 = p.y;
    if (p.x > x1) x1 = p.x;
    if (p.y > y1) y1 = p.y;
  }
  return GRect(x0, y0, x1 - x0, y1 - y0);
}

static int32_t isqrt(int32_t v) {
  if (v <= 0) return 0;
  int32_t x = v, y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + v / x) / 2;
  }
  return x;
}

NOINLINE void halftone(GContext *ctx, const Region *r, int spacing, GPoint focus, int reach,
              int r_min, int r_max, GColor color) {
  GRect b = region_bounds(r);
  int16_t x0 = b.origin.x < 0 ? 0 : b.origin.x;
  int16_t y0 = b.origin.y < 0 ? 0 : b.origin.y;
  int16_t x1 = b.origin.x + b.size.w;
  int16_t y1 = b.origin.y + b.size.h;
  if (x1 > s_canvas_w) x1 = s_canvas_w;
  if (y1 > s_canvas_h) y1 = s_canvas_h;
  graphics_context_set_fill_color(ctx, color);
  int row = 0;
  for (int16_t y = y0 + spacing / 2; y < y1; y += spacing, row++) {
    int16_t start = x0 + ((row & 1) ? spacing / 2 : 0);
    for (int16_t x = start; x < x1; x += spacing) {
      GPoint p = GPoint(x, y);
      if (!region_contains(r, p)) continue;
      int32_t dx = x - focus.x, dy = y - focus.y;
      int32_t d = isqrt(dx * dx + dy * dy);
      if (d > reach) d = reach;
      int rad = r_min + (r_max - r_min) * d / (reach ? reach : 1);
      circle_safe(ctx, p, rad);
    }
  }
}

NOINLINE void rays(GContext *ctx, const Region *r, GPoint center, int count, int rotation_deg,
          GColor color) {
  const int32_t far = 600;
  int32_t step = TRIG_MAX_ANGLE / count;
  int32_t rot = DEG_TO_TRIGANGLE(rotation_deg);
  for (int i = 0; i < count; i++) {
    int32_t a0 = rot + step * i;
    int32_t a1 = a0 + step / 2;
    static Poly p;
    p.n = 3;
    p.p[0] = center;
    p.p[1] = GPoint(center.x + far * cos_lookup(a0) / TRIG_MAX_RATIO,
                    center.y + far * sin_lookup(a0) / TRIG_MAX_RATIO);
    p.p[2] = GPoint(center.x + far * cos_lookup(a1) / TRIG_MAX_RATIO,
                    center.y + far * sin_lookup(a1) / TRIG_MAX_RATIO);
    poly_fill(ctx, &p, r, color);
  }
}

void burst_points(Poly *out, GPoint c, int r_out, int r_in, int spikes, int rotation_deg,
                  int squash_pct) {
  int n = spikes * 2;
  if (n > POLY_MAX) n = POLY_MAX;
  out->n = n;
  int32_t rot = DEG_TO_TRIGANGLE(rotation_deg);
  for (int i = 0; i < n; i++) {
    // Vary the spike length a little so the burst looks hand drawn.
    int rr = (i & 1) ? r_in : r_out - ((i * 7) % 5) * r_out / 18;
    int32_t a = rot + TRIG_MAX_ANGLE * i / n;
    out->p[i] = GPoint(c.x + rr * cos_lookup(a) / TRIG_MAX_RATIO,
                       c.y + rr * squash_pct / 100 * sin_lookup(a) / TRIG_MAX_RATIO);
  }
}

/* ---------------------------------------------------------------------------
 * Lettering
 * ------------------------------------------------------------------------- */

GSize text_size(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 400, 200),
                                               GTextOverflowModeWordWrap, GTextAlignmentLeft);
}

static void text_at(GContext *ctx, const char *text, GFont font, GRect box,
                    GTextAlignment align, int dx, int dy) {
  box.origin.x += dx;
  box.origin.y += dy;
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeWordWrap, align, NULL);
}

NOINLINE void comic_text(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align,
                GColor fill, GColor outline, int outline_px, GPoint shadow) {
  graphics_context_set_text_color(ctx, outline);
  int o = outline_px;
  bool has_shadow = shadow.x != 0 || shadow.y != 0;
  // Hard drop shadow: the outlined glyph shape again, shifted.
  if (has_shadow) {
    for (int dy = -o; dy <= o; dy++) {
      for (int dx = -o; dx <= o; dx++) {
        if (dx * dx + dy * dy > o * o + o) continue;
        text_at(ctx, text, font, box, align, dx + shadow.x, dy + shadow.y);
      }
    }
  }
  for (int dy = -o; dy <= o; dy++) {
    for (int dx = -o; dx <= o; dx++) {
      if ((dx == 0 && dy == 0) || dx * dx + dy * dy > o * o + o) continue;
      text_at(ctx, text, font, box, align, dx, dy);
    }
  }
  graphics_context_set_text_color(ctx, fill);
  text_at(ctx, text, font, box, align, 0, 0);
}
