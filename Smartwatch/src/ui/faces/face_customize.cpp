/*
 * face_customize.cpp - Accent colour and background of every face (and the tiles).
 */
#include "faces.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../kit/kit.h"
#include "../system/nav.h"

static lv_obj_t *fc_swatches[FACE_COLOR_COUNT];
static lv_obj_t *fc_bg_rows[2];

static void fc_show() {
  int32_t color = lv_subject_get_int(subj_face_color);
  for (int i = 0; i < FACE_COLOR_COUNT; i++) {
    if (!fc_swatches[i]) continue;
    lv_obj_set_style_border_width(fc_swatches[i], i == color ? 4 : 0, 0);
    lv_obj_set_hidden(lv_obj_get_child(fc_swatches[i], 0), i != color);
  }
  int32_t texture = lv_subject_get_int(subj_face_texture);
  for (int i = 0; i < 2; i++) {
    if (fc_bg_rows[i]) lv_obj_set_hidden(lv_obj_get_child(fc_bg_rows[i], -1), (i == 0) != (texture != 0));
  }
}

static void fc_color_cb(lv_event_t *e) {
  audio_click();
  subj_set(subj_face_color, (int32_t)(intptr_t)lv_event_get_user_data(e));
  fc_show();
}

static void fc_bg_cb(lv_event_t *e) {
  audio_click();
  subj_set(subj_face_texture, (int32_t)(intptr_t)lv_event_get_user_data(e));
  fc_show();
}

static void fc_done_cb(lv_event_t *e) {
  audio_click();
  nav_go_home(true);
}

static void fc_deleted_cb(lv_event_t *e) {
  memset(fc_swatches, 0, sizeof(fc_swatches));
  memset(fc_bg_rows, 0, sizeof(fc_bg_rows));
}

void face_customize_open() {
  lv_obj_t *page = kit_page_create("Customize", true);
  lv_obj_t *c = kit_page_content(page);

  kit_section(c, "COLOR");
  lv_obj_t *grid = kit_container(c);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(grid, 14, 0);
  lv_obj_set_style_pad_ver(grid, 6, 0);
  for (int i = 0; i < FACE_COLOR_COUNT; i++) {
    uint32_t color = face_color_value(i);
    lv_obj_t *sw = kit_container(grid);
    lv_obj_set_size(sw, 64, 64);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(sw, lv_color_white(), 0);
    lv_obj_set_style_opa(sw, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_clickable(sw, true);
    lv_obj_set_ext_click_area(sw, 6);
    lv_obj_t *check = kit_label(sw, &font_icons_24, color == 0xFFFFFF || color == 0xFFD60A ? 0x000000 : 0xFFFFFF, ICON_CHECK);
    lv_obj_center(check);
    lv_obj_add_event_cb(sw, fc_color_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    fc_swatches[i] = sw;
  }

  kit_section(c, "BACKGROUND");
  static const char *const BG_NAMES[2] = { "Texture", "Black" };
  for (int i = 0; i < 2; i++) {
    lv_obj_t *row = kit_row(c, NULL, 0, BG_NAMES[i], i ? "Saves the most power" : "Dark geometric pattern", false);
    kit_label(row, &font_icons_24, KIT_COLOR_ACCENT, ICON_CHECK);
    lv_obj_add_event_cb(row, fc_bg_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i ? 0 : 1));
    fc_bg_rows[i] = row;
  }
  lv_obj_t *done = kit_button(c, "Done", KIT_COLOR_ACCENT);
  lv_obj_set_style_margin_top(done, 8, 0);
  lv_obj_add_event_cb(done, fc_done_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(page, fc_deleted_cb, LV_EVENT_DELETE, NULL);
  fc_show();
  kit_page_push(page);
}
