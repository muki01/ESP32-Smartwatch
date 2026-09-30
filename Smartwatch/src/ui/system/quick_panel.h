/*
 * quick_panel.h - Quick settings, pulled down from the top of the watch face, with the
 * flashlight and "find my phone" pages they open.
 */
#pragma once

#include <lvgl.h>

lv_obj_t *quick_panel_create();
void flashlight_open();
void findphone_open();
