// Stubs for the PC preview: core/system, the drivers and the network/radio services, with
// plausible live data. The UI, the settings and the logic services are the real sources.
#include <map>
#include <string>
#include <vector>

#include "Arduino.h"
#include "Preferences.h"
#include "core/settings.h"
#include "core/system.h"
#include "drivers/audio.h"
#include "drivers/display.h"
#include "drivers/imu.h"
#include "drivers/power.h"
#include "drivers/rtc.h"
#include "drivers/sd_card.h"
#include "pv.h"
#include "services/bluetooth.h"
#include "services/car_link.h"
#include "services/clock.h"
#include "services/net_worker.h"
#include "services/ota.h"
#include "services/wifi.h"
#include "services/wled.h"

SerialStub Serial;
EspStub ESP;
uint32_t pv_ms = 1000;
uint32_t pv_hw_steps;
bool pv_axes_calibrated = true;
MusicState pv_music = { true, true, false, 1, 6, 84, 245 };
CarLinkState pv_car_state = CAR_LINK_CONNECTED;
CarLive pv_car_live = { true, 2150, 64, 88, 31, 400 };
bool pv_wled_scanned;

uint32_t millis() { return pv_ms; }

extern "C" time_t pv_time(time_t *out) {
  static time_t t0;
  if (!t0) {
    struct tm tm = {};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 8;
    tm.tm_mday = 29;
    tm.tm_hour = 14;
    tm.tm_min = 32;
    tm.tm_sec = 10;
    tm.tm_isdst = -1;
    t0 = mktime(&tm);
  }
  if (out) *out = t0;
  return t0;
}

/* ================================ Preferences (in memory) ========================= */

static std::map<std::string, std::vector<uint8_t>> &pv_store() {
  static std::map<std::string, std::vector<uint8_t>> m;
  return m;
}

static std::string pv_key(const std::string &ns, const char *key) { return ns + "/" + key; }

template <typename T>
static T pv_get(const std::string &ns, const char *key, T def) {
  auto it = pv_store().find(pv_key(ns, key));
  if (it == pv_store().end() || it->second.size() != sizeof(T)) return def;
  T v;
  memcpy(&v, it->second.data(), sizeof(T));
  return v;
}

template <typename T>
static size_t pv_put(const std::string &ns, const char *key, T v) {
  auto &b = pv_store()[pv_key(ns, key)];
  b.resize(sizeof(T));
  memcpy(b.data(), &v, sizeof(T));
  return sizeof(T);
}

void pv_prefs_seed_bytes(const char *ns, const char *key, const void *data, size_t len) {
  auto &b = pv_store()[pv_key(ns, key)];
  b.assign((const uint8_t *)data, (const uint8_t *)data + len);
}

void pv_prefs_seed_int(const char *ns, const char *key, int32_t v) { pv_put(ns, key, v); }

bool Preferences::begin(const char *ns, bool) { ns_ = ns; return true; }
int32_t Preferences::getInt(const char *key, int32_t def) { return pv_get(ns_, key, def); }
size_t Preferences::putInt(const char *key, int32_t v) { return pv_put(ns_, key, v); }
uint32_t Preferences::getUInt(const char *key, uint32_t def) { return pv_get(ns_, key, def); }
size_t Preferences::putUInt(const char *key, uint32_t v) { return pv_put(ns_, key, v); }
uint16_t Preferences::getUShort(const char *key, uint16_t def) { return pv_get(ns_, key, def); }
size_t Preferences::putUShort(const char *key, uint16_t v) { return pv_put(ns_, key, v); }
uint8_t Preferences::getUChar(const char *key, uint8_t def) { return pv_get(ns_, key, def); }
size_t Preferences::putUChar(const char *key, uint8_t v) { return pv_put(ns_, key, v); }
bool Preferences::getBool(const char *key, bool def) { return pv_get<uint8_t>(ns_, key, def) != 0; }
size_t Preferences::putBool(const char *key, bool v) { return pv_put<uint8_t>(ns_, key, v); }

