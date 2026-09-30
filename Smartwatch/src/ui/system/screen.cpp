/*
 * screen.cpp - Screen power policy.
 *
 *   awake     regular UI, brightness from the settings, CPU at 240 MHz. After the screen
 *             timeout without input the screen goes off (or to the always-on display).
 *   off       panel in sleep mode, rendering and animations paused, CPU at 80 MHz.
 *   AOD       panel on and dimmed (<= 30 %), a black clock screen that redraws once a
 *             minute (aod.cpp), CPU at 80 MHz.
 *
 * Waking up: the side button (power.cpp), the BOOT button (nav.cpp), alarms and
 * notifications, a tap (touch interrupt, handled here so even very short taps count) and
 * a raise of the wrist (motion sensor gestures, imu.cpp). Bedtime mode keeps taps and
 * raises from waking the screen and turns the always-on display off.
 * After a raise the screen goes off again when the arm is lowered without the screen
 * having been touched. After a minute off, the watch face comes back.
 * The panel is lit only once a fresh frame is out (no flash of the old picture).
 */
#include "screen.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../core/system.h"
#include "../../drivers/display.h"
#include "../../drivers/imu.h"
#include "../kit/kit.h"
#include "aod.h"
#include "nav.h"

#define SCREEN_HOME_AFTER_MS  60000   // back to the watch face after a minute off
#define SCREEN_AOD_MAX_PCT    30      // always-on display brightness cap
#define SCREEN_CPU_AWAKE_MHZ  240
#define SCREEN_CPU_IDLE_MHZ   80      // screen off / always-on: less power (APB stays at 80 MHz)
#define SCREEN_PANEL_READY_MS 120     // SH8601 shows nothing for 120 ms after leaving sleep
#define SCREEN_VIEW_TOUCH_MS  100     // a touch this recent: the user looks at the screen
#define SCREEN_VIEW_NOTE_MS   20000   // at most one such note for the motion sensor per 20 s

static bool scr_off;               // no regular UI visible (panel off or always-on display)
static bool scr_aod;               // off in always-on mode: panel on, dim clock
static bool scr_ready;             // screen_init() done: settings and screens exist
static bool scr_light_pending = true;
static uint32_t scr_off_since;
static uint32_t scr_woke_at;       // lv_tick of the last wake-up
static uint32_t scr_view_noted_at;
static WakeReason scr_wake_reason = WAKE_BUTTON;
static int scr_holds;              // screen_keep_awake() nesting
static int32_t scr_override = -1;  // brightness forced by an app (flashlight)
static lv_obj_t *scr_aod_screen;
static lv_obj_t *scr_aod_prev;     // screen shown before the always-on display

static bool scr_bedtime() {
  return lv_subject_get_int(subj_bedtime_on) != 0;
}

static void scr_apply_brightness() {
  int32_t percent = scr_override >= 0 ? scr_override : lv_subject_get_int(subj_brightness);
  if (scr_aod) percent = min(percent, (int32_t)SCREEN_AOD_MAX_PCT);
  display_set_brightness(percent);
}

static void scr_light_up_cb(lv_timer_t *t) {
  if (!scr_off) scr_apply_brightness();
}

// First frame after waking is out: light the panel (not before it can show anything).
static void scr_refr_ready_cb(lv_event_t *e) {
  if (!scr_light_pending || scr_off || !scr_ready) return;
  scr_light_pending = false;
  uint32_t since_on = display_ms_since_panel_on();
  if (since_on >= SCREEN_PANEL_READY_MS) {
    scr_apply_brightness();
  } else {
    lv_timer_t *t = lv_timer_create(scr_light_up_cb, SCREEN_PANEL_READY_MS - since_on, NULL);
    lv_timer_set_repeat_count(t, 1);
  }
}

static void scr_timeout_cb(lv_timer_t *t) {
  if (scr_off || !scr_ready || scr_holds > 0) return;
  int32_t timeout_s = lv_subject_get_int(subj_screen_timeout);
  if (timeout_s > 0 && lv_display_get_inactive_time(NULL) >= (uint32_t)timeout_s * 1000U) screen_sleep();
}

// Always-on display: the panel stays on, dimmed, with the black clock screen.
static void scr_enter_aod() {
  if (!scr_aod_screen) scr_aod_screen = aod_screen_create();
  kit_transition_finish();
  kit_modal_close();
  scr_aod = true;
  scr_aod_prev = lv_screen_active();
  lv_obj_set_hidden(lv_layer_top(), true);  // toasts and banners stay hidden meanwhile
  lv_screen_load(scr_aod_screen);
  aod_refresh();
  scr_apply_brightness();
  lv_timer_pause(lv_anim_get_timer());
}

void screen_sleep() {
  if (scr_off) return;
  scr_off = true;
  scr_light_pending = false;
  scr_off_since = millis();
  if (scr_ready && lv_subject_get_int(subj_aod) && !scr_bedtime()) {
    scr_enter_aod();
  } else {
    // Nothing is visible: stop rendering and animations.
    display_panel_off();
    lv_timer_pause(lv_display_get_refr_timer(lv_display_get_default()));
    lv_timer_pause(lv_anim_get_timer());
  }
  display_touch_suspend(true);
  imu_set_screen_on(false);
  setCpuFrequencyMhz(SCREEN_CPU_IDLE_MHZ);
}

