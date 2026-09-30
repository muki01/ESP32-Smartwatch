/*
 * nav.cpp - System navigation, the watch face picker and the BOOT button.
 *
 * Watch faces and tiles are persistent screens; everything opened from them is a page on
 * the kit stack that closes with a swipe or the BOOT button. Watch faces are built the
 * first time they are shown (or previewed in the picker), tiles at boot so the first
 * swipe is instant.
 */
#include "nav.h"

#include <Arduino.h>
#include "../../core/board.h"
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../apps/apps.h"
#include "../faces/faces.h"
#include "launcher.h"
#include "notification_center.h"
#include "quick_panel.h"
#include "screen.h"

#define NAV_BOOT_POLL_MS   20
#define NAV_BOOT_DEBOUNCE  2       // stable polls before a change counts
#define NAV_THUMB_W        (LCD_WIDTH / 2)
#define NAV_THUMB_H        (LCD_HEIGHT / 2)

static lv_obj_t *nav_faces[FACE_COUNT];
static lv_obj_t *nav_tiles[TILE_COUNT];
static bool nav_boot_down;
static uint8_t nav_boot_stable;

static const char *const NAV_FACE_NAMES[FACE_COUNT] = { "Digital", "Analog", "Modular", "Minimal" };
static lv_obj_t *(*const NAV_FACE_CREATE[FACE_COUNT])() = {
  face_digital_create, face_analog_create, face_modular_create, face_minimal_create
};
static lv_obj_t *(*const NAV_TILE_CREATE[TILE_COUNT])() = {
  activity_tile_create, weather_tile_create, music_tile_create
};

static void nav_face_event_cb(lv_event_t *e);

/* ================================ Screens ========================================= */

static int nav_tile_index(lv_obj_t *screen) {
  for (int i = 0; i < TILE_COUNT; i++) {
    if (nav_tiles[i] && nav_tiles[i] == screen) return i;
  }
  return -1;
}

static bool nav_is_face(lv_obj_t *screen) {
  for (lv_obj_t *face : nav_faces) {
    if (face && face == screen) return true;
  }
  return false;
}

lv_obj_t *nav_face_screen(int face) {
  if (face < 0 || face >= FACE_COUNT) face = FACE_DIGITAL;
  if (!nav_faces[face]) {
    lv_obj_t *screen = NAV_FACE_CREATE[face]();
    lv_obj_add_event_cb(screen, nav_face_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen, nav_face_event_cb, LV_EVENT_LONG_PRESSED, NULL);
    nav_faces[face] = screen;
  }
  return nav_faces[face];
}

lv_obj_t *nav_home_screen() {
  return nav_face_screen(lv_subject_get_int(subj_watchface));
}

bool nav_is_home() {
  return kit_page_depth() == 0 && lv_screen_active() == nav_home_screen();
}

void nav_go_home(bool animate) {
  KitAnim anim = KIT_ANIM_NONE;
  if (animate) anim = kit_page_depth() == 0 && nav_tile_index(lv_screen_active()) >= 0 ? KIT_ANIM_SLIDE_BACK : KIT_ANIM_FADE;
  kit_reset_to(nav_home_screen(), anim);
}

void nav_show_tile(int tile) {
  if (tile >= 0 && tile < TILE_COUNT && nav_tiles[tile]) kit_reset_to(nav_tiles[tile], KIT_ANIM_SLIDE);
}

void nav_open_launcher() {
  launcher_open();
}

void nav_open_quick_panel() {
  kit_open(quick_panel_create(), KIT_ANIM_FROM_TOP, true);
}

/* ================================ Gestures ======================================== */

static void nav_face_event_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
    lv_indev_wait_release(indev);
    nav_open_face_picker();
    return;
  }
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  lv_indev_wait_release(indev);
  switch (dir) {
    case LV_DIR_BOTTOM: nav_open_quick_panel(); break;
    case LV_DIR_TOP:    nav_open_launcher(); break;
    case LV_DIR_RIGHT:  notify_open_center(); break;
    case LV_DIR_LEFT:   nav_show_tile(0); break;
    default: break;
  }
}

// Tiles scroll sideways: left = next tile, right = previous tile or the watch face.
// Up and down work as on the watch face.
static void nav_tile_gesture_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  int index = nav_tile_index(lv_event_get_current_target_obj(e));
  if (index < 0) return;
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_LEFT && index + 1 >= TILE_COUNT) return;  // last tile
  lv_indev_wait_release(indev);
  switch (dir) {
    case LV_DIR_LEFT:   kit_reset_to(nav_tiles[index + 1], KIT_ANIM_SLIDE); break;
    case LV_DIR_RIGHT:  kit_reset_to(index == 0 ? nav_home_screen() : nav_tiles[index - 1], KIT_ANIM_SLIDE_BACK); break;
    case LV_DIR_BOTTOM: nav_open_quick_panel(); break;
    case LV_DIR_TOP:    nav_open_launcher(); break;
    default: break;
  }
}

