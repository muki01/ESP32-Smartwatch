/*
 * notifications.h - The notification store: the newest NOTIFY_MAX notifications from the
 * phone (Gadgetbridge) and the watch itself (battery, alarms, activity), newest first.
 * The UI (src/ui/system/notification_center.cpp) shows them.
 */
#pragma once

#include <stdint.h>
#include <time.h>

#define NOTIFY_MAX 20

struct Notification {
  uint32_t id;          // local id
  uint32_t ext_id;      // id from the phone (0 = local)
  time_t time;
  uint32_t color;
  const char *icon;     // ICON_* string (static)
  char app[24];
  char title[64];
  char body[200];
};

typedef void (*notify_listener_t)(const Notification *n);

void notify_init();
void notify_set_listener(notify_listener_t cb);  // called for every new notification (banner, sound)
uint32_t notify_post(const char *app, const char *title, const char *body, const char *icon, uint32_t color);
void notify_post_ext(uint32_t ext_id, const char *app, const char *title, const char *body, const char *icon, uint32_t color);
void notify_remove(uint32_t id);
void notify_remove_ext(uint32_t ext_id);
void notify_clear_all();
void notify_mark_all_read();
int notify_count();
const Notification *notify_get(int index);       // 0 = newest
const Notification *notify_find(uint32_t id);
