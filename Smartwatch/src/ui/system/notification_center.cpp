/*
 * notification_center.cpp - Notification center, detail view and the heads-up banner.
 *
 * A new notification shows a banner at the top of the screen and plays a chime, unless
 * Do not disturb is on; with "Wake screen" it also turns the display on. While the
 * center is open, new ones appear in its list and count as seen.
 */
#include "notification_center.h"

#include <Arduino.h>
#include "../../core/board.h"
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../services/clock.h"
#include "../../services/notifications.h"
#include "../../services/phone.h"
#include "../kit/kit.h"
#include "screen.h"

#define NOTIFY_BANNER_MS   5000
#define NOTIFY_BANNER_Y    10

static lv_obj_t *nc_page;             // open notification center page, or NULL
static lv_obj_t *nc_list;
static lv_obj_t *nc_banner;
static lv_timer_t *nc_banner_timer;
static uint32_t nc_banner_id;

static void nc_banner_hide();

// "now", "12 min", "14:05", "Yesterday", "28.09."
static void nc_format_age(time_t t, char *buf, size_t len) {
  time_t now = time(NULL);
  long age = (long)(now - t);
  if (age < 60) {
    strlcpy(buf, "now", len);
    return;
  }
  if (age < 3600) {
    snprintf(buf, len, "%ld min", age / 60);
    return;
  }
  struct tm then, today;
  localtime_r(&t, &then);
  localtime_r(&now, &today);
  if (then.tm_yday == today.tm_yday && then.tm_year == today.tm_year) clock_format_hm(then.tm_hour, then.tm_min, buf, len);
  else if (age < 2 * 86400 && (today.tm_yday - then.tm_yday == 1 || today.tm_yday == 0)) strlcpy(buf, "Yesterday", len);
  else snprintf(buf, len, "%02d.%02d.", then.tm_mday, then.tm_mon + 1);
}

/* ================================ Detail page ===================================== */

static void nc_dismiss_cb(lv_event_t *e) {
  uint32_t id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  audio_click();
  const Notification *n = notify_find(id);
  if (n && n->ext_id) phone_notify_dismiss(n->ext_id);
  notify_remove(id);
  kit_page_pop();
}

