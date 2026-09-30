/*
 * weather.cpp - Weather service.
 *
 *   forecast  https://api.open-meteo.com          (free, no API key, TLS with the core's CA bundle)
 *   location  a city chosen by the user (Open-Meteo geocoding search), or automatic from
 *             the public IP address: http://ip-api.com, falling back to https://get.geojs.io
 *
 * Downloads run on the network worker while Wi-Fi is connected: every 30 minutes, after 5
 * minutes when the last attempt failed, or on request. The result is stored in NVS (the
 * faces show the last weather right after a restart) and announced with subj_weather.
 * Without Wi-Fi, Gadgetbridge can deliver the phone's weather instead.
 */
#include "weather.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "../assets/fonts/icons.h"
#include "../core/psram_json.h"
#include "../core/settings.h"
#include "../core/system.h"
#include "net_worker.h"

#define WEATHER_POLL_MS       10000
#define WEATHER_REFRESH_MS    (30UL * 60 * 1000)
#define WEATHER_RETRY_MS      (5UL * 60 * 1000)
#define WEATHER_LOCATION_MS   (6UL * 3600 * 1000)
#define WEATHER_BUF_SIZE      8192
#define WEATHER_TIMEOUT_MS    12000
#define WEATHER_NS            "weather"

enum WeatherJobKind : uint8_t { WJOB_FORECAST, WJOB_SEARCH };

// One download at a time; filled in the UI loop, worked on by the network worker.
struct WeatherJob {
  WeatherJobKind kind;
  bool ok;
  char err[48];
  bool locate;                   // look the location up from the IP address first
  float lat, lon;
  char city[40];
  bool located;
  WeatherInfo info;
  char query[48];
  WeatherPlace found[WEATHER_SEARCH_MAX];
  int found_n;
};

static WeatherJob *weather_job;           // PSRAM
static char *weather_buf;                 // PSRAM, response bodies
static bool weather_job_running;
static WeatherInfo weather_data;
static char weather_err[48];
static int32_t weather_st = WEATHER_IDLE;
static uint32_t weather_ok_ms;            // last successful download (0 = never this boot)
static uint32_t weather_try_ms;
static bool weather_failed;
static bool weather_force;
static lv_timer_t *weather_timer;

// Chosen city (persisted); automatic location when not set.
static bool weather_manual;
static WeatherPlace weather_place;
static float weather_auto_lat, weather_auto_lon;
static char weather_auto_city[40];
static uint32_t weather_loc_ms;           // automatic location: when it was looked up
static bool weather_loc_ok;

// City search.
static char weather_query[48];
static bool weather_search_pending;
static WeatherPlace weather_found[WEATHER_SEARCH_MAX];
static int weather_found_n;
static int32_t weather_search_st = WSEARCH_IDLE;

/* ================================ Worker side ===================================== */


static bool weather_get(const char *url, WeatherJob *job) {
  weather_buf[0] = 0;
  int status = net_http(url, NULL, weather_buf, WEATHER_BUF_SIZE, WEATHER_TIMEOUT_MS, job->err, sizeof(job->err));
  if (status == 200 && weather_buf[0]) return true;
  if (status > 0 && weather_buf[0] == '{') {  // Open-Meteo explains a refused request: {"error":true,"reason":"..."}
    JsonDocument doc(psram_json_allocator());
    if (!deserializeJson(doc, weather_buf) && doc["reason"].is<const char *>()) {
      strlcpy(job->err, doc["reason"] | "", sizeof(job->err));
    }
  }
  Serial.printf("[WEATHER] %s: %s\n", url, job->err);
  return false;
}

