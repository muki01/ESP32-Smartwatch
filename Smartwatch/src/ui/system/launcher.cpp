/*
 * launcher.cpp - The app launcher, opened with a swipe up from the watch face (or the
 * BOOT button there). Pull the list down past its top, swipe right or press BOOT to go
 * back.
 *
 * Scrolling performance: the grid of icons and names is rendered once into an image
 * (PSRAM) and scrolling moves only that image - one fast copy per frame instead of
 * drawing a dozen anti-aliased circles, glyphs and labels again for every frame. The real
 * icons stay on top, invisible (opacity 0: never drawn), and receive the taps; a dark
 * circle over the pressed icon shows the touch. The launcher is a persistent screen: the
 * image is rebuilt only when the list of apps changes.
 */
#include "launcher.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../apps/apps.h"
#include "../kit/kit.h"

#define LAUNCHER_ICON_SIZE 84
#define LAUNCHER_CELL_W    116

struct LauncherApp {
  const char *name;
  const char *icon;
  uint32_t color;
  uint32_t icon_color;
  app_open_fn_t open;
  lv_subject_t **extra;  // shown only while this Extras switch is on (NULL: always)
};

static const LauncherApp LAUNCHER_APPS[] = {
  { "Activity",  ICON_ACTIVITY,   KIT_COLOR_GREEN,  0xFFFFFF, activity_open,  NULL },
  { "Workout",   ICON_RUNNING,    KIT_COLOR_ORANGE, 0xFFFFFF, workout_open,   NULL },
  { "Sleep",     ICON_BED,        KIT_COLOR_INDIGO, 0xFFFFFF, sleep_open,     NULL },
  { "Alarm",     ICON_CLOCK,      0xFF9500,         0xFFFFFF, alarm_open,     NULL },
  { "Timer",     ICON_TIMER,      KIT_COLOR_PURPLE, 0xFFFFFF, timer_open,     NULL },
  { "Stopwatch", ICON_STOPWATCH,  KIT_COLOR_TEAL,   0xFFFFFF, stopwatch_open, NULL },
  { "Weather",   ICON_CLOUD_SUN,  KIT_COLOR_BLUE,   0xFFFFFF, weather_open,   NULL },
  { "Music",     ICON_MUSIC,      KIT_COLOR_PINK,   0xFFFFFF, music_open,     NULL },
  { "Recorder",  ICON_MICROPHONE, KIT_COLOR_RED,    0xFFFFFF, recorder_open,  NULL },
  { "Calendar",  ICON_CALENDAR,   0xFF3B30,         0xFFFFFF, calendar_open,  NULL },
  { "Lights",    ICON_LIGHTS,     KIT_COLOR_YELLOW, 0x1C1C1E, lights_open,    &subj_ext_lights },
  { "Car",       ICON_CAR,        KIT_COLOR_ACCENT, 0xFFFFFF, car_open,       &subj_ext_car },
  { "Settings",  ICON_SETTINGS,   0x636366,         0xFFFFFF, settings_open,  NULL },
};
static const int LAUNCHER_APP_COUNT = sizeof(LAUNCHER_APPS) / sizeof(LAUNCHER_APPS[0]);

static lv_obj_t *launcher_page;
static lv_obj_t *launcher_stack;      // holds the image, the highlight and the real grid
static lv_obj_t *launcher_highlight;
static lv_draw_buf_t *launcher_snapshot;
static bool launcher_dirty = true;

static void launcher_click_cb(lv_event_t *e) {
  const LauncherApp *app = &LAUNCHER_APPS[(int)(intptr_t)lv_event_get_user_data(e)];
  audio_click();
  app->open();
}

// Darkens the icon of the app under the finger (the real icons are invisible).
static void launcher_press_cb(lv_event_t *e) {
  if (!launcher_highlight) return;
  if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
    lv_obj_set_hidden(launcher_highlight, true);
    return;
  }
  lv_area_t icon, stack;
  lv_obj_get_coords(lv_obj_get_child(lv_event_get_current_target_obj(e), 0), &icon);
  lv_obj_get_coords(launcher_stack, &stack);
  lv_obj_set_pos(launcher_highlight, icon.x1 - stack.x1, icon.y1 - stack.y1);
  lv_obj_set_hidden(launcher_highlight, false);
}

