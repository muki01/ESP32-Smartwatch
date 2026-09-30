/*
 * system_ui.h - How the UI reacts to system events: short messages (toasts), the side key,
 * the charger and battery, calls and "find my watch" from the phone, and the progress of
 * a wireless update.
 */
#pragma once

#include "../../drivers/power.h"
#include "../../services/phone.h"

void system_ui_init();  // message handler, update progress
void system_ui_on_power(PowerEvent event, const PowerState *state);
void system_ui_on_phone(PhoneEvent event, const PhoneCall *call);
