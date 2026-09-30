/*
 * sleep_tracker.h - Sleep estimated from wrist movement (the last 7 nights) and bedtime
 * mode: do not disturb, no wake by tap or raise, no always-on display - on a schedule or
 * switched by hand (subj_bedtime_on).
 */
#pragma once

#include <stdint.h>

#define SLEEP_NIGHTS    7
#define SLEEP_MAX_BINS  216   // 18 h of 5-minute bins

enum SleepClass : uint8_t { SLEEP_CALM_C, SLEEP_RESTLESS_C, SLEEP_AWAKE_C, SLEEP_UNKNOWN_C };

struct SleepNight {
  uint32_t start;            // epoch s, fell asleep
  uint32_t end;              // epoch s, woke up
  uint16_t calm_min;
  uint16_t restless_min;
  uint16_t awake_min;
  uint16_t reserved;
};

void sleep_tracker_init();
int sleep_night_count();
const SleepNight *sleep_night(int index);     // 0 = the most recent night
const uint8_t *sleep_last_bins(uint16_t *n);  // hypnogram of night 0 (5-minute bins)

// The night so far (from 18:00 on). Fills *night and the bins; false when there is none.
// *not_worn tells that the watch lay somewhere instead of being worn.
bool sleep_analyze_now(SleepNight *night, uint8_t *bins, uint16_t *bins_n, bool *not_worn);

void bedtime_set(bool on);                    // by hand; the schedule changes it at its next boundary
