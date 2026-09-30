/*
 * display.h - SH8601 AMOLED panel (QSPI) and FT3168 touch controller: the LVGL display
 * and pointer input, panel power and brightness. The policy (when the screen sleeps,
 * what wakes it) is src/ui/system/screen.cpp.
 */
#pragma once

#include <lvgl.h>

// Panel, touch, lv_init(), LVGL display and input device. Draw buffers are sized to the
// internal RAM that is free (see display.cpp).
void display_init();

void display_panel_on();                     // leave sleep mode (keeps brightness 0)
void display_panel_off();                    // brightness 0, display off, sleep mode
void display_set_brightness(int32_t percent);  // 1..100 %, perceptual curve; 0 = dark
uint32_t display_ms_since_panel_on();        // the panel shows nothing for 120 ms after waking

// Touch while the screen is off: LVGL gets no input, the controller's interrupt tells
// that a finger landed (the loop is woken at once).
void display_touch_suspend(bool suspend);
bool display_touch_irq_take();               // an interrupt came since the last call
bool display_touch_detected();               // a finger is down or just tapped
void display_touch_ignore_until_release();   // the touch that woke the screen does nothing else
