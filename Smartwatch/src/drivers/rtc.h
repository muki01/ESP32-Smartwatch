/*
 * rtc.h - PCF85063 real-time clock. Keeps UTC while the watch is off.
 */
#pragma once

#include <time.h>

bool rtc_init();                 // true when the chip answers
bool rtc_read(time_t *utc);      // false when the chip lost power (time is not valid)
void rtc_write(time_t utc);
