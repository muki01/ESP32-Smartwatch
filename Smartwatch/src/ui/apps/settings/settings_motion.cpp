/*
 * settings_motion.cpp - Settings > Display > Motion sensor: calibration.
 *
 * How the motion sensor sits on the board decides which of its axes is "up" for the
 * screen; raise to wake needs that. Two steps, each held still for 1.5 s: flat (screen up:
 * the axis carrying gravity is the screen normal, the others give the zero offsets), then
 * upright like reading (12 o'clock up: the vertical axis). The result is stored by imu.cpp.
 */
#include "settings_internal.h"

#include <Arduino.h>
#include "../../../drivers/audio.h"
#include "../../../drivers/imu.h"
#include "../../system/screen.h"

#define MOTION_HOLD     45      // 33 ms ticks the watch must stay still: 1.5 s
#define MOTION_STILL    0.04f   // g: largest change between two samples that is still "still"
#define MOTION_GRAVITY  0.85f   // g: the axis carrying gravity reads at least this

static lv_obj_t *mc_ring, *mc_step, *mc_title, *mc_text;
static int mc_stage;            // 0 = flat, 1 = upright
static int mc_hold;
static float mc_prev[3], mc_sum[3];
static int mc_z_axis, mc_z_sign;
static float mc_offset[3];

static void mc_show_stage() {
  lv_label_set_text(mc_step, mc_stage ? "2" : "1");
  lv_label_set_text(mc_title, mc_stage ? "Hold it upright" : "Lay it flat");
  lv_label_set_text(mc_text, mc_stage ? "Hold the watch in front of you like a phone: screen facing you, 12 o'clock up."
                                      : "Put the watch on a table, screen up, and keep it still.");
  lv_arc_set_value(mc_ring, 0);
}

static void mc_restart_hold() {
  mc_hold = 0;
  memset(mc_sum, 0, sizeof(mc_sum));
  lv_arc_set_value(mc_ring, 0);
}

static void mc_tick_cb(lv_timer_t *t) {
  float s[3];
  if (!mc_ring || !imu_read_raw(&s[0], &s[1], &s[2])) return;
  bool still = true;
  int axis = 0;
  for (int i = 0; i < 3; i++) {
    if (fabsf(s[i] - mc_prev[i]) > MOTION_STILL) still = false;
    mc_prev[i] = s[i];
    if (fabsf(s[i]) > fabsf(s[axis])) axis = i;
  }
  // Still, one axis carries gravity, and upright is not the flat axis again.
  if (!still || fabsf(s[axis]) < MOTION_GRAVITY || (mc_stage == 1 && axis == mc_z_axis)) {
    mc_restart_hold();
    return;
  }
  for (int i = 0; i < 3; i++) mc_sum[i] += s[i];
  lv_arc_set_value(mc_ring, ++mc_hold * 100 / MOTION_HOLD);
  if (mc_hold < MOTION_HOLD) return;

  float mean[3];
  for (int i = 0; i < 3; i++) mean[i] = mc_sum[i] / mc_hold;
  audio_click();
  if (mc_stage == 0) {
    mc_z_axis = axis;
    mc_z_sign = mean[axis] > 0 ? 1 : -1;
    for (int i = 0; i < 3; i++) mc_offset[i] = i == axis ? 0.0f : mean[i];  // flat: the other axes read 0
    mc_stage = 1;
    mc_restart_hold();
    mc_show_stage();
    return;
  }
  imu_calibrate_axes(axis, mean[axis] > 0 ? 1 : -1, mc_z_axis, mc_z_sign, mc_offset[0], mc_offset[1], mc_offset[2]);
  mc_ring = NULL;  // done: no more ticks
  kit_toast("Motion sensor calibrated");
  kit_page_pop();
}

static void mc_deleted_cb(lv_event_t *e) {
  mc_ring = mc_step = mc_title = mc_text = NULL;
  screen_keep_awake(false);
}

lv_obj_t *sui_build_motion() {
  lv_obj_t *page = kit_page_create_bare(0x000000);
  if (!imu_available()) {
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    kit_empty_state(page, ICON_WARNING, "Motion sensor not found.");
    return page;
  }
  lv_obj_t *col = kit_col_box(page, LV_FLEX_ALIGN_CENTER, 10);
  lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(col, 26, 0);
  kit_label(col, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "CALIBRATE MOTION SENSOR");
  mc_ring = kit_ring(col, 150, 12, KIT_COLOR_GREEN);
  mc_step = kit_label(mc_ring, &font_num_64, 0xFFFFFF, "1");
  lv_obj_center(mc_step);
  mc_title = kit_label(col, KIT_FONT_TEXT, 0xFFFFFF, "");
  mc_text = kit_label(col, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  lv_obj_set_width(mc_text, LV_PCT(100));
  lv_obj_set_style_text_align(mc_text, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(mc_text, LV_LABEL_LONG_MODE_WRAP);

  mc_stage = 0;
  mc_restart_hold();
  mc_show_stage();
  kit_page_timer(page, mc_tick_cb, 33, NULL);
  lv_obj_add_event_cb(page, mc_deleted_cb, LV_EVENT_DELETE, NULL);
  screen_keep_awake(true);
  return page;
}
