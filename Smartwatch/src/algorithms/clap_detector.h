/*
 * clap_detector.h - Double-clap detection on microphone audio.
 *
 * Fed with blocks of 16-bit mono samples (8 ms at 16 kHz). A clap is a short, sharp
 * sound: the block level jumps far above the background noise (adaptive noise floor)
 * and falls back within ~50 ms. Speech, music and a slammed door that echoes stay loud
 * longer and are ignored. Two claps 0.15-0.7 s apart followed by 0.35 s of quiet are a
 * double clap; a third clap (applause, rhythm) cancels the pattern until it is quiet for
 * a second.
 * Plain C++ without Arduino dependencies so it can be tested on a PC.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

class ClapDetector {
 public:
  enum Sensitivity { LOW_SENS, NORMAL_SENS, HIGH_SENS };

  static constexpr uint32_t DECAY_MS = 56;        // a clap is over within this time
  static constexpr uint32_t REFRACTORY_MS = 100;  // one clap, one event
  static constexpr uint32_t PAIR_MIN_MS = 150;    // second clap at the earliest ...
  static constexpr uint32_t PAIR_MAX_MS = 700;    // ... and at the latest
  static constexpr uint32_t QUIET_MS = 350;       // after the pair: nothing more
  static constexpr uint32_t LOCKOUT_MS = 1000;    // after applause

  ClapDetector() { set_sensitivity(NORMAL_SENS); }

  void set_sensitivity(Sensitivity s) {
    static const int32_t RATIO[] = { 10, 6, 4 };         // onset level over the noise floor
    static const int32_t MIN_MEAN[] = { 1800, 1100, 700 };
    ratio_ = RATIO[s];
    min_mean_ = MIN_MEAN[s];
  }

  void reset() {
    floor_q8_ = 200 << 8;
    prev_mean_ = 0;
    cand_ms_ = 0;
    last_clap_ms_ = 0;
    first_ms_ = 0;
    pair_ms_ = 0;
    lockout_until_ = 0;
  }

  // Returns true when this block completes a double clap.
  bool feed(const int16_t *s, size_t n, uint32_t now_ms) {
    if (!n) return false;
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += abs((int32_t)s[i]);
    int32_t mean = (int32_t)(sum / (int64_t)n);

    bool event = false;
    int32_t floor = floor_q8_ >> 8;
    if (cand_ms_) {  // a candidate must decay quickly to count as a clap
      if (mean * 100 < cand_mean_ * 35) {
        clap(cand_ms_);
        cand_ms_ = 0;
      } else if (now_ms - cand_ms_ > DECAY_MS) {
        cand_ms_ = 0;  // stayed loud: not a clap
        lockout_until_ = now_ms + 300;
      }
    } else if (mean >= min_mean_ && mean >= floor * ratio_ && mean >= prev_mean_ * 3 &&
               now_ms - last_clap_ms_ > REFRACTORY_MS && (int32_t)(now_ms - lockout_until_) >= 0) {
      cand_ms_ = now_ms ? now_ms : 1;
      cand_mean_ = mean;
    }

    // Background level (fixed point, 8 fraction bits): follows quiet blocks within a
    // quarter second and loud ones only over several seconds.
    if (!cand_ms_) floor_q8_ += ((mean << 8) - floor_q8_) / (mean < floor * 3 ? 32 : 512);
    if (floor_q8_ < (50 << 8)) floor_q8_ = 50 << 8;
    prev_mean_ = mean;

    if (pair_ms_ && now_ms - pair_ms_ >= QUIET_MS) {
      pair_ms_ = 0;
      first_ms_ = 0;
      event = true;
    }
    if (first_ms_ && !pair_ms_ && now_ms - first_ms_ > PAIR_MAX_MS) first_ms_ = 0;
    return event;
  }

 private:
  void clap(uint32_t t) {
    last_clap_ms_ = t;
    if (pair_ms_) {  // a third clap: applause or a rhythm, not a command
      pair_ms_ = 0;
      first_ms_ = 0;
      lockout_until_ = t + LOCKOUT_MS;
      return;
    }
    if (first_ms_ && t - first_ms_ >= PAIR_MIN_MS && t - first_ms_ <= PAIR_MAX_MS) {
      pair_ms_ = t;
      return;
    }
    first_ms_ = t;
  }

  int32_t ratio_ = 6;
  int32_t min_mean_ = 1100;
  int32_t floor_q8_ = 200 << 8;
  int32_t prev_mean_ = 0;
  int32_t cand_mean_ = 0;
  uint32_t cand_ms_ = 0;
  uint32_t last_clap_ms_ = 0;
  uint32_t first_ms_ = 0;
  uint32_t pair_ms_ = 0;
  uint32_t lockout_until_ = 0;
};
