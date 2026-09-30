/*
 * workout_app.cpp - Workout: pick an activity, a 3-2-1 countdown, the live screen (time,
 * distance, pace, steps with cadence, calories), pause, end and the summary; the history
 * of the last workouts. A running workout survives leaving its screen (the watch faces
 * show a running icon and the app goes straight back to it). Data: services/workout.cpp.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/board.h"
#include "../../core/format.h"
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../services/clock.h"
#include "../../services/workout.h"
#include "../kit/kit.h"
#include "../system/nav.h"
#include "../system/screen.h"

#define WO_TICK_MS 500

static const char *const WO_MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *const WO_NAMES[WO_TYPES] = { "Walk", "Run", "Hike", "Workout" };
static const char *const WO_ICONS[WO_TYPES] = { ICON_ACTIVITY, ICON_RUNNING, ICON_HIKING, ICON_DUMBBELL };
static const uint32_t WO_COLORS[WO_TYPES] = { KIT_COLOR_GREEN, KIT_COLOR_ORANGE, KIT_COLOR_TEAL, KIT_COLOR_PINK };

static lv_obj_t *wo_live, *wo_time, *wo_state, *wo_values[4], *wo_units[4], *wo_pause_btn, *wo_countdown;

static bool wo_imperial() {
  return lv_subject_get_int(subj_units) != 0;
}

/* ================================ Summary ========================================= */

static void wo_summary_done_cb(lv_event_t *e) {
  audio_click();
  nav_go_home(true);
}

static lv_obj_t *wo_stat(lv_obj_t *parent, const char *label, const char *value) {
  lv_obj_t *card = kit_info_card(parent);
  lv_obj_set_width(card, LV_PCT(48));
  lv_obj_set_style_pad_row(card, 0, 0);
  kit_label(card, KIT_FONT_SMALL, KIT_COLOR_TEXT2, label);
  kit_label(card, KIT_FONT_TEXT, 0xFFFFFF, value);
  return card;
}

static lv_obj_t *wo_build_summary(const WorkoutRecord &r, bool just_finished) {
  uint8_t type = r.type % WO_TYPES;
  lv_obj_t *page = kit_page_create(just_finished ? "Summary" : WO_NAMES[type], true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *head = kit_row_box(c, LV_FLEX_ALIGN_START, 14);
  kit_icon(head, WO_ICONS[type], WO_COLORS[type], 56);
  lv_obj_t *col = kit_col_box(head, LV_FLEX_ALIGN_START, 0);
  kit_label(col, KIT_FONT_TEXT, 0xFFFFFF, WO_NAMES[type]);
  char when[40], hm[16];
  time_t start = r.start;
  struct tm t;
  localtime_r(&start, &t);
  clock_format_hm(t.tm_hour, t.tm_min, hm, sizeof(hm));
  snprintf(when, sizeof(when), "%d %s, %s", t.tm_mday, WO_MONTHS[t.tm_mon], hm);
  kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, when);

  lv_obj_t *grid = kit_container(c);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, 10, 0);
  char buf[32], value[24];
  workout_format_duration(r.duration_s, buf, sizeof(buf));
  wo_stat(grid, "Time", buf);
  workout_format_distance(r.distance_m, value, sizeof(value));
  snprintf(buf, sizeof(buf), "%s %s", value, wo_imperial() ? "mi" : "km");
  wo_stat(grid, "Distance", buf);
  workout_format_pace(r.duration_s * 1000, r.distance_m, buf, sizeof(buf));
  wo_stat(grid, wo_imperial() ? "Pace /mi" : "Pace /km", buf);
  format_thousands(r.steps, buf, sizeof(buf));
  wo_stat(grid, "Steps", buf);
  snprintf(buf, sizeof(buf), "%u kcal", (unsigned)r.kcal);
  wo_stat(grid, "Calories", buf);
  snprintf(buf, sizeof(buf), "%u /min", r.duration_s ? (unsigned)(r.steps * 60 / r.duration_s) : 0U);
  wo_stat(grid, "Cadence", buf);

  if (just_finished) {
    if (r.duration_s < 60) kit_note(c, "Workouts shorter than a minute are not saved.");
    lv_obj_t *done = kit_button(c, "Done", KIT_COLOR_ACCENT);
    lv_obj_add_event_cb(done, wo_summary_done_cb, LV_EVENT_CLICKED, NULL);
  }
  return page;
}

