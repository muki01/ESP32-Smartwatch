/*
 * faces.h - Watch faces (Digital, Analog, Modular, Minimal), what they share (the screen
 * with its background, the accent colour, the status row) and the Customize page.
 */
#pragma once

#include <lvgl.h>
#include <time.h>

lv_obj_t *face_digital_create();
lv_obj_t *face_analog_create();
lv_obj_t *face_modular_create();
lv_obj_t *face_minimal_create();
void face_customize_open();

// A black screen with the background texture (faces and tiles).
lv_obj_t *face_screen_create();
// Status row: battery, Wi-Fi, phone, workout, do not disturb / bedtime, battery saver, unread.
lv_obj_t *face_status_create(lv_obj_t *parent, bool battery);

// Accent colour: these parts follow the colour the user picks.
uint32_t face_accent();
const char *face_color_name(int index);
uint32_t face_color_value(int index);
void face_accent_text(lv_obj_t *obj);
void face_accent_bg(lv_obj_t *obj);
void face_accent_line(lv_obj_t *obj);
void face_accent_arc(lv_obj_t *obj);  // arc indicator
lv_obj_t *face_accent_label(lv_obj_t *parent, const lv_font_t *font, const char *text);

// Shared by the face files.
extern const char *const FACE_WEEKDAYS_SHORT[7];   // "SUN" ...
extern const char *const FACE_WEEKDAYS_LONG[7];    // "Sunday" ...
extern const char *const FACE_MONTHS_SHORT[12];    // "JAN" ...
extern const char *const FACE_MONTHS_LONG[12];     // "January" ...
void face_now(struct tm *t);
int face_hour12(int hour);
void face_weather_value(lv_obj_t *icon, lv_obj_t *value);  // current weather icon and temperature
