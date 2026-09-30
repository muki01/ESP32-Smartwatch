/*
 * activity.h - Daily activity: steps (subj_steps), distance, calories, active minutes,
 * the last 7 days, the daily goal and the hourly reminder to move.
 */
#pragma once

#include <stdint.h>

#define ACTIVITY_DAYS 7

void activity_init();
void activity_save();                   // store today's totals now (before power off)
uint32_t activity_steps();
uint32_t activity_distance_m();
uint32_t activity_kcal();
uint32_t activity_active_min();
uint32_t activity_history(int days_ago);  // 1 = yesterday ... ACTIVITY_DAYS - 1
