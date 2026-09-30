/*
 * kit_widgets.cpp - Shared styles and the widgets of the kit.
 *
 * All styles are shared lv_style_t objects (no per-widget local styles where avoidable),
 * containers drop the theme styles they do not need, and widgets bind to subjects so a
 * page only redraws what actually changes.
 */
#include "kit_internal.h"

#include <Arduino.h>
#include "../../drivers/audio.h"

lv_style_t kit_st_screen, kit_st_header, kit_st_content, kit_st_scrollbar;
lv_style_t kit_st_card, kit_st_card_pressed, kit_st_disabled;
lv_style_t kit_st_title, kit_st_text, kit_st_sub, kit_st_section, kit_st_icon, kit_st_value;
lv_style_t kit_st_button, kit_st_button_pressed, kit_st_chip_on, kit_st_toast;
static lv_style_t kit_st_switch, kit_st_switch_on, kit_st_switch_knob;
static lv_style_t kit_st_slider, kit_st_slider_ind, kit_st_slider_knob;
static lv_style_transition_dsc_t kit_press_tr;

/* ================================ Styles ========================================== */

void kit_init() {
  static const lv_style_prop_t press_props[] = { LV_STYLE_BG_COLOR, LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, LV_STYLE_PROP_INV };
  lv_style_transition_dsc_init(&kit_press_tr, press_props, lv_anim_path_ease_out, 90, 0, NULL);

  lv_style_init(&kit_st_screen);
  lv_style_set_bg_color(&kit_st_screen, lv_color_hex(KIT_BG));
  lv_style_set_bg_opa(&kit_st_screen, LV_OPA_COVER);
  lv_style_set_text_color(&kit_st_screen, lv_color_white());
  lv_style_set_text_font(&kit_st_screen, KIT_FONT_BODY);

  lv_style_init(&kit_st_header);
  lv_style_set_pad_left(&kit_st_header, 20);
  lv_style_set_pad_right(&kit_st_header, 24);
  lv_style_set_pad_top(&kit_st_header, 14);
  lv_style_set_pad_bottom(&kit_st_header, 8);
  lv_style_set_pad_column(&kit_st_header, 10);

  lv_style_init(&kit_st_content);
  lv_style_set_pad_left(&kit_st_content, 12);
  lv_style_set_pad_right(&kit_st_content, 12);
  lv_style_set_pad_top(&kit_st_content, 4);
  lv_style_set_pad_bottom(&kit_st_content, 60);  // lets the last row scroll clear of the round corners
  lv_style_set_pad_row(&kit_st_content, 10);

  lv_style_init(&kit_st_scrollbar);
  lv_style_set_width(&kit_st_scrollbar, 4);
  lv_style_set_radius(&kit_st_scrollbar, 2);
  lv_style_set_bg_color(&kit_st_scrollbar, lv_color_white());
  lv_style_set_bg_opa(&kit_st_scrollbar, LV_OPA_40);
  lv_style_set_pad_right(&kit_st_scrollbar, 4);
  lv_style_set_pad_top(&kit_st_scrollbar, 40);
  lv_style_set_pad_bottom(&kit_st_scrollbar, 60);

  lv_style_init(&kit_st_card);
  lv_style_set_bg_color(&kit_st_card, lv_color_hex(KIT_COLOR_CARD));
  lv_style_set_bg_opa(&kit_st_card, LV_OPA_COVER);
  lv_style_set_radius(&kit_st_card, 26);
  lv_style_set_pad_hor(&kit_st_card, 14);
  lv_style_set_pad_ver(&kit_st_card, 14);
  lv_style_set_pad_column(&kit_st_card, 12);
  lv_style_set_pad_row(&kit_st_card, 4);
  lv_style_set_transition(&kit_st_card, &kit_press_tr);

  lv_style_init(&kit_st_card_pressed);
  lv_style_set_bg_color(&kit_st_card_pressed, lv_color_hex(KIT_COLOR_CARD2));
  lv_style_set_transform_width(&kit_st_card_pressed, -4);
  lv_style_set_transform_height(&kit_st_card_pressed, -2);

  lv_style_init(&kit_st_disabled);
  lv_style_set_opa(&kit_st_disabled, LV_OPA_40);

  lv_style_init(&kit_st_title);
  lv_style_set_text_font(&kit_st_title, KIT_FONT_TITLE);
  lv_style_set_text_color(&kit_st_title, lv_color_white());

  lv_style_init(&kit_st_text);
  lv_style_set_text_font(&kit_st_text, KIT_FONT_TEXT);
  lv_style_set_text_color(&kit_st_text, lv_color_white());

  lv_style_init(&kit_st_sub);
  lv_style_set_text_font(&kit_st_sub, KIT_FONT_SMALL);
  lv_style_set_text_color(&kit_st_sub, lv_color_hex(KIT_COLOR_TEXT2));

  lv_style_init(&kit_st_value);
  lv_style_set_text_font(&kit_st_value, KIT_FONT_SMALL);
  lv_style_set_text_color(&kit_st_value, lv_color_white());
  lv_style_set_text_align(&kit_st_value, LV_TEXT_ALIGN_RIGHT);

  lv_style_init(&kit_st_section);
  lv_style_set_text_font(&kit_st_section, KIT_FONT_SMALL);
  lv_style_set_text_color(&kit_st_section, lv_color_hex(KIT_COLOR_TEXT2));
  lv_style_set_text_letter_space(&kit_st_section, 1);
  lv_style_set_pad_left(&kit_st_section, 12);
  lv_style_set_pad_top(&kit_st_section, 12);

  lv_style_init(&kit_st_icon);
  lv_style_set_radius(&kit_st_icon, LV_RADIUS_CIRCLE);
  lv_style_set_bg_opa(&kit_st_icon, LV_OPA_COVER);
  lv_style_set_text_color(&kit_st_icon, lv_color_white());
  lv_style_set_text_font(&kit_st_icon, &font_icons_24);
  lv_style_set_text_align(&kit_st_icon, LV_TEXT_ALIGN_CENTER);
  lv_style_set_transition(&kit_st_icon, &kit_press_tr);

  lv_style_init(&kit_st_switch);
  lv_style_set_bg_color(&kit_st_switch, lv_color_hex(KIT_TRACK));
  lv_style_set_bg_opa(&kit_st_switch, LV_OPA_COVER);
  lv_style_init(&kit_st_switch_on);
  lv_style_set_bg_color(&kit_st_switch_on, lv_color_hex(KIT_COLOR_ACCENT));
  lv_style_init(&kit_st_switch_knob);
  lv_style_set_bg_color(&kit_st_switch_knob, lv_color_white());
  lv_style_set_pad_all(&kit_st_switch_knob, -3);

  lv_style_init(&kit_st_slider);
  lv_style_set_bg_color(&kit_st_slider, lv_color_hex(KIT_TRACK));
  lv_style_set_bg_opa(&kit_st_slider, LV_OPA_COVER);
  lv_style_set_radius(&kit_st_slider, 8);
  lv_style_init(&kit_st_slider_ind);
  lv_style_set_bg_color(&kit_st_slider_ind, lv_color_hex(KIT_COLOR_ACCENT));
  lv_style_set_radius(&kit_st_slider_ind, 8);
  lv_style_init(&kit_st_slider_knob);
  lv_style_set_bg_color(&kit_st_slider_knob, lv_color_white());
  lv_style_set_pad_all(&kit_st_slider_knob, 8);

  lv_style_init(&kit_st_button);
  lv_style_set_radius(&kit_st_button, LV_RADIUS_CIRCLE);
  lv_style_set_bg_opa(&kit_st_button, LV_OPA_COVER);
  lv_style_set_pad_ver(&kit_st_button, 16);
  lv_style_set_text_font(&kit_st_button, KIT_FONT_BODY);
  lv_style_set_text_color(&kit_st_button, lv_color_white());
  lv_style_set_text_align(&kit_st_button, LV_TEXT_ALIGN_CENTER);
  lv_style_set_transition(&kit_st_button, &kit_press_tr);
  lv_style_init(&kit_st_button_pressed);
  lv_style_set_bg_opa(&kit_st_button_pressed, LV_OPA_70);
  lv_style_set_transform_width(&kit_st_button_pressed, -4);
  lv_style_set_transform_height(&kit_st_button_pressed, -2);

  lv_style_init(&kit_st_chip_on);
  lv_style_set_bg_color(&kit_st_chip_on, lv_color_hex(KIT_COLOR_ACCENT));

  lv_style_init(&kit_st_toast);
  lv_style_set_bg_color(&kit_st_toast, lv_color_hex(KIT_COLOR_CARD2));
  lv_style_set_bg_opa(&kit_st_toast, LV_OPA_COVER);
  lv_style_set_radius(&kit_st_toast, LV_RADIUS_CIRCLE);
  lv_style_set_pad_hor(&kit_st_toast, 22);
  lv_style_set_pad_ver(&kit_st_toast, 12);
  lv_style_set_text_font(&kit_st_toast, KIT_FONT_SMALL);
  lv_style_set_text_color(&kit_st_toast, lv_color_white());
}

