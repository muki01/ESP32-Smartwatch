/*
 * face_common.cpp - What the watch faces share: the face screen with its background
 * texture, the accent colour and the status row.
 *
 * Faces are LVGL primitives over one optional background bitmap (the dark geometric
 * texture of the original Muki design, or plain black). Every face follows
 * subj_time_text / subj_date_text (once a minute). Accent-coloured parts share four
 * styles, so a new colour reaches every face at once. Tapping a complication opens its
 * app, a long press anywhere opens the face picker (nav.cpp).
 */
#include "faces.h"

#include <Arduino.h>
#include "../../assets/images/images.h"
#include "../../core/settings.h"
#include "../../services/clap_control.h"
#include "../../services/weather.h"
#include "../kit/kit.h"

const char *const FACE_WEEKDAYS_SHORT[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
const char *const FACE_WEEKDAYS_LONG[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char *const FACE_MONTHS_SHORT[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
const char *const FACE_MONTHS_LONG[12] = { "January", "February", "March", "April", "May", "June", "July",
                                           "August", "September", "October", "November", "December" };

static const uint32_t FACE_COLORS[FACE_COLOR_COUNT] = {
  0xEE1E1E, 0xFF7A00, 0xFFD60A, 0x30D158, 0x40C8E0, 0x0A84FF, 0x8E6CFF, 0xFF375F, 0xFFFFFF
};
static const char *const FACE_COLOR_NAMES[FACE_COLOR_COUNT] = {
  "Red", "Orange", "Yellow", "Green", "Teal", "Blue", "Violet", "Pink", "White"
};

static lv_style_t face_st_text, face_st_bg, face_st_line, face_st_arc;  // accent colour
static bool face_styles_ready;

void face_now(struct tm *t) {
  time_t now = time(NULL);
  localtime_r(&now, t);
}

int face_hour12(int hour) {
  int h = hour % 12;
  return h ? h : 12;
}

/* ================================ Accent colour =================================== */

uint32_t face_accent() {
  return face_color_value(lv_subject_get_int(subj_face_color));
}

uint32_t face_color_value(int index) {
  return FACE_COLORS[index >= 0 && index < FACE_COLOR_COUNT ? index : 0];
}

const char *face_color_name(int index) {
  return index >= 0 && index < FACE_COLOR_COUNT ? FACE_COLOR_NAMES[index] : "";
}

static void face_accent_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_color_t c = lv_color_hex(face_accent());
  lv_style_set_text_color(&face_st_text, c);
  lv_style_set_bg_color(&face_st_bg, c);
  lv_style_set_line_color(&face_st_line, c);
  lv_style_set_arc_color(&face_st_arc, c);
  lv_obj_report_style_change(&face_st_text);
  lv_obj_report_style_change(&face_st_bg);
  lv_obj_report_style_change(&face_st_line);
  lv_obj_report_style_change(&face_st_arc);
}

static void face_styles_init() {
  if (face_styles_ready) return;
  face_styles_ready = true;
  lv_style_init(&face_st_text);
  lv_style_init(&face_st_bg);
  lv_style_init(&face_st_line);
  lv_style_init(&face_st_arc);
  lv_subject_add_observer(subj_face_color, face_accent_obs, NULL);  // sets the colours right away
}

// A local colour would win over the shared style, so it is removed.
void face_accent_text(lv_obj_t *obj) {
  face_styles_init();
  lv_obj_remove_local_style_prop(obj, LV_STYLE_TEXT_COLOR, 0);
  lv_obj_add_style(obj, &face_st_text, 0);
}

void face_accent_bg(lv_obj_t *obj) {
  face_styles_init();
  lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_COLOR, 0);
  lv_obj_add_style(obj, &face_st_bg, 0);
}

void face_accent_line(lv_obj_t *obj) {
  face_styles_init();
  lv_obj_remove_local_style_prop(obj, LV_STYLE_LINE_COLOR, 0);
  lv_obj_add_style(obj, &face_st_line, 0);
}

void face_accent_arc(lv_obj_t *obj) {
  face_styles_init();
  lv_obj_remove_local_style_prop(obj, LV_STYLE_ARC_COLOR, LV_PART_INDICATOR);
  lv_obj_add_style(obj, &face_st_arc, LV_PART_INDICATOR);
}

lv_obj_t *face_accent_label(lv_obj_t *parent, const lv_font_t *font, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  face_accent_text(label);
  lv_label_set_text(label, text);
  return label;
}

/* ================================ Screen and status row =========================== */

lv_obj_t *face_screen_create() {
  face_styles_init();
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(screen, lv_color_white(), 0);
  lv_obj_set_scrollable(screen, false);
  lv_obj_t *bg = lv_image_create(screen);
  lv_image_set_src(bg, &img_face_bg);
  lv_obj_set_floating(bg, true);  // outside any flex layout of the screen
  lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
  lv_obj_bind_bool(bg, subj_face_texture, kit_set_visible);
  return screen;
}

static void face_battery_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *row = lv_observer_get_target_obj(observer);
  lv_obj_t *icon = lv_obj_get_child(row, 0);
  lv_obj_t *text = lv_obj_get_child(row, 1);
  int32_t level = lv_subject_get_int(subj_battery);
  bool charging = lv_subject_get_int(subj_charging);
  lv_obj_set_hidden(icon, level < 0 && !charging);
  lv_obj_set_hidden(text, level < 0);
  const char *glyph = charging ? ICON_BOLT : level > 80 ? ICON_BATTERY_FULL : level > 55 ? ICON_BATTERY_3
                                           : level > 30 ? ICON_BATTERY_2 : level > 12 ? ICON_BATTERY_1 : ICON_BATTERY_EMPTY;
  uint32_t color = charging ? face_accent() : level >= 0 && level <= 15 ? KIT_COLOR_RED : 0xFFFFFF;
  lv_label_set_text(icon, glyph);
  lv_obj_set_style_text_color(icon, lv_color_hex(color), 0);
  lv_label_set_text_fmt(text, "%d%%", (int)level);
}

