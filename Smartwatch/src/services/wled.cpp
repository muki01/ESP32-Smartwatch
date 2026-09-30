/*
 * wled.cpp - WLED control over the JSON API.
 *
 *   read    GET  http://<host>/json/state          -> on, bri, seg[0].col[0], seg[0].fx
 *   name    GET  http://<host>/json/info           -> name
 *   change  POST http://<host>/json/state {..."v":true}  (the answer is the new state)
 *   find    mDNS query for _wled._tcp
 * Requests run on the network worker (one at a time, a pool of job slots); the UI loop
 * applies the answers. Commands update the shown state at once and the device's answer
 * confirms it.
 */
#include "wled.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "../core/psram_json.h"
#include "../core/settings.h"
#include "../core/system.h"
#include "net_worker.h"

#define WLED_NS          "wled"
#define WLED_JOBS        8
#define WLED_BUF_SIZE    6144
#define WLED_TIMEOUT_MS  2500
#define WLED_MDNS_NAME   "muki-watch"

enum WledJobKind : uint8_t { WJ_STATE, WJ_SEND, WJ_INFO, WJ_SCAN };

struct WledJob {
  bool used;
  WledJobKind kind;
  char host[40];
  char body[128];
  bool ok;
  bool has_state;
  bool on;
  uint8_t bri;
  uint32_t color;
  uint8_t fx;
  char name[32];
  WledFound found[WLED_FOUND_MAX];
  int found_n;
};

struct WledStored {
  char name[32];
  char host[40];
};

static WledDevice wled_devs[WLED_MAX];
static uint8_t wled_pending[WLED_MAX];   // requests on their way, per device
static int wled_n;
static WledJob *wled_jobs;               // PSRAM pool
static char *wled_buf;                   // PSRAM, used by the worker only
static bool wled_scan_running;
static WledFound wled_found_list[WLED_FOUND_MAX];
static int wled_found_n;

/* ================================ Storage ========================================= */

static void wled_save() {
  WledStored s[WLED_MAX];
  memset(s, 0, sizeof(s));
  for (int i = 0; i < wled_n; i++) {
    strlcpy(s[i].name, wled_devs[i].name, sizeof(s[i].name));
    strlcpy(s[i].host, wled_devs[i].host, sizeof(s[i].host));
  }
  Preferences p;
  if (!p.begin(WLED_NS, false)) return;
  if (wled_n) p.putBytes("devs", s, wled_n * sizeof(WledStored));
  else p.remove("devs");
  p.end();
}

static void wled_load() {
  WledStored s[WLED_MAX];
  Preferences p;
  if (!p.begin(WLED_NS, true)) return;
  int n = (int)(p.getBytes("devs", s, sizeof(s)) / sizeof(WledStored));
  p.end();
  wled_n = 0;
  for (int i = 0; i < n && i < WLED_MAX; i++) {
    WledDevice &d = wled_devs[wled_n++];
    memset(&d, 0, sizeof(d));
    strlcpy(d.name, s[i].name, sizeof(d.name));
    strlcpy(d.host, s[i].host, sizeof(d.host));
    d.bri = 128;
    d.color = 0xFFA000;
  }
}

static int wled_index_of(const char *host) {
  for (int i = 0; i < wled_n; i++) {
    if (!strcmp(wled_devs[i].host, host)) return i;
  }
  return -1;
}

/* ================================ Worker side ===================================== */

static void wled_parse_state(WledJob *job, JsonDocument &doc) {
  if (!doc["on"].is<bool>()) return;
  job->has_state = true;
  job->on = doc["on"];
  job->bri = doc["bri"] | 128;
  JsonObject seg = doc["seg"][0];
  JsonArray col = seg["col"][0];
  if (!col.isNull()) job->color = ((uint32_t)(col[0] | 0) << 16) | ((uint32_t)(col[1] | 0) << 8) | (uint32_t)(col[2] | 0);
  job->fx = seg["fx"] | 0;
}

