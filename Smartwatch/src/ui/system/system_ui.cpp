/*
 * system_ui.cpp - UI reactions to system events.
 *
 *   side key       short press: screen on/off (or snooze/stop a ringing alert),
 *                  hold: power menu (6 s: the PMU cuts the power)
 *   charger        plugging in wakes the screen and shows the charge level; a finished
 *                  charge and a low battery (20 %, 10 %) become notifications
 *   phone          incoming calls (answer / decline) and "find my watch" ring with a
 *                  full-screen alert for at most a minute
 *   update         a full-screen progress ring while firmware is being received
 */
#include "system_ui.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../core/system.h"
#include "../../drivers/audio.h"
#include "../../services/notifications.h"
#include "../../services/ota.h"
#include "../kit/kit.h"
#include "screen.h"

#define SYS_ALERT_MAX_MS  60000
#define SYS_OTA_POLL_MS   250

static bool sys_call_active;
static bool sys_find_active;
static lv_timer_t *sys_alert_timer;
static lv_obj_t *sys_ota_page, *sys_ota_ring, *sys_ota_label;

/* ================================ Power =========================================== */

void system_ui_on_power(PowerEvent event, const PowerState *s) {
  switch (event) {
    case POWER_KEY_SHORT:
      if (!kit_alert_hw_button()) screen_toggle();
      break;
    case POWER_KEY_LONG:
      if (kit_alert_hw_button()) break;
      screen_wake(WAKE_BUTTON);
      kit_power_menu();
      break;
    case POWER_PLUGGED: {
      screen_wake(WAKE_EVENT);  // show that charging started
      char text[40];
      const char *what = s->charging ? "Charging" : s->level >= 95 ? "Fully charged" : "Connected";
      if (s->level >= 0) snprintf(text, sizeof(text), "%s  \xE2\x80\xA2  %d%%", what, (int)s->level);
      else strlcpy(text, "USB power connected", sizeof(text));
      kit_toast(text);
      break;
    }
    case POWER_CHARGED:
      notify_post("Battery", "Fully charged", "You can unplug your watch.", ICON_BATTERY_FULL, KIT_COLOR_GREEN);
      break;
    case POWER_LOW: {
      char body[64];
      snprintf(body, sizeof(body), "%d%% remaining. Charge it soon or turn on Battery saver.", (int)s->level);
      notify_post("Battery", s->level <= 10 ? "Battery very low" : "Battery low", body, ICON_BATTERY_1, KIT_COLOR_RED);
      break;
    }
  }
}

/* ================================ Phone =========================================== */

static void sys_alert_timer_stop() {
  if (!sys_alert_timer) return;
  lv_timer_delete(sys_alert_timer);
  sys_alert_timer = NULL;
}

static void sys_alerts_end() {
  sys_alert_timer_stop();
  if (!sys_call_active && !sys_find_active) return;
  sys_call_active = false;
  sys_find_active = false;
  audio_alert_stop();
  kit_alert_close();
}

static void sys_alert_timeout_cb(lv_timer_t *t) {
  sys_alert_timer = NULL;  // one-shot
  sys_alerts_end();
}

static void sys_alert_timer_start() {
  sys_alert_timer_stop();
  sys_alert_timer = lv_timer_create(sys_alert_timeout_cb, SYS_ALERT_MAX_MS, NULL);
  lv_timer_set_repeat_count(sys_alert_timer, 1);
}

static void sys_call_alert_cb(int button) {
  sys_call_active = false;
  sys_alert_timer_stop();
  audio_alert_stop();
  if (button == 0) phone_call_reply(true);
  else if (button == 1) phone_call_reply(false);
  // 2 (side button): only silence the watch
}

static void sys_find_alert_cb(int button) {
  sys_find_active = false;
  sys_alert_timer_stop();
  audio_alert_stop();
}

void system_ui_on_phone(PhoneEvent event, const PhoneCall *call) {
  switch (event) {
    case PHONE_CALL_INCOMING: {
      if (kit_alert_active() && !sys_call_active) return;  // an alarm is ringing: it wins
      sys_call_active = true;
      if (!lv_subject_get_int(subj_silent) && !lv_subject_get_int(subj_dnd)) audio_alert_start(ALERT_CALL);
      const char *who = call->name[0] ? call->name : call->number[0] ? call->number : "Unknown";
      kit_alert(ICON_PHONE, KIT_COLOR_GREEN, who, "Incoming call", "Answer", "Decline", sys_call_alert_cb);
      kit_alert_set_secondary_color(KIT_COLOR_RED);
      sys_alert_timer_start();
      break;
    }
    case PHONE_CALL_ENDED:
      if (sys_call_active) sys_alerts_end();
      break;
    case PHONE_FIND_START:
      if (kit_alert_active()) return;
      sys_find_active = true;
      audio_alert_start(ALERT_FIND);
      kit_alert(ICON_SEARCH, KIT_COLOR_GREEN, "Here I am", "Your phone is looking for me", "Found it", NULL, sys_find_alert_cb);
      sys_alert_timer_start();
      break;
    case PHONE_FIND_STOP:
      if (sys_find_active) sys_alerts_end();
      break;
  }
}

/* ================================ Wireless update ================================= */

static void sys_ota_page_deleted_cb(lv_event_t *e) {
  sys_ota_page = sys_ota_ring = sys_ota_label = NULL;
  screen_keep_awake(false);
}

static void sys_ota_show_progress() {
  if (sys_ota_page) return;
  sys_ota_page = kit_page_create_bare(0x000000);
  kit_page_set_close_dir(sys_ota_page, LV_DIR_NONE);
  sys_ota_ring = kit_ring(sys_ota_page, 240, 14, KIT_COLOR_BLUE);
  lv_obj_align(sys_ota_ring, LV_ALIGN_CENTER, 0, -20);
  lv_obj_t *col = kit_col_box(sys_ota_ring, LV_FLEX_ALIGN_CENTER, 4);
  lv_obj_center(col);
  kit_label(col, &font_icons_32, KIT_COLOR_BLUE, ICON_DOWNLOAD);
  sys_ota_label = kit_label(col, &font_num_44, 0xFFFFFF, "0%");
  lv_obj_t *note = kit_label(sys_ota_page, KIT_FONT_BODY, KIT_COLOR_TEXT2, "Updating, do not turn off");
  lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_add_event_cb(sys_ota_page, sys_ota_page_deleted_cb, LV_EVENT_DELETE, NULL);
  screen_keep_awake(true);
  screen_wake(WAKE_EVENT);
  kit_open(sys_ota_page, KIT_ANIM_FADE, true);
}

static void sys_ota_poll_cb(lv_timer_t *t) {
  OtaPhase phase = ota_phase();
  if (phase == OTA_RECEIVING || phase == OTA_DONE) {
    sys_ota_show_progress();
    if (sys_ota_label) {
      int pct = phase == OTA_DONE ? 100 : ota_percent();
      lv_label_set_text_fmt(sys_ota_label, "%d%%", pct);
      lv_arc_set_value(sys_ota_ring, pct);
    }
  } else if (phase == OTA_FAILED && sys_ota_page && kit_page_is_top(sys_ota_page)) {
    kit_page_pop();
  }
}

/* ================================ Init ============================================ */

static void sys_message(const char *text) {
  kit_toast(text);
}

void system_ui_init() {
  system_set_message_handler(sys_message);
  lv_timer_create(sys_ota_poll_cb, SYS_OTA_POLL_MS, NULL);
}
