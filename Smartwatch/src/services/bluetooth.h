/*
 * bluetooth.h - Bluetooth LE peripheral (NimBLE): battery, device information, current
 * time and the Nordic UART service the Gadgetbridge phone link uses (phone.h).
 * On/off follows subj_bt_enabled; the link state is subj_bt_state.
 */
#pragma once

#include <stddef.h>

void bt_init();
const char *bt_address();
const char *bt_peer_address();
const char *bt_name();
bool bt_uart_send(const char *data, size_t len);  // Nordic UART TX to the phone