static bool weather_locate(WeatherJob *job) {
  // 1. ip-api.com (plain HTTP)
  if (weather_get("http://ip-api.com/json/?fields=status,city,lat,lon", job)) {
    JsonDocument doc(psram_json_allocator());
    if (!deserializeJson(doc, weather_buf) && !strcmp(doc["status"] | "", "success")) {
      job->lat = doc["lat"] | 0.0f;
      job->lon = doc["lon"] | 0.0f;
      strlcpy(job->city, doc["city"] | "", sizeof(job->city));
      return true;
    }
  }
  // 2. geojs.io (HTTPS); latitude and longitude come as strings
  if (weather_get("https://get.geojs.io/v1/ip/geo.json", job)) {
    JsonDocument doc(psram_json_allocator());
    if (!deserializeJson(doc, weather_buf) && doc["latitude"].is<const char *>()) {
      job->lat = atof(doc["latitude"] | "0");
      job->lon = atof(doc["longitude"] | "0");
      strlcpy(job->city, doc["city"] | "", sizeof(job->city));
      return true;
    }
  }
  strlcpy(job->err, "Location unknown", sizeof(job->err));
  return false;
}

// "2026-09-30T06:48" -> minutes after midnight, -1 when it does not look like that.
static int16_t weather_parse_hm(const char *iso) {
  if (strlen(iso) < 16 || iso[10] != 'T' || iso[13] != ':') return -1;
  return (int16_t)(atoi(iso + 11) * 60 + atoi(iso + 14));
}

static bool weather_fetch_forecast(WeatherJob *job) {
  char url[640];
  int n = snprintf(url, sizeof(url),
                   "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
                   "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
                   "&hourly=temperature_2m,weather_code,precipitation_probability,is_day&forecast_hours=%d"
                   "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,"
                   "sunset,uv_index_max&timezone=auto&forecast_days=%d",
                   job->lat, job->lon, WEATHER_HOURS, WEATHER_DAYS);
  if (n <= 0 || n >= (int)sizeof(url)) {
    strlcpy(job->err, "Request too long", sizeof(job->err));
    return false;
  }
  if (!weather_get(url, job)) return false;

  JsonDocument doc(psram_json_allocator());
  if (deserializeJson(doc, weather_buf)) {
    strlcpy(job->err, "Unexpected answer", sizeof(job->err));
    return false;
  }
  JsonObject cur = doc["current"];
  if (cur.isNull()) {
    strlcpy(job->err, doc["reason"] | "No weather data", sizeof(job->err));
    return false;
  }
  WeatherInfo *w = &job->info;
  memset(w, 0, sizeof(*w));
  w->temp = cur["temperature_2m"] | 0.0f;
  w->feels = cur["apparent_temperature"] | w->temp;
  w->humidity = cur["relative_humidity_2m"] | 0;
  w->code = cur["weather_code"] | 0;
  w->wind = cur["wind_speed_10m"] | 0.0f;
  w->is_day = (cur["is_day"] | 1) != 0;
  JsonObject daily = doc["daily"];
  for (int i = 0; i < WEATHER_DAYS; i++) {
    WeatherDay &d = w->days[i];
    d.code = daily["weather_code"][i] | -1;
    d.t_max = daily["temperature_2m_max"][i] | 0.0f;
    d.t_min = daily["temperature_2m_min"][i] | 0.0f;
    d.precip = daily["precipitation_probability_max"][i] | 0;
  }
  w->sunrise = weather_parse_hm(daily["sunrise"][0] | "");
  w->sunset = weather_parse_hm(daily["sunset"][0] | "");
  w->uv = daily["uv_index_max"][0] | -1.0f;
  JsonObject hourly = doc["hourly"];
  for (int i = 0; i < WEATHER_HOURS; i++) {
    WeatherHour &h = w->hours[i];
    int16_t minute = weather_parse_hm(hourly["time"][i] | "");
    h.hour = minute < 0 ? -1 : (int8_t)(minute / 60);
    h.temp = (int8_t)constrain(lroundf(hourly["temperature_2m"][i] | 0.0f), -99L, 99L);
    h.code = hourly["weather_code"][i] | -1;
    h.precip = hourly["precipitation_probability"][i] | 0;
    h.is_day = (hourly["is_day"][i] | 1) != 0;
  }
  strlcpy(w->city, job->city, sizeof(w->city));
  w->updated = time(NULL);
  w->valid = true;
  return true;
}