/* ================================ Face and tile decorations ======================= */

// Page indicator of the face/tile carousel: index 0 = watch face.
void nav_add_page_dots(lv_obj_t *screen, int index) {
  lv_obj_t *row = kit_container(screen);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_floating(row, true);
  for (int i = 0; i <= TILE_COUNT; i++) {
    lv_obj_t *dot = kit_container(row);
    bool on = i == index;
    lv_obj_set_size(dot, on ? 18 : 6, 6);
    lv_obj_set_style_radius(dot, 3, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(on ? 0xFFFFFF : 0x5A5A5E), 0);
  }
}

static void nav_complication_cb(lv_event_t *e) {
  app_open_fn_t open = (app_open_fn_t)lv_event_get_user_data(e);
  audio_click();
  open();
}

void nav_make_complication(lv_obj_t *obj, app_open_fn_t open) {
  if (!obj) return;
  lv_obj_set_clickable(obj, true);
  lv_obj_set_event_bubble(obj, true);
  lv_obj_set_ext_click_area(obj, 10);
  lv_obj_set_style_opa(obj, LV_OPA_50, LV_STATE_PRESSED);
  lv_obj_add_event_cb(obj, nav_complication_cb, LV_EVENT_SHORT_CLICKED, (void *)open);
}

/* ================================ Watch face picker =============================== */

// Half-size thumbnail of a screen: rendered off screen, then 2x2 box-filtered.
static lv_draw_buf_t *nav_face_thumb(lv_obj_t *face) {
  lv_obj_send_event(face, LV_EVENT_SCREEN_LOAD_START, NULL);  // faces refresh their content on this
  lv_obj_update_layout(face);
  lv_draw_buf_t *snap = lv_snapshot_take(face, LV_COLOR_FORMAT_RGB565);
  if (!snap) return NULL;
  lv_draw_buf_t *thumb = lv_draw_buf_create(NAV_THUMB_W, NAV_THUMB_H, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  if (thumb) {
    for (int32_t y = 0; y < NAV_THUMB_H; y++) {
      const uint16_t *r0 = (const uint16_t *)(snap->data + (2 * y) * snap->header.stride);
      const uint16_t *r1 = (const uint16_t *)(snap->data + (2 * y + 1) * snap->header.stride);
      uint16_t *out = (uint16_t *)(thumb->data + y * thumb->header.stride);
      for (int32_t x = 0; x < NAV_THUMB_W; x++) {
        uint16_t a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], d = r1[2 * x + 1];
        uint32_t r = ((a >> 11) + (b >> 11) + (c >> 11) + (d >> 11) + 2) >> 2;
        uint32_t g = (((a >> 5) & 0x3F) + ((b >> 5) & 0x3F) + ((c >> 5) & 0x3F) + ((d >> 5) & 0x3F) + 2) >> 2;
        uint32_t bl = ((a & 0x1F) + (b & 0x1F) + (c & 0x1F) + (d & 0x1F) + 2) >> 2;
        out[x] = (uint16_t)((r << 11) | (g << 5) | bl);
      }
    }
  }
  lv_draw_buf_destroy(snap);
  return thumb;
}

static void nav_picker_delete_cb(lv_event_t *e) {
  lv_draw_buf_t **thumbs = (lv_draw_buf_t **)lv_event_get_user_data(e);
  for (int i = 0; i < FACE_COUNT; i++) {
    // The image cache is off (LV_CACHE_DEF_SIZE 0), so nothing else refers to the buffers.
    if (thumbs[i]) lv_draw_buf_destroy(thumbs[i]);
  }
  lv_free(thumbs);
}

static void nav_picker_customize_cb(lv_event_t *e) {
  audio_click();
  face_customize_open();
}

static void nav_picker_select_cb(lv_event_t *e) {
  audio_click();
  subj_set(subj_watchface, (int32_t)(intptr_t)lv_event_get_user_data(e));
  nav_go_home(true);
}

