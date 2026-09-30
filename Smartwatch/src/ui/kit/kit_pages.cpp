/*
 * kit_pages.cpp - The page stack, page transitions and pages.
 *
 * kit_open() shows a page with an entry animation and remembers the screen underneath (a
 * watch face or tile) as the base. kit_page_pop() plays the reverse animation and deletes
 * the page if the stack owns it (persistent screens such as the launcher are not owned).
 * Pages close with a swipe in their close direction (right by default); pages that slide
 * in from the bottom also close when their list is pulled down past the top.
 *
 * Transitions run on snapshots: the old and the new screen are each rendered once into
 * an RGB565 image (PSRAM) and only those two images move. Animating the screens
 * themselves would render both complete widget trees for every frame. The real screen is
 * loaded when the animation ends and looks exactly like its snapshot.
 */
#include "kit_internal.h"

#include <Arduino.h>
#include "../../core/board.h"
#include "../../core/settings.h"
#include "../../drivers/audio.h"

#define KIT_PAGE_ANIM_MS    220
#define KIT_SURFACE_ANIM_MS 260
#define KIT_FADE_ANIM_MS    180
#define KIT_STACK_MAX       8

struct KitEntry {
  lv_obj_t *page;
  KitAnim anim;
  bool owned;
};

struct KitPage {
  lv_obj_t *content;
  lv_obj_t *title;     // header title label, NULL without a header
  lv_dir_t close_dir;  // gesture that closes the page (LV_DIR_NONE: only buttons)
};

struct KitTransition {
  lv_obj_t *screen;            // temporary screen showing the two images
  lv_obj_t *from_img, *to_img;
  lv_draw_buf_t *from_buf, *to_buf;
  lv_obj_t *target;            // loaded when the animation ends
  lv_obj_t *old_to_delete;     // the page that was closed, deleted at the end
  lv_screen_load_anim_t anim;
};

static KitEntry kit_stack[KIT_STACK_MAX];
static int kit_depth;  // 0: no page open; otherwise kit_stack[0] is the base screen
static uint32_t kit_nav_busy_until;
static KitTransition kit_tr;

/* ================================ Animations ====================================== */

static bool kit_nav_busy() {
  return (int32_t)(lv_tick_get() - kit_nav_busy_until) < 0;
}

void kit_nav_unblock() {
  kit_nav_busy_until = 0;
}

bool kit_stack_full() {
  return kit_depth >= KIT_STACK_MAX;
}

static lv_screen_load_anim_t kit_anim_in(KitAnim a) {
  switch (a) {
    case KIT_ANIM_SLIDE:       return LV_SCREEN_LOAD_ANIM_MOVE_LEFT;
    case KIT_ANIM_SLIDE_BACK:  return LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
    case KIT_ANIM_FROM_TOP:    return LV_SCREEN_LOAD_ANIM_OVER_BOTTOM;
    case KIT_ANIM_FROM_BOTTOM: return LV_SCREEN_LOAD_ANIM_OVER_TOP;
    case KIT_ANIM_FROM_LEFT:   return LV_SCREEN_LOAD_ANIM_OVER_RIGHT;
    case KIT_ANIM_FADE:        return LV_SCREEN_LOAD_ANIM_FADE_IN;
    default:                   return LV_SCREEN_LOAD_ANIM_NONE;
  }
}

static lv_screen_load_anim_t kit_anim_out(KitAnim a) {
  switch (a) {
    case KIT_ANIM_SLIDE:       return LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
    case KIT_ANIM_SLIDE_BACK:  return LV_SCREEN_LOAD_ANIM_MOVE_LEFT;
    case KIT_ANIM_FROM_TOP:    return LV_SCREEN_LOAD_ANIM_OUT_TOP;
    case KIT_ANIM_FROM_BOTTOM: return LV_SCREEN_LOAD_ANIM_OUT_BOTTOM;
    case KIT_ANIM_FROM_LEFT:   return LV_SCREEN_LOAD_ANIM_OUT_LEFT;
    case KIT_ANIM_FADE:        return LV_SCREEN_LOAD_ANIM_FADE_OUT;
    default:                   return LV_SCREEN_LOAD_ANIM_NONE;
  }
}

