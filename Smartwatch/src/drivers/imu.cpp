/*
 * imu.cpp - QMI8658 motion sensor.
 *
 * A task reads the accelerometer 50 times per second and feeds:
 *  - the step detector (algorithms/step_counter.h): steps counted from the wrist
 *    movement itself, in any orientation, robust against shaking, gestures and bumpy
 *    rides (a walk only counts once 8 steps came at a steady rhythm),
 *  - the wrist gesture detector (algorithms/wrist_gesture.h): raise to wake while the
 *    screen is off, lower to sleep while it is on; an event wakes the UI loop at once,
 *  - a movement count per minute for the last 24 hours (sleep tracking),
 *  - the latest sample (tilt, calibration).
 * Steps and movement use magnitudes only. Gestures and tilt need the real orientation of
 * the chip on the board. The default mapping: screen x = sensor y (Waveshare's
 * auto-rotation demo for this board), the chip on the back of the board (z flipped). Until
 * the user calibrates (flat, then upright; stored in NVS), touches check the z direction:
 * a touched screen faces the eyes, so if most touches find it facing down, the chip sits
 * the other way round and z and y flip.
 * The task never touches LVGL; the stack lives in PSRAM.
 */
#include "imu.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include "SensorQMI8658.hpp"
#include "../algorithms/step_counter.h"
#include "../algorithms/wrist_gesture.h"
#include "../core/system.h"

#define IMU_RATE_HZ          50
#define IMU_TASK_STACK       4096
#define IMU_ACT_MINUTES      1440    // movement per minute, last 24 hours
#define IMU_ACT_SCALE        10.0f   // count = sum of |dynamic acceleration| (g) over the minute x this
#define IMU_ACT_UNKNOWN      0xFFFF
#define IMU_MIN_EPOCH        1735689600  // 2025-01-01: clock not set before this
#define IMU_NS               "imu"
#define IMU_CHECK_VOTES      8       // touches with a clear screen direction before judging
#define IMU_CHECK_FLIP       6       // this many facing down: the default z is the wrong way
#define IMU_CHECK_G          0.5f    // |z| above this: a clear direction

// Screen axis i (0 = x toward 3 o'clock, 1 = y toward 12 o'clock, 2 = z out of the
// screen) = sign[i] * (sensor[src[i]] - offset[src[i]]).
struct ImuAxes {
  uint8_t version;
  int8_t src[3];
  int8_t sign[3];
  float offset[3];
};

static SensorQMI8658 imu;
static bool imu_ok;
static StepCounter imu_steps_detector(IMU_RATE_HZ);
static WristGesture imu_gesture;
static volatile uint32_t imu_steps;
static volatile float imu_x, imu_y, imu_z = 1.0f;  // sensor frame, latest sample
static volatile bool imu_screen_on = true;
static volatile int imu_gesture_event = IMU_GESTURE_NONE;
static volatile bool imu_gesture_restart;          // the axes changed: the detector starts over
// The chip sits on the back of the board: z points away from the screen and x/y are
// swapped against the screen axes. Replaced by the calibration.
static ImuAxes imu_axes = { 1, { 1, 0, 2 }, { 1, 1, -1 }, { 0, 0, 0 } };
static bool imu_axes_set;
static uint8_t imu_votes_up, imu_votes_down;  // default axes check (imu_note_viewing)

// Movement per minute: written by the sensor task, read by the UI.
static uint16_t *imu_act;                     // PSRAM ring, slot = epoch minute % IMU_ACT_MINUTES
static volatile int32_t imu_act_newest = -1;  // epoch minute of the newest finished slot
static int32_t imu_act_minute = -1;           // minute being summed (sensor task)
static float imu_act_sum;
static float imu_act_gravity;

static void imu_to_screen(const float s[3], float out[3]) {
  for (int i = 0; i < 3; i++) out[i] = imu_axes.sign[i] * (s[imu_axes.src[i]] - imu_axes.offset[imu_axes.src[i]]);
}