/* ================================ Live screen ===================================== */

static void wo_update_live() {
  if (!wo_live) return;
  bool paused = lv_subject_get_int(subj_workout) == WORKOUT_PAUSED;
  uint32_t left;
  bool counting = workout_counting_down(&left);
  lv_obj_set_hidden(wo_countdown, !counting);
  if (counting) lv_label_set_text_fmt(wo_countdown, "%u", (unsigned)left);
  uint32_t ms = workout_elapsed_ms();
  uint8_t type = workout_type();
  char buf[24];
  workout_format_duration(ms / 1000, buf, sizeof(buf));
  lv_label_set_text(wo_time, buf);
  // A paused workout blinks its time.
  lv_obj_set_style_text_opa(wo_time, paused && (millis() / 600) % 2 ? LV_OPA_30 : LV_OPA_COVER, 0);
  lv_label_set_text(wo_state, paused ? "Paused" : counting ? "Get ready" : WO_NAMES[type]);

  uint32_t steps = workout_steps();
  uint32_t m = workout_distance_m(type, steps);
  workout_format_distance(m, buf, sizeof(buf));
  lv_label_set_text(wo_values[0], buf);
  lv_label_set_text(wo_units[0], wo_imperial() ? "mi" : "km");
  workout_format_pace(ms, m, buf, sizeof(buf));
  lv_label_set_text(wo_values[1], buf);
  lv_label_set_text(wo_units[1], wo_imperial() ? "pace /mi" : "pace /km");
  format_thousands(steps, buf, sizeof(buf));
  lv_label_set_text(wo_values[2], buf);
  lv_label_set_text_fmt(wo_units[2], "steps  \xE2\x80\xA2  %u/min", (unsigned)workout_cadence());
  lv_label_set_text_fmt(wo_values[3], "%u", (unsigned)workout_kcal(type, ms));
  lv_label_set_text(wo_pause_btn, paused ? ICON_PLAY : ICON_PAUSE);
}

static void wo_tick_cb(lv_timer_t *t) {
  if (!screen_is_off()) wo_update_live();
}

static void wo_pause_cb(lv_event_t *e) {
  audio_click();
  workout_pause_toggle();
  wo_update_live();
}

static void wo_end_confirmed(void *user_data) {
  WorkoutRecord r = workout_finish();
  audio_beep(1047, 150);
  lv_obj_t *live = wo_live;
  kit_page_push(wo_build_summary(r, true));
  if (live) kit_page_remove(live);  // back from the summary skips the finished workout
}

static void wo_end_cb(lv_event_t *e) {
  audio_click();
  kit_confirm("End workout?", NULL, "End", true, wo_end_confirmed, NULL);
}

static void wo_live_deleted_cb(lv_event_t *e) {
  wo_live = NULL;
}

