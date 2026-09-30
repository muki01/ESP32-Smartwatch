/*
 * alarms.cpp - Alarm clock storage and schedule. The clock hands every new minute to
 * alarms_on_minute() exactly once; a one-time alarm switches itself off when it rings.
 */
#include "alarms.h"

#include <Arduino.h>
#include <Preferences.h>
#include "../core/settings.h"
#include "clock.h"

#define ALARM_NS "alarms"

static Alarm alarms[ALARM_MAX];
static int alarm_n;
static time_t alarm_snooze_at;  // 0 = not snoozed
static alarm_ring_cb_t alarm_ring_handler;

static const char *const ALARM_DAY_SHORT[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

static void alarms_save() {
  Preferences p;
  if (!p.begin(ALARM_NS, false)) return;
  p.putUChar("n", (uint8_t)alarm_n);
  p.putBytes("list", alarms, sizeof(Alarm) * alarm_n);
  p.end();
}

static void alarms_load() {
  Preferences p;
  if (!p.begin(ALARM_NS, true)) return;
  alarm_n = min((int)p.getUChar("n", 0), ALARM_MAX);
  if (alarm_n && p.getBytes("list", alarms, sizeof(Alarm) * alarm_n) != sizeof(Alarm) * alarm_n) alarm_n = 0;
  p.end();
}

static void alarms_sort() {
  for (int i = 1; i < alarm_n; i++) {
    Alarm key = alarms[i];
    int j = i - 1;
    for (; j >= 0 && alarms[j].hour * 60 + alarms[j].minute > key.hour * 60 + key.minute; j--) alarms[j + 1] = alarms[j];
    alarms[j + 1] = key;
  }
}

static int alarm_day_bit(int tm_wday) {
  return 1 << ((tm_wday + 6) % 7);  // tm: 0 = Sunday; ours: bit 0 = Monday
}

// Next alarm (or snooze) as minutes after midnight for the faces; -1 = none within a week.
static void alarms_update_next() {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  int now_min = t.tm_hour * 60 + t.tm_min;
  int best = INT32_MAX, best_hm = -1;
  if (alarm_snooze_at) {
    struct tm s;
    localtime_r(&alarm_snooze_at, &s);
    best = (int)((alarm_snooze_at - now) / 60);
    best_hm = s.tm_hour * 60 + s.tm_min;
  }
  for (int i = 0; i < alarm_n; i++) {
    const Alarm &a = alarms[i];
    if (!a.enabled) continue;
    int hm = a.hour * 60 + a.minute;
    for (int d = 0; d < 8; d++) {
      int wday = (t.tm_wday + d) % 7;
      int delta = d * 1440 + hm - now_min;
      if (delta <= 0) continue;
      if (a.days && !(a.days & alarm_day_bit(wday))) continue;
      if (delta < best) {
        best = delta;
        best_hm = hm;
      }
      break;
    }
  }
  subj_set(subj_alarm_next, best_hm);
}

static void alarms_ring(int hour, int minute, bool snoozed) {
  if (alarm_ring_handler) alarm_ring_handler(hour, minute, snoozed);
}

static void alarms_on_minute(const struct tm *t) {
  time_t now = time(NULL);
  if (alarm_snooze_at && now >= alarm_snooze_at) {
    alarm_snooze_at = 0;
    alarms_ring(t->tm_hour, t->tm_min, true);
  } else {
    for (int i = 0; i < alarm_n; i++) {
      Alarm &a = alarms[i];
      if (!a.enabled || a.hour != t->tm_hour || a.minute != t->tm_min) continue;
      if (a.days && !(a.days & alarm_day_bit(t->tm_wday))) continue;
      if (!a.days) {  // one-time alarm: switch it off
        a.enabled = 0;
        alarms_save();
      }
      alarms_ring(a.hour, a.minute, false);
      break;
    }
  }
  alarms_update_next();
}

/* ================================ Public API ====================================== */

void alarms_init() {
  alarms_load();
  alarms_sort();
  alarms_update_next();
  clock_add_minute_handler(alarms_on_minute);
}

void alarms_set_ring_handler(alarm_ring_cb_t cb) {
  alarm_ring_handler = cb;
}

int alarms_count() {
  return alarm_n;
}

const Alarm *alarms_get(int index) {
  return index >= 0 && index < alarm_n ? &alarms[index] : NULL;
}

void alarms_put(int index, const Alarm *a) {
  if (index >= 0 && index < alarm_n) alarms[index] = *a;
  else if (alarm_n < ALARM_MAX) alarms[alarm_n++] = *a;
  alarms_sort();
  alarms_save();
  alarms_update_next();
}

void alarms_remove(int index) {
  if (index < 0 || index >= alarm_n) return;
  for (int i = index; i < alarm_n - 1; i++) alarms[i] = alarms[i + 1];
  alarm_n--;
  alarms_save();
  alarms_update_next();
}

void alarms_enable(int index, bool on) {
  if (index < 0 || index >= alarm_n) return;
  alarms[index].enabled = on;
  alarms_save();
  alarms_update_next();
}

void alarms_snooze() {
  alarm_snooze_at = (time(NULL) / 60 + ALARM_SNOOZE_MIN) * 60;
  alarms_update_next();
}

int alarms_minutes_until(const Alarm *a) {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  int delta = (a->hour * 60 + a->minute - (t.tm_hour * 60 + t.tm_min) + 1440) % 1440;
  if (!delta) delta = 1440;
  bool today_or_once = a->days == 0 || (a->days & alarm_day_bit(t.tm_wday));
  return today_or_once ? delta : -1;
}

void alarms_days_text(uint8_t days, char *buf, size_t len) {
  if (days == 0) strlcpy(buf, "Once", len);
  else if (days == 0x7F) strlcpy(buf, "Every day", len);
  else if (days == 0x1F) strlcpy(buf, "Weekdays", len);
  else if (days == 0x60) strlcpy(buf, "Weekends", len);
  else {
    buf[0] = 0;
    for (int i = 0; i < 7; i++) {
      if (!(days & (1 << i))) continue;
      if (buf[0]) strlcat(buf, ", ", len);
      strlcat(buf, ALARM_DAY_SHORT[i], len);
    }
  }
}
