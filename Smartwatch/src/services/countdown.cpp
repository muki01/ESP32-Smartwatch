/*
 * countdown.cpp - Countdown timer on millis(); a 200 ms LVGL timer watches for the end
 * only while it runs.
 */
#include "countdown.h"

#include <Arduino.h>
#include <lvgl.h>

#define COUNTDOWN_TICK_MS 200

static CountdownState cd_state = COUNTDOWN_IDLE;
static uint32_t cd_total_ms;
static uint32_t cd_end_ms;      // millis() at zero (running)
static uint32_t cd_left_ms;     // remaining time (paused)
static lv_timer_t *cd_timer;
static countdown_done_cb_t cd_done;

static void cd_tick_cb(lv_timer_t *t) {
  if (cd_state != COUNTDOWN_RUNNING || countdown_remaining() > 0) return;
  cd_state = COUNTDOWN_IDLE;
  lv_timer_pause(cd_timer);
  if (cd_done) cd_done(cd_total_ms);
}

void countdown_init() {
  cd_timer = lv_timer_create(cd_tick_cb, COUNTDOWN_TICK_MS, NULL);
  lv_timer_pause(cd_timer);
}

void countdown_set_done_handler(countdown_done_cb_t cb) {
  cd_done = cb;
}

void countdown_start(uint32_t ms) {
  cd_total_ms = ms;
  cd_end_ms = millis() + ms;
  cd_state = COUNTDOWN_RUNNING;
  lv_timer_resume(cd_timer);
}

void countdown_pause() {
  if (cd_state != COUNTDOWN_RUNNING) return;
  cd_left_ms = countdown_remaining();
  cd_state = COUNTDOWN_PAUSED;
  lv_timer_pause(cd_timer);
}

void countdown_resume() {
  if (cd_state != COUNTDOWN_PAUSED) return;
  cd_end_ms = millis() + cd_left_ms;
  cd_state = COUNTDOWN_RUNNING;
  lv_timer_resume(cd_timer);
}

void countdown_cancel() {
  cd_state = COUNTDOWN_IDLE;
  lv_timer_pause(cd_timer);
}

void countdown_add(uint32_t ms) {
  cd_total_ms += ms;
  if (cd_state == COUNTDOWN_RUNNING) cd_end_ms += ms;
  else if (cd_state == COUNTDOWN_PAUSED) cd_left_ms += ms;
}

CountdownState countdown_state() {
  return cd_state;
}

uint32_t countdown_remaining() {
  if (cd_state == COUNTDOWN_RUNNING) return (int32_t)(cd_end_ms - millis()) > 0 ? cd_end_ms - millis() : 0;
  if (cd_state == COUNTDOWN_PAUSED) return cd_left_ms;
  return 0;
}

uint32_t countdown_total() {
  return cd_total_ms;
}
