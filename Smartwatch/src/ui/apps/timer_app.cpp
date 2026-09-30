/*
 * timer_app.cpp - Countdown timer: presets (1 min ... 1 h) or a custom time, the running
 * view (ring, pause, cancel, +1:00) and the full-screen alert when it ends. The countdown
 * itself runs in the background (services/countdown.cpp): the page can be closed.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../drivers/audio.h"
#include "../../services/countdown.h"
#include "../kit/kit.h"

#define TIMER_RING_MS    60000
#define TIMER_VIEW_MS    200

static const uint32_t TIMER_PRESETS_S[] = { 60, 180, 300, 600, 900, 1800, 2700, 3600 };
static const char *const TIMER_PRESET_LABELS[] = { "1 min", "3 min", "5 min", "10 min", "15 min", "30 min", "45 min", "1 hour" };

static lv_timer_t *tmr_ring_timer;
static lv_obj_t *tmr_page;        // open timer page
static lv_obj_t *tmr_ring, *tmr_label, *tmr_total_label, *tmr_pause_btn;
static lv_obj_t *tmr_custom_h, *tmr_custom_m, *tmr_custom_s;
static CountdownState tmr_shown_state = COUNTDOWN_IDLE;

static void tmr_format(uint32_t ms, char *buf, size_t len) {
  uint32_t s = (ms + 999) / 1000;  // round up: shows 00:00 only at the very end
  if (s >= 3600) snprintf(buf, len, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else snprintf(buf, len, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void tmr_format_total(uint32_t ms, char *buf, size_t len) {
  uint32_t s = ms / 1000;
  if (s % 3600 == 0) snprintf(buf, len, "%u h timer", (unsigned)(s / 3600));
  else if (s >= 3600) snprintf(buf, len, "%u h %u min timer", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
  else if (s % 60 == 0) snprintf(buf, len, "%u min timer", (unsigned)(s / 60));
  else snprintf(buf, len, "%u:%02u timer", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void tmr_show_view();

/* ================================ Ringing ========================================= */

static void tmr_ring_stop() {
  audio_alert_stop();
  if (tmr_ring_timer) {
    lv_timer_delete(tmr_ring_timer);
    tmr_ring_timer = NULL;
  }
}

static void tmr_alert_cb(int button) {
  tmr_ring_stop();
  if (button == 1) {  // Restart
    countdown_start(countdown_total());
    tmr_show_view();
  }
}

static void tmr_ring_timeout_cb(lv_timer_t *t) {
  tmr_ring_timer = NULL;
  tmr_ring_stop();
  kit_alert_close();
}

static void tmr_done(uint32_t total_ms) {
  tmr_show_view();
  char subtitle[32];
  tmr_format_total(total_ms, subtitle, sizeof(subtitle));
  audio_alert_start(ALERT_TIMER);
  kit_alert(ICON_TIMER, KIT_COLOR_PURPLE, "Time's up", subtitle, "Stop", "Restart", tmr_alert_cb);
  tmr_ring_timer = lv_timer_create(tmr_ring_timeout_cb, TIMER_RING_MS, NULL);
  lv_timer_set_repeat_count(tmr_ring_timer, 1);
}

void timer_app_init() {
  countdown_set_done_handler(tmr_done);
}

/* ================================ Page ============================================ */

static void tmr_update_view(lv_timer_t *t) {
  if (!tmr_label) return;
  uint32_t left = countdown_remaining();
  uint32_t total = countdown_total();
  char buf[16];
  tmr_format(left, buf, sizeof(buf));
  lv_label_set_text(tmr_label, buf);
  lv_arc_set_value(tmr_ring, total ? (int32_t)((uint64_t)left * 1000 / total) : 0);
}

static void tmr_preset_cb(lv_event_t *e) {
  audio_click();
  countdown_start(TIMER_PRESETS_S[(int)(intptr_t)lv_event_get_user_data(e)] * 1000);
  tmr_show_view();
}

static void tmr_pause_cb(lv_event_t *e) {
  audio_click();
  if (countdown_state() == COUNTDOWN_RUNNING) countdown_pause();
  else countdown_resume();
  bool running = countdown_state() == COUNTDOWN_RUNNING;
  lv_label_set_text(tmr_pause_btn, running ? ICON_PAUSE : ICON_PLAY);
  lv_obj_set_style_bg_color(tmr_pause_btn, lv_color_hex(running ? KIT_COLOR_ORANGE : KIT_COLOR_GREEN), 0);
  tmr_update_view(NULL);
}

static void tmr_cancel_cb(lv_event_t *e) {
  audio_click();
  countdown_cancel();
  tmr_show_view();
}

static void tmr_add_minute_cb(lv_event_t *e) {
  audio_click();
  countdown_add(60000);
  char buf[32];
  tmr_format_total(countdown_total(), buf, sizeof(buf));
  lv_label_set_text(tmr_total_label, buf);
  tmr_update_view(NULL);
}

static void tmr_custom_start_cb(lv_event_t *e) {
  audio_click();
  uint32_t s = lv_roller_get_selected(tmr_custom_h) * 3600 + lv_roller_get_selected(tmr_custom_m) * 60 + lv_roller_get_selected(tmr_custom_s);
  if (!s) {
    kit_toast("Set a time first");
    return;
  }
  kit_page_pop();
  countdown_start(s * 1000);
  tmr_show_view();
}

static char *tmr_options(char *buf, size_t len, int count) {
  size_t used = 0;
  buf[0] = 0;
  for (int v = 0; v < count && used < len; v++) used += snprintf(buf + used, len - used, v ? "\n%02d" : "%02d", v);
  return buf;
}