static void weather_url_encode(const char *in, char *out, size_t len) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < len; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') {
      out[o++] = *p;
    } else {
      out[o++] = '%';
      out[o++] = HEX_DIGITS[*p >> 4];
      out[o++] = HEX_DIGITS[*p & 15];
    }
  }
  out[o] = 0;
}

static bool weather_search_places(WeatherJob *job) {
  char name[3 * sizeof(job->query)], url[300];
  weather_url_encode(job->query, name, sizeof(name));
  snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=en&format=json",
           name, WEATHER_SEARCH_MAX);
  job->found_n = 0;
  if (!weather_get(url, job)) return false;
  JsonDocument doc(psram_json_allocator());
  if (deserializeJson(doc, weather_buf)) {
    strlcpy(job->err, "Unexpected answer", sizeof(job->err));
    return false;
  }
  for (JsonObject r : doc["results"].as<JsonArray>()) {
    if (job->found_n >= WEATHER_SEARCH_MAX) break;
    WeatherPlace &p = job->found[job->found_n++];
    strlcpy(p.name, r["name"] | "", sizeof(p.name));
    const char *admin = r["admin1"] | "";
    const char *country = r["country"] | "";
    if (admin[0] && strcmp(admin, p.name)) snprintf(p.region, sizeof(p.region), "%s, %s", admin, country);
    else strlcpy(p.region, country, sizeof(p.region));
    p.lat = r["latitude"] | 0.0f;
    p.lon = r["longitude"] | 0.0f;
  }
  return true;
}

static void weather_job_work(void *arg) {
  WeatherJob *job = (WeatherJob *)arg;
  job->err[0] = 0;
  job->located = false;
  if (job->kind == WJOB_SEARCH) {
    job->ok = weather_search_places(job);
    return;
  }
  if (job->locate) {
    job->located = weather_locate(job);
    if (!job->located && !(job->lat || job->lon)) {  // nothing to fall back on
      job->ok = false;
      return;
    }
  }
  job->ok = weather_fetch_forecast(job);
}

/* ================================ UI loop side ==================================== */

static void weather_set_state(int32_t st) {
  weather_st = st;
  subj_bump(subj_weather);
}

static void weather_save() {
  Preferences p;
  if (!p.begin(WEATHER_NS, false)) return;
  p.putBytes("data", &weather_data, sizeof(weather_data));
  p.end();
}

static void weather_save_place() {
  Preferences p;
  if (!p.begin(WEATHER_NS, false)) return;
  p.putBool("manual", weather_manual);
  p.putBytes("place", &weather_place, sizeof(weather_place));
  p.end();
}

static void weather_load() {
  Preferences p;
  if (!p.begin(WEATHER_NS, true)) return;
  WeatherInfo w;
  if (p.getBytes("data", &w, sizeof(w)) == sizeof(w) && w.valid) weather_data = w;
  weather_manual = p.getBool("manual", false) && p.getBytes("place", &weather_place, sizeof(weather_place)) == sizeof(weather_place);
  p.end();
}

