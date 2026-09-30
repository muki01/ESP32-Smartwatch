/*
 * settings.cpp - Application state as LVGL subjects, persisted in NVS.
 *
 * UI widgets bind to the subjects; services observe them. A change of a persisted value
 * marks it dirty and one debounced write (1.5 s after the last change) stores it, so
 * dragging a slider does not wear the flash.
 */
#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "system.h"

#define SETTINGS_NS       "watch"
#define SETTINGS_SAVE_MS  1500

lv_subject_t *subj_brightness;
lv_subject_t *subj_screen_timeout;
lv_subject_t *subj_tap_to_wake;
lv_subject_t *subj_raise_wake;
lv_subject_t *subj_aod;
lv_subject_t *subj_watchface;
lv_subject_t *subj_face_color;
lv_subject_t *subj_face_texture;
lv_subject_t *subj_perf_overlay;
lv_subject_t *subj_volume;
lv_subject_t *subj_touch_sounds;
lv_subject_t *subj_silent;
lv_subject_t *subj_dnd;
lv_subject_t *subj_notif_sounds;
lv_subject_t *subj_notif_wake;
lv_subject_t *subj_wifi_enabled;
lv_subject_t *subj_bt_enabled;
lv_subject_t *subj_gadgetbridge;
lv_subject_t *subj_ota;
lv_subject_t *subj_time_24h;
lv_subject_t *subj_auto_time;
lv_subject_t *subj_timezone;
lv_subject_t *subj_units;
lv_subject_t *subj_temp_unit;
lv_subject_t *subj_step_goal;
lv_subject_t *subj_move_remind;
lv_subject_t *subj_saver;
lv_subject_t *subj_bedtime;
lv_subject_t *subj_bed_from;
lv_subject_t *subj_bed_to;
lv_subject_t *subj_bedtime_on;
lv_subject_t *subj_ext_car;
lv_subject_t *subj_ext_lights;
lv_subject_t *subj_ext_clap;
lv_subject_t *subj_wifi_state;
lv_subject_t *subj_wifi_scan;
lv_subject_t *subj_bt_state;
lv_subject_t *subj_battery;
lv_subject_t *subj_charging;
lv_subject_t *subj_usb_power;
lv_subject_t *subj_time_text;
lv_subject_t *subj_date_text;
lv_subject_t *subj_steps;
lv_subject_t *subj_weather;
lv_subject_t *subj_notif_count;
lv_subject_t *subj_notif_unread;
lv_subject_t *subj_alarm_next;
lv_subject_t *subj_phone;
lv_subject_t *subj_music;
lv_subject_t *subj_sleep;
lv_subject_t *subj_workout;
lv_subject_t *subj_agenda;
lv_subject_t *subj_recorder;
lv_subject_t *subj_lights;
lv_subject_t *subj_car;

struct PersistedSetting {
  lv_subject_t **subject;
  const char *key;
  int32_t def;
  int32_t min;
  int32_t max;
  int32_t stored;
};

static PersistedSetting persisted[] = {
  { &subj_brightness,     "bright",   80,   5,    100,   0 },
  { &subj_screen_timeout, "timeout",  15,   5,    600,   0 },
  { &subj_tap_to_wake,    "tapwake",  1,    0,    1,     0 },
  { &subj_raise_wake,     "raise",    1,    0,    1,     0 },
  { &subj_aod,            "aod",      0,    0,    1,     0 },
  { &subj_watchface,      "face",     0,    0,    FACE_COUNT - 1, 0 },
  { &subj_face_color,     "fcolor",   0,    0,    FACE_COLOR_COUNT - 1, 0 },
  { &subj_face_texture,   "ftex",     1,    0,    1,     0 },
  { &subj_perf_overlay,   "perf",     0,    0,    1,     0 },
  { &subj_volume,         "volume",   70,   0,    100,   0 },
  { &subj_touch_sounds,   "tsound",   1,    0,    1,     0 },
  { &subj_silent,         "silent",   0,    0,    1,     0 },
  { &subj_dnd,            "dnd",      0,    0,    1,     0 },
  { &subj_notif_sounds,   "nsound",   1,    0,    1,     0 },
  { &subj_notif_wake,     "nwake",    1,    0,    1,     0 },
  { &subj_wifi_enabled,   "wifi",     0,    0,    1,     0 },
  { &subj_bt_enabled,     "bt",       0,    0,    1,     0 },
  { &subj_gadgetbridge,   "gb",       0,    0,    1,     0 },
  { &subj_time_24h,       "t24h",     1,    0,    1,     0 },
  { &subj_auto_time,      "autotime", 1,    0,    1,     0 },
  { &subj_timezone,       "tz",       3,    0,    255,   0 },
  { &subj_units,          "units",    0,    0,    1,     0 },
  { &subj_temp_unit,      "tunit",    0,    0,    1,     0 },
  { &subj_step_goal,      "goal",     8000, 1000, 50000, 0 },
  { &subj_move_remind,    "move",     1,    0,    1,     0 },
  { &subj_saver,          "saver",    0,    0,    1,     0 },
  { &subj_bedtime,        "bedtime",  0,    0,    1,     0 },
  { &subj_bed_from,       "bedfrom",  1350, 0,    1439,  0 },
  { &subj_bed_to,         "bedto",    420,  0,    1439,  0 },
  { &subj_ext_car,        "xcar",     0,    0,    1,     0 },
  { &subj_ext_lights,     "xlights",  0,    0,    1,     0 },
  { &subj_ext_clap,       "xclap",    0,    0,    1,     0 },
};