size_t Preferences::getBytesLength(const char *key) {
  auto it = pv_store().find(pv_key(ns_, key));
  return it == pv_store().end() ? 0 : it->second.size();
}

size_t Preferences::getBytes(const char *key, void *buf, size_t len) {
  auto it = pv_store().find(pv_key(ns_, key));
  if (it == pv_store().end() || it->second.size() > len) return 0;
  memcpy(buf, it->second.data(), it->second.size());
  return it->second.size();
}

size_t Preferences::putBytes(const char *key, const void *buf, size_t len) {
  pv_prefs_seed_bytes(ns_.c_str(), key, buf, len);
  return len;
}

size_t Preferences::getString(const char *key, char *buf, size_t len) {
  auto it = pv_store().find(pv_key(ns_, key));
  if (!len) return 0;
  buf[0] = 0;
  if (it == pv_store().end()) return 0;
  size_t n = it->second.size() < len - 1 ? it->second.size() : len - 1;
  memcpy(buf, it->second.data(), n);
  buf[n] = 0;
  return n + 1;
}

String Preferences::getString(const char *key, const String def) {
  auto it = pv_store().find(pv_key(ns_, key));
  if (it == pv_store().end()) return def;
  std::string s(it->second.begin(), it->second.end());
  return String(s.c_str());
}

size_t Preferences::putString(const char *key, const char *v) {
  pv_prefs_seed_bytes(ns_.c_str(), key, v, strlen(v));
  return strlen(v);
}

bool Preferences::isKey(const char *key) { return pv_store().count(pv_key(ns_, key)) != 0; }

bool Preferences::clear() {
  std::string prefix = ns_ + "/";
  for (auto it = pv_store().begin(); it != pv_store().end();) {
    if (it->first.compare(0, prefix.size(), prefix) == 0) it = pv_store().erase(it);
    else ++it;
  }
  return true;
}

bool Preferences::remove(const char *key) { return pv_store().erase(pv_key(ns_, key)) != 0; }

/* ================================ core/system ===================================== */

static system_message_fn_t pv_message_fn;

void system_init() {}
void i2c_lock() {}
void i2c_unlock() {}
void *psram_malloc(size_t size) { return malloc(size); }
void *psram_calloc(size_t count, size_t size) { return calloc(count, size); }
void psram_free(void *p) { free(p); }
bool mem_internal_ok(uint32_t, const char *) { return true; }
void mem_log(const char *) {}
bool task_create_psram(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *, BaseType_t) {
  return true;
}
void system_ui_wait(uint32_t) {}
void system_ui_wake() {}
void system_ui_wake_from_isr() {}
void system_set_message_handler(system_message_fn_t fn) { pv_message_fn = fn; }
void system_message(const char *text) {
  if (pv_message_fn) pv_message_fn(text);
  else printf("[MSG] %s\n", text);
}
void system_set_shutdown_handler(system_shutdown_fn_t) {}
void system_restart() {}
void system_power_off() {}

void system_info(SystemInfo *out) {
  static const uint8_t WIFI[6] = { 0xDC, 0xDA, 0x0C, 0x4F, 0x21, 0xA8 };
  static const uint8_t BT[6] = { 0xDC, 0xDA, 0x0C, 0x4F, 0x21, 0xAA };
  out->chip = "ESP32-S3";
  out->revision = 2;
  out->cores = 2;
  out->cpu_mhz = 240;
  out->flash_bytes = 16u * 1024 * 1024;
  out->psram_bytes = 8u * 1024 * 1024;
  out->free_internal = 131 * 1024;
  out->free_psram = 7612 * 1024;
  memcpy(out->mac_wifi, WIFI, 6);
  memcpy(out->mac_bt, BT, 6);
  out->idf_version = "v5.5.1";
  out->core_version = "3.3.12";
}

/* ================================ Drivers ========================================= */