static void nc_open_detail(uint32_t id) {
  const Notification *n = notify_find(id);
  if (!n) return;
  lv_obj_t *page = kit_page_create(n->app, true);
  lv_obj_t *c = kit_page_content(page);

  lv_obj_t *head = kit_row_box(c, LV_FLEX_ALIGN_START, 12);
  lv_obj_set_style_pad_left(head, 4, 0);
  kit_icon(head, n->icon ? n->icon : ICON_BELL, n->color, 40);
  char age[24];
  nc_format_age(n->time, age, sizeof(age));
  kit_label(head, KIT_FONT_SMALL, KIT_COLOR_TEXT2, age);

  if (n->title[0]) {
    lv_obj_t *title = kit_label(c, KIT_FONT_TEXT, 0xFFFFFF, n->title);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_pad_hor(title, 4, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_WRAP);
  }
  if (n->body[0]) {
    lv_obj_t *body = kit_label(c, KIT_FONT_BODY, 0xE5E5EA, n->body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_pad_hor(body, 4, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
  }
  lv_obj_t *dismiss = kit_button(c, "Dismiss", KIT_COLOR_CARD2);
  lv_obj_set_style_margin_top(dismiss, 12, 0);
  lv_obj_add_event_cb(dismiss, nc_dismiss_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)n->id);
  kit_page_push(page);
}

/* ================================ Center ========================================== */

static void nc_card_clicked_cb(lv_event_t *e) {
  audio_click();
  nc_open_detail((uint32_t)(uintptr_t)lv_event_get_user_data(e));
}

static void nc_clear_all_cb(lv_event_t *e) {
  audio_click();
  notify_clear_all();
}

static lv_obj_t *nc_card(lv_obj_t *parent, const Notification *n) {
  lv_obj_t *card = kit_card(parent);
  lv_obj_set_clickable(card, true);
  lv_obj_set_style_pad_row(card, 6, 0);
  lv_obj_add_event_cb(card, nc_card_clicked_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)n->id);

  lv_obj_t *head = kit_row_box(card, LV_FLEX_ALIGN_START, 10);
  kit_icon(head, n->icon ? n->icon : ICON_BELL, n->color, 34);
  lv_obj_t *app = kit_label(head, KIT_FONT_SMALL, KIT_COLOR_TEXT2, n->app);
  lv_obj_set_flex_grow(app, 1);
  lv_obj_set_height(app, lv_font_get_line_height(KIT_FONT_SMALL));
  lv_label_set_long_mode(app, LV_LABEL_LONG_MODE_DOTS);
  char age[24];
  nc_format_age(n->time, age, sizeof(age));
  kit_label(head, KIT_FONT_SMALL, KIT_COLOR_TEXT2, age);

  if (n->title[0]) {
    lv_obj_t *title = kit_label(card, KIT_FONT_BODY, 0xFFFFFF, n->title);
    lv_obj_set_size(title, LV_PCT(100), lv_font_get_line_height(KIT_FONT_BODY));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
  }
  if (n->body[0]) {
    lv_obj_t *body = kit_label(card, KIT_FONT_SMALL, 0xC7C7CC, n->body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_update_layout(body);  // at most two lines
    int32_t two_lines = 2 * lv_font_get_line_height(KIT_FONT_SMALL) + lv_obj_get_style_text_line_space(body, LV_PART_MAIN);
    if (lv_obj_get_height(body) > two_lines) lv_obj_set_height(body, two_lines);
  }
  return card;
}

static void nc_rebuild(void *user_data) {
  if (!nc_list) return;
  lv_obj_clean(nc_list);
  int n = notify_count();
  if (n == 0) {
    kit_empty_state(nc_list, ICON_BELL, "No notifications");
    return;
  }
  for (int i = 0; i < n; i++) nc_card(nc_list, notify_get(i));
  lv_obj_t *clear = kit_button(nc_list, "Clear all", KIT_COLOR_CARD2);
  lv_obj_set_style_margin_top(clear, 6, 0);
  lv_obj_add_event_cb(clear, nc_clear_all_cb, LV_EVENT_CLICKED, NULL);
}

static void nc_changed() {
  if (!nc_page) return;
  lv_async_call_cancel(nc_rebuild, NULL);
  lv_async_call(nc_rebuild, NULL);  // never rebuild inside a card's own event
}

static void nc_count_obs(lv_observer_t *observer, lv_subject_t *subject) {
  nc_changed();
  // The banner's notification was removed: take the banner away as well.
  if (nc_banner && !notify_find(nc_banner_id)) nc_banner_hide();
}

static void nc_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(nc_rebuild, NULL);
  nc_page = NULL;
  nc_list = NULL;
}

void notify_open_center() {
  if (nc_page) return;
  lv_obj_t *page = kit_page_create("Notifications", false);
  kit_page_title_small(page);
  kit_page_set_close_dir(page, LV_DIR_LEFT);
  lv_obj_t *c = kit_page_content(page);
  nc_list = kit_container(c);
  lv_obj_set_size(nc_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(nc_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(nc_list, 10, 0);
  nc_page = page;
  lv_obj_add_event_cb(page, nc_deleted_cb, LV_EVENT_DELETE, NULL);
  nc_rebuild(NULL);
  notify_mark_all_read();
  nc_banner_hide();  // everything is in the list now
  kit_open(page, KIT_ANIM_FROM_LEFT, true);
  if (!kit_page_is_open(page)) {  // navigation was busy: the page is gone again
    nc_page = NULL;
    nc_list = NULL;
  }
}

/* ================================ Banner ========================================== */

static void nc_banner_deleted_cb(lv_event_t *e) {
  if (lv_event_get_current_target_obj(e) != nc_banner) return;  // an older banner going away
  nc_banner = NULL;
  if (nc_banner_timer) {
    lv_timer_delete(nc_banner_timer);
    nc_banner_timer = NULL;
  }
}

static void nc_banner_y_cb(void *obj, int32_t v) {
  lv_obj_set_y((lv_obj_t *)obj, v);
}

static void nc_banner_out_done_cb(lv_anim_t *a) {
  lv_obj_delete((lv_obj_t *)a->var);
}

static void nc_banner_hide() {
  if (!nc_banner) return;
  if (nc_banner_timer) {
    lv_timer_delete(nc_banner_timer);
    nc_banner_timer = NULL;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, nc_banner);
  lv_anim_set_exec_cb(&a, nc_banner_y_cb);
  lv_anim_set_values(&a, lv_obj_get_y(nc_banner), -lv_obj_get_height(nc_banner) - 20);
  lv_anim_set_duration(&a, 200);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&a, nc_banner_out_done_cb);
  lv_anim_start(&a);
}

static void nc_banner_timer_cb(lv_timer_t *t) {
  nc_banner_timer = NULL;  // one-shot: LVGL deletes it after this call
  nc_banner_hide();
}

static void nc_banner_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_GESTURE) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_TOP || dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
      lv_indev_wait_release(lv_indev_active());
      nc_banner_hide();
    }
  } else if (code == LV_EVENT_CLICKED) {
    uint32_t id = nc_banner_id;
    audio_click();
    lv_obj_delete_async(nc_banner);
    nc_banner = NULL;
    if (nc_banner_timer) {
      lv_timer_delete(nc_banner_timer);
      nc_banner_timer = NULL;
    }
    nc_open_detail(id);
  }
}

