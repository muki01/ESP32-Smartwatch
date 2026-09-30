/*
 * battery_saver.h - Battery saver (subj_saver): Wi-Fi off, no always-on display,
 * brightness at most 40 %, screen timeout at most 10 s. Turning it off restores the
 * previous values, also across a restart.
 */
#pragma once

void battery_saver_init();