void display_init() {}
void display_panel_on() {}
void display_panel_off() {}
void display_set_brightness(int32_t) {}
uint32_t display_ms_since_panel_on() { return 1000; }
void display_touch_suspend(bool) {}
bool display_touch_irq_take() { return false; }
bool display_touch_detected() { return false; }
void display_touch_ignore_until_release() {}

void power_init() {}
void power_start(power_event_cb_t) {}
float power_battery_voltage() { return 4.07f; }
float power_usb_voltage() { return 0.0f; }
float power_temperature() { return 31.5f; }
void power_shutdown() {}

void audio_init() {}
void audio_click() {}
void audio_beep(uint16_t, uint16_t) {}
void audio_notify() {}
void audio_alert_start(AlertSound) {}
void audio_alert_stop() {}
void audio_listen(bool) {}
bool audio_take_clap() { return false; }

static const char *const PV_TRACKS[] = {
  "Neon Coast - Midnight Drive", "The Wavelengths - Ocean Avenue", "Aurora Lane - Northern Lights",
  "Solar Fields - Golden Hour", "Kite Parade - Paper Planes", "Field recording 03",
};
void music_get_state(MusicState *out) { *out = pv_music; }
void music_track_info(int index, char *artist, size_t alen, char *title, size_t tlen) {
  artist[0] = title[0] = 0;
  if (index < 0 || index >= 6) return;
  const char *name = PV_TRACKS[index];
  const char *sep = strstr(name, " - ");
  if (sep) {
    snprintf(artist, alen, "%.*s", (int)(sep - name), name);
    strlcpy(title, sep + 3, tlen);
  } else {
    strlcpy(title, name, tlen);
  }
}
void music_toggle() {}
void music_next() {}
void music_prev() {}
void music_play(int) {}
void music_set_repeat(bool one) { pv_music.repeat_one = one; }
bool recorder_start(const char *) { subj_set(subj_recorder, REC_RECORDING); return true; }
void recorder_stop() { subj_set(subj_recorder, REC_IDLE); }
bool recorder_play(const char *) { subj_set(subj_recorder, REC_PLAYING); return true; }
void recorder_play_stop() { subj_set(subj_recorder, REC_IDLE); }
uint32_t recorder_seconds() { return 83; }
uint32_t recorder_level() { return 64; }

void imu_init() {}
bool imu_available() { return true; }
bool imu_read_accel(float *x, float *y, float *z) { *x = 0.05f; *y = -0.03f; *z = 0.99f; return true; }
bool imu_read_raw(float *x, float *y, float *z) { *x = 0.01f; *y = 0.02f; *z = -0.99f; return true; }
bool imu_axes_calibrated() { return pv_axes_calibrated; }
void imu_calibrate_axes(int, int, int, int, float, float, float) { pv_axes_calibrated = true; }
void imu_note_viewing() {}
uint32_t imu_step_counter() { return pv_hw_steps; }
void imu_set_screen_on(bool) {}
ImuGesture imu_take_gesture() { return IMU_GESTURE_NONE; }

// Last night on the wrist: asleep 23:10-07:05 with a few restless minutes and one
// awakening, awake before and after.
int32_t imu_movement(int32_t minute) {
  int32_t now_min = (int32_t)(time(NULL) / 60);
  if (minute > now_min) return -1;
  int32_t sleep_from = now_min - (15 * 60 + 22), sleep_to = now_min - (7 * 60 + 27);
  if (minute < sleep_from || minute >= sleep_to) return 900;
  int32_t m = minute - sleep_from;
  if (m >= 240 && m < 247) return 700;
  if (m % 37 < 3) return 180;
  if (m % 53 < 4) return 120;
  return 25;
}

bool rtc_init() { return true; }
bool rtc_read(time_t *utc) { *utc = time(NULL); return true; }
void rtc_write(time_t) {}

