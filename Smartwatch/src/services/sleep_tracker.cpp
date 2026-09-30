/*
 * sleep_tracker.cpp - Sleep from wrist movement, and bedtime mode.
 *
 * imu.cpp sums the wrist movement of every minute. Each morning the night is scored
 * minute by minute with a weighted window over the neighbouring minutes (the method of
 * wrist actigraphy), the longest stretch of sleep (short awakenings included) is taken
 * as the night and its minutes are classed calm, restless or awake. A night with hours of
 * no movement at all is not counted: the watch was not worn. Without heart rate there are
 * no sleep stages; the app says the result is an estimate.
 *
 * Bedtime mode follows its schedule at the schedule's start and end; switched by hand it
 * stays until switched back or the next scheduled change.
 */
#include "sleep_tracker.h"

#include <Arduino.h>
#include <Preferences.h>
#include "../assets/fonts/icons.h"
#include "../core/format.h"
#include "../core/settings.h"
#include "../core/system.h"
#include "../drivers/imu.h"
#include "clock.h"
#include "notifications.h"

#define SLEEP_NS            "sleep"
#define SLEEP_BIN_MIN       5          // hypnogram resolution
#define SLEEP_CALM          60         // weighted movement below this: calm sleep
#define SLEEP_WAKE          300        // above this: awake
#define SLEEP_GAP_MIN       15         // awake stretches this short stay inside the night
#define SLEEP_MIN_NIGHT     90         // shortest stretch that counts as a night (min)
#define SLEEP_NOT_WORN_MOVE 40         // raw movement below this for ...
#define SLEEP_NOT_WORN_MIN  180        // ... this long: the watch was lying somewhere
#define SLEEP_DONE_AFTER    30         // awake this long after the night: it is over (min)
#define SLEEP_INDIGO        0x5E5CE6

static SleepNight sleep_nights[SLEEP_NIGHTS];  // [0] = the most recent night
static int sleep_nights_n;
static uint8_t sleep_bins[SLEEP_MAX_BINS];     // hypnogram of sleep_nights[0]
static uint16_t sleep_bins_n;
static int32_t sleep_done_key = -1;            // morning (year * 1000 + day) already analysed
static int32_t bedtime_prev_dnd = -1;          // do not disturb before bedtime mode switched it on
static int bedtime_window_prev = -1;           // schedule window at the last check, -1 = unknown

/* ================================ Storage ========================================= */

static void sleep_load() {
  Preferences p;
  if (!p.begin(SLEEP_NS, true)) return;
  sleep_nights_n = (int)(p.getBytes("nights", sleep_nights, sizeof(sleep_nights)) / sizeof(SleepNight));
  sleep_bins_n = (uint16_t)p.getBytes("bins", sleep_bins, sizeof(sleep_bins));
  sleep_done_key = p.getInt("done", -1);
  p.end();
}

static void sleep_store_night(const SleepNight &night, const uint8_t *bins, uint16_t bins_n) {
  if (sleep_nights_n == SLEEP_NIGHTS) sleep_nights_n--;
  memmove(&sleep_nights[1], &sleep_nights[0], sleep_nights_n * sizeof(SleepNight));
  sleep_nights[0] = night;
  sleep_nights_n++;
  memcpy(sleep_bins, bins, bins_n);
  sleep_bins_n = bins_n;
  Preferences p;
  if (p.begin(SLEEP_NS, false)) {
    p.putBytes("nights", sleep_nights, sleep_nights_n * sizeof(SleepNight));
    p.putBytes("bins", sleep_bins, sleep_bins_n);
    p.end();
  }
}

static void sleep_mark_done(int32_t key) {
  sleep_done_key = key;
  Preferences p;
  if (p.begin(SLEEP_NS, false)) {
    p.putInt("done", key);
    p.end();
  }
}

/* ================================ Analysis ======================================== */

// Movement of minute m, weighted with its neighbours (-4..+2 minutes), -1 = no data.
static int32_t sleep_score(int32_t m) {
  static const int16_t W[7] = { 106, 54, 58, 76, 230, 74, 67 };
  int32_t sum = 0, wsum = 0;
  if (imu_movement(m) < 0) return -1;
  for (int i = 0; i < 7; i++) {
    int32_t v = imu_movement(m + i - 4);
    if (v < 0) continue;
    sum += W[i] * v;
    wsum += W[i];
  }
  return wsum ? sum / wsum : -1;
}

