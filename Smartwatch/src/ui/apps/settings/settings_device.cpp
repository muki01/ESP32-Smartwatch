/*
 * settings_device.cpp - Settings > Display (brightness, timeout, always-on, tap / raise to
 * wake, motion sensor calibration, watch face), Sound, Notifications, Battery, Date & time
 * and Units.
 */
#include "settings_internal.h"

#include <Arduino.h>
#include "../../../core/settings.h"
#include "../../../core/system.h"
#include "../../../drivers/audio.h"
#include "../../../drivers/imu.h"
#include "../../../drivers/power.h"
#include "../../../services/clock.h"
#include "../../../services/notifications.h"
#include "../../faces/faces.h"

/* ================================ Display ========================================= */

static const int32_t SUI_TIMEOUT_VALUES[] = { 5, 10, 15, 30, 60, 120, 300, 600 };
static const char *const SUI_TIMEOUT_LABELS[] = { "5 seconds", "10 seconds", "15 seconds", "30 seconds",
                                                  "1 minute", "2 minutes", "5 minutes", "10 minutes" };
static const KitChoice SUI_TIMEOUT_CHOICE = { "Timeout", &subj_screen_timeout, SUI_TIMEOUT_VALUES, SUI_TIMEOUT_LABELS, 8 };
static const char *const SUI_FACE_LABELS[] = { "Digital", "Analog", "Modular", "Minimal" };
static const KitChoice SUI_FACE_CHOICE = { "Watch face", &subj_watchface, NULL, SUI_FACE_LABELS, FACE_COUNT };

static void sui_format_timeout(int32_t seconds, char *buf, size_t len) {
  if (seconds < 60) snprintf(buf, len, "%d s", (int)seconds);
  else snprintf(buf, len, "%d min", (int)(seconds / 60));
}

static lv_obj_t *sui_build_timeout() {
  return kit_choice_page(&SUI_TIMEOUT_CHOICE);
}

static lv_obj_t *sui_build_face() {
  return kit_choice_page(&SUI_FACE_CHOICE);
}

static void sui_timeout_obs(lv_observer_t *observer, lv_subject_t *subject) {
  char buf[16];
  sui_format_timeout(lv_subject_get_int(subject), buf, sizeof(buf));
  lv_label_set_text(lv_observer_get_target_obj(observer), buf);
}

static void sui_face_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), SUI_FACE_LABELS[lv_subject_get_int(subject) % FACE_COUNT]);
}

static void sui_face_style_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%s  \xE2\x80\xA2  %s",
                        face_color_name(lv_subject_get_int(subj_face_color)),
                        lv_subject_get_int(subj_face_texture) ? "Texture" : "Black");
}

static void sui_face_style_cb(lv_event_t *e) {
  audio_click();
  face_customize_open();
}

void sui_display_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  char timeout[16];
  sui_format_timeout(lv_subject_get_int(subj_screen_timeout), timeout, sizeof(timeout));
  lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%d%%  \xE2\x80\xA2  sleep %s%s",
                        (int)lv_subject_get_int(subj_brightness), timeout, lv_subject_get_int(subj_aod) ? "  \xE2\x80\xA2  AOD" : "");
}

// Why a wake setting does nothing right now (bedtime mode, battery saver).
static void sui_wake_note_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  const char *normal = (const char *)lv_observer_get_user_data(observer);
  bool aod_row = !strcmp(normal, "Dim clock when idle");
  if (lv_subject_get_int(subj_bedtime_on)) lv_label_set_text(label, "Paused in bedtime mode");
  else if (aod_row && lv_subject_get_int(subj_saver)) lv_label_set_text(label, "Off with battery saver");
  else lv_label_set_text(label, normal);
}

static lv_obj_t *sui_wake_row(lv_obj_t *parent, const char *title, const char *subtitle, lv_subject_t *subject) {
  lv_obj_t *row = kit_switch_row(parent, NULL, 0, title, subtitle, subject);
  lv_obj_t *sub = kit_row_subtitle(row);
  lv_subject_add_observer_obj(subj_bedtime_on, sui_wake_note_obs, sub, (void *)subtitle);
  lv_subject_add_observer_obj(subj_saver, sui_wake_note_obs, sub, (void *)subtitle);
  return row;
}

static void sui_motion_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), imu_axes_calibrated() ? "Calibrated" : "Calibrate for raise to wake");
}

