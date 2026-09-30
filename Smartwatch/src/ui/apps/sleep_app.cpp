/*
 * sleep_app.cpp - Sleep: last night (duration, calm / restless / awake, a hypnogram), the
 * last 7 nights, and the bedtime mode schedule. Data: services/sleep_tracker.cpp.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/format.h"
#include "../../core/settings.h"
#include "../../core/system.h"
#include "../../drivers/audio.h"
#include "../../services/clock.h"
#include "../../services/sleep_tracker.h"
#include "../kit/kit.h"

static const uint32_t SLEEP_COLORS[3] = { 0x5E5CE6, 0x64D2FF, KIT_COLOR_ORANGE };  // calm, restless, awake

static lv_obj_t *sleep_hypnogram(lv_obj_t *parent, const uint8_t *bins, uint16_t n) {
  lv_obj_t *bar = kit_container(parent);
  lv_obj_set_size(bar, LV_PCT(100), 34);
  lv_obj_set_style_radius(bar, 8, 0);
  lv_obj_set_style_clip_corner(bar, true, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(KIT_COLOR_CARD2), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  if (!n) return bar;
  lv_obj_update_layout(parent);
  int32_t w = lv_obj_get_content_width(parent);
  // One block per run of equal bins; restless and awake are drawn shorter, calm full height.
  static const int32_t HEIGHTS[3] = { 34, 22, 12 };
  for (uint16_t i = 0; i < n;) {
    uint16_t j = i;
    while (j < n && bins[j] == bins[i]) j++;
    int32_t x0 = (int32_t)i * w / n, x1 = (int32_t)j * w / n;
    uint8_t c = bins[i] > SLEEP_AWAKE_C ? (uint8_t)SLEEP_AWAKE_C : bins[i];
    lv_obj_t *seg = kit_container(bar);
    lv_obj_set_size(seg, max(x1 - x0, (int32_t)2), HEIGHTS[c]);
    lv_obj_set_pos(seg, x0, 34 - HEIGHTS[c]);
    lv_obj_set_style_bg_color(seg, lv_color_hex(SLEEP_COLORS[c]), 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    i = j;
  }
  return bar;
}

static void sleep_legend(lv_obj_t *parent, uint32_t color, const char *name, uint32_t minutes) {
  lv_obj_t *row = kit_row_box(parent, LV_FLEX_ALIGN_START, 10);
  lv_obj_t *dot = kit_container(row);
  lv_obj_set_size(dot, 12, 12);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_t *label = kit_label(row, KIT_FONT_BODY, 0xFFFFFF, name);
  lv_obj_set_flex_grow(label, 1);
  char buf[24];
  format_duration(minutes, buf, sizeof(buf));
  kit_label(row, KIT_FONT_BODY, KIT_COLOR_TEXT2, buf);
}

static void sleep_week_chart(lv_obj_t *parent) {
  kit_section(parent, "LAST 7 NIGHTS");
  lv_obj_t *card = kit_info_card(parent);
  lv_obj_t *chart = kit_container(card);
  lv_obj_set_size(chart, LV_PCT(100), 110);
  lv_obj_set_flex_flow(chart, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chart, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  static const char *const DAYS[] = { "S", "M", "T", "W", "T", "F", "S" };
  uint32_t total = 0;
  int counted = 0;
  for (int i = SLEEP_NIGHTS - 1; i >= 0; i--) {
    lv_obj_t *col = kit_col_box(chart, LV_FLEX_ALIGN_CENTER, 6);
    uint32_t asleep = 0;
    const char *day = "";
    const SleepNight *night = sleep_night(i);
    if (night) {
      asleep = night->calm_min + night->restless_min;
      time_t end = night->end;
      struct tm t;
      localtime_r(&end, &t);
      day = DAYS[t.tm_wday];
      total += asleep;
      counted++;
    }
    lv_obj_t *bar = kit_container(col);
    lv_obj_set_size(bar, 18, max((int32_t)6, (int32_t)(min(asleep, (uint32_t)600) * 76 / 600)));
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(asleep >= 420 ? 0x5E5CE6 : asleep ? 0x3A3A8C : KIT_COLOR_CARD3), 0);
    kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, day);
  }
  if (counted) {
    char avg[24], text[48];
    format_duration(total / counted, avg, sizeof(avg));
    snprintf(text, sizeof(text), "Average %s a night", avg);
    kit_label(card, KIT_FONT_SMALL, KIT_COLOR_TEXT2, text);
  }
}

/* ================================ Bedtime schedule ================================ */

static lv_subject_t *sleep_pick_subject;
static lv_obj_t *sleep_pick_h, *sleep_pick_m;

static void sleep_pick_save_cb(lv_event_t *e) {
  audio_click();
  int32_t v = lv_roller_get_selected(sleep_pick_h) * 60 + lv_roller_get_selected(sleep_pick_m) * 5;
  subj_set(sleep_pick_subject, v);
  kit_page_pop();
}

