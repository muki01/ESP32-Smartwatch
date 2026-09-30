/*
 * step_counter.h - Step detection for a wrist-worn accelerometer.
 *
 * Fed with 3-axis samples (in g) at a fixed rate (50 Hz). Works on the magnitude, so
 * the orientation of the watch does not matter:
 *   1. gravity is removed with a slow low pass (~0.25 Hz),
 *   2. the rest is smoothed with a fast low pass (~3.6 Hz: walking and running cadence),
 *   3. every local maximum that rises far enough above the preceding valley is a step
 *      candidate (the threshold adapts to the walking intensity),
 *   4. candidates only become steps once STEP_CONFIRM of them came at a steady pace:
 *      every interval between 230 ms and 2 s, every stride (two steps) within -25/+33 %
 *      of the previous one.
 *      Strong peaks that come too fast (hand shaking) or at an uneven rhythm (gestures,
 *      a bumpy ride) restart the confirmation. A confirmed walk adds the buffered steps at once and
 *      then every further step.
 * Plain C++ without Arduino dependencies so it can be tested on a PC.
 */
#pragma once

#include <math.h>
#include <stdint.h>

class StepCounter {
 public:
  static constexpr float GRAVITY_HZ = 0.25f;       // gravity removal
  static constexpr float SMOOTH_HZ = 3.6f;         // step band low pass
  static constexpr float MIN_PEAK = 0.03f;          // g above the running mean
  static constexpr float MIN_SWING = 0.07f;         // g from valley to peak
  static constexpr float SWING_RATIO = 0.35f;       // of the average swing of recent steps
  static constexpr uint32_t MIN_INTERVAL_MS = 230;  // at most ~4.3 steps per second
  static constexpr uint32_t MAX_INTERVAL_MS = 2000; // slower: the walk has stopped
  static constexpr float STRIDE_MIN = 0.75f;        // allowed change of a stride (two steps)
  static constexpr float STRIDE_MAX = 1.33f;
  static constexpr float STRONG_RATIO = 0.60f;      // an early peak this strong is shaking
  static constexpr int STEP_CONFIRM = 8;

  explicit StepCounter(float sample_hz = 50.0f)
      : gravity_alpha_(1.0f - expf(-6.2831853f * GRAVITY_HZ / sample_hz)),
        smooth_alpha_(1.0f - expf(-6.2831853f * SMOOTH_HZ / sample_hz)) {}

  // Returns the number of steps this sample added (0, 1 or STEP_CONFIRM).
  int feed(float ax, float ay, float az, uint32_t now_ms) {
    float m = sqrtf(ax * ax + ay * ay + az * az);
    if (!started_) {
      gravity_ = m;
      started_ = true;
    }
    gravity_ += (m - gravity_) * gravity_alpha_;
    smooth_ += ((m - gravity_) - smooth_) * smooth_alpha_;

    int added = 0;
    if (smooth_ < valley_) valley_ = smooth_;
    // prev_ is a local maximum when it is higher than both neighbours.
    if (prev_ > prev2_ && prev_ >= smooth_ && prev_ > MIN_PEAK) {
      float swing = prev_ - valley_;
      float need = swing_avg_ * SWING_RATIO;
      if (need < MIN_SWING) need = MIN_SWING;
      if (swing >= need) added = candidate(now_ms, swing);
    }
    if (last_ms_ && now_ms - last_ms_ > MAX_INTERVAL_MS) {
      streak_ = 0;  // stopped: the next walk has to confirm itself again
      interval_ = interval2_ = 0;
      if (swing_avg_ > 0.25f) swing_avg_ = 0.25f;
    }
    prev2_ = prev_;
    prev_ = smooth_;
    total_ += added;
    return added;
  }

  uint32_t total() const { return total_; }
  bool walking() const { return streak_ >= STEP_CONFIRM; }

 private:
  int candidate(uint32_t now_ms, float swing) {
    uint32_t dt = now_ms - last_ms_;
#ifdef STEP_COUNTER_DEBUG
    STEP_COUNTER_DEBUG(now_ms, dt, swing, swing_avg_, streak_);
#endif
    valley_ = prev_;  // the next step needs a new valley first
    if (last_ms_ && dt < MIN_INTERVAL_MS) {
      // Too soon after the last step: a weaker bump within the same step is ignored,
      // a strong one means shaking (faster than anyone walks).
      if (swing >= swing_avg_ * STRONG_RATIO) {
        streak_ = 0;
        interval_ = interval2_ = 0;
        last_ms_ = now_ms;
      }
      return 0;
    }
    // Left and right steps can alternate between shorter and longer, so the rhythm is
    // judged on strides (two consecutive steps), which stay regular while walking.
    bool steady = last_ms_ && dt <= MAX_INTERVAL_MS;
    if (steady && interval_ && interval2_) {
      float stride = (float)(dt + interval_), before = (float)(interval_ + interval2_);
      steady = stride >= before * STRIDE_MIN && stride <= before * STRIDE_MAX;
    } else if (steady && interval_) {
      steady = dt * 2 >= interval_ && dt <= interval_ * 2;
    }
    streak_ = steady ? streak_ + 1 : 1;
    interval2_ = steady ? interval_ : 0;
    interval_ = (last_ms_ && dt <= MAX_INTERVAL_MS) ? dt : 0;
    last_ms_ = now_ms ? now_ms : 1;
    swing_avg_ += (swing - swing_avg_) * 0.25f;
    if (streak_ == STEP_CONFIRM) return STEP_CONFIRM;
    return streak_ > STEP_CONFIRM ? 1 : 0;
  }

  float gravity_alpha_;
  float smooth_alpha_;
  float gravity_ = 1.0f;
  float smooth_ = 0.0f;
  float prev_ = 0.0f;
  float prev2_ = 0.0f;
  float valley_ = 0.0f;
  float swing_avg_ = 0.25f;
  uint32_t last_ms_ = 0;
  uint32_t interval_ = 0;   // the last step interval
  uint32_t interval2_ = 0;  // the one before
  int streak_ = 0;
  uint32_t total_ = 0;
  bool started_ = false;
};
