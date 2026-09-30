/*
 * face_minimal.cpp - Just the time and the date: calm, and the fewest lit pixels.
 */
#include "faces.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../kit/kit.h"

static lv_obj_t *fmin_time, *fmin_ampm, *fmin_date;

static void fmin_time_obs(lv_observer_t *observer, lv_subject_t *subject) {
  struct tm t;
  face_now(&t);
  bool h24 = lv_subject_get_int(subj_time_24h);
  if (h24) lv_label_set_text_fmt(fmin_time, "%02d:%02d", t.tm_hour, t.tm_min);
  else lv_label_set_text_fmt(fmin_time, "%d:%02d", face_hour12(t.tm_hour), t.tm_min);
  lv_obj_set_hidden(fmin_ampm, h24);
  lv_label_set_text(fmin_ampm, t.tm_hour < 12 ? "AM" : "PM");
  lv_label_set_text_fmt(fmin_date, "%s, %d %s", FACE_WEEKDAYS_LONG[t.tm_wday], t.tm_mday, FACE_MONTHS_LONG[t.tm_mon]);
}

lv_obj_t *face_minimal_create() {
  lv_obj_t *s = face_screen_create();
  face_status_create(s, true);

  lv_obj_t *col = kit_col_box(s, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_align(col, LV_ALIGN_CENTER, 0, -16);
  fmin_ampm = kit_label(col, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  fmin_time = kit_label(col, &font_num_110, 0xFFFFFF, "");
  lv_obj_t *bar = kit_container(col);
  lv_obj_set_size(bar, 44, 4);
  lv_obj_set_style_radius(bar, 2, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  face_accent_bg(bar);
  lv_obj_set_style_margin_top(bar, 14, 0);
  lv_obj_set_style_margin_bottom(bar, 16, 0);
  fmin_date = kit_label(col, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");

  lv_subject_add_observer_obj(subj_time_text, fmin_time_obs, s, NULL);
  lv_subject_add_observer_obj(subj_date_text, fmin_time_obs, s, NULL);
  return s;
}