static void weather_job_done(void *arg) {
  WeatherJob *job = (WeatherJob *)arg;
  weather_job_running = false;
  if (job->kind == WJOB_SEARCH) {
    memcpy(weather_found, job->found, sizeof(weather_found));
    weather_found_n = job->ok ? job->found_n : 0;
    if (!job->ok) strlcpy(weather_err, job->err, sizeof(weather_err));
    weather_search_st = job->ok ? WSEARCH_DONE : WSEARCH_FAILED;
    subj_bump(subj_weather);
    return;
  }
  if (job->located) {
    weather_auto_lat = job->lat;
    weather_auto_lon = job->lon;
    strlcpy(weather_auto_city, job->city, sizeof(weather_auto_city));
    weather_loc_ok = true;
    weather_loc_ms = millis();
  }
  weather_failed = !job->ok;
  if (!job->ok) {
    strlcpy(weather_err, job->err[0] ? job->err : "Download failed", sizeof(weather_err));
    Serial.printf("[WEATHER] failed: %s\n", weather_err);
    weather_set_state(WEATHER_ERROR);
    return;
  }
  weather_data = job->info;
  weather_ok_ms = millis();
  weather_err[0] = 0;
  weather_save();
  Serial.printf("[WEATHER] %s: %.1f C, code %d\n", weather_data.city, weather_data.temp, weather_data.code);
  weather_set_state(WEATHER_OK);
}

static bool weather_start_job(WeatherJobKind kind) {
  if (weather_job_running || !weather_job || !weather_buf) return false;
  WeatherJob *job = weather_job;
  memset(job, 0, sizeof(*job));
  job->kind = kind;
  if (kind == WJOB_SEARCH) {
    strlcpy(job->query, weather_query, sizeof(job->query));
  } else if (weather_manual) {
    job->lat = weather_place.lat;
    job->lon = weather_place.lon;
    strlcpy(job->city, weather_place.name, sizeof(job->city));
  } else {
    job->locate = !weather_loc_ok || millis() - weather_loc_ms > WEATHER_LOCATION_MS;
    job->lat = weather_auto_lat;  // kept when a fresh lookup fails
    job->lon = weather_auto_lon;
    strlcpy(job->city, weather_auto_city, sizeof(job->city));
  }
  if (!net_submit(weather_job_work, weather_job_done, job)) return false;
  weather_job_running = true;
  return true;
}

static void weather_poll_cb(lv_timer_t *t) {
  if (weather_job_running) return;
  if (lv_subject_get_int(subj_wifi_state) != WIFI_ST_CONNECTED) {
    if (weather_search_pending) {
      weather_search_pending = false;
      strlcpy(weather_err, "Wi-Fi is not connected", sizeof(weather_err));
      weather_search_st = WSEARCH_FAILED;
      subj_bump(subj_weather);
    }
    return;
  }
  if (weather_search_pending) {  // a search the user waits for goes first
    weather_search_pending = false;
    if (!weather_start_job(WJOB_SEARCH)) {
      weather_search_st = WSEARCH_FAILED;
      subj_bump(subj_weather);
    }
    return;
  }

  uint32_t now = millis();
  bool due = weather_force || !weather_ok_ms || now - weather_ok_ms > WEATHER_REFRESH_MS;
  if (!due || (weather_failed && !weather_force && now - weather_try_ms < WEATHER_RETRY_MS)) return;
  if (weather_try_ms && !weather_force && now - weather_try_ms < 20000) return;

  weather_force = false;
  weather_try_ms = now;
  if (weather_start_job(WJOB_FORECAST)) weather_set_state(WEATHER_LOADING);
}

static void weather_wifi_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (lv_subject_get_int(subject) == WIFI_ST_CONNECTED) {
    lv_timer_t *t = lv_timer_create(weather_poll_cb, 3000, NULL);  // soon after connecting
    lv_timer_set_repeat_count(t, 1);
  }
}

/* ================================ Public API ====================================== */

void weather_init() {
  weather_job = (WeatherJob *)psram_calloc(1, sizeof(WeatherJob));
  weather_buf = (char *)psram_malloc(WEATHER_BUF_SIZE);
  weather_load();
  weather_timer = lv_timer_create(weather_poll_cb, WEATHER_POLL_MS, NULL);
  lv_subject_add_observer(subj_wifi_state, weather_wifi_obs, NULL);
  subj_bump(subj_weather);
}

void weather_refresh() {
  weather_force = true;
  weather_poll_cb(NULL);
}

const WeatherInfo *weather_info() {
  return &weather_data;
}

