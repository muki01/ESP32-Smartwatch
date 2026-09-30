/*
 * car_link.cpp - WebSocket client for the car module.
 *
 * A task (stack in PSRAM) keeps the connection while the Car app is open and Wi-Fi is
 * connected, reconnecting every 3 s after a failure. Text frames are parsed as JSON:
 * the numbers are taken from a "LiveData" object (or the top level), each either a plain
 * number or {"value": n}, by key: *rpm*, *speed*, *coolant*, *intake* (any case).
 * Commands are queued by the UI and sent as text frames.
 */
#include "car_link.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ws.h"
#include "../core/psram_json.h"
#include "../core/settings.h"
#include "../core/system.h"

#define CAR_NS              "car"
#define CAR_DEFAULT_HOST    "192.168.4.1"
#define CAR_PORT            80
#define CAR_PATH            "/ws"
#define CAR_TASK_STACK      6144
#define CAR_RX_MAX          1536
#define CAR_CMD_LEN         96
#define CAR_RETRY_MS        3000
#define CAR_TIMEOUT_MS      3000

static char car_host[40] = CAR_DEFAULT_HOST;
static volatile bool car_active;
static volatile uint8_t car_state = CAR_LINK_OFF;
static CarLive car_data;                 // written by the task
static portMUX_TYPE car_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t car_last_ms;
static QueueHandle_t car_cmds;           // char[CAR_CMD_LEN]
static uint8_t car_state_seen = 0xFF;
static TaskHandle_t car_task_handle;

/* ================================ Link task ======================================= */

static bool car_key_has(const char *key, const char *part) {
  char lower[32];
  size_t i = 0;
  for (; key[i] && i < sizeof(lower) - 1; i++) lower[i] = (char)tolower((unsigned char)key[i]);
  lower[i] = 0;
  return strstr(lower, part) != NULL;
}

static bool car_value(JsonVariant v, int32_t *out) {
  if (v.is<JsonObject>()) v = v["value"];
  if (!v.is<float>() && !v.is<int>()) return false;
  *out = (int32_t)lroundf(v.as<float>());
  return true;
}

static void car_parse(const char *text, size_t len) {
  JsonDocument doc(psram_json_allocator());
  if (deserializeJson(doc, text, len)) return;
  JsonObject root = doc["LiveData"].is<JsonObject>() ? doc["LiveData"].as<JsonObject>() : doc.as<JsonObject>();
  if (root.isNull()) return;
  CarLive live;
  portENTER_CRITICAL(&car_lock);
  live = car_data;
  portEXIT_CRITICAL(&car_lock);
  bool any = false;
  for (JsonPair kv : root) {
    const char *key = kv.key().c_str();
    if (car_key_has(key, "rpm")) any |= car_value(kv.value(), &live.rpm);
    else if (car_key_has(key, "speed")) any |= car_value(kv.value(), &live.speed);
    else if (car_key_has(key, "coolant")) any |= car_value(kv.value(), &live.coolant);
    else if (car_key_has(key, "intake")) any |= car_value(kv.value(), &live.intake);
  }
  if (!any) return;
  live.valid = true;
  portENTER_CRITICAL(&car_lock);
  car_data = live;
  portEXIT_CRITICAL(&car_lock);
  car_last_ms = millis();
}

static void car_set_state(CarLinkState st) {
  if (car_state == st) return;
  car_state = st;
  system_ui_wake();
}

