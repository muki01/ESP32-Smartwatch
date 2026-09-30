/*
 * alarm_app.cpp - Alarms: the list (switch per alarm), the editor (time rollers, repeat
 * days) and the ringing alert. A ringing alarm wakes the screen and plays until Stop or
 * Snooze (5 minutes) is pressed; a side button snoozes. After 2 minutes it stops by
 * itself and leaves a "Missed alarm" notification. Alarms also ring in Do not disturb and
 * silent mode. Schedule and storage: services/alarms.cpp.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../services/alarms.h"
#include "../../services/clock.h"
#include "../../services/notifications.h"
#include "../kit/kit.h"

#define ALARM_RING_MS  120000

static const char *const ALARM_DAY_LETTER[] = { "M", "T", "W", "T", "F", "S", "S" };

static uint16_t alarm_ring_hm = 0xFFFF;  // hour * 60 + minute of the ringing alarm
static lv_timer_t *alarm_ring_timer;
static lv_obj_t *alarm_list_content;     // content of the open alarm list page
static int alarm_edit_index;
static lv_obj_t *alarm_edit_hour, *alarm_edit_minute, *alarm_edit_ampm;
static lv_obj_t *alarm_edit_days[7];

/* ================================ Ringing ========================================= */

static void alarm_stop_ringing() {
  audio_alert_stop();
  if (alarm_ring_timer) {
    lv_timer_delete(alarm_ring_timer);
    alarm_ring_timer = NULL;
  }
  alarm_ring_hm = 0xFFFF;
}

static void alarm_alert_cb(int button) {
  alarm_stop_ringing();
  if (button == 1 || button == 2) {  // Snooze button or a side button
    alarms_snooze();
    kit_toast("Snoozed for 5 minutes");
  }
}

static void alarm_timeout_cb(lv_timer_t *t) {
  alarm_ring_timer = NULL;  // one-shot
  char body[48], hm[16];
  clock_format_hm(alarm_ring_hm / 60, alarm_ring_hm % 60, hm, sizeof(hm));
  snprintf(body, sizeof(body), "Alarm at %s was not stopped", hm);
  alarm_stop_ringing();
  kit_alert_close();
  notify_post("Alarm", "Missed alarm", body, ICON_CLOCK, KIT_COLOR_ORANGE);
}

static void alarm_ring(int hour, int minute, bool snoozed) {
  if (kit_alert_active()) return;  // a timer or call is on screen: skip rather than stack alerts
  char title[16];
  clock_format_hm(hour, minute, title, sizeof(title));
  alarm_ring_hm = hour * 60 + minute;
  audio_alert_start(ALERT_ALARM);
  kit_alert(ICON_CLOCK, KIT_COLOR_ORANGE, title, snoozed ? "Snoozed alarm" : "Alarm", "Stop", "Snooze", alarm_alert_cb);
  alarm_ring_timer = lv_timer_create(alarm_timeout_cb, ALARM_RING_MS, NULL);
  lv_timer_set_repeat_count(alarm_ring_timer, 1);
}

void alarm_app_init() {
  alarms_set_ring_handler(alarm_ring);
}

/* ================================ Alarm list ====================================== */

static lv_obj_t *alarm_build_edit(int index);

static void alarm_row_clicked_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(alarm_build_edit((int)(intptr_t)lv_event_get_user_data(e)));
}

static void alarm_switch_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (!alarms_get(i)) return;
  audio_click();
  bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
  alarms_enable(i, on);
  lv_obj_t *title = kit_row_title(lv_obj_get_parent(lv_event_get_target_obj(e)));
  lv_obj_set_style_text_color(title, lv_color_hex(on ? 0xFFFFFF : KIT_COLOR_TEXT2), 0);
}

static void alarm_add_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(alarm_build_edit(-1));
}

