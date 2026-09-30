/*
 * power.h - AXP2101 PMU: battery, charger and the side (power) key.
 *
 * The battery and charger state is published through subj_battery, subj_charging and
 * subj_usb_power; key presses and charger changes go to one event handler (the UI's
 * system reactions, src/ui/system/system_ui.cpp).
 */
#pragma once

#include <stdint.h>

struct PowerState {
  bool battery;
  bool vbus;      // USB power present
  bool charging;
  int32_t level;  // 0..100, -1 = no battery
};

enum PowerEvent : uint8_t {
  POWER_KEY_SHORT,    // side key pressed
  POWER_KEY_LONG,     // side key held (1.5 s)
  POWER_PLUGGED,      // USB power connected
  POWER_CHARGED,      // charging finished while plugged in
  POWER_LOW,          // the level fell to 20 % or 10 %
};

typedef void (*power_event_cb_t)(PowerEvent event, const PowerState *state);

void power_init();                               // PMU registers and interrupts (before the display)
void power_start(power_event_cb_t handler);      // polling, once the subjects exist
float power_battery_voltage();
float power_usb_voltage();
float power_temperature();
void power_shutdown();                           // cuts the power (callers store their data first)
