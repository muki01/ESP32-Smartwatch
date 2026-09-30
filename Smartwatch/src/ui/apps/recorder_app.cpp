/*
 * recorder_app.cpp - Voice recorder: records the microphone to WAV files (16 kHz mono) in
 * /recordings on the SD card, lists them newest first, plays and deletes them. The
 * recording stops when its screen is closed, so the microphone is never left on.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../core/system.h"
#include "../../drivers/audio.h"
#include "../../drivers/sd_card.h"
#include "../../services/clock.h"
#include "../kit/kit.h"
#include "../system/screen.h"

#define REC_DIR       "/recordings"
#define REC_LIST_MAX  40
#define REC_NAME_LEN  32
#define REC_PATH_LEN  64

static lv_obj_t *rec_ui_page, *rec_ui_time, *rec_ui_button, *rec_ui_hint, *rec_ui_bars[16], *rec_ui_count;
static char rec_ui_playing[REC_PATH_LEN];
static char (*rec_names)[REC_NAME_LEN];  // file names of the list page, newest first
static uint32_t *rec_sizes;
static int rec_n;
static lv_obj_t *rec_list_content;

/* ================================ Files =========================================== */

// Inserts a recording sorted, newest (largest name) first; when full the oldest falls off.
static void rec_scan_file(const char *name, uint32_t size, void *arg) {
  size_t len = strlen(name);
  if (len <= 4 || len >= REC_NAME_LEN || strcasecmp(name + len - 4, ".wav")) return;
  int i = rec_n < REC_LIST_MAX ? rec_n : REC_LIST_MAX - 1;
  if (rec_n == REC_LIST_MAX && strcmp(name, rec_names[i]) < 0) return;
  while (i > 0 && strcmp(name, rec_names[i - 1]) > 0) {
    memcpy(rec_names[i], rec_names[i - 1], REC_NAME_LEN);
    rec_sizes[i] = rec_sizes[i - 1];
    i--;
  }
  strlcpy(rec_names[i], name, REC_NAME_LEN);
  rec_sizes[i] = size;
  if (rec_n < REC_LIST_MAX) rec_n++;
}

static int rec_scan() {
  if (!rec_names) {
    rec_names = (char(*)[REC_NAME_LEN])psram_calloc(REC_LIST_MAX, REC_NAME_LEN);
    rec_sizes = (uint32_t *)psram_calloc(REC_LIST_MAX, sizeof(uint32_t));
    if (!rec_names || !rec_sizes) return 0;
  }
  rec_n = 0;
  sd_list(REC_DIR, rec_scan_file, NULL);
  return rec_n;
}

// REC_20260930_143205.wav -> "30 Sep, 14:32"
static void rec_title(const char *name, char *buf, size_t len) {
  static const char *const MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  int y, mo, d, h, mi, s;
  if (sscanf(name, "REC_%4d%2d%2d_%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) == 6 && mo >= 1 && mo <= 12) {
    char hm[16];
    clock_format_hm(h, mi, hm, sizeof(hm));
    snprintf(buf, len, "%d %s, %s", d, MON[mo - 1], hm);
  } else {
    strlcpy(buf, name, len);
  }
}

static void rec_duration(uint32_t bytes, char *buf, size_t len) {
  uint32_t s = bytes > 44 ? (bytes - 44) / 32000 : 0;
  snprintf(buf, len, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

/* ================================ List page ======================================= */

static void rec_list_fill();

static void rec_delete_cb(void *user_data) {
  int i = (int)(intptr_t)user_data;
  if (i >= rec_n) return;
  char path[REC_PATH_LEN];
  snprintf(path, sizeof(path), REC_DIR "/%s", rec_names[i]);
  if (!strcmp(path, rec_ui_playing)) recorder_play_stop();
  sd_remove(path);
  rec_list_fill();
}

static void rec_row_long_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  lv_indev_t *indev = lv_indev_active();
  if (indev) lv_indev_wait_release(indev);
  char title[40];
  rec_title(rec_names[i], title, sizeof(title));
  kit_confirm("Delete recording?", title, "Delete", true, rec_delete_cb, (void *)(intptr_t)i);
}

static void rec_row_cb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i >= rec_n) return;
  audio_click();
  char path[REC_PATH_LEN];
  snprintf(path, sizeof(path), REC_DIR "/%s", rec_names[i]);
  if (lv_subject_get_int(subj_recorder) == REC_PLAYING && !strcmp(path, rec_ui_playing)) {
    recorder_play_stop();
    rec_ui_playing[0] = 0;
  } else if (recorder_play(path)) {
    strlcpy(rec_ui_playing, path, sizeof(rec_ui_playing));
  }
}

// Play icons follow the player state.
static void rec_list_state_obs(lv_observer_t *observer, lv_subject_t *subject) {
  lv_obj_t *c = lv_observer_get_target_obj(observer);
  bool playing = lv_subject_get_int(subject) == REC_PLAYING;
  if (!playing) rec_ui_playing[0] = 0;
  for (int i = 0; i < rec_n && i < (int)lv_obj_get_child_count(c); i++) {
    lv_obj_t *row = lv_obj_get_child(c, i);
    char path[REC_PATH_LEN];
    snprintf(path, sizeof(path), REC_DIR "/%s", rec_names[i]);
    bool me = playing && !strcmp(path, rec_ui_playing);
    lv_obj_t *icon = lv_obj_get_child(row, 0);
    lv_label_set_text(icon, me ? ICON_STOP : ICON_PLAY);
    lv_obj_set_style_bg_color(icon, lv_color_hex(me ? KIT_COLOR_ACCENT : KIT_COLOR_CARD2), 0);
  }
}

