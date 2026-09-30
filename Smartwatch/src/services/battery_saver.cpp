/*
 * battery_saver.cpp - Battery saver: remembers the settings it overrides (NVS) and
 * restores them when it is turned off.
 */
#include "battery_saver.h"

#include <Arduino.h>
#include <Preferences.h>
#include "../core/settings.h"

#define SAVER_NS          "saver"
#define SAVER_BRIGHTNESS  40
#define SAVER_TIMEOUT_S   10

static bool saver_known;
static bool saver_on;

static void saver_obs(lv_observer_t *observer, lv_subject_t *subject) {
  bool on = lv_subject_get_int(subject) != 0;
  if (!saver_known) {  // at boot the saved state is already in effect
    saver_known = true;
    saver_on = on;
    return;
  }
  if (on == saver_on) return;
  saver_on = on;
  Preferences p;
  if (!p.begin(SAVER_NS, false)) return;
  if (on) {
    p.putInt("wifi", lv_subject_get_int(subj_wifi_enabled));
    p.putInt("aod", lv_subject_get_int(subj_aod));
    p.putInt("bright", lv_subject_get_int(subj_brightness));
    p.putInt("timeout", lv_subject_get_int(subj_screen_timeout));
    subj_set(subj_wifi_enabled, 0);
    subj_set(subj_aod, 0);
    if (lv_subject_get_int(subj_brightness) > SAVER_BRIGHTNESS) subj_set(subj_brightness, SAVER_BRIGHTNESS);
    if (lv_subject_get_int(subj_screen_timeout) > SAVER_TIMEOUT_S) subj_set(subj_screen_timeout, SAVER_TIMEOUT_S);
  } else {
    subj_set(subj_wifi_enabled, p.getInt("wifi", lv_subject_get_int(subj_wifi_enabled)));
    subj_set(subj_aod, p.getInt("aod", lv_subject_get_int(subj_aod)));
    subj_set(subj_brightness, p.getInt("bright", lv_subject_get_int(subj_brightness)));
    subj_set(subj_screen_timeout, p.getInt("timeout", lv_subject_get_int(subj_screen_timeout)));
  }
  p.end();
}

void battery_saver_init() {
  lv_subject_add_observer(subj_saver, saver_obs, NULL);
}