static void wo_metric(lv_obj_t *parent, int i, uint32_t color) {
  lv_obj_t *col = kit_col_box(parent, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_width(col, LV_PCT(50));
  wo_values[i] = kit_label(col, &font_num_44, color, "");
  wo_units[i] = kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
}

static void wo_open_live() {
  uint8_t type = workout_type();
  lv_obj_t *page = kit_page_create_bare(0x000000);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(page, 14, 0);
  lv_obj_set_style_pad_bottom(page, 22, 0);

  lv_obj_t *head = kit_row_box(page, LV_FLEX_ALIGN_CENTER, 10);
  kit_label(head, &font_icons_24, WO_COLORS[type], WO_ICONS[type]);
  wo_state = kit_label(head, KIT_FONT_BODY, WO_COLORS[type], "");
  wo_time = kit_label(page, &font_num_64, 0xFFFFFF, "");

  lv_obj_t *grid = kit_container(page);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_hor(grid, 10, 0);
  wo_metric(grid, 0, 0xFFFFFF);
  wo_metric(grid, 1, 0xFFFFFF);
  wo_metric(grid, 2, 0xFFFFFF);
  wo_metric(grid, 3, KIT_COLOR_ORANGE);
  lv_label_set_text(wo_units[3], "kcal");

  lv_obj_t *buttons = kit_row_box(page, LV_FLEX_ALIGN_CENTER, 34);
  lv_obj_set_style_margin_top(buttons, 8, 0);
  lv_obj_t *end = kit_round_button(buttons, ICON_STOP, KIT_COLOR_RED, 64);
  lv_obj_add_event_cb(end, wo_end_cb, LV_EVENT_CLICKED, NULL);
  wo_pause_btn = kit_round_button(buttons, ICON_PAUSE, KIT_COLOR_ORANGE, 64);
  lv_obj_add_event_cb(wo_pause_btn, wo_pause_cb, LV_EVENT_CLICKED, NULL);

  // 3-2-1 before the clock starts.
  wo_countdown = kit_label(page, &font_light_180, 0xFFFFFF, "3");
  lv_obj_set_floating(wo_countdown, true);
  lv_obj_set_size(wo_countdown, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(wo_countdown, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(wo_countdown, LV_OPA_COVER, 0);
  lv_obj_set_style_text_align(wo_countdown, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(wo_countdown, (LCD_HEIGHT - 129) / 2, 0);

  wo_live = page;
  lv_obj_add_event_cb(page, wo_live_deleted_cb, LV_EVENT_DELETE, NULL);
  kit_page_timer(page, wo_tick_cb, WO_TICK_MS, NULL);
  wo_update_live();
  kit_open(page, KIT_ANIM_SLIDE, true);
}

/* ================================ Start page ====================================== */

static void wo_type_cb(lv_event_t *e) {
  audio_click();
  lv_obj_t *list = lv_obj_get_screen(lv_event_get_current_target_obj(e));
  if (!workout_active()) workout_start((WorkoutType)(intptr_t)lv_event_get_user_data(e));
  wo_open_live();
  kit_page_remove(list);  // back from the workout leads to where the app was opened
}

static void wo_history_item_cb(lv_event_t *e) {
  audio_click();
  const WorkoutRecord *r = workout_history((int)(intptr_t)lv_event_get_user_data(e));
  if (r) kit_page_push(wo_build_summary(*r, false));
}

static void wo_history_cb(lv_event_t *e) {
  audio_click();
  lv_obj_t *page = kit_page_create("History", true);
  lv_obj_t *c = kit_page_content(page);
  int n = workout_history_count();
  if (!n) kit_empty_state(c, ICON_HISTORY, "No workouts yet.");
  for (int i = 0; i < n; i++) {
    const WorkoutRecord &r = *workout_history(i);
    uint8_t type = r.type % WO_TYPES;
    char title[40], sub[48], dur[16], dist[16];
    time_t start = r.start;
    struct tm t;
    localtime_r(&start, &t);
    snprintf(title, sizeof(title), "%s  \xE2\x80\xA2  %d %s", WO_NAMES[type], t.tm_mday, WO_MONTHS[t.tm_mon]);
    workout_format_duration(r.duration_s, dur, sizeof(dur));
    workout_format_distance(r.distance_m, dist, sizeof(dist));
    snprintf(sub, sizeof(sub), "%s  \xE2\x80\xA2  %s %s", dur, dist, wo_imperial() ? "mi" : "km");
    lv_obj_t *row = kit_row(c, WO_ICONS[type], WO_COLORS[type], title, sub, true);
    lv_obj_add_event_cb(row, wo_history_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  kit_page_push(page);
}

void workout_open() {
  if (workout_active()) {
    if (!wo_live) wo_open_live();
    return;
  }
  lv_obj_t *page = kit_page_create("Workout", true);
  lv_obj_t *c = kit_page_content(page);
  static const char *const SUBTITLES[WO_TYPES] = { "Steps, distance, pace", "Steps, distance, pace", "Steps, distance", "Time and calories" };
  for (int i = 0; i < WO_TYPES; i++) {
    lv_obj_t *row = kit_row(c, WO_ICONS[i], WO_COLORS[i], WO_NAMES[i], SUBTITLES[i], false);
    lv_obj_add_event_cb(row, wo_type_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  char last[48] = "No workouts yet";
  if (workout_history_count()) {
    const WorkoutRecord *r = workout_history(0);
    char dur[16];
    workout_format_duration(r->duration_s, dur, sizeof(dur));
    snprintf(last, sizeof(last), "Last: %s, %s", WO_NAMES[r->type % WO_TYPES], dur);
  }
  lv_obj_t *history = kit_row(c, ICON_HISTORY, KIT_COLOR_GRAY, "History", last, true);
  lv_obj_add_event_cb(history, wo_history_cb, LV_EVENT_CLICKED, NULL);
  kit_note(c, "Distance and calories are estimated from your steps.");
  kit_page_push(page);
}
