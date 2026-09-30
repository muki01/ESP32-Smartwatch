/*
 * phone.cpp - Gadgetbridge link (Bangle.js protocol).
 *
 * Phone -> watch, one line each:
 *   \x10GB({"t":"notify","id":1234,"src":"WhatsApp","title":"Ana","body":"Hi!"})
 *   \x10setTime(1727600000);E.setTimeZone(3.0);...
 * Handled: notify, notify-, call, musicinfo, musicstate, find, weather, calendar,
 * calendar-, setTime.
 * Watch -> phone: one JSON object per line, e.g. {"t":"music","n":"next"}.
 * Calls and "find my watch" go to the UI's event handler (alerts with buttons).
 * Everything here runs in the UI loop.
 */
#include "phone.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../assets/fonts/icons.h"
#include "../core/settings.h"
#include "../core/system.h"
#include "../drivers/power.h"
#include "agenda.h"
#include "bluetooth.h"
#include "clock.h"
#include "notifications.h"
#include "weather.h"

static PhoneMusic phone_music_state;
static phone_event_cb_t phone_handler;

bool phone_connected() {
  return lv_subject_get_int(subj_gadgetbridge) && lv_subject_get_int(subj_bt_state) == BT_ST_CONNECTED;
}

const PhoneMusic *phone_music() {
  return &phone_music_state;
}

void phone_set_event_handler(phone_event_cb_t cb) {
  phone_handler = cb;
}

void phone_send(const char *json) {
  if (!phone_connected()) return;
  char line[160];
  int n = snprintf(line, sizeof(line), "%s\n", json);
  if (n > 0 && n < (int)sizeof(line)) bt_uart_send(line, n);
}

void phone_send_status() {
  int32_t level = lv_subject_get_int(subj_battery);
  char json[96];
  snprintf(json, sizeof(json), "{\"t\":\"status\",\"bat\":%d,\"chg\":%d,\"volt\":%.2f}", (int)(level < 0 ? 100 : level),
           (int)lv_subject_get_int(subj_charging), power_battery_voltage());
  phone_send(json);
}

void phone_on_connect() {
  phone_send("{\"t\":\"ver\",\"fw\":\"" FW_VERSION "\",\"hw\":\"" DEVICE_MODEL "\"}");
  phone_send_status();
}

void phone_find(bool ring) {
  phone_send(ring ? "{\"t\":\"findPhone\",\"n\":true}" : "{\"t\":\"findPhone\",\"n\":false}");
}

void phone_call_reply(bool accept) {
  phone_send(accept ? "{\"t\":\"call\",\"n\":\"ACCEPT\"}" : "{\"t\":\"call\",\"n\":\"REJECT\"}");
}

void phone_music_cmd(const char *cmd) {
  char json[64];
  snprintf(json, sizeof(json), "{\"t\":\"music\",\"n\":\"%s\"}", cmd);
  phone_send(json);
  // Show the new state right away; the phone confirms it with a musicstate message.
  if (!strcmp(cmd, "play") || !strcmp(cmd, "pause")) {
    phone_music_state.playing = !strcmp(cmd, "play");
    subj_bump(subj_phone);
  }
}

void phone_notify_dismiss(uint32_t ext_id) {
  char json[64];
  snprintf(json, sizeof(json), "{\"t\":\"notify\",\"id\":%u,\"n\":\"DISMISS\"}", (unsigned)ext_id);
  phone_send(json);
}

/* ================================ Incoming messages =============================== */

struct PhoneAppStyle {
  const char *match;  // lower-case part of the app name
  const char *icon;
  uint32_t color;
};

