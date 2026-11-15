/*
 * Hero Signal — a superhero comic watchface.
 *
 * main.c owns the app lifecycle and all the state the face shows (the Scene),
 * fed by the watch's own services (time, battery, health, phone connection)
 * and by the phone (weather + settings over AppMessage, see src/pkjs). The
 * comic page itself is drawn by scene.c.
 *
 * The phone connection is the headline feature: while the Pebble app is
 * reachable, a hero signal shines over the city. When the link drops, the
 * signal cuts out with a "KRZZT!" burst and the watch vibrates once (unless
 * Quiet Time is on or the vibration is switched off in the settings).
 */

#include <pebble.h>
#include "scene.h"

// Weather older than this is shown as unknown rather than pretending.
#define WEATHER_MAX_AGE_S (3 * 60 * 60)
// The watch asks the phone for fresh weather every N minutes and on reconnect.
#define REFRESH_EVERY_MIN 30

enum {
  P_TEMP = 1,
  P_COND,
  P_IS_DAY,
  P_WEATHER_TIME,
  P_FAHRENHEIT,
  P_STEP_GOAL,
  P_VIBE,
  P_HERO,
};

static Window *s_window;
static Layer *s_layer;
static Scene s_scene;
static bool s_vibe_disconnect = true;
static time_t s_weather_time;

/* ---------------------------------------------------------------------------
 * State helpers
 * ------------------------------------------------------------------------- */

static bool daytime_fallback(const struct tm *t) {
  return t->tm_hour >= 7 && t->tm_hour < 19;
}

static void refresh_weather_validity(void) {
  time_t now = time(NULL);
  bool fresh = s_weather_time != 0 && now - s_weather_time < WEATHER_MAX_AGE_S;
  s_scene.has_weather = fresh;
  if (!fresh) {
    s_scene.cond = COND_UNKNOWN;
    s_scene.is_day = daytime_fallback(&s_scene.now);
  }
}

static void update_steps(void) {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);
  HealthServiceAccessibilityMask mask =
      health_service_metric_accessible(HealthMetricStepCount, start, end);
  s_scene.has_steps = (mask & HealthServiceAccessibilityMaskAvailable) != 0;
  s_scene.steps = s_scene.has_steps ? health_service_sum_today(HealthMetricStepCount) : 0;
#else
  s_scene.has_steps = false;
#endif
}

static void load_settings(void) {
  if (persist_exists(P_TEMP)) s_scene.temp_tenths_c = persist_read_int(P_TEMP);
  if (persist_exists(P_COND)) s_scene.cond = persist_read_int(P_COND);
  if (persist_exists(P_IS_DAY)) s_scene.is_day = persist_read_bool(P_IS_DAY);
  if (persist_exists(P_WEATHER_TIME)) s_weather_time = persist_read_int(P_WEATHER_TIME);
  s_scene.fahrenheit = persist_exists(P_FAHRENHEIT) && persist_read_bool(P_FAHRENHEIT);
  s_scene.step_goal = persist_exists(P_STEP_GOAL) ? persist_read_int(P_STEP_GOAL) : 8000;
  s_vibe_disconnect = !persist_exists(P_VIBE) || persist_read_bool(P_VIBE);
  s_scene.hero = persist_exists(P_HERO) ? persist_read_int(P_HERO) : HERO_RED;
  if (s_scene.hero >= HERO_COUNT) s_scene.hero = HERO_RED;
  if (s_scene.cond >= COND_COUNT) s_scene.cond = COND_UNKNOWN;
}

/* ---------------------------------------------------------------------------
 * Phone communication
 * ------------------------------------------------------------------------- */

static void request_refresh(void) {
  if (!connection_service_peek_pebble_app_connection()) return;
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_REFRESH, 1);
  app_message_outbox_send();
}

static int32_t tuple_int(const Tuple *t) {
  switch (t->type) {
    case TUPLE_INT:
      return t->length == 1 ? t->value->int8 : t->length == 2 ? t->value->int16 : t->value->int32;
    case TUPLE_UINT:
      return t->length == 1 ? t->value->uint8
                            : t->length == 2 ? t->value->uint16 : (int32_t)t->value->uint32;
    case TUPLE_CSTRING:
      return atoi(t->value->cstring);
    default:
      return 0;
  }
}