static void wled_job_work(void *arg) {
  WledJob *job = (WledJob *)arg;
  job->ok = false;
  job->has_state = false;
  if (job->kind == WJ_SCAN) {
    job->found_n = 0;
    if (!MDNS.begin(WLED_MDNS_NAME)) return;
    int n = MDNS.queryService("wled", "tcp");
    for (int i = 0; i < n && job->found_n < WLED_FOUND_MAX; i++) {
      WledFound &f = job->found[job->found_n];
      String ip = MDNS.address(i).toString();
      if (ip == "0.0.0.0") continue;
      strlcpy(f.host, ip.c_str(), sizeof(f.host));
      String name = MDNS.instanceName(i);
      if (!name.length()) name = MDNS.hostname(i);
      strlcpy(f.name, name.length() ? name.c_str() : f.host, sizeof(f.name));
      job->found_n++;
    }
    MDNS.end();
    job->ok = true;
    return;
  }

  char url[80], err[40];
  snprintf(url, sizeof(url), "http://%s/json/%s", job->host, job->kind == WJ_INFO ? "info" : "state");
  int status = net_http(url, job->kind == WJ_SEND ? job->body : NULL, wled_buf, WLED_BUF_SIZE, WLED_TIMEOUT_MS, err, sizeof(err));
  if (status != 200) return;
  JsonDocument doc(psram_json_allocator());
  if (deserializeJson(doc, wled_buf)) return;
  job->ok = true;
  if (job->kind == WJ_INFO) strlcpy(job->name, doc["name"] | "", sizeof(job->name));
  else wled_parse_state(job, doc);
}

/* ================================ UI loop side ==================================== */

static void wled_job_done(void *arg) {
  WledJob *job = (WledJob *)arg;
  job->used = false;
  if (job->kind == WJ_SCAN) {
    wled_scan_running = false;
    wled_found_n = job->found_n;
    for (int i = 0; i < wled_found_n; i++) {
      wled_found_list[i] = job->found[i];
      wled_found_list[i].saved = wled_index_of(job->found[i].host) >= 0;
    }
    subj_bump(subj_lights);
    return;
  }
  int i = wled_index_of(job->host);
  if (i < 0) return;  // removed meanwhile
  WledDevice &d = wled_devs[i];
  if (wled_pending[i]) wled_pending[i]--;
  d.busy = wled_pending[i] > 0;
  d.online = job->ok;
  if (job->ok) d.known = true;
  if (job->kind == WJ_INFO && job->ok && job->name[0] && strcmp(job->name, d.name)) {
    strlcpy(d.name, job->name, sizeof(d.name));
    wled_save();
  }
  if (job->has_state && !d.busy) {  // a newer command is on its way: its answer wins
    d.on = job->on;
    d.bri = job->bri;
    d.color = job->color;
    d.fx = job->fx;
  }
  subj_bump(subj_lights);
}

static WledJob *wled_job_alloc(WledJobKind kind) {
  if (!wled_jobs) return NULL;
  for (int i = 0; i < WLED_JOBS; i++) {
    if (wled_jobs[i].used) continue;
    memset(&wled_jobs[i], 0, sizeof(WledJob));
    wled_jobs[i].used = true;
    wled_jobs[i].kind = kind;
    return &wled_jobs[i];
  }
  return NULL;
}

static bool wled_submit(int index, WledJobKind kind, const char *body) {
  if (index < 0 || index >= wled_n || lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED) return false;
  WledJob *job = wled_job_alloc(kind);
  if (!job) return false;
  strlcpy(job->host, wled_devs[index].host, sizeof(job->host));
  if (body) strlcpy(job->body, body, sizeof(job->body));
  if (!net_submit(wled_job_work, wled_job_done, job)) {
    job->used = false;
    return false;
  }
  wled_pending[index]++;
  wled_devs[index].busy = true;
  return true;
}

static void wled_wifi_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (lv_subject_get_int(subject) == WIFI_ST_CONNECTED && lv_subject_get_int(subj_ext_lights)) wled_refresh_all();
}

/* ================================ Public API ====================================== */

void wled_init() {
  wled_jobs = (WledJob *)psram_calloc(WLED_JOBS, sizeof(WledJob));
  wled_buf = (char *)psram_malloc(WLED_BUF_SIZE);
  wled_load();
  lv_subject_add_observer(subj_wifi_state, wled_wifi_obs, NULL);
}

int wled_count() {
  return wled_n;
}

const WledDevice *wled_get(int index) {
  return index >= 0 && index < wled_n ? &wled_devs[index] : NULL;
}

