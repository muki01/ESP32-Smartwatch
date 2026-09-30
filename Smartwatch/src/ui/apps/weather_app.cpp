/*
 * weather_app.cpp - Weather UI: the Weather tile next to the watch face, the Weather app
 * (details, the next hours, 5-day forecast, unit) and the location page (automatic or a
 * searched city). Data: services/weather.cpp; conditions use the WMO weather codes.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../services/clock.h"
#include "../../services/weather.h"
#include "../faces/faces.h"
#include "../kit/kit.h"

static lv_obj_t *wui_tile_body;
static lv_obj_t *wui_app_page;  // the open Weather app page (target when a city was chosen)

static void wui_format_updated(time_t t, char *buf, size_t len) {
  long age = (long)(time(NULL) - t);
  if (!t || age < 0) strlcpy(buf, "", len);
  else if (age < 120) strlcpy(buf, "Updated just now", len);
  else if (age < 3600) snprintf(buf, len, "Updated %ld min ago", age / 60);
  else if (age < 86400) snprintf(buf, len, "Updated %ld h ago", age / 3600);
  else strlcpy(buf, "Updated over a day ago", len);
}

/* ================================ Shared parts ==================================== */

// Big icon next to the temperature, then the condition and today's range.
static void wui_hero(lv_obj_t *parent, const WeatherInfo *w) {
  lv_obj_t *col = kit_col_box(parent, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_width(col, LV_PCT(100));
  lv_obj_t *top = kit_container(col);
  lv_obj_set_size(top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, 14, 0);
  kit_label(top, &font_icons_96, weather_color(w->code, w->is_day), weather_icon(w->code, w->is_day));
  lv_obj_t *temp = kit_label(top, &font_num_110, 0xFFFFFF, "");
  lv_label_set_text_fmt(temp, "%d\xC2\xB0", weather_temp(w->temp));
  lv_obj_t *text = kit_label(col, KIT_FONT_TEXT, 0xFFFFFF, weather_text(w->code));
  lv_obj_set_style_margin_top(text, 2, 0);
  lv_obj_t *range = kit_label(col, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  lv_label_set_text_fmt(range, "H %d\xC2\xB0  \xE2\x80\xA2  L %d\xC2\xB0", weather_temp(w->days[0].t_max), weather_temp(w->days[0].t_min));
}

// What to show while there is no weather yet.
static const char *wui_missing_text() {
  if (weather_state() == WEATHER_LOADING) return "Getting the weather...";
  if (lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED) return "Connect to Wi-Fi to see the weather";
  return weather_error()[0] ? weather_error() : "No weather yet";
}

/* ================================ Tile ============================================ */

static const char *const WUI_DAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static void wui_tile_fill() {
  lv_obj_t *b = wui_tile_body;
  lv_obj_clean(b);
  const WeatherInfo *w = weather_info();

  lv_obj_t *place = kit_container(b);
  lv_obj_set_size(place, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(place, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(place, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(place, 8, 0);
  // The place, or just "Weather" while an automatic location is not known yet.
  bool known = (w->valid && w->city[0]) || !weather_place_is_auto();
  lv_obj_t *pin = kit_label(place, &font_icons_24, KIT_COLOR_ACCENT, weather_place_is_auto() ? ICON_LOCATION : ICON_MAP_MARKER);
  lv_obj_set_hidden(pin, !known);
  lv_obj_t *city = kit_label(place, KIT_FONT_BODY, 0xFFFFFF, !known ? "Weather" : w->valid && w->city[0] ? w->city : weather_place_name());
  lv_obj_set_style_max_width(city, 260, 0);
  lv_label_set_long_mode(city, LV_LABEL_LONG_MODE_DOTS);

  if (!w->valid) {
    lv_obj_t *icon = kit_label(b, &font_icons_96, 0x3A3A3C, ICON_CLOUD_SUN);
    lv_obj_set_style_margin_top(icon, 40, 0);
    lv_obj_t *text = kit_label(b, KIT_FONT_BODY, KIT_COLOR_TEXT2, wui_missing_text());
    lv_obj_set_width(text, LV_PCT(84));
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
    return;
  }
  wui_hero(b, w);

  // The next four days in a card.
  lv_obj_t *card = kit_container(b);
  lv_obj_set_size(card, LV_PCT(92), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, lv_color_hex(KIT_COLOR_CARD), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 26, 0);
  lv_obj_set_style_pad_ver(card, 12, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_margin_top(card, 14, 0);
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  for (int i = 1; i < WEATHER_DAYS; i++) {
    const WeatherDay &d = w->days[i];
    if (d.code < 0) continue;
    lv_obj_t *col = kit_col_box(card, LV_FLEX_ALIGN_CENTER, 4);
    kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, WUI_DAYS[(t.tm_wday + i) % 7]);
    kit_label(col, &font_icons_32, weather_color(d.code, true), weather_icon(d.code, true));
    lv_obj_t *hi = kit_label(col, KIT_FONT_BODY, 0xFFFFFF, "");
    lv_label_set_text_fmt(hi, "%d\xC2\xB0", weather_temp(d.t_max));
    lv_obj_t *lo = kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
    lv_label_set_text_fmt(lo, "%d\xC2\xB0", weather_temp(d.t_min));
  }
}

static void wui_tile_async(void *user_data) {
  if (wui_tile_body) wui_tile_fill();
}

static void wui_tile_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_async_call_cancel(wui_tile_async, NULL);
  lv_async_call(wui_tile_async, NULL);
}

static void wui_tile_click_cb(lv_event_t *e) {
  audio_click();
  weather_open();
}

lv_obj_t *weather_tile_create() {
  lv_obj_t *tile = face_screen_create();
  lv_obj_add_event_cb(tile, wui_tile_click_cb, LV_EVENT_SHORT_CLICKED, NULL);
  wui_tile_body = kit_col_box(tile, LV_FLEX_ALIGN_CENTER, 6);
  lv_obj_set_width(wui_tile_body, LV_PCT(100));
  lv_obj_align(wui_tile_body, LV_ALIGN_TOP_MID, 0, 16);
  lv_subject_add_observer_obj(subj_weather, wui_tile_obs, tile, NULL);
  lv_subject_add_observer_obj(subj_temp_unit, wui_tile_obs, tile, NULL);
  lv_subject_add_observer_obj(subj_wifi_state, wui_tile_obs, tile, NULL);
  wui_tile_fill();
  return tile;
}

/* ================================ Location ======================================== */

static void wui_result_clicked_cb(lv_event_t *e) {
  const WeatherPlace *p = weather_search_result((int)(intptr_t)lv_event_get_user_data(e));
  if (!p) return;
  audio_click();
  weather_set_place(p);
  if (wui_app_page && kit_page_is_open(wui_app_page)) kit_page_pop_to(wui_app_page);
  else kit_page_pop();
}

static void wui_results_fill(lv_obj_t *c) {
  lv_obj_clean(c);
  switch (weather_search_state()) {
    case WSEARCH_BUSY:
      kit_empty_state(c, ICON_SEARCH, "Searching...");
      return;
    case WSEARCH_FAILED:
      kit_empty_state(c, ICON_WARNING, weather_error()[0] ? weather_error() : "Search failed");
      return;
    default:
      break;
  }
  if (!weather_search_count()) {
    kit_empty_state(c, ICON_SEARCH, "No city with that name");
    return;
  }
  for (int i = 0; i < weather_search_count(); i++) {
    const WeatherPlace *p = weather_search_result(i);
    lv_obj_t *row = kit_row(c, ICON_MAP_MARKER, KIT_COLOR_BLUE, p->name, p->region, false);
    lv_obj_add_event_cb(row, wui_result_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
}

static void wui_results_async(void *content) {
  wui_results_fill((lv_obj_t *)content);
}

static void wui_results_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *c = lv_observer_get_target_obj(observer);
  lv_async_call_cancel(wui_results_async, c);
  lv_async_call(wui_results_async, c);
}

static void wui_results_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(wui_results_async, lv_event_get_current_target_obj(e));
}

static void wui_search_done(void *text) {
  if (!weather_search((const char *)text)) return;
  lv_obj_t *page = kit_page_create("Results", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_add_event_cb(c, wui_results_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_subject_add_observer_obj(subj_weather, wui_results_obs, c, NULL);
  kit_page_push(page);
}

static void wui_search_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(kit_text_input_page("Search city", "City name", false, 40, wui_search_done));
}

static void wui_auto_cb(lv_event_t *e) {
  audio_click();
  weather_set_place(NULL);
  kit_page_pop();
}

static lv_obj_t *wui_build_location() {
  lv_obj_t *page = kit_page_create("Location", true);
  lv_obj_t *c = kit_page_content(page);
  bool automatic = weather_place_is_auto();
  lv_obj_t *autorow = kit_row(c, ICON_LOCATION, KIT_COLOR_BLUE, "Automatic", "Detected online", false);
  if (automatic) kit_label(autorow, &font_icons_24, KIT_COLOR_ACCENT, ICON_CHECK);
  lv_obj_add_event_cb(autorow, wui_auto_cb, LV_EVENT_CLICKED, NULL);
  if (!automatic) {
    lv_obj_t *cur = kit_row(c, ICON_MAP_MARKER, KIT_COLOR_GREEN, weather_place_name(), "Chosen city", false);
    kit_label(cur, &font_icons_24, KIT_COLOR_ACCENT, ICON_CHECK);
  }
  lv_obj_t *search = kit_row(c, ICON_SEARCH, KIT_COLOR_GRAY, "Search city", NULL, true);
  lv_obj_add_event_cb(search, wui_search_cb, LV_EVENT_CLICKED, NULL);
  kit_note(c, "Automatic location uses your internet connection and can be off by a few kilometres. "
              "Choose a city for exact results.");
  return page;
}

/* ================================ Weather app ===================================== */

static const char *const WUI_UNIT_LABELS[] = { "Celsius (\xC2\xB0" "C)", "Fahrenheit (\xC2\xB0" "F)" };
static const KitChoice WUI_UNIT_CHOICE = { "Temperature", &subj_temp_unit, NULL, WUI_UNIT_LABELS, 2 };

static void wui_refresh_cb(lv_event_t *e) {
  audio_click();
  if (lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED) {
    kit_toast("Wi-Fi is not connected");
    return;
  }
  weather_refresh();
}

static void wui_unit_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(kit_choice_page(&WUI_UNIT_CHOICE));
}

static void wui_location_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(wui_build_location());
}

static void wui_unit_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), WUI_UNIT_LABELS[lv_subject_get_int(subject) ? 1 : 0]);
}

