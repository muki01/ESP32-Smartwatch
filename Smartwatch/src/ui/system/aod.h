/*
 * aod.h - The always-on display: a black screen with a thin, dim clock (screen.cpp shows
 * it when the screen times out with "Always on" enabled).
 */
#pragma once

#include <lvgl.h>

lv_obj_t *aod_screen_create();
void aod_refresh();  // redraw and move the clock a little (burn-in protection)
