/*
 * wifi.cpp - Wi-Fi station: on/off, scanning, connecting, saved networks.
 *
 * Runs in the UI loop: scans are asynchronous and a 500 ms LVGL timer turns the driver
 * state into subj_wifi_state. The Wi-Fi event task only records the last disconnect
 * reason (plain variable, no LVGL calls). While disconnected, the saved networks are
 * looked for again every minute.
 */
#include "wifi.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "../core/settings.h"
#include "../core/system.h"

#define WIFI_NS               "wifi"
#define WIFI_MAX_RESULTS      20
#define WIFI_CONNECT_TIMEOUT  20000
#define WIFI_RETRY_MS         60000  // look for saved networks again while disconnected
#define WIFI_MIN_FREE_KB      48     // internal RAM the Wi-Fi and TCP/IP stacks take when started

struct SavedNetwork {
  char ssid[33];
  char pass[65];
};

static WifiNet wifi_results[WIFI_MAX_RESULTS];
static int wifi_count;
static bool wifi_on;
static bool wifi_scanning;
static bool wifi_autoconnect;        // connect to the best saved network after this scan
static bool wifi_connect_after_scan;
static bool wifi_user_disconnected;  // no automatic reconnects after "Disconnect"
static bool wifi_new_credentials;    // store the password once the connection works
static char wifi_target[33];
static char wifi_target_pass[65];
static uint32_t wifi_connect_ms;
static uint32_t wifi_retry_ms;
static const char *wifi_fail_text = "Connection failed";
static volatile uint8_t wifi_disc_reason;
static SavedNetwork wifi_saved[WIFI_SAVED_MAX];
static int wifi_saved_n = -1;        // -1: not loaded yet

/* ================================ Saved networks ================================== */

static void wifi_saved_load() {
  if (wifi_saved_n >= 0) return;
  wifi_saved_n = 0;
  Preferences p;
  if (!p.begin(WIFI_NS, true)) return;
  int n = constrain(p.getInt("n", 0), 0, WIFI_SAVED_MAX);
  for (int i = 0; i < n; i++) {
    char key[12];
    snprintf(key, sizeof(key), "s%d", i);
    p.getString(key, wifi_saved[i].ssid, sizeof(wifi_saved[i].ssid));
    snprintf(key, sizeof(key), "p%d", i);
    p.getString(key, wifi_saved[i].pass, sizeof(wifi_saved[i].pass));
    if (wifi_saved[i].ssid[0]) wifi_saved_n++;
  }
  p.end();
}

static void wifi_saved_store() {
  Preferences p;
  if (!p.begin(WIFI_NS, false)) return;
  p.clear();
  p.putInt("n", wifi_saved_n);
  for (int i = 0; i < wifi_saved_n; i++) {
    char key[12];
    snprintf(key, sizeof(key), "s%d", i);
    p.putString(key, wifi_saved[i].ssid);
    snprintf(key, sizeof(key), "p%d", i);
    p.putString(key, wifi_saved[i].pass);
  }
  p.end();
}

static int wifi_saved_index(const char *ssid) {
  wifi_saved_load();
  for (int i = 0; i < wifi_saved_n; i++) {
    if (!strcmp(wifi_saved[i].ssid, ssid)) return i;
  }
  return -1;
}

static bool wifi_saved_find(const char *ssid, char *pass_out, size_t pass_len) {
  int i = wifi_saved_index(ssid);
  if (i < 0) return false;
  if (pass_out && pass_len) strlcpy(pass_out, wifi_saved[i].pass, pass_len);
  return true;
}

// Most recently used network first; the oldest one drops off when the list is full.
static void wifi_saved_put(const char *ssid, const char *pass) {
  int i = wifi_saved_index(ssid);
  if (i < 0) i = wifi_saved_n < WIFI_SAVED_MAX ? wifi_saved_n++ : WIFI_SAVED_MAX - 1;
  for (; i > 0; i--) wifi_saved[i] = wifi_saved[i - 1];
  strlcpy(wifi_saved[0].ssid, ssid, sizeof(wifi_saved[0].ssid));
  strlcpy(wifi_saved[0].pass, pass ? pass : "", sizeof(wifi_saved[0].pass));
  wifi_saved_store();
}

static void wifi_saved_forget(const char *ssid) {
  int i = wifi_saved_index(ssid);
  if (i < 0) return;
  for (; i < wifi_saved_n - 1; i++) wifi_saved[i] = wifi_saved[i + 1];
  wifi_saved_n--;
  wifi_saved_store();
}

static int wifi_saved_count() {
  wifi_saved_load();
  return wifi_saved_n;
}

/* ================================ Scanning and connecting ========================= */

static void wifi_event_cb(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) wifi_disc_reason = info.wifi_sta_disconnected.reason;
}

