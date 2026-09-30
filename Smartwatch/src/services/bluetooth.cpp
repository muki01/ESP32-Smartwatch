/*
 * bluetooth.cpp - Bluetooth LE peripheral (NimBLE).
 *
 * When enabled the watch advertises with standard GATT services that phone apps
 * understand out of the box:
 *   0x180F Battery Service      - battery level (read / notify)
 *   0x180A Device Information   - manufacturer, model, firmware
 *   0x1805 Current Time Service - the phone can read and write the watch time
 *   Nordic UART Service         - the Gadgetbridge link (phone.cpp): notifications,
 *                                 music, calls, weather, find my phone
 * With "Phone link" (Gadgetbridge) on, the watch calls itself "Bangle.js Muki" so the
 * Android app Gadgetbridge recognises it (it speaks the Bangle.js protocol).
 *
 * NimBLE callbacks run in the BLE host task and only store plain state or queue received
 * lines; an LVGL timer applies them. Turning Bluetooth off deinitialises the stack and
 * frees its RAM.
 */
#include "bluetooth.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "../core/settings.h"
#include "../core/system.h"
#include "clock.h"
#include "phone.h"

#define BT_GB_NAME       "Bangle.js Muki"
#define BT_NUS_SERVICE   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BT_NUS_RX        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // phone -> watch
#define BT_NUS_TX        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // watch -> phone
#define BT_LINE_MAX      1024
#define BT_CHUNK         20      // bytes per notification (fits the default MTU)
#define BT_MIN_FREE_KB   40      // internal RAM the controller and NimBLE take when started

static NimBLEServer *bt_server;
static NimBLECharacteristic *bt_battery_chr;
static NimBLECharacteristic *bt_tx_chr;
static bool bt_running;
static bool bt_restart;                 // re-advertise with a new name
static char bt_addr[18];
static char bt_peer[18];
static char bt_current_name[20];
static volatile bool bt_connected;
static volatile bool bt_subscribed;     // the phone listens to the UART TX
static bool bt_link_reported;
static volatile bool bt_time_pending;
static struct tm bt_time;
static QueueHandle_t bt_rx_queue;       // char * lines (heap), BLE task -> UI loop
static char bt_rx_buf[BT_LINE_MAX];     // BLE task only
static size_t bt_rx_len;

class BtServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &info) override {
    strlcpy(bt_peer, info.getAddress().toString().c_str(), sizeof(bt_peer));
    bt_rx_len = 0;
    bt_connected = true;
    system_ui_wake();
  }
  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &info, int reason) override {
    bt_connected = false;
    bt_subscribed = false;
    bt_peer[0] = 0;  // advertising restarts by itself (advertiseOnDisconnect)
    system_ui_wake();
  }
};

// Current Time characteristic (0x2A2B): year(LE16) month day hours minutes seconds
// day_of_week(1=Mon..7=Sun) fractions256 adjust_reason
class BtTimeCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    uint16_t year = t.tm_year + 1900;
    uint8_t v[10] = { (uint8_t)year, (uint8_t)(year >> 8), (uint8_t)(t.tm_mon + 1), (uint8_t)t.tm_mday,
                      (uint8_t)t.tm_hour, (uint8_t)t.tm_min, (uint8_t)t.tm_sec,
                      (uint8_t)(t.tm_wday == 0 ? 7 : t.tm_wday), 0, 0 };
    chr->setValue(v, sizeof(v));
  }
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    NimBLEAttValue v = chr->getValue();
    if (v.size() < 7) return;
    const uint8_t *d = v.data();
    memset(&bt_time, 0, sizeof(bt_time));
    bt_time.tm_year = (d[0] | (d[1] << 8)) - 1900;
    bt_time.tm_mon = d[2] - 1;
    bt_time.tm_mday = d[3];
    bt_time.tm_hour = d[4];
    bt_time.tm_min = d[5];
    bt_time.tm_sec = d[6];
    bt_time.tm_isdst = -1;
    bt_time_pending = true;
  }
};

// Nordic UART: the phone writes text; complete lines go to the UI loop.
static void bt_rx_flush() {
  if (!bt_rx_len) return;
  char *line = (char *)malloc(bt_rx_len + 1);
  if (line) {
    memcpy(line, bt_rx_buf, bt_rx_len);
    line[bt_rx_len] = 0;
    if (!bt_rx_queue || xQueueSend(bt_rx_queue, &line, 0) != pdTRUE) free(line);
    else system_ui_wake();
  }
  bt_rx_len = 0;
}

class BtUartCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override {
    NimBLEAttValue v = chr->getValue();
    const uint8_t *d = v.data();
    for (size_t i = 0; i < v.size(); i++) {
      char c = (char)d[i];
      if (c == '\n' || c == '\r') bt_rx_flush();
      else if (bt_rx_len < BT_LINE_MAX - 1) bt_rx_buf[bt_rx_len++] = c;
    }
  }
  void onSubscribe(NimBLECharacteristic *chr, NimBLEConnInfo &info, uint16_t sub_value) override {
    bt_subscribed = sub_value != 0;
  }
};

static BtServerCallbacks bt_server_cb;
static BtTimeCallbacks bt_time_cb;
static BtUartCallbacks bt_uart_cb;

static void bt_update_battery(int32_t level) {
  if (!bt_battery_chr || level < 0) return;
  uint8_t v = (uint8_t)level;
  bt_battery_chr->setValue(&v, 1);
  if (bt_connected) bt_battery_chr->notify();
}