bool wled_add(const char *name, const char *host) {
  if (!host || !host[0] || wled_n >= WLED_MAX || wled_index_of(host) >= 0) return false;
  WledDevice &d = wled_devs[wled_n];
  memset(&d, 0, sizeof(d));
  strlcpy(d.name, name && name[0] ? name : host, sizeof(d.name));
  strlcpy(d.host, host, sizeof(d.host));
  d.bri = 128;
  d.color = 0xFFA000;
  wled_pending[wled_n] = 0;
  wled_n++;
  wled_save();
  for (int i = 0; i < wled_found_n; i++) {
    if (!strcmp(wled_found_list[i].host, host)) wled_found_list[i].saved = true;
  }
  wled_submit(wled_n - 1, WJ_INFO, NULL);   // the device's own name
  wled_submit(wled_n - 1, WJ_STATE, NULL);
  subj_bump(subj_lights);
  return true;
}

void wled_remove(int index) {
  if (index < 0 || index >= wled_n) return;
  for (int i = 0; i < wled_found_n; i++) {
    if (!strcmp(wled_found_list[i].host, wled_devs[index].host)) wled_found_list[i].saved = false;
  }
  for (int i = index; i < wled_n - 1; i++) {
    wled_devs[i] = wled_devs[i + 1];
    wled_pending[i] = wled_pending[i + 1];
  }
  wled_n--;
  wled_save();
  subj_bump(subj_lights);
}

void wled_refresh(int index) {
  wled_submit(index, WJ_STATE, NULL);
}

void wled_refresh_all() {
  for (int i = 0; i < wled_n; i++) wled_submit(i, WJ_STATE, NULL);
}

void wled_set_power(int index, bool on) {
  if (index < 0 || index >= wled_n) return;
  wled_devs[index].on = on;
  wled_submit(index, WJ_SEND, on ? "{\"on\":true,\"v\":true}" : "{\"on\":false,\"v\":true}");
  subj_bump(subj_lights);
}

bool wled_any_on() {
  for (int i = 0; i < wled_n; i++) {
    if (wled_devs[i].on && wled_devs[i].online) return true;
  }
  return false;
}

void wled_toggle_all() {
  bool on = !wled_any_on();
  for (int i = 0; i < wled_n; i++) wled_set_power(i, on);
}

void wled_set_brightness(int index, uint8_t bri) {
  if (index < 0 || index >= wled_n) return;
  if (bri < 1) bri = 1;
  wled_devs[index].bri = bri;
  wled_devs[index].on = true;
  char body[48];
  snprintf(body, sizeof(body), "{\"on\":true,\"bri\":%u,\"v\":true}", (unsigned)bri);
  wled_submit(index, WJ_SEND, body);
  subj_bump(subj_lights);
}

void wled_set_color(int index, uint32_t rgb) {
  if (index < 0 || index >= wled_n) return;
  wled_devs[index].color = rgb;
  wled_devs[index].on = true;
  char body[96];
  snprintf(body, sizeof(body), "{\"on\":true,\"seg\":[{\"col\":[[%u,%u,%u]]}],\"v\":true}", (unsigned)(rgb >> 16),
           (unsigned)((rgb >> 8) & 0xFF), (unsigned)(rgb & 0xFF));
  wled_submit(index, WJ_SEND, body);
  subj_bump(subj_lights);
}

void wled_set_effect(int index, uint8_t fx) {
  if (index < 0 || index >= wled_n) return;
  wled_devs[index].fx = fx;
  wled_devs[index].on = true;
  char body[64];
  snprintf(body, sizeof(body), "{\"on\":true,\"seg\":[{\"fx\":%u}],\"v\":true}", (unsigned)fx);
  wled_submit(index, WJ_SEND, body);
  subj_bump(subj_lights);
}

bool wled_scan() {
  if (wled_scan_running || lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED) return false;
  WledJob *job = wled_job_alloc(WJ_SCAN);
  if (!job) return false;
  if (!net_submit(wled_job_work, wled_job_done, job)) {
    job->used = false;
    return false;
  }
  wled_scan_running = true;
  wled_found_n = 0;
  subj_bump(subj_lights);
  return true;
}

bool wled_scanning() {
  return wled_scan_running;
}

int wled_found_count() {
  return wled_found_n;
}

const WledFound *wled_found(int index) {
  return index >= 0 && index < wled_found_n ? &wled_found_list[index] : NULL;
}