static void wifi_collect(int n) {
  wifi_count = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;  // hidden network
    int8_t rssi = WiFi.RSSI(i);
    int dup = -1;
    for (int j = 0; j < wifi_count; j++) {
      if (ssid == wifi_results[j].ssid) dup = j;
    }
    if (dup >= 0) {  // same network on several access points: keep the strongest
      if (rssi > wifi_results[dup].rssi) wifi_results[dup].rssi = rssi;
      continue;
    }
    if (wifi_count == WIFI_MAX_RESULTS) continue;
    WifiNet &net = wifi_results[wifi_count++];
    strlcpy(net.ssid, ssid.c_str(), sizeof(net.ssid));
    net.rssi = rssi;
    net.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    net.saved = wifi_saved_find(net.ssid, NULL, 0);
  }
  WiFi.scanDelete();
  for (int i = 1; i < wifi_count; i++) {  // strongest first
    WifiNet key = wifi_results[i];
    int j = i - 1;
    for (; j >= 0 && wifi_results[j].rssi < key.rssi; j--) wifi_results[j + 1] = wifi_results[j];
    wifi_results[j + 1] = key;
  }
}

static void wifi_begin_target() {
  if (WiFi.status() == WL_CONNECTED) WiFi.disconnect();
  wifi_disc_reason = 0;
  WiFi.begin(wifi_target, wifi_target_pass[0] ? wifi_target_pass : NULL);
  wifi_connect_ms = millis();
  subj_set(subj_wifi_state, WIFI_ST_CONNECTING);
  Serial.printf("[WIFI] connecting to %s\n", wifi_target);
}

static void wifi_autoconnect_best() {
  wifi_autoconnect = false;
  if (WiFi.status() == WL_CONNECTED || wifi_user_disconnected) return;
  for (int i = 0; i < wifi_count; i++) {  // sorted by signal strength
    if (wifi_results[i].saved) {
      wifi_connect(wifi_results[i].ssid, NULL);
      return;
    }
  }
}

static void wifi_mark_saved(const char *ssid, bool saved) {
  for (int i = 0; i < wifi_count; i++) {
    if (!strcmp(wifi_results[i].ssid, ssid)) wifi_results[i].saved = saved;
  }
}

static void wifi_poll_cb(lv_timer_t *t) {
  if (!wifi_on) return;

  if (wifi_scanning) {
    int16_t n = WiFi.scanComplete();
    if (n != WIFI_SCAN_RUNNING) {
      wifi_scanning = false;
      if (n >= 0) wifi_collect(n);
      else WiFi.scanDelete();
      subj_bump(subj_wifi_scan);
      if (wifi_connect_after_scan) {
        wifi_connect_after_scan = false;
        wifi_begin_target();
      } else if (wifi_autoconnect) {
        wifi_autoconnect_best();
      }
    }
  }

  int32_t state = lv_subject_get_int(subj_wifi_state);
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    if (state != WIFI_ST_CONNECTED) {
      if (wifi_new_credentials && WiFi.SSID() == wifi_target) {
        wifi_saved_put(wifi_target, wifi_target_pass);
        wifi_mark_saved(wifi_target, true);
      }
      wifi_new_credentials = false;
      Serial.printf("[WIFI] connected to %s, IP %s, %d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
      subj_set(subj_wifi_state, WIFI_ST_CONNECTED);
    }
  } else if (state == WIFI_ST_CONNECTING) {
    uint8_t reason = wifi_disc_reason;
    bool bad_password = reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                        reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_AUTH_EXPIRE;
    bool not_found = reason == WIFI_REASON_NO_AP_FOUND || st == WL_NO_SSID_AVAIL;
    if (bad_password || not_found || st == WL_CONNECT_FAILED || millis() - wifi_connect_ms > WIFI_CONNECT_TIMEOUT) {
      wifi_fail_text = bad_password ? "Wrong password" : not_found ? "Network not found" : "Connection failed";
      Serial.printf("[WIFI] %s (reason %u)\n", wifi_fail_text, reason);
      WiFi.disconnect();
      wifi_new_credentials = false;
      wifi_retry_ms = millis();
      subj_set(subj_wifi_state, WIFI_ST_FAILED);
    }
  } else if (state == WIFI_ST_CONNECTED) {
    // Link lost: the driver reconnects on its own, give it the usual timeout.
    wifi_connect_ms = millis();
    subj_set(subj_wifi_state, WIFI_ST_CONNECTING);
  } else if (!wifi_scanning && !wifi_user_disconnected && wifi_saved_count() && millis() - wifi_retry_ms > WIFI_RETRY_MS) {
    wifi_retry_ms = millis();
    wifi_autoconnect = true;
    wifi_scan_start();
  }
}

