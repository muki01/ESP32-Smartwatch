/*
 * apps.h - The apps of the watch: entry points for the launcher, the tiles next to the
 * watch face and the watch face complications.
 *
 *   Health      Activity (+ tile), Workout, Sleep
 *   Time        Alarm, Timer, Stopwatch, Calendar
 *   Media       Weather (+ tile), Music (+ tile), Recorder
 *   Extras      Lights (WLED), Car          - shown when enabled in Settings > Extras
 *   System      Settings
 */
#pragma once

#include <lvgl.h>

void activity_open();
lv_obj_t *activity_tile_create();
void workout_open();
void sleep_open();

void alarm_app_init();   // rings alarms (full-screen alert)
void alarm_open();
void timer_app_init();   // rings the countdown
void timer_open();
void stopwatch_open();
void calendar_open();

void weather_open();
lv_obj_t *weather_tile_create();
void music_open();
lv_obj_t *music_tile_create();
void recorder_open();

void lights_open();
void car_open();

void settings_open();
