/*
 * settings.h - The watch's observable state: LVGL subjects that the UI binds to and the
 * services observe. "Persisted" ones are stored in NVS automatically (debounced).
 */
#pragma once

#include <lvgl.h>

enum WifiState : int32_t { WIFI_ST_OFF, WIFI_ST_IDLE, WIFI_ST_CONNECTING, WIFI_ST_CONNECTED, WIFI_ST_FAILED };
enum BtState : int32_t { BT_ST_OFF, BT_ST_ADVERTISING, BT_ST_CONNECTED };
enum WatchFace : int32_t { FACE_DIGITAL, FACE_ANALOG, FACE_MODULAR, FACE_MINIMAL, FACE_COUNT };
enum WorkoutState : int32_t { WORKOUT_NONE, WORKOUT_RUNNING, WORKOUT_PAUSED };
enum RecorderState : int32_t { REC_IDLE, REC_RECORDING, REC_PLAYING };

#define FACE_COLOR_COUNT 9

// ---- Display and gestures ----------------------------------------------------------
extern lv_subject_t *subj_brightness;      // 5..100 %                               (persisted)
extern lv_subject_t *subj_screen_timeout;  // seconds                                (persisted)
extern lv_subject_t *subj_tap_to_wake;     // 0/1                                    (persisted)
extern lv_subject_t *subj_raise_wake;      // 0/1                                    (persisted)
extern lv_subject_t *subj_aod;             // always-on display 0/1                  (persisted)
extern lv_subject_t *subj_watchface;       // WatchFace                              (persisted)
extern lv_subject_t *subj_face_color;      // accent colour of the faces, index      (persisted)
extern lv_subject_t *subj_face_texture;    // faces on the texture 1 / on black 0    (persisted)
extern lv_subject_t *subj_perf_overlay;    // FPS monitor 0/1                        (persisted)

// ---- Sound and notifications -------------------------------------------------------
extern lv_subject_t *subj_volume;          // 0..100 %                               (persisted)
extern lv_subject_t *subj_touch_sounds;    // 0/1                                    (persisted)
extern lv_subject_t *subj_silent;          // UI sounds off, alarms still ring       (persisted)
extern lv_subject_t *subj_dnd;             // do not disturb 0/1                     (persisted)
extern lv_subject_t *subj_notif_sounds;    // chime on notifications                 (persisted)
extern lv_subject_t *subj_notif_wake;      // notifications wake the screen          (persisted)

// ---- Connectivity ------------------------------------------------------------------
extern lv_subject_t *subj_wifi_enabled;    // 0/1                                    (persisted)
extern lv_subject_t *subj_bt_enabled;      // 0/1                                    (persisted)
extern lv_subject_t *subj_gadgetbridge;    // phone link through Gadgetbridge        (persisted)
extern lv_subject_t *subj_ota;             // wireless update listening 0/1

// ---- Time, units, health -----------------------------------------------------------
extern lv_subject_t *subj_time_24h;        // 0/1                                    (persisted)
extern lv_subject_t *subj_auto_time;       // 0/1                                    (persisted)
extern lv_subject_t *subj_timezone;        // index of the clock's zone table         (persisted)
extern lv_subject_t *subj_units;           // 0 = metric, 1 = imperial               (persisted)
extern lv_subject_t *subj_temp_unit;       // 0 = °C, 1 = °F                         (persisted)
extern lv_subject_t *subj_step_goal;       // steps per day                          (persisted)
extern lv_subject_t *subj_move_remind;     // hourly reminder to move 0/1            (persisted)

// ---- Power and bedtime -------------------------------------------------------------
extern lv_subject_t *subj_saver;           // battery saver 0/1                      (persisted)
extern lv_subject_t *subj_bedtime;         // bedtime mode on a schedule 0/1         (persisted)
extern lv_subject_t *subj_bed_from;        // bedtime start, minutes after midnight  (persisted)
extern lv_subject_t *subj_bed_to;          // bedtime end, minutes after midnight    (persisted)
extern lv_subject_t *subj_bedtime_on;      // bedtime mode is active right now

// ---- Extras (Settings > Extras) ----------------------------------------------------
extern lv_subject_t *subj_ext_car;         // Car app                                (persisted)
extern lv_subject_t *subj_ext_lights;      // Lights app for WLED devices            (persisted)
extern lv_subject_t *subj_ext_clap;        // double clap toggles the lights         (persisted)

// ---- Live state published by the services ------------------------------------------
extern lv_subject_t *subj_wifi_state;      // WifiState
extern lv_subject_t *subj_wifi_scan;       // bumped after every finished scan
extern lv_subject_t *subj_bt_state;        // BtState
extern lv_subject_t *subj_battery;         // 0..100 %, -1 = no battery
extern lv_subject_t *subj_charging;        // 0/1
extern lv_subject_t *subj_usb_power;       // 0/1
extern lv_subject_t *subj_time_text;       // "14:05" / "2:05 PM", changes every minute
extern lv_subject_t *subj_date_text;       // "Mon, 29 Sep"
extern lv_subject_t *subj_steps;           // steps today
extern lv_subject_t *subj_weather;         // bumped when weather data, state or search results change
extern lv_subject_t *subj_notif_count;     // notifications in the center
extern lv_subject_t *subj_notif_unread;    // not yet seen
extern lv_subject_t *subj_alarm_next;      // next alarm, minutes after midnight, -1 = none
extern lv_subject_t *subj_phone;           // bumped when the phone link, its music or calls change
extern lv_subject_t *subj_music;           // bumped when the music player changes track or state
extern lv_subject_t *subj_sleep;           // bumped when a night was analysed
extern lv_subject_t *subj_workout;         // WorkoutState
extern lv_subject_t *subj_agenda;          // bumped when calendar events from the phone change
extern lv_subject_t *subj_recorder;        // RecorderState
extern lv_subject_t *subj_lights;          // bumped when the WLED devices or their state change
extern lv_subject_t *subj_car;             // bumped when the car link or its data change

void settings_init();           // NVS -> subjects (after lv_init())
void settings_flush();          // store pending changes now (before power off / restart)
void settings_factory_reset();  // erase every stored setting and all data, then restart

void subj_set(lv_subject_t *subject, int32_t value);  // set only if different
void subj_bump(lv_subject_t *subject);                // "something changed" counters
