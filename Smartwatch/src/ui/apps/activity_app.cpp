/*
 * activity_app.cpp - Activity: steps with the daily goal ring, distance, calories, active
 * minutes, the last 7 days, the goal and the move reminder. Also the Activity tile next
 * to the watch face. Data: services/activity.cpp.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/format.h"
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../drivers/imu.h"
#include "../../services/activity.h"
#include "../faces/faces.h"
#include "../kit/kit.h"

static void act_format_distance(char *buf, size_t len) {
  uint32_t m = activity_distance_m();
  if (lv_subject_get_int(subj_units)) snprintf(buf, len, "%.2f", m / 1609.34f);
  else snprintf(buf, len, "%u.%02u", (unsigned)(m / 1000), (unsigned)(m % 1000 / 10));
}

// Ring with the step count in the middle; the observers keep it current.
static void act_ring_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *ring = lv_observer_get_target_obj(observer);
  lv_obj_t *col = lv_obj_get_child(ring, 0);
  int32_t goal = lv_subject_get_int(subj_step_goal);
  int32_t steps = lv_subject_get_int(subj_steps);
  lv_arc_set_value(ring, goal > 0 ? min(100, (int)((int64_t)steps * 100 / goal)) : 0);
  char buf[24];
  format_thousands(steps, buf, sizeof(buf));
  lv_label_set_text(lv_obj_get_child(col, 1), buf);
  format_thousands(goal, buf, sizeof(buf));
  lv_label_set_text_fmt(lv_obj_get_child(col, 2), "of %s", buf);
}

static lv_obj_t *act_ring(lv_obj_t *parent, int32_t size, int32_t width) {
  lv_obj_t *ring = kit_ring(parent, size, width, KIT_COLOR_GREEN);
  lv_obj_t *col = kit_col_box(ring, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_center(col);
  kit_label(col, &font_icons_32, KIT_COLOR_GREEN, ICON_STEPS);
  kit_label(col, &font_num_44, 0xFFFFFF, "0");
  kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
  lv_subject_add_observer_obj(subj_steps, act_ring_obs, ring, NULL);
  lv_subject_add_observer_obj(subj_step_goal, act_ring_obs, ring, NULL);
  return ring;
}

static void act_stats_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *row = lv_observer_get_target_obj(observer);
  char buf[16];
  act_format_distance(buf, sizeof(buf));
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(row, 0), 1), buf);
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(row, 0), 2), lv_subject_get_int(subj_units) ? "mi" : "km");
  lv_label_set_text_fmt(lv_obj_get_child(lv_obj_get_child(row, 1), 1), "%u", (unsigned)activity_kcal());
  lv_label_set_text_fmt(lv_obj_get_child(lv_obj_get_child(row, 2), 1), "%u", (unsigned)activity_active_min());
}

static void act_stat(lv_obj_t *parent, const char *icon, uint32_t color, const char *unit) {
  lv_obj_t *col = kit_col_box(parent, LV_FLEX_ALIGN_CENTER, 2);
  lv_obj_set_flex_grow(col, 1);
  kit_label(col, &font_icons_24, color, icon);
  kit_label(col, KIT_FONT_TEXT, 0xFFFFFF, "0");
  kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, unit);
}

static lv_obj_t *act_stats_row(lv_obj_t *parent) {
  lv_obj_t *row = kit_row_box(parent, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  act_stat(row, ICON_ROUTE, KIT_COLOR_BLUE, "km");
  act_stat(row, ICON_FIRE, KIT_COLOR_ORANGE, "kcal");
  act_stat(row, ICON_RUNNING, KIT_COLOR_GREEN, "active min");
  lv_subject_add_observer_obj(subj_steps, act_stats_obs, row, NULL);
  lv_subject_add_observer_obj(subj_units, act_stats_obs, row, NULL);
  return row;
}

/* ================================ Activity app ==================================== */

static const int32_t ACT_GOAL_VALUES[] = { 3000, 5000, 6000, 7500, 8000, 10000, 12000, 15000, 20000 };
static const char *const ACT_GOAL_LABELS[] = { "3,000 steps", "5,000 steps", "6,000 steps", "7,500 steps", "8,000 steps",
                                               "10,000 steps", "12,000 steps", "15,000 steps", "20,000 steps" };
static const KitChoice ACT_GOAL_CHOICE = { "Daily goal", &subj_step_goal, ACT_GOAL_VALUES, ACT_GOAL_LABELS, 9 };

static void act_goal_row_obs(lv_observer_t *observer, lv_subject_t *subject) {
  char buf[16];
  format_thousands(lv_subject_get_int(subject), buf, sizeof(buf));
  lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%s steps", buf);
}