void screen_wake(WakeReason reason) {
  if (!scr_off) {
    lv_display_trigger_activity(lv_display_get_default());
    return;
  }
  setCpuFrequencyMhz(SCREEN_CPU_AWAKE_MHZ);
  bool from_aod = scr_aod;
  if (!from_aod) display_panel_on();
  scr_off = false;
  scr_aod = false;
  scr_wake_reason = reason;
  lv_obj_set_hidden(lv_layer_top(), false);

  bool go_home = millis() - scr_off_since >= SCREEN_HOME_AFTER_MS && !kit_alert_active() &&
                 lv_subject_get_int(subj_workout) == WORKOUT_NONE;
  if (go_home) nav_go_home(false);
  else if (from_aod && scr_aod_prev && lv_screen_active() == scr_aod_screen) lv_screen_load(scr_aod_prev);
  scr_aod_prev = NULL;

  lv_timer_resume(lv_display_get_refr_timer(lv_display_get_default()));
  lv_timer_resume(lv_anim_get_timer());
  lv_obj_invalidate(lv_screen_active());
  lv_display_trigger_activity(lv_display_get_default());
  scr_woke_at = lv_tick_get();  // same clock and moment as the activity reset above
  display_touch_suspend(false);
  if (reason == WAKE_TOUCH) display_touch_ignore_until_release();
  imu_set_screen_on(true);
  if (from_aod) scr_apply_brightness();  // the panel is on already
  else scr_light_pending = true;         // brightness returns with the first fresh frame
}

void screen_toggle() {
  if (scr_off) screen_wake(WAKE_BUTTON);
  else screen_sleep();
}

// Shutdown and restart: panel off regardless of the always-on setting.
void screen_power_off() {
  scr_off = true;
  scr_aod = false;
  display_panel_off();
}

bool screen_is_off() {
  return scr_off;
}

bool screen_in_aod() {
  return scr_aod;
}

void screen_keep_awake(bool on) {
  scr_holds += on ? 1 : -1;
  if (scr_holds < 0) scr_holds = 0;
  if (!on) lv_display_trigger_activity(lv_display_get_default());  // the timeout starts over
}

void screen_brightness_override(int32_t percent) {
  scr_override = percent;
  if (!scr_off && !scr_light_pending) scr_apply_brightness();
}

/* ================================ Wake sources ==================================== */

// Untouched since a raise woke it: the screen may follow the wrist back down.
static bool scr_untouched_since_raise() {
  return scr_wake_reason == WAKE_RAISE && lv_display_get_inactive_time(NULL) >= lv_tick_elaps(scr_woke_at);
}

void screen_service() {
  if (!scr_ready) return;
  bool can_wake = !scr_bedtime();
  if (scr_off && display_touch_irq_take()) {
    if (can_wake && lv_subject_get_int(subj_tap_to_wake) && display_touch_detected()) screen_wake(WAKE_TOUCH);
  }
  // Someone is touching the screen, so it faces their eyes: lets the motion sensor check
  // its default axes (only until they are calibrated).
  if (!scr_off && lv_display_get_inactive_time(NULL) < SCREEN_VIEW_TOUCH_MS &&
      lv_tick_elaps(scr_view_noted_at) >= SCREEN_VIEW_NOTE_MS) {
    scr_view_noted_at = lv_tick_get();
    imu_note_viewing();
  }
  switch (imu_take_gesture()) {
    case IMU_GESTURE_RAISE:
      if (scr_off && can_wake && lv_subject_get_int(subj_raise_wake)) screen_wake(WAKE_RAISE);
      break;
    case IMU_GESTURE_LOWER:
      if (!scr_off && scr_untouched_since_raise() && scr_holds == 0 && !kit_alert_active()) screen_sleep();
      break;
    default:
      break;
  }
}

/* ================================ Settings ======================================== */

static void scr_brightness_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (!scr_off && !scr_light_pending) scr_apply_brightness();
}

// Always-on display turned off (or bedtime mode on) while it shows: switch the panel off.
static void scr_aod_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (scr_aod && (!lv_subject_get_int(subj_aod) || scr_bedtime())) {
    screen_wake(WAKE_EVENT);
    screen_sleep();
  }
}

static void scr_perf_obs(lv_observer_t *observer, lv_subject_t *subject) {
#if LV_USE_PERF_MONITOR
  if (lv_subject_get_int(subject)) lv_sysmon_show_performance(lv_display_get_default());
  else lv_sysmon_hide_performance(lv_display_get_default());
#endif
}

void screen_init() {
  lv_display_add_event_cb(lv_display_get_default(), scr_refr_ready_cb, LV_EVENT_REFR_READY, NULL);
  lv_timer_create(scr_timeout_cb, 500, NULL);
  lv_subject_add_observer(subj_brightness, scr_brightness_obs, NULL);
  lv_subject_add_observer(subj_aod, scr_aod_obs, NULL);
  lv_subject_add_observer(subj_bedtime_on, scr_aod_obs, NULL);
  lv_subject_add_observer(subj_perf_overlay, scr_perf_obs, NULL);
  imu_set_screen_on(true);
  scr_ready = true;
}
