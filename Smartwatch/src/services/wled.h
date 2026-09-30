/*
 * wled.h - WLED lights on the same Wi-Fi (https://kno.wled.ge): on/off, brightness,
 * colour and effect over WLED's JSON API, discovery over mDNS (_wled._tcp), saved
 * devices in NVS. Every request runs on the network worker; results and state changes
 * are announced through subj_lights.
 */
#pragma once

#include <stdint.h>

#define WLED_MAX        8
#define WLED_FOUND_MAX  8

struct WledDevice {
  char name[32];
  char host[40];     // IP address or host name
  // Live state, read from the device (not stored).
  bool known;        // answered at least once since boot
  bool online;       // answered the last request
  bool busy;         // a request is on its way
  bool on;
  uint8_t bri;       // 1..255
  uint32_t color;    // 0xRRGGBB of the main segment
  uint8_t fx;        // effect id (0 = solid)
};

struct WledFound {
  char name[32];
  char host[40];
  bool saved;
};

void wled_init();
int wled_count();
const WledDevice *wled_get(int index);
bool wled_add(const char *name, const char *host);  // false: list full or already saved
void wled_remove(int index);

void wled_refresh(int index);         // read the device's state
void wled_refresh_all();
void wled_set_power(int index, bool on);
void wled_toggle_all();               // any on: all off, else all on (clap control)
bool wled_any_on();
void wled_set_brightness(int index, uint8_t bri);
void wled_set_color(int index, uint32_t rgb);
void wled_set_effect(int index, uint8_t fx);

bool wled_scan();                     // mDNS search on the current Wi-Fi; false when busy or offline
bool wled_scanning();
int wled_found_count();
const WledFound *wled_found(int index);