static void face_wifi_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_set_hidden(lv_observer_get_target_obj(observer), lv_subject_get_int(subject) != WIFI_ST_CONNECTED);
}

static void face_bt_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_set_hidden(lv_observer_get_target_obj(observer), lv_subject_get_int(subject) != BT_ST_CONNECTED);
}

static void face_workout_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_set_hidden(lv_observer_get_target_obj(observer), lv_subject_get_int(subject) == WORKOUT_NONE);
}

// Bedtime mode shows a bed instead of the do-not-disturb moon (it includes it).
static void face_quiet_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *icon = lv_observer_get_target_obj(observer);
  bool bedtime = lv_subject_get_int(subj_bedtime_on);
  lv_label_set_text(icon, bedtime ? ICON_BED : ICON_MOON);
  lv_obj_set_hidden(icon, !bedtime && !lv_subject_get_int(subj_dnd));
}

// Privacy: the microphone is open while clap control listens.
static void face_mic_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_set_hidden(lv_observer_get_target_obj(observer), !clap_control_listening());
}

lv_obj_t *face_status_create(lv_obj_t *parent, bool battery) {
  lv_obj_t *row = kit_container(parent);
  lv_obj_set_size(row, LV_SIZE_CONTENT, 28);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 10);

  if (battery) {
    lv_obj_t *bat = kit_container(row);
    lv_obj_set_size(bat, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bat, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bat, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bat, 6, 0);
    kit_label(bat, &font_icons_24, 0xFFFFFF, "");
    kit_label(bat, KIT_FONT_SMALL, 0xFFFFFF, "");
    lv_subject_add_observer_obj(subj_battery, face_battery_obs, bat, NULL);
    lv_subject_add_observer_obj(subj_charging, face_battery_obs, bat, NULL);
    lv_subject_add_observer_obj(subj_face_color, face_battery_obs, bat, NULL);
  }
  lv_obj_t *wifi = face_accent_label(row, &font_icons_24, ICON_WIFI);
  lv_subject_add_observer_obj(subj_wifi_state, face_wifi_obs, wifi, NULL);
  lv_obj_t *bt = face_accent_label(row, &font_icons_24, ICON_BLUETOOTH);
  lv_subject_add_observer_obj(subj_bt_state, face_bt_obs, bt, NULL);
  lv_obj_t *workout = face_accent_label(row, &font_icons_24, ICON_RUNNING);
  lv_subject_add_observer_obj(subj_workout, face_workout_obs, workout, NULL);
  lv_obj_t *quiet = kit_label(row, &font_icons_24, 0xD0D0D0, ICON_MOON);
  lv_subject_add_observer_obj(subj_dnd, face_quiet_obs, quiet, NULL);
  lv_subject_add_observer_obj(subj_bedtime_on, face_quiet_obs, quiet, NULL);
  lv_obj_t *saver = kit_label(row, &font_icons_24, KIT_COLOR_GREEN, ICON_LEAF);
  lv_obj_bind_bool(saver, subj_saver, kit_set_visible);
  lv_obj_t *mic = kit_label(row, &font_icons_24, KIT_COLOR_ORANGE, ICON_MICROPHONE);
  lv_subject_t *const MIC_SUBJECTS[] = { subj_ext_clap, subj_ext_lights, subj_wifi_state, subj_lights };
  for (lv_subject_t *s : MIC_SUBJECTS) lv_subject_add_observer_obj(s, face_mic_obs, mic, NULL);
  lv_obj_t *dot = kit_container(row);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  face_accent_bg(dot);
  lv_obj_bind_bool(dot, subj_notif_unread, kit_set_visible);
  return row;
}

void face_weather_value(lv_obj_t *icon, lv_obj_t *value) {
  const WeatherInfo *w = weather_info();
  if (!w->valid) {
    if (icon) {
      lv_label_set_text(icon, ICON_CLOUD_SUN);
      lv_obj_set_style_text_color(icon, lv_color_hex(KIT_COLOR_GRAY), 0);
    }
    lv_label_set_text(value, "--\xC2\xB0");
    return;
  }
  if (icon) {
    lv_label_set_text(icon, weather_icon(w->code, w->is_day));
    lv_obj_set_style_text_color(icon, lv_color_hex(weather_color(w->code, w->is_day)), 0);
  }
  lv_label_set_text_fmt(value, "%d\xC2\xB0", weather_temp(w->temp));
}
