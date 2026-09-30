/*
 * agenda.cpp - Calendar events from the phone: PSRAM list, debounced NVS storage.
 */
#include "agenda.h"

#include <Arduino.h>
#include <Preferences.h>
#include "../core/settings.h"
#include "../core/system.h"

#define AGENDA_MAX        24
#define AGENDA_NS         "agenda"
#define AGENDA_SAVE_MS    5000
#define AGENDA_MIN_EPOCH  1735689600  // before this the clock is not set

static AgendaEvent *agenda;            // PSRAM, AGENDA_MAX entries
static int agenda_n;
static lv_timer_t *agenda_save_timer;
static bool agenda_dirty;

void agenda_flush() {
  if (agenda_save_timer) lv_timer_pause(agenda_save_timer);
  if (!agenda_dirty) return;
  agenda_dirty = false;
  Preferences p;
  if (!p.begin(AGENDA_NS, false)) return;
  if (agenda_n) p.putBytes("events", agenda, agenda_n * sizeof(AgendaEvent));
  else p.remove("events");
  p.end();
}

static void agenda_save_cb(lv_timer_t *t) {
  agenda_flush();
}

static void agenda_changed() {
  agenda_dirty = true;
  subj_bump(subj_agenda);
  if (!agenda_save_timer) return;
  lv_timer_reset(agenda_save_timer);
  lv_timer_resume(agenda_save_timer);
}

// Drops events that are over.
static void agenda_prune() {
  time_t now = time(NULL);
  if (now < AGENDA_MIN_EPOCH) return;  // unknown time: keep everything
  int kept = 0;
  for (int i = 0; i < agenda_n; i++) {
    const AgendaEvent &e = agenda[i];
    time_t end = (time_t)e.start + (e.duration ? e.duration : 3600);
    if (end > now) agenda[kept++] = e;
  }
  if (kept != agenda_n) {
    agenda_n = kept;
    agenda_changed();
  }
}

void agenda_init() {
  agenda = (AgendaEvent *)psram_calloc(AGENDA_MAX, sizeof(AgendaEvent));
  if (!agenda) return;
  Preferences p;
  if (p.begin(AGENDA_NS, true)) {
    agenda_n = (int)(p.getBytes("events", agenda, AGENDA_MAX * sizeof(AgendaEvent)) / sizeof(AgendaEvent));
    p.end();
  }
  agenda_save_timer = lv_timer_create(agenda_save_cb, AGENDA_SAVE_MS, NULL);
  lv_timer_pause(agenda_save_timer);
}

void agenda_remove(uint32_t id) {
  for (int i = 0; i < agenda_n; i++) {
    if (agenda[i].id != id) continue;
    memmove(&agenda[i], &agenda[i + 1], (agenda_n - i - 1) * sizeof(AgendaEvent));
    agenda_n--;
    agenda_changed();
    return;
  }
}

// Sorted in by start time; when the list is full the latest event goes.
void agenda_put(const AgendaEvent *e) {
  if (!agenda) return;
  agenda_remove(e->id);
  agenda_prune();
  if (agenda_n == AGENDA_MAX) {
    if (agenda[AGENDA_MAX - 1].start <= e->start) return;
    agenda_n--;
  }
  int i = agenda_n;
  while (i > 0 && agenda[i - 1].start > e->start) {
    agenda[i] = agenda[i - 1];
    i--;
  }
  agenda[i] = *e;
  agenda_n++;
  agenda_changed();
}

int agenda_count() {
  if (agenda) agenda_prune();
  return agenda_n;
}

const AgendaEvent *agenda_get(int index) {
  return agenda && index >= 0 && index < agenda_n ? &agenda[index] : NULL;
}