lv_obj_t *sui_build_display() {
  lv_obj_t *page = kit_page_create("Display", true);
  lv_obj_t *c = kit_page_content(page);
  kit_slider_card(c, "Brightness", subj_brightness, 5, 100, "%d%%");
  lv_obj_t *timeout = sui_nav_row(c, NULL, 0, "Screen timeout", sui_build_timeout);
  lv_subject_add_observer_obj(subj_screen_timeout, sui_timeout_obs, kit_row_subtitle(timeout), NULL);
  lv_obj_t *face = sui_nav_row(c, NULL, 0, "Watch face", sui_build_face);
  lv_subject_add_observer_obj(subj_watchface, sui_face_obs, kit_row_subtitle(face), NULL);
  lv_obj_t *style = kit_row(c, NULL, 0, "Face style", "", true);
  lv_subject_add_observer_obj(subj_face_color, sui_face_style_obs, kit_row_subtitle(style), NULL);
  lv_subject_add_observer_obj(subj_face_texture, sui_face_style_obs, kit_row_subtitle(style), NULL);
  lv_obj_add_event_cb(style, sui_face_style_cb, LV_EVENT_CLICKED, NULL);

  kit_section(c, "WAKE UP");
  sui_wake_row(c, "Always on", "Dim clock when idle", subj_aod);
  sui_wake_row(c, "Tap to wake", "Touch to turn on", subj_tap_to_wake);
  sui_wake_row(c, "Raise to wake", "Lift wrist to turn on", subj_raise_wake);
  lv_obj_t *motion = sui_nav_row(c, NULL, 0, "Motion sensor", sui_build_motion);
  lv_subject_add_observer_obj(subj_raise_wake, sui_motion_obs, kit_row_subtitle(motion), NULL);
  kit_note(c, "Press the side button to turn the screen on or off, hold it for the power menu. "
              "Long press the watch face to change it.");
  return page;
}

/* ================================ Sound =========================================== */

void sui_sound_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  int32_t volume = lv_subject_get_int(subj_volume);
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  if (lv_subject_get_int(subj_silent)) lv_label_set_text(label, "Silent");
  else if (volume == 0) lv_label_set_text(label, "Muted");
  else lv_label_set_text_fmt(label, "Volume %d%%", (int)volume);
}

static void sui_test_tone_cb(lv_event_t *e) {
  audio_beep(880, 120);
}

lv_obj_t *sui_build_sound() {
  lv_obj_t *page = kit_page_create("Sound", true);
  lv_obj_t *c = kit_page_content(page);
  kit_slider_card(c, "Volume", subj_volume, 0, 100, "%d%%");
  kit_switch_row(c, ICON_MUTE, KIT_COLOR_ORANGE, "Silent", "Alarms still ring", subj_silent);
  kit_switch_row(c, NULL, 0, "Touch sounds", "Click on every tap", subj_touch_sounds);
  lv_obj_t *test = kit_row(c, ICON_PLAY, KIT_COLOR_PINK, "Test sound", "Play a short tone", false);
  lv_obj_add_event_cb(test, sui_test_tone_cb, LV_EVENT_CLICKED, NULL);
  return page;
}

/* ================================ Notifications =================================== */

void sui_notif_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  int32_t count = lv_subject_get_int(subj_notif_count);
  if (lv_subject_get_int(subj_dnd)) lv_label_set_text(label, "Do not disturb is on");
  else if (count == 0) lv_label_set_text(label, "None");
  else lv_label_set_text_fmt(label, "%d waiting", (int)count);
}

static void sui_notif_count_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *row = lv_observer_get_target_obj(observer);
  int32_t count = lv_subject_get_int(subject);
  lv_label_set_text_fmt(kit_row_subtitle(row), count == 1 ? "%d notification" : "%d notifications", (int)count);
  lv_obj_set_disabled(row, count == 0);
}

static void sui_notif_test_cb(lv_event_t *e) {
  audio_click();
  notify_post(DEVICE_NAME, "Test notification",
              "Notifications work. Swipe right on the watch face to see them all.", ICON_BELL, KIT_COLOR_BLUE);
}

static void sui_notif_clear_cb(lv_event_t *e) {
  audio_click();
  notify_clear_all();
  kit_toast("Notifications cleared");
}