static const PhoneAppStyle PHONE_APPS[] = {
  { "whatsapp",  ICON_COMMENT,  0x25D366 },
  { "telegram",  ICON_COMMENT,  0x2AABEE },
  { "signal",    ICON_COMMENT,  0x3A76F0 },
  { "messenger", ICON_COMMENT,  0x0084FF },
  { "discord",   ICON_COMMENT,  0x5865F2 },
  { "instagram", ICON_IMAGE,    0xE1306C },
  { "gmail",     ICON_ENVELOPE, 0xEA4335 },
  { "mail",      ICON_ENVELOPE, 0x0A84FF },
  { "outlook",   ICON_ENVELOPE, 0x0078D4 },
  { "message",   ICON_COMMENT,  0x30D158 },
  { "sms",       ICON_COMMENT,  0x30D158 },
  { "phone",     ICON_PHONE,    0x30D158 },
  { "dialer",    ICON_PHONE,    0x30D158 },
  { "calendar",  ICON_CALENDAR, 0xFF453A },
  { "youtube",   ICON_PLAY,     0xFF0000 },
  { "spotify",   ICON_MUSIC,    0x1DB954 },
};

static void phone_app_style(const char *app, const char **icon, uint32_t *color) {
  char lower[32];
  size_t i = 0;
  for (; app[i] && i < sizeof(lower) - 1; i++) lower[i] = (char)tolower((unsigned char)app[i]);
  lower[i] = 0;
  for (const PhoneAppStyle &s : PHONE_APPS) {
    if (strstr(lower, s.match)) {
      *icon = s.icon;
      *color = s.color;
      return;
    }
  }
  *icon = ICON_BELL;
  *color = 0x0A84FF;
}

static void phone_emit(PhoneEvent event, const PhoneCall *call) {
  if (phone_handler) phone_handler(event, call);
}

static void phone_handle_call(JsonDocument &doc) {
  const char *cmd = doc["cmd"] | "";
  if (strcmp(cmd, "incoming") != 0) {  // accept, reject, end, outgoing, ...
    phone_emit(PHONE_CALL_ENDED, NULL);
    return;
  }
  PhoneCall call;
  strlcpy(call.name, doc["name"] | "", sizeof(call.name));
  strlcpy(call.number, doc["number"] | "", sizeof(call.number));
  phone_emit(PHONE_CALL_INCOMING, &call);
}

// Open Weather Map condition code -> WMO code (what the rest of the watch uses).
static int16_t phone_owm_to_wmo(int owm) {
  if (owm >= 200 && owm < 300) return 95;
  if (owm >= 300 && owm < 400) return 53;
  if (owm == 511) return 66;
  if (owm >= 520 && owm < 600) return 81;
  if (owm >= 500 && owm < 600) return owm >= 502 ? 65 : owm == 501 ? 63 : 61;
  if (owm >= 600 && owm < 700) return owm == 602 ? 75 : owm >= 620 ? 85 : owm == 601 ? 73 : 71;
  if (owm >= 700 && owm < 800) return 45;
  if (owm == 800) return 0;
  if (owm == 801) return 1;
  if (owm == 802) return 2;
  return 3;
}

static void phone_handle_weather(JsonDocument &doc) {
  WeatherInfo w;
  memset(&w, 0, sizeof(w));
  float kelvin = doc["temp"] | 0.0f;
  if (kelvin < 150) return;  // not a temperature in Kelvin: ignore
  w.temp = kelvin - 273.15f;
  w.feels = w.temp;
  w.humidity = doc["hum"] | 0;
  w.wind = doc["wind"] | 0.0f;
  w.code = phone_owm_to_wmo(doc["code"] | 803);
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  w.is_day = t.tm_hour >= 6 && t.tm_hour < 20;
  strlcpy(w.city, doc["loc"] | "", sizeof(w.city));
  for (int i = 0; i < WEATHER_DAYS; i++) w.days[i].code = -1;
  w.days[0].code = w.code;
  w.days[0].t_max = (doc["hi"] | kelvin) - 273.15f;
  w.days[0].t_min = (doc["lo"] | kelvin) - 273.15f;
  w.days[0].precip = doc["rain"] | 0;
  w.updated = now;
  w.valid = true;
  weather_apply_phone(&w);
}

