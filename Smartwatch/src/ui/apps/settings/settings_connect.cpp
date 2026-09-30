/*
 * settings_connect.cpp - Settings > Wi-Fi (on/off, networks, password, details, forget)
 * and Settings > Bluetooth (on/off, the Gadgetbridge phone link, name, addresses).
 */
#include "settings_internal.h"

#include <Arduino.h>
#include "../../../core/settings.h"
#include "../../../drivers/audio.h"
#include "../../../services/bluetooth.h"
#include "../../../services/phone.h"
#include "../../../services/wifi.h"

static const char *sui_signal_text(int rssi) {
  if (rssi >= -55) return "Excellent";
  if (rssi >= -67) return "Good";
  if (rssi >= -78) return "Fair";
  return "Weak";
}

/* ================================ Wi-Fi ============================================ */

static char sui_pw_ssid[33];

void sui_wifi_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), wifi_status_text());
}

static void sui_pw_done(void *text) {
  const char *pass = (const char *)text;
  size_t len = strlen(pass);
  if (len > 0 && len < 8) {
    kit_toast("At least 8 characters");
    return;
  }
  wifi_connect(sui_pw_ssid, pass);
  kit_page_pop();
}

static lv_obj_t *sui_build_wifi_password(const char *ssid) {
  strlcpy(sui_pw_ssid, ssid, sizeof(sui_pw_ssid));
  return kit_text_input_page(ssid, "Password", true, 63, sui_pw_done);
}

static void sui_wifi_forget_cb(void *user_data) {
  wifi_forget(wifi_current_ssid());
  kit_page_pop();
  kit_toast("Network forgotten");
}

static void sui_wifi_forget_ask_cb(lv_event_t *e) {
  audio_click();
  kit_confirm("Forget network?", "The saved password will be deleted.", "Forget", true, sui_wifi_forget_cb, NULL);
}

static void sui_wifi_disconnect_cb(lv_event_t *e) {
  audio_click();
  wifi_disconnect();
  kit_page_pop();
}