static uint32_t kit_anim_time(KitAnim a) {
  switch (a) {
    case KIT_ANIM_NONE:       return 0;
    case KIT_ANIM_SLIDE:
    case KIT_ANIM_SLIDE_BACK: return KIT_PAGE_ANIM_MS;
    case KIT_ANIM_FADE:       return KIT_FADE_ANIM_MS;
    default:                  return KIT_SURFACE_ANIM_MS;
  }
}

static void kit_tr_step_cb(void *var, int32_t v) {  // v: progress 0..1024
  const int32_t w = LCD_WIDTH, h = LCD_HEIGHT;
  int32_t in = 1024 - v;
  lv_obj_t *from = kit_tr.from_img, *to = kit_tr.to_img;
  switch (kit_tr.anim) {
    case LV_SCREEN_LOAD_ANIM_MOVE_LEFT:   lv_obj_set_x(from, -w * v / 1024); lv_obj_set_x(to, w * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_MOVE_RIGHT:  lv_obj_set_x(from, w * v / 1024); lv_obj_set_x(to, -w * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_MOVE_TOP:    lv_obj_set_y(from, -h * v / 1024); lv_obj_set_y(to, h * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM: lv_obj_set_y(from, h * v / 1024); lv_obj_set_y(to, -h * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OVER_LEFT:   lv_obj_set_x(to, w * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OVER_RIGHT:  lv_obj_set_x(to, -w * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OVER_TOP:    lv_obj_set_y(to, h * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OVER_BOTTOM: lv_obj_set_y(to, -h * in / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OUT_LEFT:    lv_obj_set_x(from, -w * v / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OUT_RIGHT:   lv_obj_set_x(from, w * v / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OUT_TOP:     lv_obj_set_y(from, -h * v / 1024); break;
    case LV_SCREEN_LOAD_ANIM_OUT_BOTTOM:  lv_obj_set_y(from, h * v / 1024); break;
    case LV_SCREEN_LOAD_ANIM_FADE_IN:     lv_obj_set_style_image_opa(to, (lv_opa_t)(255 * v / 1024), 0); break;
    case LV_SCREEN_LOAD_ANIM_FADE_OUT:    lv_obj_set_style_image_opa(from, (lv_opa_t)(255 * in / 1024), 0); break;
    default: break;
  }
}

// Ends a running transition at once: the target screen is shown for real.
void kit_transition_finish() {
  if (!kit_tr.screen) return;
  KitTransition t = kit_tr;
  memset(&kit_tr, 0, sizeof(kit_tr));
  lv_anim_delete(t.screen, kit_tr_step_cb);
  lv_screen_load(t.target);
  lv_obj_delete(t.screen);  // with both images; the image cache is off, nothing else refers to the buffers
  lv_draw_buf_destroy(t.from_buf);
  lv_draw_buf_destroy(t.to_buf);
  if (t.old_to_delete) lv_obj_delete(t.old_to_delete);
}

static void kit_tr_completed_cb(lv_anim_t *a) {
  kit_transition_finish();
}

static bool kit_tr_start(lv_obj_t *from, lv_obj_t *to, lv_screen_load_anim_t anim, uint32_t time, bool delete_old) {
  if (!from || !to || from == to) return false;
  lv_draw_buf_t *from_buf = lv_snapshot_take(from, LV_COLOR_FORMAT_RGB565);
  if (!from_buf) return false;
  lv_obj_send_event(to, LV_EVENT_SCREEN_LOAD_START, NULL);  // screens refresh their content on this
  lv_obj_update_layout(to);
  lv_draw_buf_t *to_buf = lv_snapshot_take(to, LV_COLOR_FORMAT_RGB565);
  if (!to_buf) {
    lv_draw_buf_destroy(from_buf);
    return false;
  }

  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(screen, false);
  // The closing screen is drawn on top when it slides or fades away.
  bool from_on_top = anim == LV_SCREEN_LOAD_ANIM_FADE_OUT || anim == LV_SCREEN_LOAD_ANIM_OUT_LEFT ||
                     anim == LV_SCREEN_LOAD_ANIM_OUT_RIGHT || anim == LV_SCREEN_LOAD_ANIM_OUT_TOP ||
                     anim == LV_SCREEN_LOAD_ANIM_OUT_BOTTOM;
  lv_obj_t *below = lv_image_create(screen);
  lv_obj_t *above = lv_image_create(screen);
  kit_tr.screen = screen;
  kit_tr.from_img = from_on_top ? above : below;
  kit_tr.to_img = from_on_top ? below : above;
  kit_tr.from_buf = from_buf;
  kit_tr.to_buf = to_buf;
  kit_tr.target = to;
  kit_tr.old_to_delete = delete_old ? from : NULL;
  kit_tr.anim = anim;
  lv_image_set_src(kit_tr.from_img, from_buf);
  lv_image_set_src(kit_tr.to_img, to_buf);
  kit_tr_step_cb(NULL, 0);
  lv_screen_load(screen);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, screen);
  lv_anim_set_exec_cb(&a, kit_tr_step_cb);
  lv_anim_set_values(&a, 0, 1024);
  lv_anim_set_duration(&a, time);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&a, kit_tr_completed_cb);
  lv_anim_start(&a);
  return true;
}

static void kit_load(lv_obj_t *screen, lv_screen_load_anim_t anim, uint32_t time, bool delete_old) {
  kit_transition_finish();
  lv_obj_t *from = lv_screen_active();
  if (anim == LV_SCREEN_LOAD_ANIM_NONE || time == 0 || !kit_tr_start(from, screen, anim, time, delete_old)) {
    lv_screen_load_anim(screen, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, delete_old);
  }
  kit_nav_busy_until = lv_tick_get() + (anim == LV_SCREEN_LOAD_ANIM_NONE ? 0 : time + 20);
}

/* ================================ Stack =========================================== */

void kit_open(lv_obj_t *page, KitAnim anim, bool owned) {
  if (!page) return;
  if (kit_nav_busy() || kit_depth >= KIT_STACK_MAX || kit_page_is_open(page)) {
    if (owned && !kit_page_is_open(page)) lv_obj_delete(page);
    return;
  }
  kit_modal_close();
  if (kit_depth == 0) kit_stack[kit_depth++] = { lv_screen_active(), KIT_ANIM_NONE, false };
  kit_stack[kit_depth++] = { page, anim, owned };
  kit_load(page, kit_anim_in(anim), kit_anim_time(anim), false);
}

void kit_page_push(lv_obj_t *page) {
  kit_open(page, KIT_ANIM_SLIDE, true);
}

static void kit_pop(bool force) {
  if (kit_depth <= 1 || (!force && kit_nav_busy())) return;
  KitEntry top = kit_stack[--kit_depth];
  lv_obj_t *prev = kit_stack[kit_depth - 1].page;
  if (kit_depth == 1) kit_depth = 0;  // back on the base screen
  kit_load(prev, kit_anim_out(top.anim), kit_anim_time(top.anim), top.owned);
}

// Back to the previous page; the current one is deleted when the transition finishes.
void kit_page_pop() {
  kit_pop(false);
}

void kit_pop_now() {
  kit_pop(true);
}

// Back to `page` (open somewhere below) in one transition, dropping every page above it.
void kit_page_pop_to(lv_obj_t *page) {
  if (kit_nav_busy()) return;
  int target = -1;
  for (int i = 0; i < kit_depth; i++) {
    if (kit_stack[i].page == page) target = i;
  }
  if (target < 0 || target >= kit_depth - 1) return;
  KitEntry top = kit_stack[kit_depth - 1];
  for (int i = kit_depth - 2; i > target; i--) {
    if (kit_stack[i].owned) lv_obj_delete(kit_stack[i].page);
  }
  kit_depth = target + 1;
  if (kit_depth == 1) kit_depth = 0;  // back on the base screen
  kit_load(page, kit_anim_out(top.anim), kit_anim_time(top.anim), top.owned);
}

// Takes a page that is covered by others out of the stack (and deletes it if owned), so
// going back skips it. The page on top stays.
void kit_page_remove(lv_obj_t *page) {
  for (int i = 1; i < kit_depth - 1; i++) {
    if (kit_stack[i].page != page) continue;
    bool owned = kit_stack[i].owned;
    for (int j = i; j < kit_depth - 1; j++) kit_stack[j] = kit_stack[j + 1];
    kit_depth--;
    if (owned && page != lv_screen_active()) lv_obj_delete(page);
    return;
  }
}

// Shows a persistent screen (watch face or tile) and drops every page.
void kit_reset_to(lv_obj_t *screen, KitAnim anim) {
  kit_modal_close();
  kit_transition_finish();  // the running transition's pages must be real screens again
  lv_obj_t *active = lv_screen_active();
  bool delete_active = false;
  for (int i = kit_depth - 1; i >= 1; i--) {
    if (!kit_stack[i].owned) continue;
    if (kit_stack[i].page == active) delete_active = true;
    else lv_obj_delete(kit_stack[i].page);
  }
  kit_depth = 0;
  kit_alert_forget();
  if (active != screen) kit_load(screen, kit_anim_in(anim), kit_anim_time(anim), delete_active);
}

// A different watch face was chosen while pages are open: going back ends on the new one.
void kit_set_base(lv_obj_t *screen) {
  if (kit_depth > 0 && !kit_stack[0].owned) kit_stack[0].page = screen;
}

void kit_pages_close_all() {
  if (kit_depth > 0) kit_reset_to(kit_stack[0].page, KIT_ANIM_NONE);
  else kit_modal_close();
}

bool kit_page_is_open(lv_obj_t *page) {
  for (int i = 1; i < kit_depth; i++) {
    if (kit_stack[i].page == page) return true;
  }
  return false;
}

bool kit_page_is_top(lv_obj_t *page) {
  return kit_depth > 1 && kit_stack[kit_depth - 1].page == page;
}

int kit_page_depth() {
  return kit_depth > 0 ? kit_depth - 1 : 0;
}

/* ================================ Pages =========================================== */

// A page-bound timer: deleted together with the page.
static void kit_page_timer_delete_cb(lv_event_t *e) {
  lv_timer_delete((lv_timer_t *)lv_event_get_user_data(e));
}

lv_timer_t *kit_page_timer(lv_obj_t *page, lv_timer_cb_t cb, uint32_t period, void *user_data) {
  lv_timer_t *t = lv_timer_create(cb, period, user_data);
  lv_obj_add_event_cb(page, kit_page_timer_delete_cb, LV_EVENT_DELETE, t);
  return t;
}

static void kit_page_gesture_cb(lv_event_t *e) {
  lv_obj_t *page = lv_event_get_current_target_obj(e);
  KitPage *kp = (KitPage *)lv_obj_get_user_data(page);
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
  if (!kp || !(kp->close_dir & dir) || !kit_page_is_top(page)) return;
  lv_indev_wait_release(lv_indev_active());
  kit_page_pop();
}

// Pages that close downwards also close when their list is pulled down past the top.
static void kit_content_scroll_cb(lv_event_t *e) {
  lv_obj_t *content = lv_event_get_current_target_obj(e);
  lv_obj_t *page = lv_obj_get_screen(content);
  KitPage *kp = (KitPage *)lv_obj_get_user_data(page);
  lv_indev_t *indev = lv_indev_active();
  if (!kp || !(kp->close_dir & LV_DIR_BOTTOM) || !indev || !kit_page_is_top(page)) return;
  if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED && lv_obj_get_scroll_y(content) < -45) {
    lv_indev_wait_release(indev);
    kit_page_pop();
  }
}

static void kit_page_delete_cb(lv_event_t *e) {
  lv_obj_t *page = lv_event_get_current_target_obj(e);
  lv_free(lv_obj_get_user_data(page));
  lv_obj_set_user_data(page, NULL);
}

static void kit_page_attach(lv_obj_t *page, lv_obj_t *content) {
  KitPage *kp = (KitPage *)lv_malloc(sizeof(KitPage));
  kp->content = content;
  kp->title = NULL;
  kp->close_dir = LV_DIR_RIGHT;
  lv_obj_set_user_data(page, kp);
  lv_obj_add_event_cb(page, kit_page_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(page, kit_page_delete_cb, LV_EVENT_DELETE, NULL);
}

static void kit_back_cb(lv_event_t *e) {
  audio_click();
  kit_page_pop();
}

lv_obj_t *kit_page_create(const char *title, bool with_back) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_remove_style_all(page);
  lv_obj_add_style(page, &kit_st_screen, 0);
  lv_obj_set_scrollable(page, false);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *title_label = NULL;
  if (title) {
    lv_obj_t *header = kit_container(page);
    lv_obj_add_style(header, &kit_st_header, 0);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (with_back) {
      lv_obj_t *back = kit_icon(header, ICON_CHEVRON_LEFT, KIT_COLOR_CARD, 48);
      lv_obj_add_style(back, &kit_st_card_pressed, LV_STATE_PRESSED);
      lv_obj_set_clickable(back, true);
      lv_obj_set_ext_click_area(back, 12);
      lv_obj_add_event_cb(back, kit_back_cb, LV_EVENT_CLICKED, NULL);
    }
    // One line, shortened with an ellipsis; sub pages use 28 px so it never runs into the clock.
    const lv_font_t *title_font = with_back ? KIT_FONT_TEXT : KIT_FONT_TITLE;
    title_label = kit_styled_label(header, &kit_st_title, title);
    lv_obj_set_style_text_font(title_label, title_font, 0);
    lv_obj_set_height(title_label, lv_font_get_line_height(title_font));
    lv_obj_set_flex_grow(title_label, 1);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *clock = kit_label(header, KIT_FONT_BODY, 0xFFFFFF, NULL);
    lv_label_bind_text(clock, subj_time_text, NULL);
  }

  lv_obj_t *content = lv_obj_create(page);
  lv_obj_remove_style_all(content);
  lv_obj_add_style(content, &kit_st_content, 0);
  lv_obj_add_style(content, &kit_st_scrollbar, LV_PART_SCROLLBAR);
  lv_obj_set_width(content, LV_PCT(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
  // Stays clickable: a drag that starts on a label or in a gap must still scroll the list.
  lv_obj_add_event_cb(content, kit_content_scroll_cb, LV_EVENT_SCROLL, NULL);

  kit_page_attach(page, content);
  if (title_label) ((KitPage *)lv_obj_get_user_data(page))->title = title_label;
  return page;
}

lv_obj_t *kit_page_create_bare(uint32_t bg_color) {
  lv_obj_t *page = lv_obj_create(NULL);
  lv_obj_remove_style_all(page);
  lv_obj_add_style(page, &kit_st_screen, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(bg_color), 0);
  lv_obj_set_scrollable(page, false);
  kit_page_attach(page, page);
  return page;
}

lv_obj_t *kit_page_content(lv_obj_t *page) {
  KitPage *kp = (KitPage *)lv_obj_get_user_data(page);
  return kp ? kp->content : page;
}

lv_obj_t *kit_page_title(lv_obj_t *page) {
  KitPage *kp = (KitPage *)lv_obj_get_user_data(page);
  return kp ? kp->title : NULL;
}

// Smaller title for long names on pages without a back button.
void kit_page_title_small(lv_obj_t *page) {
  lv_obj_t *t = kit_page_title(page);
  if (!t) return;
  lv_obj_set_style_text_font(t, KIT_FONT_TEXT, 0);
  lv_obj_set_height(t, lv_font_get_line_height(KIT_FONT_TEXT));
}

void kit_page_set_close_dir(lv_obj_t *page, lv_dir_t dir) {
  KitPage *kp = (KitPage *)lv_obj_get_user_data(page);
  if (kp) kp->close_dir = dir;
}
