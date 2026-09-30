/*
 * clock.cpp - Time keeping: PCF85063 RTC, system clock, NTP, time zone.
 *
 * The RTC keeps UTC. At boot it sets the system clock; NTP (Wi-Fi) and the phone (BLE
 * Current Time Service or Gadgetbridge) correct both. The time and date text subjects
 * change once a minute and the watch faces follow them. Every new minute also goes to
 * the registered minute handlers (alarms, bedtime and sleep, move reminder) - exactly
 * once, whatever redraws, settings changes or clock corrections happen around it.
 */
#include "clock.h"

#include <Arduino.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "../core/settings.h"
#include "../drivers/rtc.h"

#define CLOCK_MIN_EPOCH    1735689600  // 2025-01-01: anything older means "not set"
#define CLOCK_HANDLERS_MAX 8

struct TimeZone {
  const char *name;
  const char *posix;
};

static const TimeZone TIMEZONES[] = {
  { "UTC",                    "UTC0" },
  { "London, Lisbon",         "GMT0BST,M3.5.0/1,M10.5.0" },
  { "Berlin, Paris, Rome",    "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Sofia, Athens, Kyiv",    "EET-2EEST,M3.5.0/3,M10.5.0/4" },
  { "Istanbul",               "<+03>-3" },
  { "Moscow",                 "MSK-3" },
  { "Dubai",                  "<+04>-4" },
  { "Baku, Tbilisi",          "<+04>-4" },
  { "Tashkent, Karachi",      "<+05>-5" },
  { "New Delhi",              "IST-5:30" },
  { "Almaty, Dhaka",          "<+06>-6" },
  { "Bangkok, Jakarta",       "<+07>-7" },
  { "Beijing, Singapore",     "CST-8" },
  { "Tokyo, Seoul",           "JST-9" },
  { "Sydney",                 "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { "Auckland",               "NZST-12NZDT,M9.5.0,M4.1.0/3" },
  { "Azores",                 "<-01>1<+00>,M3.5.0/0,M10.5.0/1" },
  { "Sao Paulo",              "<-03>3" },
  { "New York, Toronto",      "EST5EDT,M3.2.0,M11.1.0" },
  { "Chicago, Mexico City",   "CST6CDT,M3.2.0,M11.1.0" },
  { "Denver",                 "MST7MDT,M3.2.0,M11.1.0" },
  { "Los Angeles",            "PST8PDT,M3.2.0,M11.1.0" },
  { "Anchorage",              "AKST9AKDT,M3.2.0,M11.1.0" },
  { "Honolulu",               "HST10" },
};
static const int TIMEZONE_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

static const char *const WEEKDAYS[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static bool clock_valid;
static time_t clock_synced_at;
static volatile bool clock_ntp_synced;
static time_t clock_last_sec;
static int clock_last_min = -1;
static int clock_last_mday = -1;
static int64_t clock_handled_minute = -1;  // last minute (epoch / 60) given to the handlers
static clock_minute_cb_t clock_handlers[CLOCK_HANDLERS_MAX];
static int clock_handler_n;

/* ================================ System time ===================================== */

static void clock_set_epoch(time_t epoch, bool synced) {
  struct timeval tv = { epoch, 0 };
  settimeofday(&tv, NULL);
  rtc_write(epoch);
  clock_valid = true;
  if (synced) clock_synced_at = epoch;
  clock_last_min = -1;  // redraw everything on the next tick
  clock_last_mday = -1;
  // Set by hand: start over from this minute. Corrected backwards: never replay minutes.
  int64_t minute = (int64_t)epoch / 60;
  if (!synced || minute < clock_handled_minute) clock_handled_minute = minute;
}

static void clock_apply_tz() {
  setenv("TZ", TIMEZONES[lv_subject_get_int(subj_timezone)].posix, 1);
  tzset();
  clock_last_min = -1;
  clock_last_mday = -1;
}

static void clock_ntp_sync_cb(struct timeval *tv) {
  clock_ntp_synced = true;  // SNTP task: just flag it, the LVGL timer does the rest
}

static void clock_ntp_update() {
  bool want = lv_subject_get_int(subj_auto_time) && lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTED;
  if (want) {
    sntp_set_time_sync_notification_cb(clock_ntp_sync_cb);
    configTzTime(TIMEZONES[lv_subject_get_int(subj_timezone)].posix, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  } else if (esp_sntp_enabled()) {
    esp_sntp_stop();  // esp_sntp_* calls are thread safe
  }
}

/* ================================ Time and date text =============================== */

void clock_format_hm(int hour, int minute, char *buf, size_t len) {
  if (lv_subject_get_int(subj_time_24h)) {
    snprintf(buf, len, "%02d:%02d", hour, minute);
  } else {
    int h = hour % 12;
    snprintf(buf, len, "%d:%02d %s", h ? h : 12, minute, hour < 12 ? "AM" : "PM");
  }
}

static void clock_update_minute(const struct tm &t) {
  char buf[24];
  clock_format_hm(t.tm_hour, t.tm_min, buf, sizeof(buf));
  lv_subject_set_string(subj_time_text, buf);
}

static void clock_update_date(const struct tm &t) {
  char text[24];
  snprintf(text, sizeof(text), "%s, %d %s", WEEKDAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
  lv_subject_set_string(subj_date_text, text);
}

static void clock_tick_cb(lv_timer_t *timer) {
  if (clock_ntp_synced) {
    clock_ntp_synced = false;
    time_t now = time(NULL);
    rtc_write(now);
    clock_valid = true;
    clock_synced_at = now;
    clock_last_min = -1;
    Serial.println("[CLOCK] NTP synchronized");
  }

  time_t now = time(NULL);
  if (now == clock_last_sec) return;
  clock_last_sec = now;
  struct tm t;
  localtime_r(&now, &t);

  if (t.tm_mday != clock_last_mday) {  // before the minute: faces redraw the date with it
    clock_last_mday = t.tm_mday;
    clock_update_date(t);
  }
  if (t.tm_min != clock_last_min) {
    clock_last_min = t.tm_min;
    clock_update_minute(t);
  }

  int64_t minute = (int64_t)now / 60;
  if (!clock_valid) return;
  if (clock_handled_minute < 0) {
    clock_handled_minute = minute;  // first valid minute after boot
  } else if (minute > clock_handled_minute) {
    clock_handled_minute = minute;
    for (int i = 0; i < clock_handler_n; i++) clock_handlers[i](&t);
  }
}

static void clock_settings_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (subject == subj_timezone) clock_apply_tz();
  clock_last_sec = 0;  // redraw on the next tick
  clock_last_min = -1;
  clock_last_mday = -1;
  if (subject != subj_time_24h) clock_ntp_update();
}

/* ================================ Public API ====================================== */

void clock_init() {
  // The zone index is persisted; keep it inside the table.
  lv_subject_set_max_value_int(subj_timezone, TIMEZONE_COUNT - 1);
  lv_subject_set_int(subj_timezone, lv_subject_get_int(subj_timezone));

  bool rtc_ok = rtc_init();
  time_t utc;
  if (rtc_ok && rtc_read(&utc) && utc >= CLOCK_MIN_EPOCH) {
    struct timeval tv = { utc, 0 };
    settimeofday(&tv, NULL);
    clock_valid = true;
  }
  Serial.printf("[CLOCK] RTC %s, time %s\n", rtc_ok ? "ready" : "NOT FOUND", clock_valid ? "valid" : "not set");

  clock_apply_tz();
  lv_subject_add_observer(subj_timezone, clock_settings_obs, NULL);
  lv_subject_add_observer(subj_time_24h, clock_settings_obs, NULL);
  lv_subject_add_observer(subj_auto_time, clock_settings_obs, NULL);
  lv_subject_add_observer(subj_wifi_state, clock_settings_obs, NULL);
  lv_timer_create(clock_tick_cb, 200, NULL);
}

void clock_add_minute_handler(clock_minute_cb_t cb) {
  if (cb && clock_handler_n < CLOCK_HANDLERS_MAX) clock_handlers[clock_handler_n++] = cb;
}

bool clock_is_valid() {
  return clock_valid;
}

time_t clock_last_sync() {
  return clock_synced_at;
}

int clock_tz_count() {
  return TIMEZONE_COUNT;
}

const char *clock_tz_name(int index) {
  return index >= 0 && index < TIMEZONE_COUNT ? TIMEZONES[index].name : "";
}

void clock_set_local(const struct tm *local) {
  struct tm t = *local;
  t.tm_isdst = -1;
  clock_set_epoch(mktime(&t), false);
  Serial.println("[CLOCK] set manually");
}

void clock_apply_external(const struct tm *local) {
  if (!lv_subject_get_int(subj_auto_time)) return;
  struct tm t = *local;
  t.tm_isdst = -1;
  clock_set_epoch(mktime(&t), true);
  Serial.println("[CLOCK] set by phone");
}

// Minutes east of UTC right now, for the zone currently set with tzset().
static int clock_current_offset_min() {
  time_t now = time(NULL);
  struct tm local, utc;
  localtime_r(&now, &local);
  gmtime_r(&now, &utc);
  int day = local.tm_yday - utc.tm_yday;
  if (day > 1) day = -1;  // year boundary
  if (day < -1) day = 1;
  return day * 1440 + (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
}

// The phone reports its UTC offset: if ours differs, switch to the first zone of the
// table that has that offset right now (summer time included).
void clock_apply_utc_offset(int offset_min) {
  if (!lv_subject_get_int(subj_auto_time) || !clock_valid) return;
  if (clock_current_offset_min() == offset_min) return;
  int current = lv_subject_get_int(subj_timezone);
  int match = -1;
  for (int i = 0; i < TIMEZONE_COUNT && match < 0; i++) {
    setenv("TZ", TIMEZONES[i].posix, 1);
    tzset();
    if (clock_current_offset_min() == offset_min) match = i;
  }
  setenv("TZ", TIMEZONES[current].posix, 1);
  tzset();
  if (match >= 0) {
    Serial.printf("[CLOCK] time zone from phone: %s\n", TIMEZONES[match].name);
    lv_subject_set_int(subj_timezone, match);
  }
}

void clock_set_utc(time_t utc) {
  if (!lv_subject_get_int(subj_auto_time) || utc < CLOCK_MIN_EPOCH) return;
  if (clock_valid && llabs((long long)(utc - time(NULL))) < 2) {
    clock_synced_at = utc;  // already right: just note the sync
    return;
  }
  clock_set_epoch(utc, true);
  Serial.println("[CLOCK] set by phone (UTC)");
}
