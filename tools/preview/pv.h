// Preview controls: state of the stubbed drivers and services that main.cpp sets.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "drivers/audio.h"
#include "services/car_link.h"

extern uint32_t pv_ms;               // fake millis()
extern uint32_t pv_hw_steps;         // hardware step counter
extern bool pv_axes_calibrated;
extern MusicState pv_music;
extern CarLinkState pv_car_state;
extern CarLive pv_car_live;
extern bool pv_wled_scanned;

void pv_prefs_seed_bytes(const char *ns, const char *key, const void *data, size_t len);
void pv_prefs_seed_int(const char *ns, const char *key, int32_t v);
void pv_run_minute_handlers();