static void nc_show_banner(const Notification *n) {
  if (nc_banner) lv_obj_delete(nc_banner);
  nc_banner_id = n->id;

  lv_obj_t *b = kit_card(lv_layer_top());
  lv_obj_set_width(b, LCD_WIDTH - 24);
  lv_obj_set_style_bg_color(b, lv_color_hex(KIT_COLOR_CARD2), 0);
  lv_obj_set_style_radius(b, 28, 0);
  lv_obj_set_style_pad_row(b, 4, 0);
  lv_obj_set_clickable(b, true);
  lv_obj_set_gesture_bubble(b, false);  // swipes on the banner stay on the banner
  lv_obj_add_event_cb(b, nc_banner_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(b, nc_banner_event_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(b, nc_banner_deleted_cb, LV_EVENT_DELETE, NULL);

  lv_obj_t *head = kit_row_box(b, LV_FLEX_ALIGN_START, 10);
  kit_icon(head, n->icon ? n->icon : ICON_BELL, n->color, 32);
  lv_obj_t *app = kit_label(head, KIT_FONT_SMALL, KIT_COLOR_TEXT2, n->app);
  lv_obj_set_flex_grow(app, 1);
  lv_obj_set_height(app, lv_font_get_line_height(KIT_FONT_SMALL));
  lv_label_set_long_mode(app, LV_LABEL_LONG_MODE_DOTS);
  kit_label(head, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "now");
  const char *lines[2] = { n->title, n->body };
  const lv_font_t *fonts[2] = { KIT_FONT_BODY, KIT_FONT_SMALL };
  for (int i = 0; i < 2; i++) {
    if (!lines[i][0]) continue;
    lv_obj_t *l = kit_label(b, fonts[i], i == 0 ? 0xFFFFFF : 0xC7C7CC, lines[i]);
    lv_obj_set_size(l, LV_PCT(100), lv_font_get_line_height(fonts[i]));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  }

  lv_obj_align(b, LV_ALIGN_TOP_MID, 0, NOTIFY_BANNER_Y);
  lv_obj_update_layout(b);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, b);
  lv_anim_set_exec_cb(&a, nc_banner_y_cb);
  lv_anim_set_values(&a, -lv_obj_get_height(b) - 20, NOTIFY_BANNER_Y);
  lv_anim_set_duration(&a, 260);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  nc_banner = b;
  nc_banner_timer = lv_timer_create(nc_banner_timer_cb, NOTIFY_BANNER_MS, NULL);
  lv_timer_set_repeat_count(nc_banner_timer, 1);
}

// A new notification in the store.
static void nc_on_new(const Notification *n) {
  if (nc_page) {  // seen right away
    notify_mark_all_read();
    nc_changed();
    return;
  }
  if (lv_subject_get_int(subj_dnd)) return;  // stored silently
  if (screen_is_off() && lv_subject_get_int(subj_notif_wake)) screen_wake(WAKE_EVENT);
  if (!screen_is_off()) nc_show_banner(n);
  audio_notify();
}

void notification_center_init() {
  notify_set_listener(nc_on_new);
  lv_subject_add_observer(subj_notif_count, nc_count_obs, NULL);
}
