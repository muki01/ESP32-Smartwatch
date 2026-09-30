/*
 * activity.cpp - Daily activity tracking.
 *
 * Steps come from the motion sensor's step detector (imu.cpp), polled every 2 s. Today's
 * total and the history survive restarts (NVS, written every 10 minutes and at midnight).
 * Reaching the goal posts a notification once per day. The hourly reminder to move comes
 * at :50 between 08:00 and 21:00 when the last hour had fewer than 100 steps - not when
 * the watch lies somewhere (no movement at all), during a workout or with do not disturb.
 */
#include "activity.h"

#include <Arduino.h>
#include <Preferences.h>
#include "../assets/fonts/icons.h"
#include "../core/format.h"
#include "../core/settings.h"
#include "../drivers/imu.h"
#include "clock.h"
#include "notifications.h"

#define ACT_POLL_MS        2000
#define ACT_SAVE_MS        600000
#define ACT_STRIDE_CM      75      // average step length
#define ACT_KCAL_PER_1000  40      // kcal per 1000 steps (walking, ~70 kg)
#define ACT_ACTIVE_STEPS   60      // steps within a minute that make it an active minute
#define ACT_REMIND_STEPS   100     // fewer steps than this in an hour: time to move
#define ACT_NS             "activity"
#define ACT_GREEN          0x30D158

static uint32_t act_steps;                   // today
static uint32_t act_history[ACTIVITY_DAYS];  // [0] = yesterday, [1] = the day before, ...
static uint16_t act_active_min;
static int32_t act_day = -1;                 // local date of act_steps: year * 1000 + day of year
static uint32_t act_hw_last;
static uint32_t act_minute_start_steps;
static int act_minute = -1;
static bool act_goal_done;
static bool act_dirty;
static uint32_t act_saved_ms;
static uint32_t act_hour_start_steps;
static int act_hour = -1;

static int32_t act_today_key() {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  return (t.tm_year + 1900) * 1000 + t.tm_yday;
}

void activity_save() {
  Preferences p;
  if (!p.begin(ACT_NS, false)) return;
  p.putInt("day", act_day);
  p.putUInt("steps", act_steps);
  p.putUShort("amin", act_active_min);
  p.putBool("goal", act_goal_done);
  p.putBytes("hist", act_history, sizeof(act_history));
  p.end();
  act_dirty = false;
  act_saved_ms = millis();
}

static void act_load() {
  Preferences p;
  if (!p.begin(ACT_NS, true)) return;
  act_day = p.getInt("day", -1);
  act_steps = p.getUInt("steps", 0);
  act_active_min = p.getUShort("amin", 0);
  act_goal_done = p.getBool("goal", false);
  p.getBytes("hist", act_history, sizeof(act_history));
  p.end();
}

// A new day: today's total moves into the history (with empty days for any gap).
static void act_rollover(int32_t today) {
  int gap = 1;
  if (act_day >= 0) {
    int32_t prev_year = act_day / 1000, year = today / 1000;
    gap = year == prev_year ? today % 1000 - act_day % 1000 : (int)(today % 1000 + 365 - act_day % 1000);
    if (gap < 1) gap = 1;
  }
  for (int g = 0; g < gap; g++) {
    for (int i = ACTIVITY_DAYS - 1; i > 0; i--) act_history[i] = act_history[i - 1];
    act_history[0] = g == 0 && act_day >= 0 ? act_steps : 0;
  }
  act_day = today;
  act_steps = 0;
  act_active_min = 0;
  act_goal_done = false;
  act_minute_start_steps = 0;
  activity_save();
}

static void act_poll_cb(lv_timer_t *t) {
  if (clock_is_valid()) {
    int32_t today = act_today_key();
    if (act_day != today) act_rollover(today);
  }

  uint32_t hw = imu_step_counter();
  uint32_t delta = hw >= act_hw_last ? hw - act_hw_last : hw;  // the sensor restarted: count from 0
  act_hw_last = hw;
  if (delta) {
    act_steps += delta;
    act_dirty = true;
    subj_set(subj_steps, (int32_t)act_steps);
  }

  // Active minutes: a minute with at least ACT_ACTIVE_STEPS steps.
  int minute = (int)(time(NULL) / 60 % 1440);
  if (minute != act_minute) {
    if (act_minute >= 0 && act_steps - act_minute_start_steps >= ACT_ACTIVE_STEPS) act_active_min++;
    act_minute = minute;
    act_minute_start_steps = act_steps;
  }

  int32_t goal = lv_subject_get_int(subj_step_goal);
  if (!act_goal_done && goal > 0 && act_steps >= (uint32_t)goal) {
    act_goal_done = true;
    act_dirty = true;
    char body[64], num[16];
    format_thousands(goal, num, sizeof(num));
    snprintf(body, sizeof(body), "You reached %s steps today. Great job!", num);
    notify_post("Activity", "Daily goal reached", body, ICON_ACTIVITY, ACT_GREEN);
  }
  if (act_dirty && millis() - act_saved_ms > ACT_SAVE_MS) activity_save();
}

static void act_on_minute(const struct tm *t) {
  if (t->tm_hour != act_hour || act_steps < act_hour_start_steps) {
    act_hour = t->tm_hour;
    act_hour_start_steps = act_steps;
  }
  if (t->tm_min != 50 || !lv_subject_get_int(subj_move_remind) || t->tm_hour < 8 || t->tm_hour >= 21) return;
  if (lv_subject_get_int(subj_dnd) || lv_subject_get_int(subj_workout) != WORKOUT_NONE) return;
  if (act_steps - act_hour_start_steps >= ACT_REMIND_STEPS) return;
  int32_t now_min = (int32_t)(time(NULL) / 60), moved = 0;
  for (int32_t m = now_min - 50; m < now_min; m++) {
    if (imu_movement(m) >= 40) moved++;
  }
  if (moved < 3) return;  // not worn
  notify_post("Activity", "Time to move", "You have been still for a while. A short walk does you good.",
              ICON_ACTIVITY, ACT_GREEN);
}

/* ================================ Public API ====================================== */

void activity_init() {
  act_load();
  act_hw_last = imu_step_counter();
  if (clock_is_valid() && act_day != act_today_key()) act_rollover(act_today_key());
  act_saved_ms = millis();
  subj_set(subj_steps, (int32_t)act_steps);
  lv_timer_create(act_poll_cb, ACT_POLL_MS, NULL);
  clock_add_minute_handler(act_on_minute);
}

uint32_t activity_steps() {
  return act_steps;
}

uint32_t activity_distance_m() {
  return act_steps * ACT_STRIDE_CM / 100;
}

uint32_t activity_kcal() {
  return act_steps * ACT_KCAL_PER_1000 / 1000;
}

uint32_t activity_active_min() {
  return act_active_min;
}

uint32_t activity_history(int days_ago) {
  return days_ago >= 1 && days_ago < ACTIVITY_DAYS ? act_history[days_ago - 1] : 0;
}