bool sd_init() { return true; }
bool sd_available() { return true; }
uint64_t sd_total_bytes() { return 31914983424ULL; }
uint64_t sd_used_bytes() { return 184549376ULL; }
int sd_list(const char *dir, sd_file_cb_t fn, void *arg) {
  if (strcmp(dir, "/recordings")) return -1;
  fn("REC_20260929_101530.wav", 32000 * 83 + 44, arg);
  fn("REC_20260928_183005.wav", 32000 * 245 + 44, arg);
  return 2;
}
bool sd_mkdir(const char *) { return true; }
bool sd_remove(const char *) { return true; }

/* ================================ Radios and network ============================== */

void net_worker_init() {}
bool net_submit(net_job_fn_t, net_job_fn_t, void *) { return false; }
int net_http(const char *, const char *, char *, size_t, int, char *err, size_t err_len) {
  strlcpy(err, "Offline (preview)", err_len);
  return -1;
}

static const WifiNet PV_NETS[] = {
  { "HomeNetwork", -48, true, true },
  { "HomeNetwork_5G", -61, true, false },
  { "Office-Guest", -72, true, false },
  { "Cafe Free WiFi", -80, false, false },
};
void wifi_init() {}
void wifi_scan_start() {}
bool wifi_is_scanning() { return false; }
int wifi_result_count() { return 4; }
const WifiNet *wifi_result(int i) { return i >= 0 && i < 4 ? &PV_NETS[i] : NULL; }
void wifi_connect(const char *, const char *) {}
void wifi_disconnect() {}
void wifi_forget(const char *) {}
const char *wifi_current_ssid() { return "HomeNetwork"; }
const char *wifi_status_text() {
  switch (lv_subject_get_int(subj_wifi_state)) {
    case WIFI_ST_OFF: return "Off";
    case WIFI_ST_CONNECTED: return "HomeNetwork";
    default: return "Not connected";
  }
}
int wifi_rssi() { return -48; }
String wifi_ip() { return String("192.168.1.42"); }
String wifi_gateway() { return String("192.168.1.1"); }
int wifi_channel() { return 6; }
String wifi_mac() { return String("DC:DA:0C:4F:21:A8"); }

void bt_init() {}
const char *bt_address() { return "DC:DA:0C:4F:21:AA"; }
const char *bt_peer_address() { return "5C:E9:1E:07:88:3B"; }
const char *bt_name() { return lv_subject_get_int(subj_gadgetbridge) ? "Bangle.js Muki" : DEVICE_NAME; }
bool bt_uart_send(const char *, size_t) { return true; }

void ota_init() {}
OtaPhase ota_phase() { return OTA_IDLE; }
int ota_percent() { return 0; }
const char *ota_status_text() { return lv_subject_get_int(subj_ota) ? "http://192.168.1.42\nPIN 482915" : "Off"; }

/* ================================ Clock =========================================== */

struct PvZone {
  const char *name;
  const char *posix;
};
static const PvZone PV_TZ[] = {
  { "UTC", "UTC0" }, { "London, Lisbon", "GMT0BST" }, { "Berlin, Paris, Rome", "CET-1CEST" },
  { "Athens, Kyiv", "EET-2EEST" }, { "Istanbul", "TRT-3" }, { "Dubai", "GST-4" },
  { "New Delhi", "IST-5:30" }, { "Tokyo, Seoul", "JST-9" }, { "New York, Toronto", "EST5EDT" },
  { "Los Angeles", "PST8PDT" },
};
static clock_minute_cb_t pv_minute_handlers[8];
static int pv_minute_n;

