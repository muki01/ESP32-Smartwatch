/*
 * car_app.cpp - The Car app (extra): vehicle controls (lights, indicators, wipers), live
 * data (engine speed, road speed, temperatures) and the connection to the car module.
 *
 * The watch talks to the car's ESP32 ("OBD2 Master") over Wi-Fi and a WebSocket
 * (services/car_link.cpp) while this app is open: join the car's Wi-Fi network in
 * Settings > Wi-Fi once. Controls show their state in the colours of the matching
 * dashboard tell-tales; turn signals blink.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../services/car_link.h"
#include "../kit/kit.h"

enum CarControl { CAR_PARK, CAR_FOG, CAR_LOW, CAR_HIGH, CAR_LEFT, CAR_RIGHT, CAR_WIPER1, CAR_WIPER2, CAR_CONTROLS };

static const char *const CAR_NAMES[CAR_CONTROLS] = { "Park lights", "Fog lights", "Low beam", "High beam",
                                                     "Left signal", "Right signal", "Wipers 1", "Wipers 2" };
static const char *const CAR_IDS[CAR_CONTROLS] = { "park_lights", "fog_lights", "low_beam", "high_beam",
                                                   "left_signal", "right_signal", "wipers_1", "wipers_2" };
static const char *const CAR_ICONS[CAR_CONTROLS] = { ICON_PARKING, ICON_SMOG, ICON_FLASHLIGHT, ICON_SUN,
                                                     ICON_ARROW_LEFT, ICON_ARROW_RIGHT, ICON_WATER, ICON_WATER };
static const uint32_t CAR_COLORS[CAR_CONTROLS] = { KIT_COLOR_GREEN, KIT_COLOR_GREEN, KIT_COLOR_GREEN, KIT_COLOR_BLUE,
                                                   KIT_COLOR_GREEN, KIT_COLOR_GREEN, KIT_COLOR_TEAL, KIT_COLOR_TEAL };

static bool car_on[CAR_CONTROLS];
static lv_obj_t *car_tiles[CAR_CONTROLS];
static lv_obj_t *car_status_row;
static lv_obj_t *car_live_values[4];
static bool car_blink_phase;

/* ================================ Controls ======================================== */

// Off: dark card, grey icon. On: the tell-tale colour; turn signals blink.
static void car_show(int control) {
  lv_obj_t *t = car_tiles[control];
  if (!t) return;
  bool on = car_on[control];
  lv_obj_t *icon = lv_obj_get_child(t, 0);
  lv_obj_set_style_bg_color(t, lv_color_hex(on ? KIT_COLOR_CARD2 : KIT_COLOR_CARD), 0);
  lv_obj_set_style_border_width(t, on ? 2 : 0, 0);
  lv_obj_set_style_border_color(t, lv_color_hex(CAR_COLORS[control]), 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(on ? CAR_COLORS[control] : KIT_COLOR_TEXT2), 0);
  bool signal = control == CAR_LEFT || control == CAR_RIGHT;
  lv_obj_set_style_opa(icon, on && signal && !car_blink_phase ? LV_OPA_20 : LV_OPA_COVER, 0);
}

static void car_blink_cb(lv_timer_t *t) {
  car_blink_phase = !car_blink_phase;
  car_show(CAR_LEFT);
  car_show(CAR_RIGHT);
}

static void car_set(int control, bool on) {
  if (car_on[control] == on) return;
  car_on[control] = on;
  car_link_command(CAR_IDS[control], on);
  car_show(control);
}

static void car_tile_cb(lv_event_t *e) {
  int control = (int)(intptr_t)lv_event_get_user_data(e);
  audio_click();
  if (car_link_state() != CAR_LINK_CONNECTED) {
    kit_toast("Not connected to the car");
    return;
  }
  bool on = !car_on[control];
  // Pairs that cannot be on together.
  if (on && control == CAR_LEFT) car_set(CAR_RIGHT, false);
  if (on && control == CAR_RIGHT) car_set(CAR_LEFT, false);
  if (on && control == CAR_WIPER1) car_set(CAR_WIPER2, false);
  if (on && control == CAR_WIPER2) car_set(CAR_WIPER1, false);
  if (on && control == CAR_HIGH) car_set(CAR_LOW, true);  // high beam needs the low beam
  if (!on && control == CAR_LOW) car_set(CAR_HIGH, false);
  car_set(control, on);
}

/* ================================ Connection ====================================== */

static void car_status_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (!car_status_row) return;
  lv_obj_t *icon = lv_obj_get_child(car_status_row, 0);
  CarLinkState st = car_link_state();
  bool wifi = lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTED;
  const char *title = st == CAR_LINK_CONNECTED ? "Connected" : st == CAR_LINK_CONNECTING ? "Connecting..." : "Not connected";
  const char *sub = st == CAR_LINK_CONNECTED ? car_link_host() : !wifi ? "Join the car's Wi-Fi first" : "Car module not answering";
  lv_label_set_text(kit_row_title(car_status_row), title);
  lv_label_set_text(kit_row_subtitle(car_status_row), sub);
  lv_obj_set_style_bg_color(icon, lv_color_hex(st == CAR_LINK_CONNECTED ? KIT_COLOR_GREEN : st == CAR_LINK_CONNECTING ? KIT_COLOR_ORANGE : 0x48484A), 0);
}

