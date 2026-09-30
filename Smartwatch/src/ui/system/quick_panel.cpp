/*
 * quick_panel.cpp - Quick settings, pulled down from the top of the watch face.
 *
 *              14:32
 *        Tue, 29 Sep  ·  86 %
 *   [Wi-Fi]  [Bluetooth] [Do not disturb] [Silent]
 *   [Always on] [Battery saver] [Bedtime] [Raise to wake]
 *   [Flashlight] [Find phone] [Settings] [Power]
 *   brightness ----o-------
 *
 * Toggles are bound to the settings subjects, so they always show the live state.
 * Swipe up (or press BOOT) to close.
 */
#include "quick_panel.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../services/phone.h"
#include "../../services/sleep_tracker.h"
#include "../apps/apps.h"
#include "../kit/kit.h"
#include "screen.h"

#define QP_BUTTON_SIZE 64
#define QP_GAP         14

/* ================================ Flashlight ====================================== */
// The whole screen white at full brightness; it stays on until closed (swipe right or
// the BOOT button).

static void flash_deleted_cb(lv_event_t *e) {
  screen_brightness_override(-1);
  screen_keep_awake(false);
}

void flashlight_open() {
  lv_obj_t *page = kit_page_create_bare(0xFFFFFF);
  lv_obj_add_event_cb(page, flash_deleted_cb, LV_EVENT_DELETE, NULL);
  screen_keep_awake(true);
  screen_brightness_override(100);
  kit_open(page, KIT_ANIM_FADE, true);
}

/* ================================ Find phone ====================================== */
// Makes the phone ring through Gadgetbridge, even in silent mode.

static bool fp_ringing;

static void fp_fill(lv_obj_t *c);

static void fp_async_fill(void *content) {
  fp_fill((lv_obj_t *)content);
}

static void fp_ring_cb(lv_event_t *e) {
  audio_click();
  fp_ringing = !fp_ringing;
  phone_find(fp_ringing);
  lv_obj_t *c = lv_obj_get_parent(lv_event_get_current_target_obj(e));
  lv_async_call_cancel(fp_async_fill, c);
  lv_async_call(fp_async_fill, c);  // the button itself is rebuilt
}

static void fp_fill(lv_obj_t *c) {
  lv_obj_clean(c);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (!phone_connected()) {
    fp_ringing = false;
    kit_empty_state(c, ICON_MOBILE, "Phone not connected. Pair it with Gadgetbridge: Settings > Bluetooth.");
    return;
  }
  lv_obj_t *icon = kit_icon(c, ICON_MOBILE, fp_ringing ? KIT_COLOR_GREEN : KIT_COLOR_CARD2, 110);
  lv_obj_set_style_margin_top(icon, 20, 0);
  lv_obj_t *text = kit_label(c, KIT_FONT_BODY, KIT_COLOR_TEXT2, fp_ringing ? "Your phone is ringing" : "Make your phone ring, even in silent mode.");
  lv_obj_set_width(text, LV_PCT(100));
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_t *btn = kit_button(c, fp_ringing ? "Stop" : "Ring phone", fp_ringing ? KIT_COLOR_RED : KIT_COLOR_GREEN);
  lv_obj_add_event_cb(btn, fp_ring_cb, LV_EVENT_CLICKED, NULL);
}

static void fp_phone_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *c = lv_observer_get_target_obj(observer);
  lv_async_call_cancel(fp_async_fill, c);
  lv_async_call(fp_async_fill, c);
}

static void fp_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(fp_async_fill, lv_event_get_user_data(e));
  if (fp_ringing) phone_find(false);
  fp_ringing = false;
}

void findphone_open() {
  fp_ringing = false;
  lv_obj_t *page = kit_page_create("Find phone", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_add_event_cb(page, fp_deleted_cb, LV_EVENT_DELETE, c);
  lv_subject_add_observer_obj(subj_bt_state, fp_phone_obs, c, NULL);
  fp_fill(c);
  kit_page_push(page);
}

/* ================================ Panel =========================================== */

static void qp_info_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *label = lv_observer_get_target_obj(observer);
  int32_t level = lv_subject_get_int(subj_battery);
  const char *date = lv_subject_get_string(subj_date_text);
  if (level < 0) lv_label_set_text(label, date);
  else lv_label_set_text_fmt(label, "%s  \xE2\x80\xA2  %d%%%s", date, (int)level, lv_subject_get_int(subj_charging) ? " \xE2\x80\xA2 charging" : "");
}

static void qp_action_cb(lv_event_t *e) {
  audio_click();
  ((app_open_fn_t)lv_event_get_user_data(e))();
}

