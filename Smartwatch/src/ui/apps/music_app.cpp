/*
 * music_app.cpp - The music player: the Music tile next to the watch face and the Music
 * app from the launcher (same player), plus the track library.
 *
 * Source: the phone's player while Gadgetbridge reports one (the controls then steer the
 * phone), otherwise the WAV files on the SD card (drivers/audio.cpp).
 *
 *        [repeat]   SD card   [library]
 *                 +---------+
 *                 |  music  |
 *                 +---------+
 *              Title (scrolls if long)
 *                     Artist
 *          1:24 ----------o---- 3:05
 *      [-]  [|<]   [ PLAY ]   [>|]  [+]
 */
#include "apps.h"

#include <Arduino.h>
#include "../../core/settings.h"
#include "../../drivers/audio.h"
#include "../../drivers/sd_card.h"
#include "../../services/phone.h"
#include "../faces/faces.h"
#include "../kit/kit.h"
#include "../system/screen.h"

#define MUS_VIEWS 2  // 0 = tile, 1 = app page

struct MusicView {
  lv_obj_t *root, *source, *art, *title, *artist, *progress, *bar, *pos, *len, *play, *repeat, *library, *empty;
};

static MusicView mus_views[MUS_VIEWS];

static bool mus_phone_mode() {
  return phone_connected() && phone_music()->valid;
}