/* ================================ Containers and labels =========================== */

lv_obj_t *kit_container(lv_obj_t *parent) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_clickable(obj, false);
  lv_obj_set_scrollable(obj, false);
  return obj;
}

// Full-width flex row / content-sized flex column without any styling.
lv_obj_t *kit_row_box(lv_obj_t *parent, lv_flex_align_t main_align, int32_t gap) {
  lv_obj_t *row = kit_container(parent);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, gap, 0);
  return row;
}

lv_obj_t *kit_col_box(lv_obj_t *parent, lv_flex_align_t cross_align, int32_t gap) {
  lv_obj_t *col = kit_container(parent);
  lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, cross_align, cross_align);
  lv_obj_set_style_pad_row(col, gap, 0);
  return col;
}

lv_obj_t *kit_styled_label(lv_obj_t *parent, lv_style_t *style, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_add_style(label, style, 0);
  lv_label_set_text(label, text ? text : "");
  return label;
}

lv_obj_t *kit_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_label_set_text(label, text ? text : "");
  return label;
}

// Setter for lv_obj_bind_bool(): visible while the subject is non-zero.
void kit_set_visible(lv_obj_t *obj, bool visible) {
  lv_obj_set_hidden(obj, !visible);
}

lv_obj_t *kit_section(lv_obj_t *parent, const char *text) {
  return kit_styled_label(parent, &kit_st_section, text);
}

