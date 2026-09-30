/*
 * lights_app.cpp - The Lights app (extra): WLED lights on the same Wi-Fi.
 *
 *   Lights           "All lights" switch, one row per light (colour dot, on/off, brightness;
 *                    the row's switch turns it on or off), "Add light", clap control
 *   Light            power button in the light's colour, brightness, colours, effects,
 *                    remove
 *   Add light        lights found on the network (mDNS), or an IP address typed in
 * Devices, state and requests: services/wled.cpp.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../services/wled.h"
#include "../kit/kit.h"

#define LIGHTS_SWATCH  52

static const uint32_t LIGHT_COLORS[] = { 0xFFA000, 0xFFFFFF, 0xFF2000, 0xFF7000, 0xFFE000,
                                         0x00FF20, 0x00E0FF, 0x0040FF, 0x8000FF, 0xFF00A0 };
struct LightEffect {
  const char *name;
  uint8_t fx;  // WLED effect id
};
static const LightEffect LIGHT_EFFECTS[] = {
  { "Solid", 0 }, { "Breathe", 2 }, { "Rainbow", 9 }, { "Colorloop", 8 }, { "Fire", 66 }, { "Pacifica", 101 },
};

static lv_obj_t *lights_list;     // content of the open Lights page
static lv_obj_t *light_page;      // open device page
static int light_index;           // its device
static lv_obj_t *light_power, *light_slider, *light_status;
static lv_obj_t *light_fx_chips[sizeof(LIGHT_EFFECTS) / sizeof(LIGHT_EFFECTS[0])];
static lv_obj_t *light_add_list;  // content of the open "Add light" page

static uint8_t lights_percent(uint8_t bri) {
  return (uint8_t)((bri * 100 + 127) / 255);
}

static const char *lights_state_text(const WledDevice *d, char *buf, size_t len) {
  if (!d->known && d->busy) strlcpy(buf, "Connecting...", len);
  else if (!d->online) strlcpy(buf, "Offline", len);
  else if (d->on) snprintf(buf, len, "On  \xE2\x80\xA2  %u%%", (unsigned)lights_percent(d->bri));
  else strlcpy(buf, "Off", len);
  return buf;
}

/* ================================ Device page ===================================== */

static void light_update(void *unused) {
  const WledDevice *d = wled_get(light_index);
  if (!light_page || !d) return;
  char buf[32];
  lv_label_set_text(light_status, lights_state_text(d, buf, sizeof(buf)));
  lv_obj_set_style_bg_color(light_power, lv_color_hex(d->on ? d->color : KIT_COLOR_CARD2), 0);
  uint32_t c = d->color;
  bool light = d->on && (((c >> 16) & 0xFF) * 3 + ((c >> 8) & 0xFF) * 6 + (c & 0xFF)) / 10 > 150;
  lv_obj_set_style_text_color(light_power, lv_color_hex(light ? 0x1C1C1E : 0xFFFFFF), 0);
  if (!lv_obj_has_state(light_slider, LV_STATE_PRESSED)) lv_slider_set_value(light_slider, lights_percent(d->bri), LV_ANIM_OFF);
  for (size_t i = 0; i < sizeof(LIGHT_EFFECTS) / sizeof(LIGHT_EFFECTS[0]); i++) {
    if (LIGHT_EFFECTS[i].fx == d->fx) lv_obj_add_state(light_fx_chips[i], LV_STATE_CHECKED);
    else lv_obj_remove_state(light_fx_chips[i], LV_STATE_CHECKED);
  }
}

static void light_power_cb(lv_event_t *e) {
  const WledDevice *d = wled_get(light_index);
  if (!d) return;
  audio_click();
  wled_set_power(light_index, !d->on);
}

static void light_bri_cb(lv_event_t *e) {
  int32_t pct = lv_slider_get_value(lv_event_get_target_obj(e));
  wled_set_brightness(light_index, (uint8_t)max((int32_t)1, pct * 255 / 100));
}

static void light_color_cb(lv_event_t *e) {
  audio_click();
  wled_set_color(light_index, LIGHT_COLORS[(int)(intptr_t)lv_event_get_user_data(e)]);
}

static void light_fx_cb(lv_event_t *e) {
  audio_click();
  wled_set_effect(light_index, LIGHT_EFFECTS[(int)(intptr_t)lv_event_get_user_data(e)].fx);
}

static void light_remove_done(void *user_data) {
  wled_remove(light_index);
  kit_page_pop();
}

static void light_remove_cb(lv_event_t *e) {
  audio_click();
  kit_confirm("Remove light?", wled_get(light_index) ? wled_get(light_index)->name : NULL, "Remove", true, light_remove_done, NULL);
}