int weather_state() {
  return weather_st;
}

const char *weather_error() {
  return weather_err;
}

// Weather sent by the phone (Gadgetbridge): used when our own download is not fresh.
void weather_apply_phone(const WeatherInfo *info) {
  if (!info || !info->valid) return;
  if (weather_ok_ms && millis() - weather_ok_ms < WEATHER_REFRESH_MS) return;  // own data is fresh
  WeatherInfo w = *info;
  // The phone sends no forecast, hours, sun times or UV: keep what we already know.
  for (int i = 0; i < WEATHER_DAYS; i++) {
    if (w.days[i].code < 0 && weather_data.valid) w.days[i] = weather_data.days[i];
  }
  for (int i = 0; i < WEATHER_HOURS; i++) w.hours[i] = weather_data.valid ? weather_data.hours[i] : WeatherHour{ -1, 0, -1, 0, 1 };
  w.sunrise = weather_data.valid ? weather_data.sunrise : -1;
  w.sunset = weather_data.valid ? weather_data.sunset : -1;
  w.uv = weather_data.valid ? weather_data.uv : -1.0f;
  if (!w.city[0]) strlcpy(w.city, weather_data.city, sizeof(w.city));
  weather_data = w;
  weather_save();
  weather_set_state(WEATHER_OK);
}

void weather_set_place(const WeatherPlace *place) {
  weather_manual = place != NULL;
  if (place) weather_place = *place;
  weather_loc_ok = false;  // automatic: look the location up again
  weather_save_place();
  weather_ok_ms = 0;
  weather_data.valid = false;  // the old place's weather no longer applies
  weather_set_state(WEATHER_IDLE);
  weather_refresh();
}

bool weather_place_is_auto() {
  return !weather_manual;
}

const char *weather_place_name() {
  if (weather_manual) return weather_place.name;
  return weather_data.valid && weather_data.city[0] ? weather_data.city : "Automatic";
}

bool weather_search(const char *query) {
  if (!query || !query[0]) return false;
  strlcpy(weather_query, query, sizeof(weather_query));
  weather_found_n = 0;
  weather_search_st = WSEARCH_BUSY;
  weather_search_pending = true;
  subj_bump(subj_weather);
  weather_poll_cb(NULL);
  return true;
}

int weather_search_state() {
  return weather_search_st;
}

int weather_search_count() {
  return weather_found_n;
}

const WeatherPlace *weather_search_result(int index) {
  return index >= 0 && index < weather_found_n ? &weather_found[index] : NULL;
}

/* ================================ Presentation ==================================== */

const char *weather_text(int code) {
  switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mostly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Rain showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return code < 0 ? "No data" : "Unknown";
  }
}

const char *weather_icon(int code, bool day) {
  if (code <= 1) return day ? ICON_SUN : ICON_MOON;
  if (code == 2) return day ? ICON_CLOUD_SUN : ICON_CLOUD_MOON;
  if (code == 3) return ICON_CLOUD;
  if (code == 45 || code == 48) return ICON_SMOG;
  if (code >= 51 && code <= 67) return code >= 65 ? ICON_CLOUD_SHOWERS : ICON_CLOUD_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return ICON_SNOWFLAKE;
  if (code >= 80 && code <= 82) return ICON_CLOUD_SHOWERS;
  if (code >= 95) return ICON_BOLT;
  return ICON_CLOUD;
}

uint32_t weather_color(int code, bool day) {
  if (code <= 2) return day ? 0xFFD60A : 0xC7C7CC;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 0xE5E5EA;
  if (code >= 51 && code <= 82) return 0x64D2FF;
  if (code >= 95) return 0xFFD60A;
  return 0xAEAEB2;
}

int weather_temp(float celsius) {
  float t = lv_subject_get_int(subj_temp_unit) ? celsius * 9.0f / 5.0f + 32.0f : celsius;
  return (int)lroundf(t);
}
