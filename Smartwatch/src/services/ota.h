/*
 * ota.h - Wireless firmware update from a web browser (subj_ota). The watch serves an
 * upload page while Wi-Fi is connected; a 6-digit PIN protects it.
 */
#pragma once

enum OtaPhase : int { OTA_IDLE, OTA_RECEIVING, OTA_DONE, OTA_FAILED };

void ota_init();
OtaPhase ota_phase();
int ota_percent();
const char *ota_status_text();  // "Off", "Waiting for Wi-Fi", "http://192.168.1.5\nPIN 123456", ...