// Time picker for the schedule: hours and minutes in 5-minute steps.
static void sleep_pick_open(const char *title, lv_subject_t *subject) {
  static char hours[24 * 3 + 1], minutes[12 * 3 + 1];
  if (!hours[0]) {
    char *p = hours;
    for (int i = 0; i < 24; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i);
    p = minutes;
    for (int i = 0; i < 12; i++) p += sprintf(p, i ? "\n%02d" : "%02d", i * 5);
  }
  sleep_pick_subject = subject;
  int32_t v = lv_subject_get_int(subject);
  lv_obj_t *page = kit_page_create(title, true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *row = kit_row_box(c, LV_FLEX_ALIGN_CENTER, 10);
  sleep_pick_h = kit_roller(row, hours, v / 60, 104);
  kit_label(row, KIT_FONT_TITLE, 0xFFFFFF, ":");
  sleep_pick_m = kit_roller(row, minutes, v % 60 / 5, 104);
  lv_obj_t *save = kit_button(c, "Save", KIT_COLOR_INDIGO);
  lv_obj_set_style_margin_top(save, 10, 0);
  lv_obj_add_event_cb(save, sleep_pick_save_cb, LV_EVENT_CLICKED, NULL);
  kit_page_push(page);
}

static void sleep_from_cb(lv_event_t *e) {
  audio_click();
  sleep_pick_open("Bedtime", subj_bed_from);
}

static void sleep_to_cb(lv_event_t *e) {
  audio_click();
  sleep_pick_open("Wake up", subj_bed_to);
}

static void sleep_time_obs(lv_observer_t *observer, lv_subject_t *subject) {
  int32_t v = lv_subject_get_int(subject);
  char buf[16];
  clock_format_hm(v / 60, v % 60, buf, sizeof(buf));
  lv_label_set_text(lv_observer_get_target_obj(observer), buf);
}

static void sleep_now_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), lv_subject_get_int(subject) ? "On now" : "Off");
}

static void sleep_now_cb(lv_event_t *e) {
  audio_click();
  bedtime_set(!lv_subject_get_int(subj_bedtime_on));
}

static void sleep_bedtime_rows(lv_obj_t *c) {
  kit_section(c, "BEDTIME MODE");
  lv_obj_t *now = kit_row(c, ICON_BED, KIT_COLOR_INDIGO, "Bedtime mode", "", false);
  lv_obj_t *sw = kit_switch(now);
  lv_obj_bind_checked(sw, subj_bedtime_on);
  lv_subject_add_observer_obj(subj_bedtime_on, sleep_now_obs, kit_row_subtitle(now), NULL);
  lv_obj_add_event_cb(now, sleep_now_cb, LV_EVENT_CLICKED, NULL);
  kit_switch_row(c, ICON_CLOCK, 0x3A3A8C, "Schedule", "Every night", subj_bedtime);
  lv_obj_t *from = kit_row(c, ICON_MOON, 0x3A3A8C, "Bedtime", "", true);
  lv_subject_add_observer_obj(subj_bed_from, sleep_time_obs, kit_row_subtitle(from), NULL);
  lv_obj_add_event_cb(from, sleep_from_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *to = kit_row(c, ICON_SUN, KIT_COLOR_ORANGE, "Wake up", "", true);
  lv_subject_add_observer_obj(subj_bed_to, sleep_time_obs, kit_row_subtitle(to), NULL);
  lv_obj_add_event_cb(to, sleep_to_cb, LV_EVENT_CLICKED, NULL);
  kit_note(c, "During bedtime: do not disturb, the screen stays dark (no tap or raise to wake, no "
              "always-on display). Press the side button to see the time.");
}

/* ================================ Sleep app ======================================= */

void sleep_open() {
  lv_obj_t *page = kit_page_create("Sleep", true);
  lv_obj_t *c = kit_page_content(page);

  // The newest night: stored this morning, or what the night so far looks like.
  SleepNight live_night;
  uint8_t *live_bins = (uint8_t *)psram_malloc(SLEEP_MAX_BINS);
  uint16_t live_bins_n = 0;
  bool not_worn = false;
  bool live = live_bins && sleep_analyze_now(&live_night, live_bins, &live_bins_n, &not_worn);
  const SleepNight *night = NULL;
  const uint8_t *bins = NULL;
  uint16_t bins_n = 0;
  const SleepNight *stored = sleep_night(0);
  bool recent_stored = stored && time(NULL) - (time_t)stored->end < 20 * 3600;
  if (recent_stored) {
    night = stored;
    bins = sleep_last_bins(&bins_n);
  } else if (live) {
    night = &live_night;
    bins = live_bins;
    bins_n = live_bins_n;
  }

  if (!night) {
    kit_empty_state(c, ICON_BED, not_worn ? "The watch did not seem to be worn last night."
                                          : "No sleep recorded yet. Wear the watch to bed; your night appears here in the morning.");
  } else {
    lv_obj_t *head = kit_row_box(c, LV_FLEX_ALIGN_START, 14);
    kit_icon(head, ICON_BED, 0x5E5CE6, 56);
    lv_obj_t *col = kit_col_box(head, LV_FLEX_ALIGN_START, 0);
    char dur[24], span[40], a[16], b[16];
    format_duration(night->calm_min + night->restless_min, dur, sizeof(dur));
    kit_label(col, KIT_FONT_TITLE, 0xFFFFFF, dur);
    time_t start = night->start, end = night->end;
    struct tm ts, te;
    localtime_r(&start, &ts);
    localtime_r(&end, &te);
    clock_format_hm(ts.tm_hour, ts.tm_min, a, sizeof(a));
    clock_format_hm(te.tm_hour, te.tm_min, b, sizeof(b));
    snprintf(span, sizeof(span), "%s \xE2\x80\x93 %s", a, b);
    kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, span);

    lv_obj_t *card = kit_info_card(c);
    lv_obj_set_style_pad_row(card, 8, 0);
    sleep_hypnogram(card, bins, bins_n);
    sleep_legend(card, SLEEP_COLORS[0], "Calm", night->calm_min);
    sleep_legend(card, SLEEP_COLORS[1], "Restless", night->restless_min);
    sleep_legend(card, SLEEP_COLORS[2], "Awake", night->awake_min);
    if (!recent_stored) kit_note(c, "The night is still going on: it is saved once you are up.");
  }
  psram_free(live_bins);
  if (sleep_night_count() > 0) sleep_week_chart(c);
  sleep_bedtime_rows(c);
  kit_note(c, "Estimated from wrist movement, without sleep stages.");
  kit_page_push(page);
}