// Finds the night between `from` and `to` (epoch s).
static bool sleep_analyze(time_t from, time_t to, SleepNight *night, uint8_t *bins, uint16_t *bins_n, bool *not_worn) {
  *not_worn = false;
  *bins_n = 0;
  int32_t first = (int32_t)(from / 60), n = (int32_t)((to - from) / 60);
  if (n <= 0) return false;
  if (n > SLEEP_MAX_BINS * SLEEP_BIN_MIN) {
    first += n - SLEEP_MAX_BINS * SLEEP_BIN_MIN;
    n = SLEEP_MAX_BINS * SLEEP_BIN_MIN;
  }
  uint8_t *cls = (uint8_t *)psram_malloc(n);
  if (!cls) return false;
  for (int32_t i = 0; i < n; i++) {
    int32_t s = sleep_score(first + i);
    cls[i] = s < 0 ? SLEEP_UNKNOWN_C : s < SLEEP_CALM ? SLEEP_CALM_C : s < SLEEP_WAKE ? SLEEP_RESTLESS_C : SLEEP_AWAKE_C;
  }
  // The longest stretch of sleep, short awakenings bridged.
  int32_t best_a = -1, best_b = -1, run_a = -1, last_sleep = -1;
  for (int32_t i = 0; i <= n; i++) {
    bool asleep = i < n && cls[i] <= SLEEP_RESTLESS_C;
    if (asleep) {
      if (run_a < 0) run_a = i;
      last_sleep = i;
    }
    if (run_a >= 0 && (i == n || (!asleep && i - last_sleep > SLEEP_GAP_MIN))) {
      if (last_sleep - run_a > best_b - best_a) {
        best_a = run_a;
        best_b = last_sleep;
      }
      run_a = -1;
    }
  }
  bool found = best_a >= 0 && best_b - best_a + 1 >= SLEEP_MIN_NIGHT;
  if (found) {
    memset(night, 0, sizeof(*night));
    night->start = (uint32_t)(first + best_a) * 60;
    night->end = (uint32_t)(first + best_b + 1) * 60;
    int32_t still_run = 0, still_max = 0;
    for (int32_t i = best_a; i <= best_b; i++) {
      if (cls[i] == SLEEP_CALM_C) night->calm_min++;
      else if (cls[i] == SLEEP_RESTLESS_C) night->restless_min++;
      else night->awake_min++;
      int32_t raw = imu_movement(first + i);
      still_run = raw >= 0 && raw < SLEEP_NOT_WORN_MOVE ? still_run + 1 : 0;
      if (still_run > still_max) still_max = still_run;
    }
    *not_worn = still_max >= SLEEP_NOT_WORN_MIN;
    // Hypnogram: the most frequent class of every 5 minutes (ties go to the more awake one).
    for (int32_t i = best_a; i <= best_b && *bins_n < SLEEP_MAX_BINS; i += SLEEP_BIN_MIN) {
      uint8_t count[4] = { 0, 0, 0, 0 };
      for (int32_t j = i; j < i + SLEEP_BIN_MIN && j <= best_b; j++) count[min((int)cls[j], (int)SLEEP_AWAKE_C)]++;
      uint8_t c = SLEEP_CALM_C;
      for (uint8_t k = 1; k < 3; k++) {
        if (count[k] >= count[c]) c = k;
      }
      bins[(*bins_n)++] = c;
    }
  }
  psram_free(cls);
  return found && !*not_worn;
}

// Local time `minute_of_day` on the day of `base` plus `day_offset` days.
static time_t sleep_local(const struct tm *base, int day_offset, int minute_of_day) {
  struct tm t = *base;
  t.tm_mday += day_offset;
  t.tm_hour = minute_of_day / 60;
  t.tm_min = minute_of_day % 60;
  t.tm_sec = 0;
  t.tm_isdst = -1;
  return mktime(&t);
}

