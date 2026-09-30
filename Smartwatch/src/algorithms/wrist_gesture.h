/*
 * wrist_gesture.h - "Raise to wake" and "lower to sleep" from the accelerometer.
 *
 * Fed with samples in the screen frame (g): x toward 3 o'clock, y toward 12 o'clock,
 * z out of the screen. A resting accelerometer reads +1 g along the axis pointing up.
 *
 *   viewing pose  screen up and turned toward the eyes: z >= 0.45, 12 o'clock not
 *                 pointing down (y >= -0.3), forearm roughly level (|x| <= 0.75)
 *   arm down      forearm hanging or screen turned away: z < 0.2, |x| > 0.85 or y < -0.55
 *
 * RAISE (screen off):
 *   - from arm down: the viewing pose, held steady for 160 ms, within 1.5 s of the arm
 *     being down, with the screen at least slightly turned toward the viewer (y >= 0.05),
 *   - from a desk or lap: in the viewing pose, the watch is tilted toward the eyes (y rose
 *     by 0.25 g within 0.8 s) and held there.
 *   Lowering the arm onto a table or the lap never matches: that ends flat or face-down,
 *   not turned toward the eyes.
 * LOWER (screen on): the arm stays down for 0.6 s.
 * Plain C++ without Arduino dependencies so it can be tested on a PC.
 */
#pragma once

#include <math.h>
#include <stdint.h>

class WristGesture {
 public:
  enum Event { NONE, RAISE, LOWER };

  static constexpr float VIEW_Z = 0.45f;
  static constexpr float VIEW_Y_MIN = -0.30f;
  static constexpr float VIEW_X_MAX = 0.75f;
  static constexpr float AWAY_Z = 0.20f;
  static constexpr float AWAY_X = 0.85f;
  static constexpr float AWAY_Y = -0.55f;
  static constexpr float STEADY_G = 0.12f;       // change between samples (sum of the axes)
  static constexpr float RAISE_Y_MIN = 0.05f;    // turned toward the viewer after an arm-down raise
  static constexpr float TILT_Y_RISE = 0.25f;    // desk -> eyes tilt
  static constexpr float TILT_Y_MIN = 0.25f;
  static constexpr uint32_t HOLD_MS = 160;
  static constexpr uint32_t RAISE_WINDOW_MS = 1500;
  static constexpr uint32_t TILT_WINDOW_MS = 800;
  static constexpr uint32_t LOWER_MS = 600;
  static constexpr uint32_t COOLDOWN_MS = 1000;
  static constexpr int HISTORY = 40;             // y history for the tilt, >= TILT_WINDOW_MS at 50 Hz

  void reset() {
    started_ = false;
    away_ms_ = 0;
    view_since_ = 0;
    away_since_ = 0;
    lower_sent_ = false;
    cooldown_until_ = 0;
    for (int i = 0; i < HISTORY; i++) y_hist_[i] = 0.0f;
    for (int i = 0; i < HISTORY; i++) t_hist_[i] = 0;
  }

  Event feed(float x, float y, float z, uint32_t now_ms, bool screen_on) {
    bool steady = started_ && fabsf(x - px_) + fabsf(y - py_) + fabsf(z - pz_) < STEADY_G;
    px_ = x;
    py_ = y;
    pz_ = z;
    started_ = true;
    y_hist_[pos_] = y;
    t_hist_[pos_] = now_ms;
    pos_ = (pos_ + 1) % HISTORY;

    bool view = z >= VIEW_Z && y >= VIEW_Y_MIN && fabsf(x) <= VIEW_X_MAX;
    bool away = z < AWAY_Z || fabsf(x) > AWAY_X || y < AWAY_Y;
    if (away) {
      away_ms_ = now_ms ? now_ms : 1;
      if (!away_since_) away_since_ = away_ms_;
    } else {
      away_since_ = 0;
      lower_sent_ = false;
    }
    if (view && steady) {
      if (!view_since_) view_since_ = now_ms ? now_ms : 1;
    } else {
      view_since_ = 0;
    }

    if (screen_on) {
      if (away_since_ && !lower_sent_ && now_ms - away_since_ >= LOWER_MS) {
        lower_sent_ = true;
        return LOWER;
      }
      return NONE;
    }

    if (!view_since_ || now_ms - view_since_ < HOLD_MS || (int32_t)(now_ms - cooldown_until_) < 0) return NONE;
    bool from_down = away_ms_ && view_since_ - away_ms_ <= RAISE_WINDOW_MS && y >= RAISE_Y_MIN;
    bool tilted = y >= TILT_Y_MIN && y - min_recent_y(now_ms) >= TILT_Y_RISE;
    if (!from_down && !tilted) return NONE;
    away_ms_ = 0;
    view_since_ = 0;
    cooldown_until_ = now_ms + COOLDOWN_MS;
    return RAISE;
  }

 private:
  float min_recent_y(uint32_t now_ms) const {
    float m = 1e9f;
    for (int i = 0; i < HISTORY; i++) {
      if (t_hist_[i] && now_ms - t_hist_[i] <= TILT_WINDOW_MS && y_hist_[i] < m) m = y_hist_[i];
    }
    return m;
  }

  bool started_ = false;
  float px_ = 0.0f, py_ = 0.0f, pz_ = 0.0f;
  float y_hist_[HISTORY] = {};
  uint32_t t_hist_[HISTORY] = {};
  int pos_ = 0;
  uint32_t away_ms_ = 0;         // the last sample with the arm down
  uint32_t view_since_ = 0;      // start of the current steady viewing pose
  uint32_t away_since_ = 0;      // start of the current arm-down stretch
  bool lower_sent_ = false;
  uint32_t cooldown_until_ = 0;
};