void nav_open_face_picker() {
  lv_draw_buf_t **thumbs = (lv_draw_buf_t **)lv_malloc_zeroed(sizeof(lv_draw_buf_t *) * FACE_COUNT);
  if (!thumbs) return;
  for (int i = 0; i < FACE_COUNT; i++) thumbs[i] = nav_face_thumb(nav_face_screen(i));

  lv_obj_t *page = kit_page_create_bare(0x000000);
  kit_page_set_close_dir(page, (lv_dir_t)(LV_DIR_BOTTOM | LV_DIR_TOP));
  lv_obj_add_event_cb(page, nav_picker_delete_cb, LV_EVENT_DELETE, thumbs);

  lv_obj_t *title = kit_label(page, KIT_FONT_BODY, KIT_COLOR_TEXT2, "Watch face");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *strip = lv_obj_create(page);
  lv_obj_remove_style_all(strip);
  lv_obj_set_size(strip, LV_PCT(100), NAV_THUMB_H + 70);
  lv_obj_align(strip, LV_ALIGN_CENTER, 0, -14);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  int32_t side = (LCD_WIDTH - NAV_THUMB_W - 8) / 2;
  lv_obj_set_style_pad_left(strip, side, 0);
  lv_obj_set_style_pad_right(strip, side, 0);
  lv_obj_set_style_pad_column(strip, 22, 0);
  lv_obj_set_scroll_dir(strip, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(strip, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(strip, LV_SCROLLBAR_MODE_OFF);

  int32_t current = lv_subject_get_int(subj_watchface);
  lv_obj_t *current_item = NULL;
  for (int i = 0; i < FACE_COUNT; i++) {
    lv_obj_t *item = kit_col_box(strip, LV_FLEX_ALIGN_CENTER, 12);
    lv_obj_set_clickable(item, true);
    lv_obj_set_snappable(item, true);
    lv_obj_add_event_cb(item, nav_picker_select_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    lv_obj_set_style_opa(item, LV_OPA_60, LV_STATE_PRESSED);

    lv_obj_t *frame = kit_container(item);
    lv_obj_set_size(frame, NAV_THUMB_W + 8, NAV_THUMB_H + 8);
    lv_obj_set_style_radius(frame, 30, 0);
    lv_obj_set_style_clip_corner(frame, true, 0);
    lv_obj_set_style_border_width(frame, 4, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(i == current ? KIT_COLOR_ACCENT : KIT_COLOR_CARD2), 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(KIT_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    if (thumbs[i]) {
      lv_obj_t *img = lv_image_create(frame);
      lv_image_set_src(img, thumbs[i]);
      lv_obj_center(img);
    }
    kit_label(item, KIT_FONT_BODY, i == current ? 0xFFFFFF : KIT_COLOR_TEXT2, NAV_FACE_NAMES[i]);
    if (i == current) current_item = item;
  }

  lv_obj_t *custom = kit_chip(page, "Customize");
  lv_obj_align(custom, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_add_event_cb(custom, nav_picker_customize_cb, LV_EVENT_CLICKED, NULL);

  if (current_item) {
    lv_obj_update_layout(page);
    lv_obj_scroll_to_view(current_item, LV_ANIM_OFF);
  }
  kit_open(page, KIT_ANIM_FADE, true);
}

/* ================================ BOOT button ===================================== */

// Back home from anywhere; on the watch face it opens the app launcher.
static void nav_boot_poll_cb(lv_timer_t *t) {
  bool down = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (down == nav_boot_down) {
    nav_boot_stable = 0;
    return;
  }
  if (++nav_boot_stable < NAV_BOOT_DEBOUNCE) return;
  nav_boot_stable = 0;
  nav_boot_down = down;
  if (!down) return;  // act on press

  if (kit_alert_hw_button()) return;
  if (screen_is_off()) {
    screen_wake(WAKE_BUTTON);
    return;
  }
  lv_display_trigger_activity(lv_display_get_default());
  audio_click();
  if (nav_is_home()) nav_open_launcher();
  else nav_go_home(true);
}

/* ================================ Setup =========================================== */

// A different face chosen while a face is on screen: swap it right away.
static void nav_watchface_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *home = nav_home_screen();
  if (kit_page_depth() == 0) {
    kit_transition_finish();
    lv_obj_t *active = lv_screen_active();
    if (active != home && nav_is_face(active)) lv_screen_load(home);
  } else {
    kit_set_base(home);  // chosen in Settings: the way back ends on the new face
  }
}

void nav_init() {
  for (int i = 0; i < TILE_COUNT; i++) {
    lv_obj_t *tile = NAV_TILE_CREATE[i]();
    lv_obj_add_event_cb(tile, nav_tile_gesture_cb, LV_EVENT_GESTURE, NULL);
    nav_add_page_dots(tile, i + 1);
    nav_tiles[i] = tile;
  }
  lv_subject_add_observer(subj_watchface, nav_watchface_obs, NULL);
  lv_screen_load(nav_home_screen());

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  nav_boot_down = digitalRead(BOOT_BUTTON_PIN) == LOW;
  lv_timer_create(nav_boot_poll_cb, NAV_BOOT_POLL_MS, NULL);
}