// Sums how much the wrist moves (acceleration magnitude minus gravity) per minute of
// wall-clock time. Minutes without data (clock changes) are marked unknown.
static void imu_act_feed(float x, float y, float z) {
  if (!imu_act) return;
  float m = sqrtf(x * x + y * y + z * z);
  if (imu_act_gravity == 0.0f) imu_act_gravity = m;
  imu_act_gravity += (m - imu_act_gravity) * 0.03f;  // ~0.25 Hz low pass
  time_t now = time(NULL);
  if (now < IMU_MIN_EPOCH) return;
  int32_t minute = (int32_t)(now / 60);
  if (minute != imu_act_minute) {
    if (imu_act_minute >= 0 && minute > imu_act_minute && minute - imu_act_minute <= IMU_ACT_MINUTES) {
      for (int32_t m2 = imu_act_minute + 1; m2 < minute; m2++) imu_act[m2 % IMU_ACT_MINUTES] = IMU_ACT_UNKNOWN;
      float count = imu_act_sum * IMU_ACT_SCALE;
      imu_act[imu_act_minute % IMU_ACT_MINUTES] = count > 65534.0f ? 65534 : (uint16_t)count;
      imu_act_newest = imu_act_minute;
    } else if (imu_act_minute >= 0) {  // the clock jumped: start over
      for (int i = 0; i < IMU_ACT_MINUTES; i++) imu_act[i] = IMU_ACT_UNKNOWN;
      imu_act_newest = -1;
    }
    imu_act_minute = minute;
    imu_act_sum = 0.0f;
  }
  imu_act_sum += fabsf(m - imu_act_gravity);
}

static void imu_task(void *arg) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    float s[3];
    bool ok;
    {
      I2CGuard lock;
      ok = imu.getAccelerometer(s[0], s[1], s[2]);
    }
    if (ok) {
      imu_x = s[0];
      imu_y = s[1];
      imu_z = s[2];
      uint32_t now = millis();
      if (imu_steps_detector.feed(s[0], s[1], s[2], now)) imu_steps = imu_steps_detector.total();
      if (imu_gesture_restart) {
        imu_gesture_restart = false;
        imu_gesture.reset();
      }
      float v[3];
      imu_to_screen(s, v);
      WristGesture::Event ev = imu_gesture.feed(v[0], v[1], v[2], now, imu_screen_on);
      if (ev != WristGesture::NONE) {
        imu_gesture_event = ev == WristGesture::RAISE ? IMU_GESTURE_RAISE : IMU_GESTURE_LOWER;
        system_ui_wake();
      }
      imu_act_feed(s[0], s[1], s[2]);
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / IMU_RATE_HZ));
  }
}

static void imu_store_axes(const ImuAxes &a) {
  imu_axes = a;
  imu_axes_set = true;
  imu_gesture_restart = true;
  Preferences p;
  if (p.begin(IMU_NS, false)) {
    p.putBytes("axes", &a, sizeof(a));
    p.end();
  }
  Serial.printf("[IMU] axes: x=%c%c y=%c%c z=%c%c\n", a.sign[0] > 0 ? '+' : '-', 'x' + a.src[0], a.sign[1] > 0 ? '+' : '-',
                'x' + a.src[1], a.sign[2] > 0 ? '+' : '-', 'x' + a.src[2]);
}

static void imu_load_axes() {
  Preferences p;
  if (!p.begin(IMU_NS, true)) return;
  ImuAxes a;
  if (p.getBytes("axes", &a, sizeof(a)) == sizeof(a) && a.version == 1) {
    imu_axes = a;
    imu_axes_set = true;
  }
  p.end();
}

/* ================================ Public API ====================================== */

