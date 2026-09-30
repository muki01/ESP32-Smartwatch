/*
 * ota.cpp - Wireless firmware update from a web browser.
 *
 * Settings > System > Update starts a small web server (port 80) while Wi-Fi is
 * connected. Open the watch's address in a browser on the same network, pick the .bin
 * file (Arduino IDE: Sketch > Export Compiled Binary) and enter the 6-digit PIN shown on
 * the watch. The image goes to the second OTA partition; the watch restarts into it when
 * the upload is complete and valid. The server runs in its own task (internal stack: it
 * writes the flash); an LVGL timer follows its state.
 */
#include "ota.h"

#include <Arduino.h>
#include <Update.h>
#include <WebServer.h>
#include "../assets/fonts/icons.h"
#include "../core/settings.h"
#include "../core/system.h"
#include "notifications.h"
#include "wifi.h"

#define OTA_POLL_MS       250
#define OTA_TASK_STACK    8192
#define OTA_MIN_BATTERY   25
#define OTA_RESTART_MS    2000

static WebServer *ota_server;
static volatile bool ota_task_running;
static volatile bool ota_stop_request;
static volatile int ota_state = OTA_IDLE;
static volatile int ota_pct;
static volatile bool ota_rejected;
static char ota_pin[8];
static char ota_status[64];

static const char OTA_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Muki Watch update</title>
<style>body{font-family:system-ui,sans-serif;background:#000;color:#eee;max-width:440px;margin:40px auto;padding:0 18px}
h1{font-weight:500}input,button{font-size:17px;padding:12px;margin:8px 0;width:100%;box-sizing:border-box;border-radius:12px;border:0}
input{background:#1c1c1e;color:#fff}button{background:#ee1e1e;color:#fff}progress{width:100%;height:14px}p{color:#8e8e93}</style>
</head><body><h1>Muki Watch</h1><p>Installed firmware: )HTML" FW_VERSION R"HTML(<br>Choose the new .bin file and enter the PIN shown on the watch.</p>
<input id="f" type="file" accept=".bin"><input id="p" placeholder="PIN" inputmode="numeric" maxlength="6">
<button onclick="go()">Update</button><progress id="g" value="0" max="100"></progress><p id="s"></p>
<script>function go(){var f=document.getElementById('f').files[0],s=document.getElementById('s');if(!f){s.textContent='Choose a file first';return;}
var d=new FormData();d.append('firmware',f);var x=new XMLHttpRequest();
x.open('POST','/update?pin='+encodeURIComponent(document.getElementById('p').value));
x.upload.onprogress=function(e){document.getElementById('g').value=e.loaded*100/e.total;};
x.onload=function(){s.textContent=x.responseText;};x.onerror=function(){s.textContent='Upload failed';};
s.textContent='Uploading...';x.send(d);}</script></body></html>)HTML";

/* ================================ Server task ===================================== */

static void ota_handle_upload() {
  HTTPUpload &up = ota_server->upload();
  if (up.status == UPLOAD_FILE_START) {
    ota_rejected = ota_server->arg("pin") != String(ota_pin);
    if (ota_rejected) return;
    ota_pct = 0;
    ota_state = OTA_RECEIVING;
    system_ui_wake();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) ota_state = OTA_FAILED;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (ota_rejected || ota_state != OTA_RECEIVING) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      ota_state = OTA_FAILED;
      return;
    }
    size_t total = ota_server->clientContentLength();
    if (total) ota_pct = (int)min((size_t)99, (size_t)((uint64_t)up.totalSize * 100 / total));
  } else if (up.status == UPLOAD_FILE_END) {
    if (ota_rejected || ota_state != OTA_RECEIVING) return;
    ota_pct = 100;
    ota_state = Update.end(true) ? OTA_DONE : OTA_FAILED;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    if (ota_state == OTA_RECEIVING) ota_state = OTA_FAILED;
  }
}

static void ota_handle_done() {
  if (ota_rejected) ota_server->send(403, "text/plain", "Wrong PIN. Check the PIN on the watch.");
  else if (ota_state == OTA_DONE) ota_server->send(200, "text/plain", "Update complete. The watch restarts now.");
  else ota_server->send(500, "text/plain", "Update failed. Make sure you picked the right .bin file.");
}

static void ota_task(void *arg) {
  ota_server = new WebServer(80);
  ota_server->on("/", HTTP_GET, []() { ota_server->send_P(200, "text/html", OTA_HTML); });
  ota_server->on("/update", HTTP_POST, ota_handle_done, ota_handle_upload);
  ota_server->begin();
  while (!ota_stop_request) {
    ota_server->handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  ota_server->stop();
  delete ota_server;
  ota_server = NULL;
  ota_task_running = false;
  vTaskDelete(NULL);
}

/* ================================ Service (UI loop) =============================== */

static bool ota_battery_ok() {
  int32_t level = lv_subject_get_int(subj_battery);
  return level < 0 || level >= OTA_MIN_BATTERY || lv_subject_get_int(subj_charging);
}

static void ota_restart_cb(lv_timer_t *t) {
  system_restart();
}

static void ota_poll_cb(lv_timer_t *t) {
  bool want = lv_subject_get_int(subj_ota) && lv_subject_get_int(subj_wifi_state) == WIFI_ST_CONNECTED && ota_battery_ok();
  if (want && !ota_task_running) {
    ota_stop_request = false;
    ota_task_running = true;
    ota_state = OTA_IDLE;
    if (xTaskCreate(ota_task, "ota", OTA_TASK_STACK, NULL, 3, NULL) != pdPASS) ota_task_running = false;
    else Serial.printf("[OTA] listening on http://%s, PIN %s\n", wifi_ip().c_str(), ota_pin);
  } else if (!want && ota_task_running && ota_state != OTA_RECEIVING) {
    ota_stop_request = true;
  }

  static int last = OTA_IDLE;
  int state = ota_state;
  if (state == last) return;
  last = state;
  if (state == OTA_DONE) {
    system_message("Update installed, restarting");
    lv_timer_t *restart = lv_timer_create(ota_restart_cb, OTA_RESTART_MS, NULL);
    lv_timer_set_repeat_count(restart, 1);
  } else if (state == OTA_FAILED) {
    notify_post("Update", "Update failed", "The firmware file was not accepted. Nothing was changed.", ICON_WARNING, 0xFF453A);
  }
}

static void ota_enabled_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (lv_subject_get_int(subject)) snprintf(ota_pin, sizeof(ota_pin), "%06u", (unsigned)(esp_random() % 1000000));
}

/* ================================ Public API ====================================== */

void ota_init() {
  lv_subject_add_observer(subj_ota, ota_enabled_obs, NULL);
  lv_timer_create(ota_poll_cb, OTA_POLL_MS, NULL);
}

OtaPhase ota_phase() {
  return (OtaPhase)ota_state;
}

int ota_percent() {
  return ota_pct;
}

const char *ota_status_text() {
  if (!lv_subject_get_int(subj_ota)) return "Off";
  if (lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED) return "Waiting for Wi-Fi";
  if (!ota_battery_ok()) return "Battery too low, charge first";
  if (ota_state == OTA_RECEIVING) {
    snprintf(ota_status, sizeof(ota_status), "Receiving %d%%", ota_pct);
    return ota_status;
  }
  snprintf(ota_status, sizeof(ota_status), "http://%s\nPIN %s", wifi_ip().c_str(), ota_pin);
  return ota_status;
}