static lv_obj_t *sui_build_wifi_details() {
  lv_obj_t *page = kit_page_create(wifi_current_ssid(), true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *card = kit_info_card(c);
  char buf[40];
  kit_info_row(card, "Status", "Connected");
  int rssi = wifi_rssi();
  snprintf(buf, sizeof(buf), "%s (%d dBm)", sui_signal_text(rssi), rssi);
  kit_info_row(card, "Signal", buf);
  kit_info_row(card, "IP address", wifi_ip().c_str());
  kit_info_row(card, "Gateway", wifi_gateway().c_str());
  snprintf(buf, sizeof(buf), "%d", wifi_channel());
  kit_info_row(card, "Channel", buf);
  kit_info_row(card, "MAC", wifi_mac().c_str());

  lv_obj_t *disc = kit_button(c, "Disconnect", KIT_COLOR_CARD2);
  lv_obj_add_event_cb(disc, sui_wifi_disconnect_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *forget = kit_button(c, "Forget network", KIT_COLOR_RED);
  lv_obj_add_event_cb(forget, sui_wifi_forget_ask_cb, LV_EVENT_CLICKED, NULL);
  return page;
}

static void sui_wifi_status_clicked_cb(lv_event_t *e) {
  if (lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED) return;
  audio_click();
  kit_page_push(sui_build_wifi_details());
}

static void sui_wifi_status_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *row = lv_observer_get_target_obj(observer);
  int32_t state = lv_subject_get_int(subj_wifi_state);
  lv_obj_t *title = kit_row_title(row);
  lv_obj_t *sub = kit_row_subtitle(row);
  lv_obj_t *icon = lv_obj_get_child(row, 0);
  lv_obj_set_hidden(row, state == WIFI_ST_OFF);
  if (state == WIFI_ST_CONNECTED) {
    int rssi = wifi_rssi();
    lv_label_set_text(title, wifi_current_ssid());
    lv_label_set_text_fmt(sub, "Connected, %s", sui_signal_text(rssi));
    lv_obj_set_style_bg_color(icon, lv_color_hex(KIT_COLOR_GREEN), 0);
  } else {
    lv_label_set_text(title, state == WIFI_ST_CONNECTING ? "Connecting..." : "Not connected");
    lv_label_set_text(sub, state == WIFI_ST_CONNECTING ? wifi_current_ssid() : wifi_status_text());
    lv_obj_set_style_bg_color(icon, lv_color_hex(state == WIFI_ST_FAILED ? KIT_COLOR_RED : 0x48484A), 0);
  }
}

static void sui_wifi_net_clicked_cb(lv_event_t *e) {
  const WifiNet *net = wifi_result((int)(intptr_t)lv_event_get_user_data(e));
  if (!net) return;
  audio_click();
  bool failed_here = lv_subject_get_int(subj_wifi_state) == WIFI_ST_FAILED && !strcmp(wifi_current_ssid(), net->ssid);
  if (net->secure && (!net->saved || failed_here)) {
    kit_page_push(sui_build_wifi_password(net->ssid));
  } else {
    wifi_connect(net->ssid, NULL);
  }
}

static void sui_wifi_list_rebuild(void *user_data) {
  lv_obj_t *list = (lv_obj_t *)user_data;
  lv_obj_clean(list);
  int32_t state = lv_subject_get_int(subj_wifi_state);
  if (state == WIFI_ST_OFF) {
    kit_note(list, "Turn on Wi-Fi to see networks nearby.");
    return;
  }
  int shown = 0;
  const char *current = state == WIFI_ST_CONNECTED ? wifi_current_ssid() : "";
  for (int i = 0; i < wifi_result_count(); i++) {
    const WifiNet *net = wifi_result(i);
    if (!strcmp(net->ssid, current)) continue;  // shown in the status row
    char sub[40];
    snprintf(sub, sizeof(sub), "%s%s, %s", net->saved ? "Saved, " : "", net->secure ? "Secured" : "Open",
             sui_signal_text(net->rssi));
    lv_obj_t *row = kit_row(list, net->secure ? ICON_LOCK : ICON_WIFI, net->rssi >= -67 ? KIT_COLOR_BLUE : 0x48484A, net->ssid, sub, false);
    lv_obj_add_event_cb(row, sui_wifi_net_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    shown++;
  }
  if (!shown) kit_note(list, wifi_is_scanning() ? "Searching..." : "No networks found.");
}

static void sui_wifi_list_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *list = lv_observer_get_target_obj(observer);
  lv_async_call_cancel(sui_wifi_list_rebuild, list);
  lv_async_call(sui_wifi_list_rebuild, list);
}

static void sui_wifi_list_deleted_cb(lv_event_t *e) {
  lv_async_call_cancel(sui_wifi_list_rebuild, lv_event_get_current_target_obj(e));
}

static void sui_hide_if_wifi_off_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_set_hidden(lv_observer_get_target_obj(observer), lv_subject_get_int(subject) == WIFI_ST_OFF);
}

static void sui_wifi_scan_cb(lv_event_t *e) {
  audio_click();
  wifi_scan_start();
  lv_subject_notify(subj_wifi_scan);  // show "Searching..." right away
}

static void sui_wifi_rescan_timer_cb(lv_timer_t *t) {
  wifi_scan_start();
}

