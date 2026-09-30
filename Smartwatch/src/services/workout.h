/*
 * workout.h - Workouts (walk, run, hike, other): time, steps, distance, pace, cadence and
 * calories, a 3 s countdown, pause and resume, and the last 20 workouts (NVS). The state
 * is subj_workout; the workout keeps running when its screen is left.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define WORKOUT_HISTORY 20

enum WorkoutType : uint8_t { WO_WALK, WO_RUN, WO_HIKE, WO_OTHER, WO_TYPES };

struct WorkoutRecord {
  uint32_t start;        // epoch seconds
  uint32_t duration_s;   // active time
  uint32_t steps;
  uint32_t distance_m;
  uint16_t kcal;
  uint8_t type;
  uint8_t reserved;
};

void workout_init();
bool workout_active();
void workout_start(WorkoutType type);  // after a 3 s countdown
void workout_pause_toggle();
WorkoutRecord workout_finish();        // saved when longer than a minute
WorkoutType workout_type();
bool workout_counting_down(uint32_t *seconds_left);
uint32_t workout_elapsed_ms();
uint32_t workout_steps();
uint32_t workout_distance_m(uint8_t type, uint32_t steps);
uint32_t workout_kcal(uint8_t type, uint32_t active_ms);
uint32_t workout_cadence();            // steps per minute over the last 30 s
int workout_history_count();
const WorkoutRecord *workout_history(int index);  // 0 = newest

// "2.41" in km or miles (subj_units), "5'12\"" per km or mile.
void workout_format_distance(uint32_t m, char *value, size_t len);
void workout_format_pace(uint32_t active_ms, uint32_t m, char *buf, size_t len);
void workout_format_duration(uint32_t s, char *buf, size_t len);
