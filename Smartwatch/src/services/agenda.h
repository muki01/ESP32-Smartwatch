/*
 * agenda.h - Calendar events from the phone (Gadgetbridge "Sync calendar events"), kept
 * sorted by start time, past ones dropped, stored in NVS. Changes: subj_agenda.
 */
#pragma once

#include <stdint.h>

struct AgendaEvent {
  uint32_t id;
  uint32_t start;        // epoch seconds
  uint32_t duration;     // seconds
  uint8_t all_day;
  char title[48];
  char location[40];
};

void agenda_init();
void agenda_flush();                       // store pending changes now (before power off)
void agenda_put(const AgendaEvent *e);     // new or changed (same id)
void agenda_remove(uint32_t id);
int agenda_count();
const AgendaEvent *agenda_get(int index);  // upcoming events, soonest first