static bool bt_start() {
  if (!mem_internal_ok(BT_MIN_FREE_KB, "Bluetooth")) return false;
  strlcpy(bt_current_name, lv_subject_get_int(subj_gadgetbridge) ? BT_GB_NAME : DEVICE_NAME, sizeof(bt_current_name));
  if (!NimBLEDevice::init(bt_current_name)) {
    NimBLEDevice::deinit(true);
    Serial.println("[BT] stack failed to start");
    return false;
  }
  bt_server = NimBLEDevice::createServer();
  bt_server->setCallbacks(&bt_server_cb, false);
  bt_server->advertiseOnDisconnect(true);

  NimBLEService *battery = bt_server->createService("180F");
  bt_battery_chr = battery->createCharacteristic("2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  bt_update_battery(lv_subject_get_int(subj_battery));

  NimBLEService *info = bt_server->createService("180A");
  info->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue("Muki");
  info->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue(DEVICE_MODEL);
  info->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue(FW_VERSION);

  NimBLEService *cts = bt_server->createService("1805");
  NimBLECharacteristic *time_chr =
    cts->createCharacteristic("2A2B", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  time_chr->setCallbacks(&bt_time_cb);

  NimBLEService *uart = bt_server->createService(BT_NUS_SERVICE);
  NimBLECharacteristic *rx = uart->createCharacteristic(BT_NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&bt_uart_cb);
  bt_tx_chr = uart->createCharacteristic(BT_NUS_TX, NIMBLE_PROPERTY::NOTIFY);
  bt_tx_chr->setCallbacks(&bt_uart_cb);

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setName(bt_current_name);
  adv->setAppearance(0x00C2);  // "Smartwatch"
  adv->addServiceUUID("180F");
  adv->enableScanResponse(true);
  adv->start();

  strlcpy(bt_addr, NimBLEDevice::getAddress().toString().c_str(), sizeof(bt_addr));
  for (char *c = bt_addr; *c; c++) *c = toupper(*c);
  bt_running = true;
  Serial.printf("[BT] advertising as \"%s\" (%s)\n", bt_current_name, bt_addr);
  mem_log("Bluetooth started");
  return true;
}

static void bt_stop() {
  bt_running = false;
  bt_battery_chr = nullptr;
  bt_tx_chr = nullptr;
  NimBLEDevice::deinit(true);
  bt_server = nullptr;
  bt_connected = false;
  bt_subscribed = false;
  bt_peer[0] = 0;
  Serial.println("[BT] off");
}

static void bt_apply_cb(lv_timer_t *t) {
  bool on = lv_subject_get_int(subj_bt_enabled);
  if (bt_restart && bt_running) {
    bt_stop();
    bt_restart = false;
  }
  if (on == bt_running) return;
  if (on && !bt_start()) {
    subj_set(subj_bt_enabled, 0);
    subj_set(subj_bt_state, BT_ST_OFF);
    system_message("Bluetooth could not start");
    return;
  }
  if (!on) bt_stop();
  subj_set(subj_bt_state, on ? BT_ST_ADVERTISING : BT_ST_OFF);
}

// Starting the BLE stack blocks for a few hundred ms: let the switch redraw first.
static void bt_schedule_apply() {
  lv_timer_t *t = lv_timer_create(bt_apply_cb, 40, NULL);
  lv_timer_set_repeat_count(t, 1);
}

static void bt_enabled_obs(lv_observer_t *observer, lv_subject_t *subject) {
  bt_schedule_apply();
}

// A different name needs a fresh start of the stack.
static void bt_gadgetbridge_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (!bt_running) return;
  bt_restart = true;
  bt_schedule_apply();
}

static void bt_battery_obs(lv_observer_t *observer, lv_subject_t *subject) {
  bt_update_battery(lv_subject_get_int(subject));
}

static void bt_poll_cb(lv_timer_t *t) {
  char *line;
  while (bt_rx_queue && xQueueReceive(bt_rx_queue, &line, 0) == pdTRUE) {
    phone_rx_line(line);
    free(line);
  }
  if (!bt_running) return;
  subj_set(subj_bt_state, bt_connected ? BT_ST_CONNECTED : BT_ST_ADVERTISING);
  bool link = bt_connected && bt_subscribed;
  if (link != bt_link_reported) {
    bt_link_reported = link;
    if (link) phone_on_connect();
    subj_bump(subj_phone);
  }
  if (bt_time_pending) {
    bt_time_pending = false;
    clock_apply_external(&bt_time);
  }
}

/* ================================ Public API ====================================== */

void bt_init() {
  bt_rx_queue = xQueueCreate(12, sizeof(char *));
  lv_subject_add_observer(subj_bt_enabled, bt_enabled_obs, NULL);
  lv_subject_add_observer(subj_gadgetbridge, bt_gadgetbridge_obs, NULL);
  lv_subject_add_observer(subj_battery, bt_battery_obs, NULL);
  lv_timer_create(bt_poll_cb, 100, NULL);
}

const char *bt_address() {
  return bt_addr[0] ? bt_addr : "-";
}

const char *bt_peer_address() {
  return bt_peer[0] ? bt_peer : "-";
}

const char *bt_name() {
  return lv_subject_get_int(subj_gadgetbridge) ? BT_GB_NAME : DEVICE_NAME;
}

// Text to the phone over the UART TX characteristic, in small notifications.
bool bt_uart_send(const char *data, size_t len) {
  if (!bt_running || !bt_tx_chr || !bt_connected || !bt_subscribed) return false;
  for (size_t off = 0; off < len; off += BT_CHUNK) {
    size_t n = len - off < BT_CHUNK ? len - off : BT_CHUNK;
    bt_tx_chr->setValue((const uint8_t *)data + off, n);
    if (!bt_tx_chr->notify()) return false;
  }
  return true;
}
