/*
 * stopwatch_app.cpp - Stopwatch with laps.
 *
 * Keeps running in the background when the page is closed (it is only arithmetic on
 * millis()). The display refreshes at 25 fps while the page is open and the stopwatch
 * runs; the screen stays on meanwhile. Laps show the lap time and the total; the fastest
 * lap is green, the slowest red.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../drivers/audio.h"
#include "../kit/kit.h"
#include "../system/screen.h"

#define SW_MAX_LAPS    99
#define SW_REFRESH_MS  40

static bool sw_running;
static uint32_t sw_start_ms;           // millis() at the last start
static uint32_t sw_accum_ms;           // time before the last start
static uint32_t sw_laps[SW_MAX_LAPS];  // total time at each lap
static int sw_lap_n;
static lv_obj_t *sw_page, *sw_time, *sw_hundredths, *sw_left_btn, *sw_right_btn, *sw_laps_box;

static uint32_t sw_elapsed() {
  return sw_accum_ms + (sw_running ? millis() - sw_start_ms : 0);
}

static void sw_format(uint32_t ms, char *buf, size_t len, bool hundredths) {
  uint32_t s = ms / 1000;
  if (hundredths) snprintf(buf, len, "%02u:%02u.%02u", (unsigned)(s / 60 % 100), (unsigned)(s % 60), (unsigned)(ms / 10 % 100));
  else if (s >= 3600) snprintf(buf, len, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else snprintf(buf, len, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void sw_update_time() {
  if (!sw_time) return;
  uint32_t ms = sw_elapsed();
  uint32_t s = ms / 1000;
  if (s >= 3600) lv_label_set_text_fmt(sw_time, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else lv_label_set_text_fmt(sw_time, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  lv_label_set_text_fmt(sw_hundredths, ".%02u", (unsigned)(ms / 10 % 100));
}

static void sw_update_buttons() {
  if (!sw_left_btn) return;
  bool idle = !sw_running && sw_elapsed() == 0;
  lv_label_set_text(sw_right_btn, sw_running ? ICON_PAUSE : ICON_PLAY);
  lv_obj_set_style_bg_color(sw_right_btn, lv_color_hex(sw_running ? KIT_COLOR_RED : KIT_COLOR_GREEN), 0);
  lv_label_set_text(sw_left_btn, sw_running || idle ? ICON_FLAG : ICON_UNDO);
  lv_obj_set_disabled(sw_left_btn, idle);
  lv_obj_set_style_opa(sw_left_btn, idle ? LV_OPA_40 : LV_OPA_COVER, 0);
}

static void sw_fill_laps() {
  if (!sw_laps_box) return;
  lv_obj_clean(sw_laps_box);
  if (!sw_lap_n) return;
  // Fastest and slowest lap (only meaningful from two laps on).
  int fast = -1, slow = -1;
  uint32_t fast_ms = UINT32_MAX, slow_ms = 0;
  for (int i = 0; i < sw_lap_n; i++) {
    uint32_t lap = sw_laps[i] - (i ? sw_laps[i - 1] : 0);
    if (lap < fast_ms) fast_ms = lap, fast = i;
    if (lap > slow_ms) slow_ms = lap, slow = i;
  }
  lv_obj_t *card = kit_info_card(sw_laps_box);
  lv_obj_set_style_pad_row(card, 10, 0);
  for (int i = sw_lap_n - 1; i >= 0; i--) {
    uint32_t lap = sw_laps[i] - (i ? sw_laps[i - 1] : 0);
    char lap_text[20], total[20], name[16];
    sw_format(lap, lap_text, sizeof(lap_text), true);
    sw_format(sw_laps[i], total, sizeof(total), true);
    snprintf(name, sizeof(name), "Lap %d", i + 1);
    uint32_t color = sw_lap_n > 1 && i == fast ? KIT_COLOR_GREEN : sw_lap_n > 1 && i == slow ? KIT_COLOR_RED : 0xFFFFFF;
    lv_obj_t *row = kit_row_box(card, LV_FLEX_ALIGN_SPACE_BETWEEN, 8);
    kit_label(row, KIT_FONT_SMALL, KIT_COLOR_TEXT2, name);
    kit_label(row, KIT_FONT_BODY, color, lap_text);
    kit_label(row, KIT_FONT_SMALL, KIT_COLOR_TEXT2, total);
  }
}

static void sw_tick_cb(lv_timer_t *t) {
  if (sw_running) sw_update_time();
}

static void sw_right_cb(lv_event_t *e) {
  audio_click();
  if (sw_running) {
    sw_accum_ms += millis() - sw_start_ms;
    sw_running = false;
    screen_keep_awake(false);
  } else {
    sw_start_ms = millis();
    sw_running = true;
    screen_keep_awake(true);  // like on a phone: the screen stays on while it runs
  }
  sw_update_time();
  sw_update_buttons();
}

static void sw_left_cb(lv_event_t *e) {
  audio_click();
  if (sw_running) {
    if (sw_lap_n < SW_MAX_LAPS) sw_laps[sw_lap_n++] = sw_elapsed();
  } else {
    sw_accum_ms = 0;
    sw_lap_n = 0;
    sw_update_time();
  }
  sw_update_buttons();
  sw_fill_laps();
}

static void sw_page_deleted_cb(lv_event_t *e) {
  sw_page = sw_time = sw_hundredths = sw_left_btn = sw_right_btn = sw_laps_box = NULL;
  if (sw_running) screen_keep_awake(false);  // leaving the app releases the screen
}

void stopwatch_open() {
  if (sw_page) return;
  sw_page = kit_page_create("Stopwatch", true);
  lv_obj_t *c = kit_page_content(sw_page);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *time_row = kit_container(c);
  lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_top(time_row, 26, 0);
  lv_obj_set_style_pad_bottom(time_row, 18, 0);
  sw_time = kit_label(time_row, &font_num_64, 0xFFFFFF, "00:00");
  sw_hundredths = kit_label(time_row, &font_num_44, KIT_COLOR_TEAL, ".00");
  lv_obj_set_style_pad_bottom(sw_hundredths, 4, 0);

  lv_obj_t *buttons = kit_row_box(c, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  sw_left_btn = kit_round_button(buttons, ICON_FLAG, KIT_COLOR_CARD2, 80);
  lv_obj_add_event_cb(sw_left_btn, sw_left_cb, LV_EVENT_CLICKED, NULL);
  sw_right_btn = kit_round_button(buttons, ICON_PLAY, KIT_COLOR_GREEN, 80);
  lv_obj_add_event_cb(sw_right_btn, sw_right_cb, LV_EVENT_CLICKED, NULL);

  sw_laps_box = kit_container(c);
  lv_obj_set_size(sw_laps_box, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(sw_laps_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_top(sw_laps_box, 8, 0);

  sw_update_time();
  sw_update_buttons();
  sw_fill_laps();
  kit_page_timer(sw_page, sw_tick_cb, SW_REFRESH_MS, NULL);
  lv_obj_add_event_cb(sw_page, sw_page_deleted_cb, LV_EVENT_DELETE, NULL);
  if (sw_running) screen_keep_awake(true);
  kit_page_push(sw_page);  // if navigation is busy the page is dropped and the delete callback cleans up
}
