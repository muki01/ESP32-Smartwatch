/*
 * workout.cpp - Workout tracking.
 *
 * Distance comes from the steps and a stride length per activity, calories from the
 * activity's MET value for a 70 kg person: estimates, as on any watch without GPS and
 * heart rate. The live values update every 500 ms while a workout runs.
 */
#include "workout.h"

#include <Arduino.h>
#include <Preferences.h>
#include "../core/settings.h"
#include "../drivers/audio.h"
#include "../drivers/imu.h"

#define WO_NS           "workouts"
#define WO_WEIGHT_KG    70.0f
#define WO_TICK_MS      500
#define WO_CADENCE_S    30       // cadence over the last 30 s
#define WO_COUNTDOWN_MS 3000
#define WO_MIN_SAVE_S   60       // shorter workouts were probably started by mistake

static const float WO_MET[WO_TYPES] = { 3.5f, 9.0f, 6.0f, 5.0f };
static const uint8_t WO_STRIDE_CM[WO_TYPES] = { 75, 105, 70, 75 };

static uint8_t wo_type;
static uint32_t wo_start_epoch;
static uint32_t wo_active_ms;        // finished running stretches
static uint32_t wo_resumed_ms;       // millis() when the current stretch began
static uint32_t wo_counting_from;    // millis() when the countdown ends (0 = no countdown)
static uint32_t wo_steps;
static uint32_t wo_hw_last;
static uint16_t wo_cadence_ring[WO_CADENCE_S];  // steps per second, last 30 s
static uint8_t wo_cadence_pos;
static uint32_t wo_cadence_sec;
static lv_timer_t *wo_timer;
static WorkoutRecord wo_history[WORKOUT_HISTORY];
static int wo_history_n;

static bool wo_imperial() {
  return lv_subject_get_int(subj_units) != 0;
}

static bool wo_running() {
  return lv_subject_get_int(subj_workout) == WORKOUT_RUNNING && !wo_counting_from;
}

static void wo_history_load() {
  Preferences p;
  if (!p.begin(WO_NS, true)) return;
  wo_history_n = (int)(p.getBytes("list", wo_history, sizeof(wo_history)) / sizeof(WorkoutRecord));
  p.end();
}

static void wo_history_add(const WorkoutRecord &r) {
  if (wo_history_n == WORKOUT_HISTORY) wo_history_n--;
  memmove(&wo_history[1], &wo_history[0], wo_history_n * sizeof(WorkoutRecord));
  wo_history[0] = r;
  wo_history_n++;
  Preferences p;
  if (p.begin(WO_NS, false)) {
    p.putBytes("list", wo_history, wo_history_n * sizeof(WorkoutRecord));
    p.end();
  }
}

static void wo_tick_cb(lv_timer_t *t) {
  if (!workout_active()) return;
  uint32_t now = millis();
  if (wo_counting_from && (int32_t)(now - wo_counting_from) >= 0) {  // countdown over: go
    wo_counting_from = 0;
    wo_resumed_ms = now;
    wo_hw_last = imu_step_counter();
    audio_beep(1320, 180);
  }
  uint32_t hw = imu_step_counter();
  uint32_t delta = hw >= wo_hw_last ? hw - wo_hw_last : 0;
  wo_hw_last = hw;
  bool running = wo_running();
  if (running) wo_steps += delta;
  uint32_t sec = now / 1000;
  if (sec != wo_cadence_sec) {  // a new second: advance the cadence window
    wo_cadence_sec = sec;
    wo_cadence_pos = (wo_cadence_pos + 1) % WO_CADENCE_S;
    wo_cadence_ring[wo_cadence_pos] = 0;
  }
  if (running) wo_cadence_ring[wo_cadence_pos] += delta;
}

/* ================================ Public API ====================================== */

void workout_init() {
  wo_history_load();
  wo_timer = lv_timer_create(wo_tick_cb, WO_TICK_MS, NULL);
}

bool workout_active() {
  return lv_subject_get_int(subj_workout) != WORKOUT_NONE;
}