lv_obj_t *kit_note(lv_obj_t *parent, const char *text) {
  lv_obj_t *note = kit_styled_label(parent, &kit_st_sub, text);
  lv_obj_set_width(note, LV_PCT(100));
  lv_obj_set_style_pad_hor(note, 12, 0);
  lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
  return note;
}

lv_obj_t *kit_empty_state(lv_obj_t *parent, const char *icon, const char *text) {
  lv_obj_t *box = kit_container(parent);
  lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(box, 56, 0);
  lv_obj_set_style_pad_row(box, 16, 0);
  kit_label(box, &font_icons_48, 0x48484A, icon);
  lv_obj_t *label = kit_label(box, KIT_FONT_BODY, KIT_COLOR_TEXT2, text);
  lv_obj_set_width(label, LV_PCT(90));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  return box;
}

/* ================================ Icons and buttons =============================== */

static const lv_font_t *kit_icon_font(int32_t size) {
  if (size >= 110) return &font_icons_48;
  if (size >= 64) return &font_icons_32;
  return &font_icons_24;
}

lv_obj_t *kit_icon(lv_obj_t *parent, const char *symbol, uint32_t color, int32_t size) {
  const lv_font_t *font = kit_icon_font(size);
  lv_obj_t *icon = kit_styled_label(parent, &kit_st_icon, symbol);
  lv_obj_set_size(icon, size, size);
  lv_obj_set_style_text_font(icon, font, 0);
  lv_obj_set_style_bg_color(icon, lv_color_hex(color), 0);
  lv_obj_set_style_pad_top(icon, (size - lv_font_get_line_height(font)) / 2, 0);
  lv_obj_set_clickable(icon, false);
  return icon;
}

