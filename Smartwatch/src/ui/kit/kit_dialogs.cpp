/*
 * kit_dialogs.cpp - Toast, confirmation dialog, power menu, full-screen alerts and the
 * text input page with its keyboard.
 */
#include "kit_internal.h"

#include <Arduino.h>
#include "../../core/system.h"
#include "../../drivers/audio.h"
#include "../system/screen.h"

static lv_obj_t *kit_modal;
static lv_obj_t *kit_toast_obj;
static lv_obj_t *kit_alert_page;
static lv_obj_t *kit_alert_second;  // secondary button of the open alert
static kit_alert_cb_t kit_alert_cb;
static kit_action_cb_t kit_modal_action;
static void *kit_modal_user;

/* ================================ Toast =========================================== */

static void kit_toast_deleted_cb(lv_event_t *e) {
  kit_toast_obj = NULL;
}

static void kit_opa_anim_cb(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void kit_toast(const char *text) {
  if (kit_toast_obj) lv_obj_delete(kit_toast_obj);
  kit_toast_obj = kit_styled_label(lv_layer_top(), &kit_st_toast, text);
  lv_obj_align(kit_toast_obj, LV_ALIGN_BOTTOM_MID, 0, -44);
  lv_obj_add_event_cb(kit_toast_obj, kit_toast_deleted_cb, LV_EVENT_DELETE, NULL);

  // One animation fades in, holds and fades out (two separate fade animations on the
  // same object would cancel each other).
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, kit_toast_obj);
  lv_anim_set_exec_cb(&a, kit_opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, 150);
  lv_anim_set_reverse_delay(&a, 2000);
  lv_anim_set_reverse_duration(&a, 250);
  lv_anim_start(&a);
  lv_obj_delete_delayed(kit_toast_obj, 2450);
}

/* ================================ Dialogs ========================================= */

void kit_modal_close() {
  if (!kit_modal) return;
  lv_obj_delete_async(kit_modal);  // safe from inside the modal's own button events
  kit_modal = NULL;
}