static lv_obj_t *qp_action(lv_obj_t *parent, const char *icon, uint32_t icon_color, app_open_fn_t open) {
  lv_obj_t *btn = kit_round_button(parent, icon, KIT_COLOR_CARD2, QP_BUTTON_SIZE);
  lv_obj_set_style_text_color(btn, lv_color_hex(icon_color), 0);
  lv_obj_add_event_cb(btn, qp_action_cb, LV_EVENT_CLICKED, (void *)open);
  return btn;
}

static void qp_bedtime_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_set_style_bg_color(lv_observer_get_target_obj(observer),
                            lv_color_hex(lv_subject_get_int(subject) ? KIT_COLOR_INDIGO : KIT_COLOR_CARD2), 0);
}

static void qp_bedtime_cb(lv_event_t *e) {
  audio_click();
  bedtime_set(!lv_subject_get_int(subj_bedtime_on));
}

static void qp_power_menu() {
  kit_power_menu();
}

lv_obj_t *quick_panel_create() {
  lv_obj_t *page = kit_page_create_bare(0x000000);
  kit_page_set_close_dir(page, LV_DIR_TOP);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(page, 16, 0);
  lv_obj_set_style_pad_row(page, 10, 0);

  lv_obj_t *time = kit_label(page, &font_num_44, 0xFFFFFF, "");
  lv_label_bind_text(time, subj_time_text, NULL);
  lv_obj_t *info = kit_label(page, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
  lv_obj_set_style_margin_top(info, -8, 0);
  lv_subject_add_observer_obj(subj_battery, qp_info_obs, info, NULL);
  lv_subject_add_observer_obj(subj_charging, qp_info_obs, info, NULL);
  lv_subject_add_observer_obj(subj_date_text, qp_info_obs, info, NULL);

  lv_obj_t *grid = kit_container(page);
  lv_obj_set_size(grid, QP_BUTTON_SIZE * 4 + QP_GAP * 3, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, QP_GAP, 0);
  lv_obj_set_style_pad_row(grid, 12, 0);
  lv_obj_set_style_margin_top(grid, 8, 0);
  kit_toggle_button(grid, ICON_WIFI, KIT_COLOR_BLUE, subj_wifi_enabled, QP_BUTTON_SIZE);
  kit_toggle_button(grid, ICON_BLUETOOTH, KIT_COLOR_INDIGO, subj_bt_enabled, QP_BUTTON_SIZE);
  kit_toggle_button(grid, ICON_MOON, KIT_COLOR_PURPLE, subj_dnd, QP_BUTTON_SIZE);
  kit_toggle_button(grid, ICON_MUTE, KIT_COLOR_ORANGE, subj_silent, QP_BUTTON_SIZE);
  kit_toggle_button(grid, ICON_EYE, KIT_COLOR_TEAL, subj_aod, QP_BUTTON_SIZE);
  kit_toggle_button(grid, ICON_LEAF, KIT_COLOR_GREEN, subj_saver, QP_BUTTON_SIZE);
  lv_obj_t *bed = kit_round_button(grid, ICON_BED, KIT_COLOR_CARD2, QP_BUTTON_SIZE);
  lv_subject_add_observer_obj(subj_bedtime_on, qp_bedtime_obs, bed, NULL);
  lv_obj_add_event_cb(bed, qp_bedtime_cb, LV_EVENT_CLICKED, NULL);
  kit_toggle_button(grid, ICON_HAND, KIT_COLOR_CYAN, subj_raise_wake, QP_BUTTON_SIZE);
  qp_action(grid, ICON_FLASHLIGHT, KIT_COLOR_YELLOW, flashlight_open);
  qp_action(grid, ICON_MOBILE, KIT_COLOR_GREEN, findphone_open);
  qp_action(grid, ICON_SETTINGS, 0xFFFFFF, settings_open);
  qp_action(grid, ICON_POWER, KIT_COLOR_RED, qp_power_menu);

  lv_obj_t *bright = kit_container(page);
  lv_obj_set_size(bright, QP_BUTTON_SIZE * 4 + QP_GAP * 3, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(bright, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bright, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bright, 16, 0);
  lv_obj_set_style_margin_top(bright, 8, 0);
  kit_label(bright, &font_icons_24, KIT_COLOR_TEXT2, ICON_SUN);
  lv_obj_t *slider = kit_slider(bright, subj_brightness, 5, 100);
  lv_obj_set_flex_grow(slider, 1);

  lv_obj_t *handle = kit_container(page);
  lv_obj_set_size(handle, 44, 5);
  lv_obj_set_style_radius(handle, 3, 0);
  lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(handle, lv_color_hex(0x48484A), 0);
  lv_obj_set_style_margin_top(handle, 4, 0);
  return page;
}