static void inbox_received(DictionaryIterator *it, void *context) {
  Tuple *t;
  bool weather = false;
  if ((t = dict_find(it, MESSAGE_KEY_WEATHER_TEMP))) {
    s_scene.temp_tenths_c = tuple_int(t);
    persist_write_int(P_TEMP, s_scene.temp_tenths_c);
    weather = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_WEATHER_COND))) {
    int32_t c = tuple_int(t);
    s_scene.cond = (c >= 0 && c < COND_COUNT) ? c : COND_UNKNOWN;
    persist_write_int(P_COND, s_scene.cond);
    weather = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_IS_DAY))) {
    s_scene.is_day = tuple_int(t) != 0;
    persist_write_bool(P_IS_DAY, s_scene.is_day);
  }
  if (weather) {
    s_weather_time = time(NULL);
    persist_write_int(P_WEATHER_TIME, s_weather_time);
  }
  if ((t = dict_find(it, MESSAGE_KEY_FAHRENHEIT))) {
    s_scene.fahrenheit = tuple_int(t) != 0;
    persist_write_bool(P_FAHRENHEIT, s_scene.fahrenheit);
  }
  if ((t = dict_find(it, MESSAGE_KEY_STEP_GOAL))) {
    int32_t goal = tuple_int(t);
    if (goal >= 500 && goal <= 100000) {
      s_scene.step_goal = goal;
      persist_write_int(P_STEP_GOAL, goal);
    }
  }
  if ((t = dict_find(it, MESSAGE_KEY_VIBE_DISCONNECT))) {
    s_vibe_disconnect = tuple_int(t) != 0;
    persist_write_bool(P_VIBE, s_vibe_disconnect);
  }
  if ((t = dict_find(it, MESSAGE_KEY_HERO_COLOR))) {
    int32_t h = tuple_int(t);
    s_scene.hero = (h >= 0 && h < HERO_COUNT) ? h : HERO_RED;
    persist_write_int(P_HERO, s_scene.hero);
  }
  refresh_weather_validity();
  layer_mark_dirty(s_layer);
}

/* ---------------------------------------------------------------------------
 * Watch services
 * ------------------------------------------------------------------------- */

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_scene.now = *tick_time;
  refresh_weather_validity();
  layer_mark_dirty(s_layer);
  if (tick_time->tm_min % REFRESH_EVERY_MIN == 0) request_refresh();
}

static void battery_handler(BatteryChargeState state) {
  s_scene.battery = state.charge_percent;
  s_scene.charging = state.is_charging;
  layer_mark_dirty(s_layer);
}

static void connection_handler(bool connected) {
  bool was_connected = s_scene.connected;
  s_scene.connected = connected;
  layer_mark_dirty(s_layer);
  if (was_connected && !connected && s_vibe_disconnect && !quiet_time_is_active()) {
    static const uint32_t pattern[] = {120, 90, 120, 90, 320};
    vibes_enqueue_custom_pattern(
        (VibePattern){.durations = pattern, .num_segments = ARRAY_LENGTH(pattern)});
  }
  if (!was_connected && connected) request_refresh();
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate) {
    update_steps();
    layer_mark_dirty(s_layer);
  }
}
#endif

/* ---------------------------------------------------------------------------
 * Window
 * ------------------------------------------------------------------------- */

static void layer_update(Layer *layer, GContext *ctx) {
  scene_draw(ctx, layer_get_bounds(layer), &s_scene);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, layer_update);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
}

#ifdef HERO_DEMO
// Screenshot mode (see README): a fixed, fully populated scene.
static void apply_demo(void) {
  s_scene.now.tm_hour = 10;
  s_scene.now.tm_min = 9;
  s_scene.h24 = true;
  s_scene.cond = HERO_DEMO_COND;
  s_scene.is_day = !HERO_DEMO_NIGHT;
  s_scene.connected = !HERO_DEMO_DISC;
  s_scene.hero = HERO_DEMO_HERO;
  s_scene.has_weather = true;
  s_scene.temp_tenths_c = 140;
  s_scene.has_steps = true;
  s_scene.steps = 6420;
  s_scene.step_goal = 8000;
  s_scene.battery = 80;
}
#endif

static void init(void) {
  time_t now = time(NULL);
  s_scene.now = *localtime(&now);
  s_scene.h24 = clock_is_24h_style();
  s_scene.german = strncmp(i18n_get_system_locale(), "de", 2) == 0;
  load_settings();
  refresh_weather_validity();
  update_steps();
  BatteryChargeState battery = battery_state_service_peek();
  s_scene.battery = battery.charge_percent;
  s_scene.charging = battery.is_charging;
  s_scene.connected = connection_service_peek_pebble_app_connection();

  scene_init();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = window_load,
                                           .unload = window_unload,
                                       });
  window_stack_push(s_window, true);

#ifdef HERO_DEMO
  apply_demo();
  return;
#endif

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers){
      .pebble_app_connection_handler = connection_handler,
  });
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif
  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
  scene_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
