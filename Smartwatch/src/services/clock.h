/*
 * clock.h - Time keeping: RTC, system clock, NTP, time zones, the time/date text
 * subjects and a once-a-minute callback for the services that work by the clock.
 */
#pragma once

#include <stddef.h>
#include <time.h>

typedef void (*clock_minute_cb_t)(const struct tm *local);

void clock_init();
void clock_add_minute_handler(clock_minute_cb_t cb);  // every new minute exactly once (valid clock only)

bool clock_is_valid();
time_t clock_last_sync();
int clock_tz_count();
const char *clock_tz_name(int index);

void clock_set_local(const struct tm *local);       // set by hand (Settings)
void clock_apply_external(const struct tm *local);  // BLE Current Time Service
void clock_set_utc(time_t utc);                     // Gadgetbridge
void clock_apply_utc_offset(int offset_min);        // phone time zone: pick a matching zone

void clock_format_hm(int hour, int minute, char *buf, size_t len);  // "07:30" / "7:30 AM"
