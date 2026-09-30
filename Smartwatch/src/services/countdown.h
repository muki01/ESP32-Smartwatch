/*
 * countdown.h - The countdown timer. It keeps running when its page is closed; when it
 * reaches zero the finish handler (the UI's alert) is called.
 */
#pragma once

#include <stdint.h>

enum CountdownState : uint8_t { COUNTDOWN_IDLE, COUNTDOWN_RUNNING, COUNTDOWN_PAUSED };

typedef void (*countdown_done_cb_t)(uint32_t total_ms);

void countdown_init();
void countdown_set_done_handler(countdown_done_cb_t cb);
void countdown_start(uint32_t ms);
void countdown_pause();
void countdown_resume();
void countdown_cancel();
void countdown_add(uint32_t ms);
CountdownState countdown_state();
uint32_t countdown_remaining();
uint32_t countdown_total();