static void wui_detail(lv_obj_t *parent, const char *icon, uint32_t color, const char *value, const char *label) {
  lv_obj_t *card = kit_info_card(parent);
  lv_obj_set_width(card, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(card, 6, 0);
  lv_obj_set_style_pad_row(card, 4, 0);
  kit_label(card, &font_icons_24, color, icon);
  kit_label(card, KIT_FONT_BODY, 0xFFFFFF, value);
  kit_label(card, KIT_FONT_SMALL, KIT_COLOR_TEXT2, label);
}

static void wui_settings_rows(lv_obj_t *c) {
  lv_obj_t *loc = kit_row(c, ICON_LOCATION, KIT_COLOR_BLUE, "Location",
                          weather_place_is_auto() ? "Automatic" : weather_place_name(), true);
  lv_obj_add_event_cb(loc, wui_location_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *refresh = kit_row(c, ICON_SYNC, KIT_COLOR_TEAL, "Refresh", NULL, false);
  lv_obj_add_event_cb(refresh, wui_refresh_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *unit = kit_row(c, ICON_THERMOMETER, KIT_COLOR_ORANGE, "Unit", "", true);
  lv_subject_add_observer_obj(subj_temp_unit, wui_unit_obs, kit_row_subtitle(unit), NULL);
  lv_obj_add_event_cb(unit, wui_unit_cb, LV_EVENT_CLICKED, NULL);
}

static void wui_fill(lv_obj_t *c) {
  lv_obj_clean(c);
  const WeatherInfo *w = weather_info();
  char buf[64];

  if (!w->valid) {
    kit_empty_state(c, weather_state() == WEATHER_LOADING ? ICON_SYNC : ICON_CLOUD, wui_missing_text());
    wui_settings_rows(c);
    return;
  }

  wui_hero(c, w);
  char updated[40];
  wui_format_updated(w->updated, updated, sizeof(updated));
  lv_obj_t *place = kit_label(c, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
  lv_obj_set_width(place, LV_PCT(100));
  lv_obj_set_style_text_align(place, LV_TEXT_ALIGN_CENTER, 0);
  if (weather_state() == WEATHER_LOADING) lv_label_set_text(place, "Updating...");
  else if (w->city[0] && updated[0]) lv_label_set_text_fmt(place, "%s  \xE2\x80\xA2  %s", w->city, updated);
  else lv_label_set_text(place, w->city[0] ? w->city : updated);
  lv_obj_set_style_margin_bottom(place, 6, 0);

  lv_obj_t *details = kit_row_box(c, LV_FLEX_ALIGN_SPACE_BETWEEN, 8);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", weather_temp(w->feels));
  wui_detail(details, ICON_THERMOMETER, KIT_COLOR_ORANGE, buf, "Feels like");
  snprintf(buf, sizeof(buf), "%u%%", (unsigned)w->humidity);
  wui_detail(details, ICON_TINT, KIT_COLOR_CYAN, buf, "Humidity");
  snprintf(buf, sizeof(buf), "%d km/h", (int)lroundf(w->wind));
  wui_detail(details, ICON_WIND, 0xAEAEB2, buf, "Wind");

  // Sun and UV.
  lv_obj_t *sun = kit_row_box(c, LV_FLEX_ALIGN_SPACE_BETWEEN, 8);
  lv_obj_set_style_margin_top(sun, 2, 0);
  if (w->sunrise >= 0) clock_format_hm(w->sunrise / 60, w->sunrise % 60, buf, sizeof(buf));
  else strlcpy(buf, "--", sizeof(buf));
  wui_detail(sun, ICON_SUN, KIT_COLOR_ORANGE, buf, "Sunrise");
  if (w->sunset >= 0) clock_format_hm(w->sunset / 60, w->sunset % 60, buf, sizeof(buf));
  else strlcpy(buf, "--", sizeof(buf));
  wui_detail(sun, ICON_MOON, 0xAEAEB2, buf, "Sunset");
  const char *uv_level = w->uv < 0 ? "UV index" : w->uv < 3 ? "Low" : w->uv < 6 ? "Moderate" : w->uv < 8 ? "High" : w->uv < 11 ? "Very high" : "Extreme";
  if (w->uv >= 0) snprintf(buf, sizeof(buf), "UV %d", (int)lroundf(w->uv));
  else strlcpy(buf, "UV --", sizeof(buf));
  wui_detail(sun, ICON_SUN, KIT_COLOR_YELLOW, buf, uv_level);

  // The next hours, every second one.
  if (w->hours[0].hour >= 0) {
    lv_obj_t *hsec = kit_section(c, "NEXT HOURS");
    lv_obj_set_width(hsec, LV_PCT(100));
    lv_obj_t *hcard = kit_info_card(c);
    lv_obj_t *hrow = kit_row_box(hcard, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    bool h24 = lv_subject_get_int(subj_time_24h);
    for (int i = 0; i < WEATHER_HOURS; i += 2) {
      const WeatherHour &h = w->hours[i];
      if (h.hour < 0) continue;
      lv_obj_t *col = kit_col_box(hrow, LV_FLEX_ALIGN_CENTER, 4);
      if (i == 0) snprintf(buf, sizeof(buf), "Now");
      else if (h24) snprintf(buf, sizeof(buf), "%02d", h.hour);
      else snprintf(buf, sizeof(buf), "%d%s", h.hour % 12 ? h.hour % 12 : 12, h.hour < 12 ? "a" : "p");
      kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, buf);
      kit_label(col, &font_icons_24, weather_color(h.code, h.is_day), weather_icon(h.code, h.is_day));
      snprintf(buf, sizeof(buf), "%d\xC2\xB0", weather_temp(h.temp));
      kit_label(col, KIT_FONT_BODY, 0xFFFFFF, buf);
      lv_obj_t *rain = kit_label(col, KIT_FONT_SMALL, KIT_COLOR_CYAN, "");
      if (h.precip >= 10) lv_label_set_text_fmt(rain, "%u%%", (unsigned)h.precip);
    }
  }

  lv_obj_t *section = kit_section(c, "FORECAST");
  lv_obj_set_width(section, LV_PCT(100));
  lv_obj_t *card = kit_info_card(c);
  lv_obj_set_style_pad_row(card, 14, 0);
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  for (int i = 0; i < WEATHER_DAYS; i++) {
    const WeatherDay &d = w->days[i];
    if (d.code < 0) continue;
    lv_obj_t *row = kit_row_box(card, LV_FLEX_ALIGN_START, 10);
    lv_obj_t *day = kit_label(row, KIT_FONT_BODY, 0xFFFFFF, i == 0 ? "Today" : WUI_DAYS[(t.tm_wday + i) % 7]);
    lv_obj_set_width(day, 82);
    lv_obj_t *ic = kit_label(row, &font_icons_24, weather_color(d.code, true), weather_icon(d.code, true));
    lv_obj_set_width(ic, 34);
    lv_obj_t *rain = kit_label(row, KIT_FONT_SMALL, KIT_COLOR_CYAN, "");
    lv_obj_set_flex_grow(rain, 1);
    if (d.precip >= 10) lv_label_set_text_fmt(rain, "%u%%", (unsigned)d.precip);
    lv_obj_t *temps = kit_label(row, KIT_FONT_BODY, 0xFFFFFF, "");
    lv_label_set_text_fmt(temps, "%d\xC2\xB0 #8E8E93 %d\xC2\xB0#", weather_temp(d.t_max), weather_temp(d.t_min));
    lv_label_set_recolor(temps, true);
  }

  wui_settings_rows(c);
  kit_note(c, "Weather data from Open-Meteo.");
}

static void wui_rebuild(void *content) {
  wui_fill((lv_obj_t *)content);
}

// Rebuilt after every change of the weather data (not for the initial notification).
static void wui_page_obs(lv_observer_t *observer, lv_subject_t *subject) {
  int32_t version = lv_subject_get_int(subject);
  if ((int32_t)(intptr_t)lv_observer_get_user_data(observer) == version) return;
  lv_observer_set_user_data(observer, (void *)(intptr_t)version);
  lv_obj_t *content = lv_observer_get_target_obj(observer);
  lv_async_call_cancel(wui_rebuild, content);
  lv_async_call(wui_rebuild, content);
}

static void wui_content_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(wui_rebuild, lv_event_get_current_target_obj(e));
}

static void wui_page_deleted_cb(lv_event_t *e) {
  if (lv_event_get_current_target_obj(e) == wui_app_page) wui_app_page = NULL;
}

void weather_open() {
  lv_obj_t *page = kit_page_create("Weather", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  wui_fill(c);
  lv_obj_add_event_cb(c, wui_content_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_obj_add_event_cb(page, wui_page_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_subject_add_observer_obj(subj_weather, wui_page_obs, c, (void *)(intptr_t)lv_subject_get_int(subj_weather));
  wui_app_page = page;
  kit_page_push(page);
}