static void act_goal_clicked_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(kit_choice_page(&ACT_GOAL_CHOICE));
}

// Seven bars, oldest left, today right, with the goal as a line.
static void act_week_chart(lv_obj_t *parent) {
  static const char *const DAYS[] = { "S", "M", "T", "W", "T", "F", "S" };
  lv_obj_t *card = kit_info_card(parent);
  lv_obj_set_style_pad_row(card, 8, 0);

  uint32_t values[ACTIVITY_DAYS];
  for (int i = 0; i < ACTIVITY_DAYS; i++) values[i] = i == ACTIVITY_DAYS - 1 ? activity_steps() : activity_history(ACTIVITY_DAYS - 1 - i);
  uint32_t goal = lv_subject_get_int(subj_step_goal);
  uint32_t top = goal;
  for (uint32_t v : values) top = max(top, v);
  if (!top) top = 1;

  const int32_t chart_h = 110;
  lv_obj_t *bars = kit_container(card);
  lv_obj_set_size(bars, LV_PCT(100), chart_h);
  lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  lv_obj_t *labels = kit_row_box(card, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  for (int i = 0; i < ACTIVITY_DAYS; i++) {
    lv_obj_t *bar = kit_container(bars);
    int32_t h = (int32_t)((uint64_t)values[i] * chart_h / top);
    lv_obj_set_size(bar, 22, max(h, (int32_t)6));
    lv_obj_set_style_radius(bar, 6, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    bool reached = goal && values[i] >= goal;
    lv_obj_set_style_bg_color(bar, lv_color_hex(i == ACTIVITY_DAYS - 1 ? KIT_COLOR_GREEN : reached ? 0x248A3D : 0x3A3A3C), 0);
    int wday = (t.tm_wday + i - (ACTIVITY_DAYS - 1) + 7 * 2) % 7;
    lv_obj_t *l = kit_label(labels, KIT_FONT_SMALL, i == ACTIVITY_DAYS - 1 ? 0xFFFFFF : KIT_COLOR_TEXT2, DAYS[wday]);
    lv_obj_set_width(l, 22);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }

  if (goal && goal <= top) {  // goal line
    lv_obj_t *line = kit_container(bars);
    lv_obj_set_ignore_layout(line, true);
    lv_obj_set_size(line, LV_PCT(100), 2);
    lv_obj_set_style_bg_opa(line, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(KIT_COLOR_GREEN), 0);
    lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, -(int32_t)((uint64_t)goal * chart_h / top));
  }
  uint32_t total = 0;
  for (uint32_t v : values) total += v;
  char avg[16];
  format_thousands(total / ACTIVITY_DAYS, avg, sizeof(avg));
  lv_obj_t *note = kit_label(card, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
  lv_label_set_text_fmt(note, "Average %s steps a day", avg);
}

void activity_open() {
  lv_obj_t *page = kit_page_create("Activity", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  act_ring(c, 230, 18);
  act_stats_row(c);
  if (!imu_available()) kit_note(c, "Motion sensor not found: steps are not counted.");

  lv_obj_t *week = kit_section(c, "LAST 7 DAYS");
  lv_obj_set_width(week, LV_PCT(100));
  act_week_chart(c);

  lv_obj_t *goal = kit_row(c, ICON_FLAG, KIT_COLOR_GREEN, "Daily goal", "", true);
  lv_subject_add_observer_obj(subj_step_goal, act_goal_row_obs, kit_row_subtitle(goal), NULL);
  lv_obj_add_event_cb(goal, act_goal_clicked_cb, LV_EVENT_CLICKED, NULL);
  kit_switch_row(c, ICON_BELL, KIT_COLOR_GREEN, "Reminders", "Move at :50 after a still hour", subj_move_remind);
  kit_note(c, "Distance and calories are estimates based on an average step length.");
  kit_page_push(page);
}

/* ================================ Activity tile =================================== */

static void act_tile_click_cb(lv_event_t *e) {
  audio_click();
  activity_open();
}

lv_obj_t *activity_tile_create() {
  lv_obj_t *tile = face_screen_create();
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(tile, 26, 0);
  lv_obj_set_style_pad_row(tile, 16, 0);
  lv_obj_add_event_cb(tile, act_tile_click_cb, LV_EVENT_SHORT_CLICKED, NULL);

  kit_label(tile, KIT_FONT_BODY, KIT_COLOR_GREEN, "Activity");
  lv_obj_t *ring = act_ring(tile, 236, 20);
  lv_obj_set_clickable(ring, false);
  lv_obj_t *stats = act_stats_row(tile);
  lv_obj_set_style_pad_hor(stats, 20, 0);
  return tile;
}
