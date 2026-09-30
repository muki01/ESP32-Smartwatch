/*
 * settings_app.cpp - The Settings app: the root list, Extras, About and System.
 *
 *   Settings
 *   |- Wi-Fi          on/off, networks, password, details, forget        (settings_connect.cpp)
 *   |- Bluetooth      on/off, Gadgetbridge phone link, name, addresses   (settings_connect.cpp)
 *   |- Display        brightness, timeout, watch face, wake up, motion   (settings_device.cpp)
 *   |- Sound          volume, silent mode, touch sounds                  (settings_device.cpp)
 *   |- Notifications  do not disturb, sounds, wake screen, clear         (settings_device.cpp)
 *   |- Battery        level, voltages, battery saver                     (settings_device.cpp)
 *   |- Date & time    automatic, time zone, 24-hour, manual              (settings_device.cpp)
 *   |- Units          metric / imperial                                  (settings_device.cpp)
 *   |- Extras         Car, WLED lights, clap control
 *   |- About          device, firmware, memory, developer options
 *   '- System         wireless update, restart, power off, reset
 *
 * Pages are built when opened and deleted when closed; every value is bound to a
 * settings subject, so the pages always show the live state.
 */
#include "settings_internal.h"

#include <Arduino.h>
#include "../../../core/settings.h"
#include "../../../core/system.h"
#include "../../../drivers/audio.h"
#include "../../../drivers/imu.h"
#include "../../../drivers/sd_card.h"
#include "../../../services/ota.h"
#include "../../../services/wled.h"
#include "../apps.h"

/* ================================ Helpers ========================================= */

static void sui_open_cb(lv_event_t *e) {
  audio_click();
  sui_builder_t build = (sui_builder_t)lv_event_get_user_data(e);
  kit_page_push(build());
}

lv_obj_t *sui_nav_row(lv_obj_t *parent, const char *symbol, uint32_t color, const char *title, sui_builder_t build) {
  lv_obj_t *row = kit_row(parent, symbol, color, title, NULL, true);
  lv_obj_add_event_cb(row, sui_open_cb, LV_EVENT_CLICKED, (void *)build);
  return row;
}

static void sui_format_bytes(uint64_t bytes, char *buf, size_t len) {
  if (bytes >= 1024ULL * 1024 * 1024) snprintf(buf, len, "%.1f GB", bytes / 1073741824.0);
  else if (bytes >= 1024 * 1024) snprintf(buf, len, "%.1f MB", bytes / 1048576.0);
  else snprintf(buf, len, "%u kB", (unsigned)(bytes / 1024));
}

/* ================================ Extras ========================================== */

static void sui_extras_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  int n = (lv_subject_get_int(subj_ext_car) ? 1 : 0) + (lv_subject_get_int(subj_ext_lights) ? 1 : 0);
  if (!n) lv_label_set_text(lv_observer_get_target_obj(observer), "Car, lights, clap control");
  else lv_label_set_text_fmt(lv_observer_get_target_obj(observer), "%d on", n);
}

// Clap control needs the Lights extra.
static void sui_clap_row_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_set_disabled(lv_observer_get_target_obj(observer), !lv_subject_get_int(subj_ext_lights));
}

static lv_obj_t *sui_build_extras() {
  lv_obj_t *page = kit_page_create("Extras", true);
  lv_obj_t *c = kit_page_content(page);
  kit_note(c, "Extra features beyond the watch. Switched on, they appear in the app list.");
  kit_switch_row(c, ICON_CAR, KIT_COLOR_ACCENT, "Car control", "Lights, signals, live data", subj_ext_car);
  lv_obj_t *lights = kit_switch_row(c, ICON_LIGHTS, KIT_COLOR_YELLOW, "Lights", "WLED on your Wi-Fi", subj_ext_lights);
  lv_obj_set_style_text_color(lv_obj_get_child(lights, 0), lv_color_hex(0x1C1C1E), 0);
  lv_obj_t *clap = kit_switch_row(c, ICON_HAND, KIT_COLOR_ORANGE, "Clap control", "Double clap toggles lights", subj_ext_clap);
  lv_subject_add_observer_obj(subj_ext_lights, sui_clap_row_obs, clap, NULL);
  kit_note(c, "Car control talks to the car module over its Wi-Fi (join it in Wi-Fi settings). "
              "Clap control keeps the microphone listening while the watch is on Wi-Fi and uses some extra battery.");
  return page;
}