static void wifi_apply_cb(lv_timer_t *t) {
  bool on = lv_subject_get_int(subj_wifi_enabled);
  if (on == wifi_on) return;
  wifi_on = on;
  if (on) {
    if (!mem_internal_ok(WIFI_MIN_FREE_KB, "Wi-Fi") || !WiFi.mode(WIFI_STA)) {
      WiFi.mode(WIFI_OFF);
      wifi_on = false;
      subj_set(subj_wifi_enabled, 0);
      system_message("Wi-Fi could not start");
      return;
    }
    mem_log("Wi-Fi started");
    WiFi.setSleep(true);  // modem sleep between beacons
    wifi_user_disconnected = false;
    wifi_retry_ms = millis();
    subj_set(subj_wifi_state, WIFI_ST_IDLE);
    wifi_autoconnect = wifi_saved_count() > 0;
    wifi_scan_start();
    Serial.println("[WIFI] on");
  } else {
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_scanning = false;
    wifi_autoconnect = false;
    wifi_connect_after_scan = false;
    wifi_count = 0;
    subj_set(subj_wifi_state, WIFI_ST_OFF);
    subj_bump(subj_wifi_scan);
    Serial.println("[WIFI] off");
  }
}

// Starting or stopping the radio takes ~100 ms: let the switch redraw first.
static void wifi_enabled_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_timer_t *t = lv_timer_create(wifi_apply_cb, 40, NULL);
  lv_timer_set_repeat_count(t, 1);
}

/* ================================ Public API ====================================== */

void wifi_init() {
  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(wifi_event_cb);
  lv_subject_add_observer(subj_wifi_enabled, wifi_enabled_obs, NULL);
  lv_timer_create(wifi_poll_cb, 500, NULL);
}

void wifi_scan_start() {
  if (!wifi_on || wifi_scanning) return;
  if (lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTING) return;  // the radio is busy
  int16_t r = WiFi.scanNetworks(true, false, false, 150);
  wifi_scanning = r == WIFI_SCAN_RUNNING;
  if (r >= 0) {
    wifi_collect(r);
    subj_bump(subj_wifi_scan);
  }
}

bool wifi_is_scanning() {
  return wifi_scanning;
}

int wifi_result_count() {
  return wifi_count;
}

const WifiNet *wifi_result(int index) {
  return index >= 0 && index < wifi_count ? &wifi_results[index] : NULL;
}

void wifi_connect(const char *ssid, const char *pass) {
  if (!wifi_on) return;
  wifi_new_credentials = pass != NULL;
  strlcpy(wifi_target, ssid, sizeof(wifi_target));
  if (pass) strlcpy(wifi_target_pass, pass, sizeof(wifi_target_pass));
  else if (!wifi_saved_find(ssid, wifi_target_pass, sizeof(wifi_target_pass))) wifi_target_pass[0] = 0;
  wifi_user_disconnected = false;
  if (wifi_scanning) {  // the driver cannot connect while it scans
    wifi_connect_after_scan = true;
    subj_set(subj_wifi_state, WIFI_ST_CONNECTING);
    wifi_connect_ms = millis();
    return;
  }
  wifi_begin_target();
}

void wifi_disconnect() {
  wifi_user_disconnected = true;
  WiFi.disconnect();
  if (wifi_on) subj_set(subj_wifi_state, WIFI_ST_IDLE);
}

void wifi_forget(const char *ssid) {
  char name[33];
  strlcpy(name, ssid, sizeof(name));  // ssid may point at wifi_current_ssid()'s buffer
  bool current = WiFi.status() == WL_CONNECTED && WiFi.SSID() == name;
  wifi_saved_forget(name);
  wifi_mark_saved(name, false);
  if (current) wifi_disconnect();
  subj_bump(subj_wifi_scan);
}

const char *wifi_current_ssid() {
  static String ssid;
  ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(wifi_target);
  return ssid.c_str();
}

const char *wifi_status_text() {
  static char text[56];  // "Connecting to " + 32-character SSID
  switch (lv_subject_get_int(subj_wifi_state)) {
    case WIFI_ST_OFF:        return "Off";
    case WIFI_ST_CONNECTED:  return wifi_current_ssid();
    case WIFI_ST_FAILED:     return wifi_fail_text;
    case WIFI_ST_CONNECTING:
      snprintf(text, sizeof(text), "Connecting to %s...", wifi_target);
      return text;
    default:                 return "Not connected";
  }
}

int wifi_rssi() {
  return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
}

String wifi_ip() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-");
}

String wifi_gateway() {
  return WiFi.status() == WL_CONNECTED ? WiFi.gatewayIP().toString() : String("-");
}

int wifi_channel() {
  return WiFi.status() == WL_CONNECTED ? WiFi.channel() : 0;
}

String wifi_mac() {
  return WiFi.macAddress();
}
