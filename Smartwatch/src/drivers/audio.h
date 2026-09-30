/*
 * audio.h - ES8311 codec + I2S: UI sounds, alert sounds, the WAV music player (files on
 * the SD card), the voice recorder and the microphone listener for clap control.
 * Every call returns at once: a single audio task owns the I2S bus and works through a
 * command queue.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

enum AlertSound : uint8_t { ALERT_ALARM, ALERT_TIMER, ALERT_FIND, ALERT_CALL };

struct MusicState {
  bool active;       // a track is open (playing or paused)
  bool playing;
  bool repeat_one;
  int index;         // current track, -1 = none yet
  int count;         // playable files found on the card
  uint32_t pos_s;
  uint32_t len_s;
};

void audio_init();

// ---- Sounds ------------------------------------------------------------------------
void audio_click();                       // touch feedback (touch sounds on, not silent)
void audio_beep(uint16_t freq_hz, uint16_t duration_ms);
void audio_notify();                      // notification chime (respects silent / do not disturb)
void audio_alert_start(AlertSound kind);  // repeating alarm / timer / call / find-my-watch sound
void audio_alert_stop();

// ---- Music player: 16-bit PCM WAV files in /, /music, /musics ----------------------
void music_get_state(MusicState *out);
void music_track_info(int index, char *artist, size_t artist_len, char *title, size_t title_len);
void music_toggle();
void music_next();
void music_prev();
void music_play(int index);
void music_set_repeat(bool one);

// ---- Voice recorder: 16 kHz mono WAV ------------------------------------------------
bool recorder_start(const char *path);
void recorder_stop();
bool recorder_play(const char *path);
void recorder_play_stop();
uint32_t recorder_seconds();              // recorded, or played of the recording being played
uint32_t recorder_level();                // 0..100 while recording

// ---- Microphone listener ------------------------------------------------------------
void audio_listen(bool on);               // keep the microphone open for clap detection
bool audio_take_clap();                   // a double clap was heard since the last call