static void alarm_list_fill() {
  lv_obj_t *c = alarm_list_content;
  if (!c) return;
  lv_obj_clean(c);
  int n = alarms_count();
  if (n == 0) kit_empty_state(c, ICON_CLOCK, "No alarms yet");
  for (int i = 0; i < n; i++) {
    const Alarm &a = *alarms_get(i);
    char hm[16], days[40];
    clock_format_hm(a.hour, a.minute, hm, sizeof(hm));
    alarms_days_text(a.days, days, sizeof(days));
    lv_obj_t *row = kit_row(c, NULL, 0, hm, days, false);
    lv_obj_t *title = kit_row_title(row);
    lv_obj_set_style_text_font(title, &font_num_44, 0);
    lv_obj_set_height(title, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(title, lv_color_hex(a.enabled ? 0xFFFFFF : KIT_COLOR_TEXT2), 0);
    lv_obj_add_event_cb(row, alarm_row_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *sw = kit_switch(row);
    lv_obj_set_clickable(sw, true);
    lv_obj_set_ext_click_area(sw, 14);
    if (a.enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, alarm_switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
  }
  if (n < ALARM_MAX) {
    lv_obj_t *row = kit_row_box(c, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_t *add = kit_round_button(row, ICON_PLUS, KIT_COLOR_ORANGE, 68);
    lv_obj_add_event_cb(add, alarm_add_cb, LV_EVENT_CLICKED, NULL);
  }
}

static void alarm_list_deleted_cb(lv_event_t *e) {
  alarm_list_content = NULL;
}

void alarm_open() {
  lv_obj_t *page = kit_page_create("Alarms", true);
  alarm_list_content = kit_page_content(page);
  lv_obj_add_event_cb(page, alarm_list_deleted_cb, LV_EVENT_DELETE, NULL);
  alarm_list_fill();
  kit_page_push(page);
}

/* ================================ Editor ========================================== */

static void alarm_edit_save_cb(lv_event_t *e) {
  audio_click();
  Alarm a;
  int hour = lv_roller_get_selected(alarm_edit_hour);
  if (alarm_edit_ampm) hour = hour % 12 + (lv_roller_get_selected(alarm_edit_ampm) ? 12 : 0);  // "12" is index 0
  a.hour = (uint8_t)hour;
  a.minute = (uint8_t)lv_roller_get_selected(alarm_edit_minute);
  a.days = 0;
  for (int i = 0; i < 7; i++) {
    if (lv_obj_has_state(alarm_edit_days[i], LV_STATE_CHECKED)) a.days |= 1 << i;
  }
  a.enabled = 1;
  alarms_put(alarm_edit_index, &a);
  alarm_list_fill();
  kit_page_pop();

  int delta = alarms_minutes_until(&a);  // "Alarm in 7 h 12 min"
  if (delta >= 0) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Alarm in %d h %d min", delta / 60, delta % 60);
    kit_toast(msg);
  }
}

static void alarm_delete_cb(void *user_data) {
  alarms_remove(alarm_edit_index);
  alarm_list_fill();
  kit_page_pop();
}

static void alarm_edit_delete_cb(lv_event_t *e) {
  audio_click();
  kit_confirm("Delete alarm?", NULL, "Delete", true, alarm_delete_cb, NULL);
}

static char *alarm_number_options(char *buf, size_t len, int from, int to, bool pad) {
  size_t used = 0;
  buf[0] = 0;
  for (int v = from; v <= to && used < len; v++) {
    used += snprintf(buf + used, len - used, pad ? "%s%02d" : "%s%d", v == from ? "" : "\n", v);
  }
  return buf;
}

static lv_obj_t *alarm_time_roller(lv_obj_t *parent, const char *options, int selected, int32_t width) {
  lv_obj_t *r = kit_roller(parent, options, selected, width);
  lv_obj_set_style_text_font(r, &font_num_44, 0);
  lv_obj_set_style_text_font(r, &font_num_44, LV_PART_SELECTED);
  return r;
}

static lv_obj_t *alarm_build_edit(int index) {
  static char hours[24 * 3 + 1], minutes[60 * 3 + 1];
  alarm_edit_index = index;
  Alarm a = { 7, 0, 0x1F, 1 };
  if (alarms_get(index)) a = *alarms_get(index);

  lv_obj_t *page = kit_page_create(index >= 0 ? "Edit alarm" : "New alarm", true);
  lv_obj_t *c = kit_page_content(page);

  lv_obj_t *time_row = kit_row_box(c, LV_FLEX_ALIGN_CENTER, 8);
  bool h24 = lv_subject_get_int(subj_time_24h);
  if (h24) {
    alarm_edit_hour = alarm_time_roller(time_row, alarm_number_options(hours, sizeof(hours), 0, 23, true), a.hour, 104);
  } else {
    // 12, 1, 2 ... 11 (index = hour % 12)
    snprintf(hours, sizeof(hours), "12");
    size_t used = strlen(hours);
    for (int h = 1; h <= 11; h++) used += snprintf(hours + used, sizeof(hours) - used, "\n%d", h);
    alarm_edit_hour = alarm_time_roller(time_row, hours, a.hour % 12, 92);
  }
  kit_label(time_row, &font_num_44, 0xFFFFFF, ":");
  alarm_edit_minute = alarm_time_roller(time_row, alarm_number_options(minutes, sizeof(minutes), 0, 59, true), a.minute, 104);
  alarm_edit_ampm = NULL;
  if (!h24) alarm_edit_ampm = kit_roller(time_row, "AM\nPM", a.hour >= 12 ? 1 : 0, 76);

  kit_section(c, "REPEAT");
  lv_obj_t *days = kit_row_box(c, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
  for (int i = 0; i < 7; i++) {
    lv_obj_t *chip = kit_chip(days, ALARM_DAY_LETTER[i]);
    lv_obj_set_size(chip, 42, 42);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_set_style_pad_top(chip, (42 - lv_font_get_line_height(KIT_FONT_SMALL)) / 2, 0);
    lv_obj_set_style_text_font(chip, KIT_FONT_SMALL, 0);
    lv_obj_set_checkable(chip, true);
    if (a.days & (1 << i)) lv_obj_add_state(chip, LV_STATE_CHECKED);
    alarm_edit_days[i] = chip;
  }
  kit_note(c, "Without repeat days the alarm rings once.");

  lv_obj_t *save = kit_button(c, "Save", KIT_COLOR_ORANGE);
  lv_obj_set_style_margin_top(save, 6, 0);
  lv_obj_add_event_cb(save, alarm_edit_save_cb, LV_EVENT_CLICKED, NULL);
  if (index >= 0) {
    lv_obj_t *del = kit_button(c, "Delete", KIT_COLOR_CARD2);
    lv_obj_set_style_text_color(del, lv_color_hex(KIT_COLOR_RED), 0);
    lv_obj_add_event_cb(del, alarm_edit_delete_cb, LV_EVENT_CLICKED, NULL);
  }
  return page;
}