/* ================================ About =========================================== */

static void sui_format_mac(const uint8_t *mac, char *buf, size_t len) {
  snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void sui_about_timer_cb(lv_timer_t *t) {
  lv_obj_t *card = (lv_obj_t *)lv_timer_get_user_data(t);
  SystemInfo info;
  system_info(&info);
  char internal[16], psram[16];
  sui_format_bytes(info.free_internal, internal, sizeof(internal));
  sui_format_bytes(info.free_psram, psram, sizeof(psram));
  lv_label_set_text_fmt(lv_obj_get_child(lv_obj_get_child(card, 0), 1), "%s RAM\n%s PSRAM", internal, psram);
  uint32_t s = millis() / 1000;
  lv_label_set_text_fmt(lv_obj_get_child(lv_obj_get_child(card, 1), 1), "%ud %02u:%02u:%02u", (unsigned)(s / 86400),
                        (unsigned)(s / 3600 % 24), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

static lv_obj_t *sui_build_about() {
  lv_obj_t *page = kit_page_create("About", true);
  lv_obj_t *c = kit_page_content(page);
  char buf[48];
  SystemInfo info;
  system_info(&info);

  lv_obj_t *device = kit_info_card(c);
  kit_info_row(device, "Name", DEVICE_NAME);
  kit_info_row(device, "Model", DEVICE_MODEL);
  kit_info_row(device, "Firmware", FW_VERSION);
  kit_info_row(device, "Built", __DATE__);

  kit_section(c, "HARDWARE");
  lv_obj_t *hw = kit_info_card(c);
  snprintf(buf, sizeof(buf), "%s rev %d", info.chip, info.revision);
  kit_info_row(hw, "Chip", buf);
  snprintf(buf, sizeof(buf), "%d cores, %lu MHz", info.cores, (unsigned long)info.cpu_mhz);
  kit_info_row(hw, "CPU", buf);
  sui_format_bytes(info.flash_bytes, buf, sizeof(buf));
  kit_info_row(hw, "Flash", buf);
  sui_format_bytes(info.psram_bytes, buf, sizeof(buf));
  kit_info_row(hw, "PSRAM", info.psram_bytes ? buf : "None");
  kit_info_row(hw, "Motion sensor", imu_available() ? "QMI8658" : "Not found");
  if (sd_available()) {
    char used[16], total[16];
    sui_format_bytes(sd_used_bytes(), used, sizeof(used));
    sui_format_bytes(sd_total_bytes(), total, sizeof(total));
    snprintf(buf, sizeof(buf), "%s of %s", used, total);
  } else {
    strlcpy(buf, "No card", sizeof(buf));
  }
  kit_info_row(hw, "SD card", buf);

  kit_section(c, "STATUS");
  lv_obj_t *live = kit_info_card(c);
  kit_info_row(live, "Free memory", "-");
  kit_info_row(live, "Uptime", "-");
  sui_about_timer_cb(kit_page_timer(page, sui_about_timer_cb, 1000, live));

  kit_section(c, "NETWORK");
  lv_obj_t *net = kit_info_card(c);
  sui_format_mac(info.mac_wifi, buf, sizeof(buf));
  kit_info_row(net, "Wi-Fi MAC", buf);
  sui_format_mac(info.mac_bt, buf, sizeof(buf));
  kit_info_row(net, "Bluetooth", buf);

  kit_section(c, "SOFTWARE");
  lv_obj_t *sw = kit_info_card(c);
  snprintf(buf, sizeof(buf), "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  kit_info_row(sw, "LVGL", buf);
  kit_info_row(sw, "Arduino core", info.core_version);
  kit_info_row(sw, "ESP-IDF", info.idf_version);

  kit_section(c, "DEVELOPER");
  kit_switch_row(c, NULL, 0, "FPS monitor", "Frame rate and CPU load", subj_perf_overlay);
  return page;
}

/* ================================ System ========================================== */

static void sui_ota_timer_cb(lv_timer_t *t) {
  lv_label_set_text((lv_obj_t *)lv_timer_get_user_data(t), ota_status_text());
}

static void sui_ota_subtitle_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_label_set_text(lv_observer_get_target_obj(observer), lv_subject_get_int(subj_ota) ? "Listening" : "Off");
}

static lv_obj_t *sui_build_ota() {
  lv_obj_t *page = kit_page_create("Update", true);
  lv_obj_t *c = kit_page_content(page);
  kit_switch_row(c, ICON_DOWNLOAD, KIT_COLOR_BLUE, "Updater", "Via web browser", subj_ota);
  lv_obj_t *card = kit_info_card(c);
  lv_obj_t *status = kit_info_row(card, "Status", "");
  sui_ota_timer_cb(kit_page_timer(page, sui_ota_timer_cb, 1000, status));
  kit_note(c, "Open the address in a browser on the same Wi-Fi, choose the firmware .bin file "
              "(Arduino IDE: Sketch > Export Compiled Binary) and enter the PIN. "
              "The watch installs it and restarts. It turns off again after a restart.");
  return page;
}

static void sui_restart_cb(void *user_data) {
  system_restart();
}

static void sui_power_off_cb(void *user_data) {
  system_power_off();
}

static void sui_reset_cb(void *user_data) {
  settings_factory_reset();
}

static void sui_restart_ask_cb(lv_event_t *e) {
  audio_click();
  kit_confirm("Restart?", NULL, "Restart", false, sui_restart_cb, NULL);
}

static void sui_power_off_ask_cb(lv_event_t *e) {
  audio_click();
  kit_confirm("Power off?", "Press the side button to turn the watch on again.", "Power off", true, sui_power_off_cb, NULL);
}

static void sui_reset_ask_cb(lv_event_t *e) {
  audio_click();
  kit_confirm("Reset settings?", "All settings, alarms, history, saved Wi-Fi networks and lights are erased, then the watch restarts.",
              "Reset", true, sui_reset_cb, NULL);
}

static lv_obj_t *sui_build_system() {
  lv_obj_t *page = kit_page_create("System", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_t *ota = sui_nav_row(c, ICON_DOWNLOAD, KIT_COLOR_BLUE, "Update", sui_build_ota);
  lv_subject_add_observer_obj(subj_ota, sui_ota_subtitle_obs, kit_row_subtitle(ota), NULL);
  lv_obj_t *restart = kit_row(c, ICON_SYNC, KIT_COLOR_ORANGE, "Restart", NULL, false);
  lv_obj_add_event_cb(restart, sui_restart_ask_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *off = kit_row(c, ICON_POWER, KIT_COLOR_RED, "Power off", NULL, false);
  lv_obj_add_event_cb(off, sui_power_off_ask_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *reset = kit_row(c, ICON_TRASH, KIT_COLOR_RED, "Reset settings", "Erase all data", false);
  lv_obj_add_event_cb(reset, sui_reset_ask_cb, LV_EVENT_CLICKED, NULL);
  return page;
}

/* ================================ Root page ======================================= */

static lv_obj_t *sui_build_root() {
  lv_obj_t *page = kit_page_create("Settings", true);
  lv_obj_t *c = kit_page_content(page);

  lv_obj_t *wifi = sui_nav_row(c, ICON_WIFI, KIT_COLOR_BLUE, "Wi-Fi", sui_build_wifi);
  lv_subject_add_observer_obj(subj_wifi_state, sui_wifi_subtitle_obs, kit_row_subtitle(wifi), NULL);

  lv_obj_t *bt = sui_nav_row(c, ICON_BLUETOOTH, KIT_COLOR_INDIGO, "Bluetooth", sui_build_bluetooth);
  lv_subject_add_observer_obj(subj_bt_state, sui_bt_subtitle_obs, kit_row_subtitle(bt), NULL);

  lv_obj_t *display = sui_nav_row(c, ICON_SUN, KIT_COLOR_ORANGE, "Display", sui_build_display);
  lv_subject_add_observer_obj(subj_brightness, sui_display_subtitle_obs, kit_row_subtitle(display), NULL);
  lv_subject_add_observer_obj(subj_screen_timeout, sui_display_subtitle_obs, kit_row_subtitle(display), NULL);
  lv_subject_add_observer_obj(subj_aod, sui_display_subtitle_obs, kit_row_subtitle(display), NULL);

  lv_obj_t *sound = sui_nav_row(c, ICON_VOLUME, KIT_COLOR_PINK, "Sound", sui_build_sound);
  lv_subject_add_observer_obj(subj_volume, sui_sound_subtitle_obs, kit_row_subtitle(sound), NULL);
  lv_subject_add_observer_obj(subj_silent, sui_sound_subtitle_obs, kit_row_subtitle(sound), NULL);

  lv_obj_t *notif = sui_nav_row(c, ICON_BELL, KIT_COLOR_RED, "Notifications", sui_build_notifications);
  lv_subject_add_observer_obj(subj_notif_count, sui_notif_subtitle_obs, kit_row_subtitle(notif), NULL);
  lv_subject_add_observer_obj(subj_dnd, sui_notif_subtitle_obs, kit_row_subtitle(notif), NULL);

  lv_obj_t *battery = sui_nav_row(c, ICON_BATTERY_FULL, KIT_COLOR_GREEN, "Battery", sui_build_battery);
  lv_subject_add_observer_obj(subj_battery, sui_battery_row_obs, battery, NULL);
  lv_subject_add_observer_obj(subj_charging, sui_battery_row_obs, battery, NULL);
  lv_subject_add_observer_obj(subj_usb_power, sui_battery_row_obs, battery, NULL);

  lv_obj_t *datetime = sui_nav_row(c, ICON_CLOCK, KIT_COLOR_TEAL, "Date & time", sui_build_datetime);
  lv_subject_add_observer_obj(subj_time_text, sui_datetime_subtitle_obs, kit_row_subtitle(datetime), NULL);
  lv_subject_add_observer_obj(subj_timezone, sui_datetime_subtitle_obs, kit_row_subtitle(datetime), NULL);

  lv_obj_t *units = sui_nav_row(c, ICON_RULER, KIT_COLOR_INDIGO, "Units", sui_build_units);
  lv_subject_add_observer_obj(subj_units, sui_units_obs, kit_row_subtitle(units), NULL);

  lv_obj_t *extras = sui_nav_row(c, ICON_PUZZLE, KIT_COLOR_PURPLE, "Extras", sui_build_extras);
  lv_subject_add_observer_obj(subj_ext_car, sui_extras_subtitle_obs, kit_row_subtitle(extras), NULL);
  lv_subject_add_observer_obj(subj_ext_lights, sui_extras_subtitle_obs, kit_row_subtitle(extras), NULL);

  lv_obj_t *about = sui_nav_row(c, ICON_INFO, KIT_COLOR_GRAY, "About", sui_build_about);
  lv_label_set_text(kit_row_subtitle(about), DEVICE_NAME ", v" FW_VERSION);

  lv_obj_t *system = sui_nav_row(c, ICON_POWER, KIT_COLOR_RED, "System", sui_build_system);
  lv_label_set_text(kit_row_subtitle(system), "Update, restart, reset");
  return page;
}

void settings_open() {
  kit_page_push(sui_build_root());
}