static void rec_list_fill() {
  if (!rec_list_content) return;
  lv_obj_clean(rec_list_content);
  rec_scan();
  for (int i = 0; i < rec_n; i++) {
    char title[40], dur[16];
    rec_title(rec_names[i], title, sizeof(title));
    rec_duration(rec_sizes[i], dur, sizeof(dur));
    lv_obj_t *row = kit_row(rec_list_content, ICON_PLAY, KIT_COLOR_CARD2, title, dur, false);
    lv_obj_add_event_cb(row, rec_row_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(row, rec_row_long_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
  }
  if (!rec_n) kit_empty_state(rec_list_content, ICON_MICROPHONE, "No recordings yet.");
  else kit_note(rec_list_content, "Tap to play, hold to delete.");
}

static void rec_list_deleted_cb(lv_event_t *e) {
  rec_list_content = NULL;
  recorder_play_stop();
}

static void rec_list_cb(lv_event_t *e) {
  audio_click();
  lv_obj_t *page = kit_page_create("Recordings", true);
  rec_list_content = kit_page_content(page);
  rec_list_fill();
  lv_subject_add_observer_obj(subj_recorder, rec_list_state_obs, rec_list_content, NULL);
  lv_obj_add_event_cb(page, rec_list_deleted_cb, LV_EVENT_DELETE, NULL);
  kit_page_push(page);
}

/* ================================ Recorder page =================================== */

static void rec_ui_update(lv_timer_t *t) {
  if (!rec_ui_page) return;
  bool recording = lv_subject_get_int(subj_recorder) == REC_RECORDING;
  uint32_t s = recording ? recorder_seconds() : 0;
  lv_label_set_text_fmt(rec_ui_time, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  lv_label_set_text(rec_ui_button, recording ? ICON_STOP : ICON_MICROPHONE);
  lv_label_set_text(rec_ui_hint, recording ? "Recording..." : "Tap to record");
  lv_obj_set_style_text_color(rec_ui_time, lv_color_hex(recording ? 0xFFFFFF : KIT_COLOR_TEXT2), 0);
  // Level meter: bars grow from the middle with the loudness.
  uint32_t level = recorder_level();
  for (int i = 0; i < 16; i++) {
    int32_t shape = 100 - abs(i * 2 - 15) * 6;  // taller in the middle
    int32_t h = recording ? max((int32_t)6, (int32_t)(level * shape * 70 / 10000)) : 6;
    lv_obj_set_height(rec_ui_bars[i], h);
  }
}

static void rec_button_cb(lv_event_t *e) {
  audio_click();
  if (lv_subject_get_int(subj_recorder) == REC_RECORDING) {
    recorder_stop();
    kit_toast("Recording saved");
    return;
  }
  if (!sd_available()) {
    kit_toast("Insert a microSD card");
    return;
  }
  sd_mkdir(REC_DIR);
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  char path[REC_PATH_LEN];
  snprintf(path, sizeof(path), REC_DIR "/REC_%04d%02d%02d_%02d%02d%02d.wav", t.tm_year + 1900, t.tm_mon + 1,
           t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  if (!recorder_start(path)) kit_toast("Recorder busy");
}

static void rec_count_obs(lv_observer_t *observer, lv_subject_t *subject) {
  if (lv_subject_get_int(subject) != REC_IDLE) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "%d saved", rec_scan());
  lv_label_set_text(lv_observer_get_target_obj(observer), buf);
}

static void rec_page_deleted_cb(lv_event_t *e) {
  if (lv_subject_get_int(subj_recorder) == REC_RECORDING) recorder_stop();
  rec_ui_page = NULL;
  screen_keep_awake(false);
}

void recorder_open() {
  lv_obj_t *page = kit_page_create("Recorder", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (!sd_available()) {
    kit_empty_state(c, ICON_SD_CARD, "Insert a microSD card to record.");
    kit_page_push(page);
    return;
  }
  rec_ui_time = kit_label(c, &font_num_64, KIT_COLOR_TEXT2, "00:00");

  lv_obj_t *meter = kit_container(c);
  lv_obj_set_size(meter, 16 * 12, 74);
  lv_obj_set_flex_flow(meter, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(meter, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < 16; i++) {
    lv_obj_t *bar = kit_container(meter);
    lv_obj_set_size(bar, 6, 6);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(KIT_COLOR_ACCENT), 0);
    rec_ui_bars[i] = bar;
  }

  rec_ui_button = kit_round_button(c, ICON_MICROPHONE, KIT_COLOR_ACCENT, 96);
  lv_obj_add_event_cb(rec_ui_button, rec_button_cb, LV_EVENT_CLICKED, NULL);
  rec_ui_hint = kit_label(c, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "");

  lv_obj_t *list = kit_row(c, ICON_LIST, KIT_COLOR_GRAY, "Recordings", "", true);
  lv_obj_set_style_margin_top(list, 8, 0);
  rec_ui_count = kit_row_subtitle(list);
  lv_subject_add_observer_obj(subj_recorder, rec_count_obs, rec_ui_count, NULL);
  lv_obj_add_event_cb(list, rec_list_cb, LV_EVENT_CLICKED, NULL);

  rec_ui_page = page;
  lv_obj_add_event_cb(page, rec_page_deleted_cb, LV_EVENT_DELETE, NULL);
  rec_ui_update(kit_page_timer(page, rec_ui_update, 150, NULL));
  screen_keep_awake(true);
  kit_page_push(page);
}
