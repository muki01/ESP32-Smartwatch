/*
 * face_digital.cpp - The layered clock of the original Muki design: the hour's tens in dim
 * grey running off the left edge, its ones in white, the minutes in the accent colour; a
 * date column, today's weather and the steps below.
 */
#include "faces.h"

#include <Arduino.h>
#include "../../core/format.h"
#include "../../core/settings.h"
#include "../../services/weather.h"
#include "../apps/apps.h"
#include "../kit/kit.h"
#include "../system/nav.h"

#define FD_TENS_RIGHT   78    // right edge of the hour's tens digit
#define FD_ONES_X       88
#define FD_HOUR_Y       60    // font_light_180 holds digits only: they fill the label (129 px)
#define FD_MIN_X        216
#define FD_MIN_Y        146   // font_num_110 digits start 16 px below the label top
#define FD_DATE_X       216

static lv_obj_t *fd_tens, *fd_ones, *fd_minutes, *fd_weekday, *fd_date, *fd_year;
static lv_obj_t *fd_temp, *fd_weather_icon, *fd_condition, *fd_range, *fd_steps;

static void fd_time_obs(lv_observer_t *observer, lv_subject_t *subject) {
  struct tm t;
  face_now(&t);
  bool h24 = lv_subject_get_int(subj_time_24h);
  int hour = h24 ? t.tm_hour : face_hour12(t.tm_hour);
  lv_label_set_text_fmt(fd_tens, "%d", hour / 10);
  lv_label_set_text_fmt(fd_ones, "%d", hour % 10);
  lv_label_set_text_fmt(fd_minutes, "%02d", t.tm_min);
  lv_label_set_text(fd_weekday, FACE_WEEKDAYS_SHORT[t.tm_wday]);
  lv_label_set_text_fmt(fd_date, "%d %s", t.tm_mday, FACE_MONTHS_SHORT[t.tm_mon]);
  if (h24) lv_label_set_text_fmt(fd_year, "%d", t.tm_year + 1900);
  else lv_label_set_text(fd_year, t.tm_hour < 12 ? "AM" : "PM");
}

static void fd_weather_obs(lv_observer_t *observer, lv_subject_t *subject) {
  face_weather_value(fd_weather_icon, fd_temp);
  const WeatherInfo *w = weather_info();
  if (w->valid) {
    lv_label_set_text(fd_condition, weather_text(w->code));
    lv_label_set_text_fmt(fd_range, "H %d\xC2\xB0   L %d\xC2\xB0", weather_temp(w->days[0].t_max), weather_temp(w->days[0].t_min));
  } else {
    lv_label_set_text(fd_condition, "");
    lv_label_set_text(fd_range, "");
  }
}

static void fd_steps_obs(lv_observer_t *observer, lv_subject_t *subject) {
  char buf[16];
  format_thousands(lv_subject_get_int(subj_steps), buf, sizeof(buf));
  lv_label_set_text(fd_steps, buf);
}

lv_obj_t *face_digital_create() {
  lv_obj_t *s = face_screen_create();
  face_status_create(s, true);

  fd_tens = kit_label(s, &font_light_180, 0x4A4A4E, "");
  lv_obj_set_width(fd_tens, 160);
  lv_obj_set_style_text_align(fd_tens, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(fd_tens, FD_TENS_RIGHT - 160, FD_HOUR_Y);
  fd_ones = kit_label(s, &font_light_180, 0xFFFFFF, "");
  lv_obj_set_pos(fd_ones, FD_ONES_X, FD_HOUR_Y);
  fd_minutes = face_accent_label(s, &font_num_110, "");
  lv_obj_set_pos(fd_minutes, FD_MIN_X, FD_MIN_Y);

  fd_weekday = kit_label(s, &font_bold_28, 0xFFFFFF, "");
  lv_obj_set_pos(fd_weekday, FD_DATE_X, 62);
  fd_date = kit_label(s, KIT_FONT_BODY, 0xFFFFFF, "");
  lv_obj_set_pos(fd_date, FD_DATE_X, 93);
  fd_year = kit_label(s, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  lv_obj_set_pos(fd_year, FD_DATE_X, 120);

  lv_obj_t *weather = kit_container(s);
  lv_obj_set_size(weather, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(weather, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(weather, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(weather, 8, 0);
  lv_obj_set_pos(weather, 66, 252);
  fd_temp = kit_label(weather, KIT_FONT_TITLE, 0xFFFFFF, "");
  fd_weather_icon = kit_label(weather, &font_icons_32, 0xFFFFFF, "");
  nav_make_complication(weather, weather_open);

  lv_obj_t *info = kit_col_box(s, LV_FLEX_ALIGN_CENTER, 2);
  lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 306);
  fd_condition = kit_label(info, KIT_FONT_BODY, 0xFFFFFF, "");
  fd_range = kit_label(info, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
  nav_make_complication(info, weather_open);

  lv_obj_t *steps = kit_container(s);
  lv_obj_set_size(steps, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(steps, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(steps, 10, 0);
  lv_obj_align(steps, LV_ALIGN_TOP_MID, 0, 378);
  face_accent_label(steps, &font_icons_24, ICON_STEPS);
  fd_steps = kit_label(steps, KIT_FONT_BODY, 0xFFFFFF, "");
  nav_make_complication(steps, activity_open);

  lv_subject_add_observer_obj(subj_time_text, fd_time_obs, s, NULL);
  lv_subject_add_observer_obj(subj_date_text, fd_time_obs, s, NULL);
  lv_subject_add_observer_obj(subj_weather, fd_weather_obs, s, NULL);
  lv_subject_add_observer_obj(subj_temp_unit, fd_weather_obs, s, NULL);
  lv_subject_add_observer_obj(subj_steps, fd_steps_obs, s, NULL);
  return s;
}
