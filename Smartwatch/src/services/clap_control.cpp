/*
 * clap_control.cpp - Glue between the microphone listener (audio.cpp) and the WLED
 * lights: turns the listener on when it can be useful and toggles all lights on a
 * double clap.
 */
#include "clap_control.h"

#include <Arduino.h>
#include <lvgl.h>
#include "../core/settings.h"
#include "../core/system.h"
#include "../drivers/audio.h"
#include "wled.h"

#define CLAP_POLL_MS 100

static bool clap_listening;

static void clap_update() {
  bool want = lv_subject_get_int(subj_ext_clap) && lv_subject_get_int(subj_ext_lights) && wled_count() > 0 &&
              lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTED;
  if (want == clap_listening) return;
  clap_listening = want;
  audio_listen(want);
}

static void clap_settings_obs(lv_observer_t *observer, lv_subject_t *subject) {
  clap_update();
}

static void clap_poll_cb(lv_timer_t *t) {
  if (!audio_take_clap() || !clap_listening) return;
  bool on = !wled_any_on();
  wled_toggle_all();
  system_message(on ? "Lights on" : "Lights off");
}

void clap_control_init() {
  lv_subject_add_observer(subj_ext_clap, clap_settings_obs, NULL);
  lv_subject_add_observer(subj_ext_lights, clap_settings_obs, NULL);
  lv_subject_add_observer(subj_wifi_state, clap_settings_obs, NULL);
  lv_subject_add_observer(subj_lights, clap_settings_obs, NULL);
  lv_timer_create(clap_poll_cb, CLAP_POLL_MS, NULL);
}

bool clap_control_listening() {
  return clap_listening;
}