void clock_init() {}
void clock_add_minute_handler(clock_minute_cb_t cb) {
  if (pv_minute_n < 8) pv_minute_handlers[pv_minute_n++] = cb;
}
void pv_run_minute_handlers() {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  for (int i = 0; i < pv_minute_n; i++) pv_minute_handlers[i](&t);
}
bool clock_is_valid() { return true; }
time_t clock_last_sync() { return time(NULL) - 600; }
int clock_tz_count() { return 10; }
const char *clock_tz_name(int i) { return i >= 0 && i < 10 ? PV_TZ[i].name : ""; }
void clock_set_local(const struct tm *) {}
void clock_apply_external(const struct tm *) {}
void clock_set_utc(time_t) {}
void clock_apply_utc_offset(int) {}
void clock_format_hm(int hour, int minute, char *buf, size_t len) {
  if (lv_subject_get_int(subj_time_24h)) snprintf(buf, len, "%02d:%02d", hour, minute);
  else snprintf(buf, len, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
}

/* ================================ WLED ============================================ */

static WledDevice pv_wled[WLED_MAX] = {
  { "Desk lamp", "192.168.1.61", true, true, false, true, 180, 0xFF8A3D, 0 },
  { "TV backlight", "wled-tv.local", true, true, false, true, 110, 0x3D7BFF, 9 },
  { "Kitchen strip", "192.168.1.64", true, true, false, false, 200, 0xFFFFFF, 0 },
  { "Garden", "192.168.1.70", true, false, false, false, 255, 0x30D158, 0 },
};
static int pv_wled_n = 4;
static const WledFound PV_FOUND[] = {
  { "Desk lamp", "192.168.1.61", true },
  { "Bedroom", "192.168.1.66", false },
  { "Hallway", "192.168.1.67", false },
};

void wled_init() {}
int wled_count() { return pv_wled_n; }
const WledDevice *wled_get(int i) { return i >= 0 && i < pv_wled_n ? &pv_wled[i] : NULL; }
bool wled_add(const char *name, const char *host) {
  if (pv_wled_n >= WLED_MAX) return false;
  WledDevice &d = pv_wled[pv_wled_n++];
  memset(&d, 0, sizeof(d));
  strlcpy(d.name, name, sizeof(d.name));
  strlcpy(d.host, host, sizeof(d.host));
  subj_bump(subj_lights);
  return true;
}
void wled_remove(int i) {
  if (i < 0 || i >= pv_wled_n) return;
  for (int k = i; k < pv_wled_n - 1; k++) pv_wled[k] = pv_wled[k + 1];
  pv_wled_n--;
  subj_bump(subj_lights);
}
void wled_refresh(int) {}
void wled_refresh_all() {}
void wled_set_power(int i, bool on) {
  if (i >= 0 && i < pv_wled_n) pv_wled[i].on = on;
  subj_bump(subj_lights);
}
void wled_toggle_all() {}
bool wled_any_on() {
  for (int i = 0; i < pv_wled_n; i++) {
    if (pv_wled[i].on && pv_wled[i].online) return true;
  }
  return false;
}
void wled_set_brightness(int i, uint8_t bri) {
  if (i >= 0 && i < pv_wled_n) pv_wled[i].bri = bri;
  subj_bump(subj_lights);
}
void wled_set_color(int i, uint32_t rgb) {
  if (i >= 0 && i < pv_wled_n) pv_wled[i].color = rgb;
  subj_bump(subj_lights);
}
void wled_set_effect(int i, uint8_t fx) {
  if (i >= 0 && i < pv_wled_n) pv_wled[i].fx = fx;
  subj_bump(subj_lights);
}
bool wled_scan() {
  pv_wled_scanned = true;
  subj_bump(subj_lights);
  return true;
}
bool wled_scanning() { return false; }
int wled_found_count() { return pv_wled_scanned ? 3 : 0; }
const WledFound *wled_found(int i) { return pv_wled_scanned && i >= 0 && i < 3 ? &PV_FOUND[i] : NULL; }

/* ================================ Car link ======================================== */

void car_link_init() {}
void car_link_set_active(bool) {}
CarLinkState car_link_state() { return pv_car_state; }
void car_link_live(CarLive *out) { *out = pv_car_live; }
bool car_link_command(const char *, bool) { return pv_car_state == CAR_LINK_CONNECTED; }
const char *car_link_host() { return "192.168.4.1"; }
void car_link_set_host(const char *) {}