static void mus_format_time(uint32_t s, char *buf, size_t len) {
  snprintf(buf, len, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

/* ================================ Controls ======================================== */

static void mus_play_cb(lv_event_t *e) {
  audio_click();
  if (mus_phone_mode()) phone_music_cmd(phone_music()->playing ? "pause" : "play");
  else music_toggle();
}

static void mus_prev_cb(lv_event_t *e) {
  audio_click();
  if (mus_phone_mode()) phone_music_cmd("previous");
  else music_prev();
}

static void mus_next_cb(lv_event_t *e) {
  audio_click();
  if (mus_phone_mode()) phone_music_cmd("next");
  else music_next();
}

static void mus_volume_cb(lv_event_t *e) {
  int dir = (int)(intptr_t)lv_event_get_user_data(e);
  audio_click();
  if (mus_phone_mode()) {
    phone_music_cmd(dir > 0 ? "volumeup" : "volumedown");
    return;
  }
  int32_t v = constrain(lv_subject_get_int(subj_volume) + dir * 10, 0, 100);
  lv_subject_set_int(subj_volume, v);
  char text[24];
  snprintf(text, sizeof(text), "Volume %d%%", (int)v);
  kit_toast(text);
}

static void mus_repeat_cb(lv_event_t *e) {
  audio_click();
  MusicState st;
  music_get_state(&st);
  music_set_repeat(!st.repeat_one);
  kit_toast(st.repeat_one ? "Repeat off" : "Repeat this track");
}

/* ================================ Library ========================================= */

static void mus_track_cb(lv_event_t *e) {
  audio_click();
  music_play((int)(intptr_t)lv_event_get_user_data(e));
  kit_page_pop();
}

static void mus_library_cb(lv_event_t *e) {
  audio_click();
  lv_obj_t *page = kit_page_create("Library", true);
  lv_obj_t *c = kit_page_content(page);
  MusicState st;
  music_get_state(&st);
  if (!st.count) {
    kit_empty_state(c, ICON_SD_CARD, sd_available() ? "No music. Copy 16-bit .wav files to the card." : "No SD card");
  }
  for (int i = 0; i < st.count; i++) {
    char artist[64], title[96];
    music_track_info(i, artist, sizeof(artist), title, sizeof(title));
    bool current = i == st.index;
    lv_obj_t *row = kit_row(c, current && st.playing ? ICON_PLAY : ICON_MUSIC, current ? KIT_COLOR_PINK : KIT_COLOR_CARD2,
                            title, artist[0] ? artist : NULL, false);
    lv_obj_add_event_cb(row, mus_track_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  kit_page_push(page);
}

/* ================================ View ============================================ */

static void mus_update(int v) {
  MusicView &m = mus_views[v];
  if (!m.root) return;
  char buf[96], artist[64];
  bool phone = mus_phone_mode();
  MusicState st;
  music_get_state(&st);
  bool available = phone || st.count > 0;

  lv_obj_set_hidden(m.empty, available);
  lv_obj_set_hidden(m.progress, !available);
  lv_obj_set_hidden(m.title, !available);
  lv_obj_set_hidden(m.artist, !available);
  lv_obj_set_hidden(m.repeat, phone || !st.count);
  lv_obj_set_hidden(m.library, phone || !st.count);
  lv_label_set_text(m.source, phone ? ICON_MOBILE "  Phone" : ICON_SD_CARD "  SD card");

  bool playing;
  uint32_t pos, len;
  if (phone) {
    const PhoneMusic *pm = phone_music();
    playing = pm->playing;
    len = pm->duration;
    pos = pm->position + (pm->playing ? (millis() - pm->position_ms) / 1000 : 0);
    if (len && pos > len) pos = len;
    lv_label_set_text(m.title, pm->track[0] ? pm->track : "Unknown track");
    lv_label_set_text(m.artist, pm->artist[0] ? pm->artist : "");
  } else {
    playing = st.playing;
    pos = st.pos_s;
    len = st.len_s;
    music_track_info(st.index < 0 ? 0 : st.index, artist, sizeof(artist), buf, sizeof(buf));
    lv_label_set_text(m.title, buf[0] ? buf : "No music");
    lv_label_set_text(m.artist, artist[0] ? artist : (st.count ? "Unknown artist" : ""));
  }
  lv_label_set_text(m.play, playing ? ICON_PAUSE : ICON_PLAY);
  lv_obj_set_style_text_color(m.repeat, lv_color_hex(st.repeat_one ? KIT_COLOR_PINK : KIT_COLOR_TEXT2), 0);
  lv_bar_set_value(m.bar, len ? (int32_t)(pos * 1000 / len) : 0, LV_ANIM_OFF);
  mus_format_time(pos, buf, sizeof(buf));
  lv_label_set_text(m.pos, buf);
  if (len) mus_format_time(len, buf, sizeof(buf));
  else strlcpy(buf, "--:--", sizeof(buf));
  lv_label_set_text(m.len, buf);
}

static void mus_tick_cb(lv_timer_t *t) {
  for (int v = 0; v < MUS_VIEWS; v++) {
    MusicView &m = mus_views[v];
    if (m.root && lv_screen_active() == lv_obj_get_screen(m.root) && !screen_is_off()) mus_update(v);
  }
}

static void mus_changed_obs(lv_observer_t *observer, lv_subject_t *subject) {
  mus_update((int)(intptr_t)lv_observer_get_user_data(observer));
}

static void mus_view_deleted_cb(lv_event_t *e) {
  int v = (int)(intptr_t)lv_event_get_user_data(e);
  memset(&mus_views[v], 0, sizeof(MusicView));
}

static lv_obj_t *mus_small_button(lv_obj_t *parent, const char *icon, lv_event_cb_t cb, void *user_data) {
  lv_obj_t *b = kit_round_button(parent, icon, KIT_COLOR_CARD, 44);
  lv_obj_set_style_text_color(b, lv_color_hex(KIT_COLOR_TEXT2), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
  return b;
}

// Builds the player into `parent` (a screen or page content) as view `v`.
static void mus_build(lv_obj_t *parent, int v) {
  MusicView &m = mus_views[v];
  memset(&m, 0, sizeof(m));
  m.root = kit_col_box(parent, LV_FLEX_ALIGN_CENTER, 0);
  lv_obj_set_width(m.root, LV_PCT(100));

  lv_obj_t *top = kit_container(m.root);
  lv_obj_set_size(top, LV_PCT(100), 44);
  m.repeat = mus_small_button(top, ICON_REPEAT, mus_repeat_cb, NULL);
  lv_obj_align(m.repeat, LV_ALIGN_LEFT_MID, 22, 0);
  m.source = kit_label(top, &font_icons_24, KIT_COLOR_TEXT2, "");
  lv_obj_set_style_text_font(m.source, &font_icons_24, 0);
  lv_obj_center(m.source);
  m.library = mus_small_button(top, ICON_LIST, mus_library_cb, NULL);
  lv_obj_align(m.library, LV_ALIGN_RIGHT_MID, -22, 0);

  m.art = kit_container(m.root);
  lv_obj_set_size(m.art, 112, 112);
  lv_obj_set_style_radius(m.art, 30, 0);
  lv_obj_set_style_bg_opa(m.art, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(m.art, lv_color_hex(KIT_COLOR_ACCENT), 0);
  lv_obj_set_style_bg_grad_color(m.art, lv_color_hex(0x7A1FA2), 0);
  lv_obj_set_style_bg_grad_dir(m.art, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_margin_top(m.art, 10, 0);
  lv_obj_t *note = kit_label(m.art, &font_icons_48, 0xFFFFFF, ICON_MUSIC);
  lv_obj_center(note);

  m.title = kit_label(m.root, KIT_FONT_TEXT, 0xFFFFFF, "");
  lv_obj_set_width(m.title, 320);
  lv_obj_set_style_text_align(m.title, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(m.title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_margin_top(m.title, 14, 0);
  m.artist = kit_label(m.root, KIT_FONT_BODY, KIT_COLOR_TEXT2, "");
  lv_obj_set_size(m.artist, 300, lv_font_get_line_height(KIT_FONT_BODY));
  lv_obj_set_style_text_align(m.artist, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(m.artist, LV_LABEL_LONG_MODE_DOTS);
  m.empty = kit_label(m.root, KIT_FONT_BODY, KIT_COLOR_TEXT2,
                      "No music yet. Copy .wav files to the SD card or connect the phone app.");
  lv_obj_set_width(m.empty, 300);
  lv_obj_set_style_text_align(m.empty, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(m.empty, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_margin_top(m.empty, 14, 0);

  lv_obj_t *progress = m.progress = kit_container(m.root);
  lv_obj_set_size(progress, 300, LV_SIZE_CONTENT);
  lv_obj_set_style_margin_top(progress, 14, 0);
  m.bar = lv_bar_create(progress);
  lv_obj_set_size(m.bar, 300, 6);
  lv_bar_set_range(m.bar, 0, 1000);
  lv_obj_set_style_bg_color(m.bar, lv_color_hex(KIT_COLOR_CARD3), 0);
  lv_obj_set_style_bg_opa(m.bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(m.bar, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_radius(m.bar, 3, 0);
  lv_obj_set_style_radius(m.bar, 3, LV_PART_INDICATOR);
  m.pos = kit_label(progress, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "0:00");
  lv_obj_align(m.pos, LV_ALIGN_TOP_LEFT, 0, 12);
  m.len = kit_label(progress, KIT_FONT_SMALL, KIT_COLOR_TEXT2, "--:--");
  lv_obj_align(m.len, LV_ALIGN_TOP_RIGHT, 0, 12);
  lv_obj_set_height(progress, 36);

  lv_obj_t *controls = kit_row_box(m.root, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
  lv_obj_set_style_pad_hor(controls, 8, 0);
  lv_obj_set_style_margin_top(controls, 10, 0);
  mus_small_button(controls, ICON_VOLUME_DOWN, mus_volume_cb, (void *)(intptr_t)-1);
  lv_obj_t *prev = kit_round_button(controls, ICON_PREV, KIT_COLOR_CARD2, 64);
  lv_obj_add_event_cb(prev, mus_prev_cb, LV_EVENT_CLICKED, NULL);
  m.play = kit_round_button(controls, ICON_PLAY, KIT_COLOR_ACCENT, 88);
  lv_obj_add_event_cb(m.play, mus_play_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *next = kit_round_button(controls, ICON_NEXT, KIT_COLOR_CARD2, 64);
  lv_obj_add_event_cb(next, mus_next_cb, LV_EVENT_CLICKED, NULL);
  mus_small_button(controls, ICON_VOLUME, mus_volume_cb, (void *)(intptr_t)1);

  lv_obj_add_event_cb(m.root, mus_view_deleted_cb, LV_EVENT_DELETE, (void *)(intptr_t)v);
  lv_subject_add_observer_obj(subj_music, mus_changed_obs, m.root, (void *)(intptr_t)v);
  lv_subject_add_observer_obj(subj_phone, mus_changed_obs, m.root, (void *)(intptr_t)v);
}

static void mus_loaded_cb(lv_event_t *e) {
  mus_update((int)(intptr_t)lv_event_get_user_data(e));
}

lv_obj_t *music_tile_create() {
  lv_obj_t *tile = face_screen_create();
  lv_obj_t *box = kit_container(tile);
  lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_top(box, 14, 0);
  mus_build(box, 0);
  lv_obj_add_event_cb(tile, mus_loaded_cb, LV_EVENT_SCREEN_LOAD_START, (void *)(intptr_t)0);
  static lv_timer_t *tick;
  if (!tick) tick = lv_timer_create(mus_tick_cb, 500, NULL);
  return tile;
}

void music_open() {
  lv_obj_t *page = kit_page_create(NULL, false);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_set_scrollable(c, false);
  lv_obj_set_style_pad_top(c, 14, 0);
  mus_build(c, 1);
  mus_update(1);
  kit_page_push(page);
}