static void launcher_fill() {
  lv_obj_t *c = kit_page_content(launcher_page);
  lv_obj_clean(c);
  if (launcher_snapshot) {  // the image that showed it is gone with the content
    lv_draw_buf_destroy(launcher_snapshot);
    launcher_snapshot = NULL;
  }
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(c, 14, 0);
  lv_obj_set_style_pad_hor(c, 4, 0);
  lv_obj_set_style_pad_row(c, 18, 0);
  lv_obj_set_style_pad_bottom(c, 70, 0);

  // Small live clock on top, as on the other system surfaces.
  lv_obj_t *clock = kit_label(c, KIT_FONT_BODY, 0xFFFFFF, "");
  lv_obj_set_style_margin_bottom(clock, 2, 0);
  lv_label_bind_text(clock, subj_time_text, NULL);

  launcher_stack = kit_container(c);
  lv_obj_set_size(launcher_stack, LV_PCT(100), LV_SIZE_CONTENT);

  lv_obj_t *grid = kit_container(launcher_stack);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, 18, 0);
  for (int i = 0; i < LAUNCHER_APP_COUNT; i++) {
    const LauncherApp &app = LAUNCHER_APPS[i];
    if (app.extra && !lv_subject_get_int(*app.extra)) continue;
    // The whole cell (icon and name) is the touch target.
    lv_obj_t *cell = kit_col_box(grid, LV_FLEX_ALIGN_CENTER, 8);
    lv_obj_set_width(cell, LAUNCHER_CELL_W);
    lv_obj_set_clickable(cell, true);
    lv_obj_add_event_cb(cell, launcher_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(cell, launcher_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cell, launcher_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(cell, launcher_press_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *icon = kit_round_button(cell, app.icon, app.color, LAUNCHER_ICON_SIZE);
    lv_obj_set_style_text_color(icon, lv_color_hex(app.icon_color), 0);
    lv_obj_set_clickable(icon, false);
    lv_obj_t *name = kit_label(cell, KIT_FONT_SMALL, 0xE5E5EA, app.name);
    lv_obj_set_size(name, LAUNCHER_CELL_W, lv_font_get_line_height(KIT_FONT_SMALL));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(name, -1, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  }

  launcher_highlight = kit_container(launcher_stack);
  lv_obj_set_size(launcher_highlight, LAUNCHER_ICON_SIZE, LAUNCHER_ICON_SIZE);
  lv_obj_set_style_radius(launcher_highlight, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(launcher_highlight, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(launcher_highlight, LV_OPA_40, 0);
  lv_obj_set_hidden(launcher_highlight, true);
  lv_obj_move_background(launcher_highlight);

  // Render the grid once; from now on it only shows as an image.
  lv_obj_update_layout(launcher_page);
  launcher_snapshot = lv_snapshot_take(grid, LV_COLOR_FORMAT_RGB565);
  if (launcher_snapshot) {
    lv_obj_t *image = lv_image_create(launcher_stack);
    lv_image_set_src(image, launcher_snapshot);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_move_background(image);
    lv_obj_set_style_opa(grid, LV_OPA_TRANSP, 0);
  }
  launcher_dirty = false;
}

static void launcher_load_cb(lv_event_t *e) {
  if (launcher_dirty) launcher_fill();
  if (launcher_highlight) lv_obj_set_hidden(launcher_highlight, true);
  lv_obj_scroll_to_y(kit_page_content(launcher_page), 0, LV_ANIM_OFF);
}

static void launcher_extras_obs(lv_observer_t *observer, lv_subject_t *subject) {
  launcher_dirty = true;
}

void launcher_open() {
  if (!launcher_page) {
    launcher_page = kit_page_create(NULL, false);
    kit_page_set_close_dir(launcher_page, (lv_dir_t)(LV_DIR_BOTTOM | LV_DIR_RIGHT));
    lv_obj_add_event_cb(launcher_page, launcher_load_cb, LV_EVENT_SCREEN_LOAD_START, NULL);
    lv_subject_add_observer(subj_ext_car, launcher_extras_obs, NULL);
    lv_subject_add_observer(subj_ext_lights, launcher_extras_obs, NULL);
  }
  kit_open(launcher_page, KIT_ANIM_FROM_BOTTOM, false);
}