static void tmr_custom_cb(lv_event_t *e) {
  static char hours[24 * 3 + 1], sixty[60 * 3 + 1];
  audio_click();
  lv_obj_t *page = kit_page_create("Custom timer", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *labels = kit_row_box(c, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  static const char *const UNITS[] = { "hours", "min", "sec" };
  for (const char *u : UNITS) {
    lv_obj_t *l = kit_label(labels, KIT_FONT_SMALL, KIT_COLOR_TEXT2, u);
    lv_obj_set_width(l, 96);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_t *row = kit_row_box(c, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  tmr_options(sixty, sizeof(sixty), 60);
  tmr_custom_h = kit_roller(row, tmr_options(hours, sizeof(hours), 24), 0, 96);
  tmr_custom_m = kit_roller(row, sixty, 5, 96);
  tmr_custom_s = kit_roller(row, sixty, 0, 96);
  lv_obj_t *start = kit_button(c, "Start", KIT_COLOR_PURPLE);
  lv_obj_set_style_margin_top(start, 10, 0);
  lv_obj_add_event_cb(start, tmr_custom_start_cb, LV_EVENT_CLICKED, NULL);
  kit_page_push(page);
}

static void tmr_build_presets(lv_obj_t *c) {
  lv_obj_t *grid = kit_container(c);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, 10, 0);
  for (int i = 0; i < 8; i++) {
    lv_obj_t *chip = kit_chip(grid, TIMER_PRESET_LABELS[i]);
    lv_obj_set_width(chip, LV_PCT(48));
    lv_obj_set_style_text_font(chip, KIT_FONT_TEXT, 0);
    lv_obj_add_event_cb(chip, tmr_preset_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  lv_obj_t *custom = kit_row(c, ICON_TIMER, KIT_COLOR_PURPLE, "Custom", "Hours, minutes, seconds", true);
  lv_obj_add_event_cb(custom, tmr_custom_cb, LV_EVENT_CLICKED, NULL);
}

static void tmr_build_running(lv_obj_t *c) {
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  tmr_ring = kit_ring(c, 270, 14, KIT_COLOR_PURPLE);
  lv_arc_set_range(tmr_ring, 0, 1000);
  lv_obj_t *col = kit_col_box(tmr_ring, LV_FLEX_ALIGN_CENTER, 4);
  lv_obj_center(col);
  tmr_label = kit_label(col, &font_num_64, 0xFFFFFF, "");
  char buf[32];
  tmr_format_total(countdown_total(), buf, sizeof(buf));
  tmr_total_label = kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, buf);
  lv_obj_t *plus = kit_chip(col, "+1:00");
  lv_obj_set_style_pad_ver(plus, 6, 0);
  lv_obj_set_style_pad_hor(plus, 14, 0);
  lv_obj_set_style_text_font(plus, KIT_FONT_SMALL, 0);
  lv_obj_set_style_bg_color(plus, lv_color_hex(KIT_COLOR_CARD2), 0);
  lv_obj_set_style_margin_top(plus, 6, 0);
  lv_obj_add_event_cb(plus, tmr_add_minute_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *buttons = kit_row_box(c, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  lv_obj_set_style_margin_top(buttons, 4, 0);
  lv_obj_t *cancel = kit_round_button(buttons, ICON_TIMES, KIT_COLOR_CARD2, 72);
  lv_obj_add_event_cb(cancel, tmr_cancel_cb, LV_EVENT_CLICKED, NULL);
  bool running = countdown_state() == COUNTDOWN_RUNNING;
  tmr_pause_btn = kit_round_button(buttons, running ? ICON_PAUSE : ICON_PLAY, running ? KIT_COLOR_ORANGE : KIT_COLOR_GREEN, 72);
  lv_obj_add_event_cb(tmr_pause_btn, tmr_pause_cb, LV_EVENT_CLICKED, NULL);
  tmr_update_view(NULL);
}

// Presets while idle, the countdown while running or paused.
static void tmr_view_build() {
  if (!tmr_page) return;
  lv_obj_t *c = kit_page_content(tmr_page);
  lv_obj_clean(c);
  tmr_ring = tmr_label = tmr_total_label = tmr_pause_btn = NULL;
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_scroll_to_y(c, 0, LV_ANIM_OFF);
  tmr_shown_state = countdown_state();
  if (tmr_shown_state == COUNTDOWN_IDLE) tmr_build_presets(c);
  else tmr_build_running(c);
}

static void tmr_view_async(void *user_data) {
  tmr_view_build();
}

// Rebuilt asynchronously: the button that caused it lives in the content being rebuilt.
static void tmr_show_view() {
  lv_async_call_cancel(tmr_view_async, NULL);
  if (tmr_page) lv_async_call(tmr_view_async, NULL);
}

static void tmr_page_timer_cb(lv_timer_t *t) {
  if ((countdown_state() == COUNTDOWN_IDLE) != (tmr_shown_state == COUNTDOWN_IDLE)) tmr_show_view();
  else tmr_update_view(NULL);
}

static void tmr_page_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(tmr_view_async, NULL);
  tmr_page = NULL;
  tmr_ring = tmr_label = tmr_total_label = tmr_pause_btn = NULL;
}

void timer_open() {
  if (tmr_page) return;
  tmr_page = kit_page_create("Timer", true);
  lv_obj_add_event_cb(tmr_page, tmr_page_deleted_cb, LV_EVENT_DELETE, NULL);
  kit_page_timer(tmr_page, tmr_page_timer_cb, TIMER_VIEW_MS, NULL);
  tmr_view_build();
  kit_page_push(tmr_page);
}
