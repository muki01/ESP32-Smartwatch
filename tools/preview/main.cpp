/*
 * main.cpp - The PC preview's scenes: boots the UI like app_init() (without hardware),
 * fills it with sample data and renders every screen, plus recorded sequences for the
 * README animations.
 *
 *   preview [fsaxcr]   f faces, s system, a apps, x extras, c settings, r recordings
 *
 * Stills go to frames/<name>.rgb565, recordings to frames/<name>.raw (25 fps) with the
 * touches in frames/<name>.touch. The clock is fixed at Tue 29 Sep 2026, 14:32.
 */
#include "Arduino.h"
#include "core/settings.h"
#include "drivers/power.h"
#include "pv.h"
#include "services/activity.h"
#include "services/agenda.h"
#include "services/alarms.h"
#include "services/battery_saver.h"
#include "services/car_link.h"
#include "services/clap_control.h"
#include "services/countdown.h"
#include "services/notifications.h"
#include "services/phone.h"
#include "services/sleep_tracker.h"
#include "services/weather.h"
#include "services/wled.h"
#include "services/workout.h"
#include "ui/apps/apps.h"
#include "ui/faces/faces.h"
#include "ui/kit/kit.h"
#include "ui/system/aod.h"
#include "ui/system/nav.h"
#include "ui/system/notification_center.h"
#include "ui/system/quick_panel.h"
#include "ui/system/screen.h"
#include "ui/system/system_ui.h"

#define W 368
#define H 448
#define REC_FRAME_MS 40

static uint8_t fb[W * H * 2] __attribute__((aligned(4)));
static int pv_failures;
static FILE *rec_raw, *rec_touch;
static int rec_frame;
static uint32_t rec_next_ms;

static void flush_cb(lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); }
static uint32_t tick_cb() { return pv_ms; }

static void settle(int ms) {
  for (int t = 0; t < ms; t += 10) {
    pv_ms += 10;
    lv_display_trigger_activity(lv_display_get_default());
    lv_timer_handler();
    if (rec_raw && pv_ms >= rec_next_ms) {
      fwrite(fb, 1, sizeof(fb), rec_raw);
      rec_frame++;
      rec_next_ms += REC_FRAME_MS;
    }
  }
}

static void rec_start(const char *name) {
  char path[128];
  snprintf(path, sizeof(path), "frames/%s.raw", name);
  rec_raw = fopen(path, "wb");
  snprintf(path, sizeof(path), "frames/%s.touch", name);
  rec_touch = fopen(path, "w");
  rec_frame = 0;
  rec_next_ms = pv_ms;
  printf("recording %s\n", name);
}

static void rec_stop() {
  fclose(rec_raw);
  fclose(rec_touch);
  rec_raw = rec_touch = NULL;
  printf("  %d frames\n", rec_frame);
}

static void render(const char *name) {
  settle(700);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(NULL);
  char path[128];
  snprintf(path, sizeof(path), "frames/%s.rgb565", name);
  FILE *f = fopen(path, "wb");
  fwrite(fb, 1, sizeof(fb), f);
  fclose(f);
  printf("rendered %s\n", name);
}

static void home() {
  kit_alert_close();
  kit_modal_close();
  nav_go_home(false);
  settle(400);
}

static lv_obj_t *page_content() {
  kit_transition_finish();
  lv_obj_t *page = lv_screen_active();
  lv_obj_update_layout(page);
  return kit_page_content(page);
}

static void scroll_to(int32_t y) {
  lv_obj_scroll_to_y(page_content(), y, LV_ANIM_OFF);
}

static void scroll_bottom() {
  scroll_to(LV_COORD_MAX);
}

// A finger movement for the recording (drawn later), then the action it stands for.
static void swipe(int x0, int y0, int x1, int y1) {
  if (rec_touch) fprintf(rec_touch, "swipe %d %d %d %d %d\n", rec_frame, x0, y0, x1, y1);
  settle(200);
}

