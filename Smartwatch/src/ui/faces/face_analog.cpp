/*
 * face_analog.cpp - A classic dial over the texture: minute track, hour numerals, the date
 * window at 3, weather at 6. The hands are those of the original design: an accent hour
 * hand with a slot, a hollow white minute hand and a thin grey second hand that ticks
 * while the face is on screen. Every hand line is sized to its own bounding box, so a
 * tick only redraws the area the second hand sweeps.
 */
#include "faces.h"

#include <Arduino.h>
#include "../../core/format.h"
#include "../../core/settings.h"
#include "../../services/weather.h"
#include "../apps/apps.h"
#include "../kit/kit.h"
#include "../system/nav.h"
#include "../system/screen.h"

#define FA_DIAL        340
#define FA_CENTER      (FA_DIAL / 2)

enum FaceHandId { FA_HOUR, FA_HOUR_SLOT, FA_MINUTE, FA_MINUTE_SLOT, FA_SECOND, FA_LINES };

struct FaceHandLine {
  lv_obj_t *line;
  lv_point_precise_t pts[2];
  int32_t width;
  int32_t from, to;  // distance from the centre, negative = tail
};

static lv_obj_t *fa_screen, *fa_dial, *fa_date, *fa_weather, *fa_steps;
static FaceHandLine fa_lines[FA_LINES];
static int fa_last_sec = -1;

// Angle in 0.1 degrees clockwise from 12 o'clock.
static void fa_line_set(int id, int32_t angle10) {
  FaceHandLine *h = &fa_lines[id];
  float a = angle10 * 3.14159265f / 1800.0f;
  float sx = sinf(a), cy = -cosf(a);
  int32_t x0 = FA_CENTER + (int32_t)lroundf(sx * h->from), y0 = FA_CENTER + (int32_t)lroundf(cy * h->from);
  int32_t x1 = FA_CENTER + (int32_t)lroundf(sx * h->to), y1 = FA_CENTER + (int32_t)lroundf(cy * h->to);
  int32_t pad = h->width / 2 + 2;
  int32_t bx = min(x0, x1) - pad, by = min(y0, y1) - pad;
  h->pts[0].x = x0 - bx;
  h->pts[0].y = y0 - by;
  h->pts[1].x = x1 - bx;
  h->pts[1].y = y1 - by;
  lv_obj_set_pos(h->line, bx, by);
  lv_obj_set_size(h->line, abs(x1 - x0) + 2 * pad, abs(y1 - y0) + 2 * pad);
  lv_line_set_points(h->line, h->pts, 2);
}

static lv_obj_t *fa_line_create(int id, int32_t width, int32_t from, int32_t to, uint32_t color) {
  FaceHandLine *h = &fa_lines[id];
  h->width = width;
  h->from = from;
  h->to = to;
  h->line = lv_line_create(fa_dial);
  lv_obj_remove_style_all(h->line);
  lv_obj_set_style_line_width(h->line, width, 0);
  lv_obj_set_style_line_color(h->line, lv_color_hex(color), 0);
  lv_obj_set_style_line_rounded(h->line, true, 0);
  lv_obj_set_clickable(h->line, false);
  return h->line;
}

static void fa_update() {
  struct tm t;
  face_now(&t);
  int32_t hour = (t.tm_hour % 12) * 300 + t.tm_min * 5, minute = t.tm_min * 60 + t.tm_sec;
  fa_line_set(FA_HOUR, hour);
  fa_line_set(FA_HOUR_SLOT, hour);
  fa_line_set(FA_MINUTE, minute);
  fa_line_set(FA_MINUTE_SLOT, minute);
  fa_line_set(FA_SECOND, t.tm_sec * 60);
  lv_label_set_text_fmt(fa_date, "%s %d", FACE_WEEKDAYS_SHORT[t.tm_wday], t.tm_mday);
  fa_last_sec = t.tm_sec;
}

// Ticks only while the face can be seen.
static void fa_tick_cb(lv_timer_t *timer) {
  if (screen_is_off() || lv_screen_active() != fa_screen) return;
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_sec != fa_last_sec) fa_update();
}

static void fa_minute_obs(lv_observer_t *observer, lv_subject_t *subject) {
  fa_update();
}

static void fa_loaded_cb(lv_event_t *e) {
  fa_update();
}

static void fa_weather_obs(lv_observer_t *observer, lv_subject_t *subject) {
  const WeatherInfo *w = weather_info();
  if (w->valid) lv_label_set_text_fmt(fa_weather, "%s  %d\xC2\xB0", weather_icon(w->code, w->is_day), weather_temp(w->temp));
  else lv_label_set_text(fa_weather, "");
}