static lv_obj_t *kit_modal_open(const char *title, const char *message) {
  kit_modal_close();
  kit_modal = kit_container(lv_layer_top());
  lv_obj_set_size(kit_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(kit_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(kit_modal, LV_OPA_80, 0);
  lv_obj_set_clickable(kit_modal, true);  // block touches to the page behind
  lv_obj_set_flex_flow(kit_modal, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(kit_modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *card = kit_card_base(kit_modal);
  lv_obj_set_width(card, 320);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(card, 22, 0);
  lv_obj_set_style_pad_row(card, 14, 0);

  lv_obj_t *t = kit_styled_label(card, &kit_st_text, title);
  lv_obj_set_width(t, LV_PCT(100));
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
  if (message) {
    lv_obj_t *m = kit_styled_label(card, &kit_st_sub, message);
    lv_obj_set_width(m, LV_PCT(100));
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_WRAP);
  }
  lv_obj_fade_in(kit_modal, 120, 0);
  return card;
}

static void kit_modal_cancel_cb(lv_event_t *e) {
  audio_click();
  kit_modal_close();
}

static void kit_modal_ok_cb(lv_event_t *e) {
  audio_click();
  kit_action_cb_t action = kit_modal_action;
  void *user = kit_modal_user;
  kit_modal_close();
  if (action) action(user);
}

void kit_confirm(const char *title, const char *message, const char *ok_text, bool danger, kit_action_cb_t on_ok, void *user_data) {
  lv_obj_t *card = kit_modal_open(title, message);
  kit_modal_action = on_ok;
  kit_modal_user = user_data;
  lv_obj_t *ok = kit_button(card, ok_text, danger ? KIT_COLOR_RED : KIT_COLOR_ACCENT);
  lv_obj_add_event_cb(ok, kit_modal_ok_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *cancel = kit_button(card, "Cancel", KIT_COLOR_CARD2);
  lv_obj_add_event_cb(cancel, kit_modal_cancel_cb, LV_EVENT_CLICKED, NULL);
}

static void kit_power_off_cb(lv_event_t *e) {
  kit_modal_close();
  system_power_off();
}

static void kit_restart_cb(lv_event_t *e) {
  kit_modal_close();
  system_restart();
}

void kit_power_menu() {
  lv_obj_t *card = kit_modal_open("Power", NULL);
  lv_obj_t *off = kit_button(card, "Power off", KIT_COLOR_RED);
  lv_obj_add_event_cb(off, kit_power_off_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *restart = kit_button(card, "Restart", KIT_COLOR_CARD2);
  lv_obj_add_event_cb(restart, kit_restart_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *cancel = kit_button(card, "Cancel", KIT_COLOR_CARD2);
  lv_obj_add_event_cb(cancel, kit_modal_cancel_cb, LV_EVENT_CLICKED, NULL);
}

/* ================================ Alerts ========================================== */
// Full-screen interruptions (alarm, timer, call, find my watch). They keep the screen on
// and stay until a button is pressed.

static void kit_alert_button_cb(lv_event_t *e) {
  int button = (int)(intptr_t)lv_event_get_user_data(e);
  kit_alert_cb_t cb = kit_alert_cb;
  kit_alert_close();
  if (cb) cb(button);
}

static void kit_pulse_anim_cb(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void kit_alert(const char *icon, uint32_t color, const char *title, const char *subtitle,
               const char *primary, const char *secondary, kit_alert_cb_t cb) {
  if (kit_alert_page) kit_alert_close();
  screen_wake(WAKE_EVENT);
  kit_alert_cb = cb;

  lv_obj_t *page = kit_page_create_bare(KIT_BG);
  kit_page_set_close_dir(page, LV_DIR_NONE);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(page, 24, 0);
  lv_obj_set_style_pad_row(page, 10, 0);

  // Icon with a softly pulsing halo.
  lv_obj_t *halo = kit_container(page);
  lv_obj_set_size(halo, 150, 150);
  lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(halo, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(halo, LV_OPA_30, 0);
  lv_obj_t *badge = kit_icon(halo, icon, color, 112);
  lv_obj_center(badge);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, halo);
  lv_anim_set_exec_cb(&a, kit_pulse_anim_cb);
  lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
  lv_anim_set_duration(&a, 700);
  lv_anim_set_reverse_duration(&a, 700);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  bool numeric = title && title[0] >= '0' && title[0] <= '9';
  lv_obj_t *t = kit_label(page, numeric ? &font_num_64 : KIT_FONT_TITLE, 0xFFFFFF, title);
  lv_obj_set_style_margin_top(t, 8, 0);
  lv_obj_set_width(t, LV_PCT(100));
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
  if (subtitle) {
    lv_obj_t *s = kit_label(page, KIT_FONT_BODY, KIT_COLOR_TEXT2, subtitle);
    lv_obj_set_width(s, LV_PCT(100));
    lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
  }

  lv_obj_t *buttons = kit_container(page);
  lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(buttons, 10, 0);
  lv_obj_set_style_margin_top(buttons, 14, 0);
  lv_obj_t *p = kit_button(buttons, primary, color);
  lv_obj_add_event_cb(p, kit_alert_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);
  kit_alert_second = NULL;
  if (secondary) {
    kit_alert_second = kit_button(buttons, secondary, KIT_COLOR_CARD2);
    lv_obj_add_event_cb(kit_alert_second, kit_alert_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
  }

  if (kit_stack_full()) kit_pages_close_all();  // an alert always gets a slot
  kit_alert_page = page;
  screen_keep_awake(true);
  kit_nav_unblock();  // and does not wait for a running transition
  kit_open(page, KIT_ANIM_FADE, true);
}

void kit_alert_forget() {
  if (!kit_alert_page) return;
  kit_alert_page = NULL;
  kit_alert_second = NULL;
  kit_alert_cb = NULL;
  screen_keep_awake(false);
}

void kit_alert_close() {
  lv_obj_t *page = kit_alert_page;
  if (!page) return;
  kit_transition_finish();
  kit_alert_forget();
  if (kit_page_is_top(page)) kit_pop_now();
  else kit_page_remove(page);  // covered by another page: just drop it
}

bool kit_alert_active() {
  return kit_alert_page != NULL;
}

void kit_alert_set_secondary_color(uint32_t color) {
  if (kit_alert_page && kit_alert_second) lv_obj_set_style_bg_color(kit_alert_second, lv_color_hex(color), 0);
}

// Side buttons act on a ringing alert (snooze an alarm, stop a timer).
bool kit_alert_hw_button() {
  if (!kit_alert_page) return false;
  kit_alert_cb_t cb = kit_alert_cb;
  kit_alert_close();
  if (cb) cb(2);
  return true;
}

/* ================================ Text input ====================================== */
// A page with one text field and the on-screen keyboard. The keyboard's check key calls
// on_done with the text (valid during the call); the caller decides what happens next.
// The layout fits a 368 px screen: at most 10 keys per row, a pop-over shows the key
// under the finger, shift works for one letter, and two symbol pages hold every
// printable ASCII character (Wi-Fi passwords).

#define KIT_KEY_DIGITS  "123"
#define KIT_KEY_MORE    "#+="
#define KIT_KEY_LETTERS "abc"
#define KIT_KB_CTRL(v)  ((lv_buttonmatrix_ctrl_t)(v))
#define KIT_KB_KEY      KIT_KB_CTRL(LV_BUTTONMATRIX_CTRL_POPOVER | 2)
#define KIT_KB_GAP      KIT_KB_CTRL(LV_BUTTONMATRIX_CTRL_HIDDEN | 1)
#define KIT_KB_MODE(w)  KIT_KB_CTRL(LV_KEYBOARD_CTRL_BUTTON_FLAGS | (w))
#define KIT_KB_DELETE   KIT_KB_CTRL(LV_BUTTONMATRIX_CTRL_CHECKED | 3)
#define KIT_KB_SPACE    KIT_KB_CTRL(10)

static const char *const KIT_KB_LOWER[] = {
  "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
  " ", "a", "s", "d", "f", "g", "h", "j", "k", "l", " ", "\n",
  LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
  KIT_KEY_DIGITS, " ", ".", LV_SYMBOL_OK, ""
};
static const char *const KIT_KB_UPPER[] = {
  "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
  " ", "A", "S", "D", "F", "G", "H", "J", "K", "L", " ", "\n",
  LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
  KIT_KEY_DIGITS, " ", ".", LV_SYMBOL_OK, ""
};
static const lv_buttonmatrix_ctrl_t KIT_KB_LETTERS_CTRL[] = {
  KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY,
  KIT_KB_GAP, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_GAP,
  KIT_KB_MODE(3), KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_DELETE,
  KIT_KB_MODE(4), KIT_KB_SPACE, KIT_KB_KEY, KIT_KB_MODE(4)
};
static const char *const KIT_KB_SYMBOLS[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
  "-", "/", ":", ";", "(", ")", "$", "&", "@", "\"", "\n",
  KIT_KEY_MORE, ",", "?", "!", "'", "_", "*", "+", LV_SYMBOL_BACKSPACE, "\n",
  KIT_KEY_LETTERS, " ", ".", LV_SYMBOL_OK, ""
};
static const char *const KIT_KB_SYMBOLS2[] = {
  "[", "]", "{", "}", "#", "%", "^", "*", "+", "=", "\n",
  "_", "\\", "|", "~", "<", ">", "`", "$", "&", "@", "\n",
  KIT_KEY_DIGITS, ",", "?", "!", "'", "\"", ":", ";", LV_SYMBOL_BACKSPACE, "\n",
  KIT_KEY_LETTERS, " ", ".", LV_SYMBOL_OK, ""
};
static const lv_buttonmatrix_ctrl_t KIT_KB_SYMBOLS_CTRL[] = {
  KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY,
  KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY,
  KIT_KB_MODE(3), KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_KEY, KIT_KB_DELETE,
  KIT_KB_MODE(4), KIT_KB_SPACE, KIT_KB_KEY, KIT_KB_MODE(4)
};

static bool kit_kb_unshift;  // a letter was typed with shift: back to lower case on release

static void kit_kb_key_cb(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_current_target_obj(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(kb);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
  const char *key = lv_buttonmatrix_get_button_text(kb, id);
  lv_obj_t *ta = lv_keyboard_get_textarea(kb);
  if (!key || !ta) return;
  lv_keyboard_mode_t mode = lv_keyboard_get_mode(kb);
  if (!strcmp(key, LV_SYMBOL_UP)) {
    lv_keyboard_set_mode(kb, mode == LV_KEYBOARD_MODE_TEXT_UPPER ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_TEXT_UPPER);
  } else if (!strcmp(key, KIT_KEY_LETTERS)) {
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  } else if (!strcmp(key, KIT_KEY_DIGITS)) {
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
  } else if (!strcmp(key, KIT_KEY_MORE)) {
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
  } else if (!strcmp(key, LV_SYMBOL_BACKSPACE)) {
    lv_textarea_delete_char(ta);
  } else if (!strcmp(key, LV_SYMBOL_OK)) {
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
  } else {
    lv_textarea_add_text(ta, key);
    kit_kb_unshift = mode == LV_KEYBOARD_MODE_TEXT_UPPER;
  }
}

static void kit_kb_released_cb(lv_event_t *e) {
  if (!kit_kb_unshift) return;
  kit_kb_unshift = false;
  lv_keyboard_set_mode(lv_event_get_current_target_obj(e), LV_KEYBOARD_MODE_TEXT_LOWER);
}

// The check key in the accent colour, an active shift key white.
static void kit_kb_draw_cb(lv_event_t *e) {
  lv_draw_task_t *task = lv_event_get_draw_task(e);
  lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
  if (base->part != LV_PART_ITEMS) return;
  lv_obj_t *kb = lv_event_get_current_target_obj(e);
  const char *key = lv_buttonmatrix_get_button_text(kb, base->id1);
  if (!key) return;
  bool ok = !strcmp(key, LV_SYMBOL_OK);
  bool shift = !strcmp(key, LV_SYMBOL_UP) && lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_UPPER;
  if (!ok && !shift) return;
  lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
  if (fill) fill->color = lv_color_hex(ok ? KIT_COLOR_ACCENT : 0xFFFFFF);
  lv_draw_label_dsc_t *label = lv_draw_task_get_label_dsc(task);
  if (label && shift) label->color = lv_color_black();
}

static void kit_input_keyboard_cb(lv_event_t *e) {
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
  kit_action_cb_t done = (kit_action_cb_t)lv_obj_get_user_data(ta);
  audio_click();
  if (done) done((void *)lv_textarea_get_text(ta));
}

static void kit_input_eye_cb(lv_event_t *e) {
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);
  bool hidden = lv_textarea_get_password_mode(ta);
  lv_textarea_set_password_mode(ta, !hidden);
  lv_label_set_text(lv_event_get_current_target_obj(e), hidden ? LV_SYMBOL_EYE_CLOSE : ICON_EYE);
}

lv_obj_t *kit_text_input_page(const char *title, const char *placeholder, bool password, int max_len, kit_action_cb_t on_done) {
  lv_obj_t *page = kit_page_create(title, true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_set_scrollable(c, false);
  lv_obj_set_style_pad_bottom(c, 0, 0);

  lv_obj_t *row = kit_row_box(c, LV_FLEX_ALIGN_START, 10);
  lv_obj_t *ta = lv_textarea_create(row);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_password_mode(ta, password);
  lv_textarea_set_max_length(ta, max_len);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_flex_grow(ta, 1);
  lv_obj_set_style_text_font(ta, KIT_FONT_BODY, 0);
  lv_obj_set_style_bg_color(ta, lv_color_hex(KIT_COLOR_CARD), 0);
  lv_obj_set_style_border_width(ta, 0, 0);
  lv_obj_set_style_radius(ta, 20, 0);
  lv_obj_set_style_pad_hor(ta, 18, 0);
  lv_obj_set_style_pad_ver(ta, 14, 0);
  lv_obj_set_style_bg_color(ta, lv_color_hex(KIT_COLOR_ACCENT), LV_PART_CURSOR);
  lv_obj_set_focused(ta, true);
  lv_obj_set_user_data(ta, (void *)on_done);
  if (password) {
    lv_obj_t *eye = kit_round_button(row, ICON_EYE, KIT_COLOR_CARD, 56);
    lv_obj_add_event_cb(eye, kit_input_eye_cb, LV_EVENT_CLICKED, ta);
  }

  lv_obj_t *kb = lv_keyboard_create(page);
  lv_obj_set_size(kb, LV_PCT(100), 250);
  lv_obj_set_style_bg_color(kb, lv_color_black(), 0);
  lv_obj_set_style_pad_hor(kb, 6, 0);
  lv_obj_set_style_pad_top(kb, 6, 0);
  lv_obj_set_style_pad_bottom(kb, 22, 0);  // the bottom corners of the panel are round
  lv_obj_set_style_pad_column(kb, 4, 0);
  lv_obj_set_style_pad_row(kb, 6, 0);
  lv_obj_set_style_text_font(kb, KIT_FONT_BODY, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, lv_color_hex(KIT_COLOR_CARD3), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, lv_color_hex(KIT_COLOR_CARD2), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(kb, lv_color_hex(0x636366), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_white(), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_gesture_bubble(kb, false);  // typing fast must not close the page
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KIT_KB_LOWER, KIT_KB_LETTERS_CTRL);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KIT_KB_UPPER, KIT_KB_LETTERS_CTRL);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KIT_KB_SYMBOLS, KIT_KB_SYMBOLS_CTRL);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, KIT_KB_SYMBOLS2, KIT_KB_SYMBOLS_CTRL);
  lv_keyboard_set_popovers(kb, true);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(kb, kit_kb_key_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(kb, kit_kb_released_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(kb, kit_kb_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
  lv_obj_set_send_draw_task_events(kb, true);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_add_event_cb(kb, kit_input_keyboard_cb, LV_EVENT_READY, ta);
  kit_kb_unshift = false;
  return page;
}