static void phone_handle_json(const char *json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return;
  const char *type = doc["t"] | "";

  if (!strcmp(type, "notify")) {
    const char *app = doc["src"] | "Phone";
    const char *title = doc["title"] | "";
    if (!title[0]) title = doc["sender"] | "";
    if (!title[0]) title = doc["subject"] | "";
    const char *body = doc["body"] | "";
    if (!body[0]) body = doc["subject"] | "";
    const char *icon;
    uint32_t color;
    phone_app_style(app, &icon, &color);
    notify_post_ext(doc["id"] | 0u, app, title, body, icon, color);
  } else if (!strcmp(type, "notify-")) {
    notify_remove_ext(doc["id"] | 0u);
  } else if (!strcmp(type, "call")) {
    phone_handle_call(doc);
  } else if (!strcmp(type, "musicinfo")) {
    strlcpy(phone_music_state.artist, doc["artist"] | "", sizeof(phone_music_state.artist));
    strlcpy(phone_music_state.track, doc["track"] | "", sizeof(phone_music_state.track));
    phone_music_state.duration = doc["dur"] | 0;
    phone_music_state.valid = true;
    subj_bump(subj_phone);
  } else if (!strcmp(type, "musicstate")) {
    const char *state = doc["state"] | "";
    phone_music_state.playing = !strcmp(state, "play");
    phone_music_state.position = doc["position"] | 0;
    phone_music_state.position_ms = millis();
    phone_music_state.valid = true;
    subj_bump(subj_phone);
  } else if (!strcmp(type, "find")) {
    phone_emit((doc["n"] | false) ? PHONE_FIND_START : PHONE_FIND_STOP, NULL);
  } else if (!strcmp(type, "weather")) {
    phone_handle_weather(doc);
  } else if (!strcmp(type, "calendar")) {
    AgendaEvent e;
    memset(&e, 0, sizeof(e));
    e.id = doc["id"] | 0u;
    e.start = doc["timestamp"] | 0u;
    e.duration = doc["durationInSeconds"] | 0u;
    e.all_day = (doc["allDay"] | false) ? 1 : 0;
    strlcpy(e.title, doc["title"] | "", sizeof(e.title));
    strlcpy(e.location, doc["location"] | "", sizeof(e.location));
    if (e.start) agenda_put(&e);
  } else if (!strcmp(type, "calendar-")) {
    agenda_remove(doc["id"] | 0u);
  }
}

void phone_rx_line(const char *line) {
  while (*line && (unsigned char)*line < 0x20) line++;  // \x10 (echo off), \x03 prefixes
  if (!lv_subject_get_int(subj_gadgetbridge)) return;

  const char *st = strstr(line, "setTime(");
  if (st) {
    long long epoch = atoll(st + 8);
    if (epoch > 0) clock_set_utc((time_t)epoch);
  }
  const char *tz = strstr(line, "E.setTimeZone(");
  if (tz) {
    float hours = atof(tz + 14);
    clock_apply_utc_offset((int)lroundf(hours * 60.0f));
  }
  const char *gb = strstr(line, "GB(");
  if (gb) {
    const char *start = gb + 3;
    const char *end = strrchr(start, ')');
    if (end && end > start) phone_handle_json(start, end - start);
  }
}

static void phone_battery_obs(lv_observer_t *observer, lv_subject_t *subject) {
  phone_send_status();
}

static void phone_link_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (!phone_connected() && phone_music_state.valid) {
    phone_music_state.valid = false;  // the phone's player is gone with the link
    subj_bump(subj_phone);
  }
}

void phone_init() {
  lv_subject_add_observer(subj_battery, phone_battery_obs, NULL);
  lv_subject_add_observer(subj_charging, phone_battery_obs, NULL);
  lv_subject_add_observer(subj_bt_state, phone_link_obs, NULL);
}
