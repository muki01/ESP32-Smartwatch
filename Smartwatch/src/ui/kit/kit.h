/*
 * kit.h - The widget kit every screen is built from: colours and fonts, the page stack
 * with its transitions, list rows, switches, sliders, buttons, rings, dialogs, alerts,
 * toasts and the text input page.
 *
 *   kit_widgets.cpp  shared styles and widgets
 *   kit_pages.cpp    page stack, page transitions, pages
 *   kit_dialogs.cpp  toast, confirmation, power menu, alerts, keyboard page
 */
#pragma once

#include <lvgl.h>
#include "../../assets/fonts/fonts.h"
#include "../../assets/fonts/icons.h"

#define KIT_COLOR_ACCENT 0xEE1E1E
#define KIT_COLOR_BLUE   0x0A84FF
#define KIT_COLOR_INDIGO 0x5E5CE6
#define KIT_COLOR_PURPLE 0xBF5AF2
#define KIT_COLOR_ORANGE 0xFF9F0A
#define KIT_COLOR_YELLOW 0xFFD60A
#define KIT_COLOR_PINK   0xFF375F
#define KIT_COLOR_GREEN  0x30D158
#define KIT_COLOR_TEAL   0x40C8E0
#define KIT_COLOR_CYAN   0x64D2FF
#define KIT_COLOR_GRAY   0x8E8E93
#define KIT_COLOR_RED    0xFF453A
#define KIT_COLOR_CARD   0x1C1C1E
#define KIT_COLOR_CARD2  0x2C2C2E
#define KIT_COLOR_CARD3  0x3A3A3C
#define KIT_COLOR_TEXT2  0x8E8E93

#define KIT_FONT_TITLE   (&font_text_32)
#define KIT_FONT_TEXT    (&font_text_28)
#define KIT_FONT_BODY    (&font_text_24)
#define KIT_FONT_SMALL   (&font_text_20)

// How a page enters; closing plays the reverse.
enum KitAnim : uint8_t {
  KIT_ANIM_NONE, KIT_ANIM_SLIDE, KIT_ANIM_SLIDE_BACK, KIT_ANIM_FROM_TOP, KIT_ANIM_FROM_BOTTOM,
  KIT_ANIM_FROM_LEFT, KIT_ANIM_FADE
};

typedef void (*kit_action_cb_t)(void *user_data);
typedef void (*kit_alert_cb_t)(int button);  // 0 = primary, 1 = secondary, 2 = hardware button
typedef void (*app_open_fn_t)();              // opens an app (launcher, complications)

// A single-choice list page bound to an int subject.
struct KitChoice {
  const char *title;
  lv_subject_t **subject;   // address of the subject pointer (subjects are created at runtime)
  const int32_t *values;    // NULL: value == index
  const char *const *labels;
  int count;
};

void kit_init();

// ---- Page stack ---------------------------------------------------------------------
lv_obj_t *kit_page_create(const char *title, bool with_back);  // title NULL: no header
lv_obj_t *kit_page_create_bare(uint32_t bg_color);             // full screen, no header, no scrolling
lv_obj_t *kit_page_content(lv_obj_t *page);
lv_obj_t *kit_page_title(lv_obj_t *page);
void kit_page_title_small(lv_obj_t *page);
void kit_page_set_close_dir(lv_obj_t *page, lv_dir_t dir);
void kit_open(lv_obj_t *page, KitAnim anim, bool owned);       // owned: deleted when closed
void kit_page_push(lv_obj_t *page);                            // slide in, owned
void kit_page_pop();
void kit_page_pop_to(lv_obj_t *page);
void kit_page_remove(lv_obj_t *page);                          // drop a covered page from the stack
void kit_pages_close_all();
void kit_transition_finish();                                  // end a running transition at once
void kit_set_base(lv_obj_t *screen);
void kit_reset_to(lv_obj_t *screen, KitAnim anim);             // show a persistent screen, drop all pages
bool kit_page_is_open(lv_obj_t *page);
bool kit_page_is_top(lv_obj_t *page);
int kit_page_depth();
lv_timer_t *kit_page_timer(lv_obj_t *page, lv_timer_cb_t cb, uint32_t period, void *user_data);

// ---- Widgets ------------------------------------------------------------------------
lv_obj_t *kit_container(lv_obj_t *parent);
lv_obj_t *kit_row_box(lv_obj_t *parent, lv_flex_align_t main_align, int32_t gap);
lv_obj_t *kit_col_box(lv_obj_t *parent, lv_flex_align_t cross_align, int32_t gap);
lv_obj_t *kit_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *kit_section(lv_obj_t *parent, const char *text);
lv_obj_t *kit_note(lv_obj_t *parent, const char *text);
lv_obj_t *kit_card(lv_obj_t *parent);
lv_obj_t *kit_row(lv_obj_t *parent, const char *symbol, uint32_t color, const char *title, const char *subtitle, bool chevron);
lv_obj_t *kit_row_title(lv_obj_t *row);
lv_obj_t *kit_row_subtitle(lv_obj_t *row);
lv_obj_t *kit_switch(lv_obj_t *parent);
lv_obj_t *kit_switch_row(lv_obj_t *parent, const char *symbol, uint32_t color, const char *title, const char *subtitle, lv_subject_t *subject);
lv_obj_t *kit_slider(lv_obj_t *parent, lv_subject_t *subject, int32_t min, int32_t max);
lv_obj_t *kit_slider_card(lv_obj_t *parent, const char *title, lv_subject_t *subject, int32_t min, int32_t max, const char *value_fmt);
lv_obj_t *kit_info_card(lv_obj_t *parent);
lv_obj_t *kit_info_row(lv_obj_t *card, const char *key, const char *value);
lv_obj_t *kit_button(lv_obj_t *parent, const char *text, uint32_t color);
lv_obj_t *kit_chip(lv_obj_t *parent, const char *text);
lv_obj_t *kit_icon(lv_obj_t *parent, const char *symbol, uint32_t color, int32_t size);
lv_obj_t *kit_round_button(lv_obj_t *parent, const char *icon, uint32_t color, int32_t size);
lv_obj_t *kit_toggle_button(lv_obj_t *parent, const char *icon, uint32_t on_color, lv_subject_t *subject, int32_t size);
lv_obj_t *kit_ring(lv_obj_t *parent, int32_t size, int32_t width, uint32_t color);
lv_obj_t *kit_roller(lv_obj_t *parent, const char *options, int selected, int32_t width);
lv_obj_t *kit_empty_state(lv_obj_t *parent, const char *icon, const char *text);
lv_obj_t *kit_choice_page(const KitChoice *choice);
void kit_set_visible(lv_obj_t *obj, bool visible);  // setter for lv_obj_bind_bool()

// ---- Dialogs, alerts, toasts --------------------------------------------------------
void kit_toast(const char *text);
void kit_confirm(const char *title, const char *message, const char *ok_text, bool danger, kit_action_cb_t on_ok, void *user_data);
void kit_modal_close();
void kit_power_menu();
void kit_alert(const char *icon, uint32_t color, const char *title, const char *subtitle,
               const char *primary, const char *secondary, kit_alert_cb_t cb);
void kit_alert_close();
bool kit_alert_active();
void kit_alert_set_secondary_color(uint32_t color);
bool kit_alert_hw_button();  // a side button was pressed during an alert: handled here
// Keyboard page; on_done gets the text as user data (valid during the call).
lv_obj_t *kit_text_input_page(const char *title, const char *placeholder, bool password, int max_len, kit_action_cb_t on_done);
