/*
 * imu.h - QMI8658 motion sensor: step counting, wrist gestures (raise to wake, lower to
 * sleep), movement per minute (sleep tracking) and tilt in screen coordinates.
 */
#pragma once

#include <stdint.h>

enum ImuGesture : int { IMU_GESTURE_NONE, IMU_GESTURE_RAISE, IMU_GESTURE_LOWER };

void imu_init();
bool imu_available();

// Latest sample in g. Screen frame: x toward 3 o'clock, y toward 12 o'clock, z out of the
// screen (needs the calibration for exact results). Sensor frame: as the chip reports it.
bool imu_read_accel(float *x, float *y, float *z);
bool imu_read_raw(float *x, float *y, float *z);

// Axis calibration (Settings > Display > Calibrate motion sensor): which sensor axis
// points to 12 o'clock and which out of the screen (+1/-1), plus the zero offsets.
bool imu_axes_calibrated();
void imu_calibrate_axes(int y_axis, int y_sign, int z_axis, int z_sign, float off_x, float off_y, float off_z);
// The user is touching the screen, so it faces their eyes: checks the default axes until
// they are calibrated (UI loop).
void imu_note_viewing();

uint32_t imu_step_counter();                 // steps counted since boot
int32_t imu_movement(int32_t epoch_minute);  // wrist movement in that minute, -1 = unknown

void imu_set_screen_on(bool on);             // raise events come while off, lower events while on
ImuGesture imu_take_gesture();               // the latest gesture, reported once
