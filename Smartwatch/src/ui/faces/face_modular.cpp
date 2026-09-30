/*
 * face_modular.cpp - "Modular" watch face: a large thin clock with three ring
 * complications (steps in the accent colour, battery, weather) and what comes next: the
 * next calendar event within 12 hours, otherwise the next alarm.
 */
#include "faces.h"

#include <Arduino.h>
#include "../../core/format.h"
#include "../../core/settings.h"
#include "../../services/agenda.h"
#include "../../services/clock.h"
#include "../../services/weather.h"
#include "../apps/apps.h"
#include "../kit/kit.h"
#include "../system/nav.h"

#define FM_RING_SIZE   104
#define FM_RING_WIDTH  8

static lv_obj_t *fm_screen;
static lv_obj_t *fm_weekday;
static lv_obj_t *fm_day;
static lv_obj_t *fm_time;
static lv_obj_t *fm_ampm;
static lv_obj_t *fm_steps_ring, *fm_steps_value;
static lv_obj_t *fm_battery_ring, *fm_battery_icon, *fm_battery_value;
static lv_obj_t *fm_weather_icon, *fm_weather_value;
static lv_obj_t *fm_next_row, *fm_next_icon, *fm_next_label;

static const char *const FM_WEEKDAYS[] = { "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };

static void fm_time_obs(lv_observer_t *observer, lv_subject_t *subject) {
  struct tm t;
  face_now(&t);
  if (lv_subject_get_int(subj_time_24h)) {
    lv_label_set_text_fmt(fm_time, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_obj_set_hidden(fm_ampm, true);
  } else {
    lv_label_set_text_fmt(fm_time, "%d:%02d", face_hour12(t.tm_hour), t.tm_min);
    lv_label_set_text(fm_ampm, t.tm_hour < 12 ? "AM" : "PM");
    lv_obj_set_hidden(fm_ampm, false);
  }
  lv_label_set_text(fm_weekday, FM_WEEKDAYS[t.tm_wday]);
  lv_label_set_text_fmt(fm_day, "%d %s", t.tm_mday, FACE_MONTHS_SHORT[t.tm_mon]);
}

static void fm_steps_obs(lv_observer_t *observer, lv_subject_t *subject) {
  int32_t steps = lv_subject_get_int(subj_steps);
  int32_t goal = lv_subject_get_int(subj_step_goal);
  lv_arc_set_value(fm_steps_ring, goal > 0 ? min(100, (int)(steps * 100 / goal)) : 0);
  if (steps >= 10000) {
    lv_label_set_text_fmt(fm_steps_value, "%d.%dk", (int)(steps / 1000), (int)(steps % 1000 / 100));
  } else {
    char buf[16];
    format_thousands(steps, buf, sizeof(buf));
    lv_label_set_text(fm_steps_value, buf);
  }
}

static void fm_battery_obs(lv_observer_t *observer, lv_subject_t *subject) {
  int32_t level = lv_subject_get_int(subj_battery);
  bool charging = lv_subject_get_int(subj_charging);
  uint32_t color = charging ? KIT_COLOR_GREEN : level < 0 ? KIT_COLOR_GRAY : level <= 15 ? KIT_COLOR_RED
                                                                           : level <= 30 ? KIT_COLOR_ORANGE
                                                                                         : KIT_COLOR_GREEN;
  lv_arc_set_value(fm_battery_ring, level < 0 ? 0 : level);
  lv_obj_set_style_arc_color(fm_battery_ring, lv_color_hex(color), LV_PART_INDICATOR);
  lv_label_set_text(fm_battery_icon, charging ? ICON_BOLT : level > 60 ? ICON_BATTERY_3 : level > 25 ? ICON_BATTERY_2 : ICON_BATTERY_1);
  lv_obj_set_style_text_color(fm_battery_icon, lv_color_hex(color), 0);
  if (level < 0) lv_label_set_text(fm_battery_value, "USB");
  else lv_label_set_text_fmt(fm_battery_value, "%d%%", (int)level);
}

static void fm_weather_obs(lv_observer_t *observer, lv_subject_t *subject) {
  face_weather_value(fm_weather_icon, fm_weather_value);
}

// Next calendar event within 12 hours, otherwise the next alarm.
static void fm_next_obs(lv_observer_t *observer, lv_subject_t *subject) {
  char buf[16];
  const AgendaEvent *e = agenda_count() ? agenda_get(0) : NULL;
  time_t now = time(NULL);
  if (e && (time_t)e->start - now < 12 * 3600) {
    time_t start = e->start;
    struct tm ts;
    localtime_r(&start, &ts);
    if (e->all_day || (time_t)e->start <= now) strlcpy(buf, e->all_day ? "Today" : "Now", sizeof(buf));
    else clock_format_hm(ts.tm_hour, ts.tm_min, buf, sizeof(buf));
    lv_label_set_text(fm_next_icon, ICON_CALENDAR);
    lv_obj_set_style_text_color(fm_next_icon, lv_color_hex(KIT_COLOR_RED), 0);
    lv_label_set_text_fmt(fm_next_label, "%s  %s", buf, e->title);
    lv_obj_set_hidden(fm_next_row, false);
    return;
  }
  int32_t next = lv_subject_get_int(subj_alarm_next);
  lv_obj_set_hidden(fm_next_row, next < 0);
  if (next < 0) return;
  clock_format_hm(next / 60, next % 60, buf, sizeof(buf));
  lv_label_set_text(fm_next_icon, ICON_CLOCK);
  lv_obj_set_style_text_color(fm_next_icon, lv_color_hex(KIT_COLOR_ORANGE), 0);
  lv_label_set_text(fm_next_label, buf);
}

// One ring complication: arc + icon + value; tapping it opens `open` (may be NULL).
static lv_obj_t *fm_complication(lv_obj_t *parent, uint32_t color, const char *icon, lv_obj_t **icon_out,
                                 lv_obj_t **value_out, app_open_fn_t open) {
  lv_obj_t *ring = kit_ring(parent, FM_RING_SIZE, FM_RING_WIDTH, color);
  lv_obj_t *col = kit_col_box(ring, LV_FLEX_ALIGN_CENTER, 2);
  lv_obj_center(col);
  lv_obj_t *ic = kit_label(col, &font_icons_24, color, icon);
  lv_obj_t *value = kit_label(col, KIT_FONT_BODY, 0xFFFFFF, "");
  if (icon_out) *icon_out = ic;
  *value_out = value;
  if (open) nav_make_complication(ring, open);
  return ring;
}

lv_obj_t *face_modular_create() {
  if (fm_screen) return fm_screen;
  fm_screen = face_screen_create();

  // Date line: weekday in the accent colour, then day and month.
  lv_obj_t *date = kit_container(fm_screen);
  lv_obj_set_size(date, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(date, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(date, 10, 0);
  lv_obj_align(date, LV_ALIGN_TOP_MID, 0, 50);
  fm_weekday = face_accent_label(date, KIT_FONT_BODY, "");
  fm_day = kit_label(date, KIT_FONT_BODY, 0xFFFFFF, "");
  lv_obj_set_style_text_letter_space(fm_weekday, 2, 0);
  lv_obj_set_style_text_letter_space(fm_day, 2, 0);

  lv_obj_t *time_row = kit_container(fm_screen);
  lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(time_row, 6, 0);
  lv_obj_align(time_row, LV_ALIGN_TOP_MID, 0, 84);
  fm_time = kit_label(time_row, &font_num_110, 0xFFFFFF, "--:--");
  fm_ampm = kit_label(time_row, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  lv_obj_set_style_pad_bottom(fm_ampm, 20, 0);

  lv_obj_t *row = kit_container(fm_screen);
  lv_obj_set_size(row, LV_PCT(100), FM_RING_SIZE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(row, 12, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 214);
  lv_obj_t *steps_icon;
  fm_steps_ring = fm_complication(row, KIT_COLOR_GREEN, ICON_STEPS, &steps_icon, &fm_steps_value, activity_open);
  face_accent_arc(fm_steps_ring);
  face_accent_text(steps_icon);
  fm_battery_ring = fm_complication(row, KIT_COLOR_GREEN, ICON_BATTERY_3, &fm_battery_icon, &fm_battery_value, NULL);
  lv_obj_t *weather_ring = fm_complication(row, KIT_COLOR_CARD2, ICON_CLOUD_SUN, &fm_weather_icon, &fm_weather_value, weather_open);
  lv_arc_set_value(weather_ring, 0);

  fm_next_row = kit_container(fm_screen);
  lv_obj_set_size(fm_next_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(fm_next_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(fm_next_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(fm_next_row, 10, 0);
  lv_obj_align(fm_next_row, LV_ALIGN_TOP_MID, 0, 346);
  fm_next_icon = kit_label(fm_next_row, &font_icons_24, KIT_COLOR_ORANGE, ICON_CLOCK);
  fm_next_label = kit_label(fm_next_row, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  lv_obj_set_style_max_width(fm_next_label, 280, 0);
  lv_label_set_long_mode(fm_next_label, LV_LABEL_LONG_MODE_DOTS);

  face_status_create(fm_screen, false);  // the battery has its own ring

  lv_subject_add_observer_obj(subj_time_text, fm_time_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_steps, fm_steps_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_step_goal, fm_steps_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_battery, fm_battery_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_charging, fm_battery_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_weather, fm_weather_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_temp_unit, fm_weather_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_alarm_next, fm_next_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_time_24h, fm_next_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_agenda, fm_next_obs, fm_screen, NULL);
  lv_subject_add_observer_obj(subj_time_text, fm_next_obs, fm_screen, NULL);
  return fm_screen;
}