static void scroll_anim(int32_t y) {
  lv_obj_t *content = page_content();
  int32_t from = lv_obj_get_scroll_y(content);
  int32_t target = y == LV_COORD_MAX ? from + lv_obj_get_scroll_bottom(content) : y;
  bool up = target > from;  // content moves up: the finger moves up
  swipe(W / 2, up ? 360 : 120, W / 2, up ? 140 : 340);
  lv_obj_scroll_to_y(content, target, LV_ANIM_ON);
  settle(700);
}

static lv_obj_t *find_text(lv_obj_t *obj, const char *text) {
  if (lv_obj_is_hidden(obj)) return NULL;
  if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), text)) return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
    lv_obj_t *found = find_text(lv_obj_get_child(obj, i), text);
    if (found) return found;
  }
  return NULL;
}

static void dump_labels(lv_obj_t *obj, int n) {
  if (lv_obj_check_type(obj, &lv_label_class)) printf(" [%s]%s", lv_label_get_text(obj), lv_obj_is_hidden(obj) ? "h" : "");
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj) && n < 40; i++) dump_labels(lv_obj_get_child(obj, i), n + 1);
}

// Taps the nearest clickable ancestor of the first visible label with this text.
static bool click(const char *text) {
  settle(300);  // like a finger: pages ignore taps while they slide in
  kit_transition_finish();
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t *label = find_text(lv_layer_top(), text);
  if (!label) label = find_text(lv_screen_active(), text);
  lv_obj_t *target = label;
  while (target && !lv_obj_is_clickable(target)) target = lv_obj_get_parent(target);
  if (!target) {
    printf("!! nothing to click: \"%s\" (depth %d, labels:", text, kit_page_depth());
    dump_labels(lv_screen_active(), 0);
    printf(")\n");
    pv_failures++;
    return false;
  }
  lv_area_t a;
  lv_obj_get_coords(label, &a);
  if (rec_touch) fprintf(rec_touch, "tap %d %d %d\n", rec_frame, (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
  lv_obj_add_state(target, LV_STATE_PRESSED);
  settle(120);
  lv_obj_remove_state(target, LV_STATE_PRESSED);
  lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
  settle(400);
  return true;
}

static void back() {
  swipe(40, H / 2, 300, H / 2);
  kit_page_pop();
  settle(400);
}

/* ================================ Seed data ======================================= */

static void seed_storage() {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  pv_prefs_seed_int("activity", "day", (t.tm_year + 1900) * 1000 + t.tm_yday);
  pv_prefs_seed_int("activity", "steps", 0);
  const uint32_t hist[ACTIVITY_DAYS] = { 9120, 4310, 8450, 11020, 7600, 5230, 8900 };
  pv_prefs_seed_bytes("activity", "hist", hist, sizeof(hist));
  const uint16_t active_min = 38;
  pv_prefs_seed_bytes("activity", "amin", &active_min, sizeof(active_min));

  SleepNight nights[SLEEP_NIGHTS];
  static const int16_t MINS[] = { 475, 395, 452, 410, 468, 382, 440 };
  uint32_t last_end = (uint32_t)now - (7 * 3600 + 27 * 60);
  for (int i = 0; i < SLEEP_NIGHTS; i++) {
    SleepNight &n = nights[i];
    memset(&n, 0, sizeof(n));
    n.end = last_end - i * 86400;
    n.start = n.end - MINS[i] * 60;
    n.calm_min = MINS[i] * 7 / 10;
    n.restless_min = MINS[i] - n.calm_min - 12;
    n.awake_min = 12;
  }
  pv_prefs_seed_bytes("sleep", "nights", nights, sizeof(nights));
  uint8_t bins[96];
  for (int i = 0; i < 96; i++) bins[i] = i == 48 || i == 49 ? SLEEP_AWAKE_C : (i % 9 < 2 ? SLEEP_RESTLESS_C : SLEEP_CALM_C);
  pv_prefs_seed_bytes("sleep", "bins", bins, sizeof(bins));
  pv_prefs_seed_int("sleep", "done", -1);

  WorkoutRecord wo[3];
  memset(wo, 0, sizeof(wo));
  wo[0] = { (uint32_t)now - 26 * 3600, 42 * 60, 6120, 4590, 310, WO_RUN, 0 };
  wo[1] = { (uint32_t)now - 3 * 86400, 65 * 60, 7400, 5550, 280, WO_WALK, 0 };
  wo[2] = { (uint32_t)now - 6 * 86400, 124 * 60, 11800, 8850, 610, WO_HIKE, 0 };
  pv_prefs_seed_bytes("workouts", "list", wo, sizeof(wo));
}

static void seed_weather() {
  static WeatherInfo w;
  memset(&w, 0, sizeof(w));
  w.valid = true;
  w.updated = time(NULL) - 12 * 60;
  strlcpy(w.city, "Amsterdam", sizeof(w.city));
  w.temp = 24.3f;
  w.feels = 25.1f;
  w.wind = 12.4f;
  w.humidity = 48;
  w.code = 2;
  w.is_day = true;
  const int16_t codes[] = { 2, 61, 3, 0, 95 };
  const float hi[] = { 27, 21, 19, 23, 18 }, lo[] = { 16, 14, 12, 13, 11 };
  const uint8_t rain[] = { 10, 80, 30, 0, 70 };
  for (int i = 0; i < WEATHER_DAYS; i++) w.days[i] = { codes[i], hi[i], lo[i], rain[i] };
  const int16_t hcodes[] = { 2, 2, 3, 3, 61, 61, 80, 3, 2, 1, 0, 0 };
  const int8_t htemps[] = { 24, 25, 25, 24, 21, 19, 18, 17, 16, 15, 15, 14 };
  const uint8_t hrain[] = { 0, 5, 10, 30, 70, 80, 60, 20, 10, 0, 0, 0 };
  for (int i = 0; i < WEATHER_HOURS; i++)
    w.hours[i] = { (int8_t)((14 + i) % 24), htemps[i], hcodes[i], hrain[i], (uint8_t)(14 + i < 19) };
  w.sunrise = 7 * 60 + 12;
  w.sunset = 19 * 60 + 4;
  w.uv = 5.2f;
  pv_prefs_seed_bytes("weather", "data", &w, sizeof(w));
}

static void seed_agenda() {
  AgendaEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.id = 1;
  ev.start = (uint32_t)time(NULL) + 28 * 60;
  ev.duration = 3600;
  strlcpy(ev.title, "Team meeting", sizeof(ev.title));
  strlcpy(ev.location, "Office, room 2", sizeof(ev.location));
  agenda_put(&ev);
  memset(&ev, 0, sizeof(ev));
  ev.id = 2;
  ev.start = (uint32_t)time(NULL) + 19 * 3600;
  ev.duration = 1800;
  strlcpy(ev.title, "Dentist", sizeof(ev.title));
  agenda_put(&ev);
  memset(&ev, 0, sizeof(ev));
  ev.id = 3;
  ev.start = (uint32_t)time(NULL) + 3 * 86400;
  ev.all_day = 1;
  strlcpy(ev.title, "Emma's birthday", sizeof(ev.title));
  agenda_put(&ev);
}

static void seed_alarms() {
  Alarm a = { 6, 45, 0x1F, 1 };
  alarms_put(-1, &a);
  a = { 9, 30, 0x60, 1 };
  alarms_put(-1, &a);
  a = { 22, 0, 0, 0 };
  alarms_put(-1, &a);
}

static void seed_notifications() {
  phone_rx_line("\x10GB({\"t\":\"notify\",\"id\":11,\"src\":\"Gmail\",\"title\":\"Your order has shipped\",\"body\":\"Your package is on its way and arrives on Thursday.\"})");
  phone_rx_line("\x10GB({\"t\":\"notify\",\"id\":12,\"src\":\"Telegram\",\"title\":\"Alex Chen\",\"body\":\"Running 10 minutes late, grab a table for us?\"})");
  phone_rx_line("\x10GB({\"t\":\"notify\",\"id\":13,\"src\":\"WhatsApp\",\"title\":\"Emma Wilson\",\"body\":\"Dinner at 8 tonight? Let's meet downtown and walk over together.\"})");
}

/* ================================ Stills ========================================== */

static void stills_faces() {
  home();
  render("face-digital");
  subj_set(subj_watchface, FACE_ANALOG);
  render("face-analog");
  subj_set(subj_watchface, FACE_MODULAR);
  render("face-modular");
  subj_set(subj_watchface, FACE_MINIMAL);
  render("face-minimal");
  subj_set(subj_watchface, FACE_DIGITAL);
  lv_screen_load(aod_screen_create());
  aod_refresh();
  render("face-always-on");
  home();
  nav_open_face_picker();
  render("face-picker");
  home();
  face_customize_open();
  render("face-customize");
  home();
  nav_show_tile(TILE_ACTIVITY);
  render("tile-activity");
  nav_show_tile(TILE_WEATHER);
  render("tile-weather");
  nav_show_tile(TILE_MUSIC);
  render("tile-music");
  home();
}

static void stills_system() {
  nav_open_quick_panel();
  render("system-quick-panel");
  home();
  nav_open_launcher();
  render("system-launcher");
  scroll_bottom();
  render("system-launcher-bottom");
  home();

  seed_notifications();
  render("system-notification-banner");
  settle(5500);
  home();
  render("face-digital-unread");
  notify_open_center();
  render("system-notifications");
  click("Emma Wilson");
  render("system-notification-detail");
  home();

  PowerState st = { true, true, true, 64 };
  system_ui_on_power(POWER_PLUGGED, &st);
  render("system-charging");
  home();
  settle(3500);  // the charging toast is gone
  kit_power_menu();
  render("system-power-menu");
  home();
  phone_rx_line("\x10GB({\"t\":\"call\",\"cmd\":\"incoming\",\"name\":\"Emma Wilson\",\"number\":\"+44 20 7946 0958\"})");
  render("system-incoming-call");
  phone_rx_line("\x10GB({\"t\":\"call\",\"cmd\":\"end\"})");
  home();
  phone_rx_line("\x10GB({\"t\":\"find\",\"n\":true})");
  render("system-find-my-watch");
  phone_rx_line("\x10GB({\"t\":\"find\",\"n\":false})");
  home();
}

static void stills_apps() {
  activity_open();
  render("app-activity");
  scroll_bottom();
  render("app-activity-history");
  home();

  workout_open();
  render("app-workout");
  click("Run");
  settle(3500);
  for (int i = 0; i < 502; i++) {  // 8 min 22 s at ~165 steps per minute
    pv_hw_steps += i % 3 ? 3 : 2;
    settle(1000);
  }
  render("app-workout-live");
  click(ICON_STOP);
  click("End");
  render("app-workout-summary");
  home();

  sleep_open();
  render("app-sleep");
  scroll_bottom();
  render("app-sleep-week");
  home();

  alarm_open();
  render("app-alarms");
  click("06:45");
  render("app-alarm-edit");
  home();
  Alarm now_alarm = { 14, 32, 0, 1 };
  alarms_put(-1, &now_alarm);
  pv_run_minute_handlers();
  render("app-alarm-ringing");
  click("Stop");
  home();
  alarms_remove(alarms_count() - 1);

  timer_open();
  render("app-timer");
  countdown_start(5 * 60 * 1000);
  pv_ms += 72 * 1000;
  render("app-timer-running");
  countdown_cancel();
  home();
  countdown_start(5 * 60 * 1000);
  pv_ms += 5 * 60 * 1000;
  settle(600);
  render("app-timer-done");
  home();

  stopwatch_open();
  click(ICON_PLAY);
  settle(21300);
  click(ICON_FLAG);
  settle(21750);
  click(ICON_FLAG);
  settle(18870);
  click(ICON_FLAG);
  settle(9450);
  render("app-stopwatch");
  home();

  calendar_open();
  render("app-calendar");
  home();

  weather_open();
  render("app-weather");
  scroll_to(300);
  render("app-weather-details");
  scroll_bottom();
  render("app-weather-forecast");
  click("Location");
  render("app-weather-location");
  click("Search city");
  render("app-weather-search");
  home();

  music_open();
  render("app-music");
  click(ICON_LIST);
  render("app-music-library");
  home();

  recorder_open();
  render("app-recorder");
  click(ICON_MICROPHONE);
  render("app-recorder-recording");
  recorder_stop();
  home();

  flashlight_open();
  render("app-flashlight");
  home();
  findphone_open();
  render("app-find-phone");
  home();
}

static void stills_extras() {
  lights_open();
  render("extra-lights");
  click("Desk lamp");
  render("extra-light-control");
  scroll_bottom();
  render("extra-light-effects");
  home();
  lights_open();
  scroll_bottom();
  click("Add light");
  render("extra-light-add");
  home();

  car_open();
  click("Low beam");
  click("Left signal");
  render("extra-car");
  scroll_bottom();
  render("extra-car-more");
  click("Live data");
  render("extra-car-live");
  home();
}

static void stills_settings() {
  settings_open();
  render("settings");
  scroll_bottom();
  render("settings-more");
  click("Extras");
  render("settings-extras");
  home();

  static const char *const PAGES[][2] = {
    { "Wi-Fi", "settings-wifi" }, { "Bluetooth", "settings-bluetooth" }, { "Display", "settings-display" },
    { "Sound", "settings-sound" }, { "Notifications", "settings-notifications" }, { "Battery", "settings-battery" },
    { "Date & time", "settings-date-time" }, { "Units", "settings-units" },
  };
  for (const auto &p : PAGES) {
    settings_open();
    scroll_to(0);
    if (!find_text(lv_screen_active(), p[0])) scroll_bottom();
    click(p[0]);
    render(p[1]);
    if (!strcmp(p[0], "Display")) {
      scroll_bottom();
      render("settings-display-wake");
      click("Motion sensor");
      render("settings-motion-sensor");
    }
    if (!strcmp(p[0], "Wi-Fi")) {
      click("Office-Guest");
      render("settings-wifi-password");
    }
    home();
  }
  settings_open();
  scroll_bottom();
  click("About");
  render("settings-about");
  scroll_bottom();
  render("settings-about-more");
  home();
  settings_open();
  scroll_bottom();
  click("System");
  render("settings-system");
  click("Update");
  click("Updater");
  render("settings-update");
  subj_set(subj_ota, 0);
  home();
}

/* ================================ Recordings ====================================== */

static void record_tour() {
  home();
  rec_start("tour");
  settle(1400);
  swipe(W / 2, 30, W / 2, 300);
  nav_open_quick_panel();
  settle(1300);
  swipe(W / 2, 380, W / 2, 120);
  kit_page_pop();
  settle(700);
  swipe(W / 2, 400, W / 2, 150);
  nav_open_launcher();
  settle(900);
  scroll_anim(LV_COORD_MAX);
  settle(500);
  scroll_anim(0);
  click("Weather");
  settle(900);
  scroll_anim(360);
  settle(600);
  back();
  click("Activity");
  settle(1200);
  back();
  swipe(W / 2, 120, W / 2, 380);  // the launcher closes downwards
  kit_page_pop();
  settle(700);
  swipe(320, H / 2, 60, H / 2);
  nav_show_tile(TILE_WEATHER);
  settle(1300);
  swipe(60, H / 2, 320, H / 2);
  nav_go_home(true);
  settle(1000);
  rec_stop();
}

static void record_apps() {
  home();
  rec_start("apps");
  settle(600);
  swipe(W / 2, 400, W / 2, 150);
  nav_open_launcher();
  settle(700);
  click("Timer");
  settle(500);
  click("5 min");
  settle(1800);
  back();
  settle(300);
  click("Sleep");
  settle(900);
  scroll_anim(LV_COORD_MAX);
  settle(500);
  back();
  settle(300);
  click("Music");
  settle(1500);
  back();
  scroll_anim(LV_COORD_MAX);
  click("Settings");
  settle(700);
  click("Display");
  settle(900);
  scroll_anim(LV_COORD_MAX);
  settle(500);
  back();
  back();
  settle(600);
  rec_stop();
  countdown_cancel();
}

static void record_extras() {
  home();
  nav_open_launcher();
  settle(600);
  scroll_bottom();
  rec_start("extras");
  settle(700);
  click("Lights");
  settle(900);
  click("Desk lamp");
  settle(800);
  wled_set_color(0, 0x3D7BFF);
  settle(700);
  wled_set_color(0, 0xBF5AF2);
  settle(700);
  wled_set_color(0, 0x30D158);
  settle(700);
  back();
  settle(500);
  back();
  settle(300);
  click("Car");
  settle(700);
  click("Low beam");
  settle(400);
  click("Left signal");
  settle(1800);
  click("Left signal");
  settle(300);
  click("Live data");
  settle(1500);
  back();
  back();
  settle(600);
  rec_stop();
}

static void record_faces() {
  static const int32_t FACES[] = { FACE_DIGITAL, FACE_ANALOG, FACE_MODULAR, FACE_MINIMAL };
  for (int32_t face : FACES) {
    subj_set(subj_watchface, face);
    home();
    char name[32];
    snprintf(name, sizeof(name), "faceshot-%d", (int)face);
    render(name);
  }
  subj_set(subj_watchface, FACE_DIGITAL);
  home();
}

int main(int argc, char **argv) {
  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_display_t *d = lv_display_create(W, H);
  lv_display_set_buffers(d, fb, NULL, sizeof(fb), LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d, flush_cb);

  seed_storage();
  seed_weather();
  settings_init();
  kit_init();
  notify_init();
  agenda_init();
  weather_init();
  activity_init();
  sleep_tracker_init();
  alarms_init();
  countdown_init();
  workout_init();
  battery_saver_init();
  wled_init();
  clap_control_init();
  car_link_init();
  system_ui_init();
  notification_center_init();
  alarm_app_init();
  timer_app_init();
  nav_init();
  screen_init();
  phone_set_event_handler(system_ui_on_phone);
  phone_init();

  lv_subject_set_string(subj_time_text, "14:32");
  lv_subject_set_string(subj_date_text, "Tue, 29 Sep");
  lv_subject_set_int(subj_wifi_enabled, 1);
  lv_subject_set_int(subj_wifi_state, WIFI_ST_CONNECTED);
  lv_subject_set_int(subj_bt_enabled, 1);
  lv_subject_set_int(subj_gadgetbridge, 1);
  lv_subject_set_int(subj_bt_state, BT_ST_CONNECTED);
  lv_subject_set_int(subj_battery, 86);
  lv_subject_set_int(subj_timezone, 2);
  subj_set(subj_ext_car, 1);
  subj_set(subj_ext_lights, 1);
  pv_hw_steps = 6420;
  settle(2100);
  seed_agenda();
  seed_alarms();

  const char *only = argc > 1 ? argv[1] : "fsaxcr";
  if (strchr(only, 'f')) stills_faces();
  if (strchr(only, 's')) stills_system();
  if (strchr(only, 'a')) stills_apps();
  if (strchr(only, 'x')) stills_extras();
  if (strchr(only, 'c')) stills_settings();
  if (strchr(only, 'r')) {
    record_faces();
    record_tour();
    record_apps();
    record_extras();
  }
  printf("done, %d failed clicks\n", pv_failures);
  return 0;
}