lv_obj_t *sui_build_wifi() {
  lv_obj_t *page = kit_page_create("Wi-Fi", true);
  lv_obj_t *c = kit_page_content(page);
  kit_switch_row(c, ICON_WIFI, KIT_COLOR_BLUE, "Wi-Fi", NULL, subj_wifi_enabled);

  lv_obj_t *status = kit_row(c, ICON_WIFI, 0x48484A, "", "", false);
  lv_obj_add_event_cb(status, sui_wifi_status_clicked_cb, LV_EVENT_CLICKED, NULL);
  lv_subject_add_observer_obj(subj_wifi_state, sui_wifi_status_obs, status, NULL);
  lv_subject_add_observer_obj(subj_wifi_scan, sui_wifi_status_obs, status, NULL);

  lv_obj_t *head = kit_row_box(c, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  kit_section(head, "NETWORKS");
  lv_obj_t *scan = kit_round_button(head, ICON_SYNC, KIT_COLOR_CARD, 44);
  lv_obj_set_ext_click_area(scan, 10);
  lv_obj_add_event_cb(scan, sui_wifi_scan_cb, LV_EVENT_CLICKED, NULL);
  lv_subject_add_observer_obj(subj_wifi_state, sui_hide_if_wifi_off_obs, head, NULL);

  lv_obj_t *list = kit_container(c);
  lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 10, 0);
  lv_obj_add_event_cb(list, sui_wifi_list_deleted_cb, LV_EVENT_DELETE, NULL);
  lv_subject_add_observer_obj(subj_wifi_scan, sui_wifi_list_obs, list, NULL);
  lv_subject_add_observer_obj(subj_wifi_state, sui_wifi_list_obs, list, NULL);

  wifi_scan_start();
  kit_page_timer(page, sui_wifi_rescan_timer_cb, 15000, NULL);
  return page;
}

/* ================================ Bluetooth ======================================= */

void sui_bt_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  static const char *const text[] = { "Off", "On, visible", "Connected" };
  lv_label_set_text(lv_observer_get_target_obj(observer), text[lv_subject_get_int(subj_bt_state)]);
}

static void sui_bt_info_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *card = lv_observer_get_target_obj(observer);
  int32_t state = lv_subject_get_int(subj_bt_state);
  lv_obj_set_hidden(card, state == BT_ST_OFF);
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(card, 0), 1), bt_name());
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(card, 1), 1), bt_address());
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(card, 2), 1), state == BT_ST_CONNECTED ? bt_peer_address() : "-");
}

static void sui_gb_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  const char *text = !lv_subject_get_int(subj_gadgetbridge) ? "Gadgetbridge app"
                     : phone_connected()                   ? "Connected"
                                                            : "Waiting for phone";
  lv_label_set_text(lv_observer_get_target_obj(observer), text);
}

lv_obj_t *sui_build_bluetooth() {
  lv_obj_t *page = kit_page_create("Bluetooth", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *sw = kit_switch_row(c, ICON_BLUETOOTH, KIT_COLOR_INDIGO, "Bluetooth", "", subj_bt_enabled);
  lv_subject_add_observer_obj(subj_bt_state, sui_bt_subtitle_obs, kit_row_subtitle(sw), NULL);

  lv_obj_t *gb = kit_switch_row(c, ICON_MOBILE, KIT_COLOR_GREEN, "Phone link", "", subj_gadgetbridge);
  lv_subject_add_observer_obj(subj_gadgetbridge, sui_gb_subtitle_obs, kit_row_subtitle(gb), NULL);
  lv_subject_add_observer_obj(subj_bt_state, sui_gb_subtitle_obs, kit_row_subtitle(gb), NULL);

  lv_obj_t *card = kit_info_card(c);
  kit_info_row(card, "Name", "-");
  kit_info_row(card, "Address", "-");
  kit_info_row(card, "Phone", "-");
  lv_subject_add_observer_obj(subj_bt_state, sui_bt_info_obs, card, NULL);
  lv_subject_add_observer_obj(subj_gadgetbridge, sui_bt_info_obs, card, NULL);

  kit_note(c, "Phone link: install Gadgetbridge on your Android phone, turn on Bluetooth and Phone link here, "
              "then add the watch in Gadgetbridge as \"Bangle.js Muki\". You get notifications, calls, music "
              "control, weather, time sync and Find phone.");
  return page;
}