void workout_start(WorkoutType type) {
  wo_type = type < WO_TYPES ? type : WO_OTHER;
  wo_start_epoch = (uint32_t)time(NULL);
  wo_active_ms = 0;
  wo_steps = 0;
  wo_hw_last = imu_step_counter();
  memset(wo_cadence_ring, 0, sizeof(wo_cadence_ring));
  wo_counting_from = millis() + WO_COUNTDOWN_MS;
  wo_resumed_ms = wo_counting_from;
  subj_set(subj_workout, WORKOUT_RUNNING);
}

void workout_pause_toggle() {
  if (wo_counting_from) return;
  uint32_t now = millis();
  if (lv_subject_get_int(subj_workout) == WORKOUT_RUNNING) {
    wo_active_ms += now - wo_resumed_ms;
    subj_set(subj_workout, WORKOUT_PAUSED);
  } else {
    wo_resumed_ms = now;
    wo_hw_last = imu_step_counter();
    subj_set(subj_workout, WORKOUT_RUNNING);
  }
}

WorkoutRecord workout_finish() {
  if (wo_running()) wo_active_ms += millis() - wo_resumed_ms;
  WorkoutRecord r;
  memset(&r, 0, sizeof(r));
  r.start = wo_start_epoch;
  r.duration_s = wo_active_ms / 1000;
  r.steps = wo_steps;
  r.distance_m = workout_distance_m(wo_type, wo_steps);
  r.kcal = (uint16_t)min(workout_kcal(wo_type, wo_active_ms), (uint32_t)65535);
  r.type = wo_type;
  subj_set(subj_workout, WORKOUT_NONE);
  wo_counting_from = 0;
  if (r.duration_s >= WO_MIN_SAVE_S) wo_history_add(r);
  return r;
}

WorkoutType workout_type() {
  return (WorkoutType)wo_type;
}

bool workout_counting_down(uint32_t *seconds_left) {
  if (!wo_counting_from) return false;
  int32_t left = (int32_t)(wo_counting_from - millis());
  if (seconds_left) *seconds_left = (uint32_t)max((int32_t)1, (left + 999) / 1000);
  return true;
}

uint32_t workout_elapsed_ms() {
  return wo_active_ms + (wo_running() ? millis() - wo_resumed_ms : 0);
}

uint32_t workout_steps() {
  return wo_steps;
}

uint32_t workout_distance_m(uint8_t type, uint32_t steps) {
  return steps * WO_STRIDE_CM[type % WO_TYPES] / 100;
}

uint32_t workout_kcal(uint8_t type, uint32_t active_ms) {
  return (uint32_t)(WO_MET[type % WO_TYPES] * WO_WEIGHT_KG * active_ms / 3600000.0f);
}

uint32_t workout_cadence() {
  uint32_t sum = 0;
  for (uint16_t v : wo_cadence_ring) sum += v;
  return sum * 60 / WO_CADENCE_S;
}

int workout_history_count() {
  return wo_history_n;
}

const WorkoutRecord *workout_history(int index) {
  return index >= 0 && index < wo_history_n ? &wo_history[index] : NULL;
}

void workout_format_distance(uint32_t m, char *value, size_t len) {
  float d = wo_imperial() ? m / 1609.34f : m / 1000.0f;
  snprintf(value, len, "%.2f", d);
}

void workout_format_pace(uint32_t active_ms, uint32_t m, char *buf, size_t len) {
  float unit = wo_imperial() ? m / 1609.34f : m / 1000.0f;
  uint32_t s = unit > 0.0f ? (uint32_t)(active_ms / 1000.0f / unit) : 0;
  if (m < 50 || unit <= 0.0f || s >= 3600) {
    strlcpy(buf, "--", len);
    return;
  }
  snprintf(buf, len, "%u'%02u\"", (unsigned)(s / 60), (unsigned)(s % 60));
}

void workout_format_duration(uint32_t s, char *buf, size_t len) {
  if (s >= 3600) snprintf(buf, len, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else snprintf(buf, len, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}
