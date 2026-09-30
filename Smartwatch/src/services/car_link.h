/*
 * car_link.h - Link to the car module ("OBD2 Master" ESP32: Wi-Fi access point with a
 * WebSocket server, default ws://192.168.4.1/ws). Receives live data as JSON
 * ({"LiveData":{"RPM":{"value":850},...}}) and sends the watch's control commands:
 *   {"cmd":"control","name":"low_beam","on":true}
 * Connects only while the Car app is open and Wi-Fi is connected (join the car's
 * network in Settings > Wi-Fi). State changes: subj_car.
 */
#pragma once

#include <stdint.h>

enum CarLinkState : uint8_t { CAR_LINK_OFF, CAR_LINK_CONNECTING, CAR_LINK_CONNECTED, CAR_LINK_FAILED };

struct CarLive {
  bool valid;
  int32_t rpm;
  int32_t speed;     // km/h
  int32_t coolant;   // °C
  int32_t intake;    // °C
  uint32_t age_ms;   // since the last message
};

void car_link_init();
void car_link_set_active(bool on);    // the Car app is open
CarLinkState car_link_state();
void car_link_live(CarLive *out);
bool car_link_command(const char *name, bool on);
const char *car_link_host();          // "192.168.4.1"
void car_link_set_host(const char *host);