lv_obj_t *kit_round_button(lv_obj_t *parent, const char *icon, uint32_t color, int32_t size) {
  lv_obj_t *btn = kit_icon(parent, icon, color, size);
  lv_obj_add_style(btn, &kit_st_button_pressed, LV_STATE_PRESSED);
  lv_obj_set_clickable(btn, true);
  lv_obj_set_ext_click_area(btn, 6);
  return btn;
}

static void kit_toggle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  uint32_t on_color = (uint32_t)(uintptr_t)lv_observer_get_user_data(observer);
  lv_obj_set_style_bg_color(lv_observer_get_target_obj(observer),
                            lv_color_hex(lv_subject_get_int(subject) ? on_color : KIT_COLOR_CARD2), 0);
}

static void kit_toggle_click_cb(lv_event_t *e) {
  lv_subject_t *subject = (lv_subject_t *)lv_event_get_user_data(e);
  audio_click();
  lv_subject_set_int(subject, !lv_subject_get_int(subject));
}

// Round quick-panel style toggle: coloured while the subject is on.
lv_obj_t *kit_toggle_button(lv_obj_t *parent, const char *icon, uint32_t on_color, lv_subject_t *subject, int32_t size) {
  lv_obj_t *btn = kit_round_button(parent, icon, KIT_COLOR_CARD2, size);
  lv_subject_add_observer_obj(subject, kit_toggle_obs, btn, (void *)(uintptr_t)on_color);
  lv_obj_add_event_cb(btn, kit_toggle_click_cb, LV_EVENT_CLICKED, subject);
  return btn;
}

lv_obj_t *kit_button(lv_obj_t *parent, const char *text, uint32_t color) {
  lv_obj_t *btn = kit_styled_label(parent, &kit_st_button, text);
  lv_obj_add_style(btn, &kit_st_button_pressed, LV_STATE_PRESSED);
  lv_obj_set_width(btn, LV_PCT(100));
  lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
  lv_obj_set_clickable(btn, true);
  return btn;
}

// Rounded selectable pill (presets, days); LV_STATE_CHECKED shows it in the accent colour.
lv_obj_t *kit_chip(lv_obj_t *parent, const char *text) {
  lv_obj_t *chip = kit_styled_label(parent, &kit_st_button, text);
  lv_obj_add_style(chip, &kit_st_button_pressed, LV_STATE_PRESSED);
  lv_obj_add_style(chip, &kit_st_chip_on, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(chip, lv_color_hex(KIT_COLOR_CARD), 0);
  lv_obj_set_clickable(chip, true);
  return chip;
}

// Progress ring 0..100 starting at 12 o'clock.
lv_obj_t *kit_ring(lv_obj_t *parent, int32_t size, int32_t width, uint32_t color) {
  lv_obj_t *arc = lv_arc_create(parent);
  lv_obj_set_size(arc, size, size);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 100);
  lv_arc_set_value(arc, 0);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_set_clickable(arc, false);
  lv_obj_set_style_pad_all(arc, 0, 0);
  lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(KIT_COLOR_CARD2), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  return arc;
}

