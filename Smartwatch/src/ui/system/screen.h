/*
 * screen.h - Screen power policy: timeout, sleep and wake, the always-on display, tap to
 * wake, raise to wake / lower to sleep, brightness.
 */
#pragma once

#include <lvgl.h>

enum WakeReason : uint8_t { WAKE_BUTTON, WAKE_TOUCH, WAKE_RAISE, WAKE_EVENT };

void screen_init();                     // after the settings and the first screen exist
void screen_service();                  // every loop: taps and wrist gestures while off

void screen_sleep();                    // off, or the always-on display when it is enabled
void screen_wake(WakeReason reason);
void screen_toggle();                   // side button
void screen_power_off();                // shutdown / restart: panel off, whatever the settings
bool screen_is_off();                   // no regular UI visible (panel off or always-on display)
bool screen_in_aod();

void screen_keep_awake(bool on);        // nested; the timeout is ignored while any hold is active
void screen_brightness_override(int32_t percent);  // -1 = follow the setting (flashlight: 100)
