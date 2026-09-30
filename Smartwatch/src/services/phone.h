/*
 * phone.h - The phone link: Gadgetbridge (Android) speaking the Bangle.js protocol over
 * the Nordic UART service (bluetooth.h). Notifications, calls, music control, weather,
 * calendar, time and "find my phone / find my watch".
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct PhoneMusic {
  bool valid;         // the phone reported a track
  bool playing;
  char artist[48];
  char track[64];
  uint32_t duration;  // s, 0 = unknown
  uint32_t position;  // s at position_ms
  uint32_t position_ms;
};

enum PhoneEvent : uint8_t {
  PHONE_CALL_INCOMING,  // name / number in the call info
  PHONE_CALL_ENDED,     // answered, rejected or hung up on the phone
  PHONE_FIND_START,     // the phone wants the watch to ring
  PHONE_FIND_STOP,
};

struct PhoneCall {
  char name[48];
  char number[24];
};

typedef void (*phone_event_cb_t)(PhoneEvent event, const PhoneCall *call);

void phone_init();
void phone_set_event_handler(phone_event_cb_t cb);
void phone_rx_line(const char *line);   // one line from the phone (UI loop)
void phone_on_connect();
bool phone_connected();                 // Gadgetbridge link is up
void phone_send(const char *json);
void phone_send_status();
void phone_find(bool ring);             // make the phone ring (or stop)
void phone_call_reply(bool accept);
void phone_music_cmd(const char *cmd);  // "play", "pause", "next", "previous", "volumeup", "volumedown"
const PhoneMusic *phone_music();
void phone_notify_dismiss(uint32_t ext_id);
