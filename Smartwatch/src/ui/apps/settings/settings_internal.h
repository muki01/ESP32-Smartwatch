/*
 * settings_internal.h - The pages of the Settings app, shared between its files.
 *
 *   settings_app.cpp      root, Extras, About, System
 *   settings_connect.cpp  Wi-Fi, Bluetooth
 *   settings_device.cpp   Display, Sound, Notifications, Battery, Date & time, Units
 *   settings_motion.cpp   motion sensor calibration
 */
#pragma once

#include <lvgl.h>
#include "../../kit/kit.h"

typedef lv_obj_t *(*sui_builder_t)();

lv_obj_t *sui_nav_row(lv_obj_t *parent, const char *symbol, uint32_t color, const char *title, sui_builder_t build);

lv_obj_t *sui_build_wifi();
lv_obj_t *sui_build_bluetooth();
lv_obj_t *sui_build_display();
lv_obj_t *sui_build_sound();
lv_obj_t *sui_build_notifications();
lv_obj_t *sui_build_battery();
lv_obj_t *sui_build_datetime();
lv_obj_t *sui_build_units();
lv_obj_t *sui_build_motion();

// Subtitles of the root rows (they follow the live state).
void sui_wifi_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject);
void sui_bt_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject);
void sui_display_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject);
void sui_sound_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject);
void sui_notif_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject);
void sui_battery_row_obs(lv_observer_t *observer, lv_subject_t *subject);
void sui_datetime_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject);
void sui_units_obs(lv_observer_t *observer, lv_subject_t *subject);
