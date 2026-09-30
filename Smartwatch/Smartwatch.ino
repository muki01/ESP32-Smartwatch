/*
 * Muki Watch - smartwatch firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.8
 *
 *   SH8601 368x448 AMOLED (QSPI), FT3168 touch, AXP2101 PMU, ES8311 codec + microphone,
 *   PCF85063 RTC, QMI8658 motion sensor, microSD
 *
 * Arduino IDE (esp32 core 3.x):
 *   Board            : Waveshare ESP32-S3-Touch-AMOLED-1.8
 *   PSRAM            : Enabled
 *   Partition Scheme : Custom (uses partitions.csv from this folder)
 *   USB CDC On Boot  : Enabled (serial log over USB)
 * Libraries: lvgl 9.6.x (configured by lv_conf.h in this folder), XPowersLib, SensorLib,
 *            NimBLE-Arduino 2.x, ArduinoJson 7.x
 *
 * The firmware lives in src/ (see src/app.cpp for the structure and start-up order).
 * Watch: 4 watch faces, tiles (activity, weather, music), quick panel, notifications,
 * always-on display. Apps: Activity, Workout, Sleep, Alarm, Timer, Stopwatch, Weather,
 * Music, Recorder, Calendar, Settings. Extras (Settings > Extras): Car control, WLED
 * lights, clap control.
 */
#include "src/app.h"

// LVGL callbacks, network setup and the settings UI run in the loop task: give it room.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  app_init();
}

void loop() {
  app_loop();
}
