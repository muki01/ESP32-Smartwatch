/*
 * alarms.h - Alarm clock: up to ALARM_MAX alarms with a time, repeat days (none = once)
 * and an on/off switch, sorted by time, stored in NVS. The next one is published as
 * subj_alarm_next. When one is due the ring handler (the UI's alert) is called.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define ALARM_MAX         6
#define ALARM_SNOOZE_MIN  5

struct Alarm {
  uint8_t hour;
  uint8_t minute;
  uint8_t days;     // bit 0 = Monday ... bit 6 = Sunday, 0 = once
  uint8_t enabled;
};

typedef void (*alarm_ring_cb_t)(int hour, int minute, bool snoozed);

void alarms_init();
void alarms_set_ring_handler(alarm_ring_cb_t cb);
int alarms_count();
const Alarm *alarms_get(int index);
void alarms_put(int index, const Alarm *a);  // index < 0: add a new one
void alarms_remove(int index);
void alarms_enable(int index, bool on);
void alarms_snooze();                         // ring again in ALARM_SNOOZE_MIN minutes
int alarms_minutes_until(const Alarm *a);     // from now, -1 when it does not ring today or tomorrow
void alarms_days_text(uint8_t days, char *buf, size_t len);  // "Weekdays", "Mon, Wed", "Once"