lv_obj_t *kit_roller(lv_obj_t *parent, const char *options, int selected, int32_t width) {
  lv_obj_t *r = lv_roller_create(parent);
  lv_roller_set_options(r, options, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(r, 3);
  lv_roller_set_selected(r, selected, LV_ANIM_OFF);
  lv_obj_set_width(r, width);
  lv_obj_set_style_text_font(r, KIT_FONT_TEXT, 0);
  lv_obj_set_style_text_color(r, lv_color_hex(KIT_COLOR_TEXT2), 0);
  lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_bg_color(r, lv_color_hex(KIT_COLOR_CARD), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_radius(r, 20, 0);
  lv_obj_set_style_text_color(r, lv_color_white(), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, lv_color_hex(KIT_COLOR_CARD3), LV_PART_SELECTED);
  lv_obj_set_style_radius(r, 14, LV_PART_SELECTED);
  lv_obj_set_gesture_bubble(r, false);  // spinning must not close the page
  return r;
}

/* ================================ Cards and rows ================================== */

lv_obj_t *kit_card_base(lv_obj_t *parent) {
  lv_obj_t *card = kit_container(parent);
  lv_obj_add_style(card, &kit_st_card, 0);
  lv_obj_add_style(card, &kit_st_disabled, LV_STATE_DISABLED);
  lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
  return card;
}

// Plain card (column layout); clickable cards get the pressed look.
lv_obj_t *kit_card(lv_obj_t *parent) {
  lv_obj_t *card = kit_card_base(parent);
  lv_obj_add_style(card, &kit_st_card_pressed, LV_STATE_PRESSED);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  return card;
}

lv_obj_t *kit_row(lv_obj_t *parent, const char *symbol, uint32_t color, const char *title, const char *subtitle, bool chevron) {
  lv_obj_t *row = kit_card_base(parent);
  lv_obj_add_style(row, &kit_st_card_pressed, LV_STATE_PRESSED);
  lv_obj_set_clickable(row, true);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_min_height(row, 76, 0);

  if (symbol) kit_icon(row, symbol, color, 48);

  lv_obj_t *col = kit_container(row);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 2, 0);

  // Long texts wrap (at most two lines each) instead of being cut off.
  lv_obj_t *t = kit_styled_label(col, &kit_st_text, title);
  lv_obj_set_width(t, LV_PCT(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_max_height(t, 2 * lv_font_get_line_height(KIT_FONT_TEXT), 0);
  lv_obj_t *s = kit_styled_label(col, &kit_st_sub, subtitle);
  lv_obj_set_width(s, LV_PCT(100));
  lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_max_height(s, 2 * lv_font_get_line_height(KIT_FONT_SMALL), 0);
  if (!subtitle) lv_obj_set_hidden(s, true);

  if (chevron) kit_label(row, &font_icons_24, KIT_COLOR_TEXT2, ICON_CHEVRON_RIGHT);
  lv_obj_set_user_data(row, col);
  return row;
}

lv_obj_t *kit_row_title(lv_obj_t *row) {
  return lv_obj_get_child((lv_obj_t *)lv_obj_get_user_data(row), 0);
}

// The subtitle label is created hidden when empty; setting text through here shows it.
lv_obj_t *kit_row_subtitle(lv_obj_t *row) {
  lv_obj_t *s = lv_obj_get_child((lv_obj_t *)lv_obj_get_user_data(row), 1);
  lv_obj_set_hidden(s, false);
  return s;
}

static void kit_switch_row_cb(lv_event_t *e) {
  lv_subject_t *subject = (lv_subject_t *)lv_event_get_user_data(e);
  audio_click();
  lv_subject_set_int(subject, !lv_subject_get_int(subject));
}

// Display-only switch: the row around it handles the taps.
lv_obj_t *kit_switch(lv_obj_t *parent) {
  lv_obj_t *sw = lv_switch_create(parent);
  lv_obj_set_size(sw, 64, 36);
  lv_obj_add_style(sw, &kit_st_switch, 0);
  lv_obj_add_style(sw, &kit_st_switch_on, (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_style(sw, &kit_st_switch_knob, LV_PART_KNOB);
  lv_obj_set_clickable(sw, false);
  return sw;
}

// The whole row toggles; the switch only mirrors the subject.
lv_obj_t *kit_switch_row(lv_obj_t *parent, const char *symbol, uint32_t color, const char *title, const char *subtitle, lv_subject_t *subject) {
  lv_obj_t *row = kit_row(parent, symbol, color, title, subtitle, false);
  lv_obj_t *sw = kit_switch(row);
  lv_obj_bind_checked(sw, subject);
  lv_obj_add_event_cb(row, kit_switch_row_cb, LV_EVENT_CLICKED, subject);
  return row;
}

lv_obj_t *kit_slider(lv_obj_t *parent, lv_subject_t *subject, int32_t min, int32_t max) {
  lv_obj_t *slider = lv_slider_create(parent);
  lv_obj_set_height(slider, 14);
  lv_obj_add_style(slider, &kit_st_slider, 0);
  lv_obj_add_style(slider, &kit_st_slider_ind, LV_PART_INDICATOR);
  lv_obj_add_style(slider, &kit_st_slider_knob, LV_PART_KNOB);
  lv_obj_set_ext_click_area(slider, 20);
  lv_obj_set_gesture_bubble(slider, false);  // dragging must not close the page
  lv_slider_set_range(slider, min, max);
  if (subject) lv_slider_bind_value(slider, subject);
  return slider;
}

lv_obj_t *kit_slider_card(lv_obj_t *parent, const char *title, lv_subject_t *subject, int32_t min, int32_t max, const char *value_fmt) {
  lv_obj_t *card = kit_card_base(parent);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 16, 0);
  lv_obj_set_style_pad_bottom(card, 22, 0);

  lv_obj_t *head = kit_container(card);
  lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  kit_styled_label(head, &kit_st_text, title);
  lv_obj_t *value = kit_label(head, KIT_FONT_BODY, KIT_COLOR_TEXT2, NULL);
  if (subject && value_fmt) lv_label_bind_text(value, subject, value_fmt);

  lv_obj_t *slider = kit_slider(card, subject, min, max);
  lv_obj_set_width(slider, LV_PCT(100));
  lv_obj_set_style_margin_hor(slider, 10, 0);
  return card;
}

lv_obj_t *kit_info_card(lv_obj_t *parent) {
  lv_obj_t *card = kit_card_base(parent);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, 12, 0);
  lv_obj_set_style_pad_ver(card, 18, 0);
  return card;
}

lv_obj_t *kit_info_row(lv_obj_t *card, const char *key, const char *value) {
  lv_obj_t *row = kit_container(card);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(row, 12, 0);
  kit_styled_label(row, &kit_st_sub, key);
  lv_obj_t *v = kit_styled_label(row, &kit_st_value, value);
  lv_obj_set_flex_grow(v, 1);
  lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_WRAP);
  return v;
}

/* ================================ Choice page ===================================== */

static void kit_choice_clicked_cb(lv_event_t *e) {
  lv_obj_t *row = lv_event_get_current_target_obj(e);
  const KitChoice *choice = (const KitChoice *)lv_obj_get_user_data(lv_obj_get_parent(row));
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  audio_click();
  lv_subject_set_int(*choice->subject, choice->values ? choice->values[i] : i);
  kit_page_pop();
}

// Checkmark visible only on the selected option.
static void kit_choice_check_obs(lv_observer_t *observer, lv_subject_t *subject) {
  int32_t value = (int32_t)(intptr_t)lv_observer_get_user_data(observer);
  lv_obj_set_hidden(lv_observer_get_target_obj(observer), lv_subject_get_int(subject) != value);
}

lv_obj_t *kit_choice_page(const KitChoice *choice) {
  lv_obj_t *page = kit_page_create(choice->title, true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *list = kit_container(c);
  lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 10, 0);
  lv_obj_set_user_data(list, (void *)choice);
  lv_obj_t *selected = NULL;
  for (int i = 0; i < choice->count; i++) {
    int32_t value = choice->values ? choice->values[i] : i;
    lv_obj_t *row = kit_row(list, NULL, 0, choice->labels[i], NULL, false);
    lv_obj_t *check = kit_label(row, &font_icons_24, KIT_COLOR_ACCENT, ICON_CHECK);
    lv_subject_add_observer_obj(*choice->subject, kit_choice_check_obs, check, (void *)(intptr_t)value);
    lv_obj_add_event_cb(row, kit_choice_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    if (lv_subject_get_int(*choice->subject) == value) selected = row;
  }
  if (selected) {
    lv_obj_update_layout(page);
    lv_obj_scroll_to_view(selected, LV_ANIM_OFF);
  }
  return page;
}
