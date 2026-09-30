/*
 * wifi.h - Wi-Fi station: on/off (subj_wifi_enabled), scanning, connecting, saved
 * networks (most recent first, up to WIFI_SAVED_MAX, in NVS).
 */
#pragma once

#include <Arduino.h>

#define WIFI_SAVED_MAX 5

struct WifiNet {
  char ssid[33];
  int8_t rssi;
  bool secure;
  bool saved;
};

void wifi_init();
void wifi_scan_start();
bool wifi_is_scanning();
int wifi_result_count();
const WifiNet *wifi_result(int index);          // strongest first
void wifi_connect(const char *ssid, const char *pass);  // pass == NULL: the saved password
void wifi_disconnect();
void wifi_forget(const char *ssid);
const char *wifi_current_ssid();
const char *wifi_status_text();
int wifi_rssi();
String wifi_ip();
String wifi_gateway();
int wifi_channel();
String wifi_mac();