static void car_host_done(void *text) {
  car_link_set_host((const char *)text);
  kit_page_pop();
}

static void car_host_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(kit_text_input_page("Car address", car_link_host(), false, 39, car_host_done));
}

/* ================================ Live data ======================================= */

static void car_live_timer_cb(lv_timer_t *t) {
  CarLive live;
  car_link_live(&live);
  const int32_t values[4] = { live.rpm, live.speed, live.coolant, live.intake };
  for (int i = 0; i < 4; i++) {
    if (!car_live_values[i]) continue;
    if (live.valid) lv_label_set_text_fmt(car_live_values[i], "%d", (int)values[i]);
    else lv_label_set_text(car_live_values[i], "--");
  }
}

static void car_live_deleted_cb(lv_event_t *e) {
  memset(car_live_values, 0, sizeof(car_live_values));
}

static void car_live_cb(lv_event_t *e) {
  static const char *const NAMES[4] = { "Engine", "Speed", "Coolant", "Intake air" };
  static const char *const UNITS[4] = { "rpm", "km/h", "\xC2\xB0" "C", "\xC2\xB0" "C" };
  static const char *const ICONS[4] = { ICON_TACHOMETER, ICON_ROAD, ICON_THERMOMETER, ICON_WIND };
  static const uint32_t COLORS[4] = { KIT_COLOR_ACCENT, KIT_COLOR_BLUE, KIT_COLOR_ORANGE, KIT_COLOR_TEAL };
  audio_click();
  lv_obj_t *page = kit_page_create("Live data", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *grid = kit_container(c);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, 10, 0);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *card = kit_info_card(grid);
    lv_obj_set_width(card, LV_PCT(48));
    lv_obj_set_style_pad_row(card, 2, 0);
    kit_label(card, &font_icons_24, COLORS[i], ICONS[i]);
    car_live_values[i] = kit_label(card, &font_num_44, 0xFFFFFF, "--");
    lv_obj_t *unit = kit_label(card, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");
    lv_label_set_text_fmt(unit, "%s  %s", NAMES[i], UNITS[i]);
  }
  kit_note(c, "Live values from the car module; \"--\" while it sends none.");
  lv_obj_add_event_cb(page, car_live_deleted_cb, LV_EVENT_DELETE, NULL);
  car_live_timer_cb(kit_page_timer(page, car_live_timer_cb, 500, NULL));
  kit_page_push(page);
}

/* ================================ Controls page =================================== */

static void car_page_deleted_cb(lv_event_t *e) {
  memset(car_tiles, 0, sizeof(car_tiles));
  car_status_row = NULL;
  car_link_set_active(false);
}

void car_open() {
  lv_obj_t *page = kit_page_create("Car", true);
  lv_obj_t *c = kit_page_content(page);

  car_status_row = kit_row(c, ICON_CAR, 0x48484A, "", "", true);
  lv_obj_add_event_cb(car_status_row, car_host_cb, LV_EVENT_CLICKED, NULL);
  lv_subject_add_observer_obj(subj_car, car_status_obs, car_status_row, NULL);
  lv_subject_add_observer_obj(subj_wifi_state, car_status_obs, car_status_row, NULL);

  lv_obj_t *grid = kit_container(c);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, 10, 0);
  for (int i = 0; i < CAR_CONTROLS; i++) {
    lv_obj_t *tile = kit_card(grid);
    lv_obj_set_size(tile, LV_PCT(48), 104);
    lv_obj_set_clickable(tile, true);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tile, 6, 0);
    kit_label(tile, &font_icons_32, KIT_COLOR_TEXT2, CAR_ICONS[i]);
    lv_obj_t *name = kit_label(tile, KIT_FONT_SMALL, 0xFFFFFF, CAR_NAMES[i]);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_add_event_cb(tile, car_tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    car_tiles[i] = tile;
    car_show(i);
  }
  lv_obj_t *live = kit_row(c, ICON_TACHOMETER, KIT_COLOR_ACCENT, "Live data", "Engine, speed, temperatures", true);
  lv_obj_set_style_margin_top(live, 4, 0);
  lv_obj_add_event_cb(live, car_live_cb, LV_EVENT_CLICKED, NULL);
  kit_note(c, "Tap the connection to change the car module's address.");

  lv_obj_add_event_cb(page, car_page_deleted_cb, LV_EVENT_DELETE, NULL);
  kit_page_timer(page, car_blink_cb, 400, NULL);
  car_link_set_active(true);
  kit_page_push(page);
}