bool sleep_analyze_now(SleepNight *night, uint8_t *bins, uint16_t *bins_n, bool *not_worn) {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  int mod = t.tm_hour * 60 + t.tm_min;
  time_t from = sleep_local(&t, mod >= 18 * 60 ? 0 : -1, 18 * 60);
  return sleep_analyze(from, now, night, bins, bins_n, not_worn);
}

/* ================================ Bedtime mode ==================================== */

void bedtime_set(bool on) {
  if (on == (lv_subject_get_int(subj_bedtime_on) != 0)) return;
  if (on) {
    bedtime_prev_dnd = lv_subject_get_int(subj_dnd);
    subj_set(subj_dnd, 1);
  } else if (bedtime_prev_dnd >= 0) {
    subj_set(subj_dnd, bedtime_prev_dnd);
    bedtime_prev_dnd = -1;
  }
  subj_set(subj_bedtime_on, on ? 1 : 0);
  Serial.printf("[SLEEP] bedtime mode %s\n", on ? "on" : "off");
}

static bool bedtime_in_window(int mod) {
  int from = lv_subject_get_int(subj_bed_from), to = lv_subject_get_int(subj_bed_to);
  if (from == to) return false;
  return from < to ? mod >= from && mod < to : mod >= from || mod < to;
}

// Follows the schedule at its boundaries only, so a manual change holds until then.
static void bedtime_check(const struct tm *t, bool settings_changed) {
  if (!lv_subject_get_int(subj_bedtime) || !clock_is_valid()) {
    if (settings_changed && bedtime_window_prev == 1) bedtime_set(false);  // schedule switched off
    bedtime_window_prev = -1;
    return;
  }
  int in = bedtime_in_window(t->tm_hour * 60 + t->tm_min) ? 1 : 0;
  if (in != bedtime_window_prev || settings_changed) bedtime_set(in);
  bedtime_window_prev = in;
}

static void bedtime_setting_obs(lv_observer_t *observer, lv_subject_t *subject) {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  bedtime_check(&t, bedtime_window_prev != -1 || lv_subject_get_int(subj_bedtime));
}

/* ================================ Service ========================================= */

// Every minute: bedtime mode, and in the morning the night that just ended.
static void sleep_on_minute(const struct tm *t) {
  bedtime_check(t, false);
  if (!imu_available()) return;
  int mod = t->tm_hour * 60 + t->tm_min;
  if (mod < 4 * 60 || mod > 12 * 60 || (t->tm_min % 10 && mod != 12 * 60)) return;
  int32_t key = (t->tm_year + 1900) * 1000 + t->tm_yday;
  if (key == sleep_done_key) return;
  SleepNight night;
  uint8_t *bins = (uint8_t *)psram_malloc(SLEEP_MAX_BINS);
  if (!bins) return;
  uint16_t bins_n;
  bool not_worn;
  if (!sleep_analyze_now(&night, bins, &bins_n, &not_worn)) {
    if (mod >= 12 * 60) sleep_mark_done(key);  // no night today
    psram_free(bins);
    return;
  }
  bool over = time(NULL) - (time_t)night.end >= SLEEP_DONE_AFTER * 60 || mod >= 12 * 60;
  if (over) {
    sleep_store_night(night, bins, bins_n);
    sleep_mark_done(key);
    subj_bump(subj_sleep);
    char body[64], dur[24];
    format_duration(night.calm_min + night.restless_min, dur, sizeof(dur));
    snprintf(body, sizeof(body), "You slept %s. Tap for details in Sleep.", dur);
    notify_post("Sleep", "Good morning", body, ICON_BED, SLEEP_INDIGO);
  }
  psram_free(bins);
}

void sleep_tracker_init() {
  sleep_load();
  lv_subject_add_observer(subj_bedtime, bedtime_setting_obs, NULL);
  lv_subject_add_observer(subj_bed_from, bedtime_setting_obs, NULL);
  lv_subject_add_observer(subj_bed_to, bedtime_setting_obs, NULL);
  clock_add_minute_handler(sleep_on_minute);
}

int sleep_night_count() {
  return sleep_nights_n;
}

const SleepNight *sleep_night(int index) {
  return index >= 0 && index < sleep_nights_n ? &sleep_nights[index] : NULL;
}

const uint8_t *sleep_last_bins(uint16_t *n) {
  *n = sleep_bins_n;
  return sleep_bins;
}