lv_obj_t *sui_build_notifications() {
  lv_obj_t *page = kit_page_create("Notifications", true);
  lv_obj_t *c = kit_page_content(page);
  kit_switch_row(c, NULL, 0, "Do not disturb", "Silent, screen stays off", subj_dnd);
  kit_switch_row(c, NULL, 0, "Sounds", "Chime on arrival", subj_notif_sounds);
  kit_switch_row(c, NULL, 0, "Wake screen", "Show them at once", subj_notif_wake);
  lv_obj_t *test = kit_row(c, ICON_BELL, KIT_COLOR_BLUE, "Send a test", NULL, false);
  lv_obj_add_event_cb(test, sui_notif_test_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *clear = kit_row(c, ICON_TRASH, KIT_COLOR_RED, "Clear all", "", false);
  lv_subject_add_observer_obj(subj_notif_count, sui_notif_count_obs, clear, NULL);
  lv_obj_add_event_cb(clear, sui_notif_clear_cb, LV_EVENT_CLICKED, NULL);
  kit_note(c, "Phone notifications arrive through the Gadgetbridge phone link (Bluetooth settings). "
              "Alarms and timers ring even with Do not disturb on.");
  return page;
}

/* ================================ Battery ========================================= */

static const char *sui_battery_state() {
  int32_t level = lv_subject_get_int(subj_battery);
  bool usb = lv_subject_get_int(subj_usb_power);
  if (level < 0) return usb ? "USB power, no battery" : "No battery";
  if (lv_subject_get_int(subj_charging)) return "Charging";
  if (usb) return level >= 99 ? "Fully charged" : "Plugged in";
  return "On battery";
}

static const char *sui_battery_symbol(int32_t level) {
  if (level < 0) return ICON_BATTERY_EMPTY;
  if (level > 80) return ICON_BATTERY_FULL;
  if (level > 55) return ICON_BATTERY_3;
  if (level > 30) return ICON_BATTERY_2;
  if (level > 10) return ICON_BATTERY_1;
  return ICON_BATTERY_EMPTY;
}

void sui_battery_row_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *row = lv_observer_get_target_obj(observer);
  int32_t level = lv_subject_get_int(subj_battery);
  lv_label_set_text(lv_obj_get_child(row, 0), lv_subject_get_int(subj_charging) ? ICON_BOLT : sui_battery_symbol(level));
  if (level < 0) lv_label_set_text(kit_row_subtitle(row), sui_battery_state());
  else lv_label_set_text_fmt(kit_row_subtitle(row), "%d%%  \xE2\x80\xA2  %s", (int)level, sui_battery_state());
}

static void sui_battery_page_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *card = lv_observer_get_target_obj(observer);
  lv_obj_t *number = lv_obj_get_child(lv_obj_get_child(card, 0), 0);
  lv_obj_t *state = lv_obj_get_child(card, 1);
  lv_obj_t *bar = lv_obj_get_child(card, 2);
  int32_t level = lv_subject_get_int(subj_battery);
  if (level < 0) lv_label_set_text(number, "--");
  else lv_label_set_text_fmt(number, "%d", (int)level);
  lv_label_set_text(state, sui_battery_state());
  lv_bar_set_value(bar, level < 0 ? 0 : level, LV_ANIM_ON);
  uint32_t color = lv_subject_get_int(subj_charging) ? KIT_COLOR_GREEN : level <= 10 ? KIT_COLOR_RED
                                                                        : level <= 20 ? KIT_COLOR_ORANGE
                                                                                      : KIT_COLOR_GREEN;
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
}

static void sui_battery_timer_cb(lv_timer_t *t) {
  lv_obj_t *card = (lv_obj_t *)lv_timer_get_user_data(t);
  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f V", power_battery_voltage());
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(card, 0), 1), buf);
  float usb = power_usb_voltage();
  if (usb > 0.5f) snprintf(buf, sizeof(buf), "%.2f V", usb);
  else strlcpy(buf, "Not connected", sizeof(buf));
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(card, 1), 1), buf);
  snprintf(buf, sizeof(buf), "%.1f \xC2\xB0" "C", power_temperature());
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(card, 2), 1), buf);
}