static void light_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_async_call_cancel(light_update, NULL);
  lv_async_call(light_update, NULL);
}

static void light_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(light_update, NULL);
  light_page = NULL;
}

static void light_open(int index) {
  const WledDevice *d = wled_get(index);
  if (!d) return;
  light_index = index;
  lv_obj_t *page = kit_page_create(d->name, true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  light_power = kit_round_button(c, ICON_POWER, KIT_COLOR_CARD2, 110);
  lv_obj_add_event_cb(light_power, light_power_cb, LV_EVENT_CLICKED, NULL);
  light_status = kit_label(c, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");

  lv_obj_t *bright = kit_row_box(c, LV_FLEX_ALIGN_START, 16);
  lv_obj_set_style_pad_hor(bright, 10, 0);
  lv_obj_set_style_margin_top(bright, 8, 0);
  kit_label(bright, &font_icons_24, KIT_COLOR_TEXT2, ICON_SUN);
  light_slider = kit_slider(bright, NULL, 1, 100);
  lv_obj_set_flex_grow(light_slider, 1);
  lv_obj_add_event_cb(light_slider, light_bri_cb, LV_EVENT_RELEASED, NULL);

  lv_obj_t *colors = kit_section(c, "COLOR");
  lv_obj_set_width(colors, LV_PCT(100));
  lv_obj_t *grid = kit_container(c);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(grid, 12, 0);
  for (size_t i = 0; i < sizeof(LIGHT_COLORS) / sizeof(LIGHT_COLORS[0]); i++) {
    lv_obj_t *sw = kit_container(grid);
    lv_obj_set_size(sw, LIGHTS_SWATCH, LIGHTS_SWATCH);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(LIGHT_COLORS[i]), 0);
    lv_obj_set_style_opa(sw, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_clickable(sw, true);
    lv_obj_set_ext_click_area(sw, 4);
    lv_obj_add_event_cb(sw, light_color_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  lv_obj_t *effects = kit_section(c, "EFFECT");
  lv_obj_set_width(effects, LV_PCT(100));
  lv_obj_t *chips = kit_container(c);
  lv_obj_set_size(chips, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(chips, 10, 0);
  for (size_t i = 0; i < sizeof(LIGHT_EFFECTS) / sizeof(LIGHT_EFFECTS[0]); i++) {
    lv_obj_t *chip = kit_chip(chips, LIGHT_EFFECTS[i].name);
    lv_obj_set_width(chip, LV_PCT(48));
    lv_obj_add_event_cb(chip, light_fx_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    light_fx_chips[i] = chip;
  }

  lv_obj_t *remove = kit_button(c, "Remove", KIT_COLOR_CARD2);
  lv_obj_set_style_text_color(remove, lv_color_hex(KIT_COLOR_RED), 0);
  lv_obj_set_style_margin_top(remove, 8, 0);
  lv_obj_add_event_cb(remove, light_remove_cb, LV_EVENT_CLICKED, NULL);
  kit_note(c, d->host);

  light_page = page;
  lv_obj_add_event_cb(page, light_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_subject_add_observer_obj(subj_lights, light_obs, page, NULL);
  light_update(NULL);
  wled_refresh(index);
  kit_page_push(page);
}

/* ================================ Add a light ===================================== */

static void add_found_cb(lv_event_t *e) {
  const WledFound *f = wled_found((int)(intptr_t)lv_event_get_user_data(e));
  if (!f || f->saved) return;
  audio_click();
  if (wled_add(f->name, f->host)) kit_toast("Light added");
}

static void add_fill(void *unused) {
  lv_obj_t *c = light_add_list;
  if (!c) return;
  lv_obj_clean(c);
  if (wled_scanning()) {
    kit_empty_state(c, ICON_SEARCH, "Looking for WLED lights on this Wi-Fi...");
    return;
  }
  int n = wled_found_count();
  if (!n) {
    kit_empty_state(c, ICON_SEARCH, lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTED
                                      ? "No WLED lights found. Add one by its IP address."
                                      : "Connect to Wi-Fi first.");
    return;
  }
  for (int i = 0; i < n; i++) {
    const WledFound *f = wled_found(i);
    lv_obj_t *row = kit_row(c, ICON_LIGHTS, f->saved ? KIT_COLOR_CARD2 : KIT_COLOR_YELLOW, f->name, f->host, false);
    lv_obj_set_style_text_color(lv_obj_get_child(row, 0), lv_color_hex(f->saved ? 0xFFFFFF : 0x1C1C1E), 0);
    if (f->saved) kit_label(row, &font_icons_24, KIT_COLOR_ACCENT, ICON_CHECK);
    lv_obj_add_event_cb(row, add_found_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
}

static void add_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_async_call_cancel(add_fill, NULL);
  lv_async_call(add_fill, NULL);
}

static void add_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(add_fill, NULL);
  light_add_list = NULL;
}

static void add_scan_cb(lv_event_t *e) {
  audio_click();
  if (!wled_scan()) kit_toast(wled_scanning() ? "Already searching" : "Connect to Wi-Fi first");
}

static void add_ip_done(void *text) {
  const char *host = (const char *)text;
  if (!host[0]) return;
  if (wled_add(NULL, host)) {
    kit_toast("Light added");
    kit_page_pop();
  } else {
    kit_toast(wled_count() >= WLED_MAX ? "The list is full" : "Already added");
  }
}

static void add_ip_cb(lv_event_t *e) {
  audio_click();
  kit_page_push(kit_text_input_page("IP address", "192.168.1.50", false, 39, add_ip_done));
}

static void lights_add_open() {
  lv_obj_t *page = kit_page_create("Add light", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *scan = kit_row(c, ICON_SEARCH, KIT_COLOR_BLUE, "Search again", "On this Wi-Fi", false);
  lv_obj_add_event_cb(scan, add_scan_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *ip = kit_row(c, ICON_PLUS, KIT_COLOR_GRAY, "Enter address", "IP or host name", true);
  lv_obj_add_event_cb(ip, add_ip_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *found = kit_section(c, "FOUND");
  lv_obj_set_width(found, LV_PCT(100));
  light_add_list = kit_container(c);
  lv_obj_set_size(light_add_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(light_add_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(light_add_list, 10, 0);
  lv_obj_add_event_cb(page, add_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_subject_add_observer_obj(subj_lights, add_obs, page, NULL);
  wled_scan();
  add_fill(NULL);
  kit_page_push(page);
}

/* ================================ Lights page ===================================== */

static void lights_fill(void *unused);

static void lights_row_cb(lv_event_t *e) {
  audio_click();
  light_open((int)(intptr_t)lv_event_get_user_data(e));
}

static void lights_switch_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  audio_click();
  wled_set_power(i, lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}

static void lights_all_cb(lv_event_t *e) {
  audio_click();
  wled_toggle_all();
}

static void lights_add_cb(lv_event_t *e) {
  audio_click();
  lights_add_open();
}

static void lights_fill(void *unused) {
  lv_obj_t *c = lights_list;
  if (!c) return;
  lv_obj_clean(c);
  int n = wled_count();
  if (n) {
    lv_obj_t *all = kit_row(c, ICON_LIGHTS, KIT_COLOR_YELLOW, "All lights", wled_any_on() ? "Some are on" : "All off", false);
    lv_obj_set_style_text_color(lv_obj_get_child(all, 0), lv_color_hex(0x1C1C1E), 0);
    lv_obj_t *sw = kit_switch(all);
    if (wled_any_on()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(all, lights_all_cb, LV_EVENT_CLICKED, NULL);
  } else {
    kit_empty_state(c, ICON_LIGHTS, "No lights yet. Add the WLED lights on your Wi-Fi.");
  }
  for (int i = 0; i < n; i++) {
    const WledDevice *d = wled_get(i);
    char state[32];
    lv_obj_t *row = kit_row(c, ICON_LIGHTS, d->on && d->online ? d->color : KIT_COLOR_CARD2, d->name,
                            lights_state_text(d, state, sizeof(state)), false);
    lv_obj_add_event_cb(row, lights_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *sw = kit_switch(row);
    lv_obj_set_clickable(sw, true);
    lv_obj_set_ext_click_area(sw, 14);
    if (d->on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    if (!d->online && d->known) lv_obj_set_disabled(sw, true);
    lv_obj_add_event_cb(sw, lights_switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
  }
  if (n < WLED_MAX) {
    lv_obj_t *add = kit_row(c, ICON_PLUS, KIT_COLOR_BLUE, "Add light", NULL, true);
    lv_obj_add_event_cb(add, lights_add_cb, LV_EVENT_CLICKED, NULL);
  }
  if (n) kit_switch_row(c, ICON_HAND, KIT_COLOR_ORANGE, "Clap control", "Double clap toggles all", subj_ext_clap);
  kit_note(c, "Works with WLED lights on the same Wi-Fi as the watch.");
}

static void lights_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_async_call_cancel(lights_fill, NULL);
  lv_async_call(lights_fill, NULL);
}

static void lights_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(lights_fill, NULL);
  lights_list = NULL;
}

void lights_open() {
  lv_obj_t *page = kit_page_create("Lights", true);
  lights_list = kit_page_content(page);
  lv_obj_add_event_cb(page, lights_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_subject_add_observer_obj(subj_lights, lights_obs, page, NULL);
  lights_fill(NULL);
  wled_refresh_all();
  kit_page_push(page);
}