// One connection: returns when it drops or the app is closed.
static void car_session(char *rx) {
  esp_transport_list_handle_t list = esp_transport_list_init();
  esp_transport_handle_t tcp = esp_transport_tcp_init();
  esp_transport_list_add(list, tcp, "_tcp");
  esp_transport_handle_t ws = esp_transport_ws_init(tcp);
  esp_transport_list_add(list, ws, "ws");
  esp_transport_ws_set_path(ws, CAR_PATH);
  char host[40];
  strlcpy(host, car_host, sizeof(host));
  car_set_state(CAR_LINK_CONNECTING);
  if (esp_transport_connect(ws, host, CAR_PORT, CAR_TIMEOUT_MS) < 0) {
    car_set_state(CAR_LINK_FAILED);
    esp_transport_list_destroy(list);
    return;
  }
  Serial.printf("[CAR] connected to ws://%s%s\n", host, CAR_PATH);
  car_set_state(CAR_LINK_CONNECTED);
  while (car_active && lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTED) {
    char cmd[CAR_CMD_LEN];
    while (xQueueReceive(car_cmds, cmd, 0) == pdTRUE) {
      esp_transport_ws_send_raw(ws, (ws_transport_opcodes_t)(WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN), cmd,
                                strlen(cmd), CAR_TIMEOUT_MS);
    }
    int n = esp_transport_read(ws, rx, CAR_RX_MAX - 1, 200);
    if (n < 0) break;  // closed or failed
    if (n > 0 && esp_transport_ws_get_read_opcode(ws) == WS_TRANSPORT_OPCODES_TEXT) {
      rx[n] = 0;
      car_parse(rx, n);
      system_ui_wake();
    }
  }
  esp_transport_close(ws);
  esp_transport_list_destroy(list);
  Serial.println("[CAR] disconnected");
  car_set_state(car_active ? CAR_LINK_FAILED : CAR_LINK_OFF);
}

static void car_task(void *arg) {
  char *rx = (char *)psram_malloc(CAR_RX_MAX);
  for (;;) {
    if (!car_active || lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED || !rx) {
      car_set_state(car_active ? CAR_LINK_FAILED : CAR_LINK_OFF);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    car_session(rx);
    if (car_active) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CAR_RETRY_MS));
  }
}

/* ================================ UI loop side ==================================== */

static void car_watch_cb(lv_timer_t *t) {
  static uint32_t seen_ms;
  uint32_t last = car_last_ms;
  if (car_state != car_state_seen || last != seen_ms) {
    car_state_seen = car_state;
    seen_ms = last;
    subj_bump(subj_car);
  }
}

void car_link_init() {
  Preferences p;
  if (p.begin(CAR_NS, true)) {
    p.getString("host", car_host, sizeof(car_host));
    p.end();
  }
  if (!car_host[0]) strlcpy(car_host, CAR_DEFAULT_HOST, sizeof(car_host));
  car_cmds = xQueueCreate(8, CAR_CMD_LEN);
  lv_timer_create(car_watch_cb, 250, NULL);
}

void car_link_set_active(bool on) {
  car_active = on;
  if (on && !car_task_handle) task_create_psram(car_task, "car", CAR_TASK_STACK, NULL, 2, &car_task_handle, tskNO_AFFINITY);
  if (car_task_handle) xTaskNotifyGive(car_task_handle);
}

CarLinkState car_link_state() {
  return (CarLinkState)car_state;
}

void car_link_live(CarLive *out) {
  portENTER_CRITICAL(&car_lock);
  *out = car_data;
  portEXIT_CRITICAL(&car_lock);
  out->age_ms = millis() - car_last_ms;
  if (out->valid && out->age_ms > 10000) out->valid = false;  // stale
}

bool car_link_command(const char *name, bool on) {
  if (car_state != CAR_LINK_CONNECTED || !car_cmds) return false;
  char cmd[CAR_CMD_LEN];
  snprintf(cmd, sizeof(cmd), "{\"cmd\":\"control\",\"name\":\"%s\",\"on\":%s}", name, on ? "true" : "false");
  return xQueueSend(car_cmds, cmd, 0) == pdTRUE;
}

const char *car_link_host() {
  return car_host;
}

void car_link_set_host(const char *host) {
  strlcpy(car_host, host && host[0] ? host : CAR_DEFAULT_HOST, sizeof(car_host));
  Preferences p;
  if (p.begin(CAR_NS, false)) {
    p.putString("host", car_host);
    p.end();
  }
  if (car_task_handle) xTaskNotifyGive(car_task_handle);
}