static char time_text_buf[12], time_text_prev[12];
static char date_text_buf[24], date_text_prev[24];
static Preferences prefs;
static lv_timer_t *settings_save_timer;
static bool settings_ready;

void subj_set(lv_subject_t *subject, int32_t value) {
  if (lv_subject_get_int(subject) != value) lv_subject_set_int(subject, value);
}

// For "something changed" counters: observers redraw, the value itself means nothing.
void subj_bump(lv_subject_t *subject) {
  lv_subject_set_int(subject, lv_subject_get_int(subject) + 1);
}

void settings_flush() {
  if (!settings_ready) return;
  lv_timer_pause(settings_save_timer);
  bool open = false;
  for (PersistedSetting &p : persisted) {
    int32_t value = lv_subject_get_int(*p.subject);
    if (value == p.stored) continue;
    if (!open) open = prefs.begin(SETTINGS_NS, false);
    if (open && prefs.putInt(p.key, value)) p.stored = value;
  }
  if (open) prefs.end();
}

static void settings_save_cb(lv_timer_t *t) {
  settings_flush();
}

static void settings_changed_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (!settings_ready) return;  // the initial notification on registration is not a change
  lv_timer_reset(settings_save_timer);
  lv_timer_resume(settings_save_timer);
}

// Metric / imperial: the temperature unit follows when the units are changed.
static void settings_units_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (settings_ready) subj_set(subj_temp_unit, lv_subject_get_int(subject) ? 1 : 0);
}

static lv_subject_t *settings_int_subject(int32_t value) {
  lv_subject_t *subject = lv_subject_create(LV_SUBJECT_TYPE_INT);
  lv_subject_set_int(subject, value);
  return subject;
}

static lv_subject_t *settings_string_subject(char *buf, char *prev, size_t size, const char *value) {
  lv_subject_t *subject = lv_subject_create(LV_SUBJECT_TYPE_STRING);
  lv_subject_set_string_buffer_static(subject, buf, prev, size);
  lv_subject_set_string(subject, value);
  return subject;
}

void settings_init() {
  prefs.begin(SETTINGS_NS, true);
  for (PersistedSetting &p : persisted) {
    int32_t value = constrain(prefs.getInt(p.key, p.def), p.min, p.max);
    p.stored = value;
    *p.subject = settings_int_subject(value);
    lv_subject_set_min_value_int(*p.subject, p.min);
    lv_subject_set_max_value_int(*p.subject, p.max);
  }
  prefs.end();

  subj_ota = settings_int_subject(0);
  subj_bedtime_on = settings_int_subject(0);
  subj_wifi_state = settings_int_subject(WIFI_ST_OFF);
  subj_wifi_scan = settings_int_subject(0);
  subj_bt_state = settings_int_subject(BT_ST_OFF);
  subj_battery = settings_int_subject(-1);
  subj_charging = settings_int_subject(0);
  subj_usb_power = settings_int_subject(0);
  subj_time_text = settings_string_subject(time_text_buf, time_text_prev, sizeof(time_text_buf), "--:--");
  subj_date_text = settings_string_subject(date_text_buf, date_text_prev, sizeof(date_text_buf), "");
  subj_steps = settings_int_subject(0);
  subj_weather = settings_int_subject(0);
  subj_notif_count = settings_int_subject(0);
  subj_notif_unread = settings_int_subject(0);
  subj_alarm_next = settings_int_subject(-1);
  subj_phone = settings_int_subject(0);
  subj_music = settings_int_subject(0);
  subj_sleep = settings_int_subject(0);
  subj_workout = settings_int_subject(WORKOUT_NONE);
  subj_agenda = settings_int_subject(0);
  subj_recorder = settings_int_subject(REC_IDLE);
  subj_lights = settings_int_subject(0);
  subj_car = settings_int_subject(0);

  settings_save_timer = lv_timer_create(settings_save_cb, SETTINGS_SAVE_MS, NULL);
  lv_timer_pause(settings_save_timer);
  for (PersistedSetting &p : persisted) lv_subject_add_observer(*p.subject, settings_changed_obs, NULL);
  lv_subject_add_observer(subj_units, settings_units_obs, NULL);
  settings_ready = true;
}

// Erases the whole NVS partition: settings, alarms, history, saved networks and devices.
void settings_factory_reset() {
  static const char *const NAMESPACES[] = { SETTINGS_NS, "wifi", "alarms", "activity", "weather", "sleep",
                                            "workouts", "imu", "agenda", "saver", "wled", "car" };
  settings_ready = false;  // nothing may be written back before the restart
  Serial.println("[SET] factory reset");
  if (nvs_flash_erase() != ESP_OK) {  // partition busy: clear every namespace of the firmware instead
    for (const char *ns : NAMESPACES) {
      if (!prefs.begin(ns, false)) continue;
      prefs.clear();
      prefs.end();
    }
  }
  delay(100);
  ESP.restart();
}