static void fa_steps_obs(lv_observer_t *observer, lv_subject_t *subject) {
  char buf[16];
  format_thousands(lv_subject_get_int(subj_steps), buf, sizeof(buf));
  lv_label_set_text_fmt(fa_steps, "%s  %s", ICON_STEPS, buf);
}

lv_obj_t *face_analog_create() {
  static const char *NUMERALS[] = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "", NULL };
  fa_screen = face_screen_create();
  face_status_create(fa_screen, true);

  fa_dial = kit_container(fa_screen);
  lv_obj_set_size(fa_dial, FA_DIAL, FA_DIAL);
  lv_obj_align(fa_dial, LV_ALIGN_CENTER, 0, 4);

  lv_obj_t *scale = lv_scale_create(fa_dial);
  lv_obj_remove_style_all(scale);
  lv_obj_set_size(scale, FA_DIAL, FA_DIAL);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_range(scale, 0, 60);
  lv_scale_set_total_tick_count(scale, 61);
  lv_scale_set_major_tick_every(scale, 5);
  lv_scale_set_angle_range(scale, 360);
  lv_scale_set_rotation(scale, 270);
  lv_scale_set_label_show(scale, true);
  lv_scale_set_text_src(scale, NUMERALS);
  lv_obj_set_clickable(scale, false);
  lv_obj_set_style_length(scale, 16, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(scale, 4, LV_PART_INDICATOR);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_length(scale, 8, LV_PART_ITEMS);
  lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_hex(0x6A6A6E), LV_PART_ITEMS);
  lv_obj_set_style_text_font(scale, KIT_FONT_TEXT, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(scale, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_pad_radial(scale, 8, LV_PART_INDICATOR);  // numerals clear of the hour marks
  lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);

  lv_obj_t *brand = face_accent_label(fa_dial, KIT_FONT_SMALL, "MUKI");
  lv_obj_set_style_text_letter_space(brand, 4, 0);
  lv_obj_align(brand, LV_ALIGN_CENTER, 0, -72);

  // Date window at 3 o'clock.
  fa_date = kit_label(fa_dial, KIT_FONT_SMALL, 0xFFFFFF, "");
  lv_obj_set_style_bg_color(fa_date, lv_color_hex(0x1A1A1C), 0);
  lv_obj_set_style_bg_opa(fa_date, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(fa_date, 1, 0);
  lv_obj_set_style_border_color(fa_date, lv_color_hex(0x48484A), 0);
  lv_obj_set_style_radius(fa_date, 8, 0);
  lv_obj_set_style_pad_hor(fa_date, 8, 0);
  lv_obj_set_style_pad_ver(fa_date, 3, 0);
  lv_obj_align(fa_date, LV_ALIGN_CENTER, 70, 0);

  fa_weather = kit_label(fa_dial, &font_icons_24, 0xFFFFFF, "");
  lv_obj_align(fa_weather, LV_ALIGN_CENTER, 0, 70);
  nav_make_complication(fa_weather, weather_open);

  // Hands: hour (accent, with a slot), minute (hollow white), second (thin grey).
  face_accent_line(fa_line_create(FA_HOUR, 14, -10, 92, 0xFFFFFF));
  fa_line_create(FA_HOUR_SLOT, 5, 40, 82, 0x0A0A0A);
  fa_line_create(FA_MINUTE, 12, -10, 138, 0xFFFFFF);
  fa_line_create(FA_MINUTE_SLOT, 5, 28, 130, 0x0A0A0A);
  fa_line_create(FA_SECOND, 3, -30, 150, 0xB4B4B8);
  lv_obj_t *hub = kit_container(fa_dial);
  lv_obj_set_size(hub, 22, 22);
  lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(hub, lv_color_hex(0xB4B4B8), 0);
  lv_obj_center(hub);
  lv_obj_t *pin = kit_container(fa_dial);
  lv_obj_set_size(pin, 8, 8);
  lv_obj_set_style_radius(pin, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pin, lv_color_hex(0x2A2A2C), 0);
  lv_obj_center(pin);

  fa_steps = kit_label(fa_screen, &font_icons_24, KIT_COLOR_TEXT2, "");
  lv_obj_align(fa_steps, LV_ALIGN_BOTTOM_MID, 0, -12);
  nav_make_complication(fa_steps, activity_open);

  lv_subject_add_observer_obj(subj_time_text, fa_minute_obs, fa_screen, NULL);
  lv_subject_add_observer_obj(subj_weather, fa_weather_obs, fa_screen, NULL);
  lv_subject_add_observer_obj(subj_temp_unit, fa_weather_obs, fa_screen, NULL);
  lv_subject_add_observer_obj(subj_steps, fa_steps_obs, fa_screen, NULL);
  lv_obj_add_event_cb(fa_screen, fa_loaded_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
  kit_page_timer(fa_screen, fa_tick_cb, 200, NULL);
  return fa_screen;
}
