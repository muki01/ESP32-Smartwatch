/*
 * notifications.cpp - Notification store (PSRAM), newest first. A phone notification with
 * a known id is replaced. subj_notif_count and subj_notif_unread follow the store; the
 * UI's listener hears about every new notification. Runs in the UI loop.
 */
#include "notifications.h"

#include <Arduino.h>
#include "../core/settings.h"
#include "../core/system.h"

static Notification *notify_list;
static int notify_n;
static uint32_t notify_next_id = 1;
static notify_listener_t notify_listener;

static int notify_index_of(uint32_t id) {
  for (int i = 0; i < notify_n; i++) {
    if (notify_list[i].id == id) return i;
  }
  return -1;
}

static int notify_index_of_ext(uint32_t ext_id) {
  for (int i = 0; i < notify_n; i++) {
    if (notify_list[i].ext_id == ext_id) return i;
  }
  return -1;
}

static void notify_remove_at(int i) {
  if (i < 0 || i >= notify_n) return;
  for (; i < notify_n - 1; i++) notify_list[i] = notify_list[i + 1];
  notify_n--;
  subj_set(subj_notif_count, notify_n);
  if (lv_subject_get_int(subj_notif_unread) > notify_n) subj_set(subj_notif_unread, notify_n);
}

static void notify_store(uint32_t ext_id, const char *app, const char *title, const char *body, const char *icon,
                         uint32_t color) {
  if (!notify_list) return;  // before notify_init()
  int old = ext_id ? notify_index_of_ext(ext_id) : -1;
  if (old >= 0) notify_remove_at(old);
  if (notify_n == NOTIFY_MAX) notify_n--;  // the oldest goes
  for (int i = notify_n; i > 0; i--) notify_list[i] = notify_list[i - 1];
  notify_n++;

  Notification &n = notify_list[0];
  memset(&n, 0, sizeof(n));
  n.id = notify_next_id++;
  n.ext_id = ext_id;
  n.time = time(NULL);
  n.color = color;
  n.icon = icon;
  strlcpy(n.app, app ? app : "", sizeof(n.app));
  strlcpy(n.title, title ? title : "", sizeof(n.title));
  strlcpy(n.body, body ? body : "", sizeof(n.body));
  subj_set(subj_notif_unread, lv_subject_get_int(subj_notif_unread) + 1);
  subj_set(subj_notif_count, notify_n);
  if (notify_listener) notify_listener(&n);
}

void notify_init() {
  notify_list = (Notification *)psram_calloc(NOTIFY_MAX, sizeof(Notification));
  notify_n = 0;
}

void notify_set_listener(notify_listener_t cb) {
  notify_listener = cb;
}

uint32_t notify_post(const char *app, const char *title, const char *body, const char *icon, uint32_t color) {
  notify_store(0, app, title, body, icon, color);
  return notify_list && notify_n ? notify_list[0].id : 0;
}

void notify_post_ext(uint32_t ext_id, const char *app, const char *title, const char *body, const char *icon, uint32_t color) {
  notify_store(ext_id, app, title, body, icon, color);
}

void notify_remove(uint32_t id) {
  notify_remove_at(notify_index_of(id));
}

void notify_remove_ext(uint32_t ext_id) {
  notify_remove_at(notify_index_of_ext(ext_id));
}

void notify_clear_all() {
  notify_n = 0;
  subj_set(subj_notif_count, 0);
  subj_set(subj_notif_unread, 0);
}

void notify_mark_all_read() {
  subj_set(subj_notif_unread, 0);
}

int notify_count() {
  return notify_n;
}

const Notification *notify_get(int index) {
  return index >= 0 && index < notify_n ? &notify_list[index] : NULL;
}

const Notification *notify_find(uint32_t id) {
  return notify_get(notify_index_of(id));
}