void imu_init() {
  imu_load_axes();
  imu_gesture.reset();
  imu_act = (uint16_t *)psram_malloc(IMU_ACT_MINUTES * sizeof(uint16_t));
  if (imu_act) {
    for (int i = 0; i < IMU_ACT_MINUTES; i++) imu_act[i] = IMU_ACT_UNKNOWN;
  }
  {
    I2CGuard lock;
    imu_ok = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS) || imu.begin(Wire, QMI8658_H_SLAVE_ADDRESS);
    if (imu_ok) {
      // 125 Hz output with a ~17 Hz low pass: clean samples for the 50 Hz reader.
      imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_3);
      imu.enableAccelerometer();
      imu.disableGyroscope();
    }
  }
  if (!imu_ok) {
    Serial.println("[IMU] QMI8658 NOT FOUND - no step counting, no raise to wake");
    return;
  }
  task_create_psram(imu_task, "imu", IMU_TASK_STACK, NULL, 3, NULL, 0);
  Serial.printf("[IMU] QMI8658 ready, axes %s\n", imu_axes_set ? "calibrated" : "default");
}

bool imu_available() {
  return imu_ok;
}

bool imu_read_raw(float *x, float *y, float *z) {
  if (!imu_ok) return false;
  *x = imu_x;
  *y = imu_y;
  *z = imu_z;
  return true;
}

bool imu_read_accel(float *x, float *y, float *z) {
  if (!imu_ok) return false;
  const float s[3] = { imu_x, imu_y, imu_z };
  float out[3];
  imu_to_screen(s, out);
  *x = out[0];
  *y = out[1];
  *z = out[2];
  return true;
}

bool imu_axes_calibrated() {
  return imu_axes_set;
}

// The x axis follows from y and z (right-handed frame: x = y cross z).
void imu_calibrate_axes(int y_axis, int y_sign, int z_axis, int z_sign, float off_x, float off_y, float off_z) {
  int x_axis = 3 - y_axis - z_axis;
  // Sign of the permutation (y_axis, z_axis, x_axis): +1 for an even permutation of (0, 1, 2).
  int parity = ((y_axis + 1) % 3 == z_axis) ? 1 : -1;
  ImuAxes a;
  a.version = 1;
  a.src[0] = x_axis;
  a.src[1] = y_axis;
  a.src[2] = z_axis;
  a.sign[0] = y_sign * z_sign * parity;
  a.sign[1] = y_sign;
  a.sign[2] = z_sign;
  a.offset[0] = off_x;
  a.offset[1] = off_y;
  a.offset[2] = off_z;
  imu_store_axes(a);
}

void imu_note_viewing() {
  if (!imu_ok || imu_axes_set || imu_votes_up + imu_votes_down >= IMU_CHECK_VOTES) return;
  float x, y, z;
  imu_read_accel(&x, &y, &z);
  if (z > IMU_CHECK_G) imu_votes_up++;
  else if (z < -IMU_CHECK_G) imu_votes_down++;
  if (imu_votes_up + imu_votes_down < IMU_CHECK_VOTES) return;
  Serial.printf("[IMU] default axes check: screen up %u, down %u\n", imu_votes_up, imu_votes_down);
  if (imu_votes_down < IMU_CHECK_FLIP) return;
  ImuAxes a = imu_axes;  // x stays: flipping z and y keeps the frame right-handed
  a.sign[1] = -a.sign[1];
  a.sign[2] = -a.sign[2];
  imu_store_axes(a);
}

uint32_t imu_step_counter() {
  return imu_steps;
}

// Movement count of one wall-clock minute (epoch / 60), -1 when unknown.
int32_t imu_movement(int32_t epoch_minute) {
  int32_t newest = imu_act_newest;
  if (!imu_act || newest < 0 || epoch_minute > newest || epoch_minute <= newest - IMU_ACT_MINUTES) return -1;
  uint16_t v = imu_act[epoch_minute % IMU_ACT_MINUTES];
  return v == IMU_ACT_UNKNOWN ? -1 : v;
}

void imu_set_screen_on(bool on) {
  imu_screen_on = on;
  imu_gesture_event = IMU_GESTURE_NONE;  // an event for the other state is stale now
}

ImuGesture imu_take_gesture() {
  int ev = imu_gesture_event;
  if (ev != IMU_GESTURE_NONE) imu_gesture_event = IMU_GESTURE_NONE;
  return (ImuGesture)ev;
}
