/*
 * calendar_app.cpp - Calendar: a month view starting on Monday; today is highlighted, days
 * with events from the phone have a dot (tap one for its events). Swipe up/down or use
 * the arrows to change the month, tap the month name to return to today. The next event
 * sits below the month. Events: services/agenda.cpp.
 */
#include "apps.h"

#include <Arduino.h>
#include "../../drivers/audio.h"
#include "../../services/agenda.h"
#include "../../services/clock.h"
#include "../kit/kit.h"

static const char *const CAL_MONTHS[] = { "January", "February", "March", "April", "May", "June", "July",
                                          "August", "September", "October", "November", "December" };
static const char *const CAL_MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *const CAL_WD[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static int cal_year, cal_month;  // month shown (1..12)
static lv_obj_t *cal_title, *cal_grid;

/* ================================ Agenda list ===================================== */

// "Today, 15:00", "Tomorrow, all day", "Thu 2 Oct, 09:30"
static void agenda_format_when(const AgendaEvent &e, char *buf, size_t len) {
  time_t now = time(NULL), start = e.start;
  struct tm tn, ts;
  localtime_r(&now, &tn);
  localtime_r(&start, &ts);
  char day[24], hm[16];
  struct tm a = tn, b = ts;  // calendar days apart: both dates at noon
  a.tm_hour = b.tm_hour = 12;
  a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
  a.tm_isdst = b.tm_isdst = -1;
  int days = (int)lround(difftime(mktime(&b), mktime(&a)) / 86400.0);
  if (days == 0) strlcpy(day, "Today", sizeof(day));
  else if (days == 1) strlcpy(day, "Tomorrow", sizeof(day));
  else snprintf(day, sizeof(day), "%s %d %s", CAL_WD[ts.tm_wday], ts.tm_mday, CAL_MON[ts.tm_mon]);
  if (e.all_day) {
    snprintf(buf, len, "%s, all day", day);
    return;
  }
  clock_format_hm(ts.tm_hour, ts.tm_min, hm, sizeof(hm));
  snprintf(buf, len, "%s, %s", day, hm);
}

static lv_obj_t *agenda_row(lv_obj_t *parent, const AgendaEvent &e) {
  char when[64], sub[112];  // when + separator + location
  agenda_format_when(e, when, sizeof(when));
  if (e.location[0]) snprintf(sub, sizeof(sub), "%s  \xE2\x80\xA2  %s", when, e.location);
  else strlcpy(sub, when, sizeof(sub));
  lv_obj_t *row = kit_row(parent, ICON_CALENDAR, KIT_COLOR_RED, e.title[0] ? e.title : "Event", sub, false);
  lv_obj_set_clickable(row, false);
  return row;
}

// Events of one day (y, m 1..12, d), or every upcoming one when d == 0.
static void agenda_open_list(int y, int m, int d) {
  char title[24];
  if (d) snprintf(title, sizeof(title), "%d %s", d, CAL_MON[m - 1]);
  else strlcpy(title, "Upcoming", sizeof(title));
  lv_obj_t *page = kit_page_create(title, true);
  lv_obj_t *c = kit_page_content(page);
  int shown = 0, n = agenda_count();
  for (int i = 0; i < n; i++) {
    const AgendaEvent &e = *agenda_get(i);
    time_t start = e.start;
    struct tm ts;
    localtime_r(&start, &ts);
    if (d && (ts.tm_year + 1900 != y || ts.tm_mon + 1 != m || ts.tm_mday != d)) continue;
    agenda_row(c, e);
    shown++;
  }
  if (!shown) {
    kit_empty_state(c, ICON_CALENDAR, d ? "No events on this day." : "No upcoming events. Turn on calendar sync for the watch in Gadgetbridge.");
  }
  kit_page_push(page);
}

/* ================================ Month view ====================================== */

static int cal_days_in_month(int y, int m) {
  static const uint8_t days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return days[m - 1] + (m == 2 && leap);
}

// 0 = Monday ... 6 = Sunday
static int cal_weekday(int y, int m, int d) {
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y -= 1;
  int sunday_based = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
  return (sunday_based + 6) % 7;
}

static void cal_day_cb(lv_event_t *e) {
  audio_click();
  agenda_open_list(cal_year, cal_month, (int)(intptr_t)lv_event_get_user_data(e));
}

static void cal_fill() {
  if (!cal_grid) return;
  lv_label_set_text_fmt(cal_title, "%s %d", CAL_MONTHS[cal_month - 1], cal_year);
  lv_obj_clean(cal_grid);

  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  bool this_month = t.tm_year + 1900 == cal_year && t.tm_mon + 1 == cal_month;

  static const char *const HEAD[] = { "M", "T", "W", "T", "F", "S", "S" };
  for (int i = 0; i < 7; i++) {
    lv_obj_t *h = kit_label(cal_grid, KIT_FONT_SMALL, i >= 5 ? KIT_COLOR_ACCENT : KIT_COLOR_TEXT2, HEAD[i]);
    lv_obj_set_size(h, 44, 24);
    lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  }
  bool marked[32] = { false };
  for (int i = 0, n = agenda_count(); i < n; i++) {
    time_t start = agenda_get(i)->start;
    struct tm ts;
    localtime_r(&start, &ts);
    if (ts.tm_year + 1900 == cal_year && ts.tm_mon + 1 == cal_month) marked[ts.tm_mday] = true;
  }
  int first = cal_weekday(cal_year, cal_month, 1);
  int days = cal_days_in_month(cal_year, cal_month);
  for (int i = 0; i < first; i++) {
    lv_obj_t *blank = kit_container(cal_grid);
    lv_obj_set_size(blank, 44, 36);
  }
  for (int d = 1; d <= days; d++) {
    char num[4];
    snprintf(num, sizeof(num), "%d", d);
    int wd = (first + d - 1) % 7;
    bool today = this_month && d == t.tm_mday;
    lv_obj_t *cell = kit_label(cal_grid, KIT_FONT_BODY, today ? 0xFFFFFF : wd >= 5 ? 0xFF8A80 : 0xFFFFFF, num);
    lv_obj_set_size(cell, 44, 36);
    lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(cell, (36 - lv_font_get_line_height(KIT_FONT_BODY)) / 2 - 1, 0);
    lv_obj_set_clickable(cell, true);
    lv_obj_set_style_opa(cell, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_add_event_cb(cell, cal_day_cb, LV_EVENT_CLICKED, (void *)(intptr_t)d);
    if (today) {
      lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(cell, lv_color_hex(KIT_COLOR_ACCENT), 0);
      lv_obj_set_style_radius(cell, LV_RADIUS_CIRCLE, 0);
    }
    if (marked[d]) {
      lv_obj_t *dot = kit_container(cell);
      lv_obj_set_size(dot, 6, 6);
      lv_obj_set_style_radius(dot, 3, 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(dot, lv_color_hex(today ? 0xFFFFFF : KIT_COLOR_ACCENT), 0);
      lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -(36 - lv_font_get_line_height(KIT_FONT_BODY)) / 2 + 2);
    }
  }
}

static void cal_shift(int delta) {
  cal_month += delta;
  if (cal_month < 1) cal_month = 12, cal_year--;
  if (cal_month > 12) cal_month = 1, cal_year++;
  cal_fill();
}

static void cal_today() {
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  cal_year = t.tm_year + 1900;
  cal_month = t.tm_mon + 1;
}

static void cal_prev_cb(lv_event_t *e) {
  audio_click();
  cal_shift(-1);
}

static void cal_next_cb(lv_event_t *e) {
  audio_click();
  cal_shift(1);
}

static void cal_title_cb(lv_event_t *e) {
  audio_click();
  cal_today();
  cal_fill();
}

static void cal_gesture_cb(lv_event_t *e) {
  lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
  if (dir == LV_DIR_TOP) cal_shift(1);
  else if (dir == LV_DIR_BOTTOM) cal_shift(-1);
}

static void cal_deleted_cb(lv_event_t *e) {
  cal_grid = cal_title = NULL;
}

static void cal_upcoming_cb(lv_event_t *e) {
  audio_click();
  agenda_open_list(0, 0, 0);
}

void calendar_open() {
  cal_today();
  lv_obj_t *page = kit_page_create("Calendar", true);
  lv_obj_t *c = kit_page_content(page);
  lv_obj_set_scrollable(c, false);  // vertical swipes change the month
  lv_obj_set_style_pad_row(c, 6, 0);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *head = kit_row_box(c, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
  lv_obj_t *prev = kit_round_button(head, ICON_CHEVRON_LEFT, KIT_COLOR_CARD, 44);
  lv_obj_add_event_cb(prev, cal_prev_cb, LV_EVENT_CLICKED, NULL);
  cal_title = kit_label(head, KIT_FONT_TEXT, 0xFFFFFF, "");
  lv_obj_set_clickable(cal_title, true);
  lv_obj_set_ext_click_area(cal_title, 10);
  lv_obj_add_event_cb(cal_title, cal_title_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *next = kit_round_button(head, ICON_CHEVRON_RIGHT, KIT_COLOR_CARD, 44);
  lv_obj_add_event_cb(next, cal_next_cb, LV_EVENT_CLICKED, NULL);

  cal_grid = kit_container(c);
  lv_obj_set_size(cal_grid, 7 * 44 + 6 * 4, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cal_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(cal_grid, 4, 0);
  lv_obj_set_style_pad_row(cal_grid, 2, 0);

  if (agenda_count()) {
    lv_obj_t *upcoming = agenda_row(c, *agenda_get(0));
    lv_obj_set_style_min_height(upcoming, 60, 0);
    lv_obj_set_clickable(upcoming, true);
    lv_obj_add_event_cb(upcoming, cal_upcoming_cb, LV_EVENT_CLICKED, NULL);
  }

  lv_obj_add_event_cb(page, cal_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(page, cal_deleted_cb, LV_EVENT_DELETE, NULL);
  cal_fill();
  kit_page_push(page);
}