lv_obj_t *sui_build_battery() {
  lv_obj_t *page = kit_page_create("Battery", true);
  lv_obj_t *c = kit_page_content(page);

  lv_obj_t *hero = kit_col_box(c, LV_FLEX_ALIGN_CENTER, 6);
  lv_obj_set_width(hero, LV_PCT(100));
  lv_obj_set_style_pad_bottom(hero, 8, 0);

  lv_obj_t *line = kit_container(hero);
  lv_obj_set_size(line, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(line, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  kit_label(line, &font_num_110, 0xFFFFFF, "");
  lv_obj_t *pct = kit_label(line, KIT_FONT_TITLE, 0xFFFFFF, "%");
  lv_obj_set_style_pad_bottom(pct, 22, 0);

  kit_label(hero, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");

  lv_obj_t *bar = lv_bar_create(hero);
  lv_obj_set_size(bar, LV_PCT(90), 14);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x39393D), 0);
  lv_obj_set_style_radius(bar, 7, 0);
  lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);

  lv_subject_add_observer_obj(subj_battery, sui_battery_page_obs, hero, NULL);
  lv_subject_add_observer_obj(subj_charging, sui_battery_page_obs, hero, NULL);
  lv_subject_add_observer_obj(subj_usb_power, sui_battery_page_obs, hero, NULL);

  lv_obj_t *card = kit_info_card(c);
  kit_info_row(card, "Voltage", "-");
  kit_info_row(card, "USB input", "-");
  kit_info_row(card, "PMU temperature", "-");
  sui_battery_timer_cb(kit_page_timer(page, sui_battery_timer_cb, 2000, card));

  kit_switch_row(c, NULL, 0, "Battery saver", "Wi-Fi and always-on off, dimmer", subj_saver);
  kit_note(c, "Battery saver turns Wi-Fi and the always-on display off, limits the brightness to 40 % and "
              "lets the screen sleep after 10 s; turning it off restores your settings. Charging stops at "
              "4.2 V. You are warned at 20 % and 10 %. Hold the side button for 6 seconds to force a power off.");
  return page;
}

/* ================================ Units =========================================== */

static const char *const SUI_UNIT_LABELS[] = { "Metric (km, \xC2\xB0" "C)", "Imperial (mi, \xC2\xB0" "F)" };
static const KitChoice SUI_UNIT_CHOICE = { "Units", &subj_units, NULL, SUI_UNIT_LABELS, 2 };

// Metric or imperial: distances, and the temperature unit follows.
void sui_units_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), SUI_UNIT_LABELS[lv_subject_get_int(subject) ? 1 : 0]);
}

lv_obj_t *sui_build_units() {
  return kit_choice_page(&SUI_UNIT_CHOICE);
}

/* ================================ Date & time ===================================== */

static const char *sui_tz_labels[64];
static KitChoice sui_tz_choice = { "Time zone", &subj_timezone, NULL, sui_tz_labels, 0 };

static lv_obj_t *sui_build_timezone() {
  sui_tz_choice.count = min(clock_tz_count(), 64);
  for (int i = 0; i < sui_tz_choice.count; i++) sui_tz_labels[i] = clock_tz_name(i);
  return kit_choice_page(&sui_tz_choice);
}

static void sui_tz_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), clock_tz_name(lv_subject_get_int(subject)));
}

static void sui_auto_time_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  time_t synced = clock_last_sync();
  if (!lv_subject_get_int(subj_auto_time)) {
    lv_label_set_text(label, "Set the time by hand");
  } else if (synced) {
    struct tm t;
    localtime_r(&synced, &t);
    lv_label_set_text_fmt(label, "Synced %02d:%02d, %02d.%02d.", t.tm_hour, t.tm_min, t.tm_mday, t.tm_mon + 1);
  } else {
    lv_label_set_text(label, lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTED ? "Syncing..." : "Wi-Fi or phone");
  }
}

void sui_datetime_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%s  \xE2\x80\xA2  %s", lv_subject_get_string(subj_time_text),
                        clock_tz_name(lv_subject_get_int(subj_timezone)));
}

// ---- Manual date & time (rollers) ----

static char *sui_number_options(char *buf, size_t len, int from, int to, const char *fmt) {
  size_t used = 0;
  buf[0] = 0;
  for (int v = from; v <= to && used < len; v++) used += snprintf(buf + used, len - used, v == from ? fmt : "\n%02d", v);
  return buf;
}

static lv_obj_t *sui_time_hour, *sui_time_minute, *sui_time_day, *sui_time_month, *sui_time_year;

