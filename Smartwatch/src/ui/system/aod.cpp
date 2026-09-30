/*
 * aod.cpp - The always-on display: a black screen with a thin, dim clock.
 *
 * Only a few hundred pixels are lit and the screen redraws once a minute, so the panel
 * can stay on for hours. Every minute the content moves by a few pixels to spread the
 * wear of the OLED pixels (burn-in protection).
 */
#include "aod.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../kit/kit.h"
#include "screen.h"

#define AOD_TIME_COLOR  0xB4B4B4
#define AOD_DIM_COLOR   0x6E6E73
#define AOD_SHIFT_PX    8

static lv_obj_t *aod_screen;
static lv_obj_t *aod_box;
static lv_obj_t *aod_time;
static lv_obj_t *aod_ampm;
static lv_obj_t *aod_date;
static lv_obj_t *aod_battery_icon;
static lv_obj_t *aod_battery;
static lv_obj_t *aod_bell;
static lv_obj_t *aod_moon;
static uint8_t aod_step;

static void aod_update_time() {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  if (lv_subject_get_int(subj_time_24h)) {
    lv_label_set_text_fmt(aod_time, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_obj_set_hidden(aod_ampm, true);
  } else {
    int h = t.tm_hour % 12;
    lv_label_set_text_fmt(aod_time, "%d:%02d", h ? h : 12, t.tm_min);
    lv_label_set_text(aod_ampm, t.tm_hour < 12 ? "AM" : "PM");
    lv_obj_set_hidden(aod_ampm, false);
  }
  lv_label_set_text(aod_date, lv_subject_get_string(subj_date_text));

  int32_t level = lv_subject_get_int(subj_battery);
  lv_obj_set_hidden(aod_battery_icon, level < 0);
  lv_obj_set_hidden(aod_battery, level < 0);
  lv_label_set_text(aod_battery_icon, lv_subject_get_int(subj_charging) ? ICON_BOLT : level > 60 ? ICON_BATTERY_3 : level > 20 ? ICON_BATTERY_2 : ICON_BATTERY_1);
  lv_label_set_text_fmt(aod_battery, "%d%%", (int)level);
  lv_obj_set_hidden(aod_bell, lv_subject_get_int(subj_notif_unread) == 0);
  lv_obj_set_hidden(aod_moon, !lv_subject_get_int(subj_dnd));
}

// Moves the clock around a small circle of positions, one step per minute.
void aod_refresh() {
  if (!aod_screen) return;
  static const int8_t dx[] = { 0, 1, 1, 0, -1, -1, 0, 1, -1 };
  static const int8_t dy[] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };
  aod_step = (aod_step + 1) % sizeof(dx);
  lv_obj_align(aod_box, LV_ALIGN_CENTER, dx[aod_step] * AOD_SHIFT_PX, dy[aod_step] * AOD_SHIFT_PX - 10);
  aod_update_time();
}

static void aod_minute_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (screen_in_aod()) aod_refresh();
}

lv_obj_t *aod_screen_create() {
  if (aod_screen) return aod_screen;
  aod_screen = lv_obj_create(NULL);
  lv_obj_remove_style_all(aod_screen);
  lv_obj_set_style_bg_color(aod_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(aod_screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(aod_screen, false);

  aod_box = kit_col_box(aod_screen, LV_FLEX_ALIGN_CENTER, 4);

  lv_obj_t *time_row = kit_container(aod_box);
  lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(time_row, 6, 0);
  aod_time = kit_label(time_row, &font_num_110, AOD_TIME_COLOR, "--:--");
  aod_ampm = kit_label(time_row, KIT_FONT_BODY, AOD_DIM_COLOR, "");
  lv_obj_set_style_pad_bottom(aod_ampm, 18, 0);

  aod_date = kit_label(aod_box, KIT_FONT_BODY, AOD_DIM_COLOR, "");

  lv_obj_t *status = kit_container(aod_box);
  lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(status, 14, 0);
  lv_obj_set_style_pad_top(status, 22, 0);
  aod_battery_icon = kit_label(status, &font_icons_24, AOD_DIM_COLOR, "");
  aod_battery = kit_label(status, KIT_FONT_SMALL, AOD_DIM_COLOR, "");
  lv_obj_set_style_margin_left(aod_battery, -6, 0);
  aod_bell = kit_label(status, &font_icons_24, AOD_DIM_COLOR, ICON_BELL);
  aod_moon = kit_label(status, &font_icons_24, AOD_DIM_COLOR, ICON_MOON);

  lv_subject_add_observer_obj(subj_time_text, aod_minute_obs, aod_screen, NULL);
  aod_refresh();
  return aod_screen;
}