static void sui_time_save_cb(lv_event_t *e) {
  audio_click();
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_hour = lv_roller_get_selected(sui_time_hour);
  t.tm_min = lv_roller_get_selected(sui_time_minute);
  t.tm_mday = lv_roller_get_selected(sui_time_day) + 1;
  t.tm_mon = lv_roller_get_selected(sui_time_month);
  t.tm_year = 2025 + lv_roller_get_selected(sui_time_year) - 1900;
  static const uint8_t days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int year = t.tm_year + 1900;
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  int max_day = days_in_month[t.tm_mon] + (t.tm_mon == 1 && leap);
  if (t.tm_mday > max_day) t.tm_mday = max_day;  // "31 Feb" -> 28/29 Feb
  clock_set_local(&t);
  kit_page_pop();
  kit_toast("Time set");
}

static lv_obj_t *sui_build_set_time() {
  static char hours[24 * 3 + 1], minutes[60 * 3 + 1], days[31 * 3 + 1], years[16 * 5 + 1];
  static const char *months = "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec";
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);

  lv_obj_t *page = kit_page_create("Set time", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *rows[2];
  for (int i = 0; i < 2; i++) {
    kit_section(c, i == 0 ? "TIME" : "DATE");
    rows[i] = kit_row_box(c, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  }

  sui_time_hour = kit_roller(rows[0], sui_number_options(hours, sizeof(hours), 0, 23, "%02d"), t.tm_hour, 130);
  kit_label(rows[0], KIT_FONT_TITLE, 0xFFFFFF, ":");
  sui_time_minute = kit_roller(rows[0], sui_number_options(minutes, sizeof(minutes), 0, 59, "%02d"), t.tm_min, 130);

  int year_index = constrain(t.tm_year + 1900 - 2025, 0, 15);
  size_t used = 0;
  for (int y = 2025; y <= 2040; y++) used += snprintf(years + used, sizeof(years) - used, y == 2025 ? "%d" : "\n%d", y);
  sui_time_day = kit_roller(rows[1], sui_number_options(days, sizeof(days), 1, 31, "%02d"), t.tm_mday - 1, 90);
  sui_time_month = kit_roller(rows[1], months, t.tm_mon, 100);
  sui_time_year = kit_roller(rows[1], years, year_index, 120);

  lv_obj_t *save = kit_button(c, "Save", KIT_COLOR_ACCENT);
  lv_obj_add_event_cb(save, sui_time_save_cb, LV_EVENT_CLICKED, NULL);
  return page;
}

lv_obj_t *sui_build_datetime() {
  lv_obj_t *page = kit_page_create("Date & time", true);
  lv_obj_t *c = kit_page_content(page);

  lv_obj_t *hero = kit_col_box(c, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_width(hero, LV_PCT(100));
  lv_obj_set_style_pad_bottom(hero, 8, 0);
  lv_obj_t *time_label = kit_label(hero, &font_num_64, 0xFFFFFF, "");
  lv_label_bind_text(time_label, subj_time_text, NULL);
  lv_obj_t *date_label = kit_label(hero, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  lv_label_bind_text(date_label, subj_date_text, NULL);

  lv_obj_t *automatic = kit_switch_row(c, NULL, 0, "Automatic", "", subj_auto_time);
  lv_obj_t *auto_sub = kit_row_subtitle(automatic);
  lv_subject_add_observer_obj(subj_auto_time, sui_auto_time_obs, auto_sub, NULL);
  lv_subject_add_observer_obj(subj_wifi_state, sui_auto_time_obs, auto_sub, NULL);
  lv_subject_add_observer_obj(subj_time_text, sui_auto_time_obs, auto_sub, NULL);

  lv_obj_t *tz = sui_nav_row(c, NULL, 0, "Time zone", sui_build_timezone);
  lv_subject_add_observer_obj(subj_timezone, sui_tz_obs, kit_row_subtitle(tz), NULL);
  kit_switch_row(c, NULL, 0, "24-hour time", NULL, subj_time_24h);
  lv_obj_t *manual = sui_nav_row(c, NULL, 0, "Set date & time", sui_build_set_time);
  lv_obj_bind_bool(manual, subj_auto_time, lv_obj_set_disabled);  // manual setting only without NTP
  return page;
}
